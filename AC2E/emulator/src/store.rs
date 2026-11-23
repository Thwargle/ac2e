//! Transactional local character journal with checked progression compaction.
//! A write is acknowledged only after fsync.
//! Corruption/torn tails stop startup rather than silently discarding characters.
use crate::movement::Placement;
use crate::world::CharacterCreationRequest;
use std::{
    collections::BTreeMap,
    fs::{self, File, OpenOptions},
    io::{self, Read, Write},
    path::{Path, PathBuf},
};

const MAX_JOURNAL_BYTES: u64 = 16 * 1024 * 1024;
const MAX_PHYSICAL_VALUES: usize = 256;
mod compaction;
mod drops;
pub use drops::DroppedItem;

fn npc_bounty_candidate(c: &Character, npc: u32, time: u64) -> io::Result<crate::recovered_quests::Book> {
    let bounty = crate::recovered_quests::npc_bounty(npc).ok_or_else(|| invalid("Unknown bounty NPC"))?;
    let level = c.progression.level().number;
    if !c.quests.recovered.offers_bounty(npc, level, time, c.inventory.free_slot().is_none()) {
        return Err(invalid("This bounty is unavailable."));
    }
    c.quests.recovered.accept(bounty.quest.id, level, time)
}

fn collection_candidate(c: &Character, npc: u32, offered: Option<u64>, time: u64, first: u64)
    -> io::Result<(crate::inventory::Inventory, crate::progression::Progression, crate::recovered_quests::Book)> {
    let plan = if crate::slavetaker::operation(npc) {crate::slavetaker::plan} else {crate::omishan_collection::plan};
    let (items, changes, book) = plan(&c.quests.recovered, &c.inventory, npc, offered, c.progression.level().number, time, first)?;
    let (bag, state) = if items.is_empty() && changes.is_empty() { (c.inventory.clone(), c.progression.clone()) }
        else { crate::player_transaction::candidate(&c.inventory, &c.progression, &items, &changes, Some(first))? };
    Ok((bag, state, book))
}

// Store the split in the same journal record as the kill. Replay does not
// depend on which characters are online or grouped after a restart.
fn encode_encounter_shares(shares: &[(u64, u64)]) -> String {
    shares.iter().map(|(id, xp)| format!("{id},{xp}")).collect::<Vec<_>>().join(";")
}

fn decode_encounter_shares(field: Option<&&str>) -> io::Result<Vec<(u64, u64)>> {
    let Some(field) = field else { return Ok(Vec::new()); };
    field.split(';').take(crate::groups::MAX_MEMBERS).map(|pair| {
        let (id, xp) = pair.split_once(',').ok_or_else(|| invalid("invalid fellowship XP record"))?;
        Ok((id.parse().map_err(|_| invalid("invalid fellowship XP recipient"))?,
            xp.parse().map_err(|_| invalid("invalid fellowship XP amount"))?))
    }).collect()
}

fn encounter_share_candidates(
    records: &BTreeMap<u64, Character>,
    killer: u64,
    shares: &[(u64, u64)],
) -> io::Result<Vec<(u64, crate::progression::Progression)>> {
    if shares.len() >= crate::groups::MAX_MEMBERS {
        return Err(invalid("too many fellowship XP recipients"));
    }
    let mut seen = std::collections::BTreeSet::new();
    shares.iter().map(|&(id, xp)| {
        if id == killer || !seen.insert(id) {
            return Err(invalid("duplicate fellowship XP recipient"));
        }
        let recipient = records.get(&id).filter(|c| !c.deleted && c.progression.alive())
            .ok_or_else(|| invalid("fellowship XP recipient unavailable"))?;
        let state = recipient.progression.apply(&crate::progression::Command::AwardXp(xp))
            .map_err(io::Error::other)?;
        Ok((id, state))
    }).collect()
}

fn publish_encounter_shares(
    records: &mut BTreeMap<u64, Character>,
    shares: Vec<(u64, crate::progression::Progression)>,
) {
    for (id, progression) in shares {
        records.get_mut(&id).unwrap().progression = progression;
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Character {
    pub id: u64,
    pub account: String,
    pub name: String,
    /// Exact ordered client appearance values; None for legacy CLI-created characters.
    pub creation: Option<CharacterCreationRequest>,
    pub deleted: bool,
    /// Durable avatar lifetime; never reused by reconnecting transports.
    pub instance: u16,
    pub ui: Option<crate::preferences::UiSettings>,
    pub hotbar: crate::preferences::Hotbar,
    pub gameplay_options: Option<crate::preferences::GameplayOptions>,
    pub inventory: crate::inventory::Inventory,
    pub progression: crate::progression::Progression,
    pub skills: crate::skills::Book,
    pub professions: crate::professions::Book,
    pub quests: crate::quests::Book,
    // Historical grant markers for journal replay; no live training mode.
    legacy_kit_received: bool,
    legacy_supplies_received: bool,
    pub starter_gear: bool,
    pub binding: Option<Placement>,
}

pub struct Store {
    file: File,
    journal_path: PathBuf,
    compact_after: u64,
    lock: PathBuf,
    pub characters: BTreeMap<u64, Character>,
    pub drops: BTreeMap<u64, DroppedItem>,
    poisoned: bool,
    positions_path: PathBuf,
    positions: BTreeMap<u64, Placement>,
    positions_dirty: bool,
    revision: u64,
}

fn invalid(msg: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, msg)
}
fn hash(bytes: &[u8]) -> u32 {
    bytes.iter().fold(2166136261u32, |h, b| {
        (h ^ u32::from(*b)).wrapping_mul(16777619)
    })
}
fn valid_name(s: &str) -> bool {
    !s.is_empty()
        && s.len() <= 32
        && s.bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b' ' || b == b'-')
}

