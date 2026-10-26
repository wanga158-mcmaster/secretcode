#pragma once

#include "pure.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#ifndef PLAN4_OBSERVER_ACTOR_PACKETS
#define PLAN4_OBSERVER_ACTOR_PACKETS 0
#endif
#ifndef PLAN4_OBSERVER_LOCAL_MASKS
#define PLAN4_OBSERVER_LOCAL_MASKS 0
#endif

namespace plan4 {

struct LifecycleObservation { Lifecycle state = Lifecycle::unknown; int timer = -1; };
struct MemberObservation { float health = 0, incoming = 0; bool exact = false; };

enum class ObserverStage : int {
    none, admission, initialization, own, public_groups, public_altars,
    collection, group, visibility, lifecycle, members, occupancy, recall,
    altar_admission, altars
};
struct ObserverDiagnostics {
    int stop_stage = 0, stop_group = -1, stop_member = -1, groups = 0, visited = 0;
    std::uint64_t collection_fuel = 0, stop_floor = 0;
    std::int64_t stop_fuel = -1;
    unsigned stop_margin = 0;
};

// The provider's live methods are deliberately behind visibility tests. Tests
// use a provider which rejects every unauthorized dynamic query.
template<class Provider>
constexpr Group observe_group(const Group& public_group, Provider& provider, int round) {
    Group result = public_group;
    result.visible = provider.visible();
    // A nonempty group with no visible spawn cannot have every spawn visible.
    result.all_spawns_visible = result.visible ? provider.all_spawns_visible() : provider.member_count() == 0;
    result.occupancy_known = result.all_spawns_visible;
    result.lifecycle = Lifecycle::unknown;
    result.provenance = {-1, Source::public_layout, Status::unknown};
    result.health_provenance = {-1, Source::public_layout, Status::unknown};
    result.health = result.full_health;
    result.incoming_damage = result.full_incoming_damage;
    result.clear_rounds = result.full_clear_rounds;
    result.reward_available = false;
    result.contested = result.occupied_spawn = false;
    result.eligible_allied_occupants = 1;
    result.own_is_lowest_occupant = true;
    result.respawn_remaining = -1;
    result.capture_started_round = -1;
    if (!result.visible) return result;
    const auto lifecycle = provider.read_lifecycle();
    if (lifecycle.state == Lifecycle::unknown) return result;
    result.lifecycle = lifecycle.state;
    result.provenance = {round, Source::personal_vision, Status::exact_now};
    result.respawn_remaining = lifecycle.timer;
    if (lifecycle.state == Lifecycle::cleared || lifecycle.state == Lifecycle::respawning) {
        result.health = result.incoming_damage = 0;
        result.reward_available = lifecycle.state == Lifecycle::cleared;
        result.health_provenance = {round, Source::personal_vision, Status::exact_now};
        return result;
    }
    result.health = result.incoming_damage = 0;
    bool all_exact = true;
    for (int member = 0; member < provider.member_count(); ++member) {
        if constexpr (requires { provider.can_continue(); }) {
            if (!provider.can_continue()) {
                result.health = result.full_health;
                result.incoming_damage = result.full_incoming_damage;
                return result;
            }
        }
        MemberObservation observed = provider.public_member(member);
        if (provider.member_visible(member)) observed = provider.read_member(member);
        result.health += observed.health;
        result.incoming_damage += observed.incoming;
        all_exact &= observed.exact;
    }
    if (all_exact)
        result.health_provenance = {round, Source::personal_vision, Status::exact_now};
    return result;
}

inline int clear_quote(float health, float damage_per_turn) {
    if (health <= 0) return 0;
    return damage_per_turn > 0 ? int(std::ceil(health / damage_per_turn)) : -1;
}

// Reuse actor-owned storage without touching unused fixed-capacity slots.
// A failed collection exposes zero groups/altars and no old physical channel.
inline void begin_observation(Information& result, int round, TravelLookup travel) {
    result.round = round;
    result.own = Actor{};
    result.own.alive = false;
    result.own_spawn = {};
    result.our_score = result.enemy_score = 0;
    result.group_count = result.altar_count = 0;
    result.travel = travel;
    result.work_budget = {};
    result.in_progress_group_id = -1;
    result.retained_group_id = result.retained_second_group_id = result.retained_altar_id = -1;
    result.observation_truncated = true;
    result.public_layout_truncated = true;
}

struct ObservationOrder {
    std::array<int, max_groups> indices;
    int count = 0;
};
inline ObservationOrder observation_order(const Information& information) {
    ObservationOrder result;
    std::array<int, max_groups> lower_distance;
    const int count = std::max(0, std::min(max_groups, information.group_count));
    for (int g = 0; g < count; ++g) {
        result.indices[g] = g;
        const Point p = information.groups[g].position, own = information.own.position;
        lower_distance[g] = std::max(std::abs(p.x - own.x), std::abs(p.y - own.y));
    }
    const int near = std::min(8, count);
    std::partial_sort(result.indices.begin(), result.indices.begin() + near,
        result.indices.begin() + count, [&](int a, int b) {
            return lower_distance[a] != lower_distance[b] ? lower_distance[a] < lower_distance[b]
                : point_less(information.groups[a].position, information.groups[b].position);
        });
    std::array<bool, max_groups> selected;
    for (int g = 0; g < count; ++g) selected[g] = false;
    for (int n = 0; n < near; ++n) selected[result.indices[n]] = true;
    result.count = near;
    for (int g = 0; g < count; ++g) if (!selected[g]) result.indices[result.count++] = g;
    return result;
}

} // namespace plan4

