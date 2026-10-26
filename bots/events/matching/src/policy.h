#pragma once
#include "assignment.h"
#include "observation.h"
#include "frontier.h"
#include "farm_observation.h"
#include "commitment.h"

namespace p11_matching {
struct Policy : p11_tempo::Policy {
    static constexpr const char* name="p11-matching";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool local_assignment=true;
    static constexpr bool trace_scan=true;
    static constexpr bool reserve_action_budget=true;
    static constexpr bool reuse_farm_observations=true;
    static constexpr bool committed_assignment=true;
    using CommitmentDomain=commitment::Domain;
    using CommitmentBoard=commitment::Board;
    using CommitmentStatus=commitment::Status;
    static bool commitments_supported(CommitmentDomain domain,int round) {
        return commitment::supported(domain,round);
    }
    static bool claimed(const CommitmentBoard& board,int self_slot,int job) {
        return commitment::claimed(board,self_slot,job);
    }
    template<class Now,class Fuel,class Read,class Write>
    static auto publish_commitment(CommitmentDomain domain,int self_slot,int job,int round,
            Now now,Fuel fuel,Read read,Write write) {
        return commitment::read_advance_publish(domain,self_slot,job,round,now,fuel,read,write);
    }
    static int select_committed(const p11::State& state,std::span<const p11::Candidate> candidates,
            const Worker& self,const CommitmentBoard& board,int self_slot) {
        return commitment::select<Policy>(state,candidates,self,board,self_slot);
    }
    using FarmObservation=p11_matching::FarmObservation;
    using BasicTargetView=p11_matching::BasicTargetView;
    template<class Read>
    static FarmObservation observe_self(Read read) {
        return p11_matching::observe_self(std::move(read));
    }
    static std::optional<int> baseline_target(std::span<const BasicTargetView> views,
            float damage,int observed_round,int current_round) {
        return p11_matching::baseline_from_detail(views,damage,observed_round,current_round);
    }
    using AssignmentJob=Job;
    using AssignmentWorker=Worker;
    template<class Read>
    static std::optional<PeerObservation> observe_worker(int handle,Read read) {
        return p11_matching::observe_worker(handle,std::move(read));
    }
    static std::vector<int> detail_frontier(const p11::State&state,
            std::span<const p11::Candidate> candidates,int limit,
            int incumbent,int fallback,int explore) {
        return p11_matching::detail_frontier<Policy>(state,candidates,limit,incumbent,fallback,explore);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {
        return p11_tempo::Policy::score(s,c);
    }
    static int assign(const p11::State&s,std::span<const Job>jobs,std::span<const Worker>workers,int self_id) {
        return p11_matching::assign(s,jobs,workers,self_id);
    }
    static int select(const p11::State&s,std::span<const p11::Candidate>candidates,int assigned_id) {
        if(assigned_id<0)return -1; // Preserve the solver's private wait.
        for(const auto&candidate:candidates)
            if(candidate.id==assigned_id&&p11::feasible(s,candidate)
                &&std::isfinite(p11_tempo::Policy::score(s,candidate)))return assigned_id;
        // A completed detailed observation can invalidate a coarse row. Reuse
        // the actual Tempo selector for the currently feasible alternatives.
        return p11::choose<p11_tempo::Policy>(s,candidates);
    }
};
} // namespace p11_matching
