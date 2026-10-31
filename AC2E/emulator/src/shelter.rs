//! Recovered Shelter tutorial introduction. Text and NPC identities are native;
//! the three introduction transitions are a reconstructed, versioned server rule.
//! Later inventory exchanges are atomic s1 journal records. Locations and balance
//! are explicit AC2E reconstruction where original server scripts are unavailable.
use crate::{quests::{Book, Command}, script, world::WorldError};

pub const GUARD: u32 = 0x4700157d;
pub const LIEUTENANT: u32 = 0x4700157e;
pub const SENTRY: u32 = 0x47001581;
pub const SCOUT: u32 = 0x47001580;
pub const ADVANCE: u32 = 0x4700157f;
pub const SHARD: u32 = 0x47002448;
pub const TOOL: u32 = 0x47001587;
pub const GLYPH: u32 = 0x47001582;
pub const DIGGER: u32 = 0x4700158b;
pub const SENTINEL: u32 = 0x4700158c;
pub const PORTAL: u64 = 0x8140000000030047;
// Clear arrival beside the scout, outside the authored seven-meter Menhir Shard.
// Verified against the native room, shard collision, and the largest avatar capsule.
pub const VAULT: crate::movement::Placement = crate::movement::Placement {
    cell:0x03f40154, coordinates:[176.,62.,15.32], heading:0 };
pub fn recover_vault_arrival(point:crate::movement::Placement) -> crate::movement::Placement {
    let [x,y,z]=point.coordinates;
    if point.cell==VAULT.cell && (178.3..180.5).contains(&x) && (62.2..63.7).contains(&y) && (14.5..17.).contains(&z) {
        VAULT
    } else {point}
}
pub const JOURNAL: u32 = 0x4000ff05; // AC2E identity; original quest ID not recovered.
pub fn supported(entity: u32) -> bool {
    matches!(entity, GUARD | LIEUTENANT | SENTRY | SCOUT | ADVANCE | SHARD | 0x47001573..=0x4700157c)
}
pub fn guidance(phase: u8) -> &'static str {
    match phase {
        1 => "Rescue the Shelter Hold: read the training stones, then speak to the Shelter Lieutenant.",
        2 => "Rescue the Shelter Hold: continue through the training stones to the Shelter Sentry.",
        3 => "Defeat a Drudge Digger, collect its Imbued Digging Tool, then speak to the Shelter Scout. Leave room in your bag for three potions.",
        4 => "Defeat a Drudge Sentinel for its Menhir Glyph. Target the Menhir Portal and use the glyph from your backpack.",
        5 => "Speak to the Advance Shelter Scout inside the Vault.",
        6 => "Use the Menhir Shard to complete Rescue the Shelter Hold.",
        7 => "Rescue the Shelter Hold complete. Use the exit portal to enter Dereth.",
        _ => "Speak to the Shelter Guard to begin Rescue the Shelter Hold.",
    }
}
/// Return the exact StringInfo table/key and optional once-only state change.
pub fn introduction(book: &Book, entity: u32) -> Option<(u32, u32, Option<Command>)> {
    let phase = book.shelter_phase();
    Some(match entity {
        GUARD => (0x25001648, if phase == 0 {0x2824fd4c} else {0x63691d8f},
            (phase == 0).then_some(Command::ShelterGuard)),
        LIEUTENANT if phase == 0 => (0x25001649, 0x8b6b186d, None),
        LIEUTENANT => (0x25001649, if phase == 1 {0x8b6b186f} else {0x63691d8f},
            (phase == 1).then_some(Command::ShelterLieutenant)),
        SENTRY => (0x2500164c, match phase {0=>0x8b6b186d,1=>0x8b6b186f,2=>0x8b6b1869,_=>0x8b6b186b},
            (phase == 2).then_some(Command::ShelterSentry)),
        SCOUT => (0x2500164b, if phase < 4 {0x8b6b1865} else {0x8b6b1867}, None),
        ADVANCE => (0x2500164a, if phase == 5 {0x8b6b1863} else {0x63691d8f},
            (phase == 5).then_some(Command::ShelterAdvance)),
        SHARD if phase >= 6 => (0x2500164a, 0x8b6b187d,
            (phase == 6).then_some(Command::ShelterComplete)),
        0x47001573..=0x4700157c => (0x2500163e + entity - 0x47001573, 0x6c459364, None),
        _ => return None,
    })
}

