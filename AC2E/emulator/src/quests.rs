//! Durable authored development quest state and native build-7148 journal data.
//!
//! Native layout: gmQuestInfo::StreamPack 0060af00 and PList<gmQuestInfo>::Pack
//! 0060b500. Status semantics: gmQuestInfo::InqExtraText 0060aca0. The training
//! objective/rewards are explicit development policy, not original quest rules.
//! Later captures carry an extra maxed-out word absent from 7148; it is omitted.
use crate::{dat::Dat, script, world, world::WorldError};
use std::io;

pub const UPDATE: u32 = 0x82130024;
// Immutable authored profile. Changing rewards requires a new journal revision.
pub const TRAINING_QUEST: u32 = 0x4000ff01;
pub const TARGET_ENTITY: u32 = 0x470007d1;
pub const GOAL: u8 = 3;
pub const REWARD_XP: u64 = 120;
pub const REWARD_GOLD: u32 = 10;
// New authored profile, separate from the immutable original training reward.
pub const SUPPLIES_QUEST: u32 = 0x4000ff02;
pub const SUPPLIES_XP: u64 = 80;
pub const SUPPLIES_GOLD: u32 = 6;
pub const POTION_QUEST: u32 = 0x4000ff03;
pub const JOURNEY_QUEST: u32 = 0x4000ff04;
pub fn rewards(command: &Command) -> Vec<crate::progression::Command> {
    if matches!(command, Command::Claim) {
        vec![
            crate::progression::Command::AwardXp(REWARD_XP),
            crate::progression::Command::AwardGold(REWARD_GOLD),
        ]
    } else if matches!(command, Command::ClaimSupplies) {
        vec![crate::progression::Command::AwardXp(SUPPLIES_XP), crate::progression::Command::AwardGold(SUPPLIES_GOLD)]
    } else if matches!(command, Command::ClaimPotion | Command::ClaimJourney) {
        vec![crate::progression::Command::AwardXp(80), crate::progression::Command::AwardGold(if matches!(command, Command::ClaimPotion) { 8 } else { 10 })]
    } else {
        Vec::new()
    }
}
const ICON: u32 = 0x4100034b;
const UNDERWAY: u32 = 0x10000000;
const FAILED: u32 = 0x20000000;
const COMPLETED: u32 = 0x30000000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Progress {
    pub killed: u8,
    pub claimed: bool,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Book {
    pub recovered: crate::recovered_quests::Book,
    pub exploration: crate::exploration_quests::Book,
    pub hunting: crate::hunting_quests::Book,
    pub arwic: crate::arwic_mines::State,
    pub windmill: crate::windmill::State,
    pub esper: crate::esper_camp::State,
    pub warder: crate::warder_referrals::Book,
    pub notebook: crate::warder_notebook::State,
    pub nature: crate::nature_gathering::State,
    training: Option<Progress>,
    supplies: Option<bool>,
    potion: u8, // 0 absent, 1 accepted, 2 crafted, 3 rewarded
    journey: u8, // 0 absent, 1 accepted, 2 bound, 3 travelled, 4 rewarded
    pub(crate) quick_exit: u8, // 0 unread, 1 warning read, 2 skull confirmed
    shelter: u8, // ordered Shelter phases; see shelter::guidance
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Command {
    Accept,
    Kill(u32),
    Claim,
    AcceptSupplies,
    ClaimSupplies,
    AcceptPotion,
    CraftedPotion,
    ClaimPotion,
    AcceptJourney,
    BoundLifestone,
    Travelled,
    ClaimJourney,
    ReadExitWarning, ConfirmExitSkull, LeaveThroughQuickExit,
    ShelterGuard,
    ShelterLieutenant,
    ShelterSentry,
    ShelterTool, ShelterGlyph, ShelterAdvance, ShelterComplete,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum QuestError {
    InvalidRecord,
    AlreadyAccepted,
    AlreadyCompleted,
    NotAccepted,
    NotReady,
}
impl std::fmt::Display for QuestError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(self.message())
    }
}
impl std::error::Error for QuestError {}
impl QuestError {
    pub fn message(&self) -> &'static str {
        match self {
            Self::AlreadyAccepted => "You already accepted the training quest.",
            Self::AlreadyCompleted => "You already claimed this quest's reward.",
            Self::NotAccepted => "Accept the training quest first.",
            Self::NotReady => "Finish the current training objective before claiming its reward.",
            Self::InvalidRecord => "Invalid development quest state.",
        }
    }
}

