#pragma once
#include <cambc.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include "allocation-wire.h"
#include "model.h"
#include "paths.h"

namespace plan3_runtime {

inline std::vector<int> market_fixed_roster(unsigned count) {
    if (count < 3 || count > 5) return {};
    constexpr std::array fixed{cambc::champion_type::tavi, cambc::champion_type::varg,
        cambc::champion_type::vendetta, cambc::champion_type::vesper_vale,
        cambc::champion_type::marcella};
    std::vector<int> result;
    for (unsigned n=0;n<count;++n) result.push_back(static_cast<int>(fixed[n]));
    std::sort(result.begin(),result.end());
    return result;
}

inline MarketPlan read_market_plan() {
    auto result = decode_market_plan(
        std::span<const std::byte>{cambc::draft_memory.data(),cambc::draft_memory.size()},
        cambc::map::width(),cambc::map::height());
    if (result.complete && result.actorOrdinals != market_fixed_roster(unsigned(result.actorOrdinals.size()))) return {};
    return result;
}

enum class DraftAllocationStatus { pending, published, unavailable };

// The draft process retains this ordinary private object across its picks.
// Work stops before the caller's valid pick. No partial assignment is written.
// One exact field is rooted at the canonical allied spawn. Group-to-altar routes
// deliberately go back through that hub, giving supported (often pessimistic)
// complete cycles without computing a second field during the short draft.
class DraftMarketAllocation {
    struct Group {
        int id = -1, gems = 0, members = 0, travel = StaticPaths::unknown;
        cambc::bitboard spawns;
        double health = 0, incoming = 0;
    };
    struct Actor { int ordinal; cambc::champion_stats stats; };
    bool initialized_ = false, metadata_complete_ = false, allocation_started_ = false;
    bool published_ = false, unavailable_ = false;
    unsigned truncated_ = 0;
    std::vector<cambc::map::camp_group> public_groups_;
    std::size_t metadata_at_ = 0, edge_at_ = 0;
    std::size_t member_at_ = 0;
    std::optional<Group> gathering_;
    std::vector<Group> groups_;
    std::vector<Actor> actors_;
    std::optional<cambc::distance_field> hub_;
    cambc::bitboard hub_visited_;
    std::vector<cambc::vec2> spawn_positions_, altar_positions_;
    std::size_t spawn_at_ = 0, altar_at_ = 0;
    bool routes_complete_ = false;
    int altar_distance_ = -1, spawn_relocation_ = 0;
    std::vector<plan3::AllocationEdge> edges_;
    plan3::Allocation allocation_;
    plan3::MarketScan scan_;
    // Stable bottom-up merge work, then one monotone pass through fixed rates.
    // Every unit can yield without losing its comparison/assignment frontier.
    std::vector<std::size_t> order_, scratch_;
    std::size_t checked_ = 0, width_ = 1, base_ = 0, left_ = 0, right_ = 0;
    std::size_t left_end_ = 0, right_end_ = 0, output_ = 0, priority_at_ = 0;
    bool merge_active_ = false, ordered_ = false, scan_fallback_ = false;

    int distance(cambc::vec2 p) const {
        if (!hub_visited_.is_set(p)) return StaticPaths::unreachable;
        const auto d = hub_->get(p);
        return d == cambc::distance_field::unreachable ? StaticPaths::unreachable : int(d);
    }
    int minimum(const cambc::bitboard& positions) const {
        int result = StaticPaths::unreachable;
        for (const auto p : positions.positions()) {
            const int d = distance(p);
            if (d >= 0 && (result < 0 || d < result)) result = d;
        }
        return result;
    }

