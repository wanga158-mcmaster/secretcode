// Probe: information (what a bot observes), wards, bushes, vision timing, shared/draft memory,
// turn order. Scenario = map width - 30: 0 info-observe, 1 info-wards, 3 info-order.
// Logging is fuel-guarded: a suspended turn resumes with stale locals and garbles the evidence.
#include "script.h"
#include <span>
#include <deque>

using namespace cambc;
using namespace probe;

static int scenario() { return map::width() - 30; }
static int W() { return map::width(); }
// Team-A script coordinate whose mirror for team B is the absolute tile x (horizontal symmetry).
static int bx(int x) { return W() - 1 - x; }
static bool fuel_ok(std::uint64_t need = 45000) { return fuel_remaining() > need; }

template <class F> static std::string q(F f) {
    try { return f(); }
    catch (const error& e) { return "E" + std::to_string(static_cast<int>(e.code())); }
    catch (const std::exception& e) { return std::string("X(") + e.what() + ")"; }
}
static std::string fs(float v) { char b[32]; std::snprintf(b, sizeof b, "%.2f", v); return b; }
static std::string is(long long v) { return std::to_string(v); }

// Every per-champion query from this perspective, each with its value or error code.
static void observe(const champion& c, const char* tag) {
    const auto h = static_cast<unsigned>(static_cast<handle_champion_t>(c));
    std::string s = std::string(tag) + " h" + std::to_string(h);
    s += " name=" + q([&] { return std::string(champ_name(c.champion_name())); });
    s += " team=" + q([&] { return is(static_cast<int>(c.team())); });
    s += " alive=" + q([&] { return is(c.is_alive()); });
    s += " vis=" + q([&] { return is(c.is_visible()); });
    s += " pos=" + q([&] { auto p = c.position(); return is(p.x) + "," + is(p.y); });
    s += " hp=" + q([&] { return fs(c.health()); }) + "/" + q([&] { return fs(c.max_health()); });
    s += " str=" + q([&] { return fs(c.strength()); }) + " foc=" + q([&] { return fs(c.focus()); });
    s += " rng=" + q([&] { return is(c.attack_range()); });
    s += " st=" + q([&] { return state_str(c.state()) + "/" + is(c.state().bits()); });
    s += " chan=" + q([&] { return is(c.remaining_channel_time()); });
    s += " cd=" + q([&] { return is(c.abilities()[0].cooldown()); }) + "," + q([&] { return is(c.abilities()[1].cooldown()); });
    s += " buffs=" + q([&] { return buffs_str(c); });
    s += " restr=" + q([&] { return is(c.restrictions().bits()); });
    s += " gems=" + q([&] { return is(c.gem_count()); });
    s += " items=" + q([&] {
        auto ic = c.item_count();
        std::string r;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(item_type::count); ++i)
            if (ic[item_type{i}]) r += is(i) + "x" + is(ic[item_type{i}]) + ";";
        return r.empty() ? std::string("none") : r;
    });
    s += " cons=" + q([&] { return is(c.consumables()[consumable_type::tp_crystal].count()) + "," + is(c.consumables()[consumable_type::altar_lock].count()); });
    s += " gold(raw)=" + q([&] { std::uint32_t g{}; detail::check(detail::sdk_champion_gold(h, &g)); return is(g); });
    s += " prio=" + q([&] { return is(c.prev_priority()) + "/" + is(c.priority()); });
    s += " vtiles=" + q([&] { return is(c.visible_tiles().count()); });
    PLOG("%s", s.c_str());
}

static std::vector<map::camp_group>* groups = nullptr;
static void observe_camps(const char* tag) {
    if (!groups) groups = new std::vector<map::camp_group>(map::camp_groups());
    for (const auto& g : *groups) {
        if (!fuel_ok(30000)) { PLOG("camps skipped (fuel)"); return; }
        std::string s = std::string(tag) + " group" + is(static_cast<int>(static_cast<handle_camp_group_t>(g))) + " gold=" + is(g.gold_value()) +
                        " gems=" + is(g.gem_value()) + " maxresp=" + is(g.max_respawn_timer()) + " state=" + q([&] {
                            auto st = g.state();
                            if (std::holds_alternative<map::camp_group_alive>(st)) return std::string("alive");
                            if (std::holds_alternative<map::camp_group_reward_available>(st)) return std::string("reward");
                            return "respawning" + is(std::get<map::camp_group_respawning>(st).timer);
                        });
        for (const auto& m : g.camps()) {
            auto sp = m.spawn_position();
            s += " [" + std::string(camp_name(m.kind())) + "@" + is(sp.x) + "," + is(sp.y) + " vis=" + q([&] { return is(m.is_visible()); }) +
                 " state=" + q([&] { auto x = m.state(); return std::string(x.is_dead() ? "dead" : x.is_awake() ? "awake" : "asleep"); }) +
                 " hp=" + q([&] { return fs(m.health()); }) + " inv=" + is(vision::in_vision(sp)) + " has=" + is(vision::has_camp(sp)) +
                 " awakemask=" + is(vision::awake_camp_tiles().is_set(sp)) + " asleepmask=" + is(vision::asleep_camp_tiles().is_set(sp)) + "]";
        }
        PLOG("%s", s.c_str());
    }
}

