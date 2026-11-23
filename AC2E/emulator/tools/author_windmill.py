"""Pin the starter windmill route to DAT items, recovered journal and wiki evidence."""
import hashlib
import json
import struct
from pathlib import Path
from import_audited_population import ContentProfile
from content_profile import door_physical

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
            physical = door_physical(raw)
            visual = profile.visual(did)
            assert physical and visual
            return dict(id=did, name=name or native[str(did)]['name'], visual=visual,
                        physical=physical, quality_hash=f'{h:016x}')
        titles = ['Kehan Windmill Quest','Find the Miller of Kehan','Find the Esper Reaches Warder']
        data = dict(version=1, policy='Reconstructed server transitions. Native item identities, map destination and journal text; guide timers and overview XP. The windmill guide transcript says 150 XP but its overview says 200; use 200. Referral sources disagree on 0 vs 30 XP; use Find the Miller of Kehan overview (30). Renewed Focus grants maximum vigor for 10800 seconds. The Legions 6f00022a and 7230 6f001b1f WStates add 40 (stat 0x40108, field 36 magnitude). The guides give +10. The repaired windmill animation remains unfinished.',
                    sources=[dict(title=t,url=wiki[t]['url'],html_sha256=wiki[t]['html_sha256']) for t in titles],
                    native_fields=[t for t in texts if int(t['quest'],16) in (0x400000c2,0x400000c7,0x400000cb)],
                    items=[asset(0x47002fa8),asset(0x470015b0),asset(0x470010a0,'Lost Wish Glyph')],
                    map_location=list(struct.unpack('<I7f',bytes.fromhex(native[str(0x47002fa8)]['props']['Usage_RequiredLocation'])))[:4],
                    map_radius=struct.unpack('<f',struct.pack('<I',native[str(0x47002fa8)]['props']['Usage_RequiredLocationRadius']))[0],
                    npcs=[dict(id=0x8140d500470015ce,entity=0x470015ce,land=[1020+11.5*10.2,1020+47*10.2],evidence='Guide windmill coordinate 47.0N 11.5E; interactable gear receiver (GearWindmillKehan), within 32m ground search. Placement reconstructed, not a capture.')],
                    camps=[],templates={str(0x470015ce):asset(0x470015ce,'Windmill')})
        (CONTENT/'windmill-v1.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf8',newline='\n')
    finally: profile.close()

if __name__=='__main__': build()
