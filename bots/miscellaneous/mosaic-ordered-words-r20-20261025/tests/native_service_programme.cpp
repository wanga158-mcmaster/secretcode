#include "../src/native_service_arrival.h"
#include "../src/native_service_execution.h"
#include "../src/native_service_macro.h"
#include "../src/native_service_egress.h"
#include "../src/native_service_route_owner.h"
#include "../src/native_service_cycle.h"
#include "../src/native_service_fields.h"
#include "../src/native_service_exit_witness.h"
#include "../src/native_service_group_frontier.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace N=mosaic::native_service;
namespace native_service=mosaic::native_service;
static unsigned checks{},test_groups{};
static void check(bool value,const char* expression,int line){
    ++checks;
    if(!value){std::fprintf(stderr,"programme check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)

// This is an actual-fragment integration fixture, not a whole-controller proof.
// Programme/arrival, ServiceFields, GroupFrontier, CycleGroupWork, CycleState and the route/egress/custody/
// execution-state reducers are production code. SDK reads/receipts, the board
// and explicit ingress/work/capture/finite-coverage feasibility helpers are seams.
// These flags do not qualify the production exit-witness math, source support,
// native fuel or physics. Separate component tests/native games own that proof.
// Navigation preparation below is test setup, never a fake fuel measurement.
using handle_camp_t=std::uint32_t;
using handle_camp_group_t=std::uint32_t;
enum class error_code : int {api_not_found=94,api_not_in_vision=97};
namespace constants {inline constexpr int submit_gems_range=1;}
namespace detail {
struct altar_lock_info {std::uint32_t handle{};};
struct Lock {N::Point point{};int code{94};std::uint32_t handle{};};
inline std::array<Lock,8> locks{};
inline std::array<N::Point,8> queried{};
inline std::array<int,8> queried_team{};
inline unsigned lock_count{},lock_calls{};
inline std::uint64_t* remaining{};
inline int sdk_altar_lock(int x,int y,int team,altar_lock_info* out){
    CHECK(lock_calls<queried.size());queried[lock_calls]={x,y};queried_team[lock_calls++]=team;
    CHECK(remaining&&*remaining>=50);*remaining-=50;
    for(unsigned i=0;i<lock_count;++i)if(locks[i].point==N::Point{x,y}){
        out->handle=locks[i].handle;return locks[i].code;
    }
    return int(error_code::api_not_found);
}
inline void check(int code){if(code)throw code;}
}

static N::SelfFact body(N::Epoch epoch={40,9,0},N::Point pose={10,10},int gems=0){
    N::SelfFact value{};value.epoch=epoch;value.id=4;value.pose=pose;value.team=0;
    value.hp=value.max_hp=100;value.alive=value.raw_m=value.raw_a=value.raw_c=true;
    value.attack_range=value.move_speed=1;value.strength=20;value.gems=gems;
    value.body_actual=value.restrictions_actual=value.buffs_actual=value.channel_actual=true;
    value.walked_actual=value.channel_time_actual=value.gems_actual=value.custody_actual=true;
    return value;
}
struct FakeMember {N::Point pos{};handle_camp_t handle{};int group{-1};};
struct FakeMemory {std::size_t begin{},end{};};

struct FakeState {
    N::ExecutionState execution{};N::EgressState egress{};N::RouteOwner route{};N::ServiceState service{};
    N::CycleState cycle{};N::ServiceFields<2048> fields{};
    N::GroupFrontier<2048> frontier{};N::CycleGroupWork<128> group_work{};
    std::array<N::FrontierMember,128> frontier_members{};
    std::uint64_t group_generation{1},board_generation{1},member_binding_serial{};bool group_goal{};
    N::ExitWitness exit_witness{};bool exit_witness_retained{};
    N::ServiceField active_field{N::ServiceField::service};
    std::array<std::uint8_t,2048> passable{};
    std::uint64_t route_tokens{},funding_serial{},service_event{},command_owner{},bank_started_owner{},batch_owner{},last_ack{};
    std::size_t arrival_work_cursor{},arrival_bank_cursor{};
    std::array<int,256> unavailable_until{};
    std::array<N::Point,256> roots{};
    std::array<N::Point,8> moves{},excluded{};
    unsigned root_count{},move_count{},excluded_count{},stage{},move_reason{},board_edit_count{},last_status{};
    unsigned work_commands{},move_commands{},bank_starts{},recall_starts{},ready_inputs{};
    unsigned rank_decreases{},work_debts{},egress_closures{},coverage_transfers{},restorations{};
    std::uint64_t intent{},goal_key{},nav_intent{},field_revision{1},rejection_serial{};
    N::Point exit_anchor{8,10},approach_goal{};unsigned approach_cursor{};int approach_range{};
    int group{-1},member{-1},bank_altar{-1},blocker_member{-1},input_round{-1},last_gems{};
    int capture_group{-1},capture_deadline{-1};bool capture_reobserve{};
    bool recovering{},exit_due{},board_ready{},approach_prepared{},bank_roots_materialized{};
};
struct Command {
    N::Primitive action{};N::RouteGoal owner{};N::ServiceJob job{};
    N::Epoch before{},after{};N::Point from{};
    bool debt_before{},debt_after{},channel_after{};
};
struct FakeRuntime {
    N::Facts facts{};N::SelfFact world{};FakeState state{};
    std::vector<FakeMember> members;std::vector<handle_camp_group_t> groups;
    std::vector<FakeMemory> memory;std::vector<N::Point> altars;
    std::vector<N::CampFact> camps;std::vector<N::GroupFact> lives;
    std::array<Command,32> commands{};unsigned command_count{},self_reads{},camp_reads{},group_reads{},goal_calls{},notes{},feedback_calls{},region_calls{};
    std::array<int,64> camp_read_order{};
    std::array<N::Point,8> changed_points{},move_validation_points{};
    unsigned changed_point_count{},move_validation_count{},egress_candidate_checks{};
    std::uint64_t changed_pure_cost{},changed_serial_before{},changed_serial_after{};
    bool changed_raw_m_before{},changed_raw_m_after{},egress_exit_funded{true};
    N::ExitWitness changed_incumbent{};
    unsigned ingress_checks{},work_checks{},cover_checks{},exit_witness_calls{};bool last_cover_decreased{};
    unsigned capture_checks{};N::RouteGoal capture_owner_seen{};N::GroupFact capture_group_seen{};N::CaptureFact capture_seen{};
    std::uint64_t remaining{500000},self_pure_cost{};int epoch{40};float camp_taken{20};
    bool exposure_enabled{},at_home{},ingress_exit_funded{true},work_exit_funded{true},finite_cover_funded{};
    bool capture_exit_funded{true},capture_denial_requires_reobserve{};
    bool index_enabled{},group_fact_available{true},region_empty{};int board_width{40},board_height{32};

    FakeRuntime(){
        facts.epoch={epoch,9,0};world=body(facts.epoch);facts.self=world;
        detail::lock_count=detail::lock_calls=0;detail::remaining=&remaining;
        state.passable.fill(1);
        prepare_field(N::ServiceField::egress,state.exit_anchor,native_egress_key());
    }
    static std::uint64_t clock(void* context){return static_cast<FakeRuntime*>(context)->remaining;}
    N::Budget budget(){N::Budget value{this,clock,remaining};value.pure_bound=20000;return value;}
    FakeState& native_service_state(){return state;}
    const FakeState& native_service_state()const{return state;}
    N::Facts& native_facts(){return facts;}
    const N::Facts& native_facts()const{return facts;}
    bool native_runtime_commit_known(std::uint64_t serial)const{return serial&&serial<=state.last_ack;}
    const N::Navigation<2048>& native_navigation()const{return state.fields.navigation(state.active_field);}
    static constexpr std::uint64_t native_egress_key(){return 0x0545475245535301ull;}
    void prepare_field(N::ServiceField field,N::Point root,std::uint64_t intent){
        CHECK(state.fields.begin(field,board_width,board_height,state.passable,{&root,1},intent));
        const auto progress=state.fields.step(field,4096);
        CHECK(progress.status==N::NavigationStatus::ready);
    }
    void native_begin_input(){facts.begin(epoch);world.epoch=facts.epoch;}
    bool query(N::Budget& budget){
        if(!budget.before_query())return false;
        CHECK(remaining>=50);remaining-=50;return budget.after_query();
    }
    bool native_read_self(N::Budget& budget,N::SelfFact& out,bool,bool){
        ++self_reads;if(!query(budget))return false;
        CHECK(remaining>=self_pure_cost);remaining-=self_pure_cost;out=world;out.epoch=facts.epoch;return true;
    }
    bool native_read_camp(N::Budget& budget,int ordinal,N::CampFact& out){
        CHECK(camp_reads<camp_read_order.size());camp_read_order[camp_reads++]=ordinal;
        if(!query(budget))return false;
        CHECK(ordinal>=0&&std::size_t(ordinal)<camps.size());out=camps[std::size_t(ordinal)];out.epoch=facts.epoch;return true;
    }
    bool native_read_group(N::Budget& budget,int ordinal,N::GroupFact& out,int witness){
        ++group_reads;if(!query(budget))return false;
        CHECK(witness>=0&&std::size_t(witness)<lives.size());out=lives[std::size_t(witness)];
        CHECK(out.ordinal==ordinal);out.epoch=facts.epoch;
        if(!group_fact_available){out.actual=false;out.visible=false;out.life=N::GroupLife::unknown;}
        return true;
    }
    // These are explicit group-runtime seams over the production pure
    // reducers. They do not qualify the controller's lifecycle/timer/index
    // acquisition or staged geometry/fuel producer.
    bool native_group_configure(){
        if(state.group<0||std::size_t(state.group)>=groups.size())return false;
        const auto& memo=memory[std::size_t(state.group)];
        const auto configured=state.group_work.configure({groups[std::size_t(state.group)],state.group,
            state.group_generation,int(memo.begin),memo.end-memo.begin});
        return configured==N::CycleGroupConfigure::configured||configured==N::CycleGroupConfigure::retained;
    }
    void native_group_routes_changed(){
        state.frontier.retire();
        if(state.group_goal){state.fields.retire(N::ServiceField::service);state.nav_intent=0;state.field_revision=0;}
    }
    void native_group_clock(N::Epoch now){if(state.group_work.refresh_time(now))native_group_routes_changed();}
    N::CycleMemberAbsence native_group_absence(const N::CampFact& camp,const N::GroupFact& group)const{
        if(camp.life!=N::Life::dead||!camp.actual(facts.epoch)||!group.current(facts.epoch)||
           group.id!=camp.group_id||group.ordinal!=camp.group_ordinal||group.ordinal!=state.group||
           (group.life!=N::GroupLife::alive&&group.life!=N::GroupLife::reward))return {};
        // Controlled finite immutable-respawn seam; no actual timer SDK query
        // or permanent remembered-dead exclusion is asserted by this fixture.
        return {state.group_generation,epoch+30,true};
    }
    void native_group_observe(const N::CampFact& camp,const N::GroupFact& group={}){
        if(!native_group_configure())return;
        const auto revision=state.group_work.route_revision();
        state.group_work.observe(camp,facts.epoch,native_group_absence(camp,group));
        if(revision!=state.group_work.route_revision())native_group_routes_changed();
    }
    bool native_arrival_member_needed(int ordinal,N::Epoch now)const{
        return !state.group_work.configured()||state.group_work.member_needed(ordinal,now);
    }
    bool native_arrival_index_ready()const{return index_enabled;}
    int native_arrival_member_at(N::Point point)const{
        for(std::size_t ordinal=0;ordinal<members.size();++ordinal)
            if(members[ordinal].pos==point)return int(ordinal);
        return -1; // Explicit lookup seam, not stationary-index/native cost evidence.
    }
    std::uint64_t native_group_field_key()const{
        return state.frontier.generation()?(state.frontier.generation()<<8)|state.frontier.batch():0;
    }
    bool native_service_field_matches()const{
        const auto expected=state.group_goal?native_group_field_key():state.goal_key;
        return expected&&state.fields.semantic_intent(N::ServiceField::service)==expected;
    }
    void native_capture_at(const N::SelfFact& me,const N::GroupFact& group,int ordinal,N::CaptureFact& out)const{
        out={};out.epoch=facts.epoch;out.group_id=group.id;out.life=group.life;out.life_actual=group.current(facts.epoch);
        out.on_spawn_actual=me.actual(facts.epoch);out.on_spawn=out.on_spawn_actual&&me.pose==members[std::size_t(ordinal)].pos;
    }
    void native_note_camp_body(N::Point,bool,N::Epoch={}){++notes;}
    void native_note_group_lifecycle(const N::GroupFact&){++notes;}
    struct ShopResult {bool attempted{};};
    ShopResult native_shop_step(N::Budget&,const N::SelfFact&){return {};}
    void native_reconcile_feedback(N::Budget&,N::ExecutionState&){++feedback_calls;}
    bool native_at_home(N::Point)const{return at_home;}
    std::uint64_t native_potential(N::Point point)const{
        return exposure_enabled&&point.x>8?std::uint64_t(point.x-8):0;
    }
    bool native_current_quiet(const N::SelfFact& me)const{return native_potential(me.pose)==0;}
    void native_cycle_actual(const N::SelfFact& me){
        const auto update=state.cycle.observe_actual(me,{me.epoch,me.pose,true,native_current_quiet(me)},
            {me.epoch,me.pose,true,native_at_home(me.pose)});
        if(update==N::CycleUpdate::restored)++state.restorations;
    }
    // Caller-selected feasibility is an explicit seam. It grants no raw SDK
    // permission and does not price a multi-round programme. Quiet ingress is
    // independent of the hazardous-exit flag, as in the production helper.
    bool native_ingress_funded(N::Budget&,const N::SelfFact&,N::Point to){
        ++ingress_checks;return native_potential(to)==0||ingress_exit_funded;
    }
    bool native_work_funded(N::Budget&,const N::SelfFact& me){
        ++work_checks;return native_current_quiet(me)||work_exit_funded;
    }
    bool native_capture_funded(N::Budget&,const N::SelfFact& me,const N::GroupFact& group,const N::CaptureFact& capture){
        ++capture_checks;capture_owner_seen=state.route.goal();capture_group_seen=group;capture_seen=capture;
        // This is a caller-selected feasibility producer, not a deadline/HP
        // model. The actual programme must bind Capture before requesting it.
        CHECK(state.route.active()&&capture_owner_seen.purpose==N::RoutePurpose::capture);
        CHECK(capture_owner_seen.group==group.id&&group.current(facts.epoch));
        if(!capture_exit_funded){
            if(capture_denial_requires_reobserve){state.capture_group=group.ordinal;state.capture_deadline=epoch;state.capture_reobserve=true;}
            return false;
        }
        if(!state.egress.outstanding())return true;
        const auto& latest=state.egress.latest();
        const auto update=state.egress.transfer({me.epoch,me.pose,latest.serial,latest.accepted_prefix,
            capture_owner_seen.token,true,true},{me.epoch,me.pose,me.actual(facts.epoch)});
        if(update==N::EgressUpdate::transferred){++state.coverage_transfers;return true;}
        return false;
    }
    const N::ExitWitness& native_exit_witness(N::Epoch now,N::Point origin){
        ++exit_witness_calls;auto& witness=state.exit_witness;witness={};
        witness.tag={now,origin,state.fields.generation(N::ServiceField::egress),native_egress_key()};
        if(!native_potential(origin)){witness.status=N::ExitWitnessStatus::quiet;return witness;}
        // Explicit deterministic route seam for this fixture's x>8 source.
        // The real witness type/next-edge contract is exercised, but the
        // production source-support and phase-price arithmetic are not mocked
        // into a claimed proof. Their feasibility is the separate flags above.
        auto cursor=origin;const auto& field=state.fields.navigation(N::ServiceField::egress);
        while(native_potential(cursor)&&witness.steps<N::ExitWitness::max_steps){
            const N::Point next{cursor.x-1,cursor.y+(state.exit_anchor.y>cursor.y)-(state.exit_anchor.y<cursor.y)};
            const auto before=field.distance(cursor),after=field.distance(next);
            CHECK(before&&after&&*after<*before);witness.path[witness.steps++]=next;cursor=next;
        }
        witness.status=native_potential(cursor)?N::ExitWitnessStatus::unsupported:N::ExitWitnessStatus::path;
        return witness;
    }
    bool native_egress_candidate_funded(N::Budget& budget,const N::SelfFact& me,N::Point to,
                                        N::ExitWitness& successor,bool& funding_denied){
        ++egress_candidate_checks;successor={};funding_denied=false;
        if(!budget.checkpoint()){funding_denied=true;return false;}
        const auto& incumbent=state.exit_witness;
        const bool cached=incumbent.prices_complete()&&incumbent.tag.origin==me.pose&&incumbent.next_point()==std::optional<N::Point>{to};
        if(!cached&&budget.pure_used()+6000>budget.pure_bound){funding_denied=true;return false;}
        // Whole exit feasibility is an explicit fixture seam. The actual
        // Navigation/ExitWitness geometry is retained; this simple source is
        // not the production union, native-fuel proof or fresh SDK authority.
        const auto& field=state.fields.navigation(N::ServiceField::egress);
        successor=N::build_exit_witness(field,{me.epoch,to,state.fields.generation(N::ServiceField::egress),native_egress_key()},
            [&](N::Point point){return N::RawCampPotential{native_potential(point),true};});
        if(!egress_exit_funded||!successor.prices_complete())return false;
        const auto phases=N::Budget::add(successor.ranged_phases,std::max(native_potential(me.pose),successor.max_origin_phase));
        if(double(me.hp)<=double(phases))return false;
        const auto tail=N::word_tail(N::single_primitive({N::PrimitiveKind::move,0,to,0}),0,budget,0,true);
        if(!budget.admit(N::Budget::add(budget.held_tail,tail))){funding_denied=true;return false;}
        return true;
    }
    bool native_cover_work(N::Budget&,const N::SelfFact& me,bool decreased){
        ++cover_checks;last_cover_decreased=decreased;
        if(!state.egress.outstanding())return true;
        if(native_current_quiet(me))return native_programme_close_egress(me);
        if(!finite_cover_funded||!work_exit_funded||!decreased||!state.route.active())return false;
        state.batch_owner=state.route.goal().token;const auto& latest=state.egress.latest();
        const auto update=state.egress.transfer({me.epoch,me.pose,latest.serial,latest.accepted_prefix,
            state.batch_owner,true,true},{me.epoch,me.pose,me.actual(facts.epoch)});
        if(update==N::EgressUpdate::transferred){++state.coverage_transfers;return true;}
        return false;
    }
    bool native_move_terrain(N::Point from,N::Point to)const{return N::distance(from,to)==1;}
    void native_region(N::Point point,int){++region_calls;if(!region_empty){state.roots[0]=point;state.root_count=1;}}
    int native_choose_group(const N::SelfFact&){return state.group;}
    void native_goal(unsigned kind,const N::SelfFact& me){
        ++goal_calls;state.group_goal=false;state.active_field=kind==4?N::ServiceField::egress:N::ServiceField::service;
        state.goal_key=kind==4?native_egress_key():(std::uint64_t(kind+1)<<56)^state.intent;
        N::Point target=me.pose;
        if(kind==4)target=state.exit_anchor;
        else if(kind==1&&state.member>=0)target=members[std::size_t(state.member)].pos;
        else if(kind==2&&!altars.empty()){state.bank_altar=0;target=altars[0];}
        native_region(target,0);state.board_ready=true;
        // A Recall goal must not rebuild/overwrite the warm service context.
        // Actual geometry preparation is outside the included body fragment.
        state.nav_intent=state.fields.semantic_intent(state.active_field)==state.goal_key?state.goal_key:0;
        state.field_revision=state.fields.generation(state.active_field);
    }
    void add_camp(N::Point point,int group=0,N::Life life=N::Life::awake){
        CHECK(group>=0);const auto ordinal=int(members.size());
        while(groups.size()<=std::size_t(group)){groups.push_back(7+unsigned(groups.size()));memory.push_back({members.size(),members.size()});}
        const auto id=20+unsigned(ordinal);members.push_back({point,id,group});memory[std::size_t(group)].end=members.size();
        CHECK(std::size_t(ordinal)<state.frontier_members.size());state.frontier_members[std::size_t(ordinal)]={point,ordinal};
        N::CampFact camp{};camp.epoch=facts.epoch;camp.id=id;camp.group_id=groups[std::size_t(group)];
        camp.member_ordinal=ordinal;camp.group_ordinal=group;camp.pose=point;camp.hp=camp.max_hp=120;
        camp.life=life;camp.life_actual=camp.body_actual=camp.buffs_actual=true;camps.push_back(camp);
        N::GroupFact group_fact{};group_fact.epoch=facts.epoch;group_fact.id=camp.group_id;group_fact.ordinal=group;
        group_fact.life=N::GroupLife::alive;group_fact.actual=group_fact.visibility_actual=group_fact.visible=true;lives.push_back(group_fact);
    }
    void next_input(){
        ++epoch;world.raw_m=world.raw_a=world.raw_c=true;world.ordinary_walked=false;remaining=500000;
    }
    void run(){auto available=budget();native_service_body(available);CHECK(!available.poisoned&&!available.in_span);}

    bool native_dispatch(N::Budget& budget,const N::Word& word){
        using namespace N;
        if(!word.count)return false;
        CHECK(word.count==1&&command_count<commands.size()&&state.route.active());
        const auto action=word.actions[0];const auto before=facts.self;const auto owner=state.route.goal();
        CHECK(before.actual(facts.epoch));
        if(action.kind==PrimitiveKind::move){
            CHECK(move_validation_count<move_validation_points.size());move_validation_points[move_validation_count++]=action.point;
            for(unsigned i=0;i<changed_point_count;++i)if(changed_points[i]==action.point){
                changed_raw_m_before=world.raw_m;changed_serial_before=state.execution.serial;
                changed_incumbent=state.exit_witness;
                CHECK(state.route.reject({owner.token,++state.rejection_serial,facts.epoch,before.pose,action.point,N::RouteRejectionKind::camp,false}));
                CHECK(remaining>=changed_pure_cost);remaining-=changed_pure_cost;
                changed_raw_m_after=world.raw_m;changed_serial_after=state.execution.serial;
                state.last_status=unsigned(N::ExecutionStatus::changed);return false;
            }
        }
        if(!budget.before_command())return false;
        CHECK(remaining>=50);remaining-=50;CHECK(budget.after_command());
        const auto serial=++state.execution.serial;
        const bool debt_before=state.egress.outstanding();
        facts.attempted(effect_scope(action,true,true));world=before;world.epoch=facts.epoch;
        if(action.kind==PrimitiveKind::move){
            CHECK(state.move_count==1&&state.moves[0]==action.point);
            world.pose=action.point;world.raw_m=false;world.ordinary_walked=true;++state.move_commands;
        }
        else if(action.kind==PrimitiveKind::basic){
            world.raw_a=false;++state.work_commands;
            bool matched=false;
            for(auto& camp:camps)if(camp.id==action.target_id){
                camp.hp=std::max(0.0f,camp.hp-camp_taken);if(camp.hp==0)camp.life=Life::dead;
                matched=true;break;
            }
            CHECK(matched); // This mutation stands in for the actual engine result.
        }
        else if(action.kind==PrimitiveKind::bank){world.submitting=world.submit_started=true;++state.bank_starts;}
        else if(action.kind==PrimitiveKind::recall){world.recalling=world.recall_started=true;++state.recall_starts;}
        else CHECK(false);
        facts.self=world;
        Receipt receipt{};receipt.serial=serial;receipt.action=action;receipt.before_epoch=before.epoch;receipt.after_epoch=world.epoch;
        receipt.before_pose=before.pose;receipt.after_pose=world.pose;receipt.applied=Truth::yes;
        receipt.attempted=receipt.returned=receipt.after_actual=true;receipt.submitting_after=world.submitting;
        state.execution.pending_receipt=receipt;state.execution.pending_recorded=false;state.execution.record();
        state.command_owner=owner.token;
        const EgressWork work{action.kind,EgressEffect::yes,serial,state.execution.accepted_prefix,before.epoch,before.pose,
            {state.group,state.member,action.target_id}};
        state.egress.observe_work(work);
        if(action.kind==PrimitiveKind::basic){
            CHECK(state.group>=0&&state.member>=0);
            const CycleJob job{groups[std::size_t(state.group)],members[std::size_t(state.member)].handle,state.group,state.member};
            const auto update=state.cycle.observe_work({work,job,!native_current_quiet(before)});
            CHECK(update==CycleUpdate::retained||update==CycleUpdate::extended);++state.work_debts;
        }
        CHECK(ack_service(state.service,serial,action.kind,ServiceApplied::yes));
        state.last_ack=serial;
        if(action.kind==PrimitiveKind::move){
            CHECK(state.route.move_applied({owner.token,serial,before.epoch,world.epoch,before.pose,world.pose,true}));
            CHECK(state.route.close_feedback({serial,true},world,native_programme_frame(world)));
        }else if(action.kind==PrimitiveKind::basic){
            CHECK(state.route.service_actual({owner.token,++state.service_event,world.epoch,RouteServiceKind::work_effect,true,true}));
        }else if(action.kind==PrimitiveKind::bank){
            state.bank_started_owner=owner.token;
            CHECK(state.route.service_actual({owner.token,++state.service_event,world.epoch,RouteServiceKind::bank_start,true,true}));
        }
        commands[command_count++]={action,owner,state.service.job,before.epoch,world.epoch,before.pose,
            debt_before,state.egress.outstanding(),world.submitting};
        state.last_status=unsigned(ExecutionStatus::complete);
        return true;
    }

    #include "../src/native_service_programme.h"
};

static void prime_work_route(FakeRuntime& runtime,int member,N::Point root){
    runtime.native_programme_job(runtime.camps[std::size_t(member)].group_ordinal,member);
    runtime.native_programme_work_goal(runtime.world,runtime.members[std::size_t(member)].pos);
    auto& state=runtime.state;state.board_ready=true;state.nav_intent=state.goal_key;
    CHECK(runtime.native_group_configure());const auto& memo=runtime.memory[std::size_t(state.group)];
    const N::FrontierIntent semantic{runtime.groups[std::size_t(state.group)],state.group,state.group_generation,
        state.board_generation,state.group_work.route_revision(),std::max(1,runtime.world.attack_range)};
    CHECK(state.frontier.begin(runtime.board_width,runtime.board_height,state.passable,
        {state.frontier_members.data()+memo.begin,memo.end-memo.begin},semantic));
    // Most legacy questions deliberately supply one deterministic geometry
    // root. The group rail regression below separately prepares the REAL
    // frontier union. Neither setup is native geometry/fuel qualification.
    runtime.prepare_field(N::ServiceField::service,root,runtime.native_group_field_key());
    state.field_revision=state.fields.generation(N::ServiceField::service);state.roots[0]=root;state.root_count=1;
    auto available=runtime.budget();
    CHECK(runtime.native_programme_bind(available,N::RoutePurpose::work,runtime.world,runtime.members[std::size_t(member)].pos));
}
static void prime_bank_route(FakeRuntime& runtime,int altar){
    auto& state=runtime.state;state.bank_altar=altar;state.goal_key=(std::uint64_t(3)<<56)^state.intent^
        (std::uint64_t(unsigned(altar+1))<<24);
    state.group_goal=false;state.active_field=N::ServiceField::service;state.board_ready=true;state.nav_intent=state.goal_key;
    const auto target=runtime.altars[std::size_t(altar)];runtime.prepare_field(N::ServiceField::service,target,state.goal_key);
    state.field_revision=state.fields.generation(N::ServiceField::service);state.roots[0]=target;state.root_count=1;
    state.bank_roots_materialized=true;
    auto available=runtime.budget();CHECK(runtime.native_programme_bind(available,N::RoutePurpose::bank,runtime.world,target));
}

static void prime_egress_route(FakeRuntime& runtime){
    CHECK(!runtime.members.empty());runtime.native_programme_job(0,0);
    auto& state=runtime.state;state.execution.serial=state.execution.accepted_prefix=state.last_ack=1;
    CHECK(N::ack_service(state.service,1,N::PrimitiveKind::basic,N::ServiceApplied::yes));
    const N::EgressWork accepted{N::PrimitiveKind::basic,N::EgressEffect::yes,1,1,{39,8,1},runtime.world.pose,
        {0,0,runtime.members[0].handle}};
    CHECK(state.egress.observe_work(accepted)==N::EgressUpdate::created);
    CHECK(state.cycle.observe_work({accepted,{runtime.groups[0],runtime.members[0].handle,0,0},true})==N::CycleUpdate::retained);
    runtime.native_cycle_actual(runtime.world);state.recovering=state.exit_due=true;
    runtime.native_goal(4,runtime.world);runtime.native_exit_witness(runtime.facts.epoch,runtime.world.pose);
    state.exit_witness_retained=true;
    auto available=runtime.budget();CHECK(runtime.native_programme_bind(available,N::RoutePurpose::recover,runtime.world,state.exit_anchor));
    CHECK(state.route.custody_owed()&&state.cycle.restore_required()&&state.egress.direct_required());
}

static void fresh_work_precedes_retained_route(){
    FakeRuntime runtime;runtime.add_camp({30,30},0);runtime.add_camp({11,10},1);
    prime_work_route(runtime,0,{29,29});const auto previous=runtime.state.route.goal();
    runtime.run();
    CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::basic);
    CHECK(runtime.commands[0].action.target_id==runtime.members[1].handle);
    CHECK(runtime.commands[0].owner.purpose==N::RoutePurpose::work&&runtime.commands[0].owner.token!=previous.token);
    CHECK(runtime.commands[0].owner.group==runtime.groups[1]&&!runtime.commands[0].owner.member_bound&&runtime.commands[0].owner.member==0);
    CHECK(runtime.commands[0].job.retained&&runtime.commands[0].job.group_ordinal==1&&runtime.commands[0].job.member_ordinal==1);
    CHECK(runtime.state.group==1&&runtime.state.member==1&&runtime.camp_reads==2&&runtime.camp_read_order[0]==1&&runtime.camp_read_order[1]==1);
    CHECK(runtime.group_reads==0&&runtime.goal_calls==0&&runtime.state.move_commands==0);
    CHECK(runtime.commands[0].debt_after); // Accepted Work publishes debt before post-body closure.
    CHECK(!runtime.state.egress.outstanding()); // Independently qualified quiet pose closes this debt.
    CHECK(runtime.state.route.active()&&runtime.state.route.service_progress()==1); // Basic is not group completion.
    CHECK(runtime.state.rank_decreases==1&&runtime.camps[1].hp==100);
    CHECK(runtime.state.cycle.retained_job().member_ordinal==1&&!runtime.state.cycle.restore_required());
    ++test_groups;
}

static void alternate_altar_and_start_are_not_completion(){
    for(int preferred_code:std::array<int,3>{94,0,97}){
        FakeRuntime runtime;runtime.world.gems=30;runtime.facts.self=runtime.world;runtime.add_camp({11,9});
        // First case mirrors a distant retained altar. Other cases require an
        // actual unlocked alternative after Locked or Unknown at the incumbent.
        runtime.altars={preferred_code==94?N::Point{20,15}:N::Point{10,11},{11,10}};
        detail::locks[0]={runtime.altars[0],preferred_code,90};detail::lock_count=1;
        prime_bank_route(runtime,0);const auto previous=runtime.state.route.goal();
        runtime.run();
        CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::bank);
        CHECK(runtime.commands[0].action.point==runtime.altars[1]&&runtime.state.bank_altar==1);
        CHECK(runtime.commands[0].owner.purpose==N::RoutePurpose::bank&&runtime.commands[0].owner.token!=previous.token);
        CHECK(runtime.commands[0].owner.target==runtime.altars[1]&&runtime.commands[0].channel_after);
        CHECK(runtime.camp_reads==0&&runtime.state.move_commands==0); // Custody owner cannot take incidental Work.
        CHECK(detail::lock_calls==(preferred_code==94?1u:2u));
        for(unsigned i=0;i<detail::lock_calls;++i)CHECK(detail::queried_team[i]==1);
        CHECK(runtime.state.service.accepted_banks==1&&runtime.state.service.bank_start_seen);
        CHECK(runtime.state.service.bank_completions==0&&runtime.state.service.banked_gems==0);
        CHECK(runtime.world.gems==30&&runtime.state.route.custody_owed()&&runtime.state.route.custody()==30);
        CHECK(runtime.state.route.active()&&runtime.state.route.service_progress()==1&&runtime.state.bank_started_owner==runtime.state.route.goal().token);
        runtime.next_input();runtime.run();
        CHECK(runtime.state.stage==5&&runtime.command_count==1&&runtime.camp_reads==0);
        CHECK(runtime.state.route.active()&&runtime.state.service.bank_completions==0&&runtime.world.gems==30);
        CHECK(!runtime.state.egress.outstanding()); // Bank START does not invent work-induced debt.
    }
    ++test_groups;
}

static void bank_root_materialization_survives_same_goal_invalidation(){
    {
        FakeRuntime runtime;runtime.world.gems=30;runtime.facts.self=runtime.world;
        runtime.altars={{20,15}};runtime.state.intent=7;
        runtime.state.group_goal=true;runtime.state.active_field=N::ServiceField::egress;
        runtime.native_programme_bank_goal(runtime.altars[0],0);
        auto& state=runtime.state;const auto key=state.goal_key;
        CHECK(!state.group_goal&&state.active_field==N::ServiceField::service&&state.bank_altar==0);
        CHECK(state.bank_roots_materialized&&state.root_count==1&&state.roots[0]==runtime.altars[0]&&runtime.region_calls==1);
        state.board_ready=true;runtime.prepare_field(N::ServiceField::service,state.roots[0],key);
        state.nav_intent=key;state.field_revision=state.fields.generation(N::ServiceField::service);
        auto available=runtime.budget();CHECK(runtime.native_programme_bind(available,N::RoutePurpose::bank,runtime.world,runtime.altars[0]));
        const auto owner=state.route.goal();
        runtime.native_programme_bank_goal(runtime.altars[0],0);
        CHECK(state.nav_intent==key&&state.root_count==1&&runtime.region_calls==1);

        // This is the explicit production invalidation boundary. The board /
        // lifecycle producer is outside this fragment; its retired borrower,
        // lost roots and false materialization bit are inputs to this test.
        state.fields.retire_all();state.nav_intent=0;state.root_count=0;state.bank_roots_materialized=false;
        CHECK(state.goal_key==key&&!state.fields.active(N::ServiceField::service));
        runtime.run();
        CHECK(state.goal_key==key&&state.bank_roots_materialized&&state.root_count==1&&state.roots[0]==runtime.altars[0]);
        CHECK(runtime.region_calls==2&&state.route.goal()==owner&&state.route.custody_owed());
        CHECK(runtime.command_count==0&&state.nav_intent==0); // Cold preparation is not a fabricated Move.

        // Geometry preparation is an explicit fixture seam. With the rebuilt
        // roots prepared, the retained actual body can issue its paid Move.
        runtime.prepare_field(N::ServiceField::service,state.roots[0],key);
        state.nav_intent=key;state.field_revision=state.fields.generation(N::ServiceField::service);
        const auto before=N::distance(runtime.world.pose,runtime.altars[0]);
        runtime.next_input();runtime.run();
        CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::move);
        CHECK(N::distance(runtime.world.pose,runtime.altars[0])<before&&state.route.goal()==owner);
        CHECK(state.route.physical_edges()==1&&state.route.service_progress()==0&&runtime.region_calls==2);
        CHECK(runtime.world.gems==30&&state.service.bank_completions==0&&!state.egress.outstanding());
    }
    {
        FakeRuntime runtime;runtime.region_empty=true;runtime.altars={{20,15},{21,15}};
        runtime.native_programme_bank_goal(runtime.altars[0],0);
        const auto key=runtime.state.goal_key;
        CHECK(runtime.state.bank_roots_materialized&&runtime.state.root_count==0&&runtime.region_calls==1);
        runtime.native_programme_bank_goal(runtime.altars[0],0);
        CHECK(runtime.state.goal_key==key&&runtime.state.bank_roots_materialized&&runtime.state.root_count==0&&runtime.region_calls==1);
        runtime.state.bank_roots_materialized=false;
        runtime.native_programme_bank_goal(runtime.altars[0],0);
        CHECK(runtime.state.goal_key==key&&runtime.state.bank_roots_materialized&&runtime.state.root_count==0&&runtime.region_calls==2);
        runtime.native_programme_bank_goal(runtime.altars[1],1);
        CHECK(runtime.state.goal_key!=key&&runtime.state.bank_altar==1&&runtime.state.bank_roots_materialized);
        CHECK(runtime.state.root_count==0&&runtime.region_calls==3);
    }
    ++test_groups;
}

