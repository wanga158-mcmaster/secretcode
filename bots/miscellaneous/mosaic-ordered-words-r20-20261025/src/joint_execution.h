// Included inside Runtime after joint_contact.h. Only the first nominated own
// word is executable here. No field preparation or joint re-evaluation occurs.
enum class JointExecutionStatus {empty,applied,refused,unsupported,unfunded,stale,invalid,progress,wait,overrun,feedback_failed};
enum class JointExecutionPhase {idle,admission,validation,command,feedback,refusal_close,complete};
struct JointExecutionBudget {
    bool declared{};
    std::uint64_t validation_bound{},command_bound{},feedback_bound{},failure_refresh_bound{};
    std::uint64_t success_physical_tail{},refusal_physical_tail{},final_tail{};
    unsigned max_attempts{unsigned(joint_world::attempt_capacity)};
};
struct JointExecutionFallbackBudget {
    bool declared{};std::uint64_t reprice_bound{};
    JointExecutionBudget word{};bool allow_risk{};
};
struct JointExecutionRefusalContext {
    int epoch{-1};bool pending{},fallback_used{};
    joint_world::Point origin{},destination{};
    joint_world::Revision frame{},observation{},effect{};unsigned horizon{},actual_prefix{};
    JointExecutionStatus cause{JointExecutionStatus::empty};
};
struct JointExecutionOutcome {
    JointExecutionStatus status{JointExecutionStatus::empty};
    bool attempted{},has_actual_effect{},closed{},finite_wait{},progress{},needs_fallback{};
    unsigned attempted_primitives{},applied_primitives{},refused_primitives{},last_primitive{joint_world::no_word};
    unsigned successful_physical_primitives{}; // observe has its own model cursor, not quote.successful
    bool feedback_closed{},fallback_handled{},fallback_attempted{},declared_risk_fallback{};
    bool raw_move_before{},raw_move_after{},restricted_move_before{},restricted_move_after{},channel_before{},channel_after{};
    int sdk_code{};std::uint64_t fuel_spent{};
};
// The two physical obligations are alternatives, not a charge summed as if a
// command both succeeds and is refused. Saturation is an invalid declaration.
static std::uint64_t joint_execution_add(std::uint64_t a,std::uint64_t b){
    const auto max=std::numeric_limits<std::uint64_t>::max();return b>max-a?max:a+b;
}
static std::uint64_t joint_execution_reserve(const JointExecutionBudget& b,unsigned count){
    const auto max=std::numeric_limits<std::uint64_t>::max();
    if(!b.declared||!b.validation_bound||!b.command_bound||!b.feedback_bound||!b.failure_refresh_bound||
       !b.final_tail||!b.max_attempts||b.max_attempts>joint_world::attempt_capacity||count>b.max_attempts)return max;
    const auto unit=joint_execution_add(joint_execution_add(b.validation_bound,b.command_bound),b.feedback_bound);
    if(unit==max||(count&&unit>max/count))return max;
    const auto alternatives=std::max(b.success_physical_tail,joint_execution_add(b.failure_refresh_bound,b.refusal_physical_tail));
    return joint_execution_add(joint_execution_add(unit*count,alternatives),b.final_tail);
}
static std::uint64_t joint_execution_reserve(const JointExecutionBudget& b){return joint_execution_reserve(b,b.max_attempts);}
static std::uint64_t joint_execution_fallback_reserve(const JointExecutionFallbackBudget& b){
    if(!b.declared||!b.reprice_bound)return std::numeric_limits<std::uint64_t>::max();
    return joint_execution_add(b.reprice_bound,joint_execution_reserve(b.word,1));
}
static bool joint_execution_fallback_prepaid(const JointExecutionBudget& original,const JointExecutionFallbackBudget& fallback){
    const auto required=joint_execution_fallback_reserve(fallback);
    return required!=std::numeric_limits<std::uint64_t>::max()&&original.refusal_physical_tail>=required;
}
static bool joint_execution_fallback_matches(const JointExecutionRefusalContext& refusal,
    const joint_world::World& world,const joint_world::Candidate& candidate,int actual_epoch){
    if(!refusal.pending||refusal.fallback_used||refusal.epoch!=actual_epoch||world.owner>=world.actor_count||
       world.frame<=refusal.frame||world.observation<refusal.observation||world.effect<refusal.effect||
       world.horizon_intervals!=refusal.horizon||!candidate.word_count||candidate.words[0].count!=1)return false;
    const auto& action=candidate.words[0].actions[0];
    return action.kind==joint_world::ActionKind::move&&action.point!=refusal.destination&&
        world.actors[world.owner].position==refusal.origin;
}

