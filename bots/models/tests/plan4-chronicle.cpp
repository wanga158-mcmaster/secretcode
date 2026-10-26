#include "../chronicle/src/chronicle.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace plan4;
namespace chronicle = plan4::chronicle;
namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int grid(Point a, Point b, const void*) {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}
void base(Information& i) {
    i.round = 10; i.own.id = 7; i.own.health = i.own.maximum_health = 1000;
    i.own.position = {0,0}; i.own_spawn = {0,0}; i.own.recall_safe = true;
    i.travel = {grid, nullptr}; i.group_count = 1;
    auto& g = i.groups[0];
    g.id = 101; g.position = {2,0}; g.full_health = g.health = 100;
    g.clear_rounds = g.full_clear_rounds = 2;
    g.incoming_damage = g.full_incoming_damage = 1;
    g.gems = 20; g.gold = 12; g.respawn_rounds = 30;
    g.lifecycle = Lifecycle::living; g.visible = g.all_spawns_visible = true;
    g.occupancy_known = true; g.reward_available = false;
    g.provenance = g.health_provenance = {i.round, Source::personal_vision, Status::exact_now};
    i.altar_count = 1; i.altars[0].id = 3; i.altars[0].position = {5,0};
    i.altars[0].altar_position = {6,0}; i.altars[0].lock_known = true;
}
void respawning(Information& i) {
    i.groups[0].lifecycle = Lifecycle::respawning;
    i.groups[0].health = 0; i.groups[0].respawn_remaining = 30;
}
void hidden(Information& i, int round) {
    i.round = round;
    auto& g = i.groups[0];
    g.visible = g.all_spawns_visible = g.occupancy_known = false;
    g.lifecycle = Lifecycle::unknown; g.reward_available = false;
    g.capture_started_round = g.respawn_remaining = -1;
    g.provenance = g.health_provenance = {};
}
chronicle::CaptureReceipt receipt(const Information& i) {
    return {i.groups[0].id, i.round, i.groups[0].respawn_rounds,
        {i.round, Source::own_occupancy, Status::exact_now}, true};
}
Job farm(int group = 0) { Job j; j.kind = JobKind::farm; j.group = group; j.altar = 0; return j; }
int events(const Forecast& f, EventKind kind, int group = -1) {
    int result = 0;
    for (int n = 0; n < f.event_count; ++n)
        result += f.events[n].kind == kind && (group < 0 || f.events[n].group_id == group);
    return result;
}
const Event* first(const Forecast& f, EventKind kind) {
    for (int n = 0; n < f.event_count; ++n) if (f.events[n].kind == kind) return &f.events[n];
    return nullptr;
}
bool same(const Forecast& a, const Forecast& b) {
    if (a.event_count != b.event_count || a.final_round != b.final_round
        || a.our_secured != b.our_secured || a.final_bag != b.final_bag
        || a.final_health != b.final_health || a.first_conversion_round != b.first_conversion_round
        || a.abort_reason != b.abort_reason || a.stock_unknown != b.stock_unknown) return false;
    for (int n = 0; n < a.event_count; ++n) {
        const auto& x = a.events[n]; const auto& y = b.events[n];
        if (x.kind != y.kind || x.start.round != y.start.round || x.start.phase != y.start.phase
            || x.end.round != y.end.round || x.end.phase != y.end.phase
            || x.group_id != y.group_id || x.bag_before != y.bag_before || x.bag_after != y.bag_after
            || x.secured != y.secured || x.status != y.status) return false;
    }
    return true;
}
void two_histories() {
    auto info = std::make_unique<Information>(); base(*info); respawning(*info);
    auto witnessed = std::make_unique<chronicle::State>();
    auto unwitnessed = std::make_unique<chronicle::State>();
    auto control = std::make_unique<chronicle::State>(false);
    witnessed->observe(*info); unwitnessed->observe(*info); control->observe(*info);
    check(witnessed->legal_action_receipt(receipt(*info)), "current personal capture receipt accepted");
    check(!control->legal_action_receipt(receipt(*info)), "disabled arm does not retain historical capture receipts");
    hidden(*info, 12);
    witnessed->observe(*info); unwitnessed->observe(*info); control->observe(*info);
    const auto history = witnessed->history(101);
    check(history.earliest_ready_round == 40 && history.capture_receipt_round == 10,
          "queried duration establishes earliest supported round");
    check(history.readiness_provenance.status == Status::derived && !history.occupancy_known,
          "remembered readiness is a bound without exact occupancy");
    check(witnessed->supply_wait(0, 12, 8, 0, false, *info) == 28
        && unwitnessed->supply_wait(0, 12, 8, 0, false, *info) == 0,
          "identical current sight differs only with legal earlier witness");
    for (int now = 12; now <= 44; ++now)
        check(witnessed->supply_wait(0, now, 8, 0, false, *info) == std::max(0, 40 - now),
              "supported lower bound expires without asserting future availability");
    auto active = std::make_unique<Forecast>(), baseline = std::make_unique<Forecast>();
    witnessed->predict_into(*info, farm(), Menu{}, *active);
    unwitnessed->predict_into(*info, farm(), Menu{}, *baseline);
    check(first(*active, EventKind::wait) && first(*active, EventKind::wait)->end.round == 40,
          "premature revisit waits off the target before traveling");
    check(active->stock_unknown && active->abort_reason == AbortReason::unknown_stock
        && active->our_secured == 0 && active->final_bag == 0 && events(*active, EventKind::capture) == 0,
          "expired readiness bound creates no assured stock or invented reward");
    check(active->final_round - baseline->final_round == 28,
          "history changes forecast delay while retaining unknown stock");
    check(witnessed->history(101).earliest_ready_round == 40,
          "forecast never mutates the observation ledger");

    // Synthetic timing test only: a different known camp can follow the failed
    // scouting premise. The earlier unknown camp still contributes no reward.
    info->group_count = 2; info->groups[1] = info->groups[0];
    auto& known = info->groups[1]; known.id = 202; known.position = {3,0};
    known.lifecycle = Lifecycle::living; known.visible = known.all_spawns_visible = known.occupancy_known = true;
    known.provenance = known.health_provenance = {12, Source::personal_vision, Status::exact_now};
    known.respawn_remaining = -1; known.health = 100;
    Menu continuation; continuation.jobs[0] = farm(1); continuation.count = continuation.single_count = 1;
    witnessed->predict_into(*info, farm(), continuation, *active);
    unwitnessed->predict_into(*info, farm(), continuation, *baseline);
    check(active->completed_conversion && baseline->completed_conversion
        && active->first_conversion_round - baseline->first_conversion_round == 28,
          "supported history delay propagates to later first conversion");
    check(events(*active, EventKind::capture, 101) == 0 && events(*active, EventKind::capture, 202) > 0,
          "only fresh known camp produces the continuation rewards");
    control->predict_into(*info, farm(), continuation, *active);
    plan4::predict_into(*info, farm(), continuation, *baseline);
    check(same(*active, *baseline), "disabled history arm exactly preserves current M0 forecast");
    check(control->history(101).earliest_ready_round < 0
        && control->history(101).last_lifecycle == Lifecycle::unknown,
          "disabled observe discards noncurrent group history");
}
void current_override_and_negatives() {
    auto i = std::make_unique<Information>(); base(*i); respawning(*i);
    auto state = std::make_unique<chronicle::State>(); state->observe(*i);
    auto bad = receipt(*i); bad.personally_witnessed = false;
    check(!state->legal_action_receipt(bad), "unwitnessed receipt rejected");
    bad = receipt(*i); bad.provenance.source = Source::public_layout;
    check(!state->legal_action_receipt(bad), "public layout cannot witness a capture");
    bad = receipt(*i); bad.provenance.status = Status::predicted;
    check(!state->legal_action_receipt(bad), "predicted capture cannot enter evidence ledger");
    bad = receipt(*i); --bad.provenance.observed_round;
    check(!state->legal_action_receipt(bad), "stale receipt rejected");
    bad = receipt(*i); ++bad.respawn_rounds;
    check(!state->legal_action_receipt(bad), "receipt cannot substitute an unqueried respawn duration");
    check(state->legal_action_receipt(receipt(*i)), "valid receipt still accepted after rejected inputs");
    hidden(*i, 12); state->observe(*i);
    i->groups[0].visible = i->groups[0].all_spawns_visible = i->groups[0].occupancy_known = true;
    i->groups[0].lifecycle = Lifecycle::living; i->groups[0].health = 100;
    i->groups[0].provenance = i->groups[0].health_provenance = {12, Source::personal_vision, Status::exact_now};
    state->observe(*i);
    check(state->history(101).earliest_ready_round < 0
        && state->history(101).last_lifecycle == Lifecycle::living,
          "new observed lifecycle overrides even an earlier remembered readiness bound");
    auto active = std::make_unique<Forecast>(), baseline = std::make_unique<Forecast>();
    state->predict_into(*i, farm(), Menu{}, *active); plan4::predict_into(*i, farm(), Menu{}, *baseline);
    check(same(*active, *baseline), "authoritative current stock gives active and M0 identical forecasts");
    check(state->supply_wait(0, 12, 8, 0, true, *i) == 0, "current ready stock is never delayed by history");
    auto without_witness = std::make_unique<chronicle::State>(); without_witness->observe(*i);
    hidden(*i, 13); state->observe(*i); without_witness->observe(*i);
    state->predict_into(*i, farm(), Menu{}, *active);
    without_witness->predict_into(*i, farm(), Menu{}, *baseline);
    check(same(*active, *baseline), "new lifecycle makes the two histories a negative control pair");
    state->enabled(false);
    check(!state->enabled() && state->retained_groups() == 0, "disabling drops ledger immediately");
    state->enabled(true); check(state->enabled(), "active mode can restart without restoring dropped history");
    base(*i); respawning(*i); state->observe(*i); state->legal_action_receipt(receipt(*i));
    hidden(*i, 11); ++i->own.id; state->observe(*i);
    check(state->history(101).earliest_ready_round < 0, "history cannot cross actor identities");
}
void walking_out_of_sight() {
    auto i = std::make_unique<Information>(); base(*i); respawning(*i);
    auto state = std::make_unique<chronicle::State>(); state->observe(*i);
    check(state->legal_action_receipt(receipt(*i)), "walking history starts with a legal capture receipt");
    i->round = 11; i->groups[0].provenance.observed_round = 11;
    i->groups[0].respawn_remaining = 29; state->observe(*i);
    check(state->history(101).earliest_ready_round == 40
        && state->history(101).readiness_provenance.status == Status::derived,
          "same visible waiting cycle preserves a supported non-guaranteed capture bound");
    hidden(*i, 12); state->observe(*i);
    check(state->supply_wait(0,12,8,0,false,*i) == 28,
          "walking through another visible turn does not erase later hidden-history effect");
    i->round = 40; i->groups[0].visible = true; i->groups[0].lifecycle = Lifecycle::respawning;
    i->groups[0].provenance = {40,Source::personal_vision,Status::exact_now};
    i->groups[0].respawn_remaining = 30; state->observe(*i);
    check(state->history(101).earliest_ready_round < 0,
          "waiting observation after earliest bound may be a new cycle and invalidates old receipt");
    base(*i); respawning(*i); state->observe(*i); state->legal_action_receipt(receipt(*i));
    i->round = 11; i->groups[0].visible = true; i->groups[0].lifecycle = Lifecycle::living;
    i->groups[0].provenance.observed_round = 11; state->observe(*i);
    check(state->history(101).earliest_ready_round < 0,
          "contradictory living observation before the old bound is authoritative");
    base(*i); respawning(*i); state->observe(*i); state->legal_action_receipt(receipt(*i));
    i->round = 11; i->groups[0].provenance.observed_round = 11;
    i->groups[0].respawn_remaining = 1; state->observe(*i);
    check(state->history(101).earliest_ready_round < 0,
          "new exact countdown cannot be contradicted by a later remembered bound");
}
void partial_lifecycle_frames() {
    auto state = std::make_unique<chronicle::State>();
    Group group; group.id = 101; group.position = {37,2}; group.respawn_rounds = 30;
    group.visible = true; group.lifecycle = Lifecycle::cleared; group.reward_available = true;
    group.provenance = {9,Source::personal_vision,Status::exact_now};
    check(state->observe_lifecycle_frame(9,7,&group,1), "partial current cleared frame accepted");
    group.lifecycle = Lifecycle::respawning; group.reward_available = false; group.respawn_remaining = 30;
    group.provenance.observed_round = 10;
    check(state->observe_lifecycle_frame(10,7,&group,1), "next partial current respawning frame accepted");
    const chronicle::CaptureReceipt captured{101,10,30,{10,Source::personal_vision,Status::exact_now},true};
    check(state->legal_action_receipt(captured),
          "off-spawn observer can reconcile consecutive lifecycle receipt without a full Information sweep");
    group.respawn_remaining = 29; group.provenance.observed_round = 11;
    check(state->observe_lifecycle_frame(11,7,&group,1,true) && state->truncated()
        && state->history(101).earliest_ready_round == 40,
          "valid truncated prefix preserves same-cycle receipt without filling missing groups");
    check(state->observe_lifecycle_frame(12,7,nullptr,0), "empty current lifecycle frame is valid");
    auto history = state->history(101);
    check(history.earliest_ready_round == 40 && !history.occupancy_known
        && history.lifecycle_provenance.status == Status::derived,
          "omitted group becomes historical without erasing its supported lower bound");
    auto info = std::make_unique<Information>(); base(*info); hidden(*info,12);
    check(info->own.position != group.position && state->supply_wait(0,12,8,0,false,*info) == 28,
          "partial off-spawn capture receipt affects readiness only, not physical actor state");

    check(!state->observe_lifecycle_frame(13,7,&group,-1) && state->truncated(), "negative prefix count rejected");
    auto ungrounded = captured; ungrounded.round = ungrounded.provenance.observed_round = 13;
    check(!state->legal_action_receipt(ungrounded), "malformed frame cannot refresh a capture premise");
    check(!state->observe_lifecycle_frame(13,7,nullptr,1) && state->truncated(), "null nonempty prefix rejected");
    check(!state->observe_lifecycle_frame(13,7,&group,max_groups+1) && state->truncated(),
          "over-capacity prefix rejected without reading beyond supplied storage");
    Group invalid = group; invalid.id = -1;
    check(!state->observe_lifecycle_frame(13,7,&invalid,1) && state->truncated(), "negative group identity rejected");
    group.respawn_rounds = 0; group.visible = false; group.provenance = {};
    state->observe_lifecycle_frame(13,7,&group,1);
    group.visible = true; group.respawn_rounds = 30; group.respawn_remaining = 26;
    group.provenance = {14,Source::personal_vision,Status::exact_now};
    state->observe_lifecycle_frame(14,7,&group,1);
    check(state->history(101).earliest_ready_round == 40,
          "hidden/stale partial row cannot replace the public duration supporting a known bound");
    group.lifecycle = Lifecycle::living; group.provenance.observed_round = 15;
    state->observe_lifecycle_frame(15,7,&group,1);
    check(state->history(101).earliest_ready_round < 0,
          "partial current living lifecycle overrides earlier capture history");

    group.lifecycle = Lifecycle::respawning; group.provenance.observed_round = 16; group.respawn_remaining = 30;
    state->observe_lifecycle_frame(16,7,&group,1);
    auto newer = captured; newer.round = newer.provenance.observed_round = 16;
    check(state->legal_action_receipt(newer), "new partial receipt accepted after a new observed cycle");
    group.provenance.observed_round = 17; state->observe_lifecycle_frame(17,8,&group,1);
    check(state->history(101).earliest_ready_round < 0, "partial actor switch drops another actor's history");
    newer.round = newer.provenance.observed_round = 17; state->legal_action_receipt(newer);
    group.provenance.observed_round = 10; state->observe_lifecycle_frame(10,8,&group,1);
    check(state->history(101).earliest_ready_round < 0, "backwards partial clock discards future history");
    check(!state->observe_lifecycle_frame(-1,8,&group,1) && state->truncated() && !state->retained_groups(),
          "negative partial clock rejects and invalidates retained premises");
    check(!state->observe_lifecycle_frame(10,-1,&group,1) && state->truncated(), "negative actor identity rejected");
    auto control = std::make_unique<chronicle::State>(false);
    group.provenance.observed_round = 10;
    control->observe_lifecycle_frame(10,7,&group,1);
    check(control->history(101).lifecycle_provenance.status == Status::exact_now
        && !control->legal_action_receipt(captured), "disabled partial frame keeps current sight without historical receipts");
    control->observe_lifecycle_frame(11,7,nullptr,0);
    check(!control->history(101).present, "disabled partial empty frame discards all noncurrent group data");
}
void health_and_visibility() {
    auto i = std::make_unique<Information>(); base(*i);
    auto state = std::make_unique<chronicle::State>();
    std::array<chronicle::MemberObservation, 2> hp{{
        {9,{3,0},80,{10,Source::personal_vision,Status::exact_now}},
        {4,{2,0},40,{10,Source::personal_vision,Status::exact_now}}}};
    chronicle::GroupMembers group{101,2,hp.data(),2,true};
    i->groups[0].capture_started_round = 8; i->groups[0].occupied_spawn = true;
    state->observe(*i, &group, 1);
    auto history = state->history(101);
    check(history.exact_health_vector && history.health_count == 2 && history.occupancy_known
        && history.capture_started_round == 8, "complete legal frame retains exact member vector and current occupancy");
    chronicle::MemberObservation sample;
    check(state->member_health(101,0,sample) && sample.member_id == 4 && sample.health == 40,
          "member vector follows public spawn order");
    hidden(*i, 11); state->observe(*i);
    history = state->history(101);
    check(!history.exact_health_vector && !history.occupancy_known && history.capture_started_round < 0
        && history.lifecycle_provenance.status == Status::derived,
          "lost sight invalidates exact occupancy, capture progress and lifecycle status");
    check(state->member_health(101,0,sample) && sample.health == 40
        && sample.provenance.status == Status::derived && sample.provenance.observed_round == 10,
          "last exact member HP remains timestamped historical evidence");
    // A claimed member sample cannot bypass the missing legal group frame.
    hp[0].health = 1; hp[0].provenance.observed_round = 11;
    group.sample_count = 1; group.all_members_visible = false;
    state->observe(*i, &group, 1);
    check(state->member_health(101,1,sample) && sample.health == 80,
          "hidden-group frame rejects purported exact HP samples");
    i->groups[0].visible = true; i->groups[0].lifecycle = Lifecycle::living;
    i->groups[0].provenance = {11,Source::personal_vision,Status::exact_now};
    state->observe(*i, &group, 1);
    check(state->member_health(101,1,sample) && sample.health == 1
        && sample.provenance.status == Status::exact_now,
          "partial current member observation overrides its own old HP only");
    check(state->member_health(101,0,sample) && sample.health == 40
        && sample.provenance.status == Status::derived && !state->history(101).exact_health_vector,
          "unseen members never acquire guessed current HP");
    hp[0].health = 900; hp[0].provenance.source = Source::model;
    state->observe(*i, &group, 1);
    check(state->member_health(101,1,sample) && sample.health == 1,
          "model-sourced HP cannot overwrite witnessed evidence");
    hidden(*i, 11); state->observe(*i);
    check(state->member_health(101,1,sample) && sample.provenance.status == Status::derived,
          "same-round loss of sight also invalidates exact-now claims");
    i->groups[0].visible = true; i->groups[0].lifecycle = Lifecycle::cleared;
    i->groups[0].provenance = {11,Source::personal_vision,Status::exact_now};
    state->observe(*i);
    check(state->history(101).health_count == 0 && !state->member_health(101,0,sample),
          "new lifecycle invalidates the former generation's member health vector");
    std::array<chronicle::MemberObservation, chronicle::max_members + 1> many;
    for (int n = 0; n < int(many.size()); ++n)
        many[n] = {n,{n,0},1,{11,Source::personal_vision,Status::exact_now}};
    group = {101,int(many.size()),many.data(),int(many.size()),true};
    state->observe(*i, &group, 1);
    check(state->history(101).member_overflow && !state->history(101).exact_health_vector
        && state->history(101).health_count == chronicle::max_members,
          "over-capacity member vector keeps a bounded prefix without a complete exact claim");
}
struct Work { int remaining = 0; };
bool can_work(const void* context) { return static_cast<Work*>(const_cast<void*>(context))->remaining-- > 0; }
void bounded_invalidation() {
    auto i = std::make_unique<Information>(); base(*i);
    auto state = std::make_unique<chronicle::State>(); state->observe(*i);
    Work work; i->work_budget = {can_work, &work}; ++i->round;
    i->groups[0].provenance.observed_round = i->round;
    state->observe(*i);
    check(state->truncated() && !state->history(101).occupancy_known
        && state->history(101).lifecycle_provenance.status == Status::derived,
          "budget-denied update cannot leave earlier exact facts current");
    i->work_budget = {}; i->round = 5; state->observe(*i);
    check(state->history(101).last_lifecycle == Lifecycle::unknown,
          "backwards replay clock resets history rather than transporting future evidence");

    auto duplicate_info = std::make_unique<Information>(); base(*duplicate_info);
    auto duplicate_state = std::make_unique<chronicle::State>();
    std::array<chronicle::MemberObservation, 2> samples{{
        {1,{2,0},10,{10,Source::personal_vision,Status::exact_now}},
        {2,{3,0},20,{10,Source::personal_vision,Status::exact_now}}}};
    std::array<chronicle::GroupMembers, 2> duplicate{{
        {101,2,samples.data(),2,true}, {101,2,samples.data(),2,true}}};
    work.remaining = 6; duplicate_info->work_budget = {can_work,&work};
    duplicate_state->observe(*duplicate_info, duplicate.data(), int(duplicate.size()));
    check(duplicate_state->truncated() && duplicate_state->history(101).health_count == 1
        && !duplicate_state->history(101).exact_health_vector,
          "interrupted duplicate full-vector replacement cannot retain earlier complete status");
}
}
int main() {
    two_histories(); current_override_and_negatives(); walking_out_of_sight(); partial_lifecycle_frames();
    health_and_visibility(); bounded_invalidation();
    std::printf("chronicle checks=%d synthetic-only legal-witness=pending\n", checks);
}
