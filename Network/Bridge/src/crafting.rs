// Native WM_Craft registry, ingredient checks, and execution. No chat-command shim.
use ac2_net::protocol::*;
use serde_json::{json, Value};

pub fn decode(function: u32, arguments: &[u8]) -> Option<Value> {
    let mut result=match CraftMessage::decode(function, arguments)? {
        Ok(CraftMessage::SetRegistry { skills, recipes }) =>
            json!({"event":"craft_registry","replace":true,"skills":skills,"recipes":recipes}),
        Ok(CraftMessage::LearnRecipe { recipe }) =>
            json!({"event":"craft_registry","replace":false,"recipes":[recipe]}),
        Ok(CraftMessage::SetCraftSkills { skills }) =>
            json!({"event":"craft_skills","skills":skills}),
        Ok(CraftMessage::SetCraftSkill { skill }) =>
            json!({"event":"craft_skills","skills":[skill]}),
        Ok(CraftMessage::UpdateRecipe { recipe }) =>
            json!({"event":"craft_recipe_update","recipes":[recipe]}),
        Ok(CraftMessage::UpdateRecipes { recipes }) =>
            json!({"event":"craft_recipe_update","recipes":recipes}),
        Ok(CraftMessage::AddIngredientDone { recipe, ordinal, ingredient, spinner, error }) =>
            json!({"event":"craft_ingredient","recipe":recipe,"ordinal":ordinal,"item":ingredient.to_string(),"spinner":spinner,"error":error}),
        Ok(CraftMessage::ExecuteCraftDone { notify, recipe, error }) =>
            json!({"event":"craft_complete","notify":notify,"recipe":recipe,"error":error}),
        Err(error) => json!({"event":"error","message":format!("Invalid native crafting message: {error}")}),
    };
    enrich(&mut result, arguments);Some(result)
}

