"""Content comparisons must explain both recovery and regressions without mixing DAT sets."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import prepare_content
from content_profile import ContentProfile, nearby_landmark


class ContentComparisonTest(unittest.TestCase):
    def test_existing_did_with_wrong_model_is_withheld_not_repositioned(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = root / 'AC2E/emulator/content'
            content.mkdir(parents=True)
            wrong = '8140aab372ef1689\t4700334c\t1f0010a8\t77b80006\t1.37962341\t100.779083\t182.50499\t1\t0\t0\t0\t-'
            valid = wrong.replace('8140aab372ef1689', '8140000000000002').replace('1f0010a8','1f0010a6')
            for name, rows in [('world-observed-expanded.tsv', []), ('training-world.tsv', []),
                               ('capture-spawns.tsv', [wrong, valid])]:
                (content / name).write_text('pyrealium-world-v3\n' + '\n'.join(rows))
            index = {'portal.dat': {0x4700334c,0x1f0010a8,0x1f0010a6}, 'cell_1.dat': {0x77b8ffff}}
            output = root / 'world.tsv'
            with patch.object(prepare_content,'ROOT',root), patch.object(prepare_content,'asset_index',return_value=index), \
                 patch.object(prepare_content,'ContentProfile') as profile:
                profile.return_value.visual.return_value = 0x1f0010a6
                prepare_content.prepare(root,output)
            self.assertEqual(output.read_text().splitlines(), ['pyrealium-world-v3',valid])
            report = json.loads(output.with_suffix('.report.json').read_text())
            self.assertEqual(report['incompatible_capture_placements'][0]['id'],'8140aab372ef1689')
            self.assertEqual(report['incompatible_capture_placements'][0]['dat_visual'],'1f0010a6')
            self.assertIn(wrong,(content / 'capture-spawns.tsv').read_text())

    def test_landmark_comparison_crosses_outdoor_cells_but_not_dungeon_rooms(self):
        self.assertTrue(nearby_landmark(0x76b80022,(85.5575,29.5398,182.505),
                                       0x76b8001a,(77.260666,27.62573,182.5)))
        self.assertTrue(nearby_landmark(0x76b80040,(159,20,182.5),0x77b80001,(1,20,182.5)))
        self.assertFalse(nearby_landmark(0x02990109,(0,0,-44),0x0299010a,(0,0,-44)))
        self.assertFalse(nearby_landmark(0x76b80022,(0,0,182),0x76b80022,(13,0,182)))

    def test_installed_legions_template_rejects_reported_capture_building(self):
        directory = prepare_content.ROOT / "Asheron's Call 2 - Legions"
        if not (directory / 'portal.dat').exists():
            self.skipTest('Local Legions DAT is unavailable')
        profile = ContentProfile(directory)
        try:
            self.assertEqual(profile.visual(0x4700334c),0x1f0010a6)
            self.assertEqual(profile.visual(0x47002224),0x1f000bf9)  # Child PhysObj overrides its Gurog parent.
            self.assertIsNone(profile.visual(0x470007de))  # Script-only lifestone uses curated pair.
        finally:
            profile.close()

    def test_candidate_restores_one_room_but_loses_one_visual(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = root / 'AC2E/emulator/content'
            content.mkdir(parents=True)
            outdoor = '8140000000000001\t47000001\t1f000001\t01020002\t0\t0\t0\t1\t0\t0\t0\t1'
            interior = '8140000000000002\t47000002\t1f000002\t01020123\t0\t0\t0\t1\t0\t0\t0\t1'
            for name, rows in [('world-observed-expanded.tsv', [outdoor, interior]),
                               ('training-world.tsv', []), ('capture-spawns.tsv', [])]:
                (content / name).write_text('pyrealium-world-v3\n' + '\n'.join(rows))
            current = {'portal.dat': {0x47000001, 0x47000002, 0x1f000001, 0x1f000002},
                       'cell_1.dat': {0x0102ffff}}
            candidate = {'portal.dat': {0x47000001, 0x47000002, 0x1f000002},
                         'cell_1.dat': {0x0102ffff, 0x01020123}}
            output = root / 'result.tsv'
            with patch.object(prepare_content, 'ROOT', root), patch.object(prepare_content, 'ContentProfile'), patch.object(prepare_content, 'asset_index', side_effect=[current, candidate]):
                prepare_content.prepare(root / 'installed', output, root / 'candidate')
            report = json.loads(output.with_suffix('.report.json').read_text())
            self.assertEqual(output.read_text().splitlines(), ['pyrealium-world-v3', outdoor])
            self.assertEqual(report['missing_assets'][0]['missing_records'],
                             [{'kind': 'cell', 'archive': 'cell_1.dat', 'record': '01020123'}])
            self.assertEqual(report['comparison']['recoverable_placements'], 1)
            self.assertEqual(report['comparison']['currently_supported_placements_missing_in_candidate'][0]['missing_records'],
                             [{'kind': 'visual', 'archive': 'portal.dat', 'record': '1f000001'}])
            self.assertFalse((root / 'installed').exists())
            self.assertFalse((root / 'candidate').exists())


if __name__ == '__main__':
    unittest.main()
