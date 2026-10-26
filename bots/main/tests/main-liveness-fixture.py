"""Fixture: reject the confirmed road/corners deadlocks and gem-fields unproductive tail.

Road-of-riches B vs starter: live v40 banks 930, then all five champions stop after
round 818. Three await an enemy-occupied zero-timer respawn and block two carriers'
altar approach. Require an own win with at least the pinned winning target of 1000
banked gems; a winning round-2000 tiebreak is insufficient.

Corners A/B: the isolated respawn-wait correction exposed native-confirmed late
inactivity against a brawl persona. Pin that opponent to frozen main-v40 plus its
persona knobs, so future subject changes cannot change the regression game keys.

Gem-fields A/B: the planning+respawn-wait candidate and live-v40 baseline have long
final counter tails against hunter. Exact native candidate traces confirm all five
champions oscillate over 2-4 tiles without productive activity in rounds 1937..2000.
Pin hunter to frozen main-v40 plus the captured PERSONAS['hunter'] overrides too.

For these known native-confirmed contexts, reject a final cumulative-counter tail
of at least 120 rounds using tools/liveness.py. The screen is a regression guard,
not a general proof that every suspect game is stalled or every other game healthy.
Fixturelib fault/exhaustion checks and explicit logged-error checks also apply.

Usage: python -B bots/main/tests/main-liveness-fixture.py [--bot main/main] [--keep]
       [--scenario all|road|corners|gemfields|gemfields-A|gemfields-B] [--expected-negative]
The diagnostic --expected-negative annotation never turns a failed fixture into
a passing one; current default-off/v40 road and gem-fields controls exit 1.
"""
from __future__ import annotations

import argparse
from decimal import Decimal, InvalidOperation
import hashlib
import json
import os
from pathlib import Path
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "bots/main/tools"))
import fixturelib
from liveness import screen_statistics


FIXTURE = "liveness"
WINNING_GEMS = 1000  # Pinned CLI/SDK 3.0.5 / engine 1.0.3 winning_gems setting.
MIN_TAIL = 120
BRAWL = "anchors/main-v40|squad_size=3,focus_builds=true,draft_mode=16,hunt_mode=1,hunt_radius=12"
# Captured from our gauntlet.py PERSONAS['hunter']; do not follow future persona edits.
HUNTER = "anchors/main-v40|hunt_mode=1,hunt_radius=20,hunt_health=0.4f,fight_better_trades_only=false,limited_chase=false"
ROAD = [("00road-of-riches", "b", "anchors/starter-cpp")]
CORNERS = [("00corners", "a", BRAWL), ("00corners", "b", BRAWL)]
GEMFIELDS = [("00gem-fields", "a", HUNTER), ("00gem-fields", "b", HUNTER)]
LIVE_V40_WASM = "7fe8d557b5539d8b2caa40a842d266352f545badc3a02c22f6a131a4ad17c18b"


def options():
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("--bot", default="main/main", help="bot under test, <suite>/<bot>")
    parser.add_argument("--keep", action="store_true", help="retain passing game artifacts too")
    parser.add_argument("--scenario", choices=("all", "road", "corners", "gemfields", "gemfields-A", "gemfields-B"), default="all")
    parser.add_argument("--expected-negative", action="store_true",
                        help="annotate an exact-v40 single-scenario negative control; still exits 1 on regression")
    args = parser.parse_args()
    if args.expected_negative and args.scenario not in ("road", "gemfields-A", "gemfields-B"):
        parser.error("--expected-negative requires one road or gemfields-A/B scenario")
    return args


def fingerprints(output: Path, games):
    """Read manifest project paths and binary bytes; never inspect opponent source."""
    result = []
    lines = (output / "manifest.txt").read_text(encoding="utf-8").splitlines()
    if len(lines) != len(games):
        raise ValueError("manifest/game count differs")
    for game, line in zip(games, lines):
        projects = line.split()[:2]
        hashes = {}
        for label, project in zip(("A", "B"), projects):
            folder = Path(project)
            builds = {name: folder / ".cambc/build-release" / name
                      for name in ("bot.wasm", "bot-injected.wasm")}
            hashes[label] = {"project": str(folder), "wasm_sha256": {
                name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in builds.items()}}
        result.append({"map": game.map, "subject": game.side.upper(), "opponent": game.opponent,
                       "binaries": hashes})
    return result


