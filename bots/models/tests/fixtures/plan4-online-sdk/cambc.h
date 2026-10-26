#pragma once
// Deliberately small fault-injecting SDK double for optional evidence collectors.
// Production wrappers are checked separately against sdk/cpp/cambc.h.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>
namespace cambc {
struct vec2 { int x=0,y=0; friend bool operator==(vec2,vec2)=default; };
enum class team { ally,enemy };
enum class camp_type { wolf, elk, gem_cluster, gold_cluster, monolith, golem };
class champion_state {
    std::uint32_t value_ = 0;
public:
    constexpr champion_state() = default;
    constexpr explicit champion_state(std::uint32_t value):value_(value){}
    static constexpr champion_state from_bits_retain(std::uint32_t value){return champion_state{value};}
    constexpr bool contains(champion_state other)const{return (value_ & other.value_) == other.value_;}
    constexpr std::uint32_t bits()const{return value_;}
    static const champion_state recalling, submitting_gems, teleporting, completed_turn;
};
inline constexpr champion_state champion_state::recalling{1u<<1}, champion_state::submitting_gems{1u<<2},
    champion_state::teleporting{1u<<12}, champion_state::completed_turn{1u<<5};
struct item_type {
    int value;
    float passive_magnitude() const { return 0.1f; }
    static const item_type monster_emblem_of_strength, jungle_emblem_of_health;
};
inline const item_type item_type::monster_emblem_of_strength{0}, item_type::jungle_emblem_of_health{1};
struct inventory {
    unsigned monster=0,jungle=0;
    int operator[](item_type item) const { return int(item.value==0?monster:jungle); }
};
struct camp_stats { float base_health=100, base_strength=20; int base_attack_range=1; };
namespace constants {
inline constexpr int max_rounds=80, winning_gems=1000, capture_time=5, recall_time=5,
    submit_gem_base_duration=0, submit_gem_minimum_duration=1, submit_gems_range=1;
inline constexpr float submit_gem_duration_multiplier=0.1f, spawn_healing=0.1f;
}
enum class handle_champion_t : unsigned {};
enum class handle_camp_t : unsigned {};
enum class handle_camp_group_t : unsigned {};
struct uninitialized_t {};
inline constexpr uninitialized_t uninitialized{};
namespace mock {
// Synthetic wrapper-query units, not a complete import-level SDK emulator.
// In particular native roster/positions/group-state wrappers contain multiple
// imports. The raw actor packet and each legacy live info getter both cost one
// unit, so their relative packet reduction is represented without discounting.
inline int round=10, fuel=100000, queries=0, stop_after=-1, shift_after=-1, query_cost=100;
inline int group_queries=0, member_queries=0, unauthorized=0;
// kind/spawn are pinned-SDK local getters. Count access without host-query fuel.
inline int kind_accesses=0, spawn_accesses=0, base_stats_queries=0;
inline int raw_queries=0, raw_error=0, raw_error_actor=-1, negative_actor=-1;
inline int camp_raw_queries=0, camp_state_queries=0, camp_raw_error=0, camp_raw_error_id=-1;
inline bool camp_raw_error_payload=false;
inline std::array<int,2> roster_calls{}, roster_successes{};
inline int roster_throw_side=-1;
inline bool raw_error_payload=false;
inline std::array<int,6> stats_by_kind{};
inline std::array<bool,2500> visible;
inline std::vector<vec2> altar_positions, allied_spawns, enemy_spawns, wall_positions;
// Opt-in current gem-cluster metadata for caller witnesses. Legacy component
// cases retain their deliberately synthetic defaults.
inline bool current_gem_camps = false;
struct Actor { int id=-1;vec2 position{};bool alive=true,shown=true;team side=team::ally;
    unsigned bag=0;float hp=100,maximum=100,strength=10;int range=1,current_priority=0,next_priority=0;bool completed=false;
    float focus=0;std::uint32_t state_bits=0;int channel_ticks=0;unsigned gold=0,monster_items=0,jungle_items=0; };
struct CampPacket { int id;vec2 spawn;float health,strength,maximum; };
inline std::vector<CampPacket> camp_packets;
#ifdef PLAN4_OBSERVER_STATE_PROBE
// Opt-in wrapper-seam probe. This is not an sdk_camp_state import emulator:
// the ordinary double still represents state by HP, not the SDK's enum ABI.
struct StateRead { int id, ordinal, round, self_id; bool finished=false, dead=false; };
inline std::vector<StateRead> state_reads;
inline int self_id=0;
inline void (*before_state)(int,int)=nullptr;
inline void (*after_state)(int,int,bool)=nullptr;
#endif
inline std::vector<Actor> actors;
inline std::vector<vec2> camp_blocks,teleport_blocks;
inline std::vector<std::pair<int,float>> camp_health_overrides;
inline std::vector<std::pair<int,float>> camp_strength_overrides;
inline std::array<int,64> states;
inline int tile(vec2 p) { assert(p.x>=0&&p.y>=0&&p.x<50&&p.y<50);return p.y*50+p.x; }
inline void query() {
    ++queries;fuel-=query_cost;
    if(queries==stop_after) fuel=60000;
    if(queries==shift_after) ++round;
    assert(fuel>0);
}
inline void reset() {
    round=10;fuel=100000;queries=group_queries=member_queries=unauthorized=0;
    kind_accesses=spawn_accesses=base_stats_queries=0;
    raw_queries=raw_error=0;raw_error_actor=negative_actor=-1;
    camp_raw_queries=camp_state_queries=camp_raw_error=0;camp_raw_error_id=-1;camp_raw_error_payload=false;
    roster_calls.fill(0);roster_successes.fill(0);roster_throw_side=-1;
    raw_error_payload=false;
    stats_by_kind.fill(0);
    stop_after=shift_after=-1;query_cost=100;visible.fill(false);states.fill(1);
    current_gem_camps=false;
    actors={{0,{0,0},true,true,team::ally}};
    camp_blocks.clear();teleport_blocks.clear();
    camp_health_overrides.clear();
    camp_strength_overrides.clear();camp_packets.clear();
#ifdef PLAN4_OBSERVER_STATE_PROBE
    state_reads.clear();self_id=0;before_state=nullptr;after_state=nullptr;
#endif
    altar_positions.clear();allied_spawns.clear();enemy_spawns.clear();wall_positions.clear();
}
inline Actor& actor(int id) { return *std::find_if(actors.begin(),actors.end(),[&](const auto& a){return a.id==id;}); }
}
namespace detail {
// Pinned imports.h layout; exactly one visible packet query per call.
struct champion_info {
    std::int32_t team, type, x, y;
    float health, max_health, strength, focus;
    std::int32_t move_speed, attack_range, state;
};
static_assert(sizeof(champion_info)==44);
inline int sdk_champion_at(int x,int y,std::uint32_t* out) {
    mock::query();
    const auto it=std::find_if(mock::actors.begin(),mock::actors.end(),[&](const auto& a){
        return a.position==vec2{x,y}&&a.shown&&a.alive;});
    if(it==mock::actors.end())return 97;
    *out=std::uint32_t(it->id);return 0;
}
inline int sdk_champion(std::uint32_t handle, champion_info* out) {
    mock::query();++mock::raw_queries;
    if(int(handle)==mock::raw_error_actor && mock::raw_error){
        if(mock::raw_error_payload)*out={0,999,8,8,999,999,999,999,9,9,-1};
        return mock::raw_error;
    }
    const auto& a=mock::actor(int(handle));
    if(handle!=0 && !a.shown)return 97;
    *out={int(a.side),a.id,a.position.x,a.position.y,a.hp,a.maximum,a.strength,a.focus,1,a.range,
        int(a.state_bits)|(a.completed?int(champion_state::completed_turn.bits()):0)};
    if(int(handle)==mock::negative_actor)out->x=-1;
    return 0;
}
struct camp_state {
    std::int32_t x,y;
    float health,max_health,strength,focus;
    std::int32_t cooldowns[2];
};
static_assert(sizeof(camp_state)==32);
inline int sdk_camp(std::uint32_t handle,camp_state* out) {
    mock::query();++mock::member_queries;++mock::camp_raw_queries;
    if(int(handle)==mock::camp_raw_error_id && mock::camp_raw_error){
        if(mock::camp_raw_error_payload)*out={8,8,999,999,999,999,{9,9}};
        return mock::camp_raw_error;
    }
    const auto found=std::find_if(mock::camp_packets.begin(),mock::camp_packets.end(),
        [handle](const auto& entry){return entry.id==int(handle);});
    if(found==mock::camp_packets.end())return 43;
    if(!mock::visible[mock::tile(found->spawn)]){++mock::unauthorized;return 97;}
    float health=found->health,strength=found->strength;
    for(const auto& [id,value]:mock::camp_health_overrides)if(id==int(handle))health=value;
    for(const auto& [id,value]:mock::camp_strength_overrides)if(id==int(handle))strength=value;
    *out={found->spawn.x,found->spawn.y,health,found->maximum,strength,0,{0,0}};
    return 0;
}
inline void check(int code){if(code)throw std::runtime_error("mock SDK error");}
}
inline std::uint64_t fuel_remaining() { return std::uint64_t(mock::fuel); }
inline unsigned round_number() { return unsigned(mock::round); }
struct bitboard {
    std::array<bool,2500> cells{};
    bitboard()=default;
    explicit bitboard(uninitialized_t){}
    friend bitboard operator|(bitboard a,const bitboard& b){a|=b;return a;}
    static std::size_t linear_buffer_size(){return (2500+7)/8;}
    void export_linear_buffer(std::span<std::uint8_t> out)const{mock::query();assert(out.size()==linear_buffer_size());
        std::fill(out.begin(),out.end(),std::uint8_t(0));
        for(unsigned n=0;n<2500;++n)if(cells[n])out[n>>3]|=std::uint8_t(1u<<(n&7));}
    bitboard& operator|=(const bitboard& other){for(int n=0;n<2500;++n)cells[n]=cells[n]||other.cells[n];return *this;}
    void set(vec2 p,bool value){cells[mock::tile(p)]=value;}
    bool is_set(vec2 p) const { mock::query();return cells[mock::tile(p)]; }
    std::vector<vec2> positions() const {
        mock::query();std::vector<vec2> out;
        for(int n=0;n<2500;++n) if(cells[n]) out.push_back({n%50,n/50});
        return out;
    }
};
class champion {
    int id_;
    const mock::Actor& observed() const { mock::query();const auto& a=mock::actor(id_);
        if(id_!=0&&!a.shown){++mock::unauthorized;assert(false);}return a; }
public:
    explicit champion(int id):id_(id){}
    operator handle_champion_t() const { return handle_champion_t(id_); }
    bool is_alive() const { mock::query();return mock::actor(id_).alive; }
    vec2 position() const { mock::query();assert(id_==0||mock::actor(id_).shown);return mock::actor(id_).position; }
    bool is_visible() const { mock::query();return mock::actor(id_).shown; }
    int champion_name()const{mock::query();return id_;}
    float health()const{return observed().hp;}
    float max_health()const{return observed().maximum;}
    float strength()const{return observed().strength;}
    int attack_range()const{return observed().range;}
    unsigned gem_count()const{return observed().bag;}
    unsigned gold_count()const{return observed().gold;}
    float focus()const{return observed().focus;}
    champion_state state()const{const auto& value=observed();return champion_state::from_bits_retain(value.state_bits|
        (value.completed?champion_state::completed_turn.bits():0));}
    int remaining_channel_time()const{return observed().channel_ticks;}
    inventory item_count()const{const auto& value=observed();return {value.monster_items,value.jungle_items};}
    int prev_priority()const{return observed().current_priority;}
    int priority()const{return observed().next_priority;}
    bool has_completed_turn()const{return observed().completed;}
};
inline champion& self() { static champion own{0};
#ifdef PLAN4_OBSERVER_STATE_PROBE
    static champion other{1};assert(mock::self_id==0||mock::self_id==1);
    if(mock::self_id==1)return other;
#endif
    return own; }
inline unsigned global_submitted_gems(team) { mock::query();return 0; }
inline std::vector<champion> champions(team side) {
    const int index=side==team::ally?0:1;
    ++mock::roster_calls[index];mock::query();
    if(index==mock::roster_throw_side)throw std::runtime_error("mock roster query error");
    std::vector<champion> out;
    for(const auto& a:mock::actors)if(a.side==side)out.emplace_back(a.id);
    ++mock::roster_successes[index];return out; }
struct camp_state { bool dead=false;bool is_dead()const{return dead;} };
class camp {
    int id_;vec2 spawn_;float hp_,strength_;camp_type kind_;
    void authorized() const {
        mock::query();++mock::member_queries;
        if(!mock::visible[mock::tile(spawn_)]) { ++mock::unauthorized;assert(false); }
    }
    float live_health()const{
        for(const auto& [id,hp]:mock::camp_health_overrides)if(id==id_)return hp;
        return hp_;
    }
    float live_strength()const{
        for(const auto& [id,value]:mock::camp_strength_overrides)if(id==id_)return value;
        return strength_;
    }
public:
    camp(int id,vec2 spawn,float hp=100,float strength=20,camp_type kind=camp_type::wolf)
        :id_(id),spawn_(spawn),hp_(hp),strength_(strength),kind_(kind){
        const auto found=std::find_if(mock::camp_packets.begin(),mock::camp_packets.end(),
            [id](const auto& entry){return entry.id==id;});
        if(mock::current_gem_camps)assert(kind==camp_type::gem_cluster&&hp>=0&&hp<=120&&strength==0);
        const mock::CampPacket value{id,spawn,hp,strength,
            mock::current_gem_camps?120.f:kind==camp_type::golem?325.f:100.f};
        if(found==mock::camp_packets.end())mock::camp_packets.push_back(value);else *found=value;
    }
    operator handle_camp_t() const { return handle_camp_t(id_); }
    vec2 spawn_position() const { ++mock::spawn_accesses;return spawn_; }
    camp_type kind()const{++mock::kind_accesses;return kind_;}
    camp_stats base_stats()const{mock::query();++mock::base_stats_queries;
        const int k=int(kind_);assert(k>=0&&k<6);++mock::stats_by_kind[k];
        if(mock::current_gem_camps){assert(kind_==camp_type::gem_cluster);return {120,0,0};}
        return kind_==camp_type::golem?camp_stats{325,47,4}:camp_stats{};}
    camp_state state() const { authorized();++mock::camp_state_queries;
#ifdef PLAN4_OBSERVER_STATE_PROBE
        int ordinal=1;
        for(const auto& read:mock::state_reads)if(read.id==id_)++ordinal;
        const auto index=mock::state_reads.size();
        mock::state_reads.push_back({id_,ordinal,mock::round,mock::self_id});
        if(mock::before_state)mock::before_state(id_,ordinal);
        const bool dead=live_health()<=0;
        mock::state_reads[index]={id_,ordinal,mock::round,mock::self_id,true,dead};
        if(mock::after_state)mock::after_state(id_,ordinal,dead);
        return {dead};
#else
        return {live_health()<=0};
#endif
    }
    float health() const { authorized();return live_health(); }
    bool is_visible()const{mock::query();return mock::visible[mock::tile(spawn_)];}
    bool is_alive()const{authorized();return live_health()>0;}
    bool is_awake()const{authorized();return live_health()>0;}
    float strength()const{authorized();return live_strength();}
    vec2 position()const{authorized();return spawn_;}
    int attack_range()const{authorized();return mock::current_gem_camps?0:1;}
};
namespace map {
inline int width(){mock::query();return 50;}
inline int height(){mock::query();return 50;}
inline bitboard walls(){mock::query();bitboard out;for(auto p:mock::wall_positions)out.set(p,true);return out;}
inline bitboard holes(){mock::query();return {};}
inline bitboard altars(){mock::query();bitboard out;for(auto p:mock::altar_positions)out.set(p,true);return out;}
inline bitboard spawn_region(team side){mock::query();bitboard out;
    for(auto p:side==team::ally?mock::allied_spawns:mock::enemy_spawns)out.set(p,true);return out;}
struct camp_group_alive {};
struct camp_group_reward_available {};
struct camp_group_respawning { int timer; };
using camp_group_state=std::variant<camp_group_alive,camp_group_reward_available,camp_group_respawning>;
class camp_group {
    int id_;std::vector<camp> members_;
public:
    camp_group(int id,std::vector<camp> members):id_(id),members_(std::move(members)){}
    operator handle_camp_group_t() const { return handle_camp_group_t(id_); }
    const std::vector<camp>& camps()const{return members_;}
    auto begin()const{return members_.begin();}auto end()const{return members_.end();}
    unsigned gold_value()const{return mock::current_gem_camps?0:10;}
    unsigned gem_value()const{
        if(!mock::current_gem_camps)return 24;
        for(const auto& member:members_)assert(member.kind()==camp_type::gem_cluster);
        return unsigned(members_.size())*7;
    }
    int max_respawn_timer()const{return mock::current_gem_camps?50:30;}
    camp_group_state state() const {
        mock::query();++mock::group_queries;
        bool any=false;for(const auto& c:members_) any|=mock::visible[mock::tile(c.spawn_position())];
        if(!any){++mock::unauthorized;assert(false);}
        return mock::states[id_]==0?camp_group_state{camp_group_alive{}}:
            mock::states[id_]==1?camp_group_state{camp_group_reward_available{}}:camp_group_state{camp_group_respawning{max_respawn_timer()}};
    }
};
// Public setup belongs to the fixture, not a dynamic vision channel.
inline std::vector<camp_group> mock_groups;
inline std::vector<camp_group> camp_groups(){mock::query();return mock_groups;}
}
namespace vision {
inline bitboard visible_tiles(){mock::query();bitboard out;out.cells=mock::visible;return out;}
inline bitboard champion_tiles(team side){
    mock::query();bitboard out;
    for(const auto& a:mock::actors)if(a.shown&&a.alive&&a.side==side)out.cells[mock::tile(a.position)]=true;
    return out;
}
inline bitboard champion_tiles(){bitboard out;for(const auto& a:mock::actors)if(a.shown&&a.alive)out.cells[mock::tile(a.position)]=true;mock::query();return out;}
inline bitboard camp_tiles(){mock::query();bitboard out;for(const auto p:mock::camp_blocks)out.cells[mock::tile(p)]=true;return out;}
inline bitboard teleport_tiles(){mock::query();bitboard out;for(const auto p:mock::teleport_blocks)out.cells[mock::tile(p)]=true;return out;}
inline bitboard altar_lock_tiles(team){mock::query();return {};}
inline std::vector<champion> champions(team side){mock::query();std::vector<champion> out;
    for(const auto& a:mock::actors)if(a.side==side&&a.shown&&a.alive)out.emplace_back(a.id);return out;}
inline champion champion_at(vec2 p){
    mock::query();const auto it=std::find_if(mock::actors.begin(),mock::actors.end(),[&](const auto& a){return a.position==p&&a.shown;});
    assert(it!=mock::actors.end());return champion{it->id};
}
}
}
