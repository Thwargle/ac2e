"""Real UDP gameplay acceptance against Legions DATs and a disposable AC2E store.

Verifies the authored starter dungeon circuit, not retail quest/script parity.
No production journals or account credentials are used.
"""
import json
import math
import os
import subprocess
import time
import uuid
from test_network import Peer, ROOT, SERVER, DAT_DIRECTORY, prepare_test_content


def until(peer, start, event, predicate=lambda e: True, timeout=15):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        found = next((e for e in peer.events[start:] if e.get('event') == event and predicate(e)), None)
        if found is not None:
            return found
        time.sleep(.02)
    raise AssertionError(f'{event} missing; recent events={peer.events[-12:]}')


def main():
    data = ROOT / 'Saved/AC2NetworkTests' / ('gameplay-' + uuid.uuid4().hex)
    data.mkdir(parents=True)
    content = prepare_test_content(data)
    log = (data/'server.log').open('w', encoding='utf-8')
    args = [str(SERVER), 'serve', '--bind', '127.0.0.1:19320', '--account', 'local',
            '--data', str(data/'world'), '--game-data', str(DAT_DIRECTORY), '--content', str(content),
            '--initialize-ui', 'true', '--training-kit', 'false', '--lifestones', 'true', '--portals', 'true', '--seconds', '240']
    server = subprocess.Popen(args, env=dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1'),
                              stdout=log, stderr=log, creationflags=0x08000000)
    peers=[]
    result={}
    try:
        time.sleep(1)
        assert server.poll() is None, (data/'server.log').read_text()
        for name in ['Dungeon Hero','Dungeon Observer']:
            p=Peer();peers.append(p)
            p.send('connect',host='127.0.0.1',port=19320,account='local');p.wait('characters')
            p.send('create',name=name,start_area=0,race=1,sex=4096,physical=[[1,1],[2,1]])
            p.id=p.wait('created')['character']['id']
            p.send('enter',id=p.id);p.wait('player')
            p.spawn=p.wait('object',lambda e:e['id']==p.id)
            p.send('ready');p.wait('status',lambda e:e['phase']=='world')
            until(p,0,'inventory',lambda e:len(e.get('items',[]))>=3)
        a,b=peers
        until(a,0,'object',lambda e:e['id']==b.id)
        inventory=next(e for e in a.events if e.get('event')=='inventory')
        items={e['entity']:e for e in a.events if e.get('event')=='object' and e.get('container')==a.id}
        sword,staff,bow=[items[did] for did in [0x470007ee,0x470007e0,0x470007ed]]
        bag=list(inventory['items'])
        def equip(item,mode):
            start=len(a.events); obs=len(b.events)
            a.send('equip',id=item['id'],location=0x40,slot=bag.index(item['id']))
            ack=until(a,start,'inventory_result',lambda e:e['id']==item['id'])
            assert ack['status']==0 and ack.get('equipped')==0x40,ack
            bag.remove(item['id'])
            held=until(b,obs,'object',lambda e:e['id']==item['id'] and e.get('parent')==a.id)
            assert held['holding'] != 0,held
            a.send('target',id='0');a.send('combat',enabled=True)
            until(a,start,'combat',lambda e:e['enabled'])
            until(b,obs,'mode',lambda e:e['id']==a.id and e['mode']==mode)
            a.send('combat',enabled=False)
            until(a,start,'combat',lambda e:not e['enabled'])
        def unequip(item):
            time.sleep(.15);start=len(a.events)
            a.send('unequip',id=item['id'],location=0x40,slot=len(bag))
            ack=until(a,start,'inventory_result',lambda e:e['id']==item['id'])
            assert ack['status']==0 and ack.get('equipped')==0,ack
            bag.insert(ack['slot'],item['id'])
            time.sleep(.15)
        pose=a.spawn['placement'];position=list(pose['position'])
        def move(destination,cell=None):
            nonlocal position
            cell=cell or pose['cell']
            # Walk at a bounded speed using native movement messages.
            distance=math.dist(position,destination)
            steps=max(1,math.ceil(distance/.5));old=position[:]
            for i in range(1,steps+1):
                position=[x+(y-x)*i/steps for x,y in zip(old,destination)]
                a.send('move',cell=cell,position=position,heading=90,motion=[0,1,0]);time.sleep(.1)
            a.send('move',cell=cell,position=position,heading=90,motion=[0,0,0])
        equip(sword,0x4100000a)
        start=len(a.events); obs=len(b.events)
        move([position[0]+.5,position[1],position[2]])
        update=until(b,obs,'position',lambda e:e['id']==a.id and math.dist(e['position'],position)<.01)
        result['player_replication']=update['position']
        # A magic skill with a sword must not damage the target or spend vigor.
        idols=[e for e in a.events if e.get('event')=='object' and e.get('entity')==0x470007d1 and e['placement']['cell']==pose['cell']]
        ids=list(dict.fromkeys(e['id'] for e in idols));assert len(ids)>=3,ids
        a.send('skill',skill=0x60,target=ids[0])
        until(a,start,'chat',lambda e:'matching' in str(e));until(a,start,'skill_finished')
        assert not any(e.get('event')=='hit' and e.get('id')==ids[0] for e in a.events[start:])
        result['wrong_weapon_rejected']=True
        # First kill: retaliation, native attack and death all visible to the observer.
        start=len(a.events);obs=len(b.events)
        a.send('attack',id=ids[0])
        until(a,start,'hit',lambda e:e['id']==a.id)
        until(b,obs,'hit',lambda e:e['id']==a.id)
        until(a,start,'quality',lambda e:e.get('id')==ids[0] and e['key']==0x102 and e['value']==0)
        until(b,obs,'quality',lambda e:e.get('id')==ids[0] and e['key']==0x102 and e['value']==0)
        until(a,start,'combat',lambda e:not e['enabled'])
        result['melee_damage_retaliation_death']=True
        # The loot grant is in a new dense bag slot.
        loot=until(a,start,'object',lambda e:e.get('container')==a.id and e['id'] not in bag and e['entity'] not in items)
        bag.append(loot['id'])
        unequip(sword);equip(staff,0x41000011)
        start=len(a.events);obs=len(b.events)
        a.send('skill',skill=0x60,target=ids[1]);until(a,start,'skill_finished')
        until(b,obs,'behavior',lambda e:e['id']==a.id and e['behavior']==0x410000b0)
        mob_pose=until(b,obs,'position',lambda e:e['id']==ids[1])
        until(a,start,'position',lambda e:e['id']==ids[1] and e['position']==mob_pose['position'])
        # Stop only the player's attack, keeping the hostile creature pursuing.
        a.send('combat',enabled=False);until(a,start,'combat',lambda e:not e['enabled'])
        attack_start=len(a.events);a.send('attack',id=ids[1])
        until(a,start,'quality',lambda e:e.get('id')==ids[1] and e['key']==0x102 and e['value']==0)
        until(a,attack_start,'combat',lambda e:not e['enabled'])
        result['magic_and_enemy_replication']=True
        loot2=until(a,start,'object',lambda e:e.get('container')==a.id and e['entity']==loot['entity'])
        bag.append(loot2['id'])
        time.sleep(.3);unequip(staff);equip(bow,0x41000010)
        start=len(a.events);obs=len(b.events)
        a.send('skill',skill=0xcf,target=ids[2]);until(a,start,'skill_finished')
        until(b,obs,'behavior',lambda e:e['id']==a.id and e['behavior']==0x410000e2)
        a.send('attack',id=ids[2]);until(a,start,'quality',lambda e:e.get('id')==ids[2] and e['key']==0x102 and e['value']==0)
        until(a,start,'combat',lambda e:not e['enabled']);time.sleep(.3)
        result['missile_skill']=True
        # Receive the combat quest reward, bind a real lifestone and leave via portal.
        mentor=next(e for e in a.events if e.get('event')=='object' and e.get('entity')==0x47001699 and e['placement']['cell']==pose['cell'])
        move(mentor['placement']['position']);start=len(a.events)
        a.send('use',item=mentor['id'],target='0')
        until(a,start,'chat',lambda e:'Training combat complete' in str(e))
        stone=next(e for e in a.events if e.get('event')=='object' and e.get('entity')==0x470007de and e['placement']['cell']==pose['cell'])
        move(stone['placement']['position']);start=len(a.events)
        a.send('use',item=stone['id'],target='0');until(a,start,'chat',lambda e:'bound' in str(e).lower())
        portal=next(e for e in a.events if e.get('event')=='object' and e['id']==str(0x8140000000030002))
        move(portal['placement']['position']);time.sleep(5.1);start=len(a.events);obs=len(b.events)
        a.send('use',item=portal['id'],target='0')
        exit_pose=until(a,start,'position',lambda e:e['id']==a.id and e['cell']==0x74ba0008)
        until(b,obs,'remove',lambda e:e['id']==a.id)
        result['starter_dungeon_exit']=exit_pose['cell']
        time.sleep(5.1);start=len(a.events);obs=len(b.events)
        a.send('recall')
        until(a,start,'position',lambda e:e['id']==a.id and e['cell']==pose['cell'],timeout=30)
        until(b,obs,'object',lambda e:e['id']==a.id,timeout=30)
        result['lifestone_recall_and_interest']=True
        # The observer deliberately lets a respawned creature kill it. Death
        # blocks combat, and resurrection restores a fresh native instance.
        respawn=until(b,0,'object',lambda e:e['id']==ids[0] and e['instance']>1,timeout=35)
        start=len(b.events);obs=len(a.events)
        b.send('skill',skill=0xb0,target=ids[0]);until(b,start,'skill_finished')
        b.send('combat',enabled=False)
        until(b,start,'death',lambda e:e['dead']==1,timeout=65)
        until(a,obs,'quality',lambda e:e.get('id')==b.id and e['key']==0x102 and e['value']==0)
        start=len(b.events);b.send('attack',id=ids[0])
        until(b,start,'chat',lambda e:'dead' in str(e).lower())
        start=len(b.events);b.send('resurrect')
        until(b,start,'death',lambda e:e['dead']==0)
        until(b,start,'quality',lambda e:e.get('id') is None and e['key']==0x102 and e['value']>0)
        result['player_death_and_resurrection']=True
        # Replaying the real journals must preserve gear and the completed quest.
        hero=a.id;bow_id=bow['id']
        for p in peers:p.close()
        (data/'world/stop.request').write_text('stop\n');assert server.wait(timeout=10)==0
        server=subprocess.Popen(args, env=dict(os.environ,PYREALIUM_UNREAL_LEGIONS_DATA='1'),
                                stdout=log,stderr=log,creationflags=0x08000000)
        time.sleep(1);assert server.poll() is None,(data/'server.log').read_text()
        p=Peer();peers.append(p);p.send('connect',host='127.0.0.1',port=19320,account='local');p.wait('characters')
        p.send('enter',id=hero);p.wait('player');p.wait('object',lambda e:e['id']==hero);p.send('ready')
        saved=until(p,0,'inventory');assert {'id':bow_id,'slot':0x40} in saved['equipment'],saved
        p.send('chat',text='@quest status',channel='say')
        until(p,0,'chat',lambda e:'Training combat complete' in str(e))
        result['restart_preserves_gear_and_quest']=True
        result['scope']='AC2E authored combat/binding/exit circuit; retail tutorial quest chain not implemented'
        (data/'result.json').write_text(json.dumps(result,indent=2))
        print(json.dumps(result,indent=2));print(data)
    finally:
        for i,p in enumerate(peers):
            (data/f'peer-{i}.json').write_text(json.dumps(p.events,indent=2))
            p.close()
        if server.poll() is None:
            (data/'world/stop.request').write_text('stop\n');server.wait(timeout=10)
        log.close()

if __name__=='__main__':main()
