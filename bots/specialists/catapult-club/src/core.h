// Actor-local Catapult Club. Included inside Bot after the inherited helpers.
enum class SiegeState { farm, approach, siege, capture, bank };
SiegeState siege_state_ = SiegeState::farm;
struct SiegeGeometry {
    int group = -1;
    std::optional<Field> capture;
    vec2 capture_tile{};
    int altar_distance = unreachable;
    bool attempted = false;
};
std::vector<SiegeGeometry> siege_geometry_;
std::optional<Field> siege_altar_field_;
bool siege_geometry_initialized_ = false, siege_altar_attempted_ = false;
int siege_group_ = -1;
vec2 siege_firing_{};
unsigned siege_stalls_ = 0, siege_unseen_ = 0, siege_casts_ = 0, siege_safe_casts_ = 0;
int siege_last_distance_ = unreachable;
unsigned siege_capture_bag_ = 0, siege_bank_score_ = 0;
bool siege_holding_ = false, siege_pending_cast_ = false, siege_witnessed_ = false;
bool siege_damage_observed_ = false;
unsigned siege_inactive_reasons_ = 0;
float siege_cast_health_ = 0;
vec2 siege_cast_position_{};
bool siege_probe_resolved_ = false, siege_probe_done_ = false, siege_probe_waypoint_done_ = false;
bool siege_probe_unresolved_logged_ = false;
bool siege_probe_triggered_ = false, siege_probe_timeout_logged_ = false;

