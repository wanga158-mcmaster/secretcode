// Included only inside the extracted Bot, under PLAN4_MACRO_MODE.
#ifndef PLAN4_COMPOSITION_FIRST
#define PLAN4_COMPOSITION_FIRST 0
#endif
#ifndef PLAN4_ACTOR_BIND_DIAGNOSTICS
#define PLAN4_ACTOR_BIND_DIAGNOSTICS 0
#endif
#ifndef PLAN4_MOVEMENT_STORAGE_PREP
#define PLAN4_MOVEMENT_STORAGE_PREP 0
#endif
plan4::OnlineObserver observer_;
plan4::PublicNavigation navigation_;
plan4::PersistentMovement movement_;
#if PLAN4_FRESH_DYNAMIC_GRID
plan4::FreshDynamicGrid<cambc::vec2> fresh_dynamic_grid_;
std::uint64_t fresh_grid_map_epoch_=0;
#endif
plan4::detour::Cursor alternate_route_;
#ifdef PLAN4_RUNTIME_TYPE
PLAN4_RUNTIME_TYPE runtime_model_{PLAN4_MODEL_ENABLED != 0};
#else
plan4::runtime::M0 runtime_model_;
#endif
#ifdef PLAN4_ONLINE_EVIDENCE
plan4::runtime::OnlineEvidence online_evidence_;
#ifdef PLAN4_ACTOR_EVIDENCE
plan4::runtime::ActorEvidence actor_evidence_;
#endif
#ifdef PLAN4_SPATIAL_EVIDENCE
plan4::runtime::SpatialEvidence spatial_evidence_;
#endif
std::uint64_t kit_signature_ = 0;
std::uint32_t kit_epoch_ = 0;
bool kit_seen_ = false;
int kit_observed_round_ = -1;
#include "production-methods.h"
#endif
bool movement_initialized_ = false, movement_pending_ = false;
#if PLAN4_MOVEMENT_STORAGE_PREP
bool movement_storage_yielded_ = false;
bool prepare_movement_storage() {
    movement_storage_yielded_ = false;
    if (movement_initialized_ || cambc::fuel_remaining() < 90000) return false;
    // This is the same immutable terrain body used by macro_move. Preparing
    // it before optional collection avoids repeatedly reaching the existing
    // cold-movement admission only after the farmer has spent its reserve.
    // No current occupancy, stock, action availability or path is retained.
    const int width=cambc::map::width(),height=cambc::map::height();
    std::vector<unsigned char> topology(width*height,0);
    for (const auto p:terrain_.positions()) topology[p.y*width+p.x]=1;
    movement_.initialize(width,height,std::move(topology));
    movement_initialized_=true;
    // Immutable allocation may suspend. End this owner turn without observing
    // or acting, and let the next current callback perform all normal guards.
    movement_storage_yielded_=true;
    return true;
}
#endif
// Bot construction precedes planning admission (and a fixture may use only
// ordinary farming). Defer the fixed-capacity planning buffers until needed.
std::unique_ptr<plan4::Information> information_storage_;
std::unique_ptr<plan4::Evaluation> evaluation_storage_;
plan4::Job macro_job_{};
bool macro_active_ = false, macro_second_ = false;
unsigned previous_bag_ = 0, previous_score_ = 0;
bool previous_tribute_ = false, macro_seen_ = false;
int macro_started_ = -1;
unsigned decision_sequence_ = 0;
#ifdef PLAN4_OWN_ACQUISITION
#include "acquisition-methods.h"
#endif
unsigned phase_seen_ = 0;
plan4::DeferredPhases phase_samples_;
int physical_vertices_ = 0, model_vertices_ = 0;
int last_fallback_row_ = -32;
int last_evaluation_round_ = -4;
int production_requested_farm_group_ = -1;
int production_requested_bank_ = -1;
unsigned optional_floor_ = 50000;
unsigned movement_floor_ = 35000;
std::unique_ptr<plan4::safe_segment::SegmentExecutor> segment_executor_;
#include "caller-receipt.h"
#include "movement-receipt.h"
#include "attempt-methods.h"

static bool movement_budget(const void* floor) { return cambc::fuel_remaining() >= *static_cast<const unsigned*>(floor); }

void macro_abort(const char* reason) {
    alternate_route_.reset();
    runtime_model_.selection_canceled();
#ifdef PLAN4_ONLINE_EVIDENCE
    production_cancel(plan4::runtime::PhaseCensor::interrupted);
#endif
    if (!macro_active_) return;
    attempt_end(std::string_view(reason) == "death" ? plan4::attempts::Outcome::death : plan4::attempts::Outcome::aborted);
    plan4::LogRow("abort", PLAN4_REFERENCE_VERSION).field("reason", reason)
        .field("job", decision_sequence_).field("actor", int(static_cast<cambc::handle_champion_t>(cambc::self())))
        .field("round", cambc::round_number()).emit();
    macro_active_ = false;
}
void macro_finish() {
    alternate_route_.reset();
    attempt_end(plan4::attempts::Outcome::unknown);
    runtime_model_.selection_canceled();
#ifdef PLAN4_ONLINE_EVIDENCE
    production_cancel(plan4::runtime::PhaseCensor::interrupted);
#endif
    macro_active_ = false;
}

void macro_observer_phase(unsigned before) {
    if (!information_storage_) return;
    const auto& info = *information_storage_;
    const auto now = cambc::round_number();
    if (int(now) != info.round || !phase_samples_.wants(0, int(now), phase_seen_)) return;
    const unsigned after = static_cast<unsigned>(cambc::fuel_remaining());
    if (after < 16000 || after > before) return;
    const auto& stop = observer_.diagnostics();
    plan4::PhaseSample sample{};
    sample.actor = int(static_cast<cambc::handle_champion_t>(cambc::self()));
    sample.round = int(now); sample.before = before; sample.after = after;
    sample.values = {std::uint64_t(info.observation_truncated), std::uint64_t(info.public_layout_truncated),
        std::uint64_t(stop.stop_stage), std::uint64_t(stop.stop_group), std::uint64_t(stop.stop_member),
        std::uint64_t(stop.groups), std::uint64_t(stop.visited), stop.collection_fuel,
        std::uint64_t(stop.stop_fuel), stop.stop_floor, stop.stop_margin};
    if (cambc::round_number() == now) phase_samples_.save(0, sample);
}

