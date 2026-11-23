"""A native UDP spawn waits for a player to vacate its observed anchor.

Uses a disposable store and one admitted capture, with an isolated initial delay.
No production content, characters or spawner settings are modified.
"""
import json
import os
import subprocess
import time
import uuid

from prepare_content import prepare
from test_gameplay import until
from test_network import DAT_DIRECTORY, ROOT, SERVER, Peer


def main():
    run = ROOT / 'Saved/AC2NetworkTests' / ('spawn-clearance-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    manifest = run / 'admitted.tsv'
    prepare(DAT_DIRECTORY, manifest)
    target = 0x81409bec26cb5af8  # Captured Edge Beetle.
    row = next(line.split('\t') for line in manifest.read_text(encoding='utf-8').splitlines()
               if line.startswith(f'{target:016x}\t'))
    content = run / 'world.tsv'
    content.write_text('pyrealium-world-v4\n' + '\t'.join(row) + '\n', encoding='utf-8')
    encounters = run / 'encounters.tsv'
    encounters.write_text(
        (ROOT / 'AC2E/emulator/content/development-encounters.tsv').read_text(encoding='utf-8')
        + f'\nspawner\t{row[1]}\t10000\t30000\n', encoding='utf-8')
    point = list(map(float, row[4:7]))
    cell = int(row[3], 16)
    observer_point = [point[0] + 12, point[1], 141.96219]  # Verified nearby native floor.
    peers = []
    with (run / 'server.log').open('w') as log:
        started = time.monotonic()
        server = subprocess.Popen([
            str(SERVER), 'serve', '--bind', '127.0.0.1:19489', '--account', 'local',
            '--data', str(run / 'world'), '--game-data', str(DAT_DIRECTORY),
            '--content', str(content), '--initialize-ui', 'true', '--encounters', str(encounters), '--lifestones', 'false', '--portals', 'false',
            '--test-map-travel', 'true', '--seconds', '60'],
            env=dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1'), stdout=log, stderr=log,
            creationflags=0x08000000)
        try:
            time.sleep(1)
            assert server.poll() is None, (run / 'server.log').read_text()
            for name in ['Spawn Observer', 'Spawn Occupant']:
                peer = Peer()
                peers.append(peer)
                peer.send('connect', host='127.0.0.1', port=19489, account='local')
                peer.wait('characters')
                peer.send('create', name=name, start_area=0, race=1, sex=4096,
                          physical=[[1, 1.3], [2, 1.2]])
                peer.id = peer.wait('created')['character']['id']
                peer.send('enter', id=peer.id)
                peer.wait('player')
                peer.send('ready')
                peer.wait('status', lambda e: e['phase'] == 'world')
            observer, occupant = peers
            observer.send('move', cell=cell, position=observer_point, heading=0, motion=[0, 0, 0])
            mark = len(observer.events)
            occupant.send('move', cell=cell, position=point, heading=0, motion=[0, 0, 0])
            arrival = until(observer, mark, 'object', lambda e: e['id'] == occupant.id
                            and e.get('placement', {}).get('cell') == cell)
            assert all(abs(a-b) < .01 for a, b in zip(arrival['placement']['position'], point))
            assert time.monotonic() - started < 10, 'Fixture arrived after initial spawn deadline'
            # Check multiple retries after the initial ten-second deadline.
            time.sleep(max(0, 13 - (time.monotonic() - started)))
            for peer in peers:
                assert not any(e.get('event') == 'object' and e['id'] == str(target)
                               for e in peer.events), 'Creature appeared inside the player'
            mark = len(observer.events)
            occupant.send('move', cell=cell, position=observer_point, heading=0, motion=[0, 0, 0])
            until(observer, mark, 'position', lambda e: e['id'] == occupant.id)
            birth = until(observer, mark, 'object', lambda e: e['id'] == str(target), timeout=5)
            assert birth['placement']['cell'] == cell
            assert all(abs(a-b) < .01 for a, b in zip(birth['placement']['position'], point)), birth
            time.sleep(2)
            for peer in peers:
                assert sum(e.get('event') == 'object' and e['id'] == str(target)
                           for e in peer.events) == 1, 'Duplicate spawn publication'
            (run / 'result.json').write_text(json.dumps({
                'result': 'PASS', 'target': row[0], 'anchor': point,
                'checks': ['native scaled player blocks occupied anchor', 'multiple delayed retries',
                           'spawn after player leaves', 'original home retained', 'one create per observer']}, indent=2))
            print('PASS: occupied spawn waits, then publishes once at its original home.', run)
        finally:
            for index, peer in enumerate(peers):
                peer.close()
                (run / f'peer-{index}.json').write_text(json.dumps(peer.events, indent=2))
            if server.poll() is None:
                (run / 'world/stop.request').write_text('stop\n')
                try:
                    server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.terminate()
                    server.wait()


if __name__ == '__main__':
    main()
