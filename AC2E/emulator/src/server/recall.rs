//! Pyreal's recovered Player.LifestoneRecall charges for 8 s, releases with
//! Enter_World, and moves the bound character 2 s later.
use super::*;
use crate::combat_visual as visual;
use crate::scene::{Delivery, Scene};

pub(super) struct Pending {
    character: u64,
    instance: u16,
    origin: Placement,
    destination: Placement,
    request: Option<u32>,
    released: bool,
    deadline: u64,
}

fn behavior(
    peer: &mut Peer,
    scene: &mut Scene,
    cast: &Pending,
    action: u32,
    stop: bool,
) -> io::Result<Vec<Delivery>> {
    let (next, stamp) = peer.emotes.next_visual(cast.instance).map_err(other)?;
    let wire = visual::recall_behavior(cast.character, cast.instance, stamp, action, stop)
        .map_err(other)?;
    let deliveries = scene.broadcast(cast.character, wire).map_err(other)?;
    scene
        .save_visual_stamp(cast.character, stamp)
        .map_err(other)?;
    peer.emotes = next;
    Ok(deliveries)
}

pub(super) fn begin(
    peer: &mut Peer,
    character: u64,
    request: Option<u32>,
    store: &Store,
    scene: &mut Scene,
    now: u64,
) -> io::Result<Vec<Delivery>> {
    let saved = store
        .characters
        .get(&character)
        .filter(|saved| {
            !saved.deleted
                && saved.account == peer.session.account
                && peer.session.phase == (Phase::InWorld { character })
        })
        .ok_or_else(|| invalid("Wait until you arrive."))?;
    if peer.combat || !saved.progression.alive() {
        return Err(invalid("Recall while alive and out of combat."));
    }
    if peer.recall.is_some() || peer.pending_skill.is_some() || peer.item_motion.is_some() || peer.portal.is_some() {
        return Err(invalid("Finish your current action before recalling."));
    }
    let destination = saved
        .binding
        .ok_or_else(|| invalid("Bind to a lifestone before recalling."))?;
    let origin = scene
        .player_placement(character)
        .ok_or_else(|| invalid("Wait until you arrive."))?;
    let cast = Pending {
        character,
        instance: saved.instance,
        origin,
        destination,
        request,
        released: false,
        deadline: now.saturating_add(8000),
    };
    // Cast in magic mode without starting an automatic attack against the selected target.
    let (next, stamp) = peer.emotes.next_visual(saved.instance).map_err(other)?;
    let mode = visual::mode_id(character, saved.instance, stamp, visual::MAGIC).map_err(other)?;
    let mut updates = scene.broadcast(character, mode).map_err(other)?;
    scene
        .save_combat_mode(character, visual::MAGIC, stamp)
        .map_err(other)?;
    peer.emotes = next;
    updates.extend(behavior(peer, scene, &cast, visual::RECALL_CHARGE, false)?);
    peer.recall = Some(cast);
    Ok(updates)
}

