// Exercise the production warmup method against owner-turn admission boundaries.
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <memory>
#include <sstream>
#include "../common/pure.h"
#ifndef PLAN4_MODEL_ENABLED
#define PLAN4_MODEL_ENABLED 1
#endif

namespace cambc {
struct Point { int x, y; };
using handle_champion_t = int;
inline unsigned fuel = 100000, round = 1;
inline int self() { return 7; }
inline unsigned fuel_remaining() { return fuel; }
inline unsigned round_number() { return round; }
enum class team { enemy };
namespace vision {
inline std::vector<int> camps() { return {}; }
inline std::vector<int> champions(team) { return {}; }
}
namespace map { inline int width() { return 3; } inline int height() { return 2; } }
}
struct Bot {
    bool initialized_ = false, movement_initialized_ = false, fixture_warming_ = false;
    bool movement_pending_ = true, throw_turn = false;
    bool fixture_preparing_ = false, macro_active_ = false, throw_planning = false, roll_tick = false;
    int fixture_prepare_round_ = -1;
    int fixture_wait_reason_ = -1, fixture_wait_round_ = -32;
    bool (*fixture_commit_)(void*) = nullptr;
    void* fixture_commit_context_ = nullptr;
    int fixture_warmup_note_ = -1, physical_vertices_ = 7, model_vertices_ = 9;
    int production_requested_farm_group_ = 3, initializations = 0, turns = 0;
    int ticks = 0, aborts = 0, planning_calls = 0, selections = 0;
    const plan4::Information* plan_info = nullptr;
    const plan4::Evaluation* plan_evaluation = nullptr;
    struct Runtime {
        bool enabled = false, supported = true, reported = true, coupling_enabled = false;
        int unsupported_reason = 2;
        void set_enabled(bool value) { enabled = value; }
        struct Evidence { bool supported; int reason; bool reported; bool coupling_enabled; };
        Evidence model_evidence() const { return {supported, supported ? 0 : unsupported_reason, reported, coupling_enabled}; }
    } runtime_model_;
    struct Terrain { std::vector<cambc::Point> positions() const { return {{1,1}}; } } terrain_;
    struct Movement {
        int initializations = 0;
        std::vector<unsigned char> cells;
        void initialize(int width, int height, std::vector<unsigned char> value) {
            assert(width == 3 && height == 2); ++initializations; cells = std::move(value);
        }
    } movement_;
    struct Navigation {
        int begins = 0;
        void begin_turn() { ++begins; }
        int completed_pairs() const { return 2; }
        int pending_fields() const { return 1; }
        int vertex_visits() const { return 7; }
    } navigation_;
    void initialize() { ++initializations; initialized_ = true; }
    void macro_abort(const char*) { ++aborts; macro_active_ = false; }
    void macro_tick() { assert(fixture_preparing_); ++ticks; if (roll_tick) ++cambc::round; }
    void macro_decide(const std::vector<int>&, const std::vector<int>&) {
        ++planning_calls;
        if (throw_planning) throw std::runtime_error("injected-planning-error");
        if (plan_info && plan_evaluation && fixture_accept_evaluation(*plan_info, *plan_evaluation)) {
            assert(!fixture_preparing_); ++selections;
        }
    }
    void turn() {
        assert(fixture_warming_ && !movement_pending_);
        assert(physical_vertices_ == 0 && model_vertices_ == 0);
        assert(production_requested_farm_group_ == -1);
        ++turns;
        if (throw_turn) throw std::runtime_error("injected-turn-error");
    }
#include "../common/fixture-methods.h"
};

