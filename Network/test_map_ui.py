"""Render the native radar against an isolated AC2E server and exercise click-to-target/combat."""
import os
import socket
import subprocess
import time
import uuid

from test_network import Peer, ROOT, SERVER, DAT_DIRECTORY, prepare_test_content


def main():
    data = ROOT / 'Saved/AC2NetworkTests' / ('map-' + uuid.uuid4().hex)
    data.mkdir(parents=True)
    content = prepare_test_content(data)
    port = 19374
    # Do not attach this destructive gameplay test to a user's running server.
    for number in (port, port + 1):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as check:
            check.bind(('127.0.0.1', number))
    env = dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1')
    with (data / 'server.log').open('w', encoding='utf-8') as log:
        server = subprocess.Popen([
            str(SERVER), 'serve', '--bind', f'127.0.0.1:{port}', '--account', 'local',
            '--data', str(data / 'world'), '--game-data', str(DAT_DIRECTORY),
            '--content', str(content),
            '--initialize-ui', 'true', '--training-kit', 'false', '--lifestones', 'false',
            '--portals', 'false', '--seconds', '180',
        ], env=env, stdout=log, stderr=log, creationflags=0x08000000)
        peer = None
        try:
            time.sleep(1)
            assert server.poll() is None, 'Test server failed; see ' + str(data / 'server.log')
            peer = Peer()
            peer.send('connect', host='127.0.0.1', port=port, account='local')
            peer.wait('characters')
            peer.send('create', name='Map Network Test', start_area=0, race=1, sex=4096,
                      physical=[[1, 1], [2, 1]])
            peer.wait('created')
            peer.close()
            peer = None
            time.sleep(1)
            result = subprocess.run([
                'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT / 'Launch-AC2.ps1'),
                '-SkipBuild', '-NetworkSmoke', '-Online', '-Dx11', '-Server', '127.0.0.1',
                '-Port', str(port), '-Account', 'local',
                '-DatDirectory', str(DAT_DIRECTORY),
            ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                errors='replace', timeout=190, creationflags=0x08000000)
            print(result.stdout, flush=True)
            result.check_returncode()
            print('PASS: native radar selected a server mob; combat, loot and vitals acknowledged. Store:', data)
        finally:
            if peer:
                peer.close()
            if server.poll() is None:
                try:
                    subprocess.run([str(SERVER), 'stop', '--data', str(data / 'world'), '--seconds', '3'],
                                   stdout=log, stderr=log, timeout=8, creationflags=0x08000000)
                except subprocess.TimeoutExpired:
                    pass
                try:
                    server.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()


if __name__ == '__main__':
    main()
