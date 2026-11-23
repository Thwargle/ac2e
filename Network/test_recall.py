"""Three local UDP clients verify Lifestone Recall and ordinary portal effects."""
import json
import math
import os
import subprocess
import time
import uuid

from test_gameplay import until
from test_network import DAT_DIRECTORY, Peer, ROOT, SERVER


PORT = 19594
CHARGE, RELEASE, PORTAL_RECALL = 0x410000BF, 0x410000D8, 0x410000D5
ENTER_WORLD_FX = 0x41000010
STONE = str(0x8140000000030001)
IDOL = str(0x8140000000030000)
PORTAL = str(0x81400000000F0001)
PORTAL_USE, ACTIVATE = 0x410004FA, 0x41000044


def enter(peer):
    mark = len(peer.events)
    peer.send('enter', id=peer.id)
    until(peer, mark, 'player')
    placement = until(peer, mark, 'object', lambda e: e['id'] == peer.id)['placement']
    peer.send('ready')
    until(peer, mark, 'status', lambda e: e['phase'] == 'world')
    until(peer, mark, 'inventory')
    return placement


def check(peers):
    hero, observer, late = peers
    pose = enter(hero)
    enter(observer)
    until(observer, 0, 'object', lambda e: e['id'] == hero.id)
    until(hero, 0, 'object', lambda e: e['id'] == STONE)
    bound = list(pose['position'])
    bound[0] -= 1

    def move(position):
        mark = len(observer.events)
        hero.send('move', cell=pose['cell'], position=position, heading=0, motion=[0, 0, 0])
        until(observer, mark, 'position', lambda e: e['id'] == hero.id and math.dist(e['position'], position) < .01)

    def action(peer, mark, kind, value, field='behavior'):
        return until(peer, mark, kind, lambda e: e.get('id') == hero.id and e.get(field) == value)

    def begin():
        marks = [len(p.events) for p in peers]
        hero.send('recall')
        request = until(hero, marks[0], 'skill_requested')['request']
        for peer, mark in zip([hero, observer], marks):
            action(peer, mark, 'mode', 0x41000011, 'mode')
            action(peer, mark, 'behavior', CHARGE)
        return marks, request

    def leave(peer):
        mark = len(peer.events)
        peer.send('logout', id=peer.id)
        until(peer, mark, 'selection')

    def interrupted(marks, request):
        for peer, mark in zip([hero, observer], marks):
            action(peer, mark, 'stop_behavior', CHARGE)
        until(hero, marks[0], 'skill_finished', lambda e: e['request'] == request)
        until(hero, marks[0], 'chat', lambda e: 'recall interrupted' in str(e))
        assert not any(e['event'] == 'position' and e.get('id') == hero.id and e.get('teleport', 0) > 0
                       for e in hero.events[marks[0]:])

    move(bound)
    hero.send('use', item=STONE, target='0')
    until(hero, 0, 'chat', lambda e: 'bound to this lifestone' in str(e))
    time.sleep(5.2)
    marks, request = begin()
    time.sleep(1.5)
    enter(late)
    until(late, marks[2], 'object', lambda e: e['id'] == hero.id)
    action(late, marks[2], 'mode', 0x41000011, 'mode')
    action(late, marks[2], 'behavior', CHARGE)
    move(pose['position'])
    interrupted(marks, request)
    action(late, marks[2], 'stop_behavior', CHARGE)
    leave(late)

    marks, request = begin()
    hero.send('attack', id=IDOL)
    interrupted(marks, request)
    until(hero, marks[0], 'quality', lambda e: e.get('id') == IDOL and e['key'] == 0x102 and e['value'] == 0)
    mark = len(hero.events)
    hero.send('combat', enabled=False)
    until(hero, mark, 'combat', lambda e: not e['enabled'])

    marks = [len(p.events) for p in peers]
    hero.send('chat', channel='say', text='@recall')
    for peer, mark in zip([hero, observer], marks):
        action(peer, mark, 'behavior', CHARGE)
    leave(hero)
    until(observer, marks[1], 'remove', lambda e: e['id'] == hero.id)
    returned = enter(hero)
    assert math.dist(returned['position'], pose['position']) < .01, returned
    until(observer, marks[1], 'object', lambda e: e['id'] == hero.id)
    # First accepted movement clears the login FX. This isolates the identical FX
    # subsequently started by recall from the one applied during world entry.
    move(pose['position'])
    action(observer, marks[1], 'stop_fx', ENTER_WORLD_FX, 'fx')

    # Moving after release must clear the glow and pending request before travel.
    marks, request = begin()
    for peer, mark in zip([hero, observer], marks):
        action(peer, mark, 'fx', ENTER_WORLD_FX, 'fx')
    displaced = list(pose['position'])
    displaced[0] += 1
    move(displaced)
    for peer, mark in zip([hero, observer], marks):
        action(peer, mark, 'stop_fx', ENTER_WORLD_FX, 'fx')
        action(peer, mark, 'stop_behavior', RELEASE)
    until(hero, marks[0], 'skill_finished', lambda e: e['request'] == request)
    until(hero, marks[0], 'chat', lambda e: 'recall interrupted' in str(e))
    assert not any(e['event'] == 'position' and e.get('id') == hero.id and e.get('teleport', 0) > 0
                   for e in hero.events[marks[0]:])

    start = time.monotonic()
    marks, request = begin()
    for peer, mark in [(hero, marks[0]), (observer, marks[1])]:
        action(peer, mark, 'stop_behavior', CHARGE)
        action(peer, mark, 'fx', ENTER_WORLD_FX, 'fx')
        action(peer, mark, 'behavior', RELEASE)
    action(hero, marks[0], 'behavior', PORTAL_RECALL)
    until(hero, marks[0], 'story', lambda e: e['id'] == hero.id and e['request'] == request)
    released = time.monotonic() - start
    enter(late)
    until(late, marks[2], 'object', lambda e: e['id'] == hero.id)
    for peer, mark in zip(peers, marks):
        recalled = until(peer, mark, 'position', lambda e: e['id'] == hero.id and e.get('teleport', 0) > 0)
        assert recalled['cell'] == pose['cell'] and math.dist(recalled['position'], bound) < .01, recalled
    elapsed = time.monotonic() - start
    until(hero, marks[0], 'skill_finished', lambda e: e['request'] == request)
    assert not any(e['event'] == 'story' and e.get('id') == hero.id and e.get('request') == request
                   for e in observer.events[marks[1]:])
    assert released >= 7.8 and elapsed >= 9.8, (released, elapsed)
    late_events = late.events[marks[2]:]
    arrival = next(i for i, e in enumerate(late_events) if e['event'] == 'position' and e['id'] == hero.id and e.get('teleport', 0) > 0)
    assert any(e['event'] == 'object' and e['id'] == hero.id for e in late_events[:arrival])
    assert any(e['event'] == 'fx' and e.get('id') == hero.id and e.get('fx') == ENTER_WORLD_FX
               for e in late_events[:arrival]), 'late observer missed Enter_World before arrival'
    leave(late)
    mark = len(late.events)
    enter(late)
    until(late, mark, 'object', lambda e: e['id'] == hero.id)
    until(late, mark, 'mode', lambda e: e['id'] == hero.id and e['mode'] == 0x40000001)
    action(late, mark, 'fx', ENTER_WORLD_FX, 'fx')
    movement = [len(p.events) for p in peers]
    move(pose['position'])
    for peer, index in zip(peers, movement):
        action(peer, index, 'stop_fx', ENTER_WORLD_FX, 'fx')
    leave(late)
    mark = len(late.events)
    enter(late)
    until(late, mark, 'object', lambda e: e['id'] == hero.id)
    assert not any(e.get('id') == hero.id and (e.get('behavior') in (CHARGE, RELEASE, PORTAL_RECALL)
                   or e.get('fx') == ENTER_WORLD_FX) for e in late.events[mark:])
    # A short route keeps the witness in both visibility sets. The gameplay
    # suite covers leaving interest on the normal cross-block starter portal.
    leave(late)
    portal = until(hero, 0, 'object', lambda e: e['id'] == PORTAL)
    move(portal['placement']['position'])
    time.sleep(5.1)  # Successful recall and ordinary portal use share a travel cooldown.
    departure = [(hero.id, PORTAL_USE), (hero.id, ENTER_WORLD_FX), (PORTAL, ACTIVATE)]
    marks = [len(p.events) for p in peers]
    started = time.monotonic()
    hero.send('use', item=PORTAL, target='0')
    for peer, mark in zip([hero, observer], marks):
        until(peer, mark, 'fx', lambda e: e.get('id') == PORTAL and e['fx'] == ACTIVATE)
        seen = [(e.get('id'), e['fx']) for e in peer.events[mark:] if e['event'] == 'fx']
        assert [pair for pair in seen if pair in departure] == departure, seen
    # Early or unrelated readiness must not end the traveler's persistent FX.
    hero.send('ready')
    observer.send('ready')
    displaced = list(portal['placement']['position'])
    displaced[1] += 1
    def try_move_while_loading():
        hero.send('move', cell=pose['cell'], position=displaced, heading=0, motion=[0, 0, 0])
    try_move_while_loading()
    for peer, mark in zip([hero, observer], marks):
        arrived = until(peer, mark, 'position', lambda e: e['id'] == hero.id and e['teleport'] == 2)
        assert math.dist(arrived['position'], [61, 240, -44.606205]) < .01, arrived
    travel_seconds = time.monotonic() - started
    assert travel_seconds >= 1.4, travel_seconds
    try_move_while_loading()
    enter(late)
    until(late, marks[2], 'object', lambda e: e.get('id') == hero.id)
    action(late, marks[2], 'fx', ENTER_WORLD_FX, 'fx')
    # The two activation bursts are one-shots. A new witness sees only the
    # traveler's active glow while the destination is still loading.
    assert not any(e.get('event') == 'fx' and (e.get('id'), e.get('fx')) in
                   [(hero.id, PORTAL_USE), (PORTAL, ACTIVATE)] for e in late.events[marks[2]:])
    assert not any(e.get('event') == 'stop_fx' and e.get('id') == hero.id and
                   e.get('fx') == ENTER_WORLD_FX for p, i in zip(peers, marks) for e in p.events[i:])
    hero.send('ready')
    for peer, mark in zip(peers, marks):
        action(peer, mark, 'stop_fx', ENTER_WORLD_FX, 'fx')
    # Retail sends the three environmental calls privately before starting the
    # public effects. ExitPortalSpace precedes the force-position packet, while
    # movement unlocks only after the client has finished rendering that scene.
    def portal_events(events):
        ordered = []
        for e in events:
            kind = e['event']
            if kind in ('portal_space', 'portal_flag'):
                ordered.append((kind, e['active']))
            elif kind == 'portal_movement_lock':
                ordered.append((kind, e['locked']))
            elif kind == 'fx' and (e['id'], e['fx']) in departure:
                assert e['strength'] == 1, e
                ordered.append((kind, e['fx']))
            elif kind == 'stop_fx' and e['id'] == hero.id and e['fx'] == ENTER_WORLD_FX:
                ordered.append((kind, e['fx']))
            elif kind == 'position' and e['id'] == hero.id and e['teleport'] == 2:
                ordered.append(('destination', 2))
        return ordered

    bursts = [('fx', fx) for _, fx in departure]
    arrival = [('destination', 2)]
    glow_cleared = [('stop_fx', ENTER_WORLD_FX)]
    expected = ([('portal_flag', True), ('portal_movement_lock', True), ('portal_space', True)]
                + bursts + [('portal_space', False), ('portal_flag', False)] + arrival
                + [('portal_movement_lock', False)] + glow_cleared)
    owner_events = portal_events(hero.events[marks[0]:])
    assert owner_events == expected, owner_events
    witness_events = portal_events(observer.events[marks[1]:])
    assert witness_events == bursts + arrival + glow_cleared, witness_events
    arriving_events = portal_events(late.events[marks[2]:])
    assert arriving_events == [('fx', ENTER_WORLD_FX)] + glow_cleared, arriving_events
    assert not any(e['event'] == 'position' and e.get('id') == hero.id and
                   math.dist(e['position'], displaced) < .01 for e in observer.events[marks[1]:])
    move(displaced)  # The same movement becomes visible as soon as the traveler is unlocked.
    return {'release_seconds': round(released, 2), 'teleport_seconds': round(elapsed, 2),
            'ordinary_portal_seconds': round(travel_seconds, 2)}


