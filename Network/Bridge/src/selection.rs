// SPDX-License-Identifier: AGPL-3.0-only
//! Icefire's patched CPlayer::Select updates general and combat selection independently.
use ac2_net::protocol::*;

pub(crate) struct Selection {
    native: bool,
    last: Option<u64>,
}
impl Selection {
    pub fn new(native: bool) -> Self { Self { native, last: None } }
    pub fn reset(&mut self) { self.last=None; }
    pub fn combat_target(&self) -> u64 {if self.native {self.last.unwrap_or(0)} else {0}}
    pub fn requests(&mut self, id: u64) -> Vec<ClientEvent> {
        // CPlayer::Select returns without notifying the server for the same
        // selection. Do not repeatedly reset native targeting on double-click.
        if self.native && self.last==Some(id) { return vec![]; }
        self.last=Some(id);
        let mut events=vec![ScriptRequest::SetTarget(id).to_event()];
        if self.native {
            // CPlayer::Select -> CCombatSystem -> WM_Combat::18 (sender).
            // It sends event 0x82040001, distinct from WM_Player's 0x82120016.
            // Neither event is StartAttacking; selecting an NPC stays peaceful.
            events.push(ClientEvent::Interp { function:0x82040001,
                arguments:pack_values(&[PackValue::LongInt(id)]),encrypted:false });
        }
        events
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn selection_and_clear_update_both_native_targets_without_attacking() {
        let mut state=Selection::new(true);
        for id in [0x8000000000000123,0] {
            let calls=state.requests(id);
            assert_eq!(calls.len(),2);
            for (call,wanted) in calls.iter().zip([0x82120016,0x82040001]) {
                let ClientEvent::Interp {function,arguments,encrypted}=call else {panic!("Script call")};
                assert_eq!(*function,wanted);assert!(!encrypted);
                assert_eq!(unpack_values(arguments).unwrap(),vec![PackValue::LongInt(id)]);
            }
            assert!(state.requests(id).is_empty());
        }
        state.reset();
        assert_eq!(state.combat_target(),0);
        assert_eq!(state.requests(0).len(),2);
    }
    #[test]
    fn ac2e_retains_its_existing_target_request() {
        let mut state=Selection::new(false);
        for _ in 0..2 {assert_eq!(state.requests(42),vec![ScriptRequest::SetTarget(42).to_event()]);}
        assert_eq!(state.combat_target(),0);
    }
}
