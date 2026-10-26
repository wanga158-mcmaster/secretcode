#define TT_CAPTURE_FIXTURE
#include "../src/independent_capture.cpp"
#include <cstdio>
#include <limits>
#include <vector>

namespace {
using tt::capture_detail::Flags;
using tt::capture_detail::Bag;
using tt::capture_detail::Life;
using tt::capture_detail::Status;
struct Point { int x{},y{}; friend bool operator==(Point,Point)=default; };
struct Member { Point pos; float strength{}; int range{}; };
struct Group { std::vector<Member> members; };
struct Actor
{
    Point pos{5,5},last;
    float hp{100};
    Bag contents{23,11};
    Flags state{true,false,true};
    int* epoch{};
    int position_calls{},suspend_position{},absolute_calls{},relative_calls{};
    bool accepted{true},suspend_hp{},suspend_bag{},suspend_flags{};
    Point position() { if(++position_calls==suspend_position)++*epoch; return pos; }
    float health() const { if(suspend_hp)++*epoch; return hp; }
    Bag bag() const { if(suspend_bag)++*epoch; return contents; }
    Flags flags() const { if(suspend_flags)++*epoch; return state; }
    bool move_absolute(Point destination)
    { ++absolute_calls; last=destination; if(accepted)pos=destination; return accepted; }
    bool move(Point delta) { ++relative_calls; pos={pos.x+delta.x,pos.y+delta.y}; return true; }
};
int checks{},failures{};
void check(bool good,const char* label)
{ ++checks; if(!good){++failures;std::printf("FAIL %s\n",label);} }
}

