#include "native_service_exit_witness.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace N=mosaic::native_service;
using Status=N::ExitWitnessStatus;
static unsigned checks=0,groups=0;
static void check(bool value,const char* expression,int line){
    ++checks;if(!value){std::fprintf(stderr,"exit witness check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)
static constinit N::ExitWitness constant_witness{};
static N::ExitWitnessTag tag(N::Point origin={2,3}){return {{10,2,1},origin,7,11};}
template<std::size_t Cells> static void finish(N::Navigation<Cells>& nav){
    unsigned calls=0;while(nav.status()==N::NavigationStatus::preparing){CHECK(++calls<4096);CHECK(nav.step(8).progressed);}
}
template<std::size_t Cells> static void descends(const N::Navigation<Cells>& nav,const N::ExitWitness& witness){
    auto from=witness.tag.origin;std::array<N::Point,33> seen{};unsigned count=1;seen[0]=from;
    for(unsigned n=0;n<witness.steps;++n){
        const auto to=witness.path[n];CHECK(std::max(std::abs(from.x-to.x),std::abs(from.y-to.y))==1);
        CHECK(nav.distance(from)&&nav.distance(to)&&*nav.distance(to)<*nav.distance(from));
        for(unsigned k=0;k<count;++k)CHECK(seen[k]!=to);
        seen[count++]=to;from=to;
    }
}

static void warm_field_minimum_potential_and_phase_sums(){
    ++groups;N::Navigation<64> nav;
    std::array<std::uint8_t,25> board{};board.fill(1);
    const std::array roots{N::Point{2,0}};CHECK(nav.begin(5,5,board,roots,11));finish(nav);
    std::array<std::uint64_t,25> amounts{};amounts.fill(9);
    amounts[2]=0;amounts[3*5+2]=10;amounts[2*5+1]=7;amounts[2*5+2]=3;amounts[2*5+3]=3;
    amounts[1*5+1]=1;amounts[1*5+2]=1;
    const auto w=N::build_exit_witness(nav,tag(),N::RawPotentialSpan{std::span<const std::uint64_t>(amounts),5,5});
    CHECK(w.complete()&&w.prices_complete()&&w.status==Status::path&&w.steps==3);
    CHECK(w.path[0]==N::Point{2,2}&&w.path[1]==N::Point{1,1}&&w.path[2]==N::Point{2,0});
    CHECK(w.next_point()==N::Point{2,2});
    CHECK(w.melee_phases==4&&w.ranged_phases==14&&w.max_origin_phase==10);
    CHECK(w.melee_with_refusal==14&&w.ranged_with_refusal==24&&!w.saturated);
    CHECK(w.source_samples<=1+8*w.steps);descends(nav,w);
    const auto repeat=N::build_exit_witness(nav,tag(),N::RawPotentialSpan{std::span<const std::uint64_t>(amounts),5,5});
    CHECK(repeat.steps==w.steps&&repeat.path==w.path&&repeat.melee_phases==w.melee_phases);
}

static void quiet_before_home_and_quiet_origin(){
    ++groups;N::Navigation<64> nav;
    std::array<std::uint8_t,7> board{};board.fill(1);
    const std::array roots{N::Point{0,0}};CHECK(nav.begin(7,1,board,roots,11));finish(nav);
    std::array<std::uint32_t,7> amounts{9,9,0,4,3,8,8};
    auto source=N::RawPotentialSpan{std::span<const std::uint32_t>(amounts),7,1};
    auto w=N::build_exit_witness(nav,tag({5,0}),source);
    CHECK(w.complete()&&w.steps==3&&w.path[2]==N::Point{2,0});
    CHECK(nav.distance(w.path[2])==2); // Quiet, before the positive-potential home root.
    CHECK(w.melee_phases==7&&w.ranged_phases==15&&w.max_origin_phase==8);descends(nav,w);
    w=N::build_exit_witness(nav,tag({2,0}),source,0);
    CHECK(w.status==Status::quiet&&w.complete()&&w.steps==0&&!w.next_point());
    CHECK(!w.melee_phases&&!w.ranged_phases&&!w.max_origin_phase&&!w.melee_with_refusal&&!w.ranged_with_refusal);
}

static void available_origin_while_global_preparing(){
    ++groups;N::Navigation<64> nav;
    std::array<std::uint8_t,9> board{};board.fill(1);
    const std::array roots{N::Point{0,0}};CHECK(nav.begin(3,3,board,roots,11));
    CHECK(nav.step(10).units==10); // Nine stamp cells, then the single root.
    CHECK(nav.status()==N::NavigationStatus::preparing&&nav.available({0,0}));
    auto positive=[](N::Point){return N::RawCampPotential{5,true};};
    auto w=N::build_exit_witness(nav,tag({0,0}),positive);
    CHECK(w.status==Status::unknown&&!w.complete()); // Positive root has no lower predecessor.
    CHECK(nav.step(1).units==1&&nav.available({1,1}));
    CHECK(nav.status()==N::NavigationStatus::preparing);
    auto source=[](N::Point p){return N::RawCampPotential{p==N::Point{0,0}?0u:5u,true};};
    w=N::build_exit_witness(nav,tag({1,1}),source);
    CHECK(w.complete()&&w.steps==1&&w.path[0]==N::Point{0,0});
    CHECK(w.melee_phases==0&&w.ranged_phases==5&&w.max_origin_phase==5);
    CHECK(nav.status()==N::NavigationStatus::preparing);descends(nav,w);
    finish(nav);w=N::build_exit_witness(nav,tag({0,0}),positive);
    CHECK(w.status==Status::unsupported&&!w.complete());
}

static void unknown_invalid_source_and_blocked_fields(){
    ++groups;N::Navigation<64> nav;
    std::array<std::uint8_t,9> board{};board.fill(1);const std::array roots{N::Point{0,0}};
    auto source=[](N::Point){return N::RawCampPotential{1,true};};
    CHECK(!constant_witness.complete()&&!constant_witness.next_point());
    CHECK(N::build_exit_witness(nav,tag({2,2}),source).status==Status::unknown);
    CHECK(nav.begin(3,3,board,roots,11));CHECK(nav.step(8).units==8);
    CHECK(N::build_exit_witness(nav,tag({2,2}),source).status==Status::unknown);finish(nav);
    unsigned samples=0;auto counted=[&](N::Point){++samples;return N::RawCampPotential{1,true};};
    CHECK(N::build_exit_witness(nav,tag({2,2}),counted,32,false).status==Status::unknown&&samples==0);
    auto invalid=[](N::Point){return N::RawCampPotential{0,false};};
    CHECK(N::build_exit_witness(nav,tag({2,2}),invalid).status==Status::invalid);
    auto invalid_neighbor=[](N::Point p){return N::RawCampPotential{1,p!=N::Point{1,1}};};
    CHECK(N::build_exit_witness(nav,tag({2,2}),invalid_neighbor).status==Status::invalid);
    std::array<std::uint64_t,8> short_source{};
    CHECK(N::build_exit_witness(nav,tag({2,2}),N::RawPotentialSpan{std::span<const std::uint64_t>(short_source),3,3}).status==Status::invalid);
    auto bad=tag({2,2});bad.epoch.round=-1;CHECK(N::build_exit_witness(nav,bad,source).status==Status::invalid);
    bad=tag({2,2});bad.field_generation=0;CHECK(N::build_exit_witness(nav,bad,source).status==Status::invalid);
    bad=tag({2,2});bad.semantic_intent=0;CHECK(N::build_exit_witness(nav,bad,source).status==Status::invalid);
    nav.retire();board[0]=0;CHECK(nav.begin(3,3,board,roots,12));finish(nav);
    CHECK(nav.status()==N::NavigationStatus::unreachable);
    CHECK(N::build_exit_witness(nav,tag({2,2}),source).status==Status::unsupported);
}

static void cap_and_saturated_prices_preserve_geometry(){
    ++groups;N::Navigation<64> nav;
    std::array<std::uint8_t,40> board{};board.fill(1);const std::array roots{N::Point{0,0}};
    CHECK(nav.begin(40,1,board,roots,11));finish(nav);
    auto source=[](N::Point p){return N::RawCampPotential{p.x?1u:0u,true};};
    auto w=N::build_exit_witness(nav,tag({32,0}),source);
    CHECK(w.complete()&&w.steps==32&&w.melee_phases==31&&w.ranged_phases==32);descends(nav,w);
    w=N::build_exit_witness(nav,tag({33,0}),source);
    CHECK(w.status==Status::unsupported&&w.steps==32&&!w.prices_complete());descends(nav,w);
    CHECK(N::build_exit_witness(nav,tag({32,0}),source,31).status==Status::unsupported);
    CHECK(N::build_exit_witness(nav,tag({1,0}),source,0).status==Status::unsupported);
    CHECK(N::build_exit_witness(nav,tag({1,0}),source,33).status==Status::unsupported);
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    auto huge=[&](N::Point p){return N::RawCampPotential{p.x?maximum:0,true};};
    w=N::build_exit_witness(nav,tag({3,0}),huge);
    CHECK(w.complete()&&w.steps==3&&w.saturated&&!w.prices_complete());
    CHECK(w.melee_phases==maximum&&w.ranged_phases==maximum&&w.max_origin_phase==maximum);
    CHECK(w.melee_with_refusal==maximum&&w.ranged_with_refusal==maximum);descends(nav,w);
}

static void exact_dependency_tags_reject_old_witness(){
    ++groups;N::Navigation<64> nav;
    std::array<std::uint8_t,9> board{};board.fill(1);const std::array roots{N::Point{0,0}};
    CHECK(nav.begin(3,3,board,roots,11));finish(nav);
    auto source=[](N::Point p){return N::RawCampPotential{p==N::Point{0,0}?0u:1u,true};};
    const auto original=tag({2,2});const auto old=N::build_exit_witness(nav,original,source);
    CHECK(old.complete()&&old.matches(original)&&old.path[0]==N::Point{1,1});
    auto changed=original;changed.epoch.attempt++;CHECK(!old.matches(changed));
    changed=original;changed.epoch.revision++;CHECK(!old.matches(changed));
    changed=original;changed.epoch.round++;CHECK(!old.matches(changed));
    changed=original;changed.origin={2,1};CHECK(!old.matches(changed));
    changed=original;changed.field_generation++;CHECK(!old.matches(changed));
    changed=original;changed.semantic_intent++;CHECK(!old.matches(changed));
    nav.retire();board[4]=0;CHECK(nav.begin(3,3,board,roots,12));finish(nav);
    changed=original;changed.field_generation++;changed.semantic_intent++;
    const auto fresh=N::build_exit_witness(nav,changed,source);
    CHECK(!old.matches(changed)&&fresh.complete()&&fresh.matches(changed));
    CHECK(fresh.path[0]!=N::Point{1,1});descends(nav,fresh);
}

static void retired_small_route_has_local_edit_dependencies(){
    ++groups;N::Navigation<64> nav;std::array<std::uint8_t,9> board{};board.fill(1);
    const std::array roots{N::Point{0,0}};CHECK(nav.begin(3,3,board,roots,11));finish(nav);
    const auto witness=N::build_exit_witness(nav,tag({2,2}),[](N::Point p){return N::RawCampPotential{p.x||p.y?1u:0u,true};});
    CHECK(witness.complete()&&witness.steps==2);nav.retire();
    for(unsigned i=0;i<witness.steps;++i){
        CHECK(N::retained_exit_survives_camp_edit(witness,witness.path[i],false));
        CHECK(!N::retained_exit_survives_camp_edit(witness,witness.path[i],true));
    }
    CHECK(N::retained_exit_survives_camp_edit(witness,{2,0},true));
    CHECK(N::retained_exit_survives_camp_edit(witness,witness.tag.origin,true));
    CHECK(!N::retained_exit_survives_camp_edit({}, {2,0},false));
    auto saturated=witness;saturated.saturated=true;
    CHECK(!N::retained_exit_survives_camp_edit(saturated,{2,0},false));
    CHECK(nav.status()==N::NavigationStatus::idle); // No edited borrowed field is reused.
}

int main(){
    warm_field_minimum_potential_and_phase_sums();quiet_before_home_and_quiet_origin();
    available_origin_while_global_preparing();unknown_invalid_source_and_blocked_fields();
    cap_and_saturated_prices_preserve_geometry();exact_dependency_tags_reject_old_witness();
    retired_small_route_has_local_edit_dependencies();
    std::printf("PASS native_service_exit_witness: %u checks, %u groups\n",checks,groups);
}
