#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

namespace plan3_matchmaker {

inline constexpr unsigned matching_max_actors=5,matching_max_groups=64;
inline constexpr unsigned matching_max_step_work=8;
enum class CellState : std::uint8_t { unknown,known,unavailable };
struct Cell { CellState state=CellState::unknown; std::uint32_t utility=0; };
struct Matrix {
    std::vector<int> actors,groups;
    std::vector<Cell> cells; // actor-major; canonical sorted nonnegative IDs
};
struct Matching {
    std::vector<int> owners; // aligned with Matrix.groups; physical actor or -1
    std::uint64_t value=0;
    bool complete=false;
};
enum class SolverStatus { pending,complete,unknown,invalid };

namespace matching_detail {
inline SolverStatus validate(const Matrix& matrix) {
    if(matrix.actors.empty() || matrix.actors.size()>matching_max_actors ||
       matrix.groups.size()>matching_max_groups ||
       matrix.cells.size()!=matrix.actors.size()*matrix.groups.size())return SolverStatus::invalid;
    for(const auto* ids:{&matrix.actors,&matrix.groups})
        for(std::size_t i=0;i<ids->size();++i)
            if((*ids)[i]<0 || (i && (*ids)[i-1]>=(*ids)[i]))return SolverStatus::invalid;
    bool unknown=false;
    for(const auto& cell:matrix.cells) {
        if(cell.state==CellState::unknown) {if(cell.utility)return SolverStatus::invalid;unknown=true;}
        else if(cell.state==CellState::unavailable) {if(cell.utility)return SolverStatus::invalid;}
        else if(cell.state!=CellState::known)return SolverStatus::invalid;
    }
    return unknown?SolverStatus::unknown:SolverStatus::pending;
}
struct Pair { std::uint8_t group=0,actor=0; };
struct State {
    std::uint64_t value=0;
    std::array<Pair,matching_max_actors> pairs{};
    unsigned count=0;
    bool valid=false;
};
inline bool lex_less(const State& a,const State& b) {
    for(unsigned i=0;i<a.count && i<b.count;++i) {
        if(a.pairs[i].group!=b.pairs[i].group)return a.pairs[i].group<b.pairs[i].group;
        if(a.pairs[i].actor!=b.pairs[i].actor)return a.pairs[i].actor<b.pairs[i].actor;
    }
    return a.count<b.count;
}
inline bool better(const State& a,const State& b) {
    return a.valid && (!b.valid || a.value>b.value || (a.value==b.value && lex_less(a,b)));
}
} // namespace matching_detail

// start owns/freezes a bounded matrix. Its copy/move, validation and allocation
// (<=320 cells/64 output owners), and restart cleanup are OUTSIDE step accounting.
// No SDK/fuel query occurs here. The caller must guard those indivisible costs.
class ExactMatching {
    Matrix matrix_;
    Matching working_,result_;
    SolverStatus status_=SolverStatus::invalid;
    std::array<std::array<matching_detail::State,32>,2> banks_{};
    matching_detail::State best_{};
    enum class Phase { copy,scan,finish,publish } phase_=Phase::copy;
    unsigned bank_=0,group_=0,cursor_=0,mask_count_=0,output_=0;
    unsigned examined_=0,extensions_=0,work_=0,last_=0;
    void publish() {
        working_.value=best_.value;working_.complete=true;
        result_=std::move(working_);status_=SolverStatus::complete;
    }
public:
    ExactMatching()=default;
    explicit ExactMatching(Matrix matrix) {start(std::move(matrix));}
    void start(Matrix matrix) {
        matrix_=std::move(matrix);working_={};result_={};banks_={};best_={};
        bank_=group_=cursor_=output_=examined_=extensions_=work_=last_=0;
        mask_count_=0;phase_=Phase::copy;status_=matching_detail::validate(matrix_);
        if(status_!=SolverStatus::pending)return;
        working_.owners.assign(matrix_.groups.size(),-1);
        mask_count_=1u<<matrix_.actors.size();banks_[0][0].valid=true;
        if(matrix_.groups.empty()) {best_=banks_[0][0];publish();}
    }
    SolverStatus step(unsigned max_work=matching_max_step_work) {
        last_=0;if(status_!=SolverStatus::pending)return status_;
        if(max_work>matching_max_step_work){status_=SolverStatus::invalid;return status_;}
        while(last_<max_work && status_==SolverStatus::pending) {
            if(phase_==Phase::copy) {
                if(cursor_==mask_count_){phase_=Phase::scan;cursor_=0;continue;}
                banks_[bank_^1u][cursor_]=banks_[bank_][cursor_];++cursor_;
            } else if(phase_==Phase::scan) {
                const auto actors=unsigned(matrix_.actors.size());
                if(cursor_==mask_count_*actors) {
                    bank_^=1u;++group_;cursor_=0;
                    phase_=group_==matrix_.groups.size()?Phase::finish:Phase::copy;continue;
                }
                const unsigned mask=cursor_/actors,actor=cursor_%actors;
                ++cursor_;++examined_;
                const auto& prior=banks_[bank_][mask];
                const auto& cell=matrix_.cells[actor*matrix_.groups.size()+group_];
                if(prior.valid && !(mask&(1u<<actor)) && cell.state==CellState::known) {
                    ++extensions_;auto candidate=prior;
                    if(cell.utility>std::numeric_limits<std::uint64_t>::max()-candidate.value) {
                        status_=SolverStatus::invalid;return status_;
                    }
                    candidate.value+=cell.utility;
                    candidate.pairs[candidate.count++]={std::uint8_t(group_),std::uint8_t(actor)};
                    auto& next=banks_[bank_^1u][mask|(1u<<actor)];
                    if(matching_detail::better(candidate,next))next=candidate;
                }
            } else if(phase_==Phase::finish) {
                if(cursor_==mask_count_){phase_=Phase::publish;continue;}
                const auto& candidate=banks_[bank_][cursor_++];
                if(matching_detail::better(candidate,best_))best_=candidate;
            } else {
                if(output_<best_.count) {
                    const auto pair=best_.pairs[output_++];
                    working_.owners[pair.group]=matrix_.actors[pair.actor];
                } else publish();
            }
            ++last_;++work_;
        }
        return status_;
    }
    bool complete() const {return status_==SolverStatus::complete;}
    SolverStatus status() const {return status_;}
    const Matching& result() const {return result_;}
    unsigned examined() const {return examined_;}
    unsigned extensions() const {return extensions_;}
    unsigned work_units() const {return work_;}
    unsigned last_step_units() const {return last_;}
};

// Repeated best-feasible-positive scans equal the declared global sorted greedy
// order: accepting an edge only removes actors/groups, never adds eligible edges.
class GreedyMatching {
    Matrix matrix_;
    Matching working_,result_;
    SolverStatus status_=SolverStatus::invalid;
    std::array<bool,matching_max_actors> used_actors_{};
    std::array<bool,matching_max_groups> used_groups_{};
    unsigned cursor_=0,assigned_=0,examined_=0,extensions_=0,work_=0,last_=0;
    int best_=-1;
    bool preferable(unsigned edge,unsigned other) const {
        const auto g=unsigned(matrix_.groups.size());
        const auto& x=matrix_.cells[edge];const auto& y=matrix_.cells[other];
        if(x.utility!=y.utility)return x.utility>y.utility;
        if(edge%g!=other%g)return edge%g<other%g;
        return edge/g<other/g;
    }
    void publish() {working_.complete=true;result_=std::move(working_);status_=SolverStatus::complete;}
public:
    GreedyMatching()=default;
    explicit GreedyMatching(Matrix matrix) {start(std::move(matrix));}
    void start(Matrix matrix) {
        matrix_=std::move(matrix);working_={};result_={};used_actors_={};used_groups_={};
        cursor_=assigned_=examined_=extensions_=work_=last_=0;best_=-1;
        status_=matching_detail::validate(matrix_);if(status_!=SolverStatus::pending)return;
        working_.owners.assign(matrix_.groups.size(),-1);if(matrix_.groups.empty())publish();
    }
    SolverStatus step(unsigned max_work=matching_max_step_work) {
        last_=0;if(status_!=SolverStatus::pending)return status_;
        if(max_work>matching_max_step_work){status_=SolverStatus::invalid;return status_;}
        const auto groups=unsigned(matrix_.groups.size());
        while(last_<max_work && status_==SolverStatus::pending) {
            if(cursor_==matrix_.cells.size()) {
                if(best_<0 || assigned_==matrix_.actors.size())publish();
                else {
                    const unsigned edge=unsigned(best_),actor=edge/groups,group=edge%groups;
                    used_actors_[actor]=used_groups_[group]=true;
                    if(matrix_.cells[edge].utility>std::numeric_limits<std::uint64_t>::max()-working_.value) {
                        status_=SolverStatus::invalid;return status_;
                    }
                    working_.owners[group]=matrix_.actors[actor];working_.value+=matrix_.cells[edge].utility;
                    ++assigned_;cursor_=0;best_=-1;
                    if(assigned_==matrix_.actors.size())publish();
                }
            } else {
                const auto edge=cursor_++,actor=edge/groups,group=edge%groups;
                const auto& cell=matrix_.cells[edge];++examined_;
                if(cell.state==CellState::known && cell.utility && !used_actors_[actor] && !used_groups_[group]) {
                    ++extensions_;if(best_<0 || preferable(edge,unsigned(best_)))best_=int(edge);
                }
            }
            ++last_;++work_;
        }
        return status_;
    }
    bool complete() const {return status_==SolverStatus::complete;}
    SolverStatus status() const {return status_;}
    const Matching& result() const {return result_;}
    unsigned examined() const {return examined_;}
    unsigned extensions() const {return extensions_;}
    unsigned work_units() const {return work_;}
    unsigned last_step_units() const {return last_;}
};
} // namespace plan3_matchmaker
