use super::*;
use ac2_gun_friends::{Event, Friend, Request};
use std::path::Path;

impl Hub {
    /// Opens the one account roster shared by the game world and its optional TCP adapter.
    pub fn open(data: &Path, auth: GunAuth) -> io::Result<Arc<Self>> {
        let file = data.join("gun-users.json");
        let mut users: BTreeMap<String, User> = match std::fs::read_to_string(&file) {
            Ok(text) => serde_json::from_str(&text)
                .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?,
            Err(e) if e.kind() == io::ErrorKind::NotFound => BTreeMap::new(),
            Err(e) => return Err(e),
        };
        // Older adapters could add Bob, Bob@AC2e, and Bob@another separately even
        // though all three resolve to the same local peer. Preserve the first spelling.
        for user in users.values_mut() {
            for list in [&mut user.friends, &mut user.ignored] {
                let mut seen = BTreeSet::new();
                list.retain(|name| seen.insert(base(name)));
            }
        }
        Ok(Arc::new(Self {
            state: Mutex::new(State {
                users,
                ..State::default()
            }),
            pending_online: Mutex::new(None),
            file,
            auth,
        }))
    }

    /// Takes ownership identity from the authenticated game session.
    pub fn world_request(&self, account: &str, request: &Request) -> Event {
        self.world_request_for_character(account, "", request)
    }

    pub fn world_request_for_character(
        &self,
        account: &str,
        character: &str,
        request: &Request,
    ) -> Event {
        let account = account.to_ascii_lowercase();
        let mut state = lock(self);
        self.apply_pending(&mut state);
        match request {
            Request::List { cx } => match state.checked_friends(&account) {
                Ok(friends) => Event::List {
                    cx: *cx,
                    hr: S_OK,
                    friends,
                },
                Err(hr) => Event::List {
                    cx: *cx,
                    hr,
                    friends: Vec::new(),
                },
            },
            Request::Flags {
                cx,
                op,
                word,
                flags,
            } => {
                let key = character.to_ascii_lowercase();
                let mut hr = S_OK;
                match (*word, *op, *flags) {
                    _ if key.is_empty() => hr = ZT_E_INVALID_ARG,
                    (1, 0, _) => {}
                    (1, 1, 1 | 2 | 4) => {
                        let before = state.availability(&key);
                        state.world_status.insert(key.clone(), *flags);
                        if before != *flags {
                            state.friend_revision = state.friend_revision.wrapping_add(1);
                            state.announce(&BTreeSet::from([key.clone()]));
                        }
                    }
                    _ => hr = ZT_E_INVALID_ARG,
                }
                Event::Flags {
                    cx: *cx,
                    hr,
                    word: *word,
                    flags: if hr == S_OK {
                        state.availability(&key)
                    } else {
                        0
                    },
                }
            }
            Request::Add { cx, name } | Request::Delete { cx, name } => {
                let add = matches!(request, Request::Add { .. });
                let hr = state.edit_list(&self.file, &account, Some(name), true, add);
                if hr == S_OK {
                    state.refresh_adapter_roster(&account, None);
                }
                if add {
                    Event::Added { cx: *cx, hr }
                } else {
                    Event::Deleted { cx: *cx, hr }
                }
            }
        }
    }

    pub fn world_friends(&self, account: &str) -> Vec<Friend> {
        let mut state = lock(self);
        self.apply_pending(&mut state);
        state.friends(&account.to_ascii_lowercase())
    }

    /// A legacy roster may exceed the world message limit even after a deletion.
    pub fn try_world_friends(&self, account: &str) -> Result<Vec<Friend>, u32> {
        let mut state = lock(self);
        self.apply_pending(&mut state);
        state.checked_friends(&account.to_ascii_lowercase())
    }

    pub fn world_revision(&self) -> u64 {
        let mut state = lock(self);
        self.apply_pending(&mut state);
        state.friend_revision
    }

