"""Place documented AC2Hero town workshops on native ground (population_authoring)."""
import argparse
import json
from pathlib import Path
import sys

from lxml import html

from build_hero_atlas import positions
from hero_trade_guides import rows

ROOT = Path(__file__).resolve().parents[3]
CONTENT = ROOT / "AC2E/emulator/content"
sys.path[:0] = [str(ROOT / "Tools/AC2"), str(ROOT / "Network")]
from content_profile import ContentProfile

# Instance ranges are stable even when the guide changes row order.
TOWN_SLOT = {"Arwic": 0, "Cragstone": 1, "Cavendo": 2, "Ikeras": 3,
             "Hakata": 4, "Linvak Tukal": 5, "Ondekodo": 6}
SERVICES = ["Crafting Workshop", "Refining Workshop", "Spellbinding Workshop"]
# The Cavendo refinery is captured. Archived pictures identify three other models.
OSTETH = (0x4700086f, 0x4700086e, 0x47000870)
OMISHAN = (0x4700089f, 0x47000895, 0x4700089c)
LINVAK = (0x4700082c, 0x4700082d, 0x4700082e)
REGIONAL_MODELS = {"Osteth": OSTETH, "Omishan": OMISHAN, "Linvak Massif": LINVAK}
CAPTURED_REFINERY = 0x8140c9483129fa42
SOURCE = "http://ac2hero.e5i5o.com/workshops.html"
MODEL_PICTURES = {
    ("Cragstone", "Crafting Workshop"): "CraftingWorkshopCragstone.jpg",
    ("Cavendo", "Spellbinding Workshop"): "SpellbindingWorkshopCavendo.jpg",
    ("Ondekodo", "Spellbinding Workshop"): "SpellbindingWorkshopOndekodo.jpg",
}


# Metres east/north from each rounded guide point. Tested against the native
# model's ground-level vertices at the authored facing, then checked for NPCs
# inside its footprint. None marks the separately captured Cavendo refinery.
FOUNDATION_OFFSETS = {
    "Cragstone": ((0, 0), (0, 10), (17, -3)),
    "Cavendo": ((8, 0), None, (0, 2)),
    "Ikeras": ((22, -6), (16, 23), (-2, 0)),
    "Hakata": ((-3, -1), (-11, -10), (0, 0)),
    "Linvak Tukal": ((0, 0), (-2, 1), (0, -6)),
    "Ondekodo": ((-1, -3), (3, 3), (0, 0)),
}


def build(page, dat):
    table = html.parse(str(page)).xpath("//table[.//th[normalize-space()='Crafting Workshop']]")
    if len(table) != 1:
        raise ValueError("Expected the AC2Hero town workshop table")
    header, *towns = rows(table[0])
    if header != ["Continent", "Town", *SERVICES]:
        raise ValueError(f"Unexpected workshop columns: {header}")
    definitions = {}
    npcs = []
    observed = []
    seen = set()
    profile = ContentProfile(dat)
    try:
        for region, town, *coordinates in towns:
            if town in seen or len(coordinates) != len(SERVICES):
                raise ValueError(f"Incomplete or duplicate workshop row for {town}")
            seen.add(town)
            for slot, (coordinate, entity) in enumerate(zip(coordinates, REGIONAL_MODELS[region])):
                (north, east), = positions(coordinate)
                evidence = {"town": town, "compass": coordinate, "url": SOURCE}
                if picture := MODEL_PICTURES.get((town, SERVICES[slot])):
                    evidence["model_reference"] = f"Archived AC2 Wiki image: {picture}"
                if town == "Cavendo" and slot == 1:
                    observed.append({"id": CAPTURED_REFINERY, "entity": entity, "evidence": evidence})
                    continue
                npcs.append({"id": 0x8140000000200000 + TOWN_SLOT[town]*16 + slot,
                             "foundation_offset_m": FOUNDATION_OFFSETS[town][slot],
                             "entity": entity, "land": [round(1020 + east*10.2, 4), round(1020 + north*10.2, 4)],
                             "evidence": evidence})

        if seen != TOWN_SLOT.keys() - {"Arwic"}:
            raise ValueError(f"Unexpected set of workshop towns: {seen}")
        # Arwic's preexisting supplemental yard is not listed by this guide.
        for slot, entity in enumerate(LINVAK):
            station = {"id": 0x8140000000200000 + slot, "entity": entity,
                       "land": [958.53 + slot*0.75, 1469.3],
                       "evidence": {"town": "Arwic", "basis": "Existing AC2E yard"}}
            if slot == 0:  # Clear the Human Weapon Vendor inside the old crafting footprint.
                station["foundation_offset_m"] = (-8, 0)
            npcs.append(station)
        for models in REGIONAL_MODELS.values():
            for entity, name in zip(models, SERVICES):
                visual = profile.visual(entity)
                if not visual:
                    raise ValueError(f"Missing native workshop model for {entity:08x}")
                definitions[str(entity)] = {"visual": visual, "name": name}
    finally:
        profile.close()
    return {"version": 1, "policy": "Guide coordinates resolve to nearby walkable native terrain. "
            "The Cavendo refinery is captured and three other models match archived town photographs. "
            "Remaining models use native regional styles; uncaptured orientations are inferred. "
            "Arwic retains its supplemental yard with the crafting building clear of the weapon vendor.",
            "npcs": sorted(npcs, key=lambda n: n["id"]), "captured_stations": observed,
            "camps": [], "templates": definitions}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--guide", type=Path, default=ROOT / "Saved/AC2Hero/workshops.html")
    parser.add_argument("--dat", type=Path, default=ROOT / "Asheron's Call 2 - Legions")
    args = parser.parse_args()
    result = build(args.guide, args.dat)
    (CONTENT / "workshops-content-v1.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