static void excluded_cached_egress_uses_ready_origin_alternative(){
    FakeRuntime runtime;runtime.exposure_enabled=true;runtime.world.gems=26;runtime.facts.self=runtime.world;
    runtime.add_camp({10,11});runtime.altars={{20,15}};
    auto& state=runtime.state;
    state.fields.retire(N::ServiceField::egress);const auto root=state.exit_anchor;
    CHECK(state.fields.begin(N::ServiceField::egress,runtime.board_width,runtime.board_height,state.passable,{&root,1},runtime.native_egress_key()));
    for(unsigned units=0;units<4096&&!state.fields.available(N::ServiceField::egress,runtime.world.pose);++units)
        CHECK(state.fields.step(N::ServiceField::egress,1).status!=N::NavigationStatus::failed);
    CHECK(state.fields.available(N::ServiceField::egress,runtime.world.pose));
    CHECK(state.fields.status(N::ServiceField::egress)==N::NavigationStatus::preparing);
    prime_egress_route(runtime);const auto owner=state.route.goal();const auto blocked=*state.exit_witness.next_point();
    CHECK(state.route.reject({owner.token,++state.rejection_serial,runtime.facts.epoch,runtime.world.pose,blocked,N::RouteRejectionKind::camp,false}));
    // Deliberately alias the mutable body: receipt.from binds the entry pose,
    // even when dispatch has already updated that body to the accepted landing.
    auto available=runtime.budget();CHECK(runtime.native_programme_transport(available,runtime.world,true));
    CHECK(runtime.move_validation_count==1&&runtime.move_validation_points[0]!=blocked);
    CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::move&&runtime.commands[0].action.point!=blocked);
    CHECK(state.route.goal()==owner&&state.route.raw_refusals()==0&&state.route.prevalidation_rejections()==1);
    CHECK(state.egress.direct_required()&&state.cycle.restore_required()&&state.route.custody_owed()&&runtime.world.gems==26);
    CHECK(state.execution.serial==2&&state.execution.accepted_prefix==2&&state.service.bank_completions==0);
    CHECK(state.exit_witness.prices_complete()&&state.exit_witness.tag.origin==runtime.world.pose&&state.exit_witness_retained);
    CHECK(state.fields.status(N::ServiceField::egress)==N::NavigationStatus::preparing); // Global completion was never a permission gate.
    ++test_groups;
}

