#pragma once
#include "matching.h"
#include "bargaining.h"

namespace plan3_assignment {
enum class Policy : unsigned char { matchmaker_exact,matchmaker_greedy,bargainer_product,bargainer_total };
inline bool bargaining(Policy policy) { return policy==Policy::bargainer_product || policy==Policy::bargainer_total; }
inline bool valid_policy(Policy policy) { return unsigned(policy)<=unsigned(Policy::bargainer_total); }
inline const char* policy_name(Policy policy) {
    switch(policy) {
    case Policy::matchmaker_exact:return "matchmaker-exact";
    case Policy::matchmaker_greedy:return "matchmaker-greedy";
    case Policy::bargainer_product:return "bargainer-product";
    case Policy::bargainer_total:return "bargainer-total";
    }
    return "invalid";
}
using Matrix=plan3_matchmaker::Matrix;
using KernelStatus=plan3_matchmaker::SolverStatus;
static_assert(unsigned(KernelStatus::pending)==unsigned(plan3_bargainer::SolverStatus::pending) &&
              unsigned(KernelStatus::complete)==unsigned(plan3_bargainer::SolverStatus::complete) &&
              unsigned(KernelStatus::unknown)==unsigned(plan3_bargainer::SolverStatus::unknown) &&
              unsigned(KernelStatus::invalid)==unsigned(plan3_bargainer::SolverStatus::invalid));
struct Result {
    bool complete=false,feasible=false;
    std::vector<int> owners;
    std::vector<std::uint32_t> disagreement;
    std::uint64_t total=0;
    plan3_bargainer::Product product;
};
// Stand aside with no assigned excursion converts zero gems in the shared
// H128 model. This declared idle outside option is NOT measured production,
// an empirical entitlement or a claim that unassigned actors cannot act.
inline std::vector<std::uint32_t> idle_disagreement(const Matrix& matrix) {
    return std::vector<std::uint32_t>(matrix.actors.size(),0);
}
class Solver {
    Policy policy_;
    plan3_matchmaker::ExactMatching exact_;
    plan3_matchmaker::GreedyMatching greedy_;
    plan3_bargainer::PairedSolver bargain_;
    Result result_;
    KernelStatus status_=KernelStatus::invalid;
    unsigned last_=0;
    void publish() {
        status_=KernelStatus::invalid;
        Result next;next.complete=true;
        if(bargaining(policy_)) {
            const auto& pair=bargain_.result();const auto& arm=policy_==Policy::bargainer_total?pair.control:pair.active;
            next.feasible=arm.status==plan3_bargainer::ArmStatus::feasible;
            next.disagreement=pair.disagreement;next.owners=arm.owners;next.total=arm.total;next.product=arm.product;
        } else {
            const auto& matching=policy_==Policy::matchmaker_greedy?greedy_.result():exact_.result();
            next.feasible=matching.complete;next.owners=matching.owners;next.total=matching.value;
        }
        result_=std::move(next);status_=KernelStatus::complete;
    }
public:
    explicit Solver(Policy policy):policy_(policy) {}
    void start(const Matrix& matrix) {
        result_={};last_=0;status_=valid_policy(policy_)?plan3_matchmaker::matching_detail::validate(matrix):KernelStatus::invalid;
        if(status_!=KernelStatus::pending)return; // Validate borrowed bounds before any kernel copy.
        if(bargaining(policy_)) {
            plan3_bargainer::Input input;input.actors.assign(matrix.actors.begin(),matrix.actors.end());input.jobs.assign(matrix.groups.begin(),matrix.groups.end());
            input.disagreement=idle_disagreement(matrix);input.horizon=128;input.cells.reserve(matrix.cells.size());
            for(const auto& cell:matrix.cells)input.cells.push_back({static_cast<plan3_bargainer::CellState>(cell.state),cell.utility});
            bargain_.start(input);status_=static_cast<KernelStatus>(bargain_.status());
        } else if(policy_==Policy::matchmaker_greedy) {greedy_.start(matrix);status_=greedy_.status();}
        else {exact_.start(matrix);status_=exact_.status();}
        if(status_==KernelStatus::complete)publish();
    }
    KernelStatus step(unsigned work=8) {
        last_=0;
        if(work>8){if(bargaining(policy_))bargain_.step(work);status_=KernelStatus::invalid;result_={};return status_;}
        if(status_!=KernelStatus::pending)return status_;
        if(bargaining(policy_)){bargain_.step(work);last_=bargain_.last_step_units();status_=static_cast<KernelStatus>(bargain_.status());}
        else if(policy_==Policy::matchmaker_greedy){greedy_.step(work);last_=greedy_.last_step_units();status_=greedy_.status();}
        else{exact_.step(work);last_=exact_.last_step_units();status_=exact_.status();}
        if(status_==KernelStatus::complete)publish();return status_;
    }
    KernelStatus status() const {return status_;}
    unsigned last_step_units() const {return last_;}
    const Result& result() const {return result_;}
    const plan3_bargainer::PairResult& bargaining_pair() const {return bargain_.result();}
};
} // namespace plan3_assignment
