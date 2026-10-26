"""Events behavior contracts and conservative native-overlap triage.

These descriptions are promises, not admissions. Existing focused fixtures call
the production policies but do not replace native execution/continuation proof.
The overlap observer deliberately reports candidates without certifying capture
completion or distinguishing the policy from Tempo by its logs alone.
"""
from __future__ import annotations

import hashlib
import json
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
_RULES = {
    "tempo": "Separate capture-gold and bank-gem clocks; bank a bag at its declared threshold or deadline.",
    "harvest": "Choose complete renewable harvest cycles rather than gold-only mature camps.",
    "strata": "Leave reusable damage in a camp and return after recovery to complete its capture.",
    "surveyor": "Rotate a three-slot detailed information frontier after screening all camps.",
    "geodesic": "Choose a complete camp-to-altar route using terrain distance rather than straight-line distance.",
    "quiescence": "Execute a bounded lethal basic/reset/direct-damage plan using the shared cast allowance.",
    "matching": "Publish an expiring commitment and execute an exact three-actor job assignment.",
    "overlap": "Start tribute while holding an uncontested cleared camp spawn, preserve both activities, and bank the capture award when phases permit.",
    "refuge": "Withdraw into a recall-safe region selected by next-event loss and finish recovery.",
    "capital": "Buy a bundle only when it crosses an integer kill-work or survival threshold.",
    "itinerary": "Execute a two-camp route whose gems share one terminal bank.",
    "reserve": "Admit an expedition by integer health capacity and amortized recovery cost.",
    "terminal": "Choose the earliest bank that reaches the remaining winning-gem target.",
    "payday": "Accept capture gold and bank gems only when each payment meets its own deadline.",
    "contingency": "Choose a private wait or job by minimax regret under one extra camp or capture event.",
    "triage": "Change the next physical basic target to reduce imminent camp exposure.",
}


