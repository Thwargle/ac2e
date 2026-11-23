//! Server-authoritative development encounters. Balance rules remain authored.
//! This module does not claim original combat balance, collision or animation parity.
use crate::{
    content::Content,
    movement::Placement,
    progression::{Command, Progression},
    scene::Scene,
    store::Store,
};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs::File,
    io::{self, Read},
    path::Path,
};
fn invalid(text: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, text)
}

#[cfg(test)]
mod skill_tests;

#[cfg(test)]
mod locomotion_tests {
    use super::*;
    #[test]
    fn different_speeds_preserve_collision_steps_and_attack_standoff() {
        let from=Placement::HUMAN_START;
        let goal=from.translated([10.,0.,0.]).unwrap();
        for speed in [3.9,6.5,7.8] {
            let mut route=Vec::new();
            let next=advance(from,goal,2.5,speed*0.1,&mut route,|a,b| {
                assert!(a.distance(b).unwrap()<0.201);Some(b)
            }).unwrap();
            assert!((from.distance(next).unwrap()-speed*0.1).abs()<0.0001);
            let stop=advance(from,goal,2.5,20.,&mut Vec::new(),|_,b|Some(b)).unwrap();
            assert!((stop.distance(goal).unwrap()-2.5).abs()<0.0001);
        }
        // A wall encountered partway through a fast tick cannot be skipped.
        let limited=advance(from,goal,0.,2.,&mut Vec::new(),|_,b|
            (b.coordinates[0]<=from.coordinates[0]+0.41).then_some(b)).unwrap();
        assert!((from.distance(limited).unwrap()-0.4).abs()<0.0001);
    }
    #[test]
    fn return_spends_time_budget_across_waypoints_without_cutting_corners() {
        let home=Placement::HUMAN_START;
        let mut route=Vec::new();
        let corner=advance(home,home.translated([1.,0.,0.]).unwrap(),0.,1.,&mut route,|_,b|Some(b)).unwrap();
        let end=advance(corner,corner.translated([0.,1.,0.]).unwrap(),0.,1.,&mut route,|_,b|Some(b)).unwrap();
        let next=retreat(end,1.5,&mut route,|a,b| {
            let [x,y,_]=a.offset_to(b).unwrap();
            assert!(x.abs()<0.001 || y.abs()<0.001,"Must follow recorded right-angle path");
            assert!(a.distance(b).unwrap()<0.201);Some(b)
        }).unwrap();
        let [x,y,_]=home.offset_to(next).unwrap();assert!((x-0.5).abs()<0.001 && y.abs()<0.001);
        let back=retreat(next,1.5,&mut route,|_,b|Some(b)).unwrap();
        assert!(home.distance(back).unwrap()<0.001);assert!(route.is_empty());
    }
}

