// Turn orchestration, observation, the pack protocol and the leader's objective choice.
#include "bot.h"
#include <cmath>
#include "config.h"
#include "model.h"
#include "nav_journal.h"

namespace wb {

namespace {
std::array<std::uint8_t, 8> read_shared() {
    std::array<std::uint8_t, 8> b{};
    cambc::shared_memory.read(0, std::span<std::uint8_t>{b.data(), b.size()});
    return b;
}
void write_shared(const PackMsg& m) {
    auto b = m.encode();
    cambc::shared_memory.write(0, std::span<std::uint8_t>{b.data(), b.size()});
}
} // namespace

volatile int g_noise_sink = 0;

void Bot::init() {
    const long f0 = fuel();
    world_.init();
    const long f1 = fuel();
    allies_ = cambc::champions(team::ally);
    enemies_ = cambc::champions(team::enemy);
    for (std::size_t i = 0; i < allies_.size(); ++i)
        if (allies_[i] == cambc::self()) me_ = static_cast<int>(i);
    kit_ = &kit_of(cambc::self().champion_name());
    {
        const auto ab = cambc::self().abilities();
        abil_.assign(ab.begin(), ab.end());
    }
    emem_.assign(enemies_.size(), EnemyMemory{});
    amem_.assign(allies_.size(), AllyMemory{});
    init_ = true;
    const long f2 = fuel();
    // The first turn has twice the fuel: warm the fields every champion needs.
    world_.home_field();
    const long f3 = fuel();
    world_.altar_field();
    const long f4 = fuel();
    if constexpr (cfg::debug_every > 0)
        std::printf("init fuel: start %ld world %ld kits %ld home %ld altar %ld\n", f0, f0 - f1, f1 - f2, f2 - f3, f3 - f4);
}

void Bot::sense() {
    auto& me = cambc::self();
    pos_ = me.position();
    hp_ = me.health();
    maxhp_ = me.max_health();
    at_spawn_ = world_.my_spawn.is_set(pos_);
    vis_allies_.clear();
    for (std::size_t i = 0; i < allies_.size(); ++i) {
        if (static_cast<int>(i) == me_) continue;
        const auto& a = allies_[i];
        try {
            if (!a.is_visible()) continue;
            vis_allies_.push_back(read_unit(a, static_cast<int>(i), false));
            const auto& u = vis_allies_.back();
            amem_[i] = {u.pos, round_, u.hp, u.maxhp, u.kit->dps(u.str, u.focus)};
        } catch (const cambc::error&) {
        }
    }
    vis_enemies_.clear();
    for (std::size_t i = 0; i < enemies_.size(); ++i) {
        const auto& e = enemies_[i];
        try {
            if (!e.is_visible()) continue;
            vis_enemies_.push_back(read_unit(e, static_cast<int>(i), true));
        } catch (const cambc::error&) {
        }
    }
    world_.observe(round_);
    update_enemy_memory();
    blocked_ready_ = false;
}

void Bot::update_enemy_memory() {
    for (std::size_t i = 0; i < enemies_.size(); ++i) {
        bool alive = true;
        try { alive = enemies_[i].is_alive(); } catch (const cambc::error&) {}
        auto& m = emem_[i];
        if (!alive && !m.dead) { m.dead = true; m.death_round = round_; }
        if (alive && m.dead) { m.dead = false; m.seen = -10000; }
    }
    for (const auto& u : vis_enemies_) {
        auto& m = emem_[static_cast<std::size_t>(u.index)];
        m.pos = u.pos;
        m.seen = round_;
        m.hp = u.hp + u.shield;
        m.maxhp = u.maxhp;
        m.dps = u.kit->dps(u.str, u.focus);
        m.gems = u.gems;
    }
}

int Bot::acting_leader() const {
    for (int i = 0; i < static_cast<int>(allies_.size()) && i < 8; ++i)
        if (pack_.avail & (1u << i)) return i;
    return -1;
}

void Bot::write_pack() { write_shared(pack_); }

void Bot::report_sighting() {
    if (vis_enemies_.empty() || !pack_.has_anchor()) return;
    const Unit* nearest = nullptr;
    for (const auto& u : vis_enemies_)
        if (!nearest || cheb(u.pos, pos_) < cheb(nearest->pos, pos_)) nearest = &u;
    pack_.sighting = true;
    pack_.sdx = nearest->pos.x - pack_.ax;
    pack_.sdy = nearest->pos.y - pack_.ay;
    pack_.sight_age = 0;
}

// Pack strength for planning: me plus every available ally at its last known state (vision alone
// would make the estimate flicker whenever a bush or a wall hides an ally).
void Bot::pack_strength(float& dps, float& spare, int& members) const {
    auto& me = cambc::self();
    dps = kit_->dps(me.strength(), me.focus());
    spare = std::max(0.0f, hp_ - cfg::recall_frac * maxhp_);
    members = 1;
    for (std::size_t i = 0; i < amem_.size() && i < 8; ++i) {
        if (static_cast<int>(i) == me_ || !(pack_.avail & (1u << i))) continue;
        const auto& a = amem_[i];
        if (round_ - a.seen > cfg::ally_memory) continue;
        dps += a.dps;
        spare += std::max(0.0f, a.hp - cfg::recall_frac * a.maxhp);
        ++members;
    }
}

void Bot::lead() {
    i_lead_ = true;
    if (leader_since_ < 0) leader_since_ = round_;
    if (!pack_.has_anchor()) { pack_.ax = pos_.x; pack_.ay = pos_.y; }
    // Age the shared sighting: it goes stale after three rounds without a refresh.
    if (pack_.sight_age < 3) ++pack_.sight_age;
    // Enemies resting on their own spawn heal every turn: they are not a fight unless they are
    // about to step on us.
    bool enemy_close = false;
    for (const auto& u : vis_enemies_)
        if (cheb(u.pos, pos_) <= 9 && (!world_.enemy_spawn.is_set(u.pos) || cheb(u.pos, pos_) <= 2)) enemy_close = true;
    if (enemy_close) {
        const float ratio = local_ratio(true);
        pack_.focus = choose_focus();
        pack_.mode = ratio >= cfg::flee_ratio ? Mode::fight : Mode::retreat;
        // The fight happens where the leader is: the anchor follows it.
        pack_.ax = pos_.x;
        pack_.ay = pos_.y;
    } else {
        pack_.focus = -1;
        if (pack_.mode == Mode::fight || pack_.mode == Mode::retreat) pack_.mode = Mode::travel;
        choose_objective();
        steer_anchor();
    }
    pack_.stamp = round_ & 255;
    report_sighting();
    if constexpr (cfg::debug_every > 0) {
        if (round_ % cfg::debug_every == 0 && round_ <= cfg::debug_until) {
            const int g = pack_.objective;
            std::printf("L r%d me%d (%d,%d) hp%.0f F%d mode%d st%d A(%d,%d) dA%d avail%x busy%x vis%d en%d fuel%ld\n", round_, me_,
                pos_.x, pos_.y, hp_, g, static_cast<int>(pack_.mode),
                g >= 0 ? static_cast<int>(world_.groups[static_cast<std::size_t>(g)].state) : -1, pack_.ax, pack_.ay,
                g >= 0 ? world_.dist_to_group({pack_.ax, pack_.ay}, g) : -1, pack_.avail, pack_.busy,
                static_cast<int>(vis_allies_.size()), static_cast<int>(vis_enemies_.size()), fuel());
        }
    }
}

// The focus group: the camp group with the best reward rate for the whole pack as one unit, seen
// from the anchor: travel + clear at pack damage + capture, feasible when the forecast camp damage
// fits the pack's spare health. Harmless groups count at a discount (one member takes them as a
// task). Hysteresis keeps the current focus unless a rival is clearly better.
void Bot::choose_objective() {
    float dps = 0, spare = 0;
    int members = 1;
    pack_strength(dps, spare, members);
    const vec2 anchor{pack_.ax, pack_.ay};
    const int prev = pack_.objective;
    int best = -1;
    float best_rate = 0, prev_rate = -1;
    for (int gi = 0; gi < static_cast<int>(world_.groups.size()); ++gi) {
        if (fuel() < 30000) break;
        auto& g = world_.groups[static_cast<std::size_t>(gi)];
        if (g.banned_until > round_) continue;
        if (g.monolith && round_ < cfg::monolith_min_round) continue;
        const int travel = world_.dist_to_group(anchor, gi);
        if (travel >= (1 << 19)) continue;
        float wait = 0;
        if (g.state == GState::respawning) {
            wait = static_cast<float>(std::max(0, g.ready_round - round_ - travel));
            if (wait > cfg::max_respawn_wait) continue;
        }
        const float value = static_cast<float>(g.gold) * cfg::gold_value + static_cast<float>(g.gems) * cfg::gem_value;
        const float factor = !g.pack_job && members > 1 ? cfg::harmless_focus : 1.0f;
        const float bound = factor * value / (static_cast<float>(travel) + wait + cfg::capture_cost + 1.0f);
        if (bound <= best_rate && gi != prev) continue;  // cannot beat the best: skip the forecast
        float clear = 0;
        if (g.state != GState::reward) {
            model::Monster buf[32];
            const int n = world_.believed_monsters(gi, round_, buf);
            const auto f = model::forecast_sorted(buf, n, dps);
            if (!g.harmless && f.damage * cfg::camp_safety > spare) continue;
            clear = static_cast<float>(f.rounds);
        }
        const float rate = factor * value / (static_cast<float>(travel) + wait + clear + cfg::capture_cost + 1.0f);
        if (gi == prev) prev_rate = rate;
        if (rate > best_rate) { best_rate = rate; best = gi; }
    }
    if constexpr (cfg::debug_every > 0) {
        if (round_ <= cfg::debug_until && (round_ % cfg::debug_every == 0 || best != prev))
            std::printf("O r%d prev%d rate%.2f best%d rate%.2f\n", round_, prev, prev_rate, best, best_rate);
    }
    if (prev >= 0 && prev_rate > 0 && best != prev && best_rate < prev_rate * cfg::retarget_ratio) best = prev;
    if (best != pack_.objective) {
        objective_since_ = round_;
        gather_since_ = -1;
        if (pack_.mode == Mode::gather || pack_.mode == Mode::clear) pack_.mode = Mode::travel;
    }
    pack_.objective = best;
}

// The anchor walks the focus group's field one tile at a time, but only while the flock is not
// busy with the zone's camps, so members harvest as the pack moves and nobody is left behind.
// At a dangerous focus group's edge the pack gathers, then clears it together.
void Bot::steer_anchor() {
    const int f = pack_.objective;
    if (f < 0) { pack_.mode = Mode::travel; return; }
    auto& g = world_.groups[static_cast<std::size_t>(f)];
    // Progress watchdog (safeguard against deadlocks the rules did not foresee).
    {
        const float hp = world_.remaining_hp(f, round_);
        const bool engaged = pack_.mode == Mode::gather || pack_.mode == Mode::clear;
        if (watch_group_ != f || !engaged || hp < watch_hp_ - 1.0f || g.state != GState::alive) {
            watch_group_ = f;
            watch_round_ = round_;
            watch_hp_ = hp;
        } else if (round_ - watch_round_ > cfg::focus_watchdog) {
            g.banned_until = round_ + cfg::focus_ban;
            watch_group_ = -1;
            pack_.objective = -1;
            pack_.mode = Mode::travel;
            return;
        }
    }
    vec2 anchor{pack_.ax, pack_.ay};
    if (!in_bounds(anchor) || world_.terrain.is_set(anchor)) anchor = pos_;
    if (g.state == GState::reward) {
        pack_.mode = Mode::travel;
        pack_.ax = g.center.x;
        pack_.ay = g.center.y;
        return;
    }
    bool awake = false;
    for (const auto& a : awake_list_)
        if (a.group == f) awake = true;
    const int edge = g.pack_job ? g.range + 1 : 1;
    const int dA = world_.dist_to_group(anchor, f);
    if (g.pack_job && (awake || pack_.mode == Mode::clear)) {
        pack_.mode = Mode::clear;
        return;
    }
    if (dA <= edge) {
        if (!g.pack_job) { pack_.mode = Mode::travel; return; }
        // At the edge: wait for the pack, bounded by patience.
        int expected = 1, gathered = world_.dist_to_group(pos_, f) <= edge + 3 ? 1 : 0;
        for (int i = 0; i < static_cast<int>(allies_.size()) && i < 8; ++i)
            if (i != me_ && (pack_.avail & (1u << i))) ++expected;
        for (const auto& a : vis_allies_)
            if ((pack_.avail & (1u << a.index)) && world_.dist_to_group(a.pos, f) <= edge + 3) ++gathered;
        if (gather_since_ < 0) gather_since_ = round_;
        pack_.mode = (gathered >= expected || round_ - gather_since_ >= cfg::gather_patience) ? Mode::clear : Mode::gather;
        return;
    }
    pack_.mode = Mode::travel;
    gather_since_ = -1;
    // Pace: hold while the flock is busy, but never longer than the patience.
    const int busy = PackMsg::popcount(static_cast<std::uint8_t>(pack_.busy & pack_.avail));
    if (busy > cfg::busy_hold && round_ - anchor_moved_ < cfg::anchor_patience) return;
    const auto* field = world_.group_field(f, fuel() > world_.field_cost() + 30000);
    vec2 next = anchor;
    if (field) {
        int here = field->get(anchor);
        for (auto d : kDirs) {
            const vec2 q{anchor.x + d.x, anchor.y + d.y};
            if (!in_bounds(q) || world_.terrain.is_set(q)) continue;
            const auto v = field->get(q);
            if (v != cambc::distance_field::unreachable && static_cast<int>(v) < here) { here = v; next = q; }
        }
    } else {
        int here = cheb(anchor, g.center);
        for (auto d : kDirs) {
            const vec2 q{anchor.x + d.x, anchor.y + d.y};
            if (!in_bounds(q) || world_.terrain.is_set(q)) continue;
            if (cheb(q, g.center) < here) { here = cheb(q, g.center); next = q; }
        }
    }
    if (next != anchor) anchor_moved_ = round_;
    pack_.ax = next.x;
    pack_.ay = next.y;
}

void Bot::count_budget(int k) {
    if (k < 0 || k >= static_cast<int>(budget_.size())) return;
    ++budget_[static_cast<std::size_t>(k)];
    if (round_ == 400 || round_ == 1000) {
        const auto& b = budget_;
        std::printf("B r%d me%d dead%d chan%d heal%d fight%d recov%d caphold%d capwalk%d bank%d gather%d clear%d task%d follow%d idle%d\n",
            round_, me_, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12]);
    }
}

