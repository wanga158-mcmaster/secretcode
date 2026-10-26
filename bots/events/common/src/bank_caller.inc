    bool bank() {
        auto& me = cambc::self();
        [[maybe_unused]] int observed_round=0;
        if constexpr(requires{Policy::packed_clock;})observed_round=cambc::round_number();
        if (!me.gem_count() || altars_.empty()) return false;
        int nearest = 10000;
        for (const auto altar : altars_) nearest = std::min(nearest, distance(me.position(), altar));
        if (!Policy::bank(state(std::max(0,nearest-cambc::constants::submit_gems_range)))) return false;
        bitboard goals;
        for (const auto altar : altars_) {
            if (distance(me.position(), altar) <= cambc::constants::submit_gems_range) {
                // Opposing locks require queried successful hit counts. Friendly locks
                // allow tributing. Only use a fresh visible lock observation.
                for (const auto& lock : cambc::vision::altar_locks(team::enemy))
                    if (lock.position() == altar) {
                        if (me.can_attack()) me.attack(lock);
                        return true;
                    }
                me.submit_gems(altar);
                if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;}) {
                    if(cambc::fuel_remaining()>25000)trace_channel(channel_snapshot(),true);
                }
                return true;
            }
            goals |= around(altar, cambc::constants::submit_gems_range);
        }
        const bool moved=move_to(goals);
        if constexpr(requires{Policy::packed_clock;}) {
            if(moved&&int(cambc::round_number())==observed_round&&cambc::fuel_remaining()>30000) {
                for(const auto altar:altars_)
                    if(distance(me.position(),altar)<=cambc::constants::submit_gems_range) {
                        // The recursive call is already in range, so it cannot
                        // move or recurse again. Channel start is a remaining
                        // legal action after arrival, before owner-end ticks.
                        bank();
                        if(cambc::fuel_remaining()>15000&&me.is_submitting_gems())
                            std::printf("p11arrival actor=%u round=%d\n",unsigned(static_cast<cambc::handle_champion_t>(me)),observed_round);
                        return true;
                    }
            }
        }
        return moved;
    }
