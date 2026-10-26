// Included inside Runtime. A model menu owns no input until an actual primitive
// or a fresh finite channel/capture continuation supplies the receipt.
struct JointControllerState {
    int attempted_epoch{-1};unsigned calls{},selected{},physical{},waits{},fallbacks{},partial{},unsupported{};
    unsigned route_nodes{},last_status{},last_purpose{};std::uint64_t entry_fuel{},after_prepare{},after_select{};
    bool last_owned{},bounded_failure{};
};
static JointControllerState& joint_controller_state(){static constinit JointControllerState value{};return value;}
void joint_diagnostic_packet(std::array<std::uint32_t,80>& packet)const{
    const auto& c=joint_controller_state();const auto& s=joint_contact_state();const auto& e=joint_execution_state();
    packet[20]=0x4a505231u;
    for(std::size_t n=21;n<packet.size();++n)packet[n]=0;
    const std::array<std::uint64_t,42> values{c.calls,c.selected,c.physical,c.waits,c.fallbacks,c.partial,c.unsupported,
        c.route_nodes,c.last_status,c.last_purpose,c.entry_fuel,c.after_prepare,c.after_select,unsigned(s.status),
        s.world.actor_count,s.world.camp_count,s.world.reply_count,s.world.frontier_count,s.candidate_count,
        s.prepared,s.quoted,s.selected,s.partial,s.declared_bounds_overrun,s.unknown_power,s.unsearched_classes,
        e.selections,e.attempts,e.applied,e.refused,e.refreshes,e.unfunded,e.overruns,e.unsupported,e.waits,
        unsigned(e.first_failure),unsigned(e.first_failure_phase),e.first_failure_primitive,
        e.max_validation,e.max_command,e.max_feedback,e.max_failure_refresh};
    for(std::size_t n=0;n<values.size();++n)packet[21+n]=unsigned(values[n]);
    for(std::size_t phase=0;phase<std::min<std::size_t>(s.costs.size(),7);++phase){packet[63+phase]=unsigned(s.costs[phase].max_phase);
        packet[70+phase]=unsigned(s.costs[phase].max_query);}
    packet[77]=unsigned(s.selection.quote.reason);packet[78]=s.selection.quote.operations;
    packet[79]=unsigned(s.selection.quote.valid)|unsigned(s.selection.quote.alive)<<1|
        unsigned(s.selection.quote.complete)<<2|unsigned(s.selection.quote.approximate)<<3;
    if constexpr(cfg::local_observation)joint_observation_diagnostic_packet(packet);
}
void joint_observation_diagnostic_packet(std::array<std::uint32_t,80>& packet)const{
    const auto& o=joint_observation_state();const auto& c=joint_controller_state();
    const auto& s=joint_contact_state();const auto& e=joint_execution_state();
    packet[20]=0x4f423134u;
    for(std::size_t n=21;n<packet.size();++n)packet[n]=0;
    const std::array<std::uint64_t,42> values{o.calls,o.partial,o.ready,o.raw_own,o.raw_enemies,o.raw_allies,
        o.source_count,o.source_overflow,o.receipts,o.group_receipts,o.target_receipts,o.unknown_identities,
        o.stage_progress,o.immutable.next,o.immutable.total,unsigned(o.stage),o.target_handle,o.target_life,o.facts,unsigned(o.epoch),
        s.quoted,s.selected,c.physical,c.selected,e.selections,e.attempts,e.applied,e.refused,e.unfunded,e.overruns,
        unsigned(e.first_failure),unsigned(e.first_failure_phase),e.max_validation,e.max_command,e.max_feedback,e.max_failure_refresh,
        c.entry_fuel,c.after_prepare,c.after_select,c.partial,c.fallbacks,c.route_nodes};
    for(std::size_t n=0;n<values.size();++n)packet[21+n]=unsigned(values[n]);
    for(std::size_t n=0;n<std::min<std::size_t>(o.costs.size(),9);++n)packet[63+n]=unsigned(o.costs[n].maximum);
    packet[72]=unsigned(o.own_receipt_epoch);packet[73]=unsigned(o.own_frame);
    packet[74]=unsigned(o.own_effect);packet[75]=unsigned(o.own_observation);
    packet[76]=unsigned(o.target_receipt_epoch);packet[77]=unsigned(o.target_frame);
    packet[78]=unsigned(o.target_effect);packet[79]=unsigned(o.target_observation);
    if constexpr(cfg::indexed_catalogue)joint_catalogue_diagnostic_packet(packet);
}
struct JointCatalogueQuoteDiagnostic {
    unsigned reason{},flags{},operations{};int epoch{-1};std::uint64_t maximum{};
};
static JointCatalogueQuoteDiagnostic& joint_catalogue_quote_diagnostic(){
    static constinit JointCatalogueQuoteDiagnostic value{};return value;
}
void joint_catalogue_quote_record(const joint_world::Quote& quote,std::uint64_t spent){
    auto& d=joint_catalogue_quote_diagnostic();d.reason=unsigned(quote.reason);d.operations=quote.operations;d.epoch=epoch;
    d.flags=0x80000000u|unsigned(quote.valid)|unsigned(quote.bounded)<<1|unsigned(quote.alive)<<2|
        unsigned(quote.complete)<<3|unsigned(quote.approximate)<<4;d.maximum=std::max(d.maximum,spent);
}
void joint_catalogue_diagnostic_packet(std::array<std::uint32_t,80>& packet)const{
    const auto& i=joint_catalogue_stats();const auto& o=joint_observation_state();const auto& c=joint_controller_state();
    const auto& s=joint_contact_state();const auto& e=joint_execution_state();const auto& q=joint_catalogue_quote_diagnostic();
    packet[20]=0x43493136u;
    const std::array<std::uint64_t,59> values{unsigned(i.stage),i.total,i.baseline_copied,q.reason,q.flags,i.cache_hits,
        i.cache_misses,i.cache_prepares,i.built,i.prepare_calls,i.prepare_deferrals,i.source_queries,i.query_failures,
        i.source_frames,i.frame_failures,unsigned(i.build_state),unsigned(i.failure),unsigned(i.last_query_status),
        unsigned(i.last_epoch),unsigned(i.last_halo),unsigned(i.last_origin.x),unsigned(i.last_origin.y),i.last_local,i.last_overflow,
        q.operations,i.max_allocation,i.max_prepare,i.max_baseline_step,i.max_query,i.max_materialize,i.max_frame,
        unsigned(q.epoch),i.last_materialize,i.last_source_prepare,o.calls,o.ready,o.partial,s.quoted,s.selected,c.physical,
        e.attempts,e.applied,e.unfunded,e.overruns,e.max_validation,e.max_command,e.max_feedback,
        unsigned(e.first_failure),unsigned(e.first_failure_phase),o.group_receipts,o.target_receipts,
        c.entry_fuel,c.after_prepare,c.after_select,o.source_count,q.maximum,unsigned(o.stage),i.max_source_prepare,s.declared_bounds_overrun};
    for(std::size_t n=0;n<values.size();++n)packet[21+n]=unsigned(values[n]);
}
JointExecutionFallbackBudget joint_controller_fallback_budget()const{
    JointExecutionBudget word{};word.declared=true;word.validation_bound=1500;
    word.command_bound=2000;word.feedback_bound=1500;word.failure_refresh_bound=3000;
    word.final_tail=2000;word.max_attempts=1;
    // Rebase and one new quote are distinct unpaid phases after refusal.
    return {true,6000+16000,word,true};
}
JointExecutionBudget joint_controller_budget()const{
    // Experimental caller declarations for a finite <=4-attempt word. These
    // are measured independently; source arithmetic is not a fuel theorem.
    JointExecutionBudget budget{};budget.declared=true;budget.validation_bound=1500;
    budget.command_bound=2000;budget.feedback_bound=1500;budget.failure_refresh_bound=3000;
    budget.success_physical_tail=4000;
    budget.refusal_physical_tail=joint_execution_fallback_reserve(joint_controller_fallback_budget());
    budget.final_tail=2000;budget.max_attempts=4;return budget;
}
std::uint64_t joint_contact_word_tail(unsigned count)const{
    return joint_execution_reserve(joint_controller_budget(),count);
}
bool joint_near_contact()const{
    return !vision::champion_tiles(team::enemy).is_empty();
}
void joint_nominate_path(const Info& me,JointContactRequest& request){
    // A proposal from the already observed adjacent domain. No SDK query or
    // broad path search can consume this family's paid quote/command tail.
    const auto& contact=joint_contact_state();const auto& world=contact.world;
    const auto& owner=world.actors[world.owner];const auto origin=point(me);
    if(!owner.can_move||owner.move_speed<1)return;
    vec2 anchor=request.spawn_known?request.spawn:origin;
    if(exit_trace.valid())anchor=vp(exit_trace.point(0));
    bool found=false;double best_damage=std::numeric_limits<double>::infinity();
    int best_distance=100000,best_separation=-1;vec2 best{};
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){const vec2 p{origin.x+x,origin.y+y};
        if((x==0&&y==0)||!joint_contact_free(p,true))continue;
        double damage=world.aggregate.present?world.aggregate.damage:0;
        for(unsigned n=0;n<world.camp_count;++n){const auto& camp=world.camps[n];
            if(camp.alive&&joint_world::distance(joint_point(p),camp.position)<=camp.range)damage+=camp.damage;}
        int separation=100000;
        for(unsigned n=0;n<world.actor_count;++n){const auto& other=world.actors[n];
            if(other.alive&&other.team!=owner.team&&other.knowledge==joint_world::Knowledge::fresh_champion)
                separation=std::min(separation,joint_world::distance(joint_point(p),other.position));}
        const int distance=dist(p,anchor);
        if(!found||damage<best_damage||(damage==best_damage&&separation>best_separation)||
           (damage==best_damage&&separation==best_separation&&distance<best_distance)){
            found=true;best=p;best_damage=damage;best_distance=distance;best_separation=separation;}
    }
    request.path_count=found?1:0;request.path_purpose=joint_world::Purpose::flee;
    if(found)request.path[0]=best;
}
void joint_enrich_current_bank(){
    auto& contact=joint_contact_state();const auto& owner=contact.world.actors[contact.world.owner];
    if(!owner.gems||contact.status!=JointContactStatus::ready)return;
    const auto origin=joint_vec(owner.position);vec2 altar{};bool nearby=false;
    for(const auto candidate:altars)if(dist(origin,candidate)<=constants::submit_gems_range){altar=candidate;nearby=true;break;}
    if(!nearby)return;
    auto funding=contact.funding;funding.command=std::max(funding.command,joint_contact_word_tail(1));
    const auto build=JointContactFunding::add(contact.budget.query_bound,contact.budget.enumeration_bound);
    if(!funding.family(fuel_remaining(),build))return;
    int code=0;bool unlocked=true;
    if(!joint_contact_query([&]{for(const auto& lock:vision::altar_locks(team::enemy))
        if(lock.position()==altar)unlocked=false;return 0;},code))return;
    detail::check(code);if(!unlocked)return;
    // This fact affects only the new bank nomination; it changes no existing
    // body, hazard or motion premise. Execution queries the lock again.
    contact.request.altar=altar;contact.request.altar_known=contact.request.altar_unlocked=true;
    joint_contact_family(3);
    if(contact.status!=JointContactStatus::overrun&&contact.status!=JointContactStatus::stale&&contact.funding.incumbent)
        joint_contact_finish(JointContactStatus::ready);
}
bool joint_step(const Info& original){
    auto& control=joint_controller_state();
    if(control.attempted_epoch==epoch)return control.last_owned;
    control.attempted_epoch=epoch;control.last_owned=false;control.bounded_failure=false;++control.calls;
    const auto execution=joint_controller_budget();auto reserve=joint_execution_reserve(execution);
    if constexpr(cfg::local_observation)reserve=joint_contact_word_tail(2);
    control.entry_fuel=fuel_remaining();
    // Pay the complete physical word before optional geometry/observations.
    if(!reserve||!fuel(reserve)){++control.partial;return false;}
    auto me=original;JointContactRequest request{};request.horizon=6;
    const auto bag=self().gem_count();
    if constexpr(cfg::local_observation)joint_observation_gems_receipt(bag);
    const auto bank_ticks=std::max(constants::submit_gem_minimum_duration,
        int(std::ceil(double(bag)*constants::submit_gem_duration_multiplier))+constants::submit_gem_base_duration);
    request.horizon=unsigned(std::clamp(bank_ticks,6,8));
    request.no_exogenous_wake=false;request.allow_approximate=true;request.allow_fatal=true;
    request.remaining_refusals=return_refusals?0:1;
    request.capture_group=job;
    const auto spawns=home->positions();if(!spawns.empty()){request.spawn=spawns.front();request.spawn_known=true;}
    // Own spawn is a region in the engine. This representative destination is
    // a forecast only; actual arrival/body feedback controls renewed service.
    const JointContactBudget budget{reserve,2000,6000,16000,true};
    JointContactStatus status;
    if constexpr(cfg::local_observation)status=joint_observation_prepare(me,request,budget);
    else status=joint_contact_prepare(me,request,budget);
    control.after_prepare=fuel_remaining();
    control.last_status=unsigned(status);
    if(status!=JointContactStatus::ready){++control.partial;return false;}
    joint_enrich_current_bank();
    request=joint_contact_state().request;
    unsigned first_geometry_family=joint_world::no_word;
    if constexpr(cfg::local_observation)
        if(joint_contact_state().world.actors[joint_contact_state().world.owner].ranged)first_geometry_family=6;
    joint_contact_enrich_geometry(request,[&](JointContactRequest& proposed){joint_nominate_path(me,proposed);},first_geometry_family);
    auto& prepared=joint_contact_state();
    if(prepared.status==JointContactStatus::ready){
        for(unsigned family=1;family<6;++family)if(!joint_contact_family(family))break;
        if(prepared.status!=JointContactStatus::overrun&&prepared.status!=JointContactStatus::stale&&prepared.funding.incumbent)
            joint_contact_finish(JointContactStatus::ready);
    }
    control.route_nodes=joint_contact_state().request.path_count;
    const auto selection=joint_contact_select();control.after_select=fuel_remaining();
    if(selection.index==std::numeric_limits<std::size_t>::max()){++control.unsupported;return false;}
    ++control.selected;control.last_purpose=unsigned(joint_contact_state().candidates[selection.index].purpose);
    joint_execution_budget(execution);joint_execution_fallback_budget(joint_controller_fallback_budget());
    auto outcome=joint_execute_selection(selection);bool any_effect=outcome.has_actual_effect;
    if(outcome.needs_fallback&&outcome.raw_move_after&&!outcome.restricted_move_after&&!outcome.channel_after){
        const auto repriced=joint_contact_reprice_refusal();
        if(repriced.index!=std::numeric_limits<std::size_t>::max()){
            outcome=joint_execute_refusal_fallback(repriced);any_effect=any_effect||outcome.has_actual_effect;
            if(outcome.fallback_attempted&&!outcome.fallback_handled)control.bounded_failure=true;
        }
    }
    control.last_status=unsigned(outcome.status);
    const bool unfinished=outcome.needs_fallback&&outcome.raw_move_after&&!outcome.restricted_move_after&&!outcome.channel_after;
    control.last_owned=!unfinished&&(outcome.attempted||outcome.has_actual_effect||outcome.finite_wait||outcome.progress);
    if(any_effect)++control.physical;
    if(outcome.finite_wait)++control.waits;
    if(!control.last_owned)++control.unsupported;
    return control.last_owned;
}
bool joint_return_step(const Info& original){
    if(joint_step(original))return true;
    if(joint_controller_state().bounded_failure)return false;
    ++joint_controller_state().fallbacks;
    // A typed unsupported quote is not a physical return. Retain the actual
    // old return owner and its refusal history rather than claiming an idle.
    return planned_return_step(body());
}
bool joint_owns_input(){
    const auto me=body();const auto flags=state(me);
    if(flags.is_dead()||flags.contains(champion_state::teleporting)||
       flags.contains(champion_state::recalling)||flags.contains(champion_state::submitting_gems))return false;
    if(home->is_set(point(me))||self().buffs().has_stun())return false;
    const auto support=exposure(point(me));
    if(support.valid&&support.amount==0){returning=false;return_refusals=0;}
    const bool contact=joint_near_contact();
    if(!(returning&&support.amount>0)&&!contact)return false;
    if(joint_step(me)){diag_stage(8);return true;}
    // A second actual refusal has used this input's finite paid alternative.
    // Yield as unsupported, without a fabricated completion or third attempt.
    if(joint_controller_state().bounded_failure)return true;
    if(returning&&support.amount>0){
        ++joint_controller_state().fallbacks;
        // Spend the retained actual return capability before legacy optional
        // collection. This fresh fallback keeps its narrower CAMP assurance.
        if(planned_return_step(body())){diag_stage(8);return true;}
    }
    return false;
}
