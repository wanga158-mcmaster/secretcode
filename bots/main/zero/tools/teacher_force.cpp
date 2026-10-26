// Compile twice: WASM SDK/candidate probe, and native included-fastrun replay checkpoint adapter.
#include <cstdint>
#include <cstring>
struct TeacherPlanStage {
    std::int32_t stage, depth, begin, end, count, status, teacher, greedy;
    std::uint64_t build_fuel, inference_fuel, route_fuel, select_fuel, projection_fuel;
};
struct TeacherRequest {
    char magic[16];
    std::int32_t phase, type, target, x, y, represented, result, count, status, flags, refresh_reserve, stage, refreshed;
    std::uint64_t observation_fuel, candidate_fuel, execution_fuel, refresh_fuel, network_fuel;
    std::int32_t obs_x, obs_y, obs_gold, obs_gems, obs_cd0, obs_cd1, obs_state, obs_allowances, obs_allies, obs_enemies, obs_camps;
    float obs_hp;
    std::int32_t quantity, macro_count, macro_complete, macro_kind, macro_target, controller_count, controller_greedy, legacy_present;
    std::uint64_t state_fuel, trunk_fuel, controller_build_fuel, controller_inference_fuel, plan_begin_fuel, plan_fuel;
    TeacherPlanStage macro_stages[12];
    std::uint64_t observer[13], initial_observer[13];
    std::int32_t observer_round, observer_complete, observer_reset, initial_observer_round, initial_observer_complete, initial_observer_reset, inventory_equal;
    std::int32_t decision_budget, normal_after_score, macro_admitted, goal_committed, controller_greedy_kind;
    std::uint64_t commit_fuel;
    std::int32_t source_kind, source_command_index, student_status, student_rule_mask, student_admitted_mask, student_used_mask;
    std::int32_t student_natural, student_admitted_greedy, student_kind, student_target, student_count, student_x, student_y;
    std::int32_t student_priority, student_quantity;
    std::uint64_t student_bounds[15], student_known_mask, student_fuel[7];
    std::uint64_t student_dispatch[15], student_dispatch_known, student_root_bound;
    std::int32_t continuation_status,continuation_type,continuation_rule,continuation_admitted,continuation_used,continuation_natural;
    std::int32_t post_gold,post_gems,post_priority,post_channel,post_remaining,post_counter,post_enemies,post_camps;
    std::uint64_t continuation_fuel[7],continuation_outer;
    std::int32_t pre_root_allowed;
    std::uint64_t pre_root_remaining,pre_root_required,context_build,context_attention,context_destroy,context_entities;
    float offline_logits[15];std::int32_t offline_rule,offline_supported,offline_greedy,offline_entities;std::uint64_t offline_fuel;
    std::uint64_t context_cache_setup_fuel;std::int32_t context_cache_prepared;
    std::int32_t context_cache_parity,context_cache_parity_words;
    std::uint64_t exposure_setup_fuel,exposure_fuel,exposure_before,exposure_required;
    std::int32_t exposure_setup_rows,exposure_rows,exposure_attempted,exposure_completed;
    double exposure_selected[4];
    std::int32_t dash_parity_checked,dash_parity_pass,dash_parity_rows;
    std::int32_t ground_parity_checked,ground_parity_pass,ground_parity_rows,ground_parity_context_words;
    std::int32_t buy_parity_checked,buy_parity_pass,buy_parity_rows;
};
#ifdef ZERO_TEACHER_WASM
#include <cambc.h>
#include <cstdio>
#include "../src/sdk.h"
#include "../src/core/action.h"
#include "../src/core/obs.h"
#include "../src/core/net.h"
#include "../src/core/plan.h"
#include "../src/core/student_decision.h"
#include "../src/core/student_context_cache.h"
#include "../src/core/student_move_exposure.h"
#include "../src/core/student_dash_fast.h"
#include "../src/core/ground_scene.h"
#include "../src/core/student_buy_fast.h"
#include "../src/core/student_history.h"
#include "../src/weights.h"
#include "teacher_lifecycle.h"
#include "teacher_priority.h"
#include "teacher_source.h"

