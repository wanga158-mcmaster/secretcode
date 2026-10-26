#pragma once
#include "metronome.h"
#include "graph.h"
#include <tuple>

namespace plan4::runtime {
namespace metronome_binding {
inline constexpr const char* version="plan4-metronome-runtime/1";
enum class Origin { missing,retained_native,synthetic_test };
enum class Use { production_only,conditional_test };
struct Profile {
    metronome::MatchedLaw law{};metronome::Limitation limitation{};metronome::CostEnvelope cost{};
    std::uint64_t identity=0,geometry_identity=0;
    Origin origin=Origin::missing;
    bool production_frozen=false;
};
struct Resources {int round=-1;std::uint64_t fuel=0;bool valid=false;};
struct ResourceClock {Resources (*read)(const void*)=nullptr;const void* context=nullptr;};
enum class Reason { none,profile,source,geometry,resources,bounds,budget,cost,no_overlap,no_work };
// A production manifest must resolve these identities to retained native
// reference traces and their fixed operation protocol. Synthetic rows cannot
// become production evidence by merely setting production_frozen.
inline bool valid_profile(const Profile& p,Use use){
    const auto& l=p.law;const auto& e=p.limitation;const auto& c=p.cost;
    return p.identity&&p.geometry_identity&&p.production_frozen&&
        (p.origin==Origin::retained_native||(use==Use::conditional_test&&p.origin==Origin::synthetic_test))&&
        l.identity&&l.receipt_source&&l.repeatable&&l.matched_complete_forecasts&&l.clock==metronome::ClockLaw::engine_calendar_and_phase_order&&
        e.source&&e.law_identity==l.identity&&e.legal_large_map_state&&e.complete_measurements&&e.competing_refinements_exceed_reserve&&
        c.source&&c.law_identity==l.identity&&c.frozen&&c.measured_operations&&c.maximum_measured_cost;
}
} // namespace metronome_binding

class Metronome : public M0 {
    using Reason=metronome_binding::Reason;
    struct Pair {Point a{},b{};};
    struct Pending {std::array<Pair,32> pairs{};int count=0,cursor=0;bool overflow=false;};
    struct Storage {
        Information copy;metronome::State state;
        std::array<metronome::Rollouts,max_jobs> rows{};
        std::array<Pending,max_jobs> pending{};
        std::array<Forecast,max_jobs> published;
        Forecast lower,upper,nominal;
        metronome::Graph graph;
        Storage() noexcept {}
    };
    // An unsupported ordinary build does not allocate/initialize graph fields,
    // alternate Information or optional forecast capacity.
    std::unique_ptr<Storage> s_;
    metronome_binding::Profile profile_{};metronome_binding::ResourceClock resources_{};
    metronome_binding::Use use_;
    SpatialFrame spatial_{};ObservationClock snapshot_{};Actor own_{};
    const Information* source_=nullptr;const Menu* menu_=nullptr;
    unsigned score_=0,enemy_score_=0;
    bool enabled_=true,seen_=false,supported_=false,failed_budget_=false,clock_failed_=false;
    bool missing_upper_=false,augmented_=false;
    enum class TravelMode { lower,nominal,upper } mode_=TravelMode::nominal;
    Pending* recording_=nullptr;
    int vertices_=0,paths_=0,work_=0;Reason reason_=Reason::profile;
    std::uint64_t revision_=0,epoch_=0,last_fuel_=0;
    bool have_fuel_=false;
    static bool work(const Information& i){return !i.work_budget.can_refine||i.work_budget.can_refine(i.work_budget.context);}
    bool budget(){if(!source_||!work(*source_)){failed_budget_=true;reason_=Reason::budget;return false;}return true;}
    static bool same_actor(const Actor& a,const Actor& b){
        return std::tie(a.id,a.position.x,a.position.y,a.health,a.maximum_health,a.bag,a.gold,a.channel,a.channel_ticks,a.alive,
            a.on_spawn,a.recall_safe,a.escape_rounds,a.escape_incoming)==
            std::tie(b.id,b.position.x,b.position.y,b.health,b.maximum_health,b.bag,b.gold,b.channel,b.channel_ticks,b.alive,
            b.on_spawn,b.recall_safe,b.escape_rounds,b.escape_incoming);
    }
    static bool same_job(const Job& a,const Job& b){return std::tie(a.kind,a.group,a.second_group,a.altar,a.menu_rank,a.nominal_travel,a.nominal_completion)==
        std::tie(b.kind,b.group,b.second_group,b.altar,b.menu_rank,b.nominal_travel,b.nominal_completion);}
    bool clock(std::uint64_t& fuel){
        if(!resources_.read){reason_=Reason::resources;return false;}
        const auto r=resources_.read(resources_.context);
        if(!r.valid||r.round!=source_->round||(have_fuel_&&r.fuel>last_fuel_)){
            clock_failed_=true;failed_budget_=true;reason_=Reason::resources;return false;}
        have_fuel_=true;last_fuel_=fuel=r.fuel;return true;
    }
    int known(Point a,Point b,bool augmented)const{
        const int existing=source_->travel.distance?source_->travel.distance(a,b,source_->travel.context):-1;
        if(existing>=0)return existing;
        return augmented?s_->graph.cached(a,b):-1;
    }
    void record(Point a,Point b){
        if(!recording_)return;
        for(int n=0;n<recording_->count;++n)if((recording_->pairs[n].a==a&&recording_->pairs[n].b==b)||
            (recording_->pairs[n].a==b&&recording_->pairs[n].b==a))return;
        if(recording_->count==32){recording_->overflow=true;return;}
        recording_->pairs[recording_->count++]={a,b};
    }
    static int distance(Point a,Point b,const void* context){return const_cast<Metronome*>(static_cast<const Metronome*>(context))->distance(a,b);}
    int distance(Point a,Point b){
        if(mode_==TravelMode::lower)return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));
        const int exact=known(a,b,augmented_);if(exact>=0)return exact;
        if(mode_==TravelMode::nominal){record(a,b);return -1;}
        // Concatenating two completed public-terrain paths is a legal (possibly
        // loose) upper route. It makes no future dynamic-occupancy assertion.
        const int first=known(a,source_->own.position,augmented_),second=known(source_->own.position,b,augmented_);
        if(first>=0&&second>=0&&std::int64_t(first)+second<=std::numeric_limits<int>::max())return first+second;
        missing_upper_=true;return horizon(*source_)+1;
    }
    void clone_source(){
        const auto& i=*source_;auto& o=s_->copy;
        o.round=i.round;o.own=i.own;o.own_spawn=i.own_spawn;o.our_score=i.our_score;o.enemy_score=i.enemy_score;o.settings=i.settings;
        o.group_count=i.group_count;std::copy_n(i.groups.begin(),i.group_count,o.groups.begin());
        o.altar_count=i.altar_count;std::copy_n(i.altars.begin(),i.altar_count,o.altars.begin());
        o.travel={distance,this};o.work_budget=i.work_budget;o.in_progress_group_id=i.in_progress_group_id;
        o.retained_group_id=i.retained_group_id;o.retained_second_group_id=i.retained_second_group_id;o.retained_altar_id=i.retained_altar_id;
        o.observation_truncated=i.observation_truncated;o.public_layout_truncated=i.public_layout_truncated;
    }
    bool rollout(int candidate,bool augmented){
        if(!budget())return false;
        augmented_=augmented;clone_source();recording_=&s_->pending[candidate];*recording_={};
        mode_=TravelMode::nominal;plan4::predict_into(s_->copy,menu_->jobs[candidate],*menu_,s_->nominal);
        recording_=nullptr;
        if(s_->nominal.budget_exhausted||!budget()){failed_budget_=true;return false;}
        mode_=TravelMode::lower;plan4::predict_into(s_->copy,menu_->jobs[candidate],*menu_,s_->lower);
        if(s_->lower.budget_exhausted||!budget()){failed_budget_=true;return false;}
        missing_upper_=false;mode_=TravelMode::upper;plan4::predict_into(s_->copy,menu_->jobs[candidate],*menu_,s_->upper);
        if(s_->upper.budget_exhausted||!budget()){failed_budget_=true;return false;}
        auto& row=s_->rows[candidate];row={};row.chebyshev=metronome::tuple(s_->lower);row.nominal=metronome::tuple(s_->nominal);
        row.pessimistic=metronome::tuple(s_->upper);row.forecast_round=source_->round;row.route_evidence_round=source_->round;
        row.physical_epoch=epoch_;row.law_identity=profile_.law.identity;row.all_complete=true;
        row.completed_legal_upper_route=!missing_upper_;row.pessimistic_arrival=source_->round+horizon(*source_)+1;
        row.useful_refinement_remaining=false;
        const auto& pending=s_->pending[candidate];
        if(pending.overflow){reason_=Reason::bounds;return false;}
        for(int n=0;n<pending.count;++n){const auto p=pending.pairs[n];
            row.useful_refinement_remaining|=s_->graph.cached(p.a,p.b)>=0||s_->graph.pending(p.a,p.b);}
        return true;
    }
    static bool graph_budget(const void* c){return const_cast<Metronome*>(static_cast<const Metronome*>(c))->budget();}
    static metronome::Refinement refine(int candidate,int maximum,void* context){return static_cast<Metronome*>(context)->refine(candidate,maximum);}
    metronome::Refinement refine(int candidate,int maximum){
        metronome::Refinement result;result.candidate=candidate;auto& state=s_->state;
        std::uint64_t before=0,after=0;
        if(!budget()||!clock(before)){result.fuel_used=state.fuel_remaining+1;return result;}
        state.fuel_remaining=before;
        if(!metronome::fuel_admits(state)){result.useful_operation=false;return result;}
        const int quota=std::min(maximum,std::max(0,spatial_.graph_quota-vertices_));
        auto& pending=s_->pending[candidate];const int start=s_->graph.vertices();
        if(quota>0&&pending.count){
            for(int n=0;n<pending.count;++n){const int index=(pending.cursor+n)%pending.count;const auto pair=pending.pairs[index];
                if(!s_->graph.pending(pair.a,pair.b))continue;
                pending.cursor=(index+1)%pending.count;
                s_->graph.advance(pair.a,pair.b,quota,{graph_budget,this});break;
            }
        }
        result.vertices_used=s_->graph.vertices()-start;vertices_+=result.vertices_used;
        const bool complete=budget()&&rollout(candidate,true);
        if(!clock(after)||after>before){result.fuel_used=state.fuel_remaining+1;return result;}
        result.fuel_used=before-after;result.completed=complete;
        if(complete){result.rollouts=s_->rows[candidate];
            result.rollouts.useful_refinement_remaining&=vertices_<spatial_.graph_quota;
            const bool valid=metronome::valid_rollouts(result.rollouts,state,*source_);
            if(!valid)reason_=Reason::bounds;
            if(result.fuel_used<=profile_.cost.maximum_measured_cost&&valid)
                s_->published[candidate]=s_->nominal;
        }
        result.useful_operation=complete&&result.rollouts.useful_refinement_remaining;
        return result;
    }
    void invalidate(){seen_=supported_=failed_budget_=clock_failed_=false;paths_=work_=0;reason_=Reason::source;if(s_)metronome::invalidate(s_->state);}
    void fallback(const Information& i,const Menu& m,Evaluation& out){
        out.menu=m;out.chosen=-1;out.evaluated_count=0;out.budget_exhausted=false;
        for(int n=0;n<m.count;++n){if(!work(i)){out.budget_exhausted=failed_budget_=true;break;}
            plan4::predict_into(i,m.jobs[n],m,out.forecasts[n]);if(out.forecasts[n].budget_exhausted){out.budget_exhausted=failed_budget_=true;break;}++out.evaluated_count;}
        Menu completed=m;completed.count=out.evaluated_count;out.chosen=choose(i,completed,out.forecasts);
    }
