#include "capital_model.h"
#include <array>
#include <cstdio>
#include <limits>
using namespace wb::capital;
namespace {int checks{},failures{};void check(bool x,const char* m){++checks;if(!x){++failures;std::printf("FAIL %s\n",m);}}}
int main(){
 std::array<Camp,1>wolf{{{10,0,0,1350,20,3,true}}};
 Body marcella{1,1200,1200,45};marcella.entry_intervals=2;marcella.exit_intervals=3;
 auto solo=estimate(wolf,marcella);
 check(solo.supported&&solo.cleared&&solo.survives,"single-wolf basic word");
 check(solo.basic_hits==30&&solo.hp_cost==640&&solo.hp_after==560,"entry and thirty retaliation charges");
 check(!solo.geometry_modelled&&solo.exit_cost==0,"geometry unproved; dead camp exit free");
 std::array<Camp,2>two{{{10,0,0,1350,20,3,true},{11,1,0,1350,20,3,true}}};
 marcella.entry_intervals=0;auto partial=estimate(two,marcella);
 check(partial.supported&&!partial.cleared&&partial.basic_hits>0&&partial.survives,"finite partial work");
 check(partial.exit_cost>0&&partial.needs_recovery&&partial.remaining_hp>0&&partial.remaining_hp<2700,"explicit exit; no fake receipt");
 auto residual=two;for(unsigned i=0;i<2;++i){residual[i].hp=partial.residual_hp[i];residual[i].alive=residual[i].hp>0;}
 check(estimate(residual,marcella).cleared,"actual recovered body can finish persistent work");
 Body varg{2,1000,1000,40};varg.basic_splash=1;varg.exit_intervals=3;
 Body willow{3,1000,1000,55};willow.exit_intervals=3;std::array<Body,2>party{varg,willow};
 auto pair=estimate_party(two,party);
 check(pair.supported&&pair.cleared&&pair.survives,"named pair with conservative per-body charge");
 check(pair.body[0].basic_hits>0&&pair.body[1].basic_hits>0,"both contributions counted");
 check(pair.body[0].damaged_members>pair.body[0].basic_hits,"splash exact geometry");
 party[1].first_hit_delay=100;auto late=estimate_party(two,party,64);
 check(!late.cleared&&late.body[1].basic_hits==0,"late helper not borrowed");
 party[1]=willow;party[1].first_hit_delay=4;auto lag=estimate_party(two,party);
 check(lag.rounds>=pair.rounds&&lag.body[1].first_target_round==4,"helper arrival lag");
 std::array<Camp,1>quiet{{{8,0,0,120,0,3,true}}};
 check(estimate(quiet,marcella).hp_after==1200,"zero-damage exposure");
 Body cold=marcella;cold.hp=100;cold.exit_intervals=4;auto none=estimate(wolf,cold);
 check(!none.cleared&&none.basic_hits==0&&none.needs_recovery&&none.survives,"preserve finite retreat");
 std::array<Camp,1>rounding{{{9,0,0,649,0,1,true}}};Body damage=marcella;damage.strength=129.8f;
 check(estimate(rounding,damage).basic_hits==6,"float32 repeated subtraction");
 Body jungle=marcella;jungle.incoming_multiplier=.7f;
 check(estimate(wolf,jungle).hp_cost<estimate(wolf,marcella).hp_cost,"observed Jungle multiplier");
 Body invalid=marcella;invalid.hp=std::numeric_limits<float>::quiet_NaN();check(!estimate(wolf,invalid).supported,"NaN refused");
 invalid=marcella;invalid.incoming_multiplier=0;check(!estimate(wolf,invalid).supported,"fake immunity refused");
 invalid=marcella;invalid.strength=std::numeric_limits<float>::infinity();check(!estimate(wolf,invalid).supported,"infinity refused");
 auto dup=two;dup[1].id=dup[0].id;check(!estimate(dup,marcella).supported,"duplicate camp refused");
 party[1]=party[0];check(!estimate_party(two,party).supported,"one body cannot help itself");
 check(!estimate(wolf,marcella,129).supported,"bounded horizon");
 CompleteAlternative a{2,200,true,28},b{3,200,true,28};auto benefit=compare_complete(2,3,200,60,true,a,b);
 check(benefit.comparable&&benefit.cooperation_better&&benefit.difference==4,"same-horizon named complete alternatives");
 b.horizon=201;check(!compare_complete(2,3,200,60,true,a,b).comparable,"horizon mismatch");
 b.horizon=200;b.complete_observed=false;check(!compare_complete(2,3,200,60,true,a,b).comparable,"unproved independent suffix");
 b.complete_observed=true;check(!compare_complete(2,3,200,60,false,a,b).comparable,"unconverted party suffix");
 check(!compare_complete(3,2,200,60,true,a,b).comparable,"named bodies retained");
 // A departing worker pays its exit at departure, before a late helper kills.
 party={varg,willow};party[0].hp=180;party[1].first_hit_delay=4;
 auto exit=estimate_party(two,party);check(exit.body[0].exit_cost>0,"departure cannot borrow future helper kills");
 std::printf("capital_model: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
