//! Native container transport for bounded, authored encounter rewards.
//! Unclaimed rewards expire with their encounter, and are not owned inventory.
//! The grant is journaled before the pending reward is removed.
use crate::{inventory::Item,script,world::WorldError};
use std::{collections::BTreeMap,io};

mod vault_glyphs;

pub const OPEN:u32=0x820e000c;
pub const CLOSE:u32=0x820e0047;
pub const TAKE:u32=0x820e0036;
pub const ALL:u32=0x820e0069;
pub const TRANSMUTE:u32=0x820e006e;
// Older Unreal builds used the descriptor constructors as request endpoints.
// Keep those bounded payloads readable while accepting the native endpoints.
const LEGACY_ALL:u32=0x820e0068;
const LEGACY_TRANSMUTE:u32=0x820e006c;
pub fn handles(f:u32,b:&[u8])->bool{
    [OPEN,CLOSE,ALL,TRANSMUTE,LEGACY_ALL,LEGACY_TRANSMUTE].contains(&f) || (f==TAKE && b.get(96..100).is_some_and(|v|u32::from_le_bytes(v.try_into().unwrap())>>16==0x8140))
}
#[derive(Debug,Clone,PartialEq,Eq)]
pub enum Request {Open(u64),Close(u64),Take{container:u64,player:u64,item:Option<u64>},Transmute{container:u64,player:u64}}
fn words(b:&[u8])->Vec<u32>{b.chunks_exact(4).map(|b|u32::from_le_bytes(b.try_into().unwrap())).collect()}
fn iid(w:&[u32],i:usize)->u64{u64::from(w[i])|(u64::from(w[i+1])<<32)}
fn write(out:&mut Vec<u8>,w:impl IntoIterator<Item=u32>){for x in w{out.extend(x.to_le_bytes());}}
pub fn decode(function:u32,b:&[u8])->Result<Request,WorldError>{
    let bad=WorldError::InvalidValue;
    let w=words(b);
    if function==TRANSMUTE || function==LEGACY_TRANSMUTE {
        if b.len()!=104 || w[..7]!=[5,1,1,0x44000100,0,0x234b0000,15]{return Err(bad)}
        let f=&w[7..22];let container=iid(f,8);let player=iid(f,13);
        let mut expected=[0u32;15];expected[2]=u32::MAX;expected[3]=1;expected[10]=u32::MAX;
        for i in [8,9,13,14]{expected[i]=f[i];}
        let tags=&b[88..103];
        let valid_tags=tags==[2,2,1,2,2,2,2,2,0x84,0xcd,1,2,2,0x84,0xcd]
            || tags==[2,2,1,2,2,2,2,2,0x82,0xcd,1,2,2,0x82,0xcd];
        if f!=expected || !valid_tags || b[103]!=0
            || [0,u64::MAX,player].contains(&container) || [0,u64::MAX].contains(&player){return Err(bad)}
        return Ok(Request::Transmute{container,player});
    }
    if function==OPEN || function==CLOSE {
        if b.len()!=12 || w[0]!=2 || [0,u64::MAX].contains(&iid(&w,1)){return Err(bad)}
        return Ok(if function==OPEN{Request::Open(iid(&w,1))}else{Request::Close(iid(&w,1))});
    }
    if function==ALL || function==LEGACY_ALL {
        if b.len()!=100 || w[..7]!=[5,1,1,0x44000100,0,0x20ef0000,14]{return Err(bad)}
        let f=&w[7..21];let container=iid(f,6);let player=iid(f,12);
        let mut expected=[0u32;14];expected[2]=1;expected[8]=u32::MAX;expected[11]=u32::MAX;
        for i in [6,7,12,13]{expected[i]=f[i];}
        let tags=&b[84..98];
        let valid_tags=tags==[2,2,2,2,2,2,0x84,0xcd,1,2,2,1,0x84,0xcd]
            || tags==[2,2,2,2,2,2,0x82,0xcd,1,2,2,1,0x82,0xcd];
        if f!=expected || !valid_tags || b[98..]!=[0,0]
            || [0,u64::MAX,player].contains(&container) || [0,u64::MAX].contains(&player){return Err(bad)}
        return Ok(Request::Take{container,player,item:None});
    }
    if function==TAKE {
        if b.len()!=284 || w[..7]!=[5,1,1,0x44000100,0,0x1f80000,51]{return Err(bad)}
        let f=&w[7..58];let player=iid(f,2);let container=iid(f,16);let item=iid(f,28);
        let mut expected=[0u32;51];
        // Native InvMoveDesc repeats fromSlot at fields 1 and 39. Corpse
        // inventory cells are zero-based; accepting only zero stranded every
        // item after the first (including appended quest rewards).
        if f[1]>=32 {return Err(bad)}
        expected[1]=f[1];expected[39]=f[1];
        for i in [0,10,13,23,25,26,35,47,49]{expected[i]=1;}
        for i in [11,24,30,50]{expected[i]=u32::MAX;}
        for i in [2,8,32]{expected[i]=player as u32;expected[i+1]=(player>>32) as u32;}
        for i in [16,36]{expected[i]=container as u32;expected[i+1]=(container>>32) as u32;}
        for i in [28,44]{expected[i]=item as u32;expected[i+1]=(item>>32) as u32;}
        const TAGS:[u8;51]=[2,2,0x82,0xcd,2,2,2,2,0x82,0xcd,2,2,2,2,2,2,0x82,0xcd,0x82,0xcd,2,2,2,2,2,2,2,2,0x82,0xcd,1,2,0x82,0xcd,2,2,0x82,0xcd,2,2,2,2,2,2,0x82,0xcd,2,2,2,2,2];
        // Retail InvMoveDesc writes object identities and slot/flag scalars as
        // unsigned values. Earlier bridge builds wrote the same values signed.
        let mut native_tags=TAGS;
        for i in [2,8,16,28,32,36,44]{native_tags[i]=0x84;}
        for i in [1,11,24,25,26,39,50]{native_tags[i]=4;}
        if f!=expected || (b[232..283]!=TAGS && b[232..283]!=native_tags) || b[283]!=0 || [0,u64::MAX,player].contains(&container)
            || [0,u64::MAX].contains(&player) || [0,u64::MAX,player,container].contains(&item){return Err(bad)}
        return Ok(Request::Take{container,player,item:Some(item)});
    }
    Err(bad)
}
fn list(ids:&[u64])->Vec<u8>{
    let mut b=Vec::new();write(&mut b,[5,1,1,0x44000100,0,0xffff003b,ids.len() as u32]);
    for id in ids{b.extend(id.to_le_bytes());}write(&mut b,[0]);b
}
pub fn opened(container:u64,item:Option<u64>)->Result<Vec<u8>,WorldError>{
    opened_items(container,&item.into_iter().collect::<Vec<_>>())
}
pub fn opened_items(container:u64,items:&[u64])->Result<Vec<u8>,WorldError>{
    if items.len()>32 {return Err(WorldError::LimitExceeded)}
    let mut b=script::integer(0);b.extend(list(&[]));b.extend(list(items));b.extend(script::identity(container));
    script::private_call(0x820e005d,&b)
}
pub fn closed(container:u64)->Result<Vec<u8>,WorldError>{
    let mut b=script::integer(0);b.extend(script::identity(container));script::private_call(0x820e0060,&b)
}
#[derive(Clone)]
pub struct Reward {pub owner:u64,pub items:Vec<Item>,pub expires:u64,pub quest_run:Option<u64>,recipients:Vec<u64>,reserved:Vec<u64>}
type Bundles = Vec<(u32, Vec<Item>)>;

