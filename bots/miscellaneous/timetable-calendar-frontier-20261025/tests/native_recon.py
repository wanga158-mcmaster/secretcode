"""Paired full-native Timetable acquisition applications, with opaque opponents.

This is a driver over arena's contained compiled runner. It registers ordinary
arena experiment metadata/rows, keeps native scratch only until exp close, and
never prepares or reads the opponent's source. Run arena exp close after review.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools/arena"))
import core
import games
import exp
import compiled_runner as runner

PASSIVE = "contenders/_mosaic-navigation-passive@fc1cf9fdc687"
PASSIVE_SHA = "650a3cda02582b8d4d7f4e4a0c2a5e78916700e872f2b4239a82a321f36ed8ce"
BASE = dict(production_service="true", prefix_service="true", direct_service="true",
            local_service="true", bounded_bootstrap="true", dash_service="true", trace_entry="false")
PROFILES = {"dash": BASE, "eager": {**BASE, "eager_service": "true"},
            "recon": {**BASE, "recon_service": "true", "eager_service": "true"},
            "capital": {**BASE, "capital_service": "true", "eager_service": "true"}}


def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf8", newline="\n")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def binding(key, directory, capture_dir, expected=None, names=("bot.wasm", "bot-injected.wasm"), allow_distinct=False):
    raw, injected = (directory / name for name in names)
    # Arena trimming deliberately retains only the selected raw executable.
    # The portable package also needs an unused injected member; repeating raw
    # there does not qualify execution of an uninspected original injected role.
    if not injected.is_file() and names == ("bot.wasm", "bot-injected.wasm"):
        injected = raw
    blobs = raw.read_bytes(), injected.read_bytes()
    if any(blob[:8] != b"\0asm\x01\0\0\0" for blob in blobs):
        raise ValueError("This panel requires WASM v1 modules")
    if blobs[0] != blobs[1] and not allow_distinct:
        raise ValueError("Distinct roles require an explicit selected-raw-only application")
    if expected and digest(raw) != expected:
        raise ValueError("Opaque opponent bytes differ")
    captured = []
    for role, blob in zip(("raw", "injected"), blobs):
        path = capture_dir / (key.replace("/", "-").replace("@", "-") + "-" + role + ".wasm")
        with path.open("xb") as output:
            output.write(blob)
        captured.append(runner._desc(path))
    return runner.Binding(key, captured[0], captured[1], blobs[0], "raw", [])


def production(stats, side):
    team = "team" + side
    owners = {int(c["id"]): {"name": c["name"], "metrics": c["metrics"], "events": {}}
              for c in stats["champions"] if c["team"] == team}
    for stream in stats["events"]:
        owner = stream.get("owner") or {}
        if owner.get("team") != team or owner.get("id") not in owners:
            continue
        if stream["kind"] not in ("campClear", "gemCollection", "gemSubmission", "buyItem", "death"):
            continue
        entries = stream["entries"]
        rounds = [int(e["timestamp"]["round"]) for e in entries]
        values = [float(e.get("value") or 0) for e in entries]
        owners[int(owner["id"])]["events"][stream["kind"]] = {
            "count": len(entries), "positive_count": sum(v > 0 for v in values),
            "sum": sum(values), "first": min(rounds) if rounds else None,
            "last": max(rounds) if rounds else None,
            "late_count": sum(r >= 1500 for r in rounds),
            "first_250_count": sum(r <= 250 for r in rounds)}
    return owners


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name")
    parser.add_argument("--run", action="store_true", required=True)
    parser.add_argument("--maps", default="00road-of-riches,01yellow-brick-road")
    parser.add_argument("--profiles", default="dash,eager,recon")
    parser.add_argument("--expect-source")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--allow-distinct-roles", action="store_true", help="Select raw only; do not infer injected-role equivalence")
    parser.add_argument("--reuse-controls", type=Path, help="Verified prior full panel; reuse its first-arm controls")
    parser.add_argument("--question", default="Do same-input route completion and named missing-information ingress produce completed independent services with measured health?")
    args = parser.parse_args()
    if args.reuse_controls:
        args.reuse_controls = args.reuse_controls.resolve()
    os.environ["CAMBC_OFFLINE"] = "1"
    os.environ["CAMBC_NO_UPDATE_CHECK"] = "1"
    labels = args.profiles.split(",")
    if not labels or any(label not in PROFILES for label in labels) or len(labels) != len(set(labels)):
        parser.error("Select unique profiles: " + ",".join(PROFILES))
    out = exp.exp_dir(args.name)
    if out.exists():
        raise ValueError("Inspect existing deterministic experiment rather than replace it")
    bot = core.bot("contenders/timetable")
    contents = core.read_closure(bot)
    source = core.version_of(contents)
    if args.expect_source and source != args.expect_source:
        raise ValueError("Owned source changed before freezing")
    entries = core.prepare_many([(bot, PROFILES[label], label, contents) for label in labels] +
                                [(bot, {}, "off", contents)], workers=min(4, len(labels) + 1))
    if any(not e.ok for e in entries):
        raise ValueError("Owned build failed: " + "\n".join(e.log[-2000:] for e in entries if not e.ok))
    if core.version_of(contents) != source:
        raise ValueError("Frozen inputs changed")
    out.mkdir(parents=True)
    captures = out / "captured-inputs"
    captures.mkdir()
    bindings = {e.key: binding(e.key, e.path / ".cambc/build-release", captures,
                              allow_distinct=args.allow_distinct_roles) for e in entries}
    passive_path = ROOT / ".cache/contenders-audit/opaque-reference"
    # Only the retained original raw role is selected. Supplying those same bytes
    # to the package's unused injected member does not inspect/qualify that role.
    bindings[PASSIVE] = binding(PASSIVE, passive_path, captures, PASSIVE_SHA,
                                ("passive-raw.wasm", "passive-raw.wasm"))
    prior_path = ROOT / "results/contenders/timetable/independent/e035a183bbe6/fresh-controls/off-preservation.json"
    prior = json.loads(prior_path.read_bytes())
    off_binding = bindings[entries[-1].key]
    off_identity = {"prior_proof": runner._desc(prior_path), "prior_coverage": prior["coverage"],
                    "raw_equal": off_binding.raw["sha256"] == prior["off_modules"]["bot.wasm"],
                    "injected_equal": off_binding.injected["sha256"] == prior["off_modules"]["bot-injected.wasm"],
                    "fresh_off_games": 0}
    maps = [core.map_path(m) for m in core.map_set(args.maps)]
    plans, reused = [], []
    prior_panel = None
    if args.reuse_controls:
        prior_panel = json.loads(args.reuse_controls.read_bytes())
        ps = prior_panel["spec"]
        if (ps["source"], ps["engine"], ps["bundle"]) != (source, games.ENGINE,
                runner._desc(ROOT / "docs/reference/current-version.json")):
            raise ValueError("Prior control source/engine/bundle differs")
        if ps["entrants"][labels[0]] != entries[0].key or ps["entrants"][PASSIVE.split("@")[0]] != PASSIVE:
            raise ValueError("Prior selected control/opponent keys differ")
        for key in (entries[0].key, PASSIVE):
            old_binding = [b for b in ps["bindings"] if b["snapshot"] == key]
            if len(old_binding) != 1 or any(old_binding[0]["raw"][f] != bindings[key].raw[f] for f in ("sha256", "size")):
                raise ValueError("Prior selected raw executable differs")
    for label, entrant in zip(labels, entries):
        for path in maps:
            for side in "AB":
                if prior_panel is not None and label == labels[0]:
                    matches = [a for a in prior_panel["summary"]["native_production"]
                               if (a["row"]["subject"], a["row"]["map"], a["row"]["side"]) == (label, path.stem, side)]
                    if len(matches) != 1 or matches[0]["row"]["rounds"] != 2000 or matches[0]["row"]["health"]:
                        raise ValueError("Prior control is missing, ambiguous, unhealthy or not a full game")
                    # Prior panel used the same pinned official map bundle. The
                    # actual current bytes must still match that pool manifest.
                    indexed = [m for m in core._pool_index() if (core.MAPS / m["file"]).resolve() == path.resolve()]
                    if len(indexed) != 1 or indexed[0]["sha256"] != digest(path):
                        raise ValueError("Control map is outside or differs from the pinned pool")
                    reused.append({"audit": matches[0], "original_evidence": runner._desc(args.reuse_controls),
                                   "map_pin": runner._desc(path), "scope": "Same source, selected raw modules, engine and pinned official map bundle; original row retained as a reused control"})
                    continue
                plans.append({"id": f"game-{len(plans):06d}", "a": entrant.key if side == "A" else PASSIVE,
                              "b": PASSIVE if side == "A" else entrant.key, "map": path.stem,
                              "map_file": str(path), "map_sha256": digest(path),
                              "tags": {"subject": side, "variant": label, "source": source}})
    spec = {"name": args.name, "question": args.question,
            "created": time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime()), "status": "open",
            "subjects": [{"label": label, "bot": bot.id, "set": PROFILES[label], "weights": None} for label in labels],
            "opponents": [PASSIVE.split("@")[0]], "maps": args.maps, "sides": "both", "rounds": 0,
            "keep": "none", "games": len(plans), "entrants": {**{l: e.key for l, e in zip(labels, entries)}, PASSIVE.split("@")[0]: PASSIVE},
            "source": source, "engine": games.ENGINE, "bundle": runner._desc(ROOT / "docs/reference/current-version.json"),
            "off_identity": off_identity, "driver": {"path": Path(__file__).relative_to(ROOT).as_posix(),
                "sha256": digest(Path(__file__)), "size": Path(__file__).stat().st_size},
            "bindings": [b.record() for b in bindings.values()], "opponent_source_read": False, "opponent_prepared": False,
            "selected_role": "raw", "unused_package_role": "May repeat raw after arena trimming; no injected execution claim",
            "distinct_roles_qualified": False, "reused_control_rows": len(reused),
            "control_reuse": [{k: v for k, v in r.items() if k != "audit"} for r in reused],
            "passive_role": "Original raw only; unused injected package member repeats raw bytes",
            "passive_original_raw": runner._desc(passive_path / "passive-raw.wasm")}
    write(out / "spec.json", spec)
    print(json.dumps({"source": source, "games": len(plans), "profiles": labels, "off_identity": off_identity}), flush=True)
    try:
        result = runner._run(plans, bindings, out / "native", "raw", args.jobs, 150,
                             {"kind": "registered-timetable-acquisition-application", "experiment": args.name,
                              "spec": runner._desc(out / "spec.json"), "opponent_source_unread": True})
    finally:
        if not runner.confirm_pending_cleanup():
            raise ValueError("Owned native process cleanup remains pending")
    if not result["stopped"] or result["jobs"] or not result["execution_pass"]:
        raise ValueError("Full contained native panel did not finish")
    rows = [r["audit"]["row"] for r in reused]
    audits = [{**r["audit"], "control_reuse": {k: v for k, v in r.items() if k != "audit"}} for r in reused]
    for row in result["rows"]:
        if not row["measurement_complete"] or not row["execution_complete"]:
            raise ValueError("Missing full native measurement")
        side = row["tags"]["subject"]
        other = "B" if side == "A" else "A"
        opponent = PASSIVE.split("@")[0]
        view = {"map": row["map"], "side": side, "subject": row["tags"]["variant"], "opponent": opponent,
                "result": "win" if row["winner"] == side else "draw" if row["winner"] == "draw" else "loss",
                "rounds": row["rounds"], "gems": row[side]["gems"], "opp_gems": row[other]["gems"],
                "health": {flag: 1 for flag in games.health(row, side)},
                "opp_health": {flag: 1 for flag in games.health(row, other)}}
        for key in ("kills", "deaths", "gold", "clears", "exhausted", "faults", "errors"):
            view[key], view["opp_" + key] = row[side][key], row[other][key]
        rows.append(view)
        stats = json.loads(runner._descriptor_path(row["statistics"]).read_bytes())
        audits.append({"row": view, "native_statistics_sha256": row["statistics"]["sha256"],
                       "native_archive_sha256": row["archive"]["sha256"],
                       "fuel_max": row[side]["fuel_max"], "fuel_at_budget": row[side]["fuel_at_budget"],
                       "champions": production(stats, side)})
    core.append_jsonl(out / "rows.jsonl", rows)
    summary = exp.summarize(spec, rows)
    summary["native_production"] = audits
    summary["source"] = source
    summary["fresh_native_games"] = len(result["rows"])
    summary["reused_control_rows"] = len(reused)
    summary["off_identity"] = off_identity
    summary["scope"] = "Selected ordinary pool prerequisites, both sides, deliberate passive; no competitive strength or broad health claim. Native raw scratch is deleted on exp close."
    write(out / "summary.json", summary)
    target = ROOT / "results/contenders/timetable/independent" / source / (args.name.split("/")[1] + ".json")
    target.parent.mkdir(parents=True, exist_ok=True)
    write(target, {"spec": spec, "summary": summary})
    print(json.dumps({"evidence": str(target.relative_to(ROOT)), "rows": rows}, indent=2), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
