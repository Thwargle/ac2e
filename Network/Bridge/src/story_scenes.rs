// SPDX-License-Identifier: AGPL-3.0-only
//! WM_Quest scene playback. The client returns the scene list to the server
//! when gmStoryUI finishes playback (including a player skip).
use ac2_net::protocol::{
    pack_values, unpack_values, ClientEvent, GmSceneInfo, NativeObject, PackValue, ServerEvent,
};
use serde_json::{json, Value};

#[derive(Default)]
pub(crate) struct StoryScenes {
    pending: Option<Vec<GmSceneInfo>>,
}

impl StoryScenes {
    pub(crate) fn receive(&mut self, event: &ServerEvent) -> Option<Value> {
        let ServerEvent::Interp {
            function: 0x82130021,
            arguments,
            object: None,
        } = event
        else {
            return None;
        };
        let decoded = unpack_values(arguments);
        let scenes = match decoded.as_deref() {
            Ok([item]) => match item.as_native() {
                Some(NativeObject::GmSceneInfoList(scenes)) => Some(scenes),
                _ => None,
            },
            _ => None,
        };
        let Some(scenes) = scenes.filter(|list| !list.is_empty()) else {
            return Some(json!({"event":"error","message":"Invalid native story scene list"}));
        };
        let rows: Vec<_> = scenes.iter().map(|scene| {
            let frames: Vec<_> = scene.keyframes.iter().map(|frame| json!({
                "number":frame.frame_num, "duration":frame.duration,
                "video":frame.video_fragment, "audio":frame.audio_fragment,
                "voice":frame.voice_fragment, "text":crate::events::string(&frame.text_fragment),
                "movie":frame.movie_fragment_filename, "tracks":frame.defined_tracks,
            })).collect();
            json!({"id":scene.id,"name":crate::events::string(&scene.name),"act":scene.act_id,
                "number":scene.scene_num,"hidden":scene.is_hidden!=0,"playable":scene.is_playable!=0,"frames":frames})
        }).collect();
        self.pending = Some(scenes.clone());
        Some(json!({"event":"story_scenes","scenes":rows}))
    }

    pub(crate) fn finished(&self, id: u32) -> Option<ClientEvent> {
        let scenes = self.pending.as_ref()?;
        if scenes.first()?.id != id {
            return None;
        }
        Some(ClientEvent::Interp {
            function: 0x82130013,
            arguments: pack_values(&[PackValue::native(NativeObject::GmSceneInfoList(
                scenes.clone(),
            ))]),
            encrypted: false,
        })
    }

    pub(crate) fn clear(&mut self) {
        self.pending = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ac2_net::protocol::{GmKeyframe, StringInfo};

    fn play(arguments: Vec<u8>) -> ServerEvent {
        ServerEvent::Interp {
            function: 0x82130021,
            arguments,
            object: None,
        }
    }

    #[test]
    fn ordered_scenes_keep_native_tracks_and_completion_uses_the_presented_list() {
        let frame = GmKeyframe {
            scene_id: 17,
            frame_num: 0,
            duration: -1.0,
            video_fragment: 23,
            audio_fragment: 0,
            voice_fragment: 8,
            text_fragment: StringInfo::default(),
            movie_fragment_filename: String::new(),
            defined_tracks: 21,
        };
        let scene = GmSceneInfo {
            id: 17,
            name: StringInfo::default(),
            act_id: 9,
            scene_num: 2,
            is_hidden: 0,
            is_playable: 1,
            keyframes: vec![frame],
        };
        let args = pack_values(&[PackValue::native(NativeObject::GmSceneInfoList(vec![
            scene.clone(),
            GmSceneInfo { id: 18, ..scene },
        ]))]);
        let mut playback = StoryScenes::default();
        let display = playback.receive(&play(args.clone())).unwrap();
        assert_eq!(display["scenes"][1]["id"], 18);
        assert_eq!(display["scenes"][0]["frames"][0]["tracks"], 21);
        assert_eq!(display["scenes"][0]["frames"][0]["duration"], -1.0);
        assert!(playback.finished(18).is_none());
        assert_eq!(
            playback.finished(17),
            Some(ClientEvent::Interp {
                function: 0x82130013,
                arguments: args,
                encrypted: false
            })
        );
        playback.clear();
        assert!(playback.finished(17).is_none());
        assert_eq!(
            playback.receive(&play(vec![5, 0, 0, 0])).unwrap()["event"],
            "error"
        );
        assert!(playback.finished(17).is_none());
    }

    #[test]
    #[ignore = "Set AC2_MENHIR_CAPTURE_DIR to the directory containing the reassembled retail play and completion messages"]
    fn replay_all_three_racial_training_exits() {
        let path = std::path::PathBuf::from(std::env::var_os("AC2_MENHIR_CAPTURE_DIR").unwrap());
        for race in ["human", "lugian", "tumerok"] {
            let trace = std::fs::read(path.join(format!("menhir-travel-{race}.wire"))).unwrap();
            let expected = std::fs::read(path.join(format!("menhir-done-{race}.wire"))).unwrap();
            for native in [false, true] {
                let mut events = crate::events::SelectionReturn::new(native);
                let mut playback = StoryScenes::default();
                let mut output = Vec::new();
                let mut bytes = trace.as_slice();
                while !bytes.is_empty() {
                    let length = u32::from_le_bytes(bytes[..4].try_into().unwrap()) as usize;
                    let event = ServerEvent::decode(
                        &bytes[4..4 + length],
                        ac2_net::protocol::Build::Legions7160,
                    )
                    .unwrap();
                    if let Some(scene) = playback.receive(&event) {
                        output.push(scene);
                    } else {
                        output.extend(events.convert(event));
                    }
                    bytes = &bytes[4 + length..];
                }
                assert_eq!(output[0]["event"], "story_scenes", "{race}: {output:?}");
                let scene = &output[0]["scenes"][0];
                assert_eq!(scene["id"], 0x40000021u32);
                assert_eq!(scene["act"], 0x40000008u32);
                assert_eq!(scene["frames"].as_array().unwrap().len(), 19);
                let fields = |event: &str, key: &str| {
                    output
                        .iter()
                        .filter(|v| v["event"] == event)
                        .map(|v| v[key].clone())
                        .collect::<Vec<_>>()
                };
                assert_eq!(
                    fields("portal_space", "active"),
                    [json!(true), json!(false)]
                );
                assert_eq!(
                    fields("fx", "fx"),
                    [
                        json!(0x4100000fu32),
                        json!(0x8100028eu32),
                        json!(0x410004fau32),
                        json!(0x41000010u32)
                    ]
                );
                let cells = fields("position", "cell");
                let expected_cell = match race {
                    "human" => 0x74ba0008u32,
                    "lugian" => 0x6cb90034u32,
                    _ => 0x8db70024u32,
                };
                assert_eq!(cells.len(), 2);
                assert_ne!(cells[0], cells[1]);
                assert_eq!(cells[1], expected_cell);
                let (queue, done) = playback.finished(0x40000021).unwrap().encode();
                assert_eq!(queue, 3);
                // The sender allocates its own VM heap reference when echoing the list.
                assert_eq!(expected[..20], done[..20]);
                assert_eq!(expected[24..], done[24..]);
            }
        }
    }
}