static std::string shared_str() {
    std::string s;
    for (std::size_t i = 0; i < shared_memory_buffer::size(); ++i) s += is(static_cast<int>(static_cast<std::uint8_t>(shared_memory[i]))) + " ";
    return s;
}

// Shape of a vision mask relative to a centre: tile count, max Chebyshev, max squared Euclid, sample tiles.
static std::string shape(const bitboard& b, vec2 c, int r) {
    int maxc = 0, maxe = 0;
    for (auto p : b.positions()) {
        int dx = p.x - c.x, dy = p.y - c.y;
        maxc = std::max(maxc, std::max(std::abs(dx), std::abs(dy)));
        maxe = std::max(maxe, dx * dx + dy * dy);
    }
    auto has = [&](int dx, int dy) { vec2 p{c.x + dx, c.y + dy}; return map::is_in_bounds(p) && b.is_set(p) ? 1 : 0; };
    return "n=" + is(b.count()) + " maxCheb=" + is(maxc) + " maxE2=" + is(maxe) + " corner(" + is(r) + "," + is(r) + ")=" + is(has(r, r)) +
           " edge(" + is(r) + ",0)=" + is(has(r, 0)) + " (" + is(r + 1) + ",0)=" + is(has(r + 1, 0));
}

static std::vector<Cmd> script(champion_type who) {
    const int s = scenario();
    using enum Op;
    if (s == 0) {
        // gold clusters (4,9),(4,10); gem cluster (4,19); altar (12,18). Meeting line x=13 (B mirrored x=16).
        switch (who) {
        case champion_type::varg:
            return {{1, GOTO, 5, 10}, {6, ATKN, 4, 10}, {16, ATKN, 4, 9}, {24, STOP}, {24, GOTO, 4, 10}, {34, GOTO, 1, 13},
                    {38, BUY, 0, 0, 0, 1}, {39, BUYC, 0, 0, 0}, {40, GOTO, 13, 13}, {55, CASTSELF, 0, 0, 1}, {62, RECALL},
                    {68, GOTO, 13, 13}, {84, USEC, 13, 10, 0}, {95, RESIGN}};
        case champion_type::marcella:
            return {{1, GOTO, 5, 19}, {6, ATKN, 4, 19}, {16, STOP}, {16, GOTO, 4, 19}, {26, GOTO, 13, 17}, {50, CASTSELF, 0, 0, 0},
                    {58, TRIB, 12, 18}};
        case champion_type::tavi:
            return {{1, GOTO, 13, 15}, {57, CAST, 16, 13, 1}};
        default: return {};
        }
    }
    if (s == 1) {
        // Ward test spot T (6,8): wall (6,6), hole (8,8), altar (4,8), bush (6,10); diagonal (8,10) plain.
        // Bush clusters K (13..14, 20..21) and its mirror; open lane y=4 for vision timing.
        if (side() == 0) {
            switch (who) {
            case champion_type::tavi: return {{1, GOTO, 6, 8}, {50, GOTO, 13, 19}};
            case champion_type::varg: return {{1, GOTO, 7, 9}, {50, GOTO, 13, 21}};
            case champion_type::vesper_vale: return {{1, GOTO, 10, 4}, {40, GOTO, 16, 21}, {80, RESIGN}};
            default: return {};
            }
        }
        switch (who) {
        case champion_type::vesper_vale: return {{1, GOTO, bx(22), 4}, {20, GOTO, bx(15), 4}, {32, GOTO, bx(14), 20}};
        case champion_type::tavi: return {{1, GOTO, bx(28), 12}, {12, GOTO, bx(7), 10}};
        case champion_type::varg: return {{1, GOTO, bx(24), 16}, {14, GOTO, bx(4), 14}};
        default: return {};
        }
    }
    if (s == 3) {
        // Centre wolf (16,22); golem (6,4) and mirror (26,4). Team A: cassi&issac, vesper, ryth; B: vesper, ryth, stellar.
        if (side() == 0) {
            switch (who) {
            case champion_type::cassi: return {{1, GOTO, 13, 22}, {20, ATK, 16, 22}};
            case champion_type::issac: return {{1, GOTO, 5, 13}};
            case champion_type::vesper_vale: return {{1, GOTO, 5, 15}, {90, RESIGN}};
            case champion_type::ryth: return {{1, GOTO, 3, 17}};
            default: return {};
            }
        }
        switch (who) {
        case champion_type::vesper_vale: return {{1, GOTO, 13, 22}, {4, PRIO, 0, 0, 5}};
        case champion_type::stellar: return {{1, GOTO, 6, 7}, {12, ATK, 6, 4}};
        case champion_type::ryth: return {{1, GOTO, 4, 12}};
        default: return {};
        }
    }
    return {};
}

