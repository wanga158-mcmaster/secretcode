"""Bounded ward/consumable legality canary on saved replay branches; no whole games.

Runs native engine WASM through arena's shared slots/private Job helper. Each trial
branches the exact ready prefix anew. Production SDK/core code is never modified.
"""
from __future__ import annotations
import gzip
import hashlib
import json
from pathlib import Path
import sys
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools/arena'))
import compiled_runner as shared
import core
from roundtrip import JS as ROUNDTRIP_JS


def analyze(result, spec):
    """Check a conservative fact predicate against legal attempts, including hidden-obstruction false results."""
    rows = {r['input']: r for r in spec['cases']}
    mismatches, categories, mask_mismatches = [], {}, []
    for trial in result['trials']:
        row = rows[trial['input']]
        self = row['before']['self']
        if 'knownAllyWardMaskHere' in trial:
            listed = any(w['team'] == self['team'] and w['at'] == trial['pos'] for w in trial['knownWards'])
            if listed != trial['knownAllyWardMaskHere']:
                mask_mismatches.append({'input': trial['input'], 'pos': trial['pos'], 'listed': listed, 'mask': trial['knownAllyWardMaskHere']})
        free = self['hp'] > 0 and not self['stateBits'] & (2 | 4 | 32 | 4096) and not self['stunned']
        if trial['kind'] == 'ward':
            known_ally = any(w['team'] == self['team'] and w['at'] == trial['pos'] for w in trial['knownWards'])
            predicted = free and not self['restrictions'] & 32 and self['wardCharges'] > 0
            predicted = predicted and trial['inside'] and trial['range'] <= result['radii']['ward']
            predicted = predicted and not any(k in trial['terrain'] for k in ('wall', 'altar', 'hole'))
            predicted = predicted and (not trial['spawn'] or trial['spawn'][0] == self['team']) and not known_ally
        else:
            own_lock = any(lock['team'] == self['team'] and [lock['x'], lock['y']] == trial['pos'] for lock in row['before']['altarLocks'])
            predicted = free and not self['restrictions'] & 16 and self['consumables'][2] > 0
            predicted = predicted and 'altar' in trial['terrain'] and trial['range'] <= 2 and not own_lock
        if bool(predicted) != (trial['code'] == 0):
            mismatches.append({'input': trial['input'], 'kind': trial['kind'], 'pos': trial['pos'], 'predicted': bool(predicted), 'code': trial['code'], 'message': trial['message']})
        if trial['kind'] == 'ward' and trial['inside'] and trial['range'] <= result['radii']['ward']:
            labels = list(trial['terrain'])
            if trial['unit']: labels.append('unit-occupied')
            if trial['ownWard']: labels.append('own-ward')
            if trial['spawn']: labels.append('own-spawn' if trial['spawn'][0] == self['team'] else 'enemy-spawn')
            for label in labels:
                key = f"{label}:{trial['code']}:{trial['successful']}"
                categories[key] = categories.get(key, 0) + 1
    return {'trials': len(result['trials']), 'predicateMismatches': mismatches, 'alliedWardMaskListMismatches': mask_mismatches,
            'exactSuccessorMatches': sum(t['successorMatches'] is True for t in result['trials']),
            'exactSuccessorDifferences': sum(t['successorMatches'] is False for t in result['trials']),
            'categories': categories,
            'limitations': ['No hole tiles in source map: excluding holes is conservative and unvalidated here.',
                            'No TP inventory/channel/use in source: positive TP-use legality remains unsupported.',
                            'Lock radius 2 is a pinned SDK/rules constant; current native settings expose no query for it.',
                            'Known allied ward occupancy is required; self ward positions alone are insufficient.',
                            'A generic movement-blocked receipt can have multiple typed inverses; input76 uniquely matches Ward among tested forms.']}