impl Command {
    pub fn encode(&self) -> String {
        match self {
            Self::ReadExitWarning => "shelter.warning".into(),
            Self::ConfirmExitSkull => "shelter.skull".into(),
            Self::LeaveThroughQuickExit => "shelter.exit".into(),
            Self::ShelterTool => "shelter.tool".into(),
            Self::ShelterGlyph => "shelter.glyph".into(),
            Self::ShelterAdvance => "shelter.advance".into(),
            Self::ShelterComplete => "shelter.complete".into(),
            Self::ShelterGuard => "shelter.guard".into(),
            Self::ShelterLieutenant => "shelter.lieutenant".into(),
            Self::ShelterSentry => "shelter.sentry".into(),
            Self::Accept => "a".into(),
            Self::Kill(entity) => format!("k,{entity:08x}"),
            Self::Claim => "c".into(),
            Self::AcceptSupplies => "s.a".into(),
            Self::ClaimSupplies => "s.c".into(),
            Self::AcceptPotion => "p.a".into(), Self::CraftedPotion => "p.f".into(), Self::ClaimPotion => "p.c".into(),
            Self::AcceptJourney => "j.a".into(), Self::BoundLifestone => "j.b".into(), Self::Travelled => "j.t".into(), Self::ClaimJourney => "j.c".into(),
        }
    }
    pub fn decode(text: &str) -> Result<Self, QuestError> {
        match text {
            "shelter.warning" => Ok(Self::ReadExitWarning),
            "shelter.skull" => Ok(Self::ConfirmExitSkull),
            "shelter.exit" => Ok(Self::LeaveThroughQuickExit),
            "shelter.tool" => Ok(Self::ShelterTool),
            "shelter.glyph" => Ok(Self::ShelterGlyph),
            "shelter.advance" => Ok(Self::ShelterAdvance),
            "shelter.complete" => Ok(Self::ShelterComplete),
            "shelter.guard" => Ok(Self::ShelterGuard),
            "shelter.lieutenant" => Ok(Self::ShelterLieutenant),
            "shelter.sentry" => Ok(Self::ShelterSentry),
            "a" => Ok(Self::Accept),
            "c" => Ok(Self::Claim),
            "s.a" => Ok(Self::AcceptSupplies),
            "s.c" => Ok(Self::ClaimSupplies),
            "p.a" => Ok(Self::AcceptPotion), "p.f" => Ok(Self::CraftedPotion), "p.c" => Ok(Self::ClaimPotion),
            "j.a" => Ok(Self::AcceptJourney), "j.b" => Ok(Self::BoundLifestone), "j.t" => Ok(Self::Travelled), "j.c" => Ok(Self::ClaimJourney),
            _ => {
                let hex = text.strip_prefix("k,").ok_or(QuestError::InvalidRecord)?;
                if hex.len() != 8 || !hex.bytes().all(|value| value.is_ascii_hexdigit()) {
                    return Err(QuestError::InvalidRecord);
                }
                let entity = u32::from_str_radix(hex, 16).map_err(|_| QuestError::InvalidRecord)?;
                if entity >> 24 != 0x47 {
                    return Err(QuestError::InvalidRecord);
                }
                Ok(Self::Kill(entity))
            }
        }
    }
}

