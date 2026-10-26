#include "../src/capital_conversion_value.h"
#include <array>
#include <cstdio>
#include <limits>
using namespace wb::capital::conversion_value;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;if(failures<=20)std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
template<class A,class B> concept Selectable=requires(A&& a,B&& b){select(static_cast<A&&>(a),static_cast<B&&>(b));};
static_assert(Selectable<Policy&,Policy&>);
static_assert(!Selectable<Policy,Policy&> && !Selectable<Policy&,Policy> && !Selectable<Policy,Policy>);
Context context(){
    Context c;c.owner=2;c.start={40,10};c.horizon={50,100};c.input=17;c.observation=29;c.effect=3;
    c.domain={1,2,3,4,5,6,7,true};c.initial_carried=14;c.initial_known=true;return c;
}
Outcome outcome(std::uint64_t key=1,Evidence evidence=Evidence::ModelForecast){
    Outcome o;o.reply={key,9};o.evidence=evidence;o.coherent=o.legal_reply=o.complete=true;
    o.terminal.own_carried={0,true};o.terminal.foe_carried={0,true};o.terminal.owner_alive={true,true};
    o.terminal.own_service_ready={{48,20},true};o.terminal.foe_service_ready={{48,20},true};
    o.tail.liquidation_known=o.tail.recovery_known=true;
    o.tail.displaced.own_known=o.tail.displaced.foe_known=o.tail.displaced.included_in_scores=true;return o;
}
Policy policy(std::uint64_t id,std::span<const Outcome> outcomes,bool incumbent=false){
    Policy p;p.id=id;p.context=context();p.outcomes=outcomes;p.complete=true;p.complete_economic_incumbent=incumbent;return p;
}
ActualPrefix prefix(Channel channel=Channel::Started){
    ActualPrefix p;p.supplied=true;p.evidence=Evidence::Actual;p.at={40,20};p.attempted=p.applied=1;
    p.channel=channel;p.custody=Custody::Held;p.alive={true,true};p.carried={14,true};return p;
}
Preference choose_candidate(const ModelPair& pair,void* user){
    ++*static_cast<unsigned*>(user);CHECK(!pair.outcomes.empty());
    for(const auto& p:pair.outcomes){CHECK(p.candidate&&p.incumbent);
        CHECK(p.reply==p.candidate->reply&&p.reply==p.incumbent->reply);}
    return Preference::Candidate;
}
void start_is_not_score(){
    const std::array<ScoreEvent,1> payout{{{{43,1},Side::Own,14}}};
    auto completed=outcome();completed.prefix=prefix();completed.scores=payout;
    auto started=outcome(1,Evidence::Actual);started.prefix=prefix();
    started.terminal.own_carried={14,true};started.tail.liquidation_class=8;
    CHECK(payout_total(started,Side::Own).known&&payout_total(started,Side::Own).value==0);
    CHECK(payout_total(completed,Side::Own).value==14);
    CHECK(completed.evidence==Evidence::ModelForecast&&completed.prefix.evidence==Evidence::Actual);
    auto bank=policy(1,{&completed,1},true),actual=policy(2,{&started,1});
    CHECK(compare(actual,bank).reason==Reason::EvidenceMismatch);
    CHECK(select(actual,bank).id==1);
    // Any queried N may supply these dates; the value layer assumes no N=1.
    const std::array<ScoreEvent,1> long_payout{{{{50,90},Side::Own,14}}};
    completed.scores=long_payout;
    CHECK(payout_total(completed,Side::Own).value==14);
    CHECK(completed.prefix.channel==Channel::Started);
}
void interrupted_and_transferred_capital(){
    const std::array<ScoreEvent,1> payout{{{{43,1},Side::Own,14}}};
    auto b=outcome();b.scores=payout;auto bank=policy(1,{&b,1},true);
    auto interrupted=outcome();interrupted.prefix=prefix(Channel::Interrupted);
    interrupted.terminal.own_carried={14,true};interrupted.tail.liquidation_class=8;
    interrupted.tail.liquidation_known=false;auto candidate=policy(2,{&interrupted,1});
    CHECK(payout_total(interrupted,Side::Own).value==0);
    CHECK(interrupted.prefix.carried.value==14&&interrupted.prefix.channel==Channel::Interrupted);
    CHECK(compare(candidate,bank).reason==Reason::UnknownTail&&select(candidate,bank).id==1);
    // Killing a bag-carrying enemy transfers custody, not scored gems.
    auto transfer=outcome();transfer.prefix=prefix(Channel::None);
    transfer.prefix.custody=Custody::TransferredToOwn;transfer.prefix.carried={28,true};
    transfer.terminal.own_carried={28,true};transfer.tail.liquidation_class=12;
    candidate.outcomes={&transfer,1};
    CHECK(payout_total(transfer,Side::Own).value==0);
    CHECK(compare(candidate,bank).reason==Reason::ResidualCapital&&select(candidate,bank).id==1);
    // Death transfers to the foe are also custody, not an invented foe score.
    transfer.prefix.alive={false,true};transfer.prefix.custody=Custody::TransferredToFoe;
    transfer.prefix.carried={0,true};transfer.terminal.owner_alive={false,true};
    transfer.terminal.own_carried={0,true};transfer.terminal.foe_carried={14,true};
    CHECK(payout_total(transfer,Side::Foe).value==0&&select(candidate,bank).id==1);
}
void exact_dated_context(){
    auto a=outcome(),b=outcome();auto candidate=policy(2,{&a,1}),bank=policy(1,{&b,1},true);
    CHECK(compare(candidate,bank).preference==Preference::Tie);
    auto mismatch=[&](const Context& c){candidate.context=c;CHECK(compare(candidate,bank).reason==Reason::ContextMismatch);CHECK(select(candidate,bank).id==1);};
    auto c=context();c.owner++;mismatch(c);c=context();c.horizon.round++;mismatch(c);
    c=context();c.start.order++;mismatch(c);c=context();c.observation++;mismatch(c);
    c=context();c.effect++;mismatch(c);c=context();c.input++;mismatch(c);
    c=context();c.domain.binding++;mismatch(c);c=context();c.domain.replies++;mismatch(c);
    c=context();c.domain.assumptions++;mismatch(c);c=context();c.domain.services++;mismatch(c);
    c=context();c.initial_carried++;mismatch(c);c=context();c.initial_own_score++;mismatch(c);
    candidate.context=context();candidate.context.domain.known=false;
    CHECK(compare(candidate,bank).reason==Reason::InvalidContext);
    candidate.context=context();candidate.context.horizon={39,0};
    CHECK(compare(candidate,bank).reason==Reason::InvalidContext);
}
void envelope_and_unknown_are_not_free_work(){
    const std::array<ScoreEvent,1> earned{{{{43,1},Side::Own,14}}};
    auto a=outcome(),b=outcome();b.scores=earned;
    auto candidate=policy(2,{&a,1}),bank=policy(1,{&b,1},true);
    candidate.metadata.assurance=std::numeric_limits<std::uint64_t>::max();candidate.metadata.envelope=999;
    CHECK(compare(candidate,bank).preference==Preference::Incumbent);
    a.evidence=Evidence::EnvelopeOnly;a.scores=earned;
    CHECK(compare(candidate,bank).reason==Reason::EnvelopeExcluded&&select(candidate,bank).id==1);
    a=outcome();a.scores=earned;a.tail.displaced.foe_known=false;
    unsigned calls=0;Options options{choose_candidate,&calls};
    CHECK(compare(candidate,bank,options).reason==Reason::UnknownService&&calls==0);
    CHECK(select(candidate,bank,options).id==1&&calls==0);
    a.tail.displaced.foe_known=true;a.tail.displaced.included_in_scores=false;
    CHECK(compare(candidate,bank).reason==Reason::UnknownService);
    a.tail.displaced.included_in_scores=true;a.tail.recovery_known=false;
    CHECK(compare(candidate,bank).reason==Reason::UnknownTail);
    a.tail.recovery_known=true;a.terminal.own_service_ready.known=false;
    CHECK(compare(candidate,bank).reason==Reason::UnknownTail);
    a=outcome();a.legal_reply=false;
    CHECK(compare(candidate,bank).reason==Reason::InvalidOutcome);
    a=outcome();a.coherent=false;
    CHECK(compare(candidate,bank).reason==Reason::InvalidOutcome);
}
void deadline_win_beats_idle(){
    const std::array<ScoreEvent,1> scored{{{{43,20},Side::Own,14}}};
    auto b=outcome(),idle=outcome();b.scores=scored;
    idle.terminal.own_service_ready={{40,10},true};idle.terminal.own_carried={14,true};idle.tail.liquidation_class=7;
    auto bank=policy(1,{&b,1},true),candidate=policy(2,{&idle,1});
    bank.context.initial_own_score=candidate.context.initial_own_score=286;
    bank.context.race_target=candidate.context.race_target={300,true};
    CHECK(compare(candidate,bank).preference==Preference::Incumbent);
    CHECK(compare(candidate,bank).reason==Reason::Race&&select(candidate,bank).id==1);
    unsigned calls=0;CHECK(compare(candidate,bank,{choose_candidate,&calls}).preference==Preference::Incumbent&&calls==0);
    // A forecast that lets the foe win first cannot combine its later bank
    // with a different branch's surviving/denying state.
    const std::array<ScoreEvent,2> lost{{{{42,10},Side::Foe,300},{{43,20},Side::Own,14}}};
    idle.scores=lost;idle.terminal.own_carried={0,true};
    CHECK(compare(candidate,bank).preference==Preference::Incumbent);
    const std::array<ScoreEvent,2> unknown_order{{{{43,20},Side::Own,14},{{43,20},Side::Foe,300}}};
    idle.scores=unknown_order;
    CHECK(compare(candidate,bank,{choose_candidate,&calls}).reason==Reason::UnresolvedRace&&calls==0);
    CHECK(select(candidate,bank).id==1);
}
void paired_scenarios_and_dates(){
    const std::array<ScoreEvent,1> fourteen{{{{43,1},Side::Own,14}}};
    const std::array<ScoreEvent,1> fifteen{{{{43,1},Side::Own,15}}};
    const std::array<ScoreEvent,1> thirteen{{{{43,1},Side::Own,13}}};
    std::array<Outcome,2> bs{outcome(1),outcome(2)},as{outcome(2),outcome(1)};
    for(auto& o:bs)o.scores=fourteen;as[0].scores=thirteen;as[1].scores=fifteen;
    auto bank=policy(1,bs,true),candidate=policy(2,as);
    CHECK(compare(candidate,bank).preference==Preference::Incomparable&&select(candidate,bank).id==1);
    unsigned calls=0;CHECK(compare(candidate,bank,{choose_candidate,&calls}).preference==Preference::Candidate&&calls==1);
    as[0].scores=fifteen;CHECK(compare(candidate,bank).preference==Preference::Candidate);
    CHECK(select(candidate,bank).id==2);
    as[0].reply={3,9};CHECK(compare(candidate,bank).reason==Reason::MissingReply);
    as[0].reply=as[1].reply;CHECK(compare(candidate,bank).reason==Reason::DuplicateReply);
    as[0].reply={2,9};bs[1].reply=bs[0].reply;CHECK(compare(candidate,bank).reason==Reason::DuplicateReply||compare(candidate,bank).reason==Reason::MissingReply);
    bs[1].reply={2,9};as[0].reply.revision++;
    CHECK(compare(candidate,bank).reason==Reason::MissingReply);
    candidate.outcomes={as.data(),1};CHECK(compare(candidate,bank).reason==Reason::MissingReply);
    candidate.outcomes=as;as[0].reply={2,9};as[0].evidence=Evidence::Actual;bs[1].evidence=Evidence::Actual;
    CHECK(compare(candidate,bank).reason==Reason::EvidenceMismatch);
    const std::array<ScoreEvent,1> later{{{{43,2},Side::Own,14}}};
    auto a=outcome(),b=outcome();a.scores=later;b.scores=fourteen;
    candidate=policy(2,{&a,1});bank=policy(1,{&b,1},true);
    CHECK(compare(candidate,bank).preference==Preference::Incumbent); // same total, later receipt
    a.scores=fourteen;CHECK(compare(candidate,bank).preference==Preference::Tie&&select(candidate,bank).id==1);
}
void callback_is_explicit_model_policy(){
    const std::array<ScoreEvent,2> aggressive{{{{43,1},Side::Own,20},{{43,2},Side::Foe,14}}};
    const std::array<ScoreEvent,1> banked{{{{43,1},Side::Own,14}}};
    auto a=outcome(),b=outcome();a.scores=aggressive;b.scores=banked;
    auto candidate=policy(2,{&a,1}),bank=policy(1,{&b,1},true);unsigned calls=0;
    CHECK(compare(candidate,bank).preference==Preference::Incomparable);
    auto compared=compare(candidate,bank,{choose_candidate,&calls});
    CHECK(compared.preference==Preference::Candidate&&compared.approximate_choice&&compared.reason==Reason::ModelCallback&&calls==1);
    a.evidence=Evidence::Actual;b.evidence=Evidence::SupportedConservation;
    CHECK(compare(candidate,bank,{choose_candidate,&calls}).preference==Preference::Incomparable&&calls==1);
    a.scores=banked;CHECK(compare(candidate,bank).preference==Preference::Tie);
    a.evidence=Evidence::ModelForecast;CHECK(compare(candidate,bank,{choose_candidate,&calls}).reason==Reason::EvidenceMismatch&&calls==1);
}
void invalid_and_overflow(){
    auto a=outcome(),b=outcome();auto candidate=policy(2,{&a,1}),bank=policy(1,{&b,1},true);
    const std::array<ScoreEvent,1> big{{{{43,1},Side::Own,14}}};a.scores=big;
    candidate.context.initial_own_score=bank.context.initial_own_score=std::numeric_limits<std::uint64_t>::max()-10;
    CHECK(compare(candidate,bank).reason==Reason::Overflow&&select(candidate,bank).id==1);
    const std::array<ScoreEvent,2> overflow{{{{43,1},Side::Own,std::numeric_limits<std::uint64_t>::max()},{{43,2},Side::Own,1}}};
    a.scores=overflow;CHECK(!payout_total(a,Side::Own).known);
    candidate.context=bank.context=context();
    const std::array<ScoreEvent,2> reversed{{{{44,1},Side::Own,1},{{43,1},Side::Own,1}}};
    a.scores=reversed;CHECK(compare(candidate,bank).reason==Reason::InvalidOutcome);
    const std::array<ScoreEvent,1> outside{{{{50,101},Side::Own,1}}};
    a.scores=outside;CHECK(compare(candidate,bank).reason==Reason::InvalidOutcome);
    a=outcome();a.prefix=prefix();a.prefix.refused=1;
    CHECK(compare(candidate,bank).reason==Reason::InvalidOutcome);
    a=outcome();a.prefix=prefix();a.prefix.applied=0;a.prefix.refused=1;
    CHECK(compare(candidate,bank).preference==Preference::Tie); // actual refusal itself mints no value
    a.prefix.evidence=Evidence::ModelForecast;
    CHECK(compare(candidate,bank).reason==Reason::InvalidOutcome);
    a.prefix=prefix();a.prefix.channel=static_cast<Channel>(99);
    CHECK(compare(candidate,bank).reason==Reason::InvalidOutcome);
    a=outcome();a.evidence=static_cast<Evidence>(99);
    CHECK(!payout_total(a,Side::Own).known);
    a=outcome();CHECK(!payout_total(a,static_cast<Side>(99)).known);
    candidate.complete=false;CHECK(compare(candidate,bank).reason==Reason::MissingPolicy);
    candidate.complete=true;bank.complete_economic_incumbent=false;
    CHECK(compare(candidate,bank).reason==Reason::MissingCompleteIncumbent&&select(candidate,bank).id==1);
    bank.complete_economic_incumbent=true;
    std::array<Outcome,scenario_limit+1> many{};
    for(std::size_t n=0;n<many.size();++n)many[n]=outcome(n+1);
    candidate.outcomes=many;CHECK(compare(candidate,bank).reason==Reason::MissingPolicy);
    std::array<ScoreEvent,score_event_limit+1> events{};
    for(auto& e:events)e={{43,1},Side::Own,0};
    a=outcome();a.scores=events;candidate.outcomes={&a,1};
    CHECK(compare(candidate,bank).reason==Reason::InvalidOutcome);
}
}
int main(){
    start_is_not_score();interrupted_and_transferred_capital();exact_dated_context();
    envelope_and_unknown_are_not_free_work();deadline_win_beats_idle();paired_scenarios_and_dates();
    callback_is_explicit_model_policy();invalid_and_overflow();
    std::printf("capital conversion value: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
