// Included inside Bot: all state is actor-local, with only the static draft plan
// shared. No live shared memory and no inference from an unseen ally.
enum class Mission { none, clear, hold, exit, second, done, aborted };
OpeningPlan opening_{};
Mission mission_ = Mission::none;
bool opening_loaded_ = false, held_ = false, reward_seen_ = false;
bool departed_ = false, productive_ = false, second_held_ = false;
bool trigger_observed_ = false;
int opening_index_ = -1, second_index_ = -1;
unsigned hold_bag_ = 0, hold_gold_ = 0, last_progress_ = 1;
unsigned second_bag_ = 0, second_gold_ = 0;
int best_leg_distance_ = unreachable;
float least_group_health_ = std::numeric_limits<float>::max();

bool mission_actor() const { return is_actor(cambc::champion_type::tavi) || is_actor(cambc::champion_type::varg); }
bool active_mission() const { return mission_ == Mission::clear || mission_ == Mission::hold || mission_ == Mission::exit || mission_ == Mission::second; }
const char* role_name() const { return is_actor(cambc::champion_type::tavi) ? "holder" : "clearer"; }
std::string mission_fields() const {
    return "\"role\":\"" + std::string(role_name()) + "\",\"group\":\"g_" + std::to_string(opening_.group)
        + "\",\"secondGroup\":\"g_" + std::to_string(opening_.second) + "\"";
}
void phase(Mission next) {
    if (mission_ == next) return;
    mission_ = next; last_progress_ = turn_; best_leg_distance_ = unreachable;
    least_group_health_ = std::numeric_limits<float>::max();
}
void abort_mission(const char* reason) {
    if (!active_mission()) return;
    telemetry_.event("observation", "choir_abort", mission_fields() + ",\"reason\":\"" + reason
        + "\",\"tile\":" + std::to_string(cambc::self().is_alive() ? tile_index(cambc::self().position()) : -1)
        + ",\"hadHeld\":" + (held_ ? "true" : "false"), true);
    if constexpr (profile::probe_mode) telemetry_.event("not_exercised", "choir_opening_aborted",
        mission_fields() + ",\"reason\":\"" + reason + "\",\"triggerObserved\":" + (trigger_observed_ ? "true" : "false"), true);
    phase(Mission::aborted);
}
bool personally_see_holder() const {
    if (opening_index_ < 0) return false;
    for (const auto& ally : cambc::vision::champions(team::ally))
        if (ally.champion_name() == cambc::champion_type::tavi && ally.is_visible()
            && ally.is_alive() && groups_[opening_index_].spawns.is_set(ally.position())) return true;
    return false;
}
void load_opening() {
    opening_loaded_ = true;
    // The SDK snapshot retains its full capacity even when a short prefix was
    // submitted. Validate at least sizeof(plan), never equality with1024.
    if (cambc::draft_memory.size() >= sizeof opening_)
        std::memcpy(&opening_, cambc::draft_memory.data(), sizeof opening_);
    if (opening_.magic != 0x43485231 || opening_.status != 1
        || (opening_index_ = find_group(opening_.group)) < 0
        || (second_index_ = find_group(opening_.second)) < 0) {
        telemetry_.event("not_exercised", opening_.status == 3 ? "choir_selector_incomplete" : "choir_no_pair",
            integer("selectorStatus", opening_.status));
        return;
    }
    if (mission_actor()) {
        phase(Mission::clear);
        telemetry_.event("prediction", "choir_opening_plan", mission_fields()
            + "," + integer("holderApproach", opening_.holder_approach)
            + "," + integer("clearerApproach", opening_.clearer_approach)
            + "," + integer("exitTile", opening_.exit));
    }
}
void core_shop() {}
void core_observe() {
    if (!opening_loaded_) load_opening();
    if (opening_index_ < 0 || !mission_actor()) return;
    auto& me = cambc::self();
    if (!me.is_alive()) { abort_mission("own_death"); return; }
    if (active_mission() && choir::deadline(turn_, profile::opening_turn_limit)) abort_mission("owner_turn_48");
    auto& g = groups_[opening_index_];
    if (seen(g)) {
        const auto state = g.group.state();
        const bool available = std::holds_alternative<cambc::map::camp_group_reward_available>(state);
        if (departed_ && std::holds_alternative<cambc::map::camp_group_alive>(state)) {
            departed_ = false;
            telemetry_.event("observation", "choir_new_generation", mission_fields());
        }
        if (available && active_mission()) {
            if (!reward_seen_) {
                reward_seen_ = true;
                telemetry_.event("observation", "choir_reward_available", mission_fields());
            }
            if (g.spawns.is_set(me.position()) && !held_) {
                held_ = true; hold_bag_ = me.gem_count(); hold_gold_ = me.gold_count();
                phase(Mission::hold);
                telemetry_.event("action_result", "choir_holder_enter", mission_fields()
                    + "," + integer("tile", tile_index(me.position())) + "," + integer("bag", hold_bag_));
            }
            if (!trigger_observed_ && ((is_actor(cambc::champion_type::tavi) && g.spawns.is_set(me.position()))
                || (is_actor(cambc::champion_type::varg) && personally_see_holder()))) {
                trigger_observed_ = true;
                telemetry_.event("opportunity", "choir_release_ready", mission_fields()
                    + ",\"rewardSeen\":true,\"holderSeen\":true");
            }
        }
        if (held_ && std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
            const bool full = choir::complete_reward(hold_bag_, hold_gold_, me.gem_count(), me.gold_count(), g.group.gem_value(), g.group.gold_value());
            const auto fields = mission_fields() + "," + integer("gemsDelta", static_cast<long long>(me.gem_count())-hold_bag_)
                + "," + integer("goldDelta", static_cast<long long>(me.gold_count())-hold_gold_)
                + ",\"fullUnsplitReward\":" + (full ? "true" : "false")
                + ",\"aborted\":" + (mission_ == Mission::aborted ? "true" : "false");
            if (is_actor(cambc::champion_type::tavi)) {
                telemetry_.event(profile::core_enabled && mission_ != Mission::aborted ? (full ? "mechanism_success" : "mechanism_failed") : "observation",
                    full ? "choir_single_holder_reward" : "choir_split_reward", fields, true);
                phase(Mission::done);
            } else {
                telemetry_.event("observation", "choir_control_reward", fields, true);
                // The two-holder control resumes its ordinary economy after
                // reward; its newly shared bag keeps normal banking priority.
                if (mission_ != Mission::aborted) phase(Mission::done);
            }
            held_ = false;
        }
    }
    if (mission_ == Mission::second && second_index_ >= 0 && seen(groups_[second_index_])) {
        auto& second = groups_[second_index_];
        const auto state = second.group.state();
        if (std::holds_alternative<cambc::map::camp_group_reward_available>(state) && second.spawns.is_set(me.position()) && !second_held_) {
            second_held_ = true; second_bag_ = me.gem_count(); second_gold_ = me.gold_count();
            last_progress_ = turn_;
        }
        if (second_held_ && std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
            telemetry_.event("observation", "choir_second_reward", mission_fields()
                + "," + integer("gemsDelta", static_cast<long long>(me.gem_count())-second_bag_)
                + "," + integer("goldDelta", static_cast<long long>(me.gold_count())-second_gold_), true);
            phase(Mission::done);
        }
    }
    if (!active_mission()) return;
    const auto enemies = cambc::vision::champions(team::enemy);
    for (const auto& enemy : enemies) for (const auto spawn : g.spawns.positions())
        if (distance(enemy.position(), spawn) <= 8) { abort_mission("enemy_within_eight"); return; }
    const int leg = mission_ == Mission::second ? second_index_ : opening_index_;
    const auto destination = mission_ == Mission::exit ? tile_position(opening_.exit) : groups_[leg].anchor;
    const int d = distance(me.position(), destination);
    if (d < best_leg_distance_) { best_leg_distance_ = d; last_progress_ = turn_; }
    if ((groups_[leg].spawns & ~cambc::vision::visible_tiles()).is_empty()) {
        float health = 0;
        for (const auto& m : cambc::vision::camps()) if (m.is_visible() && groups_[leg].spawns.is_set(m.spawn_position()) && m.is_alive()) health += m.health();
        if (health < least_group_health_ - epsilon) { least_group_health_ = health; last_progress_ = turn_; }
    }
    if (choir::stalled(turn_, last_progress_, profile::progress_turn_limit)) abort_mission("leg_stalled_eight");
}
bool core_bank_request(int nearest) { return !active_mission() && base_bank_request(nearest); }
bool core_move(bitboard, const bitboard*) { return false; }
bitboard core_obstacles() const {
    const bool reserve = departed_ || (!mission_actor() && turn_ <= profile::opening_turn_limit);
    return reserve && opening_index_ >= 0 ? groups_[opening_index_].spawns : bitboard{};
}
bool core_group_allowed(int index, float) {
    if (index != opening_index_) return true;
    if (departed_) return false;
    // Other actors have no completion message; conservatively reserve G0 for the
    // entire declared opening window, then use their independent economy.
    return mission_actor() || turn_ > profile::opening_turn_limit;
}
bool core_partial_allowed() const { return true; }
bool core_fork(const std::vector<Choice>&) { return false; }
void core_selected(int) {}
bool core_retreat(const std::vector<champion>&) const { return false; }
void core_recovery() { abort_mission("own_recovery"); }
bool core_departing(int index) const {
    if (index != opening_index_ || !active_mission()) return false;
    if (is_actor(cambc::champion_type::varg)) {
        if (mission_ == Mission::exit || mission_ == Mission::second) return true;
        return choir::release(profile::core_enabled, reward(groups_[index]), personally_see_holder());
    }
    return false;
}
bool mission_move(bitboard goals, const bitboard* avoid = nullptr) {
    auto& me = cambc::self();
    if (!me.can_move() || goals.is_set(me.position())) return false;
    auto walls = blocked(); if (avoid) walls |= *avoid;
    walls.set(me.position(), false); goals &= ~walls;
    if (goals.is_empty()) return false;
    Field from_self;
    if (!field(from_self, around(me.position(), 0), walls)) return false;
    int best = -1, cost = unreachable;
    for (const auto p : goals.positions()) {
        const int n = tile_index(p), d = from_self.get(p);
        if (d < cost || (d == cost && (best < 0 || n < best))) { best = n; cost = d; }
    }
    if (best < 0 || cost == unreachable) return false;
    return ordinary_move(around(tile_position(best), 0), avoid);
}
void enter_lowest_safe_spawn(const Group& g) {
    const auto avoid = danger(cambc::vision::camps(), cambc::vision::champions(team::enemy));
    auto walls = blocked() | avoid;
    walls.set(cambc::self().position(),false);
    const auto available = g.spawns & ~walls;
    if (available.is_empty()) return;
    Field from_self;
    if (!field(from_self,around(cambc::self().position(),0),walls)) return;
    int lowest = std::numeric_limits<int>::max();
    for (const auto p : available.positions()) if (from_self.get(p) != unreachable) lowest = std::min(lowest, tile_index(p));
    if (lowest != std::numeric_limits<int>::max()) ordinary_move(around(tile_position(lowest),0),&avoid);
}
bool safe_splash(const camp& target, const Group& g) const {
    if (!is_actor(cambc::champion_type::varg)) return true;
    for (const auto& other : groups_) if (other.id != g.id)
        for (const auto p : other.spawns.positions()) if (distance(p,target.position()) <= 1) return false;
    return true;
}
bool mission_farm(int index, bool remain_outside) {
    auto& me = cambc::self(); auto& g = groups_[index]; target_ = index;
    if (reward(g)) {
        if (!g.spawns.is_set(me.position())) enter_lowest_safe_spawn(g);
        return true;
    }
    std::optional<camp> target;
    float best = std::numeric_limits<float>::max();
    for (const auto& m : cambc::vision::camps()) if (m.is_visible() && g.spawns.is_set(m.spawn_position()) && m.is_alive()) {
        if (!safe_splash(m,g)) continue;
        const float score = m.health() + distance(me.position(), m.position()) * 100;
        if (score < best - epsilon || (std::abs(score-best) <= epsilon && (!target || tile_index(m.spawn_position()) < tile_index(target->spawn_position())))) { target = m; best = score; }
    }
    auto attack = [&](const camp& m, bool walked) {
        const auto before = m.health();
        const bool acted = fight(m, walked, g.incoming > 0);
        if (acted && mission_ == Mission::second && !productive_ && m.is_visible() && (!m.is_alive() || m.health() < before)) {
            productive_ = true;
            telemetry_.event("action_result", "choir_second_productive", mission_fields()
                + "," + integer("tile", tile_index(me.position())) + "," + integer("targetTile", tile_index(m.spawn_position()))
                + "," + number("healthBefore", before) + "," + number("healthAfter",m.is_alive() ? m.health() : 0), true);
        }
        return acted;
    };
    if (target && attack(*target, false)) return true;
    bitboard goals;
    if (target) goals = around(target->position(), me.attack_range());
    else if (mission_ == Mission::second) goals = boundary(g, me.attack_range(), terrain_);
    else goals = around(tile_position(is_actor(cambc::champion_type::tavi) ? opening_.holder_approach : opening_.clearer_approach),0);
    if (remain_outside) goals &= ~g.spawns;
    const bool walked = mission_move(goals, remain_outside ? &g.spawns : nullptr);
    if (walked) for (const auto& m : cambc::vision::camps())
        if (m.is_visible() && g.spawns.is_set(m.spawn_position()) && m.is_alive() && safe_splash(m,g) && me.attack_tiles().is_set(m.position())) { attack(m, true); break; }
    return true;
}
bool core_turn(const std::vector<camp>& camps, const std::vector<champion>& enemies, const std::vector<champion>&) {
    if constexpr (profile::probe_mode) {
        if (!mission_actor()) return true; // Other actors stay at own spawn.
        if (mission_ == Mission::done || mission_ == Mission::aborted || mission_ == Mission::none) return true;
    }
    if (!active_mission()) return false;
    auto& me = cambc::self(); auto& g = groups_[opening_index_];
    if (mission_ == Mission::second) return mission_farm(second_index_, false);
    if (mission_ == Mission::exit) {
        const auto exit = tile_position(opening_.exit);
        if (danger(camps,enemies).is_set(exit)) { abort_mission("declared_exit_unsafe"); return false; }
        if (me.position() != exit) { mission_move(around(exit,0), &g.spawns); return true; }
        telemetry_.event("action_result", "choir_first_outside", mission_fields() + "," + integer("tile", tile_index(me.position())), true);
        phase(Mission::second);
        return mission_farm(second_index_, false);
    }
    if (!reward(g)) return mission_farm(opening_index_, true);
    if (is_actor(cambc::champion_type::tavi)) {
        if (!g.spawns.is_set(me.position())) enter_lowest_safe_spawn(g);
        return true;
    }
    if (profile::core_enabled) {
        if (!personally_see_holder()) return true;
        if (danger(camps,enemies).is_set(tile_position(opening_.exit))) { abort_mission("declared_exit_unsafe"); return false; }
        departed_ = true;
        telemetry_.event("activation", "choir_clearer_departs", mission_fields()
            + ",\"rewardSeen\":true,\"holderSeen\":true," + integer("tile", tile_index(me.position())), true);
        phase(Mission::exit);
        // Already outside living spawn tiles; the declared safe exit tile is
        // visited before G1, without ever entering the reward region.
        if (me.position() == tile_position(opening_.exit)) {
            telemetry_.event("action_result", "choir_first_outside", mission_fields() + "," + integer("tile", opening_.exit), true);
            phase(Mission::second);
            return mission_farm(second_index_, false);
        }
        mission_move(around(tile_position(opening_.exit),0), &g.spawns); return true;
    }
    // Disabled arm uses the same clear but deliberately attends the reward with
    // Tavi. A singleton cannot hold two bodies and remains a geometry limitation.
    if (!g.spawns.is_set(me.position())) enter_lowest_safe_spawn(g);
    return true;
}
