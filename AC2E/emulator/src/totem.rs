//! AC2E's transferable workshop infusions, using the recovered generic totem identity.
use crate::inventory::{Item, Location};

pub const TOTEM: u32 = 0x47002f31;
pub const VISUAL: u32 = 0x1f001584;
pub const BONUS: u16 = 3;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Infusion { Weapon, Armor }

impl Infusion {
    pub fn description(self) -> &'static str {
        match self { Self::Weapon => "Weapon infusion: +3 damage", Self::Armor => "Armor infusion: +3 armor" }
    }
    pub fn rating(self, roll: &mut crate::inventory::Roll) -> &mut u16 {
        match self { Self::Weapon => &mut roll.damage, Self::Armor => &mut roll.armor }
    }
}

pub fn charge(item: &Item) -> Option<Infusion> {
    let r = item.roll?;
    if item.definition != TOTEM || item.visual != VISUAL || item.equipment_mask != 0
        || item.quantity != 1 || item.stack_limit != 1 || r.crafted || r.level != 1 || r.tint != 0 || r.wear != 0 {
        return None;
    }
    match (r.damage, r.armor) {
        (BONUS, 0) => Some(Infusion::Weapon),
        (0, BONUS) => Some(Infusion::Armor),
        _ => None,
    }
}

pub fn base(item: &Item) -> Option<(Infusion, u16)> {
    let roll = item.roll.filter(|r| r.crafted)?;
    if !crate::professions::valid_finish(item) { return None; }
    let def = crate::professions::catalog().items.iter()
        .find(|d| d.id == item.definition && d.visual == item.visual)?;
    let (kind, base) = if roll.damage > 0 && def.damage > 0 {
        (Infusion::Weapon, def.damage)
    } else if roll.armor > 0 && matches!(item.equipment_mask, 1 | 2 | 4 | 8 | 16 | 32) {
        (Infusion::Armor, crate::apparel::item_armor(def.id).unwrap_or(10))
    } else { return None; };
    Some((kind, crate::professions::crafted_rating(base, roll.level)))
}

pub fn infused(item: &Item) -> Option<Infusion> {
    let (kind, base) = base(item)?;
    (*kind.rating(&mut item.roll?) == base + BONUS).then_some(kind)
}

pub fn charged_totem(id: u64, kind: Infusion) -> Item {
    let mut roll = crate::inventory::Roll { level: 1, ..Default::default() };
    *kind.rating(&mut roll) = BONUS;
    Item {
        id,
        definition: TOTEM,
        visual: VISUAL,
        quantity: 1,
        stack_limit: 1,
        equipment_mask: 0,
        location: Location::Bag(0),
        roll: Some(roll),
    }
}
