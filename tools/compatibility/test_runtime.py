import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import update as u
import schema_profile as sp
import collect_runtime as cr


class RuntimeTests(unittest.TestCase):
    def evidence(self):
        identity = dict(timestamp=1, size=100000, sha256='a'*64)
        return dict(format=1, created=123, schema=self.schema(), globals=dict.fromkeys(u.GLOBALS,100),
            source_profile_hashes=dict.fromkeys(u.FILES,'a'*64),
            modules={'client.dll':identity,'engine2.dll':identity},
            commands=dict(identity,object=100,vtable=200,function=300,prefix='00'*32),
            buttons={'buttons':{key:{'rva':100} for key in u.BUTTONS}})
    def schema(self):
        result = {}
        for index, (owner, name) in enumerate(list(sp.FIELDS.values())+sp.ITEM):
            result.setdefault(owner, {'size':4096, 'fields':{}})['fields'][name] = index*8
        return result

    def test_all_fields_and_nested_item(self):
        schema = self.schema(); result = sp.offsets(schema)
        self.assertEqual(len(result), 33)
        self.assertEqual(result['itemId'], sum(schema[c]['fields'][f] for c,f in sp.ITEM))

    def test_missing_schema_never_inherits(self):
        schema = self.schema(); del schema['C_BaseEntity']['fields']['m_iHealth']
        with self.assertRaisesRegex(ValueError, 'm_iHealth'): sp.offsets(schema)

    def test_schema_range_and_type(self):
        for value in (-1, 4096, True, '844'):
            schema = self.schema(); schema['C_BaseEntity']['fields']['m_iHealth'] = value
            with self.assertRaises(ValueError): sp.offsets(schema)

    def test_button_cycle_is_rejected(self):
        pe = mock.Mock(); pe.rip.return_value = 16; pe.size = 0x10000
        process = mock.Mock(); process.q.return_value = 0x12000; process.string.return_value = 'forward'
        data = bytearray(0x90); struct.pack_into('<Q', data, 0x88, 0x12000)
        process.read.return_value = bytes(data)
        with self.assertRaisesRegex(ValueError, 'cycle'): cr.buttons(process, 0x10000, pe)

    def test_prepare_runtime_stays_unapproved(self):
        identity = dict(timestamp=1, size=100000, sha256='a'*64)
        evidence = self.evidence()
        report = dict(eligible=False,version=2000938,client=identity,engine=identity,limits=[],blockers=['approval missing'],profile_hashes=evidence['source_profile_hashes'])
        with tempfile.TemporaryDirectory() as td, mock.patch.object(u,'inspect',return_value=(report,{n:{} for n in u.FILES})), mock.patch.object(cr,'collect',return_value=evidence):
            output=Path(td)/'run'
            result=u.prepare('game','assets',output,42,123)
            cr.collect.assert_called_once_with(42,'assets',123)
            self.assertFalse(result['eligible'])
            self.assertFalse((output/'profiles').exists())
            self.assertTrue((output/'candidate-profiles/memory.json').exists())
            self.assertFalse(result['runtime_collection']['gameplay_verified'])
            self.assertEqual(result['candidate_hashes']['memory.json'],u.profile_digest(output/'candidate-profiles/memory.json'))

    def test_runtime_identity_mismatch_blocks_package(self):
        expected=dict(timestamp=1,size=100,sha256='a'*64)
        evidence={'modules':{'client.dll':dict(expected,sha256='b'*64)}}
        with tempfile.TemporaryDirectory() as td, mock.patch.object(u,'inspect',return_value=({'client':expected},{})), mock.patch.object(cr,'collect',return_value=evidence):
            output=Path(td)/'run'
            with self.assertRaisesRegex(ValueError,'differs'):u.prepare('game','assets',output,42)
            self.assertFalse((output/'report.json').exists())
            self.assertFalse(u.read_json(output/'failure-report.json')['eligible'])

    def test_missing_required_button(self):
        evidence=dict(format=1,schema=self.schema(),modules={'client.dll':{}},buttons={'buttons':{}})
        with self.assertRaisesRegex(ValueError,'Missing required'):sp.candidates(evidence,{}, {},2000938)

    def test_restarted_process_rejected_and_handle_closed(self):
        process=mock.Mock(created=123)
        with mock.patch.object(cr,'Process',return_value=process):
            with self.assertRaisesRegex(ValueError,'restarted'):cr.collect(42,'assets',456)
        process.close.assert_called_once()

    def test_incomplete_globals_cannot_inherit_old_addresses(self):
        evidence=self.evidence(); del evidence['globals']['pawn']
        with self.assertRaisesRegex(ValueError,'Incomplete runtime globals'):
            sp.candidates(evidence,{'pawn':123456},{},2000938)

    def test_incomplete_commands_cannot_inherit_old_addresses(self):
        evidence=self.evidence(); del evidence['commands']['function']
        with self.assertRaisesRegex(ValueError,'Incomplete runtime command'):
            sp.candidates(evidence,{}, {'function':123456},2000938)

    def test_invalid_runtime_address_ranges(self):
        for value in (True,-1,100000):
            for group in ('globals','buttons'):
                evidence=self.evidence()
                if group=='globals':evidence[group]['pawn']=value
                else:evidence[group]['buttons']['forward']['rva']=value
                with self.assertRaises(ValueError):sp.candidates(evidence,{},{},2000938)

    def test_runtime_disagreement_revokes_approval(self):
        evidence=self.evidence(); identity=evidence['modules']['client.dll']
        profiles=sp.candidates(evidence,{},{},2000938)
        profiles['memory.json']['health']+=4
        report=dict(eligible=True,status='confirmed-profile-reuse',version=2000938,client=identity,engine=identity,limits=[],blockers=[],profile_hashes=evidence['source_profile_hashes'])
        with tempfile.TemporaryDirectory() as td, mock.patch.object(u,'inspect',return_value=(report,profiles)), mock.patch.object(cr,'collect',return_value=evidence):
            output=Path(td)/'run'; result=u.prepare('game','assets',output,42)
            self.assertFalse(result['eligible'])
            self.assertEqual(result['status'],'runtime-profile-mismatch')
            self.assertFalse((output/'profiles').exists())
            self.assertEqual(result['candidate_changes'][0]['field'],'memory.json.health')

    def test_runtime_changes_preserve_types_and_missing_keys(self):
        old={n:{} for n in u.FILES}; new=copy.deepcopy(old)
        old[u.FILES[0]]={'one':True,'gone':5};new[u.FILES[0]]={'one':1,'added':8}
        rows=u.runtime_changes(old,new)
        self.assertEqual({r['status'] for r in rows},{'added','removed','changed'})

    def test_invalid_creation_time_never_opens_process(self):
        for created in (0,-1,True,2**64):
            with tempfile.TemporaryDirectory() as td, mock.patch.object(u,'inspect',return_value=({},{})), mock.patch.object(cr,'collect') as collect:
                with self.assertRaisesRegex(ValueError,'creation time'):u.prepare('game','assets',Path(td)/'run',42,created)
                collect.assert_not_called()

    def test_failed_collection_keeps_offline_evidence_not_package(self):
        offline=dict(eligible=True,version=2000938,candidates={'globals':{'pawn':123}},blockers=[])
        with tempfile.TemporaryDirectory() as td, mock.patch.object(u,'inspect',return_value=(offline,{})), mock.patch.object(cr,'collect',side_effect=OSError('process closed')):
            output=Path(td)/'run'
            with self.assertRaisesRegex(OSError,'process closed'):u.prepare('game','assets',output,42)
            failure=u.read_json(output/'failure-report.json')
            self.assertEqual(failure['reason'],'process closed')
            self.assertEqual(failure['candidates'],offline['candidates'])
            self.assertFalse(failure['eligible'])
            self.assertFalse((output/'profiles').exists())
            self.assertFalse((output/'report.json').exists())

    def test_killed_collector_leaves_unapproved_progress(self):
        with tempfile.TemporaryDirectory() as td:
            output=Path(td)/'run'
            program="import os,sys;from pathlib import Path;import update;\nwith update.runtime_attempt(Path(sys.argv[1]),{'eligible':True},'game','assets',42,123):\n os._exit(19)\n"
            run=subprocess.run([sys.executable,'-c',program,str(output)],cwd=Path(u.__file__).parent,capture_output=True,timeout=10)
            self.assertEqual(run.returncode,19,run.stderr)
            progress=u.read_json(output/'collection-progress.json')
            self.assertEqual(progress['status'],'runtime-collection-in-progress')
            self.assertFalse(progress['eligible'])
            self.assertFalse((output/'report.json').exists())

    def test_phase_error_identifies_component(self):
        def fail():raise ValueError('ambiguous signature')
        with self.assertRaisesRegex(ValueError,'schema: ambiguous signature'):cr.phase('schema',fail)

    def player_values(self):
        return dict(health=100,life=0,position=[1.,2.,3.],velocity=[0.,0.,0.],angles=[0.,180.,0.],sensitivity=2.3)

    def test_dead_player_is_valid_data(self):
        values=self.player_values();values.update(health=0,life=1)
        self.assertEqual(cr.validate_player(values)['health'],0)
        self.assertEqual(cr.validate_player(values)['validation'],'plausibility-only')

    def test_nonfinite_or_out_of_bounds_vectors_rejected(self):
        for field in ('position','velocity','angles'):
            for value in (float('nan'),float('inf'),1e20,True):
                values=self.player_values();values[field][1]=value
                with self.subTest(field=field,value=value),self.assertRaisesRegex(ValueError,field):cr.validate_player(values)

    def test_impossible_pitch_and_sensitivity_rejected(self):
        values=self.player_values();values['angles'][0]=91
        with self.assertRaisesRegex(ValueError,'pitch'):cr.validate_player(values)
        for value in (0,-1,101,float('nan'),float('inf'),True):
            values=self.player_values();values['sensitivity']=value
            with self.assertRaisesRegex(ValueError,'Sensitivity'):cr.validate_player(values)

    def test_health_and_life_limits(self):
        for key,value in [('health',-1),('health',501),('health',True),('life',4)]:
            values=self.player_values();values[key]=value
            with self.assertRaises(ValueError):cr.validate_player(values)

    def test_snapshot_rejects_respawn_during_read(self):
        process=mock.Mock();process.q.side_effect=[0x50000,0x60000,0x70000]
        fields=dict(health=4,life=8,position=16,velocity=32)
        values={0x50004:struct.pack('<i',100),0x50008:b'\0',0x50010:struct.pack('<fff',1,2,3),
                0x50020:struct.pack('<fff',0,0,0),0x10030:struct.pack('<fff',0,0,0),0x60058:struct.pack('<f',2.3)}
        process.read.side_effect=lambda address,size:values[address]
        with self.assertRaisesRegex(ValueError,'Player changed'):
            cr.player_snapshot(process,0x10000,dict(pawn=16,sensitivity=32,angles=48),fields,88)

    def test_menu_does_not_produce_player_sample(self):
        process=mock.Mock();process.q.return_value=0
        with self.assertRaisesRegex(ValueError,'игрок пока недоступен'):
            cr.player_snapshot(process,0x10000,dict(pawn=16),{},88)
        process.read.assert_not_called()

    def test_profile_snapshot_rejects_changed_files(self):
        with tempfile.TemporaryDirectory() as td:
            for name in u.FILES:(Path(td)/name).write_text('{"value":1}')
            snapshot=cr.ProfileSnapshot(td)
            (Path(td)/'memory.json').write_text('{"value":2}')
            self.assertEqual(snapshot.profiles['memory.json']['value'],1)
            with self.assertRaisesRegex(ValueError,'Source profile changed'):snapshot.verify()

    def test_profile_snapshot_verifies_same_bytes(self):
        with tempfile.TemporaryDirectory() as td:
            for name in u.FILES:(Path(td)/name).write_text('{"value":1}')
            snapshot=cr.ProfileSnapshot(td);snapshot.verify()
            self.assertEqual(snapshot.hashes['memory.json'],u.profile_digest(Path(td)/'memory.json'))

    def test_different_offline_and_runtime_sources_rejected(self):
        evidence=self.evidence();identity=evidence['modules']['client.dll']
        report=dict(eligible=True,client=identity,engine=identity,profile_hashes=dict.fromkeys(u.FILES,'b'*64))
        with tempfile.TemporaryDirectory() as td,mock.patch.object(u,'inspect',return_value=(report,{})),mock.patch.object(cr,'collect',return_value=evidence):
            output=Path(td)/'run'
            with self.assertRaisesRegex(ValueError,'between offline inspection'):u.prepare('game','assets',output,42)
            self.assertFalse((output/'candidate-profiles').exists())
            self.assertFalse(u.read_json(output/'failure-report.json')['eligible'])


if __name__ == '__main__':
    unittest.main()
