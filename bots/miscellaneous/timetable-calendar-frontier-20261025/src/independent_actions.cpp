#ifndef TT_ACTIONS_FIXTURE
#include "runtime.h"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include "contact_words.h"
#ifndef TT_ACTIONS_FIXTURE
#include "ordinary_step.h"
#endif

namespace tt::actions_detail {
using mb::service::Primitive;
using mb::service::PrimitiveKind;
using mb::service::SpellKind;
// A global packet may omit the live buff query. Unknown stun never removes an
// incoming source; only an explicitly exact positive remaining stun is credited.
inline bool stun_protects(bool exact,int remaining) { return exact && remaining>0; }
struct Target { bool visible{},living{}; vec2 pos; float hp{}; };
struct CastInfo
{
    SpellKind kind{SpellKind::none};
    bool ready{},self{},camps{},ground{},exclusive{};
    float range{};
};

// Shared by the SDK adapter and the focused caller fixture. Every observation made
// during this guard is followed by the final epoch/origin check before the action.
template<class Backend>
Status guarded_apply(Observation& observation,const WorkObservation& work,const Primitive& primitive,
                     Applied& applied,Backend& backend)
{
    // Fresh active WorkObservation can authorize this one command after an earlier
    // command invalidated the global snapshot. It does not publish old world data
    // as Ready; current owner epoch, origin and live SDK guards still bind below.
    const bool active_observation=observation.status==Status::Ready ||
        observation.status==Status::NeedObservation;
    if (!active_observation || work.status!=Status::Ready)
        return Status::NeedObservation;
    const int epoch=backend.epoch();
    if (epoch!=observation.epoch || epoch!=work.epoch) return Status::Stale;
    const vec2 origin=observation.pos;
    if (work.state.body.pos.x!=origin.x || work.state.body.pos.y!=origin.y ||
        backend.position()!=origin) return Status::NeedObservation;
    if (!backend.budget()) return Status::NeedCompute;
    if (!backend.alive() || backend.channeling()) return Status::NeedObservation;
    const auto fresh=[&] {
        return backend.budget() && backend.position()==origin && backend.epoch()==epoch;
    };
    const mb::service::Member* member=nullptr;
    if (work.state.member_count>mb::service::max_members) return Status::NeedObservation;
    if (primitive.member_id>=0)
        for (std::size_t i=0;i<work.state.member_count;++i)
            if (work.state.members[i].id==primitive.member_id) member=&work.state.members[i];
    const auto target_is_fresh=[&] {
        if (!member || !member->hp_exact || member->hp<=0.0f) return false;
        const auto target=backend.target(member->group,member->id);
        return target.visible && target.living && target.hp==member->hp &&
            target.pos.x==member->pos.x && target.pos.y==member->pos.y;
    };
    if (primitive.kind==PrimitiveKind::basic)
    {
        if (!backend.can_attack() || !target_is_fresh()) return Status::NeedObservation;
        const vec2 target{member->pos.x,member->pos.y};
        if (tt::distance(origin,target)>backend.attack_range()) return Status::NeedObservation;
        if (!fresh()) return backend.epoch()!=epoch ? Status::Stale : Status::NeedObservation;
        backend.attack(member->group,member->id);
        applied.attacked=true;
    }
    else
    {
        if (primitive.slot<0 || primitive.slot>=static_cast<int>(mb::service::spell_slots) ||
            !backend.can_cast()) return Status::NeedObservation;
        const auto cast=backend.cast_info(primitive.slot);
        if (!cast.ready || (cast.exclusive && (applied.moved || work.state.body.moved_this_turn)))
            return Status::NeedObservation;
        const vec2 aim{primitive.aim.x,primitive.aim.y};
        if (cast.kind==SpellKind::reset)
        {
            if (!cast.self || backend.can_attack()) return Status::NeedObservation;
        }
        else if (cast.kind==SpellKind::shield)
        {
            if (!cast.self) return Status::NeedObservation;
        }
        else if (cast.kind==SpellKind::aoe)
        {
            if (!cast.ground || !backend.inside(aim) || tt::distance(origin,aim)>cast.range)
                return Status::NeedObservation;
        }
        else if (cast.kind==SpellKind::damage || cast.kind==SpellKind::cc)
        {
            if (!cast.camps || !target_is_fresh() || aim.x!=member->pos.x || aim.y!=member->pos.y ||
                tt::distance(origin,aim)>cast.range) return Status::NeedObservation;
        }
        else return Status::NeedObservation;
        if (!fresh()) return backend.epoch()!=epoch ? Status::Stale : Status::NeedObservation;
        if (!backend.cast(primitive.slot,cast.kind,member ? member->group : -1,
                          member ? member->id : -1,aim)) return Status::NeedObservation;
        applied.cast=true;
    }
    observation.status=Status::NeedObservation;
    return Status::Ready;
}
} // namespace tt::actions_detail

