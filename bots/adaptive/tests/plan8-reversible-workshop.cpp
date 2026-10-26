#include "../reversible-workshop/src/policy.h"
#include "../reversible-workshop/src/draft.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <memory>

using namespace p8;
using namespace p8::reversible_workshop;
namespace {
int checks=0;
const char* scenario="physical workshop fixture";
void check(bool value,const char* label) {
    ++checks; if (!value) { std::fprintf(stderr,"FAIL [%s]: %s\n",scenario,label); std::exit(1); }
}
// Frozen pre-reuse caller: keep explicit evaluations independent of the current
// Workshop::choose implementation, including its independent-offer comparison.
struct OriginalWorkshop : Workshop {
    Decision choose(const Inputs& in,const EvidenceLedger& ledger,Arm arm) const {
        Decision out; out.clock=in.frame.clock;
        const bool owner=collector_actor(in.frame.own.actor,in.draft);
        if (!own_market(in) || (owner&&!declared)) { out.reason=Rejection::malformed; return out; }
        for (int i=0;i<in.offer_count;++i) {
            const auto& o=in.offers[i]; auto reject=common::legal_offer(in,o);
            if (reject==Rejection::none && !common::crew_legal(in,o,false)) reject=Rejection::unsafe;
            bool record=false;
            if (reject==Rejection::none && (o.diagnostic_segment || o.marked_extra_turns!=0)) reject=Rejection::evidence;
            if (reject==Rejection::none && o.plan.organization==Organization::H) {
                if (arm==Arm::control || (!common::helper_accepts(in,o) && !owner)) reject=Rejection::evidence;
                else if (owner) {
                    const auto* slot=ledger.state(b().context);
                    if (phase!=Phase::B || !(in.market.inventory.value==b().inventory) ||
                        !same_configuration(o,b()) || !common::slot_current(in,ledger,b().context,b().entry) ||
                        !slot || !slot->campaign_started || ledger.suspended(b().context,in.frame.clock.round/common::project_rounds) ||
                        (ledger.campaign_exhausted(b().context) && !ledger.organization_qualified(b().context,b().entry,in.frame.clock.round)))
                        reject=Rejection::evidence;
                    else record=true;
                }
                if (reject==Rejection::none && (!common::package_funded(in,o) ||
                    p6::compare(common::evaluate(in,o.schedule),common::evaluate(in,in.offers[in.independent].schedule))<=0))
                    reject=Rejection::no_gain;
            } else if (reject==Rejection::none && (o.plan.organization==Organization::U || o.plan.organization==Organization::G)) {
                if (owner && rebuilding()&&!rebuild.completed) reject=Rejection::evidence;
                if (owner && phase==Phase::A && same_configuration(o,a()) &&
                    common::slot_current(in,ledger,a().context,a().entry) && !ledger.campaign_exhausted(a().context)) record=true;
            } else if (reject==Rejection::none) reject=Rejection::evidence;
            if (reject==Rejection::none && record &&
                in.frame.own.actor.health<common::minimum_trial_health_fraction*in.frame.own.actor.max_health)
                reject=Rejection::unsafe;
            out.rejected[i]=reject; if (reject!=Rejection::none) continue;
            const auto score=common::evaluate(in,o.schedule);
            if (out.option<0 || p6::compare(score,out.score)>0) {
                out.option=i; out.organization=o.plan.organization; out.reason=Rejection::none;
                out.record_episode=arm==Arm::active&&record; out.score=score;
            }
        }
        return out;
    }
    Decision decide(const Inputs& in,EvidenceLedger& ledger,Arm arm) {refresh(in,ledger);return choose(in,ledger,arm);}
    Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,EvidenceLedger& ledger,
                     Arm arm,Work& work) {
        Admission out;
        if(!local.fresh()||!(local.frame.clock==in.frame.clock)||!(proposed.clock==in.frame.clock)||work.active||(rebuilding()&&!rebuild.completed)) {
            out.rejection=Rejection::stale;return out;
        }
        const auto now=decide(in,ledger,arm);
        if(now.option<0||now.option!=proposed.option||!physical_admissible(in.offers[now.option].plan,local))return out;
        const auto& offer=in.offers[now.option];
        if(now.record_episode) {
            EpisodeRequest request;request.context=offer.context_slot;request.issue_round=in.frame.clock.round;
            request.deadline=request.issue_round+std::min(common::forecast_window,offer.plan.remaining_rounds);
            request.nominal_duration=offer.plan.forecast_rounds;request.action=int(offer.plan.organization);
            request.premises={offer.entry,in.own_state.value,all_premises};
            out.episode=ledger.issue(request);if(!out.episode)return out;
        }
        out.started=work.start(offer.plan,local);out.rejection=out.started?Rejection::none:Rejection::unsafe;
        if(!out.started&&out.episode)ledger.invalidate_active(in.frame.clock.round);
        return out;
    }
};
void same_score(const p6::ScoreEvaluation& actual,const p6::ScoreEvaluation& expected) {
#define SCORE_FIELD(name) check(actual.name==expected.name,"score field: " #name)
    SCORE_FIELD(status); SCORE_FIELD(issue); SCORE_FIELD(own_score); SCORE_FIELD(denial);
    SCORE_FIELD(margin); SCORE_FIELD(min_margin); SCORE_FIELD(nominal_margin);
    SCORE_FIELD(occupancy); SCORE_FIELD(peak_bag); SCORE_FIELD(productive_arrival);
    SCORE_FIELD(target_id); SCORE_FIELD(immediate_legal_win); SCORE_FIELD(terminal_endpoint);
    SCORE_FIELD(terminal_terms); SCORE_FIELD(earned_gold_delta); SCORE_FIELD(kills_delta);
    SCORE_FIELD(source);
#undef SCORE_FIELD
}
void same_decision(const Decision& actual,const Decision& expected) {
#define DECISION_FIELD(name) check(actual.name==expected.name,"decision field: " #name)
    DECISION_FIELD(option); DECISION_FIELD(organization); DECISION_FIELD(reason);
    DECISION_FIELD(clock); DECISION_FIELD(deliberate_probe); DECISION_FIELD(capital_allowed);
    DECISION_FIELD(record_episode); DECISION_FIELD(rejected);
#undef DECISION_FIELD
    same_score(actual.score,expected.score);
}
void same_work(const Work& actual,const Work& expected) {
    const auto& a=actual.plan;const auto& e=expected.plan;
#define PLAN_FIELD(name) check(a.name==e.name,"ordinary retained Plan field: " #name)
    PLAN_FIELD(organization);PLAN_FIELD(group);PLAN_FIELD(collector);PLAN_FIELD(altar);PLAN_FIELD(crew);PLAN_FIELD(pads);
    PLAN_FIELD(crew_count);PLAN_FIELD(forecast_rounds);PLAN_FIELD(remaining_rounds);PLAN_FIELD(capture_window);
    PLAN_FIELD(gem_bearing);PLAN_FIELD(complete_verified);PLAN_FIELD(rally_legal);
#undef PLAN_FIELD
    for(Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
        const auto& ar=route_for(a,leg);const auto& er=route_for(e,leg);
        check(ar.count==er.count&&ar.verified==er.verified&&ar.blocker_assumptions==er.blocker_assumptions,
            "ordinary retained route headers match original commit");
        check(er.count>=0&&er.count<=max_route_tiles,"ordinary reference route payload is bounded");
        for(int i=0;i<er.count;++i)check(ar.tiles[i]==er.tiles[i],"ordinary retained route coordinate matches original commit");
    }
#define WORK_FIELD(name) check(actual.name==expected.name,"ordinary Work field: " #name)
    WORK_FIELD(stage);WORK_FIELD(failure);WORK_FIELD(cursor);WORK_FIELD(last_round);WORK_FIELD(owner_turn);
    WORK_FIELD(phase_started);WORK_FIELD(phase_limit);WORK_FIELD(assembly_turns);WORK_FIELD(hold_turns);
    WORK_FIELD(hold_extensions);WORK_FIELD(bank_queue_turns);WORK_FIELD(active);WORK_FIELD(stop_requested);
    WORK_FIELD(route_witness_active);WORK_FIELD(clearance_seen);WORK_FIELD(payout_seen);WORK_FIELD(bank_seen);
    WORK_FIELD(productive_action_seen);WORK_FIELD(renewal_seen);WORK_FIELD(cycle_completed);WORK_FIELD(helper_released);
    WORK_FIELD(helper_bag_return);WORK_FIELD(tribute_started);WORK_FIELD(pending);WORK_FIELD(release_admitted);
    WORK_FIELD(payout_lower);WORK_FIELD(bank_lower);WORK_FIELD(tribute_starting_bag);
    WORK_FIELD(serial);WORK_FIELD(generation);WORK_FIELD(started_clock);
#undef WORK_FIELD
#define COMMAND_FIELD(name) check(actual.issued.name==expected.issued.name,"ordinary retained command field: " #name)
    COMMAND_FIELD(action);COMMAND_FIELD(stage);COMMAND_FIELD(reason);COMMAND_FIELD(clock);COMMAND_FIELD(target);
    COMMAND_FIELD(group);COMMAND_FIELD(leg);COMMAND_FIELD(token);COMMAND_FIELD(route_hash);
#undef COMMAND_FIELD
}
struct ProbeMarks {
    unsigned count=0;
    static void mark(void* context,unsigned,int) noexcept {
        ++static_cast<ProbeMarks*>(context)->count;
    }
};
Decision compare_choose(const Workshop& workshop,const Inputs& in,const EvidenceLedger& ledger,Arm arm) {
    auto original=std::make_unique<OriginalWorkshop>();
    static_cast<Workshop&>(*original)=workshop;
    const auto expected=original->choose(in,ledger,arm);
    const auto actual=workshop.choose(in,ledger,arm);
    same_decision(actual,expected);
    same_decision(workshop.choose(in,ledger,arm),expected);
    ProbeMarks marks;
    same_decision(workshop.choose(in,ledger,arm,{&marks,ProbeMarks::mark}),expected);
    check(marks.count>0,"enabled synchronous probe preserves the default decision");
    return actual;
}
void compare_both(const Workshop& workshop,const Inputs& in,const EvidenceLedger& ledger) {
    compare_choose(workshop,in,ledger,Arm::active);
    compare_choose(workshop,in,ledger,Arm::control);
}
Decision compare_decide(Workshop& workshop,const Inputs& in,EvidenceLedger& ledger,Arm arm) {
    auto original=std::make_unique<OriginalWorkshop>();
    static_cast<Workshop&>(*original)=workshop;
    auto prior=std::make_unique<EvidenceLedger>(ledger);
    original->refresh(in,*prior);
    const auto expected=original->choose(in,*prior,arm);
    const auto actual=workshop.decide(in,ledger,arm);
    same_decision(actual,expected);
    check(workshop.phase==original->phase && workshop.rebuild_permit==original->rebuild_permit,
        "decide refresh preserves the original phase and reserved rebuild authority");
    compare_both(workshop,in,ledger);
    return actual;
}
p6::ScoreEvaluation sentinel_score() {
    p6::ScoreEvaluation score;
    score.status=p6::Status::feasible; score.issue=p6::Issue::none;
    score.own_score={11,12,13}; score.denial={14,15,16}; score.margin={17,18,19};
    score.min_margin=20; score.nominal_margin=21; score.occupancy=22; score.peak_bag=23;
    score.productive_arrival=24; score.target_id=25;
    score.immediate_legal_win=score.terminal_endpoint=score.terminal_terms=true;
    score.earned_gold_delta=26; score.kills_delta=27; score.source=p6::Source::synthetic_fixture;
    return score;
}
struct LegalityMarks {
    std::array<unsigned,3> points{99,99,99};
    unsigned count=0;
    static void mark(void* context,unsigned point) noexcept {
        auto& marks=*static_cast<LegalityMarks*>(context);
        if (marks.count<marks.points.size()) marks.points[marks.count]=point;
        ++marks.count;
    }
};
void legal_prefix(const LegalityMarks& marks,unsigned reached) {
    check(marks.count==reached,"legality callback emits only the reached stage prefix");
    for (unsigned i=0;i<reached;++i) check(marks.points[i]==i,"legality callback stage IDs preserve reached order");
    for (unsigned i=reached;i<marks.points.size();++i)
        check(marks.points[i]==99,"unreached legality stages leave callback slots untouched");
}
void compare_legal(const Inputs& in,const Offer& offer,Rejection rejection,unsigned reached,bool evaluated) {
    auto default_score=sentinel_score(),hooked_score=sentinel_score();
    const auto expected=common::legal_offer(in,offer,&default_score);
    check(expected==rejection,"default legality reaches the independently expected rejection category");
    LegalityMarks marks;
    check(common::legal_offer(in,offer,&hooked_score,{&marks,LegalityMarks::mark})==expected,
        "enabled legality callback preserves the default rejection category");
    same_score(hooked_score,default_score); legal_prefix(marks,reached);
    if (evaluated) same_score(default_score,common::evaluate(in,offer.schedule));
    else same_score(default_score,p6::ScoreEvaluation{});
    check(common::legal_offer(in,offer)==expected,"omitted output and callback preserve legality");
    LegalityMarks without_output;
    check(common::legal_offer(in,offer,nullptr,{&without_output,LegalityMarks::mark})==expected,
        "legality callback is independent of the optional score output");
    legal_prefix(without_output,reached);
    hooked_score=sentinel_score(); marks={};
    check(common::legal_offer(in,offer,&hooked_score,{&marks,LegalityMarks::mark})==expected,
        "repeated legality call does not retain an earlier callback or sentinel result");
    same_score(hooked_score,default_score); legal_prefix(marks,reached);
}
template<class T> p6::Fact<T> fact(T value) { return {value,p6::Source::synthetic_fixture}; }
Route path(std::initializer_list<p6::Point> tiles) {
    Route r; r.verified=true; r.count=int(tiles.size()); int i=0;
    for (auto p:tiles) r.tiles[i++]=p; return r;
}
Plan circuit(bool supported) {
    Plan p; p.organization=supported?Organization::H:Organization::U;
    p.group=supported?8:7; p.collector=1; p.altar=supported?5:4;
    p.crew[0]=1; p.crew_count=1;
    p.ingress=supported?path({{0,0},{1,1},{2,1}}):path({{0,0},{1,0},{2,0}});
    p.capture=supported?path({{2,1},{3,1}}):path({{2,0},{3,0}});
    p.bank=supported?path({{3,1},{4,1}}):path({{3,0},{4,0}});
    p.renewal=supported?path({{4,1},{4,0},{3,0},{2,0},{1,0},{0,0}}):
        path({{4,0},{4,1},{3,1},{2,1},{1,1},{0,0}});
    p.pads[0]=supported?p6::Point{2,1}:p6::Point{2,0}; p.pads[1]={2,2};
    if (supported) { p.crew[1]=2; p.crew_count=2; }
    p.forecast_rounds=18; p.remaining_rounds=100; p.capture_window=5;
    p.complete_verified=p.gem_bearing=p.rally_legal=true; return p;
}
void event(p6::ActorSchedule& a,p6::EventKind kind,int id,int tick,int production=-1) {
    p6::Event e; e.kind=kind; e.event_id=id; e.tick=tick; e.production_index=production;
    e.gold_cost=fact<p6::Gems>(0); e.waypoint_id=kind==p6::EventKind::renewal?90+a.actor_id:-1;
    e.source=p6::Source::synthetic_fixture; a.events[a.event_count++]=e;
}
p6::Schedule schedule(const p6::Frame& f,bool supported) {
    p6::Schedule s; s.actor_count=3; s.target_id=supported?8:7;
    for (int i=0;i<3;++i) {
        auto& a=s.actors[i]; a.actor_id=i+1; const auto* seen=f.visible(i+1); const bool own=i+1==f.clock.self;
        a.initial_bag={seen?seen->bag:0,own?p6::Source::self_observed:p6::Source::visible_subject};
        a.initial_gold={own?f.own.gold:0,own?p6::Source::self_observed:p6::Source::conservative};
        a.occupied_turns=fact<p6::Gems>(supported&&i==0?24:20);
        const int amount=i==0?(supported?24:12):supported&&i==1?0:10;
        if (amount) {
            const int pi=s.production_count++; auto& p=s.productions[pi];
            p.event_id=1000+(i==0?s.target_id:20+i); p.tick=12;
            p.gems=fact<p6::Gems>(amount); p.gold=fact<p6::Gems>(0);
            p.holder_count=1; p.holders[0]=i+1; p.source=p6::Source::synthetic_fixture;
            event(a,p6::EventKind::production,p.event_id,12,pi); event(a,p6::EventKind::tribute,2000+i,15);
        } else if (a.initial_bag.value) event(a,p6::EventKind::tribute,2000+i,15);
        event(a,p6::EventKind::renewal,3000+i,int(a.occupied_turns.value));
    }
    return s;
}
p6::CycleCertificate cert(const p6::Frame& f,int id,int group) {
    p6::CycleCertificate c; c.clock=f.clock; c.actor=id; c.group=group;
    const auto* a=f.visible(id);
    if (a) c.clear=c.hold=c.bank=c.recovery={a->health,a->max_health,10,0,0,0,true};
    c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true; return c;
}
struct Fixture {
    Inputs in{};
    EvidenceLedger ledger{1};
    Workshop workshop{};
    LocalFrame local{};
    Work work{};
    CycleTrace trace{};
    ContextDeclaration da{},db{};
    Inventory gear_a{},gear_b{};
    explicit Fixture(bool low_trial_health=false) {
        gear_a.count[damage_item]=1; gear_b.count[survival_item]=1;
        auto& f=in.frame; f.clock={0,1,0}; f.own.gold=15;
        for (int id=1;id<=3;++id) {
            p6::Actor a; a.id=id; a.pick_ordinal=id-1; a.team=p6::Team::ally;
            a.type=id==1?p6::Champion::tavi:id==2?p6::Champion::varg:p6::Champion::marcella;
            a.health=a.max_health=100; a.strength=id==2?48:20;
            a.alive=a.can_move=a.can_attack=true; a.position=id==1?p6::Point{0,0}:p6::Point{2,id};
            if (id==1) f.own.actor=a; else f.actors[f.actor_count++]={a,f.clock,id,p6::Knowledge::exact};
        }
        f.own.at_spawn=true;
        p6::Group g; g.id=7; g.lifecycle=p6::Lifecycle::living;
        g.original_spawn_visible=g.all_spawns_visible=g.uncontested_now=true;
        f.group={g,f.clock,7,p6::Knowledge::exact};
        in.settings={2000,1000,5}; in.settings_identity={17,p6::Source::queried_setting};
        in.draft.champions={p6::Champion::tavi,p6::Champion::varg,p6::Champion::marcella,p6::Champion::other,p6::Champion::other};
        in.draft.actors={1,2,3,-1,-1}; in.draft.picks=in.picks=3;
        in.target_group=8; in.offer_count=2; in.independent=0;
        in.own_state={{100,100,20,10,1,0,gear_a.hash()},f.clock,1,p6::Knowledge::exact};
        in.threat_regime={0,f.clock,1,p6::Knowledge::exact};
        in.market.inventory={gear_a,f.clock,1,p6::Knowledge::exact};
        in.offers[0].plan=circuit(false); in.offers[1].plan=circuit(true);
        da.key.settings_id=17; da.key.actor_id=1; da.key.champion=int(p6::Champion::tavi);
        da.key.source_group=7; da.key.route_template=0; da.key.endpoint=4; da.key.threat_regime=0; da.key.role=0;
        da.key.participants[0]=1; da.key.participant_count=1; da.key.route_hash=plan_route_hash(in.offers[0].plan);
        da.key.build={gear_a.hash(),100,20,10,1};
        da.envelope={{75,100},{100,100},{20,20},{10,10},{1,1},{0,100},0}; da.require_spawn_shop=true;
        for (int h=0;h<3;++h) da.hypotheses[h].offset={h*2,h*2+2};
        db=da; db.kind=EpisodeKind::full_organization; db.require_supported_work=true;
        db.key.source_group=8; db.key.route_template=1; db.key.endpoint=5; db.key.role=1;
        db.key.participant_count=2; db.key.participants[1]=2;
        db.key.route_hash=plan_route_hash(in.offers[1].plan); db.key.build={gear_b.hash(),120,16,10,1};
        db.envelope={{90,120},{120,120},{16,16},{10,10},{1,1},{0,100},0};
        if (low_trial_health) {
            const double first_positive=std::nextafter(0.,std::numeric_limits<double>::infinity());
            da.key.hp_band=db.key.hp_band=HealthBand::below_half;
            da.envelope.health={first_positive,50}; db.envelope.health={first_positive,60};
            f.own.actor.health=34;
        }
        in.component_slot=ledger.predeclare(da); in.supported_slot=ledger.predeclare(db);
        check(in.component_slot>=0&&in.supported_slot>=0,"complete fixed A/B contexts predeclare before Workshop binding");
        in.component_entry=in.offers[0].entry=da.key; in.supported_entry=in.offers[1].entry=db.key;
        in.offers[0].context_slot=in.component_slot; in.offers[1].context_slot=in.supported_slot;
        Configuration a{gear_a,da.key,in.offers[0].plan,in.component_slot,{60,p6::Source::public_map}};
        Configuration b{gear_b,db.key,in.offers[1].plan,in.supported_slot,{80,p6::Source::public_map}};
        check(workshop.declare(a,b,ledger),"exact two actual inventories and U/H geographies declare before work");
        for (auto& q:in.market.items) {
            q.price={30,p6::Source::queried_setting}; q.purchasable={true,p6::Source::queried_setting};
        }
        tick(0);
    }
    void tick(int round,int revision=0) {
        auto& f=in.frame; f.clock={round,1,revision}; f.group.clock=f.clock;
        for (int i=0;i<f.actor_count;++i) f.actors[i].clock=f.clock;
        auto& n=in.own_state.value; n.health=f.own.actor.health; n.max_health=f.own.actor.max_health;
        n.strength=f.own.actor.strength; n.bag=f.own.actor.bag; n.equipment_hash=in.market.inventory.value.hash();
        in.own_state.clock=in.threat_regime.clock=in.market.inventory.clock=f.clock;
        in.market.spawn_shop_legal={f.own.at_spawn,f.clock,1,p6::Knowledge::exact};
        in.market.sell_legal={f.own.at_spawn,f.clock,1,p6::Knowledge::exact};
        for (auto& q:in.market.items) q.buy_legal={f.own.at_spawn&&q.price.value<=f.own.gold,f.clock,1,p6::Knowledge::exact};
        in.windows.valid=true; in.windows.source=p6::Source::synthetic_fixture;
        in.windows.deadlines={2000-round,78,39}; in.windows.windows={120,78,39};
        in.windows.sampled_actor_id=1; in.windows.sampled_round=round;
        for (int k=0;k<2;++k) {
            auto& o=in.offers[k]; o.schedule=schedule(f,k==1); o.safety_count=3;
            for (int id=1;id<=3;++id) o.safety[id-1]=cert(f,id,common::participant(o.plan,id)?o.plan.group:9);
        }
        local.frame=f; local.current_round=round; local.milestones={}; local.safety_count=3;
        const auto& p=work.active?work.plan:in.offers[workshop.phase==Phase::B?1:0].plan;
        for (int id=1;id<=3;++id) local.safety[id-1]=cert(f,id,p.group);
        local.legal={{},f.clock,1,p6::Knowledge::exact}; auto& legal=local.legal.value;
        legal.safe_now=legal.attack_legal=legal.altar_legal=legal.recall_safe=legal.renewal_productive=true;
        for (Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
            auto& access=legal.routes[int(leg)]; const auto& r=route_for(p,leg);
            access.assumptions_hold=route_valid(r); access.next_move_legal=true;
            for (int i=0;i<r.count;++i) if (r.tiles[i]==f.own.actor.position) { access.next_tile=r.tiles[std::min(i+1,r.count-1)]; break; }
        }
    }
    Resolution physical(int start,bool supported) {
        // Each fixture job owns a fresh executor. The actor-private ledger,
        // transaction cap and monotonically allocated trace receipts persist.
        // Reusing a dormant Work across unobserved inter-job rounds would
        // correctly trigger its stale/fuel gap protection.
        work={};
        auto& f=in.frame; f.own.actor.position={0,0}; f.own.actor.bag=0; f.own.at_spawn=true;
        f.own.actor.channel=p6::Channel::none; f.own.actor.spawn_group=-1;
        f.group.value.id=supported?8:7; f.group.subject=f.group.value.id; f.group.value.lifecycle=p6::Lifecycle::living;
        for (int i=0;i<f.actor_count;++i) {
            auto& actor=f.actors[i].value; actor.action=p6::WitnessAction::none; actor.spawn_group=-1;
            // New personally visible start: the helper has returned from its
            // previous release during the nonoverlapping inter-job interval.
            actor.position={2,actor.id};
        }
        tick(start);
        const auto d=compare_decide(workshop,in,ledger,Arm::active);
        if (d.option!=(supported?1:0)) std::fprintf(stderr,"physical start=%d B=%d option=%d reject0=%d reject1=%d phase=%d\n",start,int(supported),d.option,int(d.rejected[0]),int(d.rejected[1]),int(workshop.phase));
        check(d.option==(supported?1:0),"current actual configuration chooses its real U or supported H duties");
        for (int id=1;id<=3;++id) local.safety[id-1]=cert(f,id,in.offers[d.option].plan.group);
        const auto prior_command=work.serial;
        const auto admission=workshop.commit(in,local,d,ledger,Arm::active,work);
        check(admission.started&&bool(admission.episode)==d.record_episode&&work.serial==prior_command,"own episode when eligible is issued before first physical command");
        if (d.record_episode) check(trace.begin(ledger,work),"physical receipt trace binds newly issued actual Work");
        const int amount=supported?24:12;
        const int attacks=supported?2:int(std::ceil(60/f.own.actor.strength)); const int shift=attacks-3;
        const auto p=work.plan;
        auto step=[&](int offset,p6::Point pos,bool payout=false,bool bank=false) {
            f.own.actor.position=pos; f.own.at_spawn=pos==p6::Point{0,0};
            f.own.actor.spawn_group=pos==p.capture.tiles[p.capture.count-1]?p.group:-1;
            if (supported&&offset==2) { f.actors[0].value.action=p6::WitnessAction::useful_attack; f.actors[0].value.action_group=8; }
            else f.actors[0].value.action=p6::WitnessAction::none;
            if (supported&&offset>=5&&offset<7) { f.actors[0].value.position={3,2}; f.actors[0].value.spawn_group=8; }
            if (supported&&offset>=7) { f.actors[0].value.position={4,2}; f.actors[0].value.spawn_group=-1; }
            if (payout) { f.own.actor.bag=amount; f.group.value.lifecycle=p6::Lifecycle::respawning; }
            if (bank) { f.own.actor.bag=0; f.own.actor.channel=p6::Channel::none; }
            tick(start+offset);
            if (payout||bank) local.milestones={{p.group,payout,bank,payout?amount:0,bank?amount:0},f.clock,1,p6::Knowledge::exact};
            const auto c=workshop.advance(in,local,ledger,work);
            check(work.confirm(c,{c.clock,c.clock,c.token,c.action,true,true}),"actual physical action confirmation consumes exact issued token");
            if (d.record_episode) trace.observe(local,work,in.market.spawn_shop_legal); return c;
        };
        const auto first=step(0,p.ingress.tiles[0]);
        if (first.action!=PhysicalAction::move || !(first.target==p.ingress.tiles[1]))
            std::fprintf(stderr,"first move start=%d B=%d action=%d target=%d,%d expected=%d,%d stage=%d failure=%d stop=%d\n",
                start,int(supported),int(first.action),first.target.x,first.target.y,p.ingress.tiles[1].x,p.ingress.tiles[1].y,
                int(work.stage),int(work.failure),int(work.stop_requested));
        check(first.action==PhysicalAction::move&&first.target==p.ingress.tiles[1],"configuration follows its own first retained route tile");
        step(1,p.ingress.tiles[1]);
        const auto attack=step(2,p.ingress.tiles[2]);
        if (attack.action!=PhysicalAction::attack)
            std::fprintf(stderr,"first attack start=%d B=%d action=%d stage=%d failure=%d helper=%d,%d pad=%d,%d\n",
                start,int(supported),int(attack.action),int(work.stage),int(work.failure),f.actors[0].value.position.x,
                f.actors[0].value.position.y,p.pads[1].x,p.pads[1].y);
        check(attack.action==PhysicalAction::attack,"actual camp action begins the changed work");
        check(step(3,p.ingress.tiles[2]).action==PhysicalAction::attack,"current work performs its second real attack");
        for (int a=2;a<attacks;++a) check(step(2+a,p.ingress.tiles[2]).action==PhysicalAction::attack,"actual current damage determines remaining solo clear attacks");
        f.group.value.lifecycle=p6::Lifecycle::reward_available;
        step(5+shift,p.capture.tiles[0]);
        for (int r=6+shift;r<=11+shift;++r) check(step(r,p.capture.tiles[1]).action==PhysicalAction::hold,"capture waits on own actual payout");
        step(12+shift,p.capture.tiles[1],true);
        check(step(13+shift,p.bank.tiles[1]).action==PhysicalAction::tribute,"actual holder starts its own tribute");
        const int ticks=(amount+9)/10;
        for (int r=14+shift;r<13+shift+ticks;++r) {
            f.own.actor.channel=p6::Channel::tribute; f.own.actor.channel_valid=true;
            check(step(r,p.bank.tiles[1]).action==PhysicalAction::preserve_channel,"existing valid channel survives role policy");
        }
        const int bank=13+shift+ticks; step(bank,p.bank.tiles[1],false,true);
        for (int i=1;i<p.renewal.count;++i) step(bank+i,p.renewal.tiles[i]);
        check(work.cycle_completed&&!work.active&&work.bank_seen&&work.renewal_seen,"actual capture bank spawn and productive return complete");
        if (!d.record_episode) { check(!ledger.active(),"temporary actual U income creates no new configuration proof"); return {}; }
        const auto result=trace.finish(ledger,f.clock.round,{supported?db.key:da.key,in.own_state.value,all_premises});
        check(result.accepted&&result.full_positive,"only complete own physical receipts supply one positive episode");
        workshop.observe_result(result,ledger); return result;
    }
    void qualify() {
        physical(0,false); check(!ledger.component_qualified(in.component_slot,da.key,20),"one actual A cycle cannot license gear rebuild");
        physical(40,false); check(ledger.component_qualified(in.component_slot,da.key,60),"two actual unchanged-gear A cycles license only component");
    }
    RebuildComparison comparison() const { return {in.offers[1].schedule,in.offers[0].schedule}; }
    bool forward(int round=61) { tick(round); return workshop.begin_forward(in,local,ledger,Arm::active,work,comparison()); }
    TransactionReceipt execute(const TransactionCommand& c) {
        check(c.valid,"typed inventory transaction is present before dispatch");
        const auto oldscore=in.frame.own_team_score;
        in.market.inventory.value=c.after; in.frame.own.gold=c.kind==TransactionKind::sell?c.gold_before+c.amount:c.gold_before-c.amount;
        auto& a=in.frame.own.actor;
        const bool b=c.after==gear_b; a.max_health=b?120:100; a.strength=c.after==gear_a?20:16;
        a.health=std::min(a.health,a.max_health);
        if (c.kind==TransactionKind::buy&&c.item==survival_item) a.health=a.max_health;
        tick(c.clock.round,c.clock.revision+1);
        check(in.frame.own_team_score==oldscore,"refund and purchase do not create tributed or earned-score income");
        return {c.clock,in.frame.clock,c.token,c.kind,c.item,true,true};
    }
    void complete_forward() {
        check(forward(),"qualified owner reserves one fully present-funded reversible rebuild at safe spawn");
        const auto sold=workshop.transaction(in,local,ledger,work);
        check(sold.kind==TransactionKind::sell&&sold.item==damage_item&&sold.amount==30,"actual Tavi sells actual D shard at queried full refund");
        const auto sr=execute(sold); check(workshop.confirm(sold,sr,in,ledger),"fresh full inventory/cash/stat requery confirms actual sale");
        check(!ledger.component_qualified(in.component_slot,da.key,in.frame.clock.round),"actual sale expires old exact build without deleting reserved permission");
        check(workshop.rebuild.latest_stats.strength==16&&in.frame.own.gold==45,"real post-sale stats and refunded personal cash replace old state");
        tick(62);
        const auto bought=workshop.transaction(in,local,ledger,work);
        check(bought.kind==TransactionKind::buy&&bought.item==survival_item&&bought.amount==30,"next action buys different S item with presently queried funds");
        const auto br=execute(bought); check(workshop.confirm(bought,br,in,ledger),"own purchase receipt confirms exact debit and changed inventory");
        check(workshop.rebuild.completed&&workshop.rebuild.latest_stats.max_health==120&&workshop.rebuild.latest_stats.health==120,
            "actual Health purchase at spawn refreshes exact max health and observed healing");
    }
};