pub(super) fn tick(
    peer: &mut Peer,
    store: &mut Store,
    scene: &mut Scene,
    encounters: &mut Option<crate::encounters::Encounters>,
    bootstrap: &mut Option<crate::bootstrap::Bootstrap>,
    now: u64,
) -> io::Result<Vec<Delivery>> {
    let Some(mut cast) = peer.recall.take() else {
        return Ok(vec![]);
    };
    let id = cast.character;
    if peer.session.phase != (Phase::InWorld { character: id })
        || !store
            .characters
            .get(&id)
            .is_some_and(|s| s.instance == cast.instance)
    {
        return Ok(vec![]);
    }
    let saved = &store.characters[&id];
    let uninterrupted = !peer.combat
        && peer.item_motion.is_none()
        && saved.progression.alive()
        && saved.binding == Some(cast.destination)
        && scene
            .player_placement(id)
            .and_then(|p| cast.origin.distance(p))
            .is_some_and(|distance| distance <= 0.75);
    if uninterrupted && now < cast.deadline {
        peer.recall = Some(cast);
        return Ok(vec![]);
    }
    let mut updates = Vec::new();
    if uninterrupted && !cast.released {
        updates.extend(player_fx(
            peer,
            scene,
            id,
            cast.instance,
            visual::ENTER_WORLD_FX,
        ));
        // A zero-distance movement heartbeat for the old login must not stop
        // the new release glow before teleportation.
        peer.entering_world_fx = false;
        updates.extend(behavior(peer, scene, &cast, visual::RECALL_CHARGE, true)?);
        updates.extend(behavior(
            peer,
            scene,
            &cast,
            visual::LIFESTONE_RELEASE,
            false,
        )?);
        // The recovered player routine sends the storyboard privately. Nearby
        // players keep the public LS_Recall1 motion rather than overriding it.
        let (next, stamp) = peer.emotes.next_visual(cast.instance).map_err(other)?;
        let data = visual::skill_story(
            id,
            cast.instance,
            stamp,
            visual::PORTAL_RECALL,
            cast.request,
            crate::skills::LIFESTONE_RECALL,
        )
        .map_err(other)?;
        updates.push(Delivery {
            recipient: id,
            data: data.into(),
        });
        scene.save_visual_stamp(id, stamp).map_err(other)?;
        peer.emotes = next;
        cast.released = true;
        cast.deadline = now.saturating_add(2000);
        peer.recall = Some(cast);
        return Ok(updates);
    }
    let travel = if uninterrupted {
        finish_recall(peer, id, store, scene, encounters, bootstrap, now)
    } else {
        Err(invalid("Lifestone recall interrupted."))
    };
    match travel {
        Ok(moved) => {
            updates.extend(moved);
            peer.entering_world_fx = true;
        }
        Err(reason) => {
            if cast.released {
                updates.extend(behavior(
                    peer,
                    scene,
                    &cast,
                    visual::LIFESTONE_RELEASE,
                    true,
                )?);
                updates.extend(
                    behavior(peer, scene, &cast, visual::PORTAL_RECALL, true)?
                        .into_iter()
                        .filter(|d| d.recipient == id),
                );
                updates.extend(player_fx_action(
                    peer,
                    scene,
                    id,
                    cast.instance,
                    visual::ENTER_WORLD_FX,
                    true,
                ));
            } else {
                updates.extend(behavior(peer, scene, &cast, visual::RECALL_CHARGE, true)?);
            }
            peer.queue_ordered_batch(vec![script::chat(&reason.to_string(), 1).map_err(other)?])?;
        }
    }
    if store.characters[&id].progression.alive() {
        updates.extend(training_combat_visual(
            peer,
            id,
            store,
            scene,
            peer.combat,
            false,
            false,
        )?);
    }
    if let Some(request) = cast.request {
        peer.queue_ordered_batch(vec![script::skill_finished(request).map_err(other)?])?;
    }
    Ok(updates)
}

pub(super) fn glowing(peer: &Peer) -> bool {
    peer.entering_world_fx || peer.recall.as_ref().is_some_and(|cast| cast.released)
}

pub(super) fn charging(peer: &Peer) -> bool {
    peer.recall.as_ref().is_some_and(|cast| !cast.released)
}

#[cfg(test)]
mod tests {
    use super::*;
    use ac2_net::protocol::{Build, ServerEvent};
    use std::time::{SystemTime, UNIX_EPOCH};

