"""Play Shreth-Hide Leather through a disposable UDP world.

Travel and combat are accelerated. Only the character XP and crafting material
are seeded. The hunter, its corpse, both hand-ins and the rewards use gameplay.
"""
import json
import subprocess

from quest_playtest import phase, quality, quest_world, use
from test_gameplay import until
from test_network import DAT_DIRECTORY, SERVER
from test_professions import owned

QUEST = 0x4000fe03
LUPE, GALLUS, HUNTER = 0x470021b7, 0x470021b8, 0x470021c1
RAWHIDE, LEATHER, TALLOW = 0x470021b1, 0x470021b0, 0x470027a7


def supply(store, quantity):
    subprocess.run([str(SERVER), 'inventory-grant', '--character-id', '1', '--account', 'local',
                    '--data', str(store), '--game-data', str(DAT_DIRECTORY), '--id', '470027a7',
                    '--visual-did', '1f000234', '--quantity', str(quantity), '--stack-limit', '500'],
                   check=True, capture_output=True)


def main():
    with quest_world('shreth-leather', (LUPE, GALLUS, HUNTER), HUNTER, 43_141_054, 19656) as game:
        supply(game.store, 60)
        p = game.connect()
        lupe = game.visit(p, LUPE)
        accepted = phase(p, use(p, lupe), QUEST, 1)
        assert 7190 <= accepted['remaining'] <= 7200, accepted
        hunter = game.visit(p, HUNTER)
        mark = len(p.events)
        p.send('attack', id=hunter)
        until(p, mark, 'quality', lambda e: e.get('id') == hunter and e['key'] == 0x102 and int(e['value']) == 0)
        p.send('combat', enabled=False)
        until(p, mark, 'combat', lambda e: not e['enabled'])
        mark = len(p.events)
        p.send('open_container', id=hunter)
        corpse = until(p, mark, 'container_state', lambda e: e['open'] and e['id'] == hunter)
        hide = until(p, mark, 'object', lambda e: e.get('entity') == RAWHIDE and e['id'] in corpse['items'])
        p.send('take_item', container=hunter, id=hide['id'], **{'from': corpse['items'].index(hide['id'])})
        bag_hide = until(p, mark, 'object', lambda e: e.get('entity') == RAWHIDE and e.get('container') == p.id)
        lupe = game.visit(p, LUPE)
        mark = use(p, bag_hide['id'], lupe)
        until(p, mark, 'inventory_remove', lambda e: e['id'] == bag_hide['id'])
        stack, = owned(p, TALLOW)
        mark = use(p, stack['id'], lupe)
        until(p, mark, 'chat', lambda e: '100' in str(e) and 'tallow' in str(e).lower())
        assert not owned(p, LEATHER)
        game.leave(p)
        supply(game.store, 43)
        p = game.connect()
        stacks = owned(p, TALLOW)
        assert sorted(s['quantity'] for s in stacks) == [43, 60], stacks
        lupe = game.visit(p, LUPE)
        mark = use(p, stacks[0]['id'], lupe)
        leather = until(p, mark, 'object', lambda e: e.get('entity') == LEATHER and e.get('container') == p.id)
        until(p, mark, 'object', lambda e: e.get('entity') == TALLOW and e.get('container') == p.id and e['quantity'] == 3)
        gallus = game.visit(p, GALLUS)
        def total(key):
            return int(until(p, 0, 'quality', lambda e: e.get('id') is None and e['key'] == key)['value'])
        xp, gold = total(0x12c), total(0xc1c)
        mark = use(p, leather['id'], gallus)
        phase(p, mark, QUEST, 0)
        quality(p, mark, 0x12c, xp + 120_000)
        quality(p, mark, 0xc1c, gold + 1250)
        quality(p, mark, 0x7ac20003, 600)
        game.leave(p)
        p = game.connect()
        phase(p, 0, QUEST, 0)
        assert not owned(p, LEATHER) and not owned(p, RAWHIDE)
        remainder, = owned(p, TALLOW)
        assert remainder['quantity'] == 3
        mark = use(p, game.visit(p, LUPE))
        until(p, mark, 'chat')
        game.leave(p)
        p = game.connect()
        phase(p, 0, QUEST, 0)
        quality(p, 0, 0x12c, xp + 120_000)
        quality(p, 0, 0xc1c, gold + 1250)
        quality(p, 0, 0x7ac20003, 600)
        game.leave(p)
        report = {'result':'PASS', 'quest_ids':['4000fe03'],
                  'checks':['Lupe acceptance and two-hour timer', 'Hunter kill, corpse and rawhide pickup',
                            'Rawhide hand-in, insufficient tallow preserved across restart',
                            '100 tallow consumed across two stacks with three left over',
                            'Gallus accepts leather for 120000 XP, 1250 gold and 600 points',
                            'Repeat attempt and restart preserve completion and reward totals']}
        (game.run / 'result.json').write_text(json.dumps(report, indent=2))
        print(game.run)


if __name__ == '__main__':
    main()
