#pragma once
#include "configuration.h"
#include "../draft.h"
#include <cstdio>
#include <iostream>
#include <memory>
namespace p8::finite_counterprotocol::native {
class Runtime {
    bool diagnostic_routes_started_=false;
    Routes routes_;sdk::Adapter adapter_;std::unique_ptr<Inputs> input_=p8::native::make_default<Inputs>();
    std::unique_ptr<EvidenceLedger> ledger_;std::unique_ptr<Protocol> protocol_=std::make_unique<Protocol>();
    std::unique_ptr<Layout> layout_=std::make_unique<Layout>();ConvoyTrace trace_;Work work_;ProbeBudget probes_;p6::ScoreHistory scores_;
    RivalDistance distances_;Field escort_;int source_=-1,bank_=-1,next_=-1,next_bank_=-1;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};int initial_safety_count_=0;
    int selected_site_=-1,renewal_site_=-1,last_round_=-1;
    int inspection_turns_=0,next_scan_=0,inspection_site_=-1,inspection_cursor_=0;
    Route inspection_{};std::uint64_t threat_=0;bool unavailable_logged_=false,proof_limit_logged_=false,configured_=false,first_income_=false;
    int named_rival_=-1;std::uint16_t reported_encounters_=0,reported_escorts_=0,reported_incomes_=0;
    struct Member {int actor=-1;NumericState state{};};std::array<Member,2> members_{};bool members_frozen_=false;
    bool bank_unlocked(int bank) const {
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))
            if(point(lock.position())==routes_.banks[bank].altar)return false;
        return true;
    }
    bool exclusive(const p8::native::Site& site,int owner,bool pair=false) const {
        const auto spawns=site.group.spawn_tiles();
        for(const auto& ally:cambc::vision::champions(cambc::team::ally))if(ally.is_alive()&&spawns.is_set(ally.position())) {
            const int id=sdk::identity(ally);if(id==owner)continue;
            if(!pair||!input_->draft.valid()||(id!=input_->draft.actors[0]&&id!=input_->draft.actors[1]))return false;
        }
        return true;
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
        std::unique_ptr<Solo> best;p6::ScoreEvaluation rank;auto windows=input_->windows;
        windows.sampled_actor_id=frame.clock.self;windows.sampled_round=frame.clock.round;
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>55000;++s) {
            if(fixed>=0&&s!=fixed)continue;
            if(std::find(excluded.begin(),excluded.end(),group_id(routes_.sites[s].group))!=excluded.end())continue;
            if(!exclusive(routes_.sites[s],frame.clock.self))continue;
            for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>55000;++b) {
                if(bank>=0&&b!=bank)continue;
                if(!bank_unlocked(b))continue;
                auto model=std::make_unique<Solo>(solo(routes_,s,b,frame,false));
                if(!model->feasible)continue;
                const auto score=p6::evaluate_schedule(model->model.schedule,windows,input_->settings,{frame.clock.round,frame.clock.self},frame.own_team_score);
                if(score.eligible()&&(!best||p6::compare(score,rank)>0)) {rank=score;best=std::move(model);}
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
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        bool bounded=false;const auto regime=rival_regime(bounded);
        in.threat_regime={int(regime&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        in.target_group=source_>=0?group_id(routes_.sites[source_].group):0;
        const auto enemy=cambc::global_submitted_gems(cambc::team::enemy);scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        if(in.offer_count==0){in.offer_count=1;in.independent=0;}
        if(configured_)in.layout=*layout_;
        if(named_rival_<0)for(int i=0;i<snapshot.frame.actor_count;++i) {
            const auto& actor=snapshot.frame.actors[i].value;if(actor.team==p6::Team::enemy&&actor.alive&&(named_rival_<0||actor.id<named_rival_))named_rival_=actor.id;
        }
        in.rival=named_rival_;in.distance=distances_.read(in.frame,named_rival_);
        (void)bounded;
    }
    std::vector<Solo> baselines(const sdk::Snapshot& snapshot,const Solo& own) {
        std::vector<Solo> out;out.reserve(3);out.push_back(own);normalize(out.back(),snapshot.frame);
        if(snapshot.frame.own.actor.pick_ordinal>=3)return out;
        // Preserve this actor's best represented U alternative. Distinct
        // productive sources avoid counting a camp reward twice.
        for(int ordinal=0;ordinal<3&&ordinal<snapshot.draft.picks;++ordinal) {
            const int id=snapshot.draft.actors[ordinal];const auto* actor=snapshot.frame.visible(id);
            if(id==snapshot.frame.clock.self)continue;
            if(!actor||!actor->alive||actor->channel!=p6::Channel::none)return {};
            std::array<int,3> excluded{};int count=0;for(const auto& model:out)excluded[count++]=model.plan.group;
            auto model=best_solo(subject(snapshot.frame,*actor),-1,-1,{excluded.data(),std::size_t(count)});
            if(!model)return {};normalize(*model,snapshot.frame);out.push_back(std::move(*model));
        }
        return out;
    }
    bool current_members(const sdk::Snapshot& snapshot,std::array<Member,2>& out) const {
        for(int ordinal=0;ordinal<2;++ordinal) {
            const int id=snapshot.draft.actors[ordinal];const auto* seen=snapshot.frame.visible(id);if(!seen||!seen->alive)return false;
            if(id==snapshot.frame.clock.self){out[ordinal]={id,snapshot.own};continue;}
            bool found=false;for(const auto& ally:cambc::vision::champions(cambc::team::ally))if(sdk::identity(ally)==id) {
                const auto stats=ally.base_stats();out[ordinal]={id,{double(ally.health()),double(ally.max_health()),double(ally.strength()),double(ally.focus()),
                    double(stats.strength_growth),std::int64_t(ally.gem_count()),sdk::equipment_identity(ally)}};
                found=valid_numeric_state(out[ordinal].state);break;
            }
            if(!found)return false;
        }
        return true;
    }
    bool member_regime(const sdk::Snapshot& snapshot) const {
        if(!members_frozen_)return true;std::array<Member,2> now{};if(!current_members(snapshot,now))return false;
        for(int i=0;i<2;++i) {
            const auto& old=members_[i].state;const auto& actual=now[i].state;const bool prism=snapshot.draft.champions[i]==p6::Champion::prism;
            const bool focus=prism?actual.focus>=old.focus&&actual.focus<=focus_upper(old.focus,actual.bag):actual.focus==old.focus;
            if(now[i].actor!=members_[i].actor||actual.equipment_hash!=old.equipment_hash||actual.max_health!=old.max_health||
                actual.strength!=old.strength||actual.growth!=old.growth||!focus)return false;
        }
        return true;
    }
    int proposal(const sdk::Snapshot& snapshot) const {
        const auto* owner=snapshot.frame.visible(snapshot.draft.actors[0]);if(!owner||!owner->alive||owner->bag||owner->channel!=p6::Channel::none)return -1;
        int source=-1;for(int s=0;s<int(routes_.sites.size());++s)if(routes_.sites[s].rally_count>=2&&owner->position==routes_.sites[s].rally[0].destination) {
            if(source>=0)return -1;source=s;
        }
        return source;
    }
    bool configure(const sdk::Snapshot& snapshot,const Solo& own,std::span<const Solo> baseline) {
        if(configured_)return true;
        if(snapshot.draft.picks<3||baseline.size()!=3||snapshot.frame.own.actor.pick_ordinal>1||snapshot.own.bag)return false;
        const bool owner=snapshot.frame.clock.self==snapshot.draft.actors[0];int source=owner?own.site:proposal(snapshot);
        // Freeze a response-conditioned context only when its named rival is
        // actually visible at this empty independent boundary. An unseen
        // future arrival is not silently inserted into an older threat key.
        const auto* rival=snapshot.frame.visible(named_rival_);
        if(owner&&(!rival||rival->team!=p6::Team::enemy||!rival->alive))return false;
        if(source<0)return false;const int bank=public_bank(routes_,source);int next=-1,next_bank=-1;
        for(const auto& model:baseline)if(model.plan.collector==snapshot.draft.actors[1]){next=model.site;next_bank=model.bank;}
        if(!owner) {
            // The helper can inspect its own current circuit and the owner's
            // public pad. It receives no private response/route message.
            next=-1;for(int s=0;s<int(routes_.sites.size());++s)if(snapshot.frame.own.actor.position==routes_.sites[s].approach.destination&&s!=source) {
                if(next>=0)return false;next=s;
            }
        }
        if(next<0||next==source)return false;
        next_bank=public_bank(routes_,next);
        if(bank<0||next_bank<0||!bank_unlocked(bank)||!bank_unlocked(next_bank)||!geometry(*layout_,escort_,routes_,source,bank,next,next_bank,snapshot,distances_))return false;
        std::array<Member,2> members{};if(!current_members(snapshot,members))return false;
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000)return false;
        input_->layout=*layout_;input_->target_group=group_id(routes_.sites[source].group);
        if(owner) {
            const auto full=declaration(*layout_,*input_,routes_.sites[source].group.gem_value());
            if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000)return false;
            if(!protocol_->declare(*layout_,full,snapshot.draft,*ledger_))return false;
        }
        source_=source;bank_=bank;next_=next;next_bank_=next_bank;configured_=true;members_=members;members_frozen_=true;
        std::cout<<"p8-15 DECLARED group="<<layout_->collector.group<<" altar="<<layout_->site.altar<<" escort="<<layout_->escort_pad.x<<','<<layout_->escort_pad.y<<'\n';return true;
    }
    bool menu(const sdk::Snapshot& snapshot,const Solo& own) {
        auto& in=*input_;in.offer_count=1;in.independent=0;in.visible_proposer={};
        auto models=baselines(snapshot,own);if(models.empty()){models.push_back(own);normalize(models.back(),snapshot.frame);}
        if(!independent_offer(in.offers[0],own,models,snapshot.frame))return false;
        configure(snapshot,own,models);if(!configured_)return true;
        in.layout=*layout_;
        // The same short conversion and renewal are a real independent option.
        if(bank_unlocked(bank_)&&exclusive(routes_.sites[source_],snapshot.frame.clock.self)) {
            auto short_u=std::make_unique<Solo>(solo(routes_,source_,bank_,snapshot.frame,false));
            if(short_u->feasible&&route_hash(short_u->plan.bank)==route_hash(layout_->collector.bank)&&
                route_hash(short_u->plan.renewal)==route_hash(layout_->collector.renewal)) {
                auto alternatives=baselines(snapshot,*short_u);
                if(!alternatives.empty()&&independent_offer(in.offers[in.offer_count],*short_u,alternatives,snapshot.frame))++in.offer_count;
            }
        }
        if(snapshot.frame.clock.self==snapshot.draft.actors[1]&&protocol_->helper.phase==HelperPhase::own_income&&bank_unlocked(next_bank_)&&
            snapshot.frame.own.actor.position==layout_->escort_pad) {
            auto after=std::make_unique<Solo>(solo(routes_,next_,next_bank_,snapshot.frame,false));
            if(after->feasible&&plan_route_hash(after->plan)==plan_route_hash(layout_->helper_after)) {
                auto alternatives=baselines(snapshot,*after);
                if(!alternatives.empty()&&independent_offer(in.offers[in.offer_count],*after,alternatives,snapshot.frame))++in.offer_count;
            }
        }
        if(!first_income_||models.size()!=3||snapshot.frame.own.actor.pick_ordinal>1||!member_regime(snapshot)||!bank_unlocked(bank_)||
            !exclusive(routes_.sites[source_],snapshot.frame.clock.self,true)||cambc::fuel_remaining()<55000)return true;
        auto candidate=std::make_unique<ConvoyModel>();convoy(*candidate,routes_,source_,bank_,next_,next_bank_,*layout_,in,models,in.offers[0]);
        if(!candidate->feasible)return true;auto& offered=in.offers[in.offer_count++];offered=candidate->offer;
        if(snapshot.frame.clock.self==snapshot.draft.actors[0]) {offered.context_slot=protocol_->slot();offered.entry=protocol_->organization().key;}
        else if(proposal(snapshot)==source_)in.visible_proposer={layout_->collector.group,snapshot.frame.clock,snapshot.draft.actors[0],p6::Knowledge::exact};
        return true;
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
        if(configured_)return finite_counterprotocol::decide(*input_,*protocol_,*ledger_,probes_,arm);
        Decision out;out.clock=input_->frame.clock;auto& offer=input_->offers[0];
        if(common::valid_inputs(*input_)&&base(*input_,offer)==Rejection::none) {
            out.option=0;out.organization=Organization::U;out.score=common::evaluate(*input_,offer.schedule);out.reason=Rejection::none;
        }
        return out;
    }
    void required_response(const sdk::Snapshot& snapshot) {
        if(!configured_||snapshot.frame.clock.self!=snapshot.draft.actors[0]||
            !protocol_->responses.approach_current(snapshot.frame.clock.self,snapshot.frame.clock.round))return;
        if(sample_failure(snapshot.frame,layout_->site,named_rival_,input_->distance)!=ResponseFailure::none)
            protocol_->responses.interrupted(snapshot.frame.clock.round);
    }
    void observed(const sdk::Snapshot& snapshot,const LocalFrame& local) {
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<16000){fault();return;}
        if(work_.cycle_completed&&work_.payout_seen&&work_.bank_seen&&work_.renewal_seen&&!first_income_) {
            first_income_=true;std::cout<<"p8-15 ORDINARY_INCOME group="<<work_.plan.group<<'\n';
        }
        if(configured_) {
            // Native response authority is sampled only during ordinary U.
            // H still observes its own physical proof and helper duties.
            const int rival=input_->rival;if(work_.plan.organization!=Organization::U)input_->rival=-1;
            const auto result=finite_counterprotocol::observe(*input_,local,*protocol_,*ledger_,work_,trace_);
            input_->rival=rival;
            if(result.accepted)std::cout<<"p8-15 EPISODE positive="<<result.full_positive<<" contradiction="<<result.contradiction<<'\n';
            if(protocol_->responses.encounters()!=reported_encounters_) {
                reported_encounters_=protocol_->responses.encounters();const auto& event=protocol_->responses.window.record;
                std::cout<<"p8-15 RESPONSE serial="<<event.serial<<" rival="<<event.rival<<" label="<<int(event.response)<<" failure="<<int(event.failure)
                    <<" baseline="<<event.baseline_round<<" end="<<event.terminal_round<<" samples="<<event.next_observations<<'\n';
            }
            if(protocol_->helper.escorted!=reported_escorts_||protocol_->helper.own_incomes!=reported_incomes_) {
                reported_escorts_=protocol_->helper.escorted;reported_incomes_=protocol_->helper.own_incomes;
                std::cout<<"p8-15 OWN_HELPER escorted="<<reported_escorts_<<" banked_renewed="<<reported_incomes_
                    <<" group="<<work_.plan.group<<" generation="<<work_.generation<<'\n';
            }
        }
        if(work_.plan.organization==Organization::H&&!proof_limit_logged_) {
            proof_limit_logged_=true;std::cout<<"p8-15 PROOF_UNAVAILABLE peer_actual_attack_attribution; visible empty bag is release capability only\n";
        }
        (void)snapshot;
    }
