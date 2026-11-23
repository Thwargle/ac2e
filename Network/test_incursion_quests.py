"""Exercise every placed kill-triggered, elixir, and recovered NPC bounty over loopback UDP.

Each quest gets its own disposable store. Use prepared monster homes and one-hit
copies for objectives that would require respawning. Use --natural-start for the
three elixir hunts to obtain the elixir from its captured source or use its NPC.
"""
import argparse
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import time
import traceback
import uuid

from prepare_content import ContentProfile, prepare
from test_gameplay import until
from test_network import DAT_DIRECTORY, Peer, ROOT, SERVER


CONTENT = ROOT / 'AC2E/emulator/content'
LEVEL_100_XP = 2_978_018_149_631
LEVEL_101_XP = 3_236_529_220_347
ACTIVE, COMPLETE = 0x10000000, 0x30000000


def catalog(name):
    return json.loads((CONTENT / f'{name}.json').read_text(encoding='utf-8'))


def policies():
    quests = {q['id']: q for q in catalog('inferred-content-v1')['quests'] if not q.get('delivery')}
    quests.update({q['id']: q for q in catalog('ac2hero-hunts-v2')['quests']})
    for update in catalog('incursion-rewards-v3'):
        quests[int(update['id'], 16)]['xp'] = update['xp']
    bounties = catalog('npc-bounties-v1')
    quests.update({q['id']: q for q in bounties['quests']})
    for update in bounties['starter_hunts']:
        quests[update['id']]['level'] = update['level']
    slimelord = catalog('slimelord-hunt-v1')
    quests[slimelord['id']].update(slimelord)
    return quests


def testcase(q, homes, visuals, fixture_unplaced, backup_homes, natural_start):
    def placements(entities):
        return [r for entity in entities for r in homes.get(entity, [])]

    targets = placements(q['targets'])
    starter = placements(q['kill_starters'] or ([q['npc']] if 'npc' in q else []))
    missing = []
    if not targets:
        missing.append('target')
    if not starter and not q['use_starters']:
        missing.append('starter')
    if missing and not fixture_unplaced:
        return {'title': q['title'], 'status': 'unplaced', 'missing': missing}
    def fixture(did):
        return ['0', f'{did:08x}', f'{visuals.visual(did):08x}', '74ba0010',
                '26', '150', '262', '1', '0', '0', '0', '-', '-']
    if 'target' in missing:
        targets = [fixture(q['targets'][0])]
    if 'starter' in missing:
        starter = [fixture((q['kill_starters'] or [q['npc']])[0])]
    steps = []
    if starter:
        steps.append(('talk' if 'npc' in q else 'kill', starter[0]))
    for i in range(q['goal']):
        steps.append(('kill', targets[i % len(targets)]))
    substitutes = 0
    if backup_homes:
        for i, (action, home) in enumerate(steps):
            # Repeated copies at one coordinate obstruct each other's initial
            # admission. Distant arenas isolate those fights from leashing.
            occupied = {int(r[3][:4], 16) for _, r in steps[:i]}
            def distinct(candidate):
                block = int(candidate[3][:4], 16)
                return all(abs((block >> 8) - (other >> 8)) > 3 or
                           abs((block & 255) - (other & 255)) > 3
                           for other in occupied)
            is_starter = bool(starter) and i == 0
            if action == 'kill' and (not is_starter or 'starter' in missing):
                native = starter if is_starter else targets
                role = 'starter' if is_starter else 'target'
                alternate = None if role in missing else next((r for r in native if distinct(r)), None)
                if alternate:
                    steps[i] = (action, alternate)
                else:
                    row = home.copy()
                    row[3:7] = next(r for r in backup_homes if distinct(r))[3:7]
                    steps[i] = (action, row)
                    substitutes += 1
    elixir = [(did, visuals.visual(did)) for did in q['use_starters'][:1]]
    source = None
    if natural_start:
        # Native captures pair these potion DIDs and species in these landblocks.
        # The guide also records an NPC start for Armoredillo Doom.
        carriers = {0x40000009: (0x47001072, '76ba'), 0x4000000a: (0x47000f7e, '8eb2')}
        if q['id'] in carriers:
            entity, block = carriers[q['id']]
            home = next(r for r in homes[entity] if r[3][:4].lower() == block)
            source = {'method': 'loot', 'entity': home[1], 'cell': home[3], 'item': elixir[0][0]}
            steps.insert(0, ('loot_elixir', home))
        else:
            ancestor = next(r['giver']['npc'] for r in catalog('npc-bounties-v1')['starter_hunts'] if r['id'] == q['id'])
            home = homes[ancestor][0]
            source = {'method': 'ancestor', 'entity': home[1], 'cell': home[3]}
            steps.insert(0, ('talk', home))
        elixir = []
    return {'steps': steps, 'missing': missing, 'substitutes':substitutes, 'elixir': elixir, 'natural_start':source}


