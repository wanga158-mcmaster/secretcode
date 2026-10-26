"""Focused exported-request provenance check; synthetic protocol fixture, no native games."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import uuid

here = Path(__file__).resolve().parent
repo = here.parents[4]
sys.path.insert(0, str(repo / "tools/arena"))
from core import Slots

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--engine", type=Path, required=True)
args = parser.parse_args()
out = repo / ".cache/contenders/optimization/learning" / ("c5-export-pin-" + uuid.uuid4().hex)
out.mkdir(parents=True)
(out / "fixture-repo").mkdir()
config = json.loads((here.parent / "examples/c5-diagnostic.json").read_text(encoding="utf-8"))
config["heldout_cases"] = [{"map": "heldout-e", "side": "A", "opponent": "diagnostic/reference"},
                           {"map": "heldout-f", "side": "B", "opponent": "diagnostic/reference"}]
(out / "input.json").write_text(json.dumps(config), encoding="utf-8")
command = [str(args.engine.resolve()), "c5", "--repo", str(out / "fixture-repo"),
           "--input", str(out / "input.json"), "--out", str(out / "run"), "--iterations", "2", "--seed", "41",
           "--evaluator", sys.executable, "--evaluator-arg=-B", "--evaluator-arg=" + str(here / "c5_fixture_evaluator.py")]
with Slots(1, quiet=True):
    result = subprocess.run(command, cwd=repo, text=True, capture_output=True, timeout=120)
(out / "execution.log").write_text(result.stdout + result.stderr, encoding="utf-8")
if result.returncode:
    raise RuntimeError(result.stdout + result.stderr)
for stem in ("best", "heldout"):
    request = json.loads((out / "run" / (stem + "-reproduction-request.json")).read_text(encoding="utf-8"))
    response = json.loads((out / "run" / (stem + "-evaluation.json")).read_text(encoding="utf-8"))
    if request.get("expected_context_identity") != response["environment_identity"]:
        raise AssertionError(stem + " exported request omitted its actual evaluated environment pin")
summary = {"diagnostic_only": True, "native_games": 0, "best_and_heldout_export_pins": "match evaluated identity",
           "executable_sha256": hashlib.sha256(args.engine.read_bytes()).hexdigest(), "output": str(out)}
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
print(json.dumps(summary))
