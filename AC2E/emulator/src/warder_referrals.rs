//! Immutable `wr1` conversation policy. These are introductions, not substitutes
//! for the excavation, notebook or potion-raid quests in the same NPC catalogue.
use serde::Deserialize;
use std::{collections::BTreeMap, io, sync::OnceLock};

pub const ESPER: u32 = 0x470015c2;
#[derive(Deserialize)]
pub struct Profile {
    pub id: u32, pub title: String, pub giver: u32, pub recipient: u32,
    pub giver_name: String, pub recipient_name: String,
    pub level: u16, pub limit: u64, pub repeat: u64, pub xp: u64, pub direction: String,
    #[serde(default)] pub greeting: Option<String>,
    #[serde(default)] pub welcome: Option<String>,
}
#[derive(Deserialize)]
struct Actor { id: u64, entity: u32, quest_entity: u32, name: String }

#[derive(Deserialize)]
struct Content {
    version: u32, quests: Vec<Profile>,
    #[serde(default)] interiors: Vec<Actor>,
}

fn content() -> &'static Content {
    static DATA: OnceLock<Content> = OnceLock::new();
    DATA.get_or_init(|| {
        let mut c: Content = serde_json::from_str(include_str!("../content/warder-referrals-v1.json")).expect("Warder referrals");
        let d: Content = serde_json::from_str(include_str!("../content/drudgerton-referral-v1.json")).expect("Drudgerton referral");
        assert_eq!((c.version, d.version), (1, 1));
        c.quests.extend(d.quests);
        c.interiors.extend(d.interiors);
        c
    })
}

pub fn profiles() -> &'static [Profile] { &content().quests }

/// An explicit name and an exact placement distinguish the two late retail NPCs
/// from other Legions actors using their presentation template.
pub fn speaker_entity(id: u64, entity: u32, name: Option<&str>) -> Option<u32> {
    let c = content();
    if let Some(a) = c.interiors.iter().find(|a| a.id == id) {
        return (a.entity == entity && name == Some(a.name.as_str())).then_some(a.quest_entity);
    }
    (supported(entity) && !c.interiors.iter().any(|a| a.quest_entity == entity)).then_some(entity)
}

pub fn supported(entity:u32)->bool { profiles().iter().any(|q|q.giver==entity || q.recipient==entity) }
pub fn speaker(entity:u32)-> &'static str {
    for q in profiles() { if q.giver==entity{return &q.giver_name;} if q.recipient==entity{return &q.recipient_name;} }
    "Warder"
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct State { pub started:u64, pub completed:u64 }
impl State {
    pub fn expired(&self,q:&Profile,time:u64)->bool {self.completed==0 && q.limit>0 && time>=self.started.saturating_add(q.limit)}
    fn restartable(&self,q:&Profile,time:u64)->bool {
        self.expired(q,time) || self.completed>0 && time>=self.completed.saturating_add(q.repeat)
    }
    pub fn guidance(&self,q:&Profile,time:u64)->String {
        if self.completed>0 {
            let repeat = if q.repeat % 86400 == 0 { format!("{} days", q.repeat / 86400) } else { format!("{} hours", q.repeat / 3600) };
            format!("{} complete. Reward: {} experience. You may repeat this introduction after {}.",q.title,q.xp,repeat)
        }
        else if self.expired(q,time) {format!("{} expired. Return to {} to accept it again.",q.title,q.giver_name)}
        else {q.direction.clone()}
    }
}
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Book(pub BTreeMap<u32,State>);
pub struct Conversation {pub book:Book,pub rewards:Vec<crate::progression::Command>,pub text:String}
impl Book {
    pub fn converse(&self,entity:u32,level:u16,camp_complete:bool,time:u64)->io::Result<Conversation> {
        let bad=|s|io::Error::other(s);
        if time==0 || level==0 || !supported(entity) {return Err(bad("Invalid Warder conversation."));}
        for (id,s) in &self.0 {
            if !profiles().iter().any(|q|q.id==*id) || s.started==0 || time<s.started || time<s.completed {
                return Err(bad("Invalid Warder journal or clock."));
            }
        }
        let mut next=self.clone();let mut rewards=Vec::new();let mut text=Vec::new();
        for q in profiles().iter().filter(|q|q.recipient==entity) {
            if let Some(s)=next.0.get_mut(&q.id) {
                if s.completed==0 && !s.expired(q,time) {
                    s.completed=time;
                    if let Some(welcome) = &q.welcome { text.push(welcome.clone()); }
                    rewards.extend(crate::quest_points::completion_rewards(q.id, q.xp));
                }
                text.push(s.guidance(q,time));
            } else if let Some(greeting) = &q.greeting {
                text.push(greeting.clone());
            }
        }
        for q in profiles().iter().filter(|q|q.giver==entity) {
            if level<q.level {text.push(format!("Reach level {} to begin {}.",q.level,q.title));continue;}
            if entity==ESPER && !camp_complete {text.push("Finish Drudge Outcast Camp before accepting the Cragstone introduction.".into());continue;}
            let s=next.0.entry(q.id).or_insert(State{started:time,completed:0});
            if s.restartable(q,time){*s=State{started:time,completed:0};}
            text.push(s.guidance(q,time));
        }
        // Native NPC speech admits one printable paragraph. Journal details
        // have their own multiline layout; do not send controls in speech.
        Ok(Conversation{book:next,rewards,text:text.join(" ")})
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn route_completes_and_starts_next_task_once() {
        let p=&profiles()[..3];let b=Book::default();
        assert!(b.converse(ESPER,3,false,100).unwrap().book.0.is_empty());
        assert!(b.converse(ESPER,2,true,100).unwrap().book.0.is_empty());
        let mut b=b.converse(ESPER,3,true,100).unwrap().book;
        for (i,q) in p.iter().enumerate() {
            let r=b.converse(q.recipient,3,true,101+i as u64).unwrap();
            assert_eq!(r.rewards,crate::quest_points::completion_rewards(q.id,q.xp));assert!(r.book.0[&q.id].completed>0);
            assert!(crate::interaction::speech(123,speaker(q.recipient),&r.text).is_ok());
            assert!(crate::interaction::dialogue(speaker(q.recipient),&r.text).is_ok());
            if i+1<p.len(){assert_eq!(r.book.0[&p[i+1].id].completed,0);}
            b=r.book;
            let again=b.converse(q.recipient,3,true,104+i as u64).unwrap();
            assert!(again.rewards.is_empty());assert_eq!(again.book,b);
        }
        assert!(b.converse(ESPER,3,true,99).is_err());
    }
    #[test]
    fn independent_acceptance_expiry_and_repeat_do_not_pay_twice() {
        let q=&profiles()[2];
        let b=Book::default().converse(q.giver,1,false,100).unwrap().book;
        let r=b.converse(q.recipient,1,false,100+q.limit).unwrap();
        assert!(r.rewards.is_empty());assert_eq!(r.book,b);assert!(r.text.contains("expired"));
        let b=b.converse(q.giver,1,false,100+q.limit).unwrap().book;
        let r=b.converse(q.recipient,1,false,101+q.limit).unwrap();assert_eq!(r.rewards,crate::quest_points::completion_rewards(q.id,30));
        let before=r.book.converse(q.giver,1,false,100+q.limit+q.repeat).unwrap();assert_eq!(before.book,r.book);
        let repeat=r.book.converse(q.giver,1,false,101+q.limit+q.repeat).unwrap();
        assert_eq!(repeat.book.0[&q.id].completed,0);assert!(repeat.rewards.is_empty());
    }
}
