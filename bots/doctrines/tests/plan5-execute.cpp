#include "../common/src/execute.h"
#include <cstdlib>
#include <iostream>

namespace e = p5::execution;
static int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition "\n"; std::exit(1); } } while (false)

e::Observation base() {
    e::Observation o;
    o.owner_turn = 10; o.remaining_rounds = 200; o.forecast_turns = 4;
    o.fuel = 100000; o.capture_duration = 5; o.safe = o.can_act = true;
    o.can_move = o.can_attack = o.can_cast = true;
    o.group_id = 7; o.at_target = o.group_live_known = true;
    o.group_count = 1; o.groups[0] = {7, 2, true, true, true, false, false};
    o.move_count = 4;
    for (int i = 0; i < 4; ++i) o.moves[i] = {100 + i, true, true, true, true, false};
    return o;
}
e::State at(e::Phase phase, const e::Observation& o) {
    e::State s; s.target = 7; e::enter(s, phase, o); return s;
}
int main() {
    {
        auto o = base(); e::State s;
        o.target_visible = o.target_category_legal = o.target_in_range = true;
        o.attack_target = 99;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::attack);
        CHECK(s.phase == e::Phase::clear && s.target == 7);
        o.groups[0].solo_safe = false; s = {};
        CHECK(e::decision(o, s).reason == e::Reason::no_opportunity);
        o.groups[0].solo_safe = true; o.groups[0].gem_bearing = false;
        CHECK(e::decision(o, s).reason == e::Reason::no_opportunity);
        o.groups[0].gem_bearing = true; o.groups[0].solo_safe = false;
        o.groups[0].unknown = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::inspect);
        ++o.owner_turn;
        CHECK(e::decision(o, s).reason == e::Reason::no_opportunity);
        CHECK(e::known_unavailable(s, 7));
        o.group_count = 2; o.groups[1] = {8, 3, true, true, false, true, false};
        CHECK(e::decision(o, s).action.kind == e::ActionKind::move);
        CHECK(s.target == 8);
    }
    {
        auto o = base(); auto s = at(e::Phase::assemble, o);
        o.forecast_turns = 40;
        o.owner_turn = 17;
        CHECK(e::decision(o, s).phase == e::Phase::assemble);
        o.owner_turn = 18;
        CHECK(e::decision(o, s).reason == e::Reason::assembly_timeout);
        s = at(e::Phase::assemble, o);
        o.coalition_visible = o.coalition_safe = true;
        CHECK(e::decision(o, s).phase == e::Phase::assemble);
        o.coalition_safety_fresh = true;
        CHECK(e::decision(o, s).phase == e::Phase::clear);
        o.forecast_turns = 0; s = at(e::Phase::assemble, o); o.coalition_visible = false;
        o.owner_turn += 8;
        CHECK(e::decision(o, s).reason == e::Reason::assembly_timeout);
    }
    {
        auto o = base(); auto s = at(e::Phase::hold, o);
        o.on_group_spawn = o.group_cleared = true;
        CHECK(e::capture_window(5) == 6);
        for (int t = 10; t < 22; ++t) {
            o.owner_turn = t;
            CHECK(e::decision(o, s).action.kind == e::ActionKind::hold);
            CHECK(s.phase == e::Phase::hold);
        }
        o.owner_turn = 22;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::hold);
        CHECK(s.capture_retry && s.retry_budget == 6);
        // An observed reset restarts only our occupancy estimate, never the retry deadline.
        o.owner_turn = 27; o.occupancy_reset_observed = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::hold);
        o.owner_turn = 28;
        CHECK(e::decision(o, s).reason == e::Reason::capture_unresolved);
        CHECK(s.phase == e::Phase::bootstrap);
        s = at(e::Phase::hold, o); o.group_respawning = true; o.occupancy_reset_observed = false;
        CHECK(e::decision(o, s).phase == e::Phase::hold); // respawn alone gives no own payout
        o.own_payout_observed = true; o.own_payout_group = 9; o.own_gems_received = 12; o.payout_receipt = 1;
        CHECK(e::decision(o, s).phase == e::Phase::hold);
        o.own_payout_group = 7;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::vacate);
        CHECK(s.phase == e::Phase::allocate && s.consumed_payout == 1);
        s = at(e::Phase::hold, o); o.payout_receipt = 2;
        o.channel = e::Channel::tribute; o.channel_valid = o.channel_safe = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::preserve_channel);
        CHECK(s.consumed_payout == 2 && s.phase == e::Phase::allocate);
    }
    {
        auto o = base(); auto s = at(e::Phase::hold, o);
        o.on_group_spawn = true; o.helper = o.holder_visible_on_spawn = true;
        o.remaining_coalition_safe = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::hold);
        o.remaining_coalition_safety_fresh = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::withdraw);
        o.remaining_coalition_safe = false;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::hold);
        o.safe = false; o.in_threat_range = true;
        CHECK(e::decision(o, s).phase == e::Phase::recover);
        o.safe = true; o.in_threat_range = false; s = at(e::Phase::hold, o);
        o.remaining_coalition_safe = true;
        const auto departure = e::decision(o, s);
        o.feedback_sequence = departure.action.sequence; o.feedback_kind = e::ActionKind::withdraw;
        o.feedback_success = true; o.on_group_spawn = false;
        CHECK(e::decision(o, s).phase != e::Phase::hold);
    }
    {
        auto o = base(); auto s = at(e::Phase::bank, o);
        o.adjacent_altar = true; o.own_bag = 11; o.tribute_ticks_quote = 2; o.altar_id = 41;
        auto d = e::decision(o, s);
        CHECK(d.action.kind == e::ActionKind::start_tribute && d.action.whole_bag);
        CHECK(d.action.starting_bag == 11 && d.action.channel_ticks == 2);
        o.channel = e::Channel::tribute; o.channel_valid = o.channel_safe = true;
        o.own_bag = 80; o.fuel = 0; o.owner_turn = 190;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::preserve_channel);
        o.channel = e::Channel::none; o.owner_turn = 10; o.fuel = 100000;
        o.hostile_lock = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::yield);
        o.lock_visible = o.lock_attack_legal = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::attack);
        o.ranged = o.moved_this_turn = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::yield);
    }
    {
        auto o = base(); auto s = at(e::Phase::clear, o);
        o.target_visible = o.target_category_legal = o.target_in_range = true; o.attack_target = 33;
        o.ranged = o.moved_this_turn = true; o.can_move = false;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::yield);
        o.attack_reset_after_move = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::attack);
        o.spells[0] = {50, true, true, true, true, true, false, true, false, true, false, false};
        CHECK(e::decision(o, s).action.kind == e::ActionKind::attack); // range alone is insufficient
        o.spells[0].path_legal = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::cast);
        o.successful_cast_this_turn = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::attack);
        o.successful_cast_this_turn = false; o.spells[0].movement_exclusive = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::attack);
    }
    {
        auto o = base(); auto s = at(e::Phase::travel, o); o.at_target = false;
        auto d = e::decision(o, s);
        for (int failed = 0; failed < 4; ++failed) {
            CHECK(d.action.kind == e::ActionKind::move && d.action.target == 100 + failed);
            o.feedback_sequence = d.action.sequence; o.feedback_kind = e::ActionKind::move;
            o.feedback_target = d.action.target; o.feedback_success = false;
            CHECK(e::decision(o, s).action.kind == e::ActionKind::refresh);
            ++o.observation_epoch;
            d = e::decision(o, s);
        }
        CHECK(d.action.kind == e::ActionKind::yield && s.failed_moves == 4);
        ++o.owner_turn; o.feedback_sequence = 0;
        o.moves[0].known_occupied = true; o.moves[1].path_legal = false; o.moves[2].landing_legal = false;
        CHECK(e::decision(o, s).action.target == 103);
    }
    {
        auto o = base(); auto s = at(e::Phase::recover, o);
        o.in_threat_range = true; o.can_recall = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::move);
        o.in_threat_range = false;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::start_recall);
        o.at_spawn = o.spawn_recovery_observed = true;
        CHECK(e::decision(o, s).reason == e::Reason::arrival_recovery);
        ++o.owner_turn;
        CHECK(e::decision(o, s).phase != e::Phase::recover);
        s = at(e::Phase::shop, o); o.purchase_requested = o.purchase_purchasable = true;
        o.purchase_affordable = true; o.purchase_id = 12; o.at_spawn = false;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::move);
        o.at_spawn = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::buy);
        o.alive = false; o.at_spawn = false;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::buy);
        o.alive = true; o.at_spawn = false; o.purchase_is_equipment = false;
        o.field_equipment_allowed = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::move);
    }
    {
        auto o = base(); e::State s; o.fuel = 44999;
        CHECK(e::decision(o, s).reason == e::Reason::low_fuel);
        s = at(e::Phase::travel, o); o.at_target = false; o.fuel = 29999;
        CHECK(e::decision(o, s).reason == e::Reason::low_fuel);
        s = at(e::Phase::hold, o); o.on_group_spawn = true; o.fuel = 15999;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::hold);
        o.channel = e::Channel::recall; o.channel_valid = o.channel_safe = true;
        CHECK(e::decision(o, s).action.kind == e::ActionKind::preserve_channel);
        CHECK(e::turn_boundary([&] { return e::decision(o, s); }).action.kind == e::ActionKind::preserve_channel);
        s = at(e::Phase::travel, o); o.channel = e::Channel::none; o.fuel = 100000;
        o.owner_turn += s.phase_budget;
        CHECK(e::decision(o, s).reason == e::Reason::timeout);
        s = at(e::Phase::recover, o); o.can_recall = false;
        o.owner_turn += s.phase_budget;
        CHECK(e::decision(o, s).reason == e::Reason::timeout);
    }
    std::cout << "plan5 execution: " << checks << " checks passed\n";
}
