"""Recover native cast/release cues and missile entities from skill hook graphs.

The existing effects importer retained only hook value 16 (target FX). Hooks
also carry attacker FX (value 19, flag 0x40), release FX (21, flag 0x2000),
and the missile entity (7). Values are one-based, as in SkillCollection.cs.
Keep this cosmetic data separate from skill eligibility and damage formulas.
Multiple distinct missile/cue values are rejected rather than guessed.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from ac2_dat import Dat
from import_skill_effects import objects, struct_fields, list_items, NODE


def presentation(data):
    values, graph = objects(data)
    fields = struct_fields(values, graph[0][1])
    if len(fields) <= 65:
        return None  # Passives do not have the active-skill hook list.
    index = fields[65]
    if index >= len(graph) or graph[index][0] != NODE:
        return None
    cast, release, missiles = set(), set(), set()
    for index in list_items(values, graph[index][1]):
        if index >= len(graph) or graph[index][0] != NODE:
            raise ValueError('invalid skill hook reference')
        hook = [0] + list_items(values, graph[index][1])
        if len(hook) < 32:
            raise ValueError('truncated skill hook')
        for mask, slot, output in ((0x40, 19, cast), (0x2000, 21, release)):
            fx = hook[slot]
            if hook[20] & mask and fx not in (0, 0xffffffff):
                if fx >> 24 not in (0x40, 0x41, 0x81):
                    raise ValueError('invalid FX identity')
                output.add(fx)
        if hook[7] >> 24 == 0x47:
            missiles.add(hook[7])
    if any(len(s) > 1 for s in (cast, release, missiles)):
        raise ValueError('multiple distinct presentation cues')
    return dict(cast_fx=next(iter(cast), 0), release_fx=next(iter(release), 0),
                missile_entity=next(iter(missiles), 0))


def missile_visual(data):
    # EntityDesc prefix: flags, optional identity/type/instance, data DID.
    words = struct.unpack_from(f'<{len(data)//4}I', data)
    bits, offset = words[0], 1
    if bits & ~0x1ff or not bits & 8:
        raise ValueError('missile has no native visual')
    offset += bool(bits & 1) + bool(bits & 2) + 2 * bool(bits & 4)
    visual = words[offset]
    if visual >> 24 != 0x1f:
        raise ValueError('missile visual is not a visual descriptor')
    return visual


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game-data', type=Path, required=True)
    p.add_argument('--catalog', type=Path, default=Path(__file__).resolve().parents[1]/'content/skills.tsv')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    skills, hashes, rejected = {}, {}, []
    with Dat(a.game_data/'portal.dat') as dat:
        dat.index()
        for line in a.catalog.read_text(encoding='utf8').splitlines():
            if not line or line.startswith(('#', 'pyrealium')):
                continue
            row = line.split('\t')
            record = int(row[1], 16)
            raw = dat.read(record)
            digest = hashlib.sha256(raw).hexdigest()
            if digest != row[2]:
                raise ValueError(f'Catalog/DAT mismatch: {row[1]}')
            try:
                data = presentation(raw)
                if not data:
                    continue
                entity = data['missile_entity']
                data['missile_visual'] = 0
                if entity:
                    entity_data = dat.read(entity)
                    data['missile_visual'] = missile_visual(entity_data)
                    dat.read(data['missile_visual'])  # Require the visual to exist.
                    hashes[f'{entity:08x}'] = hashlib.sha256(entity_data).hexdigest()
                hashes[row[1]] = digest
                if any(data.values()):
                    skills[row[0]] = data
            except (ValueError, IndexError, KeyError, struct.error) as error:
                rejected.append(dict(skill=row[0], record=row[1], reason=str(error)))
    result = dict(version=1, skills=skills, record_sha256=dict(sorted(hashes.items())), rejected=rejected)
    a.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf8', newline='\n')
    print(f'{len(skills)} presentation profiles; {len(rejected)} rejected')


if __name__ == '__main__':
    main()