public:
    void fault(){work_.request_stop();work_.route_witness_active=false;protocol_->responses.interrupted(int(cambc::round_number()));if(ledger_&&ledger_->active())ledger_->invalidate_active(int(cambc::round_number()));}
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
            std::printf("p8-15 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                startup_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
            diagnostic_routes_started_=true;
        }
        if(!startup_room(70000))return;
        const bool complete=routes_.initialize_step(1);
        if(complete&&startup_room(32000))
            std::printf("p8-15 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
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
    if(!snapshot.frame.own.actor.alive)protocol_->responses.interrupted(snapshot.frame.clock.round);
    if(work_.active) {
        const auto certificates=current_safety(snapshot);bool safe=false,bounded=false;
        for(int i=0;i<initial_safety_count_;++i)if(certificates[i].actor==snapshot.frame.clock.self)safe=certificates[i].valid(snapshot.frame,snapshot.frame.clock.self,work_.plan.group);
        safe&=physical_regime(work_.plan.group,bounded)==threat_&&bounded;
        auto local=adapter_.local(snapshot,work_.plan,{certificates.data(),std::size_t(initial_safety_count_)},safe,calm(snapshot),
            work_.plan.organization==Organization::H&&snapshot.frame.clock.self!=work_.plan.collector?false:productive(snapshot),escape(snapshot),&work_);
        local.fuel_resumed|=skipped;if(skipped)fault();
        if(work_.plan.organization==Organization::H&&!member_regime(snapshot))work_.request_stop();
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<20000){fault();return;}
        required_response(snapshot);
        const auto command=configured_?finite_counterprotocol::advance(*input_,local,*protocol_,*ledger_,arm,work_):work_.step(local);
        dispatch(command,local);observed(snapshot,local);return;
    }
    if(skipped)protocol_->responses.interrupted(snapshot.frame.clock.round);
    if(snapshot.frame.own.actor.channel!=p6::Channel::none||!snapshot.frame.own.actor.alive||snapshot.own.bag>0){fallback(snapshot);return;}
    bool bounded=false;rival_regime(bounded);if(!bounded){protocol_->responses.interrupted(snapshot.frame.clock.round);fallback(snapshot);return;}
    auto own=best_solo(snapshot.frame);if(!own||!menu(snapshot,*own)){fallback(snapshot);return;}
    if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<20000){fault();return;}
    required_response(snapshot);
    auto chosen=decision();if(chosen.option<0){fallback(snapshot);return;}
    const auto before=snapshot;const int action_group=input_->offers[chosen.option].plan.group;int site=-1;
    for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)site=s;
    if(site<0)return;snapshot=adapter_.observe(&routes_.sites[site].group,&work_);
    if(!unchanged(before,snapshot)){fault();return;}populate(snapshot);
    for(int i=0;i<input_->offer_count;++i)for(int c=0;c<input_->offers[i].safety_count;++c)input_->offers[i].safety[c].clock=snapshot.frame.clock;
    if(input_->visible_proposer.knowledge==p6::Knowledge::exact)input_->visible_proposer.clock=snapshot.frame.clock;
    if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<20000){fault();return;}
    required_response(snapshot);
    chosen=decision();if(chosen.option<0||input_->offers[chosen.option].plan.group!=action_group)return;
    auto& offered=input_->offers[chosen.option];if(offered.plan.organization==Organization::H&&!member_regime(snapshot))return;
    auto local=adapter_.local(snapshot,offered.plan,{offered.safety.data(),std::size_t(offered.safety_count)},true,calm(snapshot),true,escape(snapshot));
    if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<20000){fault();return;}
    Admission admitted;if(configured_)admitted=finite_counterprotocol::commit(*input_,local,chosen,*protocol_,*ledger_,probes_,arm,work_,trace_);
    else admitted.started=physical_admissible(offered.plan,local)&&work_.start(offered.plan,local);
    if(!admitted.started){fallback(snapshot);return;}
    selected_site_=renewal_site_=site;initial_safety_=offered.safety;initial_safety_count_=offered.safety_count;
    threat_=physical_regime(action_group,bounded);inspection_turns_=0;inspection_site_=-1;unavailable_logged_=false;
    std::cout<<"p8-15 ADMIT group="<<action_group<<" organization="<<int(offered.plan.organization)<<" episode="<<admitted.episode<<'\n';
    if(!bounded||int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<18000){fault();return;}
    const auto command=configured_?finite_counterprotocol::advance(*input_,local,*protocol_,*ledger_,arm,work_):work_.step(local);
    dispatch(command,local);observed(snapshot,local);
}
inline void draft() {
    const auto own=cambc::draft::current_draft(cambc::team::ally);const int ordinal=int(own.size()),picks=int(cambc::draft::team_size());if(ordinal>=picks)return;
    common::DraftInputs in;in.ordinal=ordinal;in.picks=picks;in.prospective_actor=0;
    for(int i=0;i<ordinal&&i<5;++i)in.own_picks[i]=sdk::type(own[i]);in.settings=sdk::settings();in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
    const int window=std::min(120,in.settings.max_rounds);in.windows.deadlines=in.windows.windows={window,window,window};
    in.windows.sampled_actor_id=0;in.windows.sampled_round=0;in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto candidates=p8::native::draft_candidates(0,ordinal,false);const auto selected=finite_counterprotocol::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-15 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
    for(const auto champion:priority(ordinal)){const auto pick=sdk::draft_type(champion);
        if(std::find(own.begin(),own.end(),pick)==own.end()){cambc::draft::pick(pick);return;}}
}
inline int run() {
    std::unique_ptr<Runtime> bot;while(true) {
        try {
            if(cambc::is_draft())native::draft();else {
                if(!bot) {
                    const int construction_round=int(cambc::round_number());
                    bot.reset(new Runtime);
                    if(int(cambc::round_number())==construction_round&&cambc::fuel_remaining()>=32000)
                        std::printf("p8-15 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                            construction_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                bot->turn();
            }
        }
        catch(const cambc::error& error){std::cout<<"p8-15 ERROR engine "<<error.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& error){std::cout<<"p8-15 ERROR exception "<<error.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-15 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::finite_counterprotocol::native