#ifndef TT_ACTIONS_FIXTURE
namespace tt {
namespace {
using mb::service::SpellKind;
struct SDKActions
{
    std::array<cambc::ability,2> abilities=cambc::self().abilities();
    int epoch() const { return static_cast<int>(cambc::round_number()); }
    bool budget() const { return tt::budget(0); }
    vec2 position() const { return cambc::self().position(); }
    bool alive() const { return cambc::self().is_alive(); }
    bool channeling() const
    { const auto s=cambc::self().state(); return s.contains(cambc::champion_state::recalling) ||
        s.contains(cambc::champion_state::submitting_gems) || s.contains(cambc::champion_state::teleporting); }
    bool can_attack() const { return cambc::self().can_attack(); }
    bool can_cast() const { return cambc::self().can_cast(); }
    int attack_range() const { return cambc::self().attack_range(); }
    bool inside(vec2 p) const
    { const auto& w=World::get(); return p.x>=0 && p.y>=0 && p.x<w.width && p.y<w.height; }
    actions_detail::Target target(int group,int member) const
    {
        const auto& w=World::get();
        if (group<0 || group>=static_cast<int>(w.groups.size()) || member<0 ||
            member>=static_cast<int>(w.groups[static_cast<std::size_t>(group)].members.size())) return {};
        const auto& camp=w.camp(group,member);
        if (!camp.is_visible()) return {};
        const bool living=camp.state().is_alive();
        return {true,living,camp.position(),living ? camp.health() : 0.0f};
    }
    actions_detail::CastInfo cast_info(int slot) const
    {
        const auto& ability=abilities[static_cast<std::size_t>(slot)];
        if (!ability.info()) return {};
        actions_detail::CastInfo result;
        result.ready=ability.is_ready();
        const auto mask=ability.valid_targets();
        result.self=mask.contains(cambc::ability_target_mask::self);
        result.camps=mask.contains(cambc::ability_target_mask::camps);
        result.ground=mask.contains(cambc::ability_target_mask::ground);
        std::visit([&](const auto& data) {
            result.exclusive=data.exclusive_with_movement;
            if constexpr (requires { data.range; }) result.range=data.range;
        },*ability.info());
        if (ability.is_attack_reset()) result.kind=SpellKind::reset;
        else if (ability.is_shield()) result.kind=SpellKind::shield;
        else if (ability.is_damage()) result.kind=SpellKind::damage;
        else if (ability.is_cc()) result.kind=SpellKind::cc;
        else if (ability.is_aoe_damage()) result.kind=SpellKind::aoe;
        return result;
    }
    void attack(int group,int member) const { cambc::self().attack(World::get().camp(group,member)); }
    bool cast(int slot,SpellKind kind,int group,int member,vec2 aim) const
    {
        const auto& ability=abilities[static_cast<std::size_t>(slot)];
        if (kind==SpellKind::reset) return ability.use();
        if (kind==SpellKind::shield) return ability.use(cambc::self());
        if (kind==SpellKind::aoe) return ability.use(aim);
        return ability.use(World::get().camp(group,member));
    }
};
bool fresh(const Observation& observation)
{
    return budget(0) && cambc::self().position()==observation.pos && current(observation.epoch);
}
bool splash_near_camp(vec2 p)
{
    const auto& w=World::get();
    for (int y=p.y-1;y<=p.y+1;++y) for (int x=p.x-1;x<=p.x+1;++x)
        if (x>=0 && y>=0 && x<w.width && y<w.height && w.camp_tiles.is_set({x,y})) return true;
    return false;
}
struct ThreatSpell { float range{},damage{}; bool control{}; };
struct Threat { vec2 pos; float strength{}; int range{}; std::array<ThreatSpell,2> spells{}; };
float exposure(vec2 p,const std::vector<Threat>& threats,const Observation& observation,bool include_camps=true)
{
    float result=0;
    for (const auto& threat:threats)
    {
        const int d=distance(p,threat.pos);
        if (d<=threat.range+1) result+=threat.strength;
        float cast=0;
        for (const auto& spell:threat.spells) if (d<=spell.range) cast=std::max(cast,spell.damage);
        result+=cast;
    }
    if (include_camps) for (const auto& camp:observation.camps)
        if (camp.awake && !actions_detail::stun_protects(camp.stun_exact,camp.stun) &&
            distance(p,camp.pos)<=camp.range)
            result+=camp.strength*observation.damage_multiplier;
    return result;
}
bool enemy_reach(vec2 p,const std::vector<Threat>& threats)
{
    for (const auto& threat:threats)
    {
        const int d=distance(p,threat.pos);
        if (threat.strength>0.0f && d<=threat.range+1) return true;
        for (const auto& spell:threat.spells)
            if ((spell.damage>0.0f || spell.control) && d<=spell.range) return true;
    }
    return false;
}

ContactHistory last_contact_history;
namespace cw=contact_words;
vec2 tile(cw::Point p) { return {p.x,p.y}; }
cw::Point point(vec2 p) { return {p.x,p.y}; }
struct ContactFoe
{
    int index{}; vec2 pos; float hp{},strength{},cast{}; int range{};
    float reach{},movement{2}; bool reset{},control{};
};
struct ContactSpell
{
    cw::Kind kind{cw::Kind::stop}; bool ready{},exclusive{},ground{},enemies{},self{};
    float range{},radius{},damage{},benefit{}; int duration{};
};

// This is a bounded visible-contact class, not an enemy game-tree solver or an
// escape certificate. Its response score deliberately prices two visible enemy
// opportunities with refreshed ordinary allowances and cooldowns. SELF resets
// add their ordered extra basics; a dash enlarges subsequent basic/spell reach.
// Hidden enemies, forced displacement, kill-refresh, future purchases/consumables
// and longer reply words remain outside its qualification. No survival admission
// or guaranteed escape is inferred from this score.
struct ContactBackend
{
    Observation& observation;
    cambc::champion& actor=cambc::self();
    std::vector<cambc::champion> enemies;
    std::array<ContactFoe,8> foes{}; std::size_t count{};
    std::array<ContactSpell,2> spells{};
    cambc::bitboard blocked=cambc::map::walls()|cambc::map::altars()|
        cambc::map::spawn_region(cambc::team::enemy);
    cambc::bitboard champions,camps,teleports;
    float hp{},strength{}; int reach{}; bool ranged{},splash{},flee{},contact{};
    explicit ContactBackend(Observation& value):observation(value) {}
    static int identity(const cambc::champion& enemy)
    { return static_cast<int>(static_cast<cambc::handle_champion_t>(enemy)); }
    const cambc::champion* target(int handle) const
    { for (const auto& enemy:enemies) if (identity(enemy)==handle) return &enemy; return nullptr; }
    int epoch() const { return static_cast<int>(cambc::round_number()); }
    // Leave the controller's ~8K bookkeeping plus the standard 14K tail, with
    // another 4K for the final bounded import/command block.
    bool budget() const { return tt::budget(12000); }
    cw::Point position() const { return point(actor.position()); }
    bool inside(vec2 p) const
    { const auto& w=World::get(); return p.x>=0 && p.y>=0 && p.x<w.width && p.y<w.height; }
    bool camp_collateral(vec2 p,float radius) const
    {
        const auto& w=World::get(); const int r=static_cast<int>(std::ceil(radius));
        for (int y=p.y-r;y<=p.y+r;++y) for (int x=p.x-r;x<=p.x+r;++x)
            if (inside({x,y}) && distance(p,{x,y})<=radius && w.camp_tiles.is_set({x,y})) return true;
        return false;
    }
    float response(vec2 p) const
    {
        float result=0;
        for (std::size_t i=0;i<count;++i)
        {
            const auto& foe=foes[i]; const int d=distance(p,foe.pos);
            result+=cw::visible_reply_score(2,d<=foe.range+foe.movement ? foe.strength : 0,
                foe.reset,d<=foe.reach ? foe.cast : 0);
        }
        return result;
    }
    int separation(vec2 p) const
    { int sum=0; for (std::size_t i=0;i<count;++i) sum+=distance(p,foes[i].pos); return sum; }
    bool vacant(vec2 p,bool landing,bool live) const
    {
        if (!inside(p) || blocked.is_set(p) || (landing && World::get().terrain.is_set(p))) return false;
        return live ? (!cambc::vision::has_champion(p) && !cambc::vision::has_camp(p) &&
            !cambc::vision::has_teleport(p)) :
            (!champions.is_set(p) && !camps.is_set(p) && !teleports.is_set(p));
    }
    bool dash_path(vec2 from,vec2 end,bool live) const
    {
        // Range >2 requires a different bounded path proof. Interior holes are
        // permitted; every interior wall/altar/spawn/unit still blocks.
        return cw::two_step_dash_path(point(from),point(end),
            [&](cw::Point p,bool landing) { return vacant(tile(p),landing,live); });
    }
    cw::Frame refresh()
    {
        const int now=epoch(); const auto pos=actor.position(); const auto state=actor.state();
        cw::Frame frame{now,point(pos),now==observation.epoch && state.contains(cambc::champion_state::alive),
            state.contains(cambc::champion_state::recalling) || state.contains(cambc::champion_state::submitting_gems) ||
            state.contains(cambc::champion_state::teleporting),state.contains(cambc::champion_state::can_move),
            state.contains(cambc::champion_state::can_attack),state.contains(cambc::champion_state::can_cast)};
        count=0; contact=false; flee=false;
        if (!budget() || !frame.live || frame.channeling) return frame;
        hp=actor.health(); strength=actor.strength(); reach=actor.attack_range(); ranged=reach>1;
        splash=observation.splash;
        enemies=cambc::vision::champions(cambc::team::enemy);
        if (enemies.size()>foes.size()) { frame.live=false; return frame; }
        for (std::size_t index=0;index<enemies.size();++index)
        {
            const auto& enemy=enemies[index];
            if (!budget() || !enemy.is_visible() || !enemy.is_alive()) continue;
            ContactFoe foe;
            foe.index=static_cast<int>(index); foe.pos=enemy.position(); foe.hp=enemy.health();
            foe.strength=enemy.strength(); foe.range=enemy.attack_range();
            // Up to two future actor opportunities may occur before the next
            // usable own input. Their ordinary allowances refresh even when the
            // presently visible C/M/A flags are spent; small cooldowns advance.
            for (const auto& ability:enemy.abilities())
            {
                if (!ability.info() || !cw::available_within_two_inputs(ability.cooldown())) continue;
                // Reset targets SELF but enables another enemy basic. Filtering
                // it by can_target_enemies would miss the C22 burst word.
                if (ability.is_attack_reset())
                {
                    foe.reset=true;
                    foe.control|=ability.get<cambc::ability_attack_reset>().enhanced_attack;
                    continue;
                }
                if (!(ability.can_target_enemies() || ability.can_target_ground())) continue;
                if (!(ability.is_damage() || ability.is_cc() || ability.is_aoe_damage() || ability.is_dash())) continue;
                float r=0;
                std::visit([&](const auto& data) {
                    if constexpr (requires { data.range; }) r=data.range;
                    if constexpr (requires { data.radius; }) r+=data.radius;
                    if constexpr (requires { data.duration; }) foe.control|=data.duration>0;
                },*ability.info());
                if (ability.is_dash()) foe.movement=std::max(foe.movement,2+ability.get<cambc::ability_dash>().range);
                foe.reach=std::max(foe.reach,r);
                foe.cast=std::max(foe.cast,std::max(ability.damage_amount(),ability.damage_amount(actor)));
            }
            foe.reach+=foe.movement;
            contact|=distance(pos,foe.pos)<=foe.range+foe.movement ||
                ((foe.cast>0 || foe.control) && distance(pos,foe.pos)<=foe.reach);
            foes[count++]=foe;
        }
        spells={};
        const auto abilities=actor.abilities();
        for (std::size_t i=0;i<spells.size();++i)
        {
            if (!budget()) return frame;
            const auto& ability=abilities[i]; auto& spell=spells[i];
            if (!ability.info()) continue;
            spell.ready=ability.is_ready(); spell.ground=ability.can_target_ground();
            spell.enemies=ability.can_target_enemies(); spell.self=ability.can_target_self();
            std::visit([&](const auto& data) {
                spell.exclusive=data.exclusive_with_movement;
                if constexpr (requires { data.range; }) spell.range=data.range;
                if constexpr (requires { data.radius; }) spell.radius=data.radius;
                if constexpr (requires { data.duration; }) spell.duration=data.duration;
            },*ability.info());
            if (ability.is_attack_reset()) spell.kind=cw::Kind::reset;
            else if (ability.is_shield()) { spell.kind=cw::Kind::shield; spell.benefit=ability.shield_amount(actor); }
            else if (ability.is_heal()) { spell.kind=cw::Kind::heal; spell.benefit=ability.heal_amount(actor); }
            else if (ability.is_dash()) spell.kind=cw::Kind::dash;
            else if (ability.is_aoe_damage()) spell.kind=cw::Kind::ground_cast;
            else if (ability.is_damage() || ability.is_cc()) spell.kind=cw::Kind::unit_cast;
            if (ability.is_damage() || ability.is_cc() || ability.is_aoe_damage() || ability.is_dash())
                spell.damage=ability.damage_amount();
        }
        if (!budget()) return frame;
        cambc::vision::champion_tiles_into(champions);
        if (!budget()) return frame;
        cambc::vision::camp_tiles_into(camps);
        if (!budget()) return frame;
        cambc::vision::teleport_tiles_into(teleports);
        flee=contact && (actor.gem_count()>0 || hp<=response(pos));
        return frame;
    }
    bool same_input(const cw::Frame& f) const
    { return budget() && epoch()==f.epoch && actor.position()==tile(f.pos); }
    bool ignored(const cw::Action& action,const std::array<cw::Action,12>& refused,std::size_t n) const
    { for (std::size_t i=0;i<n;++i) if (action==refused[i]) return true; return false; }
    cw::Action nominate(const cw::Frame& frame,const cw::History& history,
                        const std::array<cw::Action,12>& refused,std::size_t n)
    {
        cw::Action best; float best_score=-1;
        const auto offer=[&](cw::Action action,float score) {
            if (cw::permitted(action,frame,history) && !ignored(action,refused,n) && score>best_score)
            { best=action; best_score=score; }
        };
        const vec2 origin=tile(frame.pos);
        int basic=-1; float basic_value=0;
        for (std::size_t i=0;i<count;++i) if (distance(origin,foes[i].pos)<=reach &&
            (!splash || !camp_collateral(foes[i].pos,1)) && (contact || foes[i].hp<=strength))
        {
            const float value=std::min(strength,foes[i].hp);
            if (basic<0 || value>basic_value) { basic=static_cast<int>(i); basic_value=value; }
        }
        // Melee basics preserve walking. Ranged retreat walks first, then may
        // reset and basic with M still spent; the shared history forbids erasure.
        if (basic>=0 && !(flee && ranged && frame.move))
            offer({cw::Kind::basic,-1,identity(enemies[static_cast<std::size_t>(foes[static_cast<std::size_t>(basic)].index)]),
                point(foes[static_cast<std::size_t>(basic)].pos)},100000+basic_value);
        if (flee && frame.move && !history.walked)
            for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx)
            {
                const vec2 end{origin.x+dx,origin.y+dy};
                if (!(dx || dy) || !contact_immutable_quiet(end) || !vacant(end,true,false)) continue;
                const float gain=response(origin)-response(end); const int apart=separation(end)-separation(origin);
                if (gain>0 || (gain==0 && apart>0))
                    offer({cw::Kind::move,-1,-1,point(end)},90000+gain+apart*0.01f);
            }
        const auto abilities=actor.abilities();
        for (std::size_t slot=0;slot<spells.size();++slot)
        {
            const auto& spell=spells[slot]; const auto& ability=abilities[slot];
            if (!spell.ready || (spell.exclusive && history.walked)) continue;
            const auto action=[&](cw::Kind kind,int target,vec2 aim) {
                return cw::Action{kind,static_cast<int>(slot),target,point(aim),spell.exclusive};
            };
            if (spell.kind==cw::Kind::reset && spell.self && basic>=0 && !frame.attack)
                offer(action(cw::Kind::reset,-1,origin),100000+basic_value);
            else if ((spell.kind==cw::Kind::shield || spell.kind==cw::Kind::heal) && spell.self &&
                flee && spell.benefit>0 && (spell.kind==cw::Kind::heal || response(origin)>0) &&
                (!spell.exclusive || !frame.move))
                offer(action(spell.kind,-1,origin),70000+std::min(spell.benefit,response(origin)));
            else if (spell.kind==cw::Kind::unit_cast && spell.enemies)
            {
                for (std::size_t i=0;i<count;++i) if (distance(origin,foes[i].pos)<=spell.range &&
                    (contact || spell.damage>=foes[i].hp))
                    offer(action(spell.kind,identity(enemies[static_cast<std::size_t>(foes[i].index)]),foes[i].pos),80000+
                        ability.damage_amount(enemies[static_cast<std::size_t>(foes[i].index)]));
            }
            else if (spell.kind==cw::Kind::ground_cast && spell.ground)
            {
                for (std::size_t i=0;i<count;++i)
                {
                    // A bounded set of legal centers includes edge-of-range
                    // splash hits. It is not exhaustive optimal AoE placement.
                    vec2 aim=foes[i].pos;
                    const int r=static_cast<int>(std::floor(spell.range));
                    aim.x=std::clamp(aim.x,origin.x-r,origin.x+r);
                    aim.y=std::clamp(aim.y,origin.y-r,origin.y+r);
                    if (!inside(aim) || camp_collateral(aim,spell.radius)) continue;
                    float value=0; int hits=0;
                    for (std::size_t j=0;j<count;++j) if (distance(aim,foes[j].pos)<=spell.radius)
                    { value+=ability.damage_amount(enemies[static_cast<std::size_t>(foes[j].index)]); ++hits; }
                    if (hits && (contact || value>=foes[i].hp))
                        offer(action(spell.kind,-1,aim),80000+value+static_cast<float>(hits*spell.duration));
                }
            }
            else if (spell.kind==cw::Kind::dash && spell.ground && flee && spell.range<=2)
            {
                for (int dy=-2;dy<=2;++dy) for (int dx=-2;dx<=2;++dx)
                {
                    const vec2 end{origin.x+dx,origin.y+dy};
                    if (distance(origin,end)>spell.range || !contact_immutable_quiet(end) ||
                        ((spell.damage>0 || spell.duration>0) && camp_collateral(end,spell.radius)) ||
                        !dash_path(origin,end,false)) continue;
                    const float gain=response(origin)-response(end); const int apart=separation(end)-separation(origin);
                    if (gain>0 || (gain==0 && apart>0))
                        offer(action(spell.kind,-1,end),60000+gain+apart*0.01f);
                }
            }
        }
        return best;
    }
    bool validate(const cw::Action& a,const cw::Frame& f)
    {
        const vec2 origin=tile(f.pos),aim=tile(a.aim);
        if (a.kind==cw::Kind::move) return distance(origin,aim)==1 && contact_immutable_quiet(aim) && ordinary_step_may_attempt(aim);
        if (a.kind==cw::Kind::basic || a.kind==cw::Kind::unit_cast)
        {
            const auto* selected=target(a.target);
            if (!selected) return false;
            const auto& enemy=*selected;
            if (!enemy.is_visible() || !enemy.is_alive() || enemy.position()!=aim) return false;
            if (a.kind==cw::Kind::basic)
                return actor.can_attack() && distance(origin,aim)<=actor.attack_range() &&
                    (!splash || !camp_collateral(aim,1));
        }
        const auto ability=actor.abilities()[static_cast<std::size_t>(a.slot)];
        if (!ability.info() || !ability.is_ready()) return false;
        bool exclusive=false; float range=0,radius=0; int duration=0;
        std::visit([&](const auto& data) {
            exclusive=data.exclusive_with_movement;
            if constexpr (requires { data.range; }) range=data.range;
            if constexpr (requires { data.radius; }) radius=data.radius;
            if constexpr (requires { data.duration; }) duration=data.duration;
        },*ability.info());
        cw::Kind actual=cw::Kind::stop;
        if (ability.is_attack_reset()) actual=cw::Kind::reset;
        else if (ability.is_shield()) actual=cw::Kind::shield;
        else if (ability.is_heal()) actual=cw::Kind::heal;
        else if (ability.is_dash()) actual=cw::Kind::dash;
        else if (ability.is_aoe_damage()) actual=cw::Kind::ground_cast;
        else if (ability.is_damage() || ability.is_cc()) actual=cw::Kind::unit_cast;
        if (!cw::cast_guard(a,f,{actual,true,exclusive,ability.can_target_self(),
            ability.can_target_enemies(),ability.can_target_ground(),range})) return false;
        if (a.kind==cw::Kind::reset) return ability.is_attack_reset() && ability.can_target_self() && !actor.can_attack();
        if (a.kind==cw::Kind::shield) return ability.is_shield() && ability.can_target_self();
        if (a.kind==cw::Kind::heal) return ability.is_heal() && ability.can_target_self();
        if (distance(origin,aim)>range) return false;
        if (a.kind==cw::Kind::unit_cast) return (ability.is_damage() || ability.is_cc()) && ability.can_target_enemies();
        if (a.kind==cw::Kind::ground_cast) return ability.is_aoe_damage() && ability.can_target_ground() &&
            inside(aim) && !camp_collateral(aim,radius);
        if (a.kind==cw::Kind::dash) return ability.is_dash() && ability.can_target_ground() && range<=2 &&
            contact_immutable_quiet(aim) && (!(ability.damage_amount()>0 || duration>0) || !camp_collateral(aim,radius)) &&
            dash_path(origin,aim,true);
        return false;
    }
    bool apply(const cw::Action& a)
    {
        if (a.kind==cw::Kind::move) return actor.move_absolute(tile(a.aim));
        if (a.kind==cw::Kind::basic) { const auto* enemy=target(a.target); if (!enemy) return false; actor.attack(*enemy); return true; }
        const auto ability=actor.abilities()[static_cast<std::size_t>(a.slot)];
        if (a.kind==cw::Kind::reset) return ability.use();
        if (a.kind==cw::Kind::shield || a.kind==cw::Kind::heal) return ability.use(actor);
        if (a.kind==cw::Kind::unit_cast) { const auto* enemy=target(a.target); return enemy && ability.use(*enemy); }
        return ability.use(tile(a.aim));
    }
};
[[maybe_unused]] bool contact_service(Observation& observation,Applied& applied)
{
    ContactBackend backend{observation};
    cw::History history;
    history.walked=applied.moved || observation.service_body.moved_this_turn;
    history.cast=applied.cast;
    const auto result=cw::execute(backend,history);
    for (std::size_t i=0;i<result.count;++i)
    {
        const auto& record=result.records[i];
        if (record.action.kind==cw::Kind::move)
        {
            applied.moved=true; last_contact_history.walked=true;
            last_contact_history.walk_from=tile(record.from); last_contact_history.walk_to=tile(record.to);
        }
        else if (record.action.kind==cw::Kind::basic) applied.attacked=true;
        else { applied.cast=true; if (record.action.kind==cw::Kind::dash) last_contact_history.dashed=true; }
    }
    if (!current(observation.epoch)) { observation.status=Status::Stale; return true; }
    if (result.count || result.interrupted)
    {
        observation.pos=cambc::self().position(); observation.status=Status::NeedObservation;
        return true;
    }
    return backend.flee;
}
} // namespace

