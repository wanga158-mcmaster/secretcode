"""Pure legacy/compact ground-scene parity in private native and WASM closures.

No games, fitting, replay restoration or fuel claim. Use the pinned Python via
reviewed unsandboxed execution; all native work uses existing shared Slots and
private Windows Jobs. Tool-sensitive starts require the parent's hold release.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[4]
SUITE = ROOT / 'bots/main/zero'
OUT = ROOT / '.cache/main/zero/ground-scene'
FIXTURES = ROOT / '.cache/main/zero/micro-sdk/deferred-dash/fixtures.txt'
GENERATED = 'ground_scene_fixtures.h'
NAMES = ('map', 'catalog', 'moves', 'rules', 'cand', 'action', 'utility', 'net',
         'ground_scene', 'student_goal', 'student_state', 'student_action',
         'student_context', 'student_net')
sys.path.insert(0, str(ROOT / 'tools/arena'))
import core
import compiled_runner as native
import wasi


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
                    lines.append(f'f.obs.{kind}.push_back(unit({i},{{{x},{y}}}));')
                elif kind == 'camps':
                    lines.append(f'{{CampView c;c.id={i};c.at={{{x},{y}}};c.type=3;c.hp=100.0f;f.obs.camps.push_back(c);}}')
                else:
                    lines.append(f'f.obs.reserved_tiles.push_back({{{x},{y}}});')
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
                raise RuntimeError('source closure escaped owned zero suite')
            pending.append(header)
    result = {str(path.relative_to(ROOT)).replace('\\', '/'): data for path, data in found.items()}
    result['bots/main/zero/tools/' + GENERATED] = retained_header()
    return result


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
    report = {'schema': 'main.zero.ground_scene.parity.v1', 'content_sha256': identity,
              'source': manifest, 'support': support, 'jobs': [], 'games': 0,
              'scope': 'Pure facts and feature parity only; retained SDK-origin geometry has synthesized utility states. No real SDK execution, fuel, history, composition or strength claim.',
              'coverage': {'both_teams': True, 'states': 22, 'rule_scenarios': 8,
                  'goals': ['NoGoal', 'Farm', 'Bank', 'Recall', 'Engage', 'Assist', 'Avoid', 'invalid_Farm'],
                  'facts': ['ground contract masks/count/completeness', 'all ordered UtilityAction payloads/24float words',
                      'state48', 'camp context', 'full and family root masks', 'ordered student rows/status/24float words',
                      'nonzero attention/encoder/controller logits bitwise within each runtime', 'nonground delegation', 'stale map/Obs identity/revision',
                      'rebuild', 'invalid-map utility contract only'],
                  'limits': ['holes', 'reservations', 'unknown ally ward/reservation facts', 'TP unsupported',
                      'invalid radius', 'missing lock facts', 'dead/channel/stun/restrictions', 'off-map self',
                      'shuffled/duplicate altars', 'known empty/unavailable/partial rows']}}
    OUT.mkdir(parents=True, exist_ok=True)
    with core.FileLock(core.CACHE / 'snap' / f'{identity}.lock', timeout=60):
        if (build / 'report.json').is_file():
            prior = json.loads((build / 'report.json').read_text())
            if prior.get('pass'):
                print(json.dumps({'pass': True, 'reused_private_result': True, 'report': str(build / 'report.json')}))
                return 0
            raise RuntimeError('failed immutable result retained; inspect before retry')
        stage = build / 'source'
        for relative, body in data.items():
            target = stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
        write(build / 'sources.json', report)
        src = stage / 'bots/main/zero/tools/ground_scene.cpp'
        srcs = [stage / 'bots/main/zero/src/core' / (name + '.cpp') for name in NAMES]
        exe, wasm = build / 'ground-scene.exe', build / 'ground-scene.wasm'
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
                raise RuntimeError('native/WASM complete report differs')
            if any(sha((ROOT / p).read_bytes()) != value for p, value in manifest.items() if not p.endswith('/' + GENERATED)):
                raise RuntimeError('owned source drifted during parity')
            if any(sha((ROOT / p).read_bytes()) != value for p, value in support.items()):
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
