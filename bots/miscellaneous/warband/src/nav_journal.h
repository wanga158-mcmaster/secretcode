#pragma once
#include <array>
#include <cstdio>
#include "config.h"
#include "protocol.h"

namespace wb {
struct NavJournalCall { int kind=0, route=0, group=-1, x=-1, y=-1, within=0; };
struct NavJournalFrame {
    bool active=false, overflow=false, moved=false;
    int round=0, actor=-1, start_x=0, start_y=0, from_x=0, from_y=0, to_x=0, to_y=0;
    int branch=0, route=0, route_history=0, distance=-1, count=0;
    PackMsg read_pack{}, dispatch_pack{}, follow_pack{};
    std::array<NavJournalCall,6> calls{};
    void begin(int r,int a,const PackMsg& p,int x,int y) {
        if (a!=1 || (r!=350 && r!=351)) { active=false; count=0; return; }
        *this=NavJournalFrame{};
        active=true;
        round=r; actor=a; read_pack=p; start_x=x; start_y=y;
    }
    void call(int kind,int group,int x,int y,int within) {
        if (!active) return;
        if (count>=static_cast<int>(calls.size())) { overflow=true; return; }
        calls[static_cast<unsigned>(count++)]={kind,route,group,x,y,within};
    }
    void step(int x,int y,int nx,int ny) {
        if (!active) return;
        moved=true; from_x=x; from_y=y; to_x=nx; to_y=ny;
    }
    void route_to(int r,int d,const PackMsg& p) {
        if (!active) return;
        route=r; route_history=route_history*10+r; distance=d; follow_pack=p;
    }
};
inline NavJournalFrame& nav_journal_frame() { static NavJournalFrame frame; return frame; }
inline void nav_journal_begin(int r,int a,const PackMsg& p,int x,int y) {
    if constexpr(cfg::nav_journal) nav_journal_frame().begin(r,a,p,x,y);
}
inline void nav_journal_dispatch(const PackMsg& p) {
    if constexpr(cfg::nav_journal) { auto& f=nav_journal_frame(); if(f.active) f.dispatch_pack=p; }
}
inline void nav_journal_route(int route,int distance,const PackMsg& p) {
    if constexpr(cfg::nav_journal) { auto& f=nav_journal_frame(); if(f.active) {
        f.route_to(route,distance,p);
    }}
}
inline void nav_journal_call(int kind,int group,int x,int y,int within) {
    if constexpr(cfg::nav_journal) nav_journal_frame().call(kind,group,x,y,within);
}
inline void nav_journal_step(int x,int y,int nx,int ny) {
    if constexpr(cfg::nav_journal) nav_journal_frame().step(x,y,nx,ny);
}
inline void nav_journal_branch(int branch) {
    if constexpr(cfg::nav_journal) { auto& f=nav_journal_frame(); if(f.active) f.branch=branch; }
}
inline void nav_journal_flush(long fuel_left) {
    if constexpr(cfg::nav_journal) {
        auto& f=nav_journal_frame();
        if(!f.active || fuel_left<40000) return;
        // Both matched arms retain this code and recording frame. Only this data differs.
        static volatile int emit_data=17+cfg::nav_journal_emit;
        if(emit_data!=18) return;
        const auto& r=f.read_pack; const auto& d=f.dispatch_pack; const auto& p=f.follow_pack;
        const auto& c=f.calls;
        std::printf("wbnav r%d a%d read%d,%d,%d,%d,%d dispatch%d,%d,%d,%d,%d follow%d,%d,%d,%d,%d start%d,%d step%d,%d,%d,%d moved%d branch%d routes%d dist%d n%d over%d fuel%ld c0=%d,%d,%d,%d,%d,%d c1=%d,%d,%d,%d,%d,%d c2=%d,%d,%d,%d,%d,%d c3=%d,%d,%d,%d,%d,%d c4=%d,%d,%d,%d,%d,%d c5=%d,%d,%d,%d,%d,%d\n",
          f.round,f.actor,r.objective,r.ax,r.ay,static_cast<int>(r.mode),r.stamp,
          d.objective,d.ax,d.ay,static_cast<int>(d.mode),d.stamp,
          p.objective,p.ax,p.ay,static_cast<int>(p.mode),p.stamp,
          f.start_x,f.start_y,f.from_x,f.from_y,f.to_x,f.to_y,f.moved?1:0,f.branch,f.route_history,f.distance,f.count,f.overflow?1:0,fuel_left,
          c[0].kind,c[0].route,c[0].group,c[0].x,c[0].y,c[0].within,
          c[1].kind,c[1].route,c[1].group,c[1].x,c[1].y,c[1].within,
          c[2].kind,c[2].route,c[2].group,c[2].x,c[2].y,c[2].within,
          c[3].kind,c[3].route,c[3].group,c[3].x,c[3].y,c[3].within,
          c[4].kind,c[4].route,c[4].group,c[4].x,c[4].y,c[4].within,
          c[5].kind,c[5].route,c[5].group,c[5].x,c[5].y,c[5].within);
    }
}
} // namespace wb
