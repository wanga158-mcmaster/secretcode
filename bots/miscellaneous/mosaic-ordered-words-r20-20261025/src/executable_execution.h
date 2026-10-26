// Included inside Runtime, or a fixture class with CORE_ONLY defined.
// executable_policy.h is included at file scope. Numeric valuation is never
// an execution prerequisite; actual authority and complete branch funding are.
using ExecutableAction=executable_policy::Action;
using ExecutableSnapshot=executable_policy::Snapshot;
using ExecutableReceipt=executable_policy::Receipt;
struct ExecutableActionCost {
    executable_policy::Phase validation{},feedback{},refusal_close{},commit{};
};
struct ExecutableExecutionBudget {
    bool declared{};executable_policy::Price price{};
    std::uint64_t admission_bound{};unsigned branch_limit{6};
    std::uint64_t validation_compute{},feedback_compute{},refusal_compute{},commit_compute{};
    unsigned max_primary{6},max_fallback{6};
};
static ExecutableExecutionBudget executable_execution_default_budget(){
    ExecutableExecutionBudget result;result.declared=true;result.price={2000,2000,2000};
    // Explicit SDK import units and initial compute declarations for the
    // scalar producer/record hooks. Neither a fixture nor finite source loops
    // qualify these bounds: ordinary native measurements must do that.
    result.validation_compute=1024;result.feedback_compute=2048;
    result.refusal_compute=512;result.commit_compute=2048;
    result.max_primary=2;result.max_fallback=1;return result;
}
struct ExecutableAttempt {bool attempted{},applied{},refused{};int sdk_code{};};
enum class ExecutablePhase {idle,admission,validation,command,feedback,refusal_close,commit,complete};
enum class ExecutableStatus {empty,word_applied,fallback_applied,finite_wait,unfunded,stale,changed,refused,
    unsupported,overrun,feedback_missing,invalid,reconciled};
