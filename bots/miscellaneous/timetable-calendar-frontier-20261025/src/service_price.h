#pragma once
// Restricted residual BASIC service, used to rank physical work rather than prescribe a
// gold/gem policy. Every member may be selected as primary: travel, attack reach and all
// repositioning are unpriced. This is not elapsed time, a survival proof or live camp state.
// The current primary is held until a member dies; splash deaths can end a chunk.
// This is a restricted policy, not per-basic target reselection.
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
enum class Status { Ready, Incomplete, Invalid, NeedCompute };
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

// Owned frozen inputs, with no SDK or action authority. One work unit is one
// member validation (including <=32 ID comparisons), opening-member update,
// death-event chunk (<=3*32 member visits), or phase/completion check. This is a
// structural work bound, not a measured WASM fuel bound. NeedCompute is never a
// completed price or evidence of physical infeasibility.
class Pricer {
public:
    static constexpr std::size_t max_work_units=3*capacity+2;
    Pricer(std::span<const Member> members,Body body,
           std::optional<OpeningArea> opening=std::nullopt):body_(body),opening_(opening) {
        if(!std::isfinite(body.basic_damage)||body.basic_damage<=0||body.splash<0) {
            finish(Status::Invalid);return;
        }
        if(members.size()>capacity)return;
        out_.count=members.size();
        // At most32 input copies; no allocation, validation scan or basic loop.
        std::copy(members.begin(),members.end(),members_.begin());
        out_.status=Status::NeedCompute;
        phase_=members.empty()?Phase::OpeningSetup:Phase::Validate;
    }
    Status advance(std::size_t units) {
        last_work_=0;
        while(last_work_<units && out_.status==Status::NeedCompute) {
            ++last_work_;++completed_units_;
            switch(phase_) {
            case Phase::Validate:validate_member();break;
            case Phase::OpeningSetup:setup_opening();break;
            case Phase::Opening:open_member();break;
            case Phase::Chunk:chunk();break;
            case Phase::Complete:
                finish(std::all_of(out_.residual.begin(),out_.residual.begin()+static_cast<std::ptrdiff_t>(out_.count),
                    [](const Member& member){return member.hp<=0;})?Status::Ready:Status::Incomplete);
                break;
            case Phase::Done:finish(Status::Incomplete);break;
            }
        }
        return out_.status;
    }
    Status status() const { return out_.status; }
    const Result& result() const { return out_; }
    std::size_t last_work() const { return last_work_; }
    std::size_t completed_units() const { return completed_units_; }
private:
    enum class Phase { Validate, OpeningSetup, Opening, Chunk, Complete, Done };
    std::array<Member,capacity> members_{};
    Body body_;
    std::optional<OpeningArea> opening_;
    Result out_;
    Phase phase_{Phase::Done};
    std::size_t cursor_{},events_{},last_work_{},completed_units_{};
    double initial_hp_{};
    void finish(Status status) { out_.status=status;phase_=Phase::Done; }
    void validate_member() {
        const auto& member=members_[cursor_];
        if(member.id<0||!std::isfinite(member.hp)||member.hp<0||
           !std::isfinite(member.strength)||member.strength<0) {
            finish(Status::Invalid);return;
        }
        for(std::size_t j=0;j<cursor_;++j)if(members_[j].id==member.id) {
            finish(Status::Invalid);return;
        }
        out_.exact_residual_inputs=out_.exact_residual_inputs&&member.hp_exact;
        out_.residual[cursor_]=member;
        initial_hp_+=member.hp;
        if(++cursor_==out_.count)phase_=Phase::OpeningSetup;
    }
    void setup_opening() {
        if(!std::isfinite(initial_hp_)){finish(Status::Incomplete);return;}
        if(opening_&&opening_->ready) {
            if(!std::isfinite(opening_->damage)||opening_->damage<0||opening_->radius<0) {
                finish(Status::Invalid);return;
            }
            out_.opening_used=true;cursor_=0;
            phase_=out_.count?Phase::Opening:Phase::Complete;
        } else phase_=out_.count?Phase::Chunk:Phase::Complete;
    }
    void open_member() {
        auto& member=out_.residual[cursor_];
        if(within(member.pos,opening_->center,opening_->radius)) {
            const double removed=std::min(member.hp,opening_->damage);
            member.hp-=removed;out_.opening_removed_hp+=removed;
        }
        if(++cursor_==out_.count) {
            out_.total_removed_hp=out_.opening_removed_hp;
            phase_=Phase::Chunk;
        }
    }
    void chunk() {
        auto& out=out_;
        const auto body=body_;
        // Each chunk kills >=1 affected member: <=N chunks, never one iteration
        // per projected basic, even for a very large HP bag.
        constexpr std::uint64_t max_exact_steps=std::uint64_t{1}<<53;
        std::size_t primary=out.count;
        double best_priority=-1,volley=0;
        for(std::size_t i=0;i<out.count;++i) {
            const auto& member=out.residual[i];
            if(member.hp<=0)continue;
            const double priority=member.strength/member.hp;
            if(!std::isfinite(priority)){finish(Status::Incomplete);return;}
            volley+=member.strength;
            if(priority>best_priority){best_priority=priority;primary=i;}
        }
        if(primary==out.count){finish(Status::Ready);return;}
        if(!std::isfinite(volley)){finish(Status::Incomplete);return;}
        std::array<std::uint64_t,capacity> to_death{};
        std::uint64_t repeat=max_exact_steps;
        const auto center=out.residual[primary].pos;
        for(std::size_t i=0;i<out.count;++i) {
            const auto& member=out.residual[i];
            if(member.hp<=0 || (i!=primary &&
               (body.splash==0||!within(member.pos,center,body.splash))))continue;
            const double hits=std::max(1.0,std::ceil(member.hp/body.basic_damage));
            if(!std::isfinite(hits)||hits>static_cast<double>(max_exact_steps)){finish(Status::Incomplete);return;}
            to_death[i]=static_cast<std::uint64_t>(hits);
            repeat=std::min(repeat,to_death[i]);
        }
        if(repeat>max_exact_steps-out.basic_attacks){finish(Status::Incomplete);return;}
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
        if(!removed_member){finish(Status::Incomplete);return;}
        // Camps were asleep before the first hit and act before the NEXT owner basic.
        // For q repeated hits, members killed by hit q contribute q-1 future volleys;
        // every survivor contributes q. Full surviving-source reach is assumed here.
        const double incoming=static_cast<double>(repeat)*volley-killed_strength;
        if(!std::isfinite(incoming)||incoming<0||!std::isfinite(out.projected_incoming+incoming)) {
            finish(Status::Incomplete);return;
        }
        out.projected_incoming+=incoming;
        out.basic_attacks+=repeat;
        ++out.chunks;
        if(++events_==out.count)phase_=Phase::Complete;
    }
};

inline Result price(std::span<const Member> members,Body body,
                    std::optional<OpeningArea> opening=std::nullopt) {
    Pricer pricer(members,body,opening);
    pricer.advance(Pricer::max_work_units);
    return pricer.result();
}

} // namespace mb::production::service
