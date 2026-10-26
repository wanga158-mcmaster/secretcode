"""Bounded warm TRIBUTE fixture using the actual caller and included fastrun.

No cold initialization, original-expert private history, natural reachability,
training or strength claim. The native fixture owns its fixed sequence and stops
before treatment if the prefix, health or equivalent-command placebo fails.
Native work uses the existing global Slots and private Windows Jobs. Run only
after the source owner's handoff, with reviewed native execution.
"""
from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import sys
import zipfile

# Shared tool modules are read-only inputs, including their cache directories.
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[4]
SUITE = ROOT / 'bots/main/zero'
OUT = ROOT / '.cache/main/zero/tribute-sequence'
REPLAY = ROOT / 'results/online/topvtop/003b0c01-9d7a-45a0-9d9d-0dc527cdfd50.zip'
WEIGHTS = ROOT / '.cache/main/zero/runtime-variants/context-cache/cache-on.txt'
EXPECTED_REPLAY = '954fa004013d519784049711d229cb8a85f899ab324e5933e87c7b26db457f9c'
EXPECTED_WEIGHTS = 'addfccba0ea339a7346e0f61987e76517f42f9ef54b40b6a33d2b6a11a9fa019'
EXPECTED_ENGINE = '029ecd290dc400e75418178ec386d72cbd7d9f2fc2a2e451bf594ca23566df48'
EXPECTED_SUPPORT = {
    'tools/arena/core.py': 'c8bdf9a72bd2d20e032258d6b1b330cfde9719f43edd0e32b82b9a78adc3cd5e',
    'tools/arena/compiled_runner.py': '8e570c70a45fe1eb67e9c5d688cfcb7fc65b246d5758cd1787c2d8b3584fefb0',
}
sys.path.insert(0, str(ROOT / 'tools/arena'))
import core
import compiled_runner as native
import exp
import wasi


def sha(data):
    return hashlib.sha256(data).hexdigest()


def key(path):
    return str(path.relative_to(ROOT)).replace('\\', '/')


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n',
                    encoding='utf-8', newline='\n')


def collect_sources():
    """Literal owned/shared helper closure, copied privately before any compiler."""
    compile_sources = [SUITE / 'src/sdk.cpp', SUITE / 'src/weights.cpp',
                       *sorted((SUITE / 'src/core').glob('*.cpp'))]
    pending = [Path(__file__).with_suffix('.cpp'), Path(__file__).resolve(),
               *compile_sources, SUITE / 'src/bot.cpp',
               ROOT / 'sdk/cpp/cambc.h', ROOT / 'sdk/cpp/imports.h',
               ROOT / 'tools/fastrun/fastrun.cpp']
    allowed = (SUITE, ROOT / 'sdk/cpp', ROOT / 'tools/fastrun')
    found = {}
    while pending:
        path = pending.pop().resolve()
        if path in found:
            continue
        if not any(path.is_relative_to(base) for base in allowed):
            raise RuntimeError(f'source closure escaped permitted trees: {path}')
        data = path.read_bytes()
        found[path] = data
        if path.suffix in ('.cpp', '.h'):
            for include in re.findall(rb'^\s*#include\s*"([^"]+)"', data, re.M):
                pending.append((path.parent / include.decode()).resolve())
    return {key(path): data for path, data in found.items()}, compile_sources


def support_files():
    paths = {Path(module.__file__).resolve() for module in (core, native, exp, wasi)}
    arena = ROOT / 'tools/arena'
    for module in list(sys.modules.values()):
        file = getattr(module, '__file__', None)
        if file:
            path = Path(file).resolve()
            if path.is_relative_to(arena) and path.suffix == '.py':
                paths.add(path)
    result = {key(path): sha(path.read_bytes()) for path in sorted(paths)}
    for name, expected in EXPECTED_SUPPORT.items():
        if result.get(name) != expected:
            raise RuntimeError(f'shared-tool publication differs from approved epoch: {name}')
    return result