struct ExecutableExecutionOutcome {
    ExecutableStatus status{ExecutableStatus::empty};executable_policy::FailureLatch first_failure{};
    ExecutablePhase first_failure_phase{ExecutablePhase::idle};
    bool attempted{},actual_effect{},closed{},finite_wait{},needs_continuation{true};
    bool feedback_closed{};int sdk_code{};
    unsigned successful{},attempts{},refusals{};executable_policy::Revision actual_prefix{};
    ExecutableReceipt last_receipt{};executable_policy::NextRequired next_required{executable_policy::NextRequired::work};
    bool raw_m_before{},raw_m_after{},restricted_move_before{},restricted_move_after{};
    executable_policy::ServiceKey service_key{};std::uint64_t fuel_spent{};
};
struct ExecutableExecutionState {
    executable_policy::ServiceOwner service;
    executable_policy::Funding funding;
    executable_policy::Revision input_epoch{executable_policy::invalid_cost},receipt_serial{};
    unsigned input_attempts{},input_successful{},input_refusals{};
    bool input_blocked{};
    executable_policy::FailureLatch first_failure{};ExecutablePhase first_failure_phase{ExecutablePhase::idle};
    ExecutableExecutionOutcome last{};
    bool pending_feedback{},pending_recorded{},pending_hooks{};ExecutableAction pending_action{};ExecutableAttempt pending_attempt{};
    bool pending_before_health{},pending_before_shield{},pending_before_gems{};
    ExecutableReceipt pending_receipt{};
    std::uint64_t calls{},words{},fallbacks{},attempts{},applied{},refused{},unfunded{},overruns{},feedback_failed{};
    std::array<std::uint64_t,8> phase_max{},phase_spent{};
};
static std::uint64_t executable_add(std::uint64_t a,std::uint64_t b){
    return b>executable_policy::invalid_cost-a?executable_policy::invalid_cost:a+b;
}
static std::uint64_t executable_phase_cost(const executable_policy::Phase& phase,const executable_policy::Price& price){
    const auto invalid=executable_policy::invalid_cost;
    if((phase.sdk_queries&&price.query_bound>invalid/phase.sdk_queries)||
       (phase.commands&&price.command_bound>invalid/phase.commands))return invalid;
    return executable_add(executable_add(phase.sdk_queries*price.query_bound,phase.commands*price.command_bound),phase.compute_bound);
}
static bool executable_phase_append(executable_policy::Branch& branch,executable_policy::Phase phase,unsigned id){
    if(branch.count==executable_policy::phase_capacity)return false;phase.id=id;branch.phases[branch.count++]=phase;return true;
}
static bool executable_action_phases(executable_policy::Branch& branch,const ExecutableActionCost& cost,
    unsigned base,bool validation_only=false,bool refused=false){
    if(!executable_phase_append(branch,cost.validation,base))return false;
    if(validation_only)return true;
    executable_policy::Phase command{};command.commands=1;
    return executable_phase_append(branch,command,base+1)&&executable_phase_append(branch,cost.feedback,base+2)&&
        (!refused||executable_phase_append(branch,cost.refusal_close,base+3))&&
        executable_phase_append(branch,cost.commit,base+(refused?5:4));
}
template<class Host> static bool executable_execution_fill_funding(Host& host,
    const executable_policy::Policy& policy,const ExecutableExecutionBudget& budget,executable_policy::Funding& funding,bool shape_checked=false){
    funding.success.count=funding.refusal_count=funding.changed_count=0;
    if(!budget.declared||!budget.branch_limit||budget.branch_limit>executable_policy::max_attempts||
       policy.success.count>budget.max_primary||policy.on_refusal.word.count>budget.max_fallback||
       policy.on_changed.word.count>budget.max_fallback||(!shape_checked&&!executable_policy::valid_shape(policy)))return false;
    // All shared IDs denote the same common-prefix position. Fallback paths
    // have their own IDs and cannot refund an already completed work prefix.
    for(unsigned i=0;i<policy.success.count;++i){const auto& action=policy.success.actions[i];
        const auto cost=host.cost(action);
        if(action.kind==executable_policy::ActionKind::wait){
            if(!executable_phase_append(funding.success,cost.validation,1+i*6))return false;
        }else if(!executable_action_phases(funding.success,cost,1+i*6))return false;
    }
    auto alternatives=[&](const executable_policy::GuardedWord& fallback,bool refusal){
        for(unsigned at=0;at<policy.success.count;++at){
            if(policy.success.actions[at].kind==executable_policy::ActionKind::wait)continue;
            const auto kind=policy.success.actions[at].kind;
            if(refusal&&kind!=executable_policy::ActionKind::move&&kind!=executable_policy::ActionKind::cast&&
               kind!=executable_policy::ActionKind::observe)continue;
            // A fallback can succeed, or any one of its attempts can refuse.
            // Such a second failure closes; it never reopens the primary word.
            const unsigned cases=fallback.declared?fallback.word.count+1:1;
            for(unsigned failure=0;failure<cases;++failure){
                if(failure){const auto kind=fallback.word.actions[failure-1].kind;
                    if(kind!=executable_policy::ActionKind::move&&kind!=executable_policy::ActionKind::cast&&
                       kind!=executable_policy::ActionKind::observe)continue;}
                auto& count=refusal?funding.refusal_count:funding.changed_count;
                if(count>=budget.branch_limit)return false;
                auto& branch=refusal?funding.refusals[count++]:funding.changes[count++];branch.count=0;
                for(unsigned i=0;i<at;++i)
                    if(!executable_action_phases(branch,host.cost(policy.success.actions[i]),1+i*6))return false;
                if(!executable_action_phases(branch,host.cost(policy.success.actions[at]),1+at*6,!refusal,refusal))return false;
                if(!fallback.declared)continue;
                for(unsigned i=0;i<fallback.word.count;++i){
                    const auto base=(refusal?100u:1000u)+at*64+i*6;
                    if(fallback.word.actions[i].kind==executable_policy::ActionKind::wait){
                        if(!executable_phase_append(branch,host.cost(fallback.word.actions[i]).validation,base))return false;
                    }else if(!executable_action_phases(branch,host.cost(fallback.word.actions[i]),base,false,failure==i+1))return false;
                    if(failure==i+1)break;
                }
            }
        }
        return true;
    };
    return alternatives(policy.on_refusal,true)&&alternatives(policy.on_changed,false);
}
static std::uint64_t executable_admission_bound(const executable_policy::Funding& funding,const ExecutableExecutionBudget& budget){
    if(budget.admission_bound)return budget.admission_bound;
    // A declared compute model over ONLY used phase records: active writes,
    // prefix scans and linear checked prices. Native measurements must qualify
    // these initial per-record declarations; finiteness alone is not fuel proof.
    std::uint64_t phases=funding.success.count;
    for(unsigned n=0;n<funding.refusal_count;++n)phases+=funding.refusals[n].count;
    for(unsigned n=0;n<funding.changed_count;++n)phases+=funding.changes[n].count;
    return 1024+phases*128+(1+funding.refusal_count+funding.changed_count)*64;
}
static constexpr std::uint64_t executable_shape_admission(const executable_policy::Policy& policy,const ExecutableExecutionBudget& budget){
    if(budget.admission_bound)return budget.admission_bound;
    if(policy.success.count>executable_policy::max_attempts||policy.on_refusal.word.count>executable_policy::max_attempts||
       policy.on_changed.word.count>executable_policy::max_attempts)return executable_policy::invalid_cost;
    auto refused=[](const ExecutableAction& action){return action.kind==executable_policy::ActionKind::move||
        action.kind==executable_policy::ActionKind::cast||action.kind==executable_policy::ActionKind::observe;};
    auto length=[](const executable_policy::Word& word,unsigned count){std::uint64_t phases=0;
        for(unsigned n=0;n<count;++n)phases+=word.actions[n].kind==executable_policy::ActionKind::wait?1:4;return phases;};
    std::uint64_t phases=length(policy.success,policy.success.count),branches=1;
    for(unsigned family=0;family<2;++family){const auto& fallback=family?policy.on_changed:policy.on_refusal;
        for(unsigned at=0;at<policy.success.count;++at){const auto& action=policy.success.actions[at];
            if(action.kind==executable_policy::ActionKind::wait||(!family&&!refused(action)))continue;
            const auto prefix=length(policy.success,at)+(family?1:5);
            phases+=prefix+(fallback.declared?length(fallback.word,fallback.word.count):0);++branches;
            if(fallback.declared)for(unsigned n=0;n<fallback.word.count;++n)if(refused(fallback.word.actions[n])){
                phases+=prefix+length(fallback.word,n)+5;++branches;}
        }
    }
    return 1024+phases*128+branches*64;
}
static std::uint64_t executable_action_price(const ExecutableActionCost& cost,const executable_policy::Price& price,bool refused=false){
    auto value=executable_add(executable_phase_cost(cost.validation,price),price.command_bound);
    value=executable_add(value,executable_phase_cost(cost.feedback,price));
    value=executable_add(value,executable_phase_cost(cost.commit,price));
    return refused?executable_add(value,executable_phase_cost(cost.refusal_close,price)):value;
}
static std::uint64_t executable_policy_quote_bound(const executable_policy::Policy& shape){
    return 1024+128*(shape.success.count+shape.on_refusal.word.count+shape.on_changed.word.count);
}
template<class Host> static executable_policy::FundingQuote executable_scalar_quote(Host& host,
    const executable_policy::Policy& shape,const ExecutableExecutionBudget& budget){
    executable_policy::FundingQuote result{};const auto bad=executable_policy::invalid_cost;
    if(!budget.declared||!budget.price.query_bound||!budget.price.command_bound||!budget.price.final_tail||
       !budget.branch_limit||budget.branch_limit>executable_policy::max_attempts||!executable_policy::valid_shape(shape)||
       shape.success.count>budget.max_primary||shape.on_refusal.word.count>budget.max_fallback||shape.on_changed.word.count>budget.max_fallback)return result;
    auto can_refuse=[](const ExecutableAction& action){return action.kind==executable_policy::ActionKind::move||
        action.kind==executable_policy::ActionKind::cast||action.kind==executable_policy::ActionKind::observe;};
    auto fallback=[&](const executable_policy::GuardedWord& branch,unsigned& cases,unsigned& phases){
        cases=1;phases=0;if(!branch.declared)return std::uint64_t(0);
        std::uint64_t prefix=0,worst=0;unsigned used=0;
        for(unsigned n=0;n<branch.word.count;++n){const auto& action=branch.word.actions[n];const auto cost=host.cost(action);
            if(action.kind==executable_policy::ActionKind::wait){prefix=executable_add(prefix,executable_phase_cost(cost.validation,budget.price));++used;}
            else{if(can_refuse(action)){worst=std::max(worst,executable_add(prefix,executable_action_price(cost,budget.price,true)));
                    phases=std::max(phases,used+5);++cases;}
                prefix=executable_add(prefix,executable_action_price(cost,budget.price));used+=4;}}
        phases=std::max(phases,used);return std::max(worst,prefix);
    };
    unsigned refusal_cases=0,changed_cases=0,refusal_phases=0,changed_phases=0;
    const auto on_refusal=fallback(shape.on_refusal,refusal_cases,refusal_phases);
    const auto on_changed=fallback(shape.on_changed,changed_cases,changed_phases);
    if(on_refusal==bad||on_changed==bad)return result;
    std::uint64_t prefix=0,refusal=0,changed=0;unsigned used=0,r_count=0,c_count=0;
    for(unsigned n=0;n<shape.success.count;++n){const auto& action=shape.success.actions[n];const auto cost=host.cost(action);
        if(action.kind==executable_policy::ActionKind::wait){prefix=executable_add(prefix,executable_phase_cost(cost.validation,budget.price));++used;continue;}
        c_count+=changed_cases;if(used+1+changed_phases>executable_policy::phase_capacity)return result;
        changed=std::max(changed,executable_add(executable_add(prefix,executable_phase_cost(cost.validation,budget.price)),on_changed));
        if(can_refuse(action)){r_count+=refusal_cases;if(used+5+refusal_phases>executable_policy::phase_capacity)return result;
            refusal=std::max(refusal,executable_add(executable_add(prefix,executable_action_price(cost,budget.price,true)),on_refusal));}
        prefix=executable_add(prefix,executable_action_price(cost,budget.price));used+=4;
    }
    if(used>executable_policy::phase_capacity||r_count>budget.branch_limit||c_count>budget.branch_limit)return result;
    result.success=executable_add(prefix,budget.price.final_tail);
    result.refusal=r_count?executable_add(refusal,budget.price.final_tail):0;
    result.changed=c_count?executable_add(changed,budget.price.final_tail):0;
    result.required=std::max({result.success,result.refusal,result.changed});result.valid=result.required!=bad;return result;
}
static constexpr std::uint64_t executable_family_quote_bound(){return 1024;}
template<class Host> static std::uint64_t executable_family_max_tail(Host& host,const ExecutableExecutionBudget& budget){
    if(!budget.declared||!budget.price.query_bound||!budget.price.command_bound||!budget.price.final_tail||
       budget.max_primary<2||budget.max_fallback<1||budget.branch_limit<4)return executable_policy::invalid_cost;
    // Initial canonical family: one CAMP Basic, optional compatible ordinary
    // Move, and one alternate Move after native FALSE/changed premise. It also
    // encloses the smaller standalone travel/recall/bank/capture operators.
    // No Token, Snapshot, Funding array or unknown physical value is involved.
    ExecutableAction basic{};basic.kind=executable_policy::ActionKind::basic;basic.target_kind=executable_policy::TargetKind::camp;
    ExecutableAction move{};move.kind=executable_policy::ActionKind::move;move.target_kind=executable_policy::TargetKind::ground;
    const auto work=host.cost(basic),travel=host.cost(move);
    auto maximum=executable_action_price(work,budget.price);
    maximum=executable_add(maximum,executable_action_price(travel,budget.price,true));
    maximum=executable_add(maximum,executable_action_price(travel,budget.price,true));
    maximum=executable_add(maximum,budget.price.final_tail);
    // Exactly65 used phase records/seven paths for Basic→Move, including both
    // native refusal positions in its predeclared alternate word (fixture).
    const auto admission=budget.admission_bound?budget.admission_bound:1024+65*128+7*64;
    return executable_add(maximum,admission);
}
#ifdef MOSAIC_EXECUTABLE_EXECUTION_CORE_ONLY
template<class Host> static executable_policy::Funding executable_execution_funding(Host& host,
    const executable_policy::Policy& policy,const ExecutableExecutionBudget& budget){
    executable_policy::Funding result{};if(!executable_execution_fill_funding(host,policy,budget,result))return {};return result;
}
#endif
// Resource preparation has one clock per input. Read-only observations and
// replacing a candidate do not renew it. This is an explicit declaration to be
// qualified natively, not an engine-fuel guarantee.
struct ExecutableCompilationReceipt {
    std::uint64_t serial{},epoch{},before_fuel{},after_fuel{},spent{};bool paid{};
};
struct ExecutableCompilationLedger {
    std::uint64_t epoch{executable_policy::invalid_cost},bound{},paid{},serial{},compiles{},binds{};
    std::uint64_t maximum{},interval_maximum{};
    executable_policy::Failure failure{executable_policy::Failure::none};
    bool failed{};ExecutableCompilationReceipt last{};
    void enter(std::uint64_t input,std::uint64_t limit){
        if(epoch==input){if(bound!=limit){failed=true;if(failure==executable_policy::Failure::none)failure=executable_policy::Failure::stale;}return;}
        if(epoch!=executable_policy::invalid_cost&&input<epoch){failed=true;if(failure==executable_policy::Failure::none)failure=executable_policy::Failure::stale;return;}
        epoch=input;bound=limit;paid=0;failed=!limit||limit==executable_policy::invalid_cost;failure=executable_policy::Failure::none;
    }
    std::uint64_t remaining()const{return failed||paid>bound?executable_policy::invalid_cost:bound-paid;}
    bool charge(std::uint64_t input,std::uint64_t before,std::uint64_t after){
        const auto spent=before>=after?before-after:executable_policy::invalid_cost;
        const bool okay=input==epoch&&!failed&&spent!=executable_policy::invalid_cost&&spent<=bound-paid;
        last={++serial,input,before,after,spent,okay};
        interval_maximum=std::max(interval_maximum,spent);
        if(!okay){failed=true;if(failure==executable_policy::Failure::none)failure=executable_policy::Failure::bound_overrun;return false;}
        paid+=spent;maximum=std::max(maximum,paid);return true;
    }
};
static bool executable_same_action(const ExecutableAction& a,const ExecutableAction& b){
    return a.kind==b.kind&&a.target_kind==b.target_kind&&a.target_id==b.target_id&&a.point==b.point&&a.slot==b.slot&&a.wait_reason==b.wait_reason;
}
static bool executable_same_word(const executable_policy::Word& a,const executable_policy::Word& b){
    if(a.count!=b.count)return false;for(unsigned n=0;n<a.count;++n)if(!executable_same_action(a.actions[n],b.actions[n]))return false;return true;
}
static bool executable_same_guarded(const executable_policy::GuardedWord& a,const executable_policy::GuardedWord& b){
    return a.declared==b.declared&&(!a.declared||(a.guard==b.guard&&a.exclude_point==b.exclude_point&&
        (!a.exclude_point||a.excluded==b.excluded)&&executable_same_word(a.word,b.word)));
}
static bool executable_same_shape(const executable_policy::Policy& a,const executable_policy::Policy& b){
    return a.owner==b.owner&&a.purpose==b.purpose&&executable_same_word(a.success,b.success)&&
        executable_same_guarded(a.on_refusal,b.on_refusal)&&executable_same_guarded(a.on_changed,b.on_changed);
}
static void executable_copy_shape(executable_policy::Policy& to,const executable_policy::Policy& from){
    to.token={};to.actual_prefix=0;to.owner=from.owner;to.purpose=from.purpose;to.success.count=from.success.count;
    for(unsigned n=0;n<from.success.count;++n)to.success.actions[n]=from.success.actions[n];
    auto copy=[](executable_policy::GuardedWord& a,const executable_policy::GuardedWord& b){
        a.declared=b.declared;a.guard=b.guard;a.exclude_point=b.exclude_point;a.excluded=b.excluded;a.word.count=b.declared?b.word.count:0;
        for(unsigned n=0;n<a.word.count;++n)a.word.actions[n]=b.word.actions[n];};
    copy(to.on_refusal,from.on_refusal);copy(to.on_changed,from.on_changed);
}
static bool executable_same_budget(const ExecutableExecutionBudget& a,const ExecutableExecutionBudget& b){
    return a.declared==b.declared&&a.price.query_bound==b.price.query_bound&&a.price.command_bound==b.price.command_bound&&
        a.price.final_tail==b.price.final_tail&&a.admission_bound==b.admission_bound&&a.branch_limit==b.branch_limit&&
        a.validation_compute==b.validation_compute&&a.feedback_compute==b.feedback_compute&&a.refusal_compute==b.refusal_compute&&
        a.commit_compute==b.commit_compute&&a.max_primary==b.max_primary&&a.max_fallback==b.max_fallback;
}
static bool executable_same_request(const ExecutableObservationRequest& a,const ExecutableObservationRequest& b){
    return a.owner==b.owner&&a.purpose==b.purpose&&a.phase==b.phase&&a.member==b.member&&a.group==b.group&&
        a.group_witness_member==b.group_witness_member&&a.enemy==b.enemy&&a.have_enemy==b.have_enemy&&a.move==b.move&&
        a.altar==b.altar&&a.have_move==b.have_move&&a.have_altar==b.have_altar&&a.need_group==b.need_group&&
        a.need_bag==b.need_bag&&a.need_bank_receipt==b.need_bank_receipt&&a.need_home==b.need_home&&
        a.need_target_shield==b.need_target_shield&&a.capture_inputs_remaining==b.capture_inputs_remaining;
}
static constexpr std::uint64_t executable_compilation_bound(const ExecutableExecutionBudget& budget){
    constexpr executable_policy::Policy canonical=[]()constexpr{
        executable_policy::Policy value{};value.success.count=2;
        value.success.actions[0].kind=executable_policy::ActionKind::basic;
        value.success.actions[0].target_kind=executable_policy::TargetKind::camp;
        value.success.actions[1].kind=executable_policy::ActionKind::move;
        auto set=[](executable_policy::GuardedWord& branch){branch.declared=true;branch.word.count=1;
            branch.word.actions[0].kind=executable_policy::ActionKind::move;};set(value.on_refusal);set(value.on_changed);return value;}();
    // Only the explicit override is a runtime dependency. Require the fixed
    // canonical metric to be evaluated by C++20, rather than walking it once
    // in the caller's preparation clock and again in the compiler's clock.
    constexpr ExecutableExecutionBudget default_metric{};
    constexpr auto canonical_metric=executable_shape_admission(canonical,default_metric);
    static_assert(canonical_metric==9792);
    return budget.admission_bound?budget.admission_bound:canonical_metric;
}
struct ExecutableCompiledPlan {
    bool valid{};std::uint64_t generation{};
    executable_policy::Policy shape;ExecutableObservationRequest request;ExecutableExecutionBudget budget;
    executable_policy::Funding funding;
    std::array<std::array<std::uint64_t,executable_policy::phase_capacity>,13> phase_costs{};
    std::array<std::uint64_t,13> initial{};
    std::array<ExecutableAction,18> cached_actions;
    std::array<ExecutableActionCost,18> cached_costs;
    unsigned cached_count{};
    std::uint64_t required{};unsigned branches{},used_phases{},cost_lookups{};
};
struct ExecutablePlanHandle {unsigned slot{};std::uint64_t generation{};bool valid{};};
struct ExecutablePlanStore {
    std::array<ExecutableCompiledPlan,2> plans;unsigned active{};std::uint64_t generation{};
    ExecutablePlanHandle pinned{};
    ExecutableCompilationLedger compilation;
    const ExecutableCompiledPlan* get(ExecutablePlanHandle h)const{
        return h.valid&&h.slot<plans.size()&&plans[h.slot].valid&&plans[h.slot].generation==h.generation?&plans[h.slot]:nullptr;
    }
    bool pin(ExecutablePlanHandle handle){if(!get(handle))return false;pinned=handle;return true;}
    void release(){pinned.valid=false;}
};
static bool executable_plan_matches(const ExecutableCompiledPlan& plan,const executable_policy::Policy& shape,
    const ExecutableExecutionBudget& budget,const ExecutableObservationRequest& request){
    return plan.valid&&executable_same_shape(plan.shape,shape)&&executable_same_request(plan.request,request)&&
        executable_same_budget(plan.budget,budget);
}
static const executable_policy::Branch& executable_plan_branch(const executable_policy::Funding& f,unsigned b){
    return b==0?f.success:b<=f.refusal_count?f.refusals[b-1]:f.changes[b-f.refusal_count-1];
}
template<class Host> static ExecutablePlanHandle executable_compile_plan(Host& host,const executable_policy::Policy& shape,
    const ExecutableExecutionBudget& budget,const ExecutableObservationRequest& request,ExecutablePlanStore& store,std::uint64_t epoch,
    std::uint64_t retained_tail=0,std::uint64_t phase_entry=executable_policy::invalid_cost){
    const auto before=phase_entry==executable_policy::invalid_cost?host.fuel():phase_entry;
    auto& ledger=store.compilation;ledger.enter(epoch,executable_compilation_bound(budget));++ledger.compiles;
    auto finish=[&](ExecutablePlanHandle handle){if(!ledger.charge(epoch,before,host.fuel()))return ExecutablePlanHandle{};
        const auto* existing=store.get(handle);
        if(existing&&host.fuel()<=executable_add(existing->required,ledger.remaining()))return ExecutablePlanHandle{};return handle;};
    if(ledger.failed)return finish({});
    const auto retained=retained_tail?retained_tail:budget.price.final_tail;
    if(!retained||host.fuel()<=executable_add(ledger.remaining(),retained)){
        if(ledger.failure==executable_policy::Failure::none)ledger.failure=executable_policy::Failure::unfunded;return finish({});}
    for(unsigned slot=0;slot<store.plans.size();++slot){const auto& existing=store.plans[slot];
        if(executable_plan_matches(existing,shape,budget,request))return finish({slot,existing.generation,true});}
    const unsigned staging=store.get(store.pinned)?store.pinned.slot^1u:store.active^1u;
    auto& plan=store.plans[staging];plan.valid=false;plan.cost_lookups=0;
    if(request.owner!=shape.owner||request.purpose!=shape.purpose||!budget.declared||!budget.price.query_bound||!budget.price.command_bound||!budget.price.final_tail||
       !budget.branch_limit||budget.branch_limit>executable_policy::max_attempts||shape.success.count>budget.max_primary||
       shape.on_refusal.word.count>budget.max_fallback||shape.on_changed.word.count>budget.max_fallback||!executable_policy::valid_shape(shape))return finish({});
    // Cache each active action position once. Branch expansion reuses these
    // declared costs; it never queries the SDK or physical facts.
    struct CachedHost {
        Host* actual;ExecutableCompiledPlan* output;
        ExecutableActionCost cost(const ExecutableAction& action){
            auto& count=output->cached_count;
            for(unsigned n=0;n<count;++n)if(executable_same_action(output->cached_actions[n],action))return output->cached_costs[n];
            if(count==output->cached_actions.size())return {};
            output->cached_actions[count]=action;output->cached_costs[count]=actual->cost(action);
            ++output->cost_lookups;return output->cached_costs[count++];
        }
    } cached{&host,&plan};
    plan.cached_count=0;if(!executable_execution_fill_funding(cached,shape,budget,plan.funding,true))return finish({});
    plan.branches=1+plan.funding.refusal_count+plan.funding.changed_count;plan.required=0;plan.used_phases=0;
    for(unsigned b=0;b<plan.branches;++b){const auto& path=executable_plan_branch(plan.funding,b);auto total=budget.price.final_tail;
        if(!path.count||path.count>executable_policy::phase_capacity)return finish({});
        for(unsigned p=0;p<path.count;++p){const auto& phase=path.phases[p];if(!phase.id)return finish({});
            const auto cost=executable_phase_cost(phase,budget.price);plan.phase_costs[b][p]=cost;total=executable_add(total,cost);++plan.used_phases;}
        if(total==executable_policy::invalid_cost)return finish({});plan.initial[b]=total;plan.required=std::max(plan.required,total);
    }
    executable_copy_shape(plan.shape,shape);plan.request=request;plan.budget=budget;
    const auto old_active=store.active;const auto old_pinned=store.pinned;
    plan.generation=++store.generation;plan.valid=true;store.active=staging;
    const ExecutablePlanHandle handle{staging,plan.generation,true};if(!store.get(store.pinned))store.pin(handle);
    if(!ledger.charge(epoch,before,host.fuel())||host.fuel()<=executable_add(plan.required,ledger.remaining())){
        plan.valid=false;store.active=old_active;store.pinned=old_pinned;return {};}
    return handle;
}
static std::uint64_t executable_plan_required(const ExecutableCompiledPlan& plan,const ExecutableCompilationLedger& ledger){
    return plan.valid?executable_add(plan.required,ledger.remaining()):executable_policy::invalid_cost;
}
struct ExecutableFundingCursor {
    const executable_policy::Funding* funding{};executable_policy::Price price{};
    const ExecutableCompiledPlan* compiled{};
    std::array<unsigned,13> cursor{};std::uint16_t active{};bool valid{};
    std::array<std::uint64_t,13> unpaid{};
    unsigned count()const{return funding?1+funding->refusal_count+funding->changed_count:0;}
    const executable_policy::Branch& branch(unsigned index)const{
        return index==0?funding->success:index<=funding->refusal_count?funding->refusals[index-1]:
            funding->changes[index-funding->refusal_count-1];
    }
    std::uint64_t cost(unsigned b,unsigned p)const{return compiled?compiled->phase_costs[b][p]:executable_phase_cost(branch(b).phases[p],price);}
    bool start(const ExecutableCompiledPlan& plan,std::uint64_t fuel){
        compiled=&plan;funding=&plan.funding;price=plan.budget.price;valid=plan.valid&&fuel>plan.required;
        active=valid?std::uint16_t((1u<<plan.branches)-1u):0;
        for(unsigned b=0;b<plan.branches;++b){cursor[b]=0;unpaid[b]=plan.initial[b];}return valid;
    }
    bool start(const executable_policy::Funding& plan,const executable_policy::Price& rate,std::uint64_t fuel){
        // Only executable_execution_fill_funding supplies this cursor. Its
        // fixed ID construction proves common-prefix identity; the strict
        // public funding checker remains the independent fixture oracle.
        compiled=nullptr;funding=&plan;price=rate;valid=rate.query_bound&&rate.command_bound&&rate.final_tail&&
            plan.refusal_count<=executable_policy::max_attempts&&plan.changed_count<=executable_policy::max_attempts;
        std::uint64_t required=0;
        for(unsigned b=0;valid&&b<count();++b){cursor[b]=0;const auto& path=branch(b);valid=path.count&&path.count<=executable_policy::phase_capacity;
            auto cost=rate.final_tail;for(unsigned p=0;valid&&p<path.count;++p){valid=path.phases[p].id!=0;
                cost=executable_add(cost,executable_phase_cost(path.phases[p],rate));}
            valid=valid&&cost!=executable_policy::invalid_cost;unpaid[b]=cost;required=std::max(required,cost);}
        valid=valid&&fuel>required;active=valid?std::uint16_t((1u<<count())-1u):0;return valid;
    }
    std::uint64_t remaining()const{
        if(!valid)return executable_policy::invalid_cost;std::uint64_t worst=0;
        for(unsigned b=0;b<count();++b)if(active&(1u<<b))worst=std::max(worst,unpaid[b]);
        return worst;
    }
    bool begin(unsigned id,std::uint64_t fuel,std::uint64_t& phase_bound,std::uint64_t& after_phase){
        if(!valid)return false;std::uint16_t next=0;phase_bound=0;
        for(unsigned b=0;b<count();++b)if(active&(1u<<b)){
            const auto& path=branch(b);if(cursor[b]<path.count&&path.phases[cursor[b]].id==id){
                const auto bound=cost(b,cursor[b]);
                if(next&&bound!=phase_bound)return valid=false;
                next|=std::uint16_t(1u<<b);phase_bound=bound;}}
        if(!next||phase_bound==executable_policy::invalid_cost)return valid=false;
        active=next;if(fuel<=remaining())return valid=false;
        after_phase=0;
        for(unsigned b=0;b<count();++b)if(active&(1u<<b))after_phase=std::max(after_phase,unpaid[b]-phase_bound);
        return true;
    }
    bool finish(unsigned id,std::uint64_t spent,std::uint64_t fuel){
        if(!valid)return false;
        for(unsigned b=0;b<count();++b)if(active&(1u<<b)){
            const auto& phase=branch(b).phases[cursor[b]];
            const auto value=cost(b,cursor[b]);
            if(phase.id!=id||spent>value||unpaid[b]<value)return valid=false;
            unpaid[b]-=value;++cursor[b];}
        return fuel>remaining()?true:(valid=false);
    }
    bool complete(std::uint64_t fuel){
        if(!valid)return false;std::uint16_t ended=0;
        for(unsigned b=0;b<count();++b)if((active&(1u<<b))&&cursor[b]==branch(b).count)ended|=std::uint16_t(1u<<b);
        active=ended;return ended&&fuel>price.final_tail;
    }
};
static void executable_failure(ExecutableExecutionState& state,ExecutableExecutionOutcome& out,
    executable_policy::Failure failure,ExecutablePhase phase,const ExecutableSnapshot& snapshot,unsigned primitive){
    if(out.first_failure.record(failure,snapshot.stamp,primitive))out.first_failure_phase=phase;
    if(state.first_failure.record(failure,snapshot.stamp,primitive))state.first_failure_phase=phase;
    if(failure==executable_policy::Failure::bound_overrun)state.input_blocked=true;
}
static bool executable_fallback_allowed(const executable_policy::GuardedWord& branch,const ExecutableSnapshot& snapshot,
    const ExecutableAction& failed,bool refusal){
    if(!branch.declared||!executable_policy::guard_allows(branch.guard,snapshot))return false;
    for(unsigned n=0;n<branch.word.count;++n){const auto& action=branch.word.actions[n];
        const bool landing=action.kind==executable_policy::ActionKind::move||
            ((action.kind==executable_policy::ActionKind::cast||action.kind==executable_policy::ActionKind::observe)&&
             action.target_kind==executable_policy::TargetKind::ground);
        if(landing&&((branch.exclude_point&&action.point==branch.excluded)||(refusal&&action.point==failed.point)))return false;
    }
    return true;
}
static void executable_truthful_after(ExecutableReceipt& receipt,const ExecutableSnapshot& snapshot,bool complete){
    receipt.after=snapshot.stamp;receipt.after_pose=snapshot.pose;receipt.after_authority=snapshot.authority;
    receipt.actual_body=snapshot.body_actual&&snapshot.restrictions_actual&&snapshot.channel_actual;
    receipt.health_actual=receipt.health_actual&&snapshot.health_actual;receipt.after_health=snapshot.health;
    receipt.shield_actual=receipt.shield_actual&&snapshot.shield_actual;receipt.after_shield=snapshot.shield;
    receipt.gems_actual=receipt.gems_actual&&snapshot.gems_actual;receipt.after_gems=snapshot.gems;
    for(unsigned n=0;n<snapshot.target_count;++n){const auto& target=snapshot.targets[n];
        if(target.kind==receipt.action.target_kind&&target.id==receipt.action.target_id&&target.actual&&target.visible){
            receipt.target_kind=target.kind;receipt.target_id=target.id;receipt.target_life_actual=true;receipt.target_alive=target.alive;break;}}
    if(!complete){receipt.actual_targets=false;receipt.actual_occupancy=false;}
}
template<class Host> static bool executable_close_after_overrun(Host& host,const ExecutableAction& action,
    const ExecutableAttempt& attempt,const ExecutableExecutionBudget& budget,ExecutableExecutionState& state,
    ExecutableExecutionOutcome& out,ExecutableSnapshot& snapshot,bool feedback_already=false,bool feedback_complete=false){
    const auto cost=host.cost(action);auto& receipt=state.pending_receipt;
    receipt.outcome=attempt.refused?executable_policy::Outcome::refused:attempt.sdk_code?
        executable_policy::Outcome::changed_premise:executable_policy::Outcome::accepted;
    const auto feedback=feedback_already?0:executable_phase_cost(cost.feedback,budget.price);
    const auto closure=attempt.refused?executable_phase_cost(cost.refusal_close,budget.price):0;
    const auto commit=executable_phase_cost(cost.commit,budget.price);
    auto remaining=executable_add(executable_add(feedback,closure),executable_add(commit,budget.price.final_tail));
    if(remaining==executable_policy::invalid_cost||host.fuel()<=remaining)return false;
    auto paid=[&](ExecutablePhase phase,std::uint64_t bound,auto&& callback){
        if(host.fuel()<=remaining)return false;remaining-=bound;host.reserve(remaining);
        const auto before=host.fuel();const bool ok=callback();const auto after=host.fuel();const auto spent=before>=after?before-after:0;
        state.phase_max[unsigned(phase)]=std::max(state.phase_max[unsigned(phase)],spent);state.phase_spent[unsigned(phase)]+=spent;
        return ok&&spent<=bound&&after>remaining;
    };
    const bool feedback_ok=feedback_already?feedback_complete:paid(ExecutablePhase::feedback,feedback,[&]{
        receipt.health_actual=state.pending_before_health;receipt.shield_actual=state.pending_before_shield;receipt.gems_actual=state.pending_before_gems;
        return host.feedback(action,attempt,snapshot,receipt);});
    const bool close_ok=!attempt.refused||paid(ExecutablePhase::refusal_close,closure,[&]{return host.close_refusal(action,attempt,snapshot,receipt);});
    executable_truthful_after(receipt,snapshot,feedback_ok&&close_ok);receipt.serial=++state.receipt_serial;receipt.actual_attempt=true;
    out.last_receipt=receipt;
    const bool commit_ok=paid(ExecutablePhase::commit,commit,[&]{
        const bool accepted=attempt.sdk_code?true:state.service.accept(receipt);state.pending_recorded=true;
        const bool hooks=host.commit(receipt);state.pending_hooks=hooks;
        if constexpr(requires {host.service_events(receipt,state.service);})host.service_events(receipt,state.service);
        return accepted&&hooks;
    });
    out.feedback_closed=feedback_ok&&close_ok&&commit_ok;state.pending_feedback=!out.feedback_closed;return out.feedback_closed;
}
template<class Host> static ExecutableExecutionOutcome executable_reconcile_pending(Host& host,
    const ExecutableExecutionBudget& budget,ExecutableExecutionState& state){
    ExecutableExecutionOutcome out;out.status=ExecutableStatus::feedback_missing;out.first_failure=state.first_failure;
    out.first_failure_phase=state.first_failure_phase;out.service_key=state.pending_receipt.owner;
    auto& current=host.snapshot();const auto entry=host.fuel();
    auto finish=[&](ExecutableStatus status){out.status=status;out.actual_prefix=state.service.actual_prefix();
        out.next_required=state.service.next_required();out.needs_continuation=state.service.stage()!=executable_policy::Stage::complete;
        out.last_receipt=state.pending_receipt;out.fuel_spent=entry>=host.fuel()?entry-host.fuel():0;state.last=out;return out;};
    if(!state.pending_feedback)return finish(ExecutableStatus::empty);
    const auto& old=state.pending_receipt;
    bool commit_known=false;
    if constexpr(requires {host.pending_commit_known(old);})commit_known=host.pending_commit_known(old);
    const bool fresh=executable_policy::token_matches(host.token(),current)&&current.body_actual&&current.restrictions_actual&&current.channel_actual&&
        current.stamp.epoch>=old.before.epoch&&current.stamp.host_attempt>=old.after.host_attempt&&
        (current.stamp.epoch>old.after.epoch||current.stamp.frame>old.after.frame||current.stamp.host_attempt>old.after.host_attempt||
         current.stamp.observation>old.after.observation||(commit_known&&old.actual_body&&old.actual_targets&&old.actual_occupancy));
    if(!fresh)return finish(ExecutableStatus::feedback_missing);
    if(state.input_epoch!=current.stamp.epoch){state.input_epoch=current.stamp.epoch;
        state.input_attempts=state.input_successful=state.input_refusals=0;state.input_blocked=false;
        state.first_failure={};state.first_failure_phase=ExecutablePhase::idle;}
    const auto cost=host.cost(state.pending_action).commit;const auto bound=executable_phase_cost(cost,budget.price);
    if(!budget.declared||cost.sdk_queries||cost.commands||bound==executable_policy::invalid_cost||
       entry<=executable_add(bound,budget.price.final_tail))return finish(ExecutableStatus::unfunded);
    host.reserve(budget.price.final_tail);const auto start=host.fuel();
    // The fresh current packet closes acquisition. It does NOT retroactively
    // attribute a later pose/HP to an old attempt or fabricate an old edge.
    if(!state.pending_recorded){auto& receipt=state.pending_receipt;receipt.serial=++state.receipt_serial;
        receipt.actual_attempt=true;
        if(!receipt.actual_body)receipt.health_actual=receipt.shield_actual=receipt.gems_actual=receipt.target_life_actual=false;
        if(!state.pending_attempt.sdk_code)state.service.accept(receipt);state.pending_recorded=true;
    }
    // An exact causal ACK can precede an outer compute-bound failure. Those
    // hooks already ran; replaying a weakened receipt would both duplicate
    // effects and fail the producer's committed-serial identity check.
    if(commit_known)state.pending_hooks=true;
    if(!state.pending_hooks){auto minimal=state.pending_receipt;
        minimal.actual_body=minimal.actual_targets=minimal.actual_occupancy=false;
        minimal.health_actual=minimal.shield_actual=minimal.gems_actual=minimal.target_life_actual=false;
        state.pending_hooks=host.commit(minimal);}
    if(state.pending_hooks){
        if constexpr(requires {host.reconcile_unknown();})host.reconcile_unknown();
        state.service.handoff_unknown();state.pending_feedback=false;out.feedback_closed=true;
    }
    const auto spent=start>=host.fuel()?start-host.fuel():0;
    state.phase_spent[unsigned(ExecutablePhase::commit)]+=spent;
    state.phase_max[unsigned(ExecutablePhase::commit)]=std::max(state.phase_max[unsigned(ExecutablePhase::commit)],spent);
    if(spent>bound||host.fuel()<=budget.price.final_tail){++state.overruns;
        executable_failure(state,out,executable_policy::Failure::bound_overrun,ExecutablePhase::commit,current,0);
        return finish(ExecutableStatus::overrun);}
    if(!state.pending_hooks)return finish(ExecutableStatus::feedback_missing);
    return finish(ExecutableStatus::reconciled);
}
template<class Host> static ExecutableExecutionOutcome executable_execute(Host& host,const executable_policy::Policy& policy,
    const ExecutableExecutionBudget& budget,ExecutableExecutionState& state,
    const ExecutableCompiledPlan* compiled=nullptr,ExecutableCompilationLedger* compilation=nullptr,
    std::uint64_t phase_entry=executable_policy::invalid_cost){
    const auto entry=host.fuel();ExecutableExecutionOutcome out;out.service_key=policy.owner;++state.calls;
    const auto admission_entry=phase_entry==executable_policy::invalid_cost?entry:phase_entry;
    auto& snapshot=host.snapshot();
    bool admission_charged=false;
    auto finish=[&](ExecutableStatus status){
        if(compiled&&compilation&&!admission_charged){admission_charged=true;
            const auto elapsed=admission_entry>=host.fuel()?admission_entry-host.fuel():0;
            state.phase_max[unsigned(ExecutablePhase::admission)]=std::max(state.phase_max[unsigned(ExecutablePhase::admission)],elapsed);
            state.phase_spent[unsigned(ExecutablePhase::admission)]+=elapsed;
            if(!compilation->charge(snapshot.stamp.epoch,admission_entry,host.fuel())){
                executable_failure(state,out,executable_policy::Failure::bound_overrun,ExecutablePhase::admission,snapshot,0);++state.overruns;status=ExecutableStatus::overrun;}}
        out.status=status;out.actual_prefix=state.service.actual_prefix();
        out.next_required=state.service.next_required();out.needs_continuation=state.service.stage()!=executable_policy::Stage::complete;
        out.fuel_spent=entry>=host.fuel()?entry-host.fuel():0;out.raw_m_after=snapshot.authority.raw_m;
        out.restricted_move_after=(snapshot.authority.restrictions&1)!=0;state.last=out;return out;};
    out.raw_m_before=snapshot.authority.raw_m;out.restricted_move_before=(snapshot.authority.restrictions&1)!=0;
    if(state.pending_feedback)return executable_reconcile_pending(host,budget,state);
    if(state.input_epoch!=snapshot.stamp.epoch){state.input_epoch=snapshot.stamp.epoch;
        state.input_attempts=state.input_successful=state.input_refusals=0;state.input_blocked=false;state.first_failure={};state.first_failure_phase=ExecutablePhase::idle;}
    if(state.input_blocked){out.first_failure=state.first_failure;out.first_failure_phase=state.first_failure_phase;return finish(ExecutableStatus::overrun);}
    if(compiled&&compilation){++compilation->binds;
        if(compilation->epoch!=snapshot.stamp.epoch||host.fuel()<=executable_plan_required(*compiled,*compilation)){
            executable_failure(state,out,executable_policy::Failure::unfunded,ExecutablePhase::admission,snapshot,0);
            ++state.unfunded;return finish(ExecutableStatus::unfunded);}}
    if(compiled?(!compilation||!compiled->valid||!executable_same_shape(compiled->shape,policy)||
            !executable_same_budget(compiled->budget,budget)||!policy.token.valid||!policy.token.lease):!executable_policy::valid_policy(policy)){
        executable_failure(state,out,executable_policy::Failure::invalid_policy,ExecutablePhase::admission,snapshot,0);
        return finish(ExecutableStatus::invalid);}
    if(!executable_policy::token_matches(policy.token,snapshot)||!executable_policy::token_matches(host.token(),snapshot)){
        executable_failure(state,out,executable_policy::Failure::stale,ExecutablePhase::admission,snapshot,0);return finish(ExecutableStatus::stale);}
    if(policy.actual_prefix!=state.service.actual_prefix()){
        executable_failure(state,out,executable_policy::Failure::stale,ExecutablePhase::admission,snapshot,0);return finish(ExecutableStatus::stale);}
    const auto admission_bound=compiled?compilation->remaining():executable_shape_admission(policy,budget);
    if(!budget.declared||admission_bound==executable_policy::invalid_cost||entry<=executable_add(admission_bound,budget.price.final_tail)){
        executable_failure(state,out,executable_policy::Failure::unfunded,ExecutablePhase::admission,snapshot,0);++state.unfunded;
        return finish(ExecutableStatus::unfunded);}
    ExecutableFundingCursor cursor;bool funded=false;
    if(compiled){
        funded=compilation->epoch==snapshot.stamp.epoch&&host.fuel()>executable_plan_required(*compiled,*compilation)&&cursor.start(*compiled,host.fuel());
    }else{const bool funding_ready=executable_execution_fill_funding(host,policy,budget,state.funding);
        funded=funding_ready&&cursor.start(state.funding,budget.price,host.fuel());}
    const auto admission_spent=admission_entry>=host.fuel()?admission_entry-host.fuel():0;
    state.phase_max[unsigned(ExecutablePhase::admission)]=std::max(state.phase_max[unsigned(ExecutablePhase::admission)],admission_spent);
    state.phase_spent[unsigned(ExecutablePhase::admission)]+=admission_spent;
    admission_charged=true;
    if(compiled?!compilation->charge(snapshot.stamp.epoch,admission_entry,host.fuel()):admission_spent>admission_bound){
        executable_failure(state,out,executable_policy::Failure::bound_overrun,ExecutablePhase::admission,snapshot,0);
        ++state.overruns;return finish(ExecutableStatus::overrun);}
    if(!funded){executable_failure(state,out,executable_policy::Failure::unfunded,ExecutablePhase::admission,snapshot,0);
        ++state.unfunded;return finish(ExecutableStatus::unfunded);}
    const executable_policy::Word* word=&policy.success;unsigned family=0,parent=0;bool fallback=false;
    auto phase=[&](unsigned id,ExecutablePhase kind,auto&& action){
        std::uint64_t bound=0,tail=0;
        if(!cursor.begin(id,host.fuel(),bound,tail)){executable_failure(state,out,executable_policy::Failure::unfunded,kind,snapshot,out.attempts);
            ++state.unfunded;return false;}
        host.reserve(tail);const auto before=host.fuel();const bool ok=action();const auto after=host.fuel();
        const auto spent=before>=after?before-after:0;state.phase_max[unsigned(kind)]=std::max(state.phase_max[unsigned(kind)],spent);
        state.phase_spent[unsigned(kind)]+=spent;
        if(!cursor.finish(id,spent,after)){executable_failure(state,out,executable_policy::Failure::bound_overrun,kind,snapshot,out.attempts);
            ++state.overruns;return false;}return ok;
    };
    for(unsigned index=0;index<word->count;){const auto action=word->actions[index];
        const unsigned base=family==0?1+index*6:(family==1?100u:1000u)+parent*64+index*6;
        bool validated=false;
        if(!phase(base,ExecutablePhase::validation,[&]{
            if(!index&&!family)state.service.request(policy.owner,policy.purpose);
            validated=host.validate(action,snapshot);return true;}))return finish(ExecutableStatus::overrun);
        auto current_token=host.token();validated=validated&&executable_policy::token_matches(current_token,snapshot)&&executable_policy::authorize(snapshot,action);
        if(!validated){executable_failure(state,out,executable_policy::Failure::changed_premise,ExecutablePhase::validation,snapshot,index);
            if(!fallback&&executable_fallback_allowed(policy.on_changed,snapshot,action,false)){
                parent=index;family=2;word=&policy.on_changed.word;index=0;fallback=true;++state.fallbacks;continue;}
            return finish(ExecutableStatus::unsupported);}
        if(action.kind==executable_policy::ActionKind::wait){
            if(!host.finite_wait(action,snapshot)){executable_failure(state,out,executable_policy::Failure::unsupported,ExecutablePhase::validation,snapshot,index);
                return finish(ExecutableStatus::unsupported);}
            out.finite_wait=true;out.closed=true;return finish(ExecutableStatus::finite_wait);}
        if(state.input_attempts>=executable_policy::max_attempts||state.input_successful>=executable_policy::max_successful||state.input_refusals>=2){
            executable_failure(state,out,executable_policy::Failure::unsupported,ExecutablePhase::command,snapshot,index);return finish(ExecutableStatus::unsupported);}
        ExecutableAttempt attempt{};auto& receipt=state.pending_receipt;
        if(!phase(base+1,ExecutablePhase::command,[&]{
            state.pending_recorded=state.pending_hooks=false;
            receipt={};
            receipt.owner=policy.owner;receipt.action=action;receipt.before=snapshot.stamp;
            receipt.before_pose=snapshot.pose;receipt.before_authority=snapshot.authority;
            receipt.health_actual=snapshot.health_actual;receipt.before_health=snapshot.health;
            receipt.shield_actual=snapshot.shield_actual;receipt.before_shield=snapshot.shield;
            receipt.gems_actual=snapshot.gems_actual;receipt.before_gems=snapshot.gems;
            state.pending_before_health=snapshot.health_actual;state.pending_before_shield=snapshot.shield_actual;state.pending_before_gems=snapshot.gems_actual;
            if(!host.consume(current_token))return false;attempt=host.attempt(action);
            receipt.after=snapshot.stamp;receipt.actual_attempt=attempt.attempted;
            receipt.health_actual=receipt.shield_actual=receipt.gems_actual=false;
            receipt.outcome=attempt.refused?executable_policy::Outcome::refused:attempt.sdk_code?
                executable_policy::Outcome::changed_premise:executable_policy::Outcome::accepted;
            return attempt.attempted;})){
            // Even an over-budget command may have physically applied. Its
            // exact attempted/applied truth survives; no stale suffix runs.
            if(attempt.attempted){out.attempted=true;out.actual_effect=out.actual_effect||attempt.applied;
                ++out.attempts;++state.input_attempts;++state.attempts;
                if(attempt.applied){++out.successful;++state.input_successful;++state.applied;}
                if(attempt.refused){++out.refusals;++state.input_refusals;++state.refused;}
                out.sdk_code=attempt.sdk_code;state.pending_feedback=true;state.pending_action=action;state.pending_attempt=attempt;
                executable_close_after_overrun(host,action,attempt,budget,state,out,snapshot);
            }else if(cursor.valid){executable_failure(state,out,executable_policy::Failure::stale,ExecutablePhase::command,snapshot,index);
                return finish(ExecutableStatus::stale);}
            return finish(ExecutableStatus::overrun);}
        if(!attempt.attempted){executable_failure(state,out,executable_policy::Failure::stale,ExecutablePhase::command,snapshot,index);
            return finish(ExecutableStatus::stale);}
        ++out.attempts;++state.input_attempts;++state.attempts;out.attempted=true;
        out.sdk_code=attempt.sdk_code;
        if(attempt.applied){++out.successful;++state.input_successful;++state.applied;out.actual_effect=true;}
        if(attempt.refused){++out.refusals;++state.input_refusals;++state.refused;
            executable_failure(state,out,executable_policy::Failure::refused,ExecutablePhase::command,snapshot,index);}
        state.pending_feedback=true;state.pending_action=action;state.pending_attempt=attempt;
        receipt.outcome=attempt.refused?executable_policy::Outcome::refused:attempt.sdk_code?
            executable_policy::Outcome::changed_premise:executable_policy::Outcome::accepted;
        bool feedback_complete=false;
        const bool feedback_ok=phase(base+2,ExecutablePhase::feedback,[&]{
            receipt.health_actual=state.pending_before_health;receipt.shield_actual=state.pending_before_shield;receipt.gems_actual=state.pending_before_gems;
            feedback_complete=host.feedback(action,attempt,snapshot,receipt);return feedback_complete;});
        if(!feedback_ok)executable_failure(state,out,executable_policy::Failure::feedback_missing,ExecutablePhase::feedback,snapshot,index);
        if(!cursor.valid){executable_close_after_overrun(host,action,attempt,budget,state,out,snapshot,true,feedback_complete);
            ++state.feedback_failed;return finish(ExecutableStatus::overrun);}
        bool refusal_closed=true;
        if(attempt.refused&&cursor.valid)refusal_closed=phase(base+3,ExecutablePhase::refusal_close,[&]{return host.close_refusal(action,attempt,snapshot,receipt);});
        if(!refusal_closed)executable_failure(state,out,executable_policy::Failure::feedback_missing,ExecutablePhase::refusal_close,snapshot,index);
        executable_truthful_after(receipt,snapshot,feedback_ok&&refusal_closed);
        receipt.serial=++state.receipt_serial;receipt.actual_attempt=true;out.last_receipt=receipt;
        bool committed=false;
        if(cursor.valid)committed=phase(base+(attempt.refused?5:4),ExecutablePhase::commit,[&]{
            const bool accepted=attempt.sdk_code?true:state.service.accept(receipt);
            state.pending_recorded=true;const bool hooks=host.commit(receipt);state.pending_hooks=hooks;
            if constexpr(requires {host.service_events(receipt,state.service);})host.service_events(receipt,state.service);
            return accepted&&hooks;});
        if(!feedback_ok||!refusal_closed||!committed){++state.feedback_failed;
            executable_failure(state,out,executable_policy::Failure::feedback_missing,ExecutablePhase::commit,snapshot,index);
            return finish(ExecutableStatus::feedback_missing);}
        state.pending_feedback=false;out.last_receipt=receipt;out.feedback_closed=true;
        if(attempt.sdk_code){executable_failure(state,out,executable_policy::Failure::unsupported,ExecutablePhase::command,snapshot,index);
            return finish(ExecutableStatus::unsupported);}
        if(attempt.refused){
            if(!fallback&&state.input_refusals<2&&executable_fallback_allowed(policy.on_refusal,snapshot,action,true)){
                parent=index;family=1;word=&policy.on_refusal.word;index=0;fallback=true;++state.fallbacks;continue;}
            if(state.input_refusals>=2)state.service.recover();
            return finish(ExecutableStatus::unsupported);}
        ++index;
    }
    if(!cursor.complete(host.fuel())){executable_failure(state,out,executable_policy::Failure::bound_overrun,ExecutablePhase::complete,snapshot,out.attempts);
        return finish(ExecutableStatus::overrun);}
    if constexpr(requires {state.service.word_boundary(state.service.actual_prefix());}){
        if(!state.service.word_boundary(state.service.actual_prefix())){
            executable_failure(state,out,executable_policy::Failure::feedback_missing,ExecutablePhase::complete,snapshot,out.attempts);
            return finish(ExecutableStatus::unsupported);}
    }
    out.closed=true;++state.words;return finish(fallback?ExecutableStatus::fallback_applied:ExecutableStatus::word_applied);
}

