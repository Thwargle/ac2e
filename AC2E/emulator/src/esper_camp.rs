//! Esper's starter camp. `e1` journal records replay this immutable quest policy.
//! Placements, recipes and items are recovered; server transitions are reconstructed.
use crate::{
    inventory::{Command, Inventory, Item, Location},
    quests::Book,
};
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, io, sync::OnceLock};

pub const QUEST: u32 = 0x400000d0;
pub const WARDER: u32 = 0x470015c2;
pub const CADRE: u32 = 0x470015bd;
pub const BONE: u32 = 0x470015b8;
pub const MATERIAL: u32 = 0x47001400;
pub const GLYPH: u32 = 0x470010a1;
pub const LIMIT: u64 = 86400;
pub const REPEAT: u64 = 72000;
fn bad(s: &str) -> io::Error {
    io::Error::other(s)
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct State {
    // Native phases 1 craft, 2 hunt, 3 return bone; 4 completed.
    pub phase: u32,
    pub started: u64,
    pub completed: u64,
    pub last_action: u64,
}
#[derive(Deserialize)]
pub struct Asset {
    pub id: u32,
    pub name: String,
    pub visual: u32,
    physical: u32,
    quality_hash: String,
}
#[derive(Deserialize)]
pub struct Content {
    pub title: String,
    pub phases: Vec<String>,
    pub items: Vec<Asset>,
}
pub fn content() -> &'static Content {
    static C: OnceLock<Content> = OnceLock::new();
    C.get_or_init(|| {
        serde_json::from_str(include_str!("../content/esper-camp-v1.json"))
            .expect("Esper camp policy")
    })
}
pub fn recipe(id: u32) -> bool {
    (0x6f001f0f..=0x6f001f14).contains(&id)
}
pub fn weapon(id: u32) -> bool {
    (0x470015b2..=0x470015b7).contains(&id)
}
pub fn item(definition: u32, id: u64, slot: u16, quantity: u16) -> io::Result<Item> {
    let a = content()
        .items
        .iter()
        .find(|a| a.id == definition)
        .ok_or_else(|| bad("Unknown Esper item"))?;
    Ok(Item {
        id,
        definition,
        visual: a.visual,
        quantity,
        stack_limit: if definition == MATERIAL { u16::MAX } else { 1 },
        equipment_mask: if weapon(definition) { 0x40 } else { 0 },
        location: Location::Bag(slot),
        roll: None,
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
            return Err(bad("Esper DAT profile changed"));
        }
        for did in [a.id, a.visual, a.physical] {
            dat.read(did)?;
        }
        let m = if a.id == GLYPH {
            crate::inventory_request::Metadata {
                implement: None,
                icon: 0,
                physical: a.physical,
                equipment: None,
            }
        } else {
            crate::inventory_request::Metadata::load(dat, a.id)?
        };
        result.insert(a.id, m);
    }
    Ok(result)
}
pub fn check_equip(item: &Item, race: u32, level: u16) -> io::Result<()> {
    if !weapon(item.definition) {
        return Ok(());
    }
    if level < 6 {
        return Err(bad("Cadre Slayer weapons require level 6."));
    }
    let allowed = match item.definition {
        0x470015b2 => 1,
        0x470015b3 => 2,
        0x470015b4 | 0x470015b5 => 4,
        0x470015b6 => 3,
        _ => u32::MAX,
    };
    if race & allowed == 0 {
        return Err(bad("This Cadre Slayer weapon is for another race."));
    }
    Ok(())
}
pub fn slayer_weapon(item: &Item) -> bool {
    weapon(item.definition) && content().items.iter().any(|a| a.id == item.definition && a.visual == item.visual)
}
pub fn equipped(bag: &Inventory) -> bool {
    bag.items().any(|item| item.location == Location::Equipped(0x40) && slayer_weapon(item))
}
pub fn damage_multiplier(bag: &Inventory, target: u32, periodic: bool, weapon_hit: bool) -> u32 {
    if target == CADRE && !periodic && weapon_hit && equipped(bag) {
        3
    } else {
        1
    }
}
impl State {
    pub fn active(&self, time: u64) -> bool {
        (1..=3).contains(&self.phase) && time >= self.started && time - self.started < LIMIT
    }
    pub fn allowed(&self, id: u32, time: u64) -> bool {
        !recipe(id) || self.active(time)
    }
    pub fn can_loot(&self, bag: &Inventory, time: u64) -> bool {
        self.active(time) && !bag.items().any(|i| i.definition == BONE)
    }
    pub fn guidance(&self) -> String {
        match self.phase {
            1=>format!("{} Gather 25 Bone by using a Hunting Knife on vermin you have defeated. Craft a Cadre Slayer weapon under Quest Recipes. The weapons require level 6 to equip; other weapons can also defeat the Cadre. You have 24 hours.",content().phases[0]),
            2=>format!("{} Take the Southwest Esper ringway outpost and follow the road east to the island southeast of Millstone.",content().phases[1]),
            3=>format!("{} Return to Millstone at 37.5N, 19.9W; select the Esper Warder and use the Drudge Cadre Bone.",content().phases[2]),
            4=>"Drudge Outcast Camp Quest complete: 320 experience and an Esper Glyph. The glyph opens the Esper Vault. You may repeat this task after 20 hours.".into(),
            _=>"Speak to the Esper Warder in Millstone at 37.5N, 19.9W.".into(),
        }
    }
    pub fn entries(&self) -> Vec<(u32, &'static str, String, u32, u64, u64, u64)> {
        if self.phase == 0 {
            vec![]
        } else {
            vec![(
                QUEST,
                "Drudge Outcast Camp Quest",
                self.guidance(),
                self.phase,
                self.started,
                self.completed,
                LIMIT,
            )]
        }
    }
}
#[derive(Clone, Copy, Debug, Serialize, Deserialize)]
pub enum Action {
    Talk,
    Bone,
    Return(u64),
    Craft { recipe: u32, primary: u64 },
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
}
pub fn plan(
    book: &Book,
    bag: &Inventory,
    action: Action,
    first: u64,
    time: u64,
) -> io::Result<Plan> {
    if time == 0 || time < book.esper.last_action || !bag.dense_bags() {
        return Err(bad("Invalid Esper quest clock or inventory"));
    }
    let mut next = book.clone();
    let mut bag = bag.clone();
    let mut items = Vec::new();
    let mut rewards = Vec::new();
    let mut id = first;
    let apply = |bag: &mut Inventory, items: &mut Vec<Command>, c: Command| -> io::Result<()> {
        *bag = bag.apply(&c).map_err(io::Error::other)?;
        items.push(c);
        Ok(())
    };
    let mut grant =
        |bag: &mut Inventory, items: &mut Vec<Command>, definition, quantity| -> io::Result<()> {
            let slot = bag
                .free_slot()
                .ok_or_else(|| bad("Make room in your backpack for the Esper quest item."))?;
            apply(
                bag,
                items,
                Command::Grant(item(definition, id, slot, quantity)?),
            )?;
            id = id
                .checked_add(1)
                .ok_or_else(|| bad("Item identity overflow"))?;
            Ok(())
        };
    let action =
        if matches!(action, Action::Talk) && next.esper.active(time) && next.esper.phase == 3 {
            let i = crate::arwic_mines::owned(&bag, BONE)
                .ok_or_else(|| bad("Bring the Drudge Cadre Bone in your backpack."))?;
            Action::Return(i)
        } else {
            action
        };
    match action {
        Action::Talk => {
            // Completing the preceding referral is part of the same transaction.
            if next.windmill.warder_started > 0 && next.windmill.warder_completed == 0 {
                next.windmill.warder_completed = time;
            }
            if !next.esper.active(time)
                && (next.esper.completed == 0
                    || time.saturating_sub(next.esper.completed) >= REPEAT)
            {
                // An expired run cannot stockpile bones for instant repeat rewards.
                let stale = bag
                    .items()
                    .filter(|i| i.definition == BONE)
                    .map(|i| i.id)
                    .collect::<Vec<_>>();
                for item in stale {
                    apply(&mut bag, &mut items, Command::ConsumeDense { item })?;
                }
                next.esper = State {
                    phase: 1,
                    started: time,
                    last_action: time,
                    ..Default::default()
                };
            }
        }
        Action::Bone => {
            if !next.esper.can_loot(&bag, time) {
                return Err(bad(
                    "Accept an active Esper camp quest before collecting its bone; you only need one.",
                ));
            }
            grant(&mut bag, &mut items, BONE, 1)?;
            next.esper.phase = 3;
        }
        Action::Return(i) => {
            if !next.esper.active(time) || next.esper.phase != 3 {
                return Err(bad(
                    "This Esper camp quest is not ready for a bone hand-in.",
                ));
            }
            if !bag.items().any(|v| {
                v.id == i && v.definition == BONE && matches!(v.location, Location::Bag(_))
            }) {
                return Err(bad("Give the Esper Warder your Drudge Cadre Bone."));
            }
            apply(&mut bag, &mut items, Command::ConsumeDense { item: i })?;
            grant(&mut bag, &mut items, GLYPH, 1)?;
            rewards.push(crate::progression::Command::AwardXp(320));
            rewards.extend(crate::quest_points::award(QUEST));
            next.esper.phase = 4;
            next.esper.completed = time;
        }
        Action::Craft {
            recipe: rid,
            primary,
        } => {
            if !recipe(rid) || !next.esper.active(time) {
                return Err(bad(
                    "Accept the Esper Warder's active camp quest to use this recipe.",
                ));
            }
            if !bag.items().any(|i| {
                i.id == primary
                    && i.definition == MATERIAL
                    && matches!(i.location, Location::Bag(_))
            }) {
                return Err(bad("Select Bone from your backpack."));
            }
            let mut stacks = bag
                .items()
                .filter(|i| i.definition == MATERIAL && matches!(i.location, Location::Bag(_)))
                .cloned()
                .collect::<Vec<_>>();
            stacks.sort_by_key(|i| (i.id != primary, i.id));
            let mut need = 25;
            let mut remainder = 0;
            for stack in stacks {
                if need == 0 {
                    break;
                }
                let used = need.min(stack.quantity);
                need -= used;
                remainder = stack.quantity - used;
                apply(
                    &mut bag,
                    &mut items,
                    Command::ConsumeStackDense {
                        item: stack.id,
                        quantity: stack.quantity,
                    },
                )?;
            }
            if need > 0 {
                return Err(bad("Cadre Slayer weapons require 25 Bone."));
            }
            if remainder > 0 {
                grant(&mut bag, &mut items, MATERIAL, remainder)?;
            }
            grant(&mut bag, &mut items, 0x470015b2 + rid - 0x6f001f0f, 1)?;
            if next.esper.phase == 1 {
                next.esper.phase = 2;
            }
        }
    }
    if next != *book || !items.is_empty() {
        next.esper.last_action = time;
    }
    Ok(Plan {
        items,
        rewards,
        quests: next,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    fn step(
        q: &mut Book,
        b: &mut Inventory,
        a: Action,
        t: u64,
    ) -> Vec<crate::progression::Command> {
        let p = plan(
            q,
            b,
            a,
            b.high_water().max(crate::inventory::ITEM_NAMESPACE) + 1,
            t,
        )
        .unwrap();
        for c in p.items {
            *b = b.apply(&c).unwrap();
        }
        *q = p.quests;
        p.rewards
    }
    #[test]
    fn craft_and_hand_in_are_atomic_and_repeat_does_not_reuse_bones() {
        let (mut q, mut b) = (Book::default(), Inventory::default());
        assert!(
            plan(
                &q,
                &b,
                Action::Bone,
                crate::inventory::ITEM_NAMESPACE + 1,
                99
            )
            .is_err()
        );
        step(&mut q, &mut b, Action::Talk, 100);
        assert_eq!(b.items().count(), 0, "No invented material gift");
        for (n, qty) in [10, 20].into_iter().enumerate() {
            b = b
                .apply(&Command::Grant(
                    item(
                        MATERIAL,
                        crate::inventory::ITEM_NAMESPACE + n as u64 + 1,
                        n as u16,
                        qty,
                    )
                    .unwrap(),
                ))
                .unwrap();
        }
        let primary = b.items().next().unwrap().id;
        step(
            &mut q,
            &mut b,
            Action::Craft {
                recipe: 0x6f001f14,
                primary,
            },
            101,
        );
        assert_eq!(q.esper.phase, 2);
        assert_eq!(
            b.items()
                .find(|i| i.definition == MATERIAL)
                .unwrap()
                .quantity,
            5
        );
        step(&mut q, &mut b, Action::Bone, 102);
        assert_eq!(q.esper.phase, 3);
        assert!(plan(&q, &b, Action::Bone, b.high_water() + 1, 103).is_err());
        assert!(plan(&q, &b, Action::Return(primary), b.high_water() + 1, 103).is_err());
        assert!(matches!(
            step(&mut q, &mut b, Action::Talk, 104).as_slice(),
            [crate::progression::Command::AwardXp(320), crate::progression::Command::AwardQuestPoints(150)]
        ));
        assert!(step(&mut q, &mut b, Action::Talk, 105).is_empty());
        assert_eq!(q.esper.completed, 104);
        assert_eq!(b.items().filter(|i| i.definition == GLYPH).count(), 1);
        assert!(!q.esper.allowed(0x6f001f14, 105));
        step(&mut q, &mut b, Action::Talk, 104 + REPEAT);
        assert_eq!(q.esper.phase, 1);
        assert_eq!(q.esper.completed, 0);
    }
    #[test]
    fn optional_weapon_expiration_full_bag_and_clock_regressions() {
        let (mut q, mut b) = (Book::default(), Inventory::default());
        step(&mut q, &mut b, Action::Talk, 100);
        assert!(
            plan(
                &q,
                &b,
                Action::Bone,
                crate::inventory::ITEM_NAMESPACE + 1,
                100 + LIMIT
            )
            .is_err()
        );
        assert!(
            plan(
                &q,
                &b,
                Action::Talk,
                crate::inventory::ITEM_NAMESPACE + 1,
                99
            )
            .is_err()
        );
        step(&mut q, &mut b, Action::Bone, 101);
        assert_eq!(q.esper.phase, 3, "Craft is optional for completion");
        for n in 1..crate::inventory::BAG_SLOTS {
            b = b
                .apply(&Command::Grant(
                    item(GLYPH, b.high_water() + 1, n, 1).unwrap(),
                ))
                .unwrap();
        }
        step(&mut q, &mut b, Action::Talk, 102);
        assert_eq!(q.esper.phase, 4, "Hand-in reuses the consumed bone slot");
        step(&mut q, &mut b, Action::Talk, 102 + REPEAT);
        let before = q.clone();
        assert!(plan(&q, &b, Action::Bone, b.high_water() + 1, 103 + REPEAT).is_err());
        assert_eq!(q, before);
    }
    #[test]
    fn slayer_is_equipped_target_specific_and_never_periodic() {
        let sword = item(0x470015b7, crate::inventory::ITEM_NAMESPACE + 1, 0, 1).unwrap();
        let b = Inventory::default()
            .apply(&Command::Grant(sword.clone()))
            .unwrap();
        assert_eq!(damage_multiplier(&b, CADRE, false, true), 1);
        let b = b
            .apply(&Command::Wear {
                item: sword.id,
                slot: 0x40,
            })
            .unwrap();
        assert_eq!(damage_multiplier(&b, CADRE, false, true), 3);
        assert_eq!(damage_multiplier(&b, CADRE, true, true), 1);
        assert_eq!(damage_multiplier(&b, 0x470015ac, false, true), 1);
        assert_eq!(damage_multiplier(&b, CADRE, false, false), 1);
        assert!(check_equip(&sword, 1, 5).is_err());
        assert!(check_equip(&sword, 1, 6).is_ok());
        assert!(check_equip(&item(0x470015b2, 1, 0, 1).unwrap(), 2, 6).is_err());
    }
}
