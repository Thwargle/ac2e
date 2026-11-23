"""AC2E spellbinding and support through an isolated native UDP session."""
import json
import math
import os
import subprocess
import time
import uuid

from prepare_content import ContentProfile
from test_gameplay import until
from test_network import DAT_DIRECTORY, ROOT, SERVER, Peer


PORT = 19482
JACKET, JACKET_VISUAL = 0x47002AED, 0x1F001472  # Crafted Human Soft Leather Jacket, torso.
CHARM, ROCK, WEAPON = 0x47002FBB, 0x47003541, 0x47002925
HEALTH, VIGOR = 0x102, 0x103
TOTEM = 0x47002F31
DIRECT = 0xDD
DUMMIES = tuple(str(0x8140000000030EB0 + n) for n in range(4))


def main():
    run = ROOT / 'Saved/AC2NetworkTests' / ('specialties-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    print(run, flush=True)
    content = run / 'world.tsv'
    profile = ContentProfile(DAT_DIRECTORY)
    try:
        content.write_text('pyrealium-world-v3\n' + ''.join(
            f'{iid:016x}\t{did:08x}\t{profile.visual(did):08x}\t02990109\t{x}\t240.199\t-44.606205\t1\t0\t0\t0\t-\n'
            for iid, did, x in ((0x8140000000030E91, 0x4700082E, 58),
                                (0x8140000000030E92, 0x470007D1, 140))) + ''.join(
            f'{int(iid):016x}\t470013af\t{profile.visual(0x470013AF):08x}\t02990109\t{x}\t{y}\t-44.606205\t1\t0\t0\t0\t-\n'
            for iid, (x, y) in zip(DUMMIES, [(59, 242), (61.5, 242), (66, 242), (73, 242)])))
    finally:
        profile.close()
    encounters = run / 'encounters.tsv'
    encounters.write_text('pyrealium-encounters-v1\nplayer\t20\t2\t1000\t3\nregen\t1\t1\t2000\n'
                          'npc\t470007d1\t20\t1\t1500\t3\t300000\t40\t2\n'
                          'npc\t470013af\t1200\t1\t1500\t3\t300000\t1\t0\n')
    data = run / 'world'
    stop = data / 'stop.request'
    env = dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1')
    args = [str(SERVER), 'serve', '--bind', f'127.0.0.1:{PORT}', '--account', 'local',
            '--data', str(data), '--game-data', str(DAT_DIRECTORY), '--content', str(content),
            '--encounters', str(encounters), '--initialize-ui', 'true', '--lifestones', 'false',
            '--portals', 'false', '--seconds', '120']
    peers, ids = [], []
    server = None

    with (run / 'server.log').open('w') as log:
        def start():
            stop.unlink(missing_ok=True)
            process = subprocess.Popen(args, env=env, stdout=log, stderr=log, creationflags=0x08000000)
            time.sleep(1)
            assert process.poll() is None, (run / 'server.log').read_text()
            return process

        def shutdown():
            stop.write_text('stop\n')
            assert server.wait(timeout=10) == 0

        def join(index, create=False):
            p = Peer()
            peers.append(p)
            p.send('connect', host='127.0.0.1', port=PORT, account='local', dat_directory=str(DAT_DIRECTORY))
            p.wait('characters')
            if create:
                p.send('create', name=['Bind Keeper', 'Ward Ally', 'Ward Friend', 'Ward Visitor'][index],
                       race=(1, 1, 4, 2)[index], sex=4096, start_area=0, physical=[[1, 1], [2, 1]])
                ids.append(p.wait('created')['character']['id'])
            p.id = ids[index]
            p.send('enter', id=p.id)
            p.wait('player')
            p.send('ready')
            p.wait('status', lambda e: e['phase'] == 'world')
            until(p, 0, 'inventory')
            return p

        def edit(index, command, event):
            return subprocess.run([str(SERVER), command, '--account', 'local', '--data', str(data),
                                   '--character-id', ids[index], '--event', event], check=True, env=env,
                                  stdout=log, stderr=log, creationflags=0x08000000)

        def chat(peer, text, result):
            time.sleep(.55)  # Match the server's chat command throttle.
            mark = len(peer.events)
            peer.send('chat', text=text, channel='say')
            return until(peer, mark, 'chat', lambda e: result in str(e))

        def inspect(peer, item):
            mark = len(peer.events)
            peer.send('examine', id=item)
            view = until(peer, mark, 'examination', lambda e: e['id'] == item)
            return ''.join('\n' if n['kind'] == 0x40000006 else n.get('text', {}).get('resolved') or ''
                           for n in view['nodes'])

        def has_health_delta(peer, mark, before, delta):
            values = [before] + [e['value'] for e in peer.events[mark:] if e.get('event') == 'quality'
                                 and e.get('id') is None and e['key'] == HEALTH]
            return any(b - a == delta for a, b in zip(values, values[1:]))

        def stat(peer, key):
            return next(e['value'] for e in reversed(peer.events) if e.get('event') == 'quality'
                        and e.get('id') is None and e['key'] == key)

        try:
            server = start()
            high = slot = 0
            for index in range(4):
                p = join(index, create=True)
                inventory = next(e['items'] for e in reversed(p.events) if e.get('event') == 'inventory')
                slot = len(inventory) if index == 0 else slot
                high = max(high, *map(int, inventory), *(int(e['id']) for e in p.events if e.get('container') == p.id or e.get('wielder') == p.id))
                p.send('logout', id=p.id)
                p.wait('selection')
                p.close()
            shutdown()
            # Seed the inputs to isolate infusion rules from randomized recipe output.
            records = [f'g,{high+1},{CHARM},{0x1F0001CF},1,1,0,{slot}',
                       f'g,{high+2},{CHARM},{0x1F0001CF},1,1,0,{slot+1}',
                       f'g,{high+3},{ROCK},{0x1F0001CF},1,1,0,{slot+2}',
                       f'F,{high+4},{WEAPON},{0x1F0013F5},1,1,64,{slot+3},0,0,1,10',
                       f'g,{high+5},{WEAPON},{0x1F0013F5},1,1,64,{slot+4}',
                       f'F,{high+6},{WEAPON},{0x1F0013F5},1,1,64,{slot+5},0,0,1,10',
                       f'F,{high+7},{JACKET},{JACKET_VISUAL},1,1,2,{slot+6},10,321,1,0',
                       f'F,{high+8},{JACKET},{JACKET_VISUAL},1,1,2,{slot+7},10,457,1,0']
            with (data / 'characters.journal').open('ab') as journal:
                for command in records:
                    record = f'i1\t{ids[0]}\t{command}'.encode()
                    checksum = 2166136261
                    for byte in record:
                        checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
                    journal.write(record + f'\t{checksum:08x}\n'.encode())
            for index in range(4):
                edit(index, 'player-state', 'x,10000')
                edit(index, 'player-state', 'd,150')
            for skill in (0x59, 0x55, 0xDA, DIRECT):
                edit(0, 'skill-state', f't,{skill:x}')
            # Train the normal prerequisite chain in the disposable world.
            catalog = {int(row[0], 16): row for line in (ROOT / 'AC2E/emulator/content/skills.tsv').read_text().splitlines()
                       if line and line[0] in '0123456789abcdef' for row in [line.split('\t')]}
            def learn(index, wanted, trained):
                if wanted in trained:
                    return
                row = catalog[wanted]
                for dep in row[10].split(',') if row[10] != '-' else ():
                    learn(index, int(dep.split(':')[0], 16), trained)
                if int(row[6]):
                    edit(index, 'skill-state', f't,{wanted:x}')
                trained.add(wanted)
            for index in (0, 2, 3):
                edit(index, 'player-state', 'x,100000000')
            learn(0, 0x57, {0x55, 0x59, 0xda, DIRECT})
            lugian_skills = set()
            learn(3, 0x109, lugian_skills)
            learn(3, 0x114, lugian_skills)
            learn(2, 0x31d, set())
            # Co-locate the racial starts so the second client witnesses the combat pet.
            import struct
            pos = ','.join(f'{struct.unpack("<I", struct.pack("<f", x))[0]:08x}' for x in (59, 240.199, -44.606205))
            with (data / 'characters.journal').open('ab') as journal:
                for character in ids[2:]:
                    record = f'p2\t{character}\t02990109,{pos},0'.encode()
                    checksum = 2166136261
                    for byte in record:
                        checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
                    journal.write(record + f'\t{checksum:08x}\n'.encode())
            server = start()
            caster, primary, secondary, bystander = [join(i) for i in range(4)]
            original = str(high + 4)
            replacement = str(high + 9)

            inspect(caster, str(high + 5))
            chat(caster, '@bind', 'crafted weapon in your backpack')
            assert 'Weapon damage: 10' in inspect(caster, original)
            mark = len(caster.events)
            chat(caster, '@bind', 'infused the weapon with +3 damage')
            until(caster, mark, 'remove', lambda e: e['id'] == original)
            until(caster, mark, 'remove', lambda e: e['id'] == str(high + 1))
            until(caster, mark, 'object', lambda e: e['id'] == replacement and e['entity'] == WEAPON)
            time.sleep(1.05)
            assert 'Weapon damage: 13' in inspect(caster, replacement)
            mark = len(caster.events)
            chat(caster, '@bind', 'cannot hold another infusion')
            assert not any(e.get('event') == 'remove' and e.get('id') == str(high + 2) for e in caster.events[mark:])

            caster.send('fellowship', action='create', name='Ward Keepers')
            until(caster, 0, 'fellowship', lambda e: len(e['members']) == 1)
            for count, member in enumerate((primary, secondary), 2):
                a, b = len(caster.events), len(member.events)
                caster.send('fellowship', action='invite', id=member.id)
                until(member, b, 'fellowship_invitation')
                member.send('fellowship', action='accept', id=caster.id)
                until(caster, a, 'fellowship', lambda e: len(e['members']) == count)
            staff = next(e for e in caster.events if e.get('entity') == 0x470007E0 and e.get('container') == caster.id)
            mark = len(caster.events)
            caster.send('use', item=staff['id'], target='0')
            until(caster, mark, 'inventory_result', lambda e: e['id'] == staff['id'] and e['status'] == 0)
            chat(caster, '@area on', 'Area healing enabled')
            caster_mark = len(caster.events)
            observed = [(p, len(p.events), stat(p, HEALTH)) for p in (primary, secondary, bystander)]
            caster.send('skill', skill=0x55, target=primary.id)
            until(caster, caster_mark, 'chat', lambda e: 'area heal reaches 1' in str(e))
            for (p, mark, before), delta in zip(observed[:2], (25, 12)):
                until(p, mark, 'fx', lambda e: e.get('id') == p.id and e.get('fx') == 0x41000033)
                until(p, mark, 'quality', lambda e: e.get('id') is None and e['key'] == HEALTH and e['value'] >= before + delta)
                assert has_health_delta(p, mark, before, delta)
            p, mark, before = observed[-1]
            assert not has_health_delta(p, mark, before, 12)

            caster_mark, visible_mark = len(caster.events), len(primary.events)
            vigor, health = stat(caster, VIGOR), stat(caster, HEALTH)
            caster.send('use', item=str(high + 3), target='0')
            until(caster, caster_mark, 'chat', lambda e: 'You summon a Healing Ward' in str(e))
            def ward(e):
                return e.get('summoner') == caster.id and e.get('name', {}).get('literal') == 'Healing Ward'
            obj = until(caster, caster_mark, 'object', ward)
            until(primary, visible_mark, 'object', ward)
            until(caster, caster_mark, 'quality', lambda e: e.get('id') is None and e['key'] == VIGOR and e['value'] <= vigor - 18)
            chat(caster, '@ward', 'already active')
            until(caster, caster_mark, 'quality', lambda e: e.get('id') is None and e['key'] == HEALTH
                  and has_health_delta(caster, caster_mark, health, 5))
            until(primary, visible_mark, 'remove', lambda e: e['id'] == obj['id'], timeout=18)
            visible_mark = len(primary.events)
            chat(caster, '@ward', 'You summon a Healing Ward')
            renewed = until(primary, visible_mark, 'object', ward)
            assert renewed['id'] == obj['id'] and renewed['instance'] > obj['instance']
            caster.send('logout', id=caster.id)
            caster.wait('selection')
            until(primary, visible_mark, 'remove', lambda e: e['id'] == renewed['id'] and e['instance'] == renewed['instance'])

            shutdown()
            for p in (caster, primary, secondary, bystander):
                p.close()
            server = start()
            caster = join(0)
            assert 'Weapon damage: 13' in inspect(caster, replacement)
            assert any(e.get('id') == str(high + 2) and e.get('entity') == CHARM for e in caster.events)
            assert not any(e.get('summoner') == caster.id for e in caster.events)
            mark = len(caster.events)
            chat(caster, '@extract', 'The source weapon was destroyed')
            totem = until(caster, mark, 'object', lambda e: e.get('entity') == TOTEM and e.get('container') == caster.id)
            until(caster, mark, 'remove', lambda e: e['id'] == replacement)
            assert 'Weapon infusion: +3 damage' in inspect(caster, totem['id'])
            # A restart must retain the charge, not silently turn it into an empty totem.
            shutdown()
            caster.close()
            server = start()
            caster, watcher = join(0), join(1)
            assert 'Weapon infusion: +3 damage' in inspect(caster, totem['id'])
            time.sleep(.2)
            inspect(caster, str(high + 5))  # Same base type, but uncrafted.
            chat(caster, '@transfer', 'crafted weapon in your backpack')
            # A fresh, crafted receiver lives in the bag after the staged seed below.
            receiver = str(high + 6)
            inspect(caster, receiver)
            mark = len(caster.events)
            chat(caster, '@transfer', 'totem transferred its +3 damage')
            transferred = until(caster, mark, 'object', lambda e: e.get('entity') == WEAPON and e.get('container') == caster.id)
            until(caster, mark, 'remove', lambda e: e['id'] == totem['id'])
            assert 'Weapon damage: 13' in inspect(caster, transferred['id'])

            watch = len(watcher.events)
            chat(caster, '@companion', 'You summon a Healing Companion')
            companion = until(watcher, watch, 'object', lambda e: e.get('summoner') == caster.id)
            caster.send('move', cell=0x02990109, position=[56, 240.199, -44.606205], heading=0, motion=[0,0,0])
            until(watcher, watch, 'position', lambda e: e['id'] == companion['id']
                  and math.dist(e['position'], companion['placement']['position']) > .1)
            caster.send('move', cell=0x02990109, position=[59, 240.199, -44.606205], heading=0, motion=[0,0,0])

            bow = next(e for e in caster.events if e.get('entity') == 0x470007ED and e.get('container') == caster.id)
            mark = len(caster.events)
            caster.send('use', item=bow['id'], target='0')
            until(caster, mark, 'inventory_result', lambda e: e['id'] == bow['id'] and e['status'] == 0)
            chat(caster, '@cleave on', 'Cleave enabled')
            for iid in DUMMIES:
                until(caster, 0, 'object', lambda e: e['id'] == iid)
            for attempt in range(6):
                mark = len(caster.events)
                caster.send('skill', skill=DIRECT, target=DUMMIES[0])
                until(caster, mark, 'story', lambda e: e.get('request') is not None and e.get('skill') == DIRECT)
                try:
                    until(caster, mark, 'hit', lambda e: e['id'] == DUMMIES[1] and e['damage'] > 0, timeout=2.5)
                    break
                except AssertionError:
                    if attempt == 5:
                        raise
            strikes = [e['id'] for e in caster.events[mark:] if e['event'] == 'hit' and e.get('attacker') == caster.id]
            assert strikes == list(DUMMIES[:2]), strikes
            assert sum(e.get('event') == 'story' and e.get('request') is not None for e in caster.events[mark:]) == 1

            # Exercise the DAT-defined area attack with the normal hotbar request and cleave disabled.
            chat(caster, '@cleave off', 'Cleave disabled')
            caster.send('combat', enabled=False)
            staff = next(e for e in caster.events if e.get('entity') == 0x470007E0 and e.get('container') == caster.id)
            mark = len(caster.events)
            caster.send('use', item=staff['id'], target='0')
            until(caster, mark, 'inventory_result', lambda e: e['id'] == staff['id'] and e['status'] == 0)
            for attempt in range(4):
                mark = len(caster.events)
                caster.send('skill', skill=0x57, target=DUMMIES[0])
                until(caster, mark, 'story', lambda e: e.get('request') is not None and e.get('skill') == 0x57)
                until(caster, mark, 'fx', lambda e: e.get('id') == DUMMIES[2] and e.get('fx') == 0x41000490)
                try:
                    until(caster, mark, 'hit', lambda e: e['id'] == DUMMIES[2] and e['damage'] == 2, timeout=3)
                    break
                except AssertionError:
                    if attempt == 3: raise
                    time.sleep(7.1)
            assert not any(e.get('id') == DUMMIES[3] and e['event'] in ('hit', 'fx') for e in caster.events[mark:])

            # Armor retains its color and owns a distinct transferable charge.
            caster.send("combat", enabled=False)
            time.sleep(1.1)
            inspect(caster, str(high + 7))
            mark = len(caster.events)
            chat(caster, '@bind', 'infused the armor')
            armor = until(caster, mark, 'object', lambda e: e.get('entity') == JACKET and e.get('container') == caster.id)
            assert 'Armor: 13' in inspect(caster, armor['id'])
            time.sleep(1.05)
            mark = len(caster.events)
            chat(caster, '@extract', 'source armor was destroyed')
            armor_totem = until(caster, mark, 'object', lambda e: e.get('entity') == TOTEM)
            assert 'Armor infusion: +3 armor' in inspect(caster, armor_totem['id'])
            time.sleep(1.05)
            mark = len(caster.events)
            caster.send('use', item=armor_totem['id'], target=transferred['id'])
            until(caster, mark, 'chat', lambda e: 'does not match' in str(e))
            mark = len(caster.events)
            caster.send('use', item=armor_totem['id'], target=str(high + 8))
            until(caster, mark, 'remove', lambda e: e['id'] == armor_totem['id'])
            receiver = until(caster, mark, 'object', lambda e: e.get('entity') == JACKET and e.get('container') == caster.id)
            assert 'Armor: 13' in inspect(caster, receiver['id'])

            lugian = join(3)
            weapon = next(e for e in lugian.events if e.get('entity') == 0x47000809 and e.get('container') == lugian.id)
            ownmark = len(lugian.events)
            lugian.send('use', item=weapon['id'], target='0')
            until(lugian, ownmark, 'inventory_result', lambda e: e['id'] == weapon['id'] and e['equipped'] == 0x40)
            mark = len(watcher.events)
            lugian.send('skill', skill=0x109, target='0')
            sand = until(watcher, mark, 'object', lambda e: e.get('entity') == 0x47001302 and e.get('summoner') == lugian.id)
            ownmark = len(lugian.events)
            lugian.send('skill', skill=0x109, target='0')
            until(lugian, ownmark, 'skill_finished')
            assert sum(e.get('summoner') == lugian.id and e['event'] == 'object' for e in watcher.events[mark:]) == 1
            # Watch the summoned creature walk after its owner before either starts attacking.
            destination = [56, 240.199, -44.606205]
            before = sand['placement']['position']
            motion = len(watcher.events)
            lugian.send('move', cell=0x02990109, position=destination, heading=0, motion=[0, 0, 0])
            until(watcher, motion, 'position', lambda e: e['id'] == sand['id']
                  and math.dist(e['position'], destination) < math.dist(before, destination) - .15)
            lugian.send('move', cell=0x02990109, position=[59, 240.199, -44.606205], heading=0, motion=[0, 0, 0])
            # Start within melee reach and identify the pet by its attacker IID.
            lugian.send('attack', id=DUMMIES[0])
            until(watcher, mark, 'hit', lambda e: e['id'] == DUMMIES[0] and e.get('attacker') == sand['id'] and 0 < e['damage'] <= 5, timeout=35)
            lugian.send('chat', text='@dismiss', channel='say')
            until(watcher, mark, 'remove', lambda e: e['id'] == sand['id'])

            tumerok = join(2)
            for peer, implement, skill, effect, count in (
                    (lugian, 0x47000808, 0x114, 0x410002e8, 3),
                    (tumerok, 0x470007f6, 0x31d, 0x8100025a, 2)):
                peer.send('combat', enabled=False)
                item = next(e for e in peer.events if e.get('container') == peer.id and e.get('entity') == implement)
                gear = len(peer.events)
                peer.send('use', item=item['id'], target='0')
                until(peer, gear, 'inventory_result', lambda e: e['id'] == item['id'] and e['equipped'] == 0x40)
                time.sleep(1.05)  # The preceding summon/attack can still own the global cooldown.
                mark = len(watcher.events)
                ownmark = len(peer.events)
                peer.send('skill', skill=skill, target=DUMMIES[0])
                until(peer, ownmark, 'story', lambda e: e.get('skill') == skill and e.get('request') is not None)
                for dummy in DUMMIES[:count]:
                    until(watcher, mark, 'fx', lambda e: e.get('id') == dummy and e.get('fx') == effect)
                time.sleep(.25)
                impacts = {e['id'] for e in watcher.events[mark:] if e['event'] == 'fx' and e.get('fx') == effect}
                assert impacts == set(DUMMIES[:count]), (skill, impacts)
            result = {'native_earthquake_10m_and_volley_5m': True, 'native_explosion_area_and_secondary_burn': True, 'armor_extract_and_targeted_transfer': True,
                      'sand_fiend_hotbar_cast_walk_assist_and_dismiss': True, 'totem_charge_persists_and_transfers': True, 'infusion_source_destroyed': True,
                      'companion_position_replicates': True, 'offensive_area_hits_only_nearby_creatures': True,
                      'infusion_consume_and_replay': True, 'ineligible_and_repeat_infusions_rejected': True,
                      'area_vitals': [25, 12, 0], 'secondary_heal_fx': True,
                      'ward_owner_observer_and_expiry': True, 'ward_cannot_stack': True,
                      'ward_logout_cleanup_and_instance_refresh': True}
            (run / 'result.json').write_text(json.dumps(result, indent=2))
            print(json.dumps(result), flush=True)
        finally:
            for i, p in enumerate(peers):
                (run / f'peer-{i}.json').write_text(json.dumps(p.events, indent=2))
                p.close()
            if server and server.poll() is None:
                shutdown()


if __name__ == '__main__':
    main()
