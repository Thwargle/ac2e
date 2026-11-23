//! Offline checks using the same validators as AC2E. No socket or player store.
//! A passing check establishes only the scope printed beside it.
use pyrealium as game;
use std::{collections::BTreeMap, io, path::PathBuf};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    if args.len() != 2 {
        return Err("usage: dat_readiness DAT_DIRECTORY OUTPUT_JSON".into());
    }
    let directory = PathBuf::from(&args[0]);
    let mut dat = game::dat::Dat::open(directory.join("portal.dat"))?;
    let bootstrap = game::bootstrap::Bootstrap::load(&directory);
    let version = bootstrap
        .as_ref()
        .map(|b| b.client_version())
        .unwrap_or("unsupported");
    let geometry = bootstrap.as_ref().is_ok_and(|b| b.geometry.is_some());
    let mut checks = BTreeMap::new();
    let mut check = |name: &str, scope: &str, result: io::Result<()>| {
        checks.insert(
            name.to_owned(),
            serde_json::json!({"passed": result.is_ok(),
            "scope": scope, "error": result.err().map(|e| e.to_string())}),
        );
    };
    check(
        "bootstrap",
        "Recognized player scripts, racial bodies and starting cells",
        bootstrap.map(|_| ()),
    );
    check(
        "skills",
        "Skill index, advancement and definition fingerprints",
        game::skills::validate_assets(&mut dat),
    );
    check(
        "skill_effects",
        "Imported effect record fingerprints, not full effect execution",
        game::abilities::validate_assets(&mut dat),
    );
    check(
        "skill_presentation",
        "Behavior and FX catalog fingerprints",
        game::skill_presentation::validate_assets(&mut dat),
    );
    check(
        "emotes",
        "Native emote tables",
        game::emotes::validate_assets(&mut dat),
    );
    check(
        "starter_gear",
        "Usable racial starting equipment profiles",
        game::starter_gear::validate(&mut dat).map(|_| ()),
    );
    check(
        "shelter",
        "Starter quest inventory profiles",
        game::shelter::validate(&mut dat).map(|_| ()),
    );
    check(
        "quests",
        "Imported quest definitions",
        game::quests::validate_assets(&mut dat),
    );
    check(
        "potion_recipes",
        "Native health and vigor recipe identities",
        game::crafting::validate_assets(&mut dat),
    );
    let mut metadata = BTreeMap::new();
    check(
        "professions",
        "Recipe catalog loads; unsupported item metadata may still be withheld",
        game::professions::validate_assets(&mut dat, &mut metadata),
    );
    check(
        "loot",
        "Catalog loads; incompatible entries withheld, retail weights/affixes not established",
        game::loot::Catalog::load(&mut dat, &mut metadata).map(|_| ()),
    );
    let abilities = game::abilities::Runtime::load()?;
    let report = serde_json::json!({"dat_directory": directory, "client_version": version,
        "geometry_available": geometry, "checks": checks, "skills": abilities.coverage(),
        "known_npc_dialogue_profiles": game::npc_dialogue::profiles().len(),
        "recovered_quest_profiles": game::recovered_quests::profiles().len(),
        "fully_functional_game_verified": false,
        "limitations": ["Record validation does not execute every quest or skill.",
            "Many specialized skills, summons, area/combo mechanics and retail treasure formulas remain incomplete.",
            "NPC dialogue alone does not prove an executable quest.",
            "Expansion player races and complete world placement are not implemented.",
            "Icefire is a client compatibility target, not an AC2E server content profile."]});
    std::fs::write(&args[1], serde_json::to_string_pretty(&report)?)?;
    if report["checks"]
        .as_object()
        .unwrap()
        .values()
        .any(|c| c["passed"] != true)
        || !geometry
    {
        return Err("one or more DAT readiness checks failed; see report".into());
    }
    Ok(())
}
