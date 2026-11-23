//! Native world exit and character selection share the reliable send window.
//! Keep one bounded transition until both messages can be delivered, without
//! changing native queues, resetting transport history, or losing a request.
use super::*;

pub(super) struct Pending {
    character: u64,
    selection: Vec<u8>,
    exit_sent: bool,
}

#[cfg(test)]
mod tests {
    use super::*;

    fn peer() -> Peer {
        let mut peer = super::super::tests::peer();
        peer.session.connect_ack(1, 1).unwrap();
        peer.session.request_enter(7, "local", true).unwrap();
        peer.session.scene_complete().unwrap();
        peer.motion = Some(Motion::new(Placement::HUMAN_START, 1));
        peer.target = Some(42);
        peer.combat = true;
        peer.pending_skill = Some((9, 0xb0));
        peer
    }

    fn selection() -> Vec<u8> {
        // A multi-fragment selection payload exercises window capacity, not
        // character decoding (covered by the real UDP fixture).
        let mut bytes = world::CHARACTER_SET.to_le_bytes().to_vec();
        bytes.resize(4096, 0);
        bytes
    }

    #[test]
    fn logout_survives_full_window_and_waits_for_exit_acknowledgment() {
        let now = Instant::now();
        let mut peer = peer();
        let mut receiver = Transport::new(2, 1, 1, 1);
        let mut last_ack = 0;
        for _ in 0..256 {
            for packet in peer.send_world_event(b"fill", true, now).unwrap() {
                last_ack = receiver.receive(&packet, now).unwrap().ack;
            }
        }
        peer.queue_scene(Arc::from(b"unsent old-world data".as_slice())).unwrap();
        let list = selection();
        peer.begin_logout(7, list.clone()).unwrap();
        assert_eq!(peer.session.phase, Phase::CharacterSelection);
        assert!(peer.motion.is_none() && peer.target.is_none() && !peer.combat);
        assert!(peer.pending_skill.is_none() && peer.scene_outbox.is_empty());
        assert_eq!(peer.scene_bytes, 0);
        let before = (peer.blob, peer.ordered_stamp);
        assert!(peer.poll_logout(now).unwrap().is_empty());
        assert_eq!((peer.blob, peer.ordered_stamp), before);

        // Free just one fragment: exit fits, the character list does not.
        peer.transport.acknowledge(2).unwrap();
        let exit = peer.poll_logout(now).unwrap();
        assert_eq!(exit.len(), 1);
        let fragment = wire::Fragment::decode(&exit[0][wire::HEADER_LEN..]).unwrap();
        assert_eq!(fragment.queue, 1);
        assert_eq!(fragment.blob_id >> 32, 0x0100_0101);
        assert_eq!(fragment.data, world::char_exit_game());
        assert!(peer.poll_logout(now).unwrap().is_empty());
        peer.transport.acknowledge(last_ack).unwrap();
        // Even a completely free window must not let the unordered list
        // overtake the exit. Simulate losing the first exit packet entirely.
        assert!(peer.poll_logout(now).unwrap().is_empty());
        let retry = peer.transport.retransmit_due(now + Duration::from_secs(1)).unwrap();
        assert_eq!(retry, exit);
        let received = receiver.receive(&retry[0], now).unwrap();
        assert_eq!(received.messages.len(), 1);
        assert_eq!(received.messages[0].data, world::char_exit_game());
        peer.transport.acknowledge(received.ack).unwrap();
        let packets = peer.poll_logout(now).unwrap();
        assert!(packets.len() > 1);
        assert!(peer.logout_pending.is_none());
        let mut messages = Vec::new();
        for packet in packets.iter().rev() {
            let fragment = wire::Fragment::decode(&packet[wire::HEADER_LEN..]).unwrap();
            assert_eq!(fragment.queue, 1);
            assert_eq!((fragment.blob_id >> 32) & 0xffff_0000, 0x2300_0000);
            let result = receiver.receive(packet, now).unwrap();
            last_ack = result.ack;
            messages.extend(result.messages);
        }
        assert_eq!(messages.len(), 1);
        assert_eq!(messages[0].data, list);
        assert!(peer.poll_logout(now).unwrap().is_empty());
        peer.transport.acknowledge(last_ack).unwrap();
        peer.session.request_enter(8, "local", true).unwrap();
        // Same UDP session/cipher remains usable after returning to selection.
        let packets = peer.send_world_event(b"next character", false, now).unwrap();
        assert_eq!(receiver.receive(&packets[0], now).unwrap().messages[0].data, b"next character");
    }

