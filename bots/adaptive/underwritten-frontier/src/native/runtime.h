#pragma once
#include "configuration.h"
#include "../draft.h"
#include <bit>
#include <cstdio>
#include <iostream>
#include <memory>

namespace p8::underwritten_frontier::native {
using namespace p8::native;
// One concrete actor-local driver, not a remote insurer protocol. Active0
// always works independently; optional ordinal3 support is unavailable here
// and remains U. Shared range-one geometry permits current melee helpers.
// No SDK ally readiness flag is treated as a useful-attack evidence receipt.
class Runtime {
    Routes routes_;sdk::Adapter adapter_;
    std::unique_ptr<Inputs> input_=make_default<Inputs>();
    std::unique_ptr<EvidenceLedger> ledger_;ProbeBudget probes_;CycleTrace trace_;
    p6::ScoreHistory scores_;Work work_;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};int initial_safety_count_=0;
    int selected_site_=-1,selected_bank_=-1,renewal_site_=-1,last_round_=-1;
    int context_site_=-1,context_bank_=-1,context_slot_=-1;
    int inspection_turns_=0,next_scan_=0,inspection_site_=-1,inspection_cursor_=0;
    Route inspection_{};std::uint64_t threat_=0;bool unavailable_logged_=false;
    bool diagnostic_routes_started_=false;
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

