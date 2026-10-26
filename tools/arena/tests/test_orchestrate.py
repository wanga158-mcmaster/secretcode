"""Offline ownership/dispatch invariants. No builds, games, slots or external workers."""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import io
import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import orchestrate as orch
from core import ArenaError


class Ownership(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="arena-orchestration-")
        self.root = Path(self.temp.name).resolve()
        self.cache = self.root / ".cache/arena/orchestration"
        self.a = self.write("bots/odd/alpha/src/main.cpp", "alpha")
        self.shared = self.write("bots/odd/alpha/src/shared.h", "shared")
        self.b = self.write("bots/other/beta/src/main.cpp", "beta")
        self.c = self.write("bots/third/gamma/src/main.cpp", "gamma")
        self.readme = self.write("bots/odd/README.md", "keep peculiar policy")
        self.write("docs/workflow.md", "test policy")
        self.files = {"odd/alpha": [self.a, self.shared], "other/beta": [self.b, self.shared], "third/gamma": [self.c]}
        self.bots = [SimpleNamespace(id=i, suite=i.split('/')[0], name=i.split('/')[1]) for i in self.files]
        self.patches = [patch.object(orch, "ROOT", self.root), patch.object(orch, "CACHE", self.cache),
                        patch.object(orch.core, "all_bots", return_value=self.bots),
                        patch.object(orch.core, "closure", side_effect=lambda b: self.files[b.id])]
        for p in self.patches:
            p.start()
        self.rows = [{"bot": i, "state": "unassessed", "gaps": ["native witness unavailable"],
                      "contract": {"id": i.replace('/', '.'), "interaction": "strange interaction", "next_check": "native path"}}
                     for i in self.files]

    def tearDown(self):
        for p in reversed(self.patches):
            p.stop()
        self.temp.cleanup()

    def write(self, path, text):
        p = self.root / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8")
        return p

    def offer(self, bot="odd/alpha"):
        return next(o for o in orch.derive_offers(self.rows) if o["bot"] == bot and o["kind"] != "exploration")

    def acquire(self, offer=None, worker="w1"):
        orch.reconcile_workers("Inspected current native workers and their scopes.")
        return orch.claim(offer or self.offer(), worker)

    def result(self, **extra):
        return {"changed_paths": {}, "evidence_paths": [], "observed_contribution": "none: mechanism still unwitnessed",
                "limitations": ["local fixture only"], "next_obstruction": "native witness", "coherent_boundary": True,
                "relinquished": True, **extra}

    def test_offers_preserve_doctrine_and_exploration_during_repair(self):
        self.rows[0]["state"] = "repair"
        offers = orch.derive_offers(self.rows)
        self.assertEqual(offers[0]["kind"], "repair")
        self.assertEqual(sum(o["kind"] == "exploration" for o in offers), 1)
        self.assertEqual(offers[0]["preserve"], "strange interaction")
        self.assertEqual(offers[0]["next_check"], "native path")
        self.assertNotIn("bots/odd/alpha/src/shared.h", offers[0]["owned_paths"])
        self.assertNotIn("elo", json.dumps(offers).lower())

    def test_ready_requires_no_repair_assignment_but_can_explore(self):
        self.rows[0]["state"] = "ready"
        offers = orch.derive_offers(self.rows)
        self.assertFalse(any(o["bot"] == "odd/alpha" and o["kind"] != "exploration" for o in offers))

    def test_cache_absence_requires_explicit_worker_reconciliation(self):
        with self.assertRaisesRegex(ArenaError, "reconcile known workers"):
            orch.claim(self.offer(), "w")
        self.assertFalse(orch.status()["reconciled"])

    def test_changed_read_dependency_invalidates_offer(self):
        offer = self.offer()
        self.shared.write_text("new shared", encoding="utf-8")
        with self.assertRaisesRegex(ArenaError, "inputs changed"):
            self.acquire(offer)
        self.assertNotEqual(self.offer()["id"], offer["id"])

    def test_cached_inventory_identity_cannot_describe_new_source(self):
        self.rows[0]["identity"] = {"source": {"bots/odd/alpha/src/main.cpp": "old"}}
        with self.assertRaisesRegex(ArenaError, "cached assessment source changed"):
            orch.derive_offers(self.rows)

    def test_variant_assessment_pins_raw_working_source_for_assignment(self):
        raw = orch._hash_inputs(self.files["odd/alpha"])
        self.rows[0]["identity"] = {"working_source": raw,
                                     "source": {name: "applied-variant-hash" for name in raw},
                                     "overrides": {"feature": "1"}}
        offer = self.offer()
        self.assertEqual(offer["source_identity"], raw)
        self.a.write_text("different raw default", encoding="utf-8")
        with self.assertRaisesRegex(ArenaError, "cached assessment source changed"):
            orch.derive_offers(self.rows)

    def test_cached_inventory_test_and_contract_changes_fail_closed(self):
        self.write("bots/odd/tests/population.py", "adapter")
        self.rows[0]["identity"] = {"contract": orch._digest(self.rows[0]["contract"]),
                                     "tests": orch._hash_inputs(["bots/odd/tests"])}
        self.write("bots/odd/tests/new-case.cpp", "new fixture")
        with self.assertRaisesRegex(ArenaError, "cached assessment tests changed"):
            orch.derive_offers(self.rows)
        self.rows[0]["identity"]["tests"] = orch._hash_inputs(["bots/odd/tests"])
        self.rows[0]["contract"]["interaction"] = "different promise"
        with self.assertRaisesRegex(ArenaError, "cached assessment contract changed"):
            orch.derive_offers(self.rows)

    def test_cached_test_identity_includes_shared_test_harness(self):
        self.write("tools/arena/wasi.py", "harness")
        self.rows[0]["identity"] = {"tests": orch._hash_inputs(["bots/odd/tests", "tools/arena/wasi.py"])}
        self.rows[0]["identity"]["tests"] = {p:None if h == "missing" else h for p,h in self.rows[0]["identity"]["tests"].items()}
        offers = orch.derive_offers(self.rows)
        self.assertIn("tools/arena/wasi.py", offers[0]["read_inputs"])

    def test_added_source_file_invalidates_offer_closure(self):
        offer = self.offer()
        added = self.write("bots/odd/alpha/src/new.cpp", "new")
        self.files["odd/alpha"].append(added)
        with self.assertRaisesRegex(ArenaError, "source closure changed"):
            self.acquire(offer)

    def test_added_test_file_after_offer_invalidates_read_inputs(self):
        offer = self.offer()
        self.write("bots/odd/tests/new-case.cpp", "fixture")
        with self.assertRaisesRegex(ArenaError, "inputs changed"):
            self.acquire(offer)

    def test_direct_scope_conflict(self):
        first = self.acquire()
        with self.assertRaisesRegex(ArenaError, first["id"]):
            orch.claim(self.offer(), "w2")

    def test_disjoint_bot_owners_can_share_readonly_dependency(self):
        self.acquire()
        second = orch.claim(self.offer("other/beta"), "w2")
        self.assertEqual(second["affected_bots"], ["other/beta"])

    def test_module_under_bot_directory_conflicts_via_reverse_closure(self):
        self.acquire(self.offer("other/beta"))
        module = self.offer()
        module["owned_paths"] = ["bots/odd/alpha/src/shared.h"]
        self.assertEqual(orch.affected_bots(module["owned_paths"]), ["odd/alpha", "other/beta"])
        with self.assertRaisesRegex(ArenaError, "scope conflicts"):
            orch.claim(module, "module-owner")

    def test_directory_ancestor_scope_conflicts(self):
        self.acquire()
        parent = self.offer()
        parent["owned_paths"] = ["bots/odd/alpha"]
        with self.assertRaisesRegex(ArenaError, "scope conflicts"):
            orch.claim(parent, "module-owner")

    def test_metadata_only_scope_can_be_claimed_separately(self):
        self.acquire()
        meta = self.offer()
        meta["owned_paths"] = ["bots/odd/README.md", "bots/odd/tests/population.py"]
        m = orch.claim(meta, "steward")
        self.assertEqual(m["affected_bots"], [])
        with self.assertRaisesRegex(ArenaError, "scope conflicts"):
            orch.claim(meta, "another-steward")

    def test_excluded_and_escape_scopes_rejected(self):
        for path in ["bots/main/main/src", "bots/MAIN/main/src", "bots/anchors/frozen/src", "bots/ANCHORS/frozen/src",
                     "bots/surprise/private/src", "bots/SURPRISE/private/src",
                     "bots/contenders/economy/src", "bots/CONTENDERS/economy/src", "sdk/cpp", "../elsewhere", "bots"]:
            offer = self.offer()
            offer["owned_paths"] = [path]
            with self.assertRaises(ArenaError, msg=path):
                self.acquire(offer)

    def test_excluded_offer_identity_rejected_before_input_or_source_read(self):
        original = self.offer()
        for bot in ("main/main", "MAIN/main", "surprise/private", "SURPRISE/private",
                    "contenders/economy", "CONTENDERS/economy", "anchors/ref", "ANCHORS/ref"):
            offer = dict(original, bot=bot)
            with self.subTest(bot=bot), patch.object(orch, "_hash_inputs") as inputs, \
                    patch.object(orch, "_current_source") as source:
                with self.assertRaisesRegex(ArenaError, "offer bot is excluded"):
                    orch._validate_offer(offer)
                inputs.assert_not_called()
                source.assert_not_called()

    def test_heuristics_source_can_receive_an_exclusive_claim(self):
        ident = "heuristics/satisficer"
        source = self.write("bots/heuristics/satisficer/src/main.cpp", "distinct aspiration controller")
        self.files[ident] = [source]
        self.bots.append(SimpleNamespace(id=ident, suite="heuristics", name="satisficer"))
        self.rows.append({"bot": ident, "state": "unassessed", "gaps": ["current native witness"],
                          "contract": {"id": "heuristics.satisficer", "interaction": "aspiration commitment",
                                       "next_check": "current native path"}})
        claimed = self.acquire(self.offer(ident))
        self.assertEqual(claimed["bot"], ident)
        self.assertEqual(claimed["affected_bots"], [ident])

    def test_worker_identity_and_source_boundary(self):
        c = self.acquire()
        with self.assertRaises(ArenaError):
            orch.start(c["id"], "someone-else")
        orch.start(c["id"], "w1")
        self.a.write_text("edited", encoding="utf-8")
        boundary = orch.checkpoint(c["id"], "w1")
        self.assertEqual(boundary["inputs"]["bots/odd/alpha/src/main.cpp"], hashlib.sha256(b"edited").hexdigest())
        with self.assertRaises(ArenaError):
            orch.finish(c["id"], "someone-else", self.result())

    def test_checkpoint_recomputes_expanded_dependencies_and_conflicts(self):
        alpha = self.acquire()
        beta = orch.claim(self.offer("other/beta"), "w2")
        self.files["odd/alpha"].append(self.b)
        with self.assertRaisesRegex(ArenaError, "expanded source closure conflicts"):
            orch.checkpoint(alpha["id"], "w1")
        orch.reconcile(beta["id"], "returned", "worker stopped at coherent boundary", worker_stopped=True)
        boundary = orch.checkpoint(alpha["id"], "w1")
        self.assertIn("bots/other/beta/src/main.cpp", boundary["source"])
        self.assertIn("bots/other/beta/src/main.cpp", boundary["inputs"])

    def test_checkpoint_captures_new_source_files(self):
        alpha = self.acquire()
        added = self.write("bots/odd/alpha/src/new.cpp", "new")
        self.files["odd/alpha"].append(added)
        boundary = orch.checkpoint(alpha["id"], "w1")
        self.assertIn("bots/odd/alpha/src/new.cpp", boundary["source"])

    def test_result_is_structured_and_all_changes_accounted(self):
        c = self.acquire()
        with self.assertRaisesRegex(ArenaError, "result missing"):
            orch.finish(c["id"], "w1", {})
        with self.assertRaisesRegex(ArenaError, "coherent source"):
            orch.finish(c["id"], "w1", self.result(coherent_boundary=False))
        self.a.write_text("edited", encoding="utf-8")
        with self.assertRaisesRegex(ArenaError, "omits changed"):
            orch.finish(c["id"], "w1", self.result())
        changed = {"bots/odd/alpha/src/main.cpp": hashlib.sha256(b"edited").hexdigest()}
        result = orch.finish(c["id"], "w1", self.result(changed_paths=changed))
        self.assertEqual(result["state"], "reported")

    def test_outside_scope_or_missing_evidence_rejected(self):
        c = self.acquire()
        outside = {"bots/other/beta/src/main.cpp": hashlib.sha256(self.b.read_bytes()).hexdigest()}
        with self.assertRaisesRegex(ArenaError, "outside claim"):
            orch.finish(c["id"], "w1", self.result(changed_paths=outside))
        with self.assertRaisesRegex(ArenaError, "evidence unavailable"):
            orch.finish(c["id"], "w1", self.result(evidence_paths=["results/nonexistent.json"]))

    def test_finish_does_not_release_without_coordinator_verification(self):
        c = self.acquire()
        orch.finish(c["id"], "w1", self.result())
        with self.assertRaisesRegex(ArenaError, "scope conflicts"):
            orch.claim(self.offer(), "next")
        with self.assertRaisesRegex(ArenaError, "worker stopped"):
            orch.reconcile(c["id"], "verified", "looks good")
        orch.reconcile(c["id"], "verified", "result checked and worker idle", worker_stopped=True)
        self.assertEqual(orch.claim(self.offer(), "next")["state"], "claimed")

    def test_interrupted_or_old_claim_cannot_be_stolen(self):
        c = self.acquire()
        orch.reconcile(c["id"], "interrupted", "worker unavailable, inspect partial source")
        state = orch._state()
        state["claims"][0]["claimed_at"] = "1900-01-01T00:00:00Z"
        orch._save(state)
        with self.assertRaisesRegex(ArenaError, "scope conflicts"):
            orch.claim(self.offer(), "next")
        with self.assertRaisesRegex(ArenaError, "worker stopped"):
            orch.reconcile(c["id"], "returned", "inspected partial source")
        with self.assertRaisesRegex(ArenaError, "structured worker result"):
            orch.reconcile(c["id"], "verified", "not enough", worker_stopped=True)
        orch.reconcile(c["id"], "returned", "stopped worker; inspected coherent partial source", worker_stopped=True)
        orch.claim(self.offer(), "next")

    def test_concurrent_claims_only_one_succeeds(self):
        orch.reconcile_workers("native ownership reconciled")
        offer = self.offer()
        answers = []
        barrier = threading.Barrier(2)
        def take(worker):
            barrier.wait()
            try:
                answers.append(orch.claim(offer, worker)["state"])
            except ArenaError:
                answers.append("conflict")
        threads = [threading.Thread(target=take, args=(f"w{i}",)) for i in range(2)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertCountEqual(answers, ["claimed", "conflict"])

    def test_corrupt_claim_state_never_overwritten(self):
        self.cache.mkdir(parents=True)
        path = self.cache / "claims.json"
        path.write_text("bad state", encoding="utf-8")
        with self.assertRaisesRegex(ArenaError, "cannot read ownership state"):
            orch.reconcile_workers("review")
        self.assertEqual(path.read_text(encoding="utf-8"), "bad state")

    def test_cli_registration(self):
        parser = argparse.ArgumentParser()
        orch.register(parser.add_subparsers())
        args = parser.parse_args(["orchestrate", "claim", "o", "--worker", "w", "--path", "bots/odd/README.md"])
        self.assertEqual(args.path, ["bots/odd/README.md"])

    def test_cli_claim_pins_absent_shared_and_metadata_paths(self):
        orch.reconcile_workers("known workers reconciled")
        offer = self.offer()
        parser = argparse.ArgumentParser()
        orch.register(parser.add_subparsers())
        paths = ["bots/odd/common/src/initialization.h", "bots/odd/tests/population.py", "bots/odd/README.md"]
        command = ["orchestrate", "claim", offer["id"], "--worker", "module-steward"]
        for path in paths:
            command += ["--path", path]
        args = parser.parse_args(command)
        output = io.StringIO()
        with patch.object(orch, "derive_offers", return_value=[offer]), contextlib.redirect_stdout(output):
            self.assertEqual(args.func(args), 0)
        claimed = json.loads(output.getvalue())
        self.assertEqual(claimed["owned_paths"], sorted(paths))
        self.assertTrue(set(paths).issubset(claimed["read_input_roots"]))
        self.assertEqual(claimed["read_inputs"][paths[0]], "missing")
        self.assertEqual(claimed["read_inputs"][paths[1]], "missing")
        self.assertEqual(claimed["read_inputs"], orch._hash_inputs(claimed["read_input_roots"]))

    def test_cli_offer_file_path_override_rehashes_full_roots(self):
        orch.reconcile_workers("known workers reconciled")
        offer = self.offer()
        # The coordinator extends metadata scope at the same source version. A directory
        # that was absent now exists; the old missing-directory sentinel must disappear.
        new = self.write("bots/odd/tests/population.py", "new adapter")
        offer_path = self.write(".cache/offer.json", json.dumps(offer))
        parser = argparse.ArgumentParser()
        orch.register(parser.add_subparsers())
        args = parser.parse_args(["orchestrate", "claim", "--offer-file", str(offer_path), "--worker", "steward",
                                  "--path", "bots/odd/tests", "--path", "bots/odd/common/src/new.h"])
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(args.func(args), 0)
        claimed = json.loads(output.getvalue())
        self.assertNotIn("bots/odd/tests", claimed["read_inputs"])
        self.assertEqual(claimed["read_inputs"]["bots/odd/tests/population.py"], hashlib.sha256(new.read_bytes()).hexdigest())
        self.assertEqual(claimed["read_inputs"]["bots/odd/common/src/new.h"], "missing")


if __name__ == "__main__":
    unittest.main()