def assess(game):
    side = game.side.upper()
    screen = screen_statistics(game.stats, side, min_tail=MIN_TAIL)
    errors = [line.strip() for line in game.stdout.splitlines() if "error" in line.lower()]
    reasons = []
    if not game.healthy:
        reasons.append("missing statistics, own fault, or own fuel exhaustion")
    if errors:
        reasons.append(f"{len(errors)} own logged error lines")
    if screen["status"] == "UNAVAILABLE":
        reasons.append("liveness metric coverage unavailable")
    elif screen["status"] == "SUSPECT":
        reasons.append(f"known-regression context has {screen['tail_rounds']}-round final counter tail")
    banked = None
    if screen["status"] != "UNAVAILABLE":
        try:
            banked = Decimal(screen["metrics"][f"{game.team}:submittedGems"]["final_value"])
        except (InvalidOperation, KeyError, TypeError):
            reasons.append("final banked gems unavailable")
    if game.map == "00road-of-riches":
        if game.result != "win":
            reasons.append("road regression requires an own win")
        if banked is None or banked < WINNING_GEMS:
            reasons.append(f"road regression requires {WINNING_GEMS} banked gems, not a timeout tiebreak")
    return {"map": game.map, "side": side, "opponent": game.opponent,
            "passed": not reasons, "reasons": reasons, "result": game.result,
            "banked_gems": str(banked) if banked is not None else None,
            "faults": game.faults, "exhausted": game.exhausted, "error_lines": errors,
            "screen": screen, "replay": str(game.replay), "statistics": str(game.stats_path)}


def main():
    args = options()
    scenarios = {"road": ROAD, "corners": CORNERS, "gemfields": GEMFIELDS,
                 "gemfields-A": GEMFIELDS[:1], "gemfields-B": GEMFIELDS[1:],
                 "all": ROAD + CORNERS + GEMFIELDS}[args.scenario]
    # Fixturelib replaces a previous run's directory. A unique child preserves
    # failed evidence when the repaired subject is tested again or concurrently.
    run_label = f"{FIXTURE}/{time.time_ns()}-{os.getpid()}"
    games = fixturelib.play(run_label, args.bot, scenarios)
    output = fixturelib.out_dir(run_label, args.bot)
    if games is None:
        return fixturelib.finish(run_label, args.bot, games, False, args.keep)
    records = [assess(game) for game in games]
    try:
        binary_records = fingerprints(output, games)
        fingerprint_error = None
    except (OSError, ValueError) as exc:
        binary_records, fingerprint_error = [], str(exc)
    passed = all(row["passed"] for row in records) and fingerprint_error is None
    expected_reproduced = False
    if args.expected_negative and len(records) == 1 and binary_records:
        row = records[0]
        raw = binary_records[0]["binaries"][row["side"]]["wasm_sha256"]["bot.wasm"]
        known_scenario = ((args.scenario == "road" and row["banked_gems"] == "930" and row["result"] == "win") or
                          (args.scenario in ("gemfields-A", "gemfields-B") and row["map"] == "00gem-fields" and row["opponent"] == HUNTER))
        expected_reproduced = (raw == LIVE_V40_WASM and not row["passed"] and known_scenario and
                               row["screen"]["status"] == "SUSPECT" and row["faults"] == 0 and
                               row["exhausted"] == 0 and not row["error_lines"])
    report = {"fixture": FIXTURE, "bot": args.bot, "scenario": args.scenario,
              "passed": passed, "thresholds": {"winning_gems": WINNING_GEMS, "min_tail": MIN_TAIL},
              "expected_negative_requested": args.expected_negative,
              "expected_exact_v40_negative_reproduced": expected_reproduced,
              "expected_exact_v40_road_negative_reproduced": args.scenario == "road" and expected_reproduced,
              "fixed_brawl_opponent": BRAWL, "fixed_hunter_opponent": HUNTER, "binaries": binary_records,
              "fingerprint_error": fingerprint_error, "games": records,
              "limits": "Known native-confirmed road/corners/gem-fields regression guard. Gem-fields native candidate confirmation covers rounds1937..2000, not the entire counter tail. A statistics-only final counter tail is not a general proof of a stall; no general win-rate or strength threshold is used."}
    (output / "liveness-fixture.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    for game, record in zip(games, records):
        print(f"{game.map} side {game.side}: banked {record['banked_gems']}; "
              f"screen {record['screen']['status']} tail {record['screen'].get('tail_rounds', '?')}; "
              f"{len(record['error_lines'])} error lines; {game.health()}" +
              ("; FAIL " + "; ".join(record["reasons"]) if record["reasons"] else ""))
    if args.expected_negative:
        print("EXPECTED NEGATIVE " + ("reproduced; regression fixture remains FAIL" if expected_reproduced else
                                       "NOT reproduced; inspect retained diagnostic report"))
    return fixturelib.finish(run_label, args.bot, games, passed, args.keep)


if __name__ == "__main__":
    raise SystemExit(main())
