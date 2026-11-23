//! Starter route from Arwic through Kehan to Esper. Immutable `w1` journal policy.
//! Native map/gear/glyph and journal identities; reconstructed server transitions.
use crate::{
    inventory::{Command, Inventory, Item, Location},
    quests::Book,
};
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, io, sync::OnceLock};
pub const QUEST: u32 = 0x400000c7;
pub const FIND_MILLER: u32 = 0x400000c2;
pub const FIND_WARDER: u32 = 0x400000cb;
pub const MILLER: u32 = 0x470015c4;
pub const WARDER: u32 = 0x470015c2;
pub const WINDMILL: u32 = 0x470015ce;
pub const MAP: u32 = 0x47002fa8;
pub const GEAR: u32 = 0x470015b0;
pub const GLYPH: u32 = 0x470010a0;
pub const STONE: u32 = 0x470010f4;
pub const LIMIT: u64 = 86400;
pub const REPEAT: u64 = 72000;
// Renewed Focus, stat 0x40108: additive field 36 magnitude and field 0 duration.
pub const RENEWED_FOCUS_VIGOR: u32 = 40;
const RENEWED_FOCUS_SECONDS: u64 = 10800;

pub fn renewed_focus_effect() -> u32 {
    if crate::data_profile::unconverted_7230() { 0x6f001b1f } else { 0x6f00022a }
}
fn bad(s: &str) -> io::Error {
    io::Error::other(s)
}
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct State {
    pub phase: u32,
    pub started: u64,
    pub completed: u64,
    pub last_action: u64,
    pub miller_started: u64,
    pub miller_completed: u64,
    pub warder_started: u64,
    pub warder_completed: u64,
}
#[derive(Deserialize)]
pub struct Asset {
    pub id: u32,
    pub name: String,
    pub visual: u32,
    pub physical: u32,
    quality_hash: String,
}
#[derive(Deserialize)]
pub struct Content {
    pub items: Vec<Asset>,
    pub map_location: [f64; 4],
    pub map_radius: f32,
}
pub fn content() -> &'static Content {
    static C: OnceLock<Content> = OnceLock::new();
    C.get_or_init(|| {
        serde_json::from_str(include_str!("../content/windmill-v1.json")).expect("windmill policy")
    })
}
pub fn validate(
    dat: &mut crate::dat::Dat,
) -> io::Result<BTreeMap<u32, crate::inventory_request::Metadata>> {
    let mut result = BTreeMap::new();
    for a in &content().items {
        let raw = dat.read(0x81000000 | (a.id & 0xffffff))?;
        let h = raw.iter().fold(0xcbf29ce484222325u64, |h, b| {
            (h ^ u64::from(*b)).wrapping_mul(0x100000001b3)
        });
        if format!("{h:016x}") != a.quality_hash {
            return Err(bad("Windmill DAT profile changed"));
        }
        for did in [a.id, a.visual, a.physical] {
            dat.read(did)?;
        }
        result.insert(
            a.id,
            crate::inventory_request::Metadata {
                implement: None,
                icon: 0,
                physical: a.physical,
                equipment: None,
            },
        );
    }
    Ok(result)
}
pub fn item(definition: u32, id: u64, slot: u16) -> io::Result<Item> {
    let a = content()
        .items
        .iter()
        .find(|a| a.id == definition)
        .ok_or_else(|| bad("Unknown windmill item"))?;
    Ok(Item {
        id,
        definition,
        visual: a.visual,
        quantity: 1,
        stack_limit: 1,
        equipment_mask: 0,
        location: Location::Bag(slot),
        roll: None,
    })
}
pub fn map_target() -> [i32; 2] {
    let p = content().map_location;
    let cell = p[0] as u32;
    [
        ((cell >> 24) * 160) as i32 + p[1] as i32,
        (((cell >> 16) & 255) * 160) as i32 + p[2] as i32,
    ]
}
pub fn survey_map() -> &'static crate::harvesting::Map {
    static M: OnceLock<crate::harvesting::Map> = OnceLock::new();
    M.get_or_init(|| {
        let p = map_target();
        crate::harvesting::Map {
            id: MAP,
            region: 0,
            mine: 0x47002c6b,
            bounds: [p[0], p[1], p[0], p[1]],
            radius: content().map_radius as u32,
        }
    })
}
impl State {
    pub fn renewed_focus(&self, now: u64) -> Option<crate::effects::Lifetime> {
        crate::effects::Lifetime::from_quest(self.completed, RENEWED_FOCUS_SECONDS, now)
    }
    pub fn active(&self, time: u64) -> bool {
        (1..=4).contains(&self.phase) && time >= self.started && time - self.started < LIMIT
    }
    pub fn needs_referral(&self, time: u64, arwic_completed: u64) -> bool {
        self.miller_started == 0
            || (self.miller_completed == 0 && time.saturating_sub(self.miller_started) >= 172800)
            || (self.miller_completed > 0
                && arwic_completed > self.miller_started
                && time.saturating_sub(self.miller_completed) >= REPEAT)
    }
    pub fn guidance(&self) -> &'static str {
        match self.phase {
            1 => {
                "Use The Miller's Map south of Kehan to find a Stone Quarry. Craft a Rock Hammer under Starter Tools, harvest Stone, then select the Miller and use one Stone from your bag. You have 24 hours from acceptance."
            }
            2 => {
                "Harvest the quarry with a Rock Hammer. Return to the Miller of Kehan and use one Stone from your bag while she is selected."
            }
            3 => {
                "Take the Massive Gear to the Windmill north of Kehan, near 47.0N, 11.5E. Select the Windmill and use the gear from your bag."
            }
            4 => "You repaired the Windmill. Return to the Miller of Kehan for your reward.",
            5 => {
                "You earned 200 experience, 150 quest points, a Lost Wish Glyph and Renewed Focus. Your maximum vigor increases by 40 for three hours. Speak to the Esper Warder in Millstone at 37.5N, 19.9W. The windmill task can be repeated after 20 hours."
            }
            _ => "Speak to the Miller of Kehan in Kehan at 42.5N, 11.2E.",
        }
    }
    pub fn entries(&self) -> Vec<(u32, &'static str, String, u32, u64, u64, u64)> {
        let mut entries = Vec::new();
        if self.miller_started > 0 {
            entries.push((
                FIND_MILLER,
                "Find the Miller of Kehan",
                if self.miller_completed > 0 {
                    "You found the Miller of Kehan. Earned 30 experience.".into()
                } else {
                    "Speak to the Miller of Kehan at 42.5N, 11.2E within 48 hours.".into()
                },
                1,
                self.miller_started,
                self.miller_completed,
                172800,
            ));
        }
        if self.phase > 0 {
            entries.push((
                QUEST,
                "Kehan Windmill Quest",
                self.guidance().into(),
                self.phase,
                self.started,
                self.completed,
                LIMIT,
            ));
        }
        if self.warder_started > 0 {
            entries.push((
                FIND_WARDER,
                "Find the Esper Reaches Warder",
                if self.warder_completed > 0 {
                    "You found the Esper Warder. Ask about the Drudge Outcast Camp.".into()
                } else {
                    "Speak to the Esper Warder in Millstone at 37.5N, 19.9W.".into()
                },
                1,
                self.warder_started,
                self.warder_completed,
                0,
            ));
        }
        entries
    }
}
#[derive(Clone, Copy, Debug, Serialize, Deserialize)]
pub enum Action {
    Refer,
    Talk,
    Stone(u64),
    Gear(u64),
    Locate { item: u64, point: [u32; 4] },
    Warder,
}
impl Action {
    pub fn encode(self) -> String {
        serde_json::to_string(&self).unwrap()
    }
    pub fn decode(s: &str) -> io::Result<Self> {
        serde_json::from_str(s).map_err(io::Error::other)
    }
}
pub struct Plan {
    pub items: Vec<Command>,
    pub rewards: Vec<crate::progression::Command>,
    pub quests: Book,
    pub professions: crate::professions::Book,
}
fn owned(bag: &Inventory, definition: u32) -> Option<&Item> {
    bag.items()
        .find(|i| i.definition == definition && matches!(i.location, Location::Bag(_)))
}
fn grant(
    bag: &mut Inventory,
    commands: &mut Vec<Command>,
    definition: u32,
    id: u64,
) -> io::Result<()> {
    let slot = bag
        .free_slot()
        .ok_or_else(|| bad("Make room in your backpack for the quest item."))?;
    let c = Command::Grant(item(definition, id, slot)?);
    *bag = bag.apply(&c).map_err(io::Error::other)?;
    commands.push(c);
    Ok(())
}
fn consume(
    bag: &mut Inventory,
    commands: &mut Vec<Command>,
    id: u64,
    definition: u32,
) -> io::Result<Item> {
    let i = bag
        .items()
        .find(|i| {
            i.id == id && i.definition == definition && matches!(i.location, Location::Bag(_))
        })
        .cloned()
        .ok_or_else(|| bad("The required quest item must be in your backpack."))?;
    let c = Command::ConsumeStackDense {
        item: id,
        quantity: i.quantity,
    };
    *bag = bag.apply(&c).map_err(io::Error::other)?;
    commands.push(c);
    Ok(i)
}
pub fn plan(
    book: &Book,
    inventory: &Inventory,
    professions: &crate::professions::Book,
    action: Action,
    first: u64,
    time: u64,
) -> io::Result<Plan> {
    if time == 0 || time < book.windmill.last_action || !inventory.dense_bags() {
        return Err(bad("Invalid windmill state or clock"));
    }
    let mut next = book.clone();
    let mut bag = inventory.clone();
    let mut jobs = professions.clone();
    let mut items = vec![];
    let mut rewards = vec![];
    let s = &mut next.windmill;
    match action {
        Action::Refer => {
            if book.arwic.phase != 5 {
                return Err(bad("Complete the Arwic Mines lesson first."));
            }
            if s.needs_referral(time, book.arwic.completed) {
                s.miller_started = time;
                s.miller_completed = 0;
            }
        }
        Action::Talk => {
            if s.miller_started > 0 && s.miller_completed == 0 && time - s.miller_started < 172800 {
                s.miller_completed = time;
                rewards.extend(crate::quest_points::completion_rewards(FIND_MILLER, 30));
            }
            if s.phase == 0
                || (s.phase == 5 && time >= s.completed + REPEAT)
                || ((1..=4).contains(&s.phase) && !s.active(time))
            {
                // Retire only this player's old maps, deposits and unspent gear.
                let old: Vec<_> = bag
                    .items()
                    .filter(|i| {
                        matches!(i.definition, MAP | GEAR) && matches!(i.location, Location::Bag(_))
                    })
                    .map(|i| (i.id, i.definition))
                    .collect();
                for (id, d) in old {
                    consume(&mut bag, &mut items, id, d)?;
                }
                jobs.1.jobs.retain(|_, j| j.definition != MAP);
                if jobs.1.jobs.len() >= 12 {
                    return Err(bad("Finish an existing survey before accepting this map."));
                }
                grant(&mut bag, &mut items, MAP, first)?;
                jobs.1.jobs.insert(
                    first,
                    crate::harvesting::Job {
                        definition: MAP,
                        target: map_target(),
                        point: None,
                        remaining: 3,
                        expires: 0,
                    },
                );
                s.phase = 1;
                s.started = time;
                s.completed = 0;
            } else if s.phase == 4 {
                grant(&mut bag, &mut items, GLYPH, first)?;
                rewards.push(crate::progression::Command::AwardXp(200));
                rewards.extend(crate::quest_points::award(QUEST));
                s.phase = 5;
                s.completed = time;
                if s.warder_started == 0 {
                    s.warder_started = time;
                }
            } else if (s.phase == 1 && owned(&bag, MAP).is_none())
                || (s.phase == 2
                    && owned(&bag, STONE).is_none()
                    && !jobs
                        .1
                        .jobs
                        .values()
                        .any(|j| j.definition == MAP && j.expires > time && j.remaining > 0))
            {
                // Recover an accidentally sold/destroyed unused map, without duplicating a deposit.
                jobs.1.jobs.retain(|_, j| j.definition != MAP);
                if jobs.1.jobs.len() >= 12 {
                    return Err(bad(
                        "Finish an existing survey before requesting another map.",
                    ));
                }
                grant(&mut bag, &mut items, MAP, first)?;
                jobs.1.jobs.insert(
                    first,
                    crate::harvesting::Job {
                        definition: MAP,
                        target: map_target(),
                        point: None,
                        remaining: 3,
                        expires: 0,
                    },
                );
                s.phase = 1;
            } else if s.phase == 3 && owned(&bag, GEAR).is_none() {
                grant(&mut bag, &mut items, GEAR, first)?;
            }
        }
        Action::Locate { item, point } => {
            if !s.active(time) || s.phase != 1 {
                return Err(bad(
                    "Ask the Miller for a current windmill task before surveying.",
                ));
            }
            consume(&mut bag, &mut items, item, MAP)?;
            let cooldown = jobs.1.cooldown;
            jobs.1
                .apply(&crate::harvesting::Activity::Locate { item, point }, time)?;
            // A quest map must not clear a pending profession survey cooldown.
            jobs.1.cooldown = cooldown;
            s.phase = 2;
        }
        Action::Stone(id) => {
            if !s.active(time) || !matches!(s.phase, 1 | 2) {
                return Err(bad(
                    "The Miller is not waiting for Stone. Speak to her to begin or renew the task.",
                ));
            }
            let stone = consume(&mut bag, &mut items, id, STONE)?;
            let mut next_id = first;
            if stone.quantity > 1 {
                let mut remainder = stone;
                remainder.quantity -= 1;
                remainder.id = next_id;
                remainder.location =
                    Location::Bag(bag.free_slot().ok_or_else(|| bad("Backpack is full."))?);
                let c = Command::Grant(remainder);
                bag = bag.apply(&c).map_err(io::Error::other)?;
                items.push(c);
                next_id = next_id
                    .checked_add(1)
                    .ok_or_else(|| bad("Item identity overflow"))?;
            }
            grant(&mut bag, &mut items, GEAR, next_id)?;
            s.phase = 3;
        }
        Action::Gear(id) => {
            if !s.active(time) || s.phase != 3 {
                return Err(bad(
                    "Bring the Miller's replacement gear before repairing this Windmill.",
                ));
            }
            consume(&mut bag, &mut items, id, GEAR)?;
            s.phase = 4;
        }
        Action::Warder => {
            if s.warder_started == 0 {
                return Err(bad(
                    "The Miller of Kehan will introduce you after you repair her Windmill.",
                ));
            }
            if s.warder_completed == 0 {
                s.warder_completed = time;
                rewards.extend(crate::quest_points::award(FIND_WARDER));
            }
        }
    }
    if next.windmill != book.windmill || !items.is_empty() {
        next.windmill.last_action = time;
    }
    Ok(Plan {
        items,
        rewards,
        quests: next,
        professions: jobs,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::inventory::{BAG_SLOTS, ITEM_NAMESPACE};
    fn run(
        q: &mut Book,
        b: &mut Inventory,
        p: &mut crate::professions::Book,
        a: Action,
        t: u64,
    ) -> io::Result<Vec<crate::progression::Command>> {
        let out = plan(q, b, p, a, b.high_water() + 1, t)?;
        for c in out.items {
            *b = b.apply(&c).unwrap();
        }
        *q = out.quests;
        *p = out.professions;
        Ok(out.rewards)
    }
    fn stone(id: u64, slot: u16, quantity: u16) -> Item {
        Item {
            id,
            definition: STONE,
            visual: 0x1f000434,
            quantity,
            stack_limit: u16::MAX,
            equipment_mask: 0,
            location: Location::Bag(slot),
            roll: None,
        }
    }
    #[test]
    fn starter_route_requires_explicit_items_and_pays_once() {
        let (mut q, mut b, mut p) = (
            Book::default(),
            Inventory::default(),
            crate::professions::Book::default(),
        );
        assert!(run(&mut q, &mut b, &mut p, Action::Refer, 100).is_err());
        q.arwic.phase = 5;
        run(&mut q, &mut b, &mut p, Action::Refer, 100).unwrap();
        assert!(matches!(
            run(&mut q, &mut b, &mut p, Action::Talk, 101)
                .unwrap()
                .as_slice(),
            [crate::progression::Command::AwardXp(30)]
        ));
        let map = owned(&b, MAP).unwrap().id;
        assert_eq!(p.1.jobs[&map].target, map_target());
        run(&mut q, &mut b, &mut p, Action::Talk, 102).unwrap();
        assert_eq!(b.items().count(), 1);
        let c = content().map_location;
        let point = [
            c[0] as u32,
            (c[1] as f32).to_bits(),
            (c[2] as f32).to_bits(),
            (c[3] as f32).to_bits(),
        ];
        p.1.cooldown = [900, 901, 902];
        assert!(
            run(
                &mut q,
                &mut b,
                &mut p,
                Action::Locate {
                    item: map + 1,
                    point
                },
                103
            )
            .is_err()
        );
        run(
            &mut q,
            &mut b,
            &mut p,
            Action::Locate { item: map, point },
            103,
        )
        .unwrap();
        assert_eq!(p.1.cooldown, [900, 901, 902]);
        assert_eq!(q.windmill.phase, 2);
        assert!(p.1.jobs[&map].point.is_some());
        let id = b.high_water() + 1;
        b = b.apply(&Command::Grant(stone(id, 0, 28))).unwrap();
        run(&mut q, &mut b, &mut p, Action::Talk, 104).unwrap();
        assert_eq!(owned(&b, STONE).unwrap().quantity, 28);
        run(&mut q, &mut b, &mut p, Action::Stone(id), 105).unwrap();
        assert_eq!(owned(&b, STONE).unwrap().quantity, 27);
        assert!(run(&mut q, &mut b, &mut p, Action::Stone(id), 106).is_err());
        let gear = owned(&b, GEAR).unwrap().id;
        run(&mut q, &mut b, &mut p, Action::Gear(gear), 106).unwrap();
        assert!(q.windmill.renewed_focus(106).is_none());
        assert!(matches!(
            run(&mut q, &mut b, &mut p, Action::Talk, 107)
                .unwrap()
                .as_slice(),
            [crate::progression::Command::AwardXp(200), crate::progression::Command::AwardQuestPoints(150)]
        ));
        assert_eq!(q.windmill.phase, 5);
        assert!(owned(&b, GLYPH).is_some());
        assert!(
            run(&mut q, &mut b, &mut p, Action::Talk, 108)
                .unwrap()
                .is_empty()
        );
        run(&mut q, &mut b, &mut p, Action::Warder, 109).unwrap();
        let expected = crate::effects::Lifetime { start: 107_000, until: 10_907_000 };
        assert_eq!(q.windmill.renewed_focus(107), Some(expected));
        assert_eq!(q.windmill.renewed_focus(10_906), Some(expected));
        assert_eq!(q.windmill.renewed_focus(10_907), None);
        assert_eq!(q.windmill.warder_completed, 109);
        run(&mut q, &mut b, &mut p, Action::Warder, 110).unwrap();
        assert_eq!(q.windmill.warder_completed, 109);
        assert_eq!(q.windmill.entries().len(), 3);
        q.list_graph().unwrap();
        q.updates().unwrap();
        run(&mut q, &mut b, &mut p, Action::Talk, 107 + REPEAT).unwrap();
        assert_eq!(q.windmill.phase, 1);
    }
    #[test]
    fn expiration_and_full_bag_never_consume_stone_or_advance() {
        let (mut q, mut b, mut p) = (
            Book::default(),
            Inventory::default(),
            crate::professions::Book::default(),
        );
        run(&mut q, &mut b, &mut p, Action::Talk, 100).unwrap();
        let id = b.high_water() + 1;
        b = b.apply(&Command::Grant(stone(id, 1, 28))).unwrap();
        for slot in 2..BAG_SLOTS {
            b = b
                .apply(&Command::Grant(
                    item(GLYPH, ITEM_NAMESPACE + 1 + u64::from(slot), slot).unwrap(),
                ))
                .unwrap();
        }
        let before = (q.clone(), b.clone(), p.clone());
        assert!(run(&mut q, &mut b, &mut p, Action::Stone(id), 101).is_err());
        assert_eq!((q.clone(), b.clone(), p.clone()), before);
        assert!(run(&mut q, &mut b, &mut p, Action::Stone(id), 100 + LIMIT).is_err());
        // Retrying an expired quest replaces the old map instead of piling up maps.
        run(&mut q, &mut b, &mut p, Action::Talk, 100 + LIMIT).unwrap();
        assert_eq!(p.1.jobs.len(), 1);
        assert_eq!(owned(&b, STONE).unwrap().quantity, 28);
        assert!(run(&mut q, &mut b, &mut p, Action::Talk, 99).is_err());
    }
    #[test]
    fn miller_referral_repeats_only_after_another_arwic_completion() {
        let (mut q, mut b, mut p) = (
            Book::default(),
            Inventory::default(),
            crate::professions::Book::default(),
        );
        q.arwic.phase = 5;
        q.arwic.completed = 99;
        run(&mut q, &mut b, &mut p, Action::Refer, 100).unwrap();
        assert!(!q.windmill.needs_referral(101, 99));
        run(&mut q, &mut b, &mut p, Action::Talk, 101).unwrap();
        assert!(!q.windmill.needs_referral(101 + REPEAT, 99));
        run(&mut q, &mut b, &mut p, Action::Refer, 101 + REPEAT).unwrap();
        assert_eq!(q.windmill.miller_completed, 101);
        q.arwic.completed = 102 + REPEAT;
        assert!(q.windmill.needs_referral(103 + REPEAT, q.arwic.completed));
        run(&mut q, &mut b, &mut p, Action::Refer, 103 + REPEAT).unwrap();
        assert_eq!(q.windmill.miller_completed, 0);
        assert!(matches!(
            run(&mut q, &mut b, &mut p, Action::Talk, 104 + REPEAT)
                .unwrap()
                .as_slice(),
            [crate::progression::Command::AwardXp(30)]
        ));
        assert!(
            run(&mut q, &mut b, &mut p, Action::Talk, 105 + REPEAT)
                .unwrap()
                .is_empty()
        );
    }
}
