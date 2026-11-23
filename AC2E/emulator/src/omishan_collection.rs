//! Shreth-Hide Leather (AC2Hero, quest/omishan_shrethhideleather.html).
//! Native NPCs, items and hunter; local journal ID because the Lupe package mapping is unresolved.
use crate::{
    crafting::TALLOW,
    inventory::{Command as I, Inventory, Item, Location},
    progression::Command as P,
    recovered_quests::{Book, Profile, State},
};
use std::io;

pub const QUEST: u32 = 0x4000fe03;
pub const LUPE: u32 = 0x470021b7;
pub const GALLUS: u32 = 0x470021b8;
pub const HUNTER: u32 = 0x470021c1;
pub const RAWHIDE: u32 = 0x470021b1;
pub const LEATHER: u32 = 0x470021b0;
const LIMIT: u64 = 7200;
const XP: u64 = 120_000;
const GOLD: u32 = 1250;

pub fn profile() -> Profile {
    Profile {
        id: QUEST, title: "Shreth-Hide Leather".into(),
        text: "Find the Scarred Shreth Hunter in northeast Naderu and bring its rawhide to Lupe in Hakata.".into(),
        goal: 3, targets: vec![HUNTER], kill_starters: vec![], use_starters: vec![],
        level: 30, intended_level: None, xp: XP, repeat_seconds: LIMIT, limit_seconds: LIMIT, delivery: true, xp_basis_points: 0,
    }
}
pub fn guidance(phase: u16) -> &'static str {
    match phase {
        0 => "Hunt the Scarred Shreth Hunter near 39.2N, 68.1E and give its rawhide to Lupe in Hakata.",
        1 => "Give Lupe 100 units of Tallow to tan the hide.",
        2 => "Give the Scarred Shreth Leather to Gallus in Whitebay at 21.3S, 4.8W.",
        _ => "Gallus received the leather. You earned 120,000 experience and 1,250 gold.",
    }
}
fn active(book: &Book, now: u64) -> Option<&State> {
    book.0
        .get(&QUEST)
        .filter(|s| s.completed == 0 && now >= s.started && now - s.started < LIMIT)
}
pub fn drop_run(book: &Book, bag: &Inventory, now: u64) -> Option<u64> {
    active(book, now)
        .filter(|s| s.count == 0 && !bag.items().any(|i| i.definition == RAWHIDE))
        .map(|s| s.started)
}
pub fn accepts(npc: u32, item: u32) -> bool {
    matches!((npc, item), (LUPE, RAWHIDE | TALLOW) | (GALLUS, LEATHER))
}
pub fn handles(npc: u32, offered: bool, book: &Book, now: u64) -> bool {
    npc == LUPE || (npc == GALLUS && (offered || active(book, now).is_some_and(|s| s.count == 2)))
}

pub fn validate(
    dat: &mut crate::dat::Dat,
) -> io::Result<Vec<(u32, crate::inventory_request::Metadata)>> {
    let mut metadata = Vec::new();
    for (id, visual, physical) in [
        (RAWHIDE, 0x1f001237, 0x470022c3),
        (LEATHER, 0x1f001236, 0x470022c2),
    ] {
        for did in [id, visual, physical] {
            dat.read(did)?;
        }
        let info = crate::inventory_request::Metadata::load(dat, id)?;
        if info.physical != physical {
            return Err(io::Error::other("Unexpected Shreth quest item physics"));
        }
        metadata.push((id, info));
    }
    Ok(metadata)
}
pub fn rawhide() -> Item {
    Item {
        id: 1,
        definition: RAWHIDE,
        visual: 0x1f001237,
        quantity: 1,
        stack_limit: 1,
        equipment_mask: 0,
        location: Location::Bag(0),
        roll: None,
    }
}

