// One real evaluated three-core quote crosses Caravan's policy and the typed
// reference transaction. Certificates and decisions are never manufactured.
#include "../common/src/crew_forecast.h"
#include "../common/src/runtime_joint.h"
#include "../caravan/src/policy.h"
#include "../owner-builder/src/organization.h"
#include "../reference/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
using namespace p6;
namespace fc = p6::forecast;
namespace joint = p6::runtime_joint;
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL crew caller: %s\n",message); std::exit(1); }
}
template<class T> Observation<T> seen(T value, ViewClock clock, int subject) {
    return {value,clock,subject,Knowledge::exact};
}
int distance(Point a, Point b) {
    return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));
}
struct Fixture {
    organization::Input in{};
    fc::CrewRequest request{};
    fc::EvaluatedCrewQuote crew{};
    std::array<fc::EvaluatedQuote,3> source_solos{}, alternatives{};
    Schedule independent{};
    ActorState execution{};
    caravan::State policy{};
};
Actor actor(int ordinal) {
    constexpr std::array<Point,3> pads{{{1,2},{3,1},{3,3}}};
    Actor a;
    a.id=100+ordinal; a.pick_ordinal=ordinal;
    a.type=caravan::profile_value.roster[ordinal]; a.position=pads[ordinal];
    a.health=a.max_health=240; a.strength=10;
    a.ranged=ordinal!=0; a.attack_range=a.ranged?3:1; a.move_speed=1;
    a.team=Team::ally; a.alive=a.can_move=a.can_attack=true;
    a.ready_attack_group=0;
    return a;
}
fc::Request route(const organization::Input& in, const Frame& frame,
                  const Group& source, Point capture_pad, bool alternative) {
    const auto& self=frame.own.actor;
    fc::Request r;
    r.shape=fc::Shape::produce_bank; r.project_group=source.id;
    r.timing={{in.settings,Source::queried_setting},{.1,Source::queried_setting},
        {0,Source::queried_setting},{1,Source::queried_setting}};
    r.kit=seen(Productivity{self.strength,0,self.ranged},frame.clock,self.id);
    r.ambient_incoming=seen(0.,frame.clock,self.id);
    r.bank_waypoint=9000; r.bank_position={2,0};
    r.renewal_waypoint=0; r.renewal_position={1,2};
    r.bank_available=seen(true,frame.clock,r.bank_waypoint);
    r.bank_lock_hits={5,Source::queried_setting};
    r.group_count=1;
    auto& group=r.groups[0];
    group.observed=seen(source,frame.clock,source.id);
    group.observed.value.approach=self.position;
    group.observed.value.spawn=capture_pad;
    // This intact source is observed asleep, so none of its strength has been
    // included in the exact zero ambient observation above.
    group.ambient_overlap=seen(0.,frame.clock,source.id);
    group.membership_complete={true,Source::public_map};
    group.member_count=alternative?1:3;
    for(int m=0;m<group.member_count;++m) {
        const MonsterWork value{200+source.id*10+m,alternative?10.:100.,alternative?0.:3.};
        group.members[m].public_full={value,Source::public_map};
        group.members[m].current=seen(value,frame.clock,value.id);
    }
    group.gems={alternative?Gems{3}:Gems{90},Source::public_map};
    group.gold={0,Source::public_map};
    auto leg=[&](Point from,Point to) {
        r.legs[r.leg_count++]={from,to,{distance(from,to),Source::public_map}};
    };
    leg(self.position,group.observed.value.approach);
    leg(group.observed.value.approach,capture_pad);
    leg(capture_pad,r.bank_position);
    leg(r.bank_position,r.renewal_position);
    return r;
}
std::unique_ptr<Fixture> fixture() {
    auto f=std::make_unique<Fixture>();
    auto& in=f->in;
    in.ordinal=0; in.picks=3; in.settings={300,1000,5};
    in.safe_now=true; in.site_count=4; in.independent_group=1; in.proposed_group=0;
    in.frame.own.actor=actor(0); in.frame.clock={10,100,1};
    for(int ordinal=1;ordinal<3;++ordinal)
        in.frame.actors[in.frame.actor_count++]=seen(actor(ordinal),in.frame.clock,100+ordinal);
    ScoreHistory history;
    check(history.sample({10,100},0),"real private score history accepts current sample");
    in.windows=history.windows(in.settings,{10,100},0);
    auto& source=in.sites[0];
    source.known=true;
    source.group={0,{1,2},{2,1},{1,2},Lifecycle::living,true,true,true};
    in.frame.group=seen(source.group,in.frame.clock,0);
    constexpr std::array<Point,3> capture_pads{{{2,1},{2,2},{2,3}}};
    f->request.ready_basics=seen(std::uint8_t{7},in.frame.clock,0);
    f->independent.target_id=1;
    for(int ordinal=0;ordinal<3;++ordinal) {
        const auto* current=in.ordinal_actor(ordinal);
        check(current!=nullptr,"every distinct core actor is personally current");
        f->request.actors[ordinal]=current->id;
        auto frame=joint::subject_frame(in,*current);
        f->request.routes[ordinal]=route(in,frame,source.group,capture_pads[ordinal],false);
        f->source_solos[ordinal].evaluate(frame,f->request.routes[ordinal]);
        if(f->source_solos[ordinal].result().status==Status::feasible ||
           f->source_solos[ordinal].result().issue!=fc::Issue::unsafe)
            std::fprintf(stderr,"unexpected source solo ordinal=%d status=%d issue=%d\n",ordinal,
                static_cast<int>(f->source_solos[ordinal].result().status),
                static_cast<int>(f->source_solos[ordinal].result().issue));
        check(f->source_solos[ordinal].result().status!=Status::feasible &&
            f->source_solos[ordinal].result().issue==fc::Issue::unsafe,
            "each actual solo forecast rejects the same intact gem source as unsafe");
        auto& alternate=in.sites[1+ordinal];
        alternate.known=true;
        const Point position{ordinal*2,4};
        alternate.group={1+ordinal,position,position,position,Lifecycle::living,true,true,true};
        auto alternative=route(in,frame,alternate.group,position,true);
        // These are distinct reachable solo sources, not duplicate rewards from
        // three proposed captures of the same independent camp.
        alternative.groups[0].observed.value.approach=position;
        alternative.legs[0]={current->position,position,{distance(current->position,position),Source::public_map}};
        alternative.legs[1]={position,position,{0,Source::public_map}};
        f->alternatives[ordinal].evaluate(frame,alternative);
        const auto& result=f->alternatives[ordinal].result();
        check(result.status==Status::feasible,"an unchanged real independent solo continuation exists for each core actor");
        check(joint::append(f->independent,result.schedule,0,1000*(ordinal+1),in),
            "real independent schedules combine with correct visible resource provenance");
        alternate.actor_count=1; alternate.actors[0]=current->id;
        alternate.cycles[0]=result.certificate;
        alternate.cycles[0].clock=in.frame.clock;
        check(alternate.cycles[0].valid(in.frame,current->id,alternate.group.id),
            "independent source retains its current real solo certificate");
        if(ordinal==0) alternate.independent[0]=evaluate_schedule(result.schedule,in.windows,
            in.settings,{in.frame.clock.round,in.frame.clock.self},in.frame.own_team_score);
    }
    return f;
}
void positive_policy_and_bind() {
    auto f=fixture(); auto& in=f->in;
    f->crew.evaluate(in.frame,f->request);
    check(f->crew.ready() && f->crew.matches_frame(in.frame),"typed crew quote owns the exact personally witnessed frame");
    const auto& result=f->crew.result();
    check(result.status==Status::feasible && result.schedule.actor_count==3,
        "complete three-core schedule is genuinely feasible where all three solos failed");
    auto& source=in.sites[0];
    source.actor_count=3;
    for(int ordinal=0;ordinal<3;++ordinal) {
        source.actors[ordinal]=f->request.actors[ordinal];
        source.cycles[ordinal]=result.certificates[ordinal];
        check(!source.cycles[ordinal].solo && source.cycles[ordinal].valid(in.frame,source.actors[ordinal],0),
            "each participant has a genuine safe complete crew exposure without solo relabeling");
    }
    source.joint_rank=evaluate_schedule(result.schedule,in.windows,in.settings,
        {in.frame.clock.round,in.frame.clock.self},in.frame.own_team_score);
    const auto independent=evaluate_schedule(f->independent,in.windows,in.settings,
        {in.frame.clock.round,in.frame.clock.self},in.frame.own_team_score);
    check(source.joint_rank.eligible() && independent.eligible() && compare(source.joint_rank,independent)>0,
        "actual crew rank improves on all three real independent alternatives");
    source.joint_better=true; source.crew_current=true; source.holder=f->request.actors[0];
    const auto decision=caravan::choose(f->policy,in,false);
    check(decision.intent==organization::Intent::work && decision.group==0,
        "actual Caravan chooses the evaluated current crew source with independent work available");
    ReferenceInput observed{.frame=in.frame};
    observed.assigned_group=decision.group; observed.safe_now=in.safe_now;
    observed.at_approach=true; observed.fresh_target_attack_legal=true;
    const auto proposal=reference::choose_crew_replacing_into(f->execution,observed,&f->crew);
    check(proposal.has_value() && proposal->action==Action::attack && proposal->group==0 &&
        f->execution.active && f->execution.project.group==0,
        "typed crew transaction admits the selected source and produces the normal first attack");
    check(observed.crew==&f->crew && crew_authorized(observed,0) && !observed.solo.solo,
        "execution preserves typed cooperative authority without manufacturing a solo certificate");
    check(!f->execution.payout_seen && !f->execution.bank_seen && !f->execution.tribute_started &&
        observed.frame.own.actor.bag==0 && observed.frame.own_team_score==0,
        "modeled future production and banking do not create actual resources or receipts at admission");
    check(f->alternatives[0].result().status==Status::feasible && in.sites[1].solo(in.frame,100) &&
        in.sites[1].independent[0].eligible(),"crew selection and binding preserve the real independent alternative");

    // The unchanged Owner Builder policy is presented with an eligible fixed
    // owner and those genuine numeric crew certificates. Its stricter solo
    // precondition must still reject them; neither a roster rejection nor a
    // missing independent alternative is allowed to explain this result.
    auto owner_input=in;
    owner_input.frame.own.actor.type=owner_builder::roster[0];
    check(owner_builder::roster_actor(owner_input.frame.own.actor,owner_input.picks) &&
        owner_builder::fixed_owner(owner_input.frame.own.actor,0,owner_input.picks) &&
        owner_input.sites[0].cycle(100)->valid(owner_input.frame,100,0) &&
        !owner_input.sites[0].solo(owner_input.frame,100),
        "Owner Builder fixed-owner eligibility succeeds while genuine crew authority remains non-solo");
    owner_builder::ExecutablePolicy owner;
    const auto strict=owner.choose(owner_input,false);
    check(strict.intent==organization::Intent::independent && strict.group==owner_input.independent_group,
        "unchanged Owner Builder keeps its actual independent alternative instead of accepting crew-only ownership");
}
void reject_current_quote(Fixture& f, fc::Issue issue, const char* message) {
    f.crew.evaluate(f.in.frame,f.request);
    const auto& result=f.crew.result();
    if(result.status==Status::feasible || result.issue!=issue)
        std::fprintf(stderr,"unexpected rejected crew status=%d issue=%d expected=%d\n",
            static_cast<int>(result.status),static_cast<int>(result.issue),static_cast<int>(issue));
    check(result.status!=Status::feasible && result.issue==issue,message);
    check(result.schedule.actor_count==0 && result.schedule.production_count==0 &&
        !result.certificates[0].valid(f.in.frame,100,0),
        "a rejected crew leaves no usable schedule or partial certificate");
    ReferenceInput observed{.frame=f.in.frame};
    observed.assigned_group=0; observed.safe_now=true;
    observed.at_approach=true; observed.fresh_target_attack_legal=true;
    const auto proposal=reference::choose_crew_replacing_into(f.execution,observed,&f.crew);
    check(!proposal && !f.execution.active && observed.crew==nullptr,
        "the real typed caller refuses a rejected crew instead of admitting partial authority");
    const auto decision=caravan::choose(f.policy,f.in,false);
    check(decision.intent!=organization::Intent::work || decision.group!=0,
        "actual Caravan cannot select the failed source without a published current crew proof");
}
void peer_and_witness_boundaries() {
    {
        auto f=fixture(); f->in.frame.actor_count=1;
        reject_current_quote(*f,fc::Issue::unknown_input,"losing one personally visible core actor rejects the crew");
    }
    {
        auto f=fixture(); --f->in.frame.actors[1].clock.revision;
        reject_current_quote(*f,fc::Issue::unknown_input,"a stale core actor cannot supply present crew membership");
    }
    {
        auto f=fixture(); f->request.ready_basics={};
        reject_current_quote(*f,fc::Issue::unknown_input,"missing personal basic geometry witness cannot become a promise");
    }
    {
        auto f=fixture(); f->request.ready_basics.value=3;
        reject_current_quote(*f,fc::Issue::unknown_input,"two ready basic bits cannot admit the third participant");
    }
    {
        auto f=fixture(); f->in.frame.actors[0].value.health=100;
        reject_current_quote(*f,fc::Issue::unsafe,"one vulnerable peer rejects the whole otherwise viable crew");
    }
    {
        auto f=fixture(); auto& peer=f->in.frame.actors[0].value;
        peer.can_attack=peer.can_move=false; peer.completed_turn=true;
        f->crew.evaluate(f->in.frame,f->request);
        const auto& result=f->crew.result();
        check(result.status==Status::feasible && f->crew.matches_frame(f->in.frame),
            "a personally observed completed peer is permitted to act after its next owner boundary");
        check(result.clear_turns==13 && result.certificates[1].clear.incoming_total==81 &&
            result.certificates[1].clear.health==240,
            "completed peers receive no free opening damage: all actors pay the full startup camp phase");
        ReferenceInput observed{.frame=f->in.frame};
        observed.assigned_group=0; observed.safe_now=observed.at_approach=true;
        observed.fresh_target_attack_legal=true;
        const auto proposal=reference::choose_crew_replacing_into(f->execution,observed,&f->crew);
        check(proposal && proposal->action==Action::attack && f->execution.active,
            "a completed peer's fresh safe quote crosses the actual typed caller without waiting for a fictional same-turn hit");
    }
}
void capture_geometry_boundaries() {
    {
        auto f=fixture(); auto& route=f->request.routes[2];
        const Point duplicate=f->request.routes[1].groups[0].observed.value.spawn;
        route.groups[0].observed.value.spawn=duplicate;
        route.legs[1]={route.groups[0].observed.value.approach,duplicate,
            {distance(route.groups[0].observed.value.approach,duplicate),Source::public_map}};
        route.legs[2]={duplicate,route.bank_position,{distance(duplicate,route.bank_position),Source::public_map}};
        reject_current_quote(*f,fc::Issue::malformed,"two holders cannot claim the same physical capture pad");
    }
    {
        auto f=fixture();
        // Same legal endpoints, but a public terrain detour takes four moves.
        // Two conservative movement slack rounds make this exceed the actual
        // five-round capture timer started by the first arriving holder.
        f->request.routes[2].legs[1].distance={4,Source::public_map};
        reject_current_quote(*f,fc::Issue::unavailable,
            "a late third holder cannot receive a share after the first holder's capture can already finish");
    }
}
bool same_incumbent(const ActorState& before, const ActorState& after) {
    if(before.project!=after.project || before.active!=after.active || before.stage!=after.stage ||
       before.phase!=after.phase || before.failure!=after.failure || before.owner_turn!=after.owner_turn ||
       before.last_round!=after.last_round || before.phase_started!=after.phase_started ||
       before.phase_limit!=after.phase_limit || before.waits!=after.waits || before.repairs!=after.repairs ||
       before.retired_group!=after.retired_group || before.retired_epoch!=after.retired_epoch ||
       before.payout_seen!=after.payout_seen || before.bank_seen!=after.bank_seen) return false;
    for(std::size_t i=0;i<before.epoch_budgets.size();++i) {
        const auto& a=before.epoch_budgets[i]; const auto& b=after.epoch_budgets[i];
        if(a.group!=b.group || a.epoch!=b.epoch || a.repairs!=b.repairs || a.retired!=b.retired) return false;
    }
    return true;
}
void stale_replacement_is_transactional() {
    auto f=fixture(); f->crew.evaluate(f->in.frame,f->request);
    ReferenceInput independent{.frame=f->in.frame};
    independent.frame.group=seen(f->in.sites[1].group,independent.frame.clock,1);
    independent.assigned_group=1; independent.safe_now=true;
    const auto original=reference::choose_forecast_replacing_into(f->execution,independent,&f->alternatives[0]);
    check(original && original->action==Action::move && f->execution.active && f->execution.project.group==1,
        "a real evaluated independent alternative first establishes the incumbent through normal admission");
    const auto before=f->execution;
    ReferenceInput candidate{.frame=f->in.frame};
    ++candidate.frame.clock.revision;
    candidate.assigned_group=0; candidate.safe_now=candidate.at_approach=true;
    candidate.fresh_target_attack_legal=true;
    check(!f->crew.matches_frame(candidate.frame),"a new live revision cannot restamp the retained crew quote");
    const auto rejected=reference::choose_crew_replacing_into(f->execution,candidate,&f->crew);
    check(!rejected && same_incumbent(before,f->execution),
        "stale crew replacement preserves the actual incumbent and every retirement/repair charge");
    check(candidate.crew==nullptr && candidate.forecast_rounds==0 && candidate.assigned_group==0,
        "failed crew replacement leaves the caller input unbound as well");
}
void modeled_events_are_not_receipts() {
    auto f=fixture(); f->crew.evaluate(f->in.frame,f->request);
    ReferenceInput observed{.frame=f->in.frame};
    observed.assigned_group=0; observed.safe_now=observed.at_approach=true;
    observed.fresh_target_attack_legal=true;
    check(reference::choose_crew_replacing_into(f->execution,observed,&f->crew).has_value(),
        "receipt boundary starts with a genuinely admitted crew");
    // Even at the modeled capture tick, a local flag without an actual resource
    // increase cannot stand in for personally observed payout evidence.
    auto holding=f->execution;
    holding.enter(Phase::hold,Stage::execute); holding.previous_observed=true;
    holding.previous_bag=holding.previous_gold=0;
    auto payout=observed; payout.frame.clock.round+=f->crew.result().capture_tick;
    payout.completion={payout.frame.clock,0,true,false,true};
    record_completion(holding,payout);
    check(!holding.payout_seen && !holding.empty_liquidation && holding.phase==Phase::hold,
        "elapsed modeled capture and a resource-free claim cannot manufacture a payout receipt");
    auto banking=f->execution;
    banking.enter(Phase::bank,Stage::execute); banking.tribute_started=true;
    banking.tribute_started_round=observed.frame.clock.round;
    banking.tribute_started_bag=30; banking.tribute_started_score=0;
    auto bank=observed; ++bank.frame.clock.round;
    bank.completion={bank.frame.clock,0,false,true,true};
    record_completion(banking,bank);
    check(!banking.bank_seen && banking.phase==Phase::bank && banking.tribute_started,
        "empty inventory and modeled tribute cannot manufacture a bank receipt without actual score increase");
}
void budgeted_build_discards_partial_authority() {
    auto f=fixture();
    for(int cutoff:{1,8}) {
        fc::CrewResult result; int calls=0;
        fc::build_crew_into(f->in.frame,f->request,result,[&] { return ++calls<cutoff; });
        check(result.status==Status::unresolved && result.abort==Failure::expired && calls==cutoff,
            "public crew builder obeys entry and inner-loop budget cancellation");
        check(result.schedule.actor_count==0 && result.schedule.production_count==0 &&
            result.clear_turns==0 && result.capture_tick==0 && result.durations[0]==0 &&
            !result.certificates[0].valid(f->in.frame,100,0),
            "budget interruption discards every partial production, duration and action certificate");
    }
    // Runtime's quote-fill callback is intentionally private. This public
    // builder check does not impersonate that path or assert its ready flag.
}
}
int main() {
    positive_policy_and_bind();
    peer_and_witness_boundaries();
    capture_geometry_boundaries();
    stale_replacement_is_transactional();
    modeled_events_are_not_receipts();
    budgeted_build_discards_partial_authority();
    std::printf("crew caller: %d checks passed\n",checks);
    std::printf("sizes Frame=%zu Request=%zu CrewRequest=%zu CrewResult=%zu EvaluatedCrewQuote=%zu\n",
        sizeof(p6::Frame),sizeof(p6::forecast::Request),sizeof(p6::forecast::CrewRequest),
        sizeof(p6::forecast::CrewResult),sizeof(p6::forecast::EvaluatedCrewQuote));
}
