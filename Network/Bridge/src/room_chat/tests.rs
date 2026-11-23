use super::*;

#[test]
fn social_rooms_are_learned_from_native_updates_and_cleared_on_leave() {
    let mut state = RoomChatState { turbine: true, ..Default::default() };
    assert!(state.request_channel("fellowship", "Hi").is_err());
    let mut graph = PackedReference::native(NativeObject::WString("unused".into()));
    graph.entries[0] = PackedEntry::Heap { flags: 0x44000100, object: HeapObject {
        pkg_id: 0x1f0, pkg_state_id: 0, native: None,
        slots: vec![0, 0, 4, 713, 0, 0, 0, 0], tags: vec![2;8],
    }};
    let update = ServerEvent::Interp { function: 0x820c002b, object: None,
        arguments: pack_values(&[PackValue::Reference(graph)]) };
    assert!(state.receive(&update).is_none()); // Other decoders must still see it.
    let wire = state.request_channel("fellowship", "Hi fellows").unwrap();
    let ChatRequest::SendToRoomById {room, extra, ..} = ChatRequest::from_packet(&ChatPacket::decode(&wire).unwrap()).unwrap() else {panic!()};
    assert_eq!(room,713);assert_eq!(GunChatExtraInfo::decode(&extra).unwrap().weenie_flags,1);
    let incoming = ServerEvent::Chat(ChatMessage::RoomChat(RoomChat {room:713,speaker:"Fellow".into(),text:"Hi".into(),extra:vec![]}).to_packet().encode());
    assert_eq!(state.receive(&incoming).unwrap()[0]["kind"],0x40);
    state.receive(&ServerEvent::Interp {function:0x820c0028,object:None,arguments:vec![]});
    assert!(state.request_channel("fellowship","Hi").is_err());
    state.receive(&ServerEvent::Interp {function:0x82030030,object:None,arguments:pack_values(&[PackValue::Int(991)])});
    assert!(state.request_channel("allegiance","Hi").is_ok());
    state.reset();assert!(state.request_channel("allegiance","Hi").is_err());
}

fn assignment(function: u32, package: u32, pairs: &[(u32, u32)]) -> ServerEvent {
    let mut graph = PackedReference::native(NativeObject::Hash(ScriptHash {
        native_type: ntype::AR_HASH, bucket_index: 0,
        keys: pairs.iter().map(|(channel, _)| HashKey::Value(u64::from(*channel))).collect(),
        values: (0..pairs.len()).map(|i| (i + 20) as u64).collect(),
    }));
    for (i, (_, room)) in pairs.iter().enumerate() {
        graph.ids.push((i + 20) as u32);
        graph.entries.push(PackedEntry::Heap { flags: 0x44000100, object: HeapObject {
            pkg_id: package, pkg_state_id: 0, native: None,
            slots: vec![0, 0, 0, *room, 0, 0, 0], tags: vec![2; 7],
        }});
    }
    ServerEvent::Interp { function, object: None, arguments: pack_values(&[PackValue::Reference(graph)]) }
}

#[test]
fn general_requires_service_and_uses_server_assigned_room_for_both_dat_families() {
    for (function, package) in [(0x8205001B, 0x2023), (0x8205001E, 0x1AF4)] {
        let mut state = RoomChatState::default();
        assert!(state.request("Hello").unwrap_err().contains("service"));
        state.turbine = true;
        assert!(state.request("Hello").unwrap_err().contains("assigned"));
        let assigned = state.receive(&assignment(function, package, &[(GENERAL, 913), (0x100000, 17)])).unwrap();
        assert_eq!(assigned[0]["general_room"], 913);
        let wire = state.request("Hello, Ω 世界!").unwrap();
        let request = ChatRequest::from_packet(&ChatPacket::decode(&wire).unwrap()).unwrap();
        assert_eq!(request, ChatRequest::SendToRoomById {
            context: 1, room: 913, text: "Hello, Ω 世界!".into(),
            extra: GunChatExtraInfo::new(0, String::new()).encode(),
        });
        // Switching instances replaces, rather than appends to, the channel table.
        state.receive(&assignment(function, package, &[(GENERAL, 2)]));
        assert_eq!(state.channels.len(), 1);
        assert_eq!(state.channels[&GENERAL], 2);
        state.receive(&assignment(function, package, &[(GENERAL, 0)]));
        assert!(state.request("Hello").is_err());
        state.receive(&assignment(function, package, &[]));
        assert!(state.channels.is_empty());
    }
}

