//! Build-7148 native examination request/profile subset. Never executes scripts.
use crate::{
    dat::Dat,
    script,
    world::{self, WorldError},
};
use std::{collections::BTreeMap, io, path::Path};

pub const REQUEST: u32 = 0x820a0007;
// Keep the profile below the bridge's 64 KiB bound, including UTF-16 literals.
pub const MAX_LINES: usize = 48;

/// Runtime inventory state, not inferred ownership or a visual's default stack.
pub fn item_lines(item:&crate::inventory::Item,location:&str)->Vec<String> {
    let mut lines=vec![location.to_owned(),format!("Quantity: {} / {}",item.quantity,item.stack_limit)];
    if let Some((_,value))=crate::merchants::sale_value(item){lines.insert(0,format!("Value: {} coins",value*u32::from(item.quantity)));}
    let slots=[(1,"Head"),(2,"Torso"),(4,"Back"),(8,"Legs"),(16,"Feet"),(32,"Hands"),(0x40,"Main hand"),(0x80,"Off hand"),(0x100,"Ring I"),(0x200,"Ring II"),(0x400,"Neck")];
    let mask=crate::apparel::recovered_slot(item.definition,item.visual).unwrap_or(item.equipment_mask);
    let names:Vec<_>=slots.iter().filter(|(slot,_)|mask&slot!=0).map(|(_,name)|*name).collect();
    if !names.is_empty(){lines.push(format!("Equipment: {}",names.join(", ")));}
    if let Some(armor)=item.roll.filter(|_| item.definition != crate::totem::TOTEM).map(|r|u32::from(r.armor)).or_else(||crate::apparel::item_armor(item.definition)).filter(|v|*v>0){lines.push(format!("Armor: {armor}"));}
    else if crate::apparel::required_race(item.definition).is_some(){lines.push("Armor rating unavailable.".into());}
    if let Some(r)=item.roll {lines.push(format!("{} level: {}",if r.crafted{"Crafted"}else{"Treasure"},r.level));}
    if let Some(damage) = crate::professions::item_weapon_damage(item) {
        lines.push(format!("Weapon damage: {damage}"));
    }
    if let Some(infusion) = crate::totem::infused(item) {
        lines.push(format!("{}. Extractable at a spellbinding workshop.", infusion.description()));
    }
    if let Some(infusion) = crate::totem::charge(item) {
        lines.push(format!("{}.", infusion.description()));
        lines.push("Use this totem on matching uninfused crafted equipment, or examine the equipment and type @transfer.".into());
    }
    if let Some(p)=crate::treasure_weapons::profile(item) {
        lines.push(format!("Required level: {}",p.minimum_level));
    }
    if item.definition==crate::arwic_mines::SWORD {lines.push("Return this quest sword to the Arwic Mines Overseer in your backpack.".into());}
    if crate::esper_camp::slayer_weapon(item) {
        lines.extend(["Required level: 6".into(),"Cadre Slayer: triple direct weapon damage against Drudge Outcast Cadre.".into()]);
    }
    if item.definition==crate::esper_camp::BONE {lines.push("Return to the Esper Warder in Millstone (37.5N, 19.9W).".into());}
    if item.definition==crate::warder_notebook::NOTEBOOK {lines.push("Quest item: return to the Warder Scout near Molwirth (14.0N, 0.6E). Select the Scout and use the notebook, or speak to him to hand it over.".into());}
    if item.definition==crate::arwic_mines::GUIDE {lines.push("Double-click to read the next of 15 pages. Text appears in chat.".into());}
    if let Some(q)=crate::recovered_quests::use_profile(item.definition){lines.push(format!("Begins {}. Level {}+. Defeat {} targets for {} XP.",q.title,q.level,q.goal,q.xp));}
    if let Some(race)=crate::apparel::required_race(item.definition){lines.push(format!("Race: {}",match race{1=>"Human",2=>"Lugian",4=>"Tumerok",8=>"Drudge",16=>"Empyrean",_=>"Unknown"}));}
    if item.definition==crate::item_services::POTION_DEFINITION && item.visual==crate::item_services::POTION_VISUAL {
        lines.push(format!("Restores {} health.",crate::item_services::POTION_HEALTH));
        lines.push("Requires missing health. Potion recovery: 5 seconds.".into());
    } else if item.definition==crate::crafting::VIGOR_OUTPUT && item.visual==0x1f000e40 {
        lines.push("Restores 25 vigor.".into());
        lines.push("Requires missing vigor. Potion recovery: 5 seconds.".into());
    }
    if let Some(tool)=crate::harvesting::tool(item.definition){
        if let Some(skill)=crate::professions::skill(tool.skill){lines.push(format!("Gathering discipline: {}",skill.name));}
        lines.push(format!("Tool capacity: {} uses. Select a resource or corpse before using.",tool.durability));
    }
    if let Some(map)=crate::harvesting::map(item.definition){
        lines.push(format!("Survey map: search radius {} metres. Double-click to begin or continue surveying.",map.radius));
    }
    if let Some(skills)=crate::professions::creation_tool(item.definition) {
        let level=item.roll.map_or(1,|r|r.level);
        let used=item.roll.map_or(0,|r|r.wear);
        lines.push(format!("Creation tool: +{level} effective rank while equipped. Requires matching crafting rank {}. Durability: {} / 20.",u32::from(level)*10,20u16.saturating_sub(used)));
        lines.push(skills.iter().filter_map(|s|crate::professions::skill(*s).map(|s|s.name.as_str())).collect::<Vec<_>>().join(", "));
    }
    // Recipe roles are recovered data, not guessed item power or random rolls.
    let catalog=crate::professions::catalog();
    let mut disciplines=std::collections::BTreeSet::new();
    for recipe in &catalog.recipes {if recipe.ingredients.iter().any(|i|i.choices.contains(&item.definition)){
        if let Some(skill)=crate::professions::skill(recipe.skill){disciplines.insert(skill.name.as_str());}
    }}
    if !disciplines.is_empty(){lines.push(format!("Crafting material: {}",disciplines.into_iter().collect::<Vec<_>>().join(", ")));}
    lines
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Request {
    Object(u64),
    Clear,
}

/// Decode only the object and cleared request profiles proved by build 7148.
/// Clear/default construction (00609530 / 006095f0) sets type and admin to
/// zero; StreamPack 00609660 then emits those two words without a union value.
pub fn decode_request(bytes: &[u8]) -> Result<Request, WorldError> {
    if !matches!(bytes.len(), 36 | 44) {
        return Err(WorldError::InvalidValue);
    }
    let word = |offset: usize| u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap());
    if word(0) != 5
        || word(4) != 1
        || [0, u32::MAX].contains(&word(8))
        || word(12) != 0x44000100
        || word(16) != 0
        || word(20) != 0xffff009a
        || word(28) != 0
    {
        return Err(WorldError::InvalidValue);
    }
    match (word(24), bytes.len()) {
        (0, 36) if word(32) == 0 => Ok(Request::Clear),
        (0x40000002, 44) if word(40) == 0 => {
            let id = u64::from(word(32)) | (u64::from(word(36)) << 32);
            if [0, u64::MAX].contains(&id) {
                return Err(WorldError::InvalidValue);
            }
            Ok(Request::Object(id))
        }
        _ => Err(WorldError::InvalidValue),
    }
}

