"""Self-contained trace-parser seam tests; no saved/native/decoder dependency.

Copy into tools/arena/tests or patched/tests beside ../economic_renewal.py.
For an independent scratch run, ECONOMIC_RENEWAL_MODULE selects that module and
ECONOMIC_RENEWAL_TEST_TMPDIR selects regenerable temporary fixture storage.
The fake helper validates local file hashes but deliberately does not stand in
for the full activity/provenance validator; this suite proves parser rejection.
"""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
import zipfile

HERE=Path(__file__).resolve().parent
MODULE=Path(os.environ.get('ECONOMIC_RENEWAL_MODULE',str(HERE.parent/'economic_renewal.py')))
spec=importlib.util.spec_from_file_location('_economic_metadata_fixture',MODULE)
e=importlib.util.module_from_spec(spec);spec.loader.exec_module(e)

class FakePinnedHelper:
    @staticmethod
    def strict_json(raw):
        def pairs(items):
            out={}
            for key,value in items:
                if key in out:raise ValueError('duplicate key')
                out[key]=value
            return out
        return json.loads(raw,object_pairs_hook=pairs,
                          parse_constant=lambda value:(_ for _ in ()).throw(ValueError('nonfinite constant')))
    @staticmethod
    def local(root,value):
        path=(root/value).resolve()
        if not path.is_relative_to(root):raise ValueError('path outside fixture root')
        return path
    @classmethod
    def pinned(cls,root,item):
        path=cls.local(root,item['path']);raw=path.read_bytes()
        if hashlib.sha256(raw).hexdigest()!=item['sha256'] or len(raw)!=item['size']:
            raise ValueError('fixture hash mismatch')
        return path,raw

def event_id(prefix,native):
    actor=native['actor']
    values=[native[k] for k in ('round','input','substep','phase','kind')]
    values += [actor['kind'],actor['id'],native['processPhaseAfter'],
               int(native['awaitingInputAfter']),native['snapshotRound']]
    return prefix+':'+':'.join(str(value) for value in values)

