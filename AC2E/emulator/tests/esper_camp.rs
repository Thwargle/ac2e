use pyrealium::{
    esper_camp as e,
    inventory::{Command, Location},
    store::Store,
};

#[test]
fn native_assets_recipes_and_vermin_bone_are_compatible() {
    let Ok(path) = std::env::var("AC2_TEST_GAME_DATA") else {
        return;
    };
    let mut dat =
        pyrealium::dat::Dat::open(std::path::Path::new(&path).join("portal.dat")).unwrap();
    let metadata = e::validate(&mut dat).unwrap();
    assert_eq!(metadata.len(), 9);
    for r in 0x6f001f0f..=0x6f001f14 {
        let recipe = pyrealium::professions::recipe(r).unwrap();
        assert_eq!((recipe.minimum, recipe.maximum), (0, 0));
        assert_eq!(recipe.ingredients.len(), 1);
        assert_eq!(recipe.ingredients[0].choices, vec![e::MATERIAL]);
        assert_eq!(recipe.ingredients[0].quantities, vec![25]);
        let did = 0x470015b2 + r - 0x6f001f0f;
        assert_eq!(recipe.outputs[0].item, did);
        assert!(metadata[&did].equipment.is_some());
        pyrealium::item_services::acquired(
            7,
            &e::item(did, pyrealium::inventory::ITEM_NAMESPACE + 1, 0, 1).unwrap(),
            metadata[&did],
        )
        .unwrap();
    }
    let vermin = pyrealium::professions::catalog()
        .butcher
        .iter()
        .find(|b| b.id == 0x47000f7e)
        .unwrap();
    assert_eq!(vermin.material, e::MATERIAL);
}

#[test]
fn quest_inventory_entitlement_and_rewards_replay_atomically() {
    let path = std::env::temp_dir().join(format!(
        "ac2-esper-{}-{}",
        std::process::id(),
        pyrealium::recovered_quests::now()
    ));
    let mut s = Store::open(&path).unwrap();
    s.create("local", "Esper Apprentice").unwrap();
    assert!(
        s.esper_transaction("stranger", 1, e::Action::Talk, 100)
            .is_err()
    );
    s.esper_transaction("local", 1, e::Action::Talk, 100)
        .unwrap();
    let id = s.next_item_id().unwrap();
    s.inventory_command(
        "local",
        1,
        Command::Grant(e::item(e::MATERIAL, id, 0, 30).unwrap()),
    )
    .unwrap();
    s.esper_transaction(
        "local",
        1,
        e::Action::Craft {
            recipe: 0x6f001f14,
            primary: id,
        },
        101,
    )
    .unwrap();
    s.compact_journal().unwrap();
    drop(s);
    let mut s = Store::open(&path).unwrap();
    assert_eq!(s.characters[&1].quests.esper.phase, 2);
    let sword = s.characters[&1]
        .inventory
        .items()
        .find(|i| i.definition == 0x470015b7)
        .unwrap()
        .id;
    s.inventory_command(
        "local",
        1,
        Command::Wear {
            item: sword,
            slot: 0x40,
        },
    )
    .unwrap();
    assert_eq!(
        pyrealium::professions::weapon_damage(&s.characters[&1].inventory),
        Some(22)
    );
    s.esper_transaction("local", 1, e::Action::Bone, 102)
        .unwrap();
    let bone = s.characters[&1]
        .inventory
        .items()
        .find(|i| i.definition == e::BONE)
        .unwrap()
        .id;
    assert!(
        s.drop_item(
            "local",
            1,
            bone,
            pyrealium::movement::Placement::HUMAN_START
        )
        .is_err()
    );
    s.compact_journal().unwrap();
    drop(s);
    let mut s = Store::open(&path).unwrap();
    assert_eq!(s.characters[&1].quests.esper.phase, 3);
    assert_eq!(
        s.characters[&1]
            .inventory
            .items()
            .find(|i| i.id == sword)
            .unwrap()
            .location,
        Location::Equipped(0x40)
    );
    s.esper_transaction("local", 1, e::Action::Talk, 103)
        .unwrap();
    s.esper_transaction("local", 1, e::Action::Talk, 104)
        .unwrap();
    s.compact_journal().unwrap();
    drop(s);
    let s = Store::open(&path).unwrap();
    let c = &s.characters[&1];
    assert_eq!(c.progression.xp, 320);
    assert_eq!(c.quests.esper.completed, 103);
    assert_eq!(
        c.inventory
            .items()
            .filter(|i| i.definition == e::GLYPH)
            .count(),
        1
    );
    assert_eq!(
        c.inventory
            .items()
            .find(|i| i.definition == e::MATERIAL)
            .unwrap()
            .quantity,
        5
    );
    drop(s);
    std::fs::remove_dir_all(path).unwrap();
}
