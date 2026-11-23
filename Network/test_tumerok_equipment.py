"""Recovered drums/javelins: native equip parenting, stance and durable replay.

Development grants are confined to this disposable store; no player save is edited.
"""
import json
import os
import subprocess
import time
import uuid
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer
from test_gameplay import until
from prepare_content import prepare

WEAPONS = [(0x47000f02, 0x1f000059, 3, 0x41000011),  # Rain Drum
           (0x47000f05, 0x1f000427, 3, 0x41000011),  # Fury Drum
           (0x47000f2e, 0x1f0002b3, 4, 0x41000010)]  # Spine Javelin


def main():
    run = ROOT/'Saved/AC2NetworkTests'/('tumerok-equipment-'+uuid.uuid4().hex)
    run.mkdir(parents=True)
    content = run/'world.tsv'; prepare(DAT_DIRECTORY, content)
    env = dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1')
    peers, server, identity = [], None, None
    with (run/'server.log').open('w') as log:
        def start():
            return subprocess.Popen([str(SERVER), 'serve', '--bind', '127.0.0.1:19553',
                '--account', 'local', '--data', str(run/'world'), '--game-data', str(DAT_DIRECTORY),
                '--content', str(content), '--initialize-ui', 'true', '--seconds', '150'], env=env, stdout=log, stderr=log, creationflags=0x08000000)

        def stop():
            if server and server.poll() is None:
                (run/'world/stop.request').write_text('stop\n'); server.wait(timeout=15)

        def login(create=False):
            p = Peer(); peers.append(p)
            p.send('connect', host='127.0.0.1', port=19553, account='local'); p.wait('characters')
            if create:
                p.send('create', name='Tumerok Equipment', race=4, sex=4096, start_area=0, physical=[[1,1],[2,1]])
                p.id = p.wait('created')['character']['id']
            else:
                p.id = identity
            p.send('enter', id=p.id); p.wait('player'); p.send('ready')
            p.wait('status', lambda e:e['phase']=='world'); until(p, 0, 'inventory')
            return p

        try:
            server = start(); time.sleep(1); p = login(True); identity = p.id
            mark = len(p.events); p.send('logout', id=p.id); until(p, mark, 'selection'); p.close(); stop()
            for definition, visual, _, _ in WEAPONS:
                subprocess.run([str(SERVER), 'inventory-grant', '--account', 'local', '--data', str(run/'world'),
                    '--character-id', identity, '--game-data', str(DAT_DIRECTORY), '--id', f'{definition:x}',
                    '--visual-did', f'{visual:x}', '--equipment-mask', '40'], env=env, check=True, stdout=log, stderr=log)
            server = start(); time.sleep(1); p = login()
            results = []
            for definition, _, holding, mode in WEAPONS:
                item = until(p, 0, 'object', lambda e:e.get('entity')==definition and e.get('container')==p.id)['id']
                time.sleep(.3); mark = len(p.events); p.send('use', item=item, target='0')
                ack = until(p, mark, 'inventory_result', lambda e:e['id']==item)
                assert ack['status']==0 and ack['equipped']==0x40, ack
                held = until(p, mark, 'object', lambda e:e['id']==item and e.get('parent')==p.id)
                assert held['holding']==holding, held
                mark = len(p.events); p.send('combat', enabled=True)
                until(p, mark, 'combat', lambda e:e['enabled'])
                stance = until(p, mark, 'mode', lambda e:e['id']==p.id)
                assert stance['mode']==mode, stance
                p.send('combat', enabled=False); until(p, mark, 'combat', lambda e:not e['enabled'])
                results.append(dict(definition=f'{definition:08x}', holding=holding, mode=mode))
            mark = len(p.events); p.send('logout', id=p.id); until(p, mark, 'selection'); p.close(); stop()
            server = start(); time.sleep(1); p = login()
            inventory = until(p, 0, 'inventory')
            assert dict(id=item, slot=0x40) in inventory['equipment'], inventory
            held = until(p, 0, 'object', lambda e:e['id']==item and e.get('parent')==p.id)
            assert held['holding']==4, held
            (run/'result.json').write_text(json.dumps(dict(weapons=results, persisted=True), indent=2))
            print('PASS: Rain Drum, Fury Drum and Spine Javelin equip, native holding points, stances and restart.', run)
        finally:
            for i, p in enumerate(peers):
                (run/f'peer-{i}.json').write_text(json.dumps(p.events)); p.close()
            stop()


if __name__ == '__main__':
    main()
