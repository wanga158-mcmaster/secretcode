"""Check a frozen application panel's OFF module against eight old whole replays.

Only selected executable bytes are used for opponents. No bot is rebuilt here.
The old proof retains expected payload hashes after native scratch is pruned.
"""
import argparse
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
from native_recon import write

MAGIC = b"\0asm\x01\0\0\0"
MAIN = "anchors/main-v44@b3ff333b6bcf"
STARTER = "anchors/starter-cpp@3785135fc7c7"
OPPONENTS = {"anchors/main-v44": MAIN, "anchors/starter-cpp": STARTER}
RAW_PINS = {MAIN: ("5aa475177b0cae4261cab872bfa25c282edbae3b651d3689abc6ce9a34118c3b", 1108869),
            STARTER: ("c9a0ca35cf13686680ef4ea00d0bb99f654a79be78382bdb3283604d5fde9795", 3224431)}
PRIOR_PATH = ROOT / "results/contenders/timetable/independent/e035a183bbe6/fresh-controls/off-preservation.json"
PRIOR_SHA = "dc203bcf9545b43f3ae003647395f05d59f55d3528937c40c29b4be3e9b0b2d5"
QUALIFIED_PATH = ROOT / "results/contenders/timetable/independent/6eedf9797c63/off-preservation-01.json"
QUALIFIED_SHA = "a876f4139c9278060efbf752a57907b240122febb21b63b4e7ac80c98363f480"
BUNDLE_SHA = "1c74c8a5b6039652edb173e050f8dcf6cdab620100b683268371c4ebb3537347"
SCOPE = "Historical BigWorld/ElkRing OFF preservation against opaque v44/starter; no current-main or strength claim."


def checked(pin):
    path = runner._descriptor_path(pin).resolve()
    actual = runner._desc(path)
    if any(actual[field] != pin[field] for field in ("sha256", "size")):
        raise ValueError("Prepared OFF input changed: " + str(path))
    return path, actual


def selected(record, expected_key=None):
    key = record["snapshot"]
    if record.get("selected_role") != "raw" or (expected_key is not None and key != expected_key):
        raise ValueError("OFF preservation requires the exact RAW-selected key")
    path, descriptor = checked(record)
    blob = path.read_bytes()
    if len(blob) > runner.MAX_FILE or blob[:8] != MAGIC or hashlib.sha256(blob).hexdigest() != descriptor["sha256"]:
        raise ValueError("Prepared OFF payload is not unchanged bounded WASM v1")
    return {"record": record, "descriptor": descriptor, "blob": blob}


def reference_checks(pin=None):
    if runner._desc(ROOT / "docs/reference/current-version.json")["sha256"] != BUNDLE_SHA:
        raise ValueError("Historical OFF qualification does not cover the current bundle")
    pin = pin or runner._desc(PRIOR_PATH)
    path, descriptor = checked(pin)
    if descriptor["sha256"] != PRIOR_SHA:
        raise ValueError("OFF reference must be the exact retained eight-key proof")
    prior = json.loads(path.read_bytes())
    if (prior.get("coverage") != 8 or prior.get("masked_bytes") != 0 or
            not all(prior.get(field) is True for field in ("all_payloads_equal", "all_terminal_equal", "all_health_clear"))):
        raise ValueError("Retained OFF proof is incomplete")
    expected = {(m, o, s) for m in ("01big-world", "00elkring") for o in OPPONENTS for s in "AB"}
    checks = {tuple(check["key"]): check for check in prior["checks"]}
    if len(prior["checks"]) != 8 or set(checks) != expected:
        raise ValueError("OFF proof does not bind exactly the eight historical keys")
    maps = {}
    for key, check in checks.items():
        map_name, opponent, side = key
        old = check["off"]
        if (old["opponent"] != OPPONENTS[opponent] or old["map"] != map_name or old["side"] != side or
                any(old["health"].values()) or not re.fullmatch("[0-9a-f]{64}", check["reference_payload_sha256"])):
            raise ValueError("Historical OFF key, expected whole payload or both-team health differs")
        match = old["native_match"]
        path = core.map_path(map_name)
        map_pin = runner._desc(path)
        if match["engineVersion"] != games.ENGINE or match["mapHash"] != map_pin["sha256"]:
            raise ValueError("Historical OFF engine/map differs from the pinned local data")
        maps[map_name] = map_pin
    return prior, descriptor, checks, maps