volatile TeacherRequest teacher_request{{'z','e','r','o','.','t','e','a','c','h','e','r','.','v','1',0}};
int teacher_greedy(const float* logits,int count){int best=0;for(int i=1;i<count;++i)if(logits[i]>logits[best])best=i;return best;}
#if defined(ZERO_STUDENT_PROBE)
zero::GroundUtilityScene* teacher_ground_scene{};
std::uint32_t teacher_ground_rule(const zero::Context& context,std::uint32_t allowed){
    return zero::student::rule_mask_ground_scene(context,*teacher_ground_scene,allowed);
}
zero::ActionBuild teacher_ground_arguments(const zero::Context& context,zero::ActionType type,std::vector<zero::Cand>& rows){
    if(type==zero::a_ward||type==zero::a_use_consumable)
        return zero::student::argument_candidates_ground_scene(context,*teacher_ground_scene,type,rows);
    if(type==zero::a_buy&&(teacher_request.flags&134217728))return zero::student::argument_candidates_buy_fast(context,type,rows);
    return teacher_request.flags&8388608?zero::student::argument_candidates_dash_fast(context,type,rows)
                                       :zero::student::argument_candidates(context,type,rows);
}
void teacher_ground_parity(const zero::Map& map,const zero::Obs& original,const zero::Memory& memory,
                           zero::student::CampContextCache& cache,const float* weights){
    using namespace zero;
    Obs compact=original;GroundUtilityScene scene;
    const UtilityRules rules{cambc::detail::cached_game_settings.ward_placement_radius,2.0f,
        cambc::consumable_type::altar_lock.cost(),kConsumeLock};
    build_ground_utility_scene(map,compact,rules,scene);
    bool same=apply_ground_utility_contract(compact,scene);
    Context old_context{&map,nullptr,&original,&memory},new_context{&map,nullptr,&compact,&memory};
    float old_state[student::kState],new_state[student::kState];
    student::state_vector(old_context,old_state);student::state_vector(new_context,new_state);
    same= same&&std::memcmp(old_state,new_state,sizeof(old_state))==0;
    same= same&&student::rule_mask(old_context,kAllActions)==student::rule_mask_ground_scene(new_context,scene,kAllActions);
    std::vector<student::CampContext> old_camps,new_camps;
    if(weights[-kConfig+8]>0.0f){
        if(weights[-kConfig+9]>0.0f){student::camp_context_cached(old_context,cache,old_camps);student::camp_context_cached(new_context,cache,new_camps);}
        else{student::camp_context(old_context,old_camps);student::camp_context(new_context,new_camps);}
    }
    same= same&&old_camps.size()==new_camps.size();
    for(std::size_t i=0;same&&i<old_camps.size();++i)
        same=old_camps[i].group==new_camps[i].group&&std::memcmp(old_camps[i].phi,new_camps[i].phi,sizeof(old_camps[i].phi))==0;
    student::net::Embedding old_embedding,new_embedding;
    if(weights[-kConfig+8]>0.0f){same=student::net::embed_attention(weights,old_state,old_camps,old_embedding)&&same;
        same=student::net::embed_attention(weights,new_state,new_camps,new_embedding)&&same;}
    else{student::net::embed(weights,old_state,old_embedding);student::net::embed(weights,new_state,new_embedding);}
    same= same&&std::memcmp(old_embedding.h,new_embedding.h,sizeof(old_embedding.h))==0;
    float old_logits[15],new_logits[15];student::net::command_logits(weights,old_embedding,old_logits);
    student::net::command_logits(weights,new_embedding,new_logits);
    same= same&&std::memcmp(old_logits,new_logits,sizeof(old_logits))==0;
    int count=0;
    for(const auto type:{a_ward,a_use_consumable}){
        std::vector<UtilityAction> raw;same=materialize_ground_utility_scene(map,compact,scene,type,raw)&&same;
        std::size_t at=0;for(const auto& old:original.utility_actions)if(old.type==type){
            if(at>=raw.size()){same=false;break;}const auto& row=raw[at++];
            same=same&&old.type==row.type&&old.target==row.target&&old.at==row.at&&std::memcmp(old.phi,row.phi,sizeof(old.phi))==0;
        }
        same=same&&at==raw.size();
        std::vector<Cand> old_rows,new_rows;
        const auto a=student::argument_candidates(old_context,type,old_rows),b=student::argument_candidates_ground_scene(new_context,scene,type,new_rows);
        same=same&&a.status==b.status&&a.total==b.total&&old_rows.size()==new_rows.size();
        for(std::size_t i=0;same&&i<old_rows.size();++i){const auto& x=old_rows[i];const auto& y=new_rows[i];
            same=x.kind==y.kind&&x.target==y.target&&x.count==y.count&&x.at==y.at&&std::memcmp(x.phi,y.phi,sizeof(x.phi))==0;}
        count+=static_cast<int>(old_rows.size());
    }
    teacher_request.ground_parity_checked=1;teacher_request.ground_parity_pass=same;teacher_request.ground_parity_rows=count;
    teacher_request.ground_parity_context_words=static_cast<int>(old_camps.size()*student::kCampContextPhi);
    if(!same)throw std::runtime_error("ground scene raw/root parity failed");
}
#endif
std::uint64_t teacher_exposure_rows(const zero::Context& context,std::span<const zero::Cand> rows,zero::Tile selected,
                                    volatile std::int32_t& row_count,volatile double* selected_values){
    const auto start=cambc::fuel_remaining();
    {
        std::vector<zero::student::MoveObservedDesignCoverage> coverage;
        zero::student::move_observed_design_coverage(context,rows,coverage);
        row_count=static_cast<std::int32_t>(coverage.size());
        if(selected_values)for(const auto& row:coverage)if(row.move.at==selected){
            selected_values[0]=static_cast<double>(row.living_visible_enemy_champions_in_design_range);
            selected_values[1]=row.observed_enemy_strength_sum;
            selected_values[2]=static_cast<double>(row.living_visible_awake_camps_in_design_range);
            selected_values[3]=row.queried_awake_camp_base_strength_sum;
        }
    }
    return start-cambc::fuel_remaining();
}
std::uint64_t teacher_move_exposure_setup(const zero::Context& context){
    std::vector<zero::Cand> rows;
    const auto built=zero::student::argument_candidates(context,zero::a_move,rows);
    if(built.status!=zero::ActionBuildStatus::ok)throw std::runtime_error("MOVE coverage shadow has no complete raw MOVE set");
    return teacher_exposure_rows(context,rows,{-1,-1},teacher_request.exposure_setup_rows,nullptr);
}
void teacher_move_exposure_trace(void* user,const zero::student::DecisionTrace& event){
    zero::student::probe::trace(nullptr,event);
    if(event.stage!=zero::student::TraceStage::argument||event.command!=zero::a_move||
       event.decision_status!=zero::student::DecisionStatus::command||event.rows.empty())return;
    teacher_request.exposure_before=cambc::fuel_remaining();
    teacher_request.exposure_required=teacher_request.exposure_setup_fuel*5u/4u+
        teacher_request.student_dispatch[zero::a_move]*5u/4u+8000u;
    if(teacher_request.exposure_setup_rows>0&&teacher_request.exposure_before>teacher_request.exposure_required){
        teacher_request.exposure_attempted=1;
        const auto selected=event.natural_label>=0&&static_cast<std::size_t>(event.natural_label)<event.rows.size()
            ?event.rows[static_cast<std::size_t>(event.natural_label)].at:zero::Tile{-1,-1};
        teacher_request.exposure_fuel=teacher_exposure_rows(*static_cast<const zero::Context*>(user),event.rows,selected,
            teacher_request.exposure_rows,teacher_request.exposure_selected);
        teacher_request.exposure_completed=1;
    }
}
void teacher_observer(bool initial){
    const auto& p=zero::sdk::last_observer_fuel();volatile std::uint64_t* fields=initial?teacher_request.initial_observer:teacher_request.observer;
    fields[0]=p.revision;fields[1]=p.self;fields[2]=p.items;fields[3]=p.commands;fields[4]=p.champions;fields[5]=p.camps;fields[6]=p.groups;fields[7]=p.locks_moves;fields[8]=p.total;
#if defined(ZERO_STUDENT_PROBE)
    fields[9]=p.locks_read;fields[10]=p.canonical_order;fields[11]=p.move_arguments;fields[12]=p.ground_utilities;
#endif
    if(initial){teacher_request.initial_observer_round=p.round;teacher_request.initial_observer_complete=p.complete;teacher_request.initial_observer_reset=p.fuel_reset;}
    else{teacher_request.observer_round=p.round;teacher_request.observer_complete=p.complete;teacher_request.observer_reset=p.fuel_reset;}
}
struct TeacherReferencePlan {
    zero::Context context;
    zero::GoalCursor cursor_;
    explicit TeacherReferencePlan(zero::Context c):context(c){zero::plan_begin(context,cursor_);}
    zero::GoalPlanBuild candidates(zero::CandSet& out){return zero::plan_candidates(context,cursor_,out);}
    bool select(const zero::Cand& c){return zero::plan_select(context,cursor_,c);}
    int teacherIndex(zero::GoalKind kind,int target)const{return zero::plan_teacher_index(context,cursor_,kind,target);}
    zero::GoalStage stage()const{return cursor_.stage;}
    int kind()const{return cursor_.kind;}
    int target()const{return cursor_.target;}
    const zero::GoalCursor& cursor()const{return cursor_;}
};
template<class Loan>void teacher_plan_traverse(zero::Context ctx,const float* w,const zero::net::Trunk& trunk,zero::CandSet& choices){
    using namespace zero;
    const auto before_construct=cambc::fuel_remaining();
    {
        Loan loan(ctx);
        teacher_request.plan_begin_fuel=before_construct-cambc::fuel_remaining();
        net::HeadContext projection;bool projected=false;float lg[kMaxCands];
        for(int level=0;level<12&&loan.stage()!=GoalStage::done;++level){
            teacher_request.stage=20+level;auto& metric=teacher_request.macro_stages[level];const auto& cursor=loan.cursor();metric.stage=static_cast<int>(loan.stage());metric.depth=cursor.depth;metric.begin=cursor.begin;metric.end=cursor.end;teacher_request.macro_count=level+1;
            auto before_part=cambc::fuel_remaining();const auto built=loan.candidates(choices);auto after_part=cambc::fuel_remaining();metric.build_fuel=before_part-after_part;metric.status=static_cast<int>(built.status);metric.count=choices.n;
            if(!built.usable()||!choices.n)break;
            before_part=cambc::fuel_remaining();metric.teacher=loan.teacherIndex(g_farm,teacher_request.target);after_part=cambc::fuel_remaining();metric.route_fuel=before_part-after_part;
            if((teacher_request.flags&64)&&choices.n>1&&!projected){before_part=cambc::fuel_remaining();net::project_fast(w,trunk,h_goal,projection);after_part=cambc::fuel_remaining();metric.projection_fuel=before_part-after_part;projected=true;}
            before_part=cambc::fuel_remaining();metric.greedy=0;if(choices.n>1){if(teacher_request.flags&64)net::logits_cached(w,projection,choices,lg);else net::logits(w,trunk,choices,lg);metric.greedy=teacher_greedy(lg,choices.n);}after_part=cambc::fuel_remaining();metric.inference_fuel=before_part-after_part;
            before_part=cambc::fuel_remaining();const bool selected=metric.teacher>=0&&metric.teacher<choices.n&&loan.select(choices.c[metric.teacher]);after_part=cambc::fuel_remaining();metric.select_fuel=before_part-after_part;if(!selected)break;
        }
        teacher_request.macro_complete=loan.stage()==GoalStage::done;teacher_request.macro_kind=loan.kind();teacher_request.macro_target=loan.target();
    }
}
#if defined(ZERO_SDK_SOURCE_SEQUENCE)
int main(){return source_sdk_main();}
#elif !defined(ZERO_STUDENT_LIFECYCLE)
int main() {
    using namespace zero;
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    Map map;
    Obs obs;
    Memory mem;
    student::CampContextCache context_cache;
#if defined(ZERO_STUDENT_PROBE)
    GroundUtilityScene ground_scene;
#endif
    sdk::set_explicit_channel_mode(true);
    zero::fuel_hook = []() { return cambc::fuel_remaining(); };
    try {
        sdk::load_terrain(map);
        sdk::load_groups(map);
        map.finish();
        if(const_cast<const float*>(params.w)[8]>0.0f&&const_cast<const float*>(params.w)[9]>0.0f){const auto started=cambc::fuel_remaining();context_cache.prepare(map);teacher_request.context_cache_setup_fuel=started-cambc::fuel_remaining();teacher_request.context_cache_prepared=context_cache.prepared_for(map);}
        teacher_request.inventory_equal=-1;
        if(teacher_request.flags&8){
            sdk::observe(map,obs);observe(obs,map,mem);teacher_observer(true);
            const auto native=cambc::self().item_count();bool equal=true;
            for(int i=0;i<42;++i){const auto count=native[static_cast<cambc::item_type>(i)];equal&=obs.item_counts[i]==count&&obs.items[i]==std::min<std::uint32_t>(255,count);}
            teacher_request.inventory_equal=equal;
#if defined(ZERO_STUDENT_PROBE)
            if(teacher_request.flags&268435456){
                Context context{&map,nullptr,&obs,&mem};std::vector<Cand> original,fast;
                const auto a=student::argument_candidates(context,a_buy,original),b=student::argument_candidates_buy_fast(context,a_buy,fast);
                bool same=a.status==b.status&&a.total==b.total&&original.size()==fast.size();
                for(std::size_t i=0;same&&i<original.size();++i){const auto& x=original[i];const auto& y=fast[i];
                    same=x.kind==y.kind&&x.target==y.target&&x.count==y.count&&x.at==y.at&&std::memcmp(x.phi,y.phi,sizeof(x.phi))==0;
                    Cand old_quantity,new_quantity;student::quantity_candidate(context,x,old_quantity);student::quantity_candidate(context,y,new_quantity);
                    same=same&&old_quantity.kind==new_quantity.kind&&old_quantity.target==new_quantity.target&&old_quantity.count==new_quantity.count&&old_quantity.at==new_quantity.at&&std::memcmp(old_quantity.phi,new_quantity.phi,sizeof(old_quantity.phi))==0;
                }
                teacher_request.buy_parity_checked=1;teacher_request.buy_parity_pass=same;teacher_request.buy_parity_rows=static_cast<std::int32_t>(original.size());
                if(!same)throw std::runtime_error("BUY argument/quantity raw parity failed");
            }
            if(teacher_request.flags&67108864)
                teacher_ground_parity(map,obs,mem,context_cache,const_cast<const float*>(params.w)+kConfig);
#endif
            if(teacher_request.flags&16777216){
                Context context{&map,nullptr,&obs,&mem};std::vector<Cand> original,fast;
                const auto type=static_cast<ActionType>(teacher_request.type);
                const auto a=student::argument_candidates(context,type,original),b=student::argument_candidates_dash_fast(context,type,fast);
                bool same=a.status==b.status&&a.total==b.total&&original.size()==fast.size();
                for(std::size_t i=0;same&&i<original.size();++i){const auto& x=original[i];const auto& y=fast[i];same=x.kind==y.kind&&x.target==y.target&&x.count==y.count&&x.at==y.at&&std::memcmp(x.phi,y.phi,sizeof(x.phi))==0;}
                teacher_request.dash_parity_checked=1;teacher_request.dash_parity_pass=same;teacher_request.dash_parity_rows=static_cast<std::int32_t>(original.size());
                if(!same)throw std::runtime_error("dash argument raw parity failed");
            }
            if(teacher_request.flags&1048576){
                const auto started=cambc::fuel_remaining();obs.command_index=teacher_request.source_command_index;
                Context context{&map,nullptr,&obs,&mem};float state[student::kState];student::state_vector(context,state);
                const auto* w=const_cast<const float*>(params.w)+kConfig;student::net::Embedding embedding;std::vector<student::CampContext> camps;
                if(w[-kConfig+8]>0.0f){
                    if(w[-kConfig+9]>0.0f){
                        student::camp_context_cached(context,context_cache,camps);
                        std::vector<student::CampContext> reference;
                        student::camp_context(context,reference);
                        bool equal=camps.size()==reference.size();
                        for(std::size_t i=0;equal&&i<camps.size();++i)
                            equal=camps[i].group==reference[i].group&&std::memcmp(camps[i].phi,reference[i].phi,sizeof(camps[i].phi))==0;
                        teacher_request.context_cache_parity=equal?1:-1;
                        teacher_request.context_cache_parity_words=static_cast<std::int32_t>(camps.size()*student::kCampContextPhi);
                        if(!equal)throw std::runtime_error("offline context cache parity failed");
                    }else student::camp_context(context,camps);
                    if(!student::net::embed_attention(w,state,camps,embedding))throw std::runtime_error("offline attention failed");
                }
                else student::net::embed(w,state,embedding);
                float logits[15];student::net::command_logits(w,embedding,logits);for(int i=0;i<15;++i)teacher_request.offline_logits[i]=logits[i];
                const auto rule=student::rule_mask(context,kAllActions);auto supported=rule&(static_cast<std::uint32_t>(teacher_request.student_dispatch_known)|action_bit(a_end));
                if(w[-kConfig+7]>0.0f)supported&=~action_bit(a_replan);
                int best=-1;for(int i=0;i<15;++i)if((supported&(1u<<i))&&(best<0||logits[i]>logits[best]))best=i;
                teacher_request.offline_rule=rule;teacher_request.offline_supported=supported;teacher_request.offline_greedy=best;teacher_request.offline_entities=camps.size();teacher_request.offline_fuel=started-cambc::fuel_remaining();
                if(teacher_request.flags&2097152)
                    teacher_request.exposure_setup_fuel=teacher_move_exposure_setup(context);
            }
        }
        teacher_request.phase = 1;
        cambc::end_turn();  // host yield only: checkpoint does not advance
        sdk::EnginePaths paths(map, static_cast<int>(cambc::detail::absolute_team(cambc::team::ally)));
        const auto before = cambc::fuel_remaining();
        teacher_request.stage=1;
#if defined(ZERO_STUDENT_PROBE)
        // Both arms of the scene pair establish history before its immutable observation borrow.
        // This work remains inside the measured caller interval.
        if(teacher_request.flags&67108864){
            if(teacher_request.source_command_index==0)student::begin_commands(obs);
            else obs.command_index=teacher_request.source_command_index;
        }
        if(teacher_request.flags&33554432)sdk::set_probe_ground_utility_scene(&ground_scene);
#endif
        sdk::observe(map, obs);
#if defined(ZERO_STUDENT_PROBE)
        sdk::set_probe_ground_utility_scene(nullptr);
#endif
        observe(obs, map, mem);
        teacher_request.obs_x=obs.self.at.x;teacher_request.obs_y=obs.self.at.y;teacher_request.obs_hp=obs.self.hp;
        teacher_request.obs_gold=obs.self.gold;teacher_request.obs_gems=obs.self.gems;teacher_request.obs_cd0=obs.self.cooldown[0];teacher_request.obs_cd1=obs.self.cooldown[1];teacher_request.obs_state=obs.state_bits;
        teacher_request.obs_allowances=(obs.can_move?1:0)|(obs.can_attack?2:0)|(obs.can_cast?4:0);
        teacher_request.obs_allies=obs.allies.size();teacher_request.obs_enemies=obs.enemies.size();teacher_request.obs_camps=obs.camps.size();
        const auto observed = cambc::fuel_remaining();
        teacher_request.observation_fuel = before - observed;
        teacher_observer(false);
        Context ctx{&map, &paths, &obs, &mem};
        if(teacher_request.flags&256){
            teacher_request.stage=30;
            if(!(teacher_request.flags&67108864)){
                if(teacher_request.source_command_index==0)student::begin_commands(obs);else obs.command_index=teacher_request.source_command_index;
            }
            const float* w=const_cast<const float*>(params.w)+kConfig;asm volatile("" : "+r"(w) : : "memory");
            student::DecisionOptions options;options.remaining_fuel=[](){return cambc::fuel_remaining();};
            std::uint64_t rng=1;options.rng=&rng;options.cleanup_reserve=8000;
            options.diagnostic_bypass_admission=(teacher_request.flags&512)!=0;
            options.camp_attention=w[-kConfig+8]>0.0f;
#if defined(ZERO_STUDENT_PROBE)
            if(teacher_request.flags&8388608)options.argument_builder=student::argument_candidates_dash_fast;
            if(teacher_request.flags&134217728)options.argument_builder=student::argument_candidates_buy_fast;
            if(teacher_request.flags&33554432){teacher_ground_scene=&ground_scene;options.rule_builder=teacher_ground_rule;options.argument_builder=teacher_ground_arguments;}
#endif
            if(options.camp_attention&&w[-kConfig+9]>0.0f)options.camp_context_cache=&context_cache;
            options.allowed_mask=options.diagnostic_bypass_admission?kAllActions:static_cast<std::uint32_t>(teacher_request.student_dispatch_known)|action_bit(a_end);
            if(w[-kConfig+7]>0.0f)options.allowed_mask&=~action_bit(a_replan);
            student::FuelBounds bounds;bounds.known_mask=static_cast<std::uint32_t>(teacher_request.student_known_mask);
            for(int i=0;i<a_count;++i)bounds.argument[i]=teacher_request.student_bounds[i];
            student::TeacherSource source;source.type=static_cast<ActionType>(teacher_request.type);
            source.command.target=teacher_request.target;source.command.count=teacher_request.quantity;
            source.command.at={static_cast<std::int16_t>(teacher_request.x),static_cast<std::int16_t>(teacher_request.y)};
            source.match_fields=0;
            if(teacher_request.source_kind>=0){source.command.kind=static_cast<std::uint8_t>(teacher_request.source_kind);source.match_fields|=student::match_kind;}
            if(teacher_request.target!=-10000)source.match_fields|=student::match_target;
            if(source.type==a_move||source.type==a_cast0||source.type==a_cast1||source.type==a_tribute||source.type==a_use_consumable||source.type==a_ward)
                source.match_fields|=student::match_tile;
            if(teacher_request.flags&1024)source.match_fields=0;
            const bool natural=(teacher_request.flags&524288)!=0;
            teacher_request.pre_root_remaining=cambc::fuel_remaining();teacher_request.pre_root_required=teacher_request.student_root_bound*5u/4u+8000u;
            const bool source_guard=(teacher_request.flags&4194304)!=0;
            teacher_request.pre_root_allowed=!(natural||source_guard)||teacher_request.pre_root_remaining>teacher_request.pre_root_required;
            if(natural||source_guard){
#if defined(ZERO_STUDENT_PROBE)
                lifecycle_control.trace_enabled=1;
                options.trace=(teacher_request.flags&2097152)?teacher_move_exposure_trace:student::probe::trace;
                if(teacher_request.flags&2097152)options.trace_user=&ctx;
#else
                throw std::runtime_error("natural trace requires student probe build");
#endif
            }
            student::Decision decision;
            if(teacher_request.pre_root_allowed)decision=student::decide(ctx,w,options,bounds,natural?nullptr:&source);
            else{decision.status=student::DecisionStatus::fuel_end;decision.type=a_end;}
#if defined(ZERO_STUDENT_PROBE)
            teacher_ground_scene=nullptr;ground_scene=GroundUtilityScene{};  // all borrowed facts released before dispatch/yield
#endif
            teacher_request.student_status=static_cast<int>(decision.status);teacher_request.student_rule_mask=decision.rule_mask;
            teacher_request.student_admitted_mask=decision.admitted_mask;teacher_request.student_used_mask=decision.used_mask;
            teacher_request.student_natural=decision.natural_command;teacher_request.student_admitted_greedy=decision.admitted_greedy;
            teacher_request.student_kind=decision.command.kind;teacher_request.student_target=decision.command.target;teacher_request.student_count=decision.command.count;
            teacher_request.student_x=decision.command.at.x;teacher_request.student_y=decision.command.at.y;
            teacher_request.student_priority=decision.predicted_priority;teacher_request.student_quantity=decision.predicted_quantity;
            const std::uint64_t phase_fuel[]={decision.fuel.state,decision.fuel.encode,decision.fuel.rule_mask,decision.fuel.controller,decision.fuel.argument,decision.fuel.trace,decision.fuel.total};
            for(int i=0;i<7;++i)teacher_request.student_fuel[i]=phase_fuel[i];
#if defined(ZERO_STUDENT_PROBE)
            teacher_request.context_build=decision.fuel.context_build;teacher_request.context_attention=decision.fuel.context_attention;teacher_request.context_destroy=decision.fuel.context_destroy;teacher_request.context_entities=decision.fuel.context_entities;
            teacher_request.macro_stages[0].build_fuel=decision.fuel.argument_build;
            teacher_request.macro_stages[0].projection_fuel=decision.fuel.argument_project;
            teacher_request.macro_stages[0].inference_fuel=decision.fuel.argument_scan;
            teacher_request.macro_stages[0].route_fuel=decision.fuel.argument_route;
            teacher_request.macro_stages[0].select_fuel=decision.fuel.argument_destroy;
#endif
            teacher_request.status=static_cast<int>(decision.build_status);teacher_request.represented=decision.status==student::DecisionStatus::command||decision.status==student::DecisionStatus::learned_end||decision.status==student::DecisionStatus::rule_end;
            const auto before_dispatch=cambc::fuel_remaining();
            const bool dispatch_allowed=options.diagnostic_bypass_admission||decision.type==a_end||
                ((teacher_request.student_dispatch_known&action_bit(decision.type))&&
                 cambc::fuel_remaining()>teacher_request.student_dispatch[decision.type]*5u/4u+8000u);
            if(!dispatch_allowed){teacher_request.represented=0;teacher_request.student_status=static_cast<int>(student::DecisionStatus::fuel_end);}
            if(dispatch_allowed&&decision.status==student::DecisionStatus::command&&decision.type==a_replan){
                if(mem.goal_kind!=decision.command.kind||mem.goal_target!=decision.command.target)mem.goal_since=obs.round;
                mem.goal_kind=decision.command.kind;mem.goal_target=decision.command.target;mem.goal_decided=obs.round;
                teacher_request.goal_committed=1;teacher_request.result=1;
            }else if(dispatch_allowed&&decision.status==student::DecisionStatus::command){
                teacher_request.result=static_cast<int>(sdk::primitive_result(decision.type,obs,decision.command));
            }else if(teacher_request.represented&&decision.type==a_end)teacher_request.result=1;
            teacher_request.execution_fuel=before_dispatch-cambc::fuel_remaining();
            teacher_request.commit_fuel=teacher_request.goal_committed?teacher_request.execution_fuel:0;
            if(teacher_request.represented&&decision.type!=a_end)student::record_command(obs);
            const auto before_refresh=cambc::fuel_remaining();
            const bool refresh=(teacher_request.flags&2)||((teacher_request.flags&4)&&before_refresh>static_cast<std::uint64_t>(teacher_request.refresh_reserve));
            if(decision.status==student::DecisionStatus::command&&decision.type!=a_replan&&refresh){
                teacher_request.stage=31;teacher_request.refreshed=1;
#if defined(ZERO_STUDENT_PROBE)
                if(decision.type==a_priority&&(teacher_request.flags&32768))teacher_request.refresh_fuel=zero_priority_probe::refresh(map,obs,mem,decision.command);
                else
#endif
                {sdk::refresh_after_primitive(map,obs,decision.type,decision.command);observe(obs,map,mem);}
            }
#if defined(ZERO_STUDENT_PROBE)
            if(!(decision.type==a_priority&&(teacher_request.flags&32768)))
#endif
                teacher_request.refresh_fuel=before_refresh-cambc::fuel_remaining();
            teacher_request.post_gold=obs.self.gold;teacher_request.post_gems=obs.self.gems;teacher_request.post_priority=obs.priority;
            teacher_request.post_channel=obs.self.recalling?1:obs.self.tributing?2:obs.teleporting?3:0;teacher_request.post_remaining=obs.channel_remaining;
            teacher_request.post_counter=obs.command_index;teacher_request.post_enemies=obs.enemies.size();teacher_request.post_camps=obs.camps.size();
            if(teacher_request.flags&131072){
                if(!teacher_request.refreshed||!options.diagnostic_bypass_admission)throw std::runtime_error("next-root inspection requires complete diagnostic refresh");
                student::TeacherSource inspect;inspect.type=a_end;inspect.match_fields=0;
                const auto before_continuation=cambc::fuel_remaining();const auto next=student::decide(ctx,w,options,bounds,&inspect);
                teacher_request.continuation_outer=before_continuation-cambc::fuel_remaining();
                teacher_request.continuation_status=static_cast<int>(next.status);teacher_request.continuation_type=next.type;
                teacher_request.continuation_rule=next.rule_mask;teacher_request.continuation_admitted=next.admitted_mask;teacher_request.continuation_used=next.used_mask;teacher_request.continuation_natural=next.natural_command;
                const std::uint64_t fields[]={next.fuel.state,next.fuel.encode,next.fuel.rule_mask,next.fuel.controller,next.fuel.argument,next.fuel.trace,next.fuel.total};
                for(int i=0;i<7;++i)teacher_request.continuation_fuel[i]=fields[i];
            }
            for(int i=0;i<a_count;++i)teacher_request.student_bounds[i]=bounds.argument[i];
            teacher_request.student_known_mask=bounds.known_mask;
            teacher_request.phase=2;teacher_request.stage=6;
            while(true)cambc::end_turn();
        }
        if(teacher_request.flags&16){
            const float* w=const_cast<const float*>(params.w)+kConfig;asm volatile("" : "+r"(w) : : "memory");
            float sv[kState],lg[kMaxCands];net::Trunk trunk;CandSet controller,choices;
            teacher_request.stage=10;
            auto before_part=cambc::fuel_remaining();state_vector(ctx,sv);auto after_part=cambc::fuel_remaining();teacher_request.state_fuel=before_part-after_part;
            before_part=after_part;if(teacher_request.flags&64)net::trunk_fast(w,sv,trunk);else net::trunk(w,sv,trunk);after_part=cambc::fuel_remaining();teacher_request.trunk_fuel=before_part-after_part;
            before_part=after_part;action_candidates(ctx,controller,kAllActions);after_part=cambc::fuel_remaining();teacher_request.controller_build_fuel=before_part-after_part;teacher_request.controller_count=controller.n;
            before_part=after_part;if(teacher_request.flags&64)net::logits_fast(w,trunk,controller,lg);else net::logits(w,trunk,controller,lg);teacher_request.controller_greedy=teacher_greedy(lg,controller.n);after_part=cambc::fuel_remaining();teacher_request.controller_inference_fuel=before_part-after_part;
            teacher_request.controller_greedy_kind=controller.c[teacher_request.controller_greedy].kind;
            teacher_request.normal_after_score=100000-(teacher_request.decision_budget-static_cast<int>(cambc::fuel_remaining()));
            // Frozen root bound: 60K * 1.25 + 8K cleanup + 1K greedy choice reserve.
            teacher_request.macro_admitted=teacher_request.normal_after_score>84000;
            const auto plan_start=cambc::fuel_remaining();
            if(teacher_request.flags&32){
                auto& metric=teacher_request.macro_stages[0];metric.stage=-1;
                before_part=cambc::fuel_remaining();goal_candidates(ctx,choices);after_part=cambc::fuel_remaining();metric.build_fuel=before_part-after_part;metric.count=choices.n;
                metric.teacher=-1;for(int i=0;i<choices.n;++i)if(choices.c[i].kind==g_farm&&choices.c[i].target==teacher_request.target)metric.teacher=i;
                teacher_request.legacy_present=metric.teacher>=0;
                before_part=cambc::fuel_remaining();if(choices.n>1){if(teacher_request.flags&64)net::logits_fast(w,trunk,choices,lg);else net::logits(w,trunk,choices,lg);metric.greedy=teacher_greedy(lg,choices.n);}after_part=cambc::fuel_remaining();metric.inference_fuel=before_part-after_part;
                teacher_request.macro_count=1;teacher_request.macro_complete=1;teacher_request.macro_kind=g_farm;teacher_request.macro_target=teacher_request.target;
            }else if(teacher_request.flags&128)teacher_plan_traverse<GoalPlanSession>(ctx,w,trunk,choices);
            else teacher_plan_traverse<TeacherReferencePlan>(ctx,w,trunk,choices);
            teacher_request.plan_fuel=plan_start-cambc::fuel_remaining();
            const auto before_commit=cambc::fuel_remaining();
            if(teacher_request.macro_complete&&!(teacher_request.flags&32)){
                if(mem.goal_kind!=teacher_request.macro_kind||mem.goal_target!=teacher_request.macro_target)mem.goal_since=obs.round;
                mem.goal_kind=teacher_request.macro_kind;mem.goal_target=teacher_request.macro_target;mem.goal_decided=obs.round;
                sdk::publish_claim(mem.goal_kind,mem.goal_target);teacher_request.goal_committed=1;
            }
            teacher_request.commit_fuel=before_commit-cambc::fuel_remaining();
            teacher_request.represented=teacher_request.macro_complete;teacher_request.result=1;teacher_request.phase=2;teacher_request.stage=6;
            while(true)cambc::end_turn();
        }
        teacher_request.stage=2;
        CandSet families, arguments;
        const auto type = static_cast<ActionType>(teacher_request.type);
        action_candidates(ctx, families);
        const auto build = argument_candidates(ctx, type, arguments);
        teacher_request.status = static_cast<int>(build.status);
        teacher_request.count = arguments.n;
        int selected = -1;
        for (int i = 0; i < arguments.n; ++i) {
            const auto& c = arguments.c[i];
            const bool no_position = type == a_end || type == a_recall || type == a_cancel || type == a_buy || type == a_buy_consumable || type == a_sell || type == a_priority;
            if ((type == a_priority || teacher_request.target == -10000 || teacher_request.target == c.target) &&
                (no_position || (c.at.x == teacher_request.x && c.at.y == teacher_request.y))) { selected = i; break; }
        }
        teacher_request.represented = selected >= 0;
        const auto generated = cambc::fuel_remaining();
        teacher_request.candidate_fuel = observed - generated;
        Cand forced;
        teacher_request.stage=3;
        if (selected >= 0) { forced=arguments.c[selected];if(type==a_priority)forced.target=teacher_request.target;if(type==a_buy)forced.count=teacher_request.quantity;teacher_request.result = static_cast<int>(sdk::primitive_result(type, obs, forced)); }
        const auto executed = cambc::fuel_remaining();
        teacher_request.execution_fuel = generated - executed;
        if(teacher_request.flags&1){
            teacher_request.stage=4;
            float sv[kState],lg[kMaxCands];net::Trunk trunk;
            const float* w=const_cast<const float*>(params.w)+kConfig;asm volatile("" : "+r"(w) : : "memory");
            state_vector(ctx,sv);if(teacher_request.flags&64)net::trunk_fast(w,sv,trunk);else net::trunk(w,sv,trunk);if(teacher_request.flags&64)net::logits_fast(w,trunk,families,lg);else net::logits(w,trunk,families,lg);
            volatile int sink=teacher_greedy(lg,families.n);
            if(arguments.n>1){if(teacher_request.flags&64)net::logits_fast(w,trunk,arguments,lg);else net::logits(w,trunk,arguments,lg);sink=teacher_greedy(lg,arguments.n);}
            (void)sink;teacher_request.network_fuel=executed-cambc::fuel_remaining();
        }
        const auto after_network=cambc::fuel_remaining();
        const bool refresh=(teacher_request.flags&2)||((teacher_request.flags&4)&&after_network>static_cast<std::uint64_t>(teacher_request.refresh_reserve));
        if (selected >= 0 && type != a_end && refresh){teacher_request.stage=5;teacher_request.refreshed=1;sdk::refresh_after_primitive(map, obs, type, forced);}
        teacher_request.refresh_fuel = after_network - cambc::fuel_remaining();
        teacher_request.stage=6;
        teacher_request.phase = 2;
    } catch (const std::exception& e) {
#if defined(ZERO_STUDENT_PROBE)
        sdk::set_probe_ground_utility_scene(nullptr);teacher_ground_scene=nullptr;ground_scene=GroundUtilityScene{};
#endif
        std::printf("teacher error: %s\n", e.what());
        teacher_request.phase = -1;
    } catch (...) {
#if defined(ZERO_STUDENT_PROBE)
        sdk::set_probe_ground_utility_scene(nullptr);teacher_ground_scene=nullptr;ground_scene=GroundUtilityScene{};
#endif
        std::printf("teacher error: unknown\n"); teacher_request.phase = -2;
    }
    while (true) cambc::end_turn();
}
#else
#include "../src/main.cpp"
#endif
#else
#define main included_fastrun_main
#include "../../../../tools/fastrun/fastrun.cpp"
#undef main
#include "teacher_lifecycle.h"
#include "teacher_source.h"

