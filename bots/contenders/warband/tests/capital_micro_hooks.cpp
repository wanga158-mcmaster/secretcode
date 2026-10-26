#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>

// Minimal host-free SDK-shaped surface. The production entrypoints below are
// instantiated unchanged; every fake physical call records its actual arguments.
namespace cambc {
struct vec2 {
    int x{},y{};
    int chebyshev_distance(vec2 p)const{return std::max(std::abs(x-p.x),std::abs(y-p.y));}
    friend bool operator==(vec2,vec2)=default;
};
enum class handle_champion_t:int {};enum class handle_camp_t:int {};
enum class team {ally,enemy};
enum class champion_state {alive,recalling,submitting_gems,teleporting};
enum class buff_restrictions {attack,cast};
enum class SpellKind {None,Damage,Aoe,Cc,Shield,Reset,Heal,Dash};
struct Unit {vec2 pos{};float hp{1000},max_hp{1000};bool visible{true},alive{true};};
struct Spell {SpellKind kind{SpellKind::None};bool ground{},exclusive{},accepted{true};int range{4};float amount{100};};
struct Physical {
    bool cast{};int target{-1},slot{-1};vec2 destination{-1,-1};bool accepted{true};
    friend bool operator==(const Physical&,const Physical&)=default;
};
struct Mock {
    unsigned round{40},fuel{100000},queries{};
    unsigned raw_attacks{},raw_casts{},raw_moves{},wrapper_calls{},intervening_imports{},last_target_kind{};
    int raw_error{};bool next_import_is_command{};
    bool can_move{true},can_attack{true},can_cast{true},stunned{},recalling{},submitting{},teleporting{},
        restrict_attack{},restrict_cast{},throw_attack{},throw_cast{};
    float strength{55},shield{};int attack_range{1};
    std::array<Unit,3> champions{};Unit monster{};std::array<Spell,2> spells{};std::vector<Physical> calls;
};
inline Mock mock;
inline void query(){++mock.queries;if(mock.next_import_is_command)++mock.intervening_imports;}
inline unsigned round_number(){query();return mock.round;}
inline unsigned fuel_remaining(){query();return mock.fuel;}
struct State {
    bool contains(champion_state flag)const {
        query();switch(flag){case champion_state::alive:return mock.champions[0].alive;
        case champion_state::recalling:return mock.recalling;case champion_state::submitting_gems:return mock.submitting;
        case champion_state::teleporting:return mock.teleporting;}return false;
    }
};
struct Restrictions {bool contains(buff_restrictions flag)const{query();return flag==buff_restrictions::attack?mock.restrict_attack:mock.restrict_cast;}};
struct buff_shield {float value{};};
struct Buffs {
    std::vector<std::variant<buff_shield>> values;
    bool has_stun()const{query();return mock.stunned;}
    auto begin()const{return values.begin();}auto end()const{return values.end();}
};
struct bitboard {
    bool mask{};vec2 center{};int range{};std::vector<vec2> points;
    bool is_set(vec2 p)const{query();return (mask&&center.chebyshev_distance(p)<=range)||std::find(points.begin(),points.end(),p)!=points.end();}
    void set(vec2 p,bool value){if(value)points.push_back(p);else points.erase(std::remove(points.begin(),points.end(),p),points.end());}
    bitboard reachable_tiles(vec2 origin,std::uint16_t radius)const {
        query();bitboard result;
        for(int y=origin.y-radius;y<=origin.y+radius;++y)for(int x=origin.x-radius;x<=origin.x+radius;++x)
            if(x>=0&&y>=0&&x<20&&y<20&&!is_set({x,y}))result.points.push_back({x,y});
        return result;
    }
    std::vector<vec2> positions()const{query();return points;}
};
inline bitboard operator|(bitboard a,const bitboard& b){query();a.points.insert(a.points.end(),b.points.begin(),b.points.end());return a;}
struct ability_dash {bool exclusive_with_movement{};int range{};};
struct ability_other {bool exclusive_with_movement{};};
class champion;class camp;
class ability {
    unsigned index_{};
    const Spell& value()const{return mock.spells[index_];}
    bool use_actual(int target,vec2 destination)const;
public:
    explicit ability(unsigned index=0):index_(index){}
    unsigned slot()const{query();return index_;}
    bool is_ready()const{query();return value().kind!=SpellKind::None&&mock.can_cast;}
    std::optional<std::variant<ability_other,ability_dash>> info()const {
        query();if(value().kind==SpellKind::Dash)return std::variant<ability_other,ability_dash>{ability_dash{value().exclusive,value().range}};
        return std::variant<ability_other,ability_dash>{ability_other{value().exclusive}};
    }
    bool can_target_camps()const{query();return is_damage()||is_cc()||is_aoe_damage();}
    bool can_target_enemies()const{return can_target_camps();}
    bool can_target_ground()const{query();return value().ground||is_dash();}
    bool can_target_self()const{query();return is_shield()||is_attack_reset();}
    bool can_target_allies()const{query();return is_heal();}
    bool is_damage()const{query();return value().kind==SpellKind::Damage;}
    bool is_cc()const{query();return value().kind==SpellKind::Cc;}
    bool is_aoe_damage()const{query();return value().kind==SpellKind::Aoe;}
    bool is_shield()const{query();return value().kind==SpellKind::Shield;}
    bool is_attack_reset()const{query();return value().kind==SpellKind::Reset;}
    bool is_heal()const{query();return value().kind==SpellKind::Heal;}
    bool is_dash()const{query();return value().kind==SpellKind::Dash;}
    float heal_amount()const{query();return value().amount;}
    bitboard valid_tiles()const{query();bitboard b;b.mask=true;b.center=mock.champions[0].pos;b.range=value().range;return b;}
    template<class T>T get()const{query();static_assert(std::is_same_v<T,ability_dash>);return {value().exclusive,value().range};}
    bool use()const{return use_actual(-1,{-1,-1});}
    bool use(vec2 p)const{return use_actual(-1,p);}
    bool use(const champion&)const;bool use(const camp&)const;
};
class camp {
public:
    explicit camp(int=10){}
    explicit operator handle_camp_t()const{return static_cast<handle_camp_t>(10);}
    bool is_visible()const{query();return mock.monster.visible;}
    bool is_alive()const{query();return mock.monster.alive&&mock.monster.hp>0;}
    float health()const{query();return mock.monster.hp;}
    vec2 position()const{query();return mock.monster.pos;}
};
inline void perform_attack(int target){
    mock.calls.push_back({false,target,-1,{-1,-1},true});if(mock.throw_attack)throw std::runtime_error("attack boundary");
    mock.can_attack=false;if(mock.attack_range>1)mock.can_move=false;
    if(target==10)mock.monster.hp-=mock.strength;else mock.champions[target].hp-=mock.strength;
}
class champion {
    int id_{};
public:
    explicit champion(int id=0):id_(id){}
    explicit operator handle_champion_t()const{return static_cast<handle_champion_t>(id_);}
    bool is_visible()const{query();return mock.champions[id_].visible;}
    bool is_alive()const{query();return mock.champions[id_].alive&&mock.champions[id_].hp>0;}
    float health()const{query();return mock.champions[id_].hp;}
    float max_health()const{query();return mock.champions[id_].max_hp;}
    float strength()const{query();return mock.strength;}
    vec2 position()const{query();return mock.champions[id_].pos;}
    int attack_range()const{query();return mock.attack_range;}
    State state()const{query();return {};}
    bool can_attack()const{query();return mock.can_attack;}
    bool can_move()const{query();return mock.can_move;}
    Restrictions restrictions()const{query();return {};}
    Buffs buffs()const{query();Buffs b;if(mock.shield)b.values.push_back(buff_shield{mock.shield});return b;}
    std::array<ability,2> abilities()const{query();return {ability{0},ability{1}};}
    void attack(const camp&)const {
        query();++mock.wrapper_calls;perform_attack(10);
    }
    void attack(const champion& target)const {
        query();++mock.wrapper_calls;perform_attack(target.id_);
    }
};
inline champion& self(){static champion owner;return owner;}
inline bool ability::use(const champion& target)const{return use_actual(static_cast<int>(static_cast<handle_champion_t>(target)),{-1,-1});}
inline bool ability::use(const camp& target)const{return use_actual(static_cast<int>(static_cast<handle_camp_t>(target)),{-1,-1});}
inline bool perform_cast(unsigned slot,int target,vec2 destination) {
    const auto spell=mock.spells[slot];mock.calls.push_back({true,target,static_cast<int>(slot),destination,spell.accepted});
    if(mock.throw_cast)throw std::runtime_error("cast boundary");if(!spell.accepted)return false;
    mock.can_cast=false;if(spell.exclusive)mock.can_move=false;
    if(spell.kind==SpellKind::Reset)mock.can_attack=true;
    else if(spell.kind==SpellKind::Shield)mock.shield=spell.amount;
    else if(spell.kind==SpellKind::Heal&&target>=0)mock.champions[target].hp=std::min(mock.champions[target].max_hp,mock.champions[target].hp+spell.amount);
    else if(spell.kind==SpellKind::Dash)mock.champions[0].pos=destination;
    else if(target==10||destination==mock.monster.pos)mock.monster.hp-=spell.amount;
    else if(target>=0)mock.champions[target].hp-=spell.amount;
    return true;
}
inline bool ability::use_actual(int target,vec2 destination)const{query();++mock.wrapper_calls;return perform_cast(index_,target,destination);}
namespace detail {
enum class entity_kind:std::uint32_t {champion=1,camp=2};
inline void check(std::int32_t code){if(code)throw std::runtime_error("checked SDK error");}
inline std::int32_t sdk_attack(std::uint32_t kind,std::uint32_t target){
    mock.next_import_is_command=false;++mock.raw_attacks;mock.last_target_kind=kind;
    if(mock.raw_error)return mock.raw_error;perform_attack(static_cast<int>(target));return 0;
}
inline std::int32_t sdk_cast(std::uint32_t slot,std::uint32_t kind,std::uint32_t target,std::int32_t x,std::int32_t y,std::int32_t* accepted){
    mock.next_import_is_command=false;++mock.raw_casts;mock.last_target_kind=kind;
    if(mock.raw_error)return mock.raw_error;*accepted=perform_cast(slot,kind?static_cast<int>(target):-1,{x,y});return 0;
}
inline std::int32_t sdk_move_to(std::int32_t x,std::int32_t y,std::int32_t* accepted){
    mock.next_import_is_command=false;++mock.raw_moves;if(mock.raw_error)return mock.raw_error;
    mock.champions[0].pos={x,y};mock.can_move=false;*accepted=1;return 0;
}
}
namespace vision {
inline std::vector<champion> champions(team side){query();return side==team::ally?std::vector<champion>{champion{0},champion{2}}:std::vector<champion>{champion{1}};}
inline bitboard champion_tiles(){query();bitboard b;for(const auto& u:mock.champions)if(u.alive)b.points.push_back(u.pos);return b;}
inline bitboard camp_tiles(){query();bitboard b;if(mock.monster.alive)b.points.push_back(mock.monster.pos);return b;}
inline bitboard teleport_tiles(){query();return {};}
}
namespace map {
inline bitboard walls(){query();return {};}inline bitboard altars(){query();return {};}
inline bitboard spawn_region(team){query();return {};}inline bitboard holes(){query();return {};}
}
}

