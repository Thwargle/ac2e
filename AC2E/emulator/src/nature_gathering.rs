//! Black Mother Earth material path. `n1` journal records freeze this policy.
//! Eight blossoms and the recipe are guide/DAT backed. Four blossoms per use
//! and five-minute per-character regrowth are explicit reconstruction choices.
use crate::{inventory::{Command, Inventory, Item, Location}, quests::Book};
use serde::{Deserialize, Serialize};
use std::{collections::{BTreeMap, BTreeSet}, io};

pub const BUSH: u32 = 0x47002301;
pub const SHRINE: u32 = 0x4700230b;
pub const BLOSSOM: u32 = 0x470022f3;
pub const GARLAND: u32 = 0x47002355;
pub const SCENTED: u32 = 0x470022f9;
pub const RECIPE: u32 = 0x6f001fe4;
pub const YIELD: u16 = 4;
pub const REGROWTH: u64 = 300;
pub const COST: u32 = 100; // Native Flower Garland m_cost.
fn bad(s: &str) -> io::Error { io::Error::other(s) }

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct State {
    pub learned: bool,
    pub last_action: u64,
    pub harvested: BTreeMap<u64, u64>,
}
impl State {
    pub fn allowed(&self, recipe: u32) -> bool { recipe != RECIPE || self.learned }
    pub fn guidance(&self) -> &'static str {
        if self.learned {
            "You know Flower Garland. Collect seven Withered Blossoms from Flowering Bushes, then open Skills > Craft > Quest Recipes and craft a Garland of Flowers. Select this shrine and double-click the garland in your backpack to receive a Scented Blossom."
        } else {
            "Collect eight Withered Blossoms from Flowering Bushes in Naderu Greenwood. Select this shrine and double-click one blossom in your backpack to learn Flower Garland. The other seven blossoms make the garland."
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum Action {
    Harvest(u64),
    Learn(u64),
    Craft([u64; 7]),
    Offer(u64),
}
impl Action {
    pub fn encode(self) -> String { serde_json::to_string(&self).expect("serializable Nature action") }
    pub fn decode(s: &str) -> io::Result<Self> { serde_json::from_str(s).map_err(io::Error::other) }
    pub fn receipt(self) -> &'static str {
        match self {
            Self::Harvest(_) => "Collected four Withered Blossoms. This bush will replenish for you in five minutes. Offer one blossom to the Shrine to Black Mother Earth (13.5N, 61.0E) to learn Flower Garland; save seven for crafting.",
            Self::Learn(_) => "Offered one Withered Blossom. You learned Flower Garland. Open Skills > Craft > Quest Recipes and craft with seven Withered Blossoms and 100 gold, then bring the Garland of Flowers back to this shrine.",
            Self::Craft(_) => "Crafted Garland of Flowers. Select the Shrine to Black Mother Earth and double-click the garland in your backpack.",
            Self::Offer(_) => "Offered Garland of Flowers and received a Scented Blossom. Bring it to the Outcast Wasp Queen in Naderu Greenwood.",
        }
    }
}
pub struct Plan { pub items: Vec<Command>, pub rewards: Vec<crate::progression::Command>, pub quests: Book }

fn owned(bag: &Inventory, id: u64, definition: u32) -> io::Result<&Item> {
    let d = crate::nature_exchanges::asset(definition);
    bag.items().find(|i| i.id == id && i.definition == definition
        && i.visual == d.visual && i.quantity > 0 && i.quantity <= d.stack_limit
        && (i.stack_limit == d.stack_limit || (i.stack_limit == 1 && i.quantity == 1))
        && i.equipment_mask == 0 && i.roll.is_none() && matches!(i.location, Location::Bag(_)))
        .ok_or_else(|| bad(&format!("Bring {} from your backpack.", d.name)))
}

/// Require exactly the native seven ingredient ordinals. Multiple slots can
/// anchor the same stack; the planner reserves seven units across owned stacks.
pub fn craft_action(request: &crate::crafting::ExecuteRequest) -> io::Result<Action> {
    if request.recipe != RECIPE || request.spinner != 0 || request.ingredients.len() != 7
        || !request.ingredients.values().any(|i| *i == request.primary) {
        return Err(bad("Flower Garland requires seven blossom slots and no quality override."));
    }
    let mut ids = [0; 7];
    for (index, id) in ids.iter_mut().enumerate() {
        *id = *request.ingredients.get(&(index as u32 + 1))
            .ok_or_else(|| bad("Fill all seven blossom slots."))?;
    }
    Ok(Action::Craft(ids))
}

pub fn plan(book: &Book, bag: &Inventory, action: Action, first: u64, time: u64) -> io::Result<Plan> {
    if time == 0 || time < book.nature.last_action || !bag.dense_bags() {
        return Err(bad("Invalid Nature's Ally inventory or clock."));
    }
    let mut quests = book.clone();
    let mut spend = BTreeMap::<u64, u16>::new();
    let output;
    match action {
        Action::Harvest(node) => {
            if node >> 48 != 0x8140 { return Err(bad("Invalid flowering bush identity.")); }
            if let Some(last) = book.nature.harvested.get(&node) {
                if time.saturating_sub(*last) < REGROWTH {
                    return Err(bad("This flowering bush is still regrowing. Try another bush or return after five minutes."));
                }
            }
            quests.nature.harvested.retain(|_, last| time.saturating_sub(*last) < REGROWTH);
            if quests.nature.harvested.len() >= 128 { return Err(bad("Too many recently harvested bushes.")); }
            quests.nature.harvested.insert(node, time);
            output = Some((BLOSSOM, YIELD));
        }
        Action::Learn(id) => {
            if book.nature.learned { return Err(bad("You already know Flower Garland. Keep your blossoms for crafting.")); }
            owned(bag, id, BLOSSOM)?;
            spend.insert(id, 1);
            quests.nature.learned = true;
            output = None;
        }
        Action::Craft(ids) => {
            if !book.nature.learned { return Err(bad("Offer one Withered Blossom to the Shrine to Black Mother Earth to learn this recipe first.")); }
            for id in ids { owned(bag, id, BLOSSOM)?; }
            let preferred: BTreeSet<_> = ids.into_iter().collect();
            let mut stacks: Vec<_> = bag.items().filter(|i| owned(bag, i.id, BLOSSOM).is_ok()).collect();
            stacks.sort_by_key(|i| (!preferred.contains(&i.id), i.id));
            let mut needed = 7;
            for stack in stacks {
                let count = needed.min(stack.quantity);
                if count > 0 { spend.insert(stack.id, count); needed -= count; }
            }
            if needed != 0 { return Err(bad("Flower Garland requires seven Withered Blossoms in your backpack.")); }
            output = Some((GARLAND, 1));
        }
        Action::Offer(id) => {
            owned(bag, id, GARLAND)?;
            spend.insert(id, 1);
            output = Some((SCENTED, 1));
        }
    }
    let mut next = bag.clone();
    let mut items = Vec::new();
    let mut outputs = Vec::new();
    for (id, count) in spend {
        let old = bag.items().find(|i| i.id == id).unwrap();
        let consume = Command::ConsumeStackDense { item: id, quantity: old.quantity };
        next = next.apply(&consume).map_err(io::Error::other)?;
        items.push(consume);
        if old.quantity > count { outputs.push((old.definition, old.quantity - count)); }
    }
    outputs.extend(output);
    let mut id = first;
    for (definition, quantity) in outputs {
        let d = crate::nature_exchanges::asset(definition);
        let slot = next.free_slot().ok_or_else(|| bad("Make room in your backpack before collecting or exchanging blossoms."))?;
        let command = Command::Grant(Item { id, definition, visual: d.visual, quantity,
            stack_limit: d.stack_limit, equipment_mask: 0, location: Location::Bag(slot), roll: None });
        next = next.apply(&command).map_err(io::Error::other)?;
        items.push(command);
        id = id.checked_add(1).ok_or_else(|| bad("Nature item identity overflow"))?;
    }
    quests.nature.last_action = time;
    let rewards = if matches!(action, Action::Craft(_)) { vec![crate::progression::Command::SpendGold(COST)] } else { Vec::new() };
    Ok(Plan { items, rewards, quests })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::inventory::{BAG_SLOTS, ITEM_NAMESPACE};
    const NODE: u64 = 0x8140000000000001;
    fn run(book: &mut Book, bag: &mut Inventory, action: Action, time: u64) -> io::Result<()> {
        let p = plan(book, bag, action, bag.high_water() + 1, time)?;
        for c in p.items { *bag = bag.apply(&c).unwrap(); }
        *book = p.quests; Ok(())
    }
    fn id(bag: &Inventory, definition: u32) -> u64 { bag.items().find(|i| i.definition == definition).unwrap().id }
    #[test]
    fn eight_collected_blossoms_become_one_scented_blossom() {
        let (mut q, mut b) = (Book::default(), Inventory::default());
        assert!(run(&mut q, &mut b, Action::Craft([ITEM_NAMESPACE+1;7]), 1).is_err());
        run(&mut q, &mut b, Action::Harvest(NODE), 1).unwrap();
        run(&mut q, &mut b, Action::Harvest(NODE+1), 2).unwrap();
        let old = id(&b, BLOSSOM);
        run(&mut q, &mut b, Action::Learn(old), 3).unwrap();
        assert!(q.nature.learned);
        assert!(!b.items().any(|i|i.id==old));
        assert_eq!(b.items().map(|i| i.quantity).sum::<u16>(), 7);
        let anchor = id(&b, BLOSSOM);
        run(&mut q, &mut b, Action::Craft([anchor;7]), 4).unwrap();
        assert_eq!(b.items().count(), 1);
        let garland = id(&b, GARLAND);
        run(&mut q, &mut b, Action::Offer(garland), 5).unwrap();
        assert_eq!(b.items().next().unwrap().definition, SCENTED);
        assert!(run(&mut q, &mut b, Action::Offer(garland), 6).is_err());
    }
    #[test]
    fn regrowth_insufficient_quantity_and_duplicate_learning_are_atomic() {
        let (mut q, mut b) = (Book::default(), Inventory::default());
        run(&mut q, &mut b, Action::Harvest(NODE), 100).unwrap();
        let initial = (q.clone(),b.clone());
        for time in [99, 100, 399] { assert!(run(&mut q,&mut b,Action::Harvest(NODE),time).is_err()); }
        assert_eq!((q.clone(),b.clone()),initial);
        let blossom = id(&b,BLOSSOM);
        run(&mut q,&mut b,Action::Learn(blossom),101).unwrap();
        let anchor = id(&b,BLOSSOM); let initial = (q.clone(),b.clone());
        assert!(run(&mut q,&mut b,Action::Learn(anchor),102).is_err());
        assert!(run(&mut q,&mut b,Action::Craft([anchor;7]),102).is_err());
        assert_eq!((q.clone(),b.clone()),initial);
        run(&mut q,&mut b,Action::Harvest(NODE),400).unwrap();
    }
    #[test]
    fn full_backpack_does_not_charge_harvest_or_unlock_recipe() {
        let (mut q, mut b) = (Book::default(), Inventory::default());
        for n in 0..BAG_SLOTS { run(&mut q,&mut b,Action::Harvest(NODE+u64::from(n)),100).unwrap(); }
        let initial = (q.clone(),b.clone());
        assert!(run(&mut q,&mut b,Action::Harvest(NODE+100),100).is_err());
        assert_eq!((q.clone(),b.clone()),initial);
        // Learning consumes one and returns the other three into its freed slot.
        let blossom=id(&b,BLOSSOM);
        run(&mut q,&mut b,Action::Learn(blossom),101).unwrap();
        assert_eq!(b.items().count(),usize::from(BAG_SLOTS));
    }
    #[test]
    fn action_roundtrips_and_malformed_requests_reject() {
        for a in [Action::Harvest(NODE),Action::Learn(1),Action::Craft([2;7]),Action::Offer(3)] {
            assert_eq!(Action::decode(&a.encode()).unwrap(),a);
        }
        assert!(Action::decode("{\"Craft\":[1,2]}").is_err());
    }
}