    /// Publish the character's departure and clear its session status in one transition.
    pub fn leave_world(&self, character: &str) {
        let mut state = lock(self);
        self.apply_pending(&mut state);
        let key = character.to_ascii_lowercase();
        let owner = state.characters.remove(&key);
        let status = state.world_status.remove(&key);
        if owner.is_some() || status.is_some() {
            state.friend_revision = state.friend_revision.wrapping_add(1);
            let mut changed = BTreeSet::from([key]);
            changed.extend(owner);
            state.announce(&changed);
        }
    }

    pub fn update_online(&self, online: impl IntoIterator<Item = (String, String)>) {
        let now = online
            .into_iter()
            .map(|(name, account)| (name.to_ascii_lowercase(), account.to_ascii_lowercase()))
            .collect();
        *self
            .pending_online
            .lock()
            .unwrap_or_else(|poison| poison.into_inner()) = Some(now);
        flush_online(self);
    }

    pub(super) fn apply_pending(&self, state: &mut State) {
        let Some(now) = self
            .pending_online
            .lock()
            .unwrap_or_else(|poison| poison.into_inner())
            .take()
        else {
            return;
        };
        if state.characters != now {
            let mut changed = BTreeSet::new();
            state
                .world_status
                .retain(|name, _| match state.characters.get(name) {
                    Some(owner) => now.get(name) == Some(owner),
                    None => true, // A game session can set its status before the first world snapshot.
                });
            for (before, after) in [(&state.characters, &now), (&now, &state.characters)] {
                for (name, owner) in before {
                    if after.get(name) != Some(owner) {
                        changed.insert(name.clone());
                        changed.insert(owner.clone());
                    }
                }
            }
            state.characters = now;
            state.friend_revision = state.friend_revision.wrapping_add(1);
            state.announce(&changed);
        }
    }
}

impl State {
    fn checked_friends(&self, account: &str) -> Result<Vec<Friend>, u32> {
        if self
            .users
            .get(account)
            .is_some_and(|user| !world_roster_fits(&user.friends))
        {
            return Err(ZT_E_TOO_MANY);
        }
        Ok(self.friends(account))
    }
    fn availability(&self, character: &str) -> u32 {
        if let Some(&flags) = self.world_status.get(character) {
            return flags;
        }
        let mode = self
            .characters
            .get(character)
            .map(|owner| self.character_presence(character, owner).0)
            .unwrap_or(0);
        if mode & 4 != 0 {
            4
        } else if mode & 2 != 0 {
            2
        } else {
            1
        }
    }

    pub(super) fn friends(&self, account: &str) -> Vec<Friend> {
        let mut friends: Vec<Friend> = self
            .users
            .get(account)
            .into_iter()
            .flat_map(|user| &user.friends)
            .map(|name| {
                let key = base(name);
                // Game friends are characters. An account with the same name playing
                // a different character must not make this character appear online.
                let (mode, location) = match self.characters.get(&key) {
                    Some(owner) => self.character_presence(&key, owner),
                    None => (0, String::new()),
                };
                Friend {
                    name: name.clone(),
                    mode,
                    location,
                }
            })
            .collect();
        fit_optional_locations(&mut friends);
        friends
    }

    pub(super) fn character_presence(&self, who: &str, owner: &str) -> (u32, String) {
        let mut mode = MODE_IN_GAME | MODE_LOGGED_IN;
        let mut location = String::new();
        for session in self
            .sessions
            .values()
            .filter(|s| s.account.as_deref() == Some(owner))
        {
            mode |= session.flags & (2 | 4);
            if location.is_empty() {
                location.clone_from(&session.location);
            }
        }
        if let Some(flags) = self.world_status.get(who) {
            mode = MODE_IN_GAME | MODE_LOGGED_IN | flags;
        }
        (mode, location)
    }

