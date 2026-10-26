"""Freeze and audit synthetic complete production MOVE inputs, native and WASM.

No games, replay, online operation, training or policy changes. Native operations
use the existing arena global Slots/content lock/private Windows Job helper.
Run with the pinned Python reported by `arena env`, outside sandboxed Ninja.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[4]
SUITE = ROOT / 'bots/main/zero'
OUT = ROOT / '.cache/main/zero/student-alias'
sys.path.insert(0, str(ROOT / 'tools/arena'))
import core
import compiled_runner as native
import wasi

NAMES = ('map', 'catalog', 'moves', 'rules', 'cand', 'action', 'student_goal',
         'student_state', 'student_action', 'student_context', 'student_move_exposure')
ORIGINAL_RAW_SHA256 = 'f6944f8c182973cd9362f56916b750a7c7a0a59c19036dc9a5c395c8218196a5'

def sha(data):
    return hashlib.sha256(data).hexdigest()

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
    record = None
    try:
        record = native._slotted_command(lambda _: list(map(str, argv)), ROOT, directory, 120, jobs=1)
    finally:
        # Existing helper retains handles/slots after uncertain cleanup; never silently release.
        if native._PENDING_CLEANUP and not native.confirm_pending_cleanup():
            raise RuntimeError('owned private Job cleanup remains unconfirmed')
    records.append({'name': directory.name, **record})
    stdout = (directory / 'stdout.bin').read_text(encoding='utf-8', errors='replace')
    stderr = (directory / 'stderr.bin').read_text(encoding='utf-8', errors='replace')
    if record['returncode'] or not record['tree_exit_confirmed'] or record['timed_out']:
        raise RuntimeError(f'{directory.name} failed: {stderr[-2500:]} {stdout[-500:]}')
    return stdout

def main():
    data = closure()
    manifest = {name: sha(body) for name, body in data.items()}
    support_paths = [ROOT / 'tools/arena/core.py', ROOT / 'tools/arena/compiled_runner.py',
                     ROOT / 'tools/arena/wasi.py', ROOT / 'tools/arena/wasi-run.mjs',
                     ROOT / 'docs/reference/runtime-catalog.json', ROOT / 'docs/reference/current-version.json']
    support = {str(p.relative_to(ROOT)).replace('\\', '/'): sha(p.read_bytes()) for p in support_paths}
    identity = sha(json.dumps({'source': manifest, 'support': support}, sort_keys=True).encode())
    build = OUT / identity[:20]
    report = {'schema': 'main.zero.student.alias.build.v1', 'content_sha256': identity,
              'source': manifest, 'support': support, 'jobs': [], 'games': 0,
              'scope': 'Synthetic raw representation only; no natural reachability, engine damage, optimal action or fuel claim.'}
    OUT.mkdir(parents=True, exist_ok=True)
    with core.FileLock(core.CACHE / 'snap' / f'{identity}.lock', timeout=60):
        if (build / 'report.json').is_file():
            prior = json.loads((build / 'report.json').read_text())
            if prior.get('pass'):
                print(json.dumps({'pass': True, 'reused': True, 'report': str(build / 'report.json')}))
                return 0
            raise RuntimeError('existing failed audit retained; inspect before another attempt')
        stage = build / 'source'
        for relative, body in data.items():
            target = stage / relative; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(body)
        write(build / 'sources.json', report)
        src = stage / 'bots/main/zero/tools/student_alias.cpp'
        srcs = [stage / 'bots/main/zero/src/core' / (name + '.cpp') for name in NAMES]
        exe, wasm = build / 'student-alias.exe', build / 'student-alias.wasm'
        compiler = ROOT / '.tools/toolchain/llvm-mingw-20261020-ucrt-x86_64/bin/clang++.exe'
        report['tools'] = {str(path): sha(path.read_bytes()) for path in [compiler, core.CLANG, core.NODE]}
        try:
            launch([compiler, '-std=c++20', '-O2', '-static', '-UNDEBUG', src, *srcs, '-o', exe], build / 'native-build', report['jobs'])
            native_text = launch([exe], build / 'native-run', report['jobs'])
            native_value = json.loads(native_text)
            write(build / 'native.json', native_value)
            # A disproved alias is terminal; do not proceed to further probes.
            if not all(native_value['comparisons'].values()):
                raise RuntimeError('native complete raw input alias disproved')
            raw_hashes = {name: sha(json.dumps(row['raw_u32'], separators=(',', ':')).encode()) for name, row in native_value['fixtures'].items()}
            if native_value['counts']['raw_words'] != 301 or any(value != ORIGINAL_RAW_SHA256 for value in raw_hashes.values()):
                raise RuntimeError('original frozen 301-word alias input changed')
            prototype = native_value['prototype']
            if not prototype['all_controls_pass'] or not all(prototype['comparisons'][key] for key in ('reflection_augmented_differs', 'strength_augmented_differs')):
                raise RuntimeError('factual sidecar failed its bounded controls')
            launch([core.CLANG, *wasi.FLAGS, src, *srcs, f'-L{wasi.EH}', '-lunwind', '-o', wasm], build / 'wasm-build', report['jobs'])
            wasm_text = launch([core.NODE, '--disable-warning=ExperimentalWarning', ROOT / 'tools/arena/wasi-run.mjs', wasm], build / 'wasm-run', report['jobs'])
            wasm_value = json.loads(wasm_text)
            write(build / 'wasm.json', wasm_value)
            if native_value != wasm_value:
                raise RuntimeError('native/WASM complete report differs')
            if any(sha((ROOT / path).read_bytes()) != value for path, value in manifest.items()):
                raise RuntimeError('production source drifted while audit ran')
            if any(sha((ROOT / path).read_bytes()) != value for path, value in support.items()):
                raise RuntimeError('support source drifted while audit ran')
            report.update({'pass': True, 'native_wasm_report_equal': True,
                           'all_jobs_closed': all(job['tree_exit_confirmed'] and job['containment']['empty_confirmed'] for job in report['jobs']),
                           'counts': native_value['counts'], 'comparisons': native_value['comparisons'],
                           'range_witness': native_value['range_witness'],
                           'prototype': {key: prototype[key] for key in ('kind', 'scope', 'facts', 'appended_fact_values', 'appended_u32_words', 'augmented_u32_words', 'comparisons', 'order_and_input_bits_preserved', 'reversed_complete_row_order_preserved', 'invalid_input_controls', 'invalid_inputs_rejected', 'all_controls_pass')},
                           'prototype_controls': [{'name': row['name'], 'pass': row['pass'], 'east_destination_expected': row['east_destination_expected'], 'visible_enemy_rows': row['visible_enemy_rows'], 'visible_camp_rows': row['visible_camp_rows']} for row in prototype['controls']],
                           'original_301_words_unchanged': True,
                           'binaries': {path.name: sha(path.read_bytes()) for path in [exe, wasm]},
                           'fixture_raw_sha256': raw_hashes})
        except BaseException as error:
            report.update({'pass': False, 'failure': str(error), 'all_jobs_closed': not bool(native._PENDING_CLEANUP)})
            write(build / 'report.json', report)
            print(json.dumps({'pass': False, 'failure': str(error), 'report': str(build / 'report.json')}))
            return 1
        write(build / 'report.json', report)
        print(json.dumps({key: report[key] for key in ['pass', 'counts', 'comparisons', 'range_witness', 'all_jobs_closed']}))
        print(json.dumps({key: report[key] for key in ['original_301_words_unchanged', 'prototype', 'prototype_controls']}))
        print(json.dumps({'report': str(build / 'report.json'), 'content_sha256': identity}))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
