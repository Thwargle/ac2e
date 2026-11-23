//! Native WM_Allegiance actions and 1B5/1B1 profiles; names stay StringInfo.
use ac2_net::protocol::*;
use serde_json::{json,Value};
pub fn command(v:&Value)->Result<ClientEvent,String>{
    let action=crate::text(v,"action")?;
    let target=if action=="query"{v.get("id").and_then(Value::as_str).unwrap_or("0").parse().map_err(|_|"Invalid player identity")?}else{crate::id(v,"id")?};
    if target==u64::MAX||(target==0&&action!="query"){return Err("Select a player first.".into())}
    let function=match action{"query"=>0x82030005,"swear"=>0x8203000a,"break"=>0x82030009,"accept"=>0x8203001c,"decline"=>0x82030029,_=>return Err("Unknown allegiance action".into())};
    let mut args=vec![];if action=="decline"{args.push(PackValue::Int(0x1585));}args.push(PackValue::LongInt(target));
    Ok(ClientEvent::Interp{function,arguments:pack_values(&args),encrypted:false})
}
fn heap(g:&PackedReference,id:u32)->Option<&HeapObject>{let i=g.ids.iter().position(|n|*n==id)?;match g.entries.get(i)?{PackedEntry::Heap{object,..}=>Some(object),_=>None}}
fn string(g:&PackedReference,id:u32)->Value{heap(g,id).and_then(|h|h.native.as_ref()).and_then(|n|match n{NativeObject::StringInfo(s)=>Some(crate::events::string(s)),NativeObject::WString(s)=>Some(json!({"literal":s})),_=>None}).unwrap_or(json!({"literal":""}))}
fn member(g:&PackedReference,id:u32,online:bool)->Option<Value>{
    let h=heap(g,id)?;let s=&h.slots;if h.pkg_id!=0x1b1||s.len()!=14{return None}
    let id=u64::from(s[9])|(u64::from(s[10])<<32);if id==0||id==u64::MAX{return None}
    Some(json!({"id":id.to_string(),"name":string(g,s[13]),"title":string(g,s[4]),"level":s[3],"race":s[2],"gender":s[5],"rank":s[11],"online":online,"officer":s[8]!=0,"xp":(u64::from(s[0])|(u64::from(s[1])<<32)).to_string()}))
}
fn parse(f:u32,a:&[PackValue])->Option<Value>{match(f,a){
    (0x8203002e,[PackValue::Reference(g)])=>{
        let h=heap(g,*g.ids.first()?)?;let s=&h.slots;if h.pkg_id!=0x1b5||s.len()!=13{return None}
        let mut vassals=Vec::new();
        if s[6]!=u32::MAX {
            let NativeObject::List(list)=heap(g,s[6])?.native.as_ref()? else{return None};
            let NativeObject::List(flags)=heap(g,s[7])?.native.as_ref()? else{return None};
            if list.native_type!=ntype::R_LIST||flags.native_type!=ntype::A_LIST||list.items.len()!=flags.items.len()||list.items.len()>4096{return None}
            for (id,online) in list.items.iter().zip(&flags.items){vassals.push(member(g,u32::try_from(*id).ok()?,*online!=0)?);}
        }
        Some(json!({"event":"allegiance","name":string(g,s[0]),"motd":string(g,s[8]),"total":s[11],"me":member(g,s[2],s[4]!=0),"patron":member(g,s[1],s[5]!=0),"monarch":member(g,s[3],s[9]!=0),"vassals":vassals}))
    }
    (0x8203002f,[name,PackValue::LongInt(id)])=>{
        let NativeObject::StringInfo(name)=name.as_native()? else{return None};
        Some(json!({"event":"allegiance_invitation","id":id.to_string(),"name":crate::events::string(name)}))
    }
    (0x82030033|0x8203002d,[PackValue::Int(result),name,PackValue::LongInt(id)])=>{
        let NativeObject::StringInfo(name)=name.as_native()? else{return None};
        Some(json!({"event":"allegiance_result","result":result,"id":id.to_string(),"name":crate::events::string(name)}))
    }
    _=>None,
}}
pub fn decode(f:u32,a:&[u8])->Option<Value>{
    if ![0x8203002e,0x8203002f,0x82030033,0x8203002d].contains(&f){return None}
    Some(unpack_values(a).ok().and_then(|a|parse(f,&a)).unwrap_or_else(||json!({"event":"error","message":"Invalid allegiance update."})))
}
#[cfg(test)]mod tests{
    use super::*;
    #[test]fn action_arguments_are_native(){for action in ["query","swear","break","accept","decline"]{let ClientEvent::Interp{arguments,..}=command(&json!({"action":action,"id":"9308940437337407489"})).unwrap() else{panic!()};assert_eq!(unpack_values(&arguments).unwrap().last(),Some(&PackValue::LongInt(9308940437337407489)));}assert!(command(&json!({"action":"swear","id":"0"})).is_err());assert!(command(&json!({"action":"query"})).is_ok());}
}
