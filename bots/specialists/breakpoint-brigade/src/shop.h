// Included inside Bot. Only Tavi's ordinary shopping hook is replaced.
    void refresh_equipment() {
        const auto inventory=cambc::self().item_count();
        monster_shards_=item_progress(item_type::monster_shard_of_strength);
        jungle_shards_=item_progress(item_type::jungle_shard_of_health);
        camp_multiplier_=cambc::self().base_stats().camp_damage_multiplier;
        incoming_multiplier_=1;
        bool monster=false,jungle=false,durability=false;
        for (unsigned i=0;i<static_cast<unsigned>(item_type::count);++i) {
            const item_type item{static_cast<item_type::enum_type>(i)};
            if (!inventory[item]) continue;
            const auto meta=item.info();
            if (meta.passive==cambc::item_passive::monster && !monster) { camp_multiplier_*=1+meta.passive_magnitude; monster=true; }
            if (meta.passive==cambc::item_passive::jungle && !jungle) { incoming_multiplier_*=1-meta.passive_magnitude; jungle=true; }
            if (meta.passive==cambc::item_passive::durability && !durability) { incoming_multiplier_*=1-meta.passive_magnitude; durability=true; }
        }
    }
    bool stage_eligible(const Group& group) const {
        return !group.monolith && group.unavailable_until<=cambc::round_number()
            && !(has_non_monolith_gems_ && monster_shards_>=item_type::monster_shard_of_strength.combine_count()
                && jungle_shards_>=item_type::jungle_shard_of_health.combine_count() && group.group.gem_value()==0);
    }
    bool fully_visible(const Group& group,const bitboard& visible) const { return (group.spawns&~visible).is_empty(); }
    bool breakpoint_shop() {
        auto& me=cambc::self();
        if (cambc::fuel_remaining()<45000 || !me.is_alive()
            || me.champion_name()!=cambc::champion_type::tavi || !me.can_buy(item_type::monster_shard_of_strength)) return false;
        for (const auto& enemy:cambc::vision::champions(team::enemy))
            if (distance(me.position(),enemy.position())<=8) return false;
        const unsigned snapshot_round=cambc::round_number();
        const auto visible=cambc::vision::visible_tiles();
        const auto camps=cambc::vision::camps();
        const auto allies=cambc::vision::champions(team::ally);
        // One personal observation, one pass. No extra fields or live group
        // queries are constructed while ranking candidates.
        std::vector<float> health(groups_.size(),0),incoming(groups_.size(),0);
        for (const auto& member:camps) if (member.is_alive()) {
            const int group=group_at_tile_[tile(member.spawn_position())];
            if (group>=0) { health[group]+=member.health(); incoming[group]+=member.strength(); }
        }
        const auto qualified=[&](int i) { return i>=0 && stage_eligible(groups_[i])
            && fully_visible(groups_[i],visible) && health[i]>breakpoint::epsilon; };
        int selected=-1,partial=-1; float best=-1,partial_score=-1;
        // The prior commitment has explicit contract precedence whenever its
        // entire live group is currently visible. B0 ranking is the fallback.
        if (target_>=0 && fully_visible(groups_[target_],visible) && health[target_]>breakpoint::epsilon) selected=target_;
        else {
            const float dps=me.strength()*1.125f+me.focus()*0.12f;
            for (unsigned i=0;i<groups_.size();++i) {
                if (cambc::fuel_remaining()<45000) { ++planning_guards_; return false; }
                if (!qualified(i)) continue;
                float travel=distance(me.position(),groups_[i].anchor)+(i*3+identity_*11)%7;
                for (const auto& ally:allies) if (ally!=me && distance(ally.position(),groups_[i].anchor)<=3) travel+=12;
                const float score=(groups_[i].group.gem_value()*2.0f+groups_[i].group.gold_value()*0.4f)
                    /(travel+health[i]/std::max(1.0f,dps)+10);
                if (fundamentals::affordable_camp(me.health(),me.max_health(),health[i],incoming[i]*incoming_multiplier_,dps*camp_multiplier_)) {
                    if (score>best) { best=score; selected=i; }
                } else if (me.health()>=me.max_health()*0.75f && score>partial_score) { partial_score=score; partial=i; }
            }
            if (selected<0) selected=partial;
        }
        if (selected<0 || cambc::fuel_remaining()<45000) return false;
        std::vector<float> vector;
        unsigned hash=2166136261u;
        for (const auto& member:camps) if (groups_[selected].spawns.is_set(member.spawn_position()) && member.is_alive()) {
            const float hp=member.health(); vector.push_back(hp);
            hash=(hash^tile(member.spawn_position()))*16777619u;
            hash=(hash^static_cast<unsigned>(std::lround(hp*1000)))*16777619u;
        }
        if (vector.empty() || cambc::round_number()!=snapshot_round) return false;
        const unsigned before=breakpoint::hits(vector,me.strength()*camp_multiplier_);
        if (before==breakpoint::unbounded) return false;
        const auto inventory=me.item_count();
        bool monster_owned=false;
        for (const auto item:std::array{item_type::monster_emblem_of_strength,item_type::monster_emblem_of_focus,item_type::monster_emblem_of_health})
            monster_owned|=inventory[item]>0;
        breakpoint::Bundle chosen;
        constexpr std::array families{item_type::monster_shard_of_strength,item_type::power_shard_of_strength};
        const float strength=me.strength(),growth=me.base_stats().strength_growth;
        const unsigned gold=me.gold_count();
        for (unsigned family=0;family<families.size();++family) {
            const auto item=families[family]; const auto meta=item.info();
            if (!meta.purchasable || !meta.cost || !meta.result || !meta.combine_count) continue;
            const auto result=meta.result->info(); const unsigned current=inventory[item];
            if (current>=meta.combine_count) continue;
            for (unsigned count=1;count<=5 && current+count<=meta.combine_count;++count) {
                const unsigned cost=count*meta.cost;
                if (cost>gold) break;
                const float delta=breakpoint::point_delta(current,count,meta.combine_count,meta.strength_points,result.strength_points);
                const float projected_strength=strength+delta*growth;
                float multiplier=camp_multiplier_;
                if (current+count==meta.combine_count && result.passive==cambc::item_passive::monster && !monster_owned)
                    multiplier*=1+result.passive_magnitude;
                const unsigned after=breakpoint::hits(vector,projected_strength*multiplier);
                const breakpoint::Bundle candidate{family,count,cost,after,projected_strength,multiplier,true};
                if (breakpoint::better(candidate,chosen,before)) chosen=candidate;
            }
        }
        if (!chosen.valid || !me.can_buy(families[chosen.family],chosen.count)) return false;
        me.buy(families[chosen.family],chosen.count); ++purchases_; ++breakpoint_purchases_;
        ++reserved_gold_turns_; reserved_gold_total_+=me.gold_count();
        prediction_active_=true; prediction_after_=chosen.after; prediction_basics_=0; prediction_confounded_=false;
        for (const auto& ally:allies) if (ally!=me && distance(ally.position(),groups_[selected].anchor)<=8) prediction_confounded_=true;
        event("activation","breakpoint_purchase","\"group\":\""+group_key(selected)+"\",\"snapshot_hash\":"+std::to_string(hash)
            +",\"snapshot_round\":"+std::to_string(snapshot_round)+",\"N_before\":"+std::to_string(before)+",\"N_after\":"+std::to_string(chosen.after)
            +",\"item\":\""+cambc::to_string(families[chosen.family])+"\",\"count\":"+std::to_string(chosen.count)+",\"cost\":"+std::to_string(chosen.cost)
            +",\"projected_strength\":"+number(chosen.strength)+",\"M\":"+number(chosen.multiplier)+",\"reserved_gold\":"+std::to_string(me.gold_count())
            +",\"model\":\"isolated_basics\"",profile::probe_mode);
        if (profile::probe_mode) { probe_minimal_=true; probe_after_=chosen.after; }
        refresh_equipment();
        if (profile::probe_mode && (std::abs(me.strength()-chosen.strength)>breakpoint::epsilon
            || std::abs(camp_multiplier_-chosen.multiplier)>breakpoint::epsilon)) probe_stop("mechanism_failed","purchase_effect_differs_from_projection");
        return true; // Reserve all remaining gold for this owner turn.
    }
    void shop() {
        if (profile::probe_mode && (!probe_shop_allowed_ || probe_shop_frozen_)) return;
        auto& me=cambc::self(); refresh_equipment();
        if (profile::core_enabled && breakpoint_shop()) return;
        // Disabled, non-Tavi, dead, or no improving fresh bundle: B0 bulk shop.
        for (int stage=0;stage<3;++stage) {
            const bool first=monster_shards_<item_type::monster_shard_of_strength.combine_count();
            const bool second=jungle_shards_<item_type::jungle_shard_of_health.combine_count();
            auto item=item_type::power_shard_of_strength; unsigned limit=100;
            if (first) { item=item_type::monster_shard_of_strength; limit=item.combine_count()-monster_shards_; }
            else if (second) { item=item_type::jungle_shard_of_health; limit=item.combine_count()-jungle_shards_; }
            const unsigned count=std::min(limit,me.gold_count()/item.cost());
            if (!count || !me.can_buy(item,count)) break;
            me.buy(item,count); ++purchases_; refresh_equipment();
        }
        refresh_equipment();
    }
