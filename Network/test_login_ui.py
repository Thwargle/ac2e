"""Real account login, recall and Unreal frontend/combat against isolated AC2E services."""
import json
import os
import socket
import subprocess
import time
import urllib.request
import uuid
from test_network import Peer, ROOT, SERVER, DAT_DIRECTORY, prepare_test_content


def main():
    directory=ROOT/'Saved/AC2NetworkTests'/('login-ui-'+uuid.uuid4().hex)
    directory.mkdir(parents=True)
    auth=directory/'accounts';world=directory/'world'
    for port,kind in [(19480,socket.SOCK_STREAM),(19482,socket.SOCK_DGRAM),(19483,socket.SOCK_DGRAM)]:
        with socket.socket(socket.AF_INET,kind) as check:check.bind(('127.0.0.1',port))
    content=prepare_test_content(directory)
    env=dict(os.environ,PYREALIUM_UNREAL_LEGIONS_DATA='1')
    if '--gameplay' in os.sys.argv:env['AC2_GAMEPLAY_SMOKE']='1'
    log=(directory/'server.log').open('w',encoding='utf-8')
    web=subprocess.Popen([str(SERVER),'account-web','--auth-data',str(auth),'--data',str(world),
        '--web-bind','127.0.0.1:19480','--public-origin','http://127.0.0.1:19480',
        '--public-host','127.0.0.1','--public-port','19482'],stdout=log,stderr=log,creationflags=0x08000000)
    game=None;peer=None;password='AC2e-'+uuid.uuid4().hex
    def post(path):
        request=urllib.request.Request('http://127.0.0.1:19480/api/'+path,
            data=json.dumps({'account':'controller_test','password':password}).encode(),
            headers={'Content-Type':'application/json'},method='POST')
        with urllib.request.urlopen(request,timeout=10) as response:return json.load(response)
    try:
        time.sleep(1);assert post('login')['created'] is True
        game=subprocess.Popen([str(SERVER),'serve','--bind','127.0.0.1:19482','--auth-data',str(auth),
            '--data',str(world),'--game-data',str(DAT_DIRECTORY),'--content',str(content),
            '--initialize-ui','true','--training-kit','false','--lifestones','true','--portals','false','--seconds','240'],
            env=env,stdout=log,stderr=log,creationflags=0x08000000)
        time.sleep(1)
        assert game.poll() is None,(directory/'server.log').read_text()
        login=post('login');peer=Peer()
        peer.send('connect',host=login['game_host'],port=login['game_port'],account=login['account'],ticket=login['ticket']);login=None
        peer.wait('characters')
        peer.send('create',name='Discard Hero',start_area=0,race=1,sex=4096,physical=[[1,1],[2,1]])
        discard=peer.wait('created')['character']['id'];assert discard!='0'
        peer.send('delete',id=discard)
        assert peer.wait('deleted')['id']==discard
        assert all(c['id']!=discard for c in peer.wait('characters')['characters'])
        peer.send('create',name='Second Hero',start_area=0,race=2,sex=4096,physical=[[1,1],[2,1]])
        second=peer.wait('created')['character']['id'];assert second!='0'
        peer.send('create',name='Controller Hero',start_area=0,race=1,sex=4096,physical=[[1,1],[2,1]])
        character=peer.wait('created')['character']['id'];assert character!='0'
        peer.send('enter',id=character);peer.wait('player')
        spawn=peer.wait('object',lambda e:e['id']==character)['placement']
        peer.send('ready');peer.wait('status',lambda e:e['phase']=='world')
        time.sleep(.6)
        stone=next(e for e in peer.events if e.get('event')=='object' and e.get('entity')==0x470007de)
        bound=list(spawn['position']);bound[0]-=1
        peer.send('move',cell=spawn['cell'],position=bound,heading=0,motion=[0,0,0])
        time.sleep(.3);peer.send('use',target='0',item=stone['id'])
        peer.wait('chat',lambda e:'bound to this lifestone' in str(e))
        time.sleep(5.2)
        peer.send('move',cell=spawn['cell'],position=spawn['position'],heading=0,motion=[0,0,0]);time.sleep(.3)
        peer.send('recall')
        recalled=peer.wait('position',lambda e:e['id']==character and e.get('force',0)>0)
        assert all(abs(a-b)<.01 for a,b in zip(recalled['position'],bound)),recalled
        # Restore the known combat test start before handing the account to the UI.
        peer.send('move',cell=spawn['cell'],position=spawn['position'],heading=0,motion=[0,0,0]);time.sleep(.3)
        peer.send('logout',id=character);peer.wait('selection');peer.close();peer=None
        env['AC2_TEST_PASSWORD']=password;password=None
        result=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'Launch-AC2.ps1'),
            '-SkipBuild','-LoginSmoke','-AccountUrl','http://127.0.0.1:19480','-Account','controller_test',
            '-DatDirectory',str(DAT_DIRECTORY)],
            cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',timeout=190,creationflags=0x08000000)
        env.pop('AC2_TEST_PASSWORD',None)
        print(result.stdout,flush=True);result.check_returncode()
        report={'result':'PASS','checks':['password login through in-client WinHTTP form','mouse hover and click routing','character names painted in native list','native carousel and 3D selection','options and credits','delete cancellation','create and cancel buttons','enter selected character before environment',
            'native character deletion','lifestone bind','native recall skill','server-driven attacks, hits and death','loot and vitals'], 'directory':str(directory)}
        if env.get('AC2_GAMEPLAY_SMOKE')=='1':report['checks']+=['server inventory equip acknowledgement','animated held weapon socket','native magic attack graph','global clothing/face appearance']
        (directory/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
    finally:
        if peer:peer.close()
        for process,path in [(game,world/'stop.request'),(web,auth/'account-stop.request')]:
            if process and process.poll() is None:
                path.write_text('stop\n')
                try:process.wait(timeout=8)
                except subprocess.TimeoutExpired:process.kill();process.wait()
        log.close()


if __name__=='__main__':main()
