//! Read-only placement diagnostics against the installed native collision.
use pyrealium::{content::Content, creature_motion::Catalog, dat::Dat, geometry::Geometry};
use serde_json::json;
use std::{collections::BTreeMap, path::Path};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let a=std::env::args().skip(1).collect::<Vec<_>>();
    if a.len()!=3 {return Err("placement_audit DAT_DIRECTORY WORLD_TSV REPORT_JSON".into());}
    let content=Content::load(Path::new(&a[1]),Path::new(&a[0]))?;
    let mut dat=Dat::open(Path::new(&a[0]).join("portal.dat"))?;
    let mut motion=Catalog::default();let mut geometry=Geometry::open(Path::new(&a[0]))?;
    let mut objects=content.objects().collect::<Vec<_>>();objects.sort_by_key(|o|(o.placement.cell,o.id));
    let mut issues=Vec::new();let mut animated=Vec::new();let mut counts=BTreeMap::new();
    for o in &objects {
        let Some(body)=motion.resolve(&mut dat,o.visual,o.scale)? else{continue};
        animated.push(*o);
        if !body.grounded{continue;}
        let p=o.placement;if geometry.contact(p){continue;}
        let floor=geometry.ground(p,1.,3.);
        let delta=floor.and_then(|f|p.offset_to(f)).map(|d|-d[2]);
        let problem=if floor.is_none(){Some("no nearby floor")}
            else if delta.is_some_and(|z|z.abs()>0.3){Some("floor offset")}
            else{None};
        if let Some(reason)=problem {
            *counts.entry(reason).or_insert(0)+=1;
            issues.push(json!({"id":format!("{:016x}",o.id),"entity":format!("{:08x}",o.entity),
                "visual":format!("{:08x}",o.visual),"name":o.name,"cell":format!("{:08x}",p.cell),
                "position":p.coordinates,"reason":reason,"above_floor_m":delta}));
        }
    }
    let mut overlaps=Vec::new();
    for (i,a) in animated.iter().enumerate(){for b in &animated[i+1..]{
        if let Some(distance)=a.placement.distance(b.placement).filter(|d|*d<1.){
            overlaps.push(json!({"a":format!("{:016x}",a.id),"b":format!("{:016x}",b.id),
                "names":[a.name,b.name],"cell":format!("{:08x}",a.placement.cell),"distance_m":distance}));
        }
    }}
    let report=json!({"objects":objects.len(),"animated":animated.len(),"counts":counts,
        "issues":issues,"close_pairs":overlaps});
    std::fs::write(&a[2],serde_json::to_string_pretty(&report)?)?;
    println!("{} objects, {} animated, {:?}, {} close pairs",objects.len(),animated.len(),counts,overlaps.len());
    Ok(())
}
