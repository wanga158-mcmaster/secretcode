#define WB_SERVICE_NATIVE_HOST_FREE
#include "capital_service_native.h"
#include <cstdio>
using namespace wb::capital;
static unsigned checks{},failures{};
static void check(bool value){++checks;if(!value)++failures;}
int main(){
    service_program::Frame f;f.owner=4;f.round=46;f.input=8;f.sample=1;f.pose={4,8};
    f.hp=700;f.alive=true;f.observed=f.fresh=true;f.channel=service_program::Channel::None;
    f.permissions=service_program::Move|service_program::Attack|service_program::Cast;
    f.kit_revision=2;f.inventory_revision=3;f.capability_revision=2;
    f.foe_revision=7;f.lock_revision=5;f.occupancy_revision=9;
    service_native::PreparationStamp a{4,46,8,11,11,false,false};
    check(service_native::preparation_rebindable(a,f));
    for(unsigned mode=0;mode<8;++mode){auto b=a;
        if(mode==0)b.owner=3;if(mode==1)b.round=47;if(mode==2)++b.input;if(mode==3)b.input=0;
        if(mode==4)++b.effect_generation;if(mode==5)b.pending=true;if(mode==6)b.purchase=true;if(mode==7)++b.observed_generation;
        check(!service_native::preparation_rebindable(b,f));
    }
    auto false_move=a;++false_move.effect_generation;false_move.pending=true;
    check(!service_native::preparation_rebindable(false_move,f));
    false_move.pending=false;check(!service_native::preparation_rebindable(false_move,f));
    false_move.observed_generation=false_move.effect_generation;check(service_native::preparation_rebindable(false_move,f));
    for(unsigned mode=0;mode<5;++mode){auto g=f;
        if(mode==0)g.fresh=false;if(mode==1)g.observed=false;if(mode==2)g.owner=5;if(mode==3)++g.round;if(mode==4)++g.input;
        check(!service_native::preparation_rebindable(a,g));
    }
    check(service_native::same_physical(f,f));
    for(unsigned mode=0;mode<22;++mode){auto g=f;
        switch(mode){
        case 0:++g.owner;break;case 1:++g.round;break;case 2:++g.input;break;case 3:++g.pose.x;break;
        case 4:++g.bag;break;case 5:++g.gold;break;case 6:g.hp-=1;break;case 7:g.alive=false;break;
        case 8:g.stunned=true;break;case 9:g.completed_turn=true;break;case 10:g.channel=service_program::Channel::Submit;break;
        case 11:g.permissions^=service_program::Attack;break;case 12:++g.kit_revision;break;case 13:++g.inventory_revision;break;
        case 14:++g.capability_revision;break;case 15:++g.occupancy_revision;break;case 16:++g.foe_revision;break;
        case 17:++g.lock_revision;break;case 18:++g.next_investment.cost;break;case 19:++g.next_investment.target;break;
        case 20:g.next_investment.observed=true;break;case 21:g.next_investment.available=true;break;}
        check(!service_native::same_physical(f,g));
    }
    auto observed=f;++observed.sample;++observed.source_revision;++observed.route_revision;
    observed.selected_group=2;observed.source_life=1;observed.available_fuel=19000;
    check(service_native::same_physical(f,observed));
    check(service_native::preparation_rebindable(a,observed));
    std::printf("capital_service_native memo: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
