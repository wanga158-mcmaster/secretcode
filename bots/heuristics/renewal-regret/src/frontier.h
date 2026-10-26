#pragma once
#include <span>
#include <vector>
#include <algorithm>
namespace h12_renewal_regret {
struct Member {int x,y;bool visible,alive=false;};
struct Frontier {bool discover;std::vector<Member> goals;};
struct FrontierMemory {
    std::vector<unsigned char> dead;
    int certificate=-1;
    void expire(int round,int respawn) {
        if(certificate>=0&&round-certificate>=respawn){dead.clear();certificate=-1;}
    }
    void note_alive(std::size_t index,int round=0,int respawn=60) {
        expire(round,respawn);
        if(index<dead.size()&&dead[index])std::fill(dead.begin(),dead.end(),0);
        certificate=round;
    }
    void reset(){dead.clear();certificate=-1;}
};
// Called only when no living member is currently observed. Select unseen
// members after excluding witnessed deaths; navigation builds their perimeter.
inline Frontier goal_frontier(FrontierMemory&memory,std::span<const Member> members,bool known_alive=false,int round=0,int respawn=60) {
    memory.expire(round,respawn);
    if(memory.dead.size()!=members.size())memory.dead.assign(members.size(),0);
    // Death persists until group respawn. A witnessed living member that was
    // dead, or a living group after all members were dead, starts a new epoch.
    if(known_alive&&std::all_of(memory.dead.begin(),memory.dead.end(),[](auto d){return d!=0;}))
        std::fill(memory.dead.begin(),memory.dead.end(),0);
    for(std::size_t i=0;i<members.size();++i)if(members[i].visible&&members[i].alive)memory.note_alive(i,round,respawn);
    for(std::size_t i=0;i<members.size();++i)if(members[i].visible)memory.dead[i]=!members[i].alive;
    if(memory.certificate<0)memory.certificate=round;
    Frontier result{false,{}};
    result.goals.reserve(members.size());
    for(std::size_t i=0;i<members.size();++i)if(!members[i].visible&&!memory.dead[i]){
        result.discover=true;result.goals.push_back(members[i]);
    }
    if(!result.discover)result.goals.assign(members.begin(),members.end());
    return result;
}
constexpr bool clear_route_blocker(int stagnant,bool own_work,bool other_group,float incoming,bool in_range,bool splash_safe) {
    return stagnant>=4&&!own_work&&other_group&&incoming<=0&&in_range&&splash_safe;
}
template<class Mask> Mask approach_perimeter(const Mask&roots) {
    const auto horizontal=roots|roots.shift_right(1)|roots.shift_right(-1);
    return horizontal|horizontal.shift_up(1)|horizontal.shift_up(-1);
}
} // namespace h12_renewal_regret