/// Legacy object-only entry point; clear requests are intentionally distinct.
pub fn decode(bytes: &[u8]) -> Result<u64, WorldError> {
    match decode_request(bytes)? {
        Request::Object(id) => Ok(id),
        Request::Clear => Err(WorldError::InvalidValue),
    }
}

#[derive(Clone, Debug)]
pub enum Name {
    Literal(String),
    Localized([u32; 3]),
    Corpse(Box<Name>),
}
impl Name {
    pub fn wire(&self) -> Result<Vec<u8>,WorldError> {
        match self {
            Self::Literal(s)=>world::literal_name(s),
            Self::Localized(words)=>Ok(words.iter().flat_map(|w|w.to_le_bytes()).collect()),
            Self::Corpse(name)=>{
                // Native localized corpse-name StringInfo and nested victim name.
                let mut bytes=Vec::new();
                for word in [0x509a8ff6u32,0x25000011,1,0xe4ea98e6,9] {bytes.extend(word.to_le_bytes());}
                bytes.extend(name.wire()?);Ok(bytes)
            }
        }
    }
}
#[derive(Default)]
pub struct Names {
    names: BTreeMap<u32, Option<Name>>,
    descriptions: BTreeMap<u32, (u32, String)>,
    icons: BTreeMap<u32, u32>,
    weapon_stats: BTreeMap<u32, WeaponStats>,
}
#[derive(Default)]
struct WeaponStats {
    implement: Option<u32>,
    damage: Option<u32>,
    speed: Option<u32>,
    vigor_cost: Option<u32>,
    combat_delay: Option<u32>,
    precluded_slots: u32,
}
impl Names {
    pub fn item_lines(&self,item:&crate::inventory::Item,location:&str)->Vec<String> {
        let mut lines=item_lines(item,location);
        if let Some(stats) = self.weapon_stats.get(&item.definition) {
            if let Some(kind) = stats.implement.and_then(crate::abilities::item_implement_name) {
                lines.push(format!("Weapon class: {kind}"));
            }
            if crate::professions::item_weapon_damage(item).is_none() {
                if let Some(value) = stats.damage { lines.push(format!("Weapon damage: {value}")); }
            }
            for (label,value) in [("Weapon speed",stats.speed),("Combat delay",stats.combat_delay),("Vigor cost",stats.vigor_cost)] {
                if let Some(value) = value { lines.push(format!("{label}: {value}")); }
            }
            // 0x113 lists valid locations. 0x11b separately precludes an off-hand item.
            if stats.precluded_slots & 0x80 != 0 { lines.push("Requires both hands.".into()); }
        }
        lines.extend(self.description_lines(item.definition));
        lines
    }
    pub(crate) fn load_weapon_stats(&mut self, directory: &Path, definitions: impl Iterator<Item=u32>) -> io::Result<()> {
        let mut portal = Dat::open(directory.join("portal.dat"))?;
        for definition in definitions {
            let Ok((metadata, ints)) = crate::inventory_request::Metadata::load_with_ints(&mut portal, definition) else {continue};
            if !metadata.equipment.is_some_and(|e| e.valid & 0xc0 != 0) {continue;}
            self.weapon_stats.insert(definition, WeaponStats {
                implement: metadata.implement,
                damage: ints.get(&0x258).copied(),
                speed: ints.get(&0x259).copied(),
                vigor_cost: ints.get(&0x261).copied(),
                combat_delay: ints.get(&0x263).copied(),
                precluded_slots: ints.get(&0x11b).copied().unwrap_or(0),
            });
        }
        Ok(())
    }
    pub fn load(directory: &Path, definitions: impl Iterator<Item = u32>) -> io::Result<Self> {
        let mut portal = Dat::open(directory.join("portal.dat"))?;
        let dataset=ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::for_retail_install(directory.to_path_buf())).map_err(io::Error::other)?;
        let tables=ac2_db::StoreStringTables::new(ac2_db::ResourceStore::new(dataset),0x58000000);
        let mut names = Self::default();
        for id in definitions {
            if names.names.contains_key(&id) {
                continue;
            }
            if names.names.len() >= 16384 {
                return Err(io::Error::other("examination name capacity exceeded"));
            }
            // An unavailable default name cannot prevent entering the world.
            // No substituted StringInfo or unsupported prefix is offset-guessed.
            let (name, icon) = Self::read(&mut portal, id).ok().map_or((None, None), |(name, icon)| (Some(name), icon));
            if let Some(icon) = icon { names.icons.insert(id, icon); }
            if let Some(Name::Localized([_,table,0]))=&name {
                // The native Description token is independent of the name. An
                // absent/empty/unresolved entry is not substituted with a name.
                let info=ac2_db::RenderInfo {string_id:0x6c459364,table_id:*table,..Default::default()};
                if let Ok(text)=ac2_db::inq_string(&tables,&info,false) {
                    let text=String::from_utf16_lossy(&text);
                    if !text.trim().is_empty() {names.descriptions.insert(id,(*table,text));}
                }
            }
            names.names.insert(id, name);
        }
        Ok(names)
    }
    pub fn get(&self, definition: u32, fallback: &str) -> Name {
        if definition==crate::esper_camp::GLYPH {return Name::Literal("Esper Glyph".into());}
        if definition==crate::arwic_mines::GLYPH {return Name::Literal(crate::arwic_mines::name(definition).unwrap().into());}
        self.names
            .get(&definition)
            .and_then(|name| name.clone())
            .unwrap_or_else(|| Name::Literal(fallback.to_owned()))
    }
    pub fn item_response(&self, target: u64, definition: u32, lines: &[String]) -> Result<Vec<u8>, WorldError> {
        pack_profile(target, self.get(definition, "Item"), lines, self.icons.get(&definition).copied(), true)
    }
    pub fn description_chat(&self, definition:u32)->Option<Result<Vec<u8>,WorldError>> {
        let (table,_)=self.descriptions.get(&definition)?;
        Some(script::localized(*table,0x6c459364).and_then(|mut args| {
            args.extend(script::integer(1)); script::private_call(script::CHAT_STRING,&args)
        }))
    }
    pub fn description_lines(&self,definition:u32)->Vec<String> {
        let Some((_,text))=self.descriptions.get(&definition) else{return Vec::new()};
        let mut lines=Vec::new();let mut line=String::new();
        for word in text.split_whitespace() {
            if word.len()>480 || word.chars().any(char::is_control){continue;}
            if line.len()+word.len()+1>480 {lines.push(std::mem::take(&mut line));if lines.len()==16{return lines;}}
            if !line.is_empty(){line.push(' ');}line.push_str(word);
        }
        if !line.is_empty(){lines.push(line);}lines
    }
    fn read(portal: &mut Dat, definition: u32) -> io::Result<(Name, Option<u32>)> {
        let invalid = || io::Error::other("unsupported definition name prefix");
        if definition >> 24 != 0x47 {
            return Err(invalid());
        }
        let did = 0x81000000 | (definition & 0xffffff);
        let bytes = portal.read(did)?;
        if bytes.len() > 65536 {
            return Err(invalid());
        }
        let mut pos = 0;
        let mut word = || -> io::Result<u32> {
            let data = bytes.get(pos..pos + 4).ok_or_else(invalid)?;
            pos += 4;
            Ok(u32::from_le_bytes(data.try_into().unwrap()))
        };
        if word()? & 1 == 0 || word()? != did {
            return Err(invalid());
        }
        let desc = word()?;
        if desc & 4 == 0 {
            return Err(invalid());
        }
        // Only these fields precede the name in gmWeenieDesc::StreamPack.
        for bit in [1, 0x2000000, 2] {
            if desc & bit != 0 {
                word()?;
            }
        }
        let name = [word()?, word()?, word()?];
        if name[0] == 0 || name[1] >> 24 != 0x25 || name[2] != 0 {
            return Err(invalid());
        }
        // The optional plural StringInfo precedes the descriptor image. A
        // substituted plural has a variable payload; retain the resolved name
        // and let the client use its visual layers when that payload is unknown.
        let icon = (|| -> Option<u32> {
            if desc & 16 == 0 { return None; }
            if desc & 8 != 0 {
                word().ok()?;
                word().ok()?;
                if word().ok()? != 0 { return None; }
            }
            let id = word().ok()?;
            (id >> 24 == 0x41).then_some(id)
        })();
        Ok((Name::Localized(name), icon))
    }
}