impl Book {
    pub fn shelter_phase(&self) -> u8 { self.shelter }
    pub fn get(&self) -> Option<&Progress> {
        self.training.as_ref()
    }
    /// Shared player guidance for mentor speech, quest commands and the journal.
    /// The quest state and immutable rewards remain the persisted authored profile.
    pub fn guidance(&self) -> String {
        if self.journey != 0 { return self.journey_guidance(); }
        if self.potion != 0 { return self.potion_guidance(); }
        if self.supplies.is_some() { return self.supplies_guidance(); }
        match self.training {
            None => "Speak to a training mentor to begin Training Grounds, or use @quest start.".into(),
            Some(progress) if progress.claimed => format!(
                "Training complete. You received {REWARD_XP} experience and {REWARD_GOLD} gold. Speak to a training mentor again for the Field Supplies lesson."
            ),
            Some(progress) if progress.killed == GOAL => format!(
                "Training creatures defeated: {GOAL}/{GOAL}. Leave combat and speak to a training mentor for {REWARD_XP} experience and {REWARD_GOLD} gold, or use @quest claim."
            ),
            Some(progress) => format!(
                "Training creatures defeated: {}/{GOAL}. Equip a starter weapon from your bag and defeat Idol Spawn. Return to a training mentor after {GOAL} kills.", progress.killed
            ),
        }
    }
    pub fn ready(&self) -> bool {
        self.training
            .is_some_and(|state| state.killed == GOAL && !state.claimed)
    }
    pub fn apply(&self, command: &Command) -> Result<Self, QuestError> {
        if self
            .training
            .is_some_and(|state| state.killed > GOAL || state.claimed && state.killed != GOAL)
        {
            return Err(QuestError::InvalidRecord);
        }
        let mut next = self.clone();
        match command {
            Command::ReadExitWarning if self.quick_exit == 0 => next.quick_exit = 1,
            Command::ConfirmExitSkull if self.quick_exit == 1 => next.quick_exit = 2,
            Command::LeaveThroughQuickExit if self.quick_exit == 2 && self.shelter < 7 => next.shelter = 7,
            Command::ReadExitWarning | Command::ConfirmExitSkull | Command::LeaveThroughQuickExit => return Err(QuestError::NotReady),
            Command::ShelterTool | Command::ShelterGlyph | Command::ShelterAdvance | Command::ShelterComplete => {
                let previous=match command {Command::ShelterTool=>3,Command::ShelterGlyph=>4,Command::ShelterAdvance=>5,_=>6};
                if self.shelter!=previous {return Err(QuestError::NotReady);}
                next.shelter=previous+1;
            }
            Command::ShelterGuard | Command::ShelterLieutenant | Command::ShelterSentry => {
                let previous = match command {Command::ShelterGuard=>0,Command::ShelterLieutenant=>1,_=>2};
                if self.shelter != previous {return Err(QuestError::NotReady);}
                next.shelter=previous+1;
            }
            Command::AcceptPotion => {
                if self.supplies != Some(true) { return Err(QuestError::NotReady); }
                if self.potion != 0 { return Err(QuestError::AlreadyAccepted); }
                next.potion = 1;
            }
            Command::CraftedPotion => {
                if self.potion != 1 { return Err(QuestError::NotReady); } next.potion = 2;
            }
            Command::ClaimPotion => {
                if self.potion != 2 { return Err(if self.potion == 3 { QuestError::AlreadyCompleted } else { QuestError::NotReady }); } next.potion = 3;
            }
            Command::AcceptJourney => {
                if self.potion != 3 { return Err(QuestError::NotReady); }
                if self.journey != 0 { return Err(QuestError::AlreadyAccepted); } next.journey = 1;
            }
            Command::BoundLifestone => {
                if self.journey != 1 { return Err(QuestError::NotReady); } next.journey = 2;
            }
            Command::Travelled => {
                if self.journey != 2 { return Err(QuestError::NotReady); } next.journey = 3;
            }
            Command::ClaimJourney => {
                if self.journey != 3 { return Err(if self.journey == 4 { QuestError::AlreadyCompleted } else { QuestError::NotReady }); } next.journey = 4;
            }
            Command::AcceptSupplies => {
                if !self.training.is_some_and(|state| state.claimed) { return Err(QuestError::NotReady); }
                if self.supplies.is_some() { return Err(QuestError::AlreadyAccepted); }
                next.supplies = Some(false);
            }
            Command::ClaimSupplies => {
                match self.supplies {
                    None => return Err(QuestError::NotAccepted),
                    Some(true) => return Err(QuestError::AlreadyCompleted),
                    Some(false) => next.supplies = Some(true),
                }
            }
            Command::Accept => {
                if let Some(state) = self.training {
                    return Err(if state.claimed {
                        QuestError::AlreadyCompleted
                    } else {
                        QuestError::AlreadyAccepted
                    });
                }
                next.training = Some(Progress {
                    killed: 0,
                    claimed: false,
                });
            }
            Command::Kill(entity) => {
                next = self.record_kill(*entity);
            }
            Command::Claim => {
                let state = next.training.as_mut().ok_or(QuestError::NotAccepted)?;
                if state.claimed {
                    return Err(QuestError::AlreadyCompleted);
                }
                if state.killed != GOAL {
                    return Err(QuestError::NotReady);
                }
                state.claimed = true;
            }
        }
        Ok(next)
    }
    /// A qualifying kill is credited once by the authoritative encounter owner.
    /// The caller commits this candidate in the same transaction as kill rewards.
    pub fn record_kill(&self, entity: u32) -> Self {
        let mut next = self.clone();
        if entity == TARGET_ENTITY {
            if let Some(state) = &mut next.training {
                if !state.claimed && state.killed < GOAL {
                    state.killed += 1;
                }
            }
        }
        next
    }
    /// One live journal change. Never call for an unaccepted quest.
    pub fn update(&self) -> Result<Vec<u8>, WorldError> {
        let state = self.training.ok_or(WorldError::InvalidValue)?;
        let mut arguments = script::integer(0);
        arguments.extend(native_graph(0xa0, &info_payload(state, true)?));
        arguments.extend(script::integer(status(state)));
        script::private_call(UPDATE, &arguments)
    }
    /// The gmQuestInfoList argument used by UI_Load for saved journal restoration.
    pub fn list_graph(&self) -> Result<Vec<u8>, WorldError> {
        let now = crate::recovered_quests::now();
        let mut payload = Vec::new();
        word(&mut payload, u32::from(self.training.is_some()) + u32::from(self.supplies.is_some()) + u32::from(self.potion != 0) + u32::from(self.journey != 0) + u32::from(self.shelter != 0) + self.recovered.0.len() as u32 + self.exploration.0.len() as u32 + self.hunting.0.len() as u32 + u32::from(self.arwic.phase>0) + self.windmill.entries().len() as u32 + self.esper.entries().len() as u32 + self.warder.0.len() as u32 + self.notebook.entries().len() as u32);
        if let Some(state) = self.training {
            payload.extend(info_payload(state, false)?);
        }
        if let Some(claimed) = self.supplies { payload.extend(supplies_payload(claimed, false)?); }
        if self.potion != 0 { payload.extend(self.lesson_payload(false, false)?); }
        if self.journey != 0 { payload.extend(self.lesson_payload(true, false)?); }
        if self.shelter != 0 { payload.extend(self.shelter_payload(false)?); }
        self.visit_native_entries(now, |entry| {
            payload.extend(entry.payload(now, false)?);
            Ok(())
        })?;
        Ok(native_graph(0xa1, &payload))
    }

