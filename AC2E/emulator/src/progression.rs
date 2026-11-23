//! Server-owned progression and vital transitions. Client fields grant no rewards.
mod levels;
mod vitae;
// AC2E private fields. Historical SQL progression thresholds may differ from the client's DAT.
pub const LEVEL_XP_FLOOR: u32 = 0x7ac2_0001;
pub const LEVEL_XP_CEILING: u32 = 0x7ac2_0002;
pub const QUEST_POINTS: u32 = 0x7ac2_0003;
#[derive(Clone, Copy, Debug)]
pub struct Level {
    pub number: u16,
    pub xp: u64,
    pub health: u32,
    pub vigor: u32,
    pub credits: u16,
    /// ArmorRating, ShieldRating, Focus, MaxHerosCredits and TotalXpMax from
    /// Pyreal_DB_Insert_PlayerLevelData.sql. Combat does not yet apply them.
    pub armor: u32,
    pub shield: u32,
    pub focus: u16,
    pub hero_credits: u32,
    pub xp_cap: u64,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Progression {
    pub obelisk_courage: bool,
    pub renewed_focus: bool,
    pub quest_points: u32,
    pub xp: u64,
    pub available_xp: u64,
    pub vitae: u8,
    pub vitae_xp: u64,
    pub gold: u32,
    pub health: u32,
    pub vigor: u32,
}
impl Default for Progression {
    fn default() -> Self {
        Self {
            obelisk_courage: false,
            renewed_focus: false,
            quest_points: 0,
            xp: 0,
            available_xp: 0,
            vitae: 100,
            vitae_xp: 0,
            gold: 0,
            health: 100,
            vigor: 100,
        }
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Command {
    SetObeliskCourage(bool),
    SetRenewedFocus(bool),
    AwardQuestPoints(u32),
    AwardXp(u64),
    SpendXp(u64),
    AwardGold(u32),
    SpendGold(u32),
    Damage(u32),
    Heal(u32),
    SpendVigor(u32),
    RecoverVigor(u32),
    Resurrect,
    /// Replay of pre-vitae resurrection records.
    LegacyResurrect,
}
#[derive(Debug, Clone)]
pub enum ProgressionError {
    InvalidCommand,
    Overflow,
    InsufficientFunds,
    InsufficientVigor,
    InsufficientExperience,
    Dead,
    Alive,
}
impl std::fmt::Display for ProgressionError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}
impl std::error::Error for ProgressionError {}
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn renewed_focus_changes_vigor_capacity_and_clamps_on_expiry() {
        let base = Progression::default();
        let grant = Command::SetRenewedFocus(true);
        assert_eq!(Command::decode(&grant.encode()).unwrap(), grant);
        let active = base.apply(&grant).unwrap();
        assert_eq!((active.max_vigor(), active.vigor), (140, 100));
        assert_eq!(active.private_changes(&base), [
            [0x10069u32.to_le_bytes(), 0x108u32.to_le_bytes(), 140u32.to_le_bytes()].concat()
        ]);
        let restored = active.apply(&Command::RecoverVigor(40)).unwrap();
        let expired = restored.apply(&Command::SetRenewedFocus(false)).unwrap();
        assert_eq!(expired, base);
    }

    #[test]
    fn obelisk_courage_adds_health_capacity_and_clamps_on_expiry() {
        let base = Progression::default();
        let active = base.apply(&Command::SetObeliskCourage(true)).unwrap();
        assert_eq!((active.max_health(), active.health), (130, 100));
        let healed = active.apply(&Command::Heal(30)).unwrap();
        let expired = healed.apply(&Command::SetObeliskCourage(false)).unwrap();
        assert_eq!(expired, base);
    }

    #[test]
    fn quest_points_are_private_integer_deltas_separate_from_xp() {
        let base = Progression::default();
        let grant = Command::AwardQuestPoints(100);
        assert_eq!(Command::decode(&grant.encode()).unwrap(), grant);
        let next = base.apply(&grant).unwrap();
        let wire = [0x10069u32.to_le_bytes(), QUEST_POINTS.to_le_bytes(), 100u32.to_le_bytes()].concat();
        assert_eq!(next.private_changes(&base), [wire.clone()]);
        assert!(next.private_updates().contains(&wire));
        assert!(next.private_changes(&next).is_empty());
        assert_eq!((next.xp, next.available_xp, next.quest_points), (0, 0, 100));
        assert!(next.apply(&Command::AwardQuestPoints(u32::MAX)).is_err());
    }

