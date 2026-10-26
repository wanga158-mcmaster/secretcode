"""Independently observe/verify Warband's saved conditional r3 caller, without games."""
from __future__ import annotations
from collections import Counter,defaultdict
import concurrent.futures
import json
from pathlib import Path
import struct
import subprocess
import sys
import zipfile
from cofarm import ROOT,NODE,sha,observe_saved

OUT=ROOT/'results/contenders/foundation/party-r3-verification'
OWNER=ROOT/'results/contenders/warband/party-probe/r3/probe.json'


def observe():
    OUT.mkdir(parents=True,exist_ok=False)
    packed=json.loads(OWNER.read_text(encoding='utf-8'))
    assert packed['passed'] and packed['rounds']==80
    mp=ROOT/'.cache/contenders/warband/party-probe/maps/fresh-elk-party.map26a'
    assert sha(mp)==packed['map_sha256']
    geometry=OUT/'geometry.json'
    p=subprocess.run([str(NODE),'--no-warnings',str(ROOT/'bots/main/foundation/tools/economy/decode_legacy.mjs'),
        '--dir',str(mp.parent),'--out',str(geometry)],capture_output=True,text=True)
    assert p.returncode==0,p.stderr
    def one(game):
        dest=OUT/(game['arm']+'-'+game['subject'])
        result=observe_saved(ROOT/Path(game['replay']),dest,29,67,'fresh-elk-party',geometry)
        return {**result,'arm':game['arm'],'subject':game['subject']}
    with concurrent.futures.ThreadPoolExecutor() as pool:observed=list(pool.map(one,packed['games']))
    record={'purpose':'Independent saved conditional actual party caller; no autonomous-selection claim',
        'new_games':0,'games':observed,'owner_record':str(OWNER.relative_to(ROOT)),
        'owner_record_sha256':sha(OWNER),'geometry_sha256':sha(geometry),'script_sha256':sha(Path(__file__))}
    (OUT/'observation.json').write_text(json.dumps(record,indent=1)+'\n',encoding='utf-8')
    print(json.dumps({'observed_games':4,'new_games':0,'window':[29,67]}))


