// Actual common acquisition methods over a narrow Bot and the SDK collector.
// This checks delivery order and selection; consumer policy is tested separately.
#include "../common/own-acquisition-sdk.h"
#include "../covenant/src/runtime.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace rt = plan4::runtime;
namespace mock = cambc::mock;
using plan4::Information;

namespace fake {
struct Log {
    int actor = -1, round = -1, group = -1, origin = -1;
    unsigned received = 0;
    bool attributed = false, accepted = false;
};
inline std::vector<Log> logs;
}
namespace plan4 {
// Only the telemetry surface used by the included production method is doubled.
class LogRow {
    fake::Log row_{};
public:
    LogRow(const char* event, const char*) { assert(std::strcmp(event, "capture-evidence") == 0); }
    template<class Value> LogRow& field(const char* key, Value value) {
        if (std::strcmp(key, "actor") == 0) row_.actor = static_cast<int>(value);
        else if (std::strcmp(key, "round") == 0) row_.round = static_cast<int>(value);
        else if (std::strcmp(key, "group") == 0) row_.group = static_cast<int>(value);
        else if (std::strcmp(key, "origin") == 0) row_.origin = static_cast<int>(value);
        else if (std::strcmp(key, "received") == 0) row_.received = static_cast<unsigned>(value);
        else if (std::strcmp(key, "attributed") == 0) row_.attributed = static_cast<bool>(value);
        else if (std::strcmp(key, "accepted") == 0) row_.accepted = static_cast<bool>(value);
        return *this;
    }
    void emit() { fake::logs.push_back(row_); }
};
}
#define PLAN4_REFERENCE_VERSION "acquisition-hook-fixture"

namespace {
struct OnlineCapture { rt::CapturedGroup receipt; };
struct OnlineEvidence {
    rt::MemberFrame frame{};
    std::vector<OnlineCapture> captures;
    const rt::MemberFrame& members() const { return frame; }
    int capture_count() const { return static_cast<int>(captures.size()); }
    const OnlineCapture& capture(int n) const { return captures[static_cast<std::size_t>(n)]; }
};
struct Runtime {
    const Information* expected_info = nullptr;
    const rt::OwnStockFrame* expected_stock = nullptr;
    const rt::CapturedGroup* expected_receipt = nullptr;
    const rt::MemberFrame* expected_members = nullptr;
    const Information* snapshot = nullptr;
    rt::OwnStockFrame stock{};
    rt::CapturedGroup delivered{};
    std::vector<char> events;
    int stock_observations = 0, snapshots = 0, receipts = 0;
    bool accept = true;

