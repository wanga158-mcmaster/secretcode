#pragma once
#include "configuration.h"
#include "../../../common/src/defaults.h"
#include "../draft.h"
#include <bit>
#include <cstdio>
#include <iostream>
#include <memory>
namespace p8::circuitbreaker::native {
class Runtime {
    Routes routes_;sdk::Adapter adapter_;std::unique_ptr<Inputs> input_=p8::native::make_default<Inputs>();
    std::unique_ptr<EvidenceLedger> ledger_;Circuit circuit_;JTrace trace_;Work work_;p6::ScoreHistory scores_;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};int initial_safety_count_=0;
    int selected_site_=-1,renewal_site_=-1,last_round_=-1;
    int package_site_=-1,bootstrap_bank_=-1;std::array<int,2> package_banks_{-1,-1};
    int inspection_turns_=0,next_scan_=0,inspection_site_=-1,inspection_cursor_=0;
    Route inspection_{};std::uint64_t threat_=0;bool unavailable_logged_=false,proof_limit_logged_=false;
    bool replacement_shop_=false;std::uint64_t replacement_generation_=0;int replacement_completed_=-1;
    std::array<bool,2> retired_logged_{};std::uint16_t renewals_logged_=0;
    bool control_dynamic_=false;int dynamic_offer_=-1;Subsystem control_system_{};
    bool diagnostic_routes_started_=false;
    bool bank_unlocked(int bank) const {
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))
            if(point(lock.position())==routes_.banks[bank].altar)return false;
        return true;
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
    void fallback(sdk::Snapshot& snapshot) {
        const bool safe=calm(snapshot);
        Plan route;route.collector=snapshot.frame.clock.self;
        route.group=inspection_site_>=0?group_id(routes_.sites[inspection_site_].group):-1;
        if (safe&&snapshot.frame.own.actor.channel!=p6::Channel::none&&snapshot.frame.own.actor.channel_valid) {
            auto local=adapter_.local(snapshot,route,{},safe,safe,false);
            manual(route,local,PhysicalAction::preserve_channel);return;
        }
        // Existing inventory is secured before attempting another gem source.
        if (snapshot.frame.own.actor.bag>0&&safe) {
            int best=-1;Route bank_path;
            for (int i=0;i<int(routes_.banks.size());++i) {
                if(!bank_unlocked(i))continue;
                const auto path=routes_.route(snapshot.frame.own.actor.position,routes_.banks[i].approach);
                if (route_valid(path)&&(best<0||path.count<bank_path.count)) {best=i;bank_path=path;}
            }
            if (best>=0) {
                route.altar=routes_.banks[best].altar.y*cambc::map::width()+routes_.banks[best].altar.x;route.bank=bank_path;
                auto local=adapter_.local(snapshot,route,{},safe,safe,false);
                const bool arrived=bank_path.count==1;
                manual(route,local,arrived?PhysicalAction::tribute:PhysicalAction::move,
                    arrived?p6::Point{}:bank_path.tiles[1],Leg::bank);return;
            }
        }
        const int now=snapshot.frame.clock.round;
        if (safe&&now>=next_scan_&&inspection_turns_<8) {
            if (inspection_site_<0) {
                for (int i=0;i<int(routes_.sites.size());++i) {
                    const auto path=routes_.route(snapshot.frame.own.actor.position,routes_.sites[i].approach);
                    if (route_valid(path)&&(inspection_site_<0||path.count<inspection_.count)) {
                        inspection_site_=i;inspection_=path;inspection_cursor_=0;
                    }
                }
            }
            if (inspection_site_>=0&&route_valid(inspection_)) {
                route.ingress=inspection_;route.group=group_id(routes_.sites[inspection_site_].group);
                const auto pos=snapshot.frame.own.actor.position;
                if (inspection_cursor_+1<inspection_.count&&pos==inspection_.tiles[inspection_cursor_+1])++inspection_cursor_;
                if (pos==inspection_.tiles[inspection_cursor_]) {
                    ++inspection_turns_;auto local=adapter_.local(snapshot,route,{},safe,safe,false);
                    const bool arrived=inspection_cursor_+1==inspection_.count;
                    manual(route,local,arrived?PhysicalAction::inspect:PhysicalAction::move,
                        arrived?p6::Point{}:inspection_.tiles[inspection_cursor_+1]);return;
                }
            }
            inspection_turns_=8;
        }
        if (inspection_turns_>=8) {
            if (!unavailable_logged_) {std::cout<<"p8 OPPORTUNITY_UNAVAILABLE\n";unavailable_logged_=true;}
            next_scan_=now+8;inspection_turns_=0;inspection_site_=-1;
        }
        const auto exit=escape(snapshot);
        auto local=adapter_.local(snapshot,route,{},safe,safe,false,exit);
        manual(route,local,snapshot.frame.own.at_spawn?PhysicalAction::recover:
            !safe&&exit?PhysicalAction::escape:safe?PhysicalAction::recall:PhysicalAction::yield,exit.value_or(p6::Point{}));
    }

    std::unique_ptr<Solo> best_solo(const p6::Frame& frame,int fixed=-1,int bank=-1,std::span<const int> excluded={}) {
        std::unique_ptr<Solo> best;
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>55000;++s) {
            if(fixed>=0&&s!=fixed)continue;
            if(std::find(excluded.begin(),excluded.end(),group_id(routes_.sites[s].group))!=excluded.end())continue;
            for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>55000;++b) {
                if(bank>=0&&b!=bank)continue;
                if(!bank_unlocked(b))continue;
                auto model=std::make_unique<Solo>(solo(routes_,s,b,frame,true));
                if(model->feasible&&(!best||model->plan.forecast_rounds<best->plan.forecast_rounds||
                    (model->plan.forecast_rounds==best->plan.forecast_rounds&&model->plan.group<best->plan.group)))best=std::move(model);
            }
        }
        return best;
    }
    void populate(const sdk::Snapshot& snapshot) {
        auto& in=*input_;in.frame=snapshot.frame;in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        if(in.settings_identity.source!=p6::Source::queried_setting) {
            in.settings=sdk::settings();
            in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
        }
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};in.spawn_shop_legal=snapshot.spawn_shop_legal;
        const int target=circuit_.declared?circuit_.package.systems[circuit_.selected].plan.group:
            selected_site_>=0?group_id(routes_.sites[selected_site_].group):0;
        in.target_group=target;in.threat_regime={int(threats(target)&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const auto enemy=cambc::global_submitted_gems(cambc::team::enemy);scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);in.member_states={};in.actual_change={};
        int count=0;
        // Pinned item_count is visible-only. No ally gold, remote inventory or
        // readiness-based attack receipt is queried or fabricated here.
        for(const auto& ally:cambc::vision::champions(cambc::team::ally)) {
            const int id=sdk::identity(ally);if(id==snapshot.frame.clock.self||count==6||!snapshot.frame.visible(id)||!ally.is_alive())continue;
            const auto stats=ally.base_stats();NumericState state{double(ally.health()),double(ally.max_health()),double(ally.strength()),double(ally.focus()),
                double(stats.strength_growth),std::int64_t(ally.gem_count()),sdk::equipment_identity(ally)};
            if(valid_numeric_state(state))in.member_states[count++]={state,snapshot.frame.clock,id,p6::Knowledge::exact};
        }
        if(in.offer_count==0){in.offer_count=1;in.independent=0;}
        if(circuit_.declared)bind(in,circuit_);
    }
    std::vector<Solo> baselines(const sdk::Snapshot& snapshot,const Solo& own) {
        std::vector<Solo> models;models.reserve(3);models.push_back(own);normalize(models.back(),snapshot.frame);
        for(int ordinal=0;ordinal<3&&ordinal<snapshot.draft.picks;++ordinal) {
            const int id=snapshot.draft.actors[ordinal];if(id==snapshot.frame.clock.self)continue;
            const auto* actor=snapshot.frame.visible(id);if(!actor||!actor->alive||actor->channel!=p6::Channel::none)return {};
            std::array<int,3> excluded{};int count=0;for(const auto& model:models)excluded[count++]=model.plan.group;
            auto model=best_solo(subject(snapshot.frame,*actor),-1,-1,{excluded.data(),std::size_t(count)});
            if(!model)return {};normalize(*model,snapshot.frame);models.push_back(std::move(*model));
        }
        return models;
    }
    bool configure(const sdk::Snapshot& snapshot,const Solo& own) {
        if(circuit_.declared||ledger_->size()!=0||snapshot.draft.picks<3||snapshot.frame.own.actor.pick_ordinal>=3||
            snapshot.own.bag||!routes_.spawn.ready||routes_.banks.size()<2)return false;
        for(int ordinal=0;ordinal<3;++ordinal) {
            const auto* actor=snapshot.frame.visible(snapshot.draft.actors[ordinal]);
            if(!actor||!actor->alive||actor->bag||actor->channel!=p6::Channel::none||!member_state(*input_,actor->id))return false;
        }
        const auto& site=routes_.sites[own.site];if(site.rally_count<3||site.joint_capture_count<3)return false;
        input_->target_group=own.plan.group;
        input_->threat_regime={int(threats(own.plan.group)&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        auto canonical=snapshot.frame;canonical.own.actor.position=site.approach.destination;
        auto opening=std::make_unique<Solo>(solo(routes_,own.site,own.bank,canonical,true));if(!opening->feasible)return false;
        auto models=baselines(snapshot,own);if(models.size()!=3)return false;
        int chosen=-1; p6::ScoreEvaluation rank;
        for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>70000;++b) {
            if(!bank_unlocked(b))continue;
            auto joint=std::make_unique<Coalition>(coalition(routes_,own.site,b,canonical,snapshot.draft,models,Organization::J,true));
            if(!joint->feasible)continue;const auto score=common::evaluate(*input_,joint->offer.schedule);
            if(score.eligible()&&(chosen<0||p6::compare(score,rank)>0)){chosen=b;rank=score;}
        }
        if(chosen<0)return false;int changed=-1;
        for(int b=0;b<int(routes_.banks.size());++b)if(routes_.banks[b].altar!=routes_.banks[chosen].altar&&routes_.banks[b].approach.ready){changed=b;break;}
        if(changed<0)return false;
        auto package=std::make_unique<Package>();package->bootstrap=opening->plan;
        package->ordinary=declaration(opening->plan,*input_,false,site.group.gem_value());
        if(!subsystem(package->systems[0],routes_,own.site,chosen,*input_)||
            !subsystem(package->systems[1],routes_,own.site,changed,*input_)||
            int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000||
            !circuit_.declare(*package,snapshot.draft,*ledger_))return false;
        package_site_=own.site;bootstrap_bank_=own.bank;package_banks_={chosen,changed};bind(*input_,circuit_);
        std::cout<<"p8-08 DECLARED group="<<package->systems[0].plan.group<<" old_altar="<<package->systems[0].plan.altar
            <<" replacement_altar="<<package->systems[1].plan.altar<<'\n';return true;
    }
    bool locked_old_altar() const {
        if(!circuit_.declared||package_banks_[0]<0||package_banks_[1]<0||
            routes_.banks[package_banks_[0]].altar==routes_.banks[package_banks_[1]].altar)return false;
        const auto old=tile(routes_.banks[package_banks_[0]].altar),fresh=tile(routes_.banks[package_banks_[1]].altar);
        const auto visible=cambc::vision::visible_tiles();if(!visible.is_set(old)||!visible.is_set(fresh))return false;
        bool blocked=false;for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy)) {
            if(lock.position()==fresh)return false;if(lock.position()==old)blocked=true;
        }
        return blocked;
    }
    bool menu(const sdk::Snapshot& snapshot,const Solo& own) {
        auto& in=*input_;in.offer_count=1;in.independent=0;in.visible_proposer={};dynamic_offer_=-1;
        auto models=baselines(snapshot,own);if(models.empty()){models.push_back(own);normalize(models.back(),snapshot.frame);}
        if(!independent_offer(in.offers[0],own,models,snapshot.frame))return false;
        if(!circuit_.declared)return true;
        for(int i=0;i<2&&cambc::fuel_remaining()>65000;++i) {
            auto ordinary=std::make_unique<Solo>();
            if(!i&&bank_unlocked(bootstrap_bank_))*ordinary=solo(routes_,package_site_,bootstrap_bank_,snapshot.frame,true);
            else if(i&&bank_unlocked(package_banks_[1]))*ordinary=retained_solo(routes_,package_site_,package_banks_[1],snapshot.frame,circuit_.package.systems[1].plan);
            if(!ordinary->feasible)continue;
            auto alternatives=baselines(snapshot,*ordinary);if(alternatives.empty())continue;
            auto& offered=in.offers[in.offer_count];if(!independent_offer(offered,*ordinary,alternatives,snapshot.frame))continue;
            ++in.offer_count;
        }
        // Attach bootstrap evidence only to its exact still-current route. The
        // same short independent alternatives remain in the represented menu.
        for(int i=0;i<in.offer_count;++i)if(plan_route_hash(in.offers[i].plan)==circuit_.package.ordinary.key.route_hash) {
            in.offers[i].context_slot=circuit_.ordinary;in.offers[i].entry=circuit_.package.ordinary.key;
        }
        if(models.size()!=3||snapshot.frame.own.actor.pick_ordinal>=3)return true;
        for(int which=0;which<2&&in.offer_count<common::max_options&&cambc::fuel_remaining()>45000;++which) {
            if(!bank_unlocked(package_banks_[which]))continue;
            auto joint=std::make_unique<Coalition>(coalition(routes_,package_site_,package_banks_[which],snapshot.frame,snapshot.draft,models,Organization::J,true));
            if(!joint->feasible||plan_route_hash(joint->offer.plan)!=circuit_.package.systems[which].declaration.key.route_hash)continue;
            auto& offered=in.offers[in.offer_count++];offered=joint->offer;offered.context_slot=circuit_.systems[which].slot;
            offered.entry=circuit_.package.systems[which].declaration.key;
            if(snapshot.frame.clock.self!=offered.plan.collector)in.visible_proposer={offered.plan.group,snapshot.frame.clock,offered.plan.collector,p6::Knowledge::exact};
        }
        if(arm==Arm::control&&circuit_.bootstrapped) {
            auto best=std::make_unique<Coalition>();int best_site=-1;
            for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>70000;++s)for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>70000;++b) {
                if(!bank_unlocked(b)||routes_.sites[s].rally_count<3||routes_.sites[s].joint_capture_count<3)continue;
                const auto* leader=snapshot.frame.visible(snapshot.draft.actors[0]);
                if(snapshot.frame.clock.self!=snapshot.draft.actors[0]&&(!leader||leader->position!=routes_.sites[s].rally[0].destination))continue;
                auto candidate=std::make_unique<Coalition>(coalition(routes_,s,b,snapshot.frame,snapshot.draft,models,Organization::J,true));
                if(!candidate->feasible)continue;
                if(best_site<0||p6::compare(common::evaluate(in,candidate->offer.schedule),common::evaluate(in,best->offer.schedule))>0) {
                    *best=std::move(*candidate);best_site=s;
                }
            }
            if(best_site>=0&&in.offer_count<common::max_options&&subsystem_plan(control_system_,best->offer.plan,in,routes_.sites[best_site].group.gem_value())) {
                dynamic_offer_=in.offer_count;in.offers[in.offer_count++]=best->offer;
                if(snapshot.frame.clock.self!=best->offer.plan.collector)
                    in.visible_proposer={best->offer.plan.group,snapshot.frame.clock,best->offer.plan.collector,p6::Knowledge::exact};
            }
        }
        return true;
    }
    void replacement_observation(const sdk::Snapshot& snapshot,const LocalFrame& local) {
        if(!circuit_.declared||work_.plan.organization!=Organization::U||
            plan_route_hash(work_.plan)!=circuit_.package.systems[1].declaration.key.route_hash)return;
        if(replacement_generation_!=work_.generation){replacement_generation_=work_.generation;replacement_shop_=false;}
        if(!local.fresh()||local.fuel_resumed||!work_.route_witness_active||work_.last_round!=snapshot.frame.clock.round) {
            replacement_shop_=false;return;
        }
        if(work_.bank_seen&&snapshot.frame.own.at_spawn&&snapshot.spawn_shop_legal.current(snapshot.frame.clock,snapshot.frame.clock.self)&&snapshot.spawn_shop_legal.value)
            replacement_shop_=true;
        if(work_.cycle_completed&&work_.payout_seen&&work_.bank_seen&&work_.renewal_seen&&replacement_shop_&&work_.payout_lower>0&&work_.bank_lower>0)
            replacement_completed_=snapshot.frame.clock.round;
    }
    void report_state() {
        for(int i=0;i<2;++i)if(circuit_.systems[i].retired&&!retired_logged_[i]) {
            retired_logged_[i]=true;std::cout<<"p8-08 RETIRED system="<<i<<" reason="<<int(circuit_.systems[i].reason)
                <<" round="<<circuit_.systems[i].retired_round<<'\n';
        }
        if(circuit_.independent_renewals!=renewals_logged_) {
            renewals_logged_=circuit_.independent_renewals;std::cout<<"p8-08 INDEPENDENT_RENEW count="<<renewals_logged_<<'\n';
        }
    }
    void observed(const sdk::Snapshot& snapshot,const LocalFrame& local) {
        replacement_observation(snapshot,local);
        const auto result=observe_cycle(*input_,local,circuit_,*ledger_,work_,trace_);
        if(result.accepted)std::cout<<"p8-08 EPISODE positive="<<result.full_positive<<" contradiction="<<result.contradiction<<'\n';
        if(work_.plan.organization==Organization::J&&!proof_limit_logged_) {
            proof_limit_logged_=true;std::cout<<"p8-08 PROOF_UNAVAILABLE ally_actual_attack_attribution; readiness is not evidence\n";
        }
        report_state();
    }
    void requalify(const sdk::Snapshot& snapshot) {
        if(arm!=Arm::active||!circuit_.declared||circuit_.selected!=0||circuit_.requalification_used||replacement_completed_<0||
            snapshot.frame.clock.round-replacement_completed_>=qualification_rounds||!locked_old_altar())return;
        const auto& old=circuit_.systems[0];const auto* slot=ledger_->state(old.slot);
        if(!slot||!slot->campaign_started||(!old.retired&&!old.material_changed&&!ledger_->campaign_exhausted(old.slot)))return;
        auto& in=*input_;ContextChange change;change.kind=ChangeKind::route;change.previous=circuit_.package.systems[0].declaration.key;
        change.current=circuit_.package.systems[1].declaration.key;change.inspected_route=change.current.route_hash;
        in.actual_change={change,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const auto local=adapter_.local(snapshot,work_.plan,{},calm(snapshot),calm(snapshot),true);
        if(int(cambc::round_number())==snapshot.frame.clock.round&&cambc::fuel_remaining()>=25000&&
            authorize_requalification(in,local,work_,circuit_,*ledger_,arm))
            std::cout<<"p8-08 REQUALIFICATION actual_locked_altar="<<change.previous.endpoint<<" route="<<change.current.route_hash<<'\n';
    }
    bool unchanged(const sdk::Snapshot& before,const sdk::Snapshot& after) const {
        if(!after.valid||before.frame.clock.round!=after.frame.clock.round||before.frame.clock.self!=after.frame.clock.self||
            before.own.equipment_hash!=after.own.equipment_hash||before.own.health!=after.own.health||before.own.max_health!=after.own.max_health||
            before.own.strength!=after.own.strength||before.own.focus!=after.own.focus||before.own.growth!=after.own.growth||before.own.bag!=after.own.bag||
            before.frame.own.gold!=after.frame.own.gold||before.frame.own.actor.position!=after.frame.own.actor.position)return false;
        for(int i=0;i<input_->offer_count;++i)for(int a=0;a<input_->offers[i].schedule.actor_count;++a) {
            const int id=input_->offers[i].schedule.actors[a].actor_id;const auto* x=before.frame.visible(id);const auto* y=after.frame.visible(id);
            if(!x||!y||x->health!=y->health||x->max_health!=y->max_health||x->strength!=y->strength||x->bag!=y->bag||
                x->alive!=y->alive||x->position!=y->position||x->channel!=y->channel)return false;
        }
        return true;
    }
    Decision decision() {
        auto& in=*input_;Decision out;out.clock=in.frame.clock;
        if(circuit_.declared)out=circuitbreaker::decide(in,circuit_,*ledger_,arm);
        else if(common::valid_inputs(in)&&common::legal_offer(in,in.offers[0])==Rejection::none&&common::crew_legal(in,in.offers[0],false)) {
            out.option=0;out.organization=Organization::U;out.score=common::evaluate(in,in.offers[0].schedule);out.reason=Rejection::none;
        }
        if(arm==Arm::control&&dynamic_offer_>=0&&circuit_.bootstrapped) {
            const auto& offered=in.offers[dynamic_offer_];
            if(common::legal_offer(in,offered)==Rejection::none&&common::crew_legal(in,offered,false)&&common::package_funded(in,offered)&&
                members_current(in,control_system_)&&(offered.plan.collector==in.frame.clock.self||common::helper_accepts(in,offered))) {
                const auto score=common::evaluate(in,offered.schedule);
                if(out.option<0||p6::compare(score,out.score)>0) {out.option=dynamic_offer_;out.organization=Organization::J;out.score=score;out.reason=Rejection::none;out.record_episode=false;}
            }
        }
        return out;
    }
public:
    void fault(){work_.request_stop();work_.route_witness_active=false;if(ledger_&&ledger_->active())ledger_->invalidate_active(int(cambc::round_number()));}
    void turn();
};
} // namespace p8::circuitbreaker::native

