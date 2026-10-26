"""Exercise saved Territory warm-up and dynamic caller gates under an SDK double.

No engine or game is started. The actual caller is extracted from runtime.h;
frame/geometry bodies and costs are injected, so this proves caller semantics,
not full observation logic, native fuel safety, or useful gameplay activation.
"""
from __future__ import annotations

import argparse
from contextlib import nullcontext
import hashlib
import json
import os
from pathlib import Path
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
COMMON = HERE.parent / "common" / "src"
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import Slots  # noqa: E402
from wasi import compile_fixture, run_wasm  # noqa: E402
from startup_driver import extract  # noqa: E402
from phase_driver import phase_macros  # noqa: E402


def live_caller(runtime: str) -> str:
    turn = extract(runtime, "void turn()")
    # Retain both saved arms so fixtures can compile the old direct compute
    # and the new helper/round fence without reconstructing their dispatch.
    reciprocal = "#if P7_RECIPROCAL_SOURCE_FIELDS\n        if (!reciprocal_sources_ready()) {"
    start = reciprocal if reciprocal in turn else "#if P7_REUSE_CURRENT_FIELD"
    if start not in turn:
        start = "compute(current_field_, around(me.position(), 0));"
    end = "denial(f, enemies);"
    if turn.count(start) != 1 or turn.count(end) != 1:
        raise ValueError("dynamic production caller seams are missing or ambiguous")
    return turn[turn.index(start):turn.index(end)]


