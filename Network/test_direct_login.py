"""Capture the real bridge/UI logon on loopback. No external service or real credentials."""
import json
import os
import socket
import struct
import subprocess
import sys
import time
import uuid
from pathlib import Path
from test_network import ROOT, BRIDGE, DAT_DIRECTORY, Peer

sys.path.insert(0, str(ROOT / 'Tools/AC2'))
from population_probe import Reader


def check_logon(packet, account, password, version):
    sequence, flags, _, recipient, interval, size, iteration = struct.unpack_from('<IIIHHHH', packet)
    assert (sequence, flags, recipient, interval, iteration) == (0, 0x10000, 0, 0, 0)
    assert size == len(packet)-20
    r = Reader(packet[20:])
    assert r.string() == version, 'Wrong wire version'
    assert r.word() == len(r.data)-r.pos, 'Invalid authenticator size'
    assert r.word() == 0x40000002, 'Direct credential did not use native GLS auth'
    assert r.word() == 0, 'Unexpected crypto flags'
    r.word()  # Per-connection sequence.
    assert r.string() == account, 'Account capitalization changed'
    r.take(r.word())  # Crypto blob.
    extra = Reader(r.take(r.word()))
    assert extra.string() == password, 'Password was changed or replaced with a token'
    assert r.pos == len(r.data) and extra.pos == len(extra.data)


def main():
    directory = ROOT / 'Saved/AC2NetworkTests' / ('direct-login-'+uuid.uuid4().hex)
    directory.mkdir(parents=True)
    account = 'Direct_MixedCase'
    checks = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
        listener.bind(('127.0.0.1', 0))
        listener.settimeout(8)
        port = listener.getsockname()[1]
        for version in ['7148', '7160', '7230']:
            password = ' ' + uuid.uuid4().hex[:6] + '! '
            peer = Peer()
            try:
                peer.send('connect', host='127.0.0.1', port=port, account=account,
                          auth='gls_direct', password=password, version=version)
                packet, _ = listener.recvfrom(4096)
                check_logon(packet, account, password, version)
                assert password not in json.dumps(peer.events), 'Bridge exposed a password'
            finally:
                peer.close()
            checks.append('Native direct GLS logon '+version)
        if '--ui' in sys.argv:
            password = uuid.uuid4().hex[:7]+'!'
            log_path = directory / 'ui.log'
            args = [str(Path('C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe')),
                    str(ROOT/'Unreal/AC2Unreal.uproject'), '-game', '-AC2Online',
                    '-AC2Server=127.0.0.1', '-AC2Port='+str(port), '-AC2Account='+account,
                    '-AC2AccountUrl=https://unused.invalid', '-AC2DirectLoginSmoke',
                    '-AC2LoginProfiles='+str(directory/'test-profiles.json'),
                    '-AC2DatDir='+str(DAT_DIRECTORY), '-RenderOffscreen', '-unattended',
                    '-ForceRes', '-ResX=1280', '-ResY=720', '-nosound', '-nosplash', '-abslog='+str(log_path)]
            with (directory/'process.log').open('w', encoding='utf-8') as out:
                client = subprocess.Popen(args, cwd=ROOT, env=dict(os.environ, AC2_TEST_PASSWORD=password),
                                          stdout=out, stderr=out, creationflags=0x08000000)
                try:
                    listener.settimeout(.5)
                    deadline = time.monotonic()+100
                    while True:
                        try:
                            packet, _ = listener.recvfrom(4096)
                            break
                        except socket.timeout:
                            assert client.poll() is None, 'UI exited before sending login; see '+str(log_path)
                            assert time.monotonic() < deadline, 'UI did not submit login'
                    check_logon(packet, account, password, '7160')
                    assert client.wait(timeout=25) == 0, 'UI smoke failed'
                finally:
                    if client.poll() is None:
                        client.terminate(); client.wait(timeout=15)
            log = log_path.read_text(encoding='utf-8', errors='replace')
            assert 'AC2 direct login: selector, masked entry, hidden account URL and Connect passed' in log
            assert 'requesting account authentication' not in log, 'Direct mode called the web service'
            assert password not in log and password not in (directory/'process.log').read_text(encoding='utf-8',errors='replace')
            checks += ['Mouse-selected direct login and Connect', 'No HTTP request',
                       'Masked password and no credential logging', 'Non-secret retry defaults']
    password = None
    report = dict(result='PASS', checks=checks, directory=str(directory))
    (directory/'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