static Runner* runner = nullptr;

static std::string wards_str() {
    return q([&] { std::string w; for (auto x : self().ward_positions()) w += is(x.x) + "," + is(x.y) + ";"; return w; });
}
static bool ward_try(vec2 a, const char* what, bool absolute = false) {
    auto& me = self();
    const vec2 p = absolute ? a : own(a);
    std::string r = q([&] { return std::string(me.place_ward(p) ? "true" : "false"); });
    PLOG("wardtry %s at %d,%d -> %s cooldown=%s wards=%s", what, p.x, p.y, r.c_str(), q([&] { return is(me.ward_cooldown()); }).c_str(), wards_str().c_str());
    return r == "true";
}

struct WardTest { vec2 p; const char* what; };
static std::deque<WardTest> ward_queue{{{8, 10}, "own-ward-tile"}, {{6, 8}, "own-tile"}, {{6, 10}, "unseen-bush"},
                                       {{7, 10}, "enemy-occupied"}, {{7, 9}, "ally-occupied"}};

static void hook_wards() {
    auto& me = self();
    const int r = static_cast<int>(round_number());
    if (side() != 0) {
        if (me.champion_name() == champion_type::tavi && r == 3) ward_try({29, 14}, "own-spawn", true);
        if (me.champion_name() == champion_type::varg && r >= 34 && r <= 36 && me.ward_cooldown() == 0) ward_try({2, 14}, "enemy-spawn", true);
        if (me.champion_name() == champion_type::varg && r == 37) ward_try({5, 12}, "plain-near-enemy-spawn", true);
        if (me.champion_name() == champion_type::tavi && (r == 46 || r == 47)) {
            std::string w;
            for (const auto& x : vision::wards()) w += is(x.position().x) + "," + is(x.position().y) + " team=" + is(static_cast<int>(x.team())) +
                " owner=" + is(static_cast<int>(static_cast<handle_champion_t>(x.owner()))) + " life=" + (x.remaining_time() ? is(*x.remaining_time()) : std::string("inf")) + ";";
            PLOG("enemy sees wards: %s enemy-ward-mask n=%u", w.c_str(), vision::ward_tiles(team::enemy).count());
        }
        if (me.champion_name() == champion_type::vesper_vale && r >= 62 && r <= 64)
            PLOG("M at %d,%d sees enemies: %s", me.position().x, me.position().y,
                 q([&] { std::string s; for (auto p : vision::champion_tiles(team::enemy).positions()) s += is(p.x) + "," + is(p.y) + ";"; return s; }).c_str());
        return;
    }
    if (me.champion_name() == champion_type::tavi) {
        if (r == 14) {
            vec2 open{15, 12};
            PLOG("hyp champion vision at 15,12: %s", shape(self().visible_tiles_from(open), open, 8).c_str());
            PLOG("hyp ward vision at 15,12: %s", shape(ward::visible_tiles_from(open), open, 6).c_str());
        }
        if (r == 15) ward_try({8, 10}, "diag(2,2)");
        if (r == 16) ward_try({6, 10}, "bush-while-recharging");
        if (r >= 17 && r < 50 && me.ward_cooldown() == 0) {
            while (!ward_queue.empty() && fuel_ok(30000)) {
                auto t = ward_queue.front();
                ward_queue.pop_front();
                if (ward_try(t.p, t.what)) break;
            }
        }
        if (r >= 63 && r <= 64 && fuel_ok(40000)) {
            // Far from the ward: is the ward's vision part of self().visible_tiles() and of vision::visible_tiles()?
            auto all = vision::visible_tiles();
            auto mine = self().visible_tiles();
            auto hyp_own = self().visible_tiles_from(me.position());
            std::string w;
            for (auto x : me.ward_positions()) {
                auto wm = ward::visible_tiles_from(x);
                w += is(x.x) + "," + is(x.y) + " inv(ward)=" + is(vision::in_vision(x)) + " inv(ward-3y)=" + is(vision::in_vision({x.x, x.y - 3})) +
                     " ward&all=" + is((wm & all).count()) + "/" + is(wm.count()) + " all==hyp|ward:" + is(all == (hyp_own | wm)) + ";";
            }
            PLOG("vision all=%u self.visible_tiles=%u hyp_own=%u wards=%s", all.count(), mine.count(), hyp_own.count(), w.c_str());
        }
    }
    if (me.champion_name() == champion_type::vesper_vale && r >= 18 && r <= 30 && fuel_ok()) {
        // Observer O on lane y=4 (until r50), later P3 inside the mirrored cluster.
        std::string s;
        for (auto p : vision::champion_tiles(team::enemy).positions())
            s += is(p.x) + "," + is(p.y) + (vision::has_champion(p) ? "+" : "-") + q([&] { auto c = vision::champion_at(p); return std::string(champ_name(c.champion_name())); }) + ";";
        std::string g;
        for (const auto& c : cambc::champions(team::enemy))
            if (c.champion_name() == champion_type::vesper_vale) g = q([&] { auto p = c.position(); return is(p.x) + "," + is(p.y); });
        PLOG("O occupancy=%s handlepos=%s", s.c_str(), g.c_str());
        if (r == 24) {
            auto before = vision::visible_tiles();
            bool moved = q([&] { return std::string(me.move({1, 0}) ? "1" : "0"); }) == "1";
            auto after = vision::visible_tiles();
            PLOG("O moved=%d to %d,%d vision before=%u after=%u same=%d self_after=%u", moved, me.position().x, me.position().y,
                 before.count(), after.count(), static_cast<int>(before == after), self().visible_tiles().count());
        }
    }
    if (r == 63 && fuel_ok()) {
        // Ward-vision sharing: the owner's ward is at (7,10); (7,7) is seen only by that ward.
        PLOG("ward share: %s at %d,%d inv(7,7)=%d sees ally ward=%u", champ_name(me.champion_name()), me.position().x, me.position().y,
             vision::in_vision({7, 7}), vision::ward_tiles(team::ally).count());
    }
    if (r >= 62 && r <= 68 && fuel_ok()) {
        // Bush experiment: M (enemy vesper) hides at absolute (14,20).
        const vec2 hide{14, 20};
        std::string s = std::string(champ_name(me.champion_name())) + " at " + is(me.position().x) + "," + is(me.position().y) +
                        " inv(hide)=" + q([&] { return is(vision::in_vision(hide)); }) + " has(hide)=" + q([&] { return is(vision::has_champion(hide)); });
        for (const auto& c : cambc::champions(team::enemy))
            if (c.champion_name() == champion_type::vesper_vale) s += " Mvis=" + q([&] { return is(c.is_visible()); });
        PLOG("bush %s", s.c_str());
        if (me.champion_name() == champion_type::tavi && r == 64) {
            for (const auto& c : cambc::champions(team::enemy))
                if (c.champion_name() == champion_type::vesper_vale) {
                    PLOG("attack hidden M -> %s", q([&] { me.attack(c); return std::string("ok"); }).c_str());
                    PLOG("cast stun at hidden M -> %s", q([&] { return std::string(me.abilities()[1].use(c) ? "true" : "false"); }).c_str());
                }
        }
        if (me.champion_name() == champion_type::tavi && r == 66) {
            PLOG("move onto hidden -> %s can_move=%d", q([&] { return std::string(me.move_absolute(hide) ? "true" : "false"); }).c_str(), me.can_move());
            PLOG("after: inv=%d has=%s", vision::in_vision(hide), q([&] { return is(vision::has_champion(hide)); }).c_str());
            PLOG("move again (to 12,19) -> %s", q([&] { return std::string(me.move_absolute({12, 19}) ? "true" : "false"); }).c_str());
        }
        if (me.champion_name() == champion_type::tavi && r == 67)
            PLOG("next round: inv(hide)=%d has=%s", vision::in_vision(hide), q([&] { return is(vision::has_champion(hide)); }).c_str());
    }
}

