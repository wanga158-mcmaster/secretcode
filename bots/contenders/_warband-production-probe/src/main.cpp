// Bounded native fixture of warband's actual camp executor. Setup reaches, rather than injects,
// a low-health awake Wolf next to untouched Gold; the production caller runs once at round 54.
// Map: .cache/contenders/micro/maps/packing-p0.map26a (30x30, mirrored three mixed groups).
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include "../../warband/src/bot.cpp"
#include "../../warband/src/act.cpp"
#include "../../warband/src/fight.cpp"
#include "../../warband/src/nav.cpp"
#include "../../warband/src/shop.cpp"
#include "../../warband/src/sense.cpp"
#include "../../warband/src/world.cpp"

namespace wb {
struct Probe {
    static cambc::vec2 own(cambc::vec2 p) {
        return cambc::map::spawn_region(team::ally).is_set({1, 1})
            ? p : cambc::vec2{cambc::map::width() - 1 - p.x, cambc::map::height() - 1 - p.y};
    }
    static void turn(Bot& bot) {
        auto& me = cambc::self();
        if (!me.is_alive()) return;
        if (!bot.init_) bot.init();
        const int r = static_cast<int>(cambc::round_number());
        const bool tavi = me.champion_name() == cambc::champion_type::tavi;
        const bool ryth = me.champion_name() == cambc::champion_type::ryth;
        const int y = tavi ? 6 : ryth ? 12 : 18;
        const auto here = own({ryth ? 7 : 6, y - 1});
        const auto gp = own({7, y}), wp = own({8, y});
        if (r < 25) {
            if (me.position() != here && me.can_move() && !cambc::vision::has_champion(here)) me.move_towards(here);
            return;
        }
        if (r > 54 || me.position() != here || !cambc::vision::has_camp(gp)
            || !cambc::vision::has_camp(wp)) return;
        const auto gold = cambc::vision::camp_at(gp), wolf = cambc::vision::camp_at(wp);
        const int hits = tavi || ryth ? 28 : 23;
        if (r >= 25 && r < 25 + hits) { me.attack(wolf); return; }
        if (r != 54) return;
        bot.round_ = r;
        bot.pos_ = me.position();
        bot.hp_ = me.health();
        bot.maxhp_ = me.max_health();
        bot.world_.observe(r);
        cambc::vision::camp_tiles_into(bot.living_);
        cambc::vision::awake_camp_tiles_into(bot.awake_);
        const int g = bot.world_.group_of(gp);
        std::printf("AP before r%d who%d gold%.0f wolf%.0f A%d C%d fuel%llu\n", r,
            static_cast<int>(me.champion_name()), gold.health(), wolf.health(), me.can_attack(), me.can_cast(),
            static_cast<unsigned long long>(cambc::fuel_remaining()));
        const bool acted = bot.attack_camp(g);
        std::printf("AP after r%d who%d gold%.0f wolf%.0f A%d C%d acted%d fuel%llu\n", r,
            static_cast<int>(me.champion_name()), gold.is_alive() ? gold.health() : 0,
            wolf.is_alive() ? wolf.health() : 0, me.can_attack(), me.can_cast(), acted,
            static_cast<unsigned long long>(cambc::fuel_remaining()));
    }
};
} // namespace wb

int main() {
    wb::Bot bot;
    while (true) {
        try {
            if (cambc::is_draft()) {
                const std::array picks{cambc::champion_draft::tavi, cambc::champion_draft::ryth,
                    cambc::champion_draft::vesper_vale};
                const auto chosen = cambc::draft::current_draft(cambc::team::ally);
                for (const auto p : picks)
                    if (std::find(chosen.begin(), chosen.end(), p) == chosen.end()) { cambc::draft::pick(p); break; }
            } else wb::Probe::turn(bot);
        } catch (const std::exception& e) { std::printf("warband probe error: %s\n", e.what()); }
        cambc::end_turn();
    }
}
