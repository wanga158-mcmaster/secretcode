    bool mirrored_probe_home() const {
        const auto a=cambc::map::spawn_region(team::ally).positions(),b=cambc::map::spawn_region(team::enemy).positions();
        return !a.empty() && !b.empty() && tile(a.front())>tile(b.front());
    }
    unsigned fixture_tile(unsigned index) const {
        if (!mirrored_probe_home()) return index;
        bitboard point; point.set({static_cast<int>(index%cambc::map::width()),static_cast<int>(index/cambc::map::width())});
        return tile(point.reflect(cambc::map::symmetry()).positions().front());
    }
    vec2 fixture_position(unsigned index) const {
        const unsigned value=fixture_tile(index);
        return {static_cast<int>(value%cambc::map::width()),static_cast<int>(value/cambc::map::width())};
    }
    int fixture_group(unsigned member) const {
        const unsigned value=fixture_tile(member);
        return value<group_at_tile_.size()?group_at_tile_[value]:-1;
    }
    bool visible_group(int group) const { return !(groups_[group].spawns&cambc::vision::visible_tiles()).is_empty(); }
    void support_clear(int group) {
        auto& me=cambc::self();
        if (visible_group(group)) {
            const auto state=groups_[group].group.state();
            if (!std::holds_alternative<cambc::map::camp_group_alive>(state)) {
                ++probe_phase_; return;
            }
        }
        std::optional<camp> victim;
        for (const auto& c:cambc::vision::camps()) if (groups_[group].spawns.is_set(c.spawn_position()) && c.is_alive()
            && (!victim || tile(c.spawn_position())<tile(victim->spawn_position()))) victim=c;
        if (victim && fight(*victim,false,false)) return;
        if (move_to(around(victim?victim->position():groups_[group].anchor,me.attack_range()))) {
            for (const auto& c:cambc::vision::camps()) if (groups_[group].spawns.is_set(c.spawn_position()) && c.is_alive()
                && me.attack_tiles().is_set(c.position())) { fight(c,true,false); break; }
        }
    }
    bool capture_funding(int group,unsigned expected_gold) {
        auto& me=cambc::self();
        if (me.gold_count()>=expected_gold) {
            event("observation","porcupine_funding","\"group\":\""+key(group)+"\",\"gold\":"+std::to_string(me.gold_count()),true);
            return true;
        }
        if (visible_group(group)) {
            const auto state=groups_[group].group.state();
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) { move_to(groups_[group].spawns); return false; }
            if (std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
                probe_stop("not_exercised","funding_reward_not_received_solo"); return false;
            }
        }
        // Ren waits outside an occupied harmless camp while Tavi legally
        // clears it. Only Ren enters the cleared spawn region to claim gold.
        move_to(around(groups_[group].anchor,1)); return false;
    }
    bool probe_observation(std::array<float,3>& hp,float& incoming,bool& exact_geometry) {
        auto& me=cambc::self(); const int group=fixture_group(profile::probe_group_key);
        if (group<0 || !visible_group(group)) return false;
        const auto visible=cambc::vision::visible_tiles();
        if (!(groups_[group].spawns&~visible).is_empty()) return false;
        if (!std::holds_alternative<cambc::map::camp_group_alive>(groups_[group].group.state())) return false;
        const auto camps=cambc::vision::camps(); incoming=0; exact_geometry=true;
        std::vector<vec2> other_positions;
        for (const auto& actor:cambc::vision::champions())
            if (actor!=me) other_positions.push_back(actor.position());
        for (unsigned i=0;i<3;++i) {
            const unsigned member=fixture_tile(profile::probe_group_key+i*cambc::map::width());
            bool found=false;
            for (const auto& c:camps) if (tile(c.spawn_position())==member && c.is_alive()) {
                hp[i]=c.health(); found=true;
                const auto position=c.position(); const auto strength=c.strength(); const auto range=c.attack_range();
                if (strength<=0 || distance(me.position(),position)>range) exact_geometry=false;
                incoming+=strength*incoming_multiplier_;
                for (const auto position_other:other_positions)
                    if (distance(position_other,position)<=range) exact_geometry=false;
                break;
            }
            if (!found) return false;
        }
        // Any visible additional damage source invalidates the attribution.
        for (const auto& c:camps) if (!groups_[group].spawns.is_set(c.spawn_position()) && c.is_alive() && c.is_awake()
            && c.strength()>0 && distance(me.position(),c.position())<=c.attack_range()) exact_geometry=false;
        return true;
    }
    void probe_tick() {
        auto& me=cambc::self();
        const bool ren=me.champion_name()==cambc::champion_type::ren;
        const bool tavi=me.champion_name()==cambc::champion_type::tavi;
        if ((!ren && !tavi) || probe_finished_) return;
        if (!probe_start_) probe_start_=cambc::round_number();
        if (cambc::map::width()!=50 || cambc::map::height()!=50) {
            if (ren) probe_stop("not_exercised","wrong_declared_rows_geometry");
            else probe_finished_=true;
            return;
        }
        const unsigned round=cambc::round_number();
        if ((!probe_started_ && round-probe_start_>=profile::max_setup_rounds) || round>=profile::probe_round_limit) {
            if (ren) probe_stop("not_exercised","legal_setup_or_window_timeout");
            else probe_finished_=true;
            return;
        }
        if (cambc::fuel_remaining()<45000) { ++planning_guards_; return; }
        const int first=fixture_group(profile::probe_funding_first_key),second=fixture_group(profile::probe_funding_second_key);
        const int group=fixture_group(profile::probe_group_key);
        if (first<0 || second<0 || group<0) { if (ren) probe_stop("not_exercised","declared_group_missing"); return; }
        if (tavi) {
            if (probe_phase_<2) support_clear(probe_phase_?second:first);
            else if (!cambc::map::spawn_region(team::ally).is_set(me.position())) me.recall();
            return;
        }
        const unsigned first_reward=groups_[first].group.gold_value(),second_reward=groups_[second].group.gold_value();
        if (probe_phase_==0) { if (capture_funding(first,first_reward)) ++probe_phase_; return; }
        if (probe_phase_==1) { if (capture_funding(second,first_reward+second_reward)) ++probe_phase_; return; }
        if (probe_phase_==2) {
            const auto spawn=cambc::map::spawn_region(team::ally);
            if (!spawn.is_set(me.position())) { me.recall(); return; }
            if (me.health()<me.max_health()) return;
            const auto item=first_item(); const auto count=item.combine_count();
            if (me.gold_count()<count*item.cost() || !me.can_buy(item,count)) { probe_stop("not_exercised","legal_funding_insufficient"); return; }
            me.buy(item,count); ++purchases_; refresh_equipment();
            const auto inventory=me.item_count(); const auto emblem=item.result();
            if (!emblem || inventory[*emblem]!=1 || me.focus()!=me.base_stats().base_focus+me.base_stats().focus_growth*item.focus_points()*count) {
                probe_stop("mechanism_failed","focus_or_emblem_mismatch"); return;
            }
            event("observation","porcupine_probe_gear","\"focus\":"+number(me.focus())+",\"health\":"+number(me.health())
                +",\"jungle\":"+boolean(jungle_owned_)+",\"monster\":"+boolean(monster_owned_)
                +",\"camp_multiplier\":"+number(camp_multiplier_)+",\"incoming_multiplier\":"+number(incoming_multiplier_),true);
            ++probe_phase_; return;
        }
        const auto exposure=fixture_position(profile::probe_tile_index);
        if (probe_phase_==3) {
            if (me.position()!=exposure) { move_to(around(exposure,0)); return; }
            std::array<float,3> hp; float incoming; bool geometry;
            if (!probe_observation(hp,incoming,geometry) || !geometry) { probe_stop("not_exercised","full_isolated_exposure_unavailable"); return; }
            const float reflected=me.focus()*cambc::constants::passive_abilities::ren_retaliation_focus_ratio*camp_multiplier_;
            for (const float h:hp) if (h<=reflected*3+me.strength()*camp_multiplier_) { probe_stop("not_exercised","members_cannot_survive_three_phases"); return; }
            if (fundamentals::should_retreat(me.health()-3*incoming,me.max_health(),incoming)) {
                probe_stop("not_exercised","three_phase_escape_reserve_unavailable"); return;
            }
            const unsigned wake_member=fixture_tile(profile::probe_member_key);
            std::optional<camp> wake;
            for (const auto& c:cambc::vision::camps()) if (tile(c.spawn_position())==wake_member) wake=c;
            if (!wake || !me.can_attack() || !me.attack_tiles().is_set(wake->position())) { probe_stop("not_exercised","wake_basic_not_legal"); return; }
            event("opportunity","porcupine_exposure","\"group\":\""+key(group)+"\",\"tile\":"+std::to_string(tile(exposure))
                +",\"member0\":"+std::to_string(fixture_tile(168))+",\"member1\":"+std::to_string(fixture_tile(218))+",\"member2\":"+std::to_string(fixture_tile(268))
                +",\"focus\":"+number(me.focus())+",\"expected_incoming\":"+number(incoming)+",\"expected_per_hit_reflection\":"+number(reflected),true);
            me.attack(*wake); // No spell, shield or movement during the window.
            // The legal basic does not move any actor. Refresh only its damaged
            // member; repeating the complete attribution scan here exhausted
            // fuel and suspended the v1 window across rounds.
            for (unsigned i=0;i<3;++i)
                if (fixture_tile(profile::probe_group_key+i*cambc::map::width())==wake_member)
                    hp[i]=wake->health();
            probe_member_health_=hp; probe_actor_health_=me.health(); probe_sample_=round; probe_started_=true; probe_phase_=4;
            event("observation","porcupine_exposure","\"phase\":0,\"health\":"+number(me.health())+",\"hp0\":"+number(hp[0])
                +",\"hp1\":"+number(hp[1])+",\"hp2\":"+number(hp[2])+",\"wake_only\":true",true);
            return;
        }
        if (probe_phase_>=4 && probe_phase_<=6) {
            if (round!=probe_sample_+1 || me.position()!=exposure) { probe_stop("mechanism_failed","window_turn_gap_or_displacement"); return; }
            std::array<float,3> hp; float incoming; bool geometry;
            if (!probe_observation(hp,incoming,geometry) || !geometry) { probe_stop("mechanism_failed","window_target_or_visibility_changed"); return; }
            const float reflected=me.focus()*cambc::constants::passive_abilities::ren_retaliation_focus_ratio*camp_multiplier_;
            const float spent=probe_actor_health_-me.health(); bool exact=porcupine::close(spent,incoming);
            for (unsigned i=0;i<3;++i) exact &= porcupine::close(probe_member_health_[i]-hp[i],reflected);
            if (exact) { probe_verified_hits_+=3; verified_hits_+=3; }
            float delta=0; for (unsigned i=0;i<3;++i) delta+=probe_member_health_[i]-hp[i];
            probe_total_reflected_+=delta; reflected_damage_+=delta; probe_total_spent_+=std::max(0.0f,spent); health_spent_+=std::max(0.0f,spent);
            event("observation","porcupine_exposure","\"phase\":"+std::to_string(probe_phase_-3)+",\"window_round\":"+std::to_string(round)
                +",\"health\":"+number(me.health())+",\"spent\":"+number(spent)+",\"hp0\":"+number(hp[0])+",\"hp1\":"+number(hp[1])+",\"hp2\":"+number(hp[2])
                +",\"delta0\":"+number(probe_member_health_[0]-hp[0])+",\"delta1\":"+number(probe_member_health_[1]-hp[1])+",\"delta2\":"+number(probe_member_health_[2]-hp[2])
                +",\"expected\":"+number(reflected)+",\"exact\":"+boolean(exact)+",\"isolated\":true",true);
            probe_member_health_=hp; probe_actor_health_=me.health(); probe_sample_=round; ++probe_phase_;
            if (!exact) { probe_stop("mechanism_failed","unexplained_incoming_or_retaliation_modifier"); return; }
            if (fundamentals::should_retreat(me.health(),me.max_health(),incoming)) { probe_stop("mechanism_failed","measured_exposure_violated_escape_reserve"); return; }
            if (probe_phase_==7) {
                event("mechanism_success","porcupine_exposure","\"reason\":\"three_isolated_damaging_phases\",\"hits\":"+std::to_string(probe_verified_hits_)
                    +",\"reflected\":"+number(probe_total_reflected_)+",\"health_spent\":"+number(probe_total_spent_)
                    +",\"jungle\":"+boolean(jungle_owned_)+",\"monster\":"+boolean(monster_owned_)+",\"replay_attribution_required\":true",true);
                probe_finished_=true;
            }
        }
    }
