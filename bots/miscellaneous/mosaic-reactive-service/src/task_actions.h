#pragma once
#include "transition.h"
#include <optional>

namespace mosaic::task_actions {
// A nomination is only a cheap progress estimate. The interpreter must certify
// the selected primitive with transition::evaluate before applying it.
struct Nominees {
    std::optional<transition::Command> progress, protection, movement;
    float progress_value{}, protection_value{};
};
// Camps occupy integer tiles. A radius-one usefulness sum is exactly a 3x3
// stencil, so target nomination needs nine sparse lookups rather than another
// scan of every task member for each possible center. The selected transition
// still performs its independent complete effect and survival certification.
struct TileIndex {
    struct Entry {int x{},y{},head{-1};std::uint32_t generation{};};
    std::array<Entry,512> entries{};
    std::array<int,160> next{};
    std::uint32_t generation{};
    bool complete{};
    static std::size_t hash(int x,int y) {
        return ((std::uint32_t(x)*73856093u)^(std::uint32_t(y)*19349663u))&511u;
    }
    template<class Board> void rebuild(const frame::Snapshot<Board>& f,const frame::Group& group) {
        complete=f.camps.size()<=next.size();if(!complete)return;
        if(++generation==0){for(auto& e:entries)e.generation=0;generation=1;}
        for(auto i:group.members) {
            if(i>=f.camps.size()){complete=false;return;}
            const auto p=f.camps[i].pos;auto slot=hash(p.x,p.y);
            while(entries[slot].generation==generation&&(entries[slot].x!=p.x||entries[slot].y!=p.y))slot=(slot+1)&511u;
            auto& entry=entries[slot];
            if(entry.generation!=generation){entry={p.x,p.y,-1,generation};}
            next[i]=entry.head;entry.head=int(i);
        }
    }
    int at(int x,int y) const {
        auto slot=hash(x,y);
        while(entries[slot].generation==generation) {
            if(entries[slot].x==x&&entries[slot].y==y)return entries[slot].head;
            slot=(slot+1)&511u;
        }
        return -1;
    }
};
template<class Board> int splash_count(const frame::Snapshot<Board>& f,const frame::Group& group,int radius) {
    if(radius<=0)return 1;
    static TileIndex tiles;tiles.rebuild(f,group);int best=1;
    for(auto i:group.members) {
        const auto& center=f.camps[i];if(center.known&&!center.alive)continue;
        int count=0;
        if(radius==1&&tiles.complete) {
            for(int y=center.pos.y-1;y<=center.pos.y+1;++y)for(int x=center.pos.x-1;x<=center.pos.x+1;++x)
                for(int j=tiles.at(x,y);j>=0;j=tiles.next[j]) {
                    const auto& c=f.camps[std::size_t(j)];if(!c.known||c.alive)++count;
                }
        } else for(auto j:group.members) {
            const auto& c=f.camps[j];if((!c.known||c.alive)&&transition::distance(center.pos,c.pos)<=radius)++count;
        }
        best=std::max(best,count);
    }
    return best;
}
template<class Board> bool plausible(const transition::PlanningContext<Board>& context,
                                    const transition::Command& cmd) {
    using namespace transition;
    const auto& f=*context.source;const auto& s=f.self;
    if(!context.current()||!f.complete||!s.alive||s.stunned||s.recalling||s.submitting||s.teleporting)return false;
    if(cmd.kind==Kind::move)return s.can_move&&contains(f.move_destinations,cmd.destination);
    if(cmd.kind==Kind::basic) {
        const auto* c=command_camp(f,cmd);
        return s.can_attack&&c&&c->visible&&c->known&&c->alive&&c->hp>0&&contains(f.attack_targets,c->pos);
    }
    if(cmd.kind!=Kind::cast&&cmd.kind!=Kind::reset)return false;
    const auto* a=ability(f.abilities,cmd.slot);
    if(!s.can_cast||s.cast_spent||!a||!a->ready||(a->exclusive&&(s.moved||!s.can_move)))return false;
    if(cmd.kind==Kind::reset)return a->reset&&!s.can_attack;
    if(cmd.target_kind==TargetKind::self)return a->self&&a->shield;
    const auto* c=command_camp(f,cmd);
    return c&&c->visible&&c->known&&c->alive&&c->hp>0&&a->camps&&
        contains(a->legal_targets,cmd.target_kind==TargetKind::ground?cmd.destination:c->pos);
}
template<class Board> Nominees nominate(transition::PlanningContext<Board>& context,
                                       const transition::Constraints& limits,
                                       const std::optional<transition::Command>& movement=std::nullopt) {
    using namespace transition;
    Nominees result;const auto& f=*context.source;const auto& s=f.self;
    if(!context.current()||!f.complete||!s.alive||s.stunned||s.recalling||s.submitting||s.teleporting)return result;
    if(movement&&plausible(context,*movement))result.movement=movement;
    const auto* group=f.group(limits.group);if(!group)return result;
    // Private per-VM scratch. Every live entry is overwritten under a new
    // generation; neither source snapshots nor decisions depend on old values.
    static TileIndex tiles;tiles.rebuild(f,*group);
    auto retain=[&](Command cmd,float value) {
        if(value>0&&(!result.progress||value>result.progress_value)) {
            result.progress=cmd;result.progress_value=value;
        }
    };
    auto utility=[&](const frame::Camp& primary,float hit,int radius) {
        float value=0;
        // Only current-task usefulness is ranked. Exact certification below
        // retains every collateral group and every possible incoming member.
        if(radius==0)return std::min(primary.hp,hit);
        if(radius==1&&tiles.complete) {
            for(int y=primary.pos.y-1;y<=primary.pos.y+1;++y)for(int x=primary.pos.x-1;x<=primary.pos.x+1;++x)
                for(int i=tiles.at(x,y);i>=0;i=tiles.next[i]) {
                    const auto& c=f.camps[std::size_t(i)];
                    if(c.known&&c.alive&&c.hp>0)value+=std::min(c.hp,hit);
                }
            return value;
        }
        for(auto index:group->members) {
            const auto& c=f.camps[index];
            if(c.known&&c.alive&&c.hp>0&&distance(primary.pos,c.pos)<=radius)value+=std::min(c.hp,hit);
        }
        return value;
    };
    const float hit=basic_damage(s);float enabled_basic=0;
    std::array<bool,2> offensive{};
    for(std::size_t slot=0;slot<f.abilities.size();++slot) {
        const auto& a=f.abilities[slot];
        const bool ready=s.can_cast&&!s.cast_spent&&a.ready&&(!a.exclusive||(!s.moved&&s.can_move));
        offensive[slot]=ready&&!a.reset&&!a.shield&&a.camps&&(a.damage||a.aoe||a.cc);
        if(ready&&a.self&&a.shield&&(!result.protection||a.shield_amount>result.protection_value)) {
            auto cmd=command(f,Kind::cast);cmd.slot=a.slot;cmd.target_kind=TargetKind::self;
            result.protection=cmd;result.protection_value=a.shield_amount;
        }
    }
    // Chebyshev range is exactly how SDK attack_tiles()/valid_tiles() are
    // constructed. Do not repeatedly linear-search their exported tile lists.
    for(auto index:group->members) {
        const auto& c=f.camps[index];if(!c.visible||!c.known||!c.alive||c.hp<=0)continue;
        const int range=distance(s.pos,c.pos);
        if(range<=s.range) {
            const float value=utility(c,hit,std::max(0,splash_radius(s)));enabled_basic=std::max(enabled_basic,value);
            if(s.can_attack) {
                auto cmd=command(f,Kind::basic);cmd.target_kind=TargetKind::camp;cmd.target_id=c.handle;cmd.target_index=int(index);
                retain(cmd,value);
            }
        }
        for(std::size_t slot=0;slot<f.abilities.size();++slot)if(offensive[slot]) {
            const auto& a=f.abilities[slot];if(float(range)>a.range)continue;
            auto cmd=command(f,Kind::cast);cmd.slot=a.slot;cmd.target_id=c.handle;cmd.target_index=int(index);
            cmd.target_kind=a.ground?TargetKind::ground:TargetKind::camp;cmd.destination=c.pos;
            retain(cmd,utility(c,a.damage_amount,a.aoe?int(a.radius):0));
        }
    }
    if(!s.can_attack&&enabled_basic>0)for(const auto& a:f.abilities)
        if(s.can_cast&&!s.cast_spent&&a.ready&&a.reset&&(!a.exclusive||(!s.moved&&s.can_move))) {
            auto cmd=command(f,Kind::reset);cmd.slot=a.slot;retain(cmd,enabled_basic);
        }
    return result;
}
} // namespace mosaic::task_actions
