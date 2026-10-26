"""Focused tests of the specialists suite (no games): policy kernels, BC26T telemetry framing and
control-arm knobs.

    python bots/specialists/tests/run.py [bot ...]       (or ./arena.cmd test specialists[/<bot>])

Every bots/specialists/<bot>/tests/*.cpp is one self-contained fixture with its own main(). It is
compiled with the pinned WASI clang through tools/arena/wasi.py (C++20, -Wall -Wextra -Werror,
asserts enabled) and run under Node with only WASI imports. A case fails on a compile error
(static_assert), a non-zero exit (runtime assert), or, for the telemetry fixtures listed in
FRAMES, when its stdout breaks the bounded BC26T envelope the bots print. The control-knobs case
checks that the arena override core_enabled=false (the control arm) resolves exactly once in
each selected bot. One line per case; the exit status is non-zero on any failure (2 for an
unknown bot name).
"""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

SUITE = Path(__file__).resolve().parents[1]
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
import core  # noqa: E402
from core import CACHE, NODE  # noqa: E402
from wasi import DRIVER, compile_fixture, run_case  # noqa: E402

CONTROL_KNOB = "core_enabled"   # every bot's control arm: the commitment switched off

LOG_LINES = 15   # failure log lines shown per case
JOBS = 4         # fixtures compiled at once


@dataclass(frozen=True)
class Frames:
    """Expected stdout of a mocked-clock telemetry fixture: 2000 rounds through the bot's Telemetry."""
    actor: str                       # SDK champion name in "a"
    gap: bool                        # True: one summary reports turnGapCount 1; False: all report 0
    summary_keys: tuple[str, ...] = ()
    details: int = 32                # detail cap; one start frame and 62 summaries always
    summaries: int = 62


FRAMES = {
    "gremlins/gremlins-telemetry": Frames("varg", gap=True),
    "lighthouse/telemetry": Frames("vesper_vale", gap=False, summary_keys=("wardOpp", "wardUseful")),
}


def _int(value, expected=None) -> bool:
    return type(value) is int and (expected is None or value == expected)


def _scalar(value) -> bool:
    return value is None or isinstance(value, (str, bool, int, float))


def _reject_constant(name):
    raise ValueError(f"non-finite number {name}")


def check_frames(bot: str, out: bytes, spec: Frames) -> str | None:
    """None if `out` is a complete bounded BC26T stream, else the first problem found."""
    lines = out.split(b"\n")
    if lines[-1] != b"":
        return "output does not end with a newline"
    lines = lines[:-1]
    want = 1 + spec.details + spec.summaries
    if len(lines) != want:
        return f"{len(lines)} frames, expected {want} (1 start, {spec.details} detail, {spec.summaries} summary)"
    details = summaries = 0
    gaps = []
    for q, line in enumerate(lines, 1):
        if len(line) + 1 > 512:
            return f"frame {q}: {len(line) + 1} bytes > 512"
        if not line.startswith(b"BC26T{") or any(c < 0x20 or c > 0x7E for c in line):
            return f"frame {q}: not a printable-ASCII BC26T line"
        try:
            row = json.loads(line[5:].decode("ascii"), parse_constant=_reject_constant)
        except ValueError as exc:
            return f"frame {q}: bad JSON ({exc})"
        if not isinstance(row, dict) or not _int(row.get("s"), 1) or row.get("c") != bot \
                or row.get("a") != spec.actor or not _int(row.get("q"), q) or not _int(row.get("r")) \
                or not isinstance(row.get("v"), str) or not isinstance(row.get("k"), str):
            return f"frame {q}: bad envelope (s/v/c/a/r/q/k): {line[:120].decode('ascii')}"
        data = row.get("d")
        if not isinstance(data, dict) or not all(_scalar(v) for v in data.values()):
            return f"frame {q}: data is not a flat object of scalars"
        if row["k"] == "start":
            if q != 1:
                return f"frame {q}: start frame after the first frame"
        elif row["k"] == "summary":
            summaries += 1
            gaps.append(data.get("turnGapCount"))
            missing = [k for k in spec.summary_keys if k not in data]
            if missing:
                return f"frame {q}: summary lacks {', '.join(missing)}"
        else:
            details += 1
    if details != spec.details or summaries != spec.summaries:
        return f"{details} detail and {summaries} summary frames, expected {spec.details} and {spec.summaries}"
    if spec.gap and not any(_int(g, 1) for g in gaps):
        return "no summary reports the fixture's skipped owner turn (turnGapCount 1)"
    if not spec.gap and not all(_int(g, 0) for g in gaps):
        return "a summary reports a turn gap although every round ran"
    return None


