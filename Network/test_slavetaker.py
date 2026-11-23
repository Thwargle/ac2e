"""Play both Prosper Marches quests through the network bridge and a disposable server.

Uses the prepared production placements. Only travel, starting XP and encounter
difficulty are accelerated. All quest state, loot and rewards come from gameplay.
"""
import json
from quest_playtest import phase, quality, quest_world, use
from test_gameplay import until
from test_professions import owned

DANTRY, COUNSELOR, BURUN = 0x470015CA, 0x4700088A, 0x4700127E
HEAD, EARRING = 0x4700109D, 0x470011B8
HUNT, SEEK = 0x4000002B, 0x400000D6
PORT = 19648


def main():
    with quest_world('slavetaker', (DANTRY, COUNSELOR, BURUN), BURUN, 1_000_000, PORT) as game:
        peer = game.connect()
        dantry = game.visit(peer, DANTRY)
        accepted = phase(peer, use(peer, dantry), SEEK, 1)
        assert 172790 <= accepted['remaining'] <= 172800, accepted
        counselor = game.visit(peer, COUNSELOR)
        phase(peer, use(peer, counselor), HUNT, 1)
        burun = game.visit(peer, BURUN)
        mark = len(peer.events)
        peer.send('attack', id=burun)
        phase(peer, mark, HUNT, 2)
        until(peer, mark, 'quality', lambda e: e.get('id') == burun and e['key'] == 0x102 and int(e['value']) == 0)
        peer.send('combat', enabled=False)
        until(peer, mark, 'combat', lambda e: not e['enabled'])
        mark = len(peer.events)
        peer.send('open_container', id=burun)
        corpse = until(peer, mark, 'container_state', lambda e: e['id'] == burun and e['open'])
        head = until(peer, mark, 'object', lambda e: e.get('entity') == HEAD and e['id'] in corpse['items'])
        peer.send('take_item', container=burun, id=head['id'], **{'from':corpse['items'].index(head['id'])})
        phase(peer, mark, HUNT, 3)
        until(peer, mark, 'object', lambda e: e.get('entity') == HEAD and e.get('container') == peer.id)
        game.leave(peer)

        peer = game.connect()
        phase(peer, 0, HUNT, 3)
        phase(peer, 0, SEEK, 1)
        head, = owned(peer, HEAD)
        before_xp = int(until(peer, 0, 'quality', lambda e: e.get('id') is None and e['key'] == 0x12c)['value'])
        counselor = game.visit(peer, COUNSELOR)
        mark = use(peer, head['id'], counselor)
        phase(peer, mark, HUNT, 0)
        phase(peer, mark, SEEK, 2)
        quality(peer, mark, 0x12c, before_xp + 4000)
        quality(peer, mark, 0x7ac20003, 1000)
        until(peer, mark, 'object', lambda e: e.get('entity') == EARRING and e.get('container') == peer.id)
        assert not owned(peer, HEAD)
        dantry = game.visit(peer, DANTRY)
        mark = use(peer, dantry)
        phase(peer, mark, SEEK, 0)
        quality(peer, mark, 0x12c, before_xp + 4500)
        quality(peer, mark, 0x7ac20003, 1300)
        for actor in (DANTRY, COUNSELOR):
            mark = use(peer, game.visit(peer, actor))
            until(peer, mark, 'chat')
        game.leave(peer)

        peer = game.connect()
        for quest in (HUNT, SEEK):
            phase(peer, 0, quest, 0)
        quality(peer, 0, 0x12c, before_xp + 4500)
        quality(peer, 0, 0x7ac20003, 1300)
        assert len(owned(peer, EARRING)) == 1 and not owned(peer, HEAD)
        game.leave(peer)
        result = {'result': 'PASS', 'quest_ids': [f'{q:08x}' for q in (HUNT, SEEK)],
              'checks': ['Dantry referral and 48-hour deadline', 'Counselor acceptance',
                         'Burun kill and quest-bound corpse pickup', 'restart before delivery',
                         'head consumed for one Toadfang Earring, 4000 XP and 1000 points',
                         'report to Dantry for 500 XP and 300 points',
                         'repeat attempts and second restart preserve completed rewards']}
        (game.run / 'result.json').write_text(json.dumps(result, indent=2))
        print(game.run)


if __name__ == '__main__':
    main()

