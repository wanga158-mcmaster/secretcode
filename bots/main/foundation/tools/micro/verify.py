"""Independently verify saved packing games through the current pinned native replay observer.

  python bots/main/foundation/tools/micro/verify.py RUN
Reads RUN/packing.json, never plays or rebuilds a game. Trace output must be fresh.
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

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def observe(item: dict, dest: Path) -> tuple[dict, dict]:
    replay = ROOT / item["replay"]
    round_no = 30 + int(item["parity"])
    argv = [str(NODE), "--no-warnings", str(INSPECTOR), "--replay", str(replay), "--actor", "all",
            "--trace", "1", "--from-round", str(item.get("from_round", round_no)), "--to-round", str(round_no),
            "--kinds", item.get("kinds", "5,6")]
    proc = subprocess.run(argv, capture_output=True)
    if proc.returncode:
        raise RuntimeError(proc.stderr.decode("utf-8", "replace")[-3000:])
    result = json.loads(proc.stdout)
    name = f"{item['arm']}-p{item['parity']}-{item['subject']}"
    file = dest / (name + ".json")
    file.write_bytes(proc.stdout)
    if proc.stderr:
        (dest / (name + ".stderr.txt")).write_bytes(proc.stderr)
    return item, result

def verify_move_reset(run: Path) -> int:
    source = run / "move-reset.json"
    binding = json.loads(source.read_text())
    assert binding["passed"]
    dest = run / "native-traces"
    dest.mkdir(exist_ok=False)
    items = [{**item, "parity": 0, "from_round": 1, "kinds": "4,5,6"} for item in binding["games"]]
    with concurrent.futures.ThreadPoolExecutor() as pool:
        pairs = list(pool.map(lambda item: observe(item, dest), items))
    checked, before_states = [], {}
    for item, doc in pairs:
        assert doc["engineVersion"] == "1.0.3" and doc["decoderProfile"] == "cli-3.0.5"
        assert doc["stopped"] == "round_range_end"
        trace = doc["interactions"]
        actor = 0 if item["subject"] == "A" else 3
        all_events = [e for e in trace["events"] if e["actor"] == actor]
        assert not any(e["kind"] == "attack" for e in all_events if e["round"] < 30)
        events = [e for e in all_events if e["round"] == 30]
        assert [e["kind"] for e in events] == (["move", "cast", "attack"] if item["arm"] == "on" else ["move"])
        initial = trace["states"][events[0]["actorState"][0]]
        after_move = trace["states"][events[0]["actorState"][1]]
        assert initial["availability"] == {"move": True, "attack": True, "cast": True, "completedTurn": False}
        assert after_move["availability"] == {"move": False, "attack": False, "cast": True, "completedTurn": False}
        before_states[(item["arm"], item["subject"])] = initial
        applied = None
        if item["arm"] == "on":
            reset, attack = events[1:]
            assert reset["action"]["abilitySlot"] == 0 and reset["action"]["abilityType"] == 1
            after_reset = trace["states"][reset["actorState"][1]]
            assert after_reset["availability"] == {"move": False, "attack": True, "cast": False, "completedTurn": False}
            assert after_reset["cooldowns"][0] == 8
            target = [8, 6] if item["subject"] == "A" else [21, 23]
            assert attack["action"]["recordedTo"] == target
            occupant = next(c["entity"] for c in attack["nearby"]["before"]["camps"] if c["at"] == target)
            applied = next(c for c in attack["observed"] if c["entity"] == occupant)
            assert applied["before"]["health"] == 1350 and applied["after"]["health"] == 1295
        checked.append({"arm": item["arm"], "subject": item["subject"], "noPriorNativeBasic": True,
                        "round30Sequence": [e["kind"] for e in events], "nativeSteps": [e["id"] for e in events],
                        "observedCampBasicEffect": applied})
    for side in ("A", "B"):
        assert before_states[("off", side)] == before_states[("on", side)]
    result = {"passed": True, "purpose": "Vesper ranged move-reset-basic with no earlier basic; not general reset-kit or strength proof",
              "nativeGamesObserved": 4, "nativeRoundRange": [1, 30], "matchedOffOnQueriedActorStates": 2,
              "source_sha256": {str(p.relative_to(ROOT)): sha(p) for p in [Path(__file__), INSPECTOR, source]},
              "off_key": binding["off_key"], "on_key": binding["on_key"], "checked": checked}
    (run / "native-verification.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("passed", "nativeGamesObserved", "nativeRoundRange", "matchedOffOnQueriedActorStates")}))
    return 0

def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "--move-reset":
        return verify_move_reset(Path(sys.argv[2]).resolve())
    run = Path(sys.argv[1]).resolve()
    packing = json.loads((run / "packing.json").read_text())
    if not packing["passed"]:
        raise SystemExit("packing calibration failed; no successful mechanism claim")
    dest = run / "native-traces"
    dest.mkdir(exist_ok=False)
    with concurrent.futures.ThreadPoolExecutor() as pool:
        pairs = list(pool.map(lambda item: observe(item, dest), packing["games"]))
    checked, prerequisites = [], {}
    for item, doc in pairs:
        if doc["engineVersion"] != "1.0.3" or doc["decoderProfile"] != "cli-3.0.5" or doc["stopped"] != "round_range_end":
            raise AssertionError("unexpected native decoder boundary")
        trace = doc["interactions"]
        subject_b = item["subject"] == "B"
        width = doc["width"]
        def own(p):
            return [width - 1 - p[0], 29 - p[1]] if subject_b else p
        for index, who in enumerate(("tavi", "ryth", "vesper")):
            actor = 3 * int(subject_b) + index
            events = [e for e in trace["events"] if e["actor"] == actor]
            expected_kinds = (["attack"] if who == "vesper" else ["cast"])
            if item["arm"] == "on":
                expected_kinds += (["cast", "attack"] if who == "vesper" else ["attack"])
            assert [e["kind"] for e in events] == expected_kinds, (item, who, "action sequence")
            y = 6 + 6 * index
            gold, wolf = own([7, y]), own([8, y])
            first = events[0]
            assert first["native"]["actor"] == {"kind": 1, "id": actor}
            assert first["action"]["recordedTo"] == gold
            gold_id = next(c["entity"] for c in first["nearby"]["before"]["camps"] if c["at"] == gold)
            wolf_id = next(c["entity"] for c in first["nearby"]["before"]["camps"] if c["at"] == wolf)
            gold_death = next(c for c in first["observed"] if c["entity"] == gold_id)
            assert gold_death["after"]["health"] == 0
            state = trace["states"][first["actorState"][0]]
            prerequisites[(item["arm"], item["parity"], item["subject"], who)] = state
            applied = None
            if item["arm"] == "on":
                basic = events[-1]
                assert basic["native"]["actor"] == {"kind": 1, "id": actor}
                assert basic["action"]["recordedTo"] == wolf
                effect = next(c for c in basic["observed"] if c["entity"] == wolf_id)
                before, after = ((1250, 1205) if who == "ryth" else (1350, 1295) if who == "vesper" else (1350, 1305))
                assert effect["before"]["health"] == before and effect["after"]["health"] == after
                applied = {"before": before, "after": after, "step": basic["id"], "targetCoordinate": wolf,
                           "campIdentityBasis": "unique living camp at recorded target coordinate in native before snapshot"}
                if who == "vesper":
                    reset_state = trace["states"][events[1]["actorState"][1]]
                    assert reset_state["availability"] == {"move": False, "attack": True, "cast": False, "completedTurn": False}
            checked.append({"arm": item["arm"], "parity": item["parity"], "subject": item["subject"], "who": who,
                            "actions": expected_kinds, "goldDeathStep": first["id"], "survivorBasic": applied})
    for (arm, parity, side, who), state in prerequisites.items():
        if arm == "on":
            assert state == prerequisites[("off", parity, side, who)], (parity, side, who, "public prerequisites differ")
    files = [Path(__file__), INSPECTOR, ROOT / "tools/replay-interaction-native.mjs", ROOT / "tools/replay-interaction-events.mjs",
             ROOT / "tools/replay-native-slot.py", run / "packing.json"]
    result = {"passed": True, "gamesObserved": len(pairs), "sequencesChecked": len(checked),
              "matchedOffOnQueriedActorStates": 12, "nativeHealthNotFullGameQualified": True,
              "source_sha256": {str(p.relative_to(ROOT)): sha(p) for p in files}, "checked": checked}
    (run / "native-verification.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k not in ("source_sha256", "checked")}))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
