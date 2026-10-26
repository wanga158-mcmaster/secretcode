#pragma once
#include "configuration.h"
#include "../../../common/src/defaults.h"
#include "../draft.h"
#include <bit>
#include <cstdio>
#include <iostream>
#include <memory>
namespace p8::assay_guild::native {
using namespace p8::native;
class Runtime {
    Routes routes_;sdk::Adapter adapter_;std::unique_ptr<Inputs> input_=p8::native::make_default<Inputs>();
    std::unique_ptr<EvidenceLedger> ledger_;std::unique_ptr<Guild> guild_;Execution execution_;p6::ScoreHistory scores_;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};int initial_safety_count_=0;
    int selected_site_=-1,renewal_site_=-1,last_round_=-1;
    int inspection_turns_=0,next_scan_=0,inspection_site_=-1,inspection_cursor_=0;
    Route inspection_{};std::uint64_t threat_=0;bool unavailable_logged_=false,proof_limit_logged_=false,specialization_failed_=false;
    std::array<RoleSpec,2> frozen_roles_{};std::array<int,2> role_slots_{-1,-1};
    struct MemberState {int actor=-1;NumericState state{};};
    std::array<MemberState,2> member_entry_{};bool members_frozen_=false,assay_reported_=false;
    bool diagnostic_routes_started_=false;
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
        execution_.confirm(command,receipt);
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
    static std::uint64_t rivals() {
        std::uint64_t hash=1469598103934665603ULL;
        for(const auto& rival:cambc::vision::champions(cambc::team::enemy)) {
            hash=hash_word(hash,std::uint64_t(sdk::identity(rival))+1);const auto p=rival.position();
            hash=hash_word(hash,std::uint32_t(p.x));hash=hash_word(hash,std::uint32_t(p.y));
            hash=hash_word(hash,std::bit_cast<std::uint64_t>(double(rival.strength())));hash=hash_word(hash,std::bit_cast<std::uint64_t>(double(rival.focus())));
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
        in.threat_regime={int(rivals()&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        in.target_group=guild_&&guild_->assay.attempted&&guild_->assay.spec.ordinary.group>=0?guild_->assay.spec.ordinary.group:
            selected_site_>=0?group_id(routes_.sites[selected_site_].group):0;
        const auto enemy=cambc::global_submitted_gems(cambc::team::enemy);scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        if(in.offer_count==0){in.offer_count=1;in.independent=0;}
    }
    std::vector<Solo> baselines(const sdk::Snapshot& snapshot,const Solo& own) {
        std::vector<Solo> out;out.reserve(3);out.push_back(own);normalize(out.back(),snapshot.frame);
        for(int ordinal=0;ordinal<3&&ordinal<snapshot.draft.picks;++ordinal) {
            const int id=snapshot.draft.actors[ordinal];if(id==snapshot.frame.clock.self)continue;
            const auto* actor=snapshot.frame.visible(id);if(!actor||!actor->alive||actor->channel!=p6::Channel::none)return {};
            std::array<int,3> used{};int count=0;for(const auto& model:out)used[count++]=model.plan.group;
            auto model=best_solo(subject(snapshot.frame,*actor),-1,-1,{used.data(),std::size_t(count)});if(!model)return {};
            normalize(*model,snapshot.frame);out.push_back(std::move(*model));
        }
        return out;
    }
    bool current_members(const sdk::Snapshot& snapshot,std::array<MemberState,2>& out) const {
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
        if(!members_frozen_)return true;std::array<MemberState,2> now{};if(!current_members(snapshot,now))return false;
        for(int i=0;i<2;++i) {
            const auto& old=member_entry_[i].state;const auto& actual=now[i].state;
            const bool prism=snapshot.draft.champions[i]==p6::Champion::prism;
            const bool focus=prism?actual.focus>=old.focus&&actual.focus<=focus_upper(old.focus,actual.bag):actual.focus==old.focus;
            if(now[i].actor!=member_entry_[i].actor||actual.equipment_hash!=old.equipment_hash||actual.max_health!=old.max_health||
                actual.strength!=old.strength||actual.growth!=old.growth||!focus)return false;
        }
        return true;
    }
    std::pair<int,Configuration> visible_proposal(const sdk::Snapshot& snapshot) const {
        const auto* owner=snapshot.frame.visible(snapshot.draft.actors[0]);if(!owner||!owner->alive||owner->bag||owner->channel!=p6::Channel::none)return {-1,Configuration::independent};
        int source=-1;Configuration configuration=Configuration::independent;
        for(int s=0;s<int(routes_.sites.size());++s) {
            const auto& site=routes_.sites[s];if(site.rally_count<2)continue;
            for(int r=0;r<2;++r)if(owner->position==site.rally[r].destination) {
                if(source>=0)return {-1,Configuration::independent};source=s;configuration=r==0?Configuration::joint_clear:Configuration::serial;
            }
        }
        return {source,configuration};
    }
    bool menu(const sdk::Snapshot& snapshot,const Solo& own) {
        auto& in=*input_;in.offer_count=1;in.independent=0;
        for(auto& role:in.roles)role.offer=-1;
        in.proposal.knowledge=in.private_income.knowledge=p6::Knowledge::unknown;
        auto models=baselines(snapshot,own);
        auto private_offer=std::make_unique<Offer>();if(independent_offer(*private_offer,own,{&own,1},snapshot.frame)) {
            in.private_income.value=*private_offer;in.private_income.clock=snapshot.frame.clock;in.private_income.subject=snapshot.frame.clock.self;in.private_income.knowledge=p6::Knowledge::exact;
        }
        if(models.size()!=3||!independent_offer(in.offers[0],own,models,snapshot.frame)) {
            in.offers[0].plan.group=-1;in.offers[0].safety_count=0;in.assay.ordinary.group=-1;in.assay.fixed.key.route_hash=0;return true;
        }
        if(snapshot.frame.own.actor.pick_ordinal==0&&!guild_->assay.attempted) {
            in.assay.ordinary.group=-1;in.assay.fixed.key.route_hash=0;nominal_assay(in.assay,own,in.offers[0],in);return true;
        }
        if(snapshot.frame.own.actor.pick_ordinal>=2||models.size()!=3||specialization_failed_||!member_regime(snapshot))return true;
        int source=-1;Configuration announced=Configuration::independent;
        if(snapshot.frame.own.actor.pick_ordinal==0) {
            if(!guild_->assay.current(in))return true;
            for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==guild_->assay.spec.ordinary.group)source=s;
        } else {
            const auto visible=visible_proposal(snapshot);source=visible.first;announced=visible.second;
            if(source>=0)in.proposal={{announced,group_id(routes_.sites[source].group)},snapshot.frame.clock,snapshot.draft.actors[0],p6::Knowledge::exact};
        }
        if(source<0||routes_.sites[source].rally_count<2||!exclusive(routes_.sites[source],snapshot.frame.clock.self,true))return true;
        for(const auto& model:models)if(model.plan.collector==snapshot.draft.actors[2]&&model.plan.group==group_id(routes_.sites[source].group))return true;
        const int bank=public_bank(routes_,source);if(bank<0||!bank_unlocked(bank))return true;
        for(int r=0;r<2&&cambc::fuel_remaining()>65000;++r) {
            const auto configuration=r==0?Configuration::joint_clear:Configuration::serial;
            if(snapshot.frame.own.actor.pick_ordinal==1&&configuration!=announced)continue;
            const int helper=snapshot.draft.actors[configuration==Configuration::joint_clear?1:0];const auto* actor=snapshot.frame.visible(helper);if(!actor)continue;
            std::array<int,2> exclude{group_id(routes_.sites[source].group),-1};
            for(const auto& model:models)if(model.plan.collector==snapshot.draft.actors[2])exclude[1]=model.plan.group;
            auto next=best_solo(subject(snapshot.frame,*actor),-1,-1,exclude);if(!next)continue;
            auto role=std::make_unique<RoleModel>();role_model(*role,routes_,source,bank,in,models,*next,configuration);if(!role->feasible)continue;
            if(!independent_unchanged(in.offers[0].schedule,role->offer.schedule,snapshot.draft.actors[2]))continue;
            if(role_slots_[r]<0) {
                if(p6::compare(common::evaluate(in,role->offer.schedule),common::evaluate(in,in.offers[0].schedule))<=0||
                    snapshot.own.health<common::minimum_trial_health_fraction*snapshot.own.max_health||
                    int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000)continue;
                const int slot=ledger_->predeclare(role->role.context);if(slot<0)continue;role_slots_[r]=slot;frozen_roles_[r]=role->role;
            }
            if(role->role.context.key!=frozen_roles_[r].context.key)continue;
            auto& offered=in.offers[in.offer_count];offered=role->offer;offered.context_slot=role_slots_[r];offered.entry=frozen_roles_[r].context.key;
            in.roles[r].offer=in.offer_count;in.roles[r].role=role->role;in.roles[r].role.context=frozen_roles_[r].context;++in.offer_count;
        }
        return true;
    }
    bool unchanged(const sdk::Snapshot& a,const sdk::Snapshot& b) const {
        if(!b.valid||a.frame.clock.round!=b.frame.clock.round||a.frame.clock.self!=b.frame.clock.self||a.own.equipment_hash!=b.own.equipment_hash||
            a.own.health!=b.own.health||a.own.max_health!=b.own.max_health||a.own.strength!=b.own.strength||a.own.focus!=b.own.focus||a.own.growth!=b.own.growth||
            a.own.bag!=b.own.bag||a.frame.own.gold!=b.frame.own.gold||a.frame.own.actor.position!=b.frame.own.actor.position)return false;
        for(int i=0;i<input_->offer_count;++i)for(int j=0;j<input_->offers[i].schedule.actor_count;++j) {
            const int id=input_->offers[i].schedule.actors[j].actor_id;const auto* x=a.frame.visible(id);const auto* y=b.frame.visible(id);
            if(!x||!y||x->health!=y->health||x->max_health!=y->max_health||x->strength!=y->strength||x->bag!=y->bag||x->position!=y->position||x->channel!=y->channel||x->alive!=y->alive)return false;
        }
        return true;
    }
    void observed(const sdk::Snapshot& snapshot,const LocalFrame& local) {
        const auto result=assay_guild::observe(*input_,local,*guild_,*ledger_,execution_);
        if(result.accepted)std::cout<<"p8-10 ROLE_EPISODE positive="<<result.full_positive<<" contradiction="<<result.contradiction<<'\n';
        if(guild_->assay.attempted&&!guild_->assay.active&&!assay_reported_) {
            assay_reported_=true;std::cout<<"p8-10 ASSAY resolved="<<guild_->assay.resolved<<" C="<<guild_->assay.clear<<" L="<<guild_->assay.logistics
                <<" nominal_C="<<guild_->assay.nominal_clear<<" nominal_L="<<guild_->assay.nominal_logistics<<'\n';
        }
        if(execution_.specialized&&!proof_limit_logged_&&(execution_.frozen.configuration==Configuration::joint_clear||collecting(execution_.frozen,snapshot.frame.clock.self))) {
            proof_limit_logged_=true;std::cout<<"p8-10 PROOF_UNAVAILABLE peer_actual_attack_attribution; readiness is not evidence\n";
        }
    }
    void fail_specialization(int round) {
        if(!specialization_failed_)std::cout<<"p8-10 SPECIALIZATION_STOP round="<<round<<" secondary_entry_unavailable\n";
        specialization_failed_=true;execution_.work.request_stop();execution_.trace.failed=true;
        if(ledger_->active())ledger_->invalidate_active(round);execution_.awaiting_next=false;
    }
    bool prepare_next(sdk::Snapshot& snapshot) {
        const auto& retained=execution_.frozen.next;int source=-1,bank=-1;
        for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==retained.group)source=s;
        for(int b=0;b<int(routes_.banks.size());++b)if(sdk::altar_id(routes_.banks[b].altar)==retained.altar)bank=b;
        if(source<0||bank<0||!bank_unlocked(bank)||!exclusive(routes_.sites[source],snapshot.frame.clock.self)){fail_specialization(int(cambc::round_number()));return false;}
        const int expected=snapshot.frame.clock.round;snapshot=adapter_.observe(&routes_.sites[source].group,&execution_.work);
        if(!snapshot.valid||snapshot.frame.clock.round!=expected){fail_specialization(int(cambc::round_number()));return false;}
        populate(snapshot);
        auto next=std::make_unique<Solo>(solo(routes_,source,bank,snapshot.frame,false));
        const auto& context=execution_.frozen.context;
        if(snapshot.own.bag!=0||!next->feasible||plan_route_hash(next->plan)!=plan_route_hash(retained)||
            snapshot.frame.own.actor.position!=retained.ingress.tiles[0]||!context.envelope.contains(snapshot.own,context.key.build)||
            int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000) {
            fail_specialization(int(cambc::round_number()));return false;
        }
        initial_safety_={};initial_safety_[0]=next->model.certificate;initial_safety_count_=1;
        selected_site_=renewal_site_=source;threat_=threats(retained.group);return true;
    }
public:
    void fault(){execution_.work.request_stop();execution_.work.route_witness_active=false;if(ledger_&&ledger_->active())ledger_->invalidate_active(int(cambc::round_number()));}
    void turn();
};
} // namespace p8::assay_guild::native

