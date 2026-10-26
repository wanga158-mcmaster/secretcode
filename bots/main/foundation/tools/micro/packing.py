"""Current-engine mixed-camp action packing: native probe with paired OFF/ON arms.

Run with the Python printed by arena env:
  python bots/main/foundation/tools/micro/packing.py OUT
OUT must be fresh. Scratch map bodies stay under .cache/contenders/micro/.
Eight bounded native games cover two round parities and both subject sides.
This is a mechanism test, not full-game health or strength evidence.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / "tools/arena"))
import core
import games

BOT = "contenders/_micro-pack"
NODE = Path.home() / ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe"

def fields(line: str) -> dict:
    return {k: v for k, v in (part.split("=", 1) for part in line.split()[1:] if "=" in part)}

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("out", type=Path)
    args = ap.parse_args()
    out = args.out.resolve()
    if out.exists():
        raise SystemExit("refusing existing output directory: " + str(out))
    maps = ROOT / ".cache/contenders/micro/maps"
    maps.mkdir(parents=True, exist_ok=True)
    builder = ROOT / "bots/main/foundation/tools/combat/mkmap.mjs"
    if not builder.is_file():
        builder = ROOT / "bots/contenders/foundation/tools/combat/mkmap.mjs"
    paths = []
    for parity in (0, 1):
        spec = {"width": 30 + parity, "height": 30, "symmetry": "ROTATIONAL180", "champs": 3,
                "spawnA": [0, 0, 3, 3], "altars": [[0, 29]],
                "camps": [[kind, x, y] for y in (6, 12, 18) for kind, x in (("gold_cluster", 7), ("wolf", 8))]}
        sp = maps / f"packing-p{parity}.json"
        mp = sp.with_suffix(".map26a")
        sp.write_text(json.dumps(spec), encoding="utf-8")
        p = subprocess.run([str(NODE), "--no-warnings", str(builder), str(sp), str(mp)], capture_output=True, text=True)
        if p.returncode:
            raise SystemExit(p.stderr[-2500:])
        paths.append(mp)
    baseline, active = core.prepare_many([(core.bot(BOT), {}, "off"), (core.bot(BOT), {"micro_followup": "1"}, "on")])
    for e in (baseline, active):
        if not e.ok:
            raise SystemExit(e.log[-4000:])
    specs = []
    for arm, ent in (("off", baseline), ("on", active)):
        for parity, mp in enumerate(paths):
            for side in ("A", "B"):
                a, b = (ent, baseline) if side == "A" else (baseline, ent)
                specs.append(games.GameSpec(a, b, str(mp), {"arm": arm, "parity": parity, "subject": side}))
    rows = games.play(specs, out, rounds=35, keep_replays="all", quiet=True, resume=False)
    observations = []
    failures = []
    for spec, row in zip(specs, rows):
        tag = spec.tags
        rp = Path(row["replay"])
        if not rp.is_absolute():
            rp = ROOT / rp
        with zipfile.ZipFile(rp) as z:
            stdout = z.read(f"team-{tag['subject'].lower()}.stdout").replace(b"\0", b"").decode("utf-8", "replace")
            fuel = {}
            for native_side in ("a", "b"):
                raw = z.read(f"team-{native_side}.fuel")
                if raw[:8] != b"a62leuf\0" or (len(raw) - 13) % 8:
                    raise ValueError("invalid native fuel stream")
                values = struct.unpack_from(f"<{(len(raw) - 13) // 8}Q", raw, 13)
                ordinary = values[8 + 3 + 3:]
                budget = values[3 if native_side == "a" else 7]
                fuel[native_side.upper()] = {"ordinary_peak": max(ordinary, default=0), "ordinary_budget": budget,
                                             "at_budget": sum(v >= budget for v in ordinary)}
                if fuel[native_side.upper()]["at_budget"]:
                    failures.append({**tag, "reason": "native ordinary fuel reached budget", "side": native_side})
        events = [fields(line) for line in stdout.splitlines() if line.startswith("U ")]
        obs = {**tag, "replay": str(rp.relative_to(ROOT)), "row": row, "events": events, "native_fuel": fuel}
        for who in ("tavi", "ryth", "vesper"):
            evs = [e for e in events if e["who"] == who]
            phase = {e["phase"]: e for e in evs}
            if "before" not in phase or "done" not in phase:
                failures.append({**tag, "who": who, "reason": "missing native phase"})
                continue
            done = phase["done"]
            expected = {"tavi": 1350, "ryth": 1250, "vesper": 1350}[who]
            if tag["arm"] == "on":
                expected -= {"tavi": 45, "ryth": 45, "vesper": 55}[who]
            if int(done["w"]) != expected or done["ga"] != "0":
                failures.append({**tag, "who": who, "reason": "wrong applied camp effects", "done": done})
            if tag["arm"] == "on" and "after_survivor_basic" not in phase:
                failures.append({**tag, "who": who, "reason": "missing applied followup"})
            if tag["arm"] == "off" and "after_survivor_basic" in phase:
                failures.append({**tag, "who": who, "reason": "control unexpectedly followed up"})
            if who == "vesper" and tag["arm"] == "on" and (phase.get("after_reset", {}).get("M") != "0" or phase.get("after_reset", {}).get("A") != "1"):
                failures.append({**tag, "who": who, "reason": "reset allowance mismatch"})
        if "error" in stdout.lower():
            failures.append({**tag, "reason": "native stdout error"})
        observations.append(obs)
    files = [ROOT / "bots/contenders/_micro-pack/src/main.cpp", Path(__file__), ROOT / "sdk/cpp/cambc.h", ROOT / "docs/reference/current-version.json", builder]
    result = {"purpose": "bounded native action packing, not full-game health/strength", "rounds": 35,
              "baseline_key": baseline.key, "active_key": active.key,
              "bundle": json.loads((ROOT / "docs/reference/current-version.json").read_text()),
              "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
              "games": observations, "failures": failures, "passed": not failures}
    (out / "packing.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    print(json.dumps({"out": str(out), "games": len(observations), "passed": not failures, "failures": failures}))
    return 1 if failures else 0

if __name__ == "__main__":
    raise SystemExit(main())