#ifndef MODEL4_OBSERVER_NO_SDK
#include "cambc.h"
#include "model-runtime.h"
#include <memory>
namespace plan4 {

#if PLAN4_OBSERVER_LOCAL_MASKS
// One invocation's immutable SDK mask. The pinned export is packed row-major,
// least-significant bit first, exactly as SpatialEvidence already consumes it.
// Retain the original SDK test for invalid coordinates or unsupported layouts,
// including its error semantics. No live body or mask survives observation.
class ObserverMaskSnapshot {
    const cambc::bitboard& source_;
    std::vector<std::uint8_t> bytes_;
    int width_, height_;
public:
    ObserverMaskSnapshot(const cambc::bitboard& source, int width, int height)
        : source_(source), width_(width), height_(height) {
        const auto cells = std::int64_t(width) * height;
        if (width <= 0 || height <= 0 || cells > 16384 ||
            std::size_t((cells + 7) / 8) != cambc::bitboard::linear_buffer_size()) return;
        bytes_.resize(std::size_t((cells + 7) / 8));
        source_.export_linear_buffer(bytes_);
    }
    bool is_set(cambc::vec2 p) const {
        if (bytes_.empty() || p.x < 0 || p.y < 0 || p.x >= width_ || p.y >= height_)
            return source_.is_set(p);
        const unsigned key = unsigned(p.y * width_ + p.x);
        return (bytes_[key >> 3] & (1u << (key & 7))) != 0;
    }
    std::vector<cambc::vec2> positions() const { return source_.positions(); }
};
#endif

class OnlineObserver {
    struct Metadata {
        cambc::map::camp_group sdk;
        Group group;
        std::vector<MemberObservation> members;
        std::vector<int> ranges;
    };
    std::vector<Metadata> groups_;
    std::vector<Altar> altars_;
    std::vector<cambc::map::camp_group> pending_layout_;
    std::vector<cambc::vec2> pending_altars_;
    static constexpr std::size_t camp_type_count_ = std::size_t(cambc::camp_type::golem) + 1;
    std::array<cambc::camp_stats, camp_type_count_> public_stats_{};
    std::array<bool, camp_type_count_> stats_loaded_{};
    cambc::bitboard public_blocked_{cambc::uninitialized};
    std::size_t layout_cursor_ = 0;
    int width_ = 0, height_ = 0;
    bool layout_loaded_ = false, altar_layout_loaded_ = false;
    std::uint64_t fuel_floor_ = 65000;
    Point own_spawn_{};
    bool initialized_ = false, truncated_ = false;
    mutable ObserverDiagnostics diagnostics_{};
    struct ModelLayoutStorage {
        std::array<runtime::PublicGroup, 8> groups;
        std::array<runtime::PublicMember, 8 * 32> members;
    };
    std::unique_ptr<ModelLayoutStorage> model_layout_;

