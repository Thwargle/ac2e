//! Four guide-backed Nature's Ally material trades, not the complete quest.
use crate::inventory::{Command, Inventory, Item, Location};
use serde::Deserialize;
use std::{collections::BTreeMap, io, sync::OnceLock};

fn bad(text: &str) -> io::Error {
    io::Error::other(text)
}

#[derive(Deserialize)]
pub struct Asset {
    pub id: u32,
    pub name: String,
    pub visual: u32,
    physical: u32,
    quality_hash: String,
    #[serde(default)]
    pub stack_limit: u16,
}
#[derive(Deserialize)]
pub struct Rule {
    pub npc: Asset,
    pub input: u32,
    pub output: u32,
    pub quantity: u16,
}
#[derive(Deserialize)]
pub struct Content {
    pub items: Vec<Asset>,
    pub rules: Vec<Rule>,
    pub gathering: Gathering,
}
#[derive(Deserialize)]
pub struct Gathering {
    pub bush: Asset,
    pub shrine: Asset,
    pub recipe: u32,
    pub recipe_sha256: String,
    pub yield_quantity: u16,
    pub regrowth_seconds: u64,
}
pub fn content() -> &'static Content {
    static C: OnceLock<Content> = OnceLock::new();
    C.get_or_init(|| {
        serde_json::from_str(include_str!("../content/nature-exchanges-v1.json"))
            .expect("checked nature exchanges")
    })
}
pub fn rule(npc: u32) -> Option<&'static Rule> {
    content().rules.iter().find(|r| r.npc.id == npc)
}
pub(crate) fn asset(id: u32) -> &'static Asset {
    content()
        .items
        .iter()
        .find(|i| i.id == id)
        .expect("checked exchange item")
}
impl Rule {
    pub fn guidance(&self) -> String {
        format!(
            "Bring one {} to receive {} {}. Select me, then double-click that item in your backpack to exchange it. Use @talk for more information about Nature's Ally.",
            asset(self.input).name,
            self.quantity,
            asset(self.output).name
        )
    }
    pub fn receipt(&self) -> String {
        format!(
            "Exchanged one {} for {} {}.",
            asset(self.input).name,
            self.quantity,
            asset(self.output).name
        )
    }
}
pub fn validate(
    dat: &mut crate::dat::Dat,
) -> io::Result<BTreeMap<u32, crate::inventory_request::Metadata>> {
    use sha2::{Digest, Sha256};
    let g = &content().gathering;
    if g.recipe != crate::nature_gathering::RECIPE || g.bush.id != crate::nature_gathering::BUSH
        || g.shrine.id != crate::nature_gathering::SHRINE
        || g.yield_quantity != crate::nature_gathering::YIELD
        || g.regrowth_seconds != crate::nature_gathering::REGROWTH
        || format!("{:x}", Sha256::digest(dat.read(g.recipe)?)) != g.recipe_sha256 {
        return Err(bad("Nature gathering DAT recipe or version-one policy changed"));
    }
    let mut result = BTreeMap::new();
    for d in content()
        .items
        .iter()
        .chain(content().rules.iter().map(|r| &r.npc))
        .chain([&content().gathering.bush, &content().gathering.shrine])
    {
        let raw = dat.read(0x81000000 | (d.id & 0xffffff))?;
        let hash = raw.iter().fold(0xcbf29ce484222325u64, |h, b| {
            (h ^ u64::from(*b)).wrapping_mul(0x100000001b3)
        });
        if format!("{hash:016x}") != d.quality_hash {
            return Err(bad("Nature exchange DAT profile changed"));
        }
        dat.read(d.id)?;
        dat.read(d.visual)?;
        dat.read(d.physical)?;
        if d.stack_limit != 0 {
            let info = crate::inventory_request::Metadata::load(dat, d.id)?;
            if info.physical != d.physical {
                return Err(bad("Nature exchange physical profile changed"));
            }
            result.insert(d.id, info);
        }
    }
    Ok(result)
}