    pub(super) fn refresh_adapter_roster(&self, account: &str, except: Option<u64>) {
        let mut recipients = self
            .sessions
            .iter()
            .filter(|(id, session)| {
                Some(**id) != except && session.account.as_deref() == Some(account)
            })
            .map(|(id, _)| *id)
            .peekable();
        if recipients.peek().is_none() {
            return;
        }
        let mut snapshot = Elem::new("FStart");
        snapshot.children = self.friends_elems(account);
        for id in recipients {
            self.deliver(id, &snapshot);
        }
    }

    pub(super) fn edit_list(
        &mut self,
        file: &Path,
        account: &str,
        name: Option<&str>,
        friends: bool,
        add: bool,
    ) -> u32 {
        let who = match check_name(name) {
            Ok(who) => who,
            Err(hr) => return hr,
        };
        let key = base(who);
        if key.trim().is_empty() {
            return ZT_E_INVALID_ARG;
        }
        let user = self.users.entry(account.to_owned()).or_default();
        let previous = user.clone();
        let list = if friends {
            &mut user.friends
        } else {
            &mut user.ignored
        };
        let at = list.iter().position(|name| base(name) == key);
        match (add, at) {
            (true, Some(_)) => return ZT_E_ALREADY_IN_LIST,
            (true, None) if list.len() >= MAX_LIST => return ZT_E_TOO_MANY,
            (true, None) => list.push(who.to_owned()),
            (false, Some(index)) => {
                list.remove(index);
            }
            (false, None) => return ZT_E_NOT_IN_LIST,
        }
        if friends && add && !world_roster_fits(list) {
            list.pop();
            return ZT_E_TOO_MANY;
        }
        if self.save(file).is_err() {
            self.users.insert(account.to_owned(), previous);
            return 0x80004005; // E_FAIL. The mutation was not committed.
        }
        if friends {
            self.friend_revision = self.friend_revision.wrapping_add(1);
        }
        S_OK
    }

    fn save(&self, path: &Path) -> io::Result<()> {
        let temporary = path.with_extension("json.tmp");
        let mut file = std::fs::File::create(&temporary)?;
        serde_json::to_writer_pretty(&mut file, &self.users)?;
        file.sync_all()?;
        drop(file);
        std::fs::rename(temporary, path)
    }
}

// Ordinary AC2 names (up to 32 ASCII bytes) fit at the 256-entry cap.
// The older adapter also accepts escaped and Unicode names up to 63
// characters. Reserve enough bytes for the largest possible in-game status.
fn world_roster_fits(names: &[String]) -> bool {
    if names.len() > MAX_LIST {
        return false;
    }
    let friends = names
        .iter()
        .map(|name| Friend {
            name: name.clone(),
            mode: MODE_IN_GAME | MODE_LOGGED_IN | 4,
            location: String::new(),
        })
        .collect::<Vec<_>>();
    encoded_roster_fits(&friends)
}

fn encoded_roster_fits(friends: &[Friend]) -> bool {
    // Six bytes for &quot; is the maximum XML expansion per byte. The
    // allowance per row covers the tag, attribute names and numeric mode.
    let upper_bound: usize = 128
        + friends
            .iter()
            .map(|f| 100 + 6 * (f.name.len() + f.location.len()))
            .sum::<usize>();
    if upper_bound <= ac2_gun_friends::MAX_WORLD_PAYLOAD {
        return true;
    }
    let event = Event::List {
        cx: u32::MAX,
        hr: S_OK,
        friends: friends.to_vec(),
    };
    event.encode().is_ok()
}

// Optional adapter-provided locations can push an otherwise valid roster over
// the transport limit. Preserve every identity and mode for both transports.
pub(super) fn fit_optional_locations(friends: &mut [Friend]) {
    if friends.iter().any(|f| !f.location.is_empty()) && !encoded_roster_fits(friends) {
        for friend in friends {
            friend.location.clear();
        }
    }
}
