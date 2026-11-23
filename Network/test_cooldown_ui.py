"""Native cooldown timestamps and rendered meters on a disposable injured caster."""
import json
import os
import shutil
import subprocess
import time
import uuid
import heapq
import select
import socket
import threading
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer
from test_services import fixture
from test_gameplay import until


class DelayedUDP:
    """Forward real datagrams unchanged; expose pending UI with 500ms return latency."""
    def __init__(self):
        self.listeners = []
        for port in (19394, 19395):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.bind(('127.0.0.1', port))
            self.listeners.append(sock)
        # Both retail destination ports must see the same upstream peer address.
        self.upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.upstream.bind(('127.0.0.1', 0))
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        servers = [('127.0.0.1', port) for port in (19391, 19392)]
        client, pending = None, []
        while not self.stop.is_set():
            readable, _, _ = select.select([*self.listeners, self.upstream], [], [], .01)
            for sock in readable:
                try:
                    data, source = sock.recvfrom(65536)
                except ConnectionResetError:
                    # Windows reports ICMP port-unreachable here when the
                    # rendered client exits with replies still in flight.
                    continue
                if sock is self.upstream:
                    if client and source in servers:
                        heapq.heappush(pending, (time.monotonic() + .5, data, client, servers.index(source)))
                else:
                    client = source
                    self.upstream.sendto(data, servers[self.listeners.index(sock)])
            while pending and pending[0][0] <= time.monotonic():
                _, data, destination, index = heapq.heappop(pending)
                self.listeners[index].sendto(data, destination)

    def close(self):
        self.stop.set(); self.thread.join(timeout=2)
        for sock in [*self.listeners, self.upstream]:
            sock.close()


