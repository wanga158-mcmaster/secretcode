    bool move_to(bitboard goals, const bitboard* danger = nullptr) {
        auto& me = cambc::self();
#if P11_MOVE_FUEL_GUARD
        const auto observed_round=cambc::round_number();
        p11::movement::reserve(p11::movement::start_reserve,[]{return cambc::fuel_remaining();});
#endif
        if (!me.can_move() || goals.is_set(me.position())) return false;
        auto walls = blocked();
        if (danger) walls |= *danger;
        walls.set(me.position(), false);
        goals &= ~walls;
#if P11_MOVE_FUEL_GUARD
        if(goals.is_empty())return false;
        p11::movement::reserve(30000,[]{return cambc::fuel_remaining();});
        if(cambc::round_number()!=observed_round)throw p11::movement::Yield{};
#else
        if (goals.is_empty() || cambc::fuel_remaining() < 30000) return false;
#endif
        // One native field, stopped at our tile, with max distance 256.
        // This does not bound visited cells or prove an atomic fuel bound.
        // Never build fields inside candidate-scoring loops.
        cambc::distance_field field;
        bitboard visited;
        field.compute_to(walls, goals, around(me.position(), 0), visited, 256);
#if P11_MOVE_FUEL_GUARD
        if(cambc::round_number()!=observed_round)throw p11::movement::Yield{};
#endif
        if (field.get(me.position()) == cambc::distance_field::unreachable) return false;
        // Retry alternatives after a hidden occupant is revealed. The engine
        // preserves the movement allowance on an unsuccessful hidden collision.
        for (int retry = 0; retry < 3; ++retry) {
#if P11_MOVE_FUEL_GUARD
            p11::movement::reserve(p11::movement::retry_reserve,[]{return cambc::fuel_remaining();});
#endif
            const auto fresh_walls = blocked() | walls;
#if P11_MOVE_FUEL_GUARD
            if(cambc::round_number()!=observed_round)throw p11::movement::Yield{};
#endif
            auto best = me.position();
            auto best_distance = field.get(best);
            for (const auto p : fresh_walls.reachable_tiles(me.position(), 1).positions()) {
                const auto d = field.get(p);
                if (d < best_distance) { best = p; best_distance = d; }
            }
            if (best == me.position()) return false;
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
            if(tempo_move_request_) {
                if(!tempo_admit_step(best))return false;
                if(int(cambc::round_number())!=tempo_move_request_->round) {
                    tempo_move_request_->aborted=true;return false;
                }
            }
#endif
            if (me.move_absolute(best)) return true;
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
            if(tempo_move_request_) {
                // A failed hidden collision changed this owner's information.
                // Rebuild the scoped witness next turn; do not reuse its menu.
                tempo_move_request_->aborted=true;return false;
            }
#endif
        }
        return false;
    }
