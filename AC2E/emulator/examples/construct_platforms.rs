//! Reproduce the 4DA2 native-sphere review without altering any placements.
use pyrealium::{content::Content,encounters::Encounters,placement_validation::verify_support};
use ac2_core::{CellId,DataId};
use ac2_physics::{Terrain,Position,Frame,Vector,Quaternion};
use serde_json::{json,Value};
use std::{collections::BTreeMap,path::Path,sync::Arc};

fn main()->Result<(),Box<dyn std::error::Error>> {
    let args=std::env::args().skip(1).collect::<Vec<_>>();
    if args.len()!=3{return Err("construct_platforms DAT WORLD REPORT".into());}
    let data=Path::new(&args[0]);let content=Content::load(Path::new(&args[1]),data)?;
    let manifest=std::fs::read_to_string(&args[1])?;
    let rows=manifest.lines().filter_map(|r|r.split_once('\t').map(|(id,_)|(id,r))).collect::<BTreeMap<_,_>>();
    let mut encounters=Encounters::load(Path::new("world"),&content)?;
    encounters.load_motion(&content,data)?;
    let dataset=ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::new(data))?;
    let store=Arc::new(ac2_db::ResourceStore::new(dataset));
    let world=ac2_world::Ac2WorldSource::open(Arc::clone(&store))?.unwrap();
    let mut terrain=Terrain::new();let mut terrain_only=Terrain::new();
    for x in 0x4c..=0x4e {for y in 0xa1..=0xa3 {
        let cell=CellId::new(x<<24|y<<16|65535);
        ac2_physics::load_terrain_block(&world,&mut terrain,cell)?;
        ac2_physics::load_terrain_block(&world,&mut terrain_only,cell)?;
        ac2_physics::load_block_interiors(&world,&mut terrain,cell)?;
        ac2_physics::load_block_objects(&world,&mut terrain,cell)?;
    }}
    let policy:Value=serde_json::from_str(include_str!("../content/placement-reviewed-v1.json"))?;
    let mut report=Vec::new();
    for id in [0x8140db030fc1c77d,0x814095bffa0c90b2,0x8140e8e67d64bd68,0x8140f5817d7c1aa4] {
        let key=format!("{id:016x}");
        let row=policy["verified"].as_array().into_iter().flatten()
            .chain(policy["unresolved"].as_array().into_iter().flatten()).find(|r|r["id"]==key).ok_or("Missing review decision")?;
        if rows.get(key.as_str()).copied()!=row["original"].as_str(){return Err(format!("{key}: original capture row changed").into());}
        let o=content.object(id).unwrap();let profile=encounters.movement_profile(id).unwrap();
        assert!(profile.grounded);assert_eq!(o.entity,0x47002760);assert_eq!(o.visual,0x1f001384);
        let mut shape=ac2_physics::load_body_shape(&store,DataId::new(o.visual))?.unwrap();
        shape.scale*=o.scale;
        let desc=ac2_world::load_ac2_motion_interp_desc(&store,DataId::new(profile.descriptor))?.unwrap();
        let desc=Arc::new((*desc).clone());
        let p=o.placement;let fields=row["original"].as_str().unwrap().split('\t').collect::<Vec<_>>();
        let q:Vec<f32>=fields[7..11].iter().map(|v|v.parse().unwrap()).collect();
        let pos=Position::new(p.cell,Frame::new(Vector::new(p.coordinates[0],p.coordinates[1],p.coordinates[2]),
            Quaternion::new(q[0],q[1],q[2],q[3])));
        let support=verify_support(&terrain,pos,shape.clone(),Arc::clone(&desc));
        let missing_platform=verify_support(&terrain_only,pos,shape.clone(),Arc::clone(&desc));
        let mut air=pos;air.frame.origin.z+=500.;
        let open_air=verify_support(&terrain,air,shape.clone(),Arc::clone(&desc));
        let mut buried=pos;buried.frame.origin.z-=1.;
        let penetration=verify_support(&terrain,buried,shape.clone(),desc);
        println!("{key}: native={} displacement={}m; controls missing platform={} air={} buried={}",
            support.accepted,support.max_displacement_m,missing_platform.accepted,open_air.accepted,penetration.accepted);
        let passed=support.accepted && !missing_platform.accepted && !open_air.accepted && !penetration.accepted;
        report.push(json!({"id":key,"original":row["original"],"passed":passed,
            "setup_radius_m":profile.radius,"body_height_m":shape.height,"total_scale":shape.scale,
            "spheres":shape.spheres.iter().map(|s|json!({"center":[s.center.x,s.center.y,s.center.z],"radius":s.radius})).collect::<Vec<_>>(),
            "support":support,"controls":{"missing_platform":missing_platform,"open_air":open_air,"buried":penetration}}));
    }
    std::fs::write(&args[2],serde_json::to_string_pretty(&report)?+"\n")?;
    if report.iter().any(|r|r["passed"]!=true){return Err("Native construct support/control verification failed".into());}
    Ok(())
}
