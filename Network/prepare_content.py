"""Build a DAT-compatible subset of upstream placements without changing game assets."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools/AC2'))
from dat_probe import Archive
from content_profile import ContentProfile, nearby_landmark


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
    content = ROOT / 'AC2E/emulator/content'
    newer = content / 'capture-spawns-upstream.tsv'
    audited = content / 'capture-spawns-audited.tsv'
    recovered = content / 'recovered-population.tsv'
    version = 4 if newer.exists() or audited.exists() else 3
    lines = [f'pyrealium-world-v{version}']
    skipped = []
    inactive = []
    seen = set()
    lifestones = []
    duplicates = []
    incompatible = []
    curated_pairs = set()
    profile = ContentProfile(directory)
    candidate_regressions = []
    sources = ['world-observed-expanded.tsv', 'training-world.tsv']
    recovered_cells = set()
    if recovered.exists():
        evidence = json.loads(recovered.with_suffix('.report.json').read_text(encoding='utf-8'))
        if not evidence.get('native_ground_verified') or evidence.get('manifest_sha256') != hashlib.sha256(recovered.read_bytes()).hexdigest():
            raise ValueError('Recovered population requires current native ground verification')
        sources.append(recovered.name)
        version = 4
        lines[0] = 'pyrealium-world-v4'
    sources.append('capture-spawns.tsv')
    if newer.exists(): sources.append(newer.name)
    if audited.exists(): sources.append(audited.name)
    inferred = content / 'inferred-world-v1.tsv'
    if inferred.exists():
        evidence = json.loads(inferred.with_suffix('.report.json').read_text(encoding='utf8'))
        if (not evidence.get('native_ground_verified')
            or evidence['manifest_sha256'] != hashlib.sha256(inferred.read_bytes()).hexdigest()
            or evidence['source_sha256'] != hashlib.sha256((content/'inferred-content-v1.json').read_bytes()).hexdigest()):
            raise ValueError('Inferred content requires fresh native ground validation')
        sources.append(inferred.name)
    state_file=content / 'capture-spawns-audited.report.json'
    state_exclusions={}
    if audited.exists() and state_file.exists():
        evidence=json.loads(state_file.read_text(encoding='utf-8'))
        state_exclusions={e['id']:e for e in evidence['excluded_existing']}
    transient=[]
    input_seen = set()
    input_counts = {}
    earlier_capture_cells = set()
    overlapping = []
    for name in sources:
        source_cells=set()
        for line in (content / name).read_text().splitlines()[1:]:
            if not line or line.startswith('#'):
                continue
            fields = line.split('\t')
            if len(fields) not in (12, 13): raise ValueError(f'Unsupported placement in {name}')
            iid, entity, visual, cell = [int(v, 16) for v in fields[:4]]
            # A later capture cannot replace an earlier placement identity.
            # Incompatible older rows are also retained in the audit exactly once.
            if iid in input_seen: continue
            input_seen.add(iid)
            input_counts[name] = input_counts.get(name, 0) + 1
            if name.startswith('capture-spawns') and cell in recovered_cells:
                overlapping.append({'source':name,'id':fields[0],'cell':fields[3],
                                    'reason':'verified translated snapshot owns this cell'})
                continue
            if name in ('capture-spawns-upstream.tsv','capture-spawns-audited.tsv') and cell in earlier_capture_cells:
                overlapping.append({'source':name,'id':fields[0],'cell':fields[3],
                                    'reason':'earlier capture already owns this cell'})
                continue
            if name == 'world-observed-expanded.tsv' or name.startswith('capture-spawns'):
                source_cells.add(cell)
            exclusion=state_exclusions.get(fields[0])
            if exclusion and exclusion['source']==name and exclusion['row_sha256']==hashlib.sha256(line.encode()).hexdigest():
                transient.append(exclusion)
                continue
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
                if name.startswith('capture-spawns') or name in (recovered.name, inferred.name):
                    expected = profile.visual(entity)
                    if (entity, visual) not in curated_pairs and expected != visual:
                        incompatible.append({'source': name, 'id': fields[0], 'entity': fields[1],
                            'visual': fields[2], 'cell': fields[3],
                            'dat_visual': f'{expected:08x}' if expected else None,
                            'reason': 'visual differs from installed entity template' if expected else
                                      'capture visual cannot be verified against installed templates'})
                        continue
                else:
                    # The curated observation and authored training packs supply
                    # known profiles for script-only templates (e.g. lifestones).
                    curated_pairs.add((entity, visual))
                if entity == 0x470007de:
                    position = tuple(float(v) for v in fields[4:7])
                    duplicate = next((old for old, c, p in lifestones
                                      if nearby_landmark(cell, position, c, p)), None)
                    if duplicate is not None:
                        duplicates.append({'source': name, 'id': fields[0], 'retained': f'{duplicate:016x}'})
                        continue
                    lifestones.append((iid, cell, position))
                if version == 4 and len(fields) == 12: fields.append('-')
                lines.append('\t'.join(fields))
                seen.add(iid)
                if name == recovered.name: recovered_cells.add(cell)
                if comparison is not None:
                    absent = missing_records(fields, comparison)
                    if absent:
                        candidate_regressions.append({'id': fields[0], 'source': name, 'missing_records': absent})
        earlier_capture_cells.update(source_cells)
    profile.close()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    report = {'included': len(seen), 'excluded': len(skipped)+len(incompatible)+len(duplicates)+len(overlapping)+len(transient), 'missing_assets': skipped,
              'transient_capture_placements':transient,
              'source_unique_placements': input_counts,
              'overlapping_capture_cells':overlapping,
              'inactive_training_services': inactive, 'duplicate_landmarks': duplicates,
              'incompatible_capture_placements': incompatible,
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
    print(f'Unverified or incompatible capture placements withheld: {len(incompatible)}. Source captures are unchanged; see {output.with_suffix(".report.json")}')
    if overlapping: print(f'Overlapping capture observations withheld: {len(overlapping)}; one verified snapshot retained per cell.')
    if transient: print(f'Captured looted/dead objects withheld: {len(transient)}; exact original observation rows retained in source files.')
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
