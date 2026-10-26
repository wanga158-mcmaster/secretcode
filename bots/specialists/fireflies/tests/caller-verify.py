"""Read-only reconstruction of a pinned conditional production-caller report.

No saved pass flag establishes distinction: check all pins, check the compiled
actual-source reactors, then execute three fresh native checkpoint forks and
compare the complete resulting certificates/query/action traces with the report.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import subprocess
import struct
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'tools/arena'))
import core
def sha(data):return hashlib.sha256(data).hexdigest()
def require(value,reason):
    if not value:raise core.ArenaError(reason)
def contained(value):
    path=Path(value);path=(ROOT/path).resolve() if not path.is_absolute() else path.resolve()
    require(path.is_relative_to(ROOT),'caller artifact escaped repository')
    return path
def pinned(descriptor):
    require(set(descriptor)=={'path','sha256'},'caller artifact descriptor has unexpected fields')
    path=contained(descriptor['path']);require(sha(path.read_bytes())==descriptor['sha256'],'caller artifact pin changed: '+str(path))
    return path
def digest_case(case):
    contents={k:v for k,v in case.items() if k!='runner'}
    return sha(json.dumps(contents,sort_keys=True,separators=(',',':')).encode())

def retreat_certificate(case,stage):
    rows=[]
    for row in case['trace']:
        if row.get('stage')!=stage:continue
        if row['operation']=='bank_entry':break
        rows.append(row)
    else:raise core.ArenaError('actual caller bank-entry receipt is absent')
    states=[r for r in rows if r['operation']=='Game_API_Camp_GetState']
    require(states and all(r['status']==0 and r['hex'] in ('00000000','02000000') for r in states),'reactive camp incoming is not proven zero')
    require(not any(r['operation']=='Game_API_Camp_Get' or (r['operation']=='Game_API_Champion_Get' and r['args'][2]!=4) for r in rows),'reactive incoming needs additional live combat inputs')
    self_reads=[r for r in rows if r['operation']=='Game_API_Champion_Get']
    require(self_reads,'missing native health inputs')
    health,maximum=struct.unpack_from('<ff',bytes.fromhex(self_reads[-1]['hex']),16)
    require(health>maximum*.35 and health>60,'partial-clear retreat invariance failed')
    return {'stage':stage,'health':health,'max_health':maximum,'incoming':0,
        'health_gt_035max':health>maximum*.35,'health_gt_4incoming_plus60':health>60,
        'queried_camps_dead_or_asleep':len(states),'foreign_live_champion_reads':0,
        'scope':'Complete actual pre-bank prefix queries, SDK camp states0/2 and direct fresh unit getters establish zero reactive incoming; both partial-clear multipliers therefore refuse retreat.'}

def verify(report_path,expected_sha,supporting_path,supporting_sha):
    path=contained(report_path);require(sha(path.read_bytes())==expected_sha,'conditional report hash mismatch')
    report=json.loads(path.read_text(encoding='utf-8'))
    support_path=pinned({'path':supporting_path,'sha256':supporting_sha})
    support=json.loads(support_path.read_text(encoding='utf-8'))
    require(support['officialAssets']==report['officialAssets'],'supporting native assets differ from caller experiment')
    for key in ('native_helper','slot_wrapper'):pinned(support[key])
    for record in support['certificates'].values():
        pinned(record['source']);pinned(record['record'])
    inventory=json.loads(pinned(support['certificates']['inventory-probe']['record']).read_text(encoding='utf-8'))
    require(inventory['before']==inventory['after'] and inventory['snapshotSha256Before']==inventory['snapshotSha256After'] and not inventory['errors'],'inventory ABI probe changed world or failed')
    require(inventory['pins']['sdkSha256']==sha((ROOT/'sdk/cpp/cambc.h').read_bytes()) and inventory['pins']['importsSha256']==sha((ROOT/'sdk/cpp/imports.h').read_bytes()),'inventory SDK inputs changed')
    for kind,count in (('items',42),('consumables',4)):
        require(len(inventory['expected'][kind])==count,'inventory certificate cardinality changed')
        queries=[q for q in inventory['queries'] if q['kind']==kind]
        require({q['capacity'] for q in queries}=={count,count-1,0},'missing inventory capacity refusal')
        for q in queries:
            require(q['arity']==5 and all(q['canaries'].values()),'inventory native guard/arity failed')
            require(q['status']==(0 if q['capacity']==count else 111),'inventory ABI capacity status changed')
            if q['capacity']==count:require(q['actual']==inventory['expected'][kind],'inventory entries differ from official own timeline')
    spec=importlib.util.spec_from_file_location('fireflies_caller_checks',HERE/'run-caller.py')
    checks=importlib.util.module_from_spec(spec);spec.loader.exec_module(checks)
    require(report.get('snapshots')==list(checks.SNAPSHOTS),'conditional report snapshots changed')
    required={p.relative_to(ROOT).as_posix() for p in HERE.glob('caller-*') if p.is_file() and p.name!='caller-verify.py'}|{(HERE/'run-caller.py').relative_to(ROOT).as_posix()}
    require(set(report['fixture_sources']) in (required,required|{Path(__file__).relative_to(ROOT).as_posix()}),'conditional report omitted or added executable companions')
    for source,digest in report['fixture_sources'].items():require(sha(contained(source).read_bytes())==digest,'caller fixture source changed: '+source)
    manifest_path=pinned(report['build_manifest']);build=manifest_path.parent
    command=[sys.executable,'-B',str(HERE/'caller-build.py'),'--check','--build-dir',str(build)]
    checked=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=120)
    require(checked.returncode==0,'compiled source/SDK/knob/fixture pins failed: '+checked.stdout[-500:]+checked.stderr[-500:])
    seen={};saved={}
    for row in report['cases']:
        key=(row['arm'],row['mode']);require(key not in seen,'duplicate caller case')
        case_path=pinned(row['artifact']);case=json.loads(case_path.read_text(encoding='utf-8'))
        require(case['runner']['returncode']==0 and case['runner']['build_manifest']==report['build_manifest'],'case runner provenance mismatch')
        checks.validate(case,*key);seen[key]=case
        if row['mode']=='checkpoint':saved[row['arm']]=case
    require(set(seen)=={(arm,mode) for arm in checks.ARMS for mode in checks.MODES},'caller coverage matrix incomplete')
    fresh={};proof=[]
    for arm in checks.ARMS:
        with core.Slots(1,quiet=True):
            result=subprocess.run([str(core.NODE),'--disable-warning=ExperimentalWarning',str(HERE/'caller-case.mjs'),arm,'checkpoint',str(build)],cwd=ROOT,capture_output=True,text=True,timeout=120)
        require(result.returncode==0,'independent native caller execution failed: '+result.stderr[-500:])
        case=json.loads(result.stdout);checks.validate(case,arm,'checkpoint')
        packet=bytes.fromhex(case['nativePacket']['hex']);offset=next(196+i*252 for i in range(struct.unpack_from('<I',packet,172)[0]) if struct.unpack_from('<II',packet,196+i*252+4)==(4,1))
        expected_items=list(struct.unpack_from('<42I',packet,offset+48))
        expected_consumables=list(struct.unpack_from('<4I',packet,offset+216))
        require(expected_items==inventory['expected']['items'] and expected_consumables==inventory['expected']['consumables'],'fresh native packet differs from scoped inventory certificate')
        queries=[r for r in case['trace'] if r.get('stage')=='comparison' and r['operation']=='Game_API_Champion_GetItems']
        require(queries and all(r['values']==expected_items for r in queries),'fresh own-item bridge differs from official complete native packet')
        require(digest_case(case)==digest_case(saved[arm]),'fresh native query/private/action certificate differs from pinned report: '+arm)
        fresh[arm]=case
        proof.append({'arm':arm,'certificate_sha256':digest_case(case),'snapshot':checks.SNAPSHOTS[checks.ARMS.index(arm)],'continuation':case['continuation']['status'],
            'retreat_invariance':[retreat_certificate(case,s) for s in ('comparison','continuation-470')]})
    active=fresh['fireflies-on'];active_calls={c['round']:c for c in active['continuation']['calls']}
    comparisons=[]
    for arm in ('fireflies-control','breakpoint'):
        other=fresh[arm]['continuation']
        if other['status']!='physical-split':return {'status':'incomplete','reason':arm+' has no checked physical split','proof':proof}
        own=other['calls'][-1];reference=active_calls.get(own['round'])
        require(reference is not None,'active caller was not executed on the split owner turn')
        require(own['before']==reference['before'],'split callers have different native starts')
        require(other['expected']==reference['after'],'split reference is not the active physical caller outcome')
        before=own['before']['champion'];left=reference['after']['champion'];right=own['after']['champion']
        changed=[key for key in ('x','y','health','maxHealth','gold','gems','stateBits') if left[key]!=right[key]]
        require(changed and changed==other['changedPhysicalFields'],'only metadata/labels differ at the split')
        require(reference['bank'][-1]['kind']=='result' and reference['bank'][-1]['value']==1 and own['bank'][-1]['kind']=='result' and own['bank'][-1]['value']==0,'actual caller bank choices do not support threshold replacement')
        require(reference['privateBefore']['recovering']==0 and own['privateBefore']['recovering']==0,'split recovery state is not retained false')
        comparisons.append({'arm':arm,'round':own['round'],'start':[before['x'],before['y']],'active':[left['x'],left['y']],'replacement':[right['x'],right['y']],'changed_physical_fields':changed})
    return {'status':'pass','snapshot':checks.SNAPSHOTS[0],'control_snapshot':checks.SNAPSHOTS[1],'neighbor_snapshot':checks.SNAPSHOTS[2],
        'replacement_removes_coverage':True,'physical_difference':comparisons,'proof':proof,
        'scope':'One declared Collision-B native-world/fuel intervention on reached compatible private checkpoints, then retained legal peer inputs until a checked physical split. Not a historical autonomous Breakpoint decision, guest fuel safety, peer-policy counterfactual or fresh applicability claim.'}

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',required=True);parser.add_argument('--sha256',required=True)
    parser.add_argument('--supporting',required=True);parser.add_argument('--supporting-sha256',required=True)
    args=parser.parse_args();print(json.dumps(verify(args.report,args.sha256,args.supporting,args.supporting_sha256),indent=2,sort_keys=True))
if __name__=='__main__':
    try:main()
    except Exception as error:print(json.dumps({'status':'incomplete','reason':str(error)}));raise SystemExit(1)
