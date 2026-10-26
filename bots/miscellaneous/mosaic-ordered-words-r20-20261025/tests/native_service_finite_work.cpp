#include "../src/native_service_fields.h"
#include "../src/native_service_exit_witness.h"
#include "../src/native_service_cycle.h"
#include "../src/native_service_route_owner.h"
#include "../src/native_service_execution.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <vector>

namespace N=mosaic::native_service;
namespace native_service=mosaic::native_service;
enum class handle_camp_group_t : std::uint32_t {};
namespace constants {inline constexpr unsigned capture_time=5;}
static unsigned checks{},groups{};
static void check(bool value,const char* expression,int line){
    ++checks;if(!value){std::fprintf(stderr,"finite Work check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)

// The actual Runtime fragment is included below. Geometry, receipt/ownership
// reducers and Budget are real; this small board's static potential source,
// home classification and closure callback are controlled inputs. Reward and
// actual spawn holding below are supplied evidence, never capture completion.
// The six-phase deadline uses this fixture's queried capture_time=5 seam.
// There are no
// SDK imports or physical command callbacks. Fake fuel tests admission only,
// never native execution cost, future fuel or actual movement permission.
static N::SelfFact self(N::Point pose={4,0},float hp=100,N::Epoch epoch={2,2,0}){
    N::SelfFact value{};value.epoch=epoch;value.id=0;value.pose=pose;value.hp=hp;value.max_hp=200;
    value.alive=value.raw_m=value.raw_a=value.raw_c=true;value.move_speed=value.attack_range=1;
    value.body_actual=value.restrictions_actual=value.buffs_actual=value.channel_actual=true;
    value.walked_actual=value.gems_actual=value.custody_actual=true;return value;
}
struct FakeMember {N::Point pos{};int range{};float base_strength{};};
struct FakeMemory {std::size_t begin{},end{};};
struct FakeState {
    N::ServiceFields<64> fields{};N::ExitWitness exit_witness{};
    N::CycleState cycle{};N::EgressState egress{};N::RouteOwner route{};
    std::uint64_t batch_owner{};unsigned coverage_transfers{},restorations{};
    bool batch_next_input_exit{},exit_witness_retained{};
    int capture_group{-1},capture_deadline{-1};bool capture_reobserve{};
};
struct FakeRuntime {
    FakeState state{};N::Facts facts{};
    std::vector<FakeMember> members;std::vector<FakeMemory> memory;
    std::vector<handle_camp_group_t> groups;
    std::array<std::uint8_t,64> board{};
    std::array<std::uint64_t,64> potential{};
    std::uint64_t remaining{500000},semantic{17};int width{5},epoch{2};
    mutable unsigned source_calls{},home_calls{};unsigned closure_calls{};
    bool home{},allow_close{true};
    FakeRuntime(){board.fill(1);potential={0,3,6,9,12};use(self());}
    void use(const N::SelfFact& me){facts.epoch=me.epoch;facts.self=me;epoch=me.epoch.round;}
    static std::uint64_t clock(void* context){return static_cast<FakeRuntime*>(context)->remaining;}
    N::Budget budget(){N::Budget value{this,clock,remaining};value.pure_bound=20000;return value;}
    FakeState& native_service_state(){return state;}
    const FakeState& native_service_state()const{return state;}
    N::Facts& native_facts(){return facts;}
    const N::Facts& native_facts()const{return facts;}
    std::uint64_t native_egress_key()const{return semantic;}
    std::uint64_t native_potential(N::Point point)const{
        ++source_calls;
        if(point.y!=0||point.x<0||point.x>=width)return std::numeric_limits<std::uint64_t>::max();
        return potential[std::size_t(point.x)];
    }
    bool native_at_home(N::Point)const{++home_calls;return home;}
    bool native_programme_close_egress(const N::SelfFact& me){
        ++closure_calls;if(!allow_close)return false;
        const auto& latest=state.egress.latest();
        return state.egress.close({me.epoch,me.pose,latest.serial,latest.accepted_prefix,
            N::EgressProducer::complete_potential_zero,true},{me.epoch,me.pose,me.actual(facts.epoch)})==N::EgressUpdate::closed;
    }
    void warm(N::Point root={0,0}){
        const std::array<N::Point,1> roots{{root}};
        CHECK(state.fields.begin(N::ServiceField::egress,width,1,{board.data(),std::size_t(width)},roots,semantic));
        for(unsigned i=0;i<8&&state.fields.status(N::ServiceField::egress)==N::NavigationStatus::preparing;++i)
            state.fields.step(N::ServiceField::egress,64);
        CHECK(state.fields.status(N::ServiceField::egress)==N::NavigationStatus::ready);
    }
    void seed_work(const N::SelfFact& me,bool bind_route=true){
        const N::EgressWork event{N::PrimitiveKind::basic,N::EgressEffect::yes,1,1,{1,1,0},me.pose,{0,0,0}};
        CHECK(state.egress.observe_work(event)==N::EgressUpdate::created);
        CHECK(state.cycle.observe_work({event,{0,0,0,0},true})==N::CycleUpdate::retained);
        use(me);native_cycle_actual(me);
        if(bind_route){
            CHECK(state.route.bind({71,semantic,N::RoutePurpose::work,0,0,me.pose})==N::RouteBinding::bound);
            CHECK(state.route.begin(me)&&state.route.observe_custody(me));
        }
    }

    #include "../src/native_service_finite_work.h"
};

static void hp_alone_pays_the_current_work_origin_phase(){
    for(bool ranged:std::array<bool,2>{false,true}){
        FakeRuntime runtime;runtime.warm();auto me=self({4,0},31);me.ranged=ranged;me.shield=1000000;
        runtime.use(me);auto available=runtime.budget();
        CHECK(!runtime.native_work_funded(available,me));
        const auto& witness=runtime.state.exit_witness;
        // Four descending steps see 9+6+3+0. Holding the Work origin adds12;
        // the one explicit refusal phase adds12 again. A melee-only price30
        // would incorrectly admit HP31, even with this fresh current M.
        CHECK(witness.prices_complete()&&witness.steps==4);
        CHECK(witness.melee_phases==18&&witness.ranged_phases==30&&witness.max_origin_phase==12);
        CHECK(witness.melee_with_refusal==30&&witness.ranged_with_refusal==42);
        me.hp=42;runtime.use(me);CHECK(!runtime.native_work_funded(available,me));
        me.hp=43;me.shield=0;runtime.use(me);CHECK(runtime.native_work_funded(available,me));
        CHECK(available.queries==0&&available.commands==0&&!available.poisoned);
        CHECK(!runtime.state.egress.outstanding()&&!runtime.state.cycle.retained_job().valid());
    }
    ++groups;
}

static void ingress_prices_the_prospective_origin_and_preserves_hp_boundary(){
    FakeRuntime runtime;runtime.warm();auto me=self({0,0},31);me.shield=1000000;runtime.use(me);
    auto available=runtime.budget();
    CHECK(!runtime.native_ingress_funded(available,me,{4,0})); // Being quiet HERE cannot pay arrival THERE.
    CHECK(runtime.state.exit_witness.tag.origin==N::Point{4,0});
    CHECK(runtime.state.exit_witness.ranged_with_refusal==42);
    me.hp=42;runtime.use(me);CHECK(!runtime.native_ingress_funded(available,me,{4,0}));
    me.hp=43;me.shield=0;runtime.use(me);CHECK(runtime.native_ingress_funded(available,me,{4,0}));
    me.hp=27;runtime.use(me);CHECK(!runtime.native_ingress_funded(available,me,{3,0}));
    CHECK(runtime.state.exit_witness.tag.origin==N::Point{3,0}&&runtime.state.exit_witness.ranged_with_refusal==27);
    me.hp=28;runtime.use(me);CHECK(runtime.native_ingress_funded(available,me,{3,0}));
    CHECK(runtime.facts.self.pose==N::Point{0,0}&&runtime.facts.self.epoch==me.epoch); // Candidate cost is no actual arrival.
    CHECK(runtime.native_ingress_funded(available,me,{0,0}));
    CHECK(available.queries==0&&available.commands==0&&!runtime.state.egress.outstanding());
    ++groups;
}

static N::GroupFact reward(N::Epoch epoch,int ordinal=0){
    N::GroupFact value{};value.epoch=epoch;value.id=unsigned(ordinal);value.ordinal=ordinal;
    value.life=N::GroupLife::reward;value.actual=value.visibility_actual=value.visible=true;return value;
}
static N::CaptureFact holding(const N::GroupFact& group){
    N::CaptureFact value{};value.epoch=group.epoch;value.group_id=group.id;value.life=group.life;
    value.life_actual=group.actual;value.on_spawn_actual=value.on_spawn=true;return value;
}
static void transferred_work_then_new_capture_owner(FakeRuntime& runtime,const N::SelfFact& me,bool observed_holding=true){
    // At the actual occupied spawn, selected group0 contributes5 of the full
    // static12; other groups still contribute7. The complete exit keeps all12
    // at its origin and 9+6+3+0 at later poses. No Reward discount enters it.
    runtime.members={{{observed_holding?4:3,0},observed_holding?0:1,5}};runtime.memory={{0,1},{1,1}};
    runtime.groups={handle_camp_group_t(0),handle_camp_group_t(1)};
    runtime.warm();runtime.seed_work(me);auto available=runtime.budget();
    CHECK(runtime.native_cover_work(available,me,true));
    CHECK(runtime.state.egress.replacement_owner()==71&&runtime.state.coverage_transfers==1);
    runtime.facts.group=reward(me.epoch);runtime.facts.capture=holding(runtime.facts.group);
    if(!observed_holding)runtime.facts.capture.on_spawn=runtime.facts.capture.on_spawn_actual=false;
    CHECK(runtime.state.cycle.observe_lifecycle(runtime.facts.group,runtime.facts.capture,me.epoch)==N::CycleUpdate::observed);
    runtime.native_cycle_actual(me);
    CHECK(!runtime.native_current_quiet(me)&&!runtime.state.cycle.has_egressed());
    // This is the programme's paid route-owner seam, not SDK authorization.
    // It deliberately leaves physical Work debt owned by71 until the actual
    // capture helper accepts the new complete continuation under owner72.
    CHECK(runtime.state.route.replace({72,runtime.semantic+1,N::RoutePurpose::capture,0,0,me.pose},
        {72,1,me.epoch,N::RouteNeed::arrival,true,true,71}));
    CHECK(runtime.state.route.begin(me)&&runtime.state.route.observe_custody(me));
    CHECK(runtime.state.route.goal().token==72&&runtime.state.egress.replacement_owner()==71);
}

static void capture_renews_the_actual_owner_only_after_full_wait_and_exit_price(){
    for(bool next_input:std::array<bool,2>{false,true}){
        FakeRuntime runtime;const auto cost=next_input?84.0f:72.0f;
        auto me=self({4,0},cost,{20,20,0});me.ranged=next_input;me.raw_m=!next_input;me.shield=1000000;
        transferred_work_then_new_capture_owner(runtime,me);auto available=runtime.budget();available.reserve(777);
        const auto actual_group=runtime.facts.group;const auto actual_capture=runtime.facts.capture;
        CHECK(runtime.native_group_free_potential(me.pose,0)==7);
        CHECK(!runtime.native_capture_funded(available,me,actual_group,actual_capture));
        CHECK(runtime.state.capture_group==0&&runtime.state.capture_deadline==26&&!runtime.state.capture_reobserve);
        CHECK(runtime.state.exit_witness.ranged_with_refusal==42&&runtime.state.exit_witness.melee_with_refusal==30);
        CHECK(runtime.state.egress.replacement_owner()==71&&runtime.state.coverage_transfers==1);
        CHECK(available.held_tail==777); // Negative admission never credits old owner71 as renewed Capture.
        me.hp=cost+1;me.shield=0;runtime.use(me);
        CHECK(runtime.native_capture_funded(available,me,actual_group,actual_capture));
        CHECK(runtime.state.egress.replacement_owner()==72&&runtime.state.coverage_transfers==2);
        CHECK(runtime.state.egress.outstanding()&&runtime.state.egress.origin().serial==1&&runtime.state.egress.latest().accepted_prefix==1);
        CHECK(runtime.state.cycle.restore_required()&&!runtime.state.cycle.has_egressed());
        CHECK(runtime.state.cycle.retained_job()==N::CycleJob{0,0,0,0});
        CHECK(runtime.state.route.goal().purpose==N::RoutePurpose::capture&&runtime.state.route.service_progress()==0);
        CHECK(runtime.facts.self.gems==0&&runtime.facts.group.life==N::GroupLife::reward);
        CHECK(available.held_tail==777&&available.queries==0&&available.commands==0&&!available.poisoned);
        // Cost is 7*(capture_time+1) plus the retained FULL-union exit:
        // 42+30=72 with current M, 42+42=84 with next-input ranged M.
    }
    ++groups;
}

static void capture_tail_denial_cannot_claim_the_prior_work_owner_as_capture(){
    FakeRuntime runtime;auto me=self({4,0},100,{20,20,0});
    transferred_work_then_new_capture_owner(runtime,me);
    runtime.remaining=30000;auto available=runtime.budget();available.reserve(777);
    CHECK(!runtime.native_capture_funded(available,me,runtime.facts.group,runtime.facts.capture));
    CHECK(runtime.state.egress.outstanding()&&runtime.state.egress.replacement_owner()==71);
    CHECK(runtime.state.route.goal().token==72&&runtime.state.coverage_transfers==1);
    CHECK(runtime.state.capture_deadline==26&&runtime.state.route.service_progress()==0);
    CHECK(available.held_tail==777&&available.commands==0&&available.queries==0&&!available.poisoned);
    // The fixture supplies the fresh frontier to the real reclaim consumer.
    // The programme's producer/branch is tested separately; this is not a
    // helper false-return ledger reset or physical egress completion shortcut.
    const auto origin=runtime.state.egress.origin(),latest=runtime.state.egress.latest();
    CHECK(runtime.state.egress.reclaim({me.epoch,me.pose,latest.serial,latest.accepted_prefix,71},
        {me.epoch,me.pose,me.actual(runtime.facts.epoch)})==N::EgressUpdate::reclaimed);
    CHECK(runtime.state.egress.direct_required()&&runtime.state.egress.replacement_owner()==0);
    CHECK(runtime.state.egress.origin().serial==origin.serial&&runtime.state.egress.origin().epoch==origin.epoch);
    CHECK(runtime.state.egress.latest().serial==latest.serial&&runtime.state.egress.latest().accepted_prefix==latest.accepted_prefix);
    CHECK(runtime.state.cycle.restore_required()&&runtime.state.cycle.retained_job().valid());
    ++groups;
}

static void capture_deadline_is_fixed_and_expiry_demands_reobservation(){
    FakeRuntime runtime;auto me=self({4,0},100,{20,20,0});
    transferred_work_then_new_capture_owner(runtime,me);auto available=runtime.budget();
    CHECK(runtime.native_capture_funded(available,me,runtime.facts.group,runtime.facts.capture));
    CHECK(runtime.state.capture_deadline==26&&runtime.state.coverage_transfers==2);
    for(int round:std::array<int,2>{21,25}){
        me=self({4,0},float(100-7*(round-20)),{round,unsigned(round),0});runtime.use(me);
        runtime.facts.group=reward(me.epoch);runtime.facts.capture=holding(runtime.facts.group);
        runtime.native_cycle_actual(me);available=runtime.budget();
        CHECK(runtime.native_capture_funded(available,me,runtime.facts.group,runtime.facts.capture));
        CHECK(runtime.state.capture_deadline==26&&!runtime.state.capture_reobserve);
        CHECK(runtime.state.egress.replacement_owner()==72&&runtime.state.route.service_progress()==0);
    }
    const auto transfers=runtime.state.coverage_transfers;
    for(int round:std::array<int,2>{26,27}){
        me=self({4,0},100,{round,unsigned(round),0});runtime.use(me);
        runtime.facts.group=reward(me.epoch);runtime.facts.capture=holding(runtime.facts.group);available=runtime.budget();
        CHECK(!runtime.native_capture_funded(available,me,runtime.facts.group,runtime.facts.capture));
        CHECK(runtime.state.capture_deadline==26&&runtime.state.capture_reobserve);
        CHECK(runtime.state.coverage_transfers==transfers&&runtime.state.egress.outstanding());
        CHECK(runtime.state.cycle.retained_job().valid()&&runtime.state.route.service_progress()==0);
        CHECK(runtime.facts.self.gems==0&&available.queries==0&&available.commands==0);
    }
    ++groups;
}

static void capture_needs_actual_reward_with_the_exact_catalogue_binding(){
    for(unsigned scenario=0;scenario<8;++scenario){
        FakeRuntime runtime;auto me=self({4,0},100,{20,20,0});
        transferred_work_then_new_capture_owner(runtime,me);
        auto group=runtime.facts.group;const auto capture=runtime.facts.capture;
        if(scenario==0)group.actual=false;
        if(scenario==1)group.life=N::GroupLife::alive;
        if(scenario==2){group.life=N::GroupLife::respawning;group.respawn_actual=true;group.respawn_time=12;}
        if(scenario==3)group.ordinal=-1;
        if(scenario==4)group.ordinal=2;
        if(scenario==5)group.id=99;
        if(scenario==6)group.ordinal=1;
        if(scenario==7)group.epoch={19,19,0};
        auto available=runtime.budget();
        CHECK(!runtime.native_capture_funded(available,me,group,capture));
        CHECK(runtime.state.capture_group==-1&&runtime.state.capture_deadline==-1);
        CHECK(runtime.state.egress.replacement_owner()==71&&runtime.state.coverage_transfers==1);
        CHECK(runtime.state.route.service_progress()==0&&runtime.facts.self.gems==0);
        CHECK(!runtime.state.capture_reobserve&&available.queries==0&&available.commands==0);
    }
    ++groups;
}

static void actual_respawning_before_the_deadline_does_not_mint_completion(){
    FakeRuntime runtime;auto me=self({4,0},100,{20,20,0});
    transferred_work_then_new_capture_owner(runtime,me);auto available=runtime.budget();
    CHECK(runtime.native_capture_funded(available,me,runtime.facts.group,runtime.facts.capture));
    const auto transfers=runtime.state.coverage_transfers;
    me=self({4,0},93,{21,21,0});runtime.use(me);runtime.facts.group=reward(me.epoch);
    runtime.facts.group.life=N::GroupLife::respawning;
    runtime.facts.group.respawn_actual=true;runtime.facts.group.respawn_time=12;
    runtime.facts.capture=holding(runtime.facts.group);available=runtime.budget();
    CHECK(!runtime.native_capture_funded(available,me,runtime.facts.group,runtime.facts.capture));
    CHECK(runtime.state.capture_deadline==26&&!runtime.state.capture_reobserve);
    CHECK(runtime.state.coverage_transfers==transfers&&runtime.state.egress.outstanding());
    CHECK(runtime.state.cycle.observe_lifecycle(runtime.facts.group,runtime.facts.capture,me.epoch)==N::CycleUpdate::released);
    CHECK(!runtime.state.cycle.retained_job().valid()&&runtime.state.cycle.restore_required());
    CHECK(runtime.state.route.service_progress()==0&&runtime.facts.self.gems==0);
    CHECK(available.queries==0&&available.commands==0);
    // Actual group-cycle release and payout amount are separate evidence.
    ++groups;
}

static void capture_holding_requires_fresh_complete_matching_evidence(){
    for(unsigned scenario=0;scenario<7;++scenario){
        FakeRuntime runtime;auto me=self({4,0},100,{20,20,0});
        transferred_work_then_new_capture_owner(runtime,me);
        const auto group=runtime.facts.group;auto capture=runtime.facts.capture;
        if(scenario==0)capture.epoch={19,19,0};
        if(scenario==1)capture.group_id=99;
        if(scenario==2)capture.life_actual=false;
        if(scenario==3)capture.life=N::GroupLife::alive;
        if(scenario==4)capture.life=N::GroupLife::respawning;
        if(scenario==5)capture.on_spawn_actual=false;
        if(scenario==6)capture.life=N::GroupLife::unknown;
        auto available=runtime.budget();
        CHECK(!runtime.native_capture_funded(available,me,group,capture));
        CHECK(runtime.state.capture_deadline==-1&&runtime.state.capture_group==-1&&!runtime.state.capture_reobserve);
        CHECK(runtime.state.egress.replacement_owner()==71&&runtime.state.coverage_transfers==1);
        CHECK(runtime.state.route.service_progress()==0&&runtime.facts.self.gems==0);
        CHECK(available.queries==0&&available.commands==0&&!available.poisoned);
    }
    ++groups;
}

static void unknown_spawn_holding_keeps_an_explicit_zero_wait_approach(){
    FakeRuntime runtime;auto me=self({4,0},100,{20,20,0});
    transferred_work_then_new_capture_owner(runtime,me,false);
    // The real producer is positive-only: absent on_spawn_actual is not a
    // prohibition on an approach. It also cannot authorize a capture wait.
    me=self({4,0},30,{21,21,0});runtime.use(me);runtime.facts.group=reward(me.epoch);
    runtime.facts.capture=holding(runtime.facts.group);
    runtime.facts.capture.on_spawn=runtime.facts.capture.on_spawn_actual=false;
    CHECK(runtime.state.route.begin(me));auto available=runtime.budget();
    CHECK(!runtime.native_capture_funded(available,me,runtime.facts.group,runtime.facts.capture));
    CHECK(runtime.state.exit_witness.melee_with_refusal==30&&runtime.state.exit_witness.ranged_with_refusal==42);
    me.hp=31;runtime.use(me);
    CHECK(runtime.native_capture_funded(available,me,runtime.facts.group,runtime.facts.capture));
    CHECK(runtime.state.capture_group==-1&&runtime.state.capture_deadline==-1&&!runtime.state.capture_reobserve);
    CHECK(runtime.state.egress.replacement_owner()==72&&runtime.state.coverage_transfers==2);
    CHECK(runtime.state.egress.outstanding()&&runtime.state.cycle.restore_required());
    CHECK(runtime.state.route.service_progress()==0&&runtime.facts.self.gems==0);
    CHECK(!runtime.facts.capture.on_spawn_actual&&!runtime.facts.capture.holding_reward(me.epoch));
    CHECK(available.queries==0&&available.commands==0);
    ++groups;
}

static void actual_leaving_ends_the_selected_absence_wait_price(){
    FakeRuntime runtime;auto me=self({4,0},73,{20,20,0});
    transferred_work_then_new_capture_owner(runtime,me);auto available=runtime.budget();
    CHECK(runtime.native_capture_funded(available,me,runtime.facts.group,runtime.facts.capture));
    CHECK(runtime.state.capture_deadline==26&&runtime.state.egress.replacement_owner()==72);
    // Supply actual movement off the occupied spawn. That observation grants
    // an approach class only: no six-phase wait and no deadline extension.
    // The complete exit from3 includes the FULL source9+6+3, not a persistent
    // selected-Reward discount and not a fabricated arrival/capture receipt.
    me=self({3,0},18,{21,21,0});runtime.use(me);runtime.facts.group=reward(me.epoch);
    runtime.facts.capture=holding(runtime.facts.group);runtime.facts.capture.on_spawn=false;
    runtime.native_cycle_actual(me);available=runtime.budget();
    CHECK(!runtime.native_capture_funded(available,me,runtime.facts.group,runtime.facts.capture));
    CHECK(runtime.state.exit_witness.melee_with_refusal==18&&runtime.state.exit_witness.ranged_with_refusal==27);
    CHECK(runtime.state.capture_deadline==26&&runtime.state.coverage_transfers==2);
    me.hp=19;runtime.use(me);
    CHECK(runtime.native_capture_funded(available,me,runtime.facts.group,runtime.facts.capture));
    CHECK(runtime.state.capture_deadline==26&&runtime.state.coverage_transfers==3);
    CHECK(runtime.state.route.service_progress()==0&&runtime.facts.self.gems==0&&runtime.state.egress.outstanding());
    CHECK(runtime.facts.capture.on_spawn_actual&&!runtime.facts.capture.holding_reward(me.epoch));
    CHECK(available.queries==0&&available.commands==0);
    ++groups;
}

static void fresh_reward_absence_is_local_and_does_not_wait_for_cold_geometry(){
    FakeRuntime runtime;runtime.width=6;
    // Independent static source map: group0 has camp10 at2/range1 and camp3
    // at3/range0. Group1 contributes6 at3..5. Reward0 removes exactly its own
    // dead13 at3; the other live6 remains. The cold board deliberately retains
    // every static camp source, so all absence below must be freshly actual.
    runtime.potential={0,10,10,19,6,6};
    runtime.members={{{2,0},1,10},{{3,0},0,2.5f},{{4,0},1,6}};
    runtime.memory={{0,2},{2,3}};
    runtime.groups={handle_camp_group_t(0),handle_camp_group_t(1)};
    auto me=self({2,0},1);runtime.use(me);runtime.facts.group=reward(me.epoch);
    CHECK(!runtime.state.fields.active(N::ServiceField::egress));
    CHECK(runtime.native_current_quiet(me));
    auto available=runtime.budget();CHECK(runtime.native_work_funded(available,me));
    CHECK(runtime.native_ingress_funded(available,me,{1,0})); // Immediate capture ingress needs no cold exit field.
    CHECK(!runtime.native_ingress_funded(available,me,{3,0})); // Reward0 cannot erase group1's real6.
    CHECK(runtime.native_group_free_potential({3,0},0)==6);
    CHECK(available.queries==0&&available.commands==0);
    runtime.facts.group=reward({1,1,0});CHECK(!runtime.native_current_quiet(me));
    CHECK(!runtime.native_ingress_funded(available,me,{1,0}));
    runtime.facts.group=reward(me.epoch,1);CHECK(!runtime.native_current_quiet(me));
    CHECK(!runtime.native_ingress_funded(available,me,{1,0}));
    runtime.facts.group=reward(me.epoch);runtime.facts.group.id=99;
    CHECK(!runtime.native_current_quiet(me)&&!runtime.native_ingress_funded(available,me,{1,0}));
    auto other=self({4,0},1,me.epoch);runtime.use(other);runtime.facts.group=reward(other.epoch,1);runtime.facts.group.id=0;
    CHECK(!runtime.native_current_quiet(other)&&!runtime.native_ingress_funded(available,other,{5,0})); // Fresh wrong ordinal/handle pair cannot erase group1.
    runtime.facts.group=reward(other.epoch,1);runtime.groups.resize(1);
    CHECK(!runtime.native_current_quiet(other)&&!runtime.native_ingress_funded(available,other,{5,0})); // Memory alone is no catalogue binding.
    runtime.groups.push_back(handle_camp_group_t(1));runtime.use(me);
    runtime.facts.group=reward(me.epoch);runtime.facts.group.actual=false;CHECK(!runtime.native_current_quiet(me));
    runtime.facts.group=reward(me.epoch);runtime.warm();
    const auto& exit=runtime.native_exit_witness(me.epoch,me.pose);
    CHECK(exit.prices_complete()&&exit.status==N::ExitWitnessStatus::path&&exit.steps==2);
    CHECK(exit.ranged_with_refusal==30); // Present Reward is never discounted across the multi-input static exit.
    CHECK(runtime.native_current_quiet(me));
    ++groups;
}

static void transfer_needs_actual_rank_a_route_and_current_word_tail(){
    {
        FakeRuntime runtime;runtime.warm();auto me=self();runtime.seed_work(me);
        auto available=runtime.budget();CHECK(!runtime.native_cover_work(available,me,false));
        CHECK(runtime.state.egress.direct_required()&&!runtime.state.egress.transferred()&&runtime.state.coverage_transfers==0);
        CHECK(runtime.state.cycle.restore_required()&&runtime.state.cycle.retained_job()==N::CycleJob{0,0,0,0});
    }
    {
        FakeRuntime runtime;runtime.warm();auto me=self();runtime.seed_work(me,false);
        auto available=runtime.budget();CHECK(!runtime.native_cover_work(available,me,true));
        CHECK(runtime.state.egress.direct_required()&&runtime.state.coverage_transfers==0);
    }
    {
        FakeRuntime runtime;runtime.warm();auto me=self();runtime.seed_work(me);
        runtime.remaining=30000;auto available=runtime.budget();available.reserve(777);
        CHECK(!runtime.native_cover_work(available,me,true)); // HP and a warm exit cannot buy the unpaid current Move word tail.
        CHECK(available.held_tail==777&&!available.poisoned&&available.queries==0&&available.commands==0);
        CHECK(runtime.state.egress.direct_required()&&runtime.state.coverage_transfers==0);
    }
    {
        FakeRuntime runtime;runtime.warm();auto me=self();runtime.seed_work(me);
        runtime.remaining=100000;auto available=runtime.budget();available.reserve(777);
        CHECK(runtime.native_cover_work(available,me,true));
        CHECK(available.held_tail==777&&runtime.state.coverage_transfers==1&&runtime.state.batch_owner==71);
        CHECK(runtime.state.egress.outstanding()&&runtime.state.egress.transferred()&&runtime.state.egress.replacement_owner()==71);
        CHECK(runtime.state.cycle.restore_required()&&!runtime.state.cycle.has_egressed()); // Transfer is ownership, not quiet/restoration.
        CHECK(runtime.state.route.active()&&runtime.state.route.service_progress()==0);
    }
    {
        FakeRuntime runtime;runtime.warm();auto me=self();runtime.seed_work(me);
        runtime.facts.epoch={3,3,0};auto available=runtime.budget();
        CHECK(!runtime.native_cover_work(available,me,true)); // Geometry can be retagged; stale actual body cannot transfer debt.
        CHECK(runtime.state.egress.direct_required()&&runtime.state.coverage_transfers==0);
    }
    ++groups;
}

static N::Receipt accepted_move(N::Point from,N::Point to,N::Epoch before,N::Epoch after){
    N::Receipt receipt{};receipt.serial=1;receipt.action={N::PrimitiveKind::move,0,to,0};
    receipt.before_pose=from;receipt.after_pose=to;receipt.before_epoch=before;receipt.after_epoch=after;
    receipt.applied=N::Truth::yes;receipt.attempted=receipt.returned=receipt.after_actual=true;return receipt;
}
static void explicitly_retained_path_trims_only_accepted_matching_next_edges(){
    FakeRuntime runtime;runtime.width=6;runtime.potential={0,3,6,9,12,15};runtime.board[5]=0;runtime.warm();
    const std::array<N::Point,1> service_roots{{{4,0}}};
    CHECK(runtime.state.fields.begin(N::ServiceField::service,runtime.width,1,{runtime.board.data(),std::size_t(runtime.width)},service_roots,99));
    runtime.state.fields.step(N::ServiceField::service,64);
    CHECK(runtime.state.fields.active(N::ServiceField::service)&&runtime.state.fields.active(N::ServiceField::egress));
    auto me=self();const auto original=runtime.native_exit_witness(me.epoch,me.pose);
    CHECK(original.prices_complete()&&original.steps==4&&original.ranged_with_refusal==42);
    const auto source_before_retirement=runtime.source_calls;
    // Model ONLY the controller's explicit outside-path/opening edit lemma.
    // Both borrowers retire before board mutation. This does not test the
    // controller's edit-footprint producer, SDK occupancy or edge permission.
    runtime.state.fields.retire_all();
    CHECK(!runtime.state.fields.active(N::ServiceField::service)&&!runtime.state.fields.active(N::ServiceField::egress));
    runtime.state.exit_witness_retained=true;runtime.board[5]=1;
    const N::Epoch later{3,3,0};const auto retained=runtime.native_exit_witness(later,me.pose);
    CHECK(retained.prices_complete()&&retained.tag.field_generation==original.tag.field_generation);
    CHECK(runtime.state.fields.generation(N::ServiceField::egress)==0&&runtime.state.exit_witness_retained);
    CHECK(runtime.source_calls==source_before_retirement&&retained.tag.epoch==later);
    // The controller calls this seam only for an actual accepted Move; this
    // fixture supplies that receipt, rather than testing the producer's gate.
    const auto next=accepted_move({4,0},{3,0},later,{3,4,1});runtime.native_advance_exit_witness(next);
    const auto& trimmed=runtime.state.exit_witness;
    CHECK(trimmed.prices_complete()&&trimmed.steps==3&&trimmed.tag.origin==N::Point{3,0}&&trimmed.tag.epoch==next.after_epoch);
    CHECK(trimmed.path[0]==N::Point{2,0}&&trimmed.path[2]==N::Point{0,0});
    CHECK(trimmed.melee_phases==9&&trimmed.max_origin_phase==9&&trimmed.ranged_phases==18&&trimmed.ranged_with_refusal==27);
    CHECK(runtime.state.exit_witness_retained&&runtime.source_calls>source_before_retirement);
    CHECK(runtime.facts.self.pose==N::Point{4,0}&&runtime.facts.self.epoch==me.epoch); // Repricing publishes no self observation.
    CHECK(runtime.state.route.service_progress()==0&&!runtime.state.egress.outstanding());
    const auto off_path=accepted_move({3,0},{4,0},next.after_epoch,{4,5,0});
    runtime.native_advance_exit_witness(off_path);
    CHECK(!runtime.state.exit_witness.prices_complete()&&!runtime.state.exit_witness_retained);
    CHECK(!runtime.native_exit_witness(off_path.after_epoch,off_path.action.point).prices_complete()); // Cold fields cannot silently replace the invalidated proof.
    ++groups;
}

static void normal_retirement_or_a_declared_path_block_never_reuses_old_generation(){
    FakeRuntime runtime;runtime.warm();const auto first=runtime.native_exit_witness({2,2,0},{4,0});
    CHECK(first.prices_complete()&&!runtime.state.exit_witness_retained);
    runtime.state.fields.retire_all();
    CHECK(!runtime.native_exit_witness({3,3,0},{4,0}).prices_complete()); // No explicit retention lemma.
    runtime.warm();CHECK(runtime.native_exit_witness({3,3,0},{4,0}).prices_complete());
    runtime.state.fields.retire_all();runtime.board[2]=0;runtime.state.exit_witness_retained=false;
    // Controller owns detecting a newly blocking path cell. This fixture
    // supplies that negative producer result and exercises the actual consumer.
    CHECK(!runtime.native_exit_witness({4,4,0},{4,0}).prices_complete());
    runtime.warm();CHECK(!runtime.native_exit_witness({4,4,0},{4,0}).prices_complete());
    ++groups;
}

static void current_move_and_explicit_ranged_next_input_class_are_distinct(){
    for(unsigned scenario=0;scenario<4;++scenario){
        FakeRuntime runtime;runtime.warm();auto me=self();
        me.raw_m=scenario==2;me.ranged=scenario!=0;me.move_restricted=scenario==3;
        runtime.seed_work(me);auto available=runtime.budget();
        const bool covered=runtime.native_cover_work(available,me,true);
        CHECK(covered==(scenario==1||scenario==2));
        CHECK(runtime.state.batch_next_input_exit==(scenario==1));
        CHECK(runtime.state.egress.transferred()==covered&&runtime.state.egress.outstanding());
        CHECK(runtime.state.cycle.restore_required()&&available.commands==0&&available.queries==0);
        if(scenario==1)CHECK(runtime.state.exit_witness.ranged_with_refusal==42); // One origin phase pays the next-input deadline.
    }
    ++groups;
}

static void quiet_closure_and_actual_restoration_remain_separate(){
    FakeRuntime runtime;runtime.warm();auto me=self();runtime.seed_work(me);
    auto available=runtime.budget();CHECK(runtime.native_cover_work(available,me,true));
    me=self({0,0},60,{3,3,0});me.raw_m=false;runtime.use(me);runtime.native_cycle_actual(me);
    CHECK(runtime.state.cycle.restore_required()&&runtime.state.cycle.has_egressed()&&runtime.state.restorations==0);
    CHECK(runtime.state.egress.outstanding()); // The cycle observation cannot clear the physical ledger.
    available=runtime.budget();CHECK(runtime.native_cover_work(available,me,false));
    CHECK(runtime.closure_calls==1&&!runtime.state.egress.outstanding()&&runtime.state.coverage_transfers==1);
    CHECK(runtime.state.cycle.restore_required()&&runtime.state.cycle.retained_job().valid());
    me=self({0,0},200,{4,4,0});runtime.use(me);runtime.native_cycle_actual(me);
    CHECK(!runtime.state.cycle.restore_required()&&runtime.state.restorations==1&&runtime.state.cycle.retained_job().valid());
    CHECK(runtime.state.cycle.resume_ready()&&runtime.home_calls>=3);
    ++groups;
}

static void cached_static_proof_obeys_origin_real_generation_and_intent(){
    FakeRuntime runtime;runtime.warm();auto me=self();
    const auto first=runtime.native_exit_witness(me.epoch,{4,0});const auto sampled=runtime.source_calls;
    CHECK(first.prices_complete()&&first.ranged_with_refusal==42&&sampled>0);
    const auto actual_epoch=runtime.facts.self.epoch;const auto actual_pose=runtime.facts.self.pose;
    const N::Epoch later{9,9,0};const auto retagged=runtime.native_exit_witness(later,{4,0});
    CHECK(retagged.tag.epoch==later&&retagged.tag.origin==first.tag.origin&&retagged.tag.field_generation==first.tag.field_generation);
    CHECK(runtime.source_calls==sampled&&retagged.ranged_with_refusal==42);
    CHECK(runtime.facts.self.epoch==actual_epoch&&runtime.facts.self.pose==actual_pose); // No actual self/SDK claim is manufactured.
    const auto moved_origin=runtime.native_exit_witness(later,{3,0});
    CHECK(runtime.source_calls>sampled&&moved_origin.tag.origin==N::Point{3,0}&&moved_origin.ranged_with_refusal==27);
    const auto old_generation=moved_origin.tag.field_generation;const auto before_generation_change=runtime.source_calls;
    runtime.warm({1,0}); // Real ServiceFields generation changes; root1 is still hot.
    const auto changed=runtime.native_exit_witness(later,{3,0});
    CHECK(changed.tag.field_generation!=old_generation&&runtime.source_calls>before_generation_change);
    CHECK(!changed.prices_complete()&&changed.status==N::ExitWitnessStatus::unsupported);
    runtime.state.fields.retire(N::ServiceField::egress);
    CHECK(!runtime.native_exit_witness(later,{3,0}).prices_complete());
    runtime.warm();const auto restored=runtime.native_exit_witness(later,{3,0});
    CHECK(restored.prices_complete());const auto before_intent=runtime.source_calls;
    ++runtime.semantic;runtime.warm();const auto changed_intent=runtime.native_exit_witness(later,{3,0});
    CHECK(changed_intent.prices_complete()&&changed_intent.tag.semantic_intent==runtime.semantic&&runtime.source_calls>before_intent);
    CHECK(runtime.state.fields.semantic_intent(N::ServiceField::egress)==runtime.semantic);
    CHECK(!runtime.state.egress.outstanding()&&!runtime.state.cycle.retained_job().valid()&&runtime.state.route.service_progress()==0);
    ++groups;
}

static bool same_exit_quote(const N::ExitWitness& left,const N::ExitWitness& right){
    return left.tag==right.tag&&left.status==right.status&&left.path==right.path&&
        left.steps==right.steps&&left.source_samples==right.source_samples&&
        left.melee_phases==right.melee_phases&&left.ranged_phases==right.ranged_phases&&
        left.max_origin_phase==right.max_origin_phase&&left.melee_with_refusal==right.melee_with_refusal&&
        left.ranged_with_refusal==right.ranged_with_refusal&&left.saturated==right.saturated;
}
struct QuoteHistory {
    N::ExitWitness incumbent{};N::CycleJob job{};N::CyclePhase phase{};N::RouteGoal goal{};N::RouteNeed need{};
    N::Epoch actual_epoch{};N::Point actual_pose{};
    N::CycleWork work{};std::uint64_t serial{},prefix{},owner{},edges{},progress{};
    int custody{};bool owed{},outstanding{},direct{},transferred{},restore{},retained{};
};
static QuoteHistory quote_history(const FakeRuntime& runtime){
    const auto& s=runtime.state;return {s.exit_witness,s.cycle.retained_job(),s.cycle.phase(),s.route.goal(),s.route.need(),
        runtime.facts.self.epoch,runtime.facts.self.pose,s.cycle.latest_work(),s.egress.latest().serial,
        s.egress.latest().accepted_prefix,s.egress.replacement_owner(),s.route.physical_edges(),s.route.service_progress(),
        s.route.custody(),s.route.custody_owed(),s.egress.outstanding(),s.egress.direct_required(),s.egress.transferred(),
        s.cycle.restore_required(),s.exit_witness_retained};
}
static void quote_history_is_unchanged(const FakeRuntime& runtime,const QuoteHistory& before){
    const auto after=quote_history(runtime);
    CHECK(same_exit_quote(after.incumbent,before.incumbent));
    CHECK(after.job==before.job&&after.phase==before.phase&&after.goal==before.goal&&after.need==before.need);
    CHECK(after.actual_epoch==before.actual_epoch&&after.actual_pose==before.actual_pose);
    const auto& a=after.work.receipt;const auto& b=before.work.receipt;
    CHECK(after.work.job==before.work.job&&after.work.hazardous==before.work.hazardous);
    CHECK(a.kind==b.kind&&a.effect==b.effect&&a.serial==b.serial&&a.accepted_prefix==b.accepted_prefix);
    CHECK(a.epoch==b.epoch&&a.pose==b.pose&&a.job==b.job);
    CHECK(after.serial==before.serial&&after.prefix==before.prefix&&after.owner==before.owner);
    CHECK(after.edges==before.edges&&after.progress==before.progress&&after.custody==before.custody&&after.owed==before.owed);
    CHECK(after.outstanding==before.outstanding&&after.direct==before.direct&&after.transferred==before.transferred);
    CHECK(after.restore==before.restore&&after.retained==before.retained);
}

static void cached_egress_suffix_is_only_a_quote_with_the_full_hp_boundary(){
    for(bool ranged:std::array<bool,2>{false,true}){
        FakeRuntime runtime;runtime.warm();auto me=self({4,0},30);me.ranged=ranged;me.gems=26;me.shield=1000000;
        runtime.seed_work(me);const auto incumbent=runtime.native_exit_witness(me.epoch,me.pose);
        CHECK(incumbent.prices_complete()&&incumbent.melee_with_refusal==30&&incumbent.next_point()==N::Point{3,0});
        const auto before=quote_history(runtime);N::ExitWitness successor{};bool denied=true;auto available=runtime.budget();
        CHECK(!runtime.native_egress_candidate_funded(available,me,{3,0},successor,denied)&&!denied);
        CHECK(successor.prices_complete()&&successor.tag.origin==N::Point{3,0}&&successor.steps==3);
        CHECK(successor.ranged_phases==18&&successor.max_origin_phase==12); // Conservative inherited refusal maximum.
        quote_history_is_unchanged(runtime,before);
        me.hp=31;me.shield=0;runtime.use(me);
        CHECK(runtime.native_egress_candidate_funded(available,me,{3,0},successor,denied)&&!denied);
        CHECK(successor.tag.epoch==me.epoch&&successor.path[0]==N::Point{2,0});
        CHECK(successor.tag.field_generation==incumbent.tag.field_generation&&successor.tag.semantic_intent==runtime.semantic);
        quote_history_is_unchanged(runtime,before);
        CHECK(runtime.facts.self.raw_m&&runtime.state.route.custody_owed()&&runtime.state.route.custody()==26);
        CHECK(runtime.state.egress.outstanding()&&runtime.state.cycle.restore_required());
        CHECK(available.queries==0&&available.commands==0&&!available.poisoned);
    }
    ++groups;
}

static void alternate_egress_prices_landing_suffix_and_current_origin_refusal(){
    for(unsigned scenario=0;scenario<2;++scenario){
        FakeRuntime runtime;runtime.warm();auto me=self(scenario?N::Point{2,0}:N::Point{3,0},scenario?27.0f:18.0f);
        me.gems=26;me.shield=1000000;runtime.seed_work(me);
        // Even actual selected Reward never discounts this full-source exit.
        runtime.members={{{3,0},0,9}};runtime.memory={{0,1}};runtime.groups={handle_camp_group_t(0)};
        runtime.facts.group=reward(me.epoch);
        const N::Point to=scenario?N::Point{3,0}:N::Point{2,0};
        const auto before=quote_history(runtime);N::ExitWitness successor{};bool denied=true;auto available=runtime.budget();
        CHECK(!runtime.native_egress_candidate_funded(available,me,to,successor,denied)&&!denied);
        CHECK(successor.prices_complete()&&successor.tag.origin==to);
        if(scenario)CHECK(successor.ranged_phases==18&&successor.max_origin_phase==9); // 18+max(6,9)=27.
        else CHECK(successor.ranged_phases==9&&successor.max_origin_phase==6); // 9+max(9,6)=18, not15.
        quote_history_is_unchanged(runtime,before);
        ++me.hp;me.shield=0;runtime.use(me);
        CHECK(runtime.native_egress_candidate_funded(available,me,to,successor,denied)&&!denied);
        quote_history_is_unchanged(runtime,before);
        CHECK(runtime.state.route.custody_owed()&&available.commands==0&&available.queries==0&&!available.poisoned);
    }
    ++groups;
}

static void egress_candidate_uses_ready_origins_without_global_completion(){
    FakeRuntime runtime;auto me=self({2,0},9);me.gems=26;runtime.seed_work(me);
    const std::array<N::Point,1> roots{{{0,0}}};
    CHECK(runtime.state.fields.begin(N::ServiceField::egress,runtime.width,1,
        {runtime.board.data(),std::size_t(runtime.width)},roots,runtime.semantic));
    for(unsigned i=0;i<16&&!runtime.state.fields.available(N::ServiceField::egress,me.pose);++i)
        runtime.state.fields.step(N::ServiceField::egress,1);
    CHECK(runtime.state.fields.status(N::ServiceField::egress)==N::NavigationStatus::preparing);
    CHECK(runtime.state.fields.available(N::ServiceField::egress,me.pose)&&runtime.state.fields.available(N::ServiceField::egress,{1,0}));
    const auto before=quote_history(runtime);N::ExitWitness successor{};bool denied=true;auto available=runtime.budget();
    CHECK(!runtime.native_egress_candidate_funded(available,me,{1,0},successor,denied)&&!denied);
    CHECK(successor.prices_complete()&&successor.steps==1&&successor.ranged_phases==3&&successor.max_origin_phase==3);
    ++me.hp;runtime.use(me);CHECK(runtime.native_egress_candidate_funded(available,me,{1,0},successor,denied)&&!denied);
    CHECK(runtime.state.fields.status(N::ServiceField::egress)==N::NavigationStatus::preparing);
    quote_history_is_unchanged(runtime,before);
    CHECK(available.queries==0&&available.commands==0&&!available.poisoned);
    ++groups;
}

static void egress_candidate_cache_dependencies_never_replace_the_incumbent(){
    FakeRuntime runtime;runtime.warm();auto me=self();me.gems=26;runtime.seed_work(me);
    const auto incumbent=runtime.native_exit_witness(me.epoch,me.pose);const auto before=quote_history(runtime);
    runtime.state.fields.retire(N::ServiceField::egress);
    N::ExitWitness successor{};bool denied=true;auto available=runtime.budget();
    CHECK(!runtime.native_egress_candidate_funded(available,me,{3,0},successor,denied)&&!denied&&!successor.prices_complete());
    quote_history_is_unchanged(runtime,before);
    runtime.state.exit_witness_retained=true;const auto retained=quote_history(runtime);
    CHECK(runtime.native_egress_candidate_funded(available,me,{3,0},successor,denied)&&!denied);
    CHECK(successor.tag.field_generation==incumbent.tag.field_generation);
    quote_history_is_unchanged(runtime,retained);
    runtime.state.exit_witness_retained=false;runtime.warm();const auto regenerated=quote_history(runtime);
    const auto source_before=runtime.source_calls;
    CHECK(runtime.native_egress_candidate_funded(available,me,{3,0},successor,denied)&&!denied);
    CHECK(successor.tag.field_generation!=incumbent.tag.field_generation&&runtime.source_calls>source_before+2);
    quote_history_is_unchanged(runtime,regenerated);
    ++runtime.semantic;
    CHECK(!runtime.native_egress_candidate_funded(available,me,{3,0},successor,denied)&&!denied);
    runtime.warm();const auto rekeyed=quote_history(runtime);const auto before_rekey_sources=runtime.source_calls;
    CHECK(runtime.native_egress_candidate_funded(available,me,{3,0},successor,denied)&&!denied);
    CHECK(successor.tag.semantic_intent==runtime.semantic&&runtime.source_calls>before_rekey_sources+2);
    quote_history_is_unchanged(runtime,rekeyed);
    CHECK(available.queries==0&&available.commands==0&&!available.poisoned);
    ++groups;
}

static void egress_candidate_denial_keeps_the_owned_cash_and_recovery_continuation(){
    for(unsigned scenario=0;scenario<3;++scenario){
        FakeRuntime runtime;runtime.warm();auto me=self();me.gems=26;runtime.seed_work(me);
        if(scenario)CHECK(runtime.native_exit_witness(me.epoch,me.pose).prices_complete());
        const auto before=quote_history(runtime);N::ExitWitness successor{};bool denied=false;
        if(scenario==1)runtime.remaining=44000;
        auto available=runtime.budget();available.reserve(777);
        if(scenario==0)runtime.remaining-=14001; // Slow preparation has less than the existing6000pure allowance.
        if(scenario==2)runtime.remaining-=20001; // Already over the unchanged whole-input pure bound.
        CHECK(!runtime.native_egress_candidate_funded(available,me,{3,0},successor,denied)&&denied);
        CHECK(available.held_tail==777&&available.queries==0&&available.commands==0);
        CHECK(available.poisoned==(scenario==2));
        quote_history_is_unchanged(runtime,before);
        CHECK(runtime.state.route.custody_owed()&&runtime.state.route.custody()==26);
        CHECK(runtime.state.egress.outstanding()&&runtime.state.cycle.restore_required());
    }
    // An unavailable suffix, a non-adjacent nominee and unavailable M are typed
    // unsupported candidates, not invented funding failures or actual movement.
    FakeRuntime runtime;auto me=self();me.gems=26;runtime.seed_work(me);const auto before=quote_history(runtime);
    N::ExitWitness successor{};bool denied=true;auto available=runtime.budget();
    CHECK(!runtime.native_egress_candidate_funded(available,me,{3,0},successor,denied)&&!denied);
    runtime.warm();CHECK(!runtime.native_egress_candidate_funded(available,me,{2,0},successor,denied)&&!denied);
    me.raw_m=false;runtime.use(me);CHECK(!runtime.native_egress_candidate_funded(available,me,{3,0},successor,denied)&&!denied);
    quote_history_is_unchanged(runtime,before);
    CHECK(available.commands==0&&available.queries==0&&!available.poisoned);
    ++groups;
}

int main(){
    hp_alone_pays_the_current_work_origin_phase();ingress_prices_the_prospective_origin_and_preserves_hp_boundary();
    fresh_reward_absence_is_local_and_does_not_wait_for_cold_geometry();transfer_needs_actual_rank_a_route_and_current_word_tail();
    current_move_and_explicit_ranged_next_input_class_are_distinct();quiet_closure_and_actual_restoration_remain_separate();
    cached_static_proof_obeys_origin_real_generation_and_intent();
    explicitly_retained_path_trims_only_accepted_matching_next_edges();normal_retirement_or_a_declared_path_block_never_reuses_old_generation();
    capture_renews_the_actual_owner_only_after_full_wait_and_exit_price();capture_tail_denial_cannot_claim_the_prior_work_owner_as_capture();
    capture_deadline_is_fixed_and_expiry_demands_reobservation();capture_needs_actual_reward_with_the_exact_catalogue_binding();
    actual_respawning_before_the_deadline_does_not_mint_completion();
    capture_holding_requires_fresh_complete_matching_evidence();unknown_spawn_holding_keeps_an_explicit_zero_wait_approach();
    actual_leaving_ends_the_selected_absence_wait_price();
    cached_egress_suffix_is_only_a_quote_with_the_full_hp_boundary();
    alternate_egress_prices_landing_suffix_and_current_origin_refusal();
    egress_candidate_uses_ready_origins_without_global_completion();
    egress_candidate_cache_dependencies_never_replace_the_incumbent();
    egress_candidate_denial_keeps_the_owned_cash_and_recovery_continuation();
    std::printf("PASS native_service_finite_work: %u checks, %u groups\n",checks,groups);
}