    fn visit_native_entries(&self, now: u64, mut visit: impl FnMut(JournalEntry<'_>) -> Result<(), WorldError>) -> Result<(), WorldError> {
        for (&id, state) in &self.recovered.0 { visit(recovered_entry(id, state)?)?; }
        for (&id, state) in &self.exploration.0 { visit(exploration_entry(id, state)?)?; }
        for (&id, state) in &self.hunting.0 { visit(hunting_entry(id, state)?)?; }
        if self.arwic.phase > 0 { visit(arwic_entry(&self.arwic))?; }
        for entry in self.windmill.entries().into_iter().chain(self.esper.entries()).chain(self.notebook.entries()) {
            visit(windmill_entry(&entry))?;
        }
        for (&id, state) in &self.warder.0 { visit(warder_entry(id, state, now)?)?; }
        Ok(())
    }

    /// A native taking rule requires the exact objective and its current journal status.
    pub(crate) fn permits_quest_item(&self, requirement: Option<(u32, u32)>, now: u64) -> Result<bool, WorldError> {
        let Some((id, status)) = requirement else { return Ok(true) };
        let mut permitted = false;
        self.visit_native_entries(now, |entry| {
            if entry.id == id { permitted = entry.status(now) == status; }
            Ok(())
        })?;
        Ok(permitted)
    }
}

pub fn validate_assets(portal: &mut Dat) -> io::Result<()> {
    let icon = portal.read(ICON)?;
    if icon.get(..4) != Some(ICON.to_le_bytes().as_slice()) {
        return Err(io::Error::other(
            "quest icon has an incompatible DAT identity",
        ));
    }
    Ok(())
}

fn status(state: Progress) -> u32 {
    if state.claimed { COMPLETED } else { UNDERWAY }
}
fn word(bytes: &mut Vec<u8>, value: u32) {
    bytes.extend(value.to_le_bytes());
}
fn native_graph(kind: u16, payload: &[u8]) -> Vec<u8> {
    let mut bytes = Vec::new();
    for value in [5, 1, 1, 0x44000100, 0, 0xffff0000 | u32::from(kind)] {
        word(&mut bytes, value);
    }
    bytes.extend(payload);
    word(&mut bytes, 0); // Native object has no interpreted heap fields.
    bytes
}
fn info_payload(state: Progress, play_fx: bool) -> Result<Vec<u8>, WorldError> {
    if state.killed > GOAL || state.claimed && state.killed != GOAL {
        return Err(WorldError::InvalidValue);
    }
    let mut bytes = Vec::new();
    word(&mut bytes, TRAINING_QUEST);
    bytes.extend(world::literal_name("Training Grounds")?);
    bytes.extend(world::literal_name(&format!(
        "Defeat three Idol Spawn training creatures, then speak to a training mentor for {REWARD_XP} experience and {REWARD_GOLD} gold. You may also use @quest claim."
    ))?);
    word(&mut bytes, ICON);
    word(&mut bytes, status(state));
    word(&mut bytes, if state.claimed { 0 } else { 1 });
    let entry = Book { training: Some(state), ..Book::default() }.guidance();
    bytes.extend(world::literal_name(&entry)?);
    word(&mut bytes, 1); // Authored challenge level.
    bytes.extend((-1.0f64).to_le_bytes()); // Unset bestowal timestamp, native Clear sentinel.
    bytes.extend((-1.0f64).to_le_bytes()); // Unset completion timestamp.
    word(&mut bytes, 0); // No expiration.
    bytes.extend(0f64.to_le_bytes());
    bytes.extend(0f64.to_le_bytes());
    word(&mut bytes, u32::from(play_fx));
    Ok(bytes)
}

impl Book {
    pub fn supplies(&self) -> Option<bool> { self.supplies }
    pub fn supplies_guidance(&self) -> String {
        if self.supplies == Some(true) {
            "Field Supplies complete. You received a health potion, 80 experience and 6 gold. Use the crafting panel to combine a Mucor Mushroom and 100 Tallow into a health or vigor potion. Speak to a mentor again to begin Potion Practice.".into()
        } else {
            "Field Supplies: bring one Mucor Mushroom and one full 100-unit Tallow bundle to a training mentor. Buy them from the quartermaster for 2 gold each and keep them in your bag. Your reward is a health potion, 80 experience and 6 gold.".into()
        }
    }
    pub fn updates(&self) -> Result<Vec<Vec<u8>>, WorldError> {
        self.updates_since(&Self::default())
    }
    /// Native UpdateQuestInfo replaces one entry. Resending completed entries
    /// on every kill grows traffic with journal history and can fill the GUN
    /// reliable queue. Login still sends the complete list_graph snapshot.
    pub fn updates_since(&self, previous:&Self) -> Result<Vec<Vec<u8>>, WorldError> {
        let now = crate::recovered_quests::now();
        let mut events = Vec::new();
        for entry in self.windmill.entries().into_iter().chain(self.esper.entries()).chain(self.notebook.entries()) {
            if previous.windmill.entries().contains(&entry)||previous.esper.entries().contains(&entry)||previous.notebook.entries().contains(&entry){continue;}
            events.push(windmill_entry(&entry).update(now)?);
        }
        if self.arwic.phase>0 && self.arwic!=previous.arwic {
            events.push(arwic_entry(&self.arwic).update(now)?);
        }
        for (id,state) in &self.warder.0 {
            if previous.warder.0.get(id)!=Some(state) {
                events.push(warder_entry(*id,state,now)?.update(now)?);
            }
        }
        for (id,state) in &self.exploration.0 {
            if previous.exploration.0.get(id)!=Some(state) {
                events.push(exploration_entry(*id,state)?.update(now)?);
            }
        }
        for (id,state) in &self.hunting.0 {
            if previous.hunting.0.get(id)!=Some(state) {
                events.push(hunting_entry(*id,state)?.update(now)?);
            }
        }
        for (id,state) in &self.recovered.0 {
            if previous.recovered.0.get(id)!=Some(state) {
                events.push(recovered_entry(*id,state)?.update(now)?);
            }
        }
        if self.shelter != 0 && self.shelter!=previous.shelter {
            let mut args=script::integer(0);
            args.extend(native_graph(0xa0,&self.shelter_payload(true)?));
            args.extend(script::integer(UNDERWAY));
            events.push(script::private_call(UPDATE,&args)?);
        }
        if self.training.is_some() && self.training!=previous.training { events.push(self.update()?); }
        if let Some(claimed) = self.supplies.filter(|_|self.supplies!=previous.supplies) {
            let mut args = script::integer(0);
            args.extend(native_graph(0xa0, &supplies_payload(claimed, true)?));
            args.extend(script::integer(if claimed { COMPLETED } else { UNDERWAY }));
            events.push(script::private_call(UPDATE, &args)?);
        }
        for journey in [false, true] {
            if if journey {self.journey==previous.journey} else {self.potion==previous.potion} {continue;}
            if if journey { self.journey != 0 } else { self.potion != 0 } {
                let mut args = script::integer(0);
                args.extend(native_graph(0xa0, &self.lesson_payload(journey, true)?));
                let claimed = if journey { self.journey == 4 } else { self.potion == 3 };
                args.extend(script::integer(if claimed { COMPLETED } else { UNDERWAY }));
                events.push(script::private_call(UPDATE, &args)?);
            }
        }
        Ok(events)
    }
}
fn supplies_payload(claimed: bool, play_fx: bool) -> Result<Vec<u8>, WorldError> {
    let mut bytes = Vec::new();
    word(&mut bytes, SUPPLIES_QUEST);
    bytes.extend(world::literal_name("Field Supplies")?);
    bytes.extend(world::literal_name("Bring a Mucor Mushroom and a full 100-unit Tallow bundle to a training mentor. The mentor keeps these supplies and gives you one health potion, 80 experience and 6 gold.")?);
    word(&mut bytes, ICON);
    word(&mut bytes, if claimed { COMPLETED } else { UNDERWAY });
    word(&mut bytes, u32::from(!claimed));
    bytes.extend(world::literal_name(&Book { supplies: Some(claimed), ..Book::default() }.supplies_guidance())?);
    word(&mut bytes, 1);
    bytes.extend((-1.0f64).to_le_bytes()); bytes.extend((-1.0f64).to_le_bytes());
    word(&mut bytes, 0); bytes.extend(0f64.to_le_bytes()); bytes.extend(0f64.to_le_bytes());
    word(&mut bytes, u32::from(play_fx));
    Ok(bytes)
}
/// Canonical once-only supply exchange. Only owned bag items with the exact
/// supported profiles qualify; bought or otherwise acquired supplies are accepted.
pub fn supplies_items(bag: &crate::inventory::Inventory, first: u64) -> io::Result<Vec<crate::inventory::Command>> {
    use crate::inventory::{Command as ItemCommand, Location};
    let mushroom = bag.items().find(|item| matches!(item.location, Location::Bag(_)) && item.quantity == 1 && item.definition == crate::crafting::MUCOR && item.visual == crate::merchants::MUCOR_VISUAL)
        .ok_or_else(|| io::Error::other("Bring one Mucor Mushroom in your bag."))?;
    let tallow = bag.items().find(|item| matches!(item.location, Location::Bag(_)) && item.quantity == crate::crafting::TALLOW_QUANTITY && item.definition == crate::crafting::TALLOW)
        .ok_or_else(|| io::Error::other("Bring a full 100-unit Tallow bundle in your bag."))?;
    let mut commands = vec![ItemCommand::ConsumeDense { item: mushroom.id }, ItemCommand::ConsumeStackDense { item: tallow.id, quantity: crate::crafting::TALLOW_QUANTITY }];
    let mut candidate = bag.clone();
    for command in &commands { candidate = candidate.apply(command).map_err(io::Error::other)?; }
    let slot = candidate.free_slot().ok_or_else(|| io::Error::other("Make room for the reward potion."))?;
    commands.push(ItemCommand::Grant(crate::item_services::potion(first, slot)));
    Ok(commands)
}
/// Used both before writing and on journal replay. An arbitrary inventory
/// transaction cannot claim the objective or inject a different payout.
pub fn validate_supplies_exchange(book: &Book, bag: &crate::inventory::Inventory, items: &[crate::inventory::Command], changes: &[crate::progression::Command], first: u64) -> io::Result<Book> {
    let next = book.apply(&Command::ClaimSupplies).map_err(io::Error::other)?;
    if crate::player_transaction::encode_items(items) != crate::player_transaction::encode_items(&supplies_items(bag, first)?)
        || crate::player_transaction::encode_changes(changes) != crate::player_transaction::encode_changes(&rewards(&Command::ClaimSupplies)) {
        return Err(io::Error::other("Invalid Field Supplies exchange."));
    }
    Ok(next)
}

impl Book {
    fn shelter_payload(&self, play_fx: bool) -> Result<Vec<u8>, WorldError> {
        let mut bytes=Vec::new(); word(&mut bytes,crate::shelter::JOURNAL);
        bytes.extend(world::literal_name("Rescue the Shelter Hold")?);
        bytes.extend(world::literal_name(crate::shelter::guidance(self.shelter))?);
        word(&mut bytes,ICON); word(&mut bytes,if self.shelter==7 {COMPLETED} else {UNDERWAY}); word(&mut bytes,1);
        bytes.extend(world::literal_name(crate::shelter::guidance(self.shelter))?);
        word(&mut bytes,1); bytes.extend((-1.0f64).to_le_bytes()); bytes.extend((-1.0f64).to_le_bytes());
        word(&mut bytes,0); bytes.extend(0f64.to_le_bytes()); bytes.extend(0f64.to_le_bytes());
        word(&mut bytes,u32::from(play_fx)); Ok(bytes)
    }
    pub fn potion_phase(&self) -> u8 { self.potion }
    pub fn journey_phase(&self) -> u8 { self.journey }
    pub fn potion_guidance(&self) -> String {
        match self.potion {
            2 => "Potion Practice: you crafted a potion. Return to a training mentor for 80 experience and 8 gold. You keep your potion.".into(),
            3 => "Potion Practice complete. You received 80 experience and 8 gold. Speak to a mentor again to begin A Safe Journey.".into(),
            _ => "Potion Practice: buy a Mucor Mushroom and a 100-unit Tallow bundle, then use the crafting panel to create a health or vigor potion. Bought potions do not count. Return to a mentor after crafting for 80 experience and 8 gold.".into(),
        }
    }
    pub fn journey_guidance(&self) -> String {
        match self.journey {
            2 => "A Safe Journey: your lifestone is bound. Leave combat and use a training portal, then speak to a training mentor at your destination.".into(),
            3 => "A Safe Journey: you bound a lifestone and used a training portal. Speak to a training mentor for 80 experience and 10 gold.".into(),
            4 => "Training complete! You finished combat, supplies, potion crafting and safe travel. You received the final 80 experience and 10 gold. The outdoor hub connects all three racial starts; its mentors and quartermasters remain available.".into(),
            _ => "A Safe Journey: use a lifestone to bind it, then use a training portal. Leave combat before both actions. Speak to a mentor at your destination for 80 experience and 10 gold. Rebind now even if you bound a lifestone before accepting this lesson.".into(),
        }
    }
    fn lesson_payload(&self, journey: bool, play_fx: bool) -> Result<Vec<u8>, WorldError> {
        let (id, name, text, claimed) = if journey {
            (JOURNEY_QUEST, "A Safe Journey", self.journey_guidance(), self.journey == 4)
        } else { (POTION_QUEST, "Potion Practice", self.potion_guidance(), self.potion == 3) };
        let mut bytes = Vec::new(); word(&mut bytes, id);
        bytes.extend(world::literal_name(name)?); bytes.extend(world::literal_name(&text)?);
        word(&mut bytes, ICON); word(&mut bytes, if claimed { COMPLETED } else { UNDERWAY });
        word(&mut bytes, u32::from(!claimed)); bytes.extend(world::literal_name(&text)?); word(&mut bytes, 1);
        bytes.extend((-1.0f64).to_le_bytes()); bytes.extend((-1.0f64).to_le_bytes()); word(&mut bytes, 0);
        bytes.extend(0f64.to_le_bytes()); bytes.extend(0f64.to_le_bytes()); word(&mut bytes, u32::from(play_fx));
        Ok(bytes)
    }
}
pub fn is_training_portal(id: u64) -> bool {
    matches!(id, 0x8140000000030002 | 0x8140000000030004 | 0x8140000000030005 | 0x8140000000030022 | 0x8140000000030025 | 0x8140000000030030 | 0x8140000000030031)
}
/// The a3 revision credits only a genuine canonical crafting exchange. Shop
/// purchases, potion grants and reward exchanges cannot advance this lesson.
pub fn validate_practice_craft(book: &Book, bag: &crate::inventory::Inventory, items: &[crate::inventory::Command], changes: &[crate::progression::Command], first: u64) -> io::Result<Book> {
    use crate::inventory::Command as ItemCommand;
    let bad = || io::Error::other("Invalid Potion Practice craft transaction.");
    let [ItemCommand::ConsumeDense { item: mushroom }, ItemCommand::ConsumeStackDense { item: tallow, .. }, .., ItemCommand::Grant(output)] = items else {
        return Err(bad());
    };
    if !changes.is_empty() { return Err(bad()); }
    let recipe = match output.definition {
        crate::crafting::HEALTH_OUTPUT => crate::crafting::HEALTH_RECIPE,
        crate::crafting::VIGOR_OUTPUT => crate::crafting::VIGOR_RECIPE,
        _ => return Err(bad()),
    };
    let request = crate::crafting::ExecuteRequest {
        primary: *mushroom,
        ingredients: std::collections::BTreeMap::from([(1, *mushroom), (2, *tallow)]),
        spinner: 0,
        recipe,
    };
    let expected = request.potion_plan(bag, first)?;
    if !expected.iter().map(ItemCommand::encode).eq(items.iter().map(ItemCommand::encode)) {
        return Err(bad());
    }
    book.apply(&Command::CraftedPotion).map_err(io::Error::other)
}

// gmQuestInfo sends the persisted timestamps and two *remaining durations*.
// Both the login snapshot and a single-phase update use the same clock reading.
struct JournalEntry<'a> {
    id: u32,
    title: &'a str,
    description: &'a str,
    guidance: std::borrow::Cow<'a, str>,
    level: u32,
    phase: u32,
    started: u64,
    completed: u64,
    limit: u64,
    repeat: u64,
}

