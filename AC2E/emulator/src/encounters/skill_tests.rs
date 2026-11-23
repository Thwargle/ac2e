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
    runtime.enable_loot();
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
        status_fx: Vec::new(),
        ratings: Vec::new(),
        skill: 0x59,
        bonus: 3,
        vigor_damage: 0,
        weapon_damage: true,
        cost: 32,
        range: 60.0,
        behavior: 0x410000b6,
        fx: 0x41000241,
        burn: vec![Over {
            effect: 0,
            health: 1,
            vigor: 0,
            period_ms: 1000,
            duration_ms: 30000,
        }],
    }
}

#[test]
fn shelter_kills_grant_only_needed_quest_items_and_otherwise_use_corpse_loot() {
    use crate::{quests::Command as Q, shelter};
    for (entity, phase, expected) in [
        (shelter::DIGGER, 0, None),
        (shelter::DIGGER, 3, Some(shelter::TOOL)),
        (shelter::DIGGER, 4, None),
        (shelter::SENTINEL, 0, None),
        (shelter::SENTINEL, 4, Some(shelter::GLYPH)),
    ] {
        let (mut runtime, mut scene, mut store, path, target) = fixture("shelter-loot");
        let mut book = store.characters[&1].quests.clone();
        for command in [Q::ShelterGuard, Q::ShelterLieutenant, Q::ShelterSentry, Q::ShelterTool]
            .into_iter().take(phase) {
            book = book.apply(&command).unwrap();
        }
        store.characters.get_mut(&1).unwrap().quests = book;
        let actor = runtime.actors.get_mut(&target).unwrap();
        actor.entity = entity;
        actor.health = 1;
        runtime.strike(1, target, 0, &scene, &store, bolt()).unwrap();
        let events = runtime.tick(0, &mut scene, &mut store, [1].into_iter(), None).unwrap();
        let granted = store.characters[&1].inventory.items().map(|item| item.definition).collect::<Vec<_>>();
        assert_eq!(granted, expected.into_iter().collect::<Vec<_>>(), "entity {entity:08x}, phase {phase}");
        assert_eq!(events.iter().filter(|e| matches!(e, Event::Loot { .. })).count(), usize::from(expected.is_none()));
        assert_eq!(events.iter().filter(|e| matches!(e, Event::Item { .. })).count(), usize::from(expected.is_some()));
        drop(store);
        std::fs::remove_dir_all(path).unwrap();
    }
}

