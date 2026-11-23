"""Real UDP hunting dialogue, kills, return rewards and restart persistence.

NPCs use production placements. Use prepared target homes first; extra copies
near a home allow long hunts to finish without waiting for respawns. All
one-health copies and test-map travel exist only in this disposable world.
"""
import argparse
import json
import math
import os
import subprocess
import time
import uuid
from pathlib import Path
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer
from test_gameplay import until
from prepare_content import prepare

PORT = 19536


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, help='Reuse a prepared production world manifest')
    parser.add_argument('--shard', default='1/1', help='One-based I/N partition by quest giver')
    parser.add_argument('--aerie', action='store_true', help='Play the four Aerie hunts and their courier follow-up on one character')
    parser.add_argument('--port', type=int, default=PORT)
    args = parser.parse_args()
    shard, count = map(int, args.shard.split('/'))
    if not 1 <= shard <= count:
        parser.error('shard must be in 1..N')
    run = ROOT/'Saved/AC2NetworkTests'/('hunting-'+uuid.uuid4().hex)
    run.mkdir(parents=True)
    pack = {'quests': [q for version in range(1,7) for q in json.loads(
        (ROOT/f'AC2E/emulator/content/hunting-content-v{version}.json').read_text())['quests']]}
    catalog = ROOT/'AC2E/emulator/content/hunting-updates-v7.json'
    revisions = {r['id']: r for r in json.loads(catalog.read_text())['quests']}
    points = json.loads((catalog.parent/'quest-point-rewards-v1.json').read_text())
    referral = json.loads((catalog.parent/'hunting-content-v5.json').read_text())['referral']
    if args.aerie:
        if count != 1:
            parser.error('--aerie requires the four givers on one character')
        pack['quests'] = [q for q in pack['quests'] if q['id'] in referral['prerequisites']]
    # Keep chains on the same character; acceptance must exercise their real prerequisites.
    speakers = list(dict.fromkeys(q['npc'] for q in pack['quests']))
    pack['quests'] = [q for q in pack['quests'] if speakers.index(q['npc']) % count == shard - 1]
    if not pack['quests']:
        parser.error('shard contains no quest givers')
    for q in pack['quests']:
        if q['id'] in revisions:
            q['xp_cap'] = revisions[q['id']]['xp']
    prepared = args.prepared or run/'prepared.tsv'
    if not args.prepared:
        prepare(DAT_DIRECTORY, prepared)
    rows = [s.split('\t') for s in prepared.read_text(encoding='utf8').splitlines()
            if len(s.split('\t')) == 13]
    npcs, targets, selected = {}, {}, []
    do_referral = set(referral['prerequisites']).issubset(q['id'] for q in pack['quests'])
    if do_referral:
        for actor in (referral['giver'], referral['recipient']):
            npcs[actor] = next(r for r in rows if int(r[1],16) == actor)
            selected.append(npcs[actor])
    for q in pack['quests']:
        npc = next(r for r in rows if int(r[1],16) == q['npc'])
        if q['npc'] not in npcs: selected.append(npc)
        npcs[q['npc']] = npc
        originals = [r for r in rows if int(r[1],16) in q['targets']]
        assert originals, q['title']
        targets[q['id']] = []
        for n in range(q['goal']):
            row = originals[n % len(originals)].copy()
            row[0] = f'{0x8140000000034000+(q["id"] & 255)*128+n:016x}'
            if n >= len(originals):
                # Use another prepared creature's grounded home for this test copy.
                # An arbitrary XY offset at the original Z can put it inside a hill.
                cell = int(row[3],16)
                def nearby(r):
                    candidate = int(r[3],16)
                    if cell & 0xffff > 64:
                        return candidate == cell
                    return candidate >> 16 == cell >> 16 and 1 <= candidate & 0xffff <= 64
                homes=[r for r in rows if r[0].startswith(('8140c0','8140c1','8140db','8140ed','8140dd'))
                       and nearby(r) and all(not nearby(used) or math.dist(
                           tuple(map(float,r[4:7])),tuple(map(float,used[4:7]))) >= 4 for used in selected)]
                if not homes:
                    # Other blocks can host a disposable test copy as well.
                    used_homes={tuple(r[3:7]) for r in selected}
                    homes=[r for r in rows if r[0].startswith('8140c1') and tuple(r[3:7]) not in used_homes]
                home=min(homes,key=lambda r:math.dist(tuple(map(float,row[4:7])),tuple(map(float,r[4:7]))))
                row[3:7]=home[3:7]
            selected.append(row); targets[q['id']].append(row)
    content = run/'world.tsv'
    content.write_text('pyrealium-world-v4\n'+''.join('\t'.join(r)+'\n' for r in selected),encoding='utf8')
    rules = run/'rules.tsv'
    rules.write_text('pyrealium-encounters-v1\nplayer\t1000\t1\t1000\t30\nregen\t1000\t1000\t1000\n'+
                    ''.join(f'npc\t{e}\t1\t1\t60000\t3\t300000\t0\t0\n'
                            for e in sorted({r[1] for v in targets.values() for r in v})))
    # Seed only this isolated character store, using the supported offline API.
    for command in [
        ['character-create-human','--account','local','--name','Hunt Tester'],
        ['player-state','--character-id','1','--event','x,31731187528870'],
        ['character-create-human','--account','local','--name','Low Level Hunter']]:
        subprocess.run([str(SERVER),*command,'--data',str(run/'world')],check=True,capture_output=True)
    peers, result = [], {}
    process = None
    with (run/'server.log').open('w',encoding='utf8') as log:
        def stop():
            if process and process.poll() is None:
                (run/'world/stop.request').write_text('stop\n'); process.wait(timeout=15)

        def start():
            p = subprocess.Popen([str(SERVER),'serve','--bind',f'127.0.0.1:{args.port}',
                '--account','local','--data',str(run/'world'),'--game-data',str(DAT_DIRECTORY),
                '--content',str(content),'--encounters',str(rules),'--initialize-ui','true',
                '--portals','false','--lifestones','false',
                '--test-map-travel','true','--seconds','3600'],stdout=log,stderr=log,creationflags=0x08000000,
                env=dict(os.environ,PYREALIUM_UNREAL_LEGIONS_DATA='1',AC2_TEST_LOOT_SEED='17'))
            time.sleep(1)
            assert p.poll() is None,(run/'server.log').read_text()
            return p

        def login(identity='1'):
            p = Peer(); peers.append(p); p.id = identity
            p.send('connect',host='127.0.0.1',port=args.port,account='local'); p.wait('characters')
            p.send('enter',id=identity); p.wait('player'); p.send('ready')
            p.wait('status',lambda e:e['phase']=='world'); until(p,0,'inventory')
            return p

        def leave(p):
            mark=len(p.events); p.send('logout',id=p.id); until(p,mark,'selection'); p.close()

        def at(p,row,distance=.5):
            pos=list(map(float,row[4:7])); pos[0]+=distance
            cell=int(row[3],16)
            # Interior coordinates are room-local and can be negative. Never
            # reinterpret an indoor variant as an outdoor landscape cell.
            if 1 <= (cell & 65535) <= 64:
                dx,dy=math.floor(pos[0]/160),math.floor(pos[1]/160)
                pos[0]-=dx*160; pos[1]-=dy*160
                cell=(((cell>>24)+dx)<<24)|((((cell>>16)&255)+dy)<<16)|(int(pos[0]/20)*8+int(pos[1]/20)+1)
            p.send('move',cell=cell,position=pos,heading=0,motion=[0,0,0])
            until(p,0,'object',lambda e:e['id']==str(int(row[0],16)))
            time.sleep(.1)

        def use(p,npc):
            time.sleep(1.1)
            mark=len(p.events); p.send('use',item=str(int(npcs[npc][0],16)),target='0')
            return mark

        def journal(p,mark,q,count,completed=False):
            status=0x30000000 if completed else 0x10000000
            phase=0 if completed else count+1
            return until(p,mark,'quests',lambda e:any(i['id']==q and i['status']==status and i['phase']==phase for i in e['items']))

        def quality(p, key):
            return int(next(e['value'] for e in reversed(p.events)
                            if e.get('event')=='quality' and e.get('id') is None and e['key']==key))

        try:
            process=start(); low=login('2')
            gated = next((q for q in pack['quests'] if q['level'] > 1 and
                         all(choice['level'] > 1 for choice in pack['quests'] if choice['npc'] == q['npc'])), None)
            if gated:
                at(low,npcs[gated['npc']]); mark=use(low,gated['npc'])
                until(low,mark,'chat',lambda e:'level or prerequisite' in str(e))
                result['level_gate_enforced']=True
            leave(low)
            hero=login()
            if do_referral:
                at(hero, npcs[referral['giver']]); mark = use(hero, referral['giver'])
                until(hero, mark, 'chat', lambda e: 'all four Aerie hunts' in str(e))
            for i,q in enumerate(pack['quests']):
                at(hero,npcs[q['npc']]); mark=use(hero,q['npc']); journal(hero,mark,q['id'],0)
                until(hero,mark,'script',lambda e:e['function']==0x82120024)
                for n,row in enumerate(targets[q['id']]):
                    at(hero,row); mark=len(hero.events); iid=str(int(row[0],16))
                    for attempt in range(4):
                        if attempt:
                            # Follow the replicated location; a neighboring monster may
                            # already have pursued the tester and be leashing home.
                            place=next((e.get('placement',e) for e in reversed(hero.events)
                                        if e.get('id')==iid and e['event'] in ('position','object')),None)
                            if place:
                                current=row.copy()
                                current[3:7]=[f"{place['cell']:08x}", *map(str,place['position'])]
                                at(hero,current,0.01)
                        hero.send('target',id=iid); hero.send('attack',id=iid)
                        try:
                            until(hero,mark,'quality',lambda e:e.get('id')==iid and e['key']==0x102 and e['value']==0,timeout=3)
                            break
                        except AssertionError:
                            if attempt==3: raise
                    journal(hero,mark,q['id'],n+1)
                    hero.send('combat',enabled=False); until(hero,mark,'combat',lambda e:not e['enabled'])
                    changed_hunts=[item['id'] for e in hero.events[mark:] if e['event']=='quests'
                                   for item in e['items'] if item['id'] >> 8 == 0x4000fc]
                    assert changed_hunts==[q['id']], ('Unchanged hunts retransmitted', changed_hunts)
                    if i==0 and n==3:
                        leave(hero); stop(); process=start(); hero=login()
                        journal(hero,0,q['id'],4); result['partial_progress_survives_restart']=True
                at(hero,npcs[q['npc']]); before=quality(hero, 0x12c); before_points=quality(hero, 0x7ac20003)
                mark=use(hero,q['npc'])
                journal(hero,mark,q['id'],q['goal'],True)
                until(hero,mark,'quality',lambda e:e.get('id') is None and e['key']==0x12c and int(e['value'])==before+q['xp_cap'])
                award = points.get(f"{q['id']:08x}", {}).get('points', 0)
                if award:
                    until(hero,mark,'quality',lambda e:e.get('id') is None and e['key']==0x7ac20003 and int(e['value'])==before_points+award)
                if q.get('key'):
                    until(hero,mark,'object',lambda e:e.get('entity')==q['key']['item'] and e.get('container')==hero.id)
                assert quality(hero, 0x7ac20003) == before_points + award
                result[q['title']]={'journal':f"{q['id']:08x}", 'accepted':True, 'kills':q['goal'],
                                    'returned':True, 'xp':q['xp_cap'], 'points':award}
                if q.get('key'):
                    result[q['title']]['key'] = q['key']['item']
                (run/'progress.json').write_text(json.dumps(result,indent=2))
                print('PASS', q['title'], flush=True)
            # A standalone completed quest cannot pay twice before its repeat timer.
            before=quality(hero, 0x12c); before_points=quality(hero, 0x7ac20003); mark=use(hero,pack['quests'][-1]['npc'])
            until(hero,mark,'script',lambda e:e['function']==0x82120024)
            time.sleep(.2); assert quality(hero, 0x12c)==before and quality(hero, 0x7ac20003)==before_points
            result['duplicate_turn_in_rejected']=True
            leave(hero); stop(); process=start(); hero=login()
            for q in pack['quests']: journal(hero,0,q['id'],q['goal'],True)
            assert quality(hero, 0x12c)==before and quality(hero, 0x7ac20003)==before_points
            result['all_completed_journals_survive_restart']=len(pack['quests'])
            result['kill_updates_exclude_unchanged_hunts']=True
            if do_referral:
                rid, giver, recipient = (referral[k] for k in ('id', 'giver', 'recipient'))
                at(hero, npcs[giver]); mark = use(hero, giver)
                accepted = journal(hero, mark, rid, 0)
                entry = next(q for q in accepted['items'] if q['id'] == rid)
                assert referral['limit_seconds']-5 <= entry['remaining'] <= referral['limit_seconds']
                item = until(hero, mark, 'object', lambda e: e.get('entity') == referral['item'] and e.get('container') == hero.id)['id']
                mark = use(hero, giver)
                until(hero, mark, 'chat', lambda e: 'in your backpack' in str(e))
                leave(hero); stop(); process=start(); hero=login()
                journal(hero, 0, rid, 0)
                until(hero, 0, 'object', lambda e: e['id'] == item and e.get('container') == hero.id)
                at(hero, npcs[recipient]); mark=len(hero.events)
                hero.send('use', item=item, target=str(int(npcs[recipient][0],16)))
                journal(hero, mark, rid, 1, True)
                # Level 130 and 131 thresholds from the archived progression table.
                expected_xp = (34261198980545 - 31731187528870) // 100
                until(hero, mark, 'quality', lambda e: e['key'] == 0x12c and int(e['value']) == before+expected_xp)
                mark = use(hero, recipient)
                until(hero, mark, 'chat', lambda e: 'current delivery' in str(e))
                leave(hero); stop(); process=start(); hero=login()
                journal(hero, 0, rid, 1, True)
                assert item not in until(hero,0,'inventory')['items']
                assert quality(hero,0x12c) == before+expected_xp and quality(hero,0x7ac20003) == before_points
                result['aerie_delivery'] = {'all_four_prerequisites':True, 'xp':expected_xp,
                                            'consumed_report':True, 'restarted':True}
            leave(hero)
            (run/'result.json').write_text(json.dumps(result,indent=2))
            print('PASS',run,json.dumps(result))
        finally:
            for i,p in enumerate(peers):
                (run/f'peer-{i}.json').write_text(json.dumps(p.events)); p.close()
            stop()


if __name__=='__main__': main()
