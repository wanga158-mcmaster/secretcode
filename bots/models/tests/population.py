"""Models' behavior promises and offline native planning-path diagnostics.

``contracts`` describes intent, never admission. ``observe`` counts emitted native
planning records, not model opportunities: a supported forecast is still not proof
that a coupling changed an executed job relative to its control.

Run with the arena Python: population.py RUN_DIR [BOT], or --self-test.
"""
from __future__ import annotations

import json
import re
import sys
import zipfile
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HERE = "bots/models/tests"
PROMISES = {
    "reference": ("Common M0 menu, ranking and complete farm/capture/bank cycle",
                  "A productive common forecast is available",
                  "Current-bundle native planning reachability needs remeasurement"),
    "foundry": ("Shared monster health and capture reward change the selected job when an ally already works a group",
                "Fresh personally visible eligible ally hold, with unambiguous resource participation",
                "No native coupling-induced job difference; attacks are ambiguous"),
    "governor": ("Completed episodes calibrate phase duration and change the next selected complete job",
                 "Three uncensored positive current-kit completions of a phase",
                 "Own capture attribution unavailable; calibration activation unobserved"),
    "habitat": ("Visible pressure on an altar patch delays depleted stock and produces physical relocation",
                "Personally witnessed ready stock is consumed and patch pressure is supported",
                "No productive native relocation witness"),
    "repairman": ("Persisted camp damage makes a safe two-excursion clear rank ahead of a fresh full clear",
                  "Legal attack frame and safe escape/recall/restore cycle endpoint",
                  "Recall arrival is not canonical spawn; complete cycle endpoint undefined"),
    "archipelago": ("A newly visible articulation blocker reroutes the selected complete job along a proved alternate route",
                    "Fresh legal connector blocker and bounded alternate-route proof",
                    "No legal native blocker witness; observation/navigation previously suppressed decisions"),
    "timetable": ("Two personally observed actor frames cause a short physical route wait before continuing the job",
                  "Known owner turn order and visible two-frame traffic conflict",
                  "Turn order provider unknown; no native traffic witness"),
    "stewards": ("Roster-based camp responsibility and witnessed absence cause backup service takeover",
                 "Public roster complete and eight full-sight no-service rounds",
                 "Living service reach unknown; takeover unobserved"),
    "bulkheads": ("A connector release estimate propagates to dependent phases and changes a physical selected job",
                  "Eight uncensored witnessed connector-block episodes of the supported cause",
                  "No fitted episodes or release provider; ordinary forecasts equal M0"),
    "chronicle": ("Personally witnessed capture history defers premature travel to respawning stock after sight is lost",
                  "Legal capture history and queried respawn duration",
                  "No qualifying native history; forecasts otherwise equal M0"),
    "quotient": ("Conservative alias-class forecast reuse changes a deadline-bound selected complete job",
                 "Composition/lifecycle/health/distance alias class near a deadline boundary",
                 "No native alias/deadline witness"),
    "counterpart": ("Candidate-conditioned simulation of a known reactive rival changes the selected job and encounter",
                    "Witnessed known rival profile with supported route observations",
                    "No rival-profile provider installed; both arms use M0"),
    "kernel": ("Eight joint logged outcome paths change a deadline decision relative to a mean-time control",
               "First-eight-started fit bins with supported current kit and composition",
               "No fit table; reference draft and M0 fallback"),
    "capital": ("Fixed purchases propagate into future combat/recovery time and change a selected complete job",
                "Supported cash, equipment combination and runtime combat-stat parity",
                "Three item-family purchase/combination parity unverified"),
    "commons": ("A single lifecycle stock consumed by witnessed producers changes which source is physically served",
                "Sole supported enemy producer and route quotes under frozen priority",
                "No sole-producer isolation or enemy route quotes"),
    "metronome": ("Bounded closeness-prioritized refinement completes a different forecast and selected job",
                  "Measured native operation costs support the work envelope",
                  "No measured native envelope; both arms report unsupported and use M0"),
    "covenant": ("A failed witnessed premise repairs or replaces an executing complete job before its deadline",
                 "Current job automaton, observed premise failure and legal repair/alternate-path evidence",
                 "Two source-bound ranking witnesses; native FARM milestones/checkpoints/alternate proof incomplete"),
}


