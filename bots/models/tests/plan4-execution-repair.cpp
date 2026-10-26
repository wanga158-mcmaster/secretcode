#include "../common/model-runtime.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace plan4;
using namespace plan4::runtime;
struct Model { ExecutionRepair proposal;ExecutionRepair execution_repair(const ExecutionRepairFrame&){return proposal;} };
int main(){
    static_assert(observes_execution_repairs<Model> && !observes_execution_repairs<M0>);
    auto info=std::make_unique<Information>();info->round=7;info->own.id=17;info->own.bag=24;info->altar_count=1;
    info->altars[0].accessible=true;Menu menu;menu.count=2;menu.jobs[0]={JobKind::hold};menu.jobs[1]={JobKind::bank,-1,-1,0};
    ExecutionRepairFrame frame{{7,{17}},info.get(),&menu,menu.jobs[0],5,-1,true,false};
    Model model{{true,ExecutionRepairAction::bank,1,5}};int cases=0;
    assert(execution_repair(model,frame).action==ExecutionRepairAction::bank);++cases;
    M0 base;assert(!execution_repair(base,frame).supported);++cases;
    for(int mode=0;mode<9;++mode){
        auto changed=frame;auto proposal=model.proposal;
        if(mode==0)changed.clock.round=6;
        if(mode==1)changed.clock.own.id=18;
        if(mode==2)changed.same_epoch=false;
        if(mode==3)changed.job_epoch=0;
        if(mode==4)proposal.job_epoch=4;
        if(mode==5)proposal.bank_menu_index=0;
        if(mode==6)proposal.bank_menu_index=2;
        if(mode==7)proposal.action=ExecutionRepairAction::alternate_path;
        if(mode==8)proposal.supported=false;
        assert(!checked_execution_repair(changed,proposal).supported);++cases;
    }
    info->altars[0].accessible=false;assert(!execution_repair(model,frame).supported);++cases;info->altars[0].accessible=true;
    info->own.bag=0;assert(!execution_repair(model,frame).supported);++cases;info->own.bag=24;
    info->own.alive=false;assert(!execution_repair(model,frame).supported);++cases;info->own.alive=true;
    for(int mode=0;mode<3;++mode){
        auto changed=frame;
        if(mode==0)changed.protected_progress=true;
        if(mode==1)info->own.channel=Channel::tribute;
        if(mode==2)info->in_progress_group_id=3;
        assert(execution_repair(model,changed).action==ExecutionRepairAction::preserve);++cases;
        info->own.channel=Channel::none;info->in_progress_group_id=-1;
    }
    for(auto action:{ExecutionRepairAction::none,ExecutionRepairAction::preserve,ExecutionRepairAction::renew,
                     ExecutionRepairAction::replan,ExecutionRepairAction::safe_retreat}){
        model.proposal.action=action;assert(execution_repair(model,frame).action==action);++cases;
    }
    // Complete synthetic ordinary route; prefixes, foreign clocks and an
    // unbound goal cannot admit an alternate action at the common boundary.
    detour::Route route;route.actor=17;route.round=7;route.origin={0,0};route.obstruction={1,0};route.goal={2,0};
    route.steps[0]={0,1};route.steps[1]={1,1};route.steps[2]={2,0};route.count=3;route.visits=5;route.proven=true;
    auto alternate=frame;alternate.selected_job=menu.jobs[1];alternate.phase_known=true;
    alternate.alternate_route=&route;alternate.ordinary_goal_bound=true;
    model.proposal={true,ExecutionRepairAction::alternate_path,-1,5};
    assert(execution_repair(model,alternate).action==ExecutionRepairAction::alternate_path);++cases;
    for(int variant=0;variant<8;++variant) {
        auto changed=alternate;auto broken=route;changed.alternate_route=&broken;
        if(variant==0)changed.ordinary_goal_bound=false;
        if(variant==1)changed.phase_known=false;
        if(variant==2)broken.actor=18;
        if(variant==3)broken.round=6;
        if(variant==4)broken.count=2;
        if(variant==5)broken.proven=false;
        if(variant==6)changed.selected_job.kind=JobKind::hold;
        if(variant==7)changed.phase=ProductionPhase::capture;
        assert(!execution_repair(model,changed).supported);++cases;
    }
    alternate.protected_progress=true;
    assert(execution_repair(model,alternate).action==ExecutionRepairAction::preserve);++cases;
    std::cout<<"execution repair cases="<<cases<<"\n";
}
