"""Rendered combat input regression with real UDP and a disposable local store."""
import json
import os
import shutil
import socket
import subprocess
import time
import uuid
from test_network import Peer, ROOT, SERVER, DAT_DIRECTORY, prepare_test_content


def main():
    directory = ROOT / 'Saved/AC2NetworkTests' / ('combat-ui-' + uuid.uuid4().hex)
    directory.mkdir(parents=True)
    for port in (19486, 19487):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as check:
            check.bind(('127.0.0.1', port))
    content = prepare_test_content(directory)
    # A placed ground NPC in the loaded starter cell exercises foot alignment.
    # This is an isolated test fixture, not a claimed retail spawn coordinate.
    with content.open('a') as fixture:
        fixture.write('8140000000030700\t4700157e\t1f000d75\t02990109\t58\t238\t-44.606205\t1\t0\t0\t0\t-\n')
    env = dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1')
    # Give the UI/camera assertions time while native perception acquires us.
    # Damage/cadence are isolated test balance, not production creature rules.
    rules=directory/'encounters.tsv'
    rules.write_text((ROOT/'AC2E/emulator/content/development-encounters.tsv').read_text().replace(
        'npc\t470007d1\t60\t6\t1500', 'npc\t470007d1\t60\t1\t10000'))
    if '--mouse' in os.sys.argv:
        env['AC2_MOUSE_SMOKE'] = '1'
    if '--gameplay' in os.sys.argv:
        env['AC2_GAMEPLAY_SMOKE'] = '1'
    log = (directory / 'server.log').open('w', encoding='utf-8')
    server = subprocess.Popen([str(SERVER), 'serve', '--bind', '127.0.0.1:19486',
        '--account', 'local', '--data', str(directory / 'world'), '--game-data', str(DAT_DIRECTORY),
        '--content', str(content), '--initialize-ui', 'true', '--training-kit', 'false',
        '--lifestones', 'false', '--portals', 'false', '--encounters',str(rules),'--seconds', '180'],
        env=env, stdout=log, stderr=log, creationflags=0x08000000)
    peer = None
    try:
        time.sleep(1)
        assert server.poll() is None, (directory / 'server.log').read_text()
        peer = Peer()
        peer.send('connect', host='127.0.0.1', port=19486, account='local')
        peer.wait('characters')
        peer.send('create', name='Combat Input Hero', start_area=0, race=1, sex=4096, physical=[[1, 1], [2, 1]])
        assert peer.wait('created')['character']['id'] != '0'
        peer.close()
        peer = None
        result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            str(ROOT / 'Launch-AC2.ps1'), '-SkipBuild', '-NetworkSmoke', '-Server', '127.0.0.1',
            '-Port', '19486', '-Account', 'local', '-DatDirectory', str(DAT_DIRECTORY)], cwd=ROOT,
            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace',
            timeout=190, creationflags=0x08000000)
        shutil.copyfile(ROOT / 'Unreal/Saved/Logs/AC2Network-smoke.log', directory / 'client.log')
        print(result.stdout, flush=True)
        result.check_returncode()
        client = (directory / 'client.log').read_text(encoding='utf-8', errors='replace')
        assert all(f'AC2 combat input step {step} passed' in client for step in range(8))
        assert 'AC2 combat world pointer selected' in client
        assert 'AC2 network smoke PASS' in client
        assert ('AC2 native empty corpse window passed' in client or
                'AC2 native corpse loot window and item double-click passed' in client)
        if '--gameplay' in os.sys.argv:
            assert all(f'AC2 inventory input step {step} passed' in client for step in range(14))
            assert 'AC2 world props passed:' in client
            assert 'grounded NPCs=0' not in client
        if '--mouse' in os.sys.argv:
            assert all(f'AC2 mouse controls step {step} passed' in client for step in range(10))
        report = {'result': 'PASS', 'directory': str(directory), 'equipped_magic': '--gameplay' in os.sys.argv,
            'checks': ['native start/stop mouse buttons and ghosted states', 'Z toggles server and avatar stance',
                'chat focus isolation and restored attack key', 'missing-target feedback',
                'world creature mouse selection', 'server attack/hit/death animations, damage and native corpse loot window']}
        if '--gameplay' in os.sys.argv:
            report['checks'] += ['native portal hull picking', 'placed NPC foot alignment',
                'mouse double-click replaces equipped weapons', 'native layered item icons and tooltip screenshot',
                'all 78 backpack slots accessible', 'mouse drag sorts both directions without duplicate cells',
                'drag survives rendered frames, adjacent-slot moves, world drop/pickup and capture loss',
                'selection follows item identity across recreation and reordering']
        if '--mouse' in os.sys.argv:
            report['checks'] += ['DAT usage cursor and pixel hotspot', 'native server examination in the DAT panel',
                'double-click use without attacking', 'world zoom and panel scroll isolation',
                'visible default cursor and inert Alt/middle capture toggles', 'camera orbit preserves network heading',
                'native mouse-look crosshair and center inspection', 'wheel first/third-person transitions',
                'held LMB orbit, RMB steer, both-button walking and capture-loss cleanup']
        (directory / 'result.json').write_text(json.dumps(report, indent=2))
        print(json.dumps(report))
    finally:
        if peer:
            peer.close()
        if server.poll() is None:
            (directory / 'world/stop.request').write_text('stop\n')
            try:
                server.wait(timeout=8)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
        log.close()


if __name__ == '__main__':
    main()
