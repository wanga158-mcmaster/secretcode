// SDK-free fake surface around the actual common movement/repair methods.
// This checks adapter ordering and continuation, not engine legality or fuel use.
#include "../covenant/src/runtime.h"
#include "../common/movement.h"
#include "../common/telemetry.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>

struct vec2 {
    int x = 0, y = 0;
    bool operator==(const vec2&) const = default;
};
struct bitboard {
    static constexpr int width = 6, height = 5;
    std::array<bool, width * height> cells{};
    static bool inside(vec2 p) { return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height; }
    bool is_set(vec2 p) const { return inside(p) && cells[p.y * width + p.x]; }
    void set(vec2 p, bool value = true) { assert(inside(p)); cells[p.y * width + p.x] = value; }
    bool is_empty() const { return std::none_of(cells.begin(), cells.end(), [](bool b) { return b; }); }
    std::vector<vec2> positions() const {
        std::vector<vec2> out;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
            if (is_set({x,y})) out.push_back({x,y});
        return out;
    }
    bitboard& operator|=(const bitboard& other) {
        for (unsigned n = 0; n < cells.size(); ++n) cells[n] = cells[n] || other.cells[n];
        return *this;
    }
    bitboard& operator&=(const bitboard& other) {
        for (unsigned n = 0; n < cells.size(); ++n) cells[n] = cells[n] && other.cells[n];
        return *this;
    }
    friend bitboard operator&(bitboard a, const bitboard& b) { return a &= b; }
    friend bitboard operator~(bitboard a) { for (auto& cell : a.cells) cell = !cell; return a; }
};

namespace fake {
int round = 10;
unsigned fuel = 200000;
bitboard visible, occupants, terrain;
std::vector<char> events;
std::vector<vec2> attempted;
bool hidden_failure = false;
enum class Interrupt { none, fuel, round } interrupt = Interrupt::none;
void reset() {
    round = 10; fuel = 200000; visible = ~bitboard{}; occupants = {}; terrain = {};
    events.clear(); attempted.clear(); hidden_failure = false; interrupt = Interrupt::none;
}
}
namespace cambc {
using handle_champion_t = int;
struct Champion {
    vec2 at{0,2};
    bool ready = true;
    explicit operator handle_champion_t() const { return 3; }
    vec2 position() const { return at; }
    bool can_move() const { return ready; }
    bool move_absolute(vec2 destination) {
        assert(ready && bitboard::inside(destination));
        assert(std::max(std::abs(at.x-destination.x),std::abs(at.y-destination.y)) == 1);
        assert(!fake::occupants.is_set(destination) && !fake::terrain.is_set(destination));
        fake::events.push_back('M'); fake::attempted.push_back(destination);
        if (fake::hidden_failure) {
            fake::occupants.set(destination); fake::hidden_failure = false; return false;
        }
        at = destination; ready = false; return true;
    }
};
Champion champion;
Champion& self() { return champion; }
int round_number() { return fake::round; }
unsigned fuel_remaining() { return fake::fuel; }
namespace vision {
bitboard visible_tiles() { return fake::visible; }
bitboard champion_tiles() { return fake::occupants; }
bitboard camp_tiles() { return {}; }
bitboard teleport_tiles() { return {}; }
}
namespace map {
int width() { return bitboard::width; }
int height() { return bitboard::height; }
}
}

namespace {
using plan4::Point;
namespace rt = plan4::runtime;
int metric(Point a, Point b, const void*) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
struct Navigation {
    const bitboard* terrain;
    void seal() {}
    int vertex_visits() const { return 0; }
    rt::PublicTopology topology() const {
        return {bitboard::width, bitboard::height, true, [](Point p, const void* raw) {
            const auto& walls = *static_cast<const bitboard*>(raw);
            return bitboard::inside({p.x,p.y}) && !walls.is_set({p.x,p.y});
        }, terrain};
    }
};

#define PLAN4_ONLINE_EVIDENCE
#define PLAN4_REFERENCE_VERSION "alternate-hooks-fixture"
struct Bot {
    bitboard terrain_;
    Navigation navigation_{&terrain_};
    plan4::PersistentMovement movement_;
    plan4::detour::Cursor alternate_route_;
    rt::Covenant runtime_model_;
    std::unique_ptr<plan4::Information> information_storage_ = std::make_unique<plan4::Information>();
    std::unique_ptr<plan4::Evaluation> evaluation_storage_ = std::make_unique<plan4::Evaluation>();
    plan4::Job macro_job_{};
    bool macro_active_ = true, macro_second_ = false;
    bool movement_initialized_ = false, movement_pending_ = false;
    unsigned decision_sequence_ = 1, optional_floor_ = 50000, movement_floor_ = 35000;
    int physical_vertices_ = 0, model_vertices_ = 0;
    int production_requested_farm_group_ = -1, production_requested_bank_ = -1;
    int cancellations = 0, resumes = 0, movement_rows = 0;

