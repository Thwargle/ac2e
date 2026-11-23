//! Retail VitaeCostLevelMappingTable (WLib PID 0x333A, slot 5).
//! The level 38 and 150 death captures both send the level cost scaled by
//! the current vitae percentage as the XP needed for the next point.
use super::Progression;

const LEVEL_COSTS: [u64; 150] = [
    1, 1, 1, 1, 1, 2, 3, 4,
    5, 6, 7, 8, 11, 15, 22, 30,
    51, 96, 163, 258, 393, 582, 843, 1199,
    1681, 2118, 2663, 3341, 4182, 5225, 6512, 8097,
    10045, 12432, 15351, 18909, 23236, 28484, 34832, 42492,
    51710, 62773, 76015, 91824, 110645, 132992, 159455, 190705,
    227508, 270732, 353491, 460382, 598078, 774986, 1001665, 1291342,
    1806653, 2107911, 2424747, 2782011, 3183661, 3633852, 4136920, 4697359,
    5319782, 6008896, 6769446, 7606173, 8523752, 9526734, 10619467, 11806027,
    13090137, 14475073, 15963585, 17557796, 19259116, 21068144, 22984581, 25007143,
    27133481, 29360108, 31682338, 34094233, 36588574, 39156833, 41789178, 44598483,
    47596646, 50796363, 54211183, 57855566, 61744946, 65895792, 70325682, 75053374,
    80098888, 85483590, 91230283, 97363302, 103908616, 110893944, 118348866, 126304950,
    134795887, 143857633, 153528562, 163849625, 174864529, 186619918, 199165570, 212554614,
    226843744, 242093472, 258368373, 275737365, 294274001, 314056774, 335169458, 357701457,
    381748185, 407411472, 434799990, 464029721, 495224441, 528516247, 564046118, 601964510,
    642431992, 685619928, 731711204, 780900997, 833397609, 889423342, 949215444, 1013027111,
    1081128562, 1153808181, 1231373737, 1314153692, 1402498587, 1496782528, 1597404774, 1704791420,
    1819397207, 1941707449, 2072240081, 2211547861, 2360220703, 2518888180,
];

impl Progression {
    fn vitae_cost(level: u16, vitae: u8) -> u64 {
        ((LEVEL_COSTS[usize::from(level - 1)] * u64::from(vitae) + 50) / 100).max(1)
    }

    pub fn vitae_remaining(&self) -> u64 {
        if self.vitae == 100 { return 0; }
        Self::vitae_cost(self.level().number, self.vitae).saturating_sub(self.vitae_xp)
    }

    pub(super) fn recover_vitae(&mut self, mut award: u64, level: u16) {
        while self.vitae < 100 {
            let remaining = Self::vitae_cost(level, self.vitae) - self.vitae_xp;
            if award < remaining {
                self.vitae_xp += award;
                break;
            }
            award -= remaining;
            self.vitae_xp = 0;
            self.vitae += 1;
        }
    }

    fn scaled_vital(&self, raw: u32) -> u32 {
        // DeathSystem (slot 8) uses single precision and truncates to an int.
        ((raw as f32 * (f32::from(self.vitae) * 0.01)) as u32).max(1)
    }
    pub fn max_health(&self) -> u32 {
        self.scaled_vital(self.level().health)
            + if self.obelisk_courage { crate::exploration_quests::COURAGE_HEALTH } else { 0 }
    }
    pub fn max_vigor(&self) -> u32 {
        self.scaled_vital(self.level().vigor)
            + if self.renewed_focus { crate::windmill::RENEWED_FOCUS_VIGOR } else { 0 }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::progression::{levels, Command};

    #[test]
    fn retail_death_thresholds_and_owner_wire_match_both_captures() {
        for (level, remaining) in [(38, 27_060u64), (150, 2_392_943_771)] {
            let dead = Progression { xp: levels::LEVELS[level - 1].xp, health: 0, ..Progression::default() };
            let resurrected = dead.apply(&Command::Resurrect).unwrap();
            assert_eq!(resurrected.vitae_remaining(), remaining);
            let changes = resurrected.private_changes(&dead);
            assert!(changes.contains(&[
                0x1006du32.to_le_bytes(), 0x11eu32.to_le_bytes(), 95f32.to_le_bytes(),
            ].concat()));
            assert!(changes.contains(&[
                &0x100d4u32.to_le_bytes()[..], &0x13cu32.to_le_bytes(), &remaining.to_le_bytes(),
            ].concat()));
        }
    }

    #[test]
    fn vitae_recovers_one_point_at_a_time_and_never_spends_earned_experience() {
        let dead = Progression { xp: levels::LEVELS[37].xp, health: 0, ..Progression::default() };
        let revived = dead.apply(&Command::Resurrect).unwrap();
        let partial = revived.apply(&Command::AwardXp(27_059)).unwrap();
        assert_eq!((partial.vitae, partial.vitae_xp, partial.vitae_remaining()), (95, 27_059, 1));
        let recovered = partial.apply(&Command::AwardXp(1)).unwrap();
        assert_eq!((recovered.vitae, recovered.vitae_xp, recovered.available_xp), (96, 0, 27_060));
        assert_eq!(recovered.health, recovered.max_health());
        let full = recovered.apply(&Command::AwardXp(1_000_000)).unwrap();
        assert!(full.valid());
        assert_eq!((full.vitae, full.vitae_xp, full.vitae_remaining()), (100, 0, 0));
    }

    #[test]
    fn repeated_deaths_respect_floor_and_healing_respects_reduced_vitals() {
        let mut state = Progression::default();
        for _ in 0..9 {
            state = state.apply(&Command::Damage(1000)).unwrap().apply(&Command::Resurrect).unwrap();
        }
        assert_eq!(state.vitae, 65);
        assert_eq!(state.health, 65);
        assert_eq!(state.apply(&Command::Heal(100)).unwrap().health, state.max_health());
        assert_eq!(state.apply(&Command::RecoverVigor(100)).unwrap().vigor, state.max_vigor());
    }
}
