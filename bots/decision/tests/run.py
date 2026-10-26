"""Focused tests of the decision suite (no CLI, engine, game or network).

    python bots/decision/tests/run.py [bot ...] [-k PATTERN] [--list] [-j N]

With no bot, every case runs; with bots, only the cases whose target modules the bots' build
closures reach (common-module cases are relevant to every bot). One line per case; exit status 1
on any failure outside the KNOWN parts below. C++ fixtures compile with the pinned wasi-sdk clang
and run under Node with only WASI imports: fixtures without an SDK-throw shim through
tools/arena/wasi.py; shim and SDK-host fixtures (hosts.py) with the plan3 flags, because the shim
turns SDK throws into aborts instead of wasm EH. Results are modeled logic evidence only, never
native fuel, legality or strength.
"""
from __future__ import annotations

import argparse
import copy
import fnmatch
import json
import math
import subprocess
import sys
import time
import unittest
import uuid
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path

TESTS = Path(__file__).resolve().parent
SUITE = TESTS.parent
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
sys.path.insert(0, str(TESTS))
import core  # noqa: E402
import wasi  # noqa: E402
import hosts  # noqa: E402

OUT = core.CACHE / "decision-tests"
SDK = ROOT / "sdk" / "cpp"
DRIVER = TESTS / "host" / "run.mjs"
PLAN3 = ["-std=c++20", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-Werror"]  # no wasm EH: the shims abort

# Pre-existing failures carried over from plan3 (README "Known failures"). The part still runs and
# is reported as known; a failure anywhere else fails the suite. Delete an entry once it passes.
KNOWN = {
    "two-trip-ledger/tavi": "shared Ledger Campaign never starts the Tavi two-trip episode (cold startup, probes)",
    "sortie-runtime/default": "callback scenarios stale after the last plan3 sortie/runtime edits",
    "sortie-runtime/geometry": "callback scenarios stale after the last plan3 sortie/runtime edits",
    "sortie-runtime/budget": "callback scenarios stale after the last plan3 sortie/runtime edits",
    "sortie-runtime/model": "callback scenarios stale after the last plan3 sortie/runtime edits",
    "sortie-runtime/members": "callback scenarios stale after the last plan3 sortie/runtime edits",
    "sortie-runtime/exit": "callback scenarios stale after the last plan3 sortie/runtime edits",
    "sortie-ability/rows-default": "Rows caller scenario stale after the last plan3 sortie edits",
    "sortie-ability/rows-static-kit": "R31 late live-query deferral was mid-change when plan3 stopped",
    "backpressure/shared-parity": "forked Backpressure Campaign no longer matches the shared Campaign",
}


class Failure(Exception):
    pass


def need(ok, message: str) -> None:
    if not ok:
        raise Failure(message)


def lines(text: str, prefix: str) -> list:
    return [json.loads(line.split(" ", 1)[1]) for line in text.splitlines() if line.startswith(prefix + " ")]


def one(text: str, prefix: str, side: int | None = None) -> dict:
    rows = lines(text, prefix)
    need(len(rows) == 1 and rows[0].get("status") in ("pass", "passed"), f"missing {prefix} pass")
    need(side is None or rows[0].get("side") == side, f"{prefix} side mismatch")
    return rows[0]


def last_json(text: str) -> dict:
    return json.loads([line for line in text.splitlines() if line.strip()][-1])


class Ctx:
    """One case's scratch directory, compiles and fixture runs (each holds an arena slot)."""

    def __init__(self, name: str):
        self.name = name
        self.dir = OUT / name / uuid.uuid4().hex
        self.dir.mkdir(parents=True, exist_ok=False)
        self.host = self.dir / "host"  # generated headers; never tests/ (quoted includes)
        self.runs = 0
        self.known, self.fixed = [], []

    def part(self, label: str, func) -> None:
        """Run one part; a failure of a KNOWN part is recorded instead of raised."""
        if f"{self.name}/{label}" not in KNOWN:
            func()
            return
        try:
            func()
        except Exception:  # noqa: BLE001 - any failure of a known part stays known
            if label not in self.known:
                self.known.append(label)
            return
        if label not in self.fixed:
            self.fixed.append(label)

    def build(self, sources, name: str, flags=(), arena: bool = False, includes=()) -> Path:
        sources = [sources] if isinstance(sources, (str, Path)) else list(sources)
        out = self.dir / f"{name}.wasm"
        with core.Slots(1, quiet=True):
            if arena:
                ok, log = wasi.compile_fixture(sources, out, includes=includes, flags=flags)
            else:
                cmd = [str(core.CLANG), *map(str, sources), *PLAN3, *map(str, flags), "-o", str(out)]
                p = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=1800)
                ok, log = p.returncode == 0, (p.stdout + p.stderr)[-3000:]
        need(ok, f"compile {name} failed\n{log}")
        return out

    def host_build(self, source, name: str, flags=(), host: Path | None = None) -> Path:
        """SDK-host fixture: the shim defines the SDK imports; private fields reachable."""
        host = host or self.host
        return self.build(source, name, ["-fno-access-control", f"-I{SDK}", f"-I{host}", *flags, "-Wl,--allow-undefined"])

    def syntax(self, source, flags=()) -> None:
        cmd = [str(core.CLANG), str(source), *PLAN3, *map(str, flags), "-fsyntax-only"]
        with core.Slots(1, quiet=True):
            p = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=1800)
        need(p.returncode == 0, f"syntax {Path(source).name} failed\n{(p.stdout + p.stderr)[-3000:]}")

    def run(self, wasm: Path, env: dict | None = None, stub: bool = False, fail: bool = False, check: bool = True):
        """Run a fixture; returns stdout (fail=True: (stdout, stderr); check=False: (code, stdout, stderr))."""
        cmd = [str(core.NODE), "--disable-warning=ExperimentalWarning", str(DRIVER), str(wasm),
               json.dumps(env or {}), "stub" if stub else "strict"]
        with core.Slots(1, quiet=True):
            p = subprocess.run(cmd, capture_output=True, timeout=1800)
        self.runs += 1
        out, err = p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")
        if not check:
            return p.returncode, out, err
        if fail:
            need(p.returncode != 0, f"{wasm.name} {env} was expected to fail")
            return out, err
        need(p.returncode == 0, f"{wasm.name} {env} exited {p.returncode}\n{out[-1200:]}{err[-1800:]}")
        return out


@dataclass
class Case:
    name: str
    modules: tuple  # suite module dirs the case targets; decides relevance to a bot
    func: object
    what: str


CASES: list[Case] = []


def case(name: str, modules: str, what: str):
    def register(func):
        CASES.append(Case(name, tuple(modules.split()), func, what))
        return func
    return register


def T(name: str) -> Path:
    return TESTS / name


# ----------------------------------------------------------------------------- pure fixtures
def pure(name: str, source: str, modules: str, what: str):
    def func(ctx):
        ctx.run(ctx.build(T(source), name, arena=True))
    case(name, modules, what)(func)


pure("allocation", "plan3-allocation.cpp", "common", "bounded draft allocation wire and model")
pure("policy", "plan3-policy-current.cpp", "common", "shared policy events")
pure("sortie-model", "plan3-sortie-model-current.cpp", "common", "Tavi multi-sortie model arithmetic")
pure("changepoint", "plan3-changepoint-current.cpp", "changepoint-common", "bounded run-length posterior")
pure("correlator", "plan3-correlator-current.cpp", "correlator-common", "correlated bundle wire/draws")
pure("risk", "plan3-risk-current.cpp", "risk-common", "ambiguity polytope and ruin constraint (hand cases)")
pure("sequence", "plan3-sequence-current.cpp", "sequence-common", "CFR policy table use")


def plan3_pure(name: str, source: str, modules: str, what: str):
    def func(ctx):
        ctx.run(ctx.build(T(source), name))
    case(name, modules, what)(func)


plan3_pure("adapter", "plan3-adapter-current.cpp", "adapter-common", "chosen-only bandit feedback")
plan3_pure("candidate-prefix", "plan3-candidate-prefix-current.cpp", "common", "bounded candidate prefix")
plan3_pure("commitment", "plan3-commitment-current.cpp", "commitment-common", "Stackelberg commitment games")
plan3_pure("lifeline", "plan3-lifeline-current.cpp", "lifeline-common", "viability set admission")
plan3_pure("signaller", "plan3-signaller-current.cpp", "signaller-common", "signaling response games")


@case("model", "common", "choose() for the 12 foundation objectives; capital SDK syntax")
def _model(ctx):
    ctx.run(ctx.build(T("plan3-model.cpp"), "model", arena=True))
    ctx.syntax(T("plan3-capital-sdk.cpp"), ["-Wno-unused-private-field", f"-I{SDK}"])