impl JournalEntry<'_> {
    fn expires(&self) -> bool {
        self.completed == 0 && self.limit > 0
    }

    fn status(&self, now: u64) -> u32 {
        if self.completed > 0 {
            COMPLETED
        } else if self.expires() && now >= self.started.saturating_add(self.limit) {
            FAILED
        } else {
            UNDERWAY
        }
    }

    fn payload(&self, now: u64, fx: bool) -> Result<Vec<u8>, WorldError> {
        let mut bytes = Vec::new();
        word(&mut bytes, self.id);
        bytes.extend(world::literal_name(self.title)?);
        bytes.extend(world::literal_name(self.description)?);
        word(&mut bytes, ICON);
        word(&mut bytes, self.status(now));
        word(&mut bytes, if self.completed > 0 { 0 } else { self.phase });
        if let Some(points) = crate::quest_points::amount(self.id).filter(|_| self.completed > 0) {
            bytes.extend(world::literal_name(&format!("{}\nQuest points earned: {points}.", self.guidance))?);
        } else {
            bytes.extend(world::literal_name(&self.guidance)?);
        }
        word(&mut bytes, self.level);
        bytes.extend((self.started as f64).to_le_bytes());
        bytes.extend((if self.completed > 0 { self.completed as f64 } else { -1.0 }).to_le_bytes());
        word(&mut bytes, u32::from(self.expires()));
        let remaining = if self.expires() { self.started.saturating_add(self.limit).saturating_sub(now) } else { 0 };
        let retry = if self.completed > 0 && self.repeat > 0 { self.completed.saturating_add(self.repeat).saturating_sub(now) } else { 0 };
        bytes.extend((remaining as f64).to_le_bytes());
        bytes.extend((retry as f64).to_le_bytes());
        word(&mut bytes, u32::from(fx));
        Ok(bytes)
    }

    fn update(&self, now: u64) -> Result<Vec<u8>, WorldError> {
        let mut args = script::integer(0);
        args.extend(native_graph(0xa0, &self.payload(now, true)?));
        args.extend(script::integer(self.status(now)));
        script::private_call(UPDATE, &args)
    }
}