void macro_phase(const char* name, unsigned before, const plan4::MovementStep* step = nullptr, bool moved = false,
    unsigned prepared_fuel = 0, unsigned generation = 0) {
    if (std::string_view(name) == "observer") { macro_observer_phase(before); return; }
    constexpr std::array<std::string_view, 5> names{"observer", "navigation", "menu", "evaluation", "movement"};
    unsigned kind = unsigned(names.size());
    for (unsigned n = 0; n < names.size(); ++n) if (names[n] == name) { kind = n; break; }
    if (kind == names.size()) return;
    const auto now = cambc::round_number();
    if (!phase_samples_.wants(kind, int(now), phase_seen_)) return;
    const unsigned after = static_cast<unsigned>(cambc::fuel_remaining());
    if (after < 16000 || after > before) return;
    plan4::PhaseSample sample{};
    sample.actor = int(static_cast<cambc::handle_champion_t>(cambc::self()));
    sample.round = int(now); sample.before = before; sample.after = after;
    sample.movement = step != nullptr;
    if (step) sample.values = {std::uint64_t(step->status), std::uint64_t(step->next.x), std::uint64_t(step->next.y),
        std::uint64_t(step->remaining_distance), std::uint64_t(moved), prepared_fuel,
        std::uint64_t(step->visits), generation};
    if (cambc::round_number() == now) phase_samples_.save(kind, sample);
}

#ifdef PLAN4_ACTOR_EVIDENCE
void macro_actor_phase() {
    const auto& frame = actor_evidence_.frame();
    const auto now = cambc::round_number();
    if (int(now) != frame.clock.round || !phase_samples_.wants(5, int(now), phase_seen_)) return;
    const unsigned before = actor_evidence_.collection_start_fuel();
    const unsigned after = static_cast<unsigned>(cambc::fuel_remaining());
    if (after < 16000 || after > before) return;
    plan4::PhaseSample sample{};
    sample.actor = int(static_cast<cambc::handle_champion_t>(cambc::self()));
    sample.round = int(now); sample.before = before; sample.after = after;
    sample.values = {std::uint64_t(frame.count), std::uint64_t(frame.own_observed),
        std::uint64_t(frame.roster_complete), std::uint64_t(frame.truncated),
        std::uint64_t(actor_evidence_.stop_reason()), actor_evidence_.minimum_fuel(), std::uint64_t(actor_evidence_.stop_fuel())};
    if (cambc::round_number() == now) phase_samples_.save(5, sample);
}
#endif

void macro_flush_phases() {
    if (!phase_samples_.pending_mask) return;
    constexpr std::array<const char*, 6> names{"observer", "navigation", "menu", "evaluation", "movement", "actors"};
    for (unsigned n = 0; n < phase_samples_.count; ++n) {
        const unsigned kind = phase_samples_.order[n], bit = 1u << kind;
        if (!(phase_samples_.pending_mask & bit)) continue;
        const auto& sample = phase_samples_.slots[kind];
        if (int(cambc::round_number()) != sample.round || cambc::fuel_remaining() < 16000 ||
            int(cambc::round_number()) != sample.round) {
            phase_samples_.discard();
            return;
        }
        phase_samples_.pending_mask &= ~bit;
        const auto& v = sample.values;
        auto signed_value = [&](unsigned index) { return std::bit_cast<std::int64_t>(v[index]); };
        plan4::LogRow row("phase", PLAN4_REFERENCE_VERSION, kind == 0 ? 4 : kind == 5 ? 3 : 2);
        row.field("actor", sample.actor).field("round", sample.round).field("name", names[kind])
            .field("before", sample.before).field("after", sample.after);
        if (kind == 0) {
            row.field("truncated", v[0]).field("layoutTruncated", v[1])
                .field("stopStage", signed_value(2)).field("stopGroup", signed_value(3)).field("stopMember", signed_value(4))
                .field("groups", signed_value(5)).field("visited", signed_value(6)).field("collectionFuel", v[7])
                .field("stopFuel", signed_value(8)).field("stopFloor", v[9]).field("stopMargin", v[10]);
        } else if (kind == 5) {
            row.field("actorCount", signed_value(0)).field("ownObserved", v[1]).field("rosterComplete", v[2])
                .field("truncated", v[3]).field("stopReason", signed_value(4)).field("minimumFuel", v[5]).field("stopFuel", signed_value(6));
        } else if (sample.movement) {
            row.field("status", signed_value(0)).field("nextX", signed_value(1)).field("nextY", signed_value(2))
                .field("remaining", signed_value(3)).field("moved", v[4]).field("preparedFuel", v[5])
                .field("visits", signed_value(6)).field("generation", v[7]);
        }
        row.emit();
        phase_seen_ |= bit;
    }
    phase_samples_.discard();
}

void macro_tribute_started() {
    const auto& me = cambc::self();
    previous_tribute_ = true;
    previous_bag_ = me.gem_count();
    previous_score_ = cambc::global_submitted_gems(team::ally);
    attempt_tribute_observed();
#ifdef PLAN4_ONLINE_EVIDENCE
    production_begin_bank(previous_bag_);
#endif
    plan4::LogRow("milestone", PLAN4_REFERENCE_VERSION).field("actor", int(static_cast<cambc::handle_champion_t>(me)))
        .field("round", cambc::round_number()).field("job", decision_sequence_)
        .field("bag", previous_bag_).field("score", previous_score_)
        .field("completed", 0).field("censor", "tribute-start").emit();
}

#include "alternate-methods.h"

#include "movement-methods.h"

bool macro_farm_move(bitboard goals,int group_id) {
    production_requested_farm_group_=group_id;
    const bool moved=move_to(goals);
    production_requested_farm_group_=-1;
#ifdef PLAN4_ONLINE_EVIDENCE
    production_poll(true);
    if (!moved && movement_pending_ && production_tracker_.pending())
        production_transition(production_tracker_.record_wait(*production_tracker_.pending_key(),production_clock(),true));
#endif
    return moved;
}

