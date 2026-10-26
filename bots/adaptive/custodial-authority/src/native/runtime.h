#pragma once
#include "configuration.h"
#include "../draft.h"
#include <bit>
#include <cstdio>
#include <iostream>
#include <memory>
namespace p8::custodial_authority::native {
using namespace p8::native;
class Runtime {
    bool diagnostic_routes_started_=false;
    Routes routes_;sdk::Adapter adapter_;Geometry geometry_;Execution execution_;
    std::unique_ptr<Inputs> input_=p8::native::make_default<Inputs>();std::unique_ptr<EvidenceLedger> ledger_;
    std::unique_ptr<Authority> authority_;p6::ScoreHistory scores_;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};int initial_safety_count_=0;
    int selected_site_=-1,renewal_site_=-1,last_round_=-1,component_=-1,full_=-1;
    bool contexts_=false,old_visited_former_=false,public_clear_seen_=false,public_block_seen_=false;
    bool own_appointment_=false,unavailable_logged_=false,first_income_=false;int public_block_round_=-1;
    std::array<int,2> pad_round_{-1,-1},pad_streak_{};
    int inspection_turns_=0,next_scan_=0,inspection_site_=-1,inspection_cursor_=0;
    Route inspection_{};std::uint64_t threat_=0;
    int role(int ordinal)const{return ordinal==0?0:ordinal>=3?ordinal:ordinal==geometry_.successor_ordinal?1:2;}
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