void Bot::dead_turn() {
    if constexpr (cfg::debug_budget > 0) count_budget(0);
    shop();
    capture_group_ = -1;
    task_ = -1;
    banking_ = false;
    pack_.avail &= static_cast<std::uint8_t>(~(1u << me_));
    pack_.busy &= static_cast<std::uint8_t>(~(1u << me_));
    write_pack();
}

void Bot::channel_turn() {
    auto& me = cambc::self();
    if (me.is_submitting_gems()) {
        // Keep the channel unless a visible enemy close by could kill us before it completes.
        float threat = 0;
        for (const auto& u : vis_enemies_)
            if (cheb(u.pos, pos_) <= (u.kit->melee ? 3 : 5)) threat += u.kit->dps(u.str, u.focus);
        int left = 1;
        try { left = me.remaining_channel_time() + 1; } catch (const cambc::error&) {}
        if (threat * static_cast<float>(left) >= 0.8f * hp_) {
            try { me.cancel_submit_gems(); } catch (const std::exception&) {}
        }
    }
}

void Bot::play_turn() {
    round_ = static_cast<int>(cambc::round_number());
    if (!init_) init();
    if constexpr (cfg::noise_burn > 0) {
        for (int i = 0; i < cfg::noise_burn; ++i) g_noise_sink = g_noise_sink + i;
    }
    auto& me = cambc::self();
    pack_ = PackMsg::decode(read_shared());
    nav_journal_begin(round_,me_,pack_,pos_.x,pos_.y);
    i_lead_ = false;
    if (!me.is_alive()) { dead_turn(); return; }
    sense();
    if constexpr(cfg::nav_journal) { auto& j=nav_journal_frame(); if(j.active) { j.start_x=pos_.x; j.start_y=pos_.y; } }
    // Availability: alive, not channelling a recall, not waiting on spawn to heal.
    const bool available = !me.is_recalling() && !(at_spawn_ && hp_ < maxhp_);
    if (available) pack_.avail |= static_cast<std::uint8_t>(1u << me_);
    else pack_.avail &= static_cast<std::uint8_t>(~(1u << me_));
    // Allies that are dead cannot lead: clear their stale bits.
    for (std::size_t i = 0; i < allies_.size() && i < 8; ++i) {
        if (static_cast<int>(i) == me_) continue;
        bool alive = true;
        try { alive = allies_[i].is_alive(); } catch (const cambc::error&) {}
        if (!alive) pack_.avail &= static_cast<std::uint8_t>(~(1u << i));
    }
    cambc::vision::awake_camp_tiles_into(awake_);
    cambc::vision::camp_tiles_into(living_);
    awake_list_.clear();
    if (!awake_.is_empty()) {
        for (auto t : awake_.positions()) {
            const int g = world_.group_of(t);
            if (g < 0) continue;
            for (const auto& m : world_.groups[static_cast<std::size_t>(g)].members)
                if (m.tile == t) awake_list_.push_back({t, m.range, g, m.str});
        }
    }
    // Warm the objective's static field while the turn's fuel is still whole.
    if (pack_.objective >= 0 && pack_.objective < static_cast<int>(world_.groups.size())
        && fuel() > world_.field_cost() + 45000)
        world_.group_field(pack_.objective, true);
    if (acting_leader() == me_ && available) lead();
    else {
        leader_since_ = -1;
        report_sighting();
    }
    if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) {
        if constexpr (cfg::debug_budget > 0) count_budget(1);
        channel_turn();
        pack_.busy &= static_cast<std::uint8_t>(~(1u << me_));
        write_pack();
        return;
    }
    nav_journal_dispatch(pack_);
    act();
    // Busy while working a zone task or a capture: the anchor waits for busy members.
    if (task_ >= 0 || capture_group_ >= 0) pack_.busy |= static_cast<std::uint8_t>(1u << me_);
    else pack_.busy &= static_cast<std::uint8_t>(~(1u << me_));
    write_pack();
    if (fuel() > 15000) ward();
    if constexpr(cfg::nav_journal) nav_journal_flush(fuel());
}

