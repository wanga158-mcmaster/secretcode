#pragma once
#include "configuration.h"
#include "../../../common/src/defaults.h"
#include "../draft.h"
#include <bit>
#include <cstdio>
#include <iostream>
#include <memory>
namespace p8::common_cause_firewall::native {
using namespace p8::native;
// A concrete pair of disjoint return dependencies. Layout is public geometry,
// never an invented combat observation; all executable offers are recertified.
class Runtime {
    Routes routes_;sdk::Adapter adapter_;std::unique_ptr<Inputs> input_=p8::native::make_default<Inputs>();
    std::unique_ptr<EvidenceLedger> ledger_;ProbeBudget probes_;CycleTrace trace_;Shop shop_;Work work_;p6::ScoreHistory scores_;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};int initial_safety_count_=0;
    int selected_site_=-1,selected_bank_=-1,renewal_site_=-1,last_round_=-1;
    int field_site_=-1,field_bank_=-1,spawn_site_=-1,spawn_bank_=-1,layout_cursor_=0;
    bool layout_ready_=false,contexts_=false,shop_used_=false,unavailable_logged_=false,layout_unavailable_logged_=false;
    std::array<int,2> components_{-1,-1},supported_{-1,-1};std::array<std::uint64_t,2> builds_{};
    int inspection_turns_=0,next_scan_=0,inspection_site_=-1,inspection_cursor_=0;
    Route inspection_{};std::uint64_t threat_=0;
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
                                    std::span<const int> excluded={},bool spawn_visit=true) {
        std::unique_ptr<Solo> best;
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>55000;++s) {
            if(fixed>=0&&s!=fixed)continue;
            if(std::find(excluded.begin(),excluded.end(),group_id(routes_.sites[s].group))!=excluded.end())continue;
            for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>55000;++b) {
                if(bank>=0&&b!=bank)continue;
                auto model=std::make_unique<Solo>(solo(routes_,s,b,frame,spawn_visit));
                if(!spawn_visit&&model->feasible) {
                    bool spawn_dependency=false;
                    for(Leg leg:{Leg::bank,Leg::renewal}) {
                        const auto& route=route_for(model->plan,leg);
                        for(int tile_index=0;tile_index<route.count;++tile_index)
                            spawn_dependency|=cambc::map::spawn_region(cambc::team::ally).is_set(tile(route.tiles[tile_index]));
                    }
                    if(spawn_dependency)continue;
                }
                if(model->feasible&&(!best||model->plan.forecast_rounds<best->plan.forecast_rounds||
                    (model->plan.forecast_rounds==best->plan.forecast_rounds&&model->plan.group<best->plan.group)))best=std::move(model);
            }
        }
        return best;
    }
    void populate(const sdk::Snapshot& snapshot,bool quotes=false) {
        auto& in=*input_;in.frame=snapshot.frame;in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        if(in.settings_identity.source!=p6::Source::queried_setting) {
            in.settings=sdk::settings();
            in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
        }
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        const int source=layout_ready_&&(ordinal==0||ordinal==1)?
            group_id(routes_.sites[ordinal==0?field_site_:spawn_site_].group):
            selected_site_<0?0:group_id(routes_.sites[selected_site_].group);
        in.target_group=source;in.threat_regime={int(threats(source)&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const auto enemy=cambc::global_submitted_gems(cambc::team::enemy);
        scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        if(quotes) {
            constexpr std::array<int,1> quote_ids{damage_item};
            in.market=sdk::market(snapshot,quote_ids);
        }
        else {
            in.market=p8::native::exact_default<reversible_workshop::Market>();const auto inventory=sdk::own_inventory();
            if(inventory.valid()&&inventory.hash()==snapshot.own.equipment_hash)
                in.market.inventory={inventory,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
            in.market.spawn_shop_legal=snapshot.spawn_shop_legal;
        }
        in.field_shop_legal={};
        if(ordinal==0&&snapshot.frame.own.actor.type==p6::Champion::tavi&&!snapshot.frame.own.at_spawn) {
            const bool legal=cambc::self().can_buy(static_cast<cambc::item_type>(damage_item));
            if(int(cambc::round_number())==snapshot.frame.clock.round)
                in.field_shop_legal={legal,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        }
        in.component_slot=in.supported_slot=-1;in.component_entry=in.supported_entry={};
        for(int build=0;build<2;++build)if(builds_[build]&&snapshot.own.equipment_hash==builds_[build]) {
            in.component_slot=components_[build];in.supported_slot=supported_[build];
            if(const auto* slot=ledger_->state(in.component_slot))in.component_entry=slot->declaration.key;
            if(const auto* slot=ledger_->state(in.supported_slot))in.supported_entry=slot->declaration.key;break;
        }
        if(in.offer_count==0){in.offer_count=1;in.independent=0;}
    }
    p6::Frame public_owner(const sdk::Snapshot& snapshot,int ordinal,p6::Point at) const {
        auto frame=snapshot.frame;frame.clock.self=snapshot.draft.actors[ordinal];frame.own.actor.id=frame.clock.self;
        frame.own.actor.type=snapshot.draft.champions[ordinal];frame.own.actor.pick_ordinal=ordinal;
        frame.own.actor.position=at;return frame;
    }
    void layout(const sdk::Snapshot& snapshot) {
        if(layout_ready_)return;
        const int sites=int(routes_.sites.size()),banks=int(routes_.banks.size());
        if(snapshot.draft.picks<3||snapshot.draft.champions[0]!=p6::Champion::tavi||sites<2||banks<2||layout_cursor_>=sites*sites*banks*banks) {
            if(!layout_unavailable_logged_) {
                std::cout<<"p8-07 OPPORTUNITY_UNAVAILABLE layout=no_disjoint_comparable_pair\n";layout_unavailable_logged_=true;
            }
            return;
        }
        if(!routes_.spawn.ready)return;
        int cursor=layout_cursor_;const int hb=cursor%banks;cursor/=banks;const int fb=cursor%banks;cursor/=banks;
        const int h=cursor%sites,f=cursor/sites;
        if(f==h||fb==hb){++layout_cursor_;return;}
        const auto& field=routes_.sites[f];const auto& spawn=routes_.sites[h];
        if(field.group.gem_value()<=0||field.group.gem_value()!=spawn.group.gem_value()||public_work(field)!=public_work(spawn)) {++layout_cursor_;return;}
        if(!field.approach.ready||!field.capture.ready||!spawn.approach.ready||!spawn.capture.ready||
            !routes_.banks[fb].approach.ready||!routes_.banks[hb].approach.ready)return;
        for(int i=0;i<spawn.rally_count;++i)if(!spawn.rally[i].ready)return;
        for(int i=0;i<spawn.joint_capture_count;++i)if(!spawn.joint_capture[i].ready)return;
        ++layout_cursor_;auto candidate=std::make_unique<Layout>();
        candidate->field=ordinary(routes_,f,fb,snapshot.draft.actors[0],false);
        candidate->spawn_component=ordinary(routes_,h,hb,snapshot.draft.actors[1],true);
        candidate->spawn_supported=formation_plan(routes_,h,hb,public_owner(snapshot,1,spawn.approach.destination),
            supported_roster(snapshot.draft),Organization::H,true);
        candidate->uniform=formation_plan(routes_,h,hb,public_owner(snapshot,0,field.approach.destination),snapshot.draft,Organization::J,true);
        candidate->spawn_shop=routes_.spawn.destination;
        candidate->field_work=candidate->spawn_work={public_work(field),p6::Source::public_map};
        candidate->field_reward=candidate->spawn_reward={field.group.gem_value(),p6::Source::public_map};
        if(!candidate->valid(snapshot.draft)||!field_avoids_spawn(candidate->field)||
            !disjoint_returns(candidate->field,candidate->spawn_component)||!uniform_dependency_hash(candidate->uniform))return;
        input_->layout=*candidate;layout_ready_=true;field_site_=f;field_bank_=fb;spawn_site_=h;spawn_bank_=hb;
        std::cout<<"p8-07 LAYOUT field="<<candidate->field.group<<" spawn="<<candidate->spawn_component.group<<'\n';
    }
    void freeze(const sdk::Snapshot& snapshot) {
        if(contexts_||!layout_ready_||!producer(snapshot.frame.own.actor,snapshot.draft)||snapshot.own.bag||ledger_->active())return;
        const bool field=field_actor(snapshot.frame.own.actor,snapshot.draft);const auto& p=field?input_->layout.field:input_->layout.spawn_component;
        std::array<NumericState,2> states{snapshot.own,{}};p6::Frame ignored;const bool projected=project_damage(snapshot,states[1],ignored);
        for(int build=0;build<(projected?2:1);++build) {
            auto component=declaration(p,states[build],*input_,int(snapshot.frame.own.actor.type),false,!field,
                routes_.sites[field?field_site_:spawn_site_].group.gem_value());
            components_[build]=ledger_->predeclare(component);builds_[build]=states[build].equipment_hash;
            if(!field) {
                auto full=declaration(input_->layout.spawn_supported,states[build],*input_,int(snapshot.frame.own.actor.type),true,true,
                    routes_.sites[spawn_site_].group.gem_value());
                supported_[build]=ledger_->predeclare(full);
            }
        }
        contexts_=true;populate(snapshot);
    }
    std::unique_ptr<Solo> own_model(const sdk::Snapshot& snapshot) {
        if(!layout_ready_)return best_solo(snapshot.frame,-1,-1,{},snapshot.frame.own.actor.pick_ordinal!=0);
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        if(ordinal==0) {
            auto fixed=best_solo(snapshot.frame,field_site_,field_bank_,{},false);if(fixed)return fixed;
            const std::array<int,1> excluded{group_id(routes_.sites[spawn_site_].group)};
            return best_solo(snapshot.frame,-1,-1,excluded,false);
        }
        if(ordinal==1) {
            auto fixed=best_solo(snapshot.frame,spawn_site_,spawn_bank_);if(fixed)return fixed;
            const std::array<int,1> excluded{group_id(routes_.sites[field_site_].group)};
            return best_solo(snapshot.frame,-1,-1,excluded);
        }
        const std::array<int,2> excluded{group_id(routes_.sites[field_site_].group),group_id(routes_.sites[spawn_site_].group)};
        return best_solo(snapshot.frame,-1,-1,excluded);
    }
    std::vector<Solo> alternatives(const sdk::Snapshot& snapshot,const Solo& own) {
        std::vector<Solo> models;models.reserve(3);models.push_back(own);normalize(models.back(),snapshot.frame);
        const int first=arm==Arm::active?1:0;
        for(int ordinal=first;ordinal<3;++ordinal) {
            const int id=snapshot.draft.actors[ordinal];if(id==snapshot.frame.clock.self)continue;
            const auto* actor=snapshot.frame.visible(id);if(!actor||!actor->alive||actor->channel!=p6::Channel::none)return {};
            auto frame=subject(snapshot.frame,*actor);std::unique_ptr<Solo> model;
            if(ordinal==0)model=best_solo(frame,field_site_,field_bank_,{},false);
            else if(ordinal==1)model=best_solo(frame,spawn_site_,spawn_bank_);
            else {
                std::array<int,4> excluded{};int count=0;
                excluded[count++]=group_id(routes_.sites[field_site_].group);excluded[count++]=group_id(routes_.sites[spawn_site_].group);
                for(const auto& m:models)if(std::find(excluded.begin(),excluded.begin()+count,m.plan.group)==excluded.begin()+count)excluded[count++]=m.plan.group;
                model=best_solo(frame,-1,-1,{excluded.data(),std::size_t(count)});
            }
            if(!model)return {};normalize(*model,snapshot.frame);models.push_back(std::move(*model));
        }
        for(std::size_t i=0;i<models.size();++i)for(std::size_t j=0;j<i;++j)if(models[i].plan.group==models[j].plan.group)return {};
        return models;
    }
    bool uniform(const sdk::Snapshot& snapshot,const Plan& plan) const {
        if(plan.organization!=Organization::J)return false;
        const auto hash=uniform_dependency_hash(input_->layout.uniform);
        if(!hash||uniform_dependency_hash(plan)!=hash)return false;
        for(int ordinal=0;ordinal<3;++ordinal) {
            const auto* actor=snapshot.frame.visible(snapshot.draft.actors[ordinal]);if(!actor)return false;
            const auto peer=formation_plan(routes_,spawn_site_,spawn_bank_,subject(snapshot.frame,*actor),snapshot.draft,Organization::J,true);
            if(!plan_valid(peer,actor->id)||uniform_dependency_hash(peer)!=hash)return false;
        }
        return true;
    }
    bool menu(const sdk::Snapshot& snapshot,const Solo& own) {
        auto& in=*input_;in.offer_count=1;in.independent=0;in.visible_proposer={};
        if(!independent_offer(in.offers[0],own,{&own,1},snapshot.frame))return false;
        if(!layout_ready_)return true;
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        if((arm==Arm::active&&ordinal==0)||ordinal>2)return true;
        auto models=alternatives(snapshot,own);if(models.size()!=std::size_t(arm==Arm::active?2:3))return true;
        if(!independent_offer(in.offers[0],own,models,snapshot.frame))return false;
        const auto org=arm==Arm::active?Organization::H:Organization::J;
        const int collector=arm==Arm::active?snapshot.draft.actors[1]:snapshot.draft.actors[0];
        const auto* proposer=snapshot.frame.visible(collector);if(!proposer)return true;
        if(snapshot.frame.clock.self!=collector&&(routes_.sites[spawn_site_].rally_count<2||
            proposer->position!=routes_.sites[spawn_site_].rally[0].destination))return true;
        auto supported=std::make_unique<Coalition>(coalition(routes_,spawn_site_,spawn_bank_,snapshot.frame,
            arm==Arm::active?supported_roster(snapshot.draft):snapshot.draft,models,org,true));
        if(!supported->feasible||(arm==Arm::control&&!uniform(snapshot,supported->offer.plan)))return true;
        in.offers[1]=supported->offer;in.offer_count=2;
        if(arm==Arm::active&&ordinal==1){in.offers[1].context_slot=in.supported_slot;in.offers[1].entry=in.supported_entry;}
        if(snapshot.frame.clock.self!=collector)in.visible_proposer={supported->offer.plan.group,snapshot.frame.clock,collector,p6::Knowledge::exact};
        return true;
    }
    bool shop(sdk::Snapshot& snapshot) {
        if(arm!=Arm::active||!layout_ready_||shop_used_||work_.active||ledger_->active()||snapshot.own.bag||
            !producer(snapshot.frame.own.actor,snapshot.draft)||snapshot.frame.own.actor.channel!=p6::Channel::none||!calm(snapshot))return false;
        populate(snapshot,true);NumericState expected;p6::Frame projected;
        if(!project_damage(snapshot,expected,projected))return false;
        const bool field=field_actor(snapshot.frame.own.actor,snapshot.draft);
        const int s=field?field_site_:spawn_site_,b=field?field_bank_:spawn_bank_;
        auto before=std::make_unique<Solo>(solo(routes_,s,b,snapshot.frame,!field));
        auto after=std::make_unique<Solo>(solo(routes_,s,b,projected,!field));
        if(!before->feasible||!after->feasible)return false;
        auto ordinary=std::make_unique<Offer>(),improved=std::make_unique<Offer>();
        if(!independent_offer(*ordinary,*before,{before.get(),1},snapshot.frame)||
            !independent_offer(*improved,*after,{after.get(),1},projected))return false;
        auto& in=*input_;in.offers[0]=*ordinary;in.offer_count=1;in.independent=0;
        auto purchase=std::make_unique<PurchasePlan>();
        if(!purchase_schedule(*purchase,in,*improved,*ordinary,in.market.items[damage_item].price.value))return false;
        auto local=adapter_.local(snapshot,ordinary->plan,{ordinary->safety.data(),std::size_t(ordinary->safety_count)},true,true,false);
        const bool current=field?current_field_state(snapshot):sdk::unchanged_shop_state(snapshot,snapshot.frame.clock,snapshot.own,snapshot.frame.own.gold);
        if(!current||int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<30000)return false;
        const auto command=shop_.propose(in,local,*ledger_,work_,*purchase);if(!command.valid)return false;
        shop_used_=true;PurchaseReceipt receipt;sdk::Snapshot refreshed;
        if(field) {const auto result=field_buy(adapter_,snapshot,command);receipt=result.receipt;refreshed=result.after;}
        else {
            reversible_workshop::TransactionCommand c;c.valid=true;c.kind=reversible_workshop::TransactionKind::buy;
            c.clock=command.clock;c.token=command.token;c.item=command.item;c.amount=command.cost;c.gold_before=command.gold_before;
            c.before=command.before;c.after=command.after;c.stats_before=command.state_before;
            const auto result=sdk::transact(adapter_,snapshot,c);refreshed=result.after;
            receipt={result.receipt.command_clock,result.receipt.observed_clock,result.receipt.token,result.receipt.item,result.receipt.attempted,result.receipt.success};
        }
        bool confirmed=false;
        if(receipt.success&&refreshed.valid) {
            snapshot=refreshed;populate(snapshot,true);
            const bool unchanged=field?current_field_state(snapshot):sdk::unchanged_shop_state(snapshot,snapshot.frame.clock,snapshot.own,snapshot.frame.own.gold);
            if(unchanged&&int(cambc::round_number())==snapshot.frame.clock.round&&cambc::fuel_remaining()>=25000)
                confirmed=shop_.confirm(command,receipt,*input_,*ledger_);
        }
        if(!confirmed)shop_.failed_attempt();
        std::cout<<"p8-07 PURCHASE field="<<field<<" confirmed="<<confirmed<<" attempted="<<receipt.attempted<<'\n';return true;
    }
    Command advance(const sdk::Snapshot& snapshot,const LocalFrame& local) {
        auto& in=*input_;common::refresh_evidence(in,*ledger_);
        bool represented=reversible_workshop::own_market(in)&&local.fresh()&&in.frame.clock==local.frame.clock&&
            !local.fuel_resumed&&int(cambc::round_number())==in.frame.clock.round;
        if(arm==Arm::active&&work_.plan.organization==Organization::H&&work_.plan.collector==in.frame.clock.self) {
            const auto* component=ledger_->state(in.component_slot);const auto* full=ledger_->state(in.supported_slot);
            const auto* episode=ledger_->active();
            represented&=component&&full&&episode&&episode->context==in.supported_slot&&
                snapshot.frame.clock.round<=episode->deadline&&
                ledger_->component_qualified(in.component_slot,component->declaration.key,snapshot.frame.clock.round)&&
                !ledger_->suspended(in.supported_slot,snapshot.frame.clock.round/common::project_rounds)&&
                full->declaration.key.route_hash==plan_route_hash(work_.plan)&&
                full->declaration.key.settings_id==in.settings_identity.value&&full->declaration.key.threat_regime==in.threat_regime.value&&
                full->declaration.envelope.contains(snapshot.own,full->declaration.key.build);
        }
        // Entry permission and frozen transitions survive represented damage;
        // Work performs stage-specific crew/release checks. No remote actor is
        // recruited or switched when this own job loses a material premise.
        if(!represented){work_.request_stop();if(ledger_->active())ledger_->invalidate_active(snapshot.frame.clock.round);}
        return work_.step(local);
    }
    bool unchanged(const sdk::Snapshot& a,const sdk::Snapshot& b) const {
        if(!b.valid||a.frame.clock.round!=b.frame.clock.round||a.frame.clock.self!=b.frame.clock.self||
            a.own.equipment_hash!=b.own.equipment_hash||a.own.health!=b.own.health||a.own.max_health!=b.own.max_health||
            a.own.strength!=b.own.strength||a.own.focus!=b.own.focus||a.own.growth!=b.own.growth||a.own.bag!=b.own.bag||
            a.frame.own.gold!=b.frame.own.gold||a.frame.own.actor.position!=b.frame.own.actor.position)return false;
        for(int o=0;o<input_->offer_count;++o)for(int i=0;i<input_->offers[o].schedule.actor_count;++i) {
            const int id=input_->offers[o].schedule.actors[i].actor_id;const auto* x=a.frame.visible(id);const auto* y=b.frame.visible(id);
            if(!x||!y||x->health!=y->health||x->max_health!=y->max_health||x->strength!=y->strength||x->position!=y->position||
                x->bag!=y->bag||x->alive!=y->alive||x->channel!=y->channel)return false;
        }
        return true;
    }
    void observe_trace(const sdk::Snapshot& snapshot,const LocalFrame& local) {
        trace_.observe(local,work_,snapshot.spawn_shop_legal);
        if(!trace_.active||(work_.active&&ledger_->active()))return;
        const auto* e=ledger_->active();Premises terminal;if(e)terminal={e->premises.key,snapshot.own,all_premises};
        if(!work_.route_witness_active)terminal.personally_observed&=~route_known;
        for(int i=0;i<terminal.key.participant_count;++i)if(!snapshot.frame.visible(terminal.key.participants[i]))terminal.personally_observed&=~membership_known;
        const auto result=trace_.finish(*ledger_,snapshot.frame.clock.round,terminal);
        if(result.accepted)std::cout<<"p8-07 EPISODE positive="<<result.full_positive<<" contradiction="<<result.contradiction<<'\n';
    }
public:
    void fault(){work_.request_stop();work_.route_witness_active=false;if(ledger_&&ledger_->active())ledger_->invalidate_active(int(cambc::round_number()));}
    void turn() {
        const int entered=int(cambc::round_number());
        const auto room=[&](std::uint64_t reserve) {
            return int(cambc::round_number())==entered&&cambc::fuel_remaining()>=reserve;
        };
        if(!routes_.initialized()) {
            if(!room(70000))return;
            if(!diagnostic_routes_started_&&room(32000)) {
                diagnostic_routes_started_=true;
                std::printf("p8-07 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),entered,
                    static_cast<unsigned long long>(cambc::fuel_remaining()));
            }
            if(!room(70000))return;
            const bool complete=routes_.initialize_step(1);
            if(complete&&room(32000))
                std::printf("p8-07 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),entered,
                    static_cast<unsigned long long>(cambc::fuel_remaining()));
            return;
        }
        if(!work_.active&&room(70000))routes_.prepare();
        if(int(cambc::round_number())!=entered)return;
        const auto* selected=selected_site_<0?nullptr:&routes_.sites[selected_site_].group;
        auto snapshot=adapter_.observe(selected,&work_);if(!snapshot.valid){fault();return;}
        if(!ledger_)ledger_=std::make_unique<EvidenceLedger>(snapshot.frame.clock.self);
        const bool skipped=last_round_>=0&&snapshot.frame.clock.round!=last_round_+1;
        if(snapshot.frame.clock.round==last_round_)return;
        last_round_=snapshot.frame.clock.round;populate(snapshot);
        // Inspect one deterministic public geometry candidate per owner turn,
        // including productive turns. A long job cannot starve layout discovery.
        layout(snapshot);populate(snapshot);
        if(work_.active) {
            const auto certificates=current_safety(snapshot);bool safe=false;
            for(int i=0;i<initial_safety_count_;++i)if(certificates[i].actor==snapshot.frame.clock.self)
                safe=certificates[i].valid(snapshot.frame,snapshot.frame.clock.self,work_.plan.group);
            safe&=threats(work_.plan.group)==threat_;
            auto local=adapter_.local(snapshot,work_.plan,{certificates.data(),std::size_t(initial_safety_count_)},safe,calm(snapshot),productive(snapshot),escape(snapshot),&work_);
            local.fuel_resumed|=skipped;if(skipped)fault();dispatch(advance(snapshot,local),local);observe_trace(snapshot,local);return;
        }
        if(snapshot.frame.own.actor.channel!=p6::Channel::none||!snapshot.frame.own.actor.alive||snapshot.own.bag>0){fallback(snapshot);return;}
        freeze(snapshot);
        if(shop(snapshot))return;
        auto own=own_model(snapshot);if(!own){fallback(snapshot);return;}
        if(!menu(snapshot,*own)){fallback(snapshot);return;}
        if(layout_ready_&&producer(snapshot.frame.own.actor,snapshot.draft)) {
            input_->offers[0].context_slot=input_->component_slot;input_->offers[0].entry=input_->component_entry;
        }
        auto decision=layout_ready_?common_cause_firewall::decide(*input_,*ledger_,probes_,arm):Decision{};
        const bool raw=decision.option<0;
        // Ordinary startup or an unrepresented source remains a verified own
        // productive fallback. It grants no episode, shop permission, helper
        // invitation, or replacement for a frozen compartment's proof.
        if(raw&&(input_->offers[0].plan.organization!=Organization::U||input_->offers[0].capital_cost!=0)) {fallback(snapshot);return;}
        if(raw){decision.option=0;decision.clock=snapshot.frame.clock;decision.organization=Organization::U;decision.record_episode=false;}
        const auto planned=snapshot;const int action_group=input_->offers[decision.option].plan.group;
        int site=-1;for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)site=s;
        if(site<0)return;snapshot=adapter_.observe(&routes_.sites[site].group,&work_);
        if(!unchanged(planned,snapshot)){fault();return;}populate(snapshot);
        for(int o=0;o<input_->offer_count;++o)for(int c=0;c<input_->offers[o].safety_count;++c)input_->offers[o].safety[c].clock=snapshot.frame.clock;
        if(input_->visible_proposer.knowledge==p6::Knowledge::exact)input_->visible_proposer.clock=snapshot.frame.clock;
        if(!raw)decision=common_cause_firewall::decide(*input_,*ledger_,probes_,arm);else decision.clock=snapshot.frame.clock;
        if(decision.option<0||input_->offers[decision.option].plan.group!=action_group)return;
        auto& chosen=input_->offers[decision.option];auto local=adapter_.local(snapshot,chosen.plan,{chosen.safety.data(),std::size_t(chosen.safety_count)},true,calm(snapshot),true,escape(snapshot));
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<18000){fault();return;}
        Admission admitted;if(raw)admitted.started=work_.start(chosen.plan,local);
        else admitted=common_cause_firewall::commit(*input_,local,decision,*ledger_,probes_,arm,work_);
        if(!admitted.started){fallback(snapshot);return;}
        selected_site_=site;selected_bank_=own->bank;renewal_site_=chosen.plan.organization==Organization::H&&chosen.plan.collector!=snapshot.frame.clock.self?own->site:site;
        initial_safety_=chosen.safety;initial_safety_count_=chosen.safety_count;threat_=threats(chosen.plan.group);
        if(admitted.episode&&!trace_.begin(*ledger_,work_)){ledger_->invalidate_active(snapshot.frame.clock.round);work_.request_stop();}
        std::cout<<"p8-07 ADMIT group="<<chosen.plan.group<<" organization="<<int(chosen.plan.organization)<<" episode="<<admitted.episode<<'\n';
        inspection_turns_=0;inspection_site_=-1;unavailable_logged_=false;dispatch(work_.step(local),local);observe_trace(snapshot,local);
    }
};
inline void draft() {
    const auto own=cambc::draft::current_draft(cambc::team::ally);const int ordinal=int(own.size()),picks=int(cambc::draft::team_size());if(ordinal>=picks)return;
    DraftInputs in;in.ordinal=ordinal;in.picks=picks;in.prospective_actor=0;
    for(int i=0;i<ordinal&&i<5;++i)in.own_picks[i]=sdk::type(own[i]);in.settings=sdk::settings();in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
    const int window=std::min(common::forecast_window,in.settings.max_rounds);in.windows.deadlines=in.windows.windows={window,window,window};
    in.windows.sampled_actor_id=0;in.windows.sampled_round=0;in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto candidates=p8::native::draft_candidates(0,ordinal,ordinal!=0);const auto selected=common_cause_firewall::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-07 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
    for(const auto type:draft_priority(ordinal)){const auto pick=sdk::draft_type(type);if(std::find(own.begin(),own.end(),pick)==own.end()){cambc::draft::pick(pick);return;}}
}
inline int run() {
    std::unique_ptr<Runtime> bot;while(true) {
        try {
            if(cambc::is_draft())native::draft();else {
                const int entered=int(cambc::round_number());
                if(!bot) {
                    bot.reset(new Runtime);
                    if(int(cambc::round_number())==entered&&cambc::fuel_remaining()>=32000)
                        std::printf("p8-07 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),entered,
                            static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                if(int(cambc::round_number())==entered)bot->turn();
            }
        }
        catch(const cambc::error& e){std::cout<<"p8-07 ERROR engine "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& e){std::cout<<"p8-07 ERROR exception "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-07 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::common_cause_firewall::native