#ifndef MOSAIC_EXECUTABLE_EXECUTION_CORE_ONLY
struct ExecutableNativeExecutionState {
    ExecutableExecutionState execution;
    ExecutablePlanStore plans;
    ExecutableObservationRequest request,selected_request;bool selected_request_valid{};
    ExecutableExecutionBudget budget;
    std::uint64_t required_calls{},required_spent{},required_max{},required_overruns{},service_serial{};
};
static ExecutableNativeExecutionState& executable_native_execution_state(){static constinit ExecutableNativeExecutionState value{};return value;}
static ExecutableExecutionState& executable_execution_state(){return executable_native_execution_state().execution;}
static ExecutableCompilationLedger& executable_compilation_state(){return executable_native_execution_state().plans.compilation;}
struct ExecutableNativeHost {
    Runtime* runtime;const ExecutableObservationRequest* request;const ExecutableExecutionBudget* budget;
    std::uint64_t retained_tail{};
    std::uint64_t fuel()const{return fuel_remaining();}
    ExecutableSnapshot& snapshot(){return runtime->executable_observe_state().prepared.snapshot;}
    executable_policy::Token token()const{return runtime->executable_observe_token();}
    void reserve(std::uint64_t tail){retained_tail=tail;}
    ExecutableActionCost cost(const ExecutableAction& action)const{
        const auto bounds=runtime->executable_observe_query_bounds(action,*request);ExecutableActionCost result;
        result.validation.sdk_queries=bounds.validation;result.validation.compute_bound=budget->validation_compute;
        result.feedback.sdk_queries=bounds.feedback;result.feedback.compute_bound=budget->feedback_compute;
        result.refusal_close.sdk_queries=bounds.refusal_close;result.refusal_close.compute_bound=budget->refusal_compute;
        result.commit.compute_bound=budget->commit_compute;return result;
    }
    bool validate(const ExecutableAction& action,ExecutableSnapshot& current){
        return runtime->executable_observe_validate(action,current,retained_tail,budget->validation_compute);
    }
    bool consume(executable_policy::Token& token){return runtime->executable_observe_consume(token);}
    ExecutableAttempt attempt(const ExecutableAction& action){
        ExecutableAttempt result;result.attempted=true;std::int32_t accepted=0;
        const auto entity=action.target_kind==executable_policy::TargetKind::champion?std::uint32_t(detail::entity_kind::champion):
            action.target_kind==executable_policy::TargetKind::camp?std::uint32_t(detail::entity_kind::camp):0;
        switch(action.kind){
        case executable_policy::ActionKind::basic:
            result.sdk_code=detail::sdk_attack(entity,action.target_id);result.applied=!result.sdk_code;break;
        case executable_policy::ActionKind::move:
            result.sdk_code=detail::sdk_move_to(action.point.x,action.point.y,&accepted);
            result.applied=!result.sdk_code&&accepted;result.refused=!result.sdk_code&&!accepted;break;
        case executable_policy::ActionKind::cast:{const bool ground=action.target_kind==executable_policy::TargetKind::ground;
            result.sdk_code=detail::sdk_cast(action.slot,entity,entity?action.target_id:0,
                ground?action.point.x:-1,ground?action.point.y:-1,&accepted);
            result.applied=!result.sdk_code&&accepted;result.refused=!result.sdk_code&&!accepted;break;}
        case executable_policy::ActionKind::observe:
            result.sdk_code=detail::sdk_place_ward(action.point.x,action.point.y,&accepted);
            result.applied=!result.sdk_code&&accepted;result.refused=!result.sdk_code&&!accepted;break;
        case executable_policy::ActionKind::recall:result.sdk_code=detail::sdk_recall();result.applied=!result.sdk_code;break;
        case executable_policy::ActionKind::bank:
            result.sdk_code=detail::sdk_submit_gems(action.point.x,action.point.y);result.applied=!result.sdk_code;break;
        case executable_policy::ActionKind::wait:result.attempted=false;break;
        }
        return result;
    }
    bool feedback(const ExecutableAction& action,const ExecutableAttempt& attempted,ExecutableSnapshot& current,ExecutableReceipt& receipt){
        return runtime->executable_observe_feedback(action,attempted,current,receipt,retained_tail,budget->feedback_compute);
    }
    bool close_refusal(const ExecutableAction& action,const ExecutableAttempt& attempted,ExecutableSnapshot& current,ExecutableReceipt& receipt){
        return runtime->executable_observe_close_refusal(action,attempted,current,receipt,retained_tail,budget->refusal_compute);
    }
    bool commit(const ExecutableReceipt& receipt){return runtime->executable_observe_commit(receipt,retained_tail,budget->commit_compute);}
    bool pending_commit_known(const ExecutableReceipt& receipt)const{return runtime->executable_observe_commit_known(receipt);}
    void reconcile_unknown(){runtime->executable_observe_handoff_unknown();}
    bool finite_wait(const ExecutableAction& action,ExecutableSnapshot& current){return runtime->executable_observe_finite_wait(action,current);}
    void service_events(const ExecutableReceipt& receipt,executable_policy::ServiceOwner& owner){
        const auto& prepared=runtime->executable_observe_prepared();
        if(prepared.group_actual&&prepared.group_state==2&&prepared.group_index>=0&&
           executable_policy::Revision(prepared.snapshot.group)==owner.key().job&&owner.stage()==executable_policy::Stage::work){
            executable_policy::ServiceReceipt event{};event.owner=owner.key();event.event=executable_policy::ServiceEvent::clear;
            event.stamp=receipt.after;event.serial=++runtime->executable_native_execution_state().service_serial;
            event.actual=true;event.group=prepared.snapshot.group;event.pose=receipt.after_pose;owner.service(event);
        }
    }
};
ExecutablePlanHandle executable_compile_plan(const executable_policy::Policy& shape,const ExecutableExecutionBudget& budget,
    const ExecutableObservationRequest& request,std::uint64_t retained_tail=0){
    const auto phase_entry=fuel_remaining();
    auto& native=executable_native_execution_state();ExecutableNativeHost host{this,&request,&budget};
    return executable_compile_plan(host,shape,budget,request,native.plans,std::uint64_t(epoch),retained_tail,phase_entry);
}
const ExecutableCompiledPlan* executable_compiled_plan(ExecutablePlanHandle handle)const{
    return executable_native_execution_state().plans.get(handle);
}
bool executable_pin_plan(ExecutablePlanHandle handle){return executable_native_execution_state().plans.pin(handle);}
void executable_release_plan(){executable_native_execution_state().plans.release();}
std::uint64_t executable_plan_required(ExecutablePlanHandle handle)const{
    const auto& native=executable_native_execution_state();const auto* plan=native.plans.get(handle);
    return plan?executable_plan_required(*plan,native.plans.compilation):executable_policy::invalid_cost;
}
ExecutableExecutionOutcome executable_execute_plan(const executable_policy::Policy& policy,ExecutablePlanHandle handle){
    const auto phase_entry=fuel_remaining();
    auto& native=executable_native_execution_state();const auto* plan=native.plans.get(handle);
    if(!plan){ExecutableExecutionOutcome out;out.status=ExecutableStatus::invalid;out.service_key=policy.owner;return out;}
    native.request=plan->request;native.budget=plan->budget;executable_observe_execution_request(native.request);
    ExecutableNativeHost host{this,&native.request,&native.budget};
    const auto result=executable_execute(host,policy,native.budget,native.execution,plan,&native.plans.compilation,phase_entry);
    if(result.attempted||result.finite_wait)native.plans.pin(handle);
    if(result.sdk_code)detail::check(result.sdk_code);return result;
}
std::uint64_t executable_policy_required(const executable_policy::Policy& shape,const ExecutableExecutionBudget& budget,
    const ExecutableObservationRequest& request){
    auto& native=executable_native_execution_state();const auto before=fuel_remaining();++native.required_calls;
    const auto quote_bound=executable_policy_quote_bound(shape);
    if(!budget.declared||before<=executable_add(quote_bound,budget.price.final_tail))
        return executable_policy::invalid_cost;
    ExecutableNativeHost host{this,&request,&budget};
    const auto quote=executable_scalar_quote(host,shape,budget);if(!quote.valid)return executable_policy::invalid_cost;
    const auto admission=executable_shape_admission(shape,budget);
    const auto spent=before>=fuel_remaining()?before-fuel_remaining():0;native.required_spent+=spent;native.required_max=std::max(native.required_max,spent);
    if(spent>quote_bound){++native.required_overruns;return executable_policy::invalid_cost;}
    return executable_add(admission,quote.required);
}
std::uint64_t executable_family_max_tail(const ExecutableExecutionBudget& budget){
    auto& native=executable_native_execution_state();const auto before=fuel_remaining();++native.required_calls;
    if(before<=executable_add(executable_family_quote_bound(),budget.price.final_tail))return executable_policy::invalid_cost;
    ExecutableObservationRequest family;family.member=0;ExecutableNativeHost host{this,&family,&budget};
    const auto tail=executable_family_max_tail(host,budget);const auto spent=before>=fuel_remaining()?before-fuel_remaining():0;
    native.required_spent+=spent;native.required_max=std::max(native.required_max,spent);
    if(spent>executable_family_quote_bound()){++native.required_overruns;return executable_policy::invalid_cost;}return tail;
}
ExecutableExecutionOutcome executable_execute_policy(const executable_policy::Policy& policy,const ExecutableExecutionBudget& budget){
    auto& native=executable_native_execution_state();
    if(!native.selected_request_valid||native.selected_request.owner!=policy.owner){
        ExecutableExecutionOutcome out;out.status=ExecutableStatus::invalid;out.service_key=policy.owner;return out;}
    native.request=native.selected_request;native.budget=budget;
    executable_observe_execution_request(native.request);
    ExecutableNativeHost host{this,&native.request,&native.budget};
    const auto result=executable_execute(host,policy,budget,native.execution);
    // An unexpected SDK error is raised only after bounded actual feedback.
    // The repository's turn boundary logs the required error line and yields.
    if(result.sdk_code)detail::check(result.sdk_code);return result;
}
void executable_execution_bind_request(const ExecutableObservationRequest& selected){
    auto& native=executable_native_execution_state();native.selected_request=selected;native.selected_request_valid=true;
}
ExecutableExecutionOutcome executable_execute_policy(const executable_policy::Policy& policy,const ExecutableExecutionBudget& budget,
    const ExecutableObservationRequest& selected){
    executable_execution_bind_request(selected);return executable_execute_policy(policy,budget);
}
ExecutableExecutionOutcome executable_reconcile_pending_feedback(const ExecutableExecutionBudget& budget){
    auto& native=executable_native_execution_state();native.request=executable_observe_state().request;native.budget=budget;
    executable_observe_execution_request(native.request);ExecutableNativeHost host{this,&native.request,&native.budget};
    return executable_reconcile_pending(host,budget,native.execution);
}
bool executable_execution_service(const executable_policy::ServiceReceipt& receipt){return executable_execution_state().service.service(receipt);}
#endif