fn windmill_entry<'a>(e: &'a (u32,&str,String,u32,u64,u64,u64)) -> JournalEntry<'a> {
    let (id, title, text, phase, started, completed, limit) = e;
    let repeat = match *id {
        crate::windmill::FIND_WARDER => 0,
        crate::esper_camp::QUEST => crate::esper_camp::REPEAT,
        crate::warder_notebook::QUEST | crate::warder_notebook::REFERRAL => crate::warder_notebook::REPEAT,
        _ => crate::windmill::REPEAT,
    };
    JournalEntry {
        id: *id, title, description: text, guidance: text.as_str().into(),
        level: if matches!(*id, crate::warder_notebook::QUEST | crate::warder_notebook::REFERRAL) {10} else {1},
        phase: *phase, started: *started, completed: *completed, limit: *limit, repeat,
    }
}

fn warder_entry(id: u32, s: &crate::warder_referrals::State, now: u64) -> Result<JournalEntry<'static>, WorldError> {
    let q = crate::warder_referrals::profiles().iter().find(|q| q.id == id).ok_or(WorldError::InvalidValue)?;
    Ok(JournalEntry {
        id, title: &q.title, description: &q.direction, guidance: s.guidance(q, now).into(),
        level: q.level.into(), phase: 1, started: s.started, completed: s.completed, limit: q.limit, repeat: q.repeat,
    })
}

