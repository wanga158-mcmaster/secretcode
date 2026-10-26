#pragma once
#include "configuration.h"
#include "../draft.h"
#include "../../../common/src/admission_slack.h"
#include <bit>
#include <cstdio>
#include <iostream>
#include <memory>

namespace p8::reversible_workshop::native {
using namespace p8::native;
// One concrete actor-local native driver. The bounded safety/route services
// below mirror the current common driver; transactions use the typed SDK seam.
class Runtime {
    Routes routes_;sdk::Adapter adapter_;
    std::unique_ptr<Inputs> input_=make_default<Inputs>();
    std::unique_ptr<EvidenceLedger> ledger_;Workshop workshop_;CycleTrace trace_;
    std::unique_ptr<SoloStorage> primary_model_;
    std::vector<Solo> menu_models_;
    int primary_cursor_=-1,primary_fixed_pair_=-1;bool primary_free_due_=false;
    p6::ScoreHistory scores_;Work work_;
    std::unique_ptr<LocalFrame> ordinary_local_;
    bool ordinary_wait_pending_=false;NumericState ordinary_entry_{};
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};
    int initial_safety_count_=0;
    int selected_site_=-1,selected_bank_=-1,renewal_site_=-1,last_round_=-1;
    int a_site_=-1,a_bank_=-1,b_site_=-1,b_bank_=-1;
    int inspection_turns_=0,next_scan_=0,inspection_site_=-1,inspection_cursor_=0;
    Route inspection_{};std::uint64_t threat_=0,initial_token_=1;
    bool unavailable_logged_=false,initial_purchase_failed_=false;
    bool preparation_used_=false,preparing_=false;int preparation_started_=-1;
    bool declaration_failed_=false,recurring_preparation_disabled_=false;int recurring_preparation_started_=-1;
    bool diagnostic_routes_started_=false;
    struct PipelineSample {
        std::array<int,18> round{};
        std::array<unsigned long long,18> fuel{};
        std::array<unsigned long long,3> primary_budget{};
        int entered=0,returned=0,stage=-1,main_stage=-1,fallback_entered=-1,fallback_outcome=0;
        int first_primary_round=-1;unsigned long long exit=0,first_primary_fuel=0,first_forecast_fuel=0;
        unsigned primary_attempts=0,milestones=0;
        bool feasible=false,forecast_entered=false,budget_refused=false;
    };
    // Each first milestone is retained once, even while an older sample awaits
    // printing. These numbers never supply an observation or action permission.
    std::array<PipelineSample,6> pipeline_samples_{};
    unsigned pipeline_count_=0,pipeline_logged_=0,pipeline_seen_=0;
    struct PipelineScope:PipelineSample {
        bool enabled;std::array<PipelineSample,6>& saved;unsigned& count;unsigned& seen;
        PipelineScope(std::array<PipelineSample,6>& saved,unsigned& count,unsigned& seen):
            enabled(count<saved.size()&&seen!=63),saved(saved),count(count),seen(seen) {
            if(enabled){entered=int(cambc::round_number());mark(0);}
        }
        void mark(int at) {
            if(!enabled)return;
            if(at<14)main_stage=at;
            stage=at;round[at]=int(cambc::round_number());fuel[at]=cambc::fuel_remaining();
        }
        void before_primary() {
            if(!enabled)return;
            primary_budget={};
            if(!primary_attempts){first_primary_round=int(cambc::round_number());first_primary_fuel=cambc::fuel_remaining();}
            ++primary_attempts;
        }
        void after_primary() {
            if(enabled&&primary_budget[2]) {
                forecast_entered=true;
                if(!first_forecast_fuel)first_forecast_fuel=primary_budget[2];
            }
        }
        ~PipelineScope() noexcept {
            if(!enabled)return;
            try {
                after_primary();
                returned=int(cambc::round_number());exit=cambc::fuel_remaining();
                const unsigned events=(primary_attempts?1U:0U)|(forecast_entered?2U:0U)|
                    (feasible?4U:0U)|(main_stage>=9?8U:0U)|
                    (fallback_entered>=0&&returned!=fallback_entered?16U:0U)|(budget_refused?32U:0U);
                milestones=events&~seen;
                if(milestones&&count<saved.size()){saved[count++]=*this;seen|=milestones;}
            } catch(...) {
                // Diagnostics must not throw while an engine error unwinds.
            }
        }
    };
    struct ForecastSample {
        std::array<int,9> round{};
        std::array<unsigned long long,9> fuel{};
        // Outer solo fuel has no paired round; rb2 is the pre-builder reading.
        std::array<unsigned long long,3> solo_budget{};
        int input_round=-1,pair=-1,raw_status=-1;
        unsigned ordinal=0,prior_returns=0,mask=0,reasons=0;
        bool complete_current=false,raw_feasible=false,solo_feasible=false,guard=false,usable_primary=false;
    };
    // Separate retention prevents the outer sampler's first-event mask from
    // hiding later calls through this actor's persistent model/request storage.
    std::array<ForecastSample,4> forecast_samples_{};
    unsigned forecast_count_=0,forecast_logged_=0,forecast_seen_=0,forecast_returns_=0;
    struct ForecastScope:ForecastSample {
        bool enabled,returned=false;
        std::array<ForecastSample,4>& saved;unsigned& count;unsigned& seen;unsigned& returns;
        ForecastScope(std::array<ForecastSample,4>& saved,unsigned& count,unsigned& seen,unsigned& returns,
                      int frame_round,int selected_pair):
            enabled(count<saved.size()&&seen!=15),saved(saved),count(count),seen(seen),returns(returns) {
            input_round=frame_round;pair=selected_pair;
        }
        static void mark(void* context,unsigned at) noexcept {
            auto& sample=*static_cast<ForecastScope*>(context);
            if(!sample.enabled||at>=sample.round.size())return;
            try {
                const int round=int(cambc::round_number());
                const auto fuel=cambc::fuel_remaining();
                sample.round[at]=round;sample.fuel[at]=fuel;sample.mask|=1U<<at;
            } catch(...) {
                // An incomplete diagnostic never supplies a current result.
            }
        }
        forecast_probe::BuildProbe probe() noexcept {
            return enabled?forecast_probe::BuildProbe{this,mark}:forecast_probe::BuildProbe{};
        }
        void after_solo(const Solo& result) noexcept {
            returned=true;raw_status=int(result.model.status);
            raw_feasible=result.model.status==p6::Status::feasible;solo_feasible=result.feasible;
        }
        ~ForecastScope() noexcept {
            if(!enabled||!returned||!(mask&1U))return;
            prior_returns=returns++;ordinal=prior_returns+1;
            complete_current=mask==0x1ffU;
            for(unsigned at=0;complete_current&&at<round.size();++at)
                if(round[at]!=input_round||(at&&fuel[at]>fuel[at-1]))complete_current=false;
            const unsigned events=(prior_returns==0?1U:0U)|(prior_returns&&complete_current?2U:0U)|
                (prior_returns&&complete_current&&raw_feasible?4U:0U)|(prior_returns&&usable_primary?8U:0U);
            reasons=events&~seen;
            if(reasons&&count<saved.size()){saved[count++]=*this;seen|=reasons;}
        }
    };
    struct MenuSample {
        // b0/b1 bracket baselines; each attempt has assembly entry, pre-queue,
        // queue return. Exit is the build_menu return/unwind after invitations.
        std::array<int,9> round{};
        std::array<unsigned long long,9> fuel{};
        std::array<std::array<std::uint64_t,3>,2> queue_budget{};
        std::array<int,2> queue_result{-1,-1},attempt_result{-1,-1};
        int input_round=-1,result=-1;
        unsigned ordinal=0,prior_returns=0,reasons=0,mask=0,baseline_count=0,attempts=0;
        bool invite=false,returned=false,observed_allies=false;
    };
    // Independent of the outer and forecast queues: retain the first call and
    // later current-baseline, current-queue, current-success milestones once.
    std::array<MenuSample,4> menu_samples_{};
    unsigned menu_count_=0,menu_logged_=0,menu_seen_=0,menu_calls_=0,menu_returns_=0;
    struct MenuScope:MenuSample {
        bool enabled;unsigned active_attempt=0;
        std::array<MenuSample,4>& saved;unsigned& count;unsigned& seen;unsigned& returns;
        MenuScope(std::array<MenuSample,4>& saved,unsigned& count,unsigned& seen,unsigned& calls,unsigned& returns,
                  int frame_round,bool invitations):
            enabled(count<saved.size()&&seen!=15),saved(saved),count(count),seen(seen),returns(returns) {
            input_round=frame_round;invite=invitations;ordinal=++calls;
        }
        void mark(unsigned at) noexcept {
            if(!enabled||at>=round.size())return;
            try {
                const int observed_round=int(cambc::round_number());
                const auto observed_fuel=cambc::fuel_remaining();
                round[at]=observed_round;fuel[at]=observed_fuel;mask|=1U<<at;
            } catch(...) {
                // A missing diagnostic never changes the menu or error path.
            }
        }
        static void observe_offer(void* context,unsigned at,int value) noexcept {
            auto& sample=*static_cast<MenuScope*>(context);
            if(!sample.enabled||sample.active_attempt>=2||at>=3)return;
            if(at==2)sample.queue_result[sample.active_attempt]=value;
            sample.mark(2+sample.active_attempt*3+at);
        }
        IndependentOfferProbe probe(unsigned attempt) noexcept {
            active_attempt=attempt;attempts=attempt+1;
            return enabled?IndependentOfferProbe{this,observe_offer,&queue_budget[attempt]}:IndependentOfferProbe{};
        }
        void after_attempt(bool success) noexcept {
            if(enabled&&active_attempt<2)attempt_result[active_attempt]=int(success);
        }
        bool finish(bool success) noexcept {returned=true;result=int(success);return success;}
        bool current_through(unsigned end) const noexcept {
            bool observed=false;unsigned long long previous=0;
            for(unsigned at=0;at<=end;++at)if(mask&(1U<<at)) {
                if(round[at]!=input_round||(observed&&fuel[at]>previous))return false;
                observed=true;previous=fuel[at];
            }
            return observed;
        }
        ~MenuScope() noexcept {
            if(!enabled)return;
            mark(8);
            if(!(mask&1U))return;
            prior_returns=returns;if(returned)++returns;
            const bool baseline_current=(mask&3U)==3U&&current_through(1);
            const bool queue_current=((mask&0x18U)==0x18U&&current_through(4))||
                ((mask&0xc0U)==0xc0U&&current_through(7));
            const bool success_current=returned&&result==1&&(mask&0x100U)&&current_through(8);
            const unsigned events=1U|(ordinal>1&&baseline_current?2U:0U)|
                (ordinal>1&&queue_current?4U:0U)|(ordinal>1&&success_current?8U:0U);
            reasons=events&~seen;
            if(reasons&&count<saved.size()){saved[count++]=*this;seen|=reasons;}
        }
    };
    struct DecisionSample {
        // d0/d14 bracket the actual first decide call; d1..d13 are private
        // policy marks. Only option zero has phase detail, including in a
        // multi-offer menu; other options' phase costs remain unavailable.
        std::array<int,18> round{};
        std::array<unsigned long long,18> fuel{};
        int input_round=-1,offer_count=0,option=-1,choose_option=-1;
        int market=-1,legal0=-1,crew0=-1,design0=-1;
        unsigned ordinal=0,prior_returns=0,reasons=0,mask=0;
        bool returned=false,full=false,current=false,same_round=true,monotone=true;
    };
    // Retain the first actual call and later full, current, full/current
    // positive-return milestones independently of the other three queues.
    std::array<DecisionSample,4> decision_samples_{};
    unsigned decision_count_=0,decision_logged_=0,decision_seen_=0,decision_calls_=0,decision_returns_=0;
    struct DecisionScope:DecisionSample {
        std::array<DecisionSample,4>& saved;unsigned& count;unsigned& seen;unsigned& returns;
        DecisionScope(std::array<DecisionSample,4>& saved,unsigned& count,unsigned& seen,unsigned& calls,unsigned& returns,
                      int frame_round,int offers):saved(saved),count(count),seen(seen),returns(returns) {
            input_round=frame_round;offer_count=offers;ordinal=++calls;
        }
        static void observe(void* context,unsigned at,int value) noexcept {
            auto& sample=*static_cast<DecisionScope*>(context);
            if(at>=sample.round.size())return;
            try {
                const int observed_round=int(cambc::round_number());
                const auto observed_fuel=cambc::fuel_remaining();
                sample.round[at]=observed_round;sample.fuel[at]=observed_fuel;sample.mask|=1U<<at;
                if(at==6)sample.market=value;
                if(at==8)sample.legal0=value;
                if(at==9)sample.crew0=value;
                if(at==10)sample.design0=value;
                if(at==13)sample.choose_option=value;
            } catch(...) {
                // A missing numeric mark never changes the decision/error path.
            }
        }
        void mark(unsigned at) noexcept {observe(this,at,-1);}
        DecisionProbe probe() noexcept {return {this,observe};}
        void finish(const Decision& result) noexcept {returned=true;option=result.option;mark(14);}
        ~DecisionScope() noexcept {
            if(!(mask&1U))return;
            prior_returns=returns;if(returned)++returns;
            bool observed=false;unsigned long long previous=0;
            constexpr std::array<unsigned,18> order{0,1,2,3,4,5,6,7,15,16,17,8,9,10,11,12,13,14};
            for(unsigned at:order)if(mask&(1U<<at)) {
                if(round[at]!=input_round)same_round=false;
                if(observed&&fuel[at]>previous)monotone=false;
                observed=true;previous=fuel[at];
            }
            full=returned&&(mask&0x7fffU)==0x7fffU;
            // Early rejection can be current without visiting evaluation.
            current=returned&&(mask&0x607fU)==0x607fU&&same_round&&monotone;
            const unsigned events=1U|(ordinal>1&&full?2U:0U)|(ordinal>1&&current?4U:0U)|
                (ordinal>1&&full&&current&&option>=0?8U:0U);
            reasons=events&~seen;
            if(reasons&&count<saved.size()){saved[count++]=*this;seen|=reasons;}
        }
    };
    struct OrdinarySample {
        std::array<int,9> round{};
        std::array<unsigned long long,9> fuel{};
        int input=-1,group=-1,reason=-1,option=-1,rejection=-1;
        unsigned ordinal=0,mask=0;
        bool started=false,debited=false;
        void mark(unsigned at) noexcept {
            try {
                round[at]=int(cambc::round_number());fuel[at]=cambc::fuel_remaining();mask|=1U<<at;
            } catch(...) {}
        }
    };
    std::array<OrdinarySample,2> ordinary_samples_{};
    unsigned ordinary_count_=0,ordinary_logged_=0,ordinary_calls_=0;
    bool ordinary_success_seen_=false;

