#include "capacity.h"
#include <cassert>
using namespace mb;
int main() {
    std::array<capacity::Body,8> b;
    b[0]={100,1000,1,-1,true}; b[1]={100,1000,1,-1,true};
    capacity::Job j; j.group=4; j.value=100; j.fight={1000,30000,60}; j.arrival[0]=2;j.arrival[1]=30;
    auto solo=capacity::quote(j,b,2,1), pair=capacity::quote(j,b,2,3);
    assert(solo.group==4 && pair.group==4 && solo.rate>pair.rate);
    j.arrival[1]=2; j.fight={2000,110000,120}; b[0].pool=b[1].pool=900;
    assert(capacity::quote(j,b,2,1).group<0);
    pair=capacity::quote(j,b,2,3); assert(pair.group==4 && pair.crew==3);
    capacity::Calendar cal; cal.issue(pair,50,true);
    assert(cal.ready==52 && cal.deadline==50+pair.finish+20);
    cal.release(); assert(cal.group<0);
    capacity::Job reward=j;reward.group=5;reward.hp_scale=0; reward.fight={};reward.value=50;
    auto assigned=capacity::allocate({j,reward},b,2);
    assert(!assigned.jobs.empty());
    for(const auto& a:assigned.jobs) for(const auto& c:assigned.jobs)
        if(a.group!=c.group) assert((a.crew&c.crew)==0);
    b[1].locked=4; assert(capacity::quote(j,b,2,1).group<0);
    assert(capacity::quote(reward,b,2,2).group<0);
    model::ChampionSummary live;
    assert(!capacity::live_stats(live,0,0));
    assert(capacity::live_stats(live,72,103) && live.strength==72 && live.focus==103);
    std::array<std::uint8_t,10> board{};board[0]=4;board[1]=192;
    capacity::Assignment commands;commands.body[0].group=6;commands.body[1].group=6;
    capacity::assign_commands(board,commands,2,513);
    assert(board[0]==4 && board[1]==7 && board[2]==2 && board[3]==1);
    capacity::Frontier frontier;
    for(int r=0;r<9;++r){assert(frontier.index(9,0)==r);frontier.advance(9,1);}
    assert(frontier.cursor==0);
}
