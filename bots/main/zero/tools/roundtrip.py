"""Bounded exact-engine replay action round-trip canary (no whole games).

  arena's Python -B bots/main/zero/tools/roundtrip.py --replay REPLAY.zip --decisions 100

Loads one saved current-engine replay, branches before recorded inputs,
reissues reconstructed action arguments through Game_Action_*, and compares both
official spectator snapshots with the recorded successor. Only the documented
timeline identity at bytes [48,56) is masked (compare-replay-prefix.mjs).

This establishes the replay-fork/action ABI, NOT production zero observation,
candidate, SDK execution, WASM fuel or learned-policy correctness. To teacher
force zero, run sdk::observe + core candidates + sdk::act_* in a bot instance
bound to this branch through the existing fastrun bridge, then compare here.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
from pathlib import Path
import sys
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / '.cache/main/zero/roundtrip'
sys.path.insert(0, str(ROOT / 'tools/arena'))
import compiled_runner as shared
import core
import exp

JS = r"""
import fs from 'node:fs';
import crypto from 'node:crypto';
import {WASI} from 'node:wasi';
const [specPath] = process.argv.slice(2), spec = JSON.parse(fs.readFileSync(specPath,'utf8'));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const engine=fs.readFileSync(spec.engine), payload=fs.readFileSync(spec.payload);
if(hash(engine)!==spec.engineSha256 || hash(payload)!==spec.payloadSha256)throw Error('input changed');
const wasi = new WASI({version:'preview1',args:[],env:{},preopens:{}});
const instance = await WebAssembly.instantiate(await WebAssembly.compile(engine),{wasi_snapshot_preview1:wasi.wasiImport});
wasi.initialize(instance);
const e=instance.exports, view=()=>new DataView(e.memory.buffer);
const string=p=>{const b=new Uint8Array(e.memory.buffer);let q=p;while(q<b.length&&b[q])q++;return new TextDecoder().decode(b.subarray(p,q));};
const check=(name,...args)=>{const code=e[name](...args);if(code)throw Error(`${name}: ${code}: ${string(e.GetLastErrorMsg())}`);};
for(const [name,n] of Object.entries({Game_FromReplayFileBytes:3,Game_Visualiser_Timeline_Branch:4,
 Game_Visualiser_Timeline_Seek:4,Game_Visualiser_Timeline_ReplayNext:2,Game_Action_Move:4,
 Game_Action_Attack:2,Game_Action_Cast:5,Game_Action_BuyItem:3,Game_Action_BuyConsumable:2,Game_Action_Recall:1,
 Game_Action_CancelChannel:1,Game_Action_SubmitGems:2,
 Game_Visualiser_ReadState:3,Game_Visualiser_ReadTimelineState:2,Game_Runner_GetState:2}))
 if(typeof e[name]!=='function'||e[name].length!==n)throw Error(`ABI changed: ${name}`);
if(e.Game_Visualiser_TimelineSnapshotSize()!==40152||e.Game_Visualiser_SnapshotSize()!==184760)throw Error('snapshot ABI changed');
const pointers=[], alloc=n=>{const p=e.Engine_Malloc(n);if(!p)throw Error('allocation failed');pointers.push(p);return p;};
const input=alloc(payload.length),out=alloc(256),timelineOut=alloc(40152),stateOut=alloc(184760),perspective=alloc(8),entity=alloc(8),position=alloc(8),actorPerspective=alloc(8);
new Uint8Array(e.memory.buffer,input,payload.length).set(payload);
view().setUint32(perspective,2,true);view().setUint32(perspective+4,0xffffffff,true);
let game;
const mask=b=>{b.fill(0,48,56);return b;};
const moment=b=>{const d=new DataView(b.buffer,b.byteOffset,b.byteLength),u=n=>d.getUint32(n,true),i=n=>d.getInt32(n,true);
 return {round:u(8),input:u(56),actor:u(60),phase:u(24),stepPhase:u(64),kind:u(68),from:[i(80),i(84)],to:[i(88),i(92)],substep:u(104),slot:u(108),abilityType:u(112),awaitingInput:u(20)===1};};
