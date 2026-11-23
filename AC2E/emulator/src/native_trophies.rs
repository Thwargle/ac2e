//! Explicit NPC trophy slots from installed and identity-matched reference DAT properties. These probabilities
//! override observations of those same items; capture frequency is not drop rate.
use std::{collections::{BTreeMap,BTreeSet},io};
use serde::Deserialize;
use crate::{dat::Dat,inventory::{Item,Location},inventory_request::Metadata};

#[derive(Deserialize)]
struct File {schema:u32,rules:Vec<Rule>}
#[derive(Deserialize)]
struct Rule {entity:u32,sources:BTreeMap<String,String>,drops:Vec<Drop>}
#[derive(Deserialize)]
struct Drop {definition:u32,visual:u32,probability:f32,slot:u8}
#[derive(Deserialize)]
struct FoundryPack { quest_keys: Vec<FoundryKey> }
#[derive(Deserialize)]
struct FoundryKey {
    entity: u32, definition: u32, visual: u32, sources: BTreeMap<String,String>,
    corpse_min_level: u32, take_quest: u32, take_status: u32,
}
#[derive(Deserialize)]
struct ExcavationDrops { rules: Vec<ExcavationDrop> }
#[derive(Deserialize)]
struct ExcavationDrop {
    entity: u32, definition: u32, visual: u32, sources: BTreeMap<String,String>, probability: f32,
}

fn compatible(dat: &mut Dat, sources: &BTreeMap<String,String>) -> bool {
    !sources.is_empty() && sources.iter().all(|(key, expected)| {
        let Ok(did) = u32::from_str_radix(key, 16) else { return false };
        let Ok(bytes) = dat.read(did) else { return false };
        let hash = bytes.iter().fold(0xcbf29ce484222325u64, |h,b| (h ^ u64::from(*b)).wrapping_mul(0x100000001b3));
        format!("{hash:016x}") == *expected
    })
}