def identity_qualification(off, prior, proof_pin, references):
    """Reuse a prior qualification only for the exact selected executable bytes."""
    current = off["descriptor"]["sha256"]
    candidates = ((prior["off_modules"]["bot.wasm"], prior["source"]),
                  (prior["reference_modules"]["bot.wasm"], prior["reference_source"]))
    for digest_value, original_key in candidates:
        if current == digest_value:
            return {"qualified": True, "kind": "exact-selected-raw-identity", "evidence": proof_pin,
                    "original_snapshot": original_key, "original_source": original_key.rsplit("@", 1)[1], "original_coverage": 8,
                    "fresh_native_required": 0, "reused_historical_keys": 8}
    # This report independently measured the same eight whole-payload/terminal/
    # both-health checks after recon-ingress's earlier raw_equal=false preflight.
    if QUALIFIED_PATH.is_file():
        pin = runner._desc(QUALIFIED_PATH)
        if pin["sha256"] == QUALIFIED_SHA:
            report = json.loads(QUALIFIED_PATH.read_bytes())
            spec, summary = report["spec"], report["summary"]
            old = [b for b in spec["bindings"] if b["snapshot"] == "contenders/timetable@6eedf9797c63"]
            same = len(old) == 1 and old[0]["raw"]["sha256"] == current and old[0]["raw"]["size"] == off["descriptor"]["size"]
            checks = {tuple(c["key"]): c for c in summary["checks"]}
            qualified = (spec["prior_proof"] == proof_pin and len(summary["checks"]) == 8 and set(checks) == set(references)
                         and all(summary.get(f) is True for f in ("all_payloads_equal", "all_terminal_equal", "all_health_clear")))
            for key, check in checks.items():
                qualified &= (check["payload_equal"] is True and check["terminal_equal"] is True and not any(check["health"].values())
                              and check["payload_sha256"] == check["expected_payload_sha256"] == references[key]["reference_payload_sha256"])
            if same and qualified:
                return {"qualified": True, "kind": "exact-selected-raw-identity", "evidence": pin,
                        "original_snapshot": old[0]["snapshot"], "original_source": "6eedf9797c63", "original_coverage": 8,
                        "fresh_native_required": 0, "reused_historical_keys": 8}
    return {"qualified": False, "fresh_native_required": 8, "reused_historical_keys": 0}


def plans_for(key, references, maps):
    plans, by_id = [], {}
    for ref_key, check in references.items():
        map_name, opponent, side = ref_key
        other = OPPONENTS[opponent]
        ident = f"game-{len(plans):06d}"
        plans.append({"id": ident, "a": key if side == "A" else other, "b": other if side == "A" else key,
                      "map": map_name, "map_file": str(ROOT / maps[map_name]["path"]), "map_sha256": maps[map_name]["sha256"],
                      "tags": {"subject": side, "opponent": opponent}})
        by_id[ident] = check
    return plans, by_id