def run_fixture(path: Path, out_dir: Path) -> tuple[bool, str]:
    # The suite launcher and WASI helpers leave admission to each fixture worker.
    with core.Slots(1, quiet=True):
        bot, key = path.parent.parent.name, f"{path.parent.parent.name}/{path.stem}"
        spec = FRAMES.get(key)
        if spec is None:
            return run_case(f"{bot}-{path.stem}", path, out_dir=out_dir)
        wasm = out_dir / f"{bot}-{path.stem}.wasm"
        ok, log = compile_fixture(path, wasm)
        if not ok:
            return False, "COMPILE FAILED\n" + log
        try:
            proc = subprocess.run([str(NODE), "--disable-warning=ExperimentalWarning", str(DRIVER), str(wasm)],
                                  capture_output=True, timeout=600)
        except subprocess.TimeoutExpired:
            return False, "timed out after 600s"
        if proc.returncode != 0:
            return False, f"exit {proc.returncode}\n" + (proc.stdout + proc.stderr).decode(errors="replace")[-4000:]
        error = check_frames(bot, proc.stdout, spec)
        if error:
            return False, error
        return True, f"{len(proc.stdout.splitlines())} bounded BC26T frames"


def missing_framing(key: str) -> tuple[bool, str]:
    return False, f"framing fixture {key}.cpp listed in FRAMES is missing (renamed or moved?)"


def check_knobs(bots: list[str]) -> tuple[bool, str]:
    """The control arm is built by the arena override core_enabled=false on a snapshot, which needs
    exactly one `constexpr ... core_enabled = ...;` in each bot's own closure."""
    bad = []
    for name in bots:
        try:
            core.version(core.Bot("specialists", name), {CONTROL_KNOB: "false"})
        except core.ArenaError as exc:
            bad.append(f"{name}: {exc}")
    if bad:
        return False, "\n".join(bad)
    return True, f"{CONTROL_KNOB}=false resolves once in {len(bots)} bot(s)"


def main(argv: list[str]) -> int:
    bots = sorted(p.name for p in SUITE.iterdir() if (p / "src" / "main.cpp").is_file())
    if any(a in ("-h", "--help") for a in argv):
        print(__doc__.strip())
        return 0
    wanted = [a.split("/", 1)[1] if a.startswith("specialists/") else a for a in argv]
    unknown = [a for a in wanted if a not in bots]
    if unknown:
        print(f"unknown bot(s): {' '.join(unknown)}; specialists bots: {' '.join(bots)}")
        return 2
    if not bots:
        print("no bots found under bots/specialists/")
        return 1
    chosen = list(dict.fromkeys(wanted)) or bots
    fixtures = [p for b in chosen for p in sorted((SUITE / b / "tests").glob("*.cpp"))]
    if not wanted and not fixtures:
        print("no fixtures found under bots/specialists/*/tests/")
        return 1
    for b in chosen:
        if not any(p.parent.parent.name == b for p in fixtures):
            print(f"--   {b}: no kernel fixture (control-knob check and build only)")
    failed = []
    (CACHE / "wasi").mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="specialists-", dir=CACHE / "wasi") as tmp, \
            ThreadPoolExecutor(max_workers=JOBS) as pool:
        jobs = [(f"{p.parent.parent.name}/{p.stem}", pool.submit(run_fixture, p, Path(tmp))) for p in fixtures]
        found = {name for name, _ in jobs}
        jobs += [(key, pool.submit(missing_framing, key)) for key in FRAMES
                 if key.split("/")[0] in chosen and key not in found]
        jobs.append(("control-knobs", pool.submit(check_knobs, chosen)))
        for name, future in jobs:
            try:
                ok, log = future.result()
            except Exception as exc:  # a harness error is a failed case, not a crash of the run
                ok, log = False, f"harness error: {exc!r}"
            lines = [l for l in log.strip().splitlines() if l.strip()]
            if not ok and not lines:
                lines = ["no output: non-zero exit status or trap"]
            if ok:
                print(f"ok   {name}" + (f"  ({lines[-1][:100]})" if lines else ""), flush=True)
            else:
                failed.append(name)
                print(f"FAIL {name}\n    " + "\n    ".join(lines[-LOG_LINES:]), flush=True)
    print(f"{len(jobs) - len(failed)}/{len(jobs)} passed" + (f"; failed: {' '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
