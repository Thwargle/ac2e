"""Author the frozen AC2Hero hunt revision from the offline guide and native DAT export.

Run from a checkout with Saved/AC2Hero and Saved/current-native-templates.json.
Ground the emitted candidate camps with the regional_population example.
"""
from collections import defaultdict
from html import unescape
from pathlib import Path
import hashlib
import json
import math
import re
import struct

from author_recovery_content import CONTENT, ROOT, load
from author_regional_population import inside

SITE = ROOT / "Saved/AC2Hero/quest"
BASE = "http://ac2hero.e5i5o.com/quest/"
NAMESPACE = 0x8140db0000000000


# Alternate bearings / target populations accepted by the native geometry validator.
SITE_CHOICES = {1191190511: 2, 1191190504: 1, 1191190526: 2, 1191190522: 1, 1191190529: 2, 1191190498: 1, 1191190519: 2, 1191190533: 1, 1191190531: 1, 1191190499: 1}


def plain(markup):
    return " ".join(unescape(re.sub(r"<[^>]+>", " ", markup)).split())


def page(name):
    data = (SITE / (name + ".html")).read_bytes()
    return data.decode("windows-1252", errors="replace"), {
        "url": BASE + name + ".html", "sha256": hashlib.sha256(data).hexdigest()
    }


def field(text, start, end):
    return text.split(start + ":", 1)[1].split(end + ":", 1)[0].strip()


def number(text):
    return int(re.search(r"[\d,]+", text)[0].replace(",", ""))


def hours(text):
    count = sum(int(value) * {"day": 86400, "hour": 3600, "minute": 60}[unit.lower()]
                for value, unit in re.findall(r"(\d+)\s*(day|hour|minute)", text, re.I))
    assert count, text
    return count