fn loot_area(cell: u32) -> Option<u32> {
    match cell & 0xffff {
        1..=64 => Some(cell & 0xffff0000),
        0x100..=0xfffd => Some(cell),
        _ => None,
    }
}

fn observed_bundle<'a>(table: &'a Bundles, random: &mut u64) -> &'a [Item] {
    let total: u64 = table.iter().map(|(weight, _)| u64::from(*weight)).sum();
    let mut ticket = (f64::from(crate::creature_ai::random(random)) * total as f64) as u64;
    for (weight, items) in table {
        if ticket < u64::from(*weight) { return items; }
        ticket -= u64::from(*weight);
    }
    &[]
}

#[derive(Default)]
pub struct Catalog {
    supported: BTreeMap<u32, Item>,
    custom: BTreeMap<crate::world_rules::Target, Bundles>,
    tables: BTreeMap<u32, Bundles>,
    areas: BTreeMap<(u32, u32), Bundles>,
    profiles: BTreeMap<u32, Bundles>,
    // Ordinary captured items that only have evidence on their exact creature.
    exact_treasure: std::collections::BTreeSet<u32>,
    use_levels: BTreeMap<u32, u32>,
    trophies: crate::native_trophies::Trophies,
    limits: crate::loot_limits::Limits,
    weapons: std::collections::BTreeSet<u32>,
    treasure: Vec<Item>,
    levels: BTreeMap<u32, u16>,
}
impl Catalog {
    pub(crate) fn foundry_key_requirement(&self, definition: u32) -> Option<(u32,u32)> {
        self.trophies.key_requirements.get(&definition).copied()
    }
    pub(crate) fn foundry_corpse_min_level(&self, entity: u32) -> Option<u32> {
        self.trophies.corpse_levels.get(&entity).copied()
    }
    pub fn load(dat:&mut crate::dat::Dat,metadata:&mut BTreeMap<u32,Option<crate::inventory_request::Metadata>>)->io::Result<Self> {
        Self::load_with_rules(dat,metadata,&crate::world_rules::Rules::default())
    }
    pub fn load_with_rules(dat:&mut crate::dat::Dat,metadata:&mut BTreeMap<u32,Option<crate::inventory_request::Metadata>>,rules:&crate::world_rules::Rules)->io::Result<Self> {
        let mut items=BTreeMap::new();let mut rejected=0;
        let mut catalog=Self::default();
        for line in include_str!("../content/loot-items.tsv").lines().skip(1).filter(|l|!l.is_empty() && !l.starts_with('#')) {
            let f=line.split('\t').collect::<Vec<_>>();if f.len()!=3{return Err(io::Error::other("loot item row"))}
            let hex=|s|u32::from_str_radix(s,16).map_err(io::Error::other);
            let definition=hex(f[0])?;let visual=hex(f[1])?;
            let qualities=crate::inventory_request::Metadata::load_with_ints(dat,definition).ok();
            let Some(info)=qualities.as_ref().map(|(info,_)|*info).or_else(||metadata.get(&definition).copied().flatten())
                else {rejected+=1;continue};
            let Ok(entity)=dat.read(definition) else{rejected+=1;continue};
            if dat.read(visual).is_err(){rejected+=1;continue}
            let item=Item{roll:None,id:crate::inventory::ITEM_NAMESPACE+1,definition,visual,quantity:1,stack_limit:1,
                equipment_mask:crate::apparel::recovered_slot(definition,visual).unwrap_or(info.equipment.map_or(0,|e|e.valid as u16 & 0x7ff)),location:crate::inventory::Location::Bag(0)};
            if crate::item_services::acquired(7,&item,info).is_err(){rejected+=1;continue}
            // gmEntityDesc flags 0x8b: id, entity type, data parent, and properties.
            // 0x47002689 is the installed parent of the captured uncut gems.
            let gem = entity.starts_with(&[0x8b,definition,0x40000002,0x47002689].map(u32::to_le_bytes).concat());
            let jewelry = info.implement == Some(1) && info.physical == 0x4700026a && info.icon == 0x20000021
                && info.equipment.is_some_and(|e| matches!((e.valid,e.preferred),(0x300,0x100)|(0x400,0x400)));
            let shield = info.implement == Some(16)
                && info.equipment.is_some_and(|e| (e.valid,e.preferred,e.primary,e.secondary) == (0x80,0x80,2,1));
            let minimum=qualities.as_ref().and_then(|(_,ints)|ints.get(&0x2bc)).copied().unwrap_or(1);
            if minimum>1 {catalog.use_levels.insert(definition,minimum);}
            let unrestricted_equipment = (jewelry || shield) && minimum==1 && qualities.is_some_and(|(_,ints)|
                !ints.keys().any(|k| matches!(*k,0x2bd..=0x2cc|0x2d8)));
            if gem || unrestricted_equipment {
                catalog.exact_treasure.insert(definition);
            }
            metadata.insert(definition,Some(info));items.insert(definition,item);
        }
        let mut missing=0;
        for line in include_str!("../content/observed-loot.tsv").lines().skip(1).filter(|l|!l.is_empty() && !l.starts_with('#')) {
            let f=line.split('\t').collect::<Vec<_>>();if f.len()!=5{return Err(io::Error::other("loot bundle row"))}
            let entity=u32::from_str_radix(f[0],16).map_err(io::Error::other)?;
            let profile=f[1].parse::<u32>().map_err(io::Error::other)?;let count=f[2].parse::<u32>().map_err(io::Error::other)?;
            let bundle=f[3].split(',').map(|id|u32::from_str_radix(id,16).ok().and_then(|id|items.get(&id).cloned())).collect::<Option<Vec<_>>>();
            let Some(bundle)=bundle else {missing+=1;continue};
            if !(1..=32).contains(&bundle.len()) || count==0 || count>100000{return Err(io::Error::other("loot bundle bounds"))}
            let mut area_total = 0u32;
            if f[4] != "-" {
                for entry in f[4].split(',') {
                    let (area, n) = entry.split_once(':').ok_or_else(||io::Error::other("loot area entry"))?;
                    let area = u32::from_str_radix(area, 16).map_err(io::Error::other)?;
                    let n = n.parse::<u32>().map_err(io::Error::other)?;
                    if n == 0 || n > count - area_total {
                        return Err(io::Error::other("loot area weight"));
                    }
                    area_total += n;
                    catalog.areas.entry((entity, area)).or_default().push((n, bundle.clone()));
                }
            }
            catalog.tables.entry(entity).or_default().push((count,bundle.clone()));
            if profile!=0 {catalog.profiles.entry(profile).or_default().push((count,bundle));}
        }
        println!("Observed loot: {} creature tables, {} native profiles, {} compatible items; {rejected} unsupported items and {missing} incomplete bundles withheld. Random affixes and retail weights remain unrecovered.",catalog.tables.len(),catalog.profiles.len(),items.len());
        // Only supported usable profiles enter the generic inferred treasure pool.
        // Captured quest/special items remain tied to their observed creature.
        catalog.weapons=crate::treasure_weapons::compatible(dat,metadata);
        catalog.treasure=items.values().filter(|i|catalog.weapons.contains(&i.definition) || (crate::apparel::recovered_slot(i.definition,i.visual).is_some() && crate::apparel::tradable(i))
            ||matches!(i.definition,crate::item_services::POTION_DEFINITION|crate::crafting::VIGOR_OUTPUT|crate::crafting::MUCOR)).cloned().collect();
        for line in include_str!("../content/creature-combat.tsv").lines()
            .chain(include_str!("../content/inferred-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/regional-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/wilderness-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/hero-wilderness-creature-combat-v2.tsv").lines())
            .chain(include_str!("../content/ac2hero-raid-combat-v1.tsv").lines())
            .chain(include_str!("../content/ac2hero-bestiary-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/arwic-mines-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/illuminated-cave-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/the-ossuary-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/quest-encounters-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/cragstone-excavation-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/foundry-creature-combat-v1.tsv").lines()) {
            let f=line.split('\t').collect::<Vec<_>>();if f.len()<7||!f[0].starts_with("47"){continue;}
            catalog.levels.insert(u32::from_str_radix(f[0],16).map_err(io::Error::other)?,f[6].parse::<u16>().map_err(io::Error::other)?.clamp(1,150));
        }
        catalog.trophies=crate::native_trophies::Trophies::load(dat,metadata)?;
        catalog.limits=crate::loot_limits::Limits::load(dat)?;
        catalog.supported=items;
        catalog.set_world_rules(rules)?;
        if !catalog.custom.is_empty() {println!("World rules: {} custom loot tables.",catalog.custom.len());}
        Ok(catalog)
    }
    /// Whether the DAT-backed catalog was loaded (corpse loot enabled).
    pub fn loaded(&self)->bool {!self.supported.is_empty()}
    /// Operator tables may use any validated loot item; others are refused and the
    /// current tables stay. Corpses already rolled keep their loot.
    pub fn set_world_rules(&mut self,rules:&crate::world_rules::Rules)->io::Result<()> {
        let mut custom=BTreeMap::new();
        for (&target,outcomes) in &rules.loot {
            let mut table=Vec::new();
            for outcome in outcomes {
                let bundle=outcome.items.iter().map(|id|self.supported.get(id).cloned().ok_or_else(||io::Error::other(
                    format!("world rule loot item {id:08x} is not a supported loot item (see content/loot-items.tsv)")))).collect::<io::Result<Vec<_>>>()?;
                table.push((outcome.weight,bundle));
            }
            custom.insert(target,table);
        }
        self.custom=custom;
        Ok(())
    }
    #[cfg(test)]
    fn roll(&self,container:u64,entity:u32,ai:crate::creature_ai::Profile,random:&mut u64)->Vec<Item> {
        self.roll_at_level(container,entity,None,None,ai,random)
    }
    fn roll_at_level(&self,container:u64,entity:u32,level:Option<u32>,cell:Option<u32>,ai:crate::creature_ai::Profile,random:&mut u64)->Vec<Item> {
        if ai.loot_proof{return vec![]}
        use crate::world_rules::Target;
        if let Some(table)=self.custom.get(&Target::Actor(container)).or_else(||self.custom.get(&Target::Entity(entity))) {
            // An authored table replaces every native/inferred source, trophies included.
            return observed_bundle(table,random).to_vec();
        }
        let level=level.map(|n|n.clamp(1,150) as u16).or_else(||self.levels.get(&entity).copied())
            .or_else(||self.limits.0.get(&entity).map(|q|q.level)).unwrap_or(1);
        let area = cell.and_then(loot_area).and_then(|a| self.areas.get(&(entity, a)));
        let mut items=if let Some(quantity)=self.limits.0.get(&entity) {
            // Native bounds apply to generic treasure, separately from trophy and
            // quest slots. The original quantity distribution remains unavailable.
            let count=quantity.roll(random);
            let mut drops=Vec::with_capacity(count);
            self.fill_generic(&mut drops,count,entity,level,area,ai,random);
            drops
        } else if self.levels.contains_key(&entity) {self.inferred_roll(entity,level,area,ai,random)}else{self.observed_roll(entity,area,ai,random)};
        items.retain(|i|self.eligible(i,level));
        self.roll_stats(&mut items,level,random);
        self.trophies.apply(entity,&mut items,random);
        self.trophies.apply_excavation(entity,cell,&mut items,random);
        items
    }
    fn inferred_roll(&self,entity:u32,level:u16,area:Option<&Bundles>,ai:crate::creature_ai::Profile,random:&mut u64)->Vec<Item> {
        use crate::creature_ai::random as draw;
        let mut output=Vec::new();
        // A sampled bundle is evidence for eligibility, not a fixed retail drop.
        // Inferred policy: one observed slot at 35%, plus 0..2 usable rolls.
        let mut observed=self.observed_roll(entity,area,ai,random);
        observed.retain(|i|self.eligible(i,level));
        if !observed.is_empty()&&draw(random)<0.35 {
            output.push(observed[(draw(random)*observed.len() as f32) as usize].clone());
        }
        let count=output.len()+usize::from(draw(random)<0.70)+usize::from(draw(random)<0.25);
        self.fill_generic(&mut output,count,entity,level,area,ai,random);
        output
    }

