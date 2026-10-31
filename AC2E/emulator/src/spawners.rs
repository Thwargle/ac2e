//! Server-owned population slots. A captured creature is evidence for one point,
//! not for a retail generator's radius, child mix, density or regeneration time.
//! Each point owns at most one child lifetime (including its lootable corpse).
//! Fixed services and scripted actors never enter this controller.
use std::{cmp::Reverse, collections::{BTreeMap, BinaryHeap}, io};

#[derive(Clone, Copy, Debug)]
pub struct Policy {
    pub initial_delay_ms: u64,
    pub regeneration_ms: u64,
}
impl Policy {
    pub fn valid(self) -> bool {
        self.initial_delay_ms <= 86_400_000 && (1_000..=86_400_000).contains(&self.regeneration_ms)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action { Spawn(u64), Remove(u64) }

#[derive(Clone, Copy, Debug)]
enum State {
    Live,
    Waiting { due: u64 },
    Spawning,
    Corpse { remove_at: u64, refill_at: u64 },
    Removing { refill_at: u64 },
}
impl State {
    fn deadline(self) -> Option<u64> {
        match self {
            Self::Waiting { due } => Some(due),
            Self::Corpse { remove_at, .. } => Some(remove_at),
            _ => None,
        }
    }
}
struct Point { policy: Policy, state: State }
struct Group { capacity:usize, members:Vec<u64> }

#[derive(Default)]
pub struct Spawners {
    points: BTreeMap<u64, Point>,
    // Poll only pending work. Thousands of occupied generators need no timer scan.
    due: BinaryHeap<Reverse<(u64, u64)>>,
    groups:BTreeMap<u64,Group>,
    membership:BTreeMap<u64,u64>,
}
impl Spawners {
    /// Adopt the initial content lifetimes. Server bootstrap withdraws those
    /// monsters before exposing the scene, then schedules their initial spawn.
    pub fn new(points: impl Iterator<Item = (u64, Policy)>) -> io::Result<Self> {
        let mut result = Self::default();
        for (id, policy) in points {
            if id == 0 || id == u64::MAX || !policy.valid() || result.points.len() >= 16384
                || result.points.insert(id, Point { policy, state: State::Live }).is_some()
            {
                return Err(io::Error::new(io::ErrorKind::InvalidData, "invalid or duplicate point spawner"));
            }
        }
        Ok(result)
    }
    pub fn len(&self) -> usize { self.points.len() }
    pub fn group_count(&self) -> usize { self.groups.len() }
    pub fn is_empty(&self) -> bool { self.points.is_empty() }
    pub fn owns(&self, id: u64) -> bool { self.points.contains_key(&id) }
    /// Explicit verified child roster only: capacity never creates coordinates
    /// or duplicates an actor. Several simultaneous children are supported.
    pub fn group(&mut self,id:u64,capacity:usize,members:Vec<u64>)->io::Result<()> {
        let unique=members.iter().copied().collect::<std::collections::BTreeSet<_>>();
        if id==0 || id==u64::MAX || self.groups.contains_key(&id) || capacity==0 || capacity>members.len() || members.len()>64
            || unique.len()!=members.len() || members.iter().any(|id|!self.points.contains_key(id)||self.membership.contains_key(id)) {
            return Err(io::Error::other("invalid spawner group or child roster"));
        }
        for &member in &members{self.membership.insert(member,id);}
        self.groups.insert(id,Group{capacity,members});Ok(())
    }
    fn has_capacity(&self,id:u64)->bool {
        let Some(group)=self.membership.get(&id).and_then(|id|self.groups.get(id)) else{return true};
        group.members.iter().filter(|id|!matches!(self.points[id].state,State::Waiting{..})).count()<group.capacity
    }
    pub fn live(&self) -> usize {
        self.points.values().filter(|p| matches!(p.state, State::Live)).count()
    }
    /// Only at startup, after removing the imported monster lifetimes.
    pub fn initialize(&mut self, now: u64) {
        self.due.clear();
        for (&id, point) in &mut self.points {
            let due = now.saturating_add(point.policy.initial_delay_ms);
            point.state = State::Waiting { due };
            self.due.push(Reverse((due, id)));
        }
    }
    pub fn died(&mut self, id: u64, now: u64, corpse_ms: u64) -> bool {
        let Some(point) = self.points.get_mut(&id) else { return false };
        if !matches!(point.state, State::Live) { return false; }
        let remove_at = now.saturating_add(corpse_ms);
        let refill_at = now.saturating_add(point.policy.regeneration_ms);
        point.state = State::Corpse { remove_at, refill_at };
        self.due.push(Reverse((remove_at, id)));
        true
    }
    /// Acknowledge actual world removal, including a child removed by a script.
    /// Visibility loss, movement, player logout and looting are NOT removal.
    pub fn removed(&mut self, id: u64, now: u64) {
        let Some(point) = self.points.get_mut(&id) else { return };
        let due = match point.state {
            State::Corpse { refill_at, .. } | State::Removing { refill_at } => refill_at.max(now),
            State::Live => now.saturating_add(point.policy.regeneration_ms),
            _ => return, // Repeated notifications must not postpone regeneration.
        };
        point.state = State::Waiting { due };
        self.due.push(Reverse((due, id)));
    }
    pub fn spawned(&mut self, id: u64) -> bool {
        let Some(point) = self.points.get_mut(&id) else { return false };
        if !matches!(point.state, State::Spawning) { return false; }
        point.state = State::Live;
        true
    }
    pub fn poll(&mut self, now: u64) -> Vec<Action> {
        let mut actions = Vec::new();
        while self.due.peek().is_some_and(|Reverse((due, _))| *due <= now) {
            let Reverse((due, id)) = self.due.pop().unwrap();
            let capacity=self.has_capacity(id);
            let point = self.points.get_mut(&id).unwrap();
            if point.state.deadline() != Some(due) { continue; } // Superseded corpse deadline.
            match point.state {
                State::Waiting { .. } => {
                    if !capacity {
                        let due=now.saturating_add(1000);point.state=State::Waiting{due};self.due.push(Reverse((due,id)));continue;
                    }
                    point.state = State::Spawning;
                    actions.push(Action::Spawn(id));
                }
                State::Corpse { refill_at, .. } => {
                    point.state = State::Removing { refill_at };
                    actions.push(Action::Remove(id));
                }
                _ => unreachable!(),
            }
        }
        actions
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn population() -> Spawners {
        Spawners::new([(7, Policy { initial_delay_ms: 500, regeneration_ms: 3000 }),
            (8, Policy { initial_delay_ms: 0, regeneration_ms: 5000 })].into_iter()).unwrap()
    }
    #[test]
    fn initial_population_is_delayed_and_capacity_waits_for_world_acknowledgement() {
        let mut p=population();p.initialize(100);
        assert_eq!(p.poll(100),vec![Action::Spawn(8)]);
        assert_eq!(p.live(),0);assert!(p.poll(599).is_empty());
        assert_eq!(p.poll(600),vec![Action::Spawn(7)]);
        assert!(p.poll(100000).is_empty()); // An unacknowledged spawn cannot duplicate.
        assert!(p.spawned(7));assert!(!p.spawned(7));assert!(p.spawned(8));
        assert_eq!(p.live(),2);assert!(p.poll(200000).is_empty());
        assert!(!p.owns(9));assert!(!p.died(9,200000,0));
    }
    #[test]
    fn corpse_and_regeneration_deadlines_are_independent_and_never_overlap_lifetimes() {
        let mut p=population();assert!(p.died(7,1000,60000));
        assert!(!p.died(7,2000,60000)); // Duplicate kill cannot extend timers.
        assert!(p.poll(60999).is_empty());
        assert_eq!(p.poll(61000),vec![Action::Remove(7)]);
        assert!(p.poll(90000).is_empty()); // Must actually remove the corpse first.
        p.removed(7,90000);assert_eq!(p.poll(90000),vec![Action::Spawn(7)]);
        assert!(p.spawned(7));assert_eq!(p.live(),2);
        assert!(p.died(7,91000,500));assert_eq!(p.poll(91500),vec![Action::Remove(7)]);
        p.removed(7,91500);assert!(p.poll(93999).is_empty());
        assert_eq!(p.poll(94000),vec![Action::Spawn(7)]);
    }
    #[test]
    fn early_removal_does_not_leave_a_stale_corpse_event_or_duplicate_child() {
        let mut p=population();p.died(7,1000,60000);p.removed(7,2000);p.removed(7,2500);
        assert_eq!(p.poll(4000),vec![Action::Spawn(7)]);assert!(p.spawned(7));
        assert!(p.poll(61000).is_empty());assert_eq!(p.live(),2);
        p.removed(7,62000);p.removed(7,63000);
        assert_eq!(p.poll(65000),vec![Action::Spawn(7)]);assert!(p.spawned(7));
    }
    #[test]
    fn restart_reinitializes_each_slot_once_and_rejects_invalid_owners() {
        let mut p=population();p.died(7,1000,60000);p.initialize(2000);
        assert_eq!(p.poll(2000),vec![Action::Spawn(8)]);p.spawned(8);
        assert_eq!(p.poll(2500),vec![Action::Spawn(7)]);p.spawned(7);
        assert!(p.poll(61000).is_empty());
        let policy=Policy{initial_delay_ms:0,regeneration_ms:3000};
        assert!(Spawners::new([(7,policy),(7,policy)].into_iter()).is_err());
        assert!(Spawners::new([(0,policy)].into_iter()).is_err());
        assert!(!Policy{initial_delay_ms:0,regeneration_ms:0}.valid());
    }
    #[test]
    fn explicit_groups_fill_multiple_children_without_exceeding_capacity() {
        let policy=Policy{initial_delay_ms:0,regeneration_ms:1000};
        let mut p=Spawners::new([7,8,9].into_iter().map(|id|(id,policy))).unwrap();
        p.group(1,2,vec![7,8,9]).unwrap();p.initialize(0);
        assert_eq!(p.poll(0),vec![Action::Spawn(7),Action::Spawn(8)]);
        assert!(p.poll(5000).is_empty(),"unacknowledged children occupy capacity");
        p.spawned(7);p.spawned(8);assert_eq!(p.live(),2);
        p.died(7,5000,1000);assert_eq!(p.poll(6000),vec![Action::Remove(7)]);
        p.removed(7,6000);assert_eq!(p.poll(6000),vec![Action::Spawn(7)]);p.spawned(7);
        assert!(p.poll(10000).is_empty());assert_eq!(p.live(),2);
        assert!(p.group(2,3,vec![7,8]).is_err());assert!(p.group(2,1,vec![10]).is_err());
        assert!(p.group(2,1,vec![7,7]).is_err());
    }
}
