#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace p9::live {
using Gems = std::int64_t;
inline constexpr std::size_t group_capacity=24;
enum class Champion { marcella, ren, tavi, varg, willow, vesper, prism,
    vendetta, ryth, stellar, cassi, issac, heart, other };
enum class Arm { active, control };
struct Point { int x = -1, y = -1; bool operator==(const Point&) const = default; };
inline int distance(Point a, Point b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
enum class Channel { none, tribute, recall, teleport };
struct Actor {
    int id = -1, pick = -1;
    Champion type = Champion::other;
    Point position{};
    bool visible = false, alive = false, ally = false;
    bool can_move = false, can_attack = false, can_cast = false, at_spawn = false;
    Channel channel = Channel::none;
    Gems bag = 0;
    double hp = 0, max_hp = 0, strength = 0, focus = 0;
    int range = 0;
    double burst = 0;
    double camp_attack_multiplier = 1, camp_defense_multiplier = 1;
    int threat_range = 0;
    std::array<int,2> cooldown{-1,-1};
    int defense_shards = 0;
};
struct Group {
    int id = -1;
    Point position{}, approach{};
    bool observed = false, living = false, cleared = false, respawning = false;
    bool route_known = false, safe_solo = false, contested = false;
    int travel = -1, to_bank = -1, own_distance = -1, enemy_distance = -1;
    Gems gems = 0, gold = 0;
    double hp = 0, incoming = 0;
    double camp_incoming = -1;
    int clear_turns = -1;
};
struct Altar {
    int id = -1;
    Point position{}, approach{};
    bool visible = false, route_known = false, enemy_lock = false, friendly_lock = false;
    int travel = -1;
};
struct World {
    int round = 0, picks = 3, max_rounds = 0, win_gems = 0;
    int capture_turns = 0, own_gold = 0, lock_cost = 0, defense_cost = 0;
    int tribute_ticks = 0, last_bank_round = -1, last_payout_round = -1;
    double tribute_multiplier = 0;
    int tribute_base = 0, tribute_minimum = 1;
    int last_kill_round = -1, last_kill_target = -1, last_heal_round = -1, last_heal_actor = -1;
    double last_heal_amount = 0;
    int last_purchase_round = -1;
    int last_payout_group = -1, productive_group = -1;
    bool own_bank_completed = false, own_payout = false, has_lock = false;
    bool safe_now = false, capture_active = false, extra_opposition = false;
    bool own_kill_receipt = false, own_heal_receipt = false, own_purchase_receipt = false;
    bool response_window_verified = false, column_without_healer_verified = false;
    int verified_response_enemy = -1, verified_response_until = -1, verified_column_group = -1;
    Gems score = 0, enemy_score = 0, bank_lower = 0, bank_upper = -1;
    Actor self{};
    std::array<Actor,5> allies{};
    int ally_count = 0;
    std::array<Actor,2> enemies{};
    int enemy_count = 0;
    std::array<Group,group_capacity> groups{};
    int group_count = 0;
    std::array<Altar,2> altars{};
    int altar_count = 0;
    struct Travel {
        int actor = -1;
        std::array<int,2> altars{-1,-1};
        std::array<int,group_capacity> groups=[] {
            std::array<int,group_capacity> out{};out.fill(-1);return out;
        }();
    };
    std::array<Travel,6> routes{};
    int route_count = 0;
    // Deadline scenarios are official, observed score-rate, doubled-rate/floor.
    std::array<int,3> windows{0,0,0};
    int response = 3; // contest/substitute/wait/unknown; kinematics only.
    int ticks(Gems bag) const {
        if(bag<=0 || !std::isfinite(tribute_multiplier) || tribute_multiplier<0 || tribute_base<0 || tribute_minimum<1)
            return 121;
        const double raw=std::ceil(double(bag)*tribute_multiplier)+tribute_base;
        return raw>120 ? 121 : std::max(tribute_minimum,static_cast<int>(raw));
    }
    const Actor* actor(int id) const {
        if (id == self.id) return &self;
        for (int i=0;i<ally_count;++i) if (allies[i].id==id && allies[i].visible) return &allies[i];
        for (int i=0;i<enemy_count;++i) if (enemies[i].id==id && enemies[i].visible) return &enemies[i];
        return nullptr;
    }
    const Actor* pick(int ordinal) const {
        if (self.pick==ordinal) return &self;
        for (int i=0;i<ally_count;++i) if (allies[i].pick==ordinal && allies[i].visible) return &allies[i];
        return nullptr;
    }
    const Group* group(int id) const {
        for(int i=0;i<group_count;++i) if(groups[i].id==id) return &groups[i]; return nullptr;
    }
    const Altar* altar(int id) const {
        for(int i=0;i<altar_count;++i) if(altars[i].id==id) return &altars[i]; return nullptr;
    }
    bool immediate_bank() const { return self.bag>0 && win_gems>0 && self.bag>=win_gems-score; }
    int altar_travel(int actor_id,int altar_id) const {
        for(int a=0;a<altar_count;++a) if(altars[a].id==altar_id) {
            if(actor_id==self.id) return altars[a].travel;
            for(int i=0;i<route_count;++i) if(routes[i].actor==actor_id) return routes[i].altars[a];
        }
        return -1;
    }
    int group_travel(int actor_id,int group_id) const {
        for(int g=0;g<group_count;++g) if(groups[g].id==group_id) {
            if(actor_id==self.id) return groups[g].travel;
            for(int i=0;i<route_count;++i) if(routes[i].actor==actor_id) return routes[i].groups[g];
        }
        return -1;
    }
};
enum class Task { independent, farm, capture, bank, support, intercept, heal,
    buy_defense, buy_lock, place_lock, wait, recover, inspect };
struct Directive {
    Task task = Task::independent;
    int group = -1, actor = -1, altar = -1, until = -1;
    bool strategic = false;
    const char* reason = "independent";
    int purchase_target = -1;
};
struct Profile {
    int design = 0;
    const char* slug = "";
    std::array<Champion,5> draft{};
    Gems bank_batch = 24;
    unsigned capital_recall_shards = 10;
    unsigned monster_shards = 5, jungle_shards = 5;
    bool partial_clears = false;
    // Opt in per design: -2 disables gold priority, -1 selects every role,
    // otherwise one draft role funds its first emblems ahead of gem work.
    int bootstrap_pick = -2;
    // Retain stationary spell opportunities only for designs that opt in.
    bool pre_move_cast = false;
};
struct Campaign {
    int used = 0, epoch[2]{-1,-1}, admitted = -1, last_fresh = -1;
    int turns = 0, last_round = -1, budget_epoch = -1;
    bool active = false, disabled = false;
    bool may_start(const World& w) const {
        return !active && w.safe_now && (budget_epoch!=w.round/64 || (!disabled&&used<2));
    }
    bool start(const World& w) {
        if(!may_start(w)) return false;
        // Renew the bounded campaign allowance, never an active segment, at
        // the next epoch. Two attempts share each 64-round allowance.
        if(budget_epoch!=w.round/64) {
            budget_epoch=w.round/64;used=0;disabled=false;epoch[0]=epoch[1]=-1;
        }
        epoch[used++]=w.round/64; admitted=last_fresh=last_round=w.round;
        turns=1; active=true; return true;
    }
    void tick(const World& w, bool fresh) {
        if(!active) return;
        if(w.round>last_round) { turns+=w.round-last_round; last_round=w.round; }
        if(fresh) last_fresh=w.round;
        if(turns>16) disabled=true;
    }
    bool expired(const World& w) const { return disabled || turns>=16 || w.round-last_fresh>=8; }
    void finish() { active=false; }
};
inline bool ready(const Actor* a) { return a && a->visible && a->alive && a->channel==Channel::none; }
inline bool safe_for(const Group& g, const Actor& a) {
    if(!g.observed || !g.route_known || !a.visible || !a.alive || a.max_hp<=0 || a.strength<=0) return false;
    const double camps=std::clamp(g.camp_incoming<0?g.incoming:g.camp_incoming,0.,g.incoming);
    const double incoming=g.incoming-camps+camps*a.camp_defense_multiplier;
    if(g.cleared) return a.hp>=std::max(.2*a.max_hp,2*incoming);
    const double work=std::ceil(g.hp/std::max(1.,a.strength*a.camp_attack_multiplier)) + 2;
    return a.hp-incoming*work>=std::max(.2*a.max_hp,2*incoming);
}
inline const Group* nearest_safe(const World& w, int excluded=-1, bool outer=false) {
    const Group* best=nullptr;
    for(int i=0;i<w.group_count;++i) {
        const auto& g=w.groups[i];
        if(g.id==excluded || !g.observed || !g.route_known || !g.safe_solo || g.respawning ||
            g.travel<0 || g.to_bank<0 || (outer && (g.own_distance<0 || g.enemy_distance<0 || g.own_distance<g.enemy_distance))) continue;
        if(!best || g.travel<best->travel || (g.travel==best->travel && g.id<best->id)) best=&g;
    }
    return best;
}
inline const Actor* loaded_collector(const World& w, int preferred=-1) {
    if(preferred>=0) { const auto* a=w.pick(preferred); if(ready(a)&&a->bag>0) return a; }
    const Actor* best=nullptr;
    for(int i=0;i<w.ally_count;++i) {
        const auto& a=w.allies[i];
        if(ready(&a) && a.bag>0 && (!best || a.bag>best->bag || (a.bag==best->bag&&a.id<best->id))) best=&a;
    }
    return best;
}
inline const Actor* favorable_rival(const World& w) {
    if(w.extra_opposition) return nullptr;
    for(int i=0;i<w.enemy_count;++i) {
        const auto& e=w.enemies[i];
        if(e.visible&&e.alive&&e.bag>0&&distance(e.position,w.self.position)<=4&&
            w.self.hp>=std::max(.35*w.self.max_hp,3*e.strength) &&
            e.hp<=3*std::max(1.,w.self.strength)) return &e;
    }
    return nullptr;
}
struct Value {
    Gems worst=0, nominal=0;
    int turns=0; Gems peak=0; int arrival=0,id=-1;
    bool feasible=false;
    std::array<std::array<Gems,3>,4> submitted{};
    unsigned unresolved=0;
};
struct ResponseObservation {int id=-1,round=-1,primary=-1;Point position{};};
class ResponseHistory {
    std::array<ResponseObservation,2> prior_{};
public:
    // Numeric labels match World::response. Distances are supplied by the
    // retained route fields; a missing identity/route never means waiting.
    int classify(ResponseObservation now,bool contest,bool other_site,bool extra=false) {
        if(extra||now.id<0||now.round<0||now.primary<0){prior_={};return 3;}
        const bool consecutive=prior_[1].id==now.id&&prior_[1].round==now.round-1;
        int response=3;
        if(contest||(consecutive&&now.primary<prior_[1].primary))response=0;
        else if(consecutive&&other_site&&now.primary>prior_[1].primary)response=1;
        else if(consecutive&&prior_[0].id==now.id&&prior_[0].round==now.round-2&&
            now.position==prior_[1].position&&now.position==prior_[0].position)response=2;
        prior_[0]=prior_[1];prior_[1]=now;return response;
    }
};
// Complete source -> own bag -> tribute -> return, with no denial or gold-score
// conversion. Unrepresented opposition receives no favorable response credit.
inline Value cycle_value(const World& w, const Group& g, int extra_turns=0) {
    Value out;out.id=g.id;
    if(!g.safe_solo || !g.route_known || !g.observed || g.travel<0 || g.to_bank<0 || g.clear_turns<0) return out;
    const int bag=static_cast<int>(std::min<Gems>(w.self.bag+g.gems,std::numeric_limits<int>::max()));
    const int ticks=w.ticks(bag);
    const int bank=g.travel+g.clear_turns+w.capture_turns+g.to_bank+ticks+2+extra_turns;
    const int renewal=bank+g.to_bank+2;
    if(renewal>120 || bank<0 || bag<=0) return out;
    out.feasible=true;out.worst=bag;
    for(int response=0;response<4;++response)for(int scenario=0;scenario<3;++scenario) {
        // Contest/unknown has no represented favorable completion when a rival
        // can contest or an extra actor is omitted. The other branches use the
        // same paid source/bank/renewal; neither receives denial/theft credit.
        const bool unresolved=w.extra_opposition||((response==0||response==3)&&g.contested);
        if(unresolved)out.unresolved|=1u<<response;
        const Gems score=!unresolved&&bank<=w.windows[scenario]?bag:0;
        out.submitted[response][scenario]=score;out.worst=std::min(out.worst,score);
    }
    out.nominal=out.submitted[std::clamp(w.response,0,3)][1];
    out.turns=renewal;out.peak=bag;out.arrival=renewal;return out;
}
inline bool better(Value a, Value b) {
    if(a.feasible!=b.feasible) return a.feasible;
    if(a.worst!=b.worst) return a.worst>b.worst;
    if(a.nominal!=b.nominal) return a.nominal>b.nominal;
    if(a.turns!=b.turns) return a.turns<b.turns;
    if(a.peak!=b.peak) return a.peak<b.peak;
    if(a.arrival!=b.arrival) return a.arrival<b.arrival;
    return a.id<b.id;
}
inline Directive bank(int altar=-1,const char* reason="bank") {return {Task::bank,-1,-1,altar,-1,false,reason};}
inline Directive farm(int group,const char* reason="farm") {return {Task::farm,group,-1,-1,-1,false,reason};}
inline Directive support(int actor,int group=-1,const char* reason="support") {return {Task::support,group,actor,-1,-1,true,reason};}
} // namespace p9::live
