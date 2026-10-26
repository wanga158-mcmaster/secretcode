#pragma once
#include "configuration.h"
#include "../draft.h"
#include <bit>
#include <cstdio>
#include <iostream>
#include <memory>
namespace p8::abstaining_specialists::native {
using namespace p8::native;
class Runtime {
    bool diagnostic_routes_started_=false;
    Routes routes_;sdk::Adapter adapter_;Execution execution_;
    std::unique_ptr<Inputs> input_=p8::native::make_default<Inputs>();std::unique_ptr<EvidenceLedger> ledger_;
    std::unique_ptr<Specialist> specialist_;Rebuild bootstrap_;p6::ScoreHistory scores_;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};int initial_safety_count_=0;
    int selected_site_=-1,selected_bank_=-1,renewal_site_=-1,last_round_=-1;
    int cheap_site_=-1,cheap_bank_=-1,outer_site_=-1,outer_bank_=-1,g_site_=-1,g_bank_=-1;
    int inspection_turns_=0,next_scan_=0,inspection_site_=-1,inspection_cursor_=0;
    Route inspection_{};std::uint64_t threat_=0;bool unavailable_logged_=false,banked_income_=false,return_available_=false;
    bool preparing_=false,bootstrap_failed_=false;int preparation_round_=-1,preparation_bound_=0;
    std::uint64_t preparation_generation_=0;NumericState bootstrap_target_{};
    std::array<HelperRegime,2> helper_regimes_{};bool helper_regimes_frozen_=false;
    bool helpers_current(const sdk::Snapshot& snapshot) const {
        if(!helper_regimes_frozen_||!carry_actor(snapshot.frame.own.actor,snapshot.draft))return false;
        for(int i=0;i<2;++i) {
            HelperRegime current;const auto& frozen=helper_regimes_[i];
            if(snapshot.draft.picks<=i+1||snapshot.draft.actors[i+1]!=frozen.actor||
                !helper_regime(snapshot,frozen.actor,frozen.bag_limit,current)||!same_helper_regime(frozen,current))return false;
        }return true;
    }
    bool guard_helpers(const sdk::Snapshot& snapshot) {
        if(helpers_current(snapshot))return true;
        // Unknown public capability supplies no new H evidence. Stop only our
        // dependency; Work still owns its safe hold/channel/bank/renewal.
        if(arm==Arm::active)specialist_->abstaining=true;
        if(execution_.work.active&&execution_.work.plan.organization==Organization::H&&
            execution_.work.plan.collector==snapshot.frame.clock.self) {
            execution_.work.request_stop();execution_.work.route_witness_active=false;
        }
        if(ledger_->active())ledger_->invalidate_active(snapshot.frame.clock.round);
        input_->loaded={};return false;
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
        const auto receipt=adapter_.dispatch(command,local,execution_.work);
        if (receipt.current_clock!=command.clock) {
            execution_.work.request_stop();execution_.work.route_witness_active=false;
            if (ledger_&&ledger_->active())ledger_->invalidate_active(receipt.current_clock.round);
            return;
        }
        execution_.work.confirm(command,receipt);
    }
    void manual(const Plan& plan,const LocalFrame& local,PhysicalAction action,p6::Point target={},Leg leg=Leg::ingress) {
        execution_.work.plan=plan;execution_.work.route_witness_active=false;
        dispatch(execution_.work.emit(local,action,target,leg),local);
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

    void populate(const sdk::Snapshot& snapshot,bool quotes=false) {
        auto& in=*input_;in.frame=snapshot.frame;
        if(in.settings_identity.source!=p6::Source::queried_setting) {
            in.settings=sdk::settings();
            in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
        }
        in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        in.target_group=cheap_site_<0?(in.offer_count?in.offers[0].plan.group:0):group_id(routes_.sites[cheap_site_].group);
        std::uint64_t regime=1469598103934665603ULL;
        for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake()) {
            const int group=group_id(camp.camp_group());bool expected=false;for(int site:{cheap_site_,outer_site_,g_site_})expected|=site>=0&&group_id(routes_.sites[site].group)==group;
            if(!expected){regime=hash_word(regime,std::uint64_t(sdk::identity(camp))+1);regime=hash_word(regime,std::bit_cast<std::uint64_t>(double(camp.strength())));}
        }
        for(const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
            regime=hash_word(regime,std::uint64_t(sdk::identity(enemy))+1);const auto position=enemy.position();regime=hash_word(regime,std::uint32_t(position.x));regime=hash_word(regime,std::uint32_t(position.y));
            regime=hash_word(regime,std::bit_cast<std::uint64_t>(double(enemy.strength())));regime=hash_word(regime,std::bit_cast<std::uint64_t>(double(enemy.focus())));
        }
        in.threat_regime={int(regime&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const auto enemy=cambc::global_submitted_gems(cambc::team::enemy);scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        if(quotes) {
            constexpr std::array<int,2> quote_ids{damage_item,focus_item};
            in.market=sdk::market(snapshot,quote_ids);
        }
        else {in.market=p8::native::exact_default<reversible_workshop::Market>();const auto held=sdk::own_inventory();if(held.valid()&&held.hash()==snapshot.own.equipment_hash)in.market.inventory={held,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};in.market.spawn_shop_legal=snapshot.spawn_shop_legal;}
        in.return_attribution_available={return_available_,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
    }
    std::unique_ptr<Solo> best_solo(const p6::Frame& frame,int fixed=-1,int bank=-1,std::span<const int> excluded={}) const {
        std::unique_ptr<Solo> best;
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>55000;++s) {
            if(fixed>=0&&s!=fixed)continue;if(std::find(excluded.begin(),excluded.end(),group_id(routes_.sites[s].group))!=excluded.end())continue;
            for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>55000;++b) {
                if(bank>=0&&b!=bank)continue;auto candidate=std::unique_ptr<Solo>(new Solo(solo(routes_,s,b,frame,true)));
                if(candidate->feasible&&(!best||candidate->plan.forecast_rounds<best->plan.forecast_rounds||
                    (candidate->plan.forecast_rounds==best->plan.forecast_rounds&&candidate->plan.group<best->plan.group)))best=std::move(candidate);
            }
        }return best;
    }
    std::vector<Solo> baselines(const p6::Frame& frame,const Solo& own,int target=-1) const {
        std::vector<Solo> out;out.reserve(3);out.push_back(own);normalize(out.back(),frame);
        for(int ordinal=0;ordinal<3;++ordinal) {
            const auto* actor=frame.visible(input_->draft.actors[ordinal]);if(!actor||!actor->alive||actor->bag||actor->channel!=p6::Channel::none)return {};
            if(actor->id==frame.clock.self)continue;std::array<int,4> excluded{};int count=0;
            for(const auto& prior:out)excluded[count++]=prior.plan.group;
            if(target>=0&&target!=own.plan.group)excluded[count++]=target;
            auto model=best_solo(subject(frame,*actor),-1,-1,{excluded.data(),std::size_t(count)});if(!model)return {};
            normalize(*model,frame);out.push_back(std::move(*model));
        }return out;
    }
    void generalist_model(const sdk::Snapshot& snapshot,const Plan& plan,const p6::CycleCertificate& safety) {
        if(!carry_actor(snapshot.frame.own.actor,snapshot.draft))return;
        auto state=snapshot.own;state.focus-=double(state.bag)*double(cambc::constants::passive_abilities::prism_focus_per_gem);state.bag=0;
        int site=-1;for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==plan.group)site=s;
        if(site<0)return;const auto declared=declaration(plan,state,*input_,routes_.sites[site].group.gem_value(),false);
        input_->generalist_bound[0]={declared,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        input_->generalist_loaded[0]=native::loaded(declared,safety,snapshot.frame.clock);
    }
    bool configure(const sdk::Snapshot& snapshot,const Solo& own) {
        if(specialist_->declared||ledger_->size()||!banked_income_||!bootstrap_.completed||bootstrap_failed_||!carry_actor(snapshot.frame.own.actor,snapshot.draft)||!snapshot.frame.own.at_spawn||
            snapshot.own.health!=snapshot.own.max_health||!input_->market.inventory.current(snapshot.frame.clock,snapshot.frame.clock.self)||
            input_->market.inventory.value!=inventory(1)||cambc::fuel_remaining()<75000)return false;
        std::array<HelperRegime,2> helpers{};p6::Gems bag_limit=cambc::constants::winning_gems;
        for(const auto& site:routes_.sites)bag_limit=std::max(bag_limit,p6::Gems(site.group.gem_value()));
        for(int i=0;i<2;++i)if(snapshot.draft.picks<=i+1||
            !helper_regime(snapshot,snapshot.draft.actors[i+1],bag_limit,helpers[i]))return false;
        const auto projected=project(snapshot,inventory(1),inventory(1,1)),plain=project(snapshot,inventory(1),inventory(2)),retained=project(snapshot,inventory(1),inventory(2,1));
        if(!projected.valid||!plain.valid||!retained.valid)return false;
        if(routes_.sites[own.site].rally_count<2)return false;
        for(int r=0;r<2;++r)if(!routes_.sites[own.site].rally[r].ready)return false;
        int outer=-1,g=-1,g_bank=-1;
        for(int s=0;s<int(routes_.sites.size());++s)if(s!=own.site&&routes_.sites[s].rally_count>=2&&routes_.sites[s].rally[0].ready&&routes_.sites[s].rally[1].ready){outer=s;break;}
        if(outer<0)return false;std::unique_ptr<Solo> generalist;
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>55000;++s)if(s!=outer&&s!=own.site) {
            auto candidate=best_solo(plain.frame,s);if(candidate){g=s;g_bank=candidate->bank;generalist=std::move(candidate);break;}
        }
        if(!generalist)return false;
        // These are declarations of exact public routes and a common120-turn
        // physical envelope, not projected cooperation, income or receipts.
        auto cheap=std::unique_ptr<Plan>(new Plan(formation_plan(routes_,own.site,own.bank,snapshot.frame,snapshot.draft,Organization::H,true)));
        auto outer_plan=std::unique_ptr<Plan>(new Plan(formation_plan(routes_,outer,own.bank,snapshot.frame,snapshot.draft,Organization::H,true)));
        if(!plan_valid(*cheap,snapshot.frame.clock.self)||!plan_valid(*outer_plan,snapshot.frame.clock.self))return false;
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<30000)return false;
        for(int i=0;i<2;++i) {
            HelperRegime current;
            if(!helper_regime(snapshot,helpers[i].actor,bag_limit,current)||!same_helper_regime(helpers[i],current))return false;
        }
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<30000)return false;
        generalist->plan.organization=Organization::G;
        cheap_site_=own.site;cheap_bank_=own.bank;outer_site_=outer;outer_bank_=own.bank;g_site_=g;g_bank_=g_bank;populate(snapshot);
        auto h=std::make_unique<std::array<Configuration,2>>(),fallback=std::make_unique<std::array<Configuration,2>>();
        auto first=declaration(*cheap,snapshot.own,*input_,routes_.sites[own.site].group.gem_value(),true);
        auto second=declaration(*outer_plan,projected.state,*input_,routes_.sites[outer].group.gem_value(),true);
        (*h)[0]={*cheap,first.key,inventory(1),ledger_->predeclare(first)};(*h)[1]={*outer_plan,second.key,inventory(1,1),ledger_->predeclare(second)};
        const auto gd=declaration(generalist->plan,plain.state,*input_,routes_.sites[g].group.gem_value(),false);
        const auto gs=declaration(generalist->plan,retained.state,*input_,routes_.sites[g].group.gem_value(),false);
        (*fallback)[0]={generalist->plan,gd.key,inventory(2),-1};(*fallback)[1]={generalist->plan,gs.key,inventory(2,1),-1};
        if(!specialist_->declare(*h,*fallback,*ledger_))return false;
        helper_regimes_=helpers;helper_regimes_frozen_=true;
        std::cout<<"p8-14 DECLARED cheap="<<first.key.source_group<<" focus="<<second.key.source_group<<" generalist="<<gd.key.source_group<<'\n';return true;
    }
    bool menu(const sdk::Snapshot& snapshot,const Solo& own) {
        auto& in=*input_;in.offer_count=1;in.independent=0;in.collector_proposal={};in.private_income={};
        if(!independent_offer(in.offers[0],own,{&own,1},snapshot.frame))return false;
        if(!specialist_->declared||!carry_actor(snapshot.frame.own.actor,snapshot.draft)){if(!carry_actor(snapshot.frame.own.actor,snapshot.draft))helper_menu(snapshot,own);return true;}
        in.offers[0].plan.organization=Organization::G;generalist_model(snapshot,in.offers[0].plan,own.model.certificate);
        if(!guard_helpers(snapshot))return true;
        for(int i=0;i<2;++i) {
            const int site=i?outer_site_:cheap_site_,bank=i?outer_bank_:cheap_bank_;
            auto alternatives=baselines(snapshot.frame,own,group_id(routes_.sites[site].group));if(alternatives.size()!=3)continue;
            auto entry=snapshot.frame;if(!snapshot.frame.own.at_spawn)entry.own.actor.position=routes_.spawn.destination;
            auto candidate=std::unique_ptr<Coalition>(new Coalition(coalition(routes_,site,bank,entry,snapshot.draft,alternatives,Organization::H,true)));
            if(!candidate->feasible)continue;candidate->offer.context_slot=specialist_->supported[i].context;candidate->offer.entry=specialist_->supported[i].entry;
            candidate->offer.plan.forecast_rounds=specialist_->supported[i].plan.forecast_rounds;
            if(!same_plan(candidate->offer.plan,specialist_->supported[i].plan)||!delayed(candidate->offer,setup(snapshot,false),snapshot.frame))continue;
            const auto* safe=common::certificate(candidate->offer,snapshot.frame.clock.self);const auto* slot=ledger_->state(candidate->offer.context_slot);
            if(!safe||!slot)continue;in.loaded[i]=native::loaded(slot->declaration,*safe,snapshot.frame.clock);
            if(!independent_offer(in.offers[0],own,alternatives,snapshot.frame))return false;in.offers[0].plan.organization=Organization::G;
            in.offers[in.offer_count++]=candidate->offer;
        }return true;
    }
    void helper_menu(const sdk::Snapshot& snapshot,const Solo& own) {
        if(snapshot.frame.own.actor.pick_ordinal!=2)return;const auto* leader=snapshot.frame.visible(snapshot.draft.actors[0]);
        if(!leader||!carry_actor(*leader,snapshot.draft)||leader->bag||leader->channel!=p6::Channel::none)return;
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>85000;++s) {
            if(routes_.sites[s].rally_count<2||leader->position!=routes_.sites[s].rally[0].destination||own.plan.group==group_id(routes_.sites[s].group))continue;
            auto peers=baselines(snapshot.frame,own,group_id(routes_.sites[s].group));if(peers.size()!=3)continue;
            for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>85000;++b) {
                auto support=std::unique_ptr<Coalition>(new Coalition(coalition(routes_,s,b,snapshot.frame,snapshot.draft,peers,Organization::H,true)));
                if(!support->feasible)continue;
                if(!independent_offer(input_->offers[0],own,peers,snapshot.frame))return;
                input_->offers[1]=support->offer;input_->offer_count=2;
                input_->collector_proposal={support->offer.plan.group,snapshot.frame.clock,leader->id,p6::Knowledge::exact};return;
            }
        }
    }
    bool prospective(sdk::Snapshot snapshot,const Solo& ordinary,const Inventory& target,int kind,bool require_gain=true) {
        auto& in=*input_;const auto held=in.market.inventory.value;const auto future=project(snapshot,held,target);if(!future.valid)return false;
        const auto funds=reversible_workshop::funded(in,held,target);const int preparation=setup(snapshot,true);
        if(!funds.valid||funds.refunds||preparation<1||snapshot.frame.clock.round+preparation>=in.settings.max_rounds)return false;
        auto frame=future.frame;frame.own.actor.position=routes_.spawn.destination;
        const int site=kind==0?outer_site_:kind<0?ordinary.site:g_site_,bank=kind==0?outer_bank_:kind<0?ordinary.bank:g_bank_;
        if(site<0)return false;auto after=best_solo(frame,kind==0?ordinary.site:site,kind==0?ordinary.bank:bank);if(!after)return false;
        auto unchanged=std::make_unique<Offer>(),changed=std::make_unique<Offer>();
        if(kind==0) {
            if(!guard_helpers(snapshot))return false;
            auto old_peers=baselines(snapshot.frame,ordinary,group_id(routes_.sites[site].group));if(old_peers.size()!=3)return false;
            auto peers=old_peers;for(auto& prior:peers)if(prior.model.schedule.actors[0].actor_id==snapshot.frame.clock.self)prior=*after;
            if(!independent_offer(*unchanged,ordinary,old_peers,snapshot.frame))return false;
            auto coalition=std::unique_ptr<Coalition>(new Coalition(p8::native::coalition(routes_,site,bank,frame,snapshot.draft,peers,Organization::H,true)));
            coalition->offer.plan.forecast_rounds=specialist_->supported[1].plan.forecast_rounds;
            if(!coalition->feasible||!same_plan(coalition->offer.plan,specialist_->supported[1].plan))return false;
            *changed=coalition->offer;changed->context_slot=specialist_->supported[1].context;changed->entry=specialist_->supported[1].entry;
            const auto* safe=common::certificate(*changed,snapshot.frame.clock.self);const auto* declared=ledger_->state(changed->context_slot);
            if(!safe||!declared)return false;in.loaded[1]=native::loaded(declared->declaration,*safe,snapshot.frame.clock);
            if(!loaded_forecast(in,in.loaded[1],declared->declaration,changed->plan))return false;
            in.offers[0]=*unchanged;in.offers[1]=*changed;in.offer_count=2;
        } else {
            after->plan.organization=Organization::G;
            if(!independent_offer(*changed,*after,{after.get(),1},snapshot.frame)||!independent_offer(*unchanged,ordinary,{&ordinary,1},snapshot.frame))return false;
        }
        double ambient=0;for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake())ambient+=camp.strength();
        for(const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
            double cast=0;for(const auto& ability:enemy.abilities())if(ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))cast=std::max(cast,double(ability.damage_amount()));
            ambient+=enemy.strength()+cast;
        }
        for(int c=0;c<changed->safety_count;++c) {
            auto safe=changed->safety[c];if(common::participant(changed->plan,safe.actor))for(auto* exposure:{&safe.clear,&safe.hold,&safe.bank,&safe.recovery})exposure->incoming_total+=ambient*preparation;
            if(!safe.valid(snapshot.frame,safe.actor,safe.group))return false;
        }
        auto compared=std::make_unique<PurchaseComparison>();if(!comparison(*compared,in,*changed,*unchanged,funds.purchases,preparation))return false;
        for(int a=0;a<compared->changed.actor_count;++a)if(snapshot.frame.clock.round+compared->changed.actors[a].occupied_turns.value>in.settings.max_rounds)return false;
        const bool gain=p6::compare(common::evaluate(in,compared->changed),common::evaluate(in,compared->unchanged))>0;
        if(kind>=0)in.purchases[kind]=*compared;
        return !require_gain||gain;
    }
    void return_spawn(sdk::Snapshot& snapshot) {
        const bool safe=calm(snapshot);Plan plan;plan.collector=snapshot.frame.clock.self;plan.group=selected_site_<0?-1:group_id(routes_.sites[selected_site_].group);
        if(safe&&snapshot.frame.own.at_spawn&&snapshot.frame.own.actor.position!=routes_.spawn.destination&&routes_.spawn.ready) {
            const auto route=routes_.route(snapshot.frame.own.actor.position,routes_.spawn);bool legal=route_valid(route)&&route.count<=9;
            for(int i=0;legal&&i<route.count;++i)legal=cambc::map::spawn_region(cambc::team::ally).is_set(tile(route.tiles[i]));
            if(legal&&route.count>1){plan.ingress=route;auto local=adapter_.local(snapshot,plan,{},true,true,false);manual(plan,local,PhysicalAction::move,route.tiles[1]);return;}
        }
        auto local=adapter_.local(snapshot,plan,{},safe,safe,false,escape(snapshot));
        const auto action=snapshot.frame.own.actor.channel!=p6::Channel::none&&snapshot.frame.own.actor.channel_valid&&safe?PhysicalAction::preserve_channel:
            snapshot.frame.own.at_spawn?PhysicalAction::recover:safe?PhysicalAction::recall:PhysicalAction::yield;
        manual(plan,local,action);
    }
    bool purchase(sdk::Snapshot& snapshot,const Solo& ordinary) {
        if(!carry_actor(snapshot.frame.own.actor,snapshot.draft)||execution_.work.active||snapshot.own.bag||snapshot.frame.own.actor.channel!=p6::Channel::none||!banked_income_)return false;
        populate(snapshot,true);auto& in=*input_;if(!in.market.inventory.current(snapshot.frame.clock,snapshot.frame.clock.self))return false;
        const bool bootstrap=!specialist_->declared&&in.market.inventory.value==Inventory{}&&!bootstrap_failed_;
        int kind=-2;Inventory target;
        if(bootstrap){kind=-1;target=inventory(1);}
        else if(specialist_->declared&&arm==Arm::active&&specialist_->abstaining) {
            if(in.market.inventory.value==specialist_->original){kind=1;target=specialist_->generalist[0].inventory;}
            else if(in.market.inventory.value==specialist_->supported[1].inventory){kind=2;target=specialist_->generalist[1].inventory;}
        } else if(specialist_->declared&&(qualified(in,*ledger_,specialist_->supported[0])||permit_current(in,*specialist_))) {kind=0;target=specialist_->supported[1].inventory;}
        if(kind==-2||!prospective(snapshot,ordinary,target,kind))return false;
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<30000)return false;
        if(!snapshot.frame.own.at_spawn) {
            if(preparation_generation_==execution_.work.generation)return false;
            preparation_generation_=execution_.work.generation;preparing_=true;preparation_round_=snapshot.frame.clock.round;preparation_bound_=setup(snapshot,true);return_spawn(snapshot);return true;
        }
        auto local=adapter_.local(snapshot,ordinary.plan,{&ordinary.model.certificate,1},calm(snapshot),calm(snapshot),true);
        if(bootstrap) {
            const auto projected=project(snapshot,Inventory{},target);if(!projected.valid)return false;
            if(!bootstrap_.active&&!bootstrap_.begin(in,local,execution_.work,target))return false;
            bootstrap_target_=projected.state;
            const auto command=bootstrap_.next(in,local,execution_.work);if(!command.valid)return false;
            const auto result=sdk::transact(adapter_,snapshot,command);if(!result.after.valid){bootstrap_.interrupt();bootstrap_failed_|=result.receipt.attempted;return result.receipt.attempted;}populate(result.after,true);
            bool confirmed=bootstrap_.confirm(command,result.receipt,*input_);
            const auto& actual=result.after.own;
            confirmed&=actual.equipment_hash==bootstrap_target_.equipment_hash&&actual.max_health==bootstrap_target_.max_health&&actual.strength==bootstrap_target_.strength&&actual.focus==bootstrap_target_.focus&&actual.growth==bootstrap_target_.growth;
            if(!confirmed){bootstrap_failed_=true;bootstrap_.interrupt();}
            else {snapshot=result.after;std::cout<<"p8-14 INITIAL_D_CONFIRMED real_empty_income=1\n";}
            return result.receipt.attempted;
        }
        if(kind==0) {
            if(!guard_helpers(snapshot)||int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000)return false;
            if(!specialist_->permit.frozen)prepare_specialist(in,local,*specialist_,*ledger_,execution_.work);
        }
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000)return false;
        if(!begin_purchase(in,local,*specialist_,*ledger_,arm,execution_.work))return false;
        const auto command=purchase_step(in,local,*specialist_,execution_.work);if(!command.valid)return false;
        const auto result=sdk::transact(adapter_,snapshot,command);if(!result.after.valid){specialist_->rebuild.interrupt();specialist_->abstaining=true;return result.receipt.attempted;}populate(result.after,true);
        if(!confirm_purchase(command,result.receipt,*input_,*specialist_)){specialist_->rebuild.interrupt();specialist_->abstaining=true;}
        else {snapshot=result.after;std::cout<<"p8-14 PURCHASE_CONFIRMED direction="<<kind<<" focus_retained="<<input_->market.inventory.value.count[focus_item]<<'\n';}
        return result.receipt.attempted;
    }
    bool unchanged(const sdk::Snapshot& a,const sdk::Snapshot& b)const {
        if(!b.valid||a.frame.clock.round!=b.frame.clock.round||a.frame.clock.self!=b.frame.clock.self||a.own.health!=b.own.health||a.own.max_health!=b.own.max_health||
            a.own.strength!=b.own.strength||a.own.focus!=b.own.focus||a.own.growth!=b.own.growth||a.own.bag!=b.own.bag||a.own.equipment_hash!=b.own.equipment_hash||
            a.frame.own.gold!=b.frame.own.gold||a.frame.own.actor.position!=b.frame.own.actor.position)return false;
        for(int o=0;o<input_->offer_count;++o) {
          for(int c=0;c<input_->offers[o].safety_count;++c) {
            const int id=input_->offers[o].safety[c].actor;const auto* x=a.frame.visible(id);const auto* y=b.frame.visible(id);
            if(!x||!y||x->position!=y->position||x->bag!=y->bag||x->health!=y->health||x->max_health!=y->max_health||x->strength!=y->strength||x->channel!=y->channel||x->alive!=y->alive)return false;
        }
        }
        return true;
    }
    Decision decision(const sdk::Snapshot& snapshot) {
        auto out=abstaining_specialists::decide(*input_,*specialist_,*ledger_,arm);
        if(out.option>=0&&input_->offers[out.option].plan.organization==Organization::H&&input_->offers[out.option].plan.collector==snapshot.frame.clock.self&&
            snapshot.frame.own.actor.position!=input_->offers[out.option].plan.ingress.tiles[0]&&preparation_generation_==execution_.work.generation){out=Decision{};out.clock=snapshot.frame.clock;}
        if(out.option<0&&common::legal_offer(*input_,input_->offers[0])==Rejection::none&&independent(*input_,input_->offers[0])) {
            const auto held=input_->market.inventory.value;
            const bool valid=!carry_actor(snapshot.frame.own.actor,snapshot.draft)||(actual_loaded(snapshot,held)&&
                (!specialist_->declared||arm==Arm::control||!specialist_->abstaining||input_->offers[0].plan.group==group_id(routes_.sites[g_site_].group)));
            if(valid){out.option=0;out.clock=snapshot.frame.clock;out.organization=input_->offers[0].plan.organization;out.reason=Rejection::none;out.record_episode=false;}
        }return out;
    }
    void observed(const sdk::Snapshot& snapshot,const LocalFrame& local) {
        auto& work=execution_.work;if(work.bank_seen&&work.bank_lower>0)banked_income_=true;
        if(!work.active&&work.plan.collector==snapshot.frame.clock.self&&work.payout_seen)return_available_=work.bank_seen&&work.renewal_seen&&work.route_witness_active;
        execution_.trace.observe(local,work,snapshot.spawn_shop_legal);
        if(!execution_.trace.active||(work.active&&ledger_->active()))return;
        auto terminal=Premises{execution_.issued,snapshot.own,all_premises};
        if(!work.route_witness_active)terminal.personally_observed&=~route_known;
        for(int p=0;p<terminal.key.participant_count;++p)if(!snapshot.frame.visible(terminal.key.participants[p]))terminal.personally_observed&=~membership_known;
        const auto result=execution_.trace.finish(*ledger_,snapshot.frame.clock.round,terminal);
        if(result.accepted)std::cout<<"p8-14 EPISODE full_positive="<<result.full_positive<<" contradiction="<<result.contradiction<<'\n';
    }
    Admission admit(const sdk::Snapshot& snapshot,const LocalFrame& local,const Decision& selected) {
        Admission out;if(selected.option<0||execution_.work.active)return out;auto& offer=input_->offers[selected.option];
        if(!physical_admissible(offer.plan,local)||int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000)return out;
        for(int a=0;a<offer.schedule.actor_count;++a)if(snapshot.frame.clock.round+offer.schedule.actors[a].occupied_turns.value>input_->settings.max_rounds)return out;
        if(offer.plan.organization==Organization::H&&snapshot.frame.clock.round+offer.plan.forecast_rounds>=input_->settings.max_rounds)return out;
        if(offer.plan.organization==Organization::H&&offer.plan.collector==snapshot.frame.clock.self) {
            if(!guard_helpers(snapshot)||int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000)return out;
        }
        bool record=selected.record_episode;
        if(arm==Arm::control&&specialist_->declared&&exact(offer,specialist_->supported[0])) {
            const auto* slot=ledger_->state(offer.context_slot);
            record=slot&&slot->campaign_episodes<2&&!ledger_->suspended(offer.context_slot,snapshot.frame.clock.round/common::project_rounds)&&
                common::context_matches(*input_,offer)&&loaded_now(*input_,input_->loaded[0],slot->declaration,offer.plan);
        }
        if(record) {
            const auto* slot=ledger_->state(offer.context_slot);if(!slot)return out;int duration=offer.plan.forecast_rounds;
            for(int h=0;h<slot->declaration.hypothesis_count;++h)if(slot->consistent[h])duration=std::max(duration,offer.plan.forecast_rounds+slot->declaration.hypotheses[h].offset.upper);
            if(snapshot.frame.clock.round+duration>=input_->settings.max_rounds||
                (slot->latest_positive>=0&&snapshot.frame.clock.round+duration-slot->latest_positive>=qualification_rounds))return out;
            if(exact(offer,specialist_->supported[1])&&(!specialist_->permit.used||specialist_->permit.proof_round<0||
                snapshot.frame.clock.round+duration-specialist_->permit.proof_round>=qualification_rounds))return out;
            Premises premises{offer.entry,snapshot.own,all_premises};
            if(!slot->campaign_started&&!ledger_->begin_initial_campaign(offer.context_slot,snapshot.frame.clock.round,{premises,true,true,true}))return out;
            EpisodeRequest request;request.context=offer.context_slot;request.issue_round=snapshot.frame.clock.round;
            request.deadline=request.issue_round+std::min(common::forecast_window,offer.plan.remaining_rounds);
            const int own=p6::actor_index(offer.schedule,snapshot.frame.clock.self);if(own<0)return out;request.nominal_duration=int(offer.schedule.actors[own].occupied_turns.value);
            request.action=int(offer.plan.organization);request.premises=premises;out.episode=ledger_->issue(request);if(!out.episode)return out;execution_.issued=offer.entry;specialist_->trial_seen=true;
        }
        out.started=execution_.work.start(offer.plan,local);out.rejection=out.started?Rejection::none:Rejection::unsafe;
        if(!out.started&&out.episode)ledger_->invalidate_active(snapshot.frame.clock.round);
        if(out.started&&out.episode&&!execution_.trace.begin(*ledger_,execution_.work)){ledger_->invalidate_active(snapshot.frame.clock.round);execution_.work.request_stop();}
        return out;
    }