    fn fixture(test: impl FnOnce(&mut Store, &mut Peer, &mut Scene)) {
        let path = std::env::temp_dir().join(format!(
            "ac2-recall-{}-{}",
            std::process::id(),
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        let mut store = Store::open(&path).unwrap();
        store.create("local", "Recall Tester").unwrap();
        store.begin_instance("local", 1).unwrap();
        let origin = Placement::HUMAN_START;
        store
            .bind_lifestone("local", 1, origin.translated([3., 0., 0.]).unwrap())
            .unwrap();
        store.set_placement(1, origin).unwrap();
        drop(store);
        let mut store = Store::open(&path).unwrap();
        let mut peer = super::super::tests::peer();
        peer.session.phase = Phase::InWorld { character: 1 };
        peer.motion = Some(crate::movement::Motion::new(origin, 1));
        let mut scene = Scene::with_content(crate::content::Content::default());
        for id in [1, 2] {
            let wire = world::create_stationary_object(&world::StationaryObject {
                character_id: id,
                parent_visual_did: 1,
                entity_did: 2,
                cell_id: origin.cell,
                position: origin.coordinates,
                orientation: origin.orientation(),
                instance_stamp: 1,
                visual_ordering_stamp: 0,
                timestamps: [0; 4],
            })
            .unwrap();
            scene
                .enter(crate::scene::Body::new(id, origin, wire).unwrap())
                .unwrap();
        }
        test(&mut store, &mut peer, &mut scene);
        drop(store);
        std::fs::remove_dir_all(path).unwrap();
    }

    #[test]
    fn saved_binding_charges_and_releases_publicly_with_a_private_story_for_either_entry_point() {
        for request in [None, Some(47)] {
            fixture(|store, peer, scene| {
                peer.last_world_use_ms = Some(1000);
                let initial = begin(peer, 1, request, store, scene, 1000).unwrap();
                for recipient in [1, 2] {
                    let actions: Vec<_> = initial
                        .iter()
                        .filter(|d| d.recipient == recipient)
                        .map(|d| d.data.as_ref())
                        .collect();
                    assert_eq!(
                        actions,
                        vec![
                            visual::mode_id(1, 1, 1, visual::MAGIC).unwrap(),
                            visual::recall_behavior(1, 1, 2, visual::RECALL_CHARGE, false).unwrap()
                        ]
                    );
                }
                assert!(charging(peer) && !glowing(peer));
                assert!(begin(peer, 1, request, store, scene, 1001).is_err());
                assert!(tick(peer, store, scene, &mut None, &mut None, 8999)
                    .unwrap()
                    .is_empty());
                let release = tick(peer, store, scene, &mut None, &mut None, 9000).unwrap();
                for recipient in [1, 2] {
                    let actions: Vec<_> = release
                        .iter()
                        .filter(|d| d.recipient == recipient)
                        .map(|d| d.data.as_ref())
                        .collect();
                    assert_eq!(actions.len(), if recipient == 1 { 4 } else { 3 });
                    assert_eq!(
                        actions[0],
                        visual::effect(1, 1, 3, visual::ENTER_WORLD_FX).unwrap()
                    );
                    assert_eq!(
                        actions[1],
                        visual::recall_behavior(1, 1, 4, visual::RECALL_CHARGE, true).unwrap()
                    );
                    assert_eq!(
                        actions[2],
                        visual::recall_behavior(1, 1, 5, visual::LIFESTONE_RELEASE, false).unwrap()
                    );
                    if recipient == 1 {
                        assert_eq!(
                            actions[3],
                            visual::skill_story(
                                1,
                                1,
                                6,
                                visual::PORTAL_RECALL,
                                request,
                                crate::skills::LIFESTONE_RECALL
                            )
                            .unwrap()
                        );
                    }
                }
                assert!(!charging(peer) && glowing(peer));
                assert!(tick(peer, store, scene, &mut None, &mut None, 10999)
                    .unwrap()
                    .is_empty());
                assert_eq!(scene.player_placement(1), Some(Placement::HUMAN_START));
                if let Some(dir) = std::env::var_os("AC2_TEST_GAME_DATA") {
                    let mut bootstrap = Some(
                        crate::bootstrap::Bootstrap::load_profile(std::path::Path::new(&dir), true).unwrap(),
                    );
                    tick(peer, store, scene, &mut None, &mut bootstrap, 11000).unwrap();
                    assert_eq!(store.placement(1), store.characters[&1].binding);
                    assert_eq!(scene.player_placement(1), store.characters[&1].binding);
                    assert!(peer.recall.is_none() && glowing(peer));
                    if let Some(request) = request {
                        let end = script::skill_finished(request).unwrap();
                        assert!(peer
                            .scene_outbox
                            .iter()
                            .any(|(wire, ordered)| *ordered && wire.as_ref() == end));
                    }
                }
            });
        }
    }

    #[test]
    fn released_cast_cancels_on_combat_and_preserves_location() {
        fixture(|store, peer, scene| {
            begin(peer, 1, Some(42), store, scene, 0).unwrap();
            tick(peer, store, scene, &mut None, &mut None, 8000).unwrap();
            peer.combat = true;
            let stop = tick(peer, store, scene, &mut None, &mut None, 8001).unwrap();
            assert!(!glowing(peer) && !charging(peer));
            assert_eq!(scene.player_placement(1), Some(Placement::HUMAN_START));
            assert!(stop.iter().any(|d| d.recipient == 2
                && d.data.as_ref()
                    == visual::stop_effect(1, 1, 9, visual::ENTER_WORLD_FX).unwrap()));
        });
    }

    #[test]
    fn native_create_preserves_other_fields_and_snapshots_charge_or_glow() {
        let p = Placement::HUMAN_START;
        let original = world::create_stationary_object(&world::StationaryObject {
            character_id: 1,
            parent_visual_did: 0x1f00031e,
            entity_did: 0x47000001,
            cell_id: p.cell,
            position: p.coordinates,
            orientation: p.orientation(),
            instance_stamp: 4,
            visual_ordering_stamp: 21,
            timestamps: [3; 4],
        })
        .unwrap();
        let ServerEvent::CreateObject(before) =
            ServerEvent::decode(&original, Build::Legions7160).unwrap()
        else {
            panic!()
        };
        for charging in [true, false] {
            let fx = if charging {
                vec![0x41000031]
            } else {
                vec![visual::ENTER_WORLD_FX, 0x41000031]
            };
            let wire = world::with_visible_physics(&original, &fx, charging).unwrap();
            let ServerEvent::CreateObject(mut after) =
                ServerEvent::decode(&wire, Build::Legions7160).unwrap()
            else {
                panic!()
            };
            assert_eq!(after.physics.behaviors.len(), usize::from(charging));
            if charging {
                let p = &after.physics.behaviors[0];
                assert_eq!(
                    (p.behavior, p.mode, p.hold_cycles, p.moveto_cancels),
                    (visual::RECALL_CHARGE, visual::MAGIC, 0, 1)
                );
            }
            assert_eq!(after.physics.fx.iter().map(|e| e.0).collect::<Vec<_>>(), fx);
            after.physics.behaviors.clear();
            after.physics.fx.clear();
            after.physics.bitfield &= !(0x201);
            assert_eq!(after, before);
        }
    }
}
