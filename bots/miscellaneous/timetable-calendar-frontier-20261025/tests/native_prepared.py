"""Run declared full-game recipes from protected RAW payloads, without a build.

The existing arena compiled runner owns slots, subprocesses and private Windows
Jobs. This driver only validates a prepared recipe, registers an experiment and
reduces complete native results. Use --check-only first; close the experiment
with arena exp close after the answer and any focused replay inspection.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools/arena"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import core
import exp
import games
import compiled_runner as runner
from native_recon import production, write

MAGIC = b"\0asm\x01\0\0\0"
DEFAULT_PREPARED = ROOT / ".cache/contenders-timetable/capital-return-restart-6eedf9797c63/binding.json"


def utc():
    return time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime())


def checked(value):
    """Read exactly one canonical prepared descriptor; do not follow bot sources."""
    path = Path(value["path"])
    path = path if path.is_absolute() else ROOT / path
    path = path.resolve()
    actual = runner._desc(path)
    if any(actual[field] != value[field] for field in ("sha256", "size")):
        raise ValueError(f"Prepared input changed: {value['path']}")
    return path, actual


def own_descriptor(path):
    """Driver source is owned test code, outside the runner's data-path scope."""
    blob = path.read_bytes()
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": hashlib.sha256(blob).hexdigest(), "size": len(blob)}


