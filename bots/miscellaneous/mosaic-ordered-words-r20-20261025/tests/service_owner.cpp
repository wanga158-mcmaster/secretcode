#include "service_owner.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace mosaic::service_owner;
unsigned checks=0;
void check(bool value,int line){++checks;if(!value){std::fprintf(stderr,"service-owner failure line%d check%u\n",line,checks);std::abort();}}
#define CHECK(value) check(bool(value),__LINE__)

void retained_enabling_observation(){
    Observation owner(4);
    CHECK(owner.request(7,42,10)&&owner.active());
    CHECK(owner.job()==7&&owner.member()==42&&owner.life()==Life::unknown);
    // Work halo -> observation is a semantic ownership change in the adapter.
    // Nominal goal attainment and a prepared field do NOT restore work ownership.
    CHECK(owner.enter(11));
    CHECK(!owner.request(7,42,11)&&owner.active()); // same field/request, no effect
    CHECK(!owner.observed(7,41,11,Life::alive));
    CHECK(!owner.observed(7,42,10,Life::alive));
    CHECK(!owner.observed(7,42,12,Life::alive));
    CHECK(!owner.observed(7,42,11,Life::unknown));
    CHECK(owner.active()&&owner.blocked_inputs()==0);
    CHECK(!owner.blocked(11)&&owner.blocked_inputs()==1);
    CHECK(!owner.blocked(11)&&owner.blocked_inputs()==1);
    CHECK(owner.enter(12)&&owner.progress(12,1)); // actual advancing move
    CHECK(owner.active()&&owner.blocked_inputs()==0);
    CHECK(!owner.blocked(12));
    CHECK(owner.enter(13)&&!owner.request(7,42,13)); // new epoch/field commit
    CHECK(owner.active()&&!owner.progress(13,1)); // stale receipt cannot renew
    CHECK(!owner.blocked(13)&&owner.blocked_inputs()==1);
    CHECK(owner.observed(7,42,13,Life::dead)); // actual CURRENT life read
    CHECK(!owner.active()&&owner.status()==Status::completed&&owner.life()==Life::dead);
    CHECK(!owner.request(7,42,14)&&owner.status()==Status::completed);
    CHECK(!owner.observed(7,42,14,Life::alive));
    CHECK(owner.observed_frame()==13&&owner.completed());
    CHECK(owner.renew_after_observed(14)&&owner.active()&&owner.life()==Life::unknown);
    CHECK(!owner.request(7,42,14));
    CHECK(!owner.blocked(14)&&owner.blocked_inputs()==1); // old read is not new progress
    CHECK(owner.observed(7,42,14,Life::alive)&&owner.completed());
}
void finite_nonprogress(){
    Observation owner(4);CHECK(owner.request(3,9,100));
    for(Frame frame=100;frame<104;++frame){
        CHECK(owner.enter(frame));
        CHECK(!owner.request(3,9,frame));
        // Simulated field commit: no physical/info receipt and no life query.
        CHECK(owner.active());
        const auto before=owner.blocked_inputs();
        CHECK(owner.blocked(frame)==(frame==103));
        CHECK(owner.blocked_inputs()==before+1);
        CHECK(!owner.blocked(frame));
    }
    CHECK(!owner.active()&&owner.status()==Status::blocked&&owner.life()==Life::unknown);
    CHECK(!owner.request(3,9,104)&&!owner.active());
    CHECK(!owner.renew_after_observed(104)&&owner.status()==Status::blocked);
    CHECK(!owner.progress(104,99)); // no reopening by a late unrelated action
    CHECK(owner.enter(105)&&!owner.request(3,9,105)&&owner.blocked_inputs()==4);
    // If an actual fresh read happens anyway, it is real completion, not a
    // field/epoch based renewal. The owner remains inactive.
    CHECK(owner.observed(3,9,105,Life::alive));
    CHECK(owner.status()==Status::completed&&!owner.active());
}
void semantic_change_and_receipts(){
    Observation owner(2);CHECK(owner.request(1,20,1));
    CHECK(!owner.blocked(1)&&owner.blocked_inputs()==1);
    CHECK(owner.progress(1,5)&&owner.blocked_inputs()==0); // actual later effect
    CHECK(!owner.blocked(1));
    CHECK(owner.request(2,30,2)&&owner.active()); // old job invalidated
    CHECK(owner.observed(1,20,2,Life::alive)&&owner.active()); // resolves retained old key, not selected B
    CHECK(owner.job()==2&&owner.member()==30);
    CHECK(!owner.progress(2,5)); // old key's receipt cannot be replayed
    CHECK(!owner.blocked(2)&&owner.blocked_inputs()==1);
    CHECK(owner.enter(3)&&owner.progress(3,6)&&owner.blocked_inputs()==0);
    CHECK(owner.request(3,-1,4)&&!owner.active()&&owner.status()==Status::idle);
    CHECK(owner.observed(2,30,4,Life::alive)&&owner.status()==Status::idle);
    CHECK(owner.request(3,40,4)&&owner.active());
    CHECK(!owner.observed(3,40,4,Life(99))&&owner.active());
    CHECK(owner.observed(3,40,4,Life::alive)&&owner.life()==Life::alive);
    CHECK(!owner.request(4,50,3)&&owner.job()==3&&owner.member()==40); // stale key
}
void input_fences(){
    Observation invalid(0);CHECK(!invalid.request(1,1,0));
    CHECK(!invalid.active()&&invalid.status()==Status::invalid_limit);
    Observation owner(1);CHECK(owner.request(1,1,0)); // round0 is valid
    CHECK(!owner.blocked(1)&&owner.blocked_inputs()==0); // future is not current
    CHECK(!owner.progress(1,1));
    CHECK(owner.enter(1)&&!owner.enter(0));
    CHECK(owner.blocked(1)&&owner.status()==Status::blocked);
    CHECK(owner.request(1,2,2)&&owner.active()); // actual semantic member change
    CHECK(owner.enter(std::numeric_limits<Frame>::max()));
    CHECK(!owner.request(9,9,0)&&owner.member()==2);
    CHECK(!owner.observed(1,2,0,Life::alive));
    CHECK(!owner.renew_after_observed(0));
    Observation changed(2);CHECK(changed.request(1,1,1));
    CHECK(changed.progress(1,1)&&changed.request(2,2,1));
    CHECK(!changed.blocked(1)&&changed.blocked_inputs()==1); // old key's effect not inherited
}
void catalog_nonprogress_survives_switches(){
    Observation owner(4);CHECK(owner.request(1,10,1));
    for(Frame frame=1;frame<=3;++frame){
        CHECK(owner.enter(frame)&&!owner.request(1,10,frame));
        CHECK(!owner.blocked(frame)&&owner.blocked_inputs()==frame);
    }
    CHECK(owner.request(2,20,4)&&owner.active()&&owner.blocked_inputs()==0);
    CHECK(!owner.blocked(4)&&owner.blocked_inputs()==1);
    CHECK(owner.request(1,10,4)&&owner.active()&&owner.blocked_inputs()==3);
    CHECK(owner.blocked(4)&&owner.status()==Status::blocked&&owner.blocked_inputs()==4);
    CHECK(owner.request(2,20,5)&&owner.active()&&owner.blocked_inputs()==1);
    CHECK(owner.request(1,10,5)&&!owner.active()&&owner.status()==Status::blocked);
    CHECK(!owner.request(1,10,6)&&owner.blocked_inputs()==4);
    CHECK(!owner.renew_after_observed(6));
    CHECK(owner.catalog_size()==2);
    // An actual fresh A life read may arrive before A is selected again.
    CHECK(owner.request(2,20,6)&&owner.active());
    CHECK(owner.observed(1,10,6,Life::alive)&&owner.job()==2&&owner.member()==20);
    CHECK(owner.active()&&owner.blocked_inputs()==1); // A receipt does not renew B
    CHECK(owner.request(1,10,6)&&owner.completed()&&owner.life()==Life::alive);
    CHECK(owner.renew_after_observed(6)&&owner.active()&&owner.blocked_inputs()==0);
    CHECK(!owner.blocked(6)&&owner.blocked_inputs()==1); // no fabricated current-episode effect
    CHECK(owner.request(2,20,7)&&owner.blocked_inputs()==1);
    CHECK(!owner.blocked(7)&&owner.blocked_inputs()==2);
    CHECK(owner.request(1,10,7)&&owner.blocked_inputs()==1);
    CHECK(owner.progress(7,1)&&owner.blocked_inputs()==0); // actual A-only advancement
    CHECK(owner.request(2,20,7)&&owner.blocked_inputs()==2);
    CHECK(!owner.progress(7,1)&&!owner.blocked(7)&&owner.blocked_inputs()==2);
    CHECK(owner.request(1,10,8)&&owner.blocked_inputs()==0);
    CHECK(!owner.blocked(8)&&owner.blocked_inputs()==1);
    CHECK(owner.request(2,20,8)&&owner.blocked_inputs()==2);
    CHECK(owner.progress(8,2)&&owner.blocked_inputs()==0);
    CHECK(owner.request(1,10,8)&&owner.blocked_inputs()==1); // B-only progress did not renew A
    CHECK(!owner.observed(9,99,8,Life::dead)); // never nominated is no completion receipt
}
int main(){retained_enabling_observation();finite_nonprogress();semantic_change_and_receipts();input_fences();catalog_nonprogress_survives_switches();std::printf("%u service-owner checks passed\n",checks);}
