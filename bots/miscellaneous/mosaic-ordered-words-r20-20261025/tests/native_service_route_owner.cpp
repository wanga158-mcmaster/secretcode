#include "native_service_route_owner.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace N=mosaic::native_service;
static unsigned checks=0,groups=0;
static void check(bool value,const char* expression,int line){
    ++checks;
    if(!value){std::fprintf(stderr,"route owner check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)

[[maybe_unused]] static constinit N::RouteOwner constant_owner{};
static_assert(std::is_trivially_copyable_v<N::RouteOwner>);
static_assert(std::is_standard_layout_v<N::RouteOwner>);
static_assert(noexcept(constant_owner.bind({})));

static N::SelfFact self(N::Epoch epoch,N::Point pose={10,10},int gems=0){
    N::SelfFact value{};value.epoch=epoch;value.id=7;value.pose=pose;
    value.hp=value.max_hp=1000;value.alive=value.raw_m=value.raw_a=value.raw_c=true;
    value.body_actual=value.restrictions_actual=value.buffs_actual=value.channel_actual=true;
    value.gems_actual=value.custody_actual=true;value.gems=gems;return value;
}
static constexpr N::RouteGoal goal(std::uint64_t token=1,N::RoutePurpose purpose=N::RoutePurpose::work){
    return {token,90,purpose,4,8,{20,10}};
}
static N::RouteFrame frame(N::RouteGoal route,const N::SelfFact& body,std::uint64_t generation=1,
                           int distance=10,N::NavigationStatus status=N::NavigationStatus::ready){
    return {route,body.epoch,body.pose,generation,status,distance,true,true,false};
}
static N::RouteFunding funding(std::uint64_t owner,std::uint64_t serial,N::Epoch epoch,N::RouteNeed need){
    return {owner,serial,epoch,need,true,true};
}
static N::RouteMove move(std::uint64_t owner,std::uint64_t serial,const N::SelfFact& before,const N::SelfFact& after){
    return {owner,serial,before.epoch,after.epoch,before.pose,after.pose,true};
}

static void semantic_goal_and_input_epochs(){
    ++groups;N::RouteOwner owner;const auto work=goal();
    CHECK(!owner.active()&&owner.need()==N::RouteNeed::none&&!owner.funded());
    CHECK(owner.bind({})==N::RouteBinding::invalid);
    CHECK(owner.bind(work)==N::RouteBinding::bound&&owner.need()==N::RouteNeed::arrival);
    auto first=self({1,1,0},{10,10},30);CHECK(owner.begin(first));CHECK(owner.observe_custody(first));
    auto preparing=frame(work,first,1,-1,N::NavigationStatus::preparing);
    CHECK(owner.observe(first,preparing)&&owner.need()==N::RouteNeed::prepare);
    CHECK(owner.fund(funding(1,1,first.epoch,N::RouteNeed::prepare))&&owner.funded());
    auto next=self({2,2,0},{10,10},30);CHECK(owner.begin(next));
    CHECK(owner.active()&&owner.goal()==work&&owner.need()==N::RouteNeed::arrival&&!owner.funded());
    CHECK(owner.bind(work)==N::RouteBinding::retained);
    CHECK(owner.custody_owed()&&owner.custody()==30&&owner.service_progress()==0);
    CHECK(owner.observe(next,frame(work,next,1,-1,N::NavigationStatus::preparing)));
    CHECK(owner.need()==N::RouteNeed::prepare&&owner.distance_progress()==0);
    CHECK(!owner.observe(first,preparing));
    auto changed=work;changed.target={21,10};
    CHECK(owner.bind(changed)==N::RouteBinding::replacement_required);
    CHECK(owner.goal()==work&&owner.need()==N::RouteNeed::transfer&&owner.custody_owed());
    CHECK(owner.bind(work)==N::RouteBinding::retained&&owner.need()==N::RouteNeed::prepare);
}

static void per_origin_state_and_actual_arrival(){
    ++groups;N::RouteOwner owner;const auto work=goal();owner.bind(work);auto body=self({3,3,0});owner.begin(body);
    // This per-origin ready sample is usable while the global BFS may still be
    // preparing. The API deliberately has no global-ready permission gate.
    auto current=frame(work,body,7,4);CHECK(owner.observe(body,current));
    CHECK(owner.need()==N::RouteNeed::move&&owner.service_progress()==0);
    current.nominees_available=false;CHECK(owner.observe(body,current));CHECK(owner.need()==N::RouteNeed::blocked);
    current.status=N::NavigationStatus::unreachable;current.distance=-1;
    CHECK(owner.observe(body,current)&&owner.need()==N::RouteNeed::unreachable);
    current.status=N::NavigationStatus::failed;
    CHECK(owner.observe(body,current)&&owner.need()==N::RouteNeed::observe);
    current.field_matches=false;
    CHECK(owner.observe(body,current)&&owner.need()==N::RouteNeed::prepare);
    current.geometric_arrival=true;
    CHECK(owner.observe(body,current)&&owner.need()==N::RouteNeed::arrival);
    CHECK(owner.service_progress()==0&&owner.active());
    CHECK(!owner.service_actual({1,1,body.epoch,N::RouteServiceKind::work_effect,false,true}));
    CHECK(!owner.service_actual({1,1,body.epoch,N::RouteServiceKind::work_effect,true,false}));
    CHECK(!owner.service_actual({1,1,body.epoch,N::RouteServiceKind::bank_start,true,true}));
    CHECK(owner.service_actual({1,1,body.epoch,N::RouteServiceKind::work_effect,true,true}));
    CHECK(owner.service_progress()==1&&owner.active());
    CHECK(owner.service_actual({1,2,body.epoch,N::RouteServiceKind::work_complete,true,false}));
    CHECK(!owner.active()&&owner.need()==N::RouteNeed::none);
    CHECK(owner.bind(work)==N::RouteBinding::invalid);
}

static void physical_edges_and_two_tile_cycle(){
    ++groups;N::RouteOwner owner;auto work=goal();owner.bind(work);
    auto first=self({10,10,0},{10,10});owner.begin(first);owner.observe(first,frame(work,first,4,10));
    auto away=self({10,11,1},{11,10});CHECK(owner.move_applied(move(1,1,first,away)));
    CHECK(owner.physical_edges()==1&&owner.distance_progress()==0&&owner.need()==N::RouteNeed::feedback);
    CHECK(!owner.close_feedback({1,false},away,frame(work,away,4,11))&&owner.feedback_pending());
    CHECK(owner.close_feedback({1,true},away,frame(work,away,4,11)));
    CHECK(owner.distance_progress()==0&&owner.service_progress()==0);
    auto back=self({11,12,1},{10,10});owner.begin(away);owner.observe(away,frame(work,away,4,11));
    CHECK(owner.move_applied(move(1,2,away,back)));
    CHECK(owner.close_feedback({2,true},back,frame(work,back,4,10)));
    CHECK(owner.physical_edges()==2&&owner.distance_progress()==0&&owner.service_progress()==0);
    // A new minimum in the SAME field is route progress, still no service.
    auto closer=self({11,13,2},{9,10});CHECK(owner.move_applied(move(1,3,back,closer)));
    CHECK(owner.close_feedback({3,true},closer,frame(work,closer,4,9)));
    CHECK(owner.physical_edges()==3&&owner.distance_progress()==1&&owner.service_progress()==0);
    CHECK(owner.last_progress_epoch()==closer.epoch);
    CHECK(!owner.move_applied(move(1,3,back,closer)));
}

static void changed_field_and_missing_readback(){
    ++groups;N::RouteOwner owner;auto work=goal();owner.bind(work);
    auto first=self({20,20,0},{10,10});owner.begin(first);owner.observe(first,frame(work,first,1,8));
    auto after=self({20,21,1},{11,10});CHECK(owner.move_applied(move(1,1,first,after)));
    CHECK(owner.close_feedback({1,true},after,frame(work,after,2,1)));
    CHECK(owner.distance_progress()==0&&owner.service_progress()==0); // refield is not progress
    auto later=self({21,22,0},{11,10});owner.begin(later);owner.observe(later,frame(work,later,2,1));
    auto next=self({21,23,1},{12,10});CHECK(owner.move_applied(move(1,2,later,next)));
    auto bad=next;bad.body_actual=false;
    CHECK(!owner.close_feedback({2,true},bad,frame(work,bad,2,0))&&owner.feedback_pending());
    auto reconciled=self({22,24,0},{12,10});
    CHECK(owner.close_feedback({2,true},reconciled,frame(work,reconciled,2,0)));
    CHECK(owner.physical_edges()==2&&owner.distance_progress()==0&&owner.need()==N::RouteNeed::arrival);
    CHECK(owner.service_progress()==0&&owner.active());
    // A fresh ACK can close feedback even when no matching field is available.
    auto off=self({22,25,1},{13,10});owner.observe(reconciled,frame(work,reconciled,2,0));
    CHECK(owner.move_applied(move(1,3,reconciled,off)));
    CHECK(owner.close_feedback({3,true},off,{}));
    CHECK(!owner.feedback_pending()&&owner.need()==N::RouteNeed::observe&&owner.distance_progress()==0);
}

static void dated_rejections_and_paid_transfer(){
    ++groups;N::RouteOwner owner;auto work=goal();owner.bind(work);auto body=self({30,30,0},{10,10},25);
    owner.begin(body);owner.observe_custody(body);owner.observe(body,frame(work,body,1,8));
    CHECK(owner.reject({1,1,body.epoch,{10,10},{9,9},N::RouteRejectionKind::camp,false}));
    CHECK(owner.prevalidation_rejections()==1&&owner.raw_refusals()==0&&owner.need()==N::RouteNeed::blocked);
    CHECK(owner.reject({1,2,{30,30,1},{10,10},{9,10},N::RouteRejectionKind::unknown,true}));
    CHECK(owner.prevalidation_rejections()==1&&owner.raw_refusals()==1);
    std::array<N::Point,8> points{};CHECK(owner.exclusions({10,10},points)==2);
    auto fresh=self({31,31,0},{10,10},25);owner.begin(fresh);owner.observe(fresh,frame(work,fresh,1,8));
    CHECK(owner.exclusions({10,10},points)==2&&owner.custody_owed());
    CHECK(!owner.fund(funding(1,1,body.epoch,N::RouteNeed::move)));
    auto bank=goal(2,N::RoutePurpose::bank);CHECK(owner.bind(bank)==N::RouteBinding::replacement_required);
    auto paid=funding(2,1,fresh.epoch,N::RouteNeed::prepare);paid.from_owner=1;paid.reserved=false;
    CHECK(!owner.replace(bank,paid)&&owner.goal()==work&&owner.custody()==25);
    paid.reserved=true;paid.complete_continuation=false;
    CHECK(!owner.replace(bank,paid));paid.complete_continuation=true;
    paid.need=static_cast<N::RouteNeed>(255);CHECK(!owner.replace(bank,paid));paid.need=N::RouteNeed::prepare;
    paid.from_owner=99;CHECK(!owner.replace(bank,paid));paid.from_owner=1;
    CHECK(owner.replace(bank,paid)&&owner.goal()==bank&&owner.funded());
    CHECK(owner.custody_owed()&&owner.custody()==25&&owner.physical_edges()==0&&owner.service_progress()==0);
    CHECK(owner.exclusions({10,10},points)==0); // prior episode history is retained but not this goal's omission
    auto current=self({31,31,0},{10,10},25);owner.observe(current,frame(bank,current,2,0));
    CHECK(owner.service_actual({2,1,current.epoch,N::RouteServiceKind::bank_start,true,true}));
    CHECK(owner.active()&&owner.custody()==25&&owner.service_progress()==1);
    CHECK(!owner.service_actual({2,1,current.epoch,N::RouteServiceKind::bank_start,true,true}));
    CHECK(owner.service_actual({2,2,current.epoch,N::RouteServiceKind::bank_complete,true,false}));
    CHECK(!owner.active()&&owner.custody_owed()&&owner.custody()==25); // completion is not an invented fresh bag
    current.epoch={32,32,0};current.gems=0;CHECK(owner.observe_custody(current));
    CHECK(!owner.custody_owed()&&owner.custody()==0);
}

static void pending_owner_and_exclusion_capacity(){
    ++groups;N::RouteOwner owner;auto work=goal();owner.bind(work);auto body=self({40,40,0});
    owner.begin(body);owner.observe(body,frame(work,body,1,8));auto after=self({40,41,1},{11,10});
    CHECK(owner.move_applied(move(1,1,body,after)));
    auto bank=goal(2,N::RoutePurpose::bank);CHECK(owner.bind(bank)==N::RouteBinding::replacement_required);
    CHECK(owner.need()==N::RouteNeed::feedback);
    auto pending_transfer=funding(2,1,after.epoch,N::RouteNeed::prepare);pending_transfer.from_owner=1;
    CHECK(!owner.replace(bank,pending_transfer));
    CHECK(!owner.service_actual({1,1,after.epoch,N::RouteServiceKind::work_complete,true,true}));
    CHECK(owner.close_feedback({1,true},after,frame(work,after,1,7)));
    CHECK(owner.need()==N::RouteNeed::transfer);
    CHECK(owner.bind(work)==N::RouteBinding::retained&&owner.need()==N::RouteNeed::move);
    for(unsigned n=0;n<9;++n){
        auto epoch=N::Epoch{41,std::uint64_t(50+n),0};
        CHECK(owner.reject({1,n+1,epoch,{11,10},{int(n),0},N::RouteRejectionKind::champion,false}));
    }
    std::array<N::Point,8> omitted{};CHECK(owner.exclusions({11,10},omitted)==8&&owner.exclusions_truncated());
    CHECK(!owner.release_exclusion({11,10},{8,0},{41,58,0}));
    CHECK(owner.release_exclusion({11,10},{8,0},{42,59,0}));
    CHECK(owner.exclusions({11,10},omitted)==7);
    CHECK(owner.physical_edges()==1&&owner.distance_progress()==1&&owner.service_progress()==0);
}

static void preparing_gap_and_unknown_move(){
    ++groups;N::RouteOwner owner;const auto work=goal();owner.bind(work);
    auto first=self({50,60,0},{10,10});owner.begin(first);owner.observe(first,frame(work,first,9,10));
    auto away=self({50,61,1},{11,10});CHECK(owner.move_applied(move(1,1,first,away)));
    CHECK(owner.close_feedback({1,true},away,frame(work,away,9,-1,N::NavigationStatus::preparing)));
    auto ready_again=self({51,62,0},{11,10});owner.begin(ready_again);owner.observe(ready_again,frame(work,ready_again,9,11));
    auto back=self({51,63,1},{10,10});CHECK(owner.move_applied(move(1,2,ready_again,back)));
    CHECK(owner.close_feedback({2,true},back,frame(work,back,9,10)));
    CHECK(owner.physical_edges()==2&&owner.distance_progress()==0&&owner.service_progress()==0);
    auto uncertain=self({52,64,1},{9,10});
    auto receipt=move(1,3,back,uncertain);receipt.applied_actual=false;
    CHECK(owner.move_unresolved(receipt)&&owner.feedback_pending());
    CHECK(owner.physical_edges()==2&&owner.need()==N::RouteNeed::feedback);
    CHECK(owner.close_feedback({3,true},uncertain,frame(work,uncertain,9,9)));
    CHECK(owner.physical_edges()==2&&owner.distance_progress()==0&&owner.service_progress()==0);
}

int main(){
    semantic_goal_and_input_epochs();per_origin_state_and_actual_arrival();physical_edges_and_two_tile_cycle();
    changed_field_and_missing_readback();dated_rejections_and_paid_transfer();pending_owner_and_exclusion_capacity();preparing_gap_and_unknown_move();
    std::printf("PASS native_service_route_owner: %u checks, %u groups\n",checks,groups);
}
