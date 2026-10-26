#include "capital_order.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <vector>
using namespace wb::capital::order;
static unsigned checks=0,failures=0;
static void check(bool ok,const char* label) {
    ++checks;if(!ok){if(failures<8)std::printf("FAIL %s\n",label);++failures;}
}
static Roster roster(unsigned n,int base) {
    Roster out;out.count=n;out.complete=true;
    for(unsigned i=0;i<n;++i)out.members[i]={base+static_cast<int>(i),true,true,true,0,
        Commitment::CallerFixed,true,static_cast<int>(i)};
    return out;
}
static Packet packet(unsigned own=3,unsigned enemy=3) {
    Packet p;p.own=roster(own,0);p.enemy=roster(enemy,10);p.self_id=0;
    p.own.members[0].completed_current=false;
    p.fresh=p.owner_action_phase=p.next_starter_known=true;p.own_starts_next=true;return p;
}
static std::vector<unsigned> actual_order(const Roster& roster) {
    std::vector<unsigned> out;for(unsigned i=0;i<roster.count;++i)out.push_back(i);
    std::sort(out.begin(),out.end(),[&](unsigned a,unsigned b) {
        const auto& x=roster.members[a];const auto& y=roster.members[b];
        return x.next_priority!=y.next_priority?x.next_priority>y.next_priority:x.tie_key<y.tie_key;
    });return out;
}
// Ground truth physically emits each complete next-round schedule, including
// the longer team's suffix, and stops at the selected owner's next input.
static std::array<unsigned,max_team> oracle(const Packet& p) {
    const auto own=actual_order(p.own),enemy=actual_order(p.enemy);
    std::array<unsigned,max_team> counts{};
    for(unsigned rank=0;rank<std::max(own.size(),enemy.size());++rank) {
        for(unsigned turn=0;turn<2;++turn) {
            const bool ours=turn==0?p.own_starts_next:!p.own_starts_next;
            if(ours) {
                if(rank<own.size()&&p.own.members[own[rank]].id==p.self_id)return counts;
            } else if(rank<enemy.size())++counts[enemy[rank]];
        }
    }
    return counts;
}
int main() {
    // Every 3-body priority assignment over {-1,0,1}, every actual tie order,
    // every commitment subset and every visible tie-key subset. Unknown facts
    // are removed only from the model; the oracle retains the complete world.
    for(unsigned code=0;code<27;++code) {
        auto world=roster(3,0);unsigned value=code;
        for(unsigned i=0;i<3;++i){world.members[i].next_priority=static_cast<int>(value%3)-1;value/=3;}
        std::array<int,3> tie{0,1,2};
        do {
            for(unsigned i=0;i<3;++i)world.members[i].tie_key=tie[i];
            const auto actual=actual_order(world);
            for(unsigned commitment=0;commitment<8;++commitment)for(unsigned known_tie=0;known_tie<8;++known_tie) {
                auto partial=world;
                for(unsigned i=0;i<3;++i) {
                    partial.members[i].commitment=commitment&(1u<<i)?Commitment::CallerFixed:Commitment::Unknown;
                    partial.members[i].tie_key_known=(known_tie&(1u<<i))!=0;
                }
                for(unsigned target=0;target<3;++target) {
                    const auto bounds=rank(partial,target);
                    const unsigned truth=std::find(actual.begin(),actual.end(),target)-actual.begin();
                    check(bounds.valid&&bounds.min<=truth&&truth<=bounds.max,"rank encloses complete world");
                }
            }
        }while(std::next_permutation(tie.begin(),tie.end()));
    }
    // All permutations of unequal 3- and 4-body teams, all priority commitment
    // subsets, every selected owner, and both next-round team starters. Current
    // completion flags come from the actual opposite-parity emitted schedule.
    // A pending actor's CallerFixed case is an explicit fixed-policy model
    // premise; the separate mutation case below rejects inferring it from an
    // observed next priority alone.
    std::array<int,3> own_order{0,1,2};
    do {
        std::array<int,4> enemy_order{0,1,2,3};
        do {
            auto complete=packet(3,4);
            for(unsigned i=0;i<3;++i)complete.own.members[own_order[i]].next_priority=3-static_cast<int>(i);
            for(unsigned i=0;i<4;++i)complete.enemy.members[enemy_order[i]].next_priority=4-static_cast<int>(i);
            for(unsigned own_mask=0;own_mask<8;++own_mask)for(unsigned enemy_mask=0;enemy_mask<16;++enemy_mask)
            for(int owner=0;owner<3;++owner)for(unsigned starts=0;starts<2;++starts) {
                auto p=complete;p.self_id=owner;p.own_starts_next=starts!=0;
                auto previous=p;previous.own_starts_next=!p.own_starts_next;
                const auto already=oracle(previous);
                auto truth=oracle(p);
                const auto current_own=actual_order(p.own);
                const unsigned own_position=std::find(current_own.begin(),current_own.end(),static_cast<unsigned>(owner))-current_own.begin();
                for(unsigned rank=0;rank<current_own.size();++rank)p.own.members[current_own[rank]].completed_current=rank<own_position;
                for(unsigned i=0;i<4;++i) {
                    p.enemy.members[i].completed_current=already[i]!=0;
                    truth[i]+=1u-already[i];
                }
                for(unsigned i=0;i<3;++i)p.own.members[i].commitment=own_mask&(1u<<i)?Commitment::CallerFixed:Commitment::Unknown;
                for(unsigned i=0;i<4;++i)p.enemy.members[i].commitment=enemy_mask&(1u<<i)?
                    (p.enemy.members[i].completed_current?Commitment::CompletedInput:Commitment::CallerFixed):Commitment::Unknown;
                const auto result=derive(p);check(result.status!=Status::Fallback,"valid incomplete priorities remain bounded");
                for(unsigned i=0;i<4;++i) {
                    const auto w=result.for_id(10+static_cast<int>(i));
                    check(w.admitted&&w.scheduled_lower<=truth[i]&&truth[i]<=w.scheduled_upper,"input window encloses emitted schedule");
                    if(own_mask==7&&enemy_mask==15)check(result.status==Status::Exact&&w.scheduled_lower==truth[i]&&w.scheduled_upper==truth[i],"committed schedule exact");
                }
            }
        }while(std::next_permutation(enemy_order.begin(),enemy_order.end()));
    }while(std::next_permutation(own_order.begin(),own_order.end()));
    auto p=packet(1,6);p.own_starts_next=true;
    for(unsigned i=0;i<6;++i) {
        p.enemy.members[i].commitment=Commitment::Unknown;
        p.enemy.members[i].next_priority_known=false;
        p.enemy.members[i].completed_current=i==0;
    }
    auto r=derive(p);
    check(r.for_id(10).scheduled_upper==0,"own rankzero next-starter needs no enemy priority");
    for(int id=11;id<16;++id)check(r.for_id(id).scheduled_lower==1&&r.for_id(id).scheduled_upper==1,"pending body gets only current input in rankzero class");
    p.own_starts_next=false;r=derive(p);
    check(r.for_id(10).scheduled_upper==1&&r.for_id(11).scheduled_upper==2,"opposite starter retains extra input");
    p.next_starter_known=false;r=derive(p);
    check(r.for_id(10).next_min==0&&r.for_id(10).next_max==1,"unknown parity retains both schedules");
    p=packet(6,1);p.self_id=5;p.own.members[5].completed_current=false;r=derive(p);
    check(r.for_id(10).next_min==1&&r.for_id(10).next_max==1,"shorter enemy acts before appended own suffix");
    p=packet();p.enemy.members[0].completion_known=false;p.enemy.members[0].commitment=Commitment::Unknown;
    r=derive(p);check(r.for_id(10).current_min==0&&r.for_id(10).current_max==1,"unobserved current completion stays uncertain");
    p=packet();p.own.members[0].next_priority=INT_MIN;p.own.members[1].next_priority=INT_MAX;
    check(rank(p.own,0).min==2&&rank(p.own,1).max==0,"full int32 priorities compare without subtraction overflow");
    p=packet();p.enemy.members[0].commitment=Commitment::Unknown;
    p.enemy.members[0].completed_current=false;p.enemy.members[0].next_priority=INT_MAX;
    r=derive(p);check(r.for_id(10).next_rank.min==0&&r.for_id(10).next_rank.max==2,"observed pending priority is not committed");
    p=packet();for(unsigned i=0;i<3;++i) {
        p.own.members[i].commitment=Commitment::Unknown;
        p.own.members[i].next_priority_known=false;p.own.members[i].tie_key_known=false;
        p.enemy.members[i].commitment=Commitment::Unknown;p.enemy.members[i].next_priority_known=false;
    }
    p.own_rank_override_admitted=true;p.own_rank_override={0,0,true};r=derive(p);
    check(r.status==Status::Exact&&r.own_next_rank.min==0&&r.own_next_rank.max==0&&r.for_id(10).scheduled_upper==0,"independent admitted rankzero proof needs no guessed tie key");
    p.own_rank_override_admitted=false;r=derive(p);
    check(r.status==Status::Bounded&&r.for_id(10).scheduled_upper==1,"unadmitted rank hint confers no authority");
    auto fallback=[&](const Packet& invalid,Reason reason) {
        const auto result=derive(invalid);const auto window=result.for_id(10);
        check(result.status==Status::Fallback&&result.reason==reason&&!window.admitted&&window.scheduled_lower==0&&window.scheduled_upper==2,"invalid premise retains two-input fallback");
    };
    p=packet();p.fresh=false;fallback(p,Reason::Stale);
    p=packet();p.owner_action_phase=false;fallback(p,Reason::WrongPhase);
    p=packet();p.enemy.complete=false;fallback(p,Reason::IncompleteRoster);
    p=packet();p.own.count=7;fallback(p,Reason::InvalidRoster);
    p=packet();p.own.members[1].id=0;fallback(p,Reason::InvalidRoster);
    p=packet();p.enemy.members[0].id=0;fallback(p,Reason::InvalidRoster);
    p=packet();p.self_id=99;fallback(p,Reason::MissingSelf);
    p=packet();p.own.members[0].completed_current=true;fallback(p,Reason::WrongPhase);
    p=packet();p.enemy.members[0].commitment=Commitment::CompletedInput;p.enemy.members[0].completed_current=false;
    fallback(p,Reason::InconsistentCommitment);
    p=packet();p.enemy.members[0].next_priority_known=false;fallback(p,Reason::InconsistentCommitment);
    p=packet();p.enemy.members[1].tie_key=0;fallback(p,Reason::InvalidRoster);
    p=packet();p.own_rank_override_admitted=true;p.own_rank_override={0,3,true};fallback(p,Reason::InvalidOwnRank);
    p=packet();p.own_rank_override_admitted=true;p.own_rank_override={1,1,true};fallback(p,Reason::InvalidOwnRank);
    p=packet();check(!derive(p).for_id(99).admitted&&derive(p).for_id(99).scheduled_upper==2,"missing identity never means zero replies");
    check(!rank(Roster{},0).valid,"unvalidated direct rank is invalid and broad");
    std::printf("capital_order: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
