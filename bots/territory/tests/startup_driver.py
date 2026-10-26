"""Compile the actual Territory initializer against an immutable SDK double.

Methods are extracted from the saved production runtime, never rewritten here.
Synthetic fuel and synchronous yield test checkpoint placement and preservation;
native match fuel safety remains a separate requirement. Starts no engine/game.
"""
from __future__ import annotations

import argparse
from contextlib import nullcontext
import hashlib
import json
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
COMMON = HERE.parent / "common" / "src"
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from wasi import compile_fixture, run_wasm  # noqa: E402
from core import Slots  # noqa: E402


def extract(text: str, start: str, semicolon: bool = False) -> str:
    """Extract a unique saved definition, respecting comments and literals."""
    if text.count(start) != 1:
        raise ValueError(f"expected exactly one production seam {start!r}")
    begin = text.index(start)
    opening = text.index("{", begin)
    # Tokenize comments/strings as atoms so their braces do not affect depth.
    token = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]')
    depth = 0
    for match in token.finditer(text, opening):
        if match.group() == "{":
            depth += 1
        elif match.group() == "}":
            depth -= 1
            if depth == 0:
                end = match.end()
                if semicolon:
                    if text[end:end + 1] != ";":
                        raise ValueError(f"missing semicolon after {start}")
                    end += 1
                return text[begin:end]
    raise ValueError(f"unterminated production seam {start}")