struct OrdinaryFixture {
    Inputs in{};
    EvidenceLedger ledger{2};
    Workshop workshop{};
    LocalFrame local{};
    Work work{};
    ContextDeclaration declaration{};
    OrdinaryFixture() {
        auto seed=std::make_unique<Fixture>();in=seed->in;declaration=seed->da;
        auto& f=in.frame;const auto collector=f.own.actor;f.own.actor=f.actors[0].value;
        f.own.actor.position={0,0};f.own.actor.bag=0;f.clock={0,2,0};
        f.actors[0].value=collector;f.actors[0].subject=collector.id;
        for(int i=0;i<f.actor_count;++i)f.actors[i].clock=f.clock;
        f.group.clock=f.clock;in.windows.sampled_actor_id=2;
        in.own_state={{100,100,48,10,1,0,in.market.inventory.value.hash()},f.clock,2,p6::Knowledge::exact};
        in.threat_regime.clock=f.clock;in.threat_regime.subject=2;
        in.market.inventory.clock=f.clock;in.market.inventory.subject=2;
        in.offer_count=1;in.independent=0;in.component_slot=in.supported_slot=-1;
        in.component_entry=in.supported_entry={};
        auto& offer=in.offers[0];offer.context_slot=-1;offer.entry={};
        offer.plan.collector=offer.plan.crew[0]=2;
        auto scheduled=std::make_unique<p6::Schedule>();scheduled->actor_count=scheduled->production_count=1;
        scheduled->target_id=offer.plan.group;auto& actor=scheduled->actors[0];actor.actor_id=2;
        actor.initial_bag={0,p6::Source::self_observed};actor.initial_gold={f.own.gold,p6::Source::self_observed};
        actor.occupied_turns=fact<p6::Gems>(20);
        event(actor,p6::EventKind::production,1007,12,0);event(actor,p6::EventKind::tribute,2002,15);
        event(actor,p6::EventKind::renewal,3002,20);
        auto& production=scheduled->productions[0];production.event_id=1007;production.tick=12;
        production.gems=fact<p6::Gems>(10);production.gold=fact<p6::Gems>(0);
        production.holder_count=1;production.holders[0]=2;production.source=p6::Source::synthetic_fixture;
        offer.schedule=*scheduled;offer.safety_count=1;offer.safety[0]=cert(f,2,offer.plan.group);
        local.frame=f;local.current_round=f.clock.round;local.admission_only=true;
        local.action_reserve=8000;local.safety_count=1;local.safety[0]=offer.safety[0];
        local.legal={{},f.clock,2,p6::Knowledge::exact};local.legal.value.safe_now=true;
        declaration.key.actor_id=2;declaration.key.champion=int(p6::Champion::varg);declaration.key.role=1;
        declaration.key.participants[0]=2;declaration.key.build.strength=48;declaration.envelope.strength={48,48};
    }
    void begin_existing_episode() {
        const int slot=ledger.predeclare(declaration);check(slot>=0,"ordinary rejection fixture predeclares an exact own component");
        EpisodeRequest request;request.context=slot;request.issue_round=in.frame.clock.round;
        request.deadline=request.issue_round+100;request.nominal_duration=20;request.action=int(Organization::U);
        request.premises={declaration.key,in.own_state.value,all_premises};
        check(ledger.issue(request)!=0,"ordinary rejection fixture retains an actual active private episode");
    }
};