static void changed_egress_keeps_raw_move_and_tries_another_funded_class(){
    FakeRuntime runtime;runtime.exposure_enabled=true;runtime.world.gems=26;runtime.facts.self=runtime.world;
    runtime.add_camp({10,11});prime_egress_route(runtime);
    auto& state=runtime.state;const auto owner=state.route.goal();const auto incumbent=state.exit_witness;
    runtime.changed_points[0]=*incumbent.next_point();runtime.changed_point_count=1;
    auto available=runtime.budget();CHECK(runtime.native_programme_transport(available,runtime.world,true));
    CHECK(runtime.move_validation_count==2&&runtime.move_validation_points[0]==runtime.changed_points[0]);
    CHECK(runtime.move_validation_points[1]!=runtime.changed_points[0]&&runtime.command_count==1&&available.commands==1);
    CHECK(runtime.changed_raw_m_before&&runtime.changed_raw_m_after&&runtime.changed_serial_before==1&&runtime.changed_serial_after==1);
    CHECK(runtime.changed_incumbent.tag==incumbent.tag&&runtime.changed_incumbent.steps==incumbent.steps&&runtime.changed_incumbent.next_point()==incumbent.next_point());
    CHECK(state.route.goal()==owner&&state.route.prevalidation_rejections()==1&&state.route.raw_refusals()==0);
    CHECK(state.execution.serial==2&&state.execution.accepted_prefix==2&&state.service.accepted_prefix==2);
    CHECK(state.egress.latest().serial==1&&state.egress.direct_required()&&state.cycle.restore_required());
    CHECK(state.route.custody_owed()&&runtime.world.gems==26&&state.service.bank_completions==0);
    CHECK(state.exit_witness.tag.origin==runtime.world.pose&&state.exit_witness_retained);
    ++test_groups;
}

