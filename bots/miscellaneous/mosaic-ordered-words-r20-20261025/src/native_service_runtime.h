// Included INSIDE Runtime after native_service_facts.h. All command bindings
// below are direct SDK imports: no wrapper obtains hidden position/team/state.
// Parent callbacks are pure cached geometry, physical-service commit and a
// bounded alternative nomination. They must not perform SDK imports.
struct NativeHost {
    Runtime* owner{};
    bool command_started{};
    native_service::Epoch epoch()const {
        auto result=owner->native_facts().epoch;
        std::uint32_t actual{};detail::check(detail::sdk_bc_round_number(&actual));
        // Round checks are explicit query-control imports charged to the
        // whole input's pure allowance, including before the final command.
        result.round=int(actual);return result;
    }
    bool current()const {return epoch().round==owner->epoch;}
    bool fresh(const native_service::Primitive& action,native_service::Budget& budget,
               native_service::Validation& out) {
        using namespace native_service;
        if(!current()||!budget.checkpoint())return false;
        auto& facts=owner->native_facts();const auto now=epoch();
        if(!facts.self.actual(now)){
            if(!owner->native_read_self(budget,out.self))return false;
            facts.self=out.self;
        }else out.self=facts.self;
        if(target_primitive(action.kind)){
            if(facts.camp.id==action.target_id&&facts.camp.actual(now)&&facts.camp.body_actual&&facts.camp.buffs_actual)out.camp=facts.camp;
            else{
                int ordinal=facts.camp.id==action.target_id?facts.camp.member_ordinal:-1;
                // Camp handles are static and bounded by the retained catalog.
                // This scan is pure caller/control work, never a world query.
                if(ordinal<0||std::size_t(ordinal)>=owner->members.size()||
                   std::uint32_t(handle_camp_t(owner->members[std::size_t(ordinal)].handle))!=action.target_id){
                    ordinal=-1;
                    for(std::size_t i=0;i<owner->members.size();++i)
                        if(std::uint32_t(handle_camp_t(owner->members[i].handle))==action.target_id){ordinal=int(i);break;}
                }
                if(ordinal<0||!owner->native_read_camp(budget,ordinal,out.camp))return false;
                facts.camp=out.camp;
            }
        }
        if(action.kind==PrimitiveKind::reset||action.kind==PrimitiveKind::shield||
           action.kind==PrimitiveKind::cast_unit||action.kind==PrimitiveKind::cast_ground){
            if(!owner->native_read_ability(budget,action.slot,out.ability))return false;
        }
        if(action.kind==PrimitiveKind::move){
            out.move_epoch=now;out.move_actual=true;
            out.move_attempt_permitted=owner->native_move_terrain(out.self.pose,action.point);
            if(!out.move_attempt_permitted){owner->native_move_validation(1,action.point,out.self);return true;}
            // Known selected living bodies are never called an unknown free
            // destination. Otherwise occupancy stays explicitly Unknown and
            // the SDK owns the conditional attempt/refusal.
            if(facts.camp.living(now)&&facts.camp.body_actual&&facts.camp.pose==action.point){
                out.occupancy_actual=true;out.occupancy_open=false;
                owner->native_note_camp_body(action.point,true);
                owner->native_move_validation(2,action.point,out.self);
                return true;
            }
            if(!budget.before_query())return false;
            std::int32_t visible{};const auto vc=detail::sdk_is_visible(action.point.x,action.point.y,&visible);
            const bool visibility_paid=budget.after_query();detail::check(vc);if(!visibility_paid)return false;
            if(visible){
                // Visible occupancy is checked separately for every blocking
                // class. An unseen tile retains conditional TRY semantics.
                std::int32_t occupied{};
                if(!budget.before_query())return false;
                const auto cc=detail::sdk_has_camp(action.point.x,action.point.y,&occupied);
                const bool camp_paid=budget.after_query();detail::check(cc);if(!camp_paid)return false;
                if(occupied){out.occupancy_actual=true;out.occupancy_open=false;owner->native_note_camp_body(action.point,true);owner->native_move_validation(2,action.point,out.self);return true;}
                if(!budget.before_query())return false;
                const auto hc=detail::sdk_has_champion(action.point.x,action.point.y,&occupied);
                const bool champion_paid=budget.after_query();detail::check(hc);if(!champion_paid)return false;
                if(occupied){out.occupancy_actual=true;out.occupancy_open=false;owner->native_move_validation(3,action.point,out.self);return true;}
                if(!budget.before_query())return false;
                const auto tc=detail::sdk_has_teleport(action.point.x,action.point.y,&occupied);
                const bool teleport_paid=budget.after_query();detail::check(tc);if(!teleport_paid)return false;
                out.occupancy_actual=true;out.occupancy_open=occupied==0;
                owner->native_move_validation(occupied?4u:6u,action.point,out.self);
            }else owner->native_move_validation(5,action.point,out.self);
        }
        if(action.kind==PrimitiveKind::bank){
            if(!out.self.gems_actual){
                if(!budget.before_query())return false;
                std::uint32_t bag{};const auto code=detail::sdk_champion_gems(out.self.id,&bag);
                const bool paid=budget.after_query();detail::check(code);if(!paid)return false;
                out.self.gems=int(bag);out.self.gems_actual=true;facts.self=out.self;
            }
            out.bank_range=constants::submit_gems_range;
            out.bank_altar_actual=owner->native_bank_altar(action.point);
            if(!out.bank_altar_actual||out.self.team<0||out.self.team>1)return true;
            if(!budget.before_query())return false;
            detail::altar_lock_info lock{};
            const auto code=detail::sdk_altar_lock(action.point.x,action.point.y,1-out.self.team,&lock);
            const bool paid=budget.after_query();if(!paid)return false;
            if(code==int(error_code::api_not_found))out.bank_lock_actual=out.bank_unlocked=true;
            else if(code==int(error_code::api_not_in_vision))return true;
            else{detail::check(code);out.bank_lock_actual=true;out.bank_unlocked=false;}
        }
        return current()&&out.self.actual(epoch())&&budget.checkpoint();
    }
    bool attempt(const native_service::Primitive& action) {
        using namespace native_service;
        command_started=false;
        if(!current())throw std::runtime_error("error native command crossed owner input");
        std::int32_t applied{};std::int32_t code{};
        command_started=true;
        switch(action.kind){
        case PrimitiveKind::basic:
            detail::check(detail::sdk_attack(std::uint32_t(detail::entity_kind::camp),action.target_id));
            owner->attacked=true;return true;
        case PrimitiveKind::reset:
            code=detail::sdk_cast(action.slot,0,0,-1,-1,&applied);break;
        case PrimitiveKind::shield:
            code=detail::sdk_cast(action.slot,std::uint32_t(detail::entity_kind::champion),owner->native_facts().self.id,-1,-1,&applied);break;
        case PrimitiveKind::cast_unit:
            code=detail::sdk_cast(action.slot,std::uint32_t(detail::entity_kind::camp),action.target_id,-1,-1,&applied);break;
        case PrimitiveKind::cast_ground:
            code=detail::sdk_cast(action.slot,0,0,action.point.x,action.point.y,&applied);break;
        case PrimitiveKind::move:
            code=detail::sdk_move_to(action.point.x,action.point.y,&applied);break;
        case PrimitiveKind::recall:detail::check(detail::sdk_recall());return true;
        case PrimitiveKind::bank:detail::check(detail::sdk_submit_gems(action.point.x,action.point.y));return true;
        }
        detail::check(code);
        if(applied&&action.kind==PrimitiveKind::move)owner->walked=true;
        return applied!=0;
    }
    bool attempt_started()const {return command_started;}
    void invalidate(native_service::EffectScope scope) {owner->native_after_attempt(scope);}
    bool feedback(native_service::Budget& budget,native_service::SelfFact& out) {
        if(!current()||!owner->native_read_self(budget,out))return false;
        if(!current()||!out.actual(epoch()))return false;
        owner->native_facts().self=out;return true;
    }
    bool commit(const native_service::Receipt& receipt) {return owner->native_runtime_commit(receipt);}
    void recorded(const native_service::Receipt& receipt,std::uint64_t accepted_prefix) {
        owner->native_record_physical(receipt,accepted_prefix);
    }
    bool commit_known(std::uint64_t serial)const {return owner->native_runtime_commit_known(serial);}
    bool alternative_move(const native_service::Primitive& previous,unsigned refused,native_service::Primitive& out) {
        return owner->native_runtime_alternative_move(previous,refused,out);
    }
};
native_service::ExecutionOutcome native_execute_word(native_service::Budget& budget,
    native_service::ExecutionState& execution,const native_service::Word& word) {
    NativeHost host{this};const auto offered=native_service_state().move_count;
    return native_service::execute(host,budget,execution,word,{offered?std::min(2u,offered-1):0u,true});
}
native_service::ExecutionOutcome native_reconcile_feedback(native_service::Budget& budget,
    native_service::ExecutionState& execution) {
    NativeHost host{this};return native_service::reconcile(host,budget,execution);
}