def profile():
    arguments, dispatch = [0] * 15, [0] * 15
    # Complete-caller point measurements, never policy weights or general bounds.
    for action, argument, execution in ((1, 12257, 310), (2, 5375, 229),
                                         (7, 4331, 287), (8, 4078, 320),
                                         (14, 3616, 113)):
        arguments[action], dispatch[action] = argument, execution
    mask = sum(1 << action for action in (1, 2, 7, 8, 14))
    references = {}
    for label, action, relative in (
        ('move', 1, 'teacher-force/1b857ac7d2c14a78bd495ef297eae67a/result.json'),
        ('cancel', 8, 'teacher-force/217b5df6f7474cca800c714d65a4182e/result.json'),
        ('tribute', 7, 'teacher-force/a20558d4f9334fc1828e0e111b102cd5/result.json')):
        path = ROOT / '.cache/main/zero' / relative
        raw = path.read_bytes()
        record = json.loads(raw)
        row = record['rows'][0]
        selected = row['student']['admittedGreedy'] if record['studentNatural'] else row['type']
        if (selected != action or row['reason'] != 0 or row['phase'] != 2 or
                row['primitiveResult'] != 1 or row['log'] or
                row['student']['phaseFuel'][4] + row['executionFuel'] != arguments[action] or
                row['executionFuel'] != dispatch[action]):
            raise RuntimeError(f'profile measurement differs: {label}')
        references[label] = {'path': str(path), 'sha256': sha(raw),
                             'argument': row['student']['phaseFuel'][4],
                             'dispatch': row['executionFuel']}
    ap_path = ROOT / '.cache/main/zero/command-audit/tribute-profile.json'
    ap_raw = ap_path.read_bytes()
    ap = json.loads(ap_raw)
    if any(ap['bounds'][action] != arguments[action] or
           ap['dispatchBounds'][action] != dispatch[action] for action in (2, 14)):
        raise RuntimeError('ATTACK/PRIORITY profile reference differs')
    references['attackPriority'] = {'path': str(ap_path), 'sha256': sha(ap_raw)}
    return {'rootBound': 42000, 'knownMask': mask, 'bounds': arguments,
            'dispatchKnownMask': mask, 'dispatchBounds': dispatch,
            'refreshKnownMask': 0, 'refreshBounds': [0] * 15,
            'scope': 'Experimental warm fixture profile; no BUY/CAST or unmeasured refresh. Point measurements only.',
            'measurementSources': references}


def profile_text(value):
    fields = [value['rootBound'], value['knownMask'], value['dispatchKnownMask'],
              *value['bounds'], *value['dispatchBounds'],
              value['refreshKnownMask'], *value['refreshBounds']]
    return ' '.join(map(str, fields)) + '\n'


def launch(argv, directory, report, timeout=180):
    directory.mkdir(parents=True)
    try:
        record = native._slotted_command(lambda _: list(map(str, argv)), ROOT,
                                         directory, timeout, jobs=1)
        report['jobs'].append({'name': directory.name, **record})
    finally:
        if native._PENDING_CLEANUP and not native.confirm_pending_cleanup():
            raise RuntimeError('private Job cleanup remains unconfirmed')
    stdout = (directory / 'stdout.bin').read_text(encoding='utf-8', errors='replace')
    stderr = (directory / 'stderr.bin').read_text(encoding='utf-8', errors='replace')
    if record['timed_out'] or not record['tree_exit_confirmed'] or (
            record['returncode'] and directory.name != 'run'):
        raise RuntimeError(f'{directory.name} failed: {stderr[-3000:]} {stdout[-1000:]}')
    if directory.name == 'run' and stderr:
        raise RuntimeError('native fixture emitted stderr')
    if directory.name == 'run':
        report['nativeReturncode'] = record['returncode']
    return stdout


def jobs_closed(build):
    records = []
    for stage in ('native-build', 'wasm-build', 'run'):
        path = build / stage / 'process-end.json'
        if path.is_file():
            value = json.loads(path.read_text())
            records.append({'path': str(path), 'sha256': sha(path.read_bytes()),
                            'closed': bool(value.get('tree_exit_confirmed') and
                                value.get('containment', {}).get('empty_confirmed')),
                            'returncode': value.get('returncode'),
                            'timedOut': value.get('timed_out'),
                            'stderrBytes': value.get('stderr', {}).get('size')})
    return records, bool(records) and all(value['closed'] for value in records) and not native._PENDING_CLEANUP


