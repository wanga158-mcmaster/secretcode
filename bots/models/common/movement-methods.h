// Included inside Bot; keep all ordinary move guards in one executor.
bool macro_move(bitboard goals, const bitboard* danger) {
    auto& me = cambc::self();
#if PLAN4_MOVEMENT_RECEIPT
    auto* receipt=movement_receipt_row();
    if(receipt) { ++receipt->calls;receipt->stage=1;receipt->initialized=movement_initialized_;receipt->generation_before=movement_.cache_generation(); }
#endif
    movement_pending_ = false;
    if (!me.can_move() || goals.is_set(me.position())) return false;
#if PLAN4_MOVEMENT_RECEIPT
    const auto admission_fuel=cambc::fuel_remaining();
    if(receipt)receipt->admission_fuel=unsigned(admission_fuel);
    if (admission_fuel < 45000) { if(receipt)receipt->stage=2;movement_pending_ = true; return false; }
    if (!movement_initialized_) {
        const auto cold_fuel=cambc::fuel_remaining();
        if(receipt)receipt->cold_fuel=unsigned(cold_fuel);
        if(cold_fuel < 65000) { if(receipt)receipt->stage=3;movement_pending_ = true;return false; }
    }
#else
    if (cambc::fuel_remaining() < 45000) { movement_pending_ = true; return false; }
    if (!movement_initialized_ && cambc::fuel_remaining() < 65000) { movement_pending_ = true; return false; }
#endif
    const unsigned fuel_before = static_cast<unsigned>(cambc::fuel_remaining());
    const auto movement_round = cambc::round_number();
    navigation_.seal();
    auto walls = blocked();
    if (danger) walls |= *danger;
    const auto origin = me.position();
    walls.set(origin, false);
    goals &= ~walls;
    if (goals.is_empty()) return false;
    const int width = cambc::map::width(), height = cambc::map::height();
    if (!movement_initialized_) {
        std::vector<unsigned char> topology(width * height, 0);
        for (const auto p : terrain_.positions()) topology[p.y * width + p.x] = 1;
        movement_.initialize(width, height, std::move(topology));
        movement_initialized_ = true;
    }
#if PLAN4_FRESH_DYNAMIC_GRID
    auto fresh_lease=fresh_dynamic_grid_.acquire({width,height,int(static_cast<cambc::handle_champion_t>(me)),1,fresh_grid_map_epoch_});
    std::vector<unsigned char> dense_blocks;
    if(fresh_lease) fresh_lease.refill([&] { return (walls & ~terrain_).positions(); });
    else {
        // Nested/unsupported bindings retain the original local dense path.
        dense_blocks.assign(width*height,0);
        for(const auto p:(walls & ~terrain_).positions()) dense_blocks[p.y*width+p.x]=1;
    }
#else
    std::vector<unsigned char> current_blocks(width * height, 0);
    // PersistentMovement rejects its immutable terrain before this fresh-mask
    // callback. Keep full walls for goals; repack only additional blockers.
    for (const auto p : (walls & ~terrain_).positions()) current_blocks[p.y * width + p.x] = 1;
#endif
    std::vector<plan4::Point> destinations;
    for (const auto p : goals.positions()) destinations.push_back({p.x, p.y});
    if (cambc::round_number() != movement_round) return false;
#if PLAN4_FRESH_DYNAMIC_GRID
    if(fresh_lease) fresh_lease.publish();
    const auto& current_blocks=fresh_lease?fresh_lease.view():dense_blocks;
#endif
#ifdef PLAN4_SPATIAL_EVIDENCE
    plan4::runtime::SpatialFrame movement_context;
    movement_context.clock = {int(movement_round),{int(static_cast<cambc::handle_champion_t>(me))}};
    movement_context.topology = navigation_.topology(); movement_context.travel = navigation_.lookup();
    movement_context.route_prefix = plan4::PublicNavigation::route_prefix; movement_context.route_context = &navigation_;
    // This hook supplies immutable geometry and the current movement clock.
    // Fresh goals already passed the executor mask; no stale occupancy proof.
    runtime_model_.observe_movement(movement_context);
#endif
    if (runtime_model_.wait_for_movement({int(movement_round),{int(static_cast<cambc::handle_champion_t>(me))}},
            {origin.x,origin.y},destinations.data(),int(destinations.size()))) {
        movement_pending_ = true; return false;
    }
    struct Fresh { const std::vector<unsigned char>& cells; int width; } fresh{current_blocks, width};
    auto is_blocked = [](plan4::Point p, const void* context) {
        const auto& f = *static_cast<const Fresh*>(context);
        return f.cells[p.y * f.width + p.x] != 0;
    };
    const unsigned used = unsigned(physical_vertices_ + model_vertices_ + navigation_.vertex_visits());
#if PLAN4_MOVEMENT_RECEIPT
    if(receipt) {
        receipt->stage=4;receipt->used=used;receipt->remaining=256u-std::min(256u,used);
        receipt->goals=int(destinations.size());receipt->goal_prefix=int(std::min(std::size_t(256),destinations.size()));receipt->goal_digest=2166136261u;
        for(int n=0;n<receipt->goal_prefix;++n) for(unsigned value:{unsigned(destinations[n].x),unsigned(destinations[n].y)}) receipt->goal_digest=(receipt->goal_digest^value)*16777619u;
    }
#endif
    movement_.begin_turn(used, 256);
    const unsigned prepared_fuel = static_cast<unsigned>(cambc::fuel_remaining());
    plan4::MovementStep step;
    plan4::detour::Route route_proof;
    plan4::runtime::ExecutionRepairFrame repair_frame;
    bool following_route=false,new_route=false,result_sent=false;
    if constexpr (plan4::runtime::uses_alternate_routes<decltype(runtime_model_)>) {
        plan4::Point next;int remaining=0;
        if (macro_active_ && alternate_route_.next(decision_sequence_,
            int(static_cast<cambc::handle_champion_t>(me)),int(movement_round),{origin.x,origin.y},destinations,next,remaining)) {
            if (!walls.is_set({next.x,next.y})) {
                step={plan4::MovementStatus::ready,next,0,remaining};following_route=true;
            } else alternate_route_.reset();
        }
    }
    if (!following_route) {
        step=movement_.next_step({origin.x,origin.y},destinations,is_blocked,&fresh,256,{movement_budget,&movement_floor_});
        physical_vertices_+=int(step.visits);
        if (macro_try_detour(step,destinations,origin,int(movement_round),danger,route_proof,repair_frame)) {
            plan4::Point next;int remaining=0;
            if (alternate_route_.next(decision_sequence_,repair_frame.clock.own.id,int(movement_round),
                {origin.x,origin.y},destinations,next,remaining)) {
                step.status=plan4::MovementStatus::ready;step.next=next;step.remaining_distance=remaining;
                new_route=following_route=true;
#ifdef PLAN4_ONLINE_EVIDENCE
                production_cancel(plan4::runtime::PhaseCensor::interrupted);
#endif
                plan4::LogRow("repair",PLAN4_REFERENCE_VERSION).field("actor",repair_frame.clock.own.id)
                    .field("round",movement_round).field("job",decision_sequence_)
                    .field("action",int(plan4::runtime::ExecutionRepairAction::alternate_path))
                    .field("steps",route_proof.count).field("visits",route_proof.visits).emit();
            }
        }
    }
    const unsigned generation = movement_.cache_generation();
#if PLAN4_MOVEMENT_RECEIPT
    if(receipt) { receipt->stage=5;receipt->prepared_fuel=prepared_fuel;receipt->status=int(step.status);receipt->visits=step.visits;receipt->generation_after=generation; }
#endif
    movement_pending_ = step.status == plan4::MovementStatus::in_progress;
    const auto finish = [&](bool moved) {
        if (!moved && following_route) {
            alternate_route_.reset();
            if (new_route && !result_sent && cambc::round_number()==movement_round)
                plan4::runtime::alternate_route_result(runtime_model_,repair_frame,false,{origin.x,origin.y});
        }
        // Optional diagnostics must not consume a ready step's execution
        // reserve before its fresh permission/fuel checks and actual action.
        if (cambc::round_number() == movement_round) macro_phase("movement", fuel_before, &step, moved, prepared_fuel, generation);
        return moved;
    };
    if (cambc::round_number() != movement_round) { movement_pending_ = true; return finish(false); }
    if (step.status != plan4::MovementStatus::ready || cambc::fuel_remaining() < 30000) return finish(false);
    const vec2 destination{step.next.x, step.next.y};
    if (blocked().is_set(destination) || !me.can_move()) return finish(false);
#ifdef PLAN4_ONLINE_EVIDENCE
    if (!new_route && production_requested_farm_group_>=0)
        production_begin_travel_mask(goals,step.remaining_distance,production_requested_farm_group_);
    if (cambc::round_number()!=movement_round || cambc::fuel_remaining()<30000) return finish(false);
#endif
    const bool moved = me.move_absolute(destination);
    if (following_route) {
        const auto actual=me.position();
        const bool same_round=cambc::round_number()==movement_round;
        alternate_route_.commit(moved && same_round,{actual.x,actual.y});
        if (new_route && same_round) {
            const bool resumed=plan4::runtime::alternate_route_result(runtime_model_,repair_frame,moved,{actual.x,actual.y});
            result_sent=true;
            if (!resumed) alternate_route_.reset();
#ifdef PLAN4_ONLINE_EVIDENCE
            if (resumed && route_proof.count>1)
                production_begin_travel_mask(goals,route_proof.count-1,production_requested_farm_group_);
#endif
        }
    }
#ifdef PLAN4_ONLINE_EVIDENCE
    production_move_result(destination,moved,movement_round);
#endif
    movement_.notify_move_result({origin.x, origin.y}, step.next, moved);
    return finish(moved);
}

