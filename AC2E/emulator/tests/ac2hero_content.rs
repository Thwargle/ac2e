use pyrealium::{content::Content, dat::Dat, encounters::Encounters, geometry::Geometry, hunting_quests, inventory_request::Metadata, recovered_quests};
use std::{collections::BTreeSet, path::Path};

#[test]
fn raid_and_crypt_placements_load_as_individual_encounters_at_native_floor_height() {
    let Ok(data)=std::env::var("AC2_TEST_GAME_DATA") else {return};
    let data=Path::new(&data);
    let root=Path::new(env!("CARGO_MANIFEST_DIR")).join("content");
    let mut geometry=Geometry::open(data).unwrap();
    for (filename,count) in [("ac2hero-raid-world-v1.tsv",27),("fiery-crypt-map-v1.tsv",13)] {
        let content=Content::load(&root.join(filename),data).unwrap();
        let encounters=Encounters::load(Path::new("world"),&content).unwrap();
        assert_eq!(content.objects().count(),count);
        assert_eq!(encounters.spawners().group_count(),count);
        for object in content.objects() {
            assert!(encounters.is_creature(object.id),"{:?} is missing its combat profile",object.name);
            assert!(geometry.contact(object.placement),"{:?} does not have floor contact",object.name);
        }
        let placed=content.objects().map(|o|o.entity).collect::<BTreeSet<_>>();
        if count==27 {
            assert_eq!(placed.len(),27);
            for entity in placed {
                assert!(recovered_quests::profiles().iter().any(|q|q.kill_starters.contains(&entity)));
            }
        } else {
            let sneer=recovered_quests::profiles().iter().find(|q|q.title=="Kill Sneer").unwrap();
            assert!(sneer.targets.iter().any(|e|placed.contains(e)));
        }
    }
}

#[test]
fn geledite_rewards_resolve_native_item_and_visual_records() {
    let Ok(data)=std::env::var("AC2_TEST_GAME_DATA") else {return};
    let mut portal=Dat::open(Path::new(&data).join("portal.dat")).unwrap();
    let mut seen=BTreeSet::new();
    for key in hunting_quests::profiles().iter().filter_map(|q|q.key.as_ref()) {
        assert!(seen.insert(key.item));
        Metadata::load(&mut portal,key.item).unwrap();
        assert!(!portal.read(key.visual).unwrap().is_empty());
    }
    assert_eq!(seen,BTreeSet::from([1191198721,1191198733]));
}

#[test]
fn every_kill_started_hunt_can_be_bestowed_and_completed_using_its_bound_creatures() {
    for quest in recovered_quests::profiles().iter().filter(|q| !q.kill_starters.is_empty()) {
        for &starter in &quest.kill_starters {
            let (mut book, _) = recovered_quests::Book::default().kill(starter, quest.level, 1);
            assert_eq!(book.0[&quest.id].count, 0, "starter counted towards {}", quest.title);
            for i in 0..quest.goal {
                let target = quest.targets[usize::from(i) % quest.targets.len()];
                book = book.kill(target, quest.level, 2 + u64::from(i)).0;
                assert_eq!(book.0[&quest.id].count, i + 1, "{}", quest.title);
            }
            let completed = &book.0[&quest.id];
            assert!(completed.completed > 0 && completed.rewarded_xp > 0, "{}", quest.title);
            let duplicate = book.kill(quest.targets[0], quest.level, completed.completed + 1).0;
            assert_eq!(&duplicate.0[&quest.id], completed, "{}", quest.title);
        }
    }
}

#[test]
fn all_npc_hunt_chains_offer_and_settle_every_link_through_their_speakers() {
    // A single eligible character visits the actual speakers. This catches
    // chains whose next link is unreachable even though its own policy is valid.
    use pyrealium::progression::{Command, Progression};
    let mut player = Progression::default();
    while player.level().number < 150 {
        player = player.apply(&Command::AwardXp(player.level().xp_cap + 1 - player.xp)).unwrap();
    }
    let mut book = hunting_quests::Book::default();
    let profiles = hunting_quests::profiles();
    let speakers: BTreeSet<_> = profiles.iter().map(|q| q.npc).collect();
    let mut now = 1;
    let mut completed = BTreeSet::new();
    loop {
        let before = completed.len();
        for &speaker in &speakers {
            let Ok(offer) = book.converse(speaker, &player, now) else { continue };
            book = offer.book;
            let Some(quest) = profiles.iter().find(|q| q.npc == speaker
                && book.0.get(&q.id).is_some_and(|entry| entry.completed == 0)) else { continue };
            assert!(!completed.contains(&quest.id), "unexpected repeat of {}", quest.title);
            for i in 0..quest.goal {
                now += 1;
                book = book.kill(quest.targets[usize::from(i) % quest.targets.len()], now);
            }
            now += 1;
            let claim = book.converse(speaker, &player, now).unwrap();
            assert_eq!(claim.xp, quest.reward(&player), "{}", quest.title);
            assert_eq!(claim.key.map(|k| k.item), quest.key.as_ref().map(|k| k.item));
            assert!(claim.book.has_completed(quest.id), "{}", quest.title);
            completed.insert(quest.id);
            book = claim.book;
        }
        if completed.len() == before { break; }
    }
    let missing: Vec<_> = profiles.iter().filter(|q| !completed.contains(&q.id)).map(|q| &q.title).collect();
    assert!(missing.is_empty(), "unreachable hunt turn-ins: {missing:?}");
}

