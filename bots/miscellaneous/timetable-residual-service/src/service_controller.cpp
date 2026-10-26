#include "brain.h"
#include "config.h"
#include "knowledge.h"
#include "nav.h"
#include "service_observe.h"
#include "service_execution.h"
#include "service_price.h"
#include "shop.h"
#include "world.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace mb {
namespace {

enum class Operator { Acquire, Service, Capture, Bank, Recover };
enum class Planning { Ready, NeedCompute, Unreachable, Stale };
struct Residual
{
    std::array<float,service::max_members> hp{};
    std::array<bool,service::max_members> exact{};
    int epoch{-1};
};
struct ServiceRuntime
{
    Operator op{Operator::Acquire};
    int group{-1}, resume{-1}, route_group{-2}, route_kind{-1};
    int planned_epoch{-1}, logs{}, last_work{-1};
    bool needs_plan{}, previous_quiet{true};
    std::unique_ptr<cambc::distance_field> route;
    std::vector<Residual> residual;
    std::vector<int> rejected_until;
    std::vector<vec2> front_members;
};
ServiceRuntime& runtime() { static ServiceRuntime state; return state; }
bool epoch(int round) { return static_cast<int>(cambc::round_number())==round; }
bool inside(const World& world, vec2 tile)
{ return tile.x>=0 && tile.y>=0 && tile.x<world.width && tile.y<world.height; }

struct ExitCoverage
{
    Planning status{Planning::NeedCompute};
    float reserve{};
};

// Bound every possible member in the actual command family's wake envelope. A ground
// aim may be offset by its radius from a target, so an external member can be two radii
// from that target. Discovery uses immutable map membership, not personal visibility.
ExitCoverage service_exit(Ctx& ctx, int group, TurnActions* acts=nullptr, vec2 origin={-1,-1})
{
    if (!fuel_ok(cfg::fuel_safety_field+17000)) return {};
    const int round=ctx.round;
    auto& world=World::get();
    const vec2 from=origin.x>=0 ? origin : ctx.pos;
    struct PossibleSource { int group,member; vec2 tile; bool alive,awake; };
    std::vector<PossibleSource> possible;
    possible.reserve(160);
    for (std::size_t g=0;g<world.groups.size();++g)
        for (std::size_t m=0;m<world.groups[g].tiles.size();++m)
        {
            const auto tile=world.groups[g].tiles[m];
            bool alive=true;
            bool awake=false;
            if (ctx.visible.is_set(tile))
            {
                alive=false;
                for (const auto& camp : ctx.camps)
                    if (camp.pos==tile && camp.hp>0) { alive=true; awake=camp.awake; break; }
            }
            possible.push_back({static_cast<int>(g),static_cast<int>(m),tile,alive,awake});
        }
    int radius=kit().splash ? 1 : 0;
    for (const auto& spell : kit().slot)
        if (spell.camps && spell.kind==model::AbilityKind::aoe)
            radius=std::max(radius,2*static_cast<int>(spell.radius));
    std::vector<unsigned char> collateral(world.groups.size(),0);
    if (group>=0 && radius>0)
    {
        const auto& target=world.groups[static_cast<std::size_t>(group)];
        for (std::size_t g=0;g<world.groups.size();++g)
        {
            const auto& info=world.groups[g];
            const int dx=std::max({info.min_x-target.max_x,target.min_x-info.max_x,0});
            const int dy=std::max({info.min_y-target.max_y,target.min_y-info.max_y,0});
            // Bounding rectangles are an overapproximation of possible hit members.
            // False positives charge extra exposure; no actual footprint is excluded.
            collateral[g]=std::max(dx,dy)<=radius;
        }
    }
    cambc::bitboard threatened;
    float rate=0.0f;
    for (const auto& member : possible)
    {
        if (!member.alive) continue;
        const auto tile=member.tile;
        const auto& info=world.groups[static_cast<std::size_t>(member.group)];
        const auto& design=world.camp_stats[info.types[static_cast<std::size_t>(member.member)]];
        if (design.strength<=0) continue;
        if (member.group==group || collateral[static_cast<std::size_t>(member.group)] || member.awake)
        { threatened.draw_chebyshev(tile,static_cast<float>(design.range)); rate+=design.strength; }
    }
    for (int i=0;i<ctx.n_unseen;++i)
    {
        threatened.draw_chebyshev(ctx.unseen[i].pos,static_cast<float>(ctx.unseen[i].range));
        rate+=ctx.unseen[i].strength; // duplicate source counting is conservative
    }
    if (!epoch(round)) return {Planning::Stale,0};
    if (!threatened.is_set(from)) return {Planning::Ready,0};
    if (!fuel_ok(cfg::fuel_safety_field+7000)) return {};
    auto walls=blocked_now();
    walls.set(ctx.pos,false);
    cambc::bitboard visited;
    cambc::distance_field field;
    field.compute(walls,~(walls|threatened),visited,static_cast<std::uint16_t>(cfg::safety_field_radius));
    const auto distance=field.get(from);
    if (!epoch(round)) return {Planning::Stale,0};
    // A bounded miss is incomplete geometry, never a claim of permanent unreachability.
    if (distance==cambc::distance_field::unreachable) return {Planning::NeedCompute,0};
    if (acts && from==ctx.pos && distance>0 && cambc::self().can_move() && step_along(field))
    { acts->moved=true; ctx.refresh_self(); }
    return {Planning::Ready,rate*ctx.camp_damage_taken_mult*(distance+1)};
}

service::State observed_service(const service::WorkFrame& frame)
{
    auto state=frame.state;
    state.member_count=0;
    for (std::size_t i=0;i<frame.state.member_count;++i)
        if (frame.state.members[i].hp_exact)
            state.members[state.member_count++]=frame.state.members[i];
    // Omitted members are not declared dead. Their possible next and egress volleys
    // are charged independently by service_exit, including whole-group hidden siblings.
    state.incoming_complete=true;
    return state;
}

void discard_route(ServiceRuntime& state)
{
    state.route.reset();
    state.route_group=-2;
    state.route_kind=-1;
    state.needs_plan=true;
}

// This owns only a static graph search. No command is issued on this planning turn.
// A refused move or displacement may invalidate executability without refuting this graph.
Planning plan_route(ServiceRuntime& state, int round, vec2 from)
{
    auto& world=World::get();
    if (!epoch(round)) return Planning::Stale;
    const std::uint64_t need=world.field_cost_at(from)+9000;
    if (!fuel_ok(need)) return Planning::NeedCompute;
    cambc::bitboard walls=world.blocked | world.camp_spawns;
    cambc::bitboard roots;
    walls.set(from,false);
    for (std::size_t g=0;g<world.groups.size();++g)
        if (Knowledge::get().groups[g].status!=GroupStatus::alive)
            for (auto tile : world.groups[g].tiles) walls.set(tile,false);
    if (state.op==Operator::Bank)
    {
        for (auto altar : world.altars)
            roots.draw_chebyshev(altar,static_cast<float>(cambc::constants::submit_gems_range));
        // Cleared altar gates are real landing tiles, never permanent camp walls.
        if (world.gate_group>=0 && Knowledge::get().groups[world.gate_group].status!=GroupStatus::alive)
            for (auto tile : world.gate_tiles) walls.set(tile,false);
    }
    else if (state.group>=0)
    {
        const auto& info=world.groups[static_cast<std::size_t>(state.group)];
        if (state.op==Operator::Capture)
            for (auto tile : info.tiles) { walls.set(tile,false); roots.set(tile); }
        else
            for (auto tile : state.front_members)
                roots.draw_chebyshev(tile,static_cast<float>(kit().attack_range));
    }
    else return Planning::Unreachable;
    roots &= ~walls;
    cambc::bitboard visited;
    auto field=std::make_unique<cambc::distance_field>();
    // Search only as far as this origin, then retain the certified descent for later turns.
    // A cap miss is not used: unreachable here means the completed reachable component.
    visited.set(from);
    field->compute(walls,roots,visited);
    const auto distance=field->get(from);
    if (!epoch(round)) return Planning::Stale;
    if (distance==cambc::distance_field::unreachable) return Planning::Unreachable;
    state.route=std::move(field);
    state.route_group=state.group;
    state.route_kind=static_cast<int>(state.op);
    state.planned_epoch=round;
    state.needs_plan=false;
    return Planning::Ready;
}

void retain_frame(ServiceRuntime& state, const service::WorkFrame& frame)
{
    auto& saved=state.residual[static_cast<std::size_t>(frame.group)];
    for (std::size_t i=0;i<frame.state.member_count;++i)
        if (frame.state.members[i].hp_exact)
        {
            saved.hp[i]=frame.state.members[i].hp;
            saved.exact[i]=true;
        }
    saved.epoch=frame.epoch;
}

// Conditional service prices rank acquisition offers. They never authorize a command,
// establish elapsed time or turn gold into a universal exchange rate for gems.
float offer(const Ctx& ctx, int group, const ServiceRuntime& state)
{
    auto& world=World::get();
    const auto& info=world.groups[static_cast<std::size_t>(group)];
    const auto& belief=Knowledge::get().groups[static_cast<std::size_t>(group)];
    const int travel=cheb(ctx.pos,info.center);
    if (belief.status==GroupStatus::respawning && belief.respawn_round>ctx.round+travel)
        return -1.0f;
    if (belief.status==GroupStatus::reward)
        return (static_cast<float>(info.gold)+ctx.gem_weight*info.gems)/(travel+cambc::constants::capture_time+1);
    namespace price=production::service;
    std::array<price::Member,service::max_members> members{};
    if (info.tiles.size()>members.size()) return -1.0f;
    const auto& residual=state.residual[static_cast<std::size_t>(group)];
    for (std::size_t i=0;i<info.tiles.size();++i)
    {
        const auto& design=world.camp_stats[info.types[i]];
        // Retain exact partial work only while the observed generation is still alive.
        // An unobserved respawn cannot inherit dead members or partial health.
        const bool retained=residual.epoch>=0 && belief.status==GroupStatus::alive &&
            residual.epoch>=belief.seen_round && residual.exact[i];
        members[i]={static_cast<int>(i),{info.tiles[i].x,info.tiles[i].y},
            retained ? residual.hp[i] : design.health,design.strength,retained};
    }
    const auto projection=price::price(std::span<const price::Member>(members.data(),info.tiles.size()),
        {std::max(1.0f,ctx.strength),kit().splash ? 1 : 0});
    if (projection.status!=price::Status::Ready) return -1.0f;
    const float work=static_cast<float>(projection.basic_attacks);
    // Recovery is a real omitted service interval in the initial offer. This conditional
    // estimate charges a full recall/heal/return excursion per health stock consumed.
    const float damage=static_cast<float>(projection.projected_incoming)*ctx.camp_damage_taken_mult;
    const float recoveries=damage/std::max(100.0f,ctx.max_hp*0.7f);
    const float recovery=recoveries*(cambc::constants::recall_time+travel+2);
    const float time=travel+work+cambc::constants::capture_time+recovery+1;
    const float reward=static_cast<float>(info.gold)+ctx.gem_weight*info.gems;
    // Ally intent is an acquisition opportunity cost, never evidence of physical completion.
    return reward/(time*(1.0f+Knowledge::get().committed(group)));
}

bool execute_primitive(Ctx& ctx, TurnActions& acts, const service::WorkFrame& frame,
                       const service::Primitive& action)
{
    if (!epoch(ctx.round) || frame.epoch!=ctx.round) return false;
    auto& me=cambc::self();
    if (me.position()!=vec2{frame.state.body.pos.x,frame.state.body.pos.y}) return false;
    if (action.kind==service::PrimitiveKind::basic)
    {
        if (action.member_id<0 || static_cast<std::size_t>(action.member_id)>=frame.state.member_count || !me.can_attack()) return false;
        const auto& member=frame.state.members[static_cast<std::size_t>(action.member_id)];
        const auto tile=vec2{member.pos.x,member.pos.y};
        if (!member.hp_exact || member.hp<=0 || cheb(ctx.pos,tile)>kit().attack_range ||
            !cambc::vision::in_vision(tile)) return false;
        const auto& camp=World::get().camp(frame.group,action.member_id);
        if (!camp.is_alive()) return false;
        me.attack(camp);
        acts.attacked=true;
    }
    else
    {
        if (action.slot<0 || action.slot>=2 || !me.can_cast()) return false;
        const auto& projected=frame.state.spells[static_cast<std::size_t>(action.slot)];
        const auto ability=me.abilities()[static_cast<std::size_t>(action.slot)];
        if (ability.cooldown()!=0 || (projected.exclusive && acts.moved)) return false;
        if (projected.kind==service::SpellKind::reset || projected.kind==service::SpellKind::shield)
            ability.use();
        else if (projected.kind==service::SpellKind::aoe)
            ability.use(vec2{action.aim.x,action.aim.y});
        else
        {
            if (action.member_id<0 || static_cast<std::size_t>(action.member_id)>=frame.state.member_count) return false;
            const auto& camp=World::get().camp(frame.group,action.member_id);
            if (!camp.is_alive() || !cambc::vision::in_vision(camp.position())) return false;
            ability.use(camp);
        }
        acts.cast=true;
    }
    return epoch(ctx.round);
}

} // namespace

