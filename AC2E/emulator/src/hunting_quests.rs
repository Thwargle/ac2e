//! Versioned NPC hunts. Guide rules, native identities, authored quest IDs.
//! Kills only advance an accepted quest; rewards require returning to its NPC.
use serde::Deserialize;
use std::{collections::{BTreeMap, BTreeSet}, io, sync::OnceLock};

#[derive(Clone, Debug, Deserialize)]
pub struct Profile {
    pub id: u32, pub title: String, pub text: String,
    pub npc: u32, pub npc_name: String, pub targets: Vec<u32>, pub goal: u16,
    pub level: u16, pub xp_basis_points: u64, pub xp_cap: u64,
    pub prerequisite: u32, pub limit_seconds: u64, pub repeat_seconds: u64,
    #[serde(default)] pub intended_level: Option<u16>,
    #[serde(default)] pub key: Option<RewardKey>,
}
#[derive(Clone, Debug, Deserialize)]
pub struct RewardKey { pub item: u32, pub visual: u32, pub name: String }
fn original_profiles() -> &'static [Profile] {
    static DATA: OnceLock<Vec<Profile>> = OnceLock::new();
    DATA.get_or_init(|| {
        #[derive(Deserialize)] struct Catalog { version: u32, quests: Vec<Profile> }
        let mut c: Catalog = serde_json::from_str(include_str!("../content/hunting-content-v1.json")).expect("hunting policy");
        assert_eq!(c.version, 1);
        let expansion: Catalog = serde_json::from_str(include_str!("../content/hunting-content-v2.json")).expect("hunting expansion");
        assert_eq!(expansion.version, 2);
        assert!(c.quests.iter().all(|q|q.id<=0x4000fc0a));
        assert!(expansion.quests.iter().all(|q|q.id>0x4000fc0a));
        c.quests.extend(expansion.quests);
        let knorr: Catalog = serde_json::from_str(include_str!("../content/hunting-content-v3.json")).expect("Knorr NPC hunts");
        assert_eq!(knorr.version,3);
        assert!(knorr.quests.iter().all(|q|q.id>0x4000fc23));
        c.quests.extend(knorr.quests);
        let arwic: Catalog = serde_json::from_str(include_str!("../content/hunting-content-v4.json")).expect("Arwic NPC hunts");
        assert_eq!(arwic.version,4);
        assert!(arwic.quests.iter().all(|q|q.id>0x4000fc2a));
        c.quests.extend(arwic.quests);
        let aerie: Catalog = serde_json::from_str(include_str!("../content/hunting-content-v5.json")).expect("Aerie NPC hunts");
        assert_eq!(aerie.version, 5);
        assert!(aerie.quests.iter().all(|q| q.id > 0x4000fc2e));
        c.quests.extend(aerie.quests);
        let hakata: Catalog = serde_json::from_str(include_str!("../content/hunting-content-v6.json")).expect("Hakata NPC hunts");
        assert_eq!(hakata.version, 6);
        assert!(hakata.quests.iter().all(|q| q.id > 0x4000fc31));
        c.quests.extend(hakata.quests);
        let mut seen=std::collections::BTreeSet::new();
        for q in &c.quests {
            assert!(seen.insert(q.id) && q.id & 0xffffff00 == 0x4000fc00 && q.npc >> 24 == 0x47);
            assert!((1..=100).contains(&q.goal) && (1..=150).contains(&q.level));
            assert!(q.intended_level.is_none_or(|level|(q.level..=150).contains(&level)));
            assert!(!q.targets.is_empty() && q.targets.iter().all(|d|d>>24==0x47));
            assert!(q.xp_basis_points<=10000 && q.xp_cap>0);
            assert!(q.repeat_seconds>0);
            assert!(q.prerequisite==0 || seen.contains(&q.prerequisite) && q.prerequisite!=q.id);
        }
        c.quests
    })
}
pub fn profiles() -> &'static [Profile] {
    static CURRENT: OnceLock<Vec<Profile>> = OnceLock::new();
    CURRENT.get_or_init(|| {
        #[derive(Deserialize)]
        struct Revision { id: u32, level: u16, xp: u64 }
        #[derive(Deserialize)]
        struct Revisions { limit_seconds: u64, repeat_seconds: u64, quests: Vec<Revision> }
        let changes: Revisions = serde_json::from_str(include_str!("../content/hunting-updates-v7.json"))
            .expect("Omishan hunt revisions");
        let mut current = original_profiles().to_vec();
        let mut seen = BTreeSet::new();
        for revision in changes.quests {
            let q = current.iter_mut().find(|q| q.id == revision.id).expect("known hunt");
            assert!(seen.insert(q.id) && q.xp_basis_points == 0 && revision.xp > 0);
            q.xp_cap = revision.xp;
            q.level = revision.level;
            q.limit_seconds = changes.limit_seconds;
            q.repeat_seconds = changes.repeat_seconds;
        }
        current
    })
}
fn versioned_profiles(version: u8) -> &'static [Profile] {
    if version >= 7 { profiles() } else { original_profiles() }
}
pub fn profile(id:u32)->Option<&'static Profile> {profiles().iter().find(|q|q.id==id)}
fn in_version(q:&Profile,version:u8)->bool {
    match version { 1 => q.id<=0x4000fc0a, 2 => q.id<=0x4000fc23, 3 => q.id<=0x4000fc2a, 4 => q.id<=0x4000fc2e, 5 | 6 => q.id<=0x4000fc31, _ => true }
}
pub fn supported(npc:u32)->bool {profiles().iter().any(|q|q.npc==npc)}
pub fn speaker(npc:u32)->&'static str {profiles().iter().find(|q|q.npc==npc).map_or("Quest giver",|q|q.npc_name.as_str())}
#[derive(Clone,Debug,Default,PartialEq,Eq)]
pub struct State {pub count:u16,pub started:u64,pub completed:u64,pub rewarded_xp:u64}
#[derive(Clone,Debug,Default,PartialEq,Eq)]
pub struct Book(pub BTreeMap<u32,State>, BTreeSet<u32>);
pub struct Conversation {pub book:Book,pub xp:u64,quest:u32,pub text:String,pub key:Option<&'static RewardKey>}
impl Conversation {
    pub(crate) fn rewards(&self) -> Vec<crate::progression::Command> {
        if self.xp == 0 { return Vec::new(); }
        crate::quest_points::completion_rewards(self.quest, self.xp)
    }
    /// Reserve the native key in the same transaction as XP and journal completion.
    pub fn settle(&self,bag:&crate::inventory::Inventory,p:&crate::progression::Progression,first:u64)
        ->io::Result<(crate::inventory::Inventory,crate::progression::Progression,Option<crate::inventory::Item>)> {
        use crate::inventory::{Command,Item,Location};
        if self.xp==0 {return Ok((bag.clone(),p.clone(),None));}
        let item=self.key.map(|key| {
            Ok::<_,io::Error>(Item{roll:None,id:first,definition:key.item,visual:key.visual,quantity:1,stack_limit:1,
                equipment_mask:0,location:Location::Bag(bag.free_slot().ok_or_else(||bad("Make room for your quest reward key."))?)})
        }).transpose()?;
        let grants=item.iter().cloned().map(Command::Grant).collect::<Vec<_>>();
        let (bag,state)=crate::player_transaction::candidate(bag,p,&grants,&self.rewards(),Some(first))?;
        Ok((bag,state,item))
    }
}
fn bad(s:&str)->io::Error {io::Error::other(s)}
impl State {
    fn active(&self,q:&Profile,now:u64)->bool {
        self.completed==0 && now>=self.started && (q.limit_seconds==0 || now-self.started<q.limit_seconds)
    }
    fn restartable(&self,q:&Profile,now:u64)->bool {
        if self.completed==0 && q.limit_seconds==0 {return false;}
        let (start,delay)=if self.completed>0 {(self.completed,q.repeat_seconds)}else{(self.started,q.limit_seconds)};
        now>=start && now-start>=delay
    }
}
impl Profile {
    pub fn reward(&self,p:&crate::progression::Progression)->u64 {
        if self.xp_basis_points==0 {return self.xp_cap;}
        // Pyreal TotalXpMax is inclusive; wiki percentages are of one level's
        // XP requirement, not accumulated character XP or unspent skill XP.
        let l=p.level();
        // The terminal Pyreal level has a two-XP sentinel band, not a real
        // next-level requirement. All these hunts reach their advertised cap
        // below level 150; keep that capped reward for maximum-level players.
        if l.number==150 {return self.xp_cap;}
        ((l.xp_cap-l.xp+1)*self.xp_basis_points/10000).min(self.xp_cap)
    }
}
impl Book {
    /// Keep turn-in credit when a player starts a repeat of a prerequisite hunt.
    pub fn has_completed(&self, id: u32) -> bool { self.1.contains(&id) }
    pub fn guidance(q:&Profile,s:&State)->String {
        if s.completed>0 {return format!("{} complete. Earned {} experience. Repeat after {} hours.",q.title,s.rewarded_xp,q.repeat_seconds/3600);}
        let deadline=if q.limit_seconds==0 {"No time limit.".to_owned()} else {format!("Time limit: {} hours from acceptance.",q.limit_seconds/3600)};
        format!("{} Progress: {} / {}. {} {}",q.text,s.count,q.goal,
            if s.count==q.goal {"Return to the quest giver to claim your reward."} else {""},deadline)
    }
    pub fn converse(&self,npc:u32,p:&crate::progression::Progression,now:u64)->io::Result<Conversation> {
        self.converse_version(npc,p,now,7)
    }
    /// Journal replay must not offer a newly added chain link during an old
    /// qh1 interaction (notably Gregor's previously standalone Harvester hunt).
    pub(crate) fn converse_version(&self,npc:u32,p:&crate::progression::Progression,now:u64,version:u8)->io::Result<Conversation> {
        if now==0 || !p.alive() {return Err(bad("A living character is required."));}
        let catalog = versioned_profiles(version);
        for (id,s) in &self.0 {
            let q=catalog.iter().find(|q| q.id == *id).ok_or_else(||bad("Unknown hunting quest."))?;
            if s.started==0 || now<s.started || now<s.completed || s.count>q.goal ||
                (s.completed>0 && (s.count!=q.goal || s.rewarded_xp==0)) {return Err(bad("Invalid hunting quest state/time."));}
        }
        let choices=catalog.iter().filter(|q|in_version(q,version) && q.npc==npc &&
            (p.level().number>=q.level || version>=7 && self.0.get(&q.id).is_some_and(|s|s.active(q,now))) &&
            (q.prerequisite==0 || self.0.get(&q.prerequisite).is_some_and(|s|s.completed>0)
                || version>=6 && self.has_completed(q.prerequisite))).collect::<Vec<_>>();
        if choices.is_empty() {return Err(bad("You do not meet this hunt's level or prerequisite requirement."));}
        // Finish or continue an accepted hunt before offering the next link.
        // Unstarted chain links precede repeatable earlier links.
        let q=choices.iter().copied().find(|q|self.0.get(&q.id).is_some_and(|s|s.active(q,now)))
            .or_else(||choices.iter().copied().find(|q|!self.0.contains_key(&q.id)))
            .or_else(||choices.iter().copied().find(|q|self.0[&q.id].restartable(q,now)))
            .unwrap_or(choices[0]);
        let mut next=self.clone();let mut xp=0;
        let s=next.0.entry(q.id).or_insert(State{started:now,..Default::default()});
        if s.restartable(q,now) {*s=State{started:now,..Default::default()};}
        if s.active(q,now) && s.count==q.goal {
            xp=q.reward(p);s.completed=now;s.rewarded_xp=xp;next.1.insert(q.id);
        }
        let key=q.key.as_ref().filter(|_|xp>0);
        let mut text=Self::guidance(q,s);
        if let Some(key)=key {text.push_str(&format!(" You receive {}.",key.name));}
        Ok(Conversation{book:next,xp,quest:q.id,text,key})
    }
    /// Called only for an authoritative death reward, in the same durable event.
    pub fn kill(&self,entity:u32,now:u64)->Self {
        self.kill_version(entity,now,7)
    }
    pub(crate) fn kill_version(&self,entity:u32,now:u64,version:u8)->Self {
        let mut next=self.clone();
        let catalog = versioned_profiles(version);
        for (id,s) in &mut next.0 {
            let Some(q)=catalog.iter().find(|q| q.id == *id) else {continue};
            if in_version(q,version) && s.active(q,now) && q.targets.contains(&entity) {s.count=(s.count+1).min(q.goal);}
        }
        next
    }
}

#[cfg(test)] mod tests {
    use super::*;
    fn player(level:u16)->crate::progression::Progression {
        let mut p=crate::progression::Progression::default();
        while p.level().number<level {p=p.apply(&crate::progression::Command::AwardXp(p.level().xp_cap+1-p.xp)).unwrap();}
        p
    }
    #[test] fn acceptance_kills_return_timers_and_capped_rewards() {
        for q in profiles() {
            let p=player(q.level);let mut book=Book::default();
            let mut prerequisite=q.prerequisite;
            while prerequisite>0 {
                book.0.insert(prerequisite,State{count:10,started:1,completed:2,rewarded_xp:1});
                prerequisite=profile(prerequisite).unwrap().prerequisite;
            }
            assert_eq!(book.kill(q.targets[0],100),book);
            // A chain speaker may still offer a lower-level earlier quest.
            if q.level>1 {
                let low=book.converse(q.npc,&player(q.level-1),100);
                assert!(low.is_err() || !low.unwrap().book.0.contains_key(&q.id));
            }
            book=book.converse(q.npc,&p,100).unwrap().book;
            assert!(book.0.contains_key(&q.id));
            let text=Book::guidance(q,&book.0[&q.id]);
            assert!(crate::interaction::speech(10,&q.npc_name,&text).is_ok());
            assert!(crate::interaction::dialogue(&q.npc_name,&text).is_ok());
            assert_eq!(book.kill(0x47000000,101),book);
            for n in 0..q.goal {book=book.kill(q.targets[0],101+u64::from(n));}
            assert_eq!(book.0[&q.id].completed,0);
            assert_eq!(book.kill(q.targets[0],115),book);
            let result=book.converse(q.npc,&p,120).unwrap();
            assert!(crate::interaction::speech(10,&q.npc_name,&result.text).is_ok());
            assert!(crate::interaction::dialogue(&q.npc_name,&result.text).is_ok());
            assert_eq!(result.xp,q.reward(&p));assert!(result.xp>0 && result.xp<=q.xp_cap);
            assert_eq!(q.reward(&player(150)),q.xp_cap);
            assert_eq!(result.book.converse(q.npc,&p,121).unwrap().xp,0);
            if q.limit_seconds>0 {
                assert_eq!(book.kill(q.targets[0],100+q.limit_seconds),book);
                // Expired ready quests cannot pay out after their deadline.
                assert_eq!(book.converse(q.npc,&p,100+q.limit_seconds).unwrap().xp,0);
            } else {
                assert_eq!(book.converse(q.npc,&p,100+365*86400).unwrap().xp,q.xp_cap);
                assert!(Book::guidance(q,&book.0[&q.id]).contains("No time limit"));
            }
        }
        let q=profile(0x4000fc07).unwrap();assert_eq!(q.reward(&player(48)),5829950);
        assert_eq!(q.reward(&player(70)),q.xp_cap);
    }
    #[test] fn chain_requires_completion_and_journal_preserves_ready_state() {
        let p=player(60);let first=&profiles()[0];
        let mut b=Book::default().converse(first.npc,&p,100).unwrap().book;
        assert_eq!(b.0.len(),1);
        for _ in 0..10 {b=b.kill(first.targets[0],101);}
        let mut j=crate::quests::Book::default();j.hunting=b.clone();
        assert!(j.list_graph().unwrap().len()>100);assert_eq!(j.updates().unwrap().len(),1);
        b=b.converse(first.npc,&p,102).unwrap().book;
        b=b.converse(first.npc,&p,103).unwrap().book;
        assert_eq!(b.0.len(),2);assert!(b.0.contains_key(&profiles()[1].id));
        assert!(!b.0.contains_key(&profiles()[2].id));
    }
    #[test] fn expired_repeat_preserves_credit_for_next_hunt() {
        let first=profile(0x4000fc04).unwrap(); // Gregor's level 50 hunt unlocks a level 55 hunt.
        let next=profile(0x4000fc11).unwrap();
        let beginner=player(first.level);
        let mut book=Book::default().converse(first.npc,&beginner,100).unwrap().book;
        for _ in 0..first.goal {book=book.kill(first.targets[0],101);}
        book=book.converse(first.npc,&beginner,102).unwrap().book;
        let repeat=102+first.repeat_seconds;
        book=book.converse(first.npc,&beginner,repeat).unwrap().book;
        assert_eq!(book.0[&first.id].completed,0);
        assert!(book.has_completed(first.id));
        let later=repeat+first.limit_seconds;
        let advanced=player(next.level);
        assert!(!book.converse_version(next.npc,&advanced,later,5).unwrap().book.0.contains_key(&next.id));
        let offered=book.converse(next.npc,&advanced,later).unwrap();
        assert_eq!(offered.xp,0);
        assert_eq!(offered.book.0[&next.id].started,later);
    }
    #[test] fn arwic_hunts_use_distinct_native_death_effects_and_guide_reset_times() {
        let p=player(5);
        let mut book=Book::default();
        let hunts=[(0x4000fc2b,0x47001276,60),(0x4000fc2c,0x47000f82,150),
                   (0x4000fc2d,0x47000f9c,120),(0x4000fc2e,0x47000f8b,150)];
        for (id,_,_) in hunts {
            let q=profile(id).unwrap();
            assert!(book.converse_version(q.npc,&p,100,3).is_err());
            book=book.converse(q.npc,&p,100).unwrap().book;
        }
        for (id,entity,reward) in hunts {
            let q=profile(id).unwrap();
            assert_eq!(book.kill_version(entity,101,3),book);
            for _ in 0..5 {book=book.kill(entity,101);}
            for (other,_,_) in hunts.into_iter().filter(|(other,_,_)| *other>id) {
                assert_eq!(book.0[&other].count,0);
            }
            let result=book.converse(q.npc,&p,102).unwrap();
            assert_eq!(result.xp,reward);
            book=result.book;
            let reset=if id==0x4000fc2e {7*3600} else {7*86400};
            assert_eq!(book.converse(q.npc,&p,101+reset).unwrap().book,book);
            assert_eq!(book.converse(q.npc,&p,102+reset).unwrap().book.0[&id].count,0);
        }
    }
    #[test] fn fixed_rewards_no_deadline_variants_and_repeat_boundary() {
        let q=profile(0x4000fc1f).unwrap(); // Faisi plus Seeker variants
        assert!(q.targets.contains(&0x470012b8));
        assert!(!profile(0x4000fc1e).unwrap().targets.contains(&0x47002f7f)); // Gauntlet Ulu
        assert!(!profile(0x4000fc14).unwrap().targets.contains(&0x47002fd7)); // Gauntlet Lumna
        let p=player(30);let mut b=Book::default();
        b.0.insert(q.prerequisite,State{count:10,started:1,completed:2,rewarded_xp:22000});
        b=b.converse(q.npc,&p,100).unwrap().book;
        b=b.kill(0x47002f7f,101);assert_eq!(b.0[&q.id].count,0);
        for i in 0..10 {b=b.kill(q.targets[i%q.targets.len()],100+365*86400);}
        let now=100+365*86400;let result=b.converse(q.npc,&p,now).unwrap();
        assert_eq!(result.xp,40000);assert_eq!(q.reward(&player(150)),40000);
        let legacy_player=player(23);
        assert_eq!(b.converse_version(q.npc,&legacy_player,now,6).unwrap().xp,30000);
        // The newer minimum applies to acceptance. Previously accepted hunts can still finish.
        assert_eq!(b.converse(q.npc,&legacy_player,now).unwrap().xp,40000);
        // Use a standalone fixed-XP NPC to exercise the exact repeat boundary.
        let q=profile(0x4000fc23).unwrap();
        let mut b=Book::default();b.0.insert(q.id,State{count:10,started:100,completed:200,rewarded_xp:55000});
        assert_eq!(b.converse(q.npc,&p,200+q.repeat_seconds-1).unwrap().book,b);
        let c=b.converse(q.npc,&p,200+q.repeat_seconds).unwrap();
        assert_eq!(c.xp,0);assert_eq!(c.book.0[&q.id].count,0);
        assert_eq!(c.book.0[&q.id].started,200+q.repeat_seconds);
        assert_eq!(b.kill(q.targets[0],199),b,"completed hunts cannot gain kills");
    }
    #[test] fn old_catalog_replay_cannot_offer_new_chain_links() {
        let q=profile(0x4000fc04).unwrap();let p=player(70);let mut b=Book::default();
        b.0.insert(q.id,State{count:10,started:1,completed:20,rewarded_xp:q.xp_cap});
        let now=20+q.repeat_seconds;
        let old=b.converse_version(q.npc,&p,now,1).unwrap();
        assert_eq!(old.book.0.len(),1);assert_eq!(old.book.0[&q.id].count,0);
        let new=b.converse(q.npc,&p,now).unwrap();
        assert!(new.book.0.contains_key(&0x4000fc11));
        let target=profile(0x4000fc11).unwrap().targets[0];
        assert_eq!(new.book.kill_version(target,now+1,1),new.book);
        assert_eq!(new.book.kill(target,now+1).0[&0x4000fc11].count,1);
    }
    #[test] fn large_journals_only_send_changed_entries_after_combat() {
        let mut old=crate::quests::Book::default();
        for q in profiles() {old.hunting.0.insert(q.id,State{count:q.goal,started:1,completed:2,rewarded_xp:q.xp_cap});}
        assert_eq!(old.updates().unwrap().len(),profiles().len());
        assert!(old.updates_since(&old).unwrap().is_empty());
        let q=profile(0x4000fc23).unwrap();
        let mut next=old.clone();next.hunting.0.get_mut(&q.id).unwrap().completed=0;
        next.hunting.0.get_mut(&q.id).unwrap().count=3;
        assert_eq!(next.updates_since(&old).unwrap().len(),1);
        let mut killed=next.clone();killed.hunting=killed.hunting.kill(q.targets[0],100);
        let delta=killed.updates_since(&next).unwrap();assert_eq!(delta.len(),1);
        let bytes=delta.iter().map(Vec::len).sum::<usize>();
        assert!(bytes<2000 && bytes*20<killed.updates().unwrap().iter().map(Vec::len).sum::<usize>());
        assert!(killed.list_graph().unwrap().len()<48000,"Complete login snapshot still fits");
    }
}