#ifndef MOSAIC_JOINT_EXECUTION_PURE_ONLY
struct JointExecutionState {
    JointExecutionBudget budget{};JointExecutionFallbackBudget fallback_budget{};JointExecutionOutcome last{};
    JointExecutionPhase phase{JointExecutionPhase::idle},first_failure_phase{JointExecutionPhase::idle};
    JointExecutionStatus first_failure{JointExecutionStatus::empty};
    unsigned first_failure_primitive{joint_world::no_word},last_primitive{joint_world::no_word};
    std::uint64_t selections{},attempts{},applied{},refused{},refreshes{},unfunded{},overruns{},unsupported{},waits{};
    std::uint64_t max_validation{},max_command{},max_feedback{},max_failure_refresh{};
    std::uint64_t observation_intent{};unsigned observation_cursor{};
    struct WaitLease {int group{-1},last_epoch{-1};unsigned inputs{};};
    std::array<WaitLease,joint_world::camp_capacity> capture_waits{};
    int channel_epoch{-1},channel_timer{-1};unsigned channel_bits{};
    JointExecutionRefusalContext refusal;
    bool in_fallback{};
};
static JointExecutionState& joint_execution_state(){static constinit JointExecutionState value{};return value;}
void joint_execution_budget(const JointExecutionBudget& b){joint_execution_state().budget=b;}
void joint_execution_fallback_budget(const JointExecutionFallbackBudget& b){joint_execution_state().fallback_budget=b;}
static bool joint_execution_channel(const Info& q){
    const auto flags=state(q);return flags.contains(champion_state::recalling)||
        flags.contains(champion_state::submitting_gems)||flags.contains(champion_state::teleporting);
}
struct JointExecutionFresh {
    Info body{};std::uint8_t restrictions{};vec2 target{};
    unsigned kind{},handle{};detail::ability_props ability{};int cooldown{};
    int wait_group{-1},wait_timer{-1};unsigned wait_channel_bits{};
    std::uint32_t gems{},ward_charges{};bool dash{},reset{},ground{},body_fresh{},target_fresh{},occupancy_fresh{};
};
bool joint_execution_same_epoch()const{return int(round_number())==epoch&&expected_epoch==epoch;}
bool joint_execution_funded(std::uint64_t reserve)const{
    return reserve!=std::numeric_limits<std::uint64_t>::max()&&joint_execution_same_epoch()&&fuel_remaining()>reserve;
}
static bool joint_execution_known_hidden(int code){return code==int(error_code::api_not_in_vision);}
bool joint_execution_code(int code,JointExecutionOutcome& out){
    if(!code)return true;out.sdk_code=code;
    if(joint_execution_known_hidden(code))return false;
    // Unexpected SDK failures keep the repository's turn-boundary error log.
    detail::check(code);return false;
}
bool joint_execution_body(JointExecutionFresh& fresh,JointExecutionOutcome& out){
    const auto owner=std::uint32_t(handle_champion_t(self()));
    fresh.body_fresh=joint_execution_code(detail::sdk_champion(owner,&fresh.body),out)&&
        joint_execution_code(detail::sdk_champion_restrictions(owner,&fresh.restrictions),out);
    return fresh.body_fresh;
}
bool joint_execution_target(const joint_world::Action& action,JointExecutionFresh& fresh,JointExecutionOutcome& out){
    auto& contact=joint_contact_state();fresh.target=joint_vec(action.point);
    if(action.target==joint_world::Target::actor){
        if(action.index>=contact.world.actor_count)return false;
        fresh.kind=unsigned(detail::entity_kind::champion);fresh.handle=contact.handles[action.index];
        Info target{};if(!joint_execution_code(detail::sdk_champion(fresh.handle,&target),out)||state(target).is_dead())return false;
        fresh.target=point(target);fresh.target_fresh=true;
        const bool ally=target.team==fresh.body.team;
        if(action.kind==joint_world::ActionKind::basic&&ally)return false;
        if(action.kind==joint_world::ActionKind::cast){
            const auto mask=fresh.handle==std::uint32_t(handle_champion_t(self()))?detail::ability_target_self:
                ally?detail::ability_target_allies:detail::ability_target_enemies;
            if(!(fresh.ability.targets&mask))return false;
        }
    }else if(action.target==joint_world::Target::camp){
        if(action.index>=contact.world.camp_count)return false;
        const auto member=contact.camp_members[action.index];if(member>=members.size())return false;
        fresh.kind=unsigned(detail::entity_kind::camp);fresh.handle=std::uint32_t(handle_camp_t(members[member].handle));
        std::uint32_t alive=0;
        if(!joint_execution_code(detail::sdk_camp_state(fresh.handle,&alive),out)||!alive)return false;
        // A fresh living member is causal evidence of a new group episode;
        // selecting another intent/epoch alone cannot renew a spent wait lease.
        for(auto& lease:joint_execution_state().capture_waits)if(lease.group==members[member].group)lease={};
        detail::camp_state target{};if(!joint_execution_code(detail::sdk_camp(fresh.handle,&target),out)||target.health<=0)return false;
        fresh.target={target.x,target.y};fresh.target_fresh=true;
        if(action.kind==joint_world::ActionKind::cast&&!(fresh.ability.targets&detail::ability_target_camps))return false;
    }else if(action.target==joint_world::Target::self){
        fresh.kind=unsigned(detail::entity_kind::champion);fresh.handle=std::uint32_t(handle_champion_t(self()));
        fresh.target=point(fresh.body);fresh.target_fresh=true;
        if(action.kind==joint_world::ActionKind::cast&&!(fresh.ability.targets&detail::ability_target_self))return false;
    }else{
        fresh.kind=0;fresh.handle=0;fresh.ground=true;
        if(action.kind==joint_world::ActionKind::cast&&!(fresh.ability.targets&detail::ability_target_ground))return false;
        auto sight=vision::visible_tiles();sight.export_linear_buffer(visible);
        fresh.target_fresh=sees(fresh.target);if(!fresh.target_fresh)return false;
        const auto occupied=fresh_blocked(point(fresh.body));(void)occupied;fresh.occupancy_fresh=true;
    }
    return fresh.target_fresh;
}
bool joint_execution_open(vec2 destination,const Info& me,bool ordinary,JointExecutionFresh& fresh){
    if(static_blocked(destination)||destination==point(me))return false;
    auto sight=vision::visible_tiles();sight.export_linear_buffer(visible);
    if(!sees(destination))return false;
    auto blocked=fresh_blocked(point(me));fresh.occupancy_fresh=true;
    if(blocked.is_set(destination))return false;
    if(ordinary){if(me.move_speed<=0||dist(point(me),destination)>me.move_speed)return false;
        const auto reachable=blocked.reachable_tiles(point(me),std::uint16_t(me.move_speed));
        if(!reachable.is_set(destination))return false;
    }
    return true;
}
bool joint_execution_wait(const joint_world::Candidate& candidate,JointExecutionFresh& fresh,JointExecutionOutcome& out){
    auto& execution=joint_execution_state();
    if(state(fresh.body).is_dead()||state(fresh.body).contains(champion_state::completed_turn))return false;
    if(joint_execution_channel(fresh.body)){
        int timer=0;if(!joint_execution_code(detail::sdk_champion_channel_time(std::uint32_t(handle_champion_t(self())),&timer),out)||timer<0)return false;
        const auto bits=unsigned(fresh.body.state)&((1u<<1)|(1u<<2)|(1u<<12));
        if(execution.channel_bits==bits&&execution.channel_epoch!=epoch&&timer>=execution.channel_timer)return false;
        if(execution.channel_bits==bits&&execution.channel_epoch==epoch)return false;
        fresh.wait_channel_bits=bits;fresh.wait_timer=timer;return true;
    }
    if(candidate.purpose!=joint_world::Purpose::capture)return false;
    auto& contact=joint_contact_state();const int group=contact.request.capture_group;
    if(group<0||std::size_t(group)>=groups.size()||std::size_t(group)>=memory.size())return false;
    const auto& stored=memory[std::size_t(group)];
    if(!stored.end||stored.end-stored.begin>joint_world::region_capacity)return false;
    bool on_region=false;
    for(std::size_t n=stored.begin;n<stored.end;++n){if(n>=members.size())return false;
        on_region=on_region||point(fresh.body)==members[n].pos;std::uint32_t alive=0;
        if(!joint_execution_code(detail::sdk_camp_state(std::uint32_t(handle_camp_t(members[n].handle)),&alive),out)||alive)return false;
    }
    unsigned value=0;
    if(!on_region||!joint_execution_code(detail::sdk_camp_group_state(std::uint32_t(handle_camp_group_t(groups[std::size_t(group)])),&value),out)||value!=2)return false;
    for(unsigned n=0;n<contact.world.actor_count;++n){
        if(contact.world.actors[n].team==unsigned(fresh.body.team))continue;Info enemy{};
        const auto code=detail::sdk_champion(contact.handles[n],&enemy);
        if(joint_execution_known_hidden(code))continue;
        if(!joint_execution_code(code,out))return false;
        if(state(enemy).is_dead())continue;
        for(std::size_t member=stored.begin;member<stored.end;++member)if(point(enemy)==members[member].pos)return false;
    }
    JointExecutionState::WaitLease* lease=nullptr;
    for(auto& entry:execution.capture_waits)if(entry.group==group){lease=&entry;break;}
    if(!lease)for(auto& entry:execution.capture_waits)if(entry.group<0){entry.group=group;lease=&entry;break;}
    if(!lease||lease->last_epoch==epoch||lease->inputs>=unsigned(std::max(1,constants::capture_time)+1))return false;
    fresh.wait_group=group;return true;
}
bool joint_execution_validate(const joint_world::Action& action,JointExecutionFresh& fresh,JointExecutionOutcome& out){
    if(!joint_execution_body(fresh,out))return false;
    const auto flags=state(fresh.body);const auto owner=std::uint32_t(handle_champion_t(self()));
    if(flags.is_dead()||flags.contains(champion_state::completed_turn)||joint_execution_channel(fresh.body))return false;
    if(action.follow_target)return false;
    switch(action.kind){
    case joint_world::ActionKind::basic:
        if(!flags.contains(champion_state::can_attack)||(fresh.restrictions&2)||
           (action.target!=joint_world::Target::actor&&action.target!=joint_world::Target::camp))return false;
        return joint_execution_target(action,fresh,out)&&dist(point(fresh.body),fresh.target)<=fresh.body.attack_range;
    case joint_world::ActionKind::move:
        fresh.target=joint_vec(action.point);
        return flags.contains(champion_state::can_move)&&!(fresh.restrictions&1)&&joint_execution_open(fresh.target,fresh.body,true,fresh);
    case joint_world::ActionKind::cast:{
        if(action.slot>=2||!flags.contains(champion_state::can_cast)||(fresh.restrictions&4))return false;
        if(!joint_execution_code(detail::sdk_champion_ability_props(owner,action.slot,&fresh.ability),out)||
           !joint_execution_code(detail::sdk_champion_cooldown(owner,action.slot,&fresh.cooldown),out)||fresh.cooldown)return false;
        if((fresh.ability.attributes&detail::ability_attribute_exclusive_with_movement)&&walked)return false;
        const auto kind=ability_kind(fresh.ability.kind);float range=0;
        fresh.reset=kind==ability_kind::attack_reset;fresh.dash=kind==ability_kind::dash;
        if(fresh.reset){if(flags.contains(champion_state::can_attack)||action.target!=joint_world::Target::self)return false;}
        else if(kind==ability_kind::damage)range=fresh.ability.payload.damage.range;
        else if(kind==ability_kind::aoe_damage)range=fresh.ability.payload.aoe_damage.range;
        else if(kind==ability_kind::cc)range=fresh.ability.payload.cc.range;
        else if(kind==ability_kind::shield)range=fresh.ability.payload.shield.range;
        else if(fresh.dash)range=fresh.ability.payload.dash.range;
        else return false;
        if(!joint_execution_target(action,fresh,out)||dist(point(fresh.body),fresh.target)>range)return false;
        if(fresh.dash&&!joint_execution_open(fresh.target,fresh.body,false,fresh))return false;
        return true;}
    case joint_world::ActionKind::observe:{
        if(action.target!=joint_world::Target::ground||!action.relevant_observation||!action.observation_intent||
           action.cursor_after!=action.cursor_before+1||action.cursor_after>action.cursor_total||action.cursor_total>32||
           (fresh.restrictions&32))return false;
        auto& execution=joint_execution_state();
        if(execution.observation_intent==action.observation_intent&&execution.observation_cursor!=action.cursor_before)return false;
        detail::ward_resources resource{};
        if(!joint_execution_code(detail::sdk_champion_wards(owner,&resource),out)||!resource.charges)return false;
        fresh.ward_charges=resource.charges;fresh.target=joint_vec(action.point);
        if(dist(point(fresh.body),fresh.target)>constants::ward_placement_radius||static_blocked(fresh.target))return false;
        auto blocked=fresh_blocked(point(fresh.body))|vision::ward_tiles(team::ally);fresh.occupancy_fresh=true;
        return !blocked.is_set(fresh.target);}
    case joint_world::ActionKind::recall:
        return !flags.contains(champion_state::recall_started)&&!(fresh.restrictions&7);
    case joint_world::ActionKind::bank:{
        fresh.target=joint_vec(action.point);bool altar=false;for(auto p:altars)altar=altar||p==fresh.target;
        if(!altar||flags.contains(champion_state::submit_gems_started)||(fresh.restrictions&7)||
           dist(point(fresh.body),fresh.target)>constants::submit_gems_range)return false;
        if(!joint_execution_code(detail::sdk_champion_gems(owner,&fresh.gems),out)||!fresh.gems)return false;
        detail::altar_lock_info lock{};
        const auto code=detail::sdk_altar_lock(fresh.target.x,fresh.target.y,int(detail::absolute_team(team::enemy)),&lock);
        if(code==int(error_code::api_not_found))return true;
        if(!code||joint_execution_known_hidden(code))return false;
        return joint_execution_code(code,out);}
    case joint_world::ActionKind::wait:return true;
    }
    return false;
}
bool joint_execution_feedback(const joint_world::Action& action,const JointExecutionFresh& before,
    JointExecutionFresh& after,bool applied,bool refused,JointExecutionOutcome& out){
    if(!joint_execution_body(after,out))return false;
    auto sight=vision::visible_tiles();sight.export_linear_buffer(visible);
    const auto occupied=fresh_blocked(point(after.body));(void)occupied;
    after.occupancy_fresh=true;
    // Refresh the exact unit used by this primitive, including actual death.
    // No global camp scan or new candidate evaluation is hidden in feedback.
    if(action.target==joint_world::Target::actor){Info target{};
        if(!joint_execution_code(detail::sdk_champion(before.handle,&target),out))return false;
        after.target_fresh=true;after.target=point(target);
    }else if(action.target==joint_world::Target::camp){std::uint32_t alive=0;
        if(!joint_execution_code(detail::sdk_camp_state(before.handle,&alive),out))return false;
        if(alive){detail::camp_state target{};if(!joint_execution_code(detail::sdk_camp(before.handle,&target),out))return false;after.target={target.x,target.y};
            if constexpr(cfg::local_observation)
                if(!joint_observation_commit_member(joint_contact_state().camp_members[action.index],alive,&target))return false;
        }else if constexpr(cfg::local_observation)
            if(!joint_observation_commit_member(joint_contact_state().camp_members[action.index],alive))return false;
        after.target_fresh=true;
    }else after.target_fresh=true;
    if(refused){
        // Native false preserves physical allowance and cooldown; revealed
        // occupancy changes information, not successful primitive count.
        constexpr unsigned physical=(1u<<0)|(1u<<3)|(1u<<4)|(1u<<11);
        if(point(before.body)!=point(after.body)||((unsigned(before.body.state)^unsigned(after.body.state))&physical))return false;
        if(action.kind==joint_world::ActionKind::cast){int cd=0;
            if(!joint_execution_code(detail::sdk_champion_cooldown(std::uint32_t(handle_champion_t(self())),action.slot,&cd),out)||cd!=before.cooldown)return false;}
        return true;
    }
    if(!applied)return true;
    const bool motion=action.kind==joint_world::ActionKind::move||before.dash;
    if(point(after.body)!=(motion?before.target:point(before.body)))return false;
    const auto flags=state(after.body);
    if(action.kind==joint_world::ActionKind::move&&flags.contains(champion_state::can_move))return false;
    if(action.kind==joint_world::ActionKind::basic&&flags.contains(champion_state::can_attack))return false;
    if(action.kind==joint_world::ActionKind::cast){
        if(flags.contains(champion_state::can_cast))return false;
        if(before.reset&&!flags.contains(champion_state::can_attack))return false;
    }
    if(action.kind==joint_world::ActionKind::recall&&!flags.contains(champion_state::recalling))return false;
    if(action.kind==joint_world::ActionKind::bank&&!flags.contains(champion_state::submitting_gems))return false;
    if(action.kind==joint_world::ActionKind::observe){detail::ward_resources wards{};
        if(!joint_execution_code(detail::sdk_champion_wards(std::uint32_t(handle_champion_t(self())),&wards),out)||wards.charges>=before.ward_charges)return false;}
    return !flags.is_dead();
}
void joint_execution_history(const joint_world::Action& action,const JointExecutionFresh& before,const JointExecutionFresh& after,bool applied,bool refused){
    if(refused){planned_route().observe_refusal(rp(point(before.body)));++return_refusals;return;}
    if(!applied)return;last_action_round=epoch;
    if(action.kind==joint_world::ActionKind::basic)attacked=true;
    if(action.kind==joint_world::ActionKind::move){
        const auto from=rp(point(before.body)),to=rp(point(after.body));
        if(exit_trace.at(from))exit_trace.observe_step(from,to,exposure(point(after.body)),true);
        auto& route=planned_route();
        if(route.valid()&&route.at(from)&&route.size()>1&&route.node(1).point==to)route.observe_step(from,to,true);else route.clear();
        walked=true;path_slack=8;
    }else if(before.dash){
        // A dash is not an observed ordinary outward edge.
        melee_exit::observe_nonordinary(exit_trace,rp(point(after.body)));planned_route().clear();
    }
    auto& funded=funded_state();++funded.effect;funded.prepared=false;funded.hold_admitted=false;
}
JointExecutionOutcome joint_execute_selection(const joint_world::Selection& selection){
    auto& execution=joint_execution_state();auto& contact=joint_contact_state();JointExecutionOutcome out{};
    const auto start=fuel_remaining();execution.phase=JointExecutionPhase::admission;++execution.selections;
    const auto finish=[&](JointExecutionStatus status,bool closed,bool fallback){
        out.status=status;out.closed=closed;out.needs_fallback=fallback;out.fuel_spent=start>=fuel_remaining()?start-fuel_remaining():0;
        if(status!=JointExecutionStatus::applied&&status!=JointExecutionStatus::progress&&status!=JointExecutionStatus::wait&&execution.first_failure==JointExecutionStatus::empty){
            execution.first_failure=status;execution.first_failure_phase=execution.phase;execution.first_failure_primitive=out.last_primitive;}
        execution.phase=JointExecutionPhase::complete;execution.last=out;return out;
    };
    if(execution.refusal.epoch!=epoch&&!execution.in_fallback)execution.refusal={};
    if(execution.refusal.pending&&execution.refusal.epoch==epoch&&!execution.in_fallback)
        return finish(execution.refusal.cause,false,true);
    if(contact.status!=JointContactStatus::ready||contact.owner_epoch!=epoch||!joint_execution_same_epoch())return finish(JointExecutionStatus::stale,false,true);
    if(selection.index>=contact.candidate_count)return finish(JointExecutionStatus::empty,false,true);
    const auto& candidate=contact.candidates[selection.index];const auto quote=selection.quote;
    if(quote.subject!=&candidate||!quote.valid||!quote.bounded||quote.frame!=contact.world.frame||quote.observation!=contact.world.observation||
       quote.effect!=contact.world.effect||(!contact.request.allow_approximate&&quote.approximate)||
       (!(execution.in_fallback?execution.fallback_budget.allow_risk:contact.request.allow_fatal)&&(!quote.alive||!quote.complete))||!candidate.word_count)return finish(JointExecutionStatus::invalid,false,true);
    const auto word=candidate.words[0];const auto budget=execution.in_fallback?execution.fallback_budget.word:execution.budget;
    if(!word.count||word.count>joint_world::attempt_capacity)return finish(JointExecutionStatus::invalid,false,true);
    // The model's physical-prefix receipt cannot authorize an observation-only
    // prefix followed by work. Current ward/capture offers are standalone;
    // reject unsupported composition before spending any physical allowance.
    if(word.count>1)for(unsigned n=0;n<word.count;++n)
        if(word.actions[n].kind==joint_world::ActionKind::observe||word.actions[n].kind==joint_world::ActionKind::wait)
            return finish(JointExecutionStatus::unsupported,true,true);
    bool can_refuse=false;
    for(unsigned n=0;n<word.count;++n)can_refuse=can_refuse||word.actions[n].kind==joint_world::ActionKind::move||
        word.actions[n].kind==joint_world::ActionKind::cast||word.actions[n].kind==joint_world::ActionKind::observe;
    if(can_refuse&&!execution.in_fallback&&!joint_execution_fallback_prepaid(budget,execution.fallback_budget)){
        ++execution.unfunded;return finish(JointExecutionStatus::unfunded,false,true);
    }
    if(!joint_execution_funded(joint_execution_reserve(budget,word.count))){++execution.unfunded;return finish(JointExecutionStatus::unfunded,false,true);}
    for(unsigned n=0;n<word.count;++n){
        out.last_primitive=n;execution.last_primitive=n;const auto& action=word.actions[n];
        if(action.only_after_refusal)continue; // This executor closes immediately on actual false.
        if(!joint_execution_funded(joint_execution_reserve(budget,word.count-n))){++execution.unfunded;return finish(JointExecutionStatus::unfunded,false,true);}
        execution.phase=JointExecutionPhase::validation;const auto vb=fuel_remaining();JointExecutionFresh before{};
        bool legal=false;
        if(action.kind==joint_world::ActionKind::wait){legal=joint_execution_body(before,out)&&joint_execution_wait(candidate,before,out);}
        else legal=joint_execution_validate(action,before,out);
        const auto validation=vb>=fuel_remaining()?vb-fuel_remaining():0;execution.max_validation=std::max(execution.max_validation,validation);
        if(!out.attempted_primitives){out.raw_move_before=state(before.body).contains(champion_state::can_move);out.restricted_move_before=before.restrictions&1;out.channel_before=joint_execution_channel(before.body);}
        if(validation>budget.validation_bound||!joint_execution_same_epoch()){++execution.overruns;return finish(JointExecutionStatus::overrun,false,true);}
        if(!legal){++execution.unsupported;
            // Actual earlier work may invalidate the remainder (for example,
            // a target dies before Reset/Basic). Give remaining physical M the
            // same paid fresh-model owner; no native false/debt is invented.
            if(out.has_actual_effect&&!execution.in_fallback&&before.body_fresh&&
               state(before.body).contains(champion_state::can_move)&&!(before.restrictions&1)&&!joint_execution_channel(before.body)){
                auto& continuation=execution.refusal;continuation.epoch=epoch;continuation.pending=true;continuation.fallback_used=false;
                continuation.origin=joint_point(point(before.body));continuation.destination=
                    action.kind==joint_world::ActionKind::move?joint_point(before.target):joint_world::Point{-10001,-10001};
                continuation.frame=contact.world.frame;continuation.observation=contact.observation_revision;
                continuation.effect=contact.effect_revision;continuation.horizon=contact.world.horizon_intervals;
                continuation.actual_prefix=out.successful_physical_primitives;continuation.cause=JointExecutionStatus::unsupported;
                return finish(JointExecutionStatus::unsupported,false,true);
            }
            return finish(JointExecutionStatus::unsupported,true,true);
        }
        if(!out.attempted_primitives){const auto& expected=contact.world.actors[contact.world.owner];const auto flags=state(before.body);
            if(joint_point(point(before.body))!=expected.position||before.body.health!=expected.health||
               (flags.contains(champion_state::can_move)&&!(before.restrictions&1))!=expected.can_move||
               (flags.contains(champion_state::can_attack)&&!(before.restrictions&2))!=expected.can_attack||
               (flags.contains(champion_state::can_cast)&&!(before.restrictions&4))!=expected.can_cast)
                return finish(JointExecutionStatus::stale,true,true);
        }
        if(action.kind==joint_world::ActionKind::wait){out.finite_wait=true;++execution.waits;
            if(before.wait_channel_bits){execution.channel_bits=before.wait_channel_bits;execution.channel_timer=before.wait_timer;execution.channel_epoch=epoch;}
            else for(auto& lease:execution.capture_waits)if(lease.group==before.wait_group){lease.last_epoch=epoch;++lease.inputs;break;}
            out.raw_move_after=state(before.body).contains(champion_state::can_move);out.restricted_move_after=before.restrictions&1;out.channel_after=joint_execution_channel(before.body);
            joint_contact_actual_feedback(false,false);return finish(JointExecutionStatus::wait,true,false);}
        const auto residual=joint_execution_add(joint_execution_reserve(budget,word.count-n-1),joint_execution_add(budget.command_bound,budget.feedback_bound));
        if(!joint_execution_funded(residual)){++execution.unfunded;return finish(JointExecutionStatus::unfunded,false,true);}
        execution.phase=JointExecutionPhase::command;const auto cb=fuel_remaining();int code=0;std::int32_t successful=1;
        out.attempted=true;++out.attempted_primitives;++execution.attempts;
        switch(action.kind){
        case joint_world::ActionKind::basic:code=detail::sdk_attack(before.kind,before.handle);break;
        case joint_world::ActionKind::move:code=detail::sdk_move_to(before.target.x,before.target.y,&successful);break;
        case joint_world::ActionKind::cast:code=detail::sdk_cast(action.slot,before.reset?0:before.kind,before.reset?0:before.handle,
            before.ground?before.target.x:-1,before.ground?before.target.y:-1,&successful);break;
        case joint_world::ActionKind::observe:code=detail::sdk_place_ward(before.target.x,before.target.y,&successful);break;
        case joint_world::ActionKind::recall:code=detail::sdk_recall();break;
        case joint_world::ActionKind::bank:code=detail::sdk_submit_gems(before.target.x,before.target.y);break;
        case joint_world::ActionKind::wait:break;
        }
        const auto command=cb>=fuel_remaining()?cb-fuel_remaining():0;execution.max_command=std::max(execution.max_command,command);
        if(code){out.sdk_code=code;execution.first_failure_phase=JointExecutionPhase::command;detail::check(code);}
        const bool applied=successful!=0,refused=!applied;
        if(applied){out.has_actual_effect=true;++out.applied_primitives;++execution.applied;
            if(action.kind!=joint_world::ActionKind::observe)++out.successful_physical_primitives;}
        else{++out.refused_primitives;++execution.refused;}
        // Debt is physical history even if subsequent feedback cannot complete.
        if(refused){planned_route().observe_refusal(rp(point(before.body)));++return_refusals;
            if(!execution.in_fallback){execution.refusal.epoch=epoch;execution.refusal.pending=true;execution.refusal.fallback_used=false;
                execution.refusal.origin=joint_point(point(before.body));execution.refusal.destination=joint_point(before.target);
                execution.refusal.frame=contact.world.frame;execution.refusal.observation=contact.observation_revision;
                execution.refusal.effect=contact.effect_revision;execution.refusal.horizon=contact.world.horizon_intervals;
                execution.refusal.actual_prefix=out.successful_physical_primitives;execution.refusal.cause=JointExecutionStatus::refused;}
        }
        execution.phase=refused?JointExecutionPhase::refusal_close:JointExecutionPhase::feedback;
        const auto feedback_bound=refused?joint_execution_add(budget.feedback_bound,budget.failure_refresh_bound):budget.feedback_bound;
        const auto physical_tail=refused?budget.refusal_physical_tail:budget.success_physical_tail;
        if(!joint_execution_funded(joint_execution_add(joint_execution_add(feedback_bound,physical_tail),budget.final_tail))){
            // The command receipt is real, but no refresh is invented when a
            // caller-declared bound proved too small. Revoke old authority.
            if(applied){++contact.effect_revision;++contact.actual_effects;last_action_round=epoch;}
            if(refused)++contact.actual_refusals;
            contact.status=JointContactStatus::stale;contact.selection={};
            ++execution.overruns;return finish(JointExecutionStatus::overrun,false,true);
        }
        const auto fb=fuel_remaining();JointExecutionFresh after{};
        const bool observed=joint_execution_feedback(action,before,after,applied,refused,out);
        const auto feedback=fb>=fuel_remaining()?fb-fuel_remaining():0;++execution.refreshes;
        if(refused)execution.max_failure_refresh=std::max(execution.max_failure_refresh,feedback);
        else execution.max_feedback=std::max(execution.max_feedback,feedback);
        out.raw_move_after=state(after.body).contains(champion_state::can_move);out.restricted_move_after=after.restrictions&1;out.channel_after=joint_execution_channel(after.body);
        joint_contact_actual_feedback(applied,refused);
        if(applied&&after.body_fresh)joint_execution_history(action,before,after,true,false);
        out.feedback_closed=observed;
        if(refused&&!execution.in_fallback){
            if(after.body_fresh)execution.refusal.origin=joint_point(point(after.body));
            execution.refusal.observation=contact.observation_revision;execution.refusal.effect=contact.effect_revision;
        }
        if(command>budget.command_bound||feedback>(refused?joint_execution_add(budget.feedback_bound,budget.failure_refresh_bound):budget.feedback_bound)||!joint_execution_same_epoch()){
            ++execution.overruns;return finish(JointExecutionStatus::overrun,false,true);}
        if(!observed)return finish(JointExecutionStatus::feedback_failed,false,true);
        if(refused)return finish(JointExecutionStatus::refused,false,true);
        if(action.kind==joint_world::ActionKind::recall||action.kind==joint_world::ActionKind::bank){
            execution.channel_bits=0;execution.channel_epoch=-1;execution.channel_timer=-1;
        }
        if(action.kind==joint_world::ActionKind::move||before.dash)out.progress=true;
        if(action.kind==joint_world::ActionKind::observe){execution.observation_intent=action.observation_intent;execution.observation_cursor=action.cursor_after;out.progress=true;}
        // Old projections are revoked by actual_feedback. This receipt permits
        // only the remaining copied finite word, never a fresh candidate menu.
        const joint_world::Receipt receipt{contact.world.frame,contact.observation_revision,contact.effect_revision,
            out.successful_physical_primitives,after.body_fresh,after.target_fresh,after.occupancy_fresh,true,contact.request.allow_approximate,
            execution.in_fallback?execution.fallback_budget.allow_risk:contact.request.allow_fatal};
        if(n+1<word.count&&!joint_world::actual_continuation(contact.world,candidate,quote,receipt))return finish(JointExecutionStatus::feedback_failed,false,true);
    }
    if(!out.applied_primitives)return finish(JointExecutionStatus::unsupported,true,true);
    return finish(out.progress?JointExecutionStatus::progress:JointExecutionStatus::applied,true,false);
}
// The caller first invokes the separately funded contact re-pricer on fresh
// actual state. This entry point cannot promote the old quote into authority,
// retry the refused endpoint, or renew the physical episode's false allowance.
// A second native false remains an explicit unhandled physical obligation.
JointExecutionOutcome joint_execute_refusal_fallback(const joint_world::Selection& selection){
    auto& execution=joint_execution_state();auto& contact=joint_contact_state();auto& refusal=execution.refusal;
    JointExecutionOutcome out{};out.status=JointExecutionStatus::unsupported;out.needs_fallback=true;
    const auto reject=[&](JointExecutionStatus status){out.status=status;execution.last=out;return out;};
    if(!refusal.pending||refusal.fallback_used||refusal.epoch!=epoch||!joint_execution_same_epoch())return reject(JointExecutionStatus::stale);
    if(!joint_execution_fallback_prepaid(execution.budget,execution.fallback_budget))return reject(JointExecutionStatus::unfunded);
    if(contact.status!=JointContactStatus::ready||selection.index>=contact.candidate_count)return reject(JointExecutionStatus::stale);
    const auto& candidate=contact.candidates[selection.index];
    if(!joint_execution_fallback_matches(refusal,contact.world,candidate,epoch))return reject(JointExecutionStatus::unsupported);
    const bool risk=!selection.quote.alive||!selection.quote.complete;
    if(risk&&!execution.fallback_budget.allow_risk)return reject(JointExecutionStatus::invalid);
    // This finite flag is independent of observation/effect revisions; another
    // prepared model in the same input cannot reopen an attempted fallback.
    refusal.fallback_used=true;
    struct FallbackGuard {bool& flag;explicit FallbackGuard(bool& value):flag(value){flag=true;}~FallbackGuard(){flag=false;}} guard(execution.in_fallback);
    out=joint_execute_selection(selection);
    out.fallback_attempted=out.attempted;out.declared_risk_fallback=risk;
    out.fallback_handled=out.progress&&out.has_actual_effect&&out.closed&&out.status==JointExecutionStatus::progress;
    out.needs_fallback=!out.fallback_handled;
    if(out.fallback_handled)refusal.pending=false;
    execution.last=out;return out;
}
#endif
