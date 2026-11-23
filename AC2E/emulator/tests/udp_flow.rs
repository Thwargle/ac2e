//! Synthetic peer over real UDP. This does not substitute for running ac2client.exe.
#[path = "support/udp.rs"]
mod udp;
use ac2_net::protocol::{Build, ServerEvent};
use pyrealium::{
    server::{self, Config},
    store::Store,
    transport::Transport,
    wire::{self, Header},
    world,
};
use std::collections::BTreeMap;
use std::{
    net::UdpSocket,
    time::{Duration, Instant},
};

fn login(account: &str) -> Vec<u8> {
    let mut auth = Vec::new();
    for n in [1u32, 1, 1] {
        auth.extend(n.to_le_bytes());
    }
    auth.extend(wire::pack_string(account).unwrap());
    // Native Init supplies a 76-byte opaque CryptoAPI SIMPLEBLOB. This fixture
    // tests framing/policy, not decryption or Windows-generated key equivalence.
    auth.extend(76u32.to_le_bytes());
    auth.extend([0; 76]);
    auth.extend(0u32.to_le_bytes());
    let mut payload = wire::pack_string("7148").unwrap();
    payload.extend((auth.len() as u32).to_le_bytes());
    payload.extend(auth);
    wire::encode_handshake(
        Header {
            flags: wire::LOGON,
            ..Default::default()
        },
        &payload,
    )
    .unwrap()
}

#[test]
fn real_udp_login_ack_selection_enter_rejection_and_shutdown() {
    run_enter_rejection(None, false);
}

#[test]
#[ignore = "requires AC2_TEST_GAME_DATA pointing to the installed DAT directory"]
fn installed_dat_profile_rejects_7148_before_world_bootstrap() {
    run_enter_rejection(
        Some(std::env::var_os("AC2_TEST_GAME_DATA").unwrap().into()),
        false,
    );
}

#[test]
#[ignore = "requires AC2_TEST_COMPAT_DATA pointing to the converted test DAT directory"]
fn converted_dat_profile_sends_7148_world_bootstrap() {
    run_enter_rejection(
        Some(std::env::var_os("AC2_TEST_COMPAT_DATA").unwrap().into()),
        true,
    );
}

