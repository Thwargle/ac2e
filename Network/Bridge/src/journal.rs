// Native progression messages shared by AC2E and Pyrealium servers.
use ac2_net::protocol::*;
use serde_json::{json, Value};

fn text(s: &StringInfo) -> Value { json!({"literal":s.literal,"table":s.table_id,"key":s.string_id}) }
fn skill(s: &SkillRecord, graph: Option<&PackedReference>) -> Value {
    // The pinned SkillRecord API exposes XP/training only. Retain the native
    // SkillInfo TIME[0..1] and flag 0x20 directly from its validated graph.
    let reset = graph.and_then(|g|g.entries.iter().find_map(|entry| {
        let PackedEntry::Heap {object,..}=entry else{return None};
        let v=&object.slots;
        if object.pkg_id!=0x231 || v.len()!=15 || v[14]!=s.skill || v[7]&0x20==0
            || object.tags.get(..2)!=Some(&[0xc3,0xcd]) {return None;}
        let stamp=f64::from_bits(u64::from(v[0]) | u64::from(v[1])<<32);
        (stamp.is_finite() && (0.0..=1.0e12).contains(&stamp)).then_some(stamp)
    })).unwrap_or(-1.);
    json!({"id":s.skill,"trained":s.trained(),"xp":s.xp.to_string(),"reset_started":reset})
}
pub fn repository(r: &SkillRepository, graph: Option<&PackedReference>) -> Value {
    json!({"event":"skills","replace":true,"credits":r.credits,"hero_credits":r.hero_credits,
        "untraining":r.untraining,"items":r.skills.iter().map(|s|skill(s,graph)).collect::<Vec<_>>()})
}
fn quest(q: &GmQuestInfo) -> Value {
    json!({"id":q.id,"name":text(&q.name),"description":text(&q.description),"journal":text(&q.journal),
        "icon":q.icon,"status":q.status,"phase":q.phase,"level":q.challenge_level,"done":q.done,
        "expires":q.expires,"remaining":q.seconds_remaining,"bestowal":q.bestowal,
        "retry":q.seconds_until_retry,"maxed_out":q.maxed_out,"changed":q.play_fx_on_update})
}
pub fn quests(v: &PackValue, replace: bool) -> Option<Value> {
    let PackValue::Reference(r)=v else {return None};
    let PackedEntry::Heap{object,..}=r.entries.first()? else {return None};
    let items=match object.native.as_ref()? {
        NativeObject::GmQuestInfo(q)=>vec![quest(q)],
        NativeObject::GmQuestInfoList(q)=>q.iter().map(quest).collect(),
        _=>return None,
    };
    Some(json!({"event":"quests","replace":replace,"items":items}))
}
pub fn decode(function: u32, args: &[u8]) -> Option<Value> {
    if function==0x82130024 {
        let values=unpack_quest_profile(args).ok()?;
        let PackValue::Int(error)=values.first()? else{return None};
        if *error!=0 {return Some(json!({"event":"error","message":format!("Quest update rejected ({error})")}));}
        let PackValue::Int(update)=values.get(2)? else{return None};
        let mut event=quests(values.get(1)?,false)?;
        event["update"]=json!(update);
        return Some(event);
    }
    let message=SkillMessage::decode(function,args)?;
    let values=unpack_values(args).ok();
    let graph=values.as_ref().and_then(|v|match v.first()? {PackValue::Reference(r)=>Some(r),_=>None});
    Some(match message {
        Ok(SkillMessage::SetRepository(r))=>repository(&r,graph),
        Ok(SkillMessage::SetSkills(s))=>json!({"event":"skills","replace":true,"items":s.iter().map(|s|skill(s,graph)).collect::<Vec<_>>()}),
        Ok(SkillMessage::UpdateSkill(s))=>json!({"event":"skills","replace":false,"items":[skill(&s,graph)]}),
        Ok(SkillMessage::RemoveSkill(id))=>json!({"event":"skill_remove","id":id}),
        Err(e)=>json!({"event":"error","message":format!("Cannot decode native skills: {e}")}),
    })
}

