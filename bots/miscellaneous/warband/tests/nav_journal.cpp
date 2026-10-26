#include <cassert>
#include "nav_journal.h"
int main() {
    wb::NavJournalFrame f;
    wb::PackMsg p;
    p.objective=12; p.ax=2; p.ay=19; p.mode=wb::Mode::fight; p.stamp=94;
    f.begin(350,1,p,10,23);
    assert(f.active && f.read_pack.objective==12);
    f.route_to(2,8,p); f.call(1,-1,2,19,1); f.call(3,-1,-1,-1,1); f.step(10,23,11,24);
    assert(f.count==2 && f.calls[0].route==2 && f.calls[0].x==2 && f.moved && f.to_x==11);
    f.begin(351,1,p,11,24);
    assert(f.active && !f.moved && f.count==0 && !f.overflow);
    f.route_to(1,9,p); f.call(2,12,-1,-1,6); f.call(1,-1,1,15,7); f.step(11,24,10,23);
    assert(f.calls[0].kind==2 && f.calls[0].group==12 && f.calls[1].kind==1 && f.to_x==10);
    for(int i=0;i<6;++i) f.call(3,-1,-1,-1,i);
    assert(f.count==6 && f.overflow);
    f.begin(350,0,p,0,0); f.call(1,0,0,0,0);
    assert(!f.active && f.count==0);
    f.begin(352,1,p,0,0);
    assert(!f.active && f.count==0);
}
