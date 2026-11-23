//! Resolve the character's crafting registry against its own DAT. DIDs and
//! package IDs are local to a content revision, never a cross-server catalog.
use ac2_net::protocol::*;
use serde_json::{json,Value};
use std::{collections::BTreeMap,sync::Arc};

pub(crate) struct Catalog {
    data:Option<Arc<ac2_dat::DatDataset>>,
    strings:crate::localization::Localization,
    graphs:BTreeMap<u32,Option<PackedReference>>,
    skill_class:u32,
    table_classes:BTreeMap<String,u32>,
    success:Option<Vec<f32>>,
    level_thresholds:Option<Vec<u64>>,
}
fn heap(g:&PackedReference,id:u32)->Option<&HeapObject>{
    let i=g.ids.iter().position(|v|*v==id)?;
    match g.entries.get(i)? {PackedEntry::Heap{object,..}=>Some(object),_=>None}
}
fn root(g:&PackedReference)->Option<&HeapObject>{heap(g,*g.ids.first()?)}
fn list(g:&PackedReference,id:u32)->Option<Vec<u64>>{
    match heap(g,id)?.native.as_ref()? {NativeObject::List(l)=>Some(l.items.clone()),_=>None}
}
fn pairs(g:&PackedReference,id:u32)->Option<Vec<(u32,u32)>>{
    let NativeObject::Hash(h)=heap(g,id)?.native.as_ref()? else{return None};
    h.keys.iter().zip(&h.values).map(|(k,v)|match k{HashKey::Value(k)=>Some((*k as u32,*v as u32)),_=>None}).collect()
}
fn did(g:&PackedReference,id:u32)->Option<u32>{
    match g.entries.get(g.ids.iter().position(|v|*v==id)?)? {PackedEntry::Singleton{did,..}=>Some(*did),_=>None}
}
impl Catalog {
    pub fn load(directory:&str)->Self{
        let data=ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::for_retail_install(directory)).ok();
        let table_classes:BTreeMap<String,u32>=data.as_ref().and_then(|d|{
            let store=ac2_db::ResourceStore::new(d.clone());
            let lib=ac2_script::load_wlib(&store,ac2_core::DataId::new(ac2_script::LEGIONS_WLIB_DID)).ok()??;
            Some(lib.stream.vtables.package_names.iter().map(|(n,pid)|(String::from_utf8_lossy(n).into_owned(),*pid)).collect())
        }).unwrap_or_default();
        let skill_class=table_classes.get("CraftSkill").copied().unwrap_or(u32::MAX);
        Self{data,strings:crate::localization::Localization::load(directory),graphs:BTreeMap::new(),skill_class,table_classes,success:None,level_thresholds:None}
    }
    fn graph(&mut self,id:u32)->Option<PackedReference>{
        if !self.graphs.contains_key(&id){
            let g=self.data.as_ref().and_then(|d|d.read_packed(ac2_dat::DatFileKind::Portal,id).ok().flatten())
                .and_then(|b|ac2_script::WState::unpack(&b).ok())
                .and_then(|s|PackedReference::from_bytes(&s.package_state).ok());
            self.graphs.insert(id,g);
        }
        self.graphs.get(&id)?.clone()
    }
    fn text(&self,g:&PackedReference,id:u32)->String{
        match heap(g,id).and_then(|h|h.native.as_ref()){
            Some(NativeObject::WString(s))=>s.clone(),
            Some(NativeObject::StringInfo(s))=>{
                let rendered=self.strings.event(crate::events::string(s));
                rendered["resolved"].as_str().or(rendered["literal"].as_str()).unwrap_or("").into()
            },_=>String::new()
        }
    }
    fn curve(&mut self,id:u32)->Option<Vec<u64>>{
        let g=self.graph(id)?;let h=root(&g)?;let f=&h.slots;
        // Native advancement and level mapping tables include inherited slots.
        if self.table_classes.get("AdvancementTable")==Some(&h.pkg_id) && !f.is_empty(){return list(&g,f[0])}
        if self.table_classes.get("LevelMappingTable")==Some(&h.pkg_id) && f.len()>=3 && f[0]<=10000 {
            let mut values=vec![0;f[0] as usize];values.extend(list(&g,f[2])?);return Some(values)
        }None
    }
    fn level_xp(&mut self,level:usize)->Option<(u64,u64)>{
        if self.level_thresholds.is_none() {
            let class=*self.table_classes.get("CharacterLevelTable")?;
            let g=self.graph(0x70000000 | class)?;
            let table=root(&g)?;
            let maximum=*table.slots.get(1)? as usize;
            if table.pkg_id!=class {return None}
            let rows=list(&g,*table.slots.first()?)?;
            let thresholds=rows.get(1..=maximum)?.iter().map(|row|{
                let fields=&heap(&g,(*row).try_into().ok()?)?.slots;
                Some((u64::from(*fields.get(1)?)<<32) | u64::from(*fields.first()?))
            }).collect::<Option<Vec<_>>>()?;
            self.level_thresholds=Some(thresholds);
        }
        let thresholds=self.level_thresholds.as_ref()?;
        let floor=*thresholds.get(level.checked_sub(1)?)?;
        // At the final hero level TrainingSystem uses the current threshold as the span.
        let span=match thresholds.get(level) {Some(&next)=>next.checked_sub(floor)?,None=>floor};
        Some((floor,span))
    }
    fn values(&mut self,g:&PackedReference,id:u32,constant:u32)->Option<Vec<u64>>{
        if id==u32::MAX {Some(vec![constant as u64])}else{self.curve(did(g,id)?)}
    }
    fn skill(&mut self,id:u32)->Option<Value>{
        let g=self.graph(id)?;let h=root(&g)?;let f=&h.slots;
        if h.pkg_id!=self.skill_class || f.len()!=12{return None}
        Some(json!({"id":id,"name":self.text(&g,f[2]),"description":self.text(&g,f[4]),
            "cap":f[1],"icon":f[6],"xp":self.curve(f[5])?,"limit":self.curve(f[7])?}))
    }
    fn success_table(&mut self)->Vec<f32>{
        if let Some(values)=&self.success{return values.clone()}
        let ids:Vec<_>=self.data.as_ref().map(|d|d.ids_in_range(ac2_dat::DatFileKind::Portal,0x6f000000,0x6fffffff).collect()).unwrap_or_default();
        let mut found=Vec::new();
        for id in ids {
            let Some(g)=self.graph(id) else{continue};let Some(h)=root(&g) else{continue};
            if self.table_classes.get("RecipeDifficultyTable")!=Some(&h.pkg_id) || h.slots.len()!=3 || h.slots[1]!=500 || h.slots[2] as i32 != -500 {continue}
            if let Some(values)=list(&g,h.slots[0]){
                let values:Vec<_>=values.iter().map(|v|f32::from_bits(*v as u32)).collect();
                if values.len()==1001 && values.iter().all(|v|v.is_finite() && (0.0..=100.0).contains(v)){found=values;break}
            }
        }
        self.success=Some(found.clone());found
    }
    fn outputs(&self,g:&PackedReference,reference:u32)->Vec<Value>{
        let resolve=||->Option<Vec<Value>>{
            let branch=pairs(g,reference)?.into_iter().find(|(k,_)|*k==0x40000000)?.1;
            let outcome=heap(g,branch)?;let mut result=Vec::new();
            for (bits,reference) in pairs(g,*outcome.slots.get(2)?)?{
                let group=heap(g,reference)?;
                for action in list(g,*group.slots.first()?)?{
                    let action=heap(g,action as u32)?;
                    if self.table_classes.get("ProduceRecipeAction")!=Some(&action.pkg_id){continue}
                    let item=*action.slots.get(2)?;let threshold=f32::from_bits(bits);
                    if item>>24==0x47 && threshold.is_finite(){result.push(json!({"threshold":threshold,"item":item}));}
                }
            }Some(result)
        };
        resolve().unwrap_or_default()
    }
    fn recipe(&mut self,id:u32)->Option<Value>{
        let g=self.graph(id)?;let h=root(&g)?;let f=&h.slots;
        if f.len()<37 || f[11]>>24!=0x6f || self.skill(f[11]).is_none(){return None}
        let mut ingredients=Vec::new();
        for reference in list(&g,f[7])?{
            let h=heap(&g,reference as u32)?;let v=&h.slots;
            if v.len()<19 || !(1..=16).contains(&v[0]){return None}
            let choices:Vec<_>=pairs(&g,v[6]).unwrap_or_default().into_iter().filter(|(k,v)|k==v && k>>24==0x47).map(|(k,_)|k).collect();
            ingredients.push(json!({"ordinal":v[0],"name":self.text(&g,v[1]),"choices":choices,
                "minimum":v[4],"quantities":self.values(&g,v[18],v[12])?}));
        }
        // Abstract predicates (dye targets, tools, trophies) are checked by the
        // server. An empty choices list means manual selection, not no recipe.
        let outputs=self.outputs(&g,f[5]);
        let kind=if self.table_classes.get("DyeingRecipe")==Some(&h.pkg_id){"dye"}
            else if self.table_classes.iter().any(|(name,pid)|*pid==h.pkg_id && name.ends_with("RefiningRecipe")){"refine"}
            else if !outputs.is_empty(){"produce"}else{"native"};
        Some(json!({"id":id,"skill":f[11],"name":self.text(&g,f[8]),"description":self.text(&g,f[9]),
            "minimum":f[13],"maximum":f[21],"icon":f[4],"kind":kind,"workshop":did(&g,f[31]).unwrap_or(0),
            "difficulty":self.values(&g,f[3],f[29])?,"cost":self.values(&g,f[1],f[34])?,
            "ingredients":ingredients,"outputs":outputs}))
    }
    pub fn event(&mut self,mut event:Value)->Value{
        if event["event"]=="quality" && event["table"]==4 && event["key"]==0x12e {
            if let Some((floor,span))=event["value"].as_u64().and_then(|v|self.level_xp(v.try_into().ok()?)) {
                event["level_xp_floor"]=json!(floor.to_string());
                event["level_xp_span"]=json!(span.to_string());
            }
            return event;
        }
        if event["event"]=="status_effects" {
            if let Some(rows)=event.get_mut("effects").and_then(Value::as_array_mut) {
                for row in rows {
                    let Some(g)=row["did"].as_u64().and_then(|did| self.graph(did as u32)) else {continue};
                    let Some(fields)=root(&g).map(|h| &h.slots) else {continue};
                    if fields.len()<=29 {continue}
                    row["name"]=json!(self.text(&g,fields[29]));
                    row["description"]=json!(self.text(&g,fields[11]));
                    row["icon"]=json!(fields[19]);
                    row["flags"]=json!(row["flags"].as_u64().unwrap_or(0) | u64::from(fields[7]&0x800));
                }
            }
            return event;
        }
        if !matches!(event["event"].as_str(),Some("craft_registry"|"craft_skills")) {return event}
        let ids:Vec<_>=event["recipes"].as_array().into_iter().flatten().filter_map(Value::as_u64).map(|v|v as u32).collect();
        let mut recipes=Vec::new();let mut skills=BTreeMap::new();let mut missing=Vec::new();
        for id in ids {if let Some(r)=self.recipe(id){
            let skill=r["skill"].as_u64().unwrap() as u32;
            if let Some(s)=self.skill(skill){skills.insert(skill,s);}
            recipes.push(r);
        }else{missing.push(id);}}
        for id in event["skills"].as_array().into_iter().flatten().filter_map(Value::as_u64){if let Some(s)=self.skill(id as u32){skills.insert(id as u32,s);}}
        for p in event["progress"].as_array().into_iter().flatten(){if let Some(id)=p["id"].as_u64(){if let Some(s)=self.skill(id as u32){skills.insert(id as u32,s);}}}
        event["catalog"]=json!(recipes);event["professions"]=json!(skills.into_values().collect::<Vec<_>>());
        event["unresolved"]=json!(missing);if event["event"]=="craft_registry"{event["success"]=json!(self.success_table());}
        event
    }
}
#[cfg(test)]mod tests{
    use super::*;
    #[test]
    #[ignore="Requires AC2_CRAFT_DAT, stock or Icefire"]
    fn vitals_experience_resolves_level_boundaries_from_selected_dat(){
        let mut c=Catalog::load(&std::env::var("AC2_CRAFT_DAT").unwrap());
        let level=|value:u32|json!({"event":"quality","table":4,"key":0x12e,"value":value});
        assert_eq!(c.level_xp(1),Some((0,40)));
        assert_eq!(c.level_xp(2),Some((40,56)));
        let high=c.event(level(100));
        assert_eq!(high["level_xp_floor"],"2978018150324");
        assert_eq!(high["level_xp_span"],"258511070776");
        assert_eq!(c.level_xp(150),Some((146234963531265,146234963531265)));
        assert_eq!(c.event(level(151)),level(151));
    }
    #[test]
    #[ignore="Requires AC2_CRAFT_DAT, stock or Icefire"]
    fn status_effects_use_their_selected_dat_and_preserve_instance_flags() {
        let mut c=Catalog::load(&std::env::var("AC2_CRAFT_DAT").unwrap());
        let (haste,hidden)=match c.skill_class {
            0x33af=>(0x6f00064bu32,0x6f0003b1u32),
            0x1b4e=>(0x6f002642u32,0x6f000017u32),
            pkg=>panic!("Unrecognized test DAT craft skill package {pkg:x}"),
        };
        let result=c.event(json!({"event":"status_effects","effects":[
            {"did":haste,"flags":0x200},{"did":haste,"flags":0x800},{"did":hidden,"flags":0x400},
            {"did":0x6fffffffu32,"flags":0},
        ]}));
        let rows=result["effects"].as_array().unwrap();
        assert_eq!(rows[0]["name"],"Haste");
        assert_eq!(rows[0]["description"],"Combat speed enhanced.");
        assert_eq!(rows[0]["icon"],0x41001366u32);
        assert_eq!(rows[0]["flags"],0x200);
        assert_eq!(rows[1]["flags"],0x800);
        assert_eq!(rows[2]["flags"],0xc00);
        assert_eq!(rows[3],json!({"did":0x6fffffffu32,"flags":0}));
    }
    #[test]#[ignore="Requires AC2_CRAFT_DAT, stock or Icefire"]
    fn recipes_use_selected_dat_not_foreign_ids(){
        let path=std::env::var("AC2_CRAFT_DAT").unwrap();let mut c=Catalog::load(&path);
        let ids:Vec<_>=c.data.as_ref().unwrap().ids_in_range(ac2_dat::DatFileKind::Portal,0x6f000000,0x6fffffff).collect();
        let mut count=0;let mut sample=0;
        for id in ids {if let Some(g)=c.graph(id){if root(&g).map(|r|r.pkg_id)==c.table_classes.get("Recipe").copied(){
            if let Some(r)=c.recipe(id){assert!(!r["name"].as_str().unwrap().is_empty());assert!(!r["cost"].as_array().unwrap().is_empty());count+=1;sample=id;if count>=20{break}}
        }}}
        assert!(count>=20,"Known recipes must resolve in selected data");
        let e=c.event(json!({"event":"craft_registry","recipes":[sample],"skills":[]}));
        assert_eq!(e["catalog"].as_array().unwrap().len(),1);assert_eq!(e["professions"].as_array().unwrap().len(),1);
        assert!(c.recipe(0x6f000000).is_none());
        assert_eq!(e["success"].as_array().unwrap().len(),1001);
        // Both WSLs pass Recipe member 4 to gRecipeAdaptor::SetIconDID.
        // Member 32 also points to a texture but it is unrelated to the UI.
        if c.skill_class==0x33af {
            assert_eq!(c.recipe(0x6f00524f).unwrap()["icon"],0); // Brigandine Gauntlets inherit Armor: Empyrean.
            assert_eq!(c.recipe(0x6f002a3f).unwrap()["icon"],0x41004a20u32); // Basic Gear Knight Armor Plate.
            let known:Value=serde_json::from_str(include_str!("../../../AC2E/emulator/content/professions-v1.json")).unwrap();
            for expected in known["recipes"].as_array().unwrap(){
                let id=expected["id"].as_u64().unwrap() as u32;let got=c.recipe(id).expect("AC2E granted recipe must decode");
                for field in ["name","skill","icon","minimum","maximum","cost","difficulty"]{assert_eq!(got[field],expected[field],"{id:x} {field}");}
            }
            let potion=c.recipe(0x6f001f17).unwrap();assert_eq!(potion["outputs"][0]["item"],1191188293u32);
            assert_eq!(e["success"][500],80.0);
        }else if c.skill_class==0x1b4e{
            assert_eq!(c.recipe(0x6f000282).unwrap()["icon"],0x410049a6u32); // Icefire Greaves of the Ancient Kings.
        }
        if let Ok(output)=std::env::var("AC2_CRAFT_FIXTURE") {std::fs::write(output,serde_json::to_vec_pretty(&e).unwrap()).unwrap();}
    }
}





