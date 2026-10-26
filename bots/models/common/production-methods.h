// Included inside Bot for every instrumented active/control arm.
plan4::runtime::PhaseTracker production_tracker_;
plan4::runtime::ProductionEndpointEvidence production_evidence_;
std::uint64_t production_sequence_ = 0;
int production_mode_ = 0;
plan4::Point production_destination_{};
int production_radius_ = 0;
unsigned production_bank_bag_ = 0;
bitboard production_goals_;
bool production_goal_mask_ = false;
bool production_endpoint_missed_ = false;
int production_group_index_ = -1;
bool production_contract_restore_ = false;

bool production_contract_restoring() const {
    const auto* pending=production_tracker_.pending_key();
    return production_contract_restore_ && pending && pending->phase==plan4::runtime::ProductionPhase::restore &&
        pending->job_epoch==decision_sequence_;
}

bool production_budget(int round) const {
    return cambc::fuel_remaining() >= 60000 && int(cambc::round_number()) == round;
}
bool production_observation(plan4::runtime::ProductionObservation& out, int group_index = -1) const {
    const int round=int(cambc::round_number());
    if(!production_budget(round))return false;
    const auto& me=cambc::self();out={};
    out.clock={round,{int(static_cast<cambc::handle_champion_t>(me))}};
    out.own_provenance={round,plan4::Source::own_state,plan4::Status::exact_now};
    out.alive=me.is_alive();
    if(!production_budget(round))return false;
    if(!out.alive)return true;
    const auto point=me.position();
    if(!production_budget(round))return false;
    out.on_own_spawn=cambc::map::spawn_region(team::ally).is_set(point);
    out.at_requested_goal=production_goal_mask_ && production_goals_.is_set(point);
    out.health=me.health();
    if(!production_budget(round))return false;
    out.maximum_health=me.max_health();
    if(!production_budget(round))return false;
    out.channel_active=me.is_recalling() || me.is_submitting_gems() || me.is_teleporting();
    if(!production_budget(round))return false;
    if(group_index>=0) {
        if(group_index>=int(groups_.size()))return false;
        const auto& entry=groups_[group_index];
        out.group_id=int(static_cast<cambc::handle_camp_group_t>(entry.group));
        // One requested physical group only. Never traverse an unbounded
        // metadata prefix or query a hidden group's dynamic state.
        if(entry.group.camps().size()>32)return false;
        const auto visible=cambc::vision::visible_tiles();
        if(!production_budget(round))return false;
        out.group_visible=!(entry.spawns & visible).is_empty();
        if(!production_budget(round))return false;
        if(out.group_visible) {
            const auto state=entry.group.state();
            if(!production_budget(round))return false;
            out.group_lifecycle=std::holds_alternative<cambc::map::camp_group_alive>(state)?plan4::Lifecycle::living:
                std::holds_alternative<cambc::map::camp_group_reward_available>(state)?plan4::Lifecycle::cleared:plan4::Lifecycle::respawning;
            out.group_provenance={round,plan4::Source::personal_vision,plan4::Status::exact_now};
        }
    }
    return production_budget(round);
}

