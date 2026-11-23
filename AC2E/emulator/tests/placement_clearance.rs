use pyrealium::{content::Content,creature_motion::Catalog,dat::Dat,encounters::Encounters,geometry::{Geometry,Occupancy},movement::Placement,services::Service};
use serde_json::Value;
use std::path::Path;

fn placement(row:&str)->Placement {
    let fields=row.split('\t').collect::<Vec<_>>();
    Placement{cell:u32::from_str_radix(fields[3],16).unwrap(),
        coordinates:std::array::from_fn(|i|fields[4+i].parse().unwrap()),heading:0}
}

#[test]
fn repaired_live_manifest_has_native_support_and_separate_bodies() {
    let (Ok(data),Ok(manifest))=(std::env::var("AC2_TEST_GAME_DATA"),std::env::var("AC2_TEST_WORLD_CONTENT")) else{return};
    let content=Content::load(Path::new(&manifest),Path::new(&data)).unwrap();
    let mut encounters=Encounters::load(Path::new("world"),&content).unwrap();
    encounters.load_motion(&content,Path::new(&data)).unwrap();
    let mut geometry=Geometry::open(Path::new(&data)).unwrap();let mut occupied=Occupancy::default();
    for o in content.objects() {if let Some(b)=encounters.movement_profile(o.id) {occupied.insert(o.id,o.placement,b.radius,b.height);}}
    let protected=include_str!("../content/population-protected.txt").lines()
        .filter_map(|s|u32::from_str_radix(s,16).ok()).collect::<std::collections::BTreeSet<_>>();
    let mut dat=Dat::open(Path::new(&data).join("portal.dat")).unwrap();let mut catalog=Catalog::default();
    for o in content.objects().filter(|o|encounters.movement_profile(o.id).is_none() &&
        (o.service==Some(Service::Vendor)||protected.contains(&o.entity))) {
        if let Some(b)=catalog.resolve(&mut dat,o.visual,o.scale).unwrap().filter(|b|b.grounded) {
            occupied.insert(o.id,o.placement,b.radius,b.height);
        }
    }
    let policy:Value=serde_json::from_str(include_str!("../content/placement-clearance-v1.json")).unwrap();
    let review:Value=serde_json::from_str(include_str!("../content/placement-reviewed-v1.json")).unwrap();
    let rows=policy["repairs"].as_array().unwrap().iter().chain(review["repairs"].as_array().unwrap());
    assert_eq!(rows.clone().count(),204);
    for row in rows {
        let id=u64::from_str_radix(row["id"].as_str().unwrap(),16).unwrap();
        let o=content.object(id).expect("Repair must be installed in merged manifest");
        let b=encounters.movement_profile(id).unwrap_or_else(||catalog.resolve(&mut dat,o.visual,o.scale).unwrap().unwrap());
        if id==0x8140b05f1adb25fe {
            assert!(!encounters.is_creature(id),"Grounding must not turn the Mimbu Shaman into an enemy");
        }
        let expected=row["replacement"].as_str().unwrap().split('\t').collect::<Vec<_>>();
        assert_eq!(o.placement.cell,u32::from_str_radix(expected[3],16).unwrap());
        for i in 0..3 {assert_eq!(o.placement.coordinates[i],expected[4+i].parse::<f32>().unwrap());}
        assert!(geometry.body_fits(o.placement,b.radius,b.height),"{id:016x} has full native floor/body clearance");
        assert!(geometry.dry_ground(o.placement),"{id:016x} must not be underwater");
        assert!(occupied.permits(id,o.placement,o.placement,b.radius,b.height),"{id:016x} must be separate after all repairs");
        assert_eq!(o.entity,u32::from_str_radix(expected[1],16).unwrap());
    }
    for row in review["verified"].as_array().unwrap() {
        let id=u64::from_str_radix(row["id"].as_str().unwrap(),16).unwrap();
        let o=content.object(id).unwrap();let b=encounters.movement_profile(id).unwrap();
        assert!(geometry.native_body_fits(o.placement,o.visual,o.scale,b.descriptor).unwrap());
        assert!(geometry.dry_ground(o.placement));
        assert!(occupied.permits(id,o.placement,o.placement,b.radius,b.height),"Verified native support must not bypass living-body separation");
    }
}

#[test]
fn reviewed_anchors_escape_props_without_dropping_elevated_creatures_to_terrain() {
    let (Ok(data),Ok(manifest))=(std::env::var("AC2_TEST_GAME_DATA"),std::env::var("AC2_TEST_WORLD_CONTENT")) else{return};
    let content=Content::load(Path::new(&manifest),Path::new(&data)).unwrap();
    let mut encounters=Encounters::load(Path::new("world"),&content).unwrap();
    encounters.load_motion(&content,Path::new(&data)).unwrap();
    let mut geometry=Geometry::open(Path::new(&data)).unwrap();
    let review:Value=serde_json::from_str(include_str!("../content/placement-reviewed-v1.json")).unwrap();
    assert_eq!(review["repairs"].as_array().unwrap().len(),22);
    assert_eq!(review["unresolved"].as_array().unwrap().len(),0);
    assert_eq!(review["verified"].as_array().unwrap().len(),4);
    let mut escaped=0;
    for row in review["repairs"].as_array().unwrap() {
        let old=placement(row["original"].as_str().unwrap());
        let next=placement(row["replacement"].as_str().unwrap());
        assert_eq!(old.cell>>16,next.cell>>16);
        assert!(old.distance(next).unwrap()<=20.);
        assert!(geometry.contact(next));
        if !row["containing_props"].as_array().unwrap().is_empty() {
            assert!(!geometry.contact(old),"{}: penetration is not floor support",row["id"]);
            escaped+=1;
        }
        if row["id"]=="814090d50125d967" {
            assert!((old.coordinates[2]-next.coordinates[2]).abs()<0.05,"Stay on the construct's platform");
            assert!(next.coordinates[2]>row["terrain_z"].as_f64().unwrap() as f32+70.);
        }
    }
    assert_eq!(escaped,13,"Native static-prop penetration regressions");
    for row in review["verified"].as_array().unwrap() {
        let id=u64::from_str_radix(row["id"].as_str().unwrap(),16).unwrap();
        let o=content.object(id).unwrap();let b=encounters.movement_profile(id).unwrap();
        let original=placement(row["original"].as_str().unwrap());
        assert_eq!(o.placement.cell,original.cell);
        assert_eq!(o.placement.coordinates,original.coordinates);
        assert_eq!(o.entity,0x47002760);
        assert!(geometry.contact(original),"The original platform exists under the center");
        assert!(!geometry.body_fits(original,b.radius,b.height),"Regression: flat bounding-circle support is stricter than native spheres");
        assert!(original.coordinates[2]>row["terrain_z"].as_f64().unwrap() as f32+10.);
    }
}

