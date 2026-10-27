"""Exercise normal/repeated startup, lock ownership, native login, and shutdown."""
import os
import socket
import subprocess
import time
import uuid
import json
import urllib.request

from test_network import DAT_DIRECTORY, Peer, ROOT, SERVER


def main():
    if not SERVER.is_file():
        raise RuntimeError('Build AC2E before testing its existing-binary startup path.')
    directory = ROOT / 'Saved/AC2NetworkTests' / ('launcher-' + uuid.uuid4().hex)
    directory.mkdir(parents=True)
    world = directory / 'world store'
    port = 19490
    account_port = 19492
    # Reproduce the user's crashed-launch marker without touching their store.
    exited=subprocess.Popen(['cmd.exe','/d','/c','exit','0'],creationflags=subprocess.CREATE_NO_WINDOW)
    exited.wait()
    world.mkdir()
    (world/'server.lock').write_text(str(exited.pid)+'\n')
    (directory/'accounts').mkdir()
    (directory/'accounts/accounts.lock').write_text(str(exited.pid)+'\n')
    for number in (port, port + 1):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as check:
            check.bind(('127.0.0.1', number))
    log_path = directory / 'launcher.log'
    peer = None
    with log_path.open('w', encoding='utf-8') as log:
        process = subprocess.Popen([
            'cmd.exe', '/d', '/c', str(ROOT / 'start-ac2e.bat'),
            '-Port', str(port), '-DataDirectory', str(world),
            '-AccountPort', str(account_port),
            '-DatDirectory', str(DAT_DIRECTORY),
        ], cwd=directory, stdin=subprocess.DEVNULL, stdout=log, stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                output = log_path.read_text(encoding='utf-8', errors='replace')
                assert process.poll() is None, output
                # The launcher prepares content before starting the native process.
                if 'AC2E: UDP' in output:
                    break
                time.sleep(.1)
            else:
                raise AssertionError('Launcher did not start the server: ' + output)
            assert 'AC2E source offer:' not in output, 'Normal startup unexpectedly rebuilt the server.'
            assert 'Recovered stale server.lock' in output and 'Recovered stale accounts.lock' in output,output
            peer = Peer()
            request=urllib.request.Request(f'http://127.0.0.1:{account_port}/api/login',
                data=json.dumps({'account':'launcher_test','password':uuid.uuid4().hex}).encode(),
                headers={'Content-Type':'application/json'},method='POST')
            with urllib.request.urlopen(request,timeout=10) as response:login=json.load(response)
            assert login['created'] is True
            peer.send('connect', host=login['game_host'], port=login['game_port'], account=login['account'],ticket=login['ticket'])
            login=None
            peer.wait('characters')
            peer.close()
            peer = None
            lock = world / 'server.lock'
            lock_owner = lock.read_text()
            content = world / 'legions-world.tsv'
            content_time = content.stat().st_mtime_ns
            repeated = subprocess.run([
                'cmd.exe', '/d', '/c', str(ROOT / 'start-ac2e.bat'),
                '-Port', str(port), '-DataDirectory', str(world),
                '-DatDirectory', str(DAT_DIRECTORY),
            ], cwd=directory, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
            assert repeated.returncode == 0, repeated.stdout + repeated.stderr
            assert 'already running' in repeated.stdout, repeated.stdout
            assert lock.read_text() == lock_owner
            assert content.stat().st_mtime_ns == content_time, 'Repeated startup rewrote live content.'
            rebuild = subprocess.run([
                'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                '-File', str(ROOT / 'AC2E/Start-AC2E.ps1'), '-Rebuild',
                '-DataDirectory', str(world),
            ], cwd=directory, capture_output=True, text=True, timeout=15,
                creationflags=subprocess.CREATE_NO_WINDOW)
            assert rebuild.returncode != 0 and 'before rebuilding' in rebuild.stderr, rebuild
            # A recycled PID (here this Python process) must never be accepted
            # as a server owner, nor authorize removal of the store's lock.
            other_store = directory / 'unverified store'
            other_store.mkdir()
            other_lock = other_store / 'server.lock'
            other_lock.write_text(str(os.getpid()) + '\n')
            rejected = subprocess.run([
                'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                '-File', str(ROOT / 'AC2E/Start-AC2E.ps1'),
                '-DataDirectory', str(other_store),
            ], cwd=directory, capture_output=True, text=True, timeout=15,
                creationflags=subprocess.CREATE_NO_WINDOW)
            assert rejected.returncode != 0 and 'cannot be confirmed' in rejected.stderr, rejected
            assert other_lock.read_text().strip() == str(os.getpid())
            # Use the same public launcher for graceful shutdown, from another cwd.
            subprocess.run([
                'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                '-File', str(ROOT / 'AC2E/Start-AC2E.ps1'), '-Stop',
                '-DataDirectory', str(world),
            ], cwd=directory, stdout=log, stderr=log, check=True, timeout=30,
                creationflags=subprocess.CREATE_NO_WINDOW)
            assert process.wait(timeout=10) == 0, log_path.read_text(errors='replace')
            assert not lock.exists(), 'Graceful shutdown did not release the store lock.'
            assert not (directory/'accounts/accounts.lock').exists(), 'Owned account service was not stopped.'
            print('PASS: startup, native login, repeated start, live rebuild rejection, '
                  'unverified lock preservation, and graceful shutdown; log:', log_path)
        finally:
            if peer:
                peer.close()
            if process.poll() is None:
                world.mkdir(parents=True, exist_ok=True)
                (world / 'stop.request').write_text('stop\n')
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    main()
