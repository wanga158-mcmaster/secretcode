"""Pure BUY builder parity in private native/WASM source closures.

No SDK execution, games, fitting, fuel or strength claim. Synthetic catalogs are
separate immutable test data; production const tables are never mutated. Native
processes use existing shared arena Slots and private Windows Jobs.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[4]
SUITE = ROOT / 'bots/main/zero'
OUT = ROOT / '.cache/main/zero/buy-fast'
NAMES = ('map', 'moves', 'rules', 'cand', 'action', 'utility', 'ground_scene',
         'student_goal', 'student_state', 'student_action', 'net', 'student_net', 'student_context')
# catalog.cpp is deliberately absent: the test provides cloned immutable catalog fixtures.
sys.path.insert(0, str(ROOT / 'tools/arena'))
import core
import compiled_runner as native
import wasi


def sha(value):
    return hashlib.sha256(value).hexdigest()


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')


def closure():
    pending = [Path(__file__).with_suffix('.cpp'), *(SUITE / 'src/core' / (name + '.cpp') for name in NAMES)]
    found = {Path(__file__).resolve(): Path(__file__).read_bytes()}
    while pending:
        source = pending.pop().resolve()
        if source in found:
            continue
        data = source.read_bytes()
        found[source] = data
        for name in re.findall(rb'^\s*#include\s*"([^"]+)"', data, re.M):
            header = (source.parent / name.decode()).resolve()
            if not header.is_relative_to(SUITE):
                raise RuntimeError('source closure escaped owned zero suite')
            pending.append(header)
    return {str(path.relative_to(ROOT)).replace('\\', '/'): data for path, data in found.items()}


def launch(argv, directory, records):
    directory.mkdir(parents=True)
    try:
        record = native._slotted_command(lambda _: list(map(str, argv)), ROOT, directory, 120, jobs=1)
    finally:
        if native._PENDING_CLEANUP and not native.confirm_pending_cleanup():
            raise RuntimeError('private Job cleanup remains unconfirmed')
    records.append({'name': directory.name, **record})
    stdout = (directory / 'stdout.bin').read_text(encoding='utf-8', errors='replace')
    stderr = (directory / 'stderr.bin').read_text(encoding='utf-8', errors='replace')
    if record['returncode'] or record['timed_out'] or not record['tree_exit_confirmed']:
        raise RuntimeError(f'{directory.name} failed: {stderr[-4000:]} {stdout[-500:]}')
    return stdout


def main():
    data = closure()
    manifest = {name: sha(body) for name, body in data.items()}
    support_paths = [ROOT / 'tools/arena/core.py', ROOT / 'tools/arena/compiled_runner.py',
                     ROOT / 'tools/arena/wasi.py', ROOT / 'tools/arena/wasi-run.mjs',
                     ROOT / 'docs/reference/runtime-catalog.json', ROOT / 'docs/reference/current-version.json']
    support = {str(p.relative_to(ROOT)).replace('\\', '/'): sha(p.read_bytes()) for p in support_paths}
    expected = {'tools/arena/core.py': 'c8bdf9a72bd2d20e032258d6b1b330cfde9719f43edd0e32b82b9a78adc3cd5e',
                'tools/arena/compiled_runner.py': '8e570c70a45fe1eb67e9c5d688cfcb7fc65b246d5758cd1787c2d8b3584fefb0'}
    if any(support[key] != value for key, value in expected.items()):
        raise RuntimeError('shared tool epoch differs from parent-authorized stable publication')
    identity = sha(json.dumps({'source': manifest, 'support': support}, sort_keys=True).encode())
    build = OUT / identity[:20]
    report = {'schema': 'main.zero.student.buy_fast.parity.v1', 'content_sha256': identity,
        'source': manifest, 'support': support, 'jobs': [], 'games': 0,
        'scope': 'Pure production-builder input/status/payload/order/all24float parity; synthetic catalog data is not engine-qualified. No SDK/fuel/history/composition/strength claim.',
        'coverage': {'catalogs': ['literal_current', 'all_purchasable_zero_price', '48item_metadata_boundaries', 'one_item_result47', 'empty'],
            'both_teams': True, 'goals': 14, 'gold_values': [0,1,29,30,31,39,40,59,60,1000,2147483647,4294967295],
            'inventory': ['zero', 'four', 'mixed0through6', 'uint32max'],
            'permissions': ['ready', 'missing_command_facts', 'cannot_buy', 'dead_but_buyable', 'stunned_but_buyable', 'recalling_but_buyable', 'tributing_teleporting_but_buyable', 'restricted_but_buyable'],
            'buyable': ['none', 'all', 'alternating', 'affordability_pattern', 'every_one_hot_id'],
            'facts': ['complete existing catalog domain', 'uncapped ascending unique rows', 'all payload fields', 'all24float words', 'state48 and root immutability',
                'quantity rows and maximum', 'quantity nonfinite/clamp edges', 'nonzero shop/quantity scores and choice within each runtime', 'prefilled output capacities', 'nonground action delegation'],
            'limits': ['kMaxItems48', 'synthetic catalog boundaries are pure defensive tests', 'network values are compared within runtime; raw facts form cross-runtime digest']}}
    OUT.mkdir(parents=True, exist_ok=True)
    with core.FileLock(core.CACHE / 'snap' / f'{identity}.lock', timeout=60):
        if (build / 'report.json').is_file():
            raise RuntimeError('immutable result already exists; inspect it rather than overwrite')
        stage = build / 'source'
        for relative, body in data.items():
            target = stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
        write(build / 'sources.json', report)
        src = stage / 'bots/main/zero/tools/student_buy_fast_test.cpp'
        srcs = [stage / 'bots/main/zero/src/core' / (name + '.cpp') for name in NAMES]
        exe, wasm = build / 'buy-fast.exe', build / 'buy-fast.wasm'
        compiler = ROOT / '.tools/toolchain/llvm-mingw-20261020-ucrt-x86_64/bin/clang++.exe'
        report['tools'] = {str(p): sha(p.read_bytes()) for p in [compiler, core.CLANG, core.NODE]}
        try:
            launch([compiler, '-std=c++20', '-O2', '-static', '-UNDEBUG', src, *srcs, '-o', exe], build / 'native-build', report['jobs'])
            native_value = json.loads(launch([exe], build / 'native-run', report['jobs']))
            write(build / 'native.json', native_value)
            if not native_value['pass']:
                raise RuntimeError('native parity failed')
            launch([core.CLANG, *wasi.FLAGS, src, *srcs, f'-L{wasi.EH}', '-lunwind', '-o', wasm], build / 'wasm-build', report['jobs'])
            wasm_value = json.loads(launch([core.NODE, '--disable-warning=ExperimentalWarning', ROOT / 'tools/arena/wasi-run.mjs', wasm], build / 'wasm-run', report['jobs']))
            write(build / 'wasm.json', wasm_value)
            if native_value != wasm_value:
                raise RuntimeError('native/WASM raw report differs')
            if any(sha((ROOT / name).read_bytes()) != value for name, value in manifest.items()):
                raise RuntimeError('owned source closure drifted during parity')
            if any(sha((ROOT / name).read_bytes()) != value for name, value in support.items()):
                raise RuntimeError('support epoch drifted during parity')
            report.update({'pass': True, 'native_wasm_report_equal': True, 'result': native_value,
                'all_jobs_closed': all(j['tree_exit_confirmed'] and j['containment']['empty_confirmed'] for j in report['jobs']),
                'binaries': {p.name: sha(p.read_bytes()) for p in [exe, wasm]}})
        except BaseException as error:
            report.update({'pass': False, 'failure': str(error), 'all_jobs_closed': not bool(native._PENDING_CLEANUP)})
            write(build / 'report.json', report)
            print(json.dumps({'pass': False, 'failure': str(error), 'report': str(build / 'report.json')}))
            return 1
        write(build / 'report.json', report)
        print(json.dumps({'pass': True, 'result': native_value, 'all_jobs_closed': report['all_jobs_closed'], 'report': str(build / 'report.json')}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