    #[test]
    fn logout_admission_rejects_bad_identity_and_invalid_list_before_mutation() {
        let mut peer = peer();
        for list in [Vec::new(), vec![0; 4], vec![0; 65537]] {
            assert!(peer.begin_logout(7, list).is_err());
            assert_eq!(peer.session.phase, Phase::InWorld { character: 7 });
            assert!(peer.logout_pending.is_none() && peer.motion.is_some() && peer.combat);
        }
        assert!(peer.begin_logout(8, selection()).is_err());
        assert_eq!(peer.session.phase, Phase::InWorld { character: 7 });
        assert!(peer.logout_pending.is_none());
    }

    #[test]
    fn entering_logout_and_duplicate_requests_do_not_replace_the_pending_reply() {
        let mut peer = peer();
        peer.session.phase = Phase::Entering { character: 7 };
        peer.begin_logout(7, selection()).unwrap();
        assert!(peer.check_logout_retry(&world::ClientEvent::LogOffCharacter { character_id: 7 }).is_ok());
        assert!(peer.check_logout_retry(&world::ClientEvent::LogOffCharacter { character_id: 8 }).is_err());
        assert!(peer.check_logout_retry(&world::ClientEvent::EnterWorld {
            character_id: 8, account: b"local".to_vec(),
        }).is_err());
        assert!(peer.check_logout_retry(&world::ClientEvent::SceneRenderingComplete).is_err());
        assert!(peer.begin_logout(7, selection()).is_err());
        assert!(!peer.logout_pending.as_ref().unwrap().exit_sent);
        assert_eq!(peer.logout_pending.as_ref().unwrap().selection, selection());
    }
}

impl Peer {
    pub(super) fn check_logout_retry(&self, event: &world::ClientEvent) -> io::Result<()> {
        match (&self.logout_pending, event) {
            (Some(pending), world::ClientEvent::LogOffCharacter { character_id })
                if pending.character == *character_id => Ok(()),
            _ => Err(invalid("character logout is still completing")),
        }
    }

    pub(super) fn begin_logout(&mut self, character: u64, selection: Vec<u8>) -> io::Result<()> {
        if self.logout_pending.is_some()
            || !(4..=65536).contains(&selection.len())
            || selection[..4] != world::CHARACTER_SET.to_le_bytes()
        {
            return Err(invalid("invalid pending character selection"));
        }
        // Validate and retain the whole reply before changing the session. A
        // full fragment window is normal backpressure, not a failed logout.
        self.session.leave_world(character).map_err(other)?;
        self.friends = Default::default();
        self.logout_pending = Some(Pending { character, selection, exit_sent: false });
        self.clear_scene_outbox();
        self.motion = None;
        self.entering_world_fx = false;
        self.portal = None;
        self.target = None;
        self.combat = false;
        self.pending_skill = None;
        self.area_healing = false;
        self.offensive_area = false;
        self.binding_target = None;
        self.recall = None;
        self.item_motion = None;
        self.native_shop = None;
        self.merchant_open_ms = None;
        self.last_shop_refresh_ms = None;
        self.npc_dialogue = Default::default();
        self.guide_page = 0;
        Ok(())
    }

    pub(super) fn poll_logout(&mut self, now: Instant) -> io::Result<Vec<Vec<u8>>> {
        let Some(pending) = &self.logout_pending else { return Ok(Vec::new()); };
        if !pending.exit_sent {
            let exit = world::char_exit_game();
            if !self.transport.send_available(exit.len()).map_err(other)? {
                return Ok(Vec::new());
            }
            // Still use the departing world's ordered queue, even though the
            // player is already removed from server gameplay/ownership.
            let packets = self.send_world_event(&exit, true, now)?;
            self.logout_pending.as_mut().unwrap().exit_sent = true;
            return Ok(packets);
        }
        // CharacterSet is pre-world unordered traffic (bit 61). Without an
        // acknowledgment barrier it can overtake CharExitGame on the wire.
        // No new world events or time-sync packets are admitted during exit.
        if self.transport.has_unacknowledged()
            || !self.transport.send_available(pending.selection.len()).map_err(other)?
        {
            return Ok(Vec::new());
        }
        let selection = pending.selection.clone();
        let packets = self.send_to_queue(1, &selection, now)?;
        self.logout_pending = None;
        Ok(packets)
    }
}