TAIL = r'''
try {
 check('Game_FromReplayFileBytes',input,payload.length,out);game=view().getBigUint64(out,true);
 check('Game_GetBitboardMetadata',game,out);stride=view().getUint32(out+16,true);
 check('Game_Visualiser_Timeline_GetMain',game,out);const main=view().getBigUint64(out,true);
 check('Game_API_GetSettings',out);const settings=Buffer.from(new Uint8Array(e.memory.buffer,out,120));
 const radii={ward:settings.readFloatLE(92),control:settings.readFloatLE(100)};
 const wardsOut=alloc(4096),wardMask=alloc(spec.map.boardBytes);
 const maskHas=(ptr,pos)=>{if(pos[0]<0||pos[1]<0||pos[0]>=spec.map.width||pos[1]>=spec.map.height)return false;
   const chunk=(pos[1]>>3)*stride+(pos[0]>>3),bit=((pos[1]&7)<<3)+(pos[0]&7);return !!(view().getBigUint64(ptr+chunk*8,true)&(1n<<BigInt(bit)));};
 const prepare=record=>{
   check('Game_Visualiser_Timeline_Seek',game,main,record.input-1,0);
   let ready=capture();for(let i=0;!ready.moment.awaitingInput&&i<200;i++){check('Game_Visualiser_Timeline_ReplayNext',game,1);ready=capture();}
   if(!ready.moment.awaitingInput||ready.moment.input!==record.input-1||hash(ready.timeline)!==record.beforeTimelineSha256)throw Error('ready prefix mismatch '+record.input);
   check('Game_Visualiser_Timeline_Branch',game,main,record.input-1,out);
 };
 const trials=[];
 for(const record of spec.cases){
   const self=record.before.self,at=self.at;
   check('Game_Visualiser_Timeline_Seek',game,main,record.input-1,0);
   let ready=capture();for(let i=0;!ready.moment.awaitingInput&&i<200;i++){check('Game_Visualiser_Timeline_ReplayNext',game,1);ready=capture();}
   check('Game_Visualiser_Timeline_ReplayNext',game,1);const expected=capture();
   const choices=[];
   for(let dy=-3;dy<=3;dy++)for(let dx=-3;dx<=3;dx++)choices.push([at[0]+dx,at[1]+dy]);
   choices.push([-1,-1],...(self.wards??[]));
   if(record.action.name==='altar-lock')choices.push(record.action.to);
   const seen=new Set();
   for(const pos of choices){const key=pos.join(',');if(seen.has(key))continue;seen.add(key);
     for(const kind of (self.consumables[2]>0?['ward','lock']:['ward'])){
       prepare(record);
       view().setUint32(actorPerspective,self.team,true);view().setUint32(actorPerspective+4,self.id,true);
       check('Game_API_GetWards',game,actorPerspective,wardsOut,128,out);const nwards=view().getUint32(out,true);
       const knownWards=Array.from({length:nwards},(_,i)=>({team:view().getUint32(wardsOut+i*24,true),at:[view().getInt32(wardsOut+i*24+4,true),view().getInt32(wardsOut+i*24+8,true)],owner:view().getUint32(wardsOut+i*24+20,true)}));
       check('Game_API_GetAllyWardOccupancy',game,actorPerspective,wardMask);const knownAllyWardMaskHere=maskHas(wardMask,pos);
       view().setInt32(position,pos[0],true);view().setInt32(position+4,pos[1],true);view().setUint32(out,0xabcdef,true);
       const name=kind==='ward'?'Game_Action_PlaceWard':'Game_Action_UseConsumable';
       const code=kind==='ward'?e[name](game,position,out):e[name](game,2,position,out);
       const successful=code===0?view().getInt32(out,true):null,message=code?string(e.GetLastErrorMsg()):null;
       const originalDestination=pos[0]===record.action.to[0]&&pos[1]===record.action.to[1];
       const successorMatches=originalDestination&&code===0?difference(expected,capture())===null:null;
       let afterFalse=null;if(code===0&&successful===0){check('Game_API_GetAllyWardOccupancy',game,actorPerspective,wardMask);const revealed=maskHas(wardMask,pos);
         const retry=kind==='ward'?e[name](game,position,out):e[name](game,2,position,out);afterFalse={knownAllyWardAfter:revealed,retryCode:retry,message:retry?string(e.GetLastErrorMsg()):null};}
       const idx=pos[1]*spec.map.width+pos[0],inside=pos[0]>=0&&pos[1]>=0&&pos[0]<spec.map.width&&pos[1]<spec.map.height;
       const terrain=Object.entries(spec.map.terrain).filter(([k,v])=>v.includes(idx)&&inside).map(([k])=>k);
       const spawn=spec.map.spawns.map(([x0,y0,x1,y1],team)=>inside&&pos[0]>=x0&&pos[0]<x1&&pos[1]>=y0&&pos[1]<y1?team:null).filter(x=>x!==null);
       const units=[self,...record.before.visibleChampions??[],...record.before.visibleCamps??[]].filter(u=>u.at[0]===pos[0]&&u.at[1]===pos[1]);
       trials.push({input:record.input,round:record.action.round,actor:self.id,team:self.team,kind,pos,range:Math.max(Math.abs(pos[0]-at[0]),Math.abs(pos[1]-at[1])),inside,terrain,spawn,unit:units.length>0,
         ownWard:(self.wards??[]).some(p=>p[0]===pos[0]&&p[1]===pos[1]),stateBits:self.stateBits,restrictions:self.restrictions,buffs:self.buffs,wardCharges:self.wardCharges,
         consumables:self.consumables,knownWards,knownAllyWardMaskHere,code,successful,message,successorMatches,afterFalse});
     }
   }
 }
 console.log(JSON.stringify({schema:'zero-utility-canary/v1',engineSha256:spec.engineSha256,payloadSha256:spec.payloadSha256,radii,settingsRawHex:settings.toString('hex'),trials,coverage:spec.coverage}));
} finally {if(game!==undefined)check('Game_Destroy',game);for(const p of pointers)e.Engine_Free(p);}
'''


