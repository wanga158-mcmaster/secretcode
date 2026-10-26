"""Bounded replay-only diagnosis of the two authorized closed attention activity keys.

Regenerates recorded recipes through arena exp replay, queries saved history through
the existing exact exporter, and retains summaries only. It never runs a new policy.
"""
from __future__ import annotations
import argparse
from collections import Counter,defaultdict
from contextlib import redirect_stdout
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import struct
import sys
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools/arena'))
import core
import exp

EXPERIMENT='main/zero-camp-attention-first-fit'
CASES={'cornersA':('00corners','A'),'eightB':('00eight','B')}
OUT=ROOT/'.cache/main/zero/student-trace'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def cheb(a,b):return max(abs(a[0]-b[0]),abs(a[1]-b[1]))
def sign(v):return (v>0)-(v<0)
def reference(path):return {'path':str(path),'sha256':sha(path)}

def summarize(exact,bundle,map_name,side,recipe_id):
    records=[];static=None;actors=None;metadata=None;trailer=None
    with gzip.open(exact,'rt') as source:
        for line in source:
            row=json.loads(line)
            if row['type']=='input':records.append(row)
            elif row['type']=='static-map':static=row
            elif row['type']=='actors':actors=row
            elif row['type']=='metadata':metadata=row
            elif row['type']=='trailer':trailer=row
    team=0 if side=='A' else 1
    raw=(bundle/f'team-{side.lower()}.fuel').read_bytes()
    if raw[:13]!=bytes([97,54,50,108,101,117,102,0,0,0,1,0,0]) or (len(raw)-77)%8:raise ValueError('fuel stream ABI differs')
    fuels=list(struct.unpack('<'+'Q'*((len(raw)-77)//8),raw[77:]))
    fuel_index=0;draft_entries=0;turns={};turn_order=[];by_actor=defaultdict(list);counts=Counter();camp_contacts=[];champ_contacts=[]
    groups=[{'id':g['id'],'gold':g['gold'],'gems':g['gems'],'members':g['members']} for g in static['groups']]
    def nearest(at):
        options=[(cheb(at,m['at']),g['id'],m['at'],g['gold'],g['gems']) for g in groups for m in g['members']]
        d,g,t,gold,gems=min(options)
        return {'group':g,'distance':d,'at':t,'bearing':[sign(t[0]-at[0]),sign(t[1]-at[1])],'groupGold':gold,'groupGems':gems}
    def seen(before):
        return [{'id':c['id'],'at':c['at'],'hp':c['hp'],'group':c.get('group'),'distance':cheb(before['self']['at'],c['at'])} for c in before['visibleCamps']]
    for row in records:
        before=row['before'];moment=before['moment'];action=row['action'];own=before.get('self')
        if own is None:
            if action['kind']==19 and before.get('draftFacts',{}).get('team')==team:fuel_index+=1;draft_entries+=1
            continue
        if own['team']!=team:continue
        key=(moment['round'],moment['turn'],own['id'])
        if key not in turns:
            entry={'round':key[0],'turn':key[1],'actor':key[2],'firstInput':row['input'],'from':own['at'],'hp':own['hp'],'gold':own['gold'],'gems':own['gems'],
                   'channel':own['channel'],'stateBits':own['stateBits'],'allowances':own['allowances'],'nextPriority':own['nextPriority'],
                   'nearestStaticCamp':nearest(own['at']),'visibleCamps':seen(before),'visibleEnemies':[{'id':c['id'],'at':c['at'],'hp':c['hp']} for c in before['visibleChampions'] if c['team']!=team],
                   'commands':[],'fuelIndex':None,'fuel':None,'to':own['at']}
            turns[key]=entry;turn_order.append(entry);by_actor[own['id']].append(entry)
        entry=turns[key];counts[action['name']]+=1
        command={'input':row['input'],'kind':action['kind'],'name':action['name'],'from':action['from'],'to':action['to'],'priority':action['priority'],'slot':action['slot'],
                 'beforeGold':own['gold'],'afterGold':row['afterSelf']['gold'] if row['afterSelf'] else None,'beforePosition':own['at'],'afterPosition':row['afterSelf']['at'] if row['afterSelf'] else None}
        if action['kind'] in (5,6):
            camps=[c for c in before['visibleCamps'] if c['at']==action['to']]
            enemies=[c for c in before['visibleChampions'] if c['team']!=team and c['at']==action['to']]
            command['observedTargets']={'camps':[{'id':c['id'],'hp':c['hp'],'group':c.get('group')} for c in camps],'enemies':[{'id':c['id'],'hp':c['hp']} for c in enemies]}
            if camps:camp_contacts.append(dict(command,round=key[0],actor=key[2]))
            if enemies:champ_contacts.append(dict(command,round=key[0],actor=key[2]))
        entry['commands'].append(command)
        if row['afterSelf']:entry['to']=row['afterSelf']['at']
        if action['kind'] in (8,26,27):
            if fuel_index>=len(fuels):raise ValueError('fuel timeline shorter than completed owner events')
            entry['fuelIndex']=fuel_index;entry['fuel']=fuels[fuel_index];fuel_index+=1
    actor_summaries=[];ambiguous=[]
    for actor,history in sorted(by_actor.items()):
        moved=sum(t['from']!=t['to'] for t in history)
        reversals=[]
        for i in range(2,len(history)):
            if history[i]['from']==history[i-2]['from'] and history[i]['from']!=history[i-1]['from']:reversals.append(history[i]['round'])
        stationary=[];start=0
        while start<len(history):
            end=start+1
            while end<len(history) and history[end]['from']==history[start]['from'] and history[end-1]['to']==history[start]['from']:end+=1
            if end-start>=3:stationary.append({'fromRound':history[start]['round'],'toRound':history[end-1]['round'],'turns':end-start,'at':history[start]['from']})
            start=end
        actions=Counter(c['name'] for t in history for c in t['commands'])
        moved_before=False;candidate=None
        for t in history:
            if moved_before and all(c['kind']==8 for c in t['commands']) and t['hp']>0 and t['allowances']['move']:
                candidate=t;break
            moved_before=moved_before or any(c['kind']==4 for c in t['commands'])
        if candidate:ambiguous.append({k:candidate[k] for k in ['round','turn','actor','firstInput','from','fuel','nearestStaticCamp','visibleCamps','visibleEnemies','allowances']})
        actor_summaries.append({'actor':actor,'type':next((a['type'] for a in actors['roster'] if a['id']==actor),None),'turns':len(history),'movingTurns':moved,
                                'actions':dict(actions),'distinctPositions':len({tuple(t['from']) for t in history}),'first':history[0],'last':history[-1],
                                'positionReversalRounds':reversals,'stationaryRuns':stationary})
    own_records=[r for r in records if r['before'].get('self') and r['before']['self']['team']==team]
    gains=[{'input':r['input'],'round':r['before']['moment']['round'],'actor':r['before']['self']['id'],'before':r['before']['self']['gold'],'after':r['afterSelf']['gold']} for r in own_records if r['afterSelf'] and r['afterSelf']['gold']>r['before']['self']['gold']]
    log=(bundle/f'team-{side.lower()}.stdout').read_text(errors='replace')
    return {'schema':'zero-student-replay-trace/v1','purpose':'diagnostic_only_no_policy_change','experiment':EXPERIMENT,'case':{'map':map_name,'side':side,'subject':'trained','recipe':recipe_id},
            'window':{'maxRound':150,'firstInput':records[0]['input'] if records else None,'lastInput':records[-1]['input'] if records else None,'lastBeforeRound':records[-1]['before']['moment']['round'] if records else None,'stopped':trailer['stopped'],'allInputCount':len(records),'subjectInputCount':len(own_records),'subjectOwnerTurns':len(turn_order)},
            'source':{'export':reference(exact),'replay':reference(bundle/'replay.repl26a'),'recipe':json.loads((bundle/'recipe.json').read_text()),'exporter':reference(Path(__file__).with_name('exact_export.py'))},
            'fuelAlignment':{'method':'One team fuel entry per source draft-resolution19 and completed gameplay owner boundary8/26/27; carrier7 does not consume another invocation','entriesInCompleteGame':len(fuels),'prefixConsumed':fuel_index,'draftEntries':draft_entries,'healthScope':'Recorded aggregate health clean; individual decisions/admission unavailable'},
            'staticCampGroups':groups,'actions':dict(counts),'goldGains':gains,'campContacts':camp_contacts,'enemyContacts':champ_contacts,
            'actors':actor_summaries,'turns':turn_order,'firstAdmissionAmbiguity':min(ambiguous,key=lambda t:(t['round'],t['firstInput'])) if ambiguous else None,
            'ambiguityContract':'END-only and fuel used cannot distinguish a learned END from pre-root or post-root fuel admission; exact shared-selector inspection is required at the first ambiguous point. Static nearest means Chebyshev geometry, not an obstacle path.',
            'logs':{'bytes':len(log.encode()),'errorLines':[line for line in log.splitlines() if 'error' in line.lower()][:10]},'noNewPolicy':True,'noLearning':True}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--case',choices=[*CASES,'both'],default='both');args=ap.parse_args()
    directory,spec=exp.load(EXPERIMENT)
    if spec['status']!='closed' or not spec.get('private_snapshots'):raise ValueError('only recorded closed private experiment is authorized')
    export_spec=importlib.util.spec_from_file_location('zero_trace_exact',Path(__file__).with_name('exact_export.py'));exporter=importlib.util.module_from_spec(export_spec);export_spec.loader.exec_module(exporter)
    summaries=[]
    for case in CASES if args.case=='both' else [args.case]:
        map_name,side=CASES[case]
        rid,recipe=exp.replay_recipe(spec,'trained','anchors/main-v53',core.map_path(map_name),side)
        destination=OUT/f'{case}-{rid}';destination.mkdir(parents=True,exist_ok=True)
        bundle=directory/'replays'/f'{map_name}_{side}_{rid}'
        replay_log=io.StringIO()
        with core.FileLock(core.CACHE/'snap'/f'{rid}.lock',timeout=60):
            with redirect_stdout(replay_log):exp.cmd_replay(SimpleNamespace(name=EXPERIMENT,map=map_name,side=side,subject='trained',opponent='anchors/main-v53',native_role=None))
        (destination/'replay-command.txt').write_text(replay_log.getvalue())
        exact=ROOT/f'.cache/main/zero/exact/student-trace-{case}-{rid}/inputs.jsonl.gz'
        key=hashlib.sha256((sha(bundle/'replay.repl26a')+sha(Path(__file__).with_name('exact_export.py'))+'maxRound150').encode()).hexdigest()[:32]
        if not exact.exists():
            previous=sys.argv;capture=io.StringIO()
            try:
                sys.argv=['exact_export.py','--replay',str(bundle/'replay.repl26a'),'--max-round','150','--max-steps','200000','--timeout','90','--output',str(exact)]
                with core.FileLock(core.CACHE/'snap'/f'{key}.lock',timeout=60):
                    with redirect_stdout(capture):code=exporter.main()
                if code:raise RuntimeError('exact export failed')
            finally:sys.argv=previous
            (destination/'export-command.json').write_text(capture.getvalue())
        summary=summarize(exact,bundle,map_name,side,rid);summary['script']=reference(Path(__file__));summary['globalVersionLocks']=[str(core.CACHE/'snap'/f'{v}.lock') for v in [rid,key]]
        output=destination/'trace-summary.json';output.write_text(json.dumps(summary,indent=2)+'\n')
        summaries.append({'case':case,'summary':reference(output),'window':summary['window'],'actions':summary['actions'],'campContacts':len(summary['campContacts']),'goldGains':len(summary['goldGains']),'firstAdmissionAmbiguity':summary['firstAdmissionAmbiguity']})
        # Both files are regenerable owned cache. Keep only bounded summaries, not raw game bundles.
        if bundle.resolve().parent!= (directory/'replays').resolve():raise RuntimeError('bundle cleanup path escaped experiment')
        shutil.rmtree(bundle);exact.unlink()
        print(json.dumps(summaries[-1]),flush=True)
    OUT.mkdir(parents=True,exist_ok=True);(OUT/'aggregate.json').write_text(json.dumps({'schema':'zero-student-bounded-traces/v1','experiment':EXPERIMENT,'cases':summaries,'newExperiments':False,'newPolicy':False},indent=2)+'\n')
    return 0
if __name__=='__main__':raise SystemExit(main())
