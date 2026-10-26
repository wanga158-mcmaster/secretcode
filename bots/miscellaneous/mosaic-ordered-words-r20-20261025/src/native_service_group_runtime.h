// Runtime fragment. The retained service unit is a physical camp GROUP.
// A member is a fresh primitive binding; changing it is neither damage nor a
// completion. Geometry covers every possible remaining member, not one ordinal.
bool native_group_configure(){
    using namespace native_service;auto& s=native_service_state();
    if(s.group<0||std::size_t(s.group)>=groups.size())return false;
    const auto& memo=memory[std::size_t(s.group)];
    const auto result=s.group_work.configure({std::uint32_t(handle_camp_group_t(groups[std::size_t(s.group)])),
        s.group,s.group_generation,int(memo.begin),memo.end-memo.begin});
    return result==CycleGroupConfigure::configured||result==CycleGroupConfigure::retained;
}
void native_group_routes_changed(){
    auto& s=native_service_state();s.frontier.retire();
    if(s.group_goal){s.fields.retire(native_service::ServiceField::service);s.nav_intent=0;s.field_revision=0;}
}
void native_group_clock(native_service::Epoch now){
    auto& s=native_service_state();if(s.group_work.refresh_time(now))native_group_routes_changed();
}
native_service::CycleMemberAbsence native_group_absence(const native_service::CampFact& camp,
                                                       const native_service::GroupFact& group)const {
    using namespace native_service;const auto& s=native_service_state();const auto now=native_facts().epoch;
    if(camp.life!=Life::dead||!camp.actual(now)||!group.current(now)||group.ordinal!=camp.group_ordinal||
       group.id!=camp.group_id||group.ordinal!=s.group||
       (group.life!=GroupLife::alive&&group.life!=GroupLife::reward))return {};
    // A fresh Alive group still has living work; a fresh Reward group has not
    // started respawning. Even an immediate later payout needs its immutable
    // max-respawn duration before any member can return. Exclude only until
    // this conservative exclusive bound; expiry restores Unknown nomination.
    const auto ticks=std::max(1,groups[std::size_t(group.ordinal)].max_respawn_timer());
    const auto until=ticks>std::numeric_limits<int>::max()-epoch?std::numeric_limits<int>::max():epoch+ticks;
    return {s.group_generation,until,true};
}
void native_group_observe(const native_service::CampFact& camp,const native_service::GroupFact& group={}){
    using namespace native_service;auto& s=native_service_state();if(!native_group_configure())return;
    const auto revision=s.group_work.route_revision();
    s.group_work.observe(camp,native_facts().epoch,native_group_absence(camp,group));
    if(camp.living(native_facts().epoch)&&camp.group_ordinal==s.group){
        // One actual living member proves its group still has living Work.
        // This derives Life only; no unseen member HP or payout is invented.
        GroupFact alive{};alive.epoch=native_facts().epoch;alive.id=camp.group_id;alive.ordinal=camp.group_ordinal;
        alive.actual=true;alive.life=GroupLife::alive;s.cycle.observe_lifecycle(alive,{},alive.epoch);
    }
    if(revision!=s.group_work.route_revision())native_group_routes_changed();
}
bool native_arrival_member_needed(int ordinal,native_service::Epoch now)const {
    const auto& s=native_service_state();return !s.group_work.configured()||s.group_work.member_needed(ordinal,now);
}
std::uint64_t native_group_field_key()const {
    const auto& s=native_service_state();const auto generation=s.frontier.generation();
    if(!generation||generation>(std::numeric_limits<std::uint64_t>::max()>>8)||s.frontier.batch()>=256)return 0;
    return (generation<<8)|std::uint64_t(s.frontier.batch());
}
bool native_service_field_matches()const {
    const auto& s=native_service_state();const auto expected=s.group_goal?native_group_field_key():s.goal_key;
    return expected&&s.fields.semantic_intent(native_service::ServiceField::service)==expected;
}
void native_prepare_group_frontier(native_service::Budget& budget){
    using namespace native_service;auto& s=native_service_state();
    if(!s.group_goal||!s.board_ready||s.board_edit_count||!native_group_configure())return;
    const auto& memo=memory[std::size_t(s.group)];if(memo.end>s.frontier_members.size()){++s.unsupported;return;}
    const FrontierIntent intent{std::uint32_t(handle_camp_group_t(groups[std::size_t(s.group)])),s.group,
        s.group_generation,s.board_generation,s.group_work.route_revision(),std::max(1,s.approach_range)};
    if(!budget.checkpoint()||budget.pure_used()+3000>budget.pure_bound)return;
    if(!s.frontier.begin(walls->width(),walls->height(),{s.passable.data(),s.board_cursor},
       {s.frontier_members.data()+memo.begin,memo.end-memo.begin},intent)){++s.unsupported;return;}
    const auto units=budget.pure_used()+5000<=budget.pure_bound?32u:1u;
    if(s.frontier.status()==FrontierStatus::preparing){
        const auto progress=s.frontier.step(units,[&](int ordinal){return s.group_work.member_needed(ordinal,native_facts().epoch);});
        s.prepare_units+=progress.units;budget.checkpoint();return;
    }
    const auto roots=s.frontier.roots();const auto key=native_group_field_key();
    if(!key||roots.empty())return;
    if(s.fields.semantic_intent(ServiceField::service)!=key){
        if(!budget.checkpoint()||budget.pure_used()+3000>budget.pure_bound)return;
        if(!s.fields.begin(ServiceField::service,walls->width(),walls->height(),{s.passable.data(),s.board_cursor},roots,key))return;
    }
    s.nav_intent=s.goal_key;s.field_revision=s.fields.generation(ServiceField::service);
    if(!budget.checkpoint()||budget.pure_used()+600>budget.pure_bound)return;
    const auto progress=s.fields.step_guarded(ServiceField::service,units,[&]{
        return budget.checkpoint()&&budget.pure_used()+3000<=budget.pure_bound;
    });s.prepare_units+=progress.units;budget.checkpoint();
    if(s.fields.status(ServiceField::service,native_facts().self.pose)==NavigationStatus::unreachable&&
       s.frontier.status()==FrontierStatus::more){
        s.frontier.next_batch();s.fields.retire(ServiceField::service);s.nav_intent=0;
    }
}
