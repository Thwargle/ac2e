//! Accepted animated support skills need the same weapon stance as attacks.
//! Prepare before spending resources; publish the mode before the cast story.
use super::*;

pub(super) struct Prepared {
    character: u64,
    mode: u32,
    stamp: u16,
    next: crate::emotes::State,
    wire: Vec<u8>,
}

pub(super) fn prepare(
    peer: &Peer,
    character: &crate::store::Character,
    behavior: u32,
    events: &mut Vec<Vec<u8>>,
) -> io::Result<Option<Prepared>> {
    if peer.combat || behavior == 0 {
        peer.validate_scene_batch(events)?;
        return Ok(None);
    }
    let mode = crate::starter_gear::combat_mode(&character.inventory);
    let (next, stamp) = peer.emotes.next_visual(character.instance).map_err(other)?;
    let wire = crate::combat_visual::mode_id(character.id, character.instance, stamp, mode).map_err(other)?;
    events.push(script::private_call(script::COMBAT_STATE, &script::integer(1)).map_err(other)?);
    // Reserve space for the owner's mode delivery as well as private results.
    // The cast has not spent health/vigor or started its cooldown at this point.
    events.push(wire);
    let admitted = peer.validate_scene_batch(events);
    let wire = events.pop().unwrap();
    admitted?;
    Ok(Some(Prepared { character: character.id, mode, stamp, next, wire }))
}

pub(super) fn publish(
    prepared: Option<Prepared>,
    peer: &mut Peer,
    scene: &mut crate::scene::Scene,
) -> io::Result<Vec<crate::scene::Delivery>> {
    let Some(prepared) = prepared else { return Ok(Vec::new()); };
    let updates = scene.broadcast(prepared.character, prepared.wire).map_err(other)?;
    scene.save_combat_mode(prepared.character, prepared.mode, prepared.stamp).map_err(other)?;
    peer.emotes = prepared.next;
    peer.combat = true;
    // Stance alone does not change selection or start attacking it.
    Ok(updates)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixture(test: impl FnOnce(&mut Peer, &crate::store::Character, &mut crate::scene::Scene)) {
        let path = std::env::temp_dir().join(format!("ac2-support-stance-{}-{}", std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
        let mut store = Store::open(&path).unwrap();
        store.create("local", "Stance Tester").unwrap();
        store.begin_instance("local", 1).unwrap();
        let mut peer = super::super::tests::peer();
        let origin = Placement::HUMAN_START;
        let mut scene = crate::scene::Scene::with_content(crate::content::Content::default());
        let wire = world::create_stationary_object(&world::StationaryObject {
            character_id: 1, parent_visual_did: 1, entity_did: 2, cell_id: origin.cell,
            position: origin.coordinates, orientation: origin.orientation(), instance_stamp: 1,
            visual_ordering_stamp: 0, timestamps: [0; 4],
        }).unwrap();
        scene.enter(crate::scene::Body::new(1, origin, wire).unwrap()).unwrap();
        test(&mut peer, &store.characters[&1], &mut scene);
        drop(store);
        std::fs::remove_dir_all(path).unwrap();
    }

    #[test]
    fn accepted_support_stance_precedes_the_story_without_retargeting() {
        fixture(|peer, character, scene| {
            peer.target = Some(99);
            let mut events = vec![script::chat("accepted", 1).unwrap()];
            let prepared = prepare(peer, character, 0x410000c2, &mut events).unwrap();
            assert!(!peer.combat && peer.scene_outbox.is_empty(), "Preparation is not acceptance");
            assert_eq!(events.last().unwrap(), &script::private_call(script::COMBAT_STATE, &script::integer(1)).unwrap());
            peer.queue_ordered_batch(events).unwrap();
            let stance = publish(prepared, peer, scene).unwrap();
            assert!(peer.combat);
            assert_eq!(peer.target, Some(99));
            assert!(peer.pending_skill.is_none());
            let expected = crate::combat_visual::mode_id(1, 1, 1, crate::combat_visual::MARTIAL_ARTS).unwrap();
            assert!(stance.iter().any(|d| d.recipient == 1 && d.data.as_ref() == expected));
            let story = skill_visual(peer, scene, 1, 1, 0x410000c2, 0x55, 0);
            let expected = crate::combat_visual::skill_attack(1, 1, 2, 0x410000c2, 0, 0x55).unwrap();
            assert!(story.iter().any(|d| d.recipient == 1 && d.data.as_ref() == expected),
                "The cast gets a newer native visual stamp than its stance");
        });
    }

    #[test]
    fn insufficient_room_for_the_mode_rejects_before_changing_stance() {
        fixture(|peer, character, _| {
            peer.queue_ordered_batch(vec![script::chat("fill", 1).unwrap(); 1022]).unwrap();
            let mut events = vec![script::chat("result", 1).unwrap()];
            assert!(prepare(peer, character, 0x410000c2, &mut events).is_err());
            assert!(!peer.combat);
            assert_eq!(peer.scene_outbox.len(), 1022);
            assert_eq!(peer.emotes.next_visual(1).unwrap().1, 1);
        });
    }

    #[test]
    fn no_animation_or_existing_combat_does_not_publish_a_mode_change() {
        fixture(|peer, character, scene| {
            let mut events = Vec::new();
            let prepared = prepare(peer, character, 0, &mut events).unwrap();
            assert!(prepared.is_none() && events.is_empty());
            assert!(publish(prepared, peer, scene).unwrap().is_empty());
            assert!(!peer.combat);
            peer.combat = true;
            let prepared = prepare(peer, character, 0x410000c2, &mut events).unwrap();
            assert!(prepared.is_none() && events.is_empty());
            assert_eq!(peer.emotes.next_visual(1).unwrap().1, 1);
        });
    }
}
