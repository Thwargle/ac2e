use pyrealium::{dat::Dat, windmill};
#[test]
fn windmill_assets_and_native_map_are_compatible() {
    let Ok(path) = std::env::var("AC2_TEST_GAME_DATA") else {
        return;
    };
    let mut dat = Dat::open(std::path::Path::new(&path).join("portal.dat")).unwrap();
    assert_eq!(windmill::validate(&mut dat).unwrap().len(), 3);
    let map = windmill::survey_map();
    assert_eq!(map.radius, 70);
    let mine = pyrealium::harvesting::mine(map.mine).unwrap();
    assert_eq!(
        (mine.material, mine.quantity, mine.uses),
        (windmill::STONE, 25, 3)
    );
    let mut geo = pyrealium::geometry::Geometry::open(std::path::Path::new(&path)).unwrap();
    let t = windmill::content().map_location;
    let point = pyrealium::movement::Placement {
        cell: t[0] as u32,
        coordinates: [t[1] as f32, t[2] as f32, t[3] as f32],
        heading: 0,
    };
    let point = geo
        .ground(point, 20., 20.)
        .expect("native quarry destination needs walkable ground");
    assert!(geo.contact(point) && geo.dry_ground(point));
}