__attribute__((noinline)) void ordinary_current_admission_reference() {
    scenario="ordinary current admission: original fresh commit and lifecycle";
    for(int variant=0;variant<4;++variant) {
        auto f=std::make_unique<OrdinaryFixture>();
        const Arm arm=variant==1?Arm::control:Arm::active;
        if(variant==2)f->work.generation=std::numeric_limits<std::uint64_t>::max();
        if(variant==3)f->work.last_round=f->in.frame.clock.round;
        auto original=std::make_unique<OriginalWorkshop>();static_cast<Workshop&>(*original)=f->workshop;
        auto old_ledger=std::make_unique<EvidenceLedger>(f->ledger);auto old_work=std::make_unique<Work>(f->work);
        const auto proposed=original->decide(f->in,*old_ledger,arm);
        check(proposed.option==0&&!proposed.record_episode&&!proposed.deliberate_probe&&!proposed.capital_allowed,
            "fresh original policy chooses ordinary noncollector U without experimental permission");
        const auto expected=original->commit(f->in,f->local,proposed,*old_ledger,arm,*old_work);
        int calls=0;
        const auto actual=f->workshop.admit_ordinary_current(f->in,f->ledger,arm,f->work,
            [&](const Decision& current)->const LocalFrame& {++calls;same_decision(current,proposed);return f->local;});
        check(calls==1&&actual.started==expected.started&&actual.episode==expected.episode&&actual.rejection==expected.rejection,
            "current ordinary admission matches original fresh commit and observes once");
        same_work(f->work,*old_work);
        check(!actual.episode&&!f->ledger.active()&&!old_ledger->active()&&f->ledger.size()==0,
            "ordinary admission never issues or predeclares experimental evidence");
        if(variant<2) {
            check(actual.started,"active and control ordinary producers start their complete current U plan");
            check(f->work.consume_admission_turn(f->local)&&old_work->consume_admission_turn(f->local),
                "both lifecycles charge exactly the explicit admission owner turn");
            same_work(f->work,*old_work);
            check(!f->work.consume_admission_turn(f->local)&&!old_work->consume_admission_turn(f->local),
                "the charged admission turn cannot be consumed twice");
        } else check(!actual.started&&actual.rejection==Rejection::unsafe,"generation and same-turn lifecycle guards remain binding");
    }
}
__attribute__((noinline)) void ordinary_current_admission_preconditions() {
    scenario="ordinary current admission: old proposals cannot authorize changed inputs";
    for(int fault=0;fault<27;++fault) {
        auto f=std::make_unique<OrdinaryFixture>();
        const auto old=compare_choose(f->workshop,f->in,f->ledger,Arm::active);
        check(old.option==0,"each rejection case starts from an independently admitted policy proposal");
        auto& offer=f->in.offers[0];
        switch(fault) {
        case 0:f->workshop.declared=true;break;
        case 1:f->workshop.phase=Phase::B;break;
        case 2:f->in.frame.own.actor.pick_ordinal=0;f->in.draft.actors[0]=2;f->in.draft.champions[0]=p6::Champion::varg;
            f->in.draft.actors[1]=1;f->in.draft.champions[1]=p6::Champion::tavi;f->in.frame.actors[0].value.pick_ordinal=1;break;
        case 3:f->work.active=true;break;
        case 4:f->begin_existing_episode();break;
        case 5:f->in.offer_count=2;break;
        case 6:f->in.independent=1;break;
        case 7:offer.plan.organization=Organization::G;break;
        case 8:offer.plan.organization=Organization::H;break;
        case 9:offer.plan.crew_count=2;offer.plan.crew[1]=1;break;
        case 10:offer.plan.collector=1;break;
        case 11:offer.plan.crew[0]=1;break;
        case 12:offer.plan.ingress.count=1;break;
        case 13:offer.safety_count=0;break;
        case 14:offer.diagnostic_segment=true;break;
        case 15:offer.marked_extra_turns=1;break;
        case 16:offer.marked_extra_turns=-1;break;
        case 17:++offer.schedule.actors[0].initial_gold.value;break;
        case 18:++offer.safety[0].clock.revision;break;
        case 19:++f->in.market.inventory.clock.revision;break;
        case 20:f->in.settings_identity.source=p6::Source::unknown;break;
        case 21:f->in.frame.own.actor.team=p6::Team::enemy;break;
        case 22:offer.schedule.actors[0].events[0].source=p6::Source::unknown;break;
        case 23:offer.capital_cost=f->in.frame.own.gold+1;break;
        case 24:offer.safety[0].solo=false;break;
        case 25:offer.plan.ingress.verified=false;break;
        case 26:offer.safety_count=2;break;
        }
        auto before=std::make_unique<Work>(f->work);
        const auto* active=f->ledger.active();const auto serial=active?active->serial:0;
        int calls=0;
        const auto result=f->workshop.admit_ordinary_current(f->in,f->ledger,Arm::active,f->work,
            [&](const Decision&)->const LocalFrame& {++calls;return f->local;});
        check(!result.started&&!result.episode&&result.rejection!=Rejection::none&&calls==0,
            "changed role, menu, evidence, resources and policy gates reject before observing physical permission");
        same_work(f->work,*before);
        check(bool(f->ledger.active())==bool(active)&&(!active||f->ledger.active()->serial==serial),
            "ordinary precondition failure preserves every existing active episode");
    }
}
__attribute__((noinline)) void ordinary_current_admission_local_rejections() {
    scenario="ordinary current admission: fresh physical observation and no episode";
    for(int fault=0;fault<15;++fault) {
        auto f=std::make_unique<OrdinaryFixture>();auto before=std::make_unique<Work>(f->work);int calls=0;
        const auto result=f->workshop.admit_ordinary_current(f->in,f->ledger,Arm::active,f->work,
            [&](const Decision& current)->const LocalFrame& {
                ++calls;check(current.option==0&&!current.record_episode&&!current.deliberate_probe&&!current.capital_allowed,
                    "physical observer receives only current ordinary policy authority");
                switch(fault) {
                case 0:f->local.admission_only=false;break;
                case 1:++f->local.current_round;break;
                case 2:++f->local.legal.clock.revision;break;
                case 3: {
                    ++f->local.frame.clock.revision;const auto clock=f->local.frame.clock;
                    f->local.legal.clock=f->local.frame.group.clock=f->local.safety[0].clock=clock;
                    for(int i=0;i<f->local.frame.actor_count;++i)f->local.frame.actors[i].clock=clock;
                    check(f->local.fresh(),"different-clock physical frame is otherwise fresh before exact-clock rejection");break;
                }
                case 4:f->local.frame.clock.self=1;break;
                case 5:f->local.fuel_resumed=true;break;
                case 6:f->local.legal.value.safe_now=false;break;
                case 7:f->local.safety_count=0;break;
                case 8:++f->local.safety[0].clock.revision;break;
                case 9:f->local.safety[0].solo=false;break;
                case 10:f->local.frame.own.actor.position={1,0};break;
                case 11:f->local.fuel_remaining=f->local.action_reserve-1;break;
                case 12:f->local.frame.own.actor.alive=false;break;
                case 13:f->local.frame.own.actor.channel=p6::Channel::tribute;break;
                case 14:f->work.active=true;break;
                }
                return f->local;
            });
        check(calls==1&&!result.started&&!result.episode&&!f->ledger.active()&&f->ledger.size()==0,
            "stale, unsafe and changed lifecycle observations cannot start work or publish evidence");
        if(fault==14)check(f->work.active&&f->work.generation==before->generation,"intervening active work is preserved rather than restarted");
        else same_work(f->work,*before);
    }
}

