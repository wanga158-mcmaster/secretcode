"""Build and verify the actual C8 engine, including exact checkpoint continuation.

Uses the repository's existing arena slot pool only around compilation/checks.
All test outputs are fresh regenerable directories under .cache.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ENGINE = Path(__file__).resolve().parents[1]
ROOT = ENGINE.parents[3]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
import core  # noqa: E402


def run(command: list[str], cwd: Path, env: dict[str, str], log: Path) -> subprocess.CompletedProcess:
    with core.Slots(1):
        result = subprocess.run(command, cwd=cwd, env=env, text=True, encoding="utf-8",
                                errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=240)
    log.write_text(result.stdout, encoding="utf-8", newline="\n")
    if result.returncode:
        print("\n".join(result.stdout.splitlines()[-60:]), file=sys.stderr)
        raise RuntimeError(f"command failed ({result.returncode}), see {log}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--existing-exe", type=Path)
    args = parser.parse_args()
    compiler = None if args.existing_exe else args.compiler or shutil.which("g++")
    if not args.existing_exe and not compiler and Path("C:/msys64/ucrt64/bin/g++.exe").exists():
        compiler = "C:/msys64/ucrt64/bin/g++.exe"
    if not compiler and not args.existing_exe:
        raise RuntimeError("g++ is required, or pass --existing-exe for the campaign checks")
    base = ROOT / ".cache" / "contenders" / "optimization" / "c8" / "verification"
    base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=base))
    closure = [ENGINE / "src" / filename for filename in ("common.cpp", "sim.cpp", "c8.cpp")]
    closure += [ENGINE / "include" / "opt" / filename for filename in ("common.hpp", "sim.hpp", "scenarios.hpp")]
    closure += [ENGINE / "tests" / "c8.cpp", ENGINE / "examples" / "c8-smoke.json", ENGINE / "examples" / "c8.json"]
    closure += [ROOT / "docs" / "reference" / "runtime-catalog.json"]
    source_hashes = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                     for path in closure}
    env = dict(os.environ)
    if compiler:
        env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
        sources = [ENGINE / "src" / filename for filename in ("common.cpp", "sim.cpp", "c8.cpp")]
        objects = []
        for source in sources:
            target = work / (source.stem + ".o")
            run([compiler, "-std=c++20", "-O1", "-I", str(ENGINE / "include"), "-c", str(source),
                 "-o", str(target)], ROOT, env, work / (source.stem + "-build.log"))
            objects.append(str(target))
        unit = work / ("c8-tests.exe" if os.name == "nt" else "c8-tests")
        run([compiler, "-std=c++20", "-O1", "-I", str(ENGINE / "include"),
             str(ENGINE / "tests" / "c8.cpp"), *objects, "-o", str(unit)], ROOT, env, work / "unit-build.log")
        run([str(unit)], ROOT, env, work / "unit.log")
        driver = work / "main.cpp"
        driver.write_text('#include "opt/common.hpp"\n#include <iostream>\n'
                          'int main(int argc,char** argv){opt::install_signal_handlers();try{'
                          'return opt::run_c8(opt::parse_options(argc,argv));}'
                          'catch(const std::exception& e){std::cerr<<e.what()<<"\\n";return 1;}}\n',
                          encoding="utf-8", newline="\n")
        executable = work / ("c8-engine.exe" if os.name == "nt" else "c8-engine")
        run([compiler, "-std=c++20", "-O1", "-I", str(ENGINE / "include"), str(driver), *objects,
             "-o", str(executable)], ROOT, env, work / "driver-build.log")
    else:
        executable = args.existing_exe.resolve()
    config = ENGINE / "examples" / "c8-smoke.json"
    common = [str(executable), "c8", "--repo", str(ROOT), "--input", str(config), "--seed", "417"]
    full, split = work / "full", work / "split"
    run([*common, "--out", str(full), "--iterations", "4"], ROOT, env, work / "full.log")
    run([*common, "--out", str(split), "--iterations", "2"], ROOT, env, work / "split-first.log")
    run([*common, "--out", str(split), "--iterations", "4", "--resume"], ROOT, env, work / "split-resume.log")
    a = json.loads((full / "checkpoint.json").read_text(encoding="utf-8"))
    b = json.loads((split / "checkpoint.json").read_text(encoding="utf-8"))
    for field in ("identity", "iteration", "rng", "state"):
        if a[field] != b[field]:
            raise RuntimeError(f"resumed and uninterrupted C8 differ in {field}")
    report = json.loads((full / "report.json").read_text(encoding="utf-8"))
    if report["archive_cases"] < 1:
        raise RuntimeError("planted whole-cycle failure was not exported")
    hardest = json.loads((full / "hardest-case.json").read_text(encoding="utf-8"))
    if hardest["oracle"]["status"] != "success" or hardest["native_validation"]["native_validated"]:
        raise RuntimeError("wrong feasibility/native scope in exported curriculum")
    production = work / "production"
    run([str(executable), "c8", "--repo", str(ROOT), "--input", str(ENGINE / "examples" / "c8.json"),
         "--out", str(production), "--iterations", "1", "--seed", "417"], ROOT, env, work / "production.log")
    production_report = json.loads((production / "report.json").read_text(encoding="utf-8"))
    if production_report["iteration"] != 1 or production_report["evaluated_unique_cases"] != 1:
        raise RuntimeError("production input did not finish one real scenario comparison")
    changed_config = work / "changed.json"
    data = json.loads(config.read_text(encoding="utf-8"))
    data["baseline"] = "walk_first"
    changed_config.write_text(json.dumps(data), encoding="utf-8")
    with core.Slots(1):
        rejected = subprocess.run([str(executable), "c8", "--repo", str(ROOT), "--input", str(changed_config),
                                   "--out", str(split), "--iterations", "5", "--resume"],
                                  cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if rejected.returncode == 0:
        raise RuntimeError("changed semantic config was accepted on resume")
    for path in closure:
        key = str(path.relative_to(ROOT))
        if hashlib.sha256(path.read_bytes()).hexdigest() != source_hashes[key]:
            raise RuntimeError(f"source changed during verification: {key}; rerun on the settled sources")
    manifest = {"status": "passed", "work": str(work), "iteration": 4,
                "exact_resume_fields": ["identity", "iteration", "rng", "state"],
                "archive_cases": report["archive_cases"],
                "best_model_regret": report["best_model_regret"],
                "production_archive_cases": production_report["archive_cases"],
                "production_best_model_regret": production_report["best_model_regret"],
                "unit_checks_run": bool(compiler), "source_closure": source_hashes}
    (work / "verification.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
