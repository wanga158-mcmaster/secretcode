#include "../catalyst-forge/src/c-policy.h"
#include "../shield-foundry/src/c-policy.h"
#include "../two-spindle/src/c-policy.h"
#include "../metered-works/src/c-policy.h"
#include "../orchards/src/c-policy.h"
#include "../catalyst-depots/src/c-policy.h"
#include "../envelope-draft/src/c-policy.h"
#include "../deferred-forks/src/c-policy.h"
#include "../exposure-works/src/c-policy.h"
#include "../initiative-works/src/c-policy.h"
#include "../investment-frontier/src/c-policy.h"
#include "../triadic-arena/src/c-policy.h"
#include "../conversion-race/src/c-policy.h"
#include "../paired-lifeline/src/c-policy.h"
#include "../realtime-guild/src/c-policy.h"
#include "../progression-interdiction/src/c-policy.h"
#include <cstdio>
#include <cassert>
using namespace p10;
template<class P>constexpr bool owns(){if constexpr(requires{P::owns_capital;})return P::owns_capital;else return false;}
template<class P>void sample(int n){static_assert(owns<P>());World w;w.round=20;w.self.id=0;w.self.type=Champion::varg;w.self.position={1,1};w.self.visible=w.self.alive=w.self.ally=w.self.at_spawn=w.self.can_move=w.self.can_attack=w.self.can_cast=true;w.self.hp=w.self.max_hp=1000;w.self.strength=70;w.self.focus=100;w.self.strength_growth=5;w.self.focus_growth=10;w.own_gold=30;P a,b;auto x=a.step(w,Arm::active),y=b.step(w,Arm::control);assert(x.task!=Task::buy&&y.task!=Task::buy);std::printf("{\"card\":%d,\"owns_capital\":%s,\"activeIndependent\":%s,\"controlIndependent\":%s,\"activeTask\":%d,\"controlTask\":%d}\n",n,owns<P>()?"true":"false",x.task==Task::independent?"true":"false",y.task==Task::independent?"true":"false",int(x.task),int(y.task));}
int main(){
sample<p10_c_forge::CPolicy>(1);
sample<p10_c_foundry::CPolicy>(2);
sample<p10_c_spindle::CPolicy>(3);
sample<p10_c_metered::CPolicy>(4);
sample<p10_c_orchard::CPolicy>(5);
sample<p10_depots::CPolicy>(6);
sample<p10_envelope::CPolicy>(7);
sample<p10_forks::CPolicy>(8);
sample<p10_exposure::CPolicy>(9);
sample<p10_initiative::CPolicy>(10);
sample<p10_c11::CPolicy>(11);
sample<p10_c12::CPolicy>(12);
sample<p10_c13::CPolicy>(13);
sample<p10_c14::CPolicy>(14);
sample<p10_c15::CPolicy>(15);
sample<p10_c16::CPolicy>(16);
}