impl Store {
    pub fn open(dir: &Path) -> io::Result<Self> {
        fs::create_dir_all(dir)?;
        let lock = dir.join("server.lock");
        let mut guard = OpenOptions::new().write(true).create_new(true).open(&lock)
            .map_err(|e| io::Error::new(e.kind(), format!("cannot lock {}: {e}; after a crash, verify no server is running before removing this lock", lock.display())))?;
        let result = (|| {
            writeln!(guard, "{}", std::process::id())?;
            guard.sync_all()?;
            let mut file = OpenOptions::new()
                .create(true)
                .read(true)
                .append(true)
                .open(dir.join("characters.journal"))?;
            if file.metadata()?.len() > MAX_JOURNAL_BYTES {
                return Err(invalid("journal exceeds 16 MiB limit"));
            }
            let mut bytes = Vec::new();
            file.read_to_end(&mut bytes)?;
            let journal = decode_journal_state(&bytes)?;
            let characters = journal.characters;
            let positions_path = dir.join("positions.snapshot");
            let (position_revision, mut positions) = match File::open(&positions_path) {
                Ok(file) => {
                    let mut bytes = Vec::new();
                    file.take(1024 * 1024 + 1).read_to_end(&mut bytes)?;
                    decode_position_snapshot(&bytes, &characters)?
                }
                Err(e) if e.kind() == io::ErrorKind::NotFound => (0, BTreeMap::new()),
                Err(e) => return Err(e),
            };
            if position_revision > journal.revision {
                return Err(invalid("position snapshot is newer than character journal"));
            }
            let mut positions_dirty = false;
            for (id, (revision, placement)) in journal.relocations {
                if revision > position_revision {
                    positions.insert(id, placement);
                    positions_dirty = true;
                }
            }
            Ok(Self {
                file,
                journal_path: dir.join("characters.journal"),
                compact_after: MAX_JOURNAL_BYTES / 2,
                lock: lock.clone(),
                characters,
                drops: journal.drops,
                poisoned: false,
                positions_path,
                positions,
                positions_dirty,
                revision: journal.revision,
            })
        })();
        if result.is_err() {
            drop(guard);
            let _ = fs::remove_file(lock);
        }
        result
    }
    pub fn create(&mut self, account: &str, name: &str) -> io::Result<Character> {
        self.create_record(account, name, None)
    }
    pub fn placement(&self, id: u64) -> Option<Placement> {
        self.positions.get(&id).copied()
    }
    pub fn next_item_id(&self) -> io::Result<u64> {
        self.characters
            .values()
            .map(|c| c.inventory.high_water())
            .max()
            .unwrap_or(crate::inventory::ITEM_NAMESPACE)
            .checked_add(1)
            .filter(|id| id >> 48 == crate::inventory::ITEM_NAMESPACE >> 48)
            .ok_or_else(|| invalid("item identity exhausted"))
    }
    pub fn grant_starter_gear(&mut self, account: &str, id: u64) -> io::Result<bool> {
        let character = self.characters.get(&id)
            .filter(|character| character.account == account && !character.deleted)
            .ok_or_else(|| invalid("starter gear character not owned"))?;
        if character.starter_gear { return Ok(false); }
        let request = character.creation.as_ref().ok_or_else(|| invalid("Starter gear needs a saved creation profile."))?;
        let first = self.next_item_id()?;
        let next = crate::starter_gear::candidate(request, &character.inventory, first)?;
        self.append_record(&format!("t4\t{id}\t{first}"))?;
        let character = self.characters.get_mut(&id).unwrap();
        character.inventory = next;
        character.starter_gear = true;
        character.legacy_kit_received = true;
        Ok(true)
    }
    pub fn ensure_starter_skills(&mut self, account: &str, id: u64) -> io::Result<bool> {
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted)
            .ok_or_else(|| invalid("starter skills require an owned character"))?;
        let Some((race, sex)) = character
            .creation
            .as_ref()
            .map(|request| (request.race, request.sex))
            .filter(|&(race, sex)| crate::appearance::body(race, sex).is_some())
        else {
            return Ok(false);
        };
        if character.skills.has_starter_skills(race, sex) || !character.progression.alive() {
            return Ok(false);
        }
        let skills = character
            .skills
            .with_starter_skills(&character.progression, race, sex)
            .map_err(io::Error::other)?;
        self.append_record(&format!("k5\t{id}"))?;
        self.characters.get_mut(&id).unwrap().skills = skills;
        Ok(true)
    }
    pub fn bind_lifestone(
        &mut self,
        account: &str,
        id: u64,
        placement: Placement,
    ) -> io::Result<()> {
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted && c.progression.alive())
            .ok_or_else(|| invalid("binding requires an owned living character"))?;
        if !placement.valid() {
            return Err(invalid("invalid lifestone bind point"));
        }
        let skills = character
            .skills
            .with_lifestone_recall(
                &character.progression,
                character
                    .creation
                    .as_ref()
                    .map_or(1, |request| request.race),
            )
            .map_err(io::Error::other)?;
        let quests = if character.quests.journey_phase() == 1 {
            character.quests.apply(&crate::quests::Command::BoundLifestone).map_err(io::Error::other)?
        } else { character.quests.clone() };
        if quests == character.quests && character.binding == Some(placement)
            && character
                .skills
                .get(crate::skills::LIFESTONE_RECALL)
                .is_some()
        {
            return Ok(());
        }
        let revision = if quests != character.quests { "l3" } else { "l2" };
        self.append_record(&format!("{revision}\t{id}\t{}", encode_placement(placement)))?;
        let character = self.characters.get_mut(&id).unwrap();
        character.binding = Some(placement);
        character.skills = skills;
        character.quests = quests;
        Ok(())
    }
    pub fn ensure_lifestone_recall(&mut self, account: &str, id: u64) -> io::Result<bool> {
        let character = self
            .characters
            .get(&id)
            .filter(|character| {
                character.account == account && !character.deleted && character.binding.is_some()
            })
            .ok_or_else(|| invalid("recall grant requires an owned bound character"))?;
        if character
            .skills
            .get(crate::skills::LIFESTONE_RECALL)
            .is_some()
        {
            return Ok(false);
        }
        let skills = character
            .skills
            .with_lifestone_recall(
                &character.progression,
                character
                    .creation
                    .as_ref()
                    .map_or(1, |request| request.race),
            )
            .map_err(io::Error::other)?;
        self.append_record(&format!("k3\t{id}"))?;
        self.characters.get_mut(&id).unwrap().skills = skills;
        Ok(true)
    }
    /// Destination must have passed world/cell validation before this transaction.
    pub fn relocate(&mut self, account: &str, id: u64, placement: Placement) -> io::Result<()> {
        if !placement.valid()
            || !self.characters.get(&id).is_some_and(|character| {
                character.account == account && !character.deleted && character.progression.alive()
            })
        {
            return Err(invalid(
                "relocation requires an owned living character and valid placement",
            ));
        }
        self.append_record(&format!("p2\t{id}\t{}", encode_placement(placement)))?;
        self.positions.insert(id, placement);
        self.positions_dirty = true;
        Ok(())
    }
    pub fn relocate_training_portal(&mut self, account: &str, id: u64, placement: Placement, portal: u64) -> io::Result<()> {
        let character = self.characters.get(&id).filter(|c| c.account == account && !c.deleted && c.progression.alive()).ok_or_else(|| invalid("Travel requires an owned living character"))?;
        if portal == crate::shelter::HUMAN_QUICK_EXIT || crate::starters::shard_route(portal).is_some() {
            if !placement.valid() { return Err(invalid("Invalid Shelter exit destination")); }
            let (operation, result) = if portal == crate::shelter::HUMAN_QUICK_EXIT {
                ("sx1", crate::shelter::quick_exit(&character.quests, &character.progression))
            } else {
                ("sx2", crate::shelter::shard_exit(&character.quests, &character.progression))
            };
            let (book, progression) = result?;
            self.append_record(&format!("{operation}\t{id}\t{}", encode_placement(placement)))?;
            let saved = self.characters.get_mut(&id).unwrap();
            saved.quests = book;
            saved.progression = progression;
            self.positions.insert(id, placement);
            self.positions_dirty = true;
            return Ok(());
        }
        if character.quests.journey_phase() != 2 || !crate::quests::is_training_portal(portal) {
            return self.relocate(account, id, placement);
        }
        if !placement.valid() { return Err(invalid("Invalid training portal destination")); }
        let quests = character.quests.apply(&crate::quests::Command::Travelled).map_err(io::Error::other)?;
        self.append_record(&format!("p3\t{id}\t{portal:016x}\t{}", encode_placement(placement)))?;
        self.characters.get_mut(&id).unwrap().quests = quests;
        self.positions.insert(id, placement); self.positions_dirty = true;
        Ok(())
    }
    /// Binding must have passed world/cell validation before this transaction.
    pub fn return_to_binding(
        &mut self,
        account: &str,
        id: u64,
        resurrect: bool,
    ) -> io::Result<Placement> {
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted)
            .ok_or_else(|| invalid("recall character not owned"))?;
        let placement = character
            .binding
            .ok_or_else(|| invalid("character is not bound to a lifestone"))?;
        let next = if resurrect {
            character
                .progression
                .apply(&crate::progression::Command::Resurrect)
                .map_err(|_| invalid("resurrection requires a dead character"))?
        } else {
            if !character.progression.alive() {
                return Err(invalid("recall requires a living character"));
            }
            character.progression.clone()
        };
        self.append_record(&format!(
            "{}\t{id}\t{}",
            if resurrect { "r3" } else { "p1" },
            encode_placement(placement)
        ))?;
        self.characters.get_mut(&id).unwrap().progression = next;
        self.positions.insert(id, placement);
        self.positions_dirty = true;
        Ok(placement)
    }
    pub fn progression_command(
        &mut self,
        account: &str,
        id: u64,
        command: crate::progression::Command,
    ) -> io::Result<()> {
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted)
            .ok_or_else(|| invalid("progression character not owned"))?;
        let next = character
            .progression
            .apply(&command)
            .map_err(|error| invalid(&error.to_string()))?;
        if next == character.progression {
            return Ok(());
        }
        self.append_record(&format!("g1\t{id}\t{}", command.encode()))?;
        self.characters.get_mut(&id).unwrap().progression = next;
        Ok(())
    }
    pub fn raise_skill(&mut self, account: &str, id: u64, skill: u32) -> io::Result<()> {
        let character = self
            .characters
            .get(&id)
            .filter(|character| character.account == account && !character.deleted)
            .ok_or_else(|| invalid("skill advancement character not owned"))?;
        let race = character
            .creation
            .as_ref()
            .map_or(1, |request| request.race);
        let (skills, progression) = character
            .skills
            .advance(skill, &character.progression, race)
            .map_err(io::Error::other)?;
        let cost = character
            .progression
            .available_xp
            .checked_sub(progression.available_xp)
            .filter(|cost| *cost > 0)
            .ok_or_else(|| invalid("invalid advancement cost"))?;
        self.append_record(&format!("k2\t{id}\t{skill:x}\t{cost}"))?;
        let character = self.characters.get_mut(&id).unwrap();
        character.skills = skills;
        character.progression = progression;
        Ok(())
    }
    pub fn skill_command(
        &mut self,
        account: &str,
        id: u64,
        command: crate::skills::Command,
    ) -> io::Result<()> {
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted)
            .ok_or_else(|| invalid("skill character not owned"))?;
        let race = character
            .creation
            .as_ref()
            .map_or(1, |request| request.race);
        let next = character
            .skills
            .apply(command, &character.progression, race)
            .map_err(io::Error::other)?;
        self.append_record(&format!("k1\t{id}\t{}", command.encode()))?;
        self.characters.get_mut(&id).unwrap().skills = next;
        Ok(())
    }
    pub fn debit_untraining(&mut self,id:u64)->io::Result<bool> {
        let c=self.characters.get(&id).filter(|c|!c.deleted).ok_or_else(||invalid("untraining character unavailable"))?;
        let Some((skills,progression))=c.skills.debit_untraining(&c.progression).map_err(io::Error::other)? else{return Ok(false)};
        // Replay uses the persisted earned-XP total; no clock or client quantity.
        self.append_record(&format!("ku1\t{id}"))?;
        let c=self.characters.get_mut(&id).unwrap();c.skills=skills;c.progression=progression;Ok(true)
    }
    pub fn progression_batch(
        &mut self,
        account: &str,
        id: u64,
        commands: &[crate::progression::Command],
    ) -> io::Result<()> {
        if commands.is_empty() || commands.len() > 8 {
            return Err(invalid("progression batch size"));
        }
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted)
            .ok_or_else(|| invalid("progression character not owned"))?;
        let mut next = character.progression.clone();
        for command in commands {
            next = next
                .apply(command)
                .map_err(|error| invalid(&error.to_string()))?;
        }
        if next == character.progression {
            return Ok(());
        }
        let encoded = commands
            .iter()
            .map(crate::progression::Command::encode)
            .collect::<Vec<_>>()
            .join(";");
        self.append_record(&format!("g2\t{id}\t{encoded}"))?;
        self.characters.get_mut(&id).unwrap().progression = next;
        Ok(())
    }
    pub fn quest_command(
        &mut self,
        account: &str,
        id: u64,
        command: crate::quests::Command,
    ) -> io::Result<bool> {
        if crate::shelter::exchange(command) || matches!(command, crate::quests::Command::LeaveThroughQuickExit | crate::quests::Command::Kill(_) | crate::quests::Command::ClaimSupplies | crate::quests::Command::CraftedPotion | crate::quests::Command::BoundLifestone | crate::quests::Command::Travelled) {
            return Err(invalid("quest completion requires its authoritative reward transaction"));
        }
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted && c.progression.alive())
            .ok_or_else(|| invalid("quest requires an owned living character"))?;
        let quests = character.quests.apply(&command).map_err(io::Error::other)?;
        if quests == character.quests {
            return Ok(false);
        }
        let mut progression = character.progression.clone();
        for reward in crate::quests::rewards(&command) {
            progression = progression.apply(&reward).map_err(io::Error::other)?;
        }
        let revision = match command {
            crate::quests::Command::ShelterGuard | crate::quests::Command::ShelterLieutenant | crate::quests::Command::ShelterSentry | crate::quests::Command::ReadExitWarning | crate::quests::Command::ConfirmExitSkull => "q5",
            crate::quests::Command::AcceptSupplies => "q3",
            crate::quests::Command::AcceptPotion | crate::quests::Command::ClaimPotion | crate::quests::Command::AcceptJourney | crate::quests::Command::ClaimJourney => "q4",
            _ => "q1",
        };
        self.append_record(&format!("{revision}\t{id}\t{}", command.encode()))?;
        let character = self.characters.get_mut(&id).unwrap();
        character.quests = quests;
        character.progression = progression;
        Ok(true)
    }
    /// Version-one NPC/obelisk progression and its XP are one durable event.
    pub fn exploration_conversation(&mut self,account:&str,id:u64,entity:u32,time:u64)->io::Result<()> {
        let c=self.characters.get(&id).filter(|c|c.account==account&&!c.deleted&&c.progression.alive())
            .ok_or_else(||invalid("Quest owner unavailable"))?;
        let result=c.quests.exploration.converse(entity,c.progression.level().number,time)?;
        if result.book==c.quests.exploration {return Ok(());}
        let state=result.settle(&c.progression)?;
        self.append_record(&format!("q9\t{id}\t{entity}\t{time}"))?;
        let c=self.characters.get_mut(&id).unwrap();c.quests.exploration=result.book;c.progression=state;Ok(())
    }
    /// Warder introductions and their rewards share one immutable durable event.
    pub fn warder_conversation(&mut self,account:&str,id:u64,entity:u32,time:u64)->io::Result<()> {
        let c=self.characters.get(&id).filter(|c|c.account==account&&!c.deleted&&c.progression.alive())
            .ok_or_else(||invalid("Quest owner unavailable"))?;
        let result=c.quests.warder.converse(entity,c.progression.level().number,c.quests.esper.completed>0,time)?;
        if result.book==c.quests.warder {return Ok(());}
        let state=crate::quest_points::apply(&c.progression, &result.rewards)?;
        self.append_record(&format!("wr1\t{id}\t{entity}\t{time}"))?;
        let c=self.characters.get_mut(&id).unwrap();c.quests.warder=result.book;c.progression=state;Ok(())
    }
    /// Version-two NPC hunt progression and its XP are one durable event.
    pub fn hunting_conversation(&mut self,account:&str,id:u64,entity:u32,time:u64)->io::Result<()> {
        let first=self.next_item_id()?;
        let c=self.characters.get(&id).filter(|c|c.account==account&&!c.deleted&&c.progression.alive())
            .ok_or_else(||invalid("Quest owner unavailable"))?;
        let result=c.quests.hunting.converse(entity,&c.progression,time)?;
        if result.book==c.quests.hunting {return Ok(());}
        let (bag,state,_)=result.settle(&c.inventory,&c.progression,first)?;
        self.append_record(&format!("qh7\t{id}\t{entity}\t{time}\t{first}"))?;
        let c=self.characters.get_mut(&id).unwrap();c.quests.hunting=result.book;c.progression=state;c.inventory=bag;Ok(())
    }
    pub fn accept_npc_bounty(&mut self, account: &str, id: u64, npc: u32, time: u64) -> io::Result<()> {
        let c = self.characters.get(&id).filter(|c| c.account == account && !c.deleted && c.progression.alive())
            .ok_or_else(|| invalid("Quest owner unavailable"))?;
        let book = npc_bounty_candidate(c, npc, time)?;
        self.append_record(&format!("qb1\t{id}\t{npc}\t{time}"))?;
        self.characters.get_mut(&id).unwrap().quests.recovered = book;
        Ok(())
    }

    /// Commit the canonical inferred delivery exchange and journal together.
    pub fn recovered_delivery(&mut self,account:&str,id:u64,npc:u32,time:u64)->io::Result<()> {
        let first=self.next_item_id()?;
        let c=self.characters.get(&id).filter(|c|c.account==account&&!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Quest owner unavailable"))?;
        let (items,changes,book)=c.quests.recovered.delivery(&c.quests.hunting,&c.inventory,npc,c.progression.level().number,time,first)?;
        let (bag,state)=if items.is_empty()&&changes.is_empty(){(c.inventory.clone(),c.progression.clone())}else{
            crate::player_transaction::candidate(&c.inventory,&c.progression,&items,&changes,Some(first))?};
        self.append_record(&format!("qd2\t{id}\t{npc}\t{time}\t{first}"))?;
        let c=self.characters.get_mut(&id).unwrap();c.inventory=bag;c.progression=state;c.quests.recovered=book;Ok(())
    }
    pub fn collection_delivery(&mut self, account: &str, id: u64, npc: u32, offered: Option<u64>, time: u64) -> io::Result<()> {
        let first = self.next_item_id()?;
        let c = self.characters.get(&id).filter(|c| c.account == account && !c.deleted && c.progression.alive())
            .ok_or_else(||invalid("Quest owner unavailable"))?;
        let (bag, state, book) = collection_candidate(c, npc, offered, time, first)?;
        if bag == c.inventory && book == c.quests.recovered && state == c.progression {return Ok(());}
        let kind = if crate::slavetaker::operation(npc) {"qsc1"} else {"qsh1"};
        self.append_record(&format!("{kind}\t{id}\t{npc}\t{}\t{time}\t{first}", offered.unwrap_or(0)))?;
        let c = self.characters.get_mut(&id).unwrap();
        c.inventory = bag; c.progression = state; c.quests.recovered = book;
        Ok(())
    }

    /// Consume a supported hunt elixir and bestow its journal in one durable event.
    pub fn recovered_elixir(&mut self,account:&str,id:u64,item:u64)->io::Result<()> {
        let c=self.characters.get(&id).filter(|c|c.account==account&&!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Quest owner unavailable"))?;
        let owned=c.inventory.items().find(|i|i.id==item&&matches!(i.location,crate::inventory::Location::Bag(_)))
            .ok_or_else(||invalid("Quest starter must be in your backpack"))?;
        let q=crate::recovered_quests::use_profile(owned.definition).ok_or_else(||invalid("Unsupported quest starter"))?;
        let time=crate::recovered_quests::now();
        let book=c.quests.recovered.accept(q.id,c.progression.level().number,time)?;
        let bag=c.inventory.apply(&crate::inventory::Command::ConsumeDense{item}).map_err(io::Error::other)?;
        self.append_record(&format!("qe2\t{id}\t{item}\t{}\t{time}",q.id))?;
        let c=self.characters.get_mut(&id).unwrap();c.inventory=bag;c.quests.recovered=book;Ok(())
    }
    /// One fsynced command includes inventory, rewards, quest and portal relocation.
    pub fn shelter_exchange(&mut self,account:&str,id:u64,command:crate::quests::Command)->io::Result<()> {
        let first=self.next_item_id()?;
        let c=self.characters.get(&id).filter(|c|c.account==account && !c.deleted && c.progression.alive())
            .ok_or_else(||invalid("Shelter exchange owner unavailable"))?;
        let (items,changes,quests)=crate::shelter::plan(&c.quests,&c.inventory,command,first)?;
        let (inventory,progression)=if items.is_empty() && changes.is_empty() {(c.inventory.clone(),c.progression.clone())}
            else {crate::player_transaction::candidate(&c.inventory,&c.progression,&items,&changes,Some(first))?};
        self.append_record(&format!("s1\t{id}\t{}\t{first}",command.encode()))?;
        let c=self.characters.get_mut(&id).unwrap();c.inventory=inventory;c.progression=progression;c.quests=quests;
        if command==crate::quests::Command::ShelterGlyph {self.positions.insert(id,crate::shelter::VAULT);self.positions_dirty=true;}
        Ok(())
    }
    /// Persist the objective with the normal payout before publishing death.
    pub fn encounter_reward(
        &mut self,
        account: &str,
        id: u64,
        entity: u32,
        commands: &[crate::progression::Command],
    ) -> io::Result<()> {
        self.encounter_reward_with_shares(account, id, entity, commands, &[])
    }

    pub(crate) fn encounter_reward_with_shares(
        &mut self,
        account: &str,
        id: u64,
        entity: u32,
        commands: &[crate::progression::Command],
        shares: &[(u64, u64)],
    ) -> io::Result<()> {
        if commands.is_empty() || commands.len() > 8 {
            return Err(invalid("encounter reward batch size"));
        }
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted && c.progression.alive())
            .ok_or_else(|| invalid("encounter reward character unavailable"))?;
        let time=crate::recovered_quests::now();
        let mut quests=character.quests.record_kill(entity);
        let (recovered,xp)=quests.recovered.kill(entity,character.progression.level().number,time);
        quests.recovered=recovered;
        quests.hunting=quests.hunting.kill(entity,time);
        quests.notebook=quests.notebook.killed(entity,time);
        let mut progression=character.progression.clone();
        for command in commands {progression=progression.apply(command).map_err(io::Error::other)?;}
        if xp>0 {progression=progression.apply(&crate::progression::Command::AwardXp(xp)).map_err(io::Error::other)?;}
        quests.recovered.settle_completion_points(&character.quests.recovered, &mut progression)?;
        let shared = encounter_share_candidates(&self.characters, id, shares)?;
        let changes = crate::player_transaction::encode_changes(commands);
        let record = if shares.is_empty() {
            format!("qk8\t{id}\t{entity:08x}\t{time}\t{changes}")
        } else {
            format!("qk9\t{id}\t{entity:08x}\t{time}\t{changes}\t{}", encode_encounter_shares(shares))
        };
        self.append_record(&record)?;
        let character = self.characters.get_mut(&id).unwrap();
        character.quests = quests;
        character.progression = progression;
        publish_encounter_shares(&mut self.characters, shared);
        Ok(())
    }
    pub fn encounter_transaction(
        &mut self,
        account: &str,
        id: u64,
        entity: u32,
        items: &[crate::inventory::Command],
        changes: &[crate::progression::Command],
    ) -> io::Result<()> {
        self.encounter_transaction_with_shares(account, id, entity, items, changes, &[])
    }

    pub(crate) fn encounter_transaction_with_shares(
        &mut self,
        account: &str,
        id: u64,
        entity: u32,
        items: &[crate::inventory::Command],
        changes: &[crate::progression::Command],
        shares: &[(u64, u64)],
    ) -> io::Result<()> {
        if entity >> 24 != 0x47 || items.is_empty() || changes.is_empty() {
            return Err(invalid("invalid item encounter reward"));
        }
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted && c.progression.alive())
            .ok_or_else(|| invalid("encounter transaction owner unavailable"))?;
        let first = if items.iter().any(|command| command.allocated_id().is_some()) {
            Some(self.next_item_id()?)
        } else {
            None
        };
        let (inventory, progression) = crate::player_transaction::candidate(
            &character.inventory,
            &character.progression,
            items,
            changes,
            first,
        )?;
        let quests = character.quests.record_kill(entity);
        let shared = encounter_share_candidates(&self.characters, id, shares)?;
        let items = crate::player_transaction::encode_items(items);
        let changes = crate::player_transaction::encode_changes(changes);
        let record = if shares.is_empty() {
            format!("a2\t{id}\t{entity:08x}\t{items}\t{changes}")
        } else {
            format!("ak1\t{id}\t{entity:08x}\t{items}\t{changes}\t{}", encode_encounter_shares(shares))
        };
        self.append_record(&record)?;
        let character = self.characters.get_mut(&id).unwrap();
        character.inventory = inventory;
        character.progression = progression;
        character.quests = quests;
        publish_encounter_shares(&mut self.characters, shared);
        Ok(())
    }
    /// Items and payment/rewards become visible only after one durable record.
    pub fn player_transaction(
        &mut self,
        account: &str,
        id: u64,
        items: &[crate::inventory::Command],
        changes: &[crate::progression::Command],
    ) -> io::Result<()> {
        self.player_transaction_quest(account, id, items, changes, false, false)
    }
    pub fn player_transaction_quest(
        &mut self, account: &str, id: u64,
        items: &[crate::inventory::Command], changes: &[crate::progression::Command],
        supplies: bool, practice: bool,
    ) -> io::Result<()> {
        if supplies && practice { return Err(invalid("Multiple quest exchanges in one transaction")); }
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted)
            .ok_or_else(|| invalid("transaction character not owned"))?;
        let first = if items.iter().any(|command| command.allocated_id().is_some()) {
            Some(self.next_item_id()?)
        } else {
            None
        };
        let (inventory, progression) = crate::player_transaction::candidate(
            &character.inventory,
            &character.progression,
            items,
            changes,
            first,
        )?;
        if inventory == character.inventory && progression == character.progression {
            return Ok(());
        }
        let quests = if supplies {
            if !character.progression.alive() { return Err(invalid("Supply exchange requires a living player")); }
            crate::quests::validate_supplies_exchange(&character.quests, &character.inventory, items, changes, first.ok_or_else(|| invalid("Missing supply output identity"))?)?
        } else if practice {
            if !character.progression.alive() { return Err(invalid("Crafting requires a living owner")); }
            crate::quests::validate_practice_craft(&character.quests, &character.inventory, items, changes, first.ok_or_else(|| invalid("Missing craft output identity"))?)?
        } else { character.quests.clone() };
        let revision = if supplies { "a2" } else if practice { "a3" } else { "a1" };
        self.append_record(&format!(
            "{revision}\t{id}\t{}\t{}",
            crate::player_transaction::encode_items(items),
            crate::player_transaction::encode_changes(changes)
        ))?;
        let character = self.characters.get_mut(&id).unwrap();
        character.inventory = inventory;
        character.progression = progression;
        character.quests = quests;
        Ok(())
    }
    /// Inventory, lesson entitlement, journal and XP are committed together.
    pub fn arwic_transaction(&mut self,account:&str,id:u64,action:crate::arwic_mines::Action,time:u64)->io::Result<()> {
        let first=self.next_item_id()?;
        let c=self.characters.get(&id).filter(|c|c.account==account&&!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Lesson owner unavailable"))?;
        let plan=crate::arwic_mines::plan(&c.quests,&c.inventory,action,first,time)?;
        if plan.quests==c.quests && plan.items.is_empty() && plan.rewards.is_empty() {return Ok(());}
        let (bag,state)=if plan.items.is_empty()&&plan.rewards.is_empty(){(c.inventory.clone(),c.progression.clone())}else{crate::player_transaction::candidate(&c.inventory,&c.progression,&plan.items,&plan.rewards,Some(first))?};
        self.append_record(&format!("m1\t{id}\t{}\t{time}\t{first}",action.encode()))?;
        let c=self.characters.get_mut(&id).unwrap();c.inventory=bag;c.progression=state;c.quests=plan.quests;Ok(())
    }
    pub fn notebook_transaction(&mut self,account:&str,id:u64,action:crate::warder_notebook::Action,time:u64)->io::Result<()> {
        let first=self.next_item_id()?;
        let c=self.characters.get(&id).filter(|c|c.account==account&&!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Notebook owner unavailable"))?;
        let plan=crate::warder_notebook::plan(&c.quests,&c.inventory,c.progression.level().number,action,first,time)?;
        if plan.quests==c.quests && plan.items.is_empty() && plan.rewards.is_empty() {return Ok(());}
        let (bag,state)=if plan.items.is_empty()&&plan.rewards.is_empty(){(c.inventory.clone(),c.progression.clone())}else{crate::player_transaction::candidate(&c.inventory,&c.progression,&plan.items,&plan.rewards,Some(first))?};
        self.append_record(&format!("wn1\t{id}\t{}\t{time}\t{first}",action.encode()))?;
        let c=self.characters.get_mut(&id).unwrap();c.inventory=bag;c.progression=state;c.quests=plan.quests;Ok(())
    }
    pub fn esper_transaction(&mut self,account:&str,id:u64,action:crate::esper_camp::Action,time:u64)->io::Result<()> {
        let first=self.next_item_id()?;
        let c=self.characters.get(&id).filter(|c|c.account==account&&!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Esper owner unavailable"))?;
        let plan=crate::esper_camp::plan(&c.quests,&c.inventory,action,first,time)?;
        if plan.quests==c.quests && plan.items.is_empty() && plan.rewards.is_empty() {return Ok(());}
        let (bag,state)=if plan.items.is_empty()&&plan.rewards.is_empty(){(c.inventory.clone(),c.progression.clone())}else{crate::player_transaction::candidate(&c.inventory,&c.progression,&plan.items,&plan.rewards,Some(first))?};
        self.append_record(&format!("e1\t{id}\t{}\t{time}\t{first}",action.encode()))?;
        let c=self.characters.get_mut(&id).unwrap();c.inventory=bag;c.progression=state;c.quests=plan.quests;Ok(())
    }
    /// Quest items, survey ownership, journal and XP share one durable record.
    pub fn windmill_transaction(&mut self,account:&str,id:u64,action:crate::windmill::Action,time:u64)->io::Result<()> {
        let first=self.next_item_id()?;
        let c=self.characters.get(&id).filter(|c|c.account==account&&!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Windmill owner unavailable"))?;
        let plan=crate::windmill::plan(&c.quests,&c.inventory,&c.professions,action,first,time)?;
        if plan.quests==c.quests && plan.items.is_empty() && plan.rewards.is_empty(){return Ok(());}
        let (bag,state)=if plan.items.is_empty()&&plan.rewards.is_empty(){(c.inventory.clone(),c.progression.clone())}else{crate::player_transaction::candidate(&c.inventory,&c.progression,&plan.items,&plan.rewards,Some(first))?};
        self.append_record(&format!("w1\t{id}\t{}\t{time}\t{first}",action.encode()))?;
        let c=self.characters.get_mut(&id).unwrap();c.inventory=bag;c.progression=state;c.quests=plan.quests;c.professions=plan.professions;Ok(())
    }
    /// Atomically save blossom collection, recipe knowledge and item changes.
    pub fn nature_transaction(&mut self,account:&str,id:u64,action:crate::nature_gathering::Action,time:u64)->io::Result<()> {
        let first=self.next_item_id()?;
        let c=self.characters.get(&id).filter(|c|c.account==account&&!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Nature owner unavailable"))?;
        let plan=crate::nature_gathering::plan(&c.quests,&c.inventory,action,first,time)?;
        if plan.quests==c.quests && plan.items.is_empty() && plan.rewards.is_empty() {return Ok(());}
        let (bag,state)=if plan.items.is_empty()&&plan.rewards.is_empty(){(c.inventory.clone(),c.progression.clone())}else{crate::player_transaction::candidate(&c.inventory,&c.progression,&plan.items,&plan.rewards,Some(first))?};
        self.append_record(&format!("n1\t{id}\t{}\t{time}\t{first}",action.encode()))?;
        let c=self.characters.get_mut(&id).unwrap();c.inventory=bag;c.progression=state;c.quests=plan.quests;Ok(())
    }
    /// Inventory/cost and profession XP share one fsynced journal record.
    pub fn profession_transaction(&mut self,account:&str,id:u64,items:&[crate::inventory::Command],changes:&[crate::progression::Command],change:&crate::professions::Change)->io::Result<()> {
        let c=self.characters.get(&id).filter(|c|c.account==account&&!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Crafting owner unavailable"))?;
        let book=c.professions.apply(change)?;
        let first=items.iter().any(|i|i.allocated_id().is_some()).then(||self.next_item_id()).transpose()?;
        let (bag,state)=if items.is_empty()&&changes.is_empty(){(c.inventory.clone(),c.progression.clone())}else{crate::player_transaction::candidate(&c.inventory,&c.progression,items,changes,first)?};
        self.append_record(&format!("c1\t{id}\t{}\t{}\t{}",crate::player_transaction::encode_items(items),crate::player_transaction::encode_changes(changes),serde_json::to_string(change).map_err(io::Error::other)?))?;
        let c=self.characters.get_mut(&id).unwrap();c.inventory=bag;c.progression=state;c.professions=book;Ok(())
    }
    pub fn inventory_command(
        &mut self,
        account: &str,
        id: u64,
        command: crate::inventory::Command,
    ) -> io::Result<()> {
        if command
            .allocated_id()
            .is_some_and(|allocated| self.next_item_id().ok() != Some(allocated))
        {
            return Err(invalid("nonsequential item identity"));
        }
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted)
            .ok_or_else(|| invalid("inventory character not owned"))?;
        let next = character
            .inventory
            .apply(&command)
            .map_err(|error| invalid(&error.to_string()))?;
        if next == character.inventory {
            return Ok(());
        }
        self.append_record(&format!("i1\t{id}\t{}", command.encode()))?;
        self.characters.get_mut(&id).unwrap().inventory = next;
        Ok(())
    }
    pub fn update_ui(
        &mut self,
        account: &str,
        id: u64,
        patch: crate::preferences::UiSettings,
    ) -> io::Result<()> {
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted)
            .ok_or_else(|| invalid("UI settings character not owned"))?;
        let settings = match &character.ui {
            Some(previous) => previous
                .merge(&patch)
                .map_err(|_| invalid("UI settings limit exceeded"))?,
            None => patch,
        };
        if character.ui.as_ref() == Some(&settings) {
            return Ok(());
        }
        let bytes = settings.encode();
        let mut hex = String::with_capacity(bytes.len() * 2);
        const DIGITS: &[u8; 16] = b"0123456789abcdef";
        for byte in bytes {
            hex.push(DIGITS[(byte >> 4) as usize] as char);
            hex.push(DIGITS[(byte & 15) as usize] as char);
        }
        self.append_record(&format!("u1\t{id}\t{hex}"))?;
        self.characters.get_mut(&id).unwrap().ui = Some(settings);
        Ok(())
    }
    pub fn update_gameplay_options(
        &mut self,
        account: &str,
        id: u64,
        options: crate::preferences::GameplayOptions,
    ) -> io::Result<()> {
        let character = self.characters.get(&id)
            .filter(|character| character.account == account && !character.deleted)
            .ok_or_else(|| invalid("gameplay settings character not owned"))?;
        if character.gameplay_options.as_ref() == Some(&options) { return Ok(()); }
        let hex = options.encode().iter().map(|byte| format!("{byte:02x}")).collect::<String>();
        self.append_record(&format!("o1\t{id}\t{hex}"))?;
        self.characters.get_mut(&id).unwrap().gameplay_options = Some(options);
        Ok(())
    }
    pub fn update_hotbar(
        &mut self,
        account: &str,
        id: u64,
        update: crate::preferences::HotbarUpdate,
    ) -> io::Result<()> {
        let character = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted)
            .ok_or_else(|| invalid("hotbar character not owned"))?;
        let initial = !character.hotbar.is_initialized();
        let defaults = character.creation.as_ref().map_or_else(
            crate::preferences::Hotbar::default,
            |c| crate::preferences::Hotbar::starter(c.race, c.sex));
        let next = (if initial { &defaults } else { &character.hotbar }).apply(&update)
            .map_err(|_| invalid("hotbar size limit exceeded"))?;
        if next == character.hotbar {
            return Ok(());
        }
        let bytes = (if initial { next.encode() } else { update.encode() })
            .map_err(|_| invalid("invalid hotbar update"))?;
        let mut hex = String::with_capacity(bytes.len() * 2);
        const DIGITS: &[u8; 16] = b"0123456789abcdef";
        for byte in bytes {
            hex.push(DIGITS[(byte >> 4) as usize] as char);
            hex.push(DIGITS[(byte & 15) as usize] as char);
        }
        let record = if initial { "h2" } else { "h1" };
        self.append_record(&format!("{record}\t{id}\t{hex}"))?;
        self.characters.get_mut(&id).unwrap().hotbar = next;
        Ok(())
    }
    pub fn set_placement(&mut self, id: u64, placement: Placement) -> io::Result<()> {
        if self.characters.get(&id).is_none_or(|c| c.deleted) || !placement.valid() {
            return Err(invalid("invalid character placement"));
        }
        if self.positions.get(&id) != Some(&placement) {
            self.positions.insert(id, placement);
            self.positions_dirty = true;
        }
        Ok(())
    }
    /// One bounded snapshot per checkpoint, not an fsync for every movement packet.
    /// The old snapshot remains authoritative until the flushed new file is renamed.
    pub fn flush_positions(&mut self) -> io::Result<()> {
        if !self.positions_dirty {
            return Ok(());
        }
        let size = 24usize
            .checked_add(
                self.positions
                    .len()
                    .checked_mul(25)
                    .ok_or_else(|| invalid("too many placements"))?,
            )
            .filter(|size| *size <= 1024 * 1024)
            .ok_or_else(|| invalid("placement snapshot limit"))?;
        let mut bytes = Vec::with_capacity(size);
        bytes.extend(b"AC2POS02");
        bytes.extend(self.revision.to_le_bytes());
        bytes.extend((self.positions.len() as u32).to_le_bytes());
        for (id, placement) in &self.positions {
            bytes.extend(id.to_le_bytes());
            bytes.extend(placement.cell.to_le_bytes());
            for value in placement.coordinates {
                bytes.extend(value.to_le_bytes());
            }
            bytes.push(placement.heading);
        }
        bytes.extend(hash(&bytes).to_le_bytes());
        let temporary = self.positions_path.with_extension("snapshot.tmp");
        let mut file = OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(&temporary)?;
        file.write_all(&bytes)?;
        file.sync_all()?;
        drop(file);
        fs::rename(temporary, &self.positions_path)?;
        self.positions_dirty = false;
        Ok(())
    }
    /// Persists structurally valid input. The caller must validate entity/world authority.
    pub fn create_native(
        &mut self,
        account: &str,
        request: &CharacterCreationRequest,
    ) -> io::Result<Character> {
        self.create_record(account, &request.name, Some(request))
    }
    fn create_record(
        &mut self,
        account: &str,
        name: &str,
        creation: Option<&CharacterCreationRequest>,
    ) -> io::Result<Character> {
        if self.poisoned {
            return Err(invalid(
                "journal write failed previously; restart and inspect journal",
            ));
        }
        if !valid_name(account) || !valid_name(name) {
            return Err(invalid(
                "account and character names must contain 1-32 ASCII letters, digits, spaces, hyphens or underscores",
            ));
        }
        if self
            .characters
            .values()
            .any(|c| c.name.eq_ignore_ascii_case(name))
        {
            return Err(invalid("character name already exists"));
        }
        if self
            .characters
            .values()
            .filter(|c| c.account == account && !c.deleted)
            .count()
            >= 5
        {
            return Err(invalid("local character limit is five"));
        }
        let id = self
            .characters
            .keys()
            .next_back()
            .copied()
            .unwrap_or(0)
            .checked_add(1)
            .ok_or_else(|| invalid("character IDs exhausted"))?;
        let c = Character {
            id,
            account: account.into(),
            name: name.into(),
            creation: creation.cloned(),
            deleted: false,
            instance: 0,
            ui: None,
            gameplay_options: None,
            hotbar: crate::preferences::Hotbar::default(),
            inventory: crate::inventory::Inventory::default(),
            progression: crate::progression::Progression::default(),
            skills: crate::skills::Book::default(),
            professions: crate::professions::Book::default(),
            quests: crate::quests::Book::default(),
            legacy_kit_received: false,
            legacy_supplies_received: false,
            starter_gear: false,
            binding: None,
        };
        let line = match creation {
            None => format!("v1\t{}\t{}\t{}", c.id, c.account, c.name),
            Some(request) => format!(
                "v2\t{}\t{}\t{}\t{}",
                c.id,
                c.account,
                c.name,
                encode_creation(request)?
            ),
        };
        self.append_record(&line)?;
        self.characters.insert(id, c.clone());
        Ok(c)
    }
    pub fn delete(&mut self, account: &str, id: u64) -> io::Result<()> {
        if !self
            .characters
            .get(&id)
            .is_some_and(|c| c.account == account && !c.deleted)
        {
            return Err(invalid("character not owned or already deleted"));
        }
        self.append_record(&format!("d1\t{id}"))?;
        self.characters.get_mut(&id).unwrap().deleted = true;
        Ok(())
    }
    pub fn restore(&mut self, account: &str, id: u64) -> io::Result<()> {
        if !self
            .characters
            .get(&id)
            .is_some_and(|c| c.account == account && c.deleted)
        {
            return Err(invalid("deleted character not owned"));
        }
        if self
            .characters
            .values()
            .filter(|c| c.account == account && !c.deleted)
            .count()
            >= 5
        {
            return Err(invalid("local character limit is five"));
        }
        self.append_record(&format!("r1\t{id}"))?;
        self.characters.get_mut(&id).unwrap().deleted = false;
        Ok(())
    }
    pub fn begin_instance(&mut self, account: &str, id: u64) -> io::Result<u16> {
        let c = self
            .characters
            .get(&id)
            .filter(|c| c.account == account && !c.deleted)
            .ok_or_else(|| invalid("active character not owned"))?;
        let instance = c
            .instance
            .checked_add(1)
            .ok_or_else(|| invalid("character instance stamp exhausted"))?;
        self.append_record(&format!("e1\t{id}\t{instance}"))?;
        self.characters.get_mut(&id).unwrap().instance = instance;
        Ok(instance)
    }
    fn append_record(&mut self, line: &str) -> io::Result<()> {
        let revision = self
            .revision
            .checked_add(1)
            .ok_or_else(|| invalid("journal revision exhausted"))?;
        if self.poisoned {
            return Err(invalid(
                "journal write failed previously; restart and inspect journal",
            ));
        }
        let record = format!("{line}\t{:08x}\n", hash(line.as_bytes()));
        if self
            .file
            .metadata()?
            .len()
            .saturating_add(record.len() as u64)
            > self.compact_after
        {
            self.compact_journal()?;
        }
        if self
            .file
            .metadata()?
            .len()
            .saturating_add(record.len() as u64)
            > MAX_JOURNAL_BYTES
        {
            return Err(invalid("journal exceeds 16 MiB limit"));
        }
        if let Err(e) = self
            .file
            .write_all(record.as_bytes())
            .and_then(|()| self.file.sync_all())
        {
            self.poisoned = true;
            return Err(e);
        }
        self.revision = revision;
        Ok(())
    }
    /// Bound repetitive progression history while retaining every non-progression
    /// transaction and verifying complete replay before replacing the file.
    pub fn compact_journal(&mut self) -> io::Result<bool> {
        if self.poisoned {
            return Err(invalid("cannot compact a poisoned journal"));
        }
        let mut raw = Vec::new();
        File::open(&self.journal_path)?
            .take(MAX_JOURNAL_BYTES + 1)
            .read_to_end(&mut raw)?;
        let mut lines = Vec::new();
        let before = match decode_journal_collect(&raw, Some(&mut lines)) {
            Ok(state) => state,
            Err(error) => {
                self.poisoned = true;
                return Err(error);
            }
        };
        if before.characters != self.characters || before.drops != self.drops || before.revision != self.revision {
            self.poisoned = true;
            return Err(invalid("journal differs from active character state"));
        }
        let base = self
            .revision
            .checked_sub(lines.len() as u64)
            .ok_or_else(|| invalid("compaction revision underflow"))?;
        let header = format!("j1\t{base}");
        let mut bytes = format!("{header}\t{:08x}\n", hash(header.as_bytes())).into_bytes();
        for line in lines {
            bytes.extend(line.as_bytes());
            bytes.push(b'\n');
        }
        let next_attempt = (raw.len() as u64)
            .saturating_add(1024 * 1024)
            .clamp(MAX_JOURNAL_BYTES / 2, MAX_JOURNAL_BYTES);
        if bytes.len() >= raw.len() {
            self.compact_after = next_attempt;
            return Ok(false);
        }
        let after = match decode_journal_state(&bytes) {
            Ok(state) => state,
            Err(error) => {
                self.poisoned = true;
                return Err(error);
            }
        };
        if after.characters != before.characters
            || after.drops != before.drops
            || after.revision != before.revision
            || after.relocations.len() != before.relocations.len()
            || before.relocations.iter().any(|(id, (_, position))| {
                after
                    .relocations
                    .get(id)
                    .is_none_or(|(_, other)| position != other)
            })
        {
            self.poisoned = true;
            return Err(invalid("compaction replay differs from original state"));
        }
        // Anchor all old relocation revisions before rebasing their replay order.
        // Both the old and compacted journals end at this same revision.
        self.positions_dirty = true;
        self.flush_positions()?;
        let temporary = self.journal_path.with_extension("journal.compact.tmp");
        let mut file = OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(&temporary)?;
        file.write_all(&bytes)?;
        file.sync_all()?;
        drop(file);
        let backup = self.journal_path.with_extension("journal.previous");
        fs::copy(&self.journal_path, &backup)?;
        OpenOptions::new().write(true).open(&backup)?.sync_all()?;
        fs::rename(&temporary, &self.journal_path)?;
        match OpenOptions::new()
            .read(true)
            .append(true)
            .open(&self.journal_path)
        {
            Ok(file) => self.file = file,
            Err(error) => {
                self.poisoned = true;
                return Err(error);
            }
        }
        self.compact_after = (bytes.len() as u64)
            .saturating_add(1024 * 1024)
            .clamp(MAX_JOURNAL_BYTES / 2, MAX_JOURNAL_BYTES);
        eprintln!(
            "Compacted character journal: {} -> {} bytes at revision {}.",
            raw.len(),
            bytes.len(),
            self.revision
        );
        Ok(true)
    }
}

#[cfg(test)]
fn decode_positions(
    bytes: &[u8],
    characters: &BTreeMap<u64, Character>,
) -> io::Result<BTreeMap<u64, Placement>> {
    decode_position_snapshot(bytes, characters).map(|(_, positions)| positions)
}
fn decode_position_snapshot(
    bytes: &[u8],
    characters: &BTreeMap<u64, Character>,
) -> io::Result<(u64, BTreeMap<u64, Placement>)> {
    if bytes.len() < 16 || bytes.len() > 1024 * 1024 {
        return Err(invalid("invalid placement snapshot header/size"));
    }
    let (revision, start, base) = match &bytes[..8] {
        b"AC2POS01" => (0, 12, 16),
        b"AC2POS02" if bytes.len() >= 24 => {
            (u64::from_le_bytes(bytes[8..16].try_into().unwrap()), 20, 24)
        }
        _ => return Err(invalid("invalid placement snapshot version")),
    };
    let count = u32::from_le_bytes(bytes[start - 4..start].try_into().unwrap()) as usize;
    if count.checked_mul(25).and_then(|n| n.checked_add(base)) != Some(bytes.len()) {
        return Err(invalid("invalid placement snapshot length"));
    }
    let end = bytes.len() - 4;
    if hash(&bytes[..end]) != u32::from_le_bytes(bytes[end..].try_into().unwrap()) {
        return Err(invalid("placement snapshot checksum mismatch"));
    }
    let mut positions = BTreeMap::new();
    for record in bytes[start..end].chunks_exact(25) {
        let id = u64::from_le_bytes(record[..8].try_into().unwrap());
        let placement = Placement {
            cell: u32::from_le_bytes(record[8..12].try_into().unwrap()),
            coordinates: std::array::from_fn(|i| {
                f32::from_le_bytes(record[12 + i * 4..16 + i * 4].try_into().unwrap())
            }),
            heading: record[24],
        };
        if !characters.contains_key(&id)
            || !placement.valid()
            || positions.insert(id, placement).is_some()
        {
            return Err(invalid("unknown, duplicate or invalid saved placement"));
        }
    }
    Ok((revision, positions))
}
impl Drop for Store {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.lock);
    }
}

