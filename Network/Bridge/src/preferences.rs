//! Native WM_Player::SetShortcut (0x82120018) and the options profile in LOAD.
use ac2_net::protocol::*;
use serde_json::{json, Value};

/// Match self-examination replies in their native ordered stream. Earlier UI
/// inspections must not satisfy the later logout barrier.
#[derive(Default)]
pub struct LogoutFence(std::collections::VecDeque<bool>);
impl LogoutFence {
    pub fn request(&mut self, barrier: bool) { self.0.push_back(barrier); }
    pub fn response(&mut self) -> bool { self.0.pop_front().unwrap_or(false) }
    pub fn cancel(&mut self) { for barrier in &mut self.0 { *barrier=false; } }
    pub fn idle(&self) -> bool { self.0.is_empty() }
}

pub fn command(v: &Value) -> Result<ClientEvent, String> {
    let slot = super::word(v, "slot")?;
    if slot >= 100 { return Err("Shortcut slot must be between 0 and 99.".into()); }
    let b = &v["binding"];
    let shortcut = match super::text(b, "kind")? {
        "empty" => ShortcutInfo::Empty(0),
        "skill" => ShortcutInfo::Enum { kind: 0x40000001, value: super::word(b, "value")? },
        "item" => ShortcutInfo::Instance(super::id(b, "id")?),
        "recipe" => ShortcutInfo::DataId(super::word(b, "value")?),
        "enum" => ShortcutInfo::Enum { kind: 0x40000004, value: super::word(b, "value")? },
        "text" => {
            let text = super::text(b, "text")?;
            if text.trim().is_empty() || text.encode_utf16().count() > 512 || text.chars().any(char::is_control) {
                return Err("Shortcut text must contain 1–512 characters without control characters.".into());
            }
            ShortcutInfo::Text(text.into())
        }
        _ => return Err("Unknown shortcut kind.".into()),
    };
    Ok(ClientEvent::Interp { function: 0x82120018, encrypted: false,
        arguments: pack_values(&[PackValue::native(NativeObject::ShortcutInfo(shortcut)), PackValue::Int(slot)]) })
}

pub fn profile(value: &PackValue) -> Option<Value> {
    let NativeObject::GameplayOptionsProfile(p) = value.as_native()? else { return None; };
    // Ordinary inventory refreshes omit this field. Absence is not an empty bar.
    if p.pack_bitfield & 2 == 0 { return None; }
    if p.shortcuts.len() > 100 { return Some(json!({"event":"error","message":"Invalid native shortcut count."})); }
    let items: Vec<_> = p.shortcuts.iter().map(|s| match s {
        ShortcutInfo::Empty(0) => json!({"kind":"empty"}),
        ShortcutInfo::Enum {kind: 0x40000001, value} => json!({"kind":"skill","value":value}),
        ShortcutInfo::Enum {kind: 0x40000004, value} => json!({"kind":"enum","value":value}),
        ShortcutInfo::Instance(id) => json!({"kind":"item","id":id.to_string()}),
        ShortcutInfo::DataId(value) => json!({"kind":"recipe","value":value}),
        ShortcutInfo::Text(text) => json!({"kind":"text","text":text}),
        _ => json!({"kind":"unsupported"}),
    }).collect();
    Some(json!({"event":"shortcuts","items":items}))
}

pub fn game_command(v: &Value) -> Result<ClientEvent, String> {
    let bits=super::word(v,"bits")?;
    let radar=super::word(v,"radar")?;
    let height=super::word(v,"height")?;
    let range=v["range"].as_f64().ok_or("Invalid damage text range.")? as f32;
    if !(1..=5).contains(&height) || !range.is_finite() || (range<0.0 && range != -1.0) {
        return Err("Invalid game options height or damage text range.".into());
    }
    // Native UploadGameOptions: three tagged integers and one tagged float.
    Ok(ClientEvent::Interp {function:0x8212000c, encrypted:false,
        arguments:pack_values(&[PackValue::Int(bits),PackValue::Int(radar),PackValue::Int(height),PackValue::Float(range)])})
}

pub fn game_profile(value: &PackValue) -> Option<Value> {
    let NativeObject::GameplayOptionsProfile(p)=value.as_native()? else {return None;};
    let fields=p.pack_bitfield & 0x10a8;
    // A full login with no saved options establishes a legacy/default baseline.
    // Inventory-only LOAD messages omit both preferences and shortcuts.
    if fields==0 && p.pack_bitfield & 2==0 {return None;}
    let mut out=json!({"event":"game_options","fields":fields});
    if fields & 0x80!=0 {out["bits"]=json!(p.bit_field);}
    if fields & 0x20!=0 {out["radar"]=json!(p.radar_mask);}
    if fields & 0x1000!=0 {out["height"]=json!(p.shortcut_height);}
    if fields & 8!=0 {
        if !p.damage_text_range_other.is_finite() {
            return Some(json!({"event":"error","message":"Invalid native game options range."}));
        }
        out["range"]=json!(p.damage_text_range_other);
    }
    Some(out)
}

