// Pure numeric selection oracle; walls, occupancy and action legality are
// caller filters. This fixture does not make a gameplay or fuel claim.
#include "../common/src/escape_choice.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
using p8::native::EscapeInputs;
using p8::native::EscapeNeighbor;
using p8::native::choose_escape;
int checks=0;
void check(bool value,const char* message) {
    ++checks;
    if(!value) {std::fprintf(stderr,"FAIL escape choice: %s\n",message);std::exit(1);}
}
EscapeInputs input(double damage=20,int clearance=-1,double health=100) {
    EscapeInputs in;in.origin_damage=damage;in.origin_clearance=clearance;in.health=health;return in;
}
void wolf_path() {
    // Observed wolf (10,5), range3, strength20. The caller has filtered the
    // legal y/x neighbors at each step; the selector receives numeric bounds.
    auto wolf=[](int x,int y) {
        const int distance=std::max(std::abs(x-10),std::abs(y-5));
        return EscapeNeighbor{distance<=3?20.:0.,distance-3};
    };
    const auto origin=wolf(8,3);check(origin.damage==20&&origin.clearance==-1,"observed origin threat");
    auto first=input(origin.damage,origin.clearance);first.count=3;
    first.neighbors[0]=wolf(7,2);first.neighbors[1]=wolf(8,2);first.neighbors[2]=wolf(9,2);
    check(first.neighbors[0].damage==20&&first.neighbors[0].clearance==0,"first step stays in wolf range");
    check(choose_escape(first)==0,"flat-risk outward step selected with first y/x tie");
    auto second=input(first.neighbors[0].damage,first.neighbors[0].clearance);second.count=3;
    second.neighbors[0]=wolf(6,1);second.neighbors[1]=wolf(7,1);second.neighbors[2]=wolf(8,1);
    check(second.neighbors[0].damage==0,"second outward step leaves observed wolf range");
    check(choose_escape(second)==0,"strictly lower-risk second step selected");
}
void priorities() {
    auto in=input();in.count=4;in.neighbors[0]={20,5};in.neighbors[1]={10,-5};in.neighbors[2]={5,-9};in.neighbors[3]={5,99};
    check(choose_escape(in)==2,"lowest damage outranks clearance and preserves first tie");
    std::swap(in.neighbors[0],in.neighbors[2]);
    check(choose_escape(in)==0,"lower damage wins even before flat alternative");
    in=input();in.count=3;in.neighbors[0]={20,0};in.neighbors[1]={20,2};in.neighbors[2]={20,2};
    check(choose_escape(in)==1,"flat branch maximizes clearance with first tie");
    in.neighbors[2]={20,3};check(choose_escape(in)==2,"strictly greater flat clearance replaces winner");
    in.neighbors[0]={21,100};in.neighbors[1]={20,-1};in.neighbors[2]={20,-2};
    check(choose_escape(in)==-1,"increased damage and no-progress or rear-facing flats rejected");
    in=input();in.count=2;in.neighbors[0]={20,-2};in.neighbors[1]={20,-1};
    check(choose_escape(in)==-1,"equal-origin clearance is not progress");
    in.neighbors[0]={19,-100};check(choose_escape(in)==0,"strictly lower damage keeps old priority even facing threat");
    in=input(.5,-1,1);in.count=2;in.neighbors[0]={.5,0};in.neighbors[1]={.5000001,10};
    check(choose_escape(in)==0,"flat comparison requires exact origin damage without tolerance");
}
void survival_and_bounds() {
    auto in=input(20,-1,20);in.count=2;in.neighbors[0]={20,1};in.neighbors[1]={21,9};
    check(choose_escape(in)==-1,"lethal equality rejects outward movement");
    in.neighbors[1]={19,-4};check(choose_escape(in)==1,"nonlethal lower risk remains available from lethal origin");
    in.health=19;check(choose_escape(in)==-1,"lower-origin risk at exact HP is still lethal");
    in.health=19.5;check(choose_escape(in)==1,"fractional HP uses actual strict damage bound");
    check(choose_escape(EscapeInputs{})==-1,"default input has no candidate");
    in=input();check(choose_escape(in)==-1,"no legal numeric neighbor means no move");
    in.count=-1;check(choose_escape(in)==-1,"negative active count rejected");
    in.count=9;check(choose_escape(in)==-1,"overbound active count rejected rather than truncated");
    in=input();in.count=8;in.neighbors.fill({20,-1});in.neighbors[7]={0,3};
    check(choose_escape(in)==7,"eighth active neighbor included");
    in.count=7;check(choose_escape(in)==-1,"inactive neighbor cannot escape caller filtering");
    in=input();in.count=1;in.neighbors[0]={20,0};
    in.health=0;check(choose_escape(in)==-1,"zero HP rejected");
    in.health=-1;check(choose_escape(in)==-1,"negative HP rejected");
    in.health=std::numeric_limits<double>::infinity();check(choose_escape(in)==-1,"unknown infinite HP rejected");
    in.health=100;in.origin_damage=std::numeric_limits<double>::quiet_NaN();check(choose_escape(in)==-1,"NaN origin rejected");
    in.origin_damage=std::numeric_limits<double>::infinity();check(choose_escape(in)==-1,"infinite origin rejected");
    in.origin_damage=-1;check(choose_escape(in)==-1,"negative origin rejected");
    in=input();in.count=3;in.neighbors[0]={std::numeric_limits<double>::quiet_NaN(),9};in.neighbors[1]={-1,9};in.neighbors[2]={20,0};
    check(choose_escape(in)==2,"malformed neighbors cannot outrank valid numeric candidate");
    in.neighbors[2]={std::numeric_limits<double>::infinity(),9};check(choose_escape(in)==-1,"no finite nonnegative candidate remains");
}
}
int main() {
    wolf_path();priorities();survival_and_bounds();
    std::printf("PASS plan8 escape choice %d checks\n",checks);
}
