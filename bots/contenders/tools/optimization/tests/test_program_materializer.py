"""Focused native program input, patching and complete-choice parser checks."""
from __future__ import annotations

import importlib.util
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[5]
MODULE = Path(__file__).resolve().parents[1] / "adapters/program_materializer.py"
spec = importlib.util.spec_from_file_location("program_materializer", MODULE)
materializer = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(materializer)


class ProgramInputs(unittest.TestCase):
    def test_tree_types_and_float32_semantics(self):
        tree = materializer.validate_tree({"feature": 0, "threshold": 0.1,
                                          "left": {"factor": 0.5}, "right": {"factor": 2}})
        self.assertNotEqual(tree["threshold"], 0.1)
        self.assertIn("0x", materializer._tree_cpp(tree))
        self.assertEqual(materializer._max_factor(tree), 2)

    def test_tree_rejects_invalid_and_unbounded_programs(self):
        invalid = [{"factor": float("nan")}, {"factor": True}, {"factor": 0},
                   {"factor": 9}, {"feature": 12, "threshold": .5, "left": {"factor": 1}, "right": {"factor": 1}},
                   {"feature": 0, "threshold": 1.1, "left": {"factor": 1}, "right": {"factor": 1}},
                   {"factor": 1, "typo": 2}]
        nested = {"factor": 1}
        for _ in range(8):
            nested = {"feature": 0, "threshold": .5, "left": nested, "right": {"factor": 1}}
        invalid.append(nested)
        for tree in invalid:
            with self.subTest(tree=str(tree)[:80]), self.assertRaises(ValueError):
                materializer.validate_tree(tree)

    def test_policy_dimensions_seed_and_bounds(self):
        valid = {"weights": [[0] * 13 for _ in range(5)], "trace": True}
        policy = materializer.validate_policy(valid)
        self.assertEqual(policy["max_decisions"], 128)
        self.assertTrue(policy["trace"])
        for bad in [{"weights": [[0] * 12 for _ in range(5)]},
                    dict(valid, seed=-1), dict(valid, max_decisions=257),
                    dict(valid, stochastic="yes"), dict(valid, feature_names=["wrong"] * 12)]:
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                materializer.validate_policy(bad)

    def test_policy_import_rejects_changed_native_semantics(self):
        valid = {"weights": [[0] * 13 for _ in range(5)]}
        for feature_schema in ["incompatible/v9", "", None, True]:
            with self.subTest(feature_schema=feature_schema), self.assertRaises(ValueError):
                materializer.validate_policy({"policy": dict(valid, feature_schema=feature_schema)})
        for clip in [0, 1, 20.00000001, float("nan"), float("inf"), -float("inf"), True, "20", None]:
            with self.subTest(logit_clip=clip), self.assertRaises(ValueError):
                materializer.validate_policy({"policy": dict(valid, logit_clip=clip)})
        for clip in [20, 20.0]:
            normalized = materializer.validate_policy({"policy": dict(valid,
                feature_schema=materializer.ACTOR_FEATURE_SCHEMA, logit_clip=clip)})
            self.assertEqual(normalized["feature_schema"], materializer.ACTOR_FEATURE_SCHEMA)
            self.assertEqual(normalized["logit_clip"], 20.0)

    def test_policy_compatible_header_bytes_are_preserved(self):
        policy = {"weights": [[((a * 13 + j) % 17 - 8) / 8 for j in range(13)] for a in range(5)],
                  "seed": 31, "max_decisions": 16, "stochastic": True, "trace": True}
        implicit = materializer.validate_policy({"policy": policy})
        explicit = materializer.validate_policy({"policy": dict(policy,
            feature_schema=materializer.ACTOR_FEATURE_SCHEMA, logit_clip=20.0)})
        self.assertEqual(implicit, explicit)
        header = materializer._config_header("policy", None, implicit).encode("utf-8")
        # Captured before adding import validation; compatible native source stays exact.
        self.assertEqual(hashlib.sha256(header).hexdigest(),
                         "7175a82860d7d7bc2a763ca531211d5bec56179132eb88fc7d4501176c42e3bb")

    def test_materialization_actual_scoring_and_pruning_hooks(self):
        scratch = ROOT / ".cache/contenders/optimization/c1-materializer/tests"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            repo = Path(directory)
            src = repo / "bots/anchors/main-v42/src"
            src.mkdir(parents=True)
            (src / "main.cpp").write_text("int main() { return 0; }\n")
            original = ('#include "world.h"\nfloat Brain::score_group(const Ctx& ctx, int group, int* need_out) const\n'
                        '{\n    return rate;\n}\n\nint Brain::choose_target(const Ctx& ctx, float* out_score)\n'
                        '{\n    bound[g] = reward / static_cast<float>(std::max(1, travel + capture));\n}\n')
            (src / "brain.cpp").write_text(original)
            tree = {"feature": 7, "threshold": .5, "left": {"factor": .25}, "right": {"factor": 4}}
            result = materializer.materialize_program(repo, {"tree": tree})
            generated = repo / "bots/contenders/_optimizer-program/src"
            brain = (generated / "brain.cpp").read_text()
            self.assertIn("rate * optimizer::factor(ctx, info, belief, travel)", brain)
            self.assertIn("* optimizer::upper_bound(ctx, info, Knowledge::get().groups[g], travel)", brain)
            self.assertEqual(result["metadata"]["pruning_upper_bound"], 4)
            neutral = materializer.materialize_program(repo, {"tree": {"factor": 1}})
            self.assertEqual((generated / "brain.cpp").read_text(), original)
            self.assertTrue(neutral["metadata"]["identity_program_unpatched"])
            self.assertEqual((src / "brain.cpp").read_text(), original)
            policy = materializer.materialize_program(repo, {"policy": {
                "weights": [[0] * 13 for _ in range(5)], "max_decisions": 16, "trace": True}}, "policy")
            metadata = policy["metadata"]
            self.assertEqual(metadata["emission_contract"]["minimum_fuel"], metadata["actor_sampling_fuel_floor"])
            self.assertEqual(metadata["emission_contract"]["cap_per_actor"], 16)
            self.assertIn("fuel() < " + str(metadata["actor_sampling_fuel_floor"]),
                          (generated / "optimizer_runtime.h").read_text())
            with self.assertRaises(ValueError):
                materializer.materialize_program(repo, {"base": "../../escape"})
            (src / "main.cpp").write_text('#include "../../shared/other.h"\nint main() {}\n')
            with self.assertRaisesRegex(ValueError, "external source include"):
                materializer.materialize_program(repo, {"tree": {"factor": 1}})