    static Point point(cambc::vec2 p) { return {p.x, p.y}; }
    static cambc::vec2 tile(Point p) { return {p.x, p.y}; }
    static int chebyshev(Point a, Point b) {
        return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
    }
    bool has_budget(unsigned margin = 2000, ObserverStage stage = ObserverStage::none,
        int group = -1, int member = -1) const {
        const auto fuel = cambc::fuel_remaining();
        const bool allowed = fuel >= fuel_floor_ + margin;
        if (!allowed && stage != ObserverStage::none && diagnostics_.stop_stage == 0) {
            diagnostics_.stop_stage = int(stage);
            diagnostics_.stop_group = group; diagnostics_.stop_member = member;
            diagnostics_.groups = int(groups_.size());
            diagnostics_.stop_fuel = static_cast<std::int64_t>(fuel);
            diagnostics_.stop_floor = fuel_floor_; diagnostics_.stop_margin = margin;
        }
        return allowed;
    }
    bool initialize() {
        if (!layout_loaded_) {
            if (!has_budget(15000, ObserverStage::initialization)) return false;
            pending_layout_ = cambc::map::camp_groups();
            truncated_ = pending_layout_.size() > max_groups;
            layout_loaded_ = true;
        }
        const std::size_t count = std::min(pending_layout_.size(), std::size_t(max_groups));
        while (layout_cursor_ < count) {
            if (!has_budget(12000, ObserverStage::initialization)) return false;
            if (groups_.size() == layout_cursor_) {
                auto sdk = std::move(pending_layout_[layout_cursor_]);
                Group group;
                group.id = int(static_cast<cambc::handle_camp_group_t>(sdk));
                group.position = point(sdk.camps().front().spawn_position());
                group.gems = sdk.gem_value(); group.gold = sdk.gold_value();
                group.respawn_rounds = sdk.max_respawn_timer();
                groups_.push_back({std::move(sdk), group, {}, {}});
            }
            auto& cached = groups_.back();
            while (cached.members.size() < cached.sdk.camps().size()) {
                if (!has_budget(12000, ObserverStage::initialization)) return false;
                const auto& monster = cached.sdk.camps()[cached.members.size()];
                const int kind = int(monster.kind());
                if (kind < 0 || std::size_t(kind) >= public_stats_.size()) {
                    truncated_ = true;
                    return false;
                }
                if (!stats_loaded_[kind]) { public_stats_[kind] = monster.base_stats(); stats_loaded_[kind] = true; }
                const auto design = public_stats_[kind];
                cached.members.push_back({design.base_health, design.base_strength, false});
                cached.ranges.push_back(design.base_attack_range);
                cached.group.full_health += design.base_health;
                cached.group.incoming_damage += design.base_strength;
                cached.group.full_incoming_damage += design.base_strength;
            }
            cached.group.health = cached.group.full_health;
            ++layout_cursor_;
        }
        if (!altar_layout_loaded_) {
            if (!has_budget(12000, ObserverStage::initialization)) return false;
            const auto spawns = cambc::map::spawn_region(cambc::team::ally).positions();
            if (!spawns.empty()) own_spawn_ = point(spawns.front());
            public_blocked_ = cambc::map::walls() | cambc::map::holes()
                | cambc::map::altars() | cambc::map::spawn_region(cambc::team::enemy);
            pending_altars_ = cambc::map::altars().positions();
            width_ = cambc::map::width(); height_ = cambc::map::height();
            truncated_ |= pending_altars_.size() > max_altars;
            altar_layout_loaded_ = true;
        }
        while (altars_.size() < std::min(pending_altars_.size(), std::size_t(max_altars))) {
            if (!has_budget(12000, ObserverStage::initialization)) return false;
            const auto altar = pending_altars_[altars_.size()];
            Altar entry;
            entry.id = int(altars_.size()); entry.altar_position = point(altar);
            entry.accessible = false;
            const int range = cambc::constants::submit_gems_range;
            for (int dx = -range; dx <= range; ++dx) for (int dy = -range; dy <= range; ++dy) {
                const cambc::vec2 candidate{altar.x + dx, altar.y + dy};
                if (candidate.x < 0 || candidate.y < 0 || candidate.x >= width_ || candidate.y >= height_
                    || public_blocked_.is_set(candidate)) continue;
                const Point p = point(candidate);
                if (!entry.accessible || point_less(p, entry.position)) entry.position = p;
                entry.accessible = true;
            }
            altars_.push_back(entry);
        }
        initialized_ = true;
        return true;
    }

