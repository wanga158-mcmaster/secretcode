#include <cassert>
#include "../common/src/goal_receipt.h"

int main() {
    using p11::goal_receipt::Packet;
    Packet p;
    assert(!p.eligible(7,47,48)&&!p.eligible(6,48,48));
    assert(p.eligible(7,48,48));
    p.begin(48,1,9,4,8,false,57000);
    assert(p.current(48)&&!p.current(49)&&!p.eligible(7,48,48));
    assert(p.selected==1&&p.native_group==9&&p.origin_x==4&&p.origin_y==8);
    assert(p.stage==1&&p.target==-1&&p.goal_before==-1&&p.goal_round==-1);
    assert(!p.flushable(48,100000)&&!p.flushable(49,89999));
    assert(p.flushable(49,90000));
    // A suspended goal operation remains visibly cross-epoch; recording it
    // cannot silently turn the old round into a fresh action permission.
    p.goal_before=55000;p.goal_after=18000;p.goal_round=49;p.stage=4;
    assert(p.round==48&&p.goal_round!=p.round&&p.move_after==-1);
    assert(p.flush(49,95000));
    assert(p.printed&&!p.flushable(50,100000)&&!p.flush(50,100000));
    Packet refusal;
    refusal.begin(48,-1,-1,2,3,false,48000);
    assert(refusal.selected==-1&&refusal.stage==1&&refusal.goal_before==-1);
}