    explicit Bot(bool enabled, bool farm = false) : runtime_model_(enabled) {
        fake::reset(); cambc::champion = {};
        // Every ordinary shortest-distance neighbor is occupied. A route must
        // first move sideways to (0,1) or (0,3), outside the shortest policy.
        for (int y = 1; y <= 3; ++y) fake::occupants.set({1,y});
        terrain_.set({5,2}); // Actual altar centre, never a legal landing tile.
        fake::terrain = terrain_;
        auto& info = *information_storage_;
        info.round = fake::round; info.own.id = 3; info.own.position = {0,2};
        info.own.health = info.own.maximum_health = 100; info.own.bag = 24;
        info.travel = {metric,nullptr}; info.altar_count = 1; info.group_count = 1;
        info.altars[0].id = 30; info.altars[0].position = {4,2};
        info.altars[0].altar_position = {5,2}; info.altars[0].lock_known = true;
        auto& group = info.groups[0]; group.id = 17; group.position = {4,3};
        group.health = group.full_health = 100; group.clear_rounds = group.full_clear_rounds = 2;
        group.visible = group.occupancy_known = group.all_spawns_visible = true;
        group.lifecycle = plan4::Lifecycle::living;
        group.provenance = group.health_provenance = {fake::round,plan4::Source::personal_vision,plan4::Status::exact_now};
        macro_job_.kind = farm ? plan4::JobKind::farm : plan4::JobKind::bank;
        macro_job_.group = farm ? 0 : -1; macro_job_.altar = 0;
        if (farm) production_requested_farm_group_ = group.id;
        else production_requested_bank_ = 0;
        evaluation_storage_->menu.count = 1; evaluation_storage_->menu.jobs[0] = macro_job_;
        runtime_model_.observe_snapshot(info);
        runtime_model_.selected(info,macro_job_,decision_sequence_,true);
    }
    bitboard blocked() const { auto walls = terrain_; walls |= fake::occupants; return walls; }
    static bool movement_budget(const void* floor) { return cambc::fuel_remaining() >= *static_cast<const unsigned*>(floor); }
    void macro_phase(const char*, unsigned, const plan4::MovementStep*, bool, unsigned, unsigned) { ++movement_rows; }
    void production_cancel(rt::PhaseCensor censor) {
        assert(censor == rt::PhaseCensor::interrupted);
        ++cancellations; fake::events.push_back('C');
        if (fake::interrupt == fake::Interrupt::fuel) fake::fuel = 0;
        if (fake::interrupt == fake::Interrupt::round) ++fake::round;
    }
    void production_begin_travel_mask(bitboard goals, int distance, int group) {
        assert(goals.is_set({4,2}) && distance > 0);
        assert(group == production_requested_farm_group_);
        ++resumes; fake::events.push_back('B');
    }
    void production_move_result(vec2, bool, int round) {
        assert(round == fake::round); fake::events.push_back('R');
    }
    void next_turn() {
        ++fake::round; cambc::champion.ready = true;
        physical_vertices_ = model_vertices_ = 0;
        auto& info = *information_storage_; info.round = fake::round;
        const auto at = cambc::self().position(); info.own.position = {at.x,at.y};
        info.groups[0].provenance.observed_round = info.groups[0].health_provenance.observed_round = fake::round;
        runtime_model_.observe_snapshot(info);
    }
    bitboard goal() const { bitboard goals; goals.set({4,2}); return goals; }
    Point cached_next() {
        const auto at = cambc::self().position(); Point next; int remaining;
        const std::array<Point,1> destinations{{{4,2}}};
        assert(alternate_route_.next(decision_sequence_,3,fake::round,{at.x,at.y},destinations,next,remaining));
        return next;
    }
#include "../common/alternate-methods.h"
#include "../common/movement-methods.h"
};
#undef PLAN4_ONLINE_EVIDENCE

void repaired_action_and_continuation(bool enabled, bool farm) {
    auto bot = std::make_unique<Bot>(enabled,farm);
    const auto original_sequence = bot->runtime_model_.monitor().contract().phase_sequence;
    assert(bot->macro_move(bot->goal(),nullptr));
    const auto first = cambc::self().position();
    assert(first.x == 0 && std::abs(first.y-2) == 1);
    assert(metric({first.x,first.y},{4,2},nullptr) >= metric({0,2},{4,2},nullptr));
    assert(bot->alternate_route_.active() && bot->cancellations == 1 && bot->resumes == 1);
    assert((fake::events == std::vector<char>{'C','M','B','R'}));
    const auto& contract = bot->runtime_model_.monitor().contract();
    assert(!contract.failed && contract.phase_sequence == original_sequence+1);
    const auto sequence = contract.phase_sequence, epoch = contract.epoch;
    const int deadline = contract.deadline;
    const unsigned generation = bot->movement_.cache_generation();
    bot->next_turn(); const Point expected = bot->cached_next();
    assert(bot->macro_move(bot->goal(),nullptr));
    assert((cambc::self().position() == vec2{expected.x,expected.y}));
    assert(bot->alternate_route_.active() && bot->cancellations == 1);
    assert(contract.phase_sequence == sequence && contract.epoch == epoch && contract.deadline == deadline);
    assert(bot->movement_.cache_generation() == generation && bot->movement_rows == 2);
    assert(fake::attempted.size() == 2);
    for (int turns = 0; bot->alternate_route_.active(); ++turns) {
        assert(turns < plan4::detour::max_steps);
        bot->next_turn(); const Point next = bot->cached_next();
        assert(bot->macro_move(bot->goal(),nullptr));
        assert((cambc::self().position() == vec2{next.x,next.y}));
        assert(bot->cancellations == 1 && contract.phase_sequence == sequence && contract.deadline == deadline);
    }
    assert((cambc::self().position() == vec2{4,2}));
    assert(fake::attempted.size() == 5 && bot->movement_rows == 5);
}

void unknown_stale_and_protected(bool enabled) {
    {
        auto bot = std::make_unique<Bot>(enabled);
        for (int y = 1; y <= 3; ++y) fake::visible.set({1,y},false);
        assert(!bot->macro_move(bot->goal(),nullptr));
        assert(!bot->alternate_route_.active() && bot->cancellations == 0 && fake::attempted.empty());
    }
    for (int stale = 0; stale < 3; ++stale) {
        auto bot = std::make_unique<Bot>(enabled);
        if (stale == 0) --bot->information_storage_->round;
        if (stale == 1) ++bot->decision_sequence_;
        if (stale == 2) bot->production_requested_bank_ = -1;
        assert(!bot->macro_move(bot->goal(),nullptr));
        assert(bot->cancellations == 0 && !bot->alternate_route_.active() && fake::attempted.empty());
    }
    for (bool channel : {false,true}) {
        auto bot = std::make_unique<Bot>(enabled);
        if (channel) bot->information_storage_->own.channel = plan4::Channel::tribute;
        else bot->information_storage_->in_progress_group_id = 17;
        // The channel surface reports no allowance; capture protection is
        // independently enforced by the actual runtime repair gate.
        if (channel) cambc::champion.ready = false;
        assert(!bot->macro_move(bot->goal(),nullptr));
        assert(bot->cancellations == 0 && !bot->alternate_route_.active() && fake::attempted.empty());
    }
}

void failed_action_does_not_renew(bool enabled) {
    auto bot = std::make_unique<Bot>(enabled);
    const auto before = bot->runtime_model_.monitor().contract();
    fake::hidden_failure = true;
    assert(!bot->macro_move(bot->goal(),nullptr));
    const auto& after = bot->runtime_model_.monitor().contract();
    assert(bot->cancellations == 1 && bot->resumes == 0 && !bot->alternate_route_.active());
    assert((fake::events == std::vector<char>{'C','M','R'}));
    assert(after.failed && after.failed_cause == plan4::covenant::Cause::approach_obstruction);
    assert(after.deadline == before.deadline && after.epoch == before.epoch && after.phase_sequence == before.phase_sequence);
    assert((cambc::self().position() == vec2{0,2}));
}

void fresh_obstacle_and_interruptions(bool enabled) {
    {
        auto bot = std::make_unique<Bot>(enabled);
        assert(bot->macro_move(bot->goal(),nullptr)); bot->next_turn();
        const Point cached = bot->cached_next(); fake::occupants.set({cached.x,cached.y});
        fake::attempted.clear();
        bot->macro_move(bot->goal(),nullptr);
        for (vec2 attempted : fake::attempted) assert(attempted != (vec2{cached.x,cached.y}));
        // The old cached step is forbidden. A newly proven route may remain
        // active, but it must have performed the normal interruption again.
        if (bot->alternate_route_.active()) assert(bot->cancellations == 2);
    }
    for (auto interruption : {fake::Interrupt::fuel,fake::Interrupt::round}) {
        auto bot = std::make_unique<Bot>(enabled);
        fake::interrupt = interruption;
        const auto before = bot->runtime_model_.monitor().contract();
        assert(!bot->macro_move(bot->goal(),nullptr));
        assert(bot->cancellations == 1 && bot->resumes == 0 && fake::attempted.empty());
        assert(!bot->alternate_route_.active());
        const auto& after = bot->runtime_model_.monitor().contract();
        assert(after.deadline == before.deadline && after.phase_sequence == before.phase_sequence);
    }
}
}

int main() {
    for (bool enabled : {true,false}) {
        repaired_action_and_continuation(enabled,false);
        repaired_action_and_continuation(enabled,true);
        unknown_stale_and_protected(enabled);
        failed_action_does_not_renew(enabled);
        fresh_obstacle_and_interruptions(enabled);
    }
    std::puts("plan4 alternate hooks: actual common-method fixtures passed (enabled/control)");
}
