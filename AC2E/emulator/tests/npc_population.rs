//! Native assets, placement safety, and NPC combat protection for wiki recovery.
use pyrealium::{content::Content, encounters::Encounters, geometry::Geometry, npc_dialogue};
use std::{collections::BTreeSet, path::Path};

#[test]
fn nearby_quest_contacts_and_vendors_have_distinct_identities() {
    use pyrealium::{exploration_quests, hunting_quests, services::Service};

    let Ok(data) = std::env::var("AC2_TEST_GAME_DATA") else { return };
    let manifest = Path::new(env!("CARGO_MANIFEST_DIR")).join("../data/world/legions-world.tsv");
    let world = Content::load(&manifest, Path::new(&data)).unwrap();
    let mut actors = Vec::new();
    for actor in world.objects() {
        let entity = actor.entity;
        let name = if let Some(profile) = npc_dialogue::profiles().get(&entity) {
            profile.name.clone()
        } else if hunting_quests::supported(entity) {
            hunting_quests::speaker(entity).to_owned()
        } else if exploration_quests::supported(entity) {
            exploration_quests::speaker(entity).to_owned()
        } else if actor.service == Some(Service::Vendor) {
            actor.name.clone().unwrap_or_else(|| format!("{entity:08x}"))
        } else {
            continue;
        };
        actors.push((name.to_lowercase(), actor));
    }

    // Both Warder sites are objectives in separate Cragstone guides. Permit only
    // those two instances so a third placement of either template is detected.
    let warders = [0x81408a8a7c0efd61, 0x8140b56f6779f75b];
    let mut nearby = Vec::new();
    for (index, (name, first)) in actors.iter().enumerate() {
        for (other_name, second) in &actors[index + 1..] {
            if first.entity != second.entity && name != other_name { continue; }
            let Some(distance) = first.placement.distance(second.placement) else { continue };
            if distance < 200.0 && !(warders.contains(&first.id) && warders.contains(&second.id)) {
                nearby.push(format!("{name}: {:016x} and {:016x} ({distance:.1} m)", first.id, second.id));
            }
        }
    }
    assert!(nearby.is_empty(), "Review nearby NPC placements:\n{}", nearby.join("\n"));
    let present: BTreeSet<_> = world.objects().map(|o| o.id).collect();
    for id in warders.into_iter().chain([
        // Four distinct native treasurers occur in the same observed room.
        0x814088afb5994c0c, 0x8140d0647f99af01, 0x8140de8a2c5e6190, 0x8140e6a90eb368fa,
        0x8140c5735893c03b, 0x8140c4ff5b636ce9, 0x8140944ef4ef9534, 0x8140893e5bc59585,
    ]) {
        assert!(present.contains(&id), "Missing reviewed contact {id:016x}");
    }
    assert_eq!(world.objects().filter(|o| o.entity == 0x470013b6).count(), 3);
    for (id, north, east) in [
        (0x8140e7000000000c, 42.2, 12.9),
        (0x8140d30047003fa7, -32.9, 52.3),
        (0x8140d20047003fa9, -32.8, 52.4),
    ] {
        let p = world.objects().find(|o| o.id == id).unwrap().placement;
        let x = (p.cell >> 24) as f64 * 160.0 + f64::from(p.coordinates[0]);
        let y = ((p.cell >> 16) & 255) as f64 * 160.0 + f64::from(p.coordinates[1]);
        let to_meters = |coordinate: f64| (100.0 + coordinate) * 204.0;
        assert!((x - to_meters(east)).hypot(y - to_meters(north)) < 10.0,
                "Contact {id:016x} moved away from its documented site");
    }
}

#[test]
fn guide_landmarks_and_zu_tracker_are_reachable_noncombat_interactions() {
    let Ok(data) = std::env::var("AC2_TEST_GAME_DATA") else { return };
    let data = Path::new(&data);
    let pack = Path::new(env!("CARGO_MANIFEST_DIR")).join("content/ac2hero-settlements-v1.tsv");
    let content = Content::load(&pack, data).unwrap();
    let encounters = Encounters::load(Path::new("world"), &content).unwrap();
    let mut geometry = Geometry::open(data).unwrap();
    for (entity, north, east, topic) in [
        (0x47001c01, 16.1, 73.3, "Shreth-Hunter's Guide to Omishan"),
        (0x4700230a, 32.4, 66.1, "Temple Search"),
        (0x4700230c, 8.5, 65.8, "Temple Search"),
        (0x47002312, 3.2, 85.6, "Temple Search"),
        (0x47000855, -3.0, -37.9, "Icefire"),
    ] {
        let mut matching = content.objects().filter(|o| o.entity == entity);
        let actor = matching.next().expect("missing guide landmark");
        assert!(matching.next().is_none(), "duplicate {entity:08x}");
        assert!(!encounters.is_creature(actor.id), "{entity:08x} is a quest contact");
        assert!(encounters.spawners().policy(actor.id).is_none());
        assert!(npc_dialogue::profiles()[&entity].topics.iter().any(|t| t.title == topic));
        let p = actor.placement;
        assert!(geometry.dry_ground(p) && geometry.contact(p));
        let x = (p.cell >> 24) as f64 * 160. + f64::from(p.coordinates[0]);
        let y = ((p.cell >> 16) & 255) as f64 * 160. + f64::from(p.coordinates[1]);
        let meters = |coord: f64| (1020. + coord * 10.2) * 20.;
        assert!((x - meters(east)).hypot(y - meters(north)) < 10., "{entity:08x} moved outside its guide location");
    }
}