def driver_source(runtime: str) -> tuple[str, dict]:
    definitions = {start: extract(runtime, start, semi) for start, semi in (
        ("inline bitboard around(vec2 p, int r)", False),
        ("struct Group {", True),
        ("int tile(vec2 p) const", False),
        ("int distance(const cambc::distance_field& field, vec2 p) const", False),
        ("int distance(const cambc::distance_field& field, const bitboard& b) const", False),
        ("int ordinal(champion actor) const", False),
        ("void compute(cambc::distance_field& field, const bitboard& roots)", False),
        ("static void prepare_budget()", False),
        ("void initialize()", False),
    )}
    hashes = {name: hashlib.sha256(body.encode()).hexdigest() for name, body in definitions.items()}
    turn = extract(runtime, "void turn()")
    # Execute the saved entry branch itself. The sentinel after the extracted
    # prefix represents reaching the first dynamic branch of the real turn.
    begin = turn.index("auto& me = cambc::self();")
    end = turn.index("if (!me.is_alive())", begin)
    turn_entry = turn[begin:end]
    hashes["turn() entry prefix"] = hashlib.sha256(turn_entry.encode()).hexdigest()
    around = definitions.pop("inline bitboard around(vec2 p, int r)")
    group = definitions.pop("struct Group {")
    methods = "\n".join(definitions.values())
    source = f'''#include "{(HERE / 'startup-sdk.h').as_posix()}"
#include "{(COMMON / 'native_nav.h').as_posix()}"
#include "{(COMMON / 'competitive.h').as_posix()}"
namespace p7::live::native {{
using cambc::vec2; using cambc::bitboard; using cambc::champion; using cambc::team;
{around}
{group}
class Driver {{
public:
    Design design_ = Design::harvest_and_exit;
    const std::array<const char*, 5> order_{{"vendetta", "ren", "marcella", "tavi", "vesper_vale"}};
    std::vector<Group> groups_;
    std::vector<vec2> altars_;
    std::vector<bitboard> altar_approaches_;
    bitboard all_altar_approaches_, terrain_;
    std::optional<cambc::distance_field> home_field_;
    std::array<std::array<int, 8>, 2> port_distances_{{}};
    std::array<std::array<int, 8>, 8> source_distances_{{}};
    std::array<int, 2> port_home_{{}};
    std::array<int, 8> alternate_port_{{}}, connector_{{}}, opened_by_{{}};
    NativeNav nav_;
    std::vector<unsigned char> static_blocked_, holes_;
    bool initialized_ = false, is_willow_ = false;
    int width_ = 0, height_ = 0, ordinal_ = 0, team_size_ = 3;
    float jungle_reduction_ = 0;
    std::array<int, 64> history_round_{{}};
    static bool navigation_fuel() {{ return cambc::fuel_remaining() > 55000; }}
    {methods}
    std::uint64_t fingerprint() {{
        startup_mock::charging = false; startup_mock::fuel = 1000000000;
        std::uint64_t hash = 1469598103934665603ull;
        auto add = [&](std::uint64_t value) {{ hash = (hash ^ value) * 1099511628211ull; }};
        auto board = [&](const bitboard& b) {{ for (unsigned char v : b.bits) add(v); }};
        add(initialized_); add(width_); add(height_); add(ordinal_); add(team_size_); add(is_willow_);
        add(static_cast<unsigned>(jungle_reduction_ * 1000));
        board(terrain_); board(all_altar_approaches_);
        for (auto v : static_blocked_) add(v);
        for (auto v : holes_) add(v);
        for (const auto& p : altars_) {{ add(p.x); add(p.y); }}
        for (const auto& b : altar_approaches_) board(b);
        board(home_field_->walls); board(home_field_->roots); add(home_field_->cap);
        for (auto row : port_distances_) for (auto v : row) add(v);
        for (auto row : source_distances_) for (auto v : row) add(v);
        for (auto v : port_home_) add(v);
        for (auto v : alternate_port_) add(v);
        for (auto v : connector_) add(v);
        for (auto v : opened_by_) add(v);
        for (auto v : history_round_) add(v);
        for (const auto& g : groups_) {{
            add(static_cast<cambc::handle_camp_group_t>(g.group)); board(g.spawns);
            add(g.anchor.x); add(g.anchor.y); add(g.home_distance);
            add(static_cast<unsigned>(g.health)); add(static_cast<unsigned>(g.incoming));
        }}
        // This is the real NativeNav built by the production initializer.
        auto distances = nav_.field({{101}}, width_ * height_);
        if (!nav_.last_complete) throw std::runtime_error("incomplete fixture navigation");
        for (int value : distances) add(value);
        return hash;
    }}
}};
class TurnEntryProbe : public Driver {{
public:
    unsigned dynamic_entries = 0, injected_completion_fuel = 0;
    void initialize() {{
        Driver::initialize();
        // This adversarial completion boundary is an injected condition. It
        // makes no assertion about the native fuel cost of the initializer.
        startup_mock::fuel = injected_completion_fuel;
        ++startup_mock::round;
    }}
    void turn_entry() {{
        {turn_entry}
        (void)me;
        ++dynamic_entries;
    }}
}};
unsigned check_turn_entry_completion_fence() {{
    unsigned checks = 0;
    for (unsigned completion : {{50000u, 64999u, 65000u}}) {{
        startup_mock::configure(false);
        TurnEntryProbe probe;
        probe.injected_completion_fuel = completion;
        probe.turn_entry();
        if (!probe.initialized_ || startup_mock::compute_calls != 1 ||
            startup_mock::fuel != completion || startup_mock::round != 2)
            throw std::runtime_error("completion injection did not reach production turn prefix");
#if P7_BOUNDED_INITIALIZATION
        if (probe.dynamic_entries != 0)
            throw std::runtime_error("bounded initialization fell through to dynamic work");
#else
        if (probe.dynamic_entries != 1)
            throw std::runtime_error("legacy entry no longer preserves its fallthrough");
#endif
        ++checks;
        unsigned before = probe.dynamic_entries;
        probe.turn_entry();
        if (probe.dynamic_entries != before + 1 || startup_mock::compute_calls != 1)
            throw std::runtime_error("initialized turn did not resume dynamic work exactly once");
        ++checks;
    }}
    return checks;
}}
}}
int main(int argc, char** argv) {{
    using p7::live::native::Driver;
    bool constrained = argc > 1 && std::string_view(argv[1]) == "constrained";
    startup_mock::configure(constrained);
    Driver driver;
    try {{
        driver.initialize();
        unsigned yields = startup_mock::yields, min_fuel = startup_mock::min_fuel;
        auto fingerprint = driver.fingerprint();
        if (!driver.initialized_ || driver.groups_.size() != 8 || startup_mock::compute_calls != 1)
            throw std::runtime_error("initializer did not complete exactly once");
        // The real production threshold, tested at its exact boundary.
        startup_mock::fuel = 65000; unsigned before = startup_mock::yields;
        driver.prepare_budget();
        if (startup_mock::yields != before) throw std::runtime_error("yielded at threshold");
        startup_mock::fuel = 64999; driver.prepare_budget();
        if (startup_mock::yields != before + 1) throw std::runtime_error("missing threshold yield");
        unsigned entry_checks = p7::live::native::check_turn_entry_completion_fence();
        std::printf("{{\\"completed\\":true,\\"fingerprint\\":\\"%llu\\",\\"yields\\":%u,\\"minimum_synthetic_fuel\\":%u,\\"turn_entry_checks\\":%u}}\\n",
                    static_cast<unsigned long long>(fingerprint), yields, min_fuel, entry_checks);
    }} catch (const startup_mock::FuelExhausted&) {{
        std::printf("{{\\"completed\\":false,\\"yields\\":%u,\\"round\\":%u}}\\n", startup_mock::yields, startup_mock::round);
    }}
}}
'''
    return source, hashes


