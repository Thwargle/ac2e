//! Bulk inventory class IDs are allocated by each DAT's WLib, not by the wire
//! build number. The reviewed stock and Icefire layouts are equal; IDs differ.
use ac2_net::protocol::*;
use serde_json::{json,Value};

#[derive(Clone,Copy)]
pub(crate) struct Profile { take:u32, transmute:u32 }
impl Profile {
    pub(crate) fn stock()->Self {Self{take:0x20ef,transmute:0x234b}}
    pub(crate) fn load(directory:&str)->Result<Self,String> {
        let data=ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::for_retail_install(directory))
            .map_err(|e|format!("Cannot read inventory DAT: {e}"))?;
        let store=ac2_db::ResourceStore::new(data);
        let lib=ac2_script::load_wlib(&store,ac2_core::DataId::new(ac2_script::LEGIONS_WLIB_DID))
            .map_err(|e|format!("Cannot read inventory scripts: {e}"))?.ok_or("Missing inventory script library")?;
        let resolve=|name:&str,size:u32,known:&[u32]|->Result<u32,String>{
            let tables=&lib.stream.vtables;
            let pid=tables.package_names.iter().find(|(n,_)|n.as_slice()==name.as_bytes())
                .ok_or_else(||format!("Missing {name}"))?.1;
            if !known.contains(&pid) || tables.package_info.get(pid as usize).map(|p|p.size)!=Some(size) {
                return Err(format!("Unsupported {name} layout in selected DAT"));
            }
            Ok(pid)
        };
        Ok(Self{take:resolve("InvTakeAllDesc",14,&[0x20ef,0x1b58])?,
            transmute:resolve("InvTransmuteAllDesc",15,&[0x234b,0x1b59])?})
    }
    pub(crate) fn request(self,container:u64,player:u64,transmute:bool)->ClientEvent {
        let (function,pid,mut r,ids)=if transmute {
            (0x820e006e,self.transmute,InvTransmuteAllDesc::new(container,player).to_reference(),[8,13])
        }else{(0x820e0069,self.take,InvTakeAllDesc::new(container,player).to_reference(),[6,12])};
        if let PackedEntry::Heap{object,..}=&mut r.entries[0] {
            object.pkg_id=pid;
            for at in ids {object.tags[at]=0x84;}
        }
        ClientEvent::Interp{function,arguments:pack_values(&[PackValue::Reference(r)]),encrypted:false}
    }
}

pub(crate) fn decode(function:u32,arguments:&[u8])->Option<Value> {
    if !matches!(function,0x820e006a|0x820e006d){return None;}
    Some(result(function,arguments).unwrap_or_else(||json!({"event":"error","message":"Unsupported bulk inventory reply"})))
}
fn result(function:u32,arguments:&[u8])->Option<Value> {
    let values=unpack_values(arguments).ok()?;
    let [PackValue::Reference(r)]=values.as_slice() else{return None};
    let PackedEntry::Heap{object,..}=r.entries.first()? else{return None};
    let transmute=function==0x820e006d;
    let (packages,size,source,owner,status,taken,remaining)=if transmute {
        ([0x234b,0x1b59],15,8,13,7,2,10)
    }else{([0x20ef,0x1b58],14,6,12,5,11,8)};
    if !packages.contains(&object.pkg_id) || object.native.is_some() || object.slots.len()!=size || object.tags.len()!=size{return None;}
    let long=|at:usize|->Option<String>{
        if !matches!(object.tags[at],0x82|0x84) || object.tags[at+1]!=0xcd{return None;}
        Some((u64::from(object.slots[at])|(u64::from(object.slots[at+1])<<32)).to_string())
    };
    let list=|slot:usize|->Option<Vec<String>> {
        if object.tags[slot]!=1{return None;}
        let id=object.slots[slot];if id==u32::MAX{return Some(vec![])};
        let entry=r.entries.get(r.ids.iter().position(|v|*v==id)?)?;
        let PackedEntry::Heap{object,..}=entry else{return None};
        let Some(NativeObject::List(list))=&object.native else{return None};
        if list.native_type!=ntype::L_LIST{return None;}
        Some(list.items.iter().map(u64::to_string).collect())
    };
    Some(json!({"event":"inventory_bulk","id":long(source)?,"owner":long(owner)?,
        "status":object.slots[status],"transmute":transmute,"items":list(taken)?,"remaining":list(remaining)?}))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn partial_bulk_success_preserves_successful_and_rejected_unsigned_ids() {
        let mut r=InvTakeAllDesc::new(7,9).to_reference();
        let PackedEntry::Heap{object,..}=&mut r.entries[0] else{panic!()};
        object.pkg_id=0x1b58;object.slots[11]=2;object.slots[8]=3;object.slots[5]=54;
        for (id,item) in [(2,0x8000000000000123),(3,0x8000000000000456)] {
            r.ids.push(id);
            r.entries.push(PackedEntry::Heap{flags:entry_flags::LOADED|entry_flags::RECURSE|entry_flags::KIND_HEAP,
                object:HeapObject{pkg_state_id:0,pkg_id:0,native:Some(NativeObject::List(ScriptList{
                    native_type:ntype::L_LIST,items:vec![item]})),slots:vec![],tags:vec![]}});
        }
        let e=decode(0x820e006a,&pack_values(&[PackValue::Reference(r)])).unwrap();
        assert_eq!(e["status"],54);assert_eq!(e["items"],json!(["9223372036854776099"]));
        assert_eq!(e["remaining"],json!(["9223372036854776918"]));
    }
    #[test]
    fn both_reviewed_profiles_decode_and_bad_layouts_fail() {
        for p in [Profile::stock(),Profile{take:0x1b58,transmute:0x1b59}] {
            for transmute in [false,true] {
                let ClientEvent::Interp{arguments,..}=p.request(0x8000000000000123,0x2000000000000456,transmute) else{panic!()};
                let function=if transmute{0x820e006d}else{0x820e006a};
                let e=decode(function,&arguments).unwrap();
                assert_eq!(e["event"],"inventory_bulk");assert_eq!(e["id"],"9223372036854776099");
                assert_eq!(e["owner"],"2305843009213695062");assert_eq!(e["status"],0);
                let mut values=unpack_values(&arguments).unwrap();
                let PackValue::Reference(r)=&mut values[0] else{panic!()};
                let PackedEntry::Heap{object,..}=&mut r.entries[0] else{panic!()};
                object.pkg_id=1;
                assert_eq!(decode(function,&pack_values(&values)).unwrap()["event"],"error");
                assert_eq!(decode(function,&arguments[..30]).unwrap()["event"],"error");
            }
        }
    }
}
