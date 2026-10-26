"""Production SDK/candidate/fuel canary at saved replay input checkpoints.

Uses included fastrun bridge, with separate initialization and 100K decision
intervals. Each input gets a fresh WASM instance; optional warm-observe seeds
SDK caches at the same checkpoint. Network and refresh benchmarks are opt-in.
No whole games are run; benchmark inference never selects the teacher action.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
import uuid
import zipfile

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools/arena'))
import core
import compiled_runner as shared
import exp
import wasi

def launch(argv,directory,timeout=120):
    directory.mkdir(parents=True)
    try:
        record=shared._slotted_command(lambda _:list(map(str,argv)),ROOT,directory,timeout,jobs=1)
    finally:
        while shared._PENDING_CLEANUP:
            if shared.confirm_pending_cleanup():break
    stdout=(directory/'stdout.bin').read_text(errors='replace');stderr=(directory/'stderr.bin').read_text(errors='replace')
    if record['returncode'] or not record['tree_exit_confirmed'] or record['timed_out']:
        raise RuntimeError(stderr[-6000:]+stdout[-1000:])
    return stdout

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--replay',type=Path,required=True)
    ap.add_argument('--exact',type=Path,required=True)
    ap.add_argument('--decisions',type=int,default=12)
    ap.add_argument('--input',type=int,help='One explicit recorded source input, without a plan/control substitution')
    ap.add_argument('--network',action='store_true',help='Benchmark state/trunk/controller+argument heads after teacher action')
    ap.add_argument('--fast-network',action='store_true',help='Use exactfastkernels andonecachedgoalprojection pertraversal')
    ap.add_argument('--weights',type=Path,help='Optional production textweights; defaultnetworkbenchmark useszero parameters and isnotanupperbound')
    ap.add_argument('--warm-observe',action='store_true',help='Seed SDK observation caches in setup before measured decision interval')
    ap.add_argument('--refresh',choices=['none','always','budget'],default='none')
    ap.add_argument('--refresh-reserve',type=int,default=30000)
    ap.add_argument('--decision-budget',type=int,default=100000,help='Diagnostic interval; valuesabove100Kare profilingonly')
    ap.add_argument('--plan-benchmark',action='store_true',help='Internal Farm routing only; engine state must stay unchanged')
    ap.add_argument('--plan-input',type=int,default=21)
    ap.add_argument('--plan-targets',default='0,1,6,28')
    ap.add_argument('--plan-observe',choices=['both','cold','warm'],default='both')
    ap.add_argument('--plan-legacy',action='store_true',help='Also measure legacy flat goal builder cold/warm for firsttarget')
    ap.add_argument('--plan-snapshot',type=Path,help='Freeze plan.cpp/plan.h fromthisimmutablebaseline directory')
    ap.add_argument('--reuse-build',type=Path,help='Reuse exactimmutablecompiledclosure; requirecurrentprobeIPCsourcehash')
    ap.add_argument('--source-build',type=Path,help='Freeze all other sourcefiles fromthisexistingbuild beforeapplyingplanoverride')
    ap.add_argument('--coherent-source',type=Path,help='Use the captured whole repository source manifest for this gate')
    ap.add_argument('--plan-session',action='store_true',help='Use caller-owned GoalPlanSession instead of reference cursor functions')
    ap.add_argument('--student',action='store_true',help='Call the shared production compact student selector before source dispatch')
    ap.add_argument('--student-diagnostic',action='store_true',help='Explicit higher-interval calibration bypass of unknown/fuel argument bounds')
    ap.add_argument('--student-bounds',type=Path,help='New graph calibration JSON with knownMask and bounds[15]')
    ap.add_argument('--student-control',choices=['move','priority'],help='Diagnostic full production pointer/scalar, never an expert label')
    ap.add_argument('--student-lifecycle',action='store_true',help='Real production initializer/caller, one persistent actor, bounded foreign-END fixture')
    ap.add_argument('--student-draft-frame',action='store_true',help='One actual production draft pair before input1 / resolution input2; contract test only')
    ap.add_argument('--sdk-source-intents',type=Path,help='Fixed source26..30 independently decoded Intent artifact; SDK validation only, no NN/data frames')
    ap.add_argument('--lifecycle-priority-move',action='store_true',help='Require PRIORITY9 then MOVE in the same first ready production interval')
    ap.add_argument('--priority-refresh-parity',action='store_true',help='Diagnostic local-vs-full PRIORITY observation/history/rows/mask comparison; never ordinary lifecycle')
    ap.add_argument('--lifecycle-normal-defaults',action='store_true',help='Leave production StudentRuntime bounds/masks untouched; no install_profile override')
    ap.add_argument('--canonical-source-intents',type=Path,help='Consume pipeline canonical typed ATTACK at plan-input for one diagnostic calibration')
    ap.add_argument('--student-root-continuation',action='store_true',help='After complete refresh, inspect actual shared next root with diagnostic END control; no second dispatch')
    ap.add_argument('--lifecycle-move-only',action='store_true',help='One production MOVE in the first ready ordinary interval, then normal yield; no priority/replan')
    ap.add_argument('--student-natural',action='store_true',help='Natural T0 shared selector, no TeacherSource; includes production pre-root guard and separate dispatch reserve')
    ap.add_argument('--student-source-guard',action='store_true',help='Source-forced ordinary caller with the same pre-root guard; no admission bypass')
    ap.add_argument('--student-offline-root',action='store_true',help='Also inspect logits during explicit setup outside100K, so rejected pre-root points retain a model comparison')
    ap.add_argument('--move-exposure-shadow',action='store_true',help='Measure unused MOVE coverage facts in setup, then guard and build them from borrowed selected-head rows; no policy integration')
    ap.add_argument('--fast-dash-arguments',action='store_true',help='Probe-only fact-identical selected-dash computation variant')
    ap.add_argument('--dash-builder-parity',action='store_true',help='Compare every original/fast dash row in explicit warm setup outside the decision interval')
    ap.add_argument('--ground-scene',action='store_true',help='Probe-only compact ground-utility facts before root; full rows only on selection')
    ap.add_argument('--ground-scene-parity',action='store_true',help='Compare ground scene state/context/root and every utility row in explicit warm setup')
    ap.add_argument('--typed-economic-source',action='store_true',help='Validate BUY/TRIBUTE reference payload; natural mode uses it only as the expected successor')
    ap.add_argument('--typed-source-canonical',type=Path,help='Independent canonical source artifact; required for typed BUY')
    ap.add_argument('--typed-source-catalog',type=Path,help='Pinned runtime catalog for typed source validation')
    ap.add_argument('--fast-buy-arguments',action='store_true',help='Probe-only exact single-pass BUY argument computation')
    ap.add_argument('--buy-builder-parity',action='store_true',help='Compare complete BUY and quantity rows in warm setup outside decision interval')
    args=ap.parse_args()
    support_paths=[Path(module.__file__).resolve() for module in (core,shared,exp,wasi)]
    support_sha={str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest() for path in support_paths}
    if not 1<=args.decisions<=200:ap.error('decisions 1..200')
    if args.input is not None and (args.input<1 or args.plan_benchmark or args.student_lifecycle or args.student_draft_frame or args.sdk_source_intents):ap.error('explicit input requires one positive source point without another selection mode')
    if not 1000<=args.decision_budget<=2000000:ap.error('decision-budget1000..2000000')
    if args.plan_benchmark and not args.weights:ap.error('plan-benchmark requires actual --weights')
    if args.student_diagnostic and (not args.student or args.decision_budget<=100000):ap.error('student-diagnostic requires --student and decision-budget >100000')
    if args.student and args.plan_legacy:ap.error('student does not use legacy goal builders')
    if args.student_control and not (args.student and args.plan_benchmark and args.student_diagnostic):ap.error('student-control requires explicit diagnostic student plan checkpoint')
    if args.student_lifecycle and (not args.student or (not args.student_bounds and not args.lifecycle_normal_defaults) or args.student_diagnostic or args.warm_observe):ap.error('lifecycle requires ordinary student plus explicit bounds or normal defaults; no bypass or warm setup')
    if args.lifecycle_normal_defaults and (not args.student_lifecycle or args.student_bounds):ap.error('normal defaults requires lifecycle without any profile file')
    if args.canonical_source_intents and (not args.student or not (args.student_diagnostic or args.student_natural) or not args.plan_benchmark or args.student_control):ap.error('canonical ATTACK requires one explicit restored student checkpoint')
    if args.student_natural and (not args.student or not args.student_bounds or args.student_diagnostic or args.student_lifecycle or args.student_control or args.refresh!='none' or args.decision_budget!=100000):ap.error('natural restored point requires ordinary100K, explicit production bounds, no bypass/lifecycle/control or unconditional refresh')
    if args.student_source_guard and (not args.student or not args.student_bounds or args.student_natural or args.student_diagnostic or args.student_lifecycle or args.student_control or args.refresh!='none' or args.decision_budget!=100000 or args.input is None):ap.error('guarded source requires one ordinary100K explicit student input with bounds, no bypass/natural/lifecycle/control/refresh')
    if args.student_offline_root and (not args.student_natural or not args.plan_benchmark or args.plan_observe!='warm'):ap.error('offline root requires natural restored warm setup checkpoint')
    if args.move_exposure_shadow and not args.student_offline_root:ap.error('MOVE exposure shadow requires the declared ordinary natural/offline setup point')
    if args.fast_dash_arguments and (not args.student or args.input is None or args.student_lifecycle or args.sdk_source_intents):ap.error('fast dash arguments require one explicit student point')
    if args.dash_builder_parity and (not args.student or args.input is None or not args.warm_observe):ap.error('dash row parity requires one explicit warm student point')
    if args.ground_scene_parity and (not args.student or args.input is None or not args.warm_observe or args.student_lifecycle or args.refresh!='none' or args.student_root_continuation):ap.error('ground scene parity requires one explicit warm student point without lifecycle/refresh/continuation')
    if args.ground_scene and not args.ground_scene_parity:ap.error('ground scene requires explicit setup parity')
    if args.typed_economic_source and (not args.student or args.input is None or args.plan_benchmark or args.student_lifecycle or args.student_draft_frame or args.sdk_source_intents or args.canonical_source_intents or args.student_control):ap.error('typed economic source requires one explicit student input without another source selection mode')
    if (args.typed_source_canonical or args.typed_source_catalog) and not args.typed_economic_source:ap.error('typed source artifacts require typed-economic-source')
    if args.fast_buy_arguments and not args.buy_builder_parity:ap.error('fast BUY requires explicit setup parity')
    if args.buy_builder_parity and (not args.typed_economic_source or not args.warm_observe):ap.error('BUY parity requires one typed warm source')
    if args.student_root_continuation and (not args.canonical_source_intents or args.refresh!='always'):ap.error('root continuation requires canonical diagnostic ATTACK and complete refresh')
    if args.student_draft_frame and (not args.student or not args.weights or args.student_diagnostic or args.warm_observe or args.student_lifecycle):ap.error('draft-frame requires student+weights, no warm/bypass/lifecycle')
    if args.sdk_source_intents and (not args.student or args.network or args.student_diagnostic or args.student_lifecycle or args.student_draft_frame or args.warm_observe or args.decision_budget<=100000):ap.error('SDK source validation requires student facts, explicit higher diagnostic budget, no NN/lifecycle/warm/bypass')
    if args.lifecycle_priority_move and not args.student_lifecycle:ap.error('priority-move requires production lifecycle')
    if args.lifecycle_move_only and (not args.student_lifecycle or args.lifecycle_priority_move):ap.error('move-only requires lifecycle and excludes priority-move')
    if args.priority_refresh_parity and (not args.student or args.student_lifecycle or args.decision_budget<=100000 or (not args.sdk_source_intents and args.student_control!='priority')):ap.error('priority parity requires fixed source sequence or diagnostic priority checkpoint')
    typed_source=None
    if args.typed_economic_source:
        from teacher_typed import resolve_teacher_source
        typed_source=resolve_teacher_source(args.exact,args.input,args.typed_source_canonical,args.typed_source_catalog)
        if (args.fast_buy_arguments or args.buy_builder_parity) and typed_source['type']!=5:ap.error('BUY computation probes require a typed BUY source')
    student_bounds={'knownMask':0,'bounds':[0]*15,'dispatchKnownMask':0,'dispatchBounds':[0]*15,'rootBound':20000,'refreshKnownMask':0,'refreshBounds':[0]*15}
    if args.student_bounds:
        student_bounds=json.loads(args.student_bounds.read_text())
        if len(student_bounds['bounds'])!=15 or any(not isinstance(x,int) or x<0 for x in student_bounds['bounds']):ap.error('student bounds must have15 nonnegative integer entries')
        if len(student_bounds['dispatchBounds'])!=15:ap.error('student dispatchBounds must have15 entries')
    scratch=ROOT/'.cache/main/zero/teacher-force'
    directory=scratch/uuid.uuid4().hex;directory.mkdir(parents=True)
    weights_sha=None;weights_file=None
    if args.weights:
        weights_bytes=args.weights.read_bytes();weights_sha=hashlib.sha256(weights_bytes).hexdigest()
        weights_file=scratch/'weights'/f'{weights_sha}.txt'
        with core.FileLock(core.CACHE/'snap'/f'{weights_sha}.lock',timeout=60):
            weights_file.parent.mkdir(parents=True,exist_ok=True)
            if not weights_file.exists():weights_file.write_bytes(weights_bytes)
            elif hashlib.sha256(weights_file.read_bytes()).hexdigest()!=weights_sha:raise RuntimeError('cachedweightidentitydiffers')
    source=Path(__file__).with_suffix('.cpp');tc=ROOT/'.tools/toolchain';wt=tc/'wasmtime-v50.0.0-x86_64-mingw-c-api'
    suite=ROOT/'bots/main/zero';srcs=[suite/'src/sdk.cpp',suite/'src/weights.cpp',*sorted((suite/'src/core').glob('*.cpp'))]
    if args.sdk_source_intents:srcs.append(source.with_name('source_intent.cpp'))
    if args.student_lifecycle or args.student_draft_frame:srcs.extend([suite/'src/bot.cpp',suite/'src/draft.cpp'])
    sources=[source,Path(__file__).resolve(),source.with_name('teacher_lifecycle.h'),source.with_name('teacher_source.h'),source.with_name('teacher_priority.h'),source.with_name('student_sink.py'),ROOT/'tools/fastrun/fastrun.cpp',ROOT/'sdk/cpp/cambc.h',ROOT/'sdk/cpp/imports.h',*sorted((suite/'src').rglob('*.h')),*sorted((suite/'src').rglob('*.cpp'))]
    if args.sdk_source_intents:sources.extend([source.with_name('source_intent.h'),source.with_name('source_intent.cpp')])
    source_bytes={str(p.relative_to(ROOT)):p.read_bytes() for p in sources}
    if args.typed_economic_source:
        typed_module=Path(__file__).with_name('teacher_typed.py')
        source_bytes[str(typed_module.relative_to(ROOT))]=typed_module.read_bytes()
    coherent_manifest=None
    if args.coherent_source:
        coherent_manifest=json.loads((args.coherent_source/'manifest.json').read_text())
        for key in list(source_bytes):
            if key in coherent_manifest:
                data=(args.coherent_source/'source'/key).read_bytes()
                if hashlib.sha256(data).hexdigest()!=coherent_manifest[key]:raise RuntimeError('coherent source identity differs')
                source_bytes[key]=data
    if args.source_build:
        basis=args.source_build.resolve();saved=json.loads((basis/'complete.json').read_text())
        probe_key=str(source.relative_to(ROOT))
        if saved.get(probe_key)!=hashlib.sha256(source_bytes[probe_key]).hexdigest():raise RuntimeError('sourcebasisprobeIPCdiffers')
        source_bytes={key:(basis/'source'/key).read_bytes() for key in saved}
        if any(hashlib.sha256(source_bytes[key]).hexdigest()!=value for key,value in saved.items()):raise RuntimeError('sourcebasisidentitydiffers')
        srcs=[ROOT/key for key in source_bytes if key.startswith(str((suite/'src').relative_to(ROOT))) and key.endswith('.cpp')]
    if args.plan_snapshot:
        for name in ['plan.cpp','plan.h']:source_bytes[str((suite/'src/core'/name).relative_to(ROOT))]=(args.plan_snapshot/name).read_bytes()
    fingerprints={p:hashlib.sha256(data).hexdigest() for p,data in source_bytes.items()}
    buildkey=hashlib.sha256((json.dumps(fingerprints,sort_keys=True)+f'|student={args.student}|lifecycle={args.student_lifecycle}|draft={args.student_draft_frame}|sdk={bool(args.sdk_source_intents)}').encode()).hexdigest()[:24]
    build=scratch/'build'/buildkey
    if args.reuse_build:
        build=args.reuse_build.resolve();saved=json.loads((build/'complete.json').read_text())
        key=str(source.relative_to(ROOT))
        if saved.get(key)!=fingerprints[key]:raise RuntimeError('reusedprobeIPCsourcehashdiffers')
        if args.typed_economic_source:
            for producer in (Path(__file__).resolve(),Path(__file__).with_name('teacher_typed.py')):
                producer_key=str(producer.relative_to(ROOT))
                if saved.get(producer_key)!=fingerprints[producer_key]:raise RuntimeError('reused typed-source producer differs')
        fingerprints=saved
    native=build/'teacher.exe';probe=build/'probe.wasm'
    version_lock=core.CACHE/'snap'/f'{build.name}.lock'
    with core.FileLock(version_lock,timeout=60):
        build.mkdir(parents=True,exist_ok=True)
        if not (build/'complete.json').is_file():
            closure=build/'source'
            for relative,data in source_bytes.items():
                target=closure/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
            staged_source=closure/source.relative_to(ROOT)
            staged_srcs=[closure/p.relative_to(ROOT) for p in srcs]
            (build/'requested-sources.json').write_text(json.dumps(fingerprints,indent=2))
            launch([tc/'llvm-mingw-20261020-ucrt-x86_64/bin/clang++.exe','-std=c++20','-O2','-Wno-unused-parameter','-Wno-unused-function',f'-I{wt/"include"}',staged_source,'-o',native,'-static',wt/'lib/libwasmtime.dll.a'],build/'native-build')
            defines=['-DZERO_TEACHER_WASM']+(['-DZERO_STUDENT_PROBE'] if args.student else [])+(['-DZERO_STUDENT_LIFECYCLE'] if args.student_lifecycle or args.student_draft_frame else [])
            if args.sdk_source_intents:defines.append('-DZERO_SDK_SOURCE_SEQUENCE')
            launch([core.CLANG,*wasi.FLAGS,*defines,f'-I{closure/"sdk/cpp"}',staged_source,*staged_srcs,f'-L{wasi.EH}','-lunwind','-o',probe],build/'wasm-build')
            shutil.copyfile(ROOT/'.tools/fastrun/wasmtime.dll',build/'wasmtime.dll')
            (build/'complete.json').write_text(json.dumps(fingerprints))
    if zipfile.is_zipfile(args.replay):
        with zipfile.ZipFile(args.replay) as z:payload=z.read('replay.repl26a')
    else:payload=args.replay.read_bytes()
    groups={k:[] for k in ['end-turn','move','attack','cast','recall','cancel-channel','set-priority','tribute']}
    by_input={};actors_record=None;source_metadata=None
    with gzip.open(args.exact,'rt') as f:
        for line in f:
            row=json.loads(line)
            if row['type']=='input':by_input[row['input']]=row
            if row['type']=='actors':actors_record=row
            if row['type']=='metadata':source_metadata=row
            if row['type']=='input' and row['action']['name'] in groups and row['before']['self'] is not None:
                groups[row['action']['name']].append(row)
            if args.student_draft_frame and actors_record and 1 in by_input and 2 in by_input:break
            if args.sdk_source_intents and 30 in by_input:break
            if args.plan_benchmark and args.plan_input in by_input:break
    command_indices={};turn_counts={}
    for input,row in sorted(by_input.items()):
        own=row['before'].get('self')
        if own is not None:
            moment=row['before']['moment']
            key=(own['id'],moment['round'],moment['turn'])
            command_indices[input]=turn_counts.get(key,0)
            turn_counts[key]=command_indices[input]+1
    selected=[]
    for depth in range(args.decisions):
        for rows in groups.values():
            if len(selected)>=args.decisions:break
            if len(rows)>depth:selected.append(rows[depth])
    if args.input is not None:
        if args.input not in by_input:ap.error('explicit source input is absent from exact export')
        selected=[by_input[args.input]]
        if not typed_source and selected[0]['action']['name'] not in ('end-turn','move','attack','cast','recall','tribute','cancel-channel','set-priority'):
            ap.error('explicit source input has no supported typed primitive mapping; use a canonical family adapter')
        if (args.fast_dash_arguments or args.dash_builder_parity) and selected[0]['action']['name']!='cast':ap.error('dash computation probes require a cast source input')
    plan_specs=[]
    if args.plan_benchmark:
        targets=[int(x) for x in args.plan_targets.split(',')]
        warmth={'both':(False,True),'cold':(False,),'warm':(True,)}[args.plan_observe]
        plan_specs=[(target,warm,False) for target in targets for warm in warmth]
        if args.plan_legacy:plan_specs.extend((targets[0],warm,True) for warm in (False,True))
        if len(plan_specs)>12:ap.error('atmost12macrocheckpoints')
        selected=[by_input[args.plan_input] for _ in plan_specs]
    if args.student_lifecycle:
        selected=[by_input[args.plan_input]]
    if args.student_draft_frame:
        selected=[by_input[1]]
    if args.sdk_source_intents:
        selected=[by_input[26]]
        intent_artifact=json.loads(args.sdk_source_intents.read_text());intent_records=intent_artifact['records']
        if intent_artifact['source_sha256']!=hashlib.sha256(args.exact.read_bytes()).hexdigest() or [r['input'] for r in intent_records]!=list(range(26,31)):raise ValueError('canonical intent source/slice differs')
        for name,field in [('source_intent.h','source_intent_header_sha256'),('source_intent.cpp','source_intent_cpp_sha256')]:
            if hashlib.sha256(source.with_name(name).read_bytes()).hexdigest()!=intent_artifact[field]:raise ValueError('canonical intent implementation differs')
        intent_lines=[]
        for record in intent_records:
            if record['status']!='resolved':raise ValueError('source inverse unresolved; no SDK representation inference')
            item=record['intent'];argument=item['argument']
            intent_lines.append(' '.join(map(str,[record['sequence'],record['input'],record['raw_kind'],item['type'],argument['kind'],argument['target'],argument['count'],argument['x'],argument['y'],item['target_kind'],item['target_id'],int(item['scalar_known']),item['scalar']]))+'\n')
        (directory/'source-intents.txt').write_text(''.join(intent_lines),newline='\n')
        shutil.copyfile(args.sdk_source_intents,directory/'canonical-intents.json')
    canonical_record=None
    if args.canonical_source_intents:
        artifact=json.loads(args.canonical_source_intents.read_text())
        if artifact['source_sha256']!=hashlib.sha256(args.exact.read_bytes()).hexdigest():raise ValueError('canonical source exact identity differs')
        records=[r for r in artifact['records'] if r['input']==args.plan_input]
        if len(records)!=1 or len(selected)!=1:raise ValueError('canonical calibration requires exactly one source record')
        canonical_record=records[0]
        if canonical_record['status']!='resolved' or canonical_record['intent']['type']!=2:raise ValueError('ATTACK intent not uniquely source-resolved')
        own=selected[0]['before']['self'];origin=canonical_record['decision_origin']
        if (origin['actor'],origin['team'])!=(own['id'],own['team']):raise ValueError('canonical source owner differs')
        (directory/'canonical-source-intent.json').write_text(json.dumps(canonical_record,indent=2)+'\n')
    mapping={'end-turn':0,'move':1,'attack':2,'recall':6,'tribute':7,'cancel-channel':8,'set-priority':14}
    requests=[]
    for index,row in enumerate(selected):
        a=row['action'];kind=a['name'];quantity=1;request_to=a['to']
        if typed_source:
            if typed_source['input']!=row['input'] or typed_source['command_index']!=command_indices[row['input']]:raise ValueError('typed source input/history differs')
            type=typed_source['type'];target=typed_source['target'];quantity=typed_source['count'];request_to=[typed_source['x'],typed_source['y']]
            expected_fields=3|(8 if type==7 else 0)
            if typed_source['match_fields']!=expected_fields:raise ValueError('typed source mask differs from SDK TeacherSource contract')
        else:type=3+a['slot'] if kind=='cast' else mapping[kind];target=a['priority'] if kind=='set-priority' else -10000
        flags=int(args.network)|({'none':0,'always':2,'budget':4}[args.refresh])|(8 if args.warm_observe else 0)|(64 if args.fast_network else 0)
        source_kind=typed_source['kind'] if typed_source else -1
        if args.plan_benchmark:
            target,warm,legacy=plan_specs[index];type=9;flags=16|(8 if warm else 0)|(32 if legacy else 0)|(64 if args.fast_network else 0)|(128 if args.plan_session else 0)
            source_kind=0
        flags|={'none':0,'always':2,'budget':4}[args.refresh]
        if args.student:flags|=256|(512 if args.student_diagnostic else 0)
        if args.student_control:type=1 if args.student_control=='move' else 14;target=-10000 if args.student_control=='move' else 9;source_kind=-1;flags|=1024
        if args.student_lifecycle:flags|=2048
        if args.lifecycle_priority_move:flags|=16384
        if args.lifecycle_normal_defaults:flags|=65536
        if args.lifecycle_move_only:flags|=262144
        if args.sdk_source_intents:flags=8192
        if args.priority_refresh_parity:flags|=32768
        if canonical_record:
            intent=canonical_record['intent'];argument=intent['argument'];type=intent['type'];source_kind=argument['kind'];target=argument['target'];flags&=~16
            if [argument['x'],argument['y']]!=a['to']:raise ValueError('canonical/source attack position differs')
        if args.student_root_continuation:flags|=131072
        if args.student_natural:flags|=524288
        if args.student_source_guard:flags|=4194304
        if args.student_offline_root:flags|=1048576
        if args.move_exposure_shadow:flags|=2097152
        if args.fast_dash_arguments:flags|=8388608
        if args.dash_builder_parity:flags|=16777216
        if args.ground_scene:flags|=33554432
        if args.ground_scene_parity:flags|=67108864
        if args.fast_buy_arguments:flags|=134217728
        if args.buy_builder_parity:flags|=268435456
        if args.student_draft_frame:
            if not actors_record:raise ValueError('draft frame requires native completed label table')
            if by_input[1]['action']['kind']!=7 or by_input[2]['action']['kind']!=19 or by_input[1]['before']['draftFacts']['team']!=0 or by_input[2]['before']['draftFacts']['team']!=0:raise ValueError('draft source pair differs from proven first transaction')
            pick=actors_record['completedDraftPicksByTeam'][0][0]
            if pick['status']!='picked':raise ValueError('draft label unresolved')
            type=0;target=pick['rawPick'];flags=4096|256;source_kind=-1
        suffix=' '.join(map(str,[source_kind,command_indices.get(row['input'],0),student_bounds['knownMask'],*student_bounds['bounds'],student_bounds['dispatchKnownMask'],student_bounds['rootBound'],*student_bounds['dispatchBounds'],student_bounds.get('refreshKnownMask',0),*student_bounds.get('refreshBounds',[0]*15)]))
        requests.append(f'{row["input"]} {type} {target} {request_to[0]} {request_to[1]} {flags} {args.refresh_reserve} {args.decision_budget} {quantity} {suffix}\n')
    (directory/'requests.txt').write_text(''.join(requests),newline='\n')
    (directory/'input.repl26a').write_bytes(payload)
    engine_path=exp.engine_wasm();engine_sha=hashlib.sha256(engine_path.read_bytes()).hexdigest()
    if source_metadata and (source_metadata['replaySha256']!=hashlib.sha256(args.replay.read_bytes()).hexdigest() or source_metadata['engineSha256']!=engine_sha):raise ValueError('source replay/engine identity differs')
    command=[native,engine_path,probe,directory/'input.repl26a',directory/'requests.txt',directory/'jit']
    if weights_file:command.append(weights_file)
    stdout=launch(command,directory/'run',180)
    parsed=[json.loads(line) for line in stdout.splitlines()]
    if args.sdk_source_intents:
        witness=next(r['sourceSDK'] for r in parsed if 'sourceSDK' in r);observations=[r['sourceObservation'] for r in parsed if 'sourceObservation' in r]
        differences={};inapplicable_fields=[]
        for index,actual in enumerate(observations):
            record=by_input[actual['input']];before=record['before'];own=before['self'];actor=own['id'];team=own['team']
            expected={'input':record['input'],'commandIndex':index,'actor':actor,'characterType':own['type'],'at':own['at'],'hp':own['hp'],'shield':own['shield'],
                      'gold':own['gold'],'gems':own['gems'],'cooldown':own['cooldown'],'stateBits':own['stateBits'],'channelCode':{'none':0,'recall':1,'tribute':2,'teleport':3}[own['channel']],
                      'channelRemaining':own['channelRemaining'],'nextPriority':own['nextPriority'],
                      'allies':sorted(c['id'] for c in before['visibleChampions'] if c['id']!=actor and c['team']==team),
                      'enemies':sorted(c['id'] for c in before['visibleChampions'] if c['id']!=actor and c['team']!=team),'camps':sorted(c['id'] for c in before['visibleCamps'])}
            for name,value in expected.items():
                if name=='channelRemaining' and value is None and own['channel']=='none':
                    inapplicable_fields.append({'input':record['input'],'field':'channelRemaining','reason':'No active source channel; exported timer is inapplicable, SDK Obs uses neutral0.'})
                    if actual['channelCode']!=0 or actual['channelRemaining']!=0:differences.setdefault(str(record['input']),{})[name]={'source':'inapplicable(no channel)','sdk':actual[name]}
                    continue
                observed=sorted(actual[name]) if name in ('allies','enemies','camps') else actual[name]
                equal=struct.pack('<f',value)==struct.pack('<f',observed) if name in ('hp','shield') else value==observed
                if not equal:differences.setdefault(str(record['input']),{})[name]={'source':value,'sdk':observed}
        fact_meta=[r['sourceFacts'] for r in parsed if 'sourceFacts' in r]
        priority_parity=[r['priorityRefreshParity'] for r in parsed if 'priorityRefreshParity' in r]
        result={'schema':'zero-sdk-source-five/v1','purpose':'sdk_source_validation_only','pass':witness['pass'] and not differences and all(r['pass'] for r in priority_parity),'witness':witness,'priorityRefreshParity':priority_parity,
                'sourceSha256':fingerprints,'wholeSourceManifest':coherent_manifest,'coherentSource':str(args.coherent_source),'build':str(build),
                'canonicalIntentArtifact':str(args.sdk_source_intents),'canonicalIntentSha256':hashlib.sha256(args.sdk_source_intents.read_bytes()).hexdigest(),
                'sourceExactSha256':hashlib.sha256(args.exact.read_bytes()).hexdigest(),'replaySha256':hashlib.sha256(args.replay.read_bytes()).hexdigest(),
                'observations':observations,'observationDifferences':differences,'inapplicableSourceFields':inapplicable_fields,'successors':[r['sourceSuccessor'] for r in parsed if 'sourceSuccessor' in r],
                'factBodies':[dict(meta,path=str(directory/f'sdk-facts-{i}.bin'),sha256=hashlib.sha256((directory/f'sdk-facts-{i}.bin').read_bytes()).hexdigest()) for i,meta in enumerate(fact_meta) if (directory/f'sdk-facts-{i}.bin').is_file()],
                'preparationVersionLock':str(version_lock),'engineSha256':engine_sha,'noNN':True,'noLearningFrames':True,'deploymentQualified':False}
        (directory/'result.json').write_text(json.dumps(result,indent=2)+'\n');(directory/'input.repl26a').unlink()
        print(json.dumps({'pass':result['pass'],'result':str(directory/'result.json'),'completed':witness['completed'],'differences':differences}))
        return 0 if result['pass'] else 1
    if args.student_draft_frame:
        import student_sink
        sink_key=str(source.with_name('student_sink.py').relative_to(ROOT))
        if hashlib.sha256(source.with_name('student_sink.py').read_bytes()).hexdigest()!=fingerprints[sink_key]:raise ValueError('sink transport source changed after capture')
        witness=next(r['draftFrame'] for r in parsed if 'draftFrame' in r)
        result={'schema':'zero-student-draft-frame/v2','purpose':'contract_test','pass':witness['pass'],'witness':witness,
                'weightsSha256':weights_sha,'weightsSnapshot':str(weights_file),'coherentSource':str(args.coherent_source),
                'wholeSourceManifest':coherent_manifest,'sourceSha256':fingerprints,'build':str(build),
                'enginePath':str(engine_path),'engineSha256':engine_sha,
                'preparationVersionLock':str(version_lock),'preparationPayloadRoot':str(build),
                'replaySha256':hashlib.sha256(args.replay.read_bytes()).hexdigest(),'exactSha256':hashlib.sha256(args.exact.read_bytes()).hexdigest(),
                'events':[r for r in parsed if 'probeEvent' in r],'trainingQualified':False,'optimizerAllowed':False}
        if witness['pass']:
            body=(directory/'draft-body.bin').read_bytes();n=witness['rows']
            if len(body)!=192+n*116:raise ValueError('draft native body shape differs')
            def origin(seq,input,kind):
                record=by_input[input];moment=record['before']['moment']
                if record['sequence']!=seq or record['action']['kind']!=kind or moment['phase']!=12 or moment['actor']!=0xffffffff:raise ValueError('draft raw origin differs')
                return {'sequence':record['sequence'],'input':record['input'],'actor':-1,'team':record['before']['draftFacts']['team'],'round':moment['round'],'phase':moment['phase'],'native_kind':kind}
            won=int(source_metadata['winner']==0) if source_metadata and source_metadata.get('winner') in (0,1) else -1
            metadata={'kind':1,'head':6,'command':-1,'n':n,'label':witness['label'],'feature_schema':2,'won':won,
                      'rule_mask':None,'admitted_mask':None,'decision_origin':origin(0,1,7),'label_origin':origin(1,2,19),
                      'event_role':'draft_dispatch','label_kind':'observed_draft_choice','history_type':'source_draft_prefix/v1',
                      'qualification':{'source_semantic_pass':True,'representation_pass':True,'history_contract_pass':True,
                                       'deployment_slice_pass':False,'optimizer_allowed':False,'purpose':'contract_test'},
                      'pairproof':{'verified':True,'phase_checked':witness['phaseChecked'],'dispatch_api':'Game_Action_Draft',
                                   'resolution_api':'Game_Runner_EndTurn','dispatch_input':1,'resolution_input':2,'team':0,'round':0,
                                   'resolved_raw_pick':witness['selectedRawPick'],'proof_sha256':hashlib.sha256((directory/'run/stdout.bin').read_bytes()).hexdigest()}}
            payload=b''.join(body[192+i*116:212+i*116] for i in range(n));phi=b''.join(body[212+i*116:308+i*116] for i in range(n))
            encoded=student_sink.encode_record_v2(metadata,body[:192],payload,phi)
            decoded=student_sink.validate_frame_v2(encoded)
            if encoded[-len(body):]!=body:raise ValueError('host draft framing changed actual feature/payload bytes')
            sink_path=directory/'draft.zstud';writer=student_sink.SinkWriterV2(sink_path,provenance={'purpose':'contract_test','source':str(args.exact),'sourceSha256':result['exactSha256']})
            writer.append_native(encoded,provenance={'actualPlayDraft':True,'dispatchAndResolutionMatch':True,'finalYieldReason':witness['reason'],'finalReservePass':witness['used']<=witness['budget']-8000})
            writer.close(directory/'sink-audit.json')
            result.update({'record':str(sink_path),'recordSha256':hashlib.sha256(sink_path.read_bytes()).hexdigest(),'nativeBodySha256':hashlib.sha256(body).hexdigest(),
                           'wireBodyBitEqual':True,'metadata':metadata,'rawEvents':[{'origin':origin(0,1,7),'event_role':'draft_dispatch'},{'origin':origin(1,2,19),'event_role':'draft_resolution'}]})
        (directory/'result.json').write_text(json.dumps(result,indent=2)+'\n');(directory/'input.repl26a').unlink()
        print(json.dumps({'pass':result['pass'],'result':str(directory/'result.json'),'record':result.get('record')}))
        return 0 if result['pass'] else 1
    if args.student_lifecycle:
        intervals=[r['lifecycle'] for r in parsed if 'lifecycle' in r]
        outcome=next(r['lifecycleResult'] for r in parsed if 'lifecycleResult' in r)
        result={'schema':'zero-student-lifecycle/v1','label_mode':'diagnostic_control','pass':outcome['pass'],
                'scope':'One persistent real production module on a bounded saved-replay branch; foreign actors END explicitly; no expert/data/whole-game claim.',
                'weightsSha256':weights_sha,'weightsSnapshot':str(weights_file),'coherentSource':str(args.coherent_source),
                'wholeSourceManifest':coherent_manifest,'sourceSha256':fingerprints,'build':str(build),'profile':student_bounds,
                'intervals':intervals,'events':[r for r in parsed if 'probeEvent' in r],'outcome':outcome,
                'trainingQualified':False,'lifecyclePriorityMove':args.lifecycle_priority_move,'lifecycleMoveOnly':args.lifecycle_move_only,'normalDefaults':args.lifecycle_normal_defaults,'profileOverridden':not args.lifecycle_normal_defaults,'preparationVersionLock':str(version_lock),
                'historyScope':'Actual persistent initializer, observation, Memory and engine progression within this divergent control branch.'}
        (directory/'result.json').write_text(json.dumps(result,indent=2)+'\n');(directory/'input.repl26a').unlink()
        print(json.dumps({'pass':result['pass'],'intervals':len(intervals),'result':str(directory/'result.json')}))
        return 0 if result['pass'] else 1
    rows=[]
    for record in parsed:
        if 'reason' in record:rows.append(record)
        elif 'sdkObservation' in record:rows[-1]['sdkObservation']=record['sdkObservation']
        elif 'benchmark' in record:rows[-1].update(record['benchmark'])
        elif 'macro' in record:rows[-1]['macro']=record['macro']
        elif 'observer' in record:rows[-1]['observer']=record['observer']
        elif 'admission' in record:rows[-1]['admission']=record['admission']
        elif 'student' in record:rows[-1]['student']=record['student']
        elif 'diagnosticControl' in record:rows[-1].update(record['diagnosticControl'])
        elif 'continuation' in record:rows[-1]['continuation']=record['continuation']
        elif 'naturalAdmission' in record:rows[-1]['naturalAdmission']=record['naturalAdmission']
        elif 'actionOutcome' in record:rows[-1]['actionOutcome']=record['actionOutcome']
        elif 'offlineRoot' in record:rows[-1]['offlineRoot']=record['offlineRoot']
        elif 'contextCache' in record:rows[-1]['contextCache']=record['contextCache']
        elif 'actualPosition' in record:rows[-1]['actualPosition']=record['actualPosition']
        elif 'moveExposure' in record:rows[-1]['moveExposure']=record['moveExposure']
        elif 'dashBuilderParity' in record:rows[-1]['dashBuilderParity']=record['dashBuilderParity']
        elif 'groundSceneParity' in record:rows[-1]['groundSceneParity']=record['groundSceneParity']
        elif 'buyBuilderParity' in record:rows[-1]['buyBuilderParity']=record['buyBuilderParity']
    for index,row in enumerate(rows):
        actual=row['sdkObservation']
        observation=selected[index]['before'];self=observation['self']
        expected={key:self[key] for key in ['at','hp','gold','gems','cooldown','stateBits']}
        expected.update({'allowances':sum((1<<i) if self['allowances'][key] else 0 for i,key in enumerate(['move','attack','cast'])),
            'allies':sum(c['team']==self['team'] for c in observation['visibleChampions']),
            'enemies':sum(c['team']!=self['team'] for c in observation['visibleChampions']),'camps':len(observation['visibleCamps'])})
        row['observationDifferences']={key:{'exact':value,'sdk':actual[key]} for key,value in expected.items()
            if value!=actual[key] and not (key=='hp' and struct.pack('<f',value)==struct.pack('<f',actual[key]))}
    if any(hashlib.sha256(path.read_bytes()).hexdigest()!=support_sha[str(path.relative_to(ROOT))] for path in support_paths):raise RuntimeError('support tool source changed during native canary; preserve output without rebinding')
    result={'schema':'zero-sdk-teacher-force/v2','scope':'Fresh per-input WASM caches, SDK observe + core candidates + SDK primitive; separate initialization, 100K decision interval; optional pure network benchmark after action and conditional refresh',
        'supportToolSha256':support_sha,
        'networkBenchmark':args.network,'fastNetwork':args.fast_network,'warmObserve':args.warm_observe,'weightsSha256':weights_sha,'weightsSnapshot':str(weights_file) if weights_file else None,
        'planSourceOverride':str(args.plan_snapshot) if args.plan_snapshot else None,
        'sourceBasisBuild':str(args.source_build) if args.source_build else None,
        'coherentSource':str(args.coherent_source) if args.coherent_source else None,'wholeSourceManifest':coherent_manifest,
        'planSession':args.plan_session,
        'student':args.student,'studentDiagnosticBypass':args.student_diagnostic,'studentBounds':student_bounds,'studentControl':args.student_control,
        'studentNatural':args.student_natural,'studentSourceGuard':args.student_source_guard,'explicitInput':args.input,'offlineRootSetup':args.student_offline_root,'moveExposureShadow':args.move_exposure_shadow,'fastDashArguments':args.fast_dash_arguments,'dashBuilderParitySetup':args.dash_builder_parity,'groundScene':args.ground_scene,'groundSceneParitySetup':args.ground_scene_parity,'traceEvents':[r for r in parsed if 'probeEvent' in r],
        'canonicalSourceIntent':canonical_record,'canonicalSourceIntentArtifact':str(args.canonical_source_intents) if args.canonical_source_intents else None,
        'canonicalSourceIntentArtifactSha256':hashlib.sha256(args.canonical_source_intents.read_bytes()).hexdigest() if args.canonical_source_intents else None,
        'typedEconomicSource':typed_source,
        'typedEconomicSourceRole':('expected_successor_only' if args.student_natural else 'teacher_source') if typed_source else None,
        'fastBuyArguments':args.fast_buy_arguments,'buyBuilderParitySetup':args.buy_builder_parity,
        'studentRootContinuation':args.student_root_continuation,
        'historyScope':'Fresh per-checkpoint Memory with optional same-checkpoint warm observation; source command_index is chronological expert inputs per actor/round, internal annotations absent. Not complete deployment-history parity.',
        'planBenchmark':args.plan_benchmark,'decisionBudget':args.decision_budget,
        'refreshMode':args.refresh,'refreshReserve':args.refresh_reserve,
        'replaySha256':hashlib.sha256(args.replay.read_bytes()).hexdigest(),'exactSha256':hashlib.sha256(args.exact.read_bytes()).hexdigest(),
        'sourceSha256':fingerprints,'build':str(build),
        'priorityRefreshParity':[r['priorityRefreshParity'] for r in parsed if 'priorityRefreshParity' in r],
        'rows':rows,'pass':all((r.get('controlProgress',False) if args.student_control else r['equal']) and r['reason']==0 and r['phase']==2 and r['represented'] for r in rows) and all(r['priorityRefreshParity']['pass'] for r in parsed if 'priorityRefreshParity' in r),
        'snapshotPass':all(r['equal'] for r in rows),'representationPass':all(r['represented'] for r in rows),
        'normalBudgetPass':all(r['reason']==0 and r['phase']==2 and r['decisionFuel']<=100000 for r in rows),
        'cleanupReservePass':all(r['reason']==0 and r['phase']==2 and r['decisionFuel']<=92000 for r in rows)}
    (directory/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    (directory/'input.repl26a').unlink()
    print(json.dumps({'pass':result['pass'],'decisions':len(rows),'equal':sum(r['equal'] for r in rows),'represented':sum(r['represented'] for r in rows),'result':str(directory/'result.json')}))
    return 0 if result['pass'] else 1

if __name__=='__main__':raise SystemExit(main())
