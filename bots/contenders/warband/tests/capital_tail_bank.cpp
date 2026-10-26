#include "capital_order.h"
#include "capital_bank.h"
#include <cstdio>
using namespace wb::capital;
static unsigned checks=0,failures=0;
static void check(bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}}
static order::Packet promoted(unsigned enemies=3) {
    order::Packet p;p.fresh=p.owner_action_phase=p.next_starter_known=p.own_starts_next=true;p.self_id=2;
    p.own.count=3;p.own.complete=true;p.enemy.count=enemies;p.enemy.complete=true;
    for(unsigned i=0;i<3;++i)p.own.members[i]={static_cast<int>(i),true,i!=2,true,i==2?1:0,order::Commitment::CallerFixed,true,static_cast<int>(i)};
    for(unsigned i=0;i<enemies;++i) {
        auto& m=p.enemy.members[i];m.id=10+i;m.completion_known=i<3;m.completed_current=i<3;
        // Their next priorities and tie orders remain entirely unknown.
    }
    return p;
}
int main() {
    auto packet=promoted();auto slots=order::derive(packet);
    for(int foe=10;foe<13;++foe)check(slots.for_id(foe).admitted&&slots.for_id(foe).scheduled_upper==0,"strict promotion excludes every future actor input");
    bank::Observation before;before.owner=2;before.round=42;before.position={8,4};
    before.hp=37;before.bag=8;before.team_score=100;before.channel=bank::Channel::None;
    before.observed=before.fresh=before.alive=before.submit_allowed=true;
    bank::Context c;c.owner=2;c.round=42;c.altar={9,4};c.queried_ticks=1;
    c.fresh=c.altar_known=c.adjacent=c.altar_unlocked=true;
    c.foes_covered=c.npc_covered=c.active_effects_covered=true;
    c.survives_horizon=c.no_stun_horizon=true;
    bank::Monitor monitor;(void)monitor.observe(before,c);
    const auto word=monitor.plan(before,c);check(word.status==bank::Status::Admitted,"complete zero-input conversion is admitted with low actual HP");
    auto post=before;post.channel=bank::Channel::Submit;post.submit_allowed=false;
    auto accepted=monitor.accepted_start(word,true,post,c);
    check(accepted.outcome==bank::Outcome::AwaitOwnerStart&&accepted.credited_prior_bag==0,"actual start is not score");
    auto next=before;next.round=43;next.bag=0;next.team_score=108;c.round=43;
    auto paid=monitor.observe(next,c);check(paid.outcome==bank::Outcome::Banked&&paid.credited_prior_bag==8,"actual next owner-start bank closes the word");
    packet=promoted(4);slots=order::derive(packet);
    check(slots.for_id(13).scheduled_upper==1,"enemy extra body keeps its remaining current input");
    c.round=42;c.foe_opportunities_upper=slots.for_id(13).scheduled_upper;c.full_reply_horizon=false;
    check(bank::admit(before,c).status==bank::Status::NeedModel,"uncovered enemy tail cannot borrow the zero-input proof");
    packet=promoted();packet.own_starts_next=false;slots=order::derive(packet);
    check(slots.for_id(10).scheduled_upper==1,"opposite next starter permits an earlier enemy input");
    packet=promoted();packet.own.members[1].next_priority=1;slots=order::derive(packet);
    check(slots.own_next_rank.min==1&&slots.for_id(10).next_max==1,"tied request is not strict promotion");
    packet=promoted();packet.own.members[0].commitment=order::Commitment::Unknown;slots=order::derive(packet);
    check(slots.own_next_rank.max>0,"pending ally priority can invalidate first rank");
    packet=promoted();packet.fresh=false;slots=order::derive(packet);
    check(slots.status==order::Status::Fallback&&slots.for_id(10).scheduled_upper==2,"old promotion packet supplies no current authority");
    c.foe_opportunities_upper=0;c.foes_covered=true;c.npc_covered=false;
    check(bank::admit(before,c).status==bank::Status::NeedModel,"zero actor inputs do not remove the camp phase");
    c.npc_covered=true;c.active_effects_covered=false;
    check(bank::admit(before,c).status==bank::Status::NeedModel,"zero actor inputs do not erase ongoing world effects");
    std::printf("capital_tail_bank: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
