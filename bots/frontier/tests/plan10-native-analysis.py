"""Synthetic fixtures for bots/frontier/tools/analyze_native.py; launches no game.

Run through bots/frontier/tests/run.py (case native-analysis) or directly with Python 3.12.
"""
from __future__ import annotations

from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SUITE = Path(__file__).resolve().parents[1]
ROOT = SUITE.parents[1]
SPEC = importlib.util.spec_from_file_location("frontier_analyze_native", SUITE / "tools/analyze_native.py")
n = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(n)
PROFILE = next(p for p in json.loads(n.PROFILE_PATH.read_text(encoding="utf-8-sig"))["profiles"]
               if re.fullmatch(r"3\.\d+\.\d+", str(p.get("cliVersion"))))


def owner(side="teamB", actor=7):
    return {"team": side, "id": actor, "kind": "champion", "name": "Vendetta"}


def entry(round=10, ordinal=10, value="1", target=None, phase="champions", context="0"):
    return {"timestamp": {"round": round, "ordinal": ordinal, "phase": phase, "input": ordinal, "substep": 0},
            "value": value, "amount": 0.0, "context": context, "target": target if target is not None else 0}


def group(kind, entries=(), side="teamB", actor=7):
    return {"kind": kind, "owner": owner(side, actor), "entries": list(entries)}


def statistics():
    actors = [dict(owner("teamA", 0), metrics={"earnedGold": "0", "spentGold": "0", "submittedGems": "0", "collectedGems": "0", "gems": "0", "gold": "0"}),
              dict(owner(), metrics={"earnedGold": "0", "spentGold": "0", "submittedGems": "0", "collectedGems": "0", "gems": "0", "gold": "0"})]
    return {"formatVersion": "1.0", "jsonFormatVersion": "1.0", "match": {"metricSemanticsVersion": 1,
            "engineVersion": PROFILE["engineVersion"], "engineBuildHash": PROFILE["engineBuildHash"], "outcome": "teamA",
            "seed": "18446744073709551615", "roundsStarted": 100, "roundsCompleted": 100, "finalRoundPartial": False},
            "teams": [{"team": side, "submittedGems": "0"} for side in n.SIDES], "champions": actors,
            "roster": {"champions": [{k: v for k, v in a.items() if k != "metrics"} for a in actors]},
            "events": [group("fuelExhausted", side="teamA", actor=0), group("fuelExhausted")],
            "diagnostics": {"botFaults": {"teamA": 0, "teamB": 0}, "codes": [], "detailedCaptureComplete": True}}


class NativeAnalysisTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / ".cache" / "frontier-tests" / "native-analysis"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def analyze(self, stats=None, a=b"", b=b"", side="teamB", debug=b"", include_stdout=True):
        if stats is not None:
            (self.directory / "statistics.json").write_text(json.dumps(stats), encoding="utf-8")
        replay = self.directory / "replay"
        replay.mkdir(exist_ok=True)
        with zipfile.ZipFile(replay / "fixture.repl26a", "w") as archive:
            if include_stdout:
                archive.writestr("team-a.stdout", a)
                archive.writestr("team-b.stdout", b)
            archive.writestr("team-a.debug", b"")
            archive.writestr("team-b.debug", debug)
        return n.analyze(self.directory, side, "p10-fixture")

    def test_empty_fuel_groups_are_zero_not_exhaustions(self):
        result = self.analyze(statistics())
        self.assertEqual(result["fuel"]["teamB"]["observedCount"], 0)
        self.assertEqual(result["fuel"]["teamB"]["totalCount"], 0)
        self.assertEqual(result["candidateTechnicalFailuresObserved"], 0)
        self.assertIn("zero_secured_output", [v["kind"] for v in result["firstFailures"]])

    def test_side_attribution_and_all_actor_round_counts(self):
        s = statistics()
        s["events"][0]["entries"] = [entry(3, 1), entry(3, 2)]
        s["events"][1]["entries"] = [entry(8, 10), entry(9, 11), entry(10, 12), entry(12, 13), entry(12, 14)]
        result = self.analyze(s, a=b"p10 error [p10-other]: wrong opponent error\n", b=b"\x00\xff\x17p10 decision actor=7 round=8 source=18 task=1 template=2 owner=7 predicted=[10,20] reason=work fuel=100000/60000\n")
        self.assertEqual(result["candidate"]["side"], "teamB")
        self.assertEqual(result["runtimeValidationErrors"]["teamA"]["count"], 1)
        self.assertEqual(result["runtimeValidationErrors"]["teamB"]["count"], 0)
        self.assertEqual(result["fuel"]["teamB"]["byActor"][0]["roundRanges"], [[8, 10, 1], [12, 12, 2]])
        self.assertEqual(result["fuel"]["teamB"]["observedCount"], 5)
        self.assertEqual(result["candidateTechnicalFailuresObserved"], 5)
        self.assertEqual(result["timing"]["unboundIntervals"], 1)
        self.assertEqual(result["timing"]["comparisonCount"], 0)

    def test_candidate_a_not_draft_slot(self):
        s = statistics()
        s["diagnostics"]["botFaults"]["teamB"] = 2
        result = self.analyze(s, side="A")
        self.assertEqual(result["candidateTechnicalFailuresObserved"], 0)
        self.assertEqual(next(f for f in result["firstFailures"] if f["kind"] == "bot_fault")["candidate"], False)

    def test_native_fault_counts_and_guest_markers(self):
        s = statistics()
        s["diagnostics"]["botFaults"]["teamB"] = 3
        s["events"].append(group("botFault", [entry(7, 8, context="guestError: api_not_in_vision")]))
        result = self.analyze(s, b=b"\0p10 error [p10-fixture]: api_not_in_vision\n", debug=b"guest error: wasm trap\n")
        self.assertEqual(result["diagnostics"]["nativeBotFaults"]["teamB"], 3)
        self.assertEqual(result["runtimeValidationErrors"]["teamB"]["count"], 1)
        self.assertEqual(result["guestErrorMarkers"]["teamB"]["count"], 1)
        self.assertEqual(result["status"], "technical_failure_observed")
        error = result["runtimeValidationErrors"]["teamB"]["examples"][0]
        self.assertNotIn("actor", error)  # unlogged identity is unknown
        self.assertNotIn("round", error)

    def test_missing_actor_fuel_coverage_is_unknown(self):
        s = statistics()
        s["events"] = s["events"][:1]
        result = self.analyze(s)
        self.assertIsNone(result["fuel"]["teamB"]["totalCount"])
        self.assertFalse(result["fuel"]["teamB"]["completeOwnerCoverage"])
        self.assertEqual(result["status"], "evidence_incomplete")

    def test_partial_capture_keeps_observed_lower_bound(self):
        s = statistics()
        s["diagnostics"]["detailedCaptureComplete"] = False
        s["events"][1]["entries"] = [entry()]
        result = self.analyze(s)
        self.assertEqual(result["fuel"]["teamB"]["observedCount"], 1)
        self.assertIsNone(result["fuel"]["teamB"]["totalCount"])

    def test_mismatched_fault_counts_still_surface_observed_fault(self):
        s = statistics()
        s["events"].append(group("botFault", [entry(4, 4, context="guestError")]))
        r = self.analyze(s)
        self.assertTrue(r["candidateHasTechnicalFailure"])
        self.assertEqual(r["diagnostics"]["observedBotFaultEvents"]["teamB"], 1)
        self.assertIn("bot_fault_count_mismatch", [i["kind"] for i in r["issues"]])

    def test_invalid_fuel_owner_not_assigned_to_candidate(self):
        s = statistics()
        s["events"].append(group("fuelExhausted", [entry()], side="teamA", actor=7))
        r = self.analyze(s)
        self.assertEqual(r["fuel"]["teamB"]["observedCount"], 0)
        self.assertIn("invalid_event_owner", [i["kind"] for i in r["issues"]])

    def test_unsupported_exact_engine_identity_stays_unqualified(self):
        s = statistics()
        s["match"]["engineBuildHash"] = "0" * 64
        r = self.analyze(s)
        self.assertIsNone(r["coverage"]["engineProfile"])
        self.assertEqual(r["status"], "evidence_incomplete")

    def test_registered_current_engine_and_ambiguous_identity(self):
        profile = {**PROFILE, "id": "cli-3.0.4", "cliVersion": "3.0.4", "engineVersion": "1.0.2", "engineBuildHash": "d" * 64}
        stats = statistics()
        stats["match"].update(engineVersion=profile["engineVersion"], engineBuildHash=profile["engineBuildHash"])
        registry = self.directory / "profiles.json"
        registry.write_text(json.dumps({"schemaVersion": 1, "profiles": [PROFILE, profile]}), encoding="utf8")
        with patch.object(n, "PROFILE_PATH", registry):
            result = self.analyze(stats)
            self.assertEqual(result["coverage"]["engineProfile"], "cli-3.0.4")
            self.assertNotIn("unsupported_engine_identity", [i["kind"] for i in result["issues"]])
            registry.write_text(json.dumps({"schemaVersion": 1, "profiles": [profile, dict(profile, id="duplicate")]}), encoding="utf8")
            result = self.analyze(stats)
            self.assertIsNone(result["coverage"]["engineProfile"])
            self.assertEqual(result["status"], "evidence_incomplete")

    def test_missing_statistics_is_not_zero_output(self):
        result = self.analyze(None)
        self.assertIsNone(result["economy"]["submittedGems"])
        self.assertEqual(result["economy"]["evidenceState"], "unknown")
        self.assertNotIn("zero_secured_output", [v["kind"] for v in result["firstFailures"]])

    def test_capture_bank_purchase_renewal_and_combination(self):
        s = statistics()
        target = {"kind": "campGroup", "id": 18}
        s["events"] += [group("campClear", [entry(4, 4, target=target)]),
                        group("gemCollection", [entry(10, 10, "7", target, "campCaptures"), entry(60, 60, "7", target, "campCaptures")]),
                        group("buyItem", [entry(12, 12, target={"kind": "item", "name": "Power Shard of Strength"})]),
                        group("itemCombination", [entry(12, 13)]), group("gemSubmission", [entry(20, 20, "7", phase="championStartTurn")])]
        s["teams"][1]["submittedGems"] = "7"
        r = self.analyze(s)
        e = r["economy"]
        self.assertEqual(e["captureOccurrencesObserved"], 2)
        self.assertEqual(e["campClearCredits"], 1)
        self.assertEqual(e["renewedCapturesObserved"], 1)
        self.assertEqual(e["capturePurchaseBankRenewalLoops"], 1)
        self.assertEqual(e["personalPurchaseEvents"], 1)
        self.assertEqual(e["firstCapture"]["group"], 18)
        self.assertEqual(e["submittedGems"], 7)
        self.assertTrue(e["submissionSumMatches"])

    def test_camp_clear_is_not_capture_and_rival_loot_not_camp(self):
        s = statistics()
        s["events"] += [group("campClear", [entry(target={"kind": "campGroup", "id": 18})]),
                        group("gemCollection", [entry(target={"kind": "champion", "id": 0}, context="champion")])]
        r = self.analyze(s)
        self.assertEqual(r["economy"]["campClearCredits"], 1)
        self.assertEqual(r["economy"]["captureOccurrencesObserved"], 0)

    def test_parties_deduplicate_capture_not_personal_shares(self):
        s = statistics()
        partner = dict(owner("teamB", 8), metrics={})
        s["champions"].append(partner)
        s["roster"]["champions"].append({k: v for k, v in partner.items() if k != "metrics"})
        s["events"] += [group("fuelExhausted", actor=8),
                        group("gemCollection", [entry(10, 10, "4", {"kind": "campGroup", "id": 18}, "campCaptures")]),
                        group("gemCollection", [entry(10, 11, "3", {"kind": "campGroup", "id": 18}, "campCaptures")], actor=8)]
        r = self.analyze(s)
        self.assertEqual(r["economy"]["captureOccurrencesObserved"], 1)
        self.assertEqual(r["economy"]["captureRecipientsObserved"], 2)
        self.assertEqual(r["economy"]["firstCapture"]["observedGems"], 7)
        self.assertEqual(r["economy"]["firstCapture"]["actors"], [7, 8])

    def test_explicit_predictions_and_runtime_phase_logs(self):
        s = statistics()
        s["events"] += [group("gemCollection", [entry(16, 16, "7", {"kind": "campGroup", "id": 18}, "campCaptures")]),
                        group("gemSubmission", [entry(20, 20, "7", phase="championStartTurn")])]
        s["teams"][1]["submittedGems"] = 7
        payload = (b"\x00\x16p10 prediction actor=7 round=10 source=18 predictionEvent=payout predicted=[16,16] condition=continuous-uncontested\n"
                   b"p10 prediction actor=7 round=17 source=-1 predictionEvent=bank predicted=[19,19] condition=safe-channel\n"
                   b"p10 phase actor=7 round=10 phase=observe fuel=100000/80000/50000 complete=0 groups=12\n")
        r = self.analyze(s, b=payload)
        self.assertEqual(r["timing"]["explicitEventIntervals"], 2)
        self.assertEqual(r["timing"]["outsideInterval"], 1)
        self.assertEqual(r["timing"]["comparisons"][0]["condition"], "continuous-uncontested")
        self.assertTrue(r["timing"]["comparisons"][0]["insideInterval"])
        self.assertEqual(r["runtimePhases"]["byPhase"]["observe"]["incomplete"], 1)
        self.assertEqual(r["runtimePhases"]["byPhase"]["observe"]["minimumFuelAfter"], 50000)

    def test_superseded_prediction_not_declared_success(self):
        s = statistics()
        s["events"].append(group("gemSubmission", [entry(20, 20, "7")]))
        payload = (b"p10 prediction actor=7 round=10 source=-1 predictionEvent=bank predicted=[20,20]\n"
                   b"p10 decision actor=7 round=15 source=18 task=4 template=0 owner=7 predicted=[-1,-1] reason=retreat fuel=100000/50000\n")
        r = self.analyze(s, b=payload)
        self.assertEqual(r["timing"]["comparisons"][0]["state"], "superseded")
        self.assertNotIn("insideInterval", r["timing"]["comparisons"][0])

    def test_new_capture_prediction_supersedes_earlier_same_source(self):
        s = statistics()
        s["events"].append(group("gemCollection", [entry(36, 36, "7", {"kind": "campGroup", "id": 18}, "campCaptures")]))
        payload = (b"p10 prediction actor=7 round=10 source=18 predictionEvent=payout predicted=[16,16] condition=continuous-uncontested\n"
                   b"p10 prediction actor=7 round=30 source=18 predictionEvent=payout predicted=[36,36] condition=continuous-uncontested\n")
        r = self.analyze(s, b=payload)["timing"]
        old, new = r["comparisons"]
        self.assertEqual(old["state"], "superseded")
        self.assertEqual(old["endedBy"]["reason"], "newer_same_actor_source_prediction")
        self.assertIsNone(old["observedTimestamp"])
        self.assertTrue(new["insideInterval"])
        self.assertEqual(r["outsideInterval"], 0)

    def test_explicit_capture_invalidation_cuts_only_matching_actor_source(self):
        s = statistics()
        s["events"].append(group("gemCollection", [entry(36, 36, "7", {"kind": "campGroup", "id": 18}, "campCaptures")]))
        pred = b"p10 prediction actor=7 round=10 source=18 predictionEvent=payout predicted=[16,16] condition=continuous-uncontested\n"
        wrong = (b"p10 invalidate capture actor=8 r=12 group=18\n"
                 b"p10 invalidate capture actor=7 r=13 group=19\n")
        r = self.analyze(s, b=pred+wrong)["timing"]
        self.assertEqual(r["outsideInterval"], 1)  # unrelated actors/groups cannot cancel this forecast
        r = self.analyze(s, b=pred+wrong+b"p10 invalidate capture actor=7 r=14 group=18\n")["timing"]
        self.assertEqual(r["outsideInterval"], 0)
        self.assertEqual(r["comparisons"][0]["endedBy"]["reason"], "actor_capture")

    def test_death_witness_and_actor_suspension_end_continuity(self):
        s = statistics()
        s["events"].append(group("gemCollection", [entry(36, 36, "7", {"kind": "campGroup", "id": 18}, "campCaptures")]))
        pred = b"p10 prediction actor=7 round=10 source=18 predictionEvent=payout predicted=[16,16] condition=continuous-uncontested\n"
        for ending, reason in ((b"p10 witnessed actor=7 round=12 kind=6 group=-1 gemsLower=0\n", "witnessed_death"),
                               (b"p10 invalidate suspended actor=7 r=12\n", "actor_suspended")):
            with self.subTest(reason=reason):
                r = self.analyze(s, b=pred+ending)["timing"]
                self.assertEqual(r["outsideInterval"], 0)
                self.assertEqual(r["comparisons"][0]["state"], "invalidated")
                self.assertEqual(r["comparisons"][0]["endedBy"]["reason"], reason)

    def test_native_death_and_fuel_also_end_the_owner_intent(self):
        for kind in ("death", "fuelExhausted"):
            with self.subTest(kind=kind):
                s = statistics()
                s["events"].append(group("gemCollection", [entry(36, 36, "7", {"kind": "campGroup", "id": 18}, "campCaptures")]))
                if kind == "fuelExhausted":
                    s["events"][1]["entries"] = [entry(12, 12)]
                else:
                    s["events"].append(group(kind, [entry(12, 12)]))
                r = self.analyze(s, b=b"p10 prediction actor=7 round=10 source=18 predictionEvent=payout predicted=[16,16] condition=continuous-uncontested\n")["timing"]
                self.assertEqual(r["outsideInterval"], 0)
                self.assertEqual(r["comparisons"][0]["endedBy"]["reason"], "native_"+kind)

    def test_legacy_unattributed_invalidation_and_missing_condition_are_unknown(self):
        s = statistics()
        s["events"].append(group("gemCollection", [entry(36, 36, "7", {"kind": "campGroup", "id": 18}, "campCaptures")]))
        pred = b"p10 prediction actor=7 round=10 source=18 predictionEvent=payout predicted=[16,16] condition=continuous-uncontested\n"
        for ending in (b"p10 invalidate capture r=12 group=18\n", b"p10 invalidate suspended r=12\n"):
            with self.subTest(ending=ending):
                r = self.analyze(s, b=pred+ending)["timing"]
                self.assertEqual(r["outsideInterval"], 0)
                self.assertEqual(r["comparisons"][0]["state"], "conditional_unknown")
                self.assertIsNone(r["comparisons"][0]["endedBy"]["actor"])
        r = self.analyze(s, b=b"p10 prediction actor=7 round=10 source=18 predictionEvent=payout predicted=[16,16]\n")["timing"]
        self.assertEqual(r["outsideInterval"], 0)
        self.assertEqual(r["comparisons"][0]["unknownReason"], "condition_not_logged")

    def test_intervening_actual_capture_ends_old_source_lifecycle(self):
        s = statistics()
        s["events"] += [group("gemCollection", [entry(16, 16, "7", {"kind": "campGroup", "id": 18}, "campCaptures")], side="teamA", actor=0),
                        group("gemCollection", [entry(36, 36, "7", {"kind": "campGroup", "id": 18}, "campCaptures")])]
        r = self.analyze(s, b=b"p10 prediction actor=7 round=10 source=18 predictionEvent=payout predicted=[16,16] condition=continuous-uncontested\n")["timing"]
        self.assertEqual(r["outsideInterval"], 0)
        self.assertEqual(r["comparisons"][0]["endedBy"]["reason"], "intervening_source_capture")

    def test_owner_start_invalidation_cannot_undo_same_round_payout(self):
        s = statistics()
        s["events"].append(group("gemCollection", [entry(16, 16, "7", {"kind": "campGroup", "id": 18}, "campCaptures")]))
        payload = (b"p10 prediction actor=7 round=10 source=18 predictionEvent=payout predicted=[16,16] condition=continuous-uncontested\n"
                   b"p10 invalidate capture actor=7 r=16 group=18\n")
        r = self.analyze(s, b=payload)["timing"]
        self.assertEqual(r["comparisons"][0]["state"], "matched")
        self.assertTrue(r["comparisons"][0]["insideInterval"])

    def test_true_outside_survives_with_unbroken_logged_premise(self):
        s = statistics()
        s["events"].append(group("gemCollection", [entry(18, 18, "7", {"kind": "campGroup", "id": 18}, "campCaptures")]))
        r = self.analyze(s, b=b"p10 prediction actor=7 round=10 source=18 predictionEvent=payout predicted=[16,16] condition=continuous-uncontested\n")["timing"]
        self.assertEqual(r["outsideInterval"], 1)
        self.assertEqual(r["comparisons"][0]["state"], "matched")
        self.assertFalse(r["comparisons"][0]["insideInterval"])

    def test_missing_stdout_never_proves_clean_execution(self):
        r = self.analyze(statistics(), include_stdout=False)
        self.assertFalse(r["coverage"]["candidateStdout"])
        self.assertEqual(r["status"], "evidence_incomplete")

    def test_input_not_mutated(self):
        s = statistics()
        before = deepcopy(s)
        self.analyze(s)
        self.assertEqual(s, before)


if __name__ == "__main__":
    unittest.main()
