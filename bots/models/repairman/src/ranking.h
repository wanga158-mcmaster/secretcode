#pragma once
#include "runtime.h"
#include "../../common/safe-segment-execution.h"
#include <memory>
namespace plan4::runtime {
class Repairman : public M0 {
    using P=repairman::runtime::PreparedAttack;
    using A=repairman::runtime::AttackFrame;
    using C=repairman::runtime::CyclePlan;
    struct Prefix {Job job{};std::array<ModelPredictionEvent,8>events{};int count=0;};
    struct Storage {
        ActorFrame actors{};CombatKit kit{};SpatialFrame spatial{};A attack{};C cycle{};P prepared{},execution_source{};
        repairman::runtime::Path path;
        std::array<Prefix,max_jobs>prefixes{};
        repairman::WorkRecord actual_work{};
        safe_segment::AttackContext proposal_actor{};
        const Information*snapshot=nullptr,*source=nullptr;
        int chosen=-1,round=-1,actor=-1;Point position{};float health=0,maximum=0;
        unsigned bag=0,cash=0,ours=0,enemy=0;
        bool have_attack=false,have_cycle=false;
        std::uint64_t selected_epoch=0;
        int attack_receipt_round=-1;
    };
    std::unique_ptr<Storage>s_=std::make_unique<Storage>();
    bool enabled_=true,used_=false;
    int paths_=0,work_=0,unknown_=0,graph_work_=0;
    std::uint64_t revision_=0;
    repairman::runtime::Unsupported reason_=repairman::runtime::Unsupported::executor_and_continuation_missing;
    void invalidate(){used_=false;s_->source=nullptr;s_->chosen=-1;}
    static bool full_clear(int,int,float health,int clear,float incoming,const Information&info,const void*,void*){
        return clear>=0&&std::isfinite(health)&&std::isfinite(incoming)&&incoming>=0&&
            health-float(clear+1)*incoming>std::max(info.own.maximum_health*0.35f,incoming*2+60);
    }
    bool bind(const Information&info,const Menu&menu){
        if(!s_->have_attack||!s_->have_cycle||s_->snapshot!=&info||!s_->cycle.executor_enabled)return false;
        if(s_->spatial.clock.round!=info.round||s_->spatial.clock.own.id!=info.own.id||s_->spatial.information!=&info||s_->spatial.menu!=&menu)return false;
        if(menu.count<1||menu.count>max_jobs||menu.single_count<0||menu.single_count>menu.count)return false;
        const bool prepared=repairman::runtime::prepare_attack_into(info,s_->actors,s_->kit,s_->attack,s_->prepared,false,s_->spatial.graph_quota);
        graph_work_=s_->prepared.escape.vertices;
        if(!prepared){reason_=s_->prepared.unsupported;return false;}
        const auto&c=s_->cycle;
        if(c.clock.round!=info.round||c.clock.own.id!=info.own.id||c.kit_version!=s_->kit.version||c.stat_epoch!=s_->kit.stat_epoch
            ||c.group_id!=s_->prepared.group.group_id||c.target_member_id!=s_->prepared.actor.target_id
            ||c.lifecycle_version!=s_->prepared.group.lifecycle_version||!c.supported||!c.solo_capture_supported||!c.capture_hold_zero_exposure
            ||!repairman::runtime::quoted_now(c.quote,info.round))return false;
        bool present=false;
        for(int n=0;n<menu.count;++n)present|=menu.jobs[n].kind==JobKind::farm&&menu.jobs[n].group==s_->prepared.group_index;
        if(!present)return false;
        reason_=repairman::runtime::Unsupported::none;return true;
    }
public:
    explicit Repairman(bool enabled=true):enabled_(enabled){}
    const char*name()const{return "repairman";}
    void observe_actors(const ActorFrame&f){invalidate();s_->actors=f;}
    void observe_combat(const CombatKit&f){invalidate();s_->kit=f;}
    void observe_spatial(const SpatialFrame&f){invalidate();s_->spatial=f;graph_work_=0;}
    int spatial_vertices()const{return graph_work_;}
    void observe_snapshot(const Information&info,MemberFrame={}){invalidate();s_->snapshot=&info;}
    void observe_attack(const A&f){invalidate();s_->attack=f;s_->have_attack=true;}
    void observe_cycle(const C&f){invalidate();s_->cycle=f;s_->have_cycle=true;}
    // Optional action-boundary receipts from the common executor. A forecast
    // never invokes these, and absent work never authorizes a return attack.
    void attack_processed(int attacks,float actual_damage){repairman::processed(s_->actual_work,attacks,actual_damage);}
    void work_departed(){repairman::departed(s_->actual_work);}
    bool reobserve_work(const repairman::GroupObservation&g,int round){return repairman::observe(s_->actual_work,g,round);}
    bool return_attack_authorized(int round)const{return repairman::return_attack_authorized(s_->actual_work,round);}
    const repairman::WorkRecord&work_record()const{return s_->actual_work;}
    void selected(const Information&,const Job&,std::uint64_t epoch,bool){
        if(s_->selected_epoch!=epoch){s_->actual_work={};s_->selected_epoch=epoch;s_->attack_receipt_round=-1;}
    }
    bool safe_segments_enabled()const{return s_->have_attack&&s_->have_cycle&&s_->cycle.supported&&s_->cycle.executor_enabled;}
    safe_segment::ExecutionProposal segment_proposal(const safe_segment::ExecutionBody&body){
        s_->execution_source.context_supported=false;
        const auto&f=s_->attack;const auto&k=s_->kit;const auto&a=s_->actors;
        if(!safe_segments_enabled()||!s_->snapshot||f.clock.round!=body.round||f.clock.own.id!=body.actor_id||
            a.clock.round!=body.round||a.clock.own.id!=body.actor_id||!a.own_observed||a.truncated||
            a.own.id!=body.actor_id||a.own.position!=body.position||a.own.health!=body.health||a.own.maximum_health!=body.maximum_health||
            !safe_segment::current_exact(a.own.provenance,body.round,Source::own_state)||
            f.group_id!=body.group_id||f.target_member_id!=body.target_id||!f.lifecycle_version_supported||
            f.kit_version!=body.kit_version||f.stat_epoch!=body.stat_epoch||!k.exact||k.clock.round!=body.round||k.clock.own.id!=body.actor_id||
            k.version!=body.kit_version||k.stat_epoch!=body.stat_epoch||f.emergency||f.new_visible_rival||f.ability_timing_changed||
            !f.ordinary_basic_only||!repairman::runtime::quoted_now(f.action_quote,body.round)||!repairman::runtime::quoted_now(f.combat_quote,body.round)||
            !std::isfinite(f.basic_damage)||f.basic_damage<=0||!safe_segment::valid_loss(f.incoming_upper_bound)||
            f.safety.observed_round!=body.round||f.safety.actor_id!=body.actor_id)return{};
        if(s_->snapshot->round!=body.round||s_->snapshot->own.position!=body.position||s_->snapshot->own.health!=body.health||
            !repairman::runtime::prepare_attack_into(*s_->snapshot,a,k,f,s_->execution_source,true)||!s_->execution_source.context_supported)return{};
        const auto&observed=s_->execution_source.group;
        if(s_->actual_work.group_id<0)repairman::start_new_work(s_->actual_work,observed,body.round);
        else{
            const auto&w=s_->actual_work;
            const bool repeated=w.group_id==observed.group_id&&w.lifecycle_version==observed.lifecycle_version&&
                w.observed_round==body.round&&w.observed_health==observed.health&&!w.observation_current;
            if(!repeated&&!repairman::observe(s_->actual_work,observed,body.round))return{};
        }
        auto&actor=s_->proposal_actor;actor=s_->execution_source.actor;
        // No SDK generation counter exists. The explicit fresh legal-history
        // epoch is handed to the applicator, which binds it before execution.
        return{&actor,&f.safety,&s_->snapshot->settings,body.group_id,body.kit_version,body.stat_epoch,f.lifecycle_version,true};
    }
    void segment_attack_completed(const safe_segment::ExecutionBody&body,float actual_damage){
        const auto&p=s_->execution_source;
        if(p.context_supported&&s_->attack_receipt_round!=body.round&&p.actor.observed_round==body.round&&p.actor.actor_id==body.actor_id&&p.actor.target_id==body.target_id&&
            p.group.group_id==body.group_id&&p.actor.position==body.position&&p.actor.health==body.health&&p.actor.maximum_health==body.maximum_health&&
            p.kit_version==body.kit_version&&p.stat_epoch==body.stat_epoch&&p.group.lifecycle_version==body.lifecycle_version&&repairman::valid_loss(actual_damage)){
            repairman::processed(s_->actual_work,1,actual_damage);s_->attack_receipt_round=body.round;
        }
    }
    void segment_departed(const safe_segment::ExecutionBody&body){
        const auto&p=s_->execution_source;
        if(p.context_supported&&p.actor.observed_round==body.round&&p.actor.actor_id==body.actor_id&&p.actor.target_id==body.target_id&&
            p.group.group_id==body.group_id&&p.actor.position==body.position)repairman::departed(s_->actual_work);
    }
    void evaluate_into(const Information&info,const Menu&menu,Evaluation&out){
        invalidate();++revision_;paths_=work_=unknown_=graph_work_=0;reason_=repairman::runtime::Unsupported::executor_and_continuation_missing;
        out.menu=menu;out.evaluated_count=0;out.chosen=-1;out.budget_exhausted=false;
        if(!bind(info,menu)){if(reason_==repairman::runtime::Unsupported::budget)out.budget_exhausted=true;else plan4::evaluate_into(info,menu,out);return;}
        ForecastHooks hooks;hooks.full_clear_allowed=full_clear;
        for(int n=0;n<menu.count;++n){
            if(!can_refine(info)){reason_=repairman::runtime::Unsupported::budget;out.evaluated_count=0;out.budget_exhausted=true;return;}
            const auto&job=menu.jobs[n];const bool applies=job.kind==JobKind::farm&&job.group==s_->prepared.group_index;
            if(applies){
                if(!repairman::runtime::predict_into(info,menu,job,s_->prepared,s_->cycle,enabled_,s_->path,out.forecasts[n],hooks)){
                    if(!can_refine(info)||s_->path.failure==repairman::Failure::budget){reason_=repairman::runtime::Unsupported::budget;out.evaluated_count=0;out.budget_exhausted=true;return;}
                    reason_=repairman::runtime::Unsupported::executor_and_continuation_missing;plan4::evaluate_into(info,menu,out);return;}
                work_+=s_->path.work;
            }else plan4::predict_into(info,job,menu,out.forecasts[n],hooks);
            const auto&f=out.forecasts[n];
            if(f.budget_exhausted){reason_=repairman::runtime::Unsupported::budget;out.evaluated_count=0;out.budget_exhausted=true;return;}
            auto&prefix=s_->prefixes[n];prefix.job=job;prefix.count=0;
            for(int e=0;e<f.event_count&&prefix.count<8;++e){if(!can_refine(info)){reason_=repairman::runtime::Unsupported::budget;out.evaluated_count=0;out.budget_exhausted=true;return;}
                const auto&event=f.events[e];if(event.kind!=EventKind::clear&&event.kind!=EventKind::capture&&event.kind!=EventKind::bank&&event.kind!=EventKind::recall_arrival)continue;
                const auto amount=event.kind==EventKind::capture?std::int64_t(event.bag_after)-event.bag_before:std::int64_t(event.secured);
                prefix.events[prefix.count++]={event.end.round,int(event.end.phase),int(event.kind),info.own.id,event.group_id,amount*1000,event.bag_after};}
            unknown_+=f.stock_unknown||f.terminal_unknown;++paths_;++out.evaluated_count;
        }
        out.chosen=choose(info,menu,out.forecasts);used_=true;s_->source=&info;s_->chosen=out.chosen;s_->round=info.round;s_->actor=info.own.id;
        s_->position=info.own.position;s_->health=info.own.health;s_->maximum=info.own.maximum_health;s_->bag=info.own.bag;s_->cash=info.own.gold;
        s_->ours=info.our_score;s_->enemy=info.enemy_score;
    }
    ModelEvidence model_evidence()const{return{true,used_,enabled_,int(reason_),used_?1:0,unknown_+(reason_!=repairman::runtime::Unsupported::none),paths_,work_,revision_};}
    void prediction_receipts(const Information&info,const Job&job,PredictionEventSink sink,void*context)const{
        const auto&s=*s_;if(!sink||!used_||s.source!=&info||s.chosen<0||s.round!=info.round||s.actor!=info.own.id||s.position!=info.own.position
            ||s.health!=info.own.health||s.maximum!=info.own.maximum_health||s.bag!=info.own.bag||s.cash!=info.own.gold||s.ours!=info.our_score||s.enemy!=info.enemy_score)return;
        const auto&p=s.prefixes[s.chosen];if(p.job.kind!=job.kind||p.job.group!=job.group||p.job.second_group!=job.second_group||p.job.altar!=job.altar)return;
        for(int n=0;n<p.count;++n)if(!can_refine(info)||!sink(p.events[n],context))return;
    }
    bool used_repairman()const{return used_;}
    repairman::runtime::Unsupported unsupported()const{return reason_;}
    const repairman::runtime::Path&path()const{return s_->path;}
    static constexpr bool live_cycle_producer_available=false;
};
} // namespace plan4::runtime