__attribute__((noinline)) void legal_offer_probe_equivalence() {
    static constexpr std::array<const char*,10> scenarios{
        "legality probe: structural early return", "legality probe: invalid plan geometry",
        "legality probe: actor-set mismatch", "legality probe: resource mismatch",
        "legality probe: missing certificate", "legality probe: invalid certificate",
        "legality probe: present capital gate", "legality probe: unresolved evaluation",
        "legality probe: invalid evaluation", "legality probe: successful evaluation"};
    for (int fault=0;fault<int(scenarios.size());++fault) {
        scenario=scenarios[fault];
        auto f=std::make_unique<Fixture>(); auto& offer=f->in.offers[1];
        Rejection rejection=Rejection::none; unsigned reached=3; bool evaluated=true;
        switch (fault) {
        case 0: offer.safety_count=0; rejection=Rejection::malformed; reached=0; evaluated=false; break;
        case 1: offer.plan.ingress.verified=false; rejection=Rejection::malformed; reached=0; evaluated=false; break;
        case 2: --offer.schedule.actor_count; rejection=Rejection::malformed; reached=1; evaluated=false; break;
        case 3: ++offer.schedule.actors[0].initial_gold.value;
            rejection=Rejection::resources; reached=2; evaluated=false; break;
        case 4: offer.safety[0].actor=99; rejection=Rejection::unsafe; reached=2; evaluated=false; break;
        case 5: ++offer.safety[0].clock.revision; rejection=Rejection::unsafe; reached=2; evaluated=false; break;
        case 6: offer.capital_cost=f->in.frame.own.gold+1;
            rejection=Rejection::resources; reached=2; evaluated=false; break;
        case 7: offer.schedule.actors[0].events[0].source=p6::Source::unknown;
            rejection=Rejection::malformed; break;
        case 8: offer.schedule.actors[0].events[1].event_id=offer.schedule.actors[0].events[0].event_id;
            rejection=Rejection::malformed; break;
        default: break;
        }
        if (evaluated) check(common::evaluate(f->in,offer.schedule).eligible()==(fault==9),
            "fresh schedule evaluator independently distinguishes ineligible and successful fixtures");
        compare_legal(f->in,offer,rejection,reached,evaluated);
    }
    scenario="legality probe: changed and restored input on the same offer";
    auto f=std::make_unique<Fixture>(); auto& offer=f->in.offers[1];
    compare_legal(f->in,offer,Rejection::none,3,true);
    ++offer.schedule.actors[0].initial_gold.value;
    compare_legal(f->in,offer,Rejection::resources,2,false);
    --offer.schedule.actors[0].initial_gold.value;
    compare_legal(f->in,offer,Rejection::none,3,true);
}

