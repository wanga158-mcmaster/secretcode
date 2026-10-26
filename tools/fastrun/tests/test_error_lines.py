"""Check the compiled production classifier against the CLI health parser.

Run with the pinned Python. Builds only a test executable under .cache, using
the arena shared Slot/private Windows Job helpers for both compile and execution.
"""
from __future__ import annotations

import json
from pathlib import Path
import shutil
import sys
import uuid

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/arena"))
import compiled_runner as native
import games


def main() -> int:
    owner = ROOT / ".cache/health-choice"
    owner.mkdir(exist_ok=True)
    toolchain = ROOT / ".tools/toolchain"
    compiler = toolchain / "llvm-mingw-20261020-ucrt-x86_64/bin/clang++.exe"
    wasmtime = toolchain / "wasmtime-v50.0.0-x86_64-mingw-c-api"
    cases = [
        b"", b"ok\n", b"p3 error: bad state\n", b"ERROR x\n",
        b"errors are fine\nmy_error\nterror\nerrorless\n",
        b'P3 {"kind":"summary","abs_duration_error":0,"abs_health_error":1,"abs_conversion_error":2}\n',
        b"P5 UNEXPECTED_ENGINE_ERROR\n", b"engine_error\n",
        b"engine_errors\nengine_error_code\n", b"my_engine_error\n",
        b"engine_error2\nerror_\n_error\n2error\nerror2\n",
        b"error:error ERROR\n", b"(error) [ErRoR]\n",
        b"\x00\x00ok\n\x00bot error: bad\x00\nERROR x\nerrors are fine\nmy_error\n",
        b"er\x00ror\n", b"error\rerror\r\nerror\n", b"error\vERROR\ferror",
        b"error", b"\n\nerror\n\n", b'P3 {"error":"bad"}\n',
    ]
    # tempfile.TemporaryDirectory requests mode 0700; on this Windows sandbox
    # that ACL can deny the caller access to its own newly created directory.
    # Inherit the workspace ACL instead, as the native validator does.
    scratch = owner / ("error-lines-" + uuid.uuid4().hex)
    scratch.mkdir()
    try:
        executable = scratch / "error-lines.exe"
        build_dir = scratch / "build"
        build_dir.mkdir()
        build = native._slotted_command(lambda _: [
            str(compiler), "-std=c++20", "-O2", "-Wall", "-Wextra",
            "-Wno-unused-parameter", "-Wno-unused-function", "-I" + str(wasmtime / "include"),
            str(Path(__file__).with_name("error-lines.cpp")), "-o", str(executable),
            "-static", str(wasmtime / "lib/libwasmtime.dll.a"),
        ], ROOT, build_dir, 120, jobs=1)
        if build.get("returncode") != 0 or not executable.is_file():
            print(json.dumps(build))
            print((build_dir / "stderr.bin").read_text(errors="replace"))
            return 1
        # The DLL is already part of the arena runner installation. Keep the
        # temporary executable beside a private copy for deterministic loading.
        shutil.copy2(ROOT / ".tools/fastrun/wasmtime.dll", scratch / "wasmtime.dll")
        fixtures = []
        for i, case in enumerate(cases):
            fixture = scratch / f"case-{i}.stdout"
            fixture.write_bytes(case)
            fixtures.append(fixture)
        run_dir = scratch / "run"
        run_dir.mkdir()
        run = native._slotted_command(lambda _: [str(executable), *map(str, fixtures)], ROOT, run_dir, 30, jobs=1)
        if run.get("returncode") != 0:
            print(json.dumps(run))
            return 1
        actual = list(map(int, (run_dir / "stdout.bin").read_text().splitlines()))
        expected = [games.count_errors(case) for case in cases]
        for i, (want, got) in enumerate(zip(expected, actual)):
            if want != got:
                print(f"case {i}: CLI {want}, fastrun {got}, stdout={cases[i]!r}")
        if actual != expected:
            return 1
        print(f"classifier parity: {len(cases)}/{len(cases)} cases")
    finally:
        shutil.rmtree(scratch)
    return 0


if __name__ == "__main__":
    sys.exit(main())
