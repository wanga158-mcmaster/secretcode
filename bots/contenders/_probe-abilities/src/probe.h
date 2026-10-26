// Scratch probe helpers (copied per probe; probes share no code). Logging costs fuel; that is fine here.
#pragma once
#include <cambc.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <optional>

namespace probe {
using namespace cambc;

inline int side() { return detail::sdk_my_team(); } // 0 = team A, 1 = team B
inline const char* side_name() { return side() == 0 ? "A" : "B"; }

inline const char* champ_name(champion_type t) {
    static const char* names[] = {"ryth", "vesper", "vendetta", "cassi", "issac", "stellar", "prism",
                                  "marcella", "willow", "ren", "varg", "tavi", "heart"};
    auto i = static_cast<int>(t);
    return i >= 0 && i < 13 ? names[i] : "?";
}
inline const char* camp_name(camp_type t) {
    static const char* names[] = {"wolf", "elk", "gem", "gold", "monolith", "golem"};
    auto i = static_cast<int>(t);
    return i >= 0 && i < 6 ? names[i] : "?";
}

// Map symmetry transform: scenario code is written in team-A coordinates.
inline vec2 own(vec2 p) {
    if (side() == 0) return p;
    switch (map::symmetry()) {
    case symmetry::horizontal: return {map::width() - 1 - p.x, p.y};
    case symmetry::vertical: return {p.x, map::height() - 1 - p.y};
    default: return {map::width() - 1 - p.x, map::height() - 1 - p.y};
    }
}

inline std::uint32_t rnd() { return round_number(); }

#define PLOG(...) do { std::printf("P %s r%u ", ::probe::side_name(), ::cambc::round_number()); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)

inline std::string state_str(champion_state s) {
    std::string r;
    if (!s.contains(champion_state::alive)) r += "dead,";
    if (s.contains(champion_state::recalling)) r += "recall,";
    if (s.contains(champion_state::submitting_gems)) r += "tribute,";
    if (s.contains(champion_state::teleporting)) r += "tp,";
    if (s.contains(champion_state::can_move)) r += "M";
    if (s.contains(champion_state::can_attack)) r += "A";
    if (s.contains(champion_state::can_cast)) r += "C";
    return r;
}

inline std::string buffs_str(const unit& u) {
    std::string r;
    try {
        auto b = u.buffs();
        if (b.has_stun()) r += "stun" + std::to_string(b.get<buff_stun>().duration) + ",";
        if (b.has_shield()) { auto s = b.get<buff_shield>(); r += "shield" + std::to_string(s.duration) + ":" + std::to_string(static_cast<int>(s.value * 100)) + "c,"; }
        if (b.has_enhanced_attack()) r += "enh" + std::to_string(b.get<buff_enhanced_attack>().duration) + ",";
    } catch (const std::exception& e) { r += std::string("buffs-err:") + e.what(); }
    return r;
}

inline std::string restr_str(buff_restrictions r) {
    std::string s;
    if (r.contains(buff_restrictions::move)) s += "move,";
    if (r.contains(buff_restrictions::attack)) s += "attack,";
    if (r.contains(buff_restrictions::cast)) s += "cast,";
    if (r.contains(buff_restrictions::shop)) s += "shop,";
    if (r.contains(buff_restrictions::consumable)) s += "consumable,";
    if (r.contains(buff_restrictions::ward)) s += "ward,";
    return s;
}

// One compact line about self.
inline void log_self(const char* tag = "self") {
    auto& me = self();
    if (!me.is_alive()) {
        PLOG("%s %s dead gold=%u st=%s", tag, champ_name(me.champion_name()), me.gold_count(), state_str(me.state()).c_str());
        return;
    }
    auto p = me.position();
    auto a = me.abilities();
    std::string restr;
    try { restr = restr_str(me.restrictions()); } catch (const std::exception& e) { restr = std::string("err:") + e.what(); }
    // Integers (x100) instead of %f: float formatting costs several thousand fuel per value.
    PLOG("%s %s id=%u pos=%d,%d hp100=%d/%d str100=%d foc100=%d st=%s cd=%d,%d gold=%u gems=%u buffs=%s restr=%s", tag,
         champ_name(me.champion_name()), static_cast<unsigned>(static_cast<handle_champion_t>(me)), p.x, p.y,
         static_cast<int>(me.health() * 100), static_cast<int>(me.max_health() * 100), static_cast<int>(me.strength() * 100),
         static_cast<int>(me.focus() * 100), state_str(me.state()).c_str(), a[0].cooldown(), a[1].cooldown(),
         me.gold_count(), me.gem_count(), buffs_str(me).c_str(), restr.c_str());
}

// Visible camps within radius r of self: type, position, hp, state.
inline void log_camps(int r = 8) {
    auto here = self().position();
    for (auto p : vision::camp_tiles().positions()) {
        if (p.chebyshev_distance(here) > r) continue;
        try {
            auto c = vision::camp_at(p);
            auto s = c.state();
            PLOG("camp %s at %d,%d hp100=%d state=%s buffs=%s", camp_name(c.kind()), p.x, p.y, static_cast<int>(c.health() * 100),
                 s.is_awake() ? "awake" : s.is_asleep() ? "asleep" : "dead", buffs_str(c).c_str());
        } catch (const std::exception& e) { PLOG("camp at %d,%d error %s", p.x, p.y, e.what()); }
    }
}

// One step towards a tile (team-A coordinates already converted by the caller). True if moved.
inline bool step_to(vec2 target) {
    auto& me = self();
    if (me.position() == target || !me.can_move()) return false;
    try { return me.move_towards(target); } catch (const std::exception& e) { PLOG("move error %s", e.what()); return false; }
}

inline std::optional<camp> nearest_camp(int max_range = 99) {
    auto here = self().position();
    std::optional<camp> best;
    std::int64_t bd = 1 << 30;
    for (auto p : vision::camp_tiles().positions()) {
        auto d = p.chebyshev_distance(here);
        if (d <= max_range && d < bd) { bd = d; best = vision::camp_at(p); }
    }
    return best;
}

inline bool try_attack(const unit& u) {
    try { self().attack(u); return true; } catch (const std::exception& e) { PLOG("attack error %s", e.what()); return false; }
}

// Draft the given list in order (one pick per draft turn).
inline void draft_list(const std::vector<champion_draft>& picks) {
    auto mine = draft::current_draft(team::ally);
    for (auto c : picks)
        if (std::find(mine.begin(), mine.end(), c) == mine.end()) { draft::pick(c); return; }
    draft::pick(picks.back());
}
} // namespace probe
