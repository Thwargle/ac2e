"""Rendered native skill/quest panels and draggable window acceptance."""
import os
import shutil
import subprocess
import time
import uuid
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer
from test_services import fixture


def main():
    run=ROOT/'Saved/AC2NetworkTests'/('journal-ui-'+uuid.uuid4().hex);run.mkdir(parents=True)
    content=fixture(run,isolated=True);peer=None
    env=dict(os.environ,PYREALIUM_UNREAL_LEGIONS_DATA='1',AC2_JOURNAL_SMOKE='1')
    with (run/'server.log').open('w') as log:
        args=[str(SERVER),'serve','--bind','127.0.0.1:19388','--account','local',
            '--data',str(run/'world'),'--game-data',str(DAT_DIRECTORY),'--content',str(content),
            '--initialize-ui','true','--training-kit','false','--encounters','none','--portals','false',
            '--lifestones','false','--seconds','180']
        server=subprocess.Popen(args,env=env,stdout=log,stderr=log,creationflags=0x08000000)
        try:
            time.sleep(1);assert server.poll() is None,(run/'server.log').read_text()
            peer=Peer();peer.send('connect',host='127.0.0.1',port=19388,account='local');peer.wait('characters')
            peer.send('create',name='Journal Render Tester',start_area=0,race=1,sex=4096,physical=[[1,1],[2,1]])
            character=peer.wait('created')['character']['id'];assert character!='0'
            peer.send('enter',id=character);peer.wait('player');peer.send('ready');peer.wait('status',lambda e:e['phase']=='world')
            peer.wait('inventory');peer.send('logout',id=character);peer.wait('selection');peer.close();peer=None
            (run/'world/stop.request').write_text('stop\n');server.wait(timeout=10)
            # Test-only progression, written while this isolated store is closed.
            # Human magic: Mind Lash -> Robustify -> Ease Suffering (selected in UI).
            for command,event in [('player-state','x,10000'),('skill-state','t,59')]:
                subprocess.run([str(SERVER),command,'--account','local','--data',str(run/'world'),
                    '--character-id',character,'--event',event],check=True,env=env,stdout=log,stderr=log,creationflags=0x08000000)
            server=subprocess.Popen(args,env=env,stdout=log,stderr=log,creationflags=0x08000000)
            time.sleep(1);assert server.poll() is None,(run/'server.log').read_text()
            result=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'Launch-AC2.ps1'),
                '-SkipBuild','-NetworkSmoke','-Server','127.0.0.1','-Port','19388','-Account','local','-DatDirectory',str(DAT_DIRECTORY)],
                cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',timeout=190,creationflags=0x08000000)
            shutil.copyfile(ROOT/'Unreal/Saved/Logs/AC2Network-smoke.log',run/'client.log')
            print(result.stdout);result.check_returncode()
            assert 'AC2 journal render smoke PASS' in (run/'client.log').read_text(encoding='utf-8',errors='replace')
            assert 'Cannot decode native skills' not in (run/'client.log').read_text(encoding='utf-8',errors='replace')
            for shot in (ROOT/'Unreal/Saved/Screenshots').glob('AC2Journal-*.png'):shutil.copyfile(shot,run/shot.name)
            print('PASS: native skill training, advancement, untraining/cancellation, quest tabs and window drag. Logs:',run)
        finally:
            if peer:peer.close()
            if server.poll() is None:
                (run/'world/stop.request').write_text('stop\n')
                try:server.wait(timeout=8)
                except subprocess.TimeoutExpired:server.terminate();server.wait()


if __name__=='__main__':main()