def main():
    run = ROOT/'Saved/AC2NetworkTests'/('recall-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    encounters = run/'encounters.tsv'
    encounters.write_text('pyrealium-encounters-v1\nplayer\t100\t1\t1000\t3\nregen\t0\t0\t1000\n'
                          'npc\t470007d1\t1\t1\t1500\t3\t300000\t1\t0\n')
    content = run/'world.tsv'
    fixture = ROOT/'AC2E/emulator/tests/fixtures/human-practice-world.tsv'
    content.write_text(fixture.read_text().rstrip() +
                       '\n81400000000f0001\t47001454\t1f000d09\t02990109\t59\t240\t-44.606205\t1\t0\t0\t0\t-\n')
    content.with_suffix('.routes.tsv').write_text('pyrealium-portal-routes-v1\n'
        '81400000000f0001\t47001454\t02990109\t59\t240\t-44.606205\t02990109\t61\t240\t-44.606205\t0\t1\n')
    peers = []
    with (run/'server.log').open('w', encoding='utf-8') as log:
        server = subprocess.Popen([str(SERVER), 'serve', '--bind', f'127.0.0.1:{PORT}', '--account', 'local',
            '--data', str(run/'world'), '--game-data', str(DAT_DIRECTORY),
            '--content', str(content),
            '--encounters', str(encounters), '--initialize-ui', 'true', '--lifestones', 'true',
            '--portals', 'true', '--seconds', '150'], stdout=log, stderr=log, creationflags=0x08000000,
            env=dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1'))
        try:
            time.sleep(1)
            assert server.poll() is None, (run/'server.log').read_text()
            for name in ['Recall Tester', 'Recall Observer', 'Recall Arrival']:
                p = Peer()
                peers.append(p)
                p.send('connect', host='127.0.0.1', port=PORT, account='local')
                p.wait('characters')
                p.send('create', name=name, start_area=0, race=1, sex=4096, physical=[[1, 1], [2, 1]])
                p.id = p.wait('created')['character']['id']
            report = {'result': 'PASS', 'directory': str(run), **check(peers)}
            (run/'result.json').write_text(json.dumps(report, indent=2))
            print(json.dumps(report))
        finally:
            for i, p in enumerate(peers):
                p.close()
                (run/f'peer-{i}.json').write_text(json.dumps(p.events), encoding='utf-8')
            if server.poll() is None:
                (run/'world/stop.request').write_text('stop\n')
                try:
                    server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()


if __name__ == '__main__':
    main()