// Icefire appends an unidentified u32 to each quest. Keep retail decoding first,
// and restrict this fallback to the initial player profile and quest updates.
// Do not scan for tags or drop bytes from an otherwise unrecognized packet.
pub fn unpack_quest_profile(args: &[u8]) -> Result<Vec<PackValue>,ProtocolError> {
    let validate=|values:Vec<PackValue>| {
        for value in &values {
            if let PackValue::Reference(graph)=value {for entry in &graph.entries {
                if let PackedEntry::Heap{object,..}=entry {
                    if matches!(object.native,Some(NativeObject::GmQuestInfo(_)|NativeObject::GmQuestInfoList(_)))
                        && (!object.slots.is_empty() || !object.tags.is_empty()) {
                        // These native quest records have no script fields. A
                        // misaligned multi-quest list can otherwise appear to
                        // decode by treating its final words as heap slots.
                        return Err(ProtocolError::Invalid("quest native heap"));
                    }
                }
            }}
        }
        Ok(values)
    };
    unpack_values(args).and_then(validate)
        .or_else(|_|unpack_values_with_quest_extension(args).and_then(validate))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn quest_audio_reason_and_server_silence_survive_decode() {
        for extension in [None,Some(0)] {
            for update in [0x10000000,0x20000000,0x30000000,0x40000000] {
                for audible in [false,true] {
                    let q=GmQuestInfo {id:42,play_fx_on_update:audible,server_extension:extension,..Default::default()};
                    let args=pack_values(&[PackValue::Int(0),PackValue::native(NativeObject::GmQuestInfo(Box::new(q.clone()))),PackValue::Int(update)]);
                    let v=decode(0x82130024,&args).unwrap();
                    assert_eq!(v["update"],update);
                    assert_eq!(v["items"][0]["changed"],audible);
                    let args=pack_values(&[PackValue::Int(54),PackValue::native(NativeObject::GmQuestInfo(Box::new(q))),PackValue::Int(update)]);
                    assert_eq!(decode(0x82130024,&args).unwrap()["event"],"error");
                }
            }
        }
    }
    #[test]
    fn retail_and_extended_quest_lists_preserve_following_inventory_arguments() {
        for extension in [None,Some(0),Some(0x12345678)] {
            let q=|id|GmQuestInfo{id,phase:3,server_extension:extension,..Default::default()};
            let values=vec![PackValue::Int(7),PackValue::native(NativeObject::GmQuestInfoList(vec![q(42),q(43)])),PackValue::LongInt(u64::MAX)];
            let bytes=pack_values(&values);
            assert_eq!(unpack_quest_profile(&bytes).unwrap(),values);
            let mut invalid=bytes.clone();invalid.push(0);
            assert!(unpack_quest_profile(&invalid).is_err());
            invalid=bytes[..bytes.len()-1].to_vec();
            assert!(unpack_quest_profile(&invalid).is_err());
            let update=pack_values(&[PackValue::Int(0),PackValue::native(NativeObject::GmQuestInfo(Box::new(q(42)))),PackValue::Int(0)]);
            assert_eq!(decode(0x82130024,&update).unwrap()["items"][0]["phase"],3);
        }
    }
    #[test]
    fn cooldown_time_survives_individual_and_repository_updates() {
        let record=SkillRecord {skill:0x59,flags:0x21,xp:7};
        for message in [SkillMessage::UpdateSkill(record),SkillMessage::SetRepository(SkillRepository {
            skills:vec![record],..Default::default()
        })] {
            for stamp in [12345.125,0.,-1.,f64::NAN,f64::INFINITY] {
                let mut values=message.arguments();
                let PackValue::Reference(g)=&mut values[0] else{panic!("graph")};
                for e in &mut g.entries {
                    if let PackedEntry::Heap {object,..}=e {if object.pkg_id==0x231 {
                        object.slots[0]=stamp.to_bits() as u32;object.slots[1]=(stamp.to_bits()>>32) as u32;
                    }}
                }
                let value=decode(message.function(),&pack_values(&values)).unwrap();
                assert_eq!(value["items"][0]["reset_started"],if stamp.is_finite() && stamp>=0. {stamp}else{-1.});
                assert_eq!(value["items"][0]["xp"],"7");
            }
        }
        let m=SkillMessage::UpdateSkill(SkillRecord{flags:1,..record});
        assert_eq!(decode(m.function(),&pack_values(&m.arguments())).unwrap()["items"][0]["reset_started"],-1.);
    }
    #[test]
    fn native_skill_changes_keep_credits_training_and_full_width_xp() {
        let r=SkillRepository {credits:3,hero_credits:2,untraining:17,
            skills:vec![SkillRecord{skill:17,flags:1,xp:u64::MAX}],};
        let m=SkillMessage::SetRepository(r);
        let v=decode(m.function(),&pack_values(&m.arguments())).unwrap();
        assert_eq!(v["credits"],3);assert_eq!(v["items"][0]["trained"],true);
        assert_eq!(v["items"][0]["xp"],u64::MAX.to_string());
        let m=SkillMessage::RemoveSkill(17);
        assert_eq!(decode(m.function(),&pack_values(&m.arguments())).unwrap()["id"],17);
    }
    #[test]
    fn quest_lists_and_updates_keep_native_strings_and_status() {
        let q=GmQuestInfo {id:42,phase:3,status:0x30000000,challenge_level:3,bestowal:100.,seconds_remaining:60.,seconds_until_retry:72000.,expires:true,..Default::default()};
        let v=PackValue::Reference(PackedReference::native(NativeObject::GmQuestInfoList(vec![q.clone()])));
        assert_eq!(quests(&v,true).unwrap()["items"][0]["phase"],3);
        let args=pack_values(&[PackValue::Int(0),PackValue::Reference(PackedReference::native(NativeObject::GmQuestInfo(Box::new(q)))),PackValue::Int(0)]);
        let decoded=decode(0x82130024,&args).unwrap();
        assert_eq!(decoded["replace"],false);assert_eq!(decoded["items"][0]["status"],0x30000000u32);
        for (key,value) in [("level",json!(3)),("bestowal",json!(100.)),("remaining",json!(60.)),("retry",json!(72000.)),("expires",json!(true))] {
            assert_eq!(decoded["items"][0][key],value,"{key}");
        }
    }
}