bool siege_actor() const { return is_actor(cambc::champion_type::tavi); }
bool siege_active() const { return siege_state_ != SiegeState::farm; }
SiegeGeometry* siege_geometry(int index) {
    for (auto& value : siege_geometry_) if (value.group == index) return &value;
    return nullptr;
}
void siege_exit(const char* reason, bool loss = false) {
    if (!siege_active()) return;
    if (siege_group_ >= 0) {
        telemetry_.event("observation", "siege_exit", group_field(groups_[siege_group_].id)
            + ",\"reason\":\"" + reason + "\"," + integer("casts", siege_casts_)
            + "," + integer("bag", cambc::self().gem_count()) + ",\"loss\":" + (loss ? "true" : "false"), true);
        groups_[siege_group_].unavailable_until = cambc::round_number() + profile::siege_blacklist_rounds;
    }
    if (profile::probe_mode && (loss || siege_state_ == SiegeState::bank)) siege_probe_done_ = true;
    siege_state_ = SiegeState::farm; siege_group_ = -1; siege_pending_cast_ = false;
    siege_holding_ = false; siege_stalls_ = siege_unseen_ = 0;
}
void core_shop() {}
void siege_inactive(unsigned bit, const char* reason) {
    if (!(siege_inactive_reasons_ & bit)) {
        siege_inactive_reasons_ |= bit;
        telemetry_.event("observation", "core_inactive", "\"reason\":\"" + std::string(reason) + "\"");
    }
}
void core_observe() {
    if (!siege_actor()) return;
    auto& me = cambc::self();
    if (profile::probe_mode && !siege_probe_resolved_) {
        siege_probe_resolved_ = find_group(profile::probe_group) >= 0 && profile::probe_waypoint >= 0
            && profile::probe_waypoint < cambc::map::width() * cambc::map::height()
            && !terrain_.is_set(tile_position(profile::probe_waypoint));
        if (!siege_probe_resolved_ && !siege_probe_unresolved_logged_) {
            telemetry_.event("not_exercised", "probe_unresolved"); siege_probe_unresolved_logged_ = true;
        }
    }
    if (profile::probe_mode && !siege_probe_triggered_ && cambc::round_number() >= 240 && !siege_probe_timeout_logged_) {
        telemetry_.event("not_exercised", "siege_setup_incomplete", "\"maxSetupRounds\":240", true);
        siege_probe_timeout_logged_ = true;
    }
    if (!siege_active()) return;
    if (!me.is_alive()) { siege_exit("death", true); return; }
    if (siege_pending_cast_) {
        siege_pending_cast_ = false;
        const float after = me.health(); const bool same_tile = me.position() == siege_cast_position_;
        const bool no_loss = same_tile && after + epsilon >= siege_cast_health_;
        if (!no_loss && !siege_damage_observed_)
            telemetry_.event("mechanism_failed", "siege_damage_or_displacement", group_field(groups_[siege_group_].id)
                + "," + number("selfHealthBefore", siege_cast_health_) + "," + number("selfHealthNext", after)
                + ",\"sameTile\":" + (same_tile ? "true" : "false"), true);
        if (!no_loss) siege_damage_observed_ = true;
        if (no_loss) ++siege_safe_casts_;
        if (siege_casts_ <= 4)
            telemetry_.event("observation", "siege_next_camp_phase", group_field(groups_[siege_group_].id)
                + "," + integer("firingTile", tile_index(me.position())) + "," + number("selfHealthBefore", siege_cast_health_)
                + "," + number("selfHealthNext", after) + ",\"sameTile\":" + (same_tile ? "true" : "false"));
        if (!siege_witnessed_ && !siege_damage_observed_ && siege_safe_casts_ >= 2) {
            siege_witnessed_ = true;
            telemetry_.event("mechanism_success", "siege_safe_repeat", group_field(groups_[siege_group_].id)
                + "," + integer("safeCastPhases", siege_safe_casts_), true);
        }
    }
    if (siege_state_ == SiegeState::capture
        && (siege_holding_ || groups_[siege_group_].spawns.is_set(me.position())) && seen(groups_[siege_group_])) {
        const auto state = groups_[siege_group_].group.state();
        if (std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
            if (me.gem_count() > siege_capture_bag_) {
                siege_state_ = SiegeState::bank; siege_bank_score_ = cambc::global_submitted_gems(team::ally);
                telemetry_.event("observation", "siege_reward", group_field(groups_[siege_group_].id)
                    + "," + integer("ownGemDelta", me.gem_count() - siege_capture_bag_) + "," + integer("bag", me.gem_count()), true);
            } else siege_exit("reward_without_own_gems", true);
        }
    }
    // The common runtime preserves a hold before core_turn; observe occupancy
    // here so that this early return cannot hide the later reward transition.
    if (siege_state_ == SiegeState::capture && groups_[siege_group_].spawns.is_set(me.position()) && reward(groups_[siege_group_]))
        siege_holding_ = true;
    if (siege_state_ == SiegeState::bank && !me.is_submitting_gems() && !me.gem_count()) {
        const auto score = cambc::global_submitted_gems(team::ally);
        if (previous_submit_ && score > siege_bank_score_) {
            telemetry_.event("mechanism_success", "siege_route_conversion", group_field(groups_[siege_group_].id)
                + "," + integer("publicScoreDelta", score - siege_bank_score_), true);
            siege_exit("converted");
        } else siege_exit("bag_lost", true);
    }
}
bool core_bank_request(int nearest) {
    return base_bank_request(nearest) || (siege_actor() && siege_state_ == SiegeState::bank && cambc::self().gem_count());
}
bool hoard_bank() { return false; }
bool core_move(bitboard, const bitboard*) { return false; }
bool core_group_allowed(int index, float) {
    if constexpr (profile::probe_mode) {
        const int probe = find_group(profile::probe_group);
        if (probe >= 0) {
            if (index == probe) return false;
            if (!siege_actor() && distance(groups_[index].anchor, groups_[probe].anchor) <= 8) return false;
            if (siege_actor() && !monster_passive_ && !groups_[index].group.gold_value()) return false;
        }
    }
    return true;
}
bool core_partial_allowed() const { return true; }
bool core_fork(const std::vector<Choice>&) { return false; }
void core_selected(int) {}
bool core_retreat(const std::vector<champion>&) const { return false; }
void core_recovery() { if (siege_state_ != SiegeState::bank) siege_exit("recovery"); }
bool core_departing(int) const { return false; }

