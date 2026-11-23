"""Recover the four Foundry generals at the map coordinates in Aerfalle's Generals."""
import argparse
import contextlib
import io
import json
import struct
import tempfile
from pathlib import Path

from import_native_trophies import ContentProfile, Reader, entity, fnv
from author_arwic_creatures import recover

GUIDE = "https://www.20oz.net/index.php/Aerfalle%27s_Generals.html"
# Displayed coordinates are rounded to tenths in the guide. Each named general holds one key.
GENERALS = [
    ("Baron Colier", 0x4700390B, -31.2, 56.8, 0x47003903),
    ("Lord Nuvillus", 0x47003BCA, -35.4, 51.2, 0x47003904),
    ("General Dreyvon", 0x47003B72, -28.9, 61.3, 0x47003905),
    ("General Sarocete", 0x47003B73, -27.6, 64.2, 0x47003906),
]


def export(directory, output):
    profile = ContentProfile(directory)
    try:
        table = Reader(profile.portal.read(0x34000000))
        table.take(12)
        keys = {}
        for _ in range(table.word()):
            key = table.word()
            keys[table.string()] = key

        def properties(did, sources):
            values = {}
            while did >> 24 == 0x47:
                raw = profile.portal.read(did)
                sources[f"{did:08x}"] = fnv(raw)
                row = entity(Reader(raw), profile.types)
                for prop, value in row.get("properties", {}).items():
                    values.setdefault(int(prop[9:], 16), value)
                did = row["data"]
            return {name: values[prop] for name, prop in keys.items() if prop in values}

        pack = dict(version=1, source=GUIDE,
                    policy="One general at each reported map point. The exact frame and two-minute respawn are inferred. Corpse generator identities are native; the guide identifies their individual keys. The retail key probabilities are unavailable.",
                    validate_camp_bodies=True, templates={}, camps=[], npcs=[], quest_keys=[])
        rows = {"ai": [], "combat": []}
        with tempfile.TemporaryDirectory() as temp:
            scratch = Path(temp)
            for index, (name, boss, north, east, key) in enumerate(GENERALS):
                sources = {}
                actor = properties(boss, sources)
                corpse = actor["NPC_CorpseOverrideEntity"]
                container = properties(corpse, sources)
                key_properties = properties(key, sources)
                visual = profile.visual(boss)
                item_visual = profile.visual(key)
                if not visual or not item_visual or not container.get("GeneratorProfile"):
                    raise ValueError(f"Missing native binding for {name}")
                pack["templates"][str(boss)] = dict(name=name, visual=visual)
                scale = struct.unpack("<f", struct.pack("<I", actor.get("Scale", 0x3f800000)))[0]
                pack["camps"].append(dict(id=0x8140ED0000010000 + index * 16, capacity=1,
                                          roster=[boss], rotation=[1, 0, 0, 0], scale=scale,
                                          land=[round(1020 + east * 10.2, 4), round(1020 + north * 10.2, 4)]))
                pack["quest_keys"].append(dict(entity=boss, corpse=corpse, generator=container["GeneratorProfile"],
                                               definition=key, visual=item_visual, sources=sources,
                                               corpse_min_level=container["Usage_MinLevel"],
                                               take_quest=key_properties["Usage_RequiredQuest_ForTaking"],
                                               take_status=key_properties["Usage_RequiredQuestStatus_ForTaking"]))
                with contextlib.redirect_stdout(io.StringIO()):
                    recover(directory, scratch, entity_id=boss, pack="foundry", name=name)
                for kind, lines in rows.items():
                    text = (scratch / f"foundry-creature-{kind}-v1.tsv").read_text().splitlines()
                    if not lines:
                        lines.extend([text[0], "# Inherited Foundry general properties from portal.dat.", text[-2]])
                    lines.append(text[-1])
        for kind, lines in rows.items():
            (output / f"foundry-creature-{kind}-v1.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        (output / "foundry-generals-v1.json").write_text(json.dumps(pack, indent=2) + "\n", encoding="utf-8", newline="\n")
    finally:
        profile.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    export(args.directory, args.output)