fn tan(bag: &Inventory, offered: u64, first: u64) -> io::Result<Vec<I>> {
    let mut stacks: Vec<_> = bag
        .items()
        .filter(|i| i.definition == TALLOW && matches!(i.location, Location::Bag(_)))
        .collect();
    stacks.sort_by_key(|i| i.id != offered);
    let mut remaining = 100;
    let mut next = first;
    let mut inventory = bag.clone();
    let mut commands = Vec::new();
    for stack in stacks {
        let consumed = remaining.min(stack.quantity);
        let command = I::ConsumeStackDense {
            item: stack.id,
            quantity: stack.quantity,
        };
        inventory = inventory.apply(&command).map_err(io::Error::other)?;
        commands.push(command);
        if consumed < stack.quantity {
            let mut rest = stack.clone();
            rest.id = next;
            rest.quantity -= consumed;
            rest.location = Location::Bag(
                inventory
                    .free_slot()
                    .ok_or_else(|| io::Error::other("Backpack is full."))?,
            );
            let command = I::Grant(rest);
            inventory = inventory.apply(&command).map_err(io::Error::other)?;
            commands.push(command);
            next = next
                .checked_add(1)
                .ok_or_else(|| io::Error::other("Item identity exhausted"))?;
        }
        remaining -= consumed;
        if remaining == 0 {
            break;
        }
    }
    if remaining != 0 {
        return Err(io::Error::other(guidance(1)));
    }
    let slot = inventory
        .free_slot()
        .ok_or_else(|| io::Error::other("Make room for the tanned leather."))?;
    commands.push(I::Grant(Item {
        id: next,
        definition: LEATHER,
        visual: 0x1f001236,
        location: Location::Bag(slot),
        ..rawhide()
    }));
    Ok(commands)
}

