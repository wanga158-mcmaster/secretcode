// Shopping plans and wards.
#include <array>
#include "bot.h"
#include "config.h"

namespace wb {

namespace {
enum Family { monster = 0, power = 1, durability = 2, treasure = 3, jungle = 4, home = 5, bruiser = 6 };
enum Stat { strength = 0, focus = 1, health = 2 };

cambc::item_type shard(int f, int s) { return cambc::item_type{static_cast<std::uint32_t>(f * 6 + s * 2)}; }
cambc::item_type emblem(int f, int s) { return cambc::item_type{static_cast<std::uint32_t>(f * 6 + s * 2 + 1)}; }

struct Step {
    int family, stat, shards;
};

// Main stat per champion: what its kit converts into pack damage or pack health.
int main_stat(cambc::champion_type t) {
    using C = cambc::champion_type;
    switch (t) {
    case C::ryth: case C::marcella: case C::issac: case C::ren: return health;
    case C::cassi: case C::stellar: case C::prism: case C::willow: case C::heart_of_ruin: return focus;
    default: return strength;
    }
}
} // namespace

void Bot::shop() {
    auto& me = cambc::self();
    if (me.gold_count() < 30) return;
    const int stat = main_stat(me.champion_name());
    std::array<Step, 3> plan{};
    int steps = 0;
    plan[steps++] = {treasure, stat, 5};
    if (kit_->tank) plan[steps++] = {jungle, health, 5};
    plan[steps++] = {bruiser, stat, 100000};
    try {
        for (int guard = 0; guard < 8 && me.gold_count() >= 30; ++guard) {
            const auto counts = me.item_count();
            bool bought = false;
            for (int i = 0; i < steps; ++i) {
                const auto& st = plan[static_cast<std::size_t>(i)];
                const auto sh = shard(st.family, st.stat);
                const auto em = emblem(st.family, st.stat);
                const int have = static_cast<int>(counts[sh]) + 5 * static_cast<int>(counts[em]);
                if (have >= st.shards) continue;
                const int want = std::min(st.shards - have, static_cast<int>(me.gold_count() / 30));
                // Buy up to the next emblem boundary so the combination happens in its own call.
                const int to_emblem = 5 - (have % 5);
                const unsigned n = static_cast<unsigned>(std::min(want, to_emblem));
                if (n == 0 || !me.can_buy(sh, n)) return;
                me.buy(sh, n);
                bought = true;
                break;
            }
            if (!bought) return;
        }
    } catch (const cambc::error& e) {
        log_error("warband error shop r%d: %s\n", round_, e.what());
    }
}

void Bot::ward() {
    if constexpr (cfg::ward_mode == 0) return;
    auto& me = cambc::self();
    try {
        if (me.ward_cooldown() > 0 || !me.is_alive() || me.is_recalling() || me.is_submitting_gems()) return;
        const auto reach = me.ward_placement_tiles() & ~cambc::map::walls() & ~cambc::map::holes() & ~cambc::map::altars();
        vec2 spot = pos_;
        const auto bushes = (reach & cambc::map::bushes()).positions();
        for (auto b : bushes)
            if (!cambc::vision::has_ward(b, team::ally)) { spot = b; break; }
        me.place_ward(spot);
    } catch (const cambc::error& e) {
        log_error("warband error ward r%d: %s\n", round_, e.what());
    }
}

} // namespace wb
