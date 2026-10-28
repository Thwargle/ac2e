"""Build a DAT-compatible subset of upstream placements without changing game assets."""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools/AC2'))
from dat_probe import Archive

def prepare(directory, output):
    portal = Archive(directory / 'portal.dat')
    cells = Archive(directory / 'cell_1.dat')
    present = {entry[0] for entry in portal.entries()}
    cell_ids = {entry[0] for entry in cells.entries()}
    lines = ['pyrealium-world-v3']
    skipped = []
    inactive = []
    seen = set()
    lifestones = {}
    duplicates = []
    content = ROOT / 'AC2E/emulator/content'
    for name in ['world-observed-expanded.tsv', 'training-world.tsv', 'capture-spawns.tsv']:
        for line in (content / name).read_text().splitlines()[1:]:
            if not line or line.startswith('#'):
                continue
            fields = line.split('\t')
            iid, entity, visual, cell = [int(v, 16) for v in fields[:4]]
            # Outdoor cells use the landblock record; interior cells have individual records.
            record = cell if cell & 65535 >= 256 else cell | 65535
            if entity not in present or visual not in present or record not in cell_ids:
                skipped.append({'source': name, 'id': fields[0], 'entity': fields[1], 'visual': fields[2], 'cell': fields[3]})
                continue
            if iid not in seen:
                if entity == 0x470007de:
                    position = tuple(float(v) for v in fields[4:7])
                    previous = lifestones.setdefault(cell, [])
                    duplicate = next((old for old, p in previous if sum((a-b)**2 for a,b in zip(position,p)) <= 64), None)
                    if duplicate is not None:
                        duplicates.append({'source': name, 'id': fields[0], 'retained': f'{duplicate:016x}'})
                        continue
                    previous.append((iid, position))
                lines.append(line)
                seen.add(iid)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    report = {'included': len(seen), 'excluded': len(skipped), 'missing_assets': skipped,
              'inactive_training_services': inactive, 'duplicate_landmarks': duplicates}
    output.with_suffix('.report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'Compatible server placements: {len(seen)}; missing DAT assets: {len(skipped)}; duplicate landmarks: {len(duplicates)}; inactive training service props: {len(inactive)}')

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('dat_directory', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    prepare(a.dat_directory, a.output)
