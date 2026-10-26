"""Bounded multi-case C3 readiness campaign; all scores are restricted-model predictions."""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")


def peak_bytes(process: subprocess.Popen) -> int | None:
    if os.name != "nt":
        return None
    class Counters(ctypes.Structure):
        _fields_ = [("cb", ctypes.c_ulong), ("faults", ctypes.c_ulong)] + [
            (name, ctypes.c_size_t) for name in ["peak_working", "working", "peak_page_pool", "page_pool", "peak_nonpaged", "nonpaged", "pagefile", "peak_pagefile"]]
    counters = Counters()
    counters.cb = ctypes.sizeof(counters)
    query = ctypes.windll.psapi.GetProcessMemoryInfo
    query.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong]
    if query(int(process._handle), ctypes.byref(counters), counters.cb):
        return int(counters.peak_working)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[5])
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=10000)
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--cases", nargs="*", help="Optional case ids from the built-in readiness family")
    args = parser.parse_args()
    repo, out = args.repo.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if any(out.iterdir()):
        raise RuntimeError("evidence directory must be new/empty")
    exe = out / "worker.exe"
    shutil.copy2(args.exe.resolve(), exe)
    inputs = out / "inputs"
    inputs.mkdir()
    examples = repo / "bots/contenders/tools/optimization/examples"
    cases = []
    for name in ["c3-small.json", "c3-investment-cooperation.json", "c3-investment-breakpoint.json", "c3-recovery-cooperation.json"]:
        config = json.loads((examples/name).read_text())
        config["max_closed"] = 500000
        config["checkpoint_every"] = 1000
        cases.append((config["id"], config))
    base = json.loads((examples/"c3-real-map.json").read_text())
    for map_id, side in [("00gem-fields", "A"), ("00gem-fields", "B"), ("00elkring", "A")]:
        config = dict(base, id=f"{map_id}-{side}-economy", map=map_id, side=side,
                      greedy_baseline_limit=5000)
        cases.append((config["id"], config))
    mapped = json.loads((examples/"c3-real-map-atlas.json").read_text())
    atlas = inputs/"atlas-cost-modes.json"
    shutil.copy2(repo/mapped["atlas_file"], atlas)
    mapped["atlas_file"] = str(atlas)
    cases.append(("atlas-point-transfer", mapped))
    if args.cases:
        selected = set(args.cases)
        cases = [(name, config) for name, config in cases if name in selected]
        if {name for name, _ in cases} != selected:
            raise RuntimeError("unknown readiness case id")
    invocations = []

    def run(name: str, config: Path, iterations: int, resume: bool = False):
        target = out/name
        command = [str(exe), "c3", "--repo", str(repo), "--input", str(config),
                   "--out", str(target), "--iterations", str(iterations), "--seconds", str(args.seconds)]
        if resume:
            command.append("--resume")
        log = out/(name + ("-resume.log" if resume else ".log"))
        started, peak = time.monotonic(), 0
        with log.open("w", encoding="utf-8", newline="\n") as stream:
            process = subprocess.Popen(command, cwd=repo, stdout=stream, stderr=subprocess.STDOUT)
            while True:
                peak = max(peak, peak_bytes(process) or 0)
                try:
                    code = process.wait(timeout=0.2)
                    break
                except subprocess.TimeoutExpired:
                    if time.monotonic()-started > args.seconds+60:
                        process.terminate()
                        process.wait(timeout=10)
                        raise RuntimeError(f"{name} exceeded its stop boundary allowance")
        if code:
            raise RuntimeError(f"{name} failed with {code}; see {log}")
        record = {"command": command, "wall_seconds": time.monotonic()-started,
                  "sampled_peak_working_set_bytes": peak or None}
        invocations.append(record)
        return json.loads((target/"report.json").read_text()), record

    summaries = []
    for name, config in cases:
        config_path = inputs/(name+".json")
        save(config_path, config)
        first, first_work = run(name, config_path, min(1000, args.iterations))
        final, final_work = run(name, config_path, args.iterations, True)
        plan = json.loads((out/name/"best-plan.json").read_text())
        greedy = json.loads((out/name/"greedy-plan.json").read_text())
        contract = json.loads((out/name/"execution-contract.json").read_text())
        assert plan["model_replay"]["valid"] and greedy["model_replay"]["valid"]
        assert not contract["deployment_ready"] and not contract["native_execution_verified"]
        kinds = {}
        for action in plan["actions"]:
            kinds[action["kind"]] = kinds.get(action["kind"], 0)+1
        summaries.append({"case":name, "input_sha256":digest(config_path), "first_nodes":first["expanded"],
            "first_banked":first["banked_gems"],"first_win_round":first["winning_round"],
            "final_nodes":final["expanded"], "banked":final["banked_gems"], "win_round":final["winning_round"],
            "greedy_banked":greedy["banked"], "greedy_win_round":greedy["winning_round"],
            "win_round_improvement_over_greedy":greedy["winning_round"]-final["winning_round"] if min(greedy["winning_round"],final["winning_round"])>=0 else None,
            "status":final["status"], "frontier":final["frontier"], "seen":final["seen"],
            "beam_dropped":final["beam_dropped"], "optimal_within_input_macro_domain":final["optimal_within_input_macro_domain"],
            "plan_decisions":len(plan["actions"]), "plan_kinds":kinds,
            "sampled_peak_working_set_bytes":max(first_work["sampled_peak_working_set_bytes"] or 0,final_work["sampled_peak_working_set_bytes"] or 0),
            "service_modes":len(contract["service_mode_qualifications"]), "native_execution_verified":False})
        print(json.dumps(summaries[-1]), flush=True)
        save(out/"partial-index.json", summaries)
    # Exact continuation is checked independently of the time-bounded family.
    small_path = inputs/(cases[0][0]+".json")
    run("restart-split", small_path, 31)
    run("restart-split", small_path, 1000, True)
    run("restart-full", small_path, 1000)
    split = json.loads((out/"restart-split/checkpoint.json").read_text())
    full = json.loads((out/"restart-full/checkpoint.json").read_text())
    exact = all(split[key] == full[key] for key in ["iteration", "rng", "state"])
    assert exact, "interruption changed exact search continuation"
    save(out/"index.json", {"schema":"contenders-c3-readiness/v1", "qualification":"restricted scheduling-model campaign; no native schedule replay, rolling controller, disturbance/opponent or strength qualification",
        "worker_sha256":digest(exe),"cases":summaries,"exact_restart_equal":exact,
        "restart_fields":["iteration","rng","state"],"invocations":invocations,
        "essential_gap":"The guarded native macro executor, observed state transfer and rolling-horizon disturbance/opponent controller remain required; execution-contract.json exposes their contract.",
        "measured_memory_scope":"Windows process peak working set sampled while running; checkpoint JSON serialization can dominate the stop overshoot. This is not a 32 GB capacity proof."})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
