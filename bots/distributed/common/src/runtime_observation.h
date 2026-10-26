#pragma once
#include "organization.h"
#include <span>
#include <vector>

namespace p6::runtime_observation {
// A complete observation may precede recovery or bounded model preparation.
// Policies with durable notice state accept the exact executor token before
// the adapter acknowledges delivery. Others retain their current input API.
template<class Policy>
bool deliver_failure(Policy& policy, ProjectID project, Failure failure) {
    if constexpr (requires { policy.receive_failure(project, failure); })
        return policy.receive_failure(project, failure);
    return true;
}
// Numeric personal observations only. No camp handle or live SDK view is kept
// here. Position/health/max_health are needed only for living members; dead
// members retain identity, spawn, strength and their exact lifecycle.
struct Camp {
    int id = -1;
    Point spawn{}, position{};
    bool alive = false, awake = false;
    double health = 0, max_health = 0, strength = 0;
    int attack_range = 0;
};
struct AttackTarget { int site = -1, group = -1; Point position{}; };
template<class Members,class Reading,class Contains>
bool whole_basic_members(const Members& members,Reading reading,Contains contains) {
    bool live=false;
    for(const auto& member:members) {
        const auto* current=reading(member.id);
        if(!current)return false;
        if(current->alive) {live=true;if(!contains(current->position))return false;}
    }
    return live;
}
// The site catalogue is immutable geometry. A separate, clock-bound reading
// supplies only this owner's current personal lifecycle and visibility facts.
struct SiteReading {
    ViewClock clock{};
    int group = -1;
    Lifecycle lifecycle = Lifecycle::unknown;
    bool original_spawn_visible = false, all_spawns_visible = false;
    bool uncontested_now = false, damaged_now = false;
};
struct SitePass {
    int visited = 0;
    // Original catalogue indices, including completed unknown observations.
    // False means setup was not completed, never a failed personal inspection.
    std::array<bool, 8> visited_sites{};
    bool valid = false, cutoff = false;
    bool required_visited = false, required_known = false;
    bool ready() const { return valid && required_visited; }
};
// Reuse only facts already collected in this owner's complete current camp
// snapshot. Every member must be living at its original visible spawn: a dead,
// missing or moved member cannot establish either group lifecycle or original
// spawn visibility. This makes no SDK query and grants no production action.
template<class Members,class Reading,class Enemies>
bool reuse_living_site(organization::Input& in,int index,const Members& members,
                      Reading read,const Enemies& enemies) {
    if(!in.frame.valid() || index<0 || index>=in.site_count || members.empty() || members.size()>32)
        return false;
    auto& site=in.sites[index];
    if(site.known || site.group.id<0)return false;
    bool damaged=false,uncontested=true;
    for(const auto& member:members) {
        const auto* value=read(member.id);
        if(!value || value->id!=member.id || !value->alive || value->spawn!=member.spawn ||
           value->position!=member.spawn || !std::isfinite(value->health) || value->health<=0 ||
           !std::isfinite(value->max_health) || value->health>value->max_health)return false;
        damaged|=value->health<value->max_health;
        for(const auto& enemy:enemies)if(enemy.position==member.spawn)uncontested=false;
    }
    site.known=true;site.group.lifecycle=Lifecycle::living;
    site.group.original_spawn_visible=site.group.all_spawns_visible=true;
    site.group.uncontested_now=uncontested;site.damaged_now=damaged;
    return true;
}
// Predeclare every index before budgeted live work, so policy assignments and
// static movement destinations never change with the observed prefix. The
// caller has already completed self/actors/enemy danger. Its optional guard must
// not mark that frame aborted; the reader must reject work crossing an owner
// turn or interrupted before all of this site's facts have been collected.
// An unvisited or unseen site has no fresh lifecycle, even if it was known in
// an earlier call. The reader cannot replace immutable geometry.
template<class Budget, class Reader>
SitePass publish_sites(organization::Input& in, std::span<const Group> geometry,
                      int owned_group, int requested_group, Budget can_visit, Reader read) {
    SitePass result;
    in.site_count = 0; in.frame.group = {};
    for (auto& site : in.sites) {
        site.group = {}; site.known = site.blocked = site.damaged_now = false;
        site.actor_count = 0; site.joint_better = false; site.joint_rank = {}; site.holder = -1;
    }
    if (geometry.size() > in.sites.size()) return result;
    int owned = -1, requested = -1;
    for (int i = 0; i < static_cast<int>(geometry.size()); ++i) {
        const auto& source = geometry[i];
        if (source.id < 0) return result;
        for (int previous = 0; previous < i; ++previous)
            if (geometry[previous].id == source.id) return result;
        // Copy only public geometry, never a previous lifecycle or view flag.
        in.sites[i].group = {source.id, source.approach, source.spawn, source.retreat};
        if (source.id == owned_group) owned = i;
        if (source.id == requested_group) requested = i;
    }
    in.site_count = static_cast<int>(geometry.size());
    if (owned_group >= 0 && owned < 0) return result;
    result.valid = true;
    result.required_visited = result.required_known = owned_group < 0;
    std::array<int, 8> order{}; int count = 0;
    if (owned >= 0) order[count++] = owned;
    if (requested >= 0 && requested != owned) order[count++] = requested;
    for (int i = 0; i < in.site_count; ++i)
        if (i != owned && i != requested) order[count++] = i;
    const ViewClock clock = in.frame.clock;
    for (int position = 0; position < count; ++position) {
        const int index = order[position];
        if (!can_visit()) { result.cutoff = true; break; }
        SiteReading reading;
        if (!read(index, reading) || reading.clock != clock ||
            reading.group != geometry[index].id) { result.cutoff = true; break; }
        auto& target = in.sites[index];
        target.group.original_spawn_visible = reading.original_spawn_visible;
        target.group.all_spawns_visible = reading.original_spawn_visible && reading.all_spawns_visible;
        target.group.uncontested_now = target.group.all_spawns_visible && reading.uncontested_now;
        target.known = reading.original_spawn_visible && reading.lifecycle != Lifecycle::unknown;
        if (target.known) target.group.lifecycle = reading.lifecycle;
        target.damaged_now = reading.damaged_now;
        ++result.visited;
        result.visited_sites[index] = true;
        if (index == owned) {
            result.required_visited = true; result.required_known = target.known;
            if (target.known)
                in.frame.group = {target.group, clock, target.group.id, Knowledge::exact};
        }
    }
    // An unseen travelling source is legitimate, but an owned HOLD whose
    // lifecycle is unavailable must preserve its prior resource baselines.
    // Caller receipt processing/Remember may proceed only when required_visited;
    // a pending HOLD additionally needs required_known before advancing them.
    return result;
}
// Occupancy lists can omit a dead original member. Probe an absent member only
// when its original spawn is personally visible; its own visibility check is
// still required because current/last position need not be that spawn.
template<class Sources, class Members, class SpawnVisible, class Identity, class Available, class Budget>
bool append_visible_members(Sources& sources, const Members& members, SpawnVisible spawn_visible,
                            Identity identity, Available available, Budget budget) {
    for(const auto& member:members) {
        if(!spawn_visible(member))continue;
        const int id=identity(member);
        bool present=false;
        for(const auto& source:sources)if(identity(source)==id) {present=true;break;}
        if(present)continue;
        if(!budget())return false;
        if(available(member))sources.push_back(member);
    }
    return true;
}
template<class Mask, class Position>
int ready_group(std::span<const AttackTarget> targets, const Mask& mask, Position position) {
    int ready_site=-1,group=-1;
    for(const auto& target:targets) {
        if(target.site<ready_site)continue;
        if(mask.is_set(position(target.position))) {ready_site=target.site;group=target.group;}
    }
    return group;
}

struct DamageRecord {
    int id=-1,seen_round=-2,damage_round=-2;
    double health=0;
};
// Entries only append and never expire. Search the occupied prefix rather
// than all unused capacity when a newly visible camp is first encountered.
class DamageHistory {
    std::array<DamageRecord,256> records_{};
    std::size_t used_=0;
public:
    std::span<const DamageRecord> records() const { return {records_.data(),used_}; }
    void observe(int id,int round,double health) {
        DamageRecord* record=nullptr;
        for(std::size_t i=0;i<used_;++i)if(records_[i].id==id) {record=&records_[i];break;}
        if(!record && used_<records_.size()) {
            record=&records_[used_++];record->id=id;
        }
        if(!record)return;
        if(record->seen_round>=round-1 && health<record->health)record->damage_round=round;
        record->health=health;record->seen_round=round;
    }
};

class CampSnapshot {
    ViewClock clock_{};
    bool complete_ = false;
    std::vector<Camp> camps_;
public:
    void invalidate() { complete_ = false; }
    bool current(ViewClock clock) const { return complete_ && clock_ == clock; }
    std::span<const Camp> values(ViewClock clock) const {
        return current(clock) ? std::span<const Camp>{camps_} : std::span<const Camp>{};
    }
    const Camp* find(ViewClock clock, int id) const {
        if (!current(clock)) return nullptr;
        for (const auto& camp : camps_) if (camp.id == id) return &camp;
        return nullptr; // Unseen membership is unknown, not dead or full health.
    }
private:
    // sources must be the current personal visible-camp list. The caller owns
    // the round/fuel guard and immutable range lookup. Abort never publishes a
    // partial snapshot. Discard before any action or revealed obstruction.
    template<class Sources, class Identity, class Budget, class Reader>
    bool collect_fields(const Sources& sources, ViewClock clock, Identity identity,
                        Budget budget, Reader read) {
        complete_ = false; clock_ = clock; camps_.clear();
        camps_.reserve(sources.size());
        for (const auto& source : sources) {
            if (!budget()) return false;
            Camp value; value.id = identity(source);
            const auto spawn = source.spawn_position();
            value.spawn = {spawn.x, spawn.y};
            const auto state = source.state();
            value.alive = state.is_alive(); value.awake = state.is_awake();
            read(source, value);
            camps_.push_back(value);
        }
        if (!budget()) return false;
        complete_ = true; return true;
    }
public:
    template<class Sources, class Identity, class Range, class Budget, class Reader>
    bool collect(const Sources& sources, ViewClock clock, Identity identity,
                 Range range, Budget budget, Reader read) {
        return collect_fields(sources, clock, identity, budget,
            [&](const auto& source, Camp& value) {
                read(source, value); value.attack_range = range(source);
            });
    }
    // The default reader keeps generic sources and pure tests SDK-independent.
    // A runtime reader may batch numeric fields from one checked host record;
    // lifecycle, identity, range and publication remain owned by collect().
    template<class Sources, class Identity, class Range, class Budget>
    bool collect(const Sources& sources, ViewClock clock, Identity identity,
                 Range range, Budget budget) {
        return collect_fields(sources, clock, identity, budget,
            [&](const auto& source, Camp& value) {
                value.strength = source.strength();
                value.attack_range = range(source);
                if (value.alive) {
                    const auto position = source.position();
                    value.position = {position.x, position.y};
                    value.health = source.health(); value.max_health = source.max_health();
                }
            });
    }
};
} // namespace p6::runtime_observation