// The provider's certified scene is optional. Once an excursion has begun,
// missing fresh evidence interrupts it; it cannot resume ordinary combat.
bool macro_segment_execute(const camp* first_target=nullptr) {
#ifdef PLAN4_ONLINE_EVIDENCE
    if constexpr (plan4::runtime::uses_safe_segments<decltype(runtime_model_)>) {
        namespace ss=plan4::safe_segment;
        auto& me=cambc::self();
        const int round=int(cambc::round_number());
        const bool running=segment_executor_ && segment_executor_->active();
        if (!running && (!first_target || !plan4::runtime::safe_segments_enabled(runtime_model_))) return false;
        if (!segment_executor_) segment_executor_=std::make_unique<ss::SegmentExecutor>();
        const auto fail=[&] {
            segment_executor_->reset();macro_abort("segment-source");recovering_=me.is_alive();
            if (recovering_ && int(cambc::round_number())==round && cambc::fuel_remaining()>=40000 &&
                !me.is_recalling() && !me.is_submitting_gems() && !me.is_teleporting()) {
                const auto state=me.state();
                const ss::RecoveryPermission permit{round,int(cambc::round_number()),cambc::fuel_remaining(),true,false,
                    state.contains(cambc::champion_state::can_cast) && !state.contains(cambc::champion_state::recall_started)};
                ss::checked_recovery(permit,[&] {
                    const auto current_camps=cambc::vision::camps();const auto current_enemies=cambc::vision::champions(team::enemy);
                    if (current_camps.size()<=64 && current_enemies.size()<=5 && int(cambc::round_number())==round &&
                        cambc::fuel_remaining()>=35000) recover(current_camps,current_enemies);
                });
            }
            return true;
        };
        if (macro_active_ && round-macro_started_>=plan4::max_horizon) return fail();
        if (cambc::fuel_remaining()<60000) return fail();
        ss::ExecutionBody body;body.round=round;body.actor_id=int(static_cast<cambc::handle_champion_t>(me));
        body.alive=me.is_alive();if (!body.alive) return fail();
        const auto origin=me.position();body.position={origin.x,origin.y};
        body.health=me.health();body.maximum_health=me.max_health();
        body.kit_version=kit_signature_;body.stat_epoch=kit_epoch_;
        body.recalling=me.is_recalling();body.other_channel=me.is_submitting_gems() || me.is_teleporting();
        body.can_attack=me.can_attack();body.can_move=me.can_move();
        body.on_spawn=cambc::map::spawn_region(team::ally).is_set(origin);body.emergency=recovering_;
        const int group_id=running?segment_executor_->group_id():
            (target_>=0 && target_<int(groups_.size())?int(static_cast<cambc::handle_camp_group_t>(groups_[target_].group)):-1);
        body.group_id=group_id;
        body.target_id=running?segment_executor_->target_id():int(static_cast<cambc::handle_camp_t>(*first_target));
        std::optional<camp> target;
        if (first_target) target=*first_target;
        else {
            int scanned=0;
            for (const auto& candidate:cambc::vision::camps()) {
                if (++scanned>64 || cambc::fuel_remaining()<50000 || int(cambc::round_number())!=round) return fail();
                if (int(static_cast<cambc::handle_camp_t>(candidate))==body.target_id) {target=candidate;break;}
            }
        }
        body.can_attack=body.can_attack && target && target->is_visible() && target->is_alive() &&
            me.attack_tiles().is_set(target->position());
        for (const auto& group:groups_) if (int(static_cast<cambc::handle_camp_group_t>(group.group))==group_id) {
            const auto visible=cambc::vision::visible_tiles();
            if (!(group.spawns&visible).is_empty())
                body.group_cleared=std::holds_alternative<cambc::map::camp_group_reward_available>(group.group.state());
            break;
        }
        auto proposal=plan4::runtime::segment_proposal(runtime_model_,body);
        // Lifecycle generation is supplied only by the provider's supported
        // current observation; SDK exposes lifecycle but no generation counter.
        body.lifecycle_version=proposal.lifecycle_version;
        if (proposal.actor) body.new_visible_rival=proposal.actor->new_visible_rival;
        if (kit_observed_round_!=round || int(cambc::round_number())!=round || cambc::fuel_remaining()<45000) return fail();
        struct Guard {int round;unsigned floor;};Guard guard{round,40000};
        const plan4::WorkBudget work{[](const void* raw) {const auto& g=*static_cast<const Guard*>(raw);
            return int(cambc::round_number())==g.round && cambc::fuel_remaining()>=g.floor;},&guard};
        const int quota=std::max(0,256-navigation_.vertex_visits()-model_vertices_-physical_vertices_);
        const auto command=segment_executor_->next(proposal,body,quota,work);
        physical_vertices_+=segment_executor_->graph_vertices();
        if (command.kind==ss::ExecutionKind::abort) return fail();
        if (command.kind==ss::ExecutionKind::complete) {
            if (!body.group_cleared) macro_finish();
            return true;
        }
        if (command.kind==ss::ExecutionKind::wait || command.kind==ss::ExecutionKind::none) return true;
        if (int(cambc::round_number())!=round || cambc::fuel_remaining()<35000) return fail();
        bool success=false;
        float observed_damage=-1;
        if (command.kind==ss::ExecutionKind::attack && target && target->is_visible() && target->is_alive() &&
            me.can_attack() && me.attack_tiles().is_set(target->position())) {
#ifdef PLAN4_ONLINE_EVIDENCE
            production_prepare_clear_target(*target);
#endif
            if (int(cambc::round_number())!=round || cambc::fuel_remaining()<30000) return fail();
            const float health_before=target->health();
            me.attack(*target);success=true;
            if (int(cambc::round_number())==round && cambc::fuel_remaining()>=30000 && target->is_visible()) {
                const float health_after=target->health();
                if (int(cambc::round_number())==round && health_after<=health_before)
                    observed_damage=health_before-health_after;
            }
#ifdef PLAN4_ONLINE_EVIDENCE
            if (production_clear_target(*target)) production_clear_action(true);
#endif
        } else if (command.kind==ss::ExecutionKind::escape_move) {
            production_cancel(plan4::runtime::PhaseCensor::mode_change);
            const vec2 destination{command.destination.x,command.destination.y};
            if (distance(origin,destination)==1 && me.can_move() && !blocked().is_set(destination))
                success=me.move_absolute(destination);
        } else if (command.kind==ss::ExecutionKind::recall) {
            production_cancel(plan4::runtime::PhaseCensor::mode_change);
            const auto state=me.state();
            if (state.contains(cambc::champion_state::can_cast) && !state.contains(cambc::champion_state::recall_started) &&
                !me.is_recalling() && !me.is_submitting_gems() && !me.is_teleporting()) {me.recall();success=true;}
        }
        if (int(cambc::round_number())!=round || !segment_executor_->acknowledge(command,body,success)) return fail();
        if (observed_damage>=0) plan4::runtime::segment_attack_completed(runtime_model_,body,observed_damage);
        if (success && (command.kind==ss::ExecutionKind::escape_move || command.kind==ss::ExecutionKind::recall))
            plan4::runtime::segment_departed(runtime_model_,body);
        return true;
    }
#else
    (void)first_target;
#endif
    return false;
}

