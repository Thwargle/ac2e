"""Build a DAT-compatible subset of upstream placements without changing game assets."""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools/AC2'))
from dat_probe import Archive


def asset_index(directory):
    """Read a complete candidate installation without modifying its archives."""
    result = {}
    for name in ('portal.dat', 'cell_1.dat'):
        archive = Archive(directory / name)
        try:
            result[name] = {entry[0] for entry in archive.entries()}
        finally:
            archive.file.close()
    return result


def missing_records(fields, index):
    entity, visual, cell = [int(v, 16) for v in fields[1:4]]
    record = cell if cell & 65535 >= 256 else cell | 65535
    return [{'kind': kind, 'archive': archive, 'record': f'{did:08x}'}
            for kind, archive, did in [('entity', 'portal.dat', entity),
                                      ('visual', 'portal.dat', visual),
                                      ('cell', 'cell_1.dat', record)]
            if did not in index[archive]]

def prepare(directory, output, compare_directory=None):
    index = asset_index(directory)
    comparison = asset_index(compare_directory) if compare_directory else None
    lines = ['pyrealium-world-v3']
    skipped = []
    inactive = []
    seen = set()
    lifestones = {}
    duplicates = []
    candidate_regressions = []
    content = ROOT / 'AC2E/emulator/content'
    for name in ['world-observed-expanded.tsv', 'training-world.tsv', 'capture-spawns.tsv']:
        for line in (content / name).read_text().splitlines()[1:]:
            if not line or line.startswith('#'):
                continue
            fields = line.split('\t')
            iid, entity, visual, cell = [int(v, 16) for v in fields[:4]]
            # Outdoor cells use the landblock record; interior cells have individual records.
            missing = missing_records(fields, index)
            if missing:
                entry = {'source': name, 'id': fields[0], 'entity': fields[1], 'visual': fields[2],
                         'cell': fields[3], 'missing_records': missing}
                if comparison is not None:
                    entry['candidate_missing_records'] = missing_records(fields, comparison)
                    entry['candidate_has_required_records'] = not entry['candidate_missing_records']
                skipped.append(entry)
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
                if comparison is not None:
                    absent = missing_records(fields, comparison)
                    if absent:
                        candidate_regressions.append({'id': fields[0], 'source': name, 'missing_records': absent})
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    report = {'included': len(seen), 'excluded': len(skipped), 'missing_assets': skipped,
              'inactive_training_services': inactive, 'duplicate_landmarks': duplicates,
              'dat_directory': str(directory.resolve()),
              'missing_record_counts': {kind: len({r['record'] for e in skipped for r in e['missing_records'] if r['kind'] == kind})
                                       for kind in ('entity', 'visual', 'cell')}}
    if comparison is not None:
        report['comparison'] = {'dat_directory': str(compare_directory.resolve()),
                                'recoverable_placements': sum(e['candidate_has_required_records'] for e in skipped),
                                'currently_supported_placements_missing_in_candidate': candidate_regressions,
                                'note': 'Presence check only; dependent assets and protocol compatibility must also be verified.'}
    output.with_suffix('.report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'Compatible server placements: {len(seen)}; placements missing DAT records: {len(skipped)}; duplicate landmarks: {len(duplicates)}; inactive training service props: {len(inactive)}')
    if skipped:
        print(f'Missing unique records: {report["missing_record_counts"]}. Details: {output.with_suffix(".report.json")}')
    if comparison is not None:
        print(f'Candidate contains required records for {report["comparison"]["recoverable_placements"]}/{len(skipped)} excluded placements; installed DATs unchanged.')
        print(f'Currently supported placements missing in candidate: {len(candidate_regressions)}')

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('dat_directory', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--compare-dat', type=Path, help='Read-only comparison with another complete installation; does not merge archives.')
    a = p.parse_args()
    prepare(a.dat_directory, a.output, a.compare_dat)
