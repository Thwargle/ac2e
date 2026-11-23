//! Prosper Marches quests. Native journal stages and captured Slavetaker corpse contents
//! agree with AC2Hero's osteth_theslavetakershead and osteth_seekthedrudgecounselors guides.
use crate::{
    inventory::{Command as I, Inventory, Item, Location},
    inventory_request::Metadata,
    progression::Command as P,
    recovered_quests::{Book, Profile, State},
};
use std::io;

pub const HEAD_QUEST: u32 = 0x4000002b;
pub const SEEK: u32 = 0x400000d6;
pub const COUNSELOR: u32 = 0x4700088a;
pub const DANTRY: u32 = 0x470015ca;
pub const BURUN: u32 = 0x4700127e;
pub const HEAD: u32 = 0x4700109d;
pub const EARRING: u32 = 0x470011b8;
const VISUAL: u32 = 0x1f0001cf;
const TWO_DAYS: u64 = 172800;
const HEAD_XP: u64 = 4000;
const SEEK_XP: u64 = 500;

pub fn npc(actor: u32) -> bool {
    matches!(actor, COUNSELOR | DANTRY)
}
pub fn operation(actor: u32) -> bool {
    npc(actor) || actor == BURUN
}

pub fn profiles() -> [Profile; 2] {
    let head = Profile {
        id: HEAD_QUEST,
        title: "The Slavetaker's Head".into(),
        text: "Help the New Drudge Republic prove that its former masters can be slain.".into(),
        level: 13,
        intended_level: None,
        goal: 3,
        targets: vec![BURUN],
        kill_starters: vec![],
        use_starters: vec![],
        delivery: true,
        xp_basis_points: 0,
        xp: HEAD_XP,
        limit_seconds: 0,
        repeat_seconds: TWO_DAYS,
    };
    let seek = Profile {
        id: SEEK,
        title: "Seek the Drudge Counselors".into(),
        text: "Dantry the First Warder wants news of the Drudges at Eastern Prosper Outpost."
            .into(),
        goal: 2,
        targets: vec![],
        xp: SEEK_XP,
        limit_seconds: TWO_DAYS,
        repeat_seconds: 86400,
        ..head.clone()
    };
    [head, seek]
}

pub fn guidance(id: u32, count: u16) -> &'static str {
    match (id, count) {
        (HEAD_QUEST, 0) => "Kill a Burun Slavetaker in the canyons around the King Toad Idol at 2.5N, 17.8E.",
        (HEAD_QUEST, 1) => "Take the Slavetaker's head from its corpse.",
        (HEAD_QUEST, 2) => "Give the Slavetaker's head to a New Drudge Republic Counselor at Eastern Prosper Outpost.",
        (HEAD_QUEST, _) => "The Counselor gave you a Toadfang Earring.",
        (SEEK, 0) => "Speak with a Counselor of the New Drudge Republic at Eastern Prosper Outpost and help defeat a Burun Slavetaker.",
        (SEEK, 1) => "Return to Dantry the First Warder at 7.4N, 15.4E with news of the New Drudge Republic.",
        _ => "Dantry received your report.",
    }
}

fn active(book: &Book, id: u32, time: u64) -> Option<&State> {
    book.0.get(&id).filter(|s| {
        s.completed == 0 && time >= s.started && (id == HEAD_QUEST || time - s.started < TWO_DAYS)
    })
}

pub fn on_kill(book: &mut Book, entity: u32, time: u64) {
    if entity == BURUN && active(book, HEAD_QUEST, time).is_some_and(|s| s.count == 0) {
        book.0.get_mut(&HEAD_QUEST).unwrap().count = 1;
    }
}

pub fn drop_run(book: &Book, bag: &Inventory, time: u64) -> Option<u64> {
    active(book, HEAD_QUEST, time)
        .filter(|s| s.count >= 1 && (s.count == 1 || !bag.items().any(|i| i.definition == HEAD)))
        .map(|s| s.started)
}

pub fn head() -> Item {
    Item {
        id: 1,
        definition: HEAD,
        visual: VISUAL,
        quantity: 1,
        stack_limit: 1,
        equipment_mask: 0,
        location: Location::Bag(0),
        roll: None,
    }
}

pub fn earring_slot(item: &Item) -> Option<u16> {
    (item.definition == EARRING
        && item.visual == VISUAL
        && item.equipment_mask == 1
        && item.stack_limit == 1
        && item.quantity == 1)
        .then_some(1)
}

