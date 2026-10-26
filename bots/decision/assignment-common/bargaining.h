#pragma once
// Pure integer full-assignment solver. No SDK, runtime mapper or baseline policy.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace plan3_bargainer {

inline constexpr unsigned maximum_actors=5,maximum_jobs=64,maximum_step_work=8;
enum class CellState : std::uint8_t { unknown,known,unavailable };
struct Cell { CellState state=CellState::unknown;std::uint32_t utility=0; };
struct Input {
    std::vector<std::int32_t> actors,jobs;
    std::vector<Cell> cells; // actor-major, on the same complete matrix for both arms
    std::vector<std::uint32_t> disagreement; // required, in actor order; never inferred
    std::uint32_t horizon=0;
};

// Five little-endian limbs represent every product of at most five uint32 gains.
// Checked multiplication also rejects overflow for defensive standalone callers.
struct Product {
    std::array<std::uint32_t,5> limbs{};
    static Product one() { Product value;value.limbs[0]=1;return value; }
    bool multiply(std::uint32_t factor) {
        Product next;std::uint64_t carry=0;
        for(unsigned i=0;i<limbs.size();++i) {
            // limb*factor+carry <= (2^32-1)^2+(2^32-2) < 2^64.
            const auto value=std::uint64_t(limbs[i])*factor+carry;
            next.limbs[i]=std::uint32_t(value);carry=value>>32;
        }
        if(carry)return false; // Do not publish a truncated or partially changed value.
        *this=next;return true;
    }
};
inline int compare(const Product& a,const Product& b) {
    for(unsigned i=unsigned(a.limbs.size());i>0;--i) {
        if(a.limbs[i-1]!=b.limbs[i-1])return a.limbs[i-1]>b.limbs[i-1]?1:-1;
    }
    return 0;
}

enum class SolverStatus { pending,complete,unknown,invalid };
enum class ArmStatus { unpublished,feasible,infeasible };
struct Assignment {
    std::int32_t job=-1,actor=-1;
    std::uint32_t utility=0;
    std::int64_t gain=0;
};
struct ArmResult {
    ArmStatus status=ArmStatus::unpublished;
    std::vector<std::int32_t> owners; // all job rows, -1 for unassigned; empty if infeasible
    std::vector<Assignment> assignments; // numeric (job,actor) order
    std::vector<std::int64_t> gainsByActor;
    Product product; // active objective only; control keeps the canonical zero value
    std::uint64_t total=0; // complete production, not the active product objective
};
struct PairResult {
    bool complete=false;
    std::vector<std::int32_t> actors,jobs;
    std::vector<std::uint32_t> disagreement;
    std::uint32_t horizon=0;
    ArmResult active,control;
};

namespace detail {
inline SolverStatus validate(const Input& input) {
    if(input.actors.empty() || input.actors.size()>maximum_actors ||
       input.jobs.size()>maximum_jobs || input.disagreement.size()!=input.actors.size() ||
       input.cells.size()!=input.actors.size()*input.jobs.size() ||
       input.horizon<1 || input.horizon>4096)return SolverStatus::invalid;
    for(const auto* ids:{&input.actors,&input.jobs})
        for(std::size_t i=0;i<ids->size();++i)
            if((*ids)[i]<0 || (i && (*ids)[i-1]>=(*ids)[i]))return SolverStatus::invalid;
    bool unknown=false;
    for(const auto& cell:input.cells) {
        if(cell.state==CellState::known)continue;
        if(cell.state!=CellState::unknown && cell.state!=CellState::unavailable)return SolverStatus::invalid;
        if(cell.utility)return SolverStatus::invalid;
        unknown|=cell.state==CellState::unknown;
    }
    return unknown?SolverStatus::unknown:SolverStatus::pending;
}
struct Pair {std::uint8_t job=0,actor=0;};
struct State {
    Product product;
    std::uint64_t total=0;
    std::array<Pair,maximum_actors> pairs{};
    unsigned count=0;
    bool valid=false;
};
inline bool lex_less(const State& a,const State& b) {
    // Sorted numeric input identities make index comparison equivalent to ID comparison.
    for(unsigned i=0;i<a.count && i<b.count;++i) {
        if(a.pairs[i].job!=b.pairs[i].job)return a.pairs[i].job<b.pairs[i].job;
        if(a.pairs[i].actor!=b.pairs[i].actor)return a.pairs[i].actor<b.pairs[i].actor;
    }
    return a.count<b.count;
}
inline bool better(const State& a,const State& b,bool control) {
    if(!a.valid)return false;
    if(!b.valid)return true;
    const int order=control?(a.total>b.total?1:a.total<b.total?-1:0):compare(a.product,b.product);
    return order>0 || (order==0 && lex_less(a,b));
}
} // namespace detail

// start() owns/copies at most320 cells and resets fixed DP banks. Validation,
// ownership/allocation/reset, and final owned-output construction/publication
// are bounded indivisible work OUTSIDE step counters. No native fuel claim.
// Both arms process every nonempty job stream, even when jobs<actors.
class PairedSolver {
    Input input_;
    PairResult result_;
    SolverStatus status_=SolverStatus::invalid;
    std::array<std::array<std::array<detail::State,32>,2>,2> banks_{}; // arm, bank, mask
    std::array<detail::State,2> full_{};
    std::array<unsigned,2> slots_{},copies_{},extensions_{};
    unsigned arm_=0,bank_=0,job_=0,cursor_=0,masks_=0,work_=0,last_=0;
    enum class Phase { copy,scan } phase_=Phase::copy;

