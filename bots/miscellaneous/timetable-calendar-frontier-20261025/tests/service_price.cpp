#include "service_price.h"
#include <cstdio>

namespace {
using namespace mb::production::service;
int failures{},checks{};
void check(bool value,const char* name) {
    ++checks;
    if(!value){std::printf("FAIL %s\n",name);++failures;}
}
bool close(double a,double b){return std::abs(a-b)<0.00001;}
bool same(const Result& a,const Result& b) {
    if(a.status!=b.status||a.basic_attacks!=b.basic_attacks||a.total_removed_hp!=b.total_removed_hp||
       a.first_basic_removed_hp!=b.first_basic_removed_hp||a.opening_removed_hp!=b.opening_removed_hp||
       a.projected_incoming!=b.projected_incoming||a.exact_residual_inputs!=b.exact_residual_inputs||
       a.spatial_service_unmodeled!=b.spatial_service_unmodeled||a.opening_used!=b.opening_used||
       a.count!=b.count||a.chunks!=b.chunks)return false;
    for(std::size_t i=0;i<capacity;++i) {
        const auto& x=a.residual[i];const auto& y=b.residual[i];
        if(x.id!=y.id||x.pos.x!=y.pos.x||x.pos.y!=y.pos.y||x.hp!=y.hp||
           x.strength!=y.strength||x.hp_exact!=y.hp_exact)return false;
    }
    return true;
}
void sliced(std::span<const Member> members,Body body,std::optional<OpeningArea> opening,std::size_t units) {
    const auto batch=price(members,body,opening);
    Pricer pricer(members,body,opening);
    const auto initial=pricer.result();
    check(pricer.advance(0)==initial.status&&pricer.last_work()==0&&pricer.completed_units()==0,
        "zero work cannot publish a price or consume an input");
    std::size_t calls=0;
    while(pricer.status()==Status::NeedCompute&&calls++<=Pricer::max_work_units) {
        const auto before=pricer.completed_units();
        pricer.advance(units);
        check(pricer.last_work()<=units&&pricer.last_work()>0&&
              pricer.completed_units()==before+pricer.last_work(),
            "every sliced advance respects its units and has monotonic exact work accounting");
        check(pricer.result().status==pricer.status(),"partial result cannot publish Ready before the cursor");
    }
    check(pricer.status()!=Status::NeedCompute&&pricer.completed_units()<=Pricer::max_work_units&&
          same(pricer.result(),batch),"sliced result exactly matches every batch field and residual");
    const auto done=pricer.completed_units();
    pricer.advance(std::numeric_limits<std::size_t>::max());
    check(pricer.completed_units()==done&&pricer.last_work()==0&&same(pricer.result(),batch),
        "completed cursors are stable and consume no later work");
}
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

    for(const std::size_t units:{1,2,16}) {
        sliced(wolves,{40,1},std::nullopt,units);
        sliced(wolves,{40,0},std::nullopt,units);
        sliced(mixed,{40,1},std::nullopt,units);
        sliced(uncertain,{40,1},OpeningArea{true,{0,0},1000,1},units);
        sliced(wolves,{40,1},OpeningArea{true,{0,0},2000,1},units);
        sliced(maximum,{40,0},std::nullopt,units);
        sliced({}, {40,0},std::nullopt,units);
        sliced(malformed,{40,1},std::nullopt,units);
        sliced(excess,{40,0},std::nullopt,units);
        sliced(wolves,{0,0},std::nullopt,units);
        sliced(wolves,{40,1},OpeningArea{true,{0,0},-1,1},units);
        auto overflow=wolves;
        overflow[0].hp=overflow[1].hp=std::numeric_limits<double>::max();
        sliced(overflow,{40,0},std::nullopt,units);
    }
    Pricer single(std::span(wolves).first(1),{40,0});
    check(single.status()==Status::NeedCompute&&single.result().basic_attacks==0,
        "construction copies owned input without secretly validating or pricing");
    check(single.advance(1)==Status::NeedCompute&&single.result().residual[0].hp==1350,
        "one unit validates one member and remains incomplete");
    check(single.advance(1)==Status::NeedCompute&&single.result().basic_attacks==0,
        "opening setup is an explicit bounded phase");
    check(single.advance(1)==Status::NeedCompute&&single.result().basic_attacks==34&&single.result().residual[0].hp==0,
        "a completed death chunk does not publish Ready before the final completion check");
    check(single.advance(1)==Status::Ready&&same(single.result(),solo)&&single.completed_units()==4,
        "one-member price publishes only after bounded completion");
    auto changed=wolves;
    Pricer owned(changed,{40,1});changed[0].hp=1;changed[1].id=0;
    owned.advance(Pricer::max_work_units);
    check(same(owned.result(),packed),"later caller mutations cannot alter copied residual inputs");
    const std::array huge{Member{0,{0,0},1000000000000.0,1}};
    Pricer large(huge,{1,0});large.advance(Pricer::max_work_units);
    check(large.status()==Status::Ready&&large.result().basic_attacks==1000000000000ULL&&
          large.result().chunks==1&&large.completed_units()==4,
        "work is bounded by member death chunks rather than projected basic count");
    const auto ignored=price(wolves,{40,1},OpeningArea{false,{0,0},-1,-1});
    check(same(ignored,packed),"an unavailable opening retains the original ignored-payload semantics");
    Pricer rejected(excess,{40,0});
    check(rejected.status()==Status::Incomplete&&rejected.completed_units()==0&&
          rejected.advance(1)==Status::Incomplete&&rejected.last_work()==0,
        "capacity failure is terminal Incomplete rather than a pending cursor or truncated Ready");
    std::printf("service price checks: %s (%d)\n",failures==0?"passed":"failed",checks);
    return failures?1:0;
}
