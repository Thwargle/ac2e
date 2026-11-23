"""Native shortcut/game-options acceptance against a disposable AC2E store."""
import json
import os
import subprocess
import time
import uuid
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer, prepare_test_content
from test_gameplay import until


def main():
    run = ROOT / 'Saved/AC2ShortcutTests' / uuid.uuid4().hex
    run.mkdir(parents=True)
    content = prepare_test_content(run)
    env = dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1')
    args = [str(SERVER), 'serve', '--bind', '127.0.0.1:19397', '--account', 'local',
            '--data', str(run/'world'), '--game-data', str(DAT_DIRECTORY), '--content', str(content),
            '--initialize-ui', 'true', '--encounters', 'none',
            '--lifestones', 'false', '--portals', 'false', '--seconds', '180']
    server = peer = None
    options = {}
    with (run/'server.log').open('w', encoding='utf-8') as log:
        def start():
            process = subprocess.Popen(args, env=env, stdout=log, stderr=log, creationflags=0x08000000)
            time.sleep(1)
            assert process.poll() is None, (run/'server.log').read_text()
            return process

        def stop():
            if server and server.poll() is None:
                (run/'world/stop.request').write_text('stop\n')
                try:
                    server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.terminate()
                    server.wait(timeout=5)

        def connect():
            p = Peer()
            p.send('connect', host='127.0.0.1', port=19397, account='local')
            p.wait('characters')
            return p

        def create(name):
            peer.send('create', name=name, start_area=0, race=1, sex=4096, physical=[[1, 1], [2, 1]])
            return peer.wait('created')['character']['id']

        def enter(character):
            mark = len(peer.events)
            peer.send('enter', id=character)
            peer.wait('player')
            peer.send('ready')
            peer.wait('status', lambda e: e['phase']=='world')
            bar = until(peer, mark, 'shortcuts')['items']
            options[character] = until(peer, mark, 'game_options')
            assert len(bar)==100
            return bar

        def leave(character):
            mark = len(peer.events)
            peer.send('logout', id=character)
            peer.wait('selection')
            listing = peer.wait('characters')
            assert character in {c['id'] for c in listing['characters']}
            assert sum(e.get('event')=='selection' for e in peer.events[mark:])==1

        try:
            server = start()
            peer = connect()
            character = create('Shortcut Hero')
            other = create('Other Shortcut Hero')
            starter = enter(character)
            initial_options = options[character].copy()
            saved_options = {'bits': 0x8001403c, 'radar': 0x80000021, 'height': 4, 'range': 0.375}
            other_options = {'bits': 0x80002805, 'radar': 0x40000012, 'height': 2, 'range': -1.0}
            def check_options(character, expected):
                assert options[character]['fields'] == 0x10a8, options[character]
                for key, value in expected.items():
                    assert options[character][key] == value, (key, options[character], expected)
            assert [b.get('value') for b in starter[:3]]==[0xb0, 0x60, 0xcf], starter[:3]
            updates = {
                5: {'kind': 'text', 'text': '/say café, 世界 🙂'},
                6: {'kind': 'recipe', 'value': 0x6f000001},
                7: {'kind': 'item', 'id': '9308940437337407489'},
                8: {'kind': 'enum', 'value': 0},
                98: {'kind': 'text', 'text': '🙂'*256},
                99: {'kind': 'skill', 'value': 0x60},
            }
            for slot, binding in updates.items():
                peer.send('shortcut', slot=slot, binding=binding)
            peer.send('game_options', **saved_options)
            # Queue an ordinary self-inspection too: it must not satisfy the
            # later preference barrier early. Logout must work immediately.
            peer.send('examine', id=character)
            leave(character)
            restored = enter(character)
            check_options(character, saved_options)
            assert restored[:3]==starter[:3], 'First edit lost untouched starter slots'
            for slot, binding in updates.items():
                assert restored[slot]==binding, (slot, restored[slot], binding)
            mark = len(peer.events)
            peer.send('shortcut', slot=100, binding={'kind': 'empty'})
            assert 'slot' in until(peer, mark, 'error')['message'].lower()
            leave(character)
            assert enter(other)==starter, 'Preferences leaked to another character'
            assert options[other]==initial_options, 'Game options leaked to another character'
            # No shortcut writes here: options alone must fence immediate logout.
            peer.send('game_options', **other_options)
            leave(other)
            assert enter(character)==restored
            check_options(character, saved_options)
            mark = len(peer.events)
            peer.send('game_options', **dict(saved_options, height=6))
            assert 'height' in until(peer, mark, 'error')['message'].lower()
            for slot in range(100):
                peer.send('shortcut', slot=slot, binding={'kind': 'empty'})
            # A failed unrelated examination must not strand preference logout.
            peer.send('examine', id='999999999')
            leave(character)
            assert all(b['kind']=='empty' for b in enter(character)), 'Cleared bar was replaced with defaults'
            check_options(character, saved_options)
            leave(character)
            peer.close()
            peer = None
            stop()
            server = start()
            peer = connect()
            assert all(b['kind']=='empty' for b in enter(character)), 'Empty bar lost on process restart'
            check_options(character, saved_options)
            leave(character)
            assert enter(other)==starter
            check_options(other, other_options)
            leave(other)
            # Reuse one UDP session repeatedly with no scene-settle delay.
            # This used to fill the reliable window and silently lose logout.
            for index in range(12):
                current = character if index % 2 == 0 else other
                bar = enter(current)
                check_options(current, saved_options if current==character else other_options)
                assert all(b['kind']=='empty' for b in bar) if current==character else bar==starter
                leave(current)
            assert not any('Invalid native shortcut' in e.get('message','') for e in peer.events)
            assert 'application event rejected: Capacity' not in (run/'server.log').read_text(encoding='utf-8')
            print('PASS: native shortcut and game-options upload/restore, opaque flags, radar, height, float range/sentinel, invalid height rejection, starter preservation, Unicode/64-bit IDs, recipes, enum preservation, character isolation, all-clear, server restart and 12 immediate alternating-character logout/re-entry cycles. Logs:', run)
        finally:
            if peer:
                (run/'peer.json').write_text(json.dumps(peer.events, indent=2), encoding='utf-8')
                peer.close()
            stop()


if __name__=='__main__':
    main()
