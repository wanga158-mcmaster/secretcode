// Included inside Bot. A diagnostic-only legal opening, with no live shared
// memory. Both arms use the same fixed round schedule, gear and safe spell hits.
    bool select_probe_fixture() {
        const auto selected = probe_fixture_tile(profile::probe_group_key);
        const vec2 chosen{static_cast<int>(selected % cambc::map::width()), static_cast<int>(selected / cambc::map::width())};
        for (unsigned i = 0; i < groups_.size(); ++i) {
            bool wolf = false, elk = false;
            for (const auto& member : groups_[i].group) { wolf |= member.is_wolf(); elk |= member.is_elk(); }
            // Reflection can change which member has the group's minimum tile.
            if (groups_[i].spawns.is_set(chosen) && groups_[i].group.camps().size() == 2 && wolf && elk) {
                probe_group_ = i; break;
            }
        }
        if (probe_group_ < 0) { probe_stop("not_exercised", "declared_two_threat_lab_missing"); return false; }
        return true;
    }
    vec2 probe_point(unsigned canonical) const {
        const auto value = probe_fixture_tile(canonical);
        return {static_cast<int>(value % cambc::map::width()), static_cast<int>(value / cambc::map::width())};
    }
    std::optional<camp> probe_member(bool wolf) {
        for (const auto& member : groups_[probe_group_].group)
            if ((wolf ? member.is_wolf() : member.is_elk()) && member.is_visible() && member.is_alive()) return member;
        return std::nullopt;
    }
    bool fund_probe() {
        auto& me = cambc::self();
        if (probe_funded_) return true;
        const auto point = probe_point(profile::probe_funding_tile);
        if (probe_funding_group_ < 0) {
            for (unsigned i = 0; i < groups_.size(); ++i)
                if (groups_[i].spawns.is_set(point) && groups_[i].group.camps().size() == 1
                    && groups_[i].group.camps().front().is_gold_cluster()) { probe_funding_group_ = i; break; }
            if (probe_funding_group_ < 0) { probe_stop("not_exercised", "declared_funding_cluster_missing"); return false; }
        }
        auto& entry = groups_[probe_funding_group_];
        if (!(entry.spawns & cambc::vision::visible_tiles()).is_empty()) {
            const auto state = entry.group.state();
            if (std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
                if (!probe_funding_holding_ || me.gold_count() < item_type::monster_shard_of_focus.cost()) {
                    probe_stop("not_exercised", "funding_capture_not_attributed"); return false;
                }
                probe_funded_ = true;
                event("action_result", "integral_prepare_funding", "\"group\":\"g_" + std::to_string(entry.key)
                    + "\",\"capture_complete\":true,\"gold\":" + std::to_string(me.gold_count()), true);
                return true;
            }
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) {
                if (entry.spawns.is_set(me.position())) probe_funding_holding_ = true;
                else move_to(entry.spawns);
                return false;
            }
        }
        const auto member = entry.group.camps().front();
        if (member.is_visible() && member.is_alive() && fight(member, false, false)) return false;
        const auto goals = member.is_visible() && member.is_alive() ? around(member.position(), me.attack_range()) : around(point, 1);
        if (move_to(goals) && member.is_visible() && member.is_alive()) fight(member, true, false);
        return false;
    }
    void prepare_probe() {
        auto& me = cambc::self();
        if (probe_prepared_ || probe_finished_) return;
        if (cambc::round_number() >= profile::probe_preparation_deadline) {
            probe_stop("not_exercised", "preparation_deadline_before_fixed_release"); return;
        }
        if (!fund_probe()) return;
        // Buy exactly one Focus shard using captured gold. No emblem,
        // automatic ordinary shopping, stat injection or preset live health.
        const auto shard = item_type::monster_shard_of_focus;
        if (!me.item_count()[shard]) {
            if (me.gold_count() >= shard.cost() && me.can_buy(shard)) {
                me.buy(shard);
                event("action_result", "integral_prepare_gear", "\"item\":\"monster_shard_of_focus\",\"count\":1,\"focus\":" + std::to_string(me.focus()), true);
            } else return;
        }
        const auto stage = probe_point(profile::probe_preparation_tile);
        if (me.position() != stage) { move_to(around(stage, 0)); return; }
        auto wolf = probe_member(true), elk = probe_member(false);
        if (!wolf || !elk) { probe_stop("not_exercised", "preparation_lost_member_visibility_or_life"); return; }
        for (const auto& member : {*wolf, *elk})
            if (distance(stage, member.position()) <= member.attack_range()) {
                probe_stop("not_exercised", "preparation_tile_not_outside_both_attack_ranges"); return;
            }
        if (preparation_wolf_hits_ == 5 && preparation_elk_hits_ == 9) {
            if (!wolf->is_asleep() || !elk->is_asleep()) return;
            probe_prepared_ = true; probe_finished_ = true;
            event("observation", "integral_prepare_complete", "\"wolf_hits\":5,\"elk_hits\":9,\"wolf_H\":" + std::to_string(wolf->health())
                + ",\"elk_H\":" + std::to_string(elk->health()) + ",\"both_asleep\":true,\"tile\":" + std::to_string(tile(stage)), true);
            return;
        }
        const auto selected = preparation_wolf_hits_ < 5 ? *wolf : *elk;
        for (const auto& ability : me.abilities())
            if (ability.is_cc() && me.can_cast() && ability.is_ready() && ability.can_target_camps()
                && ability.valid_tiles().is_set(selected.position())) {
                const float predicted = ability.get<cambc::ability_cc>().modifier * me.focus() * me.base_stats().camp_damage_multiplier;
                if (selected.health() <= predicted + integral::epsilon) {
                    probe_stop("not_exercised", "preparation_would_kill_member"); return;
                }
                const float before = selected.health();
                if (ability.use(selected)) {
                    if (!selected.is_visible() || !selected.is_alive()) { probe_stop("not_exercised", "preparation_member_died"); return; }
                    if (selected.is_wolf()) ++preparation_wolf_hits_; else ++preparation_elk_hits_;
                    event("action_result", "integral_prepare_hit", "\"member\":" + std::to_string(tile(selected.spawn_position()))
                        + ",\"before_H\":" + std::to_string(before) + ",\"after_H\":" + std::to_string(selected.health())
                        + ",\"predicted_damage\":" + std::to_string(predicted) + ",\"focus\":" + std::to_string(me.focus()), true);
                }
                return;
            }
    }
    bool release_probe() {
        auto& me = cambc::self();
        const auto fixed = probe_point(profile::probe_tile_index);
        if (me.position() != fixed || me.health() != me.max_health() || me.buffs().has_shield()) return false;
        auto wolf = probe_member(true), elk = probe_member(false);
        if (!wolf || !elk || !wolf->is_asleep() || !elk->is_asleep()) {
            probe_stop("not_exercised", "release_requires_two_visible_sleeping_living_members"); return false;
        }
        for (const auto& member : {*wolf, *elk})
            if (!me.attack_tiles().is_set(member.position()) || distance(fixed, member.position()) > member.attack_range()
                || !member.strength()) { probe_stop("not_exercised", "release_members_not_both_actionable_threats"); return false; }
        const float damage = me.strength() * camp_multiplier_;
        integral::Candidate w{wolf->health(), wolf->strength(), integral::basic_work(wolf->health(), damage), tile(wolf->spawn_position())};
        integral::Candidate e{elk->health(), elk->strength(), integral::basic_work(elk->health(), damage), tile(elk->spawn_position())};
        const bool active_wolf = integral::precedes(w, e);
        const float ws = w.health + distance(fixed, wolf->position()) * 100.0f;
        const float es = e.health + distance(fixed, elk->position()) * 100.0f;
        const bool control_wolf = ws < es || (ws == es && w.tile < e.tile);
        window_predicted_active_ = active_wolf ? integral::isolated_loss(w,e) : integral::isolated_loss(e,w);
        window_predicted_control_ = control_wolf ? integral::isolated_loss(w,e) : integral::isolated_loss(e,w);
        const float final_health = me.health() - std::max(window_predicted_active_, window_predicted_control_);
        if (active_wolf == control_wolf || w.health == e.health || w.weight == e.weight
            || final_health < me.max_health()*0.25f || fundamentals::should_retreat(final_health, me.max_health(), w.weight+e.weight)
            || window_predicted_active_ >= window_predicted_control_ || !me.can_attack()) {
            probe_stop("not_exercised", "observed_fixture_not_discriminating_and_safely_affordable"); return false;
        }
        expected_first_kill_ = (profile::core_enabled ? active_wolf : control_wolf) ? w.tile : e.tile;
        probe_released_ = true; window_round_ = cambc::round_number();
        window_health_ = window_last_health_ = me.health(); probe_bag_ = me.gem_count();
        event("opportunity", "integral_window", "\"group\":\"g_" + std::to_string(groups_[probe_group_].key)
            + "\",\"tile\":" + std::to_string(tile(fixed)) + ",\"wolf_H\":" + std::to_string(w.health)
            + ",\"elk_H\":" + std::to_string(e.health) + ",\"wolf_w\":" + std::to_string(w.weight)
            + ",\"elk_w\":" + std::to_string(e.weight) + ",\"wolf_p\":" + std::to_string(w.work)
            + ",\"elk_p\":" + std::to_string(e.work) + ",\"predicted_active_loss\":" + std::to_string(window_predicted_active_)
            + ",\"predicted_control_loss\":" + std::to_string(window_predicted_control_)
            + ",\"expected_first_kill\":" + std::to_string(expected_first_kill_), true);
        return true;
    }
    void measured_probe() {
        auto& me = cambc::self();
        if (!me.is_alive()) { probe_stop("mechanism_failed", "measurement_death"); return; }
        const auto fixed = probe_point(profile::probe_tile_index);
        if (!probe_holding_ && me.position() != fixed) { probe_stop("mechanism_failed", "measurement_displaced"); return; }
        if (window_last_health_ > me.health()) { window_loss_ += window_last_health_-me.health(); ++window_phases_; }
        else if (me.health() > window_last_health_) window_confounded_ = true;
        window_last_health_ = me.health();
        const auto visible = cambc::vision::visible_tiles();
        if (!(groups_[probe_group_].spawns & ~visible).is_empty()) { probe_stop("mechanism_failed", "measurement_visibility_lost"); return; }
        const auto state = groups_[probe_group_].group.state();
        if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) {
            if (!probe_holding_) {
                probe_holding_ = true;
                event("observation", "integral_window", "\"phase_count\":" + std::to_string(window_phases_)
                    + ",\"observed_health_loss\":" + std::to_string(window_loss_) + ",\"first_kill\":" + std::to_string(window_first_kill_)
                    + ",\"second_kill\":" + std::to_string(window_second_kill_) + ",\"basics\":" + std::to_string(window_basics_)
                    + ",\"confounded\":" + boolean(window_confounded_), true);
            }
            if (!groups_[probe_group_].spawns.is_set(me.position())) move_to(groups_[probe_group_].spawns);
            return;
        }
        if (std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
            if (!probe_holding_ || me.gem_count() != probe_bag_ + groups_[probe_group_].group.gem_value()) {
                probe_stop("mechanism_failed", "capture_reward_not_attributed"); return;
            }
            const float expected_loss = profile::core_enabled ? window_predicted_active_ : window_predicted_control_;
            const bool match = window_kills_ == 2 && window_first_kill_ == expected_first_kill_ && !window_confounded_
                && std::abs(window_loss_-expected_loss) < 0.01f;
            event(match ? "mechanism_success" : "mechanism_failed", "integral_window",
                "\"reason\":\"" + std::string(match ? "order_and_loss_match_isolated_prediction_capture_complete" : "order_or_phase_model_mismatch")
                + "\",\"phase_count\":" + std::to_string(window_phases_) + ",\"observed_health_loss\":" + std::to_string(window_loss_)
                + ",\"predicted_loss\":" + std::to_string(expected_loss) + ",\"first_kill\":" + std::to_string(window_first_kill_)
                + ",\"second_kill\":" + std::to_string(window_second_kill_) + ",\"capture_complete\":true,\"confounded\":" + boolean(window_confounded_), true);
            probe_finished_ = true; return;
        }
        auto selected = ranked_member(probe_group_, false, profile::core_enabled);
        if (!selected || !me.can_attack() || !me.attack_tiles().is_set(selected->member.position())) return;
        record_order(probe_group_, *selected);
        const auto member = selected->member;
          const auto last_position = member.position();
        me.attack(member); ++window_basics_;
          // A killed camp no longer has a live entity view. Its state query is
          // nevertheless legal while its last occupied tile remains visible.
          if (!cambc::vision::visible_tiles().is_set(last_position)) { probe_stop("mechanism_failed", "post_hit_tile_visibility_lost"); return; }
          if (member.is_dead()) {
            if (!window_kills_) window_first_kill_ = tile(member.spawn_position());
            else window_second_kill_ = tile(member.spawn_position());
            ++window_kills_;
        }
        (void)ranked_member(probe_group_, false, profile::core_enabled);
    }
    bool probe_tick(const std::vector<camp>&, const std::vector<champion>& enemies) {
        auto& me = cambc::self();
        if (cambc::fuel_remaining() < 45000) return true;
        if (probe_group_ < 0 && !select_probe_fixture()) return true;
        if (!enemies.empty()) { probe_stop(probe_released_ ? "mechanism_failed" : "not_exercised", "visible_enemy_contamination"); return true; }
        if (me.champion_name() == cambc::champion_type::tavi) { prepare_probe(); return true; }
        if (me.champion_name() != cambc::champion_type::vendetta) return true;
        if (probe_released_) { measured_probe(); return true; }
        if (cambc::round_number() < profile::probe_measurement_departure) return true;
        const auto fixed = probe_point(profile::probe_tile_index);
        if (me.position() != fixed) { move_to(around(fixed, 0)); return true; }
        if (release_probe()) measured_probe();
        return true;
    }
