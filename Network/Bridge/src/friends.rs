// SPDX-License-Identifier: AGPL-3.0-only
use ac2_gun_friends::{Event, Friend, Request, EVENT_ID, REQUEST_ID};
use ac2_net::protocol::{ClientEvent, ServerEvent};
use serde_json::{json, Value};
use std::{collections::HashMap, time::Instant};

#[derive(Default)]
pub struct Friends {
    next: u32,
    pending: HashMap<u32, (Request, Instant)>,
    pub subscribed: bool,
}

fn row(friend: Friend) -> Value {
    json!({"name":friend.name,"mode":friend.mode,"location":friend.location})
}

fn roster(friends: Vec<Friend>) -> Value {
    json!({"event":"friends","members":friends.into_iter().map(row).collect::<Vec<_>>()})
}

fn rejected(hr: u32) -> Value {
    let message = match hr {
        0xc1120012 => "The server does not recognize that player.",
        0xc1121009 => "That player is already on your friends list.",
        0xc1121401 => "That player is no longer on your friends list.",
        0xc112001a => "Your friends list is full.",
        0x800700aa => "The friends service is busy. Try again in a moment.",
        0xc1120013 | 0xc1120019 | 0xc1120005 => "Choose a valid player name for your friends list.",
        _ => "The server could not complete the friends request.",
    };
    json!({"event":"friend_error","message":message,"result":hr})
}

impl Friends {
    pub fn reset(&mut self) {
        self.subscribed = false;
        self.pending.clear();
    }

    pub fn request(
        &mut self,
        action: &str,
        name: &str,
        mode: Option<u32>,
        send: impl FnOnce(ClientEvent) -> Result<(), String>,
    ) -> Result<(), String> {
        self.pending
            .retain(|_, (_, at)| at.elapsed().as_secs() < 15);
        if self.pending.len() >= 32 {
            return Err("Too many pending friend requests.".into());
        }
        self.next = self.next.wrapping_add(1).max(1);
        let cx = self.next;
        let req = match action {
            "list" => Request::List { cx },
            "add" => Request::Add {
                cx,
                name: name.trim().into(),
            },
            "remove" => Request::Delete {
                cx,
                name: name.trim().into(),
            },
            "mode" => match mode {
                None => Request::Flags {
                    cx,
                    op: 0,
                    word: 1,
                    flags: 0,
                },
                Some(flags @ (1 | 2 | 4)) => Request::Flags {
                    cx,
                    op: 1,
                    word: 1,
                    flags,
                },
                _ => return Err("Unknown friend availability.".into()),
            },
            _ => return Err("Unknown friends action.".into()),
        };
        let arguments = req.encode()?;
        send(ClientEvent::Interp {
            function: REQUEST_ID,
            arguments,
            encrypted: false,
        })?;
        self.pending.insert(cx, (req, Instant::now()));
        self.subscribed = true;
        Ok(())
    }

    pub fn receive(&mut self, event: &ServerEvent, player: u64) -> Option<Value> {
        let ServerEvent::Interp {
            function: EVENT_ID,
            object,
            arguments,
        } = event
        else {
            return None;
        };
        if object.as_ref().is_some_and(|o| o.id != player) {
            return None;
        }
        self.receive_xml(arguments)
    }

    fn receive_xml(&mut self, data: &[u8]) -> Option<Value> {
        if !self.subscribed {
            return None;
        }
        let event = Event::decode(data).ok()?;
        let (cx, hr) = match &event {
            Event::List { cx, hr, .. }
            | Event::Added { cx, hr }
            | Event::Deleted { cx, hr }
            | Event::Flags { cx, hr, .. } => (*cx, *hr),
            _ => (0, 0),
        };
        match event {
            Event::Start { friends } => Some(roster(friends)),
            Event::Status(friend) => {
                let mut update = row(friend);
                update["event"] = json!("friend_status");
                Some(update)
            }
            reply => {
                let (request, _) = self.pending.get(&cx)?;
                let value = match (reply, request) {
                    (Event::List { friends, .. }, Request::List { .. }) => roster(friends),
                    (Event::Added { .. }, Request::Add { name, .. }) => {
                        json!({"event":"friend_result","action":"add","name":name})
                    }
                    (Event::Deleted { .. }, Request::Delete { name, .. }) => {
                        json!({"event":"friend_result","action":"remove","name":name})
                    }
                    (Event::Flags { word: 1, flags, .. }, Request::Flags { .. }) => {
                        json!({"event":"friend_mode","mode":flags})
                    }
                    _ => return None,
                };
                self.pending.remove(&cx);
                Some(if hr == 0 { value } else { rejected(hr) })
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn edits_wait_for_matching_replies_and_roster_is_authoritative() {
        let mut service = Friends::default();
        service
            .request("add", " Ada ", None, |packet| {
                let ClientEvent::Interp {
                    function: REQUEST_ID,
                    arguments,
                    encrypted: false,
                } = packet
                else {
                    panic!()
                };
                assert_eq!(
                    Request::decode(&arguments).unwrap(),
                    Request::Add {
                        cx: 1,
                        name: "Ada".into()
                    }
                );
                Ok(())
            })
            .unwrap();
        let ack = |event: Event| event.encode().unwrap();
        assert!(service
            .receive_xml(&ack(Event::Deleted { cx: 1, hr: 0 }))
            .is_none());
        let result = service
            .receive_xml(&ack(Event::Added {
                cx: 1,
                hr: 0xc1121009,
            }))
            .unwrap();
        assert_eq!(result["event"], "friend_error");
        assert!(service
            .receive_xml(&ack(Event::Added { cx: 1, hr: 0 }))
            .is_none());
        let start = ack(Event::Start {
            friends: vec![Friend {
                name: "Ada".into(),
                mode: 9,
                location: "Dereth".into(),
            }],
        });
        assert_eq!(
            service.receive_xml(&start).unwrap()["members"][0]["mode"],
            9
        );
        service.reset();
        assert!(service.receive_xml(&start).is_none());
    }

    #[test]
    fn private_player_interps_deliver_roster_and_availability() {
        use ac2_net::protocol::{Build, InstanceIdWithStamp};
        let mut service = Friends::default();
        service.request("list", "", None, |_| Ok(())).unwrap();
        service.request("mode", "", Some(2), |_| Ok(())).unwrap();
        for (event, expected) in [
            (
                Event::List {
                    cx: 1,
                    hr: 0,
                    friends: vec![],
                },
                json!({"event":"friends", "members":[]}),
            ),
            (
                Event::Flags {
                    cx: 2,
                    hr: 0,
                    word: 1,
                    flags: 2,
                },
                json!({"event":"friend_mode", "mode":2}),
            ),
        ] {
            let payload = event.encode().unwrap();
            let mut wire = 0x10066u32.to_le_bytes().to_vec();
            wire.extend(EVENT_ID.to_le_bytes());
            wire.extend((payload.len() as u32).to_le_bytes());
            wire.extend(&payload);
            let event = ServerEvent::decode(&wire, Build::Legions7160).unwrap();
            let other = ServerEvent::Interp {
                function: EVENT_ID,
                object: Some(InstanceIdWithStamp {
                    id: 17,
                    instance: 1,
                    other: 0,
                }),
                arguments: payload,
            };
            assert!(service.receive(&other, 12).is_none());
            assert_eq!(service.receive(&event, 12), Some(expected));
        }
    }
}