const ContactHistory& contact_history() { return last_contact_history; }

Status apply_command(Observation& observation,const WorkObservation& work,
                     const mb::service::Primitive& primitive,Applied& applied)
{
    SDKActions backend;
    const auto status=actions_detail::guarded_apply(observation,work,primitive,applied,backend);
    if (status!=Status::Ready) observation.status=status;
    return status;
}

Status purchase(Observation& observation)
{
    if (!current(observation.epoch)) return Status::Stale;
    if (!budget(0)) return Status::NeedCompute;
    const auto& self=cambc::self();
    if (self.is_alive() && self.position()!=observation.pos) return Status::NeedObservation;
    const bool focus=self.champion_name()==cambc::champion_type::ryth;
    using I=cambc::item_type;
    const std::array<I,5> shards=focus ? std::array<I,5>{I::monster_shard_of_focus,I::jungle_shard_of_focus,
        I::durability_shard_of_focus,I::treasure_shard_of_focus,I::power_shard_of_focus} :
        std::array<I,5>{I::monster_shard_of_strength,I::jungle_shard_of_strength,
        I::durability_shard_of_strength,I::treasure_shard_of_strength,I::power_shard_of_strength};
    const auto counts=self.item_count();
    for (std::size_t i=0;i<shards.size();++i)
    {
        const auto shard=shards[i];
        if (!budget(0)) return Status::NeedCompute;
        const auto result=shard.result();
        if (!result || (i+1<shards.size() && counts[*result]>0) ||
            counts[*result]==std::numeric_limits<std::uint32_t>::max()) continue;
        const auto info=shard.info();
        const auto gold=self.gold_count();
        if (!info.purchasable || !info.cost || !info.combine_count || counts[shard]>=info.combine_count) continue;
        const auto count=std::min(gold/info.cost,info.combine_count-counts[shard]);
        if (!count || count>std::numeric_limits<std::uint32_t>::max()-counts[shard] || !self.can_buy(shard,count))
            return Status::Ready;
        if (!current(observation.epoch) || (self.is_alive() && self.position()!=observation.pos)) return Status::Stale;
        if (!budget(0)) return Status::NeedCompute;
        self.buy(shard,count);
        observation.status=Status::NeedObservation;
        return Status::NeedObservation;
    }
    return Status::Ready;
}

