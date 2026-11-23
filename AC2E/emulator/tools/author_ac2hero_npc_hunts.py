"""Bind AC2Hero's Knorr hunts to the installed speakers, death effects and reward keys."""
import json
from author_ac2hero_hunts import CONTENT, ROOT, field, hours, number, page, plain, load


def build():
    templates = {int(k): v for k, v in load(ROOT/"Saved/current-native-templates.json").items()}
    wiki = {a["title"]: a for a in load(ROOT/"Saved/ContentRecovery/wiki-content.json")["articles"]}
    # AC2Hero gives approximate or unspecified percentages for Ivor and Savorlin.
    # Use their independently documented XP amounts. The wiki corroborates
    # Arlen's cap and Yurikhe's cap at 59. Timers follow the AC2Hero walkthroughs.
    hunts = [
        ("destroythesorrowthralls", "Destroy the Sorrow Thralls", "Arlen Jenis", "Eff_IncrementQuest_KQ4B_SorrowThrall", "Sorrow Thrall", 20, 60, 200, 168042895, "in the Knorr Ramparts", ""),
        ("exterminatetheiridescentpiercers", "Exterminate the Iridescent Piercers", "Ivor Stronghand", "Eff_IncrementQuest_KQ1H_IridescentPiercer", "Iridescent Piercer", 10, 45, 0, 3900000, "north of Yaosenji", ""),
        ("killthechromusclavus", "Kill the Chromu Sclavus", "Yurikhe", "Eff_IncrementQuest_KQ3C_ChromuSclavus", "Chromu Sclavus", 15, 55, 200, 102639801, "in the southern Knorr Gardens", ""),
        ("slaythebanderlingberserkers", "Slay the Banderling Berserkers", "Gwynna Stronghand", "Eff_IncrementQuest_KQ1J_BanderlingBerserker", "Banderling Berserker", 10, 48, 0, 7287438, "in the Knorr Wilds", ""),
        ("slaythebanderlingmaulers", "Slay the Banderling Maulers", "Savorlin Gahntar", "Eff_IncrementQuest_KQ1G_BanderlingMauler", "Banderling Mauler", 10, 44, 0, 2800000, "east of Yaosenji", ""),
        ("removetheiridescentstinger", "Remove the Iridescent Stinger", "Muriona", "Eff_AdvanceQuest_BM1D_IridescentStingerPhase2", "Iridescent Stinger", 1, 41, 0, 300000, "near 33.9S, 32.8E", "Muriona's Key"),
        ("treeavenger", "Tree Avenger", "Tiamary Graziel", "Eff_AdvanceQuest_BM1A_TreeAvengerPhase2", "Cunning Tom", 1, 34, 0, 200000, "near 31.4S, 37.6E", "Tiamary's Key"),
    ]
    rows = []
    for i, (slug,title,speaker,effect,monster,goal,level,bps,xp,hint,key) in enumerate(hunts,0x24):
        markup, source = page("knorr_"+slug)
        guide = plain(markup)
        assert all(term in guide for term in [speaker, monster, str(level), "7 Days"])
        npc, = [d for d,t in templates.items() if t["name"]==speaker and t["props"].get("AI_NPCTable")]
        targets = sorted(d for d,t in templates.items() if t["role"]=="monster" and
                         any(v==effect for k,v in t["props"].items() if k.startswith("NPC_DeathEffect")))
        assert targets and any(templates[d]["name"]==monster for d in targets)
        instruction = (f"Defeat {monster} {hint}." if goal==1 else
                       f"Defeat {goal} {monster}{'' if monster.endswith('us') else 's'} {hint}.")
        entry = dict(id=0x4000fc00+i,title=title,npc=npc,npc_name=speaker,targets=targets,goal=goal,level=level,
                     xp_basis_points=bps,xp_cap=xp,prerequisite=0,limit_seconds=604800,repeat_seconds=604800,
                     text=instruction+" Return to "+speaker+" for your reward.",evidence=[source],
                     native_death_effect=effect)
        if title in wiki:
            article=wiki[title]
            entry["evidence"].append(dict(url=article["url"],sha256=article["html_sha256"]))
        if key:
            key_id, = [d for d,t in templates.items() if t["name"]==key and t["props"].get("Lock_KeyID")]
            assert key in guide and templates[key_id]["props"]["Item_MaxStackSize"]==1
            entry["key"]=dict(item=key_id,visual=templates[key_id]["visual"],name=key)
        rows.append(entry)
    data=dict(version=3,policy="Use AC2Hero goals, minimums, seven-day deadlines and resets. Ivor and Savorlin grant their documented capped amounts; the per-level scaling is unknown. Native names and death effects distinguish same-name monsters. Key hunts grant the actual DAT key. Chest contents and lock bindings are unverified.",quests=rows)
    (CONTENT/"hunting-content-v3.json").write_text(json.dumps(data,indent=2)+"\n",encoding="utf8")


