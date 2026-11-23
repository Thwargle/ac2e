"""Real UDP Black Mother Earth path at prepared production placements.

No blossoms, garlands or trade rewards are seeded. A disposable, weakened Idol
awards the native 100-gold crafting budget; production encounters are untouched.
"""
import json
import math
import os
import subprocess
import time
import uuid
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer
from test_gameplay import until
from prepare_content import prepare

PORT = 19543
BUSH, SHRINE, BLOSSOM, GARLAND, SCENTED, RECIPE = (0x47002301, 0x4700230b, 0x470022f3,
                                                0x47002355, 0x470022f9, 0x6f001fe4)


def main():
    run = ROOT / 'Saved/AC2NetworkTests' / ('nature-gathering-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    catalog = json.loads((ROOT / 'AC2E/emulator/content/nature-exchanges-v1.json').read_text())
    prepare(DAT_DIRECTORY, run / 'prepared.tsv')
    production = [r.split('\t') for r in (run / 'prepared.tsv').read_text().splitlines() if r.startswith('8140')]
    bushes = [r for r in production if int(r[1], 16) == BUSH]
    assert len(bushes) >= 2
    definitions = [SHRINE, 0x470007d1, *[r['npc']['id'] for r in catalog['rules'][1:]]]
    rows = {d: next(r for r in production if int(r[1], 16) == d) for d in definitions}
    assert sum(int(r[1], 16) == SHRINE for r in production) == 1
    (run / 'world.tsv').write_text('pyrealium-world-v4\n' + '\n'.join('\t'.join(r) for r in [*bushes, *rows.values()]) + '\n')
    encounter_lines = (ROOT / 'AC2E/emulator/content/development-encounters.tsv').read_text().splitlines()
    (run / 'encounters.tsv').write_text('\n'.join(line for line in encounter_lines if not line.startswith('npc\t470007d1\t'))
                                     + '\nnpc\t470007d1\t1\t1\t1500\t3\t300000\t1\t100\n')
    server, peers, character = None, [], None

    def start(log):
        p = subprocess.Popen([str(SERVER), 'serve', '--bind', f'127.0.0.1:{PORT}', '--account', 'local',
                              '--data', str(run / 'world'), '--game-data', str(DAT_DIRECTORY),
                              '--content', str(run / 'world.tsv'), '--encounters', str(run / 'encounters.tsv'),
                              '--initialize-ui', 'true', '--portals', 'false',
                              '--lifestones', 'false', '--test-map-travel', 'true', '--seconds', '240'],
                             env=dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1'),
                             stdout=log, stderr=log, creationflags=0x08000000)
        time.sleep(1)
        assert p.poll() is None, (run / 'server.log').read_text()
        return p

    def stop():
        if server and server.poll() is None:
            (run / 'world/stop.request').write_text('stop\n'); server.wait(timeout=15)

    def login():
        nonlocal character
        p = Peer(); peers.append(p)
        p.send('connect', host='127.0.0.1', port=PORT, account='local',
               dat_directory=str(DAT_DIRECTORY)); p.wait('characters')
        if character is None:
            p.send('create', name='Blossom Collector', race=1, sex=4096, start_area=0, physical=[[1, 1], [2, 1]])
            character = p.wait('created')['character']['id']
        p.id = character
        p.send('enter', id=p.id); p.wait('player'); p.send('ready')
        p.wait('status', lambda e: e['phase'] == 'world'); until(p, 0, 'inventory')
        return p

    def leave(p):
        mark = len(p.events); p.send('logout', id=p.id); until(p, mark, 'selection'); p.close()

    def owned(p, definition):
        removed = {e['id'] for e in p.events if e['event'] == 'inventory_remove'}
        return [e for e in p.events if e['event'] == 'object' and e.get('entity') == definition
                and e.get('container') == p.id and e['id'] not in removed]

    def granted(p, definition, mark):
        return until(p, mark, 'object', lambda e: e.get('entity') == definition and e.get('container') == p.id)

    def at(p, row, offset=.5):
        pos = list(map(float, row[4:7])); pos[0] += offset; cell = int(row[3], 16)
        if cell & 65535 < 256:
            dx, dy = math.floor(pos[0] / 160), math.floor(pos[1] / 160)
            pos[0] -= 160 * dx; pos[1] -= 160 * dy
            cell = (((cell >> 24) + dx) << 24) | ((((cell >> 16) & 255) + dy) << 16) | (int(pos[0] / 20) * 8 + int(pos[1] / 20) + 1)
        p.send('move', cell=cell, position=pos, heading=0, motion=[0, 0, 0])
        until(p, 0, 'object', lambda e: e['id'] == str(int(row[0], 16))); time.sleep(.25)

    def use(p, item, target='0', delay=1.1):
        time.sleep(delay); mark = len(p.events); p.send('use', item=item, target=target); return mark

    def reject(p, mark, text):
        until(p, mark, 'chat', lambda e: text in str(e))
        assert not any(e['event'] in ('inventory_remove', 'inventory_add') for e in p.events[mark:])

    def combat(p, enabled):
        mark = len(p.events); p.send('combat', enabled=enabled)
        until(p, mark, 'combat', lambda e: e['enabled'] == enabled)

    def craft(p, anchor, success, ordinals=range(1, 8), spinner=0):
        time.sleep(1.1); mark = len(p.events)
        p.send('craft_execute', recipe=RECIPE, spinner=spinner, helper=p.id,
               ingredients=[dict(ordinal=i, item=anchor) for i in ordinals])
        assert (until(p, mark, 'craft_complete')['error'] == 0) == success
        return mark

    with (run / 'server.log').open('w') as log:
        try:
            server = start(log); p = login()
            assert RECIPE not in until(p, 0, 'craft_registry')['recipes']
            bush = str(int(bushes[0][0], 16)); shrine = str(int(rows[SHRINE][0], 16))
            at(p, bushes[0], 8); reject(p, use(p, bush), 'four metres')
            at(p, bushes[0]); combat(p, True); reject(p, use(p, bush), 'out of combat'); combat(p, False)
            mark = use(p, bush); blossom = granted(p, BLOSSOM, mark)
            assert blossom['quantity'] == 4
            reject(p, use(p, bush, delay=0), 'Wait briefly')
            reject(p, use(p, bush), 'regrowing')
            # Both recipe validation and execution require the shrine entitlement.
            mark = len(p.events); p.send('craft_validate', recipe=RECIPE, ordinal=1, item=blossom['id'], spinner=0)
            assert until(p, mark, 'craft_ingredient')['error'] != 0
            craft(p, blossom['id'], False)
            at(p, bushes[1]); mark = use(p, str(int(bushes[1][0], 16)))
            granted(p, BLOSSOM, mark); assert sum(i['quantity'] for i in owned(p, BLOSSOM)) == 8
            at(p, rows[SHRINE]); mark = use(p, shrine)
            until(p, mark, 'chat', lambda e: 'Collect eight' in str(e))
            assert not any(e['event'] == 'inventory_remove' for e in p.events[mark:])
            reject(p, use(p, '12345678', shrine), 'from your backpack')
            mark = use(p, blossom['id'], shrine)
            registry = until(p, mark, 'craft_registry', lambda e: RECIPE in e['recipes'])
            assert not registry['replace'] and len(registry['catalog'][0]['ingredients']) == 7
            until(p, mark, 'inventory_remove', lambda e: e['id'] == blossom['id'])
            assert sum(i['quantity'] for i in owned(p, BLOSSOM)) == 7
            anchor = owned(p, BLOSSOM)[0]['id']
            reject(p, use(p, anchor, shrine), 'already know')
            leave(p); stop(); server = start(log); p = login()
            assert RECIPE in until(p, 0, 'craft_registry')['recipes']
            at(p, bushes[0]); reject(p, use(p, bush), 'regrowing')
            anchor = owned(p, BLOSSOM)[0]['id']
            for ordinal in range(1, 8):
                mark = len(p.events); p.send('craft_validate', recipe=RECIPE, ordinal=ordinal, item=anchor, spinner=0)
                assert until(p, mark, 'craft_ingredient')['error'] == 0
            craft(p, anchor, False, ordinals=range(1, 7))
            craft(p, anchor, False, spinner=1)
            craft(p, anchor, False)  # Native 100-gold cost is not yet affordable.
            assert sum(i['quantity'] for i in owned(p, BLOSSOM)) == 7
            control = rows[0x470007d1]; at(p, control)
            cid = str(int(control[0], 16)); mark = len(p.events); p.send('attack', id=cid)
            until(p, mark, 'quality', lambda e: e.get('id') == cid and e.get('key') == 0x102 and e.get('value') == 0)
            combat(p, False)
            mark = craft(p, anchor, True); garland = granted(p, GARLAND, mark)
            assert not owned(p, BLOSSOM)
            craft(p, anchor, False)
            at(p, rows[SHRINE]); mark = use(p, garland['id'], shrine)
            item = granted(p, SCENTED, mark)
            reject(p, use(p, garland['id'], shrine), 'from your backpack')
            # The collected/crafted item now drives the existing three trades.
            for rule in catalog['rules'][1:]:
                npc = rows[rule['npc']['id']]; at(p, npc)
                mark = use(p, item['id'], str(int(npc[0], 16)))
                until(p, mark, 'inventory_remove', lambda e: e['id'] == item['id'])
                item = granted(p, rule['output'], mark)
                assert item['quantity'] == rule['quantity']
            gems = item
            leave(p); stop(); server = start(log); p = login()
            assert RECIPE in until(p, 0, 'craft_registry')['recipes']
            assert owned(p, 0x470022ec)[0]['id'] == gems['id']
            assert owned(p, 0x470022ec)[0]['quantity'] == 8
            for d in [BLOSSOM, GARLAND, SCENTED, 0x470022fd, 0x470022fc]: assert not owned(p, d)
            leave(p)
            report = dict(result='PASS', production_bushes=len(bushes), production_shrines=1,
                          checks=['world collection without seeded quest inputs', 'range, combat and throttle guards',
                                  'persistent per-bush regrowth and learned recipe', 'eight collected blossoms, one offered and seven crafted',
                                  'native seven slots accept shared stacks without double-spending', 'native 100-gold cost; insufficient funds preserve inputs',
                                  'malformed, unlearned, foreign and replayed requests rejected', 'crafted blossom exchanged through three NPCs into eight gems',
                                  'two restart/reconnect checks'],
                          limitation='Bush yield/regrowth are reconstructed; this is the Black Mother Earth material path, not full Nature\'s Ally.')
            (run / 'result.json').write_text(json.dumps(report, indent=2)); print(json.dumps(report)); print(run)
        finally:
            for i, p in enumerate(peers):
                (run / f'peer-{i}.json').write_text(json.dumps(p.events)); p.close()
            stop()


if __name__ == '__main__':
    main()