static void egress_funding_denial_retains_the_next_input_continuation(){
    FakeRuntime runtime;runtime.exposure_enabled=true;runtime.world.gems=26;runtime.facts.self=runtime.world;
    runtime.add_camp({10,11});prime_egress_route(runtime);
    auto& state=runtime.state;const auto owner=state.route.goal();const auto incumbent=state.exit_witness;
    runtime.changed_points[0]=*incumbent.next_point();runtime.changed_point_count=1;runtime.changed_pure_cost=15000;
    auto available=runtime.budget();CHECK(!runtime.native_programme_transport(available,runtime.world,true));
    CHECK(!available.poisoned&&available.pure_used()==15000&&available.commands==0);
    CHECK(runtime.move_validation_count==1&&runtime.egress_candidate_checks==2&&runtime.command_count==0);
    CHECK(state.execution.serial==1&&state.execution.accepted_prefix==1&&runtime.world.raw_m);
    CHECK(state.route.goal()==owner&&state.route.prevalidation_rejections()==1&&state.route.raw_refusals()==0);
    CHECK(state.exit_witness.tag==incumbent.tag&&state.exit_witness.next_point()==incumbent.next_point());
    CHECK(state.egress.direct_required()&&state.cycle.restore_required()&&state.recovering&&state.exit_due);
    CHECK(state.route.custody_owed()&&runtime.world.gems==26);
    runtime.next_input();runtime.run();
    CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::move);
    CHECK(runtime.commands[0].action.point!=runtime.changed_points[0]&&state.route.goal()==owner);
    CHECK(runtime.move_validation_count==2&&state.route.prevalidation_rejections()==1&&state.route.raw_refusals()==0);
    CHECK(state.egress.direct_required()&&state.cycle.restore_required()&&runtime.world.gems==26);
    ++test_groups;
}

