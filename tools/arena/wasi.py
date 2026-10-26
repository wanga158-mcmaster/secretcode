"""Compile C++ unit fixtures to WASI with the pinned wasi-sdk clang and run them under Node.

Library use (suite tests/run.py):
    from wasi import run_cases
    sys.exit(run_cases([("model", HERE / "model-test.cpp")], includes=[SUITE / "common"]))
CLI:  python tools/arena/wasi.py FILE.cpp [-I DIR] [-D NAME[=V]] [--no-werror]
Fixtures get no game, file or network access (only wasi_snapshot_preview1 imports) and fail by
returning non-zero from main or by an uncaught exception.
"""
from __future__ import annotations

import argparse
import hashlib
import itertools
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from core import CACHE, CLANG, NODE, ROOT  # noqa: E402

DRIVER = Path(__file__).resolve().parent / "wasi-run.mjs"
EH = CLANG.parent.parent / "share/wasi-sysroot/lib/wasm32-wasip1/eh"
FLAGS = ["-std=c++20", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-fwasm-exceptions", "-mllvm",
         "-wasm-use-legacy-eh=false", "--no-wasm-opt", "-Wl,-z,stack-size=8388608"]


def compile_fixture(sources, out: Path, includes=(), defines=(), flags=(), werror=True) -> tuple[bool, str]:
    sources = [sources] if isinstance(sources, (str, Path)) else list(sources)
    cmd = [str(CLANG), *FLAGS, *(["-Werror"] if werror else []), *flags]
    cmd += [f"-I{Path(i)}" for i in includes] + [f"-I{ROOT / 'sdk' / 'cpp'}"]
    cmd += [f"-D{d}" for d in defines] + [str(s) for s in sources] + [f"-L{EH}", "-lunwind", "-o", str(out)]
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    return proc.returncode == 0, (proc.stdout + proc.stderr)[-4000:]


def run_wasm(wasm: Path, args=(), timeout: int = 600) -> tuple[bool, str]:
    try:
        proc = subprocess.run([str(NODE), "--disable-warning=ExperimentalWarning", str(DRIVER), str(wasm), *args],
                              capture_output=True, text=True, errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, f"timed out after {timeout}s"
    return proc.returncode == 0, (proc.stdout + proc.stderr).strip()[-4000:]


_RUNS = itertools.count(1)


def run_case(name: str, sources, includes=(), defines=(), flags=(), args=(), werror=True,
             out_dir: Path | None = None) -> tuple[bool, str]:
    """Compile and run one fixture. The binary name is unique per process and call, so concurrent
    runs of the same suite never overwrite each other's module; it is removed after a passing run
    (a failing run keeps it next to its siblings for debugging)."""
    srcs = [sources] if isinstance(sources, (str, Path)) else list(sources)
    key = "|".join(map(str, srcs + list(includes) + list(defines) + list(flags)))
    tag = hashlib.sha256(key.encode()).hexdigest()[:8]
    out_dir = Path(out_dir or CACHE / "wasi")
    out_dir.mkdir(parents=True, exist_ok=True)
    wasm = out_dir / f"{name}-{tag}-{os.getpid()}-{next(_RUNS)}.wasm"
    ok, log = compile_fixture(srcs, wasm, includes, defines, flags, werror)
    if not ok:
        return False, "COMPILE FAILED\n" + log
    ok, log = run_wasm(wasm, args)
    if ok:
        try:
            wasm.unlink(missing_ok=True)
        except OSError:
            pass
    return ok, log


def run_cases(cases, includes=(), defines=(), flags=(), werror=True, only=None) -> int:
    """cases: (name, sources) or (name, sources, {includes/defines/flags/args overrides}).
    Prints one line per case and returns a process exit status (0 = all passed)."""
    failed, ran = [], 0
    for case in cases:
        name, sources = case[0], case[1]
        extra = case[2] if len(case) > 2 else {}
        if only and name not in only:
            continue
        ran += 1
        ok, log = run_case(name, sources, extra.get("includes", includes), extra.get("defines", defines),
                           extra.get("flags", flags), extra.get("args", ()), extra.get("werror", werror))
        print(f"{'ok  ' if ok else 'FAIL'} {name}" + ("" if ok else "\n    " + log.replace("\n", "\n    ")))
        if not ok:
            failed.append(name)
    print(f"{ran - len(failed)}/{ran} passed")
    return 1 if failed or not ran else 0


def main() -> int:
    p = argparse.ArgumentParser(description="compile and run one C++ fixture under WASI")
    p.add_argument("sources", nargs="+")
    p.add_argument("-I", dest="includes", action="append", default=[])
    p.add_argument("-D", dest="defines", action="append", default=[])
    p.add_argument("--no-werror", action="store_true")
    a = p.parse_args()
    ok, log = run_case(Path(a.sources[0]).stem, a.sources, a.includes, a.defines, werror=not a.no_werror)
    print(log)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
