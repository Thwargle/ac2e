//! Coalesce only uninterrupted progression runs. Other transactions are replay
//! barriers, preserving training, inventory, binding and lifetime ordering.
use super::{hash, invalid};
use crate::progression::Progression;
use std::{collections::BTreeMap, io};

#[derive(Default)]
pub(super) struct Run {
    states: BTreeMap<u64, Progression>,
    starting_points: BTreeMap<u64, u32>,
    raw: Vec<String>,
}
impl Run {
    pub(super) fn record(&mut self, id: u64, starting_points: u32, state: &Progression, line: &str) {
        self.starting_points.entry(id).or_insert(starting_points);
        self.states.insert(id, state.clone());
        self.raw.push(line.to_owned());
    }
    pub(super) fn flush(&mut self, output: &mut Vec<String>) {
        if self.raw.is_empty() {
            return;
        }
        let checkpoints: Vec<_> = self
            .states
            .iter()
            .map(|(id, state)| {
                let mut data = format!(
                    "g3\t{id}\t{},{},{},{},{},{},{}",
                    state.xp, state.available_xp, state.gold, state.health, state.vigor, state.vitae, state.vitae_xp
                );
                // Quest records are replay barriers. Preserve awards recovered from
                // earlier quests when replaying this ordinary progression checkpoint.
                let gained = state.quest_points - self.starting_points[id];
                data.push_str(&format!(",{gained},{},{}", u8::from(state.obelisk_courage), u8::from(state.renewed_focus)));
                format!("{data}\t{:08x}", hash(data.as_bytes()))
            })
            .collect();
        let old: usize = self.raw.iter().map(|line| line.len() + 1).sum();
        let new: usize = checkpoints.iter().map(|line| line.len() + 1).sum();
        if new < old {
            output.extend(checkpoints);
        } else {
            output.append(&mut self.raw);
        }
        self.states.clear();
        self.starting_points.clear();
        self.raw.clear();
    }
}
pub(super) fn progression(text: &str, previous: &Progression) -> io::Result<Progression> {
    if text.len() > 128 {
        return Err(invalid("progression checkpoint size"));
    }
    let f: Vec<_> = text.split(',').collect();
    if !matches!(f.len(), 5 | 7 | 8 | 9 | 10) {
        return Err(invalid("progression checkpoint shape"));
    }
    let next = Progression {
        renewed_focus: match f.get(9).copied() {
            Some("1") => true,
            Some("0") | None => false,
            _ => return Err(invalid("checkpoint renewed focus")),
        },
        obelisk_courage: match f.get(8).copied() {
            Some("1") => true,
            Some("0") | None => false,
            _ => return Err(invalid("checkpoint obelisk courage")),
        },
        quest_points: previous.quest_points.checked_add(f.get(7).unwrap_or(&"0").parse()
            .map_err(|_| invalid("checkpoint quest points"))?).ok_or_else(|| invalid("checkpoint quest point overflow"))?,
        vitae: f.get(5).unwrap_or(&"100").parse().map_err(|_| invalid("checkpoint vitae"))?,
        vitae_xp: f.get(6).unwrap_or(&"0").parse().map_err(|_| invalid("checkpoint vitae XP"))?,
        xp: f[0].parse().map_err(|_| invalid("checkpoint XP"))?,
        available_xp: f[1]
            .parse()
            .map_err(|_| invalid("checkpoint available XP"))?,
        gold: f[2].parse().map_err(|_| invalid("checkpoint gold"))?,
        health: f[3].parse().map_err(|_| invalid("checkpoint health"))?,
        vigor: f[4].parse().map_err(|_| invalid("checkpoint vigor"))?,
    };
    if !next.valid()
        || next.xp < previous.xp
        || next.available_xp.saturating_sub(previous.available_xp) > next.xp - previous.xp
    {
        return Err(invalid("invalid progression checkpoint transition"));
    }
    Ok(next)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn progression_checkpoints_preserve_earlier_quest_awards_across_policy_updates() {
        let before = Progression { quest_points: 15, ..Default::default() };
        for historical in ["0,0,0,100,100", "0,0,0,100,100,100,0", "0,0,0,100,100,100,0,0,0"] {
            assert_eq!(progression(historical, &before).unwrap(), before);
        }
        let after = Progression { quest_points: 45, renewed_focus: true, vigor: 140, ..before.clone() };
        let mut run = Run::default();
        for _ in 0..10 { run.record(1, before.quest_points, &after, "old ordinary progression record"); }
        let mut lines = Vec::new();
        run.flush(&mut lines);
        assert_eq!(lines.len(), 1);
        let checkpoint = lines[0].split('\t').nth(2).unwrap();
        assert_eq!(progression(checkpoint, &before).unwrap(), after);
        let revised = Progression { quest_points: 20, ..before };
        assert_eq!(progression(checkpoint, &revised).unwrap().quest_points, 50);
    }
}
