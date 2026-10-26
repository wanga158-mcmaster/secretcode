// Native two-melee elk HP pooling by real next-round SDK priority.
// No items, abilities, artificial health or strategic planner; inactive opponents stay at spawn.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <optional>
#include <vector>

using namespace cambc;
namespace {
inline constexpr int rotate_pair = 0;
inline constexpr int active_pair = 1;
inline constexpr int first_hit_round = 30;
std::optional<map::camp_group> target_group;
std::optional<champion> mate;
bool initialized = false;
bool paid = false;
bool banked = false;

bool side_b() { return !map::spawn_region(team::ally).is_set({2,7}); }
vec2 own(vec2 p) { return side_b() ? vec2{map::width()-1-p.x,map::height()-1-p.y} : p; }
int id(const champion& c) { return static_cast<int>(static_cast<std::uint32_t>(static_cast<handle_champion_t>(c))); }
vec2 camp_tile() { return own({12,10}); }
bool primary() { return self().champion_name()==champion_type::ryth; }

void initialize() {
    if (initialized) return;
    for (const auto& g : map::camp_groups()) {
        for (const auto& c : g.camps()) if(c.spawn_position()==camp_tile()) target_group=g;
    }
    const auto other_type=primary()?champion_type::marcella:champion_type::ryth;
    for (const auto& c : champions(team::ally)) if(c.champion_name()==other_type) mate=c;
    if (!target_group || !mate) throw std::runtime_error("pair identity missing");
    const auto stats=target_group->camps().front().base_stats();
    std::printf("PAIR init side=%c id=%d kind=%d hp=%.1f strength=%.1f range=%d camp_hp=%.1f camp_strength=%.1f camp_range=%d capture=%d rotate=%d\n",
        side_b()?'B':'A',id(self()),int(self().champion_name()),double(self().max_health()),double(self().strength()),
        self().attack_range(),double(stats.base_health),double(stats.base_strength),stats.base_attack_range,
        constants::capture_time,rotate_pair);
    initialized=true;
}

int group_state() {
    if (!vision::in_vision(camp_tile())) return -1;
    const auto s=target_group->state();
    if(std::holds_alternative<map::camp_group_alive>(s)) return 1;
    if(std::holds_alternative<map::camp_group_reward_available>(s)) return 2;
    return 3;
}

void record(const char* phase,int ok=-1) {
    auto& me=self();
    const auto p=me.position();
    const auto& c=target_group->camps().front();
    float chp=-1.0f;
    if(vision::in_vision(camp_tile())) chp=c.is_alive()?c.health():0.0f;
    const float mhp=mate->is_alive() && mate->is_visible()?mate->health():0.0f;
    std::printf("PAIR side=%c r=%u id=%d phase=%s x=%d y=%d hp=%.1f mate_hp=%.1f camp_hp=%.1f state=%d prev=%d next=%d M=%d A=%d C=%d gold=%u gems=%u score=%u ok=%d fuel=%llu\n",
        side_b()?'B':'A',round_number(),id(me),phase,p.x,p.y,double(me.health()),double(mhp),double(chp),group_state(),
        me.prev_priority(),me.priority(),me.can_move(),me.can_attack(),me.can_cast(),me.gold_count(),me.gem_count(),
        global_submitted_gems(team::ally),ok,static_cast<unsigned long long>(fuel_remaining()));
}

bool step(vec2 goal,bool permit_camp=false) {
    auto& me=self();
    if(!me.can_move() || me.position()==goal) return false;
    const auto p=me.position();
    int best=p.chebyshev_distance(goal)*100 + std::abs(p.x-goal.x)+std::abs(p.y-goal.y);
    std::optional<vec2> next;
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        if(dx==0 && dy==0) continue;
        const vec2 q{p.x+dx,p.y+dy};
        if(q.x<0 || q.y<0 || q.x>=map::width() || q.y>=map::height()) continue;
        if(map::walls().is_set(q) || map::holes().is_set(q) || map::altars().is_set(q)
            || map::spawn_region(team::enemy).is_set(q) || vision::has_champion(q)) continue;
        if((q==own({12,10}) || q==own({37,19})) && !(permit_camp && q==goal)) continue;
        const int cost=q.chebyshev_distance(goal)*100 + std::abs(q.x-goal.x)+std::abs(q.y-goal.y);
        if(cost<best) {best=cost;next=q;}
    }
    return next && me.move_absolute(*next);
}

