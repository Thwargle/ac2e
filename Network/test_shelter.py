"""Shelter regression entry point for production routes and armor replication.

The retired one-room prototype used an obstructed Digger anchor and expected
three deterministic armor drops; neither represents the current world/loot.
The production suite exercises all three racial quest routes and saved rewards.
Armor replication is checked separately for all six supported race/sex bodies.
The armor suite also checks atomic weapon replacement and dense bag reordering.
"""
from test_starters import main as starters
from test_armor import main as armor


def main():
    starters()
    armor()


if __name__ == '__main__':
    main()
