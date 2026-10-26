#pragma once
#include <cstdio>

namespace p11::goal_receipt {
// One bounded diagnostic record. It is neither an executable job nor an
// instruction to relax a reserve. Unknown phases keep their negative sentinel.
struct Packet {
    bool captured=false,printed=false,partial=false,reward=false,moved=false;
    bool recovering_enter=false,recovering_now=false;
    int round=-1,selected=-2,retained_target=-1,native_group=-1,origin_x=-1,origin_y=-1;
    int stage=0,target=-1,target_x=-1,target_y=-1,radius=-1,members=0;
    int select_fuel=-1,goal_before=-1,goal_after=-1,goal_round=-1,move_after=-1;
    bool eligible(unsigned identity,int now,int wanted) const {
        return !captured&&identity==7&&now==wanted;
    }
    void begin(int now,int group,int native,int x,int y,bool is_partial,int fuel) {
        captured=true;round=now;selected=group;native_group=native;
        origin_x=x;origin_y=y;partial=is_partial;select_fuel=fuel;stage=1;
    }
    bool current(int now) const {return captured&&!printed&&round==now;}
    bool flushable(int now,int fuel) const {
        return captured&&!printed&&now>round&&fuel>=90000;
    }
    bool flush(int now,int fuel) {
        if(!flushable(now,fuel))return false;
        printed=true;
        std::printf("p11goal round=%d flush=%d selected=%d retained_target=%d native=%d origin=%d,%d partial=%d reward=%d stage=%d target=%d target_xy=%d,%d radius=%d members=%d select_fuel=%d goal_before=%d goal_after=%d goal_round=%d moved=%d move_after=%d recovering_enter=%d recovering_now=%d\n",
            round,now,selected,retained_target,native_group,origin_x,origin_y,int(partial),int(reward),stage,
            target,target_x,target_y,radius,members,select_fuel,goal_before,goal_after,
            goal_round,int(moved),move_after,int(recovering_enter),int(recovering_now));
        return true;
    }
};
}
