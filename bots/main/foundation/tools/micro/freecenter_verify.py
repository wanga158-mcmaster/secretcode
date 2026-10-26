"""Pure verification of the completed free-center caller r2; no games or observations.
  python freecenter_verify.py OWNER-RUN
Preserves the original over-prescribed-center failure and verifies the actual optimum.
"""
import hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[5]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    run=Path(sys.argv[1]).resolve();original=run/'caller.json';binding=json.loads(original.read_text());original_hash=sha(original)
    out=run/'verification.json'
    if out.exists():raise SystemExit('refusing existing verification')
    checked=[];before={};inputs=[original,Path(__file__)]
    for side in('A','B'):
        team=0 if side=='A' else 1
        for arm in('off','on'):
            p=run/(arm+'-'+side+'.json');inputs.append(p);doc=json.loads(p.read_text());t=doc['interactions']
            assert doc['engineVersion']=='1.0.3' and doc['stopped']=='round_range_end' and not t['native']['errors']
            actor=next(c['id'] for c in t['entities'] if c['team']==team and c['type']==0)
            events=[e for e in t['events'] if e['actor']==actor];assert [e['kind'] for e in events]==['attack','cast']
            cast=events[1];expected=[17,9] if arm=='off' else[17,10]
            if team:expected=[29-expected[0],29-expected[1]]
            assert cast['action']['recordedTo']==expected
            own=t['states'][cast['actorState'][0]];before[(arm,side)]=own
            assert own['health']==1180 and own['focus']==100 and own['availability']['cast'] and own['cooldowns'][0]==0
            foes=[d for d in cast['observed'] if d['entity'].startswith('champion:') and 'health' in(d.get('before')or{}) and d['entity']!='champion:'+str(actor)]
            assert len(foes)==(2 if arm=='off' else 3)
            assert all(d['before']['health']-d['after']['health']==100 for d in foes)
            camp=next(d for d in cast['observed'] if d['entity'].startswith('camp:'))
            assert camp['before']['health']==1350 and camp['after']['health']==1250
            self_effect=next(d for d in cast['observed'] if d['entity']=='champion:'+str(actor))
            assert self_effect['after'].get('health',1180)==(1180 if arm=='off' else 1160)
            checked.append({'arm':arm,'subject':side,'castStep':cast['id'],'center':expected,'enemyEffects':foes,
                            'preservedCampEffect':camp,'selfEffect':self_effect})
        assert before[('off',side)]==before[('on',side)]
    assert sha(original)==original_hash
    result={'passed':True,'purpose':'conditional actual fight_enemies AoE refinement; not normal strategic admission or strength',
            'nativeGames':4,'matchedOffOnQueriedActorStates':2,'checked':checked,
            'preservedOriginalCenterExpectationFailure':{'path':str(original.relative_to(ROOT)),'sha256':original_hash,'failures':binding['failures']},
            'source_sha256':{str(p.relative_to(ROOT)):sha(p) for p in inputs},'entries':binding['entries']}
    out.write_text(json.dumps(result,indent=1))
    print(json.dumps({'passed':True,'nativeGames':4,'matchedOffOnQueriedActorStates':2}))
    return 0
if __name__=='__main__':raise SystemExit(main())
