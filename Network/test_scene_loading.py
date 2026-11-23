"""A native UDP arrival receives its scene before becoming attackable."""
import json
import os
import subprocess
import time
import uuid

from prepare_content import prepare
from test_gameplay import until
from test_network import DAT_DIRECTORY, ROOT, SERVER, Peer


def main():
    run = ROOT / 'Saved/AC2NetworkTests' / ('scene-loading-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    content = run / 'world.tsv'
    prepare(DAT_DIRECTORY, content)
    target = 0x81409bec26cb5af8  # Captured Edge Beetle, with native perception.
    row = next(line.split('\t') for line in content.read_text(encoding='utf-8').splitlines()
               if line.startswith(f'{target:016x}\t'))
    near = list(map(float, row[4:7]))
    near[0] += 2
    peer = Peer()
    with (run / 'server.log').open('w') as log:
        server = subprocess.Popen([
            str(SERVER), 'serve', '--bind', '127.0.0.1:19488', '--account', 'local',
            '--data', str(run / 'world'), '--game-data', str(DAT_DIRECTORY),
            '--content', str(content), '--initialize-ui', 'true', '--encounters', 'world', '--lifestones', 'false', '--portals', 'false',
            '--test-map-travel', 'true', '--seconds', '90'],
            env=dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1'), stdout=log, stderr=log,
            creationflags=0x08000000)
        try:
            time.sleep(1)
            assert server.poll() is None, (run / 'server.log').read_text()
            peer.send('connect', host='127.0.0.1', port=19488, account='local')
            peer.wait('characters')
            peer.send('create', name='Loading Scout', start_area=0, race=1, sex=4096,
                      physical=[[1, 1], [2, 1]])
            player = peer.wait('created')['character']['id']
            peer.send('enter', id=player)
            peer.wait('player')
            peer.send('ready')
            peer.wait('status', lambda e: e['phase'] == 'world')
            # Save an outdoor arrival without altering any production character.
            mark = len(peer.events)
            peer.send('move', cell=int(row[3], 16), position=near, heading=0, motion=[0, 0, 0])
            until(peer, mark, 'object', lambda e: e['id'] == str(target))
            peer.send('logout', id=player)
            until(peer, mark, 'selection')

            mark = len(peer.events)
            peer.send('enter', id=player)
            until(peer, mark, 'player')
            # Deliberately withhold SceneRenderingComplete. Geometry/model
            # creation must not depend on the acknowledgement of that work.
            until(peer, mark, 'object', lambda e: e['id'] == str(target))
            until(peer, mark, 'object', lambda e: e.get('container') == player)
            until(peer, mark, 'inventory')
            until(peer, mark, 'skills')
            until(peer, mark, 'craft_registry')
            time.sleep(5)
            assert not any(e.get('event') == 'hit' and e.get('id') == player
                           for e in peer.events[mark:]), 'Loading player was attacked'
            assert not any(e.get('event') == 'status' and e.get('phase') == 'world'
                           for e in peer.events[mark:]), 'Arrival activated before ready'

            peer.send('ready')
            until(peer, mark, 'status', lambda e: e['phase'] == 'world')
            until(peer, mark, 'hit', lambda e: e['id'] == player, timeout=20)
            creates = [e for e in peer.events[mark:]
                       if e.get('event') == 'object' and e['id'] == str(target)]
            assert len(creates) == 1, 'Ready must not recreate already loaded models'
            (run / 'result.json').write_text(json.dumps({
                'result': 'PASS', 'target': row[0], 'protected_seconds': 5,
                'checks': ['scene, inventory, skills and crafting delivered before ready', 'no loading damage',
                           'native aggression after ready', 'no repeated create on activation']}, indent=2))
            print('PASS: scene preloads while protected; native aggression starts after ready.', run)
        finally:
            peer.close()
            (run / 'peer.json').write_text(json.dumps(peer.events, indent=2))
            if server.poll() is None:
                (run / 'world/stop.request').write_text('stop\n')
                try:
                    server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.terminate()
                    server.wait()


if __name__ == '__main__':
    main()