#[test]
fn cadre_slayer_applies_to_the_weapon_strike_without_tripling_burn() {
    let (mut runtime,mut scene,mut store,path,target)=fixture("cadre-slayer");
    let actor=runtime.actors.get_mut(&target).unwrap();actor.entity=crate::esper_camp::CADRE;actor.health=100;
    let sword=crate::esper_camp::item(0x470015b7,crate::inventory::ITEM_NAMESPACE+1,0,1).unwrap();
    store.inventory_command("local",1,crate::inventory::Command::Grant(sword.clone())).unwrap();
    store.inventory_command("local",1,crate::inventory::Command::Wear{item:sword.id,slot:0x40}).unwrap();
    runtime.strike(1,target,0,&scene,&store,bolt()).unwrap();
    runtime.tick(0,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    assert_eq!(runtime.actors[&target].health,25,"(22 native weapon + 3 skill) times three");
    runtime.tick(1000,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    assert_eq!(runtime.actors[&target].health,24,"A later burn remains one damage");
    drop(store);std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn treasure_weapon_damage_drives_skill_hits_but_not_periodic_ticks() {
    let (mut runtime, mut scene, mut store, path, target) = fixture("treasure-weapon");
    let item = crate::inventory::Item {
        id: crate::inventory::ITEM_NAMESPACE + 1, definition: 0x47000f0b, visual: 0x1f0002a5,
        equipment_mask: 0x40, quantity: 1, stack_limit: 1, location: crate::inventory::Location::Bag(0),
        roll: Some(crate::inventory::Roll {damage:28,level:10,..Default::default()})
    };
    store.inventory_command("local",1,crate::inventory::Command::Grant(item.clone())).unwrap();
    store.inventory_command("local",1,crate::inventory::Command::Wear {item:item.id,slot:0x40}).unwrap();
    runtime.strike(1,target,0,&scene,&store,bolt()).unwrap();
    runtime.tick(0,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    assert_eq!(runtime.actors[&target].health,60,"The recovered staff casts before impact");
    let release=crate::projectiles::RELEASE_MS;
    let events=runtime.tick(release,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    let flight=events.iter().find_map(|e|match e {Event::Projectile{flight_ms,..}=>Some(*flight_ms),_=>None})
        .expect("The recovered staff fires Flame Bolt");
    runtime.tick(release+flight,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    assert_eq!(runtime.actors[&target].health,25,"28 weapon + 4 magic category + 3 Flame Bolt bonus");
    runtime.tick(release+flight+1000,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    assert_eq!(runtime.actors[&target].health,24,"burn cannot add the weapon a second time");
    drop(store);
    let reopened=Store::open(&path).unwrap();
    assert_eq!(crate::professions::weapon_damage(&reopened.characters[&1].inventory),Some(28));
    drop(reopened);
    std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn tumerok_javelin_waits_for_release_then_flight_before_damage() {
    let (mut runtime, mut scene, mut store, path, target) = fixture("javelin-release");
    store.characters.get_mut(&1).unwrap().creation=Some(world::CharacterCreationRequest {
        name:"Spell Tester".into(),entity_did:0,start_area:0,race:4,sex:4096,
        class:0,implement:0,physical_values:vec![],
    });
    let item=crate::inventory::Item {id:crate::inventory::ITEM_NAMESPACE+1,definition:0x47000f2e,
        visual:0x1f0002b3,equipment_mask:0x40,quantity:1,stack_limit:1,
        location:crate::inventory::Location::Bag(0),roll:None};
    assert!(crate::treasure_weapons::profile(&item).is_some());
    store.inventory_command("local",1,crate::inventory::Command::Grant(item.clone())).unwrap();
    store.inventory_command("local",1,crate::inventory::Command::Wear{item:item.id,slot:0x40}).unwrap();
    runtime.strike(1,target,0,&scene,&store,Strike{skill:0x315,behavior:0x4100007a,burn:Vec::new(),..bolt()}).unwrap();
    // Encounters run at 100 ms cadence; leave a full tick before release.
    for now in [0,350,670,1184] {
        let events=runtime.tick(now,&mut scene,&mut store,[1].into_iter(),None).unwrap();
        assert!(!events.iter().any(|e|matches!(e,Event::Projectile{..}|Event::Hit{..})),"premature release at {now}");
        assert_eq!(runtime.actors[&target].health,60);
    }
    assert_eq!(runtime.attacks[&1].next,1284);
    let events=runtime.tick(1284,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    let flight=events.iter().find_map(|e|match e {Event::Projectile{flight_ms,..}=>Some(*flight_ms),_=>None})
        .expect("Javelin released at native hook");
    assert_eq!(runtime.actors[&target].health,60);
    runtime.tick(1284+flight,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    assert!(runtime.actors[&target].health<60);
    drop(store);std::fs::remove_dir_all(path).unwrap();
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
fn vigor_drains_clamp_expire_and_restore_on_a_new_spawn_lifetime() {
    let (mut runtime, mut scene, mut store, path, target) = fixture("vigor");
    let actor = runtime.actors.get_mut(&target).unwrap();
    actor.rule.vigor = Some(100);
    actor.vigor = Some(100);
    let before = store.characters[&1].progression.clone();
    let effect = Strike { bonus: 0, vigor_damage: 40, weapon_damage: false,
        burn: vec![Over {effect: 0,health: 0, vigor: 35, period_ms: 1000, duration_ms: 2000}], ..bolt() };
    runtime.strike(1, target, 0, &scene, &store, effect).unwrap();
    for (now, expected) in [(0,60),(900,60),(1000,25),(2000,0),(3000,0)] {
        let events = runtime.tick(now, &mut scene, &mut store, [1].into_iter(), None).unwrap();
        assert_eq!(runtime.actors[&target].vigor, Some(expected));
        assert_eq!(runtime.actors[&target].health, 60);
        assert_eq!(store.characters[&1].progression.xp, before.xp);
        assert_eq!(store.characters[&1].progression.vigor, before.vigor-32);
        assert!(!events.iter().any(|e| matches!(e, Event::Loot{..}|Event::Hit{..})));
        if [0,1000,2000].contains(&now) {
            assert!(events.iter().any(|e| matches!(e,Event::Vigor{actor,vigor} if *actor==target && *vigor==expected)));
        }
    }
    assert!(runtime.burns.is_empty());
    assert!(runtime.inspection_stats().any(|(id,key,value)|id==target && key==0x103 && value==0));
    // Removing and publishing a new incarnation restores both authoritative vitals.
    assert!(runtime.spawners.died(target, 3000, 0));
    runtime.spawners.poll(3000);
    scene.despawn_content(target).unwrap(); runtime.removed(target,3000);
    assert_eq!(runtime.spawners.poll(33000),vec![crate::spawners::Action::Spawn(target)]);
    scene.respawn_content(target).unwrap(); runtime.spawned(target).unwrap();
    assert_eq!(runtime.actors[&target].vigor,Some(100));
    drop(store);
    let reopened=Store::open(&path).unwrap();
    assert_eq!(reopened.characters[&1].progression.vigor,before.vigor-32);
    drop(reopened); std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn missed_vigor_spell_charges_once_but_never_applies_or_ticks() {
    let (mut runtime, mut scene, mut store, path, target) = fixture("vigor-miss");
    // Choose a deterministic first roll above even the highest permitted hit chance.
    let seed=(1..1000).find(|seed|crate::combat::Dice::for_test(*seed).unit()>0.95).unwrap();
    runtime.dice=crate::combat::Dice::for_test(seed);
    let actor=runtime.actors.get_mut(&target).unwrap();actor.rule.vigor=Some(100);actor.vigor=Some(100);
    let before=store.characters[&1].progression.clone();
    runtime.strike(1,target,0,&scene,&store,Strike {vigor_damage:40,
        burn:vec![Over{effect: 0,health:1,vigor:10,period_ms:1000,duration_ms:2000}],..bolt()}).unwrap();
    let events=runtime.tick(0,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    assert!(!events.iter().any(|e|matches!(e,Event::Vigor{..}|Event::Hit{..})));
    assert_eq!(runtime.actors[&target].vigor,Some(100));assert_eq!(runtime.actors[&target].health,60);
    assert_eq!(store.characters[&1].progression.vigor,before.vigor-32);
    assert!(runtime.burns.is_empty());
    drop(store);std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn a_debuff_changes_ratings_without_inventing_weapon_damage_and_expires() {
    let (mut runtime, mut scene, mut store, path, target) = fixture("debuff");
    let effect = Strike {
        status_fx: vec![crate::status_fx::Effect{fx:0x41000031,duration_ms:1000}],
        bonus: 0,
        weapon_damage: false,
        burn: Vec::new(),
        ratings: vec![RatingEffect {
            ratings: crate::combat::Ratings {
                armor: -5,
                ..Default::default()
            },
            duration_ms: 1000, effect: 0,
        }],
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
    assert!(events.iter().any(|e|matches!(e,Event::Fx{target:who,fx:0x41000031} if *who==target)));
    let events=runtime
        .tick(1000, &mut scene, &mut store, [1].into_iter(), None)
        .unwrap();
    assert_eq!(runtime.ratings_for(target).armor, 0);
    assert!(events.iter().any(|e|matches!(e,Event::StopFx{target:who,fx:0x41000031} if *who==target)));
    runtime.apply_status_fx(1,0x123,&[(target,crate::status_fx::Effect{fx:0x41000031,duration_ms:5000})],1000).unwrap();
    runtime.actors.get_mut(&target).unwrap().health=0;
    let events=runtime.tick(1100,&scene,&mut store,[1].into_iter(),None).unwrap();
    assert!(events.iter().any(|e|matches!(e,Event::StopFx{target:who,fx:0x41000031} if *who==target)),"Death removes ongoing visuals before their duration expires");
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
                effect: 0,
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
                effect: 0,
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
fn delayed_updates_keep_periodic_vitals_on_the_original_schedule() {
    let (mut runtime, scene, mut store, path, target) = fixture("late-periodic");
    store.progression_batch("local", 1, &[Command::Damage(20), Command::SpendVigor(20)]).unwrap();
    let before = store.characters[&1].progression.clone();
    runtime.actors.get_mut(&target).unwrap().health = 100;
    runtime.actors.get_mut(&target).unwrap().vigor = Some(100);
    runtime.burns.push(Burn {
        skill: 0x59, source: 1, target, per_tick: 3, vigor_per_tick: 2,
        period: 700, next: 700, until: 2100,
    });
    runtime.mend(1, 0x91, 1, Over {
        effect: 0, health: 4, vigor: 3, period_ms: 700, duration_ms: 2100,
    }, 0, 0).unwrap();

    for (now, count) in [(699, 0), (900, 1), (1410, 2), (2240, 3), (2900, 3)] {
        let events = runtime.tick(now, &scene, &mut store, [1].into_iter(), None).unwrap();
        let state = &store.characters[&1].progression;
        assert_eq!((state.health, state.vigor), (before.health + 4 * count, before.vigor + 3 * count));
        assert_eq!(runtime.actors[&target].health, 100 - 3 * count);
        assert_eq!(runtime.actors[&target].vigor, Some(100 - 2 * count));
        if now == 2240 {
            assert!(events.iter().any(|e| matches!(e, Event::Hit { victim: 1, health_change: 4, skill: Some(0x91), .. })));
        }
    }
    assert!(runtime.burns.is_empty() && runtime.mends.is_empty());
    drop(store);
    std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn compound_healing_keeps_distinct_timers_and_rejects_overflow_atomically() {
    let (mut runtime, scene, mut store, path, _) = fixture("compound-heal");
    store.progression_batch("local", 1, &[Command::Damage(60), Command::SpendVigor(60)]).unwrap();
    let before = store.characters[&1].progression.clone();
    let effects = [
        Over { effect: 0, health: 5, vigor: 0, period_ms: 1000, duration_ms: 2000 },
        Over { effect: 0, health: 0, vigor: 7, period_ms: 2000, duration_ms: 6000 },
    ];
    runtime.mend_batch(1, 0x123, 1, &effects, 0, 0).unwrap();
    for (now, health, vigor) in [(0,0,0), (1000,5,0), (2000,10,7), (3000,10,7), (4000,10,14), (6000,10,21), (7000,10,21)] {
        runtime.tick(now, &scene, &mut store, [1].into_iter(), None).unwrap();
        assert_eq!(store.characters[&1].progression.health, before.health + health, "health at {now}");
        assert_eq!(store.characters[&1].progression.vigor, before.vigor + vigor, "vigor at {now}");
    }
    runtime.mend_batch(1, 0x123, 1, &effects, 0, 8000).unwrap();
    runtime.mend_batch(1, 0x123, 1, &effects, 0, 8500).unwrap();
    assert_eq!(runtime.mends.len(), 2, "recast replaces the bundle instead of duplicating components");
    assert_eq!(runtime.mends[0].next, 9500);
    assert_eq!(runtime.mends[1].until, 14500);
    let mut unrelated = runtime.mends[0].clone(); unrelated.skill = 0x456;
    runtime.mends = vec![unrelated; MAX_TIMED-1];
    runtime.mend(1, 0x123, 1, effects[0], 0, 9000).unwrap();
    let old_next = runtime.mends.last().unwrap().next;
    assert!(runtime.mend_batch(1, 0x123, 1, &effects, 0, 9500).is_err());
    assert_eq!(runtime.mends.len(), MAX_TIMED);
    assert_eq!(runtime.mends.last().unwrap().next, old_next, "rejected refresh retains previous effect");
    drop(store); std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn compound_damage_keeps_distinct_timers_and_reserves_pending_components() {
    let (mut runtime, scene, mut store, path, target) = fixture("compound-burn");
    runtime.actors.get_mut(&target).unwrap().vigor = Some(100);
    let effects = vec![
        Over { effect: 0, health: 5, vigor: 0, period_ms: 1000, duration_ms: 2000 },
        Over { effect: 0, health: 0, vigor: 7, period_ms: 2000, duration_ms: 6000 },
    ];
    runtime.strike(1, target, 0, &scene, &store, Strike { bonus: 0, weapon_damage: false, cost: 0, burn: effects.clone(), ..bolt() }).unwrap();
    for (now, health, vigor) in [(0,60,100), (1000,55,100), (2000,50,93), (3000,50,93), (4000,50,86), (6000,50,79), (7000,50,79)] {
        runtime.tick(now, &scene, &mut store, [1].into_iter(), None).unwrap();
        assert_eq!(runtime.actors[&target].health, health, "health at {now}");
        assert_eq!(runtime.actors[&target].vigor, Some(vigor), "vigor at {now}");
    }
    runtime.strike(1, target, 8000, &scene, &store, Strike { bonus: 0, weapon_damage: false, cost: 0, burn: effects, ..bolt() }).unwrap();
    runtime.burns = vec![Burn { skill: 0x456, source: 1, target, per_tick: 1, vigor_per_tick: 0, period: 1000, next: 10000, until: 20000 }; MAX_TIMED-3];
    assert!(runtime.admit_effects(0, 1, 0, 0).is_ok());
    assert!(runtime.admit_effects(0, 2, 0, 0).is_err(), "queued strike reserves both native timers");
    drop(store); std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn compound_ratings_expire_individually_and_refresh_as_a_bundle() {
    let (mut runtime, scene, mut store, path, _) = fixture("rating-bundle");
    let armor = crate::combat::Ratings { armor: 5, ..Default::default() };
    let attack = crate::combat::Ratings { attack: [7, 0, 0], ..Default::default() };
    let effects = [RatingEffect { ratings: armor, duration_ms: 500, effect: 0x70000021 }, RatingEffect { ratings: attack, duration_ms: 2000, effect: 0x70000022 }];
    runtime.add_ratings(1, 0x59, &effects, 0).unwrap();
    assert_eq!(runtime.player_effects(1, 0).len(), 2);
    assert!(runtime.player_effects(2, 0).is_empty());
    runtime.tick(500, &scene, &mut store, [1].into_iter(), None).unwrap();
    let visible = runtime.player_effects(1, 500);
    assert_eq!(visible.len(), 1);
    assert_eq!(visible[&(0, 0x59, 0x70000022)].until, 2000);
    assert_eq!(runtime.ratings_for(1).armor, 0);
    assert_eq!(runtime.ratings_for(1).attack[0], 7);
    runtime.add_ratings(1, 0x59, &effects, 600).unwrap();
    assert_eq!(runtime.player_effects(1, 600)[&(0, 0x59, 0x70000022)].start, 600);
    assert_eq!(runtime.ratings_for(1).armor, 5);
    assert_eq!(runtime.ratings_for(1).attack[0], 7, "refresh never doubles the surviving component");
    runtime.tick(1100, &scene, &mut store, [1].into_iter(), None).unwrap();
    assert_eq!(runtime.ratings_for(1).armor, 0);
    runtime.tick(2500, &scene, &mut store, [1].into_iter(), None).unwrap();
    assert_eq!(runtime.ratings_for(1).attack[0], 7);
    runtime.tick(2600, &scene, &mut store, [1].into_iter(), None).unwrap();
    assert!(runtime.player_effects(1, 2600).is_empty());
    assert!(runtime.ratings_for(1).is_empty());
    drop(store); std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn effect_capacity_counts_queued_components_and_failed_replacement_is_atomic() {
    let (mut runtime, scene, store, path, target) = fixture("rating-capacity");
    let rating = crate::combat::Ratings { armor: 1, ..Default::default() };
    let effect = RatingEffect { ratings: rating, duration_ms: 1000, effect: 0x70000021 };
    runtime.ratings = (0..MAX_TIMED-2).map(|id| (1, id as u32, effect, 10000)).collect();
    runtime.strike(1, target, 0, &scene, &store, Strike { ratings: vec![effect; 2], ..bolt() }).unwrap();
    assert!(runtime.admit_effects(1, 0, 0, 0).is_err(), "pending two-part debuff reserves two slots");
    assert!(runtime.admit_effects(0, 0, 0, 0).is_ok());
    let before = runtime.ratings.clone();
    assert!(runtime.add_ratings(1, 0, &vec![effect; 4], 0).is_err());
    assert_eq!(runtime.ratings, before, "capacity rejection keeps the old bundle intact");
    drop(store); std::fs::remove_dir_all(path).unwrap();
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
                effect: 0,
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
                effect: 0,
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
    assert!(events.iter().any(|e| matches!(e, Event::Fx {target:1,fx:0x410004a9})));
    let flight = events.iter().find_map(|e| match e {
        Event::Projectile {profile,flight_ms,..} => {assert_eq!(profile.entity,0x47000d04);Some(*flight_ms)},
        _=>None,
    }).expect("Flame Bolt releases its native missile");
    assert!(!events.iter().any(|e|matches!(e,Event::Hit{..})),"Release is not impact");
    let events=runtime.tick(crate::projectiles::RELEASE_MS+flight,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    assert!(events.iter().any(|e| matches!(e, Event::Hit {victim, health_change, ..} if *victim == target && *health_change < 0)));
    assert_eq!(events.iter().filter(|e|matches!(e,Event::Fx{..})).count(),1,"One authored target impact");
    assert!(events.iter().any(|e|matches!(e,Event::Fx{fx:0x41000241,..})));
    let burn=runtime.tick(crate::projectiles::RELEASE_MS+flight+1000,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    assert!(burn.iter().any(|e|matches!(e,Event::Hit{health_change:-1,..})));
    assert!(!burn.iter().any(|e|matches!(e,Event::Fx{..}|Event::Projectile{..}|Event::SkillSwing{..})),"A burn tick cannot recast a spell");
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
                effect: 0,
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
fn regeneration_components_expire_refresh_and_fail_atomically() {
    let (mut runtime,mut scene,mut store,path,_)=fixture("regen-bundle");
    runtime.actors.clear();runtime.next_regen=0;
    store.progression_command("local",1,Command::Damage(100)).unwrap();
    store.progression_command("local",1,Command::SpendVigor(100)).unwrap();
    let before=store.characters[&1].progression.clone();
    let effects=[Regen {effect: 0,health:0.0,vigor:1.0,duration_ms:1000},Regen {effect: 0,health:1.0,vigor:0.0,duration_ms:10000}];
    runtime.boost_regen_batch(1,0x59,&effects,0).unwrap();
    runtime.tick(0,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    assert_eq!(store.characters[&1].progression,before);
    runtime.next_regen=1001;
    runtime.tick(1001,&mut scene,&mut store,[1].into_iter(),None).unwrap();
    let after=&store.characters[&1].progression;
    assert!(after.health>before.health,"health multiplier expires independently");
    assert_eq!(after.vigor,before.vigor,"vigor multiplier remains active");
    assert_eq!(runtime.regen.len(),1);
    runtime.boost_regen_batch(1,0x59,&effects,2000).unwrap();
    assert_eq!(runtime.regen.len(),2,"refresh replaces, rather than stacks, the bundle");
    let snapshot=runtime.regen.clone();
    let invalid=[effects[0],Regen {health:f32::NAN,..effects[1]}];
    assert!(runtime.boost_regen_batch(1,0x59,&invalid,3000).is_err());
    assert_eq!(runtime.regen,snapshot,"invalid second component cannot erase a live buff");
    for n in 0..MAX_TIMED-runtime.regen.len() {
        runtime.regen.insert((100+n as u64,0,0),(effects[0],50000));
    }
    assert!(runtime.admit_effects(0,0,0,0).is_ok(),"full regeneration capacity does not block unrelated attacks");
    assert!(runtime.admit_effects(0,0,0,1).is_err());
    assert!(runtime.boost_regen_batch(1,0x59,&effects,3000).is_ok(),"a full service can replace an existing equal-sized bundle");
    let snapshot=runtime.regen.clone();
    assert!(runtime.boost_regen_batch(2,0x59,&effects,3000).is_err());
    assert_eq!(runtime.regen,snapshot);
    drop(store);std::fs::remove_dir_all(path).unwrap();
}

#[test]
fn recasting_a_single_tick_heal_refreshes_instead_of_stacking() {
    let (mut runtime, mut scene, mut store, path, _) = fixture("refresh");
    store
        .progression_command("local", 1, Command::Damage(60))
        .unwrap();
    let before = store.characters[&1].progression.health;
    let effect = Over {
        effect: 0,
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

#[test]
fn fellowship_xp_is_local_opt_in_and_replays_with_the_kill() {
    use crate::{groups, quests::Command as Quest};
    for (shared, quest_drop) in [(false, false), (true, false), (true, true)] {
        let (mut runtime, mut scene, mut store, path, target) = fixture("fellowship-xp");
        let mut online = BTreeMap::new();
        for (id, name, offset) in [
            (1, "Spell Tester", 0.0),
            (2, "Nearby Member", 2.0),
            (3, "Distant Member", 60.0),
            (4, "Nearby Outsider", 2.0),
            (5, "Fallen Member", 2.0),
        ] {
            let mut position = Placement::HUMAN_START;
            position.coordinates[1] += offset;
            if id != 1 {
                store.create("local", name).unwrap();
                let bytes = world::create_stationary_object(&world::StationaryObject {
                    character_id: id,
                    parent_visual_did: 1,
                    entity_did: 2,
                    cell_id: position.cell,
                    position: position.coordinates,
                    orientation: position.orientation(),
                    instance_stamp: 1,
                    visual_ordering_stamp: 0,
                    timestamps: [0; 4],
                }).unwrap();
                scene.enter(Body::new(id, position, bytes).unwrap()).unwrap();
            }
            let p = &store.characters[&id].progression;
            online.insert(id, groups::Member {
                id, name: name.into(), level: u32::from(p.level().number),
                health: p.health, max_health: p.max_health(),
                vigor: p.vigor, max_vigor: p.max_vigor(),
                cell: position.cell,
            });
        }
        assert!(scene.combat_distance(2, target).unwrap() < groups::XP_SHARE_RANGE);
        assert!(scene.combat_distance(3, target).unwrap() > groups::XP_SHARE_RANGE);
        let mut fellows = groups::Runtime::default();
        let plan = fellows.prepare(1, &groups::Request::Create {
            name: "Quest Party".into(), looting: 0, social: !shared,
        }, 0, &online).unwrap();
        fellows.commit(plan).unwrap();
        for id in [2, 3, 5] {
            let plan = fellows.prepare(1, &groups::Request::Invite(id), 1, &online).unwrap();
            fellows.commit(plan).unwrap();
            let plan = fellows.prepare(id, &groups::Request::Accept(1), 2, &online).unwrap();
            fellows.commit(plan).unwrap();
        }
        let health = store.characters[&5].progression.health;
        store.progression_command("local", 5, Command::Damage(health)).unwrap();

        if quest_drop {
            for command in [Quest::ShelterGuard, Quest::ShelterLieutenant, Quest::ShelterSentry] {
                store.quest_command("local", 1, command).unwrap();
            }
        }
        let actor = runtime.actors.get_mut(&target).unwrap();
        actor.entity = crate::shelter::DIGGER;
        actor.rule.xp = 11;
        actor.rule.gold = 3;
        actor.health = 1;
        runtime.strike(1, target, 0, &scene, &store, bolt()).unwrap();
        let events = runtime.tick_with_fellowships(
            0, &scene, &mut store, [1, 2, 3, 4, 5].into_iter(), None, Some(&fellows)
        ).unwrap();

        let member_xp = if shared { 5 } else { 0 };
        assert_eq!(store.characters[&1].progression.xp, 1011 - member_xp);
        assert_eq!(store.characters[&1].progression.gold, 3);
        assert_eq!(store.characters[&2].progression.xp, member_xp);
        for id in 2..=5 {
            assert_eq!(store.characters[&id].progression.gold, 0);
            assert_eq!(store.characters[&id].quests, crate::quests::Book::default());
        }
        for id in [3, 4, 5] {
            assert_eq!(store.characters[&id].progression.xp, 0);
        }
        assert_eq!(events.iter().filter(|event| matches!(event, Event::Owner { player: 2, .. })).count(), usize::from(shared));
        assert_eq!(store.characters[&1].inventory.items().any(|item| item.definition == crate::shelter::TOOL), quest_drop);
        let expected = store.characters.clone();
        drop(store);
        let replayed = Store::open(&path).unwrap();
        for id in 1..=5 {
            assert_eq!(replayed.characters[&id].progression, expected[&id].progression);
            assert_eq!(replayed.characters[&id].quests, expected[&id].quests);
            assert_eq!(replayed.characters[&id].inventory, expected[&id].inventory);
        }
        drop(replayed);
        std::fs::remove_dir_all(path).unwrap();
    }
}