def driver_source(runtime: str) -> tuple[str, dict]:
    names = (
        ("inline bitboard around(vec2 p, int r)", False),
        ("struct Group {", True),
        ("int distance(const cambc::distance_field& field, vec2 p) const", False),
        ("int distance(const cambc::distance_field& field, const bitboard& b) const", False),
        ("bitboard port_goals(int p) const", False),
        ("void compute(cambc::distance_field& field, const bitboard& roots)", False),
        ("bool warm()", False),
    )
    definitions = {name: extract(runtime, name, semi) for name, semi in names}
    caller = live_caller(runtime)
    hashes = {name: hashlib.sha256(body.encode()).hexdigest() for name, body in definitions.items()}
    hashes["phase feature macros (disabled)"] = hashlib.sha256(phase_macros(runtime).encode()).hexdigest()
    hashes["turn current-field/frame/geometry/pre-plan caller"] = hashlib.sha256(caller.encode()).hexdigest()
    around = definitions.pop("inline bitboard around(vec2 p, int r)")
    group = definitions.pop("struct Group {")
    methods = "\n".join(definitions.values())
    return f'''#include "{(HERE / 'startup-sdk.h').as_posix()}"
#include "{(COMMON / 'native_types.h').as_posix()}"
{phase_macros(runtime)}
namespace cambc::vision {{
inline std::vector<champion> champions(team) {{ return {{}}; }}
}}
namespace p7::live::native {{
using cambc::vec2; using cambc::bitboard; using cambc::camp; using cambc::champion; using cambc::team;
{around}
{group}
inline unsigned checks = 0;
#define CHECK(test) do {{ ++checks; if (!(test)) {{ std::fprintf(stderr, "live caller assertion line %d: %s\\n", __LINE__, #test); std::abort(); }} }} while (false)
class Probe {{
public:
    int warm_ = 0;
    bool sample_turn_ = true;
    std::array<unsigned, 8> sample_fuel_{{}};
    std::vector<Group> groups_;
    std::vector<vec2> altars_;
    std::vector<bitboard> altar_approaches_;
    bitboard terrain_;
    cambc::distance_field current_field_;
    std::array<std::optional<cambc::distance_field>, 2> port_fields_;
    std::array<std::optional<cambc::distance_field>, 8> source_fields_;
    std::array<int, 2> port_home_{{}};
    std::array<std::array<int, 8>, 2> port_distances_{{}};
    std::array<std::array<int, 8>, 8> source_distances_{{}};
    unsigned frame_calls = 0, geometry_calls = 0, admitted = 0;
    unsigned frame_fuel = 45000, geometry_fuel = 30000;
    bool advance_in_frame = false, advance_in_geometry = false;
    {methods}
    // The caller is production code; these two expensive bodies are explicit
    // adversarial completion doubles, not reimplementations of their policies.
    Frame frame(const std::vector<camp>&, const std::vector<champion>&, const std::vector<champion>&) {{
        ++frame_calls; startup_mock::fuel = frame_fuel;
        if (advance_in_frame) ++startup_mock::round;
        return {{}};
    }}
    void geometry(Frame&) {{
        ++geometry_calls; startup_mock::fuel = geometry_fuel;
        if (advance_in_geometry) ++startup_mock::round;
    }}
    void dynamic_caller() {{
        const auto round = cambc::round_number();
        struct Actor {{ vec2 position() const {{ return {{1, 1}}; }} }} me;
        std::vector<camp> camps;
        std::vector<champion> enemies;
        {caller}
        ++admitted;
    }}
    unsigned port_mask() const {{
        unsigned mask = 0;
        for (unsigned i = 0; i < port_fields_.size(); ++i) if (port_fields_[i]) mask |= 1u << i;
        return mask;
    }}
    unsigned source_mask() const {{
        unsigned mask = 0;
        for (unsigned i = 0; i < source_fields_.size(); ++i) if (source_fields_[i]) mask |= 1u << i;
        return mask;
    }}
    void setup_warm_world() {{
        terrain_ = cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | cambc::map::spawn_region(team::enemy);
        altars_ = cambc::map::altars().positions();
        for (const auto& p : altars_) altar_approaches_.push_back(around(p, cambc::constants::submit_gems_range) & ~terrain_);
        for (auto g : cambc::map::camp_groups()) {{
            if (groups_.size() == 8) break;
            auto camps = g.camps();
            groups_.push_back({{g, g.spawn_tiles(), camps.front().spawn_position()}});
        }}
        port_home_.fill(-1);
        for (auto& row : port_distances_) row.fill(-1);
        for (auto& row : source_distances_) row.fill(-1);
    }}
    std::uint64_t cached_fingerprint() const {{
        std::uint64_t hash = 1469598103934665603ull;
        auto add = [&](std::uint64_t v) {{ hash = (hash ^ v) * 1099511628211ull; }};
        auto field = [&](const cambc::distance_field& f) {{
            for (auto v : f.roots.bits) add(v);
            for (auto v : f.walls.bits) add(v);
            add(f.cap);
        }};
        for (const auto& f : port_fields_) if (f) field(*f);
        for (const auto& f : source_fields_) if (f) field(*f);
        for (auto v : port_home_) add(v);
        for (auto row : port_distances_) for (auto v : row) add(v);
        for (auto row : source_distances_) for (auto v : row) add(v);
        return hash;
    }}
}};

void warm_completion_and_reuse() {{
    startup_mock::configure(false);
    Probe p; p.setup_warm_world();
    startup_mock::fuel = 64999;
    CHECK(!p.warm());
    CHECK(p.warm_ == 0 && p.port_mask() == 0 && p.source_mask() == 0);
    CHECK(startup_mock::compute_calls == 0);
    // This false return means pending, while the final false means complete.
    // Preserve and expose this current production ambiguity; do not redesign it.
    for (unsigned step = 0; step < 10; ++step) {{
        startup_mock::fuel = step == 0 ? 65000 : 100000;
        CHECK(p.warm());
        CHECK(p.warm_ == static_cast<int>(step + 1));
        CHECK(startup_mock::compute_calls == step + 1);
        CHECK(p.port_mask() == (step == 0 ? 1u : 3u));
        CHECK(p.source_mask() == (step < 2 ? 0u : (1u << (step - 1)) - 1));
    }}
    CHECK(p.port_mask() == 3 && p.source_mask() == 255);
    for (unsigned i = 0; i < 2; ++i) {{
        CHECK(p.port_fields_[i]->roots.bits == (p.port_goals(i) & ~p.terrain_).bits);
        CHECK(p.port_fields_[i]->cap == 256);
    }}
    for (unsigned i = 0; i < 8; ++i) {{
        CHECK(p.source_fields_[i]->roots.bits == (p.groups_[i].spawns & ~p.terrain_).bits);
        CHECK(p.source_fields_[i]->cap == 256);
    }}
    const auto fingerprint = p.cached_fingerprint();
    startup_mock::fuel = 100000;
    CHECK(!p.warm());
    CHECK(p.warm_ == 10 && startup_mock::compute_calls == 10);
    CHECK(p.cached_fingerprint() == fingerprint);
    startup_mock::fuel = 64999;
    CHECK(!p.warm());
    CHECK(p.warm_ == 10 && startup_mock::compute_calls == 10);
    // A fresh dynamic field must not overwrite any immutable warm field.
    startup_mock::fuel = 1000000000;
    p.dynamic_caller();
    CHECK(p.admitted == 1 && startup_mock::compute_calls == 11);
    CHECK(p.cached_fingerprint() == fingerprint);
    startup_mock::fuel = 100000;
    CHECK(!p.warm() && startup_mock::compute_calls == 11);
    CHECK(p.cached_fingerprint() == fingerprint);
}}

void dynamic_gate_boundaries() {{
    for (unsigned remaining : {{29999u, 30000u, 30001u}}) {{
        startup_mock::configure(false);
        Probe p; p.geometry_fuel = remaining;
        p.dynamic_caller();
        CHECK(startup_mock::compute_calls == 1);
        CHECK(p.frame_calls == 1 && p.geometry_calls == 1);
        CHECK(p.admitted == (remaining < 30000 ? 0u : 1u));
        CHECK(p.sample_fuel_[2] > p.sample_fuel_[3]);
        CHECK(p.sample_fuel_[3] == 45000 && p.sample_fuel_[4] == remaining);
    }}
    startup_mock::configure(false);
    Probe frame_change; frame_change.advance_in_frame = true;
    frame_change.dynamic_caller();
    CHECK(frame_change.frame_calls == 1 && frame_change.geometry_calls == 0 && frame_change.admitted == 0);
    CHECK(frame_change.sample_fuel_[3] == 0 && frame_change.sample_fuel_[4] == 0);
    startup_mock::configure(false);
    Probe geometry_change; geometry_change.advance_in_geometry = true;
    geometry_change.dynamic_caller();
    CHECK(geometry_change.frame_calls == 1 && geometry_change.geometry_calls == 1 && geometry_change.admitted == 0);
    CHECK(geometry_change.sample_fuel_[3] == 45000 && geometry_change.sample_fuel_[4] == 0);
}}
}}
int main() {{
    p7::live::native::warm_completion_and_reuse();
    p7::live::native::dynamic_gate_boundaries();
    std::printf("{{\\"checks\\":%u,\\"warm_fields\\":10,\\"pending_low_fuel_returns_false\\":true,\\"native_fuel_claim\\":false}}\\n", p7::live::native::checks);
}}
''', hashes


