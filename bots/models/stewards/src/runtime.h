#pragma once
#include "stewards.h"
#include "../../common/model-runtime.h"
#include <memory>

namespace plan4::stewards {
struct Policy { const State* state = nullptr; bool takeover_enabled = true; };
inline bool acquisition_allowed(int group, int, const Information& info, const void* model, void*) {
    const auto& policy = *static_cast<const Policy*>(model);
    return !policy.state || policy.state->own_id != info.own.id || group < 0 || group >= info.group_count ||
        availability(*policy.state, info.groups[group].id, info.round, policy.takeover_enabled).own_available;
}
inline ForecastHooks make_hooks(const Policy& policy) {
    ForecastHooks hooks; hooks.model = &policy; hooks.acquisition_allowed = acquisition_allowed;
    return hooks;
}
inline void predict_into(const Information& info, const Job& job, const Menu& menu,
                         const State& state, bool takeover_enabled, Forecast& output) {
    const Policy policy{&state, takeover_enabled};
    plan4::predict_into(info, job, menu, output, make_hooks(policy));
}
// SDK conversion still needs the actual draft roster, complete member-spawn
// personal mask and classified service evidence. This wrapper makes no SDK
// restriction, menu mutation, ally output, gold transfer or invented capture.
} // namespace plan4::stewards