pub fn command(v: &Value) -> Result<ClientEvent, String> {
    if super::text(v,"command")?=="craft_raise" {let skill=super::word(v,"skill")?;if skill>>24!=0x6f{return Err("Invalid profession".into())}let request=ScriptRequest::CraftRaiseSkill(skill);return Ok(ClientEvent::Interp{function:request.function(),arguments:pack_values(&request.arguments()),encrypted:false});}
    let spinner=v["spinner"].as_u64().unwrap_or(0);if spinner>1000{return Err("Invalid crafting level".into())}
    let recipe=super::word(v,"recipe")?;
    if recipe>>24!=0x6f {return Err("Invalid recipe identity".into());}
    let request=match super::text(v,"command")? {
        "craft_validate" => {
            let ordinal=super::word(v,"ordinal")?;let object=super::id(v,"item")?;
            if !(1..=16).contains(&ordinal) || [0,u64::MAX].contains(&object) {return Err("Invalid ingredient".into());}
            ScriptRequest::CraftAddIngredient{recipe,ordinal,object,spinner:spinner as u32}
        }
        "craft_execute" => {
            let entries=v["ingredients"].as_array().filter(|e|e.len()<=16).ok_or("Invalid ingredients")?;
            let mut ingredients=Vec::new();
            for (i,value) in entries.iter().enumerate() {
                let ordinal=value.get("ordinal").and_then(Value::as_u64).unwrap_or(i as u64+1);if !(1..=16).contains(&ordinal){return Err("Invalid ingredient ordinal".into())}
                let item=value.as_str().or_else(||value["item"].as_str()).and_then(|s|s.parse::<u64>().ok()).filter(|n|![0,u64::MAX].contains(n)).ok_or("Invalid ingredient identity")?;
                // Multiple ordinals may select the same owned stack. The
                // server validates and reserves their combined quantity.
                if ingredients.iter().any(|(o,_)|*o==ordinal as u32){return Err("Duplicate ingredient ordinal".into())}
                ingredients.push((ordinal as u32,item));
            }
            let helper=if ingredients.is_empty(){super::id(v,"helper")?}else{ingredients[0].1};
            if [0,u64::MAX].contains(&helper){return Err("Invalid crafting helper".into())}
            ScriptRequest::CraftExecute{helper,recipe,spinner:spinner as u32,ingredients}
        }
        _=>return Err("Unknown crafting command".into()),
    };
    Ok(ClientEvent::Interp{function:request.function(),arguments:pack_values(&request.arguments()),encrypted:false})
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn server_registry_accepts_zero_root_and_legacy_skill_records() {
        let message=CraftMessage::SetRegistry{skills:vec![0x6f0028d7],recipes:vec![0x6f001f17]};
        for root_id in [0,42] {
            let mut args=message.arguments();
            let PackValue::Reference(r)=&mut args[0] else {panic!("registry")};
            // The sender owns reference IDs. Rebase all reference-bearing
            // fields, leaving DIDs and other scalar values unchanged.
            let remap=|id:u32| id-1+root_id;
            for id in &mut r.ids {*id=remap(*id);}
            for entry in &mut r.entries {if let PackedEntry::Heap{object,..}=entry {
                if object.pkg_id==0x1d2 {for slot in [5,6]{object.slots[slot]=remap(object.slots[slot]);}}
                if let Some(NativeObject::Hash(h))=&mut object.native {for id in &mut h.values{*id=u64::from(remap(*id as u32));}}
                if object.pkg_id==0x345f {object.pkg_id=0x1b4f;object.slots[5]=3;}
            }}
            let event=decode(message.function(),&pack_values(&args)).unwrap();
            assert_eq!(event["event"],"craft_registry");assert_eq!(event["recipes"],json!([0x6f001f17]));
            assert_eq!(event["skills"],json!([0x6f0028d7]));assert_eq!(event["progress"][0]["level"],3);
            // An arbitrary package is not treated as a skill record.
            let PackValue::Reference(r)=&mut args[0] else {unreachable!()};
            for entry in &mut r.entries {if let PackedEntry::Heap{object,..}=entry {if object.pkg_id==0x1b4f{object.pkg_id=0x1234;}}}
            assert_eq!(decode(message.function(),&pack_values(&args)).unwrap()["event"],"error");
        }
    }
    #[test]
    fn native_registry_replaces_entitlements_and_empty_registry_clears_them() {
        for recipes in [vec![0x6f001f17,0x6f001f18],vec![]] {
            let m=CraftMessage::SetRegistry{skills:vec![0x6f0028d7],recipes:recipes.clone()};
            let j=decode(m.function(),&pack_values(&m.arguments())).unwrap();
            assert_eq!(j["recipes"],json!(recipes));assert_eq!(j["replace"],true);
        }
        assert!(decode(0x82060019,&[0]).unwrap()["message"].as_str().unwrap().contains("Invalid"));
    }
    #[test]
    fn execution_preserves_unsigned_iids_and_rejects_ambiguous_ingredients() {
        let mut v=json!({"command":"craft_execute","recipe":0x6f001f17u32,"ingredients":["9308940437337407489","9308940437337407490"]});
        let ClientEvent::Interp{function,arguments,..}=command(&v).unwrap() else {panic!()};
        let ScriptRequest::CraftExecute{helper,ingredients,..}=ScriptRequest::decode(function,&arguments).unwrap().unwrap() else {panic!()};
        assert_eq!(helper,9308940437337407489);assert_eq!(ingredients,vec![(1,helper),(2,helper+1)]);
        v["ingredients"]=json!([helper.to_string(),helper.to_string()]);assert!(command(&v).is_ok());
        v["ingredients"]=json!([helper]);assert!(command(&v).is_err());
    }
    #[test]
    fn shared_stack_ordinals_do_not_depend_on_revision_specific_recipe_ids() {
        let iid="9308940437337407489";
        let mut v=json!({"command":"craft_execute","recipe":0x6f001fe4u32,
            "ingredients":(1..=7).map(|i|json!({"ordinal":i,"item":iid})).collect::<Vec<_>>()});
        let ClientEvent::Interp{function,arguments,..}=command(&v).unwrap() else {panic!()};
        let ScriptRequest::CraftExecute{ingredients,..}=ScriptRequest::decode(function,&arguments).unwrap().unwrap() else {panic!()};
        assert_eq!(ingredients,(1..=7).map(|i|(i,iid.parse::<u64>().unwrap())).collect::<Vec<_>>());
        v["ingredients"][1]["ordinal"]=json!(1);assert!(command(&v).is_err());
        v["ingredients"][1]["ordinal"]=json!(2);v["recipe"]=json!(0x6f001f17u32);assert!(command(&v).is_ok());
    }
}

fn enrich(event:&mut Value,arguments:&[u8]){
    use ac2_net::protocol::{unpack_values,PackValue,PackedEntry};
    static DATA:std::sync::OnceLock<Value>=std::sync::OnceLock::new();
    let data=DATA.get_or_init(||serde_json::from_str(include_str!("../../../AC2E/emulator/content/professions-v1.json")).expect("profession catalog"));
    if !matches!(event["event"].as_str(),Some("craft_registry"|"craft_skills")){return}
    if let Ok(values)=unpack_values(arguments){
        let mut progress=Vec::new();
        for v in values {if let PackValue::Reference(r)=v{for entry in r.entries {if let PackedEntry::Heap{object,..}=entry{if matches!(object.pkg_id,0x345f|0x1b4f)&&object.slots.len()==6 && object.slots[2]>>24==0x6f{
            let s=&object.slots;progress.push(json!({"id":s[2],"level":s[5],"earned":u64::from(s[0])|(u64::from(s[1])<<32),"available":u64::from(s[3])|(u64::from(s[4])<<32)}));
        }}}}}
        event["progress"]=json!(progress);
    }
    if event["event"]=="craft_registry" {
        let ids:std::collections::BTreeSet<u64>=event["recipes"].as_array().into_iter().flatten().filter_map(Value::as_u64).collect();
        event["catalog"]=json!(data["recipes"].as_array().unwrap().iter().filter(|r|ids.contains(&r["id"].as_u64().unwrap())).collect::<Vec<_>>());
        event["professions"]=data["skills"].clone();
        event["success"]=data["success"].clone();
    }
}
