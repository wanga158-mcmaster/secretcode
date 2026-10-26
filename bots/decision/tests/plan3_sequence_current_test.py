"""Focused offline solver checks; no SDK, native game or generated-table import."""
import importlib.util
from pathlib import Path
import sys
import unittest
from fractions import Fraction as F

path = Path(__file__).resolve().parents[1] / "tools/export_sequence.py"
spec = importlib.util.spec_from_file_location("sequence_current", path)
s = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = s
spec.loader.exec_module(s)


class SequenceTests(unittest.TestCase):
    def test_matching_pennies_and_exact_two_column_oracle(self):
        nodes = [s.Node(1, "opponent", (0, 1), (1, 2)),
                 s.Node(0, "hidden", (0, 1), (3, 4)), s.Node(0, "hidden", (0, 1), (5, 6))]
        nodes += [s.Node(2, payoff=F(x)) for x in (1, -1, -1, 1)]
        average, _, _ = s.cfr(nodes, 1000)
        self.assertEqual(average, {"opponent": [0.5, 0.5], "hidden": [0.5, 0.5]})
        value, support, opponent = s.normal_form([[F(1), F(-1)], [F(-1), F(1)]])
        self.assertEqual((value, support, opponent), (0, ((0, F(1, 2)), (1, F(1, 2))), F(1, 2)))

    def test_counterfactual_excludes_own_reach_and_average_uses_it(self):
        nodes = [s.Node(0, "root", (0, 1), (1, 2)), s.Node(0, "child", (0, 1), (3, 4)),
                 s.Node(2), s.Node(2, payoff=F(4)), s.Node(2)]
        _, regret, total = s.cfr(nodes, 1)
        self.assertEqual(regret["child"], [2, -2])
        self.assertEqual(regret["root"], [1, -1])
        self.assertEqual(total["child"], [0.25, 0.25])

    def test_counterfactual_includes_chance_reach(self):
        nodes = [s.Node(-1, actions=(0, 1), children=(1, 2), probabilities=(F(1, 4), F(3, 4))),
                 s.Node(0, "hidden", (0, 1), (3, 4)), s.Node(0, "hidden", (0, 1), (5, 6))]
        nodes += [s.Node(2, payoff=F(x)) for x in (2, 0, 0, 1)]
        _, regret, total = s.cfr(nodes, 1)
        self.assertEqual(regret["hidden"], [-0.125, 0.125])
        self.assertEqual(total["hidden"], [0.5, 0.5])  # Once, not once per hidden history.

    def test_information_sets_actions_perfect_recall_and_invalid_tree(self):
        wrong_actions = [s.Node(-1, actions=(0, 1), children=(1, 2), probabilities=(F(1, 2), F(1, 2))),
                         s.Node(0, "I", (0,), (3,)), s.Node(0, "I", (1,), (4,)), s.Node(2), s.Node(2)]
        with self.assertRaisesRegex(ValueError, "inconsistent"):
            s.validate(wrong_actions)
        forgotten = [s.Node(0, "root", (0, 1), (1, 2)), s.Node(0, "I", (0,), (3,)),
                     s.Node(0, "I", (0,), (4,)), s.Node(2), s.Node(2)]
        with self.assertRaisesRegex(ValueError, "perfect recall"):
            s.validate(forgotten)
        with self.assertRaises(ValueError):
            s.validate([s.Node(0, "cycle", (0,), (0,))])

    def test_same_model_normal_form_expansion_exact_behavioralization(self):
        def evaluate(nodes, pure, opponent):
            def visit(at):
                n = nodes[at]
                if n.player == 2:
                    return n.payoff
                if n.player == -1:
                    return sum(p * visit(child) for p, child in zip(n.probabilities, n.children))
                if n.player == 1:
                    action = opponent
                elif n.info == "first":
                    action = pure[0]
                else:
                    _, first, signal, mask = n.info.split(":")
                    self.assertEqual(int(first), pure[0])
                    action = pure[int(signal) + 1] if int(mask) == 3 else s.actions(int(mask))[0]
                return visit(n.children[n.actions.index(action)])
            return visit(0)
        for mask in (1, 2, 3):
            nodes, rows = s.game(mask), s.matrix(mask)
            self.assertLessEqual(len(nodes), 91)
            s.validate(nodes)
            for row, pure in zip(rows, s.campaigns(mask)):
                for opponent in (0, 1):
                    self.assertEqual(row[opponent], evaluate(nodes, pure, opponent))
            value, support, opponent = s.normal_form(rows)
            behavioral = s.behavioral(mask, support)
            columns = s.columns(mask, behavioral)
            for column in (0, 1):
                expected = sum(p * rows[i][column] for i, p in support)
                self.assertAlmostEqual(columns[column], float(expected), places=12)
            self.assertAlmostEqual(min(columns), float(value), places=12)
            self.assertEqual(max(opponent * row[0] + (1 - opponent) * row[1] for row in rows), value)
            average, _, _ = s.cfr(nodes, 3000)
            best = max(average["opponent"][0] * row[0] + average["opponent"][1] * row[1] for row in rows)
            self.assertLess(float(best) - min(s.columns(mask, average)), 0.5)


if __name__ == "__main__":
    unittest.main()