    void initialize() {
        const auto roster = market_fixed_roster(cambc::draft::team_size());
        if (roster.empty() || cambc::constants::draft_memory_bytes != 1024) {
            unavailable_ = true; return;
        }
        for (const int ordinal : roster)
            actors_.push_back({ordinal,cambc::champion_designs[ordinal].base_stats()});
        const auto terrain = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
            | cambc::map::spawn_region(cambc::team::enemy);
        spawn_positions_ = cambc::map::spawn_region(cambc::team::ally).positions();
        if (spawn_positions_.empty()) { unavailable_ = true; return; }
        const auto hub = *std::min_element(spawn_positions_.begin(),spawn_positions_.end(),[](auto a,auto b) {
            return a.y == b.y ? a.x < b.x : a.y < b.y;
        });
        cambc::bitboard roots; roots.set(hub);
        // This SDK limit is a radius, not a work budget. The default requests
        // the complete public static field; native fuel still needs validation.
        hub_.emplace();hub_->compute(terrain,roots,hub_visited_);
        cambc::bitboard approaches;
        for (const auto altar : cambc::map::altars().positions())
            approaches.draw_chebyshev(altar,cambc::constants::submit_gems_range);
        altar_positions_ = approaches.positions();
        public_groups_ = cambc::map::camp_groups();
        initialized_ = true;
    }
    void metadata() {
        const int width = cambc::map::width();
        while (metadata_at_ < public_groups_.size()) {
            if (cambc::fuel_remaining() < 45000) return;
            const auto& design = public_groups_[metadata_at_];
            if (!design.gem_value()) { ++metadata_at_; continue; }
            if (!gathering_) {
                gathering_.emplace();
                gathering_->id = std::numeric_limits<int>::max();
                gathering_->gems = int(design.gem_value());
                member_at_ = 0;
            }
            auto& group = *gathering_;
            const auto members = design.camps();
            while (member_at_ < members.size()) {
                if (cambc::fuel_remaining() < 45000) return;
                const auto& camp = members[member_at_++];
                const auto p = camp.spawn_position();
                group.id = std::min(group.id,p.y*width+p.x);
                group.spawns.set(p);
                const auto stats = camp.base_stats();
                group.health += stats.base_health; group.incoming += stats.base_strength;
                ++group.members;
            }
            if (group.members) {
                // Retain exactly the same smallest 64 canonical keys without
                // a final unbounded all-map sort or retaining discarded groups.
                const auto at=std::lower_bound(groups_.begin(),groups_.end(),group.id,
                    [](const Group& g,int id){return g.id<id;});
                groups_.insert(at,std::move(group));
                if (groups_.size()>64) { ++truncated_;groups_.pop_back(); }
            }
            gathering_.reset(); ++metadata_at_;
        }
        metadata_complete_ = true;
    }
    void route_costs() {
        while (spawn_at_ < spawn_positions_.size()) {
            if (cambc::fuel_remaining() < 45000) return;
            const int d = distance(spawn_positions_[spawn_at_++]);
            if (d < 0) spawn_relocation_ = -1;
            else if (spawn_relocation_ >= 0) spawn_relocation_ = std::max(spawn_relocation_,d);
        }
        while (altar_at_ < altar_positions_.size()) {
            if (cambc::fuel_remaining() < 45000) return;
            const int d = distance(altar_positions_[altar_at_++]);
            if (d>=0 && (altar_distance_<0 || d<altar_distance_)) altar_distance_ = d;
        }
        routes_complete_ = true;
    }
    plan3::AllocationEdge estimate(const Actor& actor, Group& group, int index) const {
        plan3::AllocationEdge edge;
        edge.actorOrdinal = index; edge.groupId = group.id; edge.securedGems = group.gems;
        if (group.travel==StaticPaths::unknown) group.travel = minimum(group.spawns);
        const int travel = group.travel;
        const auto& stats = actor.stats;
        const auto clear = plan3::estimate_clear(group.health,group.incoming,stats.base_strength,
            stats.base_focus,stats.camp_damage_multiplier,1.0,stats.base_health,stats.base_health);
        const auto tribute = plan3::tribute_completion_turns(group.gems,
            cambc::constants::submit_gem_duration_multiplier,cambc::constants::submit_gem_base_duration,
            cambc::constants::submit_gem_minimum_duration);
        if (travel<0 || altar_distance_<0 || spawn_relocation_<0 || !clear.admitted || !tribute ||
            !(cambc::constants::spawn_healing>0) || stats.base_move_speed<=0) return edge;
        // Public full-health camps and aggregate exposure are analytical
        // assumptions. A connected group's traversal budgets a spanning walk.
        // Actual combat, channel timing and spawn relocation must be audited.
        const int recovery = std::max(1,int(std::ceil(clear.healthLoss /
            (stats.base_health*cambc::constants::spawn_healing))));
        const int camp_walk = std::max(0,2*(group.members-1));
        const int walk = int(std::ceil(double(2*travel+altar_distance_+2*spawn_relocation_+camp_walk) /
                                      stats.base_move_speed));
        edge.tau = walk + clear.turns + std::max(0,cambc::constants::capture_time) + 1 +
            *tribute + std::max(0,cambc::constants::recall_time) + recovery;
        edge.safe = edge.tau>0;
        return edge;
    }
    bool before(std::size_t a, std::size_t b) const {
        const auto& x=edges_[a]; const auto& y=edges_[b];
        const auto xy=std::int64_t(x.securedGems)*y.tau, yx=std::int64_t(y.securedGems)*x.tau;
        if (xy!=yx) return xy>yx;
        if (x.groupId!=y.groupId) return x.groupId<y.groupId;
        if (x.actorOrdinal!=y.actorOrdinal) return x.actorOrdinal<y.actorOrdinal;
        return a<b;
    }
    plan3::MarketWorkStatus priority_work() {
        if (checked_<edges_.size()) {
            const auto at=checked_++; const auto& e=edges_[at];
            if (!e.complete) return plan3::MarketWorkStatus::invalid;
            if (e.safe && e.tau>0 && e.tau<=128 && e.securedGems>0 &&
                e.actorOrdinal>=0 && std::size_t(e.actorOrdinal)<allocation_.actorLoads.size()) {
                order_.push_back(at);
                // Distinct positive rational rates with denominators <=128
                // differ by >=1/16384. For rewards <=60000 the model's maximum
                // relative tolerance is <=0.00006, strictly below that gap.
                // Exact-rate ties have identical floating division results.
                // Larger rewards may have nontransitive tolerance ties: retain
                // the original scan exactly instead of sorting that relation.
                if (e.securedGems>60000) scan_fallback_=true;
            }
            return plan3::MarketWorkStatus::progress;
        }
        if (scan_fallback_) return plan3::market_allocate_work(allocation_,edges_,scan_,8);
        if (!ordered_) {
            const auto n=order_.size();
            if (scratch_.size()!=n) scratch_.resize(n);
            if (width_>=n) { ordered_=true; return plan3::MarketWorkStatus::progress; }
            if (!merge_active_) {
                if (base_>=n) { order_.swap(scratch_);width_*=2;base_=0;return plan3::MarketWorkStatus::progress; }
                left_=base_;left_end_=std::min(base_+width_,n);
                right_=left_end_;right_end_=std::min(base_+2*width_,n);output_=base_;merge_active_=true;
            }
            if (left_<left_end_ && (right_==right_end_ || before(order_[left_],order_[right_])))
                scratch_[output_++]=order_[left_++];
            else scratch_[output_++]=order_[right_++];
            if (output_==right_end_) { base_=right_end_;merge_active_=false; }
            return plan3::MarketWorkStatus::progress;
        }
        if (priority_at_==order_.size()) {
            allocation_.complete=true;return plan3::MarketWorkStatus::complete;
        }
        const auto& e=edges_[order_[priority_at_++]];
        const auto found=std::lower_bound(allocation_.consideredGroups.begin(),allocation_.consideredGroups.end(),e.groupId);
        if (found==allocation_.consideredGroups.end() || *found!=e.groupId) return plan3::MarketWorkStatus::progress;
        const auto g=std::size_t(found-allocation_.consideredGroups.begin()),a=std::size_t(e.actorOrdinal);
        if (allocation_.ownerOrdinals[g]<0 && allocation_.actorLoads[a]+e.tau<=128) {
            allocation_.ownerOrdinals[g]=e.actorOrdinal;allocation_.actorLoads[a]+=e.tau;
            return plan3::MarketWorkStatus::assigned;
        }
        return plan3::MarketWorkStatus::progress;
    }
    bool publish() {
        MarketPlan plan;
        plan.complete = allocation_.complete;
        plan.width = cambc::map::width(); plan.height = cambc::map::height();
        for (const auto& actor : actors_) plan.actorOrdinals.push_back(actor.ordinal);
        plan.groupIds = allocation_.consideredGroups;
        for (const int index : allocation_.ownerOrdinals)
            plan.ownerOrdinals.push_back(index<0 ? -1 : actors_[std::size_t(index)].ordinal);
        const auto bytes = encode_market_plan(plan);
        if (!bytes) { unavailable_ = true; return false; }
        cambc::draft_memory = *bytes;
        published_ = true;
        return true;
    }
public:
    DraftAllocationStatus step() {
        if (published_) return DraftAllocationStatus::published;
        if (unavailable_ || !cambc::is_draft()) return DraftAllocationStatus::unavailable;
        if (!initialized_) {
            if (cambc::fuel_remaining()<45000) return DraftAllocationStatus::pending;
            initialize();
        }
        if (unavailable_) return DraftAllocationStatus::unavailable;
        if (!metadata_complete_) metadata();
        if (!metadata_complete_ || cambc::fuel_remaining()<45000)
            return DraftAllocationStatus::pending;
        if (!routes_complete_) route_costs();
        if (!routes_complete_) return DraftAllocationStatus::pending;
        const auto edge_count = groups_.size()*actors_.size();
        while (edge_at_<edge_count) {
            if (cambc::fuel_remaining()<45000) return DraftAllocationStatus::pending;
            const auto g = edge_at_/actors_.size(), a = edge_at_%actors_.size();
            edges_.push_back(estimate(actors_[a],groups_[g],int(a)));
            ++edge_at_;
        }
        if (!allocation_started_) {
            std::vector<int> ids; for (const auto& group : groups_) ids.push_back(group.id);
            allocation_ = plan3::market_allocation_start(std::move(ids),int(actors_.size()));
            allocation_started_ = true;
        }
        // Fixed rates and monotone loads make already-ineligible edges remain
        // ineligible. Merge/visit units replace repeated whole-menu rescans.
        // The exceptional tolerance fallback scans <=8 edges per unit.
        for (unsigned call=0;call<4096 && cambc::fuel_remaining()>=45000;++call) {
            const auto state = priority_work();
            if (state==plan3::MarketWorkStatus::invalid) {
                unavailable_ = true; return DraftAllocationStatus::unavailable;
            }
            if (state==plan3::MarketWorkStatus::complete)
                return publish() ? DraftAllocationStatus::published : DraftAllocationStatus::unavailable;
        }
        return DraftAllocationStatus::pending;
    }
    bool published() const { return published_; }
    unsigned truncated_groups() const { return truncated_; }
    unsigned considered_groups() const { return unsigned(groups_.size()); }
    bool uses_scan_fallback() const { return scan_fallback_; }
};

} // namespace plan3_runtime