// Public geometry is cached one field per owner turn, with B0 work between.
// Candidate evaluation never starts a per-candidate pathfield search.
bool siege_prepare_geometry() {
    if (!siege_geometry_initialized_) {
        unsigned scanned = 0;
        for (std::size_t i = 0; i < groups_.size() && scanned < profile::siege_candidate_limit; ++i) {
            if (!groups_[i].group.gem_value()) continue;
            ++scanned; siege_geometry_.push_back({static_cast<int>(i)});
        }
        if constexpr (profile::probe_mode) {
            const int forced = find_group(profile::probe_group);
            if (forced >= 0 && !siege_geometry(forced)) siege_geometry_.push_back({forced});
        }
        siege_geometry_initialized_ = true;
    }
    if (!siege_altar_attempted_) {
        if (cambc::fuel_remaining() < 45000) return false;
        bitboard goals; for (const auto altar : altars_) goals |= around(altar, cambc::constants::submit_gems_range);
        Field value;
        if (field(value, goals, terrain_)) siege_altar_field_ = std::move(value);
        siege_altar_attempted_ = true; return false;
    }
    if (!siege_altar_field_) return false;
    for (auto& geometry : siege_geometry_) if (!geometry.attempted) {
        if (cambc::fuel_remaining() < 45000) return false;
        const auto& group = groups_[geometry.group];
        for (const auto p : group.spawns.positions()) {
            const int d = siege_altar_field_->get(p);
            if (d < geometry.altar_distance || (d == geometry.altar_distance && tile_index(p) < tile_index(geometry.capture_tile))) {
                geometry.capture_tile = p; geometry.altar_distance = d;
            }
        }
        if (geometry.altar_distance != unreachable) {
            Field value;
            if (field(value, around(geometry.capture_tile, 0), terrain_)) geometry.capture = std::move(value);
        }
        geometry.attempted = true; return false;
    }
    return true;
}
struct SiegeView {
    bitboard firing, threats; float health = 0;
    unsigned visible_live = 0, assumed_live = 0, visible_threats = 0, assumed_threats = 0;
};
SiegeView siege_view(int index, const std::vector<camp>& camps, const std::vector<champion>& enemies, float range) {
    const auto& group = groups_[index]; const auto visible = cambc::vision::visible_tiles(); SiegeView result;
    for (const auto& member : group.group) {
        const auto p = member.spawn_position(); const auto base = member.base_stats();
        if (visible.is_set(p)) {
            if (member.is_alive()) {
                result.health += member.health(); ++result.visible_live; result.firing |= around(p, static_cast<int>(range));
                if (member.strength() > 0) { result.threats |= around(p, member.attack_range()); ++result.visible_threats; }
            }
        } else {
            result.health += base.base_health; ++result.assumed_live;
            if (base.base_strength > 0) { result.threats |= around(p, base.base_attack_range); ++result.assumed_threats; }
        }
    }
    for (const auto& monster : camps)
        if (!group.spawns.is_set(monster.spawn_position()) && monster.is_alive() && monster.is_awake() && monster.strength() > 0)
            result.threats |= around(monster.position(), monster.attack_range());
    result.firing &= ~terrain_ & ~result.threats;
    for (const auto& enemy : enemies) result.firing &= ~around(enemy.position(), 8);
    result.firing &= ~cambc::vision::camp_tiles() & ~cambc::vision::teleport_tiles();
    auto occupants = cambc::vision::champion_tiles(); occupants.set(cambc::self().position(), false);
    result.firing &= ~occupants; return result;
}
bool siege_firing_sees(const Group& group, vec2 p, float range) {
    const auto hypothetical = cambc::self().visible_tiles_from(p); const auto visible = cambc::vision::visible_tiles();
    for (const auto& member : group.group)
        if (visible.is_set(member.spawn_position()) && member.is_alive()
            && distance(p, member.spawn_position()) <= range && hypothetical.is_set(member.spawn_position())) return true;
    return false;
}
unsigned siege_prediction(const SiegeGeometry& geometry, vec2 firing, float health, float raw, unsigned cooldown, unsigned travel) {
    const auto capture = geometry.capture->get(firing);
    if (capture == unreachable || geometry.altar_distance == unreachable) return std::numeric_limits<unsigned>::max();
    return siege_policy::work(siege_policy::casts(health, raw), cooldown, travel, capture,
        std::max(0, cambc::constants::capture_time), geometry.altar_distance,
        channel_duration(cambc::self().gem_count() + groups_[geometry.group].group.gem_value()));
}
bool siege_fits(unsigned predicted) const {
    return siege_policy::fits(cambc::round_number(), predicted, cambc::constants::max_rounds, profile::siege_completion_reserve);
}
bool siege_commit(const cambc::ability& spell, const std::vector<camp>& camps, const std::vector<champion>& enemies) {
    if (!ensure_origin()) return false;
    const auto payload = spell.get<cambc::ability_cc>(); const unsigned cooldown = std::max(1, payload.max_cooldown);
    const float raw = spell.damage_amount();
    int selected = -1; vec2 firing{}; float best_score = -1; unsigned predicted = 0;
    unsigned visible_count = 0, assumed_count = 0; float predicted_health = 0; bool cutoff = false;
    for (auto& geometry : siege_geometry_) {
        if (cambc::fuel_remaining() < 45000 || cutoff) break;
        auto& group = groups_[geometry.group];
        if (!geometry.capture || group.unavailable_until > cambc::round_number()) continue;
        if constexpr (profile::probe_mode) if (group.id != profile::probe_group) continue;
        const auto view = siege_view(geometry.group, camps, enemies, payload.range);
        if (!view.visible_live) continue;
        for (const auto p : view.firing.positions()) {
            if (cambc::fuel_remaining() < 45000) { cutoff = true; break; }
            if constexpr (profile::probe_mode) if (tile_index(p) != profile::probe_waypoint) continue;
            const int travel = origin_field_->get(p); if (travel == unreachable) continue;
            const unsigned total = siege_prediction(geometry, p, view.health, raw, cooldown, travel);
            if (!siege_fits(total) || !siege_firing_sees(group, p, payload.range)) continue;
            const float score = static_cast<float>(group.group.gem_value()) / std::max(1u, total);
            if (score > best_score + epsilon || (std::abs(score - best_score) <= epsilon
                && (selected < 0 || group.id < groups_[selected].id || (group.id == groups_[selected].id && tile_index(p) < tile_index(firing))))) {
                selected = geometry.group; firing = p; best_score = score; predicted = total;
                visible_count = view.visible_threats; assumed_count = view.assumed_threats; predicted_health = view.health;
            }
        }
    }
    if (selected < 0) return false;
    siege_group_ = selected; siege_firing_ = firing; siege_state_ = SiegeState::approach;
    siege_probe_triggered_ = true;
    siege_stalls_ = siege_unseen_ = siege_casts_ = siege_safe_casts_ = 0;
    siege_last_distance_ = origin_field_->get(firing); siege_witnessed_ = false; siege_holding_ = false; siege_damage_observed_ = false;
    telemetry_.event("opportunity", "siege_start", group_field(groups_[selected].id) + "," + integer("firingTile", tile_index(firing))
        + "," + integer("visibleThreats", visible_count) + "," + integer("assumedThreats", assumed_count)
        + "," + integer("predictedCasts", siege_policy::casts(predicted_health, raw)) + "," + integer("predictedRounds", predicted)
        + "," + integer("predictedCompletion", cambc::round_number() + predicted)
        + "," + number("spellDamage", raw));
    return true;
}
bool core_turn(const std::vector<camp>& camps, const std::vector<champion>& enemies, const std::vector<champion>&) {
    if (!siege_actor()) return false;
    auto& me = cambc::self();
    if constexpr (profile::probe_mode) { if (!siege_probe_resolved_ || siege_probe_done_) return false; }
    else if (!profile::core_enabled) { siege_inactive(1, "disabled_control"); return false; }
    if (!monster_passive_) { siege_inactive(2, "monster_strength_not_owned"); return false; }
    const auto spells = me.abilities(); const auto& spell = spells[1];
    if (!spell.is_cc() || !spell.can_target_camps()) return false;
    if constexpr (profile::probe_mode) {
        if (!siege_probe_waypoint_done_) {
            const auto waypoint = tile_position(profile::probe_waypoint);
            if (me.position() != waypoint) { const auto avoid = danger(camps, enemies); ordinary_move(around(waypoint, 0), &avoid); return true; }
            siege_probe_waypoint_done_ = true;
            telemetry_.event("opportunity", "siege_probe_equipped", integer("waypoint", profile::probe_waypoint) + ",\"monsterStrengthOwned\":true");
        }
        // Both arms perform the same public geometry setup. Stay at the legal
        // waypoint while it warms; falling into B0 here would leave the fixture.
        if (!siege_prepare_geometry()) return true;
        if (!profile::core_enabled) {
            if (!siege_active()) {
                siege_group_ = find_group(profile::probe_group); siege_state_ = SiegeState::approach;
                siege_probe_triggered_ = true;
                telemetry_.event("activation", "siege_control_close_combat", group_field(profile::probe_group)
                    + "," + number("selfHealth", me.health()));
            }
            auto& group = groups_[siege_group_];
            if (reward(group) && siege_state_ != SiegeState::capture) {
                siege_state_ = SiegeState::capture; siege_capture_bag_ = me.gem_count();
                telemetry_.event("observation", "siege_capture", group_field(group.id));
            }
            if (siege_state_ == SiegeState::capture && group.spawns.is_set(me.position())) siege_holding_ = true;
            if (siege_state_ == SiegeState::bank) return bank();
            farm_group(siege_group_, camps); return true;
        }
    }
    if (siege_state_ == SiegeState::bank) return bank();
    if (siege_state_ == SiegeState::farm) {
        if (cambc::fuel_remaining() < 45000 || !siege_prepare_geometry()) { siege_inactive(4, "geometry_or_fuel_pending"); return false; }
        if (!siege_commit(spell, camps, enemies)) { siege_inactive(8, "no_eligible_visible_target"); return false; }
    }
    auto& group = groups_[siege_group_];
    if (siege_state_ == SiegeState::capture) {
        if (reward(group)) {
            if (group.spawns.is_set(me.position())) { siege_holding_ = true; return true; }
            const auto avoid = danger(camps, enemies); ordinary_move(group.spawns, &avoid); return true;
        }
        if (seen(group)) { siege_exit("capture_state_changed"); return false; }
        return true;
    }
    if (reward(group)) {
        siege_state_ = SiegeState::capture; siege_capture_bag_ = me.gem_count(); siege_holding_ = group.spawns.is_set(me.position());
        telemetry_.event("observation", "siege_capture", group_field(group.id));
        const auto avoid = danger(camps, enemies); ordinary_move(group.spawns, &avoid); return true;
    }
    if (cambc::fuel_remaining() < 45000) return true;
    if (enemy_near(me.position(), enemies) || enemy_near(siege_firing_, enemies)) { siege_exit("enemy_near"); return false; }
    const auto payload = spell.get<cambc::ability_cc>(); const auto view = siege_view(siege_group_, camps, enemies, payload.range);
    if (!view.visible_live) {
        if (!siege_unseen_) telemetry_.event("observation", "siege_target_unknown", group_field(group.id) + ",\"liveHealth\":null");
        if (++siege_unseen_ >= profile::siege_visibility_timeout) { siege_exit("visibility_timeout"); return false; }
        return true;
    }
    siege_unseen_ = 0; auto* geometry = siege_geometry(siege_group_);
    if (!geometry || !geometry->capture) { siege_exit("geometry_unavailable"); return false; }
    const bool firing_valid = view.firing.is_set(siege_firing_) && siege_firing_sees(group, siege_firing_, payload.range);
    int travel = 0;
    if (!firing_valid || me.position() != siege_firing_) {
        if (cambc::fuel_remaining() < 45000 || !ensure_origin()) return true;
        if (!firing_valid) {
            int best = unreachable; std::optional<vec2> replacement;
            for (const auto p : view.firing.positions()) {
                if (cambc::fuel_remaining() < 45000) break;
                const int d = origin_field_->get(p);
                if (d < best && geometry->capture->get(p) != unreachable && siege_firing_sees(group, p, payload.range)) { replacement = p; best = d; }
            }
            if (!replacement) { siege_exit("unsafe_firing_region"); return false; }
            siege_firing_ = *replacement; siege_last_distance_ = unreachable; siege_stalls_ = 0;
        }
        travel = origin_field_->get(siege_firing_);
        if (travel == unreachable) { siege_exit("unreachable_firing_tile"); return false; }
    }
    const unsigned prediction = siege_prediction(*geometry, siege_firing_, view.health, spell.damage_amount(), std::max(1, payload.max_cooldown), travel);
    if (!siege_fits(prediction)) { siege_exit("completion_reserve"); return false; }
    if (me.position() != siege_firing_) {
        if (travel < siege_last_distance_) siege_stalls_ = 0; else ++siege_stalls_;
        siege_last_distance_ = travel;
        if (siege_stalls_ >= profile::siege_approach_stall) { siege_exit("approach_stalled"); return false; }
        siege_state_ = SiegeState::approach; auto avoid = view.threats | danger(camps, enemies);
        for (const auto& enemy : enemies) avoid |= around(enemy.position(), 8);
        ordinary_move(around(siege_firing_, 0), &avoid);
        if (last_move_unreachable_) { siege_exit("unsafe_or_blocked_approach"); return false; }
        return true;
    }
    siege_state_ = SiegeState::siege;
    if (!spell.is_ready() || cambc::fuel_remaining() < 16000) return true;
    const auto range = spell.valid_tiles(); std::optional<camp> victim; float health = std::numeric_limits<float>::max();
    for (const auto& member : group.group) if (member.is_visible() && member.is_alive() && range.is_set(member.position())) {
        const float hp = member.health();
        if (hp < health - epsilon || (std::abs(hp - health) <= epsilon && (!victim
            || static_cast<unsigned>(static_cast<cambc::handle_camp_t>(member)) < static_cast<unsigned>(static_cast<cambc::handle_camp_t>(*victim))))) { victim = member; health = hp; }
    }
    if (!victim) return true;
    const float raw = spell.damage_amount(); siege_cast_health_ = me.health(); siege_cast_position_ = me.position();
    if (siege_casts_ < 4) telemetry_.event("activation", "siege_attempt", group_field(group.id)); else telemetry_.count("activation");
    const bool success = spell.use(*victim);
    if (success) { ++siege_casts_; siege_pending_cast_ = true; }
    if (siege_casts_ <= 4) telemetry_.event("action_result", "siege_cast", group_field(group.id)
        + "," + integer("firingTile", tile_index(me.position())) + "," + integer("campId", static_cast<unsigned>(static_cast<cambc::handle_camp_t>(*victim)))
        + "," + number("rawDamage", raw) + "," + number("healthBefore", health)
        + "," + (victim->is_visible() ? number("healthAfter", victim->health()) : "\"healthAfter\":null")
        + "," + number("selfHealthBefore", siege_cast_health_) + ",\"success\":" + (success ? "true" : "false"));
    else telemetry_.count("action_result");
    if (success) { (void)me.can_move(); (void)me.can_attack(); }
    return true;
}
