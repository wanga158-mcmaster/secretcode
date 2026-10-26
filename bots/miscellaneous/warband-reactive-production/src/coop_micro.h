#pragma once

// The certificate covers a stationary harmful-member wave only. Arrival, all
// outside damage, peer commitment/readiness, cleanup, legal escape, capture and
// conversion remain caller obligations. Optional spells receive no safety credit.
#ifndef WB_COOP_MODEL_ONLY
#include <cambc.h>
#include "config.h"
#include "native_observation.h"
#else
namespace cambc { struct vec2 { int x{}, y{}; }; }
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace wb {

struct Body {
    int id{};                     // Native champion handle, not local roster slot.
    cambc::vec2 pos{};
    float hp{}, maxhp{}, strength{};
    bool ready{}, melee{};
    float escape_damage{-1};       // Caller-proved path-prefix damage; unknown rejects.
    int range{1};                  // Caller should supply queried live attack range.
    bool acted_this_round{};       // Actual peer completed_turn, not predicted readiness.
};

struct Monster {
    int key{};
    cambc::vec2 pos{};
    float hp{}, damage{};
    int range{};
    bool alive{};
};

struct FightCertificate {
    bool feasible{};
    int turns{};                  // Harmful clear upper bound, not whole job time.
    float hp0{}, hp1{};            // Independent worst-case per-body end envelopes.
    int escapevolleys{};           // No inferred path: escape_damage is authoritative.
};

inline int micro_distance(cambc::vec2 a, cambc::vec2 b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}

inline int micro_hits(float hp, float damage) {
    if (!std::isfinite(hp) || !std::isfinite(damage) || hp <= 0 || damage <= 0)
        return -1;
    // Repeated float subtraction avoids the known quotient/ceil undercount.
    for (int hits = 1; hits <= 512; ++hits) {
        const float next = hp - damage;
        if (!std::isfinite(next) || next >= hp) return -1;
        hp = next;
        if (hp <= 0) return hits;
    }
    return -1;
}

inline FightCertificate certify(std::span<const Body> bodies,
                                std::span<const Monster> monsters) {
    FightCertificate out;
    if (bodies.empty() || bodies.size() > 2 || monsters.size() > 32) return out;
    float min_basic = std::numeric_limits<float>::max();
    for (const auto& body : bodies) {
        if ((!body.ready && !body.acted_this_round) || !std::isfinite(body.hp) || !std::isfinite(body.maxhp)
            || !std::isfinite(body.strength) || !std::isfinite(body.escape_damage)
            || body.hp <= 0 || body.maxhp <= 0 || body.hp > body.maxhp + 0.01f
            || body.strength <= 0 || body.escape_damage < 0 || body.range < 1)
            return out;
        min_basic = std::min(min_basic, body.strength);
    }
    if (bodies.size() == 2 && (!bodies[0].melee || !bodies[1].melee
        || bodies[0].id == bodies[1].id
        || micro_distance(bodies[0].pos, bodies[1].pos) == 0)) return out;

    int hits = 0;
    for (const auto& monster : monsters) {
        if (!monster.alive) continue;
        if (!std::isfinite(monster.hp) || !std::isfinite(monster.damage)
            || monster.hp <= 0 || monster.damage < 0 || monster.range < 0) return out;
        if (monster.damage == 0) continue; // Harmless cleanup is a separate macro step.
        if (monster.range == 0) return out;
        int first_distance = -1;
        for (const auto& body : bodies) {
            const int distance = micro_distance(body.pos, monster.pos);
            if (distance == 0 || distance > body.range) return out;
            if (bodies.size() == 2 && first_distance >= 0 && distance != first_distance)
                return out;
            first_distance = distance;
        }
        const int needed = micro_hits(monster.hp, min_basic);
        if (needed < 0 || hits + needed > 240) return out;
        hits += needed;
    }
    int current_basics = 0;
    for (const auto& body : bodies) current_basics += body.ready ? 1 : 0;
    // An already-acted peer supplies no invented basic in this partial round.
    const int later_hits = std::max(0, hits - current_basics);
    out.turns = hits == 0 ? 0 : 1 + (later_hits + static_cast<int>(bodies.size()) - 1)
                                      / static_cast<int>(bodies.size());
    if (out.turns > 120) return out;
    std::array<float, 2> worst{bodies[0].hp, bodies.size() == 2 ? bodies[1].hp : 0.0f};
    const int scenarios = static_cast<int>(bodies.size());
    for (int first_tank = 0; first_tank < scenarios; ++first_tank) {
        std::array<float, 2> hp{bodies[0].hp, bodies.size() == 2 ? bodies[1].hp : 0.0f};
        int tank = first_tank;
        for (int turn = 0; turn < out.turns; ++turn) {
            // Charge every initially harmful monster before every future basic
            // pair, even if asleep now or killed earlier: conservative exposure.
            for (const auto& monster : monsters) {
                if (!monster.alive || monster.damage <= 0) continue;
                int target = -1, nearest = std::numeric_limits<int>::max();
                for (int actor = 0; actor < static_cast<int>(bodies.size()); ++actor) {
                    const int distance = micro_distance(bodies[actor].pos, monster.pos);
                    if (distance > monster.range) continue;
                    if (distance < nearest || (distance == nearest && actor == tank)) {
                        target = actor;
                        nearest = distance;
                    }
                }
                if (target >= 0) hp[target] -= monster.damage;
            }
            for (int actor = 0; actor < static_cast<int>(bodies.size()); ++actor) {
                if (!std::isfinite(hp[actor]) || hp[actor] <= bodies[actor].escape_damage)
                    return out;
                worst[actor] = std::min(worst[actor], hp[actor]);
            }
            if (bodies.size() == 2) {
                const bool one_healthier = hp[1] > hp[0] + 0.01f;
                const bool equal = std::abs(hp[1] - hp[0]) <= 0.01f;
                tank = one_healthier || (equal && bodies[1].id < bodies[0].id) ? 1 : 0;
            }
        }
    }
    for (int actor = 0; actor < static_cast<int>(bodies.size()); ++actor)
        if (worst[actor] <= bodies[actor].escape_damage) return out;
    out.hp0 = worst[0];
    out.hp1 = worst[1];
    out.feasible = true;
    return out;
}

struct MicroMember {
    int key{};
    cambc::vec2 pos{};
    float hp{}, damage{};
    bool alive{}, visible{};
};
struct MicroFrame {
    std::array<MicroMember, 32> members{};
    std::size_t count{};
};
struct MicroCastChoice {
    int slot{-1}, kind{}, target{-1}; // reset=1, unit=2, area=3
    float value{-1};
};

struct MicroStaticSpawn { int group{}; cambc::vec2 pos{}; };
enum class MicroInitResult { complete, deferred, unsupported };

// Immutable spawn support, including hidden and possibly living camps. The
// input span must stay immutable/alive until reset; runtime uses MicroWorld's
// owning array. Raw allocation deliberately does not initialize all map cells.
class MicroStaticIndex {
    struct Support { int group; std::uint8_t kind; }; // Empty, single group, mixed.
    std::unique_ptr<Support[]> cells_;
    std::span<const MicroStaticSpawn> spawns_;
    int width_{}, height_{};
    std::size_t size_{}, clear_cursor_{}, seed_cursor_{};
    MicroInitResult result_{MicroInitResult::unsupported};
public:
    static constexpr std::size_t max_entries_per_advance = 128;
    void reset() {
        result_ = MicroInitResult::unsupported;
        cells_.reset(); spawns_ = {}; width_ = height_ = 0;
        size_ = clear_cursor_ = seed_cursor_ = 0;
    }
    MicroInitResult begin(int width, int height, std::span<const MicroStaticSpawn> spawns) {
        reset();
        if (width <= 0 || height <= 0 || spawns.size() > 256) return result_;
        const auto count = std::uint64_t(width)*std::uint64_t(height);
        if (count > std::numeric_limits<std::size_t>::max()/sizeof(Support)) return result_;
        std::unique_ptr<Support[]> allocation{new (std::nothrow) Support[static_cast<std::size_t>(count)]};
        if (!allocation) return result_;
        cells_ = std::move(allocation); spawns_ = spawns;
        width_ = width; height_ = height; size_ = static_cast<std::size_t>(count);
        result_ = MicroInitResult::deferred;
        return result_;
    }
    MicroInitResult status() const { return result_; }
    MicroInitResult advance(std::size_t max_entries = max_entries_per_advance) {
        if (result_ != MicroInitResult::deferred) return result_;
        auto left = std::min(max_entries,max_entries_per_advance);
        while (left && clear_cursor_ < size_) {
            cells_[clear_cursor_++] = {0,0}; --left;
        }
        while (left && seed_cursor_ < spawns_.size()) {
            const auto& spawn = spawns_[seed_cursor_++]; --left;
            if (spawn.pos.x < 0 || spawn.pos.y < 0 || spawn.pos.x >= width_ || spawn.pos.y >= height_) {
                result_ = MicroInitResult::unsupported;
                return result_;
            }
            auto& cell = cells_[std::size_t(spawn.pos.y)*std::size_t(width_)+std::size_t(spawn.pos.x)];
            if (cell.kind == 0) cell = {spawn.group,1};
            else if (cell.kind == 1 && cell.group != spawn.group) cell.kind = 2;
        }
        if (clear_cursor_ == size_ && seed_cursor_ == spawns_.size()) result_ = MicroInitResult::complete;
        return result_;
    }
    bool allows(int own_group, cambc::vec2 tile) const {
        if (result_ != MicroInitResult::complete || tile.x < 0 || tile.y < 0
            || tile.x >= width_ || tile.y >= height_) return false;
        const auto& cell = cells_[std::size_t(tile.y)*std::size_t(width_)+std::size_t(tile.x)];
        return cell.kind == 0 || (cell.kind == 1 && cell.group == own_group);
    }
};

// At most two represented operators. Scores are nominal proposals, never
// native damage/safety predictions. Live readiness and safety are checked only
// in this order, at most once per proposal; the cast itself validates afresh.
template<class Eligible>
inline MicroCastChoice micro_cast_operator(std::array<MicroCastChoice,2> proposals, Eligible&& eligible) {
    if (proposals[1].value > proposals[0].value
        || (proposals[1].value == proposals[0].value && proposals[1].slot >= 0
            && (proposals[0].slot < 0 || proposals[1].slot < proposals[0].slot)))
        std::swap(proposals[0],proposals[1]);
    for (const auto& proposal : proposals)
        if (proposal.slot >= 0 && proposal.slot < 2 && std::isfinite(proposal.value)
            && proposal.value >= 0 && eligible(proposal))
            return proposal;
    return {};
}

// ExportLinearBuffer is packed row-major, least-significant bit first. This is
// an owning input snapshot; primitive targets still require live visibility.
inline bool micro_packed_contains(std::span<const std::uint8_t> bytes, int width,
                                  int height, cambc::vec2 tile) {
    if (width <= 0 || height <= 0 || tile.x < 0 || tile.y < 0
        || tile.x >= width || tile.y >= height) return false;
    const auto tiles = std::size_t(width)*std::size_t(height);
    if (bytes.size() != (tiles+7)/8) return false;
    const auto index = std::size_t(tile.y)*std::size_t(width)+std::size_t(tile.x);
    return (bytes[index>>3] & (1u << (index&7))) != 0;
}

struct MicroFrameStamp {
    std::uint32_t owner{}, group{}, round{};
    std::uint64_t input_generation{}, view_revision{};
    friend bool operator==(const MicroFrameStamp&,const MicroFrameStamp&) = default;
};

// A one-use initial ranking observation, not a certificate of later effects.
// Only the macro's complete same-input observation assembler may seal it.
class PreparedMicroFrame {
    MicroFrameStamp stamp_{};
    MicroFrame rows_{};
    std::vector<std::uint8_t> visible_;
    int width_{}, height_{};
    bool sealed_{};
public:
    PreparedMicroFrame() = default;
    PreparedMicroFrame(const PreparedMicroFrame&) = delete;
    PreparedMicroFrame& operator=(const PreparedMicroFrame&) = delete;
    PreparedMicroFrame(PreparedMicroFrame&& other) noexcept { *this = std::move(other); }
    PreparedMicroFrame& operator=(PreparedMicroFrame&& other) noexcept {
        if (this != &other) {
            stamp_ = other.stamp_; rows_ = other.rows_; visible_ = std::move(other.visible_);
            width_ = other.width_; height_ = other.height_; sealed_ = other.sealed_;
            other.reset();
        }
        return *this;
    }
    void reset() {
        sealed_ = false; stamp_ = {}; rows_.count = 0;
        visible_.clear(); width_ = height_ = 0;
    }
    bool sealed() const { return sealed_; }
    bool seal(const MicroFrameStamp& stamp,const MicroFrame& rows,int width,int height,
              std::vector<std::uint8_t>&& visible) {
        reset();
        if (width <= 0 || height <= 0 || rows.count == 0 || rows.count > rows.members.size()) return false;
        const auto cells = std::uint64_t(width)*std::uint64_t(height);
        if (cells > std::numeric_limits<std::size_t>::max()-7
            || visible.size() != (static_cast<std::size_t>(cells)+7)/8) return false;
        for (std::size_t i=0;i<rows.count;++i) {
            const auto& row = rows.members[i];
            if (row.pos.x < 0 || row.pos.y < 0 || row.pos.x >= width || row.pos.y >= height
                || row.visible != micro_packed_contains(visible,width,height,row.pos)) return false;
            for (std::size_t j=0;j<i;++j) if (rows.members[j].key == row.key) return false;
            if (!row.visible || !row.alive) {
                if ((!row.visible && row.alive) || row.hp != 0 || row.damage != 0) return false;
            } else if (!std::isfinite(row.hp) || !std::isfinite(row.damage) || row.hp <= 0 || row.damage < 0)
                return false;
        }
        stamp_ = stamp; rows_ = rows; width_ = width; height_ = height;
        visible_ = std::move(visible); sealed_ = true;
        return true;
    }
    bool consume(const MicroFrameStamp& current,std::span<const MicroMember> geometry,
                 int width,int height,MicroFrame& output,std::vector<std::uint8_t>& visible) {
        if (!sealed_ || !(stamp_ == current) || width != width_ || height != height_
            || geometry.size() != rows_.count) return false;
        for (std::size_t i=0;i<geometry.size();++i)
            if (geometry[i].key != rows_.members[i].key
                || geometry[i].pos.x != rows_.members[i].pos.x || geometry[i].pos.y != rows_.members[i].pos.y)
                return false;
        output = rows_; visible = std::move(visible_); reset();
        return true;
    }
};

template<class Capture,class SameInput>
inline bool micro_refresh_vision_if_dirty(bool& dirty,std::vector<std::uint8_t>& destination,
                                          Capture&& capture,SameInput&& same) {
    if (!dirty) return true;
    auto local = capture();
    if (!same()) return false;
    destination = std::move(local); dirty = false;
    return true;
}

// Read-only capture may span multiple imports. Keep all observations local and
// publish nothing unless the final input fence accepts the complete capture.
template<class Capture, class SameInput>
inline bool micro_capture_member(MicroMember& destination, Capture&& capture, SameInput&& same) {
    const MicroMember local = capture();
    if (!same()) return false;
    destination = local;
    return true;
}

template<class Visibility>
inline bool micro_candidate(const MicroFrame& frame, cambc::vec2 position, int range,
                            Visibility&& visible) {
    if (frame.count > frame.members.size() || range < 0) return false;
    for (std::size_t i=0; i<frame.count; ++i)
        if (micro_distance(position,frame.members[i].pos) <= range && visible(frame.members[i].pos)) return true;
    return false;
}

// Pure selection over one input's frame. It performs no SDK observations.
inline int micro_pick(const MicroFrame& frame, cambc::vec2 position, int range) {
    if (frame.count > frame.members.size() || range < 0) return -1;
    int best = -1;
    float best_score = -1;
    bool best_harmful = false;
    for (std::size_t i = 0; i < frame.count; ++i) {
        const auto& camp = frame.members[i];
        if (!camp.visible || !camp.alive || micro_distance(position, camp.pos) > range) continue;
        const bool harmful = camp.damage > 0;
        const float score = (harmful ? camp.damage : 1.0f) / std::max(1.0f, camp.hp);
        if (best < 0 || (harmful && !best_harmful) || (harmful == best_harmful
            && (score > best_score || (score == best_score && camp.key < frame.members[best].key)))) {
            best = static_cast<int>(i); best_score = score; best_harmful = harmful;
        }
    }
    return best;
}

// The adapter owns fresh host legality checks and affected-member refreshes.
// Shared by the runtime adapter and the SDK-double prefix fixture.
template<class Adapter> inline bool micro_strike_prefix(Adapter& runtime) {
    if (!runtime.prepare() || !runtime.same_input()) return false;
    bool acted = false, applied_basic = false;
    const int first = runtime.basic();
    if (first >= 0) { acted = applied_basic = true; }
    if (!runtime.same_input()) return acted;
    if (first >= 0) runtime.refresh_basic(first);
    if (!runtime.same_input()) return acted;
    const auto choice = runtime.choose_cast(applied_basic);
    if (!runtime.same_input()) return acted;
    if (choice.slot >= 0) {
        const bool applied = runtime.cast(choice);
        acted = acted || applied;
        if (!runtime.same_input()) return acted;
        if (applied) runtime.refresh_cast(choice);
    }
    if (!runtime.same_input()) return acted;
    return runtime.basic() >= 0 || acted;
}

#ifndef WB_COOP_MODEL_ONLY

// Last entered phase only: no logging, clock read, or instruction attribution.
// The production-false branch emits no stage stores.
inline const char* micro_stage = "idle";
inline void micro_mark_stage(const char* stage) {
    if constexpr (config::phase_audit) micro_stage = stage;
}

inline int micro_id(const cambc::camp& camp) {
    return static_cast<int>(static_cast<cambc::handle_camp_t>(camp));
}
inline int micro_id(const cambc::champion& champion) {
    return static_cast<int>(static_cast<cambc::handle_champion_t>(champion));
}

struct MicroWorld {
    using Spawn = MicroStaticSpawn;
    std::array<Spawn, 256> spawns{};
    std::size_t count{};
    bool ready{};
};
inline MicroWorld micro_world;
// Owning allocation/destructor registration is reached only through the
// production controller's cold service, never through OFF-path startup.
inline MicroStaticIndex& micro_static_index() {
    static MicroStaticIndex index;
    return index;
}

// Prime from the macro's already-loaded immutable geometry. Never reload the
// map in a combat input. Unknown/overflow disables area casts, not basic work.
inline bool micro_initialize(std::span<const cambc::map::camp_group> groups) {
    micro_mark_stage("world-setup");
    micro_static_index().reset();
    micro_world.ready = false; micro_world.count = 0;
    if (groups.empty()) return false;
    for (const auto& group : groups)
        for (const auto& camp : group.camps()) {
            if (micro_world.count == micro_world.spawns.size()) return false;
            micro_world.spawns[micro_world.count++] = {
                static_cast<int>(static_cast<cambc::handle_camp_group_t>(group)), camp.spawn_position()};
        }
    const auto result = micro_static_index().begin(cambc::map::width(),cambc::map::height(),
        std::span<const MicroStaticSpawn>{micro_world.spawns.data(),micro_world.count});
    micro_world.ready = result != MicroInitResult::unsupported;
    return micro_world.ready;
}

inline MicroInitResult micro_initialize_status() { return micro_static_index().status(); }
inline MicroInitResult micro_initialize_step(std::size_t max_entries = 128) {
    micro_mark_stage("world-index-service");
    return micro_static_index().advance(max_entries);
}

class MicroRuntime {
    const cambc::map::camp_group& group_;
    cambc::champion& self_;
    std::uint32_t epoch_;
    std::array<cambc::ability, 2> abilities_;
    MicroFrame frame_;
    std::vector<std::uint8_t> visible_bytes_, enemy_bytes_;
    bool enemies_ready_{};
    bool personal_vision_dirty_{};
    bool sdk_action_attempted_{};
    int width_{cambc::map::width()}, height_{cambc::map::height()};
    cambc::vec2 position_{};
    int basic_range_{};
    PreparedMicroFrame* prepared_{};
    MicroFrameStamp expected_stamp_{};

    static int range_of(const cambc::ability& ability) {
        if (!ability.info()) return -1;
        float range = -1;
        if (ability.is_damage()) range = ability.get<cambc::ability_damage>().range;
        else if (ability.is_cc()) range = ability.get<cambc::ability_cc>().range;
        else if (ability.is_aoe_damage()) range = ability.get<cambc::ability_aoe_damage>().range;
        if (!std::isfinite(range) || range < 0
            || range >= static_cast<float>(std::numeric_limits<int>::max())) return -1;
        return static_cast<int>(std::floor(range));
    }
    static bool exclusive(const cambc::ability& ability) {
        return std::visit([](const auto& info) { return info.exclusive_with_movement; }, *ability.info());
    }
    bool read_member(int index, bool fresh_visibility) {
        auto& row = frame_.members[index];
        if (!same_input()) return false;
        try {
            return micro_capture_member(row,[&] {
                auto local = row;
                const bool seen = fresh_visibility ? cambc::vision::in_vision(row.pos)
                    : micro_packed_contains(visible_bytes_,width_,height_,row.pos);
                if (!seen) { local.visible = local.alive = false; return local; }
                const auto& camp = group_.camps()[index];
                local.alive = camp.is_alive();
                if (local.alive) {
                    const auto observed = native_camp(camp);
                    local.hp = observed.health(); local.damage = observed.strength();
                } else { local.hp = local.damage = 0; }
                local.visible = true;
                return local;
            },[this] { return same_input(); });
        } catch (const cambc::error& error) {
            if (error.code() != cambc::error_code::api_not_in_vision
                && error.code() != cambc::error_code::api_not_found) throw;
            if (!same_input()) return false;
            row.visible = row.alive = false;
            return true;
        }
    }
    // Cached ranking never authorizes an action: visibility/liveness and owner
    // allowances are read again, then the input clock is fenced before acting.
    bool live_target(int index) {
        auto& row = frame_.members[index];
        if (!same_input()) return false;
        const bool seen = cambc::vision::in_vision(row.pos);
        if (!same_input()) return false;
        if (!seen) { row.visible = row.alive = false; return false; }
        try {
            row.alive = group_.camps()[index].is_alive();
            row.visible = true;
        } catch (const cambc::error& error) {
            if (error.code() != cambc::error_code::api_not_in_vision
                && error.code() != cambc::error_code::api_not_found) throw;
            row.visible = row.alive = false;
        }
        return same_input() && row.alive && row.visible;
    }
    bool safe_aoe(cambc::vec2 center, float radius) {
        micro_mark_stage("cast-aoe-validation");
        if (!micro_world.ready || !std::isfinite(radius) || radius < 0 || radius > 4) return false;
        const auto& geometry = micro_static_index();
        if (geometry.status() != MicroInitResult::complete) return false;
        if (!same_input()) return false;
        const int r = static_cast<int>(std::ceil(radius));
        const int own = static_cast<int>(static_cast<cambc::handle_camp_group_t>(group_));
        micro_mark_stage("cast-aoe-outsiders");
        for (int y = center.y-r; y <= center.y+r; ++y)
            for (int x = center.x-r; x <= center.x+r; ++x) {
                const cambc::vec2 tile{x,y};
                if (cambc::map::is_in_bounds(tile) && micro_distance(center,tile) <= radius
                    && !geometry.allows(own,tile)) return false;
            }
        if (!same_input()) return false;
        if (!micro_refresh_vision_if_dirty(personal_vision_dirty_,visible_bytes_,[] {
                micro_mark_stage("cast-aoe-vision-capture");
                const auto visible = cambc::vision::visible_tiles();
                micro_mark_stage("cast-aoe-vision-export");
                return visible.export_linear_buffer();
            },[this] { return same_input(); })) return false;
        if (!enemies_ready_) {
            micro_mark_stage("cast-aoe-enemy-capture");
            const auto enemies = cambc::vision::champion_tiles(cambc::team::enemy);
            micro_mark_stage("cast-aoe-enemy-export");
            auto bytes = enemies.export_linear_buffer();
            if (!same_input()) return false;
            enemy_bytes_ = std::move(bytes); enemies_ready_ = true;
        }
        micro_mark_stage("cast-aoe-footprint");
        for (int y = center.y-r; y <= center.y+r; ++y)
            for (int x = center.x-r; x <= center.x+r; ++x) {
                const cambc::vec2 tile{x,y};
                if (cambc::map::is_in_bounds(tile) && micro_distance(center,tile) <= radius
                    && (!micro_packed_contains(visible_bytes_,width_,height_,tile)
                        || micro_packed_contains(enemy_bytes_,width_,height_,tile))) return false;
            }
        if (!same_input()) return false;
        micro_mark_stage("cast-aoe-fence");
        return same_input();
    }
public:
    explicit MicroRuntime(const cambc::map::camp_group& group,PreparedMicroFrame* prepared = nullptr,
                          MicroFrameStamp expected = {})
        : group_(group), self_(cambc::self()), epoch_(cambc::round_number()), abilities_(self_.abilities()),
          prepared_(prepared), expected_stamp_(expected) {
        micro_mark_stage("setup");
    }
    bool same_input() const { return cambc::round_number() == epoch_; }
    bool sdk_action_attempted() const { return sdk_action_attempted_; }
    bool prepare() {
        micro_mark_stage("prepare-setup");
        const auto& camps = group_.camps();
        if (camps.empty() || camps.size() > frame_.members.size()) return false;
        frame_.count = camps.size();
        for (std::size_t i=0; i<camps.size(); ++i) {
            frame_.members[i].key = micro_id(camps[i]);
            frame_.members[i].pos = camps[i].spawn_position();
        }
        if (prepared_ && (expected_stamp_.owner != static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(self_))
            || expected_stamp_.group != static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(group_))
            || expected_stamp_.round != epoch_)) return false;
        micro_mark_stage("prepare-owner");
        if (!same_input()) return false;
        const auto owner = native_champion(self_);
        if (!same_input()) return false;
        position_ = owner.position(); basic_range_ = owner.attack_range();
        int maximum_range = basic_range_;
        for (const auto& ability : abilities_) maximum_range = std::max(maximum_range, range_of(ability));
        // Static geometry and one personal-vision view precede all camp state,
        // health or strength imports. Cast-only centers remain candidates.
        if (!micro_candidate(frame_,position_,maximum_range,[](cambc::vec2){return true;})
            || !same_input()) return false;
        micro_mark_stage("prepare-vision");
        if (prepared_) {
            if (!prepared_->consume(expected_stamp_,
                    std::span<const MicroMember>{frame_.members.data(),frame_.count},width_,height_,frame_,visible_bytes_))
                return false;
        } else {
            const auto visible = cambc::vision::visible_tiles();
            visible_bytes_ = visible.export_linear_buffer();
        }
        if (!same_input()) return false;
        if (!micro_candidate(frame_,position_,maximum_range,[this](cambc::vec2 p){
                return micro_packed_contains(visible_bytes_,width_,height_,p);})) return false;
        micro_mark_stage("prepare-availability");
        if (cambc::fuel_remaining() < 6000 + 1000*camps.size()) return false;
        const auto state = owner.state();
        if (!same_input() || !state.contains(cambc::champion_state::alive)
            || (!state.contains(cambc::champion_state::can_attack) && !state.contains(cambc::champion_state::can_cast))
            || self_.passive() == cambc::passive_type::splash_attack) return false;
        if (!same_input()) return false;
        bool actionable = state.contains(cambc::champion_state::can_attack)
            && micro_candidate(frame_,position_,basic_range_,[this](cambc::vec2 p){
                return micro_packed_contains(visible_bytes_,width_,height_,p);});
        if (!actionable && state.contains(cambc::champion_state::can_cast)) {
            for (const auto& ability : abilities_) {
                if (range_of(ability) < 0) continue;
                const bool cooldown = ability.is_on_cooldown();
                if (!same_input()) return false;
                if (cooldown) continue;
                const bool targets_camps = ability.can_target_camps();
                if (!same_input()) return false;
                if (!targets_camps) continue;
                if (exclusive(ability) && !state.contains(cambc::champion_state::can_move)) continue;
                if (micro_candidate(frame_,position_,range_of(ability),[this](cambc::vec2 p){
                        return micro_packed_contains(visible_bytes_,width_,height_,p);}))
                    actionable = true;
            }
        }
        if (!actionable || !same_input()) return false;
        micro_mark_stage("prepare-members");
        if (!prepared_)
            for (std::size_t i=0; i<camps.size(); ++i)
                if (!read_member(static_cast<int>(i),false)) return false;
        return same_input();
    }
    int basic() {
        micro_mark_stage("basic-validation");
        if (!same_input()) return -1;
        const auto owner = native_champion(self_);
        if (!same_input() || !owner.can_attack()) return -1;
        const int target = micro_pick(frame_,owner.position(),owner.attack_range());
        if (target < 0 || !live_target(target)) return -1;
        // One fresh owner record after target checks; cached fields never grant
        // a primitive after a suspension or an earlier basic/reset/cast.
        const auto fresh = native_champion(self_);
        if (!fresh.can_attack()
            || micro_distance(fresh.position(),frame_.members[target].pos) > fresh.attack_range()) return -1;
        // Existing empirical I/O reserve for require_self + primitive; this is
        // not a formal atomicity or instruction-level suspension guarantee.
        micro_mark_stage("basic-action");
        if (cambc::fuel_remaining() <= 7000 || !same_input()) return -1;
        sdk_action_attempted_ = true;
        personal_vision_dirty_ = true; enemies_ready_ = false;
        self_.attack(group_.camps()[target]);
        return target;
    }
    void refresh_basic(int target) {
        micro_mark_stage("basic-refresh");
        read_member(target,true);
    }
    MicroCastChoice choose_cast(bool applied_basic) {
        micro_mark_stage("cast-ranking");
        if (!same_input()) return {};
        const auto owner = native_champion(self_);
        if (!same_input() || !owner.can_cast() || cambc::fuel_remaining() <= 7000 || !same_input()) return {};
        const auto position = owner.position();
        const int basic_range = owner.attack_range();
        std::array<MicroCastChoice,2> proposals{};
        for (int slot=0; slot<2; ++slot) {
            const auto& ability = abilities_[slot];
            if (!ability.info()) continue;
            if (exclusive(ability) && !owner.can_move()) continue;
            MicroCastChoice choice; choice.slot = slot;
            if (ability.is_attack_reset()) {
                if (!applied_basic || owner.can_attack() || micro_pick(frame_,position,basic_range) < 0) continue;
                choice.kind = 1; choice.value = owner.strength();
            } else if (ability.is_damage() || ability.is_cc() || ability.is_aoe_damage()) {
                // Constructor metadata proposes a target class only. The
                // selected cast still imports the live target flags below.
                const auto targets = std::visit([](const auto& info) { return info.valid_targets; },*ability.info());
                if (!targets.contains(cambc::ability_target_mask::camps)) continue;
                choice.target = micro_pick(frame_,position,range_of(ability));
                if (choice.target < 0) continue;
                choice.kind = 2;
                int count = 1;
                const float modifier = ability.is_damage() ? ability.get<cambc::ability_damage>().modifier
                    : ability.is_cc() ? ability.get<cambc::ability_cc>().modifier
                                     : ability.get<cambc::ability_aoe_damage>().modifier;
                if (ability.is_aoe_damage()) {
                    const auto info = ability.get<cambc::ability_aoe_damage>();
                    const auto center = frame_.members[choice.target].pos;
                    count = 0; choice.kind = 3;
                    for (std::size_t i=0; i<frame_.count; ++i) {
                        const auto& camp = frame_.members[i];
                        if (camp.visible && camp.alive && micro_distance(camp.pos,center) <= info.radius) ++count;
                    }
                }
                // Avoid a native damage prediction for ranking. Current owner
                // Focus is fresh, but outgoing bonuses and actual losses are
                // deliberately not credited by this nominal proposal score.
                choice.value = owner.focus()*modifier*float(count);
            } else continue;
            proposals[slot] = choice;
        }
        bool current = true;
        return micro_cast_operator(proposals,[&](const MicroCastChoice& choice) {
            if (!current || !same_input()) { current = false; return false; }
            micro_mark_stage("cast-readiness");
            const auto& ability = abilities_[choice.slot];
            const bool cooldown = ability.is_on_cooldown();
            if (!same_input()) { current = false; return false; }
            if (cooldown) return false;
            if (choice.kind == 3) {
                const auto info = ability.get<cambc::ability_aoe_damage>();
                if (!safe_aoe(frame_.members[choice.target].pos,info.radius)) {
                    if (!same_input()) current = false;
                    return false;
                }
            }
            return true;
        });
    }
    bool cast(const MicroCastChoice& choice) {
        micro_mark_stage("cast-validation");
        const auto& ability = abilities_[choice.slot];
        if (!same_input()) return false;
        const auto owner = native_champion(self_);
        if (!same_input() || !owner.can_cast()) return false;
        const bool cooldown = ability.is_on_cooldown();
        if (!same_input() || cooldown) return false;
        if (exclusive(ability) && !owner.can_move()) return false;
        if (choice.kind == 1) {
            micro_mark_stage("cast-reset-action");
            if (cambc::fuel_remaining() <= 7000 || !same_input()) return false;
            sdk_action_attempted_ = true;
            personal_vision_dirty_ = true; enemies_ready_ = false;
            return ability.use();
        }
        if (choice.target < 0) return false;
        const bool targets_camps = ability.can_target_camps();
        if (!same_input() || !targets_camps || !live_target(choice.target)) return false;
        const auto center = frame_.members[choice.target].pos;
        if (micro_distance(owner.position(),center) > range_of(ability)) return false;
        const bool ground = choice.kind == 3 && ability.can_target_ground();
        micro_mark_stage(ground ? "cast-ground-action" : "cast-unit-action");
        if (cambc::fuel_remaining() <= 7000 || !same_input()) return false;
        sdk_action_attempted_ = true;
        personal_vision_dirty_ = true; enemies_ready_ = false;
        return ground ? ability.use(center) : ability.use(group_.camps()[choice.target]);
    }
    void refresh_cast(const MicroCastChoice& choice) {
        micro_mark_stage("cast-refresh");
        if (choice.kind == 2) { read_member(choice.target,true); return; }
        if (choice.kind != 3) return;
        const auto center = frame_.members[choice.target].pos;
        const auto radius = abilities_[choice.slot].get<cambc::ability_aoe_damage>().radius;
        for (std::size_t i=0; i<frame_.count && same_input(); ++i)
            if (micro_distance(frame_.members[i].pos,center) <= radius) read_member(static_cast<int>(i),true);
    }
};