def contracts() -> list[dict]:
    """Expose all 16 current lines without promoting historic two-map results."""
    out = []
    for name, interaction in _RULES.items():
        focused = f"bots/events/tests/plan11-{name}.cpp"
        out.append({
            "id": f"events/{name}", "bot": f"events/{name}",
            "interaction": interaction,
            "preconditions": "Visible facts and available action allowances satisfying the production policy's gate; no inferred hidden state.",
            "activation": "A production choice changes a physical action or its timing; a proposal alone is insufficient.",
            "continuation": "Complete the declared event sequence, or witness its explicit safe expiry/recovery.",
            "refusal": "The production gate refuses an ineligible or unsupported opportunity.",
            "control": "events/tempo" if name != "tempo" else "anchors/fundamentals",
            "neighbor": "events/tempo" if name != "tempo" else "events/harvest",
            "positive_fixture": focused,
            "refusal_fixture": focused,
            "observer": "Existing untraced runtime telemetry plus physical replay state; no completed native observer claim yet.",
            "test_paths": ["bots/events/tests/run.py", focused,
                           "bots/events/tests/plan11-event-kernel.cpp",
                           "bots/events/tests/plan11-turn-completion.cpp",
                           "bots/events/tests/plan11-initialization.cpp",
                           "bots/events/tests/plan11-public-work.cpp",
                           "bots/events/tests/plan11-move-fuel.cpp"],
            "observer_paths": ["tools/inspect-strategy-replay.mjs", "tools/replay-profiles.mjs",
                               "tools/replay-profiles.json"],
            "gap": "No complete exact-version admission: full-pool health, native activation/continuation/refusal, matched physical distinction and fresh applicability remain unverified.",
            "next_check": "Run the focused production fixtures, then seek a native opportunity against Tempo and a declared cross-suite opponent on suitable maps.",
            "scope": "Experimental; prior two-map matrix is diagnostic, not current full-pool admission.",
        })
    overlap = next(c for c in out if c["bot"] == "events/overlap")
    overlap.update({
        "preconditions": "Hold a cleared, reward-available, uncontested camp spawn within queried submit range of an unlocked altar, carry an old bag, have no channel, and pass production fuel/retreat gates.",
        "activation": "The native capture-hold executor starts a tribute channel on that spawn; require matching p11overlap and p11channel kind=2 start=1 plus physical state.",
        "continuation": "Remain on the spawn while channeling; observe capture reward and later bank or an explicit valid interruption. Banking an enlarged bag additionally requires capture before owner-start completion.",
        "refusal": "Refuse off-spawn, unavailable reward, contested, already channeling, empty bag, out-of-range, unknown distance/range, or enemy-locked cases.",
        "observer": "observe() finds production log candidates; the pinned physical replay observer must confirm the capture/channel/bank sequence. Logs do not certify the continuation.",
        "observer_paths": ["bots/events/tests/population.py", "tools/inspect-strategy-replay.mjs",
                           "tools/replay-profiles.mjs", "tools/replay-profiles.json"],
        "next_check": "Use geometry_opportunities() to select actual altar-adjacent camps, then compare Overlap/Tempo native capture and tribute timing on identical maps/sides/opponents.",
    })
    out.append({
        "id": "events/overlap-arrival-bank", "bot": "events/overlap",
        "interaction": "Pack a qualifying tribute start into the same owner turn as the final walk into altar range, then preserve the channel and resume work after completion.",
        "preconditions": "The actual bank gate accepts the bag/deadline, a final walk reaches queried altar range in this owner turn, enough fuel remains, and no enemy altar lock or current channel prevents submission.",
        "activation": "After a successful native final walk, Overlap starts tribute in the same owner turn; Tempo under the same arrival state waits until its next owner turn.",
        "continuation": "Native channel ticks, bag removal and team submitted-gem increase agree at completion; the actor resumes physical work after completion.",
        "refusal": "The actual production bank caller refuses an ineligible bag, blocked movement, stale owner round, insufficient post-move fuel, empty bag, missing altar, out-of-range arrival or enemy lock.",
        "control": "events/tempo", "neighbor": "events/tempo",
        "positive_fixture": "bots/events/tests/plan11-bank-caller.cpp",
        "refusal_fixture": "bots/events/tests/plan11-bank-caller.cpp",
        "observer": "Pinned native channel/score receipts plus results/population/events-branch/comparison.json: identical forks and a common legal arrival move compare submit-now banking in round 222 with defer-to-next-owner banking in 223 under validated retained peer inputs. The production caller fixture attributes submit/defer to the policies; the native branch does not execute either guest policy or re-execute peer policies.",
        "test_paths": ["bots/events/tests/run.py", "bots/events/tests/plan11-bank-caller.cpp",
                       "bots/events/tests/plan11-event-kernel.cpp", "bots/events/tests/plan11-move-fuel.cpp"],
        "observer_paths": ["bots/events/tests/population.py", "tools/inspect-strategy-replay.mjs",
                           "tools/replay-profiles.mjs", "tools/replay-profiles.json",
                           "results/population/events-branch/run.py", "results/population/events-branch/case.mjs"],
        "gap": "The fixed-input native timing distinction is verified, with 74 checks and 67 exact reference inputs, excluding only timeline-id bytes 48..56. This is not a full guest/policy counterfactual: peer policies are not re-executed and selected now-arm actions 3670..3673 are not executed. The guarded executable still has eight stalls; the historical arrival witness does not certify its changed bytes. Healthy subject/opponent coverage and fresh applicability remain unresolved; this contract is not admitted.",
        "next_check": "Preserve the fixed-input native result and production caller distinction; resolve execution stalls, then verify fresh applicability and actual guest continuation on the exact candidate bytes with a healthy opponent. Re-executed policies could change interruptions, survival or the timing benefit.",
        "scope": "Independent arrival-packing contribution with a bounded native timing comparison. The original capture/tribute-overlap contract remains separate and unwitnessed.",
    })
    return out


