#include "../src/native_service_cycle.h"
#include "../src/native_service_group_frontier.h"
#include "../src/native_service_fields.h"
#include "../src/native_service_execution.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>

namespace N=mosaic::native_service;
namespace native_service=mosaic::native_service;
static unsigned checks{},groups_count{};
static void check(bool value,const char* expression,int line){
    ++checks;if(!value){std::fprintf(stderr,"group runtime check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)

// Include the actual production Runtime fragment. These are cached immutable
// catalogue/layout records, not SDK mocks. Only the already-supplied facts are
// actual within the pure test. The Budget clock intentionally charges nothing:
// this proves neither native fuel safety nor any physical command permission.
template<std::size_t RootCap=256> struct MockRuntime {
    using handle_camp_group_t=std::uint32_t;
    struct CachedGroup {
        std::uint32_t id{};int respawn{};
        constexpr operator std::uint32_t()const noexcept{return id;}
        constexpr int max_respawn_timer()const noexcept{return respawn;}
    };
    struct CachedRange {std::size_t begin{},end{};};
    struct CachedDimensions {int w{40},h{16};int width()const{return w;}int height()const{return h;}} dimensions;
    struct State {
        N::CycleState cycle{};
        N::CycleGroupWork<64> group_work{};
        N::GroupFrontier<640,RootCap> frontier{};
        N::ServiceFields<640> fields{};
        std::array<N::FrontierMember,64> frontier_members{};
        std::array<std::uint8_t,640> passable{};
        int group{0},approach_range{1};
        std::uint64_t group_generation{9},board_generation{13},goal_key{77},nav_intent{},field_revision{};
        bool group_goal{true},board_ready{true};std::size_t board_cursor{640};
        unsigned board_edit_count{},unsupported{},prepare_units{};
    } state;
    std::array<CachedGroup,2> groups{{{37,60},{99,70}}};
    std::array<CachedRange,2> memory{{{30,60},{0,1}}};
    CachedDimensions* walls{&dimensions};
    N::Facts facts{};int epoch{10};std::uint64_t remaining{1000000};
    State& native_service_state(){return state;}
    const State& native_service_state()const{return state;}
    N::Facts& native_facts(){return facts;}
    const N::Facts& native_facts()const{return facts;}
    static std::uint64_t clock(void* object){return static_cast<MockRuntime*>(object)->remaining;}
    N::Budget budget(){N::Budget value{this,clock,remaining};value.pure_bound=20000;return value;}
    void set_epoch(N::Epoch now){
        epoch=now.round;facts.epoch=now;facts.self={};facts.self.epoch=now;facts.self.pose={4,12};
        facts.self.alive=facts.self.raw_m=facts.self.raw_a=facts.self.raw_c=true;
        facts.self.body_actual=facts.self.restrictions_actual=facts.self.buffs_actual=facts.self.channel_actual=true;
    }
    explicit MockRuntime(bool dead_opening=true){
        set_epoch({10,1,0});
        // One legal contiguous group occupies x5..34 on row12. Its distant
        // ordinal30 is x34; near ordinal57 is x6; dead ordinal59 is x5. Other
        // possible members x7..33 block the intervening corridor. Dead59's
        // OPEN byte is a separate caller-supplied board fact, not inferred by
        // native_group_observe or manufactured by GroupFrontier.
        for(int x=0;x<40;++x)state.passable[12*40+std::size_t(x)]=1;
        for(int x=6;x<=34;++x)state.passable[12*40+std::size_t(x)]=0;
        state.passable[12*40+5]=std::uint8_t(dead_opening);
        for(int ordinal=31;ordinal<=56;++ordinal)state.frontier_members[std::size_t(ordinal)]={{ordinal-24,12},ordinal};
        state.frontier_members[30]={{34,12},30};state.frontier_members[57]={{6,12},57};
        state.frontier_members[58]={{33,12},58};state.frontier_members[59]={{5,12},59};
    }
#include "../src/native_service_group_runtime.h"
};

template<std::size_t Roots> static N::CampFact member(const MockRuntime<Roots>& runtime,int ordinal,N::Life life=N::Life::asleep,float hp=120){
    N::CampFact out{};out.epoch=runtime.facts.epoch;out.id=std::uint32_t(ordinal);
    out.group_id=runtime.groups[0].id;out.group_ordinal=0;out.member_ordinal=ordinal;
    out.pose=runtime.state.frontier_members[std::size_t(ordinal)].pose;out.life=life;out.life_actual=true;
    out.hp=out.max_hp=life==N::Life::dead?0:hp;out.body_actual=out.buffs_actual=life!=N::Life::dead;return out;
}
template<std::size_t Roots> static N::GroupFact group(const MockRuntime<Roots>& runtime,N::GroupLife life=N::GroupLife::alive){
    N::GroupFact out{};out.epoch=runtime.facts.epoch;out.id=runtime.groups[0].id;out.ordinal=0;out.life=life;out.actual=true;return out;
}
static bool contains(std::span<const N::Point> points,N::Point target){for(const auto p:points)if(p==target)return true;return false;}
template<std::size_t Roots> static void ready(MockRuntime<Roots>& runtime){
    auto budget=runtime.budget();
    for(unsigned n=0;n<4096;++n){
        runtime.native_prepare_group_frontier(budget);
        CHECK(!budget.poisoned);
        if(runtime.state.frontier.status()==N::FrontierStatus::ready&&
           runtime.state.fields.status(N::ServiceField::service)==N::NavigationStatus::ready){
            CHECK(runtime.native_service_field_matches());return;
        }
    }
    CHECK(false);
}

static void matching_alive_and_reward_only_grant_finite_absence(){
    ++groups_count;
    for(const auto life:std::array{N::GroupLife::alive,N::GroupLife::reward}){
        MockRuntime<> runtime;CHECK(runtime.native_group_configure());
        const auto dead=member(runtime,59,N::Life::dead);const auto known=group(runtime,life);
        const auto absence=runtime.native_group_absence(dead,known);
        CHECK(absence.actual&&absence.group_generation==9&&absence.absence_until==70);
        const auto before=runtime.state.group_work.route_revision();runtime.native_group_observe(dead,known);
        CHECK(!runtime.native_arrival_member_needed(59,runtime.facts.epoch));
        CHECK(runtime.native_arrival_member_needed(57,runtime.facts.epoch));
        CHECK(runtime.state.group_work.route_revision()>before);
        CHECK(runtime.state.group_work.earliest_absence_expiry()==70);
        CHECK(runtime.native_arrival_member_needed(59,{70,1,0})); // Exclusive finite boundary, even before refresh.
    }
    MockRuntime<> runtime;runtime.groups[0].respawn=0;
    CHECK(runtime.native_group_absence(member(runtime,59,N::Life::dead),group(runtime)).absence_until==11);
    runtime.groups[0].respawn=60;runtime.set_epoch({std::numeric_limits<int>::max()-2,1,0});
    CHECK(runtime.native_group_absence(member(runtime,59,N::Life::dead),group(runtime)).absence_until==std::numeric_limits<int>::max());
}

static void stale_foreign_and_unqualified_dead_stay_possible(){
    ++groups_count;
    for(unsigned variant=0;variant<10;++variant){
        MockRuntime<> runtime;CHECK(runtime.native_group_configure());
        auto dead=member(runtime,59,N::Life::dead);auto known=group(runtime);
        switch(variant){
        case 0:known.epoch.round=9;break;
        case 1:known.epoch.revision=0;break;
        case 2:known.id=99;break;
        case 3:known.ordinal=1;break;
        case 4:known.life=N::GroupLife::respawning;break;
        case 5:known.actual=false;break;
        case 6:dead.life_actual=false;break;
        case 7:dead.group_id=99;break;
        case 8:dead.epoch.round=9;break;
        case 9:dead.epoch.revision=0;break;
        }
        CHECK(!runtime.native_group_absence(dead,known).actual);
        runtime.native_group_observe(dead,known);
        CHECK(runtime.native_arrival_member_needed(59,runtime.facts.epoch));
    }
    MockRuntime<> runtime;runtime.native_group_observe(member(runtime,59,N::Life::dead));
    CHECK(runtime.native_arrival_member_needed(59,runtime.facts.epoch));
    CHECK(runtime.state.group_work.remaining(runtime.facts.epoch).observed_dead==1);
    CHECK(runtime.state.group_work.remaining(runtime.facts.epoch).excluded_dead==0);
}

static void stable_dead_proof_and_hp_refresh_do_not_reset_geometry(){
    ++groups_count;MockRuntime<> runtime;
    runtime.native_group_observe(member(runtime,59,N::Life::dead),group(runtime));ready(runtime);
    const auto route_revision=runtime.state.group_work.route_revision();
    const auto frontier_generation=runtime.state.frontier.generation();
    const auto field_generation=runtime.state.fields.generation(N::ServiceField::service);
    const auto key=runtime.native_group_field_key();
    runtime.set_epoch({12,2,0});runtime.native_group_observe(member(runtime,59,N::Life::dead),group(runtime));
    CHECK(runtime.state.group_work.route_revision()==route_revision);
    CHECK(runtime.state.frontier.generation()==frontier_generation&&runtime.native_group_field_key()==key);
    CHECK(runtime.state.fields.generation(N::ServiceField::service)==field_generation);
    CHECK(runtime.state.group_work.earliest_absence_expiry()==70); // Renewal keeps the earlier conservative alarm.
    runtime.native_group_observe(member(runtime,57,N::Life::asleep,120),group(runtime));
    runtime.set_epoch({13,3,0});runtime.native_group_observe(member(runtime,57,N::Life::awake,75),group(runtime));
    CHECK(runtime.state.group_work.route_revision()==route_revision);
    CHECK(runtime.state.frontier.generation()==frontier_generation);
    CHECK(runtime.state.fields.generation(N::ServiceField::service)==field_generation);
    CHECK(runtime.state.group_work.damage_events()==0&&runtime.state.group_work.death_events()==0);
    auto budget=runtime.budget();runtime.native_prepare_group_frontier(budget);
    CHECK(runtime.native_group_field_key()==key&&runtime.native_service_field_matches());
}

static void expiry_restores_possible_work_and_retires_only_group_service(){
    ++groups_count;MockRuntime<> runtime;
    runtime.native_group_observe(member(runtime,59,N::Life::dead),group(runtime));ready(runtime);
    const std::array<N::Point,1> egress{{{0,12}}};
    CHECK(runtime.state.fields.begin(N::ServiceField::egress,40,16,runtime.state.passable,egress,88));
    const auto egress_generation=runtime.state.fields.generation(N::ServiceField::egress);
    const auto old_key=runtime.native_group_field_key(),old_revision=runtime.state.group_work.route_revision();
    runtime.set_epoch({69,1,0});runtime.native_group_clock(runtime.facts.epoch);
    CHECK(runtime.native_group_field_key()==old_key&&runtime.native_service_field_matches());
    runtime.set_epoch({70,1,0});runtime.native_group_clock(runtime.facts.epoch);
    CHECK(runtime.native_arrival_member_needed(59,runtime.facts.epoch));
    CHECK(runtime.state.group_work.route_revision()>old_revision&&runtime.state.frontier.status()==N::FrontierStatus::idle);
    CHECK(!runtime.state.fields.active(N::ServiceField::service)&&!runtime.state.nav_intent&&!runtime.state.field_revision);
    CHECK(runtime.state.fields.generation(N::ServiceField::egress)==egress_generation);
    ready(runtime);
    CHECK(runtime.native_group_field_key()!=old_key&&runtime.state.frontier.complete_roots());
    CHECK(contains(runtime.state.frontier.roots(),{4,12})); // Expired Dead59 is possible again, not eternally absent.
}

static void all_remaining_members_reach_the_dead59_opening(){
    ++groups_count;MockRuntime<> runtime(false);
    runtime.native_group_observe(member(runtime,59,N::Life::dead),group(runtime));
    auto budget=runtime.budget();
    for(unsigned n=0;n<4096&&runtime.state.fields.status(N::ServiceField::service)!=N::NavigationStatus::ready;++n)
        runtime.native_prepare_group_frontier(budget);
    CHECK(runtime.state.frontier.complete_roots());
    CHECK(runtime.state.fields.status(N::ServiceField::service,{4,12})==N::NavigationStatus::unreachable);
    CHECK(!contains(runtime.state.frontier.roots(),{5,12}));
    // Actual board publication is its own caller seam. Retire EVERY borrower
    // before changing this byte; a Dead fact alone did not edit occupancy.
    runtime.state.fields.retire_all();runtime.state.frontier.retire();++runtime.state.board_generation;
    runtime.state.passable[12*40+5]=1;runtime.state.nav_intent=runtime.state.field_revision=0;
    ready(runtime);
    CHECK(runtime.state.frontier.roots().size()==2&&contains(runtime.state.frontier.roots(),{5,12})&&contains(runtime.state.frontier.roots(),{35,12}));
    CHECK(!contains(runtime.state.frontier.roots(),{4,12}));
    CHECK(runtime.state.fields.distance(N::ServiceField::service,{4,12})==1);
    N::Navigation<640> exclusive_far30;
    const std::array<N::Point,1> far{{{35,12}}};CHECK(exclusive_far30.begin(40,16,runtime.state.passable,far,999));
    for(unsigned n=0;n<4096&&exclusive_far30.status()==N::NavigationStatus::preparing;++n)exclusive_far30.step(32);
    CHECK(exclusive_far30.status({4,12})==N::NavigationStatus::unreachable);
}

static void field_identity_includes_frontier_generation_and_batch(){
    ++groups_count;MockRuntime<1> runtime;
    runtime.native_group_observe(member(runtime,59,N::Life::dead),group(runtime));auto budget=runtime.budget();
    std::uint64_t first_key=0,first_generation=0;bool published=false,advanced=false;
    for(unsigned n=0;n<4096;++n){
        runtime.native_prepare_group_frontier(budget);
        if(runtime.state.frontier.batch()==0&&runtime.state.frontier.status()==N::FrontierStatus::more){
            if(!first_key){first_key=runtime.native_group_field_key();first_generation=runtime.state.frontier.generation();}
            CHECK(!runtime.state.frontier.complete_roots()&&runtime.state.frontier.batched());
            if(runtime.state.fields.active(N::ServiceField::service)){
                published=true;CHECK(runtime.state.fields.semantic_intent(N::ServiceField::service)==first_key);
                CHECK(runtime.native_service_field_matches());
            }
        }
        if(runtime.state.frontier.batch()==1){advanced=true;break;}
    }
    CHECK(first_key&&published&&advanced);
    const auto second_key=runtime.native_group_field_key();
    CHECK(second_key!=first_key&&(second_key>>8)==first_generation&&(second_key&255)==1);
    CHECK(!runtime.state.fields.active(N::ServiceField::service)&&!runtime.native_service_field_matches());
    CHECK(runtime.state.frontier.status()==N::FrontierStatus::preparing&&!runtime.state.frontier.complete_roots());
    ready(runtime);
    CHECK(runtime.native_group_field_key()==second_key&&runtime.native_service_field_matches());
    CHECK(runtime.state.frontier.status()==N::FrontierStatus::ready&&runtime.state.frontier.batched()&&!runtime.state.frontier.complete_roots());
    CHECK(runtime.state.fields.distance(N::ServiceField::service,{4,12})==1);
    runtime.native_group_routes_changed();ready(runtime);
    CHECK((runtime.native_group_field_key()>>8)!=first_generation);
    // An earlier batch's inability to reach the actor advanced the finite
    // frontier; it did not establish whole-group unreachable or erase work.
    CHECK(runtime.native_arrival_member_needed(57,runtime.facts.epoch)&&runtime.state.unsupported==0);
}

static void publication_requires_ready_static_catalogue_and_board(){
    ++groups_count;
    for(unsigned variant=0;variant<5;++variant){
        MockRuntime<> runtime;auto budget=runtime.budget();
        switch(variant){
        case 0:runtime.state.board_ready=false;break;
        case 1:runtime.state.board_edit_count=1;break;
        case 2:runtime.memory[0].end=65;break;
        case 3:runtime.state.board_cursor=639;break;
        case 4:runtime.state.group_goal=false;break;
        }
        runtime.native_prepare_group_frontier(budget);
        CHECK(!runtime.state.fields.active(N::ServiceField::service));
        CHECK(variant==2||variant==3?runtime.state.unsupported==1:runtime.state.unsupported==0);
    }
    MockRuntime<> runtime;runtime.state.group=-1;CHECK(!runtime.native_group_configure());
    runtime.state.group=2;CHECK(!runtime.native_group_configure());
}

int main(){
    matching_alive_and_reward_only_grant_finite_absence();stale_foreign_and_unqualified_dead_stay_possible();
    stable_dead_proof_and_hp_refresh_do_not_reset_geometry();expiry_restores_possible_work_and_retires_only_group_service();
    all_remaining_members_reach_the_dead59_opening();field_identity_includes_frontier_generation_and_batch();
    publication_requires_ready_static_catalogue_and_board();
    std::printf("PASS native_service_group_runtime: %u checks, %u groups\n",checks,groups_count);
}