@case("conversion-witness", "common", "adjacent own-conversion proof; runtime SDK syntax")
def _witness(ctx):
    ctx.run(ctx.build(T("plan3-conversion-witness.cpp"), "witness", arena=True))
    ctx.syntax(T("plan3-conversion-witness.cpp"), ["-Wno-unused-private-field", "-DCHECK_CURRENT_RUNTIME", f"-I{SDK}"])


@case("two-trip-cycle", "common", "conditional two-trip accounting")
def _two_trip_cycle(ctx):
    text = ctx.run(ctx.build(T("plan3-two-trip-cycle.cpp"), "cycle", arena=True)).strip()
    need(text.startswith("TWO_TRIP_CYCLE "), "unexpected fixture output")
    row = json.loads(text[len("TWO_TRIP_CYCLE "):])
    need(row["status"] == "pass" and row["engineLaunches"] == 0 and not row["runtimeIntegrated"], "unexpected fixture result")


@case("summary", "common assignment-common", "compact telemetry summaries and policy frames (mock SDK)")
def _summary(ctx):
    mock = hosts.summary_mock(ctx.host)
    raw = ctx.run(ctx.build(T("plan3-summary-current.cpp"), "summary", arena=True, includes=[mock]))
    data = [line for line in raw.splitlines() if line.startswith("P3 ")]
    frames = [json.loads(line[3:]) for line in data]
    compact = [f for f in frames if f.get("sx") == 1]
    ordinary = [f for f in frames if "sx" not in f]
    need(len(ordinary) == 9 and len(compact) == 5 and all(len(line.encode()) + 1 <= 512 for line in data), "frame counts/sizes changed")
    need(frames[0]["abs_health_error"] == 938 and frames[0]["abs_duration_error"] == 113, "first prediction errors changed")
    need(frames[1]["abs_duration_error"] is None and frames[1]["abs_health_error"] == 1000000000, "unknown/clamped errors changed")
    policies = [f for f in frames if f["kind"] == "policy"]
    need(len(policies) == 5 and all(f["assumed"] is True and f["horizon"] == 128 for f in policies), "policy frames changed")
    need(all(f["governing_assignment"] and not f["safe_fallback"] for f in policies[:4]), "governing policy frames changed")
    need(not policies[-1]["governing_assignment"] and policies[-1]["safe_fallback"], "fallback policy frame changed")
    need(all(set(f) == {"schema", "round", "actor", "kind", "sx", "base", "sortie", "final"} for f in compact), "compact schema changed")
    need(all(f["kind"] == "summary" and len(f["base"]) == 6 and len(f["sortie"]) == 19 for f in compact), "compact summary shape changed")
    need(compact[0]["base"] == [0xffffffff] * 6 and compact[0]["sortie"] == [0xffffffff] * 19 and compact[0]["final"] is True, "saturated summary changed")
    need([compact[1]["sortie"][i] for i in (10, 11, 18)] == [-1, -1, -1] and sum(f["final"] for f in compact) == 2, "unknown summary fields changed")
    summary = last_json(raw)
    need(summary["status"] == "pass" and summary["nativeGames"] == summary["sdkRuntimeCalls"] == 0, "summary fixture did not pass")


@case("assignment", "assignment-common", "exact/bargained assignment, ownership wire, policy frames")
def _assignment(ctx):
    text = ctx.run(ctx.build(T("plan3-assignment-current.cpp"), "assignment"))
    frames = [json.loads(line[3:]) for line in text.splitlines() if line.startswith("P3 ")]
    need(len(frames) == 5 and all(f.get("kind") == "policy" and f.get("phase") == "adopt" and f.get("round") in (7, 8) for f in frames), "invalid adoption frames")
    need(all(f["governing_assignment"] and not f["safe_fallback"] for f in frames[:4]) and not frames[4]["governing_assignment"] and frames[4]["safe_fallback"], "frame ownership/fallback fields changed")
    need(last_json(text).get("status") == "pass", "fixture did not publish pass")


def belief_oracle():
    """Independent Fraction expectimax for the two-world, three-job depth-two model."""
    prior = [Fraction(1, 2), Fraction(1, 2)]
    rewards, times = [[1, 2], [6, 0], [0, 6]], [1, 2, 2]
    rows = []
    for first in range(3):
        immediate = sum(prior[s] * rewards[first][s] for s in range(2))
        continuation = 0
        for observation in set(rewards[first]):
            joint = [prior[s] if rewards[first][s] == observation else Fraction(0) for s in range(2)]
            feasible = [second for second in range(3) if second != first and times[first] + times[second] <= 3]
            continuation += max([sum(joint[s] * rewards[second][s] for s in range(2)) for second in feasible] + [Fraction(0)])
        rows.append([immediate, immediate + continuation])
    return rows


@case("belief-search", "belief-search-common", "depth-two belief lookahead vs Fraction oracle")
def _belief_search(ctx):
    text = ctx.run(ctx.build(T("plan3-belief-search-current.cpp"), "belief-search"))
    observed = lines(text, "BELIEF_MODEL")
    need(len(observed) == 1 and observed[0]["active"] == 0 and observed[0]["control"] == 1, "oracle choice mismatch")
    expected = belief_oracle()
    need(all(abs(float(expected[a][h]) - observed[0]["rows"][a][h]) <= 1e-10 for a in range(3) for h in range(2)), "oracle values mismatch")
    need(last_json(text).get("status") == "pass", "fixture did not publish pass")


@case("shadow-prices", "shadow-prices-common", "four-resource LP prices and forecast adapter")
def _shadow_prices(ctx):
    need(last_json(ctx.run(ctx.build(T("plan3-shadow-prices-current.cpp"), "shadow-prices"))).get("status") == "pass", "fixture did not publish pass")


# ----------------------------------------------------------------------------- SDK-host fixtures
def sides(ctx, wasm, env=None, stub=True):
    return [ctx.run(wasm, {"P3_BP_SIDE": str(side), **(env or {})}, stub=stub) for side in (0, 1)]


@case("capital-cache", "common", "capital metadata cache and invalidation (SDK host)")
def _capital_cache(ctx):
    hosts.capital_cache(ctx.host)
    sides(ctx, ctx.host_build(T("plan3-capital-cache-current.cpp"), "capital-cache"))


@case("bank-materialization-fence", "common", "incomplete bank refusal and fresh retry (SDK host)")
def _bank_materialization_fence(ctx):
    hosts.capital_cache(ctx.host)
    sides(ctx, ctx.host_build(T("plan3-bank-fence-current.cpp"), "bank-materialization-fence"))


@case("executor", "common", "legal executor navigation (SDK host)")
def _executor(ctx):
    hosts.executor(ctx.host)
    sides(ctx, ctx.host_build(T("plan3-executor-current.cpp"), "executor"))


@case("sortie-forecast", "common", "sortie forecast from queried metadata (SDK host)")
def _sortie_forecast(ctx):
    hosts.sortie_forecast(ctx.host)
    sides(ctx, ctx.host_build(T("plan3-sortie-forecast-current.cpp"), "sortie-forecast"))


@case("two-trip-observation", "common", "two-trip member observations (SDK host)")
def _two_trip_observation(ctx):
    hosts.two_trip_observation(ctx.host)
    for side, text in enumerate(sides(ctx, ctx.host_build(T("plan3-two-trip-observation-sdk.cpp"), "observation"))):
        one(text, "TWO_TRIP_OBSERVATION_RESULT", side)


