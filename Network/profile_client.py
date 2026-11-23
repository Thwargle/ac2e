"""Measure stationary rendered online frames in a disposable local store.

7EBA (80,80,181.5) is a verified native terrain arrival. No operator accounts,
characters, source placements or rendering quality settings are modified.
"""
import os
import subprocess
import time
import uuid
from test_network import ROOT, DAT_DIRECTORY, SERVER, Peer
from test_gameplay import until
from prepare_content import prepare


def main():
    run=ROOT/'Saved/AC2NetworkTests'/('performance-'+uuid.uuid4().hex)
    run.mkdir(parents=True)
    report=run/'performance.txt'
    content=run/'world.tsv'
    prepare(DAT_DIRECTORY,content)
    env=dict(os.environ,PYREALIUM_UNREAL_LEGIONS_DATA='1')
    peer=None
    client=None
    with (run/'server.log').open('w') as log:
        server=subprocess.Popen([str(SERVER),'serve','--bind','127.0.0.1:19486',
            '--account','local','--data',str(run/'world'),'--game-data',str(DAT_DIRECTORY),
            '--content',str(content),'--initialize-ui','true','--encounters','world','--lifestones','true','--portals','true','--seconds','240'],
            env=env,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            time.sleep(1)
            assert server.poll() is None,(run/'server.log').read_text()
            peer=Peer()
            peer.send('connect',host='127.0.0.1',port=19486,account='local')
            peer.wait('characters')
            peer.send('create',name='Frame Scout',start_area=0,race=1,sex=4096,physical=[[1,1],[2,1]])
            character=peer.wait('created')['character']['id']
            peer.send('enter',id=character);peer.wait('player')
            peer.send('ready');peer.wait('status',lambda e:e['phase']=='world')
            peer.send('move',cell=0x7eba0025,position=[80,80,181.5],heading=0,motion=[0,0,0])
            time.sleep(.3)
            mark=len(peer.events);peer.send('logout',id=character);until(peer,mark,'selection')
            peer.close();peer=None
            editor=os.environ.get('AC2_EDITOR',
                'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe')
            args=[editor,str(ROOT/'Unreal/AC2Unreal.uproject'),'-game','-AC2Performance',
                '-AC2Online','-AC2NetworkSmoke','-AC2Server=127.0.0.1','-AC2Port=19486',
                '-AC2Account=local','-AC2Block=7EBA','-AC2Radius=2','-AC2Hour=12',
                '-AC2DatDir='+str(DAT_DIRECTORY),'-RenderOffscreen','-unattended',
                '-ForceRes','-ResX=1600','-ResY=900','-novsync','-nosplash',
                '-abslog='+str(run/'client.log'),'-AC2PerformanceReport='+str(report)]
            client=subprocess.Popen(args,cwd=ROOT,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
            assert client.wait(timeout=200)==0,run/'client.log'
            assert report.is_file(),run/'client.log'
            print(report.read_text(encoding='utf-8-sig'))
            print('Report:',report)
        finally:
            if peer:peer.close()
            if client and client.poll() is None:
                client.terminate();client.wait(timeout=10)
            if server.poll() is None:
                (run/'world/stop.request').write_text('stop\n')
                try:server.wait(timeout=10)
                except subprocess.TimeoutExpired:server.terminate();server.wait(timeout=10)


if __name__=='__main__':
    main()
