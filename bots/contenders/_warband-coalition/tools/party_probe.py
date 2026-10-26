"""Bounded native actual Brain party caller on a fresh Elk; no ordinary-planner claim."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools/arena"))
import core
import games


def fields(line):
    return {key: value for key, value in (part.split("=", 1) for part in line.split()[1:] if "=" in part)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--on-set", action="append", default=[], metavar="KNOB=VALUE")
    parser.add_argument("--normal-farm", action="store_true", help="Full production Brain from prepared state, without stipulated target/shortlist")
    parser.add_argument("--with-basic", action="store_true", help="Compare ordinary OFF, basic-only party and stationary-casts party from one source capture")
    args = parser.parse_args()
    if args.with_basic and not args.normal_farm:
        parser.error("--with-basic requires --normal-farm")
    out = args.out.resolve()
    if out.exists():
        raise SystemExit("refusing existing output: " + str(out))
    maps = ROOT / ".cache/contenders/warband/party-probe/maps"
    maps.mkdir(parents=True, exist_ok=True)
    spec = {"width": 50, "height": 50, "symmetry": "ROTATIONAL180", "champs": 3,
            "spawnA": [0, 0, 3, 3], "altars": [[0, 49]],
            "camps": [["elk", 10, 10], ["gold_cluster", 20, 30]]}
    sp = maps / "fresh-elk-party.json"
    mp = sp.with_suffix(".map26a")
    sp.write_text(json.dumps(spec), encoding="utf-8")
    builder = ROOT / "bots/main/foundation/tools/combat/mkmap.mjs"
    proc = subprocess.run([str(core.NODE), "--no-warnings", str(builder), str(sp), str(mp)], capture_output=True, text=True)
    if proc.returncode:
        raise SystemExit(proc.stderr[-3000:])
    bot = core.bot("contenders/_warband-party-probe")
    contents = core.read_closure(bot)
    print(json.dumps({"event": "source_captured", "base_version": core.version_of(contents), "source_files": len(contents)}), flush=True)
    arms = ("off", "basic", "casts") if args.with_basic else ("off", "on")
    labels, preparations, arm_overrides = [], [], {}
    for side in ("A", "B"):
        for arm in arms:
            overrides = {"probe_active_side": "0" if side == "A" else "1", "coalition_focus": "false",
                         "coalition_party": "true" if arm != "off" else "false", "coalition_party_trace": "true"}
            if args.normal_farm:
                overrides["probe_normal_farm"] = "true"
            if arm != "off":
                overrides.update(dict(value.split("=", 1) for value in args.on_set))
            if args.with_basic:
                # Arm-defining flags are final, even if a generic override names one.
                overrides.update({"probe_active_side": "0" if side == "A" else "1",
                                  "coalition_focus": "false", "coalition_party": "true" if arm != "off" else "false",
                                  "probe_normal_farm": "true", "probe_compact_actions": "true",
                                  "coalition_party_casts": "true" if arm == "casts" else "false"})
                if arm != "off":
                    overrides.update({"coalition_party_rotation": "true", "coalition_party_scout": "true"})
            arm_overrides[f"{arm}-{side}"] = overrides
            labels.append((arm, side))
            preparations.append((bot, overrides, f"{arm}-{side}", contents))
    entries = dict(zip(labels, core.prepare_many(preparations)))
    for entry in entries.values():
        if not entry.ok:
            raise SystemExit(entry.log[-5000:])
    specifications = []
    for side in ("A", "B"):
        off = entries[("off", side)]
        for arm in arms:
            subject = entries[(arm, side)]
            a, b = (subject, off) if side == "A" else (off, subject)
            specifications.append(games.GameSpec(a, b, str(mp), {"arm": arm, "subject": side}))
    rows = games.play(specifications, out, rounds=80, keep_replays="all", quiet=True, resume=False)
    observations, failures = [], []
    for specification, row in zip(specifications, rows):
        tag = specification.tags
        if not row.get("replay"):
            failures.append({**tag, "reason": "native launch failure", "row": row})
            observations.append({**tag, "row": row})
            continue
        rp = Path(row["replay"])
        if not rp.is_absolute():
            rp = ROOT / rp
        with zipfile.ZipFile(rp) as archive:
            stdout = archive.read(f"team-{tag['subject'].lower()}.stdout").replace(b"\0", b"").decode("utf-8", "replace")
            raw = archive.read(f"team-{tag['subject'].lower()}.fuel")
        if raw[:8] != b"a62leuf\0" or (len(raw) - 13) % 8:
            raise ValueError("invalid native fuel stream")
        fuel = struct.unpack_from(f"<{(len(raw)-13)//8}Q", raw, 13)
        # The producer carries both team thresholds even in each team's named stream.
        budget = fuel[3 if tag["subject"] == "A" else 7]
        ordinary = fuel[14:]
        at_budget = sum(value >= budget for value in ordinary)
        events = [fields(line) for line in stdout.splitlines() if line.startswith("GP ")]
        traces = [line for line in stdout.splitlines() if line.startswith("party ")]
        seeds = [event for event in events if event.get("phase") == "seed"]
        if not args.normal_farm and (len(seeds) != 2 or any(int(seed["need"]) < 2 or seed["alt_need"] != "1" or seed["target_hp"] != "2400" for seed in seeds)):
            failures.append({**tag, "reason": "actual scored caller prerequisites failed", "seeds": seeds})
        after = [event for event in events if event.get("phase") == "after"]
        target = seeds[0]["group"] if seeds else "-2"
        if args.normal_farm:
            if not any(event.get("phase") == "normal" for event in events):
                failures.append({**tag, "reason": "no actual normal-pipeline observations"})
            if args.with_basic and tag["arm"] != "off":
                fight = [event for event in events if event.get("phase") == "normal" and event.get("why") == "party-fight"]
                if not fight:
                    failures.append({**tag, "reason": "no actual normal party production"})
                if any(int(event["otherawake"]) != 0 for event in fight):
                    failures.append({**tag, "reason": "personally visible other camp awake during party production"})
                for who in ("leader", "helper"):
                    positions = {(event["x"], event["y"]) for event in fight if event["who"] == who}
                    if len(positions) != 1:
                        failures.append({**tag, "reason": "party production position changed or unobserved", "who": who, "positions": sorted(positions)})
                if tag["arm"] == "casts" and not any(event.get("C") == "0" for event in fight):
                    failures.append({**tag, "reason": "no post-party action cast allowance consumed"})
        elif tag["arm"] == "off":
            if any(event["camp"] != "2400" or event["attacked"] != "0" or event["cast"] != "0" for event in after):
                failures.append({**tag, "reason": "OFF actual party caller changed camp"})
        else:
            def witnessed(who, phase, **terms):
                return any(event.get("who") == who and event.get("phase") == phase and all(event.get(key) == value for key, value in terms.items()) for event in events)
            required = {
                "leader_offer": witnessed("leader", "after", p="1", Lg=target, Ls="3"),
                "helper_accept": witnessed("helper", "after", p="1", Hg=target, Hs="0"),
                "helper_stage": witnessed("helper", "after", p="1", moved="1"),
                "leader_commit_production": witnessed("leader", "after", p="2", attacked="1"),
                "helper_production": witnessed("helper", "after", p="2", attacked="1"),
                "helper_release": witnessed("helper", "after", p="3", Hg="-1", Hs="3"),
                "leader_capture": witnessed("leader", "after", Lg=target, Ls="1"),
                "leader_payout": witnessed("leader", "payout", gems="15", gold="90")}
            if not all(required.values()):
                failures.append({**tag, "reason": "incomplete actual party continuation", "witnesses": required})
            if any(event.get("who") == "helper" and event.get("Hg") == target and event.get("Hs") == "1" for event in events):
                failures.append({**tag, "reason": "helper became extra target capturer"})
        if "error" in stdout.lower() or at_budget:
            failures.append({**tag, "reason": "native error/exhaustion", "stdout_tail": stdout[-3500:]})
        if row[tag["subject"]].get("faults", 0):
            failures.append({**tag, "reason": "native subject fault", "faults": row[tag["subject"]]["faults"]})
        normal = [event for event in events if event.get("phase") == "normal"]
        cleared = [event for event in normal if float(event["camp"]) == 0]
        rewards = [event for event in normal if int(event["gems"]) >= 15]
        metrics = None
        if args.with_basic:
            clear_round = min((int(event["r"]) for event in cleared), default=None)
            metrics = {"first_clear_round": clear_round,
                       "queried_hp_at_clear": {event["who"]: float(event["hp"]) for event in normal if int(event["r"]) == clear_round},
                       "first_personal_reward_round": min((int(event["r"]) for event in rewards), default=None),
                       "first_personal_recipient": rewards[0]["who"] if rewards else None,
                       "first_committed_round": min((int(event["r"]) for event in normal if event["p"] == "2"), default=None),
                       "party_turns_cast_allowance_consumed": sum(event["why"] == "party-fight" and event["C"] == "0" for event in normal),
                       "scope": "Post-production public SDK observations; exact native action/cast identity requires saved replay decoding."}
        observations.append({**tag, "replay": str(rp.relative_to(ROOT)), "events": events, "traces": traces,
                             "ordinary_peak": max(ordinary, default=0), "ordinary_budget": budget,
                             "ordinary_budget_index": 3 if tag["subject"] == "A" else 7,
                             "at_budget": at_budget, "stdout": stdout, "row": row,
                             **({"normal_metrics": metrics} if args.with_basic else {})})
    purpose = ("actual whole Brain pipeline from legal prepared state; no target/need/alternative stipulation, no normal pool reach claim" if args.normal_farm else
               "actual conditional party caller; actual scoring and native setup, no ordinary planner selection claim")
    result = {"purpose": purpose, "normal_farm": args.normal_farm,
              "with_basic": args.with_basic, "arms": arms, "arm_overrides": arm_overrides,
              "source_capture_version": core.version_of(contents),
              "rounds": 80, "epoch": 30, "on_overrides": args.on_set,
              "entries": {f"{arm}-{side}": entry.key for (arm, side), entry in entries.items()},
              "source_closure_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(body).hexdigest() for path, body in contents.items()},
              "driver_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "map_spec": spec, "map_sha256": hashlib.sha256(mp.read_bytes()).hexdigest(),
              "bundle": json.loads((ROOT / "docs/reference/current-version.json").read_text()),
              "games": observations, "failures": failures, "passed": not failures}
    (out / "probe.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    print(json.dumps({"out": str(out), "games": len(rows), "passed": not failures, "failures": failures}))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
