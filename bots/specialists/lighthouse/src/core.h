// Included inside Bot. All observation and ward state is actor-local.
struct LighthouseFact { int kind = 0, timer = -1; float health = -1, incoming = -1; bool complete = true; };
int lh_a_ = -1, lh_b_ = -1, lh_previous_selected_ = -1;
int lh_route_a_index_ = -1, lh_route_b_index_ = -1;
unsigned lh_placed_turn_ = 0, lh_last_cooldown_ = 0;
bool lh_placed_ = false, lh_routes_ready_ = false, lh_fact_pending_ = false;
bool lh_changed_ = false, lh_useful_logged_ = false, lh_probe_done_ = false;
bool lh_same_turn_rerank_ = false;
bool lh_channels_clear_ = false;
int lh_cause_ = -1, lh_cause_kind_ = 0, lh_probe_phase_ = 0, lh_probe_destination_ = -1;
int lh_changed_destination_ = -1;
LighthouseFact lh_fact_a_, lh_fact_b_;
bitboard lh_ward_mask_, lh_interest_, lh_added_;
std::optional<Field> lh_route_a_, lh_route_b_;
unsigned lh_probe_capture_gold_ = 0;
bool lh_probe_timeout_ = false;

std::vector<Choice> lh_pair(const std::vector<Choice>& candidates) const {
    std::vector<Choice> result;
    const bool partial = std::none_of(candidates.begin(), candidates.end(), [](const auto& c) { return c.affordable; });
    const bool healthy = !partial || cambc::self().health() >= cambc::self().max_health() * .75f;
    for (const auto& c : candidates) {
        const bool eligible = c.affordable || (partial && (healthy || c.index == target_));
        if (eligible) { result.push_back(c); if (result.size() == 2) break; }
    }
    return result;
}
LighthouseFact lh_fact(int index, const bitboard& visible, const std::vector<camp>& camps) const {
    LighthouseFact result; const auto& g = groups_[index];
    if ((g.spawns & visible).is_empty()) return result;
    if (cambc::fuel_remaining() < 30000) { result.complete = false; return result; }
    const auto state = g.group.state(); // SDK rechecks real personal visibility.
    if (const auto* r = std::get_if<cambc::map::camp_group_respawning>(&state)) { result.kind = 3; result.timer = r->timer; }
    else {
        result.kind = std::holds_alternative<cambc::map::camp_group_reward_available>(state) ? 2 : 1;
        if ((g.spawns & ~visible).is_empty()) {
            result.health = result.incoming = 0;
            for (const auto& m : camps) if (g.spawns.is_set(m.spawn_position())) {
                if (cambc::fuel_remaining() < 30000) { result.complete = false; result.health = result.incoming = -1; break; }
                if (m.is_visible() && m.is_alive()) { result.health += m.health(); result.incoming += m.strength(); }
            }
        }
    }
    return result;
}
void lh_observation(int index, LighthouseFact& previous, const bitboard& attributable, const LighthouseFact& fresh) {
    if ((groups_[index].spawns & attributable).is_empty() || !fresh.kind || !fresh.complete) return;
    if (fresh.kind != previous.kind || (fresh.health >= 0 && (previous.health < 0 || std::abs(fresh.health - previous.health) > epsilon))) {
        const bool consequential = fresh.kind != 1 || (fresh.health >= 0 && std::abs(fresh.health - groups_[index].health) > epsilon);
        if (consequential) {
            lh_fact_pending_ = true; telemetry_.ward_count(3);
            if (lh_cause_ != lh_previous_selected_ || index == lh_previous_selected_) { lh_cause_ = index; lh_cause_kind_ = fresh.kind; }
        }
        telemetry_.event("observation", "ward_group_fact", group_field(groups_[index].id) + "," + integer("state", fresh.kind)
            + "," + integer("respawnTimer", fresh.timer) + ",\"age\":0," + (fresh.health >= 0 ? number("visibleHealth", fresh.health) : "\"visibleHealth\":null"));
    }
    previous = fresh;
}
// Exactly the inherited score, with a single fresh post-ward observation per group.
std::optional<Choice> lh_refresh_choice(int index, const LighthouseFact& fact, vec2 position,
    float own_health, float own_maximum, float dps, const std::vector<vec2>& allies) {
    auto& g = groups_[index];
    if (fact.kind == 3) {
        g.unavailable_until = cambc::round_number() + std::max(1, fact.timer); return std::nullopt;
    }
    const bool available = fact.kind == 2;
    const float health = fact.health >= 0 ? fact.health : g.health;
    const float incoming = (fact.incoming >= 0 ? fact.incoming : g.incoming) * (jungle_passive_ ? .7f : 1.f);
    const bool affordable = available || fundamentals::affordable_camp(own_health, own_maximum, health, incoming, dps * (monster_passive_ ? 1.1f : 1.f));
    float travel = distance(position, g.anchor);
    for (const auto p : allies) if (distance(p, g.anchor) <= 3) travel += 12;
    travel += (index * 3 + identity_ * 11) % 7;
    float score = (g.group.gem_value() * 2.f + g.group.gold_value() * .4f) / (travel + (available ? 0 : health / std::max(1.f, dps)) + 10);
    if (target_ == index) score *= 1.2f;
    return Choice{index, score, health, incoming, available, affordable};
}
void lh_sort(std::vector<Choice>& values) const {
    std::sort(values.begin(), values.end(), [&](const auto& a, const auto& b) {
        return std::abs(a.score - b.score) > epsilon ? a.score > b.score : groups_[a.index].id < groups_[b.index].id;
    });
}
bool lh_routes(int a, int b) {
#if LIGHTHOUSE_WARD_ROUTES
    if constexpr (!profile::probe_mode) {
        // These fields depend only on immutable public terrain and spawn tiles.
        // Every caller has just supplied a fresh complete eligible fork. Never
        // retain camp state, affordability or scores with the geometry.
        if (lh_route_a_index_ != a || lh_route_b_index_ != b) {
            lh_route_a_.reset(); lh_route_b_.reset();
            lh_route_a_index_ = a; lh_route_b_index_ = b;
        }
        if (!lh_route_a_) {
            Field value;
            if (field(value, groups_[a].spawns, terrain_)) lh_route_a_ = std::move(value);
            return false;
        }
        if (!lh_route_b_) {
            Field value;
            if (field(value, groups_[b].spawns, terrain_)) lh_route_b_ = std::move(value);
            return false;
        }
        if (lh_route_a_->get(cambc::self().position()) == unreachable
            || lh_route_b_->get(cambc::self().position()) == unreachable) return false;
        lh_routes_ready_ = true; return true;
    }
#endif
    Field first, second;
    if (!field(first, groups_[a].spawns, terrain_) || !field(second, groups_[b].spawns, terrain_)) return false;
    if (first.get(cambc::self().position()) == unreachable || second.get(cambc::self().position()) == unreachable) return false;
    lh_route_a_ = std::move(first); lh_route_b_ = std::move(second);
    lh_route_a_index_ = a; lh_route_b_index_ = b; lh_routes_ready_ = true; return true;
}
void lh_route_interest(const Field& route) {
    if constexpr (!profile::probe_mode) {
        if (cambc::fuel_remaining() < 45000) { lh_routes_ready_ = false; return; }
#if LIGHTHOUSE_WARD_ROUTES
        // Interest is only the next three geometric steps, so inspect those
        // neighbors directly instead of exporting two whole map buffers.
        const int width = cambc::map::width(), height = cambc::map::height();
        auto p = cambc::self().position();
        for (int step = 0; step < 3; ++step) {
            const int origin = tile_index(p), origin_d = route.get(p);
            int best = origin, best_d = origin_d;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const vec2 n{p.x + dx, p.y + dy};
                if (n.x < 0 || n.y < 0 || n.x >= width || n.y >= height || terrain_.is_set(n)) continue;
                const int index = tile_index(n), d = route.get(n);
                if (d < best_d || (d == best_d && d < origin_d && index < best)) { best = index; best_d = d; }
            }
            if (best == origin) break;
            p = tile_position(best); lh_interest_.set(p);
        }
        return;
#else
        // Each SDK get/is_set is a host call. Export this public immutable
        // decision geometry once instead of querying every neighbor repeatedly.
        const auto values = route.export_linear_buffer();
        const auto walls = terrain_.export_linear_buffer();
        const int width = cambc::map::width(), height = cambc::map::height();
        auto p = cambc::self().position();
        for (int step = 0; step < 3; ++step) {
            const int origin = tile_index(p), origin_d = values[origin];
            int best = origin, best_d = origin_d;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int x = p.x + dx, y = p.y + dy;
                if (x < 0 || y < 0 || x >= width || y >= height) continue;
                const int index = y * width + x;
                if ((walls[index >> 3] >> (index & 7)) & 1) continue;
                const int d = values[index];
                if (d < best_d || (d == best_d && d < origin_d && index < best)) { best = index; best_d = d; }
            }
            if (best == origin) break;
            p = tile_position(best); lh_interest_.set(p);
        }
        return;
