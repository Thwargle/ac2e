"""Content comparisons must explain both recovery and regressions without mixing DAT sets."""
import json
import hashlib
import struct
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import prepare_content
from content_profile import ContentProfile, nearby_landmark, door_physical


class ContentComparisonTest(unittest.TestCase):
    def test_naokim_replacement_requires_both_phases_to_serve_identical_dialogue(self):
        content = prepare_content.ROOT / 'AC2E/emulator/content'
        rules = json.loads((content / 'placement-replacements.json').read_bytes())['replacements']
        rule, = [rule for rule in rules if rule.get('retire_post_atl_variant')]
        old_id, kept_id = [rule[key]['id'] for key in ('superseded', 'retained')]
        source = 'recovered-population.tsv'
        lines = [line for line in (content / source).read_text(encoding='utf-8').splitlines()
                 if line.split('\t', 1)[0] in (old_id, kept_id)]
        inputs = {(source, line.split('\t', 1)[0]): line for line in lines}
        admitted = {iid: source for _, iid in inputs}
        profiles = json.loads((content / 'npc-dialogue-v1.json').read_bytes())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'placement-replacements.json').write_text(json.dumps({'schema': 1, 'replacements': [rule]}))
            catalog = root / 'npc-dialogue-v1.json'
            catalog.write_text(json.dumps(profiles))
            result, removed, pending = prepare_content.apply_placement_replacements(root, lines, inputs, admitted)
            self.assertEqual((result, removed, pending), ([inputs[source, kept_id]], [rule], []))
            result, removed, pending = prepare_content.apply_placement_replacements(
                root, lines, inputs, {old_id: source})
            self.assertEqual((result, removed, len(pending)), (lines, [], 1))
            old_entity = int(inputs[source, old_id].split('\t')[1], 16)
            profile = next(p for p in profiles['profiles'] if p['entity'] == old_entity)
            profile['topics'][0]['text'] += ' A later phase.'
            catalog.write_text(json.dumps(profiles))
            with self.assertRaisesRegex(ValueError, 'phase and dialogue'):
                prepare_content.apply_placement_replacements(root, lines, inputs, admitted)

    def test_officer_cameron_names_only_matching_anonymous_capture_placements(self):
        directory = prepare_content.ROOT / "Asheron's Call 2 - Legions"
        if not (directory / 'portal.dat').is_file():
            self.skipTest('Local Legions portal DAT is unavailable')
        content = prepare_content.ROOT / 'AC2E/emulator/content'
        captured = (content / 'world-observed-expanded.tsv').read_text().splitlines()
        originals = [line + '\t-' for line in captured if '\t47002e27\t' in line]
        self.assertEqual({row.split('\t')[3] for row in originals}, {'6fa8000d', 'cf8c0015'})
        other = [originals[0][:-1] + 'Existing name',
                 originals[0].replace('1f001554', '1f001555'),
                 originals[0].replace('47002e27', '470038b2')]
        rows = [*originals, *other]
        profile = ContentProfile(directory)
        try:
            with patch.object(prepare_content, 'entity', return_value={
                'properties': {'40000019:41000189': 'NPCTable_Guard'}}):
                self.assertEqual(prepare_content.apply_recovered_actor_names(profile, content, rows), 0)
                self.assertEqual(rows, [*originals, *other])
            self.assertEqual(prepare_content.apply_recovered_actor_names(profile, content, rows), 2)
            self.assertEqual(rows, [*(row[:-1] + 'Officer Cameron' for row in originals), *other])
            self.assertEqual(prepare_content.apply_recovered_actor_names(profile, content, rows), 0)
        finally:
            profile.close()

    def test_portal_localization_only_names_matching_unnamed_placements(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'local_English.dat').touch()
            portal = dict(id='8140000000010001', entity='47001454', cell='6fa7001b',
                          position=[65.0608, 55.4285, 79.9443], status='placed', name=[12, 34])
            unresolved = dict(portal, id='8140000000010002', status='unresolved')
            unknown_name = dict(portal, id='8140000000010003', name=[12, 56])
            (root / 'portals.json').write_text(json.dumps({'portals': [portal, unresolved, unknown_name]}))
            blank = '8140000000010001\t47001454\t1f000d09\t6fa7001b\t65.0608\t55.4285\t79.9443\t1\t0\t0\t0\t-\t-'
            unchanged = [
                blank[:-1] + 'Existing name',
                blank.replace(portal['id'], '8140000000010004'),
                blank.replace(portal['entity'], '47001455'),
                blank.replace(portal['cell'], '6fa7001c'),
                blank.replace('79.9443', '79.95'),
                blank.replace('79.9443', 'nan'),
                blank.rsplit('\t', 1)[0],
                blank.replace(portal['id'], unresolved['id']),
                blank.replace(portal['id'], unknown_name['id']),
            ]
            labeled = blank[:-1] + 'South Gateway to Rithwic'
            rows = ['pyrealium-world-v4', blank, *unchanged]
            with patch.object(prepare_content, 'Archive') as archive, \
                 patch.object(prepare_content, 'localized_name', side_effect=['South Gateway to Rithwic', '-']) as lookup:
                self.assertEqual(prepare_content.apply_portal_names(root, root, rows), 1)
                self.assertEqual(rows, ['pyrealium-world-v4', labeled, *unchanged])
                self.assertEqual([c.args[1] for c in lookup.call_args_list],
                                 [{'table': 12, 'key': 34}, {'table': 12, 'key': 56}])
                archive.assert_called_once_with(root / 'local_English.dat')
                archive.return_value.file.close.assert_called_once()

    def test_portal_localization_is_optional_without_either_source(self):
        for present in ['portals.json', 'local_English.dat']:
            with self.subTest(present=present), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / present).touch()
                rows = ['pyrealium-world-v4']
                with patch.object(prepare_content, 'Archive') as archive:
                    self.assertEqual(prepare_content.apply_portal_names(root, root, rows), 0)
                    archive.assert_not_called()
                self.assertEqual(rows, ['pyrealium-world-v4'])

    def test_installed_portal_localizations_match_observed_placements(self):
        directory = prepare_content.ROOT / "Asheron's Call 2 - Legions"
        if not (directory / 'local_English.dat').is_file():
            self.skipTest('Local Legions language DAT is unavailable')
        content = prepare_content.ROOT / 'AC2E/emulator/content'
        captured = (content / 'world-observed-expanded.tsv').read_text(encoding='utf8').splitlines()[1:]
        before = [line + '\t-' if len(line.split('\t')) == 12 else line for line in captured]
        rows = before.copy()
        count = prepare_content.apply_portal_names(directory, content, rows)
        catalog = json.loads((content / 'portals.json').read_bytes())
        expected = {p['id'] for p in catalog['portals'] if p['status'] == 'placed'}
        changes = {}
        for old, new in zip(before, rows, strict=True):
            if old != new:
                fields = new.split('\t')
                self.assertEqual(old, '\t'.join([*fields[:-1], '-']))
                changes[fields[0]] = fields[-1]
        self.assertEqual(changes.keys(), expected)
        self.assertEqual(count, len(expected))
        self.assertEqual(changes['8140000000010001'], 'South Gateway to Rithwic')
        self.assertEqual(changes['8140000000010002'], 'South Gateway to Molwirth')
        self.assertEqual(prepare_content.apply_portal_names(directory, content, rows), 0)

    def test_verified_platform_requires_unchanged_capture_and_unforced_native_support(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            before='8140000000000001\t47002760\t1f001384\t4da20027\t81\t124\t139.44342\t1\t0\t0\t0\t-\tConstruct'
            iid=before.split('\t')[0]
            (root/'placement-clearance-v1.json').write_text(json.dumps(
                dict(schema=1,native_ground_verified=True,repairs=[],unresolved=[{'id':iid}])))
            proof=dict(accepted=True,placement_result='Ok',placement_flags=0,physics_quanta=100,
                all_samples_supported=True,max_displacement_m=0.,requested_cell='4da20027',
                requested_position=[81.,124.,139.44342])
            row=dict(id=iid,original=before,replacement=None,native_support=proof)
            policy=dict(schema=1,native_ground_verified=True,reviewed=1,repairs=[],verified=[row],unresolved=[])
            path=root/'placement-reviewed-v1.json'
            path.write_text(json.dumps(policy))
            rows,applied=prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',before])
            self.assertEqual(rows[-1],before);self.assertEqual(applied,[])
            for field,value in [('accepted',False),('placement_result','NoCell'),('placement_flags',16),
                ('placement_flags',64),('physics_quanta',1),('all_samples_supported',False),
                ('max_displacement_m',1.),('max_displacement_m',float('nan')),('requested_cell','4da20028'),
                ('requested_position',[81.,124.,140.]),('requested_position',[])]:
                row['native_support']={**proof,field:value};path.write_text(json.dumps(policy))
                with self.assertRaisesRegex(ValueError,'native sphere support'):
                    prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',before])
            row['native_support']=proof;path.write_text(json.dumps(policy))
            for changed in [before.replace('139.44342','139.5'),before.replace('1f001384','1f001385'),
                            before.replace('\t-\tConstruct','\t0.8\tConstruct')]:
                with self.assertRaisesRegex(ValueError,'Stale retained placement'):
                    prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',changed])
            row['replacement']=before;path.write_text(json.dumps(policy))
            with self.assertRaisesRegex(ValueError,'Stale retained placement'):
                prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',before])

    def test_reviewed_clearance_requires_flagged_identity_bounded_same_block_and_native_route(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            before='8140000000000001\t47000001\t1f000001\t77b80001\t10\t10\t100\t1\t0\t0\t0\t1\tCreature'
            iid=before.split('\t')[0]
            base={'schema':1,'native_ground_verified':True,'repairs':[],'unresolved':[{'id':iid}]}
            (root/'placement-clearance-v1.json').write_text(json.dumps(base))
            after=before.replace('\t10\t10\t100\t','\t22\t10\t100\t').replace('77b80001','77b80009')
            rule={'id':iid,'original':before,'replacement':after,'native_floor_route':True}
            policy={'schema':1,'native_ground_verified':True,'reviewed':1,'repairs':[rule],'unresolved':[]}
            path=root/'placement-reviewed-v1.json'
            path.write_text(json.dumps(policy))
            rows,applied=prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',before])
            self.assertEqual(rows[-1],after);self.assertEqual(len(applied),1)
            for replacement in [after.replace('\t22\t10','\t31\t10'),after.replace('77b80009','78b80009'),
                                after.replace('47000001','47000002')]:
                rule['replacement']=replacement;path.write_text(json.dumps(policy))
                with self.assertRaises(ValueError):prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',before])
            rule['replacement']=after;rule['native_floor_route']=False;path.write_text(json.dumps(policy))
            with self.assertRaisesRegex(ValueError,'verified route'):
                prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',before])
            rule['native_floor_route']=True;policy['unresolved']=[{'id':iid}];path.write_text(json.dumps(policy))
            with self.assertRaisesRegex(ValueError,'exactly the initially flagged'):
                prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',before])
            policy['repairs']=[];policy['unresolved']=[{'id':iid,'original':before,'replacement':None}]
            path.write_text(json.dumps(policy))
            rows,applied=prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',before])
            self.assertEqual(rows[-1],before);self.assertEqual(applied,[])
            with self.assertRaisesRegex(ValueError,'Stale retained placement'):
                prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',after])

    def test_clearance_repairs_pin_rows_and_cannot_change_identity_or_dungeon_floor(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            before='8140000000000001\t47000001\t1f000001\t77b80001\t10\t10\t100\t1\t0\t0\t0\t1\tCreature'
            after=before.replace('\t10\t10\t100\t','\t11\t10\t100\t')
            rule={'id':before.split('\t')[0],'original':before,'replacement':after}
            policy={'schema':1,'native_ground_verified':True,'repairs':[rule]}
            path=root/'placement-clearance-v1.json'
            path.write_text(json.dumps(policy))
            rows,applied=prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',before])
            self.assertEqual(rows,['pyrealium-world-v4',after]);self.assertEqual(len(applied),1)
            with self.assertRaisesRegex(ValueError,'Stale placement'):
                prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',before.replace('\t100\t','\t101\t')])
            for replacement in [after.replace('47000001','47000002'),after.replace('\t1\tCreature','\t2\tCreature'),
                                after.replace('77b80001','77b80100'),after.replace('\t11\t10','\t50\t10'),
                                after.replace('\t100\t','\tnan\t')]:
                policy['repairs'][0]['replacement']=replacement;path.write_text(json.dumps(policy))
                with self.assertRaises(ValueError):prepare_content.apply_clearance_repairs(root,['pyrealium-world-v4',before])

    def test_evidence_hash_accepts_checkout_line_endings_without_accepting_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'content.tsv'
            lf = b'header\nentry\tvalue\n'
            crlf = lf.replace(b'\n', b'\r\n')
            expected = {hashlib.sha256(lf).hexdigest(), hashlib.sha256(crlf).hexdigest()}
            for data in (lf, crlf):
                path.write_bytes(data)
                self.assertEqual(prepare_content.sha256_any_eol(path), expected)
            path.write_bytes(b'header\nentry\tchanged\n')
            self.assertTrue(prepare_content.sha256_any_eol(path).isdisjoint(expected))

    def test_regional_population_requires_matching_ground_proof_and_native_visual(self):
        self.assert_authored_pack_evidence('regional')

    def test_exploration_population_requires_matching_ground_proof_and_native_visual(self):
        self.assert_authored_pack_evidence('exploration')

    def test_hunting_expansion_requires_matching_ground_proof_and_native_visual(self):
        self.assert_authored_pack_evidence('hunting', 2)

    def test_npc_population_requires_matching_ground_proof_and_native_visual(self):
        self.assert_authored_pack_evidence('npc', catalog_name='npc-dialogue-v1.json')

    def assert_authored_pack_evidence(self, pack, version=1, catalog_name=None):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);content=root/'AC2E/emulator/content';content.mkdir(parents=True)
            for name in ['world-observed-expanded.tsv','training-world.tsv','capture-spawns.tsv']:
                (content/name).write_text('pyrealium-world-v4\n')
            row='8140c0000077b800\t47000001\t1f000001\t01020001\t5\t5\t0\t1\t0\t0\t0\t-\tCreature'
            manifest=content/f'{pack}-world-v{version}.tsv';manifest.write_text('pyrealium-world-v4\n'+row+'\n')
            catalog=content/(catalog_name or f'{pack}-content-v{version}.json');catalog.write_text('{}')
            evidence={'native_ground_verified':True,
                      'manifest_sha256':hashlib.sha256(manifest.read_bytes()).hexdigest(),
                      'source_sha256':hashlib.sha256(catalog.read_bytes()).hexdigest()}
            manifest.with_suffix('.report.json').write_text(json.dumps(evidence))
            index={'portal.dat':{0x47000001,0x1f000001},'cell_1.dat':{0x0102ffff}}
            output=root/'world.tsv'
            with patch.object(prepare_content,'ROOT',root),patch.object(prepare_content,'asset_index',return_value=index),patch.object(prepare_content,'ContentProfile') as profile:
                profile.return_value.visual.return_value=0x1f000001
                prepare_content.prepare(root,output)
                self.assertEqual(output.read_text().splitlines()[0],'pyrealium-world-v4')
                self.assertEqual(output.read_text().splitlines()[1:],[row])
                profile.return_value.visual.return_value=0x1f000002
                prepare_content.prepare(root,output)
                self.assertEqual(len(output.read_text().splitlines()),1)
                catalog.write_text('{"changed":true}')
                with self.assertRaisesRegex(ValueError,'fresh native ground'):
                    prepare_content.prepare(root,output)

    def test_starter_manifest_catalog_and_routes_must_match_the_reviewed_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            content,index,*_=self.replacement_fixture(root)
            files={'manifest_sha256':content/'starter-world-v1.tsv',
                   'source_sha256':content/'starter-areas-v1.json',
                   'routes_sha256':content/'starter-routes-v1.tsv'}
            for key,path in files.items():
                path.write_text('{}' if key=='source_sha256' else 'pyrealium-world-v3\n')
            evidence={key:hashlib.sha256(path.read_bytes()).hexdigest() for key,path in files.items()}
            (content/'starter-world-v1.report.json').write_text(json.dumps(evidence))
            with patch.object(prepare_content,'ROOT',root),patch.object(prepare_content,'asset_index',return_value=index),patch.object(prepare_content,'ContentProfile'):
                prepare_content.prepare(root,root/'world.tsv')
                for path in files.values():
                    original=path.read_bytes();path.write_bytes(original+b'changed')
                    with self.assertRaisesRegex(ValueError,'Starter catalog changed'):
                        prepare_content.prepare(root,root/'world.tsv')
                    path.write_bytes(original)

    def replacement_fixture(self, root):
        content = root / 'AC2E/emulator/content'
        content.mkdir(parents=True)
        old = '8140000000000001\t47001c27\t1f001042\t76b80027\t98.1885986\t133.502838\t168.323868\t1\t0\t0\t0\t-'
        new = '8140000000000002\t47001c27\t1f001042\t77b80006\t1.37962341\t100.779083\t182.50499\t1\t0\t0\t0\t-\tOrven'
        # The same template in another location is not automatically a duplicate.
        other = old.replace('8140000000000001', '8140000000000003').replace('76b80027', '80b80027')
        for name, rows in [('world-observed-expanded.tsv', [old, other]),
                           ('training-world.tsv', []), ('capture-spawns.tsv', []),
                           ('recovered-population.tsv', [new])]:
            (content / name).write_text('pyrealium-world-v4\n' + '\n'.join(rows) + '\n', encoding='utf-8')
        recovered = content / 'recovered-population.tsv'
        recovered.with_suffix('.report.json').write_text(json.dumps(dict(
            native_ground_verified=True, manifest_sha256=hashlib.sha256(recovered.read_bytes()).hexdigest())))
        endpoint = lambda source, row: dict(source=source, id=row.split('\t')[0],
                                            row_sha256=hashlib.sha256(row.encode()).hexdigest())
        policy = dict(schema=1, replacements=[dict(name='Orven', reason='Audited duplicate',
            superseded=endpoint('world-observed-expanded.tsv', old),
            retained=endpoint('recovered-population.tsv', new))])
        (content / 'placement-replacements.json').write_text(json.dumps(policy))
        index = {'portal.dat': {0x47001c27, 0x1f001042},
                 'cell_1.dat': {0x76b8ffff, 0x77b8ffff, 0x80b8ffff}}
        return content, index, old, new, other

    def test_audited_npc_replacement_crosses_cells_without_template_deduplication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content, index, old, new, other = self.replacement_fixture(root)
            original = {path.name: path.read_bytes() for path in content.iterdir()}
            with patch.object(prepare_content, 'ROOT', root), \
                 patch.object(prepare_content, 'asset_index', return_value=index), \
                 patch.object(prepare_content, 'ContentProfile') as profile:
                profile.return_value.visual.return_value = 0x1f001042
                prepare_content.prepare(root, root / 'world.tsv')
            self.assertEqual((root / 'world.tsv').read_text().splitlines(),
                             ['pyrealium-world-v4', other + '\t-', new])
            report = json.loads((root / 'world.report.json').read_text())
            self.assertEqual(report['included'], 2)
            self.assertEqual(report['excluded'], 1)
            self.assertEqual(report['superseded_placements'][0]['superseded']['id'], old.split('\t')[0])
            self.assertFalse(report['unapplied_placement_replacements'])
            self.assertEqual(original, {path.name: path.read_bytes() for path in content.iterdir()})

    def test_unavailable_replacement_preserves_original_npc(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _, index, old, _, other = self.replacement_fixture(root)
            index['cell_1.dat'].remove(0x77b8ffff)
            with patch.object(prepare_content, 'ROOT', root), \
                 patch.object(prepare_content, 'asset_index', return_value=index), \
                 patch.object(prepare_content, 'ContentProfile'):
                prepare_content.prepare(root, root / 'world.tsv')
            self.assertEqual((root / 'world.tsv').read_text().splitlines(),
                             ['pyrealium-world-v4', old + '\t-', other + '\t-'])
            report = json.loads((root / 'world.report.json').read_text())
            self.assertFalse(report['superseded_placements'])
            self.assertEqual(len(report['unapplied_placement_replacements']), 1)

    def test_stale_replacement_proof_does_not_overwrite_prepared_content(self):
        for endpoint in ['superseded', 'retained']:
            with self.subTest(endpoint=endpoint), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                content, index, *_ = self.replacement_fixture(root)
                path = content / 'placement-replacements.json'
                policy = json.loads(path.read_text())
                policy['replacements'][0][endpoint]['row_sha256'] = 'stale'
                path.write_text(json.dumps(policy))
                output = root / 'world.tsv'
                output.write_text('existing content\n')
                with patch.object(prepare_content, 'ROOT', root), \
                     patch.object(prepare_content, 'asset_index', return_value=index), \
                     patch.object(prepare_content, 'ContentProfile') as profile:
                    profile.return_value.visual.return_value = 0x1f001042
                    with self.assertRaisesRegex(ValueError, 'Stale placement replacement evidence'):
                        prepare_content.prepare(root, output)
                self.assertEqual(output.read_text(), 'existing content\n')

    def test_verified_translation_wins_over_old_cell_snapshots(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);content=root/'AC2E/emulator/content';content.mkdir(parents=True)
            row='8140000000000001\t47000001\t1f000001\t01020001\t5\t5\t0\t1\t0\t0\t0\t-\tBeetle'
            old=row.replace('00000001\t470','00000002\t470')
            for name,rows in [('world-observed-expanded.tsv',[]),('training-world.tsv',[]),
                              ('capture-spawns.tsv',[old]),('recovered-population.tsv',[row])]:
                (content/name).write_text('pyrealium-world-v4\n'+'\n'.join(rows)+'\n')
            path=content/'recovered-population.tsv'
            evidence={'native_ground_verified':True,'manifest_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
            path.with_suffix('.report.json').write_text(json.dumps(evidence))
            index={'portal.dat':{0x47000001,0x1f000001},'cell_1.dat':{0x0102ffff}}
            with patch.object(prepare_content,'ROOT',root),patch.object(prepare_content,'asset_index',return_value=index),patch.object(prepare_content,'ContentProfile') as p:
                p.return_value.visual.return_value=0x1f000001
                prepare_content.prepare(root,root/'world.tsv')
                self.assertEqual((root/'world.tsv').read_text().splitlines(),['pyrealium-world-v4',row])
                path.write_text(path.read_text().replace('5\t5\t0','6\t5\t0'))
                with self.assertRaisesRegex(ValueError,'ground verification'):prepare_content.prepare(root,root/'other.tsv')

    def test_audited_pack_respects_upstream_cells_and_exact_state_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);content=root/'AC2E/emulator/content';content.mkdir(parents=True)
            row='8140000000000001\t47000001\t1f000001\t01020001\t5\t5\t0\t1\t0\t0\t0\t-\t-'
            changed=row.replace('00000001\t470','00000002\t470').replace('01020001','01030001')
            overlap=row.replace('00000001\t470','00000003\t470')
            fresh=row.replace('00000001\t470','00000004\t470').replace('01020001','01040001')
            for name,rows in [('world-observed-expanded.tsv',[]),('training-world.tsv',[]),('capture-spawns.tsv',[]),
                              ('capture-spawns-upstream.tsv',[row,changed]),('capture-spawns-audited.tsv',[overlap,fresh])]:
                (content/name).write_text('pyrealium-world-v4\n'+'\n'.join(rows))
            proof={'excluded_existing':[
                {'id':row.split('\t')[0],'source':'capture-spawns-upstream.tsv','row_sha256':hashlib.sha256(row.encode()).hexdigest()},
                {'id':changed.split('\t')[0],'source':'capture-spawns-upstream.tsv','row_sha256':'outdated-evidence'}]}
            (content/'capture-spawns-audited.report.json').write_text(json.dumps(proof))
            index={'portal.dat':{0x47000001,0x1f000001},'cell_1.dat':{0x0102ffff,0x0103ffff,0x0104ffff}}
            output=root/'world.tsv'
            with patch.object(prepare_content,'ROOT',root),patch.object(prepare_content,'asset_index',return_value=index),patch.object(prepare_content,'ContentProfile') as profile:
                profile.return_value.visual.return_value=0x1f000001
                prepare_content.prepare(root,output)
            self.assertEqual(output.read_text().splitlines(),['pyrealium-world-v4',changed,fresh])
            report=json.loads(output.with_suffix('.report.json').read_text())
            self.assertEqual(len(report['transient_capture_placements']),1)
            self.assertEqual(len(report['overlapping_capture_cells']),1)

    def test_door_defaults_respect_nested_strings_and_truncated_tables(self):
        words = lambda *values: struct.pack('<'+'I'*len(values), *values)
        # Display name contains a tagged nested StringInfo, followed by a DID table.
        name = words(0,0,1,0x41000001) + struct.pack('<HH',9,0) + words(0,0,0)
        data = words(0x21,0x8100114a,4) + name + words(1,1,0x47001145)
        self.assertEqual(door_physical(data),0x47001145)
        for end in range(len(data)):
            with self.assertRaises((ValueError,AssertionError,struct.error)):
                door_physical(data[:end])

    def test_existing_did_with_wrong_model_is_withheld_not_repositioned(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = root / 'AC2E/emulator/content'
            content.mkdir(parents=True)
            wrong = '8140aab372ef1689\t4700334c\t1f0010a8\t77b80006\t1.37962341\t100.779083\t182.50499\t1\t0\t0\t0\t-'
            valid = wrong.replace('8140aab372ef1689', '8140000000000002').replace('1f0010a8','1f0010a6')
            for name, rows in [('world-observed-expanded.tsv', []), ('training-world.tsv', []),
                               ('capture-spawns.tsv', [wrong, valid])]:
                (content / name).write_text('pyrealium-world-v3\n' + '\n'.join(rows))
            index = {'portal.dat': {0x4700334c,0x1f0010a8,0x1f0010a6}, 'cell_1.dat': {0x77b8ffff}}
            output = root / 'world.tsv'
            with patch.object(prepare_content,'ROOT',root), patch.object(prepare_content,'asset_index',return_value=index), \
                 patch.object(prepare_content,'ContentProfile') as profile:
                profile.return_value.visual.return_value = 0x1f0010a6
                prepare_content.prepare(root,output)
            self.assertEqual(output.read_text().splitlines(), ['pyrealium-world-v3',valid])
            report = json.loads(output.with_suffix('.report.json').read_text())
            self.assertEqual(report['incompatible_capture_placements'][0]['id'],'8140aab372ef1689')
            self.assertEqual(report['incompatible_capture_placements'][0]['dat_visual'],'1f0010a6')
            self.assertIn(wrong,(content / 'capture-spawns.tsv').read_text())

    def test_landmark_comparison_crosses_outdoor_cells_but_not_dungeon_rooms(self):
        self.assertTrue(nearby_landmark(0x76b80022,(85.5575,29.5398,182.505),
                                       0x76b8001a,(77.260666,27.62573,182.5)))
        self.assertTrue(nearby_landmark(0x76b80040,(159,20,182.5),0x77b80001,(1,20,182.5)))
        self.assertFalse(nearby_landmark(0x02990109,(0,0,-44),0x0299010a,(0,0,-44)))
        self.assertFalse(nearby_landmark(0x76b80022,(0,0,182),0x76b80022,(13,0,182)))

    def test_installed_legions_template_rejects_reported_capture_building(self):
        directory = prepare_content.ROOT / "Asheron's Call 2 - Legions"
        if not (directory / 'portal.dat').exists():
            self.skipTest('Local Legions DAT is unavailable')
        profile = ContentProfile(directory)
        try:
            self.assertEqual(profile.visual(0x4700334c),0x1f0010a6)
            self.assertEqual(profile.visual(0x47002224),0x1f000bf9)  # Child PhysObj overrides its Gurog parent.
            self.assertEqual(profile.visual(0x470007de),0x1f00002d)  # Native gmQualities physical default.
            self.assertEqual(profile.visual(0x4700114a),0x1f00006c)  # Human door class default.
            self.assertEqual(profile.visual(0x4700079c),0x1f00034a)  # Barred door class default.
        finally:
            profile.close()

    def test_new_capture_names_preserved_without_stacking_cell_snapshots(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);content=root/'AC2E/emulator/content';content.mkdir(parents=True)
            old='8140000000000001\t47000001\t1f000001\t01020001\t5\t5\t0\t1\t0\t0\t0\t1'
            overlap=old.replace('00000001\t470','00000002\t470')+'\tNew observation'
            fresh=old.replace('00000001\t470','00000003\t470').replace('01020001','01030001')+'\tNamed gateway'
            for name,rows in [('world-observed-expanded.tsv',[]),('training-world.tsv',[]),
                              ('capture-spawns.tsv',[old]),('capture-spawns-upstream.tsv',[overlap,fresh])]:
                (content/name).write_text('pyrealium-world-v4\n'+'\n'.join(rows))
            index={'portal.dat':{0x47000001,0x1f000001},'cell_1.dat':{0x0102ffff,0x0103ffff}}
            output=root/'world.tsv'
            with patch.object(prepare_content,'ROOT',root),patch.object(prepare_content,'asset_index',return_value=index),patch.object(prepare_content,'ContentProfile') as profile:
                profile.return_value.visual.return_value=0x1f000001
                prepare_content.prepare(root,output)
            self.assertEqual(output.read_text().splitlines(),['pyrealium-world-v4',old+'\t-',fresh])
            self.assertEqual(len(json.loads(output.with_suffix('.report.json').read_text())['overlapping_capture_cells']),1)

    def test_candidate_restores_one_room_but_loses_one_visual(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = root / 'AC2E/emulator/content'
            content.mkdir(parents=True)
            outdoor = '8140000000000001\t47000001\t1f000001\t01020002\t0\t0\t0\t1\t0\t0\t0\t1'
            interior = '8140000000000002\t47000002\t1f000002\t01020123\t0\t0\t0\t1\t0\t0\t0\t1'
            for name, rows in [('world-observed-expanded.tsv', [outdoor, interior]),
                               ('training-world.tsv', []), ('capture-spawns.tsv', [])]:
                (content / name).write_text('pyrealium-world-v3\n' + '\n'.join(rows))
            current = {'portal.dat': {0x47000001, 0x47000002, 0x1f000001, 0x1f000002},
                       'cell_1.dat': {0x0102ffff}}
            candidate = {'portal.dat': {0x47000001, 0x47000002, 0x1f000002},
                         'cell_1.dat': {0x0102ffff, 0x01020123}}
            output = root / 'result.tsv'
            with patch.object(prepare_content, 'ROOT', root), patch.object(prepare_content, 'ContentProfile'), patch.object(prepare_content, 'asset_index', side_effect=[current, candidate]):
                prepare_content.prepare(root / 'installed', output, root / 'candidate')
            report = json.loads(output.with_suffix('.report.json').read_text())
            self.assertEqual(output.read_text().splitlines(), ['pyrealium-world-v3', outdoor])
            self.assertEqual(report['missing_assets'][0]['missing_records'],
                             [{'kind': 'cell', 'archive': 'cell_1.dat', 'record': '01020123'}])
            self.assertEqual(report['comparison']['recoverable_placements'], 1)
            self.assertEqual(report['comparison']['currently_supported_placements_missing_in_candidate'][0]['missing_records'],
                             [{'kind': 'visual', 'archive': 'portal.dat', 'record': '1f000001'}])
            self.assertFalse((root / 'installed').exists())
            self.assertFalse((root / 'candidate').exists())


if __name__ == '__main__':
    unittest.main()
