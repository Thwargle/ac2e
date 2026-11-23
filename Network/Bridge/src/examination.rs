// SPDX-License-Identifier: AGPL-3.0-only
//! Admin7148 ExaminationRequest::StreamPack 00609660 and ExaminationProfile::StreamPack
//! 00608380. Legions adds a request flag and a word after the profile's node list.
use ac2_net::protocol::*;
use serde_json::{json, Value};

pub fn request(id: u64, build: Build) -> ClientEvent {
    if build==Build::Legions7160 {
        return ScriptRequest::Examine(ExaminationRequest::object(id)).to_event();
    }
    let words = [
        5,
        1,
        1,
        0x44000100,
        0,
        0xffff009a,
        0x40000002,
        0,
        id as u32,
        (id >> 32) as u32,
        0,
    ];
    ClientEvent::Interp {
        encrypted: false,
        function: 0x820a0007,
        arguments: words.into_iter().flat_map(u32::to_le_bytes).collect(),
    }
}

fn profile(bytes: &[u8]) -> Option<ExaminationProfile> {
    let decode = |data: &[u8]| match ScriptMessage::decode(0x820a0009, data)? {
        Ok(ScriptMessage::ExaminationProfile(p)) => Some(p),
        _ => None,
    };
    if let Some(p) = decode(bytes) {
        return Some(p);
    }
    // Only adapt a complete, bounded native object-profile header; the pinned codec still
    // validates node types, references, strings, counts and trailing heap fields.
    if (48..=65536).contains(&bytes.len()) {
        let word = |n: usize| u32::from_le_bytes(bytes[n * 4..n * 4 + 4].try_into().unwrap());
        if word(0) == 5
            && word(1) == 1
            && word(2) != u32::MAX
            && word(3) == 0x44000100
            && word(4) == 0
            && word(5) == 0xffff0099
            && word(6) == 0x40000002
            && word(7) == 0
            && word(10) <= 1024
            && bytes[bytes.len()-4..] == [0;4]
        {
            let mut adapted = bytes.to_vec();
            adapted.splice(40..40, 0u32.to_le_bytes());
            let heap = adapted.len()-4;
            adapted.splice(heap..heap, 0u32.to_le_bytes());
            if let Some(p) = decode(&adapted) {
                return Some(p);
            }
        }
    }
    None
}

fn kind(v: &ExaminationValue) -> u32 {
    0x40000000
        | match v {
            ExaminationValue::Int(_) => 1,
            ExaminationValue::Float(_) => 2,
            ExaminationValue::Bool(_) => 3,
            ExaminationValue::Text(_) => 4,
            ExaminationValue::Image(_) => 5,
            ExaminationValue::Break => 6,
            ExaminationValue::Tabs(_) => 7,
            ExaminationValue::Countdown(_) => 8,
            ExaminationValue::Icon(_) => 9,
            ExaminationValue::LongInt(_) => 10,
            ExaminationValue::Effect(_) => 12,
            ExaminationValue::Other(k) => *k,
        }
}

