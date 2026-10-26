"""Read saved pair games with the current official observer, retaining raw priority values.

  python pair_verify.py RUN --observe
  python pair_verify.py RUN --verify
The observation pass is one-time/fresh. Pure verification can be repaired without replaying.
The projector differs only by retaining native.step.priority, which its upstream sampler reads.
"""
from __future__ import annotations
import concurrent.futures
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[5]
NODE=Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
sys.path.insert(0,str(ROOT/'tools/arena'))
import compiled_runner as contained


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def once(text,old,new):
    if text.count(old)!=1:raise RuntimeError('unexpected upstream locator/projection: '+old)
    return text.replace(old,new)


def prepare_source(dest):
    sources=dest/'source';sources.mkdir()
    base=ROOT/'tools/inspect-strategy-replay.mjs'
    events=ROOT/'tools/replay-interaction-events.mjs'
    event_source=sources/'pair-events.mjs'
    event_text=once(events.read_text(encoding='utf-8'),
        'native: {...native, actor: step.actor ?? null,',
        'native: {...native, priority: step.priority ?? null, actor: step.actor ?? null,')
    event_source.write_text(event_text,encoding='utf-8',newline='\n')
    text=base.read_text(encoding='utf-8')
    for name in('replay-profiles.mjs','replay-interaction-native.mjs'):
        text=once(text,"'./"+name+"'",json.dumps((ROOT/'tools'/name).as_uri()))
    text=once(text,"'./replay-interaction-events.mjs'",json.dumps(event_source.as_uri()))
    text=once(text,"const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');",
              'const ROOT = '+json.dumps(ROOT.as_posix())+';')
    # Python below already owns the single real Slot and private Job. Only this child sees the marker.
    text="process.env.ARENA_REPLAY_SLOT = '1';\n"+text
    observer=sources/'pair-observer.mjs'
    observer.write_text(text,encoding='utf-8',newline='\n')
    metadata={'base_inspector':str(base),'base_inspector_sha256':sha(base),
              'base_events':str(events),'base_events_sha256':sha(events),
              'active_inspector':str(observer),'active_inspector_sha256':sha(observer),
              'active_events':str(event_source),'active_events_sha256':sha(event_source),
              'changes':['one native.priority projection field','absolute import/root locators for owned source',
                         'child-only recursion marker; real Slot and private Job held by compiled_runner._slotted_command'],
              'unchanged':'official engine/profile/ABI/query logic and chronological event projection otherwise unchanged'}
    (sources/'binding.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    return observer,metadata


def observe_one(item,dest,observer,binding,geometry):
    name=item['arm']+'-'+item['subject']
    directory=dest/name;directory.mkdir()
    argv=[str(NODE),'--no-warnings',str(observer),'--replay',str(ROOT/item['replay']),
          '--actor','all','--trace','1','--from-round','29','--to-round','76',
          '--kinds','2,4,5,6,9,11,12,13,18,27','--captures','1',
          '--geometry',str(geometry),'--map-id','map']
    record=None
    try:
        record=contained._slotted_command(lambda count:argv,ROOT,directory,120,jobs=1)
    finally:
        if not contained.confirm_pending_cleanup():raise RuntimeError('owned observer cleanup unconfirmed')
    (directory/'process.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    if record.get('exit_code',record.get('returncode')) not in(0,None):
        raise RuntimeError((directory/'stderr.bin').read_text(encoding='utf-8',errors='replace')[-3000:])
    data=json.loads((directory/'stdout.bin').read_bytes())
    data['sourceProjection']={**binding,'upstreamDeclaredToolSha256':data.get('toolSha256'),
                              'upstreamDeclaredHelperSha256':data.get('helperSha256'),
                              'note':'Raw stdout keeps upstream fingerprint declarations; this normalized record supplies the exact active source identities.'}
    data['toolSha256']=binding['active_inspector_sha256']
    data['helperSha256']['replay-interaction-events.mjs']=binding['active_events_sha256']
    (directory/'trace.json').write_text(json.dumps(data,separators=(',',':'))+'\n',encoding='utf-8')
    return {'arm':item['arm'],'subject':item['subject'],'trace':str((directory/'trace.json').relative_to(ROOT)),
            'process':record,'source_binding':binding}


def observe(run):
    packed=json.loads((run/'pair-rotation.json').read_text(encoding='utf-8'))
    assert packed['passed']
    dest=run/'native';dest.mkdir(exist_ok=False)
    observer,binding=prepare_source(dest)
    source_runner=ROOT/'bots/main/foundation/tools/economy/pair_rotation.py'
    assert sha(source_runner)==packed['source_sha256'][source_runner.relative_to(ROOT).as_posix()]
    (dest/'source/pair_rotation-at-run.py').write_bytes(source_runner.read_bytes())
    decoder=ROOT/'bots/main/foundation/tools/economy/decode_legacy.mjs'
    geometry=dest/'geometry.json'
    p=subprocess.run([str(NODE),'--no-warnings',str(decoder),'--dir',str(ROOT/'.cache/contenders/foundation/pair-rotation'),
                      '--out',str(geometry)],capture_output=True,text=True)
    if p.returncode:raise RuntimeError(p.stderr)
    with concurrent.futures.ThreadPoolExecutor() as pool:
        results=list(pool.map(lambda item:observe_one(item,dest,observer,binding,geometry),packed['games']))
    assert sha(Path(binding['active_inspector']))==binding['active_inspector_sha256']
    assert sha(Path(binding['active_events']))==binding['active_events_sha256']
    output={'purpose':'saved native pair observation with priority-preserving projection',
            'source_projection':binding,'games':results,'new_games':0,
            'geometry_sha256':sha(geometry),'observer_source_sha256':sha(Path(__file__))}
    (dest/'observation.json').write_text(json.dumps(output,indent=1)+'\n',encoding='utf-8')
    print(json.dumps({'observed_games':len(results),'new_games':0,'projection':'native priority retained'}))


def verify(run):
    observations=json.loads((run/'native/observation.json').read_text(encoding='utf-8'))
    packed=json.loads((run/'pair-rotation.json').read_text(encoding='utf-8'))
    original_runner=run/'native/source/pair_rotation-at-run.py'
    assert sha(original_runner)==packed['source_sha256']['bots/main/foundation/tools/economy/pair_rotation.py']
    binding=observations['source_projection']
    for name in('base_inspector','base_events','active_inspector','active_events'):
        assert sha(Path(binding[name]))==binding[name+'_sha256'],name
    assert observations['new_games']==0
    checked=[]
    matched={}
    for item in observations['games']:
        label=item['arm']+'-'+item['subject'];on=item['arm']=='on'
        doc=json.loads((ROOT/item['trace']).read_text(encoding='utf-8'))
        trace=doc['interactions']
        process=item['process']
        assert process['returncode']==0 and not process['timed_out']
        assert process['tree_exit_confirmed'] and process['containment']['empty_confirmed']
        assert not process['forced_termination'] and not process['containment']['forced_termination']
        coverage=trace['coverage']
        assert coverage['complete'] and not coverage['truncated'] and coverage['requestedEndReached']
        assert coverage['queryObservationComplete'] and not coverage['queryFailures']
        assert coverage['requested']=={'fromRound':29,'toRound':76}
        assert doc['engineVersion']=='1.0.3' and doc['replayFormatVersion']=='0.0.3'
        assert doc['toolSha256']==binding['active_inspector_sha256']
        assert doc['helperSha256']['replay-interaction-events.mjs']==binding['active_events_sha256']
        subject=0 if item['subject']=='A' else 1
        states=trace['states'];current={x['id']:states[x['state']] for x in trace['initial']['champions']}
        pair=sorted(x['id'] for x in current.values() if x['team']==subject and x['type'] in(0,7))
        # Probe drafts Ryth (type0) then Marcella (type7); do not assume arbitrary future draft IDs.
        assert len(pair)==2
        leader=next(x for x in pair if current[x]['type']==0);helper=next(x for x in pair if x!=leader)
        q=[12,10] if subject==0 else [37,19]
        camp=next(x for x in trace['initial']['camps'] if [x['x'],x['y']]==q)
        cid=camp['id'];hp=camp['health'];assert hp==2400
        for x in pair:
            s=current[x]
            assert s['health']==s['maxHealth']==1200 and s['strength']==45 and s['range']==1
            assert s['isMelee'] and not s['items'] and not s['buffs'] and s['shield']==0
        initial={str(x):current[x] for x in pair}
        assert all(not s['items'] and not s['buffs'] and s['shield']==0 for s in states if s['id'] in pair)
        priorities={};priority_events={};basic=Counter();volleys=[];basic_order=defaultdict(list)
        clear=None;clear_health=None;deaths=[];captured=[];bank=[];priority_count=0;helper_release=[]
        first_basics={}
        for e in trace['events']:
            r=e['round'];na=e['native'].get('actor');aid=na['id'] if na and na['kind']==1 else None
            # Retained boundaries can omit turn resets, so take each queried before state directly.
            for change in e['championStates']:
                if change['before'] is not None:current[change['id']]=states[change['before']]
            if e['kind']=='set-priority' and aid in pair:
                before,after=e['actorState'];assert before==after
                value=e['native']['priority'];assert value in(0,100)
                expected=100 if aid==leader else 0
                if on:
                    own=current[aid];other=current[helper if aid==leader else leader]
                    if other['alive']:
                        expected=100 if own['health']>other['health'] or (own['health']==other['health'] and aid==leader) else 0
                assert value==expected,(label,r,aid,value,expected)
                priorities[r,aid]=value;priority_events[r,aid]=e['id'];priority_count+=1
            if e['kind']=='attack' and aid in pair:
                assert e['action']['recordedTo']==q
                assert current[aid]['availability']['attack']
                deltas=[x for x in e['observed'] if x['entity']=='camp:'+str(cid)]
                assert len(deltas)==1
                delta=deltas[0];assert delta['before']['health']==hp
                assert delta['after']['health']==max(0,hp-45)
                hp=delta['after']['health'];basic[aid]+=1;basic_order[r].append(aid)
                first_basics.setdefault(aid,{'event':e['id'],'state':current[aid]})
                near=e['nearby']['before']['champions']
                for member in pair:
                    if current[member]['alive']:
                        assert max(abs(current[member]['x']-q[0]),abs(current[member]['y']-q[1]))==1
                        assert any(x['entity']=='champion:'+str(member) and x['personallyVisibleToActor'] for x in near)
                if hp==0:
                    clear=r;clear_health={str(x):current[x]['health'] for x in pair}
            if e['kind']=='attack' and na and na['kind']==2 and na['id']==cid:
                assert e['phase']=='round-start' and r>=31
                wounded=[x for x in e['observed'] if x['entity'].startswith('champion:') and
                         'health' in x.get('after',{}) and x['after']['health']<x.get('before',{}).get('health',-1)]
                assert len(wounded)==1
                wound=wounded[0];victim=int(wound['entity'].split(':')[1]);assert victim in pair
                assert wound['before']['health']-wound['after']['health']==60
                alive=[x for x in pair if current[x]['alive']]
                for member in alive:
                    assert max(abs(current[member]['x']-q[0]),abs(current[member]['y']-q[1]))==1
                assert e['action']['recordedFrom']==q
                assert e['action']['recordedTo']==[current[victim]['x'],current[victim]['y']]
                expected=max(alive,key=lambda x:(priorities[r-1,x],-x))
                assert victim==expected,(label,r,victim,expected)
                volleys.append({'round':r,'event':e['id'],'recipient':victim,
                    'beforeHP':wound['before']['health'],'afterHP':wound['after']['health'],
                    'previousRoundPriorities':{str(x):priorities[r-1,x] for x in alive},
                    'settingEvents':{str(x):priority_events[r-1,x] for x in alive},
                    'positions':{str(x):[current[x]['x'],current[x]['y']] for x in alive}})
                if wound['after']['health']==0:deaths.append({'actor':victim,'round':r,'event':e['id']})
            if e['kind']=='capture':captured.append(e)
            if e['kind']=='move' and aid==helper and clear is not None and r>=clear:
                helper_release.append({'round':r,'event':e['id'],'from':e['action']['recordedFrom'],'to':e['action']['recordedTo']})
            if e['kind']=='gems-submitted':bank.append(e)
            if aid in pair:assert e['kind'] not in('ability','cast','recall','buy','use-item')
            for change in e['championStates']:
                if change['after'] is not None:current[change['id']]=states[change['after']]
        assert clear==(56 if on else 63)
        assert basic==Counter({leader:27,helper:27} if on else {leader:20,helper:34})
        assert clear_health=={str(leader):420 if on else 0,str(helper):420}
        assert len(volleys)==(26 if on else 33)
        assert Counter(x['recipient'] for x in volleys)==Counter({leader:13,helper:13} if on else {leader:20,helper:13})
        assert deaths==([] if on else [{'actor':leader,'round':50,'event':deaths[0]['event']}])
        for r,order in basic_order.items():
            expected=sorted(order,key=lambda x:(-priorities[r-1,x],x))
            assert order==expected,(label,r,order,expected)
        if on:
            assert basic_order[30]==[leader,helper]
            assert all(basic_order[r]==([leader,helper] if r%2==1 else [helper,leader]) for r in range(31,57))
        assert len(captured)==len(bank)==1 and len(doc['captures'])==1
        capturer=leader if on else helper
        ce=captured[0];be=bank[0]
        assert ce['round']==(62 if on else 69) and be['round']==(68 if on else 75)
        assert doc['captures'][0]['recipientDeltas']==[{'actor':capturer,'team':subject,'gold':90,'gems':15}]
        if on:
            assert helper_release and helper_release[0]['round']==57
            assert helper_release[0]['to']!=q
        assert be['actor']==capturer
        assert any(x['entity']=='score:'+str(subject) and x['before']['value']==0 and x['after']['value']==15 for x in be['observed'])
        assert any(x['entity']=='champion:'+str(capturer) and x['before'].get('gems')==15 and x['after'].get('gems')==0 for x in be['observed'])
        game=next(x for x in packed['games'] if x['arm']==item['arm'] and x['subject']==item['subject'])
        health=game['row'][item['subject']]
        assert all(health[k]==0 for k in('faults','exhausted','errors','fuel_at_budget'))
        with zipfile.ZipFile(ROOT/game['replay']) as archive:
            raw_fuel=archive.read('team-'+item['subject'].lower()+'.fuel')
        values=struct.unpack_from(f'<{(len(raw_fuel)-13)//8}Q',raw_fuel,13)
        budget=values[3 if subject==0 else 7];ordinary=values[14:]
        assert values[3]==values[7]==100000 and max(ordinary)<budget
        assert max(ordinary)==health['fuel_max']==game['ordinary_peak']
        matched[item['arm'],item['subject']]={str(x):first_basics[x]['state'] for x in pair}
        checked.append({'arm':item['arm'],'subject':item['subject'],'trace':item['trace'],
            'trace_sha256':sha(ROOT/item['trace']),'replay_sha256':doc['replaySha256'],
            'coverage':coverage,'initialPair':initial,'nativeCamp':camp,'basics':dict(basic),
            'firstBasicEvents':{str(x):first_basics[x]['event'] for x in pair},'basicActorOrder':dict(basic_order),
            'priorityCallsWithoutAllowanceChange':priority_count,'volleys':volleys,'deaths':deaths,
            'helperReleaseMoves':helper_release,
            'clearRound':clear,'clearHP':clear_health,'captureRound':ce['round'],'capturer':capturer,
            'captureEvent':ce['id'],'captureRecipients':doc['captures'][0]['recipientDeltas'],
            'bankRound':be['round'],'bankEvent':be['id'],'submittedGems':15,
            'ordinaryFuelPeak':health['fuel_max'],'health':health,'ownedProcessTreeEmpty':True})
        checked[-1]['nativeFuel']={'headerOrdinaryA':values[3],'headerOrdinaryB':values[7],
            'selectedBudgetIndex':3 if subject==0 else 7,'ordinaryBudget':budget,
            'ordinaryPeak':max(ordinary),'ordinaryAtBudget':sum(v>=budget for v in ordinary)}
    # Match complete queried actor info, not omniscient/private fuel or arbitrary replay bytes.
    for side in('A','B'):assert matched['off',side]==matched['on',side],side
    output={'purpose':'independent current-engine native priority, volley, basic, capture and bank verification',
        'passed':True,'engine':'1.0.3','cli_sdk':'3.0.5','new_games':0,'saved_games':4,
        'game_round_limit':90,'native_window':[29,76],'matchedFullQueriedActorStates':4,
        'sourceProjection':binding,'observer_record_sha256':sha(run/'native/observation.json'),
        'probe_record_sha256':sha(run/'pair-rotation.json'),'verifier_sha256':sha(Path(__file__)),
        'sourceRunnerAtRun':{'path':str(original_runner.relative_to(ROOT)),'sha256':sha(original_runner)},
        'budgetParserCorrection':'Original runner used header index3 for B too; both actual headers100000 in every saved game. Corrected future runner and independently rechecked saved native fuel by selected side; old runner body preserved.',
        'verificationRepairs':['First pure verification pass assumed Marcella enum1; queried enum is7.',
        'Second pass assumed rotation already changed round31 order; previous-round priority correctly leaves round31 leader first, then alternates.',
        'Extra release assertion initially assumed helper could leave on clear56; it had already acted, so actual helper release is its next own turn57. Corrected assertion and documentation; no native rerun.'],
        'games':checked,'limits':['one symmetric custom open map, one single elk and Ryth/Marcella both sides',
        'no items, abilities, shields, healing or enemy interference; both adjacent melee from first volley',
        'priority recipient identity inferred from unique occupant coordinate plus queried HP delta',
        'next-round priority only; no same-round rescue, unequal distances, other camps or general pooled-HP guarantee',
        'bounded healthy cycle, not full-game health, normal planner integration, opportunity profit or strength']}
    (run/'native-verification.json').write_text(json.dumps(output,indent=1)+'\n',encoding='utf-8')
    print(json.dumps({'passed':True,'saved_games':4,'matched_actor_states':4,
        'results':[{k:g[k] for k in('arm','subject','clearRound','clearHP','captureRound','bankRound','ordinaryFuelPeak')} for g in checked]}))


if __name__=='__main__':
    directory=Path(sys.argv[1]).resolve()
    observe(directory) if '--observe' in sys.argv else verify(directory)
