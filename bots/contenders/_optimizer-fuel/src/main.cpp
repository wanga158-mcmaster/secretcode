#include <cambc.h>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include "kernel.hpp"

struct RefPoint { int x,y; };
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static std::uint32_t reference(const RefPoint* points,int n,int qx,int qy) {
    int best=10000,encoded=0;
    for(int i=0;i<n;++i) { const int dx=points[i].x-qx,dy=points[i].y-qy;
        const int ax=dx<0?-dx:dx,ay=dy<0?-dy:dy,d=ax>ay?ax:ay;
        if(d<best) { best=d; encoded=i+1; }
    }
    return (static_cast<std::uint32_t>(best)<<16)|static_cast<std::uint32_t>(encoded);
}
static std::uint32_t next_random(std::uint32_t& x) { x^=x<<13; x^=x>>17; x^=x<<5; return x; }

static fuel_kernel::Data data;
static RefPoint points[256];
static volatile std::uint32_t sink;
static int sample=0;
static bool selected=false,initialized=false;
static constexpr int counts[]={1,1,1,1,1,1,1,1,1,1,1,1,7,7,7,7,7,7,7,7,7,7,7,7,31,31,31,31,31,31,31,31,31,31,31,31,127,127,127,127,127,127,127,127,127,127,127,127,256,256,256,256,256,256,256,256,256,256,256,256};
static constexpr int patterns[]={0,0,1,1,2,2,3,3,4,4,5,5,0,0,1,1,2,2,3,3,4,4,5,5,0,0,1,1,2,2,3,3,4,4,5,5,0,0,1,1,2,2,3,3,4,4,5,5,0,0,1,1,2,2,3,3,4,4,5,5};
static constexpr int total=60;
static void draft_turn() {
    const cambc::champion_draft picks[]={cambc::champion_draft::ryth,cambc::champion_draft::marcella,cambc::champion_draft::varg,cambc::champion_draft::tavi,cambc::champion_draft::stellar,cambc::champion_draft::vesper_vale};
    const auto mine=cambc::draft::current_draft(cambc::team::ally);
    for(auto p:picks) if(std::find(mine.begin(),mine.end(),p)==mine.end()) { cambc::draft::pick(p); return; }
}
static void play_turn() {
    auto& me=cambc::self();
    if(!initialized) {
        const auto roster=cambc::champions(cambc::team::ally);
        auto first=static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(me));
        for(const auto& c:roster) first=std::min(first,static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(c)));
        selected=first==static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(me)); initialized=true;
    }
    if(!selected||!me.is_alive()||cambc::round_number()<3||sample>=total) return;
    if(cambc::fuel_remaining()<85000) { std::printf("fuel optimizer error insufficient fresh reserve\n"); return; }
    const auto setup_start=cambc::fuel_remaining();
    const int n=counts[sample],pattern=patterns[sample];
    std::uint32_t random=211u^(static_cast<std::uint32_t>(sample+1)*2654435761u)^cambc::map::width();
    const int qx=static_cast<int>(next_random(random)%100),qy=static_cast<int>(next_random(random)%100);
    for(int i=0;i<n;++i) {
        int x=static_cast<int>(next_random(random)%100),y=static_cast<int>(next_random(random)%100);
        if(pattern==1) { x=std::clamp(qx+static_cast<int>(next_random(random)%9)-4,0,99); y=std::clamp(qy+static_cast<int>(next_random(random)%9)-4,0,99); }
        if(pattern==2) { x=50;y=50; }
        if((pattern==3&&i==0)||(pattern==4&&i==n-1)) { x=qx;y=qy; }
        if(pattern==5) { x=qx;y=i%100; }
        points[i]={x,y}; data.set(i,x,y);
    }
    const auto setup=setup_start-cambc::fuel_remaining();
    std::uint64_t raw=0,ref=0; std::uint32_t actual=0,expected=0;
    if(sample%2==0) {
        auto start=cambc::fuel_remaining(); expected=reference(points,n,qx,qy); sink=expected; ref=start-cambc::fuel_remaining();
        start=cambc::fuel_remaining(); actual=fuel_kernel::nearest(data,n,qx,qy); sink=actual; raw=start-cambc::fuel_remaining();
    } else {
        auto start=cambc::fuel_remaining(); actual=fuel_kernel::nearest(data,n,qx,qy); sink=actual; raw=start-cambc::fuel_remaining();
        start=cambc::fuel_remaining(); expected=reference(points,n,qx,qy); sink=expected; ref=start-cambc::fuel_remaining();
    }
    if(actual!=expected) std::printf("fuel optimizer error native checksum mismatch sample=%d\n",sample);
    else std::printf("FUEL_KERNEL sample=%d n=%d pattern=%d raw=%llu reference=%llu checksum=%u setup=%llu\n",sample,n,pattern,static_cast<unsigned long long>(raw),static_cast<unsigned long long>(ref),actual,static_cast<unsigned long long>(setup));
    ++sample;
}
int main() { while(true) {
    try { if(cambc::is_draft()) draft_turn(); else play_turn(); }
    catch(const std::exception& e) { std::printf("fuel optimizer error: %s\n",e.what()); }
    catch(...) { std::printf("fuel optimizer error: unknown\n"); }
    cambc::end_turn();
} }
