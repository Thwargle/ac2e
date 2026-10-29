"""Connected foliage regression at the reported Arwic-area coordinates.

Uses a disposable server/store; the user's server and characters are untouched.
The first two samples must remain bare, as authored; the other two verify frills,
ground contact and the actual highres grass surface uploaded by the client.
"""
import json
import os
import shutil
import socket
import subprocess
import time
import uuid

from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer, prepare_test_content


def main():
    run = ROOT / 'Saved/AC2NetworkTests' / ('foliage-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    port = 19392
    for number in (port, port + 1):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as check:
            check.bind(('127.0.0.1', number))
    content = prepare_test_content(run)
    # Uses an already-built editor (-SkipBuild), with no packaging or SDK work.
    # Avoid startup SDK discovery waiting behind an unrelated project's build.
    env = dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1', AC2_FOLIAGE_SMOKE='1', UE_SKIP_UBT_SDK_SETUP='1')
    peer = None
    with (run / 'server.log').open('w', encoding='utf-8') as log:
        server = subprocess.Popen([
            str(SERVER), 'serve', '--bind', f'127.0.0.1:{port}', '--account', 'local',
            '--data', str(run / 'world'), '--game-data', str(DAT_DIRECTORY), '--content', str(content),
            '--initialize-ui', 'true', '--training-kit', 'false', '--encounters', 'none',
            '--portals', 'false', '--lifestones', 'false', '--test-map-travel', 'true', '--seconds', '200',
        ], env=env, stdout=log, stderr=log, creationflags=0x08000000)
        try:
            time.sleep(1)
            assert server.poll() is None, (run / 'server.log').read_text()
            peer = Peer()
            peer.send('connect', host='127.0.0.1', port=port, account='local')
            peer.wait('characters')
            peer.send('create', name='Foliage Tester', start_area=0, race=1, sex=4096,
                      physical=[[1, 1], [2, 1]])
            assert peer.wait('created')['character']['id'] != '0'
            peer.close()
            peer = None
            result = subprocess.run([
                'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT / 'Launch-AC2.ps1'),
                '-SkipBuild', '-NetworkSmoke', '-Server', '127.0.0.1', '-Port', str(port), '-Account', 'local',
                '-DatDirectory', str(DAT_DIRECTORY),
            ], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                errors='replace', timeout=190, creationflags=0x08000000)
            shutil.copyfile(ROOT / 'Unreal/Saved/Logs/AC2Network-smoke.log', run / 'client.log')
            print(result.stdout, flush=True)
            result.check_returncode()
            text = (run / 'client.log').read_text(encoding='utf-8', errors='replace')
            assert 'AC2 foliage network smoke PASS' in text
            for block in ('77B7', '77B5', '77B4', '77B4-slope'):
                assert f'AC2 connected foliage {block[:4]}:' in text
                shutil.copyfile(ROOT / f'Unreal/Saved/Screenshots/AC2Foliage-network-{block}.png', run / f'{block}.png')
            assert 'texture=40000008 surface=41000034 uploaded=1024x256 highres=1' in text
            (run / 'result.json').write_text(json.dumps({'result': 'PASS',
                'samples': ['77B7', '77B5', '77B4', '77B4-slope'],
                'grass_texture': '40000008', 'grass_surface': '41000034', 'grass_size': [1024, 256]}))
            print('PASS: connected authored foliage, ground contact, highres texture and empty scenes. Captures:', run)
        finally:
            if peer:
                peer.close()
            if server.poll() is None:
                (run / 'world/stop.request').write_text('stop\n')
                try:
                    server.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    server.terminate()
                    server.wait()


if __name__ == '__main__':
    main()
