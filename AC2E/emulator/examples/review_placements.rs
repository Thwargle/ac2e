//! Author repairs for the initial flagged anchors without modifying source captures.
use pyrealium::{content::Content,creature_motion::Catalog,dat::Dat,encounters::Encounters,geometry::{Geometry,Occupancy},movement::Placement,services::Service};
use ac2_core::CellId;
use ac2_physics::{Terrain,LosParameters,Ray,Vector,Position,Frame,Quaternion,CellSource};
use serde_json::{json,Value};
use std::{path::Path,sync::Arc,collections::{BTreeMap,BTreeSet}};

fn position(p:Placement,up:f32)->Position {
    Position::new(p.cell,Frame::new(Vector::new(p.coordinates[0],p.coordinates[1],p.coordinates[2]+up),Quaternion::new(1.,0.,0.,0.)))
}
fn trace(terrain:&Terrain,p:Placement,up:f32,delta:Vector)->LosParameters {
    let mut ray=LosParameters::new();ray.set_ray(Ray::from_offset(position(p,up).frame.origin,delta),p.cell);
    terrain.los_check(&mut ray);ray
}
fn floor(terrain:&Terrain,p:Placement)->Option<Placement> {
    let ray=trace(terrain,p,0.45,Vector::new(0.,0.,-1.1));
    if ray.trace_result()!=1 || ray.impact_normal().z<0.57 || ray.delta()<0.001{return None;}
    let hit=ray.end_point();Some(Placement{cell:ray.final_cell_id(),coordinates:[hit.x,hit.y,hit.z],..p})
}
fn line(terrain:&Terrain,a:Placement,b:Placement,height:f32)->bool {
    let mut ray=LosParameters::new();ray.set_from_begin_and_end_position(&position(a,height),&position(b,height));
    terrain.los_check(&mut ray);ray.trace_result()==0
}
/// Follow the existing native floor in 25cm increments. The authoring-only
/// escape query may omit a prop proven to contain the original point, but no
/// other prop. The final body is always tested against the complete world.
fn route(full:&Terrain,escape:&Terrain,from:Placement,to:Placement,height:f32)->bool {
    if from.cell>>16!=to.cell>>16{return false;}
    let Some(mut prev)=floor(escape,from) else{return false};
    if (prev.coordinates[2]-from.coordinates[2]).abs()>0.65{return false;}
    let Some([dx,dy,_])=from.offset_to(to) else{return false};
    let steps=(dx.hypot(dy)/0.25).ceil().max(1.) as usize;
    let mut escaped=false;
    for step in 1..=steps {
        let t=step as f32/steps as f32;
        let Some(raw)=from.translated([dx*t,dy*t,prev.coordinates[2]-from.coordinates[2]]) else{return false};
        let Some(next)=floor(escape,raw) else{return false};
        if (next.coordinates[2]-prev.coordinates[2]).abs()>0.4 ||
            !line(escape,prev,next,0.5) || !line(escape,prev,next,(height-0.1).max(0.5)){return false;}
        let open=trace(full,next,0.25,Vector::new(0.,0.,0.01)).trace_result()==0;
        if escaped && !open{return false;}
        escaped|=open;prev=next;
    }
    escaped && (prev.coordinates[2]-to.coordinates[2]).abs()<0.05
}

