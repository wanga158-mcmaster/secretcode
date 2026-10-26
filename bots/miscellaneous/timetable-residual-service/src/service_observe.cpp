#include "service_observe.h"
#include "ctx.h"
#include "world.h"
#include <algorithm>
#include <array>
#include <type_traits>
#include <variant>

namespace mb::service {
namespace {

class RuntimeObservation
{
public:
    RuntimeObservation(Ctx& ctx, int group)
        : ctx_{ctx}, world_{World::get()}, group_{group}, info_{world_.groups[static_cast<std::size_t>(group)]}
    {
        for (auto& camp : ctx_.camps)
            if (camp.group==group_ && camp.member>=0 && static_cast<std::size_t>(camp.member)<max_members)
                views_[static_cast<std::size_t>(camp.member)]=&camp;
    }

    int round() const { return static_cast<int>(cambc::round_number()); }
    std::size_t member_count() const { return info_.tiles.size(); }

    bool enough_fuel(std::size_t queries, bool) const
    {
        std::size_t harmful=0;
        for (auto type : info_.types) harmful+=world_.camp_stats[type].strength>0.0f;
        // Workload reserves, not a proved native/allocator charge bound. They include
        // fresh masks, own state, group availability and a complete abort/yield suffix.
        const std::uint64_t need=7000u+180u*member_count()+200u*queries+
            400u*std::min(queries,harmful);
        return fuel_ok(need);
    }

    void prepare()
    {
        const auto visible=cambc::vision::visible_tiles();
        const auto living=cambc::vision::camp_tiles();
        const auto awake=cambc::vision::awake_camp_tiles();
        for (std::size_t i=0;i<member_count();++i)
        {
            const auto tile=info_.tiles[i];
            visible_[i]=visible.is_set(tile);
            living_[i]=visible_[i] && living.is_set(tile);
            awake_[i]=living_[i] && awake.is_set(tile);
        }
    }

    void body(WorkFrame& frame, bool moved_this_turn)
    {
        auto& me=cambc::self();
        auto& body=frame.state.body;
        const auto position=me.position();
        body.pos={position.x,position.y};
        body.hp=me.health();
        body.max_hp=me.max_health();
        body.strength=me.strength();
        body.focus=me.focus();
        body.attack_range=me.attack_range();
        body.ranged=kit().ranged;
        body.splash=kit().splash;
        body.camp_damage_multiplier=ctx_.camp_damage_taken_mult;
        body.moved_this_turn=moved_this_turn;
        current_state_=me.state();
        body.can_move=current_state_.contains(cambc::champion_state::can_move);
        body.can_attack=current_state_.contains(cambc::champion_state::can_attack);
        body.can_cast=current_state_.contains(cambc::champion_state::can_cast);
        body.next_basic_stun=0;
        body.shield=0.0f;
        frame.projection_limits=0;
        frame.state.width=world_.width;
        frame.state.height=world_.height;
        const auto buffs=me.buffs();
        if (buffs.has_shield()) body.shield=buffs.get<cambc::buff_shield>().value;
        if (buffs.has_enhanced_attack()) frame.projection_limits|=EnhancedAttackEffect;
        if (me.passive()==cambc::passive_type::missing_health_damage)
            frame.projection_limits|=BasicOutgoingPassive;
        // strength() is the actual queried stat, not an invented outgoing multiplier.
        // The missing-health passive is explicitly outside the exact basic projection.
        const auto abilities=me.abilities();
        for (std::size_t slot=0;slot<spell_slots;++slot)
        {
            auto& spell=frame.state.spells[slot];
            spell=Spell{};
            frame.cooldowns[slot]=-1;
            const auto& ability=abilities[slot];
            const auto& metadata=ability.info();
            if (!metadata) continue;
            frame.cooldowns[slot]=ability.cooldown();
            std::visit([&](const auto& typed) {
                using T=std::decay_t<decltype(typed)>;
                spell.exclusive=typed.exclusive_with_movement;
                if constexpr (std::is_same_v<T,cambc::ability_attack_reset>)
                {
                    spell.kind=SpellKind::reset;
                    // The enhanced buff lifetime is not its next-basic stun duration.
                    if (typed.enhanced_attack) frame.projection_limits|=EnhancedAttackEffect;
                }
                else if constexpr (std::is_same_v<T,cambc::ability_shield>)
                {
                    if (typed.valid_targets.contains(cambc::ability_target_mask::self))
                    {
                        spell.kind=SpellKind::shield;
                        spell.range=static_cast<int>(typed.range);
                        spell.duration=typed.duration;
                        spell.damage=ability.shield_amount();
                    }
                }
                else if constexpr (std::is_same_v<T,cambc::ability_damage> ||
                                   std::is_same_v<T,cambc::ability_aoe_damage> ||
                                   std::is_same_v<T,cambc::ability_cc>)
                {
                    if (typed.valid_targets.contains(cambc::ability_target_mask::camps))
                    {
                        spell.range=static_cast<int>(typed.range);
                        spell.damage=ability.damage_amount();
                        if constexpr (std::is_same_v<T,cambc::ability_damage>) spell.kind=SpellKind::damage;
                        else if constexpr (std::is_same_v<T,cambc::ability_cc>)
                        {
                            spell.kind=SpellKind::cc;
                            spell.duration=typed.duration;
                        }
                        else
                        {
                            spell.kind=SpellKind::aoe;
                            spell.radius=static_cast<int>(typed.radius);
                            spell.duration=typed.duration;
                            if (typed.duration>0) frame.projection_limits|=PersistentDamage;
                        }
                    }
                }
            },*metadata);
            if (spell.kind==SpellKind::none) frame.projection_limits|=UnsupportedSpell;
            spell.ready=spell.kind!=SpellKind::none && frame.cooldowns[slot]==0 && body.can_cast &&
                !(spell.exclusive && moved_this_turn);
        }
    }

