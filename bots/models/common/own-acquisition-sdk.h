#pragma once
#include "own-acquisition.h"
#include <cambc.h>
#include <algorithm>
#include <vector>

namespace plan4::runtime {
// A live reward-recipient API is unavailable. This adapter isolates a measured
// own share using consecutive owner boundaries. Enemy bodies remain hidden:
// only the complete public roster's alive flags are queried.
class OwnAcquisitionCollector {
    OwnAcquisitionTracker tracker_;
    std::vector<cambc::champion> enemies_;
    bool roster_loaded_ = false, roster_valid_ = false;
    int last_start_ = -1;
    unsigned floor_ = 0;

    bool budget(int round) const {
        return cambc::round_number() == unsigned(round) &&
            cambc::fuel_remaining() >= std::uint64_t(floor_) + 800;
    }
    void load_roster() {
        enemies_ = cambc::champions(cambc::team::enemy);
        roster_loaded_ = true;
        if (enemies_.empty() || enemies_.size() > 16) return;
        std::sort(enemies_.begin(), enemies_.end(), [](const auto& a, const auto& b) {
            return int(static_cast<cambc::handle_champion_t>(a)) <
                int(static_cast<cambc::handle_champion_t>(b));
        });
        int previous = -1;
        for (const auto& enemy : enemies_) {
            const int id = int(static_cast<cambc::handle_champion_t>(enemy));
            if (id < 0 || id <= previous) return;
            previous = id;
        }
        roster_valid_ = true;
    }
    template<class Entries>
    OwnStockBoundary sample(const Entries& entries, const std::vector<int>& lookup,
                            int width, unsigned minimum) {
        OwnStockBoundary out;
        const auto round = cambc::round_number();
        if (round > unsigned(std::numeric_limits<int>::max())) return out;
        out.round = int(round);
        const auto before = cambc::fuel_remaining();
        floor_ = unsigned(std::max<std::uint64_t>(minimum, before > 4000 ? before - 4000 : minimum));
        if (!budget(out.round)) return out;
        const auto& own = cambc::self();
        out.actor = int(static_cast<cambc::handle_champion_t>(own));
        out.alive = own.is_alive();
        if (!out.alive || !budget(out.round)) return out;
        const auto position = own.position();
        if (!budget(out.round)) return out;
        out.bag = own.gem_count();
        if (!budget(out.round)) return out;
        const auto state = own.state();
        out.position = {position.x, position.y};
        out.channel_none = !state.contains(cambc::champion_state::recalling) &&
            !state.contains(cambc::champion_state::submitting_gems) &&
            !state.contains(cambc::champion_state::teleporting);
        out.respawn_time = cambc::constants::respawn_time;
        out.own_provenance = {out.round, Source::own_state, Status::exact_now};
        out.complete = cambc::round_number() == round;

        // Validate the current spawn independently of the lookup. Incomplete
        // public scans leave attribution unknown without discarding own stock.
        int index = -1;
        if (width > 0 && lookup.size() % unsigned(width) == 0 && position.x >= 0 &&
            position.x < width && position.y >= 0 &&
            std::size_t(position.y) < lookup.size() / unsigned(width))
            index = lookup[std::size_t(position.y) * unsigned(width) + unsigned(position.x)];
        if (index >= 0 && index < int(entries.size()) && entries.size() <= max_groups &&
            entries[index].spawns.is_set(position) && out.channel_none && budget(out.round)) {
            bool unique = true, complete = true;
            for (int n = 0; n < int(entries.size()); ++n) {
                if (n % 16 == 0 && !budget(out.round)) { complete = false; break; }
                if (n != index && entries[n].spawns.is_set(position)) { unique = false; break; }
            }
            if (unique && complete && budget(out.round)) {
                const auto vision = cambc::vision::visible_tiles();
                if (vision.is_set(position) && budget(out.round)) {
                    const auto& group = entries[index].group;
                    const auto lifecycle = group.state();
                    const auto gems = group.gem_value();
                    if (gems <= unsigned(std::numeric_limits<int>::max())) out.group_gems = int(gems);
                    out.group = {out.actor, out.round, int(static_cast<cambc::handle_camp_group_t>(group)),
                        group.max_respawn_timer(),
                        std::holds_alternative<cambc::map::camp_group_alive>(lifecycle) ? Lifecycle::living :
                        std::holds_alternative<cambc::map::camp_group_reward_available>(lifecycle) ? Lifecycle::cleared :
                        Lifecycle::respawning, {out.round, Source::personal_vision, Status::exact_now}, true, true};
                    out.on_unique_spawn = true;
                }
            }
        }
        if (out.on_unique_spawn && budget(out.round)) {
            if (!roster_loaded_) load_roster();
            if (roster_valid_ && budget(out.round)) {
                out.enemies_all_alive = true;
                for (const auto& enemy : enemies_) {
                    if (!budget(out.round)) break;
                    const int id = int(static_cast<cambc::handle_champion_t>(enemy));
                    if (id == out.actor) break;
                    out.enemy_ids[out.enemy_count++] = id;
                    out.enemies_all_alive &= enemy.is_alive();
                }
                out.enemies_complete = out.enemy_count == int(enemies_.size()) && budget(out.round);
            }
        }
        // Fuel exhaustion can resume inside any SDK call in a later round.
        // No mixture of those observations may establish even own continuity.
        if (cambc::round_number() != round) {
            out.complete = out.on_unique_spawn = out.enemies_complete = false;
        }
        return out;
    }
public:
    template<class Entries>
    void start(const Entries& entries, const std::vector<int>& lookup, int width) {
        const auto round = cambc::round_number();
        if (round <= unsigned(std::numeric_limits<int>::max()) && last_start_ == int(round)) return;
        if (round <= unsigned(std::numeric_limits<int>::max())) last_start_ = int(round);
        tracker_.start(sample(entries, lookup, width, 60000));
    }
    template<class Entries>
    void finish(const Entries& entries, const std::vector<int>& lookup, int width) {
        tracker_.finish(sample(entries, lookup, width, 30000));
    }
    const OwnStockFrame& frame() const { return tracker_.frame(); }
    const CapturedGroup* capture() const { return tracker_.capture(); }
};
} // namespace plan4::runtime
