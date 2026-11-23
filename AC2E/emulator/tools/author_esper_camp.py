"""Pin the Esper camp quest to recovered DAT assets, captures and guide evidence."""
import hashlib
import json
from pathlib import Path
from import_audited_population import ContentProfile
from content_profile import door_physical
from craft_catalog import graph

ROOT = Path(__file__).resolve().parents[3]
CONTENT = ROOT / 'AC2E/emulator/content'

def build():
    native = json.loads((ROOT/'Saved/current-native-templates.json').read_text(encoding='utf8'))
    texts = json.loads((CONTENT/'recovered-quest-text.json').read_text(encoding='utf8'))['candidates']
    wiki = {a['title']:a for a in json.loads((ROOT/'Saved/ContentRecovery/wiki-content.json').read_text(encoding='utf8'))['articles']}
    profile = ContentProfile(ROOT/"Asheron's Call 2 - Legions")
    try:
        def asset(did, name=None):
            raw = profile.portal.read(0x81000000 | (did & 0xffffff))
            h = 0xcbf29ce484222325
            for b in raw: h = ((h ^ b)*0x100000001b3)&0xffffffffffffffff
            return dict(id=did, name=name or native[str(did)]['name'], visual=profile.visual(did),
                        physical=door_physical(raw), quality_hash=f'{h:016x}')
        fields = [t for t in texts if t['quest']=='400000d0']
        phases = [' '.join(t['text_raw'].replace('\\q','"').split()) for t in fields if t['field'].startswith('phases/')]
        placements = [l.split('\t') for l in (CONTENT/'recovered-population.tsv').read_text(encoding='utf8').splitlines() if '\t470015bd\t' in l]
        assert len(placements)==3
        data = dict(version=1, title='Drudge Outcast Camp Quest', phases=phases,
                    policy='Reconstructed server transitions. Native recipes cost 25 Bone, native weapons have 22 damage and require level 6. No free materials: the guide directs players to butcher vermin. All six recovered recipes are exposed while active; original per-race three-recipe assignment is unrecovered. Weapon equip race/level restrictions are enforced. Cadre Slayer multiplies direct weapon-bearing hits by three before armor, never periodic ticks. Captured Cadres retain native level 10 (guide says 8), 285 health and their existing independent respawning placements. Quest is 24h, reward 320 XP plus Esper Glyph, repeat 20h. Sound Heart effect graph is retained below (10800 duration table, stat 0x40107, final value table 40). Timed maximum-health modifiers are not implemented; no substitute buff is awarded.',
                    sources=[dict(title=t,url=wiki[t]['url'],html_sha256=wiki[t]['html_sha256']) for t in ['Drudge Outcast Camp Quest','Esper Warder','Drudge Outcast Cadre','Esper Glyph']],
                    native_fields=fields, placements=placements,
                    effects={f'{d:08x}':dict(sha256=hashlib.sha256(profile.portal.read(d)).hexdigest(),graph=graph(profile.portal.read(d),d)) for d in (0x70001e33,0x70001e34)},
                    items=[asset(d) for d in [0x47001400,*range(0x470015b2,0x470015b9)]]+[asset(0x470010a1,'Esper Glyph')])
        (CONTENT/'esper-camp-v1.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf8',newline='\n')
    finally: profile.close()

if __name__=='__main__': build()