    bool clear_path(const Route& route,int target) const {
        if(!route_valid(route))return false;
        for(const auto& enemy:cambc::vision::champions(cambc::team::enemy))for(int i=0;i<route.count;++i)
            if(point(enemy.position())==route.tiles[i])return false;
        for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&group_id(camp.camp_group())!=target)
            for(int i=0;i<route.count;++i)if(point(camp.position())==route.tiles[i])return false;
        return true;
    }
    bool visible_route(const Route& route) const {
        if(!route_valid(route))return false;const auto visible=cambc::vision::visible_tiles();
        for(int i=0;i<route.count;++i)if(!visible.is_set(tile(route.tiles[i]))||cambc::map::bushes().is_set(tile(route.tiles[i])))return false;
        return true;
    }
    int bank_for(int site) const {
        int best=-1,rank=100000;
        for(int b=0;b<int(routes_.banks.size());++b) {
            const auto route=routes_.route(routes_.sites[site].capture.destination,routes_.banks[b].approach);
            if(route_valid(route)&&route.count<rank){rank=route.count;best=b;}
        }return best;
    }
    void geometry(const sdk::Snapshot& snapshot) {
        if(geometry_.ready||routes_.sites.size()<4||routes_.banks.empty()||snapshot.draft.picks<3)return;
        for(const auto& site:routes_.sites) {
            if(!site.approach.ready||!site.capture.ready)return;
            for(int i=0;i<site.rally_count;++i)if(!site.rally[i].ready)return;
        }
        for(const auto& bank:routes_.banks)if(!bank.approach.ready)return;
        if(geometry_.custody<0) {
            std::array<double,2> strength{};
            for(const auto& design:cambc::champion_designs)for(int i=0;i<2;++i)
                if(sdk::type(design.champion_name())==snapshot.draft.champions[i+1])strength[i]=design.base_stats().base_strength;
            geometry_.successor_ordinal=strength[1]>strength[0]?2:1;geometry_.helper_ordinal=geometry_.successor_ordinal==1?2:1;
            for(int s=0;s<int(routes_.sites.size());++s)if(routes_.sites[s].rally_count>=3){geometry_.custody=s;break;}
            if(geometry_.custody<0)return;const auto& site=routes_.sites[geometry_.custody];
            geometry_.bank=bank_for(geometry_.custody);if(geometry_.bank<0)return;
            for(int s=0;s<int(routes_.sites.size());++s)if(s!=geometry_.custody) {
                if(geometry_.former<0)geometry_.former=s;else if(geometry_.helper<0)geometry_.helper=s;else {geometry_.backup=s;break;}
            }
            if(geometry_.former<0||geometry_.helper<0||geometry_.backup<0)return;
            geometry_.former_bank=bank_for(geometry_.former);geometry_.helper_bank=bank_for(geometry_.helper);
            geometry_.backup_bank=bank_for(geometry_.backup);
            if(geometry_.former_bank<0||geometry_.helper_bank<0||geometry_.backup_bank<0)return;
            bool found=false;
            for(int y=-2;y<=2&&!found;++y)for(int x=-2;x<=2;++x) {
                const p6::Point p{site.approach.destination.x+x,site.approach.destination.y+y};
                if(distance(p,site.approach.destination)!=2||p.x<0||p.y<0||p.x>=cambc::map::width()||p.y>=cambc::map::height()||
                    routes_.terrain().is_set(tile(p))||site.group.spawn_tiles().is_set(tile(p))||cambc::map::bushes().is_set(tile(p)))continue;
                bool reserved=false;for(int r=0;r<3;++r)reserved|=p==site.rally[r].destination;
                for(const auto& source:routes_.sites)reserved|=source.group.spawn_tiles().is_set(tile(p));
                if(!reserved){geometry_.candidate_home.destination=p;found=true;break;}
            }
            if(!found)return;geometry_.home_chosen=true;
        }
        if(!geometry_.home_chosen)return;
        if(!geometry_.candidate_home.ready) {
            if(cambc::fuel_remaining()<90000)return;cambc::bitboard visited;
            geometry_.candidate_home.paths.compute(routes_.terrain(),singleton(geometry_.candidate_home.destination),visited,256);
            geometry_.candidate_home.ready=true;
        }
        const auto& site=routes_.sites[geometry_.custody];auto& spec=geometry_.meeting;
        spec.original_pad=site.rally[0].destination;spec.successor_pad=site.rally[1].destination;spec.helper_pad=site.rally[2].destination;spec.helper=snapshot.draft.actors[geometry_.helper_ordinal];
        // A public appointment cannot be confused with spawn recovery, an
        // ordinary camp hold, or the static inspection endpoint of another U.
        for(const auto pad:{spec.original_pad,spec.successor_pad,spec.helper_pad}) {
            if(cambc::map::spawn_region(cambc::team::ally).is_set(tile(pad)))return;
            for(const auto& source:routes_.sites)
                if(source.group.spawn_tiles().is_set(tile(pad))||source.approach.destination==pad)return;
        }
        spec.original=fixed_h(routes_,geometry_,snapshot.draft,false);spec.successor=fixed_h(routes_,geometry_,snapshot.draft,true);
        auto& former=spec.former_u;const auto& other=routes_.sites[geometry_.former];former.organization=Organization::U;
        former.group=group_id(other.group);former.collector=snapshot.draft.actors[0];former.crew[0]=former.collector;former.crew_count=1;
        former.altar=sdk::altar_id(routes_.banks[geometry_.former_bank].altar);former.ingress=routes_.route(spec.original_pad,other.approach);
        former.capture=routes_.route(other.approach.destination,other.capture);former.bank=routes_.route(other.capture.destination,routes_.banks[geometry_.former_bank].approach);
        former.renewal=routes_.route(routes_.banks[geometry_.former_bank].approach.destination,site.rally[0]);
        former.forecast_rounds=former.remaining_rounds=common::forecast_window;former.capture_window=cambc::constants::capture_time;
        former.gem_bearing=former.complete_verified=former.rally_legal=true;
        geometry_.ready=spec_valid(spec,snapshot.draft)&&route_valid(routes_.route(site.approach.destination,geometry_.candidate_home));
        if(geometry_.ready)std::cout<<"p8-12 GEOMETRY source="<<spec.original.group<<" successor="<<spec.successor.collector<<" former_source="<<former.group<<'\n';
    }
    Route returned(const Solo& model,int ordinal,bool appointment) const {
        const auto start=routes_.banks[model.bank].approach.destination;
        if(ordinal==0)return routes_.route(start,appointment?routes_.sites[geometry_.custody].rally[0]:routes_.sites[geometry_.custody].approach);
        if(ordinal==1)return appointment?routes_.route(start,routes_.sites[geometry_.custody].rally[1]):
            Routes::join(routes_.route(start,routes_.sites[model.site].approach),routes_.route(routes_.sites[model.site].approach.destination,geometry_.candidate_home));
        return routes_.route(start,routes_.sites[geometry_.custody].rally[2]);
    }
    std::unique_ptr<Solo> model(const p6::Frame& frame,int site,int bank,int ordinal,bool appointment=false,bool bootstrap=false) const {
        auto out=std::make_unique<Solo>(solo(routes_,site,bank,frame,false));if(!out->feasible)return {};
        if(geometry_.ready&&ordinal<3&&!bootstrap) {
            const auto renewal=returned(*out,ordinal,appointment);if(!route_valid(renewal))return {};
            const int delta=walking(renewal)-walking(out->plan.renewal);auto& actor=out->model.schedule.actors[0];
            actor.occupied_turns.value+=delta;for(int e=0;e<actor.event_count;++e)if(actor.events[e].kind==p6::EventKind::renewal)actor.events[e].tick+=delta;
            for(auto* exposure:{&out->model.certificate.clear,&out->model.certificate.hold,&out->model.certificate.bank,&out->model.certificate.recovery})exposure->incoming_total+=out->ambient_incoming*std::max(0,delta);
            out->plan.renewal=renewal;out->plan.forecast_rounds=int(actor.occupied_turns.value);
        }
        if(!clear_path(out->plan.ingress,out->plan.group)||!clear_path(out->plan.bank,out->plan.group)||!clear_path(out->plan.renewal,out->plan.group)||
            out->plan.forecast_rounds>std::min(common::forecast_window,int(cambc::constants::max_rounds)-frame.clock.round)||
            !out->model.certificate.valid(frame,frame.clock.self,out->plan.group))return {};
        return out;
    }
    std::unique_ptr<Solo> best_solo(const p6::Frame& frame,int fixed=-1,int bank=-1,bool appointment=false) const {
        std::unique_ptr<Solo> best;const int ordinal=role(frame.own.actor.pick_ordinal);
        for(int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>55000;++s) {
            if(fixed>=0&&s!=fixed)continue;
            if(geometry_.ready&&ordinal<3&&first_income_) {
                if(ordinal==1&&s!=geometry_.custody&&s!=geometry_.backup)continue;
                if(ordinal==2&&s!=geometry_.helper)continue;
                if(ordinal==0&&s!=geometry_.former)continue;
                if(ordinal==1&&s==geometry_.custody&&authority_&&!authority_->transferred) {
                    const auto* proposer=frame.visible(input_->draft.actors[0]);
                    if(proposer&&proposer->position==geometry_.meeting.original_pad&&(!old_visited_former_||arm==Arm::control))continue;
                }
            }
            for(int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>55000;++b) {
                if(bank>=0&&b!=bank)continue;auto candidate=model(frame,s,b,ordinal,appointment,!first_income_);if(!candidate)continue;
                if(!best||candidate->plan.forecast_rounds<best->plan.forecast_rounds||
                    (candidate->plan.forecast_rounds==best->plan.forecast_rounds&&candidate->plan.group<best->plan.group))best=std::move(candidate);
            }
        }return best;
    }
    void populate(const sdk::Snapshot& snapshot) {
        auto& in=*input_;in.frame=snapshot.frame;
        if(in.settings_identity.source!=p6::Source::queried_setting) {
            in.settings=sdk::settings();
            in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
        }
        in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        in.threat_regime={int(threats(geometry_.ready?group_id(routes_.sites[geometry_.custody].group):-1)&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        const auto enemy=cambc::global_submitted_gems(cambc::team::enemy);scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy);
        in.component_slot=component_;in.supported_slot=full_;in.component_entry=in.supported_entry={};
        if(const auto* slot=ledger_->state(component_))in.component_entry=slot->declaration.key;
        if(const auto* slot=ledger_->state(full_))in.supported_entry=slot->declaration.key;
        if(geometry_.ready){in.meeting=geometry_.meeting;in.target_group=in.meeting.original.group;}
        else in.target_group=in.offer_count>0?in.offers[0].plan.group:0;
    }
    void access(const sdk::Snapshot& snapshot) {
        auto& in=*input_;in.own_access={};if(!geometry_.ready)return;
        const auto& plan=geometry_.meeting.original;const auto live=group(routes_.sites[geometry_.custody],snapshot.frame.clock);
        Access observed;observed.group=plan.group;observed.retained_return=plan_return_hash(plan);
        if(live.all_spawns_visible)observed.region_visible={true,p6::Source::self_observed};
        const auto altar=routes_.banks[geometry_.bank].altar;bool all=cambc::vision::visible_tiles().is_set(tile(altar)),locked=false,blocked=false;
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))locked|=point(lock.position())==altar;
        for(Leg leg:{Leg::bank,Leg::renewal}) {
            const auto& route=route_for(plan,leg);all&=visible_route(route);
            for(const auto& enemy:cambc::vision::champions(cambc::team::enemy))for(int i=0;i<route.count;++i)
                if(point(enemy.position())==route.tiles[i]){blocked=true;observed.blocked_tile=route.tiles[i];}
            for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&group_id(camp.camp_group())!=plan.group)
                for(int i=0;i<route.count;++i)if(point(camp.position())==route.tiles[i]){blocked=true;observed.blocked_tile=route.tiles[i];}
        }
        if(blocked){observed.return_executable={false,p6::Source::self_observed};observed.blocker_visible={true,p6::Source::self_observed};}
        else if(locked){observed.return_executable={false,p6::Source::self_observed};observed.blocker_visible={false,p6::Source::self_observed};}
        else if(all){observed.return_executable={true,p6::Source::self_observed};observed.blocker_visible={false,p6::Source::self_observed};}
        if(live.all_spawns_visible&&all&&!blocked&&!locked)public_clear_seen_=true;
        if(public_clear_seen_&&blocked){public_block_seen_=true;if(public_block_round_<0)public_block_round_=snapshot.frame.clock.round;}
        const auto* old=snapshot.frame.visible(snapshot.draft.actors[0]);
        if(public_block_seen_&&old&&old->bag>0&&snapshot.frame.clock.round>public_block_round_&&
            routes_.sites[geometry_.former].group.spawn_tiles().is_set(tile(old->position)))old_visited_former_=true;
        if(snapshot.frame.own.actor.pick_ordinal==0)in.own_access={observed,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        if(geometry_.ready)custodial_authority::refresh(in,*authority_,*ledger_);
    }
    void declare(const sdk::Snapshot& snapshot) {
        if(contexts_||!geometry_.ready||ledger_->active()||snapshot.own.bag)return;
        const int ordinal=role(snapshot.frame.own.actor.pick_ordinal);
        if(ordinal==0)full_=ledger_->predeclare(declaration(geometry_.meeting.original,snapshot,*input_,true,routes_.sites[geometry_.custody].group.gem_value()));
        else if(ordinal==1) {
            // The component's bank and normal home exactly match the new H.
            // Only its productive ingress and membership are independent.
            auto frame=snapshot.frame;frame.own.actor.position=geometry_.candidate_home.destination;
            auto component=model(frame,geometry_.custody,geometry_.bank,ordinal);
            if(!component)return;
            component_=ledger_->predeclare(declaration(component->plan,snapshot,*input_,false,routes_.sites[geometry_.custody].group.gem_value()));
            full_=ledger_->predeclare(declaration(geometry_.meeting.successor,snapshot,*input_,true,routes_.sites[geometry_.custody].group.gem_value()));
        }
        contexts_=true;populate(snapshot);
    }
    void observe_pads(const sdk::Snapshot& snapshot) {
        if(!geometry_.ready)return;const auto& spec=geometry_.meeting;
        const auto* old=snapshot.frame.visible(spec.original.collector);const auto* next=snapshot.frame.visible(spec.successor.collector);
        const auto available=[](const p6::Actor* actor,p6::Point pad){return actor&&actor->alive&&actor->can_move&&actor->can_attack&&actor->bag==0&&actor->channel==p6::Channel::none&&actor->position==pad;};
        for(int i=0;i<2;++i) {
            const bool at=available(i==0?old:next,i==0?spec.original_pad:spec.successor_pad);const int round=snapshot.frame.clock.round;
            if(round!=pad_round_[i]){pad_streak_[i]=at?(pad_round_[i]==round-1?std::min(2,pad_streak_[i]+1):1):0;pad_round_[i]=round;}
            else if(!at)pad_streak_[i]=0;
        }
    }
    void signals(const sdk::Snapshot& snapshot) {
        auto& in=*input_;in.original_offer=in.successor_offer={};in.collector_proposal={};in.meeting_safety={};
        if(!geometry_.ready)return;observe_pads(snapshot);const auto& spec=geometry_.meeting;
        const auto* old=snapshot.frame.visible(spec.original.collector);const auto* next=snapshot.frame.visible(spec.successor.collector);
        const auto available=[](const p6::Actor* actor,p6::Point pad){return actor&&actor->alive&&actor->can_move&&actor->can_attack&&actor->bag==0&&actor->channel==p6::Channel::none&&actor->position==pad;};
        const bool own_old=snapshot.frame.clock.self==spec.original.collector;
        const bool own_next=snapshot.frame.clock.self==spec.successor.collector;
        const auto custody=group(routes_.sites[geometry_.custody],snapshot.frame.clock);
        const bool between_captures=custody.all_spawns_visible&&custody.lifecycle==p6::Lifecycle::respawning;
        // A peer's pad is a public offer convention, never its private proof.
        // The old convention also requires personally witnessed post-blocker
        // productive departure, excluding an ongoing pre-loss H at this pad.
        const bool old_offer=between_captures&&available(old,spec.original_pad)&&(own_old?authority_->lost&&own_appointment_:old_visited_former_&&pad_streak_[0]>=2);
        const bool next_offer=available(next,spec.successor_pad)&&(own_next?own_appointment_&&relevant_component(in,*ledger_):pad_streak_[1]>=2);
        const Signal signal{meeting_hash(spec),spec.original.collector,spec.successor.collector,spec.original.group};
        if(old_offer)in.original_offer={signal,snapshot.frame.clock,spec.original.collector,p6::Knowledge::exact};
        if(next_offer)in.successor_offer={signal,snapshot.frame.clock,spec.successor.collector,p6::Knowledge::exact};
        if(old) {
            auto safe=model(subject(snapshot.frame,*old),geometry_.former,geometry_.former_bank,0,true);
            if(safe)in.meeting_safety[0]=safe->model.certificate;
        }
        if(next) {
            auto safe=model(subject(snapshot.frame,*next),geometry_.custody,geometry_.bank,1);
            if(safe)in.meeting_safety[1]=safe->model.certificate;
        }
        const int collector=arm==Arm::active&&authority_->transferred?spec.successor.collector:spec.original.collector;
        const auto* proposer=snapshot.frame.visible(collector);const auto pad=collector==spec.original.collector?spec.original_pad:spec.successor_pad;
        if(available(proposer,pad))in.collector_proposal={spec.original.group,snapshot.frame.clock,collector,p6::Knowledge::exact};
    }
    std::vector<Solo> alternatives(const sdk::Snapshot& snapshot,const Solo& own) const {
        std::vector<Solo> out;out.reserve(3);out.push_back(own);normalize(out.back(),snapshot.frame);
        for(int ordinal=0;ordinal<3;++ordinal) {
            const auto* actor=snapshot.frame.visible(snapshot.draft.actors[ordinal]);
            if(!actor||!actor->alive||actor->bag||actor->channel!=p6::Channel::none)return {};
            if(actor->id==snapshot.frame.clock.self)continue;
            const int duty=role(ordinal);
            const bool initial=arm==Arm::control||!authority_->transferred;
            const int site=duty==0?geometry_.former:duty==1?(initial?geometry_.backup:geometry_.custody):geometry_.helper;
            const int bank=duty==0?geometry_.former_bank:duty==1?(initial?geometry_.backup_bank:geometry_.bank):geometry_.helper_bank;
            if(duty==1&&initial&&actor->position!=routes_.sites[geometry_.backup].approach.destination)return {};
            auto baseline=model(subject(snapshot.frame,*actor),site,bank,duty,duty==0&&authority_->transferred);
            if(!baseline)return {};normalize(*baseline,snapshot.frame);out.push_back(std::move(*baseline));
        }
        for(std::size_t i=0;i<out.size();++i)for(std::size_t j=0;j<i;++j)if(out[i].plan.group==out[j].plan.group)return {};
        return out;
    }
    bool menu(const sdk::Snapshot& snapshot,const Solo& normal) {
        auto& in=*input_;in.offer_count=1;in.independent=0;in.private_income={};
        if(!independent_offer(in.offers[0],normal,{&normal,1},snapshot.frame))return false;
        if(!geometry_.ready)in.target_group=normal.plan.group;
        const int ordinal=role(snapshot.frame.own.actor.pick_ordinal);
        if(ordinal==1&&component_>=0) {in.offers[0].context_slot=component_;in.offers[0].entry=in.component_entry;}
        if(!geometry_.ready||!first_income_)return true;
        // Both home and appointment loops remain represented. The reserved
        // appointment return receives no information or coordination bonus.
        if(arm==Arm::active&&((ordinal==0&&authority_->lost)||(ordinal==1&&relevant_component(in,*ledger_)))) {
            for(int option=0;option<(ordinal==1?2:1);++option) {
                const int site=ordinal==0?geometry_.former:option==0?geometry_.custody:geometry_.backup;
                const int bank=ordinal==0?geometry_.former_bank:option==0?geometry_.bank:geometry_.backup_bank;
                auto appointment=model(snapshot.frame,site,bank,ordinal,true);
                if(appointment&&independent_offer(in.offers[in.offer_count],*appointment,{appointment.get(),1},snapshot.frame))++in.offer_count;
            }
        }
        if(ordinal>2)return true;
        auto models=alternatives(snapshot,normal);if(models.size()!=3)return true;
        const bool successor=arm==Arm::active&&authority_->transferred;const int collector=snapshot.draft.actors[successor?geometry_.successor_ordinal:0];
        if(snapshot.frame.clock.self!=collector&&ordinal!=2)return true;
        auto coalition=supported(routes_,geometry_,snapshot.frame,snapshot.draft,models,successor);
        if(!coalition->feasible)return true;
        // Rebuild every U alternative with exactly the same three affected
        // actors; each keeps its own source, purse, bag and queue service.
        for(int o=0;o<in.offer_count;++o) {
            auto self=std::make_unique<Solo>(normal);
            if(o>0) {
                const bool backup=in.offers[o].plan.group==group_id(routes_.sites[geometry_.backup].group);
                self=model(snapshot.frame,ordinal==0?geometry_.former:backup?geometry_.backup:geometry_.custody,
                    ordinal==0?geometry_.former_bank:backup?geometry_.backup_bank:geometry_.bank,ordinal,true);
            }
            if(!self)return false;auto compared=models;
            for(auto& prior:compared)if(prior.model.schedule.actors[0].actor_id==snapshot.frame.clock.self)prior=*self;
            if(!independent_offer(in.offers[o],*self,compared,snapshot.frame))return false;
            if(o==0&&ordinal==1){in.offers[o].context_slot=component_;in.offers[o].entry=in.component_entry;}
        }
        auto& offered=in.offers[in.offer_count];offered=coalition->offer;
        if(snapshot.frame.clock.self==collector){offered.context_slot=full_;offered.entry=in.supported_entry;}
        ++in.offer_count;return true;
    }
    bool unchanged(const sdk::Snapshot& before,const sdk::Snapshot& after) const {
        if(!after.valid||before.frame.clock.round!=after.frame.clock.round||before.frame.clock.self!=after.frame.clock.self||
            before.own.equipment_hash!=after.own.equipment_hash||before.own.health!=after.own.health||before.own.max_health!=after.own.max_health||
            before.own.strength!=after.own.strength||before.own.focus!=after.own.focus||before.own.growth!=after.own.growth||before.own.bag!=after.own.bag||
            before.frame.own.gold!=after.frame.own.gold||before.frame.own.actor.position!=after.frame.own.actor.position)return false;
        for(int o=0;o<input_->offer_count;++o) {
          for(int a=0;a<input_->offers[o].schedule.actor_count;++a) {
            const int actor=input_->offers[o].schedule.actors[a].actor_id;const auto* x=before.frame.visible(actor);const auto* y=after.frame.visible(actor);
            if(!x||!y||x->position!=y->position||x->health!=y->health||x->max_health!=y->max_health||x->strength!=y->strength||
                x->bag!=y->bag||x->channel!=y->channel||x->alive!=y->alive)return false;
          }
        }
        return true;
    }
    Decision decision() {
        if(geometry_.ready)return custodial_authority::decide(*input_,*authority_,*ledger_,arm);
        Decision out;out.clock=input_->frame.clock;
        if(common::valid_inputs(*input_)&&common::legal_offer(*input_,input_->offers[0])==Rejection::none){out.option=0;out.organization=Organization::U;out.reason=Rejection::none;}
        return out;
    }
    Admission admit(const sdk::Snapshot& snapshot,const LocalFrame& local,const Decision& chosen) {
        Admission out;if(chosen.option<0||execution_.work.active)return out;auto& offer=input_->offers[chosen.option];
        if(!physical_admissible(offer.plan,local))return out;
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000||
            (offer.plan.organization==Organization::H&&snapshot.frame.clock.round+offer.plan.forecast_rounds>int(cambc::constants::max_rounds)))return out;
        if(chosen.record_episode) {
            auto* slot=ledger_->state(offer.context_slot);if(!slot)return out;Premises premises{offer.entry,snapshot.own,all_premises};
            const int own=p6::actor_index(offer.schedule,snapshot.frame.clock.self);if(own<0)return out;
            const auto nominal=offer.schedule.actors[own].occupied_turns.value;
            if(nominal<1||nominal>common::forecast_window)return out;
            if(offer.plan.organization==Organization::H) {
                if(snapshot.frame.clock.round+offer.plan.forecast_rounds>int(cambc::constants::max_rounds))return out;
                if(snapshot.frame.clock.self==input_->meeting.successor.collector) {
                    const auto* component=ledger_->state(component_);
                    if(!component||!relevant_component(*input_,*ledger_)||snapshot.frame.clock.round+offer.plan.forecast_rounds-component->latest_positive>=qualification_rounds)return out;
                }
                if(slot->campaign_episodes>0&&slot->latest_positive>=0&&snapshot.frame.clock.round+offer.plan.forecast_rounds-slot->latest_positive>=qualification_rounds)return out;
                if(!slot->campaign_started) {
                    const bool started=snapshot.frame.clock.self==input_->meeting.original.collector?
                        ledger_->begin_initial_campaign(full_,snapshot.frame.clock.round,{premises,true,true,true}):
                        ledger_->begin_organization_campaign(component_,full_,snapshot.frame.clock.round,{premises,true,true,true});
                    if(!started)return out;
                }
            }
            EpisodeRequest request;request.context=offer.context_slot;request.issue_round=snapshot.frame.clock.round;
            request.deadline=request.issue_round+std::min(common::forecast_window,offer.plan.remaining_rounds);request.nominal_duration=int(nominal);
            request.action=int(offer.plan.organization);request.premises=premises;out.episode=ledger_->issue(request);if(!out.episode)return out;execution_.issued=offer.entry;
        }
        out.started=execution_.work.start(offer.plan,local);out.rejection=out.started?Rejection::none:Rejection::unsafe;
        if(!out.started&&out.episode)ledger_->invalidate_active(snapshot.frame.clock.round);
        if(out.started&&out.episode&&!execution_.trace.begin(*ledger_,execution_.work)){ledger_->invalidate_active(snapshot.frame.clock.round);execution_.work.request_stop();}
        return out;
    }
    void observed(const sdk::Snapshot& snapshot,const LocalFrame& local) {
        execution_.trace.observe(local,execution_.work,snapshot.spawn_shop_legal);
        if(!execution_.work.active) {
            const auto& work=execution_.work;const int ordinal=role(snapshot.frame.own.actor.pick_ordinal);
            if(work.cycle_completed&&work.payout_seen&&work.bank_seen&&work.renewal_seen&&work.route_witness_active)first_income_=true;
            if(geometry_.ready&&ordinal<2&&work.cycle_completed&&work.payout_seen&&work.bank_seen&&work.renewal_seen&&work.route_witness_active&&
                snapshot.frame.own.actor.position==(ordinal==0?geometry_.meeting.original_pad:geometry_.meeting.successor_pad)&&
                (ordinal==0?authority_->lost&&work.plan.group==geometry_.meeting.former_u.group:relevant_component(*input_,*ledger_)))own_appointment_=true;
        }
        if(!execution_.trace.active||(execution_.work.active&&ledger_->active()))return;
        auto terminal=Premises{execution_.issued,snapshot.own,all_premises};
        if(!execution_.work.route_witness_active)terminal.personally_observed&=~route_known;
        for(int i=0;i<terminal.key.participant_count;++i)if(!snapshot.frame.visible(terminal.key.participants[i]))terminal.personally_observed&=~membership_known;
        const auto result=execution_.trace.finish(*ledger_,snapshot.frame.clock.round,terminal);
        if(result.accepted)std::cout<<"p8-12 EPISODE full_positive="<<result.full_positive<<" kind="<<int(execution_.work.plan.organization)<<'\n';
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
            std::printf("p8-12 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                startup_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
            diagnostic_routes_started_=true;
        }
        if(!startup_room(70000))return;
        const bool complete=routes_.initialize_step(1);
        if(complete&&startup_room(32000))
            std::printf("p8-12 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                startup_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
        return;
    }
    if(!execution_.work.active&&startup_room(70000))routes_.prepare();
    if(!startup_room(30000))return;
    auto snapshot=adapter_.observe(selected_site_<0?nullptr:&routes_.sites[selected_site_].group,&execution_.work);
    if(!snapshot.valid){fault();return;}
    if(!ledger_){ledger_=std::make_unique<EvidenceLedger>(snapshot.frame.clock.self);authority_=std::make_unique<Authority>(snapshot.frame.clock.self);}
    if(snapshot.frame.clock.round==last_round_)return;const bool skipped=last_round_>=0&&snapshot.frame.clock.round!=last_round_+1;last_round_=snapshot.frame.clock.round;
    if(!execution_.work.active)geometry(snapshot);populate(snapshot);access(snapshot);observe_pads(snapshot);
    if(execution_.work.active) {
        auto certificates=current_safety(snapshot);bool safe=false;const bool changed=threats(execution_.work.plan.group)!=threat_;
        if(changed) {
            // A new threat ends dependent evidence. A fresh whole-remainder
            // ambient bound may still prove an existing hold/channel/bag safe.
            const double stress=ambient()*std::max(1,execution_.work.plan.forecast_rounds);
            for(int c=0;c<initial_safety_count_;++c)for(auto* exposure:{&certificates[c].clear,&certificates[c].hold,&certificates[c].bank,&certificates[c].recovery})exposure->incoming_total+=stress;
            fault();
        }
        for(int c=0;c<initial_safety_count_;++c)if(certificates[c].actor==snapshot.frame.clock.self)safe=certificates[c].valid(snapshot.frame,snapshot.frame.clock.self,execution_.work.plan.group);
        auto local=adapter_.local(snapshot,execution_.work.plan,{certificates.data(),std::size_t(initial_safety_count_)},safe,calm(snapshot),productive(snapshot),escape(snapshot),&execution_.work);
        local.fuel_resumed|=skipped;if(skipped)fault();
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<12000){fault();return;}
        dispatch(execution_.work.step(local),local);observed(snapshot,local);return;
    }
    if(snapshot.own.bag||!snapshot.frame.own.actor.alive||snapshot.frame.own.actor.channel!=p6::Channel::none){fallback(snapshot);return;}
    declare(snapshot);signals(snapshot);
    if(geometry_.ready&&int(cambc::round_number())==snapshot.frame.clock.round&&cambc::fuel_remaining()>=25000&&
        commit_meeting(*input_,*authority_,*ledger_,arm,execution_.work))std::cout<<"p8-12 TRANSFER own="<<snapshot.frame.clock.self<<" collector="<<authority_->collector<<'\n';
    auto own=best_solo(snapshot.frame,-1,-1,arm==Arm::active&&authority_->transferred&&snapshot.frame.own.actor.pick_ordinal==0);
    if(!own){fallback(snapshot);return;}
    populate(snapshot);access(snapshot);signals(snapshot);if(!menu(snapshot,*own)){fallback(snapshot);return;}
    if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000){fault();return;}
    auto chosen=decision();if(chosen.option<0){fallback(snapshot);return;}const auto planned=snapshot;
    const int action_group=input_->offers[chosen.option].plan.group;int site=-1;
    for(int s=0;s<int(routes_.sites.size());++s)if(group_id(routes_.sites[s].group)==action_group)site=s;
    if(site<0)return;snapshot=adapter_.observe(&routes_.sites[site].group,&execution_.work);
    if(!unchanged(planned,snapshot)){fault();return;}populate(snapshot);access(snapshot);signals(snapshot);
    for(int o=0;o<input_->offer_count;++o)for(int c=0;c<input_->offers[o].safety_count;++c)input_->offers[o].safety[c].clock=snapshot.frame.clock;
    if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000){fault();return;}
    chosen=decision();if(chosen.option<0||input_->offers[chosen.option].plan.group!=action_group)return;auto& offer=input_->offers[chosen.option];
    auto local=adapter_.local(snapshot,offer.plan,{offer.safety.data(),std::size_t(offer.safety_count)},true,calm(snapshot),true,escape(snapshot));
    if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<25000){fault();return;}
    const auto admitted=admit(snapshot,local,chosen);if(!admitted.started){fallback(snapshot);return;}
    selected_site_=renewal_site_=site;initial_safety_=offer.safety;initial_safety_count_=offer.safety_count;threat_=threats(action_group);
    own_appointment_=false;inspection_turns_=0;inspection_site_=-1;unavailable_logged_=false;
    std::cout<<"p8-12 ADMIT group="<<action_group<<" organization="<<int(offer.plan.organization)<<" episode="<<admitted.episode<<" lost="<<authority_->lost<<" transferred="<<authority_->transferred<<'\n';
    dispatch(execution_.work.step(local),local);observed(snapshot,local);
}
inline void draft() {
    const auto own=cambc::draft::current_draft(cambc::team::ally);const int ordinal=int(own.size()),picks=int(cambc::draft::team_size());if(ordinal>=picks)return;
    DraftInputs in;in.ordinal=ordinal;in.picks=picks;in.prospective_actor=0;
    for(int i=0;i<ordinal&&i<5;++i)in.own_picks[i]=sdk::type(own[i]);in.settings=sdk::settings();in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
    const int window=std::min(common::forecast_window,in.settings.max_rounds);in.windows.deadlines=in.windows.windows={window,window,window};
    in.windows.sampled_actor_id=0;in.windows.sampled_round=0;in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto candidates=p8::native::draft_candidates(0,ordinal,false);const auto selected=custodial_authority::select_draft(in,candidates);
    if(selected.candidate>=0){cambc::draft::pick(sdk::draft_type(selected.champion));return;}
    std::cout<<"p8-12 OPPORTUNITY_UNAVAILABLE draft ordinal="<<ordinal<<'\n';
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
                        std::printf("p8-12 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                            construction_round,static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                bot->turn();
            }
        }
        catch(const cambc::error& e){std::cout<<"p8-12 ERROR engine "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(const std::exception& e){std::cout<<"p8-12 ERROR exception "<<e.what()<<'\n';try{if(bot)bot->fault();}catch(...) {}}
        catch(...){std::cout<<"p8-12 ERROR unknown\n";try{if(bot)bot->fault();}catch(...) {}}
        cambc::end_turn();
    }
}
} // namespace p8::custodial_authority::native

