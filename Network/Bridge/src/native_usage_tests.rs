//! Optional independent DAT oracle, rather than a codec round trip alone.
use super::*;

fn oracle(variable: &str) -> Value {
    let path=std::env::var_os(variable).unwrap_or_else(||panic!("Set {variable} to the generated JSON file"));
    let bytes=std::fs::read(path).expect("Read independent DAT oracle");
    serde_json::from_slice(bytes.strip_prefix(&[0xef,0xbb,0xbf]).unwrap_or(&bytes)).expect("Parse DAT oracle")
}

fn assert_reference(mut actual: PackedReference, native: PackedReference) {
    // Reference indices are allocated by each VM process, not wire identities.
    assert_eq!(actual.ids.len(),native.ids.len());
    for entry in &mut actual.entries {
        if let PackedEntry::Heap{object,..}=entry {
            for (slot,tag) in object.slots.iter_mut().zip(&object.tags) {
                if *tag==1 && *slot!=u32::MAX {
                    let index=actual.ids.iter().position(|id|id==slot).expect("Local reference");
                    *slot=native.ids[index];
                }
            }
        }
    }
    actual.ids=native.ids.clone();
    assert_eq!(actual,native,"Outbound reference must match native DAT execution");
}

fn assert_call(actual: ClientEvent, native: &Value) {
    let ClientEvent::Interp{function,arguments,encrypted}=actual else {panic!("Script event")};
    assert_eq!(function,native["function"].as_u64().unwrap() as u32);
    assert_eq!(encrypted,native["encrypted"].as_bool().unwrap());
    let bytes:Vec<u8>=serde_json::from_value(native["arguments"].clone()).unwrap();
    let actual=unpack_values(&arguments).unwrap();
    let native=unpack_values(&bytes).unwrap();
    assert_eq!(actual.len(),native.len());
    for (a,n) in actual.into_iter().zip(native) {
        match (a,n) {
            (PackValue::Reference(a),PackValue::Reference(n))=>assert_reference(a,n),
            (a,n)=>assert_eq!(a,n),
        }
    }
}

fn check_world_move(variable: &str,command: &str) {
    let oracle=oracle(variable);
    let bytes:Vec<u8>=serde_json::from_value(oracle["reference"].clone()).unwrap();
    let request=json!({"command":command,"id":oracle["item"],"target":oracle["target"],
        "from":oracle["from_slot"],"quantity":oracle["quantity"]});
    let ClientEvent::Interp{function,arguments,encrypted}=world_item_request(&request,id(&oracle,"from").unwrap(),true).unwrap()
        else{panic!("MoveItem event")};
    assert_eq!(function,0x820e0036);assert!(!encrypted);
    let PackValue::Reference(actual)=unpack_values(&arguments).unwrap().remove(0) else{panic!("Move descriptor")};
    assert_reference(actual,PackedReference::from_bytes(&bytes).unwrap());
}

#[test]
#[ignore = "Requires AC2_DROP_ORACLE from RetailProbe --move-desc drop"]
fn native_ground_drop_matches_dat_constructor_and_direction() {check_world_move("AC2_DROP_ORACLE","drop_item");}

#[test]
#[ignore = "Requires AC2_GIVE_ORACLE from RetailProbe --move-desc give"]
fn native_give_matches_dat_constructor_and_direction() {check_world_move("AC2_GIVE_ORACLE","give_item");}

#[test]
fn world_transfers_keep_ac2e_drop_profile_and_reject_invalid_gifts() {
    let player=0x2000000000000456u64;
    let item=0x8000000000000123u64;
    let mut request=json!({"command":"drop_item","id":item.to_string(),"from":7,"quantity":3,"landblock":0x12340000});
    let ClientEvent::Interp{function,arguments,..}=world_item_request(&request,player,false).unwrap() else{panic!()};
    assert_eq!(function,0x820e0036);
    let ClientEvent::Interp{arguments:old,..}=reorganize_item(InvMoveDesc::new(item,player,7,0x12340000,-1,3)) else{panic!()};
    assert_eq!(arguments,old,"AC2E drop packet stays compatible");
    request["command"]=json!("give_item");
    for target in [0,player,item,u64::MAX] {
        request["target"]=json!(target.to_string());assert!(world_item_request(&request,player,true).is_err());
    }
    request["target"]=json!("9223372036854777737");
    assert!(world_item_request(&request,player,false).is_err(),"Unsupported give must never become a drop");
    assert!(world_item_request(&request,player,true).is_ok());
    for quantity in [0,u32::MAX] {request["quantity"]=json!(quantity);assert!(world_item_request(&request,player,true).is_err());}
    request["quantity"]=json!(1);request["from"]=json!(u32::MAX);assert!(world_item_request(&request,player,true).is_err());
}

