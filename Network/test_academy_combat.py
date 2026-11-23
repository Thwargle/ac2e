"""Actual UDP academy target: passive approach, retaliation, death and starter loot.

Uses the retired Human academy fixture and production encounter rules; no damage
or aggression overrides and no production accounts/journals.
"""
import json, os, subprocess, time, uuid
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer
from test_gameplay import until


def main():
    run=ROOT/'Saved/AC2NetworkTests'/('academy-'+uuid.uuid4().hex);run.mkdir(parents=True)
    target=0x8140000000030000;tid=str(target)
    row=next(r for r in (ROOT/'AC2E/emulator/tests/fixtures/human-practice-world.tsv').read_text().splitlines() if r.startswith(f'{target:016x}'))
    content=run/'world.tsv';content.write_text('pyrealium-world-v3\n'+row+'\n')
    p=None
    with (run/'server.log').open('w') as log:
        server=subprocess.Popen([str(SERVER),'serve','--bind','127.0.0.1:19514','--account','local','--data',str(run/'world'),
            '--game-data',str(DAT_DIRECTORY),'--content',str(content),'--encounters',str(ROOT/'AC2E/emulator/content/development-encounters.tsv'),
            '--initialize-ui','true','--portals','false','--lifestones','false','--seconds','100'],
            env=dict(os.environ,PYREALIUM_UNREAL_LEGIONS_DATA='1',AC2_TEST_LOOT_SEED='53'),stdout=log,stderr=log,creationflags=0x08000000)
        try:
            time.sleep(1);assert server.poll() is None,(run/'server.log').read_text()
            p=Peer();p.send('connect',host='127.0.0.1',port=19514,account='local');p.wait('characters')
            p.send('create',name='Academy Tester',start_area=0,race=1,sex=4096,physical=[[1,1],[2,1]])
            p.id=p.wait('created')['character']['id'];p.send('enter',id=p.id);p.wait('player');p.send('ready')
            p.wait('status',lambda e:e['phase']=='world');until(p,0,'inventory')
            f=row.split('\t');pos=list(map(float,f[4:7]));pos[0]+=1.5
            p.send('move',cell=int(f[3],16),position=pos,heading=0,motion=[0,0,0]);until(p,0,'object',lambda e:e['id']==tid)
            start=len(p.events);time.sleep(5)
            assert not any(e.get('event')=='hit' and e.get('id')==p.id for e in p.events[start:]),'Unprovoked academy attack'
            p.send('target',id=tid);p.send('attack',id=tid)
            until(p,start,'quality',lambda e:e.get('id')==tid and e.get('key')==0x102 and 0<e['value']<60)
            p.send('combat',enabled=False)
            until(p,start,'hit',lambda e:e.get('id')==p.id,timeout=10)
            p.send('combat',enabled=True);p.send('attack',id=tid)
            until(p,start,'quality',lambda e:e.get('id')==tid and e.get('key')==0x102 and e['value']==0,timeout=25)
            p.send('combat',enabled=False);until(p,start,'combat',lambda e:not e['enabled'])
            start=len(p.events);p.send('open_container',id=tid)
            opened=until(p,start,'container_state',lambda e:e['open'])
            for iid in opened['items']:until(p,start,'object',lambda e:e['id']==iid)
            result=dict(result='PASS',passive_before_attack=True,retaliates_after_attack=True,killable=True,loot_items=len(opened['items']))
            (run/'result.json').write_text(json.dumps(result,indent=2));print('PASS',run,json.dumps(result))
        finally:
            if p:(run/'peer.json').write_text(json.dumps(p.events));p.close()
            if server.poll() is None:(run/'world/stop.request').write_text('stop\n');server.wait(timeout=10)


if __name__=='__main__':main()
