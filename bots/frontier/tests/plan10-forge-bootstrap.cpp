#include "../catalyst-forge/src/b-policy.h"
#include <cassert>
#include <cstdio>
using namespace p10;
static World frame(){
 World w;w.round=40;w.max_rounds=2000;w.spawn={38,2};w.fuel=100000;w.self.id=0;w.self.type=Champion::varg;w.self.position={35,2};w.self.hp=w.self.max_hp=1000;w.self.strength=40;w.self.focus=100;w.self.range=1;w.self.visible=w.self.alive=w.self.ally=true;w.self.can_attack=w.self.can_cast=w.self.can_move=true;
 auto&a=w.self.abilities[0];a.reset=a.ready=a.targets_self=true;a.max_cooldown=8;
 auto&s=w.self.abilities[1];s.shield=s.ready=s.targets_self=true;s.modifier=2.5;s.duration=5;s.max_cooldown=8;
 Group g;g.id=18;g.position={36,2};g.gems=132;g.gold=30;g.travel=0;g.to_bank=g.to_spawn=3;g.observed=g.complete=g.living=g.route_known=g.program_available=true;g.approach={35,2};
 for(int y=1;y<=5;++y)for(int x=35;x<=38;++x)if(x==35||x==38||y==1||y==5)g.approach_pads.push_back({x,y});
 const int ids[3][2]={{50,22},{54,18},{52,20}};for(int y=2;y<=4;++y)for(int x=36;x<=37;++x){Monster m;m.id=ids[y-2][x-36];m.group=18;m.position={x,y};m.hp=m.max_hp=x==36?120:2400;m.strength=x==36?0:60;m.range=x==36?3:4;m.observed=m.alive=true;g.hp+=m.hp;g.monsters.push_back(m);g.spawns.push_back(m.position);}
 Group small;small.id=19;small.position={34,2};small.gems=8;small.travel=0;small.to_bank=small.to_spawn=3;small.approach={35,2};small.approach_pads={{35,2},{34,1},{34,3},{33,2}};small.observed=small.complete=small.living=small.route_known=small.program_available=true;Monster m;m.id=99;m.group=19;m.position={34,2};m.hp=m.max_hp=40;m.observed=m.alive=true;small.monsters={m};small.spawns={m.position};small.hp=40;w.groups={g,small};return w;
}
static int no_headroom(){return 40200;}
int main(){
 p10_mobile::default_cache={};auto w=frame();p10_b_forge::BPolicy p;auto first=p.step(w,Arm::active);
 // The failed local prefix and long-clear refusal use this owner's two slots.
 // Reselection names the real alternate, whose proof starts next owner.
 assert(p.refusal_events==1&&p.fresh_traces==2&&first.group==19&&first.task==Task::inspect&&!first.safety_verified);
 ++w.round;assert(p.rejected(w,w.groups[0]));auto next=p.step(w,Arm::active);assert(next.group==19&&next.task!=Task::inspect&&p.refusal_events==1&&p.fresh_traces<=2);
 // Changed real source HP/capital invalidates its old refusal. No forecast
 // result, helper damage or wallet is carried into the new physical frame.
 auto changed=w;changed.groups[0].monsters[1].hp-=40;assert(!p.rejected(changed,changed.groups[0]));changed=w;changed.self.strength+=5;assert(!p.rejected(changed,changed.groups[0]));
 // A transient fuel refusal cannot poison the source or select a fictitious
 // productive continuation. The card can retry when a fresh budget arrives.
 p10_b_forge::BPolicy low;low.set_fuel_reader(no_headroom);auto wait=low.step(w,Arm::active);assert(wait.task==Task::inspect&&low.refusal_events==0&&low.fresh_traces==0);
 std::puts("Forge actual caller: measured long-clear refusal reselects; fresh premises and temporary fuel PASS");
}