#[cfg(test)]
mod spawner_tests {
    use super::*;
    #[test]
    fn ac2hero_bestiary_combat_uses_each_native_variant() {
        let id = crate::content::NAMESPACE + 7;
        let content = Content::fixture(id, Placement::HUMAN_START, None);
        let encounters = Encounters::parse(include_str!("../content/development-encounters.tsv"), &content).unwrap();
        for (entity, level, health, damage, perception, loot) in [
            (0x47004319, 34, 1650, 181, 30., 2000), // Cunning Tom
            (0x4700431e, 41, 2150, 300, 40., 2025), // Iridescent Stinger
            (0x47003af6, 35, 1300, 169, 40., 2025), // Iridescent Worker
            (0x47003a3c, 48, 21600, 505, 30., 1990), // Knorr Banderling Berserker
        ] {
            let (rule, ai, _) = encounters.bestiary.ruled(entity, id).unwrap();
            assert_eq!((rule.level, rule.health, rule.vigor, rule.damage), (Some(level), health, Some(health), damage));
            assert_eq!((ai.perception, ai.homesick, ai.wander_range, ai.loot_profile), (perception, Some(60.), 20., loot));
            assert!(ai.aggressive() && ai.wanders() && !ai.loot_proof);
            assert!(!encounters.bestiary.protected.contains(&entity));
        }
    }
    #[test]
    fn respawn_waits_for_a_ready_player_then_publishes_once_at_the_same_home() {
        let Ok(dir)=std::env::var("AC2_TEST_GAME_DATA") else{return};
        let id=crate::content::NAMESPACE+7;let home=Placement::HUMAN_START;
        let content=Content::fixture(id,home,None);
        let mut runtime=Encounters::parse(include_str!("../content/development-encounters.tsv"),&content).unwrap();
        runtime.load_motion(&content,Path::new(&dir)).unwrap();
        let mut scene=Scene::with_content(content);runtime.initialize_spawners(&mut scene,0).unwrap();
        let path=std::env::temp_dir().join(format!("ac2-spawn-player-{}",std::process::id()));
        let mut store=Store::open(&path).unwrap();let player=store.create("local","Spawn Tester").unwrap().id;
        let body=|p:Placement|{
            let bytes=crate::world::create_stationary_object(&crate::world::StationaryObject {
                character_id:player,parent_visual_did:1,entity_did:2,cell_id:p.cell,
                position:p.coordinates,orientation:p.orientation(),instance_stamp:1,
                visual_ordering_stamp:0,timestamps:[0;4]}).unwrap();
            crate::scene::Body::new(player,p,bytes).unwrap()
        };
        scene.enter(body(home)).unwrap();
        let births=|events:Vec<Event>|events.into_iter().filter_map(|e|match e{Event::Respawn{actor,..}=>Some(actor),_=>None}).collect::<Vec<_>>();
        for now in [0,1000,2000] {
            assert!(births(runtime.tick(now,&scene,&mut store,[player].into_iter(),None).unwrap()).is_empty());
            assert_eq!(runtime.spawners.live(),0,"A delayed spawn does not consume capacity");
        }
        scene.remove(player);scene.enter(body(home.translated([5.,0.,0.]).unwrap())).unwrap();
        assert_eq!(births(runtime.tick(3000,&scene,&mut store,[player].into_iter(),None).unwrap()),vec![id]);
        scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();
        assert_eq!(scene.content_placement(id),Some(home));
        assert!(births(runtime.tick(4000,&scene,&mut store,[player].into_iter(),None).unwrap()).is_empty());
        drop(store);std::fs::remove_dir_all(path).unwrap();
    }
    #[test]
    fn spawns_wait_for_occupied_anchors_and_reserve_same_tick_bodies() {
        let first=crate::content::NAMESPACE+7;let second=first+1;
        let home=Placement::HUMAN_START;
        let mut content=Content::fixture(first,home,None);
        let other=Content::fixture(second,home,None);
        content.insert_live(other.object(second).unwrap().clone(),true).unwrap();
        let mut runtime=Encounters::parse(include_str!("../content/development-encounters.tsv"),&content).unwrap();
        let mut scene=Scene::with_content(content);runtime.initialize_spawners(&mut scene,0).unwrap();
        let path=std::env::temp_dir().join(format!("ac2-spawn-clearance-{}-{}",std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
        let mut store=Store::open(&path).unwrap();
        let births=|events:Vec<Event>|events.into_iter().filter_map(|e|match e{Event::Respawn{actor,..}=>Some(actor),_=>None}).collect::<Vec<_>>();
        assert_eq!(births(runtime.tick(0,&scene,&mut store,std::iter::empty(),None).unwrap()),vec![first],
            "The second pending child must see the first reservation before publication");
        scene.respawn_content(first).unwrap();runtime.spawned(first).unwrap();
        assert!(births(runtime.tick(1000,&scene,&mut store,std::iter::empty(),None).unwrap()).is_empty(),
            "A live neighbour continues to block the anchor without consuming its waiting child");
        scene.move_creature(first,home.translated([3.,0.,0.]).unwrap(),1500).unwrap();
        assert_eq!(births(runtime.tick(2000,&scene,&mut store,std::iter::empty(),None).unwrap()),vec![second]);
        scene.respawn_content(second).unwrap();runtime.spawned(second).unwrap();
        assert_eq!(scene.content_placement(second),Some(home),"The authored home never moves");
        assert_eq!(runtime.spawners.live(),2);
        assert!(births(runtime.tick(3000,&scene,&mut store,std::iter::empty(),None).unwrap()).is_empty());
        drop(store);std::fs::remove_dir_all(path).unwrap();
    }
    #[test]
    fn academy_idols_are_passive_level_one_without_changing_outdoor_idols() {
        let Ok(path)=std::env::var("AC2_TEST_GAME_DATA") else{return};
        let practice = include_str!("../tests/fixtures/human-practice-world.tsv").lines().skip(1).collect::<Vec<_>>().join("\n");
        let pack = format!("{}{}", include_str!("../content/training-world.tsv"), practice);
        let content=Content::parse_text(&pack,Path::new(&path)).unwrap();
        let mut runtime=Encounters::parse(include_str!("../content/development-encounters.tsv"),&content).unwrap();
        let ids=runtime.actors.iter().filter(|(_,a)|a.entity==crate::quests::TARGET_ENTITY).map(|(&id,_)|id).collect::<Vec<_>>();
        assert_eq!(ids.len(),12);
        let mut scene=Scene::with_content(content);runtime.initialize_spawners(&mut scene,0).unwrap();
        for action in runtime.spawners.poll(0) {
            let crate::spawners::Action::Spawn(id)=action else{panic!()};
            scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();
        }
        for id in ids {
            assert!(academy_idol(id));
            let a=&runtime.actors[&id];
            assert_eq!((a.rule.level,a.health,a.vigor),(Some(1),60,Some(60)));
            assert!(!a.ai.aggressive());
            runtime.spawners.died(id,0,60000);
            assert!(runtime.spawners.poll(60000).contains(&crate::spawners::Action::Remove(id)));
            scene.despawn_content(id).unwrap();runtime.spawners.removed(id,60000);
            assert!(runtime.spawners.poll(60000).contains(&crate::spawners::Action::Spawn(id)));
            scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();
            assert!(!runtime.actors[&id].ai.aggressive(),"Respawn must retain passive behavior");
        }
        let (_,outside,_)=runtime.bestiary.ruled(crate::quests::TARGET_ENTITY,0x8140c00000000001).unwrap();
        assert!(outside.aggressive(),"The same template outside the academy retains native aggression");
    }
    #[test]
    fn notebook_bandit_respawns_without_making_quest_contacts_hostile() {
        let Ok(path)=std::env::var("AC2_TEST_GAME_DATA") else{return};
        let data:serde_json::Value=serde_json::from_str(include_str!("../content/warder-notebook-v1.json")).unwrap();
        let rows=data["placements"].as_array().unwrap().iter().map(|p|p["row"].as_str().unwrap()).collect::<Vec<_>>().join("\n");
        let content=Content::parse_text(&format!("pyrealium-world-v4\n{rows}\n"),Path::new(&path)).unwrap();
        let mut runtime=Encounters::parse(include_str!("../content/development-encounters.tsv"),&content).unwrap();
        assert_eq!(runtime.actors.len(),1,"Scout and Captain must not become attackable creatures");
        let (&id,actor)=runtime.actors.iter().next().unwrap();
        assert_eq!((actor.entity,actor.rule.level,actor.health,actor.vigor),(crate::warder_notebook::BANDIT,Some(12),350,Some(350)));
        assert_eq!((actor.rule.damage,actor.rule.armor,actor.rule.xp),(56,8,31));
        assert_eq!((actor.ai.perception,actor.ai.loot_profile),(30.,949));
        assert!(actor.ai.aggressive());
        let home=actor.home;
        let mut scene=Scene::with_content(content);runtime.initialize_spawners(&mut scene,0).unwrap();
        assert_eq!(runtime.spawners.poll(0),vec![crate::spawners::Action::Spawn(id)]);
        scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();
        assert_eq!(scene.content_placement(id),Some(home));
        assert!(runtime.spawners.died(id,10,60000));
        assert!(runtime.spawners.poll(30010).is_empty(),"A live corpse occupies its spawn slot");
        assert_eq!(runtime.spawners.poll(60010),vec![crate::spawners::Action::Remove(id)]);
        scene.despawn_content(id).unwrap();runtime.spawners.removed(id,60010);
        assert_eq!(runtime.spawners.poll(60010),vec![crate::spawners::Action::Spawn(id)]);
        scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();
        assert_eq!(scene.content_placement(id),Some(home));
    }
    #[test]
    fn captured_esper_camp_has_three_native_respawning_cadres() {
        let Ok(path)=std::env::var("AC2_TEST_GAME_DATA") else{return};
        let data:serde_json::Value=serde_json::from_str(include_str!("../content/esper-camp-v1.json")).unwrap();
        let rows=data["placements"].as_array().unwrap().iter().map(|r|r.as_array().unwrap().iter().map(|v|v.as_str().unwrap()).collect::<Vec<_>>().join("\t")).collect::<Vec<_>>().join("\n");
        let content=Content::parse_text(&format!("pyrealium-world-v4\n{rows}\n"),Path::new(&path)).unwrap();
        let mut runtime=Encounters::parse(include_str!("../content/development-encounters.tsv"),&content).unwrap();
        assert_eq!(runtime.actors.len(),3);
        for actor in runtime.actors.values() {
            assert_eq!((actor.entity,actor.rule.level,actor.health,actor.vigor),(crate::esper_camp::CADRE,Some(10),285,Some(285)));
            assert_eq!((actor.ai.perception,actor.ai.homesick,actor.ai.loot_profile),(30.,Some(60.),954));
            assert!(actor.ai.aggressive());
        }
        let mut scene=Scene::with_content(content);runtime.initialize_spawners(&mut scene,0).unwrap();
        let actions=runtime.spawners.poll(0);assert_eq!(actions.len(),3);
        for action in actions {let crate::spawners::Action::Spawn(id)=action else{panic!()};scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();}
        let id=*runtime.actors.keys().next().unwrap();assert!(runtime.spawners.died(id,10,60000));
        assert!(runtime.spawners.poll(30010).is_empty());
        assert_eq!(runtime.spawners.poll(60010),vec![crate::spawners::Action::Remove(id)]);runtime.spawners.removed(id,60010);
        assert_eq!(runtime.spawners.poll(60010),vec![crate::spawners::Action::Spawn(id)]);
    }
    #[test]
    fn cragstone_excavation_captured_population_and_portal_landings_fit_legions() {
        let Some(directory) = std::env::var_os("AC2_TEST_GAME_DATA").map(std::path::PathBuf::from) else { return };
        let content = Content::parse_text(include_str!("../content/cragstone-excavation-v1.tsv"), &directory).unwrap();
        let mut encounters = Encounters::parse(include_str!("../content/development-encounters.tsv"), &content).unwrap();
        let mut terrain = crate::geometry::Geometry::open(&directory).unwrap();
        let mut dat = crate::dat::Dat::open(directory.join("portal.dat")).unwrap();
        let mut motions = crate::creature_motion::Catalog::default();
        let mut roster = std::collections::BTreeMap::new();
        for (&id, actor) in &encounters.actors {
            *roster.entry(actor.entity).or_insert(0) += 1;
            let obj = content.object(id).unwrap();
            let body = motions.resolve(&mut dat, obj.visual, obj.scale).unwrap().unwrap();
            let floor = terrain.ground(obj.placement, 0.5, 3.).unwrap();
            assert_eq!(floor.cell, obj.placement.cell);
            assert!(terrain.body_fits(floor, body.radius, body.height));
            assert!(encounters.spawners.owns(id));
            if body.grounded { assert!(floor.distance(obj.placement).unwrap() < 0.05); }
            if actor.entity == 0x4700160e {
                assert_eq!((actor.rule.level, actor.health, actor.ai.loot_profile), (Some(14), 1100, 965));
            }
        }
        assert_eq!(roster, [(0x47000fb3,2), (0x47000fc0,5), (0x470015bb,14), (0x470015bc,8),
            (0x470015d3,14), (0x4700160e,1)].into_iter().collect());
        let routes = crate::portals::Routes::bundled(&content, &directory).unwrap();
        assert_eq!(routes.len(), 3);
        for (id, cell) in [(0x8140d98000000100,0x76a80025), (0x8140d98000000101,0x78a5001e),
                           (0x8140d98000000102,0x05f90101)] {
            let route = routes.get(id).unwrap();
            assert!(!encounters.spawners.owns(id));
            assert_eq!((route.destination.cell, route.level), (cell, 1));
            let floor = terrain.ground(route.destination, 1., 2.).unwrap();
            assert_eq!(floor.cell, cell);
            assert!(route.destination.distance(floor).unwrap() < 1.);
            assert!(terrain.contact(floor) && terrain.body_fits(floor, 0.35, 1.8));
        }
        let mut scene = Scene::with_content(content);
        encounters.initialize_spawners(&mut scene, 0).unwrap();
        assert_eq!(encounters.spawners.group_count(), 26);
        assert_eq!(encounters.spawners.poll(0).len(), 44);
    }
    #[test]
    fn ossuary_portals_land_on_walkable_floors_and_recurring_revenants_spawn_once() {
        let Some(directory) = std::env::var_os("AC2_TEST_GAME_DATA").map(std::path::PathBuf::from) else { return };
        let content = Content::parse_text(include_str!("../content/the-ossuary-v1.tsv"), &directory).unwrap();
        let mut encounters = Encounters::parse(include_str!("../content/development-encounters.tsv"), &content).unwrap();
        let mut geometry = crate::geometry::Geometry::open(&directory).unwrap();
        let routes = crate::portals::Routes::bundled(&content, &directory).unwrap();
        assert_eq!(routes.len(), 2);
        for (id, cell, heading) in [(0x8140f707fb000100, 0x07fb0100, 130), (0x8140f707fb000101, 0x53790026, 43)] {
            let route = routes.get(id).unwrap();
            assert_eq!((route.destination.cell, route.destination.heading), (cell, heading));
            let floor = geometry.ground(route.destination, 1., 2.).expect("Ossuary arrival needs a native floor");
            assert_eq!(floor.cell, cell);
            assert!(route.destination.distance(floor).unwrap() < 1.);
            assert!(geometry.contact(floor) && geometry.body_fits(floor, 0.35, 1.8));
            assert!(!encounters.spawners.owns(id));
        }
        let mut roster = BTreeMap::new();
        for (&id, actor) in &encounters.actors {
            let expected_treasure = match actor.entity {
                0x4700082a => 780, 0x4700105c | 0x4700179f => 774, 0x4700179e => 1160,
                _ => panic!("unexpected Ossuary creature"),
            };
            assert_eq!(actor.ai.loot_profile, expected_treasure);
            if actor.entity == 0x4700179e {
                assert_eq!((actor.rule.level, actor.health, actor.vigor), (Some(46), 19000, Some(19000)));
                assert!(actor.ai.aggressive());
            }
            let home=content.object(id).unwrap().placement;
            if actor.entity==0x4700179f {
                assert!((1..=64).contains(&(home.cell&65535)));
                assert_eq!((actor.rule.level,actor.health),(Some(45),2850));
                let body=actor.movement;
                assert!(geometry.dry_ground(home) && geometry.body_fits(home,body.radius,body.height));
                let entrance=content.object(0x8140f707fb000100).unwrap().placement;
                assert!(home.distance(entrance).is_some_and(|d|d<200.));
                for source in [include_str!("../content/regional-world-v1.tsv"),include_str!("../content/wilderness-world-v1.tsv")] {
                    for line in source.lines().filter(|s|s.starts_with("8140")) {
                        let f=line.split('\t').collect::<Vec<_>>();
                        let other=Placement{cell:u32::from_str_radix(f[3],16).unwrap(),coordinates:[f[4].parse().unwrap(),f[5].parse().unwrap(),f[6].parse().unwrap()],heading:0};
                        assert!(!home.offset_to(other).is_some_and(|[x,y,_]|x.hypot(y)<80.),"regional camp too close: {}",f[0]);
                    }
                }
            } else {assert_eq!(home.cell>>16,0x07fb);}
            assert!(encounters.spawners.owns(id));
            *roster.entry(actor.entity).or_insert(0) += 1;
        }
        assert_eq!(roster, [(0x4700082a, 6), (0x4700105c, 10), (0x4700179e, 1), (0x4700179f,3)].into_iter().collect());
        let mut scene = Scene::with_content(content);
        encounters.initialize_spawners(&mut scene, 0).unwrap();
        assert_eq!(encounters.spawners.group_count(), 17);
        let actions=encounters.spawners.poll(0);
        assert_eq!(actions.len(),18);
        let mut visible=Vec::new();
        for action in actions {
            let crate::spawners::Action::Spawn(id)=action else {panic!()};
            scene.respawn_content(id).unwrap();
            encounters.spawned(id).unwrap();
            visible.push(encounters.actors[&id].entity);
        }
        let starter=*visible.iter().find(|&&e|e==0x4700179f).unwrap();
        let boss=*visible.iter().find(|&&e|e==0x4700179e).unwrap();
        assert_eq!(visible.iter().filter(|&&e|e==starter).count(),1);
        let book=crate::recovered_quests::Book::default();
        assert_eq!(book.kill(starter,41,100).0,book);
        let (book,start_xp)=book.kill(starter,42,100);
        assert_eq!((start_xp,book.0[&0x40000114].started),(0,100));
        assert_eq!(book.kill(boss,42,100+7*86400).1,0);
        let (book,completion_xp)=book.kill(boss,42,110);
        assert_eq!((completion_xp,book.0[&0x40000114].completed),(103000,110));
        assert_eq!(book.kill(boss,42,111).1,0);
    }
    #[test]
    fn warder_hideout_spawns_lashers_and_uses_both_surface_exits() {
        let Some(directory) = std::env::var_os("AC2_TEST_GAME_DATA").map(std::path::PathBuf::from) else { return };
        let content = Content::parse_text(include_str!("../content/warder-hideout-v1.tsv"), &directory).unwrap();
        let mut encounters = Encounters::parse(include_str!("../content/development-encounters.tsv"), &content).unwrap();
        assert_eq!(encounters.actors.len(), 5);
        assert_eq!(encounters.spawners.group_count(), 4);
        let mut terrain = crate::geometry::Geometry::open(&directory).unwrap();
        let mut dat = crate::dat::Dat::open(directory.join("portal.dat")).unwrap();
        let mut motions = crate::creature_motion::Catalog::default();
        for (&id, actor) in &encounters.actors {
            let obj = content.object(id).unwrap();
            assert!(matches!(actor.entity, 0x4700107e | 0x4700107f | 0x47001089));
            assert!(encounters.spawners.owns(id));
            let body = motions.resolve(&mut dat, obj.visual, obj.scale).unwrap().unwrap();
            let floor = terrain.ground(obj.placement, 0.5, 1.0).unwrap();
            assert_eq!(floor.cell, obj.placement.cell);
            assert!(terrain.body_fits(floor, body.radius, body.height));
        }
        let portals = crate::portals::Routes::bundled(&content, &directory).unwrap();
        assert_eq!(portals.len(), 3);
        for (id, arrival) in [(0x8140d97000000100, 0x91990017), (0x8140d97000000101, 0x91990017),
                               (0x8140d97000000102, 0x07f30105)] {
            let route = portals.get(id).unwrap();
            assert_eq!((route.destination.cell, route.level), (arrival, 1));
            assert!(!encounters.spawners.owns(id));
            let floor = terrain.ground(route.destination, 1.0, 2.0).unwrap();
            assert_eq!(floor.cell, arrival);
            assert!(route.destination.distance(floor).unwrap() < 1.0);
            assert!(terrain.contact(floor) && terrain.body_fits(floor, 0.35, 1.8));
        }
        let entrance = content.object(0x8140d97000000102).unwrap().placement;
        assert!(entrance.distance(portals.get(0x8140d97000000100).unwrap().destination).unwrap() > 4.0);
        let mut scene = Scene::with_content(content);
        encounters.initialize_spawners(&mut scene, 0).unwrap();
        assert_eq!(encounters.spawners.poll(0).len(), 5);
    }
    #[test]
    fn illuminated_cave_capture_spawns_have_walkable_floors_and_a_living_boss() {
        let Some(directory)=std::env::var_os("AC2_TEST_GAME_DATA").map(std::path::PathBuf::from) else{return};
        let content=Content::parse_text(include_str!("../content/illuminated-cave-v1.tsv"),&directory).unwrap();
        let mut game=Encounters::parse(include_str!("../content/development-encounters.tsv"),&content).unwrap();
        let boss=game.actors.get(&0x8140d70000000000).unwrap();
        assert_eq!((boss.entity,boss.rule.level,boss.health,boss.ai.loot_profile),(0x470015f2,Some(21),745,469));
        let mut terrain=crate::geometry::Geometry::open(&directory).unwrap();
        let mut dat=crate::dat::Dat::open(directory.join("portal.dat")).unwrap();
        let mut motions=crate::creature_motion::Catalog::default();
        for o in content.objects() {
            let p=o.placement;
            let m=motions.resolve(&mut dat,o.visual,o.scale).unwrap().unwrap();
            let feet=terrain.ground(p,0.5,3.0).unwrap();
            assert_eq!(feet.cell,p.cell);
            assert!(terrain.body_fits(feet,m.radius,m.height));
            if m.grounded {assert!(p.distance(feet).unwrap()<0.05);}
            else {assert!(terrain.clear(feet,p,0.05));assert!(p.coordinates[2]>feet.coordinates[2]);}
        }
        assert_eq!(game.actors.len(),17);
        assert_eq!(game.spawners.group_count(),10);
        let mut scene=Scene::with_content(content);
        game.initialize_spawners(&mut scene,0).unwrap();
        assert_eq!(game.spawners.poll(0).len(),17);
    }
    #[test]
    fn foundry_generals_use_singleton_spawners_on_native_walkable_ground() {
        let Some(directory)=std::env::var_os("AC2_TEST_GAME_DATA").map(std::path::PathBuf::from) else{return};
        let content=Content::parse_text(include_str!("../content/foundry-generals-v1.tsv"),&directory).unwrap();
        let mut game=Encounters::parse(include_str!("../content/development-encounters.tsv"),&content).unwrap();
        let mut terrain=crate::geometry::Geometry::open(&directory).unwrap();
        let mut dat=crate::dat::Dat::open(directory.join("portal.dat")).unwrap();
        let mut motions=crate::creature_motion::Catalog::default();
        let expected=[(0x4700390b,56,37500),(0x47003bca,58,44500),(0x47003b72,60,47500),(0x47003b73,62,52500)];
        let world=std::env::var_os("AC2_TEST_WORLD_CONTENT").map(|file|Content::load(std::path::Path::new(&file),&directory).unwrap());
        for (index,(entity,level,health)) in expected.into_iter().enumerate() {
            let id=0x8140ed0000010000 + index as u64 * 16;
            let actor=&game.actors[&id];
            assert_eq!((actor.entity,actor.rule.level,actor.health),(entity,Some(level),health));
        }
        for object in content.objects() {
            let p=object.placement;
            let feet=terrain.ground(p,0.5,1.).unwrap();
            let shape=motions.resolve(&mut dat,object.visual,object.scale).unwrap().unwrap();
            assert!(p.distance(feet).unwrap()<0.05 && terrain.body_fits(feet,shape.radius,shape.height));
            if let Some(world)=&world {
                assert!(world.objects().any(|other|other.id==object.id && other.placement==p));
                assert!(world.objects().filter(|other|other.id!=object.id)
                    .all(|other|p.distance(other.placement).is_none_or(|meters|meters>=6.)));
            }
        }
        let mut scene=Scene::with_content(content);
        game.initialize_spawners(&mut scene,0).unwrap();
        assert_eq!(game.spawners.group_count(),4);
        let spawns=game.spawners.poll(0);
        assert_eq!(spawns.len(),4);
        for action in spawns {
            let crate::spawners::Action::Spawn(id)=action else{panic!()};
            scene.respawn_content(id).unwrap();
            game.spawned(id).unwrap();
        }
        let id=0x8140ed0000010000;
        assert!(game.spawners.died(id,0,60000));
        assert_eq!(game.spawners.poll(60000),vec![crate::spawners::Action::Remove(id)]);
        game.spawners.removed(id,60000);
        assert!(game.spawners.poll(119999).is_empty());
        assert_eq!(game.spawners.poll(120000),vec![crate::spawners::Action::Spawn(id)]);
    }
    #[test]
    fn arwic_room_rosters_use_native_stats_and_refill_only_after_corpse_removal() {
        let Ok(path)=std::env::var("AC2_TEST_GAME_DATA") else{return};
        let content=Content::parse_text(include_str!("../content/arwic-mines-world-v1.tsv"),Path::new(&path)).unwrap();
        let mut runtime=Encounters::parse(include_str!("../content/development-encounters.tsv"),&content).unwrap();
        assert_eq!(runtime.actors.len(),6);assert_eq!(runtime.spawners.group_count(),2);
        assert_eq!(runtime.actors.values().filter(|a|a.entity==crate::arwic_mines::OUTCAST).count(),4);
        for actor in runtime.actors.values() {
            assert_eq!((actor.rule.level,actor.health),if actor.entity==crate::arwic_mines::OUTCAST {(Some(6),164)}else{(Some(3),59)});
            assert!(actor.ai.aggressive());
            if actor.entity==crate::arwic_mines::OUTCAST {
                assert_eq!((actor.rule.damage,actor.rule.armor,actor.rule.xp),(28,4,19));
                assert_eq!((actor.ai.perception,actor.ai.homesick,actor.ai.loot_profile),(30.,Some(60.),948));
                assert!(!actor.ai.wanders(),"No wandering radius was recovered for this template");
            }
        }
        let mut scene=Scene::with_content(content);runtime.initialize_spawners(&mut scene,0).unwrap();
        let actions=runtime.spawners.poll(0);assert_eq!(actions.len(),6);
        for action in actions {let crate::spawners::Action::Spawn(id)=action else{panic!()};scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();}
        let id=*runtime.actors.keys().next().unwrap();
        assert!(runtime.spawners.died(id,10,60000));
        assert!(runtime.spawners.poll(30010).is_empty(),"A corpse still occupies its child slot");
        assert_eq!(runtime.spawners.poll(60010),vec![crate::spawners::Action::Remove(id)]);
        runtime.spawners.removed(id,60010);
        assert_eq!(runtime.spawners.poll(60010),vec![crate::spawners::Action::Spawn(id)]);
        runtime.spawners.spawned(id);assert!(runtime.spawners.poll(999999).is_empty());
    }
    #[test]
    fn policy_overrides_initial_delay_without_moving_the_observed_anchor() {
        let id=crate::content::NAMESPACE+7;let home=Placement::HUMAN_START;
        let content=Content::fixture(id,home,None);
        let text=format!("{}\nspawner\t{:08x}\t2000\t5000\n",
            include_str!("../content/development-encounters.tsv"),crate::quests::TARGET_ENTITY);
        let mut runtime=Encounters::parse(&text,&content).unwrap();
        let mut scene=Scene::with_content(content);
        runtime.initialize_spawners(&mut scene,100).unwrap();
        assert!(scene.content_placement(id).is_none());
        assert!(runtime.spawners.poll(2099).is_empty());
        assert_eq!(runtime.spawners.poll(2100),vec![crate::spawners::Action::Spawn(id)]);
        scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();
        assert_eq!(scene.content_placement(id),Some(home));
        let mut moved=home;moved.coordinates[0]+=0.5;
        scene.move_creature(id,moved,2200).unwrap();
        assert!(runtime.spawners.poll(10000).is_empty(),"Pursuit is not a vacant population slot");
        scene.despawn_content(id).unwrap();runtime.removed(id,10000);
        assert!(runtime.spawners.poll(14999).is_empty());
        assert_eq!(runtime.spawners.poll(15000),vec![crate::spawners::Action::Spawn(id)]);
        scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();
        assert_eq!(scene.content_placement(id),Some(home));
        assert!(runtime.spawners.poll(100000).is_empty());
    }
    #[test]
    fn world_rules_override_stats_respawn_and_plan_patrol_legs() {
        let id=crate::content::NAMESPACE+7;let home=Placement::HUMAN_START;
        let content=Content::fixture(id,home,None);
        let rules=crate::world_rules::Rules::parse(&format!("{}\nstats\t{id:016x}\t999\t-\t-\t30\t-\t-\t-\t-\t-\t-\n\
            group\t5\t1\t4000\t{id:016x}\npatrol\t{id:016x}\t0\t3,0\n",crate::world_rules::HEADER)).unwrap();
        let mut runtime=Encounters::parse_with_rules(include_str!("../content/development-encounters.tsv"),&content,&rules).unwrap();
        assert_eq!(runtime.initial_health().collect::<Vec<_>>(),vec![(id,999)]);
        assert!(runtime.inspection_stats().any(|row|row==(id,0x12e,30)));
        let mut scene=Scene::with_content(content);
        runtime.initialize_spawners(&mut scene,0).unwrap();
        assert_eq!(runtime.spawners.poll(0),vec![crate::spawners::Action::Spawn(id)]);
        scene.respawn_content(id).unwrap();runtime.spawned(id).unwrap();
        scene.despawn_content(id).unwrap();runtime.removed(id,1000);
        assert!(runtime.spawners.poll(4999).is_empty());
        assert_eq!(runtime.spawners.poll(5000),vec![crate::spawners::Action::Spawn(id)],"group respawn wins");
        let actor=&runtime.actors[&id];
        assert_eq!(actor.patrol.as_ref().unwrap().waypoints,vec![[3.,0.]]);
    }
    #[test]
    fn policy_cannot_make_a_fixed_service_attackable_or_create_an_unplaced_creature() {
        let content=Content::fixture(crate::content::NAMESPACE+7,Placement::HUMAN_START,None);
        let text=include_str!("../content/development-encounters.tsv");
        for row in ["spawner\t47000001\t0\t1000", "spawner\t470007d1\t0\t0",
            "spawner\t470007d1\t86400001\t1000",
            "spawner\t470007d1\t0\t1000\nspawner\t470007d1\t0\t2000"] {
            assert!(Encounters::parse(&format!("{text}\n{row}\n"),&content).is_err());
        }
        let service=Content::fixture(crate::content::NAMESPACE+8,Placement::HUMAN_START,Some(crate::services::Service::Vendor));
        assert!(Encounters::parse(text,&service).is_err());
    }
}
#[derive(Clone, Copy, PartialEq)]
struct Rule {
    health: u32,
    level: Option<u32>,
    vigor: Option<u32>,
    damage: u32,
    armor: u32,
    interval: u64,
    range: f32,
    respawn: u64,
    xp: u64,
    gold: u32,
    leash: f32,
}
#[derive(Clone)]
struct Actor {
    entity: u32,
    name: Option<String>,
    home: Placement,
    pursuit: bool,
    movement: crate::creature_motion::Profile,
    velocity: f32,
    // Verified steps retraced on disengagement, without inventing a path home.
    route: Vec<Placement>,
    rule: Rule,
    health: u32,
    vigor: Option<u32>,
    target: Option<u64>,
    next_attack: u64,
    ai: crate::creature_ai::Profile,
    next_idle: u64,
    wander_goal: Option<Placement>,
    evading: bool,
    random: u64,
    /// Operator patrol route. Patrol legs are not recorded in `route`; only a
    /// pursuit is retraced, back to the patrol point where it began.
    patrol: Option<crate::world_rules::Patrol>,
    patrol_next: usize,
    patrol_wait: u64,
    /// Patrolling creatures leash around the point where they engaged.
    engaged_at: Option<Placement>,
}
#[derive(Clone)]
struct Attack {
    target: u64,
    next: u64,
    repeat: bool,
    category: usize,
    stage: u8,
    started: u64,
    skill: bool,
    strike: Option<Strike>,
    periodic: bool,
    splash: bool,
}
/// One skill hit: weapon damage plus `bonus`, spending up to `cost` vigor.
/// `behavior` and `fx` are the skill record's story behavior and FX (0 = none).
#[derive(Clone, Debug, PartialEq)]
pub struct Strike {
    pub status_fx: Vec<crate::status_fx::Effect>,
    pub ratings: Vec<RatingEffect>,
    pub skill: u32,
    pub bonus: u32,
    pub vigor_damage: u32,
    pub weapon_damage: bool,
    pub cost: u32,
    pub range: f32,
    pub behavior: u32,
    pub fx: u32,
    pub burn: Vec<Over>,
}
/// A vital change applied every `period_ms` for `duration_ms`.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Over {
    pub effect: u32,
    pub health: u32,
    pub vigor: u32,
    pub period_ms: u64,
    pub duration_ms: u64,
}
/// Regeneration multipliers from a timed buff (1.0 = unchanged).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Regen {
    pub effect: u32,
    pub health: f32,
    pub vigor: f32,
    pub duration_ms: u64,
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RatingEffect {
    pub ratings: crate::combat::Ratings,
    pub duration_ms: u64,
    pub effect: u32,
}
#[derive(Clone)]
struct Burn {
    skill: u32,
    source: u64,
    target: u64,
    per_tick: u32,
    vigor_per_tick: u32,
    period: u64,
    next: u64,
    until: u64,
}
#[derive(Clone)]
struct Mend {
    effect: u32,
    started: u64,
    instant: bool,
    skill: u32,
    source: u64,
    player: u64,
    health: u32,
    vigor: u32,
    period: u64,
    next: u64,
    until: u64,
    /// FX shown on the player when the first restoration lands (0 = none).
    fx: u32,
}
const MAX_TIMED: usize = 512;

// Allow normal world-tick jitter while discarding stale ticks after a stall.
const EFFECT_TICK_GRACE_MS: u64 = 250;

/// Keep the original schedule even if the server has skipped some updates.
/// Coalesce due ticks within the grace window through the expiry boundary.
fn advance_periodic(next: &mut u64, period: u64, until: u64, now: u64) -> u32 {
    let through = until.min(now);
    if *next > through {
        return 0;
    }
    let last_due = through - (through - *next) % period;
    let first_due = *next;
    *next = last_due.saturating_add(period);
    let oldest = first_due.max(now.saturating_sub(EFFECT_TICK_GRACE_MS));
    if last_due < oldest {
        return 0;
    }
    (1 + (last_due - oldest) / period) as u32
}
#[derive(Clone)]
pub enum Event {
    SkillSwing { player: u64, behavior: u32, skill: u32 },
    Fx { target: u64, fx: u32 },
    StopFx { target: u64, fx: u32 },
    Loot { actor:u64, player:u64, entity:u32, level:Option<u32>, home:Placement, ai:crate::creature_ai::Profile },
    Move { actor: u64, placement: Placement },
    IdolVisual(u64, crate::combat_visual::IdolAction),
    Swing(u64),
    Projectile { player: u64, target: u64, profile: crate::projectiles::Profile, flight_ms: u64 },
    /// Signed health on the victim. Negative is damage. The vital update is separate.
    Hit {
        victim: u64,
        attacker: u64,
        health_change: i32,
        skill: Option<u32>,
    },
    Item {
        player: u64,
        item: crate::inventory::Item,
        instance: u16,
    },
    Quest {
        player: u64,
        previous: crate::quests::Book,
        book: crate::quests::Book,
    },
    Health {
        actor: u64,
        health: u32,
    },
    Vigor { actor: u64, vigor: u32 },
    Despawn(u64),
    Respawn {
        actor: u64,
        health: u32,
        vigor: Option<u32>,
    },
    Owner {
        player: u64,
        previous: Progression,
        state: Progression,
    },
    Note {
        player: u64,
        text: String,
    },
    Stop(u64),
    /// A live rules edit changed a creature's vitals or level.
    Stats { actor: u64, max_health: u32, health: u32, level: Option<u32> },
}
#[derive(Clone)]
pub struct Encounters {
    native_motion: bool,
    spawn_blockers: crate::spawn_clearance::Blockers,
    dice: crate::combat::Dice,
    pet_strikes: BTreeMap<u64, (u64, u64, u16)>,
    burns: Vec<Burn>,
    status_fx: crate::status_fx::Runtime,
    mends: Vec<Mend>,
    regen: BTreeMap<(u64,u32,usize), (Regen, u64)>,
    ratings: Vec<(u64,u32,RatingEffect,u64)>,
    actors: BTreeMap<u64, Actor>,
    spawners: crate::spawners::Spawners,
    bestiary: Bestiary,
    /// Events from live edits, returned by the next tick at once.
    pending: Vec<Event>,
    attacks: BTreeMap<u64, Attack>,
    cooldowns: BTreeMap<u64, u64>,
    damage: u32,
    cost: u32,
    interval: u64,
    range: f32,
    next_tick: u64,
    regen_health: u32,
    regen_vigor: u32,
    regen_interval: u64,
    next_regen: u64,
    loot_enabled: bool,
}
/// Selected-enemy area hooks verified against the Legions and 7230 skill records.
fn native_area_radius(skill: u32) -> Option<f32> {
    match skill {
        0x57 | 0x114 => Some(10.0), // Explosion and Earthquake
        0x31d => Some(5.0),         // Volley
        _ => None,
    }
}
fn in_range(a: Option<Placement>, b: Option<Placement>, range: f32) -> bool {
    a.zip(b).and_then(|(a,b)|a.distance(b)).is_some_and(|d|d<=range)
}
fn facing(from:Placement,target:Placement)->Option<Placement> {
    let [dx,dy,_]=from.offset_to(target)?;
    if dx.hypot(dy)<0.001 {return Some(from);}
    // Native Heading::ToUInt8 truncates, using the f32 1/360 constant.
    let degrees=dx.atan2(dy).to_degrees().rem_euclid(360.0);
    Some(Placement{heading:(f64::from(degrees)*f64::from(1f32/360.)*255.).trunc() as u8,..from})
}
fn walk_clear(world:Option<&mut crate::geometry::Geometry>,occupied:&crate::geometry::Occupancy,
    id:u64,body:crate::creature_motion::Profile,from:Placement,to:Placement)->Option<Placement> {
    let next=world?.walk_body(from,to,body.radius,body.height)?;
    if !occupied.permits(id,from,next,body.radius,body.height){return None;}
    Some(next)
}
pub(crate) fn approach(from:Placement,target:Placement,stop:f32,budget:f32)->Option<Placement> {
    let [dx,dy,_]=from.offset_to(target)?;
    let distance=dx.hypot(dy);
    if distance<=stop+0.01{return None;}
    let step=budget.min(distance-stop);
    if step<=0.0001{return None;}
    let next=from.translated([dx/distance*step,dy/distance*step,0.])?;
    facing(next,target)
}
/// Spend a distance budget through the same short, collision-checked segments
/// used before variable speed. Retain every segment for safe homeward retracing.
fn advance(from:Placement,target:Placement,stop:f32,mut budget:f32,route:&mut Vec<Placement>,
    mut walk:impl FnMut(Placement,Placement)->Option<Placement>)->Option<Placement> {
    let mut point=from;
    for _ in 0..128 {
        if route.len()>=4096 {break;}
        let Some(proposed)=approach(point,target,stop,budget.min(0.2)) else {break};
        let Some(next)=walk(point,proposed) else {break};
        if from.distance(next).is_none_or(|d|d>crate::creature_motion::MAX_UPDATE_DISTANCE) {break;}
        let [x,y,_]=point.offset_to(next)?;let distance=x.hypot(y);
        if distance<0.0001 {break;}
        budget=(budget-distance).max(0.);route.push(point);point=next;
    }
    (point!=from).then_some(point)
}
fn retreat(from:Placement,mut budget:f32,route:&mut Vec<Placement>,
    mut walk:impl FnMut(Placement,Placement)->Option<Placement>)->Option<Placement> {
    let mut point=from;
    for _ in 0..128 {
        while route.last().is_some_and(|p|point.offset_to(*p).is_some_and(|[x,y,z]|x.hypot(y)<0.01 && z.abs()<=0.65)) {route.pop();}
        let Some(&goal)=route.last() else {break};
        let Some(proposed)=approach(point,goal,0.,budget.min(0.2)) else {break};
        let Some(next)=walk(point,proposed) else {break};
        if from.distance(next).is_none_or(|d|d>crate::creature_motion::MAX_UPDATE_DISTANCE) {break;}
        let [x,y,_]=point.offset_to(next)?;let distance=x.hypot(y);
        if distance<0.0001 {break;}
        budget=(budget-distance).max(0.);point=next;
    }
    (point!=from).then_some(point)
}
/// Spawn groups from world rules carry this bit, so a live edit can replace
/// exactly them and leave captured and inferred groups alone.
pub const OPERATOR_GROUP: u64 = 1 << 63;

fn academy_idol(id: u64) -> bool {
    static IDS: std::sync::OnceLock<BTreeSet<u64>> = std::sync::OnceLock::new();
    // Old stores and explicit practice fixtures may still place the retired Human idols.
    if matches!(id, 0x8140000000030000 | 0x814000000003000b | 0x814000000003000c | 0x8140000000030032 | 0x8140000000030033) {
        return true;
    }
    IDS.get_or_init(|| include_str!("../content/training-world.tsv").lines().filter_map(|line| {
        let mut fields = line.split('\t');
        let id = u64::from_str_radix(fields.next()?, 16).ok()?;
        (fields.next()? == "470007d1").then_some(id)
    }).collect()).contains(&id)
}

/// Native creature tables plus the operator rules: everything needed to build or
/// re-rule a creature after startup.
#[derive(Clone)]
struct Bestiary {
    rules: BTreeMap<u32, Rule>,
    native_rules: BTreeMap<u32, Rule>,
    regional_native: bool,
    ai: BTreeMap<u32, crate::creature_ai::Profile>,
    labels: BTreeMap<u32, String>,
    protected: BTreeSet<u32>,
    spawn_policies: BTreeMap<u32, crate::spawners::Policy>,
    world: std::sync::Arc<crate::world_rules::Rules>,
}
impl Bestiary {
    /// The rule, AI and patrol a placement gets, or None when it is no creature.
    fn ruled(&self, entity: u32, id: u64) -> Option<(Rule, crate::creature_ai::Profile, Option<crate::world_rules::Patrol>)> {
        let custom = self.world.stats_for(entity, id);
        // Operator stats can make any placed non-service object a creature.
        let regional=matches!(id & 0xffffffffff000000, 0x8140c00000000000 | 0x8140c10000000000 | 0x8140d30000000000 | 0x8140db0000000000 | 0x8140dc0000000000 | 0x8140dd0000000000);
        let selected=if self.regional_native && regional {&self.native_rules}else{&self.rules};
        let mut rule = selected.get(&entity).copied().or_else(|| custom.and_then(|s| Some(Rule {
            health: s.health?, damage: s.damage?, level: None, vigor: None, armor: 0, interval: 1500,
            range: 3.0, respawn: 30_000, xp: 0, gold: 0, leash: 18. })))?;
        let behaviour = self.world.behaviour_for(entity, id);
        let mut ai = self.ai.get(&entity).copied().unwrap_or_default();
        // Only the authored academy placements are practice targets. The same
        // template also supplies level-24 outdoor idols and captured encounters.
        if entity == crate::quests::TARGET_ENTITY && academy_idol(id) {
            rule.level = Some(1);
            rule.vigor = Some(rule.health);
            ai.custom_aggressive = Some(false);
        }
        let ai = behaviour.apply(ai);
        if ai.homesick.is_none() { rule.leash = rule.leash.max(ai.perception); }
        if let Some(radius) = behaviour.homesick.filter(|r| *r > 0.) { rule.leash = radius; }
        if let Some(s) = custom { rule = apply_stats(rule, s); }
        Some((rule, ai, self.world.patrols.get(&id).cloned()))
    }
    fn actor(&self, object: &crate::content::Object) -> Option<Actor> {
        // Authored dialogue roles override inherited combat data: the
        // training mentor's template also carries a monster profile.
        // Use the same identities as the interaction dispatcher.
        if object.service.is_some()
            || self.protected.contains(&object.entity)
            || object.entity == crate::training_dialogue::MENTOR_ENTITY
            || crate::shelter::supported(object.entity, object.id)
        {
            return None;
        }
        let (rule, ai, patrol) = self.ruled(object.entity, object.id)?;
        Some(Actor {
            entity: object.entity,
            name: object.name.clone().or_else(|| self.labels.get(&object.entity).cloned()),
            home: object.placement,
            pursuit: object.supports_pose_updates(),
            movement: crate::creature_motion::Profile::default(),
            velocity: 0.,
            route: Vec::new(),
            rule,
            health: rule.health,
            vigor: rule.vigor,
            target: None,
            next_attack: 0,
            ai, next_idle: object.id % 1000, wander_goal: None, evading: false, random: object.id,
            patrol, patrol_next: 0, patrol_wait: 0, engaged_at: None,
        })
    }
    /// Operator groups own their members' timers; operator respawn values win
    /// over development policies for the targeted type or placement.
    fn policy(&self, id: u64, actor: &Actor) -> crate::spawners::Policy {
        let custom = self.world.stats_for(actor.entity, id).and_then(|s| s.respawn_ms);
        let group = self.world.groups.iter().find(|g| g.members.contains(&id)).and_then(|g| g.regeneration_ms);
        let mut policy = self.spawn_policies.get(&actor.entity).copied().unwrap_or(crate::spawners::Policy {
            initial_delay_ms: 0,
            regeneration_ms: if matches!(id & 0xffffffffff000000, 0x8140c00000000000 | 0x8140c10000000000 | 0x8140db0000000000) { 180000 } else if id & 0xffffff0000000000 == 0x8140a00000000000 || matches!(id & 0xffffffffff000000, 0x8140d30000000000 | 0x8140dd0000000000 | 0x8140ed0000000000) { 120000 } else { actor.rule.respawn } });
        if let Some(ms) = group.or(custom) { policy.regeneration_ms = ms; }
        policy
    }
    fn operator_groups(&self, actors: &BTreeMap<u64, Actor>) -> Vec<(u64, usize, Vec<u64>)> {
        // Members that are not placed creatures (deleted or services) are skipped.
        self.world.groups.iter().map(|g| (g.id, g.capacity,
            g.members.iter().copied().filter(|m| actors.contains_key(m)).collect())).collect()
    }
}

fn apply_stats(mut rule: Rule, s: crate::world_rules::Stats) -> Rule {
    if let Some(v) = s.health { rule.health = v; }
    if let Some(v) = s.damage { rule.damage = v; }
    if let Some(v) = s.armor { rule.armor = v; }
    if let Some(v) = s.level { rule.level = Some(v).filter(|level| *level > 0); }
    if let Some(v) = s.xp { rule.xp = v; }
    if let Some(v) = s.gold { rule.gold = v; }
    if let Some(v) = s.interval_ms { rule.interval = v; }
    if let Some(v) = s.range { rule.range = v; }
    if let Some(v) = s.leash { rule.leash = v; }
    if let Some(v) = s.respawn_ms { rule.respawn = v; }
    rule
}
impl Encounters {
    pub fn load_motion(&mut self,content:&Content,directory:&Path)->io::Result<()> {
        let mut dat=crate::dat::Dat::open(directory.join("portal.dat"))?;
        let mut catalog=crate::creature_motion::Catalog::default();
        for (&id,actor) in &mut self.actors {
            let object=content.object(id).ok_or_else(||invalid("Movement actor missing content"))?;
            actor.movement=catalog.resolve(&mut dat,object.visual,object.scale)?
                .ok_or_else(||io::Error::other(format!("Missing native movement for creature {id:016x}, visual {:08x}",object.visual)))?;
        }
        self.spawn_blockers=crate::spawn_clearance::Blockers::load(content,&mut dat,&mut catalog,
            |id|self.actors.contains_key(&id))?;
        self.native_motion=true;
        Ok(())
    }
    pub fn movement_profile(&self,id:u64)->Option<crate::creature_motion::Profile> {
        self.actors.get(&id).map(|a|a.movement)
    }
    pub fn load(path: &Path, content: &Content) -> io::Result<Self> {
        Self::load_with_rules(path, content, &crate::world_rules::Rules::default())
    }
    pub fn load_with_rules(path: &Path, content: &Content, rules: &crate::world_rules::Rules) -> io::Result<Self> {
        if path == Path::new("world") {
            return Self::parse_profile(
                include_str!("../content/development-encounters.tsv"),
                content,
                true,
                rules,
            );
        }
        let mut bytes = Vec::new();
        File::open(path)?.take(65537).read_to_end(&mut bytes)?;
        if bytes.len() > 65536 {
            return Err(invalid("encounter profile exceeds 64 KiB"));
        }
        let text =
            std::str::from_utf8(&bytes).map_err(|_| invalid("encounter profile encoding"))?;
        Self::parse_with_rules(text, content, rules)
    }
    #[cfg(test)]
    fn parse(text: &str, content: &Content) -> io::Result<Self> {
        Self::parse_with_rules(text, content, &crate::world_rules::Rules::default())
    }
    fn parse_with_rules(text: &str, content: &Content, world_rules: &crate::world_rules::Rules) -> io::Result<Self> {
        Self::parse_profile(text, content, false, world_rules)
    }
    fn parse_profile(text: &str, content: &Content, regional_native: bool, world_rules: &crate::world_rules::Rules) -> io::Result<Self> {
        if text.len() > 65536 {
            return Err(invalid("encounter profile exceeds 64 KiB"));
        }
        let mut lines = text.lines();
        if lines.next() != Some("pyrealium-encounters-v1") {
            return Err(invalid("encounter profile header"));
        }
        let mut player = None;
        let mut regen = None;
        let mut rules = BTreeMap::new();
        let mut spawn_policies = BTreeMap::new();
        let mut spawn_groups=Vec::new();
        for line in lines {
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            if line.len() > if line.starts_with("spawn-group\t") { 2048 } else { 512 } {
                return Err(invalid("encounter row length"));
            }
            let f: Vec<_> = line.split('\t').collect();
            let number = |i: usize| {
                f[i].parse::<u64>()
                    .map_err(|_| invalid("invalid encounter integer"))
            };
            let small = |i: usize| {
                u32::try_from(number(i)?).map_err(|_| invalid("encounter integer overflow"))
            };
            let range = |i: usize| -> io::Result<f32> {
                let value = f[i]
                    .parse::<f32>()
                    .map_err(|_| invalid("encounter range"))?;
                if !value.is_finite() || !(0.5..=30.0).contains(&value) {
                    return Err(invalid("encounter range bounds"));
                }
                Ok(value)
            };
            match f.as_slice() {
                ["spawn-group",_,_,_] => {
                    let id=u64::from_str_radix(f[1],16).map_err(|_|invalid("spawner group identity"))?;
                    let capacity=number(2)? as usize;
                    let members=f[3].split(',').map(|id|u64::from_str_radix(id,16).map_err(|_|invalid("spawner child identity")))
                        .collect::<io::Result<Vec<_>>>()?;
                    spawn_groups.push((id,capacity,members));
                }
                ["spawner", _, _, _] => {
                    let entity=u32::from_str_radix(f[1].trim_start_matches("0x"),16)
                        .map_err(|_|invalid("spawner entity"))?;
                    let policy=crate::spawners::Policy{initial_delay_ms:number(2)?,regeneration_ms:number(3)?};
                    if entity>>24!=0x47 || !policy.valid() || spawn_policies.len()>=4096
                        || spawn_policies.insert(entity,policy).is_some() {
                        return Err(invalid("invalid or duplicate spawner policy"));
                    }
                }
                ["player", _, _, _, _] if player.is_none() => {
                    player = Some((small(1)?, small(2)?, number(3)?, range(4)?));
                }
                ["regen", _, _, _] if regen.is_none() => {
                    regen = Some((small(1)?, small(2)?, number(3)?));
                }
                ["npc", _, _, _, _, _, _, _, _] => {
                    let entity = u32::from_str_radix(f[1].trim_start_matches("0x"), 16)
                        .map_err(|_| invalid("encounter entity"))?;
                    let rule = Rule {
                        health: small(2)?,
                        level: None,
                        vigor: None,
                        damage: small(3)?,
                        armor: 0,
                        interval: number(4)?,
                        range: range(5)?,
                        respawn: number(6)?,
                        xp: number(7)?,
                        gold: small(8)?,
                        leash: 18.,
                    };
                    if entity >> 24 != 0x47
                        || rule.health == 0
                        || rule.health > 1_000_000
                        || rule.damage == 0
                        || rule.damage > 1_000_000
                        || !(250..=60000).contains(&rule.interval)
                        || !(1000..=86400000).contains(&rule.respawn)
                        || rule.xp > i64::MAX as u64
                        || rule.gold > i32::MAX as u32
                        || rules.len() >= 256
                        || rules.insert(entity, rule).is_some()
                    {
                        return Err(invalid("invalid or duplicate encounter rule"));
                    }
                }
                _ => return Err(invalid("unrecognized encounter row")),
            }
        }
        let (damage, cost, interval, range) =
            player.ok_or_else(|| invalid("missing player attack profile"))?;
        let (regen_health, regen_vigor, regen_interval) = regen.unwrap_or((0, 0, 1000));
        if damage == 0
            || damage > 1_000_000
            || cost == 0
            || cost > 1_000_000
            || !(250..=60000).contains(&interval)
            || regen_health > 1_000_000
            || regen_vigor > 1_000_000
            || !(1000..=60000).contains(&regen_interval)
        {
            return Err(invalid("player or regeneration profile bounds"));
        }
        // An authored row wins: the retail Idol Spawn is level 24 and one-hits a
        // level 1 character. Interval 0 means the record had no natural weapon
        // time. A catalog entry alone is not evidence that an NPC is attackable.
        // Only explicit encounter rules or recovered combat records qualify.
        let mut native_rules=BTreeMap::new();
        for line in include_str!("../content/creature-combat.tsv").lines()
            .chain(include_str!("../content/inferred-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/regional-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/wilderness-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/hero-wilderness-creature-combat-v2.tsv").lines())
            .chain(include_str!("../content/ac2hero-raid-combat-v1.tsv").lines())
            .chain(include_str!("../content/ac2hero-bestiary-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/arwic-mines-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/foundry-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/the-ossuary-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/quest-encounters-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/cragstone-excavation-creature-combat-v1.tsv").lines())
            .chain(include_str!("../content/illuminated-cave-creature-combat-v1.tsv").lines()) {
            if line.is_empty() || line.starts_with('#') || line.starts_with("pyrealium-") {
                continue;
            }
            let f: Vec<_> = line.split('\t').collect();
            if ![7,13,14].contains(&f.len()) {
                return Err(invalid("creature combat field count"));
            }
            let entity = u32::from_str_radix(f[0], 16).map_err(|_| invalid("creature combat entity"))?;
            let number = |i: usize| {
                f[i].parse::<u64>()
                    .map_err(|_| invalid("creature combat integer"))
            };
            let small = |i: usize| {
                u32::try_from(number(i)?).map_err(|_| invalid("creature combat overflow"))
            };
            let health = small(1)?;
            let damage = small(2)?;
            let mut interval = number(3)?;
            if interval == 0 {
                interval = 1500;
            }
            let armor = small(4)?;
            let xp = number(5)?;
            if entity >> 24 != 0x47
                || health == 0
                || health > 5_000_000
                || damage == 0
                || damage > 5_000_000
                || armor > 1_000_000
                || !(250..=60000).contains(&interval)
                || xp > i64::MAX as u64
            {
                return Err(invalid("creature combat bounds"));
            }
            let rule = Rule {
                health,
                level: Some(small(6)?).filter(|level| *level>0),
                vigor: if f.len()==14 && !f[13].is_empty(){Some(small(13)?)}else{None},
                damage,
                armor,
                interval,
                range: 3.0,
                respawn: 30_000,
                xp,
                gold: 2,
                leash: if f.len()>=13 {let value=f[11].parse::<f32>().map_err(|_|invalid("native homesick radius"))?;
                    if !value.is_finite() || !(0.0..=500.0).contains(&value){return Err(invalid("native homesick radius bounds"));}
                    if value>0. {value}else{18.}}else{18.},
            };
            native_rules.entry(entity).or_insert(rule);
            if rules.contains_key(&entity) {
                continue;
            }
            if rules.len() >= 4096 || rules.insert(entity, rule).is_some() {
                return Err(invalid("creature combat rule limit"));
            }
        }
        // Catalog labels are presentation only; unlike the old fallback list,
        // they must never confer health, rewards or attackability on an NPC.
        let labels=include_str!("../content/definitions.tsv").lines().filter_map(|line|{
            let mut fields=line.split('\t');let entity=u32::from_str_radix(fields.next()?,16).ok()?;
            fields.next()?;let name=fields.next()?;
            Some((entity,name.split('[').next().unwrap_or(name).trim().to_owned()))
        }).collect::<BTreeMap<_,_>>();
        let protected=include_str!("../content/population-protected.txt").lines()
            .filter_map(|line|u32::from_str_radix(line,16).ok()).collect::<std::collections::BTreeSet<_>>();
        let bestiary=Bestiary{rules,native_rules,regional_native,ai:crate::creature_ai::catalog()?,labels,protected,spawn_policies,
            world:std::sync::Arc::new(world_rules.clone())};
        let actors=content.objects().filter_map(|object|bestiary.actor(object).map(|actor|(object.id,actor)))
            .collect::<BTreeMap<_,_>>();
        if actors.is_empty() || actors.len() > 16384 {
            return Err(invalid("encounters require 1..16384 matching placed actors"));
        }
        if bestiary.spawn_policies.keys().any(|entity|!actors.values().any(|actor|actor.entity==*entity)) {
            return Err(invalid("spawner policy must name a placed combat creature"));
        }
        // Captures keep one point spawner per observation. The separate inferred
        // pack caps its authored rosters as groups; services remain excluded.
        let mut spawners=crate::spawners::Spawners::new(actors.iter().map(|(&id,actor)|(id,bestiary.policy(id,actor))))?;
        for (id,capacity,members) in spawn_groups {spawners.group(id,capacity,members)?;}
        for source in [include_str!("../content/inferred-world-v1.report.json"),
            include_str!("../content/regional-world-v1.report.json"),
            include_str!("../content/wilderness-world-v1.report.json"),
            include_str!("../content/hunting-world-v2.report.json"),
            include_str!("../content/vault-world-v1.report.json"),
            include_str!("../content/illuminated-cave-v1.report.json"),
            include_str!("../content/warder-hideout-v1.report.json"),
            include_str!("../content/the-ossuary-v1.report.json"),
        include_str!("../content/quest-encounters-v1.report.json"),
            include_str!("../content/cragstone-excavation-v1.report.json"),
            include_str!("../content/ac2hero-raid-world-v1.report.json"),
            include_str!("../content/hero-wilderness-camps-v2.report.json"),
            include_str!("../content/ac2hero-settlements-v1.report.json"),
            include_str!("../content/fiery-crypt-map-v1.report.json"),
            include_str!("../content/foundry-generals-v1.report.json"),
            include_str!("../content/arwic-mines-world-v1.report.json")] {
        let inferred:serde_json::Value=serde_json::from_str(source).map_err(io::Error::other)?;
        for group in inferred["groups"].as_array().ok_or_else(||invalid("inferred spawn groups"))? {
            let members=group["members"].as_array().ok_or_else(||invalid("inferred roster"))?.iter().filter_map(|v|v.as_u64()).collect::<Vec<_>>();
            // External content can select only part of the authored pack.
            let members=members.into_iter().filter(|id|actors.contains_key(id)).collect::<Vec<_>>();
            if !members.is_empty(){let capacity=usize::try_from(group["capacity"].as_u64().ok_or_else(||invalid("inferred capacity"))?)
                .map_err(|_|invalid("inferred capacity overflow"))?.min(members.len());
                let id=group["id"].as_u64().ok_or_else(||invalid("inferred group identity"))?;
                if group["roaming_homes"].as_bool()==Some(true) {
                    if capacity!=1 {return Err(invalid("roaming encounters require a single lifetime"));}
                    spawners.roaming_group(id,members)?;
                } else {spawners.group(id,capacity,members)?;}
            }
        }
        }
        spawners.replace_groups(OPERATOR_GROUP,bestiary.operator_groups(&actors))?;
        Ok(Self {
            native_motion: false,
            spawn_blockers: crate::spawn_clearance::Blockers::default(),
            actors,
            spawners,
            bestiary,
            pending: Vec::new(),
            pet_strikes: BTreeMap::new(),
            attacks: BTreeMap::new(),
            cooldowns: BTreeMap::new(),
            damage,
            cost,
            interval,
            range,
            next_tick: 0,
            regen_health,
            regen_vigor,
            regen_interval,
            next_regen: 0,
            burns: Vec::new(), mends: Vec::new(), regen: BTreeMap::new(), ratings: Vec::new(),
            status_fx: crate::status_fx::Runtime::default(),
            loot_enabled: false,
            dice:crate::combat::Dice::seeded(),
        })
    }
    /// Live edit: whether a newly placed object is a creature under the current rules.
    pub fn live_is_creature(&self, object: &crate::content::Object) -> bool {
        self.bestiary.actor(object).is_some()
    }
    pub(crate) fn live_npc(&mut self,object:&crate::content::Object,motion:Option<crate::creature_motion::Profile>) {
        self.spawn_blockers.add_npc(object,motion);
    }
    /// Live edit: the operator rules every later live change is built from.
    pub fn live_set_rules(&mut self, rules: std::sync::Arc<crate::world_rules::Rules>) {
        self.bestiary.world = rules;
    }
    /// Live edit: a new creature placement. Content holds it inactive; its point
    /// spawner shows it through the normal spawn path on the next poll.
    pub fn live_add(&mut self, object: &crate::content::Object, motion: Option<crate::creature_motion::Profile>, now: u64) -> io::Result<()> {
        let mut actor = self.bestiary.actor(object).ok_or_else(|| invalid("object is not a creature"))?;
        if self.native_motion && motion.is_none(){return Err(invalid("live creature is missing native movement data"));}
        if let Some(profile)=motion {actor.movement=profile;}
        actor.health=0; // Waiting for its first published spawner lifetime.
        if self.actors.len() >= 16384 { return Err(invalid("creature limit reached")); }
        self.spawners.add_point(object.id, self.bestiary.policy(object.id, &actor), now)?;
        self.actors.insert(object.id, actor);
        Ok(())
    }
    /// The stats a new creature's content object carries (as at startup).
    pub fn live_stats(&self, id: u64) -> Vec<(u32, u32)> {
        let Some(actor) = self.actors.get(&id) else { return Vec::new() };
        let rule = actor.rule;
        [(0x107, Some(rule.health)), (0x102, Some(rule.health)), (0x12e, rule.level), (0x108, rule.vigor), (0x103, rule.vigor)]
            .into_iter().filter_map(|(stat, value)| value.map(|v| (stat, v))).collect()
    }
    /// Live edit: the creature leaves the world. Attackers stop at once.
    pub fn live_remove(&mut self, id: u64) {
        self.spawn_blockers.remove(id);
        if self.actors.remove(&id).is_none() { return; }
        self.spawners.remove_point(id);
        self.burns.retain(|b|b.target!=id && b.source!=id);
        self.ratings.retain(|(who,_,_,_)|*who!=id);
        self.pending.retain(|event|!matches!(event,Event::Stats{actor,..} if *actor==id));
        let attackers = self.attacks.iter().filter(|(_, a)| a.target == id).map(|(&p, _)| p).collect::<Vec<_>>();
        for player in attackers {
            self.attacks.remove(&player);
            self.pending.push(Event::Stop(player));
            self.pending.push(Event::Note { player, text: "Attack stopped: your target was removed by a world edit.".into() });
        }
    }
    /// Live edit: re-rule the creatures a rules change touches (`None`: all of them),
    /// then rebuild the operator spawn groups.
    pub fn live_rules(&mut self, affected: Option<&BTreeSet<crate::world_rules::Target>>) -> io::Result<()> {
        use crate::world_rules::Target;
        for (&id, actor) in &mut self.actors {
            if affected.is_some_and(|set| !set.contains(&Target::Actor(id)) && !set.contains(&Target::Entity(actor.entity))) {
                continue;
            }
            let Some((rule, ai, patrol)) = self.bestiary.ruled(actor.entity, id) else { continue };
            if rule != actor.rule {
                let full = actor.health == actor.rule.health;
                if actor.health > 0 {
                    actor.health = if full { rule.health } else { actor.health.min(rule.health) };
                }
                self.pending.push(Event::Stats { actor: id, max_health: rule.health, health: actor.health, level: rule.level });
                actor.rule = rule;
            }
            // Made peaceful: drop the fight. A creature that was already peaceful
            // keeps defending itself against whoever attacked it.
            if actor.target.is_some() && actor.ai.aggressive() && !ai.aggressive() {
                actor.target = None;
                actor.evading = true;
                actor.wander_goal = None;
                self.pending.push(Event::IdolVisual(id, crate::combat_visual::IdolAction::Peace));
            }
            actor.ai = ai;
            if patrol != actor.patrol {
                actor.patrol = patrol;
                actor.patrol_next = 0;
                actor.patrol_wait = 0;
                if actor.target.is_none() { actor.wander_goal = None; }
            }
            self.spawners.set_policy(id, self.bestiary.policy(id, actor));
        }
        self.spawners.replace_groups(OPERATOR_GROUP, self.bestiary.operator_groups(&self.actors))
    }
    pub fn initial_health(&self) -> impl Iterator<Item = (u64, u32)> + '_ {
        self.actors
            .iter()
            .map(|(&id, actor)| (id, actor.rule.health))
    }
    pub fn inspection_stats(&self) -> impl Iterator<Item = (u64, u32, u32)> + '_ {
        self.actors.iter().flat_map(|(&id, actor)| {
            [(0x12e,actor.rule.level),(0x108,actor.rule.vigor),(0x103,actor.vigor)]
                .into_iter().filter_map(move |(stat,value)|value.map(|v|(id,stat,v)))
        })
    }
    pub fn spawners(&self) -> &crate::spawners::Spawners { &self.spawners }
    #[cfg(test)]
    pub(crate) fn spawners_mut_for_test(&mut self) -> &mut crate::spawners::Spawners { &mut self.spawners }
    /// Called before any client can observe the scene. Only managed monsters
    /// are withdrawn; vendors, dialogue actors and world objects remain placed.
    pub fn initialize_spawners(&mut self,scene:&mut Scene,now:u64)->io::Result<()> {
        for (&id,actor) in &mut self.actors {
            if scene.content_placement(id).is_some(){scene.despawn_content(id)?;}
            actor.health=0;actor.target=None;actor.route.clear();
            actor.patrol_next=0;actor.patrol_wait=0;actor.engaged_at=None;
        }
        self.spawners.initialize(now);
        Ok(())
    }
    /// Acknowledge only after Scene has created the new network lifetime.
    pub fn spawned(&mut self,id:u64)->io::Result<()> {
        if !self.spawners.spawned(id){return Err(invalid("unexpected spawner child"));}
        let actor=self.actors.get_mut(&id).ok_or_else(||invalid("unknown spawner child"))?;
        actor.health=actor.rule.health;actor.vigor=actor.rule.vigor;actor.target=None;actor.route.clear();actor.next_attack=0;
        actor.velocity=0.;
        actor.evading=false;actor.wander_goal=None;
        self.burns.retain(|b|b.target!=id);
        self.ratings.retain(|(who,_,_,_)|*who!=id);
        actor.patrol_next=0;actor.patrol_wait=0;actor.engaged_at=None;
        Ok(())
    }
    pub fn removed(&mut self,id:u64,now:u64) { self.spawners.removed(id,now); }
    pub fn stop(&mut self, player: u64) {
        self.attacks.remove(&player);
    }
    pub(crate) fn living_target(&self, id: u64) -> bool {
        self.actors.get(&id).is_some_and(|actor| actor.health > 0 && !actor.evading)
    }
    pub(crate) fn queue_pet_strike(&mut self, owner: u64, pet: u64, target: u64, rank: u16) {
        self.pet_strikes.insert(owner, (pet, target, rank));
    }
    pub(crate) fn clear_pet_strike(&mut self, owner: u64) { self.pet_strikes.remove(&owner); }

    pub fn attacking_target(&self, player: u64) -> Option<u64> {
        self.attacks.get(&player).map(|attack| attack.target)
    }
    pub fn auto_attacking_target(&self, player: u64) -> Option<u64> {
        self.attacks.get(&player)
            .filter(|attack| attack.repeat && attack.strike.is_none() && !attack.periodic)
            .map(|attack| attack.target)
    }
    fn direct_hit_chance(&self, player: &crate::store::Character, target: u64, actor: &Actor, category: usize) -> f32 {
        use crate::combat::{adjusted, hit_chance, player_attack, School};
        let school = match category { 1 => School::Missile, 2 => School::Magic, _ => School::Melee };
        let attack = adjusted(player_attack(player.progression.level().number, &player.skills, school),
            self.ratings_for(player.id).attack(school));
        let defense = adjusted(actor.rule.level.unwrap_or(1) * 8, self.ratings_for(target).creature_defense());
        hit_chance(attack, defense)
    }

    /// The examine window describes a normal attack with the currently equipped weapon.
    pub fn examine_hit_chance(&self, player: &crate::store::Character, target: u64) -> Option<f32> {
        let actor = self.actors.get(&target).filter(|actor| actor.health > 0)?;
        Some(self.direct_hit_chance(player, target, actor, crate::starter_gear::equipped_category(&player.inventory)))
    }

    pub fn is_creature(&self, id: u64) -> bool {
        self.actors.contains_key(&id)
    }
    /// Local test navigation to actual runtime actors, never a spawn generator.
    /// Use current positions and exclude dead/despawned actors and all rooms.
    pub fn outdoor_survey(&self, scene: &Scene, origin: Placement) -> Vec<String> {
        if !(1..=64).contains(&(origin.cell & 0xffff)) {
            return vec!["Go outdoors to locate imported world creatures.".into()];
        }
        let mut nearby=self.actors.iter().filter_map(|(&id,actor)| {
            if actor.health==0 {return None;}
            let point=scene.content_placement(id)?;
            if !(1..=64).contains(&(point.cell & 0xffff)) {return None;}
            Some((origin.distance(point)?,id,point,actor.name.as_deref().unwrap_or("Creature")))
        }).collect::<Vec<_>>();
        nearby.sort_by(|a,b|a.0.total_cmp(&b.0).then(a.1.cmp(&b.1)));
        let close=nearby.iter().filter(|row|row.0<=300.).count();
        let mut lines=vec![format!("Imported outdoor creatures: {} alive; {close} within 300 m. {} managed point spawners ({} live across all areas). Nearest locations (local testing):",nearby.len(),self.spawners.len(),self.spawners.live())];
        for (distance,_,point,name) in nearby.into_iter().take(5) {
            // Same native land-cell coordinates as the client map (20 m per
            // cell, 1020 at the origin, 10.2 cells per compass unit).
            let east=((point.cell>>24) as f32*8.+point.coordinates[0]/20.-1020.)/10.2;
            let north=(((point.cell>>16)&255) as f32*8.+point.coordinates[1]/20.-1020.)/10.2;
            lines.push(format!("{name}: {:.1}{}, {:.1}{} ({distance:.0} m away; cell {:08X}).",
                north.abs(),if north>=0. {'N'}else{'S'},east.abs(),if east>=0. {'E'}else{'W'},point.cell));
        }
        lines
    }
    pub fn includes_entity(&self, entity: u32) -> bool {
        self.actors.values().any(|actor| actor.entity == entity)
    }
    /// Call only after the server validates the authored potion's DAT profile.
    pub fn enable_loot(&mut self) {
        self.loot_enabled = true;
    }
    pub fn start(
        &mut self,
        player: u64,
        target: u64,
        now: u64,
        scene: &Scene,
        store: &Store,
    ) -> io::Result<()> {
        let actor = self
            .actors
            .get(&target)
            .filter(|actor| actor.health > 0)
            .ok_or_else(|| invalid("target is not an attackable creature"))?;
        if actor.evading {
            return Err(invalid("This creature is returning to its home."));
        }
        if !store
            .characters
            .get(&player)
            .is_some_and(|c| !c.deleted && c.progression.alive())
        {
            return Err(invalid("dead or unavailable character cannot attack"));
        }
        let category = crate::starter_gear::equipped_category(&store.characters[&player].inventory);
        let range = if category == 0 { self.range } else { 18.0 };
        let distance = scene.combat_distance(player, target)
            .ok_or_else(|| invalid("Target is not in a visible, shared area."))?;
        if distance > range {
            return Err(invalid(&format!(
                "Target is out of attack range ({distance:.1} m away; reach {range:.1} m). Move closer, or return to peace with Z to equip a ranged weapon."
            )));
        }
        if self.attacks.len() >= 64 && !self.attacks.contains_key(&player)
        {
            return Err(invalid("encounter attack capacity"));
        }
        self.cooldowns.retain(|_, until| *until > now);
        if self.cooldowns.len() >= 128 && !self.cooldowns.contains_key(&player) {
            return Err(invalid("attack cooldown capacity"));
        }
        let next = self.cooldowns.get(&player).copied().unwrap_or(now);
        // Repeated attack input must not restart a windup or an airborne shot.
        if let Some(queued) = self.attacks.get_mut(&player).filter(|a| a.target == target && a.category == category) {
            queued.repeat = true;
            return Ok(());
        }
        self.attacks.insert(
            player,
            Attack {
                target,
                next,
                repeat: true,
                category,
                stage: 0,
                started: next,
                skill: false,
                strike: None,
                periodic: false,
                splash: false,
            },
        );
        Ok(())
    }
    /// A racial basic attack starts the ordinary repeat chain. Only its first
    /// strike carries the requested skill; follow-ups retain normal cooldowns.
    pub fn basic_skill(
        &mut self,
        player: u64,
        target: u64,
        now: u64,
        scene: &Scene,
        store: &Store,
    ) -> io::Result<()> {
        if self.attacks.get(&player).is_some_and(|a| a.stage != 0) {
            return Err(invalid("An attack is already in progress."));
        }
        self.start(player, target, now, scene, store)?;
        self.attacks.get_mut(&player).unwrap().skill = true;
        Ok(())
    }
    fn stopped(&mut self, player: u64, events: &mut Vec<Event>) {
        self.attacks.remove(&player);
        events.push(Event::Stop(player));
    }
    pub fn tick(
        &mut self,
        now: u64,
        scene: &Scene,
        store: &mut Store,
        active: impl Iterator<Item = u64>,
        geometry: Option<&mut crate::geometry::Geometry>,
    ) -> io::Result<Vec<Event>> {
        self.tick_with_fellowships(now, scene, store, active, geometry, None)
    }

    pub(crate) fn tick_with_fellowships(
        &mut self,
        now: u64,
        scene: &Scene,
        store: &mut Store,
        active: impl Iterator<Item = u64>,
        mut geometry: Option<&mut crate::geometry::Geometry>,
        groups: Option<&crate::groups::Runtime>,
    ) -> io::Result<Vec<Event>> {
        if now < self.next_tick {
            return Ok(std::mem::take(&mut self.pending));
        }
        // Use elapsed time rather than assuming every scheduled 100ms tick ran
        // on time. Bound catch-up after a stall so no actor leaps across a zone.
        let elapsed=if self.next_tick==0 {0.1}else{(now.saturating_sub(self.next_tick.saturating_sub(100)).min(250) as f32)/1000.};
        self.next_tick = now.saturating_add(100);
        self.ratings.retain(|(_,_,_,until)|*until>now);
        self.regen.retain(|(who,_,_),(_,until)|*until>now && store.characters.get(who).is_some_and(|c|c.progression.alive()) && scene.player_placement(*who).is_some());
        self.ratings.retain(|(who,_,_,_)|store.characters.get(who).map_or_else(||self.actors.get(who).is_some_and(|a|a.health>0 && !a.evading),|c|c.progression.alive()));
        self.burns.retain(|b| b.next<=b.until && self.actors.get(&b.target).is_some_and(|a|a.health>0 && !a.evading) && store.characters.get(&b.source).is_some_and(|c|!c.deleted && c.progression.alive()) && scene.player_placement(b.source).is_some());
        let mut events = std::mem::take(&mut self.pending);
        events.extend(self.status_fx.expire(now,|_,target|
            store.characters.get(&target).map_or_else(
                ||self.actors.get(&target).is_some_and(|a|a.health>0 && !a.evading),
                |c|!c.deleted && c.progression.alive() && scene.player_placement(target).is_some()))
            .into_iter().map(Self::status_fx_event));
        let active=active.collect::<Vec<_>>();
        let quest_time = crate::recovered_quests::now();
        use crate::exploration_quests::Boon;
        let mut due = self
            .attacks
            .iter()
            .filter(|(_, attack)| attack.next <= now)
            .map(|(&id, attack)| (id, attack.clone(), false, None))
            .collect::<std::collections::VecDeque<_>>();
        for burn in &mut self.burns {
            let ticks = advance_periodic(&mut burn.next, burn.period, burn.until, now);
            if ticks > 0 {
                due.push_back((burn.source,Attack{target:burn.target,next:now,repeat:false,category:0,stage:2,started:now,skill:false,periodic:true,splash:false,
                    strike:Some(Strike{status_fx:Vec::new(),ratings:Vec::new(),skill:burn.skill,bonus:burn.per_tick.saturating_mul(ticks),vigor_damage:burn.vigor_per_tick.saturating_mul(ticks),weapon_damage:false,cost:0,range:0.,behavior:0,fx:0,burn:Vec::new()})},false,None));
            }
        }
        for (owner, (pet, target, rank)) in std::mem::take(&mut self.pet_strikes) {
            let close = scene.content_placement(pet).zip(scene.content_placement(target))
                .is_some_and(|(a,b)| a.distance(b).is_some_and(|d| d <= 3.)
                    && geometry.as_deref_mut().is_some_and(|world| world.clear(a,b,1.)));
            if !close || !active.contains(&owner) || !scene.can_observe(owner, pet) { continue; }
            // The Sand Fiend effect supplies natural damage of five per rank with 0.5 variance.
            let maximum = u32::from(rank) * 5;
            let spread = maximum / 2;
            let damage = maximum - spread + (self.dice.unit() * f64::from(spread + 1)) as u32;
            let strike = Strike { skill: 0x109, bonus: damage, vigor_damage: 0, weapon_damage: false,
                cost: 0, range: 30., behavior: 0, fx: 0, burn: Vec::new(), ratings: Vec::new(), status_fx: Vec::new() };
            let attack = Attack { target, next: now, stage: 2, started: now, repeat: false, category: 0,
                skill: false, periodic: false, splash: false, strike: Some(strike) };
            due.push_back((owner, attack, true, Some((pet,rank))));
        }
        while let Some((player, attack, secondary, pet)) = due.pop_front() {
            let initiating = !attack.periodic && !secondary;
            if initiating && !self.attacks.contains_key(&player) {
                continue;
            }
            let available = self
                .actors
                .get(&attack.target)
                .is_some_and(|actor| actor.health > 0 && !actor.evading);
            let alive = store
                .characters
                .get(&player)
                .is_some_and(|c| !c.deleted && c.progression.alive());
            if !available
                || !alive
                || (!attack.periodic && !scene.combat_distance(player, attack.target).is_some_and(|distance|
                    distance <= attack.strike.as_ref().map_or(if attack.category == 0 { self.range } else { 18.0 }, |s|s.range.max(self.range))))
            {
                if alive && !secondary {
                    events.push(Event::Note {
                        player,
                        text: if available {
                            "Attack stopped: move closer to your target."
                        } else {
                            "Attack stopped: your target is no longer available."
                        }.into(),
                    });
                }
                if initiating { self.stopped(player, &mut events); }
                continue;
            }
            if secondary && attack.strike.as_ref().is_some_and(|s|
                self.admit_effects(s.ratings.len(),s.burn.len(),0,0).is_err()
                    || self.admit_status_fx(s.status_fx.len()).is_err()) { continue; }
            if initiating { if let Some(world)=geometry.as_deref_mut() {
                let clear=scene.player_placement(player).zip(scene.content_placement(attack.target))
                    .is_some_and(|(a,b)|world.clear(a,b,1.));
                if !clear {events.push(Event::Note{player,text:"Attack stopped: your target is obstructed.".into()});
                    self.stopped(player,&mut events);continue;}
            }
            }
            if initiating && attack.strike.as_ref().is_some_and(|s|s.cost>0) && store.characters[&player].progression.vigor==0 {
                events.push(Event::Note{player,text:"Attack stopped: insufficient vigor.".into()});self.stopped(player,&mut events);continue;
            }
            if initiating && attack.category != 0 && attack.stage == 0 {
                let cost = attack.strike.as_ref().map_or(if attack.category == 2 { self.cost.max(6) } else { self.cost }, |s|s.cost.min(store.characters[&player].progression.vigor));
                if cost > 0 && store.characters[&player].progression.apply(&Command::SpendVigor(cost)).is_err() {
                    events.push(Event::Note { player, text: "Attack stopped: insufficient vigor.".into() });
                    self.stopped(player, &mut events);
                    continue;
                }
                if let Some(strike)=attack.strike.as_ref() { events.push(Event::SkillSwing{player,behavior:strike.behavior,skill:strike.skill}); } else { events.push(Event::Swing(player)); }
                let queued = self.attacks.get_mut(&player).unwrap();
                queued.started = now;
                queued.stage = 1;
                let race = store.characters[&player].creation.as_ref().map_or(1, |r| r.race);
                let behavior = attack.strike.as_ref().map_or(crate::combat_visual::MISSILE_AUTO, |s| s.behavior);
                queued.next = now.saturating_add(crate::projectiles::release_ms(race, attack.category, behavior));
                continue;
            }
            if initiating && attack.category != 0 && attack.stage == 1 {
                let race = store.characters[&player].creation.as_ref().map_or(1, |r| r.race);
                let presentation = attack.strike.as_ref().filter(|s|s.behavior!=0)
                    .map(|s|crate::skill_presentation::profile(s.skill));
                if let Some(p) = presentation.filter(|p|p.release_fx!=0) {
                    events.push(Event::Fx {target:player,fx:p.release_fx});
                }
                let projectile = if let Some(p) = presentation {p.projectile()}
                    else {crate::projectiles::profile(race, attack.category, attack.skill)};
                if let Some(profile) = projectile {
                    let distance = scene.combat_distance(player, attack.target).unwrap();
                    let flight_ms = crate::projectiles::flight_ms(distance);
                    events.push(Event::Projectile { player, target: attack.target, profile, flight_ms });
                    let queued = self.attacks.get_mut(&player).unwrap();
                    queued.stage = 2;
                    queued.next = now.saturating_add(flight_ms);
                    continue;
                }
            }
            let actor = &self.actors[&attack.target];
            // Authored starter balance; the client sends only a learned skill/target.
            let weapon=crate::professions::weapon_damage(&store.characters[&player].inventory).unwrap_or(self.damage);
            let base = if attack.category == 2 { weapon.saturating_add(4) } else { weapon };
            let raw_damage = attack.strike.as_ref().map_or(base, |s|s.bonus.saturating_add(if s.weapon_damage {base} else {0}));
            let damage = if raw_damage > 0 && initiating && store.characters[&player].quests.exploration.boon(Boon::Vengeance, quest_time).is_some() {
                raw_damage.saturating_add(crate::exploration_quests::VENGEANCE_DAMAGE)
            } else { raw_damage };
            let damage=damage.saturating_mul(crate::esper_camp::damage_multiplier(&store.characters[&player].inventory,actor.entity,attack.periodic || secondary,attack.strike.as_ref().is_none_or(|s|s.weapon_damage)));
            let armor=if attack.periodic {0} else {crate::combat::adjusted(actor.rule.armor,self.ratings_for(attack.target).armor)};
            let cost = attack.strike.as_ref().map_or(if attack.category == 2 { self.cost.max(6) } else { self.cost }, |s|s.cost.min(store.characters[&player].progression.vigor));
            let chance = if let Some((id, rank)) = pet {
                events.push(Event::IdolVisual(id, crate::combat_visual::IdolAction::Swing));
                crate::combat::hit_chance(u32::from(rank) * 8, crate::combat::adjusted(
                    actor.rule.level.unwrap_or(1) * 8, self.ratings_for(attack.target).creature_defense()))
            } else { self.direct_hit_chance(&store.characters[&player], attack.target, actor, attack.category) };
            let hit = attack.periodic || self.dice.roll(chance);
            let dealt = if !hit || damage == 0 {0} else {damage
                .saturating_sub(armor)
                .max(1)
                .min(actor.health)};
            let drained = if hit { attack.strike.as_ref().map_or(0, |s| s.vigor_damage).min(actor.vigor.unwrap_or(0)) } else { 0 };
            let killed = dealt == actor.health;
            let mut commands = if cost > 0 { vec![Command::SpendVigor(cost)] } else { Vec::new() };
            let (kill_xp, shares) = match (killed, groups) {
                (true, Some(groups)) => groups.split_kill_xp(player, actor.rule.xp, |id| {
                    active.contains(&id)
                        && scene.combat_distance(id, attack.target)
                            .is_some_and(|distance| distance <= crate::groups::XP_SHARE_RANGE)
                        && store.characters.get(&id).is_some_and(|c| {
                            !c.deleted && c.progression.alive()
                                && c.progression.xp <= (i64::MAX as u64).saturating_sub(actor.rule.xp)
                        })
                }),
                (true, None) => (actor.rule.xp, Vec::new()),
                (false, _) => (0, Vec::new()),
            };
            if kill_xp > 0 {
                commands.push(Command::AwardXp(kill_xp));
            }
            if killed && actor.rule.gold > 0 {
                commands.push(Command::AwardGold(actor.rule.gold));
            }
            let character = &store.characters[&player];
            let candidate = commands
                .iter()
                .try_fold(character.progression.clone(), |state, command| {
                    state.apply(command)
                });
            if candidate.is_err() {
                events.push(Event::Note {
                    player,
                    text: "Attack stopped: insufficient vigor or reward capacity.".into(),
                });
                if initiating { self.stopped(player, &mut events); }
                continue;
            }
            let account = character.account.clone();
            let previous = character.progression.clone();
            let previous_quests = character.quests.clone();
            let quest_drop=crate::shelter::drop(&character.quests,&character.inventory,actor.entity);
            let loot = if killed && quest_drop.is_some() {
                character
                    .inventory
                    .free_slot()
                    .filter(|_| character.inventory.dense_bags())
                    .and_then(|slot| {
                        store
                            .next_item_id()
                            .ok()
                            .and_then(|id| quest_drop.and_then(|definition| crate::shelter::item(definition, id, slot)))
                    })
                    .and_then(|item| {
                        scene
                            .admit_inventory_grant(player, item.id)
                            .ok()
                            .map(|instance| (item, instance))
                    })
            } else {
                None
            };
            let shared_previous: Vec<_> = shares.iter()
                .map(|&(id, xp)| (id, xp, store.characters[&id].progression.clone()))
                .collect();
            if let Some((item, instance)) = &loot {
                store.encounter_transaction_with_shares(
                    &account,
                    player,
                    actor.entity,
                    &[crate::inventory::Command::Grant(item.clone())],
                    &commands,
                    &shares,
                )?;
                events.push(Event::Item {
                    player,
                    item: item.clone(),
                    instance: *instance,
                });
            } else if killed {
                store.encounter_reward_with_shares(&account, player, actor.entity, &commands, &shares)?;
            } else if !commands.is_empty() {
                store.progression_batch(&account, player, &commands)?;
            }
            for (id, xp, previous) in shared_previous {
                events.push(Event::Owner {
                    player: id,
                    previous,
                    state: store.characters[&id].progression.clone(),
                });
                events.push(Event::Note { player: id, text: format!("Fellowship kill: earned {xp} XP.") });
            }
            if killed && quest_drop.is_some() && loot.is_none() {
                events.push(Event::Note{player,text:"Quest item not collected: free a backpack slot and defeat another Drudge.".into()});
            }
            let corpse_loot=killed && self.loot_enabled && quest_drop.is_none() && loot.is_none();
            if corpse_loot {events.push(Event::Loot{actor:attack.target,player,entity:actor.entity,level:actor.rule.level,home:actor.home,ai:actor.ai});}
            if store.characters[&player].quests != previous_quests {
                events.push(Event::Quest {
                    player,
                    previous: previous_quests,
                    book: store.characters[&player].quests.clone(),
                });
            }
            // The native hook hits each creature independently, even if the selected enemy evades.
            let native_radius = attack.strike.as_ref().and_then(|s| native_area_radius(s.skill));
            if let Some(radius) = native_radius.filter(|_| initiating) {
                let primary = attack.strike.as_ref().unwrap();
                let range = primary.range.max(self.range) + radius;
                for target in self.splash_targets(scene, geometry.as_deref_mut(), player, attack.target, range, radius, 32) {
                    let mut impact = primary.clone();
                    impact.cost = 0;
                    impact.range = range;
                    due.push_back((player, Attack { target, strike: Some(impact), repeat: false, splash: false, ..attack.clone() }, true, None));
                }
            } else if initiating && attack.splash && dealt > 0 && raw_damage >= 2 {
                let primary = attack.strike.as_ref().unwrap();
                let range = primary.range.max(self.range);
                let targets = self.splash_targets(scene, geometry.as_deref_mut(), player, attack.target, range, 4.0, 3);
                for target in targets {
                    let impact = Strike {
                        skill: primary.skill, bonus: raw_damage / 2, vigor_damage: 0,
                        weapon_damage: false, cost: 0, range, behavior: 0, fx: primary.fx,
                        burn: Vec::new(), status_fx: Vec::new(), ratings: Vec::new(),
                    };
                    due.push_back((player, Attack {
                        target, strike: Some(impact), repeat: false, splash: false, ..attack.clone()
                    }, true, None));
                }
            }
            if initiating {
            let until = if attack.category == 0 { now.saturating_add(self.interval) }
                else { attack.started.saturating_add(self.interval).max(now.saturating_add(100)) };
            self.cooldowns.insert(player, until);
            self.attacks.get_mut(&player).unwrap().next = until;
            self.attacks.get_mut(&player).unwrap().stage = 0;
            self.attacks.get_mut(&player).unwrap().skill = false;
                self.attacks.get_mut(&player).unwrap().strike=None;
                self.attacks.get_mut(&player).unwrap().splash=false;
            }
            if initiating && attack.category == 0 { if let Some(strike)=attack.strike.as_ref() {events.push(Event::SkillSwing{player,behavior:strike.behavior,skill:strike.skill});} else {events.push(Event::Swing(player));} }
            if initiating && attack.category == 0 {
                if let Some(p)=attack.strike.as_ref().filter(|s|s.behavior!=0).map(|s|crate::skill_presentation::profile(s.skill)).filter(|p|p.release_fx!=0) {
                    events.push(Event::Fx{target:player,fx:p.release_fx});
                }
            }
            // Only basic magic owns the racial impact cue. Named skills use their
            // own target FX; periodic damage must not replay a new impact every tick.
            if initiating && attack.strike.as_ref().is_none_or(|s|s.behavior==0 && s.fx==0) && attack.category==2 && dealt>0 {
                let race=store.characters[&player].creation.as_ref().map_or(1,|r|r.race);
                if let Some((_,fx))=crate::combat_visual::magic_effects(race) {
                    events.push(Event::Fx{target:attack.target,fx});
                }
            }
            if let Some(strike)=attack.strike.as_ref() {
                if !attack.periodic {
                    if strike.fx!=0 && !strike.status_fx.iter().any(|effect|effect.fx==strike.fx) {events.push(Event::Fx{target:attack.target,fx:strike.fx});}
                    if hit {
                        events.extend(self.status_fx.apply(player,attack.target,strike.skill,&strike.status_fx,now)?
                            .into_iter().map(Self::status_fx_event));
                    }
                    if hit { self.add_ratings(attack.target,strike.skill,&strike.ratings,now)?; }
                    if hit && !strike.burn.is_empty() {
                        self.burns.retain(|b|!(b.source==player && b.target==attack.target && b.skill==strike.skill));
                        for burn in &strike.burn {
                            if (burn.health > 0 || burn.vigor > 0) && burn.duration_ms > 0 {
                                self.burns.push(Burn{source:player,target:attack.target,skill:strike.skill,per_tick:burn.health,vigor_per_tick:burn.vigor,
                                    period:burn.period_ms.max(250),next:now.saturating_add(burn.period_ms.max(250)),until:now.saturating_add(burn.duration_ms)});
                            }
                        }
                    }
                }
            }
            let actor = self.actors.get_mut(&attack.target).unwrap();
            actor.health -= dealt;
            if drained > 0 {
                let vigor = actor.vigor.as_mut().unwrap();
                *vigor -= drained;
                events.push(Event::Vigor { actor: attack.target, vigor: *vigor });
            }
            if let Some(dealt) = i32::try_from(dealt).ok().filter(|value| *value > 0) {
                events.push(Event::Hit {
                    victim: attack.target,
                    attacker: pet.map_or(player, |(id, _)| id),
                    health_change: -dealt,
                    skill: attack.strike.as_ref().map(|s|s.skill),
                });
            }
            events.push(Event::Health {
                actor: attack.target,
                health: actor.health,
            });
            events.push(Event::Owner {
                player,
                previous,
                state: store.characters[&player].progression.clone(),
            });
            events.push(Event::Note {
                player,
                text: if pet.is_some() {
                    if hit { format!("Your Sand Fiend hits for {dealt} damage.") } else { "Your Sand Fiend missed.".into() }
                } else if !hit {"Your attack missed.".into()} else if drained > 0 {format!("You hit for {dealt} damage and drain {drained} vigor.")} else if dealt == 0 {"Your skill takes effect.".into()} else {format!("You hit for {dealt} damage.")},
            });
            if killed {
                actor.target = None;
                actor.route.clear();
                events.push(Event::Note {
                    player,
                    text: format!(
                        "Creature defeated. Earned {} XP and {} gold.",
                        kill_xp, actor.rule.gold
                    ),
                });
                // Death mode/behavior is shared by the native creature tables;
                // the receiving client selects that creature's own DAT graph.
                events.push(Event::IdolVisual(
                    attack.target,
                    crate::combat_visual::IdolAction::Death,
                ));
                let grace=crate::combat_visual::death_presentation_ms(actor.entity);
                if !self.spawners.died(attack.target,now,if corpse_loot{60000}else{grace}) {
                    return Err(invalid("defeated creature has no occupied spawner"));
                }
                let stopped = self
                    .attacks
                    .iter()
                    .filter(|(_, queued)| queued.target == attack.target)
                    .map(|(&player, _)| player)
                    .collect::<Vec<_>>();
                for player in stopped {
                    self.stopped(player, &mut events);
                }
            // The Straw Drudge family uses NPCTable_DrudgeStraw. These
            // practice dummies take damage but never retaliate.
            } else if actor.target.is_none() && !matches!(actor.entity, 0x470013af | 0x470026bd | 0x470026be) {
                actor.target = Some(player);
                if actor.patrol.is_some() {actor.engaged_at=scene.content_placement(attack.target);}
                actor.next_attack = now.saturating_add(actor.rule.interval);
                events.push(Event::IdolVisual(
                    attack.target,
                    crate::combat_visual::IdolAction::Combat,
                ));
            }
            if !attack.repeat && initiating {
                self.attacks.remove(&player);
            }
        }
        let mut spell_ratings=BTreeMap::<u64,crate::combat::Ratings>::new();
        for (who,_,rating,_) in &self.ratings {spell_ratings.entry(*who).or_default().add(rating.ratings);}
        for who in &active {
            if store.characters.get(who).and_then(|c| c.quests.exploration.boon(Boon::Ward, quest_time)).is_some() {
                spell_ratings.entry(*who).or_default().armor += crate::exploration_quests::WARD_ARMOR;
            }
        }
        let mut occupied=crate::geometry::Occupancy::default();
        for (&id,actor) in &self.actors {
            if actor.health>0 {if let Some(p)=scene.content_placement(id) {
                occupied.insert(id,p,actor.movement.radius,actor.movement.height);
            }}
        }
        for (&id, actor) in &mut self.actors {
            // A real removal may also come from a script. Refill the owned slot;
            // leaving the client's PVS never removes content from the server.
            if actor.health>0 && scene.content_placement(id).is_none() {
                actor.health=0;actor.target=None;actor.route.clear();
                self.spawners.removed(id,now);
            }
            if actor.health == 0 {
                actor.velocity=0.;
                continue;
            }
            if actor.target.is_none() && !actor.evading && now>=actor.next_idle {
                // Native detection radius, with a staggered 1s scan. Do not
                // load collision or roam distant, unobserved regions.
                actor.next_idle=now.saturating_add(1000);
                let nearby=active.iter().filter_map(|&player| {
                    let alive=store.characters.get(&player).is_some_and(|c|!c.deleted && c.progression.alive());
                    alive.then(||scene.combat_distance(player,id).map(|d|(player,d))).flatten()
                }).filter(|(_,distance)|*distance<=actor.ai.perception.max(actor.ai.wander_range+20.)
                    .max(if actor.patrol.is_some() {80.} else {0.}))
                  .collect::<Vec<_>>();
                // A patrolling creature leashes around where it currently is.
                let center=if actor.patrol.is_some() {scene.content_placement(id)} else {Some(actor.home)};
                if actor.ai.aggressive() {
                    let mut candidates=nearby.iter().filter(|(_,d)|*d<=actor.ai.perception)
                        .filter(|(p,_)|in_range(center,scene.player_placement(*p),actor.rule.leash))
                        .copied().collect::<Vec<_>>();
                    candidates.sort_by(|a,b|a.1.total_cmp(&b.1).then(a.0.cmp(&b.0)));
                    // A nearer player behind a wall must not mask a visible
                    // player who is also within the native perception radius.
                    let target=candidates.into_iter().find(|(player,_)|
                        scene.player_placement(*player).zip(scene.content_placement(id))
                            .is_some_and(|(a,b)|geometry.as_deref_mut().is_some_and(|world|world.clear(a,b,1.))));
                    if let Some((player,_))=target {
                        actor.target=Some(player);actor.wander_goal=None;
                        if actor.patrol.is_some() {actor.engaged_at=center;}
                        actor.next_attack=now.saturating_add(actor.rule.interval);
                        events.push(Event::IdolVisual(id,crate::combat_visual::IdolAction::Combat));
                    }
                }
                if let Some(patrol)=actor.patrol.as_ref().filter(|_|actor.target.is_none() && actor.wander_goal.is_none()
                    && actor.route.is_empty() && actor.pursuit && !nearby.is_empty() && now>=actor.patrol_wait) {
                    // Waypoints in order, then back to the spawn point; repeat.
                    let leg=actor.patrol_next%(patrol.waypoints.len()+1);
                    let [dx,dy]=patrol.waypoints.get(leg).copied().unwrap_or([0.,0.]);
                    actor.wander_goal=actor.home.translated([dx,dy,0.]);
                } else if actor.patrol.is_none() && actor.target.is_none() && actor.wander_goal.is_none() && actor.route.is_empty()
                    && actor.pursuit && actor.ai.wanders() && !nearby.is_empty()
                    && crate::creature_ai::random(&mut actor.random)<actor.ai.probability.unwrap_or(0.2) {
                    // Native range/probability; uniform disk destinations and
                    // 1s opportunities are explicitly AC2E scheduling policy.
                    let angle=crate::creature_ai::random(&mut actor.random)*std::f32::consts::TAU;
                    let radius=crate::creature_ai::random(&mut actor.random).sqrt()*actor.ai.wander_range.min(actor.rule.leash);
                    actor.wander_goal=actor.home.translated([angle.cos()*radius,angle.sin()*radius,0.]);
                }
            }
            let Some(mut target) = actor.target else {
                if let Some(point)=scene.content_placement(id) {
                    if let Some(goal)=actor.wander_goal {
                        if actor.route.len()<2048 {
                            let budget=actor.movement.peace.distance(&mut actor.velocity,elapsed);
                            let mut patrol_route=Vec::new();
                            let route=if actor.patrol.is_some(){&mut patrol_route}else{&mut actor.route};
                            if let Some(next)=advance(point,goal,0.2,budget,route,|from,to|
                                walk_clear(geometry.as_deref_mut(),&occupied,id,actor.movement,from,to)) {
                                occupied.reserve(id,point,next,actor.movement.radius,actor.movement.height);
                                events.push(Event::Move{actor:id,placement:next});continue;
                            }
                        }
                        actor.wander_goal=None;
                        actor.velocity=0.;
                        if let Some(patrol)=&actor.patrol {
                            // Arrived, or blocked: either way continue with the next leg.
                            actor.patrol_next=(actor.patrol_next+1)%(patrol.waypoints.len()+1);
                            actor.patrol_wait=now.saturating_add(patrol.pause_ms);
                        }
                    }
                    // A blocked return waits for geometry; never teleport or
                    // walk through an obstacle to restore an authored spawn.
                    let budget=actor.movement.peace.distance(&mut actor.velocity,elapsed);
                    if let Some(next)=retreat(point,budget,&mut actor.route,|from,to|
                        walk_clear(geometry.as_deref_mut(),&occupied,id,actor.movement,from,to)) {
                        occupied.reserve(id,point,next,actor.movement.radius,actor.movement.height);
                        events.push(Event::Move{actor:id,placement:next});
                    } else {actor.velocity=0.;}
                    if actor.route.is_empty(){actor.evading=false;actor.velocity=0.;actor.engaged_at=None;}
                }
                continue;
            };
            // Engagement and melee reach are separate. In particular, a ranged
            // hit across visible cells or a slope must not heal the victim on
            // the same tick merely because our bounded pursuit cannot reach it.
            let eligible = |player| {
                store
                    .characters
                    .get(&player)
                    .is_some_and(|c| !c.deleted && c.progression.alive())
                    && actor.route.len()<4096
                    && in_range(Some(actor.engaged_at.unwrap_or(actor.home)),scene.player_placement(player),actor.rule.leash)
                    && scene.combat_distance(player, id).is_some_and(|distance| distance <= actor.rule.leash)
            };
            if !eligible(target) {
                if let Some((&other, _)) = self
                    .attacks
                    .iter()
                    .find(|(player, attack)| attack.target == id && eligible(**player))
                {
                    target = other;
                    actor.target = Some(other);
                    actor.next_attack = now.saturating_add(actor.rule.interval);
                } else {
                    actor.target = None;
                    actor.evading=true;actor.wander_goal=None;actor.engaged_at=None;
                    events.push(Event::IdolVisual(
                        id,
                        crate::combat_visual::IdolAction::Peace,
                    ));
                    if actor.vigor != actor.rule.vigor {
                        actor.vigor = actor.rule.vigor;
                        if let Some(vigor) = actor.vigor { events.push(Event::Vigor { actor: id, vigor }); }
                    }
                    if actor.health != actor.rule.health {
                        actor.health = actor.rule.health;
                        events.push(Event::Health {
                            actor: id,
                            health: actor.health,
                        });
                    }
                    continue;
                }
            }
            // Apply homesickness before movement. Shared displacement rebases
            // adjacent blocks; native collision resolves the destination cell.
            if actor.pursuit {
                if let (Some(from),Some(player))=(scene.content_placement(id),scene.player_placement(target)) {
                    let budget=actor.movement.combat.distance(&mut actor.velocity,elapsed);
                    let step=advance(from,player,(actor.rule.range-0.5).max(0.5),budget,&mut actor.route,|from,to|
                        walk_clear(geometry.as_deref_mut(),&occupied,id,actor.movement,from,to));
                    if let Some(next)=step {
                        occupied.reserve(id,from,next,actor.movement.radius,actor.movement.height);
                        events.push(Event::Move{actor:id,placement:next});
                    } else {
                        actor.velocity=0.;
                        if let Some(turned)=facing(from,player).filter(|p|p.heading!=from.heading) {
                        // Targets can circle inside melee range. Turn in place
                        // without adding a fake walking step to the home route.
                        events.push(Event::Move{actor:id,placement:turned});
                        }
                    }
                }
            }
            if !scene.combat_distance(target, id).is_some_and(|distance| distance <= actor.rule.range)
                || now < actor.next_attack {
                continue;
            }
            if let Some(world)=geometry.as_deref_mut() {
                if !scene.player_placement(target).zip(scene.content_placement(id)).is_some_and(|(a,b)|world.clear(a,b,1.)){continue;}
            }
            let account = store.characters[&target].account.clone();
            let previous = store.characters[&target].progression.clone();
            // ArmorRating from Pyreal_DB_Insert_PlayerLevelData.sql. At least 1
            // damage remains so a hit is still a hit.
            let armor = previous.level().armor.saturating_add(crate::apparel::armor(&store.characters[&target].inventory,store.characters[&target].creation.as_ref().map_or(1,|r|r.race)));
            let offense=spell_ratings.get(&id).copied().unwrap_or_default();
            let protection=spell_ratings.get(&target).copied().unwrap_or_default();
            let armor=crate::combat::adjusted(armor,protection.armor);
            let attack=crate::combat::adjusted(actor.rule.level.unwrap_or(1)*8,offense.creature_attack());
            let defense=crate::combat::adjusted(crate::combat::player_defense(previous.level().number,&store.characters[&target].skills,crate::combat::School::Melee),protection.defense(crate::combat::School::Melee));
            if !self.dice.roll(crate::combat::hit_chance(attack,defense)) {
                actor.next_attack=now.saturating_add(actor.rule.interval);
                events.push(Event::IdolVisual(id,crate::combat_visual::IdolAction::Swing));
                events.push(Event::Hit{victim:target,attacker:id,health_change:0,skill:Some(6)});
                events.push(Event::Note{player:target,text:"The creature's attack misses you.".into()});continue;
            }
            let dealt = actor.rule.damage.saturating_sub(armor).max(1);
            store.progression_command(&account, target, Command::Damage(dealt))?;
            actor.next_attack = now.saturating_add(actor.rule.interval);
            events.push(Event::IdolVisual(
                id,
                crate::combat_visual::IdolAction::Swing,
            ));
            if let Ok(damage) = i32::try_from(dealt) {
                events.push(Event::Hit {
                    victim: target,
                    attacker: id,
                    health_change: -damage,
                    // Current creature combat is a normal melee attack. Native
                    // skill 6 (Melee Auto Attack) supplies its harmful flag.
                    skill: Some(6),
                });
            }
            let state = store.characters[&target].progression.clone();
            events.push(Event::Note {
                player: target,
                text: format!("Creature hits you for {dealt} damage."),
            });
            if !state.alive() {
                self.attacks.remove(&target);
                events.push(Event::Stop(target));
                events.push(Event::Note {
                    player: target,
                    text: "You have fallen. Use Resurrect to recover.".into(),
                });
            }
            events.push(Event::Owner {
                player: target,
                previous,
                state,
            });
        }
        self.tick_mends(now,scene,store,&mut events)?;
        if now >= self.next_regen {
            self.next_regen = now.saturating_add(self.regen_interval);
            let busy: BTreeSet<_> = self
                .attacks
                .keys()
                .copied()
                .chain(self.actors.values().filter_map(|actor| actor.target))
                .collect();
            for &player in &active {
                if scene.player_placement(player).is_none() || busy.contains(&player) {
                    continue;
                }
                let Some(character) = store
                    .characters
                    .get(&player)
                    .filter(|c| !c.deleted && c.progression.alive())
                else {
                    continue;
                };
                let state = &character.progression;
                let mut commands = Vec::new();
                let (h,v)=self.regen.iter().filter(|((who,_,_),_)|*who==player).fold((1f32,1f32),|(h,v),(_, (r,_))|((h*r.health).clamp(0.,100.),(v*r.vigor).clamp(0.,100.)));
                let health = (self.regen_health as f32*h).round() as u32;
                let vigor = (self.regen_vigor as f32*v).round() as u32;
                if health > 0 && state.health < state.max_health() {
                    commands.push(Command::Heal(health));
                }
                if vigor > 0 && state.vigor < state.max_vigor() {
                    commands.push(Command::RecoverVigor(vigor));
                }
                if commands.is_empty() {
                    continue;
                }
                let account = character.account.clone();
                let previous = character.progression.clone();
                store.progression_batch(&account, player, &commands)?;
                events.push(Event::Owner {
                    player,
                    previous,
                    state: store.characters[&player].progression.clone(),
                });
            }
        }
        // Reserve players and peaceful NPCs for spawn admission only. Combat
        // movement still uses its existing reach and collision rules.
        self.spawn_blockers.reserve(scene,store,&active,&mut occupied);
        for action in self.spawners.poll(now) {
            match action {
                crate::spawners::Action::Remove(id)=>events.push(Event::Despawn(id)),
                crate::spawners::Action::Spawn(id)=>{
                    let actor=&self.actors[&id];let body=actor.movement;
                    // A roaming neighbour can occupy a valid anchor while its
                    // owner is dead. Keep the point and its population slot;
                    // wait for clearance instead of spawning bodies together.
                    if !occupied.permits(id,actor.home,actor.home,body.radius,body.height) {
                        self.spawners.defer_spawn(id,now);
                        continue;
                    }
                    // Pending children in this same tick also occupy space,
                    // before the server publishes/acknowledges their lifetimes.
                    occupied.insert(id,actor.home,body.radius,body.height);
                    events.push(Event::Respawn{actor:id,health:actor.rule.health,vigor:actor.rule.vigor});
                },
            }
        }
        Ok(events)
    }
    /// Restore a player's vitals: once on the next tick when `duration_ms` is 0,
    /// otherwise every period until it expires. Recasting refreshes the effect.
    pub fn mend(
        &mut self,
        source: u64,
        skill: u32,
        player: u64,
        over: Over,
        fx: u32,
        now: u64,
    ) -> io::Result<()> {
        self.mend_batch(source, skill, player, &[over], fx, now)
    }

    /// Refresh all native periodic components together, preserving independent
    /// cadence and expiry. Failed admission leaves the previous bundle intact.
    pub fn mend_batch(&mut self, source: u64, skill: u32, player: u64,
        effects: &[Over], fx: u32, now: u64) -> io::Result<()> {
        if effects.iter().any(|over| over.health == 0 && over.vigor == 0) {
            return Err(invalid("empty restoration"));
        }
        let refresh = effects.iter().any(|over| over.duration_ms > 0);
        let replaces = |m: &Mend| refresh && m.source == source && m.skill == skill && m.player == player && !m.instant;
        let retained = self.mends.iter().filter(|m| !replaces(m)).count();
        if effects.len() > MAX_TIMED.saturating_sub(retained) {
            return Err(invalid("timed effect capacity"));
        }
        self.mends.retain(|m| !replaces(m));
        for (index, over) in effects.iter().enumerate() {
            let period = over.period_ms.max(250);
            self.mends.push(Mend {
            started: now,
            effect: over.effect,
            instant: over.duration_ms == 0,
            source,
            skill,
            player,
            health: over.health,
            vigor: over.vigor,
            period,
            next: if over.duration_ms == 0 {now} else {now.saturating_add(period)},
            until: now.saturating_add(over.duration_ms),
                fx: if index == 0 { fx } else { 0 },
            });
        }
        Ok(())
    }

    /// Replace this skill's complete bundle atomically. Each native effect keeps
    /// its own expiration; renewing one bundle never extends another skill.
    pub fn add_ratings(&mut self, entity: u64, skill: u32, effects: &[RatingEffect], now: u64) -> io::Result<()> {
        if entity == 0 || effects.is_empty() { return Ok(()); }
        let retained = self.ratings.iter().filter(|(who, id, _, until)| !(*who == entity && *id == skill) && *until > now).count();
        let incoming = effects.iter().filter(|e| !e.ratings.is_empty() && e.duration_ms > 0).count();
        if incoming > MAX_TIMED.saturating_sub(retained) {
            return Err(invalid("timed effect capacity"));
        }
        self.ratings.retain(|(who, id, _, until)| !(*who == entity && *id == skill) && *until > now);
        for &effect in effects {
            if !effect.ratings.is_empty() && effect.duration_ms > 0 {
                self.ratings.push((entity, skill, effect, now.saturating_add(effect.duration_ms)));
            }
        }
        Ok(())
    }
    fn ratings_for(&self, entity: u64) -> crate::combat::Ratings {
        let mut total = crate::combat::Ratings::default();
        for (who, _, ratings, _) in &self.ratings {
            if *who == entity {
                total.add(ratings.ratings);
            }
        }
        total
    }
    /// Multiply a player's regeneration until the buff expires.
    pub fn boost_regen(&mut self, player: u64, skill:u32, regen: Regen, now: u64) -> io::Result<()> {
        self.boost_regen_batch(player, skill, &[regen], now)
    }
    /// Refresh every native component together, preserving its own duration.
    /// Rejection must leave the existing bundle intact.
    pub fn boost_regen_batch(&mut self, player: u64, skill:u32, effects: &[Regen], now:u64) -> io::Result<()> {
        if effects.iter().any(|regen| !(regen.health.is_finite() && regen.vigor.is_finite())
            || regen.health < 0.0
            || regen.vigor < 0.0
            || regen.health > 100.0
            || regen.vigor > 100.0)
        {
            return Err(invalid("invalid regeneration buff"));
        }
        let previous=self.regen.keys().filter(|(who,which,_)|*who==player && *which==skill).count();
        if self.regen.len()-previous+effects.len()>MAX_TIMED {return Err(invalid("timed effect capacity"));}
        self.regen.retain(|(who,which,_),_|*who!=player || *which!=skill);
        for (index,regen) in effects.iter().copied().enumerate() {
            self.regen.insert((player,skill,index),(regen,now.saturating_add(regen.duration_ms)));
        }
        Ok(())
    }
    /// Build the owner's strip from the same ledgers that modify gameplay.
    pub fn player_effects(&self, player: u64, now: u64) -> crate::effects::Snapshot {
        let mut result = crate::effects::Snapshot::new();
        let mut include = |source, skill, definition, start, until| {
            if definition != 0 && until > now && until > start {
                result.insert((source, skill, definition), crate::effects::Lifetime { start, until });
            }
        };
        for &(entity, skill, effect, until) in &self.ratings {
            if entity == player {
                include(0, skill, effect.effect, until.saturating_sub(effect.duration_ms), until);
            }
        }
        for (&(entity, skill, _), &(effect, until)) in &self.regen {
            if entity == player {
                include(0, skill, effect.effect, until.saturating_sub(effect.duration_ms), until);
            }
        }
        for mend in &self.mends {
            if mend.player == player && !mend.instant {
                include(mend.source, mend.skill, mend.effect, mend.started, mend.until);
            }
        }
        result
    }
    fn tick_mends(
        &mut self,
        now: u64,
        scene: &Scene,
        store: &mut Store,
        events: &mut Vec<Event>,
    ) -> io::Result<()> {
        for mend in &mut self.mends {
            // An accepted one-shot restore waits for the next world update.
            let clock = if mend.instant { now.min(mend.until) } else { now };
            let ticks = advance_periodic(&mut mend.next, mend.period, mend.until, clock);
            if ticks == 0 {
                continue;
            }
            let Some(character) = store
                .characters
                .get(&mend.player)
                .filter(|c| !c.deleted && c.progression.alive())
            else {
                mend.until = 0;
                continue;
            };
            if scene.player_placement(mend.player).is_none() {
                continue;
            }
            if mend.fx != 0 {
                events.push(Event::Fx {
                    target: mend.player,
                    fx: std::mem::take(&mut mend.fx),
                });
            }
            let mut commands = Vec::new();
            if mend.health > 0 && character.progression.health < character.progression.max_health() {
                commands.push(Command::Heal(mend.health.saturating_mul(ticks)));
            }
            if mend.vigor > 0 && character.progression.vigor < character.progression.max_vigor() {
                commands.push(Command::RecoverVigor(mend.vigor.saturating_mul(ticks)));
            }
            if commands.is_empty() {
                continue;
            }
            let account = character.account.clone();
            let previous = character.progression.clone();
            store.progression_batch(&account, mend.player, &commands)?;
            let state = store.characters[&mend.player].progression.clone();
            let healed = state.health.saturating_sub(previous.health);
            if healed > 0 {
                if let Ok(healed) = i32::try_from(healed) {
                    events.push(Event::Hit {
                        victim: mend.player,
                        attacker: mend.source,
                        health_change: healed,
                        skill: Some(mend.skill).filter(|&skill| skill != 0),
                    });
                }
            }
            events.push(Event::Owner {
                player: mend.player,
                previous,
                state,
            });
        }
        self.mends.retain(|mend| {
            if mend.instant { mend.next <= mend.until } else { now < mend.until }
        });
        Ok(())
    }
    /// Queue recipient FX separately from restoration so buffs and HoTs also
    /// play once on application. The server dispatches with the recipient's stamp.
    pub fn queue_fx(&mut self, target:u64, fx:u32) {
        if fx!=0 && self.pending.len()<MAX_TIMED {self.pending.push(Event::Fx{target,fx});}
    }
    fn status_fx_event(action:crate::status_fx::Action)->Event {
        match action {
            crate::status_fx::Action::Play(target,fx)=>Event::Fx{target,fx},
            crate::status_fx::Action::Stop(target,fx)=>Event::StopFx{target,fx},
        }
    }
    pub fn admit_status_fx(&self, count:usize)->io::Result<()> {
        let pending:usize=self.attacks.values().filter_map(|a|a.strike.as_ref()).map(|s|s.status_fx.len()).sum();
        if self.status_fx.len().saturating_add(pending).saturating_add(count)>crate::status_fx::LIMIT {
            return Err(invalid("Timed visual capacity; try again shortly."));
        }
        Ok(())
    }
    pub fn apply_status_fx(&mut self, source:u64, skill:u32, effects:&[(u64,crate::status_fx::Effect)], now:u64)->io::Result<()> {
        self.pending.extend(self.status_fx.apply_batch(source,skill,effects,now)?.into_iter().map(Self::status_fx_event));
        Ok(())
    }
    pub fn visible_status_fx(&self, target: u64, now: u64) -> Vec<u32> {
        self.status_fx.snapshot(target, now)
    }
    /// Capacity is checked before any progression persistence or queue mutation.
    pub fn admit_effects(&self, rating_count: usize, burn_count: usize, mend_count: usize, regen_count:usize) -> io::Result<()> {
        let pending: usize = self.attacks.values().filter_map(|a|a.strike.as_ref()).map(|s|s.burn.len()).sum();
        let pending_ratings: usize = self.attacks.values().filter_map(|a| a.strike.as_ref()).map(|s| s.ratings.len()).sum();
        if self.burns.len().saturating_add(pending).saturating_add(burn_count)>MAX_TIMED || self.mends.len().saturating_add(mend_count)>MAX_TIMED || self.ratings.len().saturating_add(pending_ratings).saturating_add(rating_count)>MAX_TIMED || self.regen.len().saturating_add(regen_count)>MAX_TIMED {
            return Err(invalid("Timed effect capacity; try again shortly."));
        } Ok(())
    }
    pub fn allow_splash(&mut self, player: u64) {
        if let Some(attack) = self.attacks.get_mut(&player) {
            attack.splash = attack.strike.as_ref().is_some_and(|s| {
                (s.bonus > 0 || s.weapon_damage) && s.burn.is_empty() && s.status_fx.is_empty()
                    && s.ratings.is_empty() && s.vigor_damage == 0
            });
        }
    }

    fn splash_targets(&self, scene: &Scene, geometry: Option<&mut crate::geometry::Geometry>,
        player: u64, primary: u64, range: f32, radius: f32, limit: usize) -> Vec<u64> {
        let (Some(center), Some(caster)) = (scene.content_placement(primary), scene.player_placement(player))
            else { return Vec::new() };
        let mut nearby: Vec<_> = self.actors.iter().filter_map(|(&id, actor)| {
            if id == primary || actor.health == 0 || actor.evading { return None; }
            let point = scene.content_placement(id)?;
            let distance = center.distance(point)?;
            (distance <= radius && scene.combat_distance(player, id).is_some_and(|d| d <= range))
                .then_some((distance, id, point))
        }).collect();
        nearby.sort_by(|a, b| a.0.total_cmp(&b.0).then_with(|| a.1.cmp(&b.1)));
        let mut geometry = geometry;
        nearby.into_iter().filter_map(|(_, id, point)| {
            geometry.as_deref_mut().map_or(center.cell == point.cell && caster.cell == point.cell, |world| {
                world.clear(center, point, 1.0) && world.clear(caster, point, 1.0)
            }).then_some(id)
        }).take(limit).collect()
    }

    pub fn strike(&mut self, player:u64, target:u64, now:u64, scene:&Scene, store:&Store, strike:Strike) -> io::Result<()> {
        if !strike.range.is_finite() || strike.range<0. || !self.actors.get(&target).is_some_and(|a|a.health>0 && !a.evading) {
            return Err(invalid("Select a living attackable creature."));
        }
        if !store.characters.get(&player).is_some_and(|c|!c.deleted && c.progression.alive()) {
            return Err(invalid("The caster must be alive."));
        }
        if !scene.combat_distance(player,target).is_some_and(|d|d<=strike.range.max(self.range)) {
            return Err(invalid("Your target is out of skill range."));
        }
        if self.attacks.get(&player).is_some_and(|a|a.stage!=0) { return Err(invalid("Finish the current attack before casting.")); }
        self.admit_effects(strike.ratings.len(), strike.burn.len(), 0, 0)?;
        self.admit_status_fx(strike.status_fx.len())?;
        if self.attacks.len()>=64 && !self.attacks.contains_key(&player) {return Err(invalid("Attack capacity."));}
        let repeat=self.attacks.get(&player).is_some_and(|a|a.repeat && a.target==target);
        let category=crate::starter_gear::equipped_category(&store.characters[&player].inventory);
        self.attacks.insert(player,Attack{target,next:now,repeat,category,stage:0,started:now,skill:true,strike:Some(strike),periodic:false,splash:false});
        Ok(())
    }

}