const capture=()=>{check('Game_Visualiser_ReadTimelineState',game,timelineOut);check('Game_Visualiser_ReadState',game,perspective,stateOut);
 const timeline=Buffer.from(new Uint8Array(e.memory.buffer,timelineOut,40152)), state=Buffer.from(new Uint8Array(e.memory.buffer,stateOut,184760));
 return {moment:moment(timeline),timeline:mask(timeline),state:mask(state)};};
const difference=(a,b)=>{for(const layout of ['timeline','state'])if(!a[layout].equals(b[layout])){
 let count=0,offsets=[];for(let i=0;i<a[layout].length;i++)if(a[layout][i]!==b[layout][i]){count++;if(offsets.length<16)offsets.push(i);}
 return {layout,differingBytes:count,offsets};}return null;};
const kindNames={4:'move',5:'attack',6:'cast',8:'end-turn',11:'recall',12:'tribute',13:'shop',16:'cancel-channel',23:'movement-blocked',26:'turn-rollover'};
const checks=[],discovered=Object.fromEntries(Object.values(kindNames).map(k=>[k,[]]));let steps=0,previous,stride;
const dview=b=>new DataView(b.buffer,b.byteOffset,b.byteLength);
const champions=frame=>{const d=dview(frame.timeline),arr=[];for(let i=0;i<d.getUint32(172,true);i++){const p=196+i*252;arr.push({id:d.getUint32(p+4,true),team:d.getUint32(p+8,true),type:d.getUint32(p+12,true),x:d.getInt32(p+16,true),y:d.getInt32(p+20,true),hp:d.getFloat32(p+24,true),gold:d.getUint32(p+36,true),gems:d.getUint32(p+40,true),items:Array.from({length:42},(_,j)=>d.getUint32(p+48+4*j,true)),consumables:Array.from({length:4},(_,j)=>d.getUint32(p+216+4*j,true))});}return arr;};
const actor=frame=>champions(frame).find(c=>c.id===frame.moment.actor);
const unitTargets=(before,m)=>{const d=dview(before.timeline),result=[];for(const c of champions(before))if(c.hp>0&&c.x===m.to[0]&&c.y===m.to[1])result.push([1,c.id]);
 for(let i=0;i<d.getUint32(176,true);i++){const p=4228+i*28;if(d.getFloat32(p+24,true)>0&&d.getInt32(p+16,true)===m.to[0]&&d.getInt32(p+20,true)===m.to[1])result.push([2,d.getUint32(p+4,true)]);}return result;};
const exactActor=frame=>{const c=actor(frame),d=view();d.setUint32(actorPerspective,c.team,true);d.setUint32(actorPerspective+4,c.id,true);
 const unit=n=>{check('Game_API_Champion_Get',game,actorPerspective,n.id,out);const v=view(),state=v.getUint32(out+40,true),hp=v.getFloat32(out+16,true);
  if(hp!==n.hp||v.getInt32(out+8,true)!==n.x||v.getInt32(out+12,true)!==n.y)throw Error('actor-perspective info differs from same timeline');
  const strength=v.getFloat32(out+24,true),focus=v.getFloat32(out+28,true),maxHp=v.getFloat32(out+20,true);
  const cooldown=[0,1].map(slot=>{check('Game_API_Champion_GetCooldown',game,actorPerspective,n.id,slot,out);return view().getInt32(out,true);});
  const channel=state&4096?'teleport':state&4?'tribute':state&2?'recall':'none';let channelRemaining=null;
  if(channel!=='none'){check('Game_API_Champion_GetChannelTime',game,actorPerspective,n.id,out);channelRemaining=view().getInt32(out,true);}
  return {id:n.id,team:n.team,type:n.type,at:[n.x,n.y],hp,maxHp,strength,focus,gems:n.gems,
   ...(n.id===c.id?{gold:n.gold,items:n.items,consumables:n.consumables}:{}),
   stateBits:state,recalling:!!(state&2),tributing:!!(state&4),channel,channelRemaining,cooldown,
   allowances:{move:!!(state&8),attack:!!(state&16),cast:!!(state&2048)}};};
 const self=unit(c);
 check('Game_Visualiser_GetVisionBuffer',game,actorPerspective,out);const vision=view().getUint32(out,true);
 const seen=(x,y)=>{if(x<0||y<0)return false;const chunk=(y>>3)*stride+(x>>3),bit=((y&7)<<3)+(x&7);return !!(view().getBigUint64(vision+chunk*8,true)&(1n<<BigInt(bit)));};
 const visible=champions(frame).filter(n=>n.hp>0&&n.id!==c.id&&seen(n.x,n.y));
 // Copy visibility-derived membership before later API calls may grow memory.
 const camps=[],dv=dview(frame.timeline);for(let i=0;i<dv.getUint32(176,true);i++){const p=4228+i*28,x=dv.getInt32(p+16,true),y=dv.getInt32(p+20,true),hp=dv.getFloat32(p+24,true);if(hp>0&&seen(x,y))camps.push({id:dv.getUint32(p+4,true),type:dv.getUint32(p+12,true),at:[x,y],hp});}
 for(const camp of camps){check('Game_API_Camp_GetState',game,actorPerspective,camp.id,out);camp.stateBits=view().getUint32(out,true);}
 const championsVisible=visible.map(unit);
 const banked=[0,1].map(team=>{check('Game_API_GetSubmittedGemCount',game,team,out);return view().getUint32(out,true);});
 return {moment:frame.moment,self,visibleChampions:championsVisible,visibleCamps:camps,banked};};