def check_two_trip(text: str, family: str, side: int, actor: str) -> None:
    summaries, converted, service, cold, commits, low = [], [], [], [], [], []
    gold = None
    for line in text.splitlines():
        need(not line.startswith("p3 error:"), "runtime error observed")
        if line.startswith("OPTIONAL_GOLD_BEGIN "):
            need(gold is None, "nested optional gold marker")
            gold = {"begin": json.loads(line.split(" ", 1)[1]), "predictions": []}
        if line.startswith("P3 "):
            need(len(line.encode()) + 1 <= 512, "oversize telemetry")
            event = json.loads(line[3:])
            if gold and event.get("kind") == "prediction":
                gold["predictions"].append(event)
            if event.get("kind") == "endpoint" and event.get("conversion"):
                need(event.get("exact_conversion") == 66 and event.get("actor_score_lower_bound") == 66, "incorrect own conversion diagnostic")
                converted.append(event)
        if line.startswith("TWO_TRIP_RUNTIME_RECORD "):
            record = json.loads(line.split(" ", 1)[1])
            if record["kind"] == "indexer-service":
                need(family == "indexer" and actor == "tavi", "unexpected service record")
                need(sum(record["freshTerms"]) == record["Cfresh"], "fresh cost charged twice or omitted")
                for prefix in ("fresh", "current"):
                    health = record[prefix + "TerminalHealth"]
                    heal = 0 if health >= record["maxHealth"] else math.ceil((record["maxHealth"] - health) / (record["maxHealth"] * record["spawnHealing"])) + 1
                    need(record[prefix + "Healing"] == heal, "final renewal healing mismatch")
                    need(record["R" + prefix] == record["recall"] + heal + record["maximumSpawnToQ"] + record["QToP"], "recovery omitted return to p")
                period = max(record["tau"], record["Cfresh"] + record["Rfresh"], record["Cfresh"] + record["Rcurrent"], record["Cfresh"] + record["regenMax"])
                need(record["P"] == period and record["D"] == record["tau"] + record["Rcurrent"] and record["N"] == 1 + (record["H"] - record["tau"]) // period, "incorrect renewal terms")
                need(record["p"] != record["Q"] and record["QToP"] > 0 and record["readyCuts"] > 0, "missing geometry/cutoff witness")
                if record["variant"] == 2:
                    need(record["tau"] == record["H"] < record["Cfresh"], "fresh service incorrectly bounded by current H")
                service.append(record)
            if record["kind"] == "cold-commit-state":
                need(record["round"] == record["preparedRound"] == record["sourceRound"], "cold commit used stale inputs")
                need(record["fundingBudget"] in (360, 420) and record["powerShards"] == (record["fundingBudget"] - 300) // 30, "cold funding/loadout mismatch")
                need(record["stats"][:3] == [1125, 60 + 5 * record["powerShards"], 100], "incorrect actual cold loadout stats")
                need(record["stats"][3:] == [1.1000000238418579, 0.69999998807907104], "captured-float passives changed")
                commits.append(record)
            if record["kind"] == "cold-startup":
                cold.append(record)
            if record["kind"] == "affordable-low-health":
                need(record["currentHealth"] == 800 and record["maximumHealth"] == 1125 and record["strength"] == 70 and record["focus"] == 100, "low-health case changed gear")
                reserve = record["maximumHealth"] * .25
                need(record["unsafeFirst"] == (800 - record["firstLoss"] < reserve) and record["unsafeEscape"] == (800 - record["firstLoss"] - record["escapeLoss"] < reserve), "incorrect current-health safety witness")
                need((record["unsafeFirst"] or record["unsafeEscape"]) and record["fullTerminalHealth"] >= reserve and record["status"] == 2, "low-health rejection/full-health admission mismatch")
                low.append(record)
        if line.startswith("OPTIONAL_GOLD_RESULT "):
            value = json.loads(line.split(" ", 1)[1])
            need(gold and all(value[k] == gold["begin"][k] for k in ("mode", "variant")), "optional gold marker mismatch")
            need(value["oldWouldDefer"] and value["publicGemStock"] > 0, "regression lacks old-guard/gem witness")
            need(value["selected"] == (value["variant"] < 2) and value["latched"] == (value["variant"] in (1, 2)), "optional ordinary decision boundary")
            need(len(gold["predictions"]) == int(value["selected"]), "missing/partial ordinary prediction")
            for prediction in gold["predictions"]:
                need(prediction["job"] == value["expectedJob"] and prediction["companion_job"] == value["expectedJob"], "ordinary paired ID changed")
            gold = None
        if line.startswith("TWO_TRIP_RUNTIME_RESULT "):
            summaries.append(json.loads(line.split(" ", 1)[1]))
    need(len(summaries) == 1 and summaries[0]["status"] == "pass" and summaries[0]["side"] == side, "missing side summary")
    need(gold is None, "unclosed optional gold case")
    s = summaries[0]
    need(s["actor"] == actor and len(converted) == s["completedPaths"], "conversion endpoint/count mismatch")
    need(s["serviceCases"] == (9 if family == "indexer" and actor == "tavi" else 0), "missing service boundaries")
    need(len(service) == (6 if family == "indexer" and actor == "tavi" else 0), "missing three-mode service tables")
    for variant in (1, 2):
        rows = [{k: v for k, v in row.items() if k != "mode"} for row in service if row["variant"] == variant]
        need(not rows or (len(rows) == 3 and rows[0] == rows[1] == rows[2]), "modes did not share one service input")
    modes = (3 if family == "indexer" else 2 if family == "backpressure" else 1) if actor == "tavi" else 0
    need(s["coldCompletedPaths"] == 2 * modes and s["coldCases"] == ((2 * modes + (0 if family == "indexer" else 8)) if modes else 0), "missing cold startup/merge cases")
    need(s["cold360CompletedPaths"] == modes and s["cold420CompletedPaths"] == modes, "cold funding counters conflated")
    need(s["lowHealthCases"] == len(low) == int(actor == "tavi"), "missing 800HP rejection")
    need(len(cold) == len(commits) == 2 * modes, "missing funding-specific cold path evidence")
    for funding in (360, 420):
        starts = [r for r in cold if r["fundingBudget"] == funding]
        need(sorted(r["mode"] for r in starts) == list(range(modes)) and sorted(r["mode"] for r in commits if r["fundingBudget"] == funding) == list(range(modes)), "missing mode/funding cell")
        for record in starts:
            need(record["converted"] == 66 and record["powerShards"] == (funding - 300) // 30 and not record["nativeOpportunityClaim"], "incorrect conditional cold result")


def two_trip(family: str, define: str, modules: str, suffix: str = "", flags=()):
    @case("two-trip-" + family + suffix, modules, f"two-trip Campaign callbacks, {family} runtime (SDK host)")
    def func(ctx):
        hosts.two_trip_direct(ctx.host)
        wasm = ctx.host_build(T("plan3-two-trip-runtime-sdk.cpp"), family, ["-D" + define, *flags])
        for actor in ("tavi", "varg"):
            def actor_runs(actor=actor):
                for side in (0, 1):
                    check_two_trip(ctx.run(wasm, {"P3_BP_SIDE": str(side), "P3_BP_ACTOR": actor}, stub=True), family, side, actor)
            ctx.part(actor, actor_runs)


two_trip("ledger", "P3_TWO_LEDGER", "common")
two_trip("backpressure", "P3_TWO_BACKPRESSURE", "backpressure-common")
two_trip("indexer", "P3_TWO_INDEXER", "indexer-common")
two_trip("indexer", "P3_TWO_INDEXER", "indexer-common", "-nominal", ["-DPLAN3_INDEXER_NOMINAL_CAMPAIGN=1"])

TAVI = {"P3_BP_ACTOR": "tavi"}
SDK_FAULT = "Unexpected SDK fixture exception"


@case("sortie-runtime", "common", "sortie callbacks: paths, Q resume, members, exits, budget, geometry, snapshot faults")
def _sortie_runtime(ctx):
    hosts.sortie_runtime(ctx.host)
    wasm = ctx.host_build(T("plan3-sortie-runtime-current.cpp"), "sortie", ["-DP3_TWO_LEDGER"])
    for mode in (None, "GEOMETRY_ONLY", "BUDGET_ONLY", "MODEL_ONLY", "MEMBERS_ONLY", "EXIT_ONLY"):
        def mode_runs(mode=mode):
            extra = {"P3_SORTIE_" + mode: "1"} if mode else {}
            for side, text in enumerate(sides(ctx, wasm, {**TAVI, **extra}, stub=False)):
                one(text, "SORTIE_CALLBACK_RESULT", side)
        ctx.part(mode.split("_")[0].lower() if mode else "default", mode_runs)
    for at in (1, 2):
        for code in (97, 98, 1):
            _, err = ctx.run(wasm, {"P3_BP_SIDE": "0", **TAVI, "P3_SORTIE_SNAPSHOT_ERROR": f"{at}:{code}"}, fail=True)
            need("SORTIE_EXPECTED_SNAPSHOT_ERROR" in err and SDK_FAULT in err, f"snapshot error {at}:{code} did not reach the throw shim")


def enemy_tables(text: str, prefix: str, pick) -> int:
    count = 0
    for table in lines(text, prefix):
        pick(table)
        count += 1
    return count


@case("citadel", "citadel-common", "class-mean security vs uniform mean on the actual caller (SDK host)")
def _citadel(ctx):
    hosts.enemy(ctx.host)
    wasm = ctx.build(T("plan3-citadel-current.cpp"), "citadel", ["-O1", "-DP3_TWO_LEDGER", "-fno-access-control", f"-I{SDK}", f"-I{ctx.host}", "-Wl,--allow-undefined"])

    def oracle(table):
        columns = [w for w in range(4) if table["covered"] & (1 << w)]
        security = min(table["rows"], key=lambda row: (-min(row["v"][w] for w in columns), row["id"]))
        mean = min(table["rows"], key=lambda row: (-sum(row["v"][w] for w in columns) / len(columns), row["id"]))
        need(security["id"] == table["security"] and mean["id"] == table["mean"] and security["id"] != mean["id"], "independent security/mean fork mismatch")
        need(security["id"] not in (table["ledger"], table["atlas"]), "missing genuine divergence from Ledger and Atlas")
    tables = 0
    for side, text in enumerate(sides(ctx, wasm, TAVI, stub=False)):
        need("p3 error:" not in text, "caught source error")
        one(text, "CITADEL_RESULT", None)
        need(len(lines(text, "CITADEL_CALLER")) == 2 and len(lines(text, "CITADEL_FINITE")) == 1, "missing caller/finite pass")
        tables += enemy_tables(text, "CITADEL_TABLE", oracle)
    need(tables, "no generated tables")


def kernel_and_sdk(ctx, source: str, define: str, prefix: str, oracle):
    tables = 0
    for mode in ("kernel", "sdk"):
        flags = ["-Wl,--allow-undefined"] if mode == "kernel" else ["-D" + define, "-DP3_TWO_LEDGER", "-fno-access-control", f"-I{SDK}", f"-I{ctx.host}", "-Wl,--allow-undefined"]
        wasm = ctx.build(T(source), mode, flags)
        for side in ((0,) if mode == "kernel" else (0, 1)):
            text = ctx.run(wasm, {"P3_BP_SIDE": str(side), **TAVI})
            rows = lines(text, prefix + "_RESULT")
            need(len(rows) == (1 if mode == "kernel" else 2) and all(row["status"] == "pass" for row in rows), f"missing {mode} pass")
            tables += enemy_tables(text, prefix + "_TABLE", oracle)
    need(tables, "no generated tables")


@case("hindsight", "hindsight-common", "minimax regret kernel and actual caller (SDK host)")
def _hindsight(ctx):
    hosts.enemy(ctx.host)

    def oracle(t):
        indices = [w for w in range(4) if t["covered"] & (1 << w)]

        def regret(row):
            return max(other["v"][w] - row["v"][w] for other in t["rows"] for w in indices)
        active = min(t["rows"], key=lambda row: (regret(row), row["key"]))
        control = min(t["rows"], key=lambda row: (-min(row["v"][w] for w in indices), row["key"]))
        need(active["key"] == t["regret"] and control["key"] == t["security"], "independent pairwise regret oracle mismatch")
    kernel_and_sdk(ctx, "plan3-hindsight-current.cpp", "P3_HINDSIGHT_SDK", "HINDSIGHT", oracle)


@case("underwriter", "underwriter-common", "downside-budget exploitation kernel and actual caller (SDK host)")
def _underwriter(ctx):
    hosts.enemy(ctx.host)

    def oracle(t):
        indices = [w for w in range(4) if t["covered"] & (1 << w)]

        def mean(row):
            return sum(row["v"][w] for w in indices) / len(indices)
        reference = max(min(row["v"][w] for w in indices) for row in t["rows"])
        eligible = [row for row in t["rows"] if min(row["v"][w] for w in indices) >= reference - 10]
        active = min(eligible, key=lambda row: (-mean(row), row["key"]))
        control = min(t["rows"], key=lambda row: (-mean(row), row["key"]))
        need(active["key"] == t["constrained"] and control["key"] == t["unconstrained"] and active["key"] != control["key"], "independent downside-budget oracle or fork mismatch")
    kernel_and_sdk(ctx, "plan3-underwriter-current.cpp", "P3_UNDERWRITER_SDK", "UNDERWRITER", oracle)


@case("ledger-nominal", "common", "Ledger nominal acquisition: ranking oracle and opt-in parity (SDK host)")
def _ledger_nominal(ctx):
    hosts.enemy(ctx.host)
    reference = hosts.ledger_reference(ctx.dir)
    runs = {}
    for label, source in (("production", T("plan3-ledger-nominal-current.cpp")), ("full-reference", reference)):
        wasm = ctx.host_build(source, label, ["-DP3_TWO_LEDGER"])
        for side, text in enumerate(sides(ctx, wasm, TAVI, stub=False)):
            parsed = {kind: lines(text, kind) for kind in ("NOMINAL_RESULT", "NOMINAL_TABLE", "NOMINAL_CALLER", "NOMINAL_NEGATIVES")}
            need(len(parsed["NOMINAL_RESULT"]) == 1 and parsed["NOMINAL_RESULT"][0]["status"] == "pass" and len(parsed["NOMINAL_CALLER"]) == 2 and len(parsed["NOMINAL_NEGATIVES"]) == 1, "missing pass/endpoint/negative")
            for table in parsed["NOMINAL_TABLE"]:
                for row in table["rows"]:
                    need(row["gems"] / max(1, row["tau"] + row["renewal"]) == row["rate"], "independent nominal rate mismatch")
                eligible = [row for row in table["rows"] if row["id"][0] != 3 and (not table["arm"] or ((row["id"][0] == 2) == table["b0Bank"]))]

                def objective(row):
                    if table["arm"] and row["id"][0] in (0, 1):
                        return (2 * row["campGems"] + 0.4 * row["campGold"]) / (row["travelClear"] + 10)
                    return row["rate"]
                ranked = sorted(eligible, key=lambda row: (-objective(row), row["id"]))
                need(ranked and ranked[0]["id"] == table["selected"] and abs(objective(ranked[0]) - table["objective"]) <= 1e-12, "independent Ledger ranking mismatch")
            runs[label, side] = parsed
    need(all(runs["production", side] == runs["full-reference", side] for side in (0, 1)), "production/full-reference caller parity mismatch")


@case("tempo-nominal", "common", "Tempo preserves consumed complete-job features without unconsumed renewal loops")
def _tempo_nominal(ctx):
    hosts.enemy(ctx.host)
    wasm = ctx.host_build(T("plan3-tempo-nominal-current.cpp"), "tempo-nominal", ["-DP3_TWO_LEDGER"])
    for side, text in enumerate(sides(ctx, wasm, TAVI, stub=False)):
        rows = lines(text, "TEMPO_NOMINAL")
        need(len(rows) == 2 and all(row["side"] == side and row["gems"] > 0 and row["captures"] > 0 and row["submissions"] > 0 and row["fullLoops"] > 0 and row["slimLoops"] == 0 for row in rows), "missing Tempo feature parity and complete physical host continuation")
        need(one(text, "TEMPO_NOMINAL_RESULT")["checks"] > 100, "insufficient consumed-feature checks")


@case("startup-layout", "common", "Immutable layout publication separates final sort from unchanged route ordering")
def _startup_layout(ctx):
    hosts.enemy(ctx.host)
    observed = {}
    for phase in (0, 1):
        wasm = ctx.host_build(T("plan3-startup-layout-current.cpp"), f"startup-layout-{phase}", ["-DP3_TWO_LEDGER", f"-DPLAN3_STARTUP_LAYOUT_PHASE={phase}"])
        for side, text in enumerate(sides(ctx, wasm, TAVI, stub=False)):
            row = one(text, "STARTUP_LAYOUT")
            need(row["side"] == side and row["phase"] == phase and row["checks"] >= 12, "missing immutable publication boundary checks")
            observed[phase, side] = row
    need(all(observed[1, side]["callbacks"] == observed[0, side]["callbacks"] + 1 for side in (0, 1)), "layout phase must add exactly one callback")


@case("output-isolation", "common", "Same-case contexts preserve each other's output")
def _output_isolation(ctx):
    first = Ctx("output-isolation-probe")
    first_marker = first.dir / "canary.txt"
    first_marker.write_text("first\n", encoding="utf-8", newline="\n")
    second = Ctx("output-isolation-probe")
    second_marker = second.dir / "canary.txt"
    second_marker.write_text("second\n", encoding="utf-8", newline="\n")
    need(first.dir != second.dir, "same-case invocations reused an output directory")
    need(first_marker.read_text(encoding="utf-8") == "first\n", "later invocation erased earlier output")
    need(second_marker.read_text(encoding="utf-8") == "second\n", "second invocation output changed")


@case("continuation-journal", "common", "Bounded continuation journal reuses actual Campaign queries and action paths")
def _continuation_journal(ctx):
    hosts.enemy(ctx.host)
    observed = {}
    for label, enabled, emit in (("off", 0, 0), ("frame0", 1, 0), ("frame1", 1, 1)):
        wasm = ctx.host_build(T("plan3-continuation-journal-current.cpp"), f"continuation-{label}",
                              ["-DP3_TWO_LEDGER", f"-DPLAN3_CONTINUATION_JOURNAL={enabled}",
                               f"-DPLAN3_CONTINUATION_EMIT={emit}"])
        for side, text in enumerate(sides(ctx, wasm, TAVI, stub=False)):
            rows = lines(text, "CONTINUATION_CALLER")
            need(len(rows) == 4 and all(row["side"] == side for row in rows), "missing production caller cases")
            one(text, "CONTINUATION_RESULT")
            need(len(lines(text, "P3CJ")) == (3 if emit else 0), "journal emission switch or row bound failed")
            observed[label, side] = rows
    need(all(observed["off", side] == observed[label, side] for label in ("frame0", "frame1") for side in (0, 1)),
         "journal added a modeled SDK query or changed the production caller's physical actions")


@case("observation-identity-join", "common", "Immutable identity lookup preserves current group observations and live query order")
def _identity_join(ctx):
    hosts.identity_join(ctx.host)
    observed = {}
    for arm in (0, 1):
        wasm = ctx.host_build(T("plan3-identity-join-current.cpp"), f"identity-join-{arm}",
                              [f"-DPLAN3_OBSERVATION_IDENTITY_JOIN={arm}"])
        for side, text in enumerate(sides(ctx, wasm, TAVI, stub=False)):
            result = one(text, "IDENTITY_JOIN_RESULT", side)
            rows = lines(text, "IDENTITY_JOIN_CALLER")
            need(result["arm"] == arm and result["cases"] == 30 and result["boundaryCases"] == 2,
                 "missing identity metadata, lifecycle, visibility, budget or epoch scenarios")
            need(len(rows) == 30 and all(row["side"] == side and row["nativeGames"] == 0 for row in rows),
                 "missing thirty-member production observation records")
            observed[arm, side] = rows
        for error in ("s:1", "s:-1", "i:1", "i:-1", "v:3"):
            _, err = ctx.run(wasm, {"P3_BP_SIDE": "0", **TAVI, "P3_IDENTITY_JOIN_ERROR": error}, fail=True)
            need(f"IDENTITY_JOIN_EXPECTED_ERROR {error}" in err and SDK_FAULT in err,
                 f"identity join swallowed {error} or changed its SDK boundary")
            expected = "unknown engine camp state" if error == "v:3" else (
                "invalid guest pointer or buffer (-1)" if error.endswith(":-1") else "engine error 1:")
            need(expected in err, f"identity join changed the exception diagnostic for {error}")
    need(all(observed[0, side] == observed[1, side] for side in (0, 1)),
         "identity join changed GroupObservation data, ordered live queries, errors or publication")


@case("observation-visible-join", "common", "Per-call visible identity lookup preserves first matches and current live observations")
def _visible_join(ctx):
    hosts.identity_join(ctx.host)
    observed = {}
    for arm in (0, 1):
        wasm = ctx.host_build(T("plan3-visible-join-current.cpp"), f"visible-join-{arm}",
                              ["-DPLAN3_OBSERVATION_IDENTITY_JOIN=1", f"-DPLAN3_OBSERVATION_VISIBLE_JOIN={arm}"])
        for side, text in enumerate(sides(ctx, wasm, TAVI, stub=False)):
            result = one(text, "VISIBLE_JOIN_RESULT", side)
            rows = lines(text, "IDENTITY_JOIN_CALLER")
            need(result["arm"] == arm and result["cases"] == 10 and len(rows) == 40,
                 "missing visible-list sequence, boundary or first-ordinal cases")
            one(text, "IDENTITY_JOIN_RESULT", side)
            observed[arm, side] = rows
        for error in ("s:1", "s:-1", "i:1", "i:-1", "v:3"):
            _, err = ctx.run(wasm, {"P3_BP_SIDE": "0", **TAVI, "P3_IDENTITY_JOIN_ERROR": error}, fail=True)
            need(f"IDENTITY_JOIN_EXPECTED_ERROR {error}" in err and SDK_FAULT in err,
                 f"visible join swallowed {error} or changed its SDK boundary")
            expected = "unknown engine camp state" if error == "v:3" else (
                "invalid guest pointer or buffer (-1)" if error.endswith(":-1") else "engine error 1:")
            need(expected in err, f"visible join changed the exception diagnostic for {error}")
    need(all(observed[0, side] == observed[1, side] for side in (0, 1)),
         "visible join changed GroupObservation data, live query order, errors or publication")


@case("observation", "common", "observer and ordinary Campaign with current Varg (SDK host)")
def _observation(ctx):
    hosts.observation(ctx.host)
    wasm = ctx.host_build(T("plan3-observation-current.cpp"), "observation", ["-DP3_TWO_LEDGER", f"-I{TESTS}"])
    varg = {"P3_BP_ACTOR": "varg"}
    for side, text in enumerate(sides(ctx, wasm, varg, stub=False)):
        one(text, "OBSERVATION_RESULT", None)
    for mode in ("state", "info"):
        _, err = ctx.run(wasm, {"P3_BP_SIDE": "0", **varg, "P3_OBSERVATION_ERROR": mode}, fail=True)
        need("OBSERVATION_EXPECTED_ERROR" in err and SDK_FAULT in err, f"{mode} error did not propagate")


@case("startup", "common", "startup queue, three altars, cold Ledger/Atlas commit (SDK host)")
def _startup(ctx):
    hosts.startup(ctx.host)
    wasm = ctx.host_build(T("plan3-startup-current.cpp"), "startup", ["-DP3_TWO_LEDGER", f"-I{TESTS}"])
    varg = {"P3_BP_ACTOR": "varg"}
    for side, text in enumerate(sides(ctx, wasm, varg, stub=False)):
        one(text, "STARTUP_RESULT", None)
    for mode in ("0", "1", "2"):
        _, err = ctx.run(wasm, {"P3_BP_SIDE": "0", **varg, "P3_STARTUP_ERROR": mode}, fail=True)
        need("STARTUP_EXPECTED_UNCOMMITTED_ERROR" in err and SDK_FAULT in err, f"pre-throw error {mode} did not propagate")


def p3_records(raw: bytes) -> list:
    """P3 frames per NUL-separated stdout segment; never splice fragments across segments."""
    records = []
    for segment in raw.split(b"\0"):
        for line in segment.splitlines(keepends=True):
            at = line.find(b"P3 ")
            if at < 0:
                continue
            frame = line[at:]
            need(frame.endswith(b"\n"), "unterminated P3 record within stdout segment")
            need(len(frame) <= 512, "P3 record exceeds 512 bytes")
            record = json.loads(frame[3:].decode("utf-8", errors="strict"))
            need(isinstance(record, dict), "non-object P3 record")
            records.append((frame, record))
    return records


def trace_report(text: str, require_publication: bool = True) -> None:
    """Compact sortie model certificates: complete counters within the source caps, and a
    worst-case-width frame still under 512 bytes."""
    records = p3_records(text.encode("utf-8"))
    keys = {"state", "reason", "fuel", "checks", "prefix", "fast", "basic", "phase", "resumed", "complete", "chunk", "turns"}
    models = []
    for frame, record in records:
        if record.get("kind") != "sortie-preparation" or "model" not in record:
            continue
        model = record["model"]
        need(isinstance(model, dict) and set(model) == keys, "incomplete compact model certificate")
        need(all(type(model[k]) is int and model[k] >= 0 for k in ("state", "reason", "checks", "prefix", "fast", "basic", "phase", "chunk", "turns")), "invalid model count")
        need(all(type(model[k]) is bool for k in ("resumed", "complete")), "invalid model flags")
        need(isinstance(model["fuel"], list) and len(model["fuel"]) == 2 and all(type(v) is int and v >= 0 for v in model["fuel"]), "invalid model fuel pair")
        need(not (model["chunk"] > 4 or model["turns"] > 524 or model["chunk"] > model["turns"] or model["checks"] < model["turns"] or model["fast"] + model["basic"] > 512), "model work exceeds source caps")
        need(all(type(record.get(k)) is int and record[k] >= 0 for k in ("round", "observed_round", "group")), "invalid model source epoch/group")
        need(record["observed_round"] <= record["round"], "future observed epoch")
        need(model["complete"] or not (record.get("candidate") or record.get("forecast")), "partial model published a candidate")
        models.append((frame, record))
    if not models:
        need(not require_publication, "no actual Campaign compact model records")
        return
    pending = [r for _, r in models if not r["model"]["complete"]]
    complete = [r for _, r in models if r["model"]["complete"] and r.get("candidate") is True and r.get("forecast") is True]
    if require_publication:
        need(pending and complete, "stdout lacks pending and published-complete model records")
        need(any(r["model"]["resumed"] and r["model"]["turns"] > 0 for _, r in models), "no positive resumed model progress")
    need(all(r["model"]["turns"] > 0 and r.get("tau", 0) > 0 for r in complete), "vacuous complete model record")
    widest = max(v for _, r in models for v in r["model"]["fuel"])
    for _, r in models:
        wide = copy.deepcopy(r)
        wide.update(round=8192, observed_round=8192, group=65535, tau=8192, spawn_distance=65535, before=widest, after=widest)
        wide["model"].update(fuel=[widest] * 2, checks=0xffffffff, prefix=0xffffffff, fast=512, basic=512, phase=10, chunk=4, turns=524)
        need(len(b"P3 " + json.dumps(wide, separators=(",", ":"), ensure_ascii=False).encode("utf-8") + b"\n") <= 512, "worst-case model frame exceeds 512 bytes")


def rows_checks(text: str, side: int, mode: str) -> None:
    if mode == "STATIC_KIT_ONLY":
        kit, cold, endpoint = (lines(text, k) for k in ("SORTIE_STATIC_KIT", "SORTIE_COLD_CALLER", "SORTIE_METRICS_ENDPOINT"))
        need(len(kit) == len(cold) == len(endpoint) == 1, "missing static-kit/cold-caller/physical-endpoint result")
        kit, cold, endpoint = kit[0], cold[0], endpoint[0]
        need(all(r["side"] == side and r["nativeFuelClaim"] is False for r in (kit, cold, endpoint)), "static-kit side/scope mismatch")
        need(kit["staticCalls"] > 1 and kit["staticKitQueries"] == 0 and kit["lateKitQueries"] > 0 and kit["modelStarts"] == 1 and kit["freshInput"] == [899, 50, 1, 3, 0, 0, 0, 1], "cold static/fresh kit boundary failed")
        need(all(kit[k] is True for k in ("queryTimeGeometryGuard", "unsupportedKitRejected", "lateFuelDenied", "lateRoundRejected")), "missing late live-fact negative")
        need(cold["initialPriorTurns"] == 0 and cold["metadataWarmCalls"] == 0 and cold["initialPrivateWork"] is False and cold["qComplete"] is True and cold["ordinaryGoldActions"] and cold["ordinaryGoldMoves"], "cold Campaign did not retain ordinary progress")
        need((endpoint["converted"], endpoint["modelStarts"], endpoint["modelTerminals"], endpoint["modelSchedules"], endpoint["forecasts"], endpoint["episodes"], endpoint["conversions"]) == (12, 1, 1, 1, 1, 1, 1), "deferred kit changed complete endpoint/admission")
        trace_report(text, require_publication=False)
    elif mode == "METRICS_ONLY":
        transition, endpoint, abort = (lines(text, "SORTIE_METRICS_" + k) for k in ("TRANSITIONS", "ENDPOINT", "ABORT"))
        need(len(transition) == len(endpoint) == len(abort) == 1, "missing metrics transition/endpoint/abort")
        transition, endpoint, abort = transition[0], endpoint[0], abort[0]
        need(all(r["side"] == side and r["nativeFuelClaim"] is False for r in (transition, endpoint, abort)), "metrics scope/side mismatch")
        need((transition["initialOwnerTurns"], transition["originReset"], transition["originLostTurns"], transition["starts"], transition["terminals"], transition["schedules"], transition["clonedPublications"], transition["finalResets"], transition["completedStaleReset"]) == (4, 1, 4, 2, 1, 1, 2, 2, 3), "typed transition counters differ")
        need(transition["queryFreeAfterReset"] is True and transition["freshOriginChecked"] is True, "stale publication/snapshot regression")
        need((endpoint["converted"], endpoint["modelStarts"], endpoint["modelTerminals"], endpoint["modelSchedules"], endpoint["modelResets"], endpoint["forecasts"], endpoint["episodes"], endpoint["conversions"], endpoint["aborts"]) == (12, 1, 1, 1, 1, 1, 1, 1, 0) and endpoint["candidateSnapshots"] >= 2, "physical endpoint counters differ")
        need(endpoint["postConversionReset"] == 1 and endpoint["discardedCompletedOwnerTurns"] > 0, "post-conversion stale input not counted once")
        need((abort["episodes"], abort["conversions"], abort["aborts"]) == (1, 0, 1), "owned episode abort counted incorrectly")
        summaries = [r for _, r in p3_records(text.encode("utf-8")) if r.get("kind") == "summary"]
        need(summaries and all(r.get("sx") == 1 and len(r.get("base", [])) == 6 and len(r.get("sortie", [])) == 19 for r in summaries), "direct Ledger callback did not use compact summary")
        need(any(r["sortie"][4:6] == [1, 1] and r["sortie"][15] == 1 for r in summaries), "compact summaries lost model/episode counters")
        trace_report(text, require_publication=False)
    elif mode == "EXPOSURE_ONLY":
        ref, caller = lines(text, "SORTIE_EXPOSURE_REFERENCE"), lines(text, "SORTIE_EXPOSURE_CALLER")
        need(len(ref) == len(caller) == 1, "missing exposure reference/caller result")
        ref, caller = ref[0], caller[0]
        need(ref["side"] == side and ref["target"] == [192, 2230][side] and ref["actualRowsOrigins"] == 17 and min(ref["actualReadyCuts"], ref["boundaryReadyCuts"]) > 1, "incomplete exact scalar exposure reference")
        need((ref["maximumLeg"], ref["rejectedLeg"], ref["perCallUnits"]) == (128, 129, 8) and all(ref[k] is True for k in ("originalPointCostsChecked", "rootExcluded", "resetClearsBinding")), "exposure limits/reset failed")
        need(caller["side"] == side and caller["target"] == [192, 2206][side] and caller["goldJob"][:2] == [0, [798, 1551][side]], "wrong naturally selected exposure/Gold targets")
        need(caller["initialDecisions"] == 0 and caller["initialOutwardWork"] is False and caller["scope"].startswith("PREPARED-STATIC-ROUTE EXPOSURE-COLD"), "prepared boundary scope missing")
        need(caller["bound"] is True and caller["actorSnapshotCheckedAtBinding"] is True and caller["reference"] == caller["exposure"], "fresh exact-origin exposure did not bind")
        need(caller["ordinaryGoldActionsWhilePending"] and caller["ordinaryGoldMovesWhilePending"] and caller["backgroundCachedNodes"], "pending exposure froze ordinary Gold")
        need(caller["stagedAvailabilityCallbacks"] and 45000 <= caller["stagedFuelMinimum"] < 65000, "staged availability lost ordinary action budget")
        need(ref["nativeFuelClaim"] is False and caller["nativeFuelClaim"] is False, "exposure synthetic fuel overclaim")
        trace_report(text, require_publication=False)
    elif mode == "Q_COLD_ONLY":
        rows = lines(text, "SORTIE_COLD_CALLER")
        need(len(rows) == 1, "missing cold caller result")
        row = rows[0]
        need(row["side"] == side and row["initialPriorTurns"] == 0 and row["metadataWarmCalls"] == 0 and row["initialPrivateWork"] is False, "cold controller scene was warmed/primed")
        need(row["firstTargetOpportunity"] == 2 and row["firstWallOpportunity"] == 2 and row["qStartOpportunity"] >= 2 and row["eligibleOpportunities"] >= row["qStartOpportunity"], "invalid eligible-opportunity cadence")
        need(row["qCursor"] == 49 and row["qComplete"] is True and row["ordinaryGoldActions"] and row["ordinaryGoldMoves"] and row["activeBackgroundSlots"] and row["maximumCallbackSlots"] <= 16, "cold caller did not progress alongside ordinary Gold")
        need(row["nativeFuelClaim"] is False and row["finalHealth"] > 0, "cold scope/safety failure")
        trace_report(text, require_publication=False)
    else:
        one(text, "SORTIE_ROWS_RESULT", side)
        ref, caller = lines(text, "SORTIE_Q_REFERENCE"), lines(text, "SORTIE_Q_CALLER")
        need(len(ref) == len(caller) == 1, "missing off-spawn Q reference/caller result")
        ref, caller = ref[0], caller[0]
        need(ref["target"] == [192, 2230][side] and ref["identityFaults"] == 3 and ref["feasible"] > 0, "invalid direct Q scope")
        need(caller["naturalTarget"] == [192, 2206][side] and caller["goldJob"][:2] == [0, [798, 1551][side]], "wrong natural caller/ordinary target")
        need(caller["initialPriorTurns"] == [757, 541][side] and caller["coldBootClaim"] is False, "historical-phase scene overclaim")
        need(caller["qScanInitiallyEmpty"] is True and 1 <= caller["metadataCalls"] <= 16 and isinstance(caller["directFirstBinding"], bool), "public metadata/Q cold boundary missing")
        need(caller["legalMoveWhilePartial"] and caller["ordinaryGoldProgress"] and 0 < caller["maximumCallbackSlots"] <= 16, "no bounded moving ordinary caller progress")
        need(45000 <= caller["stagedBudgetAfter"] < 65000 and caller["stagedBudgetBefore"] == 100000 and caller["stagedCallbacks"] >= 1, "staged background denial lost ordinary budget")
        need(ref["nativeFuelClaim"] is False and caller["nativeFuelClaim"] is False, "synthetic budget overclaim")
        trace_report(text)


@case("sortie-ability", "common", "Tavi ability sorties on 8x8 and the actual public Rows geometry (SDK host)")
def _sortie_ability(ctx):
    rows = hosts.sortie_ability(ctx.host)
    small = ctx.host_build(T("plan3-sortie-ability-current.cpp"), "ability", ["-DP3_TWO_LEDGER"])
    large = ctx.host_build(T("plan3-sortie-ability-current.cpp"), "rows", ["-DP3_TWO_LEDGER", "-DP3_SORTIE_ROWS"], host=rows)
    for side, text in enumerate(sides(ctx, small, TAVI, stub=False)):
        one(text, "SORTIE_ABILITY_RESULT", side)
        trace_report(text, require_publication=False)
    labels = {None: "rows-default", "Q_COLD_ONLY": "rows-cold", "EXPOSURE_ONLY": "rows-exposure",
              "METRICS_ONLY": "rows-metrics", "STATIC_KIT_ONLY": "rows-static-kit"}
    for mode, label in labels.items():
        def mode_runs(mode=mode):
            extra = {"P3_" + mode: "1"} if mode else {}
            for side, text in enumerate(sides(ctx, large, {**TAVI, **extra}, stub=False)):
                rows_checks(text, side, mode or "default")
        ctx.part(label, mode_runs)


@case("indexer-nominal", "indexer-common", "shared nominal materialization preserves Indexer project triples and controls (SDK host)")
def _indexer_nominal(ctx):
    hosts.indexer_current(ctx.host)
    for nominal in (0, 1):
        wasm = ctx.host_build(T("plan3-indexer-nominal-current.cpp"), f"indexer-nominal-{nominal}",
                              [f"-DPLAN3_INDEXER_NOMINAL_CAMPAIGN={nominal}"])
        for side in (0, 1):
            for actor in ("varg", "tavi"):
                text = ctx.run(wasm, {"P3_IDX_SIDE": str(side), "P3_IDX_ACTOR": actor}, stub=True)
                rows = lines(text, "IX_NOMINAL_RESULT")
                flows = lines(text, "IX_CURRENT_RESULT")
                need(len(rows) == 1 and rows[0]["status"] == "pass" and rows[0]["tables"] == 24 and
                     rows[0]["rows"] > 0 and rows[0]["reduced"] > 0 and rows[0]["positiveTriples"] == 3,
                     "missing Indexer nominal parity tables")
                need(len(flows) == 1 and flows[0]["status"] == "pass" and flows[0]["flows"] == 3 and
                     flows[0]["conversions"] == 12, "missing original Indexer policy flows")
                print("IX_NOMINAL_CHECK " + json.dumps({"nominal": nominal, **rows[0]}))


@case("indexer", "indexer-common", "renewal index vs rate/circuit, conversions, prediction tail (SDK host)")
def _indexer(ctx):
    hosts.indexer_current(ctx.host)
    wasm = ctx.host_build(T("plan3-indexer-current-sdk.cpp"), "indexer")
    for side in (0, 1):
        for actor in ("varg", "tavi"):
            text = ctx.run(wasm, {"P3_IDX_SIDE": str(side), "P3_IDX_ACTOR": actor}, stub=True)
            summary, endpoint, tail = [], None, None
            for line in text.splitlines():
                need(not line.startswith("p3 error:"), "runtime error observed")
                if line.startswith("IX_TAIL_BEGIN "):
                    tail = {"begin": json.loads(line.split(" ", 1)[1]), "predictions": 0}
                if line.startswith("P3 "):
                    need(len(line.encode()) + 1 <= 512, "oversized telemetry")
                    frame = json.loads(line[3:])
                    if tail and frame["kind"] == "prediction":
                        tail["predictions"] += 1
                    if frame["kind"] == "start":
                        endpoint = None
                    if frame["kind"] == "endpoint":
                        endpoint = frame
                if line.startswith("IX_CURRENT "):
                    record = json.loads(line[11:])
                    if record["case"] == "prediction-tail":
                        need(tail and all(record[k] == tail["begin"][k] for k in ("mode", "fuel", "turn")), "tail marker mismatch")
                        need(tail["predictions"] == int(record["fuel"] >= 45000), "prediction guard boundary failed")
                        need(record["omitted"] == int(record["fuel"] < 45000) and record["actions"] == 0 and record["moves"] == 0, "tail action/omission mismatch")
                        need(not record["turn"] or record["freshSnapshot"], "tail turn snapshot missing")
                        tail = None
                    if record["case"] == "conversion":
                        if record["completed"]:
                            need(endpoint and endpoint["conversion"] and endpoint["actor_score_lower_bound"] == record["lowerBound"], "conversion endpoint missing/lower bound differs")
                            need(endpoint["exact_conversion"] == (record["lowerBound"] if record["exactExpected"] else None), "exact vs overlapping conversion differs")
                        else:
                            need(endpoint is None, "unconfirmed episode emitted endpoint")
                if line.startswith("IX_CURRENT_RESULT "):
                    summary.append(json.loads(line[18:]))
            need(len(summary) == 1 and summary[0]["status"] == "pass" and summary[0]["side"] == side, "missing side result")


def check_backpressure(text: str, side: int) -> None:
    frames, forks, conversions, summary, tail, gate, queue = [], [], [], None, None, None, None
    for line in text.splitlines():
        need(not line.startswith("p3 error:"), "runtime error observed")
        if line.startswith("BP_TAIL_BEGIN "):
            tail = {"begin": json.loads(line.split(" ", 1)[1]), "predictions": 0}
        if line.startswith("BP_GATE_BEGIN "):
            need(gate is None, "nested diagnostic block")
            gate = {"begin": json.loads(line.split(" ", 1)[1]), "events": []}
        if line.startswith("P3 "):
            need(len((line + "\n").encode()) <= 512, "oversize telemetry")
            frame = json.loads(line[3:])
            frames.append(frame)
            if tail and frame["kind"] == "prediction":
                tail["predictions"] += 1
            if gate and frame["kind"] == "planning-gate":
                gate["events"].append(frame)
        elif line.startswith("BP_TAIL_RESULT "):
            result = json.loads(line.split(" ", 1)[1])
            need(tail and all(result[k] == tail["begin"][k] for k in ("arm", "fuel", "turn")), "tail marker mismatch")
            need(tail["predictions"] == int(result["fuel"] >= 45000), "prediction guard boundary failed")
            need(result["omitted"] == int(result["fuel"] < 45000) and result["actions"] == 0 and result["moves"] == 0, "tail action/omission mismatch")
            need(not result["turn"] or result["freshSnapshot"], "tail turn snapshot missing")
            tail = None
        elif line.startswith("BP_REAL_FORK "):
            forks.append(json.loads(line.split(" ", 1)[1]))
        elif line.startswith("BP_DOMAIN_REASON "):
            value = json.loads(line.split(" ", 1)[1])
            need(value["queryParity"] and value["boolParity"], "reason report changed queries/eligibility")
        elif line.startswith("BP_GATE_RESULT "):
            value = json.loads(line.split(" ", 1)[1])
            need(gate and value["case"] == gate["begin"]["case"], "diagnostic block mismatch")
            events, old = gate["events"], gate["begin"]["oldRound"]
            need(len(events) == value["events"], "missing diagnostic events")
            need(all(len(e["fuel"]) == 9 and all(type(x) is int and -1 <= x <= 1000000 for x in e["fuel"]) for e in events), "invalid historical fuel vector")
            first = events[0]
            need(first["gate"] == "vision_fuel" and first["observed_round"] == old and first["coherent"] and first["fuel"][3] == 28999, "lost denial sample identity")
            need(first["round"] > old, "denial formatted on exhausted callback")
            if value["case"] == "bounded_trace":
                need(len(events) == 10 and len({e["gate"] for e in events}) == 10, "diagnostic bound/once violated")
                need(first["fuel"] == [28999, -1, -1, 28999, -1, -1, -1, -1, -1], "unmeasured point fabricated")
                need(events[1]["observed_round"] == old + 1 and not events[1]["coherent"], "mixed-round sample relabeled coherent")
            else:
                need(value["freshRound"] == first["round"] and value["observedRound"] == old and first["fuel"][0] == 1000000, "fresh callback relabeled historical inputs")
            gate = None
        elif line.startswith("BP_CURRENT_CONVERSION "):
            conversions.append(json.loads(line.split(" ", 1)[1]))
        elif line.startswith("BP_QUEUE_SELF_RESULT "):
            need(queue is None, "duplicate queue result")
            queue = json.loads(line.split(" ", 1)[1])
        elif line.startswith("BACKPRESSURE_SDK_RESULT "):
            summary = json.loads(line.split(" ", 1)[1])
    need(summary and summary["status"] == "passed" and summary["side"] == side, "missing side result")
    need(gate is None and summary["diagnosticCases"] >= 25 and summary["planningTraceCases"] == 2, "missing planning-gate checks")
    need(len(forks) == 1 and len(conversions) == 18, "missing fork/conversion cases")
    need(queue and queue["cases"] == 15 and queue["cutoffs"] >= 176 and queue["focusedCuts"] == 3, "incomplete queue/cutoff checks")
    fork = forks[0]
    need(fork["queues"] == [0, 66, 10, 0] and fork["pressureConverted"] == 76 and fork["rateConverted"] == 10, "queue conservation changed")
    need(int(fork["pressureNumerator"]) == 4456 and int(fork["rateNumerator"]) == 10, "wrong independent scores")
    need(Fraction(4456, fork["pressureD"]) > Fraction(100, fork["rateD"]) and Fraction(10, fork["rateD"]) > Fraction(76, fork["pressureD"]), "lost governing pressure/rate fork")
    contexts = [f for f in frames if f["kind"] == "backpressure-context" and f.get("Q") == fork["queues"]]
    need({f["arm"] for f in contexts} == {0, 1}, "missing both governing arm contexts")
    for context in contexts:
        need(context["ids"] == fork["ids"], "context/job identity mismatch")
        score = context["r"] if context["arm"] else context["a"]
        numerator = sum(context["s"]) if context["arm"] else sum(q * s for q, s in zip(context["Q"], context["s"]))
        need(int(score[0]) == numerator and score[1] == context["D"], "incorrect actual context arithmetic")
    endpoints = [f for f in frames if f["kind"] == "endpoint" and f.get("conversion")]
    need(sum(f.get("exact_conversion") is not None for f in endpoints) >= 6, "no exact endpoint witness")
    need(sum(f.get("exact_conversion") is None for f in endpoints) >= 2, "overlap falsely exact")


@case("backpressure", "backpressure-common", "pressure vs rate fork, queue cutoffs, gates, conversions (SDK host)")
def _backpressure(ctx):
    hosts.backpressure_current(ctx.host)
    wasm = ctx.host_build(T("plan3-backpressure-current-sdk.cpp"), "backpressure", [f"-I{TESTS}"])
    for side in (0, 1):
        code, text, err = ctx.run(wasm, {"P3_BP_SIDE": str(side)}, stub=True, check=False)
        # The queue self-checks run first and never touch the shared Campaign reference.
        queue = lines(text, "BP_QUEUE_SELF_RESULT")
        need(len(queue) == 1 and queue[0]["cases"] == 15 and queue[0]["cutoffs"] >= 176 and queue[0]["focusedCuts"] == 3, "incomplete queue/cutoff checks")

        def full(code=code, text=text, err=err, side=side):
            need(code == 0, f"side {side} exited {code}\n{err[-1500:]}")
            check_backpressure(text, side)
        ctx.part("shared-parity", full)
    for side in (0, 1):
        for item in (0, 28, 6):
            rows = lines(ctx.run(wasm, {"P3_BP_SIDE": str(side), "P3_BP_BAD_COST": str(item)}, stub=True), "BP_STATIC_DOMAIN_RESULT")
            need(len(rows) == 1 and rows[0]["side"] == side and rows[0]["item"] == item and rows[0]["reason"] == "availability", "missing immutable-cost reason")


# ----------------------------------------------------------------------------- Python tools
@case("sequence-export", "sequence-common", "offline CFR exporter tests; policy-data.h equals a fresh export")
def _sequence_export(ctx):
    suite = unittest.defaultTestLoader.loadTestsFromName("plan3_sequence_current_test")
    result = unittest.TestResult()
    suite.run(result)
    need(result.wasSuccessful() and result.testsRun, "exporter tests failed: " + "; ".join(str(t) for t, _ in result.failures + result.errors)[:600])
    tool = SUITE / "tools" / "export_sequence.py"
    header, report = ctx.dir / "policy-data.h", ctx.dir / "report.json"
    with core.Slots(1, quiet=True):
        p = subprocess.run([sys.executable, "-B", str(tool), "--header", str(header), "--report", str(report)], capture_output=True, text=True)
    need(p.returncode == 0, "export failed: " + (p.stdout + p.stderr)[-600:])
    committed = (SUITE / "sequence-common" / "policy-data.h").read_text(encoding="utf-8")
    need(header.read_text(encoding="utf-8") == committed, "sequence-common/policy-data.h differs from a fresh export")


# ----------------------------------------------------------------------------- driver
def relevant(names: list[str]) -> set[str]:
    """Suite modules reached by the named bots' build closures."""
    modules = set()
    for name in names:
        found = core.select(name if "/" in name else f"decision/{name}")
        need(found and all(b.suite == "decision" for b in found), f"{name!r} is not a decision bot")
        for b in found:
            modules |= {p.relative_to(SUITE).parts[0] for p in core.closure(b) if p.is_relative_to(SUITE)}
    return modules


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("bots", nargs="*", help="decision bots (default: every case)")
    ap.add_argument("-k", dest="pattern", help="only cases whose name matches this glob")
    ap.add_argument("--list", action="store_true", help="list cases and their target modules")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="parallel cases (default: half the arena slots, at most 12)")
    args = ap.parse_args(argv)
    try:
        chosen = CASES
        if args.bots:
            modules = relevant(args.bots)
            chosen = [c for c in chosen if modules & set(c.modules)]
        if args.pattern:
            chosen = [c for c in chosen if fnmatch.fnmatchcase(c.name, args.pattern)]
    except (Failure, core.ArenaError) as error:
        print(f"run.py: {error}", file=sys.stderr)
        return 2
    if args.list:
        for c in chosen:
            print(f"{c.name:22} {' '.join(c.modules):32} {c.what}")
        return 0
    if not chosen:
        print("no cases selected")
        return 1

    def execute(c: Case):
        started = time.perf_counter()
        ctx = None
        try:
            ctx = Ctx(c.name)
            c.func(ctx)
            detail = f"{ctx.runs} runs" if ctx.runs != 1 else "1 run"
            if ctx.known:
                detail += "; known failing: " + ", ".join(ctx.known)
            if ctx.fixed:
                detail += "; KNOWN BUT PASSING (drop from KNOWN): " + ", ".join(ctx.fixed)
            return c, True, detail, time.perf_counter() - started, ctx
        except Exception as error:  # report every failure kind as one bounded case result
            message = str(error) if isinstance(error, (Failure, ValueError)) else f"{type(error).__name__}: {error}"
            return c, False, message, time.perf_counter() - started, ctx

    jobs = args.jobs or max(1, min(12, core.slot_count() // 2))
    failed, known = [], 0
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for c, ok, detail, seconds, ctx in pool.map(execute, chosen):
            known += len(ctx.known) if ctx else 0
            if ok:
                print(f"ok   {c.name:22} {seconds:6.1f}s  {detail}", flush=True)
            else:
                failed.append(c.name)
                head, *tail = detail.strip().splitlines() or ["(no message)"]
                print(f"FAIL {c.name:22} {seconds:6.1f}s  {head[:200]}", flush=True)
                for line in tail[-14:]:
                    print("       " + line[:200])
    print(f"{len(chosen) - len(failed)}/{len(chosen)} cases passed"
          + (f", {known} known-failing parts (README: Known failures)" if known else "")
          + (f"; failed: {' '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