    #[test]
    fn level_deltas_report_authoritative_experience_bounds() {
        let first = Progression::default();
        let second = first.apply(&Command::AwardXp(40)).unwrap();
        let partial = second.apply(&Command::AwardXp(1)).unwrap();
        let bounds_on_wire = |updates: Vec<Vec<u8>>| {
            updates.into_iter().filter_map(|wire| {
                if wire.len()!=16 || wire[..4]!=0x100d4u32.to_le_bytes() {return None}
                let key=u32::from_le_bytes(wire[4..8].try_into().unwrap());
                [LEVEL_XP_FLOOR,LEVEL_XP_CEILING].contains(&key)
                    .then(|| (key,u64::from_le_bytes(wire[8..16].try_into().unwrap())))
            }).collect::<Vec<_>>()
        };
        assert_eq!(bounds_on_wire(first.private_updates()), [(LEVEL_XP_FLOOR,0),(LEVEL_XP_CEILING,40)]);
        assert_eq!(bounds_on_wire(second.private_changes(&first)), [(LEVEL_XP_FLOOR,40),(LEVEL_XP_CEILING,96)]);
        assert!(bounds_on_wire(partial.private_changes(&second)).is_empty());
        let cap = first.apply(&Command::AwardXp(146234963497218)).unwrap();
        assert_eq!(cap.level_xp_bounds(), (146234963497218,292469926994436));
    }