    void invalidate() {status_=SolverStatus::invalid;result_={};}
    ArmResult owned_arm(unsigned arm) const {
        ArmResult out;const auto& state=full_[arm];
        if(!state.valid) {out.status=ArmStatus::infeasible;return out;}
        out.status=ArmStatus::feasible;
        out.owners.assign(input_.jobs.size(),-1);
        out.gainsByActor.assign(input_.actors.size(),0);
        out.assignments.reserve(input_.actors.size());
        out.total=state.total;
        if(arm==0)out.product=state.product;
        for(unsigned i=0;i<state.count;++i) {
            const auto pair=state.pairs[i];
            const auto& cell=input_.cells[std::size_t(pair.actor)*input_.jobs.size()+pair.job];
            const auto gain=std::int64_t(cell.utility)-std::int64_t(input_.disagreement[pair.actor]);
            out.owners[pair.job]=input_.actors[pair.actor];out.gainsByActor[pair.actor]=gain;
            out.assignments.push_back({input_.jobs[pair.job],input_.actors[pair.actor],cell.utility,gain});
        }
        return out;
    }
    void publish() {
        // Build locally. Neither arm is externally visible until all allocations
        // and both outputs finish; exceptions cannot publish a half-built pair.
        // A caught allocation failure also cannot resume the exhausted job cursor.
        status_=SolverStatus::invalid;
        PairResult next;
        next.actors=input_.actors;next.jobs=input_.jobs;
        next.disagreement=input_.disagreement;next.horizon=input_.horizon;
        next.active=owned_arm(0);next.control=owned_arm(1);next.complete=true;
        result_=std::move(next);status_=SolverStatus::complete;
    }
    void scan_slot() {
        const auto actors=unsigned(input_.actors.size());
        const unsigned mask=cursor_/actors,actor=cursor_%actors;
        ++slots_[arm_];
        const auto& prior=banks_[arm_][bank_][mask];
        const auto& cell=input_.cells[std::size_t(actor)*input_.jobs.size()+job_];
        const bool control=arm_!=0;
        if(!prior.valid || (mask&(1u<<actor)) || cell.state!=CellState::known ||
           (!control && cell.utility<=input_.disagreement[actor]))return;
        ++extensions_[arm_];
        auto candidate=prior;
        // Strict comparison above precedes unsigned subtraction.
        if(!control && !candidate.product.multiply(cell.utility-input_.disagreement[actor])) {invalidate();return;}
        if(cell.utility>std::numeric_limits<std::uint64_t>::max()-candidate.total ||
           candidate.count>=maximum_actors) {invalidate();return;}
        candidate.total+=cell.utility;
        candidate.pairs[candidate.count++]={std::uint8_t(job_),std::uint8_t(actor)};
        auto& next=banks_[arm_][bank_^1u][mask|(1u<<actor)];
        if(detail::better(candidate,next,control))next=candidate;
    }
    void advance_after_unit() {
        ++cursor_;
        if(phase_==Phase::copy) {
            if(cursor_==masks_) {phase_=Phase::scan;cursor_=0;}
            return;
        }
        if(cursor_!=masks_*input_.actors.size())return;
        bank_^=1u;++job_;cursor_=0;
        if(job_==input_.jobs.size()) {
            // Only the FULL mask can supply a feasible arm; partial results are
            // not fallbacks. Keep the first completed arm private.
            full_[arm_]=banks_[arm_][bank_][masks_-1];
            if(arm_==1) {publish();return;}
            arm_=1;bank_=job_=0;
        }
        phase_=Phase::copy;
    }
public:
    PairedSolver()=default;
    explicit PairedSolver(const Input& input) {start(input);}
    void start(const Input& input) {
        input_={};result_={};banks_={};full_={};
        slots_={};copies_={};extensions_={};
        arm_=bank_=job_=cursor_=masks_=work_=last_=0;phase_=Phase::copy;
        status_=detail::validate(input);
        if(status_!=SolverStatus::pending)return;
        // Reject malformed/unknown borrowed inputs before copying any caller
        // vectors. Only a validated <=320-cell input is owned. If allocation
        // throws during the copy, no pending or old published pair survives.
        status_=SolverStatus::invalid;
        input_=input;
        status_=SolverStatus::pending;
        masks_=1u<<input_.actors.size();
        for(unsigned arm=0;arm<2;++arm) {
            auto& root=banks_[arm][0][0];root.valid=true;
            if(arm==0)root.product=Product::one();
        }
        // Explicit empty-job C++ extension. Python rejects this input shape.
        if(input_.jobs.empty())publish();
    }
    SolverStatus step(unsigned maximum_work=maximum_step_work) {
        last_=0;
        // Invalid budgets invalidate even a formerly published terminal pair.
        if(maximum_work>maximum_step_work) {invalidate();return status_;}
        if(status_!=SolverStatus::pending || maximum_work==0)return status_;
        while(last_<maximum_work && status_==SolverStatus::pending) {
            if(phase_==Phase::copy) {
                banks_[arm_][bank_^1u][cursor_]=banks_[arm_][bank_][cursor_];
                ++copies_[arm_];
            } else scan_slot();
            ++last_;++work_;
            if(status_==SolverStatus::pending)advance_after_unit();
        }
        return status_;
    }
    SolverStatus status() const {return status_;}
    bool complete() const {return status_==SolverStatus::complete;}
    const PairResult& result() const {return result_;}
    unsigned candidate_slots() const {return slots_[0]+slots_[1];}
    unsigned candidate_slots(bool control) const {return slots_[unsigned(control)];}
    unsigned bank_copies() const {return copies_[0]+copies_[1];}
    unsigned bank_copies(bool control) const {return copies_[unsigned(control)];}
    unsigned active_extensions() const {return extensions_[0];}
    unsigned control_extensions() const {return extensions_[1];}
    unsigned work_units() const {return work_;}
    unsigned last_step_units() const {return last_;}
};
} // namespace plan3_bargainer
