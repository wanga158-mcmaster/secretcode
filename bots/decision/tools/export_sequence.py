"""Offline finite perfect-recall CFR and same-model normal-form export. Stdlib only.

Generates the Sequence bot's policy table (both arms: CFR active, normal-form control):
    python bots/decision/tools/export_sequence.py --header bots/decision/sequence-common/policy-data.h --report .cache/sequence-report.json
The committed header must equal a fresh export (bots/decision/tests/run.py checks it).
Analytical gem-equivalent payoffs/signal frequencies are unfitted assumptions.
No gameplay, SDK, native launch or empirical payoff loading occurs here.
Primary recurrence: https://papers.nips.cc/paper/3306-regret-minimization-in-games-with-incomplete-information
"""
from dataclasses import dataclass
from fractions import Fraction as F
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path


@dataclass(frozen=True)
class Node:
    player: int  # -1 chance, 0/1 players, 2 terminal; utility is for player0.
    info: str = ""
    actions: tuple = ()
    children: tuple = ()
    probabilities: tuple = ()
    payoff: F = F(0)


def validate(nodes):
    if not 1 <= len(nodes) <= 256:
        raise ValueError("bounded tree required")
    seen, infos = set(), {}

    def visit(at, memory):
        if at in seen or not 0 <= at < len(nodes):
            raise ValueError("tree must have unique reachable nodes")
        seen.add(at)
        n = nodes[at]
        if n.player == 2:
            if n.children or n.actions or n.probabilities or abs(n.payoff) > 1000000:
                raise ValueError("terminal domain")
            return
        if n.player not in (-1, 0, 1) or not 1 <= len(n.children) <= 9 or len(n.actions) != len(n.children):
            raise ValueError("node domain")
        if len(set(n.actions)) != len(n.actions):
            raise ValueError("duplicate action")
        if n.player == -1:
            if len(n.probabilities) != len(n.children) or sum(n.probabilities) != 1 or min(n.probabilities) < 0:
                raise ValueError("chance distribution")
        else:
            if len(n.actions) > 2 or not n.info or n.probabilities:
                raise ValueError("player information set")
            description = (n.player, n.actions, memory[n.player])
            if n.info in infos and infos[n.info] != description:
                raise ValueError("inconsistent actions/player/perfect recall")
            infos[n.info] = description
        for action, child in zip(n.actions, n.children):
            following = list(memory)
            if n.player >= 0:
                following[n.player] += ((n.info, action),)
            visit(child, tuple(following))

    visit(0, ((), ()))
    if len(seen) != len(nodes) or len(infos) > 32:
        raise ValueError("unreachable node or information-set bound")
    return infos


def cfr(nodes, iterations):
    """Full-tree simultaneous regret matching; no sampling or runtime solve.

    Zinkevich et al.2007: regret uses OPPONENT x CHANCE reach; average
    strategy uses OWN reach, once per perfect-recall information set.
    """
    infos = validate(nodes)
    if not 1 <= iterations <= 100000:
        raise ValueError("iteration bound")
    regrets = {key: [0.0] * len(v[1]) for key, v in infos.items()}
    totals = {key: [0.0] * len(v[1]) for key, v in infos.items()}
    for _ in range(iterations):
        strategy = {}
        for key, row in regrets.items():
            positive = [max(0.0, x) for x in row]
            denominator = sum(positive)
            strategy[key] = [x / denominator for x in positive] if denominator else [1 / len(row)] * len(row)
        delta = {key: [0.0] * len(row) for key, row in regrets.items()}
        averaged = set()

        def walk(at, reach):
            n = nodes[at]
            if n.player == 2:
                return float(n.payoff)
            if n.player == -1:
                return sum(float(p) * walk(child, (reach[0], reach[1], reach[2] * float(p)))
                           for p, child in zip(n.probabilities, n.children))
            policy = strategy[n.info]
            if n.info not in averaged:
                averaged.add(n.info)
                for a, probability in enumerate(policy):
                    totals[n.info][a] += reach[n.player] * probability
            values = []
            for probability, child in zip(policy, n.children):
                following = list(reach)
                following[n.player] *= probability
                values.append(walk(child, tuple(following)))
            value = sum(p * v for p, v in zip(policy, values))
            counterfactual = reach[1 - n.player] * reach[2]
            for a, other in enumerate(values):
                delta[n.info][a] += counterfactual * (other - value) * (1 if n.player == 0 else -1)
            return value

        walk(0, (1.0, 1.0, 1.0))
        for key in regrets:
            for a in range(len(regrets[key])):
                regrets[key][a] += delta[key][a]
    average = {key: [x / sum(row) for x in row] if sum(row) else [1 / len(row)] * len(row)
               for key, row in totals.items()}
    return average, regrets, totals