namespace p8::assay_guild::native {
inline void Runtime::turn() {
    const int entered=int(cambc::round_number());
    const auto room=[&](std::uint64_t reserve) {
        return int(cambc::round_number())==entered&&cambc::fuel_remaining()>=reserve;
    };
    if(!routes_.initialized()) {
        if(!room(70000))return;
        if(!diagnostic_routes_started_&&room(32000)) {
            diagnostic_routes_started_=true;
            std::printf("p8-10 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),entered,
                static_cast<unsigned long long>(cambc::fuel_remaining()));
        }
        if(!room(70000))return;
        const bool complete=routes_.initialize_step(1);
        if(complete&&room(32000))
            std::printf("p8-10 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),entered,
                static_cast<unsigned long long>(cambc::fuel_remaining()));
        return;
    }
    if(!execution_.work.active&&!execution_.awaiting_next&&room(70000))routes_.prepare();
    if(int(cambc::round_number())!=entered)return;
    const auto* selected=selected_site_<0?nullptr:&routes_.sites[selected_site_].group;
    auto snapshot=adapter_.observe(selected,&execution_.work);if(!snapshot.valid){fault();return;}
    if(!ledger_){ledger_=std::make_unique<EvidenceLedger>(snapshot.frame.clock.self);guild_=std::make_unique<Guild>(snapshot.frame.clock.self);}
    const bool skipped=last_round_>=0&&snapshot.frame.clock.round!=last_round_+1;
    if(snapshot.frame.clock.round==last_round_)return;last_round_=snapshot.frame.clock.round;populate(snapshot);
    if(members_frozen_) {
        std::array<MemberState,2> now{};if(current_members(snapshot,now)&&!member_regime(snapshot))specialization_failed_=true;
    }
    if(execution_.awaiting_next) {
        if(skipped||specialization_failed_||!prepare_next(snapshot)) {
            fail_specialization(snapshot.frame.clock.round);fallback(snapshot);return;
        }
    }
    if(execution_.work.active||execution_.awaiting_next) {
        const auto certificates=current_safety(snapshot);bool safe=false;const auto& plan=execution_.selected_plan();
        for(int i=0;i<initial_safety_count_;++i)if(certificates[i].actor==snapshot.frame.clock.self)safe=certificates[i].valid(snapshot.frame,snapshot.frame.clock.self,plan.group);
        safe&=threats(plan.group)==threat_;
        auto local=adapter_.local(snapshot,plan,{certificates.data(),std::size_t(initial_safety_count_)},safe,calm(snapshot),productive(snapshot),escape(snapshot),
            execution_.awaiting_next?nullptr:&execution_.work);
        local.fuel_resumed|=skipped;if(skipped)fault();
        if(execution_.specialized&&!execution_.next&&!execution_.awaiting_next&&(!member_regime(snapshot)||specialization_failed_))execution_.work.request_stop();
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<20000) {fault();return;}
        const auto command=assay_guild::advance(*input_,local,*guild_,*ledger_,execution_);dispatch(command,local);observed(snapshot,local);return;
    }
    if(snapshot.frame.own.actor.channel!=p6::Channel::none||!snapshot.frame.own.actor.alive||snapshot.own.bag>0){fallback(snapshot);return;}
    auto own=best_solo(snapshot.frame);if(!own){fallback(snapshot);return;}
    if(!menu(snapshot,*own)){fallback(snapshot);return;}
    auto chosen=assay_guild::decide(*input_,*guild_,*ledger_,arm);if(chosen.option<0&&!chosen.private_income){fallback(snapshot);return;}
    const auto planned=snapshot;const auto& selected_offer=chosen.private_income?input_->private_income.value:input_->offers[chosen.option];
    const int action_group=selected_offer.plan.group;int site=-1;
    for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)site=s;
    if(site<0)return;snapshot=adapter_.observe(&routes_.sites[site].group,&execution_.work);
    if(!unchanged(planned,snapshot)){fault();return;}populate(snapshot);
    for(int i=0;i<input_->offer_count;++i)for(int c=0;c<input_->offers[i].safety_count;++c)input_->offers[i].safety[c].clock=snapshot.frame.clock;
    for(auto& role:input_->roles)if(role.offer>=0)role.role.next_safety.clock=snapshot.frame.clock;
    if(input_->proposal.knowledge==p6::Knowledge::exact)input_->proposal.clock=snapshot.frame.clock;
    if(input_->private_income.knowledge==p6::Knowledge::exact) {
        input_->private_income.clock=snapshot.frame.clock;
        for(int c=0;c<input_->private_income.value.safety_count;++c)input_->private_income.value.safety[c].clock=snapshot.frame.clock;
    }
    chosen=assay_guild::decide(*input_,*guild_,*ledger_,arm);if(chosen.option<0&&!chosen.private_income)return;
    auto& offered=chosen.private_income?input_->private_income.value:input_->offers[chosen.option];if(offered.plan.group!=action_group)return;
    std::array<MemberState,2> members{};
    if(chosen.role>=0&&(!current_members(snapshot,members)||!member_regime(snapshot)))return;
    auto local=adapter_.local(snapshot,offered.plan,{offered.safety.data(),std::size_t(offered.safety_count)},true,calm(snapshot),true,escape(snapshot));
    if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<20000){fault();return;}
    const auto admitted=assay_guild::commit(*input_,local,chosen,*guild_,*ledger_,arm,execution_);if(!admitted.started){fallback(snapshot);return;}
    selected_site_=renewal_site_=site;
    if(chosen.role>=0) {
        if(!members_frozen_){member_entry_=members;members_frozen_=true;}
        if(!collecting(execution_.frozen,snapshot.frame.clock.self))for(int s=0;s<int(routes_.sites.size());++s)
            if(group_id(routes_.sites[s].group)==execution_.frozen.next.group)renewal_site_=s;
    }
    initial_safety_=offered.safety;initial_safety_count_=offered.safety_count;threat_=threats(action_group);
    inspection_turns_=0;inspection_site_=-1;unavailable_logged_=false;
    std::cout<<"p8-10 ADMIT group="<<action_group<<" configuration="<<int(chosen.configuration)<<" assay="<<chosen.assay
        <<" episode="<<admitted.episode<<" private_income="<<chosen.private_income<<'\n';
    if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<18000){fault();return;}
    const auto command=assay_guild::advance(*input_,local,*guild_,*ledger_,execution_);
    dispatch(command,local);observed(snapshot,local);
}
inline void draft() {
    const auto own=cambc::draft::current_draft(cambc::team::ally);const int ordinal=int(own.size()),picks=int(cambc::draft::team_size());if(ordinal>=picks)return;
    DraftInputs in;in.ordinal=ordinal;in.picks=picks;in.prospective_actor=0;
    for(int i=0;i<ordinal&&i<5;++i)in.own_picks[i]=sdk::type(own[i]);in.settings=sdk::settings();in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
    const int window=std::min(120,in.settings.max_rounds);in.windows.deadlines=in.windows.windows={window,window,window};
    in.windows.sampled_actor_id=0;in.windows.sampled_round=0;in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto candidates=p8::native::draft_candidates(0,ordinal,false);const auto selected=assay_guild::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-10 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
    for(const auto champion:draft_priority(ordinal)){const auto pick=sdk::draft_type(champion);
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
                        std::printf("p8-10 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),entered,
                            static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                if(int(cambc::round_number())==entered)bot->turn();
            }
        }
        catch(const cambc::error& e){std::cout<<"p8-10 ERROR engine "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& e){std::cout<<"p8-10 ERROR exception "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-10 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::assay_guild::native
