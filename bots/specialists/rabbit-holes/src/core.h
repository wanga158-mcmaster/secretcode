// Included in Bot. All fields, cooldowns and observations remain actor-local.
std::array<int, 2> rh_endpoints_{-1, -1};
std::array<std::optional<Field>, 2> rh_fields_;
bool rh_initialized_ = false, rh_init_turn_ = false, rh_available_ = false;
bool rh_approaching_ = false, rh_disabled_logged_ = false, rh_probe_logged_ = false;
int rh_preferred_ = 0, rh_direction_ = -1, rh_approach_distance_ = unreachable;
unsigned rh_progress_turn_ = 0, rh_approach_start_ = 0, rh_blacklist_until_ = 0;
unsigned rh_predictions_ = 0, rh_crossings_ = 0, rh_probe_stage_ = 0, rh_readiness_records_ = 0;
bool rh_last_ready_ = true;
unsigned rh_probe_bag_start_ = 0, rh_probe_score_start_ = 0, rh_probe_start_round_ = 0;
bool rh_probe_received_ = false, rh_probe_converted_ = false, rh_probe_failed_ = false;
bitboard rh_goal_;

bool rh_actor() const { return is_actor(cambc::champion_type::vendetta); }
bool rh_probe_reverse() const { return !cambc::map::spawn_region(team::ally).is_set(tile_position(profile::probe_home_marker)); }
int rh_probe_group() const { return rh_probe_reverse() ? profile::probe_group_reverse : profile::probe_group; }
int rh_probe_waypoint() const { return rh_probe_reverse() ? profile::probe_waypoint_reverse : profile::probe_waypoint; }
std::string rh_edge_data() const { return integer("a", rh_endpoints_[0]) + "," + integer("b", rh_endpoints_[1]); }
void rh_exit(const char* reason) {
    if (rh_approaching_) telemetry_.event("observation", "edge_exit", "\"reason\":\"" + std::string(reason) + "\"," + rh_edge_data()
        + "," + integer("approachTurns", turn_ - rh_approach_start_), true);
    rh_approaching_ = false; rh_direction_ = -1;
}
void rh_initialize() {
    rh_init_turn_ = false;
    if (!rh_actor() || rh_initialized_) return;
    rh_init_turn_ = true;
    // The SDK snapshots the entire fixed-capacity buffer, including untouched
    // trailing bytes. Draft assignment of two endpoints does not shrink it.
    if (cambc::draft_memory.size() < sizeof(std::int32_t) * 2) {
        rh_initialized_ = true; rh_init_turn_ = false;
        telemetry_.event("not_exercised", "core_inactive", "\"reason\":\"no_draft_edge\""); return;
    }
    std::array<std::int32_t, 2> stored{};
    std::memcpy(stored.data(), cambc::draft_memory.data(), sizeof stored);
    rh_endpoints_ = {stored[0], stored[1]};
    if (stored[0] < 0 || stored[1] < 0 || !rabbit_geometry(tile_position(stored[0]), tile_position(stored[1]), terrain_)) {
        rh_initialized_ = true; rh_init_turn_ = false;
        telemetry_.event("not_exercised", "core_inactive", "\"reason\":\"no_legal_declared_edge\""); return;
    }
    for (int i = 0; i < 2; ++i) if (!rh_fields_[i]) {
        if (cambc::fuel_remaining() < 45000 || fields_ >= 2) return;
        Field value;
        if (!field(value, around(tile_position(rh_endpoints_[i]), 0), terrain_)) {
            // A cap hit is unknown geometry, never proof of disconnection.
            rh_initialized_ = true;
            telemetry_.event("not_exercised", "core_inactive", "\"reason\":\"endpoint_field_incomplete\"," + rh_edge_data()); return;
        }
        rh_fields_[i] = std::move(value);
    }
    const int walk = rh_fields_[0]->get(tile_position(rh_endpoints_[1]));
    const auto spawn = cambc::map::spawn_region(team::ally);
    const int first = min_distance(*rh_fields_[0], spawn), second = min_distance(*rh_fields_[1], spawn);
    rh_preferred_ = second < first ? 1 : 0;
    rh_available_ = walk == unreachable || walk >= profile::minimum_walk_separation;
    rh_initialized_ = true;
    telemetry_.event(rh_available_ ? "opportunity" : "not_exercised", rh_available_ ? "declared_edge" : "core_inactive", rh_edge_data()
        + "," + integer("walkingDistance", walk == unreachable ? -1 : walk) + ",\"walkingUnreachable\":" + (walk == unreachable ? "true" : "false")
        + "," + integer("preferredDeparture", rh_endpoints_[rh_preferred_]) + ",\"fieldsComplete\":true");
}