bool original_inventory_valid(const Inventory& inventory) {
    int total=0;
    for (int i=0;i<item_types;++i) {
        const int n=inventory.count[i];
        if (n<0 || n>20 || (i%2==0 && n>4)) return false;
        total+=n;
    }
    return total<=40;
}
std::uint64_t original_inventory_hash(const Inventory& inventory) {
    if (!original_inventory_valid(inventory)) return 0;
    std::uint64_t h=1469598103934665603ULL;
    for (int i=0;i<item_types;++i) { h=hash_word(h,i); h=hash_word(h,inventory.count[i]); }
    return h;
}
void compare_inventory(const Inventory& inventory) {
    check(inventory.valid()==original_inventory_valid(inventory),"inventory validity matches the original complete bounds");
    const auto expected=original_inventory_hash(inventory);
    check(inventory.hash()==expected,"all 64 inventory hash bits match the original eight-byte word loop");
    check(inventory.hash()==expected,"a repeated inventory hash preserves the same complete current value");
}
__attribute__((noinline)) void inventory_hash_original_oracle() {
    scenario="inventory hash: original complete validation and hash words";
    Inventory inventory;
    compare_inventory(inventory);
    for (int index=0;index<item_types;++index) {
        for (int count=-1;count<=21;++count) {
            inventory.count[index]=count;
            compare_inventory(inventory);
            check(inventory.valid()==(count>=0&&count<=20&&(index%2!=0||count<=4)),
                "every single index independently enforces negative, count and even-item bounds");
        }
        for (int count:{std::numeric_limits<int>::min(),std::numeric_limits<int>::max()}) {
            inventory.count[index]=count; compare_inventory(inventory);
            check(inventory.hash()==0,"extreme invalid words are rejected before hashing");
        }
        inventory.count[index]=0; compare_inventory(inventory);
    }
    inventory.count[1]=inventory.count[3]=20;
    compare_inventory(inventory); check(inventory.valid(),"exact total40 remains a valid complete inventory");
    inventory.count[5]=1;
    compare_inventory(inventory); check(!inventory.valid()&&inventory.hash()==0,"total41 preserves the original zero-hash rejection");
    inventory.count[5]=0; compare_inventory(inventory);

    inventory={};
    for (int i=0;i<item_types;++i) inventory.count[i]=i%3==0?2:i%3==1?1:0;
    compare_inventory(inventory); check(!inventory.valid(),"all42 mixed slots retain their total42 rejection");
    inventory.count[0]=0;
    compare_inventory(inventory); check(inventory.valid(),"mixed counts across all42 slots return to exact total40");
    const auto mixed=inventory;
    const auto retained=original_inventory_hash(mixed);
    inventory.count[40]=5; compare_inventory(inventory);
    check(inventory.hash()==0,"a reused mixed inventory still rejects an invalid even-item count");
    inventory=mixed; compare_inventory(inventory);
    inventory.count[41]=std::numeric_limits<int>::max(); compare_inventory(inventory);
    inventory=mixed; --inventory.count[1]; ++inventory.count[2]; compare_inventory(inventory);
    inventory=mixed; compare_inventory(inventory);
    check(inventory.hash()==retained,"restored dirty inventory recovers the original exact ordered hash");
}

