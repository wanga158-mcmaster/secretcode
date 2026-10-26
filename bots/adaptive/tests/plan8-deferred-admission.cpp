// Pure deferred-admission and exact one-turn slack oracles; no SDK calls.
// Bounded policy fixtures copied from plan8-policy.cpp and kept on the heap.
#include "../canary-and-cashflow/src/policy.h"
#include "../common/src/admission_slack.h"
#include "../earned-center/src/admission_wait.h"
#include "../earned-center/src/trace.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <memory>
using namespace p8;
using namespace p8::canary;
namespace {
int checks = 0;
const char* scenario = "fixture";
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL [%s]: %s\n",scenario,message); std::exit(1); }
}
template<class T> p6::Fact<T> fact(T v) { return {v,p6::Source::synthetic_fixture}; }
Route path(std::initializer_list<p6::Point> points) {
    Route r; r.verified = true; r.count = int(points.size());
    int i = 0; for (auto p : points) r.tiles[i++] = p; return r;
}
Plan plan(Organization org, int group = 7, int self = 1) {
    Plan p; p.organization = org; p.group = group; p.collector = 1; p.altar = 4;
    p.crew[0] = 1; p.crew_count = 1;
    p.ingress = path({{0,0},{1,0},{2,0}}); p.capture = path({{2,0},{3,0}});
    p.bank = path({{3,0},{4,0}}); p.renewal = path({{4,0},{5,0}});
    p.forecast_rounds = 20; p.remaining_rounds = 100; p.capture_window = 5;
    p.gem_bearing = p.complete_verified = p.rally_legal = true;
    p.pads = {{{2,0},{2,1},{2,2}}};
    if (org == Organization::H || org == Organization::J) {
        p.crew[1] = 2; p.crew_count = 2;
        if (org == Organization::J) { p.crew[2] = 3; p.crew_count = 3; }
        if (self != 1) {
            p.ingress = path({{0,0},{1,0},{2,1}}); p.capture = path({{2,1},{3,0}});
            p.release = path({{3,0},{3,1}});
        }
    } else { p.collector = self; p.crew[0] = self; }
    return p;
}
p6::CycleCertificate cert(const p6::Frame& f, int actor, int group) {
    p6::CycleCertificate c; c.clock = f.clock; c.actor = actor; c.group = group;
    const auto* a = f.visible(actor);
    if (a) c.clear = c.hold = c.bank = c.recovery = {a->health,a->max_health,10,0,0,0,true};
    c.route_known = c.bank_executable = c.renewal_reachable = c.solo = true;
    return c;
}
void event(p6::ActorSchedule& a, p6::EventKind kind, int id, int tick, int production = -1) {
    p6::Event e; e.kind = kind; e.event_id = id; e.tick = tick;
    e.production_index = production; e.gold_cost = fact<p6::Gems>(0);
    e.waypoint_id = kind == p6::EventKind::renewal ? 90+a.actor_id : -1;
    e.source = p6::Source::synthetic_fixture; a.events[a.event_count++] = e;
}
p6::Schedule schedule(const p6::Frame& f, int target, std::array<int,3> gains, bool shared = false, int duration = 20) {
    p6::Schedule s; s.actor_count = 3; s.target_id = target;
    for (int i = 0; i < 3; ++i) {
        auto& a = s.actors[i]; a.actor_id = i+1;
        const auto source = a.actor_id == f.clock.self ? p6::Source::self_observed : p6::Source::visible_subject;
        const auto* subject = f.visible(i+1);
        a.initial_bag = {subject ? subject->bag : 0,source};
        a.initial_gold = {a.actor_id == f.clock.self ? f.own.gold : 0,
            a.actor_id == f.clock.self ? p6::Source::self_observed : p6::Source::conservative};
        a.occupied_turns = fact<p6::Gems>(duration);
        if (gains[i] || shared) {
            const int ix = shared ? 0 : s.production_count++;
            if (!shared || i == 0) {
                auto& p = s.productions[ix]; p.event_id = 1000+i; p.tick = duration-10;
                p.gems = fact<p6::Gems>(shared ? gains[0]+gains[1]+gains[2] : gains[i]);
                p.gold = fact<p6::Gems>(0); p.source = p6::Source::synthetic_fixture;
                p.holder_count = shared ? 3 : 1;
                if (shared) p.holders = {1,2,3}; else p.holders[0] = i+1;
            }
            event(a,p6::EventKind::production,s.productions[ix].event_id,duration-10,ix);
            event(a,p6::EventKind::tribute,2000+i,duration-5);
        }
        event(a,p6::EventKind::renewal,3000+i,duration);
    }
    if (shared) s.production_count = 1;
    return s;
}
struct Fixture {
    std::unique_ptr<Inputs> in = std::make_unique<Inputs>();
    EvidenceLedger ledger{1}; ProbeBudget budget{}; LocalFrame local{}; Work work{};
    ContextDeclaration solo{}, supported{};
    explicit Fixture(bool bracketed_predictions = false) {
        auto& f = in->frame; f.clock = {0,1,0}; f.own.gold = 15;
        for (int id = 1; id <= 3; ++id) {
            p6::Actor a; a.id = id; a.pick_ordinal = id-1;
            a.type = id == 1 ? p6::Champion::tavi : id == 2 ? p6::Champion::marcella : p6::Champion::varg;
            a.health = a.max_health = 100; a.strength = 20;
            a.team = p6::Team::ally; a.alive = a.can_move = a.can_attack = true;
            a.position = id == 1 ? p6::Point{0,0} : p6::Point{2,id-1};
            if (id == 1) f.own.actor = a;
            else f.actors[f.actor_count++] = {a,f.clock,id,p6::Knowledge::exact};
        }
        p6::Group g; g.id = 7; g.original_spawn_visible = g.all_spawns_visible = g.uncontested_now = true;
        g.lifecycle = p6::Lifecycle::living; f.group = {g,f.clock,7,p6::Knowledge::exact};
        in->settings = {2000,1000,5}; in->settings_identity = {17,p6::Source::queried_setting};
        in->draft.champions = {p6::Champion::tavi,p6::Champion::marcella,p6::Champion::varg,
            p6::Champion::other,p6::Champion::other};
        in->draft.actors = {1,2,3,-1,-1}; in->draft.picks = 3;
        in->target_group = 7; in->offer_count = 4; in->independent = 0;
        in->own_state = {{100,100,20,10,1,0,99},f.clock,1,p6::Knowledge::exact};
        in->threat_regime = {0,f.clock,1,p6::Knowledge::exact};
        for (int i = 0; i < 4; ++i) {
            auto& o = in->offers[i];
            o.plan = plan(i == 0 ? Organization::G : i == 1 ? Organization::U : i == 2 ? Organization::H : Organization::J,i ? 7 : 9);
        }
        in->offers[0].schedule = schedule(f,9,{10,10,10});
        in->offers[1].schedule = schedule(f,7,{20,10,10});
        in->offers[2].schedule = schedule(f,7,{40,0,10});
        in->offers[3].schedule = schedule(f,7,{27,27,26},true);
        solo.key.settings_id = 17; solo.key.route_hash = plan_route_hash(in->offers[1].plan);
        solo.key.actor_id = 1; solo.key.champion = int(p6::Champion::tavi); solo.key.role = 0;
        solo.key.source_group = 7; solo.key.route_template = 0; solo.key.endpoint = 4;
        solo.key.threat_regime = 0; solo.key.participants[0] = 1; solo.key.participant_count = 1;
        solo.key.build = {99,100,20,10,1};
        solo.envelope = {{20,100},{100,100},{20,20},{10,10},{1,1},{0,200},0};
        if (bracketed_predictions) {
            for (int h = 0; h < 3; ++h) solo.hypotheses[h].offset = {2*h,2*h+4};
        }
        supported = solo; supported.kind = EpisodeKind::full_organization;
        supported.key.role = 1; supported.key.participant_count = 2; supported.key.participants[1] = 2;
        supported.key.route_hash = plan_route_hash(in->offers[2].plan);
        in->component_slot = ledger.predeclare(solo); in->supported_slot = ledger.predeclare(supported);
        in->component_entry = solo.key; in->supported_entry = supported.key;
        in->offers[1].context_slot = in->component_slot; in->offers[1].entry = solo.key;
        in->offers[2].context_slot = in->supported_slot; in->offers[2].entry = supported.key;
        tick(0);
        // This API deliberately admits only the sole current ordinary U.
        in->offers[0] = in->offers[1]; in->offer_count = 1; in->independent = 0;
    }
    void tick(int round) {
        auto& f = in->frame; f.clock.round = round;
        f.group.clock = f.clock;
        for (int i = 0; i < f.actor_count; ++i) f.actors[i].clock = f.clock;
        in->own_state.clock = in->threat_regime.clock = f.clock;
        in->own_state.subject = in->threat_regime.subject = f.clock.self;
        auto& n = in->own_state.value;
        n.health = int(f.own.actor.health); n.max_health = int(f.own.actor.max_health);
        n.strength = int(f.own.actor.strength); n.bag = int(f.own.actor.bag);
        in->windows.valid = true; in->windows.source = p6::Source::private_history;
        in->windows.deadlines = in->windows.windows = {120,120,120};
        in->windows.sampled_actor_id = f.clock.self; in->windows.sampled_round = round;
        for (int j = 0; j < 4; ++j) {
            auto& o = in->offers[j]; o.safety_count = 3;
            for (int i = 0; i < 3; ++i) o.safety[i] = cert(f,i+1,participant(o.plan,i+1) ? o.plan.group : 9);
        }
        local.frame = f; local.current_round = round; local.safety_count = 3;
        for (int i = 0; i < 3; ++i) local.safety[i] = cert(f,i+1,7);
        local.legal = {{},f.clock,f.clock.self,p6::Knowledge::exact};
        auto& l = local.legal.value;
        l.safe_now = l.attack_legal = l.altar_legal = l.recall_safe = l.renewal_productive = true;
        for (auto& a : l.routes) { a.assumptions_hold = a.next_move_legal = true; a.next_tile = {1,0}; }
    }
    void probe(int offer) {
        in->probe.offer = offer; in->probe.endpoint_distinguishes = true;
        in->probe.named_continuations = {Organization::U,Organization::H};
        for (int b = 0; b < 3; ++b) {
            in->probe.continuation_plans[b] = in->offers[b == 2 ? 0 : b+1].plan;
            in->probe.branches[b] = schedule(in->frame,b == 2 ? 9 : 7,
                b == 0 ? std::array<int,3>{20,10,10} : b == 1 ? std::array<int,3>{40,0,10} : std::array<int,3>{15,10,10},false,25);
            in->probe.safety_count[b] = 3;
            for (int i = 0; i < 3; ++i) in->probe.safety[b][i] = cert(in->frame,i+1,
                participant(in->probe.continuation_plans[b],i+1) ? in->probe.continuation_plans[b].group : 9);
        }
        in->offers[offer].marked_extra_turns = 2;
    }
    void cycle_success(int start, int receipt, Interval duration, bool bracketed = false, bool organization = false) {
        EpisodeRequest r; r.context = organization ? in->supported_slot : in->component_slot; r.issue_round = start;
        r.deadline = start+100; r.action = 0; r.nominal_duration = 20;
        r.premises = {organization ? supported.key : solo.key,in->own_state.value,all_premises};
        const auto serial = ledger.issue(r); check(serial != 0,"typed own episode uses real retained plan hash");
        Observation o; o.round = start+duration.upper; o.kind = ObservationKind::completed; o.premises = r.premises;
        o.duration = {duration,bracketed ? DurationSource::legally_bracketed : DurationSource::personally_exact};
        auto& c = o.receipts; c.source = ReceiptSource::own_observation;
        c.production = c.payout_owned = c.conversion = c.renewed = c.attribution_excludes_other_bag_loss = true;
        c.payout_event = c.conversion_event = c.renewal_event = receipt;
        c.production_round = start+1; c.payout_round = start+4; c.conversion_round = start+10;
        c.renewal_round = o.round; c.positive_reward_lower = 20;
        check(ledger.resolve(serial,o).full_positive,"full own cycle receipt is applicable and attributable");
    }
    void prior_success() {
        cycle_success(1,1,{22,24},true);
        const auto* slot = ledger.state(in->component_slot);
        int explanations = 0;
        for (int h = 0; h < slot->declaration.hypothesis_count; ++h) explanations += slot->consistent[h];
        check(slot->positive_completions == 1 && explanations >= 2,
              "first bracketed full success leaves multiple predeclared mechanical explanations");
        tick(26);
    }
    void qualify() {
        cycle_success(1,1,{20,20}); cycle_success(22,2,{20,20});
        tick(43);
    }
    void qualify_supported() {
        qualify();
        const Premises premises{supported.key,in->own_state.value,all_premises};
        check(ledger.begin_organization_campaign(in->component_slot,in->supported_slot,43,{premises,true,true,true}),
              "qualified component opens the actual supported-membership commissioning campaign");
        cycle_success(44,3,{20,20},false,true); cycle_success(65,4,{20,20},false,true);
        tick(86);
        check(ledger.organization_qualified(in->supported_slot,supported.key,86),
              "supported organization has two of its own applicable complete cycles");
    }
};

