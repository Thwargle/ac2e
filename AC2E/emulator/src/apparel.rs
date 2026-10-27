//! Additional DAT armor profiles. Race/sex selectors are native entity properties;
//! deterministic training drops are AC2E policy, not recovered retail loot tables.
use crate::{inventory::{Inventory,Item,Location}, inventory_request::Metadata};
struct Profile {definition:u32,visual:u32,physical:u32,appearance:u32,race:u32,slot:u16,male:u32,female:u32,armor:u32,hash:u64,entity_hash:u64}
include!("apparel_data.rs");
pub fn validate(dat:&mut crate::dat::Dat)->std::io::Result<std::collections::BTreeMap<u32,Metadata>> {
    let mut result=std::collections::BTreeMap::new();
    let hash=|b:&[u8]|b.iter().fold(0xcbf29ce484222325u64,|h,b|(h^u64::from(*b)).wrapping_mul(0x100000001b3));
    for p in PROFILES {
        if hash(&dat.read(p.definition)?)!=p.entity_hash || hash(&dat.read(0x81000000|(p.definition&0xffffff))?)!=p.hash {
            return Err(std::io::Error::other("Armor DAT profile changed"));
        }
        for id in [p.male,p.female,p.appearance,p.visual] {dat.read(id)?;}
        let expected:Vec<u8>=[0xbu32,p.physical,0x40000001,p.visual].into_iter().flat_map(u32::to_le_bytes).collect();
        if dat.read(p.physical)?!=expected {return Err(std::io::Error::other("Armor physics profile changed"));}
        result.insert(p.definition,Metadata{icon:p.appearance,physical:p.physical,equipment:None});
    }
    Ok(result)
}
fn profile(item:&Item,race:u32)->Option<&'static Profile> {
    PROFILES.iter().find(|p|p.race==race && p.definition==item.definition && p.visual==item.visual
        && p.slot==item.equipment_mask && item.quantity==1 && item.stack_limit==1)
}
pub fn slot(item:&Item,race:u32)->Option<u16> {profile(item,race).map(|p|p.slot)}
pub fn aspects(inventory:&Inventory,race:u32,sex:u32)->Vec<(u32,u32,f32)> {
    inventory.items().filter_map(|i|profile(i,race).filter(|p|i.location==Location::Equipped(p.slot)))
        .flat_map(|p|[(if sex==0x1000 {p.male}else{p.female},0x10,1.0),(if sex==0x1000 {p.male}else{p.female},2,0.0)]).collect()
}
pub fn armor(inventory:&Inventory,race:u32)->u32 {
    inventory.items().filter_map(|i|profile(i,race).filter(|p|i.location==Location::Equipped(p.slot))).map(|p|p.armor).sum()
}
pub fn drop(inventory:&Inventory,race:u32,id:u64,slot:u16)->Option<Item> {
    let p=PROFILES.iter().find(|p|p.race==race && !inventory.items().any(|i|i.definition==p.definition))?;
    Some(Item{id,definition:p.definition,visual:p.visual,quantity:1,stack_limit:1,equipment_mask:p.slot,location:Location::Bag(slot)})
}

#[cfg(test)] mod tests {
    use super::*;
    #[test] fn profiles_use_sex_specific_aspects_and_enforce_race() {
        for p in PROFILES {
            let i=Item{id:crate::inventory::ITEM_NAMESPACE+1,definition:p.definition,visual:p.visual,quantity:1,stack_limit:1,equipment_mask:p.slot,location:Location::Bag(0)};
            assert!(slot(&i,if p.race==1 {2}else{1}).is_none());
            let bag=Inventory::default().apply(&crate::inventory::Command::Grant(i.clone())).unwrap()
                .apply(&crate::inventory::Command::Wear{item:i.id,slot:i.equipment_mask}).unwrap();
            for (sex,table) in [(0x1000,p.male),(0x2000,p.female)] {
                assert_eq!(aspects(&bag,p.race,sex),vec![(table,0x10,1.0),(table,2,0.0)]);
            }
            assert_eq!(armor(&bag,p.race),p.armor);
        }
    }
}