fn arwic_entry(s: &crate::arwic_mines::State) -> JournalEntry<'static> {
    let q = crate::arwic_mines::content();
    JournalEntry {
        id: crate::arwic_mines::QUEST, title: &q.title, description: &q.summary, guidance: s.guidance().into(),
        level: 1, phase: s.phase, started: s.started, completed: s.completed, limit: 0, repeat: crate::arwic_mines::REPEAT,
    }
}

fn exploration_entry(id: u32, s: &crate::exploration_quests::State) -> Result<JournalEntry<'static>, WorldError> {
    let q = crate::exploration_quests::profile(id).ok_or(WorldError::InvalidValue)?;
    Ok(JournalEntry {
        id, title: &q.title, description: &q.text, guidance: crate::exploration_quests::Book::guidance(q, s).into(),
        level: q.level.into(), phase: s.phase as u32, started: s.started, completed: s.completed,
        limit: q.limit_seconds, repeat: q.repeat_seconds,
    })
}

fn hunting_entry(id: u32, s: &crate::hunting_quests::State) -> Result<JournalEntry<'static>, WorldError> {
    let q = crate::hunting_quests::profile(id).ok_or(WorldError::InvalidValue)?;
    Ok(JournalEntry {
        id, title: &q.title, description: &q.text, guidance: crate::hunting_quests::Book::guidance(q, s).into(),
        level: q.intended_level.unwrap_or(q.level).into(), phase: u32::from(s.count) + 1, started: s.started, completed: s.completed,
        limit: q.limit_seconds, repeat: q.repeat_seconds,
    })
}

fn recovered_entry(id: u32, s: &crate::recovered_quests::State) -> Result<JournalEntry<'static>, WorldError> {
    let q = crate::recovered_quests::state_profile(id, s).ok_or(WorldError::InvalidValue)?;
    Ok(JournalEntry {
        id, title: &q.title, description: &q.text, guidance: crate::recovered_quests::Book::guidance(q, s).into(),
        level: q.intended_level.unwrap_or(q.level).into(),
        phase: if matches!(id, crate::slavetaker::HEAD_QUEST | crate::slavetaker::SEEK) {u32::from((s.count + 1).min(q.goal))} else {1},
        started: s.started, completed: s.completed,
        limit: q.limit_seconds, repeat: q.repeat_seconds,
    })
}

#[cfg(test)]
mod journal_timer_tests {
    use super::*;
    use ac2_net::protocol::{GmQuestInfo, NativeObject, PackedEntry, PackValue, unpack_values};

