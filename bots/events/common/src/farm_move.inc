#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
        bool goal_moved=false;
        if(identity_==unsigned(cambc::champion_type::varg)&&s.bag>0&&!enemies.empty()&&target) {
            bool damaging_dash=false;
            for(const auto&e:enemies)for(const auto&a:e.abilities())if(a.is_dash()) {
                const auto props=a.template get<cambc::ability_dash>();
                damaging_dash|=props.modifier>0&&props.radius>0;
            }
            const bool live=damaging_dash&&target->is_visible()&&target->is_alive();
            if(int(cambc::round_number())!=round)return;
            if(live) {
                const auto result=tempo_move_live_source(goals,s,camps,enemies,round);
                if(result.handled)return;
                goal_moved=result.moved;
            } else goal_moved=move_to(goals);
        } else goal_moved=move_to(goals);
        if(goal_moved) {
#elif P11_GOAL_RECEIPT
        const bool goal_moved=move_to(goals);
        if(goal_receipt_.current(round)){
            goal_receipt_.moved=goal_moved;goal_receipt_.move_after=cambc::fuel_remaining();
            goal_receipt_.goal_round=int(cambc::round_number());goal_receipt_.stage=5;
        }
        if (goal_moved) {
#else
        if (move_to(goals)) {
#endif
#if P11_MOVE_FUEL_GUARD
            if(int(cambc::round_number())!=round)throw p11::movement::Yield{};
            p11::movement::reserve(p11::movement::refresh_reserve,[]{return cambc::fuel_remaining();});
#endif
            if constexpr(requires{Policy::committed_assignment;})publish_commitment(selected,round);
            // Movement can reveal the group behind a bush or wall.
#if P11_MOVE_FUEL_GUARD
            for (const auto& monster : cambc::vision::camps()) {
                if(int(cambc::round_number())!=round)throw p11::movement::Yield{};
                p11::movement::reserve(p11::movement::target_reserve,[]{return cambc::fuel_remaining();});
                if (entry.spawns.is_set(monster.spawn_position()) && monster.is_alive()
                    && me.attack_tiles().is_set(monster.position())) {
                    fight(monster, true, entry.incoming > 0);
                    break;
                }
            }
#else
            for (const auto& monster : cambc::vision::camps())
                if (entry.spawns.is_set(monster.spawn_position()) && monster.is_alive()
                    && me.attack_tiles().is_set(monster.position())) {
                    fight(monster, true, entry.incoming > 0);
                    break;
                }
#endif
        } else if (me.can_move()&&int(cambc::round_number())==round) entry.unavailable_until = round + 4;
