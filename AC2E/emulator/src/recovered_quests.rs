//! Native kill-effect hunt rules, with immutable historical journal policies.
//! Immutable journal policy. Timers use persisted UTC seconds, not session uptime.
use std::{collections::BTreeMap,sync::OnceLock};
use serde::Deserialize;
#[derive(Clone,Debug,Deserialize)]
pub struct Profile {
    pub id:u32,pub title:String,pub text:String,pub goal:u16,pub targets:Vec<u32>,
    pub kill_starters:Vec<u32>,pub use_starters:Vec<u32>,pub level:u16,pub xp:u64,
    pub repeat_seconds:u64,pub limit_seconds:u64,
    #[serde(default)] pub intended_level:Option<u16>,
    #[serde(default)] pub delivery:bool,
    #[serde(default)] pub xp_basis_points:u16,
}
fn original_profiles()->&'static [Profile] {
    static DATA:OnceLock<Vec<Profile>>=OnceLock::new();
    DATA.get_or_init(||{
        #[derive(Deserialize)] struct Catalog {version:u32,quests:Vec<Profile>}
        let d:Catalog=serde_json::from_str(include_str!("../content/inferred-content-v1.json")).expect("validated inferred quest catalog");
        assert_eq!(d.version,1);let mut ids=std::collections::BTreeSet::new();
        for q in &d.quests {assert!(ids.insert(q.id)&&q.id>>24==0x40&&(1..=100).contains(&q.goal)&&(!q.targets.is_empty()||q.delivery&&q.id==DEBT)
            &&!q.title.is_empty()&&(1..=150).contains(&q.level)&&q.xp>0&&q.xp<=100_000_000
            &&q.limit_seconds>0&&q.repeat_seconds>0);}
        d.quests
    })
}
/// Keep the original catalog available for replay and hunts accepted before the revision.
fn hero_profiles() -> &'static [Profile] {
    static CURRENT: OnceLock<Vec<Profile>> = OnceLock::new();
    CURRENT.get_or_init(|| {
        #[derive(Deserialize)]
        struct Catalog { version: u32, quests: Vec<Profile> }
        let pack: Catalog = serde_json::from_str(include_str!("../content/ac2hero-hunts-v2.json"))
            .expect("valid AC2Hero kill quest catalog");
        assert_eq!(pack.version, 2);
        let mut quests = original_profiles().to_vec();
        let mut seen = std::collections::BTreeSet::new();
        for profile in pack.quests {
            assert!(seen.insert(profile.id) && profile.id >> 24 == 0x40 && !profile.kill_starters.is_empty()
                && !profile.targets.is_empty() && profile.goal > 0 && profile.level > 0
                && profile.limit_seconds > 0 && profile.repeat_seconds > 0
                && (profile.xp > 0 || profile.xp_basis_points > 0));
            if let Some(existing) = quests.iter_mut().find(|quest| quest.id == profile.id) {
                *existing = profile;
            } else {
                quests.push(profile);
            }
        }
        let courier = &aerie().quest;
        assert!(!quests.iter().any(|q| q.id == courier.id));
        quests.push(courier.clone());
        let collection = crate::omishan_collection::profile();
        assert!(!quests.iter().any(|q| q.id == collection.id));
        quests.push(collection);
        quests
    })
}

#[derive(Deserialize)]
struct QuestGiver {
    npc: u32,
    speaker: String,
}

#[derive(Deserialize)]
struct BountyDefinition {
    #[serde(flatten)] quest: Profile,
    #[serde(flatten)] giver: QuestGiver,
}

#[derive(Deserialize)]
struct StarterHunt {
    id: u32,
    level: u16,
    intended_level: Option<u16>,
    giver: Option<QuestGiver>,
}

#[derive(Deserialize)]
struct BountyCatalog {
    version: u32,
    quests: Vec<BountyDefinition>,
    starter_hunts: Vec<StarterHunt>,
}

fn bounty_catalog() -> &'static BountyCatalog {
    static DATA: OnceLock<BountyCatalog> = OnceLock::new();
    DATA.get_or_init(|| {
        let catalog: BountyCatalog = serde_json::from_str(include_str!("../content/npc-bounties-v1.json")).expect("valid NPC bounties");
        assert_eq!(catalog.version, 1);
        catalog
    })
}