static int died_round = -1;
static void hook_order() {
    auto& me = self();
    const int r = static_cast<int>(round_number());
    int slot = 5;
    switch (me.champion_name()) {
    case champion_type::cassi: slot = 0; break;
    case champion_type::issac: slot = 1; break;
    case champion_type::vesper_vale: slot = 2; break;
    case champion_type::ryth: slot = 3; break;
    case champion_type::stellar: slot = 4; break;
    default: break;
    }
    if (r <= 6) PLOG("shared before write: %s", shared_str().c_str());
    const std::uint8_t val = static_cast<std::uint8_t>((side() == 0 ? 0 : 128) + (r & 0x7f));
    shared_memory[static_cast<std::size_t>(slot)] = val;
    if (r <= 6) PLOG("shared write slot %d = %d", slot, val);
    if (r == 2) {
        std::string d;
        for (std::size_t i = 0; i < std::min<std::size_t>(draft_memory.size(), 20); ++i) d += is(static_cast<int>(draft_memory[i])) + " ";
        PLOG("draft memory size=%zu bytes: %s", draft_memory.size(), d.c_str());
    }
    if (r == 3 && me.champion_name() != champion_type::issac) {
        std::array<std::byte, 4> b{std::byte{7}, std::byte{7}, std::byte{7}, std::byte{7}};
        PLOG("draft assign in play -> %s", q([&] { draft_memory = std::span<const std::byte>{b}; return std::string("ok"); }).c_str());
    }
    if (r == 5 && me.champion_name() != champion_type::issac) {
        std::array<std::byte, 4> b{std::byte{7}, std::byte{7}, std::byte{7}, std::byte{7}};
        PLOG("raw draft submit in play -> %s", q([&] { detail::check(detail::sdk_bc_submit_draft_memory(b.data(), b.size())); return std::string("ok"); }).c_str());
    }
    if (r == 7 && me.champion_name() == champion_type::ryth) {
        PLOG("raw shared oob write -> %s", q([&] { std::uint8_t v = 1; detail::check(detail::sdk_write_shared_memory(8, &v, 1)); return std::string("ok"); }).c_str());
        PLOG("raw shared oob read -> %s", q([&] { std::uint8_t v = 1; detail::check(detail::sdk_read_shared_memory(6, &v, 4)); return std::string("ok"); }).c_str());
    }
    if (r == 9 && me.champion_name() == champion_type::issac) {
        // Fuel of one throwing query vs a returning one (release build).
        champion hidden = cambc::champions(team::enemy).front();
        auto f0 = fuel_remaining();
        (void)vision::has_champion({0, 0});
        auto f1 = fuel_remaining();
        std::string x = q([&] { return is(hidden.position().x); });
        auto f2 = fuel_remaining();
        int code = 0;
        { detail::champion_info ci{}; code = detail::sdk_champion(static_cast<std::uint32_t>(static_cast<handle_champion_t>(hidden)), &ci); }
        auto f3 = fuel_remaining();
        PLOG("fuel: has_champion=%llu throwing position()=%llu (%s) raw sdk_champion code %d=%llu", static_cast<unsigned long long>(f0 - f1),
             static_cast<unsigned long long>(f1 - f2), x.c_str(), code, static_cast<unsigned long long>(f2 - f3));
    }
    if (r == 25 && me.champion_name() == champion_type::ryth) {
        for (int pass = 0; pass < 2; ++pass)
            for (const auto& c : cambc::champions(team::ally))
                if (c.champion_name() != champion_type::ryth && c.is_visible() == (pass == 0) && fuel_ok(60000)) observe(c, "ally");
    }
    if (!me.is_alive()) {
        if (died_round < 0) died_round = r;
        const int k = r - died_round;
        if (k == 1) PLOG("dead set_priority -> %s", q([&] { me.set_priority(-5); return std::string("ok"); }).c_str());
        if (k == 2) PLOG("dead buy item -> %s; buy lock -> %s", q([&] { me.buy(item_type::monster_shard_of_strength); return std::string("ok"); }).c_str(),
                         q([&] { me.buy(consumable_type::altar_lock); return std::string("ok"); }).c_str());
        if (k == 3) PLOG("dead move -> %s; ward -> %s", q([&] { return std::string(me.move_absolute({20, 4}) ? "true" : "false"); }).c_str(),
                         q([&] { return std::string(me.place_ward({20, 4}) ? "true" : "false"); }).c_str());
        if (k == 4) PLOG("dead recall -> %s; cast -> %s; attack -> %s", q([&] { me.recall(); return std::string("ok"); }).c_str(),
                         q([&] { return std::string(me.abilities()[0].use({20, 4}) ? "true" : "false"); }).c_str(),
                         q([&] { me.attack(cambc::champions(team::enemy).front()); return std::string("ok"); }).c_str());
        if (k == 5) PLOG("dead pos=%s hp=%s st=%s vision n=%s wardcd=%s prio=%s/%s canbuy=%s cansell=%s",
                         q([&] { auto p = me.position(); return is(p.x) + "," + is(p.y); }).c_str(), q([&] { return fs(me.health()); }).c_str(),
                         q([&] { return state_str(me.state()); }).c_str(), q([&] { return is(vision::visible_tiles().count()); }).c_str(),
                         q([&] { return is(me.ward_cooldown()); }).c_str(), q([&] { return is(me.prev_priority()); }).c_str(),
                         q([&] { return is(me.priority()); }).c_str(), q([&] { return is(me.can_buy(item_type::monster_shard_of_strength)); }).c_str(),
                         q([&] { return is(me.can_sell()); }).c_str());
    } else if (died_round >= 0) {
        PLOG("respawned at %d,%d hp=%.1f (died r%d)", me.position().x, me.position().y, me.health(), died_round);
        died_round = -2;
    }
}