def trace_driver_source(runtime: str) -> tuple[str, dict]:
    packet = extract(runtime, "struct LivePacket {")
    tail = runtime.index("struct LivePacket {") + len(packet)
    packet += runtime[tail:runtime.index(";", tail) + 1]
    if not packet.endswith(" live_packet_;"):
        raise ValueError("unexpected production LivePacket declaration seam")
    definitions = {name: extract(runtime, name) for name in (
        "void live_trace_flush()", "void live_trace_begin(unsigned round)",
        "void live_trace_mark(int stage, int slot = -1, int exit = 0)", "void live_trace_end()",
        "bool flush_live_trace_turn()")}
    hashes = {name: hashlib.sha256(body.encode()).hexdigest() for name, body in definitions.items()}
    hashes["phase feature macros (disabled)"] = hashlib.sha256(phase_macros(runtime).encode()).hexdigest()
    hashes["LivePacket declaration"] = hashlib.sha256(packet.encode()).hexdigest()
    outer_run = extract(runtime, "inline int run(Design design, bool control, const std::array<const char*, 5>& order)")
    hashes["outer production run dispatch"] = hashlib.sha256(outer_run.encode()).hexdigest()
    methods = "\n".join(definitions.values())
    return f'''#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include "{(COMMON / 'native_types.h').as_posix()}"
{phase_macros(runtime)}
namespace cambc {{
enum class handle_champion_t : unsigned {{ fixture = 7 }};
inline unsigned fixture_fuel = 100000, fixture_round = 16, position_queries = 0, round_queries = 0;
inline unsigned owner_ends = 0;
struct OwnerTurnEnded {{}};
struct vec2 {{ int x = 4, y = 5; }};
struct champion {{
    operator handle_champion_t() const {{ return handle_champion_t::fixture; }}
    vec2 position() const {{ ++position_queries; return {{}}; }}
}};
inline champion& self() {{ static champion own; return own; }}
inline unsigned fuel_remaining() {{ return fixture_fuel; }}
inline unsigned round_number() {{ ++round_queries; return fixture_round; }}
inline bool is_draft() {{ return false; }}
inline void end_turn() {{ ++owner_ends; throw OwnerTurnEnded{{}}; }}
}}
inline unsigned checks = 0;
#define CHECK(test) do {{ ++checks; if (!(test)) {{ std::fprintf(stderr, "live trace assertion line %d: %s\\n", __LINE__, #test); std::abort(); }} }} while (false)
class TraceProbe {{
public:
    {packet}
    bool live_trace_active_ = false, live_trace_pending_ = false;
    int warm_ = 10;
    std::array<std::optional<int>, 2> port_fields_;
    std::array<std::optional<int>, 8> source_fields_;
    {methods}
    std::string packet_value() const {{
        const auto& p = live_packet_;
        std::ostringstream out;
        out << p.round << ':' << p.actor << ':' << p.completed_round << ':'
            << p.stage << ':' << p.exit << ':' << p.warm_before << ':' << p.warm_after << ':'
            << p.ports << ':' << p.sources << ':' << p.proposal_goal << ':' << p.proposal_source << ':'
            << p.task_goal << ':' << p.task_source << ':' << p.farm_source << ':' << p.inspect << ':' << p.x << ':' << p.y;
        for (auto value : p.fuel) out << ':' << value;
        return out.str();
    }}
}};
namespace dispatch_double {{
inline bool pending = false, final_pending = false;
inline unsigned turns = 0, remembers = 0, drafts = 0;
}}
namespace p7::live {{
namespace native {{
class Bot : public ::TraceProbe {{
public:
    Bot(Design, bool, const std::array<const char*, 5>&) {{
        live_trace_pending_ = dispatch_double::pending;
        live_packet_.round = 128; live_packet_.actor = 7;
    }}
    ~Bot() {{ dispatch_double::final_pending = live_trace_pending_; }}
    void turn() {{ ++dispatch_double::turns; }}
    void remember() {{ ++dispatch_double::remembers; }}
    void draft() {{ ++dispatch_double::drafts; }}
}};
}}
{outer_run}
}}
void check_saved_outer_dispatch(bool pending, unsigned fuel, std::ostringstream& output) {{
    dispatch_double::pending = pending; dispatch_double::final_pending = false;
    dispatch_double::turns = dispatch_double::remembers = dispatch_double::drafts = 0;
    cambc::fixture_fuel = fuel; cambc::owner_ends = 0;
    const auto before = output.str().size();
    const std::array<const char*, 5> order{{"vendetta", "ren", "marcella", "tavi", "vesper_vale"}};
    try {{
        p7::live::run(p7::live::Design::harvest_and_exit, false, order);
        CHECK(false);
    }} catch (const cambc::OwnerTurnEnded&) {{}}
    CHECK(cambc::owner_ends == 1 && dispatch_double::drafts == 0);
    CHECK(dispatch_double::turns == (pending ? 0u : 1u));
    CHECK(dispatch_double::remembers == (pending ? 0u : 1u));
    CHECK(dispatch_double::final_pending == (pending && fuel < 90000));
    CHECK((output.str().size() > before) == (pending && fuel >= 90000));
}}
int main() {{
    std::ostringstream output;
    auto* previous = std::cout.rdbuf(output.rdbuf());
    TraceProbe p;
    p.port_fields_[0] = 1; p.port_fields_[1] = 1;
    p.source_fields_[0] = 1; p.source_fields_[7] = 1;
    p.live_trace_begin(16);
    CHECK(p.live_trace_active_ && !p.live_trace_pending_);
    CHECK(p.live_packet_.round == 16 && p.live_packet_.actor == 7 && p.live_packet_.warm_before == 10);
    for (unsigned i = 1; i < 8; ++i) CHECK(p.live_packet_.fuel[i] == -1);
    cambc::fixture_fuel = 34000;
    p.live_trace_mark(6, 3, 9);
    cambc::fixture_fuel = 19999;
    p.live_trace_end();
    CHECK(p.live_trace_pending_ && !p.live_trace_active_ && output.str().empty());
    CHECK(p.live_packet_.fuel[7] == 19999 && p.live_packet_.completed_round == 16);
    CHECK(p.live_packet_.ports == 3 && p.live_packet_.sources == 129);
    CHECK(p.live_packet_.x == 4 && p.live_packet_.y == 5);
    const auto saved = p.packet_value();
    cambc::fixture_fuel = 89999; cambc::fixture_round = 17;
    p.live_trace_begin(17);
    CHECK(p.live_trace_pending_ && !p.live_trace_active_ && p.packet_value() == saved);
    CHECK(p.flush_live_trace_turn());
    CHECK(p.live_trace_pending_ && p.packet_value() == saved && output.str().empty());
    p.live_trace_mark(99, 4, 99);
    CHECK(p.packet_value() == saved);
    cambc::fixture_round = 32;
    p.live_trace_begin(32); // A sampled low-fuel entry cannot overwrite the old packet.
    CHECK(p.live_trace_pending_ && !p.live_trace_active_ && p.packet_value() == saved);
    CHECK(p.flush_live_trace_turn());
    CHECK(output.str().empty());
    cambc::fixture_fuel = 90000; cambc::fixture_round = 18;
    CHECK(p.flush_live_trace_turn());
    CHECK(!p.live_trace_pending_ && !p.live_trace_active_ && p.packet_value() == saved);
    CHECK(output.str().find("p7 live r=16 actor=7 done=16 stage=6 exit=9") != std::string::npos);
    CHECK(output.str().find("warm=10:10 fields=3:129") != std::string::npos);
    CHECK(output.str().find("fuel=100000,-1,-1,34000,-1,-1,-1,19999,") != std::string::npos);
    output.str(""); output.clear();
    cambc::fixture_round = 19; cambc::fixture_fuel = 100000;
    CHECK(!p.flush_live_trace_turn());
    CHECK(!p.live_trace_pending_);
    cambc::fixture_round = 32;
    p.live_trace_begin(32);
    CHECK(p.live_trace_active_ && !p.live_trace_pending_);
    CHECK(p.live_packet_.round == 32 && p.live_packet_.completed_round == 0);
    CHECK(p.live_packet_.stage == 0 && p.live_packet_.exit == 0 && p.live_packet_.warm_after == 0);
    CHECK(p.live_packet_.ports == 0 && p.live_packet_.sources == 0);
    CHECK(p.live_packet_.proposal_goal == -1 && p.live_packet_.task_source == -1 && p.live_packet_.farm_source == -1);
    CHECK(p.live_packet_.x == -1 && p.live_packet_.y == -1);
    for (unsigned i = 1; i < 8; ++i) CHECK(p.live_packet_.fuel[i] == -1);
    cambc::fixture_fuel = 40000;
    p.live_trace_mark(5, 4);
    p.live_trace_end();
    CHECK(p.live_trace_pending_ && !p.live_trace_active_ && output.str().empty());
    const auto high_saved = p.packet_value();
    cambc::fixture_fuel = 89999;
    p.live_trace_flush();
    CHECK(p.live_trace_pending_ && output.str().empty() && p.packet_value() == high_saved);
    cambc::fixture_fuel = 90001;
    p.live_trace_flush();
    CHECK(!p.live_trace_pending_ && p.packet_value() == high_saved);
    CHECK(output.str().find("p7 live r=32") != std::string::npos);
    output.str(""); output.clear();
    // End always defers, even when the captured turn retains flush-level fuel.
    cambc::fixture_round = 64; cambc::fixture_fuel = 100000;
    p.live_trace_begin(64);
    p.live_trace_end();
    CHECK(p.live_trace_pending_ && output.str().empty());
    cambc::fixture_fuel = 89999;
    p.live_trace_flush();
    CHECK(p.live_trace_pending_ && output.str().empty());
    cambc::fixture_fuel = 90000;
    p.live_trace_flush();
    CHECK(!p.live_trace_pending_ && output.str().find("p7 live r=64") != std::string::npos);
    output.str(""); output.clear();
    TraceProbe low;
    cambc::fixture_fuel = 100000; cambc::fixture_round = 64;
    low.live_trace_begin(64);
    auto position_before = cambc::position_queries, round_before = cambc::round_queries;
    cambc::fixture_fuel = 999;
    low.live_trace_end();
    CHECK(low.live_trace_pending_ && !low.live_trace_active_ && output.str().empty());
    CHECK(low.live_packet_.completed_round == 0 && low.live_packet_.x == -1 && low.live_packet_.y == -1);
    CHECK(low.live_packet_.ports == 0 && low.live_packet_.sources == 0);
    CHECK(cambc::position_queries == position_before && cambc::round_queries == round_before);
    // Execute the saved outer run() rather than reproducing its dispatch if.
    // The fake Bot inherits the actual saved public flush wrapper above.
    check_saved_outer_dispatch(false, 100000, output);
    check_saved_outer_dispatch(true, 89999, output);
    check_saved_outer_dispatch(true, 90000, output);
    check_saved_outer_dispatch(true, 100000, output);
    std::cout.rdbuf(previous);
    std::printf("{{\\"checks\\":%u,\\"deferred_trace_preserved\\":true,\\"flush_floor\\":90000,\\"flush_turn_skips_turn_and_remember\\":true,\\"scoped_handle_enum\\":true,\\"native_fuel_claim\\":false}}\\n", checks);
}}
''', hashes


