#pragma once
#include "continuation.h"

namespace plan4::runtime {
class Counterpart : public M0 {
    struct QuoteContext { Counterpart* owner=nullptr;bool frozen=false; };
    struct Receipt { Job job{};counterpart::Result result{};bool supported=false; };
    struct Storage {
        ActorFrame actors{};SpatialFrame spatial{};counterpart::PublicProfile profile{};
        counterpart::Input input{};counterpart::Boundary boundary{};
        counterpart::Result active{},control{},initial{};
        std::array<Receipt,max_jobs> receipts{};
    };
    std::unique_ptr<Storage> storage_=std::make_unique<Storage>();
    std::unique_ptr<Forecast> active_=std::make_unique<Forecast>(),control_=std::make_unique<Forecast>();
    counterpart::PhysicalContinuation continuation_;
    const Information* snapshot_=nullptr;
    ObservationClock clock_{};
    VisibleActor rival_{};
    bool enabled_=true,profile_observed_=false,source_=false,coupled_=false,budget_=false,ready_=false;
    int supported_=0,unknown_=0,work_=0,vertices_=0,receipt_count_=0;
    std::uint64_t revision_=0;
    counterpart::BindingGap gap_=counterpart::BindingGap::source;
    bool reject(counterpart::BindingGap gap) {gap_=gap;if(gap==counterpart::BindingGap::budget)budget_=true;return false;}
    bool budget(const Information& info) {return can_refine(info) || reject(counterpart::BindingGap::budget);}
    static bool fresh(Provenance p,int now,Source source) {return p.observed_round==now && p.status==Status::exact_now && p.source==source;}
    static bool same_job(const Job& a,const Job& b) {return a.kind==b.kind && a.group==b.group && a.second_group==b.second_group && a.altar==b.altar && a.menu_rank==b.menu_rank;}
    static bool unique_ids(const int* ids,int count) {
        if(count<0 || count>4)return false;
        for(int n=0;n<count;++n) {if(ids[n]<0)return false;for(int p=0;p<n;++p)if(ids[n]==ids[p])return false;}return true;
    }
    static bool has(const int* ids,int count,int id) {for(int n=0;n<count;++n)if(ids[n]==id)return true;return false;}
    const VisibleActor* actor(int id) const {
        const auto& actors=storage_->actors;if(actors.own.id==id)return &actors.own;
        for(int n=0;n<actors.count;++n)if(actors.samples[n].id==id)return &actors.samples[n];return nullptr;
    }
    static bool response(const counterpart::ResponseInput& input,counterpart::Response& out,const void* context) {
        const auto& call=*static_cast<const QuoteContext*>(context);auto& self=*call.owner;
        const auto& profile=self.storage_->profile;auto actual=input;
        if(call.frozen) {
            bool found=false;
            for(int n=0;n<self.storage_->initial.count;++n)if(self.storage_->initial.steps[n].round==input.round) {
                actual.decision=self.storage_->initial.steps[n].decision;found=true;break;
            }
            if(!found)return false;
        }
        ++self.work_;
        return profile.quote_response && profile.quote_response(self.rival_,actual,out,profile.quote_context);
    }
    bool input(const Information& info) {
        using Gap=counterpart::BindingGap;
        auto& s=*storage_;const auto& p=s.profile;const auto& actors=s.actors;const auto& spatial=s.spatial;
        if(snapshot_!=&info || clock_.round!=info.round || clock_.own.id!=info.own.id || !info.own.alive ||
            info.own.health<=0 || !std::isfinite(info.own.health) || !std::isfinite(info.own.maximum_health) || info.own.maximum_health<info.own.health ||
            info.own.channel!=Channel::none || info.in_progress_group_id>=0 || info.own.on_spawn ||
            info.group_count<0 || info.group_count>max_groups || info.altar_count<0 || info.altar_count>max_altars)
            return reject(Gap::source);
        source_=true;
        if(!profile_observed_ || p.clock.round!=info.round || p.clock.own.id!=info.own.id ||
            !p.exact_module || !p.frozen || !p.inspectable || !p.matches_rule || !p.rule_identity ||
            !p.response_identity || !p.own_executor_identity || !p.matches_response || !p.matches_own_first_step ||
            !p.complete_choice_inputs || !p.complete_other_actor_laws || !p.ordinary_response_exact ||
            !p.travel_window_no_other_effects || !p.quote_response || p.turns<1 || p.turns>counterpart::max_turns ||
            !std::any_of(p.executed_module.begin(),p.executed_module.end(),[](auto byte){return byte!=0;})) return reject(Gap::profile);
        if(actors.clock.round!=info.round || actors.clock.own.id!=info.own.id || actors.truncated || !actors.roster_complete ||
            !actors.own_observed || actors.count<0 || actors.count>9 || actors.allied_count<1 || actors.allied_count>5 ||
            actors.enemy_count<1 || actors.enemy_count>5 || !unique_ids(p.stationary_choice_ids.data(),p.stationary_choice_count) ||
            !unique_ids(p.inert_other_enemy_ids.data(),p.inert_other_enemy_count) || p.stationary_choice_count!=actors.allied_count-1 ||
            p.inert_other_enemy_count!=actors.enemy_count-1) return reject(Gap::roster);
        if(actors.count!=actors.allied_count+actors.enemy_count-1)return reject(Gap::roster);
        for(int n=0;n<actors.count;++n) {
            if(actors.samples[n].id==info.own.id)return reject(Gap::roster);
            for(int k=0;k<n;++k)if(actors.samples[n].id==actors.samples[k].id)return reject(Gap::roster);
        }
        if(!p.order_certified || !p.opposing_teams || !actors.draft_order_known ||
            p.rival_before_owner!=p.rival_already_completed) return reject(Gap::ordering);
        const auto* rival=actor(p.rival_id);
        if(!rival || rival->ally || !rival->alive || !fresh(rival->provenance,info.round,Source::personal_vision) ||
            rival->completed_turn!=p.rival_already_completed || rival->draft_ordinal<0 || !std::isfinite(rival->strength) ||
            rival->strength<0 || rival->attack_range<1 || rival->current_priority!=rival->next_priority) return reject(Gap::ordering);
        rival_=*rival;
        const auto& own=actors.own;
        if(!own.ally || own.id!=info.own.id || own.position!=info.own.position || own.health!=info.own.health || own.bag!=info.own.bag ||
            own.maximum_health!=info.own.maximum_health || !fresh(own.provenance,info.round,Source::own_state) ||
            own.current_priority!=own.next_priority || own.draft_ordinal<0) return reject(Gap::source);
        s.input={};auto& out=s.input;
        out.round=info.round;out.own_id=info.own.id;out.rival_id=rival->id;out.before={info.own.position,info.own.bag};
        out.own_health=info.own.health;out.rival=rival->position;out.rival_provenance=rival->provenance;out.rival_personally_visible=true;
        out.policy={p.rule_identity,p.inspectable,p.frozen,p.matches_rule};out.geometry=p.rival_geometry;
        out.corridor_min=p.corridor_min;out.corridor_max=p.corridor_max;out.public_vision_radius=p.public_vision_radius;
        out.vision_modifiers_known=p.vision_modifiers_known;out.complete_choice_inputs=true;out.order_known=true;out.opposing_teams=true;
        out.rival_before_own_current_round=p.rival_before_owner;out.rival_already_completed=p.rival_already_completed;
        out.work_budget=info.work_budget;
        bool own_in_roster=false,rival_in_roster=false;
        for(int side=0;side<2;++side) {
            const int count=side?actors.enemy_count:actors.allied_count;const auto& ids=side?actors.enemy_roster:actors.allied_roster;
            for(int n=0;n<count;++n) {
                if(!budget(info))return false;
                for(int k=0;k<n;++k)if(ids[k]==ids[n])return reject(Gap::roster);
                if(side && has(actors.allied_roster.data(),actors.allied_count,ids[n]))return reject(Gap::roster);
                if(!side && ids[n]==info.own.id){own_in_roster=true;continue;}
                if(side && ids[n]==rival->id){rival_in_roster=true;continue;}
                const auto* other=actor(ids[n]);
                if(!other || !other->alive || other->ally==bool(side) || !fresh(other->provenance,info.round,Source::personal_vision) ||
                    other->current_priority!=other->next_priority || other->draft_ordinal<0) return reject(Gap::roster);
                if(side) {if(!has(p.inert_other_enemy_ids.data(),p.inert_other_enemy_count,ids[n]))return reject(Gap::profile);}
                else {
                    if(!has(p.stationary_choice_ids.data(),p.stationary_choice_count,ids[n]))return reject(Gap::profile);
                    out.other_targets[out.other_count++]={{other->id,other->position,other->bag,true},other->provenance,true};
                }
            }
        }
        if(!own_in_roster || !rival_in_roster)return reject(Gap::roster);
        if(spatial.clock.round!=info.round || spatial.clock.own.id!=info.own.id || !spatial.current_complete ||
            !spatial.topology.complete || !spatial.topology.walkable || !spatial.occupancy || !spatial.route_prefix ||
            !spatial.travel.distance || !p.rival_oriented_geometry || !p.rival_geometry.open || !p.rival_geometry.distance ||
            !p.vision_modifiers_known || p.public_vision_radius<1 || (spatial.information && spatial.information!=&info))return reject(Gap::geometry);
        const auto width=std::int64_t(p.corridor_max.x)-p.corridor_min.x+1,height=std::int64_t(p.corridor_max.y)-p.corridor_min.y+1;
        if(p.corridor_min.x<0 || p.corridor_min.y<0 || p.corridor_max.x>=spatial.topology.width || p.corridor_max.y>=spatial.topology.height ||
            width<1 || height<1 || width>counterpart::max_corridor_tiles || height>counterpart::max_corridor_tiles ||
            width*height>counterpart::max_corridor_tiles || width*height>spatial.graph_quota)return reject(Gap::geometry);
        for(int y=p.corridor_min.y;y<=p.corridor_max.y;++y)for(int x=p.corridor_min.x;x<=p.corridor_max.x;++x) {
            if(!budget(info))return false;++vertices_;
            if(p.rival_geometry.open({x,y},p.rival_geometry.context)!=1)return reject(Gap::geometry);
        }
        if(p.camp_count!=info.group_count || p.camp_count<0 || p.camp_count>counterpart::max_camps || p.camp_cursor<0 || !p.camp_cursor_publicly_derived)return reject(Gap::profile);
        for(int n=0;n<p.camp_count;++n) {
            bool found=false;for(int g=0;g<info.group_count;++g)found|=p.camp_priority[n].id==info.groups[g].id && p.camp_priority[n].position==info.groups[g].position;
            if(!found)return reject(Gap::profile);
            for(int k=0;k<n;++k)if(p.camp_priority[n].id==p.camp_priority[k].id)return reject(Gap::profile);
        }
        out.camp_count=p.camp_count;out.camp_cursor=p.camp_cursor;out.camp_priority=p.camp_priority;
        return true;
    }
    bool boundary(const Information& info,const Job& job) {
        using Gap=counterpart::BindingGap;auto& s=*storage_;auto& out=s.boundary;const auto& spatial=s.spatial;
        out={};
        if(job.kind==JobKind::farm && job.group>=0 && job.group<info.group_count) {
            const auto& group=info.groups[job.group];
            // A readiness wait is not an ordinary immediate first step.
            if(!group.visible || !fresh(group.provenance,info.round,Source::personal_vision) ||
                (group.lifecycle!=Lifecycle::living && group.lifecycle!=Lifecycle::cleared))return reject(Gap::prefix);
            out.target=group.position;
        } else if(job.kind==JobKind::bank && job.altar>=0 && job.altar<info.altar_count && info.altars[job.altar].accessible)out.target=info.altars[job.altar].position;
        else return reject(Gap::prefix);
        const int total=distance(info,info.own.position,out.target);
        // Stay strictly inside the first travel leg. Reaching a camp/channel
        // boundary needs a separately observed physical action checkpoint.
        if(total<2)return reject(Gap::route);
        out.turns=std::min({s.profile.turns,total-1,horizon(info)});
        if(out.turns<1 || vertices_+out.turns>spatial.graph_quota)return reject(Gap::route);
        std::array<Point,counterpart::max_turns> points;
        if(!budget(info))return false;
        const int count=spatial.route_prefix(info.own.position,out.target,points.data(),out.turns,spatial.route_context);
        if(!budget(info))return false;
        if(count!=out.turns)return reject(Gap::route);
        Point previous=info.own.position;
        for(int n=0;n<count;++n) {
            if(!budget(info))return false;++vertices_;
            const auto point=points[n];
            if(counterpart::chebyshev(previous,point)!=1 || !spatial.topology.walkable(point,spatial.topology.context) ||
                spatial.occupancy(point,spatial.occupancy_context)!=TileOccupancy::free ||
                distance(info,point,out.target)!=total-n-1)return reject(Gap::route);
            out.after[n]={point,info.own.bag};previous=point;
        }
        return true;
    }
    static bool same_response(const counterpart::Result& a,const counterpart::Result& b) {
        if(a.own_damage!=b.own_damage || a.final_health!=b.final_health || a.own_turns!=b.own_turns || a.predicted_death!=b.predicted_death)return false;
        return true;
    }
    void predict_current(const Information& info,const Job& job,const Menu& menu,Forecast& out,int index) {
        using Gap=counterpart::BindingGap;auto& s=*storage_;
        auto fallback=[&](){plan4::predict_into(info,job,menu,out);++unknown_;if(budget_)out.budget_exhausted=true;};
        if(!ready_ || !boundary(info,job)){fallback();return;}
        auto& input=s.input;input.turns=s.boundary.turns;input.after=s.boundary.after;
        // Freeze the disabled policy's sequence from ONE pre-action body before
        // comparing candidates. Its actual collision/damage quote still sees
        // the current candidate physical body in both arms.
        QuoteContext active{this,false},control{this,true};input.quote_context=&active;
        counterpart::predict_into(input,true,s.active);input.quote_context=&control;
        counterpart::predict_into(input,false,s.control);
        for(int n=0;n<s.control.count;++n)for(int f=0;f<s.initial.count;++f)if(s.control.steps[n].round==s.initial.steps[f].round)
            s.control.steps[n].decision=s.initial.steps[f].decision;
        if(!s.active.supported || !s.control.supported || s.active.own_delay || s.control.own_delay ||
            s.active.recall_interrupted || s.control.recall_interrupted) {
            reject(s.active.unsupported==counterpart::Unsupported::budget || s.control.unsupported==counterpart::Unsupported::budget?Gap::budget:Gap::response);fallback();return;
        }
        if(!continuation_.predict_into(info,job,menu,s.boundary,s.active,*active_) ||
            !continuation_.predict_into(info,job,menu,s.boundary,s.control,*control_)) {reject(can_refine(info)?Gap::continuation:Gap::budget);fallback();return;}
        if(active_->budget_exhausted || control_->budget_exhausted){reject(Gap::budget);fallback();return;}
        out=enabled_?*active_:*control_;++supported_;
        coupled_|=enabled_ && !same_response(s.active,s.control);
        if(index>=0 && index<max_jobs) {s.receipts[index]={job,enabled_?s.active:s.control,true};receipt_count_=std::max(receipt_count_,index+1);}
    }
    struct CandidateOnly {Counterpart& model;int index=0;void predict_into(const Information& i,const Job& j,const Menu& m,Forecast& f){model.predict_current(i,j,m,f,index++);}};
    void begin(const Information& info) {
        supported_=unknown_=work_=vertices_=receipt_count_=0;source_=coupled_=budget_=ready_=false;gap_=counterpart::BindingGap::none;
        storage_->active={};storage_->control={};storage_->initial={};
        for(auto& receipt:storage_->receipts)receipt.supported=false;
        if(!input(info))return;
        auto& input=storage_->input;input.turns=std::min(storage_->profile.turns,horizon(info));
        for(int n=0;n<input.turns;++n)input.after[n]=input.before;
        QuoteContext initial{this,false};input.quote_response=response;input.quote_context=&initial;
        counterpart::predict_into(input,false,storage_->initial);
        if(!storage_->initial.supported){reject(storage_->initial.unsupported==counterpart::Unsupported::budget?counterpart::BindingGap::budget:counterpart::BindingGap::response);return;}
        ready_=true;
    }
    void invalidate() {
        receipt_count_=supported_=unknown_=work_=0;source_=coupled_=ready_=budget_=false;gap_=counterpart::BindingGap::source;
        storage_->active.count=storage_->control.count=0;
    }
public:
    explicit Counterpart(bool enabled=true):enabled_(enabled){}
    const char* name() const{return "counterpart";}
    void observe_profile(const counterpart::PublicProfile& profile){invalidate();storage_->profile=profile;profile_observed_=true;++revision_;}
    void observe_actors(const ActorFrame& actors){invalidate();storage_->actors=actors;++revision_;}
    void observe_spatial(const SpatialFrame& spatial){invalidate();storage_->spatial=spatial;vertices_=0;++revision_;}
    void observe_snapshot(const Information& info,MemberFrame={}){invalidate();snapshot_=&info;clock_={info.round,{info.own.id}};}
    void evaluate_into(const Information& info,const Menu& menu,Evaluation& out){begin(info);CandidateOnly candidates{*this};evaluate_model(info,menu,candidates,out);budget_|=out.budget_exhausted;}
    void predict_into(const Information& info,const Job& job,const Menu& menu,Forecast& out){begin(info);predict_current(info,job,menu,out,0);budget_|=out.budget_exhausted;}
    int spatial_vertices() const{return vertices_;}
    ModelEvidence model_evidence() const {
        ModelEvidence out;out.reported=true;out.supported=supported_>0 && !budget_;
        out.coupling_enabled=source_ && enabled_ && coupled_;out.reason=out.supported?0:int(gap_);
        out.samples=storage_->active.count+storage_->control.count;out.unknown=unknown_;out.paths=supported_;out.work=work_;out.revision=revision_;return out;
    }
    // Exact response coordinates and branches are retained privately. The
    // generic common event DTO lacks coordinates; no fake endpoint receipt is
    // emitted. A typed production response logger remains an integration gate.
    const counterpart::Result* prediction_steps(const Information& info,const Job& job) const {
        if(snapshot_!=&info || clock_.round!=info.round || clock_.own.id!=info.own.id)return nullptr;
        for(int n=0;n<receipt_count_;++n)if(storage_->receipts[n].supported && same_job(storage_->receipts[n].job,job))return &storage_->receipts[n].result;
        return nullptr;
    }
};
} // namespace plan4::runtime