#ifndef PLAN4_FIXTURE_POLICY_TEST
int main() {
    Bot bot;
    cambc::fuel = 89999; bot.fixture_warmup();
    assert(!bot.initialized_ && bot.initializations == 0 && bot.turns == 0);
    cambc::fuel = 90000; bot.fixture_warmup();
    assert(bot.initialized_ && bot.initializations == 1 && bot.turns == 0);
    assert(!bot.movement_initialized_ && bot.navigation_.begins == 0);
    cambc::fuel = 79999; bot.fixture_warmup();
    assert(!bot.movement_initialized_ && bot.movement_.initializations == 0);
    cambc::fuel = 80000; bot.fixture_warmup();
    assert(bot.movement_initialized_ && bot.movement_.initializations == 1 && bot.turns == 0);
    assert(bot.movement_.cells == std::vector<unsigned char>({0,0,0,0,1,0}));
    cambc::fuel = 100000; bot.fixture_warmup();
    assert(bot.turns == 1 && bot.navigation_.begins == 1 && !bot.fixture_warming_);
    bot.throw_turn = true;
    try { bot.fixture_warmup(); assert(false); }
    catch (const std::runtime_error&) {}
    assert(bot.turns == 2 && bot.navigation_.begins == 2 && !bot.fixture_warming_);
    assert(bot.initializations == 1 && bot.movement_.initializations == 1);
    std::cout << "plan4 fixture warmup: admission, staged yields, topology and exception cleanup passed\n";

    auto info = std::make_unique<plan4::Information>();
    auto evaluation = std::make_unique<plan4::Evaluation>();
    info->round = int(cambc::round); info->own.id = 7;
    info->own.position = {1,1};
    info->own.alive = true; info->own.health = info->own.maximum_health = 100; info->own.bag = 10;
    info->altar_count = 2; info->altars[0].id = 5; info->altars[1].id = 9;
    info->altars[0].position = {2,2}; info->altars[1].position = {4,4};
    info->travel = {[](plan4::Point, plan4::Point b, const void*) { return b.x == 2 ? 17 : 11; }, nullptr};
    evaluation->menu.count = evaluation->evaluated_count = 4; evaluation->chosen = 0;
    evaluation->menu.jobs[0].kind = evaluation->menu.jobs[1].kind = plan4::JobKind::bank;
    evaluation->menu.jobs[0].altar = 0; evaluation->menu.jobs[1].altar = 1;
    evaluation->menu.jobs[2].kind = plan4::JobKind::renew;
    evaluation->menu.jobs[3].kind = plan4::JobKind::hold;
    bot.plan_info = info.get(); bot.plan_evaluation = evaluation.get();
    struct Commit { int calls = 0; bool success = true, roll = false; unsigned fuel = 40000; } commit;
    const auto callback = [](void* raw) {
        auto& c = *static_cast<Commit*>(raw); ++c.calls;
        if (c.roll) ++cambc::round;
        cambc::fuel = c.fuel;
        return c.success;
    };
    const auto reset = [&] {
        info->round = int(cambc::round); cambc::fuel = 100000;
        commit = {}; bot.runtime_model_.supported = true;
    };
    const auto held = [&] {
        const int selected = bot.selections, calls = commit.calls;
        bot.fixture_prepare(callback, &commit);
        assert(bot.selections == selected && commit.calls == calls);
        assert(!bot.fixture_preparing_ && !bot.fixture_commit_ && !bot.fixture_commit_context_);
    };
    reset(); bot.fixture_prepare(callback, &commit);
    assert(commit.calls == 1 && bot.selections == 1 && bot.runtime_model_.enabled == (PLAN4_MODEL_ENABLED != 0));
    assert(bot.ticks == 1 && bot.planning_calls == 1);
    assert(!bot.fixture_preparing_ && bot.fixture_prepare_round_ == -1 && !bot.fixture_commit_);
    reset(); evaluation->evaluated_count = 3; held(); evaluation->evaluated_count = 4;
    reset(); evaluation->budget_exhausted = true; held(); evaluation->budget_exhausted = false;
    reset(); evaluation->menu.truncated = true; held(); evaluation->menu.truncated = false;
    reset(); info->observation_truncated = true; held(); info->observation_truncated = false;
    reset(); info->public_layout_truncated = true; held(); info->public_layout_truncated = false;
    reset(); evaluation->chosen = -1; held(); evaluation->chosen = 0;
    reset(); evaluation->menu.jobs[1].altar = 0; held(); evaluation->menu.jobs[1].altar = 1;
    reset(); info->altars[1].id = 5; held(); info->altars[1].id = 9;
    reset(); bot.runtime_model_.supported = false; held();
    reset(); --info->round; held();
    reset(); info->own.id = 8; held(); info->own.id = 7;
    reset(); info->own.bag = 0; held(); info->own.bag = 10;
    reset(); info->own.health = 99; held(); info->own.health = 100;
    reset(); info->own.channel = plan4::Channel::tribute; held(); info->own.channel = plan4::Channel::none;
    reset(); cambc::fuel = 39999; held();
    reset(); commit.success = false; bot.fixture_prepare(callback, &commit);
    assert(commit.calls == 1 && bot.selections == 1);
    reset(); commit.roll = true; bot.fixture_prepare(callback, &commit);
    assert(commit.calls == 1 && bot.selections == 1);
    reset(); commit.fuel = 29999; bot.fixture_prepare(callback, &commit);
    assert(commit.calls == 1 && bot.selections == 1);
    reset(); commit.fuel = 30000; bot.fixture_prepare(callback, &commit);
    assert(commit.calls == 1 && bot.selections == 2);
    reset(); bot.roll_tick = true; const int planned = bot.planning_calls;
    bot.fixture_prepare(callback, &commit); bot.roll_tick = false;
    assert(commit.calls == 0 && bot.planning_calls == planned && bot.selections == 2);
    reset(); bot.throw_planning = true;
    try { bot.fixture_prepare(callback, &commit); assert(false); }
    catch (const std::runtime_error&) {}
    bot.throw_planning = false;
    assert(!bot.fixture_preparing_ && !bot.fixture_commit_ && bot.fixture_prepare_round_ == -1);
    assert(bot.fixture_accept_evaluation(*info, *evaluation)); // ordinary execution has no fixture gate
    reset(); bot.runtime_model_.supported = false;
    bot.fixture_wait_reason_ = -1; bot.fixture_wait_round_ = -32;
    std::ostringstream trace; auto* original = std::cout.rdbuf(trace.rdbuf());
    const auto first_round = cambc::round;
    for (unsigned elapsed : {0u,0u,4u,28u,32u}) {
        cambc::round = first_round + elapsed; info->round = int(cambc::round);
        bot.fixture_prepare(callback, &commit);
    }
    info->observation_truncated = true; // a changed first failed guard emits immediately
    bot.fixture_prepare(callback, &commit);
    std::cout.rdbuf(original); info->observation_truncated = false;
    const auto text = trace.str(); std::size_t cursor = 0; int rows = 0;
    while ((cursor = text.find("plan4 fixture wait ", cursor)) != std::string::npos) { ++rows; ++cursor; }
    assert(rows == 3 && text.find("reason=13 banks=3 menu=4 evaluated=4 chosen=0 modelReason=2") != std::string::npos);
    assert(text.find("reason=5 ") != std::string::npos && commit.calls == 0);
    assert(text.find("accessible=3 distance0=17 distance1=11 navPairs=2 navPending=1 navVisits=7") != std::string::npos);
    std::cout << "plan4 fixture preparation: fresh complete evaluation, callback ordering and exception cleanup passed\n";
}
#endif
