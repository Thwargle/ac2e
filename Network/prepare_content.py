"""Build a DAT-compatible subset of upstream placements without changing game assets."""
import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools/AC2'))
sys.path.insert(0, str(ROOT / 'AC2E/emulator/tools'))
from dat_probe import Archive
from content_profile import ContentProfile, nearby_landmark, landmark_position
from import_audited_population import localized_name
from population_probe import Reader, entity


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

def apply_placement_replacements(content, lines, input_rows, admitted_sources):
    """Retire audited observations only after their verified replacement passes.

    Capture IDs and subcells can change between snapshots. Do not infer unique
    NPCs from names, templates or proximity: repeated actors are valid. These
    authoring decisions pin both source rows and keep the raw packs intact.
    """
    path = content / 'placement-replacements.json'
    if not path.exists():
        return lines, [], []
    policy = json.loads(path.read_text(encoding='utf-8'))
    if policy.get('schema') != 1:
        raise ValueError('Unsupported placement replacement schema')
    rules = policy['replacements']
    retired = {rule['superseded']['id'] for rule in rules}
    retained = {rule['retained']['id'] for rule in rules}
    if len(retired) != len(rules) or retired & retained:
        raise ValueError('Duplicate or chained placement replacements')
    removed, pending = [], []
    for rule in rules:
        old, new = rule['superseded'], rule['retained']
        if (new['source'] != 'recovered-population.tsv' and (old['source'], new['source']) not in {
                ('inferred-world-v1.tsv', 'fiery-crypt-map-v1.tsv'),
                ('recovered-population.tsv', 'world-observed-expanded.tsv')}):
            raise ValueError('Placement replacement requires verified recovered population')
        rows = []
        for endpoint in (old, new):
            row = input_rows.get((endpoint['source'], endpoint['id']))
            if row is None or hashlib.sha256(row.encode()).hexdigest() != endpoint['row_sha256']:
                raise ValueError(f'Stale placement replacement evidence: {endpoint["id"]}')
            rows.append(row.split('\t'))
        if rule.get('retire_post_atl_variant'):
            # Retail captured both phases in this room. Keep the base NPC while
            # AC2E serves identical dialogue for the two native variants.
            profiles = {p['entity']: p for p in json.loads(
                (content / 'npc-dialogue-v1.json').read_text(encoding='utf-8'))['profiles']}
            prior, base = (profiles[int(row[1], 16)] for row in rows)
            if not (old['source'] == new['source'] == 'recovered-population.tsv'
                    and prior['native_table'] == base['native_table'] + 'PostATL'
                    and prior['topics'] == base['topics']
                    and prior['name'] == base['name'] == rule['name']
                    and rows[0][2:4] == rows[1][2:4]
                    and math.dist(list(map(float, rows[0][4:7])), list(map(float, rows[1][4:7]))) <= 2):
                raise ValueError('Placement replacement must preserve the co-located NPC phase and dialogue')
        elif rows[0][1:3] != rows[1][1:3]:
            raise ValueError('Placement replacement must preserve entity and visual identity')
        if admitted_sources.get(old['id']) != old['source']:
            continue
        entry = dict(rule)
        if admitted_sources.get(new['id']) != new['source']:
            entry['reason'] = 'Replacement was not admitted; original observation retained'
            pending.append(entry)
        else:
            removed.append(entry)
    removed_ids = {entry['superseded']['id'] for entry in removed}
    return [line for line in lines if line.split('\t', 1)[0] not in removed_ids], removed, pending

def sha256_any_eol(path):
    """A checked-in file's digest with LF or CRLF endings: `* text=auto` checks
    text out with CRLF on Windows and LF elsewhere, and evidence may come from either."""
    lf = path.read_bytes().replace(b'\r\n', b'\n')
    return {hashlib.sha256(lf).hexdigest(), hashlib.sha256(lf.replace(b'\n', b'\r\n')).hexdigest()}


def apply_clearance_repairs(content, lines):
    """Apply reviewed native-collision corrections, retaining original captures.

    Pin the entire admitted row. A changed capture must be reviewed again, and
    a repair cannot change identity, appearance, orientation or spawner slot.
    """
    lines, applied = _apply_clearance_policy(content, lines, 'placement-clearance-v1.json', 3.5)
    lines, reviewed = _apply_clearance_policy(content, lines, 'placement-reviewed-v1.json', 20.0)
    return lines, applied + reviewed


