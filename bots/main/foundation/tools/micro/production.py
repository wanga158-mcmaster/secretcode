"""Observe a retained warband production caller fixture; no new builds or games.

  python bots/main/foundation/tools/micro/production.py OWNER-RUN FRESH-OUTPUT
Keeps the owner's failed original verification intact and checks only Tavi/Ryth r54.
"""
from __future__ import annotations
import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[5]
NODE = Path.home() / ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe"
INSPECTOR = ROOT / "tools/inspect-strategy-replay.mjs"

def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()

def observe(item: dict, dest: Path) -> tuple[dict, dict]:
    argv = [str(NODE), "--no-warnings", str(INSPECTOR), "--replay", str(ROOT / item["replay"]),
            "--actor", "all", "--trace", "1", "--from-round", "54", "--to-round", "54", "--kinds", "5,6"]
    proc = subprocess.run(argv, capture_output=True)
    if proc.returncode:
        raise RuntimeError(proc.stderr.decode("utf-8", "replace")[-3000:])
    tag = item["arm"] + "-" + item["subject"]
    (dest / (tag + ".json")).write_bytes(proc.stdout)
    if proc.stderr:
        (dest / (tag + ".stderr.txt")).write_bytes(proc.stderr)
    return item, json.loads(proc.stdout)

def main() -> int:
    owner = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2]).resolve()
    out.mkdir(parents=True, exist_ok=False)
    owner_file = owner / "packing.json"
    binding = json.loads(owner_file.read_text())
    owner_hash = sha(owner_file)
    if binding["passed"] or not binding["failures"]:
        raise AssertionError("expected preserved original overbroad verification failure")
    with concurrent.futures.ThreadPoolExecutor() as pool:
        pairs = list(pool.map(lambda item: observe(item, out), binding["games"]))
    checked, prerequisites, failed = [], {}, []
    for item, doc in pairs:
        assert doc["engineVersion"] == "1.0.3" and doc["decoderProfile"] == "cli-3.0.5"
        assert doc["stopped"] == "round_range_end"
        trace = doc["interactions"]
        team = 0 if item["subject"] == "A" else 1
        def own(p):
            return [29 - p[0], 29 - p[1]] if team else p
        for index, who in enumerate(("tavi", "ryth")):
            actor = 3 * team + index
            events = [e for e in trace["events"] if e["actor"] == actor]
            assert [e["kind"] for e in events] == (["cast", "attack"] if item["arm"] == "on" else ["cast"])
            y = 6 + 6 * index
            gp, wp = own([7, y]), own([8, y])
            cast = events[0]
            assert cast["native"]["actor"] == {"kind": 1, "id": actor}
            assert cast["action"]["recordedTo"] == wp
            near = cast["nearby"]["before"]["camps"]
            gold = next(c["entity"] for c in near if c["at"] == gp)
            wolf = next(c["entity"] for c in near if c["at"] == wp)
            death = next(c for c in cast["observed"] if c["entity"] == wolf)
            assert death["before"]["health"] == 90 and death["after"]["health"] == 0
            after_cast = trace["states"][cast["actorState"][1]]
            assert after_cast["availability"]["attack"] is True
            assert after_cast["availability"]["cast"] is False
            if who == "ryth":
                splash = next(c for c in cast["observed"] if c["entity"] == gold)
                assert splash["before"]["health"] == 120 and splash["after"]["health"] == 20
            else:
                assert all(c["entity"] != gold for c in cast["observed"])
            state = trace["states"][cast["actorState"][0]]
            prerequisites[(item["arm"], item["subject"], who)] = state
            effect = None
            if item["arm"] == "on":
                basic = events[1]
                assert basic["native"]["actor"] == {"kind": 1, "id": actor}
                assert basic["action"]["recordedTo"] == gp
                effect = next(c for c in basic["observed"] if c["entity"] == gold)
                expected = (120, 75) if who == "tavi" else (20, 0)
                assert effect["before"]["health"] == expected[0] and effect["after"]["health"] == expected[1]
            checked.append({"arm": item["arm"], "subject": item["subject"], "who": who,
                            "nativeCastStep": cast["id"], "actions": [e["kind"] for e in events],
                            "nativeFollowupStep": events[1]["id"] if len(events) > 1 else None,
                            "followupObservedGoldEffect": effect})
    for (arm, side, who), state in prerequisites.items():
        if arm == "on":
            assert state == prerequisites[("off", side, who)], (side, who, "queried prerequisites differ")
    assert sha(owner_file) == owner_hash
    inputs = [Path(__file__), owner_file, INSPECTOR, ROOT / "tools/replay-interaction-native.mjs",
              ROOT / "tools/replay-interaction-events.mjs", ROOT / "tools/replay-native-slot.py"]
    result = {"passed": True, "purpose": "conditional actual production executor contribution; not normal planner reach or full-cycle/strength",
              "engine": "1.0.3", "roundObserved": 54, "nativeGamesObserved": 4, "callerSequencesChecked": 8,
              "matchedOffOnQueriedActorStates": 4, "off_key": binding["off_key"], "on_key": binding["on_key"],
              "originalFailedVerificationPreserved": {"path": str(owner_file.relative_to(ROOT)), "sha256": owner_hash,
                                                       "failures": binding["failures"]},
              "source_sha256": {str(p.relative_to(ROOT)): sha(p) for p in inputs}, "checked": checked}
    (out / "production-verification.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("passed", "nativeGamesObserved", "callerSequencesChecked", "matchedOffOnQueriedActorStates")}))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
