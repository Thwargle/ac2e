"""Pin Nature's Ally exchanges to the retained guide and installed native assets.

Trade amounts come from the guide; item appearance/stack limits come from DAT
recovery. Dialogue is an authored instruction, not a recovered retail quote.
"""
import argparse
import hashlib
import json
from pathlib import Path
from import_audited_population import ContentProfile

RULES = [(0x47002308, 0x470022f8, 0x470022f4, 1),
         (0x47002309, 0x470022f9, 0x470022fd, 1),
         (0x470022fe, 0x470022fd, 0x470022fc, 1),
         (0x47002305, 0x470022fc, 0x470022ec, 8)]


def recover(directory, templates, wiki, destination):
    native = json.loads(templates.read_text(encoding='utf-8'))
    article = next(a for a in json.loads(wiki.read_text(encoding='utf-8'))['articles']
                   if a['title'] == "Nature's Ally")
    profile = ContentProfile(directory)
    try:
        def asset(did):
            row = native[str(did)]
            raw = profile.portal.read(0x81000000 | (did & 0xffffff))
            value = 0xcbf29ce484222325
            for byte in raw:
                value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
            for ref in (did, row['visual'], row['props']['PhysObj']):
                profile.portal.read(ref)
            return dict(id=did, name=row['name'], visual=row['visual'],
                        physical=row['props']['PhysObj'], quality_hash=f'{value:016x}')
        items = []
        for did in sorted({i for _, a, b, _ in RULES for i in (a, b)} | {0x470022f3, 0x47002355}):
            row = asset(did)
            props = native[str(did)]['props']
            row['stack_limit'] = props.get('Item_MaxStackSize', 1) if props.get('Item_IsStackable') else 1
            items.append(row)
        rules = [dict(npc=asset(npc), input=a, output=b, quantity=q) for npc, a, b, q in RULES]
        data = dict(version=1, source=dict(url="https://20oz.net/index.php/Nature's_Ally.html",
                    archived_url=article['url'], html_sha256=article['html_sha256'],
                    template_export_sha256=hashlib.sha256(templates.read_bytes()).hexdigest()),
                    policy='Four material trades plus the Black Mother Earth blossom/recipe path; no full quest completion, XP or final reward. Guidance is authored, not verbatim retail dialogue. Bush yield (four) and per-character regrowth (five minutes) are reconstructed; the guide specifies eight blossoms total, one offered to learn and seven crafted.',
                    items=items, rules=rules,
                    gathering=dict(bush=asset(0x47002301), shrine=asset(0x4700230b),
                        recipe=0x6f001fe4, recipe_sha256=hashlib.sha256(profile.portal.read(0x6f001fe4)).hexdigest(),
                        yield_quantity=4, regrowth_seconds=300))
        destination.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8', newline='\n')
    finally:
        profile.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('directory', 'templates', 'wiki', 'destination'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    recover(args.directory, args.templates, args.wiki, args.destination)
