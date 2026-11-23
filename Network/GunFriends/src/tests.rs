use super::*;

fn payload(xml: &str) -> Vec<u8> {
    let mut bytes = vec![0x50, 0xc3];
    bytes.extend_from_slice(xml.as_bytes());
    bytes.push(0);
    bytes
}

#[test]
fn retail_requests_and_correlated_results() {
    let add = Request::Add {
        cx: 0xe09a818,
        name: "Example@Dawn".into(),
    };
    assert_eq!(
        Request::decode(&payload(
            r#"<FriendAdd Cx="0xe09a818" User="Example@Dawn" />"#
        ))
        .unwrap(),
        add
    );
    let response = Event::Added {
        cx: 0xe09a818,
        hr: 0xc1120012,
    };
    assert_eq!(
        response.encode().unwrap(),
        payload(r#"<FriendAddRes Cx="0xe09a818" HR="0xc1120012"/>"#)
    );
    for request in [
        Request::List { cx: 0 },
        add,
        Request::Delete {
            cx: u32::MAX,
            name: "Player & Ally".into(),
        },
    ] {
        assert_eq!(
            Request::decode(&request.encode().unwrap()).unwrap(),
            request
        );
    }
    for event in [
        response,
        Event::Deleted {
            cx: u32::MAX,
            hr: 0,
        },
    ] {
        assert_eq!(Event::decode(&event.encode().unwrap()).unwrap(), event);
    }
}

#[test]
fn roster_and_presence_follow_native_schema() {
    let friend = Friend {
        name: "Example@Dawn".into(),
        mode: 2,
        location: String::new(),
    };
    let snapshot = Event::List {
        cx: 0x12,
        hr: 0,
        friends: vec![friend.clone()],
    };
    assert_eq!(Event::decode(&payload(r#"<GetFriendsListRes Cx="0x12" HR="0x0" Cnt="1"><FriendStatus User="Example@Dawn" Mode="2" Location="" /></GetFriendsListRes>"#)).unwrap(), snapshot);
    let mut presence = payload(r#"<FriendStat User="Example@Dawn" Mode="2"/>"#);
    assert_eq!(Event::Status(friend.clone()).encode().unwrap(), presence);
    presence.extend_from_slice(&[1; 8]);
    assert_eq!(
        Event::decode(&presence).unwrap(),
        Event::Status(friend.clone())
    );
    let mut original = friend;
    original.location = "Arwic & environs".into();
    for event in [
        snapshot,
        Event::Start { friends: vec![] },
        Event::Start {
            friends: vec![original.clone()],
        },
        Event::Status(original),
    ] {
        assert_eq!(Event::decode(&event.encode().unwrap()).unwrap(), event);
    }
}

#[test]
fn native_state_flags_use_distinct_word_names_on_requests_and_results() {
    let request = Request::Flags {
        cx: 0x23,
        op: 1,
        word: 1,
        flags: 1,
    };
    let result = Event::Flags {
        cx: 0x23,
        hr: 0,
        word: 1,
        flags: 1,
    };
    assert_eq!(
        Request::decode(&payload(r#"<Flags Cx="0x23" Op="1" Word="1" Flags="1" />"#)).unwrap(),
        request
    );
    assert_eq!(
        Event::decode(&payload(
            r#"<FlagsRes Cx="0x23" HR="0x0" WhichFlag="1" Flags="1" />"#
        ))
        .unwrap(),
        result
    );
    assert_eq!(
        Request::decode(&request.encode().unwrap()).unwrap(),
        request
    );
    assert_eq!(Event::decode(&result.encode().unwrap()).unwrap(), result);
    assert!(Event::decode(&payload(
        r#"<FlagsRes Cx="0x23" HR="0x0" Word="1" Flags="1"/>"#
    ))
    .is_err());
}

#[test]
fn malformed_messages_never_expose_partial_rosters() {
    for xml in [
        r#"<FStart Cnt="1"/>"#,
        r#"<FStart Cnt="1"><Friend Name="Example" Mode="0" Location=""/></FStart>"#,
        r#"<FStart Cnt="1"><FriendStatus User="Example" Mode="0"/></FStart>"#,
        r#"<FriendStat User="Example" Mode="-1"/>"#,
        r#"<FriendStat User="Example" Mode="2" Mode="4"/>"#,
    ] {
        assert!(Event::decode(&payload(xml)).is_err());
    }
    assert!(Request::decode(&payload(r#"<FriendDelete Cx="0x1" User="&#10;"/>"#)).is_err());
    assert!(Request::decode(&payload(
        r#"<FriendDelete Cx="0x100000000" User="Example"/>"#
    ))
    .is_err());
    assert!(Event::decode(&vec![0; MAX_MESSAGE + 1]).is_err());
    let entry = Friend {
        name: "Example".into(),
        mode: 0,
        location: String::new(),
    };
    assert!(Event::Start {
        friends: vec![entry; MAX_LIST + 1]
    }
    .encode()
    .is_err());
}

#[test]
fn a_full_world_roster_fits_in_one_reliable_message() {
    let friends = (0..MAX_LIST)
        .map(|i| Friend {
            name: format!("Character{i:023}"),
            mode: 13,
            location: "L".repeat(MAX_NAME),
        })
        .collect();
    let roster = Event::List {
        cx: u32::MAX,
        hr: 0,
        friends,
    };
    let encoded = roster.encode().unwrap();
    assert!(encoded.len() <= MAX_WORLD_PAYLOAD);
    assert_eq!(Event::decode(&encoded).unwrap(), roster);
}