PAYOFF = ((4, 16), (12, 3))
SIGNAL = ((6, 1, 3), (1, 6, 3))  # personal detection, score rise, unavailable.
AVAILABILITY = (1, 1, 2)  # public observed masks1,2,3.
SWITCH = 1


def actions(mask):
    return tuple(a for a in range(2) if mask & (1 << a))


def game(root_mask):
    if root_mask not in (1, 2, 3):
        raise ValueError("availability")
    nodes = []

    def add(node):
        nodes.append(node)
        return len(nodes) - 1

    root = add(None)
    opponents = []
    for opponent in range(2):
        first = add(None)
        opponents.append(first)
        chance_nodes = []
        for a0 in actions(root_mask):
            chance = add(None)
            chance_nodes.append(chance)
            histories, probabilities = [], []
            for signal in range(3):
                for mask in (1, 2, 3):
                    second = add(None)
                    histories.append(second)
                    probabilities.append(F(SIGNAL[opponent][signal] * AVAILABILITY[mask - 1], 40))
                    leaves = tuple(add(Node(2, payoff=F(PAYOFF[a0][opponent] + PAYOFF[a1][opponent] - SWITCH * (a0 != a1))))
                                   for a1 in actions(mask))
                    nodes[second] = Node(0, f"after:{a0}:{signal}:{mask}", actions(mask), leaves)
            nodes[chance] = Node(-1, actions=tuple(range(9)), children=tuple(histories), probabilities=tuple(probabilities))
        nodes[first] = Node(0, "first", actions(root_mask), tuple(chance_nodes))
    nodes[root] = Node(1, "opponent", (0, 1), tuple(opponents))
    return nodes


def campaigns(root_mask):
    return [(a0,) + continuation for a0 in actions(root_mask) for continuation in itertools.product(range(2), repeat=3)]


def matrix(root_mask):
    rows = []
    for row in campaigns(root_mask):
        value = []
        for opponent in range(2):
            total = F(PAYOFF[row[0]][opponent])
            for signal in range(3):
                for mask in (1, 2, 3):
                    a1 = row[signal + 1] if mask == 3 else actions(mask)[0]
                    total += F(SIGNAL[opponent][signal] * AVAILABILITY[mask - 1], 40) * (PAYOFF[a1][opponent] - SWITCH * (a1 != row[0]))
            value.append(total)
        rows.append(value)
    return rows


def normal_form(rows):
    """Exact <=16x2 maximin LP via all supports<=2 and dual intersections."""
    candidates = [(min(row), ((i, F(1)),)) for i, row in enumerate(rows)]
    for i, j in itertools.combinations(range(len(rows)), 2):
        difference = (rows[i][0] - rows[i][1]) - (rows[j][0] - rows[j][1])
        if not difference:
            continue
        p = (rows[j][1] - rows[j][0]) / difference
        if 0 < p < 1:
            candidates.append((min(p * rows[i][w] + (1 - p) * rows[j][w] for w in range(2)), ((i, p), (j, 1 - p))))
    value = max(v for v, _ in candidates)
    support = min(s for v, s in candidates if v == value)
    dual = [F(0), F(1)]
    for a, b in itertools.combinations(rows, 2):
        difference = (a[0] - a[1]) - (b[0] - b[1])
        if difference:
            p = (b[1] - a[1]) / difference
            if 0 <= p <= 1:
                dual.append(p)
    opponent_value, opponent = min((max(p * a + (1 - p) * b for a, b in rows), p) for p in dual)
    if value != opponent_value:
        raise AssertionError("normal-form strong duality")
    return value, support, opponent


def behavioral(root_mask, support):
    policy = {}
    pure = campaigns(root_mask)
    first = [sum(weight for i, weight in support if pure[i][0] == a) for a in actions(root_mask)]
    policy["first"] = first
    for a0 in actions(root_mask):
        reach = sum(weight for i, weight in support if pure[i][0] == a0)
        for signal in range(3):
            for mask in (1, 2, 3):
                policy[f"after:{a0}:{signal}:{mask}"] = [
                    sum(weight for i, weight in support if pure[i][0] == a0 and
                        (pure[i][signal + 1] if mask == 3 else actions(mask)[0]) == a) / reach
                    if reach else F(1, len(actions(mask))) for a in actions(mask)]
    return policy