inline bool strike(const cambc::map::camp_group& group, bool* sdk_action_attempted = nullptr) {
    if (sdk_action_attempted) *sdk_action_attempted = false;
    micro_mark_stage("constructor");
    MicroRuntime runtime{group};
    try {
        const bool acted = micro_strike_prefix(runtime);
        if (sdk_action_attempted) *sdk_action_attempted = runtime.sdk_action_attempted();
        return acted;
    } catch (...) {
        if (sdk_action_attempted) *sdk_action_attempted = runtime.sdk_action_attempted();
        throw;
    }
}

// An explicitly supplied token mismatch refuses; it never silently promotes
// old macro rows or takes the fresh-observation fallback.
inline bool strike_preobserved(const cambc::map::camp_group& group,PreparedMicroFrame& prepared,
                               const MicroFrameStamp& current,bool* sdk_action_attempted = nullptr) {
    if (sdk_action_attempted) *sdk_action_attempted = false;
    micro_mark_stage("constructor");
    MicroRuntime runtime{group,&prepared,current};
    try {
        const bool acted = micro_strike_prefix(runtime);
        if (sdk_action_attempted) *sdk_action_attempted = runtime.sdk_action_attempted();
        return acted;
    } catch (...) {
        if (sdk_action_attempted) *sdk_action_attempted = runtime.sdk_action_attempted();
        throw;
    }
}

