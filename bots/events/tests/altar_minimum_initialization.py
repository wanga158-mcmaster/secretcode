"""Exercise saved production altar-cache initialization callbacks under WASI.

The four changing callbacks are extracted from runtime.h, not rewritten here.
The production initialization driver controls every yield. SDK doubles charge
deliberately chosen costs to expose boundaries; they do not model native fuel.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import CLANG, INCLUDE, NODE, Slots  # noqa: E402
from wasi import compile_fixture, run_wasm  # noqa: E402

SCRATCH = ROOT / ".cache/arena/orchestration/events-altar-cache/initialization"
EVIDENCE = ROOT / "results/population/events-altar-cache/initialization-fixture.json"
RUNTIME = ROOT / "bots/events/common/src/runtime.h"
HEADER = ROOT / "bots/events/common/src/altar_minimum.h"
DRIVER = ROOT / "bots/events/common/src/initialization.h"
EVENTS = ROOT / "bots/events/common/src/events.h"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def balanced(text: str, start: int) -> int:
    """Return the end of one C++ bracketed expression, ignoring comments/strings."""
    pairs = {"{": "}", "(": ")", "[": "]"}
    if text[start] not in pairs:
        raise ValueError("expected opening delimiter")
    stack, quote, line_comment, block_comment = [], None, False, False
    i = start
    while i < len(text):
        ch, next_ch = text[i], text[i + 1:i + 2]
        if line_comment:
            if ch == "\n":
                line_comment = False
        elif block_comment:
            if ch == "*" and next_ch == "/":
                block_comment = False
                i += 1
        elif quote:
            if ch == "\\":
                i += 1
            elif ch == quote:
                quote = None
        elif ch == "/" and next_ch == "/":
            line_comment = True
            i += 1
        elif ch == "/" and next_ch == "*":
            block_comment = True
            i += 1
        elif ch in "\"'":
            quote = ch
        elif ch in pairs:
            stack.append(pairs[ch])
        elif ch in "})]":
            if not stack or ch != stack.pop():
                raise ValueError("mismatched C++ delimiters")
            if not stack:
                return i + 1
        i += 1
    raise ValueError("unterminated C++ expression")


def select_cache_branch(text: str) -> str:
    """Select only the new cache's enabled preprocessor branches."""
    output, active, stack = [], True, []
    for line in text.splitlines(keepends=True):
        stripped = line.strip()
        if stripped == "#if P11_CACHE_ALTAR_MINIMUM":
            stack.append((active, True))
        elif stripped == "#else":
            if not stack:
                raise ValueError("unexpected preprocessor else")
            parent, _ = stack[-1]
            stack[-1] = parent, False
        elif stripped == "#endif":
            if not stack:
                raise ValueError("unexpected preprocessor endif")
            stack.pop()
        elif stripped.startswith("#"):
            raise ValueError("unreviewed callback preprocessor directive: " + stripped)
        elif active:
            output.append(line)
        active = all(parent and enabled for parent, enabled in stack)
    if stack:
        raise ValueError("unterminated cache conditional")
    return "".join(output)


