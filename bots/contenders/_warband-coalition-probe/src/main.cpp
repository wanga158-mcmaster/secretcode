// Native actual fight_enemies caller fixture. All positions and allowances are reached by
// ordinary SDK actions. No planner claim: the original caller focus is stipulated as enemy B.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include "../../_warband-coalition/src/combat.h"
#include "../../_warband-coalition/src/config.h"
#include "../../_warband-coalition/src/coalition.h"
#include "../../_warband-coalition/src/knowledge.h"
#include "../../_warband-coalition/src/world.h"

namespace mb {
bool fuel_ok(std::uint64_t need) { return cambc::fuel_remaining() > need + cfg::fuel_reserve; }
bool log_allowed() { static int count = 0; return ++count <= 400; }
} // namespace mb

namespace {
using namespace cambc;
inline constexpr int probe_active_side = 0;
int side() { return map::spawn_region(team::ally).is_set({1, 1}) ? 0 : 1; }
bool active() { return side() == probe_active_side; }
vec2 frame(vec2 p) { return probe_active_side == 0 ? p : vec2{map::width() - 1 - p.x, map::height() - 1 - p.y}; }
const char* who() {
    switch (self().champion_name()) {
    case champion_type::tavi: return "tavi";
    case champion_type::ryth: return "ryth";
    case champion_type::marcella: return "marcella";
    case champion_type::vesper_vale: return "vesper";
    default: return "reserve";
    }
}
vec2 destination() {
    const auto type = self().champion_name();
    if (active()) {
        if (type == champion_type::tavi) return frame({5, 5});
        if (type == champion_type::ryth) return frame({8, 6});
        return frame({2, 10});
    }
    if (type == champion_type::marcella) return frame({8, 5});
    if (type == champion_type::vesper_vale) return frame({9, 5});
    return frame({20, 5});
}
void draft_turn() {
    const std::array party{champion_draft::tavi, champion_draft::ryth, champion_draft::willow};
    const std::array passive{champion_draft::marcella, champion_draft::vesper_vale, champion_draft::prism};
    const auto& picks = active() ? party : passive;
    const auto chosen = draft::current_draft(team::ally);
    for (const auto p : picks)
        if (std::find(chosen.begin(), chosen.end(), p) == chosen.end()) { draft::pick(p); return; }
}
void log_state(const char* phase, const mb::Ctx& ctx, const mb::ChampView& a,
               const mb::ChampView& b, bool acted, const mb::TurnActions& acts) {
    const auto cap = shared_memory_buffer::size();
    mb::coalition::Bytes bytes{};
    shared_memory.read(cap - bytes.size(), std::span<std::uint8_t>{bytes.data(), bytes.size()});
    const auto record = mb::coalition::FocusRecord::decode(bytes);
    const auto& me = self();
    const auto pos = me.position();
    std::printf("CP phase=%s r=%u side=%c who=%s x=%d y=%d ah=%.0f bh=%.0f ai=%d bi=%d A=%d C=%d M=%d focus=%d stamp=%d acted=%d moved=%d attacked=%d fuel=%llu\n",
        phase, round_number(), side() == 0 ? 'A' : 'B', who(), pos.x, pos.y,
        a.champ->health(), b.champ->health(), a.index, b.index, me.can_attack(), me.can_cast(), me.can_move(),
        record.enemy, record.round, acted, acts.moved, acts.attacked,
        static_cast<unsigned long long>(fuel_remaining()));
    (void)ctx;
}
void turn() {
    auto& me = self();
    if (!me.is_alive()) return;
    static bool initialised = false;
    if (!initialised) {
        mb::World::get(); mb::Knowledge::get(); mb::kit();
        initialised = true;
    }
    const int r = static_cast<int>(round_number());
    const auto dest = destination();
    if (r < 54) {
        if (me.position() != dest && me.can_move() && !vision::has_champion(dest)) me.move_towards(dest);
        return;
    }
    if (r != 54 || !active() || (me.champion_name() != champion_type::tavi && me.champion_name() != champion_type::ryth)) return;
    if (me.position() != dest) {
        std::printf("CP refusal=setup_position r=%d who=%s\n", r, who());
        return;
    }
    // Spend the first attack on a harmless camp, then the cast on a real reset. The failed r1
    // setup proved an available attack cannot be reset. Gold remains alive through both actors'
    // setup basics (120 -> 75 -> 30), and enemy HP is unchanged. No action flags or private
    // world state are injected into the production caller.
    const auto prep_at = frame({7, 6});
    if (!vision::has_camp(prep_at)) { std::printf("CP refusal=preparation_camp r=%d who=%s\n", r, who()); return; }
    const auto prep = vision::camp_at(prep_at);
    const float prep_before = prep.health();
    me.attack(prep);
    const bool spent_attack = !me.can_attack();
    const int reset = me.champion_name() == champion_type::tavi ? 0 : 1;
    if (!spent_attack || !me.abilities()[static_cast<std::size_t>(reset)].use() || me.can_cast() || !me.can_attack()) {
        std::printf("CP refusal=real_reset_allowance r=%d who=%s A=%d C=%d\n", r, who(), me.can_attack(), me.can_cast());
        return;
    }
    std::printf("CP phase=prepare r=%d side=%c who=%s gold_before=%.0f gold_after=%.0f spent_attack=%d A=%d C=%d M=%d\n",
        r, side() == 0 ? 'A' : 'B', who(), prep_before, prep.health(), spent_attack,
        me.can_attack(), me.can_cast(), me.can_move());
    mb::Ctx ctx;
    ctx.observe();
    const mb::ChampView* a = nullptr;
    const mb::ChampView* b = nullptr;
    for (const auto& e : ctx.enemies) {
        if (e.type == champion_type::marcella) a = &e;
        if (e.type == champion_type::vesper_vale) b = &e;
    }
    if (!a || !b) { std::printf("CP refusal=personal_target_visibility r=%d who=%s\n", r, who()); return; }
    mb::TurnActions acts;
    log_state("before", ctx, *a, *b, false, acts);
    const bool acted = mb::fight_enemies(ctx, acts, false, b);
    log_state("after", ctx, *a, *b, acted, acts);
}
} // namespace

int main() {
    while (true) {
        try { if (cambc::is_draft()) draft_turn(); else turn(); }
        catch (const std::exception& e) { std::printf("coalition probe error: %s\n", e.what()); }
        cambc::end_turn();
    }
}
