// Included inside Bot. Search proposals never perform engine actions.
bool macro_try_detour(const plan4::MovementStep& blocked_step,
    std::span<const plan4::Point> goals, vec2 origin, int round,
    const bitboard* danger, plan4::detour::Route& route,
    plan4::runtime::ExecutionRepairFrame& frame) {
    if constexpr (!plan4::runtime::uses_alternate_routes<decltype(runtime_model_)>) return false;
    if (!macro_active_ || !information_storage_ || !evaluation_storage_ ||
        blocked_step.status!=plan4::MovementStatus::blocked || !blocked_step.has_obstruction ||
        cambc::fuel_remaining()<optional_floor_+8000) return false;
    const auto& info=*information_storage_;
    const int actor=int(static_cast<cambc::handle_champion_t>(cambc::self()));
    if (info.round!=round || info.own.id!=actor || info.own.position!=plan4::Point{origin.x,origin.y}) return false;
    const int group=macro_second_?macro_job_.second_group:macro_job_.group;
    const bool farm=macro_job_.kind==plan4::JobKind::farm && group>=0 && group<info.group_count &&
        production_requested_farm_group_==info.groups[group].id;
    const bool bank=macro_job_.kind==plan4::JobKind::bank && production_requested_bank_==macro_job_.altar;
    if (!farm && !bank) return false;
    const auto topology=navigation_.topology();
    if (!topology.complete || !topology.walkable) return false;
    struct Guard { int round;unsigned floor; } guard{round,std::max(movement_floor_,optional_floor_+4000)};
    const plan4::WorkBudget work{[](const void* raw) {
        const auto& g=*static_cast<const Guard*>(raw);
        return int(cambc::round_number())==g.round && cambc::fuel_remaining()>=g.floor;
    },&guard};
    if (!work.can_refine(work.context)) return false;
    const auto visible=cambc::vision::visible_tiles();
    if (!work.can_refine(work.context)) return false;
    auto occupied=cambc::vision::champion_tiles();
    if (!work.can_refine(work.context)) return false;
    occupied|=cambc::vision::camp_tiles();
    if (!work.can_refine(work.context)) return false;
    occupied|=cambc::vision::teleport_tiles();
    if (!work.can_refine(work.context)) return false;
    struct Known {
        const bitboard& visible;const bitboard& occupied;const bitboard* danger;
        plan4::runtime::PublicTopology topology;
    } known{visible,occupied,danger,topology};
    plan4::detour::Frame observation;
    observation.actor=actor;observation.round=round;observation.width=topology.width;observation.height=topology.height;
    observation.origin={origin.x,origin.y};observation.obstruction=blocked_step.obstruction;observation.goals=goals;
    observation.walkable=[](plan4::Point p,const void* raw) {
        const auto& k=*static_cast<const Known*>(raw);
        return k.topology.walkable(p,k.topology.context) && (!k.danger || !k.danger->is_set({p.x,p.y}));
    };
    observation.topology_context=&known;
    observation.occupancy=[](plan4::Point p,const void* raw) {
        const auto& k=*static_cast<const Known*>(raw);
        if (!k.visible.is_set({p.x,p.y})) return plan4::detour::Occupancy::unknown;
        return k.occupied.is_set({p.x,p.y})?plan4::detour::Occupancy::occupied:plan4::detour::Occupancy::free;
    };
    observation.occupancy_context=&known;observation.work=work;
    const int remaining=std::max(0,256-navigation_.vertex_visits()-model_vertices_-physical_vertices_);
    route=plan4::detour::plan(observation,unsigned(std::min(64,remaining)));
    physical_vertices_+=int(route.visits);
    if (!route.proven || !work.can_refine(work.context)) return false;
    frame.clock={round,{actor}};frame.information=&info;frame.fixed_menu=&evaluation_storage_->menu;
    frame.selected_job=macro_job_;frame.job_epoch=decision_sequence_;frame.remaining_group=farm?group:-1;
    frame.same_epoch=true;frame.protected_progress=info.own.channel!=plan4::Channel::none || info.in_progress_group_id>=0;
    frame.phase=plan4::runtime::ProductionPhase::travel;frame.phase_known=true;
    frame.alternate_route=&route;frame.ordinary_goal_bound=true;
    const auto repair=plan4::runtime::execution_repair(runtime_model_,frame);
    if (repair.action!=plan4::runtime::ExecutionRepairAction::alternate_path || !repair.supported ||
        !work.can_refine(work.context)) return false;
    return alternate_route_.begin(decision_sequence_,route);
}
