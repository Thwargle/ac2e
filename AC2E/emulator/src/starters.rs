//! Starter-instance routing. Actor coordinates have separate captured/authored
//! provenance in the catalog; the Shelter quest state machine remains shared.
use crate::movement::Placement;
use std::sync::OnceLock;

fn areas() -> &'static Vec<serde_json::Value> {
    static DATA: OnceLock<Vec<serde_json::Value>> = OnceLock::new();
    DATA.get_or_init(|| {
        serde_json::from_str::<serde_json::Value>(include_str!("../content/starter-areas-v1.json"))
            .expect("bundled starter catalog")["areas"]
            .as_array()
            .expect("starter areas")
            .clone()
    })
}
fn id(row: &serde_json::Value) -> u64 {
    u64::from_str_radix(row["id"].as_str().expect("actor id"), 16).expect("actor identity")
}
fn point(row: &serde_json::Value) -> Placement {
    Placement {
        cell: row["cell"].as_u64().expect("cell") as u32,
        coordinates: std::array::from_fn(|i| {
            row["position"][i].as_f64().expect("coordinate") as f32
        }),
        heading: 0,
    }
}
pub fn vault(portal: u64) -> Option<Placement> {
    if portal == crate::shelter::PORTAL {
        return Some(crate::shelter::VAULT);
    }
    areas()
        .iter()
        .find(|a| {
            a["actors"]
                .as_array()
                .unwrap()
                .iter()
                .any(|actor| actor["role"] == "menhir-portal" && id(actor) == portal)
        })
        .map(|a| point(&a["vault"]["start"]))
}
/// Accept the two old proxies as well, so prepared deployments can finish the
/// same quest after updating their executable and before regenerating content.
pub fn shard_route(id: u64) -> Option<crate::portals::Route> {
    let race = match id {
        0x8140000000031008 | 0x8140000000031009 | 0x814000000003100b => 1,
        0x8140000000032008 | 0x8140000000032009 | 0x814000000003200b => 4,
        0x8140000000033008 | 0x8140000000033009 | 0x814000000003300b => 2,
        // The original single-room development route led to Arwic.
        0x8140000000030048 | 0x8140000000030049 => 1,
        _ => return None,
    };
    let area = areas().iter().find(|a| a["race"] == race)?;
    Some(crate::portals::Route { destination: point(&area["overworld"]), level: 1 })
}
pub fn use_radius(id: u64) -> f32 {
    // The three porting.from.shardroom captures interact 4.6 to 5.0 m from the pivot.
    if shard_route(id).is_some() { 5.0 } else { 4.0 }
}
pub fn portal_name(portal: u64) -> Option<&'static str> {
    if portal == crate::shelter::HUMAN_QUICK_EXIT {
        return Some("Quick Exit to the Arwic Outlands");
    }
    if vault(portal).is_some() {
        return Some("Menhir Vault Portal");
    }
    if shard_route(portal).is_some() {
        return Some("Menhir Shard");
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn racial_routes_are_distinct_and_only_their_menhir_portals_accept_glyphs() {
        let mut destinations = std::collections::BTreeSet::new();
        for a in areas() {
            let actors = a["actors"].as_array().unwrap();
            for role in [
                "guard",
                "lieutenant",
                "sentry",
                "digger",
                "scout",
                "sentinel",
                "menhir-portal",
            ] {
                assert_eq!(
                    actors.iter().filter(|r| r["role"] == role).count(),
                    1,
                    "{role}"
                );
            }
            let portal = id(actors
                .iter()
                .find(|r| r["role"] == "menhir-portal")
                .unwrap());
            let destination = vault(portal).unwrap();
            assert!(destinations.insert(destination.cell));
            assert_eq!(destination.cell >> 16, point(&a["start"]).cell >> 16);
            for actor in &a["vault"]["actors"].as_array().unwrap()[..] {
                assert!(vault(id(actor)).is_none());
                let route = shard_route(id(actor));
                assert_eq!(route.is_some(), actor["role"] == "shard");
                if let Some(route) = route {
                    assert_eq!(route.destination, point(&a["overworld"]));
                    for legacy in [id(actor) - 2, id(actor) - 3] {
                        assert_eq!(shard_route(legacy).unwrap().destination, route.destination);
                    }
                }
            }
        }
        assert_eq!(destinations.len(), 3);
        assert!(vault(0).is_none());
        assert!(shard_route(0).is_none());
    }
    #[test]
    fn catalog_matches_spawn_routes_and_character_creation() {
        use std::collections::BTreeMap;
        let mut objects = BTreeMap::new();
        for source in [
            include_str!("../content/training-world.tsv"),
            include_str!("../content/starter-world-v1.tsv"),
        ] {
            for line in source
                .lines()
                .skip(1)
                .filter(|s| !s.starts_with('#') && !s.trim().is_empty())
            {
                let row: Vec<_> = line.split('\t').collect();
                let identity = u64::from_str_radix(row[0], 16).unwrap();
                assert!(
                    objects.insert(identity, row).is_none(),
                    "duplicate {identity:x}"
                );
            }
        }
        let routes: BTreeMap<_, _> = include_str!("../content/starter-routes-v1.tsv")
            .lines()
            .skip(1)
            .map(|line| {
                let row: Vec<_> = line.split('\t').collect();
                (u64::from_str_radix(row[0], 16).unwrap(), row)
            })
            .collect();
        assert_eq!(routes.len(), 7);
        for area in areas() {
            let start = point(&area["start"]);
            for sex in [crate::appearance::MALE, crate::appearance::FEMALE] {
                let body =
                    crate::appearance::body(area["race"].as_u64().unwrap() as u32, sex).unwrap();
                assert_eq!(body.start.cell, start.cell);
                for i in 0..3 {
                    assert!((body.start.coordinates[i] - start.coordinates[i]).abs() < 0.001);
                }
            }
            for room in [area, &area["vault"]] {
                for actor in room["actors"].as_array().unwrap() {
                    let row = &objects[&id(actor)];
                    assert_eq!(
                        u32::from_str_radix(row[1], 16).unwrap(),
                        actor["entity"].as_u64().unwrap() as u32
                    );
                    assert_eq!(
                        u32::from_str_radix(row[2], 16).unwrap(),
                        actor["visual"].as_u64().unwrap() as u32
                    );
                    assert_eq!(u32::from_str_radix(row[3], 16).unwrap(), point(actor).cell);
                    for i in 0..3 {
                        assert!(
                            (row[4 + i].parse::<f64>().unwrap()
                                - actor["position"][i].as_f64().unwrap())
                            .abs()
                                < 0.001
                        );
                    }
                    let destination = match actor["role"].as_str().unwrap() {
                        "menhir-portal" => Some(point(&area["vault"]["start"])),
                        "shard" | "shortcut-portal" => Some(point(&area["overworld"])),
                        _ => None,
                    };
                    if let Some(destination) = destination {
                        let route = &routes[&id(actor)];
                        assert_eq!(u32::from_str_radix(route[6], 16).unwrap(), destination.cell);
                        for i in 0..3 {
                            assert!(
                                (route[7 + i].parse::<f32>().unwrap() - destination.coordinates[i])
                                    .abs()
                                    < 0.001
                            );
                        }
                    }
                }
            }
        }
    }
}