static void lower_potential_without_complete_exit_cannot_dispatch(){
    FakeRuntime runtime;runtime.exposure_enabled=true;runtime.world.gems=26;runtime.facts.self=runtime.world;
    runtime.add_camp({10,11});prime_egress_route(runtime);runtime.egress_exit_funded=false;
    const auto owner=runtime.state.route.goal();const auto incumbent=runtime.state.exit_witness;
    auto available=runtime.budget();CHECK(!runtime.native_programme_transport(available,runtime.world,true));
    CHECK(runtime.egress_candidate_checks==3&&runtime.move_validation_count==0&&runtime.command_count==0&&available.commands==0);
    CHECK(runtime.state.route.goal()==owner&&runtime.state.exit_witness.tag==incumbent.tag&&runtime.state.exit_witness.next_point()==incumbent.next_point());
    CHECK(runtime.state.egress.direct_required()&&runtime.state.cycle.restore_required()&&runtime.state.route.custody_owed());
    CHECK(runtime.world.gems==26&&runtime.world.raw_m&&runtime.state.execution.serial==1);
    ++test_groups;
}

static void full_quiet_egress_continues_to_actual_bank_start(){
    FakeRuntime runtime;runtime.exposure_enabled=true;runtime.world.gems=26;runtime.facts.self=runtime.world;
    runtime.add_camp({10,11});runtime.altars={{8,11}};prime_egress_route(runtime);
    const auto job=runtime.state.cycle.retained_job();runtime.run();runtime.next_input();runtime.run();
    CHECK(runtime.command_count==2&&runtime.commands[0].action.kind==N::PrimitiveKind::move&&runtime.commands[1].action.kind==N::PrimitiveKind::move);
    CHECK(runtime.world.pose==runtime.state.exit_anchor&&runtime.world.hp==runtime.world.max_hp);
    CHECK(runtime.state.egress.direct_required()&&runtime.state.cycle.restore_required()); // Quiet still needs actual next-input reconciliation.
    runtime.next_input();runtime.run();
    CHECK(runtime.command_count==3&&runtime.commands[2].action.kind==N::PrimitiveKind::bank&&runtime.world.submitting);
    CHECK(runtime.commands[2].owner.purpose==N::RoutePurpose::bank&&runtime.state.bank_starts==1&&runtime.state.recall_starts==0);
    CHECK(!runtime.state.egress.outstanding()&&!runtime.state.cycle.restore_required()&&runtime.state.egress_closures==1);
    CHECK(runtime.state.restorations==1&&runtime.state.cycle.retained_job()==job&&runtime.state.cycle.bank_required());
    CHECK(runtime.state.route.custody_owed()&&runtime.world.gems==26&&runtime.state.service.bank_completions==0&&runtime.state.service.banked_gems==0);
    runtime.next_input();runtime.run();
    CHECK(runtime.state.stage==5&&runtime.command_count==3&&runtime.state.cycle.bank_required()&&runtime.world.gems==26);
    ++test_groups;
}

static void egress_blocks_work_until_actual_qualified_closure(){
    FakeRuntime runtime;runtime.exposure_enabled=true;runtime.add_camp({10,11});runtime.add_camp({8,11});
    prime_work_route(runtime,0,{10,10});
    CHECK(runtime.state.egress.observe_work({N::PrimitiveKind::basic,N::EgressEffect::yes,1,1,{39,8,1},{10,10},{0,0,20}})==N::EgressUpdate::created);
    // Receipt serials are global across inputs; the seeded accepted prefix is
    // part of that same history, independent of the retained route episode.
    runtime.state.execution.serial=runtime.state.execution.accepted_prefix=1;
    CHECK(N::ack_service(runtime.state.service,1,N::PrimitiveKind::basic,N::ServiceApplied::yes));
    CHECK(runtime.state.egress.direct_required());
    runtime.run();
    CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::move);
    CHECK(runtime.commands[0].action.point.x==9&&N::distance(runtime.commands[0].action.point,{8,10})==1&&runtime.commands[0].owner.purpose==N::RoutePurpose::exit);
    CHECK(runtime.camp_reads==0&&runtime.state.work_commands==0&&runtime.state.egress.direct_required());
    CHECK(runtime.state.route.active()&&runtime.state.route.service_progress()==0);
    const auto latest=runtime.state.egress.latest();
    CHECK(runtime.state.egress.close({runtime.facts.epoch,runtime.world.pose,latest.serial,latest.accepted_prefix,
        N::EgressProducer::none,false},{runtime.facts.epoch,runtime.world.pose,true})==N::EgressUpdate::invalid);
    CHECK(runtime.state.egress.direct_required()); // One lower-exposure Move and a label do not discharge it.
    runtime.next_input();runtime.run();
    CHECK(runtime.command_count==2&&runtime.commands[1].action.kind==N::PrimitiveKind::move);
    CHECK(runtime.commands[1].action.point==N::Point{8,10}&&runtime.camp_reads==0&&runtime.state.egress.direct_required());
    CHECK(runtime.state.route.active()&&runtime.state.route.service_progress()==0);
    runtime.next_input();runtime.run();
    CHECK(runtime.command_count==3&&runtime.commands[2].action.kind==N::PrimitiveKind::basic);
    CHECK(runtime.commands[2].action.target_id==runtime.members[1].handle&&runtime.commands[2].owner.purpose==N::RoutePurpose::work);
    CHECK(!runtime.commands[2].debt_before&&runtime.commands[2].debt_after);
    CHECK(runtime.camp_reads==2&&runtime.state.work_commands==1&&!runtime.state.egress.outstanding());
    CHECK(runtime.state.route.active()&&runtime.state.route.service_progress()==2); // Exit closure plus Work effect, no group completion.
    ++test_groups;
}

static void distance_moves_and_arrival_never_complete_service(){
    FakeRuntime runtime;runtime.add_camp({13,10});runtime.camps[0].life_actual=false;
    prime_work_route(runtime,0,{12,10});const auto owner=runtime.state.route.goal();
    runtime.run();runtime.next_input();runtime.run();
    CHECK(runtime.command_count==2&&runtime.commands[0].action.kind==N::PrimitiveKind::move&&runtime.commands[1].action.kind==N::PrimitiveKind::move);
    CHECK(runtime.world.pose==N::Point{12,10}&&runtime.state.route.goal()==owner);
    CHECK(runtime.state.route.physical_edges()==2&&runtime.state.route.distance_progress()==2);
    CHECK(runtime.state.route.service_progress()==0&&runtime.state.route.active()&&runtime.state.route.need()==N::RouteNeed::arrival);
    CHECK(!runtime.state.egress.outstanding()&&runtime.state.service.accepted_prefix==2&&runtime.state.work_commands==0);
    runtime.next_input();runtime.run();
    CHECK(runtime.command_count==2&&runtime.camp_reads==1); // Resolver's fresh Unknown is reused, not redundantly re-queried.
    CHECK(runtime.state.route.active()&&runtime.state.route.service_progress()==0&&runtime.state.route.need()==N::RouteNeed::arrival);
    CHECK(runtime.state.service.bank_completions==0&&runtime.state.service.banked_gems==0);
    runtime.camps[0].life_actual=true;runtime.next_input();runtime.run();
    CHECK(runtime.command_count==3&&runtime.commands[2].action.kind==N::PrimitiveKind::basic);
    CHECK(runtime.state.route.goal()==owner&&runtime.state.route.active()&&runtime.state.route.service_progress()==1);
    CHECK(runtime.state.route.physical_edges()==2&&runtime.state.work_commands==1);
    ++test_groups;
}