bool macro_farm_fight(const camp& target,bool walked,bool dangerous) {
    if (macro_segment_execute(&target)) return true;
    return fight(target,walked,dangerous);
}

static bool macro_budget(const void* floor) { return cambc::fuel_remaining() >= *static_cast<const unsigned*>(floor); }

void macro_tick() {
#ifdef PLAN4_OWN_ACQUISITION
    // Sample the first owner boundary before producer polling or any action.
    own_acquisition_.start(groups_, group_at_tile_, int(cambc::map::width()));
#endif
    const int attempt_sampled_round = int(cambc::round_number());
    attempt_flush();
    physical_vertices_ = 0;
    model_vertices_ = 0;
    movement_pending_ = false;
    production_requested_farm_group_ = -1;
    navigation_.begin_turn();
    navigation_.set_vertex_limit(192);
#ifdef PLAN4_STATIC_SPATIAL
    const unsigned static_round=cambc::round_number(), static_before=cambc::fuel_remaining();
    struct StaticBudget { unsigned round,floor; } static_budget{static_round,
        std::max(70000u,static_before>10000?static_before-10000:70000u)};
    const plan4::WorkBudget static_work{[](const void* value) {
        const auto& guard=*static_cast<const StaticBudget*>(value);
        return cambc::round_number()==guard.round && cambc::fuel_remaining()>=guard.floor;
    },&static_budget};
    runtime_model_.static_spatial({{int(static_round),{int(static_cast<cambc::handle_champion_t>(cambc::self()))}},
        navigation_.topology(),static_work,128});
    const int static_visits=runtime_model_.spatial_vertices();
    if (static_visits<0 || static_visits>128) throw std::runtime_error("model static vertex allowance exceeded");
    model_vertices_=static_visits;
    navigation_.set_vertex_limit(192-static_visits);
#endif
#ifdef PLAN4_ACTOR_EVIDENCE
    actor_evidence_.start();
    runtime_model_.observe_actors(actor_evidence_.frame());
    macro_actor_phase();
#endif
#ifdef PLAN4_ONLINE_EVIDENCE
    production_observe_kit();
    production_poll();
    online_evidence_.start(groups_, group_at_tile_, int(cambc::map::width()), target_);
    runtime_model_.observe_lifecycles(online_evidence_.lifecycles());
    runtime_model_.observe_producers(online_evidence_.producers());
#ifndef PLAN4_OWN_ACQUISITION
    for (int n = 0; n < online_evidence_.capture_count(); ++n) {
        const auto& event = online_evidence_.capture(n);
        const bool accepted = information_storage_ && runtime_model_.capture_receipt(*information_storage_, event.receipt);
        plan4::LogRow("capture-evidence", PLAN4_REFERENCE_VERSION).field("actor", event.receipt.provenance.observed_round >= 0
            ? online_evidence_.producers().clock.own.id : -1).field("round", event.receipt.round)
            .field("group", event.receipt.group_id).field("respawn", event.receipt.respawn_rounds)
            .field("eventStatus", int(event.receipt.event_status)).field("origin", int(event.receipt.origin))
            .field("attributed", event.receipt.actor_attribution_known).field("accepted", accepted).emit();
    }
#endif
#endif
    const auto& me = cambc::self();
#if PLAN4_MOVEMENT_RECEIPT
    movement_receipt_round_=attempt_sampled_round;
    movement_receipt_actor_=int(static_cast<cambc::handle_champion_t>(me));
#endif
    if (!me.is_alive()) macro_abort("death");
    if (macro_active_ && macro_job_.kind == plan4::JobKind::hold
        && int(cambc::round_number()) > macro_started_) macro_finish();
    if (macro_active_ && macro_job_.kind == plan4::JobKind::renew && me.is_alive()
        && cambc::map::spawn_region(team::ally).is_set(me.position())
        && me.health() >= me.max_health()) macro_finish();
    const unsigned bag = me.gem_count();
    const unsigned score = cambc::global_submitted_gems(team::ally);
    const bool tribute = me.is_submitting_gems();
    attempt_poll(attempt_sampled_round, bag, tribute);
    if (macro_seen_ && (bag != previous_bag_ || tribute != previous_tribute_)) {
        const bool completed = previous_tribute_ && !tribute && me.is_alive()
            && bag < previous_bag_ && score > previous_score_;
        plan4::LogRow("milestone", PLAN4_REFERENCE_VERSION).field("actor", int(static_cast<cambc::handle_champion_t>(me)))
            .field("round", cambc::round_number()).field("job", decision_sequence_).field("bag", bag).field("score", score)
            .field("completed", completed).field("censor", !me.is_alive() ? "death-observed" : "none").emit();
        if (completed || !me.is_alive()) macro_finish();
    }
    if (cambc::round_number() % 32 == 0 || !macro_seen_) {
        plan4::LogRow("summary", PLAN4_REFERENCE_VERSION).field("actor", int(static_cast<cambc::handle_champion_t>(me)))
            .field("round", cambc::round_number()).field("fuel", cambc::fuel_remaining()).field("bag", bag).field("score", score)
            .field("active", macro_active_).emit();
    }
    previous_bag_ = bag;
    previous_score_ = score;
    previous_tribute_ = tribute;
    macro_seen_ = true;
}

