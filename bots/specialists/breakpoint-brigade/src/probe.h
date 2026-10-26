// Included inside Bot. Diagnostic opening is isolated from production turns.
    bool mirrored_probe_home() const {
        const auto own=cambc::map::spawn_region(team::ally).positions();
        const auto enemy=cambc::map::spawn_region(team::enemy).positions();
        return profile::probe_mirror_with_home && !own.empty() && !enemy.empty() && tile(own.front())>tile(enemy.front());
    }
    unsigned fixture_tile(unsigned index) const {
        if (!mirrored_probe_home()) return index;
        bitboard point; point.set({static_cast<int>(index%cambc::map::width()),static_cast<int>(index/cambc::map::width())});
        return tile(point.reflect(cambc::map::symmetry()).positions().front());
    }
    void select_probe_fixture() {
        if (cambc::map::width()!=41 || cambc::map::height()!=41) { probe_stop("not_exercised","declared_collision_geometry_missing"); return; }
        const auto wanted=fixture_tile(profile::probe_group_key);
        for (unsigned i=0;i<groups_.size();++i) if (groups_[i].key==wanted) probe_group_=i;
        if (probe_group_<0) { probe_stop("not_exercised","declared_group_missing"); return; }
        const auto& entry=groups_[probe_group_];
        if (entry.group.camps().size()!=1 || !entry.group.camps().front().is_gold_cluster()
            || entry.incoming>breakpoint::epsilon || entry.group.gem_value()!=0) { probe_stop("not_exercised","target_not_solo_harmless_gold"); return; }
        const auto index=fixture_tile(profile::probe_tile_index);
        probe_stage_={static_cast<int>(index%cambc::map::width()),static_cast<int>(index/cambc::map::width())};
        if (terrain_.is_set(probe_stage_) || group_at_tile_[index]>=0) { probe_stop("not_exercised","staging_tile_not_free"); return; }
        target_=probe_group_;
        event("observation","breakpoint_fixture","\"group\":\""+group_key(probe_group_)+"\",\"stage_tile\":"+std::to_string(index)
            +",\"capture_tile\":"+std::to_string(entry.key)+",\"mirrored_home\":"+boolean(mirrored_probe_home()),true);
        probe_phase_=1;
    }
    bool empty_equipment() const {
        const auto inventory=cambc::self().item_count();
        for (unsigned i=0;i<static_cast<unsigned>(item_type::count);++i)
            if (inventory[item_type{static_cast<item_type::enum_type>(i)}]) return false;
        return true;
    }
    bool prefund_probe() {
        auto& me=cambc::self();
        const unsigned required=3*item_type::monster_shard_of_strength.cost();
        if (me.gold_count()>=required && !funding_capture_started_) return false;
        if (funding_index_>=std::size(profile::probe_funding_group_keys)) {
            probe_stop("not_exercised","declared_legal_gold_sources_insufficient"); return true;
        }
        const auto wanted=fixture_tile(profile::probe_funding_group_keys[funding_index_]);
        int index=-1;
        for (unsigned i=0;i<groups_.size();++i) if (groups_[i].key==wanted) index=i;
        if (index<0 || groups_[index].group.camps().size()!=1 || !groups_[index].group.camps().front().is_gold_cluster()
            || groups_[index].incoming>breakpoint::epsilon) {
            probe_stop("not_exercised","declared_funding_cluster_missing_or_unsafe"); return true;
        }
        auto& entry=groups_[index];
        if (!(entry.spawns&cambc::vision::visible_tiles()).is_empty()) {
            const auto state=entry.group.state();
            if (std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
                if (!funding_capture_started_) { probe_stop("not_exercised","funding_cluster_previously_captured"); return true; }
                const unsigned expected=(cambc::round_number()-funding_capture_round_)*cambc::constants::passive_gold+entry.group.gold_value();
                if (me.gold_count()-funding_capture_gold_!=expected) { probe_stop("not_exercised","funding_gold_not_attributed"); return true; }
                event("observation","breakpoint_legal_funding","\"group\":\""+group_key(index)+"\",\"captured_gold\":"+std::to_string(entry.group.gold_value())
                    +",\"current_gold\":"+std::to_string(me.gold_count())+",\"basic_count\":"+std::to_string(funding_basics_)+",\"equipment_empty\":true",true);
                ++funding_index_; funding_capture_started_=false; funding_basics_=0;
                return true;
            }
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) {
                if (!entry.spawns.is_set(me.position())) { move_to(entry.spawns); return true; }
                if (!funding_capture_started_) {
                    funding_capture_started_=true; funding_capture_round_=cambc::round_number(); funding_capture_gold_=me.gold_count();
                }
                return true;
            }
            for (const auto& member:cambc::vision::camps())
                if (entry.spawns.is_set(member.spawn_position()) && member.is_alive()) {
                    if (member.strength()>breakpoint::epsilon) { probe_stop("not_exercised","funding_camp_dangerous"); return true; }
                    if (me.can_attack() && me.attack_tiles().is_set(member.position())) { me.attack(member); ++funding_basics_; return true; }
                    move_to(around(member.position(),1)); return true;
                }
        }
        move_to(around(entry.anchor,1));
        return true;
    }
    // Both arms fund and walk identically without equipment. Uninvolved allies
    // and the diagnostic opponent park at their own spawn. No abilities are
    // called anywhere in this driver, including after comparison shopping.
    void probe_tick() {
        auto& me=cambc::self();
        if (me.champion_name()!=cambc::champion_type::tavi || probe_finished_) return;
        const unsigned round=cambc::round_number();
        if (!probe_start_) probe_start_=round;
        if (!me.is_alive()) { probe_stop("not_exercised","measurement_actor_died"); return; }
        if (round>=profile::probe_round_limit || (probe_phase_<3 && round-probe_start_>=profile::max_setup_rounds)) {
            probe_stop("not_exercised","legal_setup_or_witness_timeout"); return;
        }
        if (!probe_phase_) select_probe_fixture();
        if (probe_finished_) return;
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) { probe_stop("not_exercised","unexpected_channel"); return; }
        auto& entry=groups_[probe_group_];
        for (const auto& enemy:cambc::vision::champions(team::enemy))
            if (distance(me.position(),enemy.position())<=8) { probe_stop("not_exercised","visible_enemy_within_eight"); return; }
        if (probe_phase_<3) {
            if (!empty_equipment()) { probe_stop("not_exercised","prepurchase_equipment_present"); return; }
            if (probe_phase_==1 && prefund_probe()) return;
            probe_phase_=2;
            if (me.position()!=probe_stage_) { move_to(around(probe_stage_,0)); return; }
            if (!fully_visible(entry,cambc::vision::visible_tiles())) { probe_stop("not_exercised","target_not_fully_visible"); return; }
            const auto member=entry.group.camps().front();
            if (!member.is_visible() || !member.is_alive()
                || !me.attack_tiles().is_set(member.position())) { probe_stop("not_exercised","target_not_fresh_stationary_in_range"); return; }
            probe_expected_health_=member.health(); probe_actor_health_=me.health();
            if (std::abs(member.health()-member.max_health())>breakpoint::epsilon || member.strength()>breakpoint::epsilon) {
                probe_stop("not_exercised","target_already_damaged_or_dangerous"); return;
            }
            refresh_equipment();
            probe_before_=breakpoint::hits(std::vector<float>{member.health()},me.strength()*camp_multiplier_);
            probe_gold_=me.gold_count();
            event("opportunity","breakpoint_prestate","\"group\":\""+group_key(probe_group_)+"\",\"health\":"+number(member.health())
                +",\"strength\":"+number(me.strength())+",\"M\":"+number(camp_multiplier_)+",\"gold\":"+std::to_string(probe_gold_)
                +",\"N_before\":"+std::to_string(probe_before_)+",\"growth\":"+number(me.base_stats().strength_growth)
                +",\"monster_cost\":"+std::to_string(item_type::monster_shard_of_strength.cost())+",\"power_cost\":"+std::to_string(item_type::power_shard_of_strength.cost())
                +",\"equipment_empty\":true,\"stage_tile\":"+std::to_string(tile(me.position())),true);
            probe_shop_allowed_=true; shop(); probe_shop_allowed_=false; probe_shop_frozen_=true;
            if (probe_finished_) return;
            probe_spent_=probe_gold_-me.gold_count();
            const unsigned actual_projection=breakpoint::hits(std::vector<float>{member.health()},me.strength()*camp_multiplier_);
            if (profile::core_enabled && (!probe_minimal_ || probe_after_!=actual_projection)) { probe_stop("mechanism_failed","comparison_shop_not_minimal_or_projection_wrong"); return; }
            probe_after_=actual_projection;
            if (!probe_spent_ || probe_after_>=probe_before_) { probe_stop("mechanism_failed","shop_did_not_remove_a_basic"); return; }
            event("action_result","breakpoint_comparison_shop","\"enabled\":"+boolean(profile::core_enabled)+",\"spent\":"+std::to_string(probe_spent_)
                +",\"reserved_gold\":"+std::to_string(me.gold_count())+",\"N_before\":"+std::to_string(probe_before_)+",\"N_after\":"+std::to_string(probe_after_)
                +",\"strength\":"+number(me.strength())+",\"M\":"+number(camp_multiplier_)+",\"shopping_frozen\":true,\"abilities_disabled\":true",true);
            probe_phase_=3;
            return; // Both arms begin stationary attacks on the next owner turn.
        }
        if (me.health()!=probe_actor_health_) { probe_stop("not_exercised","unexpected_external_actor_damage"); return; }
        if (probe_phase_==3) {
            if (me.position()!=probe_stage_ || !fully_visible(entry,cambc::vision::visible_tiles())) { probe_stop("not_exercised","stationary_visibility_lost"); return; }
            const auto state=entry.group.state();
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) {
                if (probe_basics_!=probe_after_ || probe_expected_health_>breakpoint::epsilon) { probe_stop("mechanism_failed","basic_count_disagreed"); return; }
                event("observation","breakpoint_verify","\"observed_count\":"+std::to_string(probe_basics_)+",\"predicted\":"+std::to_string(probe_after_)
                    +",\"status\":\"exact\",\"spent\":"+std::to_string(probe_spent_),true);
                probe_phase_=4;
            } else {
                const auto member=entry.group.camps().front();
                if (!member.is_visible() || !member.is_alive()) { probe_stop("not_exercised","live_target_lost"); return; }
                if (std::abs(member.health()-probe_expected_health_)>breakpoint::epsilon) {
                    event("observation","breakpoint_verify","\"status\":\"confounded\",\"reason\":\"unattributed_camp_health_change\"",true);
                    probe_stop("not_exercised","health_changed_between_own_attacks"); return;
                }
                if (!me.can_attack() || !me.attack_tiles().is_set(member.position())) { probe_stop("not_exercised","stationary_basic_not_legal"); return; }
                const float damage=me.strength()*camp_multiplier_;
                me.attack(member); ++probe_basics_; ++prediction_basics_; expire_prediction("diagnostic_first_basic");
                probe_expected_health_=std::max(0.0f,probe_expected_health_-damage);
                if (probe_basics_>probe_after_) probe_stop("mechanism_failed","more_basics_than_predicted");
                return;
            }
        }
        if (probe_phase_==4) {
            if (!entry.spawns.is_set(me.position())) { move_to(entry.spawns); return; }
            const auto state=entry.group.state();
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) {
                if (!probe_capture_round_) { probe_capture_round_=round; probe_capture_gold_=me.gold_count(); }
                return; // Preserve the complete capture countdown.
            }
            if (std::holds_alternative<cambc::map::camp_group_respawning>(state) && probe_capture_round_) {
                const unsigned expected=(round-probe_capture_round_)*cambc::constants::passive_gold+entry.group.gold_value();
                const unsigned observed=me.gold_count()-probe_capture_gold_;
                if (observed!=expected) { probe_stop("mechanism_failed","capture_gold_not_attributed"); return; }
                event("mechanism_success","breakpoint_verify","\"status\":\"exact\",\"observed_count\":"+std::to_string(probe_basics_)
                    +",\"predicted\":"+std::to_string(probe_after_)+",\"N_before\":"+std::to_string(probe_before_)+",\"spent\":"+std::to_string(probe_spent_)
                    +",\"capture_gold\":"+std::to_string(entry.group.gold_value())+",\"minimal_enumerated_cost\":"+boolean(profile::core_enabled&&probe_minimal_),true);
                probe_finished_=true;
            } else probe_stop("not_exercised","capture_state_changed");
        }
    }
