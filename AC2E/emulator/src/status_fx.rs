//! Ownership of visual effects attached to lasting skill components. The DAT
//! component supplies its FX key and duration; particle estimates are not buffs.
use std::collections::{BTreeMap, BTreeSet};

pub const LIMIT: usize = 512;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Effect {
    pub fx: u32,
    pub duration_ms: u64,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action { Play(u64, u32), Stop(u64, u32) }
#[derive(Clone, Default)]
pub struct Runtime {
    // source, target, skill, FX -> expiration. Several effects/skills may own
    // the same visible FX key; stopping one must not erase another's visuals.
    active: BTreeMap<(u64, u64, u32, u32), u64>,
}
impl Runtime {
    pub fn len(&self) -> usize { self.active.len() }
    /// Snapshot only effects that still have a live owner at this clock. Several
    /// skills may own one visual; PhysicsDesc carries that visual exactly once.
    pub fn snapshot(&self, target:u64, now:u64) -> Vec<u32> {
        self.active.iter().filter_map(|(&(_,to,_,fx),&until)|
            (to==target && until>now).then_some(fx)).collect::<BTreeSet<_>>().into_iter().collect()
    }
    pub fn apply_batch(&mut self, source:u64, skill:u32, effects:&[(u64,Effect)], now:u64)
        -> std::io::Result<Vec<Action>> {
        let mut recipients=BTreeMap::<u64,Vec<Effect>>::new();
        for &(target,effect) in effects {recipients.entry(target).or_default().push(effect);}
        let mut next=self.clone();
        let mut actions=Vec::new();
        for (target,effects) in recipients {actions.extend(next.apply(source,target,skill,&effects,now)?);}
        *self=next;
        Ok(actions)
    }
    fn visible(&self) -> BTreeSet<(u64,u32)> {
        self.active.keys().map(|&(_, target, _, fx)| (target, fx)).collect()
    }
    pub fn apply(&mut self, source:u64, target:u64, skill:u32, effects:&[Effect], now:u64)
        -> std::io::Result<Vec<Action>> {
        let mut next=self.active.clone();
        next.retain(|&(who, to, which, _),_| (who,to,which)!=(source,target,skill));
        for effect in effects {
            if !matches!(effect.fx>>24,0x40|0x41|0x81) || source==0 || target==0 || skill==0 {
                return Err(std::io::Error::other("invalid skill visual"));
            }
            if effect.duration_ms>0 {
                let until=now.checked_add(effect.duration_ms).ok_or_else(||std::io::Error::other("skill visual clock overflow"))?;
                next.entry((source,target,skill,effect.fx)).and_modify(|value|*value=(*value).max(until)).or_insert(until);
            }
        }
        if next.len()>LIMIT {return Err(std::io::Error::other("skill visual capacity"));}
        let previous=self.visible();self.active=next;let visible=self.visible();
        let mut result:Vec<_>=previous.difference(&visible).map(|&(to,fx)|Action::Stop(to,fx)).collect();
        let mut started=BTreeSet::new();
        for effect in effects {
            if started.insert(effect.fx) { result.push(Action::Play(target,effect.fx)); }
        }
        Ok(result)
    }
    pub fn expire(&mut self, now:u64, mut valid:impl FnMut(u64,u64)->bool)->Vec<Action> {
        let previous=self.visible();
        self.active.retain(|&(source,target,_,_),until|*until>now && valid(source,target));
        let visible=self.visible();
        previous.difference(&visible).map(|&(target,fx)|Action::Stop(target,fx)).collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    const A:u32=0x41000031;
    const B:u32=0x41000032;
    #[test]
    fn independent_durations_refresh_and_shared_owners() {
        let mut fx=Runtime::default();
        let a=Effect{fx:A,duration_ms:1000};let b=Effect{fx:B,duration_ms:2000};
        assert_eq!(fx.apply(1,2,3,&[a,b],0).unwrap(),vec![Action::Play(2,A),Action::Play(2,B)]);
        fx.apply(4,2,5,&[Effect{duration_ms:4000,..a}],0).unwrap();
        assert!(fx.expire(1000,|_,_|true).is_empty());
        assert_eq!(fx.expire(2000,|_,_|true),vec![Action::Stop(2,B)]);
        fx.apply(4,2,5,&[a],3000).unwrap();
        assert!(fx.expire(3999,|_,_|true).is_empty());
        assert_eq!(fx.expire(4000,|_,_|true),vec![Action::Stop(2,A)]);
        assert!(fx.expire(4001,|_,_|true).is_empty());
        fx.apply(1,2,3,&[a],5000).unwrap();
        assert_eq!(fx.apply(1,2,3,&[b],5100).unwrap(),vec![Action::Stop(2,A),Action::Play(2,B)]);
        assert_eq!(fx.expire(5101,|_,target|target!=2),vec![Action::Stop(2,B)]);
    }
    #[test]
    fn instant_effects_do_not_leave_ownership_and_failed_refresh_is_atomic() {
        let mut fx=Runtime::default();
        assert_eq!(fx.apply(1,2,3,&[Effect{fx:A,duration_ms:0}],0).unwrap(),vec![Action::Play(2,A)]);
        assert_eq!(fx.len(),0);
        for skill in 1..=LIMIT as u32 {fx.apply(1,2,skill,&[Effect{fx:A,duration_ms:1000}],0).unwrap();}
        assert!(fx.apply(1,2,9999,&[Effect{fx:B,duration_ms:1000}],0).is_err());
        assert!(fx.apply(1,2,1,&[Effect{fx:B,duration_ms:1000}],u64::MAX).is_err());
        assert_eq!(fx.len(),LIMIT);
        assert_eq!(fx.expire(1000,|_,_|true),vec![Action::Stop(2,A)]);
    }
    #[test]
    fn snapshots_expire_without_waiting_for_tick_and_batch_failures_do_not_leak() {
        let mut fx=Runtime::default();
        let a=Effect{fx:A,duration_ms:1000};
        fx.apply_batch(1,3,&[(2,a),(4,a)],10).unwrap();
        fx.apply(5,2,6,&[a],20).unwrap();
        assert_eq!(fx.snapshot(2,1010),vec![A]);
        assert!(fx.snapshot(4,1010).is_empty());
        assert!(fx.snapshot(2,1020).is_empty());
        let invalid=Effect{fx:1,duration_ms:1000};
        assert!(fx.apply_batch(1,3,&[(2,Effect{fx:B,..a}),(4,invalid)],30).is_err());
        assert_eq!(fx.snapshot(2,30),vec![A]);
        assert_eq!(fx.snapshot(4,30),vec![A]);
    }
}