#[test]
fn constructs_require_their_native_platforms_and_reject_air_and_penetration() {
    use ac2_core::{CellId,DataId};
    use ac2_physics::{Terrain,Position,Frame,Vector,Quaternion};
    use pyrealium::placement_validation::{verify_support,SUPPORT_QUANTA};
    use std::sync::Arc;
    let (Ok(data),Ok(manifest))=(std::env::var("AC2_TEST_GAME_DATA"),std::env::var("AC2_TEST_WORLD_CONTENT")) else{return};
    let content=Content::load(Path::new(&manifest),Path::new(&data)).unwrap();
    let mut encounters=Encounters::load(Path::new("world"),&content).unwrap();
    encounters.load_motion(&content,Path::new(&data)).unwrap();
    let store=Arc::new(ac2_db::ResourceStore::new(ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::new(&data)).unwrap()));
    let world=ac2_world::Ac2WorldSource::open(Arc::clone(&store)).unwrap().unwrap();
    let mut complete=Terrain::new();let mut no_platforms=Terrain::new();
    for x in 0x4c..=0x4e {for y in 0xa1..=0xa3 {
        let cell=CellId::new(x<<24|y<<16|65535);
        ac2_physics::load_terrain_block(&world,&mut complete,cell).unwrap();
        ac2_physics::load_terrain_block(&world,&mut no_platforms,cell).unwrap();
        ac2_physics::load_block_interiors(&world,&mut complete,cell).unwrap();
        ac2_physics::load_block_objects(&world,&mut complete,cell).unwrap();
    }}
    let review:Value=serde_json::from_str(include_str!("../content/placement-reviewed-v1.json")).unwrap();
    let ai=pyrealium::creature_ai::catalog().unwrap();
    assert_eq!(ai[&0x47002760].wandering,Some(false));
    assert!(!ai[&0x47002760].wanders());
    for row in review["verified"].as_array().unwrap() {
        let id=u64::from_str_radix(row["id"].as_str().unwrap(),16).unwrap();
        let o=content.object(id).unwrap();let b=encounters.movement_profile(id).unwrap();
        assert_eq!((o.entity,o.visual,o.scale),(0x47002760,0x1f001384,1.));
        let mut shape=ac2_physics::load_body_shape(&store,DataId::new(o.visual)).unwrap().unwrap();
        shape.scale*=o.scale;
        assert_eq!(shape.spheres.len(),2);
        assert!((shape.spheres[0].radius-2.5204158).abs()<0.00001);
        assert!(b.radius>shape.spheres[0].radius+1.,"Setup radius is not the native collision-sphere radius");
        let motion=ac2_world::load_ac2_motion_interp_desc(&store,DataId::new(b.descriptor)).unwrap().unwrap();
        let motion=Arc::new((*motion).clone());
        let fields=row["original"].as_str().unwrap().split('\t').collect::<Vec<_>>();
        let q:Vec<f32>=fields[7..11].iter().map(|s|s.parse().unwrap()).collect();
        let p=o.placement;let position=Position::new(p.cell,Frame::new(Vector::new(p.coordinates[0],p.coordinates[1],p.coordinates[2]),
            Quaternion::new(q[0],q[1],q[2],q[3])));
        let proof=verify_support(&complete,position,shape.clone(),Arc::clone(&motion));
        assert!(proof.accepted,"{id:016x}: {proof:?}");
        assert_eq!(proof.physics_quanta,SUPPORT_QUANTA);
        assert_eq!(proof.max_displacement_m,0.);
        assert_eq!(proof.placement_flags,0);
        assert_eq!(proof.placed.position,p.coordinates);
        assert_eq!(proof.settled.position,p.coordinates);
        assert!(!verify_support(&no_platforms,position,shape.clone(),Arc::clone(&motion)).accepted,
            "{id:016x}: terrain far below the capture is not the supporting platform");
        for z in [500.,-1.] {
            let mut bad=position;bad.frame.origin.z+=z;
            assert!(!verify_support(&complete,bad,shape.clone(),Arc::clone(&motion)).accepted,
                "{id:016x}: air/penetration must fail even if placement can fall/step to a later floor");
        }
        let mut bad=shape;bad.spheres.clear();
        assert!(!verify_support(&complete,position,bad,motion).accepted,"Missing native shape must fail closed");
    }
}
