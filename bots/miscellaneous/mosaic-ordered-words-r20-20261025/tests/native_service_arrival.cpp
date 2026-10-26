#include "../src/native_service_arrival.h"
#include "../src/native_service_execution.h"
#include "../src/native_service_stationary_index.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

namespace N=mosaic::native_service;
namespace native_service=mosaic::native_service;
static unsigned checks{},groups{};
static void check(bool value){assert(value);++checks;}
static N::Epoch now{40,9,0};
static N::SelfFact self_fact(){
    N::SelfFact value{};value.epoch=now;value.id=4;value.pose={10,10};value.team=0;
    value.alive=value.raw_m=value.raw_a=value.raw_c=true;value.hp=value.max_hp=100;
    value.attack_range=1;value.move_speed=1;value.strength=20;
    value.body_actual=value.restrictions_actual=value.buffs_actual=value.channel_actual=true;
    value.walked_actual=value.channel_time_actual=value.gems_actual=value.custody_actual=true;return value;
}
static N::CampFact camp_fact(int ordinal=0,N::Point point={11,10}){
    N::CampFact value{};value.epoch=now;value.id=20+unsigned(ordinal);value.group_id=7;
    value.member_ordinal=ordinal;value.group_ordinal=2;value.pose=point;value.hp=120;
    value.life=N::Life::awake;value.life_actual=value.body_actual=value.buffs_actual=true;return value;
}
static N::GroupFact group_fact(N::GroupLife life=N::GroupLife::reward){
    N::GroupFact value{};value.epoch=now;value.id=7;value.ordinal=2;value.life=life;
    value.actual=value.visibility_actual=value.visible=true;return value;
}
static N::ArrivalBankOffer bank_offer(int ordinal=0,N::Point point={11,10}){
    N::ArrivalBankOffer value{};value.epoch=now;value.altar=point;value.ordinal=ordinal;value.range=1;
    value.static_altar=value.lock_actual=value.unlocked=true;return value;
}
static void work_facts(){
    auto self=self_fact();auto camp=camp_fact();
    auto work=N::arrival_work(self,camp,{},{},now);
    check(work.kind==N::ArrivalKind::work&&work.physical()&&work.needs==0);
    check(work.member_ordinal()==0&&work.group_ordinal()==2&&work.camp.id==20&&work.camp.group_id==7);
    const auto word=work.word();check(word.count==1&&word.epoch==now&&word.actions[0].kind==N::PrimitiveKind::basic&&word.actions[0].target_id==20);
    self.epoch.round--;check(N::arrival_work(self,camp,{},{},now).needs==N::arrival_need_self);self=self_fact();
    camp.epoch.revision++;check(N::arrival_work(self,camp,{},{},now).needs==N::arrival_need_life);camp=camp_fact();
    camp.body_actual=false;check(N::arrival_work(self,camp,{},{},now).needs==N::arrival_need_body);camp=camp_fact();
    camp.buffs_actual=false;check(N::arrival_work(self,camp,{},{},now).needs==N::arrival_need_buffs);camp=camp_fact();
    camp.life=N::Life::unknown;check(N::arrival_work(self,camp,{},{},now).needs==N::arrival_need_life);camp=camp_fact();
    camp.group_ordinal=-1;check(N::arrival_work(self,camp,{},{},now).needs==N::arrival_need_binding);camp=camp_fact();
    self.raw_a=false;check(N::arrival_work(self,camp,{},{},now).kind==N::ArrivalKind::none);self=self_fact();
    self.attack_restricted=true;check(!N::arrival_work(self,camp,{},{},now).physical());self=self_fact();
    self.stunned=true;check(N::arrival_work(self,camp,{},{},now).kind==N::ArrivalKind::none);self=self_fact();
    self.recalling=true;check(!N::arrival_work(self,camp,{},{},now).physical());self=self_fact();
    self.teleporting=true;check(!N::arrival_work(self,camp,{},{},now).physical());self=self_fact();
    self.pose={8,10};check(!N::arrival_work(self,camp,{},{},now).physical());self=self_fact();
    self.attack_range=3;self.pose={8,10};check(N::arrival_work(self,camp,{},{},now).physical());
    self.epoch.attempt=2;auto after=now;after.attempt=2;
    check(N::arrival_work(self,camp,{},{},after).physical()); // Same world after knowledge-only FALSE.
    check(!N::arrival_work(self,camp,{},{},after,false,false).physical());
    ++groups;
}
static void corpse_and_capture(){
    auto self=self_fact();auto camp=camp_fact();camp.life=N::Life::dead;camp.body_actual=camp.buffs_actual=false;
    check(N::arrival_work(self,camp,{},{},now).needs==N::arrival_need_group);
    auto group=group_fact();auto wrong=group;wrong.id++;
    check(N::arrival_work(self,camp,wrong,{},now).needs==N::arrival_need_group);
    auto decision=N::arrival_work(self,camp,group,{},now);
    check(decision.kind==N::ArrivalKind::capture&&!decision.physical()&&decision.needs==N::arrival_need_binding);
    N::CaptureFact capture{};capture.epoch=now;capture.group_id=group.id;capture.life=group.life;
    capture.life_actual=capture.on_spawn_actual=true;capture.on_spawn=false;
    decision=N::arrival_work(self,camp,group,capture,now);check(decision.kind==N::ArrivalKind::capture&&!decision.physical());
    capture.on_spawn=true;self.raw_a=false;
    decision=N::arrival_work(self,camp,group,capture,now);check(decision.kind==N::ArrivalKind::capture&&decision.physical()&&decision.word().count==0);
    capture.group_id++;check(!N::arrival_work(self,camp,group,capture,now).physical());capture.group_id=group.id;
    group.life=N::GroupLife::alive;check(N::arrival_work(self,camp,group,capture,now).kind==N::ArrivalKind::lifecycle);
    group.life=N::GroupLife::respawning;group.respawn_actual=true;group.respawn_time=4;
    decision=N::arrival_work(self,camp,group,capture,now);check(decision.kind==N::ArrivalKind::lifecycle&&decision.group.respawn_time==4);
    group.epoch.revision++;check(N::arrival_work(self,camp,group,capture,now).needs==N::arrival_need_group);
    check(N::arrival_work(self,camp,group,capture,now,false,false).kind==N::ArrivalKind::none);
    ++groups;
}
static void bank_facts(){
    auto self=self_fact();self.gems=30;auto offer=bank_offer();
    auto decision=N::arrival_bank(self,offer,now);
    check(decision.kind==N::ArrivalKind::bank&&decision.physical());
    check(decision.word().count==1&&decision.word().actions[0].kind==N::PrimitiveKind::bank&&decision.word().actions[0].point==offer.altar);
    self.gems_actual=false;check(N::arrival_bank(self,offer,now).needs==N::arrival_need_custody);self.gems_actual=true;
    self.gems=0;check(N::arrival_bank(self,offer,now).kind==N::ArrivalKind::none);self.gems=30;
    offer.static_altar=false;check(N::arrival_bank(self,offer,now).needs==N::arrival_need_binding);offer=bank_offer();
    offer.lock_actual=false;check(N::arrival_bank(self,offer,now).needs==N::arrival_need_lock);offer=bank_offer();
    offer.unlocked=false;decision=N::arrival_bank(self,offer,now);
    check(decision.kind==N::ArrivalKind::locked&&!decision.physical()&&decision.word().count==0);offer=bank_offer();
    offer.epoch.attempt++;check(N::arrival_bank(self,offer,now).needs==N::arrival_need_lock);offer=bank_offer();
    offer.altar={12,10};check(N::arrival_bank(self,offer,now).kind==N::ArrivalKind::none);offer=bank_offer();
    self.raw_a=false;check(N::arrival_bank(self,offer,now).physical());
    self.cast_restricted=true;check(N::arrival_bank(self,offer,now).physical());
    self.submit_started=true;check(!N::arrival_bank(self,offer,now).physical());self.submit_started=false;
    self.completed=true;check(!N::arrival_bank(self,offer,now).physical());self.completed=false;
    self.stunned=true;check(!N::arrival_bank(self,offer,now).physical());self.stunned=false;
    self.submitting=true;check(!N::arrival_bank(self,offer,now).physical());self.submitting=false;
    check(N::arrival_bank(self,offer,now,false).kind==N::ArrivalKind::none);
    ++groups;
}
static void physical_incumbents(){
    auto self=self_fact();const auto work=N::arrival_work(self,camp_fact(),{},{},now);
    auto absent=camp_fact();absent.life_actual=false;const auto unknown=N::arrival_work(self,absent,{},{},now);
    self.gems=10;auto offer=bank_offer();offer.unlocked=false;const auto locked=N::arrival_bank(self,offer,now);
    const auto bank=N::arrival_bank(self,bank_offer(),now);
    for(const auto& physical:std::array<N::ArrivalDecision,2>{work,bank}){
        for(const auto& optional:std::array<N::ArrivalDecision,3>{unknown,locked,{}}){
            const auto kept=N::arrival_prefer(physical,optional);check(kept.kind==physical.kind&&kept.physical());
            const auto found=N::arrival_prefer(optional,physical);check(found.kind==physical.kind&&found.physical());
        }
    }
    check(N::arrival_prefer(work,bank).kind==N::ArrivalKind::work); // Stable caller ordering, no utility optimizer.
    check((N::arrival_prefer(locked,unknown).needs&N::arrival_need_life)!=0);
    auto next_frame=unknown;next_frame.epoch.round++;
    check(N::arrival_prefer(work,next_frame).epoch==next_frame.epoch); // No stale physical incumbent crosses inputs.
    check(N::arrival_prefer(next_frame,work).epoch==next_frame.epoch);
    ++groups;
}