static int obs_turn = 0;
static void play_turn() {
    if (!runner) {
        runner = new Runner{};
        runner->cmds = script(self().champion_name());
        runner->log_enemies = false;
        runner->log_radius = scenario() == 3 ? 4 : 1;
    }
    const int r = static_cast<int>(round_number());
    const int s = scenario();
    if (s == 0) {
        if (r == 6 && side() == 0 && self().champion_name() == champion_type::tavi) {
            for (const auto& c : cambc::champions(team::enemy)) { if (!fuel_ok(40000)) break; observe(c, "obs-hid"); }
        }
        if (r >= 48 && fuel_ok(50000)) {
            // One visible enemy per turn, rotating.
            std::vector<champion> vis;
            for (const auto& c : cambc::champions(team::enemy)) if (c.is_visible()) vis.push_back(c);
            if (!vis.empty()) observe(vis[static_cast<std::size_t>(obs_turn++) % vis.size()], "obs-vis");
        }
        if ((r == 21 || r == 30 || r == 36 || r == 45) && self().champion_name() == champion_type::varg) observe_camps("camps");
    }
    if (s == 3) hook_order();
    runner->turn();
    if (s == 1) hook_wards();
}

static void draft_turn() {
    const int s = scenario();
    const int picks = static_cast<int>(draft::current_draft(team::ally).size());
    if (s == 3) {
        if (picks == 0) {
            std::array<std::byte, 16> b{};
            for (int i = 0; i < 16; ++i) b[static_cast<std::size_t>(i)] = std::byte(static_cast<unsigned char>(10 + i + (side() ? 100 : 0)));
            draft_memory = std::span<const std::byte>{b};
        } else if (picks == 1) {
            std::array<std::byte, 4> b{std::byte{100}, std::byte{101}, std::byte{102}, std::byte{103}};
            draft_memory = std::span<const std::byte>{b};
        }
        std::string d;
        for (std::size_t i = 0; i < std::min<std::size_t>(draft_memory.size(), 20); ++i) d += is(static_cast<int>(draft_memory[i])) + " ";
        std::printf("P %s draft pick%d snapshot size=%zu %s fuel=%llu\n", side_name(), picks, draft_memory.size(), d.c_str(),
                    static_cast<unsigned long long>(fuel_remaining()));
        if (side() == 0) draft_list({champion_draft::cassi_and_issac, champion_draft::vesper_vale, champion_draft::ryth});
        else draft_list({champion_draft::vesper_vale, champion_draft::ryth, champion_draft::stellar});
        return;
    }
    if (s == 0) draft_list({champion_draft::marcella, champion_draft::varg, champion_draft::tavi});
    else draft_list({champion_draft::tavi, champion_draft::varg, champion_draft::vesper_vale});
}

int main() {
    while (true) {
        try {
            if (is_draft()) draft_turn();
            else play_turn();
        } catch (const std::exception& e) {
            std::printf("probe error: %s\n", e.what());
        }
        end_turn();
    }
}
