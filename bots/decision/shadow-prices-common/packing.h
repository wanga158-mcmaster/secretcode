#pragma once
// Exact four-resource fractional packing surrogate. Allocations are a price
// certificate, not a composed executable camp schedule or native safety proof.
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace plan3_shadow_prices {
inline constexpr unsigned maximum_jobs=28,resources=4;
inline constexpr std::int64_t numeric_limit=1000000;
using JobKey=std::array<int,5>;
using Wide=__int128;
using UnsignedWide=unsigned __int128;
using Costs=std::array<std::int64_t,resources>;
enum Resource : unsigned {health,time,capital,lost_actions};
enum class State {known,unknown,unavailable,cutoff};
enum class Reason {none,invalid_snapshot,unknown,incomplete,current_admission,cost_unknown,numeric,
                   individual_budget,no_job,cutoff,certificate};
struct Fraction {Wide numerator=0,denominator=1;};
inline Wide magnitude(Wide n) {return n<0?-n:n;}
inline Wide gcd(Wide a,Wide b) {
    a=magnitude(a);b=magnitude(b);while(b){const Wide next=a%b;a=b;b=next;}return a;
}
inline Fraction fraction(Wide n,Wide d) {
    if(d<0){n=-n;d=-d;}const Wide divisor=gcd(n,d);return {n/divisor,d/divisor};
}
// Input bounds give determinants <=24*(10^6)^4 and objectives/surpluses
// <10^33. Those fit signed128; comparing their products needs more than128.
// Exact unsigned128-by128 products use eight32-bit limbs, without division.
inline std::array<std::uint32_t,8> product(UnsignedWide a,UnsignedWide b) {
    std::array<std::uint32_t,8> out{};
    for(unsigned i=0;i<4;++i) {
        std::uint64_t carry=0;const auto x=std::uint32_t(a>>(i*32));
        for(unsigned j=0;j<4;++j) {
            const std::uint64_t next=std::uint64_t(x)*std::uint32_t(b>>(j*32))+out[i+j]+carry;
            out[i+j]=std::uint32_t(next);carry=next>>32;
        }
        out[i+4]=std::uint32_t(carry);
    }
    return out;
}
inline int compare(Fraction a,Fraction b) {
    if((a.numerator<0)!=(b.numerator<0))return a.numerator<0?-1:1;
    const auto left=product(UnsignedWide(magnitude(a.numerator)),UnsignedWide(b.denominator));
    const auto right=product(UnsignedWide(magnitude(b.numerator)),UnsignedWide(a.denominator));
    for(unsigned i=8;i>0;--i)if(left[i-1]!=right[i-1]) {
        const int order=left[i-1]>right[i-1]?1:-1;return a.numerator<0?-order:order;
    }
    return 0;
}
inline std::string decimal(Wide n) {
    const bool negative=n<0;auto value=UnsignedWide(magnitude(n));std::string out;
    do {out.push_back(char('0'+value%10));value/=10;}while(value);
    if(negative)out.push_back('-');std::reverse(out.begin(),out.end());return out;
}
template<unsigned N> struct Basis {
    std::array<Wide,N> numerators{};Wide denominator=1;
    bool operator==(const Basis&) const =default;
};
template<unsigned N> inline Basis<N> normalized(Basis<N> point) {
    if(point.denominator<0){point.denominator=-point.denominator;for(auto& n:point.numerators)n=-n;}
    Wide divisor=point.denominator;for(auto n:point.numerators)divisor=gcd(divisor,n);
    for(auto& n:point.numerators)n/=divisor;point.denominator/=divisor;return point;
}
template<unsigned N> inline bool lex_less(const Basis<N>& a,const Basis<N>& b) {
    for(unsigned i=0;i<N;++i){const int order=compare({a.numerators[i],a.denominator},{b.numerators[i],b.denominator});if(order)return order<0;}
    return false;
}
struct Job {
    JobKey key{0,-1,-1,-1,-1};
    State state=State::known;
    std::int64_t reward=0;
    Costs cost{};
    bool complete=true,currentAdmitted=true,costsKnown=false;
    bool operator==(const Job&) const =default;
};
struct Input {
    int round=0,observationRound=0,roundLimit=0;
    std::int64_t carriedBag=0;
    Costs budget{};
    unsigned rowCount=0;
    std::array<Job,maximum_jobs> jobs{};
    // Runtime freezes the first certified derived vector. Before that first
    // certificate both arms use the same newly derived reference prices.
    std::optional<Basis<resources>> fixedPrices;
    bool cutoff=false;
};
struct Row {
    JobKey key{0,-1,-1,-1,-1};
    State state=State::unavailable;
    Reason reason=Reason::numeric;
    bool eligible=false;
    std::int64_t reward=0;
    Costs cost{};
    Fraction allocation{},surplus{},controlScore{};
};
struct Choice {bool present=false;unsigned row=0;JobKey key{0,-1,-1,-1,-1};Fraction score{};};
struct Result {
    State state=State::unavailable;
    Reason reason=Reason::invalid_snapshot;
    unsigned H=0,rowCount=0,eligibleCount=0,unknownCount=0,rejectedCount=0;
    std::vector<Row> rows;
    std::array<JobKey,maximum_jobs> canonicalKeys{};
    Basis<maximum_jobs> primal;
    Basis<resources> dual; // health,time,capital,lost-action opportunities
    Basis<resources> controlPrices;
    Fraction objective;
    Choice active,control;
    unsigned pivots=0,pricingChecks=0,systemSolves=0;
    bool controlFromCurrent=false;
    bool certified=false;
};
namespace detail {
using Matrix=std::array<std::array<std::int64_t,resources>,resources>;
inline Wide determinant(const Matrix& matrix,unsigned n) {
    std::array<unsigned,resources> permutation{0,1,2,3};Wide result=0;
    do {
        unsigned inversions=0;Wide term=1;
        for(unsigned i=0;i<n;++i) {
            term*=matrix[i][permutation[i]];
            for(unsigned j=0;j<i;++j)inversions+=permutation[j]>permutation[i];
        }
        result+=(inversions&1)?-term:term;
    }while(std::next_permutation(permutation.begin(),permutation.begin()+n));
    return result;
}
inline Basis<resources> solve(const Matrix& matrix,const Costs& rhs,unsigned n) {
    Basis<resources> out;out.denominator=determinant(matrix,n);
    if(!out.denominator)return out;
    for(unsigned column=0;column<n;++column) {
        auto replaced=matrix;for(unsigned row=0;row<n;++row)replaced[row][column]=rhs[row];
        out.numerators[column]=determinant(replaced,n);
    }
    // Preserve the identical raw positive determinant for ratio tests between
    // B^-1 b and B^-1 enteringColumn. Normalize only published certificates.
    if(out.denominator<0){out.denominator=-out.denominator;for(auto& n:out.numerators)n=-n;}
    return out;
}
inline Row whole_row(const Job& job,const Input& input,unsigned H) {
    Row row;row.key=job.key;
    const auto reject=[&](Reason reason,State state=State::unavailable){row.reason=reason;row.state=state;return row;};
    if(job.state==State::unknown)return reject(Reason::unknown,State::unknown);
    if(job.state!=State::known)return reject(Reason::numeric);
    if(!job.complete)return reject(Reason::incomplete,State::unknown);
    if(!job.currentAdmitted)return reject(Reason::current_admission);
    if(!job.costsKnown)return reject(Reason::cost_unknown,State::unknown);
    if(job.reward<1 || job.reward>numeric_limit || job.cost[time]<0 || job.cost[time]>H)return reject(Reason::numeric);
    bool positiveCost=false;
    for(unsigned r=0;r<resources;++r) {
        if(job.cost[r]<0 || job.cost[r]>numeric_limit)return reject(Reason::numeric);
        if(job.cost[r]>input.budget[r])return reject(Reason::individual_budget);
        positiveCost|=job.cost[r]>0;
    }
    if(!positiveCost)return reject(Reason::numeric); // Unsupported unbounded free-income column.
    row.state=State::known;row.reason=Reason::none;row.eligible=true;row.reward=job.reward;row.cost=job.cost;return row;
}
} // namespace detail

template<class Ready> Result evaluate(const Input& input,Ready&& ready) {
    Result result;
    const auto stop=[](Reason reason,State state){Result out;out.reason=reason;out.state=state;return out;};
    if(input.cutoff)return stop(Reason::cutoff,State::cutoff);
    if(input.round<0 || input.roundLimit<0 || input.observationRound!=input.round || input.carriedBag!=0 || input.rowCount>maximum_jobs)
        return stop(Reason::invalid_snapshot,State::unavailable);
    result.H=unsigned(std::max<std::int64_t>(0,std::min<std::int64_t>(128,std::int64_t(input.roundLimit)-input.round)));
    for(auto b:input.budget)if(b<0 || b>numeric_limit)return stop(Reason::invalid_snapshot,State::unavailable);
    if(input.budget[time]!=result.H)return stop(Reason::invalid_snapshot,State::unavailable);
    constexpr Wide rationalLimit=Wide{24}*numeric_limit*numeric_limit*numeric_limit*numeric_limit;
    if(input.fixedPrices) {
        if(input.fixedPrices->denominator<1 || input.fixedPrices->denominator>rationalLimit)
            return stop(Reason::invalid_snapshot,State::unavailable);
        for(auto n:input.fixedPrices->numerators)if(n<0 || n>rationalLimit)
            return stop(Reason::invalid_snapshot,State::unavailable);
    }
    std::array<unsigned,maximum_jobs> canonical{};
    result.rows.reserve(input.rowCount);
    for(unsigned n=0;n<input.rowCount;++n) {
        if(!ready() || input.jobs[n].state==State::cutoff)return stop(Reason::cutoff,State::cutoff);
        for(unsigned p=0;p<n;++p)if(input.jobs[n].key==input.jobs[p].key)return stop(Reason::invalid_snapshot,State::unavailable);
        result.rows.push_back(detail::whole_row(input.jobs[n],input,result.H));const auto& row=result.rows.back();
        if(row.eligible)canonical[result.eligibleCount++]=n;
        else if(row.state==State::unknown)++result.unknownCount;
        else ++result.rejectedCount;
    }
    result.rowCount=input.rowCount;
    if(!result.eligibleCount){result.state=State::known;result.reason=Reason::no_job;return result;}
    const unsigned count=result.eligibleCount;
    std::sort(canonical.begin(),canonical.begin()+count,[&](unsigned a,unsigned b){return result.rows[a].key<result.rows[b].key;});
    for(unsigned n=0;n<count;++n)result.canonicalKeys[n]=result.rows[canonical[n]].key;
    const auto row_at=[&](unsigned n)->const Row&{return result.rows[canonical[n]];};
    const auto primal_feasible=[&](const Basis<maximum_jobs>& point) {
        if(point.denominator<=0)return false;
        for(unsigned n=0;n<count;++n)if(point.numerators[n]<0)return false;
        for(unsigned r=0;r<resources;++r) {
            Wide used=0;for(unsigned n=0;n<count;++n)used+=point.numerators[n]*row_at(n).cost[r];
            if(used>Wide(input.budget[r])*point.denominator)return false;
        }
        return true;
    };
    const auto primal_value=[&](const Basis<maximum_jobs>& point) {
        Wide sum=0;for(unsigned n=0;n<count;++n)sum+=point.numerators[n]*row_at(n).reward;
        return fraction(sum,point.denominator);
    };
    const auto dual_feasible=[&](const Basis<resources>& point) {
        if(point.denominator<=0)return false;for(auto n:point.numerators)if(n<0)return false;
        for(unsigned n=0;n<count;++n) {
            Wide covered=0;for(unsigned r=0;r<resources;++r)covered+=point.numerators[r]*row_at(n).cost[r];
            if(covered<Wide(row_at(n).reward)*point.denominator)return false;
        }
        return true;
    };
    const auto dual_value=[&](const Basis<resources>& point) {
        Wide sum=0;for(unsigned r=0;r<resources;++r)sum+=point.numerators[r]*input.budget[r];return fraction(sum,point.denominator);
    };
    // Revised primal simplex starts with the four slacks, so b>=0 supplies a
    // feasible basis without phaseI. Bland's lowest canonical variable rules
    // prevent cycling. There are <=C(32,4)=35,960 possible bases at28jobs.
    std::array<unsigned,resources> basis{count,count+1,count+2,count+3};
    const auto column=[&](unsigned variable) {
        Costs out{};if(variable<count)return row_at(variable).cost;
        out[variable-count]=1;return out;
    };
    const auto reward=[&](unsigned variable)->std::int64_t{return variable<count?row_at(variable).reward:0;};
    unsigned maximumBases=1;for(unsigned n=1;n<=resources;++n)maximumBases=maximumBases*(count+n)/n;
    bool optimal=false;
    for(unsigned iteration=0;iteration<maximumBases;++iteration) {
        if(!ready())return stop(Reason::cutoff,State::cutoff);
        detail::Matrix matrix{};Costs basisReward{};
        for(unsigned c=0;c<resources;++c) {
            const auto coefficients=column(basis[c]);basisReward[c]=reward(basis[c]);
            for(unsigned r=0;r<resources;++r)matrix[r][c]=coefficients[r];
        }
        const auto basic=detail::solve(matrix,input.budget,resources);++result.systemSolves;
        if(!basic.denominator)return stop(Reason::certificate,State::unavailable);
        for(auto n:basic.numerators)if(n<0)return stop(Reason::certificate,State::unavailable);
        detail::Matrix transpose{};for(unsigned r=0;r<resources;++r)for(unsigned c=0;c<resources;++c)transpose[r][c]=matrix[c][r];
        const auto dual=detail::solve(transpose,basisReward,resources);++result.systemSolves;
        if(!dual.denominator)return stop(Reason::certificate,State::unavailable);
        unsigned entering=count+resources;
        for(unsigned variable=0;variable<count+resources;++variable) {
            if(!ready())return stop(Reason::cutoff,State::cutoff);++result.pricingChecks;
            if(std::find(basis.begin(),basis.end(),variable)!=basis.end())continue;
            const auto coefficients=column(variable);Wide reduced=Wide(reward(variable))*dual.denominator;
            for(unsigned r=0;r<resources;++r)reduced-=Wide(coefficients[r])*dual.numerators[r];
            if(reduced>0){entering=variable;break;}
        }
        if(entering==count+resources) {
            result.primal.denominator=basic.denominator;
            for(unsigned r=0;r<resources;++r)if(basis[r]<count)result.primal.numerators[basis[r]]=basic.numerators[r];
            result.primal=normalized(result.primal);result.dual=normalized(dual);optimal=true;break;
        }
        if(!ready())return stop(Reason::cutoff,State::cutoff);
        const auto direction=detail::solve(matrix,column(entering),resources);++result.systemSolves;
        if(direction.denominator!=basic.denominator)return stop(Reason::certificate,State::unavailable);
        unsigned leaving=resources;
        for(unsigned r=0;r<resources;++r)if(direction.numerators[r]>0) {
            const int order=leaving==resources?-1:compare({basic.numerators[r],direction.numerators[r]},
                                                        {basic.numerators[leaving],direction.numerators[leaving]});
            if(order<0 || (order==0 && basis[r]<basis[leaving]))leaving=r;
        }
        // A positive bounded resource in every column bounds the LP, so no positive direction
        // here means an invalid certificate rather than an accepted infinity.
        if(leaving==resources)return stop(Reason::certificate,State::unavailable);
        basis[leaving]=entering;++result.pivots;
    }
    if(!ready())return stop(Reason::cutoff,State::cutoff);
    if(!optimal || !primal_feasible(result.primal) || !dual_feasible(result.dual))
        return stop(Reason::certificate,State::unavailable);
    const auto primalValue=primal_value(result.primal),dualValue=dual_value(result.dual);
    if(compare(primalValue,dualValue)!=0)return stop(Reason::certificate,State::unavailable);
    result.controlFromCurrent=!input.fixedPrices;
    result.controlPrices=input.fixedPrices?normalized(*input.fixedPrices):result.dual;
    for(unsigned n=0;n<count;++n) {
        if(!ready())return stop(Reason::cutoff,State::cutoff);
        auto& row=result.rows[canonical[n]];Wide surplus=Wide(row.reward)*result.dual.denominator;
        for(unsigned r=0;r<resources;++r)surplus-=Wide(row.cost[r])*result.dual.numerators[r];
        if(surplus>0 || (result.primal.numerators[n]>0 && surplus!=0))return stop(Reason::certificate,State::unavailable);
        row.allocation=fraction(result.primal.numerators[n],result.primal.denominator);
        row.surplus=fraction(surplus,result.dual.denominator);
        Wide fixed=Wide(row.reward)*result.controlPrices.denominator;
        for(unsigned r=0;r<resources;++r)fixed-=Wide(row.cost[r])*result.controlPrices.numerators[r];
        row.controlScore=fraction(fixed,result.controlPrices.denominator);
    }
    for(unsigned arm=0;arm<2;++arm)for(unsigned n=0;n<count;++n) {
        if(!ready())return stop(Reason::cutoff,State::cutoff);
        const auto index=canonical[n];const auto& row=result.rows[index];const auto score=arm?row.controlScore:row.surplus;
        auto& choice=arm?result.control:result.active;const int order=choice.present?compare(score,choice.score):1;
        // Retain the arm's exact price score. On an exact tie, prefer a
        // supported whole row, then its numeric key. Fractional support is
        // only a tie certificate; whole_row already checked whole-job budgets.
        const bool supported=row.allocation.numerator>0;
        const bool previousSupported=choice.present && result.rows[choice.row].allocation.numerator>0;
        if(order>0 || (order==0 && (supported!=previousSupported?supported:row.key<choice.key)))
            choice={true,index,row.key,score};
    }
    result.certified=true;result.objective=primalValue;result.state=State::known;result.reason=Reason::none;return result;
}
inline Result evaluate(const Input& input){return evaluate(input,[]{return true;});}
} // namespace plan3_shadow_prices
