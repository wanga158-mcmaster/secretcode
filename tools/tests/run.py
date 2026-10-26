"""Tests of the replay and map tools: python tools/tests/run.py [NAME ...] [--arena]

Runs every tools/tests/test-*.mjs (node --test) and test-*.py (unittest), plus the
personal camp oracle's focused tests in tools/replay; --arena also runs
tools/arena/tests/test_*.py. NAME filters test files by substring. Offline: no game, browser or
network; tests that need the CLI's cached official engine skip when it is absent.
Exit status 0 when every selected file passes.
"""
from __future__ import annotations

import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import NODE  # noqa: E402

COUNT = re.compile(r"^(?:\S+\s+)?(tests|pass|fail|skipped)\s+(\d+)\s*$", re.M)  # spec ("i pass 3") or TAP ("# pass 3")


def _say(text: str) -> None:
    enc = sys.stdout.encoding or "ascii"
    print(text.encode(enc, "replace").decode(enc, "replace"), flush=True)


def summary(test: Path, output: str) -> str:
    if test.suffix == ".mjs":
        counts = dict((k, int(v)) for k, v in COUNT.findall(output))
        parts = [f"{counts.get('pass', 0)} passed"] + [f"{counts[k]} {k}" for k in ("fail", "skipped") if counts.get(k)]
        return ", ".join(parts)
    ran = re.search(r"^Ran (\d+) tests?", output, re.M)
    skipped = re.search(r"skipped=(\d+)", output)
    return (f"{ran.group(1) if ran else '?'} ran" + (f", {skipped.group(1)} skipped" if skipped else ""))


def main(argv: list[str]) -> int:
    names = [a for a in argv if not a.startswith("--")]
    tests = sorted(HERE.glob("test-*.mjs")) + sorted(HERE.glob("test-*.py"))
    tests += [ROOT / "tools/replay/test-personal-camp-oracle.mjs",
              ROOT / "tools/replay/test-personal-camp-oracle.py"]
    if "--arena" in argv:
        tests += sorted((ROOT / "tools" / "arena" / "tests").glob("test_*.py"))
    if names:
        tests = [t for t in tests if any(n in t.name for n in names)]
    failed = []
    for test in tests:
        if test.suffix == ".mjs":
            cmd = [str(NODE), "--disable-warning=ExperimentalWarning", "--test", str(test)]
        else:
            cmd = [sys.executable, "-B", str(test)]
        start = time.time()
        try:
            proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, encoding="utf-8", errors="replace", timeout=900)
            code, output = proc.returncode, (proc.stdout or "") + (proc.stderr or "")
        except subprocess.TimeoutExpired:
            code, output = 1, "timed out after 900 s"
        rel = test.relative_to(ROOT).as_posix()
        _say(f"{'ok  ' if code == 0 else 'FAIL'} {rel} ({summary(test, output)}; {time.time() - start:.1f} s)")
        if code:
            failed.append(rel)
            tail = [l for l in output.strip().splitlines() if l.strip()][-30:]
            _say("    " + "\n    ".join(tail))
    _say(f"{len(tests) - len(failed)}/{len(tests)} test files passed" + (f"; failed: {', '.join(failed)}" if failed else ""))
    return 1 if failed or not tests else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