class MetadataFixtures(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='economic-metadata-',
            dir=os.environ.get('ECONOMIC_RENEWAL_TEST_TMPDIR'))
        self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name).resolve()
    def descriptor(self,path):
        raw=path.read_bytes()
        return {'path':path.relative_to(self.root).as_posix(),
                'sha256':hashlib.sha256(raw).hexdigest(),'size':len(raw)}
    def json_file(self,name,value):
        path=self.root/name;path.write_text(json.dumps(value),encoding='utf-8')
        return self.descriptor(path)
    def fixture(self,mutate=None,overflow=False):
        payload=b'local synthetic replay bytes; parser seam only'
        archive=self.root/'output.zip'
        with zipfile.ZipFile(archive,'w') as z:z.writestr('replay.repl26a',payload)
        archive_desc=self.descriptor(archive);prefix=archive_desc['sha256'][:16]
        decoder='029ecd290dc400e75418178ec386d72cbd7d9f2fc2a2e451bf594ca23566df48'
        run=self.json_file('run.json',{'engine':{'mapEditorWasmSha256':decoder}})
        statistics=self.json_file('statistics.json',{
            'match':{'roundsStarted':20,'finalInput':100,'finalSubstep':20},
            'roster':{'champions':[{'id':5,'team':'teamB','name':'Fixture'}]}})
        first={'round':10,'input':50,'substep':0,'phase':0,'kind':0,'snapshotRound':10,
               'actor':{'kind':0,'id':0},'currentChampion':4294967295,
               'processPhaseAfter':1,'awaitingInputAfter':False}
        last={'round':10,'input':50,'substep':1,'phase':13,'kind':3,'snapshotRound':10,
              'actor':{'kind':1,'id':5},'currentChampion':4294967295,
              'processPhaseAfter':0,'awaitingInputAfter':False}
        rows=[{'record':'event','id':event_id(prefix,first),'round':10,'kind':'phase',
               'actor':None,'team':None,'native':first,'observed':[]},
              {'record':'event','id':event_id(prefix,last),'round':10,'kind':'tick',
               'actor':5,'team':1,'native':last,'observed':[{'entity':'champion:5',
                   'before':{'gold':0,'gems':0},'after':{'gold':2,'gems':14}}]}]
        endpoint=lambda native:{key:native[key] for key in ('round','input','substep','phase','kind','snapshotRound')}
        coverage={'requested':{'fromRound':10,'toRound':10},
                  'firstObserved':endpoint(first),'lastObserved':endpoint(last),'lastRetained':endpoint(last),
                  'nativeObservations':10,'duplicateStates':0,'candidateEvents':2,'omittedByFilter':0,
                  'retainedEvents':2,'maxEvents':100,'maxBytes':100000,'estimatedPayloadBytes':1000,
                  'complete':True,'truncated':False,'historyExhausted':False,'gameFinished':False,
                  'requestedEndReached':True,'emptyWindow':False,
                  'lastNativeMoment':{'round':11,'input':50,'substep':2,'phase':1,'awaitingInput':False,
                    'step':{'round':11,'input':50,'substep':2,'phase':0,'kind':0}},
                  'stopped':'round_range_end','firstBeforeStateAvailable':True,
                  'queryFailures':{},'queryObservationComplete':True,'note':'synthetic parser seam'}
        meta={'record':'metadata','readOnly':True,'schemaVersion':'2.0','engineVersion':'1.0.3',
              'decoderProfile':'cli-3.0.5','replayFormatVersion':'0.0.3',
              'replaySha256':archive_desc['sha256'],'replayPath':str(archive),
              'payloadSha256':hashlib.sha256(payload).hexdigest(),'decoderSha256':decoder,
              'profileRegistrySha256':'97d499cc07741f35cdd47fbdf2fe4d12b62967f4dc85010f74c1e672499641a0',
              'profileModuleSha256':'ab8a759756512de90d6571e64661adbd753627a658eb676782e5cf6f5b05c68f',
              'helperSha256':{'replay-interaction-events.mjs':e.EXTRACTOR_SHA,
                'replay-interaction-native.mjs':'7eb68e4a979015a60696e92ebb6c996cb12ddf9522f12510e279e936f1dccc15',
                'replay-native-slot.py':'6bbb65fe8cb42b92e7eca5d231f4ce147f02f1d06963bce14029f605aa5d10ed'},
              'stopped':'round_range_end','fromRound':10,'toRound':10,'timelineInputs':100,'inspectedSteps':10,
              'interactions':{'schemaVersion':'interaction-trace-v1',
                 'filters':{'actors':None,'teams':None,'targets':None,'kinds':None,'nearbyRadius':4},
                 'coverage':coverage}}
        if mutate:mutate(meta,rows)
        raw='\n'.join(json.dumps(row,separators=(',',':')) for row in [meta]+rows)+'\n'
        if overflow:
            self.assertEqual(raw.count('Infinity'),1);raw=raw.replace('Infinity','1e999')
        trace=self.root/'trace.jsonl';trace.write_text(raw,encoding='utf-8',newline='\n')
        return {'saved_case':{'archive':archive_desc,'run':run,'statistics':statistics},
                'traces':[self.descriptor(trace)]}
    def reject(self,mutate,overflow=False):
        case=self.fixture(mutate,overflow)
        with self.assertRaises((e.EvidenceError,ValueError,KeyError,TypeError)):
            e.load_traces(case,self.root,FakePinnedHelper)
    def test_valid_complete_point_window(self):
        events,pins=e.load_traces(self.fixture(),self.root,FakePinnedHelper)
        self.assertEqual(len(events),2);self.assertEqual(len(pins),1)
    def test_retained_count_matches_actual_rows(self):
        self.reject(lambda m,r:m['interactions']['coverage'].update(retainedEvents=1))
    def test_missing_and_falsy_query_failures(self):
        for value in (None,False,0,[], ''):
            with self.subTest(value=value):
                self.reject(lambda m,r:m['interactions']['coverage'].update(queryFailures=value))
    def test_missing_query_failure_key(self):
        self.reject(lambda m,r:m['interactions']['coverage'].pop('queryFailures'))
    def test_bool_omitted_count(self):
        self.reject(lambda m,r:m['interactions']['coverage'].update(omittedByFilter=False))
    def test_endpoint_retained_mismatch(self):
        self.reject(lambda m,r:m['interactions']['coverage']['lastRetained'].update(substep=0))
    def test_coherent_id_outside_window(self):
        def outside(m,rows):
            rows[0]['round']=9;rows[0]['native']['round']=9;rows[0]['native']['snapshotRound']=9
            rows[0]['id']=event_id(m['replaySha256'][:16],rows[0]['native'])
        self.reject(outside)
    def test_wrong_native_id(self):
        self.reject(lambda m,r:r[0].update(id=r[0]['id']+':extra'))
    def test_wrong_profile(self):
        self.reject(lambda m,r:m.update(profileModuleSha256='0'*64))
    def test_wrong_decoder(self):
        self.reject(lambda m,r:m.update(decoderSha256='0'*64))
    def test_wrong_replay_payload(self):
        self.reject(lambda m,r:m.update(payloadSha256='0'*64))
    def test_float_native_identity(self):
        self.reject(lambda m,r:r[1]['native']['actor'].update(id=5.0))
    def test_float_team_alias(self):
        self.reject(lambda m,r:r[1].update(team=1.0))
    def test_json_numeric_overflow(self):
        self.reject(lambda m,r:r[1]['observed'][0]['after'].update(gold=float('inf')),True)

if __name__=='__main__':unittest.main(verbosity=2)
