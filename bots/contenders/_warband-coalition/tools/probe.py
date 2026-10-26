"""Bounded native actual fight_enemies contact-coalition caller, both subject sides."""
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


def fuel_summary(fuel: tuple[int, ...], subject: str) -> tuple[int, int, int]:
    if subject not in ("A", "B") or len(fuel) < 14:
        raise ValueError("invalid fuel subject/header")
    ordinary = fuel[14:]
    budget = fuel[3 if subject == "A" else 7]
    return max(ordinary, default=0), budget, sum(value >= budget for value in ordinary)


def fields(line: str) -> dict:
    return {key: value for key, value in (part.split("=", 1) for part in line.split()[1:] if "=" in part)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists():
        raise SystemExit("refusing existing output: " + str(out))
    maps = ROOT / ".cache/contenders/warband/coalition-probe/maps"
    maps.mkdir(parents=True, exist_ok=True)
    spec = {"width": 30, "height": 30, "symmetry": "ROTATIONAL180", "champs": 3,
            "spawnA": [0, 0, 3, 3], "altars": [[0, 29]], "camps": [["gold_cluster", 7, 6]]}
    sp = maps / "contact-coalition-prep.json"
    mp = sp.with_suffix(".map26a")
    sp.write_text(json.dumps(spec), encoding="utf-8")
    builder = ROOT / "bots/main/foundation/tools/combat/mkmap.mjs"
    built = subprocess.run([str(core.NODE), "--no-warnings", str(builder), str(sp), str(mp)],
                           capture_output=True, text=True)
    if built.returncode:
        raise SystemExit(built.stderr[-3000:])
    bot = core.bot("contenders/_warband-coalition-probe")
    contents = core.read_closure(bot)
    print(json.dumps({"event": "source_captured", "base_version": core.version_of(contents),
                      "source_files": len(contents)}), flush=True)
    preparations, labels = [], []
    for side in ("A", "B"):
        for arm in ("off", "on"):
            overrides = {"probe_active_side": "0" if side == "A" else "1",
                         "coalition_focus": "true" if arm == "on" else "false", "coalition_trace": "true"}
            label = f"{arm}-{side}"
            labels.append((arm, side))
            preparations.append((bot, overrides, label, contents))
    prepared = core.prepare_many(preparations)
    entries = dict(zip(labels, prepared))
    for entry in prepared:
        if not entry.ok:
            raise SystemExit(entry.log[-5000:])
    specifications = []
    for side in ("A", "B"):
        off = entries[("off", side)]
        for arm in ("off", "on"):
            subject = entries[(arm, side)]
            a, b = (subject, off) if side == "A" else (off, subject)
            specifications.append(games.GameSpec(a, b, str(mp), {"arm": arm, "subject": side}))
    rows = games.play(specifications, out, rounds=56, keep_replays="all", quiet=True, resume=False)
    observations, failures = [], []
    for specification, row in zip(specifications, rows):
        tag = specification.tags
        if not row.get("replay"):
            failures.append({**tag, "reason": "native launch/record failure", "row": row})
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
        fuel = struct.unpack_from(f"<{(len(raw) - 13) // 8}Q", raw, 13)
        peak, budget, at_budget = fuel_summary(fuel, tag["subject"])
        events = [fields(line) for line in stdout.splitlines() if line.startswith("CP ")]
        traces = [line for line in stdout.splitlines() if line.startswith("coalition ")]
        for who in ("tavi", "ryth"):
            phases = {event.get("phase"): event for event in events if event.get("who") == who}
            if "prepare" not in phases or "before" not in phases or "after" not in phases:
                failures.append({**tag, "who": who, "reason": "missing actual caller", "events": events})
                continue
            before, after = phases["before"], phases["after"]
            preparation = phases["prepare"]
            expected_preparation = (120, 75) if who == "tavi" else (75, 30)
            if (int(preparation["gold_before"]), int(preparation["gold_after"])) != expected_preparation or preparation["spent_attack"] != "1" or preparation["A"] != "1" or preparation["C"] != "0":
                failures.append({**tag, "who": who, "reason": "wrong real basic-reset preparation", "prepare": preparation})
            expected_before = (1200, 1000) if who == "tavi" else (1155, 1000)
            expected_after = (1155, 1000) if who == "tavi" else ((1110, 1000) if tag["arm"] == "on" else (1155, 955))
            if (int(before["ah"]), int(before["bh"])) != expected_before or before["A"] != "1" or before["C"] != "0":
                failures.append({**tag, "who": who, "reason": "wrong reached public precondition", "before": before})
            if (int(after["ah"]), int(after["bh"])) != expected_after:
                failures.append({**tag, "who": who, "reason": "wrong applied basic effects", "after": after})
            if (before["x"], before["y"]) != (after["x"], after["y"]) or after["moved"] != "0" or after["attacked"] != "1":
                failures.append({**tag, "who": who, "reason": "unexpected movement/basic receipt", "after": after})
            if tag["arm"] == "on" and (after["focus"] != after["ai"] or after["stamp"] != "54"):
                failures.append({**tag, "who": who, "reason": "wrong shared enemy record", "after": after})
        if "error" in stdout.lower() or at_budget:
            failures.append({**tag, "reason": "native error or exhaustion", "stdout": stdout[-3000:]})
        observations.append({**tag, "replay": str(rp.relative_to(ROOT)), "events": events, "traces": traces,
                             "ordinary_peak": peak, "ordinary_budget": budget, "at_budget": at_budget,
                             "stdout": stdout, "row": row})
    result = {"purpose": "conditional actual fight_enemies contribution; not normal-planner, full-cycle or strength",
              "rounds": 56, "observed_round": 54,
              "source_closure_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(body).hexdigest() for path, body in contents.items()},
              "entries": {f"{arm}-{side}": entry.key for (arm, side), entry in entries.items()},
              "map_spec": spec, "map_sha256": hashlib.sha256(mp.read_bytes()).hexdigest(),
              "driver_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "bundle": json.loads((ROOT / "docs/reference/current-version.json").read_text()),
              "games": observations, "failures": failures, "passed": not failures}
    (out / "probe.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    print(json.dumps({"out": str(out), "games": len(rows), "passed": not failures, "failures": failures}))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