def geometry_opportunities(root: Path = ROOT, submit_range: int = 1) -> dict:
    """Map opportunity count only; geometry cannot prove a bag or live trigger.

    The default is the current bundle's range, not a substitute for runtime
    settings in a bot. Validate every physical map against the geometry export.
    """
    root = Path(root)
    path = root / "maps/geometry.json"
    geometry = json.loads(path.read_text(encoding="utf-8"))
    pairs, invalid = [], []
    member_count = group_count = 0
    for world in geometry["maps"]:
        name = world["id"]
        candidates = [root / "maps" / f"{name}.map26a",
                      root / "maps" / f"{name.replace('_', '-')}.map26a"]
        map_path = next((p for p in candidates if p.is_file()), None)
        if map_path is None or hashlib.sha256(map_path.read_bytes()).hexdigest() != world["mapSha256"]:
            invalid.append(name)
            continue
        width = world["width"]
        for group in world["groups"]:
            group_count += 1
            for member in group["members"]:
                member_count += 1
                for altar in world["terrain"]["altar"]:
                    distance = max(abs(member["x"] - altar % width),
                                   abs(member["y"] - altar // width))
                    if distance <= submit_range:
                        pairs.append({"map": map_path.stem, "group": group["key"],
                                      "member": member["tile"], "altar": altar,
                                      "distance": distance, "map_hash": world["mapSha256"]})
    return {"geometry_hash": hashlib.sha256(path.read_bytes()).hexdigest(),
            "submit_range": submit_range, "maps": len(geometry["maps"]),
            "groups": group_count, "members": member_count,
            "opportunity_maps": sorted({p["map"] for p in pairs}),
            "pairs": pairs, "invalid_maps": invalid,
            "limitation": "Static adjacency only; actual old bag, capture state, fuel, channels and contests are not observed."}


_OVERLAP = re.compile(r"p11overlap actor=(\d+) round=(\d+) group=(\d+)")
_ARRIVAL = re.compile(r"p11arrival actor=(\d+) round=(\d+)")
_CHANNEL = re.compile(r"p11channel actor=(\d+) round=(\d+) kind=(\d+) start=(\d+) timer=(-?\d+) bag=(\d+) due=(\d+)")


def observe(contract: dict, rows: list[dict], root: Path = ROOT) -> dict:
    """Extract native candidates without upgrading them to acceptance evidence."""
    if contract.get("bot") != "events/overlap":
        return {"verified": False, "gap": "Native sequence extractor not implemented for this policy."}
    arrival = contract.get("id") == "events/overlap-arrival-bank"
    witnesses, missing = [], []
    for row in rows:
        replay = row.get("replay")
        if not replay:
            missing.append("row without retained replay")
            continue
        path = Path(root) / replay
        if not path.is_file():
            missing.append(str(replay))
            continue
        sides = [side for side, key in (("A", "a"), ("B", "b"))
                 if str(row.get(key, "")).startswith("events/overlap@")]
        with zipfile.ZipFile(path) as bundle:
            for side in sides:
                stdout = f"team-{side.lower()}.stdout"
                if stdout not in bundle.namelist():
                    missing.append(f"{replay}:{stdout}")
                    continue
                log = bundle.read(stdout).decode("utf-8", errors="replace")
                channels = {(int(m[1]), int(m[2])): m for m in _CHANNEL.finditer(log)
                            if m[3] == "2" and m[4] == "1"}
                if arrival:
                    for event in _ARRIVAL.finditer(log):
                        actor, round_number = map(int, event.groups())
                        channel = channels.get((actor, round_number))
                        witnesses.append({"replay": replay, "side": side, "map": row.get("map"),
                                          "actor": actor, "round": round_number,
                                          "channel_started": channel is not None,
                                          "starting_bag": int(channel[6]) if channel else None,
                                          "continuation_verified": False})
                    continue
                for event in _OVERLAP.finditer(log):
                    actor, round_number, group = map(int, event.groups())
                    channel = channels.get((actor, round_number))
                    witnesses.append({"replay": replay, "side": side, "map": row.get("map"),
                                      "actor": actor, "round": round_number, "group": group,
                                      "channel_started": channel is not None,
                                      "starting_bag": int(channel[6]) if channel else None,
                                      "continuation_verified": False})
    return {"verified": False, "native_candidates": witnesses, "missing": missing,
            "gap": ("Confirm physical final walk, same-owner-turn channel start, native bank and resumed work; compare Tempo from the same native arrival state and fresh applicability before admission."
                    if arrival else "Confirm physical on-spawn channel, capture progress/award and bank/valid exit; compare Tempo and fresh applicability before admission.")}