pub fn plan(
    book: &Book,
    bag: &Inventory,
    npc: u32,
    offered: Option<u64>,
    level: u16,
    now: u64,
    first: u64,
) -> io::Result<(Vec<I>, Vec<P>, Book)> {
    if offered.is_none() && npc == LUPE && active(book, now).is_none() {
        return Ok((vec![], vec![], book.accept(QUEST, level, now)?));
    }
    let state = active(book, now)
        .ok_or_else(|| io::Error::other("Speak to Lupe in Hakata for Shreth-Hide Leather."))?;
    let item = offered
        .and_then(|id| {
            bag.items()
                .find(|i| i.id == id && matches!(i.location, Location::Bag(_)))
        })
        .ok_or_else(|| io::Error::other(guidance(state.count)))?;
    let mut next = book.clone();
    let progress = next.0.get_mut(&QUEST).unwrap();
    let mut rewards = Vec::new();
    let items = match (npc, state.count, item.definition) {
        (LUPE, 0, RAWHIDE) => vec![I::ConsumeDense { item: item.id }],
        (LUPE, 1, TALLOW) => tan(bag, item.id, first)?,
        (GALLUS, 2, LEATHER) => {
            progress.completed = now;
            progress.rewarded_xp = XP;
            rewards.extend([P::AwardXp(XP), P::AwardGold(GOLD)]);
            rewards.extend(crate::quest_points::award(QUEST));
            vec![I::ConsumeDense { item: item.id }]
        }
        _ => return Err(io::Error::other(guidance(state.count))),
    };
    progress.count += 1;
    Ok((items, rewards, next))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{inventory::ITEM_NAMESPACE, store::Store};

    #[test]
    fn hunter_has_both_guide_documented_butchering_materials() {
        let hunter = crate::professions::catalog()
            .butcher
            .iter()
            .find(|row| row.id == HUNTER)
            .unwrap();
        assert_eq!(hunter.yield_material(0), 0x470027a3); // Hide
        assert_eq!(hunter.yield_material(u32::MAX), TALLOW);
        assert!(crate::professions::catalog()
            .items
            .iter()
            .any(|item| item.id == TALLOW && item.stack > 100));
    }

    #[test]
    fn source_items_match_native_physics() {
        if let Some(dir) = std::env::var_os("AC2_TEST_GAME_DATA") {
            let mut dat =
                crate::dat::Dat::open(std::path::PathBuf::from(dir).join("portal.dat")).unwrap();
            let rows = validate(&mut dat).unwrap();
            assert_eq!(rows.len(), 2);
        }
    }

    #[test]
    fn collection_consumes_the_right_stacks_and_replays_all_stages() {
        let dir = std::env::temp_dir().join(format!("ac2-shreth-{}", std::process::id()));
        let start = 10_000;
        let xp;
        {
            let mut s = Store::open(&dir).unwrap();
            s.create("local", "Hakata Hunter").unwrap();
            assert!(s.collection_delivery("local", 1, LUPE, None, start).is_err());
            while s.characters[&1].progression.level().number < 30 {
                let p = &s.characters[&1].progression;
                s.progression_command("local", 1, P::AwardXp(p.level().xp_cap + 1 - p.xp))
                    .unwrap();
            }
            xp = s.characters[&1].progression.xp;
            s.collection_delivery("local", 1, LUPE, None, start).unwrap();
            let c = &s.characters[&1];
            assert_eq!(
                drop_run(&c.quests.recovered, &c.inventory, start + 1),
                Some(start)
            );
            assert_eq!(
                drop_run(&c.quests.recovered, &c.inventory, start + LIMIT),
                None
            );
            let hide = Item {
                id: ITEM_NAMESPACE + 1,
                ..rawhide()
            };
            s.inventory_command("local", 1, I::Grant(hide.clone()))
                .unwrap();
            let c = &s.characters[&1];
            assert_eq!(drop_run(&c.quests.recovered, &c.inventory, start + 1), None);
            assert!(s
                .collection_delivery("other", 1, LUPE, Some(hide.id), start + 1)
                .is_err());
            assert!(s
                .collection_delivery("local", 1, GALLUS, Some(hide.id), start + 1)
                .is_err());
            s.collection_delivery("local", 1, LUPE, Some(hide.id), start + 1)
                .unwrap();
            let a = Item {
                quantity: 67,
                ..crate::crafting::tallow(ITEM_NAMESPACE + 2, 0)
            };
            s.inventory_command("local", 1, I::Grant(a.clone()))
                .unwrap();
            assert!(s
                .collection_delivery("local", 1, LUPE, Some(a.id), start + 1)
                .is_err());
            let b = Item {
                quantity: 42,
                ..crate::crafting::tallow(ITEM_NAMESPACE + 3, 1)
            };
            s.inventory_command("local", 1, I::Grant(b.clone()))
                .unwrap();
            s.collection_delivery("local", 1, LUPE, Some(b.id), start + 2)
                .unwrap();
            assert!(s
                .collection_delivery("local", 1, LUPE, Some(b.id), start + 2)
                .is_err());
        }
        {
            let mut s = Store::open(&dir).unwrap();
            let c = &s.characters[&1];
            assert_eq!(c.quests.recovered.0[&QUEST].count, 2);
            assert_eq!(
                c.inventory
                    .items()
                    .find(|i| i.definition == TALLOW)
                    .unwrap()
                    .quantity,
                9
            );
            let leather = c
                .inventory
                .items()
                .find(|i| i.definition == LEATHER)
                .unwrap()
                .id;
            assert!(s
                .collection_delivery("local", 1, GALLUS, Some(leather), start + LIMIT)
                .is_err());
            s.collection_delivery("local", 1, GALLUS, Some(leather), start + 3)
                .unwrap();
            assert!(s
                .collection_delivery("local", 1, GALLUS, Some(leather), start + 3)
                .is_err());
            assert!(s
                .collection_delivery("local", 1, LUPE, None, start + LIMIT + 2)
                .is_err());
        }
        {
            let mut s = Store::open(&dir).unwrap();
            let c = &s.characters[&1];
            assert_eq!(c.quests.recovered.0[&QUEST].completed, start + 3);
            assert_eq!(c.progression.xp, xp + XP);
            assert_eq!(c.progression.quest_points, 600);
            assert_eq!(c.progression.gold, GOLD);
            assert!(c.inventory.items().all(|i| i.definition != LEATHER));
            s.collection_delivery("local", 1, LUPE, None, start + LIMIT + 3)
                .unwrap();
        }
        std::fs::remove_file(dir.join("characters.journal")).unwrap();
        std::fs::remove_dir(dir).unwrap();
    }
}
