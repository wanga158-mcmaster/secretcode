"""Independent saved actual fight_enemies caller check; no builds or games.

  python coalition.py OWNER-R2 FRESH-OUTPUT
Checks both sides, preserving the original failed E26 preparation and passed guest gate.
"""
from __future__ import annotations
import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[5]
NODE=Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
INSPECTOR=ROOT/'tools/inspect-strategy-replay.mjs'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    owner=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=False)
    source=owner/'probe.json';source_hash=sha(source);binding=json.loads(source.read_text());assert binding['passed']
    def observe(item):
        argv=[str(NODE),'--no-warnings',str(INSPECTOR),'--replay',str(ROOT/item['replay']),
              '--actor','all','--trace','1','--from-round','54','--to-round','54','--kinds','4,5,6']
        p=subprocess.run(argv,capture_output=True)
        if p.returncode:raise RuntimeError(p.stderr.decode('utf-8','replace')[-3000:])
        (out/(item['arm']+'-'+item['subject']+'.json')).write_bytes(p.stdout)
        return item,json.loads(p.stdout)
    with concurrent.futures.ThreadPoolExecutor() as pool:pairs=list(pool.map(observe,binding['games']))
    checked=[];before={}
    for item,doc in pairs:
        assert doc['engineVersion']=='1.0.3' and doc['decoderProfile']=='cli-3.0.5' and doc['stopped']=='round_range_end'
        t=doc['interactions'];assert not t['native']['errors']
        team=0 if item['subject']=='A' else 1
        own=lambda p:[29-p[0],29-p[1]] if team else p
        a,b=own([8,5]),own([9,5])
        for who,kind in(('tavi',11),('ryth',0)):
            actor=next(c['id'] for c in t['entities'] if c['team']==team and c['type']==kind)
            events=[e for e in t['events'] if e['actor']==actor]
            assert [e['kind'] for e in events]==['attack','cast','attack'],(item,who,'wrong preparation/caller action chain')
            reset,attack=events[1:]
            assert reset['action']['abilityType']==1
            state=t['states'][attack['actorState'][0]]
            assert state['availability']['attack'] and not state['availability']['cast']
            assert attack['action']['recordedFrom']==own([5,5] if who=='tavi' else [8,6])
            target=a if who=='tavi' or item['arm']=='on' else b
            assert attack['action']['recordedTo']==target
            victim=next(c['entity'] for c in attack['nearby']['before']['champions'] if c['at']==target and c['team']!=team)
            effect=next(c for c in attack['observed'] if c['entity']==victim)
            expected=(1200,1155) if who=='tavi' else (1155,1110) if item['arm']=='on' else (1000,955)
            assert effect['before']['health']==expected[0] and effect['after']['health']==expected[1]
            before[(item['arm'],item['subject'],who)]=state
            checked.append({'arm':item['arm'],'subject':item['subject'],'who':who,'actor':actor,
                            'callerAttackStep':attack['id'],'targetCoordinate':target,'observedEffect':effect,
                            'noMovement':not any(e['kind']=='move' for e in events)})
    for side in('A','B'):
        for who in('tavi','ryth'):assert before[('off',side,who)]==before[('on',side,who)]
    assert sha(source)==source_hash
    failed=owner.parent/'r1/probe.json'
    files=[Path(__file__),source,failed,INSPECTOR,ROOT/'tools/replay-interaction-native.mjs',ROOT/'tools/replay-interaction-events.mjs']
    result={'passed':True,'purpose':'conditional actual coalition fight_enemies contribution; stipulated caller focus, normal planner/admission and strength unknown',
            'nativeGamesObserved':4,'callerSequencesChecked':8,'matchedOffOnQueriedActorStates':4,'round':54,
            'entries':binding['entries'],'source_sha256':{str(p.relative_to(ROOT)):sha(p) for p in files},
            'preservedFailedE26Preparation':str(failed.relative_to(ROOT)),'checked':checked}
    (out/'coalition-verification.json').write_text(json.dumps(result,indent=1))
    print(json.dumps({k:result[k] for k in('passed','nativeGamesObserved','callerSequencesChecked','matchedOffOnQueriedActorStates')}))
    return 0
if __name__=='__main__':raise SystemExit(main())
