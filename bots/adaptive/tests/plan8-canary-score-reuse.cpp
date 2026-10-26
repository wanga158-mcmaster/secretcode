// Differential oracle for per-choose score reuse. Fixtures are bounded copies
// of plan8-policy.cpp; all assertions remain active under NDEBUG.
#include "../canary-and-cashflow/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
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

// Original choose algorithm: base policy admission is shared, but ordinary
// selection and every final ranking obtain fresh evaluations. In particular,
// this oracle never reads OptionAdmission::score or the new reuse-ranking loop.
Decision original_choose(const Inputs& in, const EvidenceLedger& ledger,
                         const ProbeBudget& budget, Arm arm) {
    Decision out; out.clock = in.frame.clock;
    if (!valid_inputs(in)) { out.reason = Rejection::malformed; return out; }
    const bool self_canary = canary_actor(in.frame.own.actor,in.picks);
    const int now = in.frame.clock.round;
    const bool component = slot_current(in,ledger,in.component_slot,in.component_entry) &&
        ledger.component_qualified(in.component_slot,in.component_entry,now);
    const bool supported = slot_current(in,ledger,in.supported_slot,in.supported_entry) &&
        ledger.organization_qualified(in.supported_slot,in.supported_entry,now);
    std::array<OptionAdmission,max_options> admissions{};
    for (int i = 0; i < in.offer_count; ++i) {
        admissions[i] = base_admission(in,ledger,arm,i,component,supported);
        out.rejected[i] = admissions[i].rejection;
    }
    const int ordinary = ordinary_choice(in,out.rejected);
    for (int i = 0; i < in.offer_count; ++i) {
        const auto& offer = in.offers[i];
        auto rejection = admissions[i].rejection;
        const bool deliberate = offer.diagnostic_segment || offer.marked_extra_turns != 0 ||
            (admissions[i].trial && i != ordinary);
        if (rejection == Rejection::none && deliberate &&
            (!admissions[i].trial || !admissions[i].record_episode)) rejection = Rejection::evidence;
        if (rejection == Rejection::none && deliberate &&
            !probe_useful(in,offer,ledger,budget,i,ordinary))
            rejection = budget.available(now/project_rounds) ? Rejection::probe_value : Rejection::probe_limit;
        out.rejected[i] = rejection;
        if (rejection != Rejection::none) continue;
        const auto rank = evaluate(in,offer.schedule);
        if (out.option < 0 || p6::compare(rank,out.score) > 0) {
            out.option = i; out.score = rank; out.reason = Rejection::none;
            out.organization = offer.plan.organization; out.deliberate_probe = deliberate;
            out.capital_allowed = self_canary && (arm == Arm::control || component);
            out.record_episode = admissions[i].record_episode;
        }
    }
    return out;
}
void same_score(const p6::ScoreEvaluation& actual,const p6::ScoreEvaluation& expected) {
#define SCORE_FIELD(name) check(actual.name == expected.name,"score field: " #name)
    SCORE_FIELD(status); SCORE_FIELD(issue); SCORE_FIELD(own_score); SCORE_FIELD(denial);
    SCORE_FIELD(margin); SCORE_FIELD(min_margin); SCORE_FIELD(nominal_margin);
    SCORE_FIELD(occupancy); SCORE_FIELD(peak_bag); SCORE_FIELD(productive_arrival);
    SCORE_FIELD(target_id); SCORE_FIELD(immediate_legal_win); SCORE_FIELD(terminal_endpoint);
    SCORE_FIELD(terminal_terms); SCORE_FIELD(earned_gold_delta); SCORE_FIELD(kills_delta);
    SCORE_FIELD(source);
#undef SCORE_FIELD
}
void same_decision(const Decision& actual,const Decision& expected) {
#define DECISION_FIELD(name) check(actual.name == expected.name,"decision field: " #name)
    DECISION_FIELD(option); DECISION_FIELD(organization); DECISION_FIELD(reason);
    DECISION_FIELD(clock); DECISION_FIELD(deliberate_probe); DECISION_FIELD(capital_allowed);
    DECISION_FIELD(record_episode); DECISION_FIELD(rejected);
#undef DECISION_FIELD
    same_score(actual.score,expected.score);
}
p6::ScoreEvaluation poisoned_score() {
    p6::ScoreEvaluation s; s.status = p6::Status::feasible; s.issue = p6::Issue::none;
    s.own_score = {11,12,13}; s.denial = {14,15,16}; s.margin = {17,18,19};
    s.min_margin = 20; s.nominal_margin = 21; s.occupancy = 22; s.peak_bag = 23;
    s.productive_arrival = 24; s.target_id = 25;
    s.immediate_legal_win = s.terminal_endpoint = s.terminal_terms = true;
    s.earned_gold_delta = 26; s.kills_delta = 27; s.source = p6::Source::synthetic_fixture;
    return s;
}
Decision compare_choose(Fixture& f,Arm arm) {
    const auto expected = original_choose(*f.in,f.ledger,f.budget,arm);
    const auto actual = choose(*f.in,f.ledger,f.budget,arm);
    same_decision(actual,expected);
    // No cross-call cache or observation mutation may affect a second choice.
    same_decision(choose(*f.in,f.ledger,f.budget,arm),expected);
    return actual;
}
void both(Fixture& f) { compare_choose(f,Arm::active); compare_choose(f,Arm::control); }
void independent_menu(Fixture& f) {
    auto& in = *f.in; in.offer_count = max_options;
    for (int i = 1; i < max_options; ++i) in.offers[i] = in.offers[0];
}
void target(Offer& offer,int group) {
    offer.plan.group = offer.schedule.target_id = group;
    for (int i = 0; i < offer.safety_count; ++i)
        if (participant(offer.plan,offer.safety[i].actor)) offer.safety[i].group = group;
}
} // namespace

