"""Exercise the saved production phase journal without an engine or game.

The journal, feature macros, public wrappers and outer dispatch are extracted
verbatim. SDK counters prove observation counts only; synthetic clock changes
do not establish a native suspended instruction or native fuel safety.
"""
from __future__ import annotations

import argparse
from contextlib import nullcontext
import hashlib
import json
import os
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
COMMON = HERE.parent / "common" / "src"
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import Slots  # noqa: E402
from wasi import compile_fixture, run_wasm  # noqa: E402
from startup_driver import extract  # noqa: E402


def conditional_block(runtime: str, begin: int) -> str:
    """Retain a saved preprocessor conditional, including nested branches."""
    depth = 0
    for match in re.finditer(r"^[ \t]*#[ \t]*(if|ifdef|ifndef|endif)\b[^\n]*", runtime[begin:], re.M):
        depth += -1 if match.group(1) == "endif" else 1
        if depth == 0:
            return runtime[begin:begin + match.end()]
    raise ValueError("unterminated production conditional")


def phase_macros(runtime: str) -> str:
    seam = "#if P7_PHASE_TRACE\n#define P7_PHASE_CLOCK"
    if runtime.count(seam) != 1:
        raise ValueError("missing or ambiguous saved phase macro block")
    return conditional_block(runtime, runtime.index(seam))


def production_seams(runtime: str) -> tuple[dict[str, str], dict[str, str]]:
    seam = "#if P7_PHASE_TRACE\n    struct PhaseJournal {"
    if runtime.count(seam) != 1:
        raise ValueError("missing or ambiguous saved phase journal block")
    definitions = {"phase feature macros": phase_macros(runtime),
                   "PhaseJournal and methods": conditional_block(runtime, runtime.index(seam))}
    for name in ("bool flush_phase_trace_turn()", "void finish_phase_trace_turn(unsigned flag = 4)",
                 "inline int run(Design design, bool control, const std::array<const char*, 5>& order)"):
        definitions[name] = extract(runtime, name)
    return definitions, {name: hashlib.sha256(body.encode()).hexdigest() for name, body in definitions.items()}