def extract(source: str) -> tuple[dict[str, str], str, dict[str, str]]:
    marker = "auto work = p11::initialization::Work{"
    start = source.index(marker) + len(marker) - 1
    finish = balanced(source, start)
    body = select_cache_branch(source[start + 1:finish - 1])
    fragments, i = [], 0
    while i < len(body):
        if body[i].isspace() or body[i] == ",":
            i += 1
            continue
        if body[i:i + 2] not in ("[]", "[&"):
            raise ValueError("expected an initializer callback: " + body[i:i + 50])
        fragment_start = i
        capture_end = balanced(body, i)
        lambda_start = body.index("{", capture_end)
        i = balanced(body, lambda_start)
        fragments.append(body[fragment_start:i])
    if len(fragments) != 11:
        raise ValueError(f"production Work changed: expected 11 callbacks, found {len(fragments)}")
    names = {4: "group_pending", 5: "group_open", 6: "open_group", 7: "member_pending",
             8: "add_member", 9: "close_group"}
    callbacks = {name: fragments[index] for index, name in names.items()}
    if "initialization_members_.front().spawn_position()" not in callbacks["open_group"]:
        raise ValueError("production native first-member anchor changed")
    if "altar_minimum.begin(" not in callbacks["open_group"]:
        raise ValueError("production open callback does not begin the altar cache")
    if "altar_minimum.ready()" not in callbacks["member_pending"]:
        raise ValueError("production pending callback does not wait for the altar cache")
    if "minimum.append(distance(initializing_group_->anchor,altars_[minimum.cursor()]))" not in callbacks["add_member"]:
        raise ValueError("production member callback changed the scalar altar step")
    group_start = source.index("struct Group {")
    group_brace = source.index("{", group_start)
    group = source[group_start:balanced(source, group_brace)] + ";"
    getters = {}
    for variable in ("e", "entry"):
        prefix = f"c.altar={variable}.altar_minimum.get("
        get_start = source.index(prefix)
        paren = get_start + len(prefix) - 1
        end = balanced(source, paren)
        if source[end] != ";":
            raise ValueError("getter statement terminator changed")
        getters[variable] = source[get_start:end + 1]
    return callbacks, group, getters


HARNESS = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#define P11_CACHE_ALTAR_MINIMUM 1
#define P11_CACHE_PUBLIC_WORK 0
#include "@EVENTS@"
#include "@HEADER@"
#include "@DRIVER@"

namespace fixture {
inline int fuel=0,starts=0,finishes=0,opens=0,closes=0;
inline std::vector<int> member_reads;
inline std::vector<std::array<int,4>> altar_reads;
void spend(int amount){assert(fuel>=amount);fuel-=amount;}
void reset(){fuel=0;starts=finishes=opens=closes=0;member_reads.clear();altar_reads.clear();}
}
namespace cambc {
struct vec2 {int x=0,y=0;bool operator==(const vec2&)const=default;};
struct camp_stats {float base_health=0,base_strength=0;};
struct camp {
    int id=0;vec2 tile;camp_stats stats;bool monolith=false;
    vec2 spawn_position()const{return tile;}
    camp_stats base_stats()const{fixture::spend(15000);fixture::member_reads.push_back(id);return stats;}
    bool is_monolith()const{return monolith;}
};
namespace constants {inline constexpr int submit_gems_range=1;}
namespace map {
inline int width(){return 32;}
struct camp_group {
    std::vector<camp> members;unsigned gems=1;
    std::span<const camp> camps()const{return members;}
    int spawn_tiles()const{fixture::spend(16000);++fixture::opens;return 1;}
    unsigned gem_value()const{fixture::spend(5000);++fixture::closes;return gems;}
};
}
}
using cambc::vec2;
using cambc::camp;
using bitboard=int;
int distance(vec2 a,vec2 b){
    fixture::spend(15000);fixture::altar_reads.push_back({a.x,a.y,b.x,b.y});
    return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));
}
@GROUP@
struct Policy {static constexpr bool bounded_scan=true;};
struct Harness {
    std::vector<Group> groups_;
    std::vector<int> group_at_tile_=std::vector<int>(1024,-1);
    std::vector<vec2> altars_{{20,20},{9,4},{1,2}};
    std::vector<cambc::map::camp_group> initialization_groups_{{
        {{0,{9,1},{2,3},false},{1,{1,1},{5,7},false}},1},
        {{{2,{4,8},{11,13},false}},1}};
    bool initialized_=false,initialization_started_=false,has_non_monolith_gems_=false;
    std::size_t initialization_group_cursor_=0,initialization_member_cursor_=0;
    std::vector<camp> initialization_members_;
    std::optional<Group> initializing_group_;
    std::vector<std::vector<p11::Monster>> models;
    auto& public_models(){return models;}
    bool resume(int fuel){
        fixture::fuel=fuel;
        auto work=p11::initialization::Work{
            []{return fixture::fuel;},[&]{return initialized_;},[&]{return initialization_started_;},
            [&]{fixture::spend(40000);++fixture::starts;models.clear();initialization_started_=true;},
            @GROUP_PENDING@,
            @GROUP_OPEN@,
            @OPEN_GROUP@,
            @MEMBER_PENDING@,
            @ADD_MEMBER@,
            @CLOSE_GROUP@,
            [&]{fixture::spend(1000);++fixture::finishes;initialized_=true;}
        };
        return p11::initialization::resume(work);
    }
    int preview_get(const Group&e){p11::Candidate c;@PREVIEW_GET@ return c.altar;}
    int detail_get(const Group&entry){p11::Candidate c;@DETAIL_GET@ return c.altar;}
};