#[test]
fn arwic_and_hakata_hunts_have_living_targets_and_usable_speakers_in_the_prepared_world() {
    let Ok(data)=std::env::var("AC2_TEST_GAME_DATA") else {return};
    let source=Path::new(env!("CARGO_MANIFEST_DIR")).join("../data/world/legions-world.tsv");
    let world=Content::load(&source,Path::new(&data)).unwrap();
    let encounters=Encounters::load(Path::new("world"),&world).unwrap();
    for id in (0x4000fc2b..=0x4000fc2e).chain([0x4000fc32]) {
        let q=hunting_quests::profile(id).unwrap();
        assert!(world.objects().any(|o|o.entity==q.npc && !encounters.is_creature(o.id)), "{}",q.npc_name);
        assert!(world.objects().any(|o|q.targets.contains(&o.entity) && encounters.is_creature(o.id)), "{}",q.title);
        assert!(!pyrealium::exploration_quests::supported(q.npc));
        assert!(!recovered_quests::delivery_npc(q.npc));
    }
}

#[test]
fn guide_forts_and_hunting_grounds_are_dry_combat_spawns_with_native_loot_profiles() {
    let Ok(data) = std::env::var("AC2_TEST_GAME_DATA") else { return };
    let data = Path::new(&data);
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("content");
    let content = Content::load(&root.join("hero-wilderness-camps-v2.tsv"), data).unwrap();
    let world = Content::load(&root.join("../../data/world/legions-world.tsv"), data).unwrap();
    let encounters = Encounters::load(Path::new("world"), &content).unwrap();
    let world_encounters = Encounters::load(Path::new("world"), &world).unwrap();
    let profiles = pyrealium::creature_ai::catalog().unwrap();
    let mut geometry = Geometry::open(data).unwrap();
    let base = 0x8140dd0000000000;
    let camps: &[(u64, &[u32])] = &[
        (15, &[0x47001759, 0x47001758, 0x4700175a, 0x47000f93]),
        (16, &[0x4700175c, 0x4700175b, 0x4700175d, 0x47000f94]),
        (17, &[0x47001762, 0x47001761, 0x47001763, 0x47000f95]),
        (18, &[0x47001765, 0x47001764, 0x47001766, 0x47000f92]),
        (19, &[0x4700104e]), (20, &[0x4700104e]), (21, &[0x4700104e]),
        (22, &[0x470016c2]), (23, &[0x4700102d, 0x4700102d]),
        (24, &[0x47001f52, 0x47001c53]), (25, &[0x47001fae]),
    ];
    assert_eq!(encounters.spawners().group_count(), 25);
    for &(group, templates) in camps {
        for (index, &entity) in templates.iter().enumerate() {
            let id = base + (group << 8) + index as u64;
            let object = content.object(id).unwrap();
            assert_eq!(object.entity, entity);
            assert!(encounters.is_creature(id) && encounters.spawners().owns(id));
            assert_eq!(world.object(id).unwrap().placement, object.placement);
            assert!(world_encounters.is_creature(id) && world_encounters.spawners().owns(id));
            assert_ne!(profiles[&entity].loot_profile, 0, "no native loot profile for {entity:x}");
            assert!(geometry.contact(object.placement) && geometry.dry_ground(object.placement),
                    "invalid outdoor floor for {id:x}");
        }
    }
}

#[test]
fn captured_quest_encounters_are_managed_combatants_in_the_prepared_world() {
    let Ok(data) = std::env::var("AC2_TEST_GAME_DATA") else { return };
    let data = Path::new(&data);
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let content = Content::load(&root.join("content/quest-encounters-v1.tsv"), data).unwrap();
    let world = Content::load(&root.join("../data/world/legions-world.tsv"), data).unwrap();
    let encounters = Encounters::load(Path::new("world"), &world).unwrap();
    let base = 0x8140e70000000000;
    let actors = [
        (0x470016bf, 0xb88001db), (0x47000896, 0xb8800198), (0x47000896, 0xb880019b),
        (0x47000896, 0xb880019d), (0x47000896, 0xb88001da), (0x470016c1, 0xb2960033),
        (0x47001610, 0x74a80016), (0x47001795, 0x07f7018a), (0x4700179a, 0x07fa016a),
        (0x47001798, 0x5a5e0035), (0x47001797, 0x0afc0137),
    ];
    for (index, (entity, cell)) in actors.into_iter().enumerate() {
        let id = base + index as u64;
        let object = world.object(id).unwrap();
        assert_eq!(object.entity, entity);
        assert_eq!(object.placement.cell, cell);
        assert_eq!(object.placement, content.object(id).unwrap().placement);
        assert!(encounters.is_creature(id) && encounters.spawners().owns(id));
    }
    for id in [0x400000f3, 0x4000010e, 0x40000113] {
        let quest = recovered_quests::profiles().iter().find(|q| q.id == id).unwrap();
        for candidates in [&quest.kill_starters, &quest.targets] {
            assert!(world.objects().any(|o| candidates.contains(&o.entity) && encounters.is_creature(o.id)),
                    "{} lacks a placed combatant in {candidates:x?}", quest.title);
        }
    }
}