static void pending_feedback_precedes_all_fresh_service(){
    FakeRuntime runtime;runtime.world.gems=30;runtime.add_camp({11,10});runtime.altars={{10,11}};
    runtime.state.execution.pending=true;runtime.run();
    CHECK(runtime.state.stage==1&&runtime.feedback_calls==1&&runtime.self_reads==0);
    CHECK(runtime.command_count==0&&runtime.camp_reads==0&&detail::lock_calls==0);
    CHECK(runtime.state.execution.pending); // The programme does not discard receipt ownership to take an arrival.
    ++test_groups;
}

static void optional_catalogue_defers_without_stealing_transport(){
    {
        FakeRuntime runtime;runtime.add_camp({13,10});runtime.add_camp({11,9},1);
        prime_work_route(runtime,0,{12,10});runtime.self_pure_cost=runtime.budget().pure_bound/2;
        runtime.run();
        // At the admitted boundary the optional search can still discover a
        // supported physical incumbent. Transport cannot displace that Work.
        CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::basic);
        CHECK(runtime.commands[0].action.target_id==runtime.members[1].handle&&runtime.camp_reads==2);
        CHECK(runtime.state.group==1&&runtime.state.member==1&&runtime.state.move_commands==0);
        CHECK(runtime.state.route.service_progress()==1&&runtime.state.route.physical_edges()==0);
    }
    {
        FakeRuntime runtime;runtime.add_camp({13,10});runtime.add_camp({11,9},1);
        prime_work_route(runtime,0,{12,10});runtime.self_pure_cost=runtime.budget().pure_bound/2+1;
        runtime.run();
        // More than half of the pure allowance is already consumed. Deferring
        // optional catalogue discovery must retain its cursor and the funded
        // physical route, without crediting the Move as service completion.
        CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::move);
        CHECK(runtime.world.pose.x==11&&N::distance(runtime.world.pose,{12,10})==1&&runtime.camp_reads==0);
        CHECK(runtime.state.arrival_work_cursor==0&&runtime.state.route.active());
        CHECK(runtime.state.route.service_progress()==0&&runtime.state.route.physical_edges()==1);
    }
    ++test_groups;
}

static void injured_quiet_recall_home_heal_and_warm_resume(){
    FakeRuntime runtime;runtime.exposure_enabled=true;runtime.finite_cover_funded=true;
    runtime.world.hp=60;runtime.facts.self=runtime.world;
    runtime.add_camp({10,11},0);runtime.add_camp({8,11},1);
    prime_work_route(runtime,0,{10,10});
    const auto service_generation=runtime.state.fields.generation(N::ServiceField::service);
    const auto service_intent=runtime.state.fields.semantic_intent(N::ServiceField::service);
    const auto egress_generation=runtime.state.fields.generation(N::ServiceField::egress);
    runtime.run();
    CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::basic);
    CHECK(runtime.state.rank_decreases==1&&runtime.state.coverage_transfers==1);
    CHECK(runtime.state.egress.outstanding()&&runtime.state.egress.transferred());
    CHECK(runtime.state.cycle.restore_required()&&runtime.state.cycle.batch_active());
    CHECK(!runtime.state.cycle.restoration_barrier()&&!runtime.state.recovering);
    const auto job=runtime.state.cycle.retained_job();
    CHECK(job.valid()&&job.group_ordinal==0&&job.member_ordinal==0);

    // Feasibility is withdrawn at the next input. It is not a threshold policy
    // in the fixture: the actual body must now keep the typed recovery owner.
    runtime.work_exit_funded=false;runtime.next_input();runtime.run();
    CHECK(runtime.command_count==2&&runtime.commands[1].action.kind==N::PrimitiveKind::move);
    CHECK(runtime.commands[1].owner.purpose==N::RoutePurpose::recover&&runtime.world.pose.x==9);
    CHECK(runtime.state.active_field==N::ServiceField::egress&&runtime.state.cycle.retained_job()==job);
    CHECK(runtime.state.fields.generation(N::ServiceField::service)==service_generation);
    CHECK(runtime.state.fields.semantic_intent(N::ServiceField::service)==service_intent);
    CHECK(runtime.state.fields.generation(N::ServiceField::egress)==egress_generation);
    CHECK(runtime.state.fields.distance(N::ServiceField::service,{10,10})==0);
    CHECK(runtime.state.egress.outstanding()&&runtime.state.cycle.restore_required());
    runtime.next_input();runtime.run();
    CHECK(runtime.command_count==3&&runtime.commands[2].action.kind==N::PrimitiveKind::move);
    CHECK(runtime.world.pose==N::Point{8,10}&&runtime.state.work_commands==1);
    CHECK(runtime.state.cycle.retained_job()==job&&runtime.state.cycle.restore_required());
    const auto camps_before_quiet=runtime.camp_reads;

    runtime.next_input();runtime.run();
    CHECK(runtime.command_count==4&&runtime.commands[3].action.kind==N::PrimitiveKind::recall);
    CHECK(runtime.commands[3].owner.purpose==N::RoutePurpose::recover&&runtime.world.recalling);
    CHECK(runtime.state.cycle.restoration_barrier()&&runtime.state.recovering);
    CHECK(!runtime.state.egress.outstanding()&&runtime.state.egress_closures==1);
    CHECK(runtime.camp_reads==camps_before_quiet&&runtime.state.work_commands==1);
    CHECK(runtime.state.group==0&&runtime.state.member==0); // Nearby group 1 cannot steal the barrier.
    CHECK(runtime.state.cycle.retained_job()==job&&runtime.state.restorations==0);
    runtime.next_input();runtime.run();
    CHECK(runtime.state.stage==5&&runtime.command_count==4&&runtime.camp_reads==camps_before_quiet);
    CHECK(runtime.state.cycle.restore_required()&&runtime.state.cycle.retained_job()==job);

    // The fixture supplies actual Recall completion and later actual healing.
    // START/absence of a channel alone never generates either observation.
    runtime.world.pose={2,10};runtime.world.recalling=runtime.world.recall_started=false;runtime.at_home=true;
    runtime.next_input();runtime.run();
    CHECK(runtime.state.stage==8&&runtime.command_count==4&&runtime.world.hp==60);
    CHECK(runtime.state.cycle.phase()==N::CyclePhase::home_heal&&runtime.state.cycle.restoration_barrier());
    CHECK(runtime.state.restorations==0&&runtime.state.work_commands==1&&runtime.camp_reads==camps_before_quiet);
    runtime.next_input();runtime.run();
    CHECK(runtime.state.stage==8&&runtime.command_count==4&&runtime.state.restorations==0);

    runtime.world.hp=runtime.world.max_hp;runtime.next_input();runtime.run();
    CHECK(runtime.state.restorations==1&&!runtime.state.cycle.restore_required());
    CHECK(!runtime.state.recovering&&!runtime.state.exit_due&&runtime.state.cycle.resume_ready());
    CHECK(runtime.state.cycle.retained_job()==job&&runtime.state.group==0&&runtime.state.member==0);
    CHECK(runtime.state.active_field==N::ServiceField::service&&runtime.state.nav_intent==runtime.state.goal_key);
    CHECK(runtime.native_service_field_matches()&&runtime.state.fields.semantic_intent(N::ServiceField::service)==service_intent);
    CHECK(runtime.state.fields.generation(N::ServiceField::service)==service_generation);
    CHECK(runtime.state.fields.generation(N::ServiceField::egress)==egress_generation);
    CHECK(runtime.command_count==5&&runtime.commands[4].action.kind==N::PrimitiveKind::move);
    CHECK(runtime.commands[4].owner.purpose==N::RoutePurpose::work&&!runtime.commands[4].owner.member_bound&&runtime.commands[4].owner.group==job.group_id);
    CHECK(runtime.state.service.job.retained&&runtime.state.service.job.member_ordinal==0);
    CHECK(runtime.state.work_commands==1&&runtime.camps[0].hp==100); // Sleeping/restore time invents no camp heal.
    ++test_groups;
}

static void hazardous_ingress_requires_the_exit_feasibility_seam(){
    FakeRuntime runtime;runtime.exposure_enabled=true;runtime.ingress_exit_funded=false;
    runtime.world.pose={8,10};runtime.facts.self=runtime.world;runtime.add_camp({13,10});
    prime_work_route(runtime,0,{12,10});const auto owner=runtime.state.route.goal();
    const auto generation=runtime.state.fields.generation(N::ServiceField::service);
    runtime.run();
    CHECK(runtime.ingress_checks>0&&runtime.ingress_checks<=3&&runtime.command_count==0);
    CHECK(runtime.world.pose==N::Point{8,10}&&runtime.state.route.goal()==owner);
    CHECK(runtime.state.route.physical_edges()==0&&runtime.state.route.service_progress()==0);
    CHECK(runtime.state.fields.generation(N::ServiceField::service)==generation);
    CHECK(!runtime.state.egress.outstanding()&&!runtime.state.cycle.retained_job().valid());
    runtime.ingress_exit_funded=true;runtime.next_input();runtime.run();
    CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::move);
    CHECK(runtime.world.pose.x==9&&runtime.state.route.goal()==owner);
    CHECK(runtime.state.route.physical_edges()==1&&runtime.state.route.service_progress()==0);
    CHECK(runtime.state.work_commands==0&&!runtime.state.egress.outstanding());
    ++test_groups;
}

static void unfunded_hazardous_work_exits_without_a_work_receipt(){
    FakeRuntime runtime;runtime.exposure_enabled=true;runtime.work_exit_funded=false;runtime.add_camp({11,10});
    prime_work_route(runtime,0,{10,10});runtime.run();
    CHECK(runtime.work_checks==1&&runtime.state.work_commands==0&&runtime.camps[0].hp==120);
    CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::move);
    CHECK(runtime.commands[0].owner.purpose==N::RoutePurpose::exit&&runtime.world.pose.x==9);
    CHECK(!runtime.state.egress.outstanding()&&!runtime.state.cycle.retained_job().valid());
    CHECK(runtime.state.work_debts==0&&runtime.state.coverage_transfers==0&&runtime.state.rank_decreases==0);
    ++test_groups;
}