def detail(row, name):
    markup, source = page(name)
    text = plain(markup)
    row.update(level=number(field(text, "Requirements", "Reward")),
               limit_seconds=hours(field(text, "Time Limit", "Reset Timer")),
               repeat_seconds=hours(field(text, "Reset Timer", "Start Location")))
    reward = field(text, "Reward", "Time Limit")
    pct = re.search(r"(\d+(?:\.\d+)?)%\s*needed", reward)
    if pct:
        row.update(xp=0, xp_basis_points=int(float(pct[1])*100))
    else:
        row["xp"] = number(reward)
    row["evidence"].append(source)


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def build(site_choices=None):
    if site_choices is None:
        site_choices = SITE_CHOICES
    templates = {int(k): v for k, v in load(ROOT/"Saved/current-native-templates.json").items()}
    native = {q["package"]: q for q in load(CONTENT/"native-quests.json")["quests"]}
    starters, targets = {}, defaultdict(list)
    for entity, template in templates.items():
        if template.get("role") != "monster":
            continue
        for i in range(1, 9):
            effect = template["props"].get(f"NPC_DeathEffect{i}", "")
            if not isinstance(effect, str):
                continue
            if effect.startswith("Eff_BestowQuestKill"):
                starters[template["name"]] = (entity, effect.removeprefix("Eff_Bestow"))
            if effect.startswith("Eff_IncrementQuestKill"):
                targets[effect.removeprefix("Eff_Increment")].append(entity)

    markup, index_source = page("arramora_incursion")
    bosses = {q["title"]: q for q in load(CONTENT/"inferred-content-v1.json")["quests"]}
    articles = {a["title"]: a for a in load(CONTENT/"reference-tables.json")["article_index"]}
    quests = []
    boss_sources = {
        "Kill Fang, the Varmint": "linvak_killfangthevarmit",
        "Kill Malik, the Sadist": "linvak_killmalikthesadist",
        "Kill Ossein, Digger of Graves": "linvak_killossein",
        "Kill the Eli Watcher": "linvak_killtheeliwatcher",
        "Kill the Gurog Conjurer": "linvak_killthegurogconjurer",
        "Kill the Gurog Henchman": "linvak_killthemasterguroghenchman",
        "Kill Chief Benamu": "omishan_killchiefbenamu",
        "Kill Cloudburst": "linvak_killcloudburst",
        "Kill Sneer": "osteth_killcaptainsneer",
    }
    for title, source in boss_sources.items():
        old = bosses[title]
        row = {key: old[key] for key in ("id","title","text","goal","targets","kill_starters",
               "use_starters","level","xp","limit_seconds","repeat_seconds")}
        row["evidence"] = []
        if title == "Kill Sneer":
            _, guide = page(source)
            article = articles["Kill Captain Sneer"]
            facts = article["facts"]
            row.update(level=number(facts["Requirements"]), xp=number(facts["XP Reward"]),
                       repeat_seconds=hours(facts["Repeat Time"]), limit_seconds=hours(facts["Time Limit"]))
            row["evidence"] = [dict(url=article["url"],sha256=article["html_sha256"]), guide]
            row["policy"] = "Use the individual AC2Wiki timers, reward and eligibility where the two guides disagree."
        else:
            detail(row,source)
            if title == "Kill Cloudburst":
                row["policy"] = "Guide explicitly marks its XP figure as unverified."
        quests.append(row)

    details = {
        "Diabolical Thornling Punisher": "thornlingpunisherraid",
        "Droning Olthoi Worker Nymph": "olthoiworkernymphraid",
        "Untiring Olthoi Drone": "olthoidroneraid",
        "Probing Effigy Spawn": "effigyspawnraid",
        "Wildly Frothing Mosswart": "frothingmosswartraid",
        "Essorin the Wandering Nomad": "outcastmumiyahraid",
        "Noxious Acid Spurt": "acidspurtraid",
    }
    raid_rows = {}
    for tr in re.findall(r"<tr\b[^>]*>(.*?)</tr>", markup, re.I|re.S):
        cells = [plain(c) for c in re.findall(r"<td\b[^>]*>(.*?)</td>",tr,re.I|re.S)]
        if len(cells)==5 and cells[0].isdigit():
            _, name, goal, timer, reward = cells
            raid_rows[name] = (number(goal), hours(timer), number(reward))
    assert len(raid_rows)==23
    for name in sorted(set(raid_rows) | set(details)):
        entity, package = starters[name]
        q = native[package]
        goal, limit, xp = raid_rows.get(name, (10,10800,0))
        assert len(q["phases"]) == goal, q["package"]
        row = dict(id=q["id"], title=q["name"]["text"], text=q["phases"][0]["journal"]["text"].strip(),
                   goal=goal, targets=sorted(set(targets[package])), kill_starters=[entity],
                   use_starters=[], level=50, xp=xp, limit_seconds=limit, repeat_seconds=7200,
                   package=package, evidence=[index_source] if name in raid_rows else [])
        assert row["targets"]
        if name in details:
            detail(row,"arramora_"+details[name])
        else:
            row["policy"] = "The island raid guides support a two-hour reset and commonly require level 50. The index lists monster levels; this quest's player level and reset are inferred."
        quests.append(row)

    # Anchor a missing named starter near existing populations of its corresponding creature.
    # Exact spawn points / respawn durations are reconstruction. A slot already represented
    # in the documented habitat never receives a second placement.
    wilderness = load(CONTENT/"wilderness-content-v1.json")
    island = next(r for r in wilderness["regions"] if r["name"]=="Arramora")
    polygon = island["polygon"]
    observed = defaultdict(list)
    for line in (ROOT/"AC2E/data/world/legions-world.tsv").read_text(encoding="utf-8").splitlines():
        f=line.split("\t")
        if len(f)!=13 or int(f[0],16) & 0xffffffffff000000 == NAMESPACE:
            continue
        cell = int(f[3],16)
        if not 1 <= cell & 65535 <= 64:
            continue
        xy=[(cell>>24)*8+float(f[4])/20, ((cell>>16)&255)*8+float(f[5])/20]
        if inside(xy,polygon):
            observed[int(f[1],16)].append(xy)
    camps,entities=[],set()
    for row in quests:
        if "package" not in row:
            continue
        starter=row["kill_starters"][0]
        if observed[starter]:
            continue
        # The first native target is the normal population, as opposed to an
        # inherited elite variant or an encounter in another dungeon.
        target=row["targets"][0]
        anchors=sorted(observed[target])
        if not anchors:
            raise ValueError(f"Missing native population anchor: {row['title']}")
        choice = site_choices.get(starter,0)
        index = (row["id"] + choice//8) % len(anchors)
        x,y=anchors[index]
        angle = ((row["id"]+137*choice)%360)*math.pi/180
        candidate=[x+6*math.cos(angle),y+6*math.sin(angle)]
        camps.append(dict(id=NAMESPACE | (row["id"] & 65535)<<8,region="Arramora",
            land=candidate,roster=[starter],capacity=1,polygon=polygon,regeneration_ms=180000,
            anchor_entity=target,anchor_land=[x,y]))
        entities.add(starter)
    write_json(CONTENT/"ac2hero-hunts-v2.json",dict(version=2,quests=quests,camps=camps,
        namespace=NAMESPACE,town_exclusions=wilderness["town_exclusions"],
        templates={k:dict(name=templates[k]["name"],visual=templates[k]["visual"]) for k in sorted(entities)}))
    (CONTENT/"ac2hero-raid-entities.txt").write_text("".join(f"{k:08x}\n" for k in sorted(entities)))
    keys = ["AI_PerceptionRadius","AI_HomesickRadius","AI_Wandering","AI_WanderingRange","AI_WanderingProb",
            "AI_LoveTable","AI_FreeAttacking","AI_IdleOnly","NPC_DeathLootProfile","NPC_IsLootProof"]
    rows=["ac2e-creature-ai-v1"]
    for k in sorted(entities):
        values=[templates[k]["props"].get(key) for key in keys]
        for i in (0,1,3,4):
            if values[i] is not None:
                values[i]=struct.unpack("<f",struct.pack("<I",values[i]))[0]
        rows.append("\t".join([f"{k:08x}"]+["-" if v is None else str(v) for v in values]))
    (CONTENT/"ac2hero-raid-ai-v1.tsv").write_text("\n".join(rows)+"\n")
    print(f"{len(quests)} quest profiles, {len(camps)} candidate raid spawners.")


if __name__ == "__main__":
    build()