#endif
    }
    auto p = cambc::self().position();
    for (int step = 0; step < 3; ++step) {
        auto best = p; int best_d = route.get(p);
        for (const auto n : around(p, 1).positions()) if (!terrain_.is_set(n)) {
            const int d = route.get(n);
            if (d < best_d || (d == best_d && d < route.get(p) && tile_index(n) < tile_index(best))) { best = n; best_d = d; }
        }
        if (best == p) break;
        lh_interest_.set(best); p = best;
    }
}
bool core_fork(std::vector<Choice>& candidates) {
    auto& me = cambc::self(); const auto pair = lh_pair(candidates);
    if constexpr (profile::probe_mode) if (lh_probe_done_) return false;
    if (!choices_complete_) return false;
    if (pair.size() != 2) return false;
#if LIGHTHOUSE_WARD_ROUTES
    // No action occurs between choices() and this hook. Use that invocation's
    // checked personal mask; post-placement observations below remain fresh.
    if (!lh_menu_visible_) return false;
    const auto& visible = *lh_menu_visible_;
    bool hidden = false;
    for (const auto& c : pair) for (const auto p : groups_[c.index].spawn_positions)
        hidden |= !visible.is_set(p);
#else
    const auto visible = cambc::vision::visible_tiles();
    const bool hidden = !((groups_[pair[0].index].spawns | groups_[pair[1].index].spawns) & ~visible).is_empty();
#endif
    if (!lighthouse_policy::fork(pair[0].score, pair[1].score, hidden)) return false;
    telemetry_.ward_count(0);
    if constexpr (!profile::probe_mode) {
        if (telemetry_.detail_available())
            telemetry_.event("opportunity", "ward_fork", integer("firstGroup", groups_[pair[0].index].id) + "," + integer("secondGroup", groups_[pair[1].index].id)
                + "," + number("firstScore", pair[0].score) + "," + number("secondScore", pair[1].score) + "," + integer("preChoice", groups_[pair[0].index].id)
                + "," + integer("fuelBeforePlanning", cambc::fuel_remaining()));
        else telemetry_.event("opportunity", "ward_fork"); // Same counters/drop, no discarded payload formatting.
    } else {
        telemetry_.event("opportunity", "ward_fork", integer("firstGroup", groups_[pair[0].index].id) + "," + integer("secondGroup", groups_[pair[1].index].id)
            + "," + number("firstScore", pair[0].score) + "," + number("secondScore", pair[1].score) + "," + integer("preChoice", groups_[pair[0].index].id)
            + "," + integer("fuelBeforePlanning", cambc::fuel_remaining()));
    }
    if constexpr (profile::probe_mode && !profile::probe_script) {
        if (pair[0].index != find_group(profile::probe_group) || me.position() != tile_position(profile::probe_waypoint)) {
            telemetry_.event("not_exercised", "probe_fork_preference_changed", "", true); lh_probe_phase_ = 3; return false;
        }
    }
    const bool same_pair = (pair[0].index == lh_a_ && pair[1].index == lh_b_) || (pair[0].index == lh_b_ && pair[1].index == lh_a_);
    const bool old_valid = same_pair && lh_a_ >= 0 && lh_b_ >= 0 && groups_[lh_a_].unavailable_until <= cambc::round_number() && groups_[lh_b_].unavailable_until <= cambc::round_number();
    if (lh_placed_ && lighthouse_policy::hold(turn_, lh_placed_turn_, old_valid)) return false;
    // Same-turn cooldown comes from core_observe; common turn ordering already
    // preserves channels before any farming hook can execute.
    if (!lighthouse_policy::ward_allowed(lh_last_cooldown_, me.restrictions().contains(cambc::buff_restrictions::ward),
        !lh_channels_clear_, cambc::constants::active_wards)) return false;
    const unsigned fuel_at_entry = cambc::fuel_remaining();
    if (fuel_at_entry < profile::ward_start_fuel || fields_ != 0) return false;
    const int a = pair[0].index, b = pair[1].index;
    if (!lh_routes(a, b)) return false;
    const unsigned fuel_after_routes = cambc::fuel_remaining();
    if constexpr (!profile::probe_mode) if (fuel_after_routes < 55000) { lh_routes_ready_ = false; return false; }
    lh_interest_ = groups_[a].spawns | groups_[b].spawns;
    lh_route_interest(*lh_route_a_);
    if constexpr (!profile::probe_mode) if (!lh_routes_ready_) return false;
    lh_route_interest(*lh_route_b_);
    if constexpr (!profile::probe_mode) if (!lh_routes_ready_) return false;
    auto legal = me.ward_placement_tiles() & ~terrain_ & ~cambc::vision::ward_tiles(team::ally)
        & ~cambc::vision::champion_tiles() & ~cambc::vision::camp_tiles() & ~cambc::vision::teleport_tiles();
    auto placements = legal.positions(); const auto interest = lh_interest_.positions();
    const auto interest_distance = [&](vec2 p) { int d = unreachable; for (const auto q : interest) d = std::min(d, distance(p, q)); return d; };
    if constexpr (!profile::probe_mode) {
        struct RankedPlacement { vec2 position; int distance, index; };
        std::vector<RankedPlacement> ranks;
        ranks.reserve(placements.size());
        for (const auto p : placements) {
            if (cambc::fuel_remaining() < 45000) return false;
            ranks.push_back({p, interest_distance(p), tile_index(p)});
        }
        std::sort(ranks.begin(), ranks.end(), [](const auto& a, const auto& b) {
            return a.distance != b.distance ? a.distance < b.distance : a.index < b.index;
        });
        for (std::size_t i = 0; i < ranks.size(); ++i) placements[i] = ranks[i].position;
    } else {
        std::sort(placements.begin(), placements.end(), [&](vec2 x, vec2 y) {
            const int dx = interest_distance(x), dy = interest_distance(y); return dx != dy ? dx < dy : tile_index(x) < tile_index(y);
        });
    }
    unsigned count = 0; int best_tile = std::numeric_limits<int>::max(); bitboard best_mask;
    unsigned evaluated = 0; bool truncated = false;
    for (std::size_t i = 0; i < std::min<std::size_t>(profile::ward_candidate_limit, placements.size()); ++i) {
        if (cambc::fuel_remaining() < 55000) { truncated = true; break; }
        auto hypothetical = cambc::ward::visible_tiles_from(placements[i]); // Geometry, never hidden state.
        ++evaluated;
        const unsigned added = (lh_interest_ & hypothetical & ~visible).count();
        if (lighthouse_policy::better(added, tile_index(placements[i]), count, best_tile)) { count = added; best_tile = tile_index(placements[i]); best_mask = std::move(hypothetical); }
    }
    if (!count) { telemetry_.event("observation", truncated ? "ward_scan_incomplete" : "ward_no_new_interest",
        integer("evaluatedPlacements", evaluated) + ",\"scanTruncated\":" + (truncated ? "true" : "false")
        + "," + integer("fuelAtEntry", fuel_at_entry) + "," + integer("fuelAfterRoutes", fuel_after_routes)
        + "," + integer("fuelAfterPlacementScan", cambc::fuel_remaining())); return false; }
    bitboard old_own_vision, old_ward;
    const auto old_positions = me.ward_positions();
    if (!old_positions.empty()) {
        old_own_vision = me.visible_tiles_from(me.position());
        for (const auto p : old_positions) old_ward |= cambc::ward::visible_tiles_from(p);
    }
    telemetry_.event("activation", "ward_attempt", integer("tile", best_tile) + "," + integer("newInterest", count)
        + "," + integer("evaluatedPlacements", evaluated) + ",\"scanTruncated\":" + (truncated ? "true" : "false")
        + "," + integer("fuelAtEntry", fuel_at_entry) + "," + integer("fuelAfterRoutes", fuel_after_routes)
        + "," + integer("fuelAfterPlacementScan", cambc::fuel_remaining())
        + "," + integer("oldInterest", (old_ward & lh_interest_).count()) + "," + integer("lostInterest", (old_ward & lh_interest_ & ~best_mask & ~old_own_vision).count())
        + "," + integer("cooldownBefore", lh_last_cooldown_) + ",\"enabled\":" + (profile::core_enabled ? "true" : "false"));
    if constexpr (!profile::core_enabled) {
        telemetry_.event("action_result", "ward_disabled_shadow", integer("tile", best_tile) + ",\"placed\":false");
        if constexpr (profile::probe_mode) lh_probe_done_ = true;
        return false;
    }
    if (cambc::fuel_remaining() < 45000) return false;
    telemetry_.ward_count(1);
    const unsigned fuel_before_place = cambc::fuel_remaining();
    const bool placed = me.place_ward(tile_position(best_tile));
    telemetry_.event("action_result", "ward_placed", integer("tile", best_tile) + ",\"placed\":" + (placed ? "true" : "false")
        + "," + integer("cooldownAfter", me.ward_cooldown()));
    if (!placed) return false;
    telemetry_.ward_count(2);
    lh_last_cooldown_ = static_cast<unsigned>(std::max(0, me.ward_cooldown()));
    lh_a_ = a; lh_b_ = b; lh_previous_selected_ = a; lh_placed_ = true; lh_placed_turn_ = turn_;
    lh_fact_pending_ = false; lh_cause_ = -1; lh_changed_ = false; lh_changed_destination_ = -1;
    lh_useful_logged_ = false; lh_fact_a_ = {}; lh_fact_b_ = {};
    lh_ward_mask_ = std::move(best_mask);
    const auto post_visible = cambc::vision::visible_tiles();
    lh_added_ = post_visible & ~visible;
    const auto post_camps = cambc::vision::camps();
    const auto fact_a = lh_fact(a, post_visible, post_camps), fact_b = lh_fact(b, post_visible, post_camps);
    if (!fact_a.complete || !fact_b.complete || cambc::fuel_remaining() < 30000) {
        lh_fact_pending_ = false;
        telemetry_.event(profile::probe_mode ? "not_exercised" : "observation", "ward_rerank_budget_incomplete",
            integer("fuelBeforePlace", fuel_before_place) + "," + integer("fuelAfterFacts", cambc::fuel_remaining()), true);
        if constexpr (profile::probe_mode) lh_probe_phase_ = 3;
        return false; // Keep the prior valid choices; no partial aggregate wins.
    }
    const auto post_position = me.position();
    const float post_health = me.health(), post_maximum = me.max_health(), post_dps = me.strength() * 1.125f + me.focus() * .12f;
    std::vector<vec2> post_allies;
    for (const auto& ally : cambc::vision::champions(team::ally)) if (ally != me) post_allies.push_back(ally.position());
    lh_observation(a, lh_fact_a_, lh_added_, fact_a); lh_observation(b, lh_fact_b_, lh_added_, fact_b);
    std::vector<Choice> refreshed;
    if (auto c = lh_refresh_choice(a, fact_a, post_position, post_health, post_maximum, post_dps, post_allies)) refreshed.push_back(*c);
    if (auto c = lh_refresh_choice(b, fact_b, post_position, post_health, post_maximum, post_dps, post_allies)) refreshed.push_back(*c);
    lh_sort(refreshed); candidates = std::move(refreshed); lh_same_turn_rerank_ = true;
    int post = -1;
    for (const auto& c : candidates) if (c.affordable) { post = c.index; break; }
    if (post < 0 && post_health >= post_maximum * .75f && !candidates.empty()) post = candidates.front().index;
    if (post < 0) for (const auto& c : candidates) if (c.index == target_) { post = c.index; break; }
    telemetry_.event("observation", "ward_rerank", integer("preChoice", groups_[a].id) + ","
        + integer("postChoice", post < 0 ? -1 : groups_[post].id) + "," + integer("addedPersonalTiles", lh_added_.count())
        + "," + integer("fuelBeforePlace", fuel_before_place) + "," + integer("fuelAfterRerank", cambc::fuel_remaining()));
    if constexpr (profile::probe_mode) {
        if (lh_fact_a_.kind != 3) { telemetry_.event("not_exercised", "probe_A_not_respawning", "", true); lh_probe_phase_ = 3; lh_fact_pending_ = false; }
        lh_probe_done_ = true;
    }
    return true;
}
void core_selected(int selected) {
    if (lh_placed_ && lh_fact_pending_ && selected != lh_previous_selected_ && (lh_cause_ == lh_previous_selected_ || lh_cause_ == selected)) {
        lh_changed_ = true;
        if (lh_same_turn_rerank_) lh_changed_destination_ = selected;
        telemetry_.ward_count(4);
        telemetry_.event(lh_same_turn_rerank_ ? "mechanism_success" : "observation", lh_same_turn_rerank_ ? "ward_choice_changed" : "ward_later_choice_change", integer("preChoice", groups_[lh_previous_selected_].id)
            + "," + integer("postChoice", groups_[selected].id) + "," + integer("factGroup", groups_[lh_cause_].id)
            + "," + integer("factState", lh_cause_kind_) + ",\"factAge\":0," + integer("actualNextDestination", groups_[selected].id), true);
    }
    if (lh_placed_) lh_previous_selected_ = selected;
    lh_fact_pending_ = false;
    if constexpr (profile::probe_mode) if (lh_probe_done_ && lh_probe_destination_ < 0) {
        lh_probe_destination_ = selected;
        telemetry_.event("action_result", "probe_actual_destination", group_field(groups_[selected].id) + ",\"enabled\":" + (profile::core_enabled ? "true" : "false"), true);
        if constexpr (!profile::core_enabled) if (selected == find_group(profile::probe_group) && cambc::self().ward_positions().empty()
            && !seen(groups_[selected]))
            telemetry_.event("mechanism_success", "disabled_retained_unobserved_preference", group_field(groups_[selected].id) + ",\"wardCount\":0,\"AHidden\":true", true);
    }
}
bool core_move(bitboard goals, const bitboard* avoid) {
    if (!lh_routes_ready_ || !cambc::self().can_move() || goals.is_set(cambc::self().position())) return false;
    const Field* route = target_ == lh_route_a_index_ && lh_route_a_ ? &*lh_route_a_ : target_ == lh_route_b_index_ && lh_route_b_ ? &*lh_route_b_ : nullptr;
    if (!route) return false;
    auto& me = cambc::self(); auto excluded = blocked(); if (avoid) excluded |= *avoid;
    if constexpr (!profile::probe_mode) {
        if (cambc::fuel_remaining() < 30000) return false;
#if !LIGHTHOUSE_ROUTE_EXECUTOR
        const auto values = route->export_linear_buffer();
#endif
        for (int retry = 0; retry < 3; ++retry) {
            if (cambc::fuel_remaining() < 16000) return false;
#if LIGHTHOUSE_ROUTE_EXECUTOR
            const auto origin = me.position(); const int origin_d = route->get(origin);
            const auto legal = legal_walk() & ~excluded;
#else
            const auto origin = me.position(); const int origin_d = values[tile_index(origin)];
#endif
            auto best = origin; int best_d = origin_d;
#if LIGHTHOUSE_ROUTE_EXECUTOR
            for (const auto p : legal.positions()) {
                const int d = route->get(p);
#else
            for (const auto p : (legal_walk() & ~excluded).positions()) {
                const int d = values[tile_index(p)];
#endif
                if (d < best_d || (d == best_d && d < origin_d && tile_index(p) < tile_index(best))) { best = p; best_d = d; }
            }
            if (best == origin) return false;
            if (me.move_absolute(best)) {
                if (lh_changed_ && lh_cause_kind_ == 3 && !lh_useful_logged_)
                    telemetry_.event("observation", "ward_changed_route_step", integer("emptyGroup", groups_[lh_cause_].id)
                        + "," + integer("actualNextDestination", groups_[target_].id) + "," + integer("actualTile", tile_index(me.position())), true);
                return true;
            }
            excluded |= blocked(); excluded.set(best);
        }
        return false;
    }
    for (int retry = 0; retry < 3; ++retry) {
        auto best = me.position(); int best_d = route->get(best);
        for (const auto p : legal_walk().positions()) if (!excluded.is_set(p)) {
            const int d = route->get(p);
            if (d < best_d || (d == best_d && d < route->get(me.position()) && tile_index(p) < tile_index(best))) { best = p; best_d = d; }
        }
        if (best == me.position()) return false;
        if (me.move_absolute(best)) {
            if (lh_changed_ && lh_cause_kind_ == 3 && !lh_useful_logged_) {
                telemetry_.event("observation", "ward_changed_route_step", integer("emptyGroup", groups_[lh_cause_].id)
                    + "," + integer("actualNextDestination", groups_[target_].id) + "," + integer("actualTile", tile_index(me.position())), true);
            }
            return true;
        }
        excluded |= blocked(); excluded.set(best);
    }
    return false;
}
void core_shop() {}
void core_observe() {
#if LIGHTHOUSE_WARD_ROUTES
    lh_menu_visible_.reset();
#endif
    lh_routes_ready_ = false; lh_same_turn_rerank_ = false; lh_channels_clear_ = false;
    if (!cambc::self().is_alive()) return;
    const unsigned cooldown = static_cast<unsigned>(std::max(0, cambc::self().ward_cooldown()));
    if (turn_ == 1) telemetry_.event("observation", "ward_settings", integer("activeLimit", cambc::constants::active_wards)
        + "," + integer("cooldown", cooldown) + "," + integer("configuredCooldown", cambc::constants::ward_cooldown)
        + "," + integer("lifetime", cambc::constants::ward_lifetime)
        + "," + number("placementRadius", cambc::constants::ward_placement_radius) + "," + number("visionRadius", cambc::constants::ward_vision_radius));
    if (lh_placed_ && lh_last_cooldown_ > 0 && cooldown == 0)
        telemetry_.event("observation", "ward_recharged", integer("configuredCooldown", cambc::constants::ward_cooldown), true);
    lh_last_cooldown_ = cooldown;
    if (lh_placed_ && cambc::fuel_remaining() >= 45000) {
        const auto visible = cambc::vision::visible_tiles();
        const auto attributable = lh_ward_mask_ & visible & ~cambc::self().visible_tiles_from(cambc::self().position());
        if (!((groups_[lh_a_].spawns | groups_[lh_b_].spawns) & attributable).is_empty()) {
            const auto camps = cambc::vision::camps();
            if (!(groups_[lh_a_].spawns & attributable).is_empty()) lh_observation(lh_a_, lh_fact_a_, attributable, lh_fact(lh_a_, visible, camps));
            if (!(groups_[lh_b_].spawns & attributable).is_empty()) lh_observation(lh_b_, lh_fact_b_, attributable, lh_fact(lh_b_, visible, camps));
        }
    }
    if (lh_changed_destination_ >= 0 && !lh_useful_logged_ && cambc::self().gem_count() > previous_bag_) {
        const auto& g = groups_[lh_changed_destination_];
        if (g.spawns.is_set(cambc::self().position()) && seen(g) && std::holds_alternative<cambc::map::camp_group_respawning>(g.group.state())) {
            lh_useful_logged_ = true; telemetry_.ward_count(5);
            telemetry_.event("observation", "ward_destination_produced", group_field(g.id) + "," + integer("gemGain", cambc::self().gem_count() - previous_bag_), true);
        }
    }
    if constexpr (profile::probe_mode) if ((profile::probe_script ? is_actor(cambc::champion_type::tavi) : is_actor(cambc::champion_type::varg))
        && !lh_probe_done_ && !lh_probe_timeout_ && cambc::round_number() > 240) {
        lh_probe_timeout_ = true; lh_probe_phase_ = 3; telemetry_.event("not_exercised", "legal_setup_cap_without_release_predicate", "", true);
    }
}
bool core_bank_request(int nearest) { if constexpr (profile::probe_mode) return false; return base_bank_request(nearest); }
bool hoard_bank() { return false; }
bool core_group_allowed(int index, float) {
    if constexpr (profile::probe_mode) return groups_[index].id == profile::probe_group || groups_[index].id == profile::probe_group_second;
    return true;
}
bool core_partial_allowed() const { return true; }
bool core_retreat(const std::vector<champion>&) const { return false; }
void core_recovery() { lh_routes_ready_ = false; }
bool core_departing(int) const { return false; }
bool lh_script(const std::vector<camp>& camps) {
    auto& me = cambc::self();
    if (!is_actor(cambc::champion_type::tavi) || lh_probe_phase_ == 3) return true;
    const int index = find_group(profile::probe_group); if (index < 0) return true; auto& g = groups_[index];
    if (lh_probe_phase_ == 0) {
        if (me.position() != tile_position(profile::probe_firing)) { ordinary_move(around(tile_position(profile::probe_firing), 0)); return true; }
        if (!seen(g)) return true;
        lh_probe_capture_gold_ = me.gold_count(); lh_probe_phase_ = 1;
        telemetry_.event("observation", "script_firing_reached", group_field(g.id) + "," + integer("tile", tile_index(me.position())));
    }
    if (lh_probe_phase_ == 1) {
        if (me.gold_count() > lh_probe_capture_gold_ && seen(g) && std::holds_alternative<cambc::map::camp_group_respawning>(g.group.state())) {
            lh_probe_phase_ = 2; telemetry_.event("observation", "script_capture_observed", group_field(g.id), true);
        } else if (reward(g)) { ordinary_move(g.spawns); return true; }
        else {
            std::optional<camp> target;
            for (const auto& m : camps) if (g.spawns.is_set(m.spawn_position()) && m.is_visible() && m.is_alive()) { target = m; break; }
            if (target) {
                const auto ability = me.abilities()[1];
                if (ability.is_ready() && ability.can_target_camps() && ability.valid_tiles().is_set(target->position())
                    && distance(me.position(), target->position()) > target->attack_range()) ability.use(*target);
            }
            return true;
        }
    }
    if (lh_probe_phase_ == 2) {
        if (me.position() != tile_position(profile::probe_marker)) ordinary_move(around(tile_position(profile::probe_marker), 0));
        else { lh_probe_phase_ = 3; lh_probe_done_ = true; telemetry_.event("observation", "script_marker_reached", integer("tile", tile_index(me.position())), true); }
    }
    return true;
}
bool core_turn(const std::vector<camp>& camps, const std::vector<champion>& enemies, const std::vector<champion>&) {
    if constexpr (!profile::probe_mode) return false;
    if constexpr (profile::probe_script) return lh_script(camps);
    auto& me = cambc::self();
    if (!is_actor(cambc::champion_type::varg) || lh_probe_phase_ == 3) return true;
    if (lh_probe_phase_ == 0) {
        if (me.position() != tile_position(profile::probe_waypoint)) { ordinary_move(around(tile_position(profile::probe_waypoint), 0)); return true; }
        lh_probe_phase_ = 1; target_ = -1; telemetry_.event("observation", "probe_waypoint_reached", integer("tile", tile_index(me.position())));
    }
    if (lh_probe_phase_ == 1) {
        bool marker = false;
        for (const auto& enemy : enemies) if (enemy.champion_name() == cambc::champion_type::tavi && enemy.position() == tile_position(profile::probe_marker)
            && distance(enemy.position(), me.position()) >= 5) marker = true;
        if (!marker) return true;
        const int a = find_group(profile::probe_group);
        if (a < 0 || seen(groups_[a])) { lh_probe_phase_ = 3; telemetry_.event("not_exercised", "probe_A_already_visible", "", true); return true; }
        const auto pair = lh_pair(choices(camps, cambc::vision::champions(team::ally)));
        if (!choices_complete_) return true;
        if (pair.size() != 2 || pair[0].index != a || !lighthouse_policy::fork(pair[0].score, pair[1].score, true)) {
            lh_probe_phase_ = 3; telemetry_.event("not_exercised", "probe_scores_do_not_form_declared_fork", "", true); return true;
        }
        lh_probe_phase_ = 2; telemetry_.event("observation", "probe_visible_marker", integer("marker", profile::probe_marker) + ",\"AHidden\":true");
    }
    return false;
}
