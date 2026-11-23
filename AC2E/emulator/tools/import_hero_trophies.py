"""Translate guide-listed retail trophy slots from the 2017 DAT to installed Legions identities."""
import argparse
from collections import defaultdict
import json
from pathlib import Path

from lxml import html

from build_hero_atlas import dat_templates
from import_native_trophies import ContentProfile, Reader, entity, fnv, recover


def guide_carriers(page):
    result = defaultdict(set)
    for tr in html.parse(str(page)).xpath('//tr[td[1]//img[contains(@src, "items/trophy/")]]'):
        cells = [' '.join(td.text_content().split()) for td in tr.xpath('./td')]
        if len(cells) == 5:
            for name in cells[4].split(','):
                if name.strip():
                    result[name.strip()].add(cells[1])
    return result


def creature_key(row):
    # AI table names and stats changed between builds. The gameplay superclass,
    # wielded template, loot profile and level distinguish similarly named variants.
    props = row['props']
    return (row['name'], row['role'], row['base'], props.get('AI_AISuperClass'),
            props.get('GameplayStatistics_Level'), props.get('NPC_DeathLootProfile'),
            props.get('NPC_DeathRemoveLootProfile'), props.get('AI_Inventory1'))


def item_key(row):
    return row['name'], row['base'], tuple(row['signature'])


def sources(profile, did):
    result = {}
    while did >> 24 == 0x47:
        key = f'{did:08x}'
        if key in result:
            raise ValueError(f'cyclic inheritance at {key}')
        data = profile.portal.read(did)
        r = Reader(data)
        desc = entity(r, profile.types)
        if r.pos != len(data):
            raise ValueError(f'trailing data for {key}')
        result[key] = fnv(data)
        did = desc['data']
    return result


def translate(guide, reference, installed):
    carriers = guide_carriers(guide)
    trophies = set().union(*carriers.values())
    src, dst = dat_templates(reference), dat_templates(installed)
    native = {r['entity']: r for r in recover(reference)['rules']}
    creatures, items = defaultdict(list), defaultdict(list)
    for did, row in src.items():
        if row['role'] == 'monster' and row['name'] in carriers:
            creatures[creature_key(row)].append(did)
    for did, row in dst.items():
        if not row['ai'] and row['signature'] is not None:
            items[item_key(row)].append(did)
    result = []
    profile = ContentProfile(installed)
    try:
        for did, row in sorted(dst.items()):
            if row['role'] != 'monster' or row['name'] not in carriers:
                continue
            if any(k.startswith('NPC_TrophyDrop') for k in row['props']):
                continue  # Installed slots, including explicitly disabled slots, take precedence.
            matches = sorted(creatures[creature_key(row)])
            if not matches or any(match not in native for match in matches):
                continue
            slots = native[matches[0]]['drops']
            if any(native[match]['drops'] != slots for match in matches):
                continue  # Ambiguous same-name variants must agree on the entire native table.
            proof = sources(profile, did)
            drops = []
            for slot in slots:
                item = src.get(slot['definition'])
                if not item or item['name'] not in trophies or item['signature'] is None:
                    continue
                choices = items[item_key(item)]
                if len(choices) != 1:
                    continue
                dest = choices[0]
                qualities = 0x81000000 | (dest & 0xffffff)
                proof.update(sources(profile, dest))
                proof[f'{qualities:08x}'] = fnv(profile.portal.read(qualities))
                drops.append(dict(definition=dest, visual=dst[dest]['visual'],
                                  probability=slot['probability'], slot=slot['slot'],
                                  reference_definition=slot['definition'], name=item['name']))
            # Anchor each variant to a documented carrier-item pair. Include other
            # unambiguously identified trophy slots if its native table contains them.
            if any(drop['name'] in carriers[row['name']] for drop in drops):
                result.append(dict(entity=did, name=row['name'], sources=proof, drops=drops,
                                   reference_entities={f'{match:08x}': native[match]['sources'] for match in matches}))
    finally:
        profile.close()
    return dict(schema=1, guide='http://ac2hero.e5i5o.com/essence2.html',
                source='2017 NPC_TrophyDrop slots and paired rates. Definition IDs and visuals translated to installed Legions.',
                rules=result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('guide', 'reference', 'installed', 'output'):
        parser.add_argument(arg, type=Path)
    args = parser.parse_args()
    pack = translate(args.guide, args.reference, args.installed)
    args.output.write_text(json.dumps(pack, indent=2) + '\n', encoding='utf8')
    print(f"{len(pack['rules'])} creatures, {sum(len(r['drops']) for r in pack['rules'])} translated trophy slots")
