#pragma once
#include "configuration.h"
#include "../draft.h"
#include <bit>
#include <cstdio>
#include <iostream>
#include <memory>
namespace p8::requalification_reserve::native {
using namespace p8::native;
// Native subset: one frozen 0/3 to 0/4 membership change, with own receipts.
// Visible capability never becomes an actual ally attack receipt.
class Runtime {
    bool diagnostic_routes_started_=false;
    Routes routes_;sdk::Adapter adapter_;Reserve reserve_;ProbeBudget probes_;CycleTrace trace_;Work work_;
    std::unique_ptr<Inputs> input_=p8::native::make_default<Inputs>();std::unique_ptr<EvidenceLedger> ledger_;p6::ScoreHistory scores_;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};int initial_safety_count_=0;
    int selected_site_=-1,selected_bank_=-1,renewal_site_=-1,last_round_=-1;
    int context_site_=-1,context_bank_=-1;bool preempted_=false,retired_logged_=false,proof_logged_=false;
    std::array<unsigned,2> helper_attempts_{};

    int inspection_turns_=0,next_scan_=0,inspection_site_=-1,inspection_cursor_=0;
    Route inspection_{};std::uint64_t threat_=0;bool unavailable_logged_=false;
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
    bool fresh(const sdk::Snapshot& snapshot,std::uint64_t reserve=25000) const {
        return snapshot.valid&&int(cambc::round_number())==snapshot.frame.clock.round&&cambc::fuel_remaining()>=reserve;
    }
    void readiness(p6::Frame& frame,int site) const {
        if(site<0)return;const auto& target=routes_.sites[site];const int id=group_id(target.group);
        for(const auto& ally:cambc::vision::champions(cambc::team::ally)) {
            const int actor=sdk::identity(ally);auto* represented=actor==frame.clock.self?&frame.own.actor:nullptr;
            for(int i=0;i<frame.actor_count;++i)if(frame.actors[i].value.id==actor)represented=&frame.actors[i].value;
            if(!represented||!represented->alive||!represented->can_attack||ally.buffs().has_stun())continue;
            const auto range=ally.attack_tiles();const auto sight=ally.visible_tiles_from(ally.position());
            for(const auto& camp:target.group)if(camp.is_visible()&&camp.is_alive()&&range.is_set(camp.position())&&sight.is_set(camp.position())) {
                represented->ready_attack_group=id;break;
            }
        }
        // This is personally queried current capability only. The SDK never
        // supplies another actor's causal useful-attack witness.
    }
    void populate(const sdk::Snapshot& snapshot) {
        auto& in=*input_;in.frame=snapshot.frame;in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        if(in.settings_identity.source!=p6::Source::queried_setting) {
            in.settings=sdk::settings();
            in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
        }
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        in.target_group=context_site_>=0?group_id(routes_.sites[context_site_].group):selected_site_>=0?group_id(routes_.sites[selected_site_].group):0;
        in.threat_regime={int(threats(in.target_group)&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const auto enemy=cambc::global_submitted_gems(cambc::team::enemy);scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.market={};const auto inventory=sdk::own_inventory();
        if(inventory.valid()&&inventory.hash()==snapshot.own.equipment_hash&&fresh(snapshot,12000))
            in.market.inventory={inventory,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        in.market.spawn_shop_legal=snapshot.spawn_shop_legal;in.component_slot=-1;in.supported_slot=-1;
        in.component_entry={};in.supported_entry={};readiness(in.frame,context_site_);
        if(reserve_.declared()){in.supported_slot=reserve_.slot(reserve_.selected());in.supported_entry=reserve_.package().systems[reserve_.selected()].context.key;}
        if(in.offer_count==0){in.offer_count=1;in.independent=0;}
    }
    void configure(const sdk::Snapshot& snapshot,const Solo& own) {
        if(reserve_.declared()||snapshot.draft.picks<5||snapshot.frame.own.actor.pick_ordinal!=0||snapshot.own.bag||ledger_->active())return;
        const auto& site=routes_.sites[own.site];if(site.rally_count<2||!site.rally[0].ready||!site.rally[1].ready)return;
        auto canonical=snapshot.frame;canonical.own.actor.position=site.rally[0].destination;
        auto package=std::make_unique<Package>();package->change=ChangeKind::visible_membership;
        input_->target_group=own.plan.group;input_->threat_regime={int(threats(own.plan.group)&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        for(int which=0;which<2;++which) {
            auto& system=package->systems[which];system.plan=owner_plan(routes_,own.site,own.bank,canonical,snapshot.draft,which?4:3);
            if(!plan_valid(system.plan,snapshot.frame.clock.self))return;
            system.context=declaration(system.plan,snapshot,*input_,site.group.gem_value());
        }
        if(!fresh(snapshot)||!reserve_.declare(*package,snapshot.draft,*ledger_))return;
        context_site_=own.site;context_bank_=own.bank;populate(snapshot);
        std::cout<<"p8-11 DECLARED source="<<own.plan.group<<" old_helper="<<snapshot.draft.actors[3]<<" replacement_helper="<<snapshot.draft.actors[4]<<'\n';
    }
    std::unique_ptr<Solo> own_model(const sdk::Snapshot& snapshot) {
        return best_solo(snapshot.frame);
    }
    std::vector<Solo> alternatives(const sdk::Snapshot& snapshot,const Solo& own) {
        std::vector<Solo> models;models.reserve(3);models.push_back(own);normalize(models.back(),snapshot.frame);
        for(int ordinal:{0,3,4}) {
            const int id=snapshot.draft.actors[ordinal];if(id==snapshot.frame.clock.self)continue;
            const auto* actor=snapshot.frame.visible(id);if(!actor||!actor->alive||actor->bag||actor->channel!=p6::Channel::none)return {};
            std::array<int,3> excluded{};int count=0;for(const auto& model:models)excluded[count++]=model.plan.group;
            auto model=best_solo(subject(snapshot.frame,*actor),-1,-1,{excluded.data(),std::size_t(count)});
            if(!model)return {};normalize(*model,snapshot.frame);models.push_back(std::move(*model));
        }
        for(std::size_t i=0;i<models.size();++i)for(std::size_t j=0;j<i;++j)if(models[i].plan.group==models[j].plan.group)return {};
        return models;
    }
    bool old_helper_observed_away(const sdk::Snapshot& snapshot,int site) const {
        const auto* old=snapshot.frame.visible(snapshot.draft.actors[3]);
        return old&&old->alive&&old->position!=routes_.sites[site].rally[1].destination;
    }
    bool build_menu(const sdk::Snapshot& snapshot,const Solo& own) {
        auto& in=*input_;in.offer_count=1;in.independent=0;in.visible_proposer={};
        if(!independent_offer(in.offers[0],own,{&own,1},snapshot.frame))return false;
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        std::vector<Solo> models;
        if(snapshot.draft.picks>=5&&(ordinal==0||ordinal==3||ordinal==4))models=alternatives(snapshot,own);
        const bool affected=models.size()==3;
        if(!affected){models.push_back(own);normalize(models.back(),snapshot.frame);}
        if(affected&&!independent_offer(in.offers[0],own,models,snapshot.frame))return false;
        if(ordinal==0&&reserve_.declared()) {
            auto retained=std::make_unique<Solo>(retained_solo(routes_,context_site_,context_bank_,snapshot.frame,snapshot.draft));
            if(retained->feasible) {
                bool duplicate=false;for(const auto& model:models)if(model.model.schedule.actors[0].actor_id!=snapshot.frame.clock.self)
                    duplicate|=model.plan.group==retained->plan.group;
                if(!duplicate) {
                    auto alternate=models;for(auto& model:alternate)if(model.model.schedule.actors[0].actor_id==snapshot.frame.clock.self)model=*retained;
                    if(independent_offer(in.offers[in.offer_count],*retained,alternate,snapshot.frame)) {
                        const auto retained_rank=common::evaluate(in,in.offers[in.offer_count].schedule);
                        const auto ordinary_rank=common::evaluate(in,in.offers[0].schedule);
                        // Declared productive tie preference, never an
                        // information bonus or forced invitation detour.
                        if(retained_rank.eligible()&&p6::compare(retained_rank,ordinary_rank)>=0) {
                            auto original=std::make_unique<Offer>(std::move(in.offers[0]));
                            in.offers[0]=std::move(in.offers[in.offer_count]);in.offers[in.offer_count]=std::move(*original);
                        }
                        ++in.offer_count;
                    }
                }
            }
        }
        if(!affected||(ordinal==0&&!reserve_.declared()))return true;
        const auto* collector=snapshot.frame.visible(snapshot.draft.actors[0]);if(!collector)return true;
        for(int which=0;which<2;++which) {
            const int helper=which?4:3;
            if(ordinal!=0&&(ordinal!=helper||helper_attempts_[which]>=2))continue;
            if(ordinal==0&&((!which&&reserve_.retired())||(which&&(!reserve_.retired()||arm==Arm::control))))continue;
            for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>75000;++s) {
                if(ordinal==0&&s!=context_site_)continue;
                if(routes_.sites[s].rally_count<2)continue;
                if(ordinal!=0&&collector->position!=routes_.sites[s].rally[0].destination)continue;
                // A missing old helper is unknown, never an observed departure
                // or evidence about the collector's private retirement.
                if(which&&!old_helper_observed_away(snapshot,s))continue;
                for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>75000;++b) {
                    if(ordinal==0&&b!=context_bank_)continue;
                    bool duplicate=false;for(const auto& model:models)if(model.model.schedule.actors[0].actor_id!=snapshot.draft.actors[0])
                        duplicate|=model.plan.group==group_id(routes_.sites[s].group);
                    if(duplicate)continue;
                    auto candidate=std::make_unique<Coalition>(supported(routes_,s,b,snapshot.frame,snapshot.draft,models,helper));
                    if(!candidate->feasible||in.offer_count==common::max_options)continue;
                    if(ordinal==0) {
                        const auto& system=reserve_.package().systems[which];
                        if(plan_route_hash(candidate->offer.plan)!=system.context.key.route_hash)continue;
                        candidate->offer.context_slot=reserve_.slot(which);candidate->offer.entry=system.context.key;
                    } else in.visible_proposer={candidate->offer.plan.group,snapshot.frame.clock,collector->id,p6::Knowledge::exact};
                    in.offers[in.offer_count++]=std::move(candidate->offer);
                }
            }
        }
        return true;
    }
    void preempt(const sdk::Snapshot& snapshot) {
        if(arm!=Arm::active||preempted_||!reserve_.declared()||!reserve_.retired()||reserve_.used()||!work_.active||
            work_.plan.organization!=Organization::U||ledger_->active()||snapshot.own.bag||snapshot.frame.own.actor.channel!=p6::Channel::none||
            snapshot.frame.clock.self!=snapshot.draft.actors[0]||
            (work_.stage!=PhysicalStage::travel&&work_.stage!=PhysicalStage::clear)||
            snapshot.frame.own.actor.position!=routes_.sites[context_site_].rally[0].destination||!old_helper_observed_away(snapshot,context_site_))return;
        const auto* helper=input_->frame.visible(snapshot.draft.actors[4]);
        if(!helper||!helper->alive||helper->bag||helper->channel!=p6::Channel::none||
            helper->position!=routes_.sites[context_site_].rally[1].destination||!helper->can_attack||
            helper->ready_attack_group!=work_.plan.group||!fresh(snapshot))return;
        preempted_=true;work_.invalidate(PhysicalFailure::unavailable);
        std::cout<<"p8-11 PREEMPT own_unrecorded_U=1 spent_work_retained=1\n";
    }
    void requalify(const sdk::Snapshot& snapshot) {
        if(arm!=Arm::active||!reserve_.declared()||!reserve_.retired()||reserve_.used()||work_.active||ledger_->active()||
            !old_helper_observed_away(snapshot,context_site_))return;
        int option=-1;for(int i=0;i<input_->offer_count;++i)if(input_->offers[i].plan.organization==Organization::H&&input_->offers[i].context_slot==reserve_.slot(1)){option=i;break;}
        if(option<0)return;const auto& candidate=input_->offers[option];
        auto local=adapter_.local(snapshot,candidate.plan,{candidate.safety.data(),std::size_t(candidate.safety_count)},true,calm(snapshot),true,escape(snapshot));
        if(!fresh(snapshot))return;
        if(reserve_.witness_membership(*input_)&&reserve_.authorize(*input_,local,work_,*ledger_,arm))
            std::cout<<"p8-11 REQUALIFICATION actual_helper="<<snapshot.draft.actors[4]<<" old_failure_retained=1\n";
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
    void observed(const sdk::Snapshot&,const LocalFrame& local) {
        const auto result=observe_cycle(*input_,local,reserve_,*ledger_,work_,trace_);
        if(result.accepted)std::cout<<"p8-11 EPISODE positive="<<result.full_positive<<" contradiction="<<result.contradiction<<'\n';
        if(reserve_.retired()&&!retired_logged_){retired_logged_=true;std::cout<<"p8-11 RETIRED actual_private_terminal=1 round="<<reserve_.retired_round()<<'\n';}
        if(work_.plan.organization==Organization::H&&!proof_logged_){proof_logged_=true;
            std::cout<<"p8-11 PROOF_UNAVAILABLE ally_actual_attack_attribution; readiness is not evidence\n";}
    }
public:
    void fault(){work_.request_stop();work_.route_witness_active=false;if(ledger_&&ledger_->active())ledger_->invalidate_active(int(cambc::round_number()));}
    void turn() {
        // Shared melee-reachable geometry is conservative for ranged drafts.
        const int startup_round=int(cambc::round_number());
    const auto startup_room=[&](std::uint64_t reserve) {
        return int(cambc::round_number())==startup_round&&cambc::fuel_remaining()>=reserve;
    };
    if(!routes_.initialized()) {
        if(!startup_room(70000))return;
        if(!diagnostic_routes_started_&&startup_room(32000)) {
            std::printf("p8-11 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                startup_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
            diagnostic_routes_started_=true;
        }
        if(!startup_room(70000))return;
        const bool complete=routes_.initialize_step(1);
        if(complete&&startup_room(32000))
            std::printf("p8-11 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                startup_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
        return;
    }
    if(!work_.active&&startup_room(70000))routes_.prepare();
    if(!startup_room(30000))return;
        const auto* selected=selected_site_<0?nullptr:&routes_.sites[selected_site_].group;
        auto snapshot=adapter_.observe(selected,&work_);if(!snapshot.valid){fault();return;}
        if(!ledger_)ledger_=std::make_unique<EvidenceLedger>(snapshot.frame.clock.self);
        const bool skipped=last_round_>=0&&snapshot.frame.clock.round!=last_round_+1;
        if(snapshot.frame.clock.round==last_round_)return;last_round_=snapshot.frame.clock.round;populate(snapshot);
        if(!fresh(snapshot,12000)){fault();return;}
        if(!skipped)preempt(snapshot);
        if(work_.active) {
            const auto certificates=current_safety(snapshot);bool safe=false;
            for(int c=0;c<initial_safety_count_;++c)if(certificates[c].actor==snapshot.frame.clock.self)
                safe=certificates[c].valid(snapshot.frame,snapshot.frame.clock.self,work_.plan.group);
            safe&=threats(work_.plan.group)==threat_;
            auto local=adapter_.local(snapshot,work_.plan,{certificates.data(),std::size_t(initial_safety_count_)},safe,calm(snapshot),productive(snapshot),escape(snapshot),&work_);
            local.fuel_resumed|=skipped;
            if(skipped||!fresh(snapshot,12000)){fault();return;}
            dispatch(requalification_reserve::advance(*input_,local,reserve_,*ledger_,arm,work_),local);observed(snapshot,local);return;
        }
        if(snapshot.own.bag||snapshot.frame.own.actor.channel!=p6::Channel::none||!snapshot.frame.own.actor.alive){fallback(snapshot);return;}
        auto own=own_model(snapshot);if(!own){fallback(snapshot);return;}
        configure(snapshot,*own);
        if(!build_menu(snapshot,*own)){fallback(snapshot);return;}
        if(!fresh(snapshot)){fault();return;}
        requalification_reserve::refresh(*input_,reserve_,*ledger_);requalify(snapshot);
        if(!fresh(snapshot)){fault();return;}
        auto decision=requalification_reserve::decide(*input_,reserve_,*ledger_,probes_,arm);
        if(decision.option<0){fallback(snapshot);return;}
        const auto planned=snapshot;const int action_group=input_->offers[decision.option].plan.group;int site=-1;
        for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)site=s;
        if(site<0)return;snapshot=adapter_.observe(&routes_.sites[site].group,&work_);
        if(!unchanged(planned,snapshot)){fault();return;}populate(snapshot);
        for(int o=0;o<input_->offer_count;++o)for(int c=0;c<input_->offers[o].safety_count;++c)input_->offers[o].safety[c].clock=snapshot.frame.clock;
        if(input_->visible_proposer.knowledge==p6::Knowledge::exact)input_->visible_proposer.clock=snapshot.frame.clock;
        if(!fresh(snapshot)){fault();return;}
        decision=requalification_reserve::decide(*input_,reserve_,*ledger_,probes_,arm);
        if(decision.option<0||input_->offers[decision.option].plan.group!=action_group)return;
        auto& chosen=input_->offers[decision.option];
        auto local=adapter_.local(snapshot,chosen.plan,{chosen.safety.data(),std::size_t(chosen.safety_count)},true,calm(snapshot),true,escape(snapshot));
        if(!fresh(snapshot)){fault();return;}
        const auto admitted=requalification_reserve::commit(*input_,local,decision,reserve_,*ledger_,probes_,arm,work_,trace_);
        if(!admitted.started){fallback(snapshot);return;}
        selected_site_=site;selected_bank_=own->bank;renewal_site_=chosen.plan.organization==Organization::H&&chosen.plan.collector!=snapshot.frame.clock.self?own->site:site;
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;if(chosen.plan.organization==Organization::H&&ordinal>=3&&ordinal<=4)++helper_attempts_[ordinal-3];
        initial_safety_=chosen.safety;initial_safety_count_=chosen.safety_count;threat_=threats(chosen.plan.group);
        std::cout<<"p8-11 ADMIT group="<<chosen.plan.group<<" organization="<<int(chosen.plan.organization)<<" episode="<<admitted.episode<<" used="<<reserve_.used()<<'\n';
        unavailable_logged_=false;inspection_turns_=0;inspection_site_=-1;dispatch(work_.step(local),local);observed(snapshot,local);
    }
};
inline void draft() {
    const auto own=cambc::draft::current_draft(cambc::team::ally);const int ordinal=int(own.size()),picks=int(cambc::draft::team_size());if(ordinal>=picks)return;
    common::DraftInputs in;in.ordinal=ordinal;in.picks=picks;in.prospective_actor=0;
    for(int i=0;i<ordinal&&i<5;++i)in.own_picks[i]=sdk::type(own[i]);in.settings=sdk::settings();in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
    const int window=std::min(common::forecast_window,in.settings.max_rounds);in.windows.deadlines=in.windows.windows={window,window,window};
    in.windows.sampled_actor_id=0;in.windows.sampled_round=0;in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto candidates=p8::native::draft_candidates(0,ordinal,true);const auto selected=requalification_reserve::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-11 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
    for(const auto type:draft_priority(ordinal)){const auto pick=sdk::draft_type(type);if(std::find(own.begin(),own.end(),pick)==own.end()){cambc::draft::pick(pick);return;}}
}
inline int run() {
    std::unique_ptr<Runtime> bot;while(true) {
        try {
            if(cambc::is_draft())native::draft();else {
                if(!bot) {
                    const int construction_round=int(cambc::round_number());
                    bot.reset(new Runtime);
                    if(int(cambc::round_number())==construction_round&&cambc::fuel_remaining()>=32000)
                        std::printf("p8-11 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                            construction_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                bot->turn();
            }
        }
        catch(const cambc::error& e){std::cout<<"p8-11 ERROR engine "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& e){std::cout<<"p8-11 ERROR exception "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-11 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::requalification_reserve::native
