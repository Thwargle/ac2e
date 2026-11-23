//! Build-7148 WM_Fellowship calls and the 1f0/1ee/1ef adaptor graph.
use ac2_net::protocol::*;
use serde_json::{json, Value};

pub fn command(v: &Value) -> Result<ClientEvent, String> {
    let (function, values) = match super::text(v,"action")? {
        "create" => {
            let name=super::text(v,"name")?;
            if name.is_empty() || name.trim()!=name || name.encode_utf16().count()>32 || name.chars().any(char::is_control) {return Err("Fellowship name must contain 1–32 characters.".into())}
            let looting=v.get("looting").and_then(Value::as_u64).unwrap_or(0);
            if looting>4{return Err("Invalid fellowship loot method.".into())}
            let shared_xp = match v.get("shared_xp") {
                Some(value) => value.as_bool().ok_or("Invalid fellowship experience mode.")?,
                None => false,
            };
            (0x820c001b,vec![PackValue::Int(looting as u32),PackValue::Int(u32::from(!shared_xp)),PackValue::native(NativeObject::WString(name.into()))])
        }
        "invite"|"accept"|"decline"|"dismiss" => {
            let id=super::id(v,"id")?;if [0,u64::MAX].contains(&id){return Err("Select a player first.".into())}
            let action=super::text(v,"action")?;
            let mut args=Vec::new();if action=="decline"{args.push(PackValue::Int(0x138d));}args.push(PackValue::LongInt(id));
            (match action{"invite"=>0x820c0015,"accept"=>0x820c000d,"decline"=>0x820c001a,_=>0x820c000e},args)
        }
        "leave"=>(0x820c001c,vec![]),"disband"=>(0x820c0003,vec![]),
        _=>return Err("Unknown fellowship action.".into()),
    };
    Ok(ClientEvent::Interp{function,arguments:pack_values(&values),encrypted:false})
}
fn heap(r:&PackedReference,id:u32)->Option<&HeapObject>{
    let i=r.ids.iter().position(|v|*v==id)?;
    match r.entries.get(i)?{PackedEntry::Heap{object,..}=>Some(object),_=>None}
}
fn fields(r:&PackedReference,id:u32,pkg:u32,count:usize)->Option<&[u32]>{
    let h=heap(r,id)?;(h.pkg_id==pkg && h.slots.len()==count && h.tags.len()==count).then_some(h.slots.as_slice())
}
fn parse(f:u32,a:&[PackValue])->Option<Value>{
    match (f,a) {
        (0x820c0028,[])=>Some(json!({"event":"fellowship_clear"})),
        (0x820c002e,[inviter,name,PackValue::LongInt(id)])=>{
            let NativeObject::StringInfo(inviter)=inviter.as_native()? else{return None};
            let NativeObject::StringInfo(name)=name.as_native()? else{return None};
            Some(json!({"event":"fellowship_invitation","id":id.to_string(),"inviter":crate::events::string(inviter),"name":crate::events::string(name)}))
        }
        (0x820c002b,[PackValue::Reference(r)])=>{
            let s=fields(r,*r.ids.first()?,0x1f0,8)?;
            let leader=u64::from(s[5])|u64::from(s[6])<<32;
            let NativeObject::WString(name)=heap(r,s[7])?.native.as_ref()? else{return None};
            let NativeObject::Hash(hash)=heap(r,s[4])?.native.as_ref()? else{return None};
            if hash.native_type!=0x40 || hash.keys.len()!=hash.values.len() || hash.keys.is_empty() || hash.keys.len()>8{return None}
            let mut members=Vec::new();let mut ids=std::collections::BTreeSet::new();
            for (key,value) in hash.keys.iter().zip(&hash.values){
                let HashKey::Value(id)=key else{return None};
                if [0,u64::MAX].contains(id)||!ids.insert(*id){return None}
                let m=fields(r,u32::try_from(*value).ok()?,0x1ee,5)?;
                let v=fields(r,m[3],0x1ef,7)?;
                let NativeObject::StringInfo(n)=heap(r,m[4])?.native.as_ref()? else{return None};
                if v[2]>v[0]||v[5]>v[4]{return None}
                members.push(json!({"id":id.to_string(),"name":crate::events::string(n),"level":m[2],"health":v[2],"max_health":v[0],"vigor":v[5],"max_vigor":v[4],"cell":v[1]}));
            }
            if !ids.contains(&leader){return None}
            let looting=match s[2]&30{2=>1,4=>2,8=>3,16=>4,_=>0};
            Some(json!({"event":"fellowship","name":name,"leader":leader.to_string(),"flags":s[2],"looting":looting,"social":s[2]&1==0,"members":members}))
        }
        (0x820c0030|0x820c002d,[PackValue::Int(_),PackValue::Int(value),PackValue::LongInt(id)])=>
            Some(json!({"event":"fellowship_vital","id":id.to_string(),"field":if f==0x820c0030{"health"}else{"vigor"},"value":value})),
        (0x820c0033|0x820c0021|0x820c003f,[PackValue::Int(value),PackValue::LongInt(id)])=>
            Some(json!({"event":"fellowship_vital","id":id.to_string(),"field":match f{0x820c0033=>"max_health",0x820c0021=>"max_vigor",_=>"cell"},"value":value})),
        _=>None,
    }
}
pub fn decode(f:u32,bytes:&[u8])->Option<Value>{
    if !matches!(f,0x820c0028|0x820c002e|0x820c002b|0x820c0030|0x820c002d|0x820c0033|0x820c0021|0x820c003f){return None}
    Some(unpack_values(bytes).ok().and_then(|a|parse(f,&a)).unwrap_or_else(||json!({"event":"error","message":"Invalid fellowship update."})))
}
#[cfg(test)]mod tests{
    use super::*;
    #[test]fn requests_preserve_identity_and_independent_reward_options(){
        for action in ["invite","accept","dismiss","decline"]{
            let ClientEvent::Interp{arguments,..}=command(&json!({"action":action,"id":"9308940437337407489"})).unwrap() else{panic!()};
            assert_eq!(unpack_values(&arguments).unwrap().last(),Some(&PackValue::LongInt(9308940437337407489)));
        }
        let ClientEvent::Interp{arguments,..}=command(&json!({"action":"create","name":"Dereth travelers"})).unwrap() else{panic!()};
        assert_eq!(&unpack_values(&arguments).unwrap()[..2],&[PackValue::Int(0),PackValue::Int(1)]);
        for looting in 0..=4 {
            for shared_xp in [false, true] {
                let ClientEvent::Interp { arguments, .. } = command(&json!({
                    "action": "create", "name": "Loot test", "looting": looting, "shared_xp": shared_xp
                })).unwrap() else { panic!() };
                assert_eq!(&unpack_values(&arguments).unwrap()[..2],
                    &[PackValue::Int(looting), PackValue::Int(u32::from(!shared_xp))]);
            }
        }
        assert!(command(&json!({"action":"create","name":"Loot test","shared_xp":5})).is_err());
        assert!(command(&json!({"action":"create","name":"Loot test","looting":5})).is_err());
        assert!(command(&json!({"action":"create","name":"bad\nname"})).is_err());
        assert_eq!(decode(0x820c0028,&[]).unwrap()["event"],"fellowship_clear");
        assert_eq!(decode(0x820c002b,&[0]).unwrap()["event"],"error");
    }
}
