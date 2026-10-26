"""Run the existing specialist fixtures against Fireflies' exact applied closure.

Example: arena's Python runtime run-variant.py --set FIREFLIES_BOUNDED_STARTUP=1
  --set FIREFLIES_CONNECTED_CATALOG=1 --set FIREFLIES_CAPTURE_DEFENSE=1 --out RECORD

This runs the ordinary specialists runner with its suite source root redirected
to a disposable applied-source tree. The fixtures include that tree's profile
and assert the requested values before testing the same production helpers.
The exact repaired variant additionally runs the bounded actual-caller native
fork fixtures. Their declared world/fuel interventions are recorded separately
from autonomous native games and never imply physical distinction by themselves.
"""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import re
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools/arena"))
import core
import population


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--set", action="append", default=[], dest="sets")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    overrides = core.parse_sets(args.sets)
    output = Path(args.out).resolve()
    allowed = [ROOT / ".cache/arena/orchestration/fireflies-startup", ROOT / "results/population/fireflies-startup"]
    if not any(output.is_relative_to(path.resolve()) for path in allowed):
        raise core.ArenaError("variant test output is outside the assigned Fireflies evidence paths")
    bot = core.bot("specialists/fireflies")
    contents = core.read_closure(bot)
    applied = core.apply_overrides(bot, contents, overrides) if overrides else contents
    version = core.version_of(applied, overrides)
    contract = next(c for c in population.contracts(bot.id) if (c.get("overrides") or {}) == overrides)
    identity = population.identity(contract)
    if identity["snapshot"] != f"{bot.id}@{version}":
        raise core.ArenaError("applied test closure does not match the contract snapshot")
    scratch = ROOT / ".cache/arena/orchestration/fireflies-startup/variant-tests"
    scratch.mkdir(parents=True, exist_ok=True)
    fixture_pins = {}
    log = io.StringIO()
    with tempfile.TemporaryDirectory(prefix=version + "-", dir=scratch) as temporary:
        working = Path(temporary).resolve()
        if not working.is_relative_to(scratch.resolve()):
            raise core.ArenaError("temporary source tree escaped its assigned cache")
        for source, data in applied.items():
            destination = working / source.relative_to(ROOT)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        fixture_dir = working / "bots/specialists/fireflies/tests"
        fixture_dir.mkdir(parents=True)
        assertions = '#include "../src/profile.h"\n'
        for name, value in sorted(overrides.items()):
            symbol = name if name.startswith("FIREFLIES_") else "profile::" + name
            assertions += f"static_assert({symbol} == ({value}), \"applied variant mismatch\");\n"
        for fixture in sorted((bot.path / "tests").glob("*.cpp")):
            raw = fixture.read_bytes()
            generated = assertions.encode() + raw
            (fixture_dir / fixture.name).write_bytes(generated)
            fixture_pins[fixture.relative_to(ROOT).as_posix()] = {"original_sha256": sha(raw), "applied_test_sha256": sha(generated)}
        runner_path = ROOT / "bots/specialists/tests/run.py"
        spec = importlib.util.spec_from_file_location("fireflies_applied_variant_runner", runner_path)
        runner = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = runner
        spec.loader.exec_module(runner)
        runner.SUITE = working / "bots/specialists"
        with contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
            code = runner.main(["fireflies"])
    totals = re.findall(r"(?m)^(\d+)/(\d+) passed", log.getvalue())
    if len(totals) != 1 or (code == 0 and totals[0][0] != totals[0][1]):
        raise core.ArenaError("production test runner did not report an unambiguous case count")
    ordinary_cases = int(totals[0][1])
    caller = None
    repaired = {"FIREFLIES_BOUNDED_STARTUP": "1", "FIREFLIES_CONNECTED_CATALOG": "1", "FIREFLIES_CAPTURE_DEFENSE": "1"}
    if code == 0 and overrides == repaired:
        caller_output = ROOT / "results/population/fireflies-caller/test-runs" / uuid.uuid4().hex / "suite.json"
        caller_command = [sys.executable, "-B", str(bot.path / "tests/run-caller.py"), "--out", str(caller_output)]
        result = subprocess.run(caller_command, cwd=ROOT, capture_output=True, text=True)
        log.write(result.stdout)
        log.write(result.stderr)
        code = result.returncode
        if code == 0:
            caller_record = json.loads(caller_output.read_text(encoding="utf-8"))
            if caller_record.get("exit_code") != 0 or not isinstance(caller_record.get("test_cases"), int) or caller_record["test_cases"] <= 0:
                raise core.ArenaError("caller runner did not report complete positive test coverage")
            caller = {"artifact": {"path": caller_output.relative_to(ROOT).as_posix(), "sha256": sha(caller_output.read_bytes())},
                      "test_cases": caller_record["test_cases"], "command": caller_command,
                      "snapshots": caller_record["snapshots"], "distinct": caller_record["distinct"]}
    if population.identity(contract) != identity:
        raise core.ArenaError("source, fixtures, observer or tool identity changed during the variant tests")
    record = {"exit_code": code, "test_cases": ordinary_cases + (caller["test_cases"] if caller else 0), "ordinary_test_cases": ordinary_cases,
              "caller": caller, "snapshot": identity["snapshot"], "identity": population.canonical(identity),
              "runner": "bots/specialists/tests/run.py", "variant_driver": "bots/specialists/fireflies/tests/run-variant.py",
              "variant_driver_sha256": sha(Path(__file__).read_bytes()), "argv": sys.argv[1:], "overrides": overrides,
              "applied_source": {p.relative_to(ROOT).as_posix(): sha(data) for p, data in applied.items()},
              "fixtures": fixture_pins, "profile_assertions": assertions, "stdout": log.getvalue(),
              "scope": "Existing runner executes production helper fixtures in the exact applied source tree. The exact three-knob variant additionally executes complete production-caller native-fork fixtures from reached private checkpoints under declared observable-world/fuel interventions; their results do not imply autonomous joint reachability, guest fuel safety or physical distinction. Native health and activation remain separately gated. The control-knob resolution check uses the unchanged live knob declaration."}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(log.getvalue(), end="")
    print(f"tested {record['snapshot']} with overrides {overrides}; record {output.relative_to(ROOT)}")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