def _apply_clearance_policy(content, lines, filename, limit):
    path = content / filename
    if not path.exists():
        return lines, []
    policy = json.loads(path.read_text(encoding='utf8'))
    if policy.get('schema') != 1 or policy.get('native_ground_verified') is not True:
        raise ValueError('Placement clearance requires native verification')
    is_review = filename == 'placement-reviewed-v1.json'
    if is_review:
        initial = json.loads((content / 'placement-clearance-v1.json').read_text(encoding='utf8'))
        expected = {entry['id'] for entry in initial['unresolved']}
        decisions = policy['repairs'] + policy.get('verified', []) + policy['unresolved']
        if (policy.get('reviewed') != len(expected) or len(decisions) != len(expected)
            or {entry['id'] for entry in decisions} != expected):
            raise ValueError('Reviewed clearance must cover exactly the initially flagged anchors')
    rows = {row.split('\t', 1)[0]: row for row in lines[1:] if '\t' in row}
    if is_review:
        for retained in policy['unresolved'] + policy.get('verified', []):
            iid = retained['id']
            if retained.get('replacement') is not None or (iid in rows and rows[iid] != retained.get('original')):
                raise ValueError(f'Stale retained placement review: {iid}')
        for retained in policy.get('verified', []):
            proof = retained.get('native_support', {})
            fields = retained['original'].split('\t')
            displacement = proof.get('max_displacement_m', float('inf'))
            if (len(fields) != 13 or fields[0] != retained['id']
                or proof.get('accepted') is not True or proof.get('placement_result') != 'Ok'
                or proof.get('placement_flags') != 0 or proof.get('physics_quanta') != 100
                or proof.get('all_samples_supported') is not True
                or not math.isfinite(displacement) or not 0 <= displacement <= .05
                or proof.get('requested_cell') != fields[3]
                or len(proof.get('requested_position', [])) != 3
                or any(not math.isfinite(v) or abs(v-float(w)) > .0001
                       for v, w in zip(proof.get('requested_position', []), fields[4:7]))):
                raise ValueError(f'Invalid native sphere support review: {retained["id"]}')
    replacements = {}
    applied = []
    for repair in policy['repairs']:
        iid = repair['id']
        before, after = repair['original'].split('\t'), repair['replacement'].split('\t')
        if (iid in replacements or len(before) != 13 or len(after) != 13 or before[0] != iid
            or before[:3] != after[:3] or before[7:] != after[7:]):
            raise ValueError('Clearance repair must preserve actor identity and appearance')
        old_cell, new_cell = int(before[3], 16), int(after[3], 16)
        old, new = [tuple(float(v) for v in fields[4:7]) for fields in (before, after)]
        if not all(math.isfinite(v) for v in (*old, *new)):
            raise ValueError('Invalid clearance coordinates')
        _, old_world = landmark_position(old_cell, old)
        _, new_world = landmark_position(new_cell, new)
        if (not 1 <= old_cell & 65535 <= 64 or not 1 <= new_cell & 65535 <= 64
            or math.dist(old_world, new_world) > limit + .001):
            raise ValueError('Clearance repair exceeds bounded outdoor correction')
        if is_review and (old_cell >> 16 != new_cell >> 16 or repair.get('native_floor_route') is not True):
            raise ValueError('Reviewed clearance must retain a verified route on the same landblock')
        if iid in rows and rows[iid] != repair['original']:
            raise ValueError(f'Stale placement clearance evidence: {iid}')
        replacements[iid] = repair['replacement']
        if iid in rows:
            applied.append(repair)
    return [replacements.get(row.split('\t', 1)[0], row) for row in lines], applied


def apply_portal_names(directory, content, lines):
    catalog = content / 'portals.json'
    language = directory / 'local_English.dat'
    if not catalog.is_file() or not language.is_file():
        return 0
    portals = {p['id']: p for p in json.loads(catalog.read_bytes())['portals'] if p['status'] == 'placed'}
    local = Archive(language)
    count = 0
    try:
        for index, line in enumerate(lines):
            row = line.split('\t')
            portal = portals.get(row[0])
            if not portal or len(row) != 13 or row[-1] != '-':
                continue
            if (row[1], row[3]) != (portal['entity'], portal['cell']):
                continue
            if not math.dist(tuple(map(float, row[4:7])), portal['position']) <= .001:
                continue
            table, key = portal['name']
            name = localized_name(local, {'table': table, 'key': key})
            if name != '-':
                row[-1] = name
                lines[index] = '\t'.join(row)
                count += 1
    finally:
        local.file.close()
    return count