void core_shop() {}
void core_observe() {
    rh_initialize();
    if (!cambc::self().is_alive()) rh_exit("own_death");
    if constexpr (profile::probe_mode) if (rh_actor()) {
        if (!rh_probe_start_round_) rh_probe_start_round_ = cambc::round_number();
        if (!rh_probe_received_ && rh_probe_stage_ == 1 && cambc::self().gem_count() > rh_probe_bag_start_) {
            rh_probe_received_ = true; rh_probe_stage_ = 2;
            telemetry_.event("observation", "route_capture", integer("bagGain", cambc::self().gem_count() - rh_probe_bag_start_)
                + "," + integer("elapsedRounds", cambc::round_number() - rh_probe_start_round_), true);
        }
        if (rh_probe_received_ && !rh_probe_converted_ && cambc::self().gem_count() == 0
            && cambc::global_submitted_gems(team::ally) > rh_probe_score_start_) {
            rh_probe_converted_ = true; rh_probe_stage_ = 3;
            telemetry_.event("observation", "route_tribute", integer("scoreDelta", cambc::global_submitted_gems(team::ally) - rh_probe_score_start_)
                + "," + integer("elapsedRounds", cambc::round_number() - rh_probe_start_round_), true);
        }
        if (!cambc::self().is_alive()) {
            if (!rh_probe_failed_) telemetry_.event("mechanism_failed", "route_exit", "\"reason\":\"own_death\"", true);
            rh_probe_failed_ = true;
        }
    }
}
bool core_initializing() const { return rh_actor() && (rh_init_turn_ || !rh_initialized_); }
bool core_bank_request(int nearest) {
    if constexpr (profile::probe_mode) if (rh_actor()) return winning_bag();
    return base_bank_request(nearest);
}
bool hoard_bank() { return false; }
bool core_move(bitboard goals, const bitboard* avoid) {
    if (!rh_actor() || !rh_available_ || core_initializing() || cambc::fuel_remaining() < 45000) return false;
    auto& me = cambc::self();
    goals &= ~terrain_;
    if (goals.is_empty() || goals.is_set(me.position())) { rh_exit("goal_reached"); return false; }
    if (rh_approaching_ && goals != rh_goal_) { rh_exit("goal_changed"); return false; }
    if (cambc::round_number() < rh_blacklist_until_) return false;
    // This is the one current destination field, never a per-candidate loop.
    Field goal_field;
    if (!field(goal_field, goals, terrain_)) { rh_exit("destination_field_unknown"); return false; }
    const int walking = goal_field.get(me.position());
    int direction = -1, best_route = unreachable;
    // Preferred departure breaks equal route lengths deterministically.
    for (const int from : {rh_preferred_, 1 - rh_preferred_}) {
        const int approach = rh_fields_[from]->get(me.position());
        const int after = min_distance(*rh_fields_[1 - from], goals);
        if (approach == unreachable || after == unreachable) continue;
        const int via = approach + 1 + after;
        if ((walking == unreachable || walking - via >= profile::minimum_savings) && via < best_route) { best_route = via; direction = from; }
    }
    if (rh_predictions_ < 4) {
        ++rh_predictions_;
        telemetry_.event("prediction", "edge_route", rh_edge_data() + "," + integer("goal", tile_index(goals.positions().front()))
            + "," + integer("walkingDistance", walking == unreachable ? -1 : walking) + ",\"walkingUnreachable\":" + (walking == unreachable ? "true" : "false")
            + "," + integer("viaDistance", direction < 0 ? -1 : best_route) + "," + integer("predictedSavings", direction < 0 || walking == unreachable ? -1 : walking - best_route));
    }
    if (direction < 0) { rh_exit("no_current_saving"); return false; }
    if constexpr (!profile::core_enabled) {
        if (!rh_disabled_logged_) {
            rh_disabled_logged_ = true;
            telemetry_.event("observation", "edge_disabled", rh_edge_data() + ",\"walkingUnreachable\":" + (walking == unreachable ? "true" : "false"));
        }
        return false;
    }
    std::optional<cambc::ability> dash;
    for (const auto& ability : me.abilities()) if (ability.is_dash() && ability.can_target_ground()) { dash = ability; break; }
    // Readiness includes the engine's one successful cast allowance.
    const auto restrictions = me.restrictions();
    const bool ready = dash && dash->is_ready() && me.can_cast() && !restrictions.contains(cambc::buff_restrictions::cast)
        && !restrictions.contains(cambc::buff_restrictions::move);
    if (rh_readiness_records_ < 4 && (!rh_readiness_records_ || ready != rh_last_ready_)) {
        ++rh_readiness_records_; rh_last_ready_ = ready;
        telemetry_.event("observation", "dash_readiness", "\"ready\":" + std::string(ready ? "true" : "false")
            + "," + integer("cooldown", dash ? dash->cooldown() : -1) + ",\"onUnavailable\":\"ordinary_walking\"");
    }
    if (!ready) {
        rh_exit("dash_unavailable"); return false;
    }
    const auto departure = tile_position(rh_endpoints_[direction]), landing = tile_position(rh_endpoints_[1 - direction]);
    auto unsafe = danger(cambc::vision::camps(), cambc::vision::champions(team::enemy));
    if (avoid) unsafe |= *avoid;
    if (blocked().is_set(landing) || unsafe.is_set(landing)) { rh_exit("landing_blocked_or_unsafe"); return false; }
    const auto properties = dash->get<cambc::ability_dash>();
    if (properties.exclusive_with_movement && !me.can_move()) { rh_exit("movement_restriction"); return false; }
    const int approach = rh_fields_[direction]->get(me.position());
    if (rh_approaching_ && direction != rh_direction_) { rh_exit("direction_changed"); return false; }
    if (!rh_approaching_) {
        rh_approaching_ = true; rh_direction_ = direction; rh_goal_ = goals;
        rh_approach_start_ = rh_progress_turn_ = turn_; rh_approach_distance_ = approach;
        telemetry_.event("opportunity", "approach_endpoint", rh_edge_data() + "," + integer("departure", tile_index(departure))
            + "," + integer("goal", tile_index(goals.positions().front())) + "," + integer("walkingDistance", walking == unreachable ? -1 : walking)
            + "," + integer("viaDistance", best_route));
    } else if (approach < rh_approach_distance_) { rh_approach_distance_ = approach; rh_progress_turn_ = turn_; }
    if (turn_ - rh_progress_turn_ >= profile::approach_stall_turns) { rh_exit("approach_stall"); return false; }
    if (me.position() != departure) {
        ordinary_move(around(departure, 0), avoid);
        return true;
    }
    if (!rabbit_geometry(departure, landing, terrain_) || !dash->valid_tiles().is_set(landing)) { rh_exit("dash_mask_invalid"); return false; }
    telemetry_.event("activation", "dash_attempt", rh_edge_data() + "," + integer("departure", tile_index(departure))
        + "," + integer("landing", tile_index(landing)) + ",\"ready\":true,\"canAttackBefore\":" + (me.can_attack() ? "true" : "false")
        + "," + integer("approachTurns", turn_ - rh_approach_start_));
    const bool crossed = dash->use(landing);
    // Query live flags after the action; callers refresh vision before combat.
    const bool can_move = me.can_move(), can_attack = me.can_attack(), can_cast = me.can_cast();
    telemetry_.event("action_result", "dash_result", "\"success\":" + std::string(crossed ? "true" : "false")
        + "," + integer("position", tile_index(me.position())) + ",\"canMove\":" + (can_move ? "true" : "false")
        + ",\"canAttack\":" + (can_attack ? "true" : "false") + ",\"canCast\":" + (can_cast ? "true" : "false")
        + "," + integer("cooldown", dash->cooldown()), true);
    if (!crossed) { rh_blacklist_until_ = cambc::round_number() + profile::hidden_collision_blacklist_rounds; rh_exit("hidden_collision"); return false; }
    ++rh_crossings_;
    telemetry_.event("mechanism_success", "hole_crossed", rh_edge_data() + "," + integer("landing", tile_index(landing))
        + "," + integer("crossings", rh_crossings_), true);
    rh_exit("crossed"); return true;
}
bool core_group_allowed(int, float) { return true; }
bool core_partial_allowed() const { return true; }
bool core_fork(const std::vector<Choice>&) { return false; }
void core_selected(int) {}
bool core_retreat(const std::vector<champion>&) const { return false; }
void core_recovery() { rh_exit("recovery"); }
bool core_departing(int) const { return false; }
bool core_turn(const std::vector<camp>& camps, const std::vector<champion>&, const std::vector<champion>&) {
    if constexpr (!profile::probe_mode) return false;
    if (!rh_actor()) return true; // diagnostic allies remain safely at spawn
    if (rh_probe_failed_) return true;
    if (rh_probe_group() < 0 || rh_probe_waypoint() < 0 || profile::probe_altar < 0) {
        if (!rh_probe_logged_) { rh_probe_logged_ = true; telemetry_.event("not_exercised", "probe_unresolved"); }
        return true;
    }
    auto& me = cambc::self();
    if (cambc::round_number() > profile::max_setup_rounds && rh_probe_stage_ == 0) {
        rh_probe_failed_ = true; telemetry_.event("not_exercised", "route_exit", "\"reason\":\"setup_incomplete\"", true); return true;
    }
    const auto waypoint = tile_position(rh_probe_waypoint()), altar = tile_position(profile::probe_altar);
    if (rh_probe_stage_ == 0) {
        if (me.position() == waypoint) {
            rh_probe_stage_ = 1; rh_probe_bag_start_ = me.gem_count(); rh_probe_score_start_ = cambc::global_submitted_gems(team::ally);
            telemetry_.event("opportunity", "route_start", rh_edge_data() + "," + integer("waypoint", rh_probe_waypoint())
                + "," + group_field(rh_probe_group()) + "," + integer("altar", profile::probe_altar)
                + "," + integer("setupRounds", cambc::round_number() - rh_probe_start_round_), true);
            rh_probe_start_round_ = cambc::round_number();
        } else ordinary_move(around(waypoint, 0));
        return true;
    }
    if (rh_probe_stage_ == 1) {
        const int group = find_group(rh_probe_group());
        if (group < 0) { rh_probe_failed_ = true; telemetry_.event("not_exercised", "route_exit", "\"reason\":\"group_unresolved\"", true); return true; }
        farm_group(group, camps); return true;
    }
    if (rh_probe_stage_ == 2) {
        if (distance(me.position(), altar) <= cambc::constants::submit_gems_range) {
            for (const auto& lock : cambc::vision::altar_locks(team::enemy)) if (lock.position() == altar) {
                if (me.can_attack() && me.attack_tiles().is_set(altar)) me.attack(lock);
                return true;
            }
            me.submit_gems(altar); return true;
        }
        move_to(around(altar, cambc::constants::submit_gems_range)); return true;
    }
    if (rh_probe_stage_ == 3) {
        if (me.position() == waypoint) {
            rh_probe_stage_ = 4;
            const bool crossed_as_expected = !profile::core_enabled || rh_crossings_ > 0;
            telemetry_.event(crossed_as_expected ? "mechanism_success" : "mechanism_failed", "route_complete", integer("crossings", rh_crossings_)
                + "," + integer("elapsedRounds", cambc::round_number() - rh_probe_start_round_) + ",\"converted\":true,\"crossingRequirementMet\":"
                + (crossed_as_expected ? "true" : "false"), true);
        } else move_to(around(waypoint, 0));
        return true;
    }
    return true;
}
