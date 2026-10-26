#include "../common/src/native_economy.h"
#include <cstdio>
#include <cstdlib>
using namespace p8::native::economy;
namespace {
int checks=0;
void check(bool value,const char* message) {
    ++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
template<class T> p6::Fact<T> fact(T value){return {value,p6::Source::synthetic_fixture};}
struct Fixture {
    p6::Schedule schedule{};
    std::array<BankVisit,3> visits{};
    std::array<p6::CycleCertificate,3> safety{};
    TributeTiming timing{{.1,p6::Source::queried_setting},{0,p6::Source::queried_setting},{1,p6::Source::queried_setting}};
};
void setup(Fixture& f,std::array<int,3> bags={101,51,1},double ambient=0) {
    static const Fixture empty{};f=empty;
    auto& s=f.schedule;s.actor_count=3;s.production_count=1;s.target_id=7;
    auto& p=s.productions[0];p.event_id=9;p.tick=1;p.gems=fact<p6::Gems>(9);p.gold=fact<p6::Gems>(0);
    p.holders={1,2,3};p.holder_count=3;p.source=p6::Source::synthetic_fixture;
    for(int i=0;i<3;++i) {
        auto& a=s.actors[i];a.actor_id=i+1;a.initial_bag=fact<p6::Gems>(bags[i]);a.initial_gold=fact<p6::Gems>(0);
        const int duration=tribute_ticks(bags[i]+9,f.timing),bank=10+duration;
        a.occupied_turns=fact<p6::Gems>(bank+2);a.event_count=3;
        a.events[0]={p6::EventKind::production,9,1,0,fact<p6::Gems>(0),-1,p6::Source::synthetic_fixture};
        a.events[1]={p6::EventKind::tribute,20+i,bank,-1,fact<p6::Gems>(0),-1,p6::Source::synthetic_fixture};
        a.events[2]={p6::EventKind::renewal,30+i,bank+2,-1,fact<p6::Gems>(0),7,p6::Source::synthetic_fixture};
        f.visits[i]={i+1,{4,4},bags[i]+9,ambient};
        auto& c=f.safety[i];c.actor=i+1;c.group=7;
        c.clear=c.hold=c.bank=c.recovery={1000,1000,0,0,0,0,true};
    }
}
p6::ScoreEvaluation rank(const p6::Schedule& schedule,int window) {
    p6::ScenarioWindows windows;windows.deadlines=windows.windows={window,window,window};
    windows.sampled_actor_id=1;windows.sampled_round=0;windows.source=p6::Source::public_map;windows.valid=true;
    return p6::evaluate_schedule(schedule,windows,{2000,1000,5},{0,1},0);
}
}
int main() {
    static Fixture f;setup(f);
    check(tribute_ticks(10,f.timing)==1&&tribute_ticks(11,f.timing)==2,
          "queried tribute formula respects the ten-to-eleven gem channel boundary");
    const auto before=rank(f.schedule,20);check(before.eligible()&&before.min_margin==58,
          "hand oracle exposes 54 plus 4 gems incorrectly secured before a shared bank can serve the crew");
    const auto queued=queue_schedule(f.schedule,f.visits,f.safety,f.timing);
    check(queued.valid&&queued.waiting==std::array<int,6>{11,16,21,0,0,0},
          "current loaded bags plus actual shared source shares determine worst competing service waits");
    check(queued.bank_delay[0]==12&&queued.bank_delay[1]==17&&queued.bank_delay[2]==22&&
          queued.renewal_delay[0]==13&&queued.renewal_delay[1]==18&&queued.renewal_delay[2]==23,
          "bank completion, entry movement and physical vacating are charged separately");
    check(rank(f.schedule,20).min_margin==0,"a tight deadline cannot secure score from overlapping bank channels");
    const auto after=rank(f.schedule,90);
    check(after.eligible()&&after.min_margin==162&&f.schedule.production_count==1&&
          f.schedule.productions[0].gems.value==9&&f.schedule.productions[0].holder_count==3,
          "J source payout stays once-only: 153 current gems plus exactly nine newly captured gems");
    check(f.schedule.actors[0].occupied_turns.value==36&&f.schedule.actors[1].occupied_turns.value==36&&
          f.schedule.actors[2].occupied_turns.value==36,
          "even a harmless camp with no ambient damage still pays bank occupancy and vacating work");
    check(f.safety[2].bank.incoming_total==0&&f.safety[2].recovery.incoming_total==0,
          "queue time does not invent damage when the observed ambient bound is zero");
    setup(f,{101,51,1},2);
    check(queue_schedule(f.schedule,f.visits,f.safety,f.timing).valid&&
          f.safety[0].clear.incoming_total==0&&f.safety[0].hold.incoming_total==0&&
          f.safety[0].bank.incoming_total==24&&f.safety[0].recovery.incoming_total==26,
          "queue exposure is cumulative after clearance, through banking and subsequent exit");
    setup(f,{0,0,0});
    check(queue_schedule(f.schedule,f.visits,f.safety,f.timing).valid&&f.schedule.actors[0].events[1].tick==18,
          "empty initial bags still collide after one new shared payout and pay both other one-tick channels");
    setup(f,{700,700,700});const auto unchanged=f.schedule.actors[0].events[1].tick;
    check(!queue_schedule(f.schedule,f.visits,f.safety,f.timing).valid&&f.schedule.actors[0].events[1].tick==unchanged,
          "individually bounded long channels whose coupled queue exceeds 120 are rejected without partial mutation");
    setup(f,{101,51,1},100);
    check(!queue_schedule(f.schedule,f.visits,f.safety,f.timing).valid&&f.safety[0].bank.incoming_total==0,
          "individually unsafe waiting cannot become a feasible queue or partially overwrite safety evidence");
    setup(f);for(int i=0;i<3;++i)f.visits[i].approach={4+i,4};
    check(queue_schedule(f.schedule,f.visits,f.safety,f.timing).valid&&f.schedule.actors[0].events[1].tick==21,
          "distinct bank approach tiles are independent services and gain no artificial queue delay");
    setup(f);f.timing.multiplier.source=p6::Source::unknown;
    check(!queue_schedule(f.schedule,f.visits,f.safety,f.timing).valid,"unqueried channel timing cannot certify bank serialization");
    setup(f);f.schedule.productions[0].holders[1]=1;
    check(!queue_schedule(f.schedule,f.visits,f.safety,f.timing).valid,"duplicate holders cannot fabricate a lower bank bag");
    setup(f);f.schedule.actors[0].events[2].kind=p6::EventKind::tribute;
    check(!queue_schedule(f.schedule,f.visits,f.safety,f.timing).valid,"unsupported multiple bank visits remain unresolved");
    setup(f,{0,0,0});
    const auto original=f.schedule.productions[0];f.schedule.production_count=3;
    for(int i=0;i<3;++i) {
        auto& source=f.schedule.productions[i];source=original;source.event_id=9+i;source.holders={i+1};source.holder_count=1;
        f.schedule.actors[i].events[0].event_id=9+i;f.schedule.actors[i].events[0].production_index=i;
    }
    check(queue_schedule(f.schedule,f.visits,f.safety,f.timing).valid&&rank(f.schedule,90).min_margin==27&&
          f.schedule.actors[0].events[1].tick==18&&f.schedule.production_count==3,
          "independent distinct-source alternatives pay the same shared-approach queue cost as the coalition arm");
    setup(f);
    static const std::array<p6::Point,2> left{{{3,4},{4,4}}},right{{{5,4},{4,4}}},above{{{4,3},{4,4}}};
    f.visits[0].bank_path=left;f.visits[0].exit={3,4};
    f.visits[1].bank_path=right;f.visits[1].exit={5,4};
    f.visits[2].bank_path=above;f.visits[2].exit={4,3};
    check(queue_exits_clear(f.visits),"separate approach directions leave each server an actual unoccupied exit");
    f.visits[1].bank_path=left;
    check(!queue_exits_clear(f.visits),"a follower blocking the server's reverse exit makes this queue geometry unsupported");
    std::printf("plan8-native-economy: %d checks passed; pure queue bounds only\n",checks);
}
