//! Durable, consent-based patron links. No inferred allegiance XP is awarded.
use std::{collections::{BTreeMap,BTreeSet},fs::{File,OpenOptions},io::{self,Read,Write},path::Path};
use ac2_net::protocol::*;
use crate::{script,store::Character};

pub const SWEAR:u32=0x8203000a;
pub const BREAK:u32=0x82030009;
pub const ACCEPT:u32=0x8203001c;
pub const DECLINE:u32=0x82030029;
#[derive(Clone,Copy,Debug,PartialEq,Eq)]
pub enum Action { Swear(u64),Break(u64),Accept(u64),Decline(u64) }
pub fn decode(function:u32,bytes:&[u8])->Option<Result<Action,crate::world::WorldError>> {
    if ![SWEAR,BREAK,ACCEPT,DECLINE].contains(&function){return None}
    let bad=crate::world::WorldError::InvalidValue;
    // These RPCs contain only one ID (and a decline result). Reject arbitrary
    // recursive values before asking the general native-value decoder to run.
    if bytes.len()!=if function==DECLINE{20}else{12}{return Some(Err(bad))}
    let values=match unpack_values(bytes){Ok(v)=>v,Err(_)=>return Some(Err(bad))};
    let id=match values.as_slice(){[PackValue::LongInt(id)] if function!=DECLINE=>*id,
        [PackValue::Int(0x1585),PackValue::LongInt(id)] if function==DECLINE=>*id,_=>return Some(Err(bad))};
    Some(if id==0||id==u64::MAX {Err(bad)} else {Ok(match function{SWEAR=>Action::Swear(id),BREAK=>Action::Break(id),ACCEPT=>Action::Accept(id),_=>Action::Decline(id)})})
}
fn error(s:&str)->io::Error{io::Error::other(s)}
fn valid_link(links:&BTreeMap<u64,u64>,child:u64,parent:u64)->bool {
    if child==0||parent==0||child==u64::MAX||parent==u64::MAX||child==parent||links.contains_key(&child){return false}
    let mut p=parent;let mut seen=BTreeSet::new();
    while seen.insert(p){if p==child{return false}match links.get(&p){Some(next)=>p=*next,None=>return true}}
    false
}
pub struct Memberships {links:BTreeMap<u64,u64>,pending:BTreeMap<u64,(u64,u64)>,file:File,poisoned:bool,records:usize}
impl Memberships {
    pub fn open(path:&Path)->io::Result<Self>{
        let mut file=OpenOptions::new().read(true).append(true).create(true).open(path)?;
        if file.metadata()?.len()>2*1024*1024{return Err(error("Allegiance journal exceeds size limit"))}
        let mut text=String::new();file.read_to_string(&mut text)?;
        if !text.is_empty()&&!text.ends_with('\n'){return Err(error("Incomplete allegiance journal record"))}
        let mut links=BTreeMap::new();let mut records=0;
        for line in text.lines(){
            let (version,child,parent):(u32,u64,u64)=serde_json::from_str(line).map_err(io::Error::other)?;
            if version!=1||child==0||child==u64::MAX{return Err(error("Invalid allegiance journal"))}
            if parent==0 {if links.remove(&child).is_none(){return Err(error("Unmatched allegiance removal"))}}
            else {if !valid_link(&links,child,parent){return Err(error("Invalid allegiance hierarchy"))}links.insert(child,parent);}
            records+=1;
            if records>20000||links.len()>4096{return Err(error("Allegiance journal limits exceeded"))}
        }
        Ok(Self{links,pending:BTreeMap::new(),file,poisoned:false,records})
    }
    pub fn retain_characters(&mut self,alive:impl Fn(u64)->bool)->io::Result<bool>{
        // Removing a patron splits the tree; it must never silently transfer
        // their vassals to a different patron without consent.
        let removed:Vec<_>=self.links.iter().filter(|(child,parent)|!alive(**child)||!alive(**parent)).map(|(child,_)|*child).collect();
        for child in &removed{self.commit(*child,0)?;}
        self.pending.retain(|child,(parent,_)|alive(*child)&&alive(*parent));
        Ok(!removed.is_empty())
    }
    pub fn monarch(&self,id:u64)->u64 {let mut root=id;for _ in 0..=self.links.len(){match self.links.get(&root){Some(p)=>root=*p,None=>break}}root}
    pub fn room(&self,id:u64)->Option<u32>{
        let root=self.monarch(id);
        (root!=id||self.links.values().any(|p|*p==id)).then_some(0x20000000|(root as u32&0x0fffffff))
    }
    fn commit(&mut self,child:u64,parent:u64)->io::Result<()> {
        if self.poisoned{return Err(error("Allegiance journal requires restart"))}
        if self.records>=20000{return Err(error("Allegiance journal record limit reached"))}
        let mut bytes=serde_json::to_vec(&(1u32,child,parent)).map_err(io::Error::other)?;bytes.push(b'\n');
        if let Err(e)=self.file.write_all(&bytes).and_then(|_|self.file.sync_all()){self.poisoned=true;return Err(e)}
        self.records+=1;if parent==0{self.links.remove(&child);}else{self.links.insert(child,parent);}Ok(())
    }
    pub fn act(&mut self,actor:u64,action:Action,now:u64,online:&BTreeSet<u64>,characters:&BTreeMap<u64,Character>)->io::Result<Vec<crate::groups::Delivery>> {
        if !online.contains(&actor)||!characters.get(&actor).is_some_and(|c|!c.deleted){return Err(error("Allegiance requires a ready character"))}
        self.pending.retain(|_,(_,until)|*until>now);
        let mut notices=Vec::new();
        match action {
            Action::Swear(parent)=>{
                if !online.contains(&parent)||!characters.get(&parent).is_some_and(|c|!c.deleted){return Err(error("The selected patron is unavailable"))}
                if self.pending.contains_key(&actor){return Err(error("An allegiance request is already pending"))}
                if !valid_link(&self.links,actor,parent){return Err(error("Break your current patron link first; allegiance cycles are not allowed"))}
                if self.links.len()>=4096||self.pending.len()>=128{return Err(error("Allegiance request limit reached"))}
                let packet=call(0x8203002f,&[info(&characters[&actor].name),PackValue::LongInt(actor)])?;
                self.pending.insert(actor,(parent,now.saturating_add(60_000)));
                notices.push(crate::groups::Delivery{recipient:parent,data:packet});
                notices.push(crate::groups::Delivery{recipient:actor,data:script::chat("Your allegiance request was sent. The patron must accept it.",1).map_err(io::Error::other)?});
                return Ok(notices)
            }
            Action::Accept(child)|Action::Decline(child)=>{
                if !matches!(self.pending.get(&child),Some((parent,until)) if *parent==actor&&*until>now){return Err(error("That allegiance request has expired"))}
                let accept=matches!(action,Action::Accept(_));
                if accept {
                    if !online.contains(&child)||!valid_link(&self.links,child,actor){return Err(error("The requesting character is no longer eligible"))}
                    self.commit(child,actor)?;
                }
                self.pending.remove(&child);
                notices.push(crate::groups::Delivery{recipient:child,data:call(0x82030033,&[PackValue::Int(if accept{0}else{0x1585}),info(&characters[&actor].name),PackValue::LongInt(actor)])?});
                if accept {notices.push(crate::groups::Delivery{recipient:actor,data:call(0x8203002d,&[PackValue::Int(0),info(&characters[&child].name),PackValue::LongInt(child)])?});}
                else{return Ok(notices)}
            }
            Action::Break(other)=>{
                let child=if self.links.get(&actor)==Some(&other){actor}else if self.links.get(&other)==Some(&actor){other}else{return Err(error("You may only break your own patron or vassal link"))};
                self.commit(child,0)?;
            }
        }
        // A split can affect room membership throughout both subtrees. Refresh
        // ready clients together; offline characters are refreshed on login.
        for id in online {notices.extend(self.updates(*id,characters,online)?);}
        Ok(notices)
    }
    pub fn updates(&self,id:u64,characters:&BTreeMap<u64,Character>,online:&BTreeSet<u64>)->io::Result<Vec<crate::groups::Delivery>>{
        Ok(vec![crate::groups::Delivery{recipient:id,data:self.profile(id,characters,online)?},crate::groups::Delivery{recipient:id,data:call(0x82030030,&[PackValue::Int(self.room(id).unwrap_or(0))])?}])
    }
    pub fn profile(&self,id:u64,characters:&BTreeMap<u64,Character>,online:&BTreeSet<u64>)->io::Result<Vec<u8>> {
        if self.room(id).is_none(){return super::unaffiliated().map_err(io::Error::other)}
        let root=self.monarch(id);let Some(monarch)=characters.get(&root).filter(|c|!c.deleted) else{return super::unaffiliated().map_err(io::Error::other)};
        let mut graph=Graph::default();let profile=graph.heap(0x1b5,vec![],vec![]);
        let name=graph.native(NativeObject::StringInfo(literal(&format!("{}'s allegiance",monarch.name))));
        let me=graph.member(characters.get(&id).ok_or_else(||error("Unknown allegiance member"))?);
        let patron_id=self.links.get(&id).copied();let patron=patron_id.and_then(|p|characters.get(&p)).map(|c|graph.member(c)).unwrap_or(u32::MAX);
        let monarch=graph.member(monarch);
        let children:Vec<_>=self.links.iter().filter(|(c,p)|**p==id&&characters.get(c).is_some_and(|c|!c.deleted)).map(|(c,_)|*c).collect();
        let members=children.iter().map(|c|u64::from(graph.member(&characters[c]))).collect();
        let vassals=graph.native(NativeObject::List(ScriptList{native_type:ntype::R_LIST,items:members}));
        let flags=graph.native(NativeObject::List(ScriptList{native_type:ntype::A_LIST,items:children.iter().map(|c|u64::from(online.contains(c))).collect()}));
        let motd=graph.native(NativeObject::StringInfo(literal("")));
        let officers=graph.native(NativeObject::Hash(ScriptHash{native_type:ntype::LA_HASH_SET,bucket_index:0,keys:vec![],values:vec![]}));
        let total=characters.keys().filter(|c|self.monarch(**c)==root&&!characters[c].deleted).count() as u32;
        graph.set(profile,vec![name,patron,me,monarch,u32::from(online.contains(&id)),u32::from(patron_id.is_some_and(|p|online.contains(&p))),vassals,flags,motd,u32::from(online.contains(&root)),0,total,officers],vec![1,1,1,1,2,2,1,1,1,2,2,2,1]);
        call(super::PROFILE,&[PackValue::Reference(graph.value)])
    }
}
fn literal(s:&str)->StringInfo{StringInfo{literal:Some(s.into()),..Default::default()}}
fn info(s:&str)->PackValue{PackValue::native(NativeObject::StringInfo(literal(s)))}
fn call(f:u32,args:&[PackValue])->io::Result<Vec<u8>>{script::private_call(f,&pack_values(args)).map_err(io::Error::other)}
struct Graph {value:PackedReference}
impl Default for Graph {fn default()->Self{Self{value:PackedReference{ids:vec![],entries:vec![]}}}}
impl Graph {
    fn heap(&mut self,pkg:u32,slots:Vec<u32>,tags:Vec<u8>)->u32{let id=self.value.ids.len() as u32+1;self.value.ids.push(id);self.value.entries.push(PackedEntry::Heap{flags:entry_flags::KIND_HEAP|entry_flags::LOADED|entry_flags::RECURSE,object:HeapObject{pkg_id:pkg,pkg_state_id:0,native:None,slots,tags}});id}
    fn set(&mut self,id:u32,slots:Vec<u32>,tags:Vec<u8>){if let PackedEntry::Heap{object,..}=&mut self.value.entries[id as usize-1]{object.slots=slots;object.tags=tags;}}
    fn native(&mut self,native:NativeObject)->u32{let id=self.heap(u32::MAX,vec![],vec![]);if let PackedEntry::Heap{object,..}=&mut self.value.entries[id as usize-1]{object.native=Some(native)}id}
    fn member(&mut self,c:&Character)->u32{
        let name=self.native(NativeObject::StringInfo(literal(&c.name)));let (race,gender)=c.creation.as_ref().map(|v|(v.race,v.sex)).unwrap_or((1,0x1000));
        // Rank/XP pass-up are deliberately not fabricated from member count.
        self.heap(0x1b1,vec![0,0,race,u32::from(c.progression.level().number),u32::MAX,gender,0,0,0,c.id as u32,(c.id>>32) as u32,0,0,name],vec![0x82,0xcd,2,2,1,2,0x82,0xcd,2,0x82,0xcd,2,2,1])
    }
}
#[cfg(test)]mod tests {
    use super::*;
    #[test]fn links_reject_cycles_and_survive_reopen(){
        let path=std::env::temp_dir().join(format!("ac2-allegiance-{}-{}.journal",std::process::id(),std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
        let mut m=Memberships::open(&path).unwrap();assert!(valid_link(&m.links,1,2));m.commit(1,2).unwrap();m.commit(2,3).unwrap();
        assert!(!valid_link(&m.links,3,1));assert!(!valid_link(&m.links,1,4));assert_eq!(m.room(1),m.room(3));drop(m);
        let mut m=Memberships::open(&path).unwrap();assert_eq!(m.monarch(1),3);
        assert!(m.retain_characters(|id|id!=2).unwrap());assert_eq!(m.room(1),None);assert_eq!(m.room(3),None);drop(m);
        let m=Memberships::open(&path).unwrap();assert!(m.links.is_empty());drop(m);std::fs::remove_file(path).unwrap();
    }
    #[test]fn action_wire_requires_exact_target(){assert_eq!(decode(SWEAR,&pack_values(&[PackValue::LongInt(15)])).unwrap().unwrap(),Action::Swear(15));assert!(decode(ACCEPT,&pack_values(&[PackValue::Int(15)])).unwrap().is_err());assert!(decode(BREAK,&pack_values(&[PackValue::LongInt(0)])).unwrap().is_err());}
}
