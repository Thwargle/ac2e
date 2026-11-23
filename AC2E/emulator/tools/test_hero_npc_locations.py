import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from build_hero_atlas import Atlas


class NpcLocationTests(unittest.TestCase):
    def setUp(self):
        directory = TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name)
        (path / "crawl.json").write_text(json.dumps([]), encoding="utf8")
        self.atlas = Atlas(path)

    def test_alternate_names_keep_separate_outposts(self):
        ancestor = self.atlas.record("NPC", "Armoredillo Doom Ancestor")
        for alias, ns, ew in [("Arwic", 43.6, -7.9), ("Esper", 40.7, -13.6), ("Lost Wish", 42.2, 12.9)]:
            actor = self.atlas.record("NPC", "Armoredillo Ancestor Head - " + alias, "search/npcsearchf0d9.html")
            self.assertIs(actor, ancestor)
            actor["places"].append(["Guide NPC", ns, ew, "search/npcsearchf0d9.html"])
        self.assertEqual(len(self.atlas.export()["records"]), 1)
        self.assertEqual(len(ancestor["places"]), 3)
        self.assertTrue(all(len(p) == 4 for p in ancestor["places"]))
        self.assertIsNot(self.atlas.record("NPC", "Ancestor Head - Farali"), ancestor)

    def test_dispute_is_scoped_to_actor_and_page(self):
        actor = self.atlas.record("NPC", "Akhri the Egg Collector")
        page = "quest/omishan_chthonicdarkenfowlegghunt.html"
        actor["places"].extend([["Guide NPC", -5.8, 44.3, page], ["Guide NPC", 6.9, 43.9, page]])
        quest = self.atlas.record("Quest", "Chthonic Darkenfowl Egg Hunt")
        quest["places"].extend([["Guide start", -5.8, 44.3, page], ["Guide waypoint", 6.9, 43.9, page]])
        other = self.atlas.record("NPC", "Another NPC")
        other["places"].append(["Guide NPC", -5.8, 44.3, page])
        another_quest = self.atlas.record("Quest", "Another quest")
        another_quest["places"].append(["Guide waypoint", -5.8, 44.3, "another.html"])
        self.atlas.export()
        self.atlas.export()
        self.assertEqual(actor["places"][0][4:], ["Disputed"])
        self.assertEqual(quest["places"][0][4:], ["Disputed"])
        self.assertEqual(len(actor["facts"]["Location evidence"]), 1)
        for p in [actor["places"][1], quest["places"][1], other["places"][0], another_quest["places"][0]]:
            self.assertEqual(len(p), 4)

    def test_dungeon_entrance_is_not_an_outdoor_npc_location(self):
        actor = self.atlas.record("NPC", "Eshivon Ugin")
        actor["places"].append(["Guide NPC", 38.6, -25.4, "search/npcsearchfd13.html"])
        quest = self.atlas.record("Quest", "Recover the Arcanum's Library")
        quest["places"].append(["Guide start", 38.6, -25.4, "quest/osteth_arcanumslibrary.html"])
        vault = self.atlas.record("Dungeon", "Arcanum Vault")
        vault["places"].append(["Vault entrance", 38.6, -25.4, "vaults.html"])
        self.atlas.export()
        self.assertEqual(actor["places"][0][4:], ["Dungeon"])
        self.assertEqual(quest["places"][0][4:], ["Dungeon"])
        self.assertEqual(vault["places"][0][4:], [])


if __name__ == "__main__":
    unittest.main()
