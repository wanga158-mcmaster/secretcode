"""Territory intent and sampled native arbitration diagnostics, never admission.

The production log samples only rounds 128/256/512 and omits several Task
fields. A matching logged tuple is weaker than Task::operator==; a successful
move alone is weaker than geographic activation plus its safe continuation.
Cached commitments omit proposal/flags entirely. Trigger opportunities remain
unknown unless a physical observer reconstructs them independently.
"""
from __future__ import annotations

import hashlib
import json
import re
import sys
import zipfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HERE = "bots/territory/tests"
_DECISION = re.compile(
    r"p7 decision r=(\d+) actor=(\d+) fuel=([\d,]+) "
    r"goal=(-?\d+) source=(-?\d+) move=(-?\d+) "
    r"proposal=(-?\d+),(-?\d+),(-?\d+) flags=(\d+)")
_NAMES = (
    "concentric", "interior-lines", "frontier", "islander", "forks", "gatehouse",
    "causeway", "sanctuary", "ports", "migration", "elastic-border", "counterfront",
    "encirclement", "harvest-and-exit", "reclamation", "veil")


def contracts() -> list[dict]:
    charter = ROOT / "bots/territory/README.md"
    rows = {}
    for line in charter.read_text(encoding="utf-8").splitlines():
        if not line.startswith("|"):
            continue
        parts = [p.strip() for p in line.strip().strip("|").split(" | ")]
        if len(parts) >= 3 and parts[0] in _NAMES:
            rows[parts[0]] = parts[1:3]
    out = []
    for name in _NAMES:
        rule, state = rows.get(name, ["Read the named geographic rule in the suite charter", "Missing charter row"])
        out.append({
            "id": f"territory/{name}/geography", "bot": f"territory/{name}",
            "interaction": rule,
            "preconditions": "The row's personally observed geometry, own completed-cycle receipts, role and safety premises; no unseen ally support.",
            "activation": "A geographic proposal survives competitive arbitration and changes an actual route, source, port, guard, withdrawal or recovery action.",
            "continuation": "Retain the promised loaded exit and own conversion/recovery, or perform the declared refusal, expiry or safe retreat.",
            "refusal": "Unsupported geometry, income, visibility, own kit or safety makes the declared mechanism refuse; fallback is not activation.",
            "control": f"territory/{name} with P7_CONTROL=1 (also bypasses competitive arbitration and early bank target)",
            "neighbor": "Unresolved; compare the closest geographic continuation before admission" if name != "frontier" else "territory/harvest-and-exit",
            "positive_fixture": f"{HERE}/plan7-live-{'a' if _NAMES.index(name) < 8 else 'b'}.cpp; controller inputs only, not native reachability",
            "refusal_fixture": f"{HERE}/plan7-competitive.cpp",
            "observer": "observe() extracts sampled production tuples and movement candidates. Independent physical continuation, true trigger opportunities and control/neighbor difference remain required.",
            "test_paths": [f"{HERE}/run.py", f"{HERE}/plan7-live-a.cpp", f"{HERE}/plan7-live-b.cpp",
                           f"{HERE}/plan7-competitive.cpp", f"{HERE}/plan7-population.cpp",
                           f"{HERE}/geometry_survey.py"],
            "observer_paths": [f"{HERE}/population.py", "tools/inspect-strategy-replay.mjs",
                               "tools/replay-profiles.mjs", "tools/replay-profiles.json"],
            "gap": f"No complete exact-version admission; sampled proposals cannot certify native sequence or applicability. Charter finding: {state}",
            "next_check": "Reconstruct a reachable native geographic action and its continuation on matched control/neighbor conditions; resolve shared reachability obstructions first.",
            "scope": "Experimental geographic interaction; raw health/stall flags remain binding.",
        })
    islander = next(c for c in out if c["bot"] == "territory/islander")
    islander.update({
        "preconditions": "An own-funded ready dash/teleport, previous own capture and bank, and a seen feasible mobility-only source with certified separate ingress/loaded exit that outranks connected work.",
        "activation": "Follow the certified mobility-only ingress, capture, and loaded exit; ordinary walking or drafting a mobile champion is insufficient.",
        "positive_fixture": f"{HERE}/plan7-population.cpp isolates real choose_04 -> competitive_plan with an injected certificate; current native initialization cannot produce that positive frame.",
        "refusal_fixture": f"{HERE}/plan7-population.cpp: no ready own mobility, no complete exit, no prior income, and native shortlist filtering",
        "gap": "Production shortlist rejects every home-unreachable group before geometry; geometry attempts mobility only for !home_connected. Pool opportunities exist but cannot enter Islander's native menu.",
        "next_check": "Shared-module owner must admit proven mobility-only candidates to the same active/control menu, then verify ingress, capture, loaded exit and redeployment physically.",
        "neighbor": "territory/forks (a different loaded-exit organization; comparison still unverified)",
    })
    causeway = next(c for c in out if c["bot"] == "territory/causeway")
    causeway.update({
        "gap": "No camp-spawn-set removal opens another retained home-connected group in the current 40-map/80-side static survey; this is the exact current Causeway predicate, not a claim about all possible cuts.",
        "next_check": "Revalidate geometry_survey.py against current map hashes and shortlist; seek a suitable explicit scenario or investigate a different supported cut representation before native activation claims.",
    })
    return out


