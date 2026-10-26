"""C1 native-host executable tests: grammar, exact training restart, pending recovery, identity rejection.

Use the Python runtime from arena env. Search fitness here is an explicitly synthetic fixture.
"""
import argparse
from contextlib import nullcontext
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, default=Path("C:/msys64/ucrt64/bin/g++.exe"))
    parser.add_argument("--work", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[5]
    home = Path(__file__).resolve().parent.parent
    work = args.work or root / ".cache/contenders/optimization/c1" / ("tests-" + str(time.time_ns()))
    work.mkdir(parents=True, exist_ok=False)
    exe = work / ("c1-tests.exe" if os.name == "nt" else "c1-tests")
    env = dict(os.environ)
    env["PATH"] = str(args.compiler.parent) + os.pathsep + env.get("PATH", "")
    sys.path.insert(0, str(root / "tools/arena"))
    from core import Slots

    def run(command, expected=0):
        compiler_call = Path(command[0]).resolve() == args.compiler.resolve()
        with Slots(1, quiet=True) if compiler_call else nullcontext():
            result = subprocess.run([str(x) for x in command], cwd=root, env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf8")
        if result.returncode != expected:
            raise AssertionError(f"expected {expected}, got {result.returncode}: {command}\n{result.stdout[-6000:]}")
        return result.stdout

    run([args.compiler, "-std=c++20", "-O2", "-static", "-I", home / "include",
         home / "src/common.cpp", home / "src/c1.cpp", home / "tests/c1_tree.cpp", "-o", exe])
    run([exe, "--self-test"])
    run([exe, "--export-fixture", work / "export.hpp"])
    exported_source = work / "export-check.cpp"
    exported_source.write_text('#include "export.hpp"\n#include <cmath>\n'
                               'int main(){float x[12]{};x[1]=0.33333334f;'
                               'if(contender_program::factor(x)!=0.125f)return 1;'
                               'x[1]=std::nextafter(x[1],1.0f);'
                               'return contender_program::factor(x)==8.0f?0:2;}\n', encoding="utf8")
    export_exe = work / ("export-check.exe" if os.name == "nt" else "export-check")
    run([args.compiler, "-std=c++20", "-O2", "-static", exported_source, "-o", export_exe])
    run([export_exe])
    config = {"population_size": 8, "max_depth": 4,
              "training": {"cases": [{"map": "synthetic-map", "side": "A", "opponent": "synthetic-opponent"}]}}
    config_path = work / "config.json"
    config_path.write_text(json.dumps(config), encoding="utf8")
    fixture = home / "tests/c1_fixture.py"

    def campaign(name, iterations, resume=False, extras=(), expected=0, input_path=None):
        cmd = [exe, "c1", "--repo", root, "--input", input_path or config_path, "--out", work / name,
               "--seed", "9123", "--iterations", str(iterations), "--evaluator", sys.executable,
               "--evaluator-arg", str(fixture)]
        for extra in extras:
            cmd += ["--evaluator-arg=" + str(extra)]
        if resume:
            cmd += ["--resume"]
        run(cmd, expected)

    campaign("full", 12)
    campaign("split", 4)
    campaign("split", 12, resume=True)
    full = json.loads((work / "full/checkpoint.json").read_text(encoding="utf8"))
    split = json.loads((work / "split/checkpoint.json").read_text(encoding="utf8"))
    assert full["rng"] == split["rng"]
    assert full["state"] == split["state"], "split training differs from uninterrupted training"
    manifest = json.loads((work / "full/artifacts.json").read_text(encoding="utf8"))
    assert len(manifest) == 1 and manifest[0]["evaluation_role"] == "training"
    copied_artifact = work / "full" / manifest[0]["path"]
    assert hashlib.sha256(copied_artifact.read_bytes()).hexdigest() == manifest[0]["sha256"]
    exported = json.loads((work / "full/best_program.json").read_text(encoding="utf8"))
    assert exported["native_export"]["fixture"]["path"] == manifest[0]["path"]
    reproduction=json.loads((work/"full/reproduce_best.json").read_text())
    assert reproduction["controller"]=="frozen-controller@abc" and reproduction["baseline"]=="frozen-baseline@def"
    assert reproduction["expected_context_identity"]==full["state"]["environment_identities"]["training"]
    assert all(case["map_sha256"]==hashlib.sha256(case["map"].encode()).hexdigest() for case in reproduction["cases"])
    population=full["state"]["population"]
    assert len({json.dumps(parent["tree"],sort_keys=True) for parent in population})==len(population)
    assert 0<len(full["state"]["diversity_archive"])<=128

    # Summaries cannot conceal missing/foreign cases, altered fitness, or an
    # unhealthy opponent in the paired baseline arm.
    for fault in ("missing-case","duplicate-case","foreign-case","summary-fitness"):
        campaign("fault-"+fault,1,extras=("--fault",fault),expected=1)
    campaign("fault-raw-health",2,extras=("--fault","raw-health"))
    unhealthy=json.loads((work/"fault-raw-health/report.json").read_text())
    assert unhealthy["best"] is None
    assert all(not p["eligible"] and not p["raw_healthy"] for p in json.loads((work/"fault-raw-health/checkpoint.json").read_text())["state"]["population"])

    def configured(name,value):
        path=work/(name+".json");path.write_text(json.dumps(value),encoding="utf8");return path
    for name,extra in (("split-opposite-side",{"validation":{"cases":[{"map":"synthetic-map","side":"B","opponent":"different-opponent"}]}}),
        ("split-family",{"validation":{"maps":["family-sibling"]},"map_families":{"synthetic-map":"one-family","family-sibling":"one-family"}})):
        campaign(name,1,expected=1,input_path=configured(name,dict(config,**extra)))
    distinct=dict(config,validation={"maps":["validation-map"]})
    campaign("split-native-map-alias",1,expected=1,extras=("--fault","map-alias"),input_path=configured("native-map-alias",distinct))

    heldout_config=configured("nonempty-roles",dict(distinct,holdout={"maps":["holdout-map"]},validation_every=3,evaluate_holdout_on_finish=True))
    campaign("roles-full",12,input_path=heldout_config)
    campaign("roles-split",4,input_path=heldout_config)
    campaign("roles-split",12,resume=True,input_path=heldout_config)
    roles_full=json.loads((work/"roles-full/checkpoint.json").read_text())
    roles_split=json.loads((work/"roles-split/checkpoint.json").read_text())
    assert roles_full["rng"]==roles_split["rng"]
    for field in ("best","population","archive","diversity_archive"):
        assert roles_full["state"][field]==roles_split["state"][field],field
    for role in ("validation","holdout"):
        a=dict(roles_full["state"][role]);b=dict(roles_split["state"][role])
        a.pop("cache_hit");b.pop("cache_hit")
        assert a==b,role

    marker = work / "fail-once.txt"
    campaign("interrupted", 12, extras=("--fail-once", marker), expected=1)
    pending = json.loads((work / "interrupted/checkpoint.json").read_text(encoding="utf8"))
    assert pending["iteration"] == 0 and "pending" in pending["state"]
    campaign("interrupted", 12, resume=True, extras=("--fail-once", marker))
    recovered = json.loads((work / "interrupted/checkpoint.json").read_text(encoding="utf8"))
    assert recovered["rng"] == full["rng"] and recovered["state"] == full["state"]

    identity = work / "external-identity.txt"
    identity.write_text("one", encoding="utf8")
    campaign("identity", 2, extras=("--identity-file", identity))
    identity.write_text("two", encoding="utf8")
    campaign("identity", 4, resume=True, extras=("--identity-file", identity), expected=1)

    response_path = work / "split" / split["state"]["best"]["response_file"]
    response_path.write_text("{}", encoding="utf8")
    campaign("split", 13, resume=True, expected=1)
    copied_artifact.write_bytes(b"corrupt preserved artifact")
    campaign("full", 12, resume=True, expected=1)

    print("C1 tests passed: tree/float grammar, compiled export, 5000 bounded mutations/crossovers, exact restart and pending recovery, raw paired coverage/fitness/health guards, map/family/byte-alias split rejection, validation-present training invariance, diversity proxies, frozen reproduction and artifact integrity.")
    checks=["tree-float-bounded-evolution-compiled-export","exact-training-restart","pending-recovery","changed-identity",
        "artifact-corruption","frozen-reproduction","unique-population","diversity-proxies","missing-case","duplicate-case",
        "foreign-case","summary-fitness","raw-health","side-split-leakage","family-split-leakage","map-byte-alias",
        "validation-present-training-invariance"]
    (work/"verification.json").write_text(json.dumps(dict(passed=True,executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        native_game_evidence=False,checks=checks,groups=len(checks),iterations=12,population_size=len(population),scope="Synthetic algorithm/contract fixture; native evidence is separate"),indent=2)+"\n",encoding="utf8")
    print("Artifacts:", work)


if __name__ == "__main__":
    main()