void independent_menu(Fixture& f) {
    const auto first=f.in.offers[0];
    f.in.offer_count=common::max_options;
    for (auto& offer:f.in.offers) offer=first;
}
__attribute__((noinline)) void score_reuse_alternatives_and_fresh_calls() {
    scenario="score reuse: complete alternatives and fresh calls";
    auto f=std::make_unique<Fixture>(); independent_menu(*f);
    check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).option==0,
        "all eight equal complete offers retain their first occurrence");
    check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).record_episode,
        "A owner records the current declared independent component");
    check(!compare_choose(f->workshop,f->in,f->ledger,Arm::control).record_episode,
        "control ranks the same alternatives without recording a new episode");
    f->in.offers[6].schedule.productions[0].gems.value=13;
    check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).option==6,
        "a later strictly better complete schedule replaces the first offer");
    f->in.offers[6].schedule.productions[0].gems.value=12;
    check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).option==0,
        "reverting the same schedule object removes its previous advantage");
    auto& last=f->in.offers[7]; last.plan.organization=Organization::G;
    last.schedule.productions[0].gems.value=14;
    auto selected=compare_choose(f->workshop,f->in,f->ledger,Arm::active);
    check(selected.option==7&&selected.organization==Organization::G&&!selected.record_episode,
        "the last alternative remains eligible G income without configuration proof");
    compare_both(f->workshop,f->in,f->ledger);
    const auto windows=f->in.windows;
    f->in.windows.deadlines=f->in.windows.windows={10,10,10};
    check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).option==0,
        "changed current windows remove the late larger payout from ranking");
    f->in.windows=windows;
    check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).option==7,
        "restored current windows restore the later payout without a cached rank");
    ++f->in.frame.own.gold;
    check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).option<0,
        "changed present cash rejects every old schedule resource snapshot");
    for (auto& offer:f->in.offers) ++offer.schedule.actors[0].initial_gold.value;
    check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).option==7,
        "fresh initial resources permit the same schedule objects again");
    --f->in.frame.own.gold;
    for (auto& offer:f->in.offers) --offer.schedule.actors[0].initial_gold.value;
    ++f->in.frame.clock.revision;
    const auto clock=f->in.frame.clock;
    f->in.frame.group.clock=f->in.own_state.clock=f->in.threat_regime.clock=f->in.market.inventory.clock=clock;
    for (int i=0;i<f->in.frame.actor_count;++i) f->in.frame.actors[i].clock=clock;
    for (auto& offer:f->in.offers) for (int i=0;i<offer.safety_count;++i) offer.safety[i].clock=clock;
    selected=compare_choose(f->workshop,f->in,f->ledger,Arm::active);
    check(selected.option==7&&selected.clock==clock,"a fresh revision supplies the complete current decision clock");
    f->in.frame.own.actor.bag=f->in.own_state.value.bag=12;
    for (auto& offer:f->in.offers) offer.schedule.actors[0].initial_bag.value=12;
    f->in.frame.own_team_score=988;
    f->in.offers[6].schedule.immediate_win={1,2000,fact<bool>(true)};
    selected=compare_choose(f->workshop,f->in,f->ledger,Arm::active);
    check(selected.option==6&&selected.score.immediate_legal_win,
        "a legal immediate-win witness outranks a later larger forecast and survives score reuse");
    --f->in.frame.own_team_score;
    check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).option==7,
        "changed current team score removes a no-longer-sufficient immediate-win witness");

    scenario="score reuse: every offer rejection and full rejected array";
    auto rejected=std::make_unique<Fixture>(); independent_menu(*rejected);
    auto& offers=rejected->in.offers;
    offers[1].plan.organization=Organization::G;
    offers[2].safety[0].solo=false;
    ++offers[3].schedule.actors[0].initial_gold.value;
    offers[4].schedule.actors[0].events[0].source=p6::Source::unknown;
    offers[5].diagnostic_segment=true;
    offers[6].marked_extra_turns=-1;
    offers[7].capital_cost=rejected->in.frame.own.gold+1;
    check(common::legal_offer(rejected->in,offers[2])==Rejection::none&&!common::crew_legal(rejected->in,offers[2],false),
        "a successfully evaluated offer can still fail the independent crew gate");
    for (Arm arm:{Arm::active,Arm::control}) {
        const auto d=compare_choose(rejected->workshop,rejected->in,rejected->ledger,arm);
        check(d.option==0&&d.rejected[0]==Rejection::none&&d.rejected[1]==Rejection::none&&
            d.rejected[2]==Rejection::unsafe&&d.rejected[3]==Rejection::resources&&
            d.rejected[4]==Rejection::malformed&&d.rejected[5]==Rejection::evidence&&
            d.rejected[6]==Rejection::evidence&&d.rejected[7]==Rejection::resources,
            "later alternatives preserve every legal, resource, evaluation and evidence rejection");
    }
    offers[0].diagnostic_segment=offers[1].diagnostic_segment=true;
    const auto empty=compare_choose(rejected->workshop,rejected->in,rejected->ledger,Arm::active);
    check(empty.option<0&&empty.reason==Rejection::opportunity_unavailable,
        "no accepted alternative retains the default unavailable reason and score");
}
__attribute__((noinline)) void score_reuse_owner_phases_and_trial_health() {
    scenario="score reuse: owner phase and recording gates";
    auto f=std::make_unique<Fixture>();
    for (Phase phase:{Phase::A,Phase::rebuilding_B,Phase::B,Phase::rollback_pending,
                      Phase::rebuilding_A,Phase::retired_A,Phase::blocked}) {
        f->workshop.phase=phase;
        for (Arm arm:{Arm::active,Arm::control}) {
            const auto d=compare_choose(f->workshop,f->in,f->ledger,arm);
            const bool waiting=phase==Phase::rebuilding_B||phase==Phase::rebuilding_A;
            check(d.option==(waiting?-1:0)&&d.rejected[1]==Rejection::evidence,
                "only an incomplete rebuild blocks presently verified independent work");
            check(d.record_episode==(phase==Phase::A&&arm==Arm::active),
                "only active A records this exact original configuration");
        }
    }
    f->workshop.phase=Phase::A; f->workshop.declared=false;
    auto invalid=compare_choose(f->workshop,f->in,f->ledger,Arm::active);
    check(invalid.option<0&&invalid.reason==Rejection::malformed&&
        invalid.rejected==std::array<Rejection,common::max_options>{},
        "an undeclared owner returns before processing any offer");
    f->workshop.declared=true; ++f->in.market.inventory.clock.revision;
    invalid=compare_choose(f->workshop,f->in,f->ledger,Arm::active);
    check(invalid.option<0&&invalid.reason==Rejection::malformed,"stale own inventory cannot authorize score reuse");

    scenario="score reuse: trial health applies only to recorded work";
    // Declare the same low-band premises in the heap-owned fixture rather than
    // materializing whole Configuration, EvidenceLedger and Workshop temporaries.
    auto low=std::make_unique<Fixture>(true);
    check(low->in.component_slot>=0&&low->in.supported_slot>=0,"low-band fixture has declared complete health envelopes");
    low->in.offer_count=3; low->in.offers[2]=low->in.offers[0]; low->in.offers[2].plan.organization=Organization::G;
    for (Arm arm:{Arm::active,Arm::control}) {
        const auto d=compare_choose(low->workshop,low->in,low->ledger,arm);
        check(d.option==2&&d.rejected[0]==Rejection::unsafe&&!d.record_episode,
            "low trial health rejects recorded A while ordinary current-gear G stays productive");
    }
    low->in.frame.own.actor.health=35; low->tick(0);
    low->in.offers[2]=low->in.offers[0]; low->in.offers[2].plan.organization=Organization::G;
    check(compare_choose(low->workshop,low->in,low->ledger,Arm::active).option==0,
        "the exact minimum trial-health boundary retains the first recorded A offer");
}
__attribute__((noinline)) void score_reuse_supported_comparisons_and_commit() {
    scenario="score reuse: real B admission and explicit independent comparison";
    auto f=std::make_unique<Fixture>(); f->qualify(); f->complete_forward();
    check(f->workshop.commission(f->in,f->local,f->ledger,f->work),"comparison fixture commissions actual rebuilt B");
    f->work={}; f->tick(62);
    const auto independent=f->in.offers[0],supported=f->in.offers[1];
    auto d=compare_decide(f->workshop,f->in,f->ledger,Arm::active);
    check(d.option==1&&d.record_episode,"a strictly better real B offer records its new supported campaign");
    d=compare_choose(f->workshop,f->in,f->ledger,Arm::control);
    check(d.option==0&&!d.record_episode&&d.rejected[1]==Rejection::evidence,"control rejects H after the same actual rebuild");
    f->in.offers[1].schedule=f->in.offers[0].schedule;
    f->in.offers[1].schedule.target_id=f->in.offers[1].plan.group;
    d=compare_choose(f->workshop,f->in,f->ledger,Arm::active);
    check(d.option==0&&d.rejected[1]==Rejection::no_gain,"H cannot substitute an equal-income higher-target schedule for real gain");
    f->in.offers[1]=supported;
    f->in.offers[0].plan.group=f->in.offers[0].schedule.target_id=supported.plan.group;
    f->in.offers[0].safety[0].group=supported.plan.group;
    f->in.offers[0].schedule=supported.schedule;
    d=compare_choose(f->workshop,f->in,f->ledger,Arm::active);
    check(d.option==0&&d.rejected[1]==Rejection::no_gain,"an exact full-score tie with independent work rejects H");
    f->in.offers[0]=independent;
    auto& buyer=f->in.offers[1].schedule.actors[0];
    event(buyer,p6::EventKind::purchase,4001,1); buyer.events[buyer.event_count-1].gold_cost=fact<p6::Gems>(1);
    check(common::legal_offer(f->in,f->in.offers[1])==Rejection::none&&!common::package_funded(f->in,f->in.offers[1]),
        "the H funding fixture reaches package funding after a successful schedule evaluation");
    d=compare_choose(f->workshop,f->in,f->ledger,Arm::active);
    check(d.option==0&&d.rejected[1]==Rejection::no_gain,"package funding retains its H no-gain short circuit");
    f->in.offers[1]=supported;
    for (bool cash_fault:{false,true}) {
        f->in.offers[0]=independent;
        f->in.offers[0].schedule.productions[0].gems.value=40;
        if (cash_fault) f->in.offers[0].capital_cost=f->in.frame.own.gold+1;
        else f->in.offers[0].safety[0].bank_executable=false;
        check(common::evaluate(f->in,f->in.offers[0].schedule).eligible(),
            "an independently rejected offer still has an eligible fresh comparison score");
        d=compare_choose(f->workshop,f->in,f->ledger,Arm::active);
        check(d.option<0&&d.rejected[0]==(cash_fault?Rejection::resources:Rejection::unsafe)&&d.rejected[1]==Rejection::no_gain,
            "H still compares against a stronger independent schedule rejected before optional output is filled");
        f->in.offers[0].schedule.productions[0].gems.value=12;
        check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).option==1,
            "a genuinely better H still wins when the weaker independent offer has the same early rejection");
        compare_both(f->workshop,f->in,f->ledger);
    }
    f->in.offers[0]=independent;
    const auto proposed=compare_decide(f->workshop,f->in,f->ledger,Arm::active);
    f->in.offers[1].schedule.productions[0].gems.value=20;
    check(!f->workshop.commit(f->in,f->local,proposed,f->ledger,Arm::active,f->work).started&&!f->ledger.active(),
        "commit reranks changed same-clock schedules before starting work or issuing an episode");
    f->in.offers[1]=supported;
    ++f->in.threat_regime.value; f->tick(63);
    d=compare_decide(f->workshop,f->in,f->ledger,Arm::active);
    check(f->workshop.phase==Phase::rollback_pending&&d.option==0&&!d.record_episode&&d.rejected[1]==Rejection::evidence,
        "decide refresh observes changed B premises before ranking productive rollback fallback");
}
__attribute__((noinline)) void score_reuse_terminal_terms() {
    scenario="score reuse: complete terminal score fields and ordering";
    auto f=std::make_unique<Fixture>(); f->tick(1980); independent_menu(*f);
    f->in.windows.deadlines=f->in.windows.windows={20,20,20};
    for (auto& offer:f->in.offers) {
        offer.plan.remaining_rounds=20;
        auto& tie=offer.schedule.terminal_tiebreak;
        tie.matched_opponent_continuation=true; tie.earned_gold_delta=fact<p6::Gems>(10); tie.kills_delta=fact<p6::Gems>(0);
    }
    f->in.offers[7].schedule.terminal_tiebreak.earned_gold_delta.value=20;
    auto d=compare_choose(f->workshop,f->in,f->ledger,Arm::active);
    check(d.option==7&&d.score.terminal_endpoint&&d.score.terminal_terms&&d.score.earned_gold_delta==20,
        "known terminal earned gold breaks a complete gem-score tie");
    f->in.offers[7].schedule.terminal_tiebreak.earned_gold_delta.value=10;
    f->in.offers[7].schedule.terminal_tiebreak.kills_delta.value=2;
    check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).score.kills_delta==2,
        "known terminal champion kills remain part of the returned winning score");
    f->in.offers[7].schedule.terminal_tiebreak.kills_delta.value=0;
    check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).option==0,"restoring all terminal ties retains the first alternative");
    compare_both(f->workshop,f->in,f->ledger);
}

