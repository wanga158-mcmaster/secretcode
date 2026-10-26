"""C6 executable checkpoint tests, using one shared arena slot per host process.

No native games are launched. All data is explicitly diagnostic. Pass --exe to
test an existing complete optimizer; otherwise compile the standalone C6 launcher.
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
import uuid

HERE = Path(__file__).resolve()
ROOT = next(p for p in HERE.parents if (p / "arena.cmd").is_file())
ENGINE = HERE.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "arena"))
import core  # noqa: E402


def run(argv, log, *, expected=0, env=None):
    with core.Slots(1, quiet=True), log.open("wb") as stream:
        result = subprocess.run([str(x) for x in argv], cwd=ROOT, env=env,
                                stdout=stream, stderr=subprocess.STDOUT, check=False)
    if (result.returncode == 0) != (expected == 0):
        raise RuntimeError(f"unexpected exit {result.returncode}: {log}\n{log.read_text(errors='replace')[-3000:]}")


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    args = parser.parse_args()
    output = ROOT / ".cache" / "contenders" / "optimization" / "c6-resume-tests" / uuid.uuid4().hex
    output.mkdir(parents=True)
    exe = args.exe.resolve() if args.exe else output / "c6-tests.exe"
    source_bindings = {}
    if not args.exe:
        compiler = os.environ.get("CXX") or shutil.which("g++") or r"C:\msys64\ucrt64\bin\g++.exe"
        env = dict(os.environ)
        env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
        frozen = output / "source"
        for relative in ("include/opt/common.hpp", "include/opt/learning.hpp", "src/common.cpp", "src/c6.cpp", "tests/c6.cpp"):
            body = (ENGINE / relative).read_bytes()
            destination = frozen / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(body)
            source_bindings[relative] = hashlib.sha256(body).hexdigest()
        run([compiler, "-std=c++20", "-O2", "-pthread", "-static", "-I", frozen / "include",
             frozen / "src/common.cpp", frozen / "src/c6.cpp", frozen / "tests/c6.cpp", "-o", exe],
            output / "build.log", env=env)
        run([exe], output / "self-test.log")
    config = ENGINE / "examples/c6-diagnostic.json"
    def fit(out, steps, resume=False, source=config):
        argv = [exe, "c6", "--input", source, "--out", out, "--iterations", str(steps), "--seed", "17"]
        if resume:
            argv.append("--resume")
        run(argv, output / f"{out.name}-{steps}.log")
    full, resumed = output / "full", output / "resumed"
    fit(full, 100)
    fit(resumed, 50)
    fit(resumed, 100, True)
    a, b = load(full / "checkpoint.json"), load(resumed / "checkpoint.json")
    for key in ("state", "rng", "iteration"):
        if key not in a or key not in b or a[key] != b[key]:
            raise AssertionError(f"C6 continuous/resumed {key} differs")
    for name in ("best-policy.json", "latest-policy.json", "value.json"):
        if (full / name).read_bytes() != (resumed / name).read_bytes():
            raise AssertionError(f"C6 resumed exported artifact differs: {name}")
    episodes = []
    for family in range(9):
        observations = []
        for decision in range(1, 7):
            features = [0.0] * 12
            features[0] = decision / 6
            observations.append({"features": features, "action": 1 if decision > 3 else 2,
                                 "actor": 0, "round": decision, "decision": decision,
                                 "legal_mask": [True] * 5})
        episodes.append({"trajectory_id": f"diagnostic-{family}", "family": f"family-{family}",
                         "source_key": "diagnostic/teacher", "engine": "diagnostic/v1",
                         "feature_schema": "contenders-actor-context/v1", "payoff": 1.0,
                         "subject_healthy": True, "opponent_healthy": True, "complete": True,
                         "logging_complete": True, "observations": observations})
    dataset = output / "mutable-diagnostic.json"
    dataset.write_text(json.dumps({"information_safe": True, "episodes": episodes}), encoding="utf-8")
    external = output / "external-config.json"
    external.write_text(json.dumps({"dataset": str(dataset), "validation_every": 5, "checkpoint_every": 5}), encoding="utf-8")
    fit(output / "external", 5, source=external)
    episodes[0]["payoff"] = -1.0
    dataset.write_text(json.dumps({"information_safe": True, "episodes": episodes}), encoding="utf-8")
    run([exe, "c6", "--input", external, "--out", output / "external", "--iterations", "10", "--seed", "17", "--resume"],
        output / "mutated-data-rejected.log", expected=1)
    rejected = (output / "mutated-data-rejected.log").read_text(errors="replace")
    if "dataset or whole-family split changed" not in rejected and "bound file identity changed" not in rejected:
        raise AssertionError("C6 rejected the mutation for an unexpected reason")
    artifact = output / "diagnostic.wasm"
    artifact.write_bytes(b"diagnostic artifact retention fixture; not game WASM")
    payload = output / "artifact-payload.json"
    payload.write_text(json.dumps({"episodes": episodes, "artifacts": [{"role": "diagnostic_fixture",
        "path": str(artifact), "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
        "size": artifact.stat().st_size}]}), encoding="utf-8")
    evaluator = output / "artifact-evaluator.py"
    evaluator.write_text('''import argparse, json
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("payload")
p.add_argument("--request")
p.add_argument("--response")
p.add_argument("--repo")
a=p.parse_args()
request=json.loads(Path(a.request).read_text())
assert request["controller"]=="diagnostic-controller"
response={"healthy":True,"environment_identity":"diagnostic-artifact-environment"}
if request["kind"]!="identity":
    response.update(json.loads(Path(a.payload).read_text()))
    if not request["collect_rollouts"]:
        strong=request["candidate"]["policy"]["stochastic"]
        response["cases"]=[dict(case,requested_opponent=case.get("opponent",""),
            complete=True,payoff=1.0,baseline_payoff=-1.0 if strong else 1.0)
            for case in request["cases"]]
Path(a.response).write_text(json.dumps(response))
''', encoding="utf-8")
    native_config = output / "artifact-config.json"
    native_config.write_text(json.dumps({"base": "diagnostic-controller", "cases": [{"map": "diagnostic"}],
        "validation_every":5, "checkpoint_every":5}), encoding="utf-8")
    native_out = output / "artifact-collection"
    def artifact_fit(steps, resume=False, expected=0, destination=native_out):
        argv = [exe, "c6", "--input", native_config, "--out", destination, "--iterations", str(steps),
                "--seed", "17", "--evaluator", sys.executable, "--evaluator-arg", evaluator,
                "--evaluator-arg", payload]
        if resume:
            argv.append("--resume")
        run(argv, output / f"artifact-fit-{steps}.log", expected=expected)
    artifact_fit(5)
    artifact_fit(10, True)
    retained = load(native_out / "teacher-artifacts.json")["artifacts"][0]
    if retained.get("path_base") != "run_output" or Path(retained["path"]).is_absolute():
        raise AssertionError("C6 retained artifact descriptor is not relative to run output")
    retained_path = native_out / retained["path"]
    if retained_path.read_bytes() != artifact.read_bytes() or retained_path.parent != native_out / "native-artifacts":
        raise AssertionError("C6 did not retain the declared native artifact")
    relocated = output / "artifact-relocated"
    shutil.copytree(native_out, relocated)
    native_out.rename(output / "artifact-original-parked")
    artifact_fit(15, True, destination=relocated)
    retained_path = relocated / retained["path"]
    retained_path.write_bytes(b"X" * retained_path.stat().st_size)
    artifact_fit(20, True, expected=1, destination=relocated)
    if "native artifact missing or changed" not in (output / "artifact-fit-20.log").read_text(errors="replace"):
        raise AssertionError("C6 artifact corruption did not fail digest verification")
    qualification_config = output / "qualification-config.json"
    qualification = {"base":"diagnostic-controller", "cases":[{"map":"diagnostic"}],
        "qualification_cases":[{"map":f"selection-{f}","side":side,"opponent":"diagnostic"}
                               for f in range(4) for side in ("A","B")],
        "validation_every":5, "checkpoint_every":5}
    qualification_config.write_text(json.dumps(qualification),encoding="utf-8")
    def qualify(name, config=qualification_config, resume=False, expected=0):
        argv=[exe,"c6","--input",config,"--out",output/name,"--iterations","10" if resume else "5",
              "--seed","17","--evaluator",sys.executable,"--evaluator-arg",evaluator,"--evaluator-arg",payload]
        if resume:argv.append("--resume")
        run(argv,output/f"{name}-{'resume' if resume else 'first'}.log",expected=expected)
    qualify("no-expert")
    qualify("no-expert",resume=True)
    selection=load(output/"no-expert/teacher-selection.json")
    if selection["qualified"] or any(x for row in selection["selected_policy"]["weights"] for x in row):
        raise AssertionError("C6 called a tied unqualified policy an expert")
    strong_policy=load(output/"no-expert/best-policy.json")
    strong_policy.update(seed=91,stochastic=True,max_decisions=256)
    strong_policy["weights"][1][12]=2.0
    qualification["teacher_candidates"]=[strong_policy]
    supplied_config=output/"supplied-qualification-config.json"
    supplied_config.write_text(json.dumps(qualification),encoding="utf-8")
    qualify("supplied-qualified",config=supplied_config)
    qualify("supplied-qualified",config=supplied_config,resume=True)
    selection=load(output/"supplied-qualified/teacher-selection.json")
    selected=load(output/"supplied-qualified/best-policy.json")
    if not selection["qualified"] or any(selected[k]!=strong_policy[k] for k in ("weights","seed","stochastic","max_decisions","trace")):
        raise AssertionError("C6 lost qualified teacher copy semantics")
    qualification["qualification_cases"][0]["map"]="diagnostic"
    leaking_config=output/"leaking-qualification-config.json"
    leaking_config.write_text(json.dumps(qualification),encoding="utf-8")
    qualify("leaking-qualification",config=leaking_config,expected=1)
    if "selection leaks" not in (output/"leaking-qualification-first.log").read_text(errors="replace"):
        raise AssertionError("C6 failed to reject teacher-selection family leakage")
    summary = {"schema": "c6-executable-checks/v1", "diagnostic_only": True,
               "native_games": 0, "continuous_vs_resumed": "state/RNG/exports exact", "dataset_mutation": "rejected",
               "native_artifact_fixture": "retained and checked across resume; copied output resumes with original path absent; same-size corruption rejected; controller forwarded",
               "teacher_qualification_fixture":"paired tie abstains; supplied candidate accepted and copied exactly; selection-family leakage rejected; selection resumes",
               "executable_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(), "directory": str(output)}
    summary["source_bindings"] = source_bindings
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