    void expect(const Information& info, const rt::OwnAcquisitionCollector& c,
                const OnlineEvidence& online) {
        expected_info = &info; expected_stock = &c.frame();
        expected_receipt = c.capture(); expected_members = &online.frame;
    }
    void observe_own_stock(const rt::OwnStockFrame& frame) {
        assert(&frame == expected_stock);
        assert(events.empty() || events.back() == 'S' || events.back() == 'C');
        stock = frame; ++stock_observations; events.push_back('O');
    }
    void observe_snapshot(const Information& info, const rt::MemberFrame& members) {
        assert(&info == expected_info && &members == expected_members);
        assert(!events.empty() && events.back() == 'O');
        if (stock.known) {
            assert(stock.clock.round == info.round && stock.clock.own.id == info.own.id);
            assert(stock.bag == info.own.bag);
        }
        snapshot = &info; ++snapshots; events.push_back('S');
    }
    bool capture_receipt(const Information& info, const rt::CapturedGroup& receipt) {
        assert(&info == expected_info && snapshot == &info && &receipt == expected_receipt);
        assert(!events.empty() && events.back() == 'S');
        assert(receipt.origin == rt::CaptureOrigin::isolated_own_stock);
        assert(receipt.actor_attribution_known && receipt.actor_id == info.own.id);
        assert(receipt.round == info.round && receipt.bag_after == info.own.bag);
        assert(stock.known && stock.stock_epoch == receipt.stock_epoch);
        delivered = receipt; ++receipts; events.push_back('C'); return accept;
    }
};
struct Navigation {
    int lookups = 0;
    int lookup() { ++lookups; return 73; }
};
struct Observer {
    const Information* next = nullptr;
    bool success = true, drift = false;
    int calls = 0;
    unsigned floor = 0;
    bool observe_into(Information& info, int lookup, unsigned minimum) {
        ++calls; floor = minimum; assert(lookup == 73 && next);
        if (!success) return false;
        info = *next;
        if (drift) ++mock::round;
        return true;
    }
};
template<class Model> struct BotHarness {
    Model runtime_model_;
    OnlineEvidence online_evidence_;
    Observer observer_;
    Navigation navigation_;
    std::unique_ptr<Information> information_storage_;
    plan4::Job macro_job_{plan4::JobKind::farm, 0, 1, -1};
    bool macro_active_ = true, macro_second_ = false;
    BotHarness() = default;
    explicit BotHarness(bool enabled) : runtime_model_(enabled) {}
    static bool macro_budget(const void* floor) {
        return cambc::fuel_remaining() >= *static_cast<const unsigned*>(floor);
    }
#include "../common/acquisition-methods.h"

    void expect(const Information& info) {
        if constexpr (requires { runtime_model_.expect(info, own_acquisition_, online_evidence_); })
            runtime_model_.expect(info, own_acquisition_, online_evidence_);
    }
    void observe(const Information& info) {
        expect(info);
        macro_acquisition_observe(info);
    }
    void hold() {
        if (information_storage_) expect(*information_storage_);
        macro_acquisition_hold();
    }
    // Construct the real DTO immediately after the actual common helper. The
    // production macro_decide call position is checked independently in source.
    rt::ExecutionRepairFrame repair_frame(const Information& info) const {
        rt::ExecutionRepairFrame frame;
        frame.clock = {info.round, {info.own.id}};
        frame.information = &info; frame.selected_job = macro_job_;
        frame.remaining_group = macro_second_ ? macro_job_.second_group : macro_job_.group;
        frame.protected_progress = info.own.channel != plan4::Channel::none || info.in_progress_group_id >= 0;
        return frame;
    }
};
using Bot = BotHarness<Runtime>;
struct Entry { cambc::map::camp_group group; cambc::bitboard spawns; };
struct Fixture {
    std::vector<Entry> entries;
    std::vector<int> lookup;
    Fixture() : lookup(mock::board_width * mock::board_height, -1) {
        mock::reset(); fake::logs.clear();
        cambc::bitboard spawns; spawns.set(mock::own.position);
        entries.push_back({cambc::map::camp_group{42}, spawns});
        lookup[static_cast<std::size_t>(mock::tile(mock::own.position))] = 0;
    }
    template<class Model> void start(BotHarness<Model>& b) const { b.own_acquisition_.start(entries, lookup, mock::board_width); }
    template<class Model> void finish(BotHarness<Model>& b) const { b.own_acquisition_.finish(entries, lookup, mock::board_width); }
    template<class Model> void anchor(BotHarness<Model>& b) const {
        mock::group(42).state = 0; start(b);
        mock::group(42).state = 1; finish(b);
    }
    template<class Model> void reward(BotHarness<Model>& b) const {
        mock::round = 2; mock::fuel = 100000; mock::own.bag = 12;
        mock::group(42).state = 2; start(b);
    }
    static std::unique_ptr<Information> information() {
        auto i = std::make_unique<Information>();
        i->round = mock::round; i->own.id = mock::own.id;
        i->own.bag = mock::own.bag; i->own.position = {mock::own.position.x, mock::own.position.y};
        i->group_count = 2;
        i->groups[0].id = 42; i->groups[0].gems = 24;
        i->groups[0].lifecycle = mock::group(42).state == 0 ? plan4::Lifecycle::living :
            mock::group(42).state == 1 ? plan4::Lifecycle::cleared : plan4::Lifecycle::respawning;
        i->groups[0].visible = true; i->groups[1].id = 43;
        return i;
    }
};
void no_hidden_bodies() {
    assert(mock::unauthorized == 0);
    for (auto q : {mock::Query::enemy_position, mock::Query::enemy_bag,
                   mock::Query::enemy_state, mock::Query::enemy_visible}) assert(mock::count(q) == 0);
}
void ordered(const Runtime& r, const char* expected) {
    assert(r.events == std::vector<char>(expected, expected + std::strlen(expected)));
}

[[gnu::noinline]] void measured_delivery(int& cases) {
    Fixture f; Bot b; f.anchor(b); f.reward(b);
    auto info = Fixture::information();
    assert(b.own_acquisition_.capture() && b.own_acquisition_.capture()->received_gems == 12);
    b.observe(*info); ordered(b.runtime_model_, "OSC");
    assert(b.runtime_model_.snapshots == 1 && b.runtime_model_.receipts == 1);
    assert(b.runtime_model_.snapshot == info.get());
    assert(b.runtime_model_.delivered.received_gems == 12 && info->groups[0].gems == 24);
    assert(fake::logs.size() == 1 && fake::logs[0].attributed && fake::logs[0].accepted);
    assert(fake::logs[0].actor == 7 && fake::logs[0].round == 2 && fake::logs[0].group == 42);
    assert(fake::logs[0].received == 12 && fake::logs[0].origin == int(rt::CaptureOrigin::isolated_own_stock));
    const auto frame = b.repair_frame(*info);
    assert(b.macro_second_ && frame.remaining_group == 1 && frame.information == info.get()); ++cases;

    // Re-observing the same planning round refreshes its snapshot but cannot
    // replay a receipt or its evidence row, even while the collector retains it.
    b.observe(*info); ordered(b.runtime_model_, "OSCOS");
    assert(b.runtime_model_.stock_observations == 2 && b.runtime_model_.snapshots == 2);
    assert(b.runtime_model_.receipts == 1 && fake::logs.size() == 1); ++cases;

    // An active contract also receives a fresh no-capture snapshot next round.
    mock::own.position = {1, 1}; mock::visible.fill(false); f.finish(b);
    mock::round = 3; mock::fuel = 100000; f.start(b);
    auto next = Fixture::information(); assert(!b.own_acquisition_.capture());
    next->groups[0].visible = false;
    b.observe(*next); ordered(b.runtime_model_, "OSCOSOS");
    assert(b.runtime_model_.snapshot == next.get() && b.runtime_model_.snapshots == 3);
    assert(b.runtime_model_.stock.clock.round == 3 && b.runtime_model_.stock.count == 1);
    assert(b.runtime_model_.receipts == 1 && fake::logs.size() == 1); ++cases;
    no_hidden_bodies();
}

[[gnu::noinline]] void stale_snapshots(int& cases) {
    Fixture f; Bot b; f.anchor(b); f.reward(b); auto info = Fixture::information();
    for (int round : {-1, 1, 3}) {
        info->round = round; b.observe(*info);
        assert(b.runtime_model_.events.empty() && fake::logs.empty());
        assert(!b.macro_second_ && b.acquisition_delivered_round_ == -1); ++cases;
    }
    info->round = 2; b.observe(*info); ordered(b.runtime_model_, "OSC");
    assert(b.runtime_model_.receipts == 1 && b.acquisition_delivered_round_ == 2); ++cases;
}

[[gnu::noinline]] void no_weak_receipts(int& cases) {
    for (int scenario = 0; scenario < 3; ++scenario) {
        Fixture f; Bot b;
        if (scenario != 0) {
            if (scenario == 1) mock::enemies[0].alive = false;
            if (scenario == 2) mock::roster.clear();
            f.anchor(b);
        }
        f.reward(b); auto info = Fixture::information();
        // Lifecycle evidence may describe a real occurrence while its owner
        // remains unknown; none of these rows may enter the own consumer.
        for (auto origin : {rt::CaptureOrigin::lifecycle_boundaries, rt::CaptureOrigin::unknown}) {
            rt::CapturedGroup row;
            row.group_id = 42; row.round = 2; row.respawn_rounds = 30;
            row.origin = origin; row.personally_witnessed = true;
            b.online_evidence_.captures.push_back({row});
        }
        assert(!b.own_acquisition_.capture());
        b.observe(*info); ordered(b.runtime_model_, "OS");
        assert(b.runtime_model_.receipts == 0 && fake::logs.size() == 2);
        for (const auto& log : fake::logs) assert(!log.attributed && !log.accepted);
        b.observe(*info); ordered(b.runtime_model_, "OSOS"); assert(fake::logs.size() == 2);
        no_hidden_bodies(); ++cases;
    }
    Fixture f; Bot b; f.anchor(b); mock::enemies[0].alive = false; f.reward(b);
    auto info = Fixture::information(); assert(!b.own_acquisition_.capture());
    b.observe(*info); ordered(b.runtime_model_, "OS");
    assert(b.runtime_model_.receipts == 0 && fake::logs.empty()); no_hidden_bodies(); ++cases;
}

[[gnu::noinline]] void rejected_delivery(int& cases) {
    Fixture f; Bot b; f.anchor(b); f.reward(b); auto info = Fixture::information();
    b.runtime_model_.accept = false; b.observe(*info); ordered(b.runtime_model_, "OSC");
    assert(b.runtime_model_.receipts == 1 && fake::logs.size() == 1);
    assert(fake::logs[0].attributed && !fake::logs[0].accepted);
    b.observe(*info); ordered(b.runtime_model_, "OSCOS");
    assert(b.runtime_model_.receipts == 1 && fake::logs.size() == 1); ++cases;
}

[[gnu::noinline]] void selection(int& cases) {
    // Without a capture receipt, selection still follows a fresh personally
    // visible lifecycle observation, after the helper refreshed the model.
    for (int scenario = 0; scenario < 20; ++scenario) {
        Fixture f; Bot b; auto info = Fixture::information();
        info->groups[0].lifecycle = plan4::Lifecycle::respawning;
        switch (scenario) {
            case 0: break;
            case 1: info->groups[0].lifecycle = plan4::Lifecycle::captured; break;
            case 2: b.macro_active_ = false; break;
            case 3: b.macro_job_.kind = plan4::JobKind::bank; break;
            case 4: b.macro_job_.kind = plan4::JobKind::renew; break;
            case 5: b.macro_job_.kind = plan4::JobKind::hold; break;
            case 6: b.macro_job_.group = -1; break;
            case 7: b.macro_job_.group = info->group_count; break;
            case 8: b.macro_job_.second_group = -1; break;
            case 9: b.macro_job_.second_group = info->group_count; break;
            case 10: info->groups[0].visible = false; break;
            case 11: info->groups[0].lifecycle = plan4::Lifecycle::living; break;
            case 12: info->groups[0].lifecycle = plan4::Lifecycle::cleared; break;
            case 13: info->groups[0].lifecycle = plan4::Lifecycle::unknown; break;
            case 14: info->own.channel = plan4::Channel::recall; break;
            case 15: info->own.channel = plan4::Channel::tribute; break;
            case 16: info->own.channel = plan4::Channel::other; break;
            case 17: info->in_progress_group_id = 42; break;
            case 18: b.macro_second_ = true; info->groups[0].visible = false; break;
            case 19: b.macro_job_.second_group = b.macro_job_.group; break;
        }
        b.observe(*info); ordered(b.runtime_model_, "OS");
        const bool expected_second = scenario < 2 || scenario == 18;
        assert(b.macro_second_ == expected_second);
        const auto frame = b.repair_frame(*info);
        assert(frame.remaining_group == (expected_second ? b.macro_job_.second_group : b.macro_job_.group));
        assert(frame.information == info.get() && frame.clock.round == info->round);
        assert(b.runtime_model_.snapshot == info.get() && b.runtime_model_.receipts == 0); ++cases;
        if (scenario >= 14 && scenario <= 17) {
            // The pending phase can end after an earlier planning snapshot;
            // receipt deduplication must not suppress selection synchronization.
            info->own.channel = plan4::Channel::none; info->in_progress_group_id = -1;
            b.observe(*info); ordered(b.runtime_model_, "OSOS");
            assert(b.macro_second_ && b.repair_frame(*info).remaining_group == 1); ++cases;
        }
    }
}

[[gnu::noinline]] void hold_observation(int& cases) {
    for (int scenario = 0; scenario < 12; ++scenario) {
        Fixture f; Bot b; f.anchor(b); auto current = Fixture::information();
        b.information_storage_ = std::make_unique<Information>();
        b.information_storage_->round = -1; b.information_storage_->own.bag = 999;
        b.observer_.next = current.get(); mock::fuel = 100000;
        switch (scenario) {
            case 0: break;
            case 1: mock::fuel = 64999; break;
            case 2: b.macro_active_ = false; break;
            case 3: b.macro_job_.kind = plan4::JobKind::bank; break;
            case 4: b.macro_job_.kind = plan4::JobKind::renew; break;
            case 5: b.macro_job_.kind = plan4::JobKind::hold; break;
            case 6: b.information_storage_.reset(); break;
            case 7: b.observer_.success = false; break;
            case 8: b.observer_.drift = true; break;
            case 9: current->round = 0; break;
            case 10: mock::fuel = 65000; break;
            case 11: mock::fuel = 300000; break;
        }
        const int before = mock::fuel, queries = mock::queries;
        const bool active = b.macro_active_, second = b.macro_second_;
        const auto job = b.macro_job_;
        b.hold();
        const bool invoked = scenario == 0 || scenario >= 7;
        const bool delivered = scenario == 0 || scenario >= 10;
        assert(b.observer_.calls == int(invoked) && b.navigation_.lookups == int(invoked));
        assert(mock::queries == queries && mock::fuel == before);
        assert(b.macro_active_ == active && b.macro_second_ == second);
        assert(b.macro_job_.kind == job.kind && b.macro_job_.group == job.group &&
            b.macro_job_.second_group == job.second_group && b.macro_job_.altar == job.altar);
        assert(b.runtime_model_.receipts == 0 && fake::logs.empty());
        if (delivered) {
            ordered(b.runtime_model_, "OS");
            assert(b.runtime_model_.snapshot == b.information_storage_.get());
            assert(b.information_storage_->round == 1 && b.information_storage_->own.bag == 0);
            const unsigned expected_floor = std::max(50000u, unsigned(before) - 15000u);
            assert(b.acquisition_hold_floor_ == expected_floor && b.observer_.floor == expected_floor);
            const auto budget = b.information_storage_->work_budget;
            assert(budget.context == &b.acquisition_hold_floor_ && budget.can_refine);
            assert(budget.can_refine(budget.context));
            mock::fuel = int(expected_floor) - 1;
            assert(!budget.can_refine(budget.context));
        } else {
            assert(b.runtime_model_.events.empty() && b.acquisition_delivered_round_ == -1);
            if (scenario == 7) assert(b.information_storage_->round == -1);
        }
        no_hidden_bodies(); ++cases;
    }
}

int metric(plan4::Point a, plan4::Point b, const void*) {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}
rt::PhaseStart phase_start(rt::ProductionPhase phase, int round, std::uint64_t sequence) {
    return {phase, 7, 1, sequence, round, 2, phase == rt::ProductionPhase::clear ? 2 : 0, 7, false, 42};
}
rt::PhaseReceipt phase_end(const rt::PhaseStart& source, int round) {
    return {source.phase, source.actor, source.job_epoch, source.phase_sequence, source.start_round,
        round, source.nominal_duration, source.mode, source.mode, source.stat_epoch, true, true,
        false, rt::PhaseCensor::none, 0, 0, true, source.group_id};
}
void refresh_hold_information(Information& info, bool holding) {
    info.round = mock::round; info.own.id = mock::own.id; info.own.bag = mock::own.bag;
    info.own.position = {mock::own.position.x, mock::own.position.y};
    info.in_progress_group_id = holding ? 42 : -1;
    for (int g = 0; g < info.group_count; ++g)
        info.groups[g].provenance = info.groups[g].health_provenance =
            {info.round, plan4::Source::personal_vision, plan4::Status::exact_now};
    auto& first = info.groups[0];
    first.lifecycle = mock::group(42).state == 0 ? plan4::Lifecycle::living :
        mock::group(42).state == 1 ? plan4::Lifecycle::cleared : plan4::Lifecycle::respawning;
    first.health = first.lifecycle == plan4::Lifecycle::living ? 100 : 0;
    first.reward_available = first.lifecycle == plan4::Lifecycle::cleared;
}
[[gnu::noinline]] void covenant_clear_hold_capture(int& cases) {
    namespace cv = plan4::covenant;
    for (bool enabled : {false, true}) {
        Fixture f; auto b = std::make_unique<BotHarness<rt::Covenant>>(enabled);
        auto fresh = Fixture::information();
        b->information_storage_ = std::make_unique<Information>(); b->observer_.next = fresh.get();
        b->macro_job_.altar = 0;
        mock::group(42).state = 0; mock::own.position = {1, 4};
        fresh->own.health = fresh->own.maximum_health = 100; fresh->own.recall_safe = true;
        fresh->own_spawn = {1, 4}; fresh->travel = {metric, nullptr}; fresh->altar_count = 1;
        fresh->altars[0].id = 30; fresh->altars[0].position = {3, 6};
        fresh->altars[0].altar_position = {3, 7}; fresh->altars[0].lock_known = true;
        for (int g = 0; g < 2; ++g) {
            auto& group = fresh->groups[g]; group.position = {3 + 2 * g, 4};
            group.health = group.full_health = 100; group.clear_rounds = group.full_clear_rounds = 2;
            group.gems = 24; group.respawn_rounds = 30; group.lifecycle = plan4::Lifecycle::living;
            group.visible = group.occupancy_known = group.all_spawns_visible = true;
        }
        refresh_hold_information(*fresh, false); f.start(*b);
        *b->information_storage_ = *fresh; b->observe(*b->information_storage_);
        auto& model = b->runtime_model_;
        model.selected(*b->information_storage_, b->macro_job_, 1, true);
        const auto travel = phase_start(rt::ProductionPhase::travel, 1, 1);
        assert(model.phase_started(travel)); model.phase_completed(phase_end(travel, 2)); f.finish(*b);

        mock::round = 2; mock::fuel = 100000; mock::own.position = {3, 4}; f.start(*b);
        refresh_hold_information(*fresh, false); *b->information_storage_ = *fresh;
        b->observe(*b->information_storage_); assert(model.monitor().contract().stage == cv::Stage::clear);
        const auto clear = phase_start(rt::ProductionPhase::clear, 2, 2);
        assert(model.phase_started(clear)); f.finish(*b);
        mock::round = 3; mock::fuel = 100000; mock::group(42).state = 1; f.start(*b);
        model.phase_completed(phase_end(clear, 3)); f.finish(*b);
        assert(model.monitor().contract().stage == cv::Stage::clear);

        // The first actual hold poll confirms clear END within one round.
        // All five holding rounds retain the protected first-camp selection.
        for (int round = 4; round <= 8; ++round) {
            mock::round = round; mock::fuel = 100000; f.start(*b);
            refresh_hold_information(*fresh, true); b->hold();
            const auto& contract = model.monitor().contract();
            assert(contract.stage == cv::Stage::capture && contract.phase_start == 4);
            assert(contract.remaining_group == 0 && contract.completed_mask == 0);
            assert(b->macro_active_ && !b->macro_second_ && b->macro_job_.group == 0 && b->macro_job_.second_group == 1);
            assert(b->information_storage_->round == round && fake::logs.empty()); f.finish(*b);
        }
        assert(b->observer_.calls == 5 && b->navigation_.lookups == 5);
        mock::round = 9; mock::fuel = 100000; mock::group(42).state = 2; mock::own.bag = 12;
        f.start(*b); refresh_hold_information(*fresh, false);
        const auto* receipt = b->own_acquisition_.capture();
        assert(receipt && receipt->round == 9 && receipt->received_gems == 12);
        *b->information_storage_ = *fresh; b->observe(*b->information_storage_);
        const auto& contract = model.monitor().contract();
        assert(contract.stage == cv::Stage::approach && contract.remaining_group == 1 && contract.completed_mask == 1);
        assert(contract.phase_start == 9 && contract.nominal_duration == 2);
        assert(contract.completed[0].stock_epoch == receipt->stock_epoch);
        assert(b->macro_second_ && b->repair_frame(*b->information_storage_).remaining_group == 1);
        assert(fake::logs.size() == 1 && fake::logs[0].accepted && fake::logs[0].received == 12);
        no_hidden_bodies(); ++cases;
    }
}

int pair_route_prefix(plan4::Point from, plan4::Point to, plan4::Point* output, int capacity, const void*) {
    const int count = std::min(metric(from, to, nullptr), capacity);
    for (int step = 0; step < count; ++step) {
        from.x += (to.x > from.x) - (to.x < from.x);
        from.y += (to.y > from.y) - (to.y < from.y);
        // This witness's static routes stay inside the board and avoid its altar.
        assert(from.x >= 0 && from.x < 12 && from.y >= 0 && from.y < 12);
        assert(from != plan4::Point({7, 3})); output[step] = from;
    }
    return count;
}
[[gnu::noinline]] void covenant_measured_pair_witness(int& cases) {
    namespace cv = plan4::covenant;
    // Source/SDK input witness only: this does not establish an observed legal
    // blocker, production navigation trace, native engine path or match result.
    for (bool enabled : {false, true}) {
        Fixture f; auto b = std::make_unique<BotHarness<rt::Covenant>>(enabled);
        f.entries.clear(); std::fill(f.lookup.begin(), f.lookup.end(), -1);
        mock::groups = {{42, 0, 30, 30, 24, {{2, 0}}}, {43, 0, 30, 30, 24, {{4, 0}}}};
        mock::visible.fill(false); mock::round = 1880; mock::own.position = {1, 0};
        for (int g = 0; g < 2; ++g) {
            cambc::bitboard spawns; spawns.set({2 + 2 * g, 0});
            f.entries.push_back({cambc::map::camp_group{42 + g}, spawns});
            f.lookup[static_cast<std::size_t>(mock::tile({2 + 2 * g, 0}))] = g;
            mock::visible[static_cast<std::size_t>(mock::tile({2 + 2 * g, 0}))] = true;
        }
        auto fresh = Fixture::information(); auto& info = *fresh;
        info.settings.max_rounds = 2000;
        info.own.health = 80; info.own.maximum_health = 100; info.own.recall_safe = true;
        info.own_spawn = {0, 0}; info.travel = {metric, nullptr, pair_route_prefix}; info.altar_count = 1;
        info.altars[0].id = 30; info.altars[0].position = {7, 2};
        info.altars[0].altar_position = {7, 3}; info.altars[0].lock_known = true;
        for (int g = 0; g < 2; ++g) {
            auto& group = info.groups[g]; group.position = {2 + 2 * g, 0};
            group.health = group.full_health = 100; group.clear_rounds = group.full_clear_rounds = 2;
            group.gems = 24; group.respawn_rounds = 30; group.lifecycle = plan4::Lifecycle::living;
            group.visible = group.occupancy_known = group.all_spawns_visible = true;
        }
        refresh_hold_information(info, false); f.start(*b); b->observe(info);
        const auto menu = plan4::make_menu(info); // Freeze every real admitted alternative and its order.
        assert(!menu.truncated && menu.single_count == 2 && menu.count == 7);
        auto initial = std::make_unique<plan4::Evaluation>();
        auto& model = b->runtime_model_; rt::evaluate_model(info, menu, model, *initial);
        assert(!initial->budget_exhausted && initial->evaluated_count == menu.count && initial->chosen >= 0);
        const int pair = initial->chosen;
        b->macro_job_ = menu.jobs[pair];
        assert(b->macro_job_.kind == plan4::JobKind::farm && b->macro_job_.group == 0 && b->macro_job_.second_group == 1);
        unsigned best_single = 0;
        for (int n = 0; n < menu.single_count; ++n) best_single = std::max(best_single, initial->forecasts[n].our_secured);
        assert(initial->forecasts[pair].our_secured == 144 && best_single == 120);
        model.selected(info, b->macro_job_, 1, true);
        auto travel = phase_start(rt::ProductionPhase::travel, 1880, 1);
        travel.nominal_duration = 1; assert(model.phase_started(travel)); f.finish(*b);

        // The first camp is reached after one step, cleared in two rounds, then
        // occupied for the six capture phases following entry at round 1883.
        mock::round = 1881; mock::fuel = 100000; mock::own.position = {2, 0}; f.start(*b);
        model.phase_completed(phase_end(travel, 1881)); refresh_hold_information(info, false); b->observe(info);
        assert(model.monitor().contract().stage == cv::Stage::clear);
        const auto clear = phase_start(rt::ProductionPhase::clear, 1881, 2);
        assert(model.phase_started(clear)); f.finish(*b);
        mock::round = 1882; mock::fuel = 100000; f.start(*b);
        refresh_hold_information(info, false); b->observe(info); f.finish(*b);
        b->information_storage_ = std::make_unique<Information>(); b->observer_.next = &info;
        for (int round = 1883; round <= 1888; ++round) {
            mock::round = round; mock::fuel = 100000; mock::group(42).state = 1; f.start(*b);
            if (round == 1883) model.phase_completed(phase_end(clear, 1883));
            refresh_hold_information(info, true); b->hold();
            assert(model.monitor().contract().stage == cv::Stage::capture && !b->macro_second_);
            assert(model.monitor().contract().phase_start == 1883 && fake::logs.empty()); f.finish(*b);
        }
        mock::round = 1889; mock::fuel = 100000; mock::group(42).state = 2; mock::own.bag = 24;
        f.start(*b); refresh_hold_information(info, false); b->observe(info);
        const auto* receipt = b->own_acquisition_.capture();
        assert(receipt && receipt->group_id == 42 && receipt->round == 1889 && receipt->received_gems == 24);
        assert(receipt->origin == rt::CaptureOrigin::isolated_own_stock && receipt->actor_id == info.own.id);
        const auto captured = model.monitor().contract();
        assert(captured.stage == cv::Stage::approach && captured.remaining_group == 1 && captured.completed_mask == 1);
        assert(captured.phase_start == 1889 && captured.nominal_duration == 2 && captured.deadline == 1894);
        assert(captured.completed[0].stock_epoch == receipt->stock_epoch && b->macro_second_);
        assert(fake::logs.size() == 1 && fake::logs[0].accepted && fake::logs[0].received == 24);
        auto targeted_second = phase_start(rt::ProductionPhase::travel, 1889, 3); targeted_second.group_id = 43;
        assert(model.phase_started(targeted_second)); f.finish(*b);

        for (int round = 1890; round <= 1893; ++round) {
            mock::round = round; mock::fuel = 100000; mock::own.position = {round - 1887, 1};
            mock::group(42).timer = 30 - (round - 1889); f.start(*b);
            refresh_hold_information(info, false); info.groups[0].respawn_remaining = mock::group(42).timer;
            b->observe(info);
            const auto& stock = b->own_acquisition_.frame();
            assert(!b->own_acquisition_.capture() && stock.known && stock.count == 1 && stock.bag == 24);
            assert(stock.acquisitions[0].capture_round == 1889 && stock.acquisitions[0].gems == 24);
            assert(stock.acquisitions[0].stock_epoch == captured.completed[0].stock_epoch);
            assert(fake::logs.size() == 1 && b->macro_second_); if (round < 1893) f.finish(*b);
        }
        assert(info.own.position == plan4::Point({6, 1}) && info.groups[0].respawn_remaining == 26);
        auto frame = b->repair_frame(info); frame.fixed_menu = &menu; frame.job_epoch = 1;
        frame.same_epoch = frame.phase_known = true; frame.phase = rt::ProductionPhase::travel;
        const auto repair = rt::execution_repair(model, frame);
        assert(repair.supported && repair.action == rt::ExecutionRepairAction::none && info.round < captured.deadline);
        auto result = std::make_unique<plan4::Evaluation>(); rt::evaluate_model(info, menu, model, *result);
        assert(!result->budget_exhausted && result->evaluated_count == menu.count && result->chosen >= 0);
        const auto& chosen = menu.jobs[result->chosen];
        const auto evidence = model.model_evidence();
        assert(evidence.supported && evidence.coupling_enabled == enabled);
        assert(chosen.kind == plan4::JobKind::farm && chosen.group == (enabled ? 1 : 0));
        assert(chosen.second_group == (enabled ? -1 : 1));
        assert(result->forecasts[result->chosen].our_secured == 144);
        assert(result->forecasts[pair].our_secured == (enabled ? 120u : 144u));
        const auto& retained = model.monitor().contract();
        assert(!retained.failed && retained.epoch == captured.epoch && retained.phase_sequence == captured.phase_sequence &&
            retained.phase_start == captured.phase_start && retained.deadline == captured.deadline && retained.completed_mask == 1);
        // The first observed acquisition is retained inventory, never a new
        // forecast capture before its personally observed respawn can finish.
        for (int n = 0; n < menu.count; ++n) for (int e = 0; e < result->forecasts[n].event_count; ++e) {
            const auto& event = result->forecasts[n].events[e];
            if (event.kind == plan4::EventKind::capture && event.group_id == 42) assert(event.start.round >= 1919);
        }
        std::cout << "{\"schema\":\"plan4-covenant-pair-witness/1\",\"enabled\":" << (enabled ? "true" : "false")
            << ",\"initialPair\":" << initial->forecasts[pair].our_secured << ",\"initialBestSingle\":" << best_single
            << ",\"round\":" << info.round << ",\"deadline\":" << captured.deadline << ",\"selectedGroup\":"
            << chosen.group << ",\"selectedSecondGroup\":" << chosen.second_group << ",\"selectedSecured\":"
            << result->forecasts[result->chosen].our_secured << ",\"pairSecured\":"
            << result->forecasts[pair].our_secured << "}\n";
        no_hidden_bodies(); ++cases;
    }
}
}
int main() {
    int cases = 0;
    measured_delivery(cases); stale_snapshots(cases); no_weak_receipts(cases);
    rejected_delivery(cases); selection(cases); hold_observation(cases); covenant_clear_hold_capture(cases);
    covenant_measured_pair_witness(cases);
    std::cout << "{\"schema\":\"plan4-own-acquisition-hooks-tests/1\",\"passed\":true,\"cases\":" << cases << "}\n";
}
