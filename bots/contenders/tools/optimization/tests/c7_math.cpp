#include "opt/league.hpp"
#include <cmath>
#include <iostream>

int main() {
    using namespace opt::league;
    unsigned checks=0;
    auto check=[&](bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);};
    const Matrix rps{{0,-1,1},{1,0,-1},{-1,1,0}};
    const auto rps_solution=solve(rps,100000);
    for(double p:rps_solution.policy)check(std::abs(p-1.0/3.0)<0.015,"RPS policy should approach uniform");
    check(rps_solution.lower<=0 && rps_solution.upper>=0,"RPS empirical bounds should contain zero");
    check(rps_solution.gap<0.025,"RPS finite matrix gap should close");
    const Matrix catastrophic{{1,1,-1},{0.25,0.25,0.25}};
    const auto robust=solve(catastrophic,50000);
    check(robust.policy[1]>0.99,"maximin should prefer safe policy over high average with catastrophic weakness");
    check(robust.lower>0.249,"safe policy security value should be retained");
    const auto single=solve({{0.4}},10);
    check(single.lower==0.4 && single.upper==0.4 && single.gap==0,"single cell is exact");
    FictitiousPlay continuous(3,3), resumed(3,3);
    for(unsigned i=0;i<300;++i) continuous.step(rps);
    resumed.restore(opt::Json::parse(continuous.checkpoint().dump()));
    for(unsigned i=0;i<700;++i){continuous.step(rps);resumed.step(rps);}
    check(continuous.checkpoint()==resumed.checkpoint(),"saved finite matrix solver must resume bit for bit");
    bool rejected=false;
    try{validate({{1,0},{0}});}catch(const std::exception&){rejected=true;}
    check(rejected,"missing matrix entry must not become zero");
    rejected=false;
    try{validate({});}catch(const std::exception&){rejected=true;}
    check(rejected,"empty eligible policy population must be rejected");
    rejected=false;
    try{validate({{std::numeric_limits<double>::infinity()}});}catch(const std::exception&){rejected=true;}
    check(rejected,"nonfinite matrix payoff must be rejected");
    const auto arbitrary=bounds({{1,-1},{-1,1}},{3,1},{1,4});
    check(arbitrary.lower<=0 && arbitrary.upper>=0,"bounds valid before convergence");
    std::cout<<"C7 math: "<<checks<<" checks passed\n";
}
