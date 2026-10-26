#if CLOCKWORK_STARTUP_REPAIR
    void initialize_bounded() {
        if (!setup_started_) {
            terrain_ = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
                | cambc::map::spawn_region(team::enemy);
            altars_ = cambc::map::altars().positions();
            group_at_tile_.assign(cambc::map::width()*cambc::map::height(), -1);
            setup_groups_ = cambc::map::camp_groups();
            identity_ = static_cast<unsigned>(cambc::self().champion_name());
            actor_ = cambc::to_string(cambc::self().champion_name());
            setup_started_ = true;
            return;
        }
        if (!setup_component_ready_) {
            auto walls = terrain_;
            walls.set(cambc::self().position(),false);
            cambc::distance_field field;
            field.compute(walls,around(cambc::self().position(),0),home_component_,
                cambc::map::width()*cambc::map::height());
            setup_component_ready_ = true;
            return;
        }
        unsigned prepared = 0;
        while (setup_cursor_ < setup_groups_.size() && prepared < 4 && cambc::fuel_remaining() >= 60000) {
            const auto group = setup_groups_[setup_cursor_++];
            Group entry{group,group.spawn_tiles(),group.camps().front().spawn_position()};
            for (const auto& monster : group) {
                if (tile(monster.spawn_position()) < tile(entry.anchor)) entry.anchor = monster.spawn_position();
                const auto stats = monster.base_stats();
                entry.health += stats.base_health; entry.incoming += stats.base_strength;
                entry.monolith |= monster.is_monolith();
            }
            has_non_monolith_gems_ |= !entry.monolith && entry.group.gem_value()>0;
            groups_.push_back(std::move(entry)); ++prepared;
        }
        if (setup_cursor_ != setup_groups_.size()) return;
        std::sort(groups_.begin(),groups_.end(),[](const Group& a,const Group& b) {
            return a.anchor.y==b.anchor.y ? a.anchor.x<b.anchor.x : a.anchor.y<b.anchor.y;
        });
        for (unsigned i=0;i<groups_.size();++i) {
            groups_[i].key=tile(groups_[i].anchor);
            for (const auto& member : groups_[i].group) group_at_tile_[tile(member.spawn_position())]=i;
        }
        std::sort(altars_.begin(),altars_.end(),[](vec2 a,vec2 b){return tile(a)<tile(b);});
        initialized_=true;
        event("start","profile","\"enabled\":"+boolean(profile::core_enabled)+",\"probe_mode\":"+boolean(profile::probe_mode)
            +",\"scenario_id\":\""+profile::scenario_id+"\",\"native_actor_id\":"
            +std::to_string(static_cast<unsigned>(static_cast<cambc::handle_champion_t>(cambc::self()))),true);
        // Gameplay always starts on the next owner turn, with fresh fuel.
    }
