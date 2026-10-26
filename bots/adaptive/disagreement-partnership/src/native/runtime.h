#pragma once
#include "configuration.h"
#include "../draft.h"
#include <bit>
#include <cstdio>
#include <iostream>
#include <memory>
namespace p8::disagreement_partnership::native {
using namespace p8::native;
// One productive own route observation; no peer ledger or attack inference.
// Public path distance stays fixed in both residual-delay predictors.
class Runtime {
    bool diagnostic_routes_started_=false;
    Routes routes_;sdk::Adapter adapter_;Partnership partnership_;ProbeBudget probes_;CycleTrace trace_;Work work_;
    std::unique_ptr<Inputs> input_=p8::native::make_default<Inputs>();std::unique_ptr<EvidenceLedger> ledger_;p6::ScoreHistory scores_;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};int initial_safety_count_=0;
    int selected_site_=-1,selected_bank_=-1,renewal_site_=-1,last_round_=-1;
    int context_site_=-1,context_bank_=-1;Corridor corridor_{};bool result_logged_=false,proof_logged_=false;
    unsigned helper_attempts_=0;

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
    bool fresh(const sdk::Snapshot& snapshot,std::uint64_t reserve=30000) const {
        return snapshot.valid&&int(cambc::round_number())==snapshot.frame.clock.round&&cambc::fuel_remaining()>=reserve;
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
        in.component_slot=-1;in.component_entry={};in.supported_slot=partnership_.slot();in.supported_entry={};
        if(partnership_.declared())in.supported_entry=partnership_.spec().organization.key;
        if(in.offer_count==0){in.offer_count=1;in.independent=0;}
    }
    std::vector<Solo> alternatives(const sdk::Snapshot& snapshot,const Solo& own) {
        std::vector<Solo> models;models.reserve(2);models.push_back(own);normalize(models.back(),snapshot.frame);
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;const int partner=ordinal==2?0:2;
        const auto* actor=snapshot.frame.visible(snapshot.draft.actors[partner]);
        if(!actor||!actor->alive||actor->bag||actor->channel!=p6::Channel::none)return {};
        const int excluded=own.plan.group;
        auto next=best_solo(subject(snapshot.frame,*actor),-1,-1,{&excluded,1});if(!next)return {};
        // This subset has no known inter-member bank queue. Current outsider
        // approach occupancy is also rejected by queue_alternatives/coalition.
        if(routes_.banks[own.bank].approach.destination==routes_.banks[next->bank].approach.destination)return {};
        normalize(*next,snapshot.frame);models.push_back(std::move(*next));return models;
    }
    std::unique_ptr<Offer> future_u(const sdk::Snapshot& snapshot,std::span<const Solo> first) {
        std::vector<Solo> models;models.reserve(first.size());
        for(const auto& original:first) {
            const int id=original.model.schedule.actors[0].actor_id;const auto* actor=snapshot.frame.visible(id);if(!actor)return {};
            auto projection=subject(snapshot.frame,*actor);projection.own.actor.position=original.plan.renewal.tiles[original.plan.renewal.count-1];
            projection.own.actor.bag=0;projection.own.gold=0;
            // Projection only, never an SDK observation or evidence receipt.
            // Public full HP camp work and current kit; healing is not credited.
            auto model=std::make_unique<Solo>(solo(routes_,original.site,original.bank,projection,true,true));if(!model->feasible)return {};
            model->ambient_incoming=original.ambient_incoming;
            for(auto* exposure:{&model->model.certificate.clear,&model->model.certificate.hold,&model->model.certificate.bank,&model->model.certificate.recovery})
                exposure->incoming_total+=original.ambient_incoming*model->plan.forecast_rounds;
            normalize(*model,snapshot.frame);models.push_back(std::move(*model));
        }
        const Solo* own=nullptr;for(const auto& model:models)if(model.model.schedule.actors[0].actor_id==snapshot.frame.clock.self)own=&model;
        if(!own)return {};auto out=std::make_unique<Offer>();
        if(!independent_offer(*out,*own,models,snapshot.frame))return {};return out;
    }
    bool same_corridor(const Corridor& current) const {
        return current.valid&&current.actor==corridor_.actor&&current.index==corridor_.index&&current.tile==corridor_.tile&&current.forced==corridor_.forced;
    }
    bool owner_models(const sdk::Snapshot& snapshot,const Solo& own,Offer& ordinary,Offer& assay,Offer& supported,Corridor& traffic) {
        if(!investigator(snapshot.frame.own.actor,snapshot.draft)||own.plan.ingress.count!=1||!full_work(routes_.sites[own.site]))return false;
        auto models=alternatives(snapshot,own);if(models.size()!=2)return false;
        for(const auto& model:models)if(model.plan.ingress.count!=1||!full_work(routes_.sites[model.site]))return false;
        for(std::size_t a=0;a<models.size();++a)for(std::size_t b=0;b<models.size();++b)if(a!=b)
            for(int t=0;t<models[a].plan.bank.count;++t)
                if(models[a].plan.bank.tiles[t]==models[b].plan.ingress.tiles[models[b].plan.ingress.count-1])return false;
        auto joint=std::make_unique<Coalition>(coalition(routes_,own.site,own.bank,snapshot.frame,geometry_roster(snapshot.draft),models,Organization::H,true));
        if(!joint->feasible)return false;
        auto retained=std::make_unique<Solo>(retained_solo(routes_,own,joint->offer.plan,snapshot.frame));if(!retained->feasible)return false;
        auto retained_models=models;for(auto& model:retained_models)if(model.model.schedule.actors[0].actor_id==snapshot.frame.clock.self)model=*retained;
        auto u=std::make_unique<Offer>(),raw=std::make_unique<Offer>();
        if(!independent_offer(*u,*retained,retained_models,snapshot.frame)||!independent_offer(*raw,own,models,snapshot.frame))return false;
        traffic=corridor(u->plan,u->schedule,snapshot.frame.clock.self,snapshot.draft.actors[0]);if(!traffic.valid)return false;
        const auto h_traffic=corridor(joint->offer.plan,joint->offer.schedule,snapshot.frame.clock.self,snapshot.draft.actors[0]);
        const auto plain_traffic=corridor(raw->plan,raw->schedule,snapshot.frame.clock.self,snapshot.draft.actors[0]);
        if(!h_traffic.valid||!plain_traffic.valid||h_traffic.actor!=traffic.actor||plain_traffic.actor!=traffic.actor||
            traffic.forced||h_traffic.forced||plain_traffic.forced)return false;
        auto next_u=future_u(snapshot,models);if(!next_u)return false;
        return recurrence(ordinary,*raw,*next_u,models,routes_,snapshot.frame,snapshot.frame.clock.self,false)&&
            recurrence(assay,*u,*u,retained_models,routes_,snapshot.frame,snapshot.frame.clock.self,false)&&
            recurrence(supported,joint->offer,joint->offer,models,routes_,snapshot.frame,snapshot.frame.clock.self,true);
    }
    bool build_menu(const sdk::Snapshot& snapshot,const Solo& own) {
        auto& in=*input_;in.offer_count=1;in.independent=0;in.visible_proposer={};in.models={};
        if(!independent_offer(in.offers[0],own,{&own,1},snapshot.frame))return false;
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        if(snapshot.draft.picks<3||(ordinal!=0&&ordinal!=2))return true;
        if(ordinal==0) {
            if(helper_attempts_>=2)return true;auto models=alternatives(snapshot,own);if(models.size()!=2)return true;
            if(!independent_offer(in.offers[0],own,models,snapshot.frame))return false;
            const auto* collector=snapshot.frame.visible(snapshot.draft.actors[2]);if(!collector)return true;
            p6::ScoreEvaluation rank;std::unique_ptr<Coalition> best;
            for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>80000;++s) {
                if(routes_.sites[s].rally_count<2||collector->position!=routes_.sites[s].rally[0].destination)continue;
                for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>80000;++b) {
                    auto candidate=std::make_unique<Coalition>(coalition(routes_,s,b,snapshot.frame,geometry_roster(snapshot.draft),models,Organization::H,true));
                    if(!candidate->feasible)continue;const auto score=common::evaluate(in,candidate->offer.schedule);
                    if(score.eligible()&&(!best||p6::compare(score,rank)>0)){best=std::move(candidate);rank=score;}
                }
            }
            if(best){in.offers[1]=best->offer;in.offer_count=2;in.visible_proposer={best->offer.plan.group,snapshot.frame.clock,collector->id,p6::Knowledge::exact};}
            return true;
        }
        std::unique_ptr<Solo> fixed;
        if(partnership_.declared())fixed=best_solo(snapshot.frame,context_site_,context_bank_);
        const auto& candidate=fixed?*fixed:own;
        if(partnership_.declared()&&!fixed)return true;
        auto ordinary=std::make_unique<Offer>(),assay=std::make_unique<Offer>(),h=std::make_unique<Offer>();Corridor traffic;
        if(!owner_models(snapshot,candidate,*ordinary,*assay,*h,traffic))return true;
        // Also retain the current best ordinary source. Its complete future
        // recurrence is public full-HP work from its actual productive return.
        if(own.plan.group!=candidate.plan.group||own.bank!=candidate.bank) {
            auto models=alternatives(snapshot,own);if(models.size()!=2)return true;
            auto first=std::make_unique<Offer>();auto next=future_u(snapshot,models);
            if(!next||!independent_offer(*first,own,models,snapshot.frame)||
                !recurrence(*ordinary,*first,*next,models,routes_,snapshot.frame,snapshot.frame.clock.self,false))return true;
        }
        in.offers[0]=*ordinary;in.offers[1]=*assay;in.offers[2]=*h;in.offer_count=3;
        in.models[0]={assay->schedule,h->schedule};
        if(!delayed(in.models[1].independent,in.models[0].independent,snapshot.frame.clock.self)||
            !delayed(in.models[1].supported,in.models[0].supported,snapshot.frame.clock.self)) {in.offer_count=1;return true;}
        if(!partnership_.declared()) {
            if(!disagreement(in)||candidate.plan.ingress.count!=1)return true;
            auto spec=std::make_unique<Spec>();spec->assay=assay->plan;spec->supported=h->plan;
            spec->nominal_stage=assay->plan.bank.count-1+traffic.forced;spec->nominal_model=0;spec->paid_bank_turn_bound=0;
            in.target_group=assay->plan.group;in.threat_regime={int(threats(in.target_group)&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
            const auto payload=routes_.sites[candidate.site].group.gem_value();
            spec->route=declaration(spec->assay,snapshot,in,false,payload);spec->route.hypothesis_count=2;
            spec->route.hypotheses[0]=spec->models[0];spec->route.hypotheses[1]=spec->models[1];
            spec->organization=declaration(spec->supported,snapshot,in,true,payload);
            if(!fresh(snapshot)||!partnership_.declare(*spec,snapshot.draft,*ledger_))return true;
            context_site_=candidate.site;context_bank_=candidate.bank;corridor_=traffic;populate(snapshot);
            std::cout<<"p8-13 DECLARED blocker="<<traffic.actor<<" bank_edges="<<spec->assay.bank.count-1<<" known_wait="<<traffic.forced<<" residual_models=0,2 paid=0\n";
        }
        if(!same_corridor(traffic)||plan_route_hash(assay->plan)!=partnership_.spec().route.key.route_hash||
            plan_route_hash(h->plan)!=partnership_.spec().organization.key.route_hash) {in.offer_count=1;in.models={};return true;}
        in.offers[2].context_slot=partnership_.slot();in.offers[2].entry=partnership_.spec().organization.key;
        const auto a=common::evaluate(in,in.offers[1].schedule),o=common::evaluate(in,in.offers[0].schedule);
        if(a.eligible()&&p6::compare(a,o)>=0) {
            auto original=std::make_unique<Offer>(std::move(in.offers[0]));in.offers[0]=std::move(in.offers[1]);in.offers[1]=std::move(*original);
        }
        return true;
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
    Command pending_u(const sdk::Snapshot& snapshot,const LocalFrame& local) {
        const auto& s=partnership_.spec();const auto& state=snapshot.own;
        const bool represented=local.fresh()&&!local.fuel_resumed&&work_.route_witness_active&&
            work_.generation==partnership_.assay.generation&&work_.started_clock==partnership_.assay.start&&
            plan_route_hash(work_.plan)==s.route.key.route_hash&&int(snapshot.frame.own.actor.type)==s.route.key.champion&&
            input_->settings_identity.value==s.route.key.settings_id&&input_->threat_regime.value==s.route.key.threat_regime&&
            state.equipment_hash==s.route.key.build.equipment_hash&&state.max_health==s.route.key.build.max_health&&
            state.strength==s.route.key.build.strength&&state.growth==s.route.key.build.growth&&s.route.envelope.contains(state,s.route.key.build);
        if(!represented){partnership_.assay.fail();work_.request_stop();}
        // Original ordinary permission survives represented damage/Prism Focus;
        // entry HP is not revalidated in the middle of its owned productive job.
        return work_.step(local);
    }
    void observed(const LocalFrame& local,const Command& command) {
        const auto result=disagreement_partnership::observe(*input_,local,command,partnership_,*ledger_,probes_,work_,trace_);
        if(result.accepted)std::cout<<"p8-13 EPISODE positive="<<result.full_positive<<" contradiction="<<result.contradiction<<'\n';
        if(partnership_.assay.attempted&&!partnership_.assay.active&&!result_logged_) {
            result_logged_=true;std::cout<<"p8-13 ASSAY resolved="<<partnership_.assay.resolved<<" unavailable="<<partnership_.assay.unavailable
                <<" model_set_failed="<<partnership_.assay.model_set_failed<<" actual_stage="<<partnership_.assay.observed.lower<<'\n';
        }
        if(work_.plan.organization==Organization::H&&!proof_logged_){proof_logged_=true;
            std::cout<<"p8-13 PROOF_UNAVAILABLE ally_actual_attack_attribution; readiness is not evidence\n";}
    }
public:
    void fault(){work_.request_stop();work_.route_witness_active=false;partnership_.assay.fail();if(ledger_&&ledger_->active())ledger_->invalidate_active(int(cambc::round_number()));}
    void turn() {
        const int startup_round=int(cambc::round_number());
    const auto startup_room=[&](std::uint64_t reserve) {
        return int(cambc::round_number())==startup_round&&cambc::fuel_remaining()>=reserve;
    };
    if(!routes_.initialized()) {
        if(!startup_room(70000))return;
        if(!diagnostic_routes_started_&&startup_room(32000)) {
            std::printf("p8-13 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                startup_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
            diagnostic_routes_started_=true;
        }
        if(!startup_room(70000))return;
        const bool complete=routes_.initialize_step(1);
        if(complete&&startup_room(32000))
            std::printf("p8-13 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                startup_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
        return;
    }
    if(!work_.active&&startup_room(70000))routes_.prepare();
    if(!startup_room(30000))return;
        auto snapshot=adapter_.observe(selected_site_<0?nullptr:&routes_.sites[selected_site_].group,&work_);if(!snapshot.valid){fault();return;}
        if(!ledger_)ledger_=std::make_unique<EvidenceLedger>(snapshot.frame.clock.self);
        const bool skipped=last_round_>=0&&snapshot.frame.clock.round!=last_round_+1;
        if(snapshot.frame.clock.round==last_round_)return;last_round_=snapshot.frame.clock.round;populate(snapshot);
        if(work_.active) {
            const auto certificates=current_safety(snapshot);bool safe=false;
            for(int c=0;c<initial_safety_count_;++c)if(certificates[c].actor==snapshot.frame.clock.self)
                safe=certificates[c].valid(snapshot.frame,snapshot.frame.clock.self,work_.plan.group);
            safe&=threats(work_.plan.group)==threat_;
            auto local=adapter_.local(snapshot,work_.plan,{certificates.data(),std::size_t(initial_safety_count_)},safe,calm(snapshot),productive(snapshot),escape(snapshot),&work_);
            local.fuel_resumed|=skipped;if(skipped||!fresh(snapshot,15000)){fault();return;}
            const bool pending=work_.plan.organization==Organization::U&&partnership_.declared()&&partnership_.assay.attempted&&work_.generation==partnership_.assay.generation;
            const auto command=pending?pending_u(snapshot,local):disagreement_partnership::advance(*input_,local,partnership_,*ledger_,arm,work_);
            dispatch(command,local);observed(local,command);return;
        }
        if(snapshot.own.bag||snapshot.frame.own.actor.channel!=p6::Channel::none||!snapshot.frame.own.actor.alive){fallback(snapshot);return;}
        auto own=best_solo(snapshot.frame);if(!own){fallback(snapshot);return;}
        if(!build_menu(snapshot,*own)){fallback(snapshot);return;}
        if(!fresh(snapshot)){fault();return;}
        auto decision=disagreement_partnership::decide(*input_,partnership_,*ledger_,probes_,arm);
        if(decision.option<0){fallback(snapshot);return;}
        const auto planned=snapshot;const int action_group=input_->offers[decision.option].plan.group;int site=-1;
        for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)site=s;
        if(site<0)return;snapshot=adapter_.observe(&routes_.sites[site].group,&work_);
        if(!unchanged(planned,snapshot)){fault();return;}populate(snapshot);
        for(int o=0;o<input_->offer_count;++o)for(int c=0;c<input_->offers[o].safety_count;++c)input_->offers[o].safety[c].clock=snapshot.frame.clock;
        if(input_->visible_proposer.knowledge==p6::Knowledge::exact)input_->visible_proposer.clock=snapshot.frame.clock;
        if(!fresh(snapshot)){fault();return;}
        decision=disagreement_partnership::decide(*input_,partnership_,*ledger_,probes_,arm);
        if(decision.option<0||input_->offers[decision.option].plan.group!=action_group)return;
        auto& chosen=input_->offers[decision.option];
        auto local=adapter_.local(snapshot,chosen.plan,{chosen.safety.data(),std::size_t(chosen.safety_count)},true,calm(snapshot),true,escape(snapshot));
        if(!fresh(snapshot)){fault();return;}
        const auto admitted=disagreement_partnership::commit(*input_,local,decision,partnership_,*ledger_,probes_,arm,work_,trace_);
        if(!admitted.started){fallback(snapshot);return;}
        selected_site_=site;selected_bank_=own->bank;renewal_site_=chosen.plan.organization==Organization::H&&chosen.plan.collector!=snapshot.frame.clock.self?own->site:site;
        if(chosen.plan.organization==Organization::H&&snapshot.frame.own.actor.pick_ordinal==0)++helper_attempts_;
        initial_safety_=chosen.safety;initial_safety_count_=chosen.safety_count;threat_=threats(chosen.plan.group);
        std::cout<<"p8-13 ADMIT group="<<chosen.plan.group<<" organization="<<int(chosen.plan.organization)<<" episode="<<admitted.episode<<" stage="<<admitted.stage_issued<<'\n';
        inspection_turns_=0;inspection_site_=-1;unavailable_logged_=false;
        const auto command=work_.step(local);dispatch(command,local);observed(local,command);
    }
};
inline void draft() {
    const auto own=cambc::draft::current_draft(cambc::team::ally);const int ordinal=int(own.size()),picks=int(cambc::draft::team_size());if(ordinal>=picks)return;
    common::DraftInputs in;in.ordinal=ordinal;in.picks=picks;in.prospective_actor=0;
    for(int i=0;i<ordinal&&i<5;++i)in.own_picks[i]=sdk::type(own[i]);in.settings=sdk::settings();in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
    const int window=std::min(common::forecast_window,in.settings.max_rounds);in.windows.deadlines=in.windows.windows={window,window,window};
    in.windows.sampled_actor_id=0;in.windows.sampled_round=0;in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto candidates=p8::native::draft_candidates(0,ordinal,true);const auto selected=disagreement_partnership::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-13 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
    for(const auto type:priority(ordinal)){const auto pick=sdk::draft_type(type);if(std::find(own.begin(),own.end(),pick)==own.end()){cambc::draft::pick(pick);return;}}
}
inline int run() {
    std::unique_ptr<Runtime> bot;while(true) {
        try {
            if(cambc::is_draft())native::draft();else {
                if(!bot) {
                    const int construction_round=int(cambc::round_number());
                    bot.reset(new Runtime);
                    if(int(cambc::round_number())==construction_round&&cambc::fuel_remaining()>=32000)
                        std::printf("p8-13 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                            construction_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                bot->turn();
            }
        }
        catch(const cambc::error& e){std::cout<<"p8-13 ERROR engine "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& e){std::cout<<"p8-13 ERROR exception "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-13 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::disagreement_partnership::native
