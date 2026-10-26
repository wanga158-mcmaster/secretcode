#pragma once
#include "policy.h"
#include <cstdint>

namespace h12_act_first {
struct Stamp {int round=0,life=0,finance=0;};
struct Receipt {
    bool actual=false,work=false,admissible=false;
    int source=-1;
    Stamp stamp{};
};
struct Proposal {int source=-1,owner=-1,life=-1,finance=-1;bool complete=false;};
constexpr bool reconsider(int no_work_age,int route_stagnation,int failures) {
    return (no_work_age>=60&&route_stagnation>=8)||failures>=6;
}
constexpr bool finance_changed(int bag,int gold,int previous_bag,int previous_gold) {
    return bag!=previous_bag||gold!=previous_gold;
}
// Host prefix performs fresh vetoes and actual useful execution. The audit
// cannot call a perception API: it receives only the host's cached scalars.
class ActFirst {
    Proposal proposal_{};
    int cursor_=0;
public:
    int incumbent=-1,no_work_age=0;
    unsigned audits=0,published=0,adopted=0,vetoed=0,low=0,invalid=0;
    std::uint64_t maximum_audit=0;
    const Proposal& proposal()const{return proposal_;}
    void invalidate(){proposal_={};}
    template<class Host> Receipt tick(Host&host,Policy&policy) {
        const auto fresh=host.stamp();
        if(proposal_.complete) {
            const bool timely=proposal_.owner+1==fresh.round&&proposal_.life==fresh.life&&proposal_.finance==fresh.finance;
            if(timely&&host.veto(proposal_.source)) {incumbent=proposal_.source;++adopted;}
            else ++vetoed;
            proposal_={};
        }
        // Bootstrap/reconsideration is bounded and uses public cached scalars.
        if(incumbent<0||!host.available(incumbent))incumbent=host.bootstrap();
        const auto receipt=host.prefix(incumbent);
        if(receipt.work)no_work_age=0;else ++no_work_age;
        if(!receipt.actual||!receipt.admissible||receipt.source<0)return receipt;
        // Admission precedes even obtaining audit inputs. 4K audit and 24K
        // normal tail are initial conservative measured-budget allowances.
        const auto before=host.fuel();
        if(before<28000){++low;return receipt;}
        const auto start=host.current_round();
        const int count=host.count();
        if(count<2)return receipt;
        int challenger=cursor_++%count;
        if(challenger==incumbent)challenger=cursor_++%count;
        ++audits;
        const auto s=host.cached_state();
        const auto old=host.cached_job(incumbent);
        const auto candidate=host.cached_job(challenger);
        const bool improve=candidate.affordable&&host.cached_available(challenger)
            &&policy.merit(s,candidate)>policy.merit(s,old)+.15;
        const auto after=host.fuel();
        const auto finish=host.current_round();
        if(finish!=start||start!=receipt.stamp.round||after>before){++invalid;return receipt;}
        maximum_audit=std::max(maximum_audit,before-after);
        if(improve) {
            proposal_={challenger,start,receipt.stamp.life,receipt.stamp.finance,true};
            ++published;
        }
        return receipt;
    }
};
}