LocalFrame admission_frame(const Fixture& f) {
    auto local = f.local; local.admission_only = true;
    local.fuel_remaining = 20000; local.action_reserve = 8000;
    local.legal.value = {}; local.legal.value.safe_now = true;
    local.milestones = {};
    return local;
}
void no_progress(const Work& work,int round,int owner_turn,std::uint64_t generation,
                 std::uint64_t serial,int phase_started) {
    check(work.active && work.route_witness_active && !work.stop_requested,"live route witness retained");
    check(work.stage == PhysicalStage::travel && work.failure == PhysicalFailure::none,"travel stage unchanged");
    check(work.last_round == round && work.owner_turn == owner_turn,"only explicit owner clock consumed");
    check(work.generation == generation && work.serial == serial && work.phase_started == phase_started,
          "generation, receipt serial and phase-start identity preserved");
    check(!work.pending && !work.clearance_seen && !work.productive_action_seen &&
          !work.payout_seen && !work.bank_seen && !work.renewal_seen && !work.cycle_completed,
          "wait emits no action, endpoint or productive proof");
    check(!work.tribute_started && !work.helper_released && !work.helper_bag_return &&
          !work.release_admitted && !work.payout_lower && !work.bank_lower && !work.tribute_starting_bag,
          "wait cannot create conversion, release or owned rewards");
    for (int at : work.cursor) check(at == 0,"wait never moves a route cursor");
}
void equal_command(const Command& actual,const Command& expected) {
#define COMMAND_FIELD(name) check(actual.name == expected.name,"command field: " #name)
    COMMAND_FIELD(action); COMMAND_FIELD(stage); COMMAND_FIELD(reason); COMMAND_FIELD(clock);
    COMMAND_FIELD(target); COMMAND_FIELD(group); COMMAND_FIELD(leg); COMMAND_FIELD(token); COMMAND_FIELD(route_hash);
#undef COMMAND_FIELD
}
void equal_plan(const Plan& actual,const Plan& expected,bool inactive = false) {
#define PLAN_FIELD(name) check(actual.name == expected.name,"Plan field: " #name)
    PLAN_FIELD(organization); PLAN_FIELD(group); PLAN_FIELD(collector); PLAN_FIELD(altar);
    PLAN_FIELD(crew); PLAN_FIELD(pads); PLAN_FIELD(crew_count); PLAN_FIELD(forecast_rounds);
    PLAN_FIELD(remaining_rounds); PLAN_FIELD(capture_window); PLAN_FIELD(gem_bearing);
    PLAN_FIELD(complete_verified); PLAN_FIELD(rally_legal);
#undef PLAN_FIELD
    for (Leg leg : {Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
        const auto& a = route_for(actual,leg); const auto& e = route_for(expected,leg);
        check(a.count == e.count && a.verified == e.verified && a.blocker_assumptions == e.blocker_assumptions,
              "all five route count/verified/blocker headers match full-copy oracle");
        check(e.count >= 0 && e.count <= max_route_tiles,"oracle route count is bounded before coordinate reads");
        const int limit = inactive ? max_route_tiles : e.count;
        for (int i = 0; i < limit; ++i) check(a.tiles[i] == e.tiles[i],"retained route payload matches independent full copy");
    }
    check(plan_route_hash(actual) == plan_route_hash(expected),"complete retained route hash matches full-copy oracle");
    check(plan_return_hash(actual) == plan_return_hash(expected),"retained return component hash matches full-copy oracle");
}
void equal_work(const Work& actual,const Work& expected,bool inactive = false) {
    equal_plan(actual.plan,expected.plan,inactive);
#define WORK_FIELD(name) check(actual.name == expected.name,"Work field: " #name)
    WORK_FIELD(stage); WORK_FIELD(failure); WORK_FIELD(cursor); WORK_FIELD(last_round); WORK_FIELD(owner_turn);
    WORK_FIELD(phase_started); WORK_FIELD(phase_limit); WORK_FIELD(assembly_turns); WORK_FIELD(hold_turns);
    WORK_FIELD(hold_extensions); WORK_FIELD(bank_queue_turns); WORK_FIELD(active); WORK_FIELD(stop_requested);
    WORK_FIELD(route_witness_active); WORK_FIELD(clearance_seen); WORK_FIELD(payout_seen); WORK_FIELD(bank_seen);
    WORK_FIELD(productive_action_seen); WORK_FIELD(renewal_seen); WORK_FIELD(cycle_completed); WORK_FIELD(helper_released);
    WORK_FIELD(helper_bag_return); WORK_FIELD(tribute_started); WORK_FIELD(pending); WORK_FIELD(release_admitted);
    WORK_FIELD(payout_lower); WORK_FIELD(bank_lower); WORK_FIELD(tribute_starting_bag);
    WORK_FIELD(serial); WORK_FIELD(generation); WORK_FIELD(started_clock);
#undef WORK_FIELD
    equal_command(actual.issued,expected.issued);
}
// Original physical predicate before the sole-U/G certificate deduplication.
// This deliberately performs crew_available and then a second own lookup.
bool original_physical_admissible(const Plan& selected,const LocalFrame& local) {
    if (!local.fresh() || local.fuel_resumed || local.fuel_remaining < local.action_reserve ||
        !local.legal.value.safe_now || !plan_valid(selected,local.frame.clock.self) ||
        !(local.frame.own.actor.position == selected.ingress.tiles[0]) ||
        !crew_available(selected,local,false)) return false;
    if (selected.organization == Organization::U || selected.organization == Organization::G)
        return independent_safety(*local.certificate(local.frame.clock.self,selected.group));
    if (selected.organization == Organization::H) {
        const auto* collector = local.frame.visible(selected.collector);
        if (!collector) return false;
        if (local.frame.clock.self != selected.collector &&
            !(collector->position == selected.pads[crew_index(selected,selected.collector)])) return false;
        if (local.frame.clock.self != selected.collector && local.frame.own.actor.bag > 0) {
            if (!route_valid(selected.release) || selected.release.count > selected.bank.count) return false;
            for (int i = 0; i < selected.release.count; ++i)
                if (!(selected.release.tiles[i] == selected.bank.tiles[i])) return false;
        }
    }
    return true;
}
// Independent pre-optimization Work.start semantics. Retain the original full
// Plan assignment and full second physical check, never the private helper.
bool original_start(Work& work,const Plan& selected,const LocalFrame& in) {
    if (work.active || work.generation == std::numeric_limits<std::uint64_t>::max() ||
        in.frame.clock.round <= work.last_round || !original_physical_admissible(selected,in)) return false;
    ++work.generation; work.started_clock = in.frame.clock;
    work.last_round = in.frame.clock.round-1;
    work.plan = selected; work.stage = PhysicalStage::travel; work.failure = PhysicalFailure::none;
    work.cursor = {}; work.active = work.route_witness_active = true; work.stop_requested = false;
    work.clearance_seen = work.payout_seen = work.bank_seen = work.renewal_seen = work.cycle_completed = false;
    work.productive_action_seen = false;
    work.helper_released = work.helper_bag_return = work.tribute_started = work.pending = work.release_admitted = false;
    work.payout_lower = work.bank_lower = work.tribute_starting_bag = 0;
    work.assembly_turns = work.hold_turns = work.hold_extensions = work.bank_queue_turns = 0;
    work.phase_started = work.owner_turn;
    work.phase_limit = std::min(selected.remaining_rounds,std::max(8,2*selected.forecast_rounds+4));
    return true;
}
template<class ObserveLocal>
Admission original_admit(const Inputs& in,EvidenceLedger& ledger,ProbeBudget& probes,
                         Arm arm,Work& work,ObserveLocal&& observe_local) {
    Admission out;
    if (work.active) { out.rejection = Rejection::stale; return out; }
    if (in.offer_count != 1 || in.independent != 0) { out.rejection = Rejection::malformed; return out; }
    const auto current = decide(in,ledger,probes,arm);
    if (current.option < 0) return out;
    if (current.option != 0) { out.rejection = Rejection::malformed; return out; }
    const auto& offer = in.offers[current.option];
    if (offer.diagnostic_segment || offer.marked_extra_turns != 0 || current.deliberate_probe) {
        out.rejection = Rejection::evidence; return out;
    }
    if (offer.plan.organization != Organization::U || offer.plan.crew_count != 1 ||
        offer.plan.collector != in.frame.clock.self || offer.plan.crew[0] != in.frame.clock.self ||
        offer.plan.ingress.count < 2) { out.rejection = Rejection::malformed; return out; }
    const auto& local = std::forward<ObserveLocal>(observe_local)(current);
    if (!local.admission_only || !local.fresh() || !(local.frame.clock == in.frame.clock) ||
        !(current.clock == in.frame.clock) || work.active) { out.rejection = Rejection::stale; return out; }
    if (!original_physical_admissible(offer.plan,local)) { out.rejection = Rejection::unsafe; return out; }
    if (current.record_episode) {
        Premises premises{offer.entry,in.own_state.value,all_premises};
        if (offer.plan.organization == Organization::H) {
            const auto* slot = ledger.state(offer.context_slot);
            if (!slot) return out;
            if (!slot->campaign_started && !ledger.begin_organization_campaign(in.component_slot,
                offer.context_slot,in.frame.clock.round,{premises,true,true,true})) return out;
        }
        EpisodeRequest request;
        request.context = offer.context_slot; request.issue_round = in.frame.clock.round;
        request.deadline = request.issue_round+std::min(forecast_window,offer.plan.remaining_rounds);
        request.nominal_duration = offer.plan.forecast_rounds;
        request.action = static_cast<int>(offer.plan.organization); request.premises = premises;
        out.episode = ledger.issue(request);
        if (!out.episode) return out;
    }
    if (current.deliberate_probe && !probes.spend(in.frame.clock.round/project_rounds)) {
        if (out.episode) ledger.invalidate_active(in.frame.clock.round);
        out.rejection = Rejection::probe_limit; return out;
    }
    out.started = original_start(work,offer.plan,local);
    out.rejection = out.started ? Rejection::none : Rejection::unsafe;
    if (!out.started && out.episode) ledger.invalidate_active(in.frame.clock.round);
    return out;
}
template<class T> concept PublicUncheckedStart = requires(T& work,const Plan& plan,const LocalFrame& local) {
    work.start_after_physical(plan,local);
};
static_assert(!PublicUncheckedStart<Work>,"physical-check reuse must not become public startup authority");
void equal_evidence(const Fixture& actual,const Fixture& expected) {
    check(actual.budget.used == expected.budget.used && actual.budget.overrun == expected.budget.overrun &&
        actual.budget.spent_epochs == expected.budget.spent_epochs,"private proof reuse preserves all persistent probe charges");
    check(actual.ledger.size() == expected.ledger.size() &&
        actual.ledger.lifetime_failure_count() == expected.ledger.lifetime_failure_count(),"private ledger size and failure charges match");
    for (int i = 0; i < actual.ledger.size(); ++i) {
        const auto& a = *actual.ledger.state(i); const auto& e = *expected.ledger.state(i);
        check(a.declaration.key == e.declaration.key && a.consistent == e.consistent,"private declarations and hypotheses match");
#define SLOT_FIELD(name) check(a.name == e.name,"evidence field: " #name)
        SLOT_FIELD(attempts); SLOT_FIELD(completed); SLOT_FIELD(failures); SLOT_FIELD(unresolved);
        SLOT_FIELD(consecutive_unresolved); SLOT_FIELD(positive_completions); SLOT_FIELD(ring_next);
        SLOT_FIELD(latest_terminal); SLOT_FIELD(previous_positive); SLOT_FIELD(latest_positive);
        SLOT_FIELD(eligible_epoch); SLOT_FIELD(retired_epoch); SLOT_FIELD(eligible_bits); SLOT_FIELD(eligible_count);
        SLOT_FIELD(model_set_failed); SLOT_FIELD(safety_suspended); SLOT_FIELD(unresolved_suspended);
        SLOT_FIELD(campaign_started); SLOT_FIELD(campaign_failed); SLOT_FIELD(material_premise_changed);
        SLOT_FIELD(rebuild_reserved); SLOT_FIELD(campaign_episodes);
#undef SLOT_FIELD
    }
    const auto* a = actual.ledger.active(); const auto* e = expected.ledger.active();
    check(bool(a) == bool(e),"same actual private episode remains active");
    if (a && e) {
        check(a->serial == e->serial && a->context == e->context && a->issue_round == e->issue_round &&
            a->deadline == e->deadline && a->action == e->action && a->premises.key == e->premises.key,
            "issued episode retains exact identity, clock, action and deadline");
    }
}
void physical_reuse_oracle() {
    for (Organization org : {Organization::U,Organization::G}) for (int fault = 0; fault < 13; ++fault) {
        scenario = "single own-certificate physical reuse equals original U/G predicate";
        auto f = std::make_unique<Fixture>(); auto local = admission_frame(*f);
        auto selected = std::make_unique<Plan>(::plan(org));
        switch (fault) {
        case 1: selected->crew[0] = 99; break;
        case 2: selected->crew_count = 2; selected->crew[1] = 2; break;
        case 3: local.frame.own.actor.alive = false; break;
        case 4: local.frame.own.actor.channel = p6::Channel::tribute; break;
        case 5: local.safety_count = 0; break;
        case 6: ++local.safety[0].clock.revision; break;
        case 7: local.safety[0].solo = false; break;
        case 8: local.safety[0].hold.assigned_timed_heal = 1; break;
        case 9: local.fuel_remaining = 7999; break;
        case 10: selected->ingress.verified = false; break;
        case 11: local.legal.value.safe_now = false; break;
        case 12: local.frame.own.actor.position = {1,0}; break;
        }
        const bool old = original_physical_admissible(*selected,local), actual = physical_admissible(*selected,local);
        check(actual == old && actual == (fault == 0),"same finite own identity/alive/channel/certificate/route/fuel rejection");
    }
    for (Organization org : {Organization::H,Organization::J}) for (bool missing : {false,true}) {
        scenario = "dependent physical admission retains original full crew predicate";
        auto f = std::make_unique<Fixture>(); auto selected = std::make_unique<Plan>(::plan(org));
        if (missing) f->local.frame.actors[0].knowledge = p6::Knowledge::unknown;
        check(physical_admissible(*selected,f->local) == original_physical_admissible(*selected,f->local) &&
            physical_admissible(*selected,f->local) == !missing,"H/J retain current visible crew requirements");
    }
}
void admission_reuse_oracle() {
    for (int variant = 0; variant < 11; ++variant) {
        scenario = "private single physical proof equals original two-check transaction";
        auto actual = std::make_unique<Fixture>(), expected = std::make_unique<Fixture>();
        const Arm arm = variant == 1 ? Arm::control : Arm::active;
        for (Fixture* f : {actual.get(),expected.get()}) {
            if (variant == 2) f->work.generation = std::numeric_limits<std::uint64_t>::max();
            if (variant == 3) f->work.last_round = f->in->frame.clock.round;
            if (variant == 8) f->in->offers[0].capital_cost = 16;
            if (variant == 9) { f->qualify(); check(f->budget.spend(0),"existing probe charge is retained across ordinary admission"); }
        }
        int actual_calls = 0, old_calls = 0;
        auto observe = [&](Fixture& f,int& calls) {
            ++calls; auto local = admission_frame(f);
            if (variant == 4) ++local.safety[0].clock.revision;
            if (variant == 5) f.in->offers[0].plan.ingress.verified = false;
            if (variant == 6) local.frame.own.actor.alive = false;
            if (variant == 7) f.work.active = true;
            if (variant == 10) local.fuel_remaining = 7999;
            return local;
        };
        const auto old = original_admit(*expected->in,expected->ledger,expected->budget,arm,expected->work,
            [&](const Decision&) { return observe(*expected,old_calls); });
        const auto result = admit_current(*actual->in,actual->ledger,actual->budget,arm,actual->work,
            [&](const Decision&) { return observe(*actual,actual_calls); });
        check(result.started == old.started && result.episode == old.episode && result.rejection == old.rejection &&
            actual_calls == old_calls,"transaction result and observer calls match original ordering");
        equal_work(actual->work,expected->work,true); equal_evidence(*actual,*expected);
        if (variant == 0 || variant == 1 || variant == 9) {
            check(result.started && bool(result.episode) == (variant == 0),"active/control/exhausted U has independent expected admission");
        } else if (variant == 2 || variant == 3) {
            check(!result.started && result.episode && !actual->ledger.active() &&
                actual->ledger.state(actual->in->component_slot)->attempts == 1,
                "lifecycle failure after issue retains the permanent attempt and closes active evidence");
        } else check(!result.started && !result.episode && !actual->ledger.active() &&
            actual->ledger.state(actual->in->component_slot)->attempts == 0,"physical/policy/callback rejection cannot publish an episode");
        check(actual->budget.used == (variant == 9 ? 1 : 0),"ordinary permission reuse never spends or refills probes");
    }
}
void dirty_inactive(Plan& selected,int tag) {
    for (Leg leg : {Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
        Route* route = leg == Leg::ingress ? &selected.ingress : leg == Leg::capture ? &selected.capture :
            leg == Leg::bank ? &selected.bank : leg == Leg::renewal ? &selected.renewal : &selected.release;
        for (int i = route->count; i < max_route_tiles; ++i) route->tiles[i] = {-1000-tag-i,5000+tag+i};
    }
}
void dirty_destination(Work& work) {
    work.plan.group = 88; work.plan.collector = 77; work.plan.altar = 66;
    work.plan.crew.fill(55); work.plan.pads.fill({44,33}); work.plan.crew_count = 6;
    int leg = 0;
    for (auto* route : {&work.plan.ingress,&work.plan.capture,&work.plan.bank,&work.plan.renewal,&work.plan.release}) {
        route->count = max_route_tiles; route->verified = false; route->blocker_assumptions = 900+leg;
        for (int i = 0; i < max_route_tiles; ++i) route->tiles[i] = {i,100+leg};
        ++leg;
    }
    work.cursor.fill(2); work.owner_turn = 17; work.phase_started = 3; work.phase_limit = 9;
    work.assembly_turns = 7; work.hold_turns = 6; work.hold_extensions = 5; work.bank_queue_turns = 4;
    work.stop_requested = work.route_witness_active = work.clearance_seen = work.productive_action_seen = true;
    work.payout_seen = work.bank_seen = work.renewal_seen = work.cycle_completed = true;
    work.helper_released = work.helper_bag_return = work.tribute_started = work.pending = work.release_admitted = true;
    work.payout_lower = 17; work.bank_lower = 23; work.tribute_starting_bag = 29;
    work.failure = PhysicalFailure::blocked_route; work.stage = PhysicalStage::recover;
    work.serial = 41; work.generation = 9; work.started_clock = {4,1,6};
    work.issued = {PhysicalAction::hold,PhysicalStage::bank,PhysicalFailure::timeout,{4,1,6},
        {3,4},88,Leg::bank,41,99};
}
void start_payload_oracle() {
    scenario = "Work.start retains exact active Plan and physical behavior from full-copy oracle";
    auto f = std::make_unique<Fixture>(); f->tick(20);
    auto selected = std::make_unique<Plan>(::plan(Organization::U));
    selected->ingress = path({{0,0},{1,0},{2,0},{3,0},{4,0}});
    selected->capture = path({{4,0},{4,1},{3,1}});
    selected->bank = path({{3,1},{4,1},{5,1},{6,1}});
    selected->renewal = path({{6,1},{6,2},{5,2},{4,2},{3,2}});
    // The general validator charges even an optional U release. Keep this
    // long active payload inside the actual 120-turn Plan bound.
    selected->release.count = 64; selected->release.verified = true;
    selected->release.tiles[0] = {3,1};
    const std::array<p6::Point,4> around{{{1,0},{0,1},{-1,0},{0,-1}}};
    for (int i = 1; i < selected->release.count; ++i) {
        const auto previous = selected->release.tiles[i-1], direction = around[(i-1)%4];
        selected->release.tiles[i] = {previous.x+direction.x,previous.y+direction.y};
    }
    selected->forecast_rounds = 110; selected->remaining_rounds = 120; selected->capture_window = 6;
    for (int i = 1; i < 6; ++i) selected->crew[i] = 100+i;
    for (int i = 0; i < 6; ++i) selected->pads[i] = {10+i,20+i};
    int leg = 0;
    for (auto* route : {&selected->ingress,&selected->capture,&selected->bank,&selected->renewal,&selected->release})
        route->blocker_assumptions = 31+leg++;
    dirty_inactive(*selected,1);
    auto actual = std::make_unique<Work>(); dirty_destination(*actual);
    auto expected = std::make_unique<Work>(*actual);
    const int walking_lower = (4+2)+(2+2)+(3+2)+(4+2)+(63+2);
    check(walking_lower == 86 && walking_lower <= selected->forecast_rounds && selected->forecast_rounds <= 120,
          "independent walking bound includes every retained positive leg and optional release");
    check(f->local.fresh() && !f->local.fuel_resumed && f->local.legal.value.safe_now &&
        f->local.fuel_remaining >= f->local.action_reserve,"physical origin observation is current, safe and funded in fuel");
    check(plan_valid(*selected,1) && crew_available(*selected,f->local,false) &&
        physical_admissible(*selected,f->local),"selected long plan meets actual physical admission conditions");
    const bool actual_started = actual->start(*selected,f->local);
    const bool expected_started = original_start(*expected,*selected,f->local);
    check(actual_started == expected_started && actual_started,
          "production and original full-copy start independently accept the valid selected plan");
    equal_work(*actual,*expected); equal_plan(actual->plan,*selected);
    const auto active_hash = plan_route_hash(actual->plan);
    dirty_inactive(*selected,2);
    check(plan_route_hash(*selected) == active_hash,"dirty source tails do not alter physical identity");
    equal_plan(actual->plan,*selected);
    selected->release.tiles[selected->release.count-1] = {4,1};
    check(plan_route_hash(*selected) != active_hash,"independent active-coordinate mutation changes only the source identity");
    equal_work(*actual,*expected);
    const auto a = actual->step(f->local), e = expected->step(f->local);
    equal_command(a,e); equal_work(*actual,*expected);
    check(a.action == PhysicalAction::move && a.target == p6::Point{1,0},"retained long plan follows same first movement");
    const ActionReceipt receipt{a.clock,a.clock,a.token,a.action,true,true};
    check(actual->confirm(a,receipt) && expected->confirm(e,receipt),"same actual command receipt confirms both cursors");
    equal_work(*actual,*expected);
    actual->invalidate(PhysicalFailure::unavailable); expected->invalidate(PhysicalFailure::unavailable);
    auto shorter = std::make_unique<Plan>(::plan(Organization::U));
    shorter->forecast_rounds = 19; shorter->remaining_rounds = 75; shorter->capture_window = 3;
    shorter->rally_legal = false; shorter->crew[5] = 222; shorter->pads[5] = {77,88};
    shorter->release.count = 0; shorter->release.verified = true; shorter->release.blocker_assumptions = 71;
    dirty_inactive(*shorter,3);
    f->tick(25);
    const bool actual_short = actual->start(*shorter,f->local);
    const bool expected_short = original_start(*expected,*shorter,f->local);
    check(actual_short == expected_short && actual_short,
          "shorter plan reenters after inactive idle gap");
    equal_work(*actual,*expected); equal_plan(actual->plan,*shorter);
    check(actual->generation == 11 && actual->serial == 42 && actual->owner_turn == 18 &&
        actual->phase_started == 18 && actual->last_round == 24 && actual->phase_limit == 42,
        "new start resets bounded job counters while retaining lifetime generation and receipt history");
    check(actual->plan.release.count == 0,"old longer optional release cannot stay active");
    const auto short_a = actual->step(f->local), short_e = expected->step(f->local);
    equal_command(short_a,short_e); equal_work(*actual,*expected);
    check(short_a.action == PhysicalAction::move && short_a.target == p6::Point{1,0},
          "stale longer tails cannot change the shorter job's physical action");
    // Publication owns active coordinates, independently of source lifetime.
    shorter->ingress.tiles[1] = {1,1}; shorter->forecast_rounds = 88;
    equal_work(*actual,*expected);
    shorter.reset(); equal_work(*actual,*expected);

    f->tick(30);
    for (int fault = 0; fault < 11; ++fault) {
        scenario = "failed Work.start publishes no Plan, clock or counter changes";
        auto before = std::make_unique<Work>(); dirty_destination(*before); before->last_round = 29;
        auto candidate = std::make_unique<Plan>(::plan(Organization::U)); dirty_inactive(*candidate,4);
        auto local = f->local;
        switch (fault) {
        case 0: before->active = true; break;
        case 1: before->generation = std::numeric_limits<std::uint64_t>::max(); break;
        case 2: before->last_round = 30; break;
        case 3: ++local.current_round; break;
        case 4: local.legal.value.safe_now = false; break;
        case 5: candidate->ingress.verified = false; break;
        case 6: candidate->ingress = path({{1,0},{2,0}}); break;
        case 7: local.fuel_remaining = local.action_reserve-1; break;
        case 8: candidate->release.count = max_route_tiles+1; break;
        case 9: candidate->ingress.count = -1; break;
        case 10: {
            candidate->release.count = max_route_tiles; candidate->release.verified = true;
            candidate->release.tiles[0] = {3,0};
            for (int i = 1; i < max_route_tiles; ++i) {
                const auto previous = candidate->release.tiles[i-1], direction = around[(i-1)%4];
                candidate->release.tiles[i] = {previous.x+direction.x,previous.y+direction.y};
            }
            candidate->forecast_rounds = candidate->remaining_rounds = 120;
            check(route_valid(candidate->release) && 13+(127+2) > candidate->forecast_rounds &&
                !plan_valid(*candidate,1),"valid 128-point release still exceeds the independently counted walking horizon");
            break;
        }
        }
        auto retained = std::make_unique<Work>(*before);
        auto old = std::make_unique<Work>(*retained);
        const bool rejected = !before->start(*candidate,local);
        const bool old_rejected = !original_start(*old,*candidate,local);
        check(rejected && old_rejected,"production and old full-copy semantics independently reject the bad start");
        equal_work(*before,*retained,true);
        equal_work(*old,*retained,true);
    }
}
struct SlackFixture {
    p6::Frame frame{};
    Plan plan{};
    p6::forecast::Request request{};
    p6::forecast::Result model{};
    SlackFixture() {
        frame.clock = {10,1,3}; frame.own.gold = 15;
        auto& own = frame.own.actor;
        own.id = 1; own.pick_ordinal = 0; own.type = p6::Champion::tavi;
        own.team = p6::Team::ally; own.alive = own.can_move = own.can_attack = true;
        own.health = own.max_health = 200; own.strength = 20; own.attack_range = 1;
        plan = ::plan(Organization::U);
        plan.renewal = path({{4,0},{3,1},{2,0}});
        request.project_group = 7; request.group_count = 1;
        request.timing = {{p6::Settings{2000,1000,5},p6::Source::queried_setting},
            {.1,p6::Source::queried_setting},{0,p6::Source::queried_setting},{1,p6::Source::queried_setting}};
        request.kit = {{20,0,false,false,false,false},frame.clock,1,p6::Knowledge::exact};
        request.ambient_incoming = {1.,frame.clock,1,p6::Knowledge::exact};
        request.bank_waypoint = 4; request.bank_position = {4,0};
        request.renewal_waypoint = 7; request.renewal_position = {2,0};
        request.bank_lock_hits = {5,p6::Source::queried_setting};
        auto& target = request.groups[0];
        target.observed = {{7,{2,0},{3,0},{2,0},p6::Lifecycle::living,true,true,true},
            frame.clock,7,p6::Knowledge::exact};
        target.membership_complete = {true,p6::Source::public_map}; target.member_count = 1;
        target.gems = {4,p6::Source::public_map}; target.gold = {2,p6::Source::public_map};
        target.members[0].public_full = {{70,20,1},p6::Source::public_map};
        target.members[0].current = {{70,20,1},frame.clock,70,p6::Knowledge::exact};
        frame.group = target.observed;
        for (const auto* route : {&plan.ingress,&plan.capture,&plan.bank,&plan.renewal}) {
            request.legs[request.leg_count++] = {route->tiles[0],route->tiles[route->count-1],
                {route->count-1,p6::Source::public_map}};
        }
        p6::forecast::build_into(frame,request,model);
        check(model.status == p6::Status::feasible && model.certificate.valid(frame,1,7),
              "real complete p6 produce-bank forecast is feasible and current");
        plan.forecast_rounds = int(model.schedule.actors[0].occupied_turns.value);
    }
    bool covered() const { return p8::native::admission_wait_covered(frame,plan,request,model); }
};
struct SpawnWaitFixture:SlackFixture {
    SpawnWaitFixture() {
        request.legs[3]={{4,0},{3,1},{1,p6::Source::public_map}};
        request.legs[4]={{3,1},{2,0},{1,p6::Source::public_map}};
        request.leg_count=5;
        request.recovery={true,false,1000000,{3,1},{1,p6::Source::conservative}};
        p6::forecast::build_into(frame,request,model);
        check(model.status==p6::Status::feasible&&model.certificate.valid(frame,1,7),
              "real complete spawn-return forecast remains feasible and current");
        plan.forecast_rounds=int(model.schedule.actors[0].occupied_turns.value);
    }
    bool covered() const {return p8::earned_center::spawn_return_wait_covered(frame,plan,request,model,{3,1});}
};
void spawn_return_wait_oracle() {
    scenario="Earned keeps its complete bank-spawn-return circuit while paying initial slack";
    auto good=std::make_unique<SpawnWaitFixture>();
    check(good->covered(),"five-leg own spawn-return model licenses one covered initial wait");
    check(!p8::native::admission_wait_covered(good->frame,good->plan,good->request,good->model),
          "Canary four-leg proof is not silently widened");
    check(good->model.phases[0].start==0&&good->model.phases[0].finish==4&&
          good->model.phases[0].incoming==4&&good->plan.forecast_rounds==31&&
          good->model.certificate.recovery.incoming_total==32,
          "initial exposure, extra return leg slack and modeled spawn wait stay charged");
    for(int fault=0;fault<27;++fault) {
        auto f=std::make_unique<SpawnWaitFixture>();
        switch(fault) {
        case 0:f->request.leg_count=4;break;
        case 1:f->request.recovery.enabled=false;break;
        case 2:f->request.recovery.requires_health_restore=true;break;
        case 3:f->request.recovery.turns.value=0;break;
        case 4:f->request.recovery.turns.source=p6::Source::unknown;break;
        case 5:f->request.recovery.position={4,0};break;
        case 6:f->request.recovery.waypoint=-1;break;
        case 7:f->request.legs[3].distance.value=-1;break;
        case 8:f->request.legs[4].distance.value=128;break;
        case 9:f->request.legs[3].to={2,0};break;
        case 10:f->request.legs[4].from={4,0};break;
        case 11:f->request.legs[4].to={3,1};break;
        case 12:f->plan.renewal.tiles[1]={3,0};break;
        case 13:f->request.bank_position={3,1};break;
        case 14:f->request.renewal_waypoint=8;break;
        case 15:++f->request.kit.clock.revision;break;
        case 16:++f->model.certificate.clock.round;break;
        case 17:f->model.phases[0].finish=3;break;
        case 18:f->request.purchase_requested=true;break;
        case 19:f->request.shield.enabled=true;break;
        case 20:f->request.kit.value.scheduled_single_cast=1;break;
        case 21:f->model.schedule.actor_count=2;break;
        case 22:f->plan.crew[0]=2;break;
        case 23:f->request.legs[2].distance.value=0;break;
        case 24:for(int i=0;i<f->model.phase_count;++i)if(f->model.phases[i].kind==p6::forecast::PhaseKind::recovery)f->model.phases[i].finish+=1;break;
        case 25:for(int i=0;i<f->model.phase_count;++i)if(f->model.phases[i].kind==p6::forecast::PhaseKind::recovery)f->model.phases[i].waypoint+=1;break;
        case 26:for(int i=0;i<f->model.phase_count;++i)if(f->model.phases[i].kind==p6::forecast::PhaseKind::recovery)f->model.phases[i].kind=p6::forecast::PhaseKind::travel;break;
        }
        check(!f->covered(),"changed spawn-return or first-leg premise cannot license admission wait");
    }
}
} // namespace

int main() {
    spawn_return_wait_oracle();
    physical_reuse_oracle();
    admission_reuse_oracle();
    start_payload_oracle();
    {
        scenario = "observer may return an owned const LocalFrame reference";
        auto f = std::make_unique<Fixture>(); auto local = std::make_unique<LocalFrame>(admission_frame(*f));
        int callbacks = 0;
        const auto result = admit_current(*f->in,f->ledger,f->budget,Arm::active,f->work,
            [&](const Decision& decision) -> const LocalFrame& {
                ++callbacks; check(decision.option == 0 && decision.record_episode,"reference callback sees actual sole U choice");
                return *local;
            });
        check(result.started && result.episode && callbacks == 1 && f->work.active && !f->work.pending,
              "reference-return callback admits the same current ordinary U as value callbacks");
        check(local->admission_only && local->frame.clock == f->in->frame.clock &&
            local->fuel_remaining == 20000 && local->action_reserve == 8000 &&
            !local->legal.value.attack_legal && !local->legal.value.routes[0].next_move_legal,
            "admission does not mutate callback-owned observation or create action permission");
    }
    {
        scenario = "one private admission and trace before explicit clock debit";
        auto f = std::make_unique<Fixture>(); f->tick(20);
        f->work.generation = 4; f->work.serial = 17; f->work.owner_turn = 9;
        int callbacks = 0;
        const auto admission = admit_current(*f->in,f->ledger,f->budget,Arm::active,f->work,
            [&](const Decision& decision) {
                ++callbacks;
                check(decision.option == 0 && decision.organization == Organization::U &&
                    decision.record_episode && !decision.deliberate_probe,"callback receives actual current ordinary choice");
                return admission_frame(*f);
            });
        check(callbacks == 1 && admission.started && admission.episode &&
            admission.rejection == Rejection::none && f->ledger.active(),"actual U starts one private episode");
        check(f->ledger.state(f->in->component_slot)->attempts == 1 && f->budget.used == 0,
              "ordinary admission consumes one evidence attempt and no probe");
        no_progress(f->work,19,9,5,17,9);
        const auto local = admission_frame(*f);
        const auto suppressed = f->work.step(local);
        check(suppressed.token == 0 && suppressed.action == PhysicalAction::yield,
              "admission-only step emits no executable command");
        no_progress(f->work,19,9,5,17,9);
        auto trace = std::make_unique<p8::earned_center::CycleTrace>();
        check(trace->begin(f->ledger,f->work),"fresh episode binds original Work before clock consumption");
        check(f->work.consume_admission_turn(local),"one verified admission turn is consumed");
        no_progress(f->work,20,10,5,17,9);
        check(f->work.started_clock == f->in->frame.clock,"original generation clock preserved");
        check(!f->work.consume_admission_turn(local),"same admission cannot debit twice");
        no_progress(f->work,20,10,5,17,9);
        check(f->work.step(local).token == 0,"read-only step remains inert after debit");
        no_progress(f->work,20,10,5,17,9);
        trace->observe(local,f->work,{});
        check(trace->active && !trace->failed && !trace->completed && trace->production_round == -1 &&
            trace->payout_round == -1 && trace->bank_round == -1 && trace->renewal_round == -1 &&
            trace->next_receipt == 0,"clock debit is neither a receipt nor a cycle endpoint");
        f->tick(21);
        const auto command = f->work.step(f->local);
        check(command.action == PhysicalAction::move && command.target == p6::Point{1,0},
              "next complete current action frame can move along the actual first tile");
        check(f->work.failure == PhysicalFailure::none && !f->work.stop_requested &&
            f->work.route_witness_active && f->work.last_round == 21 && f->work.owner_turn == 11,
            "consecutive next turn preserves route proof and charges elapsed wait");
        check(f->work.phase_started == 9 && f->work.serial == 18 && !f->work.productive_action_seen,
              "waiting counts toward timeout and first movement is not productive proof");
    }
    {
        scenario = "unconsumed active-job gap remains a real stale failure";
        auto f = std::make_unique<Fixture>(); f->tick(20);
        const auto local = admission_frame(*f);
        check(f->work.start(f->in->offers[1].plan,local),"fresh U begins before unconsumed gap");
        f->tick(21);
        const auto command = f->work.step(f->local);
        check(command.action != PhysicalAction::move && f->work.failure == PhysicalFailure::stale &&
            !f->work.route_witness_active,"omitting debit cannot hide a skipped active owner turn");
    }
    {
        scenario = "admission-purpose step cannot alter an existing pending receipt";
        auto f = std::make_unique<Fixture>(); const auto local = admission_frame(*f);
        check(f->work.start(f->in->offers[1].plan,local),"fresh job for pending-receipt guard");
        const auto issued = f->work.emit(local,PhysicalAction::move,{1,0});
        check(f->work.step(local).token == 0 && f->work.pending && f->work.serial == issued.token &&
            f->work.issued.token == issued.token && f->work.last_round == -1 && f->work.owner_turn == 0,
            "read-only purpose preserves rather than clears pending command identity");
    }
    {
        scenario = "trace cannot be rebound after the debit";
        auto f = std::make_unique<Fixture>(); auto local = admission_frame(*f);
        const auto admission = admit_current(*f->in,f->ledger,f->budget,Arm::active,f->work,
            [&](const Decision&) { return local; });
        check(admission.started && f->work.consume_admission_turn(local),"actual new Work consumes admission");
        auto trace = std::make_unique<p8::earned_center::CycleTrace>();
        check(!trace->begin(f->ledger,f->work),"late trace binding cannot weaken the old-Work protection");
    }
    {
        scenario = "closed-ledger abandonment releases trace without refunding attempts";
        auto f = std::make_unique<Fixture>(); f->tick(20); const auto local = admission_frame(*f);
        const auto admission = admit_current(*f->in,f->ledger,f->budget,Arm::active,f->work,
            [&](const Decision&) { return local; });
        auto trace = std::make_unique<p8::earned_center::CycleTrace>(); trace->next_receipt = 7;
        check(admission.started && trace->begin(f->ledger,f->work),"actual admitted trace binds before abandonment");
        f->ledger.invalidate_active(21); f->work.invalidate(PhysicalFailure::fuel);
        const auto result = trace->finish(f->ledger,21,{});
        check(!result.accepted && !trace->active && trace->next_receipt == 7 && !f->ledger.active() &&
            f->ledger.state(f->in->component_slot)->attempts == 1 && f->work.generation == 1,
            "abandonment clears an orphan trace while preserving history and spent attempt");
    }
    {
        scenario = "control ordinary admission records no actor-private episode";
        auto f = std::make_unique<Fixture>(); int callbacks = 0;
        const auto result = admit_current(*f->in,f->ledger,f->budget,Arm::control,f->work,
            [&](const Decision& decision) {
                ++callbacks; check(decision.option == 0 && !decision.record_episode && !decision.deliberate_probe,
                    "control uses the actual sole ordinary U without trial evidence");
                return admission_frame(*f);
            });
        check(result.started && !result.episode && callbacks == 1 && !f->ledger.active() &&
            f->work.consume_admission_turn(admission_frame(*f)) && f->budget.used == 0,
            "control starts and charges its physical wait without proof or probe");
    }
    {
        scenario = "exhausted active ordinary U does not replenish the evidence campaign";
        auto f = std::make_unique<Fixture>(); f->qualify();
        const auto attempts = f->ledger.state(f->in->component_slot)->attempts;
        const auto result = admit_current(*f->in,f->ledger,f->budget,Arm::active,f->work,
            [&](const Decision& decision) {
                check(!decision.record_episode && !decision.deliberate_probe,"exhausted U is solely productive");
                return admission_frame(*f);
            });
        check(result.started && !result.episode && !f->ledger.active() &&
            f->ledger.state(f->in->component_slot)->attempts == attempts && attempts == 2 && f->budget.used == 0,
            "safe U starts without refilling or reissuing its exhausted two trials");
    }
    {
        scenario = "failure after episode issue preserves the permanent charge";
        auto f = std::make_unique<Fixture>();
        f->work.generation = std::numeric_limits<std::uint64_t>::max();
        const auto result = admit_current(*f->in,f->ledger,f->budget,Arm::active,f->work,
            [&](const Decision&) { return admission_frame(*f); });
        check(!result.started && result.episode && result.rejection == Rejection::unsafe &&
            !f->ledger.active() && !f->work.active &&
            f->ledger.state(f->in->component_slot)->attempts == 1 && f->budget.used == 0,
            "failed physical start closes evidence without undoing its issued attempt");
    }
    for (int fault = 0; fault < 6; ++fault) {
        scenario = "no viable choice never invokes observer";
        auto f = std::make_unique<Fixture>();
        if (fault == 0) f->in->offer_count = 0;
        if (fault == 1) for (int i = 0; i < f->in->offer_count; ++i) f->in->offers[i].safety_count = 0;
        if (fault == 2) for (int i = 0; i < f->in->offer_count; ++i) f->in->offers[i].capital_cost = 16;
        if (fault == 3) for (int i = 0; i < f->in->offer_count; ++i) f->in->offers[i].plan.ingress.verified = false;
        if (fault == 4) ++f->in->own_state.clock.revision;
        if (fault == 5) for (int i = 0; i < f->in->offer_count; ++i)
            ++f->in->offers[i].schedule.actors[0].initial_gold.value;
        int callbacks = 0;
        const auto result = admit_current(*f->in,f->ledger,f->budget,Arm::active,f->work,
            [&](const Decision&) { ++callbacks; return admission_frame(*f); });
        check(!result.started && !result.episode && callbacks == 0 && !f->work.active &&
            !f->ledger.active() && f->work.generation == 0 && f->budget.used == 0,
            "rejected menu creates no callback, Work, evidence or spend");
    }
    {
        scenario = "exact actual admission reserve boundary is accepted without action permission";
        auto f = std::make_unique<Fixture>();
        const auto result = admit_current(*f->in,f->ledger,f->budget,Arm::active,f->work,
            [&](const Decision&) { auto local = admission_frame(*f); local.fuel_remaining = 8000; return local; });
        check(result.started && result.episode && f->work.active && f->work.serial == 0 && !f->work.pending,
              "8000 fixture fuel can admit while no command is emitted");
    }
    for (int fault = 0; fault < 8; ++fault) {
        scenario = "callback actual purpose, clock, safety and raw fuel gates";
        auto f = std::make_unique<Fixture>(); int callbacks = 0;
        const auto result = admit_current(*f->in,f->ledger,f->budget,Arm::active,f->work,
            [&](const Decision&) {
                ++callbacks; auto local = admission_frame(*f);
                switch (fault) {
                case 0: local.admission_only = false; break;
                case 1: ++local.frame.clock.revision; local.legal.clock = local.frame.clock; break;
                case 2: ++local.current_round; break;
                case 3: local.fuel_resumed = true; break;
                case 4: local.fuel_remaining = 7999; break;
                case 5: local.legal.value.safe_now = false; break;
                case 6: ++local.safety[0].clock.revision; break;
                case 7: local.frame.own.actor.position = {1,0}; break;
                }
                return local;
            });
        check(callbacks == 1 && !result.started && !result.episode && !f->work.active &&
            !f->ledger.active() && f->ledger.state(f->in->component_slot)->attempts == 0 &&
            f->budget.used == 0,"bad observation cannot start or consume a campaign");
    }
    {
        scenario = "fresh score is computed inside admission after current offer mutation";
        auto f = std::make_unique<Fixture>();
        check(choose(*f->in,f->ledger,f->budget,Arm::active).score.min_margin == 40,
              "initial ordinary U has independently expected complete income");
        f->in->offers[0].schedule = schedule(f->in->frame,7,{30,10,10});
        int callbacks = 0;
        const auto result = admit_current(*f->in,f->ledger,f->budget,Arm::active,f->work,
            [&](const Decision& decision) {
                ++callbacks; check(decision.option == 0 && decision.score.min_margin == 50 &&
                    decision.record_episode,"old computed score supplies no admission authority");
                return admission_frame(*f);
            });
        check(callbacks == 1 && result.started && result.episode && f->work.plan.organization == Organization::U,
              "current ordinary offer alone supplies the actual admission");
    }
    for (int fault = 0; fault < 9; ++fault) {
        scenario = "unsupported menu, organization or diagnostic never reaches observer";
        auto f = std::make_unique<Fixture>();
        switch (fault) {
        case 0: f->in->offers[1] = f->in->offers[0]; f->in->offer_count = 2; break;
        case 1: f->in->independent = 1; break;
        case 2: f->in->offers[0].plan.organization = Organization::G; break;
        case 3: f->in->offers[0] = f->in->offers[2]; break;
        case 4: f->in->offers[0].marked_extra_turns = 1; break;
        case 5: f->in->offers[0].diagnostic_segment = true; break;
        case 6: {
            auto& p = f->in->offers[0].plan;
            p.ingress = path({{0,0}}); p.capture = path({{0,0},{1,0}});
            p.bank = path({{1,0},{2,0}}); p.renewal = path({{2,0},{3,0}});
            break;
        }
        case 7: f->in->offers[0].plan.crew_count = 2; break;
        case 8: f->work.active = true; break;
        }
        int callbacks = 0;
        const auto result = admit_current(*f->in,f->ledger,f->budget,Arm::active,f->work,
            [&](const Decision&) { ++callbacks; return admission_frame(*f); });
        check(callbacks == 0 && !result.started && !result.episode && !f->ledger.active() &&
            f->work.generation == 0 && f->budget.used == 0,"narrow API cannot widen itself through a callback");
    }
    for (int fault = 0; fault < 21; ++fault) {
        scenario = "clock debit rejects duplicate, stale, pending or progressed jobs";
        auto f = std::make_unique<Fixture>(); auto local = admission_frame(*f);
        check(f->work.start(f->in->offers[1].plan,local),"fresh U job begins for debit boundary");
        switch (fault) {
        case 0: local.admission_only = false; break;
        case 1: ++local.current_round; break;
        case 2: local.fuel_resumed = true; break;
        case 3: ++local.frame.clock.revision; local.legal.clock = local.frame.clock; break;
        case 4: f->work.pending = true; break;
        case 5: f->work.cursor[0] = 1; break;
        case 6: f->work.cursor[4] = 1; break;
        case 7: f->work.stage = PhysicalStage::clear; break;
        case 8: f->work.productive_action_seen = true; break;
        case 9: f->work.payout_seen = true; break;
        case 10: f->work.bank_seen = true; break;
        case 11: f->work.renewal_seen = true; break;
        case 12: f->work.stop_requested = true; break;
        case 13: f->work.route_witness_active = false; break;
        case 14: f->work.last_round = 0; break;
        case 15: ++f->work.owner_turn; break;
        case 16: f->work.clearance_seen = true; break;
        case 17: f->work.cycle_completed = true; break;
        case 18: f->work.failure = PhysicalFailure::unsafe; break;
        case 19: f->work.active = false; break;
        case 20: f->work.phase_started = f->work.owner_turn = std::numeric_limits<int>::max(); break;
        }
        const int round = f->work.last_round, turn = f->work.owner_turn;
        const auto serial = f->work.serial, generation = f->work.generation;
        check(!f->work.consume_admission_turn(local),"unverified or progressed debit refused");
        check(f->work.last_round == round && f->work.owner_turn == turn &&
            f->work.serial == serial && f->work.generation == generation,"failed debit has no clock or receipt side effect");
    }
    {
        scenario = "valid zero ingress has no charged wait";
        auto f = std::make_unique<Fixture>(); auto local = admission_frame(*f);
        auto selected = std::make_unique<Plan>(f->in->offers[1].plan);
        selected->ingress = path({{0,0}}); selected->capture = path({{0,0},{1,0}});
        selected->bank = path({{1,0},{2,0}}); selected->renewal = path({{2,0},{3,0}});
        check(f->work.start(*selected,local),"ordinary zero-ingress U is physically valid");
        check(!f->work.consume_admission_turn(local),"zero ingress cannot receive free defer slack");
    }
    {
        scenario = "non-U Work cannot consume this card's admission slack";
        auto f = std::make_unique<Fixture>(); const auto local = admission_frame(*f);
        auto selected = std::make_unique<Plan>(f->in->offers[1].plan); selected->organization = Organization::G;
        check(f->work.start(*selected,local),"ordinary G remains valid in the general physical kernel");
        check(!f->work.consume_admission_turn(local) && f->work.last_round == -1 && f->work.owner_turn == 0,
              "sole-U debit does not become a generic free preparation turn");
    }
    {
        scenario = "real forecast timing and exposure cover exactly the existing ingress slack";
        auto f = std::make_unique<SlackFixture>();
        check(f->covered(),"positive ingress of a real complete U cycle licenses one wait");
        check(f->model.phases[0].start == 0 && f->model.phases[0].finish == 4 &&
            f->model.phases[0].incoming == 4,"two moves plus two slack turns retain all four ambient damage ticks");
        check(f->model.schedule.actors[0].occupied_turns.value == 28 &&
            f->model.certificate.recovery.incoming_total == 29,"full capture, hostile lock, tribute and productive return remain charged");
        f->request.legs[7].distance.value = 999;
        f->request.groups[1].member_count = 32;
        f->model.phases[15].finish = 999;
        check(f->covered(),"inactive tails cannot alter the bounded positive proof");
    }
    for (int fault = 0; fault < 36; ++fault) {
        scenario = "slack rejects altered timing, identity, shape and optional mechanisms";
        auto f = std::make_unique<SlackFixture>();
        auto& phase = f->model.phases[0]; auto& leg = f->request.legs[0];
        switch (fault) {
        case 0: f->model.status = p6::Status::unresolved; break;
        case 1: f->model.phase_count = 0; break;
        case 2: f->model.phase_count = 17; break;
        case 3: phase.kind = p6::forecast::PhaseKind::clear; break;
        case 4: phase.start = 1; break;
        case 5: phase.finish = 3; break;
        case 6: phase.finish = 5; break;
        case 7: phase.actor = 2; break;
        case 8: phase.group = 8; break;
        case 9: phase.source = p6::Source::unknown; break;
        case 10: leg.distance.source = p6::Source::queried_setting; break;
        case 11: leg.distance.value = 0; break;
        case 12: leg.from = {1,0}; break;
        case 13: leg.to = {1,0}; break;
        case 14: f->plan.ingress.count = 1; break;
        case 15: f->plan.organization = Organization::G; break;
        case 16: ++f->model.certificate.clock.revision; break;
        case 17: f->model.schedule.actor_count = 2; break;
        case 18: f->model.certificate.solo = false; break;
        case 19: f->request.shield.enabled = true; break;
        case 20: f->request.recovery.enabled = true; break;
        case 21: f->request.purchase_requested = true; break;
        case 22: f->request.kit.value.scheduled_single_cast = 1; break;
        case 23: f->request.shape = p6::forecast::Shape::bank_only; break;
        case 24: f->request.group_count = 0; break;
        case 25: f->request.leg_count = 3; break;
        case 26: ++f->request.groups[0].observed.clock.revision; break;
        case 27: f->request.project_group = 8; break;
        case 28: f->plan.crew_count = 2; break;
        case 29: f->model.certificate.group = 8; break;
        case 30: f->model.certificate.actor = 2; break;
        case 31: ++f->request.kit.clock.revision; break;
        case 32: f->model.schedule.actors[0].actor_id = 2; break;
        case 33: f->plan.ingress.verified = false; break;
        case 34: f->frame.clock.self = 2; break;
        case 35: f->plan.collector = 2; break;
        }
        check(!f->covered(),"mutated first-leg proof cannot license a wait");
    }
    std::printf("plan8 deferred admission: %d checks passed\n",checks);
}
