#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include "../overlap/src/policy.h"
#include "../common/src/event_kernel.h"

// A small deterministic SDK backend for the verbatim production bank member.
// The caller below is included, not reimplemented. Movement, locks and channel
// clocks are observable engine boundaries; this is not a fuel-cost simulator.
namespace cambc {
struct vec2 {int x=0,y=0;bool operator==(const vec2&)const=default;};
enum class team {ally,enemy};
using handle_champion_t=unsigned;
struct altar_lock {vec2 tile;vec2 position()const{return tile;}};
inline unsigned current_round=219;
inline int fuel=100000;
inline std::vector<altar_lock> locks;
namespace constants {inline constexpr int submit_gems_range=1;}
struct FakeChampion {
    vec2 tile{15,13};int bag=22,submits=0,attacks=0;
    bool move_ready=true,attack_ready=true;
    p11::kernel::ChannelSnapshot channel;
    vec2 position()const{return tile;}
    int gem_count()const{return bag;}
    bool can_attack()const{return attack_ready;}
    void attack(const altar_lock&){assert(attack_ready);++attacks;attack_ready=false;}
    bool is_submitting_gems()const{return channel.kind==p11::kernel::ChannelKind::tribute;}
    explicit operator handle_champion_t()const{return 1;}
    void submit_gems(vec2 altar) {
        assert(bag>0&&!p11::kernel::preserve_channel(channel));
        assert(std::max(std::abs(tile.x-altar.x),std::abs(tile.y-altar.y))<=constants::submit_gems_range);
        assert(std::none_of(locks.begin(),locks.end(),[&](const auto&lock){return lock.position()==altar;}));
        channel={p11::kernel::ChannelKind::tribute,p11::tribute_ticks(bag),bag};++submits;
    }
};
inline FakeChampion current;
FakeChampion& self(){return current;}
unsigned round_number(){return current_round;}
int fuel_remaining(){return fuel;}
namespace vision {auto altar_locks(team){return locks;}}
}

namespace {
using cambc::team;
struct bitboard {
    std::vector<cambc::vec2> tiles;
    bitboard& operator|=(const bitboard&other){tiles.insert(tiles.end(),other.tiles.begin(),other.tiles.end());return *this;}
};
int distance(cambc::vec2 a,cambc::vec2 b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
bitboard around(cambc::vec2 p,int radius){
    bitboard board;
    for(int x=p.x-radius;x<=p.x+radius;++x)for(int y=p.y-radius;y<=p.y+radius;++y)board.tiles.push_back({x,y});
    return board;
}
template<class Policy> struct Caller {
    std::vector<cambc::vec2> altars_{{15,15}};
    cambc::vec2 next{14,14};bool blocked=false,stale_after_move=false;
    int moves=0,traces=0;
    p11::State state(int altar)const{
        p11::State value;value.round=int(cambc::current_round);value.bag=cambc::current.bag;value.altar=altar;
        value.hp=1416;value.maximum=1500;return value;
    }
    auto channel_snapshot()const{return cambc::current.channel;}
    void trace_channel(const p11::kernel::ChannelSnapshot&channel,bool started){
        assert(started&&p11::kernel::preserve_channel(channel));++traces;
    }
    bool move_to(const bitboard&goals){
        assert(!goals.tiles.empty());
        if(blocked||!cambc::current.move_ready)return false;
        assert(distance(cambc::current.tile,next)==1);
        cambc::current.tile=next;cambc::current.move_ready=false;++moves;
        if(stale_after_move)++cambc::current_round;
        return true;
    }
#include "../common/src/bank_caller.inc"
};

void reset(){cambc::current={};cambc::current_round=219;cambc::fuel=100000;cambc::locks.clear();}

template<class Policy> std::pair<int,int> complete_from_native_arrival(){
    reset();Caller<Policy> caller;
    int bank_round=-1,resume_round=-1;
    for(int round=219;round<=224;++round){
        cambc::current_round=unsigned(round);cambc::current.move_ready=true;
        const auto paid=p11::kernel::owner_start(cambc::current.channel,cambc::current.bag);
        if(paid.banked){assert(paid.banked==22);cambc::current.bag=0;bank_round=round;}
        if(!p11::kernel::preserve_channel(cambc::current.channel)){
            const bool busy=caller.bank();
            if(!busy&&bank_round>=0&&resume_round<0)resume_round=round;
        }
        p11::kernel::owner_end(cambc::current.channel);
    }
    assert(caller.moves==1&&cambc::current.submits==1&&caller.traces==1);
    return {bank_round,resume_round};
}
}

int main(){
    // Exact native input: round219, Varg(15,13), bag22, altar(15,15),
    // one remaining walk. Compare the production caller under real policies.
    assert((complete_from_native_arrival<p11_overlap::Policy>()==std::pair{222,222}));
    assert((complete_from_native_arrival<p11_tempo::Policy>()==std::pair{223,223}));

    reset();Caller<p11_overlap::Policy> stale;stale.stale_after_move=true;
    assert(stale.bank()&&stale.moves==1&&cambc::current.submits==0);
    reset();Caller<p11_overlap::Policy> blocked;blocked.blocked=true;
    assert(!blocked.bank()&&blocked.moves==0&&cambc::current.submits==0);
    reset();Caller<p11_overlap::Policy> immobile;cambc::current.move_ready=false;
    assert(!immobile.bank()&&cambc::current.submits==0);
    reset();Caller<p11_overlap::Policy> low;cambc::fuel=30000;
    assert(low.bank()&&low.moves==1&&cambc::current.submits==0);
    reset();Caller<p11_overlap::Policy> enough;cambc::fuel=30001;
    assert(enough.bank()&&cambc::current.submits==1);
    reset();Caller<p11_overlap::Policy> still_far;cambc::current.tile={15,10};still_far.next={15,11};
    assert(still_far.bank()&&still_far.moves==1&&cambc::current.submits==0);
    reset();Caller<p11_overlap::Policy> empty;cambc::current.bag=0;
    assert(!empty.bank()&&empty.moves==0);
    reset();Caller<p11_overlap::Policy> no_altar;no_altar.altars_.clear();
    assert(!no_altar.bank()&&no_altar.moves==0);
    reset();Caller<p11_overlap::Policy> not_due;cambc::current.bag=7;cambc::current.tile={15,9};
    assert(!not_due.bank()&&not_due.moves==0&&cambc::current.submits==0);
    reset();Caller<p11_overlap::Policy> locked;cambc::locks.push_back({{15,15}});
    assert(locked.bank()&&locked.moves==1&&cambc::current.submits==0&&cambc::current.attacks==1);
    reset();Caller<p11_overlap::Policy> unavailable;cambc::locks.push_back({{15,15}});cambc::current.attack_ready=false;
    assert(unavailable.bank()&&cambc::current.submits==0&&cambc::current.attacks==0);
}