def contracts():
    result = []
    for name, (interaction, preconditions, gap) in PROMISES.items():
        stem = f"models/{name}"
        own_tests = sorted(p.relative_to(ROOT).as_posix() for p in
                           (ROOT / HERE).glob(f"plan4-{name}*.cpp"))
        result.append({
            "id": f"{stem}/coupling" if name != "reference" else f"{stem}/m0-cycle",
            "bot": stem,
            "interaction": interaction,
            "preconditions": preconditions,
            "activation": ("A common forecast-ranked FARM/BANK job reaches production action and its physical cycle; no coupling is claimed"
                           if name == "reference" else
                           "Production ranking changes the selected physical job against the same-draft mechanism-disabled arm; action must execute"),
            "continuation": "Selected source -> clear -> capture -> bank/restore completes or executes its witnessed legal repair",
            "refusal": "Unsupported evidence falls back to M0; safety/channel/capture preservation outranks the model",
            "control": f"{stem} with PLAN4_MODEL_ENABLED=0" if name != "reference" else "Frozen anchors/fundamentals; M0 is the shared baseline",
            "neighbor": "models/reference" if name != "reference" else "models/kernel (currently M0 fallback with the same draft)",
            "positive_fixture": "; ".join(own_tests) or "Missing reachable native positive fixture",
            "refusal_fixture": "bots/models/tests/plan4-runtime.cpp; own pure/runtime cases test unsupported inputs, not native continuation",
            "observer": "Offline p4 decision/model/phase diagnostics locate planning reachability; they do not establish coupling activation or control difference",
            "test_paths": [f"{HERE}/run.py", f"{HERE}/plan4-runtime.cpp", *own_tests],
            "observer_paths": [f"{HERE}/population.py", "bots/models/tools/telemetry.py"],
            "gap": gap + "; full-pool exact-version health, native activation/continuation, distinctness and fresh applicability remain unverified",
            "next_check": ("Witness a common forecast-selected complete cycle on fresh maps, then determine whether this baseline contributes coverage absent from its neighbor"
                           if name == "reference" else
                           "Locate the earliest observer/navigation/menu/evaluation loss, then demonstrate one production choice/action difference against the paired disabled control"),
            "scope": "Unadmitted model intent; no native model coupling is certified by these descriptions",
        })
    return result


def telemetry(data):
    """Parse complete emitted diagnostic rows, rejecting malformed key/value data."""
    for line in data.decode("utf-8", errors="replace").splitlines():
        # Native team stdout may pad an owner-turn boundary with NUL bytes.
        # The suite's existing telemetry parser also locates p4 past that padding.
        line = line.lstrip("\0")
        if not line.startswith("p4 "):
            continue
        parts = line.split()
        if len(parts) < 3:
            raise ValueError("incomplete p4 diagnostic row")
        fields = {}
        for token in parts[2:]:
            if token.count("=") != 1:
                raise ValueError("malformed p4 diagnostic field")
            key, value = token.split("=", 1)
            if not key or not value or key in fields:
                raise ValueError("duplicate or empty p4 diagnostic field")
            fields[key] = int(value) if re.fullmatch(r"-?\d+", value) else value
        yield parts[1], fields