def main():
    run = ROOT / 'Saved/AC2NetworkTests' / ('cooldown-ui-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    content = fixture(run, isolated=True)
    env = dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1', AC2_COOLDOWN_SMOKE='1')
    peer = server = proxy = None
    with (run / 'server.log').open('w') as log:
        args = [str(SERVER), 'serve', '--bind', '127.0.0.1:19391', '--account', 'local',
                '--data', str(run / 'world'), '--game-data', str(DAT_DIRECTORY), '--content', str(content),
                '--initialize-ui', 'true', '--encounters', 'none',
                '--lifestones', 'false', '--portals', 'false', '--seconds', '130']

        def start():
            p = subprocess.Popen(args, env=env, stdout=log, stderr=log, creationflags=0x08000000)
            time.sleep(1)
            assert p.poll() is None, (run / 'server.log').read_text()
            return p

        def connect():
            p = Peer()
            p.send('connect', host='127.0.0.1', port=19391, account='local')
            p.wait('characters')
            return p

        def enter(p, character):
            p.send('enter', id=character); p.wait('player'); p.send('ready')
            p.wait('status', lambda e: e['phase'] == 'world')

        def reset_event(p, mark):
            return until(p, mark, 'skills', lambda e: any(s['id'] == 0x55 and s.get('reset_started', -1) >= 0 for s in e['items']))

        def stamp(event):
            return next(s['reset_started'] for s in event['items'] if s['id'] == 0x55)

        try:
            server = start(); peer = connect()
            peer.send('create', name='Cooldown Render Tester', start_area=0, race=1, sex=4096, physical=[[1, 1], [2, 1]])
            character = peer.wait('created')['character']['id']; assert character != '0'
            enter(peer, character)
            peer.send('logout', id=character); peer.wait('selection'); peer.close(); peer = None
            (run / 'world/stop.request').write_text('stop\n'); server.wait(timeout=10)
            for command, event in [('player-state', 'x,10000'), ('skill-state', 't,59'),
                                   ('skill-state', 't,55'), ('player-state', 'd,150')]:
                subprocess.run([str(SERVER), command, '--account', 'local', '--data', str(run / 'world'),
                                '--character-id', character, '--event', event], check=True, env=env,
                               stdout=log, stderr=log, creationflags=0x08000000)
            server = start(); peer = connect(); enter(peer, character)
            until(peer, 0, 'inventory')
            weapon = until(peer, 0, 'object', lambda e: e.get('container') == character and e.get('entity') == 0x470007ee)
            mark = len(peer.events); peer.send('use', item=weapon['id'], target='0')
            until(peer, mark, 'inventory_result', lambda e: e['id'] == weapon['id'] and e['status'] == 0)
            mark = len(peer.events); peer.send('skill', skill=0x55, target='0')
            accepted = stamp(reset_event(peer, mark)); assert accepted >= 0
            shared = until(peer, mark, 'quality', lambda e: e['table'] == 8 and e['key'] == 0x12f)
            duration = until(peer, mark, 'quality', lambda e: e['table'] == 3 and e['key'] == 0xbb8)
            assert shared['value'] == accepted and duration['value'] == 1
            requested = until(peer, mark, 'skill_requested', lambda e: e['skill'] == 0x55)
            until(peer, mark, 'story', lambda e: e['id'] == character and e.get('request') == requested['request'])
            mark = len(peer.events); peer.send('skill', skill=0x55, target='0')
            until(peer, mark, 'skill_finished')
            assert not any(e['event'] == 'skills' or (e['event'] == 'quality' and e['table'] == 8) for e in peer.events[mark:])
            assert any(e['event'] == 'chat' and 'recovering' in str(e).lower() for e in peer.events[mark:]), peer.events[mark:]
            # Advancement refreshes SkillInfo; it must retain the running reset.
            time.sleep(.15)  # Respect the existing inventory/advancement throttle.
            mark = len(peer.events); peer.send('skill_raise', skill=0x55)
            assert stamp(reset_event(peer, mark)) == accepted
            mark = len(peer.events); peer.send('logout', id=character); peer.wait('selection')
            enter(peer, character)
            assert stamp(reset_event(peer, mark)) == accepted
            restored = until(peer, mark, 'quality', lambda e: e['table'] == 8 and e['key'] == 0x12f)
            assert restored['value'] == accepted
            peer.send('logout', id=character); peer.wait('selection')
            (run / 'peer.json').write_text(json.dumps(peer.events, indent=2))
            peer.close(); peer = None
            proxy = DelayedUDP()
            result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT/'Launch-AC2.ps1'),
                                     '-SkipBuild', '-NetworkSmoke', '-Server', '127.0.0.1', '-Port', '19394', '-Account', 'local'],
                                    cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                    errors='replace', timeout=100, creationflags=0x08000000)
            shutil.copyfile(ROOT/'Unreal/Saved/Logs/AC2Network-smoke.log', run/'client.log')
            for shot in (ROOT/'Unreal/Saved/Screenshots').glob('AC2Cooldown-*.png'):
                shutil.copyfile(shot, run/shot.name)
            print(result.stdout); result.check_returncode()
            assert 'AC2 cooldown render smoke PASS' in (run/'client.log').read_text(encoding='utf-8', errors='replace')
            (run/'result.json').write_text(json.dumps({'native_timestamps': True, 'rejected_cast_preserves_timer': True,
                'advancement_preserves_timer': True, 'reentry_restores_timer': True, 'rendered_fill_paging_expiry': True,
                'rendered_pending_accept_reject_and_paging': True, 'rendered_active_animation_and_paging': True,
                'rendered_heal_from_peace_enters_casting_stance': True}, indent=2))
            print('PASS: accepted/rejected casts, advancement, reentry, rendered cooldown movement, duplicate bindings, paging and expiry.', run)
        finally:
            if proxy:
                proxy.close()
            if peer:
                (run/'peer.json').write_text(json.dumps(peer.events, indent=2)); peer.close()
            if server and server.poll() is None:
                (run/'world/stop.request').write_text('stop\n'); server.wait(timeout=10)


if __name__ == '__main__':
    main()
