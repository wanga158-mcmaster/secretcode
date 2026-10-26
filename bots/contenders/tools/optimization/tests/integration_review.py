"""Independent short campaign/restart checks against the actual optimizer executable.

No evaluator fixture is selected here. Native modes invoke the production adapter;
model modes retain their declared restricted-model authority. Each invocation has
its stdout/stderr and exact argv recorded. --plan can reduce production workloads
with explicit real inputs without silently converting a native mode to a fixture.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
from typing import Any


ROOT = Path(__file__).resolve().parents[5]
ENGINE = ROOT / "bots/contenders/tools/optimization"
DEFAULTS = {
    "c1": ("c1-native.json", 1, 2, "native_full_game", "best_program.json"),
    "c2": ("c2-smoke.json", 1, 2, "native_full_game", "best/configuration.hpp"),
    "c3": ("c3-small.json", 13, 100, "restricted_economy_model", "best-plan.json"),
    "c4": ("c4-smoke.json", 13, 1000, "restricted_transition_model", "atlas.json"),
    "c5": ("c5-native.json", 1, 2, "native_episodic_training", "current-policy.json"),
    "c6": ("c6-native.json", 1, 2, "native_teacher_collection", "dataset-manifest.json"),
    "c7": ("c7-native.json", 1, 2, "native_full_game_league", "mixture.json"),
    "c8": ("c8-smoke.json", 1, 4, "restricted_transition_model", "curriculum-summary.json"),
    "c9": ("c9-smoke.json", 13, 2000, "restricted_endgame_model", "*/solution.json"),
    "c10": ("c10-fuel.json", 1, 2, "native_wasm_fuel", "best_candidate.json"),
    "c11": ("c11.json", 1, 2, "native_ryth_branch_oracle", "best-projection-model.json"),
}


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path: Path, body: Any) -> None:
    path.write_text(json.dumps(body, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ENGINE / "bin/optimizer.exe")
    parser.add_argument("--repo", type=Path, default=ROOT)
    parser.add_argument("--out", type=Path, required=True, help="new output directory")
    parser.add_argument("--modes", nargs="+", default=["c3", "c4", "c8", "c9"])
    parser.add_argument("--plan", type=Path, help="JSON object keyed by mode; input/first/final/artifact/extra overrides")
    parser.add_argument("--timeout", type=float, default=900)
    parser.add_argument("--seconds", type=float, default=600)
    parser.add_argument("--seed", type=int, default=731)
    args = parser.parse_args()
    executable, repo, output = args.exe.resolve(), args.repo.resolve(), args.out.resolve()
    if not executable.is_file():
        parser.error(f"missing optimizer executable: {executable}")
    output.mkdir(parents=True, exist_ok=False)
    plan = read(args.plan) if args.plan else {}
    summary: dict[str, Any] = {
        "schema": "contenders-independent-integration-review/v1",
        "started_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "executable": str(executable), "executable_sha256": digest(executable),
        "repo": str(repo), "seed": args.seed, "modes": [],
        "authority": "installation, real-input short campaign and restart checks; no competitive-strength claim",
    }
    failed = False
    for mode in args.modes:
        if mode not in DEFAULTS:
            parser.error(f"unknown mode {mode}")
        name, first, final, backend, artifact = DEFAULTS[mode]
        spec = plan.get(mode, {})
        source = Path(spec.get("input", ENGINE / "examples" / name))
        if not source.is_absolute():
            source = repo / source
        source = source.resolve()
        first, final = int(spec.get("first", first)), int(spec.get("final", final))
        entry: dict[str, Any] = {"mode": mode, "backend": backend, "input": str(source), "calls": []}
        summary["modes"].append(entry)
        campaign = output / mode
        try:
            assert source.is_file(), f"missing production input: {source}"
            assert 0 < first <= final, "invalid cumulative work limits"
            entry["input_sha256"] = digest(source)
            input_snapshot = output / "inputs" / f"{mode}.json"
            input_snapshot.parent.mkdir(exist_ok=True)
            input_snapshot.write_bytes(source.read_bytes())
            entry["input_snapshot"] = str(input_snapshot)
            mode_seed = int(spec.get("seed", args.seed))
            entry["seed"] = mode_seed
            base = [str(executable), mode, "--repo", str(repo), "--input", str(source),
                    "--out", str(campaign), "--seed", str(mode_seed), "--seconds", str(args.seconds)]
            extra = [str(value) for value in spec.get("extra", [])]
            assert not any("evaluator" in value or "diagnostic" in value for value in extra), \
                "the independent production smoke must not select a test evaluator"

            def launch(label: str, cap: int, resume: bool) -> dict[str, Any]:
                command = base + ["--iterations", str(cap)] + extra + (["--resume"] if resume else [])
                started = time.monotonic()
                result = subprocess.run(command, cwd=repo, capture_output=True, text=True,
                                        encoding="utf-8", errors="replace", timeout=args.timeout)
                (output / f"{mode}-{label}.stdout.txt").write_text(result.stdout, encoding="utf-8")
                (output / f"{mode}-{label}.stderr.txt").write_text(result.stderr, encoding="utf-8")
                call = {"label": label, "argv": command, "returncode": result.returncode,
                        "seconds": time.monotonic() - started}
                entry["calls"].append(call)
                assert result.returncode == 0, f"{label} exit {result.returncode}: {result.stderr[-2000:]}"
                checkpoint = read(campaign / "checkpoint.json")
                report = read(campaign / "report.json")
                assert isinstance(checkpoint.get("state"), dict), "checkpoint lacks engine state"
                assert 0 <= checkpoint["iteration"] <= cap, "work exceeded cumulative iteration cap"
                assert checkpoint.get("rng"), "checkpoint lacks serialized RNG"
                assert report, "empty engine report"
                call["iteration"] = checkpoint["iteration"]
                call["report_sha256"] = digest(campaign / "report.json")
                return checkpoint

            started = launch("start", first, False)
            same = launch("same-cap-resume", first, True)
            assert started["iteration"] == same["iteration"], "same-cap resume consumed extra work"
            assert started["rng"] == same["rng"], "same-cap resume resampled a candidate"
            resumed = launch("increased-cap-resume", final, True)
            assert resumed["iteration"] >= same["iteration"], "resume lost completed work"
            if spec.get("compare_uninterrupted", backend.startswith("restricted_")):
                full = output / f"{mode}-uninterrupted"
                command = base.copy()
                command[command.index("--out") + 1] = str(full)
                command += ["--iterations", str(final)] + extra
                started_at = time.monotonic()
                result = subprocess.run(command, cwd=repo, capture_output=True, text=True,
                                        encoding="utf-8", errors="replace", timeout=args.timeout)
                (output / f"{mode}-uninterrupted.stdout.txt").write_text(result.stdout, encoding="utf-8")
                (output / f"{mode}-uninterrupted.stderr.txt").write_text(result.stderr, encoding="utf-8")
                entry["calls"].append({"label": "uninterrupted", "argv": command,
                    "returncode": result.returncode, "seconds": time.monotonic() - started_at})
                assert result.returncode == 0, f"uninterrupted exit {result.returncode}: {result.stderr[-2000:]}"
                whole = read(full / "checkpoint.json")
                for field in ("iteration", "rng", "state"):
                    assert whole[field] == resumed[field], f"split/resume changed {field} versus uninterrupted execution"
                entry["split_resume_exact_state"] = True
            if mode == "c3" and spec.get("check_closed_limit", True):
                # The cap may interrupt a child enumeration. Its omitted branches
                # must remain recorded as incomplete after every restart.
                capped_input = output / "c3-closed-limit-input.json"
                capped_config = read(ENGINE / "examples/c3-small.json")
                capped_config.update(max_closed=4, max_frontier=16, checkpoint_every=1)
                write(capped_input, capped_config)
                capped_output = output / "c3-closed-limit"
                cap_command = [str(executable), "c3", "--repo", str(repo), "--input", str(capped_input),
                               "--out", str(capped_output), "--seed", str(args.seed), "--iterations", "1000"]
                snapshots = []
                for attempt in range(3):
                    command = cap_command + (["--resume"] if attempt else [])
                    result = subprocess.run(command, cwd=repo, capture_output=True, text=True,
                                            encoding="utf-8", errors="replace", timeout=args.timeout)
                    (output / f"c3-cap-{attempt}.stdout.txt").write_text(result.stdout, encoding="utf-8")
                    (output / f"c3-cap-{attempt}.stderr.txt").write_text(result.stderr, encoding="utf-8")
                    assert result.returncode == 0, f"closed-limit check exit {result.returncode}: {result.stderr[-1500:]}"
                    cap_checkpoint = read(capped_output / "checkpoint.json")
                    cap_report = read(capped_output / "report.json")
                    assert cap_report["status"] == "closed_memory_limit", "closed-limit provenance disappeared"
                    assert cap_report["optimal_within_input_macro_domain"] is False, "omitted branches became an exact result"
                    assert cap_checkpoint["state"].get("closed_limit") is True, "closed-limit provenance not persisted"
                    snapshots.append({key: cap_checkpoint[key] for key in ("iteration", "rng", "state")})
                assert snapshots[0] == snapshots[1] == snapshots[2], "closed-limit resume expanded an already incomplete search"
                entry["closed_limit_regression"] = {"passed": True, "restarts": 2,
                    "iteration": snapshots[0]["iteration"], "input_sha256": digest(capped_input),
                    "report": str(capped_output / "report.json")}
            found = list(campaign.glob(spec.get("artifact", artifact)))
            assert found and all(path.is_file() and path.stat().st_size for path in found), \
                f"missing useful output: {spec.get('artifact', artifact)}"
            assert digest(executable) == summary["executable_sha256"], "executable changed during review"
            assert digest(source) == entry["input_sha256"], "input changed during review"
            entry.update(status="passed", final_iteration=resumed["iteration"],
                         artifact_files=[{"path": str(path.relative_to(campaign)), "sha256": digest(path)} for path in found],
                         checkpoint_sha256=digest(campaign / "checkpoint.json"),
                         report_path=str(campaign / "report.json"))
            print(f"{mode}: passed, {resumed['iteration']} cumulative work units, {backend}", flush=True)
        except Exception as error:
            failed = True
            entry.update(status="failed", error=f"{type(error).__name__}: {error}")
            print(f"{mode}: FAILED: {error}", flush=True)
        finally:
            write(output / "review-results.json", summary)
    summary["finished_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    summary["passed"] = not failed
    write(output / "review-results.json", summary)
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
