//! Native CChatEvent room service. Identity and recipients come from the
//! authenticated world session; client-supplied names/extra flags are not trusted.
use ac2_net::protocol::*;
use crate::{script, world::WorldError};

pub const EVENT: u32 = 0x100d3;
pub const GENERAL: u32 = 1;

pub fn assignments() -> Result<Vec<u8>, WorldError> {
    let mut graph = PackedReference::native(NativeObject::Hash(ScriptHash {
        native_type: ntype::AR_HASH, bucket_index: 0,
        keys: vec![HashKey::Value(0x80000)], values: vec![20],
    }));
    graph.ids.push(20);
    graph.entries.push(PackedEntry::Heap { flags: 0x44000100, object: HeapObject {
        pkg_id: 0x2023, pkg_state_id: 0, native: None,
        slots: vec![0, 0, 0, GENERAL, 0, 0, 0], tags: vec![2; 7],
    }});
    let args = pack_values(&[PackValue::Reference(graph)]);
    script::private_call(0x8205001b, &args)
}

pub fn decode(bytes: &[u8]) -> Result<ChatRequest, WorldError> {
    if bytes.len() > 8192 { return Err(WorldError::LimitExceeded); }
    let packet = ChatPacket::decode(bytes).map_err(|_| WorldError::InvalidValue)?;
    ChatRequest::from_packet(&packet).map_err(|_| WorldError::InvalidValue)
}

fn event(message: ChatMessage) -> Vec<u8> {
    let body = message.to_packet().encode();
    let mut wire = EVENT.to_le_bytes().to_vec();
    wire.extend((body.len() as u32).to_le_bytes());
    wire.extend(body);
    wire
}

/// A rejected request still receives its context response so the client can
/// retire the pending send. Never permit arbitrary room IDs or spoofed speakers.
pub fn reply(request: &ChatRequest, speaker: &str, rate_allowed: bool) -> (Vec<u8>, Option<Vec<u8>>) {
    reply_in_rooms(request, speaker, rate_allowed, &[GENERAL])
}

pub fn room(request: &ChatRequest) -> Option<u32> {
    match request {
        ChatRequest::SendToRoomById { room, .. } => Some(*room),
        ChatRequest::SendToRoomByName { room, .. } if room.eq_ignore_ascii_case("General") => Some(GENERAL),
        _ => None,
    }
}

pub fn reply_in_rooms(request: &ChatRequest, speaker: &str, rate_allowed: bool, authorized: &[u32]) -> (Vec<u8>, Option<Vec<u8>>) {
    let (context, method, allowed, text) = match request {
        ChatRequest::SendToRoomById { context, room, text, .. } =>
            (*context, METHOD_SEND_TO_ROOM_BY_ID, authorized.contains(room), text),
        ChatRequest::SendToRoomByName { context, room, text, .. } =>
            (*context, METHOD_SEND_TO_ROOM_BY_NAME, room.eq_ignore_ascii_case("General"), text),
    };
    let valid = allowed && rate_allowed && !text.trim().is_empty() && text.len() <= 2048
        && !text.chars().any(char::is_control);
    let response = event(ChatMessage::Response(ChatResponse {
        context, response_id: context, method, result: if valid { 0 } else { 1 },
    }));
    let broadcast = valid.then(|| event(ChatMessage::RoomChat(RoomChat {
        room: room(request).unwrap(), speaker: speaker.into(), text: text.clone(),
        extra: GunChatExtraInfo::new(u32::from(room(request) != Some(GENERAL)), String::new()).encode(),
    })));
    (response, broadcast)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn private_room_needs_current_membership() {
        let request=ChatRequest::SendToRoomById { context:8,room:713,text:"Fellows only".into(),extra:vec![] };
        assert!(reply_in_rooms(&request,"Player",true,&[GENERAL]).1.is_none());
        let (_,message)=reply_in_rooms(&request,"Player",true,&[GENERAL,713]);
        let ChatMessage::RoomChat(chat)=ChatMessage::decode(&message.unwrap()[8..]).unwrap() else {panic!()};
        assert_eq!(chat.room,713);
        assert!(reply_in_rooms(&request,"Player",true,&[GENERAL,714]).1.is_none());
    }
    #[test]
    fn general_echo_uses_authenticated_speaker_and_returns_context() {
        let request = ChatRequest::SendToRoomById {
            context: 47, room: GENERAL, text: "Hello, Dereth!".into(), extra: vec![255; 64],
        };
        assert_eq!(decode(&request.to_packet().encode()).unwrap(), request);
        let (response, message) = reply(&request, "Actual Player", true);
        let ChatMessage::Response(result) = ChatMessage::decode(&response[8..]).unwrap() else { panic!() };
        assert_eq!((result.context, result.result), (47, 0));
        let wire = message.unwrap();
        let ChatMessage::RoomChat(chat) = ChatMessage::decode(&wire[8..]).unwrap() else { panic!() };
        assert_eq!((&*chat.speaker, &*chat.text, chat.room), ("Actual Player", "Hello, Dereth!", GENERAL));
        assert_eq!(GunChatExtraInfo::decode(&chat.extra).unwrap().weenie_flags, 0);
        assert!(reply(&request, "Actual Player", false).1.is_none());
    }
    #[test]
    fn rejects_unassigned_rooms_and_invalid_text() {
        for (room, text) in [(2, "hello".into()), (GENERAL, "".into()), (GENERAL, "bad\ntext".into()), (GENERAL, "x".repeat(2049))] {
            let request = ChatRequest::SendToRoomById { context: 1, room, text, extra: vec![] };
            assert!(reply(&request, "Player", true).1.is_none());
        }
        assert!(decode(&vec![0; 8193]).is_err());
    }
}
