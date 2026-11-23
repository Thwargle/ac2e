//! Original packs remain authoring evidence; ground checks use the reviewed
//! positions applied by prepare_content. placement_clearance verifies the real
//! prepared manifest independently, including identity and population clearance.
use pyrealium::movement::Placement;
use serde_json::Value;
use std::{collections::BTreeMap,sync::OnceLock};

pub fn effective(id:u64,original:Placement)->Placement {
    static REPAIRS:OnceLock<BTreeMap<u64,(Placement,Placement)>>=OnceLock::new();
    let rules=REPAIRS.get_or_init(||{
        let mut rules=BTreeMap::new();
        for policy in [include_str!("../../content/placement-clearance-v1.json"),
            include_str!("../../content/placement-reviewed-v1.json")] {
            let policy:Value=serde_json::from_str(policy).unwrap();
            for r in policy["repairs"].as_array().unwrap() {
                let parse=|key:&str|{
                    let f=r[key].as_str().unwrap().split('\t').collect::<Vec<_>>();
                    Placement{cell:u32::from_str_radix(f[3],16).unwrap(),
                        coordinates:std::array::from_fn(|i|f[4+i].parse().unwrap()),heading:0}
                };
                assert!(rules.insert(u64::from_str_radix(r["id"].as_str().unwrap(),16).unwrap(),
                    (parse("original"),parse("replacement"))).is_none());
            }
        }
        rules
    });
    let Some((before,after))=rules.get(&id) else{return original};
    assert_eq!(original.cell,before.cell,"Source placement changed: {id:016x}");
    assert_eq!(original.coordinates,before.coordinates,"Source placement changed: {id:016x}");
    Placement{heading:original.heading,..*after}
}
