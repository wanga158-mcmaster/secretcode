"""Reach a low-health Wolf, then exercise the actual warband camp executor (bounded native)."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
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


def verify_existing(out: Path) -> int:
    # Preserve packing.json's original failed gate. Ryth's cast itself damages Gold; Vesper
    # uses both basics on Wolf and has no third attack. Neither is a failed executor.
    original = json.loads((out / "packing.json").read_text(encoding="utf-8"))
    target = out / "verification.json"
    if target.exists():
        raise SystemExit("refusing existing verification: " + str(target))
    failures, observations = [], []
    for game in original["games"]:
        by_phase = {"before": {}, "after": {}}
        for line in game["events"]:
            m = re.search(r"AP (before|after) r(\d+) who(\d+) gold(\d+) wolf(\d+) A(\d+) C(\d+)", line)
            if m:
                by_phase[m[1]][int(m[3])] = {"round": int(m[2]), "gold": int(m[4]),
                    "wolf": int(m[5]), "attack": int(m[6]), "cast": int(m[7])}
        expected = {11: (90, 120, 75), 0: (90, 20, 0), 1: (85, 120, 120)}
        for who, (pre_wolf, off_gold, on_gold) in expected.items():
            before, after = by_phase["before"].get(who), by_phase["after"].get(who)
            if before != {"round": 54, "gold": 120, "wolf": pre_wolf, "attack": 1, "cast": 1}:
                failures.append({"arm": game["arm"], "subject": game["subject"], "who": who,
                                 "reason": "wrong reachable precondition", "before": before})
            wanted = on_gold if game["arm"] == "on" else off_gold
            if not after or after["wolf"] != 0 or after["gold"] != wanted:
                failures.append({"arm": game["arm"], "subject": game["subject"], "who": who,
                                 "reason": "wrong actual caller effect", "after": after})
            observations.append({"arm": game["arm"], "subject": game["subject"], "who": who,
                                 "before": before, "after": after})
        if game["at_budget"] or game["ordinary_peak"] >= game["ordinary_budget"]:
            failures.append({"reason": "native fuel at budget", "replay": game["replay"]})
    result = {"original_gate_passed": original["passed"], "original_failures_preserved": original["failures"],
              "scope": "pure kit-specific correction of an overbroad gate; no native rerun or strength claim",
              "source_packing_sha256": hashlib.sha256((out / "packing.json").read_bytes()).hexdigest(),
              "observations": observations, "failures": failures, "passed": not failures}
    target.write_text(json.dumps(result, indent=1), encoding="utf-8")
    print(json.dumps({"verification": str(target), "passed": not failures, "failures": failures}))
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("out", type=Path)
    ap.add_argument("--verify-existing", action="store_true")
    args = ap.parse_args()
    out = args.out.resolve()
    if args.verify_existing:
        return verify_existing(out)
    if out.exists():
        raise SystemExit("refusing existing output: " + str(out))
    maps = ROOT / ".cache/contenders/warband/maps"
    maps.mkdir(parents=True, exist_ok=True)
    spec = {"width": 30, "height": 30, "symmetry": "ROTATIONAL180", "champs": 3,
            "spawnA": [0, 0, 3, 3], "altars": [[0, 29]],
            "camps": [[kind, x, y] for y in (6, 12, 18) for kind, x in (("gold_cluster", 7), ("wolf", 8))]}
    sp = maps / "production-packing.json"
    mp = sp.with_suffix(".map26a")
    sp.write_text(json.dumps(spec), encoding="utf-8")
    builder = ROOT / "bots/main/foundation/tools/combat/mkmap.mjs"
    proc = subprocess.run([str(core.NODE), "--no-warnings", str(builder), str(sp), str(mp)],
                          capture_output=True, text=True)
    if proc.returncode:
        raise SystemExit(proc.stderr[-2500:])
    bot = core.bot("contenders/_warband-production-probe")
    off, on = core.prepare_many([(bot, {}, "off"), (bot, {"camp_retarget": "true"}, "on")])
    for ent in (off, on):
        if not ent.ok:
            raise SystemExit(ent.log[-4000:])
    specs = []
    for arm, ent in (("off", off), ("on", on)):
        for side in ("A", "B"):
            a, b = (ent, off) if side == "A" else (off, ent)
            specs.append(games.GameSpec(a, b, str(mp), {"arm": arm, "subject": side}))
    rows = games.play(specs, out, rounds=56, keep_replays="all", quiet=True, resume=False)
    observations, failures = [], []
    for game, row in zip(specs, rows):
        tag = game.tags
        rp = Path(row["replay"])
        if not rp.is_absolute():
            rp = ROOT / rp
        with zipfile.ZipFile(rp) as z:
            stdout = z.read(f"team-{tag['subject'].lower()}.stdout").replace(b"\0", b"").decode("utf-8", "replace")
            raw = z.read(f"team-{tag['subject'].lower()}.fuel")
            if raw[:8] != b"a62leuf\0" or (len(raw) - 13) % 8:
                raise ValueError("invalid native fuel stream")
            fuel = struct.unpack_from(f"<{(len(raw) - 13) // 8}Q", raw, 13)
            peak, budget, exhausted = fuel_summary(fuel, tag["subject"])
        events = [line for line in stdout.splitlines() if line.startswith("AP ")]
        before = [line for line in events if line.startswith("AP before")]
        after = [line for line in events if line.startswith("AP after")]
        if len(before) != 3 or len(after) != 3:
            failures.append({**tag, "reason": "missing actual production caller", "events": events})
        if "error" in stdout.lower() or exhausted:
            failures.append({**tag, "reason": "native error or exhaustion", "stdout": stdout[-2500:]})
        for line in after:
            match = re.search(r"gold(\d+) wolf(\d+)", line)
            if not match or int(match[2]) != 0:
                failures.append({**tag, "reason": "wolf survived production cast", "line": line})
            elif tag["arm"] == "off" and int(match[1]) != (20 if "who0 " in line else 120):
                failures.append({**tag, "reason": "control unexpectedly retargeted", "line": line})
            elif tag["arm"] == "on" and int(match[1]) != (75 if "who11 " in line else 0 if "who0 " in line else 120):
                failures.append({**tag, "reason": "no production survivor basic", "line": line})
        observations.append({**tag, "replay": str(rp.relative_to(ROOT)), "events": events,
                             "ordinary_peak": peak, "ordinary_budget": budget, "at_budget": exhausted,
                             "row": row})
    result = {"purpose": "actual production executor contribution; not full-game health/strength",
              "rounds": 56, "off_key": off.key, "on_key": on.key,
              "map_sha256": hashlib.sha256(mp.read_bytes()).hexdigest(), "map_spec": spec,
              "bundle": json.loads((ROOT / "docs/reference/current-version.json").read_text()),
              "games": observations, "failures": failures, "passed": not failures}
    (out / "packing.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    print(json.dumps({"out": str(out), "passed": not failures, "games": len(rows), "failures": failures}))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