def build_arwic():
    templates = {int(k): v for k, v in load(ROOT/"Saved/current-native-templates.json").items()}
    wiki = {a["title"]: a for a in load(ROOT/"Saved/ContentRecovery/wiki-content.json")["articles"]}
    # Read XP, deadline and repeat from each guide page. The secondary wiki
    # describes a different revision, including a 20-hour reset for three hunts.
    hunts = [
        ("crushthesandyarmoredillos", "Crush the Sandy Armoredillos", "Shiela Ondaran", "ArA_SandyArmoredillo",
         "Sandy Armoredillos", "west of Arwic near 44.1N, 8.3W", None),
        ("destroythetanglefootvermin", "Destroy the Tanglefoot Vermin", "Grey Ondaros", "ArB_TanglefootVermin",
         "Tanglefoot Vermin", "north of Arwic near 45.6N, 6.2W", "Destroy the Tanglefoot Vermin"),
        ("exterminatethephyntoswasps", "Exterminate the Phyntos Wasps", "Krestovmik", "ArC_GreenWasp",
         "Green Phyntos Wasps", "in the ravine south of Arwic near 42.4N, 7.2W", "Exterminate the Green Phyntos Wasps"),
        ("putdownthecarrionshreth", "Put Down the Carrion Shreth", "Oriona", "ArD_CarrionShreth",
         "Carrion Shreth", "east of Arwic near 44.3N, 5.0W", "Put Down the Carrion Shreth"),
    ]
    rows = []
    for i, (slug, title, speaker, binding, monster, location, article) in enumerate(hunts, 0x2b):
        markup, source = page("osteth_"+slug)
        text = plain(markup)
        assert speaker in text and f"5 {monster}" in text and title in text
        req = field(text, "Requirements", "Reward")
        if "Intended" in req:
            level = 1
        elif req == "Level":
            level = number(wiki[article]["facts"]["Requirements"])
        else:
            level = number(req)
        npc, = [d for d,t in templates.items() if t["name"]==speaker and t["props"].get("AI_NPCTable")]
        effect = "Eff_IncrementQuest"+binding
        targets = sorted(d for d,t in templates.items() if t["role"]=="monster" and
                         any(v==effect for k,v in t["props"].items() if k.startswith("NPC_DeathEffect")))
        assert targets
        row = dict(id=0x4000fc00+i,title=title,npc=npc,npc_name=speaker,targets=targets,goal=5,level=level,
                   xp_basis_points=0,xp_cap=number(field(text,"Reward","Time Limit")),prerequisite=0,
                   limit_seconds=hours(field(text,"Time Limit","Reset Timer")),
                   repeat_seconds=hours(text.split("Reset Timer:",1)[1].split("Start Location",1)[0]),
                   text=f"Defeat 5 {monster} {location}. Return to {speaker} in Arwic for your reward.",
                   evidence=[source],native_death_effect=effect)
        if "Intended" in req:
            row["intended_level"] = number(req)
        if article:
            row["evidence"].append(dict(url=wiki[article]["url"],sha256=wiki[article]["html_sha256"]))
        rows.append(row)
    data = dict(version=4, policy="Guide XP, deadlines and resets. Shiela recommends level 5 without a hard minimum. Krestovmik's missing minimum is filled from the secondary wiki. Qualifying creatures carry the native Arwic death effect. Quest journal IDs are authored.",quests=rows)
    (CONTENT/"hunting-content-v4.json").write_text(json.dumps(data,indent=2)+"\n",encoding="utf8")


if __name__ == "__main__":
    build()
    build_arwic()
