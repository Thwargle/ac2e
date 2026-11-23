"""Selected-player healing and common mastery actions through native UDP.

All characters, training XP, damage and positions are disposable test fixtures.
The installed DAT supplies skill requirements, costs and heal amount.
"""
import json
import os
import subprocess
import time
import uuid
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer
from test_services import fixture, VENDOR
from test_gameplay import until
from prepare_content import ContentProfile


def main():
    run = ROOT / 'Saved/AC2NetworkTests' / ('support-skills-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    content = fixture(run, isolated=True)
    # The combat runtime owns cross-player healing and needs one placed actor.
    # Keep this test-only idol outside aggro range of both characters.
    profile = ContentProfile(DAT_DIRECTORY)
    try:
        with content.open('a') as out:
            out.write(f'8140000000030e00\t470007d1\t{profile.visual(0x470007d1):08x}\t02990109\t140\t240.199\t-44.606205\t1\t0\t0\t0\t-\n')
    finally:
        profile.close()
    env = dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1')
    peers, ids = [], []
    server = None
    with (run / 'server.log').open('w') as log:
        args = [str(SERVER), 'serve', '--bind', '127.0.0.1:19390', '--account', 'local',
                '--data', str(run / 'world'), '--game-data', str(DAT_DIRECTORY),
                '--content', str(content), '--initialize-ui', 'true', '--lifestones', 'false', '--portals', 'false', '--seconds', '100']

        def start():
            p = subprocess.Popen(args, env=env, stdout=log, stderr=log, creationflags=0x08000000)
            time.sleep(1)
            assert p.poll() is None, (run / 'server.log').read_text()
            return p

        def join(index, create=False):
            p = Peer(); peers.append(p)
            p.send('connect', host='127.0.0.1', port=19390, account='local',
                   dat_directory=str(DAT_DIRECTORY)); p.wait('characters')
            if create:
                p.send('create', name=['Support Healer', 'Support Recipient'][index],
                       race=1, sex=4096, start_area=0, physical=[[1, 1], [2, 1]])
                ids.append(p.wait('created')['character']['id'])
            p.id = ids[index]
            p.send('enter', id=p.id); p.wait('player'); p.send('ready')
            p.wait('status', lambda e: e['phase'] == 'world'); until(p, 0, 'inventory')
            return p

        def stat(p, key):
            return next(e['value'] for e in reversed(p.events)
                        if e.get('event') == 'quality' and e.get('id') is None and e['key'] == key)

        def edit(index, command, event):
            subprocess.run([str(SERVER), command, '--account', 'local', '--data', str(run / 'world'),
                            '--character-id', ids[index], '--event', event], check=True,
                           env=env, stdout=log, stderr=log, creationflags=0x08000000)

        try:
            server = start()
            for i in range(2):
                p = join(i, create=True); p.send('logout', id=p.id); p.wait('selection'); p.close()
            peers.clear()
            (run / 'world/stop.request').write_text('stop\n'); server.wait(timeout=10)
            edit(0, 'player-state', 'x,10000')
            edit(1, 'player-state', 'x,10000')
            edit(0, 'skill-state', 't,59')
            edit(0, 'skill-state', 't,55')
            edit(0, 'skill-state', 't,64')
            edit(0, 'skill-state', 't,67')  # Timed vigor regeneration.
            for i in range(2): edit(i, 'player-state', 'd,150')
            server = start()
            healer, recipient = join(0), join(1)
            until(healer, 0, 'object', lambda e: e['id'] == recipient.id)
            staff = next(e for e in healer.events if e.get('event') == 'object'
                         and e.get('container') == healer.id and e.get('entity') == 0x470007e0)
            mark = len(healer.events); healer.send('use', item=staff['id'], target='0')
            until(healer, mark, 'inventory_result', lambda e: e['id'] == staff['id'] and e['status'] == 0 and e['equipped'] == 0x40)
            # These IDs are shared masteries (Race=0), not NPC or kingdom grants.
            for skill in (4, 10, 15):
                mark = len(healer.events); healer.send('skill_train', skill=skill)
                until(healer, mark, 'skills', lambda e: any(s['id'] == skill and s['trained'] for s in e['items']))
                time.sleep(.15)  # Respect the server's 100 ms advancement throttle.
                mark = len(healer.events); healer.send('skill_raise', skill=skill)
                until(healer, mark, 'skills', lambda e: any(s['id'] == skill and int(s['xp']) > 0 for s in e['items']))

            before, caster_before = stat(recipient, 0x102), stat(healer, 0x102)
            ma, mb = len(healer.events), len(recipient.events)
            healer.send('target', id=recipient.id)
            healer.send('skill', skill=0x55, target=recipient.id)
            until(healer, ma, 'chat', lambda e: 'restore 25 health on your target' in str(e))
            until(healer, ma, 'story', lambda e: e.get('request') is not None)
            until(healer, ma, 'combat', lambda e: e['enabled'])
            for p, mark in ((healer, ma), (recipient, mb)):
                until(p, mark, 'story', lambda e: e['id'] == healer.id and e.get('request') is not None)
                timeline = p.events[mark:]
                stance = next(i for i, e in enumerate(timeline) if e.get('event') == 'mode'
                              and e['id'] == healer.id and e['mode'] == 0x41000011)
                cast = next(i for i, e in enumerate(timeline) if e.get('event') == 'story'
                            and e['id'] == healer.id and e.get('request') is not None)
                assert stance < cast, timeline
            until(recipient, mb, 'fx', lambda e: e['id'] == healer.id and e['fx'] == 0x810001d0)
            until(recipient, mb, 'fx', lambda e: e['id'] == healer.id and e['fx'] == 0x410004a8)
            until(recipient, mb, 'fx', lambda e: e['id'] == recipient.id and e['fx'] == 0x41000033)
            assert not any(e.get('event') == 'fx' and e['id'] == healer.id and e['fx'] == 0x41000033
                           for e in recipient.events[mb:])

            until(recipient, mb, 'quality', lambda e: e.get('id') is None and e['key'] == 0x102 and e['value'] >= before + 25)
            changes = [before] + [e['value'] for e in recipient.events[mb:]
                                  if e.get('event') == 'quality' and e.get('id') is None and e['key'] == 0x102]
            assert any(b - a == 25 for a, b in zip(changes, changes[1:])), changes
            assert stat(healer, 0x102) < caster_before + 25, healer.events[ma:]
            # Rejection while recovering must not raise a peaceful character's
            # weapon, spend vigor or restart the native reset timestamp.
            mark = len(healer.events); healer.send('combat', enabled=False)
            until(healer, mark, 'combat', lambda e: not e['enabled'])
            mark = len(healer.events); healer.send('skill', skill=0x55, target=recipient.id)
            until(healer, mark, 'skill_finished')
            assert not any(e.get('event') == 'combat' and e['enabled'] for e in healer.events[mark:])
            assert not any(e.get('event') == 'story' and e.get('request') is not None for e in healer.events[mark:])
            assert any(e.get('event') == 'chat' and 'recovering' in str(e).lower() for e in healer.events[mark:])
            # Preserve the retail-friendly fallback when scenery remains selected.
            time.sleep(7.1)
            before = stat(healer, 0x102); mark = len(healer.events)
            healer.send('target', id=VENDOR)
            healer.send('skill', skill=0x55, target=VENDOR)
            until(healer, mark, 'story', lambda e: e.get('request') is not None)
            until(healer, mark, 'combat', lambda e: e['enabled'])
            until(healer, mark, 'quality', lambda e: e.get('id') is None and e['key'] == 0x102 and e['value'] >= before + 25)
            until(healer, mark, 'fx', lambda e: e['id'] == healer.id and e['fx'] == 0x41000033)
            self_fx = [e['fx'] for e in healer.events[mark:] if e.get('event') == 'fx' and e['id'] == healer.id]
            assert self_fx.index(0x810001d0) < self_fx.index(0x410004a8) < self_fx.index(0x41000033), self_fx
            assert not any(e.get('event') == 'chat' and 'not available' in str(e) for e in healer.events[mark:])
            # Self selection from the roster is also a valid native selection.
            mark = len(healer.events); healer.send('target', id=healer.id)
            until(healer, mark, 'script', lambda e: e['function'] == 0x8212002c)
            time.sleep(2.1)  # Release the shared cooldown from the preceding heal.
            mark = len(healer.events)
            healer.send('skill', skill=0x67, target=healer.id)
            event = until(healer, mark, 'status_effects', lambda e: any(
                row['did'] == 0x6f000333 for row in e.get('effects', [])))
            buff = next(row for row in event['effects'] if row['did'] == 0x6f000333)
            assert buff['eid'] > 0 and buff['duration'] == 180 and buff['flags'] & 2, buff
            assert buff['icon'] and buff['name'], buff
            report = {'shared_masteries_train_and_raise': [4, 10, 15],
                      'timed_regeneration_in_owner_vitals': True,
                      'selected_player_healed': 25, 'caster_and_recipient_effects': True, 'caster_not_healed_instead': True,
                      'scenery_selection_self_heals': True, 'self_cast_release_victim_fx_order': True, 'self_selection': True,
                      'support_stance_before_story_for_both_observers': True,
                      'rejected_cast_preserves_peace': True, 'self_heal_enters_stance': True}
            (run / 'result.json').write_text(json.dumps(report, indent=2))
            print(json.dumps(report)); print(run)
        finally:
            for i, p in enumerate(peers):
                (run / f'peer-{i}.json').write_text(json.dumps(p.events, indent=2)); p.close()
            if server and server.poll() is None:
                (run / 'world/stop.request').write_text('stop\n'); server.wait(timeout=10)


if __name__ == '__main__':
    main()