/// Replace the entire submitted stack atomically, returning any remainder with
/// a new identity. Replaying the old request therefore cannot spend it twice.
/// Placement, visibility, ownership account, combat and rate checks live at the
/// server boundary. The durable player transaction applies this complete plan.
pub fn plan(npc: u32, item: u64, bag: &Inventory, first: u64) -> io::Result<Vec<Command>> {
    let rule = rule(npc).ok_or_else(|| bad("That NPC does not offer this exchange."))?;
    let owned = bag
        .items()
        .find(|i| i.id == item)
        .ok_or_else(|| bad("Exchange item is not in your backpack."))?;
    let input = asset(rule.input);
    if owned.definition != input.id || owned.visual != input.visual || owned.quantity == 0
        || owned.quantity > input.stack_limit
        // Captured/trophy loot historically persisted singletons with limit 1.
        // Accept that owned representation as well as the native stack limit.
        || !(owned.stack_limit == input.stack_limit || (owned.stack_limit == 1 && owned.quantity == 1))
        || owned.equipment_mask != 0 || owned.roll.is_some() || !matches!(owned.location, Location::Bag(_))
    {
        return Err(bad(&format!(
            "Bring one {} from your backpack to {}.",
            input.name, rule.npc.name
        )));
    }
    if !bag.dense_bags() {
        return Err(bad("Sort your backpack before exchanging items."));
    }
    let consume = Command::ConsumeStackDense {
        item,
        quantity: owned.quantity,
    };
    let mut next = bag.apply(&consume).map_err(io::Error::other)?;
    let mut commands = vec![consume];
    let mut id = first;
    let outputs = [
        (rule.input, owned.quantity - 1),
        (rule.output, rule.quantity),
    ];
    for (definition, quantity) in outputs {
        if quantity == 0 {
            continue;
        }
        let d = asset(definition);
        let slot = next
            .free_slot()
            .ok_or_else(|| bad("Make room in your backpack for the exchange reward."))?;
        let grant = Command::Grant(Item {
            id,
            definition,
            visual: d.visual,
            quantity,
            stack_limit: d.stack_limit,
            equipment_mask: 0,
            location: Location::Bag(slot),
            roll: None,
        });
        next = next.apply(&grant).map_err(io::Error::other)?;
        commands.push(grant);
        id = id
            .checked_add(1)
            .ok_or_else(|| bad("Item identity overflow"))?;
    }
    Ok(commands)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::inventory::{BAG_SLOTS, ITEM_NAMESPACE};
    fn bag(definition: u32, quantity: u16) -> Inventory {
        let d = asset(definition);
        Inventory::default()
            .apply(&Command::Grant(Item {
                id: ITEM_NAMESPACE + 1,
                definition,
                visual: d.visual,
                quantity,
                stack_limit: d.stack_limit,
                equipment_mask: 0,
                location: Location::Bag(0),
                roll: None,
            }))
            .unwrap()
    }
    fn apply(bag: &Inventory, commands: &[Command]) -> Inventory {
        commands
            .iter()
            .fold(bag.clone(), |b, c| b.apply(c).unwrap())
    }
    #[test]
    fn all_exchanges_have_exact_outputs_and_reject_repeated_requests() {
        for r in &content().rules {
            let before = bag(r.input, 1);
            let after = apply(
                &before,
                &plan(r.npc.id, ITEM_NAMESPACE + 1, &before, ITEM_NAMESPACE + 2).unwrap(),
            );
            let reward = after.items().next().unwrap();
            assert_eq!(
                (reward.definition, reward.quantity, reward.stack_limit),
                (r.output, r.quantity, asset(r.output).stack_limit)
            );
            assert!(after.dense_bags());
            assert!(plan(r.npc.id, ITEM_NAMESPACE + 1, &after, ITEM_NAMESPACE + 3).is_err());
        }
    }
    #[test]
    fn partial_stack_returns_remainder_and_full_backpack_does_not_spend_it() {
        let mut before = bag(0x470022f8, 10);
        let after = apply(
            &before,
            &plan(0x47002308, ITEM_NAMESPACE + 1, &before, ITEM_NAMESPACE + 2).unwrap(),
        );
        assert_eq!(
            after
                .items()
                .find(|i| i.definition == 0x470022f8)
                .unwrap()
                .quantity,
            9
        );
        for slot in 1..BAG_SLOTS {
            let mut filler = bag(0x470022f4, 1).items().next().unwrap().clone();
            filler.id = ITEM_NAMESPACE + 1 + u64::from(slot);
            filler.location = Location::Bag(slot);
            before = before.apply(&Command::Grant(filler)).unwrap();
        }
        let unchanged = before.clone();
        assert!(
            plan(
                0x47002308,
                ITEM_NAMESPACE + 1,
                &before,
                ITEM_NAMESPACE + 100
            )
            .is_err()
        );
        assert_eq!(before, unchanged);
        // Spending the whole input frees the slot needed for its reward.
        let before = before
            .apply(&Command::Consume {
                item: ITEM_NAMESPACE + 1,
                quantity: 9,
            })
            .unwrap();
        let after = apply(
            &before,
            &plan(
                0x47002308,
                ITEM_NAMESPACE + 1,
                &before,
                ITEM_NAMESPACE + 100,
            )
            .unwrap(),
        );
        assert_eq!(after.items().count(), usize::from(BAG_SLOTS));
        assert!(after.dense_bags());
    }
    #[test]
    fn foreign_items_wrong_npcs_and_forged_profiles_are_rejected() {
        let before = bag(0x470022f8, 1);
        assert!(plan(0x47002308, ITEM_NAMESPACE + 2, &before, ITEM_NAMESPACE + 3).is_err());
        assert!(plan(0x47002309, ITEM_NAMESPACE + 1, &before, ITEM_NAMESPACE + 3).is_err());
        assert!(plan(0x470007d1, ITEM_NAMESPACE + 1, &before, ITEM_NAMESPACE + 3).is_err());
        let mut item = before.items().next().unwrap().clone();
        item.visual = asset(0x470022f4).visual;
        let forged = Inventory::default().apply(&Command::Grant(item)).unwrap();
        assert!(plan(0x47002308, ITEM_NAMESPACE + 1, &forged, ITEM_NAMESPACE + 3).is_err());
    }
    #[test]
    fn existing_singleton_loot_can_be_exchanged() {
        let mut old = bag(0x470022f8, 1).items().next().unwrap().clone();
        old.stack_limit = 1;
        let before = Inventory::default().apply(&Command::Grant(old)).unwrap();
        let after = apply(
            &before,
            &plan(0x47002308, ITEM_NAMESPACE + 1, &before, ITEM_NAMESPACE + 2).unwrap(),
        );
        assert_eq!(after.items().next().unwrap().definition, 0x470022f4);
    }
}