def columns(root_mask, policy):
    result = []
    for opponent in range(2):
        total = 0.0
        for a0, first in zip(actions(root_mask), policy["first"]):
            value = float(PAYOFF[a0][opponent])
            for signal in range(3):
                for mask in (1, 2, 3):
                    value += SIGNAL[opponent][signal] * AVAILABILITY[mask - 1] / 40 * sum(
                        float(p) * (PAYOFF[a1][opponent] - SWITCH * (a1 != a0))
                        for a1, p in zip(actions(mask), policy[f"after:{a0}:{signal}:{mask}"]))
            total += float(first) * value
        result.append(total)
    return result


def ticket_count(policy, info, mask):
    if mask != 3:
        return 64 if mask == 1 else 0
    return max(0, min(64, int(math.floor(float(policy[info][0]) * 64 + 0.5))))


def export(iterations=12000):
    data, reports = [], []
    for mask in (1, 2, 3):
        nodes = game(mask)
        policy, regrets, _ = cfr(nodes, iterations)
        rows = matrix(mask)
        value, support, opponent = normal_form(rows)
        control = behavioral(mask, support)
        average_columns = columns(mask, policy)
        opponent_average = policy["opponent"][0]
        best_response = max(float(opponent_average * row[0] + (1 - opponent_average) * row[1]) for row in rows)
        gap = best_response - min(average_columns)
        reports.append({"rootMask": mask, "nodes": len(nodes), "infosets": len(validate(nodes)), "iterations": iterations,
                        "normalFormValue": str(value), "normalFormSupport": [[i, str(p)] for i, p in support],
                        "normalFormOpponent0": str(opponent), "CFRColumns": average_columns, "CFRExploitability": gap,
                        "positiveRegretSumOverIterations": sum(max(0.0, max(v)) for v in regrets.values()) / iterations,
                        "perfectRecallNormalFormEquivalence": True})
        model = []
        for arm in (policy, control):
            table = {"first": ticket_count(arm, "first", mask), "after": []}
            for a0 in range(2):
                table["after"].append([[ticket_count(arm, f"after:{a0}:{s}:{m}", m) if a0 in actions(mask) else
                                         (64 if m == 1 else 0 if m == 2 else 32) for m in (1, 2, 3)] for s in range(3)])
            model.append(table)
        for arm_index, table in enumerate(model):
            quantized = {"first": [F(table["first"], 64), F(64 - table["first"], 64)] if mask == 3 else [F(1)]}
            for a0 in actions(mask):
                for signal in range(3):
                    for availability in (1, 2, 3):
                        count = table["after"][a0][signal][availability - 1]
                        quantized[f"after:{a0}:{signal}:{availability}"] = [F(count, 64), F(64 - count, 64)] if availability == 3 else [F(1)]
            reports[-1]["CFRQuantizedColumns" if not arm_index else "normalFormQuantizedColumns"] = columns(mask, quantized)
        reports[-1]["CFRQuantizedExploitabilityAgainstAverageOpponent"] = best_response - min(reports[-1]["CFRQuantizedColumns"])
        reports[-1]["normalFormQuantizedValueLoss"] = float(value) - min(reports[-1]["normalFormQuantizedColumns"])
        data.append(model)
    return data, reports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--iterations", type=int, default=12000)
    args = parser.parse_args()
    data, reports = export(args.iterations)
    if any(r["CFRExploitability"] > 0.25 for r in reports):
        raise AssertionError("declared offline residual bound")
    model = {"payoff": PAYOFF, "signalCounts": SIGNAL, "availabilityCounts": AVAILABILITY, "switch": SWITCH,
             "unit": "assumed gem-equivalent; no empirical/native payoff", "tables": data}
    digest = hashlib.sha256(json.dumps(model, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    tables = []
    for arm in range(2):
        values = []
        for mask in range(3):
            table = data[mask][arm]
            values.append([table["first"]] + [table["after"][a][s][m] for a in range(2) for s in range(3) for m in range(3)])
        tables.append(values)
    header = '#pragma once\n#include <array>\n#include <cstdint>\nnamespace plan3_sequence {\n'
    header += f'inline constexpr const char* policy_model_sha256="{digest}";\n'
    header += 'inline constexpr std::uint8_t policy_tickets[2][3][19]=' + json.dumps(tables).replace('[', '{').replace(']', '}') + ';\n}\n'
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(header, encoding="utf-8")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({"status": "pass", "model": model, "sourceSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                                     "models": reports, "ticketDenominator": 64, "nativeCalls": 0,
                                     "limits": "Analytical perfect-recall abstraction; finite prescribed runtime tickets are not independent draws or a Battlecode equilibrium."}, indent=2) + '\n')
    print(json.dumps({"models": 3, "iterations": args.iterations, "maximumExploitability": max(r["CFRExploitability"] for r in reports), "nativeCalls": 0}))


if __name__ == "__main__":
    main()