enum class error_code : int {api_not_found=94,api_not_in_vision=97};
namespace constants {inline int submit_gems_range=1;}
namespace detail {
struct altar_lock_info {std::uint32_t handle{};};
struct Lock {N::Point point{};int code{94};std::uint32_t handle{};};
inline std::array<Lock,8> locks{};inline unsigned lock_count{},lock_calls{};
inline std::array<N::Point,8> queried{};inline std::array<int,8> queried_team{};
inline std::uint64_t* remaining{};inline unsigned query_cost=50;
inline int sdk_altar_lock(int x,int y,int team,altar_lock_info* out){
    assert(lock_calls<queried.size());queried[lock_calls]={x,y};queried_team[lock_calls++]=team;
    assert(*remaining>=query_cost);*remaining-=query_cost;
    for(unsigned i=0;i<lock_count;++i)if(locks[i].point==N::Point{x,y}){out->handle=locks[i].handle;return locks[i].code;}
    return int(error_code::api_not_found);
}
inline void check(int code){if(code)throw code;}
}
struct FakeMember {N::Point pos{};int group{-1};};
struct FakeGroupMemory {std::size_t begin{},end{};};
template<bool Indexed=false,bool ReadyGuard=false> struct ArrivalRuntime {
    N::Facts facts{};std::vector<FakeMember> members;std::vector<N::Point> altars;
    std::vector<FakeGroupMemory> memory;
    std::vector<N::CampFact> bodies;std::vector<N::GroupFact> lives;
    std::uint64_t remaining=200000;unsigned camp_reads{},group_reads{};
    N::StationaryIndex<4096> index{};bool index_ready{true};std::uint64_t lookup_pure_cost{};
    std::vector<N::Point> lookup_points;
    ArrivalRuntime(){facts.epoch=now;facts.self=self_fact();detail::lock_count=detail::lock_calls=0;detail::remaining=&remaining;detail::query_cost=50;
        if constexpr(Indexed)check(index.configure(64,64)==N::StationaryConfigure::configured);}
    static std::uint64_t clock(void* context){return static_cast<ArrivalRuntime*>(context)->remaining;}
    N::Budget budget(){return {this,clock,remaining};}
    N::Facts& native_facts(){return facts;}
    void add(N::CampFact value,N::GroupFact group={}){value.member_ordinal=int(members.size());members.push_back({value.pose,value.group_ordinal});bodies.push_back(value);lives.push_back(group);
        if constexpr(Indexed){const auto added=index.add(value.pose,value.member_ordinal);
            if(added!=N::StationaryAdd::added&&added!=N::StationaryAdd::duplicate)index_ready=false;}
        if(value.group_ordinal>=0){if(memory.size()<=std::size_t(value.group_ordinal))memory.resize(std::size_t(value.group_ordinal)+1);
            memory[std::size_t(value.group_ordinal)].end=members.size();}}
    int native_arrival_member_at(N::Point point) requires(Indexed){
        lookup_points.push_back(point);check(remaining>=lookup_pure_cost);remaining-=lookup_pure_cost;return index.at(point);
    }
    bool native_arrival_index_ready()const requires(Indexed&&ReadyGuard){return index_ready;}
    bool query(N::Budget& budget){if(!budget.before_query())return false;remaining-=50;return budget.after_query();}
    bool native_read_camp(N::Budget& budget,int ordinal,N::CampFact& out){++camp_reads;if(!query(budget))return false;out=bodies[std::size_t(ordinal)];return true;}
    bool native_read_group(N::Budget& budget,int,N::GroupFact& out,int witness){++group_reads;if(!query(budget))return false;out=lives[std::size_t(witness)];return true;}
    void native_capture_at(const N::SelfFact& self,const N::GroupFact& group,int ordinal,N::CaptureFact& out)const{
        out={};out.epoch=facts.epoch;out.group_id=group.id;out.life=group.life;out.life_actual=group.current(facts.epoch);
        out.on_spawn_actual=self.actual(facts.epoch);out.on_spawn=out.on_spawn_actual&&self.pose==members[std::size_t(ordinal)].pos;
    }
    #include "../src/native_service_arrival_runtime.h"
};
using FakeRuntime=ArrivalRuntime<>;
using IndexedRuntime=ArrivalRuntime<true>;
using GuardedIndexedRuntime=ArrivalRuntime<true,true>;
static void runtime_work(){
    {
        FakeRuntime runtime;runtime.add(camp_fact());runtime.add(camp_fact(1,{10,11}));
        N::ArrivalRequest request{};request.retained_member=0;auto budget=runtime.budget();
        const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.decision.kind==N::ArrivalKind::work&&runtime.camp_reads==1&&runtime.group_reads==0);
        check(result.camp_probes==1&&result.camp_count==1&&runtime.facts.camp.member_ordinal==-1);
        check(runtime.facts.self.epoch==now&&runtime.facts.self.raw_a); // No publication or command mutation.
    }
    {
        FakeRuntime runtime;auto unknown=camp_fact();unknown.life_actual=false;runtime.add(unknown);runtime.add(camp_fact(1,{10,11}));
        N::ArrivalRequest request{};request.retained_member=0;request.blocker_member=1;auto budget=runtime.budget();
        const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.decision.kind==N::ArrivalKind::work&&result.decision.member_ordinal()==1&&runtime.camp_reads==2);
        check(result.camp_count==2&&!result.query_denied);
    }
    {
        FakeRuntime runtime;auto dead=camp_fact();dead.life=N::Life::dead;dead.body_actual=dead.buffs_actual=false;
        runtime.add(dead,group_fact(N::GroupLife::alive));runtime.add(camp_fact(1,{10,11}));
        N::ArrivalRequest request{};request.retained_member=0;request.blocker_member=1;auto budget=runtime.budget();
        const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.decision.kind==N::ArrivalKind::work&&result.groups[0].life==N::GroupLife::alive);
        check(result.camp_count==2&&result.group_count==1&&runtime.group_reads==1);
    }
    {
        FakeRuntime runtime;auto dead=camp_fact(0,{10,10});dead.life=N::Life::dead;dead.body_actual=dead.buffs_actual=false;
        runtime.add(dead,group_fact());runtime.add(camp_fact(1,{10,11}));
        N::ArrivalRequest request{};request.retained_member=0;request.allow_work=false;auto budget=runtime.budget();
        const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.decision.kind==N::ArrivalKind::capture&&result.decision.physical());
        check(runtime.camp_reads==1&&runtime.group_reads==1&&result.camp_count==1);
    }
    {
        FakeRuntime runtime;auto dead=camp_fact();dead.life=N::Life::dead;dead.body_actual=dead.buffs_actual=false;
        runtime.add(dead,group_fact());auto occupied=dead;occupied.pose={10,10};runtime.add(occupied,group_fact());
        N::ArrivalRequest request{};request.retained_member=0;request.allow_work=false;auto budget=runtime.budget();
        const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.decision.kind==N::ArrivalKind::capture&&result.decision.physical());
        check(runtime.camp_reads==1&&runtime.group_reads==1); // Another actual spawn in the same group is a capture hold.
    }
    {
        FakeRuntime runtime;runtime.add(camp_fact());N::ArrivalRequest request{};request.purpose=N::ArrivalPurpose::recover;
        auto budget=runtime.budget();auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.camp_probes==0&&runtime.camp_reads==0&&!result.decision.physical());
        request.transferred_work=true;result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.decision.kind==N::ArrivalKind::work&&runtime.camp_reads==1);
    }
    ++groups;
}
static void runtime_bank(){
    {
        FakeRuntime runtime;runtime.facts.self.gems=30;runtime.altars={{11,10},{10,11},{9,10}};runtime.add(camp_fact());
        detail::locks[0]={{11,10},0,90};detail::lock_count=1;
        N::ArrivalRequest request{};request.purpose=N::ArrivalPurpose::bank;request.preferred_altar=0;request.allow_work=false;
        auto budget=runtime.budget();const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.decision.kind==N::ArrivalKind::bank&&result.decision.bank.ordinal==1);
        check(result.lock_probes==2&&detail::lock_calls==2&&runtime.camp_reads==0);
        check(detail::queried_team[0]==1&&detail::queried_team[1]==1&&runtime.facts.self.gems==30);
    }
    {
        FakeRuntime runtime;runtime.facts.self.gems=30;runtime.altars={{11,10},{10,11}};
        detail::locks[0]={{11,10},97,0};detail::lock_count=1;
        N::ArrivalRequest request{};request.preferred_altar=0;auto budget=runtime.budget();
        const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.decision.kind==N::ArrivalKind::bank&&result.decision.bank.ordinal==1&&result.lock_probes==2);
    }
    {
        FakeRuntime runtime;runtime.facts.self.gems=30;runtime.altars={{11,10},{10,11},{9,10}};
        detail::locks[0]={{11,10},0,90};detail::locks[1]={{10,11},0,91};detail::lock_count=2;
        N::ArrivalRequest request{};request.preferred_altar=0;auto budget=runtime.budget();
        const auto partial=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(!partial.decision.physical()&&partial.more_banks&&partial.next_bank_cursor==2&&partial.lock_probes==2);
        request.bank_cursor=partial.next_bank_cursor;detail::lock_calls=0;
        const auto next=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(next.decision.kind==N::ArrivalKind::bank&&next.decision.bank.ordinal==2&&next.lock_probes==2);
    }
    {
        FakeRuntime runtime;runtime.facts.self.gems=30;runtime.altars={{11,10},{10,11}};
        N::ArrivalRequest request{};request.preferred_altar=0;auto budget=runtime.budget();
        const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.decision.kind==N::ArrivalKind::bank&&detail::lock_calls==1); // No optional second read.
    }
    ++groups;
}
static void bounded_dependencies(){
    {
        FakeRuntime runtime;for(unsigned i=0;i<3;++i){auto camp=camp_fact(int(i),{10,11});camp.life_actual=i==2;runtime.add(camp);}
        N::ArrivalRequest request{};request.retained_member=0;request.blocker_member=1;auto budget=runtime.budget();
        const auto first=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(!first.decision.physical()&&first.camp_probes==2&&runtime.camp_reads==2&&first.more_work&&first.next_work_cursor==2);
        request.retained_member=request.blocker_member=-1;request.work_cursor=first.next_work_cursor;
        const auto next=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(next.decision.kind==N::ArrivalKind::work&&next.decision.member_ordinal()==2);
    }
    {
        FakeRuntime runtime;for(unsigned i=0;i<70;++i)runtime.add(camp_fact(int(i),{30,30}));runtime.add(camp_fact(70));
        N::ArrivalRequest request{};auto budget=runtime.budget();
        const auto first=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(first.scan_incomplete&&first.more_work&&first.camp_probes==0&&first.next_work_cursor==64);
        request.work_cursor=first.next_work_cursor;
        const auto next=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(next.decision.kind==N::ArrivalKind::work&&next.decision.member_ordinal()==70);
    }
    {
        FakeRuntime runtime;runtime.facts.self.gems=30;runtime.altars={{11,10}};runtime.remaining=9000;
        N::ArrivalRequest request{};auto budget=runtime.budget();
        const auto denied=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(denied.query_denied&&denied.lock_probes==1&&detail::lock_calls==0&&!denied.decision.physical());
        check((denied.decision.needs&N::arrival_need_funding)!=0&&!budget.in_span);
    }
    {
        FakeRuntime runtime;runtime.facts.self.gems=30;runtime.altars={{11,10}};
        detail::locks[0]={{11,10},900,0};detail::lock_count=1;detail::query_cost=2001;
        N::ArrivalRequest request{};auto budget=runtime.budget();bool caught=false;
        try{runtime.native_resolve_arrival(budget,runtime.facts.self,request);}catch(int error){caught=error==900;}
        check(caught&&budget.poisoned&&!budget.in_span); // Error survives a failed price span.
    }
    ++groups;
}
static void indexed_incumbent_matches_catalogue_without_its_remote_prefix(){
    FakeRuntime catalogue;IndexedRuntime indexed;
    for(int i=0;i<72;++i){const auto camp=camp_fact(i,{30+i%8,30+i/8});catalogue.add(camp);indexed.add(camp);}
    const auto eligible=camp_fact(72,{11,10});catalogue.add(eligible);indexed.add(eligible);
    N::ArrivalRequest request{};auto old_budget=catalogue.budget();
    const auto deferred=catalogue.native_resolve_arrival(old_budget,catalogue.facts.self,request);
    check(deferred.scan_incomplete&&deferred.next_work_cursor==64&&catalogue.camp_reads==0);
    auto local_budget=indexed.budget();const auto local=indexed.native_resolve_arrival(local_budget,indexed.facts.self,request);
    check(local.decision.physical()&&local.decision.kind==N::ArrivalKind::work&&local.decision.member_ordinal()==72);
    check(indexed.lookup_points.size()==6&&indexed.lookup_points.front()==N::Point{9,9}&&indexed.lookup_points.back()==N::Point{11,10});
    check(indexed.camp_reads==1&&local.camp_probes==1&&indexed.group_reads==0);
    request.work_cursor=deferred.next_work_cursor;
    const auto later=catalogue.native_resolve_arrival(old_budget,catalogue.facts.self,request);
    check(later.decision.kind==local.decision.kind&&later.decision.member_ordinal()==local.decision.member_ordinal());
    check(later.decision.word().actions[0].target_id==local.decision.word().actions[0].target_id);
    check(indexed.facts.camp.member_ordinal==-1&&indexed.facts.self.raw_a); // Static lookup publishes no actual camp or attack.
    ++groups;
}
static void indexed_unknown_and_hidden_facts_retain_the_unprobed_cell(){
    IndexedRuntime runtime;auto unknown=camp_fact(0,{9,9});unknown.life_actual=false;
    auto hidden=camp_fact(1,{10,9});hidden.body_actual=false;
    runtime.add(unknown);runtime.add(hidden);runtime.add(camp_fact(2,{11,9}));
    N::ArrivalRequest request{};auto budget=runtime.budget();
    const auto partial=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
    check(!partial.decision.physical()&&partial.more_work&&partial.next_work_cursor==2);
    check(partial.camp_probes==2&&partial.camp_count==2&&runtime.camp_reads==2&&runtime.group_reads==0);
    check(runtime.lookup_points.size()==3&&!partial.query_denied);
    check((partial.decision.needs&N::arrival_need_life)!=0&&(partial.decision.needs&N::arrival_need_body)!=0);
    request.work_cursor=partial.next_work_cursor;runtime.lookup_points.clear();
    const auto fresh=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
    check(fresh.decision.physical()&&fresh.decision.member_ordinal()==2&&fresh.camp_probes==1);
    check(runtime.lookup_points.size()==1&&runtime.lookup_points[0]==N::Point{11,9});
    check(runtime.camp_reads==3&&runtime.group_reads==0); // Unknown/hidden is never a death or group-release receipt.
    ++groups;
}
static void indexed_retained_blocker_and_job_filters_keep_their_priority(){
    {
        IndexedRuntime runtime;auto unknown=camp_fact(0,{11,10});unknown.life_actual=false;
        runtime.add(unknown);runtime.add(camp_fact(1,{10,11}));runtime.add(camp_fact(2,{9,9}));
        runtime.lookup_pure_cost=10000;N::ArrivalRequest request{};request.retained_member=0;request.blocker_member=1;
        auto budget=runtime.budget();const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.decision.physical()&&result.decision.member_ordinal()==1&&runtime.camp_reads==2);
        check(runtime.lookup_points.empty()&&!budget.poisoned); // No optional index visit after the funded incumbent.
    }
    for(bool preserve_member:std::array<bool,2>{false,true}){
        IndexedRuntime runtime;auto foreign=camp_fact(0,{9,9});foreign.group_id=8;foreign.group_ordinal=1;
        auto retained=camp_fact(1,{11,11});retained.life_actual=false;
        runtime.add(foreign);runtime.add(retained);runtime.add(camp_fact(2,{10,9}));
        N::ArrivalRequest request{};request.retained_group=2;request.retained_member=1;
        request.preserve_job=true;request.preserve_member=preserve_member;
        auto budget=runtime.budget();const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        if(preserve_member){
            check(!result.decision.physical()&&runtime.camp_reads==1&&runtime.lookup_points.size()==9);
            check((result.decision.needs&N::arrival_need_life)!=0);
        }else{
            check(result.decision.physical()&&result.decision.member_ordinal()==2&&runtime.camp_reads==2);
            check(runtime.lookup_points.size()==2&&result.decision.group_ordinal()==2);
        }
        check(runtime.group_reads==0); // A foreign group in the first local cell is not queried.
    }
    ++groups;
}
static void indexed_visit_cap_and_rectangle_cursor_are_independent_of_catalogue_size(){
    IndexedRuntime runtime;runtime.facts.self.attack_range=4;runtime.add(camp_fact(0,{14,14}));
    N::ArrivalRequest request{};auto budget=runtime.budget();
    const auto first=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
    check(first.more_work&&first.scan_incomplete&&first.next_work_cursor==64&&!first.decision.physical());
    check(runtime.lookup_points.size()==64&&runtime.lookup_points.back()==N::Point{6,13});
    check(runtime.camp_reads==0&&first.camp_probes==0);
    request.work_cursor=first.next_work_cursor;runtime.lookup_points.clear();
    const auto last=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
    check(last.decision.physical()&&last.decision.member_ordinal()==0&&runtime.camp_reads==1);
    check(runtime.lookup_points.size()==17&&runtime.lookup_points.front()==N::Point{7,13}&&runtime.lookup_points.back()==N::Point{14,14});
    check(last.next_work_cursor==0&&last.camp_probes==1);
    ++groups;
}
static void indexed_readiness_and_half_pure_deferral_are_named_continuations(){
    {
        GuardedIndexedRuntime runtime;runtime.add(camp_fact(0,{11,10}));runtime.add(camp_fact(1,{11,10}));
        check(runtime.index.configured()&&!runtime.index_ready); // Conflicting static cell cannot certify empty/complete index.
        N::ArrivalRequest request{};auto budget=runtime.budget();
        const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.decision.physical()&&result.decision.member_ordinal()==0&&runtime.lookup_points.empty());
        check(runtime.camp_reads==1&&result.camp_probes==1); // Unsupported index uses the unchanged catalogue fallback.
    }
    {
        IndexedRuntime runtime;runtime.lookup_pure_cost=500;N::ArrivalRequest request{};auto budget=runtime.budget();
        const auto deferred=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(deferred.more_work&&deferred.scan_incomplete&&deferred.next_work_cursor==8&&runtime.lookup_points.size()==8);
        check((deferred.decision.needs&N::arrival_need_catalogue)!=0&&(deferred.decision.needs&N::arrival_need_funding)!=0);
        check(!budget.poisoned&&!deferred.query_denied&&runtime.camp_reads==0&&budget.queries==0);
        request.work_cursor=deferred.next_work_cursor;runtime.lookup_points.clear();runtime.lookup_pure_cost=0;
        ++runtime.facts.epoch.round;runtime.facts.self.epoch=runtime.facts.epoch;
        auto next_budget=runtime.budget();const auto next=runtime.native_resolve_arrival(next_budget,runtime.facts.self,request);
        check(!next.more_work&&!next.scan_incomplete&&runtime.lookup_points.size()==9);
        check(runtime.lookup_points.front()==N::Point{11,11}&&runtime.camp_reads==0);
    }
    ++groups;
}
static void indexed_membership_never_substitutes_for_actual_lifecycle_or_range(){
    {
        IndexedRuntime runtime;auto dead=camp_fact(0,{10,10});dead.life=N::Life::dead;dead.body_actual=dead.buffs_actual=false;
        runtime.add(dead,group_fact());N::ArrivalRequest request{};auto budget=runtime.budget();
        const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(result.decision.kind==N::ArrivalKind::capture&&result.decision.physical()&&result.decision.word().count==0);
        check(runtime.lookup_points.size()==5&&runtime.camp_reads==1&&runtime.group_reads==1);
        check(result.camp_count==1&&result.group_count==1);
    }
    {
        IndexedRuntime runtime;runtime.facts.self.attack_range=0;runtime.add(camp_fact(0,{11,10}));
        N::ArrivalRequest request{};auto budget=runtime.budget();const auto result=runtime.native_resolve_arrival(budget,runtime.facts.self,request);
        check(!result.decision.physical()&&runtime.lookup_points.size()==1&&runtime.lookup_points[0]==N::Point{10,10});
        check(runtime.camp_reads==0); // The optional rectangle uses the queried range, not an invented minimum range.
    }
    ++groups;
}
int main(){
    work_facts();corpse_and_capture();bank_facts();physical_incumbents();runtime_work();runtime_bank();bounded_dependencies();
    indexed_incumbent_matches_catalogue_without_its_remote_prefix();indexed_unknown_and_hidden_facts_retain_the_unprobed_cell();
    indexed_retained_blocker_and_job_filters_keep_their_priority();indexed_visit_cap_and_rectangle_cursor_are_independent_of_catalogue_size();
    indexed_readiness_and_half_pure_deferral_are_named_continuations();indexed_membership_never_substitutes_for_actual_lifecycle_or_range();
    std::printf("PASS native_service_arrival: %u checks, %u groups\n",checks,groups);
}
