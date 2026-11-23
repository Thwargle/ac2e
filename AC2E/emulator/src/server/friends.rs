use super::{invalid, other, script, Peer, Store};
use crate::gun::{Hub, S_OK, ZT_E_INVALID_ARG, ZT_E_INVALID_USER};
use ac2_gun_friends::{Event, Friend, Request, EVENT_ID};
use std::io;

const HRESULT_BUSY: u32 = 0x800700aa;

pub(super) fn depart(
    active: &mut std::collections::BTreeMap<u64, std::net::SocketAddr>,
    hub: &Hub,
    store: &Store,
    id: u64,
) {
    active.remove(&id);
    if let Some(c) = store.characters.get(&id) {
        hub.leave_world(&c.name);
    }
}

#[derive(Default)]
pub(super) struct Subscription {
    delivered: Option<Vec<Friend>>,
    revision: u64,
    window_ms: u64,
    requests: u8,
}

fn packet(event: &Event) -> io::Result<Vec<u8>> {
    let bytes = event.encode().map_err(invalid)?;
    script::private_call(EVENT_ID, &bytes).map_err(other)
}

fn rejected(request: &Request, hr: u32) -> Event {
    match *request {
        Request::List { cx } => Event::List {
            cx,
            hr,
            friends: Vec::new(),
        },
        Request::Add { cx, .. } => Event::Added { cx, hr },
        Request::Delete { cx, .. } => Event::Deleted { cx, hr },
        Request::Flags { cx, word, .. } => Event::Flags {
            cx,
            hr,
            word,
            flags: 0,
        },
    }
}

impl Peer {
    pub(super) fn friend_request(
        &mut self,
        hub: &Hub,
        character: u64,
        store: &Store,
        bytes: &[u8],
        now_ms: u64,
    ) -> io::Result<()> {
        // Even the largest escaped native character name fits in this envelope.
        if bytes.len() > 1024 {
            return Err(invalid("oversized GUN friend request"));
        }
        let mut request = Request::decode(bytes).map_err(invalid)?;
        if now_ms.saturating_sub(self.friends.window_ms) >= 1000 {
            self.friends.window_ms = now_ms;
            self.friends.requests = 0;
        }
        if self.friends.requests >= 16 {
            return self.queue_ordered_batch(vec![packet(&rejected(&request, HRESULT_BUSY))?]);
        }
        self.friends.requests += 1;
        if let Request::Add { name, .. } = &mut request {
            let base = crate::gun::base(name);
            let found = store
                .characters
                .values()
                .find(|c| !c.deleted && c.name.eq_ignore_ascii_case(&base));
            match found {
                Some(c) if c.id != character => *name = c.name.clone(),
                other => {
                    let hr = if other.is_some() {
                        ZT_E_INVALID_ARG
                    } else {
                        ZT_E_INVALID_USER
                    };
                    return self.queue_ordered_batch(vec![packet(&rejected(&request, hr))?]);
                }
            }
        }
        // Reserve space for the acknowledgement and any resulting roster before
        // changing persisted friends or publishing a new availability to observers.
        let snapshot_bytes = match request {
            Request::Add { .. } | Request::Delete { .. } => Some(ac2_gun_friends::MAX_MESSAGE),
            Request::Flags { op: 1, .. } => Some(0),
            _ => None,
        };
        if let Some(bytes) = snapshot_bytes {
            let busy = packet(&rejected(&request, HRESULT_BUSY))?;
            if self
                .validate_scene_space(1 + usize::from(bytes != 0), busy.len() + bytes)
                .is_err()
            {
                return self.queue_ordered_batch(vec![busy]);
            }
        }
        let account = &self.session.account;
        // Read the revision first so a concurrent adapter edit always causes a recheck.
        let revision = hub.world_revision();
        let character = store
            .characters
            .get(&character)
            .ok_or_else(|| invalid("unknown friend session character"))?;
        let result = hub.world_request_for_character(account, &character.name, &request);
        let mut packets = vec![packet(&result)?];
        let snapshot = match result {
            Event::List {
                hr: S_OK, friends, ..
            } => Some(friends),
            Event::Added { hr: S_OK, .. } | Event::Deleted { hr: S_OK, .. } => {
                // A legacy adapter roster may need several removals before
                // it fits. Acknowledge each durable removal in either case.
                let friends = hub.try_world_friends(account).ok();
                if let Some(rows) = &friends {
                    packets.push(packet(&Event::Start {
                        friends: rows.clone(),
                    })?);
                }
                friends
            }
            _ => None,
        };
        self.queue_ordered_batch(packets)?;
        if let Some(friends) = snapshot {
            self.friends.delivered = Some(friends);
            self.friends.revision = revision;
        }
        Ok(())
    }

    pub(super) fn refresh_friends(&mut self, hub: &Hub, revision: u64) -> io::Result<()> {
        let Some(before) = &self.friends.delivered else {
            return Ok(());
        };
        if self.friends.revision == revision {
            return Ok(());
        }
        let after = hub.world_friends(&self.session.account);
        let mut packets = Vec::new();
        if before
            .iter()
            .map(|f| &f.name)
            .eq(after.iter().map(|f| &f.name))
        {
            for (old, new) in before.iter().zip(&after) {
                if old != new {
                    packets.push(packet(&Event::Status(new.clone()))?);
                }
            }
        } else {
            packets.push(packet(&Event::Start {
                friends: after.clone(),
            })?);
        }
        self.queue_ordered_batch(packets)?;
        self.friends.delivered = Some(after);
        self.friends.revision = revision;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn busy_outbox_rejects_an_edit_before_saving_and_allows_retry() {
        let dir =
            std::env::temp_dir().join(format!("ac2-friends-backpressure-{}", std::process::id()));
        let mut store = Store::open(&dir).unwrap();
        let owner = store.create("local", "Test Owner").unwrap().id;
        store.create("other", "Test Friend").unwrap();
        let hub = Hub::open(&dir, crate::gun::GunAuth::Developer).unwrap();
        let mut peer = super::super::tests::peer();
        peer.queue_ordered_batch(vec![vec![0; 4]; 1023]).unwrap();
        let request = Request::Add {
            cx: 42,
            name: "Test Friend".into(),
        }
        .encode()
        .unwrap();
        peer.friend_request(&hub, owner, &store, &request, 1000)
            .unwrap();
        assert_eq!(
            &*peer.scene_outbox.back().unwrap().0,
            packet(&Event::Added {
                cx: 42,
                hr: HRESULT_BUSY,
            })
            .unwrap()
        );
        assert!(hub.world_friends("local").is_empty());
        peer.scene_outbox.clear();
        peer.scene_bytes = 0;
        peer.friend_request(&hub, owner, &store, &request, 2000)
            .unwrap();
        assert_eq!(hub.world_friends("local")[0].name, "Test Friend");
        drop(store);
        std::fs::remove_dir_all(dir).unwrap();
    }
}
