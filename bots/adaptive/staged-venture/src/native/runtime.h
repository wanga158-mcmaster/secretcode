#pragma once
#include "package.h"
#include "../draft.h"
#include <bit>
#include <cstdio>
#include <iostream>
#include <memory>

namespace p8::staged_venture::native {
using namespace p8::native;
// Concrete actor-local staged venture driver. Retained routes, own evidence,
// typed purchases, and the shared physical Work executor are the only state
// transitions. This conservative stationary subset uses shared range-one pads.
// The SDK supplies no actual ally useful-attack receipt. Readiness never fills
// that gap: unsupported full-H evidence remains unqualified and capped.
class Runtime {
    Routes routes_;sdk::Adapter adapter_;
    std::unique_ptr<Inputs> input_=make_default<Inputs>();
    std::unique_ptr<EvidenceLedger> ledger_;Venture venture_;CycleTrace trace_;
    p6::ScoreHistory scores_;Work work_;reversible_workshop::Market market_;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};int initial_safety_count_=0;
    int selected_site_=-1,selected_bank_=-1,renewal_site_=-1,last_round_=-1;
    int package_site_=-1,package_bank_=-1;
    int inspection_turns_=0,next_scan_=0,inspection_site_=-1,inspection_cursor_=0;
    Route inspection_{};std::uint64_t threat_=0;
    bool unavailable_logged_=false,preparation_used_=false,preparing_=false,recurring_disabled_=false;
    int preparation_started_=-1,recurring_started_=-1;
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
    bool visible_collector(const sdk::Snapshot& snapshot,NumericState& state,Projection* projected=nullptr) const {
        if(snapshot.draft.picks<3)return false;
        const int id=snapshot.draft.actors[1];const auto* known=snapshot.frame.visible(id);
        if(!known||!known->alive||known->channel!=p6::Channel::none)return false;
        for(const auto& actor:cambc::vision::champions(cambc::team::ally))if(sdk::identity(actor)==id) {
            if(!actor.is_visible()||int(cambc::round_number())!=snapshot.frame.clock.round)return false;
            const auto stats=actor.base_stats();
            state={double(actor.health()),double(actor.max_health()),double(actor.strength()),double(actor.focus()),
                double(stats.strength_growth),std::int64_t(actor.gem_count()),sdk::equipment_identity(actor)};
            const auto now=point(actor.position());
            if(now!=known->position||state.health!=known->health||state.max_health!=known->max_health||
                state.strength!=known->strength||state.bag!=known->bag||
                int(cambc::round_number())!=snapshot.frame.clock.round)return false;
            if(projected)*projected=project(actor,state,true);
            return valid_numeric_state(state)&&actor.is_visible()&&
                int(cambc::round_number())==snapshot.frame.clock.round;
        }
        return false;
    }
    void populate(const sdk::Snapshot& snapshot,bool quotes=false) {
        auto& in=*input_;in.frame=snapshot.frame;
        if(in.settings_identity.source!=p6::Source::queried_setting) {
            in.settings=sdk::settings();
            in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
        }
        in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        in.spawn_shop_legal=snapshot.spawn_shop_legal;
        in.target_group=selected_site_<0?0:group_id(routes_.sites[selected_site_].group);
        const int regime=package_valid(venture_.package,snapshot.draft)?venture_.package.preliminary.group:in.target_group;
        in.threat_regime={int(threats(regime)&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const auto enemy=cambc::global_submitted_gems(cambc::team::enemy);
        scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.collector_state={};NumericState collector;
        if(helper_actor(snapshot.frame.own.actor,snapshot.draft)&&visible_collector(snapshot,collector))
            in.collector_state={collector,snapshot.frame.clock,snapshot.draft.actors[1],p6::Knowledge::exact};
        if(quotes) {
            static constexpr std::array quote_ids{survival_item};
            market_=sdk::market(snapshot,quote_ids);
        }
        if(in.offer_count==0){in.offer_count=1;in.independent=0;}
        bind(in,venture_);
    }
    bool make_package(Package& package,const sdk::Snapshot& snapshot,int site,int bank,
                      const Projection& projected,p6::Frame owner) {
        if(!projected.valid||!routes_.spawn.ready||site<0||bank<0||snapshot.draft.picks<3)return false;
        owner.own.actor.position=routes_.sites[site].approach.destination;
        owner=with_state(owner,projected.ordinary);
        auto preliminary=std::make_unique<Solo>(solo(routes_,site,bank,owner,true));
        if(!preliminary->feasible)return false;
        owner=with_state(owner,projected.invested);owner.own.actor.position=routes_.spawn.destination;
        const auto supported=formation_plan(routes_,site,bank,owner,geometry_roster(snapshot.draft),Organization::H,true);
        if(!plan_valid(supported,owner.clock.self)||walking(supported.ingress)>8)return false;
        const auto info=static_cast<cambc::item_type>(survival_item).info();
        package={};package.preliminary=preliminary->plan;package.supported=supported;
        package.cost={info.cost,p6::Source::queried_setting};
        package.health_gain={projected.invested.max_health-projected.ordinary.max_health,p6::Source::queried_setting};
        package.ordinary=declaration(package.preliminary,projected.ordinary,*input_,false,routes_.sites[site].group.gem_value());
        package.invested=declaration(package.supported,projected.invested,*input_,true,routes_.sites[site].group.gem_value());
        const int threat=int(threats(package.preliminary.group)&0x7fffffff);
        package.ordinary.key.threat_regime=package.invested.key.threat_regime=threat;
        return package_valid(package,snapshot.draft);
    }
    bool configure(const sdk::Snapshot& snapshot,const Solo& opening) {
        if(venture_.declared||ledger_->size()!=0||!venture_actor(snapshot.frame.own.actor,snapshot.draft)||
            !snapshot.frame.own.at_spawn||snapshot.own.bag||snapshot.own.health!=snapshot.own.max_health||
            snapshot.frame.own.actor.position!=routes_.spawn.destination||!calm(snapshot)||
            sdk::own_inventory()!=Inventory{})return false;
        const auto projected=project(cambc::self(),snapshot.own,false);
        const auto info=static_cast<cambc::item_type>(survival_item).info();
        if(!projected.valid||info.cost>snapshot.frame.own.gold)return false;
        auto package=std::make_unique<Package>();
        if(!make_package(*package,snapshot,opening.site,opening.bank,projected,snapshot.frame))return false;
        if(!venture_.declare(*package,snapshot.draft,*ledger_))return false;
        package_site_=opening.site;package_bank_=opening.bank;populate(snapshot);
        std::cout<<"p8-05 DECLARED group="<<package->preliminary.group<<" ordinary_inventory=empty\n";return true;
    }
    bool build_menu(const sdk::Snapshot& snapshot,const Solo& own,bool invite=true) {
        auto& in=*input_;auto independent=baselines(snapshot.frame,own);
        in.offer_count=1;in.independent=0;in.visible_proposer={};
        if(!independent_offer(in.offers[0],own,independent,snapshot.frame)) {
            independent.clear();independent.push_back(own);normalize(independent.back(),snapshot.frame);
            if(!independent_offer(in.offers[0],own,independent,snapshot.frame))return false;
        }
        const bool owner=venture_actor(snapshot.frame.own.actor,snapshot.draft);
        if(owner&&venture_.declared&&!venture_.paid) {
            in.offers[0].context_slot=venture_.component;in.offers[0].entry=venture_.package.ordinary.key;
        }
        if(!invite||independent.size()!=3)return true;
        if(owner&&(venture_.paid||venture_.phase==VenturePhase::purchase_pending)&&package_site_>=0) {
            auto support=std::make_unique<Coalition>(coalition(routes_,package_site_,package_bank_,snapshot.frame,
                geometry_roster(snapshot.draft),independent,Organization::H,true));
            if(support->feasible) {
                in.offers[1]=support->offer;in.offers[1].context_slot=venture_.organization;
                in.offers[1].entry=venture_.package.invested.key;in.offer_count=2;
            }
        } else if(helper_actor(snapshot.frame.own.actor,snapshot.draft)) {
            const auto* collector=snapshot.frame.visible(snapshot.draft.actors[1]);NumericState actual;Projection projected;
            if(!collector||!visible_collector(snapshot,actual,&projected)||!projected.valid||actual.bag)return true;
            for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>90000;++s) {
                if(routes_.sites[s].rally_count<2||collector->position!=routes_.sites[s].rally[0].destination)continue;
                for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>90000;++b) {
                    // An unqualified local descriptor is reconstructed from
                    // visible gear and public geometry, never the owner's proof.
                    auto descriptor=std::make_unique<Package>();auto owner_frame=subject(snapshot.frame,*collector);
                    if(!make_package(*descriptor,snapshot,s,b,projected,owner_frame))continue;
                    auto support=std::make_unique<Coalition>(coalition(routes_,s,b,snapshot.frame,
                        geometry_roster(snapshot.draft),independent,Organization::H,true));
                    if(!support->feasible)continue;
                    venture_.package=*descriptor;package_site_=s;package_bank_=b;
                    in.offers[1]=support->offer;in.offer_count=2;
                    in.visible_proposer={support->offer.plan.group,snapshot.frame.clock,collector->id,p6::Knowledge::exact};
                    return true;
                }
            }
        }
        return true;
    }
    bool package_offer(const sdk::Snapshot& snapshot,PurchaseComparison& compared,bool buying) {
        if(!venture_.declared||package_site_<0||snapshot.draft.picks<3||!routes_.spawn.ready)return false;
        const auto inventory=sdk::own_inventory();
        if(inventory!=(buying?Inventory{}:survival_inventory()))return false;
        auto ordinary=best_solo(snapshot.frame);if(!ordinary)return false;
        auto alternatives=baselines(snapshot.frame,*ordinary,venture_.package.supported.group);
        if(alternatives.size()!=3)return false;
        auto normal=std::make_unique<Offer>();if(!independent_offer(*normal,*ordinary,alternatives,snapshot.frame))return false;
        auto projected=snapshot;
        if(buying) {
            const auto prediction=project(cambc::self(),snapshot.own,false);if(!prediction.valid)return false;
            projected.own=prediction.invested;projected.frame=with_state(snapshot.frame,prediction.invested);
        }
        projected.frame.own.actor.position=routes_.spawn.destination;
        auto alternative=best_solo(projected.frame);if(!alternative)return false;
        auto future_baselines=baselines(projected.frame,*alternative,venture_.package.supported.group);
        if(future_baselines.size()!=3)return false;
        auto future=std::make_unique<Coalition>(coalition(routes_,package_site_,package_bank_,projected.frame,
            geometry_roster(snapshot.draft),future_baselines,Organization::H,true));
        const int setup=preparation();
        // Current ordinary gear protects the actual preparation. Prospective
        // S certificates independently protect new combat and the whole crew.
        auto preparation_guard=std::make_unique<Offer>(*normal);
        if(!future->feasible||!preparation_safe(*preparation_guard,snapshot.frame,setup)||
            !preparation_safe(future->offer,projected.frame,setup))return false;
        const auto info=static_cast<cambc::item_type>(survival_item).info();
        if(buying&&(info.cost!=venture_.package.cost.value||info.cost>snapshot.frame.own.gold))return false;
        p6::Observation<bool> legal{true,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        if(buying&&snapshot.frame.own.at_spawn) {
            if(!market_.inventory.current(snapshot.frame.clock,snapshot.frame.clock.self)||market_.inventory.value!=Inventory{})return false;
            legal=market_.items[survival_item].buy_legal;
        }
        auto& in=*input_;in.offers[0]=*normal;in.offer_count=1;in.independent=0;
        return native::comparison(compared,in,future->offer,*normal,buying?info.cost:0,legal,buying);
    }
    bool purchase(sdk::Snapshot& snapshot,PurchaseComparison& compared) {
        auto local=adapter_.local(snapshot,work_.plan,{},calm(snapshot),calm(snapshot),false);
        if(cambc::fuel_remaining()<30000||int(cambc::round_number())!=snapshot.frame.clock.round||
            !sdk::unchanged_shop_state(snapshot,snapshot.frame.clock,snapshot.own,snapshot.frame.own.gold)||
            cambc::fuel_remaining()<25000||int(cambc::round_number())!=snapshot.frame.clock.round)return false;
        const auto proposed=propose_purchase(*input_,local,work_,venture_,*ledger_,arm,compared);
        if(!proposed.valid)return false;
        reversible_workshop::TransactionCommand command;command.valid=true;
        command.kind=reversible_workshop::TransactionKind::buy;command.clock=proposed.clock;command.token=proposed.token;
        command.item=survival_item;command.amount=proposed.cost;command.gold_before=proposed.gold_before;
        command.before=Inventory{};command.after=survival_inventory();command.stats_before=proposed.before;
        const auto result=sdk::transact(adapter_,snapshot,command);
        bool confirmed=false;
        if(result.receipt.attempted&&result.receipt.success&&result.after.valid&&
            result.after_market.inventory.current(result.after.frame.clock,proposed.clock.self)&&
            result.after_market.inventory.value==survival_inventory()) {
            snapshot=result.after;populate(snapshot,true);
            // Reforecast from actual refreshed S stats and actual visible crew.
            auto own=best_solo(snapshot.frame);
            if(own&&build_menu(snapshot,*own)&&input_->offer_count>1) {
                const auto* helper=common::certificate(input_->offers[1],snapshot.draft.actors[0]);
                if(helper&&cambc::fuel_remaining()>=30000&&
                    int(cambc::round_number())==snapshot.frame.clock.round&&
                    sdk::unchanged_shop_state(snapshot,snapshot.frame.clock,snapshot.own,snapshot.frame.own.gold)&&
                    cambc::fuel_remaining()>=25000&&int(cambc::round_number())==snapshot.frame.clock.round)
                    confirmed=confirm_purchase(*input_,proposed,venture_,*ledger_,arm,*helper);
            }
        }
        if(!confirmed)failed_purchase(venture_,*ledger_);
        preparing_=false;
        std::cout<<"p8-05 PURCHASE confirmed="<<confirmed<<" attempted="<<result.receipt.attempted<<'\n';return true;
    }
    bool preparation_or_shop(sdk::Snapshot& snapshot) {
        if(!venture_.declared||!venture_actor(snapshot.frame.own.actor,snapshot.draft)||
            venture_.phase==VenturePhase::retired||work_.active)return false;
        populate(snapshot,snapshot.frame.own.at_spawn);
        if(!venture_.paid&&venture_.phase==VenturePhase::preliminary&&
            (arm==Arm::control||venture_.component_current(*input_,*ledger_))) {
            const int limit=preparation();if(limit<0)return false;
            if(preparing_&&snapshot.frame.clock.round-preparation_started_>limit)preparing_=false;
            auto compared=std::make_unique<PurchaseComparison>();
            if(!preparing_&&!preparation_used_&&snapshot.own.bag==0&&calm(snapshot)&&package_offer(snapshot,*compared,true)) {
                if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<20000)return false;
                preparation_used_=preparing_=true;preparation_started_=snapshot.frame.clock.round;
                std::cout<<"p8-05 PREPARATION_STARTED\n";
            }
            if(preparing_) {
                if(!calm(snapshot)||snapshot.own.bag||!package_offer(snapshot,*compared,true)) {preparing_=false;return false;}
                if(!snapshot.frame.own.at_spawn||snapshot.frame.own.actor.position!=routes_.spawn.destination||
                    snapshot.own.health<snapshot.own.max_health) {return_spawn(snapshot);return true;}
                return purchase(snapshot,*compared);
            }
        }
        if(venture_.paid&&venture_.phase==VenturePhase::commissioning&&snapshot.own.bag==0&&
            (!snapshot.frame.own.at_spawn||snapshot.frame.own.actor.position!=routes_.spawn.destination)) {
            if(recurring_disabled_)return false;
            const bool qualified=ledger_->organization_qualified(venture_.organization,venture_.package.invested.key,snapshot.frame.clock.round);
            if(arm==Arm::active&&(ledger_->suspended(venture_.organization,snapshot.frame.clock.round/common::project_rounds)||
                (ledger_->campaign_exhausted(venture_.organization)&&!qualified)||
                (!qualified&&(venture_.proof_round<0||snapshot.frame.clock.round-venture_.proof_round>=common::qualification_rounds)))) {
                recurring_disabled_=true;return false;
            }
            const int limit=preparation();if(limit<0)return false;
            if(recurring_started_>=0&&snapshot.frame.clock.round-recurring_started_>limit) {recurring_disabled_=true;return false;}
            auto compared=std::make_unique<PurchaseComparison>();
            if(calm(snapshot)&&package_offer(snapshot,*compared,false)) {
                if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<20000)return false;
                if(recurring_started_<0)recurring_started_=snapshot.frame.clock.round;
                return_spawn(snapshot);return true;
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
    Command continue_control(const LocalFrame& local) {
        auto& in=*input_;common::refresh_evidence(in,*ledger_);
        bool represented=common::valid_inputs(in)&&local.fresh()&&in.frame.clock==local.frame.clock&&
            !local.fuel_resumed&&int(cambc::round_number())==in.frame.clock.round&&cambc::fuel_remaining()>=12000&&
            in.frame.own.actor.health==local.frame.own.actor.health&&
            in.frame.own.actor.bag==local.frame.own.actor.bag;
        if(work_.plan.organization==Organization::H&&work_.plan.collector==in.frame.clock.self) {
            const auto& d=venture_.package.invested;const auto& k=d.key;
            represented&=venture_.paid&&venture_.phase==VenturePhase::commissioning&&
                k.actor_id==in.frame.clock.self&&k.champion==int(in.frame.own.actor.type)&&
                k.settings_id==in.settings_identity.value&&k.threat_regime==in.threat_regime.value&&
                work_.plan.group==k.source_group&&plan_route_hash(work_.plan)==k.route_hash&&
                plan_return_hash(work_.plan)==k.return_route_hash&&
                work_.plan.crew_count==k.participant_count&&
                d.envelope.contains(in.own_state.value,k.build);
            for(int i=0;i<k.participant_count;++i)represented&=crew_index(work_.plan,k.participants[i])>=0;
            // Work applies current member/holder checks only while their work
            // is required. A released helper leaving vision is an expected
            // transition, not a remote dependency of the collector's bank.
        }
        // Control has no issued evidence episode. Entry HP bands are not
        // rechecked mid-job; actual damage and Prism Focus must remain inside
        // the frozen transition envelope. A material loss requests only this
        // actor's safe withdrawal, preserving channel/hold/banking obligations.
        if(!represented)work_.request_stop();
        return work_.step(local);
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
                std::printf("p8-05 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                    entered,static_cast<unsigned long long>(cambc::fuel_remaining()));
                diagnostic_routes_started_=true;
            }
            if(!room(70000))return;
            const bool complete=routes_.initialize_step(1);
            if(complete&&room(32000))
                std::printf("p8-05 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
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
        last_round_=snapshot.frame.clock.round;
        if(work_.active)populate(snapshot);
        if(work_.active) {
            // The first empty-gear income job may end at actual safe empty
            // spawn, without pretending that the old job completed renewal.
            if(!venture_.declared&&!ledger_->active()&&venture_actor(snapshot.frame.own.actor,snapshot.draft)&&
                snapshot.frame.own.at_spawn&&snapshot.own.bag==0&&snapshot.frame.own.actor.channel==p6::Channel::none&&calm(snapshot)) {
                work_.invalidate(PhysicalFailure::unavailable);
            } else {
                const auto certificates=current_safety(snapshot);bool safe=false;
                for(int i=0;i<initial_safety_count_;++i)if(certificates[i].actor==snapshot.frame.clock.self)
                    safe=certificates[i].valid(snapshot.frame,snapshot.frame.clock.self,work_.plan.group);
                safe&=threats(work_.plan.group)==threat_;
                auto local=adapter_.local(snapshot,work_.plan,{certificates.data(),std::size_t(initial_safety_count_)},
                    safe,calm(snapshot),productive(snapshot),escape(snapshot),&work_);local.fuel_resumed|=skipped;
                if(skipped){work_.request_stop();if(ledger_->active())ledger_->invalidate_active(snapshot.frame.clock.round);}
                const auto command=arm==Arm::control?continue_control(local):
                    staged_venture::advance(*input_,local,venture_,*ledger_,arm,work_);dispatch(command,local);
                const auto result=observe_cycle(*input_,local,*ledger_,work_,trace_);
                if(result.accepted)std::cout<<"p8-05 EPISODE positive="<<result.full_positive<<" contradiction="<<result.contradiction<<'\n';
                return;
            }
        }
        if(snapshot.frame.own.actor.channel!=p6::Channel::none||!snapshot.frame.own.actor.alive||snapshot.own.bag>0) {fallback(snapshot);return;}
        populate(snapshot,snapshot.frame.own.at_spawn);
        if(preparation_or_shop(snapshot))return;
        if(!room(30000))return;
        const bool fixed=arm==Arm::active&&venture_.declared&&!venture_.paid&&venture_.phase==VenturePhase::preliminary;
        auto own=best_solo(snapshot.frame,fixed?package_site_:-1,fixed?package_bank_:-1);
        if(!own&&fixed)own=best_solo(snapshot.frame);
        if(!own){fallback(snapshot);return;}
        configure(snapshot,*own);
        // A freshly declared control can buy immediately at a feasible actual
        // spawn. Active first makes the two exact ordinary productive starts.
        if(arm==Arm::control&&preparation_or_shop(snapshot))return;
        if(!build_menu(snapshot,*own)){fallback(snapshot);return;}
        auto decision=staged_venture::decide(*input_,venture_,*ledger_,arm);
        if(decision.option<0){fallback(snapshot);return;}
        const auto planned=snapshot;const int action_group=input_->offers[decision.option].plan.group;
        int site=-1;for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)site=s;
        if(site<0)return;
        snapshot=adapter_.observe(&routes_.sites[site].group,&work_);
        if(!unchanged(planned,snapshot)){fault();return;}
        populate(snapshot);
        for(int o=0;o<input_->offer_count;++o)for(int c=0;c<input_->offers[o].safety_count;++c)
            input_->offers[o].safety[c].clock=snapshot.frame.clock;
        if(input_->offer_count>1&&input_->visible_proposer.knowledge!=p6::Knowledge::unknown)
            input_->visible_proposer.clock=snapshot.frame.clock;
        decision=staged_venture::decide(*input_,venture_,*ledger_,arm);
        if(decision.option<0||input_->offers[decision.option].plan.group!=action_group)return;
        auto& chosen=input_->offers[decision.option];
        auto local=adapter_.local(snapshot,chosen.plan,{chosen.safety.data(),std::size_t(chosen.safety_count)},
            true,calm(snapshot),true,escape(snapshot));
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<18000) {fault();return;}
        const auto admitted=staged_venture::commit(*input_,local,decision,venture_,*ledger_,arm,work_,trace_);
        if(!admitted.started){fallback(snapshot);return;}
        recurring_started_=-1;selected_site_=site;selected_bank_=own->bank;
        renewal_site_=chosen.plan.organization==Organization::H&&chosen.plan.collector!=snapshot.frame.clock.self?own->site:site;
        initial_safety_=chosen.safety;initial_safety_count_=chosen.safety_count;threat_=threats(chosen.plan.group);
        std::cout<<"p8-05 ADMIT group="<<chosen.plan.group<<" organization="<<int(chosen.plan.organization)<<" episode="<<admitted.episode<<'\n';
        unavailable_logged_=false;inspection_turns_=0;inspection_site_=-1;
        const auto command=work_.step(local);dispatch(command,local);observe_cycle(*input_,local,*ledger_,work_,trace_);
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
    const auto selected=staged_venture::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-05 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
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
                        std::printf("p8-05 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                            entered,static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                bot->turn();
            }
        }
        catch(const cambc::error& e){std::cout<<"p8-05 ERROR engine "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& e){std::cout<<"p8-05 ERROR exception "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-05 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::staged_venture::native