#endif
    void initialize() {
        terrain_ = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
            | cambc::map::spawn_region(team::enemy);
        altars_ = cambc::map::altars().positions();
        group_at_tile_.assign(cambc::map::width() * cambc::map::height(), -1);
        for (auto group : cambc::map::camp_groups()) {
            Group entry{group, group.spawn_tiles(), group.camps().front().spawn_position()};
            for (const auto& monster : group) {
                if (tile(monster.spawn_position()) < tile(entry.anchor)) entry.anchor = monster.spawn_position();
                const auto p = monster.spawn_position();
                group_at_tile_[p.y * cambc::map::width() + p.x] = static_cast<int>(groups_.size());
                // All metadata here is public; no hidden live-state queries.
                const auto stats = monster.base_stats();
                entry.health += stats.base_health;
                entry.incoming += stats.base_strength;
                entry.monolith |= monster.is_monolith();
            }
            has_non_monolith_gems_ |= !entry.monolith && entry.group.gem_value() > 0;
            groups_.push_back(std::move(entry));
        }
        std::sort(groups_.begin(), groups_.end(), [](const Group& a, const Group& b) { return a.anchor.y == b.anchor.y ? a.anchor.x < b.anchor.x : a.anchor.y < b.anchor.y; });
        for (unsigned i = 0; i < groups_.size(); ++i) {
            groups_[i].key = tile(groups_[i].anchor);
            for (const auto& member : groups_[i].group) group_at_tile_[tile(member.spawn_position())] = i;
        }
        std::sort(altars_.begin(), altars_.end(), [](vec2 a, vec2 b) { return tile(a) < tile(b); });
        identity_ = static_cast<unsigned>(cambc::self().champion_name());
        actor_ = cambc::to_string(cambc::self().champion_name());
        event("start", "profile", "\"enabled\":" + boolean(profile::core_enabled) + ",\"probe_mode\":" + boolean(profile::probe_mode)
            + ",\"scenario_id\":\"" + profile::scenario_id + "\",\"native_actor_id\":"
            + std::to_string(static_cast<unsigned>(static_cast<cambc::handle_champion_t>(cambc::self()))), true);
        initialized_ = true;
    }

    unsigned item_progress(item_type shard) const {
        const auto inventory = cambc::self().item_count();
        const auto emblem = shard.result();
        // Only this declared item stage is bought; existing emblems represent
        // their consumed shards. Do not infer a passive from stage counters.
        return emblem && inventory[*emblem] ? shard.combine_count() : inventory[shard];
    }
    void refresh_equipment() {
        const auto inventory = cambc::self().item_count();
#if CLOCKWORK_CONTINUATION_REPAIR
        // Reuse only this invocation's immutable inventory snapshot. Purchases
        // still call refresh again and no inventory crosses a turn boundary.
        const auto progress = [&](item_type shard) {
            const auto emblem=shard.result();
            return emblem && inventory[*emblem] ? shard.combine_count() : inventory[shard];
        };
        monster_shards_ = progress(item_type::monster_shard_of_strength);
        jungle_shards_ = progress(item_type::jungle_shard_of_health);
#else
        monster_shards_ = item_progress(item_type::monster_shard_of_strength);
        jungle_shards_ = item_progress(item_type::jungle_shard_of_health);
#endif
        camp_multiplier_ = cambc::self().base_stats().camp_damage_multiplier;
        incoming_multiplier_ = 1;
        if (inventory[item_type::monster_emblem_of_strength])
            camp_multiplier_ *= 1 + item_type::monster_emblem_of_strength.info().passive_magnitude;
        if (inventory[item_type::jungle_emblem_of_health])
            incoming_multiplier_ *= 1 - item_type::jungle_emblem_of_health.info().passive_magnitude;
    }
    void shop() {
        auto& me = cambc::self();
        refresh_equipment();
        // The calendar diagnostic uses identical empty equipment in both arms.
        // Queried passive_gold is zero; captures fund gold but no diagnostic
        // purchase is permitted, including after the first legal gold reward.
        if (profile::probe_mode) return;
        // A bulk buy completes one stage at a time; Tavi may do this in the field.
        for (int stage = 0; stage < 3; ++stage) {
            const bool first = monster_shards_ < item_type::monster_shard_of_strength.combine_count();
            const bool second = jungle_shards_ < item_type::jungle_shard_of_health.combine_count();
            auto item = item_type::power_shard_of_strength;
            unsigned limit = 100;
            if (first) {
                item = item_type::monster_shard_of_strength;
                limit = item.combine_count() - monster_shards_;
            } else if (second) {
                item = item_type::jungle_shard_of_health;
                limit = item.combine_count() - jungle_shards_;
            }
            const unsigned count = std::min(limit, me.gold_count() / item.cost());
            if (!count || !me.can_buy(item, count)) { refresh_equipment(); return; }
            me.buy(item, count);
            ++purchases_;
            if (first) monster_shards_ += count;
            else if (second) jungle_shards_ += count;
        }
        refresh_equipment();
    }

    bitboard blocked() const {
        auto result = terrain_ | cambc::vision::champion_tiles() | cambc::vision::camp_tiles()
            | cambc::vision::teleport_tiles();
        result.set(cambc::self().position(), false);
        return result;
    }

    bool move_to(bitboard goals, const bitboard* danger = nullptr) {
        auto& me = cambc::self();
        if (!me.can_move() || goals.is_set(me.position())) return false;
        auto walls = blocked();
        if (danger) walls |= *danger;
        walls.set(me.position(), false);
        goals &= ~walls;
        if (goals.is_empty() || cambc::fuel_remaining() < 30000 || fields_this_turn_ >= 2) return false;
        ++fields_this_turn_;
        // One native field, stopped at our tile, at most 256 expansion steps.
        // Never build fields inside candidate-scoring loops.
        cambc::distance_field field;
        bitboard visited;
        field.compute_to(walls, goals, around(me.position(), 0), visited, 256);
        if (field.get(me.position()) == cambc::distance_field::unreachable) return false;
        // Retry alternatives after a hidden occupant is revealed. The engine
        // preserves the movement allowance on an unsuccessful hidden collision.
        for (int retry = 0; retry < 3; ++retry) {
            const auto fresh_walls = blocked() | walls;
            auto best = me.position();
            auto best_distance = field.get(best);
            for (const auto p : fresh_walls.reachable_tiles(me.position(), 1).positions()) {
                const auto d = field.get(p);
                if (d < best_distance || (d == best_distance && d < field.get(me.position()) && tile(p) < tile(best))) { best = p; best_distance = d; }
            }
            if (best == me.position()) return false;
            if (me.move_absolute(best)) return true;
            ++blocked_retries_;
        }
        return false;
    }

    // A single visible target is attacked; refresh live validity after every
    // hit because kills and Varg splash can invalidate an earlier camp view.
    template<class Target>
    bool fight(const Target& target, bool walked, bool dangerous) {
        CW_FUEL_STAGE(20);
        auto& me = cambc::self();
#if CLOCKWORK_CONTINUATION_REPAIR
        if (cambc::fuel_remaining() < 30000) { ++planning_guards_; return false; }
#else
        if (cambc::fuel_remaining() < 16000) { ++planning_guards_; return false; }
#endif
        auto valid = [&] { return target.is_visible() && target.health() > 0; };
        if (!valid()) return false;
        bool acted = false;
        auto abilities = me.abilities();
        const auto category_ok = [](const cambc::ability& ability) {
            if constexpr (std::is_same_v<Target, camp>) return ability.can_target_camps();
            else return ability.can_target_enemies();
        };
        if (!walked) {
            // Tavi/Marcella burst from range before choosing to walk.
            for (const auto& ability : abilities)
                if ((ability.is_damage() || ability.is_cc()) && ability.is_ready()
                    && category_ok(ability)
                    && ability.valid_tiles().is_set(target.position())) {
                    acted |= ability.use(target);
                    break;
                }
        }
        if (!valid()) return acted;
        CW_FUEL_STAGE(21);
        const bool in_range = me.attack_tiles().is_set(target.position());
        if (!in_range) return acted;
        if (dangerous)
            for (const auto& ability : abilities)
                if (ability.is_shield() && ability.is_ready() && ability.can_target_self()) { acted |= ability.use(); break; }
        if (me.can_attack()) { me.attack(target); acted = true; }
        CW_FUEL_STAGE(22);
        if (!valid()) return acted;
        // Includes ranged champions that walked this turn: a reset explicitly
        // restores their attack allowance before the extra attack.
        if (!me.can_attack())
            for (const auto& ability : abilities)
                if (ability.is_attack_reset() && ability.is_ready() && ability.can_target_self()) {
                    if (ability.use() && me.can_attack() && valid()) me.attack(target);
                    acted = true;
                    break;
                }
        return acted;
    }

    bool recover(const std::vector<camp>& camps, const std::vector<champion>& enemies) {
        auto& me = cambc::self();
        const auto origin = me.position();
        bool safe = true;
        bitboard danger;
        for (const auto& monster : camps)
            if (monster.is_alive() && monster.is_awake() && monster.strength() > 0) {
                danger |= around(monster.position(), monster.attack_range());
                if (distance(origin, monster.position()) <= monster.attack_range() + 1) safe = false;
            }
        for (const auto& enemy : enemies) {
            danger |= around(enemy.position(), enemy.attack_range() + 1);
            if (distance(origin, enemy.position()) <= 8) safe = false;
        }
        if (safe) { me.recall(); return true; }
        // First get out of attack range. Maximise clearance locally if danger
        // surrounds us; a path whose starting tile is threatened may be blocked.
        auto legal = blocked().reachable_tiles(origin, 1);
        auto best = origin;
        float best_score = -1e30f;
        for (const auto p : legal.positions()) {
            float score = 0;
            for (const auto& monster : camps)
                if (monster.is_alive() && monster.is_awake() && monster.strength() > 0)
                    score += std::min(distance(p, monster.position()), 6) * monster.strength();
            for (const auto& enemy : enemies) score += std::min(distance(p, enemy.position()), 9) * 150;
            if (score > best_score) { best = p; best_score = score; }
        }
        if (best != origin && me.can_move() && me.move_absolute(best)) return true;
        move_to(cambc::map::spawn_region(team::ally), &danger);
        return true;
    }

