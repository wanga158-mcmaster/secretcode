"""Actual native complete camp-cycle and fail-closed projection contract checks."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[5])
    parser.add_argument('--out',type=Path)
    parser.add_argument('--model-case',type=Path,help='Verified C++ cache row to test positive and per-field negative model predictions')
    args=parser.parse_args();root=args.repo.resolve()
    out=args.out or root/'.cache/contenders/optimization/c11/camp-tests'
    out.mkdir(parents=True,exist_ok=True)
    adapter=root/'bots/contenders/tools/optimization/adapters/native_camp_oracle.py'
    def invoke(name,request,success=True):
        path=out/(name+'-request.json');response=out/(name+'-response.json')
        path.write_text(json.dumps(request,sort_keys=True,separators=(',',':')),encoding='utf-8')
        child=subprocess.run([sys.executable,'-B','-I','-X','utf8',str(adapter),'--repo',str(root),'--request',str(path),'--response',str(response)],capture_output=True,text=True)
        if success: assert child.returncode==0,child.stderr
        else: assert child.returncode!=0
        body=json.loads(response.read_text());assert body['request_sha256']==hashlib.sha256(path.read_bytes()).hexdigest()
        return body
    first=invoke('source',{'kind':'camp_scenario','source_input':1542})
    second=invoke('source-repeat',{'kind':'camp_scenario','source_input':1542})
    assert first==second and first['native'] and first['supported']
    assert first['before']['camps'][0]['hp']==30 and first['before']['actor']['hp']==800
    actions=[{'type':'basic','target_kind':'camp','target':0},{'type':'move','x':3,'y':3}]+[{'type':'end'}]*6+[{'type':'move','x':2,'y':2}]
    trace=invoke('complete-capture',{'kind':'camp_trace','source_input':1542,'scenario':first['scenario'],'actions':actions})
    assert all(row['applied'] for row in trace['transitions'])
    assert trace['after']['round']==70
    assert trace['after']['actor']['gems']==4 and trace['after']['actor']['gold']==60
    assert trace['after']['actor']['x']==2 and trace['after']['actor']['y']==2
    assert trace['after']['group']['state']==3 and trace['after']['group']['capture_team']==2
    assert not trace['passed'] and trace['scope']=='native-observed-trace'
    empty=invoke('empty-model-refused',{'kind':'scenario_validation','source_input':1542,'actions':actions,'expected_states':[]},False)
    assert not empty['passed'] and not empty['supported']
    tampered=json.loads(json.dumps(first['scenario']));tampered['actors'][0]['hp']+=1
    altered=invoke('tampered-native-state',{'kind':'camp_trace','source_input':1542,'scenario':tampered,'actions':[]},False)
    assert not altered['supported']
    unknown=invoke('unknown-action-field',{'kind':'camp_trace','source_input':1542,'actions':[{'type':'end','owner':5}]},False)
    assert not unknown['supported']
    wrong_hash=invoke('wrong-scenario-hash',{'kind':'camp_trace','source_input':1542,'scenario_sha256':'0'*64,'actions':[]},False)
    assert not wrong_hash['supported']
    if args.model_case:
        row=json.loads(args.model_case.read_text(encoding='utf-8'))
        request=row['request'];assert request['source_input']==1542
        request['scenario']=first['scenario']
        canonical=lambda value:json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False,allow_nan=False)
        digest=lambda value:hashlib.sha256(canonical(value).encode('utf-8')).hexdigest()
        def bind(value):
            value['scenario_sha256']=digest(value['scenario'])
            value['trace_sha256']=digest(value['actions'])
            value['prediction_sha256']=digest(value['expected_states'])
            value['comparison_projection']='restricted-model-after-every-action'
        bind(request)
        positive=invoke('cpp-complete-model',request)
        assert positive['native'] and positive['supported'] and positive['passed'] and not positive['differences']
        assert positive['scope']=='entire-trace' and positive['prediction_sha256']==request['prediction_sha256']
        assert positive['comparison_projection']=='restricted-model-after-every-action'
        assert positive['query_status']['errors']==0
        negative=json.loads(json.dumps(request));state=negative['expected_states'][0];actor=state['actors'][0]
        fields={'round'};state['round']+=1
        for field in ['x','y','hp','max_hp','strength','focus','range','shield','shield_ticks','stun','gold','gems']:
            actor[field]+=1;fields.add('actor.'+field)
        actor['completed']=not actor['completed'];fields.add('actor.completed')
        actor['channel']='recall';fields.add('actor.channel')
        for field in ['can_move','can_attack','can_cast']:
            actor[field]=not actor[field];fields.add('actor.'+field)
        for spell in actor['spells']:spell['cooldown']+=1
        fields.add('actor.cooldowns')
        state['camps'][0]['hp']+=1;state['camps'][0]['awake']=not state['camps'][0]['awake']
        state['camps'][1]['hp']+=1;fields.update(['camp0.hp','camp0.awake','camp1.hp'])
        state['groups'][0]['status']='reward';state['groups'][0]['capture_team']=0
        fields.update(['group.state','group.capture_team','group.reward_available'])
        negative['expected_states'][-1]['groups'][0]['respawn_left']-=1;fields.add('group.respawn')
        bind(negative);rejected=invoke('all-claimed-fields-negative',negative)
        assert rejected['native'] and rejected['supported'] and not rejected['passed']
        actual={difference['field'] for difference in rejected['differences']}
        assert fields<=actual,(fields-actual,actual)
        hash_tamper=json.loads(json.dumps(request));hash_tamper['prediction_sha256']='0'*64
        bad_digest=invoke('prediction-hash-negative',hash_tamper,False);assert not bad_digest['passed']
        absent=json.loads(json.dumps(request));del absent['expected_states'][0]['actors'][0]['hp'];bind(absent)
        incomplete=invoke('missing-required-prediction-field',absent,False);assert not incomplete['passed']
        print('C11 positive complete C++ model and '+str(len(fields))+' claimed dynamic field negatives passed')
    print('C11 actual native camp-cycle, independent branches, complete payout/departure and fail-closed tests passed')

if __name__=='__main__':main()
