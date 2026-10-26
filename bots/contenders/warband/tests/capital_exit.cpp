#include "capital_exit.h"
#include <array>
#include <cstdio>
#include <limits>
using namespace wb::capital::exit;
namespace {int checks{},failures{};void check(bool b,const char* m){++checks;if(!b){++failures;std::printf("FAIL %s\n",m);}}}
int main(){
 std::array<Source,1>s{{{0,5,5,3,20,true,false,true}}};
 std::array<Point,4>p{{{6,5},{7,5},{8,5},{9,5}}};
 auto q=quote(s,p,100,0);
 check(q.supported&&q.affordable&&q.ends_quiet&&q.survives,"one basic and actual ordinary exit");
 check(q.damage==80&&q.hp_after==20&&q.hits==4&&q.response_intervals==5,"three in-range starts plus refused origin");
 auto no_refusal=quote(s,p,100,0,0);
 check(no_refusal.damage==60&&no_refusal.hp_after==40,"explicit zero refusal reserve");
 check(!quote(s,p,80,0).affordable&&quote(s,p,80,0).hp_after==0,"strict positive life after exit");
 auto shield=quote(s,p,100,30);
 check(shield.damage==80&&shield.hp_after==50&&shield.shield_after==0,"shield absorbs sequential incoming hits");
 auto enough=quote(s,p,100,100);
 check(enough.hp_after==100&&enough.shield_after==20,"remaining shield retained");
 s[0].range=2;check(quote(s,p,100,0).damage==60,"actual range only, no automatic plus one");
 s[0].range=0;check(quote(s,p,100,0).damage==0,"whole selected group out-of-range source not charged");
 s[0]={0,5,5,3,20,true,false,false};
 check(quote(s,p,100,0).damage==0,"sleeping external excluded under explicit limited scope");
 s[0].awake=true;check(quote(s,p,100,0).damage==80,"awake external included");
 s[0].alive=false;check(quote(s,p,100,0).damage==0,"dead source cannot retaliate");
 std::array<Source,2>f{{{0,5,5,0,30.3f,true,true,false},{1,5,5,0,30.3f,true,true,false}}};
 std::array<Point,2>fp{{{5,5},{6,5}}};
 auto exact=quote(f,fp,1200,0,0);
 float sequential=1200;sequential-=30.3f;sequential-=30.3f;
 float aggregate=1200-(30.3f+30.3f);
 check(exact.hp_after==sequential&&exact.hp_after!=aggregate,"per-source float32 arithmetic preserved");
 auto absorbed=quote(f,fp,100,40,0);float expected=100-(30.3f-(40-30.3f));
 check(absorbed.hp_after==expected&&absorbed.shield_after==0,"partial second hit absorbs exact remaining shield");
 std::array<Point,1>unsafe{{{5,5}}};auto hold=quote(f,unsafe,1200,0,0);
 check(hold.supported&&hold.survives&&!hold.ends_quiet&&!hold.affordable,"unfinished exposed hold is not complete egress");
 std::array<Source,0>empty{};check(quote(empty,p,100,0).affordable,"empty threat set supports quiet path");
 auto invalid=s;invalid[0].damage=-1;check(!quote(invalid,p,100,0).supported,"negative damage refused");
 invalid[0].damage=std::numeric_limits<float>::quiet_NaN();check(!quote(invalid,p,100,0).supported,"NaN source refused");
 invalid=s;invalid[0].range=-1;check(!quote(invalid,p,100,0).supported,"negative range refused");
 auto duplicate=f;duplicate[1].id=duplicate[0].id;check(!quote(duplicate,fp,100,0).supported,"duplicate identity refused");
 check(!quote(s,p,std::numeric_limits<float>::infinity(),0).supported,"infinite HP refused");
 check(!quote(s,p,100,-1).supported,"negative shield refused");
 check(!quote(s,p,0,0).supported,"dead body refused");
 check(!quote(s,std::span<const Point>{},100,0).supported,"missing actual origin refused");
 auto jump=p;jump[1]={8,5};check(!quote(s,jump,100,0).supported,"teleport cannot stand in ordinary egress");
 auto negative=p;negative[0].x=-1;check(!quote(s,negative,100,0).supported,"negative coordinates refused");
 check(!quote(s,p,100,0,9).supported,"finite refused-origin bound enforced");
 std::array<Point,130>longpath{};check(!quote(s,longpath,100,0).supported,"finite occupancy bound enforced");
 std::array<Source,501>many{};check(!quote(many,p,100,0).supported,"finite source bound enforced");
 auto overflow=f;overflow[0].damage=overflow[1].damage=std::numeric_limits<float>::max();
 check(!quote(overflow,fp,100,0,0).supported,"finite result arithmetic enforced");
 auto stand=fp;stand[1]=stand[0];check(quote(f,stand,1000,0,0).response_intervals==2,"duplicate cells price actual physical holds");
 std::array<Source,3>nonadjacent{{{1,5,5,0,1,true,true,false},{2,5,5,0,1,true,true,false},{1,5,5,0,1,true,true,false}}};
 check(!quote(nonadjacent,fp,100,0,0).supported,"nonadjacent duplicate refused in unordered fallback");
 std::array<Source,2>unordered{{{1,5,5,0,.5f,true,true,false},{0,5,5,0,.00005f,true,true,false}}};
 auto supplied=quote(unordered,fp,1024.25f,0,0);
 float caller_order=1024.25f;caller_order-=.5f;caller_order-=.00005f;
 float identity_order=1024.25f;identity_order-=.00005f;identity_order-=.5f;
 check(supplied.supported&&supplied.hp_after==caller_order&&caller_order!=identity_order,"unordered unique validation preserves caller per-hit order");
 std::array<Source,3>descending{{{2,5,5,0,1,true,true,false},{1,5,5,0,2,true,true,false},{0,5,5,0,3,true,true,false}}};
 auto reverse=quote(descending,fp,100,0,0);
 check(reverse.supported&&reverse.damage==6&&reverse.hp_after==94,"descending unique identities remain supported");
 std::printf("capital_exit: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
