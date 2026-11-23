//! Owner-only WM_Effect notifications for the live retail vitals strip.
use crate::{script, world::WorldError};
use ac2_net::protocol::{entry_flags, pack_values, HeapObject, PackValue, PackedEntry, PackedReference};
use std::collections::BTreeMap;

const ADD: u32 = 0x82080017;
const REMOVE: u32 = 0x82080016;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Lifetime {
    pub start: u64,
    pub until: u64,
}

impl Lifetime {
    pub fn from_quest(start: u64, duration: u64, now: u64) -> Option<Self> {
        if start == 0 || start > now || now - start >= duration { return None; }
        Some(Self {
            start: start.saturating_mul(1000),
            until: start.saturating_add(duration).saturating_mul(1000),
        })
    }
}

// Source (when tracked per caster), skill and effect WState distinguish concurrent grants.
type Key = (u64, u32, u32);
pub type Snapshot = BTreeMap<Key, Lifetime>;

#[derive(Default)]
pub struct Strip {
    sent: BTreeMap<Key, (u32, Lifetime)>,
    next_id: u32,
    next_update: u64,
}

impl Strip {
    pub fn due(&self, now: u64) -> bool {
        now >= self.next_update
    }

    pub fn update(&mut self, active: Snapshot, now: u64, world_time: f64) -> Result<Vec<Vec<u8>>, WorldError> {
        self.next_update = now.saturating_add(100);
        let mut events = Vec::new();
        // Recasts replace their record, including its native sort order.
        for (key, &(id, lifetime)) in &self.sent {
            if active.get(key) != Some(&lifetime) {
                events.push(script::private_call(REMOVE, &script::integer(id))?);
            }
        }
        self.sent.retain(|key, (_, lifetime)| active.get(key) == Some(lifetime));
        for (key, lifetime) in active {
            if self.sent.contains_key(&key) { continue; }
            self.next_id = self.next_id.checked_add(1).ok_or(WorldError::LimitExceeded)?;
            let start = world_time - now.saturating_sub(lifetime.start) as f64 / 1000.0;
            let duration = (lifetime.until - lifetime.start) as f32 / 1000.0;
            let args = pack_values(&[PackValue::Int(self.next_id), PackValue::Reference(record(key.2, start, duration))]);
            events.push(script::private_call(ADD, &args)?);
            self.sent.insert(key, (self.next_id, lifetime));
        }
        Ok(events)
    }
}

fn record(definition: u32, start: f64, duration: f32) -> PackedReference {
    // Defaults and slot types from EffectRecord WState 7000014e in both client DATs.
    let mut slots = vec![0; 26];
    let invalid_time_high = ((-1.0f64).to_bits() >> 32) as u32;
    slots[1] = invalid_time_high;
    slots[11] = u32::MAX;
    slots[13] = invalid_time_high;
    let time = start.to_bits();
    slots[2] = time as u32;
    slots[3] = (time >> 32) as u32;
    slots[6] = duration.to_bits();
    slots[14] = 2;
    slots[20] = 2; // Has a timed duration.
    let tags = vec![195,205,195,205,130,205,3,3,3,2,2,1,195,205,1,130,205,2,130,205,2,2,2,2,2,2];
    PackedReference {
        ids: vec![1, 2],
        entries: vec![
            PackedEntry::Heap {
                flags: entry_flags::KIND_HEAP | entry_flags::LOADED | entry_flags::RECURSE,
                object: HeapObject { pkg_id: 0x14e, pkg_state_id: 0, slots, tags, native: None },
            },
            PackedEntry::Singleton { flags: entry_flags::KIND_HEAP | entry_flags::SINGLETON, did: definition },
        ],
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ac2_net::protocol::unpack_values;

    fn call(bytes: &[u8], method: u32) -> Vec<PackValue> {
        assert_eq!(&bytes[4..8], &method.to_le_bytes());
        unpack_values(&bytes[12..]).unwrap()
    }

    #[test]
    fn live_grants_encode_native_world_time_and_replace_and_remove_by_instance() {
        let mut strip = Strip::default();
        let key = (0, 0x6b, 0x6f000348);
        let active = Snapshot::from([(key, Lifetime { start: 1000, until: 4100 })]);
        let grants = strip.update(active.clone(), 1750, 452.75).unwrap();
        assert_eq!(grants.len(), 1);
        let values = call(&grants[0], ADD);
        let [PackValue::Int(1), PackValue::Reference(graph)] = values.as_slice() else { panic!("invalid grant") };
        let PackedEntry::Heap { object, .. } = &graph.entries[0] else { panic!("missing record") };
        assert_eq!((object.pkg_id, object.slots.len(), object.tags.len()), (0x14e, 26, 26));
        let slots = &object.slots;
        assert_eq!(f64::from_bits(u64::from(slots[2]) | (u64::from(slots[3]) << 32)), 452.0);
        assert_eq!(f32::from_bits(slots[6]), 3.1);
        assert_eq!(slots[20], 2);
        let effect = graph.ids.iter().position(|id| *id == slots[14]).unwrap();
        assert!(matches!(graph.entries[effect], PackedEntry::Singleton { did: 0x6f000348, .. }));
        assert!(strip.update(active, 1900, 452.9).unwrap().is_empty());

        let refreshed = Snapshot::from([(key, Lifetime { start: 2200, until: 5300 })]);
        let replacement = strip.update(refreshed, 2200, 453.2).unwrap();
        assert_eq!(replacement.len(), 2);
        assert!(matches!(call(&replacement[0], REMOVE).as_slice(), [PackValue::Int(1)]));
        assert!(matches!(call(&replacement[1], ADD).as_slice(), [PackValue::Int(2), PackValue::Reference(_)]));
        let removal = strip.update(Snapshot::new(), 5300, 456.3).unwrap();
        assert!(matches!(call(&removal[0], REMOVE).as_slice(), [PackValue::Int(2)]));
    }
}