    struct Provider {
        const Metadata& metadata;
#if PLAN4_OBSERVER_LOCAL_MASKS
        const ObserverMaskSnapshot& vision;
#else
        const cambc::bitboard& vision;
#endif
        bool* truncated;
        const OnlineObserver& observer;
        bool budget(ObserverStage stage = ObserverStage::members, int member = -1) const {
            if (observer.has_budget(2000, stage, metadata.group.id, member)) return true;
            *truncated = true; return false;
        }
        bool can_continue() const { return budget(); }
        bool visible() const {
            int member = 0;
            for (const auto& monster : metadata.sdk) {
                if (!budget(ObserverStage::visibility, member++)) return false;
                if (vision.is_set(monster.spawn_position())) return true;
            }
            return false;
        }
        bool all_spawns_visible() const {
            int member = 0;
            for (const auto& monster : metadata.sdk) {
                if (!budget(ObserverStage::visibility, member++)) return false;
                if (!vision.is_set(monster.spawn_position())) return false;
            }
            return true;
        }
        LifecycleObservation read_lifecycle() const {
            if (!budget(ObserverStage::lifecycle)) return {};
            const auto state = metadata.sdk.state();
            if (const auto* respawn = std::get_if<cambc::map::camp_group_respawning>(&state))
                return {Lifecycle::respawning, respawn->timer};
            return {std::holds_alternative<cambc::map::camp_group_reward_available>(state)
                ? Lifecycle::cleared : Lifecycle::living, -1};
        }
        int member_count() const { return int(metadata.members.size()); }
        MemberObservation public_member(int member) const { return metadata.members[member]; }
        bool member_visible(int member) const {
            return budget(ObserverStage::members, member) && vision.is_set(metadata.sdk.camps()[member].spawn_position());
        }
        MemberObservation read_member(int member) const {
            if (!budget(ObserverStage::members, member)) return metadata.members[member];
            const auto& monster = metadata.sdk.camps()[member];
            if (monster.state().is_dead()) return {0, 0, true};
            // Both SDK getters fetch this same packet. Keep it within this
            // visible read; state() still owns the separate dead-body gate.
            cambc::detail::camp_state packet{};
            cambc::detail::check(cambc::detail::sdk_camp(
                std::uint32_t(static_cast<cambc::handle_camp_t>(monster)), &packet));
            return {packet.health, packet.strength, true};
        }
    };

public:
    const ObserverDiagnostics& diagnostics() const { return diagnostics_; }
    // Immutable public composition for the already-frozen menu, bounded to
    // eight physical sources and32members/source. No live HP/occupant query.
    runtime::PublicLayoutFrame layout_for_menu(const Information& info, const Menu& menu) {
        runtime::PublicLayoutFrame frame;
        frame.clock = {info.round, {info.own.id}};
        if (!initialized_ || info.public_layout_truncated || menu.single_count < 0 || menu.single_count > 8 ||
            menu.count < menu.single_count || menu.count > max_jobs) { frame.truncated = true; return frame; }
        if (!has_budget(6000)) { frame.truncated = true; return frame; }
        if (!model_layout_) model_layout_ = std::make_unique<ModelLayoutStorage>();
        frame.groups = model_layout_->groups.data(); frame.members = model_layout_->members.data();
        for (int n = 0; n < menu.single_count; ++n) {
            const int index = menu.jobs[n].group;
            if (index < 0 || index >= int(groups_.size()) || index >= info.group_count ||
                groups_[index].group.id != info.groups[index].id) { frame.truncated = true; continue; }
            bool duplicate = false;
            for (int g = 0; g < frame.group_count; ++g) duplicate |= model_layout_->groups[g].group_id == info.groups[index].id;
            if (duplicate) continue;
            auto& target = model_layout_->groups[frame.group_count++];
            target = {info.groups[index].id, frame.member_count, 0, false};
            const auto& source = groups_[index];
            if (source.members.size() != source.sdk.camps().size() || source.members.size() > 32 || source.members.empty()) {
                frame.truncated = true; continue;
            }
            for (int m = 0; m < int(source.members.size()); ++m) {
                if (!has_budget(2000) || (info.work_budget.can_refine && !info.work_budget.can_refine(info.work_budget.context))) {
                    frame.truncated = true; return frame;
                }
                const auto& monster = source.sdk.camps()[m];
                model_layout_->members[frame.member_count++] = {target.group_id,
                    int(static_cast<cambc::handle_camp_t>(monster)), m, int(source.members.size()), int(monster.kind()),
                    source.ranges[m], point(monster.spawn_position()), source.members[m].health};
                ++target.count;
            }
            target.complete = true;
        }
        if (int(cambc::round_number()) != info.round) { frame.group_count = frame.member_count = 0; frame.truncated = true; }
        return frame;
    }
    bool observe_into(Information& result, TravelLookup travel = {}, unsigned fuel_floor = 65000) {
        diagnostics_ = {}; diagnostics_.groups = int(groups_.size());
        // Respect the caller's planning reserve, with the common 10k
        // bookkeeping margin above the 30k execution/yield target.
        fuel_floor_ = std::max(std::uint64_t(40000), std::uint64_t(fuel_floor));
        // The dynamic collection cap expires with this observation. Later
        // immutable layout preparation must use the caller's planning reserve.
        struct RestoreFuelFloor {
            std::uint64_t& floor;
            std::uint64_t caller_floor;
            ~RestoreFuelFloor() { floor = caller_floor; }
        } restore_floor{fuel_floor_, fuel_floor_};
        begin_observation(result, int(cambc::round_number()), travel);
        if (!has_budget(12000, ObserverStage::admission) || (!initialized_ && !initialize())) return false;
        diagnostics_.groups = int(groups_.size());
        result.own_spawn = own_spawn_; result.observation_truncated = truncated_;
        result.public_layout_truncated = truncated_;
        auto& settings = result.settings;
        settings.max_rounds = int(cambc::constants::max_rounds);
        settings.winning_gems = cambc::constants::winning_gems;
        settings.capture_rounds = cambc::constants::capture_time;
        settings.recall_rounds = cambc::constants::recall_time;
        settings.tribute_multiplier = cambc::constants::submit_gem_duration_multiplier;
        settings.tribute_base = cambc::constants::submit_gem_base_duration;
        settings.tribute_minimum = cambc::constants::submit_gem_minimum_duration;
        settings.spawn_heal_fraction = cambc::constants::spawn_healing;
        result.our_score = cambc::global_submitted_gems(cambc::team::ally);
        result.enemy_score = cambc::global_submitted_gems(cambc::team::enemy);
        auto& me = cambc::self();
        auto& own = result.own;
        own.id = int(static_cast<cambc::handle_champion_t>(me));
        own.alive = me.is_alive();
        cambc::detail::champion_info own_packet{};
        cambc::detail::check(cambc::detail::sdk_champion(
            std::uint32_t(static_cast<cambc::handle_champion_t>(me)), &own_packet));
        own.position = {own_packet.x, own_packet.y};
        own.health = own_packet.health; own.maximum_health = own_packet.max_health;
        own.bag = me.gem_count(); own.gold = me.gold_count();
        const auto state = cambc::champion_state::from_bits_retain(own_packet.state);
        if (state.contains(cambc::champion_state::recalling)) own.channel = Channel::recall;
        else if (state.contains(cambc::champion_state::submitting_gems)) own.channel = Channel::tribute;
        else if (state.contains(cambc::champion_state::teleporting)) own.channel = Channel::other;
        if (own.channel != Channel::none) own.channel_ticks = me.remaining_channel_time();
        own.on_spawn = own.alive && cambc::map::spawn_region(cambc::team::ally).is_set(tile(own.position));
        if (!has_budget(8000, ObserverStage::own)) { result.observation_truncated = true; return false; }
        const auto items = me.item_count();
        const bool monster = items[cambc::item_type::monster_emblem_of_strength] > 0;
        const bool jungle = items[cambc::item_type::jungle_emblem_of_health] > 0;
        const float monster_factor = monster ? 1 + cambc::item_type::monster_emblem_of_strength.passive_magnitude() : 1;
        const float jungle_factor = jungle ? 1 - cambc::item_type::jungle_emblem_of_health.passive_magnitude() : 1;
        const float dps = (own_packet.strength * 1.125f + own_packet.focus * 0.12f) * monster_factor;
#if PLAN4_OBSERVER_LOCAL_MASKS
        if (!has_budget(8000, ObserverStage::own)) { result.observation_truncated = true; return false; }
        const auto vision_source = cambc::vision::visible_tiles();
        const ObserverMaskSnapshot vision(vision_source, width_, height_);
        if (!has_budget(8000, ObserverStage::own)) { result.observation_truncated = true; return false; }
        const auto enemy_source = cambc::vision::champion_tiles(cambc::team::enemy);
        const ObserverMaskSnapshot enemies(enemy_source, width_, height_);
#else
        const auto vision = cambc::vision::visible_tiles();
        const auto enemies = cambc::vision::champion_tiles(cambc::team::enemy);
#endif
        struct AllyPosition { cambc::vec2 position; int id; };
        std::vector<AllyPosition> allies;
#if PLAN4_OBSERVER_ACTOR_PACKETS
        // Same current visible-mask enumeration as vision::champions(), without
        // constructing ability/inventory wrappers that this observer never uses.
        // No packet survives this invocation, and all live errors still escape.
        const auto packet_at = [](cambc::vec2 p) {
            std::uint32_t handle{};
            cambc::detail::check(cambc::detail::sdk_champion_at(p.x, p.y, &handle));
            cambc::detail::champion_info packet{};
            cambc::detail::check(cambc::detail::sdk_champion(handle, &packet));
            return std::pair{handle, packet};
        };
        for (const auto p : cambc::vision::champion_tiles(cambc::team::ally).positions()) {
            if (!has_budget(8000, ObserverStage::own)) { result.observation_truncated = true; return false; }
            const auto [handle, packet] = packet_at(p);
            allies.push_back({{packet.x, packet.y}, int(handle)});
        }
        own.recall_safe = own.alive && !result.observation_truncated;
        // No action occurs between the mask above and this read. Reuse only
        // this invocation's existing current enemy mask, never an actor frame.
        for (const auto p : enemies.positions()) {
            if (!has_budget(8000, ObserverStage::own)) { result.observation_truncated = true; own.recall_safe = false; return false; }
            const auto [handle, packet] = packet_at(p); (void)handle;
            if (chebyshev(own.position, {packet.x, packet.y}) <= packet.attack_range) own.recall_safe = false;
        }
#else
        for (const auto& ally : cambc::vision::champions(cambc::team::ally))
            allies.push_back({ally.position(), int(static_cast<cambc::handle_champion_t>(ally))});
        own.recall_safe = own.alive && !result.observation_truncated;
        for (const auto& enemy : cambc::vision::champions(cambc::team::enemy))
            if (chebyshev(own.position, point(enemy.position())) <= enemy.attack_range()) own.recall_safe = false;
#endif
        // Only represented slots are reset. Capacity beyond these counts is
        // never read, copied, or initialized by a per-turn collection.
        for (int index = 0; index < int(groups_.size()); ++index) {
            if (!has_budget(3000, ObserverStage::public_groups, groups_[index].group.id)) { result.observation_truncated = true; return false; }
            const auto& metadata = groups_[index];
            auto& entry = result.groups[index];
            entry = metadata.group;
            entry.full_clear_rounds = clear_quote(entry.full_health, dps);
            entry.clear_rounds = entry.full_clear_rounds;
            entry.clear_damage_per_round = dps;
            entry.incoming_damage *= jungle_factor;
            entry.full_incoming_damage *= jungle_factor;
            ++result.group_count;
        }
        for (int index = 0; index < int(altars_.size()); ++index) {
            if (!has_budget(2000, ObserverStage::public_altars)) { result.observation_truncated = true; return false; }
            result.altars[index] = altars_[index]; ++result.altar_count;
        }
        // Own state and public metadata are preparation. Starting the dynamic
        // cap before that work can leave less than its 8k admission margin,
        // causing an otherwise complete public prefix to truncate every turn.
        const auto collection_fuel = cambc::fuel_remaining();
        diagnostics_.collection_fuel = collection_fuel;
        // The allowance includes the 6k group admission margin; the caller's
        // execution floor still wins when less collection fuel is available.
        constexpr std::uint64_t collection_limit = 26000;
        if (collection_fuel > collection_limit)
            fuel_floor_ = std::max(fuel_floor_, collection_fuel - collection_limit);
        if (!has_budget(8000, ObserverStage::collection)) {
            result.observation_truncated = true; own.recall_safe = false; return true;
        }
        const auto order = observation_order(result);
        for (int query = 0; query < order.count; ++query) {
            const int index = order.indices[query];
            const auto& metadata = groups_[index];
            auto& entry = result.groups[index];
            if (!has_budget(6000, ObserverStage::group, metadata.group.id)) { result.observation_truncated = true; break; }
            if (!diagnostics_.stop_stage) ++diagnostics_.visited;
            Provider provider{metadata, vision, &result.observation_truncated, *this};
            entry = observe_group(metadata.group, provider, result.round);
            entry.incoming_damage *= jungle_factor;
            entry.full_incoming_damage *= jungle_factor;
            entry.clear_rounds = clear_quote(entry.health, dps);
            entry.clear_damage_per_round = dps;
            entry.full_clear_rounds = clear_quote(entry.full_health, dps);
            entry.eligible_allied_occupants = 0;
            for (const auto& member : metadata.sdk) {
                const int member_index = int(&member - metadata.sdk.camps().data());
                if (!has_budget(2000, ObserverStage::occupancy, metadata.group.id, member_index)) {
                    result.observation_truncated = true;
                    entry.occupancy_known = false;
                    break;
                }
                const auto p = member.spawn_position();
                const bool enemy_here = enemies.is_set(p);
                entry.contested |= enemy_here;
                entry.occupied_spawn |= enemy_here;
                for (const auto& ally : allies) if (ally.position == p) {
                    ++entry.eligible_allied_occupants;
                    entry.occupied_spawn = true;
                    if (ally.id < own.id)
                        entry.own_is_lowest_occupant = false;
                }
                if (own.alive && point(p) == own.position && entry.reward_available)
                    result.in_progress_group_id = entry.id;
                if (metadata.members[member_index].incoming > 0
                    && chebyshev(own.position, point(p)) <= metadata.ranges[member_index]
                    && entry.lifecycle != Lifecycle::cleared && entry.lifecycle != Lifecycle::respawning) {
                    // Unknown nearby monsters cannot certify a safe recall.
                    if (!provider.member_visible(member_index)
                        || !has_budget(2000, ObserverStage::recall, metadata.group.id, member_index) || !member.state().is_dead())
                        own.recall_safe = false;
                }
            }
            entry.eligible_allied_occupants = std::max(1u, entry.eligible_allied_occupants);
        }
        if (result.observation_truncated) own.recall_safe = false;
        if (!has_budget(4000, ObserverStage::altar_admission)) { result.observation_truncated = true; own.recall_safe = false; return true; }
#if PLAN4_OBSERVER_LOCAL_MASKS
        if (!has_budget(8000, ObserverStage::altar_admission)) { result.observation_truncated = true; own.recall_safe = false; return true; }
        const auto hostile_source = cambc::vision::altar_lock_tiles(cambc::team::enemy);
        const ObserverMaskSnapshot hostile_locks(hostile_source, width_, height_);
        if (!has_budget(8000, ObserverStage::altar_admission)) { result.observation_truncated = true; own.recall_safe = false; return true; }
        const auto allied_source = cambc::vision::altar_lock_tiles(cambc::team::ally);
        const ObserverMaskSnapshot allied_locks(allied_source, width_, height_);
#else
        const auto hostile_locks = cambc::vision::altar_lock_tiles(cambc::team::enemy);
        const auto allied_locks = cambc::vision::altar_lock_tiles(cambc::team::ally);
#endif
        for (int index = 0; index < result.altar_count; ++index) {
            if (!has_budget(2000, ObserverStage::altars)) { result.observation_truncated = true; break; }
            auto& entry = result.altars[index]; entry = altars_[index];
            if (vision.is_set(tile(entry.altar_position))) {
                entry.lock_known = true;
                entry.hostile_lock = hostile_locks.is_set(tile(entry.altar_position))
                    && !allied_locks.is_set(tile(entry.altar_position));
            }
        }
        if (result.observation_truncated) own.recall_safe = false;
        return true;
    }
};

} // namespace plan4
#endif
