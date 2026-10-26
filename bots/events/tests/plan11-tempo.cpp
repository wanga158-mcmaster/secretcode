#include <cassert>
#include "../tempo/src/policy.h"
int main(){
 using namespace p11;
 assert(tribute_ticks(10)==1 && tribute_ticks(11)==2 && tribute_ticks(20)==2);
 assert(tribute_ticks(0)==0);
 assert(attacks(100,50)==2 && attacks(100.001f,50)==3);
 assert((Event{10,Phase::capture}<Event{10,Phase::camp_attack}));
 assert((Event{10,Phase::camp_attack}<Event{10,Phase::owner_start}));
 assert((Event{10,Phase::owner_end,0}<Event{10,Phase::owner_start,1}));
 std::array<Monster,2> ms{{{100,40,0},{300,10,0}}};
 auto schedule=clear_schedule(ms,100);
 assert(schedule.turns==4 && schedule.loss==130); // 40*2 +10*5
 assert(exposure_reserve(ms,schedule.turns)==400); // safety independent of nearest/Smith order
 State s;s.mature=true;
 std::array<Candidate,3> cs{};
 cs[0].id=0;cs[0].gems=20;cs[0].travel=3;cs[0].clear=2;cs[0].altar=20;
 cs[1]=cs[0];cs[1].id=1;cs[1].gems=15;cs[1].altar=1;
 cs[2]=cs[1];cs[2].id=2;cs[2].gems=100;cs[2].loss=900;
 assert(choose<p11_tempo::Policy>(s,cs)==1); // whole bank cycle beats bag rate; lethal option excluded
 s.round=1984;assert(choose<p11_tempo::Policy>(s,cs)==1);
 s.round=100;s.bag=11;s.altar=30;assert(!p11_tempo::Policy::bank(s));
 s.bag=20;assert(p11_tempo::Policy::bank(s));
 s.hp=600;s.round=100;cs[1].committed=true;
 assert(choose_partial<p11_tempo::Policy>(s,cs)==1);
 cs[1].committed=false;assert(choose_partial<p11_tempo::Policy>(s,cs)==-1);
 assert(capture_action(false)==CaptureAction::hold);
 assert(capture_action(true)==CaptureAction::contest);
 Candidate gold;gold.id=3;gold.gold=100;gold.travel=2;gold.clear=2;gold.altar=1;
 s.mature=false;const auto near_value=p11_tempo::Policy::score(s,gold);
 gold.altar=100;assert(p11_tempo::Policy::score(s,gold)==near_value);
 assert(target_rank(100,40,100)<target_rank(300,10,100));
 assert(ordered_exposure(ms,schedule)==230);
 // A valuable multi-monster clear must compete with a cheap camp, rather than
 // being excluded by assuming both wolves attack after their own deaths.
 std::array<Monster,4> progression{{{1100,20,0},{1100,20,0},{370,0,0},{370,0,0}}};
 auto timed=clear_schedule(progression,82.5f,.7f);
 State capital;capital.hp=capital.maximum=1125;capital.mature=false;
 std::array<Candidate,2> frontier{};frontier[0].id=0;frontier[0].gold=30;frontier[0].clear=8;
 frontier[1].id=1;frontier[1].gold=160;frontier[1].clear=timed.turns;frontier[1].loss=ordered_exposure(progression,timed,.7f);
 assert(choose<p11_tempo::Policy>(capital,frontier)==1);
}
