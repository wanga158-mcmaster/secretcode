#include "../canary-and-cashflow/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
using namespace p8;
using namespace p8::canary;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
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
    Admission commission() {
        qualify(); in->offers[3].schedule = schedule(in->frame,7,{14,13,13},true);
        const auto d = decide(*in,ledger,budget,Arm::active);
        check(d.option == 2 && !d.deliberate_probe,"qualified component permits ordinarily best H commission");
        return commit(*in,local,d,ledger,budget,Arm::active,work);
    }
};
}
int main() {
    {
        auto f = std::make_unique<Fixture>(); auto& in = *f->in;
        check(valid_inputs(in),"fixture has fresh personal observations and queried settings");
        for (int i = 0; i < 4; ++i) check(legal_offer(in,in.offers[i]) == Rejection::none,"same-three-actor physical offer is legal");
        check(evaluate(in,in.offers[0].schedule).min_margin == 30 && evaluate(in,in.offers[1].schedule).min_margin == 40 &&
              evaluate(in,in.offers[2].schedule).min_margin == 50 && evaluate(in,in.offers[3].schedule).min_margin == 80,
              "hand oracle includes independent producer income and actual J payout splitting");
        const auto a = decide(in,f->ledger,f->budget,Arm::active), c = decide(in,f->ledger,f->budget,Arm::control);
        check(a.option == 1 && a.organization == Organization::U && !a.deliberate_probe && a.record_episode &&
              c.option == 3 && c.organization == Organization::J && !c.record_episode,
              "active ranks admissible U ordinarily while control ranks immediate J with the same accurate offers");
        check(a.rejected[2] == Rejection::evidence && a.rejected[3] == Rejection::evidence,"H and J cannot bypass component gate");
        in.offers[1].schedule = schedule(in.frame,7,{100,10,10});
        const auto ordinary = decide(in,f->ledger,f->budget,Arm::active);
        check(ordinary.option == 1 && !ordinary.deliberate_probe,"frontier solo is ordinary only when unchanged comparator selects it");
        const auto admitted = commit(in,f->local,ordinary,f->ledger,f->budget,Arm::active,f->work);
        check(admitted.started && admitted.episode && f->ledger.active() && f->work.active && f->work.serial == 0,
              "commit records issued episode before any physical command exists");
        check(advance(in,f->local,f->ledger,Arm::active,f->work).action == PhysicalAction::move,"admitted work follows its retained first tile");
    }
    {
        auto f = std::make_unique<Fixture>(); f->probe(1);
        const auto d = decide(*f->in,f->ledger,f->budget,Arm::active);
        check(d.option == 0 && d.rejected[1] == Rejection::probe_value && f->budget.used == 0,
              "zero prior successes cannot value a diagnostic by promising newly qualified H after one episode");
    }
    {
        auto f = std::make_unique<Fixture>(true); f->prior_success(); f->probe(1);
        const auto d = decide(*f->in,f->ledger,f->budget,Arm::active);
        check(d.option == 1 && d.deliberate_probe && d.record_episode,
              "marked extra work is deliberate only with a valuable gate-feasible second diagnostic");
        auto admitted = commit(*f->in,f->local,d,f->ledger,f->budget,Arm::active,f->work);
        check(admitted.started && f->budget.used == 1 && !f->budget.available(0),"diagnostic admission spends persistent epoch slot");
        DiagnosticSegment marker; marker.start();
        for (int t = 0; t < 9; ++t) {
            f->tick(26+t); marker.observe(f->local,true,f->budget,f->work);
            marker.observe(f->local,true,f->budget,f->work);
        }
        check(marker.spent_turns == 9 && f->budget.overrun && f->work.stop_requested,"ninth actual marked turn stops trial; duplicate snapshots spend no extra turns");
        f->in->frame.own.actor.channel = p6::Channel::tribute; f->in->frame.own.actor.channel_valid = true;
        f->tick(35);
        check(advance(*f->in,f->local,f->ledger,Arm::active,f->work).action == PhysicalAction::preserve_channel,"diagnostic overrun preserves safe valid channel");
        f->in->frame.own.actor.channel = p6::Channel::none; f->tick(36);
        check(advance(*f->in,f->local,f->ledger,Arm::active,f->work).action != PhysicalAction::move && !f->work.active,
              "overrun stops at safe boundary without new experimental movement");
        f->ledger.invalidate_active(37);
        check(f->budget.used == 1 && !f->budget.available(1),"failure and epoch change never clear diagnostic overrun");
    }
    {
        auto f = std::make_unique<Fixture>(true); f->prior_success(); f->probe(1); f->in->probe.safety_count[1] = 0;
        check(decide(*f->in,f->ledger,f->budget,Arm::active).option == 0,"unverified continuation survival rejects probe");
        f->probe(1); f->in->probe.branches[2] = schedule(f->in->frame,9,{1,1,1},false,25);
        check(decide(*f->in,f->ledger,f->budget,Arm::active).option == 0,"conservative ambiguous branch includes displaced producer income");
    }
    {
        auto f = std::make_unique<Fixture>(true); f->prior_success(); f->probe(1); auto& in = *f->in;
        in.offers[4] = in.offers[0]; in.offer_count = 5;
        in.offers[4].schedule = schedule(in.frame,9,{17,17,16});
        const auto d = decide(in,f->ledger,f->budget,Arm::active);
        check(d.option == 4 && d.rejected[1] == Rejection::probe_value,
              "every diagnostic branch must match the best eligible ordinary alternative, not only the named fallback");
    }
    {
        auto f = std::make_unique<Fixture>(true); f->prior_success(); f->probe(1);
        f->tick(252); f->probe(1);
        check(decide(*f->in,f->ledger,f->budget,Arm::active).option == 1,
              "second diagnostic can qualify when worst consistent completion is 255 rounds after first success");
        f->tick(253); f->probe(1);
        const auto expired = decide(*f->in,f->ledger,f->budget,Arm::active);
        check(expired.option == 0 && expired.rejected[1] == Rejection::probe_value,
              "second diagnostic cannot promise H when worst completion reaches the 256-round expiry boundary");
    }
    {
        auto f = std::make_unique<Fixture>(true); f->prior_success(); f->probe(1);
        ++f->in->own_state.value.focus;
        const auto changed = decide(*f->in,f->ledger,f->budget,Arm::active);
        check(changed.option == 0 && changed.rejected[1] == Rejection::evidence,
              "changed entry build blocks diagnostic and its promised supported continuation");
        --f->in->own_state.value.focus; f->tick(27);
        check(decide(*f->in,f->ledger,f->budget,Arm::active).rejected[1] == Rejection::evidence,
              "restoring a former build does not resurrect a materially retired diagnostic campaign");
    }
    {
        auto f = std::make_unique<Fixture>(); auto& in = *f->in;
        in.offers[1].schedule = schedule(in.frame,7,{100,10,10});
        const auto old = decide(in,f->ledger,f->budget,Arm::active);
        ++in.frame.clock.revision; f->tick(0);
        check(commit(in,f->local,old,f->ledger,f->budget,Arm::active,f->work).rejection == Rejection::stale &&
              !f->ledger.active(),"revised observation clock rejects commit before issuing evidence");
        in.offers[1].capital_cost = 5;
        auto& buyer = in.offers[1].schedule.actors[0];
        event(buyer,p6::EventKind::purchase,4000,1); buyer.events[buyer.event_count-1].gold_cost = fact<p6::Gems>(5);
        check(decide(in,f->ledger,f->budget,Arm::active).rejected[1] == Rejection::evidence,"frontier capital cannot precede component qualification");
        for (int i : {2,3}) { in.offers[i].plan.group = 9; in.offers[i].schedule.target_id = 9; }
        f->tick(0);
        const auto blocked = decide(in,f->ledger,f->budget,Arm::active);
        check(blocked.rejected[2] == Rejection::evidence && blocked.rejected[3] == Rejection::evidence,"nonfrontier H or J cannot route around canary gate");
    }
    {
        auto f = std::make_unique<Fixture>(); const auto admitted = f->commission();
        check(admitted.started && admitted.episode && !f->ledger.organization_qualified(f->in->supported_slot,f->supported.key,43),
              "two solo full episodes license one H campaign while H itself remains unqualified");
        f->in->own_state.clock.revision++;
        const auto stale = advance(*f->in,f->local,f->ledger,Arm::active,f->work);
        check(stale.action != PhysicalAction::move && !f->ledger.active(),"stale advance inputs invalidate episode and cannot execute retained experimental movement");
    }
    for (int change = 0; change < 3; ++change) {
        auto f = std::make_unique<Fixture>(); check(f->commission().started,"H trial starts for safe-boundary change fixture");
        f->in->frame.own.actor.channel = p6::Channel::tribute; f->in->frame.own.actor.channel_valid = true;
        if (change == 0) ++f->in->own_state.value.focus;
        if (change == 1) f->in->supported_entry.participants[1] = 3;
        f->tick(change == 2 ? 298 : 44);
        const auto command = advance(*f->in,f->local,f->ledger,Arm::active,f->work);
        check(command.action == PhysicalAction::preserve_channel && f->work.stop_requested,
              "changed Focus, membership or expired component preserves valid tribute then requests stop");
        f->in->frame.own.actor.channel = p6::Channel::none; f->tick(change == 2 ? 299 : 45);
        check(advance(*f->in,f->local,f->ledger,Arm::active,f->work).action != PhysicalAction::move && !f->work.active,
              "safe boundary ends dependent work after lost applicability");
    }
    {
        auto f = std::make_unique<Fixture>(); f->qualify(); auto& in = *f->in;
        in.offers[1].schedule = schedule(in.frame,7,{100,10,10});
        const auto attempts = f->ledger.state(in.component_slot)->attempts;
        const auto ordinary = decide(in,f->ledger,f->budget,Arm::active);
        check(ordinary.option == 1 && !ordinary.deliberate_probe && !ordinary.record_episode,
              "exhausted U component campaign remains ordinary productive work without a new evidence episode");
        const auto admitted = commit(in,f->local,ordinary,f->ledger,f->budget,Arm::active,f->work);
        check(admitted.started && !admitted.episode && !f->ledger.active() &&
              f->ledger.state(in.component_slot)->attempts == attempts && f->budget.used == 0,
              "productive exhausted-campaign fallback neither fabricates proof nor spends a diagnostic slot");
        in.offers[1].marked_extra_turns = 2;
        check(decide(in,f->ledger,f->budget,Arm::active).rejected[1] == Rejection::evidence,
              "qualified exhausted U cannot hide marked diagnostic work behind its productive fallback");
    }
    {
        auto f = std::make_unique<Fixture>(); f->qualify_supported(); auto& in = *f->in;
        const auto ordinary = decide(in,f->ledger,f->budget,Arm::active);
        check(ordinary.option == 2 && !ordinary.deliberate_probe && ordinary.record_episode,
              "qualified ordinary H may record renewal evidence without becoming a new diagnostic trial");
        in.offers[2].marked_extra_turns = 1;
        check(decide(in,f->ledger,f->budget,Arm::active).rejected[2] == Rejection::evidence,
              "qualified H cannot execute marked extra work without an admitted diagnostic trial");
        in.offers[2].marked_extra_turns = 0; in.offers[2].diagnostic_segment = true;
        check(decide(in,f->ledger,f->budget,Arm::active).rejected[2] == Rejection::evidence && f->budget.used == 0,
              "qualified H cannot execute an explicit diagnostic segment without a chargeable admitted trial");
    }
    {
        auto f = std::make_unique<Fixture>(); f->qualify(); auto& in = *f->in;
        in.offers[2].capital_cost = 16;
        check(decide(in,f->ledger,f->budget,Arm::active).rejected[2] == Rejection::resources,"qualified frontier package still cannot exceed present personal gold");
        in.offers[2].capital_cost = 15;
        auto& actual = in.offers[2].schedule;
        actual.productions[0].gold = fact<p6::Gems>(100);
        event(actual.actors[0],p6::EventKind::purchase,4000,11);
        actual.actors[0].events[actual.actors[0].event_count-1].gold_cost = fact<p6::Gems>(20);
        check(evaluate(in,actual).eligible() && decide(in,f->ledger,f->budget,Arm::active).rejected[2] == Rejection::resources,
              "forecast income cannot fund actual package cost exceeding present gold even with smaller declared cost");
    }
    {
        auto f = std::make_unique<Fixture>();
        f->in->frame.actors[0].value.id = 99; f->in->frame.actors[0].subject = 99;
        f->in->frame.actors[0].value.team = p6::Team::enemy; f->tick(0);
        check(decide(*f->in,f->ledger,f->budget,Arm::active).rejected[2] != Rejection::none,
              "enemy with matching champion kit and ordinal cannot replace actual drafted helper ID");
    }
    {
        auto f = std::make_unique<Fixture>(); auto& in = *f->in;
        const auto original = in.frame.own.actor;
        in.frame.own.actor = in.frame.actors[0].value; in.frame.clock.self = 2;
        in.frame.actors[0].value = original; in.frame.actors[0].value.position = {2,0}; in.frame.actors[0].subject = 1;
        in.frame.own.actor.position = {0,0};
        in.offers[0].plan = plan(Organization::G,9,2); in.offers[2].plan = plan(Organization::H,7,2);
        for (int i = 0; i < 4; ++i) in.offers[i].schedule = schedule(in.frame,i ? 7 : 9,
            i == 2 ? std::array<int,3>{40,0,10} : std::array<int,3>{10,10,10});
        f->tick(0); in.visible_proposer = {7,in.frame.clock,1,p6::Knowledge::exact};
        EvidenceLedger empty_helper(2);
        auto d = decide(in,empty_helper,f->budget,Arm::active);
        check(empty_helper.size() == 0 && d.option == 2,"helper with empty private ledger adopts personally visible named proposer");
        check(commit(in,f->local,d,empty_helper,f->budget,Arm::active,f->work).started && !empty_helper.active(),
              "helper adoption consumes current capability without importing collector proof");
        in.frame.actors[0].knowledge = p6::Knowledge::unknown; f->tick(1);
        check(decide(in,empty_helper,f->budget,Arm::active).option != 2,"hidden proposer cannot retain visible admission");
    }
    {
        auto f = std::make_unique<Fixture>(); auto& in = *f->in;
        in.frame.own.actor.type = p6::Champion::vendetta;
        in.draft.champions[0] = p6::Champion::vendetta;
        check(!guarded_key_current(in,f->solo.key),"new canary champion cannot inherit original champion's evidence");
        f->solo.key.champion = f->supported.key.champion = int(p6::Champion::vendetta);
        f->ledger = EvidenceLedger(1);
        in.component_slot = f->ledger.predeclare(f->solo);
        in.supported_slot = f->ledger.predeclare(f->supported);
        in.component_entry = in.offers[1].entry = f->solo.key;
        in.supported_entry = in.offers[2].entry = f->supported.key;
        in.offers[1].context_slot = in.component_slot; in.offers[2].context_slot = in.supported_slot;
        in.offers[1].schedule = schedule(in.frame,7,{100,10,10}); f->tick(0);
        const auto choice = decide(in,f->ledger,f->budget,Arm::active);
        check(choice.option == 1 && commit(in,f->local,choice,f->ledger,f->budget,Arm::active,f->work).started,
              "diversified ordinal-zero champion can execute its own unqualified canary trial");
    }
    {
        auto f = std::make_unique<Fixture>(); f->qualify(); auto& in = *f->in;
        in.frame.own.actor.type = in.draft.champions[0] = p6::Champion::ren; f->tick(43);
        auto choice = decide(in,f->ledger,f->budget,Arm::active);
        check(choice.option != 2 && !choice.capital_allowed,
              "qualified former champion's proof cannot license new champion staffing or capital");
        in.frame.own.actor.type = in.draft.champions[0] = p6::Champion::tavi; f->tick(44);
        choice = decide(in,f->ledger,f->budget,Arm::active);
        check(choice.option != 2,"returning old champion key never resurrects materially expired proof");
    }
    {
        auto f = std::make_unique<Fixture>(); auto& in = *f->in;
        EpisodeRequest request; request.context = in.component_slot; request.issue_round = 0;
        request.deadline = 100; request.action = 0; request.nominal_duration = 20;
        request.premises = {f->solo.key,in.own_state.value,all_premises};
        const auto serial = f->ledger.issue(request);
        Observation o; o.round = 20; o.kind = ObservationKind::completed; o.premises = request.premises;
        o.duration = {{0,100},DurationSource::legally_bracketed};
        check(serial && !f->ledger.resolve(serial,o).full_positive,"ambiguous completed interval supplies no qualifying proof");
        f->tick(21); const auto choice = decide(in,f->ledger,f->budget,Arm::active);
        check(choice.option == 1 && !choice.record_episode && choice.rejected[2] == Rejection::evidence,
              "failed canary campaign retains productive U without new evidence and cannot recruit home producers");
    }
    ProbeBudget persistent;
    check(persistent.spend(0) && !persistent.spend(0) && persistent.spend(1) && !persistent.spend(2) && persistent.used == 2,
          "epoch changes allow at most two lifetime diagnostics with no repeated-epoch reset");
    std::printf("plan8-policy: %d checks passed\n",checks);
}