fn template_item(dat: &mut Dat, metadata: &mut BTreeMap<u32,Option<Metadata>>, definition: u32, visual: u32) -> Option<Item> {
    let info = match metadata.get(&definition).copied().flatten() {
        Some(info) => info,
        None => Metadata::load(dat, definition).ok()?,
    };
    dat.read(definition).ok()?;
    dat.read(visual).ok()?;
    let item = Item { roll: None, id: crate::inventory::ITEM_NAMESPACE + 1, definition, visual,
        quantity: 1, stack_limit: 1, equipment_mask: info.equipment.map_or(0, |e| e.valid as u16 & 0x7ff),
        location: Location::Bag(0) };
    crate::item_services::acquired(7, &item, info).ok()?;
    metadata.insert(definition, Some(info));
    Some(item)
}
#[derive(Default)]
pub(crate) struct Trophies {
    drops: BTreeMap<u32,Vec<(f32,Item)>>,
    excavation: BTreeMap<u32,(f32,Item)>,
    pub(crate) key_requirements: BTreeMap<u32,(u32,u32)>,
    pub(crate) corpse_levels: BTreeMap<u32,u32>,
}
impl Trophies {
    pub fn load(dat:&mut Dat,metadata:&mut BTreeMap<u32,Option<Metadata>>)->io::Result<Self> {
        let native: File = serde_json::from_str(include_str!("../content/native-trophies.json"))?;
        let translated: File = serde_json::from_str(include_str!("../content/hero-reference-trophies-v1.json"))?;
        if [&native,&translated].iter().any(|file| file.schema!=1 || file.rules.len()>16384) {
            return Err(io::Error::other("invalid trophy catalog"));
        }
        let native_entities: BTreeSet<_> = native.rules.iter().map(|r| r.entity).collect();
        let secondary = translated.rules.into_iter().filter(|r| !native_entities.contains(&r.entity));
        let mut result=Self::default();let mut rejected=0;
        for rule in native.rules.into_iter().chain(secondary) {
            if rule.entity>>24!=0x47 || rule.drops.len()>15 || rule.sources.is_empty() || rule.sources.len()>32 {
                return Err(io::Error::other("invalid trophy source"));
            }
            if !compatible(dat,&rule.sources) {rejected+=1;continue;}
            let mut slots=BTreeSet::new();let mut drops=Vec::new();
            for d in rule.drops {
                if !d.probability.is_finite() || !(0.0..=1.0).contains(&d.probability) || !(1..=15).contains(&d.slot)
                    || !slots.insert(d.slot) {return Err(io::Error::other("invalid trophy probability or slot"));}
                let Some(item)=template_item(dat,metadata,d.definition,d.visual) else{rejected+=1;continue};
                drops.push((d.probability,item));
            }
            if !drops.is_empty() && result.drops.insert(rule.entity,drops).is_some(){return Err(io::Error::other("duplicate trophy rule"));}
        }
        // The guide identifies one key per general; their separate DAT helmet slot keeps its native chance.
        let foundry: FoundryPack = serde_json::from_str(include_str!("../content/foundry-generals-v1.json"))?;
        for key in foundry.quest_keys {
            if !compatible(dat,&key.sources) { rejected += 1; continue; }
            let Some(item) = template_item(dat,metadata,key.definition,key.visual) else { rejected += 1; continue };
            let drops = result.drops.entry(key.entity).or_default();
            if drops.iter().any(|(_,i)| i.definition == key.definition) { return Err(io::Error::other("duplicate Foundry key")); }
            drops.push((1.,item));
            result.key_requirements.insert(key.definition,(key.take_quest,key.take_status));
            result.corpse_levels.insert(key.entity,key.corpse_min_level);
        }
        let excavation: ExcavationDrops = serde_json::from_str(include_str!("../content/cragstone-excavation-loot-v1.json"))?;
        for rule in excavation.rules {
            if !(0.0..=1.0).contains(&rule.probability) {
                return Err(io::Error::other("invalid excavation drop probability"));
            }
            if !compatible(dat, &rule.sources) { rejected += 1; continue; }
            let Some(item) = template_item(dat, metadata, rule.definition, rule.visual) else { rejected += 1; continue };
            if result.excavation.insert(rule.entity, (rule.probability,item)).is_some() {
                return Err(io::Error::other("duplicate excavation drop rule"));
            }
        }
        println!("Trophy and quest loot: {} compatible creature rules, {} excavation rules; {rejected} incompatible rules/items withheld.",result.drops.len(),result.excavation.len());
        Ok(result)
    }
    pub fn contains(&self,entity:u32,definition:u32)->bool {
        self.drops.get(&entity).is_some_and(|drops|drops.iter().any(|(_,item)|item.definition==definition))
    }
    pub fn apply(&self,entity:u32,items:&mut Vec<Item>,random:&mut u64) {
        let Some(drops)=self.drops.get(&entity) else{return};
        // A previously captured trophy must not turn a 5% native slot into 100%.
        items.retain(|item|!drops.iter().any(|(_,t)|t.definition==item.definition));
        let rolled=drops.iter().filter(|(chance,_)|*chance>0. && (*chance>=1. || crate::creature_ai::random(random)<*chance))
            .map(|(_,item)|item.clone()).collect::<Vec<_>>();
        items.truncate(32usize.saturating_sub(rolled.len()));items.extend(rolled);
    }
    pub fn apply_excavation(&self, entity: u32, cell: Option<u32>, items: &mut Vec<Item>, random: &mut u64) {
        // Older observed bundles can already contain a seal fragment. A fragment comes
        // only from its corresponding Excavation Drudge and gets one roll per kill.
        items.retain(|item| !matches!(item.definition, 0x470015d9 | 0x470015db | 0x470015dd));
        if !matches!(cell, Some(0x05f90100..=0x05f9fffd)) { return; }
        let Some((chance, item)) = self.excavation.get(&entity) else { return };
        items.retain(|existing| existing.definition != item.definition);
        if *chance >= 1.0 || crate::creature_ai::random(random) < *chance {
            items.truncate(31);
            items.push(item.clone());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn guide_trophies_use_verified_variant_ids_and_native_probabilities() {
        let Ok(directory) = std::env::var("AC2_TEST_GAME_DATA") else { return };
        let mut dat = Dat::open(std::path::Path::new(&directory).join("portal.dat")).unwrap();
        let mut metadata = BTreeMap::new();
        let catalog = Trophies::load(&mut dat,&mut metadata).unwrap();
        let (beard,heart,tooth) = (0x47000f7c,0x47000f7a,0x47000f7b);
        let definitions = [beard,heart,tooth];
        for entity in [0x470007a4,0x470013ae,0x470007bc,0x47000fc4,0x47001062,0x47002fc9,0x47000fc2,0x47001ad3] {
            let drops = &catalog.drops[&entity];
            assert_eq!(drops.len(), definitions.len());
            for ((probability,item),expected) in drops.iter().zip(definitions) {
                assert_eq!((*probability,item.definition),(0.0167,expected));
                assert!(metadata.contains_key(&expected));
            }
        }
        let jaw = &catalog.drops[&0x47002404];
        assert_eq!(jaw.len(),1);
        assert_eq!((jaw[0].0,jaw[0].1.definition),(0.05,0x470039b9));
        // The level 42 Raver has a different equipped club and no matching reference profile.
        assert!(!catalog.contains(0x47001063, beard));
    }
    #[test]
    fn installed_trophies_validate_against_dat_and_item_transport() {
        let Ok(directory)=std::env::var("AC2_TEST_GAME_DATA") else{return};
        let mut dat=Dat::open(std::path::Path::new(&directory).join("portal.dat")).unwrap();
        let mut metadata=BTreeMap::new();let catalog=Trophies::load(&mut dat,&mut metadata).unwrap();
        let first=catalog.drops.get(&0x470012b2).expect("native 5% trophy source missing");
        assert_eq!(first[0].0,0.05);assert_eq!(first[0].1.definition,0x470039b6);
        assert!(metadata.contains_key(&first[0].1.definition));
        for (boss,key) in [(0x4700390b,0x47003903),(0x47003bca,0x47003904),(0x47003b72,0x47003905),(0x47003b73,0x47003906)] {
            assert_eq!(catalog.key_requirements[&key],(0x80000115+(key-0x47003903),0x10000000));
            assert_eq!(catalog.corpse_levels[&boss],45);
            for seed in 1..=32 {
                let mut items=Vec::new();
                let mut rng=seed;
                catalog.apply(boss,&mut items,&mut rng);
                let keys=items.iter().filter(|i|(0x47003903..=0x47003906).contains(&i.definition)).collect::<Vec<_>>();
                assert_eq!(keys.len(),1);
                assert_eq!((keys[0].definition,keys[0].visual),(key,0x1f0010d4));
            }
        }
        for boss in [0x4700390b,0x47003bca] {
            assert!(catalog.drops[&boss].iter().any(|(chance,i)| *chance==0.05 && i.definition==0x470039b8));
        }
    }
    #[test]
    fn native_chance_replaces_captured_trophy_and_preserves_other_loot() {
        let trophy=crate::item_services::potion(1,0);let mut other=trophy.clone();other.definition+=1;
        let mut random=12345;let mut data=Trophies::default();let mut hits=0;
        data.drops.insert(0x470012b2,vec![(0.05,trophy.clone())]);
        for _ in 0..10000 {
            let mut items=vec![other.clone(),trophy.clone()];data.apply(0x470012b2,&mut items,&mut random);
            assert_eq!(items[0].definition,other.definition);hits+=items.len()-1;
        }
        assert!((400..600).contains(&hits),"{hits}");
        for (chance,count) in [(0.,1),(1.,2)] {
            data.drops.insert(1,vec![(chance,trophy.clone())]);
            let mut items=vec![other.clone(),trophy.clone(),trophy.clone()];data.apply(1,&mut items,&mut random);
            assert_eq!(items.len(),count);
        }
    }
}
