#pragma once
// Constructed native task discriminator. No purchases, allies or artificial resources.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>
#include "probe_config.h"

namespace mosaic {
namespace geometry {
using namespace cambc;
// Lazy allocation keeps this inactive mechanics fixture out of starter startup.
inline map::camp_group* task=nullptr;
inline bool initialized = false, paid = false, banked = false, recovering = false, contacted = false;
inline int recalls = 0;
inline bool side_b() { return !map::spawn_region(team::ally).is_set({2, 7}); }
inline vec2 own(vec2 p) { return side_b() ? vec2{map::width()-1-p.x, map::height()-1-p.y} : p; }
inline int actor_id() { return int(std::uint32_t(handle_champion_t(self()))); }
inline int state() {
    if (!vision::in_vision(own({12,10}))) return -1;
    const auto s=task->state();
    if (std::holds_alternative<map::camp_group_alive>(s)) return 1;
    if (std::holds_alternative<map::camp_group_reward_available>(s)) return 2;
    return 3;
}
inline void record(const char* phase, int ok=-1) {
    if constexpr (!cfg::probe_trace) return;
    auto& me=self();
    const auto p=me.position();
    float total=0; int alive=0;
    if (task) for (const auto& c:task->camps()) {
        if (!vision::in_vision(c.spawn_position())) {total=-1; alive=-1;break;}
        if (c.is_alive()) {total+=c.health(); ++alive;}
    }
    std::printf("GEOMETRY side=%c r=%u id=%d actor=%d phase=%s x=%d y=%d hp=%.2f camp_hp=%.2f alive=%d state=%d M=%d A=%d C=%d cd0=%d cd1=%d gems=%u gold=%u score=%u recalls=%d ok=%d fuel=%llu\n",
        side_b()?'B':'A',round_number(),actor_id(),cfg::probe_actor,phase,p.x,p.y,double(me.health()),double(total),alive,
        task?state():-1,me.can_move(),me.can_attack(),me.can_cast(),me.abilities()[0].cooldown(),me.abilities()[1].cooldown(),
        me.gem_count(),me.gold_count(),global_submitted_gems(team::ally),recalls,ok,(unsigned long long)fuel_remaining());
}
inline bool step(vec2 goal, bool permit_task=false) {
    auto& me=self();
    if (!me.can_move() || me.position()==goal) return false;
    const auto p=me.position();
    int best=int(p.chebyshev_distance(goal))*100+std::abs(p.x-goal.x)+std::abs(p.y-goal.y);
    std::optional<vec2> next;
    const auto blocked=map::walls()|map::holes()|map::altars()|map::spawn_region(team::enemy)|vision::champion_tiles()|vision::camp_tiles();
    for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
        if (!dx && !dy) continue;
        vec2 q{p.x+dx,p.y+dy};
        if (q.x<0 || q.y<0 || q.x>=map::width() || q.y>=map::height()) continue;
        if (blocked.is_set(q) && !(permit_task && q==goal && !vision::has_champion(q))) continue;
        const int cost=int(q.chebyshev_distance(goal))*100+std::abs(q.x-goal.x)+std::abs(q.y-goal.y);
        if (cost<best) {best=cost;next=q;}
    }
    return next && me.move_absolute(*next);
}
inline void initialize() {
    if (initialized) return;
    for (const auto& g:map::camp_groups()) for (const auto& c:g.camps()) if (c.spawn_position()==own({12,10})) task=new map::camp_group(g);
    if (!task || task->camps().size()!=4) throw std::runtime_error("geometry task identity missing");
    initialized=true; record("init");
}
inline float incoming() {
    float damage=0;
    for (const auto& c:task->camps()) if (c.is_visible() && c.is_alive()) {
        if (self().position().chebyshev_distance(c.position())<=c.base_stats().base_attack_range) damage+=c.base_stats().base_strength;
    }
    return damage;
}
inline bool recovery() {
    auto& me=self();
    if (map::spawn_region(team::ally).is_set(me.position())) {
        if (me.health()<me.max_health()) {record("spawn_heal");return true;}
        recovering=false;contacted=false;record("recovery_ready");return false;
    }
    if (me.is_recalling()) {record("recalling");return true;}
    if (incoming()>0) {step(own({7,10}));record("exit");return true;}
    me.recall();++recalls;record("recall_started");return true;
}
inline void finish() {
    auto& me=self();
    if (me.gem_count()>0) paid=true;
    if (paid) {
        if (me.gem_count()==0 && global_submitted_gems(team::ally)>=task->gem_value()) {
            banked=true; record("banked"); return;
        }
        if (me.is_submitting_gems()) {record("channel");return;}
        const auto altar=own({7,5});
        if (me.position().chebyshev_distance(altar)<=constants::submit_gems_range) {
            me.submit_gems(altar);record("tribute_started");
        } else {step(own({8,6}));record("bank_walk");}
        return;
    }
    if (state()==2) {step(own({12,10}),true);record("capture");}
    else record("unpaid_state");
}
inline void run() {
    if constexpr (cfg::probe_actor==2) return;
    auto& me=self();
    const auto selected=cfg::probe_actor==0?champion_type::varg:champion_type::ryth;
    if (me.champion_name()!=selected) return;
    if (!me.is_alive()) {std::printf("GEOMETRY side=%c r=%u actor=%d phase=dead\n",side_b()?'B':'A',round_number(),cfg::probe_actor);return;}
    initialize();
    if (banked) return;
    if (me.is_submitting_gems()) {record("channel");return;}
    if (recovering && recovery()) return;
    if (paid || state()==2 || state()==3) {finish();return;}
    if (!contacted && me.position()!=own({11,10})) {step(own({11,10}));record("approach");return;}
    if (state()!=1) {finish();return;}
    record("start");
    // Three future camp volleys can occur while walking west to safety. Cover those
    // individually before another attack; this is a partial-work/return controller.
    if (me.health()<=4.0f*incoming()) {recovering=true;recovery();return;}
    const auto a0=me.abilities()[0],a1=me.abilities()[1];
    if constexpr (cfg::probe_actor==0) {
        if (a1.is_ready()) {a1.use();record("shield");}
    } else {
        if (a0.is_ready()) {a0.use(own({12,10}));record("area_stun");}
    }
    std::optional<camp> target;
    const auto mask=me.attack_tiles();
    for (const auto& c:task->camps()) if (c.is_alive() && mask.is_set(c.position())) {
        if (!target || c.health()<target->health()) target=c;
    }
    if (!target && me.can_move()) {
        // The near members may be dead while a far member remains. Reach a legal
        // attack tile instead of treating original contact geometry as permanent.
        std::optional<vec2> next_goal;int best=1<<20;
        const auto blocked=map::walls()|map::holes()|map::altars()|vision::champion_tiles()|vision::camp_tiles();
        for (const auto& c:task->camps()) if(c.is_alive()) for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            if(!dx && !dy) continue;
            vec2 q{c.position().x+dx,c.position().y+dy};
            if(q.x<0||q.y<0||q.x>=map::width()||q.y>=map::height()||blocked.is_set(q))continue;
            const int d=int(me.position().chebyshev_distance(q));
            if(d<best){best=d;next_goal=q;}
        }
        if(next_goal) step(*next_goal);
        const auto fresh=me.attack_tiles();
        for(const auto& c:task->camps()) if(c.is_alive()&&fresh.is_set(c.position())){target=c;break;}
        record("combat_step");
    }
    if (target && me.can_attack()) {contacted=true;me.attack(*target);record("basic");}
    // Reset after an actual basic only; the one successful cast may already be spent.
    const auto reset=cfg::probe_actor==0?a0:a1;
    if (target && !me.can_attack() && me.can_cast() && reset.is_ready()) {
        reset.use();record("reset");
        target.reset();const auto attack=me.attack_tiles();
        for (const auto& c:task->camps()) if(c.is_alive() && attack.is_set(c.position())) {target=c;break;}
        if (target && me.can_attack()) {me.attack(*target);record("basic_after_reset");}
    }
    if (state()!=1) finish();
}
}
inline void probe_draft() {
    using namespace cambc;
    const std::array picks{champion_draft::varg,champion_draft::ryth,champion_draft::stellar};
    const auto chosen=draft::current_draft(team::ally);
    for (const auto p:picks) if (std::find(chosen.begin(),chosen.end(),p)==chosen.end()) {draft::pick(p);break;}
}
inline void probe_turn() { geometry::run(); }
}
