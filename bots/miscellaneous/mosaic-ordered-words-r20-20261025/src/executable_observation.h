// Included inside Runtime. Actual observations support an operational policy;
// no joint World, catalogue compilation or numeric risk quote is a prerequisite.
struct ExecutableObservationCone {
    static std::uint64_t cost(unsigned queries,std::uint64_t query_bound,std::uint64_t compute,std::uint64_t tail){
        const auto bad=executable_policy::invalid_cost;if(!query_bound||!compute||!tail)return bad;
        if(queries&&query_bound>bad/queries)return bad;const auto query=queries*query_bound;
        if(compute>bad-query||tail>bad-query-compute)return bad;return query+compute+tail;
    }
    static bool admitted(std::uint64_t fuel,unsigned queries,std::uint64_t query_bound,std::uint64_t compute,std::uint64_t tail){
        const auto required=cost(queries,query_bound,compute,tail);return required!=executable_policy::invalid_cost&&fuel>required;
    }
};
struct ExecutableObservationPreparation {
    executable_policy::Stamp stamp{};std::uint64_t entry{},query_base{},limit{},compile_paid{};
    std::uint64_t last_serial{},last_before{},last_after{};bool active{},failed{};
    void begin(executable_policy::Stamp physical,std::uint64_t fuel,std::uint64_t queries,std::uint64_t bound){
        // An overrun cannot buy a new preparation clock through a rewrite.
        if(failed&&executable_policy::same_physical(stamp,physical))return;
        stamp=physical;entry=fuel;query_base=queries;limit=bound;
        compile_paid=last_serial=last_before=0;last_after=fuel;failed=false;
        active=bound&&bound!=executable_policy::invalid_cost&&fuel!=executable_policy::invalid_cost&&queries!=executable_policy::invalid_cost;
    }
    bool matches(const executable_policy::Stamp& physical)const{return active&&!failed&&executable_policy::same_physical(stamp,physical);}
    bool known_credit(const executable_policy::Stamp& physical,std::uint64_t before,std::uint64_t after,std::uint64_t serial)const{
        return matches(physical)&&serial&&serial==last_serial&&before==last_before&&after==last_after;
    }
    // Only an externally PAID PURE phase may supply this receipt. In particular
    // its interval contains no SDK imports. Keep the credit separate from the
    // measured import counter; the adapter owns that source-level contract.
    bool credit(const executable_policy::Stamp& physical,std::uint64_t before,std::uint64_t after,
                std::uint64_t now,std::uint64_t serial,bool paid){
        const auto bad=executable_policy::invalid_cost;
        if(!matches(physical)||!paid||!serial||serial==bad||serial<=last_serial||before==bad||after==bad||now==bad||
           before>entry||after>before||now>after||before>last_after)return false;
        const auto amount=before-after;if(amount>bad-compile_paid)return false;
        compile_paid+=amount;last_serial=serial;last_before=before;last_after=after;return true;
    }
    std::uint64_t spent(const executable_policy::Stamp& physical,std::uint64_t fuel,std::uint64_t queries)const{
        const auto bad=executable_policy::invalid_cost;
        if(!matches(physical)||fuel==bad||queries==bad||fuel>entry||queries<query_base)return bad;
        const auto elapsed=entry-fuel,imports=queries-query_base;
        if(imports>elapsed||compile_paid>elapsed-imports)return bad;return elapsed-imports-compile_paid;
    }
    std::uint64_t remaining(const executable_policy::Stamp& physical,std::uint64_t fuel,std::uint64_t queries)const{
        const auto used=spent(physical,fuel,queries);return used==executable_policy::invalid_cost||used>limit?executable_policy::invalid_cost:limit-used;
    }
    bool finish(const executable_policy::Stamp& physical,std::uint64_t fuel,std::uint64_t queries){
        const bool okay=remaining(physical,fuel,queries)!=executable_policy::invalid_cost;
        active=false;if(!okay)failed=true;return okay;
    }
    void close(){active=false;}
};
struct ExecutableObservationClassificationTag {
    executable_policy::Stamp stamp{};bool actual{};
    bool current(const executable_policy::Stamp& physical)const{
        return actual&&executable_policy::same_physical(stamp,physical);
    }
};
struct ExecutableOwnerDraft {
    executable_policy::NativeId self{};executable_policy::Point pose{};executable_policy::ActualAuthority authority{};
    float health{},shield{};int attack_range{1};bool body_read{},restrictions_read{},buffs_read{};
};
struct ExecutableTargetDraft {
    executable_policy::TargetFact fact;bool visibility_read{},life_read{},health_read{},shield_read{};
};
struct ExecutableMoveDraft {
    executable_policy::MoveFact fact;bool geometry_read{},visibility_read{},visible{},champion_read{},camp_read{},teleport_read{};
    bool champion{},camp{},teleport{};
};
struct ExecutableObservationFacts {
    static bool publish_owner(executable_policy::Snapshot& out,const ExecutableOwnerDraft& draft,
                              executable_policy::Stamp stamp){
        if(!draft.body_read||!draft.restrictions_read||!draft.buffs_read)return false;
        out.stamp=stamp;out.self=draft.self;out.pose=draft.pose;out.authority=draft.authority;out.attack_range=draft.attack_range;
        out.body_actual=out.restrictions_actual=out.channel_actual=true;
        out.health_actual=out.shield_actual=true;out.health=draft.health;out.shield=draft.shield;return true;
    }
    static bool publish_target(executable_policy::Snapshot& out,const ExecutableTargetDraft& draft){
        if(!draft.visibility_read||(draft.fact.visible&&(!draft.life_read||(draft.fact.alive&&!draft.health_read))))return false;
        unsigned at=0;while(at<out.target_count&&(out.targets[at].kind!=draft.fact.kind||out.targets[at].id!=draft.fact.id))++at;
        if(at==executable_policy::target_capacity)return false;
        auto fact=draft.fact;fact.actual=fact.visible&&draft.life_read;
        fact.health_actual=fact.actual&&(!fact.alive||draft.health_read);if(!fact.alive)fact.health=0;
        fact.shield_actual=fact.actual&&draft.shield_read;
        if(!fact.visible){fact.alive=false;fact.health_actual=fact.shield_actual=false;}
        out.targets[at]=fact;if(at==out.target_count)++out.target_count;return true;
    }
    static bool publish_move(executable_policy::Snapshot& out,const ExecutableMoveDraft& draft){
        if(!draft.geometry_read)return false;
        auto fact=draft.fact;
        if(!fact.reachable){fact.actual=true;fact.open=false;}
        else{
            if(!draft.visibility_read)return false;
            if(!draft.visible){fact.actual=false;fact.open=false;}
            else{if(!draft.champion_read||!draft.camp_read||!draft.teleport_read)return false;
                fact.actual=true;fact.open=!draft.champion&&!draft.camp&&!draft.teleport;}
        }
        unsigned at=0;while(at<out.move_count&&out.moves[at].point!=fact.point)++at;
        if(at==executable_policy::move_capacity)return false;
        out.moves[at]=fact;if(at==out.move_count)++out.move_count;return true;
    }
    static bool publish_refused_move(executable_policy::Snapshot& out,executable_policy::Point point,
        const executable_policy::Stamp& before,executable_policy::Point before_pose,
        const executable_policy::ActualAuthority& authority,bool attempted,bool refused,int sdk_code){
        if(!attempted||!refused||sdk_code||!out.body_actual||!out.restrictions_actual||!out.channel_actual||
           out.pose!=before_pose||out.authority!=authority||out.stamp.epoch!=before.epoch||out.stamp.frame!=before.frame||
           out.stamp.effect!=before.effect||out.stamp.observation<before.observation||
           before.host_attempt==executable_policy::invalid_cost||out.stamp.host_attempt!=before.host_attempt+1)return false;
        unsigned at=0;while(at<out.move_count&&out.moves[at].point!=point)++at;if(at==executable_policy::move_capacity)return false;
        // Native FALSE is an alternative actual local-blocker receipt, not
        // four invented per-tile SDK reads or a fabricated unit identity.
        out.moves[at]={point,true,true,false};if(at==out.move_count)++out.move_count;return true;
    }
    static void after_attempt(executable_policy::Snapshot& out){
        out.authority_lease=0;out.body_actual=out.restrictions_actual=out.channel_actual=false;
        out.health_actual=out.shield_actual=false;out.target_count=out.move_count=out.ground_count=0;
        for(auto& ability:out.abilities){ability.actual=false;ability.cooldown_actual=false;}
        out.gems_actual=out.bank_target_actual=out.observation_actual=out.capture_actual=out.channel_timer_actual=false;
    }
    template<class Packet> static void invalidate_packet(Packet& packet){
        after_attempt(packet.snapshot);packet.token.valid=false;
        packet.owner_actual=packet.group_actual=packet.submitted_actual=packet.home_actual=packet.retained_incumbent=false;
    }
};
enum class ExecutableObservationStatus {empty,partial,ready,unknown,blocked,stale,overrun,unsupported};
enum class ExecutableMacroPhase {work,work_approach,capture_approach,capture_wait,bank_approach,bank_channel,returning,recall_channel,recovery,complete};
struct ExecutableObservationRequest {
    executable_policy::ServiceKey owner{};
    executable_policy::Purpose purpose{executable_policy::Purpose::work};
    ExecutableMacroPhase phase{ExecutableMacroPhase::work};
    int member{-1},group{-1},group_witness_member{-1};
    executable_policy::NativeId enemy{};bool have_enemy{};
    executable_policy::Point move{},altar{};bool have_move{},have_altar{};
    bool need_group{},need_bag{},need_bank_receipt{},need_home{},need_target_shield{};
    unsigned capture_inputs_remaining{};
};
struct ExecutableObservationQueryBounds {unsigned validation{},feedback{},refusal_close{};};
static ExecutableObservationQueryBounds executable_observe_query_bounds(const executable_policy::Action& action,
    const ExecutableObservationRequest& request){
    using K=executable_policy::ActionKind;using T=executable_policy::TargetKind;ExecutableObservationQueryBounds out;
    if(action.kind==K::basic){
        const bool requested=action.target_kind==T::camp?request.member>=0:request.have_enemy&&request.enemy==action.target_id;
        out.validation=requested?0:action.target_kind==T::camp?2:1;
        out.feedback=3+(action.target_kind==T::camp?2:1)+unsigned(request.need_target_shield);
    }else{out.feedback=3;
        if(action.kind==K::move)out.validation=4;
        if(action.kind==K::bank)out.validation=2;
        if(action.kind==K::wait){out.feedback=0;out.validation=action.wait_reason==executable_policy::WaitReason::channel?
            (request.phase==ExecutableMacroPhase::bank_channel||request.phase==ExecutableMacroPhase::recall_channel?0:1):request.need_group?0:3;}
    }
    if(action.kind==K::bank||action.kind==K::recall)++out.feedback;
    return out;
}
static unsigned executable_observe_prepare_query_bound(const ExecutableObservationRequest& request,bool owner_needed=true){
    unsigned count=owner_needed?3:0;
    if(request.member>=0)count+=2+unsigned(request.need_target_shield);
    if(request.have_enemy)count+=1+unsigned(request.need_target_shield);
    if(request.have_move)count+=4;if(request.need_group)count+=3;
    if(request.need_bag||(request.have_altar&&request.purpose==executable_policy::Purpose::bank))++count;
    if(request.need_bank_receipt)++count;if(request.need_home)++count;if(request.have_altar)++count;
    if(request.phase==ExecutableMacroPhase::bank_channel||request.phase==ExecutableMacroPhase::recall_channel)++count;
    return count;
}
static constexpr std::uint64_t executable_observe_query_bound=2000,executable_observe_compute_bound=6000;
#ifndef MOSAIC_EXECUTABLE_OBSERVATION_PURE_ONLY
struct ExecutableClassification:ExecutableObservationClassificationTag {
    Info body{};
    bool dead{},home_actual{},on_home{};unsigned queries{};std::uint64_t fuel{};
};
struct ExecutablePrepared {
    ExecutableObservationStatus status{ExecutableObservationStatus::empty};
    ExecutableMacroPhase phase{ExecutableMacroPhase::work};
    executable_policy::ServiceKey owner{};executable_policy::Purpose purpose{executable_policy::Purpose::work};
    executable_policy::Snapshot snapshot;executable_policy::Token token;
    executable_policy::Valuation valuation; // UNKNOWN by default; no numeric safety claim.
    bool owner_actual{};Info body{};
    bool group_actual{};unsigned group_state{},group_gems{};int group_index{-1};std::uint64_t group_receipt{};
    bool submitted_actual{};unsigned submitted_gems{};
    bool home_actual{},on_home{};
    bool retained_incumbent{};
    unsigned queries{};
};
struct ExecutableObservationStats {
    enum class Phase:unsigned {owner,target,move,group,bag,bank,home,prepare,validate,feedback,close,commit,count};
    struct Cost {std::uint64_t calls{},queries{},spent{},maximum{},max_query{},overruns{};};
    std::array<Cost,unsigned(Phase::count)> costs{};
    unsigned prepares{},ready{},partial{},unknown{},attempts{},accepted{},refused{},leases{},revoked{},member_receipts{},group_receipts{};
    unsigned last_queries{};std::uint64_t last_spent{},maximum{},last_bound{};
    int epoch{-1};Phase phase{Phase::owner};
};
struct ExecutableObservationState {
    ExecutablePrepared prepared,result;ExecutableObservationRequest request;ExecutableClassification classification;
    ExecutableObservationStats stats;executable_policy::Lease lease;
    executable_policy::Stamp before_stamp{};executable_policy::Point before_pose{};
    executable_policy::ActualAuthority before_authority{};
    std::uint64_t frame{},effect{},observation{},lease_serial{},receipt_serial{},committed_serial{};
    ExecutableObservationPreparation preparation;std::uint64_t query_spent{},preparation_spent{},max_preparation_spent{};
    std::uint64_t compilation_credits{},compilation_credit_rejections{};
    executable_policy::Stamp committed_before{};executable_policy::ServiceKey committed_owner{};
    executable_policy::Action committed_action{};executable_policy::Outcome committed_outcome{executable_policy::Outcome::changed_premise};
    struct Binding {executable_policy::NativeId id{};int member{-1};};
    std::array<Binding,executable_policy::target_capacity> bindings{};unsigned binding_count{};
    std::uint64_t phase_start{},phase_tail{},phase_bound{};unsigned phase_queries{},phase_query_limit{};
    std::uint64_t waited_frame{executable_policy::invalid_cost};
    bool attempt_pending{},last_refused{};int input_epoch{-1},refused_epoch{-1};executable_policy::Point refused_point{};
    executable_policy::ActionKind pending_kind{executable_policy::ActionKind::wait};bool pending_applied{},pending_effect_published{};
    ExecutableObservationStatus status{ExecutableObservationStatus::empty};
};
static ExecutableObservationState& executable_observe_state(){static constinit ExecutableObservationState value{};return value;}
const executable_policy::Snapshot& executable_observe_snapshot()const{return executable_observe_state().prepared.snapshot;}
executable_policy::Token executable_observe_token()const{return executable_observe_state().lease.token();}
const Info& executable_observe_body()const{return executable_observe_state().prepared.body;}
const ExecutablePrepared& executable_observe_prepared()const{return executable_observe_state().prepared;}
const ExecutableObservationStats& executable_observe_stats()const{return executable_observe_state().stats;}
void executable_observe_revoke(){auto& state=executable_observe_state();
    if(state.lease.valid())++state.stats.revoked;state.lease.revoke();state.prepared.token.valid=false;
    state.prepared.snapshot.authority_lease=0;state.classification.actual=state.classification.home_actual=false;
}
void executable_observe_epoch_failure(){
    auto& observed=executable_observe_state();observed.status=ExecutableObservationStatus::stale;executable_observe_revoke();
    ExecutableObservationFacts::invalidate_packet(observed.prepared);
    ExecutableObservationFacts::invalidate_packet(observed.result);observed.preparation.close();
    // Retain attempt generation/debt. A resumed current body must never be
    // relabelled as an actual observation from the suspended old owner input.
}
void executable_observe_enter(){
    auto& state=executable_observe_state();if(state.input_epoch==epoch)return;
    executable_observe_revoke();state.input_epoch=epoch;++state.frame;state.stats.epoch=epoch;
    auto& snapshot=state.prepared.snapshot;const auto attempt=snapshot.stamp.host_attempt;
    snapshot.stamp={std::uint64_t(epoch),state.frame,state.effect,state.observation,attempt};
    ExecutableObservationFacts::invalidate_packet(state.prepared);ExecutableObservationFacts::invalidate_packet(state.result);
    state.preparation.close();
    state.prepared.status=ExecutableObservationStatus::empty;state.status=ExecutableObservationStatus::empty;
    if(!state.attempt_pending)state.binding_count=0;
}
template<class F> bool executable_observe_run(ExecutableObservationStats::Phase phase,unsigned queries,
                                             std::uint64_t tail,F&& read,std::uint64_t compute_bound=executable_observe_compute_bound,
                                             bool shared_preparation=false){
    executable_observe_enter();auto& state=executable_observe_state();const auto start=fuel_remaining();
    if(int(round_number())!=epoch){executable_observe_epoch_failure();return false;}
    if(shared_preparation){
        if(!state.preparation.matches(state.prepared.snapshot.stamp))
            state.preparation.begin(state.prepared.snapshot.stamp,start,state.query_spent,executable_observe_compute_bound);
        compute_bound=state.preparation.remaining(state.prepared.snapshot.stamp,start,state.query_spent);
        if(compute_bound==executable_policy::invalid_cost||!compute_bound){state.status=ExecutableObservationStatus::overrun;return false;}
    }
    if(!ExecutableObservationCone::admitted(start,queries,executable_observe_query_bound,compute_bound,tail)){
        state.status=ExecutableObservationStatus::partial;return false;}
    state.stats.phase=phase;state.phase_start=start;state.phase_tail=tail;state.phase_queries=0;state.phase_query_limit=queries;
    // The caller's phase declaration is the SAME pure-work bound used by the
    // executor's whole-branch funding. It is never replaced by an entry floor.
    state.phase_bound=queries*executable_observe_query_bound+compute_bound;
    ++state.stats.costs[unsigned(phase)].calls;state.status=ExecutableObservationStatus::ready;
    const bool ok=read();const auto now=fuel_remaining();const auto spent=start>=now?start-now:executable_policy::invalid_cost;
    auto& cost=state.stats.costs[unsigned(phase)];cost.spent+=spent;cost.maximum=std::max(cost.maximum,spent);
    state.stats.last_queries=state.phase_queries;state.stats.last_spent=spent;state.stats.last_bound=state.phase_bound;
    state.stats.maximum=std::max(state.stats.maximum,spent);
    if(int(round_number())!=epoch){executable_observe_epoch_failure();return false;}
    if(shared_preparation){
        state.preparation_spent=state.preparation.spent(state.prepared.snapshot.stamp,now,state.query_spent);
        state.max_preparation_spent=std::max(state.max_preparation_spent,state.preparation_spent);
        if(state.preparation.remaining(state.prepared.snapshot.stamp,now,state.query_spent)==executable_policy::invalid_cost){
            ++cost.overruns;state.status=ExecutableObservationStatus::overrun;return false;}
    }
    if(spent>state.phase_bound||now<=tail){++cost.overruns;state.status=ExecutableObservationStatus::overrun;
        if(now<=tail)executable_observe_revoke();return false;}
    return ok;
}
template<class F> bool executable_observe_query(ExecutableObservationStats::Phase label,F&& read,int& code){
    auto& state=executable_observe_state();
    if(int(round_number())!=epoch){executable_observe_epoch_failure();return false;}
    if(state.phase_queries>=state.phase_query_limit||fuel_remaining()<=state.phase_tail){
        state.status=ExecutableObservationStatus::overrun;return false;}
    const auto before=fuel_remaining();code=read();const auto after=fuel_remaining();
    ++state.phase_queries;++state.stats.costs[unsigned(label)].queries;++state.observation;
    auto& snapshot=state.prepared.snapshot;snapshot.stamp.observation=state.observation;
    if(state.lease.valid())state.lease.observe(snapshot);
    const auto spent=before>=after?before-after:executable_policy::invalid_cost;
    state.query_spent=spent>executable_policy::invalid_cost-state.query_spent?executable_policy::invalid_cost:state.query_spent+spent;
    auto& cost=state.stats.costs[unsigned(label)];cost.max_query=std::max(cost.max_query,spent);
    if(int(round_number())!=epoch){executable_observe_epoch_failure();return false;}
    if(spent>executable_observe_query_bound||after<=state.phase_tail){
        ++cost.overruns;state.status=ExecutableObservationStatus::overrun;if(after<=state.phase_tail)executable_observe_revoke();return false;}
    return true;
}
static bool executable_observe_hidden(int code){return code==int(error_code::api_not_in_vision);}
bool executable_observe_read_owner(bool publish_lease=true){
    using P=ExecutableObservationStats::Phase;auto& state=executable_observe_state();auto& prepared=state.prepared;
    auto& snapshot=prepared.snapshot;const auto owner=std::uint32_t(handle_champion_t(self()));int code=0;
    Info body{};ExecutableOwnerDraft draft;const bool classified=executable_observe_classification_ready();
    const bool classified_home=classified&&state.classification.home_actual,on_home=state.classification.on_home;
    if(classified)body=state.classification.body;
    else{if(!executable_observe_query(P::owner,[&]{return detail::sdk_champion(owner,&body);},code))return false;detail::check(code);}
    const auto flags=state_from_observation(body);
    draft.self=owner;draft.pose={body.x,body.y};draft.health=body.health;draft.attack_range=body.attack_range;draft.body_read=true;
    draft.authority.dead=flags.is_dead();draft.authority.raw_m=flags.contains(champion_state::can_move);
    draft.authority.raw_a=flags.contains(champion_state::can_attack);draft.authority.raw_c=flags.contains(champion_state::can_cast);
    draft.authority.walked=walked||(state.attempt_pending&&state.pending_applied&&state.pending_kind==executable_policy::ActionKind::move&&
        state.before_stamp.epoch==std::uint64_t(epoch)&&state.before_stamp.frame==state.frame);
    draft.authority.completed=flags.contains(champion_state::completed_turn);
    draft.authority.recall_started=flags.contains(champion_state::recall_started);draft.authority.bank_started=flags.contains(champion_state::submit_gems_started);
    draft.authority.channel=flags.contains(champion_state::teleporting)?executable_policy::Channel::teleport:
        flags.contains(champion_state::recalling)?executable_policy::Channel::recall:
        flags.contains(champion_state::submitting_gems)?executable_policy::Channel::bank:executable_policy::Channel::none;
    if(!executable_observe_query(P::owner,[&]{return detail::sdk_champion_restrictions(owner,&draft.authority.restrictions);},code))return false;
    detail::check(code);draft.restrictions_read=true;
    std::array<detail::buff_data,3> buffs{};std::uint32_t count=0;
    if(!executable_observe_query(P::owner,[&]{return detail::sdk_champion_buff_data(owner,buffs.data(),buffs.size(),&count);},code))return false;
    detail::check(code);if(count>buffs.size()){state.status=ExecutableObservationStatus::unsupported;return false;}
    for(unsigned n=0;n<count;++n){const auto& buff=buffs[n];
        if(buff.type==unsigned(detail::buff_kind::shield))draft.shield+=std::max(0.0f,buff.data.shield.value);
        else if(buff.type==unsigned(detail::buff_kind::stun))draft.authority.stunned=true;
        else if(buff.type!=unsigned(detail::buff_kind::enhanced_attack)){state.status=ExecutableObservationStatus::unsupported;return false;}}
    draft.buffs_read=true;
    const bool changed=snapshot.body_actual&&(snapshot.self!=draft.self||snapshot.pose!=draft.pose||snapshot.authority!=draft.authority);
    if(changed){executable_observe_revoke();++state.effect;ExecutableObservationFacts::after_attempt(snapshot);}
    snapshot.stamp={std::uint64_t(epoch),state.frame,state.effect,state.observation,snapshot.stamp.host_attempt};
    if(!ExecutableObservationFacts::publish_owner(snapshot,draft,snapshot.stamp))return false;
    prepared.body=body;prepared.owner_actual=true;
    if(classified_home){prepared.home_actual=true;prepared.on_home=on_home;}
    if(!publish_lease)return true;
    if(state.lease.valid())return state.lease.observe(snapshot);
    if(!state.lease.issue(snapshot,++state.lease_serial)){state.status=ExecutableObservationStatus::stale;return false;}
    ++state.stats.leases;return true;
}
static champion_state state_from_observation(const Info& body){return champion_state::from_bits_retain(std::uint32_t(body.state));}
int executable_observe_bound_member(executable_policy::NativeId id)const{
    const auto& state=executable_observe_state();for(unsigned n=0;n<state.binding_count;++n)if(state.bindings[n].id==id)return state.bindings[n].member;
    return -1;
}
bool executable_observe_member_receipt(std::size_t index,unsigned flags,const detail::camp_state* body=nullptr,
                                      bool shield_actual=false,float shield=0){
    auto& observed=executable_observe_state();if(index>=members.size()||flags>2)return false;
    auto& member=members[index];const auto& group=memory[std::size_t(member.group)];
    member.seen=epoch;member.alive=flags!=0;member.awake=flags==1;
    if(flags){if(body){member.hp=body->health;member.strength=body->strength;}if(shield_actual)member.shield=shield;
        member.work.observe_alive(epoch);if(support_detail.ready())support_detail.restore(std::size_t(member.group),epoch);
    }else{member.hp=member.shield=0;int next=epoch+1;
        if(group.seen==epoch){if(group.state==1||group.state==2)next=epoch+std::max(1,groups[std::size_t(member.group)].max_respawn_timer()-1);
            else if(group.state==3)next=std::max(epoch+1,group.until-1);}member.work.observe_dead(epoch,next);}
    ++observed.stats.member_receipts;return true;
}
bool executable_observe_read_target(executable_policy::TargetKind kind,executable_policy::NativeId id,int member_index,bool shield){
    using P=ExecutableObservationStats::Phase;auto& observed=executable_observe_state();auto& snapshot=observed.prepared.snapshot;int code=0;
    ExecutableTargetDraft draft;draft.fact.kind=kind;draft.fact.id=id;
    if(kind==executable_policy::TargetKind::camp){
        if(member_index<0||std::size_t(member_index)>=members.size()){observed.status=ExecutableObservationStatus::unsupported;return false;}
        const auto index=std::size_t(member_index);auto& member=members[index];draft.fact.point={member.pos.x,member.pos.y};
        unsigned binding=0;while(binding<observed.binding_count&&observed.bindings[binding].id!=id)++binding;
        if(binding==observed.binding_count){if(binding==observed.bindings.size()){observed.status=ExecutableObservationStatus::unsupported;return false;}
            observed.bindings[observed.binding_count++]={id,member_index};}
        std::uint32_t life=0;
        if(!executable_observe_query(P::target,[&]{return detail::sdk_camp_state(id,&life);},code))return false;
        if(executable_observe_hidden(code)){draft.visibility_read=true;draft.fact.visible=false;
            ExecutableObservationFacts::publish_target(snapshot,draft);return true;}
        detail::check(code);if(life>2){observed.status=ExecutableObservationStatus::unsupported;return false;}
        draft.visibility_read=draft.life_read=true;draft.fact.visible=true;draft.fact.alive=life!=0;
        detail::camp_state body{};float current_shield=0;
        if(life){if(!executable_observe_query(P::target,[&]{return detail::sdk_camp(id,&body);},code))return false;
            if(executable_observe_hidden(code))return false;detail::check(code);draft.health_read=true;draft.fact.health=body.health;
            if(shield){std::array<detail::camp_buff_data,3> buffs{};std::uint32_t count=0;
                if(!executable_observe_query(P::target,[&]{return detail::sdk_camp_buffs(id,buffs.data(),buffs.size(),&count);},code))return false;
                if(executable_observe_hidden(code))return false;detail::check(code);
                if(count>buffs.size()){observed.status=ExecutableObservationStatus::unsupported;return false;}
                for(unsigned n=0;n<count;++n)if(buffs[n].type==unsigned(detail::buff_kind::shield))current_shield+=std::max(0.0f,buffs[n].magnitude);
                draft.shield_read=true;draft.fact.shield=current_shield;}
        }
        if(!ExecutableObservationFacts::publish_target(snapshot,draft))return false;
        return executable_observe_member_receipt(index,life,life?&body:nullptr,draft.shield_read,current_shield);
    }
    if(kind!=executable_policy::TargetKind::champion){observed.status=ExecutableObservationStatus::unsupported;return false;}
    Info body{};if(!executable_observe_query(P::target,[&]{return detail::sdk_champion(id,&body);},code))return false;
    draft.visibility_read=true;
    if(executable_observe_hidden(code)){draft.fact.visible=false;return ExecutableObservationFacts::publish_target(snapshot,draft);}
    detail::check(code);draft.fact.visible=body.x>=0&&body.y>=0&&body.x<walls->width()&&body.y<walls->height();
    draft.fact.point={body.x,body.y};draft.fact.ally=body.team==observed.prepared.body.team;
    if(draft.fact.visible){draft.life_read=draft.health_read=true;draft.fact.alive=!state_from_observation(body).is_dead();draft.fact.health=body.health;}
    if(shield&&draft.fact.visible){std::array<detail::buff_data,3> buffs{};std::uint32_t count=0;
        if(!executable_observe_query(P::target,[&]{return detail::sdk_champion_buff_data(id,buffs.data(),buffs.size(),&count);},code))return false;
        if(executable_observe_hidden(code))return false;detail::check(code);
        if(count>buffs.size()){observed.status=ExecutableObservationStatus::unsupported;return false;}
        for(unsigned n=0;n<count;++n)if(buffs[n].type==unsigned(detail::buff_kind::shield))draft.fact.shield+=std::max(0.0f,buffs[n].data.shield.value);
        draft.shield_read=true;}
    return ExecutableObservationFacts::publish_target(snapshot,draft);
}
bool executable_observe_read_move(executable_policy::Point point){
    using P=ExecutableObservationStats::Phase;auto& observed=executable_observe_state();const auto& prepared=observed.prepared;
    ExecutableMoveDraft draft;draft.fact.point=point;draft.geometry_read=true;
    const auto origin=prepared.snapshot.pose;const auto dx=std::int64_t(origin.x)-point.x,dy=std::int64_t(origin.y)-point.y;
    const auto d=std::max(dx<0?-dx:dx,dy<0?-dy:dy);
    draft.fact.reachable=prepared.owner_actual&&prepared.body.move_speed>=1&&d==1&&!static_blocked({point.x,point.y});
    if(!draft.fact.reachable)return ExecutableObservationFacts::publish_move(observed.prepared.snapshot,draft);
    int code=0;std::int32_t visible=0,champion=0,camp=0,teleport=0;
    if(!executable_observe_query(P::move,[&]{return detail::sdk_is_visible(point.x,point.y,&visible);},code))return false;
    detail::check(code);draft.visibility_read=true;draft.visible=visible!=0;
    if(!draft.visible)return ExecutableObservationFacts::publish_move(observed.prepared.snapshot,draft);
    if(!executable_observe_query(P::move,[&]{return detail::sdk_has_champion(point.x,point.y,&champion);},code))return false;
    detail::check(code);draft.champion_read=true;draft.champion=champion!=0;
    if(!executable_observe_query(P::move,[&]{return detail::sdk_has_camp(point.x,point.y,&camp);},code))return false;
    detail::check(code);draft.camp_read=true;draft.camp=camp!=0;
    if(!executable_observe_query(P::move,[&]{return detail::sdk_has_teleport(point.x,point.y,&teleport);},code))return false;
    detail::check(code);draft.teleport_read=true;draft.teleport=teleport!=0;
    return ExecutableObservationFacts::publish_move(observed.prepared.snapshot,draft);
}
bool executable_observe_target_seen(executable_policy::TargetKind kind,executable_policy::NativeId id,bool require_shield=false)const{
    const auto& snapshot=executable_observe_snapshot();for(unsigned n=0;n<snapshot.target_count;++n){
        if(snapshot.targets[n].kind==kind&&snapshot.targets[n].id==id){const auto& fact=snapshot.targets[n];
            return !(require_shield&&fact.actual&&fact.visible&&fact.alive&&!fact.shield_actual);}
    }
    return false;
}
bool executable_observe_move_seen(executable_policy::Point point)const{
    const auto& snapshot=executable_observe_snapshot();for(unsigned n=0;n<snapshot.move_count;++n)if(snapshot.moves[n].point==point)return true;return false;
}
bool executable_observe_read_group(int ordinal,int witness,unsigned finite_inputs){
    using P=ExecutableObservationStats::Phase;auto& observed=executable_observe_state();auto& prepared=observed.prepared;auto& snapshot=prepared.snapshot;
    if(ordinal<0||std::size_t(ordinal)>=groups.size()){observed.status=ExecutableObservationStatus::unsupported;return false;}
    auto& stored=memory[std::size_t(ordinal)];bool on_region=false,visible_witness=false;
    for(std::size_t n=stored.begin;n<stored.end;++n)if(snapshot.pose==executable_policy::Point{members[n].pos.x,members[n].pos.y})on_region=true;
    visible_witness=on_region;
    for(unsigned n=0;n<snapshot.target_count&&!visible_witness;++n){const auto& fact=snapshot.targets[n];
        const int member=executable_observe_bound_member(fact.id);
        visible_witness=fact.kind==executable_policy::TargetKind::camp&&fact.actual&&fact.visible&&member>=0&&
            members[std::size_t(member)].group==ordinal;}
    int code=0;
    if(!visible_witness){
        if(witness<0||std::size_t(witness)>=members.size()||members[std::size_t(witness)].group!=ordinal)
            witness=stored.begin<stored.end?int(stored.begin):-1;
        if(witness<0)return true;
        const auto point=members[std::size_t(witness)].pos;std::int32_t visible=0;
        if(!executable_observe_query(P::group,[&]{return detail::sdk_is_visible(point.x,point.y,&visible);},code))return false;
        detail::check(code);visible_witness=visible!=0;if(!visible_witness)return true;
    }
    std::uint32_t life=0;const auto id=std::uint32_t(handle_camp_group_t(groups[std::size_t(ordinal)]));
    if(!executable_observe_query(P::group,[&]{return detail::sdk_camp_group_state(id,&life);},code))return false;
    if(executable_observe_hidden(code))return true;detail::check(code);if(!life)return true;
    if(life>3){observed.status=ExecutableObservationStatus::unsupported;return false;}
    int until=0;
    if(life==3){std::int32_t timer=0;
        if(!executable_observe_query(P::group,[&]{return detail::sdk_camp_group_cooldown(id,&timer);},code))return false;
        detail::check(code);until=epoch+std::max(0,timer);
    }else if(life==2)until=epoch+std::max(1,groups[std::size_t(ordinal)].max_respawn_timer()-1);
    if(life==1&&(stored.state==2||stored.state==3)&&epoch>stored.seen)
        for(std::size_t n=stored.begin;n<stored.end;++n)members[n].work.new_generation(epoch);
    stored.state=int(life);stored.seen=epoch;stored.until=until;
    if(support_detail.ready()){
        if(life==1)support_detail.restore(std::size_t(ordinal),epoch);
        else support_detail.absence(std::size_t(ordinal),epoch,std::max(epoch,until-1));}
    if(life==2||life==3)for(std::size_t n=stored.begin;n<stored.end;++n){auto& member=members[n];
        member.alive=member.awake=false;member.hp=member.shield=0;member.seen=epoch;
        member.work.observe_dead(epoch,std::max(epoch+1,until-1));}
    prepared.group_actual=true;prepared.group_index=ordinal;prepared.group_state=life;
    prepared.group_gems=groups[std::size_t(ordinal)].gem_value();prepared.group_receipt=++observed.receipt_serial;
    ++observed.stats.group_receipts;snapshot.capture_actual=true;snapshot.group=id;snapshot.group_clear=life==2||life==3;
    snapshot.reward_available=life==2;snapshot.on_region=on_region;snapshot.finite_capture_inputs=finite_inputs;
    snapshot.contest_known=false;snapshot.uncontested=false;
    for(unsigned n=0;n<snapshot.target_count;++n){const auto& foe=snapshot.targets[n];
        if(foe.kind!=executable_policy::TargetKind::champion||!foe.actual||!foe.visible||!foe.alive||foe.ally)continue;
        for(std::size_t member=stored.begin;member<stored.end;++member)if(foe.point==executable_policy::Point{members[member].pos.x,members[member].pos.y})
            snapshot.contest_known=true;}
    // Group respawn/cooldown is NOT an owned capture receipt. Bag and team
    // tribute observations remain separate; no service completion is inferred.
    return true;
}
bool executable_observe_read_bag(){
    using P=ExecutableObservationStats::Phase;auto& observed=executable_observe_state();auto& snapshot=observed.prepared.snapshot;
    unsigned value=0;int code=0;
    if(!executable_observe_query(P::bag,[&]{return detail::sdk_champion_gems(snapshot.self,&value);},code))return false;
    detail::check(code);snapshot.gems=value;snapshot.gems_actual=true;return true;
}
bool executable_observe_read_submitted(){
    using P=ExecutableObservationStats::Phase;auto& observed=executable_observe_state();unsigned value=0;int code=0;
    const auto absolute=unsigned(observed.prepared.body.team);
    if(!executable_observe_query(P::bag,[&]{return detail::sdk_bc_submitted_gems(absolute,&value);},code))return false;
    detail::check(code);observed.prepared.submitted_actual=true;observed.prepared.submitted_gems=value;return true;
}
bool executable_observe_read_bank(executable_policy::Point point){
    using P=ExecutableObservationStats::Phase;auto& observed=executable_observe_state();auto& snapshot=observed.prepared.snapshot;
    bool known=false;for(const auto& altar:altars)known=known||point==executable_policy::Point{altar.x,altar.y};
    snapshot.altar=point;snapshot.bank_range=int(constants::submit_gems_range);
    if(!known||std::max(std::abs(point.x-snapshot.pose.x),std::abs(point.y-snapshot.pose.y))>snapshot.bank_range)return true;
    detail::altar_lock_info lock{};int code=0;const int enemy=1-observed.prepared.body.team;
    if(!executable_observe_query(P::bank,[&]{return detail::sdk_altar_lock(point.x,point.y,enemy,&lock);},code))return false;
    if(executable_observe_hidden(code))return true;
    if(code!=int(error_code::api_not_found))detail::check(code);
    snapshot.bank_target_actual=true;snapshot.bank_unlocked=code==int(error_code::api_not_found);return true;
}
bool executable_observe_read_home(){
    using P=ExecutableObservationStats::Phase;auto& observed=executable_observe_state();auto& prepared=observed.prepared;int code=0;bool inside=false;
    if(executable_observe_classification_ready()&&observed.classification.home_actual){
        prepared.home_actual=true;prepared.on_home=observed.classification.on_home;return true;}
    if(prepared.snapshot.authority.dead)return true;
    if(!executable_observe_query(P::home,[&]{inside=home->is_set({prepared.snapshot.pose.x,prepared.snapshot.pose.y});return 0;},code))return false;
    prepared.home_actual=true;prepared.on_home=inside;return true;
}
bool executable_observe_read_channel(){
    using P=ExecutableObservationStats::Phase;auto& observed=executable_observe_state();auto& snapshot=observed.prepared.snapshot;
    if(snapshot.authority.channel==executable_policy::Channel::none||snapshot.authority.channel==executable_policy::Channel::teleport)return true;
    std::int32_t ticks=0;int code=0;
    if(!executable_observe_query(P::owner,[&]{return detail::sdk_champion_channel_time(snapshot.self,&ticks);},code))return false;
    detail::check(code);if(ticks<0){observed.status=ExecutableObservationStatus::unsupported;return false;}
    snapshot.channel_timer_actual=true;snapshot.channel_ticks=unsigned(ticks);return true;
}
bool executable_observe_owner_ready()const{
    const auto& observed=executable_observe_state();const auto& snapshot=observed.prepared.snapshot;
    return observed.lease.valid()&&snapshot.body_actual&&snapshot.restrictions_actual&&snapshot.channel_actual&&
        snapshot.stamp.epoch==std::uint64_t(epoch)&&snapshot.stamp.frame==observed.frame;
}
bool executable_observe_classification_ready()const{
    const auto& observed=executable_observe_state();const auto& classified=observed.classification;
    return observed.input_epoch==epoch&&classified.stamp.epoch==std::uint64_t(epoch)&&
        classified.current(observed.prepared.snapshot.stamp);
}
unsigned executable_observe_owner_queries()const{
    return executable_observe_owner_ready()?0:executable_observe_classification_ready()?2:3;
}
bool executable_observe_need_bag(const ExecutableObservationRequest& request)const{
    return request.need_bag||(request.have_altar&&request.purpose==executable_policy::Purpose::bank);
}
bool executable_observe_channel_requested(const ExecutableObservationRequest& request)const{
    return request.phase==ExecutableMacroPhase::bank_channel||request.phase==ExecutableMacroPhase::recall_channel;
}
unsigned executable_observe_request_queries(const ExecutableObservationRequest& request)const{
    const auto& observed=executable_observe_state();const auto& snapshot=observed.prepared.snapshot;unsigned count=executable_observe_owner_queries();
    if(request.member>=0&&std::size_t(request.member)<members.size()){
        const auto id=std::uint32_t(handle_camp_t(members[std::size_t(request.member)].handle));
        if(!executable_observe_target_seen(executable_policy::TargetKind::camp,id,request.need_target_shield))count+=2+unsigned(request.need_target_shield);}
    if(request.have_enemy&&!executable_observe_target_seen(executable_policy::TargetKind::champion,request.enemy,request.need_target_shield))count+=1+unsigned(request.need_target_shield);
    if(request.have_move&&!executable_observe_move_seen(request.move))count+=4;
    if(request.need_group&&(!observed.prepared.group_actual||observed.prepared.group_index!=request.group))count+=3;
    if(executable_observe_need_bag(request)&&!snapshot.gems_actual)++count;
    if(request.need_bank_receipt&&!observed.prepared.submitted_actual)++count;
    if(request.need_home&&!observed.prepared.home_actual&&
       !(executable_observe_classification_ready()&&observed.classification.home_actual))++count;
    if(request.have_altar&&(!snapshot.bank_target_actual||snapshot.altar!=request.altar))++count;
    if(executable_observe_channel_requested(request)&&!snapshot.channel_timer_actual)++count;return count;
}
bool executable_observe_read_request(const ExecutableObservationRequest& request){
    auto& observed=executable_observe_state();auto& snapshot=observed.prepared.snapshot;
    if(!executable_observe_owner_ready()&&!executable_observe_read_owner())return false;
    if(request.member>=0&&std::size_t(request.member)<members.size()){
        const auto id=std::uint32_t(handle_camp_t(members[std::size_t(request.member)].handle));
        if(!executable_observe_target_seen(executable_policy::TargetKind::camp,id,request.need_target_shield)&&
           !executable_observe_read_target(executable_policy::TargetKind::camp,id,request.member,request.need_target_shield))return false;}
    if(request.have_enemy&&!executable_observe_target_seen(executable_policy::TargetKind::champion,request.enemy,request.need_target_shield)&&
       !executable_observe_read_target(executable_policy::TargetKind::champion,request.enemy,-1,request.need_target_shield))return false;
    if(request.have_move&&!executable_observe_move_seen(request.move)&&!executable_observe_read_move(request.move))return false;
    if(request.need_group&&(!observed.prepared.group_actual||observed.prepared.group_index!=request.group)&&
       !executable_observe_read_group(request.group,request.group_witness_member,request.capture_inputs_remaining))return false;
    if(request.need_group&&observed.prepared.group_actual&&observed.prepared.group_index==request.group){
        // Remaining inputs belong to the retained macro lease, not a new
        // observation nonce. Reusing a current group receipt needs no reread.
        snapshot.finite_capture_inputs=request.capture_inputs_remaining;
        const auto& region=memory[std::size_t(request.group)];
        for(unsigned n=0;n<snapshot.target_count;++n){const auto& foe=snapshot.targets[n];
            if(foe.kind!=executable_policy::TargetKind::champion||!foe.actual||!foe.visible||!foe.alive||foe.ally)continue;
            for(std::size_t member=region.begin;member<region.end;++member)
                if(foe.point==executable_policy::Point{members[member].pos.x,members[member].pos.y}){snapshot.contest_known=true;snapshot.uncontested=false;}}
    }
    if(executable_observe_need_bag(request)&&!snapshot.gems_actual&&!executable_observe_read_bag())return false;
    if(request.need_bank_receipt&&!observed.prepared.submitted_actual&&!executable_observe_read_submitted())return false;
    if(request.need_home&&!observed.prepared.home_actual&&!executable_observe_read_home())return false;
    if(request.have_altar&&(!snapshot.bank_target_actual||snapshot.altar!=request.altar)&&!executable_observe_read_bank(request.altar))return false;
    if(executable_observe_channel_requested(request)&&!snapshot.channel_timer_actual&&!executable_observe_read_channel())return false;
    return true;
}
bool executable_observe_operator_ready(const ExecutableObservationRequest& request)const{
    namespace ep=executable_policy;const auto& snapshot=executable_observe_snapshot();if(!executable_observe_owner_ready())return false;
    ep::Action action;
    if(request.member>=0&&std::size_t(request.member)<members.size()){
        action.kind=ep::ActionKind::basic;action.target_kind=ep::TargetKind::camp;
        action.target_id=std::uint32_t(handle_camp_t(members[std::size_t(request.member)].handle));if(ep::authorize(snapshot,action))return true;}
    if(request.have_enemy){action={};action.kind=ep::ActionKind::basic;action.target_kind=ep::TargetKind::champion;action.target_id=request.enemy;
        if(ep::authorize(snapshot,action))return true;}
    if(request.have_move){action={};action.kind=ep::ActionKind::move;action.target_kind=ep::TargetKind::ground;action.point=request.move;
        if(ep::authorize(snapshot,action))return true;}
    if(request.have_altar){action={};action.kind=ep::ActionKind::bank;action.target_kind=ep::TargetKind::ground;action.point=request.altar;
        if(ep::authorize(snapshot,action))return true;}
    if(request.purpose==ep::Purpose::recover){action={};action.kind=ep::ActionKind::recall;action.target_kind=ep::TargetKind::self;
        if(ep::authorize(snapshot,action))return true;}
    if(snapshot.authority.channel==ep::Channel::none&&(!request.need_group||executable_observe_prepared().group_index!=request.group))return false;
    action={};action.kind=ep::ActionKind::wait;action.wait_reason=snapshot.authority.channel!=ep::Channel::none?ep::WaitReason::channel:ep::WaitReason::capture;
    return ep::authorize(snapshot,action);
}
template<class Receipt> bool executable_observe_credit_compilation(const Receipt& receipt){
    auto& observed=executable_observe_state();const auto now=fuel_remaining();
    if(int(round_number())!=epoch||observed.input_epoch!=epoch||observed.prepared.snapshot.stamp.epoch!=std::uint64_t(epoch)){
        executable_observe_epoch_failure();return false;}
    auto& clock=observed.preparation;const auto& stamp=observed.prepared.snapshot.stamp;
    const bool actual=receipt.paid&&receipt.epoch==std::uint64_t(epoch)&&receipt.before_fuel>=receipt.after_fuel&&
        receipt.spent==receipt.before_fuel-receipt.after_fuel&&now<=receipt.after_fuel;
    // A causal retry acknowledges the very same paid interval without adding
    // credit. Different/stale/overlapping intervals cannot share its serial.
    if(actual&&clock.known_credit(stamp,receipt.before_fuel,receipt.after_fuel,receipt.serial))return true;
    if(!actual||!clock.credit(stamp,receipt.before_fuel,receipt.after_fuel,now,receipt.serial,true)){
        ++observed.compilation_credit_rejections;return false;}
    ++observed.compilation_credits;return true;
}
bool executable_observe_finish_preparation(std::uint64_t retained_tail){
    auto& observed=executable_observe_state();
    if(int(round_number())!=epoch||observed.input_epoch!=epoch){executable_observe_epoch_failure();return false;}
    const auto now=fuel_remaining();auto& packet=observed.prepared;
    const auto spent=observed.preparation.spent(packet.snapshot.stamp,now,observed.query_spent);
    observed.preparation_spent=spent;observed.max_preparation_spent=std::max(observed.max_preparation_spent,spent);
    const bool authority=packet.status==ExecutableObservationStatus::ready&&
        executable_policy::token_matches(packet.token,packet.snapshot)&&observed.lease.valid();
    const bool clock=observed.preparation.finish(packet.snapshot.stamp,now,observed.query_spent);
    if(clock&&authority&&retained_tail&&retained_tail!=executable_policy::invalid_cost&&now>retained_tail)return true;
    // Scoped observations remain truthful, but an uncompleted/overrun resource
    // transaction never publishes an executable ready token or gets a reset.
    observed.preparation.failed=true;observed.status=ExecutableObservationStatus::overrun;
    packet.status=observed.result.status=ExecutableObservationStatus::overrun;
    packet.retained_incumbent=observed.result.retained_incumbent=false;executable_observe_revoke();return false;
}
ExecutablePrepared& executable_observe_prepare(const ExecutableObservationRequest& request,std::uint64_t retained_tail,
                                               bool keep_transaction_open=false){
    const auto transaction_entry=fuel_remaining();executable_observe_enter();auto& observed=executable_observe_state();
    if(!observed.preparation.matches(observed.prepared.snapshot.stamp))
        observed.preparation.begin(observed.prepared.snapshot.stamp,transaction_entry,observed.query_spent,executable_observe_compute_bound);
    ++observed.stats.prepares;
    const unsigned count=executable_observe_request_queries(request);
    bool copied=false,ready_candidate=false;
    const bool ok=executable_observe_run(ExecutableObservationStats::Phase::prepare,count,retained_tail,[&]{
        const bool read=executable_observe_read_request(request);ready_candidate=read&&executable_observe_operator_ready(request);
        if(!ready_candidate){observed.result=observed.prepared;copied=true;}return read;},executable_observe_compute_bound,true);
    const bool ready=ok&&ready_candidate;
    if(ready){observed.request=request;auto& prepared=observed.prepared;prepared.status=ExecutableObservationStatus::ready;
        prepared.phase=request.phase;prepared.owner=request.owner;prepared.purpose=request.purpose;prepared.token=observed.lease.token();
        prepared.valuation.state=executable_policy::ValueState::unknown;prepared.valuation.assurance=executable_policy::Assurance::none;
        prepared.retained_incumbent=true;prepared.queries=observed.stats.last_queries;++observed.stats.ready;
        if(!keep_transaction_open)observed.preparation.close();return prepared;}
    // Actual scoped knowledge was published transactionally. A failed optional
    // request leaves the prior policy/token and all unrelated scopes intact.
    if(!copied){ExecutableObservationFacts::after_attempt(observed.result.snapshot);observed.result.token.valid=false;observed.result.owner_actual=false;}
    observed.result.phase=request.phase;observed.result.owner=request.owner;observed.result.purpose=request.purpose;
    observed.result.status=ok?ExecutableObservationStatus::unknown:observed.status;
    observed.result.retained_incumbent=observed.prepared.status==ExecutableObservationStatus::ready&&
        executable_policy::token_matches(observed.prepared.token,observed.prepared.snapshot);
    observed.result.queries=observed.stats.last_queries;
    if(observed.result.status==ExecutableObservationStatus::unknown)++observed.stats.unknown;else ++observed.stats.partial;return observed.result;
}
ExecutableClassification& executable_observe_classify(std::uint64_t retained_tail){
    const auto transaction_entry=fuel_remaining();executable_observe_enter();auto& observed=executable_observe_state();
    if(!observed.preparation.matches(observed.prepared.snapshot.stamp))
        observed.preparation.begin(observed.prepared.snapshot.stamp,transaction_entry,observed.query_spent,executable_observe_compute_bound);
    const bool cached=executable_observe_classification_ready(),owned=executable_observe_owner_ready();
    const unsigned count=cached||owned?0:1;
    const bool okay=executable_observe_run(ExecutableObservationStats::Phase::owner,count,retained_tail,[&]{
        Info body{};int code=0;
        if(owned)body=observed.prepared.body;
        else if(cached)body=observed.classification.body;
        else{const auto id=std::uint32_t(handle_champion_t(self()));
            if(!executable_observe_query(ExecutableObservationStats::Phase::owner,[&]{return detail::sdk_champion(id,&body);},code))return false;
            detail::check(code);}
        auto& classified=observed.classification;classified.body=body;classified.stamp=observed.prepared.snapshot.stamp;
        classified.dead=state_from_observation(body).is_dead();
        classified.home_actual=!classified.dead&&body.x>=0&&body.y>=0&&body.x<walls->width()&&body.y<walls->height();
        classified.on_home=classified.home_actual&&executable_cached_home({body.x,body.y});classified.actual=true;return true;
    },executable_observe_compute_bound,true);
    auto& classified=observed.classification;if(!okay)classified.actual=classified.home_actual=false;
    classified.queries=observed.stats.last_queries;classified.fuel=fuel_remaining();return classified;
}
ExecutablePrepared& executable_observe_owner(std::uint64_t retained_tail,bool need_home=false){
    const auto transaction_entry=fuel_remaining();executable_observe_enter();auto& observed=executable_observe_state();
    if(!observed.preparation.matches(observed.prepared.snapshot.stamp))
        observed.preparation.begin(observed.prepared.snapshot.stamp,transaction_entry,observed.query_spent,executable_observe_compute_bound);
    bool copied=false;
    const unsigned count=executable_observe_owner_queries()+unsigned(need_home&&!observed.prepared.home_actual&&
        !(executable_observe_classification_ready()&&observed.classification.home_actual));
    const bool ok=executable_observe_run(ExecutableObservationStats::Phase::owner,count,retained_tail,[&]{
        bool read=executable_observe_owner_ready()||executable_observe_read_owner();
        if(read&&need_home&&!observed.prepared.home_actual)read=executable_observe_read_home();
        observed.result=observed.prepared;copied=true;return read;},executable_observe_compute_bound,true);
    if(!copied){ExecutableObservationFacts::after_attempt(observed.result.snapshot);observed.result.owner_actual=false;}
    observed.result.status=ok?ExecutableObservationStatus::ready:observed.status;
    observed.result.token=observed.lease.token();observed.result.queries=observed.stats.last_queries;
    observed.result.retained_incumbent=observed.prepared.status==ExecutableObservationStatus::ready&&
        executable_policy::token_matches(observed.prepared.token,observed.prepared.snapshot);return observed.result;
}
void executable_observe_execution_request(const ExecutableObservationRequest& request){
    // Intent binding for the SELECTED word, not an observation or physical
    // event. Optional rejected preparations cannot enlarge its feedback cone.
    executable_observe_state().request=request;
}
bool executable_observe_validate(const executable_policy::Action& action,executable_policy::Snapshot& out,
                                std::uint64_t tail,std::uint64_t compute_bound=executable_observe_compute_bound){
    namespace ep=executable_policy;executable_observe_enter();auto& observed=executable_observe_state();auto& snapshot=observed.prepared.snapshot;
    if(!executable_observe_owner_ready()||observed.attempt_pending)return false;
    const auto bounds=executable_observe_query_bounds(action,observed.request);
    return executable_observe_run(ExecutableObservationStats::Phase::validate,bounds.validation,tail,[&]{
        if(!bounds.validation){if(&out!=&snapshot)out=snapshot;return observed.lease.observe(snapshot)&&ep::authorize(snapshot,action);}
        if(action.kind==ep::ActionKind::basic&&!executable_observe_target_seen(action.target_kind,action.target_id)){
            const int member=action.target_kind==ep::TargetKind::camp?executable_observe_bound_member(action.target_id):-1;
            if(action.target_kind==ep::TargetKind::camp&&member<0)return false;
            if(!executable_observe_read_target(action.target_kind,action.target_id,member,observed.request.need_target_shield))return false;
        }else if(action.kind==ep::ActionKind::move&&!executable_observe_move_seen(action.point)){
            if(observed.refused_epoch==epoch&&observed.last_refused&&action.point==observed.refused_point)return false;
            if(!executable_observe_read_move(action.point))return false;
        }else if(action.kind==ep::ActionKind::bank){
            if(!snapshot.gems_actual&&!executable_observe_read_bag())return false;
            if((!snapshot.bank_target_actual||snapshot.altar!=action.point)&&!executable_observe_read_bank(action.point))return false;
        }else if(action.kind==ep::ActionKind::wait){
            if(action.wait_reason==ep::WaitReason::channel&&!snapshot.channel_timer_actual&&!executable_observe_read_channel())return false;
            if(action.wait_reason==ep::WaitReason::capture&&!snapshot.capture_actual&&
               !executable_observe_read_group(observed.request.group,observed.request.group_witness_member,observed.request.capture_inputs_remaining))return false;
        }
        if(&out!=&snapshot)out=snapshot;
        return observed.lease.observe(snapshot)&&ep::authorize(snapshot,action);
    },compute_bound);
}
bool executable_observe_consume(executable_policy::Token& token){
    auto& observed=executable_observe_state();auto& snapshot=observed.prepared.snapshot;
    // Validation may have suspended at its final fuel boundary. Native epoch
    // authority must still hold immediately before consuming a host attempt.
    if(int(round_number())!=epoch||observed.input_epoch!=epoch||snapshot.stamp.epoch!=std::uint64_t(epoch)){
        executable_observe_epoch_failure();token.valid=false;return false;}
    if(!executable_policy::token_matches(token,snapshot)||!observed.lease.valid()||observed.attempt_pending){
        token.valid=false;executable_observe_revoke();return false;}
    observed.before_stamp=snapshot.stamp;observed.before_pose=snapshot.pose;observed.before_authority=snapshot.authority;
    const bool consumed=observed.lease.before_attempt(snapshot);token.valid=false;
    ExecutableObservationFacts::invalidate_packet(observed.prepared);observed.preparation.close();
    observed.classification.actual=observed.classification.home_actual=false;
    observed.attempt_pending=consumed;observed.pending_applied=observed.pending_effect_published=false;
    if(consumed)++observed.stats.attempts;return consumed;
}
template<class Attempt> bool executable_observe_feedback(const executable_policy::Action& action,const Attempt& attempt,
    executable_policy::Snapshot& out,executable_policy::Receipt& receipt,std::uint64_t tail,
    std::uint64_t compute_bound=executable_observe_compute_bound){
    namespace ep=executable_policy;auto& observed=executable_observe_state();auto& snapshot=observed.prepared.snapshot;
    if(!observed.attempt_pending||!attempt.attempted||observed.input_epoch!=epoch)return false;
    observed.pending_kind=action.kind;observed.pending_applied=attempt.applied;
    if(!observed.pending_effect_published){
        if(attempt.applied){++observed.effect;++observed.stats.accepted;}if(attempt.refused)++observed.stats.refused;
        observed.pending_effect_published=true;
    }
    snapshot.stamp.effect=observed.effect;
    const auto bounds=executable_observe_query_bounds(action,observed.request);
    bool ok=executable_observe_run(ExecutableObservationStats::Phase::feedback,bounds.feedback,tail,[&]{
        if(!executable_observe_read_owner(false))return false;
        if(action.kind==ep::ActionKind::basic){
            const int member=action.target_kind==ep::TargetKind::camp?executable_observe_bound_member(action.target_id):-1;
            if(!executable_observe_read_target(action.target_kind,action.target_id,member,observed.request.need_target_shield))return false;
        }
        if((action.kind==ep::ActionKind::recall||action.kind==ep::ActionKind::bank)&&!executable_observe_read_channel())return false;
        return true;
    },compute_bound);
    if(ok){if(observed.lease.valid())ok=observed.lease.observe(snapshot);
        else if(!observed.lease.issue(snapshot,++observed.lease_serial)){observed.status=ExecutableObservationStatus::stale;ok=false;}
        else ++observed.stats.leases;}
    // The minimal actual prefix survives even if a later target read failed.
    // Missing target/occupancy scope stays unknown and grants no fresh suffix.
    receipt.after=snapshot.stamp;receipt.after_pose=snapshot.pose;receipt.after_authority=snapshot.authority;
    receipt.actual_body=snapshot.body_actual&&snapshot.restrictions_actual&&snapshot.channel_actual;
    receipt.after_health=snapshot.health;receipt.after_shield=snapshot.shield;
    receipt.health_actual=receipt.health_actual&&snapshot.health_actual;receipt.shield_actual=receipt.shield_actual&&snapshot.shield_actual;
    receipt.gems_actual=false; // commands are not owned bank/reward receipts.
    receipt.actual_targets=ok;
    receipt.actual_occupancy=ok&&!attempt.refused;
    if(!ok)executable_observe_revoke();else observed.prepared.token=observed.lease.token();
    if(&out!=&snapshot)out=snapshot;return ok;
}
template<class Attempt> bool executable_observe_close_refusal(const executable_policy::Action& action,const Attempt& attempt,
    executable_policy::Snapshot& out,executable_policy::Receipt& receipt,std::uint64_t tail,
    std::uint64_t compute_bound=executable_observe_compute_bound){
    namespace ep=executable_policy;auto& observed=executable_observe_state();auto& snapshot=observed.prepared.snapshot;
    return executable_observe_run(ExecutableObservationStats::Phase::close,0,tail,[&]{
        if(!attempt.attempted||!attempt.refused||attempt.sdk_code||action.kind!=ep::ActionKind::move||
           !snapshot.body_actual||snapshot.pose!=receipt.before_pose||snapshot.authority!=receipt.before_authority||
           snapshot.stamp.effect!=receipt.before.effect)return false;
        observed.last_refused=true;observed.refused_epoch=epoch;observed.refused_point=action.point;
        if(!ExecutableObservationFacts::publish_refused_move(snapshot,action.point,receipt.before,receipt.before_pose,
            receipt.before_authority,attempt.attempted,attempt.refused,attempt.sdk_code))return false;
        receipt.actual_occupancy=true;if(&out!=&snapshot)out=snapshot;return true;
    },compute_bound);
}
bool executable_observe_commit_known(const executable_policy::Receipt& receipt)const{
    const auto& observed=executable_observe_state();const auto& action=observed.committed_action;
    return receipt.serial&&receipt.serial==observed.committed_serial&&receipt.before==observed.committed_before&&
        receipt.owner==observed.committed_owner&&receipt.outcome==observed.committed_outcome&&
        receipt.action.kind==action.kind&&receipt.action.target_kind==action.target_kind&&receipt.action.target_id==action.target_id&&
        receipt.action.point==action.point&&receipt.action.slot==action.slot&&receipt.action.wait_reason==action.wait_reason;
}
void executable_observe_commit_ack(const executable_policy::Receipt& receipt){
    auto& observed=executable_observe_state();observed.committed_before=receipt.before;observed.committed_owner=receipt.owner;
    observed.committed_action=receipt.action;observed.committed_outcome=receipt.outcome;observed.committed_serial=receipt.serial;
}
bool executable_observe_commit(const executable_policy::Receipt& receipt,std::uint64_t tail,
                              std::uint64_t compute_bound=executable_observe_compute_bound){
    namespace ep=executable_policy;auto& observed=executable_observe_state();
    if(executable_observe_commit_known(receipt))return true;
    return executable_observe_run(ExecutableObservationStats::Phase::commit,0,tail,[&]{
    if(!receipt.actual_attempt||!receipt.serial||receipt.serial<=observed.committed_serial)return false;
    // Native FALSE exists even when acquisition failed. Retain its debt once,
    // while refusing to manufacture an old geometry/HP continuation.
    if(!receipt.actual_body){if(receipt.outcome==ep::Outcome::refused)++return_refusals;
        observed.attempt_pending=false;executable_observe_commit_ack(receipt);return true;}
    if(receipt.outcome==ep::Outcome::refused){
        planned_route().observe_refusal(rp({receipt.before_pose.x,receipt.before_pose.y}));++return_refusals;
    }else if(receipt.outcome==ep::Outcome::accepted){
        last_action_round=epoch;
        if(receipt.action.kind==ep::ActionKind::basic)attacked=true;
        if(receipt.action.kind==ep::ActionKind::move){
            const vec2 before{receipt.before_pose.x,receipt.before_pose.y},after{receipt.after_pose.x,receipt.after_pose.y};
            const auto from=rp(before),to=rp(after);
            if(!exit_trace.valid()){const auto actual=exposure(before);if(actual.valid&&actual.amount==0)exit_trace.reset_quiet(from,actual);}
            if(exit_trace.at(from))exit_trace.observe_step(from,to,exposure(after),true);
            auto& route=planned_route();
            if(route.valid()&&route.at(from)&&route.size()>1&&route.node(1).point==to)route.observe_step(from,to,true);
            else if(route.valid())route.clear(); // actual off-plan displacement, not a knowledge reset.
            walked=true;path_slack=8;
        }
        if constexpr(cfg::funded_dispatch){auto& previous=funded_state();++previous.effect;previous.prepared=previous.hold_admitted=false;}
    }
    observed.attempt_pending=false;
    executable_observe_commit_ack(receipt);return true;},compute_bound);
}
void executable_observe_handoff_unknown(){
    // An actual unclosed physical prefix breaks usable geometry authority.
    // Keep its receipt/debt and current fresh observation; never splice old
    // coordinates or HP into the new input's history.
    exit_trace.clear();planned_route().clear();exit_priced_epoch=-1;exit_support_revision=0;
    if constexpr(cfg::funded_dispatch){auto& previous=funded_state();previous.prepared=previous.hold_admitted=false;}
    executable_observe_state().attempt_pending=false;
}
bool executable_observe_finite_wait(const executable_policy::Action& action,executable_policy::Snapshot& snapshot){
    auto& observed=executable_observe_state();
    if(action.kind!=executable_policy::ActionKind::wait||!executable_policy::authorize(snapshot,action)||
       observed.waited_frame==snapshot.stamp.frame||observed.attempt_pending)return false;
    observed.waited_frame=snapshot.stamp.frame;return true;
}
#endif