def cache_driver_source(runtime: str) -> tuple[str, dict]:
    names = ("inline bitboard around(vec2 p, int r)", "int tile(vec2 p) const",
             "void compute(cambc::distance_field& field, const bitboard& roots)",
             "void prepare_current_field(vec2 origin)")
    definitions = {name: extract(runtime, name) for name in names}
    caller = live_caller(runtime)
    hashes = {name: hashlib.sha256(body.encode()).hexdigest() for name, body in definitions.items()}
    hashes["turn cache/direct-field caller"] = hashlib.sha256(caller.encode()).hexdigest()
    hashes["phase feature macros (disabled)"] = hashlib.sha256(phase_macros(runtime).encode()).hexdigest()
    around = definitions.pop("inline bitboard around(vec2 p, int r)")
    methods = "\n".join(definitions.values())
    return f'''#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "{(COMMON / 'native_types.h').as_posix()}"
{phase_macros(runtime)}
namespace cambc {{
struct vec2 {{ int x = 0, y = 0; }};
namespace cache_double {{
inline constexpr int width = 8, height = 8, count = width * height;
inline unsigned owner_round = 1, fuel = 100000, subject_computes = 0, visible_revision = 1;
inline vec2 physical{{1, 1}}, landing{{5, 5}};
inline bool fail_next = false, suspend_next = false, displace_during_suspend = false;
struct PartialWriteFailed {{}};
}}
class bitboard {{
public:
    std::array<unsigned char, cache_double::count> bits{{}};
    void draw_chebyshev(vec2 p, int radius) {{
        for (int y = std::max(0, p.y - radius); y <= std::min(cache_double::height - 1, p.y + radius); ++y)
            for (int x = std::max(0, p.x - radius); x <= std::min(cache_double::width - 1, p.x + radius); ++x)
                bits[y * cache_double::width + x] = 1;
    }}
    friend bitboard operator&(const bitboard& a, const bitboard& b) {{
        bitboard out;
        for (unsigned i = 0; i < out.bits.size(); ++i) out.bits[i] = a.bits[i] && b.bits[i];
        return out;
    }}
    friend bitboard operator~(const bitboard& a) {{
        bitboard out;
        for (unsigned i = 0; i < out.bits.size(); ++i) out.bits[i] = !a.bits[i];
        return out;
    }}
}};
class distance_field {{
public:
    bool tracked_subject = false; // Measurement only; forced reference computes are separate.
    bitboard walls, roots;
    unsigned cap = 0;
    std::array<std::uint16_t, cache_double::count> values{{}};
    void compute(const bitboard& blocked, const bitboard& origin, bitboard& visited, unsigned maximum) {{
        if (tracked_subject) ++cache_double::subject_computes;
        walls = blocked; roots = origin; cap = maximum;
        values[0] = 60000; // Poison a partial write before the injected SDK error.
        if (tracked_subject && cache_double::fail_next) {{
            cache_double::fail_next = false;
            throw cache_double::PartialWriteFailed{{}};
        }}
        values.fill(65535);
        for (int t = 0; t < cache_double::count; ++t) if (!blocked.bits[t]) {{
            for (int root = 0; root < cache_double::count; ++root) if (origin.bits[root]) {{
                unsigned distance = std::max(std::abs(t % cache_double::width - root % cache_double::width),
                                             std::abs(t / cache_double::width - root / cache_double::width));
                if (distance <= maximum) values[t] = std::min(values[t], static_cast<std::uint16_t>(distance));
            }}
        }}
        visited = ~blocked;
        if (tracked_subject && cache_double::suspend_next) {{
            cache_double::suspend_next = false;
            ++cache_double::owner_round;
            if (cache_double::displace_during_suspend) cache_double::physical = cache_double::landing;
        }}
    }}
    std::uint64_t fingerprint() const {{
        std::uint64_t hash = 1469598103934665603ull;
        auto add = [&](std::uint64_t v) {{ hash = (hash ^ v) * 1099511628211ull; }};
        add(cap);
        for (auto v : roots.bits) add(v);
        for (auto v : walls.bits) add(v);
        for (auto v : values) add(v);
        return hash;
    }}
}};
struct camp {{}};
struct champion {{}};
enum class team {{ ally, enemy }};
namespace vision {{ inline std::vector<champion> champions(team) {{ return {{}}; }} }}
inline unsigned round_number() {{ return cache_double::owner_round; }}
inline unsigned fuel_remaining() {{ return cache_double::fuel; }}
}}
namespace p7::live::native {{
using cambc::vec2; using cambc::bitboard; using cambc::camp; using cambc::champion; using cambc::team;
{around}
inline unsigned checks = 0;
#define CHECK(test) do {{ ++checks; if (!(test)) {{ std::fprintf(stderr, "current field cache assertion line %d: %s\\n", __LINE__, #test); std::abort(); }} }} while (false)
class CacheProbe {{
public:
    int width_ = 8, current_field_root_ = -1, last_tile_ = -1;
    bool current_field_valid_ = false, sample_turn_ = true;
    std::array<unsigned, 8> sample_fuel_{{}};
    bitboard terrain_;
    cambc::distance_field current_field_, movement_field_;
    unsigned frame_calls = 0, geometry_calls = 0, downstream_entries = 0;
    unsigned frame_revision = 0, geometry_revision = 0;
    CacheProbe() {{
        current_field_.tracked_subject = true;
        terrain_.bits[27] = 1;
        movement_field_.cap = 77; movement_field_.roots.bits[63] = 1;
        movement_field_.walls.bits[17] = 1;
        for (unsigned i = 0; i < movement_field_.values.size(); ++i) movement_field_.values[i] = 1234 + i;
    }}
    {methods}
    Frame frame(const std::vector<camp>&, const std::vector<champion>&, const std::vector<champion>&) {{
        ++frame_calls; frame_revision = cambc::cache_double::visible_revision;
        cambc::cache_double::fuel = 45000;
        return {{}};
    }}
    void geometry(Frame&) {{
        ++geometry_calls; geometry_revision = cambc::cache_double::visible_revision;
        cambc::cache_double::fuel = 40000;
    }}
    void dynamic_caller() {{
        const auto round = cambc::round_number();
        struct Actor {{ vec2 position() const {{ return cambc::cache_double::physical; }} }} me;
        std::vector<camp> camps;
        std::vector<champion> enemies;
        {caller}
        ++downstream_entries;
    }}
    void compare_forced(vec2 root) {{
        cambc::distance_field forced;
        compute(forced, around(root, 0)); // The same saved production compute helper.
        CHECK(current_field_.fingerprint() == forced.fingerprint());
    }}
}};
void reset_double() {{
    using namespace cambc::cache_double;
    owner_round = 1; fuel = 100000; subject_computes = 0; visible_revision = 1;
    physical = {{1, 1}}; landing = {{5, 5}};
    fail_next = suspend_next = displace_during_suspend = false;
}}
void stationary_and_new_root() {{
    using namespace cambc::cache_double;
    reset_double(); CacheProbe p;
    const auto movement = p.movement_field_.fingerprint();
    p.dynamic_caller();
    CHECK(subject_computes == 1 && p.current_field_valid_ && p.current_field_root_ == 9);
    CHECK(p.frame_calls == 1 && p.geometry_calls == 1 && p.downstream_entries == 1);
    p.compare_forced(physical);
    const auto stationary = p.current_field_.fingerprint();
    ++owner_round; ++visible_revision; fuel = 100000;
    p.dynamic_caller();
    CHECK(subject_computes == 1 && p.current_field_.fingerprint() == stationary);
    CHECK(p.frame_calls == 2 && p.geometry_calls == 2 && p.downstream_entries == 2);
    CHECK(p.frame_revision == visible_revision && p.geometry_revision == visible_revision);
    CHECK(p.movement_field_.fingerprint() == movement);
    unsigned expected = 1;
    // Ordinary walk, dash-like jump, external displacement and a return to
    // an earlier tile. The post-action receipt deliberately matches NOW.
    for (vec2 next : std::vector<vec2>{{{{2, 1}}, {{4, 1}}, {{5, 5}}, {{1, 1}}}}) {{
        physical = next; p.last_tile_ = p.tile(next);
        ++owner_round; ++visible_revision; fuel = 100000;
        p.dynamic_caller(); ++expected;
        CHECK(subject_computes == expected);
        CHECK(p.current_field_valid_ && p.current_field_root_ == p.tile(next));
        CHECK(p.frame_revision == visible_revision && p.geometry_revision == visible_revision);
        p.compare_forced(next);
        CHECK(p.movement_field_.fingerprint() == movement);
    }}
}}
void partial_write_invalidates_before_retry() {{
    using namespace cambc::cache_double;
    reset_double(); CacheProbe p;
    const auto movement = p.movement_field_.fingerprint();
    p.prepare_current_field({{1, 1}});
    const auto good = p.current_field_.fingerprint();
    fail_next = true;
    try {{ p.prepare_current_field({{2, 1}}); CHECK(false); }}
    catch (const PartialWriteFailed&) {{}}
    CHECK(!p.current_field_valid_ && p.current_field_root_ == 9);
    CHECK(p.current_field_.fingerprint() != good && subject_computes == 2);
    CHECK(p.frame_calls == 0 && p.geometry_calls == 0 && p.downstream_entries == 0);
    // Returning to the OLD recorded root must still rebuild the poisoned field.
    p.prepare_current_field({{1, 1}});
    CHECK(subject_computes == 3 && p.current_field_valid_ && p.current_field_root_ == 9);
    p.compare_forced({{1, 1}});
    p.prepare_current_field({{2, 1}});
    CHECK(subject_computes == 4 && p.current_field_root_ == 10);
    p.compare_forced({{2, 1}});
    CHECK(p.movement_field_.fingerprint() == movement);
}}
void suspended_compute_publishes_old_root_and_fences_caller() {{
    using namespace cambc::cache_double;
    for (bool displaced : {{false, true}}) {{
        reset_double(); CacheProbe p;
        const auto movement = p.movement_field_.fingerprint();
        suspend_next = true; displace_during_suspend = displaced;
        p.dynamic_caller();
        CHECK(owner_round == 2 && subject_computes == 1);
        CHECK(p.current_field_valid_ && p.current_field_root_ == 9);
        CHECK(p.frame_calls == 0 && p.geometry_calls == 0 && p.downstream_entries == 0);
        p.compare_forced({{1, 1}});
        CHECK(p.movement_field_.fingerprint() == movement);
        p.last_tile_ = p.tile(physical); // Cannot substitute for the stored compute root.
        ++visible_revision; fuel = 100000;
        p.dynamic_caller();
        CHECK(subject_computes == (displaced ? 2u : 1u));
        CHECK(p.current_field_valid_ && p.current_field_root_ == p.tile(physical));
        CHECK(p.frame_calls == 1 && p.geometry_calls == 1 && p.downstream_entries == 1);
        CHECK(p.frame_revision == visible_revision && p.geometry_revision == visible_revision);
        p.compare_forced(physical);
        CHECK(p.movement_field_.fingerprint() == movement);
    }}
}}
}}
int main() {{
    p7::live::native::stationary_and_new_root();
    p7::live::native::partial_write_invalidates_before_retry();
    p7::live::native::suspended_compute_publishes_old_root_and_fences_caller();
    std::printf("{{\\"checks\\":%u,\\"cache_matches_forced_roots\\":true,\\"fresh_frame_and_geometry\\":true,\\"partial_write_retry\\":true,\\"suspension_round_fence\\":true,\\"movement_buffer_unchanged\\":true,\\"native_fuel_claim\\":false}}\\n", p7::live::native::checks);
}}
''', hashes


