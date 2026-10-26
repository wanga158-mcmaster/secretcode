#pragma once
// SDK-free, assumed standalone renewal model. The bridge must independently
// validate legal lifecycle evidence and every inherited forecast/helper domain.
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace plan3_indexer {
inline constexpr unsigned maximum_projects=8;
enum class State { known,unknown,unavailable,cutoff };
enum class Reason { none,unknown,unavailable,incomplete,current_admission,fresh_clear,
    route,recovery,numeric,horizon,no_project,invalid_snapshot,cutoff };
struct Project {
    int key=-1;
    State state=State::known;
    std::int64_t tau=0,Cfresh=0,Rfresh=0,Rcurrent=0,regenMax=0,G=0;
    bool currentComplete=true,currentAdmitted=true,freshClearAdmitted=true,
         routeKnown=true,recoveryKnown=true;
};
struct Input {
    int round=0,observationRound=0,roundLimit=0,width=0,height=0;
    std::int64_t carriedBag=0;
    unsigned physicalCount=0,projectCount=0;
    std::array<int,maximum_projects> physicalKeys{};
    int physicalCursor=-1; // A key in the full physical order, never an eligible index.
    std::array<Project,maximum_projects> projects{};
    bool cutoff=false;
};
struct Score { std::uint64_t numerator=0,denominator=1; };
// Only produced scores: numerator<=128,000,000; denominator<=8192. Their
// cross-products fit uint64_t. This is not an arbitrary uint64 comparator.
inline int compare(Score a,Score b) {
    const auto left=a.numerator*b.denominator,right=b.numerator*a.denominator;
    return left>right?1:left<right?-1:0;
}
struct Row {
    int key=-1;
    State state=State::unavailable;
    Reason reason=Reason::unavailable;
    bool eligible=false;
    unsigned tau=0,N=0,P=0,D=0;
    std::uint64_t G=0;
    Score index{},rate{};
};
struct Choice {
    bool present=false;
    unsigned row=0;
    int key=-1;
    Score score{};
    unsigned N=0,P=0,D=0;
};
struct Triple {
    State state=State::unavailable;
    Reason reason=Reason::invalid_snapshot;
    unsigned H=0,rowCount=0,eligibleCount=0,unknownCount=0,rejectedCount=0;
    std::array<Row,maximum_projects> rows{};
    Choice active{},rate{},circuit{};
    unsigned projectedCount=0;
    std::array<int,maximum_projects> projectedCircuit{};
};
inline int physical_index(const Input& input,int key) {
    for(unsigned n=0;n<input.physicalCount && n<maximum_projects;++n)
        if(input.physicalKeys[n]==key)return int(n);
    return -1;
}
inline bool physical_valid(const Input& input) {
    if(input.width<1 || input.width>100 || input.height<1 || input.height>100 ||
       input.physicalCount>maximum_projects)return false;
    for(unsigned n=0;n<input.physicalCount;++n)
        if(input.physicalKeys[n]<0 || input.physicalKeys[n]>=input.width*input.height ||
           (n && input.physicalKeys[n-1]>=input.physicalKeys[n]))return false;
    return input.physicalCount?physical_index(input,input.physicalCursor)>=0:input.physicalCursor==-1;
}
inline Row project_row(const Project& project,unsigned horizon) {
    Row row;row.key=project.key;
    const auto reject=[&](Reason reason,State state=State::unavailable) {
        row.reason=reason;row.state=state;return row;
    };
    if(project.state==State::unknown)return reject(Reason::unknown,State::unknown);
    if(project.state!=State::known)return reject(Reason::unavailable);
    if(!project.currentComplete)return reject(Reason::incomplete,State::unknown);
    if(!project.currentAdmitted)return reject(Reason::current_admission);
    if(!project.freshClearAdmitted)return reject(Reason::fresh_clear);
    if(!project.routeKnown)return reject(Reason::route,State::unknown);
    if(!project.recoveryKnown)return reject(Reason::recovery,State::unknown);
    // Check each term before addition. No signed overflow can precede rejection.
    if(project.G<1 || project.G>1000000 || project.tau<1 || project.tau>8192 ||
       project.Cfresh<1 || project.Cfresh>8192 || project.Rfresh<0 || project.Rfresh>8192 ||
       project.Rcurrent<0 || project.Rcurrent>8192 || project.regenMax<0 || project.regenMax>8192)
        return reject(Reason::numeric);
    const auto period=std::max({project.tau,project.Cfresh+project.Rfresh,
        project.Cfresh+project.Rcurrent,project.Cfresh+project.regenMax});
    const auto duration=project.tau+project.Rcurrent;
    if(period>8192 || duration>8192)return reject(Reason::numeric);
    if(project.tau>horizon)return reject(Reason::horizon);
    row.state=State::known;row.reason=Reason::none;row.eligible=true;
    row.tau=unsigned(project.tau);row.P=unsigned(period);row.D=unsigned(duration);
    row.N=1+(horizon-row.tau)/row.P;row.G=std::uint64_t(project.G);
    row.index={row.G*row.N,horizon};row.rate={row.G,row.D};return row;
}
template<class Ready> Triple evaluate(const Input& input,Ready&& ready) {
    Triple result;
    const auto stop=[&](Reason reason,State state) {
        Triple empty;empty.H=result.H;empty.reason=reason;empty.state=state;return empty;
    };
    if(input.cutoff)return stop(Reason::cutoff,State::cutoff);
    if(!physical_valid(input) || input.projectCount!=input.physicalCount ||
       input.round<0 || input.roundLimit<0 || input.observationRound!=input.round || input.carriedBag!=0)
        return stop(Reason::invalid_snapshot,State::unavailable);
    result.H=unsigned(std::min<std::int64_t>(128,std::max<std::int64_t>(0,
        std::int64_t(input.roundLimit)-input.round)));
    std::array<bool,maximum_projects> represented{};
    for(unsigned n=0;n<input.projectCount;++n) {
        if(!ready() || input.projects[n].state==State::cutoff)return stop(Reason::cutoff,State::cutoff);
        const int found=physical_index(input,input.projects[n].key);
        if(found<0 || represented[unsigned(found)])return stop(Reason::invalid_snapshot,State::unavailable);
        represented[unsigned(found)]=true;
        auto& row=result.rows[n];row=project_row(input.projects[n],result.H);
        if(row.eligible)++result.eligibleCount;
        else if(row.state==State::unknown)++result.unknownCount;
        else ++result.rejectedCount;
    }
    result.rowCount=input.projectCount;
    // Fixed active -> complete-occupation rate -> circuit order. A cutoff at
    // any boundary discards the entire owned triple, never only one ranking.
    for(unsigned mode=0;mode<2;++mode)for(unsigned n=0;n<result.rowCount;++n) {
        if(!ready())return stop(Reason::cutoff,State::cutoff);
        const auto& row=result.rows[n];if(!row.eligible)continue;
        const auto score=mode?row.rate:row.index;auto& choice=mode?result.rate:result.active;
        const int order=choice.present?compare(score,choice.score):1;
        if(order>0 || (order==0 && row.key<choice.key))
            choice={true,n,row.key,score,row.N,row.P,row.D};
    }
    const unsigned start=input.physicalCount?unsigned(physical_index(input,input.physicalCursor)):0;
    for(unsigned offset=0;offset<input.physicalCount;++offset) {
        if(!ready())return stop(Reason::cutoff,State::cutoff);
        const int key=input.physicalKeys[(start+offset)%input.physicalCount];
        for(unsigned n=0;n<result.rowCount;++n)if(result.rows[n].key==key && result.rows[n].eligible) {
            result.projectedCircuit[result.projectedCount++]=key;
            if(!result.circuit.present) {
                const auto& row=result.rows[n];
                result.circuit={true,n,key,row.index,row.N,row.P,row.D};
            }
            break;
        }
    }
    result.state=State::known;result.reason=result.eligibleCount?Reason::none:Reason::no_project;return result;
}
inline Triple evaluate(const Input& input) {return evaluate(input,[] {return true;});}

// Conservative local witness supplied before clearing the original active Cycle.
// It cannot infer a completion from a global score change or complete(true).
struct Completion {
    int previousRound=-1,currentRound=-1,key=-1;
    std::uint64_t previousEpisode=0,currentEpisode=0;
    bool originalCycle=false,conversionStage=false,previousAlive=false,
         previousSubmitting=false,currentAlive=false,currentSubmitting=false,
         cancelled=false,aborted=false;
    std::int64_t previousBag=0,currentBag=0,previousOwnScore=0,currentOwnScore=0;
};
struct CursorResult { bool valid=false,advanced=false;int cursor=-1; };
inline CursorResult advance_circuit(const Input& input,const Completion& witness) {
    CursorResult result{false,false,input.physicalCursor};
    if(!physical_valid(input))return result;
    result.valid=true;const int index=physical_index(input,witness.key);
    const auto quantity=[](std::int64_t value) {return value>=0 && value<=std::numeric_limits<int>::max();};
    if(index<0 || witness.currentRound!=input.round || input.observationRound!=input.round ||
       !witness.originalCycle || !witness.conversionStage || !witness.previousAlive ||
       !witness.previousSubmitting || !witness.currentAlive || witness.currentSubmitting ||
       witness.cancelled || witness.aborted || witness.previousRound<0 ||
       std::int64_t(witness.currentRound)!=std::int64_t(witness.previousRound)+1 ||
       witness.previousEpisode!=witness.currentEpisode || !quantity(witness.previousBag) ||
       !witness.previousBag || witness.currentBag!=0 || !quantity(witness.previousOwnScore) ||
       !quantity(witness.currentOwnScore) || witness.currentOwnScore<witness.previousOwnScore ||
       witness.currentOwnScore-witness.previousOwnScore!=witness.previousBag)return result;
    result.advanced=true;result.cursor=input.physicalKeys[(unsigned(index)+1)%input.physicalCount];return result;
}
} // namespace plan3_indexer