def summarize(data):
    records = list(telemetry(data))
    decisions = [fields for kind, fields in records if kind == "decision"]
    models = {(f.get("actor"), f.get("round"), f.get("job")): f
              for kind, f in records if kind == "model"}
    phases = defaultdict(list)
    for kind, fields in records:
        if kind == "phase":
            phases[fields.get("name", "unknown")].append(fields)
    return {
        "emitted_decisions": len(decisions),
        "fallbacks": dict(Counter(f.get("fallback", "unknown") for f in decisions)),
        "forecast_ranked": sum(f.get("fallback") == "none" for f in decisions),
        "model_rows": len(models),
        "supported_enabled_model_rows": sum(f.get("supported") == 1 and f.get("enabled") == 1
                                            for f in models.values()),
        "model_reasons": dict(Counter(str(f.get("reason", "unknown")) for f in models.values())),
        "truncation_masks": dict(Counter(str(f["truncationMask"]) for f in models.values()
                                         if "truncationMask" in f)),
        "supported_enabled_forecast_ranked": sum(
            f.get("fallback") == "none" and
            models.get((f.get("actor"), f.get("round"), f.get("job")), {}).get("supported") == 1 and
            models.get((f.get("actor"), f.get("round"), f.get("job")), {}).get("enabled") == 1
            for f in decisions),
        "phase_samples": {name: {"count": len(samples),
            "minimum_after": min(f["after"] for f in samples),
            "maximum_cost": max(f["before"] - f["after"] for f in samples),
            "observer_stop_stages": dict(Counter(f.get("stopStage", "unreported") for f in samples))
                if name == "observer" else {}}
            for name, samples in phases.items()},
        "opportunities": None,
        "verified_activations": [],
        "limitation": "Emitted decision rows are sampled fallbacks or productive forecasts, not all attempts. No opportunity detector or paired physical continuation observer exists yet.",
    }


def observe(contract, rows, root=ROOT):
    observations = []
    for row in rows:
        bot = contract["bot"]
        sides = [side for side in ("A", "B")
                 if str(row.get(side.lower(), "")).split("@", 1)[0] == bot]
        for side in sides:
            replay = row.get("replay")
            if not replay:
                observations.append({"map": row.get("map"), "side": side,
                                     "entrant": row.get(side.lower()),
                                     "variant": row.get("tags", {}).get("variant"),
                                     "unavailable": "native replay missing"})
                continue
            path = Path(root) / replay
            try:
                with zipfile.ZipFile(path) as archive:
                    summary = summarize(archive.read(f"team-{side.lower()}.stdout"))
            except (OSError, KeyError, zipfile.BadZipFile) as exc:
                summary = {"unavailable": f"native replay/stdout unavailable: {type(exc).__name__}"}
            observations.append({"map": row.get("map"), "side": side,
                                 "entrant": row.get(side.lower()),
                                 "variant": row.get("tags", {}).get("variant"),
                                 "replay": replay, **summary})
    return {"bot": contract["bot"], "games": observations,
            "verified": False, "reason": "Reachability diagnostics are not coupling/continuation evidence"}


def self_test():
    sample = (b"\0\0p4 model actor=1 round=2 job=3 supported=1 enabled=1\n"
              b"p4 decision actor=1 round=2 job=3 fallback=none\n"
              b"p4 decision actor=1 round=6 job=4 fallback=budget\n"
              b"\0p4 phase name=observer before=80000 after=61000 stopStage=9\n")
    result = summarize(sample)
    assert result["emitted_decisions"] == 2 and result["forecast_ranked"] == 1
    assert result["supported_enabled_forecast_ranked"] == 1
    assert result["opportunities"] is None and result["verified_activations"] == []
    assert result["phase_samples"]["observer"]["maximum_cost"] == 19000
    assert len(contracts()) == 17 and len({c["id"] for c in contracts()}) == 17
    reference = next(c for c in contracts() if c["bot"] == "models/reference")
    assert "no coupling is claimed" in reference["activation"]
    absent = observe(reference, [{"a": "models/reference@missing", "replay": "not-a-native-replay.repl26a"}])
    assert "unavailable" in absent["games"][0] and not absent["verified"]
    try:
        summarize(b"p4 decision actor=1 actor=2\n")
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate fields must not inflate witness counts")
    print("models population observer: checks passed; no activation inferred")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    else:
        run = Path(sys.argv[1])
        rows = [json.loads(line) for line in (run / "games.jsonl").read_text(encoding="utf-8").splitlines() if line.strip()]
        selected = sys.argv[2] if len(sys.argv) > 2 else "models/reference"
        contract = next(c for c in contracts() if c["bot"] == selected)
        print(json.dumps(observe(contract, rows), indent=2))