#define WB_CAPITAL_MICRO_HOST_FREE
#include "../src/capital_micro.h"
namespace legacy=wb::capital_micro;
namespace m=wb::capital_micro::guarded;
static_assert(std::is_same_v<decltype(&legacy::support),legacy::Result (*)(legacy::Context)>);
static_assert(std::is_same_v<decltype(&legacy::gap_dash),bool (*)(cambc::vec2)>);
static_assert(std::is_same_v<decltype(&legacy::engage<cambc::camp>),legacy::Result (*)(const cambc::camp&,legacy::Context)>);
static_assert(std::is_same_v<decltype(&legacy::detail::basic<cambc::camp>),bool (*)(const cambc::camp&,unsigned)>);
static_assert(std::is_same_v<legacy::Hooks,m::Hooks> && std::is_same_v<legacy::Attempt,m::Attempt>);
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;if(failures<=20)std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
void reset(){
    cambc::mock={};auto& s=cambc::mock;s.champions[0].pos={4,4};s.champions[1].pos={5,4};
    s.champions[2]={{4,5},100,500,true,true};s.monster.pos={5,4};
}
struct Audit {
    unsigned before{},after{},deny_at{},invalidate_after{},expire_before{},expire_after{},fuel_before{};
    bool valid{true},pending{};std::vector<m::Attempt> events;std::vector<bool> accepted;
};
bool before(void* user,const m::Attempt& a){
    auto& c=*static_cast<Audit*>(user);++c.before;c.events.push_back(a);
    if(!c.valid||c.before==c.deny_at)return false;
    if(c.before==c.expire_before)++cambc::mock.round;if(c.before==c.fuel_before)cambc::mock.fuel=8999;
    // Real caller's before(TRUE) owns the FINAL fence and pending authority.
    if(cambc::mock.round!=a.round||cambc::mock.fuel<9000)return false;
    c.pending=true;cambc::mock.next_import_is_command=true;return true;
}
void after(void* user,const m::Attempt& a,bool accepted){
    auto& c=*static_cast<Audit*>(user);++c.after;c.events.push_back(a);c.accepted.push_back(accepted);c.pending=false;
    if(c.after==c.invalidate_after)c.valid=false;if(c.after==c.expire_after)++cambc::mock.round;
}
m::Hooks hooks(Audit& a){return {&a,before,after};}
void same_arguments(const m::Attempt& a,const cambc::Physical& p){
    CHECK(a.kind==(p.cast?m::Primitive::Cast:m::Primitive::Basic));
    CHECK(a.round==40&&a.target==p.target&&a.slot==p.slot&&a.destination==p.destination);
}
void null_and_elided_equivalence(){
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Reset;
    const auto old_result=legacy::engage(cambc::camp{});const auto old_calls=cambc::mock.calls;const auto old_queries=cambc::mock.queries;
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Reset;
    const auto baseline=m::engage<false>(cambc::camp{});const auto calls=cambc::mock.calls;const auto queries=cambc::mock.queries;
    CHECK(baseline.basics==2&&baseline.cast&&calls.size()==3);
    CHECK(baseline.basics==old_result.basics&&baseline.cast==old_result.cast&&calls==old_calls&&queries==old_queries);
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Reset;
    const auto ordinary=m::engage(cambc::camp{});
    CHECK(ordinary.basics==baseline.basics&&ordinary.cast==baseline.cast&&cambc::mock.calls==calls&&cambc::mock.queries==queries);
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Reset;Audit a;a.deny_at=1;auto h=hooks(a);
    const auto elided=m::engage<false>(cambc::camp{}, {},&h);
    CHECK(elided.basics==2&&elided.cast&&a.before==0&&a.after==0&&cambc::mock.calls==calls&&cambc::mock.queries==queries);
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Reset;m::Hooks empty;
    const auto empty_result=m::engage(cambc::camp{}, {},&empty);
    CHECK(empty_result.basics==2&&empty_result.cast&&cambc::mock.calls==calls&&cambc::mock.queries==queries);
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Reset;Audit observer;m::Hooks observer_only{&observer,nullptr,after};
    const auto observed=m::engage(cambc::camp{}, {},&observer_only);
    CHECK(observed.basics==2&&observed.cast&&observer.before==0&&observer.after==3&&cambc::mock.calls==calls);
    CHECK(cambc::mock.wrapper_calls==3&&cambc::mock.raw_attacks==0&&cambc::mock.raw_casts==0);
    for(std::size_t i=0;i<calls.size();++i)same_arguments(observer.events[i],calls[i]);
    static_assert(sizeof(m::Result)==8);
}
void basic_reset_and_stop(){
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Reset;Audit a;auto h=hooks(a);
    const auto result=m::engage(cambc::camp{}, {},&h);
    CHECK(result.basics==2&&result.cast&&a.before==3&&a.after==3&&!a.pending);
    CHECK(cambc::mock.raw_attacks==2&&cambc::mock.raw_casts==1&&cambc::mock.wrapper_calls==0&&cambc::mock.intervening_imports==0);
    for(std::size_t i=0;i<cambc::mock.calls.size();++i){same_arguments(a.events[i*2],cambc::mock.calls[i]);same_arguments(a.events[i*2+1],cambc::mock.calls[i]);}
    for(unsigned stop=1;stop<=3;++stop){
        reset();cambc::mock.spells[0].kind=cambc::SpellKind::Reset;Audit denied;denied.deny_at=stop;auto gate=hooks(denied);
        const auto prefix=m::engage(cambc::camp{}, {},&gate);
        CHECK(cambc::mock.calls.size()==stop-1&&denied.after==stop-1&&denied.before==stop);
        CHECK(prefix.basics==(stop==1?0u:1u)&&prefix.cast==(stop==3));
        if(stop==2)CHECK(cambc::mock.can_move&&!cambc::mock.can_attack&&cambc::mock.can_cast);
        if(stop==3)CHECK(cambc::mock.can_move&&cambc::mock.can_attack&&!cambc::mock.can_cast);
    }
    for(unsigned stop=1;stop<=2;++stop){
        reset();cambc::mock.spells[0].kind=cambc::SpellKind::Reset;Audit invalid;invalid.invalidate_after=stop;auto gate=hooks(invalid);
        const auto prefix=m::engage(cambc::camp{}, {},&gate);
        CHECK(cambc::mock.calls.size()==stop&&invalid.after==stop&&invalid.before==stop+1);
        CHECK(prefix.basics==1&&prefix.cast==(stop==2));
    }
}
void every_cast_site_and_refusal(){
    for(bool ground:{false,true})for(bool accepted:{false,true}){
        reset();auto& spell=cambc::mock.spells[0];spell.kind=cambc::SpellKind::Damage;spell.ground=ground;spell.accepted=accepted;
        Audit a;a.invalidate_after=1;auto h=hooks(a);const auto result=m::engage(cambc::camp{}, {},&h);
        CHECK(cambc::mock.calls.size()==1&&a.after==1&&a.accepted[0]==accepted&&result.cast==accepted&&result.basics==0);
        CHECK(cambc::mock.raw_casts==1&&cambc::mock.wrapper_calls==0&&cambc::mock.intervening_imports==0);
        if(!accepted)CHECK(cambc::mock.can_move&&cambc::mock.can_attack&&cambc::mock.can_cast);
        same_arguments(a.events[0],cambc::mock.calls[0]);same_arguments(a.events[1],cambc::mock.calls[0]);
    }
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Damage;Audit denied;denied.deny_at=1;auto h=hooks(denied);
    CHECK(!m::engage(cambc::camp{}, {2000,2},&h).acted()&&cambc::mock.calls.empty()&&denied.after==0&&denied.before==1);
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Shield;Audit shield;shield.invalidate_after=1;h=hooks(shield);
    const auto protected_prefix=m::engage(cambc::camp{}, {2000,2},&h);
    CHECK(protected_prefix.cast&&protected_prefix.basics==0&&cambc::mock.calls.size()==1&&shield.after==1);
    same_arguments(shield.events[0],cambc::mock.calls[0]);
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Heal;cambc::mock.spells[0].amount=300;Audit heal;h=hooks(heal);
    const auto supported=m::support({},&h);
    CHECK(supported.supported&&supported.cast&&heal.before==1&&heal.after==1&&cambc::mock.calls.size()==1);
    CHECK(heal.events[0].target==2);same_arguments(heal.events[0],cambc::mock.calls[0]);
    reset();cambc::mock.spells[1].kind=cambc::SpellKind::Dash;cambc::mock.spells[1].range=2;Audit dash;h=hooks(dash);
    CHECK(m::gap_dash({8,4},&h)&&dash.before==1&&dash.after==1&&cambc::mock.calls.size()==1);
    CHECK(dash.events[0].slot==1&&dash.events[0].target==-1);same_arguments(dash.events[0],cambc::mock.calls[0]);
}
void other_refusals_and_null_entrypoints(){
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Shield;cambc::mock.spells[0].accepted=false;
    Audit shield;shield.invalidate_after=1;auto h=hooks(shield);
    const auto shielded=m::engage(cambc::camp{}, {2000,2},&h);
    CHECK(!shielded.acted()&&shield.after==1&&!shield.accepted[0]&&cambc::mock.calls.size()==1);
    CHECK(cambc::mock.can_move&&cambc::mock.can_attack&&cambc::mock.can_cast);
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Reset;cambc::mock.spells[0].accepted=false;
    Audit reset_hook;h=hooks(reset_hook);const auto reset_result=m::engage(cambc::camp{}, {},&h);
    CHECK(reset_result.basics==1&&!reset_result.cast&&reset_hook.after==2&&!reset_hook.accepted[1]&&cambc::mock.calls.size()==2);
    CHECK(cambc::mock.can_move&&!cambc::mock.can_attack&&cambc::mock.can_cast);
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Heal;cambc::mock.spells[0].amount=300;cambc::mock.spells[0].accepted=false;
    Audit heal;h=hooks(heal);CHECK(!m::support({},&h).acted()&&heal.after==1&&!heal.accepted[0]&&cambc::mock.calls.size()==1);
    CHECK(cambc::mock.champions[2].hp==100&&cambc::mock.can_cast);
    reset();cambc::mock.spells[1].kind=cambc::SpellKind::Dash;cambc::mock.spells[1].range=2;cambc::mock.spells[1].accepted=false;
    Audit dash;h=hooks(dash);CHECK(!m::gap_dash({8,4},&h)&&dash.after==1&&!dash.accepted[0]&&cambc::mock.calls.size()==1);
    CHECK((cambc::mock.champions[0].pos==cambc::vec2{4,4})&&cambc::mock.can_move&&cambc::mock.can_cast);
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Heal;cambc::mock.spells[0].amount=300;
    const auto support_result=m::support<false>();const auto support_calls=cambc::mock.calls;const auto support_queries=cambc::mock.queries;
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Heal;cambc::mock.spells[0].amount=300;
    CHECK(m::support().supported==support_result.supported&&cambc::mock.calls==support_calls&&cambc::mock.queries==support_queries);
    reset();cambc::mock.spells[1].kind=cambc::SpellKind::Dash;cambc::mock.spells[1].range=2;
    const bool dashed=m::gap_dash<false>({8,4});const auto dash_calls=cambc::mock.calls;const auto dash_queries=cambc::mock.queries;
    reset();cambc::mock.spells[1].kind=cambc::SpellKind::Dash;cambc::mock.spells[1].range=2;
    CHECK(m::gap_dash({8,4})==dashed&&cambc::mock.calls==dash_calls&&cambc::mock.queries==dash_queries);
}
void epoch_fuel_and_exceptions(){
    for(bool fuel:{false,true}){
        reset();cambc::mock.spells[0].kind=cambc::SpellKind::Damage;Audit a;
        if(fuel)a.fuel_before=1;else a.expire_before=1;auto h=hooks(a);
        CHECK(!m::engage(cambc::camp{}, {},&h).acted()&&cambc::mock.calls.empty()&&a.before==1&&a.after==0&&!a.pending);
    }
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Reset;Audit a;a.expire_after=1;auto h=hooks(a);
    const auto prefix=m::engage(cambc::camp{}, {},&h);
    CHECK(prefix.basics==1&&!prefix.cast&&cambc::mock.calls.size()==1&&a.after==1);
    reset();cambc::mock.throw_attack=true;Audit error;h=hooks(error);bool caught=false;
    try{m::engage(cambc::camp{}, {},&h);}catch(const std::runtime_error&){caught=true;}
    CHECK(caught&&error.before==1&&error.after==0&&error.pending&&cambc::mock.calls.size()==1);
    for(bool cast:{false,true}){
        reset();if(cast)cambc::mock.spells[0].kind=cambc::SpellKind::Damage;cambc::mock.raw_error=77;
        Audit raw_error;auto gate=hooks(raw_error);caught=false;
        try{m::engage(cambc::camp{}, {},&gate);}catch(const std::runtime_error&){caught=true;}
        CHECK(caught&&raw_error.before==1&&raw_error.after==0&&raw_error.pending&&cambc::mock.calls.empty());
        CHECK(cambc::mock.raw_attacks+cambc::mock.raw_casts==1&&cambc::mock.wrapper_calls==0&&cambc::mock.intervening_imports==0);
    }
}
void standalone_detail_and_champion(){
    reset();Audit a;a.deny_at=1;auto h=hooks(a);
    CHECK(!m::detail::basic(cambc::camp{},40,&h)&&cambc::mock.calls.empty());
    reset();cambc::mock.can_attack=false;cambc::mock.spells[1].kind=cambc::SpellKind::Reset;Audit reset_hook;h=hooks(reset_hook);
    CHECK(m::detail::reset(cambc::camp{},40,&h)&&reset_hook.after==1);same_arguments(reset_hook.events[0],cambc::mock.calls[0]);
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Shield;Audit shield;h=hooks(shield);
    CHECK(m::detail::shield_for_work(cambc::camp{}, {2000,2},40,&h)&&shield.after==1);
    reset();cambc::mock.spells[0].kind=cambc::SpellKind::Damage;Audit spell;h=hooks(spell);
    CHECK(m::detail::spell(cambc::champion{1},40,&h)&&spell.events[0].target==1&&spell.after==1);
    reset();Audit champion;h=hooks(champion);
    CHECK(m::engage(cambc::champion{1},{},&h).basics==1&&champion.events[0].target==1&&champion.after==1);
}
}
int main(){
    null_and_elided_equivalence();basic_reset_and_stop();every_cast_site_and_refusal();other_refusals_and_null_entrypoints();
    epoch_fuel_and_exceptions();standalone_detail_and_champion();
    std::printf("capital micro hooks: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
