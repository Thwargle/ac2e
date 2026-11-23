use pyrealium::{content::Content, geometry::Geometry, portals::Routes};

#[test]
fn prosper_dungeons_have_walkable_entries_and_both_return_routes() {
    let Some(directory) = std::env::var_os("AC2_TEST_GAME_DATA").map(std::path::PathBuf::from) else { return };
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("content");
    let captured = Content::load(&path.join("capture-spawns-audited.tsv"), &directory).unwrap();
    let restored = Content::load(&path.join("vault-world-v1.tsv"), &directory).unwrap();
    let entrances = Routes::bundled(&restored, &directory).unwrap();
    let exits = Routes::bundled(&captured, &directory).unwrap();
    let mut geometry = Geometry::open(&directory).unwrap();
    for (entrance, exit, source_cell, source_position, room, outside, minimum) in [
        (0x8140d60000001000, 0x81409a74c52d86d5, 0x94860036, [123.845001,109.844002,85.953659], 0x05fc0100, 0x9486003e, 1),
        (0x8140d60000001001, 0x8140dabf0d975d4f, 0x9484003e, [145.0,116.0,211.560654], 0x07f20107, 0x94840037, 10),
    ] {
        let enter = entrances.get(entrance).unwrap();
        let leave = exits.get(exit).unwrap();
        assert_eq!((enter.destination.cell, enter.level), (room, minimum));
        assert_eq!((leave.destination.cell, leave.level), (outside, 1));
        let portal = restored.object(entrance).unwrap().placement;
        assert_eq!((portal.cell, portal.coordinates), (source_cell, source_position));
        for point in [enter.destination, leave.destination] {
            let feet = geometry.ground(point, 1.0, 2.0).unwrap_or_else(|| panic!("no floor near {point:?}"));
            assert!(geometry.contact(feet));
            assert!(point.distance(feet).unwrap() < 1.0);
            assert!(geometry.body_fits(feet, 0.35, 1.8));
        }
        // A portal can hover above its plinth. Check the server's four-metre reach at both ends.
        for point in [portal, captured.object(exit).unwrap().placement] {
            let reachable = (-7..=7).any(|dx| (-7..=7).any(|dy| {
                let sample = point.translated([dx as f32*0.5,dy as f32*0.5,0.]).unwrap();
                geometry.ground(sample,1.0,3.9).is_some_and(|feet|
                    feet.cell==point.cell && point.distance(feet).unwrap()<4.0 && geometry.body_fits(feet,0.35,1.8))
            }));
            assert!(reachable, "no same-cell approach at {point:?}");
        }
        assert!(portal.distance(leave.destination).unwrap() > 4.0, "return must be outside automatic use range");
    }
}