inline bool configure_next_priority(const cambc::map::camp_group& group,
                                    const cambc::champion& peer, int designated_id,
                                    std::uint32_t expected_round) {
    const auto same = [expected_round] { return cambc::round_number() == expected_round; };
    if (!same()) return false;
    auto& self = cambc::self();
    if (!same()) return false;
    const auto owner = native_champion(self);
    if (!same()) return false;
    const auto ally = native_visible_champion(peer);
    if (!same() || !ally || !owner.is_alive() || !ally->is_alive()
        || owner.attack_range() != 1 || ally->attack_range() != 1) return false;
    if (owner.is_recalling() || owner.is_submitting_gems() || owner.is_teleporting()
        || ally->is_recalling() || ally->is_submitting_gems() || ally->is_teleporting()) return false;
    // Availability can be spent already in this round; restrictions are the
    // separate live obligation for selecting the following round's tank.
    if (!same()) return false;
    const auto self_restrictions = self.restrictions();
    if (!same()) return false;
    const auto peer_restrictions = peer.restrictions();
    if (!same()) return false;
    if ((self_restrictions.contains(cambc::buff_restrictions::attack)
         && self_restrictions.contains(cambc::buff_restrictions::cast))
        || (peer_restrictions.contains(cambc::buff_restrictions::attack)
            && peer_restrictions.contains(cambc::buff_restrictions::cast))) return false;
    const auto a = owner.position(), b = ally->position();
    if (micro_distance(a, b) == 0) return false;
    bool harmful = false;
    for (const auto& camp : group.camps()) {
        if (!same()) return false;
        const bool visible = cambc::vision::in_vision(camp.spawn_position());
        if (!same() || !visible) return false;
        const bool alive = camp.is_alive();
        if (!same()) return false;
        if (!alive) continue;
        const auto observed = native_camp(camp);
        if (!same()) return false;
        if (observed.strength() <= 0) continue;
        const int da = micro_distance(a, camp.spawn_position());
        const int db = micro_distance(b, camp.spawn_position());
        if (da != 1 || db != 1) return false;
        harmful = true;
    }
    if (!harmful) return false;
    const int self_id = micro_id(self), peer_id = micro_id(peer);
    if (designated_id != self_id && designated_id != peer_id) return false;
    // Certificate's tie is minimum native identity. A different designation must
    // not silently select a different modeled future targeting schedule.
    if (designated_id != std::min(self_id, peer_id)) return false;
    const float hp = owner.health(), peer_hp = ally->health();
    const bool tank = hp > peer_hp + 0.01f
        || (std::abs(hp - peer_hp) <= 0.01f && self_id == designated_id);
    if (!same()) return false;
    self.set_priority(tank ? 100 : 0);
    return same();
}

#endif
} // namespace wb
