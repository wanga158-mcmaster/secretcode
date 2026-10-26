#pragma once
// Restricted residual BASIC service, used to rank physical work rather than prescribe a
// gold/gem policy. Every member may be selected as primary: travel, attack reach and all
// repositioning are unpriced. This is not elapsed time, a survival proof or live camp state.
// No resets, shields, stuns, mitigation, allies, enemy champions, respawns or payout are
// simulated. Incoming is raw full-group strength on the declared basic-event clock only.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace mb::production::service {

inline constexpr std::size_t capacity=32;
struct Point { int x{},y{}; };
struct Member {
    int id{-1};
    Point pos;
    double hp{},strength{};
    bool hp_exact{true};
};
struct Body { double basic_damage{}; int splash{}; };
// A ready opening area is applied before the first basic in the SAME model owner turn.
// There is no intervening camp volley. Cast legality, exclusivity and availability are
// caller obligations; this optional projection cannot authorize an actual cast.
struct OpeningArea { bool ready{}; Point center; double damage{}; int radius{}; };
enum class Status { Ready, Incomplete, Invalid };
struct Result {
    Status status{Status::Incomplete};
    std::uint64_t basic_attacks{};
    double total_removed_hp{},first_basic_removed_hp{},opening_removed_hp{},projected_incoming{};
    bool exact_residual_inputs{true},spatial_service_unmodeled{true},opening_used{};
    std::size_t count{},chunks{};
    std::array<Member,capacity> residual{};
};

inline bool within(Point a,Point b,int radius) {
    const auto dx=static_cast<std::int64_t>(a.x)-b.x;
    const auto dy=static_cast<std::int64_t>(a.y)-b.y;
    return dx>=-static_cast<std::int64_t>(radius) && dx<=radius &&
           dy>=-static_cast<std::int64_t>(radius) && dy<=radius;
}

inline Result price(std::span<const Member> members,Body body,
                    std::optional<OpeningArea> opening=std::nullopt) {
    Result out;
    if(!std::isfinite(body.basic_damage)||body.basic_damage<=0||body.splash<0) {
        out.status=Status::Invalid;return out;
    }
    if(members.size()>capacity)return out;
    out.count=members.size();
    double initial_hp=0;
    for(std::size_t i=0;i<members.size();++i) {
        const auto& member=members[i];
        if(member.id<0||!std::isfinite(member.hp)||member.hp<0||
           !std::isfinite(member.strength)||member.strength<0) {
            out.status=Status::Invalid;return out;
        }
        for(std::size_t j=0;j<i;++j)if(members[j].id==member.id) {
            out.status=Status::Invalid;return out;
        }
        out.exact_residual_inputs=out.exact_residual_inputs&&member.hp_exact;
        out.residual[i]=member;
        initial_hp+=member.hp;
    }
    if(!std::isfinite(initial_hp))return out;
    if(opening&&opening->ready) {
        if(!std::isfinite(opening->damage)||opening->damage<0||opening->radius<0) {
            out.status=Status::Invalid;return out;
        }
        out.opening_used=true;
        for(std::size_t i=0;i<out.count;++i) {
            auto& member=out.residual[i];
            if(within(member.pos,opening->center,opening->radius)) {
                const double removed=std::min(member.hp,opening->damage);
                member.hp-=removed;out.opening_removed_hp+=removed;
            }
        }
        out.total_removed_hp=out.opening_removed_hp;
    }

    // Each chunk ends at a death of at least one affected member, hence at most N
    // chunks. No loop iterates once per projected basic, even for a very large HP bag.
    constexpr std::uint64_t max_exact_steps=std::uint64_t{1}<<53;
    for(std::size_t event=0;event<out.count;++event) {
        std::size_t primary=out.count;
        double best_priority=-1,volley=0;
        for(std::size_t i=0;i<out.count;++i) {
            const auto& member=out.residual[i];
            if(member.hp<=0)continue;
            const double priority=member.strength/member.hp;
            if(!std::isfinite(priority))return out;
            volley+=member.strength;
            if(priority>best_priority){best_priority=priority;primary=i;}
        }
        if(primary==out.count){out.status=Status::Ready;return out;}
        if(!std::isfinite(volley))return out;
        std::array<std::uint64_t,capacity> to_death{};
        std::uint64_t repeat=max_exact_steps;
        const auto center=out.residual[primary].pos;
        for(std::size_t i=0;i<out.count;++i) {
            const auto& member=out.residual[i];
            if(member.hp<=0 || (i!=primary &&
               (body.splash==0||!within(member.pos,center,body.splash))))continue;
            const double hits=std::max(1.0,std::ceil(member.hp/body.basic_damage));
            if(!std::isfinite(hits)||hits>static_cast<double>(max_exact_steps))return out;
            to_death[i]=static_cast<std::uint64_t>(hits);
            repeat=std::min(repeat,to_death[i]);
        }
        if(repeat>max_exact_steps-out.basic_attacks)return out;
        double killed_strength=0;
        bool removed_member=false;
        for(std::size_t i=0;i<out.count;++i) {
            auto& member=out.residual[i];
            if(to_death[i]==0)continue;
            if(out.basic_attacks==0)out.first_basic_removed_hp+=std::min(member.hp,body.basic_damage);
            const double before=member.hp;
            if(to_death[i]==repeat) {
                member.hp=0;killed_strength+=member.strength;removed_member=true;
            } else member.hp=std::max(0.0,member.hp-body.basic_damage*static_cast<double>(repeat));
            out.total_removed_hp+=before-member.hp;
        }
        if(!removed_member)return out;
        // Camps were asleep before the first hit and act before the NEXT owner basic.
        // For q repeated hits, members killed by hit q contribute q-1 future volleys;
        // every survivor contributes q. Full surviving-source reach is assumed here.
        const double incoming=static_cast<double>(repeat)*volley-killed_strength;
        if(!std::isfinite(incoming)||incoming<0||!std::isfinite(out.projected_incoming+incoming))return out;
        out.projected_incoming+=incoming;
        out.basic_attacks+=repeat;
        ++out.chunks;
    }
    const bool empty=std::all_of(out.residual.begin(),out.residual.begin()+static_cast<std::ptrdiff_t>(out.count),
                               [](const Member& member){return member.hp<=0;});
    if(empty)out.status=Status::Ready;
    return out;
}

} // namespace mb::production::service
