// Runtime fragment: finite Work coverage uses a warm camp-scope route and real
// HP. Shield is deliberately excluded: no future owner-START expiry support is
// imported here. Numeric rank/cost is never SDK command authorization.
std::uint64_t native_group_free_potential(native_service::Point point,int group)const {
    auto amount=native_potential(point);if(group<0||std::size_t(group)>=memory.size())return amount;
    const auto& memo=memory[std::size_t(group)];
    for(auto i=memo.begin;i<memo.end;++i){const auto& member=members[i];
        if(native_service::distance(point,{member.pos.x,member.pos.y})>member.range)continue;
        const auto hit=std::uint64_t(std::ceil(std::max(0.0f,member.base_strength)));
        amount=hit>amount?0:amount-hit;
    }return amount;
}
bool native_current_quiet(const native_service::SelfFact& me)const {
    if(native_potential(me.pose)==0)return true;
    const auto& group=native_facts().group;
    // Current actual Reward proves every selected member dead. This is a
    // present-epoch camp source fact, never remembered absence or a discounted
    // multi-input exit. The actual exit witness always keeps the full union.
    return group.capturable(native_facts().epoch)&&group.ordinal>=0&&std::size_t(group.ordinal)<groups.size()&&
        group.id==std::uint32_t(handle_camp_group_t(groups[std::size_t(group.ordinal)]))&&
        native_group_free_potential(me.pose,group.ordinal)==0;
}
void native_cycle_actual(const native_service::SelfFact& me){
    using namespace native_service;auto& s=native_service_state();
    const auto update=s.cycle.observe_actual(me,{me.epoch,me.pose,true,native_current_quiet(me)},
        {me.epoch,me.pose,true,native_at_home(me.pose)});
    if(update==CycleUpdate::restored)++s.restorations;
}
const native_service::ExitWitness& native_exit_witness(native_service::Epoch epoch_value,native_service::Point origin){
    using namespace native_service;auto& s=native_service_state();
    const ExitWitnessTag tag{epoch_value,origin,s.fields.generation(ServiceField::egress),native_egress_key()};
    auto& witness=s.exit_witness;
    if(witness.tag.origin==tag.origin&&(witness.tag.field_generation==tag.field_generation||s.exit_witness_retained)&&
       witness.tag.semantic_intent==tag.semantic_intent&&witness.prices_complete()){
        // Only the immutable static-source/field proof is reused. The caller
        // supplies a new actual/candidate input epoch and SDK-checks its edge.
        witness.tag.epoch=epoch_value;return witness;
    }
    s.exit_witness_retained=false;witness=build_exit_witness(s.fields.navigation(ServiceField::egress),tag,
        [&](Point point){const auto amount=native_potential(point);
            return RawCampPotential{amount,amount!=std::numeric_limits<std::uint64_t>::max()};});
    return witness;
}
void native_advance_exit_witness(const native_service::Receipt& receipt){
    using namespace native_service;auto& s=native_service_state();auto& witness=s.exit_witness;
    if(!witness.prices_complete())return;
    if(witness.tag.origin==receipt.action.point){witness.tag.epoch=receipt.after_epoch;return;}
    if(witness.tag.origin!=receipt.before_pose||!witness.steps||witness.path[0]!=receipt.action.point){
        witness={};s.exit_witness_retained=false;return;
    }
    for(unsigned i=1;i<witness.steps;++i)witness.path[i-1]=witness.path[i];--witness.steps;
    witness.tag.origin=receipt.action.point;witness.tag.epoch=receipt.after_epoch;
    witness.melee_phases=witness.max_origin_phase=0;witness.saturated=false;
    auto before=native_potential(witness.tag.origin);
    for(unsigned i=0;i<witness.steps;++i){
        witness.max_origin_phase=std::max(witness.max_origin_phase,before);before=native_potential(witness.path[i]);
        witness.melee_phases=exit_witness_detail::add(witness.melee_phases,before,witness.saturated);
    }
    witness.ranged_phases=exit_witness_detail::add(native_potential(witness.tag.origin),witness.melee_phases,witness.saturated);
    witness.melee_with_refusal=exit_witness_detail::add(witness.melee_phases,witness.max_origin_phase,witness.saturated);
    witness.ranged_with_refusal=exit_witness_detail::add(witness.ranged_phases,witness.max_origin_phase,witness.saturated);
    witness.status=witness.steps?ExitWitnessStatus::path:ExitWitnessStatus::quiet;s.exit_witness_retained=true;
}
bool native_egress_candidate_funded(native_service::Budget& budget,const native_service::SelfFact& me,
                                    native_service::Point to,native_service::ExitWitness& successor,bool& funding_denied){
    using namespace native_service;const auto& s=native_service_state();successor={};funding_denied=false;
    if(!me.can_move(native_facts().epoch)||distance(me.pose,to)!=1)return false;
    if(!budget.checkpoint()){funding_denied=true;return false;}
    const auto generation=s.fields.generation(ServiceField::egress),semantic=native_egress_key();
    const auto& incumbent=s.exit_witness;
    const bool cached=incumbent.prices_complete()&&incumbent.tag.origin==me.pose&&
        incumbent.tag.semantic_intent==semantic&&(incumbent.tag.field_generation==generation||s.exit_witness_retained)&&
        incumbent.next_point()==std::optional<Point>{to};
    const auto here=native_potential(me.pose),landing=native_potential(to);
    if(here==std::numeric_limits<std::uint64_t>::max()||landing==std::numeric_limits<std::uint64_t>::max())return false;
    if(cached){
        successor=incumbent;successor.tag.epoch=me.epoch;successor.tag.origin=to;
        for(unsigned i=1;i<successor.steps;++i)successor.path[i-1]=successor.path[i];
        --successor.steps;
        if(landing>successor.melee_phases){successor={};return false;}
        successor.melee_phases-=landing;
        // Retaining the earlier maximum before-move phase is a conservative
        // suffix bound. It avoids rescanning the cached path before dispatch.
        successor.ranged_phases=exit_witness_detail::add(landing,successor.melee_phases,successor.saturated);
        successor.melee_with_refusal=exit_witness_detail::add(successor.melee_phases,successor.max_origin_phase,successor.saturated);
        successor.ranged_with_refusal=exit_witness_detail::add(successor.ranged_phases,successor.max_origin_phase,successor.saturated);
        successor.status=successor.steps?ExitWitnessStatus::path:ExitWitnessStatus::quiet;
    }else{
        // Acquisition is local and leaves the incumbent untouched. A current
        // per-origin field suffices even while its other origins are Preparing.
        if(!s.fields.active(ServiceField::egress)||s.fields.semantic_intent(ServiceField::egress)!=semantic||
           !s.fields.available(ServiceField::egress,to))return false;
        if(budget.pure_used()+6000>budget.pure_bound){funding_denied=true;return false;}
        successor=build_exit_witness(s.fields.navigation(ServiceField::egress),{me.epoch,to,generation,semantic},
            [&](Point point){const auto amount=native_potential(point);
                return RawCampPotential{amount,amount!=std::numeric_limits<std::uint64_t>::max()};});
        if(!budget.checkpoint()){successor={};funding_denied=true;return false;}
    }
    if(!successor.prices_complete())return false;
    bool saturated=false;
    const auto phases=exit_witness_detail::add(successor.ranged_phases,
        std::max(here,successor.max_origin_phase),saturated);
    if(saturated||double(me.hp)<=double(phases))return false;
    const auto tail=word_tail(single_primitive({PrimitiveKind::move,0,to,0}),0,budget,0,true);
    if(!budget.admit(Budget::add(budget.held_tail,tail))){funding_denied=true;return false;}
    return true;
}
bool native_ingress_funded(native_service::Budget& budget,const native_service::SelfFact& me,native_service::Point to){
    if(native_potential(to)==0)return true;
    const auto& group=native_facts().group;
    // Only this freshly checked immediate ingress uses Reward absence. In the
    // next round respawns run BEFORE captures, so a group currently in Reward
    // cannot have respawned for that next camp phase. Next input rechecks Life;
    // no discount is propagated into the multi-input exit witness below.
    if(group.capturable(native_facts().epoch)&&group.ordinal>=0&&std::size_t(group.ordinal)<groups.size()&&
       group.id==std::uint32_t(handle_camp_group_t(groups[std::size_t(group.ordinal)]))&&
       native_group_free_potential(to,group.ordinal)==0)return true;
    if(!budget.checkpoint()||budget.pure_used()+6000>budget.pure_bound)return false;
    const auto& witness=native_exit_witness(me.epoch,to);
    // Incoming phase at the destination plus the complete descending exit,
    // including one explicit refusal-phase allowance. No shield continuation.
    return witness.prices_complete()&&double(me.hp)>double(witness.ranged_with_refusal);
}
bool native_work_funded(native_service::Budget& budget,const native_service::SelfFact& me){
    if(native_current_quiet(me))return true;
    if(!budget.checkpoint()||budget.pure_used()+6000>budget.pure_bound)return false;
    const auto& witness=native_exit_witness(me.epoch,me.pose);
    // A continuing Work quantum holds this pose for the next camp phase.
    // Both actors then retain a same-input exit on their next activation; a
    // ranged Basic cannot spend M again in the current activation.
    return witness.prices_complete()&&double(me.hp)>double(witness.ranged_with_refusal);
}
bool native_cover_work(native_service::Budget& budget,const native_service::SelfFact& me,bool actual_rank_decreased){
    using namespace native_service;auto& s=native_service_state();
    if(!s.egress.outstanding())return true;
    if(native_current_quiet(me))return native_programme_close_egress(me);
    if(!actual_rank_decreased||!native_work_funded(budget,me)||!s.route.active())return false;
    s.batch_next_input_exit=me.ranged&&!me.raw_m&&!me.move_restricted;
    if(!me.can_move(me.epoch)&&!s.batch_next_input_exit)return false;
    const auto& witness=s.exit_witness;const auto next=witness.next_point();if(!next)return false;
    const auto word=single_primitive({PrimitiveKind::move,0,*next,0});
    const auto tail=word_tail(word,0,budget,0,true),outer=budget.held_tail;
    if(!budget.admit(Budget::add(outer,tail)))return false;
    budget.reserve(Budget::add(outer,tail));
    // The receiver is the persistent finite camp-service operator. Its next
    // activation must revalidate rank/HP/exit, or execute this retained exit.
    // For ranged work, raw M is already spent: its explicitly priced origin
    // phase precedes next-input fresh M authorization, never a same-input Move;
    // reaching quiet still owes actual restoration. No future fuel is quoted.
    s.batch_owner=s.route.goal().token;const auto& latest=s.egress.latest();
    const auto result=s.egress.transfer({me.epoch,me.pose,latest.serial,latest.accepted_prefix,s.batch_owner,true,true},
        {me.epoch,me.pose,me.actual(native_facts().epoch)});
    budget.reserve(outer);if(result==EgressUpdate::transferred){++s.coverage_transfers;return true;}return false;
}
bool native_capture_funded(native_service::Budget& budget,const native_service::SelfFact& me,
                           const native_service::GroupFact& group,const native_service::CaptureFact& capture){
    using namespace native_service;auto& s=native_service_state();
    if(!group.capturable(me.epoch)||group.ordinal<0||std::size_t(group.ordinal)>=groups.size()||
       group.id!=std::uint32_t(handle_camp_group_t(groups[std::size_t(group.ordinal)])))return false;
    if(capture.epoch!=me.epoch||capture.group_id!=group.id||!capture.life_actual||capture.life!=GroupLife::reward)return false;
    if(capture.on_spawn&&!capture.on_spawn_actual)return false;
    const bool holding=capture.holding_reward(me.epoch)&&capture.group_id==group.id;
    if(holding&&(s.capture_group!=group.ordinal||s.capture_deadline<0)){
        // Presence first begun after R's capture phase can only ARM at R+1.
        // capture_time later phases pay at R+capture_time+1; that camp batch
        // still precedes owner START. Cover all six future batches when arming
        // is unknown. Actual earlier payout ends the episode; continued Reward
        // or contest never renews this deadline.
        s.capture_group=group.ordinal;s.capture_deadline=epoch+std::max(1,int(constants::capture_time))+1;
    }
    if(holding&&epoch>=s.capture_deadline){s.capture_reobserve=true;return false;}
    const auto other=native_group_free_potential(me.pose,group.ordinal);
    if(other==0&&!s.egress.outstanding())return true;
    if(!me.can_move(me.epoch)&&!(me.ranged&&!me.raw_m&&!me.move_restricted))return false;
    if(!budget.checkpoint()||budget.pure_used()+6000>budget.pure_bound)return false;
    const auto& witness=native_exit_witness(me.epoch,me.pose);
    if(!witness.prices_complete())return false;
    const auto phases=holding?std::uint64_t(s.capture_deadline-epoch):0;
    const auto exit=me.can_move(me.epoch)?witness.melee_with_refusal:witness.ranged_with_refusal;
    const auto cost=Budget::add(exit,Budget::mul(phases,other));
    // Selected Reward absence is used only while this actual actor holds a
    // selected spawn: its maintained occupancy prevents group respawn. Other
    // groups retain their FULL union. Exit after leaving uses the FULL union,
    // and a contested/unfinished capture cannot renew this finite deadline.
    if(double(me.hp)<=double(cost)||!s.route.active())return false;
    const auto next=witness.next_point();if(!next)return false;
    const auto tail=word_tail(single_primitive({PrimitiveKind::move,0,*next,0}),0,budget,0,true);
    const auto outer=budget.held_tail;if(!budget.admit(Budget::add(outer,tail)))return false;
    if(!s.egress.outstanding())return true;
    budget.reserve(Budget::add(outer,tail));const auto& latest=s.egress.latest();
    const auto update=s.egress.transfer({me.epoch,me.pose,latest.serial,latest.accepted_prefix,s.route.goal().token,true,true},
        {me.epoch,me.pose,me.actual(me.epoch)});budget.reserve(outer);
    if(update==EgressUpdate::transferred){++s.coverage_transfers;return true;}return false;
}
