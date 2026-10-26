#include "../earned-center/src/policy.h"
#include "../earned-center/src/purchases.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <limits>
#include <tuple>

using namespace p8;
using namespace p8::earned_center;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
template<class T> p6::Fact<T> fact(T value) { return {value,p6::Source::synthetic_fixture}; }
// Frozen pre-seam admission, independent of the shared new commit tail.
Admission original_commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,
                          EvidenceLedger& ledger,ProbeBudget& probes,Arm arm,Work& work) {
    Admission out;
    if (!local.fresh() || !(local.frame.clock==in.frame.clock) || !(proposed.clock==in.frame.clock) || work.active) {
        out.rejection=Rejection::stale; return out;
    }
    const auto now=decide(in,ledger,probes,arm);
    if (now.option<0 || now.option!=proposed.option || now.deliberate_probe!=proposed.deliberate_probe) return out;
    const auto& o=in.offers[now.option];
    if (!physical_admissible(o.plan,local)) { out.rejection=Rejection::unsafe; return out; }
    if (arm==Arm::active && now.record_episode && collector_actor(in.frame.own.actor,in.draft) &&
        o.plan.group==in.target_group) {
        Premises p{o.entry,in.own_state.value,all_premises};
        const auto* slot=ledger.state(o.context_slot);
        if (!slot) return out;
        if (o.plan.organization==Organization::H && !slot->campaign_started &&
            !ledger.begin_organization_campaign(in.component_slot,o.context_slot,in.frame.clock.round,{p,true,true,true})) return out;
        EpisodeRequest r; r.context=o.context_slot; r.issue_round=in.frame.clock.round;
        r.deadline=r.issue_round+std::min(forecast_window,o.plan.remaining_rounds);
        r.nominal_duration=o.plan.forecast_rounds; r.action=int(o.plan.organization); r.premises=p;
        out.episode=ledger.issue(r); if (!out.episode) return out;
    }
    if (now.deliberate_probe && !probes.spend(in.frame.clock.round/project_rounds)) {
        if (out.episode) ledger.invalidate_active(in.frame.clock.round);
        out.rejection=Rejection::probe_limit; return out;
    }
    out.started=work.start(o.plan,local); out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if (!out.started && out.episode) ledger.invalidate_active(in.frame.clock.round);
    return out;
}
Route path(std::initializer_list<p6::Point> tiles) {
    Route route; route.verified = true; route.count = int(tiles.size());
    int i = 0; for (auto tile : tiles) route.tiles[i++] = tile;
    return route;
}
Plan circuit(Organization organization, int self = 1) {
    Plan p; p.organization = organization; p.group = 7; p.collector = self; p.altar = 4;
    p.crew[0] = self; p.crew_count = 1;
    p.ingress = path({{0,0},{1,0},{2,0}}); p.capture = path({{2,0},{3,0}});
    p.bank = path({{3,0},{4,0}});
    p.renewal = path({{4,0},{4,1},{3,1},{2,1},{1,1},{0,0}});
    p.forecast_rounds = 18; p.remaining_rounds = 100; p.capture_window = 5;
    p.gem_bearing = p.complete_verified = p.rally_legal = true;
    p.pads[0] = {2,0}; p.pads[1] = {2,1};
    if (organization == Organization::H) {
        p.forecast_rounds = 17; // Current helper work removes one clearing turn.
        p.collector = 1; p.crew[0] = 1; p.crew[1] = 2; p.crew_count = 2;
        if (self == 2) {
            p.ingress = path({{0,0},{1,0},{2,1}});
            p.capture = path({{2,1},{3,0}});
            p.release = path({{3,0},{3,1}});
        }
    }
    return p;
}
void event(p6::ActorSchedule& a, p6::EventKind kind, int id, int tick, int production = -1) {
    p6::Event e; e.kind = kind; e.event_id = id; e.tick = tick;
    e.production_index = production; e.gold_cost = fact<p6::Gems>(0);
    e.waypoint_id = kind == p6::EventKind::renewal ? 90+a.actor_id : -1;
    e.source = p6::Source::synthetic_fixture; a.events[a.event_count++] = e;
}
p6::Schedule schedule(const p6::Frame& f, std::array<int,3> gains, int duration = 18) {
    p6::Schedule s; s.actor_count = 3; s.target_id = 7;
    for (int i = 0; i < 3; ++i) {
        auto& a = s.actors[i]; a.actor_id = i+1;
        const auto* actor = f.visible(i+1);
        const bool own = a.actor_id == f.clock.self;
        a.initial_bag = {actor ? actor->bag : 0,own ? p6::Source::self_observed : p6::Source::visible_subject};
        a.initial_gold = {own ? f.own.gold : 0,own ? p6::Source::self_observed : p6::Source::conservative};
        a.occupied_turns = fact<p6::Gems>(duration);
        if (gains[i]) {
            const int ix = s.production_count++;
            auto& p = s.productions[ix]; p.event_id = 1000+i; p.tick = duration-8;
            p.gems = fact<p6::Gems>(gains[i]); p.gold = fact<p6::Gems>(0);
            p.source = p6::Source::synthetic_fixture; p.holder_count = 1; p.holders[0] = i+1;
            event(a,p6::EventKind::production,1000+i,duration-8,ix);
            event(a,p6::EventKind::tribute,2000+i,duration-5);
        } else if (a.initial_bag.value > 0) {
            event(a,p6::EventKind::tribute,2000+i,duration-5);
        }
        event(a,p6::EventKind::renewal,3000+i,duration);
    }
    return s;
}
// Fixed public fixture geography: distinct groups 7 and 8 each yield twelve
// gems. The supported clearer can move the second conversion inside the same
// 39-turn stress window; it cannot enlarge either camp's payload. The full
// schedules retain the later U conversion and all return time, even when late.
// Source 8's equal later work preserves the single observed first-source turn
// advantage. This declared synthetic continuation does not establish native
// geometry or throughput. The helper's later independent production is charged
// and retained even though its bank falls outside the stress window.
p6::Schedule campaign(const p6::Frame& f, bool supported, int duration = -1,
                      bool first_solo = false, int helper = 2) {
    auto s = schedule(f,{0,0,0},48);
    s.production_count = 0;
    for (int i = 0; i < s.actor_count; ++i) s.actors[i].event_count = 0;
    for (int i = 0; i < s.actor_count; ++i) {
        auto& a = s.actors[i];
        const bool late_helper = supported && a.actor_id == helper;
        const int actor_duration = duration >= 0 ? duration : a.actor_id == 1 ? (supported ? 44 : 45) :
            late_helper ? 48 : 18;
        a.occupied_turns = fact<p6::Gems>(actor_duration);
        const int cycles = a.actor_id == 1 ? 2 : 1;
        if (late_helper && a.initial_bag.value > 0)
            event(a,p6::EventKind::tribute,2100+a.actor_id,15);
        for (int cycle = 0; cycle < cycles; ++cycle) {
            const bool collector = a.actor_id == 1;
            const int ix = s.production_count++; auto& p = s.productions[ix];
            // Actor 1's distinct event IDs bind public source 7 then source 8.
            p.event_id = collector ? 1007+cycle : 1100+a.actor_id;
            const int production = collector ? cycle ? (supported ? 36 : 37) :
                (supported && !first_solo ? 11 : 12) : late_helper ? 42 : 10;
            const int bank = collector ? cycle ? (supported ? 39 : 40) :
                (supported && !first_solo ? 14 : 15) : late_helper ? 45 : 15;
            p.tick = production; p.gems = fact<p6::Gems>(collector ? 12 : 10);
            p.gold = fact<p6::Gems>(0); p.source = p6::Source::synthetic_fixture;
            p.holder_count = 1; p.holders[0] = a.actor_id;
            event(a,p6::EventKind::production,p.event_id,production,ix);
            event(a,p6::EventKind::tribute,2000+p.event_id,bank);
        }
        event(a,p6::EventKind::renewal,3000+a.actor_id,actor_duration);
    }
    return s;
}
p6::CycleCertificate certificate(const p6::Frame& f, int id, int group) {
    p6::CycleCertificate c; c.clock = f.clock; c.actor = id; c.group = group;
    const auto* a = f.visible(id);
    if (a) c.clear = c.hold = c.bank = c.recovery = {a->health,a->max_health,10,0,0,0,true};
    c.route_known = c.bank_executable = c.renewal_reachable = c.solo = true;
    return c;
}
ActionReceipt receipt(const Command& c, bool success = true) {
    return {c.clock,c.clock,c.token,c.action,true,success};
}
struct Trace {
    int issued = -1, production = -1, payout = -1, bank = -1, spawn = -1, renewed = -1;
    int amount = 0;
    std::uint32_t serial = 0;
    bool actual_purchase = false;
};
struct Fixture {
    std::unique_ptr<Inputs> in = std::make_unique<Inputs>();
    EvidenceLedger ledger{1}; ProbeBudget probes{};
    LocalFrame local{}; Work work{};
    CycleTrace cycle_trace{};
    ContextDeclaration component{}, supported{};
    int own_receipt = 0;
    Fixture() {
        auto& f = in->frame; f.clock = {0,1,0}; f.own.gold = 15;
        for (int id = 1; id <= 3; ++id) {
            p6::Actor a; a.id = id; a.pick_ordinal = id-1;
            a.type = id == 1 ? p6::Champion::marcella : id == 2 ? p6::Champion::tavi : p6::Champion::varg;
            a.team = p6::Team::ally; a.health = a.max_health = 100; a.strength = 20;
            a.alive = a.can_move = a.can_attack = true;
            a.position = id == 1 ? p6::Point{0,0} : p6::Point{2,id-1};
            if (id == 1) f.own.actor = a;
            else f.actors[f.actor_count++] = {a,f.clock,id,p6::Knowledge::exact};
        }
        p6::Group g; g.id = 7; g.original_spawn_visible = g.all_spawns_visible = g.uncontested_now = true;
        g.lifecycle = p6::Lifecycle::living; f.group = {g,f.clock,7,p6::Knowledge::exact};
        in->settings = {2000,1000,5}; in->settings_identity = {17,p6::Source::queried_setting};
        in->draft.champions = {p6::Champion::marcella,p6::Champion::tavi,p6::Champion::varg,
            p6::Champion::other,p6::Champion::other};
        in->draft.actors = {1,2,3,-1,-1}; in->draft.picks = 3;
        in->target_group = 7; in->offer_count = 2; in->independent = 0;
        in->own_state = {{100,100,20,10,1,0,99},f.clock,1,p6::Knowledge::exact};
        in->threat_regime = {0,f.clock,1,p6::Knowledge::exact};
        in->offers[0].plan = circuit(Organization::U); in->offers[1].plan = circuit(Organization::H);
        in->offers[0].schedule = campaign(f,false);
        in->offers[1].schedule = campaign(f,true);
        component.key.settings_id = 17; component.key.route_hash = plan_route_hash(in->offers[0].plan);
        component.key.actor_id = 1; component.key.champion = int(p6::Champion::marcella); component.key.role = 0;
        component.key.source_group = 7; component.key.route_template = 0; component.key.endpoint = 4;
        component.key.threat_regime = 0; component.key.participants[0] = 1; component.key.participant_count = 1;
        component.key.build = {99,100,20,10,1};
        component.envelope = {{20,100},{100,100},{20,20},{10,10},{1,1},{0,200},0};
        component.require_spawn_shop = true;
        // Fixed before execution: complete routes have two turns of declared
        // channel/arrival uncertainty. The neighboring mechanisms remain +2/+4.
        for (int h = 0; h < 3; ++h) component.hypotheses[h].offset = {2*h,2*h+2};
        supported = component; supported.kind = EpisodeKind::full_organization;
        supported.require_supported_work = true;
        supported.key.role = 1; supported.key.participant_count = 2; supported.key.participants[1] = 2;
        supported.key.route_hash = plan_route_hash(in->offers[1].plan);
        declare();
        in->probe.offer = 0; in->probe.endpoint_distinguishes = true;
        in->probe.named_continuations = {Organization::U,Organization::H};
        for (int b = 0; b < 3; ++b) {
            in->probe.continuation_plans[b] = in->offers[b == 1 ? 1 : 0].plan;
            in->probe.branches[b] = campaign(f,b == 1,-1,true);
        }
        tick(0);
    }
    void declare() {
        in->component_slot = ledger.predeclare(component); in->supported_slot = ledger.predeclare(supported);
        in->component_entry = in->offers[0].entry = component.key;
        in->supported_entry = in->offers[1].entry = supported.key;
        in->offers[0].context_slot = in->component_slot; in->offers[1].context_slot = in->supported_slot;
        check(in->component_slot >= 0 && in->supported_slot >= 0,"actual collector's bounded contexts predeclare");
    }
    void tick(int round) {
        auto& f = in->frame; f.clock.round = round; f.group.clock = f.clock;
        for (int i = 0; i < f.actor_count; ++i) f.actors[i].clock = f.clock;
        in->own_state.clock = in->threat_regime.clock = f.clock;
        in->own_state.subject = in->threat_regime.subject = f.clock.self;
        in->spawn_shop_legal = {f.own.at_spawn,f.clock,f.clock.self,p6::Knowledge::exact};
        auto& n = in->own_state.value; n.health = int(f.own.actor.health); n.max_health = int(f.own.actor.max_health);
        n.strength = int(f.own.actor.strength); n.bag = int(f.own.actor.bag);
        in->windows.valid = true; in->windows.source = p6::Source::synthetic_fixture;
        in->windows.deadlines = {in->settings.max_rounds-round,78,39};
        in->windows.windows = {std::min(120,in->settings.max_rounds-round),78,39};
        in->windows.sampled_actor_id = f.clock.self; in->windows.sampled_round = round;
        for (int j = 0; j < in->offer_count; ++j) {
            auto& o = in->offers[j]; o.safety_count = in->draft.picks;
            for (int i = 0; i < in->draft.picks; ++i) o.safety[i] = certificate(f,in->draft.actors[i],
                participant(o.plan,in->draft.actors[i]) ? o.plan.group : 9);
        }
        for (int b = 0; b < 3; ++b) {
            in->probe.safety_count[b] = 3;
            for (int i = 0; i < 3; ++i) in->probe.safety[b][i] = certificate(f,i+1,
                participant(in->probe.continuation_plans[b],i+1) ? 7 : 9);
        }
        local.frame = f; local.current_round = round; local.safety_count = in->draft.picks;
        local.milestones = {};
        for (int i = 0; i < in->draft.picks; ++i) local.safety[i] = certificate(f,in->draft.actors[i],7);
        local.legal = {{},f.clock,f.clock.self,p6::Knowledge::exact};
        auto& legal = local.legal.value;
        legal.safe_now = legal.attack_legal = legal.altar_legal = legal.recall_safe = legal.renewal_productive = true;
        const auto& p = work.active ? work.plan : in->offers[0].plan;
        for (Leg leg : {Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
            auto& access = legal.routes[int(leg)]; const auto& route = route_for(p,leg);
            access.assumptions_hold = route_valid(route); access.next_move_legal = true;
            for (int i = 0; i < route.count; ++i) if (route.tiles[i] == f.own.actor.position) {
                access.next_tile = route.tiles[std::min(i+1,route.count-1)]; break;
            }
        }
    }
    void reset_cycle(int round) {
        auto& f = in->frame; f.own.actor.position = {0,0}; f.own.actor.spawn_group = -1;
        f.own.actor.bag = 0; f.own.actor.channel = p6::Channel::none; f.own.at_spawn = false;
        f.group.value.lifecycle = p6::Lifecycle::living;
        for (int i = 0; i < f.actor_count; ++i) {
            auto& actor = f.actors[i].value; actor.spawn_group = -1;
            actor.action = p6::WitnessAction::none;
            actor.position = actor.id == 2 ? p6::Point{2,1} : p6::Point{2,2};
        }
        work = {}; tick(round);
        in->offers[0].schedule = campaign(f,false); in->offers[1].schedule = campaign(f,true);
    }
    Trace physical_cycle(int start, bool supported_cycle, bool own_conversion = true, bool own_shop = true,
                         bool expected_damage = false, bool current_admission = false);
    void qualify();
};

void current_ordinary(Fixture& f) {
    auto& in=*f.in;in.offer_count=1;in.independent=0;
    auto& offer=in.offers[0];auto& s=offer.schedule;
    const int own=p6::actor_index(s,in.frame.clock.self);
    check(own>=0,"current ordinary fixture retains its actual own complete schedule");
    auto actor=s.actors[own];int count=0;
    for(int p=0;p<s.production_count;++p) {
        bool holder=false;
        for(int h=0;h<s.productions[p].holder_count;++h)holder|=s.productions[p].holders[h]==actor.actor_id;
        if(!holder)continue;
        s.productions[count]=s.productions[p];
        for(int e=0;e<actor.event_count;++e)
            if(actor.events[e].kind==p6::EventKind::production&&actor.events[e].production_index==p)
                actor.events[e].production_index=count;
        ++count;
    }
    s.actors[0]=actor;s.actor_count=1;s.production_count=count;
    offer.safety_count=1;offer.safety[0]=certificate(in.frame,in.frame.clock.self,offer.plan.group);
    f.local.safety_count=1;f.local.safety[0]=offer.safety[0];f.local.admission_only=true;
}

Trace Fixture::physical_cycle(int start, bool supported_cycle, bool own_conversion, bool own_shop, bool expected_damage,
                              bool current_admission) {
    reset_cycle(start);
    if(current_admission)current_ordinary(*this);
    const auto d = current_admission?choose(*in,ledger,probes,Arm::active):decide(*in,ledger,probes,Arm::active);
    check(d.option == (supported_cycle ? 1 : 0),"physical cycle selects the expected earned organization");
    const auto admission = current_admission?
        admit_current(*in,ledger,probes,Arm::active,work,[&](const Decision& now)->const LocalFrame& {
            check(now.option==0&&!now.deliberate_probe,"ordinary current callback receives the freshly selected sole U");
            return local;
        }):commit(*in,local,d,ledger,probes,Arm::active,work);
    if (!admission.started || !admission.episode || work.serial != 0)
        std::fprintf(stderr,"cycle start=%d H=%d option=%d record=%d probe=%d used=%d admission=%d episode=%u rejection=%d serial=%llu\n",
            start,int(supported_cycle),d.option,int(d.record_episode),int(d.deliberate_probe),probes.used,
            int(admission.started),admission.episode,int(admission.rejection),static_cast<unsigned long long>(work.serial));
    check(admission.started && admission.episode && work.serial == 0,"cycle admission issues before any action");
    check(cycle_trace.begin(ledger,work),"own trace latches the actually issued episode and newly started Work");
    local.admission_only=false;
    Trace trace; trace.issued = start; trace.serial = admission.episode;
    trace.amount = 12; // Same named source 7, same actual camp reward in both organizations.
    auto step = [&](int offset, p6::Point position, bool payout = false, bool bank = false) {
        auto& f = in->frame;
        f.own.actor.position = position; f.own.at_spawn = position == p6::Point{4,1};
        if (expected_damage && offset == 2) f.own.actor.health = 70;
        if (expected_damage && f.own.at_spawn) f.own.actor.health = 100;
        f.own.actor.spawn_group = position == p6::Point{3,0} ? 7 : -1;
        tick(start+offset);
        if (payout) {
            f.own.actor.bag = trace.amount; tick(start+offset);
            local.milestones = {{7,true,false,trace.amount,0},f.clock,1,p6::Knowledge::exact};
            trace.payout = f.clock.round;
        }
        if (bank) {
            f.own.actor.bag = 0; f.own.actor.channel = p6::Channel::none; tick(start+offset);
            local.milestones = {{7,false,own_conversion,0,trace.amount},f.clock,1,p6::Knowledge::exact};
            if (!own_conversion) in->frame.own_team_score = 100;
            trace.bank = f.clock.round;
        }
        auto c = advance(*in,local,ledger,Arm::active,work);
        if (c.action == PhysicalAction::attack || c.action == PhysicalAction::move || c.action == PhysicalAction::tribute)
            check(work.confirm(c,receipt(c)),"physical executor consumes actual same-clock action receipt");
        if (!own_shop && f.own.at_spawn) in->spawn_shop_legal.knowledge = p6::Knowledge::unknown;
        cycle_trace.observe(local,work,in->spawn_shop_legal);
        if (f.own.at_spawn) trace.spawn = f.clock.round;
        return c;
    };
    auto c = step(0,{0,0});
    check(c.action == PhysicalAction::move && c.target == p6::Point{1,0},"productive trial follows first retained tile");
    c = step(1,{1,0}); check(c.target == p6::Point{2,0},"observed arrival advances retained ingress");
    if (supported_cycle) {
        in->frame.actors[0].value.action = p6::WitnessAction::useful_attack;
        in->frame.actors[0].value.action_group = 7;
    }
    c = step(2,{2,0}); trace.production = start+2;
    check(c.action == PhysicalAction::attack && work.productive_action_seen,"actual successful attack supplies production");
    if (expected_damage)
        check(work.active && ledger.component_qualified(in->component_slot,component.key,start+2),
              "expected H damage inside its frozen transition envelope preserves the applicable route component mid-episode");
    if (supported_cycle) in->frame.actors[0].value.action = p6::WitnessAction::none;
    c = step(3,{2,0});
    check(c.action == PhysicalAction::attack,"second actual attack continues the still living camp clear");
    const int shift = supported_cycle ? -1 : 0;
    if (!supported_cycle) {
        c = step(4,{2,0});
        check(c.action == PhysicalAction::attack,"solo third attack spans the predeclared common and reposition duration overlap");
    }
    in->frame.group.value.lifecycle = p6::Lifecycle::reward_available;
    c = step(5+shift,{2,0}); check(c.target == p6::Point{3,0},"personally observed clearance begins capture entry");
    for (int round = 6+shift; round <= 11+shift; ++round) {
        if (supported_cycle) {
            auto& helper = in->frame.actors[0].value;
            helper.action = p6::WitnessAction::none;
            helper.position = round < 8+shift ? p6::Point{3,1} : p6::Point{4,1};
            helper.spawn_group = round < 8+shift ? 7 : -1;
        }
        c = step(round,{3,0});
        check(c.action == PhysicalAction::hold && !work.payout_seen,"full conservative hold does not infer payout from elapsed time");
    }
    c = step(12+shift,{3,0},true);
    check(work.payout_seen && c.action == PhysicalAction::move && c.target == p6::Point{4,0},
          "own payout has actual holder ownership and leaves along bank route");
    c = step(13+shift,{4,0});
    check(c.action == PhysicalAction::tribute && work.tribute_started && !work.bank_seen,
          "actual tribute start remains distinct from confirmed conversion");
    const int channel_ticks = (trace.amount+9)/10;
    for (int offset = 14+shift; offset < 13+shift+channel_ticks; ++offset) {
        in->frame.own.actor.channel = p6::Channel::tribute; in->frame.own.actor.channel_valid = true;
        c = step(offset,{4,0});
        check(c.action == PhysicalAction::preserve_channel && !work.bank_seen,"safe tribute channel waits for actual own endpoint");
    }
    const int conversion_offset = 13+shift+channel_ticks;
    c = step(conversion_offset,{4,0},false,true);
    check(work.bank_seen == own_conversion && c.action == PhysicalAction::move && c.target == p6::Point{4,1},
          "return follows only actual bank observations; team score supplies no own attribution");
    const std::array<p6::Point,5> return_tiles{{{4,1},{3,1},{2,1},{1,1},{0,0}}};
    for (int i = 0; i < int(return_tiles.size()); ++i) c = step(conversion_offset+i+1,return_tiles[i]);
    trace.renewed = in->frame.clock.round;
    check(work.renewal_seen && work.cycle_completed == own_conversion && !work.active && trace.spawn > trace.bank,
          "complete productive route physically visits own spawn and returns; unresolved bank cannot become full cycle proof");
    const Premises terminal{supported_cycle ? supported.key : component.key,in->own_state.value,all_premises};
    const auto resolved = cycle_trace.finish(ledger,trace.renewed,terminal);
    check((resolved.accepted || !own_conversion) && resolved.full_positive == (own_conversion && own_shop),
          "earned qualification requires own physical capture bank and legal spawn return receipts");
    check(!cycle_trace.finish(ledger,trace.renewed,terminal).accepted,"second snapshot cannot resolve or duplicate productive proof");
    return trace;
}
void Fixture::qualify() {
    const auto first = physical_cycle(0,false);
    check(!ledger.component_qualified(in->component_slot,component.key,first.renewed),"one actual U cycle cannot qualify a component");
    const auto second = physical_cycle(64,false);
    check(ledger.component_qualified(in->component_slot,component.key,second.renewed),"two distinct own U cycles qualify actual capture bank spawn return");
    check(probes.used == 0,"best policy-admissible independent production earns the component without diagnostic charges");
    check(!ledger.organization_qualified(in->supported_slot,supported.key,second.renewed),"new helper membership inherits no full organization proof");
}

// Deliberately kept out of main's optimized stack frame; Inputs, ledger and
// retained alternatives live in one heap fixture on default WASI stack sizes.
__attribute__((noinline)) void same_input_admission() {
    auto f = std::make_unique<Fixture>(); auto& in = *f->in;
    check(valid_inputs(in),"same-input hand oracle uses current own state and real draft IDs");
    check(legal_offer(in,in.offers[0]) == Rejection::none && legal_offer(in,in.offers[1]) == Rejection::none,
          "both U and H are legal safe complete cycles on identical observations");
    check(evaluate(in,in.offers[0].schedule).min_margin == 32 && evaluate(in,in.offers[1].schedule).min_margin == 34,
          "oracle accounts for displaced helper production and future holder ownership");
    const auto a = decide(in,f->ledger,f->probes,Arm::active);
    const auto c = decide(in,f->ledger,f->probes,Arm::control);
    check(a.option == 0 && a.organization == Organization::U && c.option == 1 && c.organization == Organization::H,
          "identical feasible input separates earned U from immediate control H");
    check(!a.deliberate_probe && !a.capital_allowed && !c.capital_allowed,
          "best active-admissible U is ordinary production while current capital stays gated off spawn");
    const auto admission = commit(in,f->local,a,f->ledger,f->probes,Arm::active,f->work);
    check(admission.started && admission.episode && f->ledger.active() && f->work.serial == 0,
          "private component episode is issued before first movement command");
    check(advance(in,f->local,f->ledger,Arm::active,f->work).action == PhysicalAction::move,
          "issued U work begins along its retained route");
    check(f->probes.used == 0,"ordinary active U does not spend a diagnostic slot merely because control can propose H");
}

__attribute__((noinline)) void earned_center_cycles() {
    auto f = std::make_unique<Fixture>(); f->qualify();
    f->reset_cycle(90);
    auto d = decide(*f->in,f->ledger,f->probes,Arm::active);
    check(d.option == 1 && !d.deliberate_probe,"actual component permits one ordinary supported commissioning campaign");
    const auto first = f->physical_cycle(90,true);
    check(!f->ledger.organization_qualified(f->in->supported_slot,f->supported.key,first.renewed),
          "one H completion does not inherit the two solo cycles as organization evidence");
    const auto second = f->physical_cycle(116,true);
    check(f->ledger.organization_qualified(f->in->supported_slot,f->supported.key,second.renewed),
          "two actual own H capture conversion spawn return cycles qualify actual helper membership");
    check(f->probes.used == 0,"ordinary U and H commissioning spend no diagnostic charges");

    // Return from named productive work by a confirmed legal recall. Reaching
    // spawn follows its actual five turn-end ticks before capital is available.
    auto& in = *f->in; const int recall_round = second.renewed+1; f->tick(recall_round);
    auto c = advance(in,f->local,f->ledger,Arm::active,f->work);
    check(c.action == PhysicalAction::recall && f->work.confirm(c,receipt(c)),"completed supported work starts actual safe spawn recall");
    for (int tick = 1; tick < 5; ++tick) {
        in.frame.own.actor.channel = p6::Channel::recall; in.frame.own.actor.channel_valid = true;
        f->tick(recall_round+tick); c = advance(in,f->local,f->ledger,Arm::active,f->work);
        check(c.action == PhysicalAction::preserve_channel,"actual safe return recall is preserved until completion");
    }
    in.frame.own.actor.channel = p6::Channel::none; in.frame.own.actor.position = {4,1}; in.frame.own.at_spawn = true;
    f->tick(recall_round+5);
    d = decide(in,f->ledger,f->probes,Arm::active);
    check(d.option == 1 && d.capital_allowed,"qualified collector with secured bag and fresh legal own spawn can invest current gold");
    const int cost = 5; const auto before = in.frame.own.gold;
    check(in.spawn_shop_legal.current(in.frame.clock,1) && in.spawn_shop_legal.value && before >= cost,
          "synthetic purchase dispatch is backed by actual spawn arrival and present personal gold");
    auto purchase = std::make_unique<PurchasePlan>();
    purchase->bundle_id = 0; purchase->cost = cost; purchase->equipment_after = 100;
    purchase->queried_price = {cost,p6::Source::queried_setting};
    purchase->item_legal = {true,in.frame.clock,1,p6::Knowledge::exact};
    purchase->without_purchase = campaign(in.frame,true);
    purchase->with_purchase = purchase->without_purchase;
    event(purchase->with_purchase.actors[0],p6::EventKind::purchase,4000,0);
    auto& expense = purchase->with_purchase.actors[0].events[purchase->with_purchase.actors[0].event_count-1];
    expense.gold_cost = fact<p6::Gems>(cost);
    Shop shop;
    purchase->queried_price = {};
    check(!shop.propose(in,f->local,f->ledger,Arm::active,*purchase).valid,"unqueried item price cannot license a purchase");
    purchase->queried_price = {cost,p6::Source::queried_setting};
    purchase->cost = before+1;
    check(!shop.propose(in,f->local,f->ledger,Arm::active,*purchase).valid,"unaffordable current purchase cannot borrow later income");
    purchase->cost = cost; purchase->item_legal.knowledge = p6::Knowledge::unknown;
    check(!shop.propose(in,f->local,f->ledger,Arm::active,*purchase).valid,"unseen shop legality cannot authorize current purchase");
    purchase->item_legal.knowledge = p6::Knowledge::exact;
    check(!shop.propose(in,f->local,f->ledger,Arm::active,*purchase).valid,"equipment with no complete-cycle gain cannot earn capital merely through gold availability");
    // The declared stronger bundle removes two future occupied owner turns.
    // Every source and payload is identical to the rejected no-gain schedule.
    for (int e = 0; e < purchase->with_purchase.actors[0].event_count; ++e) {
        auto& action = purchase->with_purchase.actors[0].events[e];
        if (action.kind == p6::EventKind::production || action.kind == p6::EventKind::tribute) action.tick -= 2;
    }
    for (int i = 0; i < purchase->with_purchase.production_count; ++i)
        if (purchase->with_purchase.productions[i].holders[0] == 1) purchase->with_purchase.productions[i].tick -= 2;
    const auto base_occupied = purchase->without_purchase.actors[0].occupied_turns.value;
    purchase->with_purchase.actors[0].occupied_turns = fact<p6::Gems>(base_occupied-2);
    int base_return = -1;
    for (int e = 0; e < purchase->without_purchase.actors[0].event_count; ++e)
        if (purchase->without_purchase.actors[0].events[e].kind == p6::EventKind::renewal)
            base_return = purchase->without_purchase.actors[0].events[e].tick;
    check(base_occupied >= 2 && base_return >= 2,"beneficial bundle has a complete positive baseline return to shorten");
    for (int e = 0; e < purchase->with_purchase.actors[0].event_count; ++e)
        if (purchase->with_purchase.actors[0].events[e].kind == p6::EventKind::renewal)
            purchase->with_purchase.actors[0].events[e].tick = base_return-2;
    const auto transaction = shop.propose(in,f->local,f->ledger,Arm::active,*purchase);
    check(transaction.valid && transaction.cost == cost,"earned safe spawn collector issues one current funded beneficial bundle before dispatch");
    check(!shop.propose(in,f->local,f->ledger,Arm::active,*purchase).valid,"pending transaction cannot issue a duplicate purchase");
    in.frame.own.gold -= cost; ++in.frame.clock.revision; f->tick(recall_round+5);
    check(!shop.confirm(transaction,in,f->ledger),"gold debit without expected actual items cannot confirm a purchase");
    in.own_state.value.equipment_hash = 100; in.frame.own.actor.strength = 25;
    ++in.frame.clock.revision; f->tick(recall_round+5);
    check(shop.confirm(transaction,in,f->ledger),"same-round fresh own gold and actual item query confirms issued purchase");
    check(!shop.confirm(transaction,in,f->ledger),"duplicate purchase receipt cannot count twice");
    in.offers[0].schedule = campaign(in.frame,false); in.offers[1].schedule = campaign(in.frame,true);
    check(in.frame.own.gold == before-cost,"own current purchase debits actual personal resources");
    check(decide(in,f->ledger,f->probes,Arm::active).organization == Organization::U,
          "changed equipment stops new concentration rather than inheriting old supported proof");
}

__attribute__((noinline)) void qualification_guards() {
    for (int mutation = 0; mutation < 6; ++mutation) {
        auto f = std::make_unique<Fixture>(); f->qualify(); auto& in = *f->in;
        const int round = in.frame.clock.round+1; f->reset_cycle(round);
        if (mutation == 0) {
            f->tick(round+256);
        } else if (mutation == 1) {
            in.own_state.value.focus = 11;
        } else if (mutation == 2) {
            in.threat_regime.value = 1;
        } else if (mutation == 3) {
            in.frame.own.actor.type = in.draft.champions[0] = p6::Champion::ren;
        } else if (mutation == 4) {
            in.offers[0].plan.renewal.tiles[2] = {3,2};
            in.component_entry.route_hash = in.offers[0].entry.route_hash = plan_route_hash(in.offers[0].plan);
        } else {
            in.supported_entry.participants[1] = in.offers[1].entry.participants[1] = 3;
            in.offers[1].plan.crew[1] = 3; in.offers[1].plan.pads[1] = {2,2};
        }
        f->tick(in.frame.clock.round);
        const auto d = decide(in,f->ledger,f->probes,Arm::active);
        check(d.organization == Organization::U && d.option == 0 && !d.capital_allowed,
              "expired route threat Focus champion or actual membership premises stop new H and preserve U");
        if (mutation == 3) {
            in.frame.own.actor.type = in.draft.champions[0] = p6::Champion::marcella;
            f->tick(in.frame.clock.round+1);
            check(decide(in,f->ledger,f->probes,Arm::active).organization == Organization::U,
                  "restoring a former champion never resurrects materially invalidated private proof");
        }
    }
}

__attribute__((noinline)) void expected_damage_and_new_entry_band() {
    auto f = std::make_unique<Fixture>(); f->qualify();
    const auto supported = f->physical_cycle(90,true,true,true,true);
    check(f->in->frame.own.actor.health == 100,"actual spawn recovery restores the planned return health");
    f->in->frame.own.actor.health = 70; f->tick(supported.renewed+1);
    check(decide(*f->in,f->ledger,f->probes,Arm::active).organization == Organization::U,
          "a fresh start in another health band requires a new guarded context even though prior within-episode damage was expected");
}

__attribute__((noinline)) void actual_resources_and_safe_rollback() {
    auto f = std::make_unique<Fixture>(); auto& in = *f->in;
    in.offers[1].capital_cost = 16;
    check(decide(in,f->ledger,f->probes,Arm::control).option == 0,
          "even immediate control H cannot spend more than present personal gold");
    in.offers[1].capital_cost = 15;
    auto& s = in.offers[1].schedule;
    s.productions[0].gold = fact<p6::Gems>(100);
    event(s.actors[0],p6::EventKind::purchase,4000,12);
    s.actors[0].events[s.actors[0].event_count-1].gold_cost = fact<p6::Gems>(20);
    check(evaluate(in,s).eligible() && decide(in,f->ledger,f->probes,Arm::control).option == 0,
          "forecast later capture gold cannot finance current collector capital");

    // Rollback during an actually valid channel preserves its work, even when
    // the experiment's own private eligibility has disappeared.
    f = std::make_unique<Fixture>(); f->qualify(); f->reset_cycle(90);
    auto d = decide(*f->in,f->ledger,f->probes,Arm::active);
    check(commit(*f->in,f->local,d,f->ledger,f->probes,Arm::active,f->work).started,"safe rollback fixture commissions H");
    f->in->frame.own.actor.channel = p6::Channel::tribute;
    f->in->frame.own.actor.channel_valid = true;
    f->in->threat_regime.value = 1; f->tick(91);
    const auto c = advance(*f->in,f->local,f->ledger,Arm::active,f->work);
    check(c.action == PhysicalAction::preserve_channel && f->work.stop_requested,
          "lost earned-center premises request rollback while preserving a safe valid channel");
    f->in->frame.own.actor.channel = p6::Channel::none; f->tick(92);
    check(advance(*f->in,f->local,f->ledger,Arm::active,f->work).action != PhysicalAction::move && !f->work.active,
          "safe boundary completes rollback before any unsupported H travel");
}

__attribute__((noinline)) void private_helper_keeps_existing_bag() {
    auto f = std::make_unique<Fixture>(); auto& in = *f->in;
    const auto collector = in.frame.own.actor;
    in.frame.own.actor = in.frame.actors[0].value; in.frame.clock.self = 2;
    in.frame.actors[0] = {collector,in.frame.clock,1,p6::Knowledge::exact};
    in.frame.actors[0].value.position = {2,0};
    in.frame.own.actor.position = {0,0}; in.frame.own.actor.bag = 6;
    in.offers[0].plan = circuit(Organization::U,2);
    auto& hp = in.offers[1].plan; hp = circuit(Organization::H,2);
    hp.bank = path({{3,0},{3,1},{4,1}}); hp.release = path({{3,0},{3,1}});
    hp.renewal = path({{4,1},{4,2}}); hp.forecast_rounds = 24;
    in.offers[0].schedule = campaign(in.frame,false);
    in.offers[1].schedule = campaign(in.frame,true);
    auto helper_ledger = std::make_unique<EvidenceLedger>(2);
    f->tick(0);
    check(decide(in,*helper_ledger,f->probes,Arm::active).organization == Organization::U,
          "a core helper starts productive U without private proof or a visible proposal");
    in.visible_proposer = {7,in.frame.clock,1,p6::Knowledge::exact};
    auto d = decide(in,*helper_ledger,f->probes,Arm::active);
    check(d.option == 1 && helper_ledger->size() == 0,"empty private helper ledger adopts only current personally visible collector");
    check(commit(in,f->local,d,*helper_ledger,f->probes,Arm::active,f->work).started && !helper_ledger->active(),
          "helper adoption imports no collector proof and issues no borrowed private episode");
    auto step = [&](int round, p6::Point position) {
        in.frame.own.actor.position = position;
        in.frame.own.actor.spawn_group = position == p6::Point{3,0} ? 7 : -1;
        f->tick(round);
        auto c = advance(in,f->local,*helper_ledger,Arm::active,f->work);
        if (c.action == PhysicalAction::attack || c.action == PhysicalAction::move || c.action == PhysicalAction::tribute)
            check(f->work.confirm(c,receipt(c)),"helper's actual personal action receipt confirms independently");
        return c;
    };
    step(0,{0,0}); step(1,{1,0}); auto c = step(2,{2,1});
    check(c.action == PhysicalAction::attack,"safe visible helper contributes actual clearing work");
    in.frame.group.value.lifecycle = p6::Lifecycle::reward_available;
    step(3,{2,1}); c = step(4,{3,0});
    check(c.action == PhysicalAction::hold && in.frame.own.actor.bag == 6 && !f->work.helper_released,
          "old bag stays owned while unobserved remaining holder prevents withdrawal");
    in.frame.actors[0].value.position = {3,2}; in.frame.actors[0].value.spawn_group = 7;
    c = step(5,{3,0});
    check(c.action == PhysicalAction::move && c.target == p6::Point{3,1} && in.frame.own.actor.bag == 6,
          "actual safe named holder licenses withdrawal without transferring helper's existing bag");
    c = step(6,{3,1});
    check(f->work.helper_released && c.action == PhysicalAction::move && c.target == p6::Point{4,1} && !f->work.payout_seen,
          "leaving capture changes future payout ownership and retains the loaded helper's own bank route");
    c = step(7,{4,1});
    check(c.action == PhysicalAction::tribute && f->work.tribute_starting_bag == 6,
          "released helper independently starts tribute of its whole preexisting bag");
    in.frame.own.actor.bag = 0; in.frame.own.actor.position = {4,1}; f->tick(8);
    f->local.milestones = {{7,false,true,0,6},in.frame.clock,2,p6::Knowledge::exact};
    c = advance(in,f->local,*helper_ledger,Arm::active,f->work);
    check(f->work.bank_seen && f->work.bank_lower == 6 && c.target == p6::Point{4,2},
          "actual helper conversion secures only its own retained six gems");
    check(f->work.confirm(c,receipt(c)),"helper confirms independent renewed movement");
    step(9,{4,2});
    check(f->work.renewal_seen && !f->work.active && !f->work.payout_seen && !f->work.cycle_completed,
          "released helper renews productive U without claiming the collector's future capture");
    check(helper_ledger->size() == 0 && !helper_ledger->active() && in.frame.actors[0].value.bag == 0,
          "helper retains a private empty ledger and cannot mutate the collector's resources");
}

__attribute__((noinline)) void unknown_and_censored_are_not_proof() {
    {
        auto f = std::make_unique<Fixture>(); f->physical_cycle(0,false);
        auto predecessor = std::make_unique<Work>(f->work);
        f->reset_cycle(64); const auto d = decide(*f->in,f->ledger,f->probes,Arm::active);
        const auto issued = commit(*f->in,f->local,d,f->ledger,f->probes,Arm::active,f->work);
        check(issued.started && issued.episode,"new context-matched episode can issue a fresh second productive cycle");
        CycleTrace stale;
        check(!stale.begin(f->ledger,*predecessor),"completed predecessor Work cannot supply newly issued episode milestones");
        predecessor->step(f->local);
        check(predecessor->last_round == 64 && !stale.begin(f->ledger,*predecessor),
              "refreshing old completed Work's round cannot rebind old capture bank spawn return flags");
        CycleTrace fresh;
        check(fresh.begin(f->ledger,f->work),"actually newly started unexecuted Work binds to the second issue");
        check(!f->ledger.component_qualified(f->in->component_slot,f->component.key,64),
              "retimestamping one completed cycle still cannot create two earned successes");
        f->ledger.invalidate_active(64);
    }
    {
        auto f = std::make_unique<Fixture>(); f->physical_cycle(0,false,false,true);
        check(f->in->frame.own_team_score == 100 &&
              !f->ledger.component_qualified(f->in->component_slot,f->component.key,f->in->frame.clock.round),
              "public team score plus missing own bank attribution supplies no earned-center proof");
    }
    {
        auto f = std::make_unique<Fixture>(); f->physical_cycle(0,false,true,false);
        check(f->work.cycle_completed &&
              !f->ledger.component_qualified(f->in->component_slot,f->component.key,f->in->frame.clock.round),
              "completed capture bank return without known legal own spawn visit cannot qualify the card component");
    }
    {
        auto f = std::make_unique<Fixture>(); auto d = decide(*f->in,f->ledger,f->probes,Arm::active);
        const auto a = commit(*f->in,f->local,d,f->ledger,f->probes,Arm::active,f->work);
        for (int round = 0; round <= 6; ++round) {
            f->in->frame.own.actor.position = round == 0 ? p6::Point{0,0} : round == 1 ? p6::Point{1,0} : p6::Point{2,0};
            f->tick(round); const auto c = advance(*f->in,f->local,f->ledger,Arm::active,f->work);
            if (c.action == PhysicalAction::attack || c.action == PhysicalAction::move)
                check(f->work.confirm(c,receipt(c)),"censored cycle observes actual unfinished productive work continuously");
        }
        Observation o; o.round = 6; o.kind = ObservationKind::censored;
        o.premises = {f->component.key,f->in->own_state.value,all_premises};
        o.duration.source = DurationSource::personally_unfinished_through;
        o.duration.unfinished_through = 6; o.duration.continuous_personal_observation = true;
        const auto r = f->ledger.resolve(a.episode,o);
        check(r.accepted && !r.full_positive && r.status == Status::censored,
              "actually unfinished-through censor remains a bound rather than a completed success");
        check(!f->ledger.component_qualified(f->in->component_slot,f->component.key,6) && f->probes.used == 0 &&
              f->ledger.state(f->in->component_slot)->campaign_episodes == 1,
              "ordinary censored work earns no collector support and retains its spent commissioning attempt");
    }
}

__attribute__((noinline)) void independent_core_and_visible_extension() {
    {
        auto f = std::make_unique<Fixture>(); auto& in = *f->in;
        const auto old = in.frame.own.actor;
        in.frame.own.actor = in.frame.actors[1].value; in.frame.clock.self = 3;
        in.frame.actors[1] = {old,in.frame.clock,1,p6::Knowledge::exact};
        in.frame.own.actor.position = {0,0}; in.offers[0].plan = circuit(Organization::U,3);
        in.offers[0].schedule = campaign(in.frame,false);
        in.offers[1].schedule = campaign(in.frame,true); f->tick(0);
        auto private_ledger = std::make_unique<EvidenceLedger>(3);
        check(decide(in,*private_ledger,f->probes,Arm::active).organization == Organization::U &&
              physical_admissible(in.offers[0].plan,f->local),
              "third core producer also has a legal independent first-income path with no borrowed proof");
    }
    {
        auto f = std::make_unique<Fixture>(); auto& in = *f->in;
        in.frame.actors[in.frame.actor_count++] = {in.frame.own.actor,in.frame.clock,1,p6::Knowledge::exact};
        auto& extra = in.frame.own.actor; extra.id = 4; extra.pick_ordinal = 3; extra.type = p6::Champion::willow;
        extra.position = {0,0}; in.frame.clock.self = 4;
        in.picks = in.draft.picks = 4; in.draft.actors[3] = 4; in.draft.champions[3] = extra.type;
        in.frame.actors[2].value.position = {2,0};
        in.offers[0].plan = circuit(Organization::U,4);
        auto& h = in.offers[1].plan; h = circuit(Organization::H,2); h.crew[1] = 4; h.forecast_rounds = 24;
        auto add_extension = [&](p6::Schedule& s, int gain, bool late = false) {
            auto& a = s.actors[s.actor_count++]; a.actor_id = 4;
            a.initial_bag = {0,p6::Source::self_observed}; a.initial_gold = {in.frame.own.gold,p6::Source::self_observed};
            a.occupied_turns = fact<p6::Gems>(late ? 48 : 24);
            if (gain) {
                const int ix = s.production_count++; auto& p = s.productions[ix];
                p.event_id = 1004; p.tick = late ? 42 : 10; p.gems = fact<p6::Gems>(gain); p.gold = fact<p6::Gems>(0);
                p.source = p6::Source::synthetic_fixture; p.holder_count = 1; p.holders[0] = 4;
                event(a,p6::EventKind::production,1004,late ? 42 : 10,ix);
                event(a,p6::EventKind::tribute,2004,late ? 45 : 13);
            }
            event(a,p6::EventKind::renewal,3004,late ? 48 : 24);
        };
        in.offers[0].schedule = campaign(in.frame,false); add_extension(in.offers[0].schedule,10);
        in.offers[1].schedule = campaign(in.frame,true,-1,false,4); add_extension(in.offers[1].schedule,10,true);
        auto private_ledger = std::make_unique<EvidenceLedger>(4); f->tick(0);
        check(decide(in,*private_ledger,f->probes,Arm::active).organization == Organization::U,
              "fourth pick retains independent U until its own visible proposal");
        in.visible_proposer = {7,in.frame.clock,1,p6::Knowledge::exact};
        auto d = decide(in,*private_ledger,f->probes,Arm::active);
        check(d.organization == Organization::H && private_ledger->size() == 0,
              "fourth pick may voluntarily adopt an actual safe named collector without shared proof");
        in.frame.actors[2].knowledge = p6::Knowledge::unknown; f->tick(1);
        check(decide(in,*private_ledger,f->probes,Arm::active).organization != Organization::H,
              "losing personal view of proposer prevents a fresh extension commitment");
    }
}

__attribute__((noinline)) void first_lower_ranked_trial_cannot_promise_support() {
    auto f = std::make_unique<Fixture>(); auto& in = *f->in;
    in.offer_count = 3; in.independent = 2;
    auto& alternative = in.offers[2]; alternative.plan = circuit(Organization::U);
    alternative.plan.group = 9;
    alternative.plan.ingress = path({{0,0},{1,1},{2,2}});
    alternative.plan.capture = path({{2,2},{3,2}});
    alternative.plan.bank = path({{3,2},{4,2}});
    alternative.plan.renewal = path({{4,2},{4,1},{3,1},{2,1},{1,1},{0,0}});
    alternative.schedule = campaign(in.frame,false); alternative.schedule.target_id = 9;
    // The independent alternative's different static route brings its second
    // fixed twelve-gem bank just inside the declared stress window.
    for (int e = 0; e < alternative.schedule.actors[0].event_count; ++e)
        if (alternative.schedule.actors[0].events[e].kind == p6::EventKind::tribute &&
            alternative.schedule.actors[0].events[e].tick == 40)
            alternative.schedule.actors[0].events[e].tick = 39;
    f->tick(0);
    check(legal_offer(in,alternative) == Rejection::none,"lower-ranked trial counterexample retains a legal better independent alternative");
    const auto d = decide(in,f->ledger,f->probes,Arm::active);
    check(d.option == 2 && !d.deliberate_probe && d.rejected[0] == Rejection::probe_value,
          "first lower-ranked U trial cannot promise an H continuation before any own applicable success");
    check(f->probes.used == 0 && !f->ledger.active(),"unadmitted lower-ranked first trial spends neither diagnostic slot nor private episode");
}

__attribute__((noinline)) void conditional_support_requires_an_applicable_first_cycle() {
    auto f = std::make_unique<Fixture>(); auto& in = *f->in;
    check(!common::continuation_component_permitted(in,in.offers[0],f->ledger),
          "zero completed own cycles cannot promise a supported continuation after one new trial");
    const auto first = f->physical_cycle(0,false); f->reset_cycle(64);
    check(!f->ledger.component_qualified(in.component_slot,f->component.key,64) &&
          common::continuation_component_permitted(in,in.offers[0],f->ledger),
          "one actual applicable full cycle permits only the conditional second-cycle path to H");

    const auto original = in.component_entry;
    in.component_entry.threat_regime = in.offers[0].entry.threat_regime = 1;
    in.threat_regime.value = 1;
    check(!common::continuation_component_permitted(in,in.offers[0],f->ledger),
          "changed guarded context cannot borrow the predecessor's single success for conditional support");
    in.component_entry = in.offers[0].entry = original; in.threat_regime.value = 0;

    const auto* slot = f->ledger.state(in.component_slot); int worst_offset = 0;
    for (int h = 0; h < slot->declaration.hypothesis_count; ++h)
        if (slot->consistent[h]) worst_offset = std::max(worst_offset,slot->declaration.hypotheses[h].offset.upper);
    const int duration = in.offers[0].plan.forecast_rounds+worst_offset;
    const int expiry_start = first.renewed+p8::qualification_rounds-duration;
    f->reset_cycle(expiry_start-1);
    check(common::continuation_component_permitted(in,in.offers[0],f->ledger),
          "conditional second cycle is feasible when its worst supported finish precedes prior-proof expiry");
    f->reset_cycle(expiry_start);
    check(!common::continuation_component_permitted(in,in.offers[0],f->ledger),
          "second-cycle finish at the exact 256-round expiry cannot promise a qualified H continuation");
    check(f->probes.used == 0 && !f->ledger.active(),"conditional-continuation reads neither issue work nor spend diagnostic slots");
}

__attribute__((noinline)) void qualified_proof_cannot_make_diagnostics_free() {
    {
        auto f = std::make_unique<Fixture>(); f->qualify(); auto& in = *f->in;
        const int latest = f->ledger.state(in.component_slot)->latest_positive;
        f->reset_cycle(latest+1);
        const auto duration = common::probe_terminal_round(in,in.offers[0],f->ledger)-in.frame.clock.round;
        check(duration > 0,"qualified component has a bounded conservative diagnostic duration");
        const int start = latest+p8::qualification_rounds-int(duration);
        f->reset_cycle(start);
        check(f->ledger.component_qualified(in.component_slot,f->component.key,start),
              "component remains qualified at diagnostic admission just before its own expiry");
        check(!common::continuation_component_permitted(in,in.offers[0],f->ledger),
              "currently qualified component cannot promise support when its worst diagnostic finish reaches proof expiry");
    }
    {
        auto f = std::make_unique<Fixture>(); f->qualify();
        f->physical_cycle(90,true); f->physical_cycle(116,true); f->reset_cycle(150);
        auto& in = *f->in;
        check(f->ledger.component_qualified(in.component_slot,f->component.key,150) &&
              f->ledger.organization_qualified(in.supported_slot,f->supported.key,150),
              "marked-work counterexample has actual qualified U and H histories");
        check(f->probes.spend(0) && f->probes.spend(1) && !f->probes.available(2),
              "marked qualified-work counterexample explicitly exhausts the persistent probe budget");
        for (int marker = 0; marker < 2; ++marker) {
            for (int i = 0; i < 2; ++i) {
                in.offers[i].diagnostic_segment = marker == 0;
                in.offers[i].marked_extra_turns = marker == 1 ? 1 : 0;
            }
            const auto d = decide(in,f->ledger,f->probes,Arm::active);
            check(d.option < 0 && d.rejected[0] == Rejection::evidence && d.rejected[1] == Rejection::evidence,
                  "qualified U and H marked as diagnostic or extra waiting cannot bypass trial admission as free ordinary work");
            check(!commit(in,f->local,d,f->ledger,f->probes,Arm::active,f->work).started &&
                  !f->ledger.active() && f->work.serial == 0 && f->probes.used == 2,
                  "rejected marked qualified work executes no action and cannot replenish or evade exhausted probe charges");
        }
    }
}

void current_work_equal(const Work& a,const Work& b) {
    check(std::tie(a.stage,a.failure,a.cursor,a.last_round,a.owner_turn,a.phase_started,a.phase_limit,
        a.assembly_turns,a.hold_turns,a.hold_extensions,a.bank_queue_turns,a.active,a.stop_requested,
        a.route_witness_active,a.clearance_seen,a.payout_seen,a.bank_seen,a.productive_action_seen,
        a.renewal_seen,a.cycle_completed,a.helper_released,a.helper_bag_return,a.tribute_started,a.pending,
        a.release_admitted,a.payout_lower,a.bank_lower,a.tribute_starting_bag,a.serial,a.generation)==
        std::tie(b.stage,b.failure,b.cursor,b.last_round,b.owner_turn,b.phase_started,b.phase_limit,
        b.assembly_turns,b.hold_turns,b.hold_extensions,b.bank_queue_turns,b.active,b.stop_requested,
        b.route_witness_active,b.clearance_seen,b.payout_seen,b.bank_seen,b.productive_action_seen,
        b.renewal_seen,b.cycle_completed,b.helper_released,b.helper_bag_return,b.tribute_started,b.pending,
        b.release_admitted,b.payout_lower,b.bank_lower,b.tribute_starting_bag,b.serial,b.generation)&&
        a.started_clock==b.started_clock,"current admission preserves the old complete Work lifecycle");
    check(std::tie(a.issued.action,a.issued.stage,a.issued.reason,a.issued.group,a.issued.leg,a.issued.token,a.issued.route_hash)==
        std::tie(b.issued.action,b.issued.stage,b.issued.reason,b.issued.group,b.issued.leg,b.issued.token,b.issued.route_hash)&&
        a.issued.clock==b.issued.clock&&a.issued.target==b.issued.target,"current admission issues no physical command");
    const auto& p=a.plan;const auto& q=b.plan;
    check(std::tie(p.organization,p.group,p.collector,p.altar,p.crew,p.pads,p.crew_count,p.forecast_rounds,
        p.remaining_rounds,p.capture_window,p.gem_bearing,p.complete_verified,p.rally_legal)==
        std::tie(q.organization,q.group,q.collector,q.altar,q.crew,q.pads,q.crew_count,q.forecast_rounds,
        q.remaining_rounds,q.capture_window,q.gem_bearing,q.complete_verified,q.rally_legal),"current admission retains every Plan header");
    for(const auto leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
        const auto& x=route_for(p,leg);const auto& y=route_for(q,leg);
        check(x.count==y.count&&x.verified==y.verified&&x.blocker_assumptions==y.blocker_assumptions,"retained route headers match old admission");
        for(int i=0;i<x.count;++i)check(x.tiles[i]==y.tiles[i],"retained active route matches old admission");
    }
}
void current_ledger_equal(const Fixture& a,const Fixture& b) {
    check(a.probes.used==b.probes.used&&a.probes.spent_epochs==b.probes.spent_epochs&&a.probes.overrun==b.probes.overrun,
        "ordinary current admission preserves persistent diagnostic charges");
    check(bool(a.ledger.active())==bool(b.ledger.active()),"current and old admission have the same active episode");
    if(const auto* x=a.ledger.active()) {
        const auto* y=b.ledger.active();
        check(std::tie(x->serial,x->context,x->issue_round,x->deadline,x->action,x->latched_epoch,x->stage,x->status)==
            std::tie(y->serial,y->context,y->issue_round,y->deadline,y->action,y->latched_epoch,y->stage,y->status)&&
            x->premises.key==y->premises.key&&x->premises.personally_observed==y->premises.personally_observed,
            "episode identity issue deadline action and exact guarded premises match the frozen commit");
        const auto& n=x->premises.state;const auto& m=y->premises.state;
        check(std::tie(n.health,n.max_health,n.strength,n.focus,n.growth,n.bag,n.equipment_hash)==
            std::tie(m.health,m.max_health,m.strength,m.focus,m.growth,m.bag,m.equipment_hash),"episode stores the same actual numeric entry state");
        for(int h=0;h<max_hypotheses;++h)check(x->prediction[h].lower==y->prediction[h].lower&&
            x->prediction[h].upper==y->prediction[h].upper,"episode prediction retains the old nominal duration and offsets");
    }
    check(a.ledger.size()==b.ledger.size(),"current admission cannot allocate new evidence contexts");
    for(int i=0;i<a.ledger.size();++i) {
        const auto* x=a.ledger.state(i);const auto* y=b.ledger.state(i);
        check(std::tie(x->attempts,x->completed,x->failures,x->unresolved,x->consecutive_unresolved,x->positive_completions,
            x->campaign_started,x->campaign_failed,x->campaign_episodes,x->ring_next,x->latest_terminal,x->safety_suspended)==
            std::tie(y->attempts,y->completed,y->failures,y->unresolved,y->consecutive_unresolved,y->positive_completions,
            y->campaign_started,y->campaign_failed,y->campaign_episodes,y->ring_next,y->latest_terminal,y->safety_suspended),
            "issue and failed-start cleanup preserve old evidence attempts and campaign state");
    }
}
__attribute__((noinline)) void current_admission_matches_old_commit() {
    for(const auto arm:{Arm::active,Arm::control})for(int fault=0;fault<15;++fault) {
        auto old=std::make_unique<Fixture>();auto fresh=std::make_unique<Fixture>();
        for(auto* f:{old.get(),fresh.get()}) {
            current_ordinary(*f);
            if(fault==1)f->local.fuel_remaining=f->local.action_reserve-1;
            if(fault==2)f->local.legal.value.safe_now=false;
            if(fault==3)++f->local.current_round;
            if(fault==4)f->local.fuel_resumed=true;
            if(fault==5)f->local.safety[0].solo=false;
            if(fault==6)f->local.safety[0].bank.incoming_total=1000;
            if(fault==7)f->work.generation=std::numeric_limits<std::uint64_t>::max();
            if(fault==8)f->work.last_round=f->in->frame.clock.round;
            if(fault==9)f->in->offers[0].capital_cost=1;
            if(fault==10)++f->in->offers[0].schedule.actors[0].initial_bag.value;
            if(fault==11)f->in->windows.valid=false;
            if(fault==12)++f->in->own_state.value.equipment_hash;
            if(fault==13) {
                f->in->frame.own.actor.bag=f->in->own_state.value.bag=f->local.frame.own.actor.bag=1;
                f->in->offers[0].schedule.actors[0].initial_bag.value=1;
            }
            if(fault==14) {
                f->in->frame.own.actor.health=f->in->own_state.value.health=f->local.frame.own.actor.health=20;
                f->in->offers[0].safety[0]=f->local.safety[0]=certificate(f->in->frame,1,7);
            }
        }
        const auto proposed=decide(*old->in,old->ledger,old->probes,arm);
        const auto expected=original_commit(*old->in,old->local,proposed,old->ledger,old->probes,arm,old->work);
        int callbacks=0;
        const auto actual=admit_current(*fresh->in,fresh->ledger,fresh->probes,arm,fresh->work,
            [&](const Decision& now)->const LocalFrame& {
                ++callbacks;check(now.option==0&&now.organization==Organization::U&&!now.deliberate_probe&&
                    now.clock==fresh->in->frame.clock,"callback sees the current sole ordinary U after policy selection");
                check(!fresh->work.active&&!fresh->ledger.active(),"local factory runs before Work or episode issue");
                return fresh->local;
            });
        check(actual.started==expected.started&&actual.episode==expected.episode,"current policy preserves old start and episode permissions");
        if(expected.started||expected.rejection==Rejection::unsafe||expected.rejection==Rejection::stale)
            check(actual.rejection==expected.rejection,"current physical and lifecycle failures keep old rejection");
        check(callbacks==(proposed.option>=0?1:0),"policy rejection never invokes the local observation factory");
        current_work_equal(old->work,fresh->work);current_ledger_equal(*old,*fresh);
        if(fault==7||fault==8)check(!actual.started&&!fresh->ledger.active()&&
            (arm==Arm::control||actual.episode!=0),"failed public Work start invalidates an already issued collector episode");
    }
}
__attribute__((noinline)) void current_admission_scope_and_freshness() {
    for(int fault=0;fault<16;++fault) {
        auto f=std::make_unique<Fixture>();current_ordinary(*f);auto& o=f->in->offers[0];
        if(fault==0)f->in->offer_count=2;
        if(fault==1)f->in->independent=1;
        if(fault==2)o.plan.organization=Organization::G;
        if(fault==3)o.plan.organization=Organization::H;
        if(fault==4)o.plan.collector=2;
        if(fault==5)o.plan.crew[0]=2;
        if(fault==6)o.plan.crew_count=2;
        if(fault==7)o.plan.ingress.count=1;
        if(fault==8)o.diagnostic_segment=true;
        if(fault==9)o.marked_extra_turns=1;
        if(fault==10)o.schedule.actor_count=2;
        if(fault==11)o.schedule.actors[0].actor_id=2;
        if(fault==12)o.safety_count=2;
        if(fault==13)f->work.active=true;
        if(fault==14) {
            const auto d=decide(*f->in,f->ledger,f->probes,Arm::active);
            check(original_commit(*f->in,f->local,d,f->ledger,f->probes,Arm::active,f->work).episode!=0,"active-episode guard fixture issues an actual component trial");
            f->work.active=false;
        }
        if(fault==15)o.plan.organization=Organization::J;
        int callbacks=0;const auto admitted=admit_current(*f->in,f->ledger,f->probes,Arm::active,f->work,
            [&](const Decision&)->const LocalFrame& {++callbacks;return f->local;});
        check(!admitted.started&&!callbacks&&!admitted.episode,"out-of-scope work is rejected before the local factory");
    }
    for(int fault=0;fault<4;++fault) {
        auto f=std::make_unique<Fixture>();current_ordinary(*f);int callbacks=0;
        const auto admitted=admit_current(*f->in,f->ledger,f->probes,Arm::active,f->work,
            [&](const Decision&)->const LocalFrame& {
                ++callbacks;
                if(fault==0)f->local.admission_only=false;
                if(fault==1)++f->local.frame.clock.revision;
                if(fault==2)++f->local.current_round;
                if(fault==3)f->local.legal.knowledge=p6::Knowledge::stale;
                return f->local;
            });
        check(callbacks==1&&!admitted.started&&!admitted.episode&&!f->ledger.active()&&f->work.generation==0,
            "post-decision local must be admission-only fresh and at exactly the selected clock before issue");
    }
    {
        auto f=std::make_unique<Fixture>();current_ordinary(*f);
        const auto old=decide(*f->in,f->ledger,f->probes,Arm::active);
        f->tick(1);current_ordinary(*f);
        check(!original_commit(*f->in,f->local,old,f->ledger,f->probes,Arm::active,f->work).started,
            "a prior-round proposal remains inadmissible under the original commit");
        int callbacks=0;
        const auto admitted=admit_current(*f->in,f->ledger,f->probes,Arm::active,f->work,
            [&](const Decision& now)->const LocalFrame& {
                ++callbacks;check(now.clock==f->in->frame.clock&&!(now.clock==old.clock),
                    "current admission selects the changed input rather than accepting an old proposal");
                return f->local;
            });
        check(admitted.started&&admitted.episode&&callbacks==1&&f->work.started_clock==f->in->frame.clock,
            "one fresh ordinary decision can admit the new clock after an old proposal was refused");
    }
    auto helper=std::make_unique<Fixture>();auto& in=*helper->in;
    const auto collector=in.frame.own.actor;
    in.frame.own.actor=in.frame.actors[0].value;in.frame.clock.self=2;
    in.frame.actors[0]={collector,in.frame.clock,1,p6::Knowledge::exact};
    in.frame.own.actor.position={0,0};in.offers[0].plan=circuit(Organization::U,2);
    in.offers[0].schedule=campaign(in.frame,false);helper->tick(0);current_ordinary(*helper);
    auto ledger=std::make_unique<EvidenceLedger>(2);
    const auto admitted=admit_current(in,*ledger,helper->probes,Arm::active,helper->work,
        [&](const Decision& now)->const LocalFrame& {check(!now.record_episode,"ordinary helper receives no collector evidence permission");return helper->local;});
    check(admitted.started&&!admitted.episode&&ledger->size()==0&&!ledger->active(),"helper starts ordinary U with its own empty private ledger");
}
__attribute__((noinline)) void current_admission_component_cycles() {
    auto f=std::make_unique<Fixture>();
    const auto first=f->physical_cycle(0,false,true,true,false,true);
    check(!f->ledger.component_qualified(f->in->component_slot,f->component.key,first.renewed),"one current-admitted complete U episode is insufficient component proof");
    const auto second=f->physical_cycle(64,false,true,true,false,true);
    check(f->ledger.component_qualified(f->in->component_slot,f->component.key,second.renewed)&&
        !f->ledger.organization_qualified(f->in->supported_slot,f->supported.key,second.renewed)&&
        !f->ledger.state(f->in->supported_slot)->campaign_started&&f->probes.used==0,
        "two current-admitted actual U capture bank spawn return episodes qualify only the component without diagnostic charge");
    f->reset_cycle(second.renewed+1);f->in->offer_count=2;f->tick(second.renewed+1);
    const auto supported=decide(*f->in,f->ledger,f->probes,Arm::active);
    check(supported.option==1&&supported.record_episode&&!center_qualified(*f->in,f->ledger),
        "component proof permits only the existing unqualified H commissioning rule");
    int callbacks=0;
    check(!admit_current(*f->in,f->ledger,f->probes,Arm::active,f->work,
        [&](const Decision&)->const LocalFrame& {++callbacks;return f->local;}).started&&!callbacks&&
        !f->ledger.active()&&!f->ledger.state(f->in->supported_slot)->campaign_started,
        "ordinary current admission cannot execute H or start a supported campaign from components alone");
}

} // namespace
int main() {
    same_input_admission();
    earned_center_cycles();
    qualification_guards();
    expected_damage_and_new_entry_band();
    actual_resources_and_safe_rollback();
    private_helper_keeps_existing_bag();
    unknown_and_censored_are_not_proof();
    independent_core_and_visible_extension();
    first_lower_ranked_trial_cannot_promise_support();
    conditional_support_requires_an_applicable_first_cycle();
    qualified_proof_cannot_make_diagnostics_free();
    current_admission_matches_old_commit();
    current_admission_scope_and_freshness();
    current_admission_component_cycles();
    std::printf("plan8-earned-center: %d checks passed\n",checks);
}
