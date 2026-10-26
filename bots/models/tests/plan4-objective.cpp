#include "../common/pure.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace plan4;
int main(){
    auto info=std::make_unique<Information>();auto a=std::make_unique<Forecast>(),b=std::make_unique<Forecast>();
    a->reset();b->reset();Job job;int cases=0;
    a->objective={8,1,80,0,800};b->objective={8,0,800,0,8};
    assert(forecast_better(*info,*a,job,*b,job));++cases;
    a->objective={8,0,1,0,800};b->objective={8,0,0,0,8};
    assert(forecast_better(*info,*a,job,*b,job));++cases;
    a->objective={8,0,80,16,800};b->objective={4,0,40,8,404};
    assert(forecast_better(*info,*a,job,*b,job));++cases;
    // A deterministic fallback is the same law repeated eight times.
    a->objective={8,0,80,16,800};b->objective={};b->our_secured=10;b->enemy_secured=2;b->first_conversion_round=100;
    assert(!forecast_better(*info,*a,job,*b,job)&&!forecast_better(*info,*b,job,*a,job));++cases;
    // Cutoff before expectation: (80,0),(160,48) at H128 gives zero;
    // the mean-duration/mean-reward shortcut would falsely credit24.
    a->objective={2,0,0,0,258};b->objective={2,0,48,0,209};
    assert(forecast_better(*info,*b,job,*a,job));++cases;
    Forecast copy=*a;assert(copy.objective.paths==2&&copy.objective.first_conversion_round==258);
    copy.reset();assert(!copy.objective.paths);++cases;
    ObjectiveSum invalid{9,0,0,0,0};assert(!set_objective(copy,invalid)&&!copy.objective.paths);++cases;
    invalid={8,9,0,0,0};assert(!set_objective(copy,invalid));++cases;
    invalid={8,0,std::numeric_limits<std::uint64_t>::max(),0,0};assert(!set_objective(copy,invalid));++cases;
    assert(set_objective(copy,{8,0,80,0,800}));++cases;
    std::cout<<"{\"schema\":\"plan4-objective-tests/1\",\"passed\":true,\"cases\":"<<cases<<"}\n";
}
