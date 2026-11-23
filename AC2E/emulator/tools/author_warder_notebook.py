"""Pin Notebook assets, native stages and archived guide facts without moving captures."""
import hashlib
import json
import struct
from pathlib import Path
from import_audited_population import ContentProfile, Archive, localized_name
from content_profile import door_physical

ROOT = Path(__file__).resolve().parents[3]
CONTENT = ROOT / 'AC2E/emulator/content'

def build():
    native = json.loads((ROOT/'Saved/current-native-templates.json').read_text('utf8'))
    wiki = {a['title']: a for a in json.loads((ROOT/'Saved/ContentRecovery/wiki-content.json').read_text('utf8'))['articles']}
    texts = json.loads((CONTENT/'recovered-quest-text.json').read_text('utf8'))['candidates']
    profile = ContentProfile(ROOT/"Asheron's Call 2 - Legions")
    local = Archive(ROOT/"Asheron's Call 2 - Legions/local_English.dat")
    try:
        items = []
        for did, name in [(0x470015b1,"Warder Scout's Notebook"), (0x4700156d,'Emblem of Order'),
                          (0x470010b9,'Mad Crone Glyph'), (0x470010a2,'Prosper Glyph')]:
            raw = profile.portal.read(0x81000000 | did & 0xffffff)
            h = 0xcbf29ce484222325
            for b in raw: h = ((h ^ b)*0x100000001b3)&0xffffffffffffffff
            at = raw.find(struct.pack('<I',0xa8607cec))
            table = struct.unpack_from('<I',raw,at+4)[0]
            assert localized_name(local,dict(key=0xa8607cec,table=table)) == name
            items.append(dict(id=did,name=name,visual=profile.visual(did),physical=door_physical(raw),
                              quality_hash=f'{h:016x}',name_table=f'{table:08x}'))
        placements = [l for l in (CONTENT/'recovered-population.tsv').read_text('utf8').splitlines()
                      if any(f'\t{did:08x}\t' in l for did in (0x470015cc,0x470015be,0x470015c8))]
        assert len(placements)==3
        data = dict(version=1, policy='Reconstructed server transitions, native quest/item IDs and kill/acquire stages. Level 10+, 24h deadline, 800 XP and three named rewards, 20h repeat. Completion starts Seek the Warder Captain (48h, 2500 XP). Preserve verified translated placements: Scout 14.0N 0.6E and Bandit 15.5N 1.7W. Older guide/journal coordinates (Scout 25.0N 3.8E, camp 28.1N 3.9E) conflict with these captures; live guidance uses retained positions. No fellowship-shared credit or Drudge Potion Raid is claimed. Respawn cadence remains authored, not recovered.',
                    sources=[dict(title=t,url=wiki[t]['url'],html_sha256=wiki[t]['html_sha256'],facts=wiki[t]['facts'])
                             for t in ["The Warder Scout's Notebook",'Seek the Warder Captain','Drudge Slinker Bandit']],
                    fields=[t for t in texts if t['quest'] in ('400000c4','400000c5')],items=items,
                    placements=[dict(row=l,sha256=hashlib.sha256(l.encode()).hexdigest()) for l in placements],
                    native_bandit=native[str(0x470015be)],native_notebook=native[str(0x470015b1)])
        (CONTENT/'warder-notebook-v1.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf8',newline='\n')
    finally:
        profile.close();local.file.close()

if __name__=='__main__': build()
