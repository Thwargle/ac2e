"""Real UDP exchanges at production NPC placements, in a disposable store.

Inputs are seeded offline to isolate the four trades. The separate
test_nature_gathering.py covers world collection, learning and the shrine chain.
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

PORT = 19542


def main():
    run = ROOT / 'Saved/AC2NetworkTests' / ('nature-exchanges-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    catalog = json.loads((ROOT / 'AC2E/emulator/content/nature-exchanges-v1.json').read_text())
    definitions = {d['id']: d for d in catalog['items']}
    prepare(DAT_DIRECTORY, run / 'prepared.tsv')
    production = [r.split('\t') for r in (run / 'prepared.tsv').read_text().splitlines() if r.startswith('8140')]
    rows = {}
    for rule in catalog['rules']:
        matches = [r for r in production if int(r[1], 16) == rule['npc']['id']]
        assert len(matches) == 1, (rule['npc']['name'], len(matches))
        rows[rule['npc']['id']] = matches[0]
    control = next(r for r in production if r[1] == '470007d1')
    (run / 'world.tsv').write_text('pyrealium-world-v4\n' + '\n'.join('\t'.join(r) for r in [*rows.values(), control]) + '\n')

    def cli(*args):
        return subprocess.run([str(SERVER), *args, '--account', 'local', '--data', str(run / 'world')],
                              check=True, capture_output=True, text=True).stdout

    cli('character-create-human', '--name', 'Nature Trader')
    cli('character-create-human', '--name', 'Other Trader')
    for owner, definition, quantity, limit in [('1', 0x470022f8, 2, 10), ('1', 0x470022f9, 1, 1),
                                              ('2', 0x470022f8, 1, 10), ('1', 0x470022f8, 1, 1)]:
        d = definitions[definition]
        cli('inventory-grant', '--character-id', owner, '--game-data', str(DAT_DIRECTORY),
            '--id', f'{definition:08x}', '--visual-did', f"{d['visual']:08x}",
            '--quantity', str(quantity), '--stack-limit', str(limit))
    foreign = str(int(cli('inventory-list', '--character-id', '2').split()[0], 16))
    server, peers = None, []

    def start(log):
        p = subprocess.Popen([str(SERVER), 'serve', '--bind', f'127.0.0.1:{PORT}', '--account', 'local',
                              '--data', str(run / 'world'), '--game-data', str(DAT_DIRECTORY),
                              '--content', str(run / 'world.tsv'), '--initialize-ui', 'true',
                              '--portals', 'false', '--lifestones', 'false',
                              '--test-map-travel', 'true', '--seconds', '240'],
                             env=dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1'),
                             stdout=log, stderr=log, creationflags=0x08000000)
        time.sleep(1)
        assert p.poll() is None, (run / 'server.log').read_text()
        return p

    def stop():
        if server and server.poll() is None:
            (run / 'world/stop.request').write_text('stop\n')
            server.wait(timeout=15)

    def login():
        p = Peer(); p.id = '1'; peers.append(p)
        p.send('connect', host='127.0.0.1', port=PORT, account='local'); p.wait('characters')
        p.send('enter', id=p.id); p.wait('player'); p.send('ready')
        p.wait('status', lambda e: e['phase'] == 'world'); until(p, 0, 'inventory')
        return p

    def leave(p):
        mark = len(p.events); p.send('logout', id=p.id); until(p, mark, 'selection'); p.close()

    def owned(p, definition, mark=0):
        return until(p, mark, 'object', lambda e: e.get('entity') == definition and e.get('container') == p.id)

    def at(p, npc, offset=.5):
        row = rows[npc]; pos = list(map(float, row[4:7])); pos[0] += offset; cell = int(row[3], 16)
        dx, dy = math.floor(pos[0] / 160), math.floor(pos[1] / 160)
        pos[0] -= 160 * dx; pos[1] -= 160 * dy
        cell = (((cell >> 24) + dx) << 24) | ((((cell >> 16) & 255) + dy) << 16) | (int(pos[0] / 20) * 8 + int(pos[1] / 20) + 1)
        p.send('move', cell=cell, position=pos, heading=0, motion=[0, 0, 0])
        until(p, 0, 'object', lambda e: e['id'] == str(int(row[0], 16))); time.sleep(.25)

    def use(p, item, npc=None, delay=1.1):
        time.sleep(delay); mark = len(p.events)
        p.send('use', item=item, target=str(int(rows[npc][0], 16)) if npc else '0')
        return mark

    def reject(p, mark, text):
        until(p, mark, 'chat', lambda e: text in str(e))
        assert not any(e['event'] in ('inventory_remove', 'inventory_add') for e in p.events[mark:])

    with (run / 'server.log').open('w') as log:
        try:
            server = start(log); p = login()
            meat = owned(p, 0x470022f8); blossom = owned(p, 0x470022f9)
            old_meat = until(p, 0, 'object', lambda e:e.get('entity') == 0x470022f8 and e.get('container') == p.id and e.get('quantity') == 1)
            at(p, 0x47002308, 8)
            reject(p, use(p, meat['id'], 0x47002308), 'four metres')
            at(p, 0x47002308)
            mark = use(p, str(int(rows[0x47002308][0], 16)))
            until(p, mark, 'chat', lambda e: 'Bring one Rabbit Meat' in str(e))
            assert not any(e['event'] in ('inventory_remove', 'inventory_add', 'quests') for e in p.events[mark:])
            reject(p, use(p, meat['id'], 0x47002308, 0), 'Wait briefly')
            reject(p, use(p, blossom['id'], 0x47002308), 'Bring one Rabbit Meat')
            reject(p, use(p, foreign, 0x47002308), 'not in your backpack')
            mark = len(p.events); p.send('combat', enabled=True); until(p, mark, 'combat', lambda e:e['enabled'])
            reject(p, use(p, meat['id'], 0x47002308), 'out of combat')
            mark = len(p.events); p.send('combat', enabled=False); until(p, mark, 'combat', lambda e:not e['enabled'])
            mark = use(p, meat['id'], 0x47002308)
            until(p, mark, 'inventory_remove', lambda e:e['id'] == meat['id'])
            bone = owned(p, 0x470022f4, mark); remainder = owned(p, 0x470022f8, mark)
            assert remainder['quantity'] == 1 and remainder['id'] != meat['id']
            reject(p, use(p, meat['id'], 0x47002308), 'not in your backpack')
            mark = use(p, old_meat['id'], 0x47002308)
            until(p, mark, 'inventory_remove', lambda e:e['id'] == old_meat['id'])
            owned(p, 0x470022f4, mark)
            leave(p); stop(); server = start(log); p = login()
            assert owned(p, 0x470022f4)['id'] == bone['id']
            assert owned(p, 0x470022f8)['quantity'] == 1
            # Chain the actual returned instances into subsequent trades.
            item = owned(p, 0x470022f9)
            for rule in catalog['rules'][1:]:
                npc = rule['npc']['id']; at(p, npc)
                mark = use(p, item['id'], npc)
                until(p, mark, 'inventory_remove', lambda e:e['id'] == item['id'])
                item = owned(p, rule['output'], mark)
                assert item['quantity'] == rule['quantity']
                until(p, mark, 'chat', lambda e:'Exchanged one' in str(e))
            gems = item
            leave(p); stop(); server = start(log); p = login()
            assert owned(p, 0x470022ec)['id'] == gems['id']
            assert owned(p, 0x470022ec)['quantity'] == 8
            assert not any(e.get('container') == p.id and e.get('entity') in (0x470022f9, 0x470022fd, 0x470022fc) for e in p.events)
            leave(p)
            report = dict(result='PASS', production_npcs=4, exchanges=4,
                          checks=['range, combat, wrong item, foreign ownership and throttle rejection',
                                  'ordinary NPC use gives guidance without spending',
                                  'partial stack preserves remainder; consumed identity replay fails',
                                  'existing singleton Rabbit Meat loot remains usable',
                                  'returned eggs and truffle usable in subsequent trades',
                                  'eight native gems with stack metadata', 'two restart/reconnect persistence checks'],
                          limitation='Inputs seeded offline; full Nature\'s Ally quest not implemented.')
            (run / 'result.json').write_text(json.dumps(report, indent=2)); print(json.dumps(report)); print(run)
        finally:
            for i, p in enumerate(peers):
                (run / f'peer-{i}.json').write_text(json.dumps(p.events)); p.close()
            stop()


if __name__ == '__main__':
    main()