def main():
    out = ROOT / '.cache/main/zero/utility-canary' / uuid.uuid4().hex
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT / '.cache/main/zero/exact/75d9c81c911f4579a71eeb55145c691f'
    export_spec = json.loads((source / 'spec.json').read_text())
    cases, ward, locks, samples, counts, static_map, special = {}, [], [], [], {}, None, set()
    with gzip.open(source / 'inputs.jsonl.gz', 'rt') as stream:
        for line in stream:
            row = json.loads(line)
            if row['type'] == 'static-map': static_map = row
            if row['type'] != 'input': continue
            name = row['action']['name']
            counts[name] = counts.get(name, 0) + 1
            if name == 'ward': ward.append(row)
            if name == 'altar-lock': locks.append(row)
            if row['input'] in (28, 29, 57, 76, 584, 2416, 18813): cases[row['input']] = row
            self = row['before'].get('self')
            if self:
                categories = []
                if self['hp'] <= 0: categories.append('dead')
                if self['hp'] > 0 and not self['wardCharges'] and not self['stunned'] and self['channel'] == 'none': categories.append('zero-charge')
                if self['consumables'][2] and self['channel'] != 'none': categories.append('lock-channel')
                if self['consumables'][2] and self['stunned']: categories.append('lock-stun')
                for category in categories:
                    if category not in special:
                        cases[row['input']] = row
                        special.add(category)
    for row in ward[::max(1, len(ward)//8)][:8] + locks:
        cases[row['input']] = row
    # Find real ward examples with each nearby terrain class, instead of inventing map states.
    for category in ('wall', 'hole', 'altar'):
        terrain = set(static_map['terrain'][category])
        for row in ward:
            x, y = row['before']['self']['at']
            if any((y+dy)*static_map['width']+x+dx in terrain for dy in range(-2,3) for dx in range(-2,3)):
                cases[row['input']] = row
                break
    replay = ROOT / 'results/online/topvtop/003b0c01-9d7a-45a0-9d9d-0dc527cdfd50.zip'
    with zipfile.ZipFile(replay) as archive: payload = archive.read('replay.repl26a')
    digest = lambda b: hashlib.sha256(b).hexdigest()
    if digest(payload) != export_spec['payloadSha256']: raise ValueError('saved replay differs')
    payload_path = out / 'input.repl26a'
    payload_path.write_bytes(payload)
    prefix = ROUNDTRIP_JS[:ROUNDTRIP_JS.index("try{\n check('Game_FromReplayFileBytes'")]
    script = out / 'utility-canary.mjs'
    script.write_text(prefix + TAIL, encoding='utf-8', newline='\n')
    spec = {**export_spec, 'payload': str(payload_path), 'map': static_map,
            'cases': list(cases.values()), 'coverage': {'inputKinds': counts, 'wardInputs': len(ward), 'altarLockInputs': len(locks), 'selectedInputs': sorted(cases), 'specialStates': sorted(special), 'holeTiles': len(static_map['terrain']['hole'])}}
    spec_path = out / 'spec.json'
    spec_path.write_text(json.dumps(spec), encoding='utf-8')
    try:
        record = shared._slotted_command(lambda _: [str(core.NODE), '--disable-warning=ExperimentalWarning', str(script), str(spec_path)], ROOT, out, 120, jobs=1)
    finally:
        payload_path.unlink(missing_ok=True)
    if record.get('returncode') != 0:
        print((out / 'stderr.bin').read_text(errors='replace'))
        return 1
    result = json.loads((out / 'stdout.bin').read_text())
    result['analysis'] = analyze(result, spec)
    (out / 'result.json').write_text(json.dumps(result, indent=2)+'\n')
    (out / 'summary.json').write_text(json.dumps(result['analysis'], indent=2)+'\n')
    (out.parent / 'latest.json').write_text(json.dumps({'result': str(out / 'result.json')})+'\n')
    groups = {}
    for trial in result['trials']:
        key = (trial['kind'], trial['code'], trial['successful'])
        groups[str(key)] = groups.get(str(key), 0) + 1
    print(json.dumps({'trials': len(result['trials']), 'groups': groups, 'coverage': spec['coverage'], 'radii': result['radii'], 'result': str(out / 'result.json')}))
    return int(bool(result['analysis']['predicateMismatches'] or result['analysis']['exactSuccessorDifferences'] or result['analysis']['alliedWardMaskListMismatches']))


if __name__ == '__main__':
    raise SystemExit(main())
