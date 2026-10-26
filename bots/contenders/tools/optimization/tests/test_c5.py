"""Host-only C5 protocol/restart test. Its diagnostic labels are not native evidence."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import Slots

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--engine", type=Path, required=True)
p.add_argument("--out", type=Path)
a = p.parse_args()
output = a.out or ROOT / ".cache" / "contenders" / "optimization" / "learning" / ("c5-cli-"+str(time.time_ns()))
output.mkdir(parents=True, exist_ok=False)
fixture_repo = output / "fixture-repo"
fixture_repo.mkdir()
command = [str(a.engine.resolve()), "c5", "--input", str(HERE.parent / "examples" / "c5-diagnostic.json"),
           "--seed", "41", "--repo", str(fixture_repo), "--evaluator", sys.executable,
           "--evaluator-arg=-B", "--evaluator-arg="+str(HERE / "c5_fixture_evaluator.py")]

def run(name, iterations, resume=False):
    call = command + ["--out", str(output/name), "--iterations", str(iterations)]
    if resume:
        call.append("--resume")
    # This test only calls the mathematical fixture, never native games.
    with Slots(1, quiet=True):
        result = subprocess.run(call, cwd=ROOT, text=True, capture_output=True, timeout=120)
    if result.returncode:
        raise RuntimeError(result.stdout+result.stderr)

run("continuous", 4)
run("split", 2)
run("split", 4, True)
left = json.loads((output/"continuous"/"checkpoint.json").read_text(encoding="utf-8"))
right = json.loads((output/"split"/"checkpoint.json").read_text(encoding="utf-8"))
for key in ("state", "iteration", "rng"):
    if left[key] != right[key]:
        raise AssertionError("split-resume differs: "+key)
for name in ("best-policy.json", "best-policy.hpp", "population.json", "current-policy.json"):
    if (output/"continuous"/name).read_bytes() != (output/"split"/name).read_bytes():
        raise AssertionError("split-resume artifact differs: "+name)
report = json.loads((output/"continuous"/"report.json").read_text(encoding="utf-8"))
if report.get("result", report).get("updates", 0) != 4:
    raise AssertionError("diagnostic accepted no learning updates")
population = json.loads((output/"continuous"/"population.json").read_text(encoding="utf-8"))
if not any(member.get("admission") == "periodic_behavioral_diversity" for member in population):
    raise AssertionError("population froze when validation tied despite changed actor behavior")
if len(population) != 2 or not any(member.get("reference") for member in population) or max(member["iteration"] for member in population) != 4:
    raise AssertionError("bounded population pruning failed to retain its initial reference and latest actor")
if not (output/"continuous"/"initial-reference.json").is_file():
    raise AssertionError("C5 omitted its pretraining validation reference")

leaking = json.loads((HERE.parent/"examples"/"c5-diagnostic.json").read_text(encoding="utf-8"))
leaking["validation_cases"][0]["family"] = "maps/" + leaking["train_cases"][0]["map"].replace("-", "_").upper() + ".MAP26A"
leak_path = output / "leaking-config.json"
leak_path.write_text(json.dumps(leaking), encoding="utf-8")
leak_call = list(command)
leak_call[leak_call.index("--input")+1] = str(leak_path)
with Slots(1, quiet=True):
    rejection = subprocess.run(leak_call+["--out", str(output/"leak-rejection"), "--iterations", "1"],
                               cwd=ROOT, text=True, capture_output=True, timeout=120)
if rejection.returncode == 0 or "leakage" not in rejection.stderr:
    raise AssertionError("C5 failed to reject training/validation family leakage")

missing_baseline = json.loads((HERE.parent/"examples"/"c5-diagnostic.json").read_text(encoding="utf-8"))
missing_baseline["evaluation"]["diagnostic_omit_paired_baseline"] = True
missing_baseline["max_consecutive_rejections"] = 1
missing_path = output / "missing-baseline-config.json"
missing_path.write_text(json.dumps(missing_baseline), encoding="utf-8")
missing_call = list(command)
missing_call[missing_call.index("--input")+1] = str(missing_path)
with Slots(1, quiet=True):
    missing = subprocess.run(missing_call+["--out", str(output/"missing-baseline"), "--iterations", "1"],
                             cwd=ROOT, text=True, capture_output=True, timeout=120)
missing_report = json.loads((output/"missing-baseline"/"report.json").read_text(encoding="utf-8"))
if missing.returncode == 0 or missing_report.get("updates") != 0 or missing_report.get("rejected_batches") != 1:
    raise AssertionError("C5 invented a paired baseline when actual native provenance was absent")
print("C5 exact continuation, plateau population admission, initial reference, family leakage and missing paired-baseline rejection passed; diagnostic only")
print(output)
