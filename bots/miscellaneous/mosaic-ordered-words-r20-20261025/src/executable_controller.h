// Included inside Runtime after the operational observation/execution layers.
// Numeric utility and assurance may be unknown. They never replace the actual
// macro goal, native authority or complete success/failure resource contract.
template<std::size_t Capacity=512> struct ExecutableCaptureLeases {
    struct Entry {bool reward_observed{};unsigned remaining{};};
    std::array<Entry,Capacity> entries{};
    bool reward(std::size_t group,unsigned duration,bool actual){
        if(!actual||group>=Capacity||!duration)return false;auto& entry=entries[group];
        if(!entry.reward_observed){entry.reward_observed=true;entry.remaining=duration;}return true;
    }
    bool alive(std::size_t group,bool actual){
        if(!actual||group>=Capacity)return false;entries[group].reward_observed=false;entries[group].remaining=0;return true;
    }
    unsigned remaining(std::size_t group)const{return group<Capacity?entries[group].remaining:0;}
    bool waited(std::size_t group,bool actual){
        if(!actual||group>=Capacity||!entries[group].reward_observed||!entries[group].remaining)return false;
        --entries[group].remaining;return true;
    }
};
// A fresh group Alive receipt identifies no particular living member. A
// bounded sequence of actual member queries may locate one. Re-reading Alive,
// changing the job, or advancing the clock cannot renew a completed pass.
template<std::size_t Capacity=512> struct ExecutableGroupProbes {
    struct Entry {std::uint64_t episode{};int x{},y{};unsigned count{},next{},left{};bool started{};};
    std::array<Entry,Capacity> entries{};
    int begin(std::size_t group,unsigned count,std::uint64_t episode,int x,int y,bool actual_alive){
        if(!actual_alive||group>=Capacity||!count)return -1;auto& entry=entries[group];
        if(!entry.started||entry.episode!=episode||entry.x!=x||entry.y!=y||entry.count!=count){
            entry={episode,x,y,count,0,count,true};}
        return entry.left?int(entry.next):-1;
    }
    bool observed(std::size_t group,unsigned ordinal,bool actual_query){
        if(!actual_query||group>=Capacity)return false;auto& entry=entries[group];
        if(!entry.started||!entry.left||ordinal!=entry.next)return false;
        --entry.left;entry.next=(entry.next+1)%entry.count;return true;
    }
    void changed_lifecycle(std::size_t group,bool actual){if(actual&&group<Capacity)entries[group].started=false;}
};
enum class ExecutableOwnerSelection {pending_feedback,retained_legacy,fresh_policy};
static constexpr ExecutableOwnerSelection executable_owner_selection(bool pending,bool retained){
    return pending?ExecutableOwnerSelection::pending_feedback:
        retained?ExecutableOwnerSelection::retained_legacy:ExecutableOwnerSelection::fresh_policy;
}
#ifndef MOSAIC_EXECUTABLE_CONTROLLER_PURE_ONLY
struct ExecutableControllerState {
    executable_policy::Policy policy;
    ExecutableObservationRequest request;
    executable_policy::Valuation valuation;
    ExecutableCaptureLeases<> capture;
    ExecutableGroupProbes<> probes;
    std::uint64_t episode{1},calls{},prepared{},selected{},owned{},handoffs{},need_observation{},unsupported{};
    std::uint64_t required{},max_required{},service_events{},last_emit_attempts{};
    int last_epoch{-1},last_member{-1},last_group{-1},capture_group{-1},capture_epoch{-1};
    unsigned capture_inputs{},first_packets{},stage{};
    bool bag_known{},bank_started{},bank_chain{},previous_on_region{};
    unsigned bag{},submitted{},bank_amount{},bank_start_submitted{},last_channel_ticks{};
    int bank_epoch{-1},last_observed_epoch{-1};
    executable_policy::Channel last_channel{executable_policy::Channel::none};
    ExecutableMacroPhase phase{ExecutableMacroPhase::work};
    bool input_started{},legacy_latched{},legacy_body_current{};
    Info legacy_body{};int legacy_body_epoch{-1};
    unsigned legacy_bypasses{},deferred_handoffs{};
};
static ExecutableControllerState& executable_controller_state(){
    static constinit ExecutableControllerState value{};return value;
}
static executable_policy::Point executable_point(vec2 point){return {point.x,point.y};}
static vec2 executable_vec(executable_policy::Point point){return {point.x,point.y};}
static unsigned executable_distance(executable_policy::Point a,executable_policy::Point b){
    return unsigned(executable_policy::distance(a,b));
}
void executable_policy_reset(executable_policy::Policy& policy,executable_policy::ServiceKey owner,
                             executable_policy::Purpose purpose){
    policy.token={};policy.owner=owner;policy.purpose=purpose;policy.actual_prefix=executable_execution_state().service.actual_prefix();
    policy.success.count=0;policy.on_refusal.declared=policy.on_changed.declared=false;
    policy.on_refusal.word.count=policy.on_changed.word.count=0;
    policy.on_refusal.exclude_point=policy.on_changed.exclude_point=false;
}
static executable_policy::Action executable_move_action(executable_policy::Point point){
    executable_policy::Action action{};action.kind=executable_policy::ActionKind::move;
    action.target_kind=executable_policy::TargetKind::ground;action.point=point;return action;
}
void executable_move_closure(executable_policy::Policy& policy,executable_policy::Point point,
                             executable_policy::Point excluded){
    policy.on_changed.declared=true;policy.on_changed.guard=executable_policy::Guard::usable_move;
    policy.on_changed.word.count=1;policy.on_changed.word.actions[0]=executable_move_action(point);
    policy.on_changed.exclude_point=false;
    bool refusable=false;for(unsigned i=0;i<policy.success.count;++i)
        refusable=refusable||policy.success.actions[i].kind==executable_policy::ActionKind::move||
            policy.success.actions[i].kind==executable_policy::ActionKind::cast||
            policy.success.actions[i].kind==executable_policy::ActionKind::observe;
    if(refusable){policy.on_refusal.declared=true;policy.on_refusal.guard=executable_policy::Guard::usable_move;
        policy.on_refusal.word.count=1;policy.on_refusal.word.actions[0]=executable_move_action(point);
        policy.on_refusal.exclude_point=true;policy.on_refusal.excluded=excluded;}
}
// This is only a dated geometric nomination. The observer obtains actual M,
// reachability and occupancy; no risk value or future path legality is minted.
bool executable_geometry_step(executable_policy::Point origin,executable_policy::Point goal,
                              executable_policy::Point excluded,bool exclude,
                              executable_policy::Point& answer)const{
    bool found=false;unsigned best=std::numeric_limits<unsigned>::max();
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx&&!dy)continue;
        const executable_policy::Point candidate{origin.x+dx,origin.y+dy};
        if((exclude&&candidate==excluded)||static_blocked(executable_vec(candidate)))continue;
        const auto distance=executable_distance(candidate,goal);
        if(!found||distance<best){found=true;best=distance;answer=candidate;}}
    return found;
}
bool executable_return_nomination(const executable_policy::Snapshot& snapshot,
                                  executable_policy::Point& next,executable_policy::Point& alternate)const{
    auto goal=snapshot.pose;bool historical=false;
    if(exit_trace.valid()&&exit_trace.size()>=2){const auto last=exit_trace.point(exit_trace.size()-1);
        if(last.x==snapshot.pose.x&&last.y==snapshot.pose.y){const auto previous=exit_trace.point(exit_trace.size()-2);
            next={previous.x,previous.y};const auto anchor=exit_trace.point(0);goal={anchor.x,anchor.y};historical=true;}}
    if(!historical){const auto& positions=executable_home_cache();if(positions.empty())return false;
        goal=executable_point(positions.front());
        if(!executable_geometry_step(snapshot.pose,goal,{},false,next))return false;}
    return executable_geometry_step(snapshot.pose,goal,next,true,alternate);
}
int executable_nominate_member(const executable_policy::Snapshot& snapshot,int group)const{
    if(group<0||std::size_t(group)>=memory.size())return -1;
    const auto& saved=memory[std::size_t(group)];int best=-1;unsigned best_distance=std::numeric_limits<unsigned>::max();
    bool best_in_range=false;
    for(std::size_t i=saved.begin;i<saved.end;++i){const auto& member=members[i];
        if(member.work.nomination(epoch)==work_memory::Nomination::DeferredDead)continue;
        const auto distance=executable_distance(snapshot.pose,executable_point(member.pos));
        const bool in_range=distance<=unsigned(std::max(0,snapshot.attack_range));
        if(best<0||(in_range&&!best_in_range)||(in_range==best_in_range&&distance<best_distance)){
            best=int(i);best_distance=distance;best_in_range=in_range;}}
    return best;
}
bool executable_service_fact(executable_policy::ServiceEvent event,const ExecutablePrepared& prepared,
                             unsigned amount=0,bool full_bank=false){
    auto& native=executable_native_execution_state();auto& service=executable_execution_state().service;
    executable_policy::ServiceReceipt receipt{};receipt.owner=service.key();receipt.event=event;
    receipt.stamp=prepared.snapshot.stamp;receipt.serial=++native.service_serial;receipt.actual=true;
    receipt.group=prepared.snapshot.group;receipt.amount=amount;receipt.pose=prepared.snapshot.pose;
    receipt.full_bank_actual=full_bank;
    const bool accepted=executable_execution_service(receipt);
    if(accepted)++executable_controller_state().service_events;return accepted;
}
void executable_reconcile_service(const ExecutablePrepared& prepared){
    auto& control=executable_controller_state();auto& service=executable_execution_state().service;
    const auto& snapshot=prepared.snapshot;
    if(!prepared.owner_actual||!snapshot.body_actual||snapshot.stamp.epoch!=std::uint64_t(epoch)||
       snapshot.self!=std::uint32_t(handle_champion_t(self())))return;
    service.knowledge(snapshot.stamp);
    const bool requested_group=prepared.group_actual&&prepared.group_index>=0&&
        prepared.group_index==control.request.group&&std::size_t(prepared.group_index)<memory.size()&&
        executable_policy::Revision(snapshot.group)==service.key().job;
    if(requested_group&&prepared.group_state==2&&
       executable_policy::Revision(snapshot.group)==service.key().job&&service.stage()==executable_policy::Stage::work)
        executable_service_fact(executable_policy::ServiceEvent::clear,prepared);
    if(snapshot.gems_actual){
        if(snapshot.authority.channel==executable_policy::Channel::bank&&prepared.submitted_actual&&!control.bank_started){
            control.bank_started=true;control.bank_amount=snapshot.gems;
            control.bank_start_submitted=prepared.submitted_gems;control.bank_epoch=epoch;}
        if(control.bag_known&&snapshot.gems>control.bag&&control.previous_on_region&&
           requested_group&&control.last_group==prepared.group_index&&prepared.group_state==3)
            executable_service_fact(executable_policy::ServiceEvent::reward,prepared,snapshot.gems-control.bag);
        if(snapshot.authority.dead&&control.bag_known&&control.bag>0&&snapshot.gems==0)
            executable_service_fact(executable_policy::ServiceEvent::loss,prepared,control.bag);
        // A bank command is only a start. Completion needs observed native
        // score, whole-bag completion and an uninterrupted actual channel tail.
        const bool channel_join=control.last_observed_epoch==epoch-1&&
            control.last_channel==executable_policy::Channel::bank&&control.last_channel_ticks==1;
        if(control.bank_started&&channel_join&&!snapshot.authority.dead&&snapshot.gems==0&&
           snapshot.authority.channel==executable_policy::Channel::none&&prepared.submitted_actual&&
           prepared.submitted_gems>=control.bank_start_submitted&&
           prepared.submitted_gems-control.bank_start_submitted>=control.bank_amount){
            executable_service_fact(executable_policy::ServiceEvent::bank,prepared,control.bank_amount,true);
            control.bank_started=false;++control.episode;}
        control.bag=snapshot.gems;control.bag_known=true;
    }
    if(prepared.submitted_actual)control.submitted=prepared.submitted_gems;
    if(requested_group&&prepared.group_state==1)
        control.capture.alive(std::size_t(prepared.group_index),true);
    if(control.request.member>=0&&std::size_t(control.request.member)<members.size())
        for(unsigned n=0;n<snapshot.target_count;++n)if(snapshot.targets[n].kind==executable_policy::TargetKind::camp&&
           snapshot.targets[n].id==std::uint32_t(handle_camp_t(members[std::size_t(control.request.member)].handle))&&
           snapshot.targets[n].actual&&snapshot.targets[n].alive){
            control.capture.alive(std::size_t(members[std::size_t(control.request.member)].group),true);break;}
    if(prepared.home_actual&&prepared.on_home&&snapshot.health_actual&&
       prepared.body.health>=prepared.body.max_health&&service.stage()==executable_policy::Stage::recover)
        executable_service_fact(executable_policy::ServiceEvent::recovered,prepared);
    control.last_group=requested_group?prepared.group_index:-1;
    control.previous_on_region=requested_group&&snapshot.capture_actual&&snapshot.on_region;
    control.last_channel=snapshot.authority.channel;control.last_channel_ticks=snapshot.channel_timer_actual?snapshot.channel_ticks:0;
    control.last_observed_epoch=epoch;
}
bool executable_handoff(){
    auto& control=executable_controller_state();++control.handoffs;
    // Unwrapped legacy actions can change custody and placement. Preserve the
    // known bag/bank obligation, but never reuse this capture/channel chain as
    // evidence assigning a later reward or tribute to this owner.
    control.previous_on_region=false;control.last_group=-1;
    control.bank_started=false;control.last_channel=executable_policy::Channel::none;
    executable_observe_revoke();executable_execution_state().service.handoff_unknown();return false;
}
Info executable_legacy_body_or_query(){
    auto& control=executable_controller_state();
    if(control.legacy_body_current&&control.legacy_body_epoch==epoch){
        control.legacy_body_current=false;return control.legacy_body;}
    return body();
}
struct ExecutableLegacyGuard {
    Runtime* runtime;int bag;bool recovering;
    ~ExecutableLegacyGuard(){
        if constexpr(cfg::executable_policy){
        auto& control=runtime->executable_controller_state();
        // Only an actual action/custody/recovery transition reopens the new
        // producer. A job change or the next round cannot erase the latch.
        if(runtime->last_action_round==runtime->epoch||runtime->last_bag!=bag||
           (recovering&&!runtime->recovering))control.legacy_latched=false;
        control.legacy_body_current=false;
        }
    }
};
bool executable_legacy_return_owns_input(){
    const auto& before=model_return_state();const auto work=before.actual_work,moves=before.actual_moves,recalls=before.actual_recalls;
    const auto refused=before.actual_false;
    bool claimed=false;
    if(returning){
        const auto me=executable_legacy_body_or_query();const auto flags=state(me);
        if(!flags.is_dead()&&!flags.contains(champion_state::recalling)&&!flags.contains(champion_state::submitting_gems)&&
           !flags.contains(champion_state::teleporting)&&!self().buffs().has_stun()){
            if(exposure(point(me)).amount==0)returning=false;
            else {diag_stage(8);modeled_return_step(me);claimed=true;}}
    }
    const auto& after=model_return_state();
    // The measured backend remains available, but a completed search/model
    // menu alone cannot claim this input from the remaining macro dispatcher.
    return claimed&&(after.actual_work!=work||after.actual_moves!=moves||after.actual_recalls!=recalls||after.actual_false!=refused);
}