def load_prepared(path):
    """Validate protected OFF bytes and historical keys without a build or game."""
    path = Path(path).resolve()
    pin = runner._desc(path)
    state = json.loads(path.read_bytes())
    source = state["source"]
    if state.get("schema") != "timetable-off-prepared/v1" or re.fullmatch("[0-9a-f]{12}", source) is None:
        raise ValueError("Prepared OFF schema/source differs")
    if state["engine"] != games.ENGINE:
        raise ValueError("Prepared OFF engine differs")
    _, bundle = checked(state["bundle"])
    if bundle != runner._desc(ROOT / "docs/reference/current-version.json"):
        raise ValueError("Prepared OFF bundle differs")
    key = "contenders/timetable@" + source
    payloads = {key: selected(state["off"], key)}
    records = state["historical_opponents"]
    if len(records) != 2 or {r["snapshot"] for r in records} != set(RAW_PINS):
        raise ValueError("Prepared OFF historical opponent keys differ")
    for record in records:
        value = selected(record)
        if tuple(value["descriptor"][f] for f in ("sha256", "size")) != RAW_PINS[record["snapshot"]]:
            raise ValueError("Prepared historical opponent RAW differs")
        payloads[record["snapshot"]] = value
    prior, proof_pin, references, maps = reference_checks(state["reference_evidence"])
    plans, by_id = plans_for(key, references, maps)
    qualification = identity_qualification(payloads[key], prior, proof_pin, references)
    return {"source": source, "off_key": key, "selected": payloads, "plans": plans, "references": by_id,
            "prior_proof": proof_pin, "maps": maps, "bundle": bundle, "prepared": pin, "state": state,
            "identity_qualification": qualification, "historical_only": True, "scope": SCOPE}


def load_panel(panel_name):
    """Legacy positional panel mode; selected RAW alone authorizes execution."""
    panel_path = exp.exp_dir(panel_name) / "spec.json"
    panel = json.loads(panel_path.read_bytes())
    source = panel["source"]
    if re.fullmatch("[0-9a-f]{12}", source) is None:
        raise ValueError("Legacy panel source must be its exact twelve-hex identity")
    key = "contenders/timetable@" + source
    records = [b for b in panel["bindings"] if b["snapshot"] == key]
    if len(records) != 1:
        raise ValueError("Require exactly the frozen unmodified OFF module")
    record = records[0]
    own = {"snapshot": key, "selected_role": "raw", **record["raw"],
           "observed_original_roles": {r: record[r] for r in ("raw", "injected") if r in record}}
    payloads = {key: selected(own, key)}
    for other, path in ((MAIN, ROOT / ".cache/contenders-audit/opaque-reference/main-v44-raw.wasm"),
                        (STARTER, ROOT / ".cache/arena/snap/3785135fc7c7/bots/anchors/starter-cpp/.cambc/build-release/bot.wasm")):
        desc = runner._desc(path)
        if tuple(desc[f] for f in ("sha256", "size")) != RAW_PINS[other]:
            raise ValueError("Legacy opaque historical opponent RAW differs")
        payloads[other] = selected({"snapshot": other, "selected_role": "raw", **desc})
    _, proof_pin, references, maps = reference_checks()
    plans, by_id = plans_for(key, references, maps)
    return {"source": source, "off_key": key, "selected": payloads, "plans": plans, "references": by_id,
            "prior_proof": proof_pin, "maps": maps, "panel": runner._desc(panel_path), "state": {},
            "identity_qualification": {"qualified": False, "fresh_native_required": 8, "reused_historical_keys": 0},
            "historical_only": True, "scope": SCOPE}


def check_view(loaded):
    return {"valid": True, "source": loaded["source"], "off_snapshot": loaded["off_key"],
            "selected_raw": loaded["selected"][loaded["off_key"]]["descriptor"], "historical_key_coverage": 8,
            "identity_qualification": loaded["identity_qualification"],
            "fresh_native_required": loaded["identity_qualification"]["fresh_native_required"],
            "reused_historical_keys": loaded["identity_qualification"]["reused_historical_keys"], "selected_role": "raw",
            "role_equivalence_claim": False, "scope": SCOPE, "new_games": 0, "builds": 0}


