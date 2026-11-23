//! Author bounded outdoor creature corrections against native floor and body
//! collision. Originals remain in capture packs; the merge pins each full row.
use pyrealium::{content::Content,encounters::Encounters,geometry::{Geometry,Occupancy},movement::Placement};
use serde_json::json;
use std::{collections::BTreeMap,path::Path};

fn main()->Result<(),Box<dyn std::error::Error>> {
    let a=std::env::args().skip(1).collect::<Vec<_>>();
    if a.len()!=3{return Err("repair_placements DAT UNREPAIRED_WORLD_TSV OUTPUT_POLICY_JSON".into());}
    let content=Content::load(Path::new(&a[1]),Path::new(&a[0]))?;
    let mut encounters=Encounters::load(Path::new("world"),&content)?;
    encounters.load_motion(&content,Path::new(&a[0]))?;
    let text=std::fs::read_to_string(&a[1])?;
    let rows=text.lines().filter_map(|line|line.split_once('\t').map(|(id,_)|(id.to_owned(),line))).collect::<BTreeMap<_,_>>();
    let mut occupied=Occupancy::default();let mut world=Geometry::open(Path::new(&a[0]))?;
    let mut actors=content.objects().filter_map(|o|encounters.movement_profile(o.id).map(|b|(o,b))).collect::<Vec<_>>();
    actors.sort_by_key(|(o,_)|(o.placement.cell,o.id));
    for (o,b) in &actors {occupied.insert(o.id,o.placement,b.radius,b.height);}
    let mut repairs=Vec::new();let mut unresolved=Vec::new();let mut checked=0;
    for (o,b) in &actors {
        let p=o.placement;
        // Interiors can have deliberately different floors and native objects
        // omitted by this query backend. Never guess a new room or move flyers.
        if !b.grounded || !(1..=64).contains(&(p.cell&65535)){continue;}
        checked+=1;
        let fits=world.dry_ground(p) && (world.body_fits(p,b.radius,b.height) ||
            world.native_body_fits(p,o.visual,o.scale,b.descriptor)?);
        let separate=occupied.permits(o.id,p,p,b.radius,b.height);
        if fits && separate{continue;}
        let reason=if !fits{"unsupported ground or body intersects native geometry"}else{"overlapping living creature bodies"};
        let mut found=None;
        'search: for radius in [0.,0.5,1.,2.,3.] {
            for angle in 0..if radius==0.{1}else{16} {
                let t=angle as f32*std::f32::consts::TAU/16.;
                let Some(raw)=p.translated([radius*t.cos(),radius*t.sin(),0.]) else{continue};
                // Small near-floor probe first, bounded extended probe second.
                let floor=world.ground(raw,0.3,0.4).or_else(||world.ground(raw,1.,2.));
                let Some(next)=floor.filter(|q|(1..=64).contains(&(q.cell&65535)) &&
                    p.distance(*q).is_some_and(|d|d<=3.5) && world.dry_ground(*q) && world.body_fits(*q,b.radius,b.height)) else{continue};
                // A repair may separate an existing overlap, but the final
                // anchor itself must be clear of every other living body.
                if !occupied.permits(o.id,next,next,b.radius,b.height){continue;}
                // Horizontal relocations cannot cross a native wall. A purely
                // vertical grounding correction is allowed for buried pivots.
                if radius>0. && !world.clear(Placement{coordinates:[p.coordinates[0],p.coordinates[1],next.coordinates[2]],..p},next,0.5){continue;}
                found=Some(next);break 'search;
            }
        }
        if let Some(next)=found {
            let id=format!("{:016x}",o.id);let original=rows[&id];
            let mut fields=original.split('\t').map(str::to_owned).collect::<Vec<_>>();
            fields[3]=format!("{:08x}",next.cell);
            for i in 0..3 {fields[4+i]=format!("{:.6}",next.coordinates[i]);}
            repairs.push(json!({"id":id,"original":original,"replacement":fields.join("\t"),"reason":reason,
                "body_radius_m":b.radius,"body_height_m":b.height}));
            occupied.reserve(o.id,p,next,b.radius,b.height);
        } else {
            let floor=world.ground(p,1.,3.);
            unresolved.push(json!({"id":format!("{:016x}",o.id),"cell":format!("{:08x}",p.cell),"reason":reason,
                "position":p.coordinates,"body_radius_m":b.radius,"body_height_m":b.height,
                "floor_contact":world.contact(p),"dry_ground":world.dry_ground(p),
                "nearby_floor":floor.map(|f|f.coordinates),"water_height":world.water_height(p.cell).ok().flatten(),
                "body_clear_at_floor":floor.map(|f|world.body_fits(f,b.radius,b.height)),"separate":separate}));
        }
    }
    // Later repairs may move the neighbour which blocked an earlier anchor.
    // Report the final population, rather than retaining stale overlap warnings.
    let mut pending=Vec::new();
    for entry in unresolved {
        let id=u64::from_str_radix(entry["id"].as_str().unwrap(),16).unwrap();
        let o=content.object(id).unwrap();let b=encounters.movement_profile(id).unwrap();
        let supported=world.dry_ground(o.placement) && (world.body_fits(o.placement,b.radius,b.height) ||
            world.native_body_fits(o.placement,o.visual,o.scale,b.descriptor)?);
        if !supported || !occupied.permits(id,o.placement,o.placement,b.radius,b.height){pending.push(entry);}
    }
    let unresolved=pending;
    std::fs::write(&a[2],serde_json::to_string_pretty(&json!({"schema":1,"native_ground_verified":true,
        "scope":"Outdoor terrestrial combat actors only; <=3.5m corrections. Native body footprint, ceiling, wall, dry floor and living-body separation checked. Original narrow-platform anchors may instead pass native sphere placement plus 100 stable physics quanta without sliding or forcing. Captures, identity, orientation, scale and spawner membership are preserved. Unresolved anchors retained for review; no guessing across dungeon floors.",
        "checked":checked,"repairs":repairs,"unresolved":unresolved}))?+"\n")?;
    println!("{checked} grounded outdoor actors: {} repaired, {} unresolved",repairs.len(),unresolved.len());Ok(())
}