pub fn validate(dat: &mut crate::dat::Dat) -> io::Result<Vec<(u32, Metadata)>> {
    let mut items = Vec::new();
    for did in [HEAD, EARRING] {
        let meta = Metadata::load(dat, did)?;
        let physical = dat.read(meta.physical)?;
        let matches_visual = meta.physical == 0x4700026a
            && physical.get(8..12) == Some(&0x40000001u32.to_le_bytes())
            && physical.get(12..16) == Some(&VISUAL.to_le_bytes());
        if !matches_visual
            || (did == EARRING
                && !meta
                    .equipment
                    .is_some_and(|e| e.valid == 1 && e.preferred == 1))
        {
            return Err(io::Error::other(format!(
                "Unexpected Slavetaker item physics or equipment slot for {did:08x}"
            )));
        }
        items.push((did, meta));
    }
    Ok(items)
}

fn finish(state: &mut State, count: u16, time: u64, xp: u64) {
    state.count = count;
    state.completed = time;
    state.rewarded_xp = xp;
}

pub fn plan(
    book: &Book,
    bag: &Inventory,
    actor: u32,
    offered: Option<u64>,
    level: u16,
    time: u64,
    first: u64,
) -> io::Result<(Vec<I>, Vec<P>, Book)> {
    let quest = if actor == DANTRY { SEEK } else { HEAD_QUEST };
    let state = active(book, quest, time);
    let mut next = book.clone();
    match (actor, offered) {
        (BURUN, None) if drop_run(book, bag, time).is_some() => {
            let slot = bag
                .free_slot()
                .ok_or_else(|| io::Error::other("Make room for the Slavetaker's head."))?;
            next.0.get_mut(&HEAD_QUEST).unwrap().count = 2;
            Ok((
                vec![I::Grant(Item {
                    id: first,
                    location: Location::Bag(slot),
                    ..head()
                })],
                vec![],
                next,
            ))
        }
        (COUNSELOR, None) | (DANTRY, None) if state.is_none() => {
            Ok((vec![], vec![], book.accept(quest, level, time)?))
        }
        (DANTRY, None) if state.is_some_and(|s| s.count == 1) => {
            finish(next.0.get_mut(&SEEK).unwrap(), 2, time, SEEK_XP);
            Ok((
                vec![],
                crate::quest_points::completion_rewards(SEEK, SEEK_XP),
                next,
            ))
        }
        (DANTRY | COUNSELOR, None) => Ok((vec![], vec![], next)),
        (COUNSELOR, Some(id)) if state.is_some_and(|s| s.count == 2) => {
            let item = bag
                .items()
                .find(|i| {
                    i.id == id && i.definition == HEAD && matches!(i.location, Location::Bag(_))
                })
                .ok_or_else(|| {
                    io::Error::other("Give the Counselor the Slavetaker's head from your backpack.")
                })?;
            let consume = I::ConsumeDense { item: item.id };
            let slot = bag
                .apply(&consume)
                .map_err(io::Error::other)?
                .free_slot()
                .ok_or_else(|| io::Error::other("Backpack is full."))?;
            let earring = Item {
                id: first,
                definition: EARRING,
                equipment_mask: 1,
                location: Location::Bag(slot),
                ..head()
            };
            finish(next.0.get_mut(&HEAD_QUEST).unwrap(), 3, time, HEAD_XP);
            if active(book, SEEK, time).is_some() {
                next.0.get_mut(&SEEK).unwrap().count = 1;
            }
            Ok((
                vec![consume, I::Grant(earring)],
                crate::quest_points::completion_rewards(HEAD_QUEST, HEAD_XP),
                next,
            ))
        }
        _ => Err(io::Error::other(guidance(
            quest,
            state.map_or(0, |s| s.count),
        ))),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::store::Store;

    #[test]
    fn native_head_and_earring_metadata() {
        if let Some(dir) = std::env::var_os("AC2_TEST_GAME_DATA") {
            let mut dat =
                crate::dat::Dat::open(std::path::PathBuf::from(dir).join("portal.dat")).unwrap();
            assert_eq!(validate(&mut dat).unwrap().len(), 2);
        }
    }

    #[test]
    fn two_quests_require_a_personal_kill_loot_exchange_and_report() {
        let dir = std::env::temp_dir().join(format!(
            "ac2-slay-{}-{}",
            std::process::id(),
            crate::recovered_quests::now()
        ));
        let start = crate::recovered_quests::now() - 60;
        let base_xp;
        {
            let mut s = Store::open(&dir).unwrap();
            s.create("local", "Prosper Hunter").unwrap();
            assert!(s
                .collection_delivery("local", 1, DANTRY, None, start)
                .is_err());
            while s.characters[&1].progression.level().number < 13 {
                let p = &s.characters[&1].progression;
                s.progression_command("local", 1, P::AwardXp(p.level().xp_cap + 1 - p.xp))
                    .unwrap();
            }
            base_xp = s.characters[&1].progression.xp;
            s.collection_delivery("local", 1, DANTRY, None, start)
                .unwrap();
            s.collection_delivery("local", 1, COUNSELOR, None, start)
                .unwrap();
            s.collection_delivery("local", 1, DANTRY, None, start + 1)
                .unwrap();
            s.encounter_reward("local", 1, BURUN + 1, &[P::AwardXp(1)])
                .unwrap();
            assert!(s
                .collection_delivery("local", 1, BURUN, None, start + 61)
                .is_err());
            assert!(s
                .collection_delivery("local", 1, COUNSELOR, Some(1), start + 61)
                .is_err());
            s.encounter_reward("local", 1, BURUN, &[P::AwardXp(1)])
                .unwrap();
            let c = &s.characters[&1];
            assert_eq!(
                drop_run(&c.quests.recovered, &c.inventory, start + TWO_DAYS),
                Some(start)
            );
            assert!(c
                .quests
                .recovered
                .accept(HEAD_QUEST, 13, start + TWO_DAYS)
                .is_err());
            assert!(s
                .collection_delivery("other", 1, BURUN, None, start + 61)
                .is_err());
            s.collection_delivery("local", 1, BURUN, None, start + 61)
                .unwrap();
            assert!(s
                .collection_delivery("local", 1, BURUN, None, start + 62)
                .is_err());
            let head = s.characters[&1]
                .inventory
                .items()
                .find(|i| i.definition == HEAD)
                .unwrap()
                .id;
            assert!(s
                .collection_delivery("local", 1, DANTRY, Some(head), start + 62)
                .is_err());
            s.collection_delivery("local", 1, COUNSELOR, Some(head), start + 62)
                .unwrap();
            assert!(s
                .collection_delivery("local", 1, COUNSELOR, Some(head), start + 63)
                .is_err());
            let c = &s.characters[&1];
            assert_eq!(c.progression.quest_points, 1000);
            let earring = c
                .inventory
                .items()
                .find(|i| i.definition == EARRING)
                .unwrap();
            assert_eq!(crate::apparel::slot(earring, 1), Some(1));
            assert!(c
                .inventory
                .apply(&I::Wear {
                    item: earring.id,
                    slot: 1
                })
                .is_ok());
            assert!(c.inventory.items().all(|i| i.definition != HEAD));
            s.collection_delivery("local", 1, DANTRY, None, start + 63)
                .unwrap();
            assert!(s
                .collection_delivery("local", 1, DANTRY, None, start + 64)
                .is_err());
        }
        {
            let mut s = Store::open(&dir).unwrap();
            let c = &s.characters[&1];
            assert_eq!(c.progression.quest_points, 1300);
            assert_eq!(c.progression.xp, base_xp + 4502);
            assert_eq!(c.quests.recovered.0[&HEAD_QUEST].completed, start + 62);
            assert_eq!(c.quests.recovered.0[&SEEK].completed, start + 63);
            assert_eq!(
                c.inventory
                    .items()
                    .filter(|i| i.definition == EARRING)
                    .count(),
                1
            );
            assert!(s
                .collection_delivery("local", 1, DANTRY, None, start + 63 + 86400 - 1)
                .is_err());
            s.collection_delivery("local", 1, DANTRY, None, start + 63 + 86400)
                .unwrap();
            assert!(s
                .collection_delivery("local", 1, COUNSELOR, None, start + 62 + TWO_DAYS - 1)
                .is_err());
            s.collection_delivery("local", 1, COUNSELOR, None, start + 62 + TWO_DAYS)
                .unwrap();
        }
        std::fs::remove_file(dir.join("characters.journal")).unwrap();
        std::fs::remove_dir(dir).unwrap();
    }
}
