#pragma once
#include "bulkheads.h"
#include "../../common/model-runtime.h"
#include <tuple>

namespace plan4::runtime {
namespace bulkheads_binding {
inline constexpr const char* version="plan4-bulkheads-runtime/1";
// These are explicit fixture/production inputs, not facts inferred from an
// ordinary opponent. A manifest must bind the immutable table, policy and
// public geometry before any probe can use this adapter.
struct Connector { int id=-1; Point position{}; };
struct Dependencies {
    std::array<Connector,bulkheads::max_causes> connectors{};
    int count=0;
    std::uint64_t geometry_identity=0;
    bool complete=false;
};
struct SafeWait {
    int decision_round=-1,actor_id=-1,maximum_rounds=0;
    Point position{};
    std::uint64_t geometry_identity=0;
    bool ordinary_legal=false,zero_exposure=false,no_healing_effect=false;
    bool no_channel_or_capture_effect=false;
};
struct Profile {
    bulkheads::Fit fit{};
    bulkheads::PolicyReceipt policy{};
    std::uint64_t table_identity=0,geometry_identity=0;
    bool production_frozen=false;
    // Bounded SDK-free read-only lookup over an immutable public dependency
    // map; no graph search, SDK access or mutation during a forecast. The
    // profile owner certifies that geometry_identity names frame.topology.
    // Unknown routes return false. Complete count0 positively proves that
    // this actual physical leg has no represented connector dependency.
    bool (*dependencies)(const PhaseEntry&,const Information&,const SpatialFrame&,
                         Dependencies&,const void*)=nullptr;
    // A positive quote requires legal stationary waits at entry.from with no
    // added damage, healing, channel completion or incidental capture. Future
    // safety is a declared conditional premise, never future exact vision.
    bool (*safe_wait)(const PhaseEntry&,int,const Information&,SafeWait&,const void*)=nullptr;
    // Certifies the fixture's frozen scheduling law for this inclusive owner-
    // round interval against current legal actor evidence. Published priorities
    // alone cannot certify an arbitrary future interval. Unknown is false.
    bool (*order_supported)(int,int,const Information&,const ActorFrame&,const void*)=nullptr;
    const void* context=nullptr;
};
struct CauseSample {
    int episode_id=-1,connector_id=-1,blocker_id=-1;
    Point position{};
    Provenance provenance{};
    bool blocked=false,personally_visible=false,action_conditioned=false;
    bool onset=false,preceding_clear=false;
    Provenance preceding{};
    bulkheads::PolicyReceipt policy{};
};
struct CauseFrame {
    ObservationClock clock{};
    std::array<CauseSample,bulkheads::max_causes> samples{};
    int count=0;
    bool truncated=false;
};
enum class Reason { none,profile,source,causes,route,generation,order,waiting,budget,no_travel };

inline bool valid_profile(const Profile& p) {
    const auto& f=p.fit;
    if(!p.production_frozen||!p.table_identity||!p.geometry_identity||!p.dependencies||!p.safe_wait||!p.order_supported||
        !p.policy.fingerprint||!p.policy.frozen||!p.policy.release_independent_of_own_actions||
        !f.frozen||!f.supported||f.started!=bulkheads::fit_episodes||f.policy!=p.policy.fingerprint)return false;
    for(int n=0;n<bulkheads::fit_episodes;++n) {
        const auto& e=f.episodes[n];
        if(e.id<0||e.start<0||e.last<e.start||e.duration<=0||e.duration!=e.last-e.start||
            !e.complete||e.censor!=bulkheads::Censor::none||e.policy!=f.policy||f.durations[n]!=e.duration||
            (n&&e.start<f.episodes[n-1].start))return false;
        for(int k=0;k<n;++k)if(f.episodes[k].id==e.id)return false;
    }
    return true;
}
} // namespace bulkheads_binding

class Bulkheads : public M0 {
    using Reason=bulkheads_binding::Reason;
    bulkheads::State state_{};
    bulkheads_binding::Profile profile_{};
    bulkheads_binding::CauseFrame causes_{};
    ActorFrame actors_{};
    SpatialFrame spatial_{};
    Actor snapshot_actor_{};
    ObservationClock snapshot_{};
    bool enabled_=true,profile_valid_=false,causes_valid_=false,batch_seen_=false;
    bool batch_budget_=false;
    int supported_paths_=0,changed_paths_=0,unknown_paths_=0,work_=0;
    Reason reason_=Reason::profile;
    std::uint64_t revision_=0;
    struct Path {
        bulkheads::Path process{};
        Reason failure=Reason::none;
        int calls=0,dependencies=0,changed=0,order_calls=0;
    };
    static bool budget(const Information& i) {
        return !i.work_budget.can_refine||i.work_budget.can_refine(i.work_budget.context);
    }
    static bool same_actor(const Actor& a,const Actor& b) {
        return std::tie(a.id,a.position.x,a.position.y,a.health,a.maximum_health,a.bag,a.gold,a.channel,a.channel_ticks,
            a.alive,a.on_spawn,a.recall_safe,a.escape_rounds,a.escape_incoming)==
            std::tie(b.id,b.position.x,b.position.y,b.health,b.maximum_health,b.bag,b.gold,b.channel,b.channel_ticks,
            b.alive,b.on_spawn,b.recall_safe,b.escape_rounds,b.escape_incoming);
    }
    void reset_evidence() {
        batch_seen_=batch_budget_=false;supported_paths_=changed_paths_=unknown_paths_=work_=0;
        reason_=profile_valid_?Reason::no_travel:Reason::profile;
    }
    void invalidate_causes() {
        causes_valid_=false;
        for(auto& cause:state_.causes)if(cause.episode_id>=0&&!cause.resolved) {
            cause.unknown=true;cause.onset_known=false;
        }
    }
    bool source_current(const Information& i) const {
        const auto& body=actors_.own;
        return snapshot_.round==i.round&&snapshot_.own.id==i.own.id&&same_actor(snapshot_actor_,i.own)&&
            i.group_count>=0&&i.group_count<=max_groups&&i.altar_count>=0&&i.altar_count<=max_altars&&
            !i.public_layout_truncated&&i.own.alive&&i.own.health>0&&i.own.channel==Channel::none&&
            actors_.clock.round==i.round&&actors_.clock.own.id==i.own.id&&actors_.own_observed&&!actors_.truncated&&
            actors_.count>=0&&actors_.count<=9&&
            body.id==i.own.id&&body.ally&&body.alive&&body.position==i.own.position&&
            body.health==i.own.health&&body.maximum_health==i.own.maximum_health&&body.bag==i.own.bag&&
            body.provenance.observed_round==i.round&&body.provenance.source==Source::own_state&&body.provenance.status==Status::exact_now&&
            spatial_.clock.round==i.round&&spatial_.clock.own.id==i.own.id&&spatial_.topology.complete&&
            spatial_.topology.width>0&&spatial_.topology.height>0&&spatial_.topology.walkable&&
            spatial_.travel.distance==i.travel.distance&&spatial_.travel.context==i.travel.context;
    }
    const bulkheads_binding::CauseSample* binding(int connector) const {
        for(int n=0;n<causes_.count;++n)if(causes_.samples[n].connector_id==connector)return &causes_.samples[n];
        return nullptr;
    }
    bool current_body_matches(const bulkheads_binding::CauseSample& s,int round)const {
        const VisibleActor* body=nullptr;
        for(int n=0;n<actors_.count;++n)if(actors_.samples[n].id==s.blocker_id) {
            if(body)return false;
            body=&actors_.samples[n];
        }
        return body&&body->alive&&body->position==s.position&&bulkheads::exact(body->provenance,round);
    }
    bool currently_empty(Point p,int round)const {
        if(actors_.own.position==p)return false;
        for(int n=0;n<actors_.count;++n)if(actors_.samples[n].alive&&actors_.samples[n].position==p&&
            bulkheads::exact(actors_.samples[n].provenance,round))return false;
        return true;
    }
    static EntryWait entry(const PhaseEntry& e,const Information& i,const void* model,void* scratch) {
        const auto& self=*static_cast<const Bulkheads*>(model);auto& path=*static_cast<Path*>(scratch);++path.calls;
        auto fail=[&](Reason r){path.failure=r;return EntryWait{};};
        if(!budget(i))return fail(Reason::budget);
        if(e.kind!=EventKind::travel||e.nominal<0||e.preserves_current_channel_or_capture)return fail(Reason::waiting);
        // No physical departure has no connector crossing. In particular an
        // already occupied capture tile must not manufacture a waiting phase.
        if(e.from==e.to)return {0,true,true};
        bulkheads_binding::Dependencies dependencies;
        const bool known=self.profile_.dependencies(e,i,self.spatial_,dependencies,self.profile_.context);
        if(!budget(i))return fail(Reason::budget);
        if(!known||!dependencies.complete||dependencies.geometry_identity!=self.profile_.geometry_identity||
            dependencies.count<0||dependencies.count>bulkheads::max_causes)return fail(Reason::route);
        std::array<int,bulkheads::max_causes> ids{};int count=0;
        for(int n=0;n<dependencies.count;++n) {
            if(!budget(i))return fail(Reason::budget);
            const auto& connector=dependencies.connectors[n];
            if(connector.id<0||connector.position.x<0||connector.position.y<0||
                connector.position.x>=self.spatial_.topology.width||connector.position.y>=self.spatial_.topology.height)
                return fail(Reason::route);
            for(int p=0;p<n;++p)if(dependencies.connectors[p].id==connector.id||dependencies.connectors[p].position==connector.position)
                return fail(Reason::route);
            const bool legal=self.spatial_.topology.walkable(connector.position,self.spatial_.topology.context);
            if(!budget(i))return fail(Reason::budget);
            if(!legal)return fail(Reason::route);
            const auto* current=self.binding(connector.id);
            if(!current||current->position!=connector.position)return fail(Reason::generation);
            ++path.dependencies;
            if(!current->blocked) {
                if(!self.currently_empty(connector.position,i.round))return fail(Reason::generation);
                continue; // Current exact clear observation.
            }
            if(!self.current_body_matches(*current,i.round))return fail(Reason::generation);
            const int c=bulkheads::cause_index(self.state_,current->episode_id);
            if(c<0)return fail(Reason::generation);
            const auto& cause=self.state_.causes[c];
            if(cause.connector_id!=connector.id||cause.blocker_id!=current->blocker_id||cause.last!=i.round||
                cause.unknown||!cause.onset_known||cause.resolved||cause.policy!=self.profile_.policy.fingerprint)
                return fail(Reason::generation);
            ids[count++]=cause.episode_id;
        }
        const auto delay=bulkheads::phase_wait(path.process,ids.data(),count,e.now);
        if(!delay.supported)return fail(Reason::causes);
        const auto end=std::int64_t(e.now)+delay.wait+e.nominal;
        if(end>std::numeric_limits<int>::max())return fail(Reason::waiting);
        ++path.order_calls;
        const bool ordered=self.profile_.order_supported(e.now,int(end),i,self.actors_,self.profile_.context);
        if(!budget(i))return fail(Reason::budget);
        if(!ordered)return fail(Reason::order);
        if(delay.wait==0)return {0,true,true};
        if(e.health<i.own.maximum_health&&(e.from==i.own_spawn||(e.from==i.own.position&&i.own.on_spawn)))
            return fail(Reason::waiting);
        if(e.now==i.round&&e.from==i.own.position&&i.in_progress_group_id>=0)
            for(int g=0;g<i.group_count;++g)if(i.groups[g].id==i.in_progress_group_id&&
                i.groups[g].lifecycle==Lifecycle::cleared&&i.groups[g].capture_started_round>=0)
                return fail(Reason::waiting);
        bulkheads_binding::SafeWait proof;
        const bool safe=self.profile_.safe_wait(e,delay.wait,i,proof,self.profile_.context);
        if(!budget(i))return fail(Reason::budget);
        if(!safe||proof.decision_round!=i.round||proof.actor_id!=i.own.id||proof.position!=e.from||
            proof.geometry_identity!=self.profile_.geometry_identity||proof.maximum_rounds<delay.wait||
            !proof.ordinary_legal||!proof.zero_exposure||!proof.no_healing_effect||!proof.no_channel_or_capture_effect)
            return fail(Reason::waiting);
        ++path.changed;
        return {delay.wait,true,true};
    }
    void predict_current(const Information& i,const Job& j,const Menu& m,Forecast& out) {
        if(!profile_valid_||!source_current(i)||!causes_valid_||causes_.clock.round!=i.round||causes_.clock.own.id!=i.own.id) {
            reason_=!profile_valid_?Reason::profile:!source_current(i)?Reason::source:Reason::causes;
            ++unknown_paths_;plan4::predict_into(i,j,m,out);batch_budget_|=out.budget_exhausted;return;
        }
        Path path;bulkheads::prepare_path(state_,i.round,enabled_,path.process);
        ForecastHooks hooks;hooks.model=this;hooks.path=&path;hooks.entry_wait=entry;
        plan4::predict_into(i,j,m,out,hooks);work_+=path.calls+path.dependencies+path.order_calls;
        if(out.abort_reason==AbortReason::unsupported_wait||path.failure!=Reason::none) {
            reason_=path.failure; ++unknown_paths_;
            // Discard the ENTIRE attempted refinement, including earlier legal
            // receipts. Never splice an M0 tail onto a differently timed prefix.
            plan4::predict_into(i,j,m,out);
        } else if(!out.budget_exhausted&&path.calls) {
            ++supported_paths_;changed_paths_+=path.changed>0;reason_=Reason::none;
        }
        batch_budget_|=out.budget_exhausted;
    }
    struct CandidateOnly {
        Bulkheads& self;
        void predict_into(const Information& i,const Job& j,const Menu& m,Forecast& out){self.predict_current(i,j,m,out);}
    };
public:
    explicit Bulkheads(bool enabled=true):enabled_(enabled){}
    const char* name()const{return "bulkheads";}
    bool configure(const bulkheads_binding::Profile& profile) {
        profile_=profile;profile_valid_=bulkheads_binding::valid_profile(profile);state_={};
        if(profile_valid_)state_.fit=profile.fit;
        causes_={};causes_valid_=false;snapshot_={};actors_={};spatial_={};++revision_;reset_evidence();return profile_valid_;
    }
    void observe_actors(const ActorFrame& frame){actors_=frame;reset_evidence();}
    void observe_spatial(const SpatialFrame& frame){spatial_=frame;reset_evidence();}
    void observe_snapshot(const Information& i,MemberFrame={}) {
        snapshot_={i.round,{i.own.id}};snapshot_actor_=i.own;reset_evidence();
    }
    bool observe_causes(const bulkheads_binding::CauseFrame& frame) {
        reset_evidence();++revision_;
        if(frame.clock.round<0||frame.clock.own.id<0||frame.count<0||frame.count>bulkheads::max_causes||frame.truncated||
            (causes_.clock.round>=0&&(frame.clock.own.id!=causes_.clock.own.id||frame.clock.round<=causes_.clock.round))) {
            invalidate_causes();return false;
        }
        if(actors_.clock.round!=frame.clock.round||actors_.clock.own.id!=frame.clock.own.id||actors_.truncated||
            !actors_.own_observed||actors_.count<0||actors_.count>9){invalidate_causes();return false;}
        for(int n=0;n<frame.count;++n) {
            const auto& s=frame.samples[n];
            if(s.connector_id<0||!s.personally_visible||!bulkheads::exact(s.provenance,frame.clock.round)||
                s.action_conditioned){invalidate_causes();return false;}
            for(int p=0;p<n;++p)if(frame.samples[p].connector_id==s.connector_id||frame.samples[p].position==s.position||
                (s.blocked&&frame.samples[p].blocked&&frame.samples[p].episode_id==s.episode_id)) {
                invalidate_causes();return false;
            }
            if(s.blocked) {
                if(s.episode_id<0||s.blocker_id<0){invalidate_causes();return false;}
                if(!current_body_matches(s,frame.clock.round)){invalidate_causes();return false;}
            } else {
                if(!currently_empty(s.position,frame.clock.round)){invalidate_causes();return false;}
            }
        }
        causes_=frame;
        for(int n=0;n<frame.count;++n) {
            const auto& s=frame.samples[n];
            if(s.blocked&&s.onset) {
                bulkheads::Onset onset;onset.episode_id=s.episode_id;onset.connector_id=s.connector_id;
                onset.blocker_id=s.blocker_id;onset.round=frame.clock.round;onset.current=s.provenance;
                onset.preceding=s.preceding;onset.preceding_clear=s.preceding_clear;onset.policy=s.policy;
                bulkheads::observe_onset(state_,onset);
            }
            if(s.episode_id>=0)bulkheads::observe(state_,{s.episode_id,frame.clock.round,s.personally_visible,s.blocked,
                s.action_conditioned,s.provenance,s.connector_id,s.blocker_id});
        }
        bulkheads::finish_frame(state_,frame.clock.round);causes_valid_=true;return true;
    }
    void predict_into(const Information& i,const Job& j,const Menu& m,Forecast& out) {
        reset_evidence();batch_seen_=true;predict_current(i,j,m,out);
    }
    void evaluate_into(const Information& i,const Menu& m,Evaluation& out) {
        reset_evidence();batch_seen_=true;CandidateOnly model{*this};evaluate_model(i,m,model,out);batch_budget_|=out.budget_exhausted;
    }
    ModelEvidence model_evidence()const {
        ModelEvidence out;out.reported=true;out.reason=int(batch_budget_?Reason::budget:reason_);
        out.supported=batch_seen_&&!batch_budget_&&supported_paths_>0;
        out.coupling_enabled=out.supported&&enabled_;out.samples=profile_valid_?bulkheads::fit_episodes:0;
        out.unknown=unknown_paths_;out.paths=supported_paths_;out.work=work_;out.revision=revision_;return out;
    }
    const bulkheads::State& state()const{return state_;}
};
} // namespace plan4::runtime