#[test]
fn malformed_table_preserves_valid_mapping_and_logout_clears_it() {
    let mut state = RoomChatState { turbine: true, ..Default::default() };
    state.receive(&assignment(0x8205001E, 0x1AF4, &[(GENERAL, 1)]));
    for event in [assignment(0x8205001E, 0xFFFF, &[(GENERAL, 2)]),
        assignment(0x8205001E, 0x1AF4, &[(GENERAL, 2), (GENERAL, 3)]),
        ServerEvent::Interp { function: 0x8205001E, object: None, arguments: vec![5] }] {
        assert_eq!(state.receive(&event).unwrap()[0]["event"], "error");
        assert_eq!(state.channels[&GENERAL], 1);
    }
    for invalid in ["", " ", "Hello\nWorld", "Hi\0", &"x".repeat(2049)] {
        assert!(state.request(invalid).is_err());
    }
    state.request("Hi").unwrap();
    state.reset();
    assert!(state.pending.is_empty());
    assert!(state.request("Hi").is_err());
}

#[test]
fn incoming_messages_preserve_unicode_speaker_channel_and_report_rejections() {
    let mut state = RoomChatState { turbine: true, ..Default::default() };
    state.receive(&assignment(0x8205001E, 0x1AF4, &[(GENERAL, 1)]));
    state.request("Hi").unwrap();
    let response = |context, result| ServerEvent::Chat(ChatMessage::Response(ChatResponse {
        context, response_id: 1, method: 2, result,
    }).to_packet().encode());
    assert!(state.receive(&response(99, 10)).unwrap().is_empty());
    let error = state.receive(&response(1, 10)).unwrap();
    assert!(error[0]["message"].as_str().unwrap().contains("rejected"));
    assert!(state.receive(&response(1, 10)).unwrap().is_empty());
    state.request("Hi again").unwrap();
    assert!(state.receive(&response(2, 0)).unwrap().is_empty());
    let incoming = ServerEvent::Chat(ChatMessage::RoomChat(RoomChat {
        room: 1, speaker: "Other player".into(), text: "Hi, Ω 世界!".into(), extra: vec![],
    }).to_packet().encode());
    let result = state.receive(&incoming).unwrap();
    assert_eq!(result.len(), 1);
    assert_eq!(result[0]["channel"], "General");
    assert_eq!(result[0]["speaker"]["literal"], "Other player");
    assert_eq!(result[0]["text"]["literal"], "Hi, Ω 世界!");
    assert_eq!(state.receive(&ServerEvent::Chat(vec![1])).unwrap()[0]["event"], "error");
}

#[test]
fn real_bridge_command_round_trips_over_native_queue_four() {
    use ac2_net::{NetClient, NetClientConfig, Logon, Credential, NetEvent};
    use legions_server_double::{double, addr, Spawn, SERVER};
    let (transport, server) = double(Spawn::default());
    let mut session = crate::Session::connect(&json!({"host":"127.0.0.1","port":9000,"account":"local","version":"7160"})).unwrap();
    session.client = NetClient::with_transport(NetClientConfig {
        server: addr(SERVER), logon: Logon { account: "local".into(), version: "7160".into(),
            credential: Credential::Account, connection_sequence: 1, crypto: None },
    }, Box::new(transport));
    for _ in 0..20 { session.client.poll(); server.borrow_mut().elapsed += 0.05; }
    session.client.net_mut().enter_world();
    session.ready = true;
    session.room_chat.turbine = true;
    session.room_chat.receive(&assignment(0x8205001E, 0x1AF4, &[(GENERAL, 731)]));
    session.command(&json!({"command":"chat","channel":"general","text":"General round trip Ω"})).unwrap();
    let mut received = Vec::new();
    for _ in 0..20 {
        for event in session.client.poll() {
            if let NetEvent::Blob(blob) = event {
                if let Ok(event) = crate::login::decode(&blob.data, Build::Legions7160) {
                    if let Some(values) = session.room_chat.receive(&event) { received.extend(values); }
                }
            }
        }
        server.borrow_mut().elapsed += 0.05;
    }
    let server = server.borrow();
    assert!(server.script_errors.is_empty());
    let (id, queue, data) = server.blobs.iter().find(|(_, _, data)| data.starts_with(&0x100D3u32.to_le_bytes())).expect("native room event");
    assert_eq!(*queue, 4);
    assert_eq!(*id >> 48, 0x2300); // CCliDatEvent, not a game-script event.
    assert_eq!(u32::from_le_bytes(data[4..8].try_into().unwrap()) as usize, data.len() - 8);
    let request = ChatRequest::from_packet(&ChatPacket::decode(&data[8..]).unwrap()).unwrap();
    assert!(matches!(request, ChatRequest::SendToRoomById {room:731, ..}));
    assert!(received.iter().any(|e| e["event"] == "chat" && e["channel"] == "General" && e["text"]["literal"] == "General round trip Ω"), "{received:?}");
    assert!(!received.iter().any(|e| e["event"] == "error"), "{received:?}");
}