def _run(work: Path) -> dict:
    runtime = (COMMON / "runtime.h").read_text(encoding="utf-8")
    source, method_hashes = driver_source(runtime)
    work.mkdir(parents=True, exist_ok=True)
    generated = work / "production-startup.cpp"
    generated.write_text(source, encoding="utf-8", newline="\n")
    results = {}
    for bounded in (0, 1):
        wasm = work / f"production-startup-{bounded}.wasm"
        ok, log = compile_fixture(generated, wasm, defines=[f"P7_BOUNDED_INITIALIZATION={bounded}", "P7_STARTUP_TRACE=0"])
        if not ok:
            raise RuntimeError(f"compile bounded={bounded}: {log}")
        for mode in ("baseline", "constrained"):
            ok, log = run_wasm(wasm, args=[mode])
            if not ok:
                raise RuntimeError(f"run bounded={bounded}, {mode}: {log}")
            results[f"bounded={bounded}/{mode}"] = json.loads(log.splitlines()[-1])
    old, enabled = results["bounded=0/baseline"], results["bounded=1/baseline"]
    limited, safe = results["bounded=0/constrained"], results["bounded=1/constrained"]
    if not old["completed"] or not enabled["completed"] or old["fingerprint"] != enabled["fingerprint"]:
        raise AssertionError("off/on production initialization changed immutable output")
    if limited["completed"]:
        raise AssertionError("synthetic case failed to discriminate the unguarded startup")
    if not safe["completed"] or safe["yields"] < 3 or safe["fingerprint"] != old["fingerprint"]:
        raise AssertionError("bounded startup failed preservation or multiple-yield coverage")
    if any(result.get("turn_entry_checks") != 6 for result in (old, enabled, safe)):
        raise AssertionError("missing actual turn-entry completion boundary checks")
    report = {"kind": "production-initializer-sdk-double", "native_fuel_claim": False,
              "runtime_sha256": hashlib.sha256(runtime.encode()).hexdigest(),
              "fixture_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "sdk_double_sha256": hashlib.sha256((HERE / "startup-sdk.h").read_bytes()).hexdigest(),
              "extracted_method_hashes": method_hashes, "results": results,
              "turn_entry_completion_injection": {"remaining_fuel": [50000, 64999, 65000],
                                                   "round_advance": 1, "native_cost_claim": False},
              "limitation": "SDK costs/distances are synthetic; real NativeNav and production initializer/selection execute unchanged."}
    (work / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    return report


def run(work: Path, *, slot_held: bool = False) -> dict:
    """Use one arena slot, or reuse the calling test runner's existing slot."""
    with nullcontext() if slot_held else Slots(1, quiet=True):
        return _run(work)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--work", type=Path, default=ROOT / f".cache/arena/orchestration/territory-health/startup-driver/run-{os.getpid()}")
    parser.add_argument("--slot-held", action="store_true", help="calling suite runner already holds one arena slot")
    args = parser.parse_args()
    try:
        report = run(args.work, slot_held=args.slot_held)
    except (ValueError, RuntimeError, AssertionError, OSError) as error:
        print(f"FAIL production startup driver: {error}")
        return 1
    print("PASS extracted production initializer: identical output, bounded yields, actual turn-entry completion fence, legacy fallthrough")
    print(json.dumps(report["results"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