int main() {
    {
        scenario = "optional legal score equals fresh evaluation on every accepted organization";
        auto f = std::make_unique<Fixture>();
        auto output = poisoned_score();
        for (int i = 0; i < f->in->offer_count; ++i) {
            check(legal_offer(*f->in,f->in->offers[i],&output) == Rejection::none,"offer accepted before score reuse");
            same_score(output,evaluate(*f->in,f->in->offers[i].schedule));
        }
    }
    for (int fault = 0; fault < 15; ++fault) {
        scenario = "every early legal-offer return resets dirty optional score";
        auto f = std::make_unique<Fixture>(); auto& offer = f->in->offers[1];
        Rejection expected = Rejection::malformed;
        switch (fault) {
        case 0: offer.safety_count = 0; break;
        case 1: offer.safety_count = p6::max_actors+1; break;
        case 2: offer.capital_cost = -1; break;
        case 3: offer.plan.group = -1; break;
        case 4: ++offer.schedule.target_id; break;
        case 5: offer.plan.ingress.verified = false; break;
        case 6: offer.plan.remaining_rounds = 2001; break;
        case 7: offer.schedule.actor_count = 2; break;
        case 8: ++offer.schedule.actors[0].initial_gold.value; expected = Rejection::resources; break;
        case 9: ++offer.safety[0].clock.revision; expected = Rejection::unsafe; break;
        case 10: offer.safety[0].actor = 99; expected = Rejection::unsafe; break;
        case 11: offer.capital_cost = 16; expected = Rejection::resources; break;
        case 12: offer.schedule.actors[1].initial_bag.source = p6::Source::conservative;
            expected = Rejection::resources; break;
        case 13: offer.plan.crew_count = 0; break;
        case 14: offer.safety[0].bank_executable = false; expected = Rejection::unsafe; break;
        }
        auto output = poisoned_score();
        check(legal_offer(*f->in,offer,&output) == expected,"early rejection category is independently expected");
        same_score(output,p6::ScoreEvaluation{});
    }
    for (int fault = 0; fault < 7; ++fault) {
        scenario = "evaluated rejection returns actual fresh invalid or unresolved score";
        auto f = std::make_unique<Fixture>(); auto& offer = f->in->offers[1];
        auto& actor = offer.schedule.actors[0];
        switch (fault) {
        case 0: actor.events[1].event_id = actor.events[0].event_id; break;
        case 1: actor.events[0].source = p6::Source::unknown; break;
        case 2: --actor.event_count; break;
        case 3: actor.events[0].tick = 2001; break;
        case 4: actor.events[1] = actor.events[2]; actor.event_count = 2; break;
        case 5: event(actor,p6::EventKind::purchase,4000,1);
            actor.events[actor.event_count-1].gold_cost = fact<p6::Gems>(20); break;
        case 6: actor.occupied_turns.source = p6::Source::unknown; break;
        }
        const auto fresh = evaluate(*f->in,offer.schedule);
        check(!fresh.eligible(),"fresh oracle independently rejects edited schedule");
        auto output = poisoned_score();
        check(legal_offer(*f->in,offer,&output) == Rejection::malformed,"evaluation rejection reached");
        same_score(output,fresh);
    }
    {
        scenario = "ordinary active U and immediate control J";
        auto f = std::make_unique<Fixture>();
        check(valid_inputs(*f->in),"fixture inputs valid");
        for (int i = 0; i < 4; ++i)
            check(legal_offer(*f->in,f->in->offers[i]) == Rejection::none,"fixture offers legal");
        const auto active = compare_choose(*f,Arm::active);
        const auto control = compare_choose(*f,Arm::control);
        check(active.option == 1 && active.record_episode && !active.deliberate_probe,"active ordinary U");
        check(control.option == 3 && control.organization == Organization::J &&
            !control.record_episode,"control immediate J");
    }
    {
        scenario = "joint cached score preserves gain and no-gain admission";
        auto f = std::make_unique<Fixture>(); f->qualify();
        check(compare_choose(*f,Arm::active).option == 2,"positive H score admits real commission");
        check(compare_choose(*f,Arm::control).option == 3,"positive J score admits control organization");
        f->in->offers[2].schedule = schedule(f->in->frame,7,{5,0,5});
        f->in->offers[3].schedule = schedule(f->in->frame,7,{4,3,3},true);
        const auto active = compare_choose(*f,Arm::active);
        const auto control = compare_choose(*f,Arm::control);
        check(active.rejected[2] == Rejection::no_gain && active.option == 1,"H score below actual baseline rejected");
        check(control.rejected[3] == Rejection::no_gain && control.option == 1,"J score below actual baseline rejected");
    }
    {
        scenario = "eight independent ties and new per-call winners";
        auto f = std::make_unique<Fixture>(); independent_menu(*f);
        check(compare_choose(*f,Arm::active).option == 0,"complete tie retains first option");
        both(*f);
        f->in->offers[6].schedule = schedule(f->in->frame,9,{11,11,11});
        check(compare_choose(*f,Arm::active).option == 6,"later greater score wins");
        both(*f);
        f->in->offers[6].schedule = schedule(f->in->frame,9,{10,10,10});
        check(compare_choose(*f,Arm::active).option == 0,"reverted schedule has no cached advantage");
        f->in->offers[7].schedule = schedule(f->in->frame,9,{10,10,10},false,18);
        check(compare_choose(*f,Arm::active).option == 7,"lower complete occupancy wins tied income");
        both(*f);
        f->in->offers[7] = f->in->offers[0]; target(f->in->offers[5],8);
        check(compare_choose(*f,Arm::active).option == 5,"target ID resolves otherwise identical ranks");
        both(*f);
    }
    {
        scenario = "peak bag tie and scenario-window rank";
        auto f = std::make_unique<Fixture>(); independent_menu(*f);
        for (int i = 0; i < max_options; ++i)
            f->in->offers[i].schedule = schedule(f->in->frame,9,{20,5,5});
        f->in->offers[7].schedule = schedule(f->in->frame,9,{10,10,10});
        check(compare_choose(*f,Arm::active).option == 7,"lower peak bag resolves equal income and occupancy");
        both(*f);
        f->in->windows.deadlines = f->in->windows.windows = {10,15,120};
        f->in->offers[7].schedule = schedule(f->in->frame,9,{20,20,20},false,25);
        check(compare_choose(*f,Arm::active).option == 0,"nominal-window score beats later larger payout");
        both(*f);
    }
    for (int marked = 0; marked < 3; ++marked) {
        scenario = "unlicensed diagnostic or extra work excluded from ordinary menu";
        auto f = std::make_unique<Fixture>(); independent_menu(*f);
        f->in->offers[4].schedule = schedule(f->in->frame,9,{100,100,100});
        f->in->offers[4].diagnostic_segment = marked == 0;
        f->in->offers[4].marked_extra_turns = marked == 1 ? 1 : marked == 2 ? -1 : 0;
        const auto d = compare_choose(*f,Arm::active); both(*f);
        check(d.option == 0 && d.rejected[4] != Rejection::none,"unlicensed high score cannot become ordinary");
    }
    {
        scenario = "qualified H commission and present resources";
        auto f = std::make_unique<Fixture>(); f->qualify();
        check(compare_choose(*f,Arm::active).option == 2,"component qualification admits ordinary H");
        both(*f);
        f->in->offers[2].capital_cost = 16;
        check(compare_choose(*f,Arm::active).rejected[2] == Rejection::resources,"present cash gate remains");
        both(*f);
        f->in->offers[2].capital_cost = 0; ++f->in->supported_entry.participants[1];
        check(compare_choose(*f,Arm::active).rejected[2] == Rejection::evidence,"changed membership blocks H");
        both(*f);
    }
    for (int marked = 0; marked < 3; ++marked) {
        scenario = "qualified H renewal versus marked work";
        auto f = std::make_unique<Fixture>(); f->qualify_supported();
        f->in->offers[2].diagnostic_segment = marked == 1;
        f->in->offers[2].marked_extra_turns = marked == 2 ? 1 : 0;
        const auto d = compare_choose(*f,Arm::active); both(*f);
        check(marked ? d.rejected[2] == Rejection::evidence :
            d.option == 2 && d.record_episode && !d.deliberate_probe,"qualified H diagnostic boundary");
    }
    {
        scenario = "exhausted U remains productive without proof";
        auto f = std::make_unique<Fixture>(); f->qualify();
        f->in->offers[1].schedule = schedule(f->in->frame,7,{100,10,10});
        const auto d = compare_choose(*f,Arm::active); both(*f);
        check(d.option == 1 && !d.record_episode && !d.deliberate_probe,"exhausted ordinary U");
        f->in->offers[1].marked_extra_turns = 2;
        check(compare_choose(*f,Arm::active).rejected[1] == Rejection::evidence,"exhaustion cannot hide extra work");
        both(*f);
    }
    {
        scenario = "expired qualification cannot reuse admitted H score";
        auto f = std::make_unique<Fixture>(); f->qualify();
        check(compare_choose(*f,Arm::active).option == 2,"H starts qualified by component");
        f->tick(400);
        const auto d = compare_choose(*f,Arm::active); both(*f);
        check(d.option != 2 && d.rejected[2] == Rejection::evidence,"expired proof is not a score-cache permission");
    }
    for (int variation = 0; variation < 7; ++variation) {
        scenario = "second diagnostic branches, expiry and budget";
        auto f = std::make_unique<Fixture>(true); f->prior_success(); f->probe(1);
        if (variation == 1) f->in->probe.safety_count[1] = 0;
        if (variation == 2) f->budget.spend(0);
        if (variation == 3) {
            f->in->offers[4] = f->in->offers[0]; f->in->offer_count = 5;
            f->in->offers[4].schedule = schedule(f->in->frame,9,{17,17,16});
        }
        if (variation == 4 || variation == 5) { f->tick(variation == 4 ? 252 : 253); f->probe(1); }
        if (variation == 6) f->in->probe.branches[2] = schedule(f->in->frame,9,{1,1,1},false,25);
        const auto d = compare_choose(*f,Arm::active); both(*f);
        if (variation == 0 || variation == 4)
            check(d.option == 1 && d.deliberate_probe && d.record_episode,"eligible second diagnostic selected");
        else check(d.rejected[1] != Rejection::none,"diagnostic has concrete rejected branch or budget");
    }
    {
        scenario = "zero prior diagnostic cannot promise qualified continuation";
        auto f = std::make_unique<Fixture>(); f->probe(1);
        check(compare_choose(*f,Arm::active).rejected[1] == Rejection::probe_value,"no invented initial proof");
        both(*f);
    }
    for (int fault = 0; fault < 12; ++fault) {
        scenario = "malformed, unsafe, resource or stale individual candidate";
        auto f = std::make_unique<Fixture>(); independent_menu(*f);
        auto& offer = f->in->offers[4];
        offer.schedule = schedule(f->in->frame,9,{100,100,100});
        switch (fault) {
        case 0: --offer.safety[0].clock.round; break;
        case 1: ++offer.safety[0].clock.revision; break;
        case 2: offer.safety[0].clear.incoming_total = 100; break;
        case 3: offer.safety_count = 0; break;
        case 4: offer.plan.ingress.verified = false; break;
        case 5: ++offer.schedule.actors[0].initial_gold.value; break;
        case 6: ++offer.schedule.actors[1].initial_bag.value; break;
        case 7: offer.schedule.actors[0].events[1].event_id = offer.schedule.actors[0].events[0].event_id; break;
        case 8: --offer.schedule.actors[0].event_count; break;
        case 9: offer.schedule.actors[0].initial_gold.source = p6::Source::visible_subject; break;
        case 10: offer.capital_cost = 16; break;
        case 11: offer.schedule.actors[0].events[0].tick = 2001; break;
        }
        const auto d = compare_choose(*f,Arm::active); both(*f);
        check(d.option != 4 && d.rejected[4] != Rejection::none,"bad candidate's previously eligible score cannot win");
    }
    for (int fault = 0; fault < 5; ++fault) {
        scenario = "invalid whole input leaves exact default decision";
        auto f = std::make_unique<Fixture>();
        switch (fault) {
        case 0: f->in->offer_count = 0; break;
        case 1: f->in->offer_count = max_options+1; break;
        case 2: f->in->independent = -1; break;
        case 3: ++f->in->own_state.clock.revision; break;
        case 4: f->in->frame.own.actor.health = 101; break;
        }
        const auto d = compare_choose(*f,Arm::active); both(*f);
        check(d.option == -1 && d.reason == Rejection::malformed,"whole-input rejection is not an offer score");
    }
    {
        scenario = "all candidates unsafe preserve default no-winner result";
        auto f = std::make_unique<Fixture>();
        for (int i = 0; i < f->in->offer_count; ++i) f->in->offers[i].safety_count = 0;
        const auto d = compare_choose(*f,Arm::active); both(*f);
        check(d.option == -1 && d.reason == Rejection::opportunity_unavailable,"all rejection has no winner");
    }
    {
        scenario = "visible helper adoption uses its own empty ledger";
        auto f = std::make_unique<Fixture>(); auto& in = *f->in;
        const auto original = in.frame.own.actor;
        in.frame.own.actor = in.frame.actors[0].value; in.frame.clock.self = 2;
        in.frame.actors[0].value = original; in.frame.actors[0].value.position = {2,0};
        in.frame.actors[0].subject = 1; in.frame.own.actor.position = {0,0};
        in.offers[0].plan = plan(Organization::G,9,2);
        in.offers[2].plan = plan(Organization::H,7,2);
        for (int i = 0; i < 4; ++i) in.offers[i].schedule = schedule(in.frame,i ? 7 : 9,
            i == 2 ? std::array<int,3>{40,0,10} : std::array<int,3>{10,10,10});
        f->ledger = EvidenceLedger(2); f->tick(0);
        in.visible_proposer = {7,in.frame.clock,1,p6::Knowledge::exact};
        check(compare_choose(*f,Arm::active).option == 2,"visible current proposer admits helper H");
        both(*f);
        in.frame.actors[0].knowledge = p6::Knowledge::unknown;
        check(compare_choose(*f,Arm::active).option != 2,"hidden proposer supplies no admission");
        both(*f);
    }
    std::printf("plan8 canary score reuse: %d checks passed\n",checks);
}