int checks=0;
void verified(bool value){assert(value);++checks;}
void expect_real_writes(const Harness&h,std::size_t count){
    verified(fixture::member_reads.size()==count);
    std::size_t rows=0;for(const auto&row:h.models)rows+=row.size();
    verified(rows==count);
}
void finish_group_one_step_at_a_time(Harness&h,std::size_t group,std::size_t real_count){
    for(std::size_t i=0;i<h.initialization_groups_[group].members.size();++i){
        verified(!h.resume(14999));
        verified(!h.resume(15000));
        verified(h.groups_.size()==group&&h.initializing_group_.has_value());
    }
    expect_real_writes(h,real_count);
    verified(!h.initializing_group_->altar_minimum.ready());
    verified(h.initializing_group_->altar_minimum.cursor()==0);
    const auto real_order=fixture::member_reads;
    const auto row_count=h.models.size();
    for(std::size_t cursor=0;cursor<h.altars_.size();++cursor){
        const auto calls=fixture::altar_reads.size();
        verified(!h.resume(14999));
        verified(h.initializing_group_->altar_minimum.cursor()==cursor);
        verified(fixture::altar_reads.size()==calls);
        verified(!h.resume(15000));
        verified(h.initializing_group_->altar_minimum.cursor()==cursor+1);
        verified(fixture::altar_reads.size()==calls+1);
        verified(h.initialization_member_cursor_==h.initialization_members_.size());
        verified(h.groups_.size()==group&&h.models.size()==row_count);
        verified(fixture::member_reads==real_order);
        expect_real_writes(h,real_count);
    }
    verified(h.initializing_group_->altar_minimum.ready());
    verified(!h.resume(4999));
    verified(h.groups_.size()==group&&h.initializing_group_.has_value());
    verified(!h.resume(5000));
    verified(h.groups_.size()==group+1&&!h.initializing_group_);
}

