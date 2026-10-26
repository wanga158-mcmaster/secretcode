#pragma once
// Immutable SDK double for the extracted production initializer. Fuel charges
// are deliberately synthetic and prove checkpoint coverage, never native cost.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace startup_mock {
inline constexpr int width = 100, height = 100, count = width * height;
inline std::uint64_t fuel = 1000000000, ordinary_budget = 100000;
inline unsigned yields = 0, round = 1, copy_cost = 0, compute_calls = 0;
inline unsigned min_fuel = std::numeric_limits<unsigned>::max();
inline bool charging = false;
inline std::vector<unsigned char> walls(count), holes(count), altars(count);
struct FuelExhausted {};
inline void charge(unsigned amount) {
    if (!charging) return;
    if (fuel < amount) throw FuelExhausted{};
    fuel -= amount;
    min_fuel = std::min(min_fuel, static_cast<unsigned>(fuel));
}
}

namespace cambc {
struct vec2 {
    int x = 0, y = 0;
    vec2() = default;
    vec2(int xx, int yy) : x(xx), y(yy) {}
    vec2(const vec2& v) : x(v.x), y(v.y) { startup_mock::charge(startup_mock::copy_cost); }
    vec2(vec2&&) noexcept = default;
    vec2& operator=(const vec2&) = default;
    vec2& operator=(vec2&&) noexcept = default;
};
class bitboard {
public:
    std::vector<unsigned char> bits;
    bitboard() : bits(startup_mock::count) {}
    explicit bitboard(const std::vector<unsigned char>& b) : bits(b) {}
    std::vector<vec2> positions() const {
        std::vector<vec2> result;
        result.reserve(bits.size());
        for (int t = 0; t < startup_mock::count; ++t)
            if (bits[t]) result.emplace_back(t % startup_mock::width, t / startup_mock::width);
        return result;
    }
    void draw_chebyshev(const vec2& p, int radius) {
        for (int y = std::max(0, p.y - radius); y <= std::min(startup_mock::height - 1, p.y + radius); ++y)
            for (int x = std::max(0, p.x - radius); x <= std::min(startup_mock::width - 1, p.x + radius); ++x)
                bits[y * startup_mock::width + x] = 1;
    }
    friend bitboard operator|(const bitboard& a, const bitboard& b) {
        bitboard out;
        for (int t = 0; t < startup_mock::count; ++t) out.bits[t] = a.bits[t] || b.bits[t];
        return out;
    }
    friend bitboard operator&(const bitboard& a, const bitboard& b) {
        bitboard out;
        for (int t = 0; t < startup_mock::count; ++t) out.bits[t] = a.bits[t] && b.bits[t];
        return out;
    }
    friend bitboard operator~(const bitboard& a) {
        bitboard out;
        for (int t = 0; t < startup_mock::count; ++t) out.bits[t] = !a.bits[t];
        return out;
    }
    bitboard& operator|=(const bitboard& other) { return *this = *this | other; }
};
enum class team { ally, enemy };
using handle_camp_group_t = std::uint32_t;
enum class item_passive { jungle };
struct item_info { item_passive passive = item_passive::jungle; float passive_magnitude = .1f; };
namespace item_type {
struct Item { item_info info() const { startup_mock::charge(100); return {}; } };
inline Item jungle_emblem_of_health;
}
namespace constants { inline constexpr int submit_gems_range = 1; }
inline std::array<std::byte, 2> draft_memory{std::byte{7}, std::byte{5}};
struct champion { int champion_name() const { return 0; } };
inline champion& self() { static champion own; return own; }
inline std::string_view to_string(int) { return "vendetta"; }
inline std::uint64_t fuel_remaining() { return startup_mock::fuel; }
inline void end_turn() {
    ++startup_mock::yields;
    ++startup_mock::round;
    startup_mock::fuel = startup_mock::ordinary_budget;
}
inline int round_number() { return startup_mock::round; }

// Deliberately simple static distances: this fixture tests that the production
// initializer retains the same inputs, ordering and metadata across yields.
// NativeNav itself remains the production implementation below the SDK seam.
class distance_field {
public:
    inline static constexpr std::uint16_t unreachable = 65535;
    bitboard walls, roots;
    unsigned cap = 0;
    void compute(const bitboard& blocked, const bitboard& origin, bitboard& visited, unsigned maximum) {
        startup_mock::charge(20000);
        ++startup_mock::compute_calls;
        walls = blocked; roots = origin; cap = maximum;
        visited = ~blocked;
    }
    unsigned get(const vec2& p) const {
        startup_mock::charge(30);
        int index = p.y * startup_mock::width + p.x;
        if (walls.bits[index]) return unreachable;
        unsigned best = unreachable;
        for (int t = 0; t < startup_mock::count; ++t) if (roots.bits[t])
            best = std::min(best, static_cast<unsigned>(std::max(std::abs(p.x - t % startup_mock::width),
                                                               std::abs(p.y - t / startup_mock::width))));
        return best <= cap ? best : unreachable;
    }
};
struct stats { float base_health = 100, base_strength = 5; };
struct camp {
    int id = 0;
    vec2 spawn_position() const { return {5 + (id / 2) * 7 + id % 2, 5 + (id / 2) * 5}; }
    stats base_stats() const { startup_mock::charge(10); return {100.f + id, 5.f + id % 3}; }
};
namespace map {
inline int width() { return startup_mock::width; }
inline int height() { return startup_mock::height; }
inline bitboard walls() { return bitboard(startup_mock::walls); }
inline bitboard holes() { return bitboard(startup_mock::holes); }
inline bitboard altars() { return bitboard(startup_mock::altars); }
inline bitboard spawn_region(team side) {
    bitboard out;
    int start = side == team::ally ? 1 : 97;
    for (int y = start; y < start + 2; ++y) for (int x = start; x < start + 2; ++x)
        out.bits[y * startup_mock::width + x] = 1;
    return out;
}
struct camp_group {
    int id = 0;
    operator handle_camp_group_t() const { return id; }
    std::vector<camp> camps() const { return {{id * 2}, {id * 2 + 1}}; }
    bitboard spawn_tiles() const {
        bitboard out;
        for (const auto& c : camps()) {
            auto p = c.spawn_position(); out.bits[p.y * startup_mock::width + p.x] = 1;
        }
        return out;
    }
    int gem_value() const { return id % 3 == 0 ? 0 : 8 + id * 3; }
};
inline std::vector<camp_group> camp_groups() {
    std::vector<camp_group> out;
    for (int id = 0; id < 12; ++id) out.push_back({id});
    return out;
}
}
}

namespace startup_mock {
inline void configure(bool constrained) {
    charging = false; yields = 0; round = 1; compute_calls = 0;
    min_fuel = std::numeric_limits<unsigned>::max();
    walls.assign(count, 0); holes.assign(count, 0); altars.assign(count, 0);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        holes[y * width + x] = x % 3 == 0;
        walls[y * width + x] = y % 7 == 0 && x % 3 != 0;
    }
    for (int t : {1010, 2070, 7020, 9090}) { altars[t] = 1; walls[t] = holes[t] = 0; }
    for (auto side : {cambc::team::ally, cambc::team::enemy})
        for (auto p : cambc::map::spawn_region(side).positions()) walls[p.y * width + p.x] = holes[p.y * width + p.x] = 0;
    for (const auto& group : cambc::map::camp_groups())
        for (const auto& c : group.camps()) {
            auto p = c.spawn_position(); walls[p.y * width + p.x] = holes[p.y * width + p.x] = 0;
        }
    copy_cost = constrained ? 40 : 0;
    fuel = constrained ? 200000 : 1000000000;
    ordinary_budget = constrained ? 100000 : 1000000000;
    charging = true;
}
}