pub struct NpcBounty {
    pub npc: u32,
    pub speaker: &'static str,
    pub quest: &'static Profile,
}

pub fn npc_bounty(npc: u32) -> Option<NpcBounty> {
    let catalog = bounty_catalog();
    let (id, giver) = catalog.quests.iter().map(|q| (q.quest.id, &q.giver))
        .chain(catalog.starter_hunts.iter().filter_map(|q| q.giver.as_ref().map(|giver| (q.id, giver))))
        .find(|(_, giver)| giver.npc == npc)?;
    let quest = profiles().iter().find(|q| q.id == id)?;
    Some(NpcBounty { npc, speaker: &giver.speaker, quest })
}

fn published_profiles() -> &'static [Profile] {
    static CURRENT: OnceLock<Vec<Profile>> = OnceLock::new();
    CURRENT.get_or_init(|| {
        #[derive(Deserialize)]
        struct Reward { id: String, xp: u64 }
        let revisions: Vec<Reward> = serde_json::from_str(include_str!("../content/incursion-rewards-v3.json"))
            .expect("valid published incursion rewards");
        let mut quests = hero_profiles().to_vec();
        let mut seen = std::collections::BTreeSet::new();
        for revision in revisions {
            let id = u32::from_str_radix(&revision.id, 16).expect("native quest ID");
            let q = quests.iter_mut().find(|q| q.id == id).expect("existing hunt");
            assert!(seen.insert(id) && !q.kill_starters.is_empty() && q.xp_basis_points == 0 && revision.xp > 0);
            q.xp = revision.xp;
        }
        for revision in &bounty_catalog().starter_hunts {
            let q = quests.iter_mut().find(|q| q.id == revision.id).expect("existing elixir hunt");
            assert!(seen.insert(q.id) && !q.use_starters.is_empty() && revision.level > 0
                && revision.intended_level.is_none_or(|level| level >= revision.level));
            q.level = revision.level;
            q.intended_level = revision.intended_level;
        }
        for bounty in &bounty_catalog().quests {
            assert!(!quests.iter().any(|q| q.id == bounty.quest.id));
            quests.push(bounty.quest.clone());
        }
        quests.extend(crate::slavetaker::profiles());
        quests
    })
}

pub fn profiles() -> &'static [Profile] {
    static CURRENT: OnceLock<Vec<Profile>> = OnceLock::new();
    CURRENT.get_or_init(|| {
        #[derive(Deserialize)]
        struct Revision { id: u32, level: u16, xp: u64, limit_seconds: u64 }
        let revision: Revision = serde_json::from_str(include_str!("../content/slimelord-hunt-v1.json"))
            .expect("valid Slimelord hunt revision");
        let mut quests = published_profiles().to_vec();
        let q = quests.iter_mut().find(|q| q.id == revision.id).expect("existing Slimelord quest");
        q.level = revision.level;
        q.xp = revision.xp;
        q.limit_seconds = revision.limit_seconds;
        quests
    })
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(crate) enum HuntRules { Original, Hero, PublishedRewards, #[default] SlimelordRevision }

impl HuntRules {
    fn for_quest(self, id: u32) -> Self {
        // Only the Slimelord journal changed with qk8. Other bestowals still replay
        // under exactly the same policy as their existing item and NPC records.
        if self == Self::SlimelordRevision && id != 0x40000106 { Self::PublishedRewards } else { self }
    }

    fn catalog(self) -> &'static [Profile] {
        match self {
            Self::Original => original_profiles(),
            Self::Hero => hero_profiles(),
            Self::PublishedRewards => published_profiles(),
            Self::SlimelordRevision => profiles(),
        }
    }
}

pub fn state_profile(id: u32, state: &State) -> Option<&'static Profile> {
    state.rules.catalog().iter().find(|quest| quest.id == id)
}

