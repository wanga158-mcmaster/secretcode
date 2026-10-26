"""Check an exported C1 request's exact map pins and optional native module replay."""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[5]

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--request',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--expected-result',type=Path);p.add_argument('--rejected-context')
    a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    request=json.loads(a.request.read_text());assert all('map_sha256' in c for c in request['cases'])
    adapter=ROOT/'bots/contenders/tools/optimization/adapters/native_evaluator.py'
    def run(name,body,expected=0):
        path=out/(name+'.request.json');response=out/(name+'.response.json')
        path.write_text(json.dumps(body,indent=2)+'\n',encoding='utf8')
        process=subprocess.run([sys.executable,'-B','-I','-X','utf8',str(adapter),'--repo',str(ROOT),
            '--request',str(path),'--response',str(response)],cwd=ROOT,capture_output=True,text=True,timeout=600)
        (out/(name+'.stdout.txt')).write_text(process.stdout,encoding='utf8')
        (out/(name+'.stderr.txt')).write_text(process.stderr,encoding='utf8')
        assert process.returncode==expected,(process.returncode,process.stderr[-2000:])
        return json.loads(response.read_text())
    identity=copy.deepcopy(request);identity['target_kind']=identity['kind'];identity['kind']='identity'
    correct=run('correct-map-pins',identity)
    assert correct['healthy'] and correct['games_played']==0
    expected={(c['map_sha256'],c['side'],c['opponent']) for c in request['cases']}
    observed={(c['map_sha256'],c['side'],c['opponent']['key']) for c in correct['identity']['cases']}
    assert expected==observed
    wrong=copy.deepcopy(identity);wrong['cases'][0]['map_sha256']='0'*64
    rejected=run('wrong-map-pin',wrong,2)
    error=rejected.get('error',rejected.get('reason','')).lower()
    assert not rejected['healthy'] and ('hash' in error or 'map_sha256' in error),rejected
    checks=['correct-pins-identity','wrong-pin-fails-before-games']
    if a.rejected_context:
        stale=copy.deepcopy(identity);stale['expected_context_identity']=a.rejected_context
        rejected_context=run('old-context-pin',stale,2)
        assert not rejected_context['healthy'] and 'context identity mismatch' in rejected_context.get('error','').lower()
        checks.append('old-context-fails-before-candidate-build-or-games')
    if a.expected_result:
        original=json.loads(a.expected_result.read_text());replayed=run('native-replay',request)
        assert replayed['healthy'] and replayed['selection_eligible']
        assert replayed['candidate_identity']['modules']==original['candidate_identity']['modules']
        assert len(replayed['cases'])==len(request['cases'])
        for row in replayed['cases']:
            assert row['complete'] and all(not row[k] for k in ('health','opponent_health','baseline_health','baseline_opponent_health'))
        checks.append('exact-native-modules-and-healthy-paired-replay')
    summary=dict(passed=True,checks=checks,request_sha256=sha(a.request),adapter_sha256=sha(adapter),
        expected_cases=len(request['cases']),scope='Reproduction and map identity, not independent strength evidence')
    (out/'verification.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf8')
    print(json.dumps(summary))

if __name__=='__main__':main()
