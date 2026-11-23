"""Notebook and Captain chain through real native UDP, using production placements.

Only this disposable test store grants XP; its bandit has reduced combat stats.
"""
import json
import math
import os
import subprocess
import time
import uuid
import test_crafting as craft
from test_professions import owned
from test_gameplay import until
from test_network import ROOT, SERVER, DAT_DIRECTORY
from prepare_content import prepare

SCOUT, BANDIT, CAPTAIN = 0x470015cc, 0x470015be, 0x470015c8
NOTEBOOK, QUEST, REFERRAL = 0x470015b1, 0x400000c4, 0x400000c5
REWARDS = (0x4700156d, 0x470010b9, 0x470010a2)

def main():
    craft.PORT = 19545
    run = ROOT/'Saved/AC2NetworkTests'/('warder-notebook-'+uuid.uuid4().hex)
    run.mkdir(parents=True)
    prepare(DAT_DIRECTORY, run/'prepared.tsv')
    rows = [l.split('\t') for l in (run/'prepared.tsv').read_text().splitlines() if l.startswith('8140')]
    evidence = json.loads((ROOT/'AC2E/emulator/content/warder-notebook-v1.json').read_text())
    contacts = {int(p['row'].split('\t')[1],16):p['row'].split('\t') for p in evidence['placements']}
    for row in contacts.values(): assert row in rows, row
    content, encounters = craft.fixture(run)
    content.write_text('pyrealium-world-v4\n'+''.join(l+'\t-\n' for l in content.read_text().splitlines()[1:])
                       +''.join('\t'.join(r)+'\n' for r in contacts.values()))
    with encounters.open('a') as out: out.write(f'npc\t{BANDIT:08x}\t20\t1\t1500\t3\t300000\t31\t2\n')
    peers, server = [], None
    def start(log):
        s=subprocess.Popen([str(SERVER),'serve','--bind',f'127.0.0.1:{craft.PORT}','--account','local','--data',str(run/'world'),
            '--game-data',str(DAT_DIRECTORY),'--content',str(content),'--encounters',str(encounters),'--initialize-ui','true',
            '--portals','false','--lifestones','false','--test-map-travel','true','--seconds','240'],
            env=dict(os.environ,PYREALIUM_UNREAL_LEGIONS_DATA='1'),stdout=log,stderr=log,creationflags=0x08000000)
        time.sleep(1);assert s.poll() is None,(run/'server.log').read_text();return s
    def stop():
        if server and server.poll() is None:
            (run/'world/stop.request').write_text('stop\n');server.wait(timeout=15)
    def connect(character=None):
        p=craft.connect('Notebook Apprentice',character);peers.append(p);return p
    def leave(p):
        mark=len(p.events);p.send('logout',id=p.id);until(p,mark,'selection');p.close()
    def at(p,entity,distance=.5):
        row=contacts[entity];pos=list(map(float,row[4:7]));pos[0]+=distance;cell=int(row[3],16)
        if cell&65535<256:
            dx,dy=math.floor(pos[0]/160),math.floor(pos[1]/160);pos[0]-=dx*160;pos[1]-=dy*160
            cell=(((cell>>24)+dx)<<24)|((((cell>>16)&255)+dy)<<16)|(int(pos[0]/20)*8+int(pos[1]/20)+1)
        p.send('move',cell=cell,position=pos,heading=0,motion=[0,0,0]);time.sleep(.3)
        iid=str(int(row[0],16));until(p,0,'object',lambda e:e['id']==iid);return iid
    def use(p,i,target='0'):
        time.sleep(1.6);mark=len(p.events);p.send('use',item=i,target=target);return mark
    def phase(p,mark,value,qid=QUEST):
        event=until(p,mark,'quests',lambda e:any(q['id']==qid and q['phase']==value for q in e['items']))
        return next(q for q in event['items'] if q['id']==qid)
    def xp(p):
        return int(next(e['value'] for e in reversed(p.events) if e.get('event')=='quality' and e.get('id') is None and e['key']==0x12c))
    try:
        with (run/'server.log').open('w') as log:
            server=start(log);p=connect();character=p.id
            sid=at(p,SCOUT);mark=use(p,sid);until(p,mark,'chat',lambda e:'level 10' in str(e))
            assert not any(q['id']==QUEST for e in p.events if e.get('event')=='quests' for q in e['items'])
            leave(p);stop()
            subprocess.run([str(SERVER),'player-state','--account','local','--data',str(run/'world'),
                '--character-id',character,'--event','x,100000'],check=True,stdout=log,stderr=log,creationflags=0x08000000)
            server=start(log);p=connect(character);sid=at(p,SCOUT,8)
            mark=use(p,sid);until(p,mark,'chat',lambda e:'four metres' in str(e))
            at(p,SCOUT);mark=use(p,sid);accepted=phase(p,mark,1)
            assert accepted['level']==10 and 86390<=accepted['remaining']<=86400,accepted
            leave(p);stop();server=start(log);p=connect(character);restored=phase(p,0,1)
            assert restored['remaining']<accepted['remaining']
            mid=at(p,BANDIT);mark=len(p.events);p.send('attack',id=mid)
            until(p,mark,'quality',lambda e:e.get('id')==mid and e['key']==0x102 and e['value']==0);phase(p,mark,2)
            mark=len(p.events);p.send('combat',enabled=False);until(p,mark,'combat',lambda e:not e['enabled'])
            mark=len(p.events);p.send('open_container',id=mid)
            corpse=until(p,mark,'container_state',lambda e:e['id']==mid and e['open'])
            notebook=until(p,mark,'object',lambda e:e.get('entity')==NOTEBOOK)
            mark=len(p.events);p.send('take_item',container=mid,id=notebook['id'],**{'from':corpse['items'].index(notebook['id'])})
            phase(p,mark,3);until(p,mark,'object',lambda e:e.get('entity')==NOTEBOOK and e.get('container')==p.id)
            leave(p);stop();server=start(log);p=connect(character);phase(p,0,3);before=xp(p)
            sid=at(p,SCOUT);mark=use(p,owned(p,NOTEBOOK)[0]['id'],sid);finished=phase(p,mark,0)
            phase(p,mark,1,REFERRAL)
            assert not finished['expires'] and finished['remaining']==0
            until(p,mark,'quality',lambda e:e['key']==0x12c and int(e['value'])==before+800)
            for definition in REWARDS:until(p,mark,'object',lambda e:e.get('entity')==definition and e.get('container')==p.id)
            assert not owned(p,NOTEBOOK)
            mark=use(p,sid);until(p,mark,'chat',lambda e:'20 hours' in str(e));assert xp(p)==before+800
            cid=at(p,CAPTAIN);mark=use(p,cid);phase(p,mark,0,REFERRAL)
            until(p,mark,'quality',lambda e:e['key']==0x12c and int(e['value'])==before+3300)
            mark=use(p,cid);until(p,mark,'chat',lambda e:'introduction is complete' in str(e));assert xp(p)==before+3300
            leave(p);stop();server=start(log);p=connect(character);phase(p,0,0);phase(p,0,0,REFERRAL)
            for definition in REWARDS:assert len(owned(p,definition))==1
            assert xp(p)==before+3300;leave(p)
        report={'result':'PASS','checks':['production Scout, Bandit and interior Captain placements',
            'level/range gates and persistent 24h deadline','kill and corpse pickup advance separate native phases',
            'notebook hand-in grants three DAT items and 800 XP once','48h Captain referral completes for 2500 XP once',
            'four server restarts preserve quest stages and rewards']}
        (run/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report));print(run)
    finally:
        for i,p in enumerate(peers):(run/f'peer-{i}.json').write_text(json.dumps(p.events));p.close()
        stop()

if __name__=='__main__':main()