int main()
{
    namespace capture=tt::capture_detail;
    const std::vector<Group> base{{{{{7,5},200,5},{{7,6},200,5}}},{{{{20,20},20,3}}}};
    auto groups=base;
    int epoch=60,group_queries=0,enemy_queries=0,legal_queries=0;
    Actor actor; actor.epoch=&epoch;
    Life life=Life::Reward;
    bool enemy=false,allowed=true,fuel=true;
    const auto reset=[&] {
        groups=base; actor=Actor{}; actor.epoch=&epoch; life=Life::Reward;
        enemy=false; allowed=true; fuel=true; group_queries=enemy_queries=legal_queries=0;
    };
    const auto invoke=[&](int group,Point expected,Point destination) {
        return capture::authority(groups,group,expected,destination,actor,[&]{return epoch;},
            [&](int){++group_queries;return life;},[&]{++enemy_queries;return enemy;},
            [&](Point){++legal_queries;return allowed;},[&]{return fuel;});
    };
    auto result=invoke(0,{5,5},{6,5});
    check(result.status==Status::Ready && result.availability==Life::Reward && result.zero_external &&
          result.safe_input && result.current_damage==0 && result.next_damage==0 && result.moved &&
          result.pos==Point{6,5} && actor.last==Point{6,5} && actor.absolute_calls==1 && actor.relative_calls==0,
          "fresh Reward omits selected sources only and commands an absolute adjacent CaptureWord waypoint");
    check(!capture::member_spawn(groups[0],Point{6,5}) && !result.holding &&
          result.gold==23 && result.gems==11 && result.hp==100,
          "Step waypoint need not be spawn and the result carries only pre-move actual bag and HP");
    reset(); actor.pos={7,5}; actor.state.can_move=false;
    result=invoke(0,{7,5},{7,5});
    check(result.status==Status::Ready && result.safe_input && result.zero_external && result.holding &&
          !result.moved && actor.absolute_calls==0 && legal_queries==0,
          "Hold is fresh one-input authority on actual selected member spawn even with spent movement");
    reset(); result=invoke(0,{5,5},{5,5});
    check(result.status==Status::NeedObservation && !result.holding && actor.position_calls==0,
          "Hold outside selected spawn is Unknown rather than invented capture progress");
    reset();
    result=capture::authority(groups,0,Point{5,5},Point{5,5},actor,[&]{return epoch;},
        [&](int){return life;},[&]{return enemy;},[](Point){return true;},[]{return true;},true);
    check(result.status==Status::Ready && result.safe_input && !result.holding && !result.moved &&
          actor.absolute_calls==0,"fresh Reward permits quiet non-capture staging without inventing progress");
    reset();groups[1].members={{{4,5},20,1}};
    result=capture::authority(groups,0,Point{5,5},Point{5,5},actor,[&]{return epoch;},
        [&](int){return life;},[]{return false;},[](Point){return true;},[]{return true;},true);
    check(result.status==Status::NeedObservation && !result.safe_input && actor.absolute_calls==0,
          "staging never drops an external harmful source");
    for(int invalid : {-1,2})
    {
        reset(); result=invoke(invalid,{5,5},{6,5});
        check(result.status==Status::NeedObservation && actor.position_calls==0 && group_queries==0 &&
              actor.absolute_calls==0,"invalid group cannot reach a native lifecycle query or move");
    }
    reset(); result=invoke(0,{5,5},{7,5});
    check(result.status==Status::NeedObservation && actor.position_calls==0,
          "CaptureWord waypoint must still be one distinct ordinary adjacent step");
    reset(); actor.pos={4,5}; result=invoke(0,{5,5},{6,5});
    check(result.status==Status::NeedObservation && actor.absolute_calls==0 && group_queries==0,
          "actual origin mismatch withdraws stale waypoint authority before group query");
    reset(); groups[1].members={{{4,5},20,1}};
    result=invoke(0,{5,5},{6,5});
    check(result.status==Status::NeedObservation && !result.zero_external && !result.safe_input &&
          result.current_damage==20 && result.next_damage==0 && actor.absolute_calls==0,
          "an external source threatening refused-step origin is never dropped by selected Reward");
    reset(); groups[1].members={{{7,5},20,1}};
    result=invoke(0,{5,5},{6,5});
    check(result.status==Status::NeedObservation && result.current_damage==0 && result.next_damage==20 &&
          !result.zero_external && actor.absolute_calls==0,
          "a positive destination-only external potential is unsupported without consuming finite HP");
    reset(); groups[1].members={{{5,5},200,2},{{6,5},200,2}};
    result=invoke(0,{5,5},{6,5});
    check(result.current_damage==100 && result.next_damage==100 && result.status==Status::NeedObservation &&
          actor.absolute_calls==0,"external sums saturate at actual HP without overflow or positive-exposure admission");
    reset(); groups[1].members[0].strength=std::numeric_limits<float>::quiet_NaN();
    result=invoke(0,{5,5},{6,5});
    check(result.status==Status::NeedObservation && !result.zero_external && actor.absolute_calls==0,
          "unknown external source metadata cannot become a quiet certificate");
    reset(); enemy=true; result=invoke(0,{5,5},{6,5});
    check(result.status==Status::NeedObservation && result.visible_enemy && !result.safe_input &&
          actor.absolute_calls==0 && enemy_queries==1,"fresh personally visible enemy withdraws capture authority");
    reset(); allowed=false; result=invoke(0,{5,5},{6,5});
    check(result.status==Status::NeedObservation && !result.moved && actor.absolute_calls==0 && legal_queries==1,
          "current terrain visibility occupancy or teleport point refusal prevents Step");
    reset(); actor.accepted=false; result=invoke(0,{5,5},{6,5});
    check(result.status==Status::NeedObservation && !result.moved && actor.pos==Point{5,5} && actor.absolute_calls==1,
          "native hidden refusal revokes authority without pretending the step or payout happened");
    reset(); actor.state.can_move=false; result=invoke(0,{5,5},{6,5});
    check(result.status==Status::Ready && result.safe_input && !result.can_move && !result.moved &&
          actor.absolute_calls==0 && legal_queries==0,"spent Step allowance returns Ready unmoved under zero external exposure");
    reset(); actor.state.alive=false; result=invoke(0,{5,5},{6,5});
    check(result.status==Status::NeedObservation && actor.absolute_calls==0 && group_queries==0,
          "dead body cannot start capture observation or act");
    reset(); actor.state.channeling=true; result=invoke(0,{5,5},{6,5});
    check(result.status==Status::NeedObservation && actor.absolute_calls==0 && group_queries==0,
          "active own channel withdraws capture action authority");
    reset(); actor.hp=0; result=invoke(0,{5,5},{6,5});
    check(result.status==Status::NeedObservation && actor.absolute_calls==0,"zero actual HP cannot admit even zero external bound");
    reset(); actor.hp=std::numeric_limits<float>::infinity(); result=invoke(0,{5,5},{6,5});
    check(result.status==Status::NeedObservation && actor.absolute_calls==0,"nonfinite actual HP is Unknown");
    for(Life changed : {Life::Alive,Life::Respawning,Life::Unknown})
    {
        reset(); actor.pos={7,5}; result=invoke(0,{7,5},{7,5});
        check(result.status==Status::Ready && result.holding,"fresh Reward can authorize only this current hold input");
        life=changed; actor.contents={41,19}; result=invoke(0,{7,5},{7,5});
        const auto lifecycle_status=changed==Life::Unknown ? Status::NeedObservation : Status::Ready;
        check(result.status==lifecycle_status && result.availability==changed && !result.holding &&
              !result.zero_external && !result.safe_input && result.gold==41 && result.gems==19,
              "known lifecycle transition publishes fresh bag only while unobserved stays Unknown and no payout is inferred");
    }
    for(int suspension=1;suspension<=7;++suspension)
    {
        reset(); actor.suspend_position=suspension; result=invoke(0,{5,5},{6,5});
        check(result.status==Status::Stale && actor.absolute_calls==0,
              "every position-query epoch suspension withdraws capture command authority");
    }
    reset(); actor.suspend_hp=true; result=invoke(0,{5,5},{6,5});
    check(result.status==Status::Stale && actor.absolute_calls==0,"epoch stamp precedes own HP observation");
    reset(); actor.suspend_bag=true; result=invoke(0,{5,5},{6,5});
    check(result.status==Status::Stale && actor.absolute_calls==0,"epoch stamp precedes actual bag observation");
    reset(); actor.suspend_flags=true; result=invoke(0,{5,5},{6,5});
    check(result.status==Status::Stale && actor.absolute_calls==0,"epoch stamp precedes own flag observation");
    reset();
    result=capture::authority(groups,0,Point{5,5},Point{6,5},actor,[&]{return epoch;},
        [&](int){++epoch;return Life::Reward;},[]{return false;},[](Point){return true;},[]{return true;});
    check(result.status==Status::Stale && actor.absolute_calls==0,"suspended Reward query does not omit stale selected sources");
    reset();
    result=capture::authority(groups,0,Point{5,5},Point{6,5},actor,[&]{return epoch;},
        [](int){return Life::Reward;},[&]{++epoch;return false;},[](Point){return true;},[]{return true;});
    check(result.status==Status::Stale && actor.absolute_calls==0,"suspended enemy query cannot become absence authority");
    reset();
    result=capture::authority(groups,0,Point{5,5},Point{6,5},actor,[&]{return epoch;},
        [](int){return Life::Reward;},[]{return false;},[&](Point){++epoch;return true;},[]{return true;});
    check(result.status==Status::Stale && actor.absolute_calls==0,"position then epoch fence follows fresh landing queries");
    reset(); fuel=false; result=invoke(0,{5,5},{6,5});
    check(result.status==Status::NeedCompute && actor.position_calls==0 && actor.absolute_calls==0,
          "missing entry headroom defers before any own observation");
    reset(); int budgets=0;
    result=capture::authority(groups,0,Point{5,5},Point{6,5},actor,[&]{return epoch;},
        [](int){return Life::Reward;},[]{return false;},[](Point){return true;},[&]{return ++budgets<4;});
    check(result.status==Status::NeedCompute && actor.absolute_calls==0 && budgets==4,
          "fresh final headroom follows point legality and can defer the absolute command");
    std::printf("independent capture: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
