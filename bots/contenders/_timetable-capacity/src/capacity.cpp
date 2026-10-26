#include "brain.h"
#include "capacity.h"
#include "config.h"
#include "knowledge.h"
#include "nav.h"
#include "world.h"

#if CAPACITY_JOBS
namespace mb {

int Brain::capacity_choose_target(const Ctx& ctx,float* out_score)
{
    auto& world=World::get(); auto& kn=Knowledge::get();
    const int n=std::min(capacity::bodies_max,static_cast<int>(kn.allies.size()));
    const int self=kn.self_index;
    if(self>=n || static_cast<int>(kn.allies.size())!=n || cambc::shared_memory_buffer::size()<static_cast<std::size_t>(n+2)) return -2;
    std::array<capacity::Body,capacity::bodies_max> bodies;
    std::array<model::ChampionSummary,capacity::bodies_max> summaries;
    std::array<vec2,capacity::bodies_max> positions;
    const float base=std::max(1.f,kit().summary.strength);
    const float growth=std::clamp(ctx.strength/base,1.f,4.f);
    for(int i=0;i<n;++i) {
        auto s=design_summary(kn.allies[i].champion_name());
        positions[i]=world.ally_spawn_center;
        float hp=s.health*growth, max_hp=hp;
        s.strength*=growth; s.focus*=growth;
        const auto& slot=kn.slots()[i];
        int locked=-1; bool observed=false,eligible=false;
        if(slot.group>=0 && (slot.status==SlotStatus::engaging || slot.status==SlotStatus::capturing)) {
            locked=slot.group; positions[i]=world.groups[locked].center;
        }
        for(const auto& a:ctx.allies) if(a.index==i) {
            positions[i]=a.pos;hp=a.hp;max_hp=a.max_hp;observed=true;
            eligible=capacity::live_stats(s,a.strength,a.focus);
            const auto state=a.champ->state();
            const auto restrictions=a.champ->restrictions();
            eligible=eligible && !state.contains(cambc::champion_state::recalling) && !state.contains(cambc::champion_state::submitting_gems) &&
              !state.contains(cambc::champion_state::teleporting) && a.gems<cfg::tribute_gems &&
              !restrictions.contains(cambc::buff_restrictions::move) && !restrictions.contains(cambc::buff_restrictions::attack) &&
              !restrictions.contains(cambc::buff_restrictions::cast) &&
              (state.contains(cambc::champion_state::can_move) || state.contains(cambc::champion_state::can_attack) || state.contains(cambc::champion_state::can_cast));
        }
        // Unknown idle allies are never newly commanded. An unseen existing lease stays locked
        // and uses explicitly approximate design capacity until physical assembly is observed.
        bodies[i]={model::camp_dps(s,0,cfg::camp_dash_damage),std::max(0.f,hp-cfg::recall_health_frac*max_hp),1.f,locked,kn.alive[i]!=0 && (observed?eligible:locked>=0)};
        summaries[i]=s;
    }
    positions[self]=ctx.route_pos;
    summaries[self]=kit().summary; summaries[self].strength=ctx.strength;summaries[self].focus=ctx.focus;
    bodies[self]={model::camp_dps(summaries[self],0,cfg::camp_dash_damage),std::max(0.f,ctx.hp-cfg::recall_health_frac*ctx.max_hp),ctx.camp_damage_taken_mult,-1,true};

    struct Candidate { int group; float upper; };
    std::vector<Candidate> order;
    const int capture=cambc::constants::capture_time;
    // Capital until the first productive emblem budget is earned; later the queried game race
    // weight prices gems. This changes the executed opening portfolio, not the fixed draft.
    const float gold_weight=cfg::gold_value*(ctx.round<250 && ctx.gold<300 ? cfg::capacity_bootstrap : 1.f);
    const auto make_job=[&](int g) {
        const auto& info=world.groups[g];const auto& belief=kn.groups[g];
        capacity::Job j;j.group=g;j.capture=capture;
        j.earliest=belief.status==GroupStatus::respawning?std::max(0,belief.respawn_round-ctx.round):0;
        j.value=ctx.gem_weight*static_cast<float>(info.gems)+gold_weight*static_cast<float>(info.gold);
        j.fight={info.total_hp,info.weighted_hp,info.total_strength};
#if CAPACITY_MEMBER_CLOCK
        for(auto type:info.types)if(type<6)j.largest_hp=std::max(j.largest_hp,world.camp_stats[type].health);
#endif
        if(belief.status==GroupStatus::reward){j.fight={};j.hp_scale=0;}
        else if(belief.hp_left>=0 && info.total_hp>0)j.hp_scale=std::min(1.f,belief.hp_left/info.total_hp);
        const auto* field=world.group_field(g,false);
        j.home=std::min(300,World::distance(field,world.ally_spawn_center,info.center));
        for(int i=0;i<n;++i){
            j.arrival[i]=World::distance(field,positions[i],info.center);
            j.dps[i]=model::camp_dps(summaries[i],info.neighbours,cfg::camp_dash_damage);
#if CAPACITY_MEMBER_CLOCK
            j.member_dps[i]=model::camp_dps(summaries[i],0,cfg::camp_dash_damage);
#endif
        }
        return j;
    };
    const auto accept=[&](const capacity::Bid& bid) {
        if(bid.group<0){if(out_score)*out_score=0;publish(-1,static_cast<int>(SlotStatus::idle));return -2;}
        const auto& b=kn.groups[bid.group];
        capacity::calendar().issue(bid,ctx.round,b.seen_round==ctx.round && b.status!=GroupStatus::respawning);
        target_need_=capacity::count(bid.crew);
        if(out_score)*out_score=bid.rate;
        if constexpr(cfg::capacity_trace) MB_LOG("job issue r%d s%d g%d crew%x ready%d end%d clear%d rate%.3f\n",ctx.round,self,bid.group,bid.crew,ctx.round+bid.ready,ctx.round+bid.finish,bid.clear,bid.rate);
        return bid.group;
    };
    const auto& own=kn.slots()[self];
    std::byte command_mask{};cambc::shared_memory.read(n,std::span<std::byte>{&command_mask,1});
    if(own.group>=0 && (static_cast<int>(command_mask)&(1<<self)) && (own.status==SlotStatus::engaging || own.status==SlotStatus::capturing)) {
        int crew=1<<self;
        for(int i=0;i<n;++i)if(i!=self && kn.alive[i] && kn.slots()[i].group==own.group && static_cast<int>(kn.slots()[i].status)<2)crew|=1<<i;
        return accept(capacity::quote(make_job(own.group),bodies,n,crew,cfg::camp_safety_margin));
    }
    publish(-1,static_cast<int>(SlotStatus::idle));
    std::array<std::byte,10> wire{};
    cambc::shared_memory.read(0,std::span<std::byte>{wire.data(),static_cast<std::size_t>(n+2)});
    std::array<std::uint8_t,10> board{};
    for(int i=0;i<n+2;++i)board[i]=static_cast<std::uint8_t>(wire[i]);
    // A round stamp identifies the last serial transaction; it never vetoes a body that the
    // previous planner could not see. Each later idle actor may fill still-unassigned slots.
    for(std::size_t g=0;g<world.groups.size() && g<63;++g) {
        if(ctx.round<banned_until_[g] || world.walk_reachable(static_cast<int>(g))==0) continue;
        const auto& info=world.groups[g]; const auto& b=kn.groups[g];
        if(b.status==GroupStatus::reward && kn.capturers(static_cast<int>(g))>0) continue;
        const auto* field=world.group_field(static_cast<int>(g),false);
        if(!field) continue;
        const int travel=World::distance(field,ctx.route_pos,info.center);
        if(travel>=16384) continue;
        if(b.status==GroupStatus::respawning && b.respawn_round>ctx.round+travel+8) continue;
        const float value=ctx.gem_weight*static_cast<float>(info.gems)+gold_weight*static_cast<float>(info.gold);
        float upper=value/static_cast<float>(std::max(1,travel+capture));
        if(kn.committed(static_cast<int>(g))>0) upper*=1.05f;
        order.push_back({static_cast<int>(g),upper});
    }
    std::sort(order.begin(),order.end(),[](auto a,auto b){return a.upper>b.upper;});
    std::vector<capacity::Job> jobs;
    static capacity::Frontier frontier;
    for(int step=0;step<static_cast<int>(order.size());++step) {
        if(jobs.size()>=static_cast<std::size_t>(cfg::capacity_candidates) || fuel()<50000) break;
        const auto& c=order[frontier.index(static_cast<int>(order.size()),step)];
        jobs.push_back(make_job(c.group));
    }
    auto result=capacity::allocate(jobs,bodies,n,cfg::camp_safety_margin,[]{return fuel()>54000;});
    frontier.advance(static_cast<int>(order.size()),result.evaluated);
    capacity::assign_commands(board,result,n,ctx.round);
    for(int i=0;i<n+2;++i)wire[i]=static_cast<std::byte>(board[i]);
    cambc::shared_memory.write(0,std::span<const std::byte>{wire.data(),static_cast<std::size_t>(n+2)});
    kn.read_slots(); // the same authoritative command board will be consumed by later actors
    return accept(result.body[self]);
}

bool Brain::capacity_tick(Ctx& ctx,TurnActions& acts)
{
    auto& job=capacity::calendar();auto& kn=Knowledge::get();auto& world=World::get();
    if(job.group<0) return false;
    const int group=job.group;
    if(target_!=group) {job.release();return false;}
    if(kn.slots()[kn.self_index].group!=group) {job.release();target_=-1;capturing_=false;engaged_=false;return false;}
    const auto& b=kn.groups[group]; const bool fresh=b.seen_round==ctx.round;
    if(fresh && b.status!=GroupStatus::respawning) job.started=true;
    const char* release=nullptr;
    if(fresh && b.status==GroupStatus::respawning && job.started) release="lifecycle";
    else if(ctx.round>job.deadline) release="deadline";
    else if(!engaged_ && !capturing_ && ctx.round>job.assembly) release="assembly";
    else if(fresh && b.status==GroupStatus::respawning && ctx.round>job.ready+8) release="unavailable";
    if(!release && fresh && b.status==GroupStatus::reward) {
        int capturer=kn.self_index, best=1<<20;
        const auto distance=[&](vec2 p){int d=1<<20;for(auto tile:world.groups[group].tiles)d=std::min(d,cheb(p,tile));return d;};
        best=distance(ctx.pos);
        if(capturing_) best=-1;
        for(std::size_t i=0;i<kn.slots().size();++i) if(static_cast<int>(i)!=kn.self_index && kn.alive[i]) {
            const auto& s=kn.slots()[i]; if(s.group!=group || s.status==SlotStatus::idle || s.status==SlotStatus::cleared) continue;
            int d=1<<20;
            if(s.status==SlotStatus::capturing) d=-1;
            else for(const auto& a:ctx.allies) if(a.index==static_cast<int>(i)) d=distance(a.pos);
            if(d<best || (d==best && static_cast<int>(i)<capturer)) {capturer=static_cast<int>(i);best=d;}
        }
        if(capturer!=kn.self_index) release="helper";
    }
    if(!release) return false;
    if constexpr(cfg::capacity_trace) MB_LOG("job release r%d s%d g%d why%s\n",ctx.round,kn.self_index,group,release);
    if(release[0]!='h') banned_until_[group]=ctx.round+12;
    target_=-1;capturing_=false;engaged_=false;staged_since_=-1;job.release();
    publish(-1,static_cast<int>(SlotStatus::idle));
    if(release[0]=='h') {
        leave_camp_tiles(ctx,acts);
        return acts.moved;
    }
    return false;
}

bool Brain::capacity_stage(Ctx& ctx,TurnActions& acts,int group)
{
    const auto& info=World::get().groups[group];
    const auto score=[&](vec2 tile){
        int nearest=1<<20,penalty=0;
        for(const auto& c:ctx.camps) if(c.group==group) {
            const int d=cheb(tile,c.pos);nearest=std::min(nearest,d);
            if(c.awake && c.strength>0 && d<=c.range)penalty-=10000;
        }
        if(nearest==1<<20)nearest=cheb(tile,info.center);
        return penalty-50*std::abs(nearest-kit().attack_range);
    };
    if(staged_since_<0)staged_since_=ctx.round;
    if(step_best(score,score(ctx.pos))){acts.moved=true;ctx.refresh_self();}
    return true;
}

bool Brain::capacity_ready(const Ctx& ctx,int group) const
{
    const auto& job=capacity::calendar();const auto& kn=Knowledge::get();
    if(job.group!=group || ctx.round<job.ready) return false;
    int present=1;
    for(const auto& a:ctx.allies) {
        if(!(job.crew&(1<<a.index)) || kn.slots()[a.index].group!=group) continue;
        const auto state=a.champ->state();
        const auto restrictions=a.champ->restrictions();
        if(state.contains(cambc::champion_state::recalling) || state.contains(cambc::champion_state::submitting_gems) ||
           restrictions.contains(cambc::buff_restrictions::attack) || restrictions.contains(cambc::buff_restrictions::cast) ||
           (!state.contains(cambc::champion_state::can_move) && !state.contains(cambc::champion_state::can_attack) && !state.contains(cambc::champion_state::can_cast))) continue;
        int distance=1<<20;
        for(const auto& c:ctx.camps) if(c.group==group) distance=std::min(distance,cheb(a.pos,c.pos));
        const int reach=a.range+(a.range<=1?1:0);
        if(distance<=reach) ++present;
    }
    return present>=target_need_;
}
} // namespace mb
#endif