public:
    void fault(){execution_.work.request_stop();execution_.work.route_witness_active=false;if(ledger_&&ledger_->active())ledger_->invalidate_active(int(cambc::round_number()));}
    void turn();
};
inline void Runtime::turn() {
    const int startup_round=int(cambc::round_number());
    const auto startup_room=[&](std::uint64_t reserve) {
        return int(cambc::round_number())==startup_round&&cambc::fuel_remaining()>=reserve;
    };
    if(!routes_.initialized()) {
        if(!startup_room(70000))return;
        if(!diagnostic_routes_started_&&startup_room(32000)) {
            std::printf("p8-14 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                startup_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
            diagnostic_routes_started_=true;
        }
        if(!startup_room(70000))return;
        const bool complete=routes_.initialize_step(1);
        if(complete&&startup_room(32000))
            std::printf("p8-14 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                startup_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
        return;
    }
    if(!execution_.work.active&&startup_room(70000))routes_.prepare();
    if(!startup_room(30000))return;
    auto snapshot=adapter_.observe(selected_site_<0?nullptr:&routes_.sites[selected_site_].group,&execution_.work);
    if(!snapshot.valid){fault();return;}
    if(!ledger_){ledger_=std::make_unique<EvidenceLedger>(snapshot.frame.clock.self);specialist_=std::make_unique<Specialist>(snapshot.frame.clock.self);}
    if(snapshot.frame.clock.round==last_round_)return;const bool skipped=last_round_>=0&&snapshot.frame.clock.round!=last_round_+1;last_round_=snapshot.frame.clock.round;populate(snapshot);
    if(execution_.work.active) {
        bool helpers_valid=true;
        if(execution_.work.plan.organization==Organization::H&&execution_.work.plan.collector==snapshot.frame.clock.self)
            helpers_valid=guard_helpers(snapshot);
        auto safe=current_safety(snapshot);bool valid=false;
        const auto held=sdk::own_inventory();const bool prism=carry_actor(snapshot.frame.own.actor,snapshot.draft);
        const bool changed=threats(execution_.work.plan.group)!=threat_;
        if(changed) {
            double stress=0;for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake())stress+=camp.strength();
            for(const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
                double cast=0;for(const auto& ability:enemy.abilities())if(ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))cast=std::max(cast,double(ability.damage_amount()));
                stress+=enemy.strength()+cast;
            }
            stress*=std::max(1,execution_.work.plan.forecast_rounds);
            for(int c=0;c<initial_safety_count_;++c)for(auto* exposure:{&safe[c].clear,&safe[c].hold,&safe[c].bank,&safe[c].recovery})exposure->incoming_total+=stress;
            fault();
        }
        for(int c=0;c<initial_safety_count_;++c)if(safe[c].actor==snapshot.frame.clock.self) {
            valid=safe[c].valid(snapshot.frame,safe[c].actor,execution_.work.plan.group);
            if(prism&&specialist_->declared) {
                if(helpers_valid)for(int i=0;i<2;++i)if(const auto* slot=ledger_->state(specialist_->supported[i].context))input_->loaded[i]=native::loaded(slot->declaration,safe[c],snapshot.frame.clock);
                generalist_model(snapshot,execution_.work.plan,safe[c]);
            }
        }
        if(prism&&!actual_loaded(snapshot,held)){execution_.work.request_stop();specialist_->abstaining=true;if(ledger_->active())ledger_->invalidate_active(snapshot.frame.clock.round);}
        if(execution_.work.plan.organization==Organization::H&&execution_.work.plan.collector!=snapshot.frame.clock.self&&
            execution_.work.stage!=PhysicalStage::bank&&execution_.work.stage!=PhysicalStage::renew) {
            const auto* collector=snapshot.frame.visible(execution_.work.plan.collector);bool retained=false;
            if(collector&&selected_site_>=0) {
                const auto route=routes_.route(execution_.work.plan.pads[0],routes_.sites[selected_site_].capture);
                for(int i=0;i<route.count;++i)retained|=collector->position==route.tiles[i];
            }
            if(!retained)execution_.work.request_stop();
        }
        if(specialist_->declared)abstaining_specialists::refresh(*input_,*specialist_,*ledger_);
        if(arm==Arm::active&&prism&&specialist_->abstaining&&execution_.work.plan.organization==Organization::H){execution_.work.request_stop();if(ledger_->active())ledger_->invalidate_active(snapshot.frame.clock.round);}
        auto local=adapter_.local(snapshot,execution_.work.plan,{safe.data(),std::size_t(initial_safety_count_)},valid,calm(snapshot),productive(snapshot),escape(snapshot),&execution_.work);
        local.fuel_resumed|=skipped;if(skipped)fault();
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<12000){fault();return;}
        dispatch(execution_.work.step(local),local);observed(snapshot,local);return;
    }
    if(snapshot.own.bag||!snapshot.frame.own.actor.alive||snapshot.frame.own.actor.channel!=p6::Channel::none){fallback(snapshot);return;}
    if(preparing_) {
        if(snapshot.frame.clock.round-preparation_round_>preparation_bound_){preparing_=false;std::cout<<"p8-14 PREPARATION_UNAVAILABLE\n";}
        else if(!snapshot.frame.own.at_spawn||snapshot.frame.own.actor.position!=routes_.spawn.destination||snapshot.own.health<snapshot.own.max_health){return_spawn(snapshot);return;}
        else preparing_=false;
    }
    const bool abstaining=arm==Arm::active&&specialist_->declared&&specialist_->abstaining;
    std::unique_ptr<Solo> own;
    if(abstaining)own=best_solo(snapshot.frame,g_site_,g_bank_);
    else if(selected_site_>=0)own=best_solo(snapshot.frame,selected_site_,selected_bank_);
    if(!own&&!abstaining)own=best_solo(snapshot.frame);
    if(!own){fallback(snapshot);return;}if(carry_actor(snapshot.frame.own.actor,snapshot.draft))own->plan.organization=Organization::G;
    if(!menu(snapshot,*own)){fallback(snapshot);return;}
    if(specialist_->declared)abstaining_specialists::refresh(*input_,*specialist_,*ledger_);
    if(arm==Arm::active&&specialist_->declared&&specialist_->abstaining&&carry_actor(snapshot.frame.own.actor,snapshot.draft)&&own->site!=g_site_) {
        own=best_solo(snapshot.frame,g_site_,g_bank_);if(!own){fallback(snapshot);return;}
        own->plan.organization=Organization::G;if(!menu(snapshot,*own)){fallback(snapshot);return;}
    }
    if(carry_actor(snapshot.frame.own.actor,snapshot.draft)&&purchase(snapshot,*own))return;
    populate(snapshot,true);configure(snapshot,*own);populate(snapshot);if(!menu(snapshot,*own)){fallback(snapshot);return;}
    if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000){fault();return;}
    auto chosen=decision(snapshot);if(chosen.option<0){fallback(snapshot);return;}
    auto& before=input_->offers[chosen.option];
    if(before.plan.organization==Organization::H&&before.plan.collector==snapshot.frame.clock.self&&snapshot.frame.own.actor.position!=before.plan.ingress.tiles[0]) {
        if(preparation_generation_!=execution_.work.generation&&setup(snapshot,false)>0) {
            preparation_generation_=execution_.work.generation;preparing_=true;preparation_round_=snapshot.frame.clock.round;preparation_bound_=setup(snapshot,false);return_spawn(snapshot);return;
        }fallback(snapshot);return;
    }
    const auto planned=snapshot;const int action_group=before.plan.group;int site=-1;
    for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)site=s;
    if(site<0)return;snapshot=adapter_.observe(&routes_.sites[site].group,&execution_.work);
    if(!unchanged(planned,snapshot)){fault();return;}populate(snapshot);
    for(int o=0;o<input_->offer_count;++o)for(int c=0;c<input_->offers[o].safety_count;++c)input_->offers[o].safety[c].clock=snapshot.frame.clock;
    for(int i=0;i<2;++i){if(input_->loaded[i].knowledge==p6::Knowledge::exact)input_->loaded[i].clock=snapshot.frame.clock;
        if(input_->generalist_bound[i].knowledge==p6::Knowledge::exact)input_->generalist_bound[i].clock=snapshot.frame.clock;
        if(input_->generalist_loaded[i].knowledge==p6::Knowledge::exact)input_->generalist_loaded[i].clock=snapshot.frame.clock;}
    if(input_->collector_proposal.knowledge==p6::Knowledge::exact)input_->collector_proposal.clock=snapshot.frame.clock;
    if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000){fault();return;}
    chosen=decision(snapshot);if(chosen.option<0||input_->offers[chosen.option].plan.group!=action_group)return;auto& offered=input_->offers[chosen.option];
    auto local=adapter_.local(snapshot,offered.plan,{offered.safety.data(),std::size_t(offered.safety_count)},true,calm(snapshot),true,escape(snapshot));
    if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000){fault();return;}
    const auto admitted=admit(snapshot,local,chosen);if(!admitted.started){fallback(snapshot);return;}
    selected_site_=renewal_site_=site;selected_bank_=own->bank;initial_safety_=offered.safety;initial_safety_count_=offered.safety_count;threat_=threats(action_group);
    inspection_turns_=0;inspection_site_=-1;unavailable_logged_=false;
    std::cout<<"p8-14 ADMIT group="<<action_group<<" organization="<<int(offered.plan.organization)<<" episode="<<admitted.episode<<" abstaining="<<specialist_->abstaining<<'\n';
    dispatch(execution_.work.step(local),local);observed(snapshot,local);
}
inline void draft() {
    const auto own=cambc::draft::current_draft(cambc::team::ally);const int ordinal=int(own.size()),picks=int(cambc::draft::team_size());if(ordinal>=picks)return;
    DraftInputs in;in.ordinal=ordinal;in.picks=picks;in.prospective_actor=0;
    for(int i=0;i<ordinal&&i<5;++i)in.own_picks[i]=sdk::type(own[i]);in.settings=sdk::settings();in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
    const int window=std::min(common::forecast_window,in.settings.max_rounds);in.windows.deadlines=in.windows.windows={window,window,window};
    in.windows.sampled_actor_id=0;in.windows.sampled_round=0;in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto candidates=p8::native::draft_candidates(0,ordinal,true);const auto selected=abstaining_specialists::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-14 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
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
                        std::printf("p8-14 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                            construction_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                bot->turn();
            }
        }
        catch(const cambc::error& e){std::cout<<"p8-14 ERROR engine "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& e){std::cout<<"p8-14 ERROR exception "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-14 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::abstaining_specialists::native

