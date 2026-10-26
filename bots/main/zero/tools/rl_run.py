"""Game runner for rl.py: arena's fastrun transport (shared slot pool, private Job) with per-game stdout.

Same runner as tools/arena/exp.py run_games (it reuses exp.native_chunk, exp.fastrun, exp.engine_wasm,
exp.read_rows); the only differences are --bundles into a scratch directory and a larger --log-limit so the
decision log (`Z ...` lines) of every champion survives. Each game's bundle is handed to a callback and then
deleted (replays are never kept: games are deterministic and regenerable).
"""
from __future__ import annotations

import shutil
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools' / 'arena'))
import core  # noqa: E402
import exp  # noqa: E402

SNAP = ROOT / '.cache/main/snap'
TEACHER = ROOT / '.cache/main/teacher/fastrun-teacher.exe'
OFFICIAL = exp.FASTRUN


def use_runner(fuel_scale: float) -> None:
    """fuel_scale > 0: the private teacher runner (fastrun with every turn budget x FASTRUN_FUEL_SCALE) for
    training an unconstrained teacher; 0: the official-budget fastrun."""
    import os
    if fuel_scale and fuel_scale != 1:
        if not TEACHER.is_file():
            raise SystemExit(f'missing teacher runner {TEACHER}')
        exp.FASTRUN = TEACHER
        os.environ['FASTRUN_FUEL_SCALE'] = f'{fuel_scale:g}'
    else:
        exp.FASTRUN = OFFICIAL
        os.environ.pop('FASTRUN_FUEL_SCALE', None)


def source_hash(path: Path) -> str:
    import hashlib
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(name: str, overrides: dict | None = None) -> str:
    e = core.prepare(core.bot(name), dict(overrides or {}), snapshot_root=SNAP, snapshot_owner='main')
    if not e.ok:
        raise SystemExit(f'build failed {name} {overrides}: {e.log[-1500:]}')
    return exp.wasm(e)


def health(row: dict, a: int) -> dict:
    return {k: row[k][a] for k in exp.HEALTH if row[k][a]}


def run_logged(games: list[str], bots: dict[str, str], work: Path, on_game, *, chunk: int = 96,
               log_limit: int = 64 << 20, want_stdout: bool = True) -> list[dict]:
    """Play game lines `MAP SIDE KEY_A W_A KEY_B W_B TAG` (unique TAGs). For each finished game calls
    on_game(row, stdout_a: bytes, stdout_b: bytes) (team a = the KEY_A bot) before its bundle is deleted."""
    engine = exp.engine_wasm()
    work.mkdir(parents=True, exist_ok=True)
    rows: list[dict] = []
    for c in range(0, len(games), chunk):
        part = games[c:c + chunk]
        token = uuid.uuid4().hex[:8]
        listing = work / f'games-{token}.txt'
        out = work / f'rows-{token}.jsonl'
        bundles = work / f'bundles-{token}'
        listing.write_text('\n'.join(part) + '\n', encoding='utf-8')
        argv = [str(exp.fastrun()), '--engine', str(engine), '--games', str(listing), '--out', str(out),
                '--log-limit', str(log_limit)]
        if want_stdout:
            argv += ['--bundles', str(bundles)]
        for key, path in bots.items():
            argv += ['--bot', f'{key}={path}']
        wanted = min(core.slot_count(), len(part))
        minimum = 1 if len(part) <= 4 else min(4, wanted)
        transport = work / '.native-commands' / uuid.uuid4().hex
        transport.mkdir(parents=True)
        try:
            process = exp.native_chunk(argv, transport, wanted, minimum, .05 if len(part) <= 4 else None)
            failed = (process.get('timed_out') is not False or process.get('forced_termination') or
                      type(process.get('returncode')) is not int or process['returncode'] != 0)
            got = exp.read_rows(out) if (not failed and out.is_file()) else []
            if failed or len(got) != len(part):
                stderr = transport / 'stderr.bin'
                detail = stderr.read_text(encoding='utf-8', errors='replace')[-1500:] if stderr.is_file() else ''
                raise RuntimeError(f'fastrun failed ({process.get("returncode")}, {len(got)}/{len(part)} rows): {detail}')
            for row in got:
                sa = sb = b''
                if want_stdout:
                    d = bundles / f"{row['map']}_{row['a_side']}_{row['tag']}"
                    fa, fb = d / 'team-a.stdout', d / 'team-b.stdout'
                    sa = fa.read_bytes() if fa.is_file() else b''
                    sb = fb.read_bytes() if fb.is_file() else b''
                    shutil.rmtree(d, ignore_errors=True)
                on_game(row, sa, sb)
            rows += got
        finally:
            listing.unlink(missing_ok=True)
            out.unlink(missing_ok=True)
            shutil.rmtree(bundles, ignore_errors=True)
            shutil.rmtree(transport, ignore_errors=True)
    return rows
