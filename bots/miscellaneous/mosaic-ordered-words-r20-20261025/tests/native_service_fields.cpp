#include "native_service_fields.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace N=mosaic::native_service;
using Which=N::ServiceField;
static unsigned checks=0,groups=0;
static void check(bool value,const char* expression,int line){
    ++checks;
    if(!value){std::fprintf(stderr,"service fields check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)

static constinit N::ServiceFields<> constant_fields{};
static_assert(!std::is_copy_constructible_v<N::ServiceFields<>>);
static_assert(!std::is_move_constructible_v<N::ServiceFields<>>);
static_assert(std::is_same_v<decltype(constant_fields.navigation(Which::service)),const N::Navigation<>&>);
static_assert(noexcept(constant_fields.retire_all()));

template<std::size_t Cells> static void finish(N::ServiceFields<Cells>& fields,Which which){
    unsigned guard=0;
    while(fields.status(which)==N::NavigationStatus::preparing){
        const auto progress=fields.step(which,3);CHECK(progress.units<=3);
        CHECK(++guard<Cells*4+300);
    }
}

static void two_fields_keep_partial_progress(){
    ++groups;N::ServiceFields<25> fields;
    std::array<std::uint8_t,25> board{};board.fill(1);
    const std::array<N::Point,1> service{{{4,4}}},egress{{{0,0}}};
    CHECK(fields.begin(Which::service,5,5,board,service,10));
    const auto service_generation=fields.generation(Which::service);
    CHECK(service_generation&&fields.active(Which::service));
    const auto partial=fields.step(Which::service,13);
    CHECK(partial.status==N::NavigationStatus::preparing&&partial.units==13);
    CHECK(fields.begin(Which::egress,5,5,board,egress,20));
    const auto egress_generation=fields.generation(Which::egress);
    CHECK(egress_generation&&egress_generation!=service_generation);
    finish(fields,Which::egress);
    CHECK(fields.distance(Which::egress,{4,4})==4);
    CHECK(fields.generation(Which::service)==service_generation);
    CHECK(fields.begin(Which::service,5,5,board,service,10));
    CHECK(fields.generation(Which::service)==service_generation);
    // The13 stamp initializations survived the entire other field. Exactly12
    // remain before one root is seeded and becomes available.
    CHECK(fields.step(Which::service,12).units==12);
    CHECK(!fields.available(Which::service,{4,4}));
    CHECK(fields.step(Which::service,1).units==1);
    CHECK(fields.available(Which::service,{4,4}));
    finish(fields,Which::service);
    CHECK(fields.distance(Which::service,{0,0})==4);
    CHECK(fields.distance(Which::egress,{4,4})==4);
    CHECK(fields.semantic_intent(Which::service)==10&&fields.semantic_intent(Which::egress)==20);
}

static void ordered_root_and_intent_identity(){
    ++groups;N::ServiceFields<9> fields;std::array<std::uint8_t,9> board{};board.fill(1);
    std::array<N::Point,2> roots{{{0,0},{2,2}}};
    CHECK(fields.begin(Which::service,3,3,board,roots,0));
    const auto first=fields.generation(Which::service);fields.step(Which::service,9);
    auto matching=roots;CHECK(fields.begin(Which::service,3,3,board,matching,0));
    CHECK(fields.generation(Which::service)==first);
    CHECK(fields.step(Which::service,1).units==1&&!fields.available(Which::service,{0,0}));
    CHECK(fields.step(Which::service,1).units==1&&fields.available(Which::service,{0,0}));
    std::swap(roots[0],roots[1]);
    CHECK(fields.begin(Which::service,3,3,board,roots,0));
    const auto reordered=fields.generation(Which::service);CHECK(reordered!=first);
    CHECK(!fields.available(Which::service,{0,0}));
    CHECK(fields.begin(Which::service,3,3,board,roots,1));
    const auto changed_intent=fields.generation(Which::service);CHECK(changed_intent!=reordered);
    auto other_storage=board;
    CHECK(fields.begin(Which::service,3,3,other_storage,roots,1));
    CHECK(fields.generation(Which::service)!=changed_intent);
    const auto changed_board=fields.generation(Which::service);
    CHECK(fields.begin(Which::service,1,9,other_storage,std::array<N::Point,1>{{{0,8}}},1));
    CHECK(fields.generation(Which::service)!=changed_board);
}

static void copied_roots_and_retirement(){
    ++groups;N::ServiceFields<16> fields;std::array<std::uint8_t,16> board{};board.fill(1);
    std::array<N::Point,1> source{{{0,0}}};
    CHECK(fields.begin(Which::service,4,4,board,source,30));
    const auto first=fields.generation(Which::service);source[0]={3,3};
    finish(fields,Which::service);
    CHECK(fields.distance(Which::service,{0,0})==0); // caller roots were copied
    CHECK(fields.begin(Which::egress,4,4,board,source,40));finish(fields,Which::egress);
    const auto egress_generation=fields.generation(Which::egress);
    fields.retire(Which::service);
    CHECK(!fields.active(Which::service)&&fields.generation(Which::service)==0);
    CHECK(fields.status(Which::service)==N::NavigationStatus::idle);
    CHECK(!fields.distance(Which::service,{0,0}));
    CHECK(fields.generation(Which::egress)==egress_generation&&fields.distance(Which::egress,{3,3})==0);
    CHECK(fields.begin(Which::service,4,4,board,std::array<N::Point,1>{{{0,0}}},30));
    CHECK(fields.generation(Which::service)!=first&&fields.generation(Which::service)!=egress_generation);
    finish(fields,Which::service);CHECK(fields.distance(Which::service,{3,3})==3);
    fields.retire_all();CHECK(!fields.active(Which::service)&&!fields.active(Which::egress));
    CHECK(fields.navigation(Which::egress).status()==N::NavigationStatus::idle);
}

static void shared_board_mutation_boundary(){
    ++groups;N::ServiceFields<15> fields;std::array<std::uint8_t,15> board{};board.fill(1);
    const std::array<N::Point,1> right{{{4,1}}},left{{{0,1}}};
    CHECK(fields.begin(Which::service,5,3,board,right,100));CHECK(fields.begin(Which::egress,5,3,board,left,200));
    finish(fields,Which::service);finish(fields,Which::egress);
    CHECK(fields.distance(Which::service,{0,1})==4&&fields.distance(Which::egress,{4,1})==4);
    const auto old_service=fields.generation(Which::service),old_egress=fields.generation(Which::egress);
    fields.retire_all(); // both borrowers are ended BEFORE touching board bytes
    for(unsigned y=0;y<3;++y)board[y*5+2]=0;
    CHECK(!fields.available(Which::service,{0,1})&&!fields.available(Which::egress,{4,1}));
    CHECK(fields.begin(Which::service,5,3,board,right,101));CHECK(fields.begin(Which::egress,5,3,board,left,201));
    CHECK(fields.generation(Which::service)!=old_service&&fields.generation(Which::egress)!=old_egress);
    finish(fields,Which::service);finish(fields,Which::egress);
    CHECK(fields.status(Which::service,{0,1})==N::NavigationStatus::unreachable);
    CHECK(fields.status(Which::egress,{4,1})==N::NavigationStatus::unreachable);
    fields.retire_all();board[1*5+2]=1;
    CHECK(fields.begin(Which::service,5,3,board,right,102));finish(fields,Which::service);
    CHECK(fields.distance(Which::service,{0,1})==4);
}

static void nominees_and_one_context_failure(){
    ++groups;N::ServiceFields<9> fields;std::array<std::uint8_t,9> board{};board.fill(1);
    const std::array<N::Point,1> roots{{{2,2}}};
    CHECK(fields.begin(Which::service,3,3,board,roots,1));CHECK(fields.begin(Which::egress,3,3,board,roots,2));
    finish(fields,Which::service);finish(fields,Which::egress);
    std::array<N::Point,8> choices{};const auto count=fields.nominees(Which::service,{0,0},{},choices);
    CHECK(count==1&&choices[0]==N::Point{1,1});
    const std::array<N::Point,1> omitted{{{1,1}}};CHECK(fields.nominees(Which::service,{0,0},omitted,choices)==0);
    const auto untouched=fields.generation(Which::egress);
    CHECK(!fields.begin(Which::service,0,3,board,roots,1));
    CHECK(!fields.active(Which::service)&&fields.generation(Which::service)==0);
    CHECK(fields.status(Which::service)==N::NavigationStatus::failed);
    CHECK(fields.generation(Which::egress)==untouched&&fields.distance(Which::egress,{0,0})==2);
    const auto invalid=static_cast<Which>(255);
    CHECK(!fields.begin(invalid,3,3,board,roots,1));
    CHECK(fields.step(invalid,3).status==N::NavigationStatus::failed);
    CHECK(!fields.available(invalid,{0,0})&&!fields.distance(invalid,{0,0}));
    CHECK(fields.nominees(invalid,{0,0},{},choices)==0&&fields.generation(invalid)==0);
    fields.retire(invalid);CHECK(fields.generation(Which::egress)==untouched);
    CHECK(constant_fields.status(Which::service)==N::NavigationStatus::idle);
}

template<std::size_t Cells> static void same_geometry(const N::ServiceFields<Cells>& left,
                                                     const N::ServiceFields<Cells>& right,
                                                     Which which,int width,int height){
    CHECK(left.status(which)==right.status(which));
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        CHECK(left.available(which,{x,y})==right.available(which,{x,y}));
        CHECK(left.status(which,{x,y})==right.status(which,{x,y}));
        CHECK(left.distance(which,{x,y})==right.distance(which,{x,y}));
    }
}

static void guarded_denial_retains_the_exact_preparation_cursor(){
    ++groups;N::ServiceFields<9> fields,reference;std::array<std::uint8_t,9> board{};board.fill(1);
    const std::array<N::Point,1> roots{{{2,2}}};
    CHECK(fields.begin(Which::service,3,3,board,roots,90));CHECK(reference.begin(Which::service,3,3,board,roots,90));
    const auto generation=fields.generation(Which::service);unsigned calls=0;
    const auto first=fields.step_guarded(Which::service,4,[&]{++calls;return true;});
    CHECK(first.status==N::NavigationStatus::preparing&&first.units==4&&first.progressed&&calls==4);
    CHECK(reference.step(Which::service,4).units==4);same_geometry(fields,reference,Which::service,3,3);
    calls=0;const auto denied=fields.step_guarded(Which::service,32,[&]{++calls;return false;});
    CHECK(denied.status==N::NavigationStatus::preparing&&denied.units==0&&!denied.progressed&&calls==1);
    CHECK(fields.generation(Which::service)==generation&&fields.semantic_intent(Which::service)==90);
    same_geometry(fields,reference,Which::service,3,3);
    // Unrelated egress preparation cannot reset the denied service borrower.
    CHECK(fields.begin(Which::egress,3,3,board,std::array<N::Point,1>{{{0,0}}},91));finish(fields,Which::egress);
    CHECK(fields.begin(Which::service,3,3,board,roots,90)&&fields.generation(Which::service)==generation);
    calls=0;const auto initialized=fields.step_guarded(Which::service,5,[&]{++calls;return true;});
    CHECK(initialized.units==5&&initialized.progressed&&calls==5&&!fields.available(Which::service,{2,2}));
    CHECK(reference.step(Which::service,5).units==5);same_geometry(fields,reference,Which::service,3,3);
    const auto seeded=fields.step_guarded(Which::service,1,[]{return true;});
    CHECK(seeded.units==1&&seeded.progressed&&fields.available(Which::service,{2,2}));
    CHECK(reference.step(Which::service,1).units==1);same_geometry(fields,reference,Which::service,3,3);
    while(fields.status(Which::service)==N::NavigationStatus::preparing){
        calls=0;const auto chunk=fields.step_guarded(Which::service,3,[&]{++calls;return true;});
        const auto direct=reference.step(Which::service,3);
        CHECK(chunk.units<=3&&chunk.units==direct.units&&calls==chunk.units&&chunk.progressed==direct.progressed);
        CHECK(fields.generation(Which::service)==generation);same_geometry(fields,reference,Which::service,3,3);
    }
    CHECK(fields.status(Which::service)==N::NavigationStatus::ready&&fields.distance(Which::service,{0,0})==2);
    CHECK(fields.distance(Which::egress,{2,2})==2);
}

static void guarded_partial_allowance_preserves_per_origin_readiness(){
    ++groups;N::ServiceFields<5> fields;std::array<std::uint8_t,5> board{};board.fill(1);
    const std::array<N::Point,1> roots{{{0,0}}};CHECK(fields.begin(Which::egress,5,1,board,roots,17));
    const auto generation=fields.generation(Which::egress);unsigned calls=0;
    const auto cap=fields.step_guarded(Which::egress,3,[&]{++calls;return true;});
    CHECK(cap.units==3&&cap.progressed&&cap.status==N::NavigationStatus::preparing&&calls==3);
    calls=0;const auto partial=fields.step_guarded(Which::egress,32,[&]{return ++calls<=2;});
    CHECK(partial.units==2&&partial.progressed&&partial.status==N::NavigationStatus::preparing&&calls==3);
    CHECK(!fields.available(Which::egress,{0,0}));
    CHECK(fields.step_guarded(Which::egress,1,[]{return true;}).units==1);
    CHECK(fields.available(Which::egress,{0,0})&&fields.distance(Which::egress,{0,0})==0);
    CHECK(fields.step_guarded(Which::egress,2,[]{return true;}).units==2);
    CHECK(fields.status(Which::egress)==N::NavigationStatus::preparing);
    CHECK(fields.status(Which::egress,{2,0})==N::NavigationStatus::ready&&fields.distance(Which::egress,{2,0})==2);
    std::array<N::Point,8> nominees{};
    CHECK(fields.nominees(Which::egress,{2,0},{},nominees)==1&&nominees[0]==N::Point{1,0});
    calls=0;const auto completed=fields.step_guarded(Which::egress,32,[&]{++calls;return true;});
    CHECK(completed.status==N::NavigationStatus::ready&&completed.units==3&&completed.progressed&&calls==3);
    CHECK(fields.generation(Which::egress)==generation&&fields.semantic_intent(Which::egress)==17);
    CHECK(fields.distance(Which::egress,{4,0})==4);
}

static void guarded_nonwork_states_never_request_admission(){
    ++groups;N::ServiceFields<9> fields;std::array<std::uint8_t,9> board{};board.fill(1);
    const std::array<N::Point,1> roots{{{0,0}}};unsigned calls=0;const auto grant=[&]{++calls;return true;};
    const auto invalid=fields.step_guarded(static_cast<Which>(255),32,grant);
    CHECK(invalid.status==N::NavigationStatus::failed&&invalid.units==0&&!invalid.progressed&&calls==0);
    const auto idle=fields.step_guarded(Which::service,32,grant);
    CHECK(idle.status==N::NavigationStatus::idle&&idle.units==0&&!idle.progressed&&calls==0);
    CHECK(!fields.begin(Which::service,0,3,board,roots,1));
    const auto failed=fields.step_guarded(Which::service,32,grant);
    CHECK(failed.status==N::NavigationStatus::failed&&failed.units==0&&!failed.progressed&&calls==0);
    CHECK(fields.begin(Which::service,3,3,board,std::span<const N::Point>{},2));
    const auto unreachable=fields.step_guarded(Which::service,32,grant);
    CHECK(unreachable.status==N::NavigationStatus::unreachable&&unreachable.units==0&&!unreachable.progressed&&calls==0);
    CHECK(fields.begin(Which::service,3,3,board,roots,3));
    const auto generation=fields.generation(Which::service);
    const auto zero=fields.step_guarded(Which::service,0,grant);
    CHECK(zero.status==N::NavigationStatus::preparing&&zero.units==0&&!zero.progressed&&calls==0);
    CHECK(fields.generation(Which::service)==generation&&!fields.available(Which::service,{0,0}));
    finish(fields,Which::service);
    const auto ready=fields.step_guarded(Which::service,32,grant);
    CHECK(ready.status==N::NavigationStatus::ready&&ready.units==0&&!ready.progressed&&calls==0);
    CHECK(fields.generation(Which::service)==generation&&fields.distance(Which::service,{2,2})==2);
}

static void guarded_reentrant_retirement_never_spends_an_old_grant(){
    ++groups;N::ServiceFields<9> fields;std::array<std::uint8_t,9> board{};board.fill(1);
    const std::array<N::Point,1> roots{{{0,0}}};
    CHECK(fields.begin(Which::service,3,3,board,roots,100));CHECK(fields.begin(Which::egress,3,3,board,roots,200));
    const auto first=fields.generation(Which::service);unsigned calls=0;
    const auto retired=fields.step_guarded(Which::service,5,[&]{++calls;fields.retire(Which::service);return true;});
    CHECK(retired.status==N::NavigationStatus::idle&&retired.units==0&&!retired.progressed&&calls==1);
    CHECK(!fields.active(Which::service)&&fields.generation(Which::service)==0);
    CHECK(fields.begin(Which::service,3,3,board,roots,100));const auto second=fields.generation(Which::service);CHECK(second!=first);
    calls=0;const auto rebound=fields.step_guarded(Which::service,5,[&]{
        ++calls;CHECK(fields.begin(Which::service,3,3,board,roots,101));return true;
    });
    CHECK(rebound.status==N::NavigationStatus::preparing&&rebound.units==0&&!rebound.progressed&&calls==1);
    CHECK(fields.generation(Which::service)!=second&&!fields.available(Which::service,{0,0}));
    const auto replacement=fields.generation(Which::service);calls=0;
    const auto old_work=fields.step_guarded(Which::service,5,[&]{
        if(++calls==2){fields.retire(Which::service);CHECK(fields.begin(Which::service,3,3,board,roots,102));}
        return true;
    });
    CHECK(old_work.status==N::NavigationStatus::preparing&&old_work.units==1&&old_work.progressed&&calls==2);
    CHECK(fields.generation(Which::service)!=replacement&&!fields.available(Which::service,{0,0}));
    const auto current=fields.generation(Which::service);calls=0;
    const auto other=fields.step_guarded(Which::service,2,[&]{++calls;fields.retire(Which::egress);return true;});
    CHECK(other.units==2&&other.progressed&&other.status==N::NavigationStatus::preparing&&calls==2);
    CHECK(fields.generation(Which::service)==current&&!fields.active(Which::egress));
    calls=0;const auto denied=fields.step_guarded(Which::service,5,[&]{++calls;fields.retire(Which::service);return false;});
    CHECK(denied.status==N::NavigationStatus::idle&&denied.units==0&&!denied.progressed&&calls==1);
}

int main(){
    two_fields_keep_partial_progress();ordered_root_and_intent_identity();copied_roots_and_retirement();
    shared_board_mutation_boundary();nominees_and_one_context_failure();
    guarded_denial_retains_the_exact_preparation_cursor();guarded_partial_allowance_preserves_per_origin_readiness();
    guarded_nonwork_states_never_request_admission();guarded_reentrant_retirement_never_spends_an_old_grant();
    std::printf("PASS native_service_fields: %u checks, %u groups\n",checks,groups);
}