void set_tank_priority() {
    auto& me=self();
    int next=primary()?100:0;
    if constexpr(rotate_pair!=0) {
        if(mate->is_alive() && mate->is_visible()) {
            const float hp=me.health(),other=mate->health();
            const bool healthier=hp>other+0.1f || (std::abs(hp-other)<=0.1f && primary());
            next=healthier?100:0;
        }
    }
    const bool m=me.can_move(),a=me.can_attack(),c=me.can_cast();
    me.set_priority(next);
    std::printf("PAIR priority side=%c r=%u id=%d prev=%d next=%d before_M=%d before_A=%d before_C=%d after_M=%d after_A=%d after_C=%d\n",
        side_b()?'B':'A',round_number(),id(me),me.prev_priority(),me.priority(),m,a,c,me.can_move(),me.can_attack(),me.can_cast());
}

void capture_or_bank() {
    auto& me=self();
    const bool capturer=primary() || !mate->is_alive();
    if(!capturer) {
        step(own({14,10}));
        record("helper_released");
        return;
    }
    if(me.gem_count()>0) paid=true;
    if(paid) {
        const auto altar=own({7,5});
        if(me.gem_count()==0 && global_submitted_gems(team::ally)>=target_group->gem_value()) {
            banked=true;record("banked");return;
        }
        if(me.is_submitting_gems()) {record("channel");return;}
        if(me.position().chebyshev_distance(altar)<=constants::submit_gems_range) {
            record("before_bank");
            if(me.can_move() || me.can_attack() || me.can_cast()) me.submit_gems(altar);
            record("after_bank");
        } else {step(own({8,6}));record("bank_walk");}
        return;
    }
    if(group_state()==2) {
        step(camp_tile(),true);
        record("capture");
    } else record("unpaid_state");
}

void turn() {
    auto& me=self();
    if constexpr(active_pair==0) return;
    const auto type=me.champion_name();
    if(type!=champion_type::ryth && type!=champion_type::marcella) {
        if(round_number()==1) me.set_priority(-100);
        return;
    }
    if(!me.is_alive()) {
        std::printf("PAIR dead side=%c r=%u id=%d\n",side_b()?'B':'A',round_number(),id(me));
        return;
    }
    initialize();
    if(banked) return;
    set_tank_priority();
    if(int(round_number())<first_hit_round) {
        step(own(primary()?vec2{11,10}:vec2{13,10}));
        if(int(round_number())==first_hit_round-1) record("ready");
        return;
    }
    record("start");
    const auto& c=target_group->camps().front();
    if(group_state()==1) {
        if(me.position().chebyshev_distance(camp_tile())!=1) {
            record("geometry_refusal");return;
        }
        if(me.can_attack() && c.is_alive()) {
            me.attack(c);
            record("after_basic",1);
        }
    }
    if(group_state()!=1) capture_or_bank();
}
}

int main() {
    while(true) {
        try {
            if(is_draft()) {
                const std::array picks{champion_draft::ryth,champion_draft::marcella,champion_draft::stellar};
                const auto mine=draft::current_draft(team::ally);
                for(auto pick:picks) if(std::find(mine.begin(),mine.end(),pick)==mine.end()) {draft::pick(pick);break;}
            } else turn();
        } catch(const std::exception& e) {std::printf("pair error: %s\n",e.what());}
        catch(...) {std::printf("pair error: unknown\n");}
        end_turn();
    }
}