const alternatives=(before,expected)=>{const m=expected.moment;
 if(m.kind===4)return [{name:'Game_Action_Move',args:[...m.to,'out'],successOut:true}];
 if(m.kind===8)return [{name:'Game_Runner_EndTurn',args:[0]}];
 if(m.kind===26)return [0,1,2].map(reason=>({name:'Game_Runner_EndTurn',args:[reason]}));
 if(m.kind===23)return [{name:'Game_Action_Move',args:[...m.to,'out'],successOut:true,expectedAcceptance:0},
  ...[0,1].map(slot=>({name:'Game_Action_Cast',args:[slot,'entity','position','out'],target:[0,0],at:m.to,successOut:true,expectedAcceptance:0})),
  {name:'Game_Action_PlaceWard',args:['position','out'],at:m.to,successOut:true,expectedAcceptance:0},
  ...[0,2].map(rawId=>({name:'Game_Action_UseConsumable',args:[rawId,'position','out'],at:m.to,successOut:true,expectedAcceptance:0})),
  ...[0,1,2].map(reason=>({name:'Game_Runner_EndTurn',args:[reason]})),{name:'Game_Runner_Step',args:[]},{name:'Game_Runner_Advance',args:[]}];
 if(m.kind===5)return unitTargets(before,m).map(t=>({name:'Game_Action_Attack',args:['entity'],target:t}));
 if(m.kind===11)return [{name:'Game_Action_Recall',args:[]}];
 if(m.kind===12)return [{name:'Game_Action_SubmitGems',args:['position'],at:m.to}];
 if(m.kind===16)return [{name:'Game_Action_CancelChannel',args:[]}];
 if(m.kind===13){const a=actor(before),b=actor(expected),result=[];for(const it of spec.items){let bought=b.items[it.id]-a.items[it.id];if(it.result<42)bought+=(b.items[it.result]-a.items[it.result])*it.combine_count;if(bought>0)result.push({name:'Game_Action_BuyItem',args:[it.id,bought]});}for(let i=0;i<4;i++)if(b.consumables[i]>a.consumables[i])result.push({name:'Game_Action_BuyConsumable',args:[i]});return result;}
 if(m.kind===6){const base={name:'Game_Action_Cast',successOut:true},arr=[];
  // The replay exposes endpoints, not the original typed target. Try the legal
  // SDK forms and require an exact successor, recording the chosen inverse.
  for(const t of unitTargets(before,m))arr.push({...base,args:[m.slot,'entity','position','out'],target:t,at:[-1,-1]});
  arr.push({...base,args:[m.slot,'entity','position','out'],target:[0,0],at:m.to});
  arr.push({...base,args:[m.slot,'entity','position','out'],target:[0,0],at:[-1,-1]});return arr;}
 return [];};
