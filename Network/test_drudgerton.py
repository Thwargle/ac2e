"""Play To Drudgerton! using the real UDP client bridge and prepared world.

Only the disposable character's starting XP and travel to the two captured NPC
positions are accelerated. NPC identity, reach, dialogue, journal, cooldown and
rewards are checked through the same protocol used by the game client.
"""
import json
import os
import subprocess
import time
import uuid

from prepare_content import prepare
from test_gameplay import until
from test_network import DAT_DIRECTORY, Peer, ROOT, SERVER


QUEST = 0x4000fe04
LEVEL_40_XP = 43_141_054
PORT = 19612


def main():
    run = ROOT / 'Saved/AC2NetworkTests' / ('drudgerton-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    store = run / 'world'
    content = run / 'world.tsv'
    prepare(DAT_DIRECTORY, content)
    speakers = {}
    for line in content.read_text(encoding='utf-8').splitlines():
        row = line.split('\t')
        if len(row) == 13 and row[-1] in ('Jrent', 'Jellio'):
            assert row[-1] not in speakers, f'Duplicate {row[-1]} in prepared world'
            speakers[row[-1]] = row
    assert set(speakers) == {'Jrent', 'Jellio'}
    clients = []
    server = None

    def start(log):
        nonlocal server
        server = subprocess.Popen([
            str(SERVER), 'serve', '--bind', f'127.0.0.1:{PORT}', '--account', 'local',
            '--data', str(store), '--game-data', str(DAT_DIRECTORY), '--content', str(content),
            '--initialize-ui', 'true', '--test-map-travel', 'true', '--seconds', '180'],
            env=dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1'), stdout=log, stderr=log,
            creationflags=0x08000000)
        time.sleep(1)
        assert server.poll() is None, (run / 'server.log').read_text()

    def stop():
        if server and server.poll() is None:
            (store / 'stop.request').write_text('stop\n')
            server.wait(timeout=15)

    def login():
        p = Peer()
        clients.append(p)
        p.send('connect', host='127.0.0.1', port=PORT, account='local', dat_directory=str(DAT_DIRECTORY))
        p.wait('characters')
        p.id = '1'
        p.send('enter', id=p.id)
        p.wait('player')
        p.send('ready')
        p.wait('status', lambda e: e['phase'] == 'world')
        until(p, 0, 'inventory')
        return p

    def logout(p):
        mark = len(p.events)
        p.send('logout', id=p.id)
        until(p, mark, 'selection')
        p.close()

    def use(p, speaker, distance=0.5):
        row = speakers[speaker]
        pos = list(map(float, row[4:7]))
        pos[0] += distance
        cell = (int(row[3], 16) & 0xffff0000) | (int(pos[0] / 20) * 8 + int(pos[1] / 20) + 1)
        p.send('move', cell=cell, position=pos, heading=0, motion=[0, 0, 0])
        iid = str(int(row[0], 16))
        until(p, 0, 'object', lambda e: e['id'] == iid)
        time.sleep(1.1)
        mark = len(p.events)
        p.send('use', item=iid, target='0')
        return mark

    def journal(p, mark, status):
        event = until(p, mark, 'quests', lambda e: any(
            q['id'] == QUEST and q['status'] == status for q in e['items']))
        return next(q for q in event['items'] if q['id'] == QUEST)

    def total(p, mark, key, amount):
        until(p, mark, 'quality', lambda e: e.get('id') is None and
              e['key'] == key and int(e['value']) == amount)

    def award_xp(amount):
        subprocess.run([str(SERVER), 'player-state', '--account', 'local', '--character-id', '1',
                        '--event', f'x,{amount}', '--data', str(store)], check=True, capture_output=True)

    subprocess.run([str(SERVER), 'character-create-human', '--account', 'local',
                    '--name', 'Drudge Traveler', '--data', str(store)], check=True, capture_output=True)
    award_xp(LEVEL_40_XP - 1)
    with (run / 'server.log').open('w') as log:
        try:
            start(log)
            p = login()
            mark = use(p, 'Jrent')
            until(p, mark, 'chat', lambda e: 'Reach level 40' in str(e))
            assert not any(q['id'] == QUEST for e in p.events if e['event'] == 'quests' for q in e['items'])
            logout(p)
            stop()
            award_xp(1)
            start(log)
            p = login()
            mark = use(p, 'Jellio')
            until(p, mark, 'chat', lambda e: 'Jrent is at Linvak Tukal' in str(e))
            mark = use(p, 'Jrent', 8)
            until(p, mark, 'chat', lambda e: 'four metres' in str(e))
            mark = use(p, 'Jrent')
            accepted = journal(p, mark, 0x10000000)
            assert accepted['phase'] == 1 and 604790 <= accepted['remaining'] <= 604800, accepted
            logout(p)
            stop()
            start(log)
            p = login()
            restored = journal(p, 0, 0x10000000)
            assert accepted['bestowal'] == restored['bestowal']
            assert 0 < restored['remaining'] <= accepted['remaining']
            mark = use(p, 'Jellio')
            until(p, mark, 'chat', lambda e: 'Blazing Drudge Festival' in str(e))
            finished = journal(p, mark, 0x30000000)
            assert finished['phase'] == 0 and not finished['expires']
            assert 604790 <= finished['retry'] <= 604800
            total(p, mark, 0x12c, LEVEL_40_XP + 750_000)
            total(p, mark, 0x7ac20003, 1500)
            mark = use(p, 'Jellio')
            until(p, mark, 'chat', lambda e: 'complete' in str(e))
            mark = use(p, 'Jrent')
            until(p, mark, 'chat', lambda e: 'repeat' in str(e))
            logout(p)
            stop()
            start(log)
            p = login()
            journal(p, 0, 0x30000000)
            total(p, 0, 0x12c, LEVEL_40_XP + 750_000)
            total(p, 0, 0x7ac20003, 1500)
            logout(p)
            report = dict(result='PASS', quest=f'{QUEST:08x}', title='To Drudgerton!',
                          xp=750000, quest_points=1500, checks=[
                              'Jrent rejects level 39 and grants quest at level 40',
                              'unaccepted Jellio greeting and range rejection',
                              'seven-day deadline persists after restart',
                              'Jellio completes quest and awards XP and points',
                              'duplicate use and early repeat pay nothing across restart'])
            (run / 'result.json').write_text(json.dumps(report, indent=2))
            print('PASS', run, json.dumps(report))
        finally:
            for i, client in enumerate(clients):
                (run / f'peer-{i}.json').write_text(json.dumps(client.events))
                client.close()
            stop()


if __name__ == '__main__':
    main()