void Brain::service_turn()
{
    auto& me=cambc::self();
    auto& world=World::get();
    auto& kn=Knowledge::get();
    auto& state=runtime();
    const int round=static_cast<int>(cambc::round_number());
    new_turn_obstacles();
    if (!initialised_)
    {
        kit(); roster(cambc::team::ally); roster(cambc::team::enemy);
        state.residual.resize(world.groups.size());
        state.rejected_until.assign(world.groups.size(),0);
        banned_until_.assign(world.groups.size(),0);
        initialised_=true;
        return;
    }
    kn.round=round;
    kn.read_slots();
    if (!me.is_alive())
    {
        state.op=Operator::Acquire; state.group=-1; state.resume=-1;
        discard_route(state);
        Ctx dead; dead.round=round; shop(dead);
        publish(-1,static_cast<int>(SlotStatus::idle));
        return;
    }
    if (!world.calibrated() && fuel_ok(world.field_cost()+16000))
    { world.spawn_field(true); return; }
    // Static search has its own bounded phase, before the full active observer. Cached
    // quietness merely chooses a scheduling phase; a later fresh observation authorizes steps.
    if (state.needs_plan && state.previous_quiet && state.op!=Operator::Recover && state.op!=Operator::Acquire)
    {
        const auto planned=plan_route(state,round,me.position());
        if (planned==Planning::Unreachable)
        {
            if (state.group>=0) state.rejected_until[state.group]=round+20;
            state.op=Operator::Acquire; state.group=-1; state.needs_plan=false;
        }
        return;
    }
    Ctx ctx;
    ctx.observe();
    if (!epoch(round)) return;
    kn.observe_groups(ctx,kit().vision+1);
    remember_threats(ctx);
    ctx.gem_weight=gem_weight(ctx);
    state.previous_quiet=ctx.enemies.empty() && camp_threat_at(ctx,ctx.pos)<=0.0f && ctx.n_unseen==0 && !ctx.hit_unseen;
    if (ctx.state.contains(cambc::champion_state::recalling) ||
        ctx.state.contains(cambc::champion_state::submitting_gems) ||
        ctx.state.contains(cambc::champion_state::teleporting)) return;
    if (ctx.on_spawn || kit().remote_shop) shop(ctx);
    spawn_turns_=ctx.on_spawn ? spawn_turns_+1 : 0;
    if (ctx.on_spawn && ctx.hp_frac()<0.999f && spawn_turns_<=1) return;
    TurnActions acts;
    target_=state.group;
    if (!ctx.enemies.empty() && fuel_ok(cfg::fuel_phase_threats) && handle_threats(ctx,acts))
    { discard_route(state); return; }
    if (!epoch(round)) return;

    if (state.op==Operator::Recover)
    {
        if (ctx.on_spawn && ctx.hp_frac()>=0.95f)
        {
            state.group=state.resume;
            state.resume=-1;
            state.op=state.group>=0 ? Operator::Service : Operator::Acquire;
            discard_route(state);
        }
        else
        {
            // Retain residual work during egress/recall. A deferred egress computation
            // is not a claim that the camp is impossible or that a new generation exists.
            if (camp_threat_at(ctx,ctx.pos)>0 || ctx.n_unseen>0)
            {
                if (fuel_ok(cfg::fuel_safety_field+17000)) service_exit(ctx,state.resume,&acts);
                if (!acts.moved && fuel_ok(12000)) retreat(ctx,acts);
            }
            else if (!ctx.on_spawn && me.can_move()) me.recall();
            return;
        }
    }
    if (state.op==Operator::Bank && ctx.gems==0)
    { state.op=Operator::Acquire; state.group=-1; discard_route(state); }

    if (state.op==Operator::Acquire)
    {
        float best=-1.0f;
        int group=-1;
        for (int g=0;g<static_cast<int>(world.groups.size()) && fuel_ok(16000);++g)
        {
            if (state.rejected_until[g]>round) continue;
            const float value=offer(ctx,g,state);
            if (value>best) { best=value; group=g; }
        }
        int bank_walk=100000;
        for (auto altar : world.altars) bank_walk=std::min(bank_walk,std::max(0,cheb(ctx.pos,altar)-static_cast<int>(cambc::constants::submit_gems_range)));
        const float bank_time=bank_walk+std::max(static_cast<float>(cambc::constants::submit_gem_minimum_duration),
            static_cast<float>(std::ceil(ctx.gems*cambc::constants::submit_gem_duration_multiplier)+cambc::constants::submit_gem_base_duration))+1;
        // Inventory can fund an immediate score operation. Compare its own completion
        // time at an operator boundary; a camp payout does not force a bank excursion.
        const float bank_value=ctx.gems>0 ? ctx.gem_weight*ctx.gems/bank_time : -1.0f;
        if (ctx.gems>0 && (bank_value>=best || group<0 || round+bank_time+100>=cambc::constants::max_rounds))
        { state.op=Operator::Bank; state.group=-1; }
        else if (group>=0)
        {
            state.group=group;
            state.op=kn.groups[group].status==GroupStatus::reward ? Operator::Capture : Operator::Service;
            state.front_members=world.groups[group].tiles;
        }
        else return;
        discard_route(state);
        target_=state.group;
    }

    if (state.op==Operator::Bank)
    {
        publish(-1,static_cast<int>(SlotStatus::idle));
        for (auto altar : world.altars)
            if (cheb(ctx.pos,altar)<=cambc::constants::submit_gems_range)
            {
                if (cambc::vision::has_altar_lock(altar))
                {
                    auto lock=cambc::vision::altar_lock_at(altar);
                    if (lock.is_enemy() && me.can_attack() && cheb(ctx.pos,lock.position())<=kit().attack_range)
                    { me.attack(lock); return; }
                    if (lock.is_enemy()) return;
                }
                me.submit_gems(altar);
                return;
            }
    }
    else
    {
        if (!fuel_ok(14000)) return;
        auto frame=service::observe_work(ctx,state.group,acts.moved);
        if (frame.status!=service::FrameStatus::Ready) return;
        retain_frame(state,frame);
        if (frame.availability==service::Availability::Respawning)
        {
            // Observed payout/occupation consumes availability. Release the tile so the
            // renewal timer can become usable; neither timer0 nor intent proves respawn.
            leave_camp_tiles(ctx,acts);
            state.op=Operator::Acquire; state.group=-1; discard_route(state);
            return;
        }
        if (frame.availability==service::Availability::Reward && state.op!=Operator::Capture)
        { state.op=Operator::Capture; discard_route(state); }
        else if (frame.availability==service::Availability::Alive && state.op==Operator::Capture)
        { state.op=Operator::Service; discard_route(state); }
        if (state.op==Operator::Capture)
        {
            for (auto tile : world.groups[state.group].tiles)
                if (ctx.pos==tile)
                { publish(state.group,static_cast<int>(SlotStatus::capturing)); return; }
            // A real occupant already doing this job needs no duplicate capture labour.
            for (const auto& ally : ctx.allies)
                for (auto tile : world.groups[state.group].tiles)
                    if (ally.pos==tile)
                    { state.op=Operator::Acquire; state.group=-1; discard_route(state); return; }
        }
        else
        {
            publish(state.group,static_cast<int>(SlotStatus::engaging));
            bool near=false;
            state.front_members.clear();
            for (std::size_t i=0;i<frame.state.member_count;++i)
            {
                const auto& member=frame.state.members[i];
                if (member.hp>0)
                {
                    const vec2 tile{member.pos.x,member.pos.y};
                    state.front_members.push_back(tile);
                    near|=member.hp_exact && cheb(ctx.pos,tile)<=kit().attack_range;
                }
            }
            if (near)
            {
                if (!fuel_ok(cfg::fuel_safety_field+17000)) return;
                const auto exit=service_exit(ctx,state.group);
                if (!epoch(round)) return;
                if (exit.status!=Planning::Ready) return;
                // The exit guard separately covers the entire prospective collateral
                // wake and unseen awake sources. Its conservative extra next volley
                // also covers sources outside this active group's pointwise kernel.
                for (int primitive=0;primitive<3 && fuel_ok(16000);++primitive)
                {
                    if (ctx.hit_unseen) return;
                    const auto choice=service::choose_first_command(observed_service(frame),exit.reserve);
                    if (choice.status!=service::ExecutionStatus::Ready)
                    {
                        if (choice.status==service::ExecutionStatus::NoProductive && !acts.attacked && !acts.cast)
                        { state.resume=state.group; state.op=Operator::Recover; discard_route(state); }
                        return;
                    }
                    const auto action=choice.primitive;
                    if (!execute_primitive(ctx,acts,frame,action)) return;
                    state.last_work=round;
                    if (service::refresh_work(ctx,frame,action,acts.moved)!=service::FrameStatus::Ready) return;
                    retain_frame(state,frame);
                }
                return;
            }
            if (state.route && state.route->get(ctx.pos)==0)
                discard_route(state); // A dead member's old stance is not current work.
        }
    }
    if (state.needs_plan || !state.route)
    {
        if (!state.previous_quiet) plan_route(state,round,ctx.pos);
        return;
    }
    if (!me.can_move() || !fuel_ok(10000)) return;
    const auto current=state.route->get(ctx.pos);
    if (current==cambc::distance_field::unreachable) { discard_route(state); return; }
    auto options=step_options(blocked_now(),ctx.pos);
    std::sort(options.begin(),options.end(),[&](vec2 a,vec2 b){return state.route->get(a)<state.route->get(b);});
    for (auto tile : options)
    {
        if (state.route->get(tile)>=current) continue;
        // Movement remains separately authorized by this epoch's actual occupancy and
        // whole-group approach exposure. The stored graph does not assert dynamic safety.
        if (camp_threat_at(ctx,tile)>0 || ctx.n_unseen>0)
        {
            if (!fuel_ok(cfg::fuel_safety_field+10000)) return;
            const auto exit=service_exit(ctx,state.group,nullptr,tile);
            if (exit.status!=Planning::Ready) return;
            if (ctx.hp<=exit.reserve)
            { state.resume=state.group; state.op=Operator::Recover; discard_route(state); return; }
        }
        if (!epoch(round) || me.position()!=ctx.pos) { discard_route(state); return; }
        const bool moved=production::apply_absolute_destination(me,tile);
        if (!moved) remember_revealed(tile);
        invalidate_obstacles();
        if (!moved) { discard_route(state); return; }
        return;
    }
    discard_route(state);
}

} // namespace mb