    fn fill_generic(&self, drops: &mut Vec<Item>, count: usize, entity: u32, level: u16,
                    area: Option<&Bundles>, ai: crate::creature_ai::Profile, random: &mut u64) {
        if drops.len() >= count { return; }
        let pool: Vec<_> = self.treasure.iter()
            .filter(|i| self.eligible(i,level) && !self.trophies.contains(entity,i.definition)).collect();
        // Each complete observation can supply more than one generic slot. Try an
        // observation from each successively broader source before using the global pool.
        for (table, exact) in [(area,true), (self.tables.get(&entity),true), (self.profiles.get(&ai.loot_profile),false)] {
            let Some(table)=table else {continue};
            let mut candidates: Vec<_> = observed_bundle(table, random).iter()
                .filter(|i| pool.iter().any(|p| p.definition == i.definition) ||
                    exact && self.exact_treasure.contains(&i.definition) && self.eligible(i,level)
                        && !self.trophies.contains(entity,i.definition)).collect();
            while !candidates.is_empty() {
                let index = (crate::creature_ai::random(random) * candidates.len() as f32) as usize;
                let item = candidates.swap_remove(index);
                if drops.iter().any(|other| other.definition == item.definition) { continue; }
                drops.push(item.clone());
                if drops.len() == count { return; }
            }
        }
        if pool.is_empty() { return; }
        while drops.len() < count {
            let mut item=pool[(crate::creature_ai::random(random) * pool.len() as f32) as usize];
            if drops.iter().any(|d| d.definition == item.definition) {
                let unused: Vec<_> = pool.iter().copied()
                    .filter(|p| !drops.iter().any(|d| d.definition == p.definition)).collect();
                if !unused.is_empty() {
                    item=unused[(crate::creature_ai::random(random) * unused.len() as f32) as usize];
                }
            }
            drops.push(item.clone());
        }
    }
    fn eligible(&self,item:&Item,level:u16)->bool {
        if self.use_levels.get(&item.definition).is_some_and(|required|*required>u32::from(level)) {
            return false;
        }
        if self.weapons.contains(&item.definition) {
            return crate::treasure_weapons::eligible_treasure(item,level);
        }
        // Recovered apparel profiles are all verified to have no use-level gate.
        // Fixed-stat equipment still cannot bypass the generated tier's budget.
        crate::apparel::item_armor(item.definition).is_none_or(|armor|armor<=u32::from(level)*2+2)
    }
    fn roll_stats(&self,items:&mut [Item],level:u16,random:&mut u64) {
        use crate::creature_ai::random as draw;
        for item in items {
            if self.weapons.contains(&item.definition) {
                crate::treasure_weapons::roll(item,level,random);
            } else if crate::apparel::recovered_slot(item.definition,item.visual).is_some() {
                let base=crate::apparel::item_armor(item.definition).unwrap_or(u32::from(level)*2+2);
                let armor=((base as f32)*(0.75+draw(random)*0.5)).round().clamp(1.,1000.) as u16;
                item.roll=Some(crate::inventory::Roll{armor,tint:(draw(random)*1001.) as u16,level,..Default::default()});
            }
        }
    }
    fn observed_roll(&self,entity:u32,area:Option<&Bundles>,ai:crate::creature_ai::Profile,random:&mut u64)->Vec<Item> {
        let exact=area.or_else(|| self.tables.get(&entity));
        let Some(table)=exact.or_else(||self.profiles.get(&ai.loot_profile)) else{return vec![]};
        let items = observed_bundle(table,random);
        if exact.is_some(){return items.to_vec()}
        // Siblings sharing a native profile can establish ordinary treasure only.
        items.iter().filter(|item|self.treasure.iter().any(|generic|
            generic.definition==item.definition)).cloned().collect()
    }
}
#[derive(Default)]
pub struct Loot {rewards:BTreeMap<u64,Reward>,sequence:u64,pub catalog:Catalog,random:u64}
impl Loot {
    /// Reproducible isolated integration runs; never accepted from a client.
    pub fn seed_for_test(&mut self,seed:u64)->io::Result<()> {
        if seed==0||!self.rewards.is_empty(){return Err(io::Error::other("invalid test loot seed"));}
        self.random=seed;Ok(())
    }
    pub fn add(&mut self,container:u64,owner:u64,now:u64,entity:u32,ai:crate::creature_ai::Profile)->usize {
        self.add_with_quest(container,owner,now,entity,ai,None)
    }
    pub fn add_with_quest(&mut self,container:u64,owner:u64,now:u64,entity:u32,ai:crate::creature_ai::Profile,quest:Option<Item>)->usize {
        self.add_with_quest_at_level(container,owner,now,entity,None,ai,quest,None)
    }
    pub fn add_with_quest_at_level(&mut self,container:u64,owner:u64,now:u64,entity:u32,level:Option<u32>,ai:crate::creature_ai::Profile,quest:Option<Item>,spawn:Option<crate::movement::Placement>)->usize {
        if let Some(reward)=self.rewards.get(&container).filter(|r|r.expires>now){return reward.items.len()}
        if self.random==0 {self.random=std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos() as u64 ^ container;}
        let mut items=self.catalog.roll_at_level(container,entity,level,spawn.map(|p|p.cell),ai,&mut self.random);
        items.retain(|i|!matches!(i.definition,crate::esper_camp::BONE|crate::warder_notebook::NOTEBOOK|crate::arwic_mines::HAMMER|crate::omishan_collection::RAWHIDE|crate::omishan_collection::LEATHER));
        if let Some(spawn) = spawn.filter(|_| !ai.loot_proof && self.catalog.loaded()) {
            use crate::world_rules::Target;
            if !self.catalog.custom.contains_key(&Target::Actor(container)) && !self.catalog.custom.contains_key(&Target::Entity(entity)) {
                items.retain(|item| !vault_glyphs::is_vault_glyph(item.definition));
                if let Some(glyph) = vault_glyphs::drop_for(entity, spawn, &mut self.random) {
                    items.truncate(31);
                    items.push(glyph);
                }
            }
        }
        let quest=quest.filter(|_|!ai.loot_proof);
        let definition=quest.as_ref().map(|item|item.definition);
        if let Some(item)=quest {items.retain(|i|i.definition!=item.definition);items.truncate(31);items.push(item);}
        let count=self.insert(container,owner,now,items);
        if let Some(reward)=self.rewards.get_mut(&container){reward.reserved=reward.items.iter().filter(|i|Some(i.definition)==definition).map(|i|i.id).collect();}
        count
    }
    fn insert(&mut self,container:u64,owner:u64,now:u64,mut items:Vec<Item>)->usize {
        self.rewards.retain(|_,r|r.expires>now);
        if let Some(reward)=self.rewards.get(&container){return reward.items.len()}
        if self.rewards.len()>=4096 || items.len()>32{return 0}
        for item in &mut items {self.sequence+=1;item.id=0x8191000000000000|self.sequence;}
        let count=items.len();self.rewards.insert(container,Reward{owner,items,expires:now.saturating_add(60000),quest_run:None,recipients:vec![owner],reserved:Vec::new()});count
    }
    pub fn bind_quest_run(&mut self,container:u64,started:u64) {
        if let Some(r)=self.rewards.get_mut(&container) {if r.quest_run.is_none(){r.quest_run=Some(started);}}
    }
    pub fn share(&mut self,container:u64,recipients:Vec<u64>){
        if let Some(r)=self.rewards.get_mut(&container){if !recipients.is_empty() && recipients.len()<=8 {r.recipients=recipients;}}
    }
    pub fn get(&self,container:u64,owner:u64,now:u64)->Option<Reward>{
        let r=self.rewards.get(&container).filter(|r|r.expires>now && (r.recipients.contains(&owner) || (r.owner==owner && !r.reserved.is_empty())))?;
        let mut result=r.clone();
        result.items.retain(|i|if r.reserved.contains(&i.id){r.owner==owner}else{r.recipients.contains(&owner)});
        if owner!=r.owner{result.quest_run=None;}
        Some(result)
    }
    pub fn inspect_item(&self,item:u64,owner:u64,now:u64,visible:impl Fn(u64)->bool)->Option<&Item>{
        self.rewards.iter().filter(|(container,r)|r.expires>now&&visible(**container))
            .find_map(|(_,r)|r.items.iter().find(|i|i.id==item && if r.reserved.contains(&i.id){r.owner==owner}else{r.recipients.contains(&owner)}))
    }
    pub fn contains(&self,container:u64,now:u64)->bool{self.rewards.get(&container).is_some_and(|r|r.expires>now)}
    pub fn remove(&mut self,container:u64){self.rewards.remove(&container);}
    pub fn claimed(&mut self,container:u64,item:u64) {
        if let Some(reward)=self.rewards.get_mut(&container){reward.items.retain(|i|i.id!=item);}
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn cragstone_excavation_drops_use_the_captured_carrier_and_room() {
        let Ok(directory) = std::env::var("AC2_TEST_GAME_DATA") else { return };
        let mut dat = crate::dat::Dat::open(std::path::Path::new(&directory).join("portal.dat")).unwrap();
        let mut metadata = BTreeMap::new();
        let catalog = Catalog::load(&mut dat, &mut metadata).unwrap();
        let profiles = crate::creature_ai::catalog().unwrap();
        let mut random = 42;
        for (entity, fragment, sampled_fraction) in [
            (0x470015bc, 0x470015d9, 6./15.), (0x470015bb, 0x470015db, 4./16.), (0x470015d3, 0x470015dd, 3./15.),
        ] {
            assert!(metadata.get(&fragment).is_some_and(Option::is_some));
            let mut hits = 0;
            for _ in 0..512 {
                let items = catalog.roll_at_level(7, entity, None, Some(0x05f90120), profiles[&entity], &mut random);
                let pieces: Vec<_> = items.iter().filter(|i| matches!(i.definition,0x470015d9|0x470015db|0x470015dd)).collect();
                assert!(pieces.len() <= 1 && pieces.iter().all(|i| i.definition == fragment));
                hits += pieces.len();
                let other = catalog.roll_at_level(7, entity, None, Some(0x07f30120), profiles[&entity], &mut random);
                assert!(other.iter().all(|i| !matches!(i.definition,0x470015d9|0x470015db|0x470015dd)));
            }
            assert!((hits as f64 / 512. - sampled_fraction).abs() < 0.09);
        }
        for _ in 0..32 {
            let items = catalog.roll_at_level(7, 0x47000fc0, None, Some(0x05f90146), profiles[&0x47000fc0], &mut random);
            assert_eq!(items.iter().filter(|item| item.definition == 0x47000f79).count(), 1);
        }
    }
    #[test]
    fn ac2hero_bestiary_drops_use_native_quantities_and_independent_trophies() {
        let Ok(directory) = std::env::var("AC2_TEST_GAME_DATA") else { return };
        let mut dat = crate::dat::Dat::open(std::path::Path::new(&directory).join("portal.dat")).unwrap();
        let catalog = Catalog::load(&mut dat, &mut BTreeMap::new()).unwrap();
        let profiles = crate::creature_ai::catalog().unwrap();
        for (entity, level, lo, hi) in [
            (0x47004319, 34, 0, 1), (0x4700431e, 41, 1, 2),
            (0x47003af6, 35, 0, 1), (0x47003a3c, 48, 3, 6),
        ] {
            assert_eq!(catalog.levels[&entity], level);
            let limits = catalog.limits.0[&entity];
            assert_eq!((limits.level, limits.minimum, limits.maximum), (level, lo, hi));
            let mut quantities = std::collections::BTreeSet::new();
            let mut trophies = std::collections::BTreeSet::new();
            let mut random = 42;
            for _ in 0..512 {
                let items = catalog.roll(7, entity, profiles[&entity], &mut random);
                let mut ordinary = 0;
                for item in items {
                    if catalog.trophies.contains(entity, item.definition) { trophies.insert(item.definition); }
                    else { ordinary += 1; }
                }
                quantities.insert(ordinary);
            }
            assert_eq!(quantities, (lo..=hi).collect());
            if entity == 0x47004319 {
                assert_eq!(trophies, [0x470039b3].into_iter().collect()); // Kallikan Tooth
            } else {
                assert_eq!(trophies.len(), 3, "native trophy slots for {entity:08x}");
            }
        }
    }
    #[test]
    fn shared_and_rotated_loot_cannot_steal_quest_items_or_duplicate_claims() {
        let mut loot=Loot::default();
        let potion=crate::item_services::potion(1,0);
        let quest=crate::warder_notebook::item(crate::warder_notebook::NOTEBOOK,1,0).unwrap();
        loot.insert(10,7,100,vec![potion,quest]);
        let ids:Vec<_>=loot.rewards[&10].items.iter().map(|i|i.id).collect();
        loot.rewards.get_mut(&10).unwrap().reserved=vec![ids[1]];
        loot.bind_quest_run(10,123);
        loot.share(10,vec![8]);
        assert_eq!(loot.get(10,7,101).unwrap().items[0].id,ids[1]);
        assert_eq!(loot.get(10,8,101).unwrap().items[0].id,ids[0]);
        assert_eq!(loot.get(10,8,101).unwrap().quest_run,None);
        assert!(loot.get(10,9,101).is_none());
        assert!(loot.inspect_item(ids[1],8,101,|_|true).is_none());
        loot.share(10,vec![7,8]);
        loot.claimed(10,ids[0]);
        assert!(loot.get(10,8,101).unwrap().items.is_empty());
        assert_eq!(loot.get(10,7,101).unwrap().items.len(),1);
        assert!(loot.get(10,7,60100).is_none());
    }
    #[test]
    fn notebook_is_quest_scoped_and_old_corpses_keep_their_attempt() {
        let entity=crate::warder_notebook::BANDIT;
        let notebook=crate::warder_notebook::item(crate::warder_notebook::NOTEBOOK,1,0).unwrap();
        let mut loot=Loot::default();
        // Captured/custom bundles must not bypass the active-task check.
        loot.catalog.custom.insert(crate::world_rules::Target::Entity(entity),vec![(1,vec![notebook.clone()])]);
        let ai=crate::creature_ai::Profile::default();
        assert_eq!(loot.add(10,7,100,entity,ai),0);
        assert_eq!(loot.add_with_quest(11,7,100,entity,ai,Some(notebook.clone())),1);
        loot.bind_quest_run(11,123);loot.bind_quest_run(11,456);
        assert_eq!(loot.get(11,7,101).unwrap().quest_run,Some(123));
        assert!(loot.get(11,8,101).is_none());
        assert!(loot.get(11,7,60100).is_none());
        let mut protected=ai;protected.loot_proof=true;
        assert_eq!(loot.add_with_quest(12,7,100,entity,protected,Some(notebook)),0);
    }
    #[test]
    fn native_quantity_uses_observed_generic_loot_and_shared_profiles_exclude_quest_items() {
        let mut catalog=Catalog::default();
        let potion=crate::item_services::potion(1,0);
        let mut alternate=potion.clone();alternate.definition=0x4700aaaa;
        let mut special=potion.clone();special.definition=0x4700bbbb;
        catalog.treasure=vec![potion.clone(),alternate.clone()];
        let sample=vec![(1,vec![alternate.clone(),special.clone()])];
        catalog.tables.insert(17,sample.clone());
        catalog.profiles.insert(12,sample);
        let ai=crate::creature_ai::Profile{loot_profile:12,..Default::default()};
        let mut random=17;
        for entity in [17,18] {
            catalog.limits.0.insert(entity,crate::loot_limits::Quantity{minimum:2,maximum:2,level:1});
            let mut varied=false;
            for _ in 0..50 {
                let items=catalog.roll(3,entity,ai,&mut random);
                assert_eq!(items.len(),2);
                assert!(items.iter().any(|i|i.definition==alternate.definition));
                assert!(items.iter().all(|i|i.definition!=special.definition));
                varied|=items.iter().any(|i|i.definition==potion.definition);
            }
            assert!(varied);
        }
        assert!(catalog.observed_roll(17,None,ai,&mut random).iter().any(|i|i.definition==special.definition));
        assert_eq!(catalog.observed_roll(18,None,ai,&mut random),vec![alternate]);
    }
    #[test]
    fn native_slots_prefer_home_area_and_restrict_exact_treasure_to_observed_species() {
        let make_item = |definition| Item { definition, ..crate::item_services::potion(1, 0) };
        let bundle = |ids: [u32; 2]| vec![(1, ids.map(make_item).to_vec())];
        let mut catalog = Catalog::default();
        catalog.treasure = [101, 103, 104].map(make_item).to_vec();
        catalog.exact_treasure.insert(102);
        catalog.areas.insert((17, 0x12340000), bundle([101, 102]));
        catalog.areas.insert((17, 0x12340102), bundle([101, 102]));
        catalog.tables.insert(17, bundle([103, 102]));
        catalog.profiles.insert(12, bundle([104, 102]));
        for entity in [17, 18] {
            catalog.limits.0.insert(entity, crate::loot_limits::Quantity { minimum: 2, maximum: 2, level: 5 });
        }
        let ai = crate::creature_ai::Profile { loot_profile: 12, ..Default::default() };
        let mut random = 18;
        for (entity, cell, expected) in [(17, 0x12340001, 101), (17, 0x12340040, 101),
            (17, 0x12340102, 101), (17, 0x12340103, 103), (17, 0x12440001, 103), (18, 0x12340001, 104)] {
            let items = catalog.roll_at_level(3, entity, None, Some(cell), ai, &mut random);
            let ids: std::collections::BTreeSet<_> = items.iter().map(|item| item.definition).collect();
            assert_eq!(ids.len(), 2);
            assert!(ids.contains(&expected));
            assert_eq!(ids.contains(&102), entity == 17);
        }
        catalog.use_levels.insert(102, 6);
        let items = catalog.roll_at_level(3,17,Some(5),Some(0x12340001),ai,&mut random);
        let ids: std::collections::BTreeSet<_> = items.iter().map(|item|item.definition).collect();
        assert_eq!(ids, [101,103].into_iter().collect());
    }
    #[test]
    fn native_quantity_overrides_fallback_but_not_lootproof_or_operator_tables() {
        let mut catalog=Catalog::default();let mut random=37;
        let ai=crate::creature_ai::Profile::default();
        catalog.treasure=vec![crate::item_services::potion(1,0)];
        catalog.tables.insert(17,vec![(1,vec![crate::item_services::potion(1,0)])]);
        catalog.limits.0.insert(17,crate::loot_limits::Quantity{minimum:3,maximum:6,level:1});
        let counts=(0..500).map(|_|catalog.roll(21,17,ai,&mut random).len()).collect::<std::collections::BTreeSet<_>>();
        assert_eq!(counts,(3..=6).collect());
        catalog.limits.0.insert(17,crate::loot_limits::Quantity{minimum:0,maximum:0,level:1});
        assert!(catalog.roll(21,17,ai,&mut random).is_empty(),"zero overrides a captured bundle");
        catalog.custom.insert(crate::world_rules::Target::Actor(21),vec![(1,vec![crate::item_services::potion(1,0)])]);
        assert_eq!(catalog.roll(21,17,ai,&mut random).len(),1,"operator table wins");
        let mut protected=ai;protected.loot_proof=true;
        assert!(catalog.roll(21,17,protected,&mut random).is_empty());
        catalog.custom.clear();catalog.treasure.clear();
        catalog.limits.0.insert(17,crate::loot_limits::Quantity{minimum:3,maximum:6,level:1});
        assert!(catalog.roll(21,17,ai,&mut random).is_empty(),"no fabricated fallback item");
    }
    #[test]
    fn installed_native_boss_quantity_is_used_and_rolled_stats_survive_claim_transport() {
        let Ok(directory)=std::env::var("AC2_TEST_GAME_DATA") else{return};
        let mut dat=crate::dat::Dat::open(std::path::Path::new(&directory).join("portal.dat")).unwrap();
        let catalog=Catalog::load(&mut dat,&mut BTreeMap::new()).unwrap();
        assert_eq!(catalog.limits.0.len(),312);
        assert_eq!(catalog.weapons.len(),69);
        assert_eq!(catalog.areas.len(),497);
        assert_eq!(catalog.exact_treasure.len(),45); // 24 uncut gems, 10 rings/talismans, 11 shields.
        for did in [0x47002567,0x470010cd,0x470010d8,0x47000ff3] {
            assert!(catalog.exact_treasure.contains(&did));
            assert!(!catalog.treasure.iter().any(|i|i.definition==did));
        }
        for did in [0x470010a3,0x470015b1,0x47003903] {
            assert!(!catalog.exact_treasure.contains(&did));
        }
        let hive=&catalog.supported[&0x470026bc];
        assert!(!catalog.eligible(hive,49));
        assert!(catalog.eligible(hive,50));
        let boss=0x4700390b; // Baron Colier: native 3..6 generic treasure slots.
        let quantity=catalog.limits.0.get(&boss).unwrap();assert_eq!((quantity.minimum,quantity.maximum),(3,6));
        let mut loot=Loot{catalog,random:42,..Default::default()};let mut seen=std::collections::BTreeSet::new();
        for corpse in 100..200 {
            loot.add(corpse,7,0,boss,crate::creature_ai::Profile::default());
            let reward=loot.get(corpse,7,0).unwrap();
            let normal=reward.items.iter().filter(|i|!loot.catalog.trophies.contains(boss,i.definition)).count();
            assert!((3..=6).contains(&normal));
            let original=reward.items.clone();let random=loot.random;
            assert_eq!(loot.add(corpse,8,1,boss,crate::creature_ai::Profile::default()),original.len());
            assert_eq!(loot.random,random);assert!(loot.get(corpse,8,1).is_none());
            for item in original {
                if let Some(roll)=item.roll {assert_eq!(roll.level,56);seen.insert((item.definition,roll.armor,roll.tint));
                    // Claiming replaces the temporary corpse IID with a journal-owned IID.
                    let mut owned=item.clone();owned.id=crate::inventory::ITEM_NAMESPACE+1;
                    let command=crate::inventory::Command::Grant(owned.clone());
                    let decoded=crate::inventory::Command::decode(&command.encode()).unwrap();
                    let inventory=crate::inventory::Inventory::default().apply(&decoded).unwrap();
                    assert_eq!(inventory.items().next(),Some(&owned));
                }
                loot.claimed(corpse,item.id);
            }
            assert_eq!(loot.add(corpse,7,2,boss,crate::creature_ai::Profile::default()),0);
        }
        assert!(seen.len()>50,"gear must carry varied durable stats");
    }
    #[test]
    fn academy_loot_uses_encounter_level_and_excludes_powerful_fixed_gear() {
        let Ok(directory)=std::env::var("AC2_TEST_GAME_DATA") else{return};
        let mut dat=crate::dat::Dat::open(std::path::Path::new(&directory).join("portal.dat")).unwrap();
        let catalog=Catalog::load(&mut dat,&mut BTreeMap::new()).unwrap();
        let ai=crate::creature_ai::Profile::default();let mut random=53;let mut gear=0;
        assert_eq!(catalog.levels[&crate::quests::TARGET_ENTITY],24);
        for _ in 0..1000 {
            for item in catalog.roll_at_level(7,crate::quests::TARGET_ENTITY,Some(1),None,ai,&mut random) {
                if let Some(roll)=item.roll {
                    gear+=1;assert_eq!(roll.level,1);
                    assert!(roll.armor<=5 && roll.damage<=9,"starter stats: {roll:?}");
                }
            }
        }
        assert!(gear>100,"Starter mobs still provide varied usable equipment");
        let mut high=false;
        for _ in 0..100 {
            for item in catalog.roll(7,crate::quests::TARGET_ENTITY,ai,&mut random) {
                if let Some(roll)=item.roll {assert_eq!(roll.level,24);high|=roll.armor>5 || roll.damage>9;}
            }
        }
        assert!(high,"Outdoor idols retain their own loot tier");
    }
    #[test]
    fn quest_bonus_is_owned_once_and_never_replaces_normal_loot() {
        let mut loot=Loot::default();let ai=crate::creature_ai::Profile::default();
        let hammer=crate::arwic_mines::item(crate::arwic_mines::HAMMER,1,0,1).unwrap();
        loot.catalog.tables.insert(17,vec![(1,vec![crate::item_services::potion(1,0),hammer.clone()])]);
        assert_eq!(loot.add(20,7,100,17,ai),1);
        assert!(loot.get(20,7,100).unwrap().items.iter().all(|i|i.definition!=hammer.definition));
        assert!(crate::merchants::sale_value(&hammer).is_none());
        assert_eq!(loot.add_with_quest(21,7,100,17,ai,Some(hammer.clone())),2);
        let reward=loot.get(21,7,100).unwrap();
        let iid=reward.items.iter().find(|i|i.definition==hammer.definition).unwrap().id;
        assert!(loot.get(21,8,100).is_none());loot.claimed(21,iid);
        assert_eq!(loot.add_with_quest(21,8,101,17,ai,Some(hammer.clone())),1);
        assert!(!loot.get(21,7,101).unwrap().items.iter().any(|i|i.definition==hammer.definition));
        let mut protected=ai;protected.loot_proof=true;
        loot.add_with_quest(22,7,101,17,protected,Some(hammer));
        assert!(loot.get(22,7,101).unwrap().items.is_empty());
    }
    #[test]
    fn rewards_are_owned_temporary_and_one_time(){
        let mut loot=Loot::default();loot.insert(21,7,100,vec![crate::item_services::potion(1,0)]);
        assert!(loot.get(21,8,200).is_none());assert!(loot.get(21,7,60100).is_none());
        assert!(loot.get(21,7,200).is_some());loot.remove(21);assert!(loot.get(21,7,200).is_none());
    }
    #[test]
    fn malformed_container_requests_cannot_supply_inventory_authority(){
        for f in [OPEN,CLOSE,TAKE,ALL,TRANSMUTE,LEGACY_ALL,LEGACY_TRANSMUTE] {for n in 0..300{assert!(decode(f,&vec![0;n]).is_err());}}
        let b=script::identity(21);assert_eq!(decode(OPEN,&b).unwrap(),Request::Open(21));
        assert!(decode(OPEN,&script::identity(0)).is_err());
    }
    #[test]
    fn native_dat_constructor_requests_decode_without_loosening_authority() {
        // Serialized by the stock DAT's native WLib constructors, independently
        // of this decoder/bridge. Only the allocated graph ID is normalized.
        let fixture=|hex:&str|hex.trim().as_bytes().chunks_exact(2)
            .map(|v|u8::from_str_radix(std::str::from_utf8(v).unwrap(),16).unwrap()).collect::<Vec<_>>();
        let player=0x2000000000000456;
        let container=0x8000000000000123;
        let all=fixture(include_str!("../tests/fixtures/InvTakeAllDesc.txt"));
        let transmute=fixture(include_str!("../tests/fixtures/InvTransmuteAllDesc.txt"));
        let take=fixture(include_str!("../tests/fixtures/InvMoveDesc.txt"));
        assert!(handles(ALL,&all) && handles(TRANSMUTE,&transmute));
        assert_eq!(decode(ALL,&all).unwrap(),Request::Take{container,player,item:None});
        assert_eq!(decode(TRANSMUTE,&transmute).unwrap(),Request::Transmute{container,player});
        assert_eq!(decode(TAKE,&take).unwrap(),Request::Take{container:0x8000000000000789,player,item:Some(container)});
        // Amount/price/flags and duplicated identities are still authoritative.
        for (function,b,fields) in [(ALL,&all,&[0,1,2,3,4,5,8,9,10,11][..]),
            (TRANSMUTE,&transmute,&[0,1,2,3,4,5,6,7,10,11,12][..]),
            (TAKE,&take,&[1,2,8,10,11,16,24,28,32,36,39,44,50][..])] {
            for &field in fields {
                let mut invalid=b.clone();invalid[28+field*4]^=1;
                assert!(decode(function,&invalid).is_err(),"{function:x} field {field}");
            }
            let mut invalid=b.clone();invalid.push(0);
            assert!(decode(function,&invalid).is_err());
        }
    }
    #[test]
    fn single_item_loot_accepts_all_native_source_slots_but_not_mismatched_copies() {
        let player=7u64;let corpse=0x8140000000000021u64;let item=0x8181000000000001u64;
        let packet=|slot:u32| {
            let mut b=Vec::new();write(&mut b,[5,1,1,0x44000100,0,0x1f80000,51]);
            let mut f=[0u32;51];
            for i in [0,10,13,23,25,26,35,47,49]{f[i]=1;}
            for i in [11,24,30,50]{f[i]=u32::MAX;}
            for (indices,id) in [(&[2,8,32][..],player),(&[16,36][..],corpse),(&[28,44][..],item)] {
                for &i in indices {f[i]=id as u32;f[i+1]=(id>>32) as u32;}
            }
            f[1]=slot;f[39]=slot;write(&mut b,f);
            b.extend([2,2,0x82,0xcd,2,2,2,2,0x82,0xcd,2,2,2,2,2,2,0x82,0xcd,0x82,0xcd,2,2,2,2,2,2,2,2,0x82,0xcd,1,2,0x82,0xcd,2,2,0x82,0xcd,2,2,2,2,2,2,0x82,0xcd,2,2,2,2,2,0]);b
        };
        for slot in [0,1,2,31] {
            let b=packet(slot);assert!(handles(TAKE,&b));
            assert_eq!(decode(TAKE,&b).unwrap(),Request::Take{container:corpse,player,item:Some(item)});
        }
        assert!(decode(TAKE,&packet(32)).is_err());
        assert!(decode(TAKE,&packet(u32::MAX)).is_err());
        let mut b=packet(2);b[(7+39)*4..(7+40)*4].copy_from_slice(&0u32.to_le_bytes());
        assert!(decode(TAKE,&b).is_err());
    }
    #[test]
    fn transmute_rejects_client_price_and_identity_tampering(){
        let mut b=Vec::new();write(&mut b,[5,1,1,0x44000100,0,0x234b0000,15]);
        write(&mut b,[0,0,u32::MAX,1,0,0,0,0,21,0,u32::MAX,0,0,7,0]);
        b.extend([2,2,1,2,2,2,2,2,0x82,0xcd,1,2,2,0x82,0xcd,0]);
        assert_eq!(decode(TRANSMUTE,&b).unwrap(),Request::Transmute{container:21,player:7});
        for slot in [0,1,4,5,6,7,10,11,12]{let mut tampered=b.clone();tampered[28+slot*4]^=1;assert!(decode(TRANSMUTE,&tampered).is_err());}
        b[28+8*4]=7;assert!(decode(TRANSMUTE,&b).is_err());
    }
    #[test]
    fn multi_item_claims_preserve_the_original_roll_and_owner() {
        let mut loot=Loot::default();
        let items=vec![crate::item_services::potion(1,0),crate::item_services::potion(2,1)];
        loot.catalog.tables.insert(17,vec![(1,items)]);
        let ai=crate::creature_ai::Profile::default();
        assert_eq!(loot.add(21,7,100,17,ai),2);
        let ids=loot.get(21,7,100).unwrap().items.iter().map(|i|i.id).collect::<Vec<_>>();
        assert_ne!(ids[0],ids[1]);
        loot.claimed(21,ids[0]);loot.claimed(21,ids[0]);
        // Repeated death/open notifications cannot reroll or steal a corpse.
        let random=loot.random;assert_eq!(loot.add(21,8,200,17,ai),1);assert_eq!(random,loot.random);
        assert!(loot.get(21,8,200).is_none());
        assert_eq!(loot.get(21,7,200).unwrap().items[0].id,ids[1]);
        loot.claimed(21,ids[1]);assert_eq!(loot.add(21,7,300,17,ai),0);
        assert!(loot.contains(21,300));assert!(loot.get(21,7,60100).is_none());
    }
    #[test]
    fn missing_profiles_do_not_substitute_potions_and_lootproof_is_respected() {
        let mut catalog=Catalog::default();let mut random=123;
        catalog.treasure.push(crate::item_services::potion(1,0));
        catalog.profiles.insert(411,vec![(1,vec![crate::item_services::potion(1,0)])]);
        let mut ai=crate::creature_ai::Profile::default();
        assert!(catalog.roll(0,17,ai,&mut random).is_empty());
        ai.loot_profile=411;assert_eq!(catalog.roll(0,17,ai,&mut random).len(),1);
        ai.loot_proof=true;assert!(catalog.roll(0,17,ai,&mut random).is_empty());
    }
    #[test]
    fn installed_trophy_rolls_without_observed_bundle_and_does_not_reroll_corpse() {
        let Ok(directory)=std::env::var("AC2_TEST_GAME_DATA") else{return};
        let mut dat=crate::dat::Dat::open(std::path::Path::new(&directory).join("portal.dat")).unwrap();
        let mut metadata=BTreeMap::new();let mut catalog=Catalog::load(&mut dat,&mut metadata).unwrap();
        catalog.tables.clear();catalog.profiles.clear();
        let ai=crate::creature_ai::Profile::default();let mut random=12345;let mut hits=0;
        for _ in 0..10000 {
            let items=catalog.roll(0,0x470012b2,ai,&mut random);
            assert!(items.iter().all(|i|i.definition==0x470039b6));hits+=items.len();
        }
        assert!((400..600).contains(&hits),"native 5% slot: {hits}");
        let mut protected=ai;protected.loot_proof=true;
        assert!(catalog.roll(0,0x470012b2,protected,&mut random).is_empty());
        let mut loot=Loot {catalog,random:12345,..Default::default()};
        let count=loot.add(21,7,100,0x470012b2,ai);let state=loot.random;
        assert_eq!(loot.add(21,8,200,0x470012b2,ai),count);
        assert_eq!(loot.random,state);assert!(loot.get(21,8,200).is_none());
    }
    #[test]
    fn inferred_vermin_loot_varies_and_armor_rolls_are_serializable() {
        let Ok(directory)=std::env::var("AC2_TEST_GAME_DATA") else{return};
        let mut dat=crate::dat::Dat::open(std::path::Path::new(&directory).join("portal.dat")).unwrap();
        let catalog=Catalog::load(&mut dat,&mut BTreeMap::new()).unwrap();
        assert!(!catalog.treasure.is_empty());
        let ai=crate::creature_ai::Profile::default();let mut random=55182;
        let mut shapes=std::collections::BTreeSet::new();let mut stats=std::collections::BTreeSet::new();
        for _ in 0..500 {
            let items=catalog.roll(0,0x47001640,ai,&mut random);
            shapes.insert(items.iter().map(|i|i.definition).collect::<Vec<_>>());
            for item in items {
                if let Some(roll)=item.roll {
                    stats.insert((roll.armor,roll.tint));
                    let command=crate::inventory::Command::Grant(item.clone());
                    let decoded=crate::inventory::Command::decode(&command.encode()).unwrap();
                    let restored=crate::inventory::Inventory::default().apply(&decoded).unwrap();
                    assert_eq!(restored.items().next().unwrap(),&item);
                }
            }
        }
        assert!(shapes.len()>20,"Only {} distinct bundles",shapes.len());
        assert!(stats.len()>100,"Only {} armor variants",stats.len());
        let mut protected=ai;protected.loot_proof=true;
        assert!(catalog.roll(0,0x47001640,protected,&mut random).is_empty());
    }
    #[test]
    fn network_loot_fixture_seed() {
        let Ok(directory)=std::env::var("AC2_TEST_GAME_DATA") else{return};
        let mut dat=crate::dat::Dat::open(std::path::Path::new(&directory).join("portal.dat")).unwrap();
        let catalog=Catalog::load(&mut dat,&mut BTreeMap::new()).unwrap();let ai=crate::creature_ai::catalog().unwrap();
        let mut state=5;
        let a=catalog.roll(0,0x470008bf,ai[&0x470008bf],&mut state);
        let b=catalog.roll(0,0x470008bf,ai[&0x470008bf],&mut state);
        state=5;let c=catalog.roll(0,0x47000fa2,ai[&0x47000fa2],&mut state);
        assert!(!a.is_empty()&&!b.is_empty()&&c.len()>=2,"UDP claim fixtures need claimable rolls with seed 5");
        let legionary=0x47003b1b;
        state=2088;
        let drops=catalog.roll(0,legionary,ai[&legionary],&mut state);
        assert_eq!(drops.iter().filter(|i|!catalog.trophies.contains(legionary,i.definition)).count(),4);
        assert!(drops.iter().any(|i|i.definition==0x47000f0b && (100..=136).contains(&i.roll.unwrap().damage)));
        assert!(drops.iter().any(|i|i.definition==0x470010da));
    }
}