#[test]
fn wiki_npcs_are_grounded_unique_and_cannot_be_killed_for_loot() {
    let Ok(data)=std::env::var("AC2_TEST_GAME_DATA") else {return};
    let root=Path::new(env!("CARGO_MANIFEST_DIR")).join("content");
    let world=Content::load(&root.join("../../data/world/legions-world.tsv"),Path::new(&data)).unwrap();
    let content=Content::load(&root.join("npc-world-v1.tsv"),Path::new(&data)).unwrap();
    let source:serde_json::Value=serde_json::from_str(include_str!("../content/npc-dialogue-v1.json")).unwrap();
    let encounters=Encounters::load(Path::new("world"),&world).unwrap();
    let mut geometry=Geometry::open(Path::new(&data)).unwrap();
    assert_eq!(content.objects().count(),27);
    let mut identities=BTreeSet::new();
    for npc in content.objects() {
        assert!(identities.insert(npc.entity));
        assert!(npc_dialogue::profiles().contains_key(&npc.entity));
        assert!(!encounters.is_creature(npc.id));
        assert!(encounters.spawners().policy(npc.id).is_none());
        assert!(geometry.contact(npc.placement) && geometry.dry_ground(npc.placement));
        assert_eq!(world.objects().filter(|o|o.entity==npc.entity).count(),1,"Duplicate {:?}",npc.name);
        assert!(world.objects().filter(|o|o.id!=npc.id).all(|o|
            npc.placement.distance(o.placement).is_none_or(|d|d>=5.999)));
        let anchor=source["npcs"].as_array().unwrap().iter().find(|n|n["id"].as_u64()==Some(npc.id)).unwrap();
        let p=npc.placement;
        let x=(p.cell>>24) as f64*160.+p.coordinates[0] as f64;
        let y=((p.cell>>16)&255) as f64*160.+p.coordinates[1] as f64;
        assert!((x-anchor["land"][0].as_f64().unwrap()*20.).hypot(y-anchor["land"][1].as_f64().unwrap()*20.)<=32.001);
    }
    // Every catalog identity is checked against the DAT combat classifier,
    // including variants withheld from the permanent world (spirits/events).
    let path=std::env::temp_dir().join(format!("ac2-npc-identities-{}.tsv",std::process::id()));
    let mut rows=String::from("pyrealium-world-v4\n");
    for (i,p) in source["profiles"].as_array().unwrap().iter().enumerate() {
        let entity=p["entity"].as_u64().unwrap();
        let x=20.*(1+(i/60)) as f32+1.;
        let cell=0x77b80000+((x/20.) as u32*8)+1;
        rows+=&format!("{:016x}\t{entity:08x}\t{:08x}\t{cell:08x}\t{x}\t1\t180\t1\t0\t0\t0\t-\t{}\n",
            0x8140d50000000000u64+entity,p["visual"].as_u64().unwrap(),p["name"].as_str().unwrap());
    }
    // Encounters requires at least one combat actor. Include an independently
    // classified production creature as the positive control.
    let monster=world.objects().find(|o|encounters.is_creature(o.id)).unwrap();
    rows+=&format!("{:016x}\t{:08x}\t{:08x}\t{:08x}\t{}\t{}\t{}\t1\t0\t0\t0\t-\tControl\n",
        monster.id,monster.entity,monster.visual,monster.placement.cell,
        monster.placement.coordinates[0],monster.placement.coordinates[1],monster.placement.coordinates[2]);
    std::fs::write(&path,rows).unwrap();
    let catalog=Content::load(&path,Path::new(&data)).unwrap();
    let runtime=Encounters::load(Path::new("world"),&catalog).unwrap();
    assert!(runtime.is_creature(monster.id));
    for npc in catalog.objects().filter(|o|o.id!=monster.id) {
        assert!(!runtime.is_creature(npc.id),"Conversation on hostile {:?}",npc.name);
    }
    std::fs::remove_file(path).unwrap();
}
