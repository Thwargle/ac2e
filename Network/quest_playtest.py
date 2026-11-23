"""Disposable loopback gameplay fixture using prepared actor placements."""
from contextlib import contextmanager
import json
import os
import subprocess
import time
import uuid
from types import SimpleNamespace

from prepare_content import prepare
from test_gameplay import until
from test_network import DAT_DIRECTORY, ROOT, SERVER, Peer


def phase(peer, mark, quest, number):
    packet = until(peer, mark, 'quests', lambda e: any(
        q['id'] == quest and q['phase'] == number for q in e['items']))
    return next(q for q in packet['items'] if q['id'] == quest)


def use(peer, item, target='0'):
    time.sleep(1.15)
    mark = len(peer.events)
    peer.send('use', item=item, target=target)
    return mark


def quality(peer, mark, key, amount):
    return until(peer, mark, 'quality', lambda e: e.get('id') is None and
                 e['key'] == key and int(e['value']) == amount)


@contextmanager
def quest_world(name, required, enemy, xp, port):
    run = ROOT / 'Saved/AC2NetworkTests' / (name + '-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    prepare(DAT_DIRECTORY, run / 'prepared.tsv')
    actors = {}
    for line in (run / 'prepared.tsv').read_text(encoding='utf-8').splitlines()[1:]:
        row = line.split('\t')
        if len(row) == 13 and int(row[1], 16) in required:
            actors.setdefault(int(row[1], 16), row)
    assert set(actors) == set(required), actors
    content = run / 'world.tsv'
    content.write_text('pyrealium-world-v4\n' + ''.join('\t'.join(r) + '\n' for r in actors.values()))
    rules = run / 'encounters.tsv'
    rules.write_text('pyrealium-encounters-v1\nplayer\t100\t1\t1000\t5\n'
                     f'regen\t2\t4\t1000\nnpc\t{enemy:08x}\t1\t1\t60000\t3\t300000\t31\t2\n')
    world = run / 'store'
    for args in (['character-create-human', '--name', 'Atlas Hunter'],
                 ['player-state', '--character-id', '1', '--event', f'x,{xp}']):
        subprocess.run([str(SERVER), *args, '--account', 'local', '--data', str(world)],
                       capture_output=True, check=True)
    peers = []
    server = None

    def connect(log):
        nonlocal server
        server = subprocess.Popen([
            str(SERVER), 'serve', '--bind', f'127.0.0.1:{port}', '--account', 'local',
            '--data', str(world), '--game-data', str(DAT_DIRECTORY), '--content', str(content),
            '--encounters', str(rules), '--initialize-ui', 'true', '--test-map-travel', 'true',
            '--portals', 'false', '--lifestones', 'false', '--seconds', '180'],
            env=dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1'), stdout=log, stderr=log,
            creationflags=0x08000000)
        time.sleep(1)
        assert server.poll() is None, (run / 'server.log').read_text()
        peer = Peer()
        peers.append(peer)
        peer.send('connect', host='127.0.0.1', port=port, account='local', dat_directory=str(DAT_DIRECTORY))
        peer.wait('characters')
        peer.id = '1'
        peer.send('enter', id=peer.id)
        peer.wait('player')
        peer.send('ready')
        peer.wait('status', lambda e: e['phase'] == 'world')
        until(peer, 0, 'inventory')
        return peer

    def stop():
        if server and server.poll() is None:
            (world / 'stop.request').write_text('stop\n')
            assert server.wait(timeout=15) == 0

    def leave(peer):
        mark = len(peer.events)
        peer.send('logout', id=peer.id)
        until(peer, mark, 'selection')
        peer.close()
        stop()

    def visit(peer, definition):
        row = actors[definition]
        iid = str(int(row[0], 16))
        peer.send('move', cell=int(row[3], 16), position=list(map(float, row[4:7])), heading=0, motion=[0,0,0])
        until(peer, 0, 'object', lambda e: e['id'] == iid)
        time.sleep(.3)
        return iid

    try:
        with (run / 'server.log').open('w') as log:
            yield SimpleNamespace(connect=lambda: connect(log), leave=leave, visit=visit, run=run, store=world)
    finally:
        for i, peer in enumerate(peers):
            (run / f'peer-{i}.json').write_text(json.dumps(peer.events))
            peer.close()
        stop()

