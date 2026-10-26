// Actor-local Gremlins state. No live protocol or remote ally observations.
struct GremlinTarget {
    champion victim;
    unsigned id = 0, bag = 0;
    int altar = -1, route = unreachable, remaining = 0;
    bitboard placement;
};
std::optional<GremlinTarget> disruption_;
unsigned disruption_first_ = 0, disruption_until_ = 0, disruption_stalls_ = 0;
unsigned disruption_turns_ = 0, disruption_foregone_ = 0;
int disruption_best_ = unreachable;
bool disruption_observation_pending_ = false, disruption_inactive_logged_ = false;
unsigned disruption_observe_id_ = 0, disruption_observe_bag_ = 0;
unsigned disruption_observe_round_ = 0;
int disruption_observe_altar_ = -1, disruption_observe_remaining_ = 0;
unsigned probe_phase_ = 0;
bool probe_released_ = false, probe_done_ = false, probe_timeout_ = false;
int probe_gold_ = -1, probe_staging_ = -1;

std::string gremlin_fields(const GremlinTarget& t) const {
    return integer("victimId", t.id) + "," + integer("victimBag", t.bag) + "," + integer("altarTile", t.altar)
        + "," + integer("remainingTicks", t.remaining) + "," + integer("routeDistance", t.route);
}
void gremlin_abort(const char* reason) {
    if (!disruption_) return;
    telemetry_.event("observation", "gremlins_abort", gremlin_fields(*disruption_) + ",\"reason\":\"" + reason
        + "\"," + integer("pursuitTurns", turn_ - disruption_first_ + 1) + "," + integer("foregoneInRangeFarmBasics", disruption_foregone_), true);
    disruption_.reset(); disruption_until_ = gremlins::cooldown_until(turn_, profile::cooldown_turns);
    if constexpr (profile::probe_mode) probe_done_ = true;
}
void core_shop() {
    if (!is_actor(cambc::champion_type::varg)) return;
    auto& me = cambc::self(); const auto lock = cambc::consumable_type::altar_lock;
    if (!me.consumables()[lock].count() && me.can_buy(lock)) {
        const auto cost = lock.cost(); me.buy(lock);
        telemetry_.event("action_result", "gremlins_lock_purchase", integer("cost", cost) + ",\"inventory\":1");
    }
}
void core_observe() {
    if (!is_actor(cambc::champion_type::varg)) return;
    auto& me = cambc::self();
    if (disruption_ && (!me.is_alive() || me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()))
        gremlin_abort("higher_priority_channel_or_death");
    if (disruption_) {
        // Revalidate even if a higher-priority ordinary defense later consumes
        // the turn. Losing vision never preserves a remembered active channel.
        if (!disruption_->victim.is_visible()) gremlin_abort("victim_not_personally_visible");
        else {
            const auto current = gremlin_candidate(disruption_->victim, cambc::vision::visible_tiles());
            if (!current || current->altar != disruption_->altar) gremlin_abort("victim_no_longer_qualifies");
        }
        if (disruption_ && (!me.consumables()[cambc::consumable_type::altar_lock].count()
            || me.restrictions().contains(cambc::buff_restrictions::consumable))) gremlin_abort("no_usable_lock");
        if (disruption_ && gremlins::expired(disruption_first_, turn_, disruption_stalls_, profile::pursuit_turns, profile::stall_turns)) gremlin_abort("pursuit_or_progress_limit");
        if (disruption_ && gremlin_hazards(cambc::vision::camps(), cambc::vision::champions(team::enemy)).is_set(me.position()))
            gremlin_abort("current_position_became_unsafe");
    }
    if (disruption_) for (const auto& g : groups_) if (g.spawns.is_set(me.position()) && reward(g)) {
        gremlin_abort("capture_duty"); break;
    }
    if constexpr (profile::probe_mode) {
        if (!probe_released_ && !probe_timeout_ && cambc::round_number() > profile::setup_limit) {
            probe_timeout_ = probe_done_ = true;
            telemetry_.event("not_exercised", "gremlins_probe", "\"reason\":\"legal_setup_cap_without_visible_channel\"", true);
        }
    }
    if (!disruption_observation_pending_ || !me.is_alive()) return;
    for (const auto& e : cambc::vision::champions(team::enemy)) {
        const auto id = static_cast<unsigned>(static_cast<cambc::handle_champion_t>(e));
        if (id != disruption_observe_id_) continue;
        const auto state = e.state(); const auto bag = e.gem_count();
        const bool active = state.contains(cambc::champion_state::submitting_gems);
        const bool locked = cambc::vision::altar_lock_tiles(team::ally).is_set(tile_position(disruption_observe_altar_));
        telemetry_.event("observation", "gremlins_next_visible_victim", integer("victimId", id) + "," + integer("victimBag", bag)
            + ",\"submitting\":" + (active ? "true" : "false") + ",\"friendlyLockVisible\":" + (locked ? "true" : "false")
            + "," + integer("selectedRemainingTicks", disruption_observe_remaining_)
            + "," + integer("observationDelayRounds", cambc::round_number() - disruption_observe_round_), true);
        if (!active && locked && bag >= disruption_observe_bag_)
            telemetry_.event("observation", "gremlins_visible_channel_stopped", "\"victimOwnLogRequired\":true," + integer("victimId", id), true);
        disruption_observation_pending_ = false; break;
    }
}
bool core_bank_request(int nearest) {
    const bool request = base_bank_request(nearest);
    if (request) gremlin_abort("banking_duty");
    return request;
}
bool hoard_bank() { return false; }
bool core_move(bitboard, const bitboard*) { return false; }
bool core_group_allowed(int, float) { return true; }
bool core_partial_allowed() const { return true; }
bool core_fork(const std::vector<Choice>&) { return false; }
void core_selected(int) {}
bool core_retreat(const std::vector<champion>&) const { return false; }
void core_recovery() { gremlin_abort("recovery_duty"); }
bool core_departing(int) const { return false; }

