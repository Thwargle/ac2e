"""Esper starter route over real UDP using production camp/NPC placements.

Isolated encounters are weakened for a quick kill; no production stats/stores
are changed. Materials come from actual butchery and the existing supplier.
"""
import json
import math
import os
import subprocess
import time
import uuid
import test_crafting as craft
from test_professions import buy, owned, execute
from test_gameplay import panel_text, until
from test_network import ROOT, SERVER, DAT_DIRECTORY
from prepare_content import prepare

WARDER, CADRE = 0x470015c2, 0x470015bd
BONE, MATERIAL, GLYPH, SWORD = 0x470015b8, 0x47001400, 0x470010a1, 0x470015b7
RECIPE, QUEST = 0x6f001f14, 0x400000d0
VERMIN = str(0x8140000000030b10)

def main():
    craft.PORT = 19544
    run=ROOT/'Saved/AC2NetworkTests'/('esper-camp-'+uuid.uuid4().hex);run.mkdir(parents=True)
    prepare(DAT_DIRECTORY,run/'prepared.tsv')
    rows=[l.split('\t') for l in (run/'prepared.tsv').read_text().splitlines() if l.startswith('8140')]
    warders=[r for r in rows if int(r[1],16)==WARDER];cadres=[r for r in rows if int(r[1],16)==CADRE]
    assert len(warders)==1 and len(cadres)==3,(warders,cadres)
    warder,mob=warders[0],cadres[0]
    referrals=json.loads((ROOT/'AC2E/emulator/content/warder-referrals-v1.json').read_text())['quests']
    contacts={q['recipient']:next(r for r in rows if int(r[1],16)==q['recipient']) for q in referrals}
    content,encounters=craft.fixture(run)
    content.write_text('pyrealium-world-v4\n'+''.join(l+'\t-\n' for l in content.read_text().splitlines()[1:])+''.join('\t'.join(r)+'\n' for r in [warder,*cadres,*contacts.values()]))
    with content.open('a') as f:
        f.write(f'{int(VERMIN):016x}\t47000f7e\t1f0004e0\t02990109\t60\t249\t-44.606205\t1\t0\t0\t0\t-\t-\n')
        for n,entity,visual in [(0,0x4700082c,0x1f00065a),(1,0x4700082d,0x1f00056a)]:
            f.write(f'{0x8140000000030B00+n:016x}\t{entity:08x}\t{visual:08x}\t02990109\t60\t240\t-44.606205\t1\t0\t0\t0\t-\t-\n')
    with encounters.open('a') as f:
        for entity in (0x47000f7e,CADRE):f.write(f'npc\t{entity:08x}\t20\t1\t1500\t3\t300000\t19\t2\n')
    peers=[];server=None
    def start(log):
        s=subprocess.Popen([str(SERVER),'serve','--bind',f'127.0.0.1:{craft.PORT}','--account','local','--data',str(run/'world'),
            '--game-data',str(DAT_DIRECTORY),'--content',str(content),'--encounters',str(encounters),'--initialize-ui','true',
            '--portals','false','--lifestones','false','--test-map-travel','true','--seconds','240'],
            env=dict(os.environ,PYREALIUM_UNREAL_LEGIONS_DATA='1'),stdout=log,stderr=log,creationflags=0x08000000)
        time.sleep(1);assert s.poll() is None,(run/'server.log').read_text();return s
    def stop():
        if server and server.poll() is None:(run/'world/stop.request').write_text('stop\n');server.wait(timeout=15)
    def connect(character=None):
        p=craft.connect('Esper Apprentice',character);peers.append(p);return p
    def leave(p):
        mark=len(p.events);p.send('logout',id=p.id);until(p,mark,'selection');p.close()
    def at(p,row,distance=.5):
        pos=list(map(float,row[4:7]));pos[0]+=distance;cell=int(row[3],16)
        dx,dy=math.floor(pos[0]/160),math.floor(pos[1]/160);pos[0]-=dx*160;pos[1]-=dy*160
        cell=(((cell>>24)+dx)<<24)|((((cell>>16)&255)+dy)<<16)|(int(pos[0]/20)*8+int(pos[1]/20)+1)
        p.send('move',cell=cell,position=pos,heading=0,motion=[0,0,0]);time.sleep(.3)
        until(p,0,'object',lambda e:e['id']==str(int(row[0],16)))
    def use(p,i,target='0'):
        time.sleep(1.6);mark=len(p.events);p.send('use',item=i,target=target);return mark
    def phase(p,mark,value,qid=QUEST):
        event=until(p,mark,'quests',lambda e:any(q['id']==qid and q['phase']==value for q in e['items']))
        return next(q for q in event['items'] if q['id']==qid)
    def peace(p):
        mark=len(p.events);p.send('combat',enabled=False);until(p,mark,'combat',lambda e:not e['enabled'])
    try:
        with (run/'server.log').open('w') as log:
            server=start(log);p=connect();character=p.id
            assert RECIPE not in until(p,0,'craft_registry')['recipes']
            craft.prepare_practice(p);buy(p,0x470010f5);buy(p,MATERIAL)
            _,reply=execute(p,RECIPE,0,{1:owned(p,MATERIAL)[0]['id']});assert reply['error']!=0
            for _ in range(12):
                _,reply=execute(p,0x6f002851,1,{2:owned(p,0x470010f5)[0]['id']})
                if reply['error']==0:break
            assert reply['error']==0;knife=owned(p,0x47002de8)[0]['id']
            craft.at(p,60,248)
            until(p,0,'object',lambda e:e['id']==VERMIN)
            mark=len(p.events);p.send('attack',id=VERMIN)
            until(p,mark,'quality',lambda e:e.get('id')==VERMIN and e['key']==0x102 and e['value']==0);peace(p)
            mark=use(p,knife,VERMIN);until(p,mark,'chat',lambda e:'Gathered ' in str(e))
            before=owned(p,MATERIAL)[0]['quantity'];assert 104<=before<=106,before
            mark=use(p,knife,VERMIN);until(p,mark,'chat',lambda e:'already been butchered' in str(e))
            at(p,warder,8);wid=str(int(warder[0],16));mark=use(p,wid);until(p,mark,'chat',lambda e:'four metres' in str(e))
            at(p,warder);mark=len(p.events);p.send('combat',enabled=True);until(p,mark,'combat',lambda e:e['enabled'])
            mark=use(p,wid);until(p,mark,'chat',lambda e:'out of combat' in str(e));peace(p)
            mark=use(p,wid);accepted=phase(p,mark,1)
            assert 86390<=accepted['remaining']<=86400,accepted
            registry=until(p,mark,'craft_registry');assert set(range(0x6f001f0f,0x6f001f15)).issubset(registry['recipes'])
            assert owned(p,MATERIAL)[0]['quantity']==before,'NPC must not grant free material'
            mark,reply=execute(p,RECIPE,0,{1:owned(p,MATERIAL)[0]['id']});assert reply['error']==0;phase(p,mark,2)
            assert owned(p,MATERIAL)[0]['quantity']==before-25;sword=owned(p,SWORD)[0]['id']
            mark=len(p.events);p.send('examine',id=sword);panel=until(p,mark,'examination');assert 'Weapon damage: 22' in panel_text(panel) and 'Required level: 6' in panel_text(panel),panel
            leave(p);stop();server=start(log);p=connect(character);restored=phase(p,0,2)
            assert restored['remaining']<accepted['remaining'];assert RECIPE in until(p,0,'craft_registry')['recipes']
            at(p,mob);mid=str(int(mob[0],16));mark=len(p.events);p.send('attack',id=mid)
            until(p,mark,'quality',lambda e:e.get('id')==mid and e['key']==0x102 and e['value']==0);peace(p)
            mark=len(p.events);p.send('open_container',id=mid);corpse=until(p,mark,'container_state',lambda e:e['id']==mid and e['open'])
            bone=until(p,mark,'object',lambda e:e.get('entity')==BONE)
            assert len([e for e in p.events[mark:] if e.get('event')=='object' and e.get('entity')==BONE])==1
            mark=len(p.events);p.send('take_item',container=mid,id=bone['id'],**{'from':corpse['items'].index(bone['id'])});phase(p,mark,3)
            until(p,mark,'object',lambda e:e.get('entity')==BONE and e.get('container')==p.id)
            leave(p);stop();server=start(log);p=connect(character);phase(p,0,3)
            before_xp=int(until(p,0,'quality',lambda e:e['key']==0x12c and e['table']==12)['value'])
            at(p,warder);mark=use(p,owned(p,SWORD)[0]['id'],wid);until(p,mark,'chat',lambda e:'Drudge Cadre Bone' in str(e));assert owned(p,BONE)
            mark=use(p,owned(p,BONE)[0]['id'],wid);finished=phase(p,mark,0)
            assert not finished['expires'] and finished['remaining']==0
            until(p,mark,'quality',lambda e:e['key']==0x12c and int(e['value'])==before_xp+320)
            until(p,mark,'object',lambda e:e.get('entity')==GLYPH and e.get('container')==p.id)
            assert RECIPE not in until(p,mark,'craft_registry')['recipes'];assert not owned(p,BONE)
            mark=use(p,wid);phase(p,mark,1,referrals[0]['id']);assert len(owned(p,GLYPH))==1
            for index,q in enumerate(referrals):
                row=contacts[q['recipient']];at(p,row);mark=use(p,str(int(row[0],16)));phase(p,mark,0,q['id'])
                if index+1<len(referrals):phase(p,mark,1,referrals[index+1]['id'])
            mark=use(p,str(int(contacts[referrals[-1]['recipient']][0],16)))
            until(p,mark,'chat',lambda e:'level 10' in str(e))
            _,reply=execute(p,RECIPE,0,{1:owned(p,MATERIAL)[0]['id']});assert reply['error']!=0
            leave(p);stop();server=start(log);p=connect(character);phase(p,0,0)
            assert len(owned(p,GLYPH))==1 and not owned(p,BONE)
            assert int(until(p,0,'quality',lambda e:e['key']==0x12c and e['table']==12)['value'])==before_xp+350
            for q in referrals:phase(p,0,0,q['id'])
            assert RECIPE not in until(p,0,'craft_registry')['recipes'];leave(p)
        report={'result':'PASS','checks':['one production Warder and three captured Cadre placements','vermin butchery yields Bone once per corpse',
            'recipe entitlement, range/combat/wrong item rejection','25 Bone consumed and native weapon inspected','real Cadre kill and one owned quest bone',
            '320 XP, glyph and recipe removal; Cragstone, Corporal and Scout referrals with one 30 XP payout','three server restarts preserve deadline, quest, material and rewards']}
        (run/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report));print('Evidence:',run)
    finally:
        for n,p in enumerate(peers):(run/f'peer-{n}.json').write_text(json.dumps(p.events));p.close()
        stop()

if __name__=='__main__':main()