pub fn decode_journal(bytes: &[u8]) -> io::Result<BTreeMap<u64, Character>> {
    decode_journal_state(bytes).map(|journal| journal.characters)
}
struct JournalState {
    characters: BTreeMap<u64, Character>,
    drops: BTreeMap<u64, DroppedItem>,
    relocations: BTreeMap<u64, (u64, Placement)>,
    revision: u64,
}
fn encode_placement(placement: Placement) -> String {
    format!(
        "{:08x},{:08x},{:08x},{:08x},{}",
        placement.cell,
        placement.coordinates[0].to_bits(),
        placement.coordinates[1].to_bits(),
        placement.coordinates[2].to_bits(),
        placement.heading
    )
}
fn decode_placement(text: &str) -> io::Result<Placement> {
    if text.len() > 64 {
        return Err(invalid("placement field limit"));
    }
    let fields: Vec<_> = text.split(',').collect();
    if fields.len() != 5 {
        return Err(invalid("placement field count"));
    }
    let word = |index: usize| {
        u32::from_str_radix(fields[index], 16).map_err(|_| invalid("invalid placement word"))
    };
    let placement = Placement {
        cell: word(0)?,
        coordinates: [
            f32::from_bits(word(1)?),
            f32::from_bits(word(2)?),
            f32::from_bits(word(3)?),
        ],
        heading: fields[4]
            .parse()
            .map_err(|_| invalid("invalid placement heading"))?,
    };
    if !placement.valid() {
        return Err(invalid("invalid persisted placement"));
    }
    Ok(placement)
}
fn decode_journal_state(bytes: &[u8]) -> io::Result<JournalState> {
    decode_journal_collect(bytes, None)
}
fn decode_journal_collect(
    bytes: &[u8],
    mut compacted: Option<&mut Vec<String>>,
) -> io::Result<JournalState> {
    if bytes.len() as u64 > MAX_JOURNAL_BYTES {
        return Err(invalid("journal exceeds 16 MiB limit"));
    }
    if !bytes.is_empty() && bytes.last() != Some(&b'\n') {
        return Err(invalid("incomplete character journal tail"));
    }
    let text =
        std::str::from_utf8(bytes).map_err(|_| invalid("invalid character journal encoding"))?;
    let mut records: BTreeMap<u64, Character> = BTreeMap::new();
    let mut drops = BTreeMap::new();
    let mut relocations = BTreeMap::new();
    let mut revision = 0u64;
    let mut run = compaction::Run::default();
    for (index, line) in text.lines().enumerate() {
        revision = revision
            .checked_add(1)
            .ok_or_else(|| invalid("journal revision exhausted"))?;
        let (data, sum) = line
            .rsplit_once('\t')
            .ok_or_else(|| invalid("missing journal checksum"))?;
        if u32::from_str_radix(sum, 16).ok() != Some(hash(data.as_bytes())) {
            return Err(invalid("character journal checksum mismatch"));
        }
        let fields: Vec<_> = data.split('\t').collect();
        if fields.len() == 2 && fields[0] == "j1" {
            if index != 0 {
                return Err(invalid("journal revision base must be first"));
            }
            revision = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid journal revision base"))?;
            continue;
        }
        if let Some(output) = compacted.as_mut() {
            if !(fields.len() == 3 && matches!(fields[0], "g1" | "g2" | "g3")) {
                run.flush(output);
                output.push(line.to_owned());
            }
        }
        if fields.len() == 3 && matches!(fields[0], "q1" | "q3" | "q4" | "q5") {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid quest owner"))?;
            let command = crate::quests::Command::decode(fields[2]).map_err(io::Error::other)?;
            if !(fields[0] == "q5" && matches!(command, crate::quests::Command::ShelterGuard | crate::quests::Command::ShelterLieutenant | crate::quests::Command::ShelterSentry | crate::quests::Command::ReadExitWarning | crate::quests::Command::ConfirmExitSkull)
                || fields[0] == "q1" && matches!(command, crate::quests::Command::Accept | crate::quests::Command::Claim)
                || fields[0] == "q3" && matches!(command, crate::quests::Command::AcceptSupplies)
                || fields[0] == "q4" && matches!(command, crate::quests::Command::AcceptPotion | crate::quests::Command::ClaimPotion | crate::quests::Command::AcceptJourney | crate::quests::Command::ClaimJourney)) {
                return Err(invalid("quest command does not match its journal revision"));
            }
            let character = records
                .get_mut(&id)
                .filter(|c| !c.deleted && c.progression.alive())
                .ok_or_else(|| invalid("unavailable quest owner"))?;
            character.quests = character.quests.apply(&command).map_err(io::Error::other)?;
            for reward in crate::quests::rewards(&command) {
                character.progression = character
                    .progression
                    .apply(&reward)
                    .map_err(io::Error::other)?;
            }
            continue;
        }
        if fields.len()==4 && fields[0]=="wr1" {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("Warder owner"))?;
            let entity=fields[2].parse::<u32>().map_err(|_|invalid("Warder NPC"))?;
            let time=fields[3].parse::<u64>().map_err(|_|invalid("Warder time"))?;
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Warder owner unavailable"))?;
            let result=c.quests.warder.converse(entity,c.progression.level().number,c.quests.esper.completed>0,time)?;
            if result.book==c.quests.warder {return Err(invalid("Warder event did not advance"));}
            c.progression=crate::quest_points::apply(&c.progression, &result.rewards)?;
            c.quests.warder=result.book;continue;
        }
        if fields.len()==4 && fields[0]=="q9" {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("exploration owner"))?;
            let entity=fields[2].parse::<u32>().map_err(|_|invalid("exploration NPC"))?;
            let time=fields[3].parse::<u64>().map_err(|_|invalid("exploration time"))?;
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("exploration owner unavailable"))?;
            let result=c.quests.exploration.converse(entity,c.progression.level().number,time)?;
            if result.book==c.quests.exploration {return Err(invalid("exploration event did not advance"));}
            c.progression=result.settle(&c.progression)?;
            c.quests.exploration=result.book;continue;
        }
        if fields.len()==4 && matches!(fields[0],"qh1"|"qh2") {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("hunting owner"))?;
            let entity=fields[2].parse::<u32>().map_err(|_|invalid("hunting NPC"))?;
            let time=fields[3].parse::<u64>().map_err(|_|invalid("hunting time"))?;
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("hunting owner unavailable"))?;
            let result=c.quests.hunting.converse_version(entity,&c.progression,time,if fields[0]=="qh1" {1}else{2})?;
            if result.book==c.quests.hunting {return Err(invalid("hunting event did not advance"));}
            c.progression=crate::quest_points::apply(&c.progression, &result.rewards())?;
            c.quests.hunting=result.book;continue;
        }
        if fields.len()==5 && matches!(fields[0],"qh3"|"qh4"|"qh5"|"qh6"|"qh7") {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("hunting owner"))?;
            let npc=fields[2].parse::<u32>().map_err(|_|invalid("hunting NPC"))?;
            let time=fields[3].parse::<u64>().map_err(|_|invalid("hunting time"))?;
            let first=fields[4].parse::<u64>().map_err(|_|invalid("hunting item"))?;
            let expected=records.values().map(|c|c.inventory.high_water()).max().unwrap_or(crate::inventory::ITEM_NAMESPACE).checked_add(1);
            if Some(first)!=expected {return Err(invalid("hunting item sequence"));}
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("hunting owner unavailable"))?;
            let version=match fields[0] {"qh3"=>3,"qh4"=>4,"qh5"=>5,"qh6"=>6,_=>7};
            let result=c.quests.hunting.converse_version(npc,&c.progression,time,version)?;
            if result.book==c.quests.hunting {return Err(invalid("hunting event did not advance"));}
            let (bag,state,_)=result.settle(&c.inventory,&c.progression,first)?;
            c.quests.hunting=result.book;c.inventory=bag;c.progression=state;continue;
        }
        if fields.len() == 6 && matches!(fields[0], "qsh1" | "qsc1") {
            let id = fields[1].parse::<u64>().map_err(|_|invalid("collection owner"))?;
            let npc = fields[2].parse::<u32>().map_err(|_|invalid("collection NPC"))?;
            if (fields[0] == "qsc1") != crate::slavetaker::operation(npc) {return Err(invalid("collection NPC mismatch"));}
            let item = fields[3].parse::<u64>().map_err(|_|invalid("collection item"))?;
            let time = fields[4].parse().map_err(|_|invalid("collection time"))?;
            let first = fields[5].parse().map_err(|_|invalid("collection item sequence"))?;
            let high = records.values().map(|c|c.inventory.high_water()).max().unwrap_or(crate::inventory::ITEM_NAMESPACE);
            if high.checked_add(1) != Some(first) {return Err(invalid("collection item sequence"));}
            let c = records.get_mut(&id).filter(|c|!c.deleted && c.progression.alive()).ok_or_else(||invalid("collection owner unavailable"))?;
            (c.inventory, c.progression, c.quests.recovered) = collection_candidate(c, npc, (item != 0).then_some(item), time, first)?;
            continue;
        }
        if fields.len() == 4 && fields[0] == "qb1" {
            let id = fields[1].parse::<u64>().map_err(|_| invalid("bounty owner"))?;
            let npc = fields[2].parse::<u32>().map_err(|_| invalid("bounty NPC"))?;
            let time = fields[3].parse::<u64>().map_err(|_| invalid("bounty time"))?;
            let c = records.get_mut(&id).filter(|c| !c.deleted && c.progression.alive())
                .ok_or_else(|| invalid("bounty owner unavailable"))?;
            c.quests.recovered = npc_bounty_candidate(c, npc, time)?;
            continue;
        }
        if fields.len()==5 && matches!(fields[0],"q8"|"qd2") {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("delivery owner"))?;
            let npc=fields[2].parse::<u32>().map_err(|_|invalid("delivery NPC"))?;
            if fields[0]=="q8" && !matches!(npc,crate::recovered_quests::KANTEN|crate::recovered_quests::ORVEN) {return Err(invalid("legacy delivery NPC"));}
            let time=fields[3].parse::<u64>().map_err(|_|invalid("delivery time"))?;
            let first=fields[4].parse::<u64>().map_err(|_|invalid("delivery item"))?;
            let expected=records.values().map(|c|c.inventory.high_water()).max().unwrap_or(crate::inventory::ITEM_NAMESPACE).checked_add(1).ok_or_else(||invalid("Item identity exhausted"))?;
            if first!=expected{return Err(invalid("delivery item sequence"));}
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("delivery owner unavailable"))?;
            let (items,changes,book)=c.quests.recovered.delivery(&c.quests.hunting,&c.inventory,npc,c.progression.level().number,time,first)?;
            if !items.is_empty()||!changes.is_empty(){(c.inventory,c.progression)=crate::player_transaction::candidate(&c.inventory,&c.progression,&items,&changes,Some(first))?;}
            c.quests.recovered=book;continue;
        }
        if (fields.len() == 5 && matches!(fields[0],"q6"|"qk1"|"qk2"|"qk3"|"qk4"|"qk5"|"qk6"|"qk7"|"qk8"))
            || (fields.len() == 6 && fields[0] == "qk9") {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("recovered quest owner"))?;
            let entity=u32::from_str_radix(fields[2],16).map_err(|_|invalid("recovered quest entity"))?;
            let time=fields[3].parse::<u64>().map_err(|_|invalid("recovered quest time"))?;
            if entity>>24!=0x47||time==0 {return Err(invalid("recovered quest event"));}
            let changes=crate::player_transaction::decode_changes(fields[4])?;
            if changes.is_empty(){return Err(invalid("empty recovered kill payout"));}
            let shared = encounter_share_candidates(&records, id, &decode_encounter_shares(fields.get(5))?)?;
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("recovered kill owner"))?;
            use crate::recovered_quests::HuntRules;
            let rules = match fields[0] {
                "qk8" | "qk9" => HuntRules::SlimelordRevision,
                "qk7" => HuntRules::PublishedRewards,
                "qk4" | "qk5" | "qk6" => HuntRules::Hero,
                _ => HuntRules::Original,
            };
            let (book,xp)=c.quests.recovered.kill_version(entity,c.progression.level().number,time,rules);
            let mut state=c.progression.clone();
            for change in changes {state=state.apply(&change).map_err(io::Error::other)?;}
            if xp>0 {state=state.apply(&crate::progression::Command::AwardXp(xp)).map_err(io::Error::other)?;}
            book.settle_completion_points(&c.quests.recovered, &mut state)?;
            c.quests=c.quests.record_kill(entity);c.quests.recovered=book;
            if fields[0]!="q6" {
                let version=match fields[0] {"qk1"=>1,"qk4"=>3,"qk5"=>4,"qk6"=>5,"qk7"|"qk8"|"qk9"=>7,_=>2};
                c.quests.hunting=c.quests.hunting.kill_version(entity,time,version);
            }
            if matches!(fields[0],"qk3"|"qk4"|"qk5"|"qk6"|"qk7"|"qk8"|"qk9") {c.quests.notebook=c.quests.notebook.killed(entity,time);}
            c.progression = state;
            publish_encounter_shares(&mut records, shared);
            continue;
        }
        if fields.len()==5 && matches!(fields[0], "q7" | "qe2") {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("quest use owner"))?;
            let item=fields[2].parse::<u64>().map_err(|_|invalid("quest use item"))?;
            let quest=fields[3].parse::<u32>().map_err(|_|invalid("quest use id"))?;
            let time=fields[4].parse::<u64>().map_err(|_|invalid("quest use time"))?;
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("quest use owner"))?;
            let owned=c.inventory.items().find(|i|i.id==item&&matches!(i.location,crate::inventory::Location::Bag(_)))
                .ok_or_else(||invalid("quest starter not owned"))?;
            if crate::recovered_quests::use_profile(owned.definition).is_none_or(|q|q.id!=quest){return Err(invalid("quest starter mismatch"));}
            use crate::recovered_quests::HuntRules;
            let rules = if fields[0] == "q7" { HuntRules::Hero } else { HuntRules::PublishedRewards };
            let book=c.quests.recovered.accept_version(quest,c.progression.level().number,time,rules)?;
            let bag=c.inventory.apply(&crate::inventory::Command::ConsumeDense{item}).map_err(io::Error::other)?;
            c.inventory=bag;c.quests.recovered=book;continue;
        }
        if fields.len() == 4 && fields[0] == "q2" {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid quest reward owner"))?;
            let command = crate::quests::Command::decode(&format!("k,{}", fields[2]))
                .map_err(io::Error::other)?;
            let changes = crate::player_transaction::decode_changes(fields[3])?;
            if changes.is_empty() {
                return Err(invalid("quest kill payout is empty"));
            }
            let character = records
                .get_mut(&id)
                .filter(|c| !c.deleted && c.progression.alive())
                .ok_or_else(|| invalid("unavailable quest kill owner"))?;
            let quests = character.quests.apply(&command).map_err(io::Error::other)?;
            if quests == character.quests {
                return Err(invalid("quest kill did not advance an objective"));
            }
            for change in changes {
                character.progression = character
                    .progression
                    .apply(&change)
                    .map_err(io::Error::other)?;
            }
            character.quests = quests;
            continue;
        }
        if fields.len() == 3 && fields[0] == "g3" {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid progression checkpoint character"))?;
            let character = records
                .get_mut(&id)
                .filter(|character| !character.deleted)
                .ok_or_else(|| invalid("unavailable progression checkpoint character"))?;
            let starting_points = character.progression.quest_points;
            character.progression = compaction::progression(fields[2], &character.progression)?;
            if compacted.is_some() {
                run.record(id, starting_points, &character.progression, line);
            }
            continue;
        }
        if fields.len() == 2 && fields[0] == "k4" {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid training attack character"))?;
            let character = records
                .get_mut(&id)
                .filter(|c| {
                    !c.deleted
                        && c.legacy_kit_received
                        && c.skills.get(crate::skills::TRAINING_ATTACK).is_none()
                })
                .ok_or_else(|| invalid("unavailable training attack character"))?;
            character.skills = character
                .skills
                .with_training_attack(
                    &character.progression,
                    character
                        .creation
                        .as_ref()
                        .map_or(1, |request| request.race),
                )
                .map_err(|_| invalid("invalid training attack grant"))?;
            continue;
        }
        if fields.len() == 2 && fields[0] == "k3" {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid recall grant character"))?;
            let character = records
                .get_mut(&id)
                .filter(|character| {
                    !character.deleted
                        && character.binding.is_some()
                        && character
                            .skills
                            .get(crate::skills::LIFESTONE_RECALL)
                            .is_none()
                })
                .ok_or_else(|| invalid("unavailable recall grant character"))?;
            character.skills = character
                .skills
                .with_lifestone_recall(
                    &character.progression,
                    character
                        .creation
                        .as_ref()
                        .map_or(1, |request| request.race),
                )
                .map_err(|_| invalid("invalid recall grant"))?;
            continue;
        }
        if fields.len() == 4 && fields[0] == "k2" {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid advancement character"))?;
            let skill = u32::from_str_radix(fields[2], 16)
                .map_err(|_| invalid("invalid advancement skill"))?;
            let paid = fields[3]
                .parse::<u64>()
                .map_err(|_| invalid("invalid advancement payment"))?;
            let character = records
                .get_mut(&id)
                .filter(|character| !character.deleted)
                .ok_or_else(|| invalid("unavailable advancement character"))?;
            let race = character
                .creation
                .as_ref()
                .map_or(1, |request| request.race);
            let (skills, progression) = character
                .skills
                .advance(skill, &character.progression, race)
                .map_err(|_| invalid("invalid skill advancement transition"))?;
            if character
                .progression
                .available_xp
                .checked_sub(progression.available_xp)
                != Some(paid)
                || paid == 0
            {
                return Err(invalid("advancement payment differs from immutable curve"));
            }
            character.skills = skills;
            character.progression = progression;
            continue;
        }
        if fields.len()==4 && fields[0]=="s1" {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("Shelter owner"))?;
            let command=crate::quests::Command::decode(fields[2]).map_err(io::Error::other)?;
            let first=fields[3].parse::<u64>().map_err(|_|invalid("Shelter item identity"))?;
            let expected=records.values().map(|c|c.inventory.high_water()).max().unwrap_or(crate::inventory::ITEM_NAMESPACE).checked_add(1).ok_or_else(||invalid("Item identity exhausted"))?;
            if first!=expected {return Err(invalid("Shelter item sequence"));}
            let c=records.get_mut(&id).filter(|c|!c.deleted && c.progression.alive()).ok_or_else(||invalid("Shelter owner unavailable"))?;
            let (items,changes,quests)=crate::shelter::plan(&c.quests,&c.inventory,command,first)?;
            if !items.is_empty() || !changes.is_empty() {
                (c.inventory,c.progression)=crate::player_transaction::candidate(&c.inventory,&c.progression,&items,&changes,Some(first))?;
            }
            c.quests=quests;
            if command==crate::quests::Command::ShelterGlyph {relocations.insert(id,(revision,crate::shelter::VAULT));}
            continue;
        }
        if fields.len() == 3 && matches!(fields[0], "sx1" | "sx2") {
            let id = fields[1].parse::<u64>().map_err(|_| invalid("Invalid Shelter exit owner"))?;
            let placement = decode_placement(fields[2])?;
            let c = records.get_mut(&id).filter(|c| !c.deleted && c.progression.alive())
                .ok_or_else(|| invalid("Unavailable Shelter exit owner"))?;
            (c.quests, c.progression) = if fields[0] == "sx1" {
                crate::shelter::quick_exit(&c.quests, &c.progression)?
            } else {
                crate::shelter::shard_exit(&c.quests, &c.progression)?
            };
            relocations.insert(id, (revision, placement));
            continue;
        }
        if fields.len() == 4 && fields[0] == "p3" {
            let id = fields[1].parse::<u64>().map_err(|_| invalid("Invalid travel lesson owner"))?;
            let portal = u64::from_str_radix(fields[2], 16).map_err(|_| invalid("Invalid training portal identity"))?;
            if !crate::quests::is_training_portal(portal) { return Err(invalid("Travel lesson portal is not enabled")); }
            let placement = decode_placement(fields[3])?;
            let character = records.get_mut(&id).filter(|c| !c.deleted && c.progression.alive()).ok_or_else(|| invalid("Unavailable travel lesson owner"))?;
            character.quests = character.quests.apply(&crate::quests::Command::Travelled).map_err(io::Error::other)?;
            relocations.insert(id, (revision, placement));
            continue;
        }
        if fields.len() == 3 && matches!(fields[0], "l1" | "l2" | "l3" | "p1" | "p2" | "r2" | "r3") {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid lifestone character"))?;
            let placement = decode_placement(fields[2])?;
            let character = records
                .get_mut(&id)
                .filter(|character| !character.deleted)
                .ok_or_else(|| invalid("unavailable lifestone character"))?;
            if matches!(fields[0], "l1" | "l2" | "l3") {
                if !character.progression.alive() {
                    return Err(invalid("dead character binding"));
                }
                if fields[0] == "l3" { character.quests = character.quests.apply(&crate::quests::Command::BoundLifestone).map_err(io::Error::other)?; }
                character.binding = Some(placement);
                if matches!(fields[0], "l2" | "l3") {
                    character.skills = character
                        .skills
                        .with_lifestone_recall(
                            &character.progression,
                            character
                                .creation
                                .as_ref()
                                .map_or(1, |request| request.race),
                        )
                        .map_err(|_| invalid("invalid bound recall skill"))?;
                }
            } else {
                if fields[0] != "p2" && character.binding != Some(placement) {
                    return Err(invalid("relocation differs from saved binding"));
                }
                if matches!(fields[0], "r2" | "r3") {
                    let resurrection = if fields[0] == "r2" { crate::progression::Command::LegacyResurrect } else { crate::progression::Command::Resurrect };
                    character.progression = character
                        .progression
                        .apply(&resurrection)
                        .map_err(|_| invalid("invalid bound resurrection"))?;
                } else if !character.progression.alive() {
                    return Err(invalid("dead character recall"));
                }
                relocations.insert(id, (revision, placement));
            }
            continue;
        }
        if fields.len() == 2 && fields[0] == "k5" {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid starter skill character"))?;
            let character = records
                .get_mut(&id)
                .filter(|c| !c.deleted)
                .ok_or_else(|| invalid("unavailable starter skill character"))?;
            let request = character
                .creation
                .as_ref()
                .ok_or_else(|| invalid("starter skills without a creation profile"))?;
            character.skills = character
                .skills
                .with_starter_skills(&character.progression, request.race, request.sex)
                .map_err(|_| invalid("invalid starter skill grant"))?;
            continue;
        }
        if fields.len() == 3 && fields[0] == "t4" {
            let id = fields[1].parse::<u64>().map_err(|_| invalid("invalid starter owner"))?;
            let first = fields[2].parse::<u64>().map_err(|_| invalid("invalid starter identity"))?;
            let high = records.values().map(|character| character.inventory.high_water()).max()
                .unwrap_or(crate::inventory::ITEM_NAMESPACE);
            if high.checked_add(1) != Some(first) { return Err(invalid("nonsequential starter identity")); }
            let character = records.get_mut(&id)
                .filter(|character| !character.deleted && !character.starter_gear)
                .ok_or_else(|| invalid("invalid repeated starter grant"))?;
            let request = character.creation.as_ref().ok_or_else(|| invalid("starter grant without creation"))?;
            character.inventory = crate::starter_gear::candidate(request, &character.inventory, first)?;
            character.starter_gear = true;
            character.legacy_kit_received = true;
            continue;
        }
        if fields.len() == 3 && fields[0] == "t3" {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("training supplies owner"))?;
            let first = fields[2]
                .parse::<u64>()
                .map_err(|_| invalid("training supplies identity"))?;
            let high = records
                .values()
                .map(|c| c.inventory.high_water())
                .max()
                .unwrap_or(crate::inventory::ITEM_NAMESPACE);
            if high.checked_add(1) != Some(first) {
                return Err(invalid("nonsequential training supplies identity"));
            }
            let character = records
                .get_mut(&id)
                .filter(|c| {
                    !c.deleted && c.legacy_kit_received && !c.legacy_supplies_received && c.progression.alive()
                })
                .ok_or_else(|| invalid("invalid repeated training supplies grant"))?;
            character.inventory =
                crate::training_kit::supply_inventory(&character.inventory, first)
                    .map_err(io::Error::other)?;
            character.legacy_supplies_received = true;
            continue;
        }
        if fields.len() == 3 && matches!(fields[0], "t1" | "t2") {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid training kit owner"))?;
            let item = fields[2]
                .parse::<u64>()
                .map_err(|_| invalid("invalid training kit item"))?;
            let high = records
                .values()
                .map(|character| character.inventory.high_water())
                .max()
                .unwrap_or(crate::inventory::ITEM_NAMESPACE);
            if high.checked_add(1) != Some(item) {
                return Err(invalid("nonsequential training kit identity"));
            }
            let character = records
                .get_mut(&id)
                .filter(|character| !character.deleted && !character.legacy_kit_received)
                .ok_or_else(|| invalid("invalid repeated training kit grant"))?;
            if !character.creation.as_ref().is_some_and(|request| {
                request.race == 1 && request.entity_did == crate::bootstrap::PLAYER_ENTITY
            }) {
                return Err(invalid("unsupported training kit character profile"));
            }
            let slot = character
                .inventory
                .free_slot()
                .ok_or_else(|| invalid("training kit bag full"))?;
            character.inventory = character
                .inventory
                .apply(&crate::inventory::Command::Grant(
                    crate::training_kit::item(item, slot),
                ))
                .map_err(|_| invalid("invalid training kit inventory transition"))?;
            if fields[0] == "t2" {
                character.skills = character
                    .skills
                    .with_training_attack(
                        &character.progression,
                        character.creation.as_ref().unwrap().race,
                    )
                    .map_err(|_| invalid("invalid training kit skill transition"))?;
            }
            character.legacy_kit_received = true;
            continue;
        }
        if fields.len() == 3 && fields[0] == "k1" {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid skill owner"))?;
            let command = crate::skills::Command::decode(fields[2])
                .map_err(|_| invalid("invalid skill mutation"))?;
            let character = records
                .get_mut(&id)
                .filter(|c| !c.deleted)
                .ok_or_else(|| invalid("skill owner unavailable"))?;
            let race = character
                .creation
                .as_ref()
                .map_or(1, |request| request.race);
            character.skills = character
                .skills
                .apply(command, &character.progression, race)
                .map_err(|_| invalid("invalid saved skill transition"))?;
            continue;
        }
        if fields.len()==2 && fields[0]=="ku1" {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("untraining owner"))?;
            let c=records.get_mut(&id).filter(|c|!c.deleted).ok_or_else(||invalid("untraining character unavailable"))?;
            let (skills,progression)=c.skills.debit_untraining(&c.progression).map_err(io::Error::other)?.ok_or_else(||invalid("untraining without earned XP"))?;
            c.skills=skills;c.progression=progression;continue;
        }
        if fields.len() == 3 && fields[0] == "g2" {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid progression batch owner"))?;
            if fields[2].len() > 256 {
                return Err(invalid("progression batch limit"));
            }
            let commands = fields[2].split(';').collect::<Vec<_>>();
            if commands.is_empty() || commands.len() > 8 {
                return Err(invalid("progression batch size"));
            }
            let character = records
                .get_mut(&id)
                .filter(|c| !c.deleted)
                .ok_or_else(|| invalid("progression batch owner unavailable"))?;
            let mut next = character.progression.clone();
            for text in commands {
                let command = crate::progression::Command::decode(text)
                    .map_err(|_| invalid("invalid progression batch event"))?;
                next = next
                    .apply(&command)
                    .map_err(|_| invalid("invalid progression batch transition"))?;
            }
            let starting_points = character.progression.quest_points;
            character.progression = next;
            if compacted.is_some() {
                run.record(id, starting_points, &character.progression, line);
            }
            continue;
        }
        if fields.len() == 3 && fields[0] == "g1" {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid progression character ID"))?;
            let command = crate::progression::Command::decode(fields[2])
                .map_err(|_| invalid("invalid progression event"))?;
            let character = records
                .get_mut(&id)
                .filter(|c| !c.deleted)
                .ok_or_else(|| invalid("progression event references unavailable character"))?;
            let starting_points = character.progression.quest_points;
            character.progression = character
                .progression
                .apply(&command)
                .map_err(|_| invalid("invalid progression transition"))?;
            if compacted.is_some() {
                run.record(id, starting_points, &character.progression, line);
            }
            continue;
        }
        if (fields.len() == 5 && fields[0] == "a2") || (fields.len() == 6 && fields[0] == "ak1") {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid encounter transaction owner"))?;
            if fields[2].len() != 8 {
                return Err(invalid("invalid encounter entity encoding"));
            }
            let entity = u32::from_str_radix(fields[2], 16)
                .map_err(|_| invalid("invalid encounter entity"))?;
            if entity >> 24 != 0x47 {
                return Err(invalid("invalid encounter entity type"));
            }
            let items = crate::player_transaction::decode_items(fields[3])?;
            let changes = crate::player_transaction::decode_changes(fields[4])?;
            if items.is_empty() || changes.is_empty() {
                return Err(invalid("empty encounter item payout"));
            }
            let shared = encounter_share_candidates(&records, id, &decode_encounter_shares(fields.get(5))?)?;
            let first = if items.iter().any(|command| command.allocated_id().is_some()) {
                Some(
                    records
                        .values()
                        .map(|c| c.inventory.high_water())
                        .max()
                        .unwrap_or(crate::inventory::ITEM_NAMESPACE)
                        .checked_add(1)
                        .ok_or_else(|| invalid("encounter item identity exhausted"))?,
                )
            } else {
                None
            };
            let character = records
                .get_mut(&id)
                .filter(|c| !c.deleted && c.progression.alive())
                .ok_or_else(|| invalid("unavailable encounter payout owner"))?;
            let (inventory, progression) = crate::player_transaction::candidate(
                &character.inventory,
                &character.progression,
                &items,
                &changes,
                first,
            )?;
            character.quests = character.quests.record_kill(entity);
            character.inventory = inventory;
            character.progression = progression;
            publish_encounter_shares(&mut records, shared);
            continue;
        }
        if fields.len()==5 && fields[0]=="m1" {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("Invalid lesson owner"))?;
            let action=crate::arwic_mines::Action::decode(fields[2])?;
            let time=fields[3].parse::<u64>().map_err(|_|invalid("Invalid lesson time"))?;
            let first=fields[4].parse::<u64>().map_err(|_|invalid("Invalid lesson identity"))?;
            let expected=records.values().map(|c|c.inventory.high_water()).max().unwrap_or(crate::inventory::ITEM_NAMESPACE).checked_add(1).ok_or_else(||invalid("Lesson identity overflow"))?;
            if first!=expected {return Err(invalid("Lesson item identity mismatch"));}
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Lesson owner unavailable"))?;
            let plan=crate::arwic_mines::plan(&c.quests,&c.inventory,action,first,time)?;
            let (bag,state)=if plan.items.is_empty()&&plan.rewards.is_empty(){(c.inventory.clone(),c.progression.clone())}else{crate::player_transaction::candidate(&c.inventory,&c.progression,&plan.items,&plan.rewards,Some(first))?};
            c.inventory=bag;c.progression=state;c.quests=plan.quests;continue;
        }
        if fields.len()==5 && fields[0]=="wn1" {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("Invalid Notebook owner"))?;
            let action=crate::warder_notebook::Action::decode(fields[2])?;
            let time=fields[3].parse::<u64>().map_err(|_|invalid("Invalid Notebook time"))?;
            let first=fields[4].parse::<u64>().map_err(|_|invalid("Invalid Notebook identity"))?;
            let expected=records.values().map(|c|c.inventory.high_water()).max().unwrap_or(crate::inventory::ITEM_NAMESPACE).checked_add(1).ok_or_else(||invalid("Notebook identity overflow"))?;
            if first!=expected {return Err(invalid("Notebook item identity mismatch"));}
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Notebook owner unavailable"))?;
            let plan=crate::warder_notebook::plan(&c.quests,&c.inventory,c.progression.level().number,action,first,time)?;
            let (bag,state)=if plan.items.is_empty()&&plan.rewards.is_empty(){(c.inventory.clone(),c.progression.clone())}else{crate::player_transaction::candidate(&c.inventory,&c.progression,&plan.items,&plan.rewards,Some(first))?};
            c.inventory=bag;c.progression=state;c.quests=plan.quests;continue;
        }
        if fields.len()==5 && fields[0]=="e1" {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("Invalid Esper owner"))?;
            let action=crate::esper_camp::Action::decode(fields[2])?;
            let time=fields[3].parse::<u64>().map_err(|_|invalid("Invalid Esper time"))?;
            let first=fields[4].parse::<u64>().map_err(|_|invalid("Invalid Esper identity"))?;
            let expected=records.values().map(|c|c.inventory.high_water()).max().unwrap_or(crate::inventory::ITEM_NAMESPACE).checked_add(1).ok_or_else(||invalid("Esper identity overflow"))?;
            if first!=expected {return Err(invalid("Esper item identity mismatch"));}
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Esper owner unavailable"))?;
            let plan=crate::esper_camp::plan(&c.quests,&c.inventory,action,first,time)?;
            let (bag,state)=if plan.items.is_empty()&&plan.rewards.is_empty(){(c.inventory.clone(),c.progression.clone())}else{crate::player_transaction::candidate(&c.inventory,&c.progression,&plan.items,&plan.rewards,Some(first))?};
            c.inventory=bag;c.progression=state;c.quests=plan.quests;continue;
        }
        if fields.len()==5 && fields[0]=="w1" {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("Invalid windmill owner"))?;
            let action=crate::windmill::Action::decode(fields[2])?;
            let time=fields[3].parse::<u64>().map_err(|_|invalid("Invalid windmill time"))?;
            let first=fields[4].parse::<u64>().map_err(|_|invalid("Invalid windmill identity"))?;
            let expected=records.values().map(|c|c.inventory.high_water()).max().unwrap_or(crate::inventory::ITEM_NAMESPACE).checked_add(1).ok_or_else(||invalid("Windmill identity overflow"))?;
            if first!=expected{return Err(invalid("Windmill item identity mismatch"));}
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Windmill owner unavailable"))?;
            let plan=crate::windmill::plan(&c.quests,&c.inventory,&c.professions,action,first,time)?;
            let (bag,state)=if plan.items.is_empty()&&plan.rewards.is_empty(){(c.inventory.clone(),c.progression.clone())}else{crate::player_transaction::candidate(&c.inventory,&c.progression,&plan.items,&plan.rewards,Some(first))?};
            c.inventory=bag;c.progression=state;c.quests=plan.quests;c.professions=plan.professions;continue;
        }
        if fields.len()==5 && fields[0]=="n1" {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("Invalid nature owner"))?;
            let action=crate::nature_gathering::Action::decode(fields[2])?;
            let time=fields[3].parse::<u64>().map_err(|_|invalid("Invalid nature time"))?;
            let first=fields[4].parse::<u64>().map_err(|_|invalid("Invalid nature identity"))?;
            let expected=records.values().map(|c|c.inventory.high_water()).max().unwrap_or(crate::inventory::ITEM_NAMESPACE).checked_add(1).ok_or_else(||invalid("Nature identity overflow"))?;
            if first!=expected {return Err(invalid("Nature item identity mismatch"));}
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Nature owner unavailable"))?;
            let plan=crate::nature_gathering::plan(&c.quests,&c.inventory,action,first,time)?;
            let (bag,state)=if plan.items.is_empty()&&plan.rewards.is_empty(){(c.inventory.clone(),c.progression.clone())}else{crate::player_transaction::candidate(&c.inventory,&c.progression,&plan.items,&plan.rewards,Some(first))?};
            c.inventory=bag;c.progression=state;c.quests=plan.quests;continue;
        }
        if fields.len()==5&&fields[0]=="c1" {
            let id=fields[1].parse::<u64>().map_err(|_|invalid("Invalid crafting owner"))?;
            let items=crate::player_transaction::decode_items(fields[2])?;
            let changes=crate::player_transaction::decode_changes(fields[3])?;
            let change:crate::professions::Change=serde_json::from_str(fields[4]).map_err(io::Error::other)?;
            let first=if items.iter().any(|i|i.allocated_id().is_some()){Some(records.values().map(|c|c.inventory.high_water()).max().unwrap_or(crate::inventory::ITEM_NAMESPACE).checked_add(1).ok_or_else(||invalid("Craft identity overflow"))?)}else{None};
            let c=records.get_mut(&id).filter(|c|!c.deleted&&c.progression.alive()).ok_or_else(||invalid("Crafting owner unavailable"))?;
            let book=c.professions.apply(&change)?;
            let (bag,state)=if items.is_empty()&&changes.is_empty(){(c.inventory.clone(),c.progression.clone())}else{crate::player_transaction::candidate(&c.inventory,&c.progression,&items,&changes,first)?};
            c.inventory=bag;c.progression=state;c.professions=book;continue;
        }
        if fields.len() == 4 && matches!(fields[0], "a1" | "a2" | "a3") {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid transaction owner"))?;
            let items = crate::player_transaction::decode_items(fields[2])?;
            let changes = crate::player_transaction::decode_changes(fields[3])?;
            let first = if items.iter().any(|command| command.allocated_id().is_some()) {
                Some(
                    records
                        .values()
                        .map(|c| c.inventory.high_water())
                        .max()
                        .unwrap_or(crate::inventory::ITEM_NAMESPACE)
                        .checked_add(1)
                        .ok_or_else(|| invalid("transaction item identity exhausted"))?,
                )
            } else {
                None
            };
            let character = records
                .get_mut(&id)
                .filter(|c| !c.deleted)
                .ok_or_else(|| invalid("unavailable transaction owner"))?;
            let (inventory, progression) = crate::player_transaction::candidate(
                &character.inventory,
                &character.progression,
                &items,
                &changes,
                first,
            )?;
            if fields[0] == "a2" {
                if !character.progression.alive() { return Err(invalid("Dead supply exchange owner")); }
                character.quests = crate::quests::validate_supplies_exchange(&character.quests, &character.inventory, &items, &changes, first.ok_or_else(|| invalid("Missing supply output identity"))?)?;
            }
            if fields[0] == "a3" {
                if !character.progression.alive() { return Err(invalid("Dead practice craft owner")); }
                character.quests = crate::quests::validate_practice_craft(&character.quests, &character.inventory, &items, &changes, first.ok_or_else(|| invalid("Missing craft output identity"))?)?;
            }
            character.inventory = inventory;
            character.progression = progression;
            continue;
        }
        if matches!((fields[0],fields.len()), ("wd1" | "wp1",4) | ("wd2",5) | ("wx1",3)) {
            drops::replay(&fields,&mut records,&mut drops)?;
            continue;
        }
        if fields.len() == 3 && fields[0] == "i1" {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid inventory character"))?;
            let command = crate::inventory::Command::decode(fields[2])
                .map_err(|_| invalid("invalid inventory event"))?;
            if let Some(allocated) = command.allocated_id() {
                let high = records
                    .values()
                    .map(|c| c.inventory.high_water())
                    .max()
                    .unwrap_or(crate::inventory::ITEM_NAMESPACE);
                if high.checked_add(1) != Some(allocated) {
                    return Err(invalid("invalid global item identity progression"));
                }
            }
            let character = records
                .get_mut(&id)
                .filter(|c| !c.deleted)
                .ok_or_else(|| invalid("inventory event references unavailable character"))?;
            character.inventory = character
                .inventory
                .apply(&command)
                .map_err(|_| invalid("invalid inventory transition"))?;
            continue;
        }
        if fields.len() == 3 && matches!(fields[0], "u1" | "h1" | "h2" | "o1") {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid UI character ID"))?;
            let character = records
                .get_mut(&id)
                .filter(|c| !c.deleted)
                .ok_or_else(|| invalid("UI event references unavailable character"))?;
            let hex = fields[2].as_bytes();
            if hex.len() > crate::preferences::MAX_BYTES * 2 || hex.len() % 2 != 0 {
                return Err(invalid("invalid UI settings encoding size"));
            }
            let mut bytes = Vec::with_capacity(hex.len() / 2);
            for pair in hex.chunks_exact(2) {
                let high = (pair[0] as char)
                    .to_digit(16)
                    .ok_or_else(|| invalid("invalid UI hex"))?;
                let low = (pair[1] as char)
                    .to_digit(16)
                    .ok_or_else(|| invalid("invalid UI hex"))?;
                bytes.push(((high << 4) | low) as u8);
            }
            if fields[0] == "u1" {
                character.ui = Some(
                    crate::preferences::UiSettings::decode(&bytes)
                        .map_err(|_| invalid("invalid saved UI settings"))?,
                );
            } else if fields[0] == "o1" {
                character.gameplay_options = Some(
                    crate::preferences::GameplayOptions::decode(&bytes)
                        .map_err(|_| invalid("invalid saved gameplay options"))?,
                );
            } else if fields[0] == "h2" {
                character.hotbar = crate::preferences::Hotbar::decode(&bytes)
                    .map_err(|_| invalid("invalid saved hotbar checkpoint"))?;
            } else {
                let update = crate::preferences::HotbarUpdate::decode(&bytes)
                    .map_err(|_| invalid("invalid saved hotbar update"))?;
                character.hotbar = character
                    .hotbar
                    .apply(&update)
                    .map_err(|_| invalid("saved hotbar exceeds limit"))?;
            }
            continue;
        }
        if fields.len() == 3 && fields[0] == "e1" {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid instance character ID"))?;
            let instance = fields[2]
                .parse::<u16>()
                .map_err(|_| invalid("invalid instance stamp"))?;
            let c = records
                .get_mut(&id)
                .filter(|c| !c.deleted)
                .ok_or_else(|| invalid("instance event references unavailable character"))?;
            if c.instance.checked_add(1) != Some(instance) {
                return Err(invalid("invalid instance stamp progression"));
            }
            c.instance = instance;
            continue;
        }
        if fields.len() == 2 && matches!(fields[0], "d1" | "r1") {
            let id = fields[1]
                .parse::<u64>()
                .map_err(|_| invalid("invalid character lifecycle ID"))?;
            let c = records
                .get(&id)
                .ok_or_else(|| invalid("lifecycle event references unknown character"))?;
            let deleting = fields[0] == "d1";
            if c.deleted == deleting {
                return Err(invalid("invalid character lifecycle transition"));
            }
            if !deleting
                && records
                    .values()
                    .filter(|other| other.account == c.account && !other.deleted)
                    .count()
                    >= 5
            {
                return Err(invalid("restoration exceeds local character limit"));
            }
            records.get_mut(&id).unwrap().deleted = deleting;
            continue;
        }
        if !((fields.len() == 4 && fields[0] == "v1") || (fields.len() == 5 && fields[0] == "v2"))
            || !valid_name(fields[2])
            || !valid_name(fields[3])
        {
            return Err(invalid("invalid journal record"));
        }
        let id = fields[1]
            .parse::<u64>()
            .map_err(|_| invalid("invalid character ID"))?;
        if id == 0 || records.contains_key(&id) {
            return Err(invalid("duplicate or zero character ID"));
        }
        if records
            .values()
            .any(|c: &Character| c.name.eq_ignore_ascii_case(fields[3]))
        {
            return Err(invalid("duplicate character name in journal"));
        }
        if records
            .values()
            .filter(|c| c.account == fields[2] && !c.deleted)
            .count()
            >= 5
        {
            return Err(invalid("account exceeds local character limit"));
        }
        let creation = if fields[0] == "v2" {
            Some(decode_creation(fields[3], fields[4])?)
        } else {
            None
        };
        records.insert(
            id,
            Character {
                id,
                account: fields[2].into(),
                name: fields[3].into(),
                creation,
                deleted: false,
                instance: 0,
                ui: None,
                gameplay_options: None,
                hotbar: crate::preferences::Hotbar::default(),
                inventory: crate::inventory::Inventory::default(),
                progression: crate::progression::Progression::default(),
                skills: crate::skills::Book::default(),
                professions: crate::professions::Book::default(),
                quests: crate::quests::Book::default(),
                legacy_kit_received: false,
                legacy_supplies_received: false,
                starter_gear: false,
                binding: None,
            },
        );
    }
    if let Some(output) = compacted {
        run.flush(output);
    }
    Ok(JournalState {
        characters: records,
        drops,
        relocations,
        revision,
    })
}