fn main()->Result<(),Box<dyn std::error::Error>> {
    let a=std::env::args().skip(1).collect::<Vec<_>>();
    if a.len()!=4{return Err("review_placements DAT WORLD_TSV POLICY_JSON REPORT_JSON".into());}
    let content=Content::load(Path::new(&a[1]),Path::new(&a[0]))?;
    let text=std::fs::read_to_string(&a[1])?;
    let rows=text.lines().filter_map(|r|r.split_once('\t').map(|(id,_)|(id,r))).collect::<BTreeMap<_,_>>();
    let policy:Value=serde_json::from_str(&std::fs::read_to_string(&a[2])?)?;
    let mut runtime=Encounters::load(Path::new("world"),&content)?;
    runtime.load_motion(&content,Path::new(&a[0]))?;
    let mut geometry=Geometry::open(Path::new(&a[0]))?;
    let dataset=ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::new(&a[0]))?;
    let store=Arc::new(ac2_db::ResourceStore::new(dataset));
    let world=ac2_world::Ac2WorldSource::open(store)?.unwrap();
    let mut occupied=Occupancy::default();
    for o in content.objects(){if let Some(b)=runtime.movement_profile(o.id){occupied.insert(o.id,o.placement,b.radius,b.height);}}
    let protected=include_str!("../content/population-protected.txt").lines()
        .filter_map(|s|u32::from_str_radix(s,16).ok()).collect::<BTreeSet<_>>();
    let mut dat=Dat::open(Path::new(&a[0]).join("portal.dat"))?;let mut catalog=Catalog::default();
    for o in content.objects().filter(|o|runtime.movement_profile(o.id).is_none() &&
        (o.service==Some(Service::Vendor)||protected.contains(&o.entity))) {
        if let Some(b)=catalog.resolve(&mut dat,o.visual,o.scale)?.filter(|b|b.grounded) {
            occupied.insert(o.id,o.placement,b.radius,b.height);
        }
    }
    let mut repairs=Vec::new();let mut unresolved=Vec::new();let mut verified=Vec::new();
    for entry in policy["unresolved"].as_array().unwrap() {
        let id=u64::from_str_radix(entry["id"].as_str().unwrap(),16)?;
        let o=content.object(id).unwrap();let p=o.placement;
        let b=match runtime.movement_profile(id) {
            Some(b)=>b,
            None=>catalog.resolve(&mut dat,o.visual,o.scale)?.ok_or("Flagged actor has no native body")?,
        };
        let expected_cell=u32::from_str_radix(entry["cell"].as_str().unwrap(),16)?;
        let expected_position=std::array::from_fn(|i|entry["position"][i].as_f64().unwrap() as f32);
        if p.cell!=expected_cell || p.coordinates!=expected_position {
            return Err(format!("{id:016x}: input must contain the initially flagged placement, before the reviewed correction layer").into());
        }
        let mut terrain=Terrain::new();let block=CellId::new(p.cell|65535);
        ac2_physics::load_terrain_block(&world,&mut terrain,block)?;
        let terrain_z=terrain.terrain_height(&Position::new(p.cell,Frame::new(Vector::new(p.coordinates[0],p.coordinates[1],p.coordinates[2]),Quaternion::new(1.,0.,0.,0.))));
        ac2_physics::load_block_interiors(&world,&mut terrain,block)?;
        let bare=terrain.clone();
        ac2_physics::load_block_objects(&world,&mut terrain,block)?;
        let mut escape=terrain.clone();let mut containing=Vec::new();
        for &object in &terrain.land_cell(p.cell).unwrap().objects {
            let prop=terrain.object(object).unwrap();let mut isolated=bare.clone();isolated.insert_object(prop.clone());
            if [Vector::new(0.,0.,0.01),Vector::new(0.01,0.,0.)].into_iter().all(|d|{
                let background=trace(&bare,p,0.25,d);
                let r=trace(&isolated,p,0.25,d);
                background.trace_result()==0 && r.trace_result()==1 && r.delta()<0.001
            }) {
                let loc=prop.position().frame.origin;
                containing.push(json!({"static_index":object,"position":[loc.x,loc.y,loc.z]}));
                escape.remove_object(object);
            }
        }
        let mut probes=Vec::new();
        for up in [0.25,0.5,1.,2.,4.,8.,16.,32.] {
            let mut ray=LosParameters::new();
            ray.set_ray(Ray::from_offset(Vector::new(p.coordinates[0],p.coordinates[1],p.coordinates[2]+up),Vector::new(0.,0.,-up-32.)),p.cell);
            terrain.los_check(&mut ray);let hit=ray.end_point();let n=ray.impact_normal();
            probes.push(json!({"up":up,"result":ray.trace_result(),"fraction":ray.delta(),"z":hit.z,"normal":[n.x,n.y,n.z]}));
        }
        let mut edges=Vec::new();let span=b.radius*1.45+0.15;
        for i in 0..8 {
            let t=i as f32*std::f32::consts::FRAC_PI_4;
            let e=p.translated([t.cos()*b.radius,t.sin()*b.radius,0.]).unwrap();
            let floor=geometry.ground(e,span,span);
            edges.push(json!({"angle":i*45,"floor":floor.map(|f|f.coordinates),
                "low_clear":floor.map(|f|geometry.clear(p,f,(b.height*0.35).max(0.2))),
                "high_clear":floor.map(|f|geometry.clear(p,f,(b.height-0.1).max(0.1)))}));
        }
        let mut candidates=Vec::new();
        'search: for radius in [0.,0.25,0.5,1.,2.,3.,4.,5.,6.,8.,10.,12.,16.,20.] {
            for angle in 0..if radius==0.{1}else{64} {
                let t=angle as f32*std::f32::consts::TAU/64.;
                let Some(raw)=p.translated([radius*t.cos(),radius*t.sin(),0.]) else{continue};
                for up in [0.3,1.,2.,4.,8.] {
                    let Some(next)=geometry.ground(raw,up,8.) else{continue};
                    if p.distance(next).is_none_or(|d|d>20.) || !geometry.dry_ground(next) || !geometry.body_fits(next,b.radius,b.height) ||
                        !occupied.permits(id,next,next,b.radius,b.height){continue;}
                    let projected=Placement{coordinates:[p.coordinates[0],p.coordinates[1],next.coordinates[2]],..p};
                    let clear=geometry.clear(projected,next,0.5);
                    if !route(&terrain,&escape,p,next,b.height){continue;}
                    candidates.push(json!({"cell":format!("{:08x}",next.cell),"position":next.coordinates,"distance":p.distance(next),"clear_from_anchor":clear,"native_floor_route":true}));
                    if candidates.len()>=8 {break 'search;}
                    break;
                }
            }
        }
        candidates.sort_by(|a,b|a["distance"].as_f64().unwrap().total_cmp(&b["distance"].as_f64().unwrap()));
        let id_text=format!("{id:016x}");let original=rows[id_text.as_str()];
        let replacement=if let Some(candidate)=candidates.first() {
            let mut fields=original.split('\t').map(str::to_owned).collect::<Vec<_>>();
            fields[3]=candidate["cell"].as_str().unwrap().to_owned();
            for i in 0..3{fields[4+i]=format!("{:.6}",candidate["position"][i].as_f64().unwrap());}
            let next=Placement{cell:u32::from_str_radix(&fields[3],16)?,coordinates:std::array::from_fn(|i|fields[4+i].parse().unwrap()),..p};
            // Validate exactly the serialized coordinates, including rounding.
            assert!(geometry.body_fits(next,b.radius,b.height)&&geometry.dry_ground(next)&&
                occupied.permits(id,next,next,b.radius,b.height)&&route(&terrain,&escape,p,next,b.height));
            occupied.reserve(id,p,next,b.radius,b.height);
            Some(fields.join("\t"))
        }else{None};
        let native_support=if replacement.is_none() && geometry.dry_ground(p) && occupied.permits(id,p,p,b.radius,b.height) {
            geometry.native_support(p,o.visual,o.scale,b.descriptor)?.filter(|r|r.accepted)
        }else{None};
        println!("{:016x} {}: {} candidates, {}",id,o.name.as_deref().unwrap_or("?"),candidates.len(),
            if replacement.is_some(){"repair"}else if native_support.is_some(){"verified original native sphere support"}else{"retained for platform review"});
        let shape=ac2_physics::load_body_shape(world.resource_store(),ac2_core::DataId::new(o.visual))?;
        let reason=if native_support.is_some(){"Captured platform placement verified by native spheres without sliding or forcing, with stable walkable contact for 100 physics quanta; conservative flat footprint was a false positive"}
            else if replacement.is_none(){"No full-body candidate reachable on the same native floor within 20m; preserve the captured elevated platform placement"}
            else if !containing.is_empty(){"Escape a native static prop containing the original anchor, following the same terrain floor"}
            else if !geometry.dry_ground(p){"Move from submerged ground to nearby dry floor"}
            else{"Move along the same native floor to support the entire body footprint"};
        let result=json!({"id":entry["id"],"original":original,"replacement":replacement,"reason":reason,
            "native_support":native_support,
            "native_floor_route":replacement.is_some(),"distance_m":candidates.first().map(|c|&c["distance"]),
            "entity":format!("{:08x}",o.entity),"visual":format!("{:08x}",o.visual),"name":o.name,
            "native_body":format!("{shape:?}"),"native_peace_speed":b.peace.speed,"native_combat_speed":b.combat.speed,
            "cell":entry["cell"],"position":p.coordinates,"terrain_z":terrain_z,"radius":b.radius,"height":b.height,
            "ceiling_clear":geometry.clear(p,p.translated([0.,0.,b.height-0.05]).unwrap(),0.05),"containing_props":containing,
            "probes":probes,"edges":edges,"candidates":candidates});
        if replacement.is_some(){repairs.push(result);}else if native_support.is_some(){verified.push(result);}else{unresolved.push(result);}
    }
    std::fs::write(&a[3],serde_json::to_string_pretty(&json!({"schema":1,"native_ground_verified":true,"reviewed":repairs.len()+verified.len()+unresolved.len(),
        "scope":"Explicit review of the initial 26 unresolved outdoor anchors. Corrections stay in the same landblock, within 20m, connected by native floor samples every 25cm; final full body, dry ground and population separation are verified. Only static props proven to contain the original point may be omitted from the authoring escape route; all props remain in the destination clearance and the game world. This is a documented repair, not recovered retail spawn coordinates.",
        "native_sources":["Legions 7160 CPhysicsObj::GetRadius/GetHeight", "Legions 7160 CPartArray::TraceRayThroughPartsPhysics and Cylsphere::TraceRay", "Installed Legions CSetup, VisualDesc, CLandBlockData/Info and captured/recovered placement rows"],
        "repairs":repairs,"verified":verified,"unresolved":unresolved}))?+"\n")?;Ok(())
}