namespace plan4::runtime {
// Actor-local reconciliation only. All body fields below come from the common
// collector; public geometry is cached separately and never becomes live sight.
class Stewards : public M0 {
public:
    enum class EvidenceReason { none, unconfigured, not_current, malformed, no_takeover_proof };
private:
    static constexpr int frame_limit=16, member_limit=32;
    struct Geometry {int id=-1,count=0;std::array<PublicMember,member_limit> members{};bool valid=false;};
    struct Pending {
        ActorFrame actors{};ProducerFrame producers{};
        std::array<Group,frame_limit> groups{};int count=0,round=-1,own=-1;
        bool actor_seen=false,lifecycle_seen=false,producer_seen=false,snapshot_seen=false,committed=false,bad=false;
    };
    std::unique_ptr<stewards::State> state_=std::make_unique<stewards::State>();
    std::unique_ptr<Pending> pending_=std::make_unique<Pending>();
    std::array<std::unique_ptr<Geometry>,max_groups> geometry_{};
    std::array<int,max_groups> group_ids_{};int geometry_count_=0,group_count_=0,source_actor_=-1;
    struct Order {int id=-1,ordinal=-1;bool before=false,next_before=false;};
    std::array<Order,5> order_{};int order_count_=0,order_round_=-1;
    bool enabled_=true,public_groups_known_=false,binding_dirty_=true;
    int current_proofs_=0,current_full_sight_=0;
    int forecast_paths_=0;
    std::uint64_t evidence_revision_=0;
    static bool current(Provenance p,int round,Source source){return p.observed_round==round&&p.status==Status::exact_now&&p.source==source;}
    static int separation(Point a,Point b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
    Geometry* geometry(int id){for(int n=0;n<geometry_count_;++n)if(geometry_[n]->id==id)return geometry_[n].get();return nullptr;}
    static bool roster(const ActorFrame& frame){
        if(!frame.roster_complete||frame.clock.round<0||frame.clock.own.id<0||frame.allied_count<1||frame.allied_count>5
            ||frame.enemy_count<0||frame.enemy_count>5)return false;
        int own=0;
        for(int a=0;a<frame.allied_count;++a){const int id=frame.allied_roster[a];if(id<0)return false;own+=id==frame.clock.own.id;
            for(int b=0;b<a;++b)if(id==frame.allied_roster[b])return false;
            for(int b=0;b<frame.enemy_count;++b)if(id==frame.enemy_roster[b])return false;}
        for(int a=0;a<frame.enemy_count;++a){if(frame.enemy_roster[a]<0)return false;for(int b=0;b<a;++b)if(frame.enemy_roster[a]==frame.enemy_roster[b])return false;}
        return own==1;
    }
    static bool allied(const ActorFrame& frame,int id){for(int n=0;n<frame.allied_count;++n)if(frame.allied_roster[n]==id)return true;return false;}
    static bool enemy(const ActorFrame& frame,int id){for(int n=0;n<frame.enemy_count;++n)if(frame.enemy_roster[n]==id)return true;return false;}
    static bool body(const VisibleActor& actor,int round,bool own){
        return actor.id>=0&&actor.alive&&actor.position.x>=0&&actor.position.y>=0&&actor.attack_range>=0
            &&std::isfinite(actor.health)&&std::isfinite(actor.maximum_health)&&actor.health>0&&actor.health<=actor.maximum_health
            &&current(actor.provenance,round,own?Source::own_state:Source::personal_vision);
    }
    static bool actor_values(const ActorFrame& frame){
        if(!roster(frame)||frame.truncated||!frame.own_observed||frame.count<0||frame.count>9||!frame.own.ally
            ||frame.own.id!=frame.clock.own.id||!body(frame.own,frame.clock.round,true))return false;
        for(int a=0;a<frame.count;++a){const auto& actor=frame.samples[a];
            if(actor.id==frame.own.id||!body(actor,frame.clock.round,false)||actor.ally!=allied(frame,actor.id)
                ||(!allied(frame,actor.id)&&!enemy(frame,actor.id)))return false;
            for(int b=0;b<a;++b)if(actor.id==frame.samples[b].id)return false;}
        return true;
    }
    const VisibleActor* actor(int id)const{
        if(pending_->actors.own_observed&&pending_->actors.own.id==id)return&pending_->actors.own;
        for(int n=0;n<pending_->actors.count&&n<9;++n)if(pending_->actors.samples[n].id==id)return&pending_->actors.samples[n];
        return nullptr;
    }
    void configure(){
        if(!public_groups_known_||source_actor_!=pending_->own||!roster(pending_->actors))return;
        bool same=state_->supported&&!binding_dirty_&&state_->own_id==pending_->own&&state_->actor_count==pending_->actors.allied_count;
        for(int a=0;same&&a<pending_->actors.allied_count;++a)same=stewards::roster_actor(*state_,pending_->actors.allied_roster[a]);
        if(same)return;
        if(!stewards::configure(*state_,group_ids_.data(),group_count_,pending_->actors.allied_roster.data(),
            pending_->actors.allied_count,pending_->own,true))state_->supported=false;
        binding_dirty_=false;
    }
    void invalidate(){
        pending_->bad=true;current_proofs_=current_full_sight_=0;
        if(state_->supported&&pending_->round>=0)stewards::observe(*state_,{pending_->round,-1,nullptr});
    }
    void merge(const Group* groups,int count,bool duplicate_is_bad){
        if(count<0||count>max_groups||(count&&!groups)){invalidate();return;}
        for(int n=0;n<count;++n){const auto& group=groups[n];
            if(group.id<0){invalidate();return;}
            for(int a=0;a<n;++a)if(groups[a].id==group.id){invalidate();return;}
            if(!group.visible||!current(group.provenance,pending_->round,Source::personal_vision))continue;
            int slot=0;while(slot<pending_->count&&pending_->groups[slot].id!=group.id)++slot;
            if(slot<pending_->count){
                if(duplicate_is_bad||pending_->groups[slot].lifecycle!=group.lifecycle){invalidate();return;}
            }else if(slot==frame_limit){invalidate();return;}else ++pending_->count;
            pending_->groups[slot]=group;
        }
    }
    bool producer_values()const{
        const auto& frame=pending_->producers;
        if(!pending_->producer_seen||frame.clock.round!=pending_->round||frame.clock.own.id!=pending_->own||frame.truncated||frame.count<0||frame.count>4)return false;
        for(int n=0;n<frame.count;++n){const auto& row=frame.samples[n];
            if(row.actor_id<0||!row.personally_visible||!current(row.provenance,pending_->round,Source::personal_vision))return false;
            const auto* joined=actor(row.actor_id);
            if(!joined||joined->position!=row.position||joined->ally!=allied(pending_->actors,row.actor_id))return false;
            for(int p=0;p<n;++p)if(row.actor_id==frame.samples[p].actor_id)return false;}
        return true;
    }
    static bool layout_row(const PublicLayoutFrame& frame,const PublicGroup& group,std::array<PublicMember,member_limit>& members){
        if(group.group_id<0||group.count<1||group.count>member_limit||group.first<0||group.first>frame.member_count-group.count)return false;
        std::array<bool,member_limit> ordinals{};
        for(int m=0;m<group.count;++m){const auto& member=frame.members[group.first+m];
            if(member.group_id!=group.group_id||member.member_id<0||member.ordinal<0||member.ordinal>=group.count||ordinals[member.ordinal]
                ||member.declared_members!=group.count||member.attack_range<0||member.spawn.x<0||member.spawn.y<0||member.kind<0
                ||!std::isfinite(member.maximum_health)||member.maximum_health<=0)return false;
            ordinals[member.ordinal]=true;members[member.ordinal]=member;
        }
        return true;
    }
    bool stable_order(){
        const auto& own=pending_->actors.own;
        const bool draft_known=pending_->actors.draft_order_known;
        if(own.completed_turn||(draft_known&&(own.draft_ordinal<0||own.draft_ordinal>=pending_->actors.allied_count))){order_round_=-1;return false;}
        std::array<Order,5> next{};int count=0;bool stable=true;
        for(int a=0;a<state_->actor_count;++a){const auto* seen=actor(state_->actors[a]);
            if(!seen||(draft_known&&(seen->draft_ordinal<0||seen->draft_ordinal>=state_->actor_count))){order_round_=-1;return false;}
            for(int p=0;draft_known&&p<count;++p)if(next[p].ordinal==seen->draft_ordinal){order_round_=-1;return false;}
            const bool before=seen->id!=own.id&&(seen->current_priority>own.current_priority||
                (seen->current_priority==own.current_priority&&(draft_known?seen->draft_ordinal<own.draft_ordinal:seen->completed_turn)));
            const bool next_before=draft_known?(seen->id!=own.id&&(seen->next_priority>own.next_priority||
                (seen->next_priority==own.next_priority&&seen->draft_ordinal<own.draft_ordinal))):before;
            if(!draft_known)stable&=seen->current_priority==seen->next_priority&&own.current_priority==own.next_priority;
            if(seen->completed_turn!=before){order_round_=-1;return false;}
            stable&=before==next_before;next[count++]={seen->id,draft_known?seen->draft_ordinal:-1,before,next_before};
        }
        if(order_round_==pending_->round-1){
            stable&=order_count_==count;
            for(int n=0;stable&&n<count;++n)stable=order_[n].id==next[n].id&&order_[n].ordinal==next[n].ordinal
                &&order_[n].before==next[n].before&&order_[n].next_before==next[n].before;
        }
        order_=next;order_count_=count;order_round_=pending_->round;return stable;
    }
    void commit(){
        if(pending_->committed)return;
        current_proofs_=current_full_sight_=0;
        pending_->committed=true;configure();
        if(!state_->supported)return;
        if(pending_->bad||!pending_->actor_seen||!pending_->lifecycle_seen||!actor_values(pending_->actors)||!producer_values()){
            order_round_=-1;stewards::observe(*state_,{pending_->round,-1,nullptr});return;}
        const bool order_stable=stable_order();
        std::array<stewards::GroupObservation,frame_limit> observations{};
        for(int n=0;n<pending_->count;++n){const auto& group=pending_->groups[n];auto& row=observations[n];row.group_id=group.id;
            row.lifecycle=group.lifecycle;row.lifecycle_provenance=group.provenance;row.visibility_provenance=group.provenance;
            const auto* layout=geometry(group.id);
            row.all_member_spawns_visible=layout&&layout->valid&&group.all_spawns_visible;
            bool all_actors=true,far=true,own_spawn=false;
            for(int a=0;a<pending_->actors.allied_count;++a){const auto* seen=actor(pending_->actors.allied_roster[a]);
                if(!seen){all_actors=false;continue;}
                if(layout&&layout->valid)for(int m=0;m<layout->count;++m){const auto& member=layout->members[m];
                    const auto reach=std::int64_t(std::max(seen->attack_range,member.attack_range))+1;
                    far&=std::int64_t(separation(seen->position,member.spawn))>reach;
                    own_spawn|=seen->id==pending_->own&&seen->position==member.spawn;
                }
            }
            // No spell-service reach is supplied by ActorFrame. Living-group
            // absence remains unknown; cleared groups require physical spawn
            // occupancy, so fresh complete geometry/body proof is sufficient.
            const bool known_absence=group.lifecycle==Lifecycle::cleared&&all_actors&&far&&order_stable&&row.all_member_spawns_visible;
            row.service=known_absence?stewards::Service::none:stewards::Service::ambiguous;
            row.service_observation_complete=known_absence;row.service_provenance=group.provenance;
            const auto ownership=stewards::availability(*state_,group.id,pending_->round,false);
            for(int p=0;p<pending_->producers.count;++p){const auto& sample=pending_->producers.samples[p];const auto* seen=actor(sample.actor_id);
                if(sample.group_id!=group.id||!seen||!seen->ally||seen->position!=sample.position||sample.ambiguous)continue;
                bool on_spawn=false;if(layout&&layout->valid)for(int m=0;m<layout->count;++m)on_spawn|=sample.position==layout->members[m].spawn;
                if(group.lifecycle==Lifecycle::cleared&&on_spawn&&sample.producing&&sample.evidence==ProductionEvidence::visible_cleared_spawn){
                    row.service=stewards::Service::ally_cleared_spawn;row.servicing_actor=sample.actor_id;row.service_provenance=sample.provenance;row.service_observation_complete=false;break;}
                if(sample.actor_id==ownership.primary_id&&on_spawn){row.service=stewards::Service::owner_presence;
                    row.servicing_actor=sample.actor_id;row.service_provenance=sample.provenance;row.service_observation_complete=false;}
            }
            row.own_capture_witnessed=group.lifecycle==Lifecycle::cleared&&group.occupancy_known&&!group.contested&&own_spawn&&row.all_member_spawns_visible;
            row.capture_provenance={pending_->round,Source::own_occupancy,Status::exact_now};
        }
        stewards::observe(*state_,{pending_->round,pending_->count,observations.data()});
        for(int n=0;n<pending_->count;++n){const auto proof=stewards::availability(*state_,pending_->groups[n].id,pending_->round,true);
            current_proofs_+=proof.backup_eligible||proof.protected_capture;
            const int g=stewards::find(*state_,pending_->groups[n].id);current_full_sight_+=g>=0&&state_->records[g].full_sight;}
    }
public:
    explicit Stewards(bool enabled=true):enabled_(enabled){}
    const char* name()const{return"stewards";}
    void observe_actors(const ActorFrame& frame){
        ++evidence_revision_;
        if(pending_->actor_seen&&frame.clock.round<=pending_->round){invalidate();return;}
        if(pending_->round>=0)commit();
        current_proofs_=current_full_sight_=forecast_paths_=0;
        *pending_=Pending{};pending_->round=frame.clock.round;pending_->own=frame.clock.own.id;pending_->actors=frame;pending_->actor_seen=true;
        if(!roster(frame)){state_->supported=false;pending_->bad=true;}
        if(source_actor_>=0&&source_actor_!=pending_->own){state_->supported=false;public_groups_known_=false;binding_dirty_=true;order_round_=-1;}
        configure();
    }
    void observe_lifecycles(const LifecycleFrame& frame){
        if(frame.clock.round!=pending_->round||frame.clock.own.id!=pending_->own||pending_->lifecycle_seen||pending_->committed){invalidate();return;}
        pending_->lifecycle_seen=true;if(frame.truncated){invalidate();return;}merge(frame.groups,frame.count,true);
    }
    void observe_producers(const ProducerFrame& frame){
        if(frame.clock.round!=pending_->round||frame.clock.own.id!=pending_->own||pending_->producer_seen||pending_->committed){invalidate();return;}
        pending_->producers=frame;pending_->producer_seen=true;
    }
    void observe_layout(const PublicLayoutFrame& frame){
        if(frame.clock.own.id!=pending_->own||frame.clock.round!=pending_->round||frame.group_count<0||frame.group_count>8||frame.member_count<0||frame.member_count>256
            ||(frame.group_count&&!frame.groups)||(frame.member_count&&!frame.members)){invalidate();return;}
        std::array<int,256> member_ids{};int seen_members=0;
        // Validate the entire incoming prefix before caching any row. A
        // malformed later row cannot leave a cherry-picked geometry receipt.
        for(int n=0;n<frame.group_count;++n){const auto& group=frame.groups[n];
            for(int p=0;p<n;++p)if(frame.groups[p].group_id==group.group_id){invalidate();return;}
            if(!group.complete)continue;
            std::array<PublicMember,member_limit> members{};
            if(!layout_row(frame,group,members)||group.count>int(member_ids.size())-seen_members){invalidate();return;}
            for(int m=0;m<group.count;++m)member_ids[seen_members++]=members[m].member_id;
        }
        std::sort(member_ids.begin(),member_ids.begin()+seen_members);
        for(int n=1;n<seen_members;++n)if(member_ids[n]==member_ids[n-1]){invalidate();return;}
        for(int n=0;n<frame.group_count;++n){const auto& group=frame.groups[n];if(!group.complete)continue;
            std::array<PublicMember,member_limit> members{};layout_row(frame,group,members);
            auto* cached=geometry(group.group_id);
            if(!cached){if(geometry_count_==max_groups){invalidate();return;}geometry_[geometry_count_]=std::make_unique<Geometry>();cached=geometry_[geometry_count_++].get();cached->id=group.group_id;}
            if(cached->valid){
                bool same=cached->count==group.count;
                for(int m=0;same&&m<group.count;++m)same=cached->members[m].member_id==members[m].member_id&&cached->members[m].spawn==members[m].spawn
                    &&cached->members[m].attack_range==members[m].attack_range&&cached->members[m].kind==members[m].kind&&cached->members[m].maximum_health==members[m].maximum_health;
                if(!same){cached->valid=false;invalidate();return;}
            }
            cached->count=group.count;cached->members=members;cached->valid=true;
        }
    }
    void observe_snapshot(const Information& info,MemberFrame ={}){
        if(info.round!=pending_->round||info.own.id!=pending_->own||pending_->snapshot_seen||pending_->committed){invalidate();return;}
        pending_->snapshot_seen=true;
        if(info.public_layout_truncated||info.group_count<0||info.group_count>max_groups){public_groups_known_=state_->supported=false;invalidate();return;}
        group_count_=info.group_count;for(int g=0;g<group_count_;++g)group_ids_[g]=info.groups[g].id;
        std::sort(group_ids_.begin(),group_ids_.begin()+group_count_);
        binding_dirty_|=group_count_!=state_->group_count;
        for(int g=0;g<group_count_;++g){if(group_ids_[g]<0||(g&&group_ids_[g]==group_ids_[g-1])){state_->supported=false;invalidate();return;}
            if(g<state_->group_count)binding_dirty_|=group_ids_[g]!=state_->records[g].group_id;}
        source_actor_=info.own.id;public_groups_known_=true;configure();merge(info.groups.data(),info.group_count,false);commit();
    }
    void predict_into(const Information& info,const Job& job,const Menu& menu,Forecast& out){
        if(info.round==pending_->round&&info.own.id==pending_->own)commit();
        stewards::predict_into(info,job,menu,*state_,enabled_,out);
        forecast_paths_+=state_->supported&&info.round==pending_->round&&info.own.id==pending_->own&&!out.budget_exhausted;
    }
    stewards::Availability availability(int group,int round){if(round==pending_->round)commit();return stewards::availability(*state_,group,round,enabled_);}
    const stewards::State& state()const{return*state_;}
    ModelEvidence model_evidence()const{
        // samples counts current takeover/own-occupancy proofs; paths counts
        // complete forecasts under this static eligibility law. Neither claims
        // an action difference. Bad dynamic evidence resets takeover while the
        // configured static law remains active, rather than becoming M0.
        const bool current=pending_->committed&&pending_->snapshot_seen&&state_->round==pending_->round;
        const auto reason=!state_->supported?EvidenceReason::unconfigured:pending_->bad?EvidenceReason::malformed:
            !current?EvidenceReason::not_current:current_proofs_?EvidenceReason::none:EvidenceReason::no_takeover_proof;
        return{true,state_->supported&&current,enabled_,int(reason),current_proofs_,
            std::max(0,state_->group_count-current_full_sight_),forecast_paths_,pending_->count,evidence_revision_};
    }
};
} // namespace plan4::runtime