struct ItemProfile { definition:u32, visual:u32, physical:u32, appearance:u32, hash:u64 }
const ITEMS: &[ItemProfile] = &[
    ItemProfile{definition:TOOL,visual:0x1f000771,physical:0x47000779,appearance:0x20000021,hash:0xb158fe7e464bd1aa},
    ItemProfile{definition:GLYPH,visual:0x1f0001cf,physical:0x4700026a,appearance:0,hash:0x5a95a5ca45673770},
    ItemProfile{definition:0x47001583,visual:0x1f0018de,physical:0x470034fe,appearance:0x20000268,hash:0xc5d340d20e895fbd},
    ItemProfile{definition:0x47001584,visual:0x1f0018df,physical:0x470034ff,appearance:0x20000268,hash:0x3957ba01e0a605fe},
    ItemProfile{definition:0x47001585,visual:0x1f0018e0,physical:0x47003500,appearance:0x20000268,hash:0xcfe0c57a8c55276b},
];
pub fn validate(dat:&mut crate::dat::Dat)->std::io::Result<std::collections::BTreeMap<u32,crate::inventory_request::Metadata>> {
    let mut result=std::collections::BTreeMap::new();
    for p in ITEMS {
        let b=dat.read(0x81000000|(p.definition&0xffffff))?;
        let hash=b.iter().fold(0xcbf29ce484222325u64,|h,b|(h^u64::from(*b)).wrapping_mul(0x100000001b3));
        if hash!=p.hash {return Err(std::io::Error::other("Shelter item DAT profile changed"));}
        dat.read(p.definition)?;dat.read(p.visual)?;dat.read(p.physical)?;
        if p.appearance!=0 {dat.read(p.appearance)?;}
        result.insert(p.definition,crate::inventory_request::Metadata{icon:p.appearance,physical:p.physical,equipment:None});
    }
    Ok(result)
}
pub fn item(definition:u32,id:u64,slot:u16)->Option<crate::inventory::Item> {
    let p=ITEMS.iter().find(|p|p.definition==definition)?;
    Some(crate::inventory::Item{roll:None,id,definition,visual:p.visual,quantity:1,stack_limit:1,equipment_mask:0,location:crate::inventory::Location::Bag(slot)})
}
pub fn owned(inventory:&crate::inventory::Inventory,definition:u32)->Option<u64> {
    inventory.items().find(|i|matches!(i.location,crate::inventory::Location::Bag(s) if item(definition,i.id,s).as_ref()==Some(i))).map(|i|i.id)
}
pub fn drop(book:&Book,inventory:&crate::inventory::Inventory,entity:u32)->Option<u32> {
    let definition=match (book.shelter_phase(),entity) {(3,DIGGER)=>TOOL,(4,SENTINEL)=>GLYPH,_=>return None};
    owned(inventory,definition).is_none().then_some(definition)
}
pub fn exchange(command:Command)->bool {
    matches!(command,Command::ShelterTool|Command::ShelterGlyph|Command::ShelterAdvance|Command::ShelterComplete)
}
/// Canonical exchange shared by request handling, durable writes and replay.
pub fn plan(book:&Book,inventory:&crate::inventory::Inventory,command:Command,first:u64)
    ->std::io::Result<(Vec<crate::inventory::Command>,Vec<crate::progression::Command>,Book)> {
    use crate::inventory::Command as I;
    if !exchange(command) {return Err(std::io::Error::other("Unknown Shelter exchange"));}
    let next=book.apply(&command).map_err(std::io::Error::other)?;
    let mut bag=inventory.clone();let mut items=Vec::new();let mut rewards=Vec::new();
    if matches!(command,Command::ShelterTool|Command::ShelterGlyph) {
        let id=owned(&bag,if command==Command::ShelterTool {TOOL}else{GLYPH})
            .ok_or_else(||std::io::Error::other("You do not have the required Shelter quest item."))?;
        let consume=I::ConsumeDense{item:id};bag=bag.apply(&consume).map_err(std::io::Error::other)?;items.push(consume);
    }
    if command==Command::ShelterTool {
        for (n,definition) in [0x47001583,0x47001584,0x47001585].into_iter().enumerate() {
            let slot=bag.free_slot().ok_or_else(||std::io::Error::other("Make room for three Scout potions."))?;
            let grant=I::Grant(item(definition,first.checked_add(n as u64).ok_or_else(||std::io::Error::other("Item identity exhausted"))?,slot).unwrap());
            bag=bag.apply(&grant).map_err(std::io::Error::other)?;items.push(grant);
        }
        rewards.push(crate::progression::Command::AwardXp(40));
    }
    if command==Command::ShelterComplete {rewards.push(crate::progression::Command::AwardXp(65));}
    Ok((items,rewards,next))
}
pub fn text(table: u32, key: u32) -> Result<Vec<u8>, WorldError> {
    let mut args = script::localized(table, key)?;
    args.extend(script::integer(1));
    script::private_call(script::CHAT_STRING, &args)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn vault_recovery_only_moves_the_old_obstructed_arrival() {
        let mut p=VAULT;p.coordinates=[178.730484,62.94236,15.121846];
        assert_eq!(recover_vault_arrival(p),VAULT);
        assert_eq!(recover_vault_arrival(VAULT),VAULT);
        p.coordinates=[178.,64.,15.25];assert_eq!(recover_vault_arrival(p),p);
        p.cell=0x02990109;p.coordinates=[178.730484,62.94236,15.121846];assert_eq!(recover_vault_arrival(p),p);
    }
    #[test]
    fn introductions_are_ordered_repeatable_and_keep_native_strings() {
        let mut book = Book::default();
        assert_eq!(introduction(&book,LIEUTENANT),Some((0x25001649,0x8b6b186d,None)));
        assert!(book.apply(&Command::ShelterSentry).is_err());
        for (npc,command,phase) in [(GUARD,Command::ShelterGuard,1),(LIEUTENANT,Command::ShelterLieutenant,2),(SENTRY,Command::ShelterSentry,3)] {
            assert_eq!(introduction(&book,npc).unwrap().2,Some(command));
            book=book.apply(&command).unwrap();
            assert_eq!(book.shelter_phase(),phase);
            assert_eq!(introduction(&book,npc).unwrap().2,None);
            assert!(book.apply(&command).is_err());
            assert_eq!(Command::decode(&command.encode()).unwrap(),command);
        }
        assert!(introduction(&book,0x47001587).is_none());
        assert_eq!(introduction(&book,0x4700157c),Some((0x25001647,0x6c459364,None)));
        assert!(!crate::quests::rewards(&Command::ShelterSentry).iter().next().is_some());
        assert!(book.list_graph().unwrap().windows(4).any(|v|v==JOURNAL.to_le_bytes()));
    }
}
