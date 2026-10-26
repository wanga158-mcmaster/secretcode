#include "service_price.h"
#include <cstdio>

namespace {
using namespace mb::production::service;
int failures{};
void check(bool value,const char* name) {
    if(!value){std::printf("FAIL %s\n",name);++failures;}
}
bool close(double a,double b){return std::abs(a-b)<0.00001;}
}
int main() {
    using namespace mb::production::service;
    const std::array wolves{Member{0,{0,0},1350,20},Member{1,{1,0},1350,20},
                           Member{2,{0,1},1350,20},Member{3,{1,1},1350,20}};
    const auto packed=price(wolves,{40,1});
    check(packed.status==Status::Ready&&packed.basic_attacks==34&&packed.chunks==1&&
          close(packed.first_basic_removed_hp,160)&&close(packed.total_removed_hp,5400)&&
          close(packed.projected_incoming,2640)&&
          std::all_of(packed.residual.begin(),packed.residual.begin()+4,[](const Member& m){return m.hp==0;}),
          "four adjacent wolves take thirty-four simultaneous basics and thirty-three live volleys");
    const auto solo=price(std::span(wolves).first(1),{40,0});
    const std::array elk{Member{4,{0,0},2400,60}};
    const auto single_elk=price(elk,{40,0});
    check(solo.basic_attacks==34&&close(solo.projected_incoming,660)&&
          single_elk.basic_attacks==60&&close(single_elk.projected_incoming,3540),
          "solo wolf and elk retain individual basic service and no pre-wake volley");
    const auto dispersed=price(wolves,{40,0});
    check(dispersed.basic_attacks==136&&dispersed.chunks==4&&close(dispersed.total_removed_hp,5400),
          "removing splash restores each source's separate residual service");
    const std::array mixed{Member{0,{0,0},1350,20},Member{1,{1,0},2400,60}};
    const auto neighbor_death=price(mixed,{40,1});
    check(neighbor_death.status==Status::Ready&&neighbor_death.basic_attacks==60&&neighbor_death.chunks==2&&
          close(neighbor_death.total_removed_hp,3750)&&close(neighbor_death.projected_incoming,4200),
          "a splash neighbor dies before the higher-priority primary and triggers reranking");
    const auto concentration=price(mixed,{40,0});
    check(concentration.basic_attacks==94&&close(concentration.projected_incoming,5400),
          "strength over residual HP prioritizes elk without pooling independent damage");
    const auto opened=price(wolves,{40,1},OpeningArea{true,{0,0},1000,1});
    check(opened.status==Status::Ready&&opened.opening_used&&opened.basic_attacks==9&&
          close(opened.opening_removed_hp,4000)&&close(opened.first_basic_removed_hp,160)&&
          close(opened.projected_incoming,640)&&close(opened.total_removed_hp,5400),
          "ready opening area and first basic share one model turn without an invented volley");
    const auto opened_dead=price(wolves,{40,1},OpeningArea{true,{0,0},2000,1});
    check(opened_dead.status==Status::Ready&&opened_dead.basic_attacks==0&&opened_dead.projected_incoming==0,
          "an opening area that removes every source needs no basic or later camp volley");
    auto uncertain=wolves;uncertain[0].hp_exact=false;
    const auto estimate=price(uncertain,{40,1});
    check(estimate.status==Status::Ready&&!estimate.exact_residual_inputs&&estimate.spatial_service_unmodeled,
          "estimated residual HP remains a usable conditional projection rather than observed generation");
    std::array<Member,32> maximum;
    for(int i=0;i<32;++i)maximum[static_cast<std::size_t>(i)]={i,{i,0},40,1};
    const auto full=price(maximum,{40,0});
    check(full.status==Status::Ready&&full.count==32&&full.chunks==32&&full.basic_attacks==32&&
          close(full.projected_incoming,496),"all thirty-two members are serviced without an eight-member cap");
    std::array<Member,33> excess;
    check(price(excess,{40,0}).status==Status::Incomplete,"capacity overflow remains incomplete rather than truncated");
    auto malformed=wolves;malformed[1].id=0;
    check(price(wolves,{0,1}).status==Status::Invalid&&price(malformed,{40,1}).status==Status::Invalid,
          "zero basic damage and duplicate physical source identity cannot form a price");
    std::printf("service price checks: %s\n",failures==0?"passed":"failed");
    return failures?1:0;
}
