use super::*;
use crate::{content::Content, encounters::Encounters, scene::{Body, Scene}};

fn fixture(test: impl FnOnce(&mut Peer, &mut Option<Encounters>, &mut Scene, &mut Store, u64, u64)) {
    let path = std::env::temp_dir().join(format!("ac2-attack-cancel-{}-{}", std::process::id(),
        std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
    let mut store = Store::open(&path).unwrap();
    store.create("local", "Attack Tester").unwrap();
    store.begin_instance("local", 1).unwrap();
    let origin = Placement::HUMAN_START;
    let near = crate::content::NAMESPACE + 7;
    let far = near + 1;
    let mut content = Content::fixture(near, origin.translated([2., 0., 0.]).unwrap(), None);
    let other = Content::fixture(far, origin.translated([10., 0., 0.]).unwrap(), None);
    content.insert_live(other.object(far).unwrap().clone(), true).unwrap();
    let mut runtime = Encounters::load(&std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("content/development-encounters.tsv"), &content).unwrap();
    let mut scene = Scene::with_content(content);
    runtime.initialize_spawners(&mut scene, 0).unwrap();
    for action in runtime.spawners_mut_for_test().poll(0) {
        if let crate::spawners::Action::Spawn(id) = action {
            scene.respawn_content(id).unwrap();
            runtime.spawned(id).unwrap();
        }
    }
    let spawn = world::create_stationary_object(&world::StationaryObject {
        character_id: 1, parent_visual_did: 1, entity_did: 2, cell_id: origin.cell,
        position: origin.coordinates, orientation: origin.orientation(), instance_stamp: 1,
        visual_ordering_stamp: 0, timestamps: [0; 4],
    }).unwrap();
    scene.enter(Body::new(1, origin, spawn).unwrap()).unwrap();
    runtime.basic_skill(1, near, 0, &scene, &store).unwrap();
    let mut peer = tests::peer();
    peer.combat = true;
    peer.target = Some(near);
    peer.pending_skill = Some((0, 0x4c)); // Native request counters start at zero.
    test(&mut peer, &mut Some(runtime), &mut scene, &mut store, near, far);
    drop(store);
    std::fs::remove_dir_all(path).unwrap();
}

fn completions(peer: &Peer) -> usize {
    let expected = script::skill_finished(0).unwrap();
    peer.scene_outbox.iter().filter(|(wire, ordered)| *ordered && wire.as_ref() == expected).count()
}

fn fill(peer: &mut Peer, count: usize) {
    peer.queue_ordered_batch(vec![script::chat("fill", 1).unwrap(); count]).unwrap();
}

#[test]
fn cancellation_completes_request_zero_once_and_removes_the_server_attack() {
    fixture(|peer, runtime, _, _, _, _| {
        peer.cancel_attack(1, runtime).unwrap();
        peer.cancel_attack(1, runtime).unwrap();
        assert!(peer.pending_skill.is_none());
        assert_eq!(runtime.as_ref().unwrap().attacking_target(1), None);
        assert_eq!(completions(peer), 1);
    });
}

#[test]
fn failed_cancellation_keeps_the_request_and_attack_for_retry() {
    fixture(|peer, runtime, _, _, near, _| {
        fill(peer, 1024);
        assert!(peer.cancel_attack(1, runtime).is_err());
        assert_eq!(peer.pending_skill, Some((0, 0x4c)));
        assert_eq!(runtime.as_ref().unwrap().attacking_target(1), Some(near));
        assert_eq!(completions(peer), 0);
        peer.clear_scene_outbox();
        peer.cancel_attack(1, runtime).unwrap();
        assert_eq!(completions(peer), 1);
    });
}

#[test]
fn refreshing_selection_does_not_turn_a_single_spell_into_auto_attack() {
    fixture(|peer, runtime, scene, store, near, _| {
        let encounters = runtime.as_mut().unwrap();
        encounters.stop(1);
        encounters.strike(1, near, 0, scene, store, crate::encounters::Strike {
            status_fx: Vec::new(),
            ratings: Vec::new(), skill: 0x59, bonus: 1, vigor_damage: 0, weapon_damage: false,
            cost: 0, range: 60., behavior: 0x410000b6, fx: 0x41000241, burn: Vec::new(),
        }).unwrap();
        select_target(peer, 1, Some(near), scene, store, runtime, 0).unwrap();
        assert_eq!(peer.pending_skill, Some((0, 0x4c)));
        assert_eq!(completions(peer), 0);
        for now in [0, 1000, 2000, 3000] {
            runtime.as_mut().unwrap().tick(now, scene, store, [1].into_iter(), None).unwrap();
        }
        assert_eq!(runtime.as_ref().unwrap().attacking_target(1), None,
            "Refreshing inspection must not start repeat attacks after the spell");
    });
}

#[test]
fn selecting_an_out_of_range_target_stops_attacking_the_previous_creature() {
    fixture(|peer, runtime, scene, store, _, far| {
        assert!(scene.can_observe(1, far));
        select_target(peer, 1, Some(far), scene, store, runtime, 0).unwrap();
        assert_eq!(peer.target, Some(far));
        assert!(peer.pending_skill.is_none());
        assert_eq!(runtime.as_ref().unwrap().attacking_target(1), None);
        assert_eq!(completions(peer), 1);
        assert!(peer.combat, "Changing targets does not leave combat stance");
    });
}

#[test]
fn clearing_selection_cancels_the_queued_skill() {
    fixture(|peer, runtime, scene, store, _, _| {
        select_target(peer, 1, None, scene, store, runtime, 0).unwrap();
        assert!(peer.target.is_none() && peer.pending_skill.is_none());
        assert_eq!(runtime.as_ref().unwrap().attacking_target(1), None);
        assert_eq!(completions(peer), 1);
    });
}

#[test]
fn selecting_in_combat_stance_without_repeat_intent_never_starts_attacks() {
    fixture(|peer, runtime, scene, store, near, _| {
        peer.cancel_attack(1,runtime).unwrap();
        peer.target=None;
        select_target(peer,1,Some(near),scene,store,runtime,0).unwrap();
        assert_eq!(peer.target,Some(near));assert!(peer.combat);
        assert_eq!(runtime.as_ref().unwrap().attacking_target(1),None);
    });
}

#[test]
fn changing_a_pending_skill_target_cancels_it_without_starting_repeat_attacks() {
    fixture(|peer, runtime, scene, store, near, far| {
        scene.move_creature(far,Placement::HUMAN_START.translated([2.,1.,0.]).unwrap(),0).unwrap();
        assert_eq!(runtime.as_ref().unwrap().attacking_target(1),Some(near));
        select_target(peer,1,Some(far),scene,store,runtime,0).unwrap();
        assert_eq!(runtime.as_ref().unwrap().attacking_target(1),None);
        assert_eq!(completions(peer),1);assert!(peer.combat);
    });
}

#[test]
fn manual_retargeting_keeps_an_existing_basic_repeat_chain() {
    fixture(|peer, runtime, scene, store, near, far| {
        peer.pending_skill=None;
        scene.move_creature(far,Placement::HUMAN_START.translated([2.,1.,0.]).unwrap(),0).unwrap();
        assert_eq!(runtime.as_ref().unwrap().auto_attacking_target(1),Some(near));
        select_target(peer,1,Some(far),scene,store,runtime,0).unwrap();
        assert_eq!(runtime.as_ref().unwrap().auto_attacking_target(1),Some(far));
        assert_eq!(completions(peer),0);assert!(peer.combat);
    });
}

#[test]
fn rejected_selection_preserves_the_original_target_and_pending_attack() {
    fixture(|peer, runtime, scene, store, near, far| {
        assert!(select_target(peer, 1, Some(u64::MAX), scene, store, runtime, 0).is_err());
        assert!(peer.scene_outbox.is_empty());
        // One free slot cannot admit both the new selection and completion.
        fill(peer, 1023);
        assert!(select_target(peer, 1, Some(far), scene, store, runtime, 0).is_err());
        assert_eq!(peer.target, Some(near));
        assert_eq!(peer.pending_skill, Some((0, 0x4c)));
        assert_eq!(runtime.as_ref().unwrap().attacking_target(1), Some(near));
        assert_eq!(peer.scene_outbox.len(), 1023);
        assert_eq!(completions(peer), 0);
    });
}

fn drop_action(store: &mut Store) -> item_motion::Action {
    let item = crate::training_kit::item(crate::inventory::ITEM_NAMESPACE + 1, 0);
    store.inventory_command("local", 1, crate::inventory::Command::Grant(item.clone())).unwrap();
    item_motion::Action::Drop(crate::inventory_request::DropRequest {
        landblock: Placement::HUMAN_START.cell & 0xffff0000,
        moved: crate::inventory_request::MoveRequest {
            owner: 1, item: item.id, source: 0, destination: 0, quantity: 1,
        },
    })
}

#[test]
fn item_animation_cancels_both_the_skill_request_and_runtime_attack() {
    fixture(|peer, runtime, scene, store, _, _| {
        let action = drop_action(store);
        item_motion::begin(peer, 1, action, store, scene, &crate::loot::Loot::default(), runtime, 0).unwrap();
        assert!(peer.item_motion.is_some());
        assert!(!peer.combat && peer.pending_skill.is_none());
        assert_eq!(runtime.as_ref().unwrap().attacking_target(1), None);
        assert_eq!(completions(peer), 1);
    });
}

#[test]
fn failed_item_animation_cannot_leave_a_cancelled_skill_running_on_the_server() {
    fixture(|peer, runtime, scene, store, _, _| {
        let action = drop_action(store);
        fill(peer, 1023); // Completion fits, but the peace-mode packet does not.
        assert!(item_motion::begin(peer, 1, action, store, scene,
            &crate::loot::Loot::default(), runtime, 0).is_err());
        assert!(peer.item_motion.is_none() && peer.pending_skill.is_none());
        assert_eq!(runtime.as_ref().unwrap().attacking_target(1), None);
        assert_eq!(completions(peer), 1);
        assert!(store.characters[&1].inventory.items().any(|item| item.id == crate::inventory::ITEM_NAMESPACE + 1));
    });
}

#[test]
fn invalid_item_action_does_not_cancel_combat() {
    fixture(|peer, runtime, scene, store, near, _| {
        assert!(item_motion::begin(peer, 1, item_motion::Action::Pickup(u64::MAX), store, scene,
            &crate::loot::Loot::default(), runtime, 0).is_err());
        assert!(peer.item_motion.is_none());
        assert_eq!(peer.pending_skill, Some((0, 0x4c)));
        assert_eq!(runtime.as_ref().unwrap().attacking_target(1), Some(near));
        assert!(peer.scene_outbox.is_empty());
    });
}
