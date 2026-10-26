#pragma once
#include "../../common/src/events.h"
#include <cstdint>

namespace p11_capital {
struct Input {
    float strength=0,maximum=0,strength_growth=0,health_growth=0;
    float monster_bonus=.1f,jungle_reduction=.3f;
    int gold=0,monster_count=0,jungle_count=0,monster_price=30,jungle_price=30;
    int monster_required=5,jungle_required=5;
};
struct Job { std::array<p11::Monster,8> monsters{};int count=0; };
enum class Project { none,monster,jungle };
struct Purchase { Project project=Project::none;int count=0; };

namespace detail {
struct Value {
    int feasible=0,work=0;
    std::int64_t deficit=0;
    int cost=0;
};
struct WorkResult {
    int work=0,attack_calls=0;
    bool reused=false;
};
// clear_schedule already summed every included member's integer hits. Reuse
// that exact work without moving any floating exposure arithmetic. Unsupported
// inputs keep the original work loop, including its NaN/invalid-damage rules.
inline WorkResult work_from_clear(std::span<const p11::Monster> monsters,
        float damage,const p11::Clear&clear) {
    auto original=[&] {
        WorkResult result;
        for(const auto&monster:monsters) {
            result.work+=p11::attacks(monster.hp,damage);++result.attack_calls;
        }
        return result;
    };
    if(monsters.size()>8||!clear.bounded||!std::isfinite(damage)||damage<=0)return original();
    std::int64_t approach=0;int alive=0;
    for(const auto&monster:monsters) {
        if(!std::isfinite(monster.hp)||!std::isfinite(monster.strength))return original();
        if(monster.hp<=0)continue;
        if(monster.approach<0)return original();
        approach+=monster.approach;++alive;
    }
    const std::int64_t maximum_work=std::int64_t(alive)*10000;
    // Nonnegative bounded approaches also protect every intermediate sum in
    // the supplied clear schedule, independently of its weighted kill order.
    if(approach>std::numeric_limits<int>::max()-maximum_work)return original();
    const std::int64_t work=std::int64_t(clear.turns)-approach;
    if(work<alive||work>maximum_work)return original();
    return {int(work),0,true};
}
inline bool better(const Value&a,const Value&b) {
    if(a.feasible!=b.feasible)return a.feasible>b.feasible;
    if(a.work!=b.work)return a.work<b.work;
    if(a.deficit!=b.deficit)return a.deficit<b.deficit;
    return a.cost<b.cost;
}
inline Value value(const Input&in,std::span<const Job> jobs,Purchase purchase,int cost) {
    const int monster=purchase.project==Project::monster?purchase.count:0;
    const int jungle=purchase.project==Project::jungle?purchase.count:0;
    const float damage=(in.strength+monster*in.strength_growth)
        *(in.monster_count+monster>=in.monster_required?1+in.monster_bonus:1);
    const float maximum=in.maximum+jungle*in.health_growth;
    const float mitigation=in.jungle_count+jungle>=in.jungle_required?1-in.jungle_reduction:1;
    Value result;result.cost=cost;
    for(std::size_t i=0;i<std::min(jobs.size(),std::size_t(3));++i) {
        const auto&job=jobs[i];
        if(job.count<=0||job.count>8)continue;
        const auto monsters=std::span<const p11::Monster>(job.monsters.data(),job.count);
        const auto clear=p11::clear_schedule(monsters,damage,mitigation);
        const float loss=p11::ordered_exposure(monsters,clear,mitigation);
        result.feasible+=loss<=.75f*maximum;
        // Compare actual integer basic work. Approach offsets remain in the
        // exposure model, but cannot turn raw DPS into fictitious saved hits.
        result.work+=work_from_clear(monsters,damage,clear).work;
        const double missing=std::ceil(std::max(0.,double(loss)/.75-double(maximum)));
        constexpr auto cap=std::numeric_limits<std::int64_t>::max()/4;
        result.deficit+=missing>=double(cap)?cap:std::int64_t(missing);
    }
    return result;
}
}

inline Purchase choose_capital(const Input&in,std::span<const Job> jobs) {
    // Capital measures a later departure at full health. In particular it
    // promises no healing from Tavi's field Health purchases; the live caller
    // retains current health, recovery and retreat decisions.
    Purchase chosen;
    auto best=detail::value(in,jobs,chosen,0);
    for(const auto project:{Project::monster,Project::jungle}) {
        const int owned=project==Project::monster?in.monster_count:in.jungle_count;
        const int required=project==Project::monster?in.monster_required:in.jungle_required;
        const int price=project==Project::monster?in.monster_price:in.jungle_price;
        if(price<0||in.gold<0)continue;
        const int remaining=std::clamp(required-owned,0,5);
        for(int count=1;count<=remaining;++count) {
            if(price>0&&count>in.gold/price)break;
            const int cost=count*price;
            const Purchase candidate{project,count};
            const auto next=detail::value(in,jobs,candidate,cost);
            // Waiting has cost zero and wins exact ties. No bundle is bought
            // without a strict lexicographic gain over the no-purchase option.
            if(detail::better(next,best)){best=next;chosen=candidate;}
        }
    }
    return chosen;
}
}