def _run(work: Path) -> dict:
    runtime_path = COMMON / "runtime.h"
    runtime = runtime_path.read_text(encoding="utf-8")
    source, extracted = driver_source(runtime)
    trace_source, trace_extracted = trace_driver_source(runtime)
    cache_source, cache_extracted = cache_driver_source(runtime)
    paths = [runtime_path, Path(__file__), HERE / "phase_driver.py", HERE / "startup_driver.py", HERE / "startup-sdk.h", COMMON / "native_types.h"]
    pinned = {p: p.read_bytes() for p in paths}
    if pinned[runtime_path].decode("utf-8") != runtime:
        raise RuntimeError("runtime changed while extracting production seams")
    work.mkdir(parents=True, exist_ok=True)
    cpp, wasm = work / "production-live.cpp", work / "production-live.wasm"
    cpp.write_text(source, encoding="utf-8", newline="\n")
    ok, log = compile_fixture(cpp, wasm, defines=["P7_LIVE_TRACE=0", "P7_STARTUP_TRACE=0", "P7_REUSE_CURRENT_FIELD=0", "P7_PHASE_TRACE=0"])
    if not ok:
        raise RuntimeError(f"compile: {log}")
    ok, log = run_wasm(wasm)
    if not ok:
        raise RuntimeError(f"production live caller: {log}")
    observed = json.loads(log.splitlines()[-1])
    if observed.get("checks", 0) < 70:
        raise AssertionError("missing production warm/caller assertions")
    trace_cpp, trace_wasm = work / "production-live-trace.cpp", work / "production-live-trace.wasm"
    trace_cpp.write_text(trace_source, encoding="utf-8", newline="\n")
    ok, log = compile_fixture(trace_cpp, trace_wasm, defines=["P7_LIVE_TRACE=1", "P7_PHASE_TRACE=0"])
    if not ok:
        raise RuntimeError(f"trace compile: {log}")
    ok, log = run_wasm(trace_wasm)
    if not ok:
        raise RuntimeError(f"production deferred trace: {log}")
    trace_observed = json.loads(log.splitlines()[-1])
    if trace_observed.get("checks", 0) < 48 or trace_observed.get("flush_floor") != 90000:
        raise AssertionError("missing production deferred trace assertions")
    cache_cpp, cache_wasm = work / "production-field-cache.cpp", work / "production-field-cache.wasm"
    cache_cpp.write_text(cache_source, encoding="utf-8", newline="\n")
    ok, log = compile_fixture(cache_cpp, cache_wasm, defines=["P7_REUSE_CURRENT_FIELD=1", "P7_LIVE_TRACE=0", "P7_STARTUP_TRACE=0", "P7_PHASE_TRACE=0"])
    if not ok:
        raise RuntimeError(f"cache compile: {log}")
    ok, log = run_wasm(cache_wasm)
    if not ok:
        raise RuntimeError(f"production static field cache: {log}")
    cache_observed = json.loads(log.splitlines()[-1])
    if cache_observed.get("checks", 0) < 50:
        raise AssertionError("missing production cache invalidation/caller checks")
    changed = [p.relative_to(ROOT).as_posix() for p, value in pinned.items() if p.read_bytes() != value]
    if changed:
        raise RuntimeError("pinned inputs changed during compilation: " + ", ".join(changed))
    report = {"kind": "production-warm-and-live-caller-sdk-double", "native_fuel_claim": False,
              "input_hashes": {p.relative_to(ROOT).as_posix(): hashlib.sha256(value).hexdigest() for p, value in pinned.items()},
              "extracted_hashes": extracted, "observed": observed,
              "trace_extracted_hashes": trace_extracted, "trace_observed": trace_observed,
              "cache_extracted_hashes": cache_extracted, "cache_observed": cache_observed,
              "limitations": ["Frame and geometry completion are injected; their internal algorithms are not tested.",
                              "SDK field costs and distances are synthetic; native fuel and gameplay activation need native games.",
                              "Pending low-fuel warm false and completed warm false are recorded, not redefined."]}
    (work / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    return report


def run(work: Path, *, slot_held: bool = False) -> dict:
    with nullcontext() if slot_held else Slots(1, quiet=True):
        return _run(work)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--work", type=Path, default=ROOT / f".cache/arena/orchestration/territory-live/fixture/run-{os.getpid()}")
    parser.add_argument("--slot-held", action="store_true", help="caller already holds one arena slot")
    args = parser.parse_args()
    try:
        report = run(args.work, slot_held=args.slot_held)
    except (ValueError, RuntimeError, AssertionError, OSError) as error:
        print(f"FAIL production live driver: {error}")
        return 1
    print(f"PASS {report['observed']['checks']} warm/caller + {report['trace_observed']['checks']} trace + {report['cache_observed']['checks']} cache checks (saved production, synthetic SDK, no native fuel claim)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