/// ExaminationProfile::StreamPack 00608380, node pack 00608490 and list pack
/// 00609140. The matching 7148 profile ends after its node list, unlike newer
/// retained capture profiles. HeapObject::Pack 006dbb00 adds zero field count.
pub fn response(target: u64, name: Name, lines: &[String]) -> Result<Vec<u8>, WorldError> {
    pack_profile(target, name, lines, None, false)
}

fn pack_profile(target: u64, name: Name, lines: &[String], icon: Option<u32>, item: bool) -> Result<Vec<u8>, WorldError> {
    if lines.len() > MAX_LINES
        || lines
            .iter()
            .any(|line| line.len() > 512 || line.chars().any(char::is_control))
    {
        return Err(WorldError::LimitExceeded);
    }
    // gmExaminationUI::ProcessNode gives every span its own DAT template.
    // Use the native cream/bold name style for numbers; do not assign a rarity
    // color or fabricate enchantment stats that the server has not recovered.
    fn text_node(name: Name, appearance: u32) -> Result<Vec<u8>, WorldError> {
        let mut out=Vec::new();
        for word in [0x40000004u32,2,appearance] {out.extend(word.to_le_bytes());}
        out.extend(name.wire()?);Ok(out)
    }
    let line_break=[0x40000006u32.to_le_bytes(),2u32.to_le_bytes()].concat();
    let mut nodes=Vec::new();
    if let Some(image)=icon {
        nodes.push([0x40000005u32.to_le_bytes(),2u32.to_le_bytes(),image.to_le_bytes()].concat());
    }
    let section = |line: &str| -> usize {
        if !item { return 0; }
        match line.split_once(": ").map(|(label, _)| label) {
            Some("Weapon class" | "Weapon damage" | "Weapon speed" | "Vigor cost" | "Combat delay") => 1,
            Some("Armor") => 2,
            Some("Cadre Slayer") => 3,
            Some("Required level" | "Race") => 4,
            _ => 0,
        }
    };
    let enchanted = lines.iter().any(|line| section(line)==3);
    nodes.push(text_node(name,if enchanted {0x6005ff6b} else {0xbe5b6513})?);
    nodes.push(line_break.clone());
    for (group,heading) in ["","Weapon Stats:","Armor Stats:","Permanent Enchantment Effect:","Requirements for Use:"].iter().enumerate() {
        let mut fields = lines.iter().filter(|line| section(line)==group).peekable();
        if fields.peek().is_none() {continue;}
        if group!=0 {
            nodes.push(line_break.clone());
            nodes.push(text_node(Name::Literal((*heading).into()),0)?);
            nodes.push(line_break.clone());
        }
        for line in fields {
            if group==3 {
                let (effect, description)=line.split_once(": ").unwrap();
                nodes.push(text_node(Name::Literal(format!("{effect}:")),0x22649835)?);
                nodes.push(line_break.clone());
                nodes.push([0x40000007u32.to_le_bytes(),2u32.to_le_bytes(),1u32.to_le_bytes()].concat());
                nodes.push(text_node(Name::Literal(description.into()),0)?);
                nodes.push(line_break.clone());
                continue;
            }
            if group!=0 {
                nodes.push([0x40000007u32.to_le_bytes(),2u32.to_le_bytes(),1u32.to_le_bytes()].concat());
            }
            if let Some((label,value))=line.split_once(": ").filter(|(_,value)|value.starts_with(|c:char|c.is_ascii_digit() || c=='-')) {
                nodes.push(text_node(Name::Literal(format!("{label}: ")),0)?);
                nodes.push(text_node(Name::Literal(value.to_owned()),0xbe5b6513)?);
            } else if !line.is_empty() {
                nodes.push(text_node(Name::Literal(line.clone()),0)?);
            }
            nodes.push(line_break.clone());
        }
    }
    let mut bytes = Vec::new();
    for word in [5,1,1,0x44000100,0,0xffff0099,0x40000002,0,target as u32,(target >> 32) as u32,nodes.len() as u32] {
        bytes.extend(word.to_le_bytes());
    }
    for node in nodes {bytes.extend(node);}
    bytes.extend(0u32.to_le_bytes());
    script::private_call(0x820a0009, &bytes)
}

