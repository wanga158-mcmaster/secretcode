#include "../counterpart/src/counterpart.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
namespace c=plan4::counterpart;
namespace {
int checks=0;
void check(bool v,const char* m){++checks;if(!v){std::fprintf(stderr,"FAIL: %s\n",m);std::exit(1);}}
struct World { Point not_open{-1,-1}; int quotes=0; };
int distance(Point a,Point b,const void*){return c::chebyshev(a,b);}
int open(Point p,const void* v){return p==static_cast<const World*>(v)->not_open?0:1;}
bool response(const c::ResponseInput& input,c::Response& out,const void* v){
    ++static_cast<World*>(const_cast<void*>(v))->quotes;
    out.rival_position=input.rival;out.supported=true;
    if(input.decision.branch==c::Branch::idle)return true;
    if(c::chebyshev(input.rival,input.decision.target)<=1){
        if(input.decision.branch==c::Branch::chase&&input.decision.target_id==7
            &&c::chebyshev(input.rival,input.own.position)<=1)out.own_damage=std::min(10.0f,input.own_health);
        return true;
    }
    const Point step{input.rival.x+(input.decision.target.x>input.rival.x)-(input.decision.target.x<input.rival.x),
                     input.rival.y+(input.decision.target.y>input.rival.y)-(input.decision.target.y<input.rival.y)};
    if(step!=input.own.position)out.rival_position=step;
    return true;
}
void base(c::Input& i,World& world){
    i.round=10;i.own_id=7;i.rival_id=9;i.turns=1;i.before={{3,3},10};i.after[0]={{3,2},10};
    i.own_health=100;i.rival={6,3};i.rival_provenance={10,Source::personal_vision,Status::exact_now};
    i.rival_personally_visible=true;i.policy={123,true,true,true};i.geometry={distance,open,&world};
    i.corridor_min={0,0};i.corridor_max={12,6};i.public_vision_radius=8;
    i.vision_modifiers_known=i.complete_choice_inputs=i.order_known=i.opposing_teams=true;
    i.rival_before_own_current_round=i.rival_already_completed=false;i.other_count=0;i.work_budget={};
    i.camp_priority[0]={101,{11,3}};i.camp_count=1;i.quote_response=response;i.quote_context=&world;
}
void causal_response(){
    World world;auto left=std::make_unique<c::Input>();base(*left,world);auto right=std::make_unique<c::Input>(*left);
    right->after[0].position={3,4};c::Result a,b,ca,cb;
    c::predict_into(*left,true,a);c::predict_into(*right,true,b);
    c::predict_into(*left,false,ca);c::predict_into(*right,false,cb);
    check(a.supported&&b.supported&&a.steps[0].rival_after==Point{5,2}&&b.steps[0].rival_after==Point{5,4},
          "two legal first steps induce distinct deterministic rival chase responses");
    check(ca.supported&&cb.supported&&ca.steps[0].rival_after==Point{5,3}&&cb.steps[0].rival_after==Point{5,3},
          "disabled policy response intent is frozen from the same pre-action state");
    check(a.steps[0].status==Status::predicted&&left->rival==Point{6,3},"predicted response cannot overwrite exact current rival state");
    left->before.bag=right->before.bag=left->after[0].bag=right->after[0].bag=0;
    c::predict_into(*left,true,a);c::predict_into(*right,true,b);
    check(a.steps[0].decision.branch==c::Branch::camp&&a.steps[0].rival_after==b.steps[0].rival_after,
          "zero-bag negative pair follows the same fixed camp priority");
    base(*left,world);left->other_count=1;
    left->other_targets[0]={{1,{6,1},5,true},{10,Source::personal_vision,Status::exact_now},true};
    *right=*left;right->after[0].position={3,4};
    c::predict_into(*left,true,a);c::predict_into(*right,true,b);
    check(a.supported&&b.supported&&a.steps[0].decision.target_id==1&&b.steps[0].decision.target_id==1,
          "nearer supported other target makes candidate response a negative control");
    left->complete_choice_inputs=false;c::predict_into(*left,true,a);
    check(!a.supported&&a.unsupported==c::Unsupported::observability,"unseen possible rival choice inputs force fallback");
}
void phase_order_and_guards(){
    World world;auto i=std::make_unique<c::Input>();base(*i,world);i->turns=6;
    for(int n=0;n<6;++n)i->after[n]={{3,n%2?1:2},10};
    c::Result out;c::predict_into(*i,true,out);
    check(out.supported&&out.count==6,"bounded model emits no more than six ordered rival responses");
    check(out.steps[1].decision.target==i->after[0].position,
          "next-round rival-before-owner response sees prior own position, not future action");
    i->rival_before_own_current_round=i->rival_already_completed=true;c::predict_into(*i,true,out);
    check(out.supported&&out.count==5&&out.steps[0].round==11,
          "already completed rival turn is never replayed inside current owner action");
    i->rival_already_completed=false;c::predict_into(*i,true,out);
    check(!out.supported&&out.unsupported==c::Unsupported::ordering,"inconsistent observed completion/order proof rejects");
    base(*i,world);i->policy.matches_rule=false;c::predict_into(*i,true,out);
    check(!out.supported&&out.unsupported==c::Unsupported::unknown_policy,"unfamiliar opponent never presumed to obey fixture rule");
    base(*i,world);i->rival_personally_visible=false;c::predict_into(*i,true,out);
    check(!out.supported&&out.unsupported==c::Unsupported::stale_rival,"hidden rival does not enter model");
    base(*i,world);world.not_open={2,2};c::predict_into(*i,true,out);
    check(!out.supported&&out.unsupported==c::Unsupported::observability,"public occlusion/bush corridor fails guard");
    world.not_open={-1,-1};base(*i,world);i->vision_modifiers_known=false;c::predict_into(*i,true,out);
    check(!out.supported,"unknown vision modifiers do not borrow another actor's vision");
    base(*i,world);i->public_vision_radius=2;c::predict_into(*i,true,out);
    check(!out.supported,"action outside guaranteed rival vision is unsupported negative fixture");
    base(*i,world);i->after[0].position={0,0};c::predict_into(*i,true,out);
    check(!out.supported&&out.unsupported==c::Unsupported::path,"unquoted multi-tile first movement rejected");
    base(*i,world);i->rival={4,3};i->rival_before_own_current_round=i->rival_already_completed=true;
    i->after[0].position=i->rival;c::predict_into(*i,true,out);
    check(!out.supported&&out.unsupported==c::Unsupported::path,"already-completed rival body blocks the first own step");
}
bool deny(const void*){return false;}
int unknown(Point,Point,const void*){return -1;}
void rule_and_physics(){
    World world;const c::Geometry geometry{distance,open,&world};
    std::array<c::Target,2> targets{{{7,{2,1},4,true},{3,{2,3},2,true}}};
    const c::Camp camp{101,{8,2}};
    const auto decision=c::fixture_decision({4,2},targets.data(),2,&camp,1,0,geometry);
    check(decision.branch==c::Branch::chase&&decision.target_id==3,"fixture nearest-distance exact tie uses public target ID");
    const auto bad=c::fixture_decision({4,2},targets.data(),2,&camp,1,0,{unknown,open,&world});
    check(bad.branch==c::Branch::unsupported,"unknown nearest terrain distance cannot be skipped in fixture rule");
    auto i=std::make_unique<c::Input>();base(*i,world);i->rival={4,3};
    i->after[0].position={3,2};c::Result near,far;
    c::predict_into(*i,false,near);i->after[0].position={2,3};c::predict_into(*i,false,far);
    check(near.supported&&far.supported&&near.own_damage==10&&far.own_damage==0,
          "control freezes rival intent but preserves common physical damage dependence on actual candidate body");
    base(*i,world);i->work_budget={deny,nullptr};const int before=world.quotes;c::predict_into(*i,true,near);
    check(!near.supported&&near.unsupported==c::Unsupported::budget&&world.quotes==before,"denied work does not invoke response estimator");
}
}
int main(){causal_response();phase_order_and_guards();rule_and_physics();
    std::printf("counterpart checks=%d synthetic-only legal-fixture-order-observability=pending\n",checks);}