def driver_source(runtime: str) -> tuple[str, dict[str, str]]:
    definitions, hashes = production_seams(runtime)
    macros = definitions["phase feature macros"]
    journal = definitions["PhaseJournal and methods"]
    wrappers = "\n".join(definitions[name] for name in (
        "bool flush_phase_trace_turn()", "void finish_phase_trace_turn(unsigned flag = 4)"))
    outer = definitions["inline int run(Design design, bool control, const std::array<const char*, 5>& order)"]
    source = r'''#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>
#include "@NATIVE_TYPES@"
@MACROS@
namespace cambc {
inline std::uint64_t fixture_fuel = 100000;
inline unsigned fixture_round = 19, clock_queries = 0, fuel_queries = 0, position_queries = 0;
inline unsigned owner_ends = 0, stop_after = 1;
inline std::vector<std::uint64_t> turn_fuels;
struct OwnerTurnEnded {};
inline std::uint64_t fuel_remaining() { ++fuel_queries; return fixture_fuel; }
inline unsigned round_number() { ++clock_queries; return fixture_round; }
inline bool is_draft() { return false; }
inline void end_turn() {
    ++owner_ends;
    if (owner_ends == stop_after) throw OwnerTurnEnded{};
    ++fixture_round;
    if (owner_ends < turn_fuels.size()) fixture_fuel = turn_fuels[owner_ends];
}
inline void reset() {
    fixture_fuel = 100000; fixture_round = 19;
    clock_queries = fuel_queries = position_queries = owner_ends = 0;
    stop_after = 1; turn_fuels.clear();
}
}
inline unsigned checks = 0;
#define CHECK(test) do { ++checks; if (!(test)) { std::fprintf(stderr, "phase assertion line %d: %s\n", __LINE__, #test); std::abort(); } } while (false)
namespace dispatch_double {
inline bool pending = false, begin_in_turn = false, throw_in_turn = false;
inline unsigned turns = 0, remembers = 0, drafts = 0, final_flags = 0, final_attempts = 0;
inline bool final_pending = false;
}
namespace p7::live {
namespace native {
class Bot {
public:
    bool initialized_ = true;
@JOURNAL@
@WRAPPERS@
    Bot() = default;
    Bot(Design, bool, const std::array<const char*, 5>&) {
        if (dispatch_double::pending) { phase_begin(19, 2); phase_finish(); }
    }
    ~Bot() {
        dispatch_double::final_flags = phase_.flags;
        dispatch_double::final_attempts = phase_.attempts;
        dispatch_double::final_pending = phase_.pending;
    }
    void draft() { ++dispatch_double::drafts; }
    void turn() {
        ++dispatch_double::turns;
        if (dispatch_double::begin_in_turn) phase_begin(19, 2);
        if (dispatch_double::throw_in_turn) throw std::runtime_error("injected fixture exception");
    }
    void remember() { ++dispatch_double::remembers; }
    unsigned clock_expression() { return P7_PHASE_CLOCK(cambc::round_number(), 31); }
    std::uint64_t fuel_expression() { return P7_PHASE_FUEL(cambc::fuel_remaining(), 32); }
    void scope_without_clock() { P7_PHASE_SCOPE(18); ++cambc::fixture_round; }
    void scope_exception_without_clock() { P7_PHASE_SCOPE(19); ++cambc::fixture_round; throw std::runtime_error("synthetic resume"); }
    void explicit_pair() { P7_PHASE_ENTER(21); P7_PHASE_LEAVE(21); }
};
}
@OUTER@
}
using Bot = p7::live::native::Bot;
inline std::uint64_t event(unsigned kind, unsigned id, unsigned value = 0) {
    return (std::uint64_t(kind) << 56) | (std::uint64_t(id) << 40) | value;
}
std::string state(const Bot& p) {
    const auto& j = p.phase_; std::ostringstream out;
    out << j.size << ':' << j.entry_round << ':' << j.actor << ':' << j.flags << ':' << j.attempts
        << ':' << j.active << ':' << j.pending << ':' << j.armed;
    for (auto value : j.events) out << ':' << value;
    return out.str();
}
void wrappers_and_selection() {
    cambc::reset(); Bot p;
    p.initialized_ = false; p.phase_begin(19, 2); CHECK(!p.phase_.armed && p.phase_.size == 0);
    p.initialized_ = true;
    p.phase_begin(18, 2); p.phase_begin(19, 3);
    CHECK(!p.phase_.armed && p.phase_.size == 0);
    p.phase_begin(19, 2);
    CHECK(p.phase_.active && p.phase_.armed && !p.phase_.pending);
    CHECK(p.phase_.entry_round == 19 && p.phase_.actor == 2 && p.phase_.size == 1);
    CHECK(p.phase_.events[0] == event(3, 1, 19));
    const auto prefix = state(p); p.phase_begin(19, 2); CHECK(state(p) == prefix);
    const auto calls = cambc::clock_queries + cambc::fuel_queries + cambc::position_queries;
    CHECK(p.phase_clock(19, 7) == 19);
    const std::uint64_t wide = (std::uint64_t(1) << 44) + 0xfedcba98u;
    CHECK(p.phase_fuel(wide, 8) == wide);
    CHECK(cambc::clock_queries + cambc::fuel_queries + cambc::position_queries == calls);
    CHECK(p.phase_.events[1] == event(3, 7, 19));
    CHECK(p.phase_.events[2] == event(4, 8, 0xfedcba98u));
    CHECK(p.clock_expression() == 19 && cambc::clock_queries == 1);
    cambc::fixture_fuel = wide;
    CHECK(p.fuel_expression() == wide && cambc::fuel_queries == 1);
    p.explicit_pair();
    CHECK(p.phase_.events[p.phase_.size - 2] == event(1, 21));
    CHECK(p.phase_.events[p.phase_.size - 1] == event(2, 21));
    CHECK(cambc::clock_queries == 1 && cambc::fuel_queries == 1 && cambc::position_queries == 0);
}
void crossing_freezes_observed_interval() {
    cambc::reset(); Bot p; p.phase_begin(19, 2);
    p.phase_event(1, 15); p.phase_clock(19, 10); p.phase_event(1, 17); p.phase_fuel(24001, 4);
    const auto prefix = p.phase_.events; const auto count = p.phase_.size;
    CHECK(p.phase_clock(20, 11) == 20);
    CHECK(!p.phase_.active && p.phase_.pending && p.phase_.flags == 1);
    CHECK(p.phase_.size == count + 1 && p.phase_.events[count] == event(3, 11, 20));
    for (unsigned i = 0; i < count; ++i) CHECK(p.phase_.events[i] == prefix[i]);
    CHECK(p.phase_.events[2] == event(3, 10, 19)); // Last same-round probe retained.
    const auto frozen = state(p);
    p.phase_event(2, 17); p.phase_clock(21, 12); p.phase_fuel(99900, 13); p.phase_finish(8); p.phase_begin(19, 2);
    CHECK(state(p) == frozen);
    CHECK(cambc::clock_queries == 0 && cambc::fuel_queries == 0 && cambc::position_queries == 0);
}
void completion_is_order_not_clock() {
    cambc::reset(); Bot p; p.phase_begin(19, 2); p.scope_without_clock();
    CHECK(cambc::fixture_round == 20 && cambc::clock_queries == 0);
    CHECK(p.phase_.size == 3 && p.phase_.events[1] == event(1, 18) && p.phase_.events[2] == event(2, 18));
    CHECK(p.phase_.flags == 0 && p.phase_.active); // Completion alone cannot certify a crossing.
    p.finish_phase_trace_turn();
    CHECK(p.phase_.flags == 4 && p.phase_.pending && !p.phase_.active);
    CHECK(p.phase_.size == 3 && cambc::clock_queries == 0 && cambc::fuel_queries == 0);
    const auto normal = state(p); p.phase_clock(20, 11); CHECK(state(p) == normal);
    Bot q; q.phase_begin(19, 2);
    try { q.scope_exception_without_clock(); CHECK(false); } catch (const std::runtime_error&) {}
    q.finish_phase_trace_turn(8);
    CHECK(q.phase_.flags == 8 && q.phase_.size == 3 && q.phase_.pending);
    CHECK(q.phase_.events[2] == event(2, 19));
    CHECK(cambc::clock_queries == 0 && cambc::fuel_queries == 0);
    Bot inactive; inactive.finish_phase_trace_turn();
    CHECK(inactive.phase_.flags == 0 && inactive.phase_.size == 0 && !inactive.phase_.pending);
}
void bounded_append_only_overflow() {
    cambc::reset(); Bot p; p.phase_begin(19, 2);
    for (unsigned i = 1; i < 64; ++i) {
        const auto prefix = p.phase_.events; p.phase_event(1, i, i * 3);
        CHECK(p.phase_.size == i + 1);
        for (unsigned k = 0; k < i; ++k) CHECK(p.phase_.events[k] == prefix[k]);
    }
    const auto prefix = p.phase_.events;
    p.phase_event(2, 65, 7);
    CHECK(p.phase_.size == 64 && p.phase_.events == prefix);
    CHECK(p.phase_.flags == 2 && !p.phase_.active && p.phase_.pending);
    const auto frozen = state(p); p.phase_finish(); p.phase_clock(20, 11); CHECK(state(p) == frozen);
    Bot final_slot; final_slot.phase_begin(19, 2);
    for (unsigned i = 1; i < 63; ++i) final_slot.phase_event(1, i);
    final_slot.phase_clock(20, 11);
    CHECK(final_slot.phase_.size == 64 && final_slot.phase_.flags == 1 && final_slot.phase_.events[63] == event(3, 11, 20));
    Bot no_slot; no_slot.phase_begin(19, 2);
    for (unsigned i = 1; i < 64; ++i) no_slot.phase_event(1, i);
    no_slot.phase_clock(20, 11);
    CHECK(no_slot.phase_.size == 64 && no_slot.phase_.flags == 2); // Lost changed probe cannot certify crossing.
}
struct Capture : std::streambuf {
    Bot* bot = nullptr; bool fail = false;
    unsigned writes = 0; std::string bytes;
    std::streamsize xsputn(const char* text, std::streamsize size) override {
        ++writes;
        CHECK(bot && bot->phase_.attempts > 0 && !bot->phase_.pending);
        CHECK(cambc::clock_queries > 0);
        if (fail) throw std::runtime_error("synthetic output interruption");
        bytes.append(text, static_cast<std::size_t>(size)); return size;
    }
};
void deferred_flush_and_output_bound() {
    cambc::reset(); Bot p; Capture output; output.bot = &p;
    auto* original = std::cout.rdbuf(&output);
    CHECK(!p.flush_phase_trace_turn());
    CHECK(cambc::fuel_queries == 0 && cambc::clock_queries == 0 && output.writes == 0);
    p.phase_begin(19, 2); p.phase_event(1, 17); p.phase_clock(20, 11);
    const auto saved = state(p);
    cambc::fixture_fuel = 89999;
    CHECK(p.flush_phase_trace_turn());
    CHECK(state(p) == saved && output.writes == 0 && cambc::clock_queries == 0 && cambc::fuel_queries == 1);
    cambc::fixture_fuel = 90000; cambc::fixture_round = 21;
    CHECK(p.flush_phase_trace_turn());
    CHECK(p.phase_.attempts == 1 && !p.phase_.pending && output.writes == 1);
    CHECK(cambc::clock_queries == 1 && cambc::fuel_queries == 2);
    CHECK(output.bytes.find("p7 phase entry=19 actor=2 emit=21 flags=1 attempts=1 count=3 events=") == 0);
    CHECK(output.bytes.size() < 1200 && output.bytes.back() == '\n');
    const auto header_end = output.bytes.find(" events=") + 8;
    CHECK(output.bytes.size() - header_end - 1 == p.phase_.size * 16);
    for (unsigned i = 0; i < p.phase_.size; ++i) {
        const auto hex = output.bytes.substr(header_end + i * 16, 16);
        CHECK(std::stoull(hex, nullptr, 16) == p.phase_.events[i]);
    }
    CHECK(!p.flush_phase_trace_turn()); p.phase_begin(19, 2);
    CHECK(!p.phase_.active && p.phase_.attempts == 1 && output.writes == 1);
    CHECK(cambc::fuel_queries == 2 && cambc::clock_queries == 1);
    std::cout.rdbuf(original);

    Bot largest; largest.phase_begin(19, 2);
    for (unsigned i = 1; i < 64; ++i) largest.phase_event(255, 65535, 0xffffffffu);
    largest.phase_finish();
    largest.phase_.entry_round = largest.phase_.actor = largest.phase_.flags = std::numeric_limits<unsigned>::max();
    largest.phase_.attempts = std::numeric_limits<unsigned>::max() - 1;
    cambc::fixture_round = std::numeric_limits<unsigned>::max(); cambc::fixture_fuel = 90001;
    Capture max_output; max_output.bot = &largest; original = std::cout.rdbuf(&max_output);
    CHECK(largest.phase_flush()); std::cout.rdbuf(original);
    const std::string expected_header = "p7 phase entry=4294967295 actor=4294967295 emit=4294967295 flags=4294967295 attempts=4294967295 count=64 events=";
    CHECK(max_output.bytes.find(expected_header) == 0);
    CHECK(max_output.writes == 1 && max_output.bytes.size() == expected_header.size() + 64 * 16 + 1);
    CHECK(max_output.bytes.size() <= 1200);

    Bot interrupted; interrupted.phase_begin(19, 2); interrupted.phase_finish();
    Capture bad_output; bad_output.bot = &interrupted; bad_output.fail = true;
    original = std::cout.rdbuf(&bad_output); std::cout.exceptions(std::ios::badbit);
    bool caught = false;
    try { interrupted.phase_flush(); } catch (const std::exception&) { caught = true; }
    std::cout.exceptions(std::ios::goodbit); std::cout.clear(); std::cout.rdbuf(original);
    CHECK(caught && interrupted.phase_.attempts == 1 && !interrupted.phase_.pending && bad_output.writes == 1);
    CHECK(!interrupted.phase_flush() && bad_output.writes == 1);
}
void saved_dispatch(bool pending, const std::vector<std::uint64_t>& fuels, unsigned expected_turns, unsigned expected_remembers, unsigned expected_attempts) {
    cambc::reset(); cambc::turn_fuels = fuels; cambc::fixture_fuel = fuels.front(); cambc::stop_after = fuels.size();
    dispatch_double::pending = pending; dispatch_double::begin_in_turn = false; dispatch_double::throw_in_turn = false;
    dispatch_double::turns = dispatch_double::remembers = dispatch_double::drafts = 0;
    const std::array<const char*, 5> order{"vendetta", "ren", "marcella", "tavi", "vesper_vale"};
    std::ostringstream output; auto* original = std::cout.rdbuf(output.rdbuf());
    try { p7::live::run(p7::live::Design::harvest_and_exit, false, order); CHECK(false); } catch (const cambc::OwnerTurnEnded&) {}
    std::cout.rdbuf(original);
    CHECK(cambc::owner_ends == fuels.size() && dispatch_double::drafts == 0);
    CHECK(dispatch_double::turns == expected_turns && dispatch_double::remembers == expected_remembers);
    CHECK(dispatch_double::final_attempts == expected_attempts);
    CHECK((!output.str().empty()) == (expected_attempts == 1));
    CHECK(dispatch_double::final_pending == (pending && expected_attempts == 0));
}
void dispatch_completion(bool exceptional) {
    cambc::reset(); dispatch_double::pending = false; dispatch_double::begin_in_turn = true;
    dispatch_double::throw_in_turn = exceptional; dispatch_double::turns = dispatch_double::remembers = 0;
    const std::array<const char*, 5> order{}; std::ostringstream output;
    auto* original = std::cout.rdbuf(output.rdbuf());
    try { p7::live::run(p7::live::Design::harvest_and_exit, false, order); CHECK(false); } catch (const cambc::OwnerTurnEnded&) {}
    std::cout.rdbuf(original);
    CHECK(dispatch_double::turns == 1 && dispatch_double::remembers == (exceptional ? 0u : 1u));
    CHECK(dispatch_double::final_flags == (exceptional ? 8u : 4u) && dispatch_double::final_pending);
    CHECK(cambc::clock_queries == 0 && cambc::fuel_queries == 0);
}
int main() {
    wrappers_and_selection(); crossing_freezes_observed_interval(); completion_is_order_not_clock();
    bounded_append_only_overflow(); deferred_flush_and_output_bound();
    saved_dispatch(false, {100000}, 1, 1, 0);
    saved_dispatch(true, {89999}, 0, 0, 0);
    saved_dispatch(true, {90000}, 0, 0, 1);
    saved_dispatch(true, {89999, 90000, 100000}, 1, 1, 1);
    dispatch_completion(false); dispatch_completion(true);
    std::printf("{\"checks\":%u,\"capacity\":64,\"flush_floor\":90000,\"existing_value_wrappers_add_sdk_reads\":0,\"capture_round\":19,\"capture_actor\":2,\"phase_attribution_requires_observed_interval\":true,\"native_fuel_claim\":false}\n", checks);
}
'''
    for name, body in {"@NATIVE_TYPES@": (COMMON / "native_types.h").as_posix(), "@MACROS@": macros,
                       "@JOURNAL@": journal, "@WRAPPERS@": wrappers, "@OUTER@": outer}.items():
        source = source.replace(name, body)
    return source, hashes


