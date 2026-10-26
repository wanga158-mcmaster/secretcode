#pragma once
#include "configuration.h"
#include "../draft.h"
#include <cstdio>
#include <iostream>
#include <memory>
namespace p8::probe_reveal_commit::native {
class Runtime {
    bool diagnostic_routes_started_=false;
    Routes routes_;sdk::Adapter adapter_;std::unique_ptr<Inputs> input_=p8::native::make_default<Inputs>();
    std::unique_ptr<EvidenceLedger> ledger_;std::unique_ptr<Protocol> protocol_;Execution execution_{};
    Work& work_=execution_.work;std::unique_ptr<Layout> layout_=std::make_unique<Layout>();
    RivalDistance distances_;Slots roles_{};p6::ScoreHistory scores_;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};int initial_safety_count_=0;
    int bank_=-1,other_bank_=-1,selected_=-1,renewal_=-1,last_round_=-1,rival_=-1;
    int inspection_turns_=0,inspection_site_=-1,inspection_cursor_=0,next_scan_=0,helper_starts_=0;Route inspection_{};
    bool configured_=false,first_income_=false,unavailable_logged_=false,proof_logged_=false;
    std::uint64_t physical_=0;std::uint16_t reported_=0;
    struct Member {int actor=-1;NumericState state{};};std::array<Member,2> members_{};
    bool fresh(const sdk::Snapshot& snapshot,std::uint64_t fuel=22000) const {
        return snapshot.valid&&int(cambc::round_number())==snapshot.frame.clock.round&&cambc::fuel_remaining()>fuel;
    }
    bool unlocked(int bank) const {
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))if(point(lock.position())==routes_.banks[bank].altar)return false;
        return true;
    }
    bool exclusive(int source,int owner,bool pair=false) const {
        for(const auto& ally:cambc::vision::champions(cambc::team::ally))if(ally.is_alive()&&routes_.sites[source].group.spawn_tiles().is_set(ally.position())) {
            const int id=sdk::identity(ally);if(id==owner)continue;
            if(!pair||!input_->draft.valid()||(id!=input_->draft.actors[0]&&id!=input_->draft.actors[2]))return false;
        }
        return true;
    }
    bool calm(const sdk::Snapshot& snapshot) const {
        const auto pos=snapshot.frame.own.actor.position;
        for(const auto& enemy:cambc::vision::champions(cambc::team::enemy))if(distance(pos,point(enemy.position()))<=enemy.attack_range()+2)return false;
        for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake()&&distance(pos,point(camp.position()))<=camp.attack_range()+1)return false;
        return true;
    }
    std::optional<p6::Point> escape(const sdk::Snapshot& snapshot) const {
        const auto origin=snapshot.frame.own.actor.position;const auto camps=cambc::vision::camps();const auto enemies=cambc::vision::champions(cambc::team::enemy);
        auto incoming=[&](p6::Point p) {double damage=0;
            for(const auto& camp:camps)if(camp.is_alive()&&camp.is_awake()&&distance(p,point(camp.position()))<=camp.attack_range())damage+=camp.strength();
            for(const auto& enemy:enemies) {
                if(distance(p,point(enemy.position()))<=enemy.attack_range()+1) {
                    double cast=0;for(const auto& ability:enemy.abilities())if(ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))cast=std::max(cast,double(ability.damage_amount()));
                    damage+=enemy.strength()+cast;
                }
            }
            return damage;
        };
        double best=incoming(origin);std::optional<p6::Point> chosen;
        const auto occupied=cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
        for(int y=-1;y<=1;++y) {
            for(int x=-1;x<=1;++x) {
                const p6::Point at{origin.x+x,origin.y+y};
                if((!x&&!y)||at.x<0||at.y<0||at.x>=cambc::map::width()||at.y>=cambc::map::height()||routes_.terrain().is_set(tile(at))||occupied.is_set(tile(at)))continue;
                const double risk=incoming(at);if(risk<best&&risk<snapshot.own.health){best=risk;chosen=at;}
            }
        }
        return chosen;
    }
    bool productive(const sdk::Snapshot& snapshot) const {
        if(renewal_<0)return false;const auto live=group(routes_.sites[renewal_],snapshot.frame.clock);
        return live.original_spawn_visible&&(live.lifecycle==p6::Lifecycle::living||live.lifecycle==p6::Lifecycle::reward_available);
    }
    std::array<p6::CycleCertificate,p6::max_actors> safety(const sdk::Snapshot& snapshot) const {
        auto out=initial_safety_;for(int c=0;c<initial_safety_count_;++c) {
            out[c].clock=snapshot.frame.clock;if(const auto* actor=snapshot.frame.visible(out[c].actor))
                for(auto* e:{&out[c].clear,&out[c].hold,&out[c].bank,&out[c].recovery}){e->health=actor->health;e->max_health=actor->max_health;}
        }return out;
    }
    void dispatch(const Command& command,const LocalFrame& local) {
        if(command.clock!=local.frame.clock||int(cambc::round_number())!=command.clock.round)return;
        const auto receipt=adapter_.dispatch(command,local,work_);
        if(receipt.current_clock!=command.clock){fault();return;}
        if(configured_)execution_.confirm(*input_,*protocol_,command,receipt);else work_.confirm(command,receipt);
        if(configured_&&protocol_->bound(work_)&&protocol_->action_token&&!protocol_->response_serial&&!protocol_->initial_observed) {
            // This newer personal snapshot is deliberately AFTER confirmation
            // of the newly issued Work's own productive empty-bag attack.
            auto after=adapter_.observe(&routes_.sites[selected_].group,&work_);
            if(!fresh(after,16000)||after.frame.clock.round!=command.clock.round){fault();return;}
            populate(after);const auto certificates=safety(after);
            auto observed=adapter_.local(after,work_.plan,{certificates.data(),std::size_t(initial_safety_count_)},true,calm(after),productive(after),escape(after),&work_);
            if(fresh(after,14000))protocol_->observe(*input_,observed,work_);else fault();
        }
    }
    void manual(const Plan& plan,const LocalFrame& local,PhysicalAction action,p6::Point target={},Leg leg=Leg::ingress) {
        work_.plan=plan;work_.route_witness_active=false;const auto command=work_.emit(local,action,target,leg);
        // Securing old inventory is not a newly selected ordinary encounter.
        if(command.clock!=local.frame.clock||int(cambc::round_number())!=command.clock.round)return;
        const auto receipt=adapter_.dispatch(command,local,work_);if(receipt.current_clock!=command.clock){fault();return;}work_.confirm(command,receipt);
    }
    void fallback(sdk::Snapshot& snapshot) {
        const bool safe=calm(snapshot);Plan plan;plan.collector=snapshot.frame.clock.self;
        plan.group=inspection_site_>=0?group_id(routes_.sites[inspection_site_].group):-1;
        if(safe&&snapshot.frame.own.actor.channel!=p6::Channel::none&&snapshot.frame.own.actor.channel_valid) {
            auto local=adapter_.local(snapshot,plan,{},safe,safe,false);manual(plan,local,PhysicalAction::preserve_channel);return;
        }
        if(safe&&snapshot.own.bag>0) {
            int best=-1;Route path;for(int b=0;b<int(routes_.banks.size());++b)if(unlocked(b)) {
                const auto current=routes_.route(snapshot.frame.own.actor.position,routes_.banks[b].approach);
                if(route_valid(current)&&(best<0||current.count<path.count)){best=b;path=current;}
            }
            if(best>=0) {
                plan.altar=sdk::altar_id(routes_.banks[best].altar);plan.bank=path;
                auto local=adapter_.local(snapshot,plan,{},safe,safe,false);const bool arrived=path.count==1;
                manual(plan,local,arrived?PhysicalAction::tribute:PhysicalAction::move,arrived?p6::Point{}:path.tiles[1],Leg::bank);return;
            }
        }
        const int now=snapshot.frame.clock.round;
        if(safe&&now>=next_scan_&&inspection_turns_<8) {
            if(inspection_site_<0)for(int s=0;s<int(routes_.sites.size());++s) {
                const auto path=routes_.route(snapshot.frame.own.actor.position,routes_.sites[s].approach);
                if(route_valid(path)&&(inspection_site_<0||path.count<inspection_.count)){inspection_site_=s;inspection_=path;inspection_cursor_=0;}
            }
            if(inspection_site_>=0&&route_valid(inspection_)) {
                plan.ingress=inspection_;plan.group=group_id(routes_.sites[inspection_site_].group);const auto pos=snapshot.frame.own.actor.position;
                if(inspection_cursor_+1<inspection_.count&&pos==inspection_.tiles[inspection_cursor_+1])++inspection_cursor_;
                if(pos==inspection_.tiles[inspection_cursor_]) {
                    ++inspection_turns_;auto local=adapter_.local(snapshot,plan,{},safe,safe,false);const bool arrived=inspection_cursor_+1==inspection_.count;
                    manual(plan,local,arrived?PhysicalAction::inspect:PhysicalAction::move,arrived?p6::Point{}:inspection_.tiles[inspection_cursor_+1]);return;
                }
            }inspection_turns_=8;
        }
        if(inspection_turns_>=8){if(!unavailable_logged_){std::cout<<"p8-16 OPPORTUNITY_UNAVAILABLE\n";unavailable_logged_=true;}next_scan_=now+8;inspection_turns_=0;inspection_site_=-1;}
        const auto exit=escape(snapshot);auto local=adapter_.local(snapshot,plan,{},safe,safe,false,exit);
        manual(plan,local,snapshot.frame.own.at_spawn?PhysicalAction::recover:!safe&&exit?PhysicalAction::escape:safe?PhysicalAction::recall:PhysicalAction::yield,exit.value_or(p6::Point{}));
    }
    void populate(const sdk::Snapshot& snapshot) {
        auto& in=*input_;in.frame=snapshot.frame;in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        if(in.settings_identity.source!=p6::Source::queried_setting) {
            in.settings=sdk::settings();
            in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
        }
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        bool bounded=false;const auto threat=rival_regime(bounded);in.threat_regime={int(threat&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const auto enemy=cambc::global_submitted_gems(cambc::team::enemy);scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        if(configured_){in.layout=*layout_;in.target_group=layout_->supported.group;}
        else {in.target_group=0;if(!in.offer_count){in.offer_count=1;in.independent=0;}}
        if(rival_<0)for(int a=0;a<snapshot.frame.actor_count;++a) {
            const auto& seen=snapshot.frame.actors[a].value;if(seen.team==p6::Team::enemy&&seen.alive&&(rival_<0||seen.id<rival_))rival_=seen.id;
        }
        in.rival=rival_;in.distance=distances_.read(in.frame,rival_);(void)bounded;
        if(configured_&&protocol_->initial_approach&&fresh(snapshot)&&finite_counterprotocol::sample_failure(in.frame,layout_->site,in.rival,in.distance)!=finite_counterprotocol::ResponseFailure::none)
            protocol_->responses.interrupted(in.frame.clock.round);
    }
    std::unique_ptr<Solo> best(const p6::Frame& frame,std::span<const int> excluded={}) {
        std::unique_ptr<Solo> out;p6::ScoreEvaluation rank;auto windows=input_->windows;windows.sampled_actor_id=frame.clock.self;
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>55000;++s) {
            if(std::find(excluded.begin(),excluded.end(),group_id(routes_.sites[s].group))!=excluded.end()||!exclusive(s,frame.clock.self))continue;
            for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>55000;++b)if(unlocked(b)) {
                auto candidate=std::unique_ptr<Solo>(new Solo(solo(routes_,s,b,frame,false)));if(!candidate->feasible)continue;
                const auto rank_now=p6::evaluate_schedule(candidate->model.schedule,windows,input_->settings,{frame.clock.round,frame.clock.self},frame.own_team_score);
                if(rank_now.eligible()&&(!out||p6::compare(rank_now,rank)>0)){rank=rank_now;out=std::move(candidate);}
            }
        }return out;
    }
    std::vector<Solo> alternatives(const sdk::Snapshot& snapshot,const Solo& own,bool fixed=false) {
        std::vector<Solo> out;out.reserve(3);out.push_back(own);normalize(out.back(),snapshot.frame);
        if(snapshot.frame.own.actor.pick_ordinal>=3)return out;
        for(int ordinal=0;ordinal<3&&ordinal<snapshot.draft.picks;++ordinal) {
            const int id=snapshot.draft.actors[ordinal];if(id==snapshot.frame.clock.self)continue;const auto* seen=snapshot.frame.visible(id);
            if(!seen||!seen->alive||seen->channel!=p6::Channel::none)return {};
            std::unique_ptr<Solo> candidate;
            if(fixed&&roles_.ready) {
                const int source=ordinal==0?roles_.low:ordinal==1?roles_.income:roles_.helper;
                if(!exclusive(source,id))return {};
                candidate=model(routes_,roles_,source,ordinal==1?other_bank_:bank_,subject(snapshot.frame,*seen));
            } else {
                std::array<int,3> used{};int count=0;for(const auto& previous:out)used[count++]=previous.plan.group;
                candidate=best(subject(snapshot.frame,*seen),{used.data(),std::size_t(count)});
            }
            if(!candidate||!candidate->feasible)return {};
            for(const auto& previous:out)if(previous.plan.group==candidate->plan.group)return {};
            normalize(*candidate,snapshot.frame);out.push_back(std::move(*candidate));
        }return out;
    }
    bool members(const sdk::Snapshot& snapshot,std::array<Member,2>& out) const {
        for(int m=0;m<2;++m) {
            const int ordinal=m?2:0,id=snapshot.draft.actors[ordinal];const auto* seen=snapshot.frame.visible(id);if(!seen||!seen->alive)return false;
            if(id==snapshot.frame.clock.self){out[m]={id,snapshot.own};continue;}
            bool found=false;
            for(const auto& ally:cambc::vision::champions(cambc::team::ally)) {
                if(sdk::identity(ally)==id) {
                    const auto stats=ally.base_stats();out[m]={id,{double(ally.health()),double(ally.max_health()),double(ally.strength()),double(ally.focus()),double(stats.strength_growth),std::int64_t(ally.gem_count()),sdk::equipment_identity(ally)}};
                    found=valid_numeric_state(out[m].state);break;
                }
            }
            if(!found)return false;
        }return true;
    }
    bool frozen_members(const sdk::Snapshot& snapshot) const {
        std::array<Member,2> now{};if(!members(snapshot,now))return false;
        for(int m=0;m<2;++m) {
            const auto& old=members_[m].state;const auto& actual=now[m].state;const int ordinal=m?2:0;
            const bool focus=snapshot.draft.champions[ordinal]==p6::Champion::prism?actual.focus>=old.focus&&actual.focus<=focus_upper(old.focus,actual.bag):actual.focus==old.focus;
            if(now[m].actor!=members_[m].actor||actual.equipment_hash!=old.equipment_hash||actual.max_health!=old.max_health||actual.strength!=old.strength||actual.growth!=old.growth||!focus)return false;
        }return true;
    }
    bool member_regime(const sdk::Snapshot& snapshot) const {
        return (work_.stage!=PhysicalStage::travel&&work_.stage!=PhysicalStage::assemble&&work_.stage!=PhysicalStage::clear)||frozen_members(snapshot);
    }
    bool configure(const sdk::Snapshot& snapshot);
    bool menu(const sdk::Snapshot& snapshot,const Solo& own);
    bool forecast(const sdk::Snapshot& snapshot,const Offer& prefix);
    bool unchanged(const sdk::Snapshot& before,const sdk::Snapshot& after) const {
        if(!after.valid||before.frame.clock.round!=after.frame.clock.round||before.frame.clock.self!=after.frame.clock.self||
            before.own.equipment_hash!=after.own.equipment_hash||before.own.health!=after.own.health||before.own.max_health!=after.own.max_health||
            before.own.strength!=after.own.strength||before.own.focus!=after.own.focus||before.own.growth!=after.own.growth||before.own.bag!=after.own.bag||
            before.frame.own.gold!=after.frame.own.gold||before.frame.own.actor.position!=after.frame.own.actor.position)return false;
        for(int o=0;o<input_->offer_count;++o) {
            for(int a=0;a<input_->offers[o].schedule.actor_count;++a) {
                const int id=input_->offers[o].schedule.actors[a].actor_id;const auto* x=before.frame.visible(id);const auto* y=after.frame.visible(id);
                if(!x||!y||x->health!=y->health||x->max_health!=y->max_health||x->strength!=y->strength||x->bag!=y->bag||x->alive!=y->alive||x->position!=y->position||x->channel!=y->channel)return false;
            }
        }
        return true;
    }
    Decision decision() {
        if(configured_)return probe_reveal_commit::decide(*input_,*protocol_,*ledger_,arm);
        auto out=probe_reveal_commit::ordinary(*input_);return out;
    }
    Command advance(const LocalFrame& local) {
        // The intervention keeps its entry-licensed represented transition.
        // A new HP band caused by its own charged damage is not a new entry.
        if(configured_&&work_.active&&(protocol_->bound(work_)||(work_.plan.organization==Organization::H&&work_.plan.collector==input_->frame.clock.self))) {
            const auto& state=input_->own_state.value;const auto& d=protocol_->organization;
            if(!d.envelope.contains(state,d.key.build)||state.equipment_hash!=d.key.build.equipment_hash||input_->settings_identity.value!=d.key.settings_id||input_->threat_regime.value!=d.key.threat_regime) {
                if(protocol_->bound(work_))protocol_->intervention_failed=true;
                protocol_->responses.interrupted(local.frame.clock.round);work_.request_stop();
            }
            if(protocol_->bound(work_)){protocol_->limit_before_step(local,work_);return work_.step(local);}
        }
        return configured_?probe_reveal_commit::advance(*input_,local,*protocol_,*ledger_,arm,execution_):work_.step(local);
    }
    void observed(const LocalFrame& local) {
        if(int(cambc::round_number())!=local.frame.clock.round||cambc::fuel_remaining()<12000){fault();return;}
        if(work_.cycle_completed&&work_.payout_seen&&work_.bank_seen&&work_.renewal_seen&&!first_income_){first_income_=true;std::cout<<"p8-16 ORDINARY_INCOME group="<<work_.plan.group<<'\n';}
        if(configured_) {
            // A post-attack baseline was already taken on a fresh revision.
            // Do not feed its older pre-action snapshot back to the sampler.
            if(!protocol_->action_token||local.frame.clock.round>protocol_->action_clock.round||protocol_->initial_observed)protocol_->observe(*input_,local,work_);
            execution_.trace.observe(local,work_,{});
            if(!work_.active||!ledger_->active()) {
                auto known=all_premises;
                if(work_.failure==PhysicalFailure::missing_member)known&=~PremiseMask(membership_known);
                const auto result=execution_.trace.finish(*ledger_,local.frame.clock.round,{execution_.issued,input_->own_state.value,known});
                if(result.accepted)std::cout<<"p8-16 EPISODE positive="<<result.full_positive<<" contradiction="<<result.contradiction<<'\n';
            }
            if(protocol_->responses.encounters()!=reported_){reported_=protocol_->responses.encounters();const auto& event=protocol_->responses.window.record;
                std::cout<<"p8-16 RESPONSE serial="<<event.serial<<" label="<<int(event.response)<<" failure="<<int(event.failure)<<" baseline="<<event.baseline_round<<" end="<<event.terminal_round<<'\n';}
        }
        if(work_.plan.organization==Organization::H&&!proof_logged_){proof_logged_=true;std::cout<<"p8-16 PROOF_UNAVAILABLE peer_actual_attack_attribution\n";}
    }