    Member design(std::size_t i) const
    {
        const auto& stats=world_.camp_stats[info_.types[i]];
        const auto position=info_.tiles[i];
        Member member;
        member.id=static_cast<int>(i);
        member.group=group_;
        member.pos={position.x,position.y};
        member.hp=stats.health;
        member.strength=stats.strength;
        member.range=stats.range;
        return member;
    }
    bool visible(std::size_t i) const { return visible_[i]; }
    bool living(std::size_t i) const { return living_[i]; }
    bool awake(std::size_t i) const { return awake_[i]; }
    float health(std::size_t i) const { return world_.camp(group_,static_cast<int>(i)).health(); }
    int stun(std::size_t i) const
    {
        const auto buffs=world_.camp(group_,static_cast<int>(i)).buffs();
        return buffs.has_stun() ? buffs.get<cambc::buff_stun>().duration : 0;
    }
    observation_detail::AvailabilityObservation availability() const
    {
        const auto actual=world_.group_objects[static_cast<std::size_t>(group_)].state();
        if (std::holds_alternative<cambc::map::camp_group_alive>(actual)) return {Availability::Alive,-1};
        if (std::holds_alternative<cambc::map::camp_group_reward_available>(actual)) return {Availability::Reward,-1};
        return {Availability::Respawning,std::get<cambc::map::camp_group_respawning>(actual).timer};
    }
    bool incoming_complete(WorkFrame& frame) const
    {
        bool complete=ctx_.n_unseen==0 && !ctx_.hit_unseen;
        int splash_radius=frame.state.body.splash ? 1 : 0;
        for (const auto& spell : frame.state.spells)
            if (spell.kind==SpellKind::aoe) splash_radius=std::max(splash_radius,spell.radius);
        for (const auto& camp : ctx_.camps)
        {
            if (camp.group==group_ || camp.hp<=0.0f) continue;
            const int dx=std::max({info_.min_x-camp.pos.x,camp.pos.x-info_.max_x,0});
            const int dy=std::max({info_.min_y-camp.pos.y,camp.pos.y-info_.max_y,0});
            const bool collateral=splash_radius>0 && std::max(dx,dy)<=splash_radius &&
                world_.groups[static_cast<std::size_t>(camp.group)].total_strength>0.0f;
            const bool awake_source=camp.awake && camp.strength>0.0f &&
                distance(frame.state.body.pos,{camp.pos.x,camp.pos.y})<=camp.range;
            if (collateral || awake_source) complete=false;
        }
        if (!complete) frame.projection_limits|=ExternalCampThreats;
        return complete;
    }
    void commit(const WorkFrame& frame)
    {
        for (std::size_t i=0;i<frame.state.member_count;++i)
            if (views_[i] && frame.state.members[i].hp_exact)
            {
                views_[i]->hp=frame.state.members[i].hp;
                views_[i]->awake=frame.state.members[i].awake;
            }
        ctx_.pos={frame.state.body.pos.x,frame.state.body.pos.y};
        ctx_.hp=frame.state.body.hp;
        ctx_.max_hp=frame.state.body.max_hp;
        ctx_.strength=frame.state.body.strength;
        ctx_.focus=frame.state.body.focus;
        ctx_.state=current_state_;
    }

private:
    Ctx& ctx_;
    World& world_;
    int group_;
    const GroupInfo& info_;
    std::array<bool,max_members> visible_{},living_{},awake_{};
    std::array<CampView*,max_members> views_{};
    cambc::champion_state current_state_{};
};

} // namespace

WorkFrame observe_work(Ctx& ctx, int group, bool moved_this_turn)
{
    // Capture the epoch before any work-frame query or adapter preparation.
    const int epoch=static_cast<int>(cambc::round_number());
    auto& world=World::get();
    if (epoch!=ctx.round)
    {
        WorkFrame frame; frame.epoch=epoch; frame.group=group; frame.status=FrameStatus::Stale; return frame;
    }
    if (group<0 || static_cast<std::size_t>(group)>=world.groups.size())
    {
        WorkFrame frame; frame.epoch=epoch; frame.group=group; return frame;
    }
    RuntimeObservation backend{ctx,group};
    return observation_detail::collect_work_frame(backend,ctx.round,group,moved_this_turn);
}

FrameStatus refresh_work(Ctx& ctx, WorkFrame& frame, const Primitive& primitive, bool moved_this_turn)
{
    const int epoch=static_cast<int>(cambc::round_number());
    if (epoch!=ctx.round || epoch!=frame.epoch) return frame.status=FrameStatus::Stale;
    auto& world=World::get();
    if (frame.group<0 || static_cast<std::size_t>(frame.group)>=world.groups.size())
        return frame.status=FrameStatus::Incomplete;
    RuntimeObservation backend{ctx,frame.group};
    return observation_detail::refresh_work_frame(backend,frame,ctx.round,primitive,moved_this_turn);
}

} // namespace mb::service
