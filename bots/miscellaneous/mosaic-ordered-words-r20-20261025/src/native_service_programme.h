// Included inside Runtime. Every route terminates in a fresh service operator;
// movement is never evidence that Work, capture, tribute or egress completed.
#include "native_service_arrival_runtime.h"

native_service::RouteFrame native_programme_frame(const native_service::SelfFact& me)const {
    using namespace native_service;const auto& s=native_service_state();RouteFrame frame{};
    frame.goal=s.route.goal();frame.epoch=me.epoch;frame.pose=me.pose;frame.field_generation=s.field_revision;
    frame.field_matches=s.board_ready&&s.nav_intent&&s.nav_intent==s.goal_key&&!s.board_edit_count&&
        (s.active_field==ServiceField::egress||native_service_field_matches());
    if(frame.field_matches){frame.status=native_navigation().status(me.pose);frame.distance=native_navigation().distance(me.pose).value_or(-1);
        // A discovered non-root BFS cell has a predecessor in the immutable
        // board. The actual bounded nominees are acquired once by transport.
        frame.nominees_available=frame.status==NavigationStatus::ready&&frame.distance>0;}
    for(unsigned i=0;i<s.root_count;++i)frame.geometric_arrival|=s.roots[i]==me.pose;
    return frame;
}
bool native_programme_bind(native_service::Budget& budget,native_service::RoutePurpose purpose,
                           const native_service::SelfFact& me,native_service::Point target) {
    using namespace native_service;auto& s=native_service_state();
    const NativeId group=s.group>=0?std::uint32_t(handle_camp_group_t(groups[std::size_t(s.group)])):0;
    const bool member_bound=purpose!=RoutePurpose::work;
    const NativeId member=member_bound&&s.member>=0?std::uint32_t(handle_camp_t(members[std::size_t(s.member)].handle)):0;
    if(!member_bound&&s.group>=0){const auto at=members[memory[std::size_t(s.group)].begin].pos;target={at.x,at.y};}
    const auto old=s.route.goal();
    if(s.route.active()&&old.purpose==purpose&&old.group==group&&old.member==member&&
       old.target==target&&old.footprint==s.goal_key&&old.member_bound==member_bound){s.route.begin(me);s.route.observe(me,native_programme_frame(me));return true;}
    // A replacement has its own non-reused identity and a paid acquisition /
    // dispatch branch. It cannot transfer an unresolved physical receipt.
    if(s.route.feedback_pending()||!budget.admit(budget.query_bound))return false;
    const RouteGoal goal{++s.route_tokens,s.goal_key,purpose,group,member,target,member_bound};
    bool bound=false;
    if(s.route.active()){
        const auto outer=budget.held_tail;budget.reserve(Budget::add(outer,budget.query_bound));
        bound=s.route.replace(goal,{goal.token,++s.funding_serial,me.epoch,RouteNeed::arrival,true,true,old.token});
        budget.reserve(outer);
    }else bound=s.route.bind(goal)==RouteBinding::bound;
    if(bound){s.route.begin(me);s.route.observe(me,native_programme_frame(me));s.route.observe_custody(me);}
    return bound;
}
void native_programme_bank_goal(native_service::Point altar,int ordinal){
    auto& s=native_service_state();s.group_goal=false;s.active_field=native_service::ServiceField::service;
    s.bank_altar=ordinal;const auto key=(std::uint64_t(3)<<56)^s.intent^
        (std::uint64_t(unsigned(ordinal+1))<<24);
    // Semantic ownership survives board/lifecycle retirement, while its root
    // materialization does not. Empty is a completed materialization too.
    if(s.goal_key!=key||!s.bank_roots_materialized){
        s.nav_intent=0;s.root_count=0;s.goal_key=key;
        native_region(altar,int(constants::submit_gems_range));s.bank_roots_materialized=true;
    }
}
void native_programme_work_goal(const native_service::SelfFact& me,native_service::Point){
    auto& s=native_service_state();s.group_goal=true;s.active_field=native_service::ServiceField::service;
    const auto key=(std::uint64_t(1)<<63)^s.group_generation^
        (std::uint64_t(std::uint32_t(s.group+1))<<24)^(std::uint64_t(unsigned(me.attack_range))<<48);
    if(s.goal_key!=key){s.goal_key=key;s.root_count=0;s.excluded_count=0;s.nav_intent=0;}
    s.approach_range=std::max(1,me.attack_range);s.approach_prepared=false;
    if(s.group>=0){const auto at=members[memory[std::size_t(s.group)].begin].pos;s.approach_goal={at.x,at.y};}
    if(native_service_field_matches()){s.nav_intent=key;s.field_revision=s.fields.generation(s.active_field);}
}
void native_programme_job(int group,int member){
    auto& s=native_service_state();if(group<0||member<0||std::size_t(group)>=groups.size()||std::size_t(member)>=members.size())return;
    const bool new_group=s.group!=group;
    if(new_group){++s.group_generation;s.frontier.retire();s.nav_intent=0;}
    if(new_group||s.member!=member){s.group=group;s.member=member;++s.intent;}
    native_service::ServiceJob job{};job.group_ordinal=group;job.member_ordinal=member;
    job.group_id=std::uint32_t(handle_camp_group_t(groups[std::size_t(group)]));
    job.member_id=std::uint32_t(handle_camp_t(members[std::size_t(member)].handle));job.retained=true;
    native_service::retain_job(s.service,job);native_group_configure();
}
void native_programme_publish(const native_service::ArrivalResult& arrival,const native_service::SelfFact& me){
    using namespace native_service;auto& s=native_service_state();
    s.arrival_work_cursor=arrival.next_work_cursor;s.arrival_bank_cursor=arrival.next_bank_cursor;
    for(unsigned i=0;i<arrival.camp_count;++i){const auto& camp=arrival.camps[i];
        s.cycle.observe_member(camp,native_facts().epoch);
        GroupFact same_group{};for(unsigned g=0;g<arrival.group_count;++g)
            if(arrival.groups[g].ordinal==camp.group_ordinal)same_group=arrival.groups[g];
        native_group_observe(camp,same_group);
        if(camp.actual(native_facts().epoch))native_note_camp_body(camp.pose,camp.life!=Life::dead,camp.epoch);}
    for(unsigned i=0;i<arrival.group_count;++i){const auto& group=arrival.groups[i];
        s.cycle.observe_lifecycle(group,{},native_facts().epoch);
        native_note_group_lifecycle(group);
        if(group.current(native_facts().epoch)&&group.life==GroupLife::respawning&&group.ordinal>=0&&
           std::size_t(group.ordinal)<s.unavailable_until.size()){
            if(s.capture_group==group.ordinal){s.capture_group=s.capture_deadline=-1;s.capture_reobserve=false;}
            if(group.respawn_actual)s.unavailable_until[std::size_t(group.ordinal)]=epoch+std::max(1,group.respawn_time);
            if(group.ordinal==s.group){
                if(s.route.active()&&s.route.goal().purpose==RoutePurpose::capture)
                    s.route.service_actual({s.route.goal().token,++s.service_event,me.epoch,RouteServiceKind::capture,true,true});
                s.group=s.member=-1;s.nav_intent=0;
                s.frontier.retire();
            }
        }
    }
}
bool native_programme_close_egress(const native_service::SelfFact& me){
    using namespace native_service;auto& s=native_service_state();if(!s.egress.outstanding())return true;
    const bool home=native_at_home(me.pose),quiet=native_current_quiet(me);
    if(!home&&!quiet)return false;
    const auto& latest=s.egress.latest();
    const auto result=s.egress.close({me.epoch,me.pose,latest.serial,latest.accepted_prefix,
        home?EgressProducer::home:EgressProducer::complete_potential_zero,true},{me.epoch,me.pose,me.actual(native_facts().epoch)});
    if(result!=EgressUpdate::closed)return false;++s.egress_closures;
    if(s.route.active()&&(s.route.goal().purpose==RoutePurpose::exit||s.route.goal().purpose==RoutePurpose::recover))
        s.route.service_actual({s.route.goal().token,++s.service_event,me.epoch,RouteServiceKind::egress_closed,true,true});
    return true;
}
bool native_programme_transport(native_service::Budget& budget,const native_service::SelfFact& me,bool egress=false){
    using namespace native_service;auto& s=native_service_state();s.move_count=0;const Point origin=me.pose;
    if(!me.can_move(native_facts().epoch))return false;
    s.excluded_count=s.route.exclusions(me.pose,s.excluded);
    if(egress){
        const auto omitted=[&](Point point){
            for(unsigned i=0;i<s.excluded_count;++i)if(s.excluded[i]==point)return true;
            return false;
        };
        const auto append=[&](Point point){
            if(omitted(point)||s.move_count==s.moves.size())return;
            for(unsigned i=0;i<s.move_count;++i)if(s.moves[i]==point)return;
            s.moves[s.move_count++]=point;
        };
        // Nomination reads an already retained SMALL incumbent. Building a
        // complete alternative belongs to its separately admitted class below.
        const auto& witness=s.exit_witness;
        if(witness.prices_complete()&&witness.tag.origin==me.pose&&
           witness.tag.semantic_intent==native_egress_key()&&
           (s.exit_witness_retained||witness.tag.field_generation==s.fields.generation(ServiceField::egress)))
            if(const auto next=witness.next_point())append(*next);
        if(s.fields.active(ServiceField::egress)){
            std::array<Point,8> nearby{};
            const auto count=s.fields.navigation(ServiceField::egress).nominees(me.pose,
                {s.excluded.data(),s.excluded_count},nearby);
            for(unsigned i=0;i<count;++i)append(nearby[i]);
        }
    }
    if(!egress&&s.board_ready&&s.nav_intent==s.goal_key&&s.nav_intent)
        s.move_count=native_navigation().nominees(me.pose,{s.excluded.data(),s.excluded_count},s.moves);
    // Dated omissions are retained, but an unchanged transient occupant is not
    // an eternal wall. A bounded paid retry is fresh SDK authorization, without
    // treating the old rejection as an actual empty observation.
    if(!egress&&!s.move_count&&s.board_ready&&s.nav_intent==s.goal_key&&s.nav_intent)
        s.move_count=native_navigation().nominees(me.pose,{},s.moves);
    const auto candidates=s.moves;const auto count=s.move_count;
    for(unsigned i=0;i<std::min(3u,count)&&!budget.poisoned;++i){
        ExitWitness successor{};
        if(egress){
            bool funding_denied=false;
            if(!native_egress_candidate_funded(budget,me,candidates[i],successor,funding_denied)){
                if(funding_denied)return false;
                continue;
            }
        }else if(!native_ingress_funded(budget,me,candidates[i]))continue;
        // Each alternative is its own freshly priced class. The executor may
        // not substitute an unpriced ingress/egress edge after a raw FALSE.
        s.moves[0]=candidates[i];s.move_count=1;
        const auto before_serial=s.execution.serial;
        const bool attempted=native_dispatch(budget,single_primitive({PrimitiveKind::move,0,candidates[i],0}));
        if(egress&&s.execution.serial>before_serial){
            const auto& receipt=s.execution.pending_receipt;
            if(receipt.serial==s.execution.serial&&receipt.attempted&&receipt.returned&&receipt.action.kind==PrimitiveKind::move&&
               receipt.action.point==candidates[i]&&receipt.before_pose==origin&&receipt.applied==Truth::yes){
                // The accepted Move itself proves its landing even if later
                // feedback is pending. A Changed/false cannot replace the
                // previously priced incumbent with a speculative alternative.
                successor.tag.epoch=receipt.after_epoch;s.exit_witness=successor;s.exit_witness_retained=true;
            }
        }
        if(attempted)return true;
        if(s.execution.pending||s.execution.input_blocked||budget.poisoned)return false;
        if(s.last_status==unsigned(ExecutionStatus::unfunded)||s.last_status==unsigned(ExecutionStatus::overrun))return false;
    }
    return false;
}
bool native_programme_egress(native_service::Budget& budget,const native_service::SelfFact& me){
    using namespace native_service;auto& s=native_service_state();
    if(s.egress.transferred()){
        const auto& latest=s.egress.latest();
        s.egress.reclaim({me.epoch,me.pose,latest.serial,latest.accepted_prefix,s.egress.replacement_owner()},
            {me.epoch,me.pose,me.actual(native_facts().epoch)});
    }
    if(native_programme_close_egress(me)&&!s.exit_due&&!s.recovering)return false;
    if(native_current_quiet(me)){s.exit_due=false;native_cycle_actual(me);
        if(s.cycle.restoration_barrier())s.recovering=true;
        else if(me.hp>=me.max_hp&&!s.capture_reobserve)s.recovering=false;
        if(s.recovering){native_goal(3,me);native_programme_bind(budget,RoutePurpose::recover,me,me.pose);
            native_dispatch(budget,single_primitive({PrimitiveKind::recall,0,me.pose,0}));return true;}
        return false;
    }
    native_goal(4,me);s.stage=20;
    if(native_programme_bind(budget,s.recovering?RoutePurpose::recover:RoutePurpose::exit,me,s.exit_anchor))
        native_programme_transport(budget,me,true);
    return true;
}
bool native_programme_arrival(native_service::Budget& budget,native_service::SelfFact& me,
                             const native_service::ArrivalResult& result){
    using namespace native_service;auto& s=native_service_state();const auto& decision=result.decision;
    if(decision.kind==ArrivalKind::bank){
        native_programme_bank_goal(decision.bank.altar,decision.bank.ordinal);
        set_macro_intent(s.service,ServicePhase::bank);s.stage=9;
        if(native_programme_bind(budget,RoutePurpose::bank,me,decision.bank.altar))native_dispatch(budget,decision.word());
        return true;
    }
    if(decision.kind==ArrivalKind::work&&decision.physical()){
        if(!native_work_funded(budget,me)){
            s.exit_due=true;s.recovering=me.hp<me.max_hp||s.cycle.restore_required();
            native_programme_egress(budget,me);return true;
        }
        native_programme_job(decision.group_ordinal(),decision.member_ordinal());
        native_programme_work_goal(me,decision.camp.pose);
        if(!native_programme_bind(budget,RoutePurpose::work,me,decision.camp.pose))return true;
        native_group_observe(decision.camp);
        const auto outer=budget.held_tail;
        const auto post=Budget::add(Budget::mul(5,budget.query_bound),native_current_quiet(me)?0:
            word_tail(single_primitive({PrimitiveKind::move,0,me.pose,0}),0,budget,0,true));
        const auto word_cost=word_tail(decision.word(),0,budget,0,true);
        if(!budget.admit(Budget::add(outer,Budget::add(post,word_cost))))return true;
        budget.reserve(Budget::add(outer,post));
        const auto owner=s.cycle.retained_job();
        if(owner.valid()){
            const auto bound=s.cycle.bind_member(decision.camp,me.epoch,
                {++s.member_binding_serial,s.cycle.group_generation(),me.epoch,true,true});
            if(bound!=CycleUpdate::bound&&bound!=CycleUpdate::unchanged){budget.reserve(outer);return true;}
        }
        native_facts().camp=decision.camp;observe_service(s.service,me,{},decision.camp);
        s.stage=17;const bool attempted=native_dispatch(budget,decision.word());budget.reserve(outer);
        if(!attempted)return true;
        if(s.execution.pending||s.execution.input_blocked||budget.poisoned)return true;
        me=native_facts().self;
        native_cycle_actual(me);
        CampFact after{};
        if(!native_read_camp(budget,s.member,after)){native_programme_egress(budget,me);return true;}
        native_facts().camp=after;s.cycle.observe_member(after,native_facts().epoch);
        native_group_observe(after);
        if(after.actual(native_facts().epoch))native_note_camp_body(after.pose,after.life!=Life::dead,after.epoch);
        const auto revision=s.group_work.route_revision();
        const auto effect=s.group_work.observe_effect({s.group_generation,s.cycle.latest_work(),native_facts().epoch,
            native_runtime_commit_known(s.execution.pending_receipt.serial),decision.camp,after,{}});
        if(revision!=s.group_work.route_revision())native_group_routes_changed();
        const bool decreased=effect.progress();
        if(decreased)++s.rank_decreases;
        if(after.actual(native_facts().epoch)&&after.life==Life::dead){
            GroupFact group{};
            if(native_read_group(budget,s.group,group,s.member)){
                native_facts().group=group;native_note_group_lifecycle(group);s.cycle.observe_lifecycle(group,{},native_facts().epoch);
                native_group_observe(after,group);
                if(group.capturable(native_facts().epoch)){
                    CaptureFact capture{};native_capture_at(me,group,s.member,capture);
                    ArrivalResult complete{};complete.decision=arrival_work(me,after,group,capture,native_facts().epoch);
                    native_programme_close_egress(me);native_cycle_actual(me);
                    if(s.cycle.restoration_barrier()){s.recovering=true;native_programme_egress(budget,me);return true;}
                    if(native_programme_arrival(budget,me,complete))return true;
                }
            }
        }
        if(native_cover_work(budget,me,decreased))return true;
        s.recovering=me.hp<me.max_hp||s.cycle.restore_required();s.exit_due=true;
        native_programme_egress(budget,me);return true;
    }
    if(decision.kind==ArrivalKind::capture&&decision.group.current(native_facts().epoch)&&
       decision.group.life==GroupLife::reward){
        native_programme_job(decision.group_ordinal(),decision.member_ordinal());
        if(s.route.active()&&s.route.goal().purpose==RoutePurpose::work&&s.route.goal().group==decision.group.id)
            s.route.service_actual({s.route.goal().token,++s.service_event,me.epoch,RouteServiceKind::work_complete,true,true});
        native_facts().group=decision.group;native_facts().capture=decision.capture;
        s.cycle.observe_lifecycle(decision.group,decision.capture,native_facts().epoch);native_cycle_actual(me);
        native_programme_close_egress(me);
        if(s.cycle.restoration_barrier()){s.recovering=true;native_programme_egress(budget,me);return true;}
        observe_service(s.service,me,decision.group,decision.camp,{},decision.capture);
        native_goal(1,me);s.stage=14;
        if(!native_programme_bind(budget,RoutePurpose::capture,me,decision.camp.pose))return true;
        if(!native_capture_funded(budget,me,decision.group,decision.capture)){
            s.recovering=s.exit_due=true;native_programme_egress(budget,me);return true;
        }
        if(!decision.capture.holding_reward(native_facts().epoch)&&!native_programme_transport(budget,me)&&!native_current_quiet(me)){
            s.recovering=s.exit_due=true;native_programme_egress(budget,me);
        }
        return true;
    }
    return false;
}
void native_service_body(native_service::Budget& budget){
    using namespace native_service;auto& s=native_service_state();native_begin_input();s.execution.begin(epoch);
    if(s.input_round!=epoch){s.input_round=epoch;s.stage=0;s.move_reason=0;}
    if(s.execution.pending){s.stage=1;native_reconcile_feedback(budget,s.execution);return;}
    SelfFact me{};if(!native_read_self(budget,me,true,true)){s.stage=2;return;}native_facts().self=me;++s.ready_inputs;
    if(me.alive)native_note_camp_body(me.pose,false);
    native_group_clock(me.epoch);
    native_cycle_actual(me);
    s.route.begin(me);s.route.observe_custody(me);observe_service(s.service,me);
    s.last_gems=me.gems;
    if(!me.alive){s.stage=3;s.recovering=true;s.exit_due=false;native_shop_step(budget,me);return;}
    if(me.teleporting||me.stunned||me.completed){s.stage=4;return;}
    if(me.recalling||me.submitting){s.stage=5;return;}
    native_programme_close_egress(me);
    if(native_at_home(me.pose)){
        if(me.gold){const auto shopping=native_shop_step(budget,me);if(shopping.attempted||budget.poisoned){s.stage=7;return;}}
        if(me.hp<me.max_hp){s.stage=8;s.recovering=true;return;}
        if(s.route.active()&&s.route.goal().purpose==RoutePurpose::recover)
            s.route.service_actual({s.route.goal().token,++s.service_event,me.epoch,RouteServiceKind::recovered,true,true});
        s.recovering=s.exit_due=false;
        s.capture_reobserve=false;s.capture_group=s.capture_deadline=-1;
    }
    if(s.cycle.restoration_barrier())s.recovering=true;
    if(s.recovering||s.exit_due||s.egress.direct_required()){
        s.stage=10;set_macro_intent(s.service,s.recovering?ServicePhase::recover:ServicePhase::exit);
        if(native_programme_egress(budget,me))return;
    }
    ArrivalRequest request{};request.purpose=me.gems>0?ArrivalPurpose::bank:ArrivalPurpose::work;
    request.retained_group=s.group;request.retained_member=native_arrival_member_needed(s.member,me.epoch)?s.member:-1;
    request.blocker_member=s.blocker_member;
    const auto retained=s.cycle.retained_job();request.preserve_job=retained.valid();
    if(retained.valid()){
        if(s.group!=retained.group_ordinal)native_programme_job(retained.group_ordinal,retained.member_ordinal);
        request.retained_group=s.group;request.retained_member=native_arrival_member_needed(s.member,me.epoch)?s.member:-1;
        request.preserve_member=false;
    }
    request.preferred_altar=s.bank_altar;request.work_cursor=s.arrival_work_cursor;request.bank_cursor=s.arrival_bank_cursor;
    request.custody_required=me.gems>0;
    // Reserve an executable Basic/tribute incumbent while acquiring optional
    // alternatives. Denial is a named paid observation continuation.
    const auto outer=budget.held_tail;
    const auto incumbent=word_tail(single_primitive({me.gems>0?PrimitiveKind::bank:PrimitiveKind::basic,0,me.pose,0}),0,budget,0,true);
    budget.reserve(Budget::add(outer,incumbent));
    const auto arrival=native_resolve_arrival(budget,me,request);budget.reserve(outer);
    native_programme_publish(arrival,me);if(budget.poisoned)return;
    if(native_programme_arrival(budget,me,arrival))return;
    if(me.gems>0){
        s.group_goal=false;s.active_field=ServiceField::service;
        s.stage=9;set_macro_intent(s.service,ServicePhase::bank);
        // Keep the actual altar episode until an actual arrival chooses a
        // different eligible service. Its index participates in the footprint.
        if(s.bank_altar<0||std::size_t(s.bank_altar)>=altars.size())native_goal(2,me);
        else{const auto at=altars[std::size_t(s.bank_altar)];native_programme_bank_goal({at.x,at.y},s.bank_altar);}
        if(s.bank_altar>=0){const auto at=altars[std::size_t(s.bank_altar)];
            if(native_programme_bind(budget,RoutePurpose::bank,me,{at.x,at.y})&&!native_programme_transport(budget,me)&&!native_current_quiet(me)){
                s.recovering=s.exit_due=true;native_programme_egress(budget,me);
            }}
        return;
    }
    if(s.group<0&&native_choose_group(me)<0){s.stage=11;return;}
    if(s.member<0||std::size_t(s.member)>=members.size())native_programme_job(s.group,int(memory[std::size_t(s.group)].begin));
    if(s.group_work.all_members_excluded(me.epoch)){
        GroupFact group{};bool acquired=false;
        for(unsigned i=0;i<arrival.group_count;++i)if(arrival.groups[i].ordinal==s.group&&
            same_world(arrival.groups[i].epoch,native_facts().epoch)){group=arrival.groups[i];acquired=true;break;}
        if(!acquired&&!native_read_group(budget,s.group,group,s.member)){s.stage=12;return;}
        native_facts().group=group;ArrivalResult lifecycle{};lifecycle.groups[0]=group;lifecycle.group_count=1;
        native_programme_publish(lifecycle,me);
        if(s.group<0){s.stage=13;return;}
        if(group.capturable(me.epoch)){
            const auto witness=[&]<class Owner>(Owner& owner){
                if constexpr(requires(Owner& value,Point point){value.native_arrival_member_at(point);}){
                    const auto ordinal=owner.native_arrival_member_at(me.pose);
                    if(ordinal>=0&&std::size_t(ordinal)<owner.members.size()&&owner.members[std::size_t(ordinal)].group==s.group)return ordinal;
                }return s.member;
            }(*this);
            // Actual Reward proves every member dead. This creates no unseen
            // living HP, no submitted amount and no capture-completion credit.
            CampFact dead{};dead.epoch=me.epoch;dead.id=std::uint32_t(handle_camp_t(members[std::size_t(witness)].handle));
            dead.member_ordinal=witness;dead.group_ordinal=s.group;dead.group_id=group.id;
            dead.pose={members[std::size_t(witness)].pos.x,members[std::size_t(witness)].pos.y};dead.life=Life::dead;dead.life_actual=true;
            CaptureFact capture{};native_capture_at(me,group,witness,capture);
            lifecycle.decision=arrival_work(me,dead,group,capture,me.epoch);
            if(native_programme_arrival(budget,me,lifecycle))return;
        }
        // Empty possible-work geometry is a lifecycle observation obligation,
        // never an imagined payout or an unpriced capture hold.
        native_goal(1,me);s.stage=12;
        if(native_programme_bind(budget,RoutePurpose::work,me,me.pose)&&!native_programme_transport(budget,me)&&!native_current_quiet(me)){
            s.recovering=s.exit_due=true;native_programme_egress(budget,me);
        }return;
    }
    // Reuse all fresh Arrival facts. Route the whole remaining group instead
    // of rereading a corpse then wrapping it into an exclusive far ordinal.
    const auto at=members[memory[std::size_t(s.group)].begin].pos;
    native_programme_work_goal(me,{at.x,at.y});s.stage=22;
    if(native_programme_bind(budget,RoutePurpose::work,me,{at.x,at.y})&&!native_programme_transport(budget,me)&&
       native_potential(me.pose)>0&&(s.cycle.restore_required()||me.hp<me.max_hp)){
        s.recovering=s.exit_due=true;native_programme_egress(budget,me);
    }
}
