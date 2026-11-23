//! Resolve inferred guide camps onto native ground. Source anchors remain in JSON.
use pyrealium::{geometry::Geometry,map_travel::Landscape,movement::Placement};
use serde_json::{json,Value};
use sha2::{Digest,Sha256};
use std::{collections::BTreeSet,path::Path};

// A walkable point can still leave a rigid workshop crossing a steep hillside.
fn workshop_footprint(store: &ac2_db::ResourceStore, visual: u32) -> Result<Vec<[f32; 2]>, Box<dyn std::error::Error>> {
    use ac2_assets::{Ac2Mesh, resolve_ac2_visual_meshes};
    use ac2_core::DataId;
    use ac2_dat::DatFileKind;
    use ac2_db::TypedResourceRef;
    let mut points = BTreeSet::new();
    for (mesh_id, transform) in resolve_ac2_visual_meshes(store, DataId::new(visual))? {
        let mesh = store.load_cached_packed::<Ac2Mesh>(TypedResourceRef::new(DatFileKind::Portal, mesh_id, 0))?
            .ok_or("missing workshop mesh")?;
        for vertex in mesh.vertices.iter() {
            let p = transform.apply(vertex.position);
            if p.z <= 0.25 {
                points.insert(((p.x * 2.).round() as i32, (p.y * 2.).round() as i32));
            }
        }
    }
    if points.is_empty() { return Err("missing workshop foundation".into()); }
    Ok(points.into_iter().map(|(x,y)| [x as f32 / 2., y as f32 / 2.]).collect())
}

fn supports_workshop(geometry: &mut Geometry, center: Placement, footprint: &[[f32; 2]], occupied: &[Placement]) -> bool {
    if !geometry.body_fits(center, 1.8, 2.5) {
        return false;
    }
    let mut low = center.coordinates[2];
    let mut high = low;
    let mut approaches = 0;
    for step in 0..16 {
        let angle = step as f32 * std::f32::consts::TAU / 16.;
        let sample = |radius| center.translated([radius * angle.cos(), radius * angle.sin(), 0.]);
        let Some(edge) = sample(2.).and_then(|p| geometry.ground(p, 1., 1.)) else { return false };
        low = low.min(edge.coordinates[2]);
        high = high.max(edge.coordinates[2]);
        if high - low > 0.6 {
            return false;
        }
        if sample(3.5).and_then(|p| geometry.ground(p, 1.5, 1.5)).is_some_and(|p|
            geometry.body_fits(p, 0.35, 1.8) && geometry.clear(p, center, 1.)) {
            approaches += 1;
        }
    }
    if approaches < 8 { return false; }
    let mut min_x = f32::INFINITY;
    let mut max_x = f32::NEG_INFINITY;
    let mut min_y = f32::INFINITY;
    let mut max_y = f32::NEG_INFINITY;
    for &[x,y] in footprint {
        min_x = min_x.min(x);
        max_x = max_x.max(x);
        min_y = min_y.min(y);
        max_y = max_y.max(y);
        let Some(ground) = center.translated([x,y,0.]).and_then(|p| geometry.ground(p, 2., 4.)) else { return false };
        low = low.min(ground.coordinates[2]);
        high = high.max(ground.coordinates[2]);
        if high - low > 1. || !ground.translated([0.,0.,3.]).is_some_and(|p| geometry.clear(ground, p, 0.25)) {
            return false;
        }
    }
    !occupied.iter().filter_map(|&p| center.offset_to(p)).any(|[x,y,z]| {
        z.abs() < 3. && x >= min_x - 0.7 && x <= max_x + 0.7 && y >= min_y - 0.7 && y <= max_y + 0.7
    })
}