plan4::runtime::PhaseClock production_clock() const {
    const auto& me = cambc::self();
    const int round = int(cambc::round_number());
    return {int(static_cast<cambc::handle_champion_t>(me)), round, production_mode_, kit_epoch_,
        kit_observed_round_ == round, me.is_alive(), recovering_ && !(production_contract_restore_ && production_mode_==3), true};
}
void production_transition(const plan4::runtime::PhaseTransition& transition) {
    transition.forward_to(runtime_model_);
    if (transition.has_start) {
        const auto& p = transition.start;
        plan4::LogRow("production-start", PLAN4_REFERENCE_VERSION).field("actor", p.actor).field("round", p.start_round)
            .field("job", p.job_epoch).field("sequence", p.phase_sequence).field("phase", unsigned(p.phase))
            .field("group",p.group_id)
            .field("nominal", p.nominal_duration).field("mode", p.mode).field("kitEpoch", p.stat_epoch).emit();
    }
    if (transition.has_receipt) {
        const auto& p = transition.receipt;
        plan4::LogRow("production-end", PLAN4_REFERENCE_VERSION).field("actor", p.actor).field("round", p.end_round)
            .field("job", p.job_epoch).field("sequence", p.phase_sequence).field("phase", unsigned(p.phase))
            .field("group",p.group_id)
            .field("start", p.start_round).field("nominal", p.nominal_duration).field("mode", p.executed_mode)
            .field("kitEpoch", p.stat_epoch).field("completed", p.completed).field("endpoint", p.endpoint_observed)
            .field("censor", unsigned(p.censor)).field("cause", unsigned(transition.cause))
            .field("wait", p.legal_wait_rounds).field("waitBound", p.waiting_bound).emit();
        production_evidence_.reset();production_goal_mask_=false;production_group_index_=-1;production_endpoint_missed_=false;
        production_contract_restore_=false;
    }
}
void production_observe_kit(bool force = false) {
    const int round = int(cambc::round_number());
    if ((!force && kit_observed_round_ == round) || cambc::fuel_remaining() < 65000) return;
    const auto& me = cambc::self();
    std::uint64_t signature = 1469598103934665603ull;
    auto mix = [&](unsigned n) { signature = (signature ^ n) * 1099511628211ull; };
    auto floating = [&](float value) { unsigned bits; static_assert(sizeof(bits) == sizeof(value));
        __builtin_memcpy(&bits, &value, sizeof(bits)); mix(bits); };
    const int champion = int(me.champion_name()), attack_range = int(me.attack_range());
    const float strength = me.strength(), focus = me.focus();
    mix(unsigned(champion)); mix(unsigned(attack_range));
    floating(strength); floating(focus); floating(me.max_health());
    mix(monster_shards_); mix(jungle_shards_);
    if (int(cambc::round_number()) != round) return;
    if (!kit_seen_ || signature != kit_signature_) { ++kit_epoch_; kit_signature_ = signature; kit_seen_ = true; }
    kit_observed_round_ = round;
    runtime_model_.observe_kit(signature, kit_epoch_);
    runtime_model_.observe_combat({{round, {int(static_cast<cambc::handle_champion_t>(me))}}, champion, attack_range,
        strength, focus, monster_shards_, jungle_shards_, signature, kit_epoch_, true});
    // Kit observation does not prove an endpoint/group was observed this turn.
    // Only production_poll/action receipts advance that continuity clock.
}
void production_cancel(plan4::runtime::PhaseCensor reason) {
    if (production_tracker_.pending()) {
        const auto clock=production_clock();
        production_transition(clock.alive?production_tracker_.censor_pending(reason,clock):production_tracker_.observe_death(clock));
    }
}
bool production_begin(plan4::runtime::ProductionPhase phase, int nominal, int mode, int group_id = -1,
                      bool contract_restore = false) {
    if (nominal <= 0 || kit_observed_round_ != int(cambc::round_number())) return false;
    if (production_tracker_.pending()) production_cancel(plan4::runtime::PhaseCensor::mode_change);
    production_mode_ = mode;
    production_contract_restore_=contract_restore && phase==plan4::runtime::ProductionPhase::restore;
    const auto clock = production_clock();
    const plan4::runtime::PhaseStart request{phase, clock.actor, decision_sequence_, ++production_sequence_,
        clock.round, nominal, mode, kit_epoch_, clock.emergency, group_id};
    production_transition(production_tracker_.begin_phase(request, clock, plan4::max_horizon));
    return production_tracker_.pending();
}
void production_begin_travel(plan4::Point destination, int radius, int nominal) {
    if (const auto* pending = production_tracker_.pending_key()) {
        if (!production_goal_mask_ && pending->phase == plan4::runtime::ProductionPhase::travel && production_destination_ == destination
            && production_radius_ == radius && pending->job_epoch == decision_sequence_) return;
    }
    if (production_begin(plan4::runtime::ProductionPhase::travel, nominal, 0)) {
        production_destination_ = destination; production_radius_ = radius;
    } else production_cancel(plan4::runtime::PhaseCensor::ambiguous_endpoint);
}
void production_begin_travel_mask(const bitboard& goals,int nominal,int group_id=-1) {
    if(const auto* pending=production_tracker_.pending_key()) {
        if(pending->job_epoch==decision_sequence_ && pending->group_id==group_id) {
            if(pending->phase==plan4::runtime::ProductionPhase::clear && production_evidence_.has_targeted_action())return;
            if(pending->phase==plan4::runtime::ProductionPhase::travel && production_goal_mask_ && goals==production_goals_)return;
        }
    }
    if(goals.is_empty() || !production_begin(plan4::runtime::ProductionPhase::travel,nominal,0,group_id)) {
        production_cancel(plan4::runtime::PhaseCensor::ambiguous_endpoint);return;
    }
    production_goals_=goals;production_goal_mask_=true;
    plan4::runtime::ProductionObservation observation;
    if(!production_observation(observation) || !production_evidence_.begin(*production_tracker_.pending_key(),observation))
        production_cancel(plan4::runtime::PhaseCensor::ambiguous_endpoint);
}
bool production_begin_clear(const plan4::Information& info,int group_index) {
    if(group_index<0 || group_index>=info.group_count || group_index>=int(groups_.size()) ||
        info.round!=int(cambc::round_number()) || info.own.id!=int(static_cast<cambc::handle_champion_t>(cambc::self())))return false;
    const auto& group=info.groups[group_index];
    const int physical=int(static_cast<cambc::handle_camp_group_t>(groups_[group_index].group));
    if(group.id!=physical || !group.visible || !group.all_spawns_visible || group.lifecycle!=plan4::Lifecycle::living ||
        group.clear_rounds<=0 || group.health_provenance.observed_round!=info.round ||
        group.health_provenance.source!=plan4::Source::personal_vision || group.health_provenance.status!=plan4::Status::exact_now)return false;
    if(const auto* pending=production_tracker_.pending_key()) {
        if(pending->phase==plan4::runtime::ProductionPhase::clear && pending->group_id==physical && pending->job_epoch==decision_sequence_)return true;
        if(pending->phase==plan4::runtime::ProductionPhase::travel)production_poll(true);
    }
    plan4::runtime::ProductionObservation observation;
    if(!production_observation(observation,group_index) || !observation.group_visible || observation.group_lifecycle!=plan4::Lifecycle::living)return false;
    if(!production_begin(plan4::runtime::ProductionPhase::clear,group.clear_rounds,2,physical))return false;
    production_group_index_=group_index;
    if(!production_evidence_.begin(*production_tracker_.pending_key(),observation)) {
        production_cancel(plan4::runtime::PhaseCensor::ambiguous_endpoint);return false;
    }
    return true;
}
bool production_clear_target(const camp& target) const {
    const auto* pending=production_tracker_.pending_key();
    return pending && pending->phase==plan4::runtime::ProductionPhase::clear && pending->job_epoch==decision_sequence_ &&
        production_group_index_>=0 && production_group_index_<int(groups_.size()) &&
        pending->group_id==int(static_cast<cambc::handle_camp_group_t>(groups_[production_group_index_].group)) &&
        groups_[production_group_index_].spawns.is_set(target.spawn_position());
}
bool production_prepare_clear_target(const camp& target) {
    if(!macro_active_ || macro_job_.kind!=plan4::JobKind::farm)return false;
    const int index=macro_second_?macro_job_.second_group:macro_job_.group;
    if(index<0 || index>=int(groups_.size()) || !groups_[index].spawns.is_set(target.spawn_position()))return false;
    // The caller is about to request an actual targeted action. Even when a
    // missing HP/nominal observation prevents CLEAR admission, combat cannot
    // be relabelled as legal waiting in an otherwise pending travel phase.
    if(const auto* pending=production_tracker_.pending_key()) {
        if(pending->phase==plan4::runtime::ProductionPhase::travel) {
            production_poll(true);
            if(production_tracker_.pending_key() && production_tracker_.pending_key()->phase==plan4::runtime::ProductionPhase::travel)
                production_cancel(plan4::runtime::PhaseCensor::mode_change);
        }
    }
    return production_begin_clear(*information_storage_,index);
}
void production_clear_action(bool targeted_action_executed) {
    if(!production_tracker_.pending())return;
    if(!targeted_action_executed) {production_cancel(plan4::runtime::PhaseCensor::interrupted);return;}
    production_evidence_.targeted_action(true,int(cambc::round_number()));
}
bool production_begin_restore(const plan4::Information& info,bool contract_repair = false) {
    if(const auto* pending=production_tracker_.pending_key())
        if(pending->phase==plan4::runtime::ProductionPhase::restore && pending->job_epoch==decision_sequence_)return true;
    const int nominal=plan4::runtime::nominal_restore(info);
    plan4::runtime::ProductionObservation observation;
    if(nominal<=0 || !production_observation(observation) || info.round!=observation.clock.round || info.own.id!=observation.clock.own.id ||
        info.own.health!=observation.health || info.own.maximum_health!=observation.maximum_health)return false;
    if(!production_begin(plan4::runtime::ProductionPhase::restore,nominal,3,-1,contract_repair))return false;
    if(!production_evidence_.begin(*production_tracker_.pending_key(),observation)) {
        production_cancel(plan4::runtime::PhaseCensor::ambiguous_endpoint);return false;
    }
    return true;
}
void production_begin_bank(unsigned bag) {
    production_bank_bag_ = bag;
    plan4::Settings settings;
    settings.tribute_multiplier = cambc::constants::submit_gem_duration_multiplier;
    settings.tribute_base = cambc::constants::submit_gem_base_duration;
    settings.tribute_minimum = cambc::constants::submit_gem_minimum_duration;
    production_begin(plan4::runtime::ProductionPhase::bank, plan4::tribute_ticks(bag, settings), 1);
}
void production_move_result(const cambc::vec2& destination,bool moved,unsigned move_round) {
    const auto* pending=production_tracker_.pending_key();
    if(!moved || !pending || pending->phase!=plan4::runtime::ProductionPhase::travel || pending->job_epoch!=decision_sequence_)return;
    const bool arrived=production_goal_mask_?production_goals_.is_set(destination):
        std::max(std::abs(destination.x-production_destination_.x),std::abs(destination.y-production_destination_.y))<=production_radius_;
    if(!arrived)return;
    if(int(cambc::round_number())!=int(move_round) || cambc::fuel_remaining()<35000 || kit_observed_round_!=int(move_round)) {
        production_endpoint_missed_=true;return;
    }
    // Successful own ordinary movement is a fresh action receipt for its exact
    // destination, not a guessed position at a later owner START. No HP/vision
    // resampling is needed to establish this travel endpoint.
    const auto key=*pending;
    const plan4::runtime::PhaseClock clock{key.actor,int(move_round),production_mode_,kit_epoch_,true,true,recovering_,true};
    production_transition(production_tracker_.finish({key,int(move_round),plan4::runtime::EndpointKind::travel_arrival,
        {int(move_round),plan4::Source::own_state,plan4::Status::exact_now},false},clock));
}
void production_poll(bool at_end = false) {
    const auto* pending = production_tracker_.pending_key();
    if (!pending || cambc::fuel_remaining() < 60000) return;
    auto clock = production_clock();
    if (!clock.fresh) return;
    if(production_endpoint_missed_) {production_cancel(plan4::runtime::PhaseCensor::ambiguous_endpoint);return;}
    if (pending->job_epoch != decision_sequence_) { production_cancel(plan4::runtime::PhaseCensor::mode_change); return; }
    auto& me = cambc::self();
    plan4::runtime::ProductionObservation observation;
    const bool physical_evidence=production_goal_mask_ || pending->phase==plan4::runtime::ProductionPhase::clear || pending->phase==plan4::runtime::ProductionPhase::restore;
    if(physical_evidence) {
        if(!production_observation(observation,pending->phase==plan4::runtime::ProductionPhase::clear?production_group_index_:-1))return;
        if(pending->phase==plan4::runtime::ProductionPhase::clear)clock.premises_visible=observation.group_visible;
    }
    production_transition(production_tracker_.observe_turn(clock));
    pending = production_tracker_.pending_key();
    if (!pending || clock.round < pending->start_round) return;
    plan4::runtime::EndpointKind endpoint = plan4::runtime::EndpointKind::none;
    plan4::Provenance provenance{clock.round,plan4::Source::own_state,plan4::Status::exact_now};
    if(physical_evidence) {
        const auto evidence=production_evidence_.inspect(*pending,observation,at_end);
        if(evidence.verdict==plan4::runtime::ProductionVerdict::complete) {endpoint=evidence.kind;provenance=evidence.provenance;}
        else if(evidence.verdict!=plan4::runtime::ProductionVerdict::pending) {
            production_cancel(evidence.verdict==plan4::runtime::ProductionVerdict::lost_sight?plan4::runtime::PhaseCensor::lost_sight:
                plan4::runtime::PhaseCensor::ambiguous_endpoint);return;
        }
    } else if (pending->phase == plan4::runtime::ProductionPhase::travel && clock.alive) {
        const auto p = me.position();
        if (std::max(std::abs(p.x - production_destination_.x), std::abs(p.y - production_destination_.y)) <= production_radius_)
            endpoint = plan4::runtime::EndpointKind::travel_arrival;
    } else if (!at_end && pending->phase == plan4::runtime::ProductionPhase::bank && !me.is_submitting_gems()) {
        // No other living-actor operation consumes its whole gem bag. A public
        // team-score rise alone, or a group lifecycle change, is insufficient.
        if (clock.alive && previous_tribute_ && production_bank_bag_ > 0 && me.gem_count() == 0)
            endpoint = plan4::runtime::EndpointKind::own_tribute_resolution;
        else { production_cancel(plan4::runtime::PhaseCensor::ambiguous_endpoint); return; }
    }
    if (endpoint != plan4::runtime::EndpointKind::none) {
        const auto key = *pending;
        production_transition(production_tracker_.finish({key, clock.round, endpoint,
            provenance, false}, clock));
    }
}