public:
    explicit Metronome(bool enabled=true,metronome_binding::Use use=metronome_binding::Use::production_only):use_(use),enabled_(enabled){}
    const char* name()const{return "metronome";}
    void configure(const metronome_binding::Profile& p){profile_=p;invalidate();++revision_;}
    void observe_resources(metronome_binding::ResourceClock resources){resources_=resources;invalidate();++revision_;}
    void observe_spatial(const SpatialFrame& frame){spatial_=frame;vertices_=0;invalidate();++revision_;}
    void observe_snapshot(const Information& i,MemberFrame={}){source_=&i;snapshot_={i.round,{i.own.id}};own_=i.own;score_=i.our_score;enemy_score_=i.enemy_score;invalidate();}
    void evaluate_into(const Information& i,const Menu& m,Evaluation& out){
        seen_=true;supported_=failed_budget_=clock_failed_=false;paths_=work_=0;reason_=Reason::profile;++revision_;++epoch_;have_fuel_=false;
        out.menu=m;out.chosen=-1;out.evaluated_count=0;out.budget_exhausted=false;
        if(m.count<0||m.count>max_jobs){out.budget_exhausted=failed_budget_=true;return;}
        if(!metronome_binding::valid_profile(profile_,use_)){fallback(i,m,out);return;}
        reason_=Reason::source;
        if(source_!=&i||snapshot_.round!=i.round||snapshot_.own.id!=i.own.id||!same_actor(own_,i.own)||score_!=i.our_score||enemy_score_!=i.enemy_score||
            i.public_layout_truncated||i.group_count<0||i.group_count>max_groups||i.altar_count<0||i.altar_count>max_altars||!i.own.alive||!std::isfinite(i.own.health)||i.own.health<=0||
            spatial_.clock.round!=i.round||spatial_.clock.own.id!=i.own.id||spatial_.information!=&i||!spatial_.menu||spatial_.menu->count!=m.count||
            spatial_.travel.distance!=i.travel.distance||spatial_.travel.context!=i.travel.context){fallback(i,m,out);return;}
        for(int n=0;n<m.count;++n)if(!same_job(spatial_.menu->jobs[n],m.jobs[n])){fallback(i,m,out);return;}
        reason_=Reason::geometry;
        if(!budget()){out.budget_exhausted=true;return;}
        std::uint64_t fuel=0;
        if(!clock(fuel)){if(failed_budget_)out.budget_exhausted=true;else fallback(i,m,out);return;}
        const auto reserve=std::uint64_t(spatial_.optional_fuel_floor),maximum=profile_.cost.maximum_measured_cost;
        if(fuel<reserve||maximum>fuel-reserve||(maximum/2+maximum%2)>fuel-reserve-maximum){
            reason_=Reason::no_work;fallback(i,m,out);return;
        }
        if(!s_)s_.reset(new Storage);
        if(spatial_.reference_vertices_used<0||spatial_.reference_vertices_used>256||spatial_.graph_quota<0||
            spatial_.graph_quota>256-spatial_.reference_vertices_used||!spatial_.optional_fuel_floor||
            !s_->graph.bind(spatial_.topology,profile_.geometry_identity)){fallback(i,m,out);return;}
        // The reference consumes actual vertices; graph_quota separately keeps
        // the common executor's movement reserve. Reserved vertices are not visits.
        s_->graph.begin_turn();menu_=&m;
        if(!budget()){out.budget_exhausted=true;return;}
        for(int n=0;n<m.count;++n){if(!rollout(n,false)){if(failed_budget_){out.budget_exhausted=true;return;}fallback(i,m,out);return;}
            s_->published[n]=s_->nominal;}
        if(!clock(fuel)){out.budget_exhausted=true;return;}
        if(!metronome::begin(s_->state,i,m,s_->rows.data(),m.count,epoch_,profile_.law,profile_.limitation,profile_.cost,
            spatial_.reference_vertices_used,fuel,spatial_.optional_fuel_floor,enabled_,spatial_.graph_quota)){reason_=Reason::bounds;fallback(i,m,out);return;}
        if(!spatial_.graph_quota)for(int n=0;n<m.count;++n)s_->state.candidates[n].useful=false;
        metronome::refine(s_->state,i,epoch_,refine,this);
        work_=s_->state.refinements;
        if(failed_budget_){reason_=Reason::budget;out.budget_exhausted=true;return;}
        if(s_->state.contract_violation||clock_failed_){reason_=clock_failed_?Reason::resources:reason_==Reason::bounds?Reason::bounds:Reason::cost;fallback(i,m,out);return;}
        if(!clock(fuel)||fuel<spatial_.optional_fuel_floor){reason_=Reason::budget;out.budget_exhausted=failed_budget_=true;return;}
        for(int n=0;n<m.count;++n)out.forecasts[n]=s_->published[n];
        out.evaluated_count=m.count;out.chosen=choose(i,m,out.forecasts);paths_=m.count;supported_=true;
        reason_=s_->state.refinements?Reason::none:metronome::next_candidate(s_->state,i)<0?Reason::no_overlap:Reason::no_work;
    }
    void predict_into(const Information& i,const Job& j,const Menu& m,Forecast& out){
        // Priority allocation requires the frozen complete batch. A single-path
        // caller receives ordinary M0 rather than a private candidate subset.
        invalidate();seen_=true;reason_=Reason::source;plan4::predict_into(i,j,m,out);
    }
    void selected(const Information&,const Job&,std::uint64_t,bool){if(s_)metronome::invalidate(s_->state);}
    void selection_canceled(){if(s_)metronome::invalidate(s_->state);}
    int spatial_vertices()const{return vertices_;}
    ModelEvidence model_evidence()const{return {true,seen_&&supported_&&!failed_budget_,enabled_&&seen_&&supported_&&!failed_budget_,
        int(failed_budget_?Reason::budget:reason_),int(std::min(profile_.cost.measured_operations,unsigned(std::numeric_limits<int>::max()))),supported_?0:1,paths_,work_,revision_};}
    int refinements()const{return seen_?work_:0;}
};
} // namespace plan4::runtime
