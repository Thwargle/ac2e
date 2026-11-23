use super::*;

fn session(account: &str) -> (Session, mpsc::Receiver<Vec<u8>>, TcpStream) {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let peer = TcpStream::connect(listener.local_addr().unwrap()).unwrap();
    let (socket, _) = listener.accept().unwrap();
    let (out, receiver) = mpsc::sync_channel(1);
    (Session { account: Some(account.into()), out, socket, flags: 0,
        location: String::new(), words: [0; 2] }, receiver, peer)
}
fn response(receiver: &mpsc::Receiver<Vec<u8>>) -> Elem {
    let bytes = receiver.recv_timeout(Duration::from_secs(1)).unwrap();
    Elem::parse(std::str::from_utf8(&bytes[4..]).unwrap()).unwrap()
}
fn hub(state: State, file: PathBuf) -> Hub {
    Hub { state: Mutex::new(state), pending_online: Mutex::new(None), file, auth: GunAuth::Developer }
}

#[test]
fn full_output_queue_disconnects_instead_of_blocking_shared_state() {
    let (client, _receiver, _peer) = session("alice");
    let mut state = State::default();
    state.sessions.insert(1, client);
    state.deliver(1, &Elem::new("First"));
    let start = Instant::now();
    state.deliver(1, &Elem::new("Overflow"));
    assert!(start.elapsed() < Duration::from_millis(250));
}

#[test]
fn ignoring_a_character_blocks_its_account_tells() {
    let (alice, alice_out, _alice_peer) = session("alice");
    let (bob, bob_out, _bob_peer) = session("bob");
    let mut state = State::default();
    state.sessions.extend([(1, alice), (2, bob)]);
    state.characters.insert("alice hero".into(), "alice".into());
    state.users.insert("bob".into(), User { friends: vec![], ignored: vec!["Alice Hero@AC2e".into()] });
    let hub = hub(State::default(), PathBuf::new());
    request(&hub, &mut state, 1, "alice", "1", &Elem::new("SendToUserEx").attr("Name", "bob").attr("Text", "hidden"));
    assert_eq!(response(&alice_out).get("hr"), Some("0x00000000"));
    assert!(bob_out.try_recv().is_err());
}

#[test]
fn release_revokes_identity_and_location_only_updates_are_announced() {
    let (alice, alice_out, _alice_peer) = session("alice");
    let (bob, bob_out, _bob_peer) = session("bob");
    let mut state = State::default();
    state.sessions.extend([(1, alice), (2, bob)]);
    state.users.insert("alice".into(), User { friends: vec!["bob".into()], ignored: vec![] });
    state.announce(&BTreeSet::from(["bob".into()]));
    assert_eq!(response(&alice_out).get("Mode"), Some("1"));
    state.sessions.get_mut(&2).unwrap().location = "Cragstone".into();
    state.announce(&BTreeSet::from(["bob".into()]));
    assert_eq!(response(&alice_out).get("Location"), Some("Cragstone"));
    let hub = hub(state, PathBuf::new());
    handle(&hub, 2, "127.0.0.1".parse().unwrap(), &Elem::new("ReleaseTicketCmd"));
    assert_eq!(response(&bob_out).name, "ReleaseTicketRes");
    assert_eq!(response(&alice_out).get("Mode"), Some("0"));
    handle(&hub, 2, "127.0.0.1".parse().unwrap(), &Elem::new("GetFriendsList"));
    assert_eq!(response(&bob_out).get("hr"), Some("0xC112080B"));
}

#[test]
fn failed_list_save_rolls_back_and_reports_failure() {
    let (alice, outgoing, _peer) = session("alice");
    let mut state = State::default();
    state.sessions.insert(1, alice);
    let file = std::env::temp_dir().join(format!("gun-absent-{}", std::process::id())).join("no-parent/users.json");
    let hub = hub(State::default(), file);
    request(&hub, &mut state, 1, "alice", "1", &Elem::new("FriendAdd").attr("Name", "bob"));
    assert_eq!(response(&outgoing).get("hr"), Some("0x80004005"));
    assert!(state.users["alice"].friends.is_empty());
}

#[test]
fn partial_frame_timeout_is_fatal_not_an_idle_timeout() {
    let (client, _outgoing, mut peer) = session("alice");
    let mut socket = client.socket;
    socket.set_read_timeout(Some(Duration::from_millis(5))).unwrap();
    peer.write_all(&[0, 0, 0, 10, b'<']).unwrap();
    let error = read_frame_with_deadline(&mut socket, &mut Vec::new(), &AtomicBool::new(false),
        Duration::from_millis(10)).unwrap_err();
    assert_eq!(error.kind(), io::ErrorKind::InvalidData);
}