    // A sole independent noncollector has no external proposal or episode to
    // bind. Select once on current inputs, then observe physical admission.
    bool admit_ordinary(sdk::Snapshot& snapshot,const Solo& own) {
        auto& in=*input_;
        if(!ordinary_local_||work_.active||workshop_.declared||workshop_.phase!=Phase::A||ledger_->active()||
            collector_actor(snapshot.frame.own.actor,snapshot.draft)||in.offer_count!=1||in.independent!=0)return false;
        auto& offer=in.offers[0];const auto& plan=offer.plan;
        if(plan.organization!=Organization::U||plan.crew_count!=1||plan.collector!=snapshot.frame.clock.self||
            plan.crew[0]!=snapshot.frame.clock.self||plan.ingress.count<2||
            offer.schedule.actor_count!=1||offer.schedule.actors[0].actor_id!=snapshot.frame.clock.self||offer.safety_count!=1||
            offer.diagnostic_segment||offer.marked_extra_turns!=0)return false;
        OrdinarySample sample;sample.input=snapshot.frame.clock.round;sample.group=plan.group;
        sample.ordinal=++ordinary_calls_;
        const bool observe=ordinary_count_<ordinary_samples_.size()&&!ordinary_success_seen_;
        const auto mark=[&](unsigned at){if(observe)sample.mark(at);};
        const auto finish=[&](int reason) {
            sample.reason=reason;
            if(observe&&(ordinary_count_==0||(sample.debited&&!ordinary_success_seen_))) {
                ordinary_samples_[ordinary_count_++]=sample;
                ordinary_success_seen_|=sample.debited;
            }
            return true;
        };
        const auto current=[&](std::uint64_t reserve) {
            return cambc::fuel_remaining()>=reserve&&int(cambc::round_number())==snapshot.frame.clock.round;
        };
        mark(0);
        if(!current(25000))return finish(1);
        if(!admission_wait_covered(snapshot.frame,plan,primary_model_->request,own.model))return finish(2);
        mark(1);
        const auto target_threat=threats(plan.group);
        if(!current(25000))return finish(1);
        if(own.site<0||own.site>=int(routes_.sites.size())||
            !adapter_.attach_ordinary_group(snapshot,routes_.sites[own.site].group,
                primary_model_->request.groups[0].observed,work_))return finish(3);
        // Attachment publishes group and actor occupancy facts at this same
        // clock. Publish that frame before the one final policy evaluation.
        in.frame=snapshot.frame;in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        in.target_group=plan.group;
        in.threat_regime={int(target_threat&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        mark(2);
        auto& local=*ordinary_local_;local.current_round=-1;local.fuel_resumed=true;
        local.admission_only=true;local.safety_count=0;local.legal.knowledge=p6::Knowledge::unknown;
        mark(3);
        const auto admission=workshop_.admit_ordinary_current(in,*ledger_,arm,work_,
            [&](const Decision& selected)->const LocalFrame& {
                sample.option=selected.option;mark(4);
                if(selected.option==0)adapter_.admission_local(snapshot,plan,
                    {offer.safety.data(),std::size_t(offer.safety_count)},work_,local);
                mark(5);return local;
            });
        sample.started=admission.started;sample.rejection=int(admission.rejection);mark(6);
        if(!admission.started)return finish(4);
        const auto abandon=[&](PhysicalFailure reason) {
            ordinary_wait_pending_=false;work_.invalidate(reason);
        };
        if(!current(2000)){abandon(PhysicalFailure::fuel);return finish(5);}
        if(offer.safety_count!=1){abandon(PhysicalFailure::stale);return finish(5);}
        initial_safety_[0]=offer.safety[0];initial_safety_count_=1;threat_=target_threat;
        selected_site_=own.site;selected_bank_=own.bank;renewal_site_=own.site;
        unavailable_logged_=false;inspection_turns_=0;inspection_site_=-1;
        ordinary_entry_=snapshot.own;mark(7);
        if(!current(1000)){abandon(PhysicalFailure::fuel);return finish(5);}
        if(!work_.consume_admission_turn(local)){abandon(PhysicalFailure::stale);return finish(6);}
        ordinary_wait_pending_=true;sample.debited=true;mark(8);
        // No command or receipt is produced on this charged ingress wait.
        return finish(0);
    }
    static std::uint64_t threats(int except) {
        std::uint64_t hash=1469598103934665603ULL;
        for (const auto& camp:cambc::vision::camps())
            if (camp.is_alive()&&camp.is_awake()&&group_id(camp.camp_group())!=except) {
                hash=hash_word(hash,std::uint64_t(sdk::identity(camp))+1);
                hash=hash_word(hash,std::bit_cast<std::uint64_t>(double(camp.strength())));
            }
        for (const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
            hash=hash_word(hash,std::uint64_t(sdk::identity(enemy))+1);
            const auto p=enemy.position();hash=hash_word(hash,std::uint32_t(p.x));hash=hash_word(hash,std::uint32_t(p.y));
            hash=hash_word(hash,std::bit_cast<std::uint64_t>(double(enemy.strength())));
            hash=hash_word(hash,std::bit_cast<std::uint64_t>(double(enemy.focus())));
        }
        return hash;
    }
    std::array<p6::CycleCertificate,p6::max_actors> current_safety(const sdk::Snapshot& snapshot) const {
        auto result=initial_safety_;
        for (int i=0;i<initial_safety_count_;++i) {
            result[i].clock=snapshot.frame.clock;
            if (const auto* actor=snapshot.frame.visible(result[i].actor))
                for (auto* exposure:{&result[i].clear,&result[i].hold,&result[i].bank,&result[i].recovery}) {
                    exposure->health=actor->health;exposure->max_health=actor->max_health;
                }
        }
        return result;
    }
    bool calm(const sdk::Snapshot& snapshot) const {
        const auto pos=snapshot.frame.own.actor.position;
        for (const auto& enemy:cambc::vision::champions(cambc::team::enemy))
            if (distance(pos,point(enemy.position()))<=enemy.attack_range()+2) return false;
        for (const auto& camp:cambc::vision::camps())
            if (camp.is_alive()&&camp.is_awake()&&distance(pos,point(camp.position()))<=camp.attack_range()+1) return false;
        return true;
    }
    bool productive(const sdk::Snapshot& snapshot) const {
        if (renewal_site_<0||renewal_site_>=int(routes_.sites.size()))return false;
        const auto live=group(routes_.sites[renewal_site_],snapshot.frame.clock);
        return live.original_spawn_visible&&(live.lifecycle==p6::Lifecycle::living||
            live.lifecycle==p6::Lifecycle::reward_available);
    }
    std::optional<p6::Point> escape(const sdk::Snapshot& snapshot) const {
        const auto origin=snapshot.frame.own.actor.position;
        const auto camps=cambc::vision::camps();
        const auto enemies=cambc::vision::champions(cambc::team::enemy);
        auto incoming=[&](p6::Point p) {
            double damage=0;
            for (const auto& camp:camps)
                if (camp.is_alive()&&camp.is_awake()&&distance(p,point(camp.position()))<=camp.attack_range()) damage+=camp.strength();
            for (const auto& enemy:enemies)
                if (distance(p,point(enemy.position()))<=enemy.attack_range()+1) {
                    double cast=0;for (const auto& ability:enemy.abilities())
                        if (ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))
                            cast=std::max(cast,double(ability.damage_amount()));
                    damage+=enemy.strength()+cast;
                }
            return damage;
        };
        double best=incoming(origin);std::optional<p6::Point> selected;
        const auto occupied=cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
        for (int y=-1;y<=1;++y)for (int x=-1;x<=1;++x) {
            p6::Point p{origin.x+x,origin.y+y};
            if ((!x&&!y)||p.x<0||p.y<0||p.x>=cambc::map::width()||p.y>=cambc::map::height()||
                routes_.terrain().is_set(tile(p))||occupied.is_set(tile(p)))continue;
            const auto risk=incoming(p);
            if (risk<best&&risk<snapshot.frame.own.actor.health) {best=risk;selected=p;}
        }
        return selected;
    }
    void dispatch(const Command& command,const LocalFrame& local) {
        if (local.frame.clock.round!=int(cambc::round_number())||command.clock!=local.frame.clock) return;
        const auto receipt=adapter_.dispatch(command,local,work_);
        if (receipt.current_clock!=command.clock) {
            work_.request_stop();work_.route_witness_active=false;
            if (ledger_&&ledger_->active())ledger_->invalidate_active(receipt.current_clock.round);
            return;
        }
        work_.confirm(command,receipt);
    }
    void manual(const Plan& plan,const LocalFrame& local,PhysicalAction action,p6::Point target={},Leg leg=Leg::ingress) {
        work_.plan=plan;work_.route_witness_active=false;
        dispatch(work_.emit(local,action,target,leg),local);
    }
    void fallback(sdk::Snapshot& snapshot,PipelineScope* pipeline=nullptr) {
        if(pipeline){pipeline->mark(14);pipeline->fallback_entered=pipeline->round[14];}
        auto current=[&](std::uint64_t reserve=20000) {
            const bool same=snapshot.valid&&int(cambc::round_number())==snapshot.frame.clock.round;
            const bool enough=cambc::fuel_remaining()>=reserve;
            if((!same||!enough)&&pipeline){pipeline->budget_refused=true;pipeline->fallback_outcome=same?2:1;}
            return same&&enough;
        };
        if(!current())return;
        const bool safe=calm(snapshot);
        if(!current())return;
        Plan route=exact_default<Plan>();route.collector=snapshot.frame.clock.self;
        route.group=inspection_site_>=0?group_id(routes_.sites[inspection_site_].group):-1;
        auto issue=[&](PhysicalAction action,p6::Point target={},Leg leg=Leg::ingress,
                       std::optional<p6::Point> exit={},bool inspection=false) {
            if(!current())return;
            if(pipeline)pipeline->mark(16);
            auto local=adapter_.local(snapshot,route,{},safe,safe,false,exit);
            if(!current(std::uint64_t(local.action_reserve)))return;
            if(!local.fresh()||local.fuel_resumed){if(pipeline)pipeline->fallback_outcome=5;return;}
            if(!current(std::uint64_t(local.action_reserve)))return;
            local.fuel_remaining=std::int64_t(cambc::fuel_remaining());
            if(inspection)++inspection_turns_;
            manual(route,local,action,target,leg);
            if(pipeline){pipeline->fallback_outcome=3;pipeline->mark(17);}
        };
        if (safe&&snapshot.frame.own.actor.channel!=p6::Channel::none&&snapshot.frame.own.actor.channel_valid) {
            if(pipeline)pipeline->mark(15);
            issue(PhysicalAction::preserve_channel);return;
        }
        // Existing inventory is secured before attempting another gem source.
        if (snapshot.frame.own.actor.bag>0&&safe) {
            int best=-1,steps=max_route_tiles;bool pending=false;
            for (int i=0;i<int(routes_.banks.size());++i) {
                const auto& field=routes_.banks[i].approach;
                if(!field.ready){pending=true;continue;}
                const int cost=routes_.static_distance(snapshot.frame.own.actor.position,field);
                if(cost<steps){best=i;steps=cost;}
            }
            if(!current())return;
            if (best>=0) {
                routes_.route_into(snapshot.frame.own.actor.position,routes_.banks[best].approach,route.bank);
                if(pipeline)pipeline->mark(15);
                if(!current()||!route_valid(route.bank))return;
                route.altar=routes_.banks[best].altar.y*cambc::map::width()+routes_.banks[best].altar.x;
                const bool arrived=route.bank.count==1;
                issue(arrived?PhysicalAction::tribute:PhysicalAction::move,
                    arrived?p6::Point{}:route.bank.tiles[1],Leg::bank);return;
            }
            if(pending){if(pipeline)pipeline->fallback_outcome=4;return;}
        }
        const int now=snapshot.frame.clock.round;
        if (safe&&now>=next_scan_&&inspection_turns_<8) {
            if (inspection_site_<0) {
                int nearest=-1,steps=max_route_tiles;bool pending=false;
                for (int i=0;i<int(routes_.sites.size());++i) {
                    const auto& field=routes_.sites[i].approach;
                    if(!field.ready){pending=true;continue;}
                    const int cost=routes_.static_distance(snapshot.frame.own.actor.position,field);
                    if(cost<steps){nearest=i;steps=cost;}
                }
                if(!current())return;
                if(nearest>=0) {
                    routes_.route_into(snapshot.frame.own.actor.position,routes_.sites[nearest].approach,route.ingress);
                    if(pipeline)pipeline->mark(15);
                    if(!current())return;
                    if(route_valid(route.ingress)){inspection_site_=nearest;inspection_=route.ingress;inspection_cursor_=0;}
                    else inspection_turns_=8;
                } else if(pending) {
                    // A ready but unreachable field does not turn another
                    // unfinished immutable field into a failed opportunity.
                    next_scan_=now+1;if(pipeline)pipeline->fallback_outcome=4;return;
                } else {
                    inspection_turns_=8;
                }
            }
            if (inspection_site_>=0&&route_valid(inspection_)) {
                route.ingress=inspection_;route.group=group_id(routes_.sites[inspection_site_].group);
                if(pipeline)pipeline->mark(15);
                if(!current())return;
                const auto pos=snapshot.frame.own.actor.position;
                if (inspection_cursor_+1<inspection_.count&&pos==inspection_.tiles[inspection_cursor_+1])++inspection_cursor_;
                if (pos==inspection_.tiles[inspection_cursor_]) {
                    const bool arrived=inspection_cursor_+1==inspection_.count;
                    issue(arrived?PhysicalAction::inspect:PhysicalAction::move,
                        arrived?p6::Point{}:inspection_.tiles[inspection_cursor_+1],Leg::ingress,{},true);return;
                }
            }
            inspection_turns_=8;
        }
        if (inspection_turns_>=8) {
            if (!unavailable_logged_) {std::cout<<"p8 OPPORTUNITY_UNAVAILABLE\n";unavailable_logged_=true;}
            next_scan_=now+8;inspection_turns_=0;inspection_site_=-1;
        }
        if(!current())return;
        const auto exit=safe?std::optional<p6::Point>{}:escape(snapshot);
        if(pipeline)pipeline->mark(15);
        if(!current())return;
        issue(snapshot.frame.own.at_spawn?PhysicalAction::recover:
            !safe&&exit?PhysicalAction::escape:safe?PhysicalAction::recall:PhysicalAction::yield,exit.value_or(p6::Point{}),Leg::ingress,exit);
    }

    void populate(const sdk::Snapshot& snapshot,bool quotes=false) {
        auto& in=*input_;in.frame=snapshot.frame;
        if(in.settings_identity.source!=p6::Source::queried_setting) {
            in.settings=sdk::settings();
            in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
        }
        in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        in.target_group=selected_site_<0?0:group_id(routes_.sites[selected_site_].group);
        const bool supported_regime=workshop_.phase==Phase::B||workshop_.phase==Phase::rebuilding_B||workshop_.phase==Phase::rollback_pending;
        const int regime=workshop_.declared?(supported_regime?workshop_.b().plan.group:workshop_.a().plan.group):in.target_group;
        in.threat_regime={int(threats(regime)&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const auto enemy=cambc::global_submitted_gems(cambc::team::enemy);
        scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        if(quotes) {
            static constexpr std::array quote_ids{damage_item,survival_item};
            in.market=sdk::market(snapshot,quote_ids);
        }
        else {
            in.market=exact_default<Market>();const auto inventory=sdk::own_inventory();
            if(inventory.valid()&&inventory.hash()==snapshot.own.equipment_hash)
                in.market.inventory={inventory,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
            in.market.spawn_shop_legal=snapshot.spawn_shop_legal;
        }
        // A market/ongoing-work observation has no scored hypothetical income.
        // Before planning, this unused menu slot satisfies the shared shop
        // snapshot shape; choose() is called only after build_menu replaces it.
        if(in.offer_count==0){in.offer_count=1;in.independent=0;}
        if(workshop_.declared) {
            in.component_slot=workshop_.a().context;in.supported_slot=workshop_.b().context;
            in.component_entry=workshop_.a().entry;in.supported_entry=workshop_.b().entry;
        }
    }
    std::unique_ptr<Solo> best_solo(const p6::Frame& frame,int fixed=-1,int bank=-1,
                                    std::span<const int> excluded={}) {
        std::unique_ptr<Solo> best;
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>55000;++s) {
            if(fixed>=0&&s!=fixed)continue;
            if(std::find(excluded.begin(),excluded.end(),group_id(routes_.sites[s].group))!=excluded.end())continue;
            for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>55000;++b) {
                if(bank>=0&&b!=bank)continue;
                const bool spawn_visit=collector_actor(frame.own.actor,input_->draft);
                auto model=std::make_unique<Solo>(solo(routes_,s,b,frame,spawn_visit));
                if(model->feasible&&(!best||model->plan.forecast_rounds<best->plan.forecast_rounds||
                    (model->plan.forecast_rounds==best->plan.forecast_rounds&&model->plan.group<best->plan.group)))best=std::move(model);
            }
        }
        return best;
    }
    const Solo* primary_solo(const sdk::Snapshot& snapshot,int fixed_site,int fixed_bank,PipelineScope& pipeline) {
        const int banks=int(routes_.banks.size()),pairs=int(routes_.sites.size())*banks;
        if(!primary_model_||!pairs)return nullptr;
        // Storage survives turns; a forecast's permission never does. Only this
        // call's freshly filled result can be returned to the menu below.
        primary_model_->result.feasible=false;
        const int fixed=fixed_site>=0&&fixed_site<int(routes_.sites.size())&&fixed_bank>=0&&fixed_bank<banks?
            fixed_site*banks+fixed_bank:-1;
        if(primary_fixed_pair_!=fixed){primary_fixed_pair_=fixed;primary_free_due_=false;}
        if(primary_cursor_<0)primary_cursor_=(snapshot.frame.own.actor.pick_ordinal*banks)%pairs;
        auto current=[&](std::uint64_t reserve) {
            const bool ok=snapshot.valid&&int(cambc::round_number())==snapshot.frame.clock.round&&cambc::fuel_remaining()>=reserve;
            if(!ok)pipeline.budget_refused=true;
            return ok;
        };
        if(!current(55000))return nullptr;
        const auto visible=cambc::vision::visible_tiles();
        auto eligible=[&](int pair) {
            const auto& source=routes_.sites[pair/banks];
            if(!source.approach.ready||!source.capture.ready||!routes_.banks[pair%banks].approach.ready||!routes_.spawn.ready)return false;
            for(const auto& member:source.group)if(visible.is_set(member.spawn_position()))return true;
            return false;
        };
        int pair=-1;bool retained=false;
        if(fixed>=0&&!primary_free_due_&&eligible(fixed)){pair=fixed;retained=true;}
        if(pair<0)for(int offset=0;offset<pairs;++offset) {
            const int candidate=(primary_cursor_+offset)%pairs;
            if(candidate!=fixed&&eligible(candidate)){pair=candidate;break;}
        }
        if(!current(55000))return nullptr;
        if(pair<0){primary_free_due_=false;return nullptr;}
        // A retained attempt gets one later free fallback if it cannot produce
        // a usable current result. Never run both expensive models this turn.
        if(retained)primary_free_due_=true;
        else {primary_cursor_=(pair+1)%pairs;primary_free_due_=false;}
        ForecastScope forecast{forecast_samples_,forecast_count_,forecast_seen_,forecast_returns_,snapshot.frame.clock.round,pair};
        pipeline.before_primary();
        auto* solo_budget=pipeline.enabled?&pipeline.primary_budget:forecast.enabled?&forecast.solo_budget:nullptr;
        const bool spawn_visit=collector_actor(snapshot.frame.own.actor,snapshot.draft);
        solo_into(routes_,pair/banks,pair%banks,snapshot.frame,spawn_visit,false,primary_model_->result,&primary_model_->request,
            solo_budget,forecast.probe(),true);
        if(forecast.enabled&&pipeline.enabled)forecast.solo_budget=pipeline.primary_budget;
        forecast.after_solo(primary_model_->result);
        pipeline.after_primary();
        pipeline.feasible=primary_model_->result.feasible;
        if(!(forecast.guard=current(30000))||!primary_model_->result.feasible)return nullptr;
        forecast.usable_primary=true;
        if(retained)primary_free_due_=false;
        return &primary_model_->result;
    }
    std::vector<Solo> baselines(const p6::Frame& frame,const Solo& own,int excluded=-1) {
        std::vector<Solo> models;models.reserve(3);models.push_back(own);normalize(models.back(),frame);
        for(int ordinal=0;ordinal<3&&ordinal<input_->draft.picks;++ordinal) {
            const int id=input_->draft.actors[ordinal];if(id==frame.clock.self)continue;
            const auto* actor=frame.visible(id);if(!actor||!actor->alive)continue;
            std::array<int,4> used{};int count=0;for(const auto& m:models)used[count++]=m.plan.group;
            if(excluded>=0)used[count++]=excluded;
            auto model=best_solo(subject(frame,*actor),-1,-1,{used.data(),std::size_t(count)});
            if(model){normalize(*model,frame);models.push_back(std::move(*model));}
        }
        return models;
    }
    std::span<const Solo> menu_baselines(const p6::Frame& frame,const Solo& own) {
        unsigned active=1;bool copied_own=false;
        for(int ordinal=0;ordinal<3&&ordinal<input_->draft.picks;++ordinal) {
            const int id=input_->draft.actors[ordinal];if(id==frame.clock.self)continue;
            const auto* actor=frame.visible(id);if(!actor||!actor->alive)continue;
            // best_solo cannot search below this same strict budget. Avoid
            // copying a complete subject Frame for an unavailable search.
            if(cambc::fuel_remaining()<=55000)break;
            std::array<int,4> used{};int count=0;used[count++]=own.plan.group;
            for(unsigned i=1;i<active;++i)used[count++]=menu_models_[i].plan.group;
            auto model=best_solo(subject(frame,*actor),-1,-1,{used.data(),std::size_t(count)});
            if(model) {
                if(!copied_own) {
                    copy_menu_baseline(menu_models_[0],own);normalize(menu_models_[0],frame);copied_own=true;
                }
                normalize(*model,frame);copy_menu_baseline(menu_models_[active++],*model);
            }
        }
        if(!copied_own) {
            if(can_borrow_menu_own(own,frame))return {&own,1};
            copy_menu_baseline(menu_models_[0],own);normalize(menu_models_[0],frame);
        }
        return {menu_models_.data(),std::size_t(active)};
    }
    bool initial_purchase(sdk::Snapshot& snapshot) {
        if(!collector_actor(snapshot.frame.own.actor,snapshot.draft)||workshop_.declared||work_.active||
            initial_purchase_failed_||!snapshot.frame.own.at_spawn||snapshot.own.bag||
            snapshot.frame.own.actor.channel!=p6::Channel::none||!calm(snapshot))return false;
        populate(snapshot,true);const auto& in=*input_;
        if(!in.market.inventory.current(snapshot.frame.clock,snapshot.frame.clock.self))return false;
        if(in.market.inventory.value==damage_inventory())return false;
        if(!(in.market.inventory.value==Inventory{}))return false;
        p6::Gems price=0;if(!quote_current(in,damage_item,false,price)||price>snapshot.frame.own.gold||!initial_token_)return false;
        TransactionCommand command;command.valid=true;command.kind=TransactionKind::buy;command.clock=snapshot.frame.clock;
        command.token=initial_token_++;command.item=damage_item;command.amount=price;command.gold_before=snapshot.frame.own.gold;
        command.before=in.market.inventory.value;command.after=damage_inventory();command.stats_before=snapshot.own;
        const auto result=sdk::transact(adapter_,snapshot,command);
        const bool confirmed=result.receipt.attempted&&result.receipt.success&&result.after.valid&&
            result.after.frame.clock.round==command.clock.round&&result.after.frame.clock.self==command.clock.self&&
            result.after.frame.clock.revision>command.clock.revision&&result.after.frame.own.gold==command.gold_before-price&&
            result.after_market.inventory.current(result.after.frame.clock,command.clock.self)&&
            result.after_market.inventory.value==damage_inventory()&&
            result.after.own.equipment_hash==damage_inventory().hash();
        if(result.receipt.attempted&&!confirmed)initial_purchase_failed_=true;
        if(confirmed){snapshot=result.after;populate(snapshot,true);std::cout<<"p8-04 INITIAL_D_CONFIRMED\n";}
        return result.receipt.attempted;
    }
    bool configure(const sdk::Snapshot& snapshot,const Solo& opening) {
        if(workshop_.declared||declaration_failed_||ledger_->size()!=0||!snapshot.frame.own.at_spawn||
            !collector_actor(snapshot.frame.own.actor,snapshot.draft)||snapshot.frame.own.actor.type!=p6::Champion::tavi||
            snapshot.own.health!=snapshot.own.max_health||!input_->market.inventory.current(snapshot.frame.clock,snapshot.frame.clock.self)||
            input_->market.inventory.value!=damage_inventory())return false;
        auto projected=project(snapshot,damage_inventory(),survival_inventory());if(!projected.valid||!routes_.spawn.ready)return false;
        projected.frame.own.actor.position=routes_.spawn.destination;
        auto a_frame=snapshot.frame;a_frame.own.actor.position=routes_.sites[opening.site].approach.destination;
        auto a=std::make_unique<Solo>(solo(routes_,opening.site,opening.bank,a_frame,true));if(!a->feasible)return false;
        auto best=std::make_unique<Coalition>();int chosen_site=-1,chosen_bank=-1;
        p6::ScoreEvaluation rank;
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>90000;++s) {
            if(s==opening.site||public_work(routes_.sites[s])<=public_work(routes_.sites[opening.site]))continue;
            auto projected_own=best_solo(projected.frame,opening.site,opening.bank);if(!projected_own)continue;
            const auto alternatives=baselines(projected.frame,*projected_own,group_id(routes_.sites[s].group));
            if(alternatives.size()!=3)continue;
            for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>90000;++b) {
                auto candidate=std::make_unique<Coalition>(coalition(routes_,s,b,projected.frame,snapshot.draft,alternatives,Organization::H,true));
                if(!candidate->feasible)continue;
                const auto next=common::evaluate(*input_,candidate->offer.schedule);
                if(next.eligible()&&(chosen_site<0||p6::compare(next,rank)>0)) {
                    rank=next;chosen_site=s;chosen_bank=b;best=std::move(candidate);
                }
            }
        }
        if(chosen_site<0)return false;
        auto da=native::declare(a->plan,snapshot.own,*input_,false,routes_.sites[opening.site].group.gem_value());
        auto db=native::declare(best->offer.plan,projected.state,*input_,true,routes_.sites[chosen_site].group.gem_value());
        da.key.threat_regime=int(threats(da.key.source_group)&0x7fffffff);
        db.key.threat_regime=int(threats(db.key.source_group)&0x7fffffff);
        const int sa=ledger_->predeclare(da),sb=ledger_->predeclare(db);
        Configuration ca{damage_inventory(),da.key,a->plan,sa,{public_work(routes_.sites[opening.site]),p6::Source::public_map}};
        Configuration cb{survival_inventory(),db.key,best->offer.plan,sb,{public_work(routes_.sites[chosen_site]),p6::Source::public_map}};
        if(!workshop_.declare(ca,cb,*ledger_)){declaration_failed_=true;return false;}
        a_site_=opening.site;a_bank_=opening.bank;b_site_=chosen_site;b_bank_=chosen_bank;
        populate(snapshot);std::cout<<"p8-04 DECLARED A="<<ca.plan.group<<" B="<<cb.plan.group<<'\n';return true;
    }
    bool build_menu(const sdk::Snapshot& snapshot,const Solo& own,bool invite=true) {
        MenuScope menu{menu_samples_,menu_count_,menu_seen_,menu_calls_,menu_returns_,snapshot.frame.clock.round,invite};
        auto& in=*input_;menu.mark(0);
        auto independent=menu_baselines(snapshot.frame,own);
        menu.baseline_count=unsigned(independent.size());menu.mark(1);
        if(!snapshot.valid||int(cambc::round_number())!=snapshot.frame.clock.round)return menu.finish(false);
        // Only the actual ordinary caller reuses the Adapter-owned complete
        // observation. Missing completeness retains the live queue check.
        const auto* observed_allies=!workshop_.declared&&workshop_.phase==Phase::A&&
            !collector_actor(snapshot.frame.own.actor,snapshot.draft)?
            adapter_.observed_allies(snapshot.frame.clock):nullptr;
        menu.observed_allies=observed_allies!=nullptr;
        in.offer_count=1;in.independent=0;
        const bool independent_ok=independent_offer(in.offers[0],own,independent,snapshot.frame,menu.probe(0),observed_allies);
        menu.after_attempt(independent_ok);
        if(!independent_ok) {
            copy_menu_baseline(menu_models_[0],own);normalize(menu_models_[0],snapshot.frame);
            independent={menu_models_.data(),1};
            if(int(cambc::round_number())!=snapshot.frame.clock.round)return menu.finish(false);
            const bool fallback_ok=independent_offer(in.offers[0],own,independent,snapshot.frame,menu.probe(1),observed_allies);
            menu.after_attempt(fallback_ok);
            if(!fallback_ok)return menu.finish(false);
        }
        if(workshop_.declared) {
            in.offers[0].context_slot=workshop_.a().context;in.offers[0].entry=workshop_.a().entry;
        }
        if(!invite||arm==Arm::control||independent.size()!=3)return menu.finish(true);
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        if(ordinal==0&&workshop_.declared) {
            auto support=std::make_unique<Coalition>(coalition(routes_,b_site_,b_bank_,snapshot.frame,snapshot.draft,independent,Organization::H,true));
            if(support->feasible) {
                in.offers[1]=support->offer;in.offers[1].context_slot=workshop_.b().context;in.offers[1].entry=workshop_.b().entry;
                in.offer_count=2;
            }
        } else if(ordinal==2) {
            const auto* collector=snapshot.frame.visible(snapshot.draft.actors[0]);
            if(!collector||collector->type!=p6::Champion::tavi)return menu.finish(true);
            for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>80000;++s) {
                if(routes_.sites[s].rally_count<2||collector->position!=routes_.sites[s].rally[0].destination)continue;
                for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>80000;++b) {
                    auto support=std::make_unique<Coalition>(coalition(routes_,s,b,snapshot.frame,snapshot.draft,independent,Organization::H,true));
                    if(!support->feasible)continue;
                    in.offers[1]=support->offer;in.offer_count=2;
                    in.visible_proposer={support->offer.plan.group,snapshot.frame.clock,collector->id,p6::Knowledge::exact};return menu.finish(true);
                }
            }
        }
        return menu.finish(true);
    }
    bool package_offer(const sdk::Snapshot& snapshot,RebuildComparison& compared,bool require_gain=true) {
        if(!workshop_.declared||b_site_<0||!snapshot.frame.visible(snapshot.draft.actors[2]))return false;
        auto current=snapshot;current.frame.own.actor.position=routes_.spawn.destination;
        const auto projected=project(current,input_->market.inventory.value,survival_inventory());if(!projected.valid)return false;
        auto ordinary=best_solo(snapshot.frame,a_site_,a_bank_);if(!ordinary)return false;
        auto alternatives=baselines(snapshot.frame,*ordinary,workshop_.b().plan.group);if(alternatives.size()!=3)return false;
        auto normal=std::make_unique<Offer>();if(!independent_offer(*normal,*ordinary,alternatives,snapshot.frame))return false;
        // Current A legality and prospective S survival are separate complete
        // calculations. No old A certificate is reused as new-build survival.
        auto a_at_spawn=best_solo(current.frame,a_site_,a_bank_);if(!a_at_spawn)return false;
        auto current_baselines=baselines(current.frame,*a_at_spawn,workshop_.b().plan.group);
        if(current_baselines.size()!=3)return false;
        auto actual=std::make_unique<Coalition>(coalition(routes_,b_site_,b_bank_,current.frame,snapshot.draft,current_baselines,Organization::H,true));
        auto b_alternative=best_solo(projected.frame,a_site_,a_bank_);if(!b_alternative)return false;
        auto b_baselines=baselines(projected.frame,*b_alternative,workshop_.b().plan.group);if(b_baselines.size()!=3)return false;
        auto future=std::make_unique<Coalition>(coalition(routes_,b_site_,b_bank_,projected.frame,snapshot.draft,b_baselines,Organization::H,true));
        const int setup=preparation(snapshot);
        if(!actual->feasible||!future->feasible||!preparation_safe(actual->offer,current.frame,setup)||
            !preparation_safe(future->offer,projected.frame,setup))return false;
        actual->offer.context_slot=workshop_.b().context;actual->offer.entry=workshop_.b().entry;
        auto& in=*input_;in.offers[0]=*normal;in.offers[1]=actual->offer;in.offer_count=2;in.independent=0;
        // At spawn the actual Frame position equals the projected entry. Before
        // the one recall preparation this is only a conditional cost forecast.
        const auto funds=snapshot.frame.own.at_spawn?funded(in,workshop_.a().inventory,workshop_.b().inventory):preparation_funding(in);
        if(!native::comparison(compared,in,future->offer,*normal,funds,setup))return false;
        return !require_gain||workshop_.comparison_benefits(in,compared,funds);
    }
    void return_spawn(sdk::Snapshot& snapshot) {
        const bool safe=calm(snapshot);Plan p;p.collector=snapshot.frame.clock.self;
        p.group=selected_site_<0?-1:group_id(routes_.sites[selected_site_].group);
        if(safe&&snapshot.frame.own.at_spawn&&snapshot.frame.own.actor.position!=routes_.spawn.destination&&routes_.spawn.ready) {
            const auto alignment=routes_.route(snapshot.frame.own.actor.position,routes_.spawn);
            bool known=route_valid(alignment)&&alignment.count<=9;
            for(int i=0;known&&i<alignment.count;++i)known=cambc::map::spawn_region(cambc::team::ally).is_set(tile(alignment.tiles[i]));
            if(known) {
                p.ingress=alignment;
                auto local=adapter_.local(snapshot,p,{},true,true,false);
                manual(p,local,PhysicalAction::move,alignment.tiles[1],Leg::ingress);return;
            }
        }
        const auto exit=escape(snapshot);
        auto local=adapter_.local(snapshot,p,{},safe,safe,false,exit);
        const auto& own=snapshot.frame.own.actor;
        const auto action=own.channel!=p6::Channel::none&&own.channel_valid&&safe?PhysicalAction::preserve_channel:
            snapshot.frame.own.at_spawn?PhysicalAction::recover:
            safe?PhysicalAction::recall:exit?PhysicalAction::escape:PhysicalAction::yield;
        manual(p,local,action,exit.value_or(p6::Point{}));
    }
    bool commission_current(sdk::Snapshot& snapshot) {
        auto own=best_solo(snapshot.frame,a_site_,a_bank_);
        if(!own||!build_menu(snapshot,*own))return false;
        const auto local=adapter_.local(snapshot,workshop_.b().plan,{},calm(snapshot),calm(snapshot),false);
        if(cambc::fuel_remaining()<30000||int(cambc::round_number())!=snapshot.frame.clock.round||
            !sdk::unchanged_shop_state(snapshot,snapshot.frame.clock,snapshot.own,snapshot.frame.own.gold)||
            cambc::fuel_remaining()<25000||int(cambc::round_number())!=snapshot.frame.clock.round)return false;
        return workshop_.commission(*input_,local,*ledger_,work_);
    }
    bool preparation_or_shop(sdk::Snapshot& snapshot) {
        if(!workshop_.declared||!collector_actor(snapshot.frame.own.actor,snapshot.draft)||arm==Arm::control)return false;
        auto& in=*input_;populate(snapshot,true);
        auto local=adapter_.local(snapshot,work_.plan,{},calm(snapshot),calm(snapshot),false,escape(snapshot),&work_);
        if(workshop_.rebuilding()) {
            const auto command=workshop_.transaction(in,local,*ledger_,work_);
            if(command.valid) {
                const auto result=sdk::transact(adapter_,snapshot,command);
                if(!result.after.valid){workshop_.transaction_failed(*ledger_);return true;}
                snapshot=result.after;populate(snapshot,true);
                if(!workshop_.confirm(command,result.receipt,*input_,*ledger_)) {
                    workshop_.transaction_failed(*ledger_);std::cout<<"p8-04 TRANSACTION_UNCONFIRMED\n";
                } else {
                    std::cout<<"p8-04 TRANSACTION_CONFIRMED item="<<command.item<<'\n';
                    if(workshop_.rebuild.completed&&workshop_.phase==Phase::rebuilding_B) {
                        if(commission_current(snapshot)){preparing_=false;std::cout<<"p8-04 COMMISSION_B\n";}
                        else std::cout<<"p8-04 COMMISSION_UNAVAILABLE retain_current_gear=1\n";
                    }
                }
                return true;
            }
            if(workshop_.rebuild.completed&&workshop_.phase==Phase::rebuilding_B) {
                // Unknown cooperation does not authorize another sale. Retain
                // current S gear and ordinary productive U, with no invitation
                // or automatic recall just to retry an unavailable campaign.
                if(!snapshot.frame.own.at_spawn)return false;
                if(commission_current(snapshot)){preparing_=false;std::cout<<"p8-04 COMMISSION_B\n";return true;}
                return workshop_.phase==Phase::rollback_pending;
            }
            return true;
        }
        if(workshop_.phase==Phase::rollback_pending) {
            if(snapshot.frame.own.actor.bag>0)return false;
            if(snapshot.frame.own.at_spawn) {
                if(workshop_.begin_rollback(in,local,*ledger_,work_))std::cout<<"p8-04 ROLLBACK_RESERVED\n";
                return true;
            }
            return_spawn(snapshot);return true;
        }
        if(workshop_.phase==Phase::A&&
            ledger_->component_qualified(workshop_.a().context,workshop_.a().entry,snapshot.frame.clock.round)) {
            const int limit=preparation(snapshot);if(limit<0)return false;
            if(preparing_&&snapshot.frame.clock.round-preparation_started_>limit)preparing_=false;
            if(!preparing_&&!preparation_used_) {
                auto compared=std::make_unique<RebuildComparison>();
                if(package_offer(snapshot,*compared)&&snapshot.own.bag==0&&calm(snapshot)) {
                    // Exactly one dedicated departure is allowed before a
                    // reservation; losing the package does not refill it.
                    preparation_used_=preparing_=true;preparation_started_=snapshot.frame.clock.round;
                    std::cout<<"p8-04 PREPARATION_STARTED\n";
                }
            }
            if(preparing_) {
                auto compared=std::make_unique<RebuildComparison>();
                if(!package_offer(snapshot,*compared)||!calm(snapshot)||snapshot.own.bag) {
                    preparing_=false;return false;
                }
                if(!snapshot.frame.own.at_spawn) {return_spawn(snapshot);return true;}
                if(snapshot.frame.own.actor.position!=routes_.spawn.destination) {return_spawn(snapshot);return true;}
                if(snapshot.own.health<snapshot.own.max_health) {return_spawn(snapshot);return true;}
                auto shop_local=adapter_.local(snapshot,work_.plan,{},true,true,false);
                if(cambc::fuel_remaining()>=30000&&int(cambc::round_number())==snapshot.frame.clock.round&&
                    sdk::unchanged_shop_state(snapshot,snapshot.frame.clock,snapshot.own,snapshot.frame.own.gold)&&
                    cambc::fuel_remaining()>=25000&&int(cambc::round_number())==snapshot.frame.clock.round&&
                    workshop_.begin_forward(*input_,shop_local,*ledger_,arm,work_,*compared)) {
                    preparing_=false;std::cout<<"p8-04 REBUILD_B_RESERVED\n";return true;
                }
                preparing_=false;
            }
        }
        // Both B starts use the same exact spawn-entry context. Recurring
        // recall is actual preparation, never an extra completed episode.
        if(workshop_.phase==Phase::B&&snapshot.own.bag==0&&
            (!snapshot.frame.own.at_spawn||snapshot.frame.own.actor.position!=routes_.spawn.destination)) {
            if(recurring_preparation_disabled_)return false;
            const bool qualified=ledger_->organization_qualified(workshop_.b().context,workshop_.b().entry,snapshot.frame.clock.round);
            if(ledger_->suspended(workshop_.b().context,snapshot.frame.clock.round/common::project_rounds)||
                (ledger_->campaign_exhausted(workshop_.b().context)&&!qualified)) {
                recurring_preparation_started_=-1;recurring_preparation_disabled_=true;return false;
            }
            const int now=snapshot.frame.clock.round,setup_limit=preparation(snapshot);
            if(setup_limit<0)return false;
            if(recurring_preparation_started_>=0&&now-recurring_preparation_started_>setup_limit) {
                recurring_preparation_started_=-1;recurring_preparation_disabled_=true;return false;
            }
            auto own=best_solo(snapshot.frame);if(!own)return false;
            if(!build_menu(snapshot,*own,false))return false;
            auto projected=snapshot;projected.frame.own.actor.position=routes_.spawn.destination;
            auto baseline=best_solo(projected.frame);if(!baseline)return false;
            const auto alternatives=baselines(projected.frame,*baseline,workshop_.b().plan.group);
            auto supported=std::make_unique<Coalition>(coalition(routes_,b_site_,b_bank_,projected.frame,snapshot.draft,alternatives,Organization::H,true));
            if(supported->feasible) {
                auto compared=std::make_unique<RebuildComparison>();Funding zero;zero.valid=true;
                const int setup=preparation(snapshot);
                if(preparation_safe(supported->offer,projected.frame,setup)&&
                    native::comparison(*compared,in,supported->offer,in.offers[0],zero,setup)&&
                    p6::compare(common::evaluate(in,compared->changed),common::evaluate(in,compared->unchanged))>0) {
                    if(recurring_preparation_started_<0)recurring_preparation_started_=now;
                    return_spawn(snapshot);return true;
                }
            }
        }
        return false;
    }
    bool unchanged(const sdk::Snapshot& before,const sdk::Snapshot& after) const {
        if(!after.valid||before.frame.clock.round!=after.frame.clock.round||before.frame.clock.self!=after.frame.clock.self||
            before.own.equipment_hash!=after.own.equipment_hash||before.own.health!=after.own.health||
            before.own.max_health!=after.own.max_health||before.own.strength!=after.own.strength||
            before.own.focus!=after.own.focus||before.own.growth!=after.own.growth||before.own.bag!=after.own.bag||
            before.frame.own.gold!=after.frame.own.gold||before.frame.own.actor.position!=after.frame.own.actor.position)return false;
        for(int i=0;i<input_->offers[0].schedule.actor_count;++i) {
            const int id=input_->offers[0].schedule.actors[i].actor_id;
            const auto* a=before.frame.visible(id);const auto* b=after.frame.visible(id);
            if(!a||!b||a->health!=b->health||a->max_health!=b->max_health||a->strength!=b->strength||
                a->bag!=b->bag||a->position!=b->position||a->channel!=b->channel||a->alive!=b->alive)return false;
        }
        return true;
    }
