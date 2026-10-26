"""Read-only full40-pool scoreboard from existing exact-source tournament rows.

  python -B -I -X utf8 bots/contenders/tools/pool_scoreboard.py RUN ...

Writes results/contenders/evidence/tournament-v42-pool.json. This is separate
from six-map training and does not represent fresh17-legacy confirmation.
"""
import hashlib
import itertools
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(root / "bots/contenders/tools"))
import cohort
import scoreboard as sb

paths = [root / value for value in sys.argv[1:]]
groups, inputs = cohort.collect(paths)
pool = {path.stem for path in (root / "maps").glob("*.map26a")}
assert len(pool) == 40
packages = []
for group in groups:
    package = cohort.summarize(group, pool)
    for opponent, summary in package["opponents"].items():
        items = [item for key, item in group["cells"].items() if key[0] == opponent and cohort.terminal(item["row"])]
        summary["terminal_gem_margin"] = sb._values([value for item in items if (value := sb._margin(item)) is not None])
        summary["expected_pool_keys"] = 80
    package["pool_complete"] = (len(package["opponents"]) == 2 and not group["conflicts"] and
                                 all(value["finished"] == 80 and value["full_pool_both_sides"]
                                     for value in package["opponents"].values()))
    packages.append(package)

comparisons = []
for base, arm in itertools.permutations(groups, 2):
    b, a = base["identity"], arm["identity"]
    if b[0].split("@")[0] != a[0].split("@")[0] or b[2] != "base" or a[2] == "base" or b[3] != a[3]:
        continue
    result = cohort.matched(base, arm)
    if not result["n"]:
        comparisons.append(result)
        continue
    def details(left, right):
        part = cohort.matched(left, right)
        if part["n"]:
            pairs = [(left["cells"][key], right["cells"][key]) for key in left["cells"].keys() & right["cells"].keys()]
            pairs = [(l, r) for l, r in pairs if cohort.terminal(l["row"]) and cohort.terminal(r["row"])]
            part["paired_gem_margin_delta"] = sb._values([r - l for x, y in pairs
                                                           if (l := sb._margin(x)) is not None and (r := sb._margin(y)) is not None])
            part["paired_subject_terminal_metric_delta"] = {
                metric: sb._values([rv - lv for left_item, right_item in pairs
                                    if (lv := (left_item["row"].get(left_item["side"]) or {}).get(metric)) is not None
                                    and (rv := (right_item["row"].get(right_item["side"]) or {}).get(metric)) is not None])
                for metric in cohort.METRICS}
        return part
    result["opponent_details"] = {}
    result["paired_gem_margin_delta"] = details(base, arm).get("paired_gem_margin_delta")
    opponents = {key[0] for key in base["cells"]} | {key[0] for key in arm["cells"]}
    for opponent in sorted(opponents):
        left = dict(base, cells={key: item for key, item in base["cells"].items() if key[0] == opponent})
        right = dict(arm, cells={key: item for key, item in arm["cells"].items() if key[0] == opponent})
        result["opponent_details"][opponent] = details(left, right)
    bad = {key for group in (base, arm) for key, item in group["cells"].items()
           if cohort.health([item])["status"] != "clean" or cohort.health([item], True)["status"] != "clean"}
    clean_base = dict(base, cells={key: item for key, item in base["cells"].items() if key not in bad})
    clean_arm = dict(arm, cells={key: item for key, item in arm["cells"].items() if key not in bad})
    result["health_sensitivity"] = {"excluded_keys": sorted(bad, key=str), "paired": details(clean_base, clean_arm),
                                    "interpretation": "Secondary paired sensitivity only: exclude a key from both arms if either subject or opponent has unknown/failed recorded health. Raw results remain primary and unchanged."}
    comparisons.append(result)

output = {"schema": "policy-full-pool-scoreboard/1", "inputs": inputs,
          "measurement": {"artifact_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                          "path": str(Path(__file__).resolve()),
                          "cohort_tool_sha256": hashlib.sha256(Path(cohort.__file__).read_bytes()).hexdigest(),
                          "scoreboard_tool_sha256": hashlib.sha256(Path(sb.__file__).read_bytes()).hexdigest()},
          "scope": {"classification": "full40pool; overlaps six-map training; no fresh17legacy confirmation",
                    "maps": sorted(pool), "keys_per_opponent": 80, "declared_live_target": sb.ANCHOR,
                    "exact_opponent_sets_remain_separate": True},
          "packages": packages, "paired": comparisons,
          "limits": ["Actual saved rows only; source and opponent identities are unchanged.",
                     "Subject and opponent health are reported separately; zero clears is a stall proxy.",
                     "Terminal row gems are submitted/banked score, not lifetime collected or current carried gems; carried and spending totals are unknown here.",
                     "Terminal gold/clear/kill/death totals are not horizon-matched production rates; different game duration can change them.",
                     "A full-pool selection is not held-out confirmation and does not show victory against eventual current working main.",
                     "No games, builds, native decodes or online actions were performed."]}
target = root / "results/contenders/evidence/tournament-v42-pool.json"
target.write_text(json.dumps(output, indent=1) + "\n", encoding="utf-8", newline="\n")
for package in packages:
    print(json.dumps({"subject": package["subject"], "variant": package["variant"], "complete": package["pool_complete"],
                      "opponents": {key: {"results": value["results"], "margin": value["terminal_gem_margin"],
                                            "subject_health": value["subject_health"], "opponent_health": value["opponent_health"]}
                                    for key, value in package["opponents"].items()}}))
for comparison in comparisons:
    print(json.dumps({"candidate": comparison["b"], "n": comparison["n"], "gained": comparison.get("gained"),
                      "lost": comparison.get("lost"), "health_excluded": comparison.get("health_sensitivity", {}).get("excluded_keys"),
                      "clean_n": comparison.get("health_sensitivity", {}).get("paired", {}).get("n")}))