def _run(work: Path) -> dict:
    runtime_path = COMMON / "runtime.h"
    paths = [runtime_path, Path(__file__), HERE / "startup_driver.py", COMMON / "native_types.h"]
    pinned = {path: path.read_bytes() for path in paths}
    runtime = pinned[runtime_path].decode("utf-8")
    source, hashes = driver_source(runtime)
    work.mkdir(parents=True, exist_ok=True)
    cpp, wasm = work / "production-phase.cpp", work / "production-phase.wasm"
    cpp.write_text(source, encoding="utf-8", newline="\n")
    ok, log = compile_fixture(cpp, wasm, defines=["P7_PHASE_TRACE=1", "P7_LIVE_TRACE=0"])
    if not ok:
        raise RuntimeError("phase compile: " + log)
    ok, log = run_wasm(wasm)
    if not ok:
        raise RuntimeError("production phase journal: " + log)
    observed = json.loads(log.splitlines()[-1])
    if observed.get("checks", 0) < 2000 or observed.get("existing_value_wrappers_add_sdk_reads") != 0:
        raise AssertionError("missing production journal assertions")
    changed = [path.relative_to(ROOT).as_posix() for path, value in pinned.items() if path.read_bytes() != value]
    if changed:
        raise RuntimeError("pinned inputs changed during compilation: " + ", ".join(changed))
    report = {"kind": "saved-production-phase-journal-sdk-double", "native_fuel_claim": False,
              "input_hashes": {path.relative_to(ROOT).as_posix(): hashlib.sha256(value).hexdigest() for path, value in pinned.items()},
              "extracted_hashes": hashes, "observed": observed,
              "limitations": ["Clock advances and output interruption are synthetic; native suspension attribution and fuel safety remain separate.",
                              "Phase completion events order source work and do not timestamp an unobserved suspension.",
                              "Fuel wrappers preserve uint64 return values; packed event payloads store low32 only. Native fuel values up to200000 fit this domain.",
                              "The fixed-packet boundary stress uses maximum-width unsigned metadata; it is not a reachable gameplay claim."]}
    (work / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    return report


def run(work: Path, *, slot_held: bool = False) -> dict:
    with nullcontext() if slot_held else Slots(1, quiet=True):
        return _run(work)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--work", type=Path, default=ROOT / f".cache/arena/orchestration/territory-phase/fixture/run-{os.getpid()}")
    parser.add_argument("--slot-held", action="store_true", help="caller already holds one arena slot")
    args = parser.parse_args()
    try:
        report = run(args.work, slot_held=args.slot_held)
    except (ValueError, RuntimeError, AssertionError, OSError) as error:
        print(f"FAIL production phase driver: {error}")
        return 1
    print(f"PASS {report['observed']['checks']} phase journal checks (saved production, synthetic SDK, no native fuel claim)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