namespace {
std::int32_t replay_call(EngineRt& E, const char* name, std::initializer_list<Raw> args) {
    wasmtime_extern_t item;
    if (!wasmtime_instance_export_get(E.ctx, &E.instance, name, std::strlen(name), &item) || item.kind != WASMTIME_EXTERN_FUNC) die(std::string("missing ")+name);
    Raw buf[12]{}; std::size_t n=0; for(const auto& a:args)buf[n++]=a;
    wasm_trap_t* trap=nullptr;
    ok(wasmtime_func_call_unchecked(E.ctx,&item.of.func,buf,std::max<std::size_t>(n,1),&trap),name);
    if(trap)die(std::string(name)+": "+take_trap(trap));
    if(buf[0].i32)die(std::string(name)+": "+E.last_error());
    return buf[0].i32;
}
std::uint32_t u32(EngineRt& E,std::uint32_t p){std::uint32_t v;std::memcpy(&v,E.mem()+p,4);return v;}
std::vector<std::uint8_t> snapshot_bytes(EngineRt& E,std::int64_t g,std::uint32_t p,bool full=false){
    const auto n=full?184760u:40152u;
    if(full){put_i32(E,E.s_persp,2,-1);replay_call(E,"Game_Visualiser_ReadState",{I64(g),U(E.s_persp),U(p)});}
    else E.call(F_ReadTimeline,{I64(g),U(p)});
    std::vector<std::uint8_t>b(E.mem()+p,E.mem()+p+n);std::fill(b.begin()+48,b.begin()+56,0);return b;
}
void ready_prefix(EngineRt& E,std::int64_t g,std::int64_t timeline,int input,std::uint32_t p){
    replay_call(E,"Game_Visualiser_Timeline_Seek",{I64(g),I64(timeline),I32(input),I32(0)});
    for(int i=0;i<256;++i){E.call(F_ReadTimeline,{I64(g),U(p)});if(u32(E,p+20)){if(u32(E,p+56)!=static_cast<unsigned>(input))die("wrong prefix");return;}replay_call(E,"Game_Visualiser_Timeline_ReplayNext",{I64(g),I32(1)});}die("prefix not ready");
}
void prepare_game(Game& g,EngineRt& E,const Options& opt,Program& p,std::int64_t handle){
    g.E=&E;g.opt=&opt;g.handle=handle;g.program[0]=g.program[1]=&p;
    E.call(F_MapMeta,{I64(handle),U(E.s_meta)});
    g.width=u32(E,E.s_meta);g.height=u32(E,E.s_meta+4);g.chunked_w=u32(E,E.s_meta+8);g.chunked_h=u32(E,E.s_meta+12);g.stride=u32(E,E.s_meta+16);
    E.call(F_BoardSize,{U(E.s_meta),U(E.s_out)});g.board_bytes=u32(E,E.s_out);
    E.call(F_FieldSize,{U(E.s_meta),U(E.s_out)});g.field_bytes=u32(E,E.s_out);
    g.chunks=static_cast<std::uint64_t>(g.chunked_w)*g.chunked_h;g.board_words=g.board_bytes/8;g.field_words=g.field_bytes/8;
    for(auto& b:g.bb)b=E.malloc(g.board_bytes);for(auto& f:g.df)f=E.malloc(g.field_bytes);g.buf_cap=65536;g.buf=E.malloc(g.buf_cap);
}
std::pair<int,int> actor_position(const std::vector<std::uint8_t>& state,int actor){
    std::uint32_t count;std::memcpy(&count,state.data()+172,4);
    for(std::uint32_t i=0;i<count;++i){const auto* p=state.data()+196+i*252;std::uint32_t id;std::memcpy(&id,p+4,4);if(id==static_cast<unsigned>(actor)){int x,y;std::memcpy(&x,p+16,4);std::memcpy(&y,p+20,4);return{x,y};}}
    die("lifecycle actor absent from official snapshot");return{};
}
std::uint32_t actor_gold(const std::vector<std::uint8_t>& state,int actor){std::uint32_t count;std::memcpy(&count,state.data()+172,4);for(std::uint32_t i=0;i<count;++i){const auto* p=state.data()+196+i*252;std::uint32_t id;std::memcpy(&id,p+4,4);if(id==static_cast<unsigned>(actor)){std::uint32_t gold;std::memcpy(&gold,p+36,4);return gold;}}die("actor gold absent");return 0;}
bool run_lifecycle(Shared& shared,Game& game,EngineRt& engine,Program& program,std::int64_t handle,std::int64_t timeline,int input,std::uint32_t snap,
                   std::uint64_t known,const std::uint64_t* arguments,std::uint64_t dispatch_known,const std::uint64_t* dispatch,std::uint64_t root_bound,
                   std::uint64_t refresh_known,const std::uint64_t* refresh,bool priority_move=false,bool normal_defaults=false,bool move_only=false){
    ready_prefix(engine,handle,timeline,input-1,snap);replay_call(engine,"Game_Visualiser_Timeline_Branch",{I64(handle),I64(timeline),I32(input-1),U(engine.s_out)});
    if(engine.call(F_GetState,{I64(handle),U(engine.s_out)}))die("lifecycle initial state query failed");const int team=u32(engine,engine.s_out+4),actor=u32(engine,engine.s_out+12),start_round=u32(engine,engine.s_out+8);
    if(u32(engine,engine.s_out)!=1||team<0||team>1||actor!=0)die("lifecycle initial phase/owner invalid (fixture requires actor0)");
    auto bot=new_bot(shared,game,team,actor);auto* memory=wasmtime_memory_data(bot->ctx,&bot->memory);auto size=wasmtime_memory_data_size(bot->ctx,&bot->memory);
    const auto* found=std::search(memory,memory+size,zero_lifecycle::magic,zero_lifecycle::magic+16);if(found==memory+size)die("lifecycle control missing");const auto offset=found-memory;
    if(static_cast<std::size_t>(offset)+sizeof(zero_lifecycle::Control)>size||std::search(found+1,static_cast<const std::uint8_t*>(memory+size),zero_lifecycle::magic,zero_lifecycle::magic+16)!=memory+size)die("lifecycle control ambiguous/outside memory");
    zero_lifecycle::Control control{};std::memcpy(control.magic,zero_lifecycle::magic,16);control.enabled=1;control.last_init=-1;control.trace_enabled=1;control.known=known;control.dispatch_known=dispatch_known;control.root_bound=root_bound;control.mode=move_only?2:priority_move?1:0;control.normal_defaults=normal_defaults;
    control.refresh_known=refresh_known;
    for(int i=0;i<15;++i){control.arguments[i]=arguments[i];control.dispatch[i]=dispatch[i];control.refresh[i]=refresh[i];}
    std::memcpy(memory+offset,&control,sizeof control);int foreign_ends=0;bool passed=false;
    for(int interval=0;interval<8;++interval){
        if(engine.call(F_GetState,{I64(handle),U(engine.s_out)}))die("lifecycle owner state query failed");const int round=u32(engine,engine.s_out+8),owner=u32(engine,engine.s_out+12);
        if(u32(engine,engine.s_out)!=1||u32(engine,engine.s_out+4)!=static_cast<unsigned>(team)||owner!=actor||round>start_round+8)die("lifecycle owner/round boundary");
        const int previous_stage=control.last_init,previous_decisions=control.decisions,previous_phase=control.phase;
        const auto before=actor_position(snapshot_bytes(engine,handle,snap),actor);
        const std::uint64_t budget=bot->turns==0?200000:100000;std::uint64_t used=0;
        game.log[0].clear();game.log[1].clear();game.fault_text.clear();lifecycle_events.clear();
        lifecycle_command_choices=0;lifecycle_dispatches=0;
        const int reason=run_turn(game,*bot,budget,used);
        memory=wasmtime_memory_data(bot->ctx,&bot->memory);std::memcpy(&control,memory+offset,sizeof control);
        const auto after=actor_position(snapshot_bytes(engine,handle,snap),actor);
        const bool reserve=used<=budget-8000,progress=control.last_init>previous_stage||control.decisions>previous_decisions||control.phase>previous_phase;
        const bool dispatch_complete=lifecycle_command_choices==lifecycle_dispatches;
        const bool useful=control.primitive_accepted&&before!=after;
        const auto log=game.log[0]+game.log[1];
        std::printf("{\"lifecycle\":{\"interval\":%d,\"actor\":%d,\"round\":%d,\"budget\":%llu,\"used\":%llu,\"reason\":%d,\"stageBefore\":%d,\"stageAfter\":%d,\"decisions\":%d,\"phase\":%d,\"failed\":%d,\"reserve\":%s,\"progress\":%s,\"usefulPrimitive\":%s,\"from\":[%d,%d],\"to\":[%d,%d],\"foreignEnds\":%d,\"log\":\"%s\"}}\n",interval,actor,round,static_cast<unsigned long long>(budget),static_cast<unsigned long long>(used),reason,previous_stage,control.last_init,control.decisions,control.phase,control.failed,reserve?"true":"false",progress?"true":"false",useful?"true":"false",before.first,before.second,after.first,after.second,foreign_ends,json_escape(log).c_str());
        for(const auto& event:lifecycle_events)std::puts(event.c_str());
        std::printf("{\"lifecycleDispatchCheck\":{\"interval\":%d,\"choices\":%d,\"dispatches\":%d,\"complete\":%s}}\n",interval,lifecycle_command_choices,lifecycle_dispatches,dispatch_complete?"true":"false");
        if(reason||!reserve||control.failed||!progress||!dispatch_complete)break;
        if(priority_move&&control.decisions>previous_decisions&&!(control.priority_done&&useful&&lifecycle_command_choices==2&&lifecycle_dispatches==2))break;
        if(move_only&&control.decisions>previous_decisions&&!(budget==100000&&useful&&lifecycle_command_choices==1&&lifecycle_dispatches==1))break;
        if(engine.call(F_EndTurn,{I64(handle),I32(0)}))die("lifecycle owner END refused");
        if(useful){passed=true;break;}
        bool ready=false;
        for(int step=0;step<1024;++step){if(engine.call(F_Advance,{I64(handle)}))die("lifecycle advance failed");if(engine.call(F_GetState,{I64(handle),U(engine.s_out)}))die("lifecycle foreign state query failed");const int phase=u32(engine,engine.s_out),next=u32(engine,engine.s_out+12);if(u32(engine,engine.s_out+8)>static_cast<unsigned>(start_round+8))break;if(phase!=1)break;if(next==actor){ready=true;break;}if(next<0||u32(engine,engine.s_out+4)>1)die("lifecycle foreign owner invalid");if(engine.call(F_EndTurn,{I64(handle),I32(0)}))die("lifecycle foreign END refused");++foreign_ends;}
        if(!ready)break;
    }
    std::printf("{\"lifecycleResult\":{\"pass\":%s,\"actor\":%d,\"foreignEnds\":%d,\"priorityMoveSameInterval\":%s,\"moveOnlyOrdinaryInterval\":%s,\"normalDefaults\":%s,\"profileHookMode\":%d,\"labelMode\":\"diagnostic_control\",\"trainingQualified\":false}}\n",passed?"true":"false",actor,foreign_ends,priority_move&&passed?"true":"false",move_only&&passed?"true":"false",normal_defaults?"true":"false",control.installed);return passed;
}
bool run_draft_frame(Shared& shared,Game& game,EngineRt& engine,std::int64_t handle,std::int64_t timeline,std::uint32_t snap,int raw_pick,const fs::path& destination){
    ready_prefix(engine,handle,timeline,0,snap);const auto expected_before=snapshot_bytes(engine,handle,snap),expected_before_full=snapshot_bytes(engine,handle,snap,true);
    replay_call(engine,"Game_Visualiser_Timeline_ReplayNext",{I64(handle),I32(1)});const auto expected_dispatch=snapshot_bytes(engine,handle,snap),expected_dispatch_full=snapshot_bytes(engine,handle,snap,true);
    replay_call(engine,"Game_Visualiser_Timeline_ReplayNext",{I64(handle),I32(1)});const auto expected_resolution=snapshot_bytes(engine,handle,snap),expected_resolution_full=snapshot_bytes(engine,handle,snap,true);
    ready_prefix(engine,handle,timeline,0,snap);replay_call(engine,"Game_Visualiser_Timeline_Branch",{I64(handle),I64(timeline),I32(0),U(engine.s_out)});
    if(snapshot_bytes(engine,handle,snap)!=expected_before||snapshot_bytes(engine,handle,snap,true)!=expected_before_full)die("draft frame branch prefix differs");
    if(engine.call(F_GetState,{I64(handle),U(engine.s_out)}))die("draft frame runner state failed");
    const bool phase_checked=u32(engine,engine.s_out)==0&&u32(engine,engine.s_out+4)==0&&u32(engine,engine.s_out+8)==0&&u32(engine,engine.s_out+12)==0xffffffffu;
    if(!phase_checked)die("draft frame requires original team0 round0 prefix");
    auto bot=new_bot(shared,game,0,-1);auto* memory=wasmtime_memory_data(bot->ctx,&bot->memory);const auto size=wasmtime_memory_data_size(bot->ctx,&bot->memory);
    const auto* found=std::search(memory,memory+size,zero_lifecycle::magic,zero_lifecycle::magic+16);if(found==memory+size)die("draft control missing");const auto offset=found-memory;
    if(static_cast<std::size_t>(offset)+sizeof(zero_lifecycle::Control)>size||std::search(found+1,static_cast<const std::uint8_t*>(memory+size),zero_lifecycle::magic,zero_lifecycle::magic+16)!=memory+size)die("draft control ambiguous/outside memory");
    zero_lifecycle::Control control{};std::memcpy(control.magic,zero_lifecycle::magic,16);control.enabled=1;control.trace_enabled=1;control.draft_mode=1;control.draft_raw_pick=raw_pick;control.draft_index=-1;
    std::memcpy(memory+offset,&control,sizeof control);native_draft_body.clear();native_draft_frames=0;native_draft_rows=0;native_draft_label=-1;native_drafted_index=-1;native_drafted_target=-1;lifecycle_events.clear();
    game.log[0].clear();game.log[1].clear();game.fault_text.clear();std::uint64_t used=0;constexpr std::uint64_t budget=1000000;
    const int reason=run_turn(game,*bot,budget,used);memory=wasmtime_memory_data(bot->ctx,&bot->memory);std::memcpy(&control,memory+offset,sizeof control);
    const bool dispatch_equal=snapshot_bytes(engine,handle,snap)==expected_dispatch&&snapshot_bytes(engine,handle,snap,true)==expected_dispatch_full;
    bool resolution_equal=false;int end_code=-1;
    if(reason==0){end_code=engine.call(F_EndTurn,{I64(handle),I32(0)});if(end_code==0)resolution_equal=snapshot_bytes(engine,handle,snap)==expected_resolution&&snapshot_bytes(engine,handle,snap,true)==expected_resolution_full;}
    int selected_payload=-1;if(native_draft_label>=0&&native_draft_label<native_draft_rows&&native_draft_body.size()==48u*4u+static_cast<std::size_t>(native_draft_rows)*116u)std::memcpy(&selected_payload,native_draft_body.data()+48u*4u+native_draft_label*116u+4u,4);
    const bool passed=reason==0&&used<=budget-8000&&phase_checked&&dispatch_equal&&resolution_equal&&end_code==0&&control.draft_done&&!control.failed&&native_draft_frames==1&&native_draft_bits_equal&&native_draft_label==native_drafted_index&&selected_payload==raw_pick&&native_drafted_target==raw_pick;
    if(passed){std::ofstream output(destination,std::ios::binary);output.write(reinterpret_cast<const char*>(native_draft_body.data()),native_draft_body.size());if(!output)die("draft body publication failed");}
    const auto log=game.log[0]+game.log[1];
    std::printf("{\"draftFrame\":{\"pass\":%s,\"reason\":%d,\"budget\":%llu,\"used\":%llu,\"phaseChecked\":%s,\"dispatchEqual\":%s,\"resolutionEqual\":%s,\"endCode\":%d,\"frames\":%d,\"rows\":%d,\"label\":%d,\"natural\":%d,\"selectedRawPick\":%d,\"featureBitsEqual\":%s,\"bodyBytes\":%llu,\"log\":\"%s\"}}\n",passed?"true":"false",reason,static_cast<unsigned long long>(budget),static_cast<unsigned long long>(used),phase_checked?"true":"false",dispatch_equal?"true":"false",resolution_equal?"true":"false",end_code,native_draft_frames,native_draft_rows,native_draft_label,native_draft_natural,selected_payload,native_draft_bits_equal?"true":"false",static_cast<unsigned long long>(native_draft_body.size()),json_escape(log).c_str());
    for(const auto& event:lifecycle_events)std::puts(event.c_str());return passed;
}
wasm_trap_t* source_probe_extra(wasmtime_caller_t* caller,Raw* args,std::size_t){
    auto* bot=bot_of(caller);auto& game=*bot->game;auto& engine=*game.E;const auto memory=guest(caller,bot);const int event=args[0].i32;const auto p=static_cast<std::uint32_t>(args[1].i32);
    auto control=probe_read<zero_source_probe::Control>(memory,static_cast<std::uint32_t>(source_control_offset));
    auto fail=[&](int code){control.failed=code;std::memcpy(memory.base+source_control_offset,&control,sizeof control);};
    if(control.current<0||control.current>=5){fail(9);return nullptr;}
    const auto& step=control.steps[control.current];std::ostringstream out;out.precision(9);
    if(event==7){const auto w=probe_read<zero_lifecycle::Trace>(memory,p);SourceFactBody record{step.input,w.stage,w.head,w.command,w.count,w.label,w.rule,{}};
        if(w.count<0||w.count>100000||!memory.ok(w.state,192)){fail(10);return nullptr;}
        record.body.assign(memory.base+w.state,memory.base+w.state+192);
        auto append=[&](std::int32_t value){const auto* at=reinterpret_cast<const std::uint8_t*>(&value);record.body.insert(record.body.end(),at,at+4);};
        for(int i=0;i<w.count;++i){const auto row=w.rows+i*w.stride;append(probe_read<std::uint8_t>(memory,row+w.kind_offset));append(probe_read<std::int32_t>(memory,row+w.target_offset));append(probe_read<std::int32_t>(memory,row+w.count_offset));append(probe_read<std::int16_t>(memory,row+w.x_offset));append(probe_read<std::int16_t>(memory,row+w.y_offset));if(!memory.ok(row+w.phi_offset,96)){fail(11);return nullptr;}record.body.insert(record.body.end(),memory.base+row+w.phi_offset,memory.base+row+w.phi_offset+96);}
        out<<"{\"sourceFacts\":{\"input\":"<<step.input<<",\"stage\":"<<w.stage<<",\"head\":"<<w.head<<",\"command\":"<<w.command<<",\"rows\":"<<w.count<<",\"label\":"<<w.label<<",\"semanticMask\":"<<w.rule<<",\"admittedMask\":null,\"bodyBytes\":"<<record.body.size()<<"}}";
        source_fact_bodies.push_back(std::move(record));lifecycle_events.push_back(out.str());
    }else if(event==8){const int index=probe_read<int>(memory,p);bool equal=false;int end_code=0;
        if(index!=source_expected_cursor||index!=control.current){fail(12);return nullptr;}
        if(step.type==0)end_code=engine.call(F_EndTurn,{I64(game.handle),I32(0)});
        const auto scratch=engine.malloc(184760);
        equal=end_code==0&&snapshot_bytes(engine,game.handle,scratch)==source_expected_timeline[index]&&snapshot_bytes(engine,game.handle,scratch,true)==source_expected_full[index];engine.free(scratch);
        out<<"{\"sourceSuccessor\":{\"input\":"<<step.input<<",\"equal\":"<<(equal?"true":"false")<<",\"endCode\":"<<end_code<<"}}";lifecycle_events.push_back(out.str());
        if(!equal)fail(13);else ++source_expected_cursor;
    }else if(event==9){const auto w=probe_read<zero_source_probe::Observation>(memory,p);out<<"{\"sourceObservation\":{\"input\":"<<w.input<<",\"commandIndex\":"<<w.command_index<<",\"actor\":"<<w.actor<<",\"characterType\":"<<w.type<<",\"at\":["<<w.x<<','<<w.y<<"],\"hp\":"<<w.hp<<",\"shield\":"<<w.shield<<",\"gold\":"<<w.gold<<",\"gems\":"<<w.gems<<",\"cooldown\":["<<w.cd0<<','<<w.cd1<<"],\"stateBits\":"<<w.state<<",\"channelCode\":"<<w.channel<<",\"channelRemaining\":"<<w.remaining<<",\"nextPriority\":"<<w.next_priority;
        auto ids=[&](const char* name,std::uint32_t at,std::uint32_t n,std::uint32_t stride,std::uint32_t offset){out<<",\""<<name<<"\":[";for(std::uint32_t i=0;i<n;++i)out<<(i?",":"")<<probe_read<std::int32_t>(memory,at+i*stride+offset);out<<']';};
        ids("allies",w.allies,w.allies_n,w.unit_stride,w.unit_id_offset);ids("enemies",w.enemies,w.enemies_n,w.unit_stride,w.unit_id_offset);ids("camps",w.camps,w.camps_n,w.camp_stride,w.camp_id_offset);out<<"}}";lifecycle_events.push_back(out.str());
    }else fail(14);
    return nullptr;
}
bool run_source_five(Shared& shared,Game& game,EngineRt& engine,std::int64_t handle,std::int64_t timeline,std::uint32_t snap,const fs::path& directory,std::uint64_t budget,bool priority_parity){
    std::ifstream input(directory/"source-intents.txt");zero_source_probe::Control control{};std::memcpy(control.magic,zero_source_probe::magic,16);control.count=5;control.priority_parity=priority_parity;
    for(int i=0;i<5;++i){auto& s=control.steps[i];if(!(input>>s.sequence>>s.input>>s.raw_kind>>s.type>>s.kind>>s.target>>s.count>>s.x>>s.y>>s.target_kind>>s.target_id>>s.scalar_known>>s.scalar))die("incomplete shared canonical intent transport");if(s.input!=26+i||s.sequence!=25+i)die("source SDK diagnostic is fixed to26..30");}
    std::string extra;if(input>>extra)die("unexpected canonical intent transport fields");
    ready_prefix(engine,handle,timeline,25,snap);const auto expected_before=snapshot_bytes(engine,handle,snap),expected_before_full=snapshot_bytes(engine,handle,snap,true);source_expected_timeline.clear();source_expected_full.clear();
    for(int i=0;i<5;++i){replay_call(engine,"Game_Visualiser_Timeline_ReplayNext",{I64(handle),I32(1)});const auto expected=snapshot_bytes(engine,handle,snap);if(u32(engine,snap+56)!=static_cast<unsigned>(26+i))die("source oracle did not advance one requested input");source_expected_timeline.push_back(expected);source_expected_full.push_back(snapshot_bytes(engine,handle,snap,true));}
    ready_prefix(engine,handle,timeline,25,snap);replay_call(engine,"Game_Visualiser_Timeline_Branch",{I64(handle),I64(timeline),I32(25),U(engine.s_out)});
    if(snapshot_bytes(engine,handle,snap)!=expected_before||snapshot_bytes(engine,handle,snap,true)!=expected_before_full)die("source SDK ready branch differs");
    if(engine.call(F_GetState,{I64(handle),U(engine.s_out)})||u32(engine,engine.s_out)!=1||u32(engine,engine.s_out+4)!=1||u32(engine,engine.s_out+12)!=6)die("source SDK branch is not actor6team1");
    auto bot=new_bot(shared,game,1,6);auto* memory=wasmtime_memory_data(bot->ctx,&bot->memory);const auto size=wasmtime_memory_data_size(bot->ctx,&bot->memory);
    const auto* found=std::search(memory,memory+size,zero_source_probe::magic,zero_source_probe::magic+16);if(found==memory+size)die("source SDK control missing");source_control_offset=found-memory;
    if(static_cast<std::size_t>(source_control_offset)+sizeof control>size||std::search(found+1,static_cast<const std::uint8_t*>(memory+size),zero_source_probe::magic,zero_source_probe::magic+16)!=memory+size)die("source SDK control ambiguous/outside memory");std::memcpy(memory+source_control_offset,&control,sizeof control);
    lifecycle_events.clear();source_fact_bodies.clear();source_expected_cursor=0;extra_probe_handler=source_probe_extra;game.log[0].clear();game.log[1].clear();game.fault_text.clear();std::uint64_t used=0;
    const int reason=run_turn(game,*bot,budget,used);extra_probe_handler=nullptr;memory=wasmtime_memory_data(bot->ctx,&bot->memory);std::memcpy(&control,memory+source_control_offset,sizeof control);
    const bool passed=reason==0&&!control.failed&&control.completed==5&&source_expected_cursor==5&&source_fact_bodies.size()==9;
    for(std::size_t i=0;i<source_fact_bodies.size();++i){const auto& f=source_fact_bodies[i];std::ofstream output(directory/("sdk-facts-"+std::to_string(i)+".bin"),std::ios::binary);output.write(reinterpret_cast<const char*>(f.body.data()),f.body.size());if(!output)die("source SDK diagnostic body write failed");}
    std::printf("{\"sourceSDK\":{\"pass\":%s,\"reason\":%d,\"failed\":%d,\"completed\":%d,\"successors\":%d,\"factBodies\":%llu,\"budget\":%llu,\"used\":%llu,\"observationFuel\":%llu,\"argumentFuel\":%llu,\"dispatchFuel\":%llu,\"refreshFuel\":%llu,\"noNN\":true,\"deploymentQualified\":false,\"log\":\"%s\"}}\n",passed?"true":"false",reason,control.failed,control.completed,source_expected_cursor,static_cast<unsigned long long>(source_fact_bodies.size()),static_cast<unsigned long long>(budget),static_cast<unsigned long long>(used),static_cast<unsigned long long>(control.observation_fuel),static_cast<unsigned long long>(control.argument_fuel),static_cast<unsigned long long>(control.dispatch_fuel),static_cast<unsigned long long>(control.refresh_fuel),json_escape(game.log[0]+game.log[1]).c_str());
    for(const auto& event:lifecycle_events)std::puts(event.c_str());return passed;
}
}
int main(int argc,char**argv){
    if(argc!=6&&argc!=7)die("teacher_force ENGINE PROBE_WASM REPLAY REQUESTS CACHE [WEIGHTS]");
    Shared sh;sh.engine_engine=wasm_engine_new_with_config(wasm_config_new());
    auto* bc=wasm_config_new();wasmtime_config_consume_fuel_set(bc,true);wasmtime_config_async_stack_size_set(bc,8u<<20);sh.bot_engine=wasm_engine_new_with_config(bc);
    sh.engine_module=load_module(sh.engine_engine,argv[1],argv[5],"engine");link_engine(sh);sh.programs.resize(1);auto& prog=sh.programs[0];prog.path=argv[2];prog.name="zero-teacher";
    prog.module=load_module(sh.bot_engine,prog.path,argv[5],"bot");link_probe_bot(sh,prog);Weights weights;if(argc==7){find_params(sh,prog);weights=read_weights(argv[6]);}auto engine=new_engine(sh);auto& E=*engine;
    const auto bytes=read_file(argv[3]);const auto in=E.malloc(bytes.size());std::memcpy(E.mem()+in,bytes.data(),bytes.size());replay_call(E,"Game_FromReplayFileBytes",{U(in),U(bytes.size()),U(E.s_out)});
    std::int64_t handle,timeline;std::memcpy(&handle,E.mem()+E.s_out,8);replay_call(E,"Game_Visualiser_Timeline_GetMain",{I64(handle),U(E.s_out)});std::memcpy(&timeline,E.mem()+E.s_out,8);
    const auto snap=E.malloc(184760);Options opt;Game g;prepare_game(g,E,opt,prog,handle);
    if(!weights.empty())g.weights[0]=g.weights[1]=&weights;
    std::ifstream requests(argv[4]);int input,type,target,x,y,flags,reserve,budget,quantity,source_kind,command_index,total=0;std::uint64_t known_mask,argument_bounds[15],dispatch_known,dispatch_bounds[15],root_bound,refresh_known,refresh_bounds[15];const char magic[16]={'z','e','r','o','.','t','e','a','c','h','e','r','.','v','1',0};
    while(requests>>input>>type>>target>>x>>y>>flags>>reserve>>budget>>quantity>>source_kind>>command_index>>known_mask){
        for(auto& value:argument_bounds)if(!(requests>>value))die("incomplete argument bounds");
        if(!(requests>>dispatch_known>>root_bound))die("missing dispatch/root bounds");
        for(auto& value:dispatch_bounds)if(!(requests>>value))die("incomplete dispatch bounds");
        if(!(requests>>refresh_known))die("missing refresh known mask");for(auto& value:refresh_bounds)if(!(requests>>value))die("incomplete refresh bounds");
        if(flags&8192){run_source_five(sh,g,E,handle,timeline,snap,fs::path(argv[4]).parent_path(),budget,(flags&32768)!=0);++total;continue;}
        if(flags&4096){run_draft_frame(sh,g,E,handle,timeline,snap,target,fs::path(argv[4]).parent_path()/"draft-body.bin");++total;continue;}
        if(flags&2048){run_lifecycle(sh,g,E,prog,handle,timeline,input,snap,known_mask,argument_bounds,dispatch_known,dispatch_bounds,root_bound,refresh_known,refresh_bounds,(flags&16384)!=0,(flags&65536)!=0,(flags&262144)!=0);++total;continue;}
        ready_prefix(E,handle,timeline,input-1,snap);if(!(flags&16))replay_call(E,"Game_Visualiser_Timeline_ReplayNext",{I64(handle),I32(1)});
        const auto expected=snapshot_bytes(E,handle,snap),expected_full=snapshot_bytes(E,handle,snap,true);
        ready_prefix(E,handle,timeline,input-1,snap);replay_call(E,"Game_Visualiser_Timeline_Branch",{I64(handle),I64(timeline),I32(input-1),U(E.s_out)});
        E.call(F_GetState,{I64(handle),U(E.s_out)});const int team=u32(E,E.s_out+4),actor=u32(E,E.s_out+12);
        auto bot=new_bot(sh,g,team,actor);auto* mem=wasmtime_memory_data(bot->ctx,&bot->memory);const auto size=wasmtime_memory_data_size(bot->ctx,&bot->memory);
        const auto* at=std::search(mem,mem+size,magic,magic+16);if(at==mem+size)die("teacher request block missing");const auto offset=at-mem;
        TeacherRequest request{};std::memcpy(request.magic,magic,16);request.type=type;request.target=target;request.x=x;request.y=y;request.result=-1;request.flags=flags;request.refresh_reserve=reserve;request.quantity=quantity;request.decision_budget=budget;
        request.source_kind=source_kind;request.source_command_index=command_index;request.student_known_mask=known_mask;for(int i=0;i<15;++i)request.student_bounds[i]=argument_bounds[i];
        request.student_dispatch_known=dispatch_known;request.student_root_bound=root_bound;for(int i=0;i<15;++i)request.student_dispatch[i]=dispatch_bounds[i];
        std::memcpy(mem+offset,&request,sizeof request);g.log[0].clear();g.log[1].clear();g.fault_text.clear();lifecycle_events.clear();
        std::uint64_t init_fuel=0,decision_fuel=0;const int init_reason=run_turn(g,*bot,2000000,init_fuel);
        mem=wasmtime_memory_data(bot->ctx,&bot->memory);std::memcpy(&request,mem+offset,sizeof request);
        int reason=-1;if(init_reason==0&&request.phase==1)reason=run_turn(g,*bot,budget,decision_fuel);
        mem=wasmtime_memory_data(bot->ctx,&bot->memory);std::memcpy(&request,mem+offset,sizeof request);
        if(reason==0&&request.phase==2&&request.represented&&request.result==1&&type==0)E.call(F_EndTurn,{I64(handle),I32(0)});
        const auto actual=snapshot_bytes(E,handle,snap),actual_full=snapshot_bytes(E,handle,snap,true);
        const auto actual_position=actor_position(actual,actor);
        const bool equal=actual==expected&&actual_full==expected_full;
        const auto log=g.log[0]+g.log[1];
        std::printf("{\"input\":%d,\"type\":%d,\"actor\":%d,\"initReason\":%d,\"reason\":%d,\"phase\":%d,\"represented\":%d,\"primitiveResult\":%d,\"candidateStatus\":%d,\"candidates\":%d,\"equal\":%s,\"initFuel\":%llu,\"decisionFuel\":%llu,\"observationFuel\":%llu,\"candidateFuel\":%llu,\"executionFuel\":%llu,\"refreshFuel\":%llu,\"log\":\"%s\"}\n",input,type,actor,init_reason,reason,request.phase,request.represented,request.result,request.status,request.count,equal?"true":"false",static_cast<unsigned long long>(init_fuel),static_cast<unsigned long long>(decision_fuel),static_cast<unsigned long long>(request.observation_fuel),static_cast<unsigned long long>(request.candidate_fuel),static_cast<unsigned long long>(request.execution_fuel),static_cast<unsigned long long>(request.refresh_fuel),json_escape(log).c_str());
        std::printf("{\"input\":%d,\"sdkObservation\":{\"at\":[%d,%d],\"hp\":%.9g,\"gold\":%d,\"gems\":%d,\"cooldown\":[%d,%d],\"stateBits\":%d,\"allowances\":%d,\"allies\":%d,\"enemies\":%d,\"camps\":%d}}\n",input,request.obs_x,request.obs_y,static_cast<double>(request.obs_hp),request.obs_gold,request.obs_gems,request.obs_cd0,request.obs_cd1,request.obs_state,request.obs_allowances,request.obs_allies,request.obs_enemies,request.obs_camps);
        if(flags&1024){const auto from=actor_position(expected,actor),to=actor_position(actual,actor);std::int32_t actual_kind,actual_priority;std::memcpy(&actual_kind,actual.data()+68,4);std::memcpy(&actual_priority,actual.data()+116,4);const bool progress=request.result==1&&(type==14?(actual_kind==9&&actual_priority==target):from!=to);std::printf("{\"input\":%d,\"diagnosticControl\":{\"controlProgress\":%s,\"from\":[%d,%d],\"to\":[%d,%d],\"labelMode\":\"diagnostic_control\"}}\n",input,progress?"true":"false",from.first,from.second,to.first,to.second);}
        std::printf("{\"input\":%d,\"benchmark\":{\"networkFuel\":%llu,\"stage\":%d,\"refreshed\":%d}}\n",input,static_cast<unsigned long long>(request.network_fuel),request.stage,request.refreshed);
        if(flags&256){
            std::printf("{\"input\":%d,\"student\":{\"status\":%d,\"ruleMask\":%d,\"admittedMask\":%d,\"usedMask\":%d,\"naturalCommand\":%d,\"admittedGreedy\":%d,\"forcedPayload\":[%d,%d,%d,%d,%d],\"predictedPriority\":%d,\"predictedQuantity\":%d,\"knownMask\":%llu,\"bounds\":[",input,request.student_status,request.student_rule_mask,request.student_admitted_mask,request.student_used_mask,request.student_natural,request.student_admitted_greedy,request.student_kind,request.student_target,request.student_count,request.student_x,request.student_y,request.student_priority,request.student_quantity,static_cast<unsigned long long>(request.student_known_mask));
            for(int i=0;i<15;++i)std::printf("%s%llu",i?",":"",static_cast<unsigned long long>(request.student_bounds[i]));
            std::printf("],\"phaseFuel\":[");for(int i=0;i<7;++i)std::printf("%s%llu",i?",":"",static_cast<unsigned long long>(request.student_fuel[i]));
            const auto& detail=request.macro_stages[0];std::printf("],\"argumentDetail\":[%llu,%llu,%llu,%llu,%llu]}}\n",static_cast<unsigned long long>(detail.build_fuel),static_cast<unsigned long long>(detail.projection_fuel),static_cast<unsigned long long>(detail.inference_fuel),static_cast<unsigned long long>(detail.route_fuel),static_cast<unsigned long long>(detail.select_fuel));
            if(flags&(524288|4194304)){std::printf("{\"naturalAdmission\":{\"preRootAllowed\":%s,\"remaining\":%llu,\"required\":%llu,\"rootBound\":%llu,\"contextBuild\":%llu,\"contextAttention\":%llu,\"contextDestroy\":%llu,\"contextEntities\":%llu,\"teacherSource\":%s,\"bypass\":false}}\n",request.pre_root_allowed?"true":"false",static_cast<unsigned long long>(request.pre_root_remaining),static_cast<unsigned long long>(request.pre_root_required),static_cast<unsigned long long>(request.student_root_bound),static_cast<unsigned long long>(request.context_build),static_cast<unsigned long long>(request.context_attention),static_cast<unsigned long long>(request.context_destroy),static_cast<unsigned long long>(request.context_entities),(flags&524288)?"false":"true");std::printf("{\"actionOutcome\":{\"expectedGold\":%u,\"actualGold\":%u,\"officialSuccessorEqual\":%s}}\n",actor_gold(expected,actor),actor_gold(actual,actor),equal?"true":"false");for(const auto& event:lifecycle_events)std::puts(event.c_str());}
            if(flags&(524288|4194304))std::printf("{\"actualPosition\":[%d,%d]}\n",actual_position.first,actual_position.second);
            if(flags&2097152)std::printf("{\"moveExposure\":{\"shadowOnly\":true,\"policyInputsChanged\":false,\"borrowedCurrentRows\":true,\"setupFuel\":%llu,\"setupRows\":%d,\"before\":%llu,\"required\":%llu,\"attempted\":%s,\"completed\":%s,\"fuel\":%llu,\"rows\":%d,\"selected\":[%.17g,%.17g,%.17g,%.17g]}}\n",static_cast<unsigned long long>(request.exposure_setup_fuel),request.exposure_setup_rows,static_cast<unsigned long long>(request.exposure_before),static_cast<unsigned long long>(request.exposure_required),request.exposure_attempted?"true":"false",request.exposure_completed?"true":"false",static_cast<unsigned long long>(request.exposure_fuel),request.exposure_rows,request.exposure_selected[0],request.exposure_selected[1],request.exposure_selected[2],request.exposure_selected[3]);
            if(flags&16777216)std::printf("{\"dashBuilderParity\":{\"checked\":%s,\"pass\":%s,\"rows\":%d,\"floatWords\":%d,\"payloadWords\":%d,\"setupOnly\":true}}\n",request.dash_parity_checked?"true":"false",request.dash_parity_pass?"true":"false",request.dash_parity_rows,request.dash_parity_rows*24,request.dash_parity_rows*5);
            if(flags&67108864)std::printf("{\"groundSceneParity\":{\"checked\":%s,\"pass\":%s,\"rows\":%d,\"stateWords\":48,\"contextWords\":%d,\"rootLogitWords\":15,\"embeddingWords\":16,\"setupOnly\":true}}\n",request.ground_parity_checked?"true":"false",request.ground_parity_pass?"true":"false",request.ground_parity_rows,request.ground_parity_context_words);
            if(flags&268435456)std::printf("{\"buyBuilderParity\":{\"checked\":%s,\"pass\":%s,\"rows\":%d,\"floatWords\":%d,\"quantityFloatWords\":%d,\"setupOnly\":true}}\n",request.buy_parity_checked?"true":"false",request.buy_parity_pass?"true":"false",request.buy_parity_rows,request.buy_parity_rows*24,request.buy_parity_rows*24);
            if(flags&1048576){std::printf("{\"offlineRoot\":{\"scope\":\"setup_model_inspection_not_part_of100K\",\"setupFuel\":%llu,\"ruleMask\":%d,\"supportedMask\":%d,\"greedy\":%d,\"contextEntities\":%d,\"logits\":[",static_cast<unsigned long long>(request.offline_fuel),request.offline_rule,request.offline_supported,request.offline_greedy,request.offline_entities);for(int i=0;i<15;++i)std::printf("%s%.9g",i?",":"",static_cast<double>(request.offline_logits[i]));std::printf("]}}\n");}
            std::printf("{\"contextCache\":{\"prepared\":%s,\"setupFuel\":%llu,\"setupParityChecked\":%s,\"setupRawParity\":%s,\"setupParityWords\":%d,\"scope\":\"public_map_prepare_and_optional_raw_parity_before_decision_interval\"}}\n",request.context_cache_prepared?"true":"false",static_cast<unsigned long long>(request.context_cache_setup_fuel),request.context_cache_parity?"true":"false",request.context_cache_parity==1?"true":"false",request.context_cache_parity_words);
            if(flags&131072){std::printf("{\"continuation\":{\"mode\":\"diagnostic_END_root_inspection_no_dispatch\",\"status\":%d,\"type\":%d,\"naturalCommand\":%d,\"ruleMask\":%d,\"admittedMask\":%d,\"usedMask\":%d,\"outerFuel\":%llu,\"phaseFuel\":[",request.continuation_status,request.continuation_type,request.continuation_natural,request.continuation_rule,request.continuation_admitted,request.continuation_used,static_cast<unsigned long long>(request.continuation_outer));for(int i=0;i<7;++i)std::printf("%s%llu",i?",":"",static_cast<unsigned long long>(request.continuation_fuel[i]));std::printf("],\"post\":{\"gold\":%d,\"gems\":%d,\"priority\":%d,\"channel\":%d,\"remaining\":%d,\"commandIndex\":%d,\"enemies\":%d,\"camps\":%d}}}\n",request.post_gold,request.post_gems,request.post_priority,request.post_channel,request.post_remaining,request.post_counter,request.post_enemies,request.post_camps);}
        }
        std::printf("{\"input\":%d,\"admission\":{\"normalRemainingAfterScore\":%d,\"replanAdmittedAtFrozenBound\":%d,\"controllerGreedyKind\":%d,\"goalCommitted\":%d,\"commitFuel\":%llu}}\n",input,request.normal_after_score,request.macro_admitted,request.controller_greedy_kind,request.goal_committed,static_cast<unsigned long long>(request.commit_fuel));
        std::printf("{\"input\":%d,\"observer\":{\"inventoryEqual\":%d,",input,request.inventory_equal);
        for(int which=0;which<2;++which){
            const auto* f=which?request.initial_observer:request.observer;
            std::printf("%s\"%s\":{\"revision\":%llu,\"round\":%d,\"complete\":%d,\"fuelReset\":%d,\"self\":%llu,\"items\":%llu,\"commands\":%llu,\"champions\":%llu,\"camps\":%llu,\"groups\":%llu,\"locksMoves\":%llu,\"total\":%llu,\"locksRead\":%llu,\"canonicalOrder\":%llu,\"moveArguments\":%llu,\"groundUtilities\":%llu}",
                which?",":"",which?"initial":"measured",static_cast<unsigned long long>(f[0]),
                which?request.initial_observer_round:request.observer_round,which?request.initial_observer_complete:request.observer_complete,
                which?request.initial_observer_reset:request.observer_reset,
                static_cast<unsigned long long>(f[1]),static_cast<unsigned long long>(f[2]),static_cast<unsigned long long>(f[3]),
                static_cast<unsigned long long>(f[4]),static_cast<unsigned long long>(f[5]),static_cast<unsigned long long>(f[6]),
                static_cast<unsigned long long>(f[7]),static_cast<unsigned long long>(f[8]),static_cast<unsigned long long>(f[9]),
                static_cast<unsigned long long>(f[10]),static_cast<unsigned long long>(f[11]),static_cast<unsigned long long>(f[12]));
        }
        std::printf("}}\n");
        if(flags&16){
            std::printf("{\"input\":%d,\"macro\":{\"target\":%d,\"warm\":%s,\"legacy\":%s,\"complete\":%d,\"finalKind\":%d,\"finalTarget\":%d,\"stateFuel\":%llu,\"trunkFuel\":%llu,\"controllerBuildFuel\":%llu,\"controllerInferenceFuel\":%llu,\"beginFuel\":%llu,\"planFuel\":%llu,\"legacyPresent\":%d,\"controllerCount\":%d,\"controllerGreedy\":%d,\"stages\":[",input,target,(flags&8)?"true":"false",(flags&32)?"true":"false",request.macro_complete,request.macro_kind,request.macro_target,static_cast<unsigned long long>(request.state_fuel),static_cast<unsigned long long>(request.trunk_fuel),static_cast<unsigned long long>(request.controller_build_fuel),static_cast<unsigned long long>(request.controller_inference_fuel),static_cast<unsigned long long>(request.plan_begin_fuel),static_cast<unsigned long long>(request.plan_fuel),request.legacy_present,request.controller_count,request.controller_greedy);
            for(int i=0;i<request.macro_count&&i<12;++i){const auto& m=request.macro_stages[i];std::printf("%s{\"stage\":%d,\"depth\":%d,\"begin\":%d,\"end\":%d,\"count\":%d,\"status\":%d,\"teacher\":%d,\"greedy\":%d,\"buildFuel\":%llu,\"inferenceFuel\":%llu,\"routeFuel\":%llu,\"selectFuel\":%llu,\"projectionFuel\":%llu}",i?",":"",m.stage,m.depth,m.begin,m.end,m.count,m.status,m.teacher,m.greedy,static_cast<unsigned long long>(m.build_fuel),static_cast<unsigned long long>(m.inference_fuel),static_cast<unsigned long long>(m.route_fuel),static_cast<unsigned long long>(m.select_fuel),static_cast<unsigned long long>(m.projection_fuel));}
            std::printf("]}}\n");
        }
        total++;
    }
    replay_call(E,"Game_Destroy",{I64(handle)});return total?0:2;
}
#endif