def parse_decisions(log: str) -> list[dict]:
    result = []
    for match in _DECISION.finditer(log):
        round_number, actor = int(match[1]), int(match[2])
        goal, source, move, pg, ps, pf, flags = map(int, match.groups()[3:])
        result.append({"round": round_number, "actor": actor,
                       "fuel_by_phase": [int(x) for x in match[3].split(",") if x],
                       "goal": goal, "source": source, "move_code": move,
                       "proposal_goal": pg, "proposal_source": ps,
                       "proposal_flags": pf, "flags": flags,
                       "proposal_sampled": pg >= 0,
                       "sample_tuple_preserved": pg >= 0 and (goal, source, flags) == (pg, ps, pf),
                       "mechanism_task": bool(flags & 1),
                       "successful_move": move == 4})
    return result


def observe(contract: dict, rows: list[dict], root: Path = ROOT) -> dict:
    """Keep diagnostics separate from independently verified physical witnesses."""
    root = Path(root); bot = contract["bot"]
    counts = Counter(); candidates = []; missing = []
    for row in rows:
        for side, key in (("A", "a"), ("B", "b")):
            if not str(row.get(key, "")).startswith(bot + "@"):
                continue
            counts["games"] += 1
            replay = row.get("replay")
            path = root / str(replay or "-missing-")
            if not replay or not path.is_file():
                missing.append(str(replay or "row without retained replay")); continue
            counts["retained_replays"] += 1
            with zipfile.ZipFile(path) as bundle:
                name = f"team-{side.lower()}.stdout"
                if name not in bundle.namelist():
                    missing.append(f"{replay}:{name}"); continue
                decisions = parse_decisions(bundle.read(name).decode("utf-8", errors="replace"))
            for event in decisions:
                counts["sampled_turns"] += 1
                counts["planned_or_cached"] += event["goal"] >= 0
                counts["sampled_proposals"] += event["proposal_sampled"]
                counts["sample_tuple_preserved"] += event["sample_tuple_preserved"]
                counts["proposed_mechanism"] += event["proposal_sampled"] and bool(event["proposal_flags"] & 1)
                counts["mechanism_tasks"] += event["mechanism_task"]
                counts["successful_moves"] += event["successful_move"]
                counts["mechanism_moves"] += event["mechanism_task"] and event["successful_move"]
                if event["mechanism_task"]:
                    candidates.append({"replay": replay, "replay_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                                       "side": side, "map": row.get("map"), "snapshot": row[key], **event,
                                       "physical_continuation_verified": False})
    return {"verified": False, "opportunities": None, "activations": 0, "continuations": 0,
            "diagnostics": dict(counts), "native_candidates": candidates, "missing": missing,
            "limitation": "Only rounds 128/256/512 sampled; cached commitments omit proposal/flags; logs omit Task fields and downstream state. Zero verified counts do not mean zero true trigger opportunities.",
            "gap": "Independent physical sequence, refusal, matched control/neighbor distinction, fresh applicability and full-pool health remain required."}


def main(argv: list[str]) -> int:
    if not argv or argv == ["--contracts"]:
        print(json.dumps(contracts(), indent=2)); return 0
    if argv == ["--self-test"]:
        lines = ("p7 decision r=512 actor=4 fuel=99931,99208,92646,58540,57634,45179,44943,39191, "
                 "goal=3 source=4 move=4 proposal=3,4,1 flags=1\n"
                 "p7 decision r=128 actor=4 fuel=99931,0,0,0,0,0,0,0, goal=0 source=1 move=4 proposal=-1,-1,0 flags=0\n")
        first, cached = parse_decisions(lines)
        assert first["sample_tuple_preserved"] and first["mechanism_task"] and first["successful_move"]
        assert not cached["proposal_sampled"] and not cached["sample_tuple_preserved"]
        assert len(contracts()) == 16
        print("3 sampled observer checks; no activation or opportunity certification"); return 0
    path = Path(argv[0]); path = path / "games.jsonl" if path.is_dir() else path
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    selected = [c for c in contracts() if len(argv) == 1 or c["bot"] == argv[1]]
    print(json.dumps({c["bot"]: observe(c, rows) for c in selected}, indent=2)); return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
