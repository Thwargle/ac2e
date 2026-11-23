"""Identify the unplaced steps of active AC2E quest policies in the prepared world."""


def audit_routes(atlas, policies, placed, item_sources, rewards, native, portal_transitions, academy):
    def label(entity):
        did = f"{entity:08x}"
        return f"{native.get(did, {}).get('name') or 'Unnamed template'} ({did})"

    checks = {}
    for did, quest in policies.items():
        missing = []
        unverified = []

        def require_any(role, entities):
            if entities and not any(entity in placed for entity in entities):
                missing.append(f"{role}: " + " or ".join(map(label, entities)))

        for field, role in (("npc", "Quest NPC"), ("giver", "Giver"), ("recipient", "Recipient")):
            if quest.get(field):
                require_any(role, [quest[field]])
        for number, objective in enumerate(quest.get("objectives", []), 1):
            require_any(f"Objective {number}", [objective])
        require_any("Hunt target", quest.get("targets", []))
        starters = quest.get("kill_starters", [])
        items = quest.get("use_starters", [])
        if items and not any(item in item_sources or item in placed for item in items):
            if not any(entity in placed for entity in starters) and quest.get("alternate_giver") not in placed:
                unverified.append("Starter item source: " + " or ".join(map(label, items)))
        elif not items:
            require_any("Encounter that starts the quest", starters)
        if did == "4000fe01":  # Gambling Debt is dispatched by the two NPC templates in recovered_quests.rs.
            require_any("Giver", [0x47001a3c])
            require_any("Recipient", [0x47001c27])
        dungeon = {"4000010e": 0x07f7, "40000111": 0x07fa, "40000113": 0x0afc}.get(did)
        if dungeon:
            entrance = any(dest >> 16 == dungeon and start >> 16 != dungeon for start, dest in portal_transitions)
            exit_route = any(start >> 16 == dungeon and dest >> 16 != dungeon for start, dest in portal_transitions)
            if not entrance:
                unverified.append("Captured Academy entrance and destination are not enabled in AC2E"
                                  if did == academy["quest"] else "Dungeon entrance has no verified placed portal route")
            if not exit_route:
                unverified.append("Captured Academy surface exit and destination are not enabled in AC2E"
                                  if did == academy["quest"] else "Return route from the dungeon is unverified")
        if did == academy["quest"]:
            unverified.extend(academy["remaining_steps"])
        checks[did] = missing, unverified

    # A repeatable follow-up can have every actor in the world but still depend
    # on an earlier hunt with no placed target. Check the whole prerequisite chain.
    def prerequisite_gaps(did, seen):
        quest = policies[did]
        chain = list(quest.get("prerequisites", []))
        if isinstance(quest.get("prerequisite"), int) and quest["prerequisite"]:
            chain.append(quest["prerequisite"])
        result = []
        for prior in chain:
            prior = f"{prior:08x}"
            if prior in seen:
                raise ValueError(f"Cyclic quest prerequisites at {prior}")
            if prior not in checks or any(checks[prior]) or prerequisite_gaps(prior, seen | {prior}):
                result.append(f"Prerequisite: {policies.get(prior, {}).get('title', prior)}")
        return result

    guides = {id(record) for group in atlas.page_quests.values() for record in group}
    for record in atlas.records.values():
        if record["kind"] != "Quest":
            continue
        ids = set(record.get("ids", [])) | set(record["facts"].get("AC2E journal ID", []))
        if "4000000c" in ids and "4000000c" not in policies:
            atlas.fact(record, "Route evidence", "AC2Hero names the Dustbin Grutt Rathunter as the source of the "
                       "Rat Smasher Charm. The Legions/2017 item IDs are 47000f6c/47001863, both with visual 1f0000c1.")
            atlas.fact(record, "Route evidence", "Using the charm starts a three-hour hunt. The native journal orders "
                       "ten Tanglefoot Vermin before ten Dank Vermin. AC2Hero lists 300 XP, 70 quest points and "
                       "+20 maximum health for six hours. The matching Legions/2017 effects are 6f000135/6f000d88.")
            if 0x47000f6c not in item_sources and 0x47000f6c not in placed:
                atlas.fact(record, "Quest route gap", "No verified charm source in the prepared world. The Dustbin "
                           "Rathunter has death-loot profile 546 and no native trophy-rate slot in either DAT.")
            atlas.fact(record, "Quest route gap", "Charm activation and ordered kill credit are unimplemented. "
                       "The guide lists the reset timer as unknown.")
            atlas.fact(record, "Route source", "http://ac2hero.e5i5o.com/quest/osteth_ratsmashers.html")
        if "4000028f" in ids and "4000028f" not in policies:
            atlas.fact(record, "Route evidence", "At the Deru Tree in Rakani (28.1 N, 55.1 E), turn in the Chaos "
                       "Power Crystal (470022f5) for 100,000 XP, a Deru Heartwood Talisman and a Nature's Ally Drum.")
            atlas.fact(record, "Quest route gap", "The Redberry Preserves, butterfly, cocoon and temporary Purified "
                       "Spirit Spawn exchanges must supply Enchanted Silk Thread and a Purified Spawn Husk.")
            atlas.fact(record, "Quest route gap", "The Anthracite Necklace must open the Chaos Dungeon. The Lasher "
                       "and fountain trades must empower the husk and temporarily turn wisps into pearl-bearing rabbits.")
            atlas.fact(record, "Quest route gap", "The Pearl Choker trade at the Undulating Slithis Tentacle must "
                       "supply the Slithis Heart. The heart opens the chamber for the Chaos Breach exchange.")
            for entity, name in ((0x47002303, "Chaos Breach"), (0x4700089a, "Great Deru Shrine")):
                if entity not in placed:
                    atlas.fact(record, "Quest route gap", f"No prepared placement for {name} ({entity:08x}).")
            atlas.fact(record, "Timing evidence", "AC2Hero lists a seven-day time limit. The Nature's Ally wiki "
                       "lists seven hours. Both list a seven-day reset.")
            atlas.fact(record, "Route source", "http://ac2hero.e5i5o.com/quest/omishan_naturesfriend.html")
            atlas.fact(record, "Route source", "https://20oz.net/index.php/Nature%27s_Ally.html")
        for did in sorted(ids & rewards.keys()):
            reward = rewards[did]
            atlas.fact(record, "Documented quest points", f"{reward['points']:,} ({did})")
            atlas.fact(record, "Quest point evidence", reward["source"])
        explicit = set(record["facts"].get("AC2E journal ID", []))
        matching = {did for did in ids & checks.keys() if policies[did]["title"].casefold() == record["name"].casefold()}
        route_ids = (explicit or matching or ids) & checks.keys()
        for did in sorted(route_ids):
            quest = policies[did]
            if did in {"400000cc", "400000f0"}:
                if did == "400000cc":
                    source = quest["source"]["url"]
                else:
                    source = quest["evidence"][0]["url"]
                atlas.fact(record, "XP source used by AC2E", source)
            if did == academy["quest"]:
                for fact in academy["evidence"]:
                    atlas.fact(record, "New Mage Academy evidence", fact)
            percent = quest.get("xp_basis_points", 0)
            if percent:
                cap = quest.get("xp_cap")
                summary = f"{percent / 100:g}% of one level" + (f", capped at {cap:,} XP" if cap else "")
                atlas.fact(record, "Server completion XP", summary)
            elif amount := quest.get("xp_cap") or quest.get("xp"):
                atlas.fact(record, "Server completion XP", f"{amount:,} XP")
            if did not in rewards:
                atlas.fact(record, "Quest point evidence", f"No verified award for {did}")
            missing, unverified = checks[did]
            for step in missing + unverified + prerequisite_gaps(did, {did}):
                atlas.fact(record, "Quest route gap", f"{policies[did]['title']} ({did}): {step}" if len(route_ids) > 1 else step)
        if len(route_ids) > 1:
            status = "Multiple journal identities; see individual quests"
        elif record["facts"].get("Quest route gap"):
            status = "Missing or unverified quest steps"
        elif record["facts"].get("Partial quest support"):
            status = "Partial implementation"
        elif route_ids:
            status = "Required actor templates present"
        elif record["facts"].get("Quest policy"):
            status = "Dedicated quest script"
        elif id(record) in guides:
            status = "No linked quest policy"
        else:
            continue
        atlas.fact(record, "Quest route status", status)