impl Profile {
    fn reward(&self, level: u16) -> u64 {
        if self.xp_basis_points == 0 { return self.xp; }
        let span = crate::progression::quest_level_span(level);
        span * u64::from(self.xp_basis_points) / 10_000
    }
}
#[derive(Clone,Debug,Default,PartialEq,Eq)]
pub struct State {pub count:u16,pub started:u64,pub completed:u64,pub(crate) rules:HuntRules,pub rewarded_xp:u64}
#[derive(Clone,Debug,Default,PartialEq,Eq)]
pub struct Book(pub BTreeMap<u32,State>);
pub fn now()->u64 {std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_secs()}
pub fn use_profile(entity:u32)->Option<&'static Profile>{profiles().iter().find(|q|q.use_starters.contains(&entity))}
pub const DEBT:u32=0x4000fe01;
pub const KANTEN:u32=0x47001a3c;
pub const ORVEN:u32=0x47001c27;
pub const BAG:u32=0x47001c1e;
#[derive(Deserialize)]
struct AerieDelivery {
    #[serde(flatten)] quest: Profile,
    giver: u32, recipient: u32, item: u32, prerequisites: Vec<u32>,
}
fn aerie() -> &'static AerieDelivery {
    static POLICY: OnceLock<AerieDelivery> = OnceLock::new();
    POLICY.get_or_init(|| {
        #[derive(Deserialize)] struct Catalog { referral: AerieDelivery }
        let policy = serde_json::from_str::<Catalog>(include_str!("../content/hunting-content-v5.json"))
            .expect("valid Aerie courier policy").referral;
        assert!(policy.quest.delivery && policy.quest.goal == 1 && policy.prerequisites.len() == 4);
        assert!(policy.prerequisites.iter().all(|id| crate::hunting_quests::profile(*id).is_some()));
        policy
    })
}
const REPORT_VISUAL: u32 = 0x1f000f1c;
pub fn report() -> u32 { aerie().item }
pub fn delivery_npc(entity:u32)->bool {matches!(entity,KANTEN|ORVEN) || entity==aerie().giver || entity==aerie().recipient}
pub fn accepts_delivery(npc:u32,item:u32)->bool {(npc==ORVEN && item==BAG) || (npc==aerie().recipient && item==report())}
pub fn delivery_reply(npc:u32,earned:u64)->String {
    if npc==KANTEN {"Gambling Debt: deliver Kanten's Bag to Orven in Arwic (44.9N, 7.1W) within two hours.".into()}
    else if npc==ORVEN {"Gambling Debt complete. You receive 1,000 experience.".into()}
    else if npc==aerie().giver {"Take my Aerie Report to Legionary Brigadier Maekarn in Fort Strathelar (51.8S, 39.5E).".into()}
    else {format!("The Aerie Report has reached Fort Strathelar. Journey to Fort Strathelar complete. You receive {earned} experience.")}
}
pub fn delivery_metadata(dat:&mut crate::dat::Dat)->std::io::Result<crate::inventory_request::Metadata> {
    dat.read(0x1f00103f)?;crate::inventory_request::Metadata::load(dat,BAG)
}
impl Book {
    /// Server-derived inventory exchange: no packet-supplied rewards/items.
    pub fn delivery(&self,hunts:&crate::hunting_quests::Book,bag:&crate::inventory::Inventory,npc:u32,level:u16,now:u64,first:u64)
        ->std::io::Result<(Vec<crate::inventory::Command>,Vec<crate::progression::Command>,Self)> {
        use crate::inventory::{Item,Location,Command as I};
        let bad=|s:&str|std::io::Error::other(s);
        let route = aerie();
        if npc == route.giver || npc == route.recipient {
            let q = &route.quest;
            let active = self.0.get(&q.id).filter(|s| s.completed == 0 && now >= s.started && now < s.started.saturating_add(q.limit_seconds));
            let report = bag.items().find(|i| i.definition == route.item && i.quantity == 1 && matches!(i.location, Location::Bag(_)));
            if npc == route.giver {
                if level < q.level || !route.prerequisites.iter().all(|id| hunts.has_completed(*id)) {
                    return Err(bad("Reach level 70 and turn in all four Aerie hunts for Arlen Jenis, Jasper Urleg, Temakhe and Bertrand Jas."));
                }
                if active.is_some() && report.is_some() {
                    return Err(bad("Take the Aerie Report in your backpack to Legionary Brigadier Maekarn in Fort Strathelar."));
                }
                let next = if active.is_some() { self.clone() } else { self.accept(q.id, level, now)? };
                let items = if report.is_some() { Vec::new() } else {
                    let slot = bag.free_slot().ok_or_else(|| bad("Make room in your backpack for the Aerie Report."))?;
                    vec![I::Grant(Item { id: first, definition: route.item, visual: REPORT_VISUAL, quantity: 1,
                        stack_limit: 1, equipment_mask: 0, location: Location::Bag(slot), roll: None })]
                };
                return Ok((items, Vec::new(), next));
            }
            let accepted = active.ok_or_else(|| bad("Speak to Elite Legionary Otonku in the Aerie for a current delivery."))?;
            let report = report.ok_or_else(|| bad("Bring the Aerie Report from Elite Legionary Otonku."))?;
            let xp = q.reward(level);
            let mut next = self.clone();
            next.0.insert(q.id, State { count: 1, completed: now, rewarded_xp: xp, ..accepted.clone() });
            return Ok((vec![I::ConsumeDense { item: report.id }], crate::quest_points::completion_rewards(q.id, xp), next));
        }
        let q=profiles().iter().find(|q|q.id==DEBT).unwrap();
        let owned=bag.items().find(|i|i.definition==BAG&&i.visual==0x1f00103f&&i.quantity==1&&i.stack_limit==1&&matches!(i.location,Location::Bag(_)));
        if npc==KANTEN {
            if self.0.get(&DEBT).is_some_and(|s|s.completed==0&&now>=s.started&&now<s.started.saturating_add(q.limit_seconds)) {
                return Err(bad("Deliver Kanten's Bag to Orven in Arwic before returning for another job."));
            }
            let next=self.accept(DEBT,level,now)?;
            let items=if owned.is_some(){vec![]}else{vec![I::Grant(Item{roll:None,id:first,definition:BAG,visual:0x1f00103f,
                quantity:1,stack_limit:1,equipment_mask:0,location:Location::Bag(bag.free_slot().ok_or_else(||bad("Make room for Kanten's Bag."))?)})]};
            return Ok((items,vec![],next));
        }
        if npc!=ORVEN{return Err(bad("This NPC does not accept Kanten's Bag."));}
        let state=self.0.get(&DEBT).filter(|s|s.completed==0&&now>=s.started&&now<s.started.saturating_add(q.limit_seconds))
            .ok_or_else(||bad("Speak to Kanten for a delivery job. Expired jobs must be renewed there."))?;
        let item=owned.ok_or_else(||bad("Bring Kanten's Bag in your backpack to complete Gambling Debt."))?;
        let mut next=self.clone();next.0.insert(DEBT,State{count:1,completed:now,..state.clone()});
        Ok((vec![I::ConsumeDense{item:item.id}],crate::quest_points::completion_rewards(q.id, q.xp),next))
    }
    fn can_start(&self,q:&Profile,level:u16,now:u64)->bool {
        level>=q.level && self.0.get(&q.id).is_none_or(|s| {
            let q=state_profile(q.id,s).unwrap_or(q);
            if s.completed>0 {
            now>=s.completed.saturating_add(q.repeat_seconds)
        } else {q.limit_seconds != 0 && now>=s.started.saturating_add(q.limit_seconds)} })
    }
    pub fn offers_bounty(&self, npc: u32, level: u16, time: u64, bag_full: bool) -> bool {
        // Kanten first offers his delivery. He can also give work to someone with a full backpack.
        npc_bounty(npc).is_some_and(|b| self.can_start(b.quest, level, time)
            && (npc != KANTEN || bag_full || self.0.contains_key(&DEBT)))
    }
    pub fn accept(&self,id:u32,level:u16,now:u64)->std::io::Result<Self> {
        self.accept_version(id, level, now, HuntRules::SlimelordRevision.for_quest(id))
    }
    pub(crate) fn accept_version(&self, id:u32, level:u16, now:u64, rules:HuntRules)->std::io::Result<Self> {
        let q=rules.catalog().iter().find(|q|q.id==id).ok_or_else(||std::io::Error::other("Unknown recovered quest."))?;
        if now==0||!self.can_start(q,level,now){return Err(std::io::Error::other(format!("{} requires level {} and an available quest timer.",q.title,q.level)));}
        let mut next=self.clone();next.0.insert(id,State{started:now,rules,..Default::default()});Ok(next)
    }
    /// Only authoritative first death of a live encounter may call this.
    /// A starter kill bestows but does not also count toward that same quest.
    pub fn kill(&self,entity:u32,level:u16,now:u64)->(Self,u64) {
        self.kill_version(entity,level,now,HuntRules::SlimelordRevision)
    }
    pub(crate) fn kill_version(&self,entity:u32,level:u16,now:u64,rules:HuntRules)->(Self,u64) {
        let mut next=self.clone();let mut xp=0;
        if matches!(rules, HuntRules::PublishedRewards | HuntRules::SlimelordRevision) {crate::slavetaker::on_kill(&mut next,entity,now);}
        let catalog=rules.catalog();
        for q in catalog.iter().filter(|q|!q.delivery) {
            if q.kill_starters.contains(&entity)&&self.can_start(q,level,now) {
                next.0.insert(q.id,State{started:now,rules:rules.for_quest(q.id),..Default::default()});continue;
            }
            let Some(s)=next.0.get_mut(&q.id) else{continue};
            let q=if s.rules!=rules {state_profile(q.id,s).unwrap_or(q)} else {q};
            if s.completed>0||now<s.started||now>=s.started.saturating_add(q.limit_seconds)||!q.targets.contains(&entity){continue;}
            s.count=(s.count+1).min(q.goal);
            if s.count==q.goal{s.completed=now;s.rewarded_xp=q.reward(level);xp+=s.rewarded_xp;}
        }
        (next,xp)
    }
    /// Several active objectives can finish on one kill. A completed record
    /// left in the journal earns nothing on later kills or repeated dialogue.
    pub(crate) fn settle_completion_points(&self, previous: &Self, state: &mut crate::progression::Progression) -> std::io::Result<()> {
        for (&id, entry) in &self.0 {
            if entry.completed != 0 && previous.0.get(&id).is_none_or(|old| old.completed != entry.completed) {
                if let Some(award) = crate::quest_points::award(id) {
                    *state = state.apply(&award).map_err(std::io::Error::other)?;
                }
            }
        }
        Ok(())
    }
    pub fn guidance(q:&Profile,s:&State)->String {
        if s.completed>0 {format!("{} complete. Earned {} experience. Repeat after {} minutes.",q.title,if s.rewarded_xp>0 {s.rewarded_xp} else {q.xp},q.repeat_seconds/60)}
        else if matches!(q.id, crate::slavetaker::HEAD_QUEST | crate::slavetaker::SEEK) {crate::slavetaker::guidance(q.id,s.count).into()}
        else if q.id == crate::omishan_collection::QUEST {crate::omishan_collection::guidance(s.count).into()}
        else if q.xp_basis_points>0 { format!("{}\n\nProgress: {} / {}. Reward: {}% of a level. Time limit: {} minutes.",q.text,s.count,q.goal,f64::from(q.xp_basis_points)/100.0,q.limit_seconds/60) }
        else {format!("{}\n\nProgress: {} / {}. Reward: {} experience. Time limit: {} minutes.",q.text,s.count,q.goal,q.xp,q.limit_seconds/60)}
    }
}
#[cfg(test)] mod tests {
    use super::*;
    #[test] fn native_starter_target_count_timers_and_once_only_rewards() {
        let q=profiles().iter().find(|q|q.id==0x40000097).unwrap();let start=q.kill_starters[0];let target=q.targets[0];
        let b=Book::default();assert_eq!(b.kill(start,q.level-1,100).0,b);
        let (mut b,xp)=b.kill(start,q.level,100);assert_eq!(xp,0);assert_eq!(b.0[&q.id].count,0);
        assert_eq!(b.kill(0x47000000,150,101).0,b);
        for i in 0..q.goal {let(n,xp)=b.kill(target,150,102+u64::from(i));b=n;assert_eq!(xp,if i+1==q.goal{q.xp}else{0});}
        let finish=b.0[&q.id].completed;assert_eq!(b.kill(target,150,finish+1).1,0);
        assert_eq!(b.kill(start,150,finish+1).0,b);
        assert_eq!(b.kill(start,150,finish+q.repeat_seconds).0.0[&q.id].count,0);
        let expired=Book::default().kill(start,150,100).0;
        assert_eq!(expired.kill(target,150,100+q.limit_seconds).0,expired);
    }
    #[test]
    fn a_completed_kill_quest_grants_points_once_per_eligible_run() {
        let q = profiles().iter().find(|q| q.id == 0x40000009).unwrap();
        let mut balance = crate::progression::Progression::default();
        let mut book = Book::default();
        let mut start = 100;
        for run in 1..=2 {
            book = book.accept(q.id, q.level, start).unwrap();
            for i in 0..q.goal {
                let (next, _) = book.kill(q.targets[0], q.level, start + 1 + u64::from(i));
                next.settle_completion_points(&book, &mut balance).unwrap();
                book = next;
                assert_eq!(balance.quest_points, 15 * if i+1 == q.goal {run} else {run-1});
            }
            let finish = book.0[&q.id].completed;
            let (unchanged, _) = book.kill(q.targets[0], q.level, finish + 1);
            unchanged.settle_completion_points(&book, &mut balance).unwrap();
            assert_eq!(balance.quest_points, 15 * run);
            start = finish + q.repeat_seconds;
        }
    }
    #[test]
    fn starter_hunt_requirements_timers_and_native_targets_match_the_guides() {
        let empty = Book::default();
        for (elixir, id, level, xp, points) in [
            (0x47000f42, 0x40000009, 2, 100, 15),
            (0x47000f43, 0x4000000a, 3, 190, 30),
            (0x47000f40, 0x4000000b, 3, 150, 25),
        ] {
            let q = use_profile(elixir).unwrap();
            assert_eq!((q.id, q.level, q.goal, q.xp), (id, level, 10, xp));
            assert_eq!((q.limit_seconds, q.repeat_seconds), (10800, 86400));
            assert_eq!(crate::quest_points::amount(id), Some(points));
            assert!(empty.accept(id, level - 1, 100).is_err());
            let mut book = empty.accept(id, level, 100).unwrap();
            assert_eq!(book.kill(0x47000000, level, 101).0, book);
            for i in 0..10 {
                let (next, reward) = book.kill(q.targets[i % q.targets.len()], level, 101 + i as u64);
                assert_eq!(reward, if i == 9 { xp } else { 0 });
                book = next;
            }
            assert!(!book.can_start(q, level, 110 + 86399));
            assert!(book.can_start(q, level, 110 + 86400));
        }
        let head = npc_bounty(0x470013b6).unwrap();
        assert_eq!(head.quest.id, 0x4000000b);
        assert!(empty.offers_bounty(head.npc, 3, 100, false));
        let elixir_first = empty.accept(use_profile(0x47000f40).unwrap().id, 3, 100).unwrap();
        assert!(!elixir_first.offers_bounty(head.npc, 3, 101, false));
        let head_first = empty.accept(head.quest.id, 3, 100).unwrap();
        assert!(head_first.accept(use_profile(0x47000f40).unwrap().id, 3, 101).is_err());
    }
    #[test] fn elixirs_require_acceptance_and_do_not_grant_kill_credit() {
        let q=use_profile(0x47000f43).unwrap();let b=Book::default();
        assert_eq!(b.kill(q.targets[0],150,100).0,b);
        let b=b.accept(q.id,150,100).unwrap();assert!(b.accept(q.id,150,101).is_err());
        assert_eq!(b.kill(q.targets[0],150,101).0.0[&q.id].count,1);
    }
    #[test]
    fn slimelord_replays_old_acceptance_and_uses_the_published_deadline_for_new_hunts() {
        let id = 0x40000106;
        let guardian = 0x47001688;
        let keerik = 0x47001682;
        let start = 100;
        let book = Book::default();
        assert!(!book.kill(guardian, 37, start).0.0.contains_key(&id));
        assert!(book.kill(keerik, 38, start).0.0.is_empty());
        for (rules, level, duration, reward) in [
            (HuntRules::PublishedRewards, 36, 3600, 780),
            (HuntRules::SlimelordRevision, 38, 7200, 27000),
        ] {
            let (accepted, _) = book.kill_version(guardian, level, start, rules);
            assert_eq!(state_profile(id, &accepted.0[&id]).unwrap().limit_seconds, duration);
            let expired = accepted.kill(keerik, 38, start + duration).0;
            assert_eq!(expired.0[&id].completed, 0);
            let (finished, xp) = accepted.kill(keerik, 38, start + duration - 1);
            assert_eq!((xp, finished.0[&id].rewarded_xp), (reward, reward));
            assert_eq!(finished.kill(keerik, 38, start + duration).1, 0);
        }
    }
    #[test]
    fn incursion_preserves_accepted_reward_across_replay_and_uses_published_xp_on_repeat() {
        let q = profiles().iter().find(|q| q.id == 0x40000095).unwrap();
        assert_eq!(q.xp, 400);
        let (mut book, _) = Book::default().kill_version(q.kill_starters[0], 30, 1, HuntRules::Hero);
        assert_eq!(state_profile(q.id, &book.0[&q.id]).unwrap().xp, 3400);
        for i in 0..q.goal {
            book = book.kill(q.targets[0], 30, 2 + u64::from(i)).0;
        }
        let finish = book.0[&q.id].completed;
        assert_eq!(book.0[&q.id].rewarded_xp, 3400);
        let restart = finish + q.repeat_seconds;
        book = book.kill(q.kill_starters[0], 30, restart).0;
        for i in 0..q.goal {
            book = book.kill(q.targets[0], 30, restart + 1 + u64::from(i)).0;
        }
        assert_eq!(book.0[&q.id].rewarded_xp, 400);
    }
    #[test]
    fn existing_kills_keep_original_rewards_and_new_raids_use_the_current_level() {
        let fang=profiles().iter().find(|q|q.id==0x40000110).unwrap();
        let original=original_profiles().iter().find(|q|q.id==fang.id).unwrap();
        assert_ne!(fang.xp,original.xp);
        for (legacy,expected) in [(HuntRules::Original,original.xp),(HuntRules::Hero,fang.xp)] {
            let (book,_)=Book::default().kill_version(fang.kill_starters[0],50,100,legacy);
            let (book,xp)=book.kill(fang.targets[0],50,101);
            assert_eq!(xp,expected);
            assert_eq!(book.0[&fang.id].rewarded_xp,expected);
        }
        let raid=profiles().iter().find(|q|q.id==0x40000214).unwrap();
        let (legacy,_)=Book::default().kill_version(raid.kill_starters[0],50,100,HuntRules::Original);
        assert!(!legacy.0.contains_key(&raid.id));
        let (mut book,_)=Book::default().kill(raid.kill_starters[0],50,100);
        for i in 0..raid.goal {book=book.kill(raid.targets[0],50,101+u64::from(i)).0;}
        assert_eq!(book.0[&raid.id].rewarded_xp,crate::progression::quest_level_span(50)*3/100);
        assert_eq!(book.kill(raid.targets[0],50,201).1,0);
    }
    #[test] fn all_inferred_quests_encode_in_native_journal_and_finish_once() {
        let mut journal=crate::quests::Book::default();
        for q in profiles().iter().filter(|q|!q.delivery) {
            let mut book=Book::default().accept(q.id,150,100).unwrap();
            for i in 0..q.goal {
                // Accept explicitly isolates progression from other quest starters.
                let (next,xp)=book.kill(q.targets[0],150,101+u64::from(i));book=next;
                assert_eq!(book.0[&q.id].count,i+1,"{}",q.title);
                assert_eq!(xp,if i+1==q.goal{q.reward(150)}else{0},"{}",q.title);
            }
            journal.recovered.0.insert(q.id,book.0[&q.id].clone());
        }
        assert!(journal.list_graph().unwrap().len()>1000);
        assert!(journal.list_graph().unwrap().len()<48*1024,"Leave room for native UI_Load inventory/skill fields");
        assert_eq!(journal.updates().unwrap().len(),profiles().iter().filter(|q|!q.delivery).count());
    }
}