    #[test]
    fn player_levels_keep_the_pyreal_vitals_and_add_the_ratings() {
        let first = &levels::LEVELS[0];
        let last = levels::LEVELS.last().unwrap();
        assert_eq!(
            (first.number, first.xp, first.health, first.vigor, first.credits),
            (1, 0, 100, 100, 0)
        );
        assert_eq!((first.armor, first.shield, first.focus, first.hero_credits), (3, 3, 0, 0));
        assert_eq!(first.xp_cap, 39);
        assert_eq!(
            (last.number, last.xp, last.health, last.vigor, last.credits),
            (150, 146234963497218, 4000, 2600, 49)
        );
        assert_eq!(last.armor, 1904);
        assert_eq!(last.hero_credits, 19750);
    }
}

/// Experience required to gain a level. At the level cap use the final playable
/// level band, since the terminal progression row has no successor.
pub(crate) fn quest_level_span(level: u16) -> u64 {
    let index = usize::from(level.saturating_sub(1)).min(levels::LEVELS.len()-2);
    levels::LEVELS[index+1].xp - levels::LEVELS[index].xp
}

impl Progression {
    pub fn level(&self) -> &'static Level {
        &levels::LEVELS[levels::LEVELS
            .partition_point(|level| level.xp <= self.xp)
            .saturating_sub(1)]
    }
    pub fn level_xp_bounds(&self) -> (u64, u64) {
        let level = self.level();
        let ceiling = levels::LEVELS.get(usize::from(level.number))
            .map_or(level.xp * 2, |next| next.xp);
        (level.xp, ceiling)
    }
    pub fn alive(&self) -> bool {
        self.health > 0
    }
    pub fn valid(&self) -> bool {
        self.available_xp <= self.xp
            && self.xp <= i64::MAX as u64
            && self.gold <= i32::MAX as u32
            && (65..=100).contains(&self.vitae)
            && if self.vitae == 100 { self.vitae_xp == 0 } else { self.vitae_remaining() > 0 }
            && self.health <= self.max_health()
            && self.vigor <= self.max_vigor()
    }
    pub fn apply(&self, command: &Command) -> Result<Self, ProgressionError> {
        if !self.valid() {
            return Err(ProgressionError::InvalidCommand);
        }
        let mut next = self.clone();
        match *command {
            Command::SetObeliskCourage(active) => {
                next.obelisk_courage = active;
                next.health = next.health.min(next.max_health());
            }
            Command::SetRenewedFocus(active) => {
                next.renewed_focus = active;
                next.vigor = next.vigor.min(next.max_vigor());
            }
            Command::AwardQuestPoints(0) => return Err(ProgressionError::InvalidCommand),
            Command::AwardQuestPoints(points) => {
                next.quest_points = self.quest_points.checked_add(points).ok_or(ProgressionError::Overflow)?;
            }
            Command::SpendXp(amount) => {
                if amount == 0 {
                    return Err(ProgressionError::InvalidCommand);
                }
                if !self.alive() {
                    return Err(ProgressionError::Dead);
                }
                next.available_xp = self
                    .available_xp
                    .checked_sub(amount)
                    .ok_or(ProgressionError::InsufficientExperience)?;
            }
            Command::AwardXp(amount) => {
                if amount == 0 {
                    return Err(ProgressionError::InvalidCommand);
                }
                next.xp = self
                    .xp
                    .checked_add(amount)
                    .filter(|xp| *xp <= i64::MAX as u64)
                    .ok_or(ProgressionError::Overflow)?;
                next.available_xp = self
                    .available_xp
                    .checked_add(amount)
                    .filter(|xp| *xp <= i64::MAX as u64)
                    .ok_or(ProgressionError::Overflow)?;
                next.recover_vitae(amount, self.level().number);
                // Local profile preserves missing vital amounts on level-up;
                // gaining a level must not resurrect a dead character.
                if self.alive() {
                    next.health = self.health + next.max_health() - self.max_health();
                    next.vigor = self.vigor + next.max_vigor() - self.max_vigor();
                }
            }
            Command::AwardGold(amount) => {
                if amount == 0 {
                    return Err(ProgressionError::InvalidCommand);
                }
                next.gold = self
                    .gold
                    .checked_add(amount)
                    .filter(|gold| *gold <= i32::MAX as u32)
                    .ok_or(ProgressionError::Overflow)?;
            }
            Command::SpendGold(amount) => {
                if amount == 0 {
                    return Err(ProgressionError::InvalidCommand);
                }
                next.gold = self
                    .gold
                    .checked_sub(amount)
                    .ok_or(ProgressionError::InsufficientFunds)?;
            }
            Command::Damage(amount) => {
                if !self.alive() {
                    return Err(ProgressionError::Dead);
                }
                if amount == 0 {
                    return Err(ProgressionError::InvalidCommand);
                }
                next.health = self.health.saturating_sub(amount);
            }
            Command::Heal(amount) => {
                if !self.alive() {
                    return Err(ProgressionError::Dead);
                }
                if amount == 0 {
                    return Err(ProgressionError::InvalidCommand);
                }
                next.health = self.health.saturating_add(amount).min(self.max_health());
            }
            Command::SpendVigor(amount) => {
                if !self.alive() {
                    return Err(ProgressionError::Dead);
                }
                if amount == 0 {
                    return Err(ProgressionError::InvalidCommand);
                }
                next.vigor = self
                    .vigor
                    .checked_sub(amount)
                    .ok_or(ProgressionError::InsufficientVigor)?;
            }
            Command::RecoverVigor(amount) => {
                if !self.alive() {
                    return Err(ProgressionError::Dead);
                }
                if amount == 0 {
                    return Err(ProgressionError::InvalidCommand);
                }
                next.vigor = self.vigor.saturating_add(amount).min(self.max_vigor());
            }
            Command::Resurrect | Command::LegacyResurrect => {
                if self.alive() {
                    return Err(ProgressionError::Alive);
                }
                if matches!(command, Command::Resurrect) {
                    next.vitae = self.vitae.saturating_sub(5).max(65);
                    next.vitae_xp = 0;
                }
                next.health = next.max_health();
                next.vigor = next.max_vigor();
            }
        }
        Ok(next)
    }
    pub fn int_stats(&self) -> [(u32, u32); 9] {
        let level = self.level();
        [
            (0x100, level.health),
            (0x101, level.vigor),
            (0x102, self.health),
            (0x103, self.vigor),
            (0x107, self.max_health()),
            (0x108, self.max_vigor()),
            (0x12e, u32::from(level.number)),
            (0xc1c, self.gold),
            (QUEST_POINTS, self.quest_points),
        ]
    }
    pub fn private_updates(&self) -> Vec<Vec<u8>> {
        self.private_since(None)
    }
    /// Ordered deltas against the preceding committed state, not a guessed cache.
    pub fn private_changes(&self, previous: &Self) -> Vec<Vec<u8>> {
        self.private_since(Some(previous))
    }
    fn private_since(&self, previous: Option<&Self>) -> Vec<Vec<u8>> {
        let old_stats = previous.map(Self::int_stats);
        let mut events: Vec<_> = self
            .int_stats()
            .into_iter()
            .enumerate()
            .filter(|(index, (_, value))| old_stats.is_none_or(|old| old[*index].1 != *value))
            .map(|(_, (key, value))| {
                [
                    0x10069u32.to_le_bytes(),
                    key.to_le_bytes(),
                    value.to_le_bytes(),
                ]
                .concat()
            })
            .collect();
        if previous.is_none_or(|state| state.vitae != self.vitae) {
            events.insert(0, [
                0x1006du32.to_le_bytes(),
                0x11eu32.to_le_bytes(),
                f32::from(self.vitae).to_le_bytes(),
            ].concat());
        }
        let (floor, ceiling) = self.level_xp_bounds();
        let old_bounds = previous.map(Self::level_xp_bounds);
        for (key, value, old) in [
            (LEVEL_XP_FLOOR, floor, old_bounds.map(|(floor, _)| floor)),
            (LEVEL_XP_CEILING, ceiling, old_bounds.map(|(_, ceiling)| ceiling)),
            (0x13bu32, self.vitae_xp, previous.map(|state| state.vitae_xp)),
            (0x13c, self.vitae_remaining(), previous.map(Self::vitae_remaining)),
            (0x12cu32, self.xp, previous.map(|state| state.xp)),
            (
                0x12d,
                self.available_xp,
                previous.map(|state| state.available_xp),
            ),
        ] {
            if old == Some(value) {
                continue;
            }
            let mut event = 0x100d4u32.to_le_bytes().to_vec();
            event.extend(key.to_le_bytes());
            event.extend(value.to_le_bytes());
            events.push(event);
        }
        if previous.is_none_or(|old| old.alive() != self.alive()) {
            events.push(
                [
                    0x1006bu32.to_le_bytes(),
                    0x117u32.to_le_bytes(),
                    u32::from(!self.alive()).to_le_bytes(),
                ]
                .concat(),
            );
        }
        events
    }
}
impl Command {
    pub fn encode(&self) -> String {
        match self {
            Self::SetObeliskCourage(active) => format!("oc,{}", u8::from(*active)),
            Self::SetRenewedFocus(active) => format!("rf,{}", u8::from(*active)),
            Self::AwardQuestPoints(v) => format!("p,{v}"),
            Self::AwardXp(v) => format!("x,{v}"),
            Self::SpendXp(v) => format!("a,{v}"),
            Self::AwardGold(v) => format!("g,{v}"),
            Self::SpendGold(v) => format!("s,{v}"),
            Self::Damage(v) => format!("d,{v}"),
            Self::Heal(v) => format!("h,{v}"),
            Self::SpendVigor(v) => format!("v,{v}"),
            Self::RecoverVigor(v) => format!("r,{v}"),
            Self::Resurrect => "z2".into(),
            Self::LegacyResurrect => "z".into(),
        }
    }
    pub fn decode(text: &str) -> Result<Self, ProgressionError> {
        if text == "z" { return Ok(Self::LegacyResurrect); }
        if text == "z2" {
            return Ok(Self::Resurrect);
        }
        if text.len() > 32 {
            return Err(ProgressionError::InvalidCommand);
        }
        let (kind, value) = text
            .split_once(',')
            .ok_or(ProgressionError::InvalidCommand)?;
        let value = value
            .parse::<u64>()
            .map_err(|_| ProgressionError::InvalidCommand)?;
        if kind == "x" {
            return Ok(Self::AwardXp(value));
        }
        if kind == "a" {
            return Ok(Self::SpendXp(value));
        }
        let value = u32::try_from(value).map_err(|_| ProgressionError::InvalidCommand)?;
        Ok(match kind {
            "oc" if value <= 1 => Self::SetObeliskCourage(value == 1),
            "rf" if value <= 1 => Self::SetRenewedFocus(value == 1),
            "p" => Self::AwardQuestPoints(value),
            "g" => Self::AwardGold(value),
            "s" => Self::SpendGold(value),
            "d" => Self::Damage(value),
            "h" => Self::Heal(value),
            "v" => Self::SpendVigor(value),
            "r" => Self::RecoverVigor(value),
            _ => return Err(ProgressionError::InvalidCommand),
        })
    }
}
