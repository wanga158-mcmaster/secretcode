#pragma once
#include "configuration.h"
#include "../../../common/src/defaults.h"
#include "../draft.h"
#include <bit>
#include <cstdio>
#include <iostream>
#include <memory>
namespace p8::rollback_crew::native {
using namespace p8::native;
// Own ledgers and receipts only. Public duties do not certify another actor's
// incumbency; actual incumbent adoption must pass its own Crew policy.
class Runtime {
    Routes routes_;sdk::Adapter adapter_;Duties duties_;Crew crew_;ProbeBudget probes_;CycleTrace trace_;Work work_;
    std::unique_ptr<Inputs> input_=p8::native::make_default<Inputs>();std::unique_ptr<EvidenceLedger> ledger_;p6::ScoreHistory scores_;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};int initial_safety_count_=0;
    int selected_site_=-1,selected_bank_=-1,renewal_site_=-1,last_round_=-1;
    int component_=-1,joint_=-1,joint_site_=-1,joint_bank_=-1;bool contexts_=false;
    bool preempted_=false,control_replacement_=false;int candidate_renewal_=-1;
    std::array<int,2> observed_returns_{};std::array<bool,2> observed_away_{};
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
    std::uint64_t context_regime() const {
        std::uint64_t hash=1469598103934665603ULL;
        for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake()) {
            bool retained=false;if(duties_.valid)for(int ordinal=0;ordinal<3;++ordinal)
                retained|=group_id(camp.camp_group())==group_id(routes_.sites[duties_.site[ordinal]].group);
            // Expected wakes in the public duty set do not rename a context.
            // Current route/ambient certificates still cover their real damage.
            if(!retained){hash=hash_word(hash,std::uint64_t(sdk::identity(camp))+1);hash=hash_word(hash,std::bit_cast<std::uint64_t>(double(camp.strength())));}
        }
        for(const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
            hash=hash_word(hash,std::uint64_t(sdk::identity(enemy))+1);const auto at=enemy.position();
            hash=hash_word(hash,std::uint32_t(at.x));hash=hash_word(hash,std::uint32_t(at.y));
            hash=hash_word(hash,std::bit_cast<std::uint64_t>(double(enemy.strength())));
            hash=hash_word(hash,std::bit_cast<std::uint64_t>(double(enemy.focus())));
        }
        return hash;
    }
    void populate(const sdk::Snapshot& snapshot) {
        auto& in=*input_;in.frame=snapshot.frame;in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        if(in.settings_identity.source!=p6::Source::queried_setting) {
            in.settings=sdk::settings();
            in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
        }
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        in.target_group=duties_.valid&&ordinal<3?group_id(routes_.sites[duties_.site[ordinal]].group):
            selected_site_<0?0:group_id(routes_.sites[selected_site_].group);
        in.threat_regime={int(context_regime()&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const auto enemy=cambc::global_submitted_gems(cambc::team::enemy);
        scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.component_slot=component_;in.supported_slot=joint_;in.component_entry=in.supported_entry={};
        if(const auto* slot=ledger_->state(component_))in.component_entry=slot->declaration.key;
        if(const auto* slot=ledger_->state(joint_))in.supported_entry=slot->declaration.key;
        if(in.offer_count==0){in.offer_count=1;in.independent=0;}
    }
    void assign() {
        if(duties_.valid||routes_.sites.size()<3||routes_.banks.empty()||!routes_.spawn.ready)return;
        std::vector<int> candidates;
        for(int s=0;s<int(routes_.sites.size());++s) {
            if(!routes_.sites[s].approach.ready||!routes_.sites[s].capture.ready)return;
            if(routes_.sites[s].group.gem_value()>0)candidates.push_back(s);
        }
        for(const auto& bank:routes_.banks)if(!bank.approach.ready)return;
        if(candidates.size()<3)return;
        std::sort(candidates.begin(),candidates.end(),[&](int a,int b) {
            const auto da=distance(routes_.spawn.destination,routes_.sites[a].approach.destination);
            const auto db=distance(routes_.spawn.destination,routes_.sites[b].approach.destination);
            return da!=db?da<db:group_id(routes_.sites[a].group)<group_id(routes_.sites[b].group);
        });
        auto proposed=duties_;
        for(int ordinal=0;ordinal<3;++ordinal) {
            proposed.site[ordinal]=candidates[ordinal];int best=100000;
            for(int b=0;b<int(routes_.banks.size());++b) {
                const auto bank=routes_.route(routes_.sites[candidates[ordinal]].capture.destination,routes_.banks[b].approach);
                const auto home=routes_.route(routes_.banks[b].approach.destination,routes_.spawn);
                if(route_valid(bank)&&route_valid(home)&&bank.count+home.count<best){best=bank.count+home.count;proposed.bank[ordinal]=b;}
            }
            if(proposed.bank[ordinal]<0)return;
        }
        proposed.valid=true;duties_=proposed;
        std::cout<<"p8-09 DUTIES "<<group_id(routes_.sites[duties_.site[0]].group)<<' '
            <<group_id(routes_.sites[duties_.site[1]].group)<<' '<<group_id(routes_.sites[duties_.site[2]].group)<<'\n';
    }
    Plan fixed_u(int ordinal,int actor) const {
        const auto& site=routes_.sites[duties_.site[ordinal]];const auto& bank=routes_.banks[duties_.bank[ordinal]];
        Plan p;p.group=group_id(site.group);p.collector=actor;p.altar=sdk::altar_id(bank.altar);p.crew[0]=actor;p.crew_count=1;
        p.ingress=stay(site.approach.destination);p.capture=routes_.route(site.approach.destination,site.capture);
        p.bank=routes_.route(site.capture.destination,bank.approach);
        p.renewal=Routes::join(routes_.route(bank.approach.destination,routes_.spawn),routes_.route(routes_.spawn.destination,site.approach));
        p.forecast_rounds=120;p.remaining_rounds=std::min(120,int(cambc::constants::max_rounds)-int(cambc::round_number()));
        p.capture_window=cambc::constants::capture_time;p.gem_bearing=p.complete_verified=p.rally_legal=true;return p;
    }
    Plan fixed_joint(const sdk::Snapshot& snapshot,int site,int bank,int ordinal) const {
        auto frame=snapshot.frame;frame.clock.self=snapshot.draft.actors[ordinal];frame.own.actor.id=frame.clock.self;
        frame.own.actor.type=snapshot.draft.champions[ordinal];frame.own.actor.pick_ordinal=ordinal;
        frame.own.actor.position=routes_.sites[duties_.site[ordinal]].approach.destination;
        auto plan=formation_plan(routes_,site,bank,frame,geometry_roster(snapshot.draft),Organization::J,true);
        plan.renewal=Routes::join(routes_.route(routes_.banks[bank].approach.destination,routes_.spawn),
            routes_.route(routes_.spawn.destination,routes_.sites[duties_.site[ordinal]].approach));
        return plan;
    }
    void declare(const sdk::Snapshot& snapshot) {
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        if(contexts_||!duties_.valid||ordinal>2||snapshot.draft.picks<3||snapshot.own.bag||ledger_->active())return;
        for(int i=0;i<2;++i) {
            const auto& site=routes_.sites[duties_.site[i]];
            for(int pad=0;pad<site.rally_count;++pad)if(!site.rally[pad].ready)return;
            for(int pad=0;pad<site.joint_capture_count;++pad)if(!site.joint_capture[pad].ready)return;
        }
        if(ordinal<2) {
            const auto plan=fixed_u(ordinal,snapshot.frame.clock.self);if(!plan_valid(plan,snapshot.frame.clock.self))return;
            component_=ledger_->predeclare(declaration(plan,snapshot,*input_,false,routes_.sites[duties_.site[ordinal]].group.gem_value()));
        }
        for(int target=0;target<2;++target) {
            const int s=duties_.site[target],b=duties_.bank[target];bool common_geometry=true;
            for(int role=0;role<3;++role) {
                const auto peer=fixed_joint(snapshot,s,b,role);
                common_geometry&=plan_valid(peer,snapshot.draft.actors[role])&&walking(peer.ingress)<=8;
            }
            if(!common_geometry)continue;
            auto plan=std::make_unique<Plan>(fixed_joint(snapshot,s,b,ordinal));
            joint_=ledger_->predeclare(declaration(*plan,snapshot,*input_,true,routes_.sites[s].group.gem_value()));
            if(joint_>=0){joint_site_=s;joint_bank_=b;break;}
        }
        contexts_=true;populate(snapshot);
    }
    void observe_setup(const sdk::Snapshot& snapshot) {
        if(!duties_.valid||snapshot.frame.own.actor.pick_ordinal!=2)return;
        for(int ordinal=0;ordinal<2;++ordinal) {
            const auto* actor=snapshot.frame.visible(snapshot.draft.actors[ordinal]);if(!actor||!actor->alive)continue;
            const bool ready=actor->position==routes_.sites[duties_.site[ordinal]].approach.destination&&actor->bag==0&&actor->channel==p6::Channel::none;
            if(!ready&&actor->spawn_group==group_id(routes_.sites[duties_.site[ordinal]].group))observed_away_[ordinal]=true;
            else if(ready&&observed_away_[ordinal]){observed_returns_[ordinal]=std::min(3,observed_returns_[ordinal]+1);observed_away_[ordinal]=false;}
        }
        // Public setup history only. These observations never certify income,
        // a teammate ledger, or the incumbent's own two-cycle qualification.
    }
    std::unique_ptr<Solo> own_model(const sdk::Snapshot& snapshot) {
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        if(duties_.valid&&ordinal<3&&(arm==Arm::active||!control_replacement_)) {
            auto model=best_solo(snapshot.frame,duties_.site[ordinal],duties_.bank[ordinal]);if(model)return model;
            std::array<int,2> excluded{};int count=0;
            for(int other=0;other<3;++other)if(other!=ordinal)excluded[count++]=group_id(routes_.sites[duties_.site[other]].group);
            return best_solo(snapshot.frame,-1,-1,{excluded.data(),std::size_t(count)});
        }
        return best_solo(snapshot.frame);
    }
    std::vector<Solo> alternatives(const sdk::Snapshot& snapshot,const Solo& own) {
        std::vector<Solo> models;models.reserve(3);models.push_back(own);normalize(models.back(),snapshot.frame);
        for(int ordinal=0;ordinal<3;++ordinal) {
            const int id=snapshot.draft.actors[ordinal];if(id==snapshot.frame.clock.self)continue;
            const auto* actor=snapshot.frame.visible(id);if(!actor||!actor->alive||actor->bag||actor->channel!=p6::Channel::none)return {};
            std::unique_ptr<Solo> model;
            if(arm==Arm::active)model=best_solo(subject(snapshot.frame,*actor),duties_.site[ordinal],duties_.bank[ordinal]);
            else {
                std::array<int,3> excluded{};int count=0;for(const auto& prior:models)excluded[count++]=prior.plan.group;
                model=best_solo(subject(snapshot.frame,*actor),-1,-1,{excluded.data(),std::size_t(count)});
            }
            if(!model)return {};normalize(*model,snapshot.frame);models.push_back(std::move(*model));
        }
        for(std::size_t i=0;i<models.size();++i)for(std::size_t j=0;j<i;++j)if(models[i].plan.group==models[j].plan.group)return {};
        return models;
    }
    bool menu(const sdk::Snapshot& snapshot,const Solo& own) {
        auto& in=*input_;in.offer_count=1;in.independent=0;in.visible_proposer={};
        candidate_renewal_=own.site;
        if(!independent_offer(in.offers[0],own,{&own,1},snapshot.frame))return false;
        if(incumbent_actor(snapshot.frame.own.actor,snapshot.draft)){in.offers[0].context_slot=component_;in.offers[0].entry=in.component_entry;}
        if(arm==Arm::active&&incumbent_actor(snapshot.frame.own.actor,snapshot.draft)) {
            if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000)return true;
            crew_.remember(in,*ledger_);
        }
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        if(!duties_.valid||snapshot.draft.picks<3||ordinal>2||(arm==Arm::active&&(crew_.retired()||crew_.attempts())))return true;
        if(arm==Arm::active&&ordinal<2&&!crew_.saved().present)return true;
        const auto* proposer=snapshot.frame.visible(snapshot.draft.actors[2]);if(!proposer)return true;
        if(ordinal==2&&(arm==Arm::active||!control_replacement_)) {
            if(observed_returns_[0]<3||observed_returns_[1]<3)return true;
            for(int incumbent=0;incumbent<2;++incumbent) {
                const auto* actor=snapshot.frame.visible(snapshot.draft.actors[incumbent]);
                if(!actor||actor->position!=routes_.sites[duties_.site[incumbent]].approach.destination||actor->bag||actor->channel!=p6::Channel::none)return true;
            }
        }
        auto models=alternatives(snapshot,own);if(models.size()!=3)return true;
        auto destinations=duties_;
        if(arm==Arm::control)for(const auto& model:models) {
            const auto* actor=snapshot.frame.visible(model.model.schedule.actors[0].actor_id);
            if(actor&&actor->pick_ordinal>=0&&actor->pick_ordinal<3) {
                destinations.site[actor->pick_ordinal]=model.site;destinations.bank[actor->pick_ordinal]=model.bank;
            }
        }
        if(!independent_offer(in.offers[0],own,models,snapshot.frame))return false;
        if(ordinal<2){in.offers[0].context_slot=component_;in.offers[0].entry=in.component_entry;}
        std::unique_ptr<Coalition> best;p6::ScoreEvaluation rank;int chosen=-1;
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>90000;++s) {
            if(arm==Arm::active&&s!=joint_site_)continue;
            if(ordinal!=2&&(routes_.sites[s].rally_count<3||proposer->position!=routes_.sites[s].rally[0].destination))continue;
            for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>90000;++b) {
                if(arm==Arm::active&&b!=joint_bank_)continue;
                auto candidate=std::make_unique<Coalition>(replacement(routes_,destinations,s,b,snapshot.frame,snapshot.draft,models));
                if(!candidate->feasible)continue;const auto score=common::evaluate(in,candidate->offer.schedule);
                if(score.eligible()&&(!best||p6::compare(score,rank)>0)){best=std::move(candidate);rank=score;chosen=s;}
            }
        }
        if(best) {
            in.offers[1]=best->offer;in.offer_count=2;
            candidate_renewal_=destinations.site[ordinal];
            if(arm==Arm::active){in.offers[1].context_slot=joint_;in.offers[1].entry=in.supported_entry;}
            if(ordinal!=2)in.visible_proposer={group_id(routes_.sites[chosen].group),snapshot.frame.clock,proposer->id,p6::Knowledge::exact};
        }
        return true;
    }
    bool preempt(const sdk::Snapshot& snapshot) {
        if(arm!=Arm::active||preempted_||!work_.active||work_.plan.organization!=Organization::U||ledger_->active()||
            !incumbent_actor(snapshot.frame.own.actor,snapshot.draft)||!crew_.saved().present||crew_.attempts()||crew_.retired()||
            snapshot.own.bag||snapshot.frame.own.actor.channel!=p6::Channel::none||
            (work_.stage!=PhysicalStage::travel&&work_.stage!=PhysicalStage::clear)||
            snapshot.frame.own.actor.position!=crew_.saved().plan.ingress.tiles[0]||joint_site_<0||
            !common::slot_current(*input_,*ledger_,crew_.saved().context,crew_.saved().entry)||
            !ledger_->component_qualified(crew_.saved().context,crew_.saved().entry,snapshot.frame.clock.round))return false;
        const auto* proposer=snapshot.frame.visible(snapshot.draft.actors[2]);
        if(!proposer||!proposer->alive||proposer->channel!=p6::Channel::none||routes_.sites[joint_site_].rally_count<3||
            proposer->position!=routes_.sites[joint_site_].rally[0].destination)return false;
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000)return false;
        // One narrow own-generation handoff. Past attacks/time stay spent; the
        // new comparison uses current damaged/awake camps, not the old reward.
        preempted_=true;work_.invalidate(PhysicalFailure::unavailable);
        std::cout<<"p8-09 PREEMPT own_unrecorded_U=1\n";return true;
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
        const auto result=trace_.finish(*ledger_,snapshot.frame.clock.round,terminal);crew_.observe_result(result,work_);
        if(result.accepted)std::cout<<"p8-09 EPISODE positive="<<result.full_positive<<" retired="<<crew_.retired()<<'\n';
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
                std::printf("p8-09 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),entered,
                    static_cast<unsigned long long>(cambc::fuel_remaining()));
            }
            if(!room(70000))return;
            const bool complete=routes_.initialize_step(1);
            if(complete&&room(32000))
                std::printf("p8-09 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),entered,
                    static_cast<unsigned long long>(cambc::fuel_remaining()));
            return;
        }
        if(!work_.active&&room(70000))routes_.prepare();
        if(int(cambc::round_number())!=entered)return;
        assign();
        int observe_site=selected_site_;
        if(!work_.active&&arm==Arm::active&&crew_.phase()==Phase::restore&&crew_.saved().present)
            for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==crew_.saved().plan.group)observe_site=s;
        auto snapshot=adapter_.observe(observe_site<0?nullptr:&routes_.sites[observe_site].group,&work_);
        if(!snapshot.valid){fault();return;}if(!ledger_)ledger_=std::make_unique<EvidenceLedger>(snapshot.frame.clock.self);
        const bool skipped=last_round_>=0&&snapshot.frame.clock.round!=last_round_+1;
        if(snapshot.frame.clock.round==last_round_)return;
        last_round_=snapshot.frame.clock.round;populate(snapshot);observe_setup(snapshot);
        if(!skipped)preempt(snapshot);
        if(work_.active) {
            const auto certificates=current_safety(snapshot);bool safe=false;
            for(int i=0;i<initial_safety_count_;++i)if(certificates[i].actor==snapshot.frame.clock.self)
                safe=certificates[i].valid(snapshot.frame,snapshot.frame.clock.self,work_.plan.group);
            safe&=threats(work_.plan.group)==threat_;
            auto local=adapter_.local(snapshot,work_.plan,{certificates.data(),std::size_t(initial_safety_count_)},safe,calm(snapshot),productive(snapshot),escape(snapshot),&work_);
            local.fuel_resumed|=skipped;
            if(skipped||int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<12000){fault();return;}
            dispatch(crew_.advance(*input_,local,*ledger_,arm,work_),local);observe_trace(snapshot,local);return;
        }
        if(snapshot.frame.own.actor.channel!=p6::Channel::none||!snapshot.frame.own.actor.alive||snapshot.own.bag>0){fallback(snapshot);return;}
        declare(snapshot);auto own=own_model(snapshot);if(!own){fallback(snapshot);return;}
        if(arm==Arm::active&&crew_.phase()==Phase::restore) {
            const auto& saved=crew_.saved();
            if(!saved.present||own->plan.group!=saved.plan.group||plan_route_hash(own->plan)!=plan_route_hash(saved.plan))own->plan.organization=Organization::G;
        }
        if(!menu(snapshot,*own)){fallback(snapshot);return;}
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000){fault();return;}
        auto decision=crew_.decide(*input_,*ledger_,probes_,arm);const bool raw=decision.option<0;
        if(raw) {
            // Unrepresented bootstrap/restoration is current own production,
            // never an exact saved-role receipt or a replacement invitation.
            input_->offers[0].plan.organization=crew_.phase()==Phase::restore?Organization::G:Organization::U;
            decision.option=0;decision.clock=snapshot.frame.clock;decision.organization=input_->offers[0].plan.organization;
        }
        const auto planned=snapshot;const int action_group=input_->offers[decision.option].plan.group;
        int site=-1;for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)site=s;
        if(site<0)return;snapshot=adapter_.observe(&routes_.sites[site].group,&work_);
        if(!unchanged(planned,snapshot)){fault();return;}populate(snapshot);
        for(int o=0;o<input_->offer_count;++o)for(int c=0;c<input_->offers[o].safety_count;++c)input_->offers[o].safety[c].clock=snapshot.frame.clock;
        if(input_->visible_proposer.knowledge==p6::Knowledge::exact)input_->visible_proposer.clock=snapshot.frame.clock;
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000){fault();return;}
        if(!raw)decision=crew_.decide(*input_,*ledger_,probes_,arm);else decision.clock=snapshot.frame.clock;
        if(decision.option<0||input_->offers[decision.option].plan.group!=action_group)return;
        auto& chosen=input_->offers[decision.option];auto local=adapter_.local(snapshot,chosen.plan,{chosen.safety.data(),std::size_t(chosen.safety_count)},true,calm(snapshot),true,escape(snapshot));
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000){fault();return;}
        Admission admitted;if(raw)admitted.started=work_.start(chosen.plan,local);else admitted=crew_.commit(*input_,local,decision,*ledger_,probes_,arm,work_);
        if(!admitted.started){fallback(snapshot);return;}
        selected_site_=site;selected_bank_=own->bank;
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        renewal_site_=chosen.plan.organization==Organization::J&&duties_.valid&&ordinal<3?candidate_renewal_:site;
        if(arm==Arm::control&&chosen.plan.organization==Organization::J)control_replacement_=true;
        initial_safety_=chosen.safety;initial_safety_count_=chosen.safety_count;threat_=threats(chosen.plan.group);
        if(admitted.episode&&!trace_.begin(*ledger_,work_)){ledger_->invalidate_active(snapshot.frame.clock.round);work_.request_stop();}
        std::cout<<"p8-09 ADMIT group="<<chosen.plan.group<<" organization="<<int(chosen.plan.organization)<<" episode="<<admitted.episode<<" attempts="<<crew_.attempts()<<'\n';
        inspection_turns_=0;inspection_site_=-1;unavailable_logged_=false;dispatch(work_.step(local),local);observe_trace(snapshot,local);
    }
};
inline void draft() {
    const auto own=cambc::draft::current_draft(cambc::team::ally);const int ordinal=int(own.size()),picks=int(cambc::draft::team_size());if(ordinal>=picks)return;
    common::DraftInputs in;in.ordinal=ordinal;in.picks=picks;in.prospective_actor=0;
    for(int i=0;i<ordinal&&i<5;++i)in.own_picks[i]=sdk::type(own[i]);in.settings=sdk::settings();in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
    const int window=std::min(common::forecast_window,in.settings.max_rounds);in.windows.deadlines=in.windows.windows={window,window,window};
    in.windows.sampled_actor_id=0;in.windows.sampled_round=0;in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto candidates=p8::native::draft_candidates(0,ordinal,true);const auto selected=rollback_crew::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-09 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
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
                        std::printf("p8-09 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),entered,
                            static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                if(int(cambc::round_number())==entered)bot->turn();
            }
        }
        catch(const cambc::error& e){std::cout<<"p8-09 ERROR engine "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& e){std::cout<<"p8-09 ERROR exception "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-09 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::rollback_crew::native