void Bot::act() {
    auto& me = cambc::self();
    if constexpr (cfg::action_guard) {
        const auto state = me.state();
        if (!state.contains(cambc::champion_state::alive)
            || state.contains(cambc::champion_state::completed_turn)
            || state.contains(cambc::champion_state::recalling)
            || state.contains(cambc::champion_state::submitting_gems)
            || state.contains(cambc::champion_state::teleporting)
            || me.restrictions().contains(cambc::buff_restrictions::cast)) return;
    }
    if (at_spawn_) shop();
    // Recovery on spawn completes at turn end: stay put until full.
    if (at_spawn_ && hp_ < maxhp_) {
        bool threatened = false;
        for (const auto& u : vis_enemies_)
            if (cheb(u.pos, pos_) <= 2) threatened = true;
        if (!threatened) {
            if constexpr (cfg::debug_budget > 0) count_budget(2);
            return;
        }
    }
    const vec2 start = pos_;
    int branch = 0;
    if (fight_turn()) branch = 1;
    else if (recover_turn()) branch = 2;
    else if (capture_turn()) branch = 3;
    else if (bank_turn(false)) branch = 4;
    else if (converge_turn()) branch = 5;
    else if (task_turn()) branch = 6;
    else if (me.can_move()) { follow_anchor(1); branch = 7; }
    nav_journal_branch(branch);
    if constexpr (cfg::debug_budget > 0) {
        int k = 12;
        switch (branch) {
        case 1: k = 3; break;
        case 2: k = 4; break;
        case 3: k = world_.group_of(pos_) == capture_group_ ? 5 : 6; break;
        case 4: k = 7; break;
        case 5: k = pack_.mode == Mode::gather ? 8 : 9; break;
        case 6: k = 10; break;
        case 7: k = start == pos_ ? 12 : 11; break;
        default: k = 12; break;
        }
        count_budget(k);
    }
    if constexpr (cfg::debug_every > 0) {
        if (round_ % cfg::debug_every == 0 && round_ <= cfg::debug_until)
            std::printf("M r%d me%d (%d,%d)->(%d,%d) br%d obj%d mode%d sat%d cap%d\n", round_, me_, start.x, start.y, pos_.x, pos_.y,
                branch, pack_.objective, static_cast<int>(pack_.mode), task_, capture_group_);
    }
}

} // namespace wb
