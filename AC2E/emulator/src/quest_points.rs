//! Retail quest rating awards with a documented quest identity and amount.
use crate::progression::Command;
use std::{collections::BTreeMap, sync::LazyLock};

#[derive(serde::Deserialize)]
struct Reward {
    points: u32,
}

static REWARDS: LazyLock<BTreeMap<u32, u32>> = LazyLock::new(|| {
    let records: BTreeMap<String, Reward> =
        serde_json::from_str(include_str!("../content/quest-point-rewards-v1.json"))
            .expect("quest point reward policy");
    records.into_iter().map(|(key, reward)| {
        let id = u32::from_str_radix(&key, 16).expect("hex quest identity");
        assert!(id >> 24 == 0x40 && reward.points > 0);
        (id, reward.points)
    }).collect()
});

pub(crate) fn amount(id: u32) -> Option<u32> {
    REWARDS.get(&id).copied()
}

pub(crate) fn completion_rewards(id: u32, xp: u64) -> Vec<Command> {
    (xp > 0).then_some(Command::AwardXp(xp)).into_iter().chain(award(id)).collect()
}

pub(crate) fn apply(progression: &crate::progression::Progression, rewards: &[Command])
    -> std::io::Result<crate::progression::Progression>
{
    rewards.iter().try_fold(progression.clone(), |state, command| {
        state.apply(command).map_err(std::io::Error::other)
    })
}

pub(crate) fn award(id: u32) -> Option<Command> {
    amount(id).map(Command::AwardQuestPoints)
}
