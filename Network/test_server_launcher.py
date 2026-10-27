"""Exercise the public batch launcher with an existing server and an isolated store."""
import socket
import subprocess
import time
import uuid

from test_network import DAT_DIRECTORY, Peer, ROOT, SERVER


def main():
    if not SERVER.is_file():
        raise RuntimeError('Build AC2E before testing its existing-binary startup path.')
    directory = ROOT / 'Saved/AC2NetworkTests' / ('launcher-' + uuid.uuid4().hex)
    directory.mkdir(parents=True)
    world = directory / 'world'
    port = 19490
    for number in (port, port + 1):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as check:
            check.bind(('127.0.0.1', number))
    log_path = directory / 'launcher.log'
    peer = None
    with log_path.open('w', encoding='utf-8') as log:
        process = subprocess.Popen([
            'cmd.exe', '/d', '/c', str(ROOT / 'start-ac2e.bat'),
            '-Port', str(port), '-DataDirectory', str(world),
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
            peer = Peer()
            peer.send('connect', host='127.0.0.1', port=port, account='local')
            peer.wait('characters')
            peer.close()
            peer = None
            # Use the same public launcher for graceful shutdown, from another cwd.
            subprocess.run([
                'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                '-File', str(ROOT / 'AC2E/Start-AC2E.ps1'), '-Stop',
                '-DataDirectory', str(world),
            ], cwd=directory, stdout=log, stderr=log, check=True, timeout=30,
                creationflags=subprocess.CREATE_NO_WINDOW)
            assert process.wait(timeout=10) == 0, log_path.read_text(errors='replace')
            print('PASS: batch startup without rebuilding, native login, and graceful shutdown; log:', log_path)
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