int main(){
    fixture::reset();Harness phased;
    verified(!phased.resume(59999)&&!phased.initialization_started_);
    verified(!phased.resume(60000)&&phased.initialization_started_);
    verified(!phased.resume(30000)&&phased.initializing_group_.has_value());
    verified(phased.initializing_group_->anchor==vec2{9,1});
    verified(phased.initializing_group_->anchor!=vec2{1,1});
    verified(phased.models.size()==1&&phased.models[0].empty());
    finish_group_one_step_at_a_time(phased,0,2);
    fixture::fuel=0;
    verified(phased.groups_[0].altar_minimum.get([]{assert(false);return -1;})==2);
    verified(phased.groups_[0].altar_minimum.cursor()==3);
    verified(!phased.groups_[0].altar_minimum.append(0));
    verified(phased.groups_[0].altar_minimum.cursor()==3);
    verified(!phased.resume(30000)&&phased.initializing_group_.has_value());
    finish_group_one_step_at_a_time(phased,1,3);
    verified(!phased.initialized_&&!phased.resume(24999));
    verified(phased.resume(25000)&&phased.initialized_);
    verified(fixture::starts==1&&fixture::opens==2&&fixture::closes==2&&fixture::finishes==1);
    verified(fixture::member_reads==std::vector<int>({0,1,2}));
    verified(fixture::altar_reads==std::vector<std::array<int,4>>({
        {9,1,20,20},{9,1,9,4},{9,1,1,2},{4,8,20,20},{4,8,9,4},{4,8,1,2}}));
    const auto phased_reads=fixture::member_reads;
    const auto phased_altars=fixture::altar_reads;
    verified(phased.group_at_tile_[1*32+9]==0&&phased.group_at_tile_[1*32+1]==0);
    verified(phased.group_at_tile_[8*32+4]==1);
    verified(phased.groups_[0].health==7&&phased.groups_[0].incoming==10);
    verified(phased.groups_[1].health==11&&phased.groups_[1].incoming==13);
    verified(phased.models[0][0].hp==2&&phased.models[0][1].hp==5&&phased.models[1][0].hp==11);
    verified(phased.models[0][0].strength==3&&phased.models[0][1].strength==7&&phased.models[1][0].strength==13);
    verified(phased.models[0][0].approach==0&&phased.models[0][1].approach==0&&phased.models[1][0].approach==0);
    verified(phased.has_non_monolith_gems_&&!phased.groups_[0].monolith&&!phased.groups_[1].monolith);
    verified(phased.resume(0)&&fixture::fuel==0);
    verified(fixture::member_reads==phased_reads&&fixture::altar_reads==phased_altars);

    fixture::reset();Harness whole;verified(whole.resume(1000000));
    verified(whole.group_at_tile_==phased.group_at_tile_);
    verified(whole.models.size()==phased.models.size());
    for(std::size_t g=0;g<whole.groups_.size();++g){
        verified(whole.groups_[g].anchor==phased.groups_[g].anchor);
        verified(whole.groups_[g].health==phased.groups_[g].health);
        verified(whole.groups_[g].incoming==phased.groups_[g].incoming);
        verified(whole.models[g].size()==phased.models[g].size());
        verified(whole.groups_[g].altar_minimum.get([]{assert(false);return -1;})
            ==phased.groups_[g].altar_minimum.get([]{assert(false);return -1;}));
    }
    verified(fixture::member_reads==phased_reads&&fixture::altar_reads==phased_altars);

    // Actual preview and detailed getter statements must use the exact old
    // loop on legacy/unfinished objects, and neither getter may publish cache.
    fixture::reset();Harness legacy;fixture::fuel=1000000;
    Group entry{legacy.initialization_groups_[0],0,{9,1}};
    verified(!entry.altar_minimum.ready()&&entry.altar_minimum.cursor()==0);
    verified(!entry.altar_minimum.append(0));
    verified(legacy.preview_get(entry)==2&&legacy.detail_get(entry)==2);
    verified(!entry.altar_minimum.ready()&&entry.altar_minimum.cursor()==0);
    verified(fixture::altar_reads.size()==6);
    entry.altar_minimum.begin(1,3);verified(entry.altar_minimum.append(19));
    verified(!entry.altar_minimum.ready()&&entry.altar_minimum.cursor()==1);
    verified(legacy.preview_get(entry)==2&&legacy.detail_get(entry)==2);
    verified(!entry.altar_minimum.ready()&&entry.altar_minimum.cursor()==1);
    verified(fixture::altar_reads.size()==12);
    // Geometry-first would give zero, while the actual native first member
    // gives two. The production callbacks and getters must retain the latter.
    Group geometry_first{legacy.initialization_groups_[0],0,{1,1}};
    verified(legacy.preview_get(geometry_first)==0);

    fixture::reset();Harness empty;empty.altars_.clear();
    empty.initialization_groups_.resize(1);
    verified(!empty.resume(60000));verified(!empty.resume(30000));
    verified(empty.initializing_group_->altar_minimum.ready());
    verified(empty.groups_.empty()&&empty.models[0].empty());
    verified(!empty.resume(15000)&&empty.groups_.empty());
    verified(!empty.resume(15000)&&empty.groups_.empty());
    verified(empty.initializing_group_->altar_minimum.cursor()==0);
    verified(!empty.resume(4999)&&empty.groups_.empty());
    verified(!empty.resume(5000)&&empty.groups_.size()==1);
    verified(empty.resume(25000));
    verified(fixture::altar_reads.empty()&&fixture::member_reads==std::vector<int>({0,1}));
    fixture::fuel=0;
    verified(empty.preview_get(empty.groups_[0])==10000&&empty.detail_get(empty.groups_[0])==10000);
    verified(empty.groups_[0].altar_minimum.ready()&&empty.groups_[0].altar_minimum.cursor()==0);
    verified(!empty.groups_[0].altar_minimum.append(5));
    verified(empty.groups_[0].altar_minimum.get([]{assert(false);return -1;})==10000);
    std::printf("altar initialization checks=%d native_anchor=2 geometry_anchor=0 sentinel=10000\n",checks);
}
'''


def run() -> tuple[bool, dict]:
    workdir = SCRATCH / f"run-{os.getpid()}"
    pinned = workdir / "pinned"
    workdir.mkdir(parents=True, exist_ok=True)
    source = RUNTIME.read_bytes()
    callbacks, group, getters = extract(source.decode("utf-8"))
    saved: dict[Path, bytes] = {RUNTIME: source}
    stack = [HEADER, DRIVER, EVENTS]
    while stack:
        path = stack.pop().resolve()
        if path in saved:
            continue
        data = path.read_bytes()
        saved[path] = data
        for include in INCLUDE.findall(data.decode("utf-8")):
            child = (path.parent / include).resolve()
            if child.is_file() and child.is_relative_to(ROOT):
                stack.append(child)
    for path, data in saved.items():
        destination = pinned / path.relative_to(ROOT)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    values = {name.upper(): text for name, text in callbacks.items()}
    values.update({"GROUP": group, "PREVIEW_GET": getters["e"], "DETAIL_GET": getters["entry"]})
    values.update({name: (pinned / path.relative_to(ROOT)).as_posix()
                   for name, path in (("HEADER", HEADER), ("DRIVER", DRIVER), ("EVENTS", EVENTS))})
    generated = HARNESS
    for name, value in values.items():
        generated = generated.replace("@" + name + "@", value)
    if re.search(r"@[A-Z_]+@", generated):
        raise ValueError("unfilled fixture marker")
    cpp = workdir / "initialization.cpp"
    cpp.write_text(generated, encoding="utf-8", newline="\n")
    wasm = workdir / "initialization.wasm"
    with Slots(1, quiet=True):
        compiled, compile_log = compile_fixture(cpp, wasm)
        passed, output = run_wasm(wasm) if compiled else (False, compile_log)
    (workdir / "output.log").write_text(output + "\n", encoding="utf-8", newline="\n")
    report = {
        "schema": "events-altar-minimum-initialization-fixture-v1",
        "passed": passed, "compiled": compiled, "admission": False,
        "production_driver": "p11::initialization::resume",
        "callbacks": {name: digest(text.encode()) for name, text in callbacks.items()},
        "getter_statements": {name: digest(text.encode()) for name, text in getters.items()},
        "group_definition": digest(group.encode()),
        "source_hashes": {path.relative_to(ROOT).as_posix(): digest(data) for path, data in saved.items()},
        "fixture_source_hash": digest(Path(__file__).read_bytes()),
        "generated_source": cpp.relative_to(ROOT).as_posix(), "generated_source_hash": digest(cpp.read_bytes()),
        "wasm": wasm.relative_to(ROOT).as_posix(), "wasm_hash": digest(wasm.read_bytes()) if compiled else None,
        "toolchain": {"clang": str(CLANG), "node": str(NODE)},
        "coverage": ["actual saved open/pending/member/close callbacks",
                     "production driver setup, group, member, altar, close and finish reserves",
                     "pause before first altar, every cursor, and after final altar before publication",
                     "unchanged real-member writes, model rows, order and group indexes",
                     "native first-member anchor differs from geometry-first anchor",
                     "actual preview/detail fallback getters for legacy and unfinished cache",
                     "zero-altar ready sentinel 10000 without premature group publication"],
        "limitations": ["SDK doubles use artificial costs; no native fuel or gameplay claim.",
                        "Start and finish callbacks are fixture-controlled; changed catalog callbacks and driver are production code.",
                        "One finite synthetic catalog and two anchors; broader header cases and native health are separate."],
        "output": output[-4000:],
    }
    EVIDENCE.parent.mkdir(parents=True, exist_ok=True)
    EVIDENCE.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    return passed, report


def main() -> int:
    ok, report = run()
    print(("ok" if ok else "FAIL") + " altar-minimum-initialization: " + report["output"].strip())
    print("evidence " + EVIDENCE.relative_to(ROOT).as_posix())
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
