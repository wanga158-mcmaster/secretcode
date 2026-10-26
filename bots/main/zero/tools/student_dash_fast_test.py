"""Freeze and compare the complete old/new dash rows in native and WASM builds.

No games, training, replay restoration or fuel claim. All native processes use
the shared arena Slots and existing private Windows Job helper. Run with the
pinned Python from arena env through reviewed unsandboxed execution.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[4]
SUITE = ROOT / 'bots/main/zero'
OUT = ROOT / '.cache/main/zero/dash-fast'
FIXTURES = ROOT / '.cache/main/zero/micro-sdk/deferred-dash/fixtures.txt'
sys.path.insert(0, str(ROOT / 'tools/arena'))
import core
import compiled_runner as native
import wasi

NAMES = ('map', 'catalog', 'moves', 'rules', 'cand', 'action', 'student_goal',
         'student_state', 'student_action')
GENERATED = 'student_dash_fast_fixtures.h'

def sha(value):
    return hashlib.sha256(value).hexdigest()

def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')

def retained_header():
    values = iter(map(int, FIXTURES.read_text().split()))
    count = next(values)
    lines = ['std::vector<Fixture> retained_fixtures() {', 'std::vector<Fixture> out;']
    for _ in range(count):
        index, width, height, team, sx, sy, na, ne, nc, nr = [next(values) for _ in range(10)]
        flags = [next(values) for _ in range(width * height)]
        lines += ['{ Fixture f{' + str(index) + ',{},{} };', f'f.map.w={width};f.map.h={height};',
                  'f.map.flags={' + ','.join(map(str, flags)) + '};', f'f.obs.team={team};f.obs.self.at={{{sx},{sy}}};']
        for kind, size in [('allies', na), ('enemies', ne), ('camps', nc), ('reserved_tiles', nr)]:
            for i in range(size):
                x, y = next(values), next(values)
                if kind in ('allies', 'enemies'):
                    lines += [f'f.obs.{kind}.push_back(unit({i},{{{x},{y}}}));']
                elif kind == 'camps':
                    lines += [f'{{CampView c;c.id={i};c.at={{{x},{y}}};c.type=3;c.hp=100.0f;f.obs.camps.push_back(c);}}']
                else:
                    lines += [f'f.obs.reserved_tiles.push_back({{{x},{y}}});']
        lines += ['out.push_back(f); }']
    if next(values, None) is not None:
        raise RuntimeError('retained fixture trailing data')
    lines += ['return out;', '}']
    return ('\n'.join(lines) + '\n').encode()

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
            if name.decode() == GENERATED:
                continue
            header = (source.parent / name.decode()).resolve()
            if not header.is_relative_to(SUITE):
                raise RuntimeError('source closure escaped zero suite')
            pending.append(header)
    result = {str(path.relative_to(ROOT)).replace('\\', '/'): data for path, data in found.items()}
    result['bots/main/zero/tools/' + GENERATED] = retained_header()
    return result

def launch(argv, directory, records):
    directory.mkdir(parents=True)
    record = None
    try:
        record = native._slotted_command(lambda _: list(map(str, argv)), ROOT, directory, 120, jobs=1)
    finally:
        if native._PENDING_CLEANUP and not native.confirm_pending_cleanup():
            raise RuntimeError('private Job cleanup remains unconfirmed')
    records.append({'name': directory.name, **record})
    stdout = (directory / 'stdout.bin').read_text(encoding='utf-8', errors='replace')
    stderr = (directory / 'stderr.bin').read_text(encoding='utf-8', errors='replace')
    if record['returncode'] or record['timed_out'] or not record['tree_exit_confirmed']:
        raise RuntimeError(f'{directory.name} failed: {stderr[-3000:]} {stdout[-500:]}')
    return stdout

def main():
    data = closure()
    manifest = {name: sha(body) for name, body in data.items()}
    support_paths = [ROOT / 'tools/arena/core.py', ROOT / 'tools/arena/compiled_runner.py',
                     ROOT / 'tools/arena/wasi.py', ROOT / 'tools/arena/wasi-run.mjs', FIXTURES,
                     ROOT / 'docs/reference/runtime-catalog.json', ROOT / 'docs/reference/current-version.json']
    support = {str(p.relative_to(ROOT)).replace('\\', '/'): sha(p.read_bytes()) for p in support_paths}
    identity = sha(json.dumps({'source': manifest, 'support': support}, sort_keys=True).encode())
    build = OUT / identity[:20]
    report = {'schema': 'main.zero.student.dash_fast.parity.v1', 'content_sha256': identity,
              'source': manifest, 'support': support, 'jobs': [], 'games': 0,
              'scope': 'Raw argument payload/order/status/all 24 float bits only; no fuel or strength claim.',
              'coverage': {'command_facts': [False, True], 'both_teams': True,
                           'cache_modes': ['deferred', 'known', 'reverse_duplicate', 'wrong_range', 'known_empty',
                                           'permissive_occupied_terrain_offmap_duplicate', 'self_and_outofrange', 'unknown_bogus'],
                           'goals': ['NoGoal', 'Farm', 'Bank', 'Recall', 'Engage', 'Assist', 'Avoid', 'Hold', 'None',
                                     'invalid_Farm', 'invalid_Bank', 'missing_Engage', 'missing_Assist', 'negative_NoGoal'],
                           'states': ['ready', 'dead', 'cannot_cast', 'cooldown', 'slot_not_ready', 'unknown_cooldown'],
                           'generator_ranges': [-1, 0, 1, 2, 3, 4, 5],
                           'generator_controls': 'Retained plus corners/edges/outside sources, blocked source terrain, thin maps, blocker rings, coincident blockers, all81 isolated cells,128 deterministic mixed windows; both teams/modes/alive/cast states.',
                           'delegation': 'Every catalog non-dash slot and all non-cast types, a_count and 255; known/unknown utility.'}}
    OUT.mkdir(parents=True, exist_ok=True)
    with core.FileLock(core.CACHE / 'snap' / f'{identity}.lock', timeout=60):
        if (build / 'report.json').is_file():
            prior = json.loads((build / 'report.json').read_text())
            if prior.get('pass'):
                print(json.dumps({'pass': True, 'reused': True, 'report': str(build / 'report.json')}))
                return 0
            raise RuntimeError('existing failed audit retained; inspect before retry')
        stage = build / 'source'
        for relative, body in data.items():
            target = stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
        write(build / 'sources.json', report)
        src = stage / 'bots/main/zero/tools/student_dash_fast_test.cpp'
        srcs = [stage / 'bots/main/zero/src/core' / (name + '.cpp') for name in NAMES]
        exe, wasm = build / 'dash-fast.exe', build / 'dash-fast.wasm'
        compiler = ROOT / '.tools/toolchain/llvm-mingw-20261020-ucrt-x86_64/bin/clang++.exe'
        report['tools'] = {str(p): sha(p.read_bytes()) for p in [compiler, core.CLANG, core.NODE]}
        try:
            launch([compiler, '-std=c++20', '-O2', '-static', '-UNDEBUG', src, *srcs, '-o', exe], build / 'native-build', report['jobs'])
            native_value = json.loads(launch([exe], build / 'native-run', report['jobs']))
            write(build / 'native.json', native_value)
            if not native_value['pass']:
                raise RuntimeError('native raw row parity failed')
            launch([core.CLANG, *wasi.FLAGS, src, *srcs, f'-L{wasi.EH}', '-lunwind', '-o', wasm], build / 'wasm-build', report['jobs'])
            wasm_value = json.loads(launch([core.NODE, '--disable-warning=ExperimentalWarning', ROOT / 'tools/arena/wasi-run.mjs', wasm], build / 'wasm-run', report['jobs']))
            write(build / 'wasm.json', wasm_value)
            if native_value != wasm_value:
                raise RuntimeError('native/WASM report differs')
            if any(sha((ROOT / path).read_bytes()) != value for path, value in manifest.items() if not path.endswith('/' + GENERATED)):
                raise RuntimeError('source closure drifted during audit')
            if any(sha((ROOT / path).read_bytes()) != value for path, value in support.items()):
                raise RuntimeError('support closure drifted during audit')
            report.update({'pass': True, 'native_wasm_report_equal': True, 'result': native_value,
                           'all_jobs_closed': all(job['tree_exit_confirmed'] and job['containment']['empty_confirmed'] for job in report['jobs']),
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
