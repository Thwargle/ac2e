//! Immutable `wn1` quest policy: recovered assets/stages, authored transactions.
use crate::{inventory::{Command, Inventory, Item, Location}, quests::Book};
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, io, sync::OnceLock};

pub const QUEST: u32 = 0x400000c4;
pub const REFERRAL: u32 = 0x400000c5;
pub const SCOUT: u32 = 0x470015cc;
pub const CAPTAIN: u32 = 0x470015c8;
pub const BANDIT: u32 = 0x470015be;
pub const NOTEBOOK: u32 = 0x470015b1;
pub const REWARDS: [u32; 3] = [0x4700156d, 0x470010b9, 0x470010a2];
pub const LIMIT: u64 = 86400;
pub const REPEAT: u64 = 72000;
pub const REFERRAL_LIMIT: u64 = 172800;
fn bad(s: &str) -> io::Error { io::Error::other(s) }

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct State {
    pub phase: u32, // 1 hunt, 2 take notebook, 3 return notebook, 4 complete
    pub started: u64,
    pub completed: u64,
    pub last_action: u64,
    pub captain_started: u64,
    pub captain_completed: u64,
}
#[derive(Deserialize)]
struct Asset { id: u32, visual: u32, physical: u32, quality_hash: String }
#[derive(Deserialize)]
struct Content { version: u32, items: Vec<Asset> }
fn content() -> &'static Content {
    static C: OnceLock<Content> = OnceLock::new();
    C.get_or_init(|| {
        let c: Content = serde_json::from_str(include_str!("../content/warder-notebook-v1.json")).expect("Notebook content");
        assert_eq!(c.version, 1); assert_eq!(c.items.len(), 4); c
    })
}
pub fn validate(dat: &mut crate::dat::Dat) -> io::Result<BTreeMap<u32, crate::inventory_request::Metadata>> {
    let mut result = BTreeMap::new();
    for a in &content().items {
        let raw = dat.read(0x81000000 | a.id & 0xffffff)?;
        let hash = raw.iter().fold(0xcbf29ce484222325u64, |h,b| (h ^ u64::from(*b)).wrapping_mul(0x100000001b3));
        if format!("{hash:016x}") != a.quality_hash { return Err(bad("Notebook DAT profile changed")); }
        for did in [a.id, a.visual, a.physical] { dat.read(did)?; }
        result.insert(a.id, crate::inventory_request::Metadata { implement: None, icon: 0, physical: a.physical, equipment: None });
    }
    Ok(result)
}
pub fn item(definition: u32, id: u64, slot: u16) -> io::Result<Item> {
    let a = content().items.iter().find(|a| a.id==definition).ok_or_else(|| bad("Unknown Notebook item"))?;
    Ok(Item { id, definition, visual: a.visual, quantity: 1, stack_limit: if definition==REWARDS[0] {10} else {1},
              equipment_mask: 0, location: Location::Bag(slot), roll: None })
}
impl State {
    pub fn active(&self, time: u64) -> bool {
        (1..=3).contains(&self.phase) && time>=self.started && time-self.started<LIMIT
    }
    pub fn killed(&self, entity: u32, time: u64) -> Self {
        let mut next = self.clone();
        if entity==BANDIT && self.phase==1 && self.active(time) && time>=self.last_action {
            next.phase=2; next.last_action=time;
        }
        next
    }
    pub fn can_loot(&self, bag: &Inventory, time: u64) -> bool {
        self.active(time) && self.phase>=2 && !bag.items().any(|i| i.definition==NOTEBOOK)
    }
    pub fn guidance(&self) -> String {
        match self.phase {
            1 => "Recover my notebook from the Drudge Slinker Bandit at the camp near 15.5N, 1.7W. You have 24 hours to return it.".into(),
            2 => "Take the Warder Scout's Notebook from the defeated Drudge Slinker Bandit. If the corpse is gone, defeat another Bandit.".into(),
            3 => "Bring the notebook to the Warder Scout near Molwirth (14.0N, 0.6E). Select him and use the notebook from your backpack, or speak to him to hand it over. If you lost it, defeat another Bandit.".into(),
            4 => "Notebook returned: 800 experience, Emblem of Order, Mad Crone Glyph and Prosper Glyph. Seek the Warder Captain in the Warder's Hideout (20.6N, 14.0E). You may repeat the notebook task after 20 hours.".into(),
            _ => "Reach level 10 and speak to the Warder Scout near Molwirth (14.0N, 0.6E).".into(),
        }
    }
    pub fn entries(&self) -> Vec<(u32, &'static str, String, u32, u64, u64, u64)> {
        let mut entries=Vec::new();
        if self.phase>0 { entries.push((QUEST,"The Warder Scout's Notebook",self.guidance(),self.phase,self.started,self.completed,LIMIT)); }
        if self.captain_started>0 {
            entries.push((REFERRAL,"Seek the Warder Captain",if self.captain_completed>0 {
                "You found the Warder Captain. Earned 2,500 experience. Drudge Potion Raid is not playable yet.".into()
            } else { "Seek the Warder Captain inside the Warder's Hideout at 20.6N, 14.0E within 48 hours. Stay left through the Lasher caves and take the circular ramp up.".into() },
                1,self.captain_started,self.captain_completed,REFERRAL_LIMIT));
        }
        entries
    }
}
#[derive(Clone, Copy, Debug, Serialize, Deserialize)]
pub enum Action { Talk, Take, Return(u64), Captain }
impl Action {
    pub fn encode(self) -> String { serde_json::to_string(&self).unwrap() }
    pub fn decode(s: &str) -> io::Result<Self> { serde_json::from_str(s).map_err(io::Error::other) }
}
pub struct Plan { pub items: Vec<Command>, pub rewards: Vec<crate::progression::Command>, pub quests: Book, pub text: String }
pub fn plan(book: &Book, bag: &Inventory, level: u16, action: Action, first: u64, time: u64) -> io::Result<Plan> {
    if time==0 || time<book.notebook.last_action || !bag.dense_bags() { return Err(bad("Invalid notebook clock or inventory")); }
    let mut next=book.clone(); let mut bag=bag.clone(); let mut items=Vec::new(); let mut rewards=Vec::new();
    let mut speech=String::new(); let mut id=first;
    let apply=|bag:&mut Inventory,items:&mut Vec<Command>,c:Command| -> io::Result<()> {
        *bag=bag.apply(&c).map_err(io::Error::other)?; items.push(c); Ok(())
    };
    let mut grant=|bag:&mut Inventory,items:&mut Vec<Command>,definition| -> io::Result<()> {
        let slot=bag.free_slot().ok_or_else(||bad("Make room for the notebook rewards: two free backpack slots in addition to the notebook."))?;
        apply(bag,items,Command::Grant(item(definition,id,slot)?))?;
        id=id.checked_add(1).ok_or_else(||bad("Notebook identity overflow"))?; Ok(())
    };
    // The incoming introduction and the notebook start/hand-in are one transaction.
    if matches!(action,Action::Talk) {
        let referral=next.warder.converse(SCOUT,level,next.esper.completed>0,time)?;
        next.warder=referral.book; speech=referral.text;
        rewards.extend(referral.rewards);
    }
    let action=if matches!(action,Action::Talk) && next.notebook.active(time) && next.notebook.phase==3 {
        crate::arwic_mines::owned(&bag,NOTEBOOK).map(Action::Return).unwrap_or(action)
    } else { action };
    match action {
        Action::Talk => {
            if level<10 { speech.push_str(" Reach level 10 to help recover my notebook."); }
            else if !next.notebook.active(time) && (next.notebook.completed==0 || time-next.notebook.completed>=REPEAT) {
                let stale=bag.items().filter(|i|i.definition==NOTEBOOK).map(|i|i.id).collect::<Vec<_>>();
                for item in stale { apply(&mut bag,&mut items,Command::ConsumeDense{item})?; }
                next.notebook.phase=1;next.notebook.started=time;next.notebook.completed=0;
            }
            // An expired Captain referral can be renewed without discarding a completed notebook.
            if next.notebook.captain_started>0 && next.notebook.captain_completed==0 && time>=next.notebook.captain_started.saturating_add(REFERRAL_LIMIT) {
                next.notebook.captain_started=time;
            }
        }
        Action::Take => {
            if !next.notebook.can_loot(&bag,time) { return Err(bad("Accept an active notebook task and defeat the Bandit before taking its notebook. You only need one.")); }
            grant(&mut bag,&mut items,NOTEBOOK)?; next.notebook.phase=3;
        }
        Action::Return(item) => {
            if !next.notebook.active(time) || next.notebook.phase!=3 { return Err(bad("This notebook task is not ready for hand-in. Speak to the Scout to renew an expired task.")); }
            if !bag.items().any(|i|i.id==item && i.definition==NOTEBOOK && matches!(i.location,Location::Bag(_))) {
                return Err(bad("Give the Warder Scout his notebook from your backpack."));
            }
            apply(&mut bag,&mut items,Command::ConsumeDense{item})?;
            for definition in REWARDS { grant(&mut bag,&mut items,definition)?; }
            rewards.push(crate::progression::Command::AwardXp(800));
            rewards.extend(crate::quest_points::award(QUEST));
            next.notebook.phase=4;next.notebook.completed=time;
            if next.notebook.captain_started==0 || next.notebook.captain_completed>0 && time.saturating_sub(next.notebook.captain_completed)>=REPEAT
                || next.notebook.captain_completed==0 && time.saturating_sub(next.notebook.captain_started)>=REFERRAL_LIMIT {
                next.notebook.captain_started=time;next.notebook.captain_completed=0;
            }
            speech.push_str(" Thank you for recovering my notebook! Take these rewards, and seek my Captain in the Warder's Hideout.");
        }
        Action::Captain => {
            if next.notebook.captain_started==0 { speech="Help the Warder Scout recover his notebook before seeking this introduction.".into(); }
            else if next.notebook.captain_completed==0 {
                if time>=next.notebook.captain_started.saturating_add(REFERRAL_LIMIT) {
                    speech="Your introduction has expired. Speak to the Warder Scout to renew it.".into();
                } else {
                    next.notebook.captain_completed=time;rewards.extend(crate::quest_points::completion_rewards(REFERRAL,2500));
                    speech="You found the Warder Captain. Earned 2,500 experience. Drudge Potion Raid is not playable yet.".into();
                }
            } else { speech="Your introduction is complete. Drudge Potion Raid is not playable yet.".into(); }
        }
    }
    if !matches!(action,Action::Captain) { speech.push(' ');speech.push_str(&next.notebook.guidance()); }
    if next!=*book || !items.is_empty() { next.notebook.last_action=time; }
    Ok(Plan{items,rewards,quests:next,text:speech.trim().into()})
}