bitboard gremlin_hazards(const std::vector<camp>& camps, const std::vector<champion>& enemies) const {
    bitboard result;
    // Cache SDK values outside tile loops. All channeling enemies are unable to
    // act; every other visible enemy remains a conservative threat.
    for (const auto& m : camps) {
        const auto alive = m.is_alive(), awake = m.is_awake();
        if (!alive || !awake) continue;
        const auto strength = m.strength(); if (strength <= 0) continue;
        const auto position = m.position(); const auto range = m.attack_range();
        result |= around(position, range + 1);
    }
    for (const auto& e : enemies) {
        const auto state = e.state();
        if (state.contains(cambc::champion_state::submitting_gems)) continue;
        const auto position = e.position(); const auto range = e.attack_range();
        result |= around(position, range + 1);
    }
    return result;
}
std::optional<GremlinTarget> gremlin_candidate(const champion& e, const bitboard& visible) const {
    const auto state = e.state(); const auto bag = e.gem_count(); const auto position = e.position();
    unsigned count = 0; int altar = -1;
    for (const auto p : altars_) if (distance(position, p) <= cambc::constants::submit_gems_range) { ++count; altar = tile_index(p); }
    if (!gremlins::qualifies(true, state.contains(cambc::champion_state::alive),
        state.contains(cambc::champion_state::submitting_gems), bag, count, profile::minimum_bag)) return std::nullopt;
    if (!visible.is_set(tile_position(altar)) || cambc::vision::has_altar_lock(tile_position(altar))) return std::nullopt;
    GremlinTarget candidate{e, static_cast<unsigned>(static_cast<cambc::handle_champion_t>(e)), bag, altar};
    candidate.remaining = e.remaining_channel_time();
    return candidate;
}
bool gremlin_disrupt(const std::vector<camp>& camps, const std::vector<champion>& enemies) {
    auto& me = cambc::self(); const auto origin = me.position();
    // A failed ordinary banking route does not remove the banking duty.
    if (recovering_ || base_bank_request(nearest_altar())
        || (!cambc::map::spawn_region(team::ally).is_set(origin) && me.gold_count() >= 300)) {
        gremlin_abort("higher_priority_bank_recovery_or_spend"); return false;
    }
    if (!gremlins::cooldown_complete(disruption_until_, turn_) && !disruption_) return false;
    const auto lock = cambc::consumable_type::altar_lock;
    if (!me.consumables()[lock].count() || me.restrictions().contains(cambc::buff_restrictions::consumable)) {
        gremlin_abort("no_usable_lock"); return false;
    }
    if (disruption_ && gremlins::expired(disruption_first_, turn_, disruption_stalls_, profile::pursuit_turns, profile::stall_turns)) {
        gremlin_abort("pursuit_or_progress_limit"); return false;
    }
    if (cambc::fuel_remaining() < 45000) return disruption_.has_value();
    const auto visible = cambc::vision::visible_tiles();
    std::vector<GremlinTarget> candidates;
    for (const auto& enemy : enemies) {
        const auto id = static_cast<unsigned>(static_cast<cambc::handle_champion_t>(enemy));
        if (disruption_ && id != disruption_->id) continue;
        auto candidate = gremlin_candidate(enemy, visible);
        if (candidate && (!disruption_ || candidate->altar == disruption_->altar)) candidates.push_back(*candidate);
    }
    if (candidates.empty()) { gremlin_abort("victim_not_visible_active_at_same_unique_altar"); return false; }
    // One common conservative origin field, never one field per target.
    const auto hazard = gremlin_hazards(camps, enemies);
    auto walls = terrain_ | hazard;
    for (const auto& g : groups_) walls |= g.spawns;
    walls.set(origin, false);
    const auto occupancy = cambc::vision::champion_tiles();
    std::optional<GremlinTarget> selected;
    if (hazard.is_set(origin)) { gremlin_abort("no_safe_reachable_placement"); return false; }
    for (auto& c : candidates) {
        auto goals = around(tile_position(c.altar), profile::placement_range) & ~walls & ~occupancy;
        if (distance(origin, tile_position(c.altar)) <= profile::placement_range) goals.set(origin);
        c.placement = goals;
        if (!goals.is_set(origin)) continue;
        c.route = 0;
        if (!selected || gremlins::precedes(c.bag, 0, c.id, c.altar, selected->bag, 0, selected->id, selected->altar)) selected = c;
    }
    const bool have_zero = selected.has_value();
    const unsigned zero_bag = selected ? selected->bag : 0;
    bool need_route = false;
    for (const auto& c : candidates) if (c.route != 0 && !c.placement.is_empty()
        && gremlins::needs_route_field(have_zero, zero_bag, c.bag)) need_route = true;
    if (need_route) {
        Field route;
        if (!field(route, around(origin, 0), walls)) { gremlin_abort("safe_static_route_unresolved"); return false; }
        for (auto& c : candidates) {
            if (c.route == 0 || c.placement.is_empty()
                || !gremlins::needs_route_field(have_zero, zero_bag, c.bag)) continue;
            c.route = min_distance(route, c.placement);
            if (c.route == unreachable) continue;
            if (!selected || gremlins::precedes(c.bag, c.route, c.id, c.altar, selected->bag, selected->route, selected->id, selected->altar)) selected = c;
        }
    }
    if (!selected) { gremlin_abort("no_safe_reachable_placement"); return false; }
    if (!disruption_) {
        disruption_first_ = turn_; disruption_stalls_ = disruption_turns_ = disruption_foregone_ = 0; disruption_best_ = selected->route;
        probe_released_ = true;
        telemetry_.event("opportunity", "gremlins_visible_active_tribute", gremlin_fields(*selected));
    } else {
        if (selected->route < disruption_best_) { disruption_best_ = selected->route; disruption_stalls_ = 0; }
        else ++disruption_stalls_;
        if (disruption_stalls_ >= profile::stall_turns) { gremlin_abort("three_turns_without_distance_progress"); return false; }
    }
    disruption_ = *selected; disruption_turns_ = turn_ - disruption_first_ + 1;
    // This is a narrow observed opportunity count, not a claim that every
    // pursuit turn displaced productive farming elsewhere on the map.
    if (me.can_attack()) {
        const auto attacks = me.attack_tiles();
        for (const auto& c : camps) if (c.is_alive() && attacks.is_set(c.position())) { ++disruption_foregone_; break; }
    }
    if (distance(origin, tile_position(selected->altar)) > profile::placement_range) {
        auto safe_avoid = walls; safe_avoid.set(origin, false);
        ordinary_move(selected->placement, &safe_avoid);
        // Fresh personal observation after movement; no remembered channel.
        if (!selected->victim.is_visible()) { gremlin_abort("lost_personal_sight_after_move"); return true; }
        const auto refreshed = gremlin_candidate(selected->victim, cambc::vision::visible_tiles());
        if (!refreshed || refreshed->altar != selected->altar) { gremlin_abort("channel_changed_after_move"); return true; }
        disruption_->remaining = refreshed->remaining; disruption_->bag = refreshed->bag;
        const auto refreshed_hazards = gremlin_hazards(cambc::vision::camps(), cambc::vision::champions(team::enemy));
        if (refreshed_hazards.is_set(me.position())) { gremlin_abort("newly_visible_route_hazard"); return true; }
    }
    if (distance(me.position(), tile_position(selected->altar)) > profile::placement_range) return true;
    if (!me.consumables()[lock].count() || me.restrictions().contains(cambc::buff_restrictions::consumable)
        || cambc::vision::has_altar_lock(tile_position(selected->altar))) { gremlin_abort("placement_restriction_changed"); return true; }
    telemetry_.event("activation", "gremlins_placement_attempt", gremlin_fields(*disruption_) + "," + integer("pursuitTurns", disruption_turns_));
    if constexpr (profile::core_enabled) {
        const bool placed = me.consumables()[lock].use(tile_position(selected->altar));
        telemetry_.event("action_result", "gremlins_lock_placement", "\"placed\":" + std::string(placed ? "true" : "false")
            + "," + gremlin_fields(*disruption_) + "," + integer("pursuitTurns", disruption_turns_)
            + "," + integer("foregoneInRangeFarmBasics", disruption_foregone_) + "," + integer("inventoryAfter", me.consumables()[lock].count()), true);
        if (!placed) { gremlin_abort("lock_placement_returned_false"); return true; }
        disruption_observation_pending_ = true; disruption_observe_id_ = disruption_->id; disruption_observe_bag_ = disruption_->bag;
        disruption_observe_altar_ = disruption_->altar; disruption_observe_remaining_ = disruption_->remaining;
        disruption_observe_round_ = cambc::round_number();
    } else {
        telemetry_.event("mechanism_success", "gremlins_shadow_left_channel_active", gremlin_fields(*disruption_)
            + "," + integer("pursuitTurns", disruption_turns_) + ",\"lockUsed\":false", true);
    }
    disruption_.reset(); disruption_until_ = gremlins::cooldown_until(turn_, profile::cooldown_turns);
    if constexpr (profile::probe_mode) probe_done_ = true;
    return true;
}
bool core_turn(const std::vector<camp>& camps, const std::vector<champion>& enemies, const std::vector<champion>&) {
    if (!is_actor(cambc::champion_type::varg)) return false;
    if constexpr (profile::probe_mode) {
        if (probe_gold_ < 0) {
            const bool a = cambc::map::spawn_region(team::ally).is_set(tile_position(44));
            probe_gold_ = find_group(a ? 299 : 1365); probe_staging_ = a ? 767 : 931;
            if (probe_gold_ < 0) throw std::runtime_error("Gremlins fixed gold fixture missing");
        }
        if (probe_done_) return true;
        auto& me = cambc::self();
        if (probe_phase_ == 0) {
            const auto& funding = groups_[probe_gold_];
            const bool captured = seen(funding) && std::holds_alternative<cambc::map::camp_group_respawning>(funding.group.state());
            if (me.gold_count() < cambc::consumable_type::altar_lock.cost() || !captured) { farm_group(probe_gold_, camps); return true; }
            probe_phase_ = 1; telemetry_.event("observation", "gremlins_legal_funding", integer("gold", me.gold_count()) + "," + group_field(groups_[probe_gold_].id));
        }
        if (probe_phase_ == 1) {
            if (!me.consumables()[cambc::consumable_type::altar_lock].count()) {
                if (!cambc::map::spawn_region(team::ally).is_set(me.position())) me.recall();
                return true;
            }
            probe_phase_ = 2;
        }
        if (probe_phase_ == 2) {
            if (tile_index(me.position()) != probe_staging_) { ordinary_move(around(tile_position(probe_staging_), 0)); return true; }
            probe_phase_ = 3; telemetry_.event("observation", "gremlins_staging", integer("tile", probe_staging_) + ",\"lockInventory\":1");
        }
        gremlin_disrupt(camps, enemies); return true;
    }
    if (!disruption_inactive_logged_) {
        disruption_inactive_logged_ = true;
        telemetry_.event("observation", "core_inactive", "\"reason\":\"await_visible_active_tribute_and_carried_lock\"");
    }
    return gremlin_disrupt(camps, enemies);
}