def publication_target(source):
    target = ROOT / "results/contenders/timetable/independent" / source / "off-preservation-01.json"
    if target.exists():
        raise ValueError("Existing OFF qualification is immutable; inspect or reuse it instead of overwriting: " + str(target))
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("panel", nargs="?")
    parser.add_argument("name", nargs="?")
    parser.add_argument("--prepared", type=Path, help="Protected explicit OFF RAW recipe; one positional argument is its experiment name")
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--run", action="store_true")
    action.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    os.environ["CAMBC_OFFLINE"] = "1"
    os.environ["CAMBC_NO_UPDATE_CHECK"] = "1"
    if args.prepared:
        if args.name is not None:
            parser.error("Prepared mode accepts only one positional experiment name")
        loaded = load_prepared(args.prepared)
        name = args.panel
    else:
        if not args.panel:
            parser.error("Legacy mode requires its panel")
        loaded = load_panel(args.panel)
        name = args.name
    if args.check_only:
        print(json.dumps(check_view(loaded), indent=2), flush=True)
        return 0
    if not name:
        parser.error("--run requires an experiment name")
    if loaded["state"].get("validation_only") or loaded["state"].get("forbid_native_execution"):
        raise ValueError("Validation-only OFF recipe cannot run or register an experiment")
    source, key = loaded["source"], loaded["off_key"]
    target = publication_target(source)  # Before experiment registration/capture.
    plans, references = loaded["plans"], loaded["references"]
    out = exp.exp_dir(name)
    if out.exists():
        raise ValueError("Existing deterministic OFF experiment")
    out.mkdir(parents=True)
    inputs = out / "captured-inputs"
    inputs.mkdir()
    bindings, input_records = {}, []
    for snapshot, value in loaded["selected"].items():
        path = inputs / (snapshot.replace("/", "-").replace("@", "-") + "-selected-raw.wasm")
        path.write_bytes(value["blob"])
        desc = runner._desc(path)
        bindings[snapshot] = runner.Binding(snapshot, desc, desc, value["blob"], "raw", [])
        input_records.append({"snapshot": snapshot, "selected_role": "raw", "selected_payload": desc,
                              "protected_original": value["descriptor"], "unselected_descriptor_slot_is_raw_alias": True,
                              "historical_observed_roles": value["record"].get("observed_original_roles", {})})
    qualified = loaded["identity_qualification"]["qualified"]
    spec = {"name": name, "question": "Does current default OFF preserve the eight audited old whole game payloads bit for bit?",
            "created": time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime()), "status": "open",
            "subjects": [{"label": "off", "bot": "contenders/timetable", "set": {}, "weights": None}],
            "opponents": ["anchors/main-v44", "anchors/starter-cpp"], "maps": "01big-world,00elkring",
            "sides": "both", "rounds": 0, "keep": "none", "games": 0 if qualified else 8, "source": source,
            "entrants": {"off": key, **OPPONENTS},
            "selected_role": "raw", "opponent_source_read": False, "source_prepared": False,
            "prior_proof": loaded["prior_proof"], "bindings": [b.record() for b in bindings.values()],
            "input_records": input_records, "role_equivalence_claim": False, "scope": SCOPE,
            "historical_only": True, "current_main_strength_claim": False,
            "identity_qualification": loaded["identity_qualification"], "historical_key_coverage": 8,
            "new_native_planned": 0 if qualified else 8,
            "imported_reused_historical_count": 8 if qualified else 0}
    spec.update({field: loaded[field] for field in ("panel", "prepared", "bundle") if field in loaded})
    write(out / "spec.json", spec)
    if qualified:
        # Empty rows are intentional: eight old qualifications are reused, not
        # eight newly played games. Preserve their original provenance per key.
        (out / "rows.jsonl").write_text("", encoding="utf8")
        summary = exp.summarize(spec, [])
        summary.update({"checks": [{"key": c["key"], "expected_payload_sha256": c["reference_payload_sha256"],
                                     "preservation_basis": "exact RAW bytes of qualified historical executable",
                                     "original_qualification": loaded["identity_qualification"],
                                     "raw_identity_mapping": {"new_source": source, "new_snapshot": key,
                                                              "selected_raw_sha256": loaded["selected"][key]["descriptor"]["sha256"],
                                                              "qualified_source": loaded["identity_qualification"]["original_source"],
                                                              "qualified_snapshot": loaded["identity_qualification"]["original_snapshot"],
                                                              "evidence": loaded["identity_qualification"]["evidence"]}}
                                    for c in references.values()],
                        "all_payloads_equal": True, "all_terminal_equal": True, "all_health_clear": True,
                        "fresh_native_count": 0, "new_native_count": 0, "reused_historical_keys": 8,
                        "imported_reused_historical_count": 8, "new_health_measured": False})
        write(out / "summary.json", summary)
        target.parent.mkdir(parents=True, exist_ok=True)
        write(target, {"spec": spec, "summary": summary})
        print(json.dumps({**check_view(loaded), "registered": name, "fresh_native_count": 0}), flush=True)
        return 0
    try:
        result = runner._run(plans, bindings, out / "native", "raw", 4, 150,
            {"kind": "registered-frozen-timetable-off-preservation", "spec": runner._desc(out / "spec.json"),
             "opponent_source_unread": True})
    finally:
        if not runner.confirm_pending_cleanup():
            raise ValueError("Native cleanup unconfirmed")
    assert result["execution_pass"] and result["stopped"] and not result["jobs"]
    if len(result["rows"]) != 8 or {row["id"] for row in result["rows"]} != set(references):
        raise ValueError("OFF execution did not return the exact eight declared keys")
    rows, checks = [], []
    for row in result["rows"]:
        assert row["measurement_complete"] and row["execution_complete"]
        side = row["tags"]["subject"]
        other = "B" if side == "A" else "A"
        old = references[row["id"]]
        with zipfile.ZipFile(runner._descriptor_path(row["archive"])) as archive:
            payload_sha = hashlib.sha256(archive.read("replay.repl26a")).hexdigest()
        old_row = old["off"]
        equal = row["winner"] == old_row["winner"] and row["rounds"] == old_row["rounds"]
        for current, previous in ((row[side], old_row["subject_metrics"]), (row[other], old_row["opponent_metrics"])):
            equal &= all(current[k] == previous[k] for k in ("gems", "gold", "kills", "deaths", "clears", "faults", "exhausted", "errors"))
        view = {"map": row["map"], "side": side, "subject": "off", "opponent": row["tags"]["opponent"],
                "result": "win" if row["winner"] == side else "loss", "rounds": row["rounds"],
                "gems": row[side]["gems"], "opp_gems": row[other]["gems"],
                "health": {f: 1 for f in games.health(row, side)}, "opp_health": {f: 1 for f in games.health(row, other)}}
        rows.append(view)
        checks.append({"key": old["key"], "payload_sha256": payload_sha,
                       "expected_payload_sha256": old["reference_payload_sha256"],
                       "payload_equal": payload_sha == old["reference_payload_sha256"], "terminal_equal": equal,
                       "health": {s: games.health(row, s) for s in "AB"}})
    core.append_jsonl(out / "rows.jsonl", rows)
    summary = exp.summarize(spec, rows)
    summary["checks"] = checks
    summary["all_payloads_equal"] = all(c["payload_equal"] for c in checks)
    summary["all_terminal_equal"] = all(c["terminal_equal"] for c in checks)
    summary["all_health_clear"] = all(not any(c["health"].values()) for c in checks)
    summary.update(fresh_native_count=len(checks), new_native_count=len(checks), reused_historical_keys=0,
                   imported_reused_historical_count=0, new_health_measured=True)
    write(out / "summary.json", summary)
    target.parent.mkdir(parents=True, exist_ok=True)
    write(target, {"spec": spec, "summary": summary})
    print(json.dumps({"source": source, "coverage": len(checks),
                      **{k: summary[k] for k in ("all_payloads_equal", "all_terminal_equal", "all_health_clear")}}), flush=True)
    return int(not all(summary[k] for k in ("all_payloads_equal", "all_terminal_equal", "all_health_clear")))


if __name__ == "__main__":
    raise SystemExit(main())