// Banking, combat and recovery use the same adapters as the extraction. Only
// the selected public destination comes from M0. Every action is freshly tested.
bool macro_bank(const plan4::Information& info, int altar_index) {
    auto& me = cambc::self();
    if (altar_index < 0 || altar_index >= info.altar_count) {
        macro_finish();
        return false;
    }
    const auto a = info.altars[altar_index];
    const vec2 altar{a.altar_position.x, a.altar_position.y};
    if (distance(me.position(), altar) <= 1) {
        if (!me.gem_count()) { macro_finish(); return true; }
        attempt_planned(info);
        for (const auto& lock : cambc::vision::altar_locks(team::enemy))
            if (lock.position() == altar) {
                if (me.can_attack() && me.attack_tiles().is_set(lock.position())) me.attack(lock);
                return true;
            }
        me.submit_gems(altar);
        macro_tribute_started();
        return true;
    }
    attempt_planned(info);
#ifdef PLAN4_ONLINE_EVIDENCE
    const int quote = plan4::distance(info, {me.position().x, me.position().y}, a.position);
    if (!alternate_route_.active()) production_begin_travel(a.altar_position, 1, quote);
#endif
    production_requested_bank_=altar_index;
    const bool moved = move_to(around(altar, 1));
    production_requested_bank_=-1;
#ifdef PLAN4_ONLINE_EVIDENCE
    if (!moved && production_tracker_.pending()) {
        if (movement_pending_) production_transition(production_tracker_.record_wait(
            *production_tracker_.pending_key(), production_clock(), true));
        else production_cancel(plan4::runtime::PhaseCensor::interrupted);
    }
#endif
    return moved || movement_pending_;
}

bool macro_bootstrap(const std::vector<camp>&, const std::vector<champion>&) {
#ifdef PLAN4_SETUP_TYPE
    if (fixture_preparing_) return true;
#endif
    auto& me = cambc::self();
    if (cambc::fuel_remaining() < 60000) return true;
    if (me.gem_count()) return bank(true);
    // The ordinary turn has already admitted danger-based recovery. A failed
    // forecast must not turn harmless camp damage into an unconditional recall.
    return false;
}