__attribute__((noinline)) void productive_rebuild_and_new_campaign() {
    auto f=std::make_unique<Fixture>();
    check(compare_choose(f->workshop,f->in,f->ledger,Arm::active).option==0,"all actors start in A independent circulation");
    check(!f->forward(0),"unqualified collector cannot spend a rebuild attempt");
    f->qualify();
    check(!f->workshop.begin_forward(f->in,f->local,f->ledger,Arm::control,f->work,f->comparison()),"control retains its original specialization");
    check(common::evaluate(f->in,f->in.offers[0].schedule).min_margin==32&&common::evaluate(f->in,f->in.offers[1].schedule).min_margin==34,
        "public distinct higher-load source repays helper displacement and counted preparation");
    f->complete_forward(); const auto token=f->workshop.rebuild_permit;
    CampaignAdmission admission{{f->db.key,f->in.own_state.value,all_premises},true,true,true,true,false};
    check(!f->ledger.begin_after_declared_rebuild(token,62,admission),"unconfirmed rebuild flag cannot spend shared reservation");
    admission.confirmed_rebuild=true; admission.actual_spawn=false;
    check(!f->ledger.begin_after_declared_rebuild(token,62,admission),"absence of actual spawn prevents new campaign");
    admission.actual_spawn=true; ++admission.premises.key.source_group;
    check(!f->ledger.begin_after_declared_rebuild(token,62,admission),"changed declared B premises cannot borrow reserved old proof");
    check(f->workshop.commission(f->in,f->local,f->ledger,f->work),"actual complete rebuilt gear and current safe visible crew commission new H");
    check(!f->ledger.begin_after_declared_rebuild(token,62,admission),"reserved authority token is consumed exactly once");
    check(f->ledger.state(f->in.supported_slot)->positive_completions==0&&!f->ledger.organization_qualified(f->in.supported_slot,f->db.key,62),
        "new actual build routes and membership inherit no complete organization qualification");
    f->physical(64,true);
    check(!f->ledger.organization_qualified(f->in.supported_slot,f->db.key,84),"first real H completion stays unqualified");
    f->physical(100,true);
    check(f->ledger.organization_qualified(f->in.supported_slot,f->db.key,120),"only two complete actual H cycles qualify its changed duties and gear");
    check(f->workshop.rebuild.attempts==1&&f->workshop.phase==Phase::B,"success neither spends nor replenishes rollback attempt");
}
__attribute__((noinline)) void cash_and_receipt_boundaries() {
    auto f=std::make_unique<Fixture>(); f->in.frame.own.gold=0; f->tick(0);
    check(!f->in.market.items[survival_item].buy_legal.value&&funded(f->in,f->gear_a,f->gear_b).valid,
        "currently unaffordable purchase can be funded by legal prior actual Tavi sale");
    check(f->workshop.rebuild.begin(f->in,f->local,f->work,f->gear_b),"present refund funds chain without future capture income");
    const auto c=f->workshop.rebuild.next(f->in,f->local,f->work); const auto r=f->execute(c);
    auto stale=r; stale.observed_clock=c.clock;
    check(!f->workshop.rebuild.confirm(c,stale,f->in),"stale same revision is not an actual transaction receipt");
    check(f->workshop.rebuild.confirm(c,r,f->in),"legal exact sale receipt confirms after initial rejection");
    check(!f->workshop.rebuild.confirm(c,r,f->in),"duplicate sale confirmation cannot increase cash or attempts");
    f->tick(1); const auto buy=f->workshop.rebuild.next(f->in,f->local,f->work);
    check(buy.valid&&buy.kind==TransactionKind::buy&&f->in.frame.own.gold==30,"buy waits for own observed refunded gold");
    const auto br=f->execute(buy); --f->in.frame.own.gold;
    check(!f->workshop.rebuild.confirm(buy,br,f->in),"an unrelated or partial cash delta cannot confirm item transaction");
    ++f->in.frame.own.gold;
    check(f->workshop.rebuild.confirm(buy,br,f->in),"exact actual cash and items allow confirmation without rounded stat assumptions");
    check(!f->workshop.rebuild.confirm(buy,br,f->in),"duplicate completed purchase cannot become another rebuild");

    auto p=std::make_unique<Fixture>(); p->in.frame.own.actor.type=p6::Champion::marcella;
    p->in.draft.champions[0]=p6::Champion::marcella; p->in.draft.champions[2]=p6::Champion::ren;
    p->in.frame.actors[1].value.type=p6::Champion::ren; p->tick(0);
    check(!funded(p->in,p->gear_a,p->gear_b).valid,"non-Tavi role cannot inherit Tavi full-refund rule");
    p->in.market.items[damage_item].refund={10,p6::Source::queried_setting};
    check(!funded(p->in,p->gear_a,p->gear_b).valid,"queried partial refund plus present cash insufficient for full new bundle");
    p->in.frame.own.gold=20; p->tick(1);
    check(funded(p->in,p->gear_a,p->gear_b).valid,"explicit queried partial refund may fund a currently permitted non-Tavi transaction");
    p->in.market.sell_legal.value=false;
    check(!funded(p->in,p->gear_a,p->gear_b).valid,"pinned host non-Tavi sale refusal overrides copied economics");
    Inventory fifth=p->gear_a; fifth.count[damage_item]=5;
    check(!fifth.valid(),"unmodeled automatic fifth-shard conversion is outside the fixed pair");
}
__attribute__((noinline)) void interrupted_rebuild_and_cap() {
    auto f=std::make_unique<Fixture>(); f->qualify(); check(f->forward(),"partial rebuild first receives real component permission");
    const auto c=f->workshop.transaction(f->in,f->local,f->ledger,f->work); const auto r=f->execute(c);
    check(f->workshop.confirm(c,r,f->in,f->ledger),"partial sell confirmed and actual inventory retained");
    const auto token=f->workshop.rebuild_permit; f->workshop.transaction_failed(f->ledger);
    check(f->workshop.phase==Phase::rollback_pending&&f->workshop.rebuild.attempts==1&&f->in.market.inventory.value!=f->gear_a,
        "interrupted partial rebuild keeps spent attempt and actual partly sold gear");
    check(!f->ledger.reserve_declared_rebuild(f->in.component_slot,f->in.supported_slot,62,f->db.key),"abandoned B target cannot reset or reserve anew");
    CampaignAdmission admission{{f->db.key,f->in.own_state.value,all_premises},true,true,true,true,true};
    check(!f->ledger.begin_after_declared_rebuild(token,62,admission),"abandoned reservation cannot later commission B");
    f->tick(62); check(f->workshop.begin_rollback(f->in,f->local,f->ledger,f->work),"safe partial inventory can fund one return to A");
    const auto restore=f->workshop.transaction(f->in,f->local,f->ledger,f->work);
    check(restore.kind==TransactionKind::buy&&restore.item==damage_item,"rollback restores actual first configuration item");
    const auto rr=f->execute(restore); check(f->workshop.confirm(restore,rr,f->in,f->ledger),"restored A follows real cash debit and stat requery");
    check(f->workshop.phase==Phase::retired_A&&f->workshop.rebuild.attempts==2&&f->in.market.inventory.value==f->gear_a,
        "one failed forward and one actual rollback exhaust two permanent match attempts");
    check(!f->workshop.rebuild.begin(f->in,f->local,f->work,f->gear_b)&&!f->forward(63),"epoch or restored old gear cannot replenish rebuild cap");
    check(!f->ledger.component_qualified(f->in.component_slot,f->da.key,63),"returning to old inventory does not resurrect old proof");
}
__attribute__((noinline)) void safe_inventory_and_failure_gate() {
    auto f=std::make_unique<Fixture>(); f->qualify();
    f->in.frame.own.actor.bag=12; f->tick(61);
    check(!f->forward(61)&&f->workshop.rebuild.attempts==0,"existing own bag must be actually secured before rebuild");
    f->in.frame.own.actor.bag=0; f->in.frame.own.actor.channel=p6::Channel::tribute; f->in.frame.own.actor.channel_valid=true; f->tick(61);
    check(!f->forward(61),"valid own tribute cannot be interrupted for specialization switch");
    f->in.frame.own.actor.channel=p6::Channel::none; f->tick(61); f->complete_forward();
    check(f->workshop.commission(f->in,f->local,f->ledger,f->work),"B enters only after own inventory obligation is finished");
    f->work={}; f->tick(62);
    Resolution unknown; unknown.accepted=true; unknown.status=Status::unavailable;
    f->workshop.observe_result(unknown,f->ledger);
    check(f->workshop.phase==Phase::B,"censored or unknown result does not spend rollback permission");
    const auto d=compare_decide(f->workshop,f->in,f->ledger,Arm::active);
    check(f->workshop.commit(f->in,f->local,d,f->ledger,Arm::active,f->work).started,"real H starts its first new-build episode");
    const auto e=*f->ledger.active();
    Observation bad; bad.round=e.issue_round+1; bad.kind=ObservationKind::safety_failure;
    bad.premises={f->db.key,f->in.own_state.value,all_premises};
    const auto failed=f->ledger.resolve(e.serial,bad); f->workshop.observe_result(failed,f->ledger);
    check(failed.accepted&&failed.applicable&&f->workshop.phase==Phase::rollback_pending,"one applicable actual supported safety failure licenses rollback");
    f->in.frame.own.actor.position={3,1}; f->in.frame.own.actor.spawn_group=8; f->in.frame.own.actor.bag=24;
    f->work.stage=PhysicalStage::bank; f->work.tribute_started=false; f->work.pending=false; f->tick(64);
    check(!f->workshop.begin_rollback(f->in,f->local,f->ledger,f->work),"loaded active H must finish safe own banking before any rollback sale");
    const auto command=f->workshop.advance(f->in,f->local,f->ledger,f->work);
    check(command.action==PhysicalAction::move&&command.target==p6::Point{4,1},"rollback request retains the actual loaded bank route");
    check(f->work.confirm(command,{command.clock,command.clock,command.token,command.action,true,true}),"loaded bank movement remains an actual action");
}
__attribute__((noinline)) void reservation_expiry_and_diagnostic_guard() {
    auto f=std::make_unique<Fixture>(); f->qualify(); f->complete_forward();
    f->tick(316);
    check(!f->workshop.commission(f->in,f->local,f->ledger,f->work)&&f->workshop.phase==Phase::rollback_pending,
        "old component authority expires at exact latest-positive plus256 even after actual funded rebuild");
    auto g=std::make_unique<Fixture>(); g->qualify(); g->complete_forward();
    check(g->workshop.commission(g->in,g->local,g->ledger,g->work),"fresh reserved proof can still begin only unqualified B");
    g->in.offers[1].diagnostic_segment=true; g->in.offers[1].marked_extra_turns=1;
    const auto ordinary=compare_choose(g->workshop,g->in,g->ledger,Arm::active);
    check(ordinary.option==0&&!ordinary.record_episode&&ordinary.rejected[1]==Rejection::evidence,
        "marked H diagnostic is rejected while verified ordinary current-gear U remains productive");
    g->in.offers[1].diagnostic_segment=false; g->in.offers[1].marked_extra_turns=0;
    g->in.threat_regime.value=1; g->tick(63); g->workshop.refresh(g->in,g->ledger);
    check(g->workshop.phase==Phase::rollback_pending,"actual changed new-build threat premise licenses safe rollback");
    check(reversible_collector(p6::Champion::tavi)&&!reversible_collector(p6::Champion::marcella),"current draft collector requires actual reversible SDK capability");
}
__attribute__((noinline)) void unknown_keeps_current_gear_productive() {
    auto f=std::make_unique<Fixture>(); f->qualify(); f->complete_forward();
    check(f->workshop.commission(f->in,f->local,f->ledger,f->work),"unknown-case starts real B trial only after transactions");
    f->work={}; f->tick(62);
    const auto d=compare_decide(f->workshop,f->in,f->ledger,Arm::active);
    check(f->workshop.commit(f->in,f->local,d,f->ledger,Arm::active,f->work).started,"new B trial begins before unavailable observation");
    const auto e=*f->ledger.active(); Observation unavailable;
    unavailable.round=63; unavailable.kind=ObservationKind::unavailable;
    unavailable.premises={f->db.key,f->in.own_state.value,all_premises};
    const auto result=f->ledger.resolve(e.serial,unavailable); f->workshop.observe_result(result,f->ledger);
    f->work.invalidate(PhysicalFailure::unavailable); f->work.pending=false; f->tick(63);
    const auto fallback=compare_decide(f->workshop,f->in,f->ledger,Arm::active);
    check(fallback.option==0&&!fallback.record_episode&&f->workshop.phase==Phase::B&&f->workshop.rebuild.attempts==1,
        "unavailable H permits current-S-gear independent income without rollback authority or expense");
    f->in.offers[0].plan.forecast_rounds=19;
    f->physical(64,false);
    check(f->in.market.inventory.value==f->gear_b&&f->workshop.rebuild.attempts==1,
        "fallback actually captures banks and renews with retained B gear rather than idling or rebuilding");
}
__attribute__((noinline)) void visible_helper_keeps_own_bag() {
    auto origin=std::make_unique<Fixture>(); auto h=std::make_unique<Inputs>(origin->in);
    auto local=std::make_unique<LocalFrame>(); auto work=std::make_unique<Work>();
    auto helper=std::make_unique<Workshop>(); auto private_ledger=std::make_unique<EvidenceLedger>(2);
    auto& f=h->frame; const auto collector=f.own.actor; const auto worker=f.actors[0].value; const auto third=f.actors[1].value;
    f.own.actor=worker; f.own.actor.position={0,0}; f.own.actor.bag=9; f.own.gold=15;
    f.actors[0].value=collector; f.actors[0].subject=1; f.actors[0].value.position={2,1};
    f.actors[1].value=third; f.group.value.id=8; f.group.subject=8; f.group.value.lifecycle=p6::Lifecycle::living;
    h->market.inventory.value={}; h->own_state.value={100,100,48,10,1,9,h->market.inventory.value.hash()}; h->own_state.subject=2;
    h->threat_regime.subject=2; h->component_slot=h->supported_slot=-1;
    h->offers[0].plan.collector=2; h->offers[0].plan.crew[0]=2;
    auto& p=h->offers[1].plan;
    p.ingress=path({{0,0},{1,1},{2,2}}); p.capture=path({{2,2},{3,1}});
    p.release=path({{3,1},{3,2}}); p.bank=path({{3,1},{3,2},{4,2}});
    p.renewal=path({{4,2},{4,1},{3,1},{2,1},{1,1},{0,0}}); p.forecast_rounds=26;
    auto tick=[&](int round,p6::Point position) {
        f.clock={round,2,0}; f.own.actor.position=position; f.own.at_spawn=position==p6::Point{0,0};
        f.own.actor.spawn_group=position==p6::Point{3,1}?8:-1;
        f.group.clock=f.clock;
        for (int i=0;i<f.actor_count;++i) f.actors[i].clock=f.clock;
        h->own_state.clock=h->threat_regime.clock=h->market.inventory.clock=f.clock; h->market.inventory.subject=2;
        h->own_state.value.bag=f.own.actor.bag;
        h->windows.sampled_round=round; h->windows.sampled_actor_id=2;
        h->visible_proposer={8,f.clock,1,p6::Knowledge::exact};
        for (int i=0;i<2;++i) {
            auto& o=h->offers[i]; o.schedule=schedule(f,i==1); o.safety_count=3;
            if (i==1) o.schedule.actors[1].occupied_turns=fact<p6::Gems>(26);
            for (int id=1;id<=3;++id) o.safety[id-1]=cert(f,id,common::participant(o.plan,id)?o.plan.group:9);
        }
        local->frame=f; local->current_round=round; local->safety_count=3; local->milestones={};
        for (int id=1;id<=3;++id) local->safety[id-1]=cert(f,id,8);
        local->legal={{},f.clock,2,p6::Knowledge::exact};
        auto& legal=local->legal.value; legal.safe_now=legal.attack_legal=legal.altar_legal=legal.renewal_productive=legal.recall_safe=true;
        for (Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
            auto& a=legal.routes[int(leg)]; const auto& r=route_for(p,leg); a.assumptions_hold=route_valid(r); a.next_move_legal=true;
            for (int i=0;i<r.count;++i) if (position==r.tiles[i]) { a.next_tile=r.tiles[std::min(i+1,r.count-1)]; break; }
        }
    };
    tick(0,{0,0});
    auto d=compare_decide(*helper,*h,*private_ledger,Arm::active);
    check(d.option==1&&!d.record_episode&&!helper->declared&&private_ledger->size()==0,
        "undeclared helper adopts only visible safe proposal without collector private proof");
    const auto control=compare_choose(*helper,*h,*private_ledger,Arm::control);
    check(control.option==0&&!control.record_episode&&control.rejected[1]==Rejection::evidence,
        "control helper retains its independently safe income without adopting H");
    const auto visible=h->visible_proposer;
    ++h->visible_proposer.clock.revision;
    const auto hidden=compare_choose(*helper,*h,*private_ledger,Arm::active);
    check(hidden.option==0&&hidden.rejected[1]==Rejection::evidence,
        "a stale visible proposer cannot supply helper admission on a later call");
    h->visible_proposer=visible;
    h->offers[1].diagnostic_segment=true;
    check(compare_choose(*helper,*h,*private_ledger,Arm::active).rejected[1]==Rejection::evidence,
        "helper admission does not bypass the diagnostic-work rejection");
    h->offers[1].diagnostic_segment=false;
    same_decision(compare_choose(*helper,*h,*private_ledger,Arm::active),d);
    check(helper->commit(*h,*local,d,*private_ledger,Arm::active,*work).started,"real helper enters its own retained H work");
    auto step=[&](int round,p6::Point pos,bool bank=false) {
        tick(round,pos);
        if (bank) { f.own.actor.bag=0; h->own_state.value.bag=0; local->frame=f;
            local->milestones={{8,false,true,0,9},f.clock,2,p6::Knowledge::exact}; }
        const auto c=helper->advance(*h,*local,*private_ledger,*work);
        check(work->confirm(c,{c.clock,c.clock,c.token,c.action,true,true}),"helper physical command consumes actual receipt"); return c;
    };
    step(0,{0,0}); step(1,{1,1});
    check(step(2,{2,2}).action==PhysicalAction::attack,"helper performs useful actual clear work");
    f.group.value.lifecycle=p6::Lifecycle::reward_available;
    step(3,{2,2}); f.actors[0].value.position={3,0}; f.actors[0].value.spawn_group=8;
    const auto released=step(4,{3,1});
    check(released.action==PhysicalAction::move&&released.target==p6::Point{3,2}&&f.own.actor.bag==9,
        "helper withdraws after visible independently safe holder and retains existing own bag");
    step(5,{3,2}); check(step(6,{4,2}).action==PhysicalAction::tribute,"released helper banks its retained bag on prefix-compatible route");
    check(step(7,{4,2},true).action==PhysicalAction::move,"actual helper bank receipt begins own return");
    step(8,{4,1}); step(9,{3,1}); step(10,{2,1}); step(11,{1,1}); step(12,{0,0});
    check(work->helper_released&&work->bank_seen&&work->renewal_seen&&f.own.actor.bag==0&&!private_ledger->active(),
        "helper actually banks and renews without receiving source payout or shared qualification");
}
} // namespace
int main() {
    ordinary_current_admission_reference();ordinary_current_admission_preconditions();ordinary_current_admission_local_rejections();
    legal_offer_probe_equivalence();
    inventory_hash_original_oracle();
    score_reuse_alternatives_and_fresh_calls(); score_reuse_owner_phases_and_trial_health();
    score_reuse_supported_comparisons_and_commit(); score_reuse_terminal_terms();
    scenario="physical workshop fixture";
    productive_rebuild_and_new_campaign(); cash_and_receipt_boundaries(); interrupted_rebuild_and_cap();
    safe_inventory_and_failure_gate(); reservation_expiry_and_diagnostic_guard();
    unknown_keeps_current_gear_productive(); visible_helper_keeps_own_bag();
    std::printf("plan8 reversible workshop: %d checks passed\n",checks);
}
