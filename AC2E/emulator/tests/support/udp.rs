use ac2_net::protocol::{Build, ServerEvent};
use pyrealium::movement::Placement;
use std::path::Path;

pub fn assert_spawn(bytes: &[u8], actor: u64, instance: u16, expected: Placement) {
    let ServerEvent::CreateObject(object) = ServerEvent::decode(bytes, Build::Admin7148).unwrap()
    else {
        panic!("expected a native CreateObject");
    };
    assert_eq!(object.id, actor);
    assert_eq!(object.physics.instance_stamp, instance);
    let position = object.physics.position.expect("player placement");
    assert_eq!(position.cell, expected.cell);
    assert_eq!(position.origin, expected.coordinates);
}

pub fn remove_test_store(dir: &Path) {
    assert_eq!(dir.parent(), Some(std::env::temp_dir().as_path()));
    for entry in std::fs::read_dir(dir).unwrap() {
        std::fs::remove_file(entry.unwrap().path()).unwrap();
    }
    std::fs::remove_dir(dir).unwrap();
}
