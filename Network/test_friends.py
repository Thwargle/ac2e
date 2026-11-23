"""Exercise AC2E Social > Friends using real bridge processes and isolated accounts."""
import json
import os
import socket
import struct
import subprocess
import time
import urllib.request
import uuid
import xml.etree.ElementTree as ET

from test_gameplay import until
from test_network import DAT_DIRECTORY, Peer, ROOT, SERVER


def port(kind=socket.SOCK_STREAM):
    while True:
        with socket.socket(type=kind) as first:
            first.bind(('127.0.0.1', 0))
            result = first.getsockname()[1]
            if kind == socket.SOCK_STREAM:
                return result
            if result < 65535:
                with socket.socket(type=kind) as second:
                    try:
                        second.bind(('127.0.0.1', result + 1))
                    except OSError:
                        continue
                    return result


def stop(process, marker):
    if process and process.poll() is None:
        marker.write_text('stop\n')
        try:
            assert process.wait(timeout=10) == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()


def main():
    run = ROOT / 'Saved/AC2NetworkTests' / ('friends-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    auth, world = run / 'accounts', run / 'world'
    game_port, web_port = port(socket.SOCK_DGRAM), port()
    origin = f'http://127.0.0.1:{web_port}'
    secret = 'AC2e-' + uuid.uuid4().hex
    peers = []
    game = None

    def login(account):
        request = urllib.request.Request(origin + '/api/login',
            data=json.dumps({'account': account, 'password': secret}).encode(),
            headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=5) as response:
            return json.load(response)

    def enter(peer, character):
        mark = len(peer.events)
        peer.send('enter', id=character)
        until(peer, mark, 'player')
        peer.send('ready')
        until(peer, mark, 'status', lambda e: e['phase'] == 'world')
        return until(peer, mark, 'friends')['members']

    def join(account, name):
        token = login(account)
        peer = Peer()
        peers.append(peer)
        peer.send('connect', host='127.0.0.1', port=game_port, account=token['account'], ticket=token['ticket'])
        listing = peer.wait('characters')['characters']
        existing = next((row for row in listing if row['name'] == name), None)
        if not existing:
            peer.send('create', name=name, race=1, sex=4096, start_area=0, physical=[[1, 1], [2, 1]])
            existing = peer.wait('created')['character']
        peer.id = existing['id']
        return peer, enter(peer, peer.id)

    def command(peer, action, event, **fields):
        mark = len(peer.events)
        peer.send('friends', action=action, **fields)
        return until(peer, mark, event)

    with (run / 'server.log').open('w') as log:
        def launch(*args):
            return subprocess.Popen([str(SERVER), *map(str, args)], stdout=log, stderr=log,
                env=dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1'), creationflags=subprocess.CREATE_NO_WINDOW)

        def start_game(*extra):
            proc = launch('serve', '--bind', f'127.0.0.1:{game_port}', '--auth-data', auth, '--data', world,
                '--game-data', DAT_DIRECTORY, '--content', 'none', '--encounters', 'none', '--seconds', '150', *extra)
            time.sleep(1)
            assert proc.poll() is None, run
            return proc

        web = launch('account-web', '--auth-data', auth, '--data', world, '--web-bind', f'127.0.0.1:{web_port}',
            '--public-origin', origin, '--public-host', '127.0.0.1', '--public-port', game_port)
        try:
            time.sleep(.7)
            assert web.poll() is None, run
            adapter = port()
            game = start_game('--gun-adapter-bind', f'127.0.0.1:{adapter}')
            alice, rows = join('friend_alpha', 'Aster Watcher')
            assert not rows
            bob, rows = join('friend_beta', 'Birch Walker')
            assert not rows
            assert command(alice, 'add', 'friend_error', name='Aster Watcher')['result'] != 0
            assert command(alice, 'add', 'friend_error', name='Absent Player')['result'] != 0

            mark = len(alice.events)
            command(alice, 'add', 'friend_result', name='bIRch wAlKer')
            row, = until(alice, mark, 'friends')['members']
            assert row['name'] == 'Birch Walker', row
            if row['mode'] != 9:
                until(alice, mark, 'friend_status', lambda e: e['name'] == row['name'] and e['mode'] == 9)
            assert command(alice, 'add', 'friend_error', name='Birch Walker')['result'] == 0xc1121009
            assert command(bob, 'list', 'friends')['members'] == []

            for setting in (2, 4, 1):
                mark = len(alice.events)
                assert command(bob, 'mode', 'friend_mode', mode=setting)['mode'] == setting
                until(alice, mark, 'friend_status', lambda e: e['name'] == 'Birch Walker' and e['mode'] == 9 | setting)
            mark = len(alice.events)
            bob.send('logout', id=bob.id)
            bob.wait('characters')
            until(alice, mark, 'friend_status', lambda e: e['mode'] == 0)
            mark = len(alice.events)
            command(alice, 'remove', 'friend_result', name='Birch Walker')
            assert until(alice, mark, 'friends')['members'] == []
            mark = len(alice.events)
            command(alice, 'add', 'friend_result', name='birch walker')
            offline, = until(alice, mark, 'friends')['members']
            assert (offline['name'], offline['mode']) == ('Birch Walker', 0)
            bob.send('create', name='Cedar Walker', race=1, sex=4096, start_area=0, physical=[[1, 1], [2, 1]])
            alternate = bob.wait('created')['character']['id']
            assert enter(bob, alternate) == []
            assert command(alice, 'list', 'friends')['members'][0]['mode'] == 0
            bob.send('logout', id=alternate)
            bob.wait('characters')
            mark = len(alice.events)
            assert enter(bob, bob.id) == []
            until(alice, mark, 'friend_status', lambda e: e['name'] == 'Birch Walker' and e['mode'] == 9)

            # A separately ticketed legacy adapter session shares the same account roster.
            with socket.create_connection(('127.0.0.1', adapter), timeout=5) as sock, sock.makefile('rb') as stream:
                def send_xml(tag, **attrs):
                    packet = ET.tostring(ET.Element(tag, attrs))
                    sock.sendall(struct.pack('>I', len(packet)) + packet)

                def receive_xml(tag):
                    while True:
                        size, = struct.unpack('>I', stream.read(4))
                        msg = ET.fromstring(stream.read(size))
                        if msg.tag == tag:
                            return msg

                token = login('friend_alpha')
                ticket = (token['account'] + ':' + token['ticket']).encode().hex()
                send_xml('PresentTicketCmd', seq='1', Ticket=ticket)
                assert receive_xml('PresentTicketRes').get('hr') == '0x00000000'
                assert receive_xml('FStart')[0].get('Name') == 'Birch Walker'
                mark = len(alice.events)
                send_xml('FriendDelete', seq='2', Name='Birch Walker')
                assert receive_xml('FriendDeleteRes').get('hr') == '0x00000000'
                assert until(alice, mark, 'friends')['members'] == []
                command(alice, 'add', 'friend_result', name='Birch Walker')
                assert receive_xml('FStart')[0].get('Name') == 'Birch Walker'

            for peer in peers:
                peer.close()
            peers.clear()
            stop(game, world / 'stop.request')
            game = start_game()  # The Unreal tab also works with the optional TCP service off.
            alice, rows = join('friend_alpha', 'Aster Watcher')
            assert [(row['name'], row['mode']) for row in rows] == [('Birch Walker', 0)]
            mark = len(alice.events)
            command(alice, 'remove', 'friend_result', name='BIRCH WALKER')
            assert until(alice, mark, 'friends')['members'] == []
            assert command(alice, 'remove', 'friend_error', name='Birch Walker')['result'] == 0xc1121401
            print('PASS: account-private friend lists, edits, availability, character switching, adapter sync and restart.', run)
        finally:
            for peer in peers:
                peer.close()
            stop(game, world / 'stop.request')
            stop(web, auth / 'account-stop.request')


if __name__ == '__main__':
    main()
