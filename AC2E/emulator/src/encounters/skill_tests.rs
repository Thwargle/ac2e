use super::*;
use crate::{scene::Body, world};

fn fixture(label: &str) -> (Encounters, Scene, Store, std::path::PathBuf, u64) {
    let path = std::env::temp_dir().join(format!(
        "ac2-skill-{label}-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    let mut store = Store::open(&path).unwrap();
    store.create("local", "Spell Tester").unwrap();
    store
        .progression_command("local", 1, Command::AwardXp(1000))
        .unwrap();
    store
        .quest_command("local", 1, crate::quests::Command::Accept)
        .unwrap();
    let id = crate::content::NAMESPACE + 7;
    let position = Placement::HUMAN_START;
    let mut target = position;
    target.coordinates[0] += 2.0;
    let content = Content::fixture(id, target, None);
    let mut runtime = Encounters::parse(
        include_str!("../../content/development-encounters.tsv"),
        &content,
    )
    .unwrap();
    runtime.dice = crate::combat::Dice::for_test(1);
    runtime.enable_training_loot();
    runtime.next_regen = u64::MAX;
    let mut scene = Scene::with_content(content);
    runtime.initialize_spawners(&mut scene, 0).unwrap();
    assert_eq!(
        runtime.spawners.poll(0),
        vec![crate::spawners::Action::Spawn(id)]
    );
    scene.respawn_content(id).unwrap();
    runtime.spawned(id).unwrap();
    let bytes = world::create_stationary_object(&world::StationaryObject {
        character_id: 1,
        parent_visual_did: 1,
        entity_did: 2,
        cell_id: position.cell,
        position: position.coordinates,
        orientation: position.orientation(),
        instance_stamp: 1,
        visual_ordering_stamp: 0,
        timestamps: [0; 4],
    })
    .unwrap();
    scene.enter(Body::new(1, position, bytes).unwrap()).unwrap();
    let actor = runtime.actors.get_mut(&id).unwrap();
    actor.target = Some(1);
    actor.next_attack = u64::MAX;
    (runtime, scene, store, path, id)
}

fn bolt() -> Strike {
    Strike {
        ratings: None,
        skill: 0x59,
        bonus: 3,
        weapon_damage: true,
        cost: 32,
        range: 60.0,
        behavior: 0x410000b6,
        fx: 0x41000241,
        burn: Some(Over {
            health: 1,
            vigor: 0,
            period_ms: 1000,
            duration_ms: 30000,
        }),
    }
}

#[test]
fn burn_kill_uses_normal_quest_reward_loot_and_spawner_transaction_once() {
    let (mut runtime, mut scene, mut store, path, target) = fixture("burn");
    runtime.actors.get_mut(&target).unwrap().health = 26;
    let before = store.characters[&1].progression.clone();
    runtime
        .strike(1, target, 0, &scene, &store, bolt())
        .unwrap();
    let first = runtime
        .tick(0, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert!(first.iter().any(|e| matches!(
        e,
        Event::SkillSwing {
            behavior: 0x410000b6,
            ..
        }
    )));
    assert_eq!(store.characters[&1].progression.vigor, before.vigor - 32);
    assert_eq!(runtime.actors[&target].health, 3);
    let mut events = Vec::new();
    for now in [1000, 2000, 3000, 4000] {
        events.extend(
            runtime
                .tick(now, &mut scene, &mut store, [1].into_iter(), None)
                .unwrap(),
        );
    }
    assert_eq!(runtime.actors[&target].health, 0);
    assert_eq!(
        events
            .iter()
            .filter(|e| matches!(e, Event::Loot { .. }))
            .count(),
        1
    );
    assert_eq!(store.characters[&1].quests.get().unwrap().killed, 1);
    assert_eq!(store.characters[&1].progression.xp, before.xp + 40);
    assert_eq!(store.characters[&1].progression.gold, before.gold + 2);
    assert_eq!(store.characters[&1].progression.vigor, before.vigor - 32);
    assert!(runtime.burns.is_empty());
    // A new incarnation at the same content ID must not inherit the old burn.
    scene.despawn_content(target).unwrap();
    runtime.removed(target, 4000);
    assert_eq!(
        runtime.spawners.poll(34000),
        vec![crate::spawners::Action::Spawn(target)]
    );
    scene.respawn_content(target).unwrap();
    runtime.spawned(target).unwrap();
    runtime
        .tick(34000, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert_eq!(runtime.actors[&target].health, 60);
    drop(store);
    let reopened = Store::open(&path).unwrap();
    assert_eq!(reopened.characters[&1].quests.get().unwrap().killed, 1);
    assert_eq!(reopened.characters[&1].progression.xp, before.xp + 40);
    assert_eq!(reopened.characters[&1].progression.vigor, before.vigor - 32);
    drop(reopened);
    std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn cancelled_or_invalid_spell_cannot_charge_or_leave_a_burn() {
    let (mut runtime, mut scene, mut store, path, target) = fixture("cancel");
    let before = store.characters[&1].progression.clone();
    assert!(runtime.strike(1, 1, 0, &scene, &store, bolt()).is_err());
    assert!(
        runtime
            .strike(1, target + 1, 0, &scene, &store, bolt())
            .is_err()
    );
    runtime
        .strike(1, target, 0, &scene, &store, bolt())
        .unwrap();
    runtime.stop(1);
    runtime
        .tick(0, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert_eq!(store.characters[&1].progression, before);
    assert_eq!(runtime.actors[&target].health, 60);
    assert!(runtime.burns.is_empty());
    drop(store);
    std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn a_debuff_changes_ratings_without_inventing_weapon_damage_and_expires() {
    let (mut runtime, mut scene, mut store, path, target) = fixture("debuff");
    let effect = Strike {
        bonus: 0,
        weapon_damage: false,
        burn: None,
        ratings: Some((
            crate::combat::Ratings {
                armor: -5,
                ..Default::default()
            },
            1000,
        )),
        ..bolt()
    };
    runtime
        .strike(1, target, 0, &scene, &store, effect)
        .unwrap();
    let events = runtime
        .tick(0, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert_eq!(runtime.actors[&target].health, 60);
    assert!(
        !events
            .iter()
            .any(|e| matches!(e, Event::Hit { victim, .. } if *victim == target))
    );
    assert_eq!(runtime.ratings_for(target).armor, -5);
    runtime
        .tick(1000, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert_eq!(runtime.ratings_for(target).armor, 0);
    drop(store);
    std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn healing_over_time_respects_period_expiry_and_never_resurrects() {
    let (mut runtime, mut scene, mut store, path, _) = fixture("heal");
    store
        .progression_command("local", 1, Command::Damage(60))
        .unwrap();
    let before = store.characters[&1].progression.health;
    runtime
        .mend(
            1,
            0x123,
            1,
            Over {
                health: 5,
                vigor: 0,
                period_ms: 1000,
                duration_ms: 2000,
            },
            0,
            0,
        )
        .unwrap();
    for (now, healed) in [(0, 0), (1000, 5), (2000, 10), (3000, 10)] {
        runtime
            .tick(now, &mut scene, &mut store, [1].into_iter(), None)
            .unwrap();
        assert_eq!(store.characters[&1].progression.health, before + healed);
    }
    runtime
        .mend(
            1,
            0x123,
            1,
            Over {
                health: 5,
                vigor: 0,
                period_ms: 1000,
                duration_ms: 2000,
            },
            0,
            3000,
        )
        .unwrap();
    store
        .progression_command("local", 1, Command::Damage(u32::MAX))
        .unwrap();
    runtime
        .tick(4000, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert_eq!(store.characters[&1].progression.health, 0);
    assert!(runtime.mends.is_empty());
    drop(store);
    std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn an_expired_heal_never_catches_up_after_a_stall_but_instant_support_lands() {
    let (mut runtime, mut scene, mut store, path, _) = fixture("expiry");
    store
        .progression_command("local", 1, Command::Damage(60))
        .unwrap();
    let before = store.characters[&1].progression.health;
    runtime
        .mend(
            1,
            0x123,
            1,
            Over {
                health: 5,
                vigor: 0,
                period_ms: 1000,
                duration_ms: 2000,
            },
            0,
            0,
        )
        .unwrap();
    runtime
        .tick(3000, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert_eq!(store.characters[&1].progression.health, before);
    runtime
        .mend(
            1,
            0x124,
            1,
            Over {
                health: 25,
                vigor: 0,
                period_ms: 1000,
                duration_ms: 0,
            },
            0,
            3000,
        )
        .unwrap();
    runtime
        .tick(3100, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert_eq!(store.characters[&1].progression.health, before + 25);
    assert!(runtime.mends.is_empty());
    drop(store);
    std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn zero_cost_ranged_skill_releases_without_spending_vigor() {
    let (mut runtime, mut scene, mut store, path, target) = fixture("free");
    let before = store.characters[&1].progression.vigor;
    runtime
        .strike(1, target, 0, &scene, &store, Strike { cost: 0, ..bolt() })
        .unwrap();
    // The server obtains this category from the equipped implement.
    runtime.attacks.get_mut(&1).unwrap().category = 2;
    let events = runtime
        .tick(0, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert!(events.iter().any(|e| matches!(e, Event::SkillSwing { .. })));
    assert_eq!(
        runtime.actors[&target].health, 60,
        "No impact before release"
    );
    let events = runtime
        .tick(
            crate::projectiles::RELEASE_MS,
            &mut scene,
            &mut store,
            [1].into_iter(),
            None,
        )
        .unwrap();
    assert!(events.iter().any(|e| matches!(e, Event::Hit {victim, health_change, ..} if *victim == target && *health_change < 0)));
    assert_eq!(store.characters[&1].progression.vigor, before);
    drop(store);
    std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn zero_regeneration_does_not_submit_invalid_zero_healing() {
    let (mut runtime, mut scene, mut store, path, _) = fixture("zero-regen");
    runtime.actors.clear();
    runtime.next_regen = 0;
    store
        .progression_command("local", 1, Command::Damage(60))
        .unwrap();
    store
        .progression_command("local", 1, Command::SpendVigor(50))
        .unwrap();
    let before = store.characters[&1].progression.clone();
    runtime
        .boost_regen(
            1,
            0x123,
            Regen {
                health: 0.0,
                vigor: 0.0,
                duration_ms: 1000,
            },
            0,
        )
        .unwrap();
    runtime
        .tick(0, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert_eq!(store.characters[&1].progression, before);
    runtime
        .tick(
            runtime.regen_interval.max(1000),
            &mut scene,
            &mut store,
            [1].into_iter(),
            None,
        )
        .unwrap();
    assert!(store.characters[&1].progression.health > before.health);
    assert!(store.characters[&1].progression.vigor > before.vigor);
    drop(store);
    std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn recasting_a_single_tick_heal_refreshes_instead_of_stacking() {
    let (mut runtime, mut scene, mut store, path, _) = fixture("refresh");
    store
        .progression_command("local", 1, Command::Damage(60))
        .unwrap();
    let before = store.characters[&1].progression.health;
    let effect = Over {
        health: 5,
        vigor: 0,
        period_ms: 1000,
        duration_ms: 1000,
    };
    runtime.mend(1, 0x123, 1, effect, 0, 0).unwrap();
    runtime.mend(1, 0x123, 1, effect, 0, 500).unwrap();
    runtime
        .tick(1000, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert_eq!(store.characters[&1].progression.health, before);
    runtime
        .tick(1500, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert_eq!(store.characters[&1].progression.health, before + 5);
    assert!(runtime.mends.is_empty());
    drop(store);
    std::fs::remove_dir_all(path).unwrap();
}
