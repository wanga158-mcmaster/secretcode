"""Rebuild and execute the bounded conditional production-caller comparison.

This is a replay-state fork experiment, not an ordinary match or a claim of
native guest fuel safety. Each invocation writes a new evidence directory.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import subprocess
import sys
import uuid
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'tools/arena'))
import core

ARMS=('fireflies-on','fireflies-control','breakpoint')
MODES=('checkpoint','partial1','recover','capture','channel','lowfuel','missing')
SNAPSHOTS=('specialists/fireflies@f6c57ee622a9','specialists/fireflies@a194be9502fd','specialists/breakpoint-brigade@babb2b9cb23d')
def sha(data):return hashlib.sha256(data).hexdigest()
def descriptor(path):return {'path':path.resolve().relative_to(ROOT).as_posix(),'sha256':sha(path.read_bytes())}
def put(path,record):
    if path.exists():raise core.ArenaError('caller evidence is append-only: '+str(path))
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(record,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
def require(value,reason):
    if not value:raise core.ArenaError(reason)

def validate(case,arm,mode):
    require(case.get('arm')==arm and case.get('mode')==mode,'caller case identity changed')
    require(case.get('checks',{}).get('reachable_offspawn_recovery_false') is True,'missing actual reachable checkpoint')
    require(case.get('worldIntervention',{}).get('equal') is True,'complete private memory not retained')
    for name in ('certificateFork','comparisonFork'):
        proof=case[name]['comparison']
        require(proof['equal'] and proof['maskedBytes']==[48,56] and proof['snapshotBytes']==40152 and proof['referenceSha256']==proof['forkSha256'],'native fork differs beyond timeline identity')
    cert=case['certificate']
    require([cert['before']['champion'][k] for k in ('x','y','health','maxHealth','type','team')]==[5,38,1000,1000,10,1],'certificate is not the pinned Collision B Varg')
    require([cert['after']['champion'][k] for k in ('x','y')]==[6,37] and cert['privateState']['recovering']==0,'real production spawn exit did not preserve false recovery')
    comparison=case['comparison'];bank=comparison['bank'];actions=comparison['actions']
    trace=[row for row in case['trace'] if row.get('stage')=='comparison']
    if mode=='missing':
        require(comparison['failure']=='Deliberately unavailable required query: champion','query-missing mutation did not fail closed')
        require(not bank and not actions and any(r['operation']=='missing_query' and r['name']=='champion' for r in trace),'missing query was bypassed')
        return
    require(case['checks']['complete_prefix'] and comparison['failure'] is None,'complete actual caller prefix failed')
    require(not any(r['operation'] in ('unsupported_import','import_failure','missing_query') for r in trace),'unexpected query/action substituted')
    if mode in ('checkpoint','partial1'):
        before=comparison['before']['champion']
        require([before[k] for k in ('x','y','health','maxHealth','gold','gems','stateBits','type','team')]==[15,30,1402,1500,83,13,2073,10,1],'conditional native B469 packet changed')
        require(comparison['privateBefore']['recovering']==0,'primary packet silently relatches or assigns recovery')
        require(comparison['privateBefore']['partial']==(mode=='partial1'),'partial-clear sensitivity not explicit')
        require([r['kind'] for r in bank]==['entry','result'] and bank[-1]['value']==(arm=='fireflies-on'),'actual production bank return changed')
        require(len(actions)==1 and actions[0]['operation']=='move' and actions[0]['success'] and [actions[0]['x'],actions[0]['y']]==[16,29],'actual first native movement changed')
        require(comparison['after']['champion']['stateBits']==2065,'native movement was not consumed')
        require(any(r['operation']=='Game_API_Champion_CanShop' and r['hex']=='00000000' for r in trace),'off-spawn no-shop guard not natively queried')
        if mode=='checkpoint':
            continuation=case['continuation']
            require(all(r['equal'] for r in continuation['receipts']),'retained peer validation failed')
            if continuation['status']=='physical-split':
                expected=continuation['expected']['champion'];actual=continuation['actual']['champion']
                changed=[key for key in ('x','y','health','maxHealth','gold','gems','stateBits') if expected[key]!=actual[key]]
                if continuation['expected']['submitted']!=continuation['actual']['submitted']:changed.append('submitted')
                require(changed and changed==continuation['changedPhysicalFields'] and continuation['actions'],'metadata/labels alone were classified as a physical split')
                require(all(a.get('success') and a['before']['header']['actor']==4 and a['before']['header']['phase']==10 and a['before']['header']['awaitingInput'] for a in continuation['actions']),'physical split did not execute actual native owner action')
            else:
                require(continuation['status']=='protocol-obstruction' and continuation['expectedHeader']['kind']==6,'unsupported continuation outcome requires review')
                require(continuation['unrecordedPassivePeerTurns']==0 and len(continuation['calls'])>=2,'bounded negative continuation lacks subsequent checked owner turns')
    else:
        require(not bank,'negative control reached the actual bank call')
        if mode=='recover':
            require(comparison['privateBefore']['recovering']==1 and len(actions)==1 and actions[0]['operation']=='recall' and actions[0]['status']==0,'sticky recovery did not preempt through actual native Recall')
            require(comparison['after']['champion']['stateBits']&2,'native Recall did not create a channel')
        else:
            require(not actions,'protected or low-fuel caller emitted a physical action')
            if mode=='channel':require(comparison['before']['champion']['stateBits']&4,'channel control lacks actual native tribute')
            elif mode=='capture':require(comparison['privateAfter']['target']==14,'actual occupied capture group did not preempt')
            elif mode=='lowfuel':require(any(r['operation']=='host_fuel' and r['value']==29999 for r in trace),'low-fuel control did not consume its declared input')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True)
    parser.add_argument('--build-dir')
    args=parser.parse_args()
    output=Path(args.out).resolve()
    roots=[ROOT/'.cache/arena/orchestration/fireflies-caller',ROOT/'results/population/fireflies-caller']
    require(any(output.is_relative_to(p.resolve()) for p in roots),'caller output is outside its assigned evidence directories')
    require(not output.exists(),'refusing to replace accepted caller evidence')
    run_id=uuid.uuid4().hex
    build=Path(args.build_dir).resolve() if args.build_dir else ROOT/'.cache/arena/orchestration/fireflies-caller/runs'/run_id/'build'
    require(build.is_relative_to(roots[0].resolve()),'caller build escaped owned cache')
    case_dir=output.parent/(output.stem+'-cases-'+run_id)
    pins={str(p.relative_to(ROOT).as_posix()):sha(p.read_bytes()) for p in sorted(HERE.glob('caller-*')) if p.is_file()}
    pins[str(Path(__file__).relative_to(ROOT).as_posix())]=sha(Path(__file__).read_bytes())
    command=[sys.executable,'-B',str(HERE/'caller-build.py'),'--build-dir',str(build)]
    built=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
    require(built.returncode==0,'actual-caller build failed: '+built.stdout[-1500:]+built.stderr[-1500:])
    manifest_path=build/'manifest.json';manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    require(tuple(a['snapshot'] for a in manifest['arms'])==SNAPSHOTS,'caller builder supplied different snapshots')
    records=[]
    for arm in ARMS:
        for mode in MODES:
            with core.Slots(1,quiet=True):
                result=subprocess.run([str(core.NODE),'--disable-warning=ExperimentalWarning',str(HERE/'caller-case.mjs'),arm,mode,str(build)],cwd=ROOT,capture_output=True,text=True,timeout=120)
            case=json.loads(result.stdout)
            case['runner']={'returncode':result.returncode,'stderr':result.stderr,'case_source':descriptor(HERE/'caller-case.mjs'),'build_manifest':descriptor(manifest_path)}
            destination=case_dir/(arm+'-'+mode+'.json');put(destination,case)
            require(result.returncode==0,'caller process failed: '+arm+'/'+mode+': '+str(case.get('failure')))
            validate(case,arm,mode)
            records.append({'arm':arm,'mode':mode,'artifact':descriptor(destination),'status':'pass',
                'scope':'Actual prefix/negative-control checks; no physical distinction/admission inferred.',
                'continuation':case.get('continuation',{}).get('status')})
            print('caller '+arm+'/'+mode+': pass',flush=True)
    require(all(sha((ROOT/path).read_bytes())==value for path,value in pins.items()),'caller companions changed during execution')
    checkpoint={r['arm']:json.loads((ROOT/r['artifact']['path']).read_text(encoding='utf-8'))['continuation'] for r in records if r['mode']=='checkpoint'}
    profile=next(p for p in json.loads((ROOT/'tools/replay-profiles.json').read_text(encoding='utf-8'))['profiles'] if p['id']=='cli-3.0.4')
    assets=Path(os.environ['LOCALAPPDATA'])/'cambc/assets'/profile['cacheRelease']/'visualiser/assets'
    asset_hashes={profile['engineFile']:profile['decoderSha256'],
        'index-CexarSMK.js':'8179bff9a6ada21a429f21b88f7e239377da217c600efa3dac6229b863133508',
        'PanelChrome-BXlTYAwV.js':'960affde6198366d2347bd21f43efec5bc06acc6cb0c53e5f7f96dbf87f027e6'}
    require(all(sha((assets/name).read_bytes())==digest for name,digest in asset_hashes.items()),'pinned official native assets changed')
    record={'schema':1,'exit_code':0,'test_cases':len(records),'snapshots':list(SNAPSHOTS),'fixture_sources':pins,
        'build_manifest':descriptor(manifest_path),'build_command':command,'build_stdout':built.stdout,'build_stderr':built.stderr,
        'cases':records,'conditional_outcomes':{arm:{key:value for key,value in outcome.items() if key not in ('receipts','calls')} for arm,outcome in checkpoint.items()},
        'distinct':{'status':'incomplete','reason':'Complete actual-caller/native outcomes require independent review before any admission hook. Metadata and branch labels never count as a physical split; the active-only later Cast remains a finite protocol boundary.'},
        'evidence_paths':[build.relative_to(ROOT).as_posix(),case_dir.relative_to(ROOT).as_posix()],
        'officialAssets':{name:{'path':str((assets/name).resolve()),'sha256':digest} for name,digest in asset_hashes.items()},
        'scope':'Actual production callers from reached spawn-exit checkpoints under declared native-world and fuel interventions. All physical native statuses and reconstructed peer frames checked; peer policies are not rerun. Healthy cross-suite and fresh applicability remain separate.'}
    put(output,record)
    print(str(len(records))+'/'+str(len(records))+' caller cases passed; distinct remains incomplete')
    return 0
if __name__=='__main__':
    try:raise SystemExit(main())
    except Exception as error:print('caller test error: '+str(error),file=sys.stderr);raise SystemExit(1)