std::uint64_t executable_entry_tail(const ExecutableExecutionBudget& budget){
    return executable_family_max_tail(budget);
}
ExecutablePlanHandle executable_compile_current(const executable_policy::Policy& policy,
    const ExecutableExecutionBudget& budget,const ExecutableObservationRequest& request,std::uint64_t entry_tail){
    const auto compiler=executable_compilation_bound(budget);
    if(entry_tail<=compiler)return {};
    const auto handle=executable_compile_plan(policy,budget,request,entry_tail-compiler);
    if(!executable_observe_credit_compilation(executable_compilation_state().last))return {};
    return handle;
}
bool executable_capture_policy(executable_policy::Policy& policy,ExecutableObservationRequest& request,
    const executable_policy::Snapshot& actual,executable_policy::Point& next,executable_policy::Point& alternate,bool& have_step){
    auto& control=executable_controller_state();const int group=request.group;
    if(group<0||std::size_t(group)>=memory.size())return false;
    if(!control.capture.reward(std::size_t(group),unsigned(std::max(1,constants::capture_time)),true))return false;
    control.capture_group=group;control.capture_inputs=control.capture.remaining(std::size_t(group));
    policy.purpose=request.purpose=executable_policy::Purpose::capture;
    request.member=-1;request.need_group=request.need_bag=true;
    const auto& region=memory[std::size_t(group)];bool on_region=false;
    executable_policy::Point goal{};unsigned distance=std::numeric_limits<unsigned>::max();
    for(std::size_t n=region.begin;n<region.end;++n){const auto candidate=executable_point(members[n].pos);
        on_region=on_region||candidate==actual.pose;const auto d=executable_distance(actual.pose,candidate);
        if(d<distance){distance=d;goal=candidate;}}
    policy.success.count=1;policy.success.actions[0]={};
    if(on_region&&control.capture_inputs){
        request.phase=ExecutableMacroPhase::capture_wait;request.capture_inputs_remaining=control.capture_inputs;request.have_move=false;
        policy.success.actions[0].kind=executable_policy::ActionKind::wait;
        policy.success.actions[0].wait_reason=executable_policy::WaitReason::capture;
        policy.on_refusal.declared=policy.on_changed.declared=false;have_step=false;return true;
    }
    if(on_region||distance==std::numeric_limits<unsigned>::max()||
       !executable_geometry_step(actual.pose,goal,{},false,next)||
       !executable_geometry_step(actual.pose,goal,next,true,alternate))return false;
    request.phase=ExecutableMacroPhase::capture_approach;request.have_move=true;request.move=next;
    policy.success.actions[0]=executable_move_action(next);executable_move_closure(policy,alternate,next);have_step=true;return true;
}
bool executable_service_step(){
    auto& control=executable_controller_state();++control.calls;control.last_epoch=epoch;control.stage=0;
    control.input_started=false;control.legacy_body_current=false;
    const bool retained=control.legacy_latched||returning||recovering||task==Task::recover||job<0||
        (std::size_t(job)<memory.size()&&memory[std::size_t(job)].state==3&&epoch<memory[std::size_t(job)].until);
    const auto selection=executable_owner_selection(executable_execution_state().pending_feedback,retained);
    if(selection==ExecutableOwnerSelection::pending_feedback){
        // The previous SDK attempt owns its feedback obligation even when
        // FALSE preserved M or an applied prefix selected return/recovery.
        // Acquire current self facts, close/mark the old prefix Unknown, and
        // transfer only at a later fresh input. No new host command runs here.
        control.input_started=true;const auto pending_budget=executable_execution_default_budget();
        const auto tail=executable_add(pending_budget.commit_compute,pending_budget.price.final_tail);
        const auto& fresh=executable_observe_owner(tail,false);
        if(fresh.status!=ExecutableObservationStatus::ready||!fresh.owner_actual){++control.unsupported;return executable_handoff();}
        const auto reconciled=executable_reconcile_pending_feedback(pending_budget);
        if(!reconciled.feedback_closed)++control.unsupported;
        return executable_handoff();
    }
    // Select a retained legacy owner before new preparation. Partial new
    // preparation can hand off only at a fresh input, until actual progress.
    if(selection==ExecutableOwnerSelection::retained_legacy){
        ++control.legacy_bypasses;return executable_handoff();}
    const auto budget=executable_execution_default_budget();control.input_started=true;control.stage=1;
    auto& classified=executable_observe_classify(budget.price.final_tail);
    if(!classified.actual)return executable_handoff();
    if(classified.dead||classified.on_home){
        control.legacy_body=classified.body;control.legacy_body_current=true;control.legacy_body_epoch=epoch;
        control.input_started=false;++control.legacy_bypasses;return executable_handoff();}
    const auto entry_tail=executable_entry_tail(budget);
    if(!entry_tail||entry_tail==executable_policy::invalid_cost)return executable_handoff();
    control.required=entry_tail;control.max_required=std::max(control.max_required,entry_tail);
    auto& owner=executable_observe_owner(entry_tail,false);
    if(owner.status!=ExecutableObservationStatus::ready||!owner.owner_actual)return executable_handoff();
    const auto& initial=owner.snapshot;const auto& me=owner.body;
    control.stage=2;
    auto& service=executable_execution_state().service;
    const bool at_home=owner.home_actual&&owner.on_home;
    if(initial.authority.dead)service.recover();
    if(initial.authority.dead&&control.bag_known&&control.bag>0){
        // Death ends current custody. Unwrapped history may hide an earlier
        // tribute, so the old bag is not attributed as an exact loss amount.
        executable_service_fact(executable_policy::ServiceEvent::loss,owner,0);
        control.bag=0;control.bank_started=false;}
    if(initial.authority.channel==executable_policy::Channel::teleport)return executable_handoff();
    if(initial.authority.dead||(at_home&&initial.authority.channel==executable_policy::Channel::none)){
        executable_reconcile_service(owner);
        auto& request=control.request;request={};request.owner=service.key();request.need_home=request.need_bag=true;
        request.need_bank_receipt=control.bank_started;const auto& facts=executable_observe_prepare(request,entry_tail);
        executable_reconcile_service(facts);
        return executable_handoff();
    }
    const bool valid_job=job>=0&&std::size_t(job)<groups.size();
    const auto native_group=valid_job?std::uint64_t(handle_camp_group_t(groups[std::size_t(job)])):executable_policy::invalid_cost;
    const unsigned known_bag=control.bag_known?control.bag:unsigned(std::max(0,last_bag));
    const bool bank_due=known_bag>0&&(task==Task::bank||known_bag>=30||
        me.health<0.6f*me.max_health||epoch>int(constants::max_rounds)-150);
    const bool banking=bank_due||initial.authority.channel==executable_policy::Channel::bank;
    const auto purpose=banking?executable_policy::Purpose::bank:
        (returning||recovering?executable_policy::Purpose::recover:executable_policy::Purpose::work);
    const executable_policy::ServiceKey key{initial.self,banking?executable_policy::invalid_cost:native_group,control.episode};
    service.request(key,purpose);
    if(returning||recovering)service.recover();
    auto& policy=control.policy;auto& request=control.request;
    executable_policy_reset(policy,key,purpose);request={};request.owner=key;request.purpose=purpose;
    request.need_bag=banking;request.need_home=purpose==executable_policy::Purpose::recover;request.need_bank_receipt=banking;
    request.group=valid_job&&!banking?job:-1;
    request.need_group=request.group>=0&&(memory[std::size_t(request.group)].state==2||memory[std::size_t(request.group)].state==3);
    if(request.need_group)request.need_bag=true;
    if(request.group>=0)request.group_witness_member=int(memory[std::size_t(request.group)].begin);
    executable_policy::Point next{},alternate{};bool have_step=false;int selected=-1;
    int probe_offset=-1;
    if(!banking&&initial.authority.channel==executable_policy::Channel::none&&request.group>=0&&
       executable_nominate_member(initial,request.group)<0){
        // Dead members are not an observed Group Reward. Reach this paid
        // dependency before requiring placeholder return geometry.
        request.need_group=true;request.need_bag=true;
        const auto& group_facts=executable_observe_prepare(request,entry_tail,true);
        executable_reconcile_service(group_facts);
        if(!group_facts.group_actual||group_facts.group_index!=request.group)return executable_handoff();
        if(group_facts.group_state==1){const auto& group=memory[std::size_t(request.group)];
            probe_offset=control.probes.begin(std::size_t(request.group),unsigned(group.end-group.begin),
                control.episode,initial.pose.x,initial.pose.y,true);
            if(probe_offset<0)return executable_handoff();
        }else control.probes.changed_lifecycle(std::size_t(request.group),true);
    }
    const bool channel=initial.authority.channel!=executable_policy::Channel::none;
    const auto& observed_group=executable_observe_prepared();
    const bool fresh_reward=observed_group.group_actual&&observed_group.group_index==request.group&&
        observed_group.group_state==2&&observed_group.snapshot.stamp.epoch==std::uint64_t(epoch)&&
        executable_policy::Revision(observed_group.snapshot.group)==key.job;
    if(channel){request.phase=initial.authority.channel==executable_policy::Channel::bank?
        ExecutableMacroPhase::bank_channel:ExecutableMacroPhase::recall_channel;
        policy.success.count=1;policy.success.actions[0]={};policy.success.actions[0].kind=executable_policy::ActionKind::wait;
        policy.success.actions[0].wait_reason=executable_policy::WaitReason::channel;
    }else if(fresh_reward){
        if(!executable_capture_policy(policy,request,initial,next,alternate,have_step))return executable_handoff();
    }else if(banking){
        request.phase=ExecutableMacroPhase::bank_approach;bool found=false;unsigned nearest=std::numeric_limits<unsigned>::max();
        executable_policy::Point altar{};for(const auto location:altars){const auto candidate=executable_point(location);
            const auto distance=executable_distance(initial.pose,candidate);if(!found||distance<nearest){found=true;nearest=distance;altar=candidate;}}
        if(!found)return executable_handoff();
        request.altar=altar;request.have_altar=nearest<=unsigned(constants::submit_gems_range);
        have_step=executable_geometry_step(initial.pose,altar,{},false,next)&&executable_geometry_step(initial.pose,altar,next,true,alternate);
        if(request.have_altar){policy.success.count=1;policy.success.actions[0]={};policy.success.actions[0].kind=executable_policy::ActionKind::bank;
            policy.success.actions[0].target_kind=executable_policy::TargetKind::ground;policy.success.actions[0].point=altar;}
        else if(have_step){request.move=next;request.have_move=true;policy.success.count=1;policy.success.actions[0]=executable_move_action(next);}
        else return executable_handoff();
    }else{
        selected=probe_offset>=0?int(memory[std::size_t(job)].begin)+probe_offset:executable_nominate_member(initial,job);
        control.last_member=selected;
        if(selected>=0){const auto destination=executable_point(members[std::size_t(selected)].pos);
            const bool attack_distance=executable_distance(initial.pose,destination)<=unsigned(std::max(0,initial.attack_range));
            if(attack_distance&&executable_policy::guard_allows(executable_policy::Guard::usable_attack,initial)){
                request.member=selected;request.phase=ExecutableMacroPhase::work;
                policy.success.count=1;policy.success.actions[0]={};policy.success.actions[0].kind=executable_policy::ActionKind::basic;
                policy.success.actions[0].target_kind=executable_policy::TargetKind::camp;
                policy.success.actions[0].target_id=std::uint32_t(handle_camp_t(members[std::size_t(selected)].handle));
                have_step=executable_return_nomination(initial,next,alternate);
                if(initial.attack_range==1&&executable_policy::guard_allows(executable_policy::Guard::usable_move,initial)&&have_step){
                    request.move=next;request.have_move=true;policy.success.actions[policy.success.count++]=executable_move_action(next);}
            }else if(executable_policy::guard_allows(executable_policy::Guard::usable_move,initial)){
                request.phase=ExecutableMacroPhase::work_approach;request.member=-1;
                have_step=executable_geometry_step(initial.pose,destination,{},false,next)&&
                    executable_geometry_step(initial.pose,destination,next,true,alternate);
                if(have_step){request.move=next;request.have_move=true;policy.success.count=1;policy.success.actions[0]=executable_move_action(next);}
            }
        }
        if(!policy.success.count){
            have_step=executable_return_nomination(initial,next,alternate);
            if(have_step&&executable_policy::guard_allows(executable_policy::Guard::usable_move,initial)){
                request.phase=ExecutableMacroPhase::returning;request.have_move=true;request.move=next;
                policy.success.count=1;policy.success.actions[0]=executable_move_action(next);}
            else return executable_handoff();
        }
    }
    if(!channel&&request.phase!=ExecutableMacroPhase::capture_wait){if(!have_step)have_step=executable_return_nomination(initial,next,alternate);
        if(!have_step)return executable_handoff();executable_move_closure(policy,alternate,next);}
    control.stage=3;auto plan=executable_compile_current(policy,budget,request,entry_tail);
    auto required=executable_plan_required(plan);
    if(!required||required==executable_policy::invalid_cost)return executable_handoff();
    control.required=required;control.max_required=std::max(control.max_required,required);control.phase=request.phase;
    control.stage=4;auto* prepared=&executable_observe_prepare(request,required,true);
    executable_reconcile_service(*prepared);
    if(selected>=0){
        bool target_read=false,dead=false;
        const auto native=std::uint32_t(handle_camp_t(members[std::size_t(selected)].handle));
        for(unsigned n=0;n<prepared->snapshot.target_count;++n){const auto& target=prepared->snapshot.targets[n];
            if(target.kind==executable_policy::TargetKind::camp&&target.id==native){target_read=true;dead=target.actual&&!target.alive;}}
        if(probe_offset>=0)control.probes.observed(std::size_t(job),unsigned(probe_offset),target_read);
        if(dead&&!prepared->group_actual){
            request.need_group=true;request.need_bag=true;
            plan=executable_compile_current(policy,budget,request,entry_tail);required=executable_plan_required(plan);
            if(!required||required==executable_policy::invalid_cost)return executable_handoff();
            prepared=&executable_observe_prepare(request,required,true);executable_reconcile_service(*prepared);
        }
    }
    // An unavailable optional egress cannot erase an executable attack.
    if(prepared->status!=ExecutableObservationStatus::ready&&policy.success.count==2&&
       policy.success.actions[0].kind==executable_policy::ActionKind::basic){
        policy.success.count=1;policy.on_refusal.declared=false;request.have_move=false;
        plan=executable_compile_current(policy,budget,request,entry_tail);required=executable_plan_required(plan);
        if(required&&required!=executable_policy::invalid_cost){prepared=&executable_observe_prepare(request,required,true);
            executable_reconcile_service(*prepared);}
    }
    const auto& actual=prepared->snapshot;
    if(!banking&&actual.gems_actual&&actual.gems>0&&(task==Task::bank||actual.gems>=30||
       prepared->body.health<0.6f*prepared->body.max_health||epoch>int(constants::max_rounds)-150)){
        task=Task::bank;return executable_handoff();}
    if(prepared->owner_actual&&actual.body_actual&&actual.stamp.epoch==std::uint64_t(epoch)&&
       prepared->group_actual&&prepared->group_index==request.group&&request.group>=0&&
       std::size_t(request.group)<memory.size()&&executable_policy::Revision(prepared->snapshot.group)==key.job&&
       prepared->group_state==2&&!banking&&!channel){
        control.stage=5;
        if(!executable_capture_policy(policy,request,actual,next,alternate,have_step))return executable_handoff();
        plan=executable_compile_current(policy,budget,request,entry_tail);required=executable_plan_required(plan);
        if(!required||required==executable_policy::invalid_cost)return executable_handoff();
        prepared=&executable_observe_prepare(request,required,true);
        executable_reconcile_service(*prepared);
    }
    if(prepared->status!=ExecutableObservationStatus::ready){++control.need_observation;return executable_handoff();}
    ++control.prepared;
    policy.token=prepared->token;policy.actual_prefix=service.actual_prefix();control.valuation=prepared->valuation;
    if(!executable_observe_finish_preparation(required)){++control.unsupported;return executable_handoff();}
    const int observed_reward_group=prepared->group_actual&&prepared->group_index==request.group&&request.group>=0&&
        executable_policy::Revision(prepared->snapshot.group)==key.job&&prepared->group_state==2?prepared->group_index:-1;
    control.stage=6;++control.selected;const auto outcome=executable_execute_plan(policy,plan);control.stage=7;
    if(outcome.actual_effect&&outcome.last_receipt.action.kind==executable_policy::ActionKind::bank&&
       outcome.last_receipt.outcome==executable_policy::Outcome::accepted){
        control.bank_started=true;control.bank_amount=control.bag;control.bank_start_submitted=control.submitted;control.bank_epoch=epoch;}
    const auto& after=executable_observe_snapshot();
    if(after.body_actual&&after.channel_actual){control.last_channel=after.authority.channel;
        control.last_channel_ticks=after.channel_timer_actual?after.channel_ticks:0;control.last_observed_epoch=epoch;}
    if(outcome.actual_effect&&outcome.last_receipt.actual_body&&observed_reward_group>=0){
        const auto& capture=memory[std::size_t(observed_reward_group)];bool region=false;
        for(std::size_t n=capture.begin;n<capture.end;++n)region=region||executable_point(members[n].pos)==after.pose;
        control.previous_on_region=region;control.last_group=observed_reward_group;}
    if(outcome.finite_wait&&request.phase==ExecutableMacroPhase::capture_wait&&request.group>=0){
        control.capture.waited(std::size_t(request.group),true);
        control.capture_inputs=control.capture.remaining(std::size_t(request.group));}
    if(outcome.actual_effect){const auto potential=exposure({after.pose.x,after.pose.y});
        if(potential.valid&&potential.amount>0)returning=true;}
    if(outcome.actual_effect||outcome.finite_wait){++control.owned;return true;}
    if(outcome.refusals>=2){++control.unsupported;control.legacy_latched=true;return true;}
    return executable_handoff();
}
void executable_diagnostic_end(){
    if constexpr(cfg::trace>=7){
        auto& control=executable_controller_state();const auto& execution=executable_execution_state();
        const bool first=execution.input_epoch==std::uint64_t(epoch)&&execution.input_attempts&&
            control.first_packets<2&&execution.attempts!=control.last_emit_attempts;
        const bool periodic=epoch==128||epoch==512||epoch==1024||epoch==1536||epoch==1999;
        if(!first&&!periodic)return;
        if(!fuel(20000))return;
        const auto remaining=fuel_remaining();const auto current=body();const auto flags=state(current);
        const auto& observation=executable_observe_state();const auto& stats=executable_observe_stats();
        const auto& compilation=executable_compilation_state();const auto& service=execution.service;
        std::uint64_t max_import=0;for(const auto& cost:stats.costs)max_import=std::max(max_import,cost.max_query);
        std::array<std::uint32_t,80> packet{};packet[0]=0x3943444du;packet[1]=(1u<<16)|320u;
        packet[2]=unsigned(epoch);packet[3]=std::uint32_t(handle_champion_t(self()));
        packet[4]=unsigned(current.x);packet[5]=unsigned(current.y);std::memcpy(&packet[6],&current.health,sizeof(current.health));
        packet[7]=flags.bits();packet[8]=unsigned(job);packet[9]=unsigned(idle);packet[10]=unsigned(task);
        packet[11]=returning;packet[12]=recovering;packet[13]=control.policy.success.count;packet[14]=unsigned(control.request.member);
        packet[15]=executable_observe_snapshot().target_count;packet[16]=executable_observe_snapshot().move_count;
        packet[17]=unsigned(control.phase);packet[18]=unsigned(execution.last.status);packet[19]=unsigned(remaining);packet[20]=0x45503138u;
        const std::array<std::uint64_t,59> values{control.calls,control.prepared,control.selected,control.owned,control.handoffs,
            control.need_observation,control.unsupported,execution.calls,execution.words,execution.fallbacks,execution.attempts,
            execution.applied,execution.refused,execution.unfunded,execution.overruns,execution.feedback_failed,
            execution.phase_max[1],execution.phase_max[2],execution.phase_max[3],execution.phase_max[4],execution.phase_max[5],execution.phase_max[6],
            unsigned(execution.first_failure.failure),unsigned(execution.first_failure_phase),execution.first_failure.primitive,
            execution.first_failure.stamp.epoch,execution.first_failure.stamp.host_attempt,unsigned(service.stage()),unsigned(service.next_required()),
            service.key().job,service.key().episode,service.actual_prefix(),service.refusal_debt(),service.trail_complete(),service.bank_pending(),
            unsigned(execution.last.status),execution.last.closed,execution.last.needs_continuation,execution.last.raw_m_before,
            execution.last.raw_m_after,execution.last.restricted_move_after,control.required,control.max_required,compilation.maximum,
            compilation.failed,stats.prepares,stats.ready,stats.partial,stats.unknown,stats.maximum,observation.max_preparation_spent,
            max_import,stats.costs[unsigned(ExecutableObservationStats::Phase::prepare)].maximum,execution.input_epoch,
            executable_observe_snapshot().stamp.host_attempt,executable_observe_snapshot().stamp.effect,control.legacy_latched,
            control.deferred_handoffs,control.stage};
        for(std::size_t n=0;n<values.size();++n)packet[21+n]=unsigned(values[n]);
        emit_packet(packet);control.last_emit_attempts=execution.attempts;if(first)++control.first_packets;
    }
}
#endif
