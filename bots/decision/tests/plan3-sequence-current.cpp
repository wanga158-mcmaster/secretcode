#include "../sequence-common/policy.h"
#include <cstdio>
#include <cstdlib>

namespace sequence=plan3_sequence;
unsigned checks=0;
void require(bool value,const char* text,int line) {
    ++checks;if(!value) {std::fprintf(stderr,"sequence:%d: %s\n",line,text);std::exit(1);}
}
#define CHECK(value) require(bool(value),#value,__LINE__)
plan3::InformationState snapshot() {
    plan3::InformationState info;info.round=20;info.roundLimit=300;info.winningGems=1000;
    info.health=info.maximumHealth=100;info.actorOrdinal=11;return info;
}
plan3::JobForecast forecast(int key,int reward,int duration,int worst) {
    plan3::JobForecast job;job.id={plan3::JobKind::cycle,key,-1,7,-1};
    for(unsigned world=0;world<4;++world) {
        plan3::Outcome outcome;outcome.response=static_cast<plan3::ResponseWorld>(world);outcome.tau=duration;
        outcome.deltaS=world==1?worst:reward;outcome.conversions={{duration,outcome.deltaS}};
        outcome.terminal.health=outcome.terminal.maximumHealth=100;job.outcomes.push_back(outcome);
    }
    return job;
}
plan3::PolicyEvent commit(const sequence::Selection& selected,int round) {
    return {plan3::PolicyEvent::Kind::committed,selected.intent.policyToken,*selected.intent.job,round,round,std::nullopt,false};
}
plan3::PolicyEvent endpoint(const sequence::Selection& selected,int start,int end,std::optional<unsigned> exact,bool completed=true) {
    return {completed?plan3::PolicyEvent::Kind::completed:plan3::PolicyEvent::Kind::aborted,
        selected.intent.policyToken,*selected.intent.job,start,end,exact,completed};
}
void tables() {
    std::array<bool,64> slots{};
    for(unsigned cursor=0;cursor<64;++cursor) {const auto slot=sequence::ticket_slot(cursor);CHECK(slot<64 && !slots[slot]);slots[slot]=true;CHECK(sequence::ticket_slot(cursor+64)==slot);}
    for(bool control:{false,true})for(unsigned root=1;root<=3;++root) {
        auto count=sequence::tickets(control,false,root,0,sequence::Signal::unavailable,root);CHECK(count && *count<=64);
        for(unsigned first=0;first<2;++first)for(unsigned signal=0;signal<3;++signal)for(unsigned mask=1;mask<=3;++mask) {
            count=sequence::tickets(control,true,root,first,static_cast<sequence::Signal>(signal),mask);
            if(!(root&(1u<<first))) {CHECK(!count);continue;}
            CHECK(count && *count<=64);unsigned zeros=0;
            for(unsigned cursor=0;cursor<64;++cursor) {
                const unsigned action=sequence::ticket_slot(cursor)<*count?0:1;CHECK(mask&(1u<<action));zeros+=action==0;
            }
            CHECK(zeros==*count);
        }
    }
    CHECK(!sequence::tickets(false,false,0,0,sequence::Signal::unavailable,3));
    CHECK(!sequence::tickets(false,false,1,0,sequence::Signal::unavailable,3));
}
void mapping() {
    const auto info=snapshot();std::vector<plan3::JobForecast> jobs{forecast(9,12,4,12),forecast(4,20,2,4)};
    auto map=sequence::map_jobs(info,jobs,[] {return true;});CHECK(map.state==sequence::State::known && map.availability==3);
    CHECK(map.roles[0]->id.firstGroup==4 && map.roles[1]->id.firstGroup==9);
    jobs.push_back(forecast(4,999,1,999));map=sequence::map_jobs(info,jobs,[] {return true;});
    CHECK(map.roles[0]->converted==20); // Shared stable FIRST duplicate.
    jobs.pop_back();jobs[1].outcomes.pop_back();map=sequence::map_jobs(info,jobs,[] {return true;});
    CHECK(map.availability==1 && map.omitted==1 && map.roles[0]->id.firstGroup==9);
    jobs={forecast(4,20,129,4)};CHECK(sequence::map_jobs(info,jobs,[] {return true;}).state==sequence::State::unavailable);
    jobs={forecast(9,12,4,12),forecast(4,20,2,4)};
    unsigned calls=0;sequence::Planner baseline;
    CHECK(baseline.preview(false,info,jobs,{},[&] {++calls;return true;}).state==sequence::State::known);
    for(unsigned cut=0;cut<calls;++cut) {
        sequence::Planner current;unsigned at=0;const auto choice=current.preview(false,info,jobs,{},[&] {return at++!=cut;});
        CHECK(choice.state==sequence::State::cutoff && !choice.intent.job && current.statistics().committed==0);
    }
    const sequence::Baseline before{info.actorOrdinal,0,100};auto later=info;
    CHECK(sequence::literal_signal(later,before)==sequence::Signal::unavailable);
    later.opponentPermanentScore=1;CHECK(sequence::literal_signal(later,before)==sequence::Signal::public_score_rise);
    later.health=99;CHECK(sequence::literal_signal(later,before)==sequence::Signal::personal_detection);
    later.health=100;plan3::ChampionObservation enemy;enemy.position.value=plan3::Position{12};
    enemy.position.provenance=plan3::Provenance::personally_observed;enemy.position.observedRound=later.round-1;
    later.champions={enemy};CHECK(sequence::literal_signal(later,before)==sequence::Signal::public_score_rise);
    later.champions[0].position.observedRound=later.round;CHECK(sequence::literal_signal(later,before)==sequence::Signal::personal_detection);
}
void episodes() {
    auto info=snapshot();const std::vector<plan3::JobForecast> jobs{forecast(9,12,4,12),forecast(4,20,2,4)};
    for(bool control:{false,true}) {
        info=snapshot();
        sequence::Planner planner(control);
        const auto first=planner.preview(control,info,jobs);const auto companion=planner.preview(!control,info,jobs);
        CHECK(first.state==sequence::State::known && first.intent.policyToken==1 && !companion.intent.policyToken);
        CHECK(first.slot==companion.slot && planner.statistics().committed==0 && first.action==0);
        CHECK(planner.observe(commit(first,20)) && planner.statistics().committed==1);
        plan3::ControllerContext context;context.activeJob=first.intent.job;
        CHECK(planner.preview(control,info,jobs,context).intent.status==plan3::IntentStatus::preserve);
        CHECK(planner.preview(control,info,jobs).state==sequence::State::invalid);
        auto wrong=endpoint(first,20,25,20);++wrong.token;CHECK(!planner.observe(wrong));
        CHECK(planner.observe(endpoint(first,20,25,20)) && planner.waiting_second());
        info.round=25;info.opponentPermanentScore=1;
        const auto second=planner.preview(control,info,jobs);CHECK(second.second && second.signal==sequence::Signal::public_score_rise);
        CHECK(second.intent.policyToken==2 && second.slot==32 && planner.statistics().firstCompleted==1);
        CHECK(planner.observe(commit(second,25)));
        CHECK(planner.observe(endpoint(second,25,30,12)) && !planner.waiting_second() && planner.statistics().episodes==1);
        info=snapshot();info.round=30;
        const auto third=planner.preview(control,info,jobs);CHECK(!third.second && third.intent.policyToken==3);
        CHECK(planner.observe(commit(third,30)));CHECK(planner.observe(endpoint(third,30,31,std::nullopt)));
        CHECK(!planner.waiting_second() && planner.statistics().censored==1);
    }
    for(const auto exact:{std::optional<unsigned>{},std::optional<unsigned>{0}}) {
        sequence::Planner planner;info=snapshot();const auto first=planner.preview(false,info,jobs);
        CHECK(planner.observe(commit(first,20)));CHECK(planner.observe(endpoint(first,20,25,exact)));
        CHECK(!planner.waiting_second() && planner.statistics().firstCompleted==0 && planner.statistics().censored==1);
    }
    sequence::Planner planner;info=snapshot();const auto first=planner.preview(false,info,jobs);
    CHECK(planner.observe(commit(first,20)));CHECK(planner.observe(endpoint(first,20,21,std::nullopt,false)));
    CHECK(!planner.waiting_second() && planner.statistics().censored==1);
}
int main() {
    tables();mapping();episodes();std::printf("SEQUENCE_RESULT {\"checks\":%u,\"nativeCalls\":0}\n",checks);
}