def observation(actor=3, decision=1, round_number=20):
    return {"schema": materializer.TRACE_SCHEMA, "feature_schema": materializer.ACTOR_FEATURE_SCHEMA,
            "actor": actor, "decision": decision, "round": round_number,
            "features": [.1, .9, .1, 0, 0, .2, 0, 0, 0, 0, 0, .1], "action": 2,
            "probabilities": [.2] * 5, "log_prob": -1.60943791, "legal_mask": [True] * 5}


class CompleteChoiceTrace(unittest.TestCase):
    def test_integer_encoded_samples_preserve_float32_bits(self):
        original = observation()
        encoded = {key: value for key, value in original.items() if key not in ["features", "probabilities", "log_prob"]}
        def bits(value):
            return struct.unpack("<I", struct.pack("<f", value))[0]
        encoded.update(encoding="float32-bits", feature_bits=[bits(x) for x in original["features"]],
                       probability_bits=[bits(x) for x in original["probabilities"]])
        result = materializer.parse_rollouts("OPT_TRAJ " + json.dumps(encoded))[0]
        self.assertEqual([bits(x) for x in result["features"]], encoded["feature_bits"])
        self.assertEqual([bits(x) for x in result["probabilities"]], encoded["probability_bits"])
        for bad in [dict(encoded, probability_bits=[0] * 5), dict(encoded, feature_bits=[0x7F800000] * 12),
                    dict(encoded, encoding="unknown"), dict(encoded, features=original["features"])]:
            with self.assertRaises(ValueError):
                materializer.parse_rollouts("OPT_TRAJ " + json.dumps(bad))

    def test_native_records_allow_interleaved_actors(self):
        lines = [observation(3), observation(7), observation(3, 2, 22)]
        text = "ordinary log\n" + "\n".join("OPT_TRAJ " + json.dumps(item) for item in lines)
        self.assertEqual(materializer.parse_rollouts(text), lines)

    def test_reject_gaps_duplicates_bad_probabilities_and_hidden_features(self):
        bad = [observation(decision=2), dict(observation(), probabilities=[.1] * 5),
               dict(observation(), log_prob=-2), dict(observation(), legal_mask=[True] * 4),
               dict(observation(), features=[float("nan")] * 12)]
        leaked = observation()
        leaked["features"][7] = .5
        bad.append(leaked)
        for item in bad:
            with self.subTest(item=item), self.assertRaises(ValueError):
                materializer.parse_rollouts("OPT_TRAJ " + json.dumps(item))
        duplicate = "\n".join(["OPT_TRAJ " + json.dumps(observation())] * 2)
        with self.assertRaises(ValueError):
            materializer.parse_rollouts(duplicate)
        with self.assertRaises(ValueError):
            materializer.parse_rollouts("OPT_TRAJ broken")


if __name__ == "__main__":
    unittest.main()
