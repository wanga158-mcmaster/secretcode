#include "../src/micro.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

static unsigned checks{},groups{};
static void check(bool value,const char* expression,int line){
    ++checks;if(!value){std::fprintf(stderr,"observations check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) ::check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)

// The actual adapter below runs against controlled imports. This tests which
// imports it makes, their span accounting and published knowledge. SDK ABI,
// engine permissions and native fuel require the separate real-SDK/native run.
struct Budget {
    unsigned before{},after{},allow{100},fail_after{100};bool in_span{},poisoned{};
    bool before_query(){CHECK(!in_span);++before;if(poisoned||before>allow)return false;in_span=true;return true;}
    bool after_query(){CHECK(in_span);in_span=false;++after;if(after==fail_after){poisoned=true;return false;}return true;}
};
namespace cambc {
enum class handle_champion_t:std::uint32_t{};
enum class handle_camp_t:std::uint32_t{};
enum class handle_camp_group_t:std::uint32_t{};
enum class passive_type:std::uint8_t{none,splash_attack=4,stun_immunity=8};
enum class ability_kind:std::uint32_t{attack_reset=1,aoe_damage=3,shield=7};
enum class error_code:int{api_not_found=94,api_not_in_vision=97};
struct vec2 {int x{},y{};};
struct camp {handle_camp_t value{};operator handle_camp_t()const{return value;}};
namespace map {
struct camp_group {
    handle_camp_group_t value{};operator handle_camp_group_t()const{return value;}
    unsigned gem_value()const{return 26;}unsigned gold_value()const{return 190;}
};
}
struct Self {operator handle_champion_t()const{return handle_champion_t(0);}};
inline Self self(){return {};}
namespace detail {
inline constexpr std::uint32_t ability_target_camps=1,ability_target_enemies=2,ability_target_allies=4,ability_target_self=8,ability_target_ground=16;
struct champion_info {int team{},type{},x{},y{};float health{},max_health{},strength{},focus{};int move_speed{},attack_range{},state{};};
struct buff_data {
    unsigned type{};struct Data {struct Shield {int duration{};float value{};} shield;struct Timer {int duration{};} stun,enhanced_attack;} data;
};
struct champion_design {struct Definition {std::uint32_t passive_type{};} definition;};
struct camp_state {int x{},y{};float health{},max_health{},strength{};};
struct camp_buff_data {unsigned type{};int timer{};float magnitude{};};
struct ability_props {
    unsigned kind{},targets{},cooldown{},attributes{};
    struct Payload {
        unsigned enhanced_attack{};
        struct Value {float range{},modifier{},radius{},duration{};} damage,aoe_damage,cc,heal,dash,shield;
    } payload;
};
inline void check(int code){if(code)throw code;}
// Declarations required by the unchanged archived readers. These paths are
// compiled by real-SDK syntax; this fixture exercises only new adapter methods.
int sdk_champion_restrictions(...);int sdk_champion_channel_time(...);int sdk_champion_gems(...);int sdk_champion_gold(...);
int sdk_camp_state(...);int sdk_camp(...);int sdk_camp_buffs(...);int sdk_camp_group_state(...);int sdk_camp_group_cooldown(...);
int sdk_champion_ability_props(...);int sdk_champion_cooldown(...);int sdk_bc_submitted_gems(...);
enum class Import {body,buffs,design,visibility,camp,champion,teleport};
inline Budget* budget{};inline std::array<Import,32> calls{};inline unsigned count{};
inline champion_info body{};inline std::array<buff_data,3> buffs{};inline unsigned buff_count{};
inline champion_design design{};inline int body_code{},buff_code{},design_code{},visible{1},visibility_code{};
inline std::array<int,3> occupancy{},occupancy_code{};inline std::uint32_t last_id{99};inline int last_type{-1};
inline void touch(Import kind){CHECK(budget&&budget->in_span&&count<calls.size());calls[count++]=kind;}
inline int sdk_champion(std::uint32_t id,champion_info* out){touch(Import::body);last_id=id;*out=body;return body_code;}
inline int sdk_champion_buff_data(std::uint32_t id,buff_data* out,std::uint32_t capacity,std::uint32_t* found){
    touch(Import::buffs);CHECK(id==last_id&&capacity==3);for(unsigned i=0;i<std::min(3u,buff_count);++i)out[i]=buffs[i];*found=buff_count;return buff_code;
}
inline int sdk_champion_design(std::int32_t type,champion_design* out){touch(Import::design);last_type=type;*out=design;return design_code;}
inline int sdk_is_visible(int,int,std::int32_t* out){touch(Import::visibility);*out=visible;return visibility_code;}
inline int sdk_has_camp(int,int,std::int32_t* out){touch(Import::camp);*out=occupancy[0];return occupancy_code[0];}
inline int sdk_has_champion(int,int,std::int32_t* out){touch(Import::champion);*out=occupancy[1];return occupancy_code[1];}
inline int sdk_has_teleport(int,int,std::int32_t* out){touch(Import::teleport);*out=occupancy[2];return occupancy_code[2];}
inline void reset(){
    budget=nullptr;calls={};count=0;body={1,3,0,0,80,120,20,10,1,3,1};buffs={};buff_count=0;design={};
    body_code=buff_code=design_code=visibility_code=0;visible=1;occupancy={};occupancy_code={};last_id=99;last_type=-1;
}
}
}
#define MOSAIC_ADAPTIVE_OBSERVATIONS_EXTERNAL_SDK 1
#include "../src/observations.h"

namespace A=mosaic::adaptive;
namespace G=mosaic::native_service;
namespace D=cambc::detail;
struct Fixture {
    A::Observations observation;std::array<std::uint8_t,9> terrain{};G::SelfFact me{};
    Fixture(){
        D::reset();observation.native_facts()={};observation.epoch=7;observation.native_begin_input();terrain.fill(1);
        CHECK(observation.set_terrain(3,3,terrain));me.epoch=observation.native_facts().epoch;me.id=0;me.pose={1,1};
        me.alive=me.raw_m=me.raw_a=me.raw_c=true;me.body_actual=me.restrictions_actual=me.buffs_actual=me.channel_actual=true;
        me.hp=me.max_hp=100;me.move_speed=me.attack_range=1;observation.native_facts().self=me;
    }
    bool movement(Budget& budget,A::BlockKind& reason,bool& permitted,A::Point to={2,1}){
        D::budget=&budget;const bool complete=observation.movement(budget,me,to,reason,permitted);CHECK(!budget.in_span);return complete;
    }
    bool champion(Budget& budget,A::MicroChampion& out,G::NativeId id=0){
        D::budget=&budget;const bool complete=observation.native_read_champion(budget,id,out);CHECK(!budget.in_span);return complete;
    }
};
static A::MicroChampion sentinel(){A::MicroChampion value{};value.id=99;value.epoch={1,2,3};value.hp=999;value.pose={8,9};value.body_actual=true;return value;}
static void unchanged(const A::MicroChampion& value){CHECK(value.id==99&&value.epoch==G::Epoch{1,2,3}&&value.hp==999&&value.pose==A::Point{8,9}&&value.body_actual);}

static void unseen_landing_is_conditional_and_has_no_hidden_occupancy_queries(){
    ++groups;Fixture f;Budget budget;D::visible=0;D::occupancy={1,1,1};A::BlockKind reason=A::BlockKind::terrain;bool permitted=false;
    CHECK(f.movement(budget,reason,permitted)&&permitted&&reason==A::BlockKind::unknown);
    CHECK(D::count==1&&D::calls[0]==D::Import::visibility&&budget.before==1&&budget.after==1);
    CHECK(f.me.raw_m&&f.observation.native_facts().self.raw_m&&f.observation.native_facts().self.actual(f.me.epoch));
    CHECK(f.observation.native_facts().epoch==f.me.epoch); // No attempt, invalidation or fabricated empty observation.
}
static void current_visible_occupancy_has_typed_refusal_without_a_command(){
    ++groups;
    for(unsigned blocker=0;blocker<4;++blocker){
        Fixture f;Budget budget;if(blocker<3)D::occupancy[blocker]=1;A::BlockKind reason{};bool permitted=true;
        CHECK(f.movement(budget,reason,permitted)&&permitted==(blocker==3));
        const auto expected=blocker==0?A::BlockKind::camp:blocker==1?A::BlockKind::champion:blocker==2?A::BlockKind::teleport:A::BlockKind::unknown;
        CHECK(reason==expected&&D::count==std::min(blocker+2,4u)&&budget.before==D::count&&budget.after==D::count);
        CHECK(f.me.raw_m&&f.observation.native_facts().self.raw_m&&f.observation.native_facts().self.hp==100);
    }
    for(unsigned hidden=0;hidden<3;++hidden){
        Fixture f;Budget budget;D::occupancy_code[hidden]=97;A::BlockKind reason{};bool permitted=false;
        CHECK(f.movement(budget,reason,permitted)&&permitted&&reason==A::BlockKind::unknown);
        CHECK(D::count==hidden+2&&budget.after==D::count); // Lost vision stops occupancy reads, not unseen TRY.
    }
}
static void terrain_and_actual_self_are_required_without_optional_partial_masks(){
    ++groups;
    for(unsigned scenario=0;scenario<5;++scenario){
        Fixture f;Budget budget;A::BlockKind reason{};bool permitted=true;
        if(scenario==0){f.observation.retire_terrain();f.terrain[5]=0;CHECK(f.observation.set_terrain(3,3,f.terrain));}
        if(scenario==1)f.observation.retire_terrain();
        if(scenario==2)f.me.move_restricted=true;
        if(scenario==3)++f.me.epoch.attempt;
        const bool complete=f.movement(budget,reason,permitted,scenario==4?A::Point{3,1}:A::Point{2,1});
        CHECK(complete==(scenario!=1&&scenario!=3)&&!permitted&&D::count==0&&budget.before==0);
        CHECK(reason==((scenario==0||scenario==4)?A::BlockKind::terrain:A::BlockKind::unknown));
    }
}
static void import_denial_preserves_prior_facts_and_failed_span_revokes_them(){
    ++groups;
    for(unsigned allowed=0;allowed<4;++allowed){
        Fixture f;Budget budget;budget.allow=allowed;A::BlockKind reason=A::BlockKind::terrain;bool permitted=true;
        CHECK(!f.movement(budget,reason,permitted)&&!permitted&&reason==A::BlockKind::unknown);
        CHECK(D::count==allowed&&budget.before==allowed+1&&budget.after==allowed&&!budget.poisoned);
        CHECK(f.observation.native_facts().self.actual(f.me.epoch)&&f.observation.native_facts().self.raw_m);
    }
    for(unsigned failed=1;failed<=4;++failed){
        Fixture f;Budget budget;budget.fail_after=failed;A::BlockKind reason{};bool permitted=true;
        CHECK(!f.movement(budget,reason,permitted)&&!permitted&&budget.poisoned);
        CHECK(D::count==failed&&budget.after==failed&&f.observation.native_facts().epoch.round==-1);
        CHECK(!f.observation.native_facts().self.body_actual);
    }
    Fixture f;Budget budget;budget.poisoned=true;A::BlockKind reason{};bool permitted=true;
    CHECK(!f.movement(budget,reason,permitted)&&!permitted&&D::count==0&&f.observation.native_facts().epoch.round==-1);
}
static void visible_champion_zero_id_has_personal_body_buffs_and_queried_passive(){
    ++groups;Fixture f;Budget budget;D::buff_count=2;D::buffs[0].type=0;D::buffs[0].data.shield={0,55};
    D::buffs[1].type=1;D::buffs[1].data.stun.duration=2;D::design.definition.passive_type=unsigned(cambc::passive_type::stun_immunity);
    auto out=sentinel();CHECK(f.champion(budget,out));
    CHECK(D::last_id==0&&D::last_type==3&&D::count==3&&budget.before==3&&budget.after==3);
    CHECK(D::calls[0]==D::Import::body&&D::calls[1]==D::Import::buffs&&D::calls[2]==D::Import::design);
    CHECK(out.id==0&&out.epoch==f.me.epoch&&out.pose==A::Point{0,0}&&out.team==1&&out.hp==80&&out.max_hp==120);
    CHECK(out.alive&&out.visible&&out.visibility_actual&&out.body_actual&&out.buffs_actual&&out.shield==55&&out.stun_time==2&&out.stun_immune);
    CHECK(!out.damage[0].observed&&!out.healing[0].observed&&f.observation.native_facts().self.actual(f.me.epoch));
}
static void hidden_champion_does_not_invent_a_dead_body_or_import_another_perspective(){
    ++groups;
    for(int code:std::array<int,2>{97,94}){
        Fixture f;Budget budget;D::body_code=code;auto out=sentinel();CHECK(f.champion(budget,out));
        CHECK(out.id==0&&out.epoch==f.me.epoch&&!out.visible&&!out.body_actual&&!out.buffs_actual&&!out.alive);
        CHECK(out.visibility_actual==(code==97)&&D::count==1&&budget.after==1);
    }
    Fixture f;Budget budget;D::buff_code=97;auto out=sentinel();CHECK(f.champion(budget,out));
    CHECK(!out.visible&&out.visibility_actual&&!out.body_actual&&!out.buffs_actual&&D::count==2);
}
static void champion_soft_denial_never_overwrites_a_prior_output(){
    ++groups;
    for(unsigned allowed=0;allowed<3;++allowed){
        Fixture f;Budget budget;budget.allow=allowed;auto out=sentinel();CHECK(!f.champion(budget,out));unchanged(out);
        CHECK(D::count==allowed&&budget.after==allowed&&!budget.poisoned&&f.observation.native_facts().self.actual(f.me.epoch));
    }
    Fixture f;Budget budget;budget.fail_after=2;auto out=sentinel();CHECK(!f.champion(budget,out));unchanged(out);
    CHECK(budget.poisoned&&D::count==2&&f.observation.native_facts().epoch.round==-1);
}
int main(){
    unseen_landing_is_conditional_and_has_no_hidden_occupancy_queries();
    current_visible_occupancy_has_typed_refusal_without_a_command();
    terrain_and_actual_self_are_required_without_optional_partial_masks();
    import_denial_preserves_prior_facts_and_failed_span_revokes_them();
    visible_champion_zero_id_has_personal_body_buffs_and_queried_passive();
    hidden_champion_does_not_invent_a_dead_body_or_import_another_perspective();
    champion_soft_denial_never_overwrites_a_prior_output();
    std::printf("PASS observations: %u checks, %u groups\n",checks,groups);
    return 0;
}
