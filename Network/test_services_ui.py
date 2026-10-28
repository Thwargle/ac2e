"""Rendered DAT shop pointer and door animation/collision acceptance."""
import os
import shutil
import subprocess
import time
import uuid
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer
from test_services import fixture


def main():
    run=ROOT/'Saved/AC2NetworkTests'/('services-ui-'+uuid.uuid4().hex);run.mkdir(parents=True)
    content=fixture(run,isolated=True);peer=None
    env=dict(os.environ,PYREALIUM_UNREAL_LEGIONS_DATA='1',AC2_SHOP_SMOKE='1')
    with (run/'server.log').open('w') as log:
        server=subprocess.Popen([str(SERVER),'serve','--bind','127.0.0.1:19386','--account','local',
            '--data',str(run/'world'),'--game-data',str(DAT_DIRECTORY),'--content',str(content),
            '--initialize-ui','true','--training-kit','false','--encounters','none','--portals','false',
            '--lifestones','false','--seconds','180'],env=env,stdout=log,stderr=log,creationflags=0x08000000)
        try:
            time.sleep(1);assert server.poll() is None,(run/'server.log').read_text()
            peer=Peer();peer.send('connect',host='127.0.0.1',port=19386,account='local');peer.wait('characters')
            peer.send('create',name='Service Render Tester',start_area=0,race=1,sex=4096,physical=[[1,1],[2,1]])
            assert peer.wait('created')['character']['id']!='0';peer.close();peer=None
            result=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'Launch-AC2.ps1'),
                '-SkipBuild','-NetworkSmoke','-Server','127.0.0.1','-Port','19386','-Account','local','-DatDirectory',str(DAT_DIRECTORY)],
                cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',timeout=190,creationflags=0x08000000)
            shutil.copyfile(ROOT/'Unreal/Saved/Logs/AC2Network-smoke.log',run/'client.log')
            print(result.stdout);result.check_returncode()
            assert 'AC2 services render smoke PASS' in (run/'client.log').read_text(encoding='utf-8',errors='replace')
            print('PASS: native shop mouse sale/purchase/close; door animation and collision. Logs:',run)
        finally:
            if peer:peer.close()
            if server.poll() is None:
                (run/'world/stop.request').write_text('stop\n')
                try:server.wait(timeout=8)
                except subprocess.TimeoutExpired:server.terminate();server.wait()


if __name__=='__main__':main()
