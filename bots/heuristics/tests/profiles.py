"""Compare existing heuristic policies on a fixed behavioral corpus, without games.

Run with the Python shown by ``arena env``:
    python bots/heuristics/tests/profiles.py [bot ...] [--jobs 4] [--strict]

Policies are discovered from */src/policy.h. Each generated C++ fixture calls the
real heuristics::choose, bank, retreat, build, purchase and roster under arena's WASI runner.
The corpus covers fresh decisions, progress, stagnation, invalidation, partial
work and changing economic phases for all physical champion_type IDs 0..12.
This macro profile omits combat and movement flows, ability sequencing and target
micro. Identical fingerprints are a review flag that can be superseded by genuine
production-flow fixtures plus matched game traces. Differing fingerprints do not
prove strategic distinction. --strict makes the review flag a nonzero exit;
compiler/contract failures always fail.

Generated fixtures and the complete machine-readable report stay under .cache/.
No policies, suite sources, ladder records or external services are modified.
Roster fingerprints use append-only suite indexes, validated against common/src/draft.h;
report metadata maps these to SDK draft choices and separate physical champion IDs.
The optional purchase(physical_identity, monster, jungle) hook selects one of nine
pure equipment plans; policies without it retain int(build(monster, jungle)).
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import fnmatch
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import sys
import time

HERE = Path(__file__).resolve().parent
SUITE = HERE.parent
ROOT = SUITE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import CACHE, Slots, slot_count  # noqa: E402
from wasi import run_case  # noqa: E402

CORPUS = "heuristics-profiles-v3"
FIELDS = ("choice", "bank", "risk", "build", "roster", "gear")
NAMESPACE = re.compile(r"\bnamespace\s+(h12_[A-Za-z0-9_]+)\s*\{")
ROSTER_SCHEMA = "heuristics-draft-index-v1"
# (suite-index choice, SDK champion_draft ID, physical champion_type ID(s)).
# These spaces differ: suite index 6 is Heart, physical identity 6 is Prism.
ROSTER = (
    ("tavi", 10, (11,)), ("varg", 9, (10,)), ("vendetta", 2, (2,)),
    ("vesper_vale", 1, (1,)), ("marcella", 6, (7,)), ("prism", 5, (6,)),
    ("heart_of_ruin", 11, (12,)), ("willow", 7, (8,)), ("stellar", 4, (5,)),
    ("ryth", 0, (0,)), ("ren", 8, (9,)), ("cassi_and_issac", 3, (3, 4)),
)
GEAR = ("monster_strength", "jungle_health", "power_strength", "monster_focus",
        "jungle_focus", "power_focus", "monster_health", "jungle_strength", "power_health")


def roster_metadata() -> dict:
    """Check the production lookup without importing SDK runtime initialization into WASI."""
    header = SUITE / "common" / "src" / "draft.h"
    source = header.read_text(encoding="utf-8")
    choices = re.search(r"\bchoices\s*\{([\s\S]*?)\};", source)
    indexes = re.search(r"\benum\s+Index\s*\{([\s\S]*?)\};", source)
    names = re.findall(r"cambc::champion_draft::(\w+)", choices.group(1)) if choices else []
    values = re.findall(r"\b(\w+)\s*=\s*(\d+)", indexes.group(1)) if indexes else []
    if names != [name for name, _, _ in ROSTER] or values != [(name, str(i)) for i, (name, _, _) in enumerate(ROSTER)]:
        raise ValueError("common/src/draft.h differs from the canonical append-only roster schema; update profile metadata explicitly")
    return {"version": ROSTER_SCHEMA, "source": header.relative_to(ROOT).as_posix(),
            "source_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
            "choices": [{"index": i, "choice": name, "sdk_draft_id": draft,
                         "physical_champion_type_ids": list(physical)}
                        for i, (name, draft, physical) in enumerate(ROSTER)]}


def purchase_metadata() -> dict:
    header = SUITE / "common" / "src" / "equipment.h"
    source = header.read_text(encoding="utf-8")
    enum = re.search(r"\benum\s+class\s+Purchase\s*\{([\s\S]*?)\};", source)
    values = re.findall(r"\b(\w+)\s*=\s*(\d+)", enum.group(1)) if enum else []
    if values != [(name, str(i)) for i, name in enumerate(GEAR)]:
        raise ValueError("common/src/equipment.h differs from the canonical purchase labels")
    return {"version": "heuristics-purchase-v1", "source": header.relative_to(ROOT).as_posix(),
            "source_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
            "choices": [{"id": i, "plan": name} for i, name in enumerate(GEAR)],
            "hook": "purchase(physical_identity, monster, jungle)",
            "fallback": "int(build(monster, jungle))",
            "corpus_order": "physical identity 0..12, then monster 0/1/4/5/6, then jungle 0/1/4/5/6"}

CPP = r'''
@CONFIG@
#undef HEURISTICS_ENABLED
#define HEURISTICS_ENABLED 1
#undef HEURISTICS_TRACE
#define HEURISTICS_TRACE 0
#include "@POLICY@"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using Policy = @NAMESPACE@::Policy;
using heuristics::State;
using heuristics::Job;

static void require(bool ok,const char*message) {
    if(!ok){std::fprintf(stderr,"profile contract: %s\n",message);std::exit(1);}
}
static char code(int value) {
    require(value>=-1&&value<8,"selector returned an out-of-range ID");
    return char('A'+value+1);
}
static std::uint32_t random_word(std::uint32_t&seed) {
    seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;
}
static State context(int k) {
    State s;
    s.round=std::array{12,180,700,1200,1700,1930,1980,500}[k%8];
    // Physical champion_type IDs, including both separate twin actors.
    s.limit=2000;s.identity=k%13;
    s.maximum=1000;s.hp=std::array{1000.f,820.f,650.f,480.f}[k%4];
    s.bag=std::array{0,9,10,11,19,20,24,45}[k%8];
    s.altar=std::array{1,3,8,16}[k%4];s.gold=(k%7)*60;
    s.monster=k%3==0?0:5;s.jungle=k%3==2?5:0;
    s.mature=s.monster>=5&&s.jungle>=5;
    return s;
}
static Job job(const State&s,int id,int travel,int gems,int gold,float work,
               float incoming,int altar=5) {
    Job j;
    j.id=id;j.travel=travel;j.gems=gems;j.gold=gold;j.work=work;
    j.incoming=incoming;j.loss=incoming*(work+2);j.altar=altar;
    j.original_hp=work*100;j.remaining_hp=j.original_hp;
    j.home=travel+4;j.known=true;
    j.affordable=s.hp-j.loss>=s.maximum*.25f;
    return j;
}
static void finish(const State&s,std::vector<Job>&jobs) {
    for(auto&j:jobs) {
        if(j.ready){j.work=0;j.incoming=0;j.loss=0;j.remaining_hp=0;j.known=true;}
        j.affordable=j.ready||s.hp-j.loss>=s.maximum*.25f;
    }
}
// Each menu changes one meaningful conflict against a stable set of alternatives.
static std::vector<Job> menu(const State&s,int axis) {
    std::vector<Job> v{job(s,0,5,12,60,6,8,8),job(s,1,12,30,100,10,12,2),
        job(s,2,3,0,100,4,0,10),job(s,3,18,45,170,12,18,12)};
    switch(axis%16) {
    case 0: v[0].gold=150;v[0].gems=0;v[1].gold=0;break;
    case 1: v[1].travel=3;v[1].work=18;v[0].work=2;break;
    case 2: v[0].incoming=60;v[0].loss=480;v[1].loss=70;break;
    case 3: v[0].known=false;v[0].age=70;v[1].age=0;break;
    case 4: v[1].ready=true;v[1].travel=14;break;
    case 5: v[0].allies=2;v[2].allies=1;break;
    case 6: v[1].enemy_eta=2;v[0].enemy_eta=30;break;
    case 7: v[0].original_hp=2200;v[0].remaining_hp=200;v[0].work=2;break;
    case 8: v[1].home=1;v[0].home=25;break;
    case 9: v[1].committed=true;v[0].travel=4;break;
    case 10: v[0].altar=25;v[1].altar=1;break;
    case 11: v[0].gems=1;v[1].gems=9;v[2].gems=10;v[3].gems=11;break;
    case 12: for(auto&j:v){j.loss=900;j.incoming=90;j.committed=j.id==1;}break;
    case 13: v[0].ready=true;v[0].allies=2;v[1].ready=true;break;
    case 14: v[0].gold=29;v[1].gold=30;v[2].gold=31;break;
    case 15: v[0].gems=7;v[1].gems=8;v[2].gems=9;break;
    }
    finish(s,v);return v;
}
static int choose(Policy&p,const State&s,std::vector<Job>&jobs) {
    const int result=heuristics::choose(p,s,jobs);
    require(result==-1||std::any_of(jobs.begin(),jobs.end(),[&](const Job&j){return j.id==result;}),
        "selector returned an ID absent from its menu");
    return result;
}
// This optional pure hook is also the contract used by future executors. Legacy
// build stages retain their exact meaning as the first three purchase labels.
template<class P> static int purchase(P&p,int identity,unsigned monster,unsigned jungle) {
    if constexpr(requires{p.purchase(identity,monster,jungle);})
        return int(p.purchase(identity,monster,jungle));
    else return int(p.build(monster,jungle));
}
int main() {
    std::string choices,banks,risks,builds,roster,gear;
    // Fresh policies: 16 conflicts x all 13 champion contexts x both menu orders.
    for(int axis=0;axis<16;++axis)for(int k=0;k<13;++k)for(int reverse=0;reverse<2;++reverse) {
        auto s=context(k);auto jobs=menu(s,axis);
        if(reverse)std::reverse(jobs.begin(),jobs.end());
        Policy p;choices+=code(choose(p,s,jobs));
    }
    // A fixed xorshift seed broadens coverage without using game/opponent data.
    std::uint32_t seed=0x12c0ffeeU;
    for(int n=0;n<192;++n) {
        auto s=context(n);s.round=int(random_word(seed)%1999)+1;
        s.hp=float(300+random_word(seed)%701);s.bag=int(random_word(seed)%61);
        std::vector<Job> jobs;
        for(int i=0;i<6;++i) {
            const int travel=int(random_word(seed)%36);
            const int gems=int(random_word(seed)%61);
            const int gold=int(random_word(seed)%11)*30;
            const float work=float(1+random_word(seed)%24);
            const float incoming=float(random_word(seed)%6)*9;
            const int altar=int(random_word(seed)%25);
            auto j=job(s,i,travel,gems,gold,work,incoming,altar);
            j.loss=j.incoming*(j.work+2);j.allies=int(random_word(seed)%3);
            j.known=random_word(seed)%3!=0;j.age=j.known?0:int(random_word(seed)%81);
            j.enemy_eta=random_word(seed)%3==0?int(random_word(seed)%30):10000;
            j.ready=random_word(seed)%13==0;j.committed=i==n%6;
            j.home=int(random_word(seed)%41);
            if(random_word(seed)%3==0){j.original_hp*=3;j.remaining_hp/=2;}
            jobs.push_back(j);
        }
        finish(s,jobs);Policy p;choices+=code(choose(p,s,jobs));
    }
    // One persistent episode per physical champion, rotating through six patterns:
    // progress, stagnation, blocked travel, partial siege, growing bag/capital,
    // and invalidated observation/deadline. Never reset p mid-episode.
    for(int episode=0;episode<13;++episode) {
        Policy p;int previous=-1;const int mode=episode%6;
        for(int step=0;step<24;++step) {
            State s=context(episode);s.round=100+step;
            auto jobs=menu(s,mode+7);
            for(auto&j:jobs)j.committed=j.id==previous;
            if(mode==0){jobs[0].travel=std::max(0,14-step);jobs[0].remaining_hp=std::max(50.f,1600.f-step*75.f);}
            if(mode==1&&step>=9){jobs[1].gems=50;jobs[1].travel=1;jobs[1].work=1;}
            if(mode==2){jobs[0].travel=10+step%2;jobs[0].remaining_hp=1400;}
            if(mode==3){for(auto&j:jobs){j.loss=900;j.incoming=90;}s.hp=step<12?900.f:650.f;}
            if(mode==4){s.bag=step*3;s.gold=step*30;s.mature=step>=12;s.monster=step<6?step:5;s.jungle=step<12?0:5;}
            if(mode==5){s.round=1960+step;jobs[0].known=step<8;jobs[0].age=step<8?0:step-7;}
            if(step==15&&previous>=0)jobs.erase(std::remove_if(jobs.begin(),jobs.end(),[&](const Job&j){return j.id==previous;}),jobs.end());
            finish(s,jobs);previous=choose(p,s,jobs);choices+=code(previous);
            banks+=p.bank(s)?'1':'0';risks+=p.retreat(s.hp,s.maximum,30,s.bag)?'1':'0';
        }
    }
    // Independent financial and survival boundaries, including tribute tick steps.
    int financial_context=0;
    for(int round:std::array{20,700,1900,1980})for(int bag:std::array{0,1,9,10,11,19,20,24,30,60})
        for(int altar:std::array{1,3,8,24})for(float hp:std::array{1000.f,450.f}) {
            State s=context(financial_context++);s.round=round;s.bag=bag;s.altar=altar;s.hp=hp;
            Policy p;banks+=p.bank(s)?'1':'0';
        }
    for(float hp:std::array{180.f,300.f,350.f,450.f,650.f,900.f,1000.f})
        for(float incoming:std::array{0.f,20.f,80.f,180.f,400.f})for(int bag:std::array{0,10,20,60}) {
            Policy p;risks+=p.retreat(hp,1000,incoming,bag)?'1':'0';
        }
    for(unsigned monster:std::array{0U,1U,4U,5U,6U})for(unsigned jungle:std::array{0U,1U,4U,5U,6U}) {
        Policy p;const int stage=int(p.build(monster,jungle));
        require(stage>=0&&stage<=2,"build returned an unknown stage");builds+=char('0'+stage);
    }
    for(auto pick:Policy::roster){const int n=int(pick);require(n>=0&&n<@ROSTER_SIZE@,"roster pick outside canonical suite-index mapping");roster+=char('A'+n);}
    for(int identity=0;identity<13;++identity)
        for(unsigned monster:std::array{0U,1U,4U,5U,6U})for(unsigned jungle:std::array{0U,1U,4U,5U,6U}) {
            Policy p;const int plan=purchase(p,identity,monster,jungle);
            require(plan>=0&&plan<9,"purchase returned an unknown equipment plan");gear+=char('0'+plan);
        }
    // Each separate fixture stays below run_wasm's 4000-character output cap.
    require(choices.size()+banks.size()+risks.size()+builds.size()+roster.size()+gear.size()<3500,"profile output exceeds runner cap");
    std::printf("{\"choice\":\"%s\",\"bank\":\"%s\",\"risk\":\"%s\",\"build\":\"%s\",\"roster\":\"%s\",\"gear\":\"%s\"}\n",
        choices.c_str(),banks.c_str(),risks.c_str(),builds.c_str(),roster.c_str(),gear.c_str());
}
'''


def discover(patterns: list[str], pending: list[str] | None = None) -> list[tuple[str, Path, str]]:
    found = []
    for header in sorted(SUITE.glob("*/src/policy.h")):
        bot = header.parent.parent.name
        if patterns and not any(fnmatch.fnmatchcase(bot, p.removeprefix("heuristics/")) for p in patterns):
            continue
        source = header.read_text(encoding="utf-8")
        namespaces = sorted(set(NAMESPACE.findall(source)))
        if len(namespaces) != 1 or not re.search(r"\b(?:struct|class)\s+Policy\b", source):
            if pending is not None:
                pending.append(f"{bot}: no complete h12_* Policy declaration yet")
                continue
            raise ValueError(f"{header.relative_to(ROOT)}: expected one h12_* namespace and Policy")
        found.append((bot, header, namespaces[0]))
    if not found:
        raise ValueError("no matching heuristic policies")
    return found


def profile(item: tuple[str, Path, str], scratch: Path) -> tuple[str, dict | None, str]:
    bot, header, namespace = item
    fixture = scratch / f"{bot}.cpp"
    config = header.with_name("config.h")
    config_include = f'#include "{config.as_posix()}"' if config.is_file() else ""
    source = (CPP.replace("@CONFIG@", config_include).replace("@POLICY@", header.as_posix())
              .replace("@NAMESPACE@", namespace).replace("@ROSTER_SIZE@", str(len(ROSTER))))
    fixture.write_text(source, encoding="utf-8", newline="\n")
    with Slots(1, quiet=True):
        ok, log = run_case(f"h12-profile-{bot}", fixture, out_dir=scratch)
    if not ok:
        return bot, None, log
    try:
        row = json.loads(log.splitlines()[-1])
        if set(row) != set(FIELDS) or any(not isinstance(row[k], str) or not row[k] for k in FIELDS):
            raise ValueError("missing or malformed fingerprint fields")
    except (ValueError, IndexError) as exc:
        return bot, None, f"invalid profile output: {exc}\n{log[-1200:]}"
    row["header"] = header.relative_to(ROOT).as_posix()
    row["policy_sha256"] = hashlib.sha256(header.read_bytes()).hexdigest()
    row["fingerprint"] = hashlib.sha256(json.dumps([row[k] for k in FIELDS]).encode()).hexdigest()[:12]
    return bot, row, ""


def differences(a: dict, b: dict, field: str) -> tuple[int, int]:
    x, y = a[field], b[field]
    if len(x) != len(y):
        raise ValueError(f"incompatible {field} corpus lengths: {len(x)} vs {len(y)}")
    return sum(c != d for c, d in zip(x, y)), len(x)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("bots", nargs="*", help="optional bot names or globs")
    parser.add_argument("--jobs", type=int, default=4, help="concurrent WASI fixtures, each using one shared slot")
    parser.add_argument("--pairs", type=int, default=24, help="closest pairs to print (0..40; full matrix is saved)")
    parser.add_argument("--strict", action="store_true", help="exit 2 when profiles are identical on this corpus")
    args = parser.parse_args(argv)
    if args.jobs < 1 or not 0 <= args.pairs <= 40:
        parser.error("--jobs must be positive; --pairs must be between 0 and 40")
    try:
        pending: list[str] = []
        policies = discover(args.bots, pending)
        roster = roster_metadata()
        equipment = purchase_metadata()
    except ValueError as exc:
        print(f"FAIL {exc}")
        return 1
    scratch = CACHE / "heuristics-profiles" / f"{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}-{time.time_ns()%1000000000:09d}"
    scratch.mkdir(parents=True, exist_ok=False)
    profiles, failures = {}, []
    with ThreadPoolExecutor(max_workers=min(args.jobs, slot_count(), len(policies))) as pool:
        for bot, row, error in pool.map(lambda item: profile(item, scratch), policies):
            if row is None:
                failures.append(bot)
                print(f"FAIL {bot}: " + " | ".join(error.strip().splitlines()[-5:]))
            else:
                profiles[bot] = row
    pairs = []
    for left, right in itertools.combinations(sorted(profiles), 2):
        metrics = {field: differences(profiles[left], profiles[right], field) for field in FIELDS}
        pairs.append({"a": left, "b": right, "differences": metrics,
                      "identical": all(n == 0 for n, total in metrics.values())})
    pairs.sort(key=lambda p: (sum(n / total for n, total in p["differences"].values()), p["a"], p["b"]))
    groups = {}
    for bot, row in profiles.items():
        groups.setdefault(tuple(row[k] for k in FIELDS), []).append(bot)
    identical_groups = [bots for bots in groups.values() if len(bots) > 1]
    report = {"corpus": CORPUS, "scope": "fixed coarse states; no games or strategic-distinctness certification",
              "champion_type_ids": list(range(13)),
              "roster_index_schema": roster,
              "purchase_schema": equipment,
              "omitted_flows": ["combat", "movement", "ability sequencing", "target micro"],
              "identical_profile_interpretation": "review flag; genuine production-flow fixtures plus matched game traces can supersede it",
              "generated_at_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
              "segments": {"choice": {"fresh_conflicts": [0, 416], "fixed_seed": [416, 608],
                  "progress_stagnation_invalidation_episodes": [608, 920]},
                  "bank": {"persistent_episodes": [0, 312], "financial_boundaries": [312, 632]},
                  "risk": {"persistent_episodes": [0, 312], "survival_boundaries": [312, 452]},
                  "gear": {"all_identities_and_shard_counts": [0, 325]}},
              "profiles": profiles, "pairs": pairs, "identical_groups": identical_groups,
              "failures": failures, "pending": pending}
    report_path = scratch / "report.json"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    print(f"{CORPUS}: {len(profiles)}/{len(policies)} policies; champion_type 0..12; no games")
    for item in pending:
        print(f"pending {item}")
    for bot, row in profiles.items():
        counts = "/".join(str(len(row[k])) for k in FIELDS)
        print(f"  {bot:<24} {row['fingerprint']}  choice/bank/risk/build/roster/gear={counts}")
    if pairs:
        print("closest pairs: disagreement percentages (choice / bank / risk / build / roster / gear)")
        for pair in pairs[:args.pairs]:
            values = " / ".join(f"{100 * n / total:5.1f}" for n, total in pair["differences"].values())
            flag = "  IDENTICAL ON CORPUS" if pair["identical"] else ""
            print(f"  {pair['a']} : {pair['b']}  {values}{flag}")
        if len(pairs) > args.pairs:
            print(f"  {len(pairs) - args.pairs} additional pairs in report.json")
    else:
        print("pairwise comparison needs at least two policies")
    for bots in identical_groups:
        print("FLAG identical on corpus: " + ", ".join(bots))
    print("Macro profile omits combat/movement flows; identical profiles are review flags, not proof of equivalence.")
    print("Genuine production-flow fixtures plus matched traces can supersede a flag; differing profiles do not prove distinct strategies.")
    print(f"report {report_path.relative_to(ROOT).as_posix()}")
    return 1 if failures else (2 if args.strict and identical_groups else 0)


if __name__ == "__main__":
    sys.exit(main())