def declared_profile_sources(source, profiles, explicit=None):
    labels = {p["label"] for p in profiles}
    if explicit is None:
        return {label: source for label in labels}
    if not isinstance(explicit, dict) or set(explicit) != labels:
        raise ValueError("Explicit profile_sources must name every profile exactly once")
    if any(not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{12}", value) is None for value in explicit.values()):
        raise ValueError("Every profile source must be an exact 12-hex identity")
    if source not in explicit.values():
        raise ValueError("The prepared source must belong to at least one declared profile")
    return dict(explicit)


def published_raw_inputs(spec):
    """Accept the two published schemas without reopening old cache artifacts."""
    values = {}
    if spec.get("selected_role", "raw") != "raw":
        raise ValueError("Reused controls must select the raw role")
    records = [(b["snapshot"], b["raw"], b.get("selected_role", "raw")) for b in spec.get("bindings", [])]
    records += [(r["snapshot"], r["selected_payload"], r["selected_role"]) for r in spec.get("input_records", [])]
    for key, descriptor, role in records:
        if role != "raw":
            raise ValueError("Reused control input is not raw-selected")
        if key in values and any(values[key][field] != descriptor[field] for field in ("sha256", "size")):
            raise ValueError("Published control has contradictory selected raw descriptors")
        values[key] = descriptor
    if not values:
        raise ValueError("Reused control has no published selected raw provenance")
    return values


def load_prepared(path):
    path = path.resolve()
    prepared_pin = runner._desc(path)
    state = json.loads(path.read_bytes())
    source = state["source"]
    if re.fullmatch(r"[0-9a-f]{12}", source) is None:
        raise ValueError("Prepared source must be its exact 12-hex identity")
    if state["engine"] != games.ENGINE:
        raise ValueError("Prepared engine differs from the current pinned runner")
    _, current_bundle = checked(state["bundle"])
    if current_bundle != runner._desc(ROOT / "docs/reference/current-version.json"):
        raise ValueError("Prepared bundle path is not the current pinned bundle")
    profiles = state["profiles"]
    if not profiles or len({p["label"] for p in profiles}) != len(profiles):
        raise ValueError("Prepared profiles are missing or ambiguous")
    profile_sources = declared_profile_sources(source, profiles, state.get("profile_sources"))
    source_mapping_explicit = "profile_sources" in state
    selected, key_names = {}, {}
    for record in state["selected_payloads"]:
        key = record["snapshot"]
        if key in selected or record["selected_role"] != "raw":
            raise ValueError("Prepared payload keys must be unique and RAW-selected")
        selected_path, descriptor = checked(record)
        blob = selected_path.read_bytes()
        if len(blob) > runner.MAX_FILE or blob[:8] != MAGIC:
            raise ValueError("Prepared selected payload is not bounded WASM v1")
        if hashlib.sha256(blob).hexdigest() != descriptor["sha256"]:
            raise ValueError("Prepared selected payload changed while reading")
        selected[key] = {"record": record, "descriptor": descriptor, "blob": blob}
        name = key.replace("/", "-").replace("@", "-")
        if name in key_names:
            raise ValueError("Prepared portable names collide")
        key_names[name] = key
    if not selected:
        raise ValueError("No prepared selected payloads")

    # Exact source/engine/module/map controls remain the original published rows.
    controls, control_pins, map_pins, profile_keys = [], {}, {}, {}
    labels = {p["label"]: p for p in profiles}
    for control in state.get("controls", []):
        evidence_path, evidence_pin = checked(control["original_evidence"])
        control_pins[evidence_pin["path"]] = evidence_pin
        _, map_pin = checked(control["map_pin"])
        map_pins[map_pin["sha256"]] = map_pin
        prior = json.loads(evidence_path.read_bytes())
        old_spec = prior["spec"]
        if (old_spec["engine"], old_spec["bundle"]) != (state["engine"], state["bundle"]):
            raise ValueError("Prepared control engine/bundle differs")
        old_sources = declared_profile_sources(old_spec["source"], old_spec["subjects"], old_spec.get("profile_sources"))
        old_bindings = published_raw_inputs(old_spec)
        for label, profile in labels.items():
            old_profiles = [p for p in old_spec["subjects"] if p["label"] == label]
            if not old_profiles:
                continue
            if len(old_profiles) != 1 or old_profiles[0] != profile:
                raise ValueError("Reused control profile differs")
            if old_sources[label] != profile_sources[label]:
                raise ValueError("Reused control profile source differs; cross-source reuse requires explicit profile_sources")
            key = old_spec["entrants"][label]
            if key not in selected or key not in old_bindings:
                raise ValueError("Reused control selected payload is missing")
            if any(old_bindings[key][f] != selected[key]["descriptor"][f] for f in ("sha256", "size")):
                raise ValueError("Reused control raw module differs")
            if label in profile_keys and profile_keys[label] != key:
                raise ValueError("Contradictory prepared profile identity")
            profile_keys[label] = key
        # Every selected prior opponent must also be the same exact raw bytes.
        for key in selected.keys() & old_bindings.keys():
            if any(old_bindings[key][f] != selected[key]["descriptor"][f] for f in ("sha256", "size")):
                raise ValueError("Reused opponent/control raw module differs")
        controls.append({"report": prior, "evidence": evidence_pin, "map_pin": map_pin,
                         "profile_sources": old_sources, "raw_inputs": old_bindings})

    # New prepared profiles can declare explicit keys. The deferred capital recipe
    # predates that field; its one remaining owned key identifies its one new arm.
    for label, key in state.get("profile_keys", {}).items():
        if label in profile_keys and key != profile_keys[label]:
            raise ValueError("Explicit profile key contradicts the exact reused control")
        profile_keys[label] = key
    unresolved = [p for p in profiles if p["label"] not in profile_keys]
    remaining = [k for k in selected if k not in profile_keys.values() and
                 any(k.startswith(p["bot"] + "@") for p in unresolved)]
    if len(unresolved) == 1 and len(remaining) == 1:
        profile_keys[unresolved[0]["label"]] = remaining[0]
    if set(profile_keys) != set(labels) or any(k not in selected for k in profile_keys.values()):
        raise ValueError("Prepared profiles require unambiguous selected keys")
    keys_to_labels = {k: label for label, k in profile_keys.items()}
    if len(keys_to_labels) != len(profile_keys):
        raise ValueError("Two prepared profiles name one selected key")

    plans = []
    for index, old in enumerate(state["planned_jobs"]):
        if "a" in old:
            a, b = old["a"], old["b"]
            map_file = Path(old["map_file"])
            map_file = map_file if map_file.is_absolute() else ROOT / map_file
            map_pin = runner._desc(map_file.resolve())
            if map_pin["sha256"] != old["map_sha256"]:
                raise ValueError("Declared prepared map changed")
            map_pins[map_pin["sha256"]] = map_pin
        else:
            # A prior API manifest names old ZIP paths, not bot source paths.
            # Resolve ONLY their portable basenames to verified selected bytes.
            try:
                a = key_names[Path(old["teamA"]["path"]).stem]
                b = key_names[Path(old["teamB"]["path"]).stem]
                map_pin = map_pins[Path(old["map"]).stem]
            except KeyError as error:
                raise ValueError("Old manifest lacks an exact prepared payload/map binding") from error
        if a not in selected or b not in selected:
            raise ValueError("Declared job uses an unprepared payload")
        tag = dict(old.get("tags", {}))
        candidates = [(s, k) for s, k in (("A", a), ("B", b)) if k in keys_to_labels]
        if len(candidates) != 1:
            raise ValueError("Each prepared job needs exactly one declared subject arm")
        side, subject_key = candidates[0]
        label = keys_to_labels[subject_key]
        if tag.get("subject", side) != side or tag.get("variant", label) != label:
            raise ValueError("Declared prepared job tags contradict selected sides")
        if tag.get("source", profile_sources[label]) != profile_sources[label]:
            raise ValueError("Declared job source contradicts its profile origin")
        tag.update(subject=side, variant=label, source=profile_sources[label])
        map_file = ROOT / map_pin["path"]
        plans.append({"id": f"game-{index:06d}", "a": a, "b": b, "map": map_file.stem,
                      "map_file": str(map_file), "map_sha256": map_pin["sha256"], "tags": tag})
    if not plans or len({(p["a"], p["b"], p["map_sha256"]) for p in plans}) != len(plans):
        raise ValueError("Prepared jobs are empty or duplicate deterministic keys")

    # Each control description may repeat its report/map for the two old sides.
    # Reduce it once per actual declared opponent/map/side key, never duplicate rows.
    reused, seen = [], set()
    base_label = profiles[0]["label"]
    for plan in plans:
        side = plan["tags"]["subject"]
        other_key = plan["b"] if side == "A" else plan["a"]
        opponent = other_key.split("@")[0]
        for control in controls:
            if control["map_pin"]["sha256"] != plan["map_sha256"]:
                continue
            report = control["report"]
            matches = [audit for audit in report["summary"]["native_production"]
                       if (audit["row"]["subject"], audit["row"]["map"], audit["row"]["side"], audit["row"]["opponent"])
                       == (base_label, plan["map"], side, opponent)]
            if len(matches) != 1:
                continue
            row = matches[0]["row"]
            if row.get("truncated") or row["health"] or row["rounds"] != 2000:
                raise ValueError("Prepared reused control is incomplete or unhealthy")
            prior_opp = report["spec"]["entrants"][opponent]
            if prior_opp != other_key or other_key not in control["raw_inputs"]:
                raise ValueError("Prepared reused control opponent key differs")
            original_source = control["profile_sources"][base_label]
            original_key = report["spec"]["entrants"][base_label]
            if row.get("source", original_source) != original_source or row.get("selected_snapshot", original_key) != original_key:
                raise ValueError("Published control row contradicts its original source/key")
            native_match = matches[0].get("native_match")
            if native_match and (native_match["mapHash"] != plan["map_sha256"] or native_match["engineVersion"] != state["engine"]):
                raise ValueError("Observed native control map/engine differs")
            key = opponent, plan["map"], side
            if key not in seen:
                reused.append({"audit": matches[0], "original_evidence": control["evidence"],
                               "map_pin": control["map_pin"], "original_row_unchanged": True,
                               "original_source": original_source, "original_snapshot": original_key,
                               "original_profile_label": base_label})
                seen.add(key)
    if controls and len(reused) != len(plans):
        raise ValueError("Each prepared new key needs its exact complete reused control")
    if any(p["tags"]["variant"] == base_label for p in plans) and reused and not state.get("allow_control_regeneration_for_inspection"):
        raise ValueError("Settled controls must be reused; declare deliberate inspection regeneration separately")
    planned_sides = sorted({p["tags"]["subject"] for p in plans})
    requested = state.get("requested_sides", planned_sides)
    if isinstance(requested, str):
        requested = ["A", "B"] if requested == "both" else [requested]
    if (not isinstance(requested, list) or any(side not in ("A", "B") for side in requested)
            or len(requested) != len(set(requested)) or sorted(requested) != planned_sides):
        raise ValueError("Requested sides must exactly match the declared native plans")
    origins = {label: {"source": profile_sources[label], "selected_snapshot": key, "selected_role": "raw",
                       "selected_payload": selected[key]["descriptor"],
                       "planned_native_keys": sum(p["tags"]["variant"] == label for p in plans),
                       "reused_control_rows": sum(r["original_profile_label"] == label for r in reused)}
               for label, key in profile_keys.items()}
    return {"state": state, "prepared": prepared_pin, "bundle": current_bundle, "selected": selected,
            "profiles": profiles, "profile_keys": profile_keys, "plans": plans,
            "profile_sources": profile_sources, "profile_origins": origins,
            "profile_sources_explicit": source_mapping_explicit,
            "cross_source_contrast": len(set(profile_sources.values())) > 1,
            "requested_sides": sorted(requested), "planned_native_sides": planned_sides,
            "controls": reused, "control_evidence": list(control_pins.values()), "maps": list(map_pins.values())}


def native_view(row):
    side = row["tags"]["subject"]
    other = "B" if side == "A" else "A"
    opponent = row["b"] if side == "A" else row["a"]
    out = {"map": row["map"], "side": side, "subject": row["tags"]["variant"],
           "source": row["tags"]["source"], "selected_snapshot": row[side.lower()], "selected_role": "raw",
           "opponent": opponent.split("@")[0], "result": "win" if row["winner"] == side else
           "draw" if row["winner"] == "draw" else "loss", "rounds": row["rounds"],
           "gems": row[side]["gems"], "opp_gems": row[other]["gems"],
           "health": {flag: 1 for flag in games.health(row, side)},
           "opp_health": {flag: 1 for flag in games.health(row, other)}}
    for key in ("kills", "deaths", "gold", "clears", "exhausted", "faults", "errors"):
        out[key], out["opp_" + key] = row[side][key], row[other][key]
    return out


def focused_events(stats, side):
    selected = []
    for stream in stats["events"]:
        owner = stream.get("owner") or {}
        if owner.get("team") != "team" + side or stream["kind"] not in (
                "buyItem", "itemCombination", "campClear", "gemCollection", "gemSubmission", "death"):
            continue
        for entry in stream["entries"]:
            selected.append({"kind": stream["kind"], "actor": owner.get("id"), "name": owner.get("name"),
                             **{k: entry[k] for k in ("timestamp", "value", "amount", "context", "target", "x", "y") if k in entry}})
    return sorted(selected, key=lambda e: (e["timestamp"]["round"], e["timestamp"].get("input", 0), e["kind"]))


def capital_messages(archive, side):
    with zipfile.ZipFile(archive) as bundle:
        stdout = bundle.read(f"team-{side.lower()}.stdout").replace(b"\0", b"").decode("utf8", "replace")
    out = []
    for line in stdout.splitlines():
        marker = line.find("capital ")
        if marker >= 0:
            message = line[marker:]
            out.append({"kind": message.split()[1] if len(message.split()) > 1 else "unknown", "message": message})
    return out


def execute(prepared, name, jobs, timeout):
    if prepared["state"].get("validation_only") or prepared["state"].get("forbid_native_execution"):
        raise ValueError("Validation-only prepared data cannot stage or execute a native experiment")
    if not name.startswith("contenders-timetable/"):
        raise ValueError("Prepared experiments belong to contenders-timetable")
    plans = prepared["plans"]
    if not 1 <= jobs <= len(plans) or timeout < 1:
        raise ValueError("Jobs must be positive and no larger than the declared useful native jobs")
    out = exp.exp_dir(name)
    if out.exists():
        raise ValueError("Existing experiment must be inspected, never replaced or repeated")
    out.mkdir(parents=True)
    (out / ".keep").write_text("Active prepared experiment; close after answer/inspection.\n", encoding="utf8", newline="\n")
    captures = out / "captured-inputs"
    captures.mkdir()
    bindings, input_records = {}, []
    for key, item in prepared["selected"].items():
        target = captures / (key.replace("/", "-").replace("@", "-") + "-selected-raw.wasm")
        with target.open("xb") as output:
            output.write(item["blob"])
        descriptor = runner._desc(target)
        if any(descriptor[f] != item["descriptor"][f] for f in ("sha256", "size")):
            raise ValueError("Selected payload changed during staging")
        # Binding requires two descriptor slots. Both refer to the SAME selected
        # raw file. Historical observed roles stay separate; the unselected slot
        # is a schema alias, not an observation or execution of an injected role.
        bindings[key] = runner.Binding(key, descriptor, descriptor, item["blob"], "raw", [])
        input_records.append({"snapshot": key, "selected_role": "raw", "selected_payload": descriptor,
                              "protected_original": item["descriptor"], "unselected_descriptor_slot_is_raw_alias": True,
                              "historical_observed_roles": item["record"].get("observed_original_roles", {})})
    state = prepared["state"]
    opponent_ids = sorted({(p["b"] if p["tags"]["subject"] == "A" else p["a"]).split("@")[0] for p in plans})
    spec = {"name": name, "question": state["question"], "created": utc(), "status": "open",
            "subjects": prepared["profiles"], "opponents": opponent_ids, "maps": ",".join(sorted({p["map"] for p in plans})),
            "sides": "both" if prepared["planned_native_sides"] == ["A", "B"] else prepared["planned_native_sides"][0],
            "requested_sides": prepared["requested_sides"], "planned_native_sides": prepared["planned_native_sides"],
            "rounds": 0, "keep": "none", "games": len(plans),
            "source": state["source"], "engine": state["engine"], "bundle": prepared["bundle"],
            "profile_sources": prepared["profile_sources"], "profile_origins": prepared["profile_origins"],
            "profile_sources_explicit": prepared["profile_sources_explicit"],
            "cross_source_contrast": prepared["cross_source_contrast"], "source_identity_transfer_claim": False,
            "entrants": {**prepared["profile_keys"], **{k.split("@")[0]: k for k in bindings if k not in prepared["profile_keys"].values()}},
            "selected_role": "raw", "role_equivalence_claim": False, "opponent_source_read": False,
            "source_read_or_prepared": False, "prepared_input": prepared["prepared"], "input_records": input_records,
            "plans": plans, "maps_pinned": prepared["maps"], "reused_control_count": len(prepared["controls"]),
            "control_evidence": prepared["control_evidence"], "driver": own_descriptor(Path(__file__).resolve())}
    write(out / "spec.json", spec)
    launch = {"launcher_pid": os.getpid(), "launcher_started_utc": utc(), "experiment": name,
              "phase": "entering_shared_runner", "planned_native_count": len(plans), "new_native_completed_count": 0,
              "worker_start_records": "native/batches/*/process-start.json", "native_job_results": "native/games.jsonl",
              "phase_limit": "entering_shared_runner may be queued for slots; only a process-start record confirms a worker launch"}
    write(out / "launcher.json", launch)
    print(json.dumps({"experiment": name, "source": state["source"], "launcher_pid": os.getpid(),
                      "new_native_planned": len(plans), "reused_controls": len(prepared["controls"]),
                      "source_read_or_prepared": False, "worker_start_records": launch["worker_start_records"]}), flush=True)
    try:
        result = runner._run(plans, bindings, out / "native", "raw", jobs, timeout,
                             {"kind": "registered-timetable-protected-prepared-application", "experiment": name,
                              "spec": runner._desc(out / "spec.json"), "protected_recipe": prepared["prepared"],
                              "no_source_read_or_build": True})
    except BaseException as error:
        cleanup = runner.confirm_pending_cleanup()
        launch.update(phase="failed_or_interrupted", finished_utc=utc(), exception=repr(error),
                      owned_cleanup_confirmed=cleanup)
        write(out / "launcher.json", launch)
        raise
    cleanup = runner.confirm_pending_cleanup()
    launch.update(phase="native_returned", finished_utc=utc(), owned_cleanup_confirmed=cleanup,
                  execution_pass=result["execution_pass"], new_native_completed_count=len(result["rows"]))
    write(out / "launcher.json", launch)
    if not cleanup or not result["stopped"] or result["jobs"] or not result["execution_pass"]:
        raise ValueError("Prepared native execution/cleanup failed; inspect retained raw output, no complete application claim")
    rows = [copy.deepcopy(c["audit"]["row"]) for c in prepared["controls"]]
    audits = [{**copy.deepcopy(c["audit"]), "reused_control": {k: v for k, v in c.items() if k != "audit"}}
              for c in prepared["controls"]]
    for row in result["rows"]:
        if not row["measurement_complete"] or not row["execution_complete"] or row.get("truncated"):
            raise ValueError("Prepared game lacks a complete untruncated native measurement")
        side = row["tags"]["subject"]
        view = native_view(row)
        rows.append(view)
        stats_path = runner._descriptor_path(row["statistics"])
        archive_path = runner._descriptor_path(row["archive"])
        stats = json.loads(stats_path.read_bytes())
        if stats["match"]["mapHash"] != next(p["map_sha256"] for p in plans if p["id"] == row["id"]):
            raise ValueError("Native observed map hash differs from the prepared recipe")
        audits.append({"row": view, "fresh_native": True, "native_statistics_sha256": row["statistics"]["sha256"],
                       "native_archive_sha256": row["archive"]["sha256"], "native_match": stats["match"],
                       "fuel_max": row[side]["fuel_max"], "fuel_at_budget": row[side]["fuel_at_budget"],
                       "champions": production(stats, side), "focused_events": focused_events(stats, side),
                       "capital_messages": capital_messages(archive_path, side),
                       "late_window": {"from_round": 1500, "observed": row["rounds"] >= 1500}})
    core.append_jsonl(out / "rows.jsonl", rows)
    summary = exp.summarize(spec, rows)
    summary.update(source=state["source"], fresh_native_games=len(result["rows"]), reused_control_count=len(prepared["controls"]),
                   profile_sources=prepared["profile_sources"], profile_origins=prepared["profile_origins"],
                   cross_source_contrast=prepared["cross_source_contrast"], source_identity_transfer_claim=False,
                   native_production=audits, owned_cleanup_confirmed=True, source_read_or_prepared=False,
                   selected_role="raw", role_equivalence_claim=False,
                   native_side_coverage={"requested": prepared["requested_sides"],
                                         "planned": prepared["planned_native_sides"],
                                         "completed": sorted({row["tags"]["subject"] for row in result["rows"]}),
                                         "by_profile": {profile["label"]: sorted({row["tags"]["subject"] for row in result["rows"]
                                                         if row["tags"]["variant"] == profile["label"]})
                                                        for profile in prepared["profiles"]}},
                   limits=["A conditional nomination or stage log is not a completed investment cycle.",
                           "Recall/channel, equipped readback, exact movement and later work need focused replay-state corroboration.",
                           "Native damage totals are not attributed to named camps.",
                           "Explicit profile_sources preserve declared origins; a cross-source contrast never transfers source identity or old-row qualification.",
                           "This selected-key application does not establish broad health or strength against current main."])
    write(out / "summary.json", summary)
    target = ROOT / "results/contenders/timetable/independent" / state["source"] / (name.split("/")[1] + ".json")
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        raise ValueError("Durable application result already exists; do not overwrite it")
    write(target, {"spec": spec, "summary": summary})
    print(json.dumps({"evidence": str(target.relative_to(ROOT)), "fresh_native_games": len(result["rows"]),
                      "reused_controls": len(prepared["controls"]), "rows": rows}, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepared", type=Path, default=DEFAULT_PREPARED)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check-only", action="store_true")
    mode.add_argument("--run", metavar="OWNER/NAME")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=150)
    args = parser.parse_args()
    os.environ["CAMBC_OFFLINE"] = "1"
    os.environ["CAMBC_NO_UPDATE_CHECK"] = "1"
    prepared = load_prepared(args.prepared)
    if args.check_only:
        print(json.dumps({"valid": True, "source": prepared["state"]["source"],
                          "payloads": len(prepared["selected"]), "new_native_planned": len(prepared["plans"]),
                          "exact_reused_controls": len(prepared["controls"]), "selected_role": "raw",
                          "profile_sources": prepared["profile_sources"], "profile_origins": prepared["profile_origins"],
                          "profile_sources_explicit": prepared["profile_sources_explicit"],
                          "cross_source_contrast": prepared["cross_source_contrast"],
                          "requested_sides": prepared["requested_sides"],
                          "planned_native_sides": prepared["planned_native_sides"],
                          "validation_only": bool(prepared["state"].get("validation_only")),
                          "role_equivalence_claim": False, "source_read_or_prepared": False,
                          "plans": prepared["plans"]}, indent=2))
    else:
        execute(prepared, args.run, args.jobs, args.timeout)


if __name__ == "__main__":
    main()