def apply_recovered_actor_names(profile, content, lines):
    path = content / 'recovered-actor-names.tsv'
    if not path.is_file():
        return 0
    header, *records = path.read_text(encoding='utf8').splitlines()
    if header != 'ac2e-recovered-actor-names-v1':
        raise ValueError('Unsupported recovered actor names format')
    names = {}
    for record in records:
        if not record or record.startswith('#'):
            continue
        template, visual, ai, name = record.split('\t')
        did = int(template, 16)
        # Numeric template IDs can identify different actors across retail builds.
        model = int(visual, 16)
        if profile.visual(did) != model:
            continue
        desc = entity(Reader(profile.portal.read(did)), profile.types)
        if desc.get('properties', {}).get('40000019:41000189') == ai:
            names[did, model] = name

    count = 0
    for index, line in enumerate(lines):
        row = line.split('\t')
        if len(row) != 13 or row[-1] != '-':
            continue
        name = names.get((int(row[1], 16), int(row[2], 16)))
        if name:
            row[-1] = name
            lines[index] = '\t'.join(row)
            count += 1
    return count


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
    starters = content / 'starter-world-v1.tsv'
    if starters.exists():
        evidence = json.loads(starters.with_suffix('.report.json').read_text(encoding='utf8'))
        if (evidence['manifest_sha256'] not in sha256_any_eol(starters)
            or evidence['source_sha256'] not in sha256_any_eol(content/'starter-areas-v1.json')
            or evidence['routes_sha256'] not in sha256_any_eol(content/'starter-routes-v1.tsv')):
            raise ValueError('Starter catalog changed; regenerate and run AC2.Data.StarterClearance')
        sources.append(starters.name)
    recovered_cells = set()
    if recovered.exists():
        evidence = json.loads(recovered.with_suffix('.report.json').read_text(encoding='utf-8'))
        if not evidence.get('native_ground_verified') or evidence.get('manifest_sha256') not in sha256_any_eol(recovered):
            raise ValueError('Recovered population requires current native ground verification')
        sources.append(recovered.name)
        version = 4
        lines[0] = 'pyrealium-world-v4'
    sources.append('capture-spawns.tsv')
    if newer.exists(): sources.append(newer.name)
    if audited.exists(): sources.append(audited.name)
    inferred = content / 'inferred-world-v1.tsv'
    regional = content / 'regional-world-v1.tsv'
    wilderness = content / 'wilderness-world-v1.tsv'
    exploration = content / 'exploration-world-v1.tsv'
    hunting = content / 'hunting-world-v1.tsv'
    hunting_expansion = content / 'hunting-world-v2.tsv'
    foundry = content / 'foundry-generals-v1.tsv'
    workshops = content / 'workshops-world-v1.tsv'
    mines = content / 'arwic-mines-world-v1.tsv'
    vaults = content / 'vault-world-v1.tsv'
    hero_camps = content / 'hero-wilderness-camps-v2.tsv'
    windmill = content / 'windmill-world-v1.tsv'
    npcs = content / 'npc-world-v1.tsv'
    verified_packs = set()
    for inferred_pack, catalog in [(inferred, 'inferred-content-v1.json'),
                                   (regional, 'regional-content-v1.json'),
                                   (wilderness, 'wilderness-content-v1.json'),
                                   (exploration, 'exploration-content-v1.json'),
                                   # Individual guide coordinates for Furakhe and Shurakhe
                                   # supersede the town offsets in the original hunt packs.
                                   (content/'quest-encounters-v1.tsv', 'quest-encounters-v1.json'),
                                   (hunting, 'hunting-content-v1.json'),
                                   (hunting_expansion, 'hunting-content-v2.json'),
                                   (foundry, 'foundry-generals-v1.json'),
                                   (workshops, 'workshops-content-v1.json'),
                                   (mines, 'arwic-mines-world-v1.json'),
                                   (vaults, 'vault-world-v1.json'),
                                   (content/'illuminated-cave-v1.tsv', 'illuminated-cave-v1.json'),
                                   (content/'warder-hideout-v1.tsv', 'warder-hideout-v1.json'),
                                   (content/'cragstone-excavation-v1.tsv', 'cragstone-excavation-v1.json'),
                                   (content/'the-ossuary-v1.tsv', 'the-ossuary-v1.json'),
                                   (content/'crystal-caves-v1.tsv', 'crystal-caves-v1.json'),
                                   (content/'golden-age-reliquary-v1.tsv', 'golden-age-reliquary-v1.json'),
                                   (content/'drudgerton-referral-v1.tsv', 'drudgerton-referral-v1.json'),
                                   (content/'ac2hero-raid-world-v1.tsv', 'ac2hero-hunts-v2.json'),
                                   (content/'fiery-crypt-map-v1.tsv', 'fiery-crypt-map-v1.json'),
                                   (hero_camps, 'hero-wilderness-camps-v2.json'),
                                   (windmill, 'windmill-v1.json'),
                                   (content/'ac2hero-settlements-v1.tsv', 'ac2hero-settlements-v1.json'),
                                   (npcs, 'npc-dialogue-v1.json')]:
        if not inferred_pack.exists():
            continue
        evidence = json.loads(inferred_pack.with_suffix('.report.json').read_text(encoding='utf8'))
        if (not evidence.get('native_ground_verified')
            or evidence['manifest_sha256'] not in sha256_any_eol(inferred_pack)
            or evidence['source_sha256'] not in sha256_any_eol(content/catalog)):
            raise ValueError(f'{inferred_pack.name} requires fresh native ground validation against {catalog}')
        verified_packs.add(inferred_pack.name)
        sources.append(inferred_pack.name)
        version = 4
        lines[0] = 'pyrealium-world-v4'
    state_file=content / 'capture-spawns-audited.report.json'
    state_exclusions={}
    if audited.exists() and state_file.exists():
        evidence=json.loads(state_file.read_text(encoding='utf-8'))
        state_exclusions={e['id']:e for e in evidence['excluded_existing']}
    transient=[]
    input_seen = set()
    input_counts = {}
    input_rows = {}
    admitted_sources = {}
    earlier_capture_cells = set()
    overlapping = []
    capture_line_indices = {}
    for name in sources:
        source_cells=set()
        for line in (content / name).read_text().splitlines()[1:]:
            if not line or line.startswith('#'):
                continue
            fields = line.split('\t')
            if len(fields) not in (12, 13): raise ValueError(f'Unsupported placement in {name}')
            iid, entity, visual, cell = [int(v, 16) for v in fields[:4]]
            input_rows[name, fields[0]] = line
            # A later capture cannot replace an earlier placement identity.
            # Incompatible older rows are also retained in the audit exactly once.
            if iid in input_seen:
                # A named capture can identify an already-admitted generic portal.
                # Keep its original placement and require matching entity, visual,
                # cell, scale and frame before filling its empty name.
                previous = capture_line_indices.get(iid)
                if previous is not None and name in verified_packs and len(fields) == 13 and fields[-1] != '-':
                    prior = lines[previous].split('\t')
                    if (prior[-1] == '-' and prior[1:4] == fields[1:4] and prior[11] == fields[11]
                            and all(abs(float(a) - float(b)) < .0001 for a, b in zip(prior[4:11], fields[4:11]))):
                        prior[-1] = fields[-1]
                        lines[previous] = '\t'.join(prior)
                continue
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
                if name.startswith('capture-spawns') or name in ('ac2hero-raid-world-v1.tsv', 'ac2hero-settlements-v1.tsv', 'fiery-crypt-map-v1.tsv', 'warder-hideout-v1.tsv', 'cragstone-excavation-v1.tsv', 'the-ossuary-v1.tsv', 'crystal-caves-v1.tsv', 'golden-age-reliquary-v1.tsv', 'quest-encounters-v1.tsv', 'drudgerton-referral-v1.tsv', hero_camps.name, recovered.name, inferred.name, regional.name, wilderness.name, exploration.name, hunting.name, hunting_expansion.name, foundry.name, workshops.name, vaults.name, npcs.name):
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
                if name.startswith('capture-spawns') and len(fields) == 13 and fields[-1] == '-' and entity == 0x47001451:
                    capture_line_indices[iid] = len(lines)
                lines.append('\t'.join(fields))
                seen.add(iid)
                admitted_sources[fields[0]] = name
                if name == recovered.name: recovered_cells.add(cell)
                if comparison is not None:
                    absent = missing_records(fields, comparison)
                    if absent:
                        candidate_regressions.append({'id': fields[0], 'source': name, 'missing_records': absent})
        earlier_capture_cells.update(source_cells)
    try:
        lines, superseded, pending_replacements = apply_placement_replacements(
            content, lines, input_rows, admitted_sources)
        lines, clearance = apply_clearance_repairs(content, lines)
        actor_names = apply_recovered_actor_names(profile, content, lines)
    finally:
        profile.close()
    retired_ids = {entry['superseded']['id'] for entry in superseded}
    seen.difference_update(int(iid, 16) for iid in retired_ids)
    candidate_regressions = [entry for entry in candidate_regressions if entry['id'] not in retired_ids]
    portal_names = apply_portal_names(directory, content, lines)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    report = {'included': len(seen), 'excluded': len(skipped)+len(incompatible)+len(duplicates)+len(overlapping)+len(transient)+len(superseded), 'missing_assets': skipped,
              'superseded_placements': superseded, 'unapplied_placement_replacements': pending_replacements,
              'clearance_repairs': clearance, 'localized_portal_names': portal_names,
              'recovered_actor_names': actor_names,
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
    if superseded: print(f'Superseded duplicate placements withheld: {len(superseded)}; verified replacements retained.')
    if clearance: print(f'Native body-clearance placement repairs applied: {len(clearance)}; original capture rows retained.')
    if pending_replacements: print(f'Placement replacements unavailable: {len(pending_replacements)}; original observations retained.')
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
