#include "return_funding.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace mosaic::return_funding;
unsigned checks=0;
void check(bool ok,int line){++checks;if(!ok){std::fprintf(stderr,"return-funding failed %d check%u\n",line,checks);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)
const Token initial{19,40,0,7};
const Bounds whole{3000,5000,11000,16000,true};
Witness action(bool applied=true){Witness w;w.actual_attempt=true;w.applied=applied;return w;}
Witness feedback(bool move=true){Witness w;w.actual_feedback=true;w.movement_ready=move;w.movement_spent=!move;w.failure_closed=true;return w;}
Witness repriced(){Witness w;w.actual_reprice=true;return w;}

void whole_tail_and_paid_prefix(){
    // A3K + actual feedback5K + route/model reprice11K + Move/false closure16K.
    // The legacy24..28K work entry cannot fund this35K complete operation.
    CHECK(remaining(whole).known&&remaining(whole).amount==35000);
    for(Fuel fuel=24000;fuel<=28000;++fuel){
        Tail tail;CHECK(tail.start(initial,whole,fuel)==Reason::unfunded);
        CHECK(!tail.valid()&&tail.fallback_required());
    }
    Tail tail;CHECK(tail.start(initial,whole,35000)==Reason::unfunded);
    CHECK(tail.start(initial,whole,35001)==Reason::ok);
    CHECK(tail.begin(Phase::action,initial,35001));
    Token after=initial;++after.effect;
    CHECK(tail.finish(Phase::action,after,32001,action())==Reason::ok);
    CHECK(tail.remaining()==32000&&tail.next()==Phase::feedback);
    CHECK(funded(32001,{32000,true})&&!funded(32001,{35000,true}));
    CHECK(tail.begin(Phase::feedback,after,32001)); // no second charge for paid3K

    // Actual complete execution with exactly the declared phase expenditures.
    CHECK(tail.start(initial,whole,40000)==Reason::ok);
    CHECK(tail.begin(Phase::action,initial,40000));
    CHECK(tail.finish(Phase::action,after,37000,action())==Reason::ok);
    CHECK(tail.begin(Phase::feedback,after,37000));++after.observation;
    CHECK(tail.finish(Phase::feedback,after,32000,feedback())==Reason::ok&&tail.remaining()==27000);
    CHECK(tail.begin(Phase::reprice,after,32000));
    CHECK(tail.finish(Phase::reprice,after,21000,repriced())==Reason::ok&&tail.remaining()==16000);
    CHECK(tail.begin(Phase::physical,after,21000));++after.effect;
    CHECK(tail.finish(Phase::physical,after,5000,action())==Reason::ok);
    CHECK(tail.done()&&tail.remaining()==0&&!tail.fallback_required());
}
void optional_erosion_and_overrun(){
    Tail tail;CHECK(tail.start(initial,whole,60000)==Reason::ok);
    Fuel actual=60000;unsigned menus=0;
    while(tail.optional_fits(initial,actual,10000)){actual-=10000;++menus;}
    CHECK(menus==2&&actual==40000&&actual>tail.remaining());
    CHECK(!tail.optional_fits(initial,40000,5000)); // strict equality leaves no margin
    CHECK(!tail.optional_fits(initial,actual,0));
    CHECK(tail.begin(Phase::action,initial,actual));
    CHECK(!tail.optional_fits(initial,actual,1000)); // no menu inside paid phase
    Token after=initial;++after.effect;
    CHECK(tail.finish(Phase::action,after,actual-3000,action())==Reason::ok);

    // A lying/overrunning optional bound is caught BEFORE the work attempt.
    // A fresh independently validated16K physical fallback can remain funded;
    // this resource predicate alone does not manufacture its SDK authority.
    CHECK(tail.start(initial,whole,60000)==Reason::ok);
    CHECK(tail.optional_fits(initial,60000,10000));
    CHECK(!tail.begin(Phase::action,initial,34000));
    CHECK(tail.reason()==Reason::unfunded&&tail.fallback_required()&&!tail.valid());
    CHECK(funded(34000,{16000,true}));
    // Even when ample total fuel remains, a measured phase overrun revokes the
    // stated bound. The actual SDK effect has happened and cannot be undone.
    CHECK(tail.start(initial,whole,60000)==Reason::ok);
    CHECK(tail.begin(Phase::action,initial,60000));
    CHECK(tail.finish(Phase::action,after,56999,action())==Reason::overrun);
    CHECK(!tail.valid()&&tail.fallback_required());
}
void feedback_and_frame_authority(){
    Tail tail;Token after=initial;++after.effect;
    CHECK(tail.start(initial,whole,60000)==Reason::ok);
    CHECK(tail.begin(Phase::action,initial,60000));
    CHECK(tail.finish(Phase::action,after,57000,action())==Reason::ok);
    CHECK(!tail.begin(Phase::physical,after,57000)); // paid action cannot skip real feedback
    for(unsigned stale=0;stale<4;++stale){
        CHECK(tail.start(initial,whole,60000)==Reason::ok);Token wrong=initial;
        if(stale==0)++wrong.revision;if(stale==1)++wrong.frame;
        if(stale==2)++wrong.effect;if(stale==3)++wrong.observation;
        CHECK(!tail.begin(Phase::action,wrong,60000)&&tail.reason()==Reason::stale_token);
    }
    // A decremented number or renamed observation is not an actual SDK query.
    CHECK(tail.start(initial,whole,60000)==Reason::ok);
    CHECK(tail.begin(Phase::action,initial,60000));
    CHECK(tail.finish(Phase::action,after,57000,action())==Reason::ok);
    CHECK(tail.begin(Phase::feedback,after,57000));++after.observation;
    auto fake=feedback();fake.actual_feedback=false;
    CHECK(tail.finish(Phase::feedback,after,52000,fake)==Reason::missing_witness);
    CHECK(tail.fallback_required());
    // Work is real, but a suspension into the next owner frame revokes the
    // entire stale continuation even if its numerical tail remains funded.
    CHECK(tail.start(initial,whole,60000)==Reason::ok);
    CHECK(tail.begin(Phase::action,initial,60000));after=initial;++after.effect;++after.frame;
    CHECK(tail.finish(Phase::action,after,57000,action())==Reason::stale_token);
}
void refused_actions_need_actual_closure(){
    Tail tail;CHECK(tail.start(initial,whole,60000)==Reason::ok);
    CHECK(tail.begin(Phase::action,initial,60000));
    CHECK(tail.finish(Phase::action,initial,58000,action(false))==Reason::ok);
    CHECK(tail.begin(Phase::feedback,initial,58000));Token after=initial;++after.observation;
    auto open=feedback();open.failure_closed=false;
    CHECK(tail.finish(Phase::feedback,after,53000,open)==Reason::missing_witness);

    CHECK(tail.start(initial,whole,60000)==Reason::ok);
    CHECK(tail.begin(Phase::action,initial,60000));after=initial;++after.effect;
    CHECK(tail.finish(Phase::action,after,57000,action())==Reason::ok);
    CHECK(tail.begin(Phase::feedback,after,57000));++after.observation;
    CHECK(tail.finish(Phase::feedback,after,52000,feedback())==Reason::ok);
    CHECK(tail.begin(Phase::reprice,after,52000));
    CHECK(tail.finish(Phase::reprice,after,41000,repriced())==Reason::ok);
    CHECK(tail.begin(Phase::physical,after,41000));++after.observation;
    auto refusal=action(false);refusal.actual_feedback=refusal.failure_closed=true;
    CHECK(tail.finish(Phase::physical,after,37000,refusal)==Reason::ok&&tail.done());
}
void absent_physical_requires_actual_postcondition(){
    const Bounds spent{3000,5000,11000,0,true,false};
    CHECK(remaining(spent).amount==19000);
    Tail tail;CHECK(tail.start(initial,spent,20001)==Reason::ok);
    CHECK(tail.begin(Phase::action,initial,20001));Token after=initial;++after.effect;
    CHECK(tail.finish(Phase::action,after,17001,action())==Reason::ok);
    CHECK(tail.begin(Phase::feedback,after,17001));++after.observation;
    CHECK(tail.finish(Phase::feedback,after,12001,feedback(false))==Reason::ok);
    CHECK(tail.begin(Phase::reprice,after,12001));
    CHECK(tail.finish(Phase::reprice,after,1001,repriced())==Reason::ok&&tail.done());
    // The0 physical quote alone cannot erase the mandatory operation. Actual
    // feedback must prove M was spent, or a real channel is now maintained.
    CHECK(tail.start(initial,spent,20001)==Reason::ok);
    CHECK(tail.begin(Phase::action,initial,20001));after=initial;++after.effect;
    CHECK(tail.finish(Phase::action,after,17001,action())==Reason::ok);
    CHECK(tail.begin(Phase::feedback,after,17001));++after.observation;
    CHECK(tail.finish(Phase::feedback,after,12001,feedback())==Reason::postcondition);
    CHECK(tail.fallback_required());
    CHECK(tail.start(initial,spent,20001)==Reason::ok);
    CHECK(tail.begin(Phase::action,initial,20001));after=initial;++after.effect;
    CHECK(tail.finish(Phase::action,after,17001,action())==Reason::ok);
    CHECK(tail.begin(Phase::feedback,after,17001));++after.observation;
    auto channel=feedback();channel.channel_active=true;
    CHECK(tail.finish(Phase::feedback,after,12001,channel)==Reason::ok);
    CHECK(tail.begin(Phase::reprice,after,12001));
    CHECK(tail.finish(Phase::reprice,after,1001,repriced())==Reason::ok&&tail.done());
    // A predicted Basic->Move may become an actually complete return when its
    // lethal hit frees CURRENT quiet support. Only fresh feedback and actual
    // repricing can discharge the Move obligation; no false attempt is invented.
    CHECK(tail.start(initial,whole,40000)==Reason::ok);
    CHECK(tail.begin(Phase::action,initial,40000));after=initial;++after.effect;
    CHECK(tail.finish(Phase::action,after,37000,action())==Reason::ok);
    CHECK(tail.begin(Phase::feedback,after,37000));++after.observation;
    CHECK(tail.finish(Phase::feedback,after,32000,feedback())==Reason::ok);
    CHECK(tail.begin(Phase::reprice,after,32000));auto quiet=repriced();quiet.return_complete=true;
    CHECK(tail.finish(Phase::reprice,after,21000,quiet)==Reason::ok&&tail.done());
    CHECK(tail.remaining()==0&&21000>whole.physical);
}
void dynamic_costs_and_overflow(){
    const auto evaluation=affine_bound(2500,80,70,30,3);
    CHECK(evaluation.known&&evaluation.amount==11200);
    auto dynamic=whole;dynamic.reprice=evaluation.amount;
    CHECK(remaining(dynamic).amount==35200);
    CHECK(!funded(35200,remaining(dynamic))&&funded(35201,remaining(dynamic)));
    Fuel result{};CHECK(!checked_add(limit,1,result));CHECK(!checked_multiply(limit,2,result));
    CHECK(!affine_bound(2500,80,70,limit,limit).known);
    Tail tail;auto invalid=whole;invalid.known=false;
    CHECK(tail.start(initial,invalid,60000)==Reason::unknown_cost&&!tail.valid());
    invalid=whole;invalid.reprice=0;CHECK(tail.start(initial,invalid,60000)!=Reason::ok&&!tail.valid());
    invalid=whole;invalid.reprice=limit;CHECK(!remaining(invalid).known);
    CHECK(tail.start(initial,invalid,limit)==Reason::overflow&&!tail.valid());
    CHECK(tail.start(initial,whole,limit+1)==Reason::invalid_fuel);
    CHECK(!funded(60000,{0,true})&&!funded(60000,{16000,false}));
    CHECK(affine_bound(2500,80,70,0,8).amount==2500);
}
int main(){
    whole_tail_and_paid_prefix();optional_erosion_and_overrun();feedback_and_frame_authority();
    refused_actions_need_actual_closure();absent_physical_requires_actual_postcondition();dynamic_costs_and_overflow();
    std::printf("%u return-funding checks passed\n",checks);
}