def verify():
    observation=json.loads((OUT/'observation.json').read_text(encoding='utf-8'))
    packed=json.loads(OWNER.read_text(encoding='utf-8'));assert sha(OWNER)==observation['owner_record_sha256']
    games=[];matched={};matched_joint={}
    for row in observation['games']:
        doc=json.loads((ROOT/row['trace']).read_text(encoding='utf-8'))
        t=doc['interactions'];side=0 if row['subject']=='A' else 1;on=row['arm']=='on'
        assert t['coverage']['complete'] and not t['coverage']['queryFailures'] and t['coverage']['omittedByFilter']==0
        process=row['process'];assert process['returncode']==0 and process['tree_exit_confirmed'] and process['containment']['empty_confirmed']
        states=t['states'];cur={x['id']:states[x['state']] for x in t['initial']['champions']}
        leader=next(x for x in cur if cur[x]['team']==side and cur[x]['type']==0)
        helper=next(x for x in cur if cur[x]['team']==side and cur[x]['type']==7);pair=[leader,helper]
        q=[10,10] if side==0 else [39,39]
        camp=next(c for c in t['initial']['camps']if[c['x'],c['y']]==q);cid=camp['id'];hp=camp['health'];assert hp==2400
        basics=Counter();volleys=[];priorities=[];pre={};clear=None;clear_hp=None;moves=[];recalls=[];capture=[]
        basic_rounds=defaultdict(list);first_states={};hp_changes=[];death=[];joint30=None
        for e in t['events']:
            for ch in e['championStates']:
                if ch['before'] is not None:cur[ch['id']]=states[ch['before']]
            r=e['round'];a=e['actor'];na=e['native'].get('actor')
            if r==30 and a in pair and e['phase']=='action' and a not in pre:
                s=dict(cur[a]);s['personalVision']=t['visions'][s['personalVision']] if s['personalVision'] is not None else None
                s['abilityDefinition']=t['abilityDefinitions'][s['abilityDefinition']] if s['abilityDefinition'] is not None else None
                pre[a]={'state':s,'event':e['id'],'nativeKind':e['kind']}
                assert s['health']==s['maxHealth']==1200 and s['strength']==45 and s['range']==1
                assert s['channel']['kind']=='none' and not s['stunned'] and s['restrictions']==0
                assert all(s['availability'][x] for x in('move','attack','cast')) and not s['availability']['completedTurn']
                assert s['gems']==s['gold']==0 and not s['items'] and not s['buffs']
                if a==helper:
                    joint30={'event':e['id'],'nativeKind':e['kind'],'input':e['native']['input'],'substep':e['native']['substep'],'pair':{}}
                    for member in pair:
                        value=dict(cur[member])
                        value['personalVision']=t['visions'][value['personalVision']] if value['personalVision']is not None else None
                        value['abilityDefinition']=t['abilityDefinitions'][value['abilityDefinition']] if value['abilityDefinition']is not None else None
                        joint30['pair'][str(member)]=value
            if e['kind']=='set-priority' and a in pair:
                before,after=(states[k] for k in e['actorState'])
                assert before['availability']==after['availability']
                assert before['priorityCurrent']==after['priorityCurrent']
                assert after['priorityNext']==e['native']['priority']
                priorities.append({'round':r,'actor':a,'value':e['native']['priority'],'event':e['id'],
                    'current':before['priorityCurrent'],'nextBefore':before['priorityNext']})
            if e['kind']=='attack' and a in pair:
                assert on and e['action']['recordedTo']==q and cur[a]['availability']['attack']
                assert cur[a]['isMelee'] and cur[a]['strength']==45 and not cur[a]['items']
                delta=next(x for x in e['observed']if x['entity']=='camp:'+str(cid))
                assert delta['before']['health']==hp and delta['after']['health']==max(0,hp-45)
                hp=delta['after']['health'];basics[a]+=1;basic_rounds[r].append(a)
                first_states.setdefault(a,{'state':dict(cur[a]),'event':e['id']})
                expected=sorted(basic_rounds[r],key=lambda x:(-cur[x]['priorityCurrent'],x))
                assert basic_rounds[r]==expected
                if hp==0:clear=r;clear_hp={str(x):cur[x]['health'] for x in pair}
            if e['kind']=='attack' and na and na['kind']==2 and na['id']==cid:
                assert on and e['phase']=='round-start'
                wound=[x for x in e['observed']if x['entity'].startswith('champion:')and'health'in x.get('after',{})]
                assert len(wound)==1;wound=wound[0];victim=int(wound['entity'].split(':')[1]);assert victim in pair
                assert wound['before']['health']-wound['after']['health']==60
                assert all(max(abs(cur[x]['x']-q[0]),abs(cur[x]['y']-q[1]))==1 for x in pair)
                assert all(cur[x]['alive'] and not cur[x]['items'] and not cur[x]['shield'] for x in pair)
                expected=max(pair,key=lambda x:(cur[x]['priorityCurrent'],-x));assert victim==expected
                volleys.append({'round':r,'event':e['id'],'recipient':victim,
                    'beforeHP':wound['before']['health'],'afterHP':wound['after']['health'],
                    'queriedCurrentPriorities':{str(x):cur[x]['priorityCurrent'] for x in pair},
                    'queriedNextPriorities':{str(x):cur[x]['priorityNext'] for x in pair},
                    'positions':{str(x):[cur[x]['x'],cur[x]['y']] for x in pair}})
            if a in pair and e['kind']=='move':moves.append({'round':r,'actor':a,'from':e['action']['recordedFrom'],'to':e['action']['recordedTo'],'event':e['id']})
            if a in pair and e['kind']=='recall':recalls.append({'round':r,'actor':a,'event':e['id']})
            if e['kind']=='capture':capture.append(e)
            if a in pair:assert e['native']['kind']not in(6,13,15,20,21,25)
            for ch in e['championStates']:
                if ch['after'] is not None:
                    s=states[ch['after']]
                    if ch['id'] in pair and s['health']<cur[ch['id']]['health']:
                        hp_changes.append(e['id'])
                    if ch['id'] in pair and not s['alive']:death.append(e['id'])
                    cur[ch['id']]=s
        assert len(pre)==2 and not death
        matched[row['arm'],row['subject']]={str(x):pre[x]['state'] for x in pair}
        assert joint30 is not None;matched_joint[row['arm'],row['subject']]=joint30['pair']
        if on:
            assert basics==Counter({leader:27,helper:27}) and clear==58 and clear_hp=={str(leader):420,str(helper):420}
            assert len(volleys)==26 and Counter(x['recipient']for x in volleys)==Counter({leader:13,helper:13})
            assert [x['round']for x in volleys]==list(range(33,59))
            assert set(basic_rounds)==set(range(32,59)) and all(len(v)==2 for v in basic_rounds.values())
            assert all(x in(leader,helper) for x in basics)
            assert hp_changes==[x['event']for x in volleys]
            assert len(capture)==len(doc['captures'])==1 and capture[0]['round']==65
            assert doc['captures'][0]['recipientDeltas']==[{'actor':leader,'team':side,'gold':90,'gems':15}]
            assert any(x['round']==59 and x['actor']==leader and x['to']==q for x in moves)
            assert any(x['round']==59 and x['actor']==helper and max(abs(x['to'][0]-q[0]),abs(x['to'][1]-q[1]))==2 for x in moves)
            assert any(x['round']==60 and x['actor']==helper for x in recalls)
            assert all(cur[x]['priorityNext']==0 for x in pair)
        else:
            assert not basics and not volleys and not capture and hp==2400 and not hp_changes
        game=next(g for g in packed['games']if g['arm']==row['arm']and g['subject']==row['subject'])
        with zipfile.ZipFile(ROOT/Path(game['replay']))as archive:
            fuel=archive.read('team-'+row['subject'].lower()+'.fuel')
        values=struct.unpack_from(f'<{(len(fuel)-13)//8}Q',fuel,13);budget=values[3 if side==0 else 7]
        assert max(values[14:])<budget and not game['at_budget']
        assert all(game['row'][row['subject']][k]==0 for k in('errors','exhausted','faults','fuel_at_budget'))
        games.append({'arm':row['arm'],'subject':row['subject'],'trace':row['trace'],'trace_sha256':sha(ROOT/row['trace']),
            'round30Prerequisites':{str(k):v for k,v in pre.items()},'firstDifferingActionBoundary':joint30,
            'basics':dict(basics),'firstBasicStates':first_states,
            'basicActorOrder':dict(basic_rounds),'volleys':volleys,'priorityActions':priorities,
            'clearRound':clear,'clearHP':clear_hp,'moves':moves,'recalls':recalls,
            'capture':doc['captures'],'nativeFuel':{'budget':budget,'peak':max(values[14:]),'budgetHeaderIndex':3 if side==0 else 7},
            'coverage':t['coverage'],'ownedProcessTreeEmpty':True})
    for side in('A','B'):
        assert matched['off',side]==matched['on',side]
        assert matched_joint['off',side]==matched_joint['on',side]
    result={'passed':True,'purpose':'Independent actual conditional party executor contribution; not autonomous planner selection',
        'engine':'1.0.3','cli_sdk':'3.0.5','saved_games':4,'new_games':0,'native_window':[29,67],'game_round_limit':80,
        'matchedFullQueriedRound30PrerequisiteActorStates':4,'owner_record':str(OWNER.relative_to(ROOT)),
        'matchedFullQueriedPairStatesBeforeFirstDifferingNativeAction':2,
        'prerequisiteTiming':'Before each actor first native gameplay action at round30; protocol shared-memory writes are not decoded. The first differing native action is helper move versus end, after an identical leader end.',
        'owner_record_sha256':sha(OWNER),'observation_sha256':sha(OUT/'observation.json'),
        'verifier_sha256':sha(Path(__file__)),'games':games,
        'limits':['offer/accept shared-memory protocol and model score values are owner guest/source evidence, not independently decoded native memory',
        'fixture stipulates named target/alternative selection and clear prior knowledge; normal autonomous selection unproven',
        'no bank in nativewindow, no full-game release qualification, strength or global opportunity-profit claim',
        'r2 failures preserved by owner; r3 setup omits earlier support-cast health contamination']}
    (OUT/'verification.json').write_text(json.dumps(result,indent=1)+'\n',encoding='utf-8')
    print(json.dumps({'passed':True,'saved_games':4,'matched_round30_actor_states':4,
        'outcomes':[{k:x[k]for k in('arm','subject','clearRound','clearHP','basics')}for x in games]}))


if __name__=='__main__':observe() if '--observe' in sys.argv else verify()