fn main()->Result<(),Box<dyn std::error::Error>> {
    let a=std::env::args().skip(1).collect::<Vec<_>>();
    if a.len()!=5{return Err("population_authoring DAT JSON OUTPUT_TSV REPORT_JSON EXISTING_WORLD_TSV".into());}
    let bytes=std::fs::read(&a[1])?;let data:Value=serde_json::from_slice(&bytes)?;
    let mut own_ids: BTreeSet<u64> = data["npcs"].as_array().ok_or("npcs")?.iter()
        .filter_map(|row|row["id"].as_u64()).collect();
    for camp in data["camps"].as_array().ok_or("camps")? {
        let id=camp["id"].as_u64().ok_or("camp id")?;
        for slot in 0..camp["roster"].as_array().ok_or("roster")?.len() {
            own_ids.insert(id.checked_add(slot as u64).ok_or("camp identity overflow")?);
        }
    }
    if let Some(interiors)=data["interiors"].as_array() {
        own_ids.extend(interiors.iter().filter_map(|row|row["id"].as_u64()));
    }
    if let Some(portals) = data["portals"].as_array() {
        own_ids.extend(portals.iter().filter_map(|row| row["id"].as_u64()));
    }
    let mut land=Landscape::load(Path::new(&a[0]))?;let mut geometry=Geometry::open(Path::new(&a[0]))?;
    let captured_bodies=data["validate_captured_bodies"].as_bool().unwrap_or(false);
    let validate_interior_bodies=captured_bodies || data["validate_interior_bodies"].as_bool().unwrap_or(false);
    let portals_only = data["portals"].as_array().is_some_and(|rows| !rows.is_empty())
        && ["npcs", "camps", "interiors"].iter().all(|key| data[*key].as_array().is_none_or(Vec::is_empty));
    let note=if portals_only {"# Captured portal positions and facing. Native player approach validated."}
        else if captured_bodies && data["camps"].as_array().is_some_and(|entries|!entries.is_empty()) {"# Captured dungeon placements and inferred outdoor homes. Native floor and body clearance verified."}
        else if captured_bodies && data["npcs"].as_array().is_some_and(|entries| !entries.is_empty()) {"# Captured creature positions and guide-based quest NPC positions. Native ground validated."}
        else if captured_bodies {"# Recorded creature positions and facing. Native floor and body clearance verified."}
        else {"# Inferred guide placements. Ground/collision validated; not exact retail coordinates."};
    let mut lines=vec!["pyrealium-world-v4".to_owned(),note.to_owned()];
    let mut habitat_camps=Vec::new();
    let mut groups=vec![];let mut failures=vec![];let mut occupied:Vec<Placement>=vec![];let mut ids=BTreeSet::new();
    let mut floor_repairs=vec![];
    let mut native_platforms=vec![];
    // Existing captures/services take priority. Ignore this pack's previous
    // output so regeneration is deterministic against the same source world.
    let existing=std::fs::read_to_string(&a[4])?;
    let mut base_lines=Vec::new();
    for line in existing.lines() {
        let f=line.split('\t').collect::<Vec<_>>();
        if f.len()<7 || !f[0].starts_with("8140") {continue;}
        if own_ids.contains(&u64::from_str_radix(f[0],16)?) {continue;}
        let cell=u32::from_str_radix(f[3],16)?;
        if (1..=64).contains(&(cell&65535)) || cell&65535>=256 {
            occupied.push(Placement{cell,coordinates:[f[4].parse()?,f[5].parse()?,f[6].parse()?],heading:0});
            if cell&65535<256 {
                base_lines.push(line);
                if f[0].starts_with("8140c000") || f[0].starts_with("8140c100") {
                    habitat_camps.push(*occupied.last().unwrap());
                }
            }
        }
    }
    let mut append=|id:u64,entity:u32,p:Placement,frame:Option<&Value>|->Result<(),Box<dyn std::error::Error>> {
        if !ids.insert(id){return Err("inferred placement identity collision".into());}
        let t=&data["templates"][entity.to_string()];let visual=t["visual"].as_u64().ok_or("visual")?;
        let name=frame.and_then(|f|f["name"].as_str()).or_else(||t["name"].as_str()).ok_or("name")?.replace(['\t','\n','\r']," ");
        let orientation=if let Some(frame)=frame.filter(|v|v.get("rotation").is_some()) {
            let q=frame["rotation"].as_array().filter(|q|q.len()==4).ok_or("capture quaternion")?;
            let mut values=Vec::new();
            for x in q {
                let n=x.as_f64().filter(|n|n.is_finite()&&n.abs()<=1.001).ok_or("quaternion component")?;
                values.push(n.to_string());
            }
            values.push(frame["scale"].as_f64().map_or("-".to_owned(),|n|n.to_string()));
            values.join("\t")
        } else {"1\t0\t0\t0\t-".to_owned()};
        lines.push(format!("{id:016x}\t{entity:08x}\t{visual:08x}\t{:08x}\t{:.6}\t{:.6}\t{:.6}\t{orientation}\t{name}",p.cell,p.coordinates[0],p.coordinates[1],p.coordinates[2]));Ok(())
    };
    let workshop_store = if data["npcs"].as_array().unwrap().iter().any(|n| n.get("foundation_offset_m").is_some()) {
        let dataset = ac2_dat::DatDataset::open(ac2_dat::DatOpenOptions::new(Path::new(&a[0])))?;
        Some(ac2_db::ResourceStore::new(dataset))
    } else { None };
    let mut camp_bodies=pyrealium::creature_motion::Catalog::default();
    let mut camp_dat=if data["validate_camp_bodies"].as_bool()==Some(true) {
        Some(pyrealium::dat::Dat::open(Path::new(&a[0]).join("portal.dat"))?)
    } else {None};
    for (kind,entries) in [("camp",&data["camps"]),("npc",&data["npcs"])] {
        for entry in entries.as_array().ok_or("entries")? {
            let id=entry["id"].as_u64().ok_or("id")?;
            let center=[entry["land"][0].as_f64().ok_or("x")?,entry["land"][1].as_f64().ok_or("y")?];
            let roster=if kind=="camp" {entry["roster"].as_array().unwrap().iter().map(|v|v.as_u64().unwrap() as u32).collect::<Vec<_>>()} else {vec![entry["entity"].as_u64().unwrap() as u32]};
            let roaming=entry.get("roaming_homes").map(|v|v.as_array().ok_or("roaming homes")).transpose()?;
            if let Some(homes)=roaming {
                if kind!="camp" || entry["capacity"].as_u64()!=Some(1) || homes.len()!=roster.len() || homes.len()<2
                    || roster.iter().any(|entity|*entity!=roster[0]) || camp_dat.is_none() {
                    return Err("roaming home group needs one body-validated template at two or more locations".into());
                }
            }
            let mut accepted=vec![];let mut origin=None;
            for (slot,entity) in roster.into_iter().enumerate() {
                // Stationary quest props have a mesh foundation instead of a creature capsule.
                let workshop = entry.get("foundation_offset_m").filter(|_| kind=="npc");
                let body=if let Some(dat)=camp_dat.as_mut().filter(|_| workshop.is_none()) {
                    let visual=data["templates"][entity.to_string()]["visual"].as_u64().ok_or("camp visual")? as u32;
                    let scale=entry["scale"].as_f64().unwrap_or(1.) as f32;
                    Some(camp_bodies.resolve(dat,visual,scale)?.ok_or("camp body missing")?)
                } else {None};
                let center=if let Some(homes)=roaming {
                    [homes[slot][0].as_f64().ok_or("roaming east coordinate")?, homes[slot][1].as_f64().ok_or("roaming north coordinate")?]
                } else {center};
                let mut found=None;
                // Guide coordinates are rounded. Footprint-audited offsets use metres
                // and must be validated in place to avoid moving a building onto an NPC.
                let offsets: Vec<(f64, f64)> = if let Some(offset) = workshop {
                    vec![(offset[0].as_f64().ok_or("workshop east offset")?,
                          offset[1].as_f64().ok_or("workshop north offset")?)]
                } else {
                    (0..64).map(|attempt| {
                        let radius=if (slot==0 || roaming.is_some()) && attempt==0 {0.}else{7.+((attempt+slot*7)%26) as f64};
                        let angle=(attempt+slot*17) as f64*2.3999632297;
                        (angle.cos()*radius, angle.sin()*radius)
                    }).collect()
                };
                let footprint = if workshop.is_some() {
                    let visual = data["templates"][entity.to_string()]["visual"].as_u64().ok_or("workshop visual")? as u32;
                    Some(workshop_footprint(workshop_store.as_ref().ok_or("missing workshop DAT")?, visual)?)
                } else { None };
                for (dx, dy) in offsets {
                    let xy=[center[0]+dx/20.,center[1]+dy/20.];
                    let Ok(candidate)=land.destination(xy,0) else{continue};
                    let Some(p)=geometry.ground(candidate,0.5,1.0) else{continue};
                    if p.cell&65535>64 || !geometry.dry_ground(p) || occupied.iter().any(|old|old.distance(p).is_some_and(|d|d<6.)) || !geometry.contact(p){continue;}
                    if body.is_some_and(|b|!geometry.body_fits(p,b.radius,b.height)){continue;}
                    if roaming.is_some() && habitat_camps.iter().any(|old|old.offset_to(p)
                        .is_some_and(|[x,y,_]|x.hypot(y)<80.)){continue;}
                    if footprint.as_ref().is_some_and(|points| !supports_workshop(&mut geometry, p, points, &occupied)) {continue;}
                    // Require a walkable patch and a clear route through the camp.
                    if ![[0.6,0.,0.],[-0.6,0.,0.],[0.,0.6,0.],[0.,-0.6,0.]].into_iter().all(|offset|
                        p.translated(offset).and_then(|to|geometry.walk_step(p,to,0.35)).is_some()) {continue;}
                    if roaming.is_none() && origin.is_some_and(|home|!geometry.clear(home,p,1.0)){continue;}
                    found=Some(p);break;
                }
                if let Some(p)=found {
                    origin.get_or_insert(p);occupied.push(p);let child=id+slot as u64;append(child,entity,p,Some(entry))?;accepted.push(child);
                } else {
                    failures.push(json!({"group":id,"entity":entity,"slot":slot,"reason":"no reachable walkable native ground within 32m"}));
                    // A target-only camp cannot bestow its own quest.
                    if slot==0 {break;}
                }
            }
            if kind=="camp" && !accepted.is_empty() {
                let mut group=json!({"id":id,"capacity":entry["capacity"].as_u64().unwrap().min(accepted.len() as u64),"members":accepted,"regeneration_ms":120000});
                if roaming.is_some() {
                    if accepted.len()<2 {return Err("fewer than two validated roaming homes".into());}
                    group["roaming_homes"]=json!(true);
                    group.as_object_mut().unwrap().remove("regeneration_ms"); // Runtime uses the creature policy.
                }
                groups.push(group);
            }
        }
    }
    // A captured interior frame is a candidate, never an excuse to place a
    // monster through a wall. Do not silently relocate rejected room anchors.
    if let Some(interiors)=data["interiors"].as_array() {
        let mut motion=pyrealium::creature_motion::Catalog::default();
        let mut dat=pyrealium::dat::Dat::open(Path::new(&a[0]).join("portal.dat"))?;
        // Distinct actors captured together can stand closer than the six-metre
        // buffer for inferred placements; keep their native body footprints apart.
        let mut observed_groups = std::collections::BTreeMap::new();
        for group in data["interior_groups"].as_array().into_iter().flatten() {
            if group["captured_together"] == true && captured_bodies {
                let group_id = group["id"].as_u64().ok_or("captured group id")?;
                for member in group["members"].as_array().ok_or("captured group members")? {
                    observed_groups.insert(member.as_u64().ok_or("captured group member")?, group_id);
                }
            }
        }
        let mut placed: Vec<(Placement, Option<u64>, f32)> = Vec::new();
        for entry in interiors {
            let id=entry["id"].as_u64().ok_or("interior id")?;
            let entity=entry["entity"].as_u64().ok_or("interior entity")? as u32;
            let candidate=Placement{cell:entry["cell"].as_u64().ok_or("interior cell")? as u32,
                coordinates:[0,1,2].map(|i|entry["position"][i].as_f64().unwrap_or(f64::NAN) as f32),heading:0};
            let body=if validate_interior_bodies {
                let visual=data["templates"][entity.to_string()]["visual"].as_u64().ok_or("visual")? as u32;
                let scale=entry["scale"].as_f64().unwrap_or(1.0) as f32;
                Some(motion.resolve(&mut dat,visual,scale)?.ok_or("missing captured body profile")?)
            } else {None};
            // Some native creatures hover several metres above their walking surface.
            // Keep the recorded height for those bodies, while checking room and floor clearance.
            let depth=if body.is_some() {3.0}else{1.0};
            let group = observed_groups.get(&id).copied();
            let radius = body.map_or(0., |b| b.radius);
            let outdoor = entry["outdoor_capture"] == true && captured_bodies && (1..=64).contains(&(candidate.cell & 65535));
            let unoccupied = |point: Placement| {
                !occupied.iter().any(|old|old.distance(point).is_some_and(|d|d<6.)) &&
                placed.iter().all(|(other, other_group, other_radius)| {
                    let separation = if group.is_some() && group == *other_group && other.cell == point.cell {
                        radius + other_radius + 0.25
                    } else { 6. };
                    other.distance(point).is_none_or(|d| d >= separation)
                })
            };
            let mut probe = |sample: Placement, repair: bool| {
                geometry.ground(sample,0.5,depth).filter(|p| (if outdoor {
                    p.cell == candidate.cell && geometry.dry_ground(*p)
                } else { p.cell & 65535 >= 256 }) &&
                p.distance(candidate).is_some_and(|d|d<=depth) && geometry.contact(*p) &&
                (!repair || ((p.coordinates[2]-candidate.coordinates[2]).abs() <= 0.5
                    && geometry.clear(candidate,*p,0.35)
                    && body.is_some_and(|b|geometry.clear(candidate,*p,(b.height-0.1).max(0.35))))) &&
                body.is_none_or(|b| {
                    if p.cell!=candidate.cell || !geometry.body_fits(*p,b.radius,b.height) {return false;}
                    if b.grounded {return true;}
                    let head=candidate.translated([0.,0.,b.height-0.05]);
                    geometry.clear(*p,candidate,0.05) && head.is_some_and(|h|geometry.clear(candidate,h,0.05))
                }) &&
                unoccupied(*p) &&
                [[0.6,0.,0.],[-0.6,0.,0.],[0.,0.6,0.],[0.,-0.6,0.]].into_iter().all(|offset|
                    p.translated(offset).and_then(|to|geometry.walk_step(*p,to,0.35)).is_some()))
            };
            let mut point=probe(candidate, false);
            // An older build can put a foot on the lip of a solid prop. Keep the
            // measured frame in the evidence and record any same-floor adjustment.
            if point.is_none() && captured_bodies && body.is_some_and(|b|b.grounded)
                && data["allow_captured_floor_repair"] == true {
                if let Some(offset)=entry["reviewed_floor_offset_m"].as_array() {
                    if offset.len()!=2 {return Err("floor repair requires two offsets".into());}
                    let (dx,dy)=(offset[0].as_f64().ok_or("floor repair east offset")? as f32,
                        offset[1].as_f64().ok_or("floor repair north offset")? as f32);
                    if !dx.hypot(dy).is_finite() || dx.hypot(dy)>1.5 {
                        return Err("reviewed floor repair exceeds 1.5 m".into());
                    }
                    point=candidate.translated([dx,dy,0.]).and_then(|sample|probe(sample,true));
                    if let Some(feet)=point {
                        floor_repairs.push(json!({"id":id,"cell":feet.cell,"captured":candidate.coordinates,"placed":feet.coordinates}));
                    }
                }
                'search: for radius in [0.25,0.5,0.75,1.0] {
                    if point.is_some() {break;}
                    for direction in 0..8 {
                        let angle=std::f32::consts::TAU * direction as f32 / 8.;
                        if let Some(feet)=candidate.translated([radius*angle.cos(),radius*angle.sin(),0.])
                            .and_then(|sample|probe(sample,true)) {
                            floor_repairs.push(json!({"id":id,"cell":feet.cell,"captured":candidate.coordinates,"placed":feet.coordinates}));
                            point=Some(feet);
                            break 'search;
                        }
                    }
                }
            }
            // A captured actor can stand on a dais where floor rays start inside
            // a prop. The DAT sphere solver has to place the unmodified body and
            // maintain walkable contact at the recorded location for 100 quanta.
            if point.is_none() && captured_bodies && entry["verify_native_platform"] == true &&
                candidate.cell & 65535 >= 256 && entry["capture_id"].is_string() && unoccupied(candidate) {
                if let Some(profile)=body.filter(|b|b.grounded) {
                    let visual=data["templates"][entity.to_string()]["visual"].as_u64().ok_or("visual")? as u32;
                    let scale=entry["scale"].as_f64().unwrap_or(1.) as f32;
                    // Native headings turn clockwise about +Z from +Y.
                    let w=entry["rotation"][0].as_f64().ok_or("captured orientation")?;
                    let z=entry["rotation"][3].as_f64().ok_or("captured orientation")?;
                    let heading=((-z.atan2(w)/std::f64::consts::PI).rem_euclid(1.)*255.).round() as u8;
                    let captured=Placement{heading,..candidate};
                    if let Some(result)=geometry.native_support(captured,visual,scale,profile.descriptor)?.filter(|r|r.accepted) {
                        native_platforms.push(json!({"id":id,"support":result}));
                        point=Some(captured);
                    }
                }
            }
            if let Some(feet)=point {
                // A drawn map has no measured hover height. Preserve an airborne
                // origin only when the source frame came from a capture.
                let pose=if captured_bodies && body.is_some_and(|b|!b.grounded){candidate}else{feet};
                append(id,entity,pose,Some(entry))?;placed.push((feet, group, radius));
            }
            else {failures.push(json!({"id":id,"entity":entity,"reason":"captured interior anchor has no clear native walking patch"}));}
        }
        occupied.extend(placed.into_iter().map(|p| p.0));
    }
    // A portal is used within four metres. Its captured origin may hover above
    // a plinth; validate a player's approach and preserve the original frame.
    if let Some(portals) = data["portals"].as_array() {
        for entry in portals {
            let id = entry["id"].as_u64().ok_or("portal id")?;
            let entity = entry["entity"].as_u64().ok_or("portal entity")? as u32;
            let center = Placement {
                cell: entry["cell"].as_u64().ok_or("portal cell")? as u32,
                coordinates: [0, 1, 2].map(|i| entry["position"][i].as_f64().unwrap_or(f64::NAN) as f32),
                heading: 0,
            };
            let reachable = (-7..=7).any(|dx| (-7..=7).any(|dy| {
                let Some(sample) = center.translated([dx as f32 * 0.5, dy as f32 * 0.5, 0.]) else { return false };
                geometry.ground(sample, 1., 3.9).is_some_and(|feet| feet.cell == center.cell
                    && center.distance(feet).is_some_and(|d| d < 4.)
                    && geometry.contact(feet) && geometry.body_fits(feet, 0.35, 1.8))
            }));
            // The captured Fiery Crypt sentry stands five metres from its portal.
            // Keep the four-metre use area clear when restoring a recorded frame.
            let separation = if entry["capture_id"].is_string() { 4.0 } else { 6.0 };
            if !reachable || occupied.iter().any(|old| old.distance(center).is_some_and(|d| d < separation)) {
                return Err(format!("portal {id:016x} has no unobstructed placement or approach").into());
            }
            append(id, entity, center, Some(entry))?;
            occupied.push(center);
        }
    }
    if let Some(interior_groups)=data["interior_groups"].as_array() {
        for group in interior_groups {
            let members=group["members"].as_array().ok_or("interior group members")?.iter()
                .filter_map(Value::as_u64).filter(|id|ids.contains(id)).collect::<Vec<_>>();
            if !members.is_empty() {groups.push(json!({"id":group["id"],"capacity":members.len(),"members":members}));}
        }
    }
    let text=lines.join("\n")+"\n";std::fs::write(&a[2],&text)?;
    let mut report=json!({"version":1,"inferred":!captured_bodies,"native_ground_verified":true,
        "source_sha256":format!("{:x}",Sha256::digest(&bytes)),"manifest_sha256":format!("{:x}",Sha256::digest(text.as_bytes())),
        "existing_outdoor_sha256":format!("{:x}",Sha256::digest(base_lines.join("\n").as_bytes())),
        "groups":groups,"rejected":failures,"included":ids.len()});
    if data["allow_captured_floor_repair"] == true {report["floor_repairs"]=json!(floor_repairs);}
    if !native_platforms.is_empty() {report["native_platforms"]=json!(native_platforms);}
    std::fs::write(&a[3],serde_json::to_string_pretty(&report)?)?;
    println!("Validated population: {} placements, {} spawner groups, {} rejected slots",ids.len(),groups.len(),failures.len());Ok(())
}
