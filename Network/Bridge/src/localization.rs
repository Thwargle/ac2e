// SPDX-License-Identifier: AGPL-3.0-only
//! Render server StringInfo with the selected DAT's native strings, number
//! formats and grammar. Concatenating text fragments loses numeric variables
//! (including item values), nested names, plurals and dialogue substitutions.
use ac2_db::{RenderInfo,RenderVar,ResourceStore,StoreStringTables};
use serde_json::Value;

pub(crate) struct Localization(Option<StoreStringTables>);
impl Localization {
    pub fn load(directory:&str)->Self {
        let tables=ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::for_retail_install(directory))
            .ok().map(|data|StoreStringTables::new(ResourceStore::new(data),0x58000000));
        Self(tables)
    }
    pub fn event(&self,mut value:Value)->Value {
        if let Some(tables)=&self.0 {visit(&mut value,tables,0);}
        value
    }
}
fn word(v:&Value,key:&str)->u32 {v[key].as_u64().unwrap_or(0) as u32}
fn long(v:&Value)->u64 {v["value"].as_str().and_then(|s|s.parse().ok()).unwrap_or(0)}
fn info(v:&Value,depth:usize)->RenderInfo {
    if depth>32{return RenderInfo::default()}
    RenderInfo {
        string_id:word(v,"key"),table_id:word(v,"table"),
        literal:v["literal"].as_str().map(|s|s.encode_utf16().collect()),
        variables:v["variables"].as_array().into_iter().flatten().map(|var|{
            let value=match word(var,"kind") {
                2=>RenderVar::Int(word(var,"value") as i32),
                3=>RenderVar::FormattedInt{value:word(var,"value") as i32,grouping:word(var,"grouping") as i32,base:word(var,"base") as u16},
                4=>RenderVar::Float{value:f64::from_bits(long(var)),precision:word(var,"precision") as u16},
                5=>RenderVar::FormattedFloat{value:f64::from_bits(long(var)),precision:word(var,"precision") as u16,grouping:word(var,"grouping") as i32,base:word(var,"base") as u16},
                6=>RenderVar::UInt(word(var,"value")),
                7=>RenderVar::FormattedUInt{value:word(var,"value"),grouping:word(var,"grouping") as i32,base:word(var,"base") as u16},
                9=>RenderVar::Info(Box::new(info(&var["text"],depth+1))),
                10=>RenderVar::LInt(long(var) as i64),
                11=>RenderVar::FormattedLInt{value:long(var) as i64,grouping:word(var,"grouping") as i32,base:word(var,"base") as u16},
                12=>RenderVar::ULInt(long(var)),
                13=>RenderVar::FormattedULInt{value:long(var),grouping:word(var,"grouping") as i32,base:word(var,"base") as u16},
                _=>RenderVar::Skipped,
            };(word(var,"key"),value)
        }).collect(),
    }
}
fn visit(v:&mut Value,tables:&StoreStringTables,depth:usize){
    if depth>40{return}
    if v.get("table").is_some() && v.get("key").is_some() && v.get("variables").is_some(){
        // Failed/missing tables retain their original data for the UI fallback;
        // never replace a valid message with a native diagnostic string.
        if let Ok(text)=ac2_db::inq_string(tables,&info(v,0),false){
            v["resolved"]=Value::String(String::from_utf16_lossy(&text));
        }
    }else{match v{
        Value::Object(fields)=>for child in fields.values_mut(){visit(child,tables,depth+1)},
        Value::Array(items)=>for child in items {visit(child,tables,depth+1)},
        _=>(),
    }}
}

#[cfg(test)]mod tests{
    use super::*;
    use ac2_net::protocol::{StringInfo,StringVariable};
    #[test]fn native_variables_preserve_signed_unsigned_float_and_nested_data(){
        let s=StringInfo{variables:vec![
            (1,StringVariable::Word{kind:2,value:(-7i32) as u32}),
            (2,StringVariable::Long{kind:12,value:u64::MAX}),
            (3,StringVariable::LongShort{value:12.25f64.to_bits(),c:2}),
            (4,StringVariable::Nested(Box::new(StringInfo{literal:Some("Drudge".into()),..Default::default()}))),
        ],..Default::default()};
        let result=info(&crate::events::string(&s),0);
        assert_eq!(result.variables,vec![(1,RenderVar::Int(-7)),(2,RenderVar::ULInt(u64::MAX)),
            (3,RenderVar::Float{value:12.25,precision:2}),
            (4,RenderVar::Info(Box::new(RenderInfo{literal:Some("Drudge".encode_utf16().collect()),..Default::default()})))]);
    }
    #[test]
    #[ignore="Requires AC2_LOCALIZATION_DAT pointing to a stock or Icefire installation"]
    fn selected_dat_renders_chat_and_numeric_examination_variables(){
        use ac2_db::StringTables;
        let renderer=Localization::load(&std::env::var("AC2_LOCALIZATION_DAT").unwrap());
        let tables=renderer.0.as_ref().expect("Load selected DAT string tables");
        let mut checked=false;let mut speech_checked=false;
        for did in tables.store().dataset().ids_in_range(ac2_dat::DatFileKind::Local,ac2_db::STRING_TABLE_FIRST,ac2_db::STRING_TABLE_LAST) {
            let Some(table)=tables.table(did) else {continue};
            if table.get(0xd85e9379).is_some(){
                let speech=StringInfo {table_id:did,string_id:0xd85e9379,variables:vec![
                    (0x9b4e3800,StringVariable::Nested(Box::new(StringInfo{literal:Some("Hello, Dereth!".into()),..Default::default()})))
                ],..Default::default()};
                let out=renderer.event(crate::events::string(&speech));
                assert_eq!(out["resolved"].as_str().map(str::trim_end),Some("You say, \"Hello, Dereth!\""));speech_checked=true;
            }
            for (key,entry) in &table.strings {
                if !entry.text().contains("coin") || entry.variables.len()!=1{continue}
                let value=StringInfo {table_id:did,string_id:*key,variables:entry.variables.iter()
                    .map(|id|(*id,StringVariable::Word{kind:6,value:27})).collect(),..Default::default()};
                let out=renderer.event(crate::events::string(&value));
                let rendered=out["resolved"].as_str().unwrap_or_else(||panic!("Resolve actual value template {did:08x}/{key:08x}: {}",entry.text()));
                assert!(rendered.contains("27"),"{rendered}");checked=true;
            }
        }
        assert!(checked,"No native numeric item-value template tested");
        assert!(speech_checked,"No native speech template tested");
    }
}
