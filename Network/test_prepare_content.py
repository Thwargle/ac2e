"""Content comparisons must explain both recovery and regressions without mixing DAT sets."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import prepare_content


class ContentComparisonTest(unittest.TestCase):
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
            with patch.object(prepare_content, 'ROOT', root), patch.object(prepare_content, 'asset_index', side_effect=[current, candidate]):
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
