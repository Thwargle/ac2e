use pyrealium::{dat::Dat, nature_exchanges};
use std::path::Path;

#[test]
fn exchange_assets_match_installed_native_quality_and_transport() {
    let Ok(directory) = std::env::var("AC2_TEST_GAME_DATA") else {
        return;
    };
    let mut dat = Dat::open(Path::new(&directory).join("portal.dat")).unwrap();
    let metadata = nature_exchanges::validate(&mut dat).unwrap();
    assert_eq!(metadata.len(), 8);
    let recipe=pyrealium::professions::recipe(pyrealium::nature_gathering::RECIPE).unwrap();
    assert_eq!(recipe.ingredients.len(),7);
    assert_eq!(recipe.cost,vec![u64::from(pyrealium::nature_gathering::COST)]);
    for input in &recipe.ingredients {
        assert_eq!(input.choices,vec![pyrealium::nature_gathering::BLOSSOM]);
        assert_eq!(input.quantities,vec![1]);
    }
    for rule in &nature_exchanges::content().rules {
        assert!(metadata.contains_key(&rule.input));
        assert!(metadata.contains_key(&rule.output));
        assert!(
            rule.quantity
                <= nature_exchanges::content()
                    .items
                    .iter()
                    .find(|i| i.id == rule.output)
                    .unwrap()
                    .stack_limit
        );
        pyrealium::interaction::dialogue(&rule.npc.name, &rule.guidance()).unwrap();
        pyrealium::interaction::speech(0x8140000000000001, &rule.npc.name, &rule.receipt())
            .unwrap();
    }
}