bool service_threat(Observation& observation,Applied& applied)
{
    if constexpr (cfg::contact_service)
    {
        last_contact_history={}; last_contact_history.epoch=observation.epoch;
        if (!budget(12000)) { observation.status=Status::NeedCompute; return true; }
        if (observation.status==Status::Ready && fresh(observation) && !observation.channeling &&
            !observation.enemies.empty() && observation.enemies.size()<=8 &&
            contact_immutable_quiet(observation.pos))
            return contact_service(observation,applied);
        // Outside the quiet-origin bounded class, preserve the existing handler.
    }
    if (observation.status!=Status::Ready || !fresh(observation)) return true;
    if (observation.enemies.empty() || observation.channeling) return false;
    const auto& self=cambc::self();
    std::vector<Threat> threats;
    threats.reserve(observation.enemies.size());
    for (const auto& seen:observation.enemies)
    {
        if (!budget(0) || !current(observation.epoch)) return true;
        const auto& enemy=seen.handle;
        if (!enemy.is_visible() || !enemy.is_alive()) continue;
        Threat threat{enemy.position(),enemy.strength(),enemy.attack_range(),{}};
        const auto abilities=enemy.abilities();
        for (std::size_t slot=0;slot<abilities.size();++slot)
        {
            const auto& ability=abilities[slot];
            if (!ability.info() || ability.cooldown()>0 || !ability.can_target_enemies()) continue;
            if (!(ability.is_damage() || ability.is_cc() || ability.is_aoe_damage() || ability.is_dash())) continue;
            float reach=0;
            std::visit([&](const auto& data) {
                if constexpr (requires { data.range; }) reach=data.range+(data.exclusive_with_movement ? 0.0f : 1.0f);
                if constexpr (requires { data.radius; }) reach+=data.radius;
            },*ability.info());
            threat.spells[slot]={reach,ability.damage_amount(self),ability.is_cc() || ability.is_dash() || ability.is_aoe_damage()};
        }
        threats.push_back(threat);
    }
    if (!fresh(observation)) return true;
    const float danger=exposure(observation.pos,threats,observation,false);
    const bool flee=enemy_reach(observation.pos,threats) && (self.gem_count()>0 || self.health()<=danger);
    if (flee && self.can_move())
    {
        auto best=observation.pos;
        float best_damage=exposure(best,threats,observation);
        int best_distance=0;
        for (const auto& threat:threats) best_distance+=distance(best,threat.pos);
        for (const auto step:legal_steps(observation))
        {
            if (!budget(0)) return true;
            const float damage=exposure(step,threats,observation);
            int separation=0;
            for (const auto& threat:threats) separation+=distance(step,threat.pos);
            if (damage<best_damage || (damage==best_damage && separation>best_distance))
            { best=step; best_damage=damage; best_distance=separation; }
        }
        const auto previous=observation.pos;
        if (best!=observation.pos && fresh(observation) && move_absolute(observation,best))
        {
            if constexpr (cfg::contact_service)
            { last_contact_history.walked=true; last_contact_history.walk_from=previous;
              last_contact_history.walk_to=best; }
            applied.moved=true; observation.status=Status::NeedObservation; return true;
        }
    }
    for (const auto& seen:observation.enemies)
    {
        if (!budget(0) || !fresh(observation)) return true;
        const auto& enemy=seen.handle;
        if (!enemy.is_visible() || !enemy.is_alive()) continue;
        const auto target=enemy.position();
        const auto hp=enemy.health();
        const int d=distance(observation.pos,target);
        if (self.can_attack() && d<=self.attack_range() && (flee || danger>0 || hp<=self.strength()) &&
            (!observation.splash || !splash_near_camp(target)))
        {
            if (!fresh(observation)) return true;
            self.attack(enemy); applied.attacked=true; observation.status=Status::NeedObservation; return true;
        }
        const auto abilities=self.abilities();
        for (const auto& ability:abilities)
        {
            if (!budget(0) || !fresh(observation)) return true;
            if (!ability.info() || !ability.is_ready()) continue;
            bool exclusive=false;
            float range=0;
            std::visit([&](const auto& data) {
                exclusive=data.exclusive_with_movement;
                if constexpr (requires { data.range; }) range=data.range;
            },*ability.info());
            if (exclusive && applied.moved) continue;
            if (flee && ability.is_shield() && ability.can_target_self())
            {
                if (!fresh(observation)) return true;
                if (ability.use(self)) { applied.cast=true; observation.status=Status::NeedObservation; return true; }
            }
            else if ((ability.is_damage() || ability.is_cc()) && ability.can_target_enemies() && d<=range &&
                (danger>0 || ability.damage_amount(enemy)>=hp))
            {
                if (!enemy.is_visible() || !enemy.is_alive() || enemy.position()!=target || !fresh(observation)) return true;
                if (ability.use(enemy)) { applied.cast=true; observation.status=Status::NeedObservation; return true; }
            }
        }
    }
    return flee;
}
} // namespace tt
#endif