namespace p8::circuitbreaker::native {
inline void Runtime::turn() {
    const int entered=int(cambc::round_number());
    const auto room=[&](std::uint64_t reserve) {
        return int(cambc::round_number())==entered&&cambc::fuel_remaining()>=reserve;
    };
    if(!routes_.initialized()) {
        if(!room(70000))return;
        if(!diagnostic_routes_started_&&room(32000)) {
            diagnostic_routes_started_=true;
            std::printf("p8-08 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),entered,
                static_cast<unsigned long long>(cambc::fuel_remaining()));
        }
        if(!room(70000))return;
        const bool complete=routes_.initialize_step(1);
        if(complete&&room(32000))
            std::printf("p8-08 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),entered,
                static_cast<unsigned long long>(cambc::fuel_remaining()));
        return;
    }
    if(!work_.active&&room(70000))routes_.prepare();
    if(int(cambc::round_number())!=entered)return;
    const auto* selected=selected_site_<0?nullptr:&routes_.sites[selected_site_].group;
    auto snapshot=adapter_.observe(selected,&work_);if(!snapshot.valid){fault();return;}
    if(!ledger_)ledger_=std::make_unique<EvidenceLedger>(snapshot.frame.clock.self);
    const bool skipped=last_round_>=0&&snapshot.frame.clock.round!=last_round_+1;
    if(snapshot.frame.clock.round==last_round_)return;last_round_=snapshot.frame.clock.round;populate(snapshot);
    if(work_.active) {
        const auto certificates=current_safety(snapshot);bool safe=false;
        for(int i=0;i<initial_safety_count_;++i)if(certificates[i].actor==snapshot.frame.clock.self)
            safe=certificates[i].valid(snapshot.frame,snapshot.frame.clock.self,work_.plan.group);
        safe&=threats(work_.plan.group)==threat_;
        auto local=adapter_.local(snapshot,work_.plan,{certificates.data(),std::size_t(initial_safety_count_)},safe,calm(snapshot),productive(snapshot),escape(snapshot),&work_);
        local.fuel_resumed|=skipped;if(skipped)fault();Command command;
        if(control_dynamic_) {
            if(!common::valid_inputs(*input_)||!local.fresh()||local.fuel_resumed||!members_current(*input_,control_system_,true))work_.request_stop();
            command=work_.step(local);
        } else if(circuit_.declared)command=circuitbreaker::advance(*input_,local,circuit_,*ledger_,arm,work_);
        else command=work_.step(local);
        dispatch(command,local);observed(snapshot,local);return;
    }
    if(snapshot.frame.own.actor.channel!=p6::Channel::none||!snapshot.frame.own.actor.alive||snapshot.own.bag>0){fallback(snapshot);return;}
    auto own=best_solo(snapshot.frame);if(!own){fallback(snapshot);return;}
    configure(snapshot,*own);populate(snapshot);if(!menu(snapshot,*own)){fallback(snapshot);return;}
    requalify(snapshot);auto chosen=decision();if(chosen.option<0){fallback(snapshot);return;}
    const auto planned=snapshot;const auto member_before=input_->member_states;
    const int action_group=input_->offers[chosen.option].plan.group;int site=-1;
    for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)site=s;
    if(site<0)return;snapshot=adapter_.observe(&routes_.sites[site].group,&work_);
    if(!unchanged(planned,snapshot)){fault();return;}populate(snapshot);
    for(const auto& before:member_before)if(before.knowledge==p6::Knowledge::exact) {
        const auto* after=member_state(*input_,before.subject);const auto& old=before.value;
        if(!after||after->value.equipment_hash!=old.equipment_hash||after->value.health!=old.health||after->value.max_health!=old.max_health||
            after->value.strength!=old.strength||after->value.focus!=old.focus||after->value.growth!=old.growth||after->value.bag!=old.bag){fault();return;}
    }
    for(int i=0;i<input_->offer_count;++i)for(int c=0;c<input_->offers[i].safety_count;++c)input_->offers[i].safety[c].clock=snapshot.frame.clock;
    if(input_->visible_proposer.knowledge==p6::Knowledge::exact)input_->visible_proposer.clock=snapshot.frame.clock;
    chosen=decision();if(chosen.option<0||input_->offers[chosen.option].plan.group!=action_group)return;
    auto& offered=input_->offers[chosen.option];auto local=adapter_.local(snapshot,offered.plan,
        {offered.safety.data(),std::size_t(offered.safety_count)},true,calm(snapshot),true,escape(snapshot));
    if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<18000){fault();return;}
    control_dynamic_=arm==Arm::control&&chosen.option==dynamic_offer_;Admission admitted;
    if(control_dynamic_||!circuit_.declared) {
        admitted.started=physical_admissible(offered.plan,local)&&work_.start(offered.plan,local);
        if(admitted.started)circuit_.track_bootstrap(work_);
    } else admitted=circuitbreaker::commit(*input_,local,chosen,circuit_,*ledger_,arm,work_,trace_);
    if(!admitted.started){fallback(snapshot);return;}
    selected_site_=renewal_site_=site;
    initial_safety_=offered.safety;initial_safety_count_=offered.safety_count;threat_=threats(action_group);
    inspection_turns_=0;inspection_site_=-1;unavailable_logged_=false;
    std::cout<<"p8-08 ADMIT group="<<action_group<<" organization="<<int(offered.plan.organization)<<" episode="<<admitted.episode
        <<" selected="<<circuit_.selected<<" requalification_used="<<circuit_.requalification_used<<" control_current="<<control_dynamic_<<'\n';
    dispatch(work_.step(local),local);observed(snapshot,local);
}
inline void draft() {
    const auto own=cambc::draft::current_draft(cambc::team::ally);const int ordinal=int(own.size()),picks=int(cambc::draft::team_size());if(ordinal>=picks)return;
    DraftInputs in;in.ordinal=ordinal;in.picks=picks;in.prospective_actor=0;
    for(int i=0;i<ordinal&&i<5;++i)in.own_picks[i]=sdk::type(own[i]);
    in.settings=sdk::settings();in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
    const int window=std::min(120,in.settings.max_rounds);in.windows.deadlines=in.windows.windows={window,window,window};
    in.windows.sampled_actor_id=0;in.windows.sampled_round=0;in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto candidates=p8::native::draft_candidates(0,ordinal,true);const auto selected=circuitbreaker::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-08 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
    for(const auto champion:draft_priority(ordinal)) {const auto pick=sdk::draft_type(champion);
        if(std::find(own.begin(),own.end(),pick)==own.end()){cambc::draft::pick(pick);return;}}
}
inline int run() {
    std::unique_ptr<Runtime> bot;while(true) {
        try {
            if(cambc::is_draft())native::draft();else {
                const int entered=int(cambc::round_number());
                if(!bot) {
                    bot.reset(new Runtime);
                    if(int(cambc::round_number())==entered&&cambc::fuel_remaining()>=32000)
                        std::printf("p8-08 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),entered,
                            static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                if(int(cambc::round_number())==entered)bot->turn();
            }
        }
        catch(const cambc::error& e){std::cout<<"p8-08 ERROR engine "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& e){std::cout<<"p8-08 ERROR exception "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-08 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::circuitbreaker::native