static void finite_cover_retains_each_actual_prefix_but_not_zero_damage(){
    FakeRuntime runtime;runtime.exposure_enabled=true;runtime.finite_cover_funded=true;runtime.add_camp({11,10});
    prime_work_route(runtime,0,{10,10});const auto generation=runtime.state.fields.generation(N::ServiceField::service);
    runtime.run();const auto owner=runtime.state.route.goal();const auto job=runtime.state.cycle.retained_job();
    CHECK(runtime.command_count==1&&runtime.state.work_commands==1&&runtime.state.rank_decreases==1);
    CHECK(runtime.last_cover_decreased&&runtime.state.egress.replacement_owner()==owner.token);
    CHECK(runtime.state.cycle.batch_active()&&!runtime.state.cycle.restoration_barrier());
    CHECK(runtime.state.cycle.restore_required()&&runtime.state.egress.outstanding());
    runtime.next_input();runtime.run();
    CHECK(runtime.command_count==2&&runtime.state.work_commands==2&&runtime.state.rank_decreases==2);
    CHECK(runtime.camps[0].hp==80&&runtime.state.route.goal()==owner&&runtime.state.coverage_transfers==2);
    CHECK(runtime.state.egress.latest().serial==2&&runtime.state.egress.latest().accepted_prefix==2);
    CHECK(runtime.state.egress.replacement_owner()==owner.token&&runtime.state.cycle.latest_work().receipt.serial==2);
    CHECK(runtime.state.cycle.retained_job()==job&&!runtime.state.cycle.ownership_conflict());
    CHECK(runtime.state.fields.generation(N::ServiceField::service)==generation);

    runtime.camp_taken=0;runtime.next_input();runtime.run();
    CHECK(runtime.command_count==4&&runtime.commands[2].action.kind==N::PrimitiveKind::basic&&runtime.commands[3].action.kind==N::PrimitiveKind::move);
    CHECK(runtime.state.work_commands==3&&runtime.state.rank_decreases==2&&!runtime.last_cover_decreased);
    CHECK(runtime.state.coverage_transfers==2&&runtime.camps[0].hp==80);
    CHECK(runtime.state.egress.latest().serial==3&&runtime.state.egress.latest().accepted_prefix==3);
    CHECK(runtime.state.egress.direct_required()&&runtime.state.egress.replacement_owner()==0);
    CHECK(runtime.state.cycle.retained_job()==job&&runtime.state.cycle.restore_required());
    CHECK(!runtime.state.cycle.has_egressed()&&runtime.commands[3].owner.purpose==N::RoutePurpose::recover);
    CHECK(runtime.state.fields.generation(N::ServiceField::service)==generation);
    ++test_groups;
}

static std::uint64_t prepare_covered_work_then_ally_reward(FakeRuntime& runtime,bool enter_spawn){
    runtime.exposure_enabled=runtime.finite_cover_funded=true;
    runtime.add_camp({10,11},0);runtime.add_camp({11,11},1);
    prime_work_route(runtime,0,{10,10});runtime.run();
    CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::basic);
    CHECK(runtime.camps[0].hp==100&&runtime.state.coverage_transfers==1&&runtime.state.egress.transferred());
    const auto work_owner=runtime.state.route.goal().token;
    CHECK(runtime.state.egress.replacement_owner()==work_owner&&runtime.state.route.goal().purpose==N::RoutePurpose::work);
    // The engine-side ally event clears group0 without another actor Work
    // receipt. The still-living group1 is represented by the fixture's x>8
    // exposure source, including this newly available group0 capture spawn.
    runtime.camps[0].hp=0;runtime.camps[0].life=N::Life::dead;
    runtime.lives[0].life=N::GroupLife::reward;
    if(enter_spawn){
        const auto spawn=runtime.members[0].pos;runtime.state.moves[0]=spawn;runtime.state.move_count=1;
        auto available=runtime.budget();
        CHECK(runtime.native_dispatch(available,N::single_primitive({N::PrimitiveKind::move,0,spawn,0})));
        CHECK(runtime.world.pose==spawn&&runtime.commands[1].action.kind==N::PrimitiveKind::move);
        CHECK(runtime.state.egress.replacement_owner()==work_owner); // Move is no transfer or egress closure.
    }
    CHECK(runtime.state.work_commands==1&&runtime.state.cycle.restore_required()&&!runtime.state.cycle.has_egressed());
    return work_owner;
}

static void exposed_capture_binds_and_funds_ownership_before_hold_or_recovery(){
    {
        FakeRuntime runtime;const auto work_owner=prepare_covered_work_then_ally_reward(runtime,true);
        const auto work_receipt=runtime.state.egress.latest();const auto job=runtime.state.cycle.retained_job();
        const auto before_capture=runtime.command_count;
        runtime.capture_exit_funded=false;runtime.capture_denial_requires_reobserve=true;
        runtime.next_input();runtime.run();
        CHECK(runtime.capture_checks==1&&runtime.capture_owner_seen.purpose==N::RoutePurpose::capture);
        CHECK(runtime.capture_owner_seen.token!=work_owner&&runtime.capture_owner_seen.group==runtime.groups[0]);
        CHECK(runtime.capture_group_seen.capturable(runtime.capture_seen.epoch)&&runtime.capture_seen.holding_reward(runtime.capture_seen.epoch));
        CHECK(runtime.command_count==before_capture+1&&runtime.commands[before_capture].action.kind==N::PrimitiveKind::move);
        CHECK(runtime.commands[before_capture].owner.purpose==N::RoutePurpose::recover&&runtime.world.pose.x==9);
        CHECK(runtime.state.recovering&&runtime.state.exit_due&&runtime.state.capture_reobserve);
        CHECK(runtime.state.capture_group==0&&runtime.state.capture_deadline==runtime.epoch);
        CHECK(runtime.state.egress.direct_required()&&runtime.state.egress.replacement_owner()==0);
        CHECK(runtime.state.egress.latest().serial==work_receipt.serial&&runtime.state.egress.latest().accepted_prefix==work_receipt.accepted_prefix);
        CHECK(runtime.state.egress.latest().job==work_receipt.job&&runtime.state.cycle.retained_job()==job);
        CHECK(runtime.state.work_commands==1&&runtime.state.coverage_transfers==1&&runtime.state.restorations==0);
        CHECK(runtime.state.route.goal().purpose==N::RoutePurpose::recover&&runtime.state.route.goal().token!=work_owner);

        runtime.next_input();runtime.run();
        CHECK(runtime.world.pose==N::Point{8,10}&&runtime.commands[runtime.command_count-1].owner.purpose==N::RoutePurpose::recover);
        const auto reads_before_quiet=runtime.camp_reads;runtime.next_input();runtime.run();
        CHECK(runtime.world.hp==runtime.world.max_hp&&runtime.world.recalling);
        CHECK(runtime.commands[runtime.command_count-1].action.kind==N::PrimitiveKind::recall);
        CHECK(runtime.commands[runtime.command_count-1].owner.purpose==N::RoutePurpose::recover);
        CHECK(runtime.state.capture_reobserve&&runtime.state.recovering&&runtime.capture_checks==1);
        CHECK(runtime.camp_reads==reads_before_quiet&&runtime.state.work_commands==1&&!runtime.state.egress.outstanding());
        CHECK(runtime.state.restorations==1&&!runtime.state.cycle.restore_required());
        runtime.next_input();runtime.run();
        CHECK(runtime.state.stage==5&&runtime.capture_checks==1&&runtime.camp_reads==reads_before_quiet);

        // This is an actual Home/full observation, supplied independently of
        // START. Only it can reset the forced capture-reobserve episode.
        runtime.world.pose={2,10};runtime.world.recalling=runtime.world.recall_started=false;runtime.at_home=true;
        runtime.group_fact_available=false; // Home truth does not manufacture distant group visibility.
        runtime.next_input();runtime.run();
        CHECK(!runtime.state.capture_reobserve&&runtime.state.capture_group==-1&&runtime.state.capture_deadline==-1);
        CHECK(!runtime.state.recovering&&!runtime.state.exit_due&&runtime.state.cycle.retained_job()==job);
        CHECK(runtime.state.work_commands==1&&runtime.capture_checks==1&&runtime.state.service.bank_completions==0);
    }
    {
        FakeRuntime runtime;const auto work_owner=prepare_covered_work_then_ally_reward(runtime,true);
        const auto before_capture=runtime.command_count;runtime.next_input();runtime.run();
        CHECK(runtime.capture_checks==1&&runtime.capture_seen.holding_reward(runtime.capture_seen.epoch));
        CHECK(runtime.command_count==before_capture&&runtime.world.pose==runtime.members[0].pos); // Explicitly funded finite Hold.
        CHECK(runtime.state.route.goal().purpose==N::RoutePurpose::capture&&runtime.state.route.goal().token==runtime.capture_owner_seen.token);
        CHECK(runtime.state.egress.transferred()&&runtime.state.egress.replacement_owner()==runtime.capture_owner_seen.token);
        CHECK(runtime.state.egress.replacement_owner()!=work_owner&&runtime.state.coverage_transfers==2);
        CHECK(runtime.state.cycle.restore_required()&&!runtime.state.recovering&&runtime.state.work_commands==1);
    }
    {
        FakeRuntime runtime;const auto work_owner=prepare_covered_work_then_ally_reward(runtime,false);
        const auto before_capture=runtime.command_count;runtime.next_input();runtime.run();
        CHECK(runtime.capture_checks==1&&!runtime.capture_seen.holding_reward(runtime.capture_seen.epoch));
        CHECK(runtime.capture_owner_seen.token!=work_owner&&runtime.capture_owner_seen.purpose==N::RoutePurpose::capture);
        CHECK(runtime.command_count==before_capture+1&&runtime.commands[before_capture].action.kind==N::PrimitiveKind::move);
        CHECK(runtime.commands[before_capture].owner.purpose==N::RoutePurpose::recover&&runtime.state.recovering);
        CHECK(runtime.state.egress.direct_required()&&runtime.state.egress.replacement_owner()==0);
        CHECK(runtime.state.coverage_transfers==2&&runtime.state.work_commands==1&&runtime.world.pose.x==9);
    }
    ++test_groups;
}