    #[test]
    fn quest_item_taking_requires_the_exact_live_objective() {
        let q = crate::exploration_quests::catalog().quests.iter().find(|q| q.title == "Lost Wish Obelisk Search").unwrap();
        let mut book = Book::default();
        assert!(book.permits_quest_item(None, 100).unwrap());
        assert!(!book.permits_quest_item(Some((q.id, UNDERWAY)), 100).unwrap());
        book.exploration.0.insert(q.id, crate::exploration_quests::State { started: 100, completed: 0, phase: 1 });
        assert!(book.permits_quest_item(Some((q.id, UNDERWAY)), 100).unwrap());
        assert!(!book.permits_quest_item(Some((q.id, UNDERWAY)), 100 + q.limit_seconds).unwrap());
        assert!(!book.permits_quest_item(Some((0x80000115, UNDERWAY)), 100).unwrap());
        book.exploration.0.get_mut(&q.id).unwrap().completed = 110;
        assert!(!book.permits_quest_item(Some((q.id, UNDERWAY)), 111).unwrap());
    }

    fn decoded_update(entry: &JournalEntry, now: u64) -> GmQuestInfo {
        let packet = entry.update(now).unwrap();
        let args = unpack_values(&packet[12..]).unwrap();
        let [PackValue::Int(0), PackValue::Reference(graph), PackValue::Int(reason)] = &args[..] else {
            panic!("invalid quest update arguments");
        };
        let PackedEntry::Heap { object, .. } = &graph.entries[0] else { panic!("missing quest object") };
        let Some(NativeObject::GmQuestInfo(info)) = &object.native else { panic!("wrong quest object") };
        assert_eq!(*reason, info.status);
        info.as_ref().clone()
    }

    #[test]
    fn starter_hunt_wasp_bane_displays_its_intended_level_when_accepted_at_three() {
        let q = crate::recovered_quests::use_profile(0x47000f43).unwrap();
        let book = crate::recovered_quests::Book::default().accept(q.id, 3, 100).unwrap();
        let entry = recovered_entry(q.id, &book.0[&q.id]).unwrap();
        assert_eq!(entry.level, 6);
        let info = decoded_update(&entry, 120);
        assert_eq!(info.seconds_remaining, 10780.);
    }

    #[test]
    fn timed_quest_uses_original_deadline_and_completed_retry() {
        let q = crate::exploration_quests::catalog().quests.iter().find(|q| q.title == "Lost Wish Obelisk Search").unwrap();
        let mut state = crate::exploration_quests::State { started: 100, completed: 0, phase: 1 };
        let info = decoded_update(&exploration_entry(q.id, &state).unwrap(), 100);
        assert!(info.expires);
        assert_eq!((info.status, info.seconds_remaining, info.seconds_until_retry), (UNDERWAY, 86400., 0.));
        state.phase = 2;
        for (clock, status, remaining) in [(86499, UNDERWAY, 1.), (86500, FAILED, 0.), (90000, FAILED, 0.)] {
            let info = decoded_update(&exploration_entry(q.id, &state).unwrap(), clock);
            assert_eq!((info.bestowal, info.status, info.seconds_remaining), (100., status, remaining));
        }
        state.completed = 110;
        let info = decoded_update(&exploration_entry(q.id, &state).unwrap(), 115);
        assert!(!info.expires);
        assert_eq!((info.status, info.done, info.seconds_remaining, info.seconds_until_retry), (COMPLETED, 110., 0., 71995.));
    }

    #[test]
    fn slimelord_journal_preserves_the_limit_in_force_at_bestowal() {
        use crate::recovered_quests::{Book as Hunts, HuntRules};
        let id = 0x40000106;
        let guardian = 0x47001688;
        let hunts = Hunts::default();
        let (previous, _) = hunts.kill_version(guardian, 36, 10, HuntRules::PublishedRewards);
        let (current, _) = hunts.kill(guardian, 38, 10);
        for (book, level, duration) in [(previous, 36, 3600_u64), (current, 38, 7200_u64)] {
            let entry = recovered_entry(id, &book.0[&id]).unwrap();
            assert_eq!(entry.level, level);
            let info = decoded_update(&entry, 20);
            assert_eq!((info.status, info.seconds_remaining), (UNDERWAY, (duration - 10) as f64));
            let deadline = decoded_update(&entry, 10 + duration);
            assert_eq!((deadline.status, deadline.seconds_remaining), (FAILED, 0.));
        }
    }

    #[test]
    fn every_hunting_and_recovered_profile_uses_its_gameplay_limit() {
        let h = crate::hunting_quests::State { started: 10, ..Default::default() };
        for q in crate::hunting_quests::profiles() {
            let info = decoded_update(&hunting_entry(q.id, &h).unwrap(), 20);
            assert_eq!((info.expires, info.seconds_remaining), (q.limit_seconds > 0, q.limit_seconds.saturating_sub(10) as f64), "{}", q.title);
            assert_eq!(info.status, UNDERWAY);
        }
        let r = crate::recovered_quests::State { started: 10, ..Default::default() };
        for q in crate::recovered_quests::profiles() {
            let info = decoded_update(&recovered_entry(q.id, &r).unwrap(), 20);
            assert_eq!(info.expires, q.limit_seconds > 0);
            assert_eq!(info.seconds_remaining, q.limit_seconds.saturating_sub(10) as f64, "{}", q.title);
        }
    }
}