public:
    void fault(){work_.request_stop();work_.route_witness_active=false;if(protocol_)protocol_->responses.interrupted(int(cambc::round_number()));if(ledger_&&ledger_->active())ledger_->invalidate_active(int(cambc::round_number()));}
    void turn();
};
inline bool Runtime::configure(const sdk::Snapshot& snapshot) {
    if(configured_)return true;
    if(!roles_.ready||bank_<0||other_bank_<0||snapshot.draft.picks<3||(snapshot.frame.own.actor.pick_ordinal!=0&&snapshot.frame.own.actor.pick_ordinal!=2)||snapshot.own.bag)return false;
    if(routes_.sites[roles_.value].rally_count<2)return false;
    const auto* owner=snapshot.frame.visible(snapshot.draft.actors[0]);const auto* helper=snapshot.frame.visible(snapshot.draft.actors[2]);
    const auto* producer=snapshot.frame.visible(snapshot.draft.actors[1]);
    if(!owner||!helper||!producer||!owner->alive||!helper->alive||!producer->alive||owner->bag||helper->bag||producer->bag||
        owner->channel!=p6::Channel::none||helper->channel!=p6::Channel::none||producer->channel!=p6::Channel::none||
        helper->position!=routes_.sites[roles_.value].rally[1].destination)return false;
    const bool owner_entry=owner->position==routes_.sites[roles_.general].approach.destination;
    const bool visible_proposal=snapshot.frame.clock.self==helper->id&&owner->position==routes_.sites[roles_.value].rally[0].destination;
    if(!owner_entry&&!visible_proposal)return false;
    const auto* enemy=snapshot.frame.visible(rival_);
    if(arm==Arm::active&&snapshot.frame.clock.self==snapshot.draft.actors[0]&&(!enemy||!enemy->alive||enemy->team!=p6::Team::enemy))return false;
    auto frame=subject(snapshot.frame,*owner);
    // A helper may reconstruct the public frozen collector ingress only after
    // seeing its actual named proposal. This position is geometry, not a peer
    // observation, private qualification, income receipt or current H forecast.
    if(visible_proposal)frame.own.actor.position=routes_.sites[roles_.general].approach.destination;
    auto low=model(routes_,roles_,roles_.low,bank_,frame);
    auto value=model(routes_,roles_,roles_.value,bank_,frame);
    auto general=model(routes_,roles_,roles_.general,bank_,frame);
    if(!low||!value||!general)return false;general->plan.organization=Organization::G;
    std::vector<Solo> models;models.reserve(3);models.push_back(*value);
    auto income=model(routes_,roles_,roles_.income,other_bank_,subject(snapshot.frame,*producer));
    auto helper_model=model(routes_,roles_,roles_.helper,bank_,subject(snapshot.frame,*helper));
    if(!income||!helper_model)return false;models.push_back(*income);models.push_back(*helper_model);
    for(auto& m:models){if(!m.feasible)return false;normalize(m,snapshot.frame);}
    auto group_model=std::make_unique<Coalition>(coalition(routes_,roles_.value,bank_,snapshot.frame,snapshot.draft,models,Organization::H,false));
    if(!group_model->feasible||!retarget_supported(group_model->offer,routes_,roles_,bank_,snapshot.frame))return false;
    auto candidate=std::make_unique<Layout>();auto independent=std::make_unique<Offer>();
    models[0]=*low;if(!independent_offer(*independent,*low,models,frame))return false;candidate->intervention=independent->plan;
    models[0]=*value;if(!independent_offer(*independent,*value,models,frame))return false;candidate->independent=independent->plan;
    models[0]=*general;if(!independent_offer(*independent,*general,models,frame))return false;candidate->generalist=independent->plan;
    models[0]=*value;
    candidate->supported=formation_plan(routes_,roles_.value,bank_,frame,snapshot.draft,Organization::H,false,&models[0]);
    candidate->supported.renewal=value->plan.renewal;
    const int own_index=p6::actor_index(group_model->offer.schedule,owner->id),helper_index=p6::actor_index(group_model->offer.schedule,helper->id);
    if(own_index<0||helper_index<0)return false;
    candidate->supported.forecast_rounds=int(group_model->offer.schedule.actors[own_index].occupied_turns.value);
    candidate->helper=formation_plan(routes_,roles_.value,bank_,subject(snapshot.frame,*helper),snapshot.draft,Organization::H,false,&models[2]);
    candidate->helper.forecast_rounds=int(group_model->offer.schedule.actors[helper_index].occupied_turns.value);
    if(!named_site(candidate->site,routes_,roles_.value,bank_,distances_)||!candidate->valid(snapshot.draft))return false;
    std::array<Member,2> present{};if(!members(snapshot,present))return false;
    input_->layout=*candidate;input_->target_group=candidate->supported.group;
    const auto d=snapshot.frame.clock.self==snapshot.draft.actors[0]?declaration(*candidate,*input_,std::max(routes_.sites[roles_.low].group.gem_value(),routes_.sites[roles_.value].group.gem_value())):ContextDeclaration{};
    if(!fresh(snapshot,26000)||!protocol_->declare(*candidate,d,snapshot.draft,*ledger_))return false;
    *layout_=*candidate;members_=present;configured_=true;
    std::cout<<"p8-16 DECLARED low="<<layout_->intervention.group<<" valuable="<<layout_->supported.group<<" fallback="<<layout_->generalist.group<<" paid_disabled=1\n";
    return true;
}
inline bool Runtime::forecast(const sdk::Snapshot& snapshot,const Offer& prefix) {
    auto& in=*input_;in.forecast={};
    if(snapshot.frame.clock.self!=snapshot.draft.actors[0]||protocol_->issued||!same_plan(prefix.plan,layout_->intervention)||!frozen_members(snapshot)||
        !common::guarded_key_current(in,protocol_->organization.key)||
        finite_counterprotocol::sample_failure(snapshot.frame,layout_->site,rival_,in.distance)!=finite_counterprotocol::ResponseFailure::none)return false;
    const auto* protected_certificate=common::certificate(prefix,snapshot.draft.actors[1]);
    const auto* helper_certificate=common::certificate(prefix,snapshot.draft.actors[2]);
    if(!protected_certificate||!helper_certificate||protected_certificate->group!=group_id(routes_.sites[roles_.income].group)||
        helper_certificate->group!=group_id(routes_.sites[roles_.helper].group))return false;
    for(const int s:{roles_.low,roles_.value,roles_.general})if(!full_work(routes_.sites[s]))return false;
    const auto* helper=snapshot.frame.visible(snapshot.draft.actors[2]);const auto* producer=snapshot.frame.visible(snapshot.draft.actors[1]);
    if(!helper||!producer||helper->bag||helper->channel!=p6::Channel::none||helper->position!=routes_.sites[roles_.value].rally[1].destination||
        snapshot.frame.own.actor.position!=routes_.sites[roles_.general].approach.destination)return false;
    const int pi=p6::actor_index(prefix.schedule,snapshot.frame.clock.self),hi=p6::actor_index(prefix.schedule,helper->id);if(pi<0||hi<0)return false;
    const auto* own_certificate=common::certificate(prefix,snapshot.frame.clock.self);if(!own_certificate)return false;
    const double after_prefix=std::max(0.,snapshot.own.health-own_certificate->recovery.incoming_total);
    if(after_prefix<=0||health_band(after_prefix,snapshot.own.max_health)!=protocol_->organization.key.hp_band)return false;
    int not_before=0;bool owned=false,helper_payout=false;
    for(int e=0;e<prefix.schedule.actors[pi].event_count;++e)if(prefix.schedule.actors[pi].events[e].kind==p6::EventKind::production) {
        const auto& event=prefix.schedule.actors[pi].events[e];const auto& p=prefix.schedule.productions[event.production_index];
        in.forecast.lowload_event=p.event_id;
        in.forecast.lowload_gems={routes_.sites[roles_.low].group.gem_value(),p6::Source::public_map};
        in.forecast.lowload_gold={routes_.sites[roles_.low].group.gold_value(),p6::Source::public_map};owned=true;
    }
    for(int e=0;e<prefix.schedule.actors[hi].event_count;++e)if(prefix.schedule.actors[hi].events[e].kind==p6::EventKind::production) {
        // Wait a full queried respawn AFTER its complete prefix: this also
        // bounds actual vacating of the captured source before the timer can
        // run. Charge this in every branch despite no second helper reward.
        not_before=int(prefix.schedule.actors[hi].occupied_turns.value)+routes_.sites[roles_.helper].group.max_respawn_timer()+1;helper_payout=true;
    }
    if(!owned||!helper_payout)return false;
    const int bound=std::min(common::forecast_window,int(cambc::constants::max_rounds)-snapshot.frame.clock.round);
    for(int b=0;b<3&&cambc::fuel_remaining()>42000;++b) {
        auto own=model(routes_,roles_,b==2?roles_.general:roles_.value,bank_,snapshot.frame);
        if(!own)return false;if(b==2)own->plan.organization=Organization::G;
        auto models=alternatives(snapshot,*own,true);if(models.size()!=3)return false;
        const Solo* helper_alternative=nullptr;for(const auto& candidate:models)if(candidate.plan.collector==helper->id)helper_alternative=&candidate;
        if(!helper_alternative||route_hash(helper_alternative->plan.ingress)!=route_hash(layout_->helper.release)||
            helper_alternative->plan.ingress.tiles[0]!=routes_.sites[roles_.value].rally[1].destination||
            layout_->helper.release.tiles[layout_->helper.release.count-1]!=routes_.sites[roles_.helper].approach.destination)return false;
        auto next=std::make_unique<Offer>();
        if(b==0) {
            auto h=std::make_unique<Coalition>(coalition(routes_,roles_.value,bank_,snapshot.frame,snapshot.draft,models,Organization::H,false));
            if(!h->feasible||!retarget_supported(h->offer,routes_,roles_,bank_,snapshot.frame)||!frozen_offer(h->offer,layout_->supported,snapshot.frame))return false;
            *next=h->offer;
        } else if(!independent_offer(*next,*own,models,snapshot.frame)||!frozen_offer(*next,b==1?layout_->independent:layout_->generalist,snapshot.frame))return false;
        double target_exposure=0;for(const auto& camp:routes_.sites[roles_.value].group)target_exposure+=std::max(double(camp.base_stats().base_strength),double(camp.strength()));
        if(!branch(in.forecast.branches[b],in.forecast.safety[b],in.forecast.safety_count[b],prefix,*next,in,bound,not_before,
            layout_->helper.release,group_id(routes_.sites[roles_.helper].group),target_exposure,
            in.forecast.continuation_start[b],in.forecast.lowload_end[b],in.forecast.continuation_event[b]))return false;
        if(b==0) {
            const int actor=p6::actor_index(in.forecast.branches[b],snapshot.frame.clock.self);
            // Even the earliest possible confirmed baseline takes three next
            // observations. Do not promise a continuation already outside its
            // initial <64-round permission by that conservative endpoint.
            if(actor<0||in.forecast.branches[b].actors[actor].occupied_turns.value>=67)return false;
        }
        const int source=b==2?roles_.general:roles_.value;
        in.forecast.continuation_gems[b]={routes_.sites[source].group.gem_value(),p6::Source::public_map};
        in.forecast.continuation_gold[b]={routes_.sites[source].group.gold_value(),p6::Source::public_map};
    }
    if(!fresh(snapshot,24000))return false;
    for(int b=0;b<3;++b)if(in.forecast.continuation_event[b]<0)return false;
    in.forecast.endpoint_distinguishes=true;in.forecast.marked_extra_turns=0;return true;
}
inline bool Runtime::menu(const sdk::Snapshot& snapshot,const Solo& own) {
    auto& in=*input_;in.offer_count=0;in.independent=0;in.intervention=-1;in.own_income={};in.collector_proposal={};in.forecast={};
    auto baseline=alternatives(snapshot,own);if(baseline.empty()){baseline.push_back(own);normalize(baseline.back(),snapshot.frame);}
    if(!independent_offer(in.offers[in.offer_count],own,baseline,snapshot.frame))return false;++in.offer_count;
    const int ordinal=snapshot.frame.own.actor.pick_ordinal;
    if(roles_.ready&&ordinal<3&&bank_>=0&&other_bank_>=0) {
        const int source=ordinal==0?roles_.low:ordinal==1?roles_.income:roles_.helper;
        auto retained=model(routes_,roles_,source,ordinal==1?other_bank_:bank_,snapshot.frame);
        if(retained) {
            auto fixed=alternatives(snapshot,*retained,true);
            if(fixed.size()==3&&independent_offer(in.offers[in.offer_count],*retained,fixed,snapshot.frame))++in.offer_count;
        }
    }
    configure(snapshot);if(!configured_||ordinal==1||ordinal>=3)return true;in.layout=*layout_;
    int low=-1;
    if(ordinal==0)for(const int source:{roles_.low,roles_.value,roles_.general}) {
        if(in.offer_count>=common::max_options||!unlocked(bank_)||!exclusive(source,snapshot.frame.clock.self))continue;
        auto candidate=model(routes_,roles_,source,bank_,snapshot.frame);if(!candidate)continue;
        if(source==roles_.general)candidate->plan.organization=Organization::G;
        auto models=alternatives(snapshot,*candidate,true);if(models.size()!=3)continue;
        auto& offer=in.offers[in.offer_count];if(!independent_offer(offer,*candidate,models,snapshot.frame))continue;
        const auto& plan=source==roles_.low?layout_->intervention:source==roles_.value?layout_->independent:layout_->generalist;
        if(!frozen_offer(offer,plan,snapshot.frame))continue;
        if(source==roles_.low)low=in.offer_count;++in.offer_count;
    }
    if(ordinal==0&&!protocol_->issued&&low>=0) {
        // No native paid intervention. The complete unchanged productive menu
        // must already select this exact low-load job before it may elicit.
        const auto unchanged=probe_reveal_commit::ordinary(in);
        if(unchanged.option>=0&&same_plan(in.offers[unchanged.option].plan,layout_->intervention)&&forecast(snapshot,in.offers[unchanged.option]))in.intervention=unchanged.option;
    }
    if(in.offer_count>=common::max_options||(arm==Arm::active&&ordinal==2&&helper_starts_>=2)||
        (arm==Arm::active&&ordinal==0&&protocol_->issued&&!protocol_->ongoing_response_current(snapshot.frame.clock.round))||cambc::fuel_remaining()<42000||!unlocked(bank_)||
        !exclusive(roles_.value,snapshot.frame.clock.self,true)||!frozen_members(snapshot))return true;
    auto ordinary_model=model(routes_,roles_,ordinal==0?roles_.value:roles_.helper,bank_,snapshot.frame);if(!ordinary_model)return true;
    auto models=alternatives(snapshot,*ordinary_model,true);if(models.size()!=3)return true;
    auto supported=std::make_unique<Coalition>(coalition(routes_,roles_.value,bank_,snapshot.frame,snapshot.draft,models,Organization::H,false));
    if(!supported->feasible||!retarget_supported(supported->offer,routes_,roles_,bank_,snapshot.frame)||
        !frozen_offer(supported->offer,ordinal==0?layout_->supported:layout_->helper,snapshot.frame))return true;
    const auto ordinary_choice=probe_reveal_commit::ordinary(in);
    // The nominal control also needs a presently valuable full same-actor
    // deployment. Feasibility alone is not a free economic benefit.
    if(ordinary_choice.option<0||!protected_same(supported->offer.schedule,in.offers[ordinary_choice.option].schedule,snapshot.draft.actors[1])||
        p6::compare(common::evaluate(in,supported->offer.schedule),ordinary_choice.score)<=0)return true;
    if(ordinal==0){supported->offer.entry=protocol_->organization.key;supported->offer.context_slot=protocol_->slot;}
    else {
        const auto* collector=snapshot.frame.visible(snapshot.draft.actors[0]);
        if(!collector||!collector->alive||collector->bag||collector->channel!=p6::Channel::none||collector->position!=layout_->supported.pads[0])return true;
        in.collector_proposal={layout_->supported.group,snapshot.frame.clock,collector->id,p6::Knowledge::exact};
    }
    in.offers[in.offer_count++]=supported->offer;return true;
}
inline void Runtime::turn() {
    // Shared melee-reachable geometry conservatively forgoes ranged distance.
    const int startup_round=int(cambc::round_number());
    const auto startup_room=[&](std::uint64_t reserve) {
        return int(cambc::round_number())==startup_round&&cambc::fuel_remaining()>=reserve;
    };
    if(!routes_.initialized()) {
        if(!startup_room(70000))return;
        if(!diagnostic_routes_started_&&startup_room(32000)) {
            std::printf("p8-16 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                startup_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
            diagnostic_routes_started_=true;
        }
        if(!startup_room(70000))return;
        const bool complete=routes_.initialize_step(1);
        if(complete&&startup_room(32000))
            std::printf("p8-16 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                startup_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
        return;
    }
    if(!work_.active&&startup_room(70000))routes_.prepare();
    if(!startup_room(30000))return;
    auto snapshot=adapter_.observe(selected_<0?nullptr:&routes_.sites[selected_].group,&work_);if(!snapshot.valid){fault();return;}
    if(!ledger_)ledger_=std::make_unique<EvidenceLedger>(snapshot.frame.clock.self);
    if(!protocol_)protocol_=std::make_unique<Protocol>(snapshot.frame.clock.self);
    const bool skipped=last_round_>=0&&snapshot.frame.clock.round!=last_round_+1;
    if(snapshot.frame.clock.round==last_round_)return;last_round_=snapshot.frame.clock.round;populate(snapshot);
    if(!snapshot.frame.own.actor.alive||skipped)protocol_->responses.interrupted(snapshot.frame.clock.round);
    if(work_.active) {
        const auto certificates=safety(snapshot);bool safe=false,bounded=false;
        for(int c=0;c<initial_safety_count_;++c)if(certificates[c].actor==snapshot.frame.clock.self)safe=certificates[c].valid(snapshot.frame,snapshot.frame.clock.self,work_.plan.group);
        safe&=physical_regime(work_.plan.group,bounded)==physical_&&bounded;
        auto local=adapter_.local(snapshot,work_.plan,{certificates.data(),std::size_t(initial_safety_count_)},safe,calm(snapshot),
            productive(snapshot),escape(snapshot),&work_);
        local.fuel_resumed|=skipped;if(skipped)fault();
        if(work_.plan.organization==Organization::H&&!member_regime(snapshot))work_.request_stop();
        if(!fresh(snapshot)){fault();return;}
        const auto command=advance(local);dispatch(command,local);
        if(local.frame.clock==input_->frame.clock)observed(local);return;
    }
    if(snapshot.frame.own.actor.channel!=p6::Channel::none||snapshot.own.bag>0||!snapshot.frame.own.actor.alive){fallback(snapshot);return;}
    bool bounded=false;rival_regime(bounded);if(!bounded){protocol_->responses.interrupted(snapshot.frame.clock.round);fallback(snapshot);return;}
    if(!roles_.ready)roles_=slots(routes_);
    if(roles_.ready&&bank_<0) {
        bank_=finite_counterprotocol::native::public_bank(routes_,roles_.value);
        if(bank_>=0)for(int b=0;b<int(routes_.banks.size());++b)if(b!=bank_&&routes_.banks[b].approach.destination!=routes_.banks[bank_].approach.destination&&unlocked(b)){other_bank_=b;break;}
        if(other_bank_<0)bank_=-1;
    }
    auto own=best(snapshot.frame);if(!own||!menu(snapshot,*own)){fallback(snapshot);return;}
    if(!fresh(snapshot)){fault();return;}auto chosen=decision();
    // A lost exact response opportunity never removes real independent income.
    // This fallback receives no protocol permission, episode or response spend.
    bool ordinary_fallback=chosen.option<0;
    if(ordinary_fallback) {
        chosen=probe_reveal_commit::ordinary(*input_);
        if(chosen.option>=0&&configured_&&snapshot.frame.own.actor.pick_ordinal==0&&protocol_->issued)
            input_->offers[chosen.option].plan.organization=Organization::G;
    }
    if(chosen.option<0){fallback(snapshot);return;}
    const auto before=snapshot;const int action_group=input_->offers[chosen.option].plan.group;int source=-1;
    for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)source=s;
    if(source<0)return;snapshot=adapter_.observe(&routes_.sites[source].group,&work_);
    if(!unchanged(before,snapshot)){fault();return;}populate(snapshot);
    for(int o=0;o<input_->offer_count;++o)for(int c=0;c<input_->offers[o].safety_count;++c)input_->offers[o].safety[c].clock=snapshot.frame.clock;
    for(int b=0;b<3;++b)for(int c=0;c<input_->forecast.safety_count[b];++c)input_->forecast.safety[b][c].clock=snapshot.frame.clock;
    if(input_->collector_proposal.knowledge==p6::Knowledge::exact)input_->collector_proposal.clock=snapshot.frame.clock;
    if(!fresh(snapshot)){fault();return;}
    chosen=ordinary_fallback?probe_reveal_commit::ordinary(*input_):decision();
    if(chosen.option<0||input_->offers[chosen.option].plan.group!=action_group)return;auto& offer=input_->offers[chosen.option];
    if(offer.plan.organization==Organization::H&&(!frozen_members(snapshot)||
        (arm==Arm::active&&snapshot.frame.own.actor.pick_ordinal==0&&!protocol_->ongoing_response_current(snapshot.frame.clock.round))))return;
    auto local=adapter_.local(snapshot,offer.plan,{offer.safety.data(),std::size_t(offer.safety_count)},true,calm(snapshot),true,escape(snapshot));
    if(!fresh(snapshot)){fault();return;}
    Admission admission;
    if(configured_&&!ordinary_fallback)admission=probe_reveal_commit::commit(*input_,local,chosen,*protocol_,*ledger_,arm,execution_);
    else {admission.started=physical_admissible(offer.plan,local)&&work_.start(offer.plan,local);execution_.ordinary=true;}
    if(!admission.started){fallback(snapshot);return;}
    if(offer.plan.organization==Organization::H&&snapshot.frame.own.actor.pick_ordinal==2)++helper_starts_;
    selected_=source;renewal_=offer.plan.organization==Organization::H&&snapshot.frame.clock.self!=offer.plan.collector?roles_.helper:
        roles_.ready&&offer.plan.renewal.tiles[offer.plan.renewal.count-1]==routes_.sites[roles_.general].approach.destination?roles_.general:
        roles_.ready&&snapshot.frame.own.actor.pick_ordinal==2&&offer.plan.renewal.tiles[offer.plan.renewal.count-1]==routes_.sites[roles_.value].rally[1].destination?roles_.value:source;
    initial_safety_=offer.safety;initial_safety_count_=offer.safety_count;
    physical_=physical_regime(action_group,bounded);inspection_turns_=0;inspection_site_=-1;unavailable_logged_=false;
    std::cout<<"p8-16 ADMIT group="<<action_group<<" organization="<<int(offer.plan.organization)<<" intervention="<<chosen.intervention<<" episode="<<admission.episode<<'\n';
    if(!bounded||!fresh(snapshot,18000)){fault();return;}
    const auto command=advance(local);dispatch(command,local);if(local.frame.clock==input_->frame.clock)observed(local);
}
inline void draft() {
    const auto own=cambc::draft::current_draft(cambc::team::ally);const int ordinal=int(own.size()),picks=int(cambc::draft::team_size());if(ordinal>=picks)return;
    common::DraftInputs in;in.ordinal=ordinal;in.picks=picks;in.prospective_actor=0;
    for(int i=0;i<ordinal&&i<5;++i)in.own_picks[i]=sdk::type(own[i]);in.settings=sdk::settings();in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
    const int window=std::min(120,in.settings.max_rounds);in.windows.deadlines=in.windows.windows={window,window,window};
    in.windows.sampled_actor_id=0;in.windows.sampled_round=0;in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto candidates=p8::native::draft_candidates(0,ordinal,false);const auto selected=probe_reveal_commit::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-16 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
    for(const auto champion:draft_priority(ordinal)){const auto pick=sdk::draft_type(champion);if(std::find(own.begin(),own.end(),pick)==own.end()){cambc::draft::pick(pick);return;}}
}
inline int run() {
    std::unique_ptr<Runtime> bot;while(true) {
        try {
            if(cambc::is_draft())native::draft();else {
                if(!bot) {
                    const int construction_round=int(cambc::round_number());
                    bot.reset(new Runtime);
                    if(int(cambc::round_number())==construction_round&&cambc::fuel_remaining()>=32000)
                        std::printf("p8-16 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                            construction_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                bot->turn();
            }
        }
        catch(const cambc::error& error){std::cout<<"p8-16 ERROR engine "<<error.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& error){std::cout<<"p8-16 ERROR exception "<<error.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-16 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::probe_reveal_commit::native