pub fn convert(bytes: &[u8]) -> Option<Value> {
    let p = profile(bytes)?;
    let nodes: Vec<Value> = p
        .nodes
        .iter()
        .map(|n| {
            let mut out = json!({"kind":kind(&n.value),"appearance":n.appearance,"order":n.order});
            match &n.value {
                ExaminationValue::Text(s) => {
                    out["text"] = crate::events::string(&s)
                }
                ExaminationValue::Int(v) => out["value"] = json!(v),
                ExaminationValue::Float(v) => out["value"] = json!(v),
                ExaminationValue::Bool(v) => out["value"] = json!(v),
                ExaminationValue::LongInt(v) => out["value"] = json!(v.to_string()),
                ExaminationValue::Countdown(v) => out["value"] = json!(v),
                ExaminationValue::Image(v) => out["image"] = json!(v),
                ExaminationValue::Tabs(v) => out["value"] = json!(v),
                ExaminationValue::Icon(layers) => out["layers"] = json!(layers.iter().map(|l|
                    json!({"layer":l.layer,"image":l.image,"color":l.color})).collect::<Vec<_>>()),
                ExaminationValue::Effect(v) => out["effect"] = json!(v),
                _ => (),
            }
            out
        })
        .collect();
    Some(json!({"event":"examination","id":p.request.iid.to_string(),"nodes":nodes}))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn admin_request_keeps_unsigned_id_and_has_no_legions_union_word() {
        let ClientEvent::Interp {
            function,
            arguments,
            ..
        } = request(0x814000000000002a, Build::Admin7148)
        else {
            panic!()
        };
        assert_eq!(function, 0x820a0007);
        assert_eq!(arguments.len(), 44);
        assert_eq!(&arguments[32..40], &0x814000000000002au64.to_le_bytes());
        assert_eq!(&arguments[40..], &[0; 4]);
    }
    #[test]
    fn legions_request_includes_the_authored_object_flags_before_heap_size() {
        let ClientEvent::Interp{function,arguments,encrypted}=request(0x800000000000002a,Build::Legions7160) else {panic!()};
        assert_eq!(function,0x820a0007);assert!(!encrypted);
        assert_eq!(arguments.len(),48);
        assert_eq!(&arguments[32..40],&0x800000000000002au64.to_le_bytes());
        assert_eq!(&arguments[40..44],&0x203u32.to_le_bytes());
        assert_eq!(&arguments[44..48],&0u32.to_le_bytes());
        let ScriptRequest::Examine(decoded)=ScriptRequest::decode(function,&arguments).unwrap().unwrap() else {panic!()};
        assert_eq!(decoded,ExaminationRequest::object(0x800000000000002a));
    }
    #[test]
    fn rich_profile_retains_icon_layers_styles_tabs_and_empty_lines() {
        use ac2_net::protocol::IconLayer;
        let p = ExaminationProfile {
            request: ExaminationRequest::object(42), extra: 0,
            nodes: vec![
                ExaminationDataNode {order:2,appearance:0,value:ExaminationValue::Icon(vec![
                    IconLayer {layer:1,image:0x41000001,color:0xff123456},
                    IconLayer {layer:2,image:0x41000002,color:0xffffffff}])},
                ExaminationDataNode {order:2,appearance:0x6005ff6b,value:ExaminationValue::Text(StringInfo {literal:Some("Enchanted focus".into()),..Default::default()})},
                ExaminationDataNode {order:2,appearance:0,value:ExaminationValue::Break},
                ExaminationDataNode {order:2,appearance:0,value:ExaminationValue::Break},
                ExaminationDataNode {order:2,appearance:0,value:ExaminationValue::Tabs(2)},
                ExaminationDataNode {order:2,appearance:0xbe5b6513,value:ExaminationValue::LongInt(9_007_199_254_740_993)},
                ExaminationDataNode {order:2,appearance:1,value:ExaminationValue::Effect(123)},
            ],
        };
        let bytes=pack_values(&[PackValue::native(NativeObject::ExaminationProfile(p))]);
        let out=convert(&bytes).unwrap(); let n=out["nodes"].as_array().unwrap();
        assert_eq!(n.len(),7);
        assert_eq!(n[0]["layers"][0]["color"],0xff123456u32);
        assert_eq!(n[0]["layers"][1]["image"],0x41000002u32);
        assert_eq!(n[1]["appearance"],0x6005ff6bu32);
        assert_eq!(n[2]["kind"],0x40000006u32);assert_eq!(n[3]["kind"],n[2]["kind"]);
        assert_eq!(n[4]["value"],2);assert_eq!(n[5]["value"],"9007199254740993");
        assert_eq!(n[6]["effect"],123);assert_eq!(n[6]["appearance"],1);
    }
    #[test]
    fn both_profile_layouts_decode_without_changing_names_or_ids() {
        let p = ExaminationProfile {
            request: ExaminationRequest::object(0x814000000000002a),
            extra: 0,
            nodes: vec![
                ExaminationDataNode {
                    order: 2,
                    appearance: 0xbe5b6513,
                    value: ExaminationValue::Text(StringInfo {
                        literal: Some("Training creature".into()),
                        ..Default::default()
                    }),
                },
                ExaminationDataNode {
                    order: 2,
                    appearance: 0,
                    value: ExaminationValue::Break,
                },
            ],
        };
        let bytes = pack_values(&[PackValue::native(NativeObject::ExaminationProfile(p))]);
        let modern = convert(&bytes).unwrap();
        let mut admin = bytes.clone();
        admin.drain(admin.len()-8..admin.len()-4);
        admin.drain(40..44);
        assert_eq!(convert(&admin).unwrap(), modern);
        assert_eq!(modern["id"], 0x814000000000002au64.to_string());
        assert_eq!(modern["nodes"][0]["text"]["literal"], "Training creature");
        for end in [0, 4, 24, 40, admin.len() - 1] {
            assert!(convert(&admin[..end]).is_none());
        }
        admin[20] = 0;
        assert!(convert(&admin).is_none());
    }
}
