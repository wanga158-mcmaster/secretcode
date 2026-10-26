#include "service_task.h"
#include <array>
#include <cassert>
#include <cstdio>

// Actual Packs native group2/g_506 member geometry, in row-major order.
// The caller owns lifecycle evidence. This SDK-free fixture supplies its reader
// contract; proposal_lifecycle.cpp separately integrates the real Lifecycle API.
namespace {
using namespace mosaic::service_task;
constexpr std::array<std::size_t,9> indices{0,1,2,3,4,5,6,7,8};
constexpr std::array<Point,9> positions{{
    {10,16},{11,16},{12,16},{10,17},{11,17},{12,17},{10,18},{11,18},{12,18}
}};
struct Fixture {
    std::array<bool,9> proposed{true,true,true,true,true,true,true,true,true};
    Member read(std::size_t index) const {return {positions[index],120.0f,proposed[index]};}
    void local_four_dead() {for(auto i:{0u,1u,3u,4u})proposed[i]=false;}
};
template<class Reader> std::size_t goals(bool reward,Reader read) {
    return std::count_if(indices.begin(),indices.end(),[&](auto i){return goal_member(i,reward,read);});
}
}

int main() {
    int checks=0;
    Fixture fixture;
    auto reader=[&](std::size_t i){return fixture.read(i);};
    assert(goals(false,reader)==9);++checks;
    assert(in_frontier(indices,{9,15},1,reader));++checks;
    assert(nominate_basic(indices,{9,15},1,40.0f,reader)==std::optional<std::size_t>{0});++checks;

    // The actual local splash kills these four members, not the entire group.
    // Every remaining member is three tiles from the old body.
    fixture.local_four_dead();
    assert(goals(false,reader)==5);++checks;
    for(auto i:indices)if(reader(i).proposed){assert(distance({9,15},reader(i).pos)==3);++checks;}
    assert(!nominate_basic(indices,{9,15},1,40.0f,reader));++checks;
    assert(!in_frontier(indices,{9,15},1,reader));++checks;
    assert(!in_frontier(indices,{10,16},1,reader));++checks;
    assert(in_frontier(indices,{11,17},1,reader));++checks;
    assert(nominate_basic(indices,{11,17},1,40.0f,reader)==std::optional<std::size_t>{2});++checks;

    // Capture still uses physical spawn tiles. Negative work evidence cannot
    // delete the old tiles or remove the actor's possible reward goal.
    assert(!goal_member(std::size_t{0},false,reader));++checks;
    assert(goal_member(std::size_t{0},true,reader));++checks;
    assert(goals(true,reader)==9);++checks;
    for(auto i:indices)fixture.proposed[i]=false;
    assert(goals(false,reader)==0);++checks;
    assert(!in_frontier(indices,{11,17},1,reader));++checks;
    assert(!nominate_basic(indices,{11,17},1,40.0f,reader));++checks;
    assert(goals(true,reader)==9);++checks;

    int reads=0;
    auto unused=[&](std::size_t i){++reads;return fixture.read(i);};
    assert(goals(true,unused)==9&&reads==0);++checks;
    std::printf("service task checks: %d passed; SDK calls: 0\n",checks);
}