#[cfg(test)]
mod item_tests {
    use super::*;
    use crate::inventory::{Item,Location,ITEM_NAMESPACE};
    #[test]
    fn rolled_skewer_uses_its_instance_damage_and_native_hand_qualities() {
        let Ok(directory) = std::env::var("AC2_TEST_GAME_DATA") else {return};
        let directory = Path::new(&directory);
        let item = Item {
            id: ITEM_NAMESPACE+1, definition: 0x47000f20, visual: 0x1f0002c4,
            quantity: 1, stack_limit: 1, equipment_mask: 0xc0, location: Location::Bag(0),
            roll: Some(crate::inventory::Roll {armor:0,tint:0,level:5,damage:14,crafted:false,wear:0}),
        };
        let mut names = Names::load(directory, [item.definition].into_iter()).unwrap();
        names.load_weapon_stats(directory, [item.definition].into_iter()).unwrap();
        for location in ["In the loot container.", "In your inventory."] {
            let lines = names.item_lines(&item, location);
            for expected in [location, "Weapon class: an axe", "Weapon damage: 14", "Treasure level: 5", "Required level: 1"] {
                assert!(lines.iter().any(|line| line == expected), "{lines:?}");
            }
            assert!(!lines.iter().any(|line| line.contains("both hands")));
            assert!(names.item_response(item.id,item.definition,&lines).is_ok());
        }
    }
    #[test]
    fn numeric_stats_use_independent_native_bold_spans() {
        let lines=vec!["Health: 12 / 20".into(),"".into(),"Armor: 9".into(),"Recovered description.".into()];
        let bytes=response(42,Name::Literal("Example".into()),&lines).unwrap();
        let header=|appearance:u32|[0x40000004u32.to_le_bytes(),2u32.to_le_bytes(),appearance.to_le_bytes()].concat();
        assert_eq!(bytes.windows(12).filter(|b|*b==header(0xbe5b6513)).count(),3,"name and two values retain bold DAT appearance");
        assert_eq!(bytes.windows(12).filter(|b|*b==header(0)).count(),3,"labels and description retain normal DAT appearance");
        let breaks=[0x40000006u32.to_le_bytes(),2u32.to_le_bytes()].concat();
        let blank_line=[breaks.clone(),breaks].concat();
        assert!(bytes.windows(16).any(|b|b==blank_line),"blank section break survives packing");
        let value=world::literal_name("12 / 20").unwrap();
        assert!(bytes.windows(value.len()).any(|b|b==value),"authoritative stat remains unchanged");
    }
    #[test]
    fn item_profile_decodes_into_a_native_icon_title_and_indented_stats() {
        use ac2_net::protocol::{ExaminationValue, ScriptMessage};
        let mut names = Names::default();
        let definition = 0x47001234;
        names.icons.insert(definition, 0x41000123);
        let raw = names.item_response(42, definition, &["Armor: 9".into(), "Race: Human".into()]).unwrap();
        // The Legions decoder used by the bridge has two additional zero words
        // compared with the 7148 object request and profile sent by AC2E.
        let mut args = raw[12..].to_vec();
        args.splice(40..40, 0u32.to_le_bytes());
        let heap = args.len()-4;
        args.splice(heap..heap, 0u32.to_le_bytes());
        let ScriptMessage::ExaminationProfile(profile) = ScriptMessage::decode(0x820a0009, &args).unwrap().unwrap() else {panic!("expected item profile")};
        assert!(matches!(profile.nodes[0].value, ExaminationValue::Image(0x41000123)));
        assert!(matches!(profile.nodes[1].value, ExaminationValue::Text(_)));
        assert!(matches!(profile.nodes[2].value, ExaminationValue::Break));
        assert_eq!(profile.nodes.iter().filter(|n|matches!(n.value, ExaminationValue::Tabs(1))).count(),2);
        for heading in ["Armor Stats:", "Requirements for Use:"] {
            let literal=world::literal_name(heading).unwrap();
            assert!(raw.windows(literal.len()).any(|span| span==literal));
        }
    }
    #[test]
    fn long_descriptions_and_runtime_fields_survive_examination() {
        let item=crate::item_services::potion(ITEM_NAMESPACE+1,0);
        let mut names=Names::default();let description="A recovered description with useful quest instructions. ".repeat(50);
        names.descriptions.insert(item.definition,(0x25000001,description.clone()));
        let recovered=names.description_lines(item.definition).join(" ");
        assert_eq!(recovered,description.trim());
        let mut lines=names.item_lines(&item,"In your inventory.");
        lines.extend((0..15).map(|n|format!("Runtime field {n}")));
        assert!(lines.len()>12);
        let bytes=response(item.id,Name::Literal("Known item".into()),&lines).unwrap();
        assert!(bytes.len()<65536);
        assert!(response(item.id,Name::Literal("Item".into()),&vec!["x".into();MAX_LINES+1]).is_err());
    }
    #[test]
    fn inventory_profiles_include_descriptions_and_only_supported_effects() {
        let mut names=Names::default();
        let mut item=Item {roll: None, id:ITEM_NAMESPACE+1,definition:crate::item_services::POTION_DEFINITION,
            visual:crate::item_services::POTION_VISUAL,quantity:1,stack_limit:1,equipment_mask:0,location:Location::Bag(0)};
        names.descriptions.insert(item.definition,(0x25000001,"Native item description.".into()));
        let lines=names.item_lines(&item,"In your inventory.");
        assert!(lines.iter().any(|s|s.contains("Restores 25 health")));
        assert!(lines.iter().any(|s|s=="Native item description."));
        assert!(response(item.id,Name::Literal("Potion".into()),&lines).is_ok());
        item.definition=0x47000f43;item.visual=0x1f000e52;
        let lines=names.item_lines(&item,"In your inventory.");
        assert!(!lines.iter().any(|s|s.contains("Restores")),"Slayer elixirs are not healing potions");
        item.definition=0x47000fdc;item.visual=0x1f00058c;
        let lines=names.item_lines(&item,"In the loot container.");
        assert!(lines.iter().any(|s|s=="Equipment: Feet"));
        assert!(!lines.iter().any(|s|s.starts_with("Armor:")),"Rolled armor cannot be inferred from a boot mesh");
    }
}