public:
    void fault() {
        work_.request_stop();work_.route_witness_active=false;
        if(ledger_&&ledger_->active())ledger_->invalidate_active(int(cambc::round_number()));
    }
    void turn() {
        const int entered=int(cambc::round_number());
        auto room=[entered](std::uint64_t reserve) {
            return int(cambc::round_number())==entered&&cambc::fuel_remaining()>=reserve;
        };
        // The shared geometry keeps current melee helpers able to reach it;
        // ranged-only distance advantages are outside this bounded subset.
        if(!routes_.initialized()) {
            if(!room(70000))return;
            if(!diagnostic_routes_started_&&room(32000)) {
                std::printf("p8-04 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                    entered,static_cast<unsigned long long>(cambc::fuel_remaining()));
                diagnostic_routes_started_=true;
            }
            if(!room(70000))return;
            const bool complete=routes_.initialize_step(1);
            if(complete&&room(32000))
                std::printf("p8-04 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                    entered,static_cast<unsigned long long>(cambc::fuel_remaining()));
            return;
        }
        // Seed initialized model/request storage on its own fresh turn. Do not
        // require this reserve after the ordinary observation/population costs.
        if(!primary_model_) {
            if(room(70000))primary_model_=make_default<SoloStorage>();
            return;
        }
        // Seed one persistent menu slot per fresh turn. Four slots preserve
        // own plus the first three peers when self has a later draft ordinal.
        // reserve allocates raw capacity without constructing the other Solos.
        if(menu_models_.size()<4) {
            if(room(70000)) {
                if(menu_models_.capacity()<4)menu_models_.reserve(4);
                menu_models_.emplace_back();
            }
            return;
        }
        if(!ordinary_local_) {
            if(room(70000))ordinary_local_=make_default<LocalFrame>();
            return;
        }
        // Numeric samples are emitted on a later fresh turn, before optional
        // preparation. They never become a proposal or action permission.
        if(ordinary_logged_<ordinary_count_&&room(85000)) {
            const auto& p=ordinary_samples_[ordinary_logged_++];
            std::printf("p8-04 DIAG ordinary_admission v=1 actor=%d input=%d ordinal=%u group=%d reason=%d option=%d rejection=%d started=%d debited=%d mask=%u o0=%d:%llu o1=%d:%llu o2=%d:%llu o3=%d:%llu o4=%d:%llu o5=%d:%llu o6=%d:%llu o7=%d:%llu o8=%d:%llu\n",
                sdk::identity(cambc::self()),p.input,p.ordinal,p.group,p.reason,p.option,p.rejection,
                int(p.started),int(p.debited),p.mask,p.round[0],p.fuel[0],p.round[1],p.fuel[1],
                p.round[2],p.fuel[2],p.round[3],p.fuel[3],p.round[4],p.fuel[4],p.round[5],p.fuel[5],
                p.round[6],p.fuel[6],p.round[7],p.fuel[7],p.round[8],p.fuel[8]);
            if(p.debited)std::printf("p8-04 ADMIT group=%d organization=%d episode=0 ordinary_wait=1 input=%d\n",p.group,int(Organization::U),p.input);
        } else if(pipeline_logged_<pipeline_count_&&room(85000)) {
            const auto& p=pipeline_samples_[pipeline_logged_];
            std::printf("p8-04 DIAG pipeline v=4 actor=%d entered=%d returned=%d stage=%d main=%d feasible=%d milestones=%u attempts=%u attempt=%d:%llu forecast=%d forecast_fuel=%llu fb0=%llu fb1=%llu fb2=%llu fallback=%d refused=%d p0=%d:%llu p1=%d:%llu p2=%d:%llu p3=%d:%llu p4=%d:%llu p5=%d:%llu p6=%d:%llu p7=%d:%llu p8=%d:%llu p9=%d:%llu p10=%d:%llu p11=%d:%llu p12=%d:%llu p13=%d:%llu p14=%d:%llu p15=%d:%llu p16=%d:%llu p17=%d:%llu exit=%d:%llu\n",
                sdk::identity(cambc::self()),p.entered,p.returned,p.stage,p.main_stage,int(p.feasible),
                p.milestones,p.primary_attempts,p.first_primary_round,p.first_primary_fuel,
                int(p.forecast_entered),p.first_forecast_fuel,p.primary_budget[0],p.primary_budget[1],p.primary_budget[2],
                p.fallback_outcome,int(p.budget_refused),
                p.round[0],p.fuel[0],p.round[1],p.fuel[1],p.round[2],p.fuel[2],p.round[3],p.fuel[3],
                p.round[4],p.fuel[4],p.round[5],p.fuel[5],p.round[6],p.fuel[6],p.round[7],p.fuel[7],
                p.round[8],p.fuel[8],p.round[9],p.fuel[9],p.round[10],p.fuel[10],p.round[11],p.fuel[11],
                p.round[12],p.fuel[12],p.round[13],p.fuel[13],p.round[14],p.fuel[14],p.round[15],p.fuel[15],
                p.round[16],p.fuel[16],p.round[17],p.fuel[17],p.returned,p.exit);
            ++pipeline_logged_;
        } else if(forecast_logged_<forecast_count_&&room(85000)) {
            const auto& p=forecast_samples_[forecast_logged_];
            std::printf("p8-04 DIAG forecast_inner v=1 actor=%d input=%d pair=%d ordinal=%u prior_returns=%u reasons=%u mask=%u complete_current=%d raw_status=%d raw_feasible=%d solo_feasible=%d guard=%d usable_primary=%d rb0=%llu rb1=%llu rb2=%llu b0=%d:%llu b1=%d:%llu b2=%d:%llu b3=%d:%llu b4=%d:%llu b5=%d:%llu b6=%d:%llu b7=%d:%llu b8=%d:%llu\n",
                sdk::identity(cambc::self()),p.input_round,p.pair,p.ordinal,p.prior_returns,p.reasons,p.mask,
                int(p.complete_current),p.raw_status,int(p.raw_feasible),int(p.solo_feasible),int(p.guard),int(p.usable_primary),
                p.solo_budget[0],p.solo_budget[1],p.solo_budget[2],
                p.round[0],p.fuel[0],p.round[1],p.fuel[1],p.round[2],p.fuel[2],p.round[3],p.fuel[3],
                p.round[4],p.fuel[4],p.round[5],p.fuel[5],p.round[6],p.fuel[6],p.round[7],p.fuel[7],p.round[8],p.fuel[8]);
            ++forecast_logged_;
        } else if(menu_logged_<menu_count_&&room(85000)) {
            const auto& p=menu_samples_[menu_logged_];
            std::printf("p8-04 DIAG menu_inner v=2 actor=%d input=%d ordinal=%u prior_returns=%u reasons=%u mask=%u returned=%d result=%d baselines=%u attempts=%u invite=%d observed_allies=%d queue0=%d result0=%d queue1=%d result1=%d b0=%d:%llu b1=%d:%llu a00=%d:%llu a01=%d:%llu a02=%d:%llu a10=%d:%llu a11=%d:%llu a12=%d:%llu exit=%d:%llu q00=%llu q01=%llu q02=%llu q10=%llu q11=%llu q12=%llu\n",
                sdk::identity(cambc::self()),p.input_round,p.ordinal,p.prior_returns,p.reasons,p.mask,
                int(p.returned),p.result,p.baseline_count,p.attempts,int(p.invite),int(p.observed_allies),
                p.queue_result[0],p.attempt_result[0],p.queue_result[1],p.attempt_result[1],
                p.round[0],p.fuel[0],p.round[1],p.fuel[1],p.round[2],p.fuel[2],p.round[3],p.fuel[3],
                p.round[4],p.fuel[4],p.round[5],p.fuel[5],p.round[6],p.fuel[6],p.round[7],p.fuel[7],p.round[8],p.fuel[8],
                static_cast<unsigned long long>(p.queue_budget[0][0]),static_cast<unsigned long long>(p.queue_budget[0][1]),
                static_cast<unsigned long long>(p.queue_budget[0][2]),static_cast<unsigned long long>(p.queue_budget[1][0]),
                static_cast<unsigned long long>(p.queue_budget[1][1]),static_cast<unsigned long long>(p.queue_budget[1][2]));
            ++menu_logged_;
        } else if(decision_logged_<decision_count_&&room(85000)) {
            const auto& p=decision_samples_[decision_logged_];
            std::printf("p8-04 DIAG decision_inner v=2 actor=%d input=%d ordinal=%u prior_returns=%u reasons=%u mask=%u returned=%d full=%d current=%d same_round=%d monotone=%d offers=%d result=%d choose=%d market=%d legal0=%d crew0=%d design0=%d detail_option=0 d0=%d:%llu d1=%d:%llu d2=%d:%llu d3=%d:%llu d4=%d:%llu d5=%d:%llu d6=%d:%llu d7=%d:%llu d8=%d:%llu d9=%d:%llu d10=%d:%llu d11=%d:%llu d12=%d:%llu d13=%d:%llu d14=%d:%llu d15=%d:%llu d16=%d:%llu d17=%d:%llu\n",
                sdk::identity(cambc::self()),p.input_round,p.ordinal,p.prior_returns,p.reasons,p.mask,
                int(p.returned),int(p.full),int(p.current),int(p.same_round),int(p.monotone),
                p.offer_count,p.option,p.choose_option,p.market,p.legal0,p.crew0,p.design0,
                p.round[0],p.fuel[0],p.round[1],p.fuel[1],p.round[2],p.fuel[2],p.round[3],p.fuel[3],
                p.round[4],p.fuel[4],p.round[5],p.fuel[5],p.round[6],p.fuel[6],p.round[7],p.fuel[7],
                p.round[8],p.fuel[8],p.round[9],p.fuel[9],p.round[10],p.fuel[10],p.round[11],p.fuel[11],
                p.round[12],p.fuel[12],p.round[13],p.fuel[13],p.round[14],p.fuel[14],
                p.round[15],p.fuel[15],p.round[16],p.fuel[16],p.round[17],p.fuel[17]);
            ++decision_logged_;
        }
        PipelineScope pipeline{pipeline_samples_,pipeline_count_,pipeline_seen_};
        if(!work_.active&&room(70000))routes_.prepare();
        pipeline.mark(1);
        if(!room(30000)){pipeline.budget_refused=true;return;}
        const bool ordinary_idle=!work_.active&&!workshop_.declared&&input_->draft.valid()&&
            !collector_actor(input_->frame.own.actor,input_->draft);
        const auto* group=selected_site_<0||ordinary_idle?nullptr:&routes_.sites[selected_site_].group;
        auto snapshot=adapter_.observe(group,&work_);pipeline.mark(2);
        if(!snapshot.valid){fault();return;}
        if(!ledger_)ledger_=std::make_unique<EvidenceLedger>(snapshot.frame.clock.self);
        pipeline.mark(3);
        const bool skipped=last_round_>=0&&snapshot.frame.clock.round!=last_round_+1;
        if(snapshot.frame.clock.round==last_round_)return;
        last_round_=snapshot.frame.clock.round;
        if(work_.active)populate(snapshot);
        pipeline.mark(4);
        if(work_.active) {
            // Before any experimental issue, an ordinary startup job may stop
            // at its actual empty safe spawn. It gains no renewal receipt.
            if(!workshop_.declared&&!ledger_->active()&&collector_actor(snapshot.frame.own.actor,snapshot.draft)&&
                snapshot.frame.own.at_spawn&&snapshot.own.bag==0&&snapshot.frame.own.actor.channel==p6::Channel::none&&calm(snapshot)) {
                work_.invalidate(PhysicalFailure::unavailable);
            } else {
                const auto certificates=current_safety(snapshot);bool safe=false;
                for(int i=0;i<initial_safety_count_;++i)if(certificates[i].actor==snapshot.frame.clock.self)
                    safe=certificates[i].valid(snapshot.frame,snapshot.frame.clock.self,work_.plan.group);
                safe&=threats(work_.plan.group)==threat_;
                if(ordinary_wait_pending_) {
                    const auto& old=ordinary_entry_;const auto& now=snapshot.own;
                    const bool same_build=old.equipment_hash==now.equipment_hash&&old.max_health==now.max_health&&
                        old.strength==now.strength&&old.focus==now.focus&&old.growth==now.growth;
                    safe&=same_build;
                    if(!same_build)work_.request_stop();
                    ordinary_wait_pending_=false;
                }
                auto local=adapter_.local(snapshot,work_.plan,{certificates.data(),std::size_t(initial_safety_count_)},
                    safe,calm(snapshot),productive(snapshot),escape(snapshot),&work_);local.fuel_resumed|=skipped;
                if(!room(16000)||int(cambc::round_number())!=snapshot.frame.clock.round) {
                    work_.request_stop();return;
                }
                local.fuel_remaining=std::int64_t(cambc::fuel_remaining());
                if(skipped){work_.request_stop();if(ledger_->active())ledger_->invalidate_active(snapshot.frame.clock.round);}
                const auto command=workshop_.advance(*input_,local,*ledger_,work_);dispatch(command,local);
                trace_.observe(local,work_,snapshot.spawn_shop_legal);
                if(trace_.active&&(!work_.active||!ledger_->active())) {
                    const auto* episode=ledger_->active();Premises terminal;
                    if(episode)terminal={episode->premises.key,snapshot.own,all_premises};
                    if(!work_.route_witness_active)terminal.personally_observed&=~route_known;
                    // A collector never replaces lost membership with a known
                    // premise. SDK attack readiness is not useful_attack; the
                    // inherited trace requires an actual personally witnessed
                    // helper action, currently unavailable in the thin bridge.
                    for(int i=0;i<terminal.key.participant_count;++i)if(!snapshot.frame.visible(terminal.key.participants[i]))
                        terminal.personally_observed&=~membership_known;
                    const auto result=trace_.finish(*ledger_,snapshot.frame.clock.round,terminal);
                    workshop_.observe_result(result,*ledger_);
                    if(result.accepted)std::cout<<"p8-04 EPISODE positive="<<result.full_positive<<" contradiction="<<result.contradiction<<'\n';
                }
                return;
            }
        }
        if(snapshot.frame.own.actor.channel!=p6::Channel::none||!snapshot.frame.own.actor.alive||snapshot.own.bag>0) {fallback(snapshot,&pipeline);return;}
        if(initial_purchase(snapshot))return;
        if(!room(30000)){pipeline.budget_refused=true;return;}
        // initial_purchase may already have populated these exact unchanged
        // owner-turn inputs. No live market observation survives a new clock.
        const auto clock=snapshot.frame.clock;
        // Ordinary undeclared noncollectors cannot transact. They still bind
        // actual inventory to current stats; only transaction roles need quotes.
        const bool needs_quotes=snapshot.frame.own.at_spawn&&
            (collector_actor(snapshot.frame.own.actor,snapshot.draft)||workshop_.declared||workshop_.phase!=Phase::A);
        const bool current_input=input_->own_state.current(clock,clock.self)&&
            input_->market.inventory.current(clock,clock.self)&&
            (!needs_quotes||
                (input_->market.items[damage_item].buy_legal.current(clock,clock.self)&&
                 input_->market.items[survival_item].buy_legal.current(clock,clock.self)));
        if(!current_input)populate(snapshot,needs_quotes);
        if(preparation_or_shop(snapshot))return;
        pipeline.mark(5);
        if(!room(30000)){pipeline.budget_refused=true;return;}
        const bool incumbent=arm==Arm::active&&workshop_.declared&&(workshop_.phase==Phase::A||workshop_.phase==Phase::retired_A);
        const auto* own=primary_solo(snapshot,incumbent?a_site_:-1,incumbent?a_bank_:-1,pipeline);
        pipeline.mark(6);
        if(!own){fallback(snapshot,&pipeline);return;}
        configure(snapshot,*own);
        pipeline.mark(7);
        if(!build_menu(snapshot,*own)){fallback(snapshot,&pipeline);return;}
        pipeline.mark(8);
        if(admit_ordinary(snapshot,*own))return;
        auto decision=workshop_.declared||!collector_actor(snapshot.frame.own.actor,snapshot.draft)?[&]() {
            if(decision_count_>=decision_samples_.size()||decision_seen_==15)
                return workshop_.decide(*input_,*ledger_,arm);
            DecisionScope sample{decision_samples_,decision_count_,decision_seen_,decision_calls_,decision_returns_,
                                 snapshot.frame.clock.round,input_->offer_count};
            sample.mark(0);
            auto result=workshop_.decide(*input_,*ledger_,arm,sample.probe());
            sample.finish(result);
            return result;
        }():Decision{};
        if(!workshop_.declared&&collector_actor(snapshot.frame.own.actor,snapshot.draft)) {
            decision.option=0;decision.clock=snapshot.frame.clock;decision.organization=Organization::U;
        }
        if(decision.option<0){fallback(snapshot,&pipeline);return;}
        pipeline.mark(9);
        const auto planned=snapshot;const int action_group=input_->offers[decision.option].plan.group;
        int site=-1;for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)site=s;
        if(site<0)return;
        snapshot=adapter_.observe(&routes_.sites[site].group,&work_);
        pipeline.mark(10);
        if(!unchanged(planned,snapshot)){fault();return;}
        populate(snapshot);
        for(int o=0;o<input_->offer_count;++o)for(int c=0;c<input_->offers[o].safety_count;++c)
            input_->offers[o].safety[c].clock=snapshot.frame.clock;
        if(input_->offer_count>1)input_->visible_proposer.clock=snapshot.frame.clock;
        decision=workshop_.declared||!collector_actor(snapshot.frame.own.actor,snapshot.draft)?
            workshop_.decide(*input_,*ledger_,arm):decision;decision.clock=snapshot.frame.clock;
        if(decision.option<0||input_->offers[decision.option].plan.group!=action_group)return;
        auto& chosen=input_->offers[decision.option];
        auto local=adapter_.local(snapshot,chosen.plan,{chosen.safety.data(),std::size_t(chosen.safety_count)},
            true,calm(snapshot),true,escape(snapshot));
        pipeline.mark(11);
        Admission admitted;
        if(workshop_.declared||!collector_actor(snapshot.frame.own.actor,snapshot.draft))
            admitted=workshop_.commit(*input_,local,decision,*ledger_,arm,work_);
        else admitted.started=work_.start(chosen.plan,local);
        pipeline.mark(12);
        if(!admitted.started){fallback(snapshot,&pipeline);return;}
        recurring_preparation_started_=-1;
        selected_site_=site;selected_bank_=own->bank;renewal_site_=chosen.plan.organization==Organization::H&&
            chosen.plan.collector!=snapshot.frame.clock.self?own->site:site;
        initial_safety_=chosen.safety;initial_safety_count_=chosen.safety_count;threat_=threats(chosen.plan.group);
        if(admitted.episode&&!trace_.begin(*ledger_,work_)){ledger_->invalidate_active(snapshot.frame.clock.round);work_.request_stop();}
        std::cout<<"p8-04 ADMIT group="<<chosen.plan.group<<" organization="<<int(chosen.plan.organization)<<" episode="<<admitted.episode<<'\n';
        unavailable_logged_=false;inspection_turns_=0;inspection_site_=-1;
        const auto command=work_.step(local);dispatch(command,local);trace_.observe(local,work_,snapshot.spawn_shop_legal);
        pipeline.mark(13);
    }
};
inline void draft() {
    const auto own=cambc::draft::current_draft(cambc::team::ally);const int ordinal=int(own.size()),picks=int(cambc::draft::team_size());
    if(ordinal>=picks)return;
    DraftInputs in;in.ordinal=ordinal;in.picks=picks;in.prospective_actor=0;
    for(int i=0;i<ordinal&&i<5;++i)in.own_picks[i]=sdk::type(own[i]);
    in.settings=sdk::settings();in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
    const int window=std::min(common::forecast_window,in.settings.max_rounds);in.windows.deadlines=in.windows.windows={window,window,window};
    in.windows.sampled_actor_id=0;in.windows.sampled_round=0;in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto candidates=p8::native::draft_candidates(0,ordinal,true);
    const auto selected=reversible_workshop::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-04 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
    for(const auto type:draft_priority(ordinal)) {
        const auto pick=sdk::draft_type(type);if(std::find(own.begin(),own.end(),pick)==own.end()){cambc::draft::pick(pick);return;}
    }
}
inline int run() {
    std::unique_ptr<Runtime> bot;
    while(true) {
        try {
            if(cambc::is_draft())native::draft();else {
                if(!bot) {
                    const int entered=int(cambc::round_number());
                    // Member defaults initialize private state without a leading whole-object zero-fill.
                    bot.reset(new Runtime);
                    if(int(cambc::round_number())==entered&&cambc::fuel_remaining()>=32000)
                        std::printf("p8-04 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                            entered,static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                bot->turn();
            }
        }
        catch(const cambc::error& e){std::cout<<"p8-04 ERROR engine "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& e){std::cout<<"p8-04 ERROR exception "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-04 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::reversible_workshop::native