fn run_enter_rejection(game_data: Option<std::path::PathBuf>, expect_bootstrap: bool) {
    let profile_enabled = game_data.is_some();
    let (base, secondary) = (0..100)
        .find_map(|_| {
            let base = UdpSocket::bind("127.0.0.1:0").ok()?;
            let addr = base.local_addr().ok()?;
            let next = addr.port().checked_add(1)?;
            let secondary = UdpSocket::bind((addr.ip(), next)).ok()?;
            Some((base, secondary))
        })
        .unwrap();
    let addr = base.local_addr().unwrap();
    let ack_addr = secondary.local_addr().unwrap();
    let socket = UdpSocket::bind("127.0.0.1:0").unwrap();
    drop(base);
    drop(secondary);
    let dir = std::env::temp_dir().join(format!("ac2-udp-{}-{}", std::process::id(), addr.port()));
    std::fs::create_dir(&dir).unwrap();
    {
        let mut store = Store::open(&dir).unwrap();
        if profile_enabled {
            store
                .create_native(
                    "local",
                    &world::CharacterCreationRequest {
                        name: "Explorer".into(),
                        entity_did: 0x4700110f,
                        start_area: 0,
                        race: 1,
                        sex: 0x1000,
                        class: 0,
                        implement: 0,
                        physical_values: Vec::new(),
                    },
                )
                .unwrap();
        } else {
            store.create("local", "Explorer").unwrap();
        }
    }
    let server_dir = dir.clone();
    let worker = std::thread::spawn(move || {
        server::run(Config {
            content: None,
            encounters: None,
            content_overlay: None,
            world_rules: None,
            admin_bind: None,
            initialize_ui: expect_bootstrap,
            lifestones: false,
            portals: false,
            map_travel: false,
            bind: addr,
            account: "local".into(),
            data: server_dir,
            run_seconds: Some(45),
            visual_did: None,
            game_data,
        })
        .unwrap()
    });
    socket
        .set_read_timeout(Some(Duration::from_millis(100)))
        .unwrap();
    let mut buf = [0; 4096];
    let deadline = Instant::now() + Duration::from_secs(20);
    let (h, connect, connect_bytes) = loop {
        socket.send_to(&[0; 7], addr).unwrap(); // malformed input cannot kill the listener
        socket.send_to(&login("local"), addr).unwrap();
        match socket.recv_from(&mut buf) {
            Ok((len, source)) => {
                assert_eq!(source, addr);
                let (h, payload) = wire::decode_handshake(&buf[..len]).unwrap();
                break (
                    h,
                    wire::Connect::decode(payload).unwrap(),
                    buf[..len].to_vec(),
                );
            }
            Err(_) if Instant::now() < deadline => {
                // Windows may immediately report ICMP port-unreachable while
                // the server binds. Back off instead of flooding retry logons.
                std::thread::sleep(Duration::from_millis(25));
                continue;
            }
            Err(e) => panic!("server did not respond: {e}"),
        }
    };
    socket
        .set_read_timeout(Some(Duration::from_secs(5)))
        .unwrap();
    socket.send_to(&login("local"), addr).unwrap();
    let (len, _) = socket.recv_from(&mut buf).unwrap();
    assert_eq!(&buf[..len], connect_bytes); // repeated login preserves cookie/seeds
    let ack = wire::encode_handshake(
        Header {
            flags: wire::CONNECT_ACK,
            recipient: connect.net_id as u16,
            iteration: h.iteration,
            ..Default::default()
        },
        &connect.cookie.to_le_bytes(),
    )
    .unwrap();
    socket.send_to(&ack, ack_addr).unwrap();
    let mut peer = Transport::new(
        connect.outgoing_seed,
        connect.incoming_seed,
        h.recipient,
        h.iteration,
    );
    let ready = loop {
        let (len, source) = socket.recv_from(&mut buf).unwrap();
        assert_eq!(source, addr);
        if Header::decode(&buf[..len]).unwrap().flags == wire::CONNECT {
            continue;
        }
        // Native world-clock control packets can precede application events.
        let received = peer.receive(&buf[..len], Instant::now()).unwrap();
        if !received.messages.is_empty() {
            break received;
        }
    };
    assert_eq!(ready.messages.len(), 1);
    assert_eq!(ready.messages[0].queue, 5);
    assert_eq!(ready.messages[0].blob_id >> 56, 0x23);
    assert_eq!(ready.messages[0].data, 0x1003bu32.to_le_bytes());
    let ack_ready = wire::encode_control_packet(
        Header {
            flags: wire::ACK,
            recipient: h.recipient,
            iteration: h.iteration,
            ..Default::default()
        },
        &ready.ack.to_le_bytes(),
        None,
    )
    .unwrap();
    socket.send_to(&ack_ready, addr).unwrap();
    let list = loop {
        let (len, source) = socket.recv_from(&mut buf).unwrap();
        assert_eq!(source, addr);
        if Header::decode(&buf[..len]).unwrap().flags == wire::CONNECT {
            continue;
        }
        let received = peer.receive(&buf[..len], Instant::now()).unwrap();
        if !received.messages.is_empty() {
            break received;
        }
    };
    assert_eq!(list.messages.len(), 1);
    assert_eq!(list.messages[0].queue, 1);
    assert_eq!(list.messages[0].blob_id >> 56, 0x23);
    assert_eq!(
        list.messages[0].data,
        world::character_set_for(
            "7148",
            if profile_enabled {
                world::character_set(
                    b"local",
                    5,
                    &[world::SelectionCharacter {
                        id: 1,
                        name: "Explorer".into(),
                        seconds_greyed_out: 0,
                        parent_visual_did: 0x1f000023,
                        appearance: Some(
                            pyrealium::appearance::body(1, 0x1000)
                                .unwrap()
                                .visual_desc(&[])
                                .unwrap(),
                        ),
                    }],
                )
                .unwrap()
            } else {
                world::empty_character_set(b"local", 5).unwrap()
            }
        )
    );
    assert_eq!(
        &list.messages[0].data[list.messages[0].data.len() - 4..],
        &1u32.to_le_bytes(),
        "The native selection packet advertises the enabled General chat service"
    );
    let ack_list = wire::encode_control_packet(
        Header {
            flags: wire::ACK,
            recipient: h.recipient,
            iteration: h.iteration,
            ..Default::default()
        },
        &list.ack.to_le_bytes(),
        None,
    )
    .unwrap();
    socket.send_to(&ack_list, addr).unwrap();
    let event = world::encode_enter_world(1, b"local").unwrap();
    for packet in peer
        .send(0x0300000000000001, 4, &event, Instant::now())
        .unwrap()
    {
        socket.send_to(&packet, addr).unwrap();
    }
    let mut received_events = Vec::new();
    for _ in 0..32 {
        let (len, _) = socket.recv_from(&mut buf).unwrap();
        // A retried logon may already have queued an identical connection reply.
        if Header::decode(&buf[..len]).unwrap().flags == wire::CONNECT {
            continue;
        }
        let reply = peer.receive(&buf[..len], Instant::now()).unwrap();
        for message in reply.messages {
            received_events.push(message.data);
        }
        if received_events.len() == if expect_bootstrap { 3 } else { 1 } {
            break;
        }
    }
    let mut snapshot = None;
    if expect_bootstrap {
        assert_eq!(received_events.len(), 3);
        assert_eq!(received_events[0], world::create_player(1, 1));
        assert_eq!(
            &received_events[1][..4],
            &world::CREATE_OBJECT.to_le_bytes()
        );
        assert_eq!(&received_events[2][..4], &world::PLAYER_DESC.to_le_bytes());
        // Decode native variable-length descriptions and every populated qualities table.
        let ServerEvent::PlayerDescription(player) =
            ServerEvent::decode(&received_events[2], Build::Admin7148).unwrap()
        else {
            panic!("expected PlayerDescription");
        };
        assert_eq!(
            player.weenie.unwrap().name.unwrap().literal.as_deref(),
            Some("Explorer")
        );
        assert!(player.ints.windows(2).all(|p| p[0].0 % 11 <= p[1].0 % 11));
        assert_eq!(player.ints.len(), 17);
        let ints: BTreeMap<_, _> = player.ints.into_iter().collect();
        assert_eq!(ints.len(), 17);
        for stat in [0x100, 0x101, 0x102, 0x103, 0x107, 0x108] {
            assert_eq!(ints[&stat], 100);
        }
        for (stat, value) in [
            (0x12e, 1),
            (0x3b6, -1),
            (0x118, 78),
            (0x2d1, 4),
            (0x14, 31),
            (0xa, 1),
            (0xc1c, 0),
            (pyrealium::progression::QUEST_POINTS, 0),
            (0x122, 1),
            (0x123, 0x1000),
            (0x124, 0),
        ] {
            assert_eq!(ints[&stat], value, "quality {stat:x}");
        }
        let mut portal = pyrealium::dat::Dat::open(
            std::path::PathBuf::from(std::env::var_os("AC2_TEST_COMPAT_DATA").unwrap())
                .join("portal.dat"),
        )
        .unwrap();
        let authored = portal.read(0x8100110f).unwrap();
        let word = |offset| u32::from_le_bytes(authored[offset..offset + 4].try_into().unwrap());
        let mut bools: BTreeMap<_, _> = (100..180)
            .step_by(8)
            .map(|i| (word(i), word(i + 4) as i32))
            .collect();
        bools.insert(0x117, 0); // alive
        assert_eq!(player.bools.into_iter().collect::<BTreeMap<_, _>>(), bools);
        let floats = BTreeMap::from([(word(184), f32::from_bits(word(188))), (0x11e, 100.)]);
        assert_eq!(
            player.floats.into_iter().collect::<BTreeMap<_, _>>(),
            floats
        );
        assert_eq!(player.dids, [(word(196), 0x470000cd)]);
        assert_eq!(
            player.long_ints.into_iter().collect::<BTreeMap<_, _>>(),
            BTreeMap::from([
                (pyrealium::progression::LEVEL_XP_FLOOR, 0),
                (pyrealium::progression::LEVEL_XP_CEILING, 40),
                (0x12c, 0),
                (0x12d, 0),
                (0x13b, 0),
                (0x13c, 0),
            ])
        );
        let mut position = pyrealium::movement::Placement::HUMAN_START;
        udp::assert_spawn(&received_events[1], 1, 1, position);
        let mut receive_until = |peer: &mut Transport, predicate: &dyn Fn(&[u8]) -> bool| {
            let mut events = Vec::new();
            let deadline = Instant::now() + Duration::from_secs(8);
            while !events.iter().any(|p: &Vec<u8>| predicate(p)) {
                assert!(Instant::now() < deadline, "missing UDP event");
                let (len, source) = socket.recv_from(&mut buf).unwrap();
                assert_eq!(source, addr);
                let header = Header::decode(&buf[..len]).unwrap();
                if header.flags == wire::CONNECT {
                    continue;
                }
                let reply = peer.receive(&buf[..len], Instant::now()).unwrap();
                if header.sequence != 0 {
                    let ack = wire::encode_control_packet(
                        Header {
                            flags: wire::ACK,
                            recipient: h.recipient,
                            iteration: h.iteration,
                            ..Default::default()
                        },
                        &reply.ack.to_le_bytes(),
                        None,
                    )
                    .unwrap();
                    socket.send_to(&ack, addr).unwrap();
                }
                events.extend(reply.messages.into_iter().map(|m| m.data));
            }
            events
        };
        let is_ui = |p: &[u8]| p.get(4..8) == Some(&pyrealium::ui::LOAD.to_le_bytes());
        let initial = receive_until(&mut peer, &is_ui);
        let initial_ui = initial.iter().find(|e| is_ui(e)).unwrap();
        assert!(
            initial
                .iter()
                .any(|p| p.starts_with(&world::CREATE_OBJECT.to_le_bytes())),
            "missing starter gear"
        );
        position.coordinates[0] += 3.0;
        let update = pyrealium::movement::ClientPosition {
            time: 10.0,
            placement: position,
            motion: 0xbf7f7f7f,
            cancel_moveto: false,
            stamps: [1, 0, 0, 0],
            jump_velocity: None,
        };
        let mut events = vec![
            (
                0x0300000000000002,
                4,
                world::SCENE_RENDERING_COMPLETE.to_le_bytes().to_vec(),
            ),
            (
                0x0300000000000007,
                4,
                world::SCENE_RENDERING_COMPLETE.to_le_bytes().to_vec(),
            ),
            (0x0100000100000003, 3, update.encode().unwrap()),
        ];
        // Replayed time and another instance must not overwrite accepted placement.
        let invalid = pyrealium::movement::ClientPosition {
            placement: pyrealium::movement::Placement {
                coordinates: [9.0; 3],
                ..position
            },
            ..update
        };
        events.push((0x0100000200000004, 3, invalid.encode().unwrap()));
        let wrong_instance = pyrealium::movement::ClientPosition {
            time: 11.0,
            stamps: [2, 0, 0, 0],
            ..invalid
        };
        events.push((0x0100000300000008, 3, wrong_instance.encode().unwrap()));
        let mut logout = world::CHAR_EXIT_GAME.to_le_bytes().to_vec();
        logout.extend(1u64.to_le_bytes());
        events.push((0x0300000000000005, 4, logout));
        for (id, queue, event) in events {
            for packet in peer.send(id, queue, &event, Instant::now()).unwrap() {
                socket.send_to(&packet, addr).unwrap();
            }
        }
        let exit_events = receive_until(&mut peer, &|p| {
            p.starts_with(&world::CHARACTER_SET.to_le_bytes())
        });
        let exit_index = exit_events
            .iter()
            .position(|p| p == &world::char_exit_game())
            .unwrap();
        assert!(exit_events[exit_index + 1].starts_with(&world::CHARACTER_SET.to_le_bytes()));
        for packet in peer
            .send(
                0x0300000000000006,
                4,
                &world::encode_enter_world(1, b"local").unwrap(),
                Instant::now(),
            )
            .unwrap()
        {
            socket.send_to(&packet, addr).unwrap();
        }
        let reentry = receive_until(&mut peer, &is_ui);
        udp::assert_spawn(&reentry[1], 1, 2, position);
        assert_eq!(reentry.iter().find(|p| is_ui(p)).unwrap(), initial_ui);
        snapshot = Some(initial_ui.clone());
    } else {
        assert_eq!(
            received_events,
            vec![world::character_error(
                world::CharacterError::CouldNotPlaceCharacter
            )]
        );
    }
    std::fs::write(dir.join("stop.request"), b"stop").unwrap();
    worker.join().unwrap();
    assert!(!dir.join("server.lock").exists());
    {
        let store = Store::open(&dir).unwrap();
        assert_eq!(store.characters[&1].name, "Explorer");
        if expect_bootstrap {
            let mut position = pyrealium::movement::Placement::HUMAN_START;
            position.coordinates[0] += 3.0;
            assert_eq!(store.placement(1), Some(position));
            let saved = &store.characters[&1];
            assert!(saved.inventory.items().count() > 0);
            assert!(!saved.skills.is_empty());
            let starter_bar = pyrealium::preferences::Hotbar::starter(1, 0x1000);
            let restored = pyrealium::ui::player_all_quests_options(
                saved.ui.as_ref(),
                Some(&starter_bar),
                Some(&saved.inventory),
                Some(&saved.progression),
                Some(&saved.skills),
                Some(&saved.quests),
                saved.gameplay_options.as_ref(),
            )
            .unwrap();
            assert_eq!(snapshot.unwrap(), restored);
        }
    }
    udp::remove_test_store(&dir);
}
