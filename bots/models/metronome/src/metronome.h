#pragma once
#include "../../common/pure.h"

namespace plan4::metronome {
inline constexpr const char* version = "plan4-metronome/1";
inline constexpr int shared_vertex_quota=256,vertices_per_refinement=32,refinement_cap=8;
enum class ClockLaw { unsupported, engine_calendar_and_phase_order };
struct MatchedLaw {
    std::uint64_t identity=0,receipt_source=0;
    ClockLaw clock=ClockLaw::unsupported;
    bool repeatable=false,matched_complete_forecasts=false;
};
struct Limitation {
    std::uint64_t source=0,law_identity=0;
    bool legal_large_map_state=false,complete_measurements=false,competing_refinements_exceed_reserve=false;
};
struct CostEnvelope {
    std::uint64_t source=0,law_identity=0,maximum_measured_cost=0;
    unsigned measured_operations=0;
    bool frozen=false;
};
struct Tuple { int z=0;std::int64_t margin=0;int first_conversion=0; };
inline Tuple tuple(const Forecast& f){return {f.terminal_z,std::int64_t(f.our_secured)-f.enemy_secured,f.first_conversion_round};}
inline int compare(const Information& info,Tuple a,const Job& aj,Tuple b,const Job& bj) {
    if(a.z!=b.z)return a.z>b.z?1:-1;
    if(a.margin!=b.margin)return a.margin>b.margin?1:-1;
    if(a.first_conversion!=b.first_conversion)return a.first_conversion<b.first_conversion?1:-1;
    if(aj.nominal_travel!=bj.nominal_travel)return aj.nominal_travel<bj.nominal_travel?1:-1;
    if(job_key_less(info,aj,bj))return 1;
    if(job_key_less(info,bj,aj))return -1;
    return 0;
}
struct Rollouts {
    Tuple chebyshev{},nominal{},pessimistic{};
    int forecast_round=-1,route_evidence_round=-1,pessimistic_arrival=-1;
    std::uint64_t physical_epoch=0,law_identity=0;
    bool all_complete=false,completed_legal_upper_route=false,useful_refinement_remaining=true;
};
struct Interval {Tuple optimistic{},nominal{},pessimistic{};};
struct Candidate {Interval interval{};int route_evidence_round=-1;bool useful=false,complete=false;};
struct State {
    Menu menu{};std::array<Candidate,max_jobs> candidates{};
    MatchedLaw law{};Limitation limitation{};CostEnvelope cost{};
    int round=-1,actor=-1,vertices_used=0,optional_vertices_used=0,optional_vertex_quota=256,refinements=0,round_robin=0,chosen=-1;
    std::uint64_t physical_epoch=0,fuel_remaining=0,reserve=0;
    bool priority_enabled=true,optional_supported=false,invalidated=false,contract_violation=false;
};
inline bool valid_tuple(Tuple value,const Information& info) {
    const auto cap=std::int64_t(std::numeric_limits<unsigned>::max());
    return value.z>=-1&&value.z<=1&&value.margin>=-cap&&value.margin<=cap&&
        value.first_conversion>=info.round&&std::int64_t(value.first_conversion)<=std::int64_t(info.round)+horizon(info)+1;
}
inline bool valid_rollouts(const Rollouts& rows,const State& state,const Information& info) {
    // A missing route makes this candidate's arrival later than the forecast.
    // Merely tagging an arbitrary profitable tuple with that late arrival does
    // not make it a completed pessimistic rollout.
    const auto deadline=std::int64_t(info.round)+horizon(info);
    const bool route_supported=rows.completed_legal_upper_route||
        (std::int64_t(rows.pessimistic_arrival)>deadline&&rows.pessimistic.z<=0&&
         rows.pessimistic.margin<=0&&std::int64_t(rows.pessimistic.first_conversion)==deadline+1);
    return rows.all_complete&&rows.forecast_round==state.round&&rows.physical_epoch==state.physical_epoch&&
        rows.law_identity==state.law.identity&&rows.route_evidence_round<=state.round&&
        valid_tuple(rows.chebyshev,info)&&valid_tuple(rows.nominal,info)&&valid_tuple(rows.pessimistic,info)&&
        route_supported;
}
inline void publish(State& state,const Information& info,int candidate,const Rollouts& rows) {
    auto& cache=state.candidates[candidate];const auto& key=state.menu.jobs[candidate];
    cache.interval.nominal=rows.nominal;cache.interval.optimistic=cache.interval.pessimistic=rows.nominal;
    for(Tuple value:{rows.chebyshev,rows.pessimistic}) {
        if(compare(info,value,key,cache.interval.optimistic,key)>0)cache.interval.optimistic=value;
        if(compare(info,value,key,cache.interval.pessimistic,key)<0)cache.interval.pessimistic=value;
    }
    cache.route_evidence_round=rows.route_evidence_round;cache.useful=rows.useful_refinement_remaining;cache.complete=true;
}
inline int leader(const State& state,const Information& info) {
    int chosen=-1;
    if(state.invalidated||state.round!=info.round||state.actor!=info.own.id)return chosen;
    for(int n=0;n<state.menu.count;n++)if(state.candidates[n].complete&&
        (chosen<0||compare(info,state.candidates[n].interval.nominal,state.menu.jobs[n],
                         state.candidates[chosen].interval.nominal,state.menu.jobs[chosen])>0))chosen=n;
    return chosen;
}
inline bool begin(State& state,const Information& info,const Menu& menu,const Rollouts* rows,int count,
                  std::uint64_t physical_epoch,MatchedLaw law,Limitation limitation,CostEnvelope cost,
                  int reference_vertices_used,std::uint64_t fuel_remaining,std::uint64_t reserve,bool enabled,
                  int optional_vertex_quota=-1) {
    state.invalidated=true;state.chosen=-1;
    if(count<0||count>max_jobs||count!=menu.count||(count&&!rows)||info.round<0||!physical_epoch||
        info.group_count<0||info.group_count>max_groups||info.altar_count<0||info.altar_count>max_altars)return false;
    for(int n=0;n<count;n++) {
        const auto& job=menu.jobs[n];
        if(int(job.kind)<0||int(job.kind)>int(JobKind::hold)||job.group < -1||job.group>=info.group_count||
            job.second_group < -1||job.second_group>=info.group_count||job.altar < -1||job.altar>=info.altar_count||
            job.nominal_travel<0||(job.kind==JobKind::farm&&(job.group<0||job.altar<0))||
            (job.kind==JobKind::bank&&job.altar<0))return false;
    }
    state.menu=menu;state.round=info.round;state.actor=info.own.id;state.physical_epoch=physical_epoch;
    state.law=law;state.limitation=limitation;state.cost=cost;state.fuel_remaining=fuel_remaining;state.reserve=reserve;
    state.vertices_used=reference_vertices_used;state.refinements=0;state.round_robin=0;state.priority_enabled=enabled;
    state.optional_vertices_used=0;state.optional_vertex_quota=optional_vertex_quota<0?shared_vertex_quota-reference_vertices_used:optional_vertex_quota;
    state.contract_violation=false;
    state.optional_supported=law.identity&&law.receipt_source&&law.repeatable&&law.matched_complete_forecasts&&
        law.clock==ClockLaw::engine_calendar_and_phase_order&&limitation.source&&limitation.law_identity==law.identity&&
        limitation.legal_large_map_state&&limitation.complete_measurements&&limitation.competing_refinements_exceed_reserve&&
        cost.source&&cost.law_identity==law.identity&&cost.frozen&&cost.measured_operations&&cost.maximum_measured_cost&&
        reference_vertices_used>=0&&reference_vertices_used<=shared_vertex_quota&&state.optional_vertex_quota>=0&&
        state.optional_vertex_quota<=shared_vertex_quota-reference_vertices_used;
    for(int n=0;n<count;n++) {
        state.candidates[n].complete=false;
        if(!valid_rollouts(rows[n],state,info))return false;
        publish(state,info,n,rows[n]);
    }
    state.invalidated=false;state.chosen=leader(state,info);return true;
}
struct Priority {int component=3,candidate=-1;std::int64_t gap=0;};
inline Priority priority(const State& state,const Information& info,int candidate,int leading) {
    Priority result;
    const auto& entry=state.candidates[candidate];const auto& incumbent=state.candidates[leading];
    if(!entry.complete||!entry.useful||compare(info,entry.interval.optimistic,state.menu.jobs[candidate],
                                           incumbent.interval.pessimistic,state.menu.jobs[leading])<=0)return result;
    const auto a=entry.interval.optimistic,b=incumbent.interval.pessimistic,n=incumbent.interval.nominal;
    if(a.z!=b.z){result.component=0;result.gap=std::int64_t(a.z)-n.z;}
    else if(a.margin!=b.margin){result.component=1;result.gap=a.margin-n.margin;}
    else if(a.first_conversion!=b.first_conversion){result.component=2;result.gap=std::int64_t(n.first_conversion)-a.first_conversion;}
    else return result; // Fixed nominal travel/key ties are not uncertain utility.
    result.candidate=candidate;return result;
}
inline int next_candidate(State& state,const Information& info) {
    const int leading=leader(state,info);if(leading<0)return -1;
    if(!state.priority_enabled)for(int offset=0;offset<state.menu.count;offset++) {
        const int n=(state.round_robin+offset)%state.menu.count;
        if(state.candidates[n].complete&&state.candidates[n].useful){state.round_robin=(n+1)%state.menu.count;return n;}
    }
    if(!state.priority_enabled)return -1;
    bool ranking_uncertain=false;
    for(int n=0;n<state.menu.count;n++)if(n!=leading&&state.candidates[n].complete&&
        compare(info,state.candidates[n].interval.optimistic,state.menu.jobs[n],
                state.candidates[leading].interval.pessimistic,state.menu.jobs[leading])>0)ranking_uncertain=true;
    if(!ranking_uncertain)return -1; // Refining an isolated leader cannot change the selected key.
    Priority best;
    for(int n=0;n<state.menu.count;n++) {
        const auto value=priority(state,info,n,leading);if(value.candidate<0)continue;
        if(best.candidate<0||value.component<best.component||(value.component==best.component&&
            (value.gap>best.gap||(value.gap==best.gap&&
             (state.candidates[n].route_evidence_round<state.candidates[best.candidate].route_evidence_round||
              (state.candidates[n].route_evidence_round==state.candidates[best.candidate].route_evidence_round&&
               job_key_less(info,state.menu.jobs[n],state.menu.jobs[best.candidate])))))))best=value;
    }
    return best.candidate;
}
inline bool fuel_admits(const State& state) {
    if(state.fuel_remaining<state.reserve)return false;
    const auto available=state.fuel_remaining-state.reserve,maximum=state.cost.maximum_measured_cost;
    return maximum<=available&&(maximum/2+maximum%2)<=available-maximum;
}
struct Refinement {
    int candidate=-1,vertices_used=0;
    std::uint64_t fuel_used=0;
    bool completed=false,useful_operation=true;
    Rollouts rollouts{};
};
using Refiner=Refinement(*)(int candidate,int maximum_vertices,void* context);
// The refiner owns bounded graph progress and performs no SDK action. Its
// fuel report includes completed rollout work. Cost envelopes are frozen input,
// never an online learner. Fuel changes neither calendar time nor gem utility.
inline int refine(State& state,const Information& info,std::uint64_t physical_epoch,Refiner operation,void* context) {
    if(state.invalidated||state.round!=info.round||state.actor!=info.own.id||state.physical_epoch!=physical_epoch){state.invalidated=true;state.chosen=-1;return -1;}
    if(state.contract_violation||!state.optional_supported||!operation)return state.chosen=leader(state,info);
    while(state.refinements<refinement_cap&&state.vertices_used<shared_vertex_quota&&
          state.optional_vertices_used<state.optional_vertex_quota&&fuel_admits(state)) {
        const int candidate=next_candidate(state,info);if(candidate<0)break;
        const int quota=std::min({vertices_per_refinement,shared_vertex_quota-state.vertices_used,
                                  state.optional_vertex_quota-state.optional_vertices_used});
        const auto result=operation(candidate,quota,context);++state.refinements;
        if(result.candidate!=candidate||result.vertices_used<0||result.vertices_used>quota||result.fuel_used>state.fuel_remaining) {
            state.contract_violation=true;break;
        }
        state.vertices_used+=result.vertices_used;state.optional_vertices_used+=result.vertices_used;state.fuel_remaining-=result.fuel_used;
        if(result.fuel_used>state.cost.maximum_measured_cost){state.contract_violation=true;break;}
        if(result.completed) {
            if(!valid_rollouts(result.rollouts,state,info)){state.contract_violation=true;break;}
            publish(state,info,candidate,result.rollouts);
        } else if(!result.useful_operation||!result.vertices_used)state.candidates[candidate].useful=false;
    }
    return state.chosen=leader(state,info);
}
inline void invalidate(State& state){state.invalidated=true;state.chosen=-1;}
inline bool action_fresh(const State& state,const Information& info,std::uint64_t physical_epoch) {
    return !state.invalidated&&!state.contract_violation&&state.fuel_remaining>=state.reserve&&
        state.round==info.round&&state.actor==info.own.id&&state.physical_epoch==physical_epoch&&state.chosen>=0;
}
// Required runtime adapter: three complete matching-law rollouts (Chebyshev,
// current M0, completed legal-route upper or beyond-H pessimistic arrival),
// bounded public graph operations sharing the preprocessing quota, frozen
// measured evidence, and fresh SDK action validation. No launch proves these.
} // namespace plan4::metronome
