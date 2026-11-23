"""Kehan starter quest over real UDP, production NPCs and native quarry location.

The isolated training fixture supplies earned buying money; all quest items come
from actual conversations/harvesting. No production character store is touched.
"""
import json
import math
import os
import subprocess
import time
import uuid
import test_crafting as craft
from test_professions import buy, owned, execute
from test_gameplay import until
from test_network import ROOT, SERVER, DAT_DIRECTORY
from prepare_content import prepare

MILLER, WARDER, WINDMILL = 0x470015c4, 0x470015c2, 0x470015ce
MAP, GEAR, GLYPH, STONE, HAMMER = 0x47002fa8, 0x470015b0, 0x470010a0, 0x470010f4, 0x47002dec
QUEST, REFERRAL = 0x400000c7, 0x400000cb
QUEST_POINTS, BASE_VIGOR, MAX_VIGOR = 0x7ac20003, 0x101, 0x108
RENEWED_FOCUS = 0x6f00022a

def main():
    craft.PORT = 19539
    run=ROOT/'Saved/AC2NetworkTests'/('windmill-'+uuid.uuid4().hex);run.mkdir(parents=True)
    prepare(DAT_DIRECTORY,run/'prepared.tsv')
    rows=[l.split('\t') for l in (run/'prepared.tsv').read_text().splitlines() if l.startswith('8140')]
    actors={}
    for did in (MILLER,WARDER,WINDMILL):
        matches=[r for r in rows if int(r[1],16)==did]
        assert len(matches)==1,(hex(did),matches)
        actors[did]=matches[0]
    content,encounters=craft.fixture(run)
    content.write_text('pyrealium-world-v4\n'+''.join(l+'\t-\n' for l in content.read_text().splitlines()[1:])+''.join('\t'.join(r)+'\n' for r in actors.values()))
    with content.open('a') as f:
        for n,entity,visual in [(0,0x4700082c,0x1f00065a),(1,0x4700082d,0x1f00056a)]:
            f.write(f'{0x8140000000030B00+n:016x}\t{entity:08x}\t{visual:08x}\t02990109\t60\t240\t-44.606205\t1\t0\t0\t0\t-\t-\n')
    peers=[];server=None
    def start(log):
        s=subprocess.Popen([str(SERVER),'serve','--bind',f'127.0.0.1:{craft.PORT}','--account','local',
            '--data',str(run/'world'),'--game-data',str(DAT_DIRECTORY),'--content',str(content),
            '--encounters',str(encounters),'--initialize-ui','true','--portals','false','--lifestones','false','--test-map-travel','true','--seconds','240'],
            env=dict(os.environ,PYREALIUM_UNREAL_LEGIONS_DATA='1'),stdout=log,stderr=log,creationflags=0x08000000)
        time.sleep(1);assert s.poll() is None,(run/'server.log').read_text();return s
    def stop():
        if server and server.poll() is None:
            (run/'world/stop.request').write_text('stop\n');server.wait(timeout=15)
    def connect(character=None):
        p=craft.connect('Kehan Apprentice',character);peers.append(p);return p
    def leave(p):
        mark=len(p.events);p.send('logout',id=p.id);until(p,mark,'selection');p.close()
    def at(p,did,distance=.5):
        row=actors[did];pos=list(map(float,row[4:7]));pos[0]+=distance
        cell=int(row[3],16);dx,dy=math.floor(pos[0]/160),math.floor(pos[1]/160)
        pos[0]-=dx*160;pos[1]-=dy*160
        cell=(((cell>>24)+dx)<<24)|((((cell>>16)&255)+dy)<<16)|(int(pos[0]/20)*8+int(pos[1]/20)+1)
        p.send('move',cell=cell,position=pos,heading=0,motion=[0,0,0]);time.sleep(.3)
        until(p,0,'object',lambda e:e['id']==str(int(row[0],16)))
    def use(p,i,target='0'):
        time.sleep(1.6);mark=len(p.events);p.send('use',item=i,target=target);return mark
    def npc(p,did):return use(p,str(int(actors[did][0],16)))
    def phase(p,mark,value,qid=QUEST):
        event=until(p,mark,'quests',lambda e:any(q['id']==qid and q['phase']==value for q in e['items']))
        return next(q for q in event['items'] if q['id']==qid and q['phase']==value)
    def focus(p, mark):
        state = until(p, mark, 'status_effects', lambda e: any(
            effect['did'] == RENEWED_FOCUS for effect in e['effects']))
        buffs = [effect for effect in state['effects'] if effect['did'] == RENEWED_FOCUS]
        assert len(buffs) == 1 and buffs[0]['duration'] == 10800 and buffs[0]['flags'] & 2, buffs
        base = next(int(e['value']) for e in reversed(p.events)
                    if e.get('event') == 'quality' and e['key'] == BASE_VIGOR and e['table'] == 4
                    and e.get('id') is None)
        until(p, mark, 'quality', lambda e: e['key'] == MAX_VIGOR and e.get('id') is None
              and int(e['value']) == base + 40)
        return buffs[0]['cast']
    try:
        with (run/'server.log').open('w') as log:
            server=start(log);p=connect();character=p.id
            craft.prepare_practice(p)
            buy(p,0x470010f3)
            for _ in range(7):
                _,reply=execute(p,0x6f002855,1,{2:owned(p,0x470010f3)[0]['id']})
                if reply['error']==0:break
            assert reply['error']==0,'Rock Hammer crafting failed seven times'
            hammer=owned(p,HAMMER)[0]['id']
            at(p,MILLER,8);mark=npc(p,MILLER);until(p,mark,'chat',lambda e:'four metres' in str(e))
            at(p,MILLER);mark=len(p.events);p.send('combat',enabled=True);until(p,mark,'combat',lambda e:e['enabled'])
            mark=npc(p,MILLER);until(p,mark,'chat',lambda e:'out of combat' in str(e))
            mark=len(p.events);p.send('combat',enabled=False);until(p,mark,'combat',lambda e:not e['enabled'])
            mark=npc(p,MILLER);accepted=phase(p,mark,1);survey=owned(p,MAP)[0]
            assert accepted['expires'] and 86390<=accepted['remaining']<=86400,accepted
            mark=npc(p,MILLER);until(p,mark,'chat',lambda e:'Map south of Kehan' in str(e));assert len(owned(p,MAP))==1
            mark=use(p,survey['id']);until(p,mark,'chat',lambda e:'Survey site:' in str(e))
            assert not any(e.get('event')=='inventory_remove' and e.get('id')==survey['id'] for e in p.events[mark:])
            native=json.loads((ROOT/'AC2E/emulator/content/windmill-v1.json').read_text())['map_location']
            cell=int(native[0]);land=[((cell>>24)*160+native[1])/20,(((cell>>16)&255)*160+native[2])/20]
            mark=len(p.events);p.send('chat',text=f'@maptravel {land[0]} {land[1]}',channel='say')
            until(p,mark,'position',lambda e:e['id']==p.id);time.sleep(2)
            mark=use(p,survey['id']);phase(p,mark,2)
            node=str(0x8190000000000000|(int(survey['id'])&0xffffffffffff))
            until(p,mark,'object',lambda e:e['id']==node)
            mark=use(p,hammer,node);until(p,mark,'chat',lambda e:'Gathered ' in str(e))
            stone=owned(p,STONE)[0];amount=stone['quantity'];assert 21<=amount<=29
            leave(p);stop();server=start(log);p=connect(character);restored=phase(p,0,2)
            assert restored['expires'] and 0<restored['remaining']<accepted['remaining']-2,restored
            until(p,0,'object',lambda e:e['id']==node)
            at(p,MILLER);mark=npc(p,MILLER);until(p,mark,'chat',lambda e:'Harvest the quarry' in str(e))
            assert owned(p,STONE)[0]['quantity']==amount
            mark=use(p,owned(p,STONE)[0]['id'],str(int(actors[MILLER][0],16)));phase(p,mark,3)
            assert owned(p,STONE)[0]['quantity']==amount-1
            gear=owned(p,GEAR)[0]
            mark=use(p,gear['id'],str(int(actors[MILLER][0],16)));until(p,mark,'chat',lambda e:'not waiting for Stone' in str(e))
            at(p,WINDMILL);mark=use(p,gear['id'],str(int(actors[WINDMILL][0],16)));phase(p,mark,4)
            assert not owned(p,GEAR)
            leave(p);stop();server=start(log);p=connect(character);phase(p,0,4)
            before_xp=int(until(p,0,'quality',lambda e:e['key']==0x12c and e['table']==12)['value'])
            before_points=int(until(p,0,'quality',lambda e:e['key']==QUEST_POINTS)['value'])
            at(p,MILLER);mark=npc(p,MILLER);finished=phase(p,mark,0);phase(p,mark,1,REFERRAL)
            assert not finished['expires'] and finished['remaining']==0,finished
            until(p,mark,'quality',lambda e:e['key']==0x12c and int(e['value'])==before_xp+200)
            until(p,mark,'quality',lambda e:e['key']==QUEST_POINTS and int(e['value'])==before_points+150)
            cast=focus(p,mark)
            until(p,mark,'object',lambda e:e.get('entity')==GLYPH and e.get('container')==p.id)
            mark=npc(p,MILLER);until(p,mark,'chat',lambda e:'20 hours' in str(e));assert len(owned(p,GLYPH))==1
            at(p,WARDER);mark=npc(p,WARDER);phase(p,mark,0,REFERRAL)
            leave(p);stop();server=start(log);p=connect(character);phase(p,0,0);phase(p,0,0,REFERRAL)
            assert int(until(p,0,'quality',lambda e:e['key']==0x12c and e['table']==12)['value'])==before_xp+200
            assert int(until(p,0,'quality',lambda e:e['key']==QUEST_POINTS)['value'])==before_points+150
            assert abs(focus(p,0)-cast)<2,'Restart changed the Renewed Focus cast time'
            assert len(owned(p,GLYPH))==1;assert owned(p,STONE)[0]['quantity']==amount-1
            leave(p)
        report={'result':'PASS','checks':['one production Miller, Warder and grounded windmill receiver',
            'crafted Rock Hammer; native map destination and quarry harvest','range, combat and wrong hand-ins rejected',
            'map and quarry ownership persist through restart','one Stone spent, remainder retained',
            'gear repair, 200 XP, 150 quest points, glyph, Esper referral',
            'Renewed Focus owner effect and +40 maximum vigor persist across restart with original cast time',
            'three restarts preserve phases, items and no duplicate payout']}
        (run/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report));print('Evidence:',run)
    finally:
        for n,p in enumerate(peers):
            (run/f'peer-{n}.json').write_text(json.dumps(p.events));p.close()
        stop()

if __name__=='__main__':main()
