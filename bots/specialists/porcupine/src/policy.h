#pragma once
#include <algorithm>
#include <cmath>
namespace fundamentals {
constexpr bool should_retreat(float health, float maximum, float incoming) {
    return maximum>0 && (health<=maximum*0.35f || health<=incoming*2.0f+60.0f);
}
constexpr bool should_bank(unsigned gems,unsigned round,unsigned max_rounds,int altar_distance) {
    const unsigned late=max_rounds>100?max_rounds-100:0;
    return gems>0 && (gems>=24 || altar_distance<=4 || round>=late);
}
constexpr bool affordable_camp(float health,float maximum,float monster_health,float incoming,float dps) {
    return incoming<=0 || (dps>0 && health-incoming*(monster_health/dps+2.0f)>=maximum*0.25f);
}
}
namespace porcupine {
inline constexpr float epsilon=0.0001f;
struct Estimate { unsigned triggers=0; float reflected=0,incoming=0,remaining_health=0; bool competing=false,full=false; };
constexpr float expected_loss(const Estimate& e,float strength,float focus) {
    const float denominator=std::max(1.0f,strength+0.12f*focus+e.reflected);
    return e.incoming*(2.0f+e.remaining_health/denominator);
}
constexpr bool qualifies(const Estimate& e,float health,float maximum,float strength,float focus) {
    return e.full && !e.competing && e.triggers>=2 && e.reflected/std::max(epsilon,e.incoming)>=0.5f
        && health-expected_loss(e,strength,focus)>=maximum*0.25f;
}
constexpr bool better_group(const Estimate& a,float score_a,unsigned key_a,const Estimate& b,float score_b,unsigned key_b) {
    const float ratio_a=a.reflected/std::max(epsilon,a.incoming),ratio_b=b.reflected/std::max(epsilon,b.incoming);
    return ratio_a>ratio_b || (ratio_a==ratio_b && (score_a>score_b || (score_a==score_b && key_a<key_b)));
}
constexpr bool better_tile(const Estimate& a,unsigned tile_a,const Estimate& b,unsigned tile_b) {
    return a.triggers>b.triggers || (a.triggers==b.triggers && (a.incoming<b.incoming || (a.incoming==b.incoming && tile_a<tile_b)));
}
inline bool close(float actual,float expected) { return std::abs(actual-expected)<=0.02f; }
}
