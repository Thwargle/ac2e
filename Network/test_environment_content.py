"""Native portal labels and duplicate landmark snapshots over real UDP."""
import json
import os
import subprocess
import time
import uuid
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer, prepare_test_content


def main():
    run = ROOT / 'Saved/AC2NetworkTests' / ('environment-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    content = prepare_test_content(run)
    report = json.loads(content.with_suffix('.report.json').read_text())
    assert any(v['id'] == '8140af2d7223b1d8' for v in report['duplicate_landmarks']), report
    # Exercise the server's own overlap filter as well as the preparer's filter.
    with content.open('a') as f:
        f.write('81400000000ffff0\t470007de\t1f00002d\t02990109\t55.060577\t241.199\t-44.606205\t1\t0\t0\t0\t1\n')
    peer = None
    with (run / 'server.log').open('w') as log:
        server = subprocess.Popen([str(SERVER), 'serve', '--bind', '127.0.0.1:19380',
            '--account', 'local', '--data', str(run / 'world'), '--game-data', str(DAT_DIRECTORY),
            '--content', str(content), '--initialize-ui', 'true', '--training-kit', 'false',
            '--portals', 'true', '--seconds', '90'], env=dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1'),
            stdout=log, stderr=log, creationflags=0x08000000)
        try:
            time.sleep(1)
            assert server.poll() is None, (run / 'server.log').read_text()
            peer = Peer()
            peer.send('connect', host='127.0.0.1', port=19380, account='local')
            peer.wait('characters')
            peer.send('create', name='Environment Tester', race=1, sex=4096, start_area=0, physical=[[1,1],[2,1]])
            ident = peer.wait('created')['character']['id']
            peer.send('enter', id=ident)
            peer.wait('player')
            peer.wait('object', lambda e: e['id'] == ident)
            peer.send('ready')
            peer.wait('status', lambda e: e['phase'] == 'world')
            portal = next((e for e in peer.events if e.get('event') == 'object' and e.get('id') == str(0x8140000000030002)), None)
            if portal is None:
                portal = peer.wait('object', lambda e: e['id'] == str(0x8140000000030002))
            assert portal['name']['literal'] == 'Arwic Outland Portal', portal
            time.sleep(1)
            stones = {e['id'] for e in peer.events if e.get('event') == 'object' and e.get('entity') == 0x470007de
                      and e.get('placement', {}).get('cell') == 0x02990109}
            assert stones == {str(0x8140000000030001)}, stones
            result = {'native_portal_name': portal['name']['literal'], 'single_lifestone': True,
                      'merged_landmarks': len(report['duplicate_landmarks'])}
            (run / 'result.json').write_text(json.dumps(result, indent=2))
            print(json.dumps(result))
        finally:
            if peer:
                peer.close()
            if server.poll() is None:
                server.terminate()
            server.wait(timeout=10)


if __name__ == '__main__':
    main()
