    // One fresh personal observation drives this bounded local preference.
    struct CampSample { unsigned member; int group; vec2 position; float health,strength; int range; bool awake; };
    struct ActorSample { vec2 position; float strength; int range; bool enemy; };
    std::vector<CampSample> observe_camps(const std::vector<camp>& camps) const {
        std::vector<CampSample> result; result.reserve(camps.size());
        for (const auto& c:camps) if (c.is_alive()) result.push_back({tile(c.spawn_position()),
            group_at_tile_[tile(c.spawn_position())],c.position(),c.health(),c.strength(),c.attack_range(),c.is_awake()});
        return result;
    }
    porcupine::Estimate estimate(int group,vec2 p,const std::vector<CampSample>& samples,
        const std::vector<ActorSample>& actors,const bitboard& visible) const {
        porcupine::Estimate e;
        e.full=(groups_[group].spawns&~visible).is_empty();
        if (!e.full) return e;
        for (const auto& c:samples) {
            const bool member=c.group==group;
            if (member) e.remaining_health+=c.health;
            if (distance(p,c.position)>c.range || c.strength<=0) continue;
            if (member || c.awake) e.incoming+=c.strength*incoming_multiplier_;
            if (!member) continue;
            ++e.triggers;
            for (const auto a:actors) if (distance(a.position,c.position)<=c.range) e.competing=true;
        }
        for (const auto a:actors) if (a.enemy && distance(p,a.position)<=a.range+1) e.incoming+=a.strength*champion_incoming_multiplier_;
        // R is the prescribed coarse potential, not a verified damage rate.
        e.reflected=e.triggers*cambc::self().focus()*cambc::constants::passive_abilities::ren_retaliation_focus_ratio;
        return e;
    }
    bool eligible(int i) const {
        const auto& g=groups_[i];
        return !g.monolith && g.unavailable_until<=cambc::round_number()
            && !(has_non_monolith_gems_ && monster_shards_>=first_item().combine_count()
                && jungle_shards_>=second_item().combine_count() && g.group.gem_value()==0);
    }
    float inherited_score(int i,const porcupine::Estimate& e,const std::vector<vec2>& allies) const {
        auto& me=cambc::self(); float travel=distance(me.position(),groups_[i].anchor);
        for (const auto a:allies) if (distance(a,groups_[i].anchor)<=3) travel+=12;
        travel+=(i*3+identity_*11)%7;
        const float dps=me.strength()*1.125f+me.focus()*0.12f;
        float value=(groups_[i].group.gem_value()*2.0f+groups_[i].group.gold_value()*0.4f)
            /(travel+e.remaining_health/std::max(1.0f,dps)+10);
        if (target_==i) value*=1.2f;
        return value;
    }
    void exit_exposure(const char* reason) {
        if (exposure_group_<0) return;
        event("observation","porcupine_exit","\"group\":\""+key(exposure_group_)+"\",\"reason\":\""+reason+"\"");
        exposure_group_=-1;
    }
    bool qualify(const porcupine::Estimate& e) {
        ++candidates_;
        if (!e.full) { ++hidden_; return false; }
        if (e.competing) { ++conflicts_; return false; }
        if (e.triggers<2) { ++low_trigger_; return false; }
        if (e.reflected/std::max(porcupine::epsilon,e.incoming)<0.5f) { ++low_ratio_; return false; }
        if (!porcupine::qualifies(e,cambc::self().health(),cambc::self().max_health(),cambc::self().strength(),cambc::self().focus())) {
            ++unsafe_; return false;
        }
        return true;
    }
    bool ordinary_action(int group,vec2 p,const std::vector<CampSample>& samples,bool after_move) const {
        auto& me=cambc::self();
        if (me.can_attack()) for (const auto& c:samples) if (c.group==group && distance(p,c.position)<=me.attack_range()) return true;
        // Ren's damaging stun excludes ordinary walking. His dash is never
        // used here, and exposure alone is never a reason to idle for damage.
        if (!after_move) for (const auto& a:me.abilities()) if ((a.is_damage() || a.is_cc()) && a.is_ready() && a.can_target_camps()) {
            const auto mask=a.valid_tiles();
            for (const auto& c:samples) if (c.group==group && mask.is_set(c.position)) return true;
        }
        return false;
    }
    bool exposure_hook(const std::vector<camp>& camps) {
        auto& me=cambc::self();
        if (!profile::core_enabled || profile::draft_only || me.champion_name()!=cambc::champion_type::ren) return false;
        if (cambc::fuel_remaining()<45000) { ++planning_guards_; return false; }
        const auto visible=cambc::vision::visible_tiles();
        const auto samples=observe_camps(camps);
        std::vector<ActorSample> actors; std::vector<vec2> allies;
        for (const auto& actor:cambc::vision::champions()) if (actor!=me) {
            actors.push_back({actor.position(),actor.is_enemy()?actor.strength():0,actor.attack_range(),actor.is_enemy()});
            if (actor.is_ally()) allies.push_back(actor.position());
        }
        int best=-1; porcupine::Estimate best_e; float best_score=-1;
        auto chosen=me.position();
        if (exposure_group_>=0) {
            const int i=exposure_group_;
            if (!eligible(i) || (groups_[i].spawns&visible).is_empty()) exit_exposure("ineligible_or_vision");
            else {
                const auto state=groups_[i].group.state();
                if (!std::holds_alternative<cambc::map::camp_group_alive>(state)) exit_exposure("clear_or_respawn");
                else {
                    const auto e=estimate(i,exposure_tile_,samples,actors,visible);
                    if (!qualify(e) || !ordinary_action(i,exposure_tile_,samples,false)) exit_exposure("safety_visibility_competitor_or_no_action");
                    else { best=i; best_e=e; chosen=exposure_tile_; }
                }
            }
        }
        if (best<0) {
            // Current tile only across groups. Never multiply nine positions
            // by all groups, and never create a field inside this scan.
            for (unsigned i=0;i<groups_.size();++i) {
                if (cambc::fuel_remaining()<45000) { ++planning_guards_; break; }
                if (!eligible(i)) continue;
                const auto e=estimate(i,me.position(),samples,actors,visible);
                if (!qualify(e) || !ordinary_action(i,me.position(),samples,false)) continue;
                const float score=inherited_score(i,e,allies);
                if (best<0 || porcupine::better_group(e,score,groups_[i].key,best_e,best_score,groups_[best].key)) {
                    best=i; best_e=e; best_score=score;
                }
            }
            // An ordinary committed target can approach the first exposure
            // opportunity. Its local neighbours are inspected once, even if
            // our current tile has fewer than two in-range attackers.
            const int local=best>=0?best:target_;
            if (cambc::fuel_remaining()>=45000 && local>=0 && eligible(local) && (groups_[local].spawns&~visible).is_empty()) {
                auto local_best=best_e; auto local_tile=me.position(); bool has_local=best==local;
                const auto legal=blocked().reachable_tiles(me.position(),1);
                for (const auto p:legal.positions()) {
                    if (p==me.position()) continue;
                    const auto e=estimate(local,p,samples,actors,visible);
                    if (!qualify(e) || !ordinary_action(local,p,samples,true)) continue;
                    if (!has_local || porcupine::better_tile(e,tile(p),local_best,tile(local_tile))) {
                        has_local=true; local_best=e; local_tile=p;
                    }
                }
                if (has_local) { best=local; best_e=local_best; chosen=local_tile; }
            }
            if (best<0) return false;
            exposure_group_=best; exposure_tile_=chosen; target_=best; partial_clear_=false;
            event("activation","porcupine_candidate","\"group\":\""+key(best)+"\",\"tile\":"+std::to_string(tile(chosen))
                +",\"triggers\":"+std::to_string(best_e.triggers)+",\"R\":"+number(best_e.reflected)+",\"I\":"+number(best_e.incoming)
                +",\"ratio\":"+number(best_e.reflected/std::max(porcupine::epsilon,best_e.incoming))
                +",\"predicted_loss\":"+number(porcupine::expected_loss(best_e,me.strength(),me.focus()))
                +",\"jungle\":"+boolean(jungle_owned_)+",\"monster\":"+boolean(monster_owned_)+",\"durability\":"+boolean(durability_owned_));
        }
        ++exposure_turns_;
        bool walked=false;
        if (me.position()!=chosen) {
            // The retained tile was adjacent when chosen. Revalidate current
            // occupancy; a hidden collision is a failed opportunity, never a
            // permission to force a known occupied step or use Ren's dash.
            if (!me.can_move() || distance(me.position(),chosen)>1 || blocked().is_set(chosen)) { exit_exposure("movement_unavailable"); return false; }
            if (!me.move_absolute(chosen)) { ++blocked_retries_; exit_exposure("hidden_collision"); return true; }
            walked=true;
            if (cambc::fuel_remaining()<30000) { ++planning_guards_; exit_exposure("postmove_planning_guard"); return true; }
            const auto fresh_camps=cambc::vision::camps();
            const auto fresh_samples=observe_camps(fresh_camps);
            std::vector<ActorSample> fresh_actors;
            for (const auto& actor:cambc::vision::champions()) if (actor!=me)
                fresh_actors.push_back({actor.position(),actor.is_enemy()?actor.strength():0,actor.attack_range(),actor.is_enemy()});
            const auto fresh=estimate(best,me.position(),fresh_samples,fresh_actors,cambc::vision::visible_tiles());
            if (!qualify(fresh)) { exit_exposure("postmove_observation_changed"); return true; }
        }
        bitboard action_tiles;
        if (me.can_attack()) action_tiles|=me.attack_tiles();
        if (!walked) for (const auto& a:me.abilities()) if ((a.is_damage() || a.is_cc()) && a.is_ready() && a.can_target_camps()) action_tiles|=a.valid_tiles();
        std::optional<camp> victim; float lowest=std::numeric_limits<float>::max();
        for (const auto& c:cambc::vision::camps()) if (groups_[best].spawns.is_set(c.spawn_position()) && c.is_alive() && action_tiles.is_set(c.position())) {
            const float score=c.health()+distance(me.position(),c.position())*100.0f;
            if (!victim || score<lowest || (score==lowest && tile(c.spawn_position())<tile(victim->spawn_position()))) { victim=c; lowest=score; }
        }
        if (victim) { fight(*victim,walked,true); return true; }
        exit_exposure("no_fresh_ordinary_action"); return walked;
    }