def play(run, port, q, case, award):
    directory = run / f"{q['id']:08x}"
    directory.mkdir()
    store = directory / 'world'
    steps = []
    for i, (action, home) in enumerate(case['steps']):
        row = home.copy()
        # Distinct encounter instances keep source transforms while bypassing respawn waits.
        row[0] = f'{0x8140000000039000+i:016x}'
        steps.append((action, row))
    world = directory / 'world.tsv'
    world.write_text('pyrealium-world-v4\n'+''.join('\t'.join(r)+'\n' for _, r in steps), encoding='utf-8')
    rules = directory / 'rules.tsv'
    rules.write_text('pyrealium-encounters-v1\nplayer\t1000\t1\t500\t30\nregen\t1000\t1000\t1000\n'+
                     ''.join(f'npc\t{did}\t1\t1\t60000\t3\t300000\t0\t0\n'
                             for did in sorted({r[1] for action,r in steps if action in ('kill','loot_elixir')})))
    env = dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1', AC2_TEST_LOOT_SEED='7' if case['natural_start'] else '17')
    def seed(args):
        subprocess.run([str(SERVER), *args, '--data', str(store), '--account', 'local'],
                       env=env, capture_output=True, check=True)

    seed(['character-create-human', '--name', 'Incursion Tester'])
    seed(['player-state', '--character-id', '1', '--event', f'x,{LEVEL_100_XP}'])
    for did, visual in case['elixir']:
        seed(['inventory-grant', '--character-id', '1', '--game-data', str(DAT_DIRECTORY),
              '--id', f'{did:08x}', '--visual-did', f'{visual:08x}'])

    def journal(p, mark, status):
        event = until(p, mark, 'quests', lambda e:any(i['id']==q['id'] and i['status']==status for i in e['items']))
        return next(i for i in event['items'] if i['id']==q['id'])

    def balance(p, mark, key, value):
        until(p, mark, 'quality', lambda e:e.get('id') is None and e['key']==key and int(e['value'])==value)

    def login():
        p = Peer()
        try:
            p.send('connect', host='127.0.0.1', port=port, account='local')
            p.wait('characters')
            p.send('enter', id='1')
            p.wait('player')
            p.send('ready')
            p.wait('status', lambda e:e['phase']=='world')
            until(p, 0, 'inventory')
            return p
        except BaseException:
            p.close()
            raise

    def start(log):
        server = subprocess.Popen([str(SERVER),'serve','--bind',f'127.0.0.1:{port}',
            '--account','local','--data',str(store),'--game-data',str(DAT_DIRECTORY),
            '--content',str(world),'--encounters',str(rules),'--initialize-ui','true',
            '--portals','false','--lifestones','false','--test-map-travel','true','--seconds','180'],
            env=env, stdout=log, stderr=log, creationflags=0x08000000)
        time.sleep(.7)
        assert server.poll() is None, (directory / 'server.log').read_text()
        return server

    def stop(server):
        if server and server.poll() is None:
            (store / 'stop.request').write_text('stop\n')
            server.wait(timeout=15)

    xp = q['xp']
    if q.get('xp_basis_points'):
        xp = (LEVEL_101_XP - LEVEL_100_XP) * q['xp_basis_points'] // 10_000
    server = p = None
    with (directory / 'server.log').open('w') as log:
        try:
            server = start(log)
            p = login()
            count = -1
            if case['elixir']:
                did = case['elixir'][0][0]
                item = until(p, 0, 'object', lambda e:e.get('container')=='1' and e['entity']==did)['id']
                mark = len(p.events)
                p.send('use', item=item, target='0')
                entry = journal(p, mark, ACTIVE)
                assert entry['phase'] == 1 and 0 < entry['remaining'] <= q['limit_seconds'], entry
                count = 0
            for action, row in steps:
                iid = str(int(row[0],16))
                pos = list(map(float,row[4:7]))
                pos[0] += .35 if pos[0] % 20 < 19.5 else -.35
                p.send('move', cell=int(row[3],16), position=pos, heading=0, motion=[0,0,0])
                until(p, 0, 'object', lambda e:e['id']==iid)
                time.sleep(.12)
                mark = len(p.events)
                if action in ('kill','loot_elixir'):
                    p.send('target', id=iid)
                    p.send('attack', id=iid)
                    until(p, mark, 'quality', lambda e:e.get('id')==iid and e['key']==0x102 and e['value']==0)
                    p.send('combat', enabled=False)
                    until(p, mark, 'combat', lambda e:not e['enabled'])
                    if action == 'loot_elixir':
                        did = case['natural_start']['item']
                        mark = len(p.events)
                        p.send('open_container', id=iid)
                        corpse = until(p, mark, 'container_state', lambda e:e['id'] == iid and e['open'])
                        potion = until(p, mark, 'object', lambda e:e.get('entity') == did and e['id'] in corpse['items'])
                        mark = len(p.events)
                        p.send('take_item', container=iid, id=potion['id'], **{'from':corpse['items'].index(potion['id'])})
                        owned = until(p, mark, 'object', lambda e:e.get('entity') == did and e.get('container') == '1')
                        mark = len(p.events)
                        p.send('use', item=owned['id'], target='0')
                else:
                    p.send('use', item=iid, target='0')
                    # Kanten first offers Gambling Debt, then his beetle bounty.
                    if q.get('npc') == 0x47001a3c:
                        until(p, mark, 'quests')
                        time.sleep(1.1)
                        mark = len(p.events)
                        p.send('use', item=iid, target='0')
                count += 1
                entry = journal(p, mark, COMPLETE if count == q['goal'] else ACTIVE)
                if count < q['goal']:
                    assert entry['phase']==1 and 0 < entry['remaining'] <= q['limit_seconds'], entry
                    assert f"Progress: {count} / {q['goal']}." in entry['journal']['literal'], entry
                else:
                    assert entry['phase']==0 and 0 <= q['repeat_seconds'] - entry['retry'] <= 5, entry
                    balance(p, mark, 0x12c, LEVEL_100_XP + xp)
                    if award:
                        balance(p, mark, 0x7ac20003, award)
            mark = len(p.events)
            p.send('logout', id='1')
            until(p, mark, 'selection')
            p.close()
            p = None
            stop(server)
            server = start(log)
            p = login()
            journal(p, 0, COMPLETE)
            balance(p, 0, 0x12c, LEVEL_100_XP + xp)
            balance(p, 0, 0x7ac20003, award)
            return {'title':q['title'], 'status':'fixture-passed' if case['missing'] else 'passed',
                    'missing':case['missing'], 'substitute_encounters':case['substitutes'], 'kills':q['goal'], 'xp':xp,
                    'quest_points':award, 'restarted':True, 'pregranted_elixir':bool(case['elixir']),
                    'natural_start': case['natural_start']}
        except Exception:
            if p:
                events = [e for e in p.events if e['event'] in ('chat','error','attack','combat','notice','message','position','movement_error','status')]
                (directory/'diagnostics.json').write_text(json.dumps(events[-50:], indent=2), encoding='utf-8')
            raise
        finally:
            if p:
                p.close()
            stop(server)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--base-port', type=int, default=19640)
    parser.add_argument('--quest', action='append', type=lambda s:int(s,16))
    parser.add_argument('--fixture-unplaced', action='store_true',
                        help='Exercise missing starter/target roles at synthetic coordinates and label those results separately')
    parser.add_argument('--natural-start', action='store_true',
                        help='Start the three elixir quests through documented corpse drops or the placed Ancestor')
    parser.add_argument('--disperse-encounters', action='store_true',
                        help='Use separate grounded arenas for test copies if native homes share a population cluster')
    args = parser.parse_args()
    if not 1 <= args.workers <= 12 or not 1024 <= args.base_port <= 65536-2*args.workers:
        parser.error('Use 1–12 workers and a valid loopback port range')
    quests = policies()
    if args.quest:
        quests = {qid:quests[qid] for qid in args.quest}
    if args.natural_start:
        ids = (0x40000009, 0x4000000a, 0x4000000b)
        if args.quest and any(qid not in ids for qid in args.quest):
            parser.error('--natural-start applies only to Beetle Slayer, Wasp Bane and Armoredillo Doom')
        quests = {qid:q for qid, q in quests.items() if qid in ids}
    run = ROOT/'Saved/AC2NetworkTests'/('incursions-'+uuid.uuid4().hex)
    run.mkdir(parents=True)
    print(run, flush=True)
    prepared = args.prepared or run/'prepared.tsv'
    if not args.prepared:
        prepare(DAT_DIRECTORY, prepared)
    homes = defaultdict(list)
    for line in prepared.read_text(encoding='utf-8').splitlines():
        row = line.split('\t')
        if len(row)==13:
            homes[int(row[1],16)].append(row)
    points = catalog('quest-point-rewards-v1')
    cases = {}
    results = {}
    visuals = ContentProfile(DAT_DIRECTORY)
    try:
        backup_homes = []
        if args.disperse_encounters:
            backup_homes = [r for locations in homes.values() for r in locations if r[0].startswith('8140c1')]
            backup_homes.sort(key=lambda r:r[0])
        for qid, q in quests.items():
            c = testcase(q, homes, visuals, args.fixture_unplaced, backup_homes, args.natural_start)
            if c.get('status') == 'unplaced':
                results[f'{qid:08x}'] = c
            else:
                cases[qid] = c
    finally:
        visuals.close()

    def worker(slot):
        for qid in list(cases)[slot::args.workers]:
            key = f'{qid:08x}'
            try:
                result = play(run, args.base_port+2*slot, quests[qid], cases[qid], points.get(key,{}).get('points',0))
            except Exception:
                result = {'title':quests[qid]['title'], 'status':'failed', 'error':traceback.format_exc()}
            (run/key/'result.json').write_text(json.dumps(result,indent=2), encoding='utf-8')
            results[key] = result
            print(key, result['status'], quests[qid]['title'], flush=True)

    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        list(pool.map(worker, range(args.workers)))
    (run/'result.json').write_text(json.dumps(results, indent=2, sort_keys=True), encoding='utf-8')
    if any(r['status']=='failed' for r in results.values()):
        raise SystemExit(1)


if __name__=='__main__':
    main()
