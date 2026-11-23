use pyrealium::{
    content::Content,
    geometry::Geometry,
    movement::Placement,
    professions,
    scene::{Body, Scene},
    world,
};
use std::path::Path;

#[test]
fn guide_towns_offer_three_distinct_services_in_the_prepared_world() {
    let Ok(directory) = std::env::var("AC2_TEST_GAME_DATA") else {
        return;
    };
    let data = Path::new(&directory);
    let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../data/world/legions-world.tsv");
    let content = Content::load(&path, data).unwrap();
    let stations = &professions::catalog().workshops;
    // AC2Hero compass coordinates, ordered crafting, refining, spellbinding.
    let towns = [
        ("Cragstone", [(32.1, -13.4), (32.8, -13.3), (32.2, -12.0)]),
        ("Cavendo", [(16.0, 28.8), (16.2, 27.9), (16.2, 28.9)]),
        ("Ikeras", [(10.7, 62.1), (9.9, 62.1), (9.6, 63.2)]),
        ("Hakata", [(8.6, 75.4), (8.8, 75.5), (8.7, 76.1)]),
        (
            "Linvak Tukal",
            [(-10.1, -31.2), (-10.5, -31.6), (-10.1, -31.7)],
        ),
        ("Ondekodo", [(-19.9, -31.2), (-20.1, -31.4), (-20.1, -31.2)]),
    ];
    let effects = [0x6f002ee5, 0x6f002ee6, 0x6f002ee7];
    let recipes = effects.map(|effect| {
        professions::catalog()
            .recipes
            .iter()
            .find(|r| r.workshop == effect)
            .unwrap()
            .id
    });
    let mut sites = Vec::new();
    for (town, coords) in towns {
        for (slot, (north, east)) in coords.into_iter().enumerate() {
            let matches: Vec<_> = content
                .objects()
                .filter(|o| {
                    let p = o.placement;
                    if !(1..=64).contains(&(p.cell & 0xffff))
                        || !stations
                            .iter()
                            .any(|w| w.id == o.entity && w.effect == effects[slot])
                    {
                        return false;
                    }
                    let x = (p.cell >> 24) as f64 * 160.0 + f64::from(p.coordinates[0]);
                    let y = (p.cell >> 16 & 255) as f64 * 160.0 + f64::from(p.coordinates[1]);
                    (x - (east + 100.0) * 204.0).hypot(y - (north + 100.0) * 204.0) <= 32.0
                })
                .collect();
            assert_eq!(matches.len(), 1, "{town} workshop slot {slot}");
            if town == "Cavendo" && slot == 1 {
                assert_eq!(matches[0].id, 0x8140c9483129fa42);
            }
            sites.push((town, slot, matches[0].placement));
        }
    }
    let mut scene = Scene::with_content(content);
    let mut geometry = Geometry::open(data).unwrap();
    let player = 0x8120000000000001;
    for (town, slot, station) in sites {
        // Use the same distance and scene visibility check as real recipe execution.
        let neighbor: Placement = station.translated([1., 0., 0.]).unwrap();
        let p = geometry.ground(neighbor, 0.75, 1.0).expect(town);
        let spawn = world::create_stationary_object(&world::StationaryObject {
            character_id: player,
            parent_visual_did: 1,
            entity_did: 2,
            cell_id: p.cell,
            position: p.coordinates,
            orientation: p.orientation(),
            instance_stamp: 1,
            visual_ordering_stamp: 0,
            timestamps: [0; 4],
        })
        .unwrap();
        scene.enter(Body::new(player, p, spawn).unwrap()).unwrap();
        for (index, recipe) in recipes.iter().enumerate() {
            assert_eq!(
                professions::require_workshop(&scene, player, *recipe).is_ok(),
                index == slot,
                "{town} station {slot} recipe {recipe:08x}"
            );
        }
        scene.remove(player);
    }
}