try{
 check('Game_FromReplayFileBytes',input,payload.length,out);game=view().getBigUint64(out,true);
 check('Game_GetBitboardMetadata',game,out);stride=view().getUint32(out+16,true);
 check('Game_Visualiser_Timeline_GetMain',game,out);const main=view().getBigUint64(out,true);
 check('Game_Visualiser_Timeline_Seek',game,main,0,0);
 for(;steps<spec.maxSteps;steps++){
  const expected=capture(),m=expected.moment;
  if(m.phase!==12 && m.round>spec.maxRound)break;
  if(kindNames[m.kind] && m.substep===0 && previous?.moment.awaitingInput && m.input===previous.moment.input+1)
   discovered[kindNames[m.kind]].push({...m,prefixSubstep:previous.moment.substep});
  previous=expected;
  const code=e.Game_Visualiser_Timeline_ReplayNext(game,1);
  if(code)break;
 }
 const selected=[];
 for(let depth=0;selected.length<spec.decisions;depth++){let added=0;for(const a of Object.values(discovered))if(a[depth]&&selected.length<spec.decisions){selected.push(a[depth]);added++;}if(!added)break;}
 for(const include of spec.include){const [round,actor,kind]=include.split(':'),candidate=discovered[kind]?.find(m=>m.round===+round&&m.actor===+actor);if(!candidate)throw Error(`required sample missing: ${include}`);if(!selected.some(m=>m.input===candidate.input)){const replace=selected.findLastIndex(m=>kindNames[m.kind]===kind);selected[replace<0?selected.length-1:replace]=candidate;}}
 for(const m of selected){
   check('Game_Visualiser_Timeline_Seek',game,main,m.input-1,0);
   let ready=capture();
   for(let j=0;!ready.moment.awaitingInput&&j<200;j++){check('Game_Visualiser_Timeline_ReplayNext',game,1);ready=capture();}
   if(!ready.moment.awaitingInput||ready.moment.input!==m.input-1)throw Error(`ready seek differs ${JSON.stringify({m,ready:ready.moment})}`);
   const before=capture();
   if(!before.moment.awaitingInput||before.moment.input!==m.input-1)throw Error('not at ready prefix');
   const observation=exactActor(before);
   if(spec.observationsOnly){checks.push({round:m.round,actor:m.actor,input:m.input,kind:kindNames[m.kind],observation});continue;}
   check('Game_Visualiser_Timeline_ReplayNext',game,1);
   const expected=capture();
   if(expected.moment.input!==m.input||expected.moment.kind!==m.kind)throw Error('successor is not requested input');
   let success=null,attempts=0,lastFailure,failures=[],matchingAlternatives=[];
   for(const action of alternatives(before,expected)){
   // Branch(game, sourceTimeline, prefixInput, outTimelineHandle) seeks the new
   // timeline to the ready state before its next input. No Runner_Init needed.
   check('Game_Visualiser_Timeline_Seek',game,main,m.input-1,0);
   let positioned=capture();for(let j=0;!positioned.moment.awaitingInput&&j<200;j++){check('Game_Visualiser_Timeline_ReplayNext',game,1);positioned=capture();}
   check('Game_Visualiser_Timeline_Branch',game,main,m.input-1,out);
   const branchBefore=capture();
   const beforeDifference=difference(before,branchBefore);
   if(beforeDifference)throw Error(`branch prefix differs: ${JSON.stringify(beforeDifference)}`);
   check('Game_Runner_GetState',game,out);
   const state=[0,4,8,12].map(n=>view().getUint32(out+n,true));
   if(state[0]!==1||state[2]!==m.round||state[3]!==m.actor)throw Error('branch owner mismatch');
   if(action.target){view().setUint32(entity,action.target[0],true);view().setUint32(entity+4,action.target[1],true);}
   if(action.at){view().setInt32(position,action.at[0],true);view().setInt32(position+4,action.at[1],true);}
   const args=action.args.map(x=>x==='out'?out:x==='entity'?entity:x==='position'?position:x);
   const code=e[action.name](game,...args);attempts++;
   if(code){lastFailure={action,code,error:string(e.GetLastErrorMsg())};
    const rejectedDelta=difference(expected,capture());
    if(m.kind===23&&!rejectedDelta){matchingAlternatives.push({action,statusCode:code,accepted:0,returnedFalse:false});success??={round:m.round,actor:m.actor,input:m.input,kind:kindNames[m.kind],from:m.from,to:m.to,accepted:0,statusCode:code,returnedFalse:false,error:lastFailure.error,equal:true,action,attempts,observation};continue;}
    lastFailure.difference=rejectedDelta;failures.push(lastFailure);continue;}
   const accepted=action.successOut?view().getUint32(out,true):1,actual=capture(),delta=difference(expected,actual);
   if(accepted===(action.expectedAcceptance??1)&&!delta){const receipt={round:m.round,actor:m.actor,input:m.input,kind:kindNames[m.kind],from:m.from,to:m.to,accepted,returnedFalse:!!action.successOut&&accepted===0,equal:true,action,attempts,observation};if(m.kind===23){matchingAlternatives.push({action,statusCode:0,accepted,returnedFalse:receipt.returnedFalse});success??=receipt;continue;}success=receipt;break;}
   lastFailure={action,accepted,difference:delta};
   failures.push(lastFailure);
   }
   if(!success)throw Error(`action round-trip differs ${JSON.stringify({m,failures,before:actor(before),after:actor(expected)})}`);
   if(m.kind===23){success.matchingAlternatives=matchingAlternatives;success.inverseUniqueAmongTested=matchingAlternatives.length===1;}
   checks.push(success);
  }
 if(checks.length!==spec.decisions)throw Error(`only ${checks.length}/${spec.decisions} actions within bounds`);
 console.log(JSON.stringify({schema:spec.observationsOnly?'zero-exact-decision-observations/v1':'zero-engine-action-roundtrip/v2',pass:true,checks,steps,
  discovered:Object.fromEntries(Object.entries(discovered).map(([k,v])=>[k,v.length])),coverage:Object.fromEntries(Object.keys(discovered).map(k=>[k,checks.filter(x=>x.kind===k).length])),
  engineSha256:spec.engineSha256,replaySha256:spec.replaySha256,mask:[48,56],
  scope:spec.observationsOnly?'Exact own-perspective observation before sampled replay inputs; personal-visible membership from official vision mask; zero production observation/features and fuel not exercised':'Official spectator state + timeline successor after reconstructed Game_Action inputs; typed cast target inferred by exact successor; zero production path and fuel not exercised'}));
}finally{if(game!==undefined)check('Game_Destroy',game);for(const p of pointers)e.Engine_Free(p);}
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--replay', type=Path, required=True)
    ap.add_argument('--decisions', '--moves', dest='decisions', type=int, default=100)
    ap.add_argument('--max-steps', type=int, default=50000)
    ap.add_argument('--max-round', type=int, default=64)
    ap.add_argument('--observations-only', action='store_true')
    ap.add_argument('--include', action='append', default=[], metavar='ROUND:ACTOR:KIND')
    ap.add_argument('--compare-decoded', type=Path, help='Compare sampled pre-cast cooldowns with replay_obs update ordering')
    args = ap.parse_args()
    if not 1 <= args.decisions <= 1000 or not 1 <= args.max_steps <= 100000 or not 1 <= args.max_round <= 64:
        ap.error('decisions 1..1000, max-steps 1..100000, max-round 1..64')
    replay = args.replay.resolve(strict=True)
    data = replay.read_bytes()
    if zipfile.is_zipfile(replay):
        with zipfile.ZipFile(replay) as z:
            if z.getinfo('replay.repl26a').file_size > 256 * 1024 * 1024:
                raise ValueError('oversize replay payload')
            payload = z.read('replay.repl26a')
    else:
        payload = data
    if len(payload) < 14 or int.from_bytes(payload[:8], 'little') != 125779999602273:
        raise ValueError('unsupported replay header')
    engine = exp.engine_wasm()
    sha = lambda b: hashlib.sha256(b).hexdigest()
    engine_sha = sha(engine.read_bytes())
    profiles = json.loads((ROOT / 'tools/replay-profiles.json').read_text())['profiles']
    profile = next(p for p in profiles if p['decoderSha256'] == engine_sha)
    if '.'.join(map(str, payload[8:11])) != profile['engineVersion']:
        raise ValueError('replay requires a different engine; no cross-version action claims')
    directory = SCRATCH / uuid.uuid4().hex
    directory.mkdir(parents=True)
    payload_file, script, spec_file = (directory / n for n in ['input.repl26a','roundtrip.mjs','spec.json'])
    payload_file.write_bytes(payload)
    script.write_text(JS, encoding='utf-8', newline='\n')
    spec_file.write_text(json.dumps({'engine':str(engine),'engineSha256':engine_sha,'replaySha256':sha(data),
        'payload':str(payload_file),'payloadSha256':sha(payload),'decisions':args.decisions,
        'items':json.loads((ROOT / 'docs/reference/runtime-catalog.json').read_text())['result']['items'],
        'observationsOnly':args.observations_only,
        'include':args.include,
        'maxSteps':args.max_steps,'maxRound':args.max_round}), encoding='utf-8')
    try:
        record = shared._slotted_command(lambda _: [str(core.NODE),'--disable-warning=ExperimentalWarning',
            str(script),str(spec_file)], ROOT, directory, 55, jobs=1)
    finally:
        while shared._PENDING_CLEANUP:
            if shared.confirm_pending_cleanup():
                break
    stdout, stderr = (directory / 'stdout.bin').read_text(), (directory / 'stderr.bin').read_text()
    # Saved inputs and transport streams regenerate from the source replay.
    for generated in (payload_file, script, directory / 'stdout.bin', directory / 'stderr.bin'):
        generated.unlink(missing_ok=True)
    if record['returncode'] != 0 or not record['tree_exit_confirmed'] or record['timed_out']:
        (directory / 'failure.json').write_text(json.dumps({'returncode':record['returncode'],
            'treeExitConfirmed':record['tree_exit_confirmed'],'stderr':stderr[-4000:]}), encoding='utf-8')
        print(stderr, file=sys.stderr)
        return record['returncode'] or 1
    result = json.loads(stdout)
    if args.compare_decoded:
        decoded = json.loads(gzip.decompress(args.compare_decoded.read_bytes()))
        if decoded['match'] != replay.stem:
            raise ValueError('decoded match differs from replay')
        catalog = json.loads((ROOT / 'docs/reference/runtime-catalog.json').read_text())['result']['champions']
        designs = {c['type']: c for c in catalog}
        by_round = {}
        for event in decoded['events']:
            by_round.setdefault(event[0], []).append(event)
        samples = {(x['round'], x['actor']): x for x in result['checks'] if x['kind'] == 'cast'}
        cooldown = {int(actor): [0, 0] for actor in decoded['tracks']}
        comparisons = []
        for round_number in range(1, max(x['round'] for x in result['checks']) + 1):
            events = by_round.get(round_number, [])
            for actor_id, slots in cooldown.items():
                own = [e for e in events if e[1] == actor_id]
                if not any(e[2] == 'end' for e in own):
                    continue
                sample = samples.get((round_number, actor_id))
                if sample:
                    actual = sample['observation']['self']['cooldown']
                    comparisons.append({'round':round_number,'actor':actor_id,'input':sample['input'],
                        'reconstructed':slots.copy(),'exact':actual,'different':slots != actual})
                # Literal replay_obs.cpp update order: decrement existing CDs,
                # then assign a new cast's full catalog CD (not CD-1).
                slots[:] = [max(0, x - 1) for x in slots]
                cast = next((e for e in own if e[2] == 'cast'), None)
                if cast and cast[3] in (0, 1):
                    champ_type = decoded['tracks'][str(actor_id)]['type']
                    slots[cast[3]] = designs[champ_type]['abilities'][cast[3]]['cooldown']
        result['reconstructionCooldownComparison'] = {
            'scope':'Initial owner-turn cooldown reconstructed with replay_obs update ordering vs exact cooldown before a sampled cast input',
            'decodedSha256':sha(args.compare_decoded.read_bytes()),'comparisons':comparisons,
            'mismatches':sum(x['different'] for x in comparisons)}
    (directory / 'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'pass':result['pass'],'decisions':len(result['checks']),'steps':result['steps'],'coverage':result['coverage'],
        'result':str(directory / 'result.json'),'scope':result['scope']}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
