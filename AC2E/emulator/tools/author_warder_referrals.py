"""Pin the first three Warder introductions to native text and cached guide evidence."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CONTENT = ROOT / 'AC2E/emulator/content'

def build():
    texts = json.loads((CONTENT/'recovered-quest-text.json').read_text(encoding='utf8'))['candidates']
    wiki = {a['title']: a for a in json.loads((ROOT/'Saved/ContentRecovery/wiki-content.json').read_text(encoding='utf8'))['articles']}
    native = json.loads((ROOT/'Saved/current-native-templates.json').read_text(encoding='utf8'))
    quests = []
    for qid, title, giver, recipient, level, limit, xp, direction in [
        (0x400000cf, 'Find a Warder in Cragstone', 0x470015c2, 0x470015c0, 3, 0, 0,
         'Speak to the Warder of Cragstone at 32.8N, 12.7W. The gateway to Cragstone requires level 6.'),
        (0x400000d1, 'Find the Warder Corporal', 0x470015c0, 0x470015c6, 1, 0, 0,
         'Speak to the Warder Corporal near Rithwic at 23.9N, 4.3E. The Rithwic gateway is at 32.0N, 12.4W in Cragstone.'),
        (0x400000cc, 'Find the Warder Scout', 0x470015c6, 0x470015cc, 1, 172800, 30,
         'Speak to the Warder Scout near Molwirth at 14.0N, 0.6E within 48 hours.')]:
        fields = [t for t in texts if t['quest'] == f'{qid:08x}']
        assert fields and any(t['field']=='name' for t in fields)
        page = wiki[title]
        quests.append(dict(id=qid, title=title, giver=giver, recipient=recipient,
            giver_name=native[str(giver)]['name'], recipient_name=native[str(recipient)]['name'],
            level=level, limit=limit, repeat=72000, xp=xp, direction=direction,
            prerequisite='esper_camp' if qid==0x400000cf else None,
            native_fields=fields, source=dict(url=page['url'], html_sha256=page['html_sha256'], facts=page['facts'])))
    data = dict(version=1, policy='Native quest and NPC identities; reconstructed conversation transitions from archived guide evidence. Cragstone requires completed Drudge Outcast Camp; the Corporal and Scout referrals can begin independently. Blank guide deadlines are treated as unlimited. Completion and the next eligible referral happen in the same conversation. Rewards are paid once per accepted run. This profile covers the first three introductions. Notebook and Captain continuation use warder-notebook-v1; Dantry remains excluded because Potion Raid is not implemented.', quests=quests)
    (CONTENT/'warder-referrals-v1.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf8',newline='\n')

if __name__=='__main__': build()
