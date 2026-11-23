"""Three racial Shelter quests using production placements and encounter rules.

Moves directly between checked anchors to test the network/state machine; client
collision traversal is separately covered by AC2.Data.StarterClearance.
No production characters, stores, mob stats or quest rewards are modified.
"""
import json
import math
import os
import subprocess
import time
import uuid
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer
from test_gameplay import until
from prepare_content import prepare


def main():
    run = ROOT/'Saved/AC2NetworkTests'/('starters-'+uuid.uuid4().hex)
    run.mkdir(parents=True)
    content = run/'world.tsv'
    prepare(DAT_DIRECTORY, content)
    catalog = json.loads((ROOT/'AC2E/emulator/content/starter-areas-v1.json').read_text())
    human = next(area for area in catalog['areas'] if area['race'] == 1)
    expected = {actor['id'] for room in (human, human['vault']) for actor in room['actors']}
    rows = [line.split('\t') for line in content.read_text().splitlines() if '\t0299' in line]
    assert {row[0] for row in rows} == expected, 'Unexpected or missing Human Shelter placement'
    assert not any(row[1] in ('470007d1', '470007de', '47003724', '47001699') for row in rows)
    for entity, count in [('470013af', 3), ('4700158b', 4), ('4700158c', 8)]:
        assert sum(row[1] == entity for row in rows) == count, entity
    peers, results = [], []
    with (run/'server.log').open('w') as log:
        server = subprocess.Popen([str(SERVER), 'serve', '--bind', '127.0.0.1:19546',
            '--account', 'local', '--data', str(run/'world'), '--game-data', str(DAT_DIRECTORY),
            '--content', str(content), '--encounters', str(ROOT/'AC2E/emulator/content/development-encounters.tsv'),
            '--initialize-ui', 'true', '--portals', 'true', '--seconds', '300'],
            env=dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1'), stdout=log, stderr=log, creationflags=0x08000000)
        try:
            time.sleep(1)
            assert server.poll() is None, (run/'server.log').read_text()
            for area in catalog['areas']:
                p = Peer(); peers.append(p)
                p.send('connect', host='127.0.0.1', port=19546, account='local');p.wait('characters')
                p.send('create', name=area['name']+' Shelter', start_area=0, race=area['race'], sex=4096, physical=[[1,1],[2,1]])
                p.id=p.wait('created')['character']['id'];p.send('enter',id=p.id);p.wait('player')
                spawn=p.wait('object',lambda e:e['id']==p.id)
                assert spawn['placement']['cell']==area['start']['cell'],spawn
                assert math.dist(spawn['placement']['position'],area['start']['position'])<.02,spawn
                p.send('ready');p.wait('status',lambda e:e['phase']=='world');until(p,0,'inventory')
                initial_quests=until(p,0,'quests')['items']
                assert not any(q['id']==0x4000ff01 for q in initial_quests), 'Login auto-enrolled a training quest'
                actors={r['role']:r for r in area['actors']+area['vault']['actors']}
                def iid(role):return str(int(actors[role]['id'],16))
                def at(role):
                    r=actors[role];pos=r['position'][:];pos[0]+=.8
                    p.send('move',cell=r['cell'],position=pos,heading=0,motion=[0,0,0])
                    time.sleep(.35);until(p,0,'object',lambda e:e['id']==iid(role))
                def use(role, target='0'):
                    time.sleep(1.1);mark=len(p.events)
                    p.send('use',item=iid(role) if role in actors else role,target=target)
                    return mark
                def phase(mark, expected):
                    words={1:'Lieutenant',2:'Sentry',3:'Digging Tool',4:'Menhir Glyph',5:'Advance Shelter Scout',6:'Use the Menhir Shard',7:'complete'}
                    return until(p,mark,'quests',lambda e:any(q['id']==0x4000ff05 and words[expected] in str(q['journal']) for q in e['items']))
                def kill(role, definition):
                    at(role);mark=len(p.events);p.send('attack',id=iid(role))
                    item=until(p,mark,'object',lambda e:e.get('container')==p.id and e.get('entity')==definition,timeout=40)
                    p.send('combat',enabled=False);until(p,mark,'combat',lambda e:not e['enabled'])
                    return item['id']
                # An early portal visit must point to the missing quest stage,
                # rather than telling a new character to use an unobtainable glyph.
                at('menhir-portal');mark=use('menhir-portal')
                until(p,mark,'chat',lambda e:'Shelter Guard' in str(e))
                at('sentry');mark=use('sentry')
                until(p,mark,'chat',lambda e:e.get('text',{}).get('key')==0x8b6b186d)
                for role, expected in [('guard',1),('lieutenant',2),('sentry',3)]:
                    at(role);phase(use(role),expected)
                if area['name'] == 'Human':
                    at('straw-drudge'); mark = len(p.events)
                    dummy = iid('straw-drudge')
                    p.send('attack', id=dummy)
                    until(p, mark, 'quality', lambda e: e.get('id') == dummy and e['key'] == 0x102 and 0 < e['value'] < 25000)
                    p.send('combat', enabled=False)
                    until(p, mark, 'combat', lambda e: not e['enabled'])
                    time.sleep(3.2)
                    assert not any(e.get('event') == 'hit' and e.get('attacker') == dummy for e in p.events[mark:]), 'Straw Drudge fought back'
                tool=kill('digger',0x47001587)
                mark=use('digger')
                until(p,mark,'chat',lambda e:'Imbued Digging Tool is already in your backpack' in str(e))
                assert not any('Additional scripted actions' in str(e) for e in p.events[mark:])
                at('scout');mark=use('scout');phase(mark,4)
                until(p,mark,'inventory_remove',lambda e:e['id']==tool)
                for definition in [0x47001583,0x47001584,0x47001585]:
                    until(p,mark,'object',lambda e:e.get('container')==p.id and e.get('entity')==definition)
                mark=use('scout');until(p,mark,'chat',lambda e:e.get('text',{}).get('key')==0x8b6b1867)
                assert not any(e.get('event')=='object' and e.get('container')==p.id for e in p.events[mark:])
                glyph=kill('sentinel',0x47001582)
                mark=use('sentinel')
                until(p,mark,'chat',lambda e:'Menhir Glyph is already in your backpack' in str(e))
                at('menhir-portal');mark=use('menhir-portal')
                until(p,mark,'chat',lambda e:'glyph' in str(e).lower())
                mark=use(glyph,iid('menhir-portal'));phase(mark,5)
                arrival=until(p,mark,'position',lambda e:e['id']==p.id and e['cell']==area['vault']['start']['cell'])
                assert math.dist(arrival['position'],area['vault']['start']['position'])<.02,arrival
                assert arrival['teleport']==1 and arrival['force']==0, arrival
                p.send('ready')  # Client acknowledges the newly loaded room.
                until(p,mark,'inventory_remove',lambda e:e['id']==glyph)
                # Failed retries must not prolong the five-second travel timer.
                # Return to the entrance and spam use through the expiration,
                # exercising the same portal after its one-use glyph is gone.
                at('menhir-portal');mark=len(p.events);deadline=time.monotonic()+8
                while time.monotonic()<deadline:
                    p.send('use',item=iid('menhir-portal'),target='0')
                    time.sleep(.25)
                    if any(e.get('event')=='position' and e.get('teleport')==2 for e in p.events[mark:]):break
                retry=until(p,mark,'position',lambda e:e['id']==p.id and e['teleport']==2)
                assert retry['cell']==area['vault']['start']['cell'],retry
                assert any('Wait a few seconds' in str(e) for e in p.events[mark:]),'Cooldown not exercised'
                p.send('ready')
                at('shard');mark=use('shard')
                until(p,mark,'chat',lambda e:'Advance Shelter Scout' in str(e))
                at('advance');phase(use('advance'),6)
                at('shard')
                time.sleep(5)  # The test warps between anchors, skipping the walk across the vault.
                if area['name'] == 'Human':
                    # The initial activation is visible before movement lock.
                    # Walking away must leave the quest and character in the vault.
                    attempt=use('shard')
                    until(p,attempt,'fx',lambda e:e['id']==iid('shard') and e['fx']==0x41000044)
                    at('advance')
                    until(p,attempt,'chat',lambda e:'activation interrupted' in str(e),timeout=15)
                    assert not any(e['event']=='story_scenes' for e in p.events[attempt:])
                    at('shard')
                mark=use('shard')
                activation=until(p,mark,'fx',lambda e:e['id']==iid('shard') and e['fx']==0x41000044)
                scene=until(p,mark,'story_scenes',timeout=20)
                story=scene['scenes'][0]
                assert story['id']==0x40000021 and story['act']==0x40000008, story
                assert len(story['frames'])==19 and story['frames'][0]['video']==0x4100497d, story
                assert story['frames'][0]['voice']==0x2c000311, story
                entered=until(p,mark,'portal_space',lambda e:e['active'])
                effects=[until(p,mark,'fx',lambda e:e['id']==p.id and e['fx']==resource)
                         for resource in [0x4100000f,0x8100028e,0x410004fa,0x41000010]]
                exited=until(p,mark,'portal_space',lambda e:not e['active'])
                arrival=until(p,mark,'position',lambda e:e['id']==p.id and e['cell']==area['overworld']['cell'])
                ordered=[activation,scene,entered,*effects,exited]
                indexes=[p.events.index(event,mark) for event in ordered]
                assert indexes==sorted(indexes), indexes
                assert math.dist(arrival['position'],area['overworld']['position'])<.02,arrival
                assert arrival['teleport']==3 and arrival['force']==0, arrival
                phase(mark,7)
                p.send('ready')
                until(p,mark,'stop_fx',lambda e:e['id']==p.id and e['fx']==0x41000010)
                p.send('story_scene_complete',id=story['id'])
                mark=len(p.events);p.send('logout',id=p.id);until(p,mark,'selection')
                mark=len(p.events);p.send('enter',id=p.id);until(p,mark,'player');p.send('ready');phase(mark,7)
                restored=until(p,mark,'object',lambda e:e['id']==p.id)
                assert restored['placement']['cell']==area['overworld']['cell']
                results.append(dict(race=area['name'],complete=True,overworld=f"{area['overworld']['cell']:08x}",relogin=True))
                p.close()
                if area['name'] == 'Human':
                    # A fresh player can opt out, but only after reading the
                    # warning and confirming the skull. Both survive logout.
                    p = Peer(); peers.append(p)
                    p.send('connect', host='127.0.0.1', port=19546, account='local')
                    p.wait('characters')
                    p.send('create', name='Shortcut Traveler', start_area=0, race=area['race'], sex=4096, physical=[[1,1],[2,1]])
                    p.id = p.wait('created')['character']['id']
                    p.send('enter', id=p.id); p.wait('player')
                    p.send('ready'); until(p, 0, 'inventory')
                    at('warning-skull'); mark = use('warning-skull')
                    until(p, mark, 'chat', lambda e: e['text'].get('key') == 0x63691d8f)
                    at('exit-warning'); mark = use('exit-warning')
                    until(p, mark, 'chat', lambda e: e['text'].get('table') == 0x2500169e)
                    at('shortcut-portal'); mark = use('shortcut-portal')
                    until(p, mark, 'chat', lambda e: 'Warning Skull' in str(e))
                    at('warning-skull'); mark = use('warning-skull')
                    until(p, mark, 'chat', lambda e: e['text'].get('key') == 0x2824fd4c)
                    mark = len(p.events); p.send('logout', id=p.id); until(p, mark, 'selection')
                    mark = len(p.events); p.send('enter', id=p.id); until(p, mark, 'player')
                    p.send('ready'); until(p, mark, 'inventory')
                    at('shortcut-portal'); mark = use('shortcut-portal')
                    arrival = until(p, mark, 'position', lambda e: e['id'] == p.id and e['cell'] == area['overworld']['cell'])
                    assert math.dist(arrival['position'], area['overworld']['position']) < .02, arrival
                    phase(mark, 7)
                    results.append(dict(race='Human', route='quick exit', overworld=f"{arrival['cell']:08x}"))
                    p.close()
            (run/'result.json').write_text(json.dumps(results,indent=2));print('PASS',run,json.dumps(results))
            records=(run/'world/characters.journal').read_text().splitlines()
            assert not any(line.startswith(('t1\t','t2\t','t3\t','k4\t')) for line in records), 'New sessions wrote removed training grants'
        finally:
            for n,p in enumerate(peers):
                (run/f'peer-{n}.json').write_text(json.dumps(p.events));p.close()
            if server.poll() is None:
                (run/'world/stop.request').write_text('stop\n');server.wait(timeout=15)


if __name__=='__main__':main()