// Journal v2 uses canonical little-endian words encoded as hex, independent of network
// string padding. Physical tuple order and float bits survive without normalization.
fn encode_creation(request: &CharacterCreationRequest) -> io::Result<String> {
    if request.physical_values.len() > MAX_PHYSICAL_VALUES {
        return Err(invalid("too many physical values"));
    }
    let mut keys = std::collections::BTreeSet::new();
    if request
        .physical_values
        .iter()
        .any(|(key, bits)| !keys.insert(*key) || !f32::from_bits(*bits).is_finite())
    {
        return Err(invalid("invalid or duplicate physical value"));
    }
    let mut bytes = Vec::with_capacity(28 + request.physical_values.len() * 8);
    for word in [
        request.entity_did,
        request.start_area,
        request.race,
        request.sex,
        request.class,
        request.implement,
        request.physical_values.len() as u32,
    ] {
        bytes.extend(word.to_le_bytes());
    }
    for (key, bits) in &request.physical_values {
        bytes.extend(key.to_le_bytes());
        bytes.extend(bits.to_le_bytes());
    }
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut out = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        out.push(HEX[(b >> 4) as usize] as char);
        out.push(HEX[(b & 15) as usize] as char);
    }
    Ok(out)
}
fn decode_creation(name: &str, hex: &str) -> io::Result<CharacterCreationRequest> {
    if hex.len() < 56 || hex.len() > (28 + MAX_PHYSICAL_VALUES * 8) * 2 || hex.len() % 8 != 0 {
        return Err(invalid("invalid native character data length"));
    }
    fn nibble(b: u8) -> io::Result<u8> {
        match b {
            b'0'..=b'9' => Ok(b - b'0'),
            b'a'..=b'f' => Ok(b - b'a' + 10),
            _ => Err(invalid("invalid native character hex")),
        }
    }
    let bytes: Vec<u8> = hex
        .as_bytes()
        .chunks_exact(2)
        .map(|p| Ok((nibble(p[0])? << 4) | nibble(p[1])?))
        .collect::<io::Result<_>>()?;
    let words: Vec<u32> = bytes
        .chunks_exact(4)
        .map(|w| u32::from_le_bytes(w.try_into().unwrap()))
        .collect();
    let count = words[6] as usize;
    if count > MAX_PHYSICAL_VALUES || words.len() != 7 + count * 2 {
        return Err(invalid("invalid native character physical count"));
    }
    let request = CharacterCreationRequest {
        name: name.into(),
        entity_did: words[0],
        start_area: words[1],
        race: words[2],
        sex: words[3],
        class: words[4],
        implement: words[5],
        physical_values: words[7..].chunks_exact(2).map(|w| (w[0], w[1])).collect(),
    };
    encode_creation(&request)?;
    Ok(request)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn partial_vitae_recovery_survives_binding_relog_and_compaction() {
        use crate::progression::Command::*;
        let dir = std::env::temp_dir().join(format!("ac2-vitae-{}", std::process::id()));
        let expected = {
            let mut store = Store::open(&dir).unwrap();
            let mut hero = request("Vitae Hero");
            hero.race = 1;
            hero.sex = 4096;
            store.create_native("local", &hero).unwrap();
            store.progression_command("local", 1, AwardXp(1_000_000)).unwrap();
            store.bind_lifestone("local", 1, Placement::HUMAN_START).unwrap();
            store.progression_command("local", 1, Damage(10_000)).unwrap();
            store.return_to_binding("local", 1, true).unwrap();
            for _ in 0..10 { store.progression_command("local", 1, AwardXp(1)).unwrap(); }
            let state = store.characters[&1].progression.clone();
            assert_eq!((state.vitae, state.vitae_xp), (95, 10));
            state
        };
        {
            let mut store = Store::open(&dir).unwrap();
            assert_eq!(store.characters[&1].progression, expected);
            store.compact_journal().unwrap();
        }
        assert_eq!(Store::open(&dir).unwrap().characters[&1].progression, expected);
        let historical = compaction::progression("0,0,0,100,100", &crate::progression::Progression::default()).unwrap();
        assert_eq!(historical, crate::progression::Progression::default());
        let dead = historical.apply(&Damage(100)).unwrap();
        assert_eq!(dead.apply(&crate::progression::Command::decode("z").unwrap()).unwrap().vitae, 100);
        fs::remove_dir_all(dir).unwrap();
    }

    #[test]
    fn historical_training_grants_replay_without_a_live_grant_api() {
        for revision in ["t1", "t2", "t4"] {
            let dir = std::env::temp_dir().join(format!("ac2-legacy-training-{revision}-{}", std::process::id()));
            {
                let mut store = Store::open(&dir).unwrap();
                let mut profile = request("Legacy Hero");
                profile.entity_did = crate::bootstrap::PLAYER_ENTITY;
                profile.race = 1;
                profile.sex = 4096;
                store.create_native("local", &profile).unwrap();
                let first = store.next_item_id().unwrap();
                store.append_record(&format!("{revision}\t1\t{first}")).unwrap();
                if revision != "t2" {store.append_record("k4\t1").unwrap();}
            }
            let expected;
            {
                let mut store = Store::open(&dir).unwrap();
                assert!(store.characters[&1].skills.get(crate::skills::TRAINING_ATTACK).is_some());
                let first = store.next_item_id().unwrap();
                expected = crate::training_kit::supply_inventory(&store.characters[&1].inventory, first).unwrap();
                store.append_record(&format!("t3\t1\t{first}")).unwrap();
            }
            {
                let mut store = Store::open(&dir).unwrap();
                assert_eq!(store.characters[&1].inventory, expected);
                store.compact_journal().unwrap();
            }
            {
                let store = Store::open(&dir).unwrap();
                assert_eq!(store.characters[&1].inventory, expected);
                assert!(store.characters[&1].legacy_supplies_received);
            }
            fs::remove_dir_all(dir).unwrap();
        }
    }
    #[test]
    fn hotbar_initial_edit_clear_restart_and_compaction_preserve_character_preferences() {
        use crate::preferences::{Hotbar, HotbarUpdate};
        fn update(slot:u32,skill:u32)->HotbarUpdate {
            let mut words=vec![5u32,1,1,0x44000100,0,0xffff00ac];
            if skill==0 {words.push(0);} else {words.extend([0x40000001,skill]);}
            words.extend([0,1,slot]);
            HotbarUpdate::decode(&words.into_iter().flat_map(u32::to_le_bytes).collect::<Vec<_>>()).unwrap()
        }
        let dir=std::env::temp_dir().join(format!("ac2-hotbar-{}-{}",std::process::id(),std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
        let expected=Hotbar::starter(1,4096).apply(&update(5,0xb1)).unwrap();
        {
            let mut s=Store::open(&dir).unwrap();let mut c=request("Hotbar Hero");c.race=1;c.sex=4096;
            s.create_native("local",&c).unwrap();c.name="Other Hero".into();s.create_native("local",&c).unwrap();
            assert!(s.update_hotbar("other",1,update(5,0xb1)).is_err());
            s.update_hotbar("local",1,update(5,0xb1)).unwrap();
            assert_eq!(s.characters[&1].hotbar,expected);
            assert!(!s.characters[&2].hotbar.is_initialized());
        }
        {
            let mut s=Store::open(&dir).unwrap();assert_eq!(s.characters[&1].hotbar,expected);
            for slot in 0..100 {s.update_hotbar("local",1,update(slot,0)).unwrap();}
            assert!(s.characters[&1].hotbar.is_empty() && s.characters[&1].hotbar.is_initialized());
            // An empty first write must also establish a saved preference.
            for slot in 0..3 {s.update_hotbar("local",2,update(slot,0)).unwrap();}
            assert!(s.characters[&2].hotbar.is_initialized());s.compact_journal().unwrap();
        }
        {
            let mut s=Store::open(&dir).unwrap();
            for id in [1,2] {assert!(s.characters[&id].hotbar.is_empty() && s.characters[&id].hotbar.is_initialized());}
            s.update_hotbar("local",1,update(99,0x60)).unwrap();
            assert_eq!(s.characters[&1].hotbar,Hotbar::default().apply(&update(99,0x60)).unwrap());
        }
        fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn invested_skill_untraining_refunds_earned_xp_and_survives_restart() {
        use crate::{skills::Command as S,progression::Command as P};
        let dir=std::env::temp_dir().join(format!("ac2-untrain-{}-{}",std::process::id(),std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
        let partial;
        {
            let mut s=Store::open(&dir).unwrap();let mut creation=request("Skill Hero");creation.race=1;creation.sex=4096;
            s.create_native("local",&creation).unwrap();
            s.progression_command("local",1,P::AwardXp(10000)).unwrap();
            s.ensure_starter_skills("local",1).unwrap();
            for id in [0x59,0x55] {s.skill_command("local",1,S::Train(id)).unwrap();}
            s.raise_skill("local",1,0x55).unwrap();
            let invested=s.characters[&1].skills.get(0x55).unwrap().xp;assert!(invested>1);
            assert!(s.skill_command("local",1,S::Untrain(0x59)).is_err());
            s.skill_command("local",1,S::Untrain(0x55)).unwrap();
            assert!(!s.characters[&1].skills.usable(0x55));
            assert!(s.raise_skill("local",1,0x55).is_err());
            assert!(!s.debit_untraining(1).unwrap());
            let available=s.characters[&1].progression.available_xp;
            s.progression_command("local",1,P::AwardXp(1)).unwrap();
            assert!(s.debit_untraining(1).unwrap());
            assert_eq!(s.characters[&1].skills.get(0x55).unwrap().xp,invested-1);
            assert_eq!(s.characters[&1].progression.available_xp,available+2);
            assert!(!s.debit_untraining(1).unwrap());
            partial=s.characters[&1].clone();
            s.compact_journal().unwrap();
        }
        {
            let mut s=Store::open(&dir).unwrap();assert_eq!(s.characters[&1],partial);
            s.skill_command("local",1,S::CancelUntrain).unwrap();assert!(s.characters[&1].skills.usable(0x55));
            let remaining=s.characters[&1].skills.get(0x55).unwrap().xp;
            s.progression_command("local",1,P::AwardXp(10)).unwrap();assert!(!s.debit_untraining(1).unwrap());
            s.skill_command("local",1,S::Untrain(0x55)).unwrap();
            s.progression_command("local",1,P::AwardXp(remaining+20)).unwrap();s.debit_untraining(1).unwrap();
            assert!(s.characters[&1].skills.get(0x55).is_none());assert_eq!(s.characters[&1].skills.untraining_id(),0);
            assert_eq!(s.characters[&1].progression.available_xp,s.characters[&1].progression.xp);
        }
        {let s=Store::open(&dir).unwrap();assert!(s.characters[&1].skills.get(0x55).is_none());}
        fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn shelter_shortcut_replays_travel_and_only_the_unpaid_reward() {
        use crate::{quests::Command as Q, shelter};
        let dir=std::env::temp_dir().join(format!("ac2-shelter-exit-{}",std::process::id()));
        let dest=Placement{cell:0x74ba0008,coordinates:[5.11786,154.12,266.168],heading:0};
        let id;
        {
            let mut s=Store::open(&dir).unwrap();
            id=s.create_native("local",&request("Early Exit")).unwrap().id;
            assert!(s.quest_command("local",id,Q::ConfirmExitSkull).is_err());
            s.quest_command("local",id,Q::ReadExitWarning).unwrap();
            assert!(s.relocate_training_portal("local",id,dest,shelter::HUMAN_QUICK_EXIT).is_err());
            s.quest_command("local",id,Q::ConfirmExitSkull).unwrap();
            for command in [Q::ShelterGuard,Q::ShelterLieutenant,Q::ShelterSentry] {
                s.quest_command("local",id,command).unwrap();
            }
            let item=shelter::item(shelter::TOOL,s.next_item_id().unwrap(),0).unwrap();
            s.inventory_command("local",id,crate::inventory::Command::Grant(item)).unwrap();
            s.shelter_exchange("local",id,Q::ShelterTool).unwrap();
            assert_eq!(s.characters[&id].progression.xp,40);
        }
        {
            let mut s=Store::open(&dir).unwrap();
            for _ in 0..2 { s.relocate_training_portal("local",id,dest,shelter::HUMAN_QUICK_EXIT).unwrap(); }
            s.compact_journal().unwrap();
        }
        {
            let s=Store::open(&dir).unwrap();
            assert_eq!(s.placement(id),Some(dest));
            assert_eq!(s.characters[&id].quests.shelter_phase(),7);
            assert_eq!(s.characters[&id].progression.xp,105);
        }
        fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn shelter_exchanges_replay_without_duplicate_rewards_or_lost_travel() {
        use crate::{quests::Command as Q,inventory::Command as I,shelter};
        let dir=std::env::temp_dir().join(format!("ac2-shelter-{}",std::process::id()));
        let final_state;
        {
            let mut s=Store::open(&dir).unwrap();let c=s.create_native("local",&request("Shelter Hero")).unwrap();
            assert!(s.shelter_exchange("local",c.id,Q::ShelterComplete).is_err());
            for q in [Q::ShelterGuard,Q::ShelterLieutenant,Q::ShelterSentry] {s.quest_command("local",c.id,q).unwrap();}
            assert!(s.shelter_exchange("local",c.id,Q::ShelterTool).is_err());
            let id=s.next_item_id().unwrap();s.inventory_command("local",c.id,I::Grant(shelter::item(shelter::TOOL,id,0).unwrap())).unwrap();
            assert!(s.shelter_exchange("other",c.id,Q::ShelterTool).is_err());
            s.shelter_exchange("local",c.id,Q::ShelterTool).unwrap();
            assert!(s.shelter_exchange("local",c.id,Q::ShelterTool).is_err());
            assert_eq!(s.characters[&c.id].inventory.items().count(),3);
            let id=s.next_item_id().unwrap();s.inventory_command("local",c.id,I::Grant(shelter::item(shelter::GLYPH,id,3).unwrap())).unwrap();
            s.shelter_exchange("local",c.id,Q::ShelterGlyph).unwrap();
            assert_eq!(s.placement(c.id),Some(shelter::VAULT));
            s.shelter_exchange("local",c.id,Q::ShelterAdvance).unwrap();s.shelter_exchange("local",c.id,Q::ShelterComplete).unwrap();
            final_state=s.characters[&c.id].clone();
            assert_eq!(final_state.quests.shelter_phase(),7);
            assert!(s.shelter_exchange("local",c.id,Q::ShelterComplete).is_err());
        }
        {let s=Store::open(&dir).unwrap();assert_eq!(s.characters[&1],final_state);assert_eq!(s.placement(1),Some(shelter::VAULT));}
        // A character that credited the shard in the previous release can return
        // after a disconnect, activate the native shard, and leave without more XP.
        let native_shard = 0x814000000003100b;
        let dest = crate::starters::shard_route(native_shard).unwrap().destination;
        {
            let mut s = Store::open(&dir).unwrap();
            s.relocate_training_portal("local", 1, dest, native_shard).unwrap();
            assert_eq!(s.characters[&1].progression, final_state.progression);
        }
        {
            let s = Store::open(&dir).unwrap();
            assert_eq!(s.placement(1), Some(dest));
            assert_eq!(s.characters[&1].quests.shelter_phase(), 7);
            assert_eq!(s.characters[&1].progression.xp, 105);
        }
        for file in ["characters.journal","positions.snapshot"] {let _=fs::remove_file(dir.join(file));}fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn shelter_full_bag_rejects_without_consuming_tool_or_advancing_quest() {
        use crate::{quests::Command as Q,inventory::{Command as I,BAG_SLOTS},shelter};
        let dir=std::env::temp_dir().join(format!("ac2-shelter-full-{}",std::process::id()));
        {
            let mut s=Store::open(&dir).unwrap();let c=s.create_native("local",&request("Full Pack")).unwrap();
            for q in [Q::ShelterGuard,Q::ShelterLieutenant,Q::ShelterSentry] {s.quest_command("local",c.id,q).unwrap();}
            for slot in 0..BAG_SLOTS {
                let id=s.next_item_id().unwrap();
                s.inventory_command("local",c.id,I::Grant(shelter::item(if slot==0 {shelter::TOOL}else{0x47001583},id,slot).unwrap())).unwrap();
            }
            let before=s.characters[&c.id].clone();let journal=fs::read(dir.join("characters.journal")).unwrap();
            assert!(s.shelter_exchange("local",c.id,Q::ShelterTool).is_err());
            assert_eq!(s.characters[&c.id],before);assert_eq!(fs::read(dir.join("characters.journal")).unwrap(),journal);
        }
        for file in ["characters.journal","positions.snapshot"] {let _=fs::remove_file(dir.join(file));}fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn deletion_restoration_keeps_identity_appearance_and_saved_placement() {
        let dir = std::env::temp_dir().join(format!("ac2-lifecycle-{}", std::process::id()));
        {
            let mut store = Store::open(&dir).unwrap();
            for i in 1..=5 {
                store
                    .create_native("local", &request(&format!("Hero {i}")))
                    .unwrap();
            }
            store.set_placement(5, Placement::HUMAN_START).unwrap();
            store.flush_positions().unwrap();
            assert!(store.delete("other", 5).is_err());
            store.delete("local", 5).unwrap();
            assert!(store.set_placement(5, Placement::HUMAN_START).is_err());
            assert!(store.delete("local", 5).is_err());
            assert_eq!(store.create("local", "Sixth Hero").unwrap().id, 6);
            assert!(store.restore("local", 5).is_err());
            store.delete("local", 6).unwrap();
            store.restore("local", 5).unwrap();
            assert!(store.restore("other", 6).is_err());
            assert!(store.create("other", "Sixth Hero").is_err());
        }
        {
            let store = Store::open(&dir).unwrap();
            assert!(!store.characters[&5].deleted);
            assert!(store.characters[&6].deleted);
            assert_eq!(store.characters[&5].creation, Some(request("Hero 5")));
            assert_eq!(store.placement(5), Some(Placement::HUMAN_START));
        }
        for line in ["d1\t1", "r1\t1"] {
            assert!(decode_journal(&record(line)).is_err());
        }
        let mut bad = record("v1\t1\tlocal\tHero");
        bad.extend(record("r1\t1"));
        assert!(decode_journal(&bad).is_err());
        for file in ["characters.journal", "positions.snapshot"] {
            fs::remove_file(dir.join(file)).unwrap();
        }
        fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn placement_snapshots_replace_durably_and_reject_corruption() {
        let dir = std::env::temp_dir().join(format!("ac2-positions-{}", std::process::id()));
        let mut final_position = Placement::HUMAN_START;
        {
            let mut store = Store::open(&dir).unwrap();
            store.create("local", "Explorer").unwrap();
            assert!(store.set_placement(2, final_position).is_err());
            store.set_placement(1, final_position).unwrap();
            store.flush_positions().unwrap();
            final_position.coordinates[0] += 4.0;
            store.set_placement(1, final_position).unwrap();
            store.flush_positions().unwrap();
        }
        {
            let store = Store::open(&dir).unwrap();
            assert_eq!(store.placement(1), Some(final_position));
            let bytes = fs::read(dir.join("positions.snapshot")).unwrap();
            for end in 0..bytes.len() {
                assert!(decode_positions(&bytes[..end], &store.characters).is_err());
            }
            let mut corrupt = bytes;
            corrupt[20] ^= 1;
            assert!(decode_positions(&corrupt, &store.characters).is_err());
            // A valid checksum is insufficient when a placement references no character.
            assert!(
                decode_positions(
                    &fs::read(dir.join("positions.snapshot")).unwrap(),
                    &BTreeMap::new()
                )
                .is_err()
            );
        }
        fs::remove_file(dir.join("characters.journal")).unwrap();
        fs::remove_file(dir.join("positions.snapshot")).unwrap();
        fs::remove_dir(dir).unwrap();
    }
    fn request(name: &str) -> CharacterCreationRequest {
        CharacterCreationRequest {
            name: name.into(),
            entity_did: 0x10001234,
            start_area: 7,
            race: 2,
            sex: 1,
            class: 3,
            implement: 4,
            physical_values: vec![(9, (-0.0f32).to_bits()), (2, 0.1234567f32.to_bits())],
        }
    }
    #[test]
    fn npc_bounties_complete_on_the_native_kill_and_survive_restart() {
        use crate::progression::Command::AwardXp;
        use crate::recovered_quests::{KANTEN, DEBT};
        const AKKILEA: u32 = 0x47001a98;
        let dir = std::env::temp_dir().join(format!("ac2-npc-bounties-{}", std::process::id()));
        let now = crate::recovered_quests::now();
        let mut store = Store::open(&dir).unwrap();
        store.create("local", "Bounty Hunter").unwrap();
        assert!(store.accept_npc_bounty("local", 1, AKKILEA, now).is_err());
        store.progression_command("local", 1, AwardXp(10_000_000)).unwrap();
        assert!(store.accept_npc_bounty("other", 1, AKKILEA, now).is_err());
        assert!(store.accept_npc_bounty("local", 1, KANTEN, now).is_err());
        store.recovered_delivery("local", 1, KANTEN, now).unwrap();
        store.accept_npc_bounty("local", 1, KANTEN, now).unwrap();
        store.accept_npc_bounty("local", 1, AKKILEA, now).unwrap();
        assert!(store.accept_npc_bounty("local", 1, AKKILEA, now + 1).is_err());
        let xp = store.characters[&1].progression.xp;
        store.encounter_reward("local", 1, 0x47001680, &[AwardXp(1)]).unwrap(); // Polluter is a different monster
        store.encounter_reward("local", 1, 0x470008a0, &[AwardXp(1)]).unwrap();
        store.encounter_reward("local", 1, 0x4700108b, &[AwardXp(1)]).unwrap();
        store.encounter_reward("local", 1, 0x4700108b, &[AwardXp(1)]).unwrap();
        let c = &store.characters[&1];
        assert_eq!(c.quests.recovered.0[&DEBT].completed, 0);
        assert_eq!(c.quests.recovered.0[&0x40000181].rewarded_xp, 1500);
        assert_eq!(c.quests.recovered.0[&0x400001af].rewarded_xp, 10000);
        assert_eq!(c.progression.xp, xp + 11504);
        let quests = c.quests.clone();
        let state = c.progression.clone();
        drop(store);
        let store = Store::open(&dir).unwrap();
        assert_eq!(store.characters[&1].quests, quests);
        assert_eq!(store.characters[&1].progression, state);
        drop(store);
        fs::remove_dir_all(dir).unwrap();
    }

    #[test]
    fn starter_hunt_replays_historic_level_one_elixir_and_current_ancestor() {
        use crate::inventory::{Command::Grant, Item, Location, ITEM_NAMESPACE};
        use crate::progression::Command::AwardXp;
        use crate::recovered_quests::{now, HuntRules};
        const ANCESTOR: u32 = 0x470013b6;
        const BEETLE: u32 = 0x40000009;
        const ARMOREDILLO: u32 = 0x4000000b;
        let dir = std::env::temp_dir().join(format!("ac2-starter-hunt-{}", std::process::id()));
        let elixir = Item { id: ITEM_NAMESPACE + 1, definition: 0x47000f42, visual: 0x1f000e53,
            quantity: 1, stack_limit: 1, equipment_mask: 0, location: Location::Bag(0), roll: None };
        let time = now();
        {
            let mut store = Store::open(&dir).unwrap();
            store.create("local", "Ancestor Hunter").unwrap();
            store.inventory_command("local", 1, Grant(elixir.clone())).unwrap();
            assert!(store.recovered_elixir("local", 1, elixir.id).is_err());
            assert!(store.accept_npc_bounty("local", 1, ANCESTOR, time).is_err());
            // Previous releases allowed a level-one Beetle Slayer bestowal.
            store.append_record(&format!("q7\t1\t{}\t{BEETLE}\t{time}", elixir.id)).unwrap();
        }
        let (quests, progress) = {
            let mut store = Store::open(&dir).unwrap();
            let c = &store.characters[&1];
            assert!(c.inventory.items().all(|i| i.id != elixir.id));
            assert_eq!(c.quests.recovered.0[&BEETLE].rules, HuntRules::Hero);
            store.progression_command("local", 1, AwardXp(10000)).unwrap();
            assert!(store.accept_npc_bounty("other", 1, ANCESTOR, time).is_err());
            store.accept_npc_bounty("local", 1, ANCESTOR, time).unwrap();
            for _ in 0..10 {
                for id in [BEETLE, ARMOREDILLO] {
                    let q = crate::recovered_quests::profiles().iter().find(|q| q.id == id).unwrap();
                    store.encounter_reward("local", 1, q.targets[0], &[AwardXp(1)]).unwrap();
                }
            }
            let c = &store.characters[&1];
            assert_eq!((c.progression.xp, c.progression.quest_points), (10270, 40));
            assert_eq!(c.quests.recovered.0[&ARMOREDILLO].rewarded_xp, 150);
            (c.quests.clone(), c.progression.clone())
        };
        let store = Store::open(&dir).unwrap();
        assert_eq!((&store.characters[&1].quests, &store.characters[&1].progression), (&quests, &progress));
        drop(store);
        fs::remove_dir_all(dir).unwrap();
    }

    #[test]
    fn inferred_hunts_elixirs_and_rolled_armor_survive_restart_atomically() {
        use crate::{inventory::{Item,Roll,Location,Command as I,ITEM_NAMESPACE},progression::Command as P};
        let dir=std::env::temp_dir().join(format!("ac2-recovered-gameplay-{}",std::process::id()));
        let q=crate::recovered_quests::profiles().iter().find(|q|q.id==0x40000097).unwrap();
        let armor=Item{roll:Some(Roll{armor:17,tint:731,level:7,..Default::default()}),id:ITEM_NAMESPACE+1,
            definition:0x47000fdc,visual:0x1f00058c,quantity:1,stack_limit:1,equipment_mask:16,location:Location::Bag(0)};
        let before;let finished;let bag;
        {
            let mut s=Store::open(&dir).unwrap();s.create("local","Recovered Hero").unwrap();
            s.progression_command("local",1,P::AwardXp(10000)).unwrap();
            s.inventory_command("local",1,I::Grant(armor.clone())).unwrap();
            s.inventory_command("local",1,I::Wear{item:armor.id,slot:16}).unwrap();
            assert_eq!(crate::apparel::armor(&s.characters[&1].inventory,1),17);
            assert!(crate::apparel::aspects(&s.characters[&1].inventory,1,0x1000).iter().any(|(_,a,v)|*a==2&&(*v-0.731).abs()<0.0001));
            before=s.characters[&1].progression.xp;
            let normal=[P::AwardXp(1)];
            s.encounter_reward("local",1,q.kill_starters[0],&normal).unwrap();
            for _ in 0..q.goal {s.encounter_reward("local",1,q.targets[0],&normal).unwrap();}
            assert_eq!(s.characters[&1].progression.xp,before+u64::from(q.goal)+1+q.xp);
            finished=s.characters[&1].quests.recovered.0[&q.id].clone();assert!(finished.completed>0);
            s.encounter_reward("local",1,q.kill_starters[0],&normal).unwrap();
            assert_eq!(s.characters[&1].quests.recovered.0[&q.id],finished);
            for offset in 2..=3 {
                let elixir=Item{roll:None,id:ITEM_NAMESPACE+offset,definition:0x47000f43,visual:0x1f0001cf,
                    quantity:1,stack_limit:1,equipment_mask:0,location:Location::Bag((offset-2) as u16)};
                s.inventory_command("local",1,I::Grant(elixir)).unwrap();
            }
            let length=s.file.metadata().unwrap().len();
            assert!(s.recovered_elixir("other",1,ITEM_NAMESPACE+2).is_err());assert_eq!(s.file.metadata().unwrap().len(),length);
            s.recovered_elixir("local",1,ITEM_NAMESPACE+2).unwrap();
            let length=s.file.metadata().unwrap().len();
            assert!(s.recovered_elixir("local",1,ITEM_NAMESPACE+3).is_err());assert_eq!(s.file.metadata().unwrap().len(),length);
            assert!(s.characters[&1].inventory.items().any(|i|i.id==ITEM_NAMESPACE+3));
            bag=s.characters[&1].inventory.clone();
        }
        {
            let s=Store::open(&dir).unwrap();let c=&s.characters[&1];
            assert_eq!(c.inventory,bag);assert_eq!(c.quests.recovered.0[&q.id],finished);
            assert_eq!(c.progression.xp,before+u64::from(q.goal)+2+q.xp);
            assert_eq!(c.quests.recovered.0[&0x4000000a].count,0);
            assert_eq!(crate::apparel::armor(&c.inventory,1),17);
            assert!(!c.inventory.items().any(|i|i.id==ITEM_NAMESPACE+2));
        }
        std::fs::remove_file(dir.join("characters.journal")).unwrap();std::fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn recovered_delivery_consumes_owned_bag_and_replays_without_duplicate_xp() {
        use crate::{recovered_quests::{DEBT,KANTEN,ORVEN,BAG},progression::Command as P};
        let dir=std::env::temp_dir().join(format!("ac2-recovered-delivery-{}",std::process::id()));
        let xp;
        {
            let mut s=Store::open(&dir).unwrap();s.create("local","Delivery Hero").unwrap();
            let length=s.file.metadata().unwrap().len();
            assert!(s.recovered_delivery("local",1,KANTEN,100).is_err());assert_eq!(s.file.metadata().unwrap().len(),length);
            s.progression_command("local",1,P::AwardXp(100000)).unwrap();
            s.recovered_delivery("local",1,KANTEN,100).unwrap();
            assert_eq!(s.characters[&1].inventory.items().filter(|i|i.definition==BAG).count(),1);
            assert!(s.recovered_delivery("local",1,KANTEN,101).is_err());
            assert!(s.recovered_delivery("other",1,ORVEN,102).is_err());
            assert!(s.recovered_delivery("local",1,ORVEN,7300).is_err());
            // Renewing an expired job reuses the owned bag, never duplicates it.
            s.recovered_delivery("local",1,KANTEN,7300).unwrap();
            assert_eq!(s.characters[&1].inventory.items().filter(|i|i.definition==BAG).count(),1);
            s.recovered_delivery("local",1,ORVEN,7301).unwrap();
            xp=s.characters[&1].progression.xp;assert_eq!(xp,101000);
            assert!(s.recovered_delivery("local",1,ORVEN,7302).is_err());
            assert!(s.recovered_delivery("local",1,KANTEN,7302).is_err());
        }
        {
            let s=Store::open(&dir).unwrap();let c=&s.characters[&1];assert_eq!(c.progression.xp,xp);
            assert_eq!(c.quests.recovered.0[&DEBT].completed,7301);
            assert!(!c.inventory.items().any(|i|i.definition==BAG));
        }
        std::fs::remove_file(dir.join("characters.journal")).unwrap();std::fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn aerie_courier_requires_four_turn_ins_and_exchanges_the_report_once_after_relog() {
        use crate::progression::Command::AwardXp;
        let dir = std::env::temp_dir().join(format!("ac2-aerie-courier-{}", std::process::id()));
        let hunts = [0x4000fc24, 0x4000fc2f, 0x4000fc30, 0x4000fc31];
        let (otonku, maekarn, journey) = (0x470040ea, 0x470040ed, 0x4000fe02);
        let start = crate::recovered_quests::now();
        let report = crate::recovered_quests::report();
        let quest_xp;
        {
            let mut store = Store::open(&dir).unwrap();
            store.create("local", "Aerie Courier").unwrap();
            while store.characters[&1].progression.level().number < 70 {
                let p = &store.characters[&1].progression;
                store.progression_command("local", 1, AwardXp(p.level().xp_cap + 1 - p.xp)).unwrap();
            }
            for id in hunts {
                let q = crate::hunting_quests::profile(id).unwrap();
                store.hunting_conversation("local", 1, q.npc, start).unwrap();
                for _ in 0..q.goal {
                    store.encounter_reward("local", 1, q.targets[0], &[AwardXp(1)]).unwrap();
                }
                assert!(store.recovered_delivery("local", 1, otonku, start + 1).is_err());
                store.hunting_conversation("local", 1, q.npc, start).unwrap();
            }
            // A repeat in progress still leaves the original KEF commendation valid.
            let repeat = start + 2 + 7 * 86400;
            let hunt = crate::hunting_quests::profile(hunts[0]).unwrap();
            store.hunting_conversation("local", 1, hunt.npc, repeat).unwrap();
            assert_eq!(store.characters[&1].quests.hunting.0[&hunt.id].count, 0);
            store.recovered_delivery("local", 1, otonku, repeat).unwrap();
            assert!(store.recovered_delivery("local", 1, otonku, repeat + 1).is_err());
            assert_eq!(store.characters[&1].inventory.items().filter(|i| i.definition == report).count(), 1);
            assert!(store.recovered_delivery("local", 1, maekarn, repeat + 601200).is_err());
            // Renew the expired assignment with the same physical report.
            store.recovered_delivery("local", 1, otonku, repeat + 601200).unwrap();
            store.compact_journal().unwrap();
        }
        {
            let mut store = Store::open(&dir).unwrap();
            let c = &store.characters[&1];
            assert_eq!(c.inventory.items().filter(|i| i.definition == report).count(), 1);
            let before = c.progression.xp;
            let expected = crate::progression::quest_level_span(c.progression.level().number) / 100;
            let delivered = c.quests.recovered.0[&journey].started + 1;
            store.recovered_delivery("local", 1, maekarn, delivered).unwrap();
            let c = &store.characters[&1];
            quest_xp = c.progression.xp;
            assert_eq!(quest_xp - before, expected);
            assert_eq!(c.quests.recovered.0[&journey].rewarded_xp, expected);
            assert!(store.recovered_delivery("local", 1, maekarn, delivered + 1).is_err());
            assert!(store.recovered_delivery("local", 1, otonku, delivered + 601199).is_err());
        }
        let store = Store::open(&dir).unwrap();
        assert_eq!(store.characters[&1].progression.xp, quest_xp);
        assert!(!store.characters[&1].inventory.items().any(|i| i.definition == report));
        drop(store);
        for file in ["characters.journal", "characters.journal.previous", "positions.snapshot"] {
            std::fs::remove_file(dir.join(file)).unwrap();
        }
        std::fs::remove_dir(dir).unwrap();
    }

    #[test]
    fn warder_referrals_replay_with_exactly_once_rewards() {
        let dir=std::env::temp_dir().join(format!("ac2-warder-{}-{}",std::process::id(),crate::recovered_quests::now()));
        let p=crate::warder_referrals::profiles();
        {
            let mut s=Store::open(&dir).unwrap();s.create("local","Warder Walker").unwrap();
            assert!(s.warder_conversation("other",1,p[1].giver,100).is_err());
            s.warder_conversation("local",1,p[1].giver,100).unwrap();
            s.compact_journal().unwrap();
        }
        {
            let mut s=Store::open(&dir).unwrap();assert_eq!(s.characters[&1].quests.warder.0[&p[1].id].completed,0);
            s.warder_conversation("local",1,p[1].recipient,101).unwrap();
            assert_eq!(s.characters[&1].quests.warder.0[&p[2].id].started,101);
            s.warder_conversation("local",1,p[2].recipient,102).unwrap();
            let length=s.file.metadata().unwrap().len();s.warder_conversation("local",1,p[2].recipient,103).unwrap();
            assert_eq!(s.file.metadata().unwrap().len(),length);s.compact_journal().unwrap();
        }
        {
            let s=Store::open(&dir).unwrap();let c=&s.characters[&1];assert_eq!(c.progression.xp,30);
            assert_eq!(c.quests.warder.0[&p[2].id].completed,102);assert_eq!(c.quests.updates().unwrap().len(),2);
        }
        std::fs::remove_file(dir.join("characters.journal")).unwrap();std::fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn drudgerton_named_speakers_level_expiry_reward_and_replay() {
        use crate::warder_referrals::{profiles, speaker_entity};
        let giver_iid = 0x8140d00000002320;
        let recipient_iid = giver_iid + 1;
        let giver = speaker_entity(giver_iid, 0x470042f9, Some("Jrent")).unwrap();
        let recipient = speaker_entity(recipient_iid, 0x4700279e, Some("Jellio")).unwrap();
        assert_eq!(speaker_entity(giver_iid + 8, 0x470042f9, Some("Cedric")), None);
        assert_eq!(speaker_entity(recipient_iid + 8, 0x4700279e, Some("New Drudge Republic Representative")), None);
        assert_eq!(speaker_entity(giver_iid, 0x470042f9, Some("Cedric")), None);
        assert_eq!(speaker_entity(giver_iid, 0x4700279e, Some("Jrent")), None);
        let quest = profiles().iter().find(|q| q.giver == giver && q.recipient == recipient).unwrap();
        assert_eq!((quest.level, quest.limit, quest.repeat), (40, 604800, 604800));
        let dir = std::env::temp_dir().join(format!("ac2-drudgerton-{}-{}", std::process::id(), crate::recovered_quests::now()));
        let initial_xp = 43_141_054;
        let started = 100;
        let expired = started + quest.limit;
        let completed = expired + 1;
        {
            let mut s = Store::open(&dir).unwrap();
            s.create("local", "Drudge Traveler").unwrap();
            s.progression_command("local", 1, crate::progression::Command::AwardXp(initial_xp - 1)).unwrap();
            s.warder_conversation("local", 1, giver, started).unwrap();
            assert!(s.characters[&1].quests.warder.0.is_empty());
            s.progression_command("local", 1, crate::progression::Command::AwardXp(1)).unwrap();
            s.warder_conversation("local", 1, recipient, started).unwrap();
            assert!(s.characters[&1].quests.warder.0.is_empty());
            s.warder_conversation("local", 1, giver, started).unwrap();
            s.warder_conversation("local", 1, recipient, expired).unwrap();
            assert_eq!(s.characters[&1].progression.quest_points, 0);
            assert!(s.characters[&1].quests.warder.0[&quest.id].expired(quest, expired));
            s.warder_conversation("local", 1, giver, expired).unwrap();
        }
        {
            let mut s = Store::open(&dir).unwrap();
            let conversation = s.characters[&1].quests.warder.converse(recipient, 40, false, completed).unwrap();
            assert!(crate::interaction::speech(recipient_iid, "Jellio", &conversation.text).is_ok());
            assert!(crate::interaction::dialogue("Jellio", &conversation.text).is_ok());
            s.warder_conversation("local", 1, recipient, completed).unwrap();
            s.warder_conversation("local", 1, recipient, completed + 1).unwrap();
            s.compact_journal().unwrap();
        }
        {
            let mut s = Store::open(&dir).unwrap();
            let c = &s.characters[&1];
            assert_eq!((c.progression.xp, c.progression.quest_points), (initial_xp + 750000, 1500));
            assert_eq!(c.quests.warder.0[&quest.id].completed, completed);
            assert_eq!(c.quests.updates().unwrap().len(), 1);
            s.warder_conversation("local", 1, giver, completed + quest.repeat - 1).unwrap();
            assert_eq!(s.characters[&1].quests.warder.0[&quest.id].completed, completed);
            s.warder_conversation("local", 1, giver, completed + quest.repeat).unwrap();
            assert_eq!(s.characters[&1].quests.warder.0[&quest.id].completed, 0);
            assert_eq!(s.characters[&1].progression.quest_points, 1500);
        }
        std::fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn exploration_conversations_replay_partial_and_completed_quests_exactly_once() {
        let dir=std::env::temp_dir().join(format!("ac2-exploration-{}-{}",std::process::id(),crate::recovered_quests::now()));
        let q=&crate::exploration_quests::catalog().quests[0];
        let referral=&crate::exploration_quests::catalog().referral;
        {
            let mut s=Store::open(&dir).unwrap();s.create("local","Ringway Hero").unwrap();
            assert!(s.exploration_conversation("other",1,q.npc,100).is_err());
            assert!(s.exploration_conversation("local",1,q.objectives[0],100).is_err());
            s.exploration_conversation("local",1,q.npc,100).unwrap();
            s.exploration_conversation("local",1,q.objectives[0],101).unwrap();
            let length=s.file.metadata().unwrap().len();
            s.exploration_conversation("local",1,q.objectives[0],102).unwrap();
            assert_eq!(s.file.metadata().unwrap().len(),length,"duplicate touch must not append");
        }
        {
            let mut s=Store::open(&dir).unwrap();
            assert_eq!(s.characters[&1].quests.exploration.0[&q.id].phase,2);
            assert_eq!(s.characters[&1].progression.quest_points,0);
            for (i,entity) in q.objectives[1..].iter().enumerate() {
                s.exploration_conversation("local",1,*entity,103+i as u64).unwrap();
            }
            s.exploration_conversation("local",1,q.npc,105).unwrap();
            assert_eq!(s.characters[&1].progression.xp,80);
            assert_eq!(s.characters[&1].progression.quest_points,100);
            s.exploration_conversation("local",1,q.npc,106).unwrap();
            s.exploration_conversation("local",1,referral.npc,107).unwrap();
            let book=&s.characters[&1].quests;
            assert!(book.list_graph().unwrap().len()<48000);
            assert_eq!(book.updates().unwrap().len(),2);
            // Compact an ordinary progression run following a quest and replay it.
            for _ in 0..5 { s.progression_command("local",1,crate::progression::Command::AwardGold(1)).unwrap(); }
            s.compact_journal().unwrap();
        }
        {
            let mut s=Store::open(&dir).unwrap();
            assert_eq!(s.characters[&1].progression.xp,80);
            assert_eq!(s.characters[&1].progression.quest_points,100);
            assert_eq!(s.characters[&1].quests.exploration.0[&referral.id].completed,107);
            s.exploration_conversation("local",1,q.npc,108).unwrap();
            assert_eq!(s.characters[&1].progression.xp,80);
            assert_eq!(s.characters[&1].progression.quest_points,100);
        }
        std::fs::remove_file(dir.join("characters.journal")).unwrap();
        std::fs::remove_file(dir.join("characters.journal.previous")).unwrap();
        std::fs::remove_file(dir.join("positions.snapshot")).unwrap();
        std::fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn hunting_quests_replay_acceptance_kills_return_and_exactly_one_reward() {
        use crate::progression::Command::AwardXp;
        let dir=std::env::temp_dir().join(format!("ac2-hunting-{}-{}",std::process::id(),crate::recovered_quests::now()));
        let q=crate::hunting_quests::profile(0x4000fc07).unwrap();
        let time=crate::recovered_quests::now();let initial=447949214;
        {
            let mut s=Store::open(&dir).unwrap();s.create("local","Foundry Hunter").unwrap();
            s.progression_command("local",1,AwardXp(initial)).unwrap();
            assert!(s.hunting_conversation("other",1,q.npc,time).is_err());
            s.hunting_conversation("local",1,q.npc,time).unwrap();
            for _ in 0..4 {s.encounter_reward("local",1,q.targets[0],&[AwardXp(1)]).unwrap();}
        }
        {
            let mut s=Store::open(&dir).unwrap();assert_eq!(s.characters[&1].quests.hunting.0[&q.id].count,4);
            for _ in 4..10 {s.encounter_reward("local",1,q.targets[0],&[AwardXp(1)]).unwrap();}
            assert_eq!(s.characters[&1].progression.xp,initial+10,"kill must not pay quest XP");
            s.hunting_conversation("local",1,q.npc,time+1).unwrap();
            assert_eq!(s.characters[&1].progression.xp,initial+10+q.xp_cap);
            let length=s.file.metadata().unwrap().len();
            s.hunting_conversation("local",1,q.npc,time+2).unwrap();
            assert_eq!(s.file.metadata().unwrap().len(),length);
        }
        {
            let s=Store::open(&dir).unwrap();assert_eq!(s.characters[&1].progression.xp,initial+10+q.xp_cap);
            assert_eq!(s.characters[&1].quests.hunting.0[&q.id].completed,time+1);
        }
        std::fs::remove_file(dir.join("characters.journal")).unwrap();std::fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn knorr_key_hunts_grant_once_with_xp_and_survive_replay() {
        use crate::{hunting_quests, inventory::{Command as I, BAG_SLOTS}, progression::Command::AwardXp, shelter};
        let dir=std::env::temp_dir().join(format!("ac2-knorr-keys-{}",std::process::id()));
        let keys=hunting_quests::profiles().iter().filter(|q|q.key.is_some()).collect::<Vec<_>>();
        assert_eq!(keys.len(),2);
        let time=crate::recovered_quests::now();
        let final_state={
            let mut s=Store::open(&dir).unwrap();s.create("local","Geledite Hunter").unwrap();
            s.progression_command("local",1,AwardXp(447949214)).unwrap();
            for q in &keys {
                s.hunting_conversation("local",1,q.npc,time).unwrap();
                for _ in 0..q.goal {s.encounter_reward("local",1,q.targets[0],&[AwardXp(1)]).unwrap();}
            }
            for q in &keys {
                let before=s.characters[&1].progression.clone();
                s.hunting_conversation("local",1,q.npc,time+1).unwrap();
                s.hunting_conversation("local",1,q.npc,time+2).unwrap();
                assert_eq!(s.characters[&1].progression.xp,before.xp+q.reward(&before));
                let definition=q.key.as_ref().unwrap().item;
                assert_eq!(s.characters[&1].inventory.items().filter(|i|i.definition==definition).count(),1);
            }
            s.create("local","Full Hunter").unwrap();
            s.progression_command("local",2,AwardXp(447949214)).unwrap();
            let q=keys[0];
            s.hunting_conversation("local",2,q.npc,time).unwrap();
            for _ in 0..q.goal {s.encounter_reward("local",2,q.targets[0],&[AwardXp(1)]).unwrap();}
            for slot in 0..BAG_SLOTS {
                let id=s.next_item_id().unwrap();
                s.inventory_command("local",2,I::Grant(shelter::item(0x47001583,id,slot).unwrap())).unwrap();
            }
            let before=s.characters[&2].clone();let length=s.file.metadata().unwrap().len();
            assert!(s.hunting_conversation("local",2,q.npc,time+1).is_err());
            assert_eq!(s.characters[&2],before);assert_eq!(s.file.metadata().unwrap().len(),length);
            s.compact_journal().unwrap();
            s.characters[&1].clone()
        };
        {let s=Store::open(&dir).unwrap();assert_eq!(s.characters[&1],final_state);}
        std::fs::remove_file(dir.join("characters.journal")).unwrap();std::fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn boglurk_turn_in_awards_points_once_and_survives_reload() {
        use crate::progression::Command::AwardXp;
        let dir = std::env::temp_dir().join(format!("ac2-boglurk-{}", std::process::id()));
        let quest = crate::hunting_quests::profile(0x4000fc32).unwrap();
        let now = crate::recovered_quests::now();
        let starting_xp = 447949214;
        {
            let mut store = Store::open(&dir).unwrap();
            store.create("local", "Hakata Hunter").unwrap();
            store.progression_command("local", 1, AwardXp(starting_xp)).unwrap();
            store.hunting_conversation("local", 1, quest.npc, now).unwrap();
            for _ in 0..10 {
                store.encounter_reward("local", 1, quest.targets[0], &[AwardXp(1)]).unwrap();
            }
            assert_eq!(store.characters[&1].progression.quest_points, 0);
        }
        {
            let mut store = Store::open(&dir).unwrap();
            store.hunting_conversation("local", 1, quest.npc, now + 1).unwrap();
            store.hunting_conversation("local", 1, quest.npc, now + 2).unwrap();
            store.compact_journal().unwrap();
        }
        {
            let store = Store::open(&dir).unwrap();
            let result = &store.characters[&1].progression;
            assert_eq!(result.quest_points, 1_750);
            assert_eq!(result.xp, starting_xp + 10 + 340_000);
        }
        std::fs::remove_file(dir.join("characters.journal")).unwrap();
        std::fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn hunting_v1_journal_replays_before_new_gregor_chain_and_fixed_rewards() {
        use crate::progression::Command::AwardXp;
        let dir=std::env::temp_dir().join(format!("ac2-hunting-versions-{}-{}",std::process::id(),crate::recovered_quests::now()));
        let q=crate::hunting_quests::profile(0x4000fc04).unwrap();
        let initial=113726634933;let repeat=120+q.repeat_seconds;
        {
            let mut s=Store::open(&dir).unwrap();s.create("local","Version Hunter").unwrap();
            s.progression_command("local",1,AwardXp(initial)).unwrap();
            s.append_record(&format!("qh1\t1\t{}\t100",q.npc)).unwrap();
            for n in 0..10 {s.append_record(&format!("qk1\t1\t{:08x}\t{}\tx,1",q.targets[0],101+n)).unwrap();}
            s.append_record(&format!("qh1\t1\t{}\t120",q.npc)).unwrap();
            s.append_record(&format!("qh1\t1\t{}\t{repeat}",q.npc)).unwrap();
        }
        {
            let mut s=Store::open(&dir).unwrap();let c=&s.characters[&1];
            assert_eq!(c.quests.hunting.0.len(),1);
            assert_eq!(c.quests.hunting.0[&q.id].started,repeat);
            assert_eq!(c.progression.xp,initial+10+q.xp_cap);
            let fixed=crate::hunting_quests::profile(0x4000fc23).unwrap();
            let now=crate::recovered_quests::now();
            s.hunting_conversation("local",1,fixed.npc,now).unwrap();
            for _ in 0..10 {s.encounter_reward("local",1,fixed.targets[0],&[AwardXp(1)]).unwrap();}
            s.hunting_conversation("local",1,fixed.npc,now+1).unwrap();
            s.compact_journal().unwrap();
        }
        {
            let s=Store::open(&dir).unwrap();
            assert_eq!(s.characters[&1].progression.xp,initial+20+q.xp_cap+100000);
            assert_eq!(s.characters[&1].quests.hunting.0[&0x4000fc23].rewarded_xp,100000);
        }
        std::fs::remove_file(dir.join("characters.journal")).unwrap();std::fs::remove_dir(dir).unwrap();
    }
    fn record(data: &str) -> Vec<u8> {
        format!("{data}\t{:08x}\n", hash(data.as_bytes())).into_bytes()
    }
    #[test]
    fn windmill_replays_quarry_ownership_and_rewards_after_compaction() {
        use crate::windmill::{Action,MAP,STONE,GEAR,GLYPH};
        let dir=std::env::temp_dir().join(format!("ac2-windmill-{}-{}",std::process::id(),crate::recovered_quests::now()));
        let owned=|s:&Store,d|s.characters[&1].inventory.items().find(|i|i.definition==d).unwrap().id;
        {
            let mut s=Store::open(&dir).unwrap();s.create("local","Windmill Apprentice").unwrap();
            s.windmill_transaction("local",1,Action::Talk,100).unwrap();
            assert!(s.windmill_transaction("stranger",1,Action::Talk,101).is_err());
        }
        {
            let mut s=Store::open(&dir).unwrap();let id=owned(&s,MAP);let t=crate::windmill::content().map_location;
            assert!(s.characters[&1].professions.1.jobs.contains_key(&id));
            s.windmill_transaction("local",1,Action::Locate{item:id,point:[t[0] as u32,(t[1] as f32).to_bits(),(t[2] as f32).to_bits(),(t[3] as f32).to_bits()]},101).unwrap();
            let id=s.next_item_id().unwrap();let slot=s.characters[&1].inventory.free_slot().unwrap();
            s.inventory_command("local",1,crate::inventory::Command::Grant(crate::inventory::Item{id,definition:STONE,visual:0x1f000434,quantity:25,stack_limit:u16::MAX,equipment_mask:0,location:crate::inventory::Location::Bag(slot),roll:None})).unwrap();
            s.windmill_transaction("local",1,Action::Stone(id),102).unwrap();s.compact_journal().unwrap();
        }
        {
            let mut s=Store::open(&dir).unwrap();assert_eq!(s.characters[&1].quests.windmill.phase,3);
            let gear=owned(&s,GEAR);s.windmill_transaction("local",1,Action::Gear(gear),103).unwrap();
            s.windmill_transaction("local",1,Action::Talk,104).unwrap();s.windmill_transaction("local",1,Action::Talk,105).unwrap();
            s.windmill_transaction("local",1,Action::Warder,106).unwrap();s.compact_journal().unwrap();
        }
        {
            let s=Store::open(&dir).unwrap();let c=&s.characters[&1];
            assert_eq!(c.progression.xp,200);assert_eq!(c.quests.windmill.warder_completed,106);
            assert_eq!(c.inventory.items().filter(|i|i.definition==GLYPH).count(),1);
            assert_eq!(c.inventory.items().find(|i|i.definition==STONE).unwrap().quantity,24);
        }
        std::fs::remove_file(dir.join("characters.journal")).unwrap();std::fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn nature_collection_recipe_cost_and_cooldown_survive_compaction() {
        use crate::nature_gathering::{Action, BLOSSOM, GARLAND, SCENTED, COST};
        let dir=std::env::temp_dir().join(format!("ac2-nature-{}-{}",std::process::id(),crate::recovered_quests::now()));
        let node=0x8140000000000001;
        let owned=|s:&Store,d|s.characters[&1].inventory.items().find(|i|i.definition==d).unwrap().id;
        {
            let mut s=Store::open(&dir).unwrap();s.create("local","Nature Apprentice").unwrap();
            assert!(s.nature_transaction("other",1,Action::Harvest(node),100).is_err());
            s.nature_transaction("local",1,Action::Harvest(node),100).unwrap();
            s.nature_transaction("local",1,Action::Harvest(node+1),101).unwrap();
            s.nature_transaction("local",1,Action::Learn(owned(&s,BLOSSOM)),102).unwrap();
            s.compact_journal().unwrap();
        }
        {
            let mut s=Store::open(&dir).unwrap();
            assert!(s.characters[&1].quests.nature.learned);
            assert!(s.nature_transaction("local",1,Action::Harvest(node),103).is_err());
            let anchor=owned(&s,BLOSSOM);
            let before=s.characters[&1].inventory.clone();let length=s.file.metadata().unwrap().len();
            assert!(s.nature_transaction("local",1,Action::Craft([anchor;7]),104).is_err());
            assert_eq!(s.characters[&1].inventory,before);
            assert_eq!(s.file.metadata().unwrap().len(),length);
            s.progression_command("local",1,crate::progression::Command::AwardGold(COST+1)).unwrap();
            s.nature_transaction("local",1,Action::Craft([anchor;7]),104).unwrap();
            assert_eq!(s.characters[&1].progression.gold,1);
            assert!(s.nature_transaction("local",1,Action::Craft([anchor;7]),105).is_err());
            s.nature_transaction("local",1,Action::Offer(owned(&s,GARLAND)),105).unwrap();
            s.compact_journal().unwrap();
        }
        {
            let mut s=Store::open(&dir).unwrap();
            assert!(s.characters[&1].quests.nature.learned);
            assert_eq!(s.characters[&1].progression.gold,1);
            assert_eq!(s.characters[&1].progression.xp,0);
            assert_eq!(s.characters[&1].inventory.items().next().unwrap().definition,SCENTED);
            assert!(s.nature_transaction("local",1,Action::Harvest(node),399).is_err());
            s.nature_transaction("local",1,Action::Harvest(node),400).unwrap();
        }
        std::fs::remove_file(dir.join("characters.journal")).unwrap();std::fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn arwic_lesson_replays_inventory_entitlement_and_reward_atomically() {
        use crate::arwic_mines::{Action,IRON,SWORD,GLYPH,GUIDE};
        let dir=std::env::temp_dir().join(format!("ac2-arwic-{}-{}",std::process::id(),crate::recovered_quests::now()));
        {
            let mut s=Store::open(&dir).unwrap();s.create("local","Mine Apprentice").unwrap();
            assert!(s.arwic_transaction("other",1,Action::Talk,100).is_err());
            s.arwic_transaction("local",1,Action::Talk,100).unwrap();
            s.arwic_transaction("local",1,Action::Hammer,101).unwrap();
        }
        {
            let mut s=Store::open(&dir).unwrap();assert_eq!(s.characters[&1].quests.arwic.phase,2);
            s.arwic_transaction("local",1,Action::Talk,102).unwrap();
            let iron=crate::arwic_mines::owned(&s.characters[&1].inventory,IRON).unwrap();
            s.arwic_transaction("local",1,Action::Craft(iron),103).unwrap();
            assert!(s.characters[&1].inventory.items().any(|i|i.definition==SWORD));
            s.compact_journal().unwrap();
        }
        {
            let mut s=Store::open(&dir).unwrap();assert_eq!(s.characters[&1].quests.arwic.phase,4);
            assert!(s.characters[&1].quests.arwic.learned);
            s.arwic_transaction("local",1,Action::Talk,104).unwrap();
            let length=s.file.metadata().unwrap().len();
            s.arwic_transaction("local",1,Action::Talk,105).unwrap();
            assert_eq!(s.file.metadata().unwrap().len(),length);s.compact_journal().unwrap();
        }
        {
            let s=Store::open(&dir).unwrap();let c=&s.characters[&1];
            assert_eq!(c.progression.xp,150);assert_eq!(c.quests.arwic.completed,104);
            assert!(c.quests.arwic.learned);
            for definition in [GLYPH,GUIDE] {assert_eq!(c.inventory.items().filter(|i|i.definition==definition).count(),1);}
            assert!(!c.inventory.items().any(|i|i.definition==SWORD));
        }
        std::fs::remove_file(dir.join("characters.journal")).unwrap();std::fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn avatar_lifetimes_survive_restart_and_reject_invalid_journal_transitions() {
        let dir = std::env::temp_dir().join(format!("ac2-instances-{}", std::process::id()));
        {
            let mut store = Store::open(&dir).unwrap();
            store.create("local", "Hero").unwrap();
            assert!(store.begin_instance("other", 1).is_err());
            assert_eq!(store.begin_instance("local", 1).unwrap(), 1);
            store.delete("local", 1).unwrap();
            assert!(store.begin_instance("local", 1).is_err());
            store.restore("local", 1).unwrap();
        }
        {
            let mut store = Store::open(&dir).unwrap();
            assert_eq!(store.begin_instance("local", 1).unwrap(), 2);
            let length = store.file.metadata().unwrap().len();
            store.characters.get_mut(&1).unwrap().instance = u16::MAX;
            assert!(store.begin_instance("local", 1).is_err());
            assert_eq!(store.file.metadata().unwrap().len(), length);
        }
        let prefix = record("v1\t1\tlocal\tHero");
        for suffix in ["e1\t1\t0", "e1\t1\t2", "e1\t2\t1", "e1\t1\t65536"] {
            let mut bytes = prefix.clone();
            bytes.extend(record(suffix));
            assert!(decode_journal(&bytes).is_err());
        }
        let mut bytes = prefix;
        bytes.extend(record("e1\t1\t1"));
        let valid = bytes.clone();
        bytes.extend(record("e1\t1\t1"));
        assert!(decode_journal(&bytes).is_err());
        bytes = valid;
        bytes.extend(record("d1\t1"));
        bytes.extend(record("e1\t1\t2"));
        assert!(decode_journal(&bytes).is_err());
        fs::remove_file(dir.join("characters.journal")).unwrap();
        fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn mixed_versions_retain_exact_native_appearance_after_restart() {
        let dir = std::env::temp_dir().join(
            format!(
                "ac2-native-journal-{}-{:?}",
                std::process::id(),
                std::thread::current().id()
            )
            .replace(['(', ')'], ""),
        );
        let original = request("Native Hero");
        {
            let mut store = Store::open(&dir).unwrap();
            assert_eq!(store.create("local", "Legacy Hero").unwrap().creation, None);
            let created = store.create_native("local", &original).unwrap();
            assert_eq!(created.creation.as_ref(), Some(&original));
            assert!(
                store
                    .create_native("other", &request("native hero"))
                    .is_err()
            );
            for i in 0..3 {
                store
                    .create_native("local", &request(&format!("Extra {i}")))
                    .unwrap();
            }
            assert!(store.create_native("local", &request("Sixth")).is_err());
        }
        {
            let store = Store::open(&dir).unwrap();
            assert_eq!(store.characters.len(), 5);
            assert_eq!(store.characters[&1].creation, None);
            assert_eq!(store.characters[&2].creation.as_ref(), Some(&original));
        }
        fs::remove_file(dir.join("characters.journal")).unwrap();
        fs::remove_dir(dir).unwrap();
    }
    #[test]
    fn native_records_reject_torn_corrupt_and_semantically_malformed_data() {
        let req = request("Hero");
        let encoded = encode_creation(&req).unwrap();
        let good = record(&format!("v2\t1\tlocal\tHero\t{encoded}"));
        assert_eq!(
            decode_journal(&good).unwrap()[&1].creation.as_ref(),
            Some(&req)
        );
        for n in 1..good.len() {
            assert!(
                decode_journal(&good[..n]).is_err(),
                "accepted torn prefix {n}"
            );
        }
        for n in 0..encoded.len() {
            assert!(
                decode_journal(&record(&format!("v2\t1\tlocal\tHero\t{}", &encoded[..n]))).is_err()
            );
        }
        let mut malformed = encoded.clone();
        malformed.replace_range(48..56, "ffffffff");
        assert!(decode_journal(&record(&format!("v2\t1\tlocal\tHero\t{malformed}"))).is_err());
        let mut invalid = req.clone();
        invalid.physical_values.push(invalid.physical_values[0]);
        assert!(encode_creation(&invalid).is_err());
        invalid.physical_values = vec![(1, f32::NAN.to_bits())];
        assert!(encode_creation(&invalid).is_err());
        invalid.physical_values = (0..257).map(|i| (i, 0)).collect();
        assert!(encode_creation(&invalid).is_err());
        let mut mixed = record("v1\t1\tlocal\tHero");
        mixed.extend(record("v1\t2\tother\thero"));
        assert!(decode_journal(&mixed).is_err());
        let mut too_many = Vec::new();
        for i in 1..=6 {
            too_many.extend(record(&format!("v1\t{i}\tlocal\tHero {i}")));
        }
        assert!(decode_journal(&too_many).is_err());
    }
    #[test]
    fn persistence_exclusive_writer_and_corruption() {
        let dir = std::env::temp_dir().join(
            format!(
                "ac2-journal-{}-{:?}",
                std::process::id(),
                std::thread::current().id()
            )
            .replace(['(', ')'], ""),
        );
        fs::create_dir_all(&dir).unwrap();
        {
            let mut s = Store::open(&dir).unwrap();
            assert!(Store::open(&dir).is_err());
            assert_eq!(s.create("local", "Explorer").unwrap().id, 1);
            assert!(s.create("local", "explorer").is_err());
        }
        {
            let s = Store::open(&dir).unwrap();
            assert_eq!(s.characters[&1].name, "Explorer");
        }
        let path = dir.join("characters.journal");
        let mut bytes = fs::read(&path).unwrap();
        bytes[0] ^= 1;
        assert!(decode_journal(&bytes).is_err());
        assert!(decode_journal(&bytes[..bytes.len() - 1]).is_err());
        fs::remove_file(path).unwrap();
        fs::remove_dir(dir).unwrap();
    }
}
