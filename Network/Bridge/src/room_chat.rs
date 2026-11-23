// SPDX-License-Identifier: AGPL-3.0-only
//! Retail room chat, carried by CChatEvent on game queue 4 (not local /say).
//! Legions WM_Communication 0x1B -> CPlayer 0x147 sets slot 31's room table.
//! Icefire's DAT uses WM_Communication 0x1E -> CPlayer 0x13A instead; its
//! ChannelData package is 0x1AF4. Both keep the room ID in ChannelData slot 3.
use ac2_net::protocol::*;
use serde_json::{json, Value};
use std::collections::BTreeMap;

const GENERAL: u32 = 0x80000; // CPlayerShell::CG's channel, NOT a room ID.

#[derive(Default)]
pub(crate) struct RoomChatState {
    pub turbine: bool,
    channels: BTreeMap<u32, u32>,
    context: u32,
    pending: BTreeMap<u32, ()>,
    fellowship: u32,
    allegiance: u32,
}

fn heap(graph: &PackedReference, id: u32) -> Option<&HeapObject> {
    let index = graph.ids.iter().position(|v| *v == id)?;
    match graph.entries.get(index)? {
        PackedEntry::Heap { object, .. } => Some(object),
        _ => None,
    }
}

fn channels(arguments: &[u8], package: u32) -> Option<BTreeMap<u32, u32>> {
    let values = unpack_values(arguments).ok()?;
    let [PackValue::Reference(graph)] = values.as_slice() else { return None };
    let root = heap(graph, *graph.ids.first()?)?;
    let Some(NativeObject::Hash(hash)) = &root.native else { return None };
    if hash.keys.len() != hash.values.len() { return None; }
    let mut result = BTreeMap::new();
    for (key, id) in hash.keys.iter().zip(&hash.values) {
        let HashKey::Value(channel) = key else { return None };
        let data = heap(graph, u32::try_from(*id).ok()?)?;
        if data.pkg_id != package { return None; }
        let room = *data.slots.get(3)?;
        if result.insert(u32::try_from(*channel).ok()?, room).is_some() { return None; }
    }
    Some(result)
}

impl RoomChatState {
    pub fn reset(&mut self) {
        self.channels.clear();
        self.pending.clear();
        self.fellowship = 0;
        self.allegiance = 0;
    }

    pub fn request(&mut self, message: &str) -> Result<Vec<u8>, String> {
        self.request_channel("general", message)
    }

    pub fn request_channel(&mut self, channel: &str, message: &str) -> Result<Vec<u8>, String> {
        if message.trim().is_empty() || message.len() > 2048 || message.chars().any(char::is_control) {
            return Err("Use /cg followed by a message (up to 2048 bytes).".into());
        }
        if !self.turbine {
            return Err("This server has not enabled the supported room-chat service.".into());
        }
        let (room, flags) = match channel {
            "general" => (self.channels.get(&GENERAL).copied().unwrap_or(0), 0),
            "fellowship" => (self.fellowship, 1),
            "allegiance" => (self.allegiance, 1),
            _ => return Err("Unknown chat channel.".into()),
        };
        if room == 0 { return Err(format!("No {channel} chat room is assigned to this character.")); }
        self.context = self.context.wrapping_add(1).max(1);
        if self.pending.len() >= 64 { self.pending.pop_first(); }
        self.pending.insert(self.context, ());
        // Native CPlayer::channel passes flags=0. The room comes from the
        // server; channel filter bits must never be sent as a room number.
        Ok(ChatRequest::SendToRoomById {
            context: self.context, room, text: message.to_owned(),
            extra: GunChatExtraInfo::new(flags, String::new()).encode(),
        }.to_packet().encode())
    }

    pub fn receive(&mut self, event: &ServerEvent) -> Option<Vec<Value>> {
        match event {
            // These calls also update the social models. Observe them without
            // consuming them: the fellowship/profile decoder still needs them.
            ServerEvent::Interp { function: 0x820c002b, arguments, .. } => {
                if let Ok(values) = unpack_values(arguments) {
                    if let [PackValue::Reference(graph)] = values.as_slice() {
                        if let Some(root) = graph.ids.first().and_then(|id| heap(graph, *id)) {
                            if root.pkg_id == 0x1f0 && root.slots.len() == 8 { self.fellowship = root.slots[3]; }
                        }
                    }
                }
                None
            }
            ServerEvent::Interp { function: 0x820c0028, .. } => { self.fellowship = 0; None }
            ServerEvent::Interp { function: 0x82030030, arguments, .. } => {
                if let Ok(values) = unpack_values(arguments) {
                    if let [PackValue::Int(room)] = values.as_slice() { self.allegiance = *room; }
                }
                None
            }
            ServerEvent::Interp { function, arguments, .. } if matches!(*function, 0x8205001B | 0x8205001E) => {
                let package = if *function == 0x8205001B { 0x2023 } else { 0x1AF4 };
                let Some(next) = channels(arguments, package) else {
                    return Some(vec![json!({"event":"error","message":"Cannot decode the server's chat channel assignments."})]);
                };
                self.channels = next;
                Some(vec![json!({"event":"chat_channels","turbine":self.turbine,
                    "general_room":self.channels.get(&GENERAL).copied().unwrap_or(0)})])
            }
            ServerEvent::Chat(data) => Some(match ChatMessage::decode(data) {
                Ok(ChatMessage::RoomChat(chat)) => {
                    let (label, kind) = if self.channels.get(&GENERAL) == Some(&chat.room) { ("General", GENERAL) }
                        else if self.fellowship != 0 && self.fellowship == chat.room { ("Fellowship", 0x40) }
                        else if self.allegiance != 0 && self.allegiance == chat.room { ("Allegiance", 0x80) }
                        else { ("Room", 0x10) };
                    vec![json!({"event":"chat","room":chat.room,"channel":label,"kind":kind,
                        "speaker":{"literal":chat.speaker},"text":{"literal":chat.text}})]
                }
                Ok(ChatMessage::Response(response)) => {
                    if response.method != METHOD_SEND_TO_ROOM_BY_ID || self.pending.remove(&response.context).is_none() {
                        vec![]
                    } else if response.result != 0 {
                        vec![json!({"event":"error","message":format!("Chat was rejected by the server ({}).",response.result)})]
                    } else { vec![] } // The server's room echo displays the message once.
                }
                Err(_) => vec![json!({"event":"error","message":"Cannot decode a server room-chat message."})],
            }),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests;