#[test]
#[ignore = "Requires AC2_MOVE_ORACLE from RetailProbe --move-desc"]
fn native_loot_transfer_matches_installed_dat_constructor() {
    let oracle=oracle("AC2_MOVE_ORACLE");
    let bytes:Vec<u8>=serde_json::from_value(oracle["reference"].clone()).unwrap();
    let native=PackedReference::from_bytes(&bytes).unwrap();
    let ClientEvent::Interp{function,arguments,encrypted}=transfer_item(InvMoveDesc::new(
        id(&oracle,"item").unwrap(),id(&oracle,"from").unwrap(),0,id(&oracle,"target").unwrap(),-1,1))
        else {panic!("move event")};
    assert_eq!(function,0x820e0036);assert!(!encrypted);
    let PackValue::Reference(actual)=unpack_values(&arguments).unwrap().remove(0) else {panic!("move descriptor")};
    assert_reference(actual,native);
}

#[test]
#[ignore = "Requires AC2_BULK_ORACLE from RetailProbe --bulk-desc and AC2_BULK_DAT directory"]
fn native_bulk_requests_match_selected_dat_constructors() {
    let oracle=oracle("AC2_BULK_ORACLE");
    let profile=inventory_bulk::Profile::load(&std::env::var("AC2_BULK_DAT").unwrap()).unwrap();
    for (i,transmute) in [false,true].into_iter().enumerate() {
        let bytes:Vec<u8>=serde_json::from_value(oracle["cases"][i]["reference"].clone()).unwrap();
        let native=PackedReference::from_bytes(&bytes).unwrap();
        let ClientEvent::Interp{function,arguments,encrypted}=profile.request(id(&oracle,"container").unwrap(),id(&oracle,"player").unwrap(),transmute) else{panic!()};
        assert_eq!(function,if transmute{0x820e006e}else{0x820e0069});assert!(!encrypted);
        let PackValue::Reference(actual)=unpack_values(&arguments).unwrap().remove(0) else{panic!()};
        assert_reference(actual,native);
    }
}

#[test]
#[ignore = "Requires AC2_COMBAT_ORACLE from RetailProbe --use-selection DID --combat --selected"]
fn native_stance_uses_the_current_selection() {
    let oracle=oracle("AC2_COMBAT_ORACLE");
    let mut selection=selection::Selection::new(true);
    selection.requests(0x8000000000000123);
    for (i,enabled) in [true,false].into_iter().enumerate() {
        assert_call(combat_stance(enabled,selection.combat_target()),&oracle[i]["calls"][0]);
    }
}

#[test]
#[ignore = "Requires AC2_SELECTION_ORACLE from RetailProbe --use-selection"]
fn icefire_selection_and_use_match_installed_dat_path() {
    let oracle=oracle("AC2_SELECTION_ORACLE");
    assert_eq!(oracle["completed"],true);
    assert!(oracle["dropped"].as_array().unwrap().is_empty());
    let item=id(&oracle,"item").unwrap();
    let user=id(&oracle,"user").unwrap();
    let mut state=selection::Selection::new(true);
    for (key,id) in [("select_calls",item),("clear_calls",0)] {
        let expected=oracle[key].as_array().unwrap();
        assert_eq!(expected.len(),3);
        let calls=state.requests(id);
        assert_eq!(calls.len(),2);
        for (call,native) in calls.into_iter().zip(expected) {assert_call(call,native);}
        // Examination remains an explicit UI request; its packet is also
        // checked against what native selection generates for this object.
        if id!=0 {assert_call(examination::request(id,Build::Legions7160),&expected[2]);}
        assert!(state.requests(id).is_empty());
    }
    assert!(oracle["repeat_calls"].as_array().unwrap().is_empty());
    let expected=oracle["use_calls"].as_array().unwrap();
    assert_eq!(expected.len(),1);
    assert_call(use_item(user,item,0),&expected[0]);
}

#[test]
#[ignore = "Requires AC2_USAGE_ORACLE from RetailProbe --usage (see Tools/RetailProbe/README.md)"]
fn native_use_matches_installed_dat_constructor() {
    let oracle=oracle("AC2_USAGE_ORACLE");
    let bytes:Vec<u8>=serde_json::from_value(oracle["reference"].clone()).expect("Packed DAT reference");
    let native=PackedReference::from_bytes(&bytes).expect("Decode DAT constructor output");
    let ClientEvent::Interp{function,arguments,encrypted}=use_item(
        id(&oracle,"user").unwrap(),id(&oracle,"item").unwrap(),id(&oracle,"target").unwrap()
    ) else {panic!("Native use request")};
    assert_eq!(function,0x82160016);assert!(!encrypted);
    let arguments=unpack_values(&arguments).unwrap();
    assert_eq!(arguments.len(),1);
    let PackValue::Reference(actual)=&arguments[0] else {panic!("Usage reference")};
    assert_reference(actual.clone(),native);
}