static void group_frontier_accepts_near_work_after_shared_dead_without_far_member_lock(){
    FakeRuntime runtime;runtime.index_enabled=true;
    runtime.world.pose={5,12};runtime.world.raw_a=false;runtime.facts.self=runtime.world;
    // Static group1 spans every x5..34 rail cell. Ordinal58 supplies x7,
    // deliberately leaving ordinal57 atx6 and the old far nomination30 atx34.
    for(int ordinal=0;ordinal<60;++ordinal){
        N::Point point{38,30};const int group=ordinal<30?0:1;
        if(ordinal>=30&&ordinal<=56)point={34-(ordinal-30),12};
        if(ordinal==57)point={6,12};
        if(ordinal==58)point={7,12};
        if(ordinal==59)point={5,12};
        runtime.add_camp(point,group,ordinal==59?N::Life::dead:N::Life::awake);
        if(ordinal==59)runtime.camps[std::size_t(ordinal)].hp=0;
    }
    runtime.state.fields.retire_all(); // Before modifying their shared borrowed board.
    for(int x=6;x<=34;++x)runtime.state.passable[12*40+std::size_t(x)]=0;
    runtime.state.passable[12*40+5]=1;++runtime.state.board_generation;
    runtime.prepare_field(N::ServiceField::egress,runtime.state.exit_anchor,runtime.native_egress_key());
    prime_work_route(runtime,59,{5,12});
    const N::EgressWork historical{N::PrimitiveKind::basic,N::EgressEffect::yes,1,1,{39,8,0},{5,11},
        {1,59,runtime.members[59].handle}};
    CHECK(runtime.state.cycle.observe_work({historical,{runtime.groups[1],runtime.members[59].handle,1,59},false})==N::CycleUpdate::retained);
    runtime.state.execution.serial=runtime.state.execution.accepted_prefix=1;runtime.state.last_ack=1;
    CHECK(N::ack_service(runtime.state.service,1,N::PrimitiveKind::basic,N::ServiceApplied::yes));
    const auto cycle_generation=runtime.state.cycle.group_generation();
    const auto group_generation=runtime.state.group_generation;const auto group_key=runtime.state.goal_key;
    const auto owner=runtime.state.route.goal();
    CHECK(owner.group==runtime.groups[1]&&!owner.member_bound&&owner.member==0);

    runtime.run(); // Shared fresh Dead59 + Alive group; no A means no physical Work.
    CHECK(runtime.command_count==0&&runtime.state.work_commands==0&&runtime.state.move_commands==0);
    CHECK(runtime.camp_reads==2&&runtime.camp_read_order[0]==59&&runtime.camp_read_order[1]==57&&runtime.group_reads==1);
    CHECK(!runtime.state.group_work.member_needed(59,runtime.facts.epoch)&&runtime.state.group_work.member_needed(57,runtime.facts.epoch));
    CHECK(runtime.state.cycle.retained_job().group_ordinal==1&&runtime.state.cycle.retained_job().member_ordinal==59);
    CHECK(runtime.state.member==59); // Dead binding does not ordinal-wrap into a new Work goal.
    CHECK(runtime.state.cycle.group_generation()==cycle_generation&&runtime.state.group_generation==group_generation);
    CHECK(runtime.state.group_work.damage_events()==0&&runtime.state.group_work.death_events()==0&&runtime.state.rank_decreases==0);
    CHECK(runtime.state.route.goal()==owner&&runtime.state.route.service_progress()==0);

    runtime.native_programme_job(1,30);runtime.native_programme_work_goal(runtime.world,runtime.members[30].pos);
    auto available=runtime.budget();CHECK(runtime.native_programme_bind(available,N::RoutePurpose::work,runtime.world,runtime.members[30].pos));
    CHECK(runtime.state.member==30&&runtime.state.goal_key==group_key&&runtime.state.route.goal()==owner);
    CHECK(runtime.state.cycle.retained_job().member_ordinal==59&&runtime.state.cycle.latest_work().receipt.serial==1);
    CHECK(runtime.state.group_work.damage_events()==0&&runtime.state.rank_decreases==0&&runtime.state.route.service_progress()==0);

    // Prepare the ACTUAL group frontier from the shared qualified absence.
    // This setup proves root-set consequence only, not controller/native cost.
    const auto& memo=runtime.memory[1];const N::FrontierIntent semantic{runtime.groups[1],1,group_generation,
        runtime.state.board_generation,runtime.state.group_work.route_revision(),1};
    CHECK(runtime.state.frontier.begin(runtime.board_width,runtime.board_height,runtime.state.passable,
        {runtime.state.frontier_members.data()+memo.begin,memo.end-memo.begin},semantic));
    auto possible=[&](int ordinal){return runtime.state.group_work.member_needed(ordinal,runtime.facts.epoch);};
    CHECK(runtime.state.frontier.step(4096,possible).status==N::FrontierStatus::ready);
    CHECK(runtime.state.frontier.complete_roots());const auto roots=runtime.state.frontier.roots();
    CHECK(std::find(roots.begin(),roots.end(),N::Point{5,12})!=roots.end());
    CHECK(runtime.state.fields.begin(N::ServiceField::service,runtime.board_width,runtime.board_height,runtime.state.passable,
        roots,runtime.native_group_field_key()));
    CHECK(runtime.state.fields.step(N::ServiceField::service,4096).status==N::NavigationStatus::ready);
    runtime.state.nav_intent=runtime.state.goal_key;runtime.state.field_revision=runtime.state.fields.generation(N::ServiceField::service);
    runtime.state.root_count=unsigned(roots.size());std::copy(roots.begin(),roots.end(),runtime.state.roots.begin());
    CHECK(runtime.native_service_field_matches()&&runtime.native_navigation().distance(runtime.world.pose)==0);
    CHECK(runtime.state.route.physical_edges()==0&&runtime.state.route.distance_progress()==0&&runtime.state.route.service_progress()==0);

    runtime.next_input();runtime.run();
    CHECK(runtime.command_count==1&&runtime.commands[0].action.kind==N::PrimitiveKind::basic);
    CHECK(runtime.commands[0].action.target_id==runtime.members[57].handle&&runtime.commands[0].action.point==N::Point{6,12});
    CHECK(runtime.commands[0].owner==owner&&runtime.commands[0].job.group_ordinal==1&&runtime.commands[0].job.member_ordinal==57);
    CHECK(runtime.state.member==57&&runtime.state.cycle.retained_job().member_ordinal==57&&runtime.state.cycle.retained_job().group_ordinal==1);
    CHECK(runtime.state.cycle.group_generation()==cycle_generation&&runtime.state.group_generation==group_generation);
    CHECK(runtime.state.cycle.last_member_binding().from.member_ordinal==59&&runtime.state.cycle.last_member_binding().to.member_ordinal==57);
    CHECK(runtime.state.group_work.damage_events()==1&&runtime.state.group_work.death_events()==0&&runtime.state.rank_decreases==1);
    const auto effect=runtime.state.group_work.last_effect();
    CHECK(effect.job.member_ordinal==57&&effect.serial==2&&effect.before_rank==120&&effect.after_rank==100);
    CHECK(effect.before==runtime.commands[0].before&&effect.after==runtime.commands[0].after);
    CHECK(runtime.camps[57].hp==100&&runtime.camps[30].hp==120&&runtime.state.move_commands==0);
    CHECK(runtime.camp_reads==4&&runtime.camp_read_order[2]==57&&runtime.camp_read_order[3]==57&&runtime.group_reads==1);
    CHECK(runtime.state.route.service_progress()==1&&runtime.state.service.bank_completions==0&&!runtime.state.egress.outstanding());

    runtime.world.gems=30;runtime.altars={{5,11}};runtime.next_input();runtime.run();
    CHECK(runtime.command_count==2&&runtime.commands[1].action.kind==N::PrimitiveKind::bank);
    CHECK(runtime.commands[1].owner.purpose==N::RoutePurpose::bank&&runtime.commands[1].owner.group==runtime.groups[1]);
    CHECK(!runtime.state.group_goal); // Geometry purpose changes; the retained group service/custody does not disappear.
    CHECK(runtime.state.cycle.retained_job().group_ordinal==1&&runtime.state.service.job.group_ordinal==1);
    CHECK(runtime.state.cycle.bank_required()&&runtime.state.route.custody_owed()&&runtime.world.gems==30);
    CHECK(runtime.state.group_work.damage_events()==1&&runtime.state.service.bank_completions==0);
    ++test_groups;
}

int main(){
    fresh_work_precedes_retained_route();alternate_altar_and_start_are_not_completion();
    bank_root_materialization_survives_same_goal_invalidation();
    excluded_cached_egress_uses_ready_origin_alternative();changed_egress_keeps_raw_move_and_tries_another_funded_class();
    egress_funding_denial_retains_the_next_input_continuation();lower_potential_without_complete_exit_cannot_dispatch();
    full_quiet_egress_continues_to_actual_bank_start();
    egress_blocks_work_until_actual_qualified_closure();distance_moves_and_arrival_never_complete_service();
    pending_feedback_precedes_all_fresh_service();optional_catalogue_defers_without_stealing_transport();
    injured_quiet_recall_home_heal_and_warm_resume();hazardous_ingress_requires_the_exit_feasibility_seam();
    unfunded_hazardous_work_exits_without_a_work_receipt();finite_cover_retains_each_actual_prefix_but_not_zero_damage();
    exposed_capture_binds_and_funds_ownership_before_hold_or_recovery();
    group_frontier_accepts_near_work_after_shared_dead_without_far_member_lock();
    std::printf("PASS native_service_programme: %u checks, %u groups\n",checks,test_groups);
}