bool macro_decide(const std::vector<camp>& camps, const std::vector<champion>& enemies) {
    PLAN4_CALLER_MARK(4);
    auto& me = cambc::self();
#ifndef PLAN4_OWN_ACQUISITION
#ifdef PLAN4_SETUP_TYPE
    if (!fixture_preparing_)
#endif
    for (const auto& enemy : enemies)
        if (distance(me.position(), enemy.position()) <= 1 && fight(enemy, false, true)) return true;
#endif
    const unsigned fuel_before = static_cast<unsigned>(cambc::fuel_remaining());
#ifdef PLAN4_ONLINE_EVIDENCE
    constexpr unsigned planning_admission = 75000;
#else
    constexpr unsigned planning_admission = 85000;
#endif
    if (fuel_before < planning_admission) { macro_abort("budget-admission"); return macro_bootstrap(camps, enemies); }
    const auto started_round = cambc::round_number();
    if (!macro_active_ && int(started_round) - last_evaluation_round_ < 4) {
        // Backoff leaves fuel for public geometry without collecting another
        // observation or reusing old stock/occupancy as a current frame.
        if (me.is_alive() && !me.is_recalling() && !me.is_submitting_gems() && !me.is_teleporting()) {
            const auto position = me.position();
            if (cambc::round_number() != started_round) return true;
            navigation_.warm(int(started_round), {position.x, position.y}, 50000, 15000);
            navigation_.seal();
        }
        if (cambc::round_number() != started_round) return true;
        return macro_bootstrap(camps, enemies);
    }
    // Observation/navigation can fail before a menu is evaluated. Count that
    // attempt too, so optional work cannot starve the inherited farmer on
    // every subsequent turn. An executing contract keeps its consultation clock.
    if (!macro_active_) last_evaluation_round_ = int(started_round);
    // Keep 10k of bookkeeping headroom above the 30k execution/yield target.
    optional_floor_ = std::max(40000u, fuel_before > 65000 ? fuel_before - 65000 : 40000u);
    if (!information_storage_) {
        information_storage_ = std::make_unique<plan4::Information>();
        // An allocation may suspend execution. Never observe or act with the
        // admission clock from before that suspension.
        if (cambc::round_number() != started_round) return true;
        if (cambc::fuel_remaining() < optional_floor_ + 12000) return macro_bootstrap(camps, enemies);
    }
    auto& info = *information_storage_;
    PLAN4_CALLER_MARK(5);
    if (!observer_.observe_into(info, navigation_.lookup(), optional_floor_)) {
        macro_abort("observation-budget");
        return macro_bootstrap(camps, enemies);
    }
    macro_phase("observer", fuel_before);
    PLAN4_CALLER_MARK(6);
    info.work_budget = {macro_budget, &optional_floor_};
    if (macro_active_ && macro_job_.kind == plan4::JobKind::farm) {
        info.retained_group_id = info.groups[macro_job_.group].id;
        info.retained_second_group_id = macro_job_.second_group >= 0 ? info.groups[macro_job_.second_group].id : -1;
        info.retained_altar_id = macro_job_.altar >= 0 ? info.altars[macro_job_.altar].id : -1;
    }
    const unsigned nav_before = cambc::fuel_remaining();
    navigation_.prepare(info, std::max(optional_floor_, nav_before > 15000 ? nav_before - 15000 : optional_floor_), 15000);
    navigation_.seal();
    macro_phase("navigation", nav_before);
#ifdef PLAN4_OWN_ACQUISITION
    macro_acquisition_observe(info);
    if (cambc::round_number() != started_round || cambc::fuel_remaining() < 30000) return true;
    // Receipt delivery must precede combat that can change this turn's bag.
    for (const auto& enemy : enemies)
        if (distance(me.position(), enemy.position()) <= 1 && fight(enemy, false, true)) return true;
#endif
    if (macro_active_ && !evaluation_storage_) {
        macro_abort("missing-evaluation"); return macro_bootstrap(camps, enemies);
    }
    plan4::runtime::ActiveJobConsultation consultation;
    if constexpr (plan4::runtime::observes_execution_repairs<decltype(runtime_model_)>) {
        if (macro_active_) {
            plan4::runtime::ExecutionRepairFrame repair_frame{{info.round,{info.own.id}},&info,&evaluation_storage_->menu,
                macro_job_,decision_sequence_,macro_second_?macro_job_.second_group:macro_job_.group,
                true,info.own.channel!=plan4::Channel::none || info.in_progress_group_id>=0};
#ifdef PLAN4_ONLINE_EVIDENCE
            if (const auto* phase=production_tracker_.pending_key()) {
                repair_frame.phase=phase->phase;repair_frame.phase_known=phase->job_epoch==decision_sequence_;
            }
#endif
            const auto repair=plan4::runtime::execution_repair(runtime_model_,repair_frame);
            if (cambc::round_number()!=started_round || cambc::fuel_remaining()<30000) return true;
            using Action=plan4::runtime::ExecutionRepairAction;
            if (repair.supported && repair.action!=Action::none && repair.action!=Action::preserve) {
                plan4::LogRow("repair",PLAN4_REFERENCE_VERSION).field("actor",info.own.id).field("round",info.round)
                    .field("job",decision_sequence_).field("action",int(repair.action)).field("bankMenu",repair.bank_menu_index)
                    .field("phase",int(repair_frame.phase)).field("phaseKnown",repair_frame.phase_known)
                    .field("status",int(plan4::Status::predicted)).emit();
                if (repair.action==Action::bank) {
                    macro_job_=evaluation_storage_->menu.jobs[repair.bank_menu_index];macro_second_=false;
                    return macro_bank(info,macro_job_.altar);
                }
                if (repair.action==Action::renew || repair.action==Action::safe_retreat) {
                    macro_abort("contract-restore");
#ifdef PLAN4_ONLINE_EVIDENCE
                    production_begin_restore(info,true);
#endif
                    recovering_=true;return recover(camps,enemies);
                }
                if (repair.action==Action::replan) macro_abort("contract-replan");
            }
            if (repair_frame.protected_progress) return true;
            consultation=plan4::runtime::active_job_consultation(runtime_model_,repair_frame);
        }
    }
    const bool reconsidering=macro_active_ && consultation.supported && int(started_round)-last_evaluation_round_>=4;
    if (!macro_active_ || reconsidering) {
        last_evaluation_round_ = int(started_round);
        if (cambc::fuel_remaining() < optional_floor_ + 12000) return reconsidering?true:macro_bootstrap(camps, enemies);
        const unsigned menu_before = cambc::fuel_remaining();
        // An executing contract consults its original common menu. Neither arm
        // can add a private candidate or discard the retained physical job.
        const auto menu = reconsidering?evaluation_storage_->menu:plan4::make_menu(info);
        PLAN4_CALLER_MARK(7);
#if PLAN4_CALLER_RECEIPT
        if (auto* row=caller_receipt_.active) row->menu=menu.count;
#endif
        macro_phase("menu", menu_before);
        if (cambc::fuel_remaining() < optional_floor_ + 6000) return reconsidering?true:macro_bootstrap(camps, enemies);
        const unsigned evaluation_before = cambc::fuel_remaining();
        if (!evaluation_storage_) {
            PLAN4_CALLER_MARK(8);
            evaluation_storage_ = std::make_unique<plan4::Evaluation>();
            PLAN4_CALLER_MARK(9);
            if (cambc::round_number() != started_round) return true;
            if (cambc::fuel_remaining() < optional_floor_ + 6000) return macro_bootstrap(camps, enemies);
        }
#ifdef PLAN4_ONLINE_EVIDENCE
        // The common menu is frozen before the model sees this planning frame.
        // planning-frame-producers-begin
#if PLAN4_COMPOSITION_FIRST && defined(PLAN4_PUBLIC_COMPOSITION)
        // Immutable composition pays its startup admission before optional
        // current-mask collection. The legacy order remains the control.
        runtime_model_.observe_layout(observer_.layout_for_menu(info, menu));
#endif
#ifdef PLAN4_SPATIAL_EVIDENCE
        spatial_evidence_.start(info, menu, navigation_.topology(), camps, enemies, plan4::PublicNavigation::route_prefix,
            &navigation_, std::max(0,192-navigation_.vertex_visits()), optional_floor_, navigation_.vertex_visits());
        runtime_model_.observe_spatial(spatial_evidence_.frame());
#endif
#if defined(PLAN4_PUBLIC_COMPOSITION) && !PLAN4_COMPOSITION_FIRST
        runtime_model_.observe_layout(observer_.layout_for_menu(info, menu));
#endif
        // planning-frame-producers-end
        PLAN4_CALLER_MARK(10);
#ifndef PLAN4_OWN_ACQUISITION
        runtime_model_.observe_snapshot(info, online_evidence_.members());
#endif
#endif
        plan4::runtime::evaluate_model(info, menu, runtime_model_, *evaluation_storage_);
        PLAN4_CALLER_MARK(11);
#if PLAN4_CALLER_RECEIPT
        if (auto* row=caller_receipt_.active) {
            row->evaluated=evaluation_storage_->evaluated_count;row->budget=evaluation_storage_->budget_exhausted;
            const auto evidence=runtime_model_.model_evidence();row->model_reason=evidence.reason;row->model_supported=evidence.supported;
        }
#endif
#ifdef PLAN4_SPATIAL_EVIDENCE
        const int model_visits = runtime_model_.spatial_vertices();
        if (model_visits < model_vertices_ || model_visits > spatial_evidence_.frame().graph_quota)
            throw std::runtime_error("model spatial vertex allowance exceeded");
        model_vertices_ = model_visits;
#endif
        macro_phase("evaluation", evaluation_before);
        const auto& evaluation = *evaluation_storage_;
#ifdef PLAN4_SETUP_TYPE
        if (!fixture_accept_evaluation(info, evaluation)) {
            macro_abort("fixture-preparation");
            return true;
        }
#endif
        const auto index = evaluation.chosen;
        const auto* forecast = index >= 0 ? &evaluation.forecasts[index] : nullptr;
        const bool productive = forecast && (plan4::valid_objective(forecast->objective)
            ? forecast->objective.our_secured > 0 : forecast->completed_conversion && forecast->our_secured > 0);
        const auto job = index >= 0 ? evaluation.menu.jobs[index] : plan4::Job{};
        if (reconsidering && cambc::round_number()!=started_round) return true;
        const bool complete=!evaluation.budget_exhausted && evaluation.evaluated_count==menu.count &&
            cambc::fuel_remaining()>=45000;
        const bool same_job=job.kind==macro_job_.kind && job.group==macro_job_.group &&
            job.second_group==macro_job_.second_group && job.altar==macro_job_.altar;
        const bool retained=reconsidering && (!complete || !productive || same_job);
        if (reconsidering) {
            unsigned digest=2166136261u;
            for(int n=0;n<menu.count;++n) {
                const auto& candidate=menu.jobs[n];
                for(unsigned value:{unsigned(candidate.kind),unsigned(candidate.group+1),unsigned(candidate.second_group+1),unsigned(candidate.altar+1)})
                    digest=(digest^value)*16777619u;
            }
            const auto evidence=runtime_model_.model_evidence();
            plan4::LogRow("consult",PLAN4_REFERENCE_VERSION).field("model",runtime_model_.name()).field("actor",info.own.id)
                .field("round",info.round).field("job",decision_sequence_).field("epoch",consultation.epoch)
                .field("sequence",consultation.sequence).field("deadline",consultation.deadline).field("completed",consultation.completed)
                .field("phase",unsigned(consultation.phase)).field("candidates",menu.count).field("evaluated",evaluation.evaluated_count)
                .field("menu",digest).field("chosen",complete?index:-1).field("retained",retained).field("complete",complete)
                .field("supported",evidence.supported).field("enabled",evidence.coupling_enabled).field("reason",evidence.reason)
                .field("paths",evidence.paths).field("work",evidence.work).emit();
        }
        if (!retained) {
        if (reconsidering) macro_abort("contract-reselect");
        // A zero-output HOLD/RENEW cannot suppress the specified bootstrap.
        macro_active_ = productive;
        macro_job_ = job;
        macro_second_ = false;
        macro_started_ = static_cast<int>(cambc::round_number());
        const bool emit = productive || info.round - last_fallback_row_ >= 32;
        if (emit) ++decision_sequence_;
        runtime_model_.selected(info,job,decision_sequence_,productive);
        if (emit) {
            const auto evidence = runtime_model_.model_evidence();
            plan4::LogRow model_row("model", PLAN4_REFERENCE_VERSION,
                plan4::runtime::model_record_version<decltype(runtime_model_)>());
            model_row.field("model",runtime_model_.name()).field("actor",info.own.id)
                .field("round",info.round).field("job",decision_sequence_).field("reported",evidence.reported)
                .field("supported",evidence.supported).field("enabled",evidence.coupling_enabled).field("reason",evidence.reason)
                .field("samples",evidence.samples).field("unknown",evidence.unknown).field("paths",evidence.paths)
                .field("work",evidence.work).field("revision",evidence.revision);
            plan4::runtime::append_model_coverage(model_row, runtime_model_);
#if PLAN4_ACTOR_BIND_DIAGNOSTICS && defined(PLAN4_ACTOR_EVIDENCE)
#include "actor-bind-diagnostics.h"
#endif
            model_row.emit();
            struct ReceiptContext { int actor,round;unsigned job;const char* model;int count=0; } receipt_context{
                info.own.id,info.round,decision_sequence_,runtime_model_.name()};
            runtime_model_.prediction_receipts(info,job,[](const plan4::runtime::ModelPredictionEvent& event,void* context) {
                auto& sink=*static_cast<ReceiptContext*>(context);
                if (sink.count>=8 || event.predicted_round<sink.round || event.predicted_round>sink.round+plan4::max_horizon) return false;
                plan4::LogRow("model-event", PLAN4_REFERENCE_VERSION).field("model",sink.model).field("actor",sink.actor)
                    .field("round",sink.round).field("job",sink.job).field("ordinal",sink.count++)
                    .field("predictedRound",event.predicted_round).field("phase",event.phase).field("kind",event.kind)
                    .field("subjectActor",event.actor_id).field("group",event.group_id).field("amountMilli",event.amount_milli)
                    .field("bagAfter",event.bag_after).field("status",int(plan4::Status::predicted)).emit();
                return sink.count<8;
            },&receipt_context);
        }
        unsigned digest = 2166136261u;
        auto mix = [&](unsigned n) { digest = (digest ^ n) * 16777619u; };
        mix(info.round); mix(info.own.bag); mix(info.our_score); mix(info.enemy_score);
        for (int g = 0; g < info.group_count; ++g) { mix(info.groups[g].id); mix(unsigned(info.groups[g].lifecycle)); mix(unsigned(info.groups[g].health)); }
        unsigned menu_digest = 2166136261u;
        for (int j = 0; j < evaluation.menu.count; ++j) {
            const auto& candidate = evaluation.menu.jobs[j];
            menu_digest = (menu_digest ^ unsigned(candidate.kind)) * 16777619u;
            menu_digest = (menu_digest ^ unsigned(candidate.group + 1)) * 16777619u;
            menu_digest = (menu_digest ^ unsigned(candidate.second_group + 1)) * 16777619u;
            menu_digest = (menu_digest ^ unsigned(candidate.altar + 1)) * 16777619u;
        }
        if (emit) plan4::LogRow("decision", PLAN4_REFERENCE_VERSION).field("model", runtime_model_.name())
            .field("actor", int(static_cast<cambc::handle_champion_t>(me))).field("round", info.round)
            .field("job", decision_sequence_).field("kind", int(job.kind)).field("group", job.group)
            .field("second", job.second_group).field("altar", job.altar).field("info", digest).field("menu", menu_digest)
            .field("candidates", evaluation.menu.count).field("evaluated", evaluation.evaluated_count)
            .field("first", forecast ? forecast->first_conversion_round : -1)
            .field("secured", forecast ? forecast->our_secured : 0).field("z", forecast ? forecast->terminal_z : 0)
            .field("terminalUnknown", forecast && forecast->terminal_unknown).field("fuelBefore", fuel_before)
            .field("fuelAfter", cambc::fuel_remaining())
            .field("fallback", productive ? "none" : evaluation.budget_exhausted ? "budget" : "bootstrap").emit();
        if (emit) {
            if (forecast && plan4::valid_objective(forecast->objective)) {
                const auto& value = forecast->objective;
                plan4::LogRow("objective", PLAN4_REFERENCE_VERSION).field("actor", info.own.id).field("round", info.round)
                    .field("job", decision_sequence_).field("paths", value.paths).field("zSum", value.terminal_z)
                    .field("ourSecuredSum", value.our_secured).field("enemySecuredSum", value.enemy_secured)
                    .field("firstConversionSum", value.first_conversion_round).emit();
            }
            const auto* group = job.group >= 0 ? &info.groups[job.group] : nullptr;
            plan4::LogRow("input", PLAN4_REFERENCE_VERSION).field("actor", info.own.id).field("round", info.round)
                .field("job", decision_sequence_).field("x", info.own.position.x).field("y", info.own.position.y)
                .field("bag", info.own.bag).field("hp", int(info.own.health)).field("maxHp", int(info.own.maximum_health))
                .field("strength", int(me.strength())).field("focus", int(me.focus())).field("champion", int(me.champion_name()))
                .field("monsterShards", monster_shards_).field("jungleShards", jungle_shards_)
                .field("groupId", group ? group->id : -1).field("visible", group && group->visible)
                .field("healthStatus", group ? int(group->health_provenance.status) : 3)
                .field("groupHp", group ? int(group->health) : -1).field("incoming", group ? int(group->incoming_damage) : -1)
                .field("gems", group ? int(group->gems) : -1).field("nominal", job.nominal_completion)
                .field("abort", forecast ? int(forecast->abort_reason) : -1).emit();
        }
        if (!productive && emit) last_fallback_row_ = info.round;
        }
    }
    navigation_.seal();
    if (cambc::round_number() != started_round) { macro_finish(); return true; }
    if (!macro_active_) return macro_bootstrap(camps, enemies);
    if (cambc::fuel_remaining() < 45000) return true;
    if (static_cast<int>(cambc::round_number()) - macro_started_ >= plan4::max_horizon) {
        plan4::LogRow("abort", PLAN4_REFERENCE_VERSION).field("reason", "horizon").field("job", decision_sequence_)
            .field("actor", int(static_cast<cambc::handle_champion_t>(me))).field("round", cambc::round_number()).emit();
        macro_finish();
        return false;
    }
    if (macro_job_.kind == plan4::JobKind::bank) return macro_bank(info, macro_job_.altar);
    if (macro_job_.kind == plan4::JobKind::renew) {
        attempt_end(plan4::attempts::Outcome::replaced);
#ifdef PLAN4_ONLINE_EVIDENCE
        production_begin_restore(info);
#endif
        return recover(camps,enemies);
    }
    if (macro_job_.kind == plan4::JobKind::hold) {
        attempt_end(plan4::attempts::Outcome::replaced);
        return true;
    }
    int selected = macro_second_ ? macro_job_.second_group : macro_job_.group;
    if (selected < 0 || selected >= info.group_count || selected >= static_cast<int>(groups_.size())) {
        macro_finish();
        return false;
    }
    auto* state = &info.groups[selected];
    attempt_planned(info);
    if (state->visible && (state->lifecycle == plan4::Lifecycle::respawning || state->lifecycle == plan4::Lifecycle::captured)) {
        if (!macro_second_ && macro_job_.second_group >= 0) {
            macro_second_ = true;
            selected = macro_job_.second_group;
            state = &info.groups[selected];
        } else return macro_bank(info, macro_job_.altar);
    }
    target_ = selected;
    partial_clear_ = false;
    const auto& entry = groups_[selected];
    if (state->reward_available) { macro_farm_move(entry.spawns,state->id); return true; }
    // Reuse B0's conservative admission; an unobserved group remains a scout.
    const float dps = (me.strength() * 1.125f + me.focus() * 0.12f) * (monster_shards_ >= 5 ? 1.1f : 1.0f);
    bool segment_admission=false;
    segment_admission=plan4::runtime::safe_segments_enabled(runtime_model_);
    if (!segment_admission && !fundamentals::affordable_camp(me.health(), me.max_health(), state->health,
            state->incoming_damage, dps)) {
        plan4::LogRow("abort", PLAN4_REFERENCE_VERSION).field("reason", "unsafe-clear").field("job", decision_sequence_)
            .field("actor", int(static_cast<cambc::handle_champion_t>(me))).field("round", cambc::round_number()).emit();
        macro_finish();
        return false;
    }
    std::optional<camp> monster_target;
    float weakest = std::numeric_limits<float>::max();
    for (const auto& monster : camps) {
        if (!entry.spawns.is_set(monster.spawn_position()) || !monster.is_alive()) continue;
        const float score = monster.health() + distance(me.position(), monster.position()) * 100.0f;
        if (score < weakest) { weakest = score; monster_target = monster; }
    }
    if (monster_target) {
        if (macro_farm_fight(*monster_target,false,entry.incoming>0)) return true;
    }
    bitboard goals;
    if (monster_target) goals = around(monster_target->position(), me.attack_range());
    else for (const auto& monster : entry.group) goals |= around(monster.spawn_position(), 1);
    if (macro_farm_move(goals,state->id)) {
        for (const auto& monster : cambc::vision::camps())
            if (entry.spawns.is_set(monster.spawn_position()) && monster.is_alive()
                && me.attack_tiles().is_set(monster.position())) {
                macro_farm_fight(monster,true,entry.incoming>0);break;
            }
    }
    return true;
}
