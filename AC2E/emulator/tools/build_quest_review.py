"""Join explicitly selected loopback gameplay reports to atlas quest identities.

Use --run once per result directory under Saved/AC2NetworkTests. With no
arguments, reproduce the checked-in review from its recorded runs. A directory
with only a partial progress file cannot serve as a completed test.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
ATLAS = ROOT / 'Docs/Atlas'
PLAYED = 'Played through UDP'
PARTIAL = 'Partially exercised through UDP'


def load_js(path):
    raw = path.read_text(encoding='utf-8')
    return json.loads(raw.split('=', 1)[1].strip().removesuffix(';'))


def checked(value, message):
    if not value:
        raise ValueError(message)


# Bind each result to the journal(s) the test actually asserted. Some tests
# advance a referral to its first stage without completing it.
SUITES = {
    'arwic-mines': ('test_arwic_mines', ['400000c1'], ['400000c2']),
    'windmill': ('test_windmill', ['400000c7', '400000cb'], []),
    'warder-notebook': ('test_warder_notebook', ['400000c4', '400000c5'], []),
    'esper-camp': ('test_esper_camp', ['400000d0', '400000cf', '400000d1', '400000cc'], []),
    'slavetaker': ('test_slavetaker', ['400000d6', '4000002b'], []),
    'shreth-leather': ('test_shreth_leather', ['4000fe03'], []),
    'drudgerton': ('test_drudgerton', ['4000fe04'], []),
    'nature-exchanges': ('test_nature_exchanges', [], ['4000028f']),
    'nature-gathering': ('test_nature_gathering', [], ['4000028f']),
}


def read_run(directory, evidence, problems):
    report = json.loads((directory / 'result.json').read_text(encoding='utf-8'))
    suite, run = directory.name.rsplit('-', 1)
    checked(len(run) == 32 and all(c in '0123456789abcdef' for c in run), directory.name)

    def attach(quest, script, checks, partial=False):
        path = f'Network/{script}.py'
        checked((ROOT / path).is_file(), path)
        evidence[quest].append((directory.name, path, list(checks), partial))

    if suite in SUITES:
        script, passed, partial = SUITES[suite]
        checked(report.get('result') == 'PASS' and report.get('checks'), directory)
        for quest in passed:
            attach(quest, script, report['checks'])
        for quest in partial:
            if suite == 'arwic-mines' and report.get('completed_miller_referral'):
                attach(quest, script, ['Referral accepted from the Overseer after the mine lesson and resumed after restart.',
                                       'Talked to the Miller of Kehan. Completion awarded 30 XP once and persisted after restart.'])
                continue
            checks = (['Referral accepted after the mine quest; its recipient turn-in was outside this run.']
                      if suite == 'arwic-mines' else report['checks'] + [report['limitation']])
            attach(quest, script, checks, partial=True)
    elif suite == 'exploration':
        checked(report['all_four_completed_journals_survive_restart'] and report['grounded_obelisks'] == 9, directory)
        for quest in ('40000016', '40000017', '40000018', '400000c0'):
            checks = (['Explorer acceptance, three ordered obelisk interactions and blessings, return and ward.']
                      if quest != '400000c0' else ['Mines Overseer referral accepted and completed at the recipient.'])
            attach(quest, 'test_exploration_quests', checks + ['Completed journals and combined balances checked after restart.'])
    elif suite == 'starters':
        races = sorted(r['race'] for r in report if r.get('complete') and r.get('overworld') and r.get('relogin'))
        checked(races == ['Human', 'Lugian', 'Tumerok'], directory)
        attach('4000ff05', 'test_starters',
               ['Guard, Lieutenant and Sentry, Digger tool, Scout exchange, Sentinel glyph, vault portal and shard.',
                'Confirmed overworld arrival and relog for ' + ', '.join(races) + '.'])
    elif suite == 'inferred':
        checked(report['completion_and_consumed_items_survive_server_restart'], directory)
        checked(report['delivery_bag_exchange_and_duplicate_rejection'], directory)
        attach('4000fe01', 'test_inferred_content', ['Delivery bag exchange, duplicate rejection and restart.'])
        checked(report['owned_elixir_starts_hunt'] and report['hunt_kills'] == 5, directory)
        attach('40000097', 'test_inferred_content',
               ['Starter kill, five target kills, group capacity, corpse re-open and loot retention after restart.'])
        attach('4000000a', 'test_inferred_content',
               ['A supplied elixir started the hunt. Its active journal persisted after restart.'], partial=True)
    elif suite == 'hunting':
        cases = [r for r in report.values() if isinstance(r, dict) and 'journal' in r]
        checked(report['all_completed_journals_survive_restart'] == len(cases) > 0 and
                report['duplicate_turn_in_rejected'] and report['kill_updates_exclude_unchanged_hunts'], directory)
        for case in cases:
            checked(case['accepted'] and case['returned'] and case['kills'] > 0, case)
            checks = [f"NPC acceptance, {case['kills']} credited kills and a return to the giver.",
                      f"Verified {case['xp']:,} completion XP and {case['points']:,} quest points at the test level.",
                      'Completed journal and cumulative reward balances restored after restart.']
            if case.get('key'):
                checks.append(f"Verified key {case['key']:08x} in the character's inventory.")
            attach(case['journal'], 'test_hunting_quests', checks)
        if referral := report.get('aerie_delivery'):
            checked(referral['all_four_prerequisites'] and referral['consumed_report'] and referral['restarted'], referral)
            attach('4000fe02', 'test_hunting_quests',
                   ['Completed all four prerequisite hunts on the same character before obtaining the Aerie Report.',
                    f"Verified the timer, delivery to Maekarn, {referral['xp']:,} XP, item consumption and restart."])
    elif suite == 'incursions':
        for quest, case in report.items():
            if case['status'] not in ('passed', 'fixture-passed'):
                missing = case['status'] == 'unplaced'
                if missing:
                    message = 'Missing from prepared world content: ' + ', '.join(case['missing']) + '.'
                else:
                    checked(case['status'] == 'failed' and case['error'], case)
                    message = 'The attempted server gameplay route did not reach verified completion.'
                problems[quest].append((directory.name, 'Network/test_incursion_quests.py', message, missing))
                continue
            checked(case['restarted'] and case['kills'] > 0, case)
            checks = [f"Activation, {case['kills']} credited kills, {case['xp']:,} completion XP and "
                      f"{case['quest_points']:,} points; persisted after restart."]
            if case['pregranted_elixir']:
                checks.append('Test supplied the activating item; acquisition remains outside this run.')
            if source := case.get('natural_start'):
                if source['method'] == 'loot':
                    checks.append(f"Killed source {source['entity']} in landblock {source['cell'][:4]}, opened the corpse, "
                                  f"picked up the rolled elixir {source['item']:08x} and drank it to start the quest.")
                else:
                    checked(source['method'] == 'ancestor', source)
                    checks.append(f"Accepted the quest from the placed Armoredillo Doom Ancestor in {source['cell'][:4]}.")
            if case['status'] == 'fixture-passed':
                checked(case['missing'], case)
                checks.append('The test supplied unplaced actor roles: ' + ', '.join(case['missing']) + '.')
            if case.get('substitute_encounters'):
                checks.append(f"{case['substitute_encounters']} test encounters used alternate grounded homes.")
            attach(quest, 'test_incursion_quests', checks, partial=case['pregranted_elixir'] or case['status'] == 'fixture-passed')
    else:
        raise ValueError(f'Unrecognized quest gameplay report {directory.name}')


def row_review(row, evidence, problems):
    facts = row['facts']
    journals = set(facts.get('AC2E journal ID') or row.get('ids', []))
    route = facts.get('Quest route status', [])
    blockers = list(dict.fromkeys(facts.get('Quest route gap', []) + facts.get('Partial quest support', [])))
    checks, tests = [], []
    played, exercised = set(), set()
    for journal in sorted(journals):
        for run, script, assertions, partial in evidence.get(journal, []):
            exercised.add(journal)
            if not partial:
                played.add(journal)
            label = f'{script} ({run})'
            if label not in tests:
                tests.append(label)
            for message in assertions:
                if len(journals) > 1:
                    message = f'{journal}: {message}'
                if message not in checks:
                    checks.append(message)
    missing_actor = False
    for journal in sorted(journals - played):
        for run, script, reason, missing in problems.get(journal, []):
            if journal in exercised and not missing:
                continue
            missing_actor |= missing
            message = f'{journal}: {reason}' if len(journals) > 1 else reason
            if message not in blockers:
                blockers.append(message)
            label = f'{script} ({run})'
            if label not in tests:
                tests.append(label)
    if played and played == journals and not blockers:
        status = PLAYED
    elif exercised:
        status = PARTIAL
        if uncovered := journals - played:
            blockers.append('No complete route test for ' + ', '.join(sorted(uncovered)) + '.')
    elif missing_actor or facts.get('Quest route gap') or facts.get('Partial quest support'):
        status = 'Missing route steps'
    elif 'No linked quest policy' in route:
        status = 'No linked quest policy'
        blockers.append('No server quest policy linked to this guide entry.')
    else:
        status = 'Gameplay not verified'
        if facts.get('Quest policy'):
            blockers.append('No successful gameplay report for the linked policy.')
        elif journals:
            blockers.append('Recovered journal identity has no verified start-to-finish route.')
        else:
            blockers.append('No verified route or journal identity for this entry.')
    return {'status':status, 'indexed':'Guide section' in facts, 'checks':checks,
            'blockers':blockers, 'tests':tests}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--atlas', type=Path, default=ATLAS / 'atlas-data.js')
    parser.add_argument('--out', type=Path, default=ATLAS / 'quest-review.js')
    parser.add_argument('--run-root', type=Path, default=ROOT / 'Saved/AC2NetworkTests')
    parser.add_argument('--run', action='append', default=[])
    args = parser.parse_args()
    runs = args.run or load_js(args.out)['runs']
    evidence, problems = defaultdict(list), defaultdict(list)
    for name in sorted(set(runs)):
        checked(Path(name).name == name, name)
        read_run(args.run_root / name, evidence, problems)
    quests = [r for r in load_js(args.atlas)['records'] if r['kind'] == 'Quest']
    records = {'Quest/' + r['name']:row_review(r, evidence, problems) for r in quests}
    checked(len(records) == len(quests), 'Duplicate atlas quest identity')
    reviewed_ids = {did for r in quests for did in r['facts'].get('AC2E journal ID') or r.get('ids', [])}
    checked(not evidence.keys() - reviewed_ids, f'Tested journals absent from atlas: {evidence.keys() - reviewed_ids}')
    totals = dict(Counter(r['status'] for r in records.values()))
    indexed = dict(Counter(r['status'] for r in records.values() if r['indexed']))
    data = {'schema':1, 'runs':sorted(set(runs)), 'summary':totals, 'indexedSummary':indexed,
            'scope':'AC2E loopback protocol. Test-map travel, prepared character levels and selected '
                    'encounter fixtures expedite the routes. Walking routes, client visuals, Icefire and '
                    'retail rates are outside these results.', 'records':records}
    args.out.write_text('window.AC2QuestReview = '+json.dumps(data, ensure_ascii=False, separators=(',', ':'))+';\n',
                        encoding='utf-8')
    print('Quest atlas records:', len(records), totals)
    print('Unique guide-index records:', sum(indexed.values()), indexed)


if __name__ == '__main__':
    main()