#[cfg(test)] mod tests {
    use super::*;
    #[test] fn native_game_options_preserve_opaque_bits_and_float_sentinel() {
        for range in [-1.0,0.0,0.375,2.0] {
            let input=json!({"bits":0x8001a83du32,"radar":0x80000021u32,"height":5,"range":range});
            let ClientEvent::Interp {function,arguments,encrypted}=game_command(&input).unwrap() else {panic!()};
            assert_eq!(function,0x8212000c);assert!(!encrypted);assert_eq!(arguments.len(),32);
            assert_eq!(unpack_values(&arguments).unwrap(),vec![PackValue::Int(0x8001a83d),PackValue::Int(0x80000021),PackValue::Int(5),PackValue::Float(range as f32)]);
            let mut p=Box::new(GameplayOptionsProfile {pack_bitfield:0x10a8,bit_field:0x8001a83d,radar_mask:0x80000021,
                shortcut_height:5,damage_text_range_other:range as f32,..Default::default()});
            let result=game_profile(&PackValue::native(NativeObject::GameplayOptionsProfile(p.clone()))).unwrap();
            for key in ["bits","radar","height","range"] {assert_eq!(result[key].as_f64(),input[key].as_f64());}
            p.pack_bitfield=0;
            assert!(game_profile(&PackValue::native(NativeObject::GameplayOptionsProfile(p.clone()))).is_none());
            p.pack_bitfield=2;
            assert_eq!(game_profile(&PackValue::native(NativeObject::GameplayOptionsProfile(p.clone()))).unwrap(),json!({"event":"game_options","fields":0}));
            p.pack_bitfield=0x20;
            assert_eq!(game_profile(&PackValue::native(NativeObject::GameplayOptionsProfile(p))).unwrap(),json!({"event":"game_options","fields":0x20,"radar":0x80000021u32}));
        }
    }
    #[test] fn invalid_game_options_cannot_be_sent() {
        let base=json!({"bits":0,"radar":63,"height":1,"range":1});
        for (key,value) in [("bits",json!(-1)),("radar",json!(1.5)),("height",json!(0)),("height",json!(6)),
            ("range",json!(-0.5)),("range",json!(1e100)),("range",json!("1"))] {
            let mut input=base.clone();input[key]=value;assert!(game_command(&input).is_err());
        }
    }
    #[test] fn earlier_self_inspections_cannot_release_the_logout_barrier() {
        let mut fence=LogoutFence::default();fence.request(false);fence.request(false);fence.request(true);
        assert!(!fence.response());assert!(!fence.response());assert!(fence.response());assert!(!fence.response());
        fence.request(true);fence.cancel();fence.request(true);
        assert!(!fence.response());assert!(fence.response());
    }
    #[test] fn native_shortcuts_round_trip_without_truncating_ids_or_unicode() {
        for binding in [json!({"kind":"empty"}), json!({"kind":"skill","value":176}),
            json!({"kind":"item","id":"9308940437337407489"}), json!({"kind":"recipe","value":0x6f000001u32}),
            json!({"kind":"text","text":"/say café, 世界"}), json!({"kind":"enum","value":7})] {
            let ClientEvent::Interp {function, arguments, encrypted} = command(&json!({"slot":99,"binding":binding})).unwrap() else {panic!()};
            assert_eq!(function, 0x82120018); assert!(!encrypted);
            let values = unpack_values(&arguments).unwrap(); assert_eq!(values[1], PackValue::Int(99));
            let NativeObject::ShortcutInfo(s) = values[0].as_native().unwrap() else {panic!()};
            let p = PackValue::native(NativeObject::GameplayOptionsProfile(Box::new(GameplayOptionsProfile {
                pack_bitfield: 2, shortcuts: vec![s.clone()], ..Default::default() })));
            assert_eq!(profile(&p).unwrap()["items"][0], binding);
        }
    }
    #[test] fn absent_empty_and_malformed_profiles_and_updates_are_distinct() {
        let mut p = Box::new(GameplayOptionsProfile::default());
        assert!(profile(&PackValue::native(NativeObject::GameplayOptionsProfile(p.clone()))).is_none());
        p.pack_bitfield=2; p.shortcuts=vec![ShortcutInfo::Empty(0);100];
        assert_eq!(profile(&PackValue::native(NativeObject::GameplayOptionsProfile(p))).unwrap()["items"].as_array().unwrap().len(),100);
        for slot in [json!(-1),json!(100),json!(1.5)] {assert!(command(&json!({"slot":slot,"binding":{"kind":"empty"}})).is_err());}
        for text in ["x".repeat(513),"🙂".repeat(257),"\n".into()," ".into()] {
            assert!(command(&json!({"slot":0,"binding":{"kind":"text","text":text}})).is_err());
        }
        assert!(command(&json!({"slot":0,"binding":{"kind":"text","text":"🙂".repeat(256)}})).is_ok());
    }
}
