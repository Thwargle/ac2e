"""Reviewed AC2Hero guide identities that differ from the server journal titles."""
from collections import defaultdict
import json
from urllib.parse import urlsplit


# (Guide page, quest index label, AC2E journal ID). Bind an individual entry
# even if another quest is described on the same walkthrough.
PLAYABLE = (
    ("osteth_killcaptainsneer", "Kill Captain Sneer", "400000f0"),
    ("linvak_killfangthevarmit", "Kill Fang the Varmit", "40000110"),
    ("linvak_killmalikthesadist", "Kill Malik the Sadist", "40000112"),
    ("linvak_killthemasterguroghenchman", "Kill the Gurog Master Henchman", "40000113"),
    ("linvak_killossein", "Kill Ossein", "40000114"),
    ("osteth_thewarderscoutsnotebook", "Warder Scouts Notebook", "400000c4"),
    # These NPC hunts were reconstructed with AC2E journal IDs. Their givers
    # and objectives distinguish them from kill-triggered incursions.
    ("arramora_exterminatetheolthoinymphworkers", "Exterminate the Olthoi Nymph Worker", "4000fc11"),
    ("omishan_driveoutthemimbukachem", "Drive out the Mimbu Kachem", "4000fc1b"),
    ("omishan_deflatetheparfalnephanes", "Deflate the Parfal Nephanes", "4000fc1c"),
    ("omishan_puncturetheopornefane", "Puncture the Opor Nefane", "4000fc22"),
    ("knorr_exterminatetheiridescentpiercers", "Exterminate Iridescent Piercers", "4000fc25"),
    ("osteth_exterminatethephyntoswasps", "Exterm. the Green Phyntos Wasps", "4000fc2d"),
)

# Each guide names its native journal quest in the walkthrough heading or the
# objective. A native journal record alone does not establish a server policy.
JOURNALS = (
    ("arramora_annihilatehighpriestofisishaa", "Annihilate High Priest of Isishaa", "40000234"),
    ("arramora_annihilaterumpustheharrasser", "Annihilate Rumpus the Thornling Harrasser", "40000262"),
    ("omishan_blackferahsshadow", "Black Ferahs Shadow", "40000373"),
    ("osteth_burundespoilment", "Burun Despoilment", "40000062"),
    ("omishan_clensetheburialmound", "Clense the Burial Mound", "40000069"),
    ("linvak_dillorustler", "Dillo Rustler", "4000006e"),
    ("osteth_fateofthescholars", "Fate of the Scholars / Cacaphos", "4000033b"),
    ("osteth_cragstoneexcavation", "Old Cragstone Excavation", "400000d3"),
    ("omishan_sclavushierophantscabals", "Sclavus Hierophant's Cabals", "40000101"),
    ("omishan_spiritguide", "Spirit Guide - Cesta", "400002bd"),
    ("omishan_starofheroes", "Star of Heroes", "400001cf"),
)


def guide_entry(atlas, slug, title):
    page = f"quest/{slug}.html"
    return next(record for record in atlas.page_quests[page]
                if record["name"] == title or title in record["facts"].get("Guide name", ()))


def link_quest_policies(atlas, content, policy_titles):
    journals = {journal: r for r in atlas.records.values()
                if r["kind"] == "Quest" and r["facts"].get("Journal package")
                for journal in r["facts"].get("Journal definition", [])}

    def add_native_id(guide, journal):
        ids = guide.setdefault("ids", [])
        if journal not in ids:
            ids.append(journal)

    for slug, title, journal in PLAYABLE:
        guide = guide_entry(atlas, slug, title)
        policy = atlas.record("Quest", policy_titles[journal])
        atlas.link(guide, policy, "Server quest identity")
        atlas.fact(guide, "AC2E journal ID", journal)
        if journal in journals:
            add_native_id(guide, journal)
        for filename in policy["facts"]["Quest policy"]:
            atlas.fact(guide, "Quest policy", filename)

    for slug, title, journal in JOURNALS:
        guide = guide_entry(atlas, slug, title)
        atlas.link(guide, journals[journal], "Native journal identity")
        atlas.fact(guide, "Journal definition", journal)
        add_native_id(guide, journal)

    # A source specific to one journal and one index entry can resolve a renamed quest.
    # Several rewards cite the same overview page, which cannot identify a single row.
    source_journals = defaultdict(set)
    for journal, award in json.loads((content / "quest-point-rewards-v1.json").read_bytes()).items():
        url = urlsplit(award["source"])
        if url.hostname == "ac2hero.e5i5o.com":
            source_journals[url.path.lstrip("/")].add(journal)
    for page, ids in source_journals.items():
        entries = atlas.page_quests.get(page, [])
        if len(ids) != 1 or len(entries) != 1:
            continue
        journal = next(iter(ids))
        guide = entries[0]
        add_native_id(guide, journal)
        if journal in journals:
            atlas.link(guide, journals[journal], "Journal identified by reward guide")
        if journal in policy_titles:
            policy = atlas.record("Quest", policy_titles[journal])
            atlas.fact(guide, "AC2E journal ID", journal)
            for filename in policy["facts"]["Quest policy"]:
                atlas.fact(guide, "Quest policy", filename)

    # Regional overview pages list many independent quests. Match named
    # starter cells only. Targets are often shared with unrelated NPC hunts.
    starters = defaultdict(list)
    for record in atlas.records.values():
        if record["kind"] != "Quest" or not record["facts"].get("Quest policy"):
            continue
        if not (record["name"].startswith("Incursion:") or record["name"].endswith(" Raid")):
            continue
        for role, kind, name in record["links"]:
            if (role, kind) == ("Quest starter", "Creature"):
                starters[name.casefold()].append(record)
    for region, label in (("osteth", "Osteth Incursion Quest Starters"),
                          ("omishan", "Omishan Incursion Quest Starters"),
                          ("linvak", "Linvak Massif Incursion Quest Starters"),
                          ("arramora", "Arramora Incursion Quests")):
        slug = f"{region}_incursion"
        guide = guide_entry(atlas, slug, label)
        for row in atlas.read(f"quest/{slug}.html").xpath("//tr[not(.//tr)]"):
            cells = row.xpath("./td")
            if len(cells) < 4 or not cells[0].text_content().strip().isdigit():
                continue
            starter = " ".join(cells[1].text_content().split()).casefold()
            for policy in starters.get(starter, ()):
                atlas.link(guide, policy, "Listed incursion with server policy")

    generals = guide_entry(atlas, "knorr_aerfallesgenerals", "Aerfalle's Generals")
    pack = json.loads((content / "foundry-generals-v1.json").read_bytes())
    for key in pack["quest_keys"]:
        creature = atlas.record("Creature", pack["templates"][str(key["entity"])]["name"])
        atlas.link(generals, creature, "Server general with gated key drop")
    atlas.fact(generals, "Partial quest support",
               "Four generals spawn and carry gated keys. Foundry Gates and the Troop Status starter are missing.")

    nature = guide_entry(atlas, "omishan_naturesfriend", "Nature's Friend")
    pack = json.loads((content / "nature-exchanges-v1.json").read_bytes())
    for rule in pack["rules"]:
        atlas.link(nature, atlas.record("NPC", rule["npc"]["name"]), "Server material exchange")
    atlas.fact(nature, "Partial quest support",
               "Four material exchanges and the Black Mother Earth blossom and garland steps are playable. "
               "The final turn-in and rewards are missing.")