def main():
    original_source, compile_sources = collect_sources()
    original_manifest = {name: sha(data) for name, data in original_source.items()}
    source = dict(original_source)
    bridge = 'bots/main/zero/tools/teacher_force.cpp'
    needle = b'int main(int argc,char**argv){'
    replacement = b'int sequence_original_teacher_main(int argc,char**argv){'
    if source[bridge].count(needle) != 1:
        raise RuntimeError('native bridge entry is not uniquely recognized for private rename')
    source[bridge] = source[bridge].replace(needle, replacement, 1)
    overrides = [{'path': bridge, 'reason': 'Private native entry rename only; original helper remains untouched.',
                  'originalSha256': original_manifest[bridge], 'compiledSha256': sha(source[bridge]),
                  'from': needle.decode(), 'to': replacement.decode()}]
    manifest = {name: sha(data) for name, data in source.items()}
    support = support_files()
    replay_bytes, weights_bytes = REPLAY.read_bytes(), WEIGHTS.read_bytes()
    if sha(replay_bytes) != EXPECTED_REPLAY or sha(weights_bytes) != EXPECTED_WEIGHTS:
        raise RuntimeError('fixed fixture replay/model identity differs')
    engine = exp.engine_wasm()
    engine_sha = sha(engine.read_bytes())
    if engine_sha != EXPECTED_ENGINE:
        raise RuntimeError('fixed replay engine identity differs')
    fixture_profile = profile()
    identity = sha(json.dumps({'source': manifest, 'originalSource': original_manifest,
        'support': support,
        'model': sha(weights_bytes), 'replay': sha(replay_bytes),
        'engine': engine_sha, 'profile': fixture_profile}, sort_keys=True).encode())
    build = OUT / identity[:24]
    report = {'schema': 'zero-tribute-sequence/v1', 'contentSha256': identity,
        'scope': 'One controlled warm actor7 fixture from source prefix2520. Persistent actual production caller/SDK/Memory/bounds after declared setup; not cold, original-source private history, natural reachability, training or strength evidence.',
        'source': manifest, 'originalSource': original_manifest, 'stageOverrides': overrides,
        'support': support,
        'inputs': {'replay': {'path': str(REPLAY), 'sha256': sha(replay_bytes)},
                   'weights': {'path': str(WEIGHTS), 'sha256': sha(weights_bytes)},
                   'engine': {'path': str(engine), 'sha256': engine_sha}},
        'profile': fixture_profile, 'jobs': [], 'wholeGames': 0,
        'recipe': {'prefixInput': 2520, 'actor': 7, 'team': 1, 'startRound': 83,
                   'prefix': ['CANCEL at83', 'recorded MOVE28,26 at84'],
                   'comparisonRound': 85, 'lastRound': 89,
                   'setupBudget': 1000000, 'ownerBudget': 100000,
                   'admissionMultiplier': 1.25, 'callerReserve': 8000}}
    OUT.mkdir(parents=True, exist_ok=True)
    replay_path = build / 'input.repl26a'
    with core.FileLock(core.CACHE / 'snap' / f'{identity}.lock', timeout=60):
        if (build / 'report.json').exists():
            raise RuntimeError(f'immutable fixture result already exists; inspect without rerunning: {build}')
        stage = build / 'source'
        for name, data in source.items():
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        write(build / 'sources.json', report)
        write(build / 'profile.json', fixture_profile)
        (build / 'profile.txt').write_text(profile_text(fixture_profile), encoding='utf-8', newline='\n')
        (build / 'weights.txt').write_bytes(weights_bytes)
        src = stage / 'bots/main/zero/tools/tribute_sequence.cpp'
        srcs = [stage / path.relative_to(ROOT) for path in compile_sources]
        tc = ROOT / '.tools/toolchain'
        wt = tc / 'wasmtime-v50.0.0-x86_64-mingw-c-api'
        compiler = tc / 'llvm-mingw-20261020-ucrt-x86_64/bin/clang++.exe'
        exe, wasm = build / 'tribute-sequence.exe', build / 'tribute-sequence.wasm'
        report['tools'] = {str(path): sha(path.read_bytes()) for path in
            (compiler, core.CLANG, wt / 'lib/libwasmtime.dll.a', ROOT / '.tools/fastrun/wasmtime.dll')}
        try:
            launch([compiler, '-std=c++20', '-O2', '-Wno-unused-parameter', '-Wno-unused-function',
                    f'-I{wt / "include"}', src, '-o', exe, '-static', wt / 'lib/libwasmtime.dll.a'],
                   build / 'native-build', report)
            launch([core.CLANG, *wasi.FLAGS, '-DZERO_TEACHER_WASM', '-DZERO_STUDENT_PROBE',
                    f'-I{stage / "sdk/cpp"}', src, *srcs, f'-L{wasi.EH}', '-lunwind', '-o', wasm],
                   build / 'wasm-build', report)
            shutil.copyfile(ROOT / '.tools/fastrun/wasmtime.dll', build / 'wasmtime.dll')
            with zipfile.ZipFile(io.BytesIO(replay_bytes)) as archive:
                replay_path.write_bytes(archive.read('replay.repl26a'))
            stdout = launch([exe, engine, wasm, replay_path, build / 'module-cache',
                             build / 'weights.txt', build / 'profile.txt'], build / 'run', report)
            lines = [line for line in stdout.splitlines() if line.strip()]
            events = [json.loads(line) for line in lines if line.lstrip().startswith('{')]
            report['nativeNonJsonLines'] = sum(not line.lstrip().startswith('{') for line in lines)
            results = [event['sequenceResult'] for event in events if 'sequenceResult' in event]
            if len(results) != 1:
                raise RuntimeError('fixture did not emit exactly one sequenceResult')
            report['events'] = events
            report['result'] = results[0]
            if not isinstance(results[0].get('pass'), bool):
                raise RuntimeError('fixture result lacks boolean pass')
            report['pass'] = results[0]['pass']
            if report['pass'] and report['nativeReturncode'] != 0:
                raise RuntimeError('fixture reported success with a nonzero native exit')
            if any(sha((ROOT / name).read_bytes()) != digest for name, digest in original_manifest.items()):
                raise RuntimeError('source epoch changed during fixture; preserve without rebinding')
            if any(sha((stage / name).read_bytes()) != digest for name, digest in manifest.items()):
                raise RuntimeError('private compiled source changed during fixture')
            if support_files() != support:
                raise RuntimeError('support epoch changed during fixture; preserve without rebinding')
            if sha(REPLAY.read_bytes()) != EXPECTED_REPLAY or sha(WEIGHTS.read_bytes()) != EXPECTED_WEIGHTS:
                raise RuntimeError('source replay/model changed during fixture')
            if sha(engine.read_bytes()) != EXPECTED_ENGINE or any(
                    sha(Path(name).read_bytes()) != digest for name, digest in report['tools'].items()):
                raise RuntimeError('engine/compiler/runtime identity changed during fixture')
            report['binaries'] = {path.name: sha(path.read_bytes()) for path in
                                  (exe, wasm, build / 'wasmtime.dll')}
        except BaseException as error:
            report.update({'pass': False, 'failure': str(error)})
        finally:
            if replay_path.is_file():
                replay_path.unlink()  # regenerable private extraction only
            ends, closed = jobs_closed(build)
            report['jobEnds'], report['allJobsClosed'] = ends, closed
            report['rawReplayRemoved'] = not replay_path.exists()
        write(build / 'report.json', report)
    print(json.dumps({'pass': report['pass'], 'result': report.get('result'),
        'failure': report.get('failure'), 'allJobsClosed': report['allJobsClosed'],
        'report': str(build / 'report.json')}))
    return 0 if report['pass'] and report['allJobsClosed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