    std::unique_ptr<Solo> best_solo(const p6::Frame& frame,int fixed=-1,int bank=-1,
                                    std::span<const int> excluded={}) {
        std::unique_ptr<Solo> best;
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>55000;++s) {
            if(fixed>=0&&s!=fixed)continue;
            if(std::find(excluded.begin(),excluded.end(),group_id(routes_.sites[s].group))!=excluded.end())continue;
            for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>55000;++b) {
                if(bank>=0&&b!=bank)continue;
                auto model=std::make_unique<Solo>(solo(routes_,s,b,frame,true));
                if(model->feasible&&(!best||model->plan.forecast_rounds<best->plan.forecast_rounds||
                    (model->plan.forecast_rounds==best->plan.forecast_rounds&&model->plan.group<best->plan.group)))best=std::move(model);
            }
        }
        return best;
    }
    void populate(const sdk::Snapshot& snapshot) {
        auto& in=*input_;in.frame=snapshot.frame;
        if(in.settings_identity.source!=p6::Source::queried_setting) {
            in.settings=sdk::settings();
            in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
        }
        in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        in.target_group=context_site_>=0?group_id(routes_.sites[context_site_].group):
            selected_site_>=0?group_id(routes_.sites[selected_site_].group):0;
        in.threat_regime={int(threats(in.target_group)&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const auto enemy=cambc::global_submitted_gems(cambc::team::enemy);
        scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.component_slot=-1;in.component_entry={};in.supported_slot=context_slot_;in.supported_entry={};
        if(const auto* slot=ledger_->state(context_slot_))in.supported_entry=slot->declaration.key;
        if(in.offer_count==0){in.offer_count=1;in.independent=0;}
    }
    void declare_context(const sdk::Snapshot& snapshot,const Solo& own) {
        if(arm!=Arm::active||snapshot.frame.own.actor.pick_ordinal!=2||context_slot_>=0||
            snapshot.draft.picks<3||snapshot.own.bag||ledger_->active())return;
        auto fixed=snapshot.frame;fixed.own.actor.position=routes_.sites[own.site].approach.destination;
        auto plan=std::make_unique<Plan>(formation_plan(routes_,own.site,own.bank,fixed,
            geometry_roster(snapshot.draft),Organization::H,true));
        if(!plan_valid(*plan,snapshot.frame.clock.self))return;
        const auto maximum=routes_.sites[own.site].group.gem_value();if(maximum<=0)return;
        input_->threat_regime={int(threats(plan->group)&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        auto d=declaration(*plan,snapshot,*input_,maximum);
        const int slot=ledger_->predeclare(d);if(slot<0)return;
        context_slot_=slot;context_site_=own.site;context_bank_=own.bank;populate(snapshot);
        std::cout<<"p8-06 DECLARED full_H="<<plan->group<<" insurer_excluded=1\n";
    }
    std::vector<Solo> alternatives(const sdk::Snapshot& snapshot,const Solo& own) {
        std::vector<Solo> models;models.reserve(3);models.push_back(own);normalize(models.back(),snapshot.frame);
        const int first=arm==Arm::active?1:0;
        for(int ordinal=first;ordinal<3;++ordinal) {
            const int id=snapshot.draft.actors[ordinal];if(id==snapshot.frame.clock.self)continue;
            const auto* actor=snapshot.frame.visible(id);
            if(!actor||!actor->alive||actor->channel!=p6::Channel::none||actor->bag!=0)return {};
            std::array<int,3> used{};int count=0;for(const auto& m:models)used[count++]=m.plan.group;
            auto model=best_solo(subject(snapshot.frame,*actor),-1,-1,{used.data(),std::size_t(count)});
            if(!model)return {};
            normalize(*model,snapshot.frame);models.push_back(std::move(*model));
        }
        return models;
    }
    bool build_menu(const sdk::Snapshot& snapshot,const Solo& own) {
        auto& in=*input_;in.offer_count=1;in.independent=0;
        in.venture_alternative=in.control_alternative=-1;in.loaded_return={};in.existing_inventory={};in.visible_proposer={};
        if(!independent_offer(in.offers[0],own,{&own,1},snapshot.frame))return false;
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        // Insurer labor, gold, production and safety never appear in either
        // active venture arm. Extensions retain their independent own job.
        if(snapshot.draft.picks<3||ordinal>2||(arm==Arm::active&&ordinal==0))return true;
        if(arm==Arm::active&&ordinal==2)if(const auto* context=ledger_->state(context_slot_)) {
            if(ledger_->suspended(context_slot_,snapshot.frame.clock.round/project_rounds)||
                (ledger_->campaign_exhausted(context_slot_)&&!ledger_->organization_qualified(
                    context_slot_,context->declaration.key,snapshot.frame.clock.round)))return true;
        }
        auto models=alternatives(snapshot,own);if(models.size()!=std::size_t(arm==Arm::active?2:3))return true;
        if(!independent_offer(in.offers[1],own,models,snapshot.frame))return true;
        in.offer_count=2;
        if(arm==Arm::active)in.venture_alternative=1;else in.control_alternative=1;
        const auto* collector=snapshot.frame.visible(snapshot.draft.actors[2]);if(!collector)return true;
        p6::ScoreEvaluation best_rank;std::unique_ptr<Coalition> best;int selected=-1;
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>80000;++s) {
            if(ordinal==2&&arm==Arm::active&&s!=context_site_)continue;
            if(ordinal!=2&&(routes_.sites[s].rally_count<2||collector->position!=routes_.sites[s].rally[0].destination))continue;
            for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>80000;++b) {
                if(ordinal==2&&arm==Arm::active&&b!=context_bank_)continue;
                auto candidate=std::make_unique<Coalition>(coalition(routes_,s,b,snapshot.frame,geometry_roster(snapshot.draft),
                    models,arm==Arm::active?Organization::H:Organization::J,true));
                if(!candidate->feasible)continue;
                const auto rank=evaluate(in,candidate->offer.schedule);
                if(rank.eligible()&&(!best||p6::compare(rank,best_rank)>0)) {
                    best_rank=rank;best=std::move(candidate);selected=s;
                }
            }
        }
        if(!best)return true;
        in.offers[2]=best->offer;in.offer_count=3;
        if(ordinal==2&&arm==Arm::active) {
            const auto* slot=ledger_->state(context_slot_);if(!slot){in.offer_count=2;return true;}
            in.offers[2].context_slot=context_slot_;in.offers[2].entry=slot->declaration.key;
            if(const auto* certificate=common::certificate(in.offers[2],snapshot.frame.clock.self))
                in.loaded_return=loaded(snapshot,slot->declaration,*certificate);
        }
        if(ordinal!=2&&selected>=0)
            in.visible_proposer={group_id(routes_.sites[selected].group),snapshot.frame.clock,collector->id,p6::Knowledge::exact};
        return true;
    }
    double ambient() const {
        double incoming=0;
        for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake())incoming+=camp.strength();
        for(const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
            double cast=0;for(const auto& ability:enemy.abilities())
                if(ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))cast=std::max(cast,double(ability.damage_amount()));
            incoming+=enemy.strength()+cast;
        }
        return incoming;
    }
    bool bank_only(const sdk::Snapshot& snapshot) {
        if(snapshot.own.bag<=0||!snapshot.frame.own.actor.alive||snapshot.frame.own.actor.channel!=p6::Channel::none||
            !routes_.spawn.ready)return false;
        const int now=snapshot.frame.clock.round;const auto at=snapshot.frame.own.actor.position;
        economy::TributeTiming timing{{cambc::constants::submit_gem_duration_multiplier,p6::Source::queried_setting},
            {cambc::constants::submit_gem_base_duration,p6::Source::queried_setting},
            {cambc::constants::submit_gem_minimum_duration,p6::Source::queried_setting}};
        const int service=economy::tribute_ticks(snapshot.own.bag,timing);
        const double healing=cambc::constants::spawn_healing;
        if(service<1||!std::isfinite(healing)||healing<=0)return false;
        const int recovery=int(std::ceil(1./std::min(1.,healing)))+1;
        if(recovery>9)return false;
        const double risk=ambient();int best_site=-1,best_bank=-1,best_time=121;
        auto best=std::make_unique<Offer>();
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>45000;++s) {
            const auto live=group(routes_.sites[s],snapshot.frame.clock);
            if(!live.original_spawn_visible||(live.lifecycle!=p6::Lifecycle::living&&live.lifecycle!=p6::Lifecycle::reward_available))continue;
            for(int b=0;b<int(routes_.banks.size());++b) {
                const auto altar=routes_.banks[b].altar;
                if(!cambc::vision::visible_tiles().is_set(tile(altar)))continue;
                bool locked=false;for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))
                    locked|=point(lock.position())==altar;
                if(locked)continue;
                auto candidate=std::make_unique<Offer>();auto& p=candidate->plan;
                p.organization=Organization::U;p.group=group_id(routes_.sites[s].group);p.collector=snapshot.frame.clock.self;
                p.crew[0]=p.collector;p.crew_count=1;p.altar=sdk::altar_id(routes_.banks[b].altar);
                p.ingress=p.capture=stay(at);p.bank=routes_.route(at,routes_.banks[b].approach);
                p.renewal=Routes::join(routes_.route(routes_.banks[b].approach.destination,routes_.spawn),
                    routes_.route(routes_.spawn.destination,routes_.sites[s].approach));
                if(!route_valid(p.bank)||!route_valid(p.renewal))continue;
                const int tribute=walking(p.bank)+service+1,finish=tribute+walking(p.renewal)+recovery;
                if(finish>=best_time||finish>std::min(forecast_window,int(cambc::constants::max_rounds)-now))continue;
                p.forecast_rounds=finish;p.remaining_rounds=std::min(forecast_window,int(cambc::constants::max_rounds)-now);
                p.capture_window=cambc::constants::capture_time;p.gem_bearing=p.complete_verified=p.rally_legal=true;
                auto& schedule=candidate->schedule;schedule.target_id=p.group;schedule.actor_count=1;
                auto& a=schedule.actors[0];a.actor_id=p.collector;a.initial_bag={snapshot.own.bag,p6::Source::self_observed};
                a.initial_gold={snapshot.frame.own.gold,p6::Source::self_observed};a.occupied_turns={finish,p6::Source::conservative};
                a.events[0]={p6::EventKind::tribute,1,tribute,-1,{0,p6::Source::queried_setting},-1,p6::Source::scheduled};
                a.events[1]={p6::EventKind::renewal,2,finish,-1,{0,p6::Source::queried_setting},p.group,p6::Source::scheduled};a.event_count=2;
                auto& c=candidate->safety[0];c.clock=snapshot.frame.clock;c.actor=p.collector;c.group=p.group;
                c.clear=c.hold={snapshot.own.health,snapshot.own.max_health,0,risk,0,0,true};
                c.bank={snapshot.own.health,snapshot.own.max_health,risk*tribute,risk,0,0,true};
                c.recovery={snapshot.own.health,snapshot.own.max_health,risk*finish,risk,0,0,true};
                c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true;candidate->safety_count=1;
                const economy::BankVisit visit{p.collector,p.bank.tiles[p.bank.count-1],snapshot.own.bag,risk,
                    p.renewal.tiles[std::min(1,p.renewal.count-1)],{p.bank.tiles.data(),std::size_t(p.bank.count)}};
                if(!apply_bank_queue(*candidate,{&visit,1},snapshot.frame))continue;
                best_time=candidate->plan.forecast_rounds;best_site=s;best_bank=b;best=std::move(candidate);
            }
        }
        if(best_site<0)return false;
        auto& in=*input_;in.offer_count=1;in.independent=0;in.offers[0]=*best;
        in.existing_inventory={*best,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const auto decision=underwritten_frontier::decide(in,*ledger_,probes_,arm);
        auto local=adapter_.local(snapshot,best->plan,{best->safety.data(),1},true,calm(snapshot),true,escape(snapshot));
        if(int(cambc::round_number())!=now||cambc::fuel_remaining()<18000)return false;
        const auto admitted=underwritten_frontier::commit(in,local,decision,*ledger_,probes_,arm,work_);
        if(!admitted.started)return false;
        selected_site_=renewal_site_=best_site;selected_bank_=best_bank;
        initial_safety_=best->safety;initial_safety_count_=1;threat_=threats(best->plan.group);
        dispatch(work_.step(local),local);return true;
    }
    bool unchanged(const sdk::Snapshot& before,const sdk::Snapshot& after) const {
        if(!after.valid||before.frame.clock.round!=after.frame.clock.round||before.frame.clock.self!=after.frame.clock.self||
            before.own.equipment_hash!=after.own.equipment_hash||before.own.health!=after.own.health||
            before.own.max_health!=after.own.max_health||before.own.strength!=after.own.strength||
            before.own.focus!=after.own.focus||before.own.growth!=after.own.growth||before.own.bag!=after.own.bag||
            before.frame.own.gold!=after.frame.own.gold||before.frame.own.actor.position!=after.frame.own.actor.position)return false;
        for(int o=0;o<input_->offer_count;++o)for(int i=0;i<input_->offers[o].schedule.actor_count;++i) {
            const int id=input_->offers[o].schedule.actors[i].actor_id;
            const auto* a=before.frame.visible(id);const auto* b=after.frame.visible(id);
            if(!a||!b||a->health!=b->health||a->max_health!=b->max_health||a->strength!=b->strength||
                a->bag!=b->bag||a->position!=b->position||a->channel!=b->channel||a->alive!=b->alive)return false;
        }
        return true;
    }
    void observe_trace(const sdk::Snapshot& snapshot,const LocalFrame& local) {
        trace_.observe(local,work_,snapshot.spawn_shop_legal);
        if(!trace_.active||(work_.active&&ledger_->active()))return;
        const auto* episode=ledger_->active();Premises terminal;
        if(episode)terminal={episode->premises.key,snapshot.own,all_premises};
        if(!work_.route_witness_active)terminal.personally_observed&=~route_known;
        for(int i=0;i<terminal.key.participant_count;++i)if(!snapshot.frame.visible(terminal.key.participants[i]))
            terminal.personally_observed&=~membership_known;
        const auto result=trace_.finish(*ledger_,snapshot.frame.clock.round,terminal);
        if(result.accepted)std::cout<<"p8-06 EPISODE positive="<<result.full_positive<<" contradiction="<<result.contradiction<<'\n';
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
        if(!routes_.initialized()) {
            if(!room(70000))return;
            if(!diagnostic_routes_started_&&room(32000)) {
                std::printf("p8-06 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                    entered,static_cast<unsigned long long>(cambc::fuel_remaining()));
                diagnostic_routes_started_=true;
            }
            if(!room(70000))return;
            const bool complete=routes_.initialize_step(1);
            if(complete&&room(32000))
                std::printf("p8-06 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                    entered,static_cast<unsigned long long>(cambc::fuel_remaining()));
            return;
        }
        if(!work_.active&&room(70000))routes_.prepare();
        if(!room(30000))return;
        const auto* selected=selected_site_<0?nullptr:&routes_.sites[selected_site_].group;
        auto snapshot=adapter_.observe(selected,&work_);if(!snapshot.valid){fault();return;}
        if(!ledger_)ledger_=std::make_unique<EvidenceLedger>(snapshot.frame.clock.self);
        const bool skipped=last_round_>=0&&snapshot.frame.clock.round!=last_round_+1;
        if(snapshot.frame.clock.round==last_round_)return;
        last_round_=snapshot.frame.clock.round;populate(snapshot);
        if(work_.active) {
            const auto certificates=current_safety(snapshot);bool safe=false;
            for(int i=0;i<initial_safety_count_;++i)if(certificates[i].actor==snapshot.frame.clock.self)
                safe=certificates[i].valid(snapshot.frame,snapshot.frame.clock.self,work_.plan.group);
            safe&=threats(work_.plan.group)==threat_;
            if(const auto* slot=ledger_->state(context_slot_))
                for(int i=0;i<initial_safety_count_;++i)if(certificates[i].actor==snapshot.frame.clock.self)
                    input_->loaded_return=loaded(snapshot,slot->declaration,certificates[i]);
            auto local=adapter_.local(snapshot,work_.plan,{certificates.data(),std::size_t(initial_safety_count_)},
                safe,calm(snapshot),productive(snapshot),escape(snapshot),&work_);local.fuel_resumed|=skipped;
            if(skipped||int(cambc::round_number())!=snapshot.frame.clock.round)fault();
            const auto command=underwritten_frontier::advance(*input_,local,*ledger_,arm,work_);dispatch(command,local);
            observe_trace(snapshot,local);return;
        }
        if(snapshot.frame.own.actor.channel!=p6::Channel::none||!snapshot.frame.own.actor.alive) {fallback(snapshot);return;}
        if(snapshot.own.bag>0) {if(!bank_only(snapshot))fallback(snapshot);return;}
        const auto* context=ledger_->state(context_slot_);
        const bool exhausted=context&&ledger_->campaign_exhausted(context_slot_)&&
            !ledger_->organization_qualified(context_slot_,context->declaration.key,snapshot.frame.clock.round);
        const bool suspended=context&&ledger_->suspended(context_slot_,snapshot.frame.clock.round/project_rounds);
        const bool fixed=arm==Arm::active&&snapshot.frame.own.actor.pick_ordinal==2&&context_site_>=0&&!exhausted&&!suspended;
        auto own=best_solo(snapshot.frame,fixed?context_site_:-1,fixed?context_bank_:-1);
        if(!own&&fixed)own=best_solo(snapshot.frame);
        if(!own){fallback(snapshot);return;}
        declare_context(snapshot,*own);
        if(!build_menu(snapshot,*own)){fallback(snapshot);return;}
        auto decision=underwritten_frontier::decide(*input_,*ledger_,probes_,arm);
        if(decision.option<0){fallback(snapshot);return;}
        const auto planned=snapshot;const int action_group=input_->offers[decision.option].plan.group;
        int site=-1;for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)site=s;
        if(site<0)return;
        snapshot=adapter_.observe(&routes_.sites[site].group,&work_);
        if(!unchanged(planned,snapshot)){fault();return;}
        populate(snapshot);
        for(int o=0;o<input_->offer_count;++o)for(int c=0;c<input_->offers[o].safety_count;++c)
            input_->offers[o].safety[c].clock=snapshot.frame.clock;
        if(input_->loaded_return.knowledge==p6::Knowledge::exact)input_->loaded_return.clock=snapshot.frame.clock;
        if(input_->visible_proposer.knowledge==p6::Knowledge::exact)input_->visible_proposer.clock=snapshot.frame.clock;
        decision=underwritten_frontier::decide(*input_,*ledger_,probes_,arm);
        if(decision.option<0||input_->offers[decision.option].plan.group!=action_group)return;
        auto& chosen=input_->offers[decision.option];
        auto local=adapter_.local(snapshot,chosen.plan,{chosen.safety.data(),std::size_t(chosen.safety_count)},
            true,calm(snapshot),true,escape(snapshot));
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<18000){fault();return;}
        const auto admitted=underwritten_frontier::commit(*input_,local,decision,*ledger_,probes_,arm,work_);
        if(!admitted.started){fallback(snapshot);return;}
        selected_site_=site;selected_bank_=own->bank;
        renewal_site_=chosen.plan.organization==Organization::H&&chosen.plan.collector!=snapshot.frame.clock.self?own->site:site;
        initial_safety_=chosen.safety;initial_safety_count_=chosen.safety_count;threat_=threats(chosen.plan.group);
        if(admitted.episode&&!trace_.begin(*ledger_,work_)){ledger_->invalidate_active(snapshot.frame.clock.round);work_.request_stop();}
        std::cout<<"p8-06 ADMIT group="<<chosen.plan.group<<" organization="<<int(work_.plan.organization)<<" episode="<<admitted.episode<<'\n';
        unavailable_logged_=false;inspection_turns_=0;inspection_site_=-1;
        dispatch(work_.step(local),local);observe_trace(snapshot,local);
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
    const auto selected=underwritten_frontier::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-06 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
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
                        std::printf("p8-06 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                            entered,static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                bot->turn();
            }
        }
        catch(const cambc::error& e){std::cout<<"p8-06 ERROR engine "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& e){std::cout<<"p8-06 ERROR exception "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-06 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::underwritten_frontier::native