#[test]
fn authenticated_ticket_is_ip_bound_and_single_use() {
    let directory = std::env::temp_dir().join(format!("gun-auth-{}-{}", std::process::id(), crate::auth::hex(&crate::auth::random::<8>().unwrap())));
    std::fs::create_dir_all(&directory).unwrap();
    let mut accounts = crate::auth::Accounts::open(&directory).unwrap();
    accounts.create("alice", "long-test-password").unwrap();
    let ip = "127.0.0.1".parse().unwrap();
    let token = accounts.issue("alice", ip).unwrap();
    let hub = Hub { state: Mutex::new(State::default()), pending_online: Mutex::new(None), file: directory.join("gun-users.json"), auth: GunAuth::Tickets(directory.clone()) };
    assert_eq!(account_for_ticket(&hub, &format!("alice:{token}"), ip), Ok("alice".into()));
    assert_eq!(account_for_ticket(&hub, &format!("alice:{token}"), ip), Err(ZT_E_AUTH_INVALID_TICKET));
    let token = accounts.issue("alice", ip).unwrap();
    assert_eq!(account_for_ticket(&hub, &format!("alice:{token}"), "127.0.0.2".parse().unwrap()), Err(ZT_E_AUTH_INVALID_TICKET));
    drop(accounts);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
fn world_rosters_preserve_legacy_lists_and_track_presence_without_an_adapter() {
    use ac2_gun_friends::{Event, Request};

    let dir = std::env::temp_dir().join(format!("gun-rosters-{}", crate::auth::hex(&crate::auth::random::<8>().unwrap())));
    std::fs::create_dir(&dir).unwrap();
    let path = dir.join("gun-users.json");
    std::fs::write(&path, r#"{"alice":{"friends":["Bee@AC2e","bee","Bee Keeper"],"ignored":["Muted"]}}"#).unwrap();
    let hub = Hub::open(&dir, GunAuth::Developer).unwrap();
    let roster = hub.world_friends("ALICE");
    assert_eq!(roster.iter().map(|f| (&*f.name, f.mode)).collect::<Vec<_>>(),
        [("Bee@AC2e", 0), ("Bee Keeper", 0)]);
    assert!(hub.world_friends("bee").is_empty());

    let original_revision = hub.world_revision();
    let duplicate = Request::Add { cx: 1, name: "bEe".into() };
    assert_eq!(hub.world_request("Alice", &duplicate), Event::Added { cx: 1, hr: ZT_E_ALREADY_IN_LIST });
    assert_eq!(hub.world_revision(), original_revision);
    let online = [("Bee Keeper".to_owned(), "BEE".to_owned())];
    hub.update_online(online.clone());
    let roster = hub.world_friends("Alice");
    assert_eq!((roster[0].mode, roster[1].mode), (0, MODE_IN_GAME | MODE_LOGGED_IN));
    let joined = hub.world_revision();
    assert_ne!(joined, original_revision);
    hub.update_online(online);
    assert_eq!(hub.world_revision(), joined);
    hub.update_online([]);
    assert!(hub.world_friends("Alice").iter().all(|f| f.mode == 0));
    let remove = Request::Delete { cx: 2, name: "Bee".into() };
    assert_eq!(hub.world_request("Alice", &remove), Event::Deleted { cx: 2, hr: S_OK });
    assert_eq!(hub.world_request("Alice", &remove), Event::Deleted { cx: 2, hr: ZT_E_NOT_IN_LIST });
    drop(hub);
    let hub = Hub::open(&dir, GunAuth::Developer).unwrap();
    assert_eq!(hub.world_friends("alice").iter().map(|f| f.name.as_str()).collect::<Vec<_>>(), ["Bee Keeper"]);
    assert_eq!(lock(&hub).users["alice"].ignored, ["Muted"]);
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn game_and_adapter_edits_refresh_the_other_sessions_and_share_one_roster() {
    use ac2_gun_friends::{Event, Request};

    let dir = std::env::temp_dir().join(format!("gun-shared-{}", crate::auth::hex(&crate::auth::random::<8>().unwrap())));
    std::fs::create_dir(&dir).unwrap();
    let hub = Hub::open(&dir, GunAuth::Developer).unwrap();
    let (adapter, outgoing, _peer) = session("alice");
    lock(&hub).sessions.insert(1, adapter);
    let add = Request::Add { cx: 3, name: "Bee".into() };
    assert_eq!(hub.world_request("alice", &add), Event::Added { cx: 3, hr: S_OK });
    let first = response(&outgoing);
    assert_eq!(first.name, "FStart");
    assert_eq!(first.children[0].get("Name"), Some("Bee"));

    let (second, other, _peer) = session("alice");
    lock(&hub).sessions.insert(2, second);
    let revision = hub.world_revision();
    handle(&hub, 1, "127.0.0.1".parse().unwrap(), &Elem::new("FriendDelete").attr("Name", "BEE@ac2e"));
    assert_eq!(response(&outgoing).get("hr"), Some("0x00000000"));
    let cleared = response(&other);
    assert_eq!((cleared.name.as_str(), cleared.children.len()), ("FStart", 0));
    assert!(hub.world_friends("alice").is_empty());
    assert_ne!(hub.world_revision(), revision);
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn availability_follows_character_session_and_not_the_account_of_the_same_name() {
    use ac2_gun_friends::{Event, Request};

    let mut state = State::default();
    state.users.entry("observer".into()).or_default().friends = vec!["Alice".into(), "Alt".into()];
    let hub = hub(state, PathBuf::new());
    // "Alice" is a character on the owner account; the account "alice" plays Alt.
    let online = [("Alice".to_owned(), "owner".to_owned()), ("Alt".to_owned(), "alice".to_owned())];
    let flags = |op, word, flags| hub.world_request_for_character("owner", "Alice", &Request::Flags {
        cx: 17, op, word, flags,
    });
    let result = |hr, word, flags| Event::Flags { cx: 17, hr, word, flags };
    assert_eq!(flags(1, 1, 2), result(S_OK, 1, 2)); // Before the first presence pulse.
    hub.update_online(online.clone());
    let modes = || hub.world_friends("observer").into_iter().map(|f| f.mode).collect::<Vec<_>>();
    assert_eq!(modes(), [11, 9]);
    assert_eq!(flags(0, 1, 0), result(S_OK, 1, 2));
    let before = hub.world_revision();
    assert_eq!(flags(1, 1, 4), result(S_OK, 1, 4)); // DND replaces AFK.
    assert_ne!(before, hub.world_revision());
    assert_eq!(modes(), [13, 9]);
    let before = hub.world_revision();
    assert_eq!(flags(1, 1, 4), result(S_OK, 1, 4));
    assert_eq!(flags(1, 3, 1), result(ZT_E_INVALID_ARG, 3, 0));
    assert_eq!(flags(1, 1, 6), result(ZT_E_INVALID_ARG, 1, 0));
    assert_eq!(flags(7, 1, 4), result(ZT_E_INVALID_ARG, 1, 0));
    assert_eq!(before, hub.world_revision());
    hub.leave_world("Alice");
    assert_eq!(modes(), [0, 9]);
    hub.update_online([online[1].clone()]);
    assert_eq!(modes(), [0, 9]);
    hub.update_online(online);
    assert_eq!(flags(0, 1, 0), result(S_OK, 1, 1));
    assert_eq!(modes(), [9, 9]);
}

#[test]
fn departure_publishes_the_final_presence_without_an_available_transition() {
    use ac2_gun_friends::Request;

    for (adapter_connected, expected_after) in [(false, "0"), (true, "1")] {
        let mut state = State::default();
        let (observer, outgoing, _peer) = session("observer");
        state.sessions.insert(1, observer);
        state.users.entry("observer".into()).or_default().friends = vec!["Alice".into()];
        let (adapter, _out, _other_peer) = session("alice");
        if adapter_connected {
            state.sessions.insert(2, adapter);
        }
        let hub = hub(state, PathBuf::new());
        let online = [("Alice".to_owned(), "alice".to_owned())];
        hub.update_online(online.clone());
        assert_eq!(response(&outgoing).get("Mode"), Some("9"));
        hub.world_request_for_character("alice", "Alice", &Request::Flags { cx: 1, op: 1, word: 1, flags: 2 });
        assert_eq!(response(&outgoing).get("Mode"), Some("11"));
        // A snapshot queued before the logout must not put the character
        // back in the world when the adapter next processes its pending work.
        let busy = lock(&hub);
        hub.update_online(online);
        drop(busy);
        hub.leave_world("ALICE");
        assert_eq!(response(&outgoing).get("Mode"), Some(expected_after));
        assert_eq!(hub.world_friends("observer")[0].mode, 0);
        hub.update_online([]);
        assert!(outgoing.try_recv().is_err());
    }
}

#[test]
fn oversized_legacy_rosters_and_escaped_adapter_locations_respect_world_limits() {
    use ac2_gun_friends::{Event, Request};

    let names: Vec<_> = (0..256).map(|i| format!("{i:03}{}", "\"".repeat(60))).collect();
    let mut state = State::default();
    state.users.entry("observer".into()).or_default().friends = names.clone();
    let hub = hub(state, PathBuf::new());
    assert_eq!(hub.try_world_friends("observer"), Err(ZT_E_TOO_MANY));
    assert_eq!(hub.world_request("observer", &Request::List { cx: 1 }),
        Event::List { cx: 1, hr: ZT_E_TOO_MANY, friends: vec![] });

    let mut state = lock(&hub);
    let normal: Vec<_> = (0..256).map(|i| format!("Character {i}")).collect();
    state.users.get_mut("observer").unwrap().friends = normal.clone();
    let (mut adapter, _out, _peer) = session("owner");
    adapter.location = "\"".repeat(63);
    state.sessions.insert(1, adapter);
    drop(state);
    hub.update_online(normal.into_iter().map(|name| (name, "owner".to_owned())));
    let friends = hub.try_world_friends("observer").unwrap();
    assert_eq!(friends.len(), 256);
    assert!(friends.iter().all(|f| f.mode == 9 && f.location.is_empty()));
    assert!(Event::List { cx: u32::MAX, hr: S_OK, friends }.encode().is_ok());
    let mut adapter_roster = Elem::new("FStart");
    adapter_roster.children = lock(&hub).friends_elems("observer");
    assert_eq!(adapter_roster.children.len(), 256);
    assert!(frame(&adapter_roster).len() <= MAX_FRAME + 4);
}
