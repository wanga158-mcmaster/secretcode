"""Question-agnostic, local-only experiments using arena snapshots, games and paired reports.

Use the Python printed by arena env:
  python -B bots/surprise/tools/lab.py plan [--spec FILE] [--maps SELECTOR]
  python -B bots/surprise/tools/lab.py run --out .cache/surprise/NAME [same options]

The editable recipe describes a question, arbitrary subject knob variants and exact local
opponents (with optional knob overrides). It prescribes no strategy classes or optimizer.
Every subject variant uses one captured source closure. No shared ladder or online APIs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path

SUITE = Path(__file__).resolve().parents[1]
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
import core  # noqa: E402
import games  # noqa: E402
import sweep  # noqa: E402

SUBJECT = "surprise/_greed"
DEFAULT_SPEC = SUITE / "experiment.json"
NAME = re.compile(r"[A-Za-z0-9_.+-]+\Z")


@dataclass
class Plan:
    record: dict
    items: list[tuple]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def knobs(value) -> dict[str, str]:
    if not isinstance(value, dict):
        raise core.ArenaError("set must be an object of knob names and C++ values")
    out = {}
    for name, v in value.items():
        if not re.fullmatch(r"[A-Za-z_]\w*", name):
            raise core.ArenaError(f"invalid knob name {name!r}")
        if type(v) not in (str, bool, int, float) or (isinstance(v, float) and not math.isfinite(v)):
            raise core.ArenaError(f"invalid knob value for {name}")
        text = str(v).lower() if isinstance(v, bool) else str(v)
        if not text.strip() or "\n" in text or "\r" in text:
            raise core.ArenaError(f"invalid knob value for {name}")
        out[name] = text
    return out


def named_rows(value, opponent=False) -> list[dict]:
    if not isinstance(value, list) or not value:
        raise core.ArenaError("give at least one opponent and one variant")
    found, out = set(), []
    for row in value:
        allowed = {"name", "set", "notes"} | ({"bot"} if opponent else set())
        if not isinstance(row, dict) or set(row) - allowed:
            raise core.ArenaError(f"unknown fields in {'opponent' if opponent else 'variant'}")
        name = row.get("name")
        if not isinstance(name, str) or not NAME.fullmatch(name) or name in found or name == "base":
            raise core.ArenaError(f"invalid, reserved or duplicate name {name!r}")
        found.add(name)
        out.append(dict(row, set=knobs(row.get("set", {}))))
    return out


def make_plan(recipe: dict, maps_override: str = "") -> Plan:
    allowed = {"schema_version", "question", "notes", "maps", "seeds", "seed_knob", "variants", "opponents"}
    if not isinstance(recipe, dict) or set(recipe) - allowed:
        raise core.ArenaError("unknown experiment fields")
    if type(recipe.get("schema_version")) is not int or recipe["schema_version"] != 1:
        raise core.ArenaError("expected experiment schema_version 1")
    if not isinstance(recipe.get("question"), str) or not recipe["question"].strip():
        raise core.ArenaError("state the current question")
    variants = [{"name": "base", "set": {}}] + named_rows(recipe.get("variants"))
    opponents = named_rows(recipe.get("opponents"), opponent=True)
    seeds = recipe.get("seeds", [0])
    if (not isinstance(seeds, list) or not seeds or any(type(s) is not int or s < 0 for s in seeds)
            or len(set(seeds)) != len(seeds)):
        raise core.ArenaError("seeds must be distinct nonnegative integers; 0 means no perturbation")
    seed_knob = recipe.get("seed_knob", "noise_burn")
    if not isinstance(seed_knob, str) or not re.fullmatch(r"[A-Za-z_]\w*", seed_knob):
        raise core.ArenaError("invalid seed_knob")
    map_selector = maps_override or recipe.get("maps", "pool")
    if not isinstance(map_selector, str):
        raise core.ArenaError("maps must be an arena map selector")
    maps = core.map_set(map_selector)
    if not maps:
        raise core.ArenaError("no maps selected")

    captured, items, bindings = {}, [], []

    def add(role, name, seed, ident, settings):
        if not isinstance(ident, str) or ident.count("/") != 1:
            raise core.ArenaError("opponents must use exact local bot IDs")
        b = core.bot(ident)
        if b.id != ident:
            raise core.ArenaError("opponents must use exact local bot IDs")
        if ident not in captured:
            captured[ident] = core.read_closure(b)
        contents = captured[ident]
        changed = core.apply_overrides(b, contents, settings) if settings else contents
        key = f"{ident}@{core.version_of(changed, settings)}"
        bindings.append(dict(role=role, name=name, seed=seed, key=key, set=settings))
        items.append((b, settings, f"{ident}:{name}", contents))

    for seed in seeds:
        for variant in variants:
            settings = dict(variant["set"])
            if seed:
                if seed_knob in settings:
                    raise core.ArenaError(f"variant {variant['name']} overrides the active seed knob")
                settings[seed_knob] = str(seed)
            add("subject", variant["name"], seed, SUBJECT, settings)
    for opponent in opponents:
        add("opponent", opponent["name"], None, opponent.get("bot"), opponent["set"])
    opponent_keys = [b["key"] for b in bindings if b["role"] == "opponent"]
    if len(set(opponent_keys)) != len(opponent_keys):
        raise core.ArenaError("duplicate exact opponent versions add no evidence; keep one entry")

    record = dict(schema_version=1, subject=SUBJECT, question=recipe["question"],
                  notes=recipe.get("notes", ""), variants=variants, opponents=opponents,
                  maps=[dict(name=m, sha256=digest(core.map_path(m)), **core.map_info(m)) for m in maps],
                  sides=["A", "B"], seeds=seeds, seed_knob=seed_knob, bindings=bindings,
                  source_versions={ident: core.version_of(c) for ident, c in captured.items()},
                  bundle_sha256=digest(ROOT / "docs/reference/current-version.json"),
                  toolchain=core.toolchain(),
                  expected_games=len(maps) * 2 * len(seeds) * len(variants) * len(opponents))
    return Plan(record, items)


def game_specs(plan: Plan, entrants: list) -> list[games.GameSpec]:
    record = plan.record
    bound = list(zip(record["bindings"], entrants, strict=True))
    mine = {(b["name"], b["seed"]): e for b, e in bound if b["role"] == "subject"}
    opponents = [(b, e) for b, e in bound if b["role"] == "opponent"]
    out = []
    for opponent, other in opponents:
        for m in record["maps"]:
            for side in record["sides"]:
                for seed in record["seeds"]:
                    for variant in record["variants"]:
                        me = mine[variant["name"], seed]
                        a, b = (me, other) if side == "A" else (other, me)
                        tags = dict(subject=side, variant=variant["name"], seed=seed,
                                    opponent=opponent["name"], phase="sweep", private=True)
                        out.append(games.GameSpec(a, b, m["name"], tags))
    return out


def check_rows(rows: list[dict], record: dict) -> dict:
    problems, indexed = [], {}
    if len(rows) != record["expected_games"]:
        problems.append(f"incomplete coverage: {len(rows)}/{record['expected_games']} rows")
    for row in rows:
        tags = row.get("tags", {})
        key = (tags.get("opponent"), row.get("map"), tags.get("subject"), tags.get("seed"))
        index = (tags.get("variant"), key)
        if index in indexed:
            problems.append(f"duplicate game key {index}")
        indexed[index] = row
        if row.get("winner") not in ("A", "B", "draw") or row.get("truncated"):
            problems.append(f"unfinished game {index}")
        for side in "AB":
            flags = games.health(row, side)
            if flags:
                problems.append(f"{index} {side}: {', '.join(flags)}")
        if row.get("note") and "not measured" in row["note"]:
            problems.append(f"unmeasured health {index}: {row['note']}")
    same = {(b["name"], b["seed"]): b["key"] for b in record["bindings"] if b["role"] == "subject"}
    identical = 0
    for (variant, key), row in indexed.items():
        if variant == "base" or same.get((variant, key[3])) != same.get(("base", key[3])):
            continue
        base = indexed.get(("base", key))
        if base is None:
            problems.append(f"missing control for {variant} {key}")
        elif any(row.get(field) != base.get(field) for field in ("winner", "rounds", "A", "B", "engine")):
            problems.append(f"unchanged-build mismatch {variant} {key}")
        else:
            identical += 1
    return dict(ok=not problems, games=len(rows), identical_control_pairs=identical,
                comparison_scope="final native aggregates and fuel; no world-prefix equivalence claim",
                problems=problems)


def write_json(path: Path, data) -> None:
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def verify_inputs(record: dict) -> None:
    if digest(ROOT / "docs/reference/current-version.json") != record["bundle_sha256"]:
        raise core.ArenaError("bundle changed during preparation; use a new experiment")
    for m in record["maps"]:
        if digest(core.map_path(m["name"])) != m["sha256"]:
            raise core.ArenaError(f"map changed during preparation: {m['name']}")


def run_plan(plan: Plan, out: Path, keep_replays="all") -> int:
    out = Path(out).resolve()
    if not any(out.is_relative_to((ROOT / p).resolve()) for p in (".cache/surprise", "results/surprise")):
        raise core.ArenaError("keep private output under .cache/surprise/ or results/surprise/")
    if re.search(r"\s", str(out)):
        raise core.ArenaError("arena output paths cannot contain whitespace")
    manifest = out / "experiment.json"
    if manifest.exists():
        if json.loads(manifest.read_text(encoding="utf-8")) != plan.record:
            raise core.ArenaError("experiment inputs changed; choose a new output directory")
    elif out.exists() and any(out.iterdir()):
        raise core.ArenaError("refusing a nonempty output directory without an experiment identity")
    out.mkdir(parents=True, exist_ok=True)
    if not manifest.exists():
        write_json(manifest, plan.record)
    verify_inputs(plan.record)
    entrants = core.prepare_many(plan.items)
    for binding, entrant in zip(plan.record["bindings"], entrants, strict=True):
        if not entrant.ok:
            write_json(out / "check.json", dict(ok=False, build_failed=entrant.key, log=entrant.log))
            print(f"build failed: {entrant.key}\n{entrant.log[-2500:]}", file=sys.stderr)
            return 1
        if entrant.key != binding["key"]:
            raise core.ArenaError("prepared snapshot differs from the experiment identity")
    verify_inputs(plan.record)
    rows = games.play(game_specs(plan, entrants), out, keep_replays=keep_replays, resume=True)
    checks = check_rows(rows, plan.record)
    sections, results = sweep.sweep_report(rows, [v["name"] for v in plan.record["variants"]])
    write_json(out / "check.json", checks)
    write_json(out / "results.json", results)
    lines = ["# " + plan.record["question"], "", plan.record["notes"], "",
             f"Coverage: {len(rows)}/{plan.record['expected_games']} full-game keys; both sides.",
             f"Infrastructure/health checks: {'PASS' if checks['ok'] else 'FAIL'}.",
             "Scope: this experiment answers its stated question; it does not establish optimality.", "",
             *sweep.render(sections, terminal=False)]
    (out / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"{len(rows)} games; checks {'PASS' if checks['ok'] else 'FAIL'}; "
          f"{checks['identical_control_pairs']} identical control pairs; {out / 'report.md'}")
    for problem in checks["problems"][:8]:
        print(problem)
    return 0 if checks["ok"] else 1


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["plan", "run"])
    parser.add_argument("--spec", type=Path, default=DEFAULT_SPEC)
    parser.add_argument("--maps", default="", help="override this experiment's map selector")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--keep-replays", choices=games.KEEP, default="all")
    args = parser.parse_args(argv)
    try:
        if args.command == "run" and args.out is None:
            raise core.ArenaError("run requires --out .cache/surprise/NAME (same inputs resume)")
        plan = make_plan(json.loads(args.spec.read_text(encoding="utf-8")), args.maps)
        r = plan.record
        print(f"Question: {r['question']}")
        print(f"Private subject: {SUBJECT}; {len(r['maps'])} maps; both sides; {r['expected_games']} games")
        print("Variants: " + ", ".join(v["name"] for v in r["variants"]))
        print("Opponents: " + ", ".join(o["name"] for o in r["opponents"]))
        if args.command == "plan":
            print("Plan only: no builds or games. Run records exact source, bundle and map identities.")
            return 0
        return run_plan(plan, args.out, args.keep_replays)
    except (core.ArenaError, OSError, ValueError) as error:
        print(f"surprise lab: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
