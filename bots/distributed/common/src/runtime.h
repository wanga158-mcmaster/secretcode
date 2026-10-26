#pragma once
#include <cambc.h>
#include "organization.h"
#include "runtime_joint.h"
#include "runtime_combat.h"
#include "runtime_economy.h"
#include "runtime_intercept.h"
#include "runtime_trace.h"
#include "runtime_observation.h"
#include "runtime_catalogue.h"
#include "runtime_opportunities.h"
#include "../../reference/src/policy.h"
#include <cstring>
#include <cstdio>
#include <iostream>
#include <memory>
#include <vector>

// Ablation arm of every bot: 1 passes control=true to Policy::choose and disables
// the runtime's own active-only paths. Build it with the knob override P6_CONTROL=1.
#ifndef P6_CONTROL
#define P6_CONTROL 0
#endif
// Bounded native probe for missing cooperative prerequisites. This records
// only the current actor's personally observed frame and grants no permission.
#ifndef P6_JOINT_PREREQUISITES
#define P6_JOINT_PREREQUISITES 0
#endif
#ifndef P6_JOINT_PREPARE
#define P6_JOINT_PREPARE 0
#endif
#ifndef P6_REPLICAS_CALLER_PACKET
#define P6_REPLICAS_CALLER_PACKET 0
#endif
// Only refuse a walking inspection after a complete public-terrain component
// proves its approach disconnected. This changes no quote or organization.
#ifndef P6_WALKING_INSPECTION_REPAIR
#define P6_WALKING_INSPECTION_REPAIR 0
#endif
#ifndef P6_INSPECTION_CONTINUATION_REPAIR
#define P6_INSPECTION_CONTINUATION_REPAIR 0
#endif
#ifndef P6_CAPITAL_CALLER_PACKET
#define P6_CAPITAL_CALLER_PACKET 0
#endif
#ifndef P6_IDLE_RENEWAL_REPAIR
#define P6_IDLE_RENEWAL_REPAIR 0
#endif
#if P6_CAPITAL_CALLER_PACKET
#include <bit>
#endif
#if P6_WALKING_INSPECTION_REPAIR
#include "runtime_routes.h"
#endif

namespace p6::runtime {
using cambc::vec2;
using cambc::bitboard;
inline vec2 native(Point p) { return {p.x,p.y}; }
inline Point point(vec2 p) { return {p.x,p.y}; }
inline int distance(Point a,Point b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
inline bitboard tile(Point p) { bitboard b; b.set(native(p)); return b; }
inline bitboard around(Point p,int n) { bitboard b; b.draw_chebyshev(native(p),n); return b; }
template<class T> Observation<T> seen(T value,ViewClock clock,int id) {
    return {value,clock,id,Knowledge::exact};
}
inline Champion type(cambc::champion_type c) {
    switch(c) {
#define P6_TYPE(name) case cambc::champion_type::name: return Champion::name
    P6_TYPE(marcella); P6_TYPE(tavi); P6_TYPE(varg); P6_TYPE(vendetta);
    P6_TYPE(willow); P6_TYPE(prism); P6_TYPE(ren); P6_TYPE(cassi); P6_TYPE(issac);
    P6_TYPE(vesper_vale); P6_TYPE(ryth); P6_TYPE(stellar); P6_TYPE(heart_of_ruin);
#undef P6_TYPE
    default:return Champion::other;
    }
}
inline cambc::champion_draft draft_type(Champion c) {
    switch(c) {
#define P6_PICK(name) case Champion::name:return cambc::champion_draft::name
    P6_PICK(marcella); P6_PICK(tavi); P6_PICK(varg); P6_PICK(vendetta);
    P6_PICK(willow); P6_PICK(prism); P6_PICK(ren); P6_PICK(vesper_vale);
    P6_PICK(ryth); P6_PICK(stellar); P6_PICK(heart_of_ruin);
#undef P6_PICK
    default:return cambc::champion_draft::marcella;
    }
}
inline int identity(const cambc::champion& a) {
    return static_cast<int>(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(a)));
}
inline int identity(const cambc::camp& a) {
    return static_cast<int>(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(a)));
}
struct StaticSite {
    struct Member { int id = -1; Point spawn{}; MonsterWork full{}; };
    cambc::map::camp_group source;
    Group geometry{};
    runtime_catalogue::Site work{};
    bitboard spawns;
    cambc::distance_field paths;
    bool paths_ready=false;
    std::vector<Member> members;
    explicit StaticSite(cambc::map::camp_group group):source(group),spawns(group.spawn_tiles()) {}
};
struct Quote {
    const forecast::Request* request=nullptr;
    const forecast::EvaluatedQuote* evaluated=nullptr;
    bool available=false;
};

// Each instance belongs to one authenticated actor. No live shared bytes, ally
// private state, replay state, cached live handles or remote commands are used.
template<class Policy> class Bot {
    Policy policy_{};
    std::vector<StaticSite> sites_;
    struct InitialSite { cambc::map::camp_group source; double key=0; Point spawn{}; int id=-1;
        std::int64_t gold=0,gems=0; double health=0,strength=0; int members=0;
        std::vector<Point> spawns{}; };
    std::vector<InitialSite> initial_sites_;
    runtime_catalogue::Order initial_order_{};
    Point initial_origin_{};
    std::size_t initial_key_=0,initial_member_=0;
    double initial_health_=0,initial_strength_=0;
    int preferred_site_=0,proposed_site_=-1,picks_=0;
    bool initial_started_=false,initial_sorted_=false,initial_quote_checked_=false;
#if P6_JOINT_PREPARE
    int joint_initial_site_=-1;
    bool joint_storage_checked_=false;
#endif
    bool verification_eligible_=false,verification_paths_ready_=false;
    int verification_initial_=-1,verification_site_=-1;
    double verification_key_=std::numeric_limits<double>::infinity();
    Point verification_origin_{};
    std::array<int,3> verification_ranges_{};
    std::array<Point,3> verification_pads_{};
    std::unique_ptr<cambc::distance_field> verification_paths_;
    mutable std::uint8_t verification_mask_=0;
    bitboard terrain_,camp_spawns_;
    Point bank_{},altar_{};
    cambc::distance_field bank_paths_;
    bool initialized_=false,bank_ready_=false,bank_exists_=false,recovering_=false;
#if P6_WALKING_INSPECTION_REPAIR
    runtime_routes::WalkingComponent walking_component_;
#endif
    bool shop_pending_=false;
    ActorState execution_{};
    ScoreHistory history_{};
    organization::Input input_{};
    std::array<Quote,8> quotes_{};
    std::array<bool,8> quote_deferred_{};
    std::array<bool,8> site_visited_{};
    std::array<std::unique_ptr<forecast::EvaluatedQuote>,8> evaluated_{};
    std::array<int,8> admission_gate_{};
    std::array<bool,8> turnback_logged_{};
    runtime_opportunities::History opportunities_;
    std::array<runtime_opportunities::Candidate,8> opportunities_now_{};
    int requested_group_=-1;
    std::unique_ptr<forecast::EvaluatedQuote> delivery_;
    runtime_joint::Models models_{};
    runtime_joint::Requests requests_{};
    std::unique_ptr<forecast::EvaluatedCrewQuote> crew_quote_;
    int crew_site_=-1;
    bool crew_current_=false;
    std::array<std::uint8_t,8> crew_logged_{};
    runtime_economy::Selection economy_{};
    bool bank_preferred_=false;
    Observation<double> target_ehp_{};
    double observed_incoming_=0;
    std::vector<cambc::camp> camps_;
    std::vector<cambc::camp> observed_camp_sources_;
    runtime_observation::CampSnapshot camp_view_;
    mutable std::array<std::optional<cambc::camp_stats>,6> camp_designs_{};
    mutable std::optional<bitboard> observed_self_attack_;
    std::vector<runtime_observation::AttackTarget> attack_targets_;
    struct EnemyObservation { int id = -1; Point position{}; double strength = 0, stress = 0; };
    std::vector<EnemyObservation> observed_enemies_;
    runtime_threat::Prepared immediate_{};
    std::vector<cambc::champion> allies_,enemies_;
    int revision_=0,inspections_=0,last_inspection_group_=-1,successful_attack_group_=-1;
    int successful_attack_round_=-2;
    struct FuelTrace {
        int round=-1;
#if P6_REPLICAS_CALLER_PACKET
        bool caller_pending=false,caller_policy=false;
        int caller_code=0,caller_intent=-1,caller_group=-1;
        bool caller_recovering=false,caller_shop=false;
        std::uint32_t caller_reason=0;
        std::array<std::int64_t,52> caller_packet{};
#endif
        std::array<std::uint64_t,7> values{};
        std::array<std::uint64_t,5> costs{};
        int builds=0;
        std::uint64_t joint_before=0;
        std::uint64_t joint_previous=0,joint_max_gap=0;
        int joint_checks=0,joint_denials=0,sites_visited=0,sites_cutoff=0;
#if P6_JOINT_PREREQUISITES
        std::array<std::int64_t,18> joint_packet{};
        int joint_packets=0,joint_packet_round=-32,optional_stop=0;
        bool joint_pending=false;
#endif
        std::array<std::int64_t,23> turnback{};
        bool turnback_pending=false;
        const char* reason=nullptr;
        int group=-1;
        const char* reported_reason=nullptr;
        int reported_group=-1;
        struct Heavy {
            bool pending=false;
            int round=-1,finished=-1,turn_end=-1,serial=-1,subject=-1,group=-1,lifecycle=-1;
            bool known=false,all_visible=false,bank_ready=false,paths_ready=false;
            bool crossed_budget=false;
            int status=-1,issue=-1,failure=-1,members=0,bank_known=0;
            std::array<int,4> legs{-1,-1,-1,-1};
            std::uint64_t before=0,requested=0,built=0;
            std::uint64_t candidates=0,prebind=0,premove=0,end_fuel=0;
            std::array<std::uint64_t,3> execution{};
        } heavy{};
        int heavy_left=8;
        int serial=0;
        int stage=0,obs_part=0,obs_abort=0,intent=-1,participant=-1;
        int action=-1,target=-1,actions=0,move_reason=-1,model_status=-2,model_issue=-1,clear_ticks=-1,model_group=-1;
        int project=-1,phase=-1,bag=-1,gold=-1,score=-1,spawn=-1,receipt=0;
        int emitted=0,dropped=0,last_stage=-1,last_actions=-1,last_project=-1,last_phase=-1;
        std::uint64_t print_max=0;
        void begin() {
            const int now=cambc::round_number();
            round=now;
            stage=obs_part=obs_abort=actions=receipt=0;
            intent=participant=action=target=move_reason=model_issue=clear_ticks=model_group=-1;model_status=-2;
            project=phase=bag=gold=score=spawn=-1;
            ++serial;values.fill(0);costs.fill(0);builds=0;joint_before=joint_previous=joint_max_gap=0;
            joint_checks=joint_denials=sites_visited=sites_cutoff=0;reason=nullptr;group=-1;mark(0);
#if P6_JOINT_PREREQUISITES
            joint_pending=false;optional_stop=0;
#endif
        }
        void mark(int phase) {
            if(round>=0 || heavy.pending)values[phase]=cambc::fuel_remaining();
            if(heavy.pending && heavy.serial==serial) {
                if(phase==5)heavy.candidates=values[phase];
                if(phase==6)heavy.premove=values[phase];
            }
        }
        std::uint64_t measure() const { return round>=0 || heavy.pending?cambc::fuel_remaining():0; }
        void spend(int phase,std::uint64_t before) {
            if(round<0 && !heavy.pending)return;
            const auto after=cambc::fuel_remaining();
            if(after<=before)costs[phase]+=before-after;
            if(heavy.pending && heavy.serial==serial && phase>=2) {
                if(after<=before)heavy.execution[phase-2]+=before-after;
                else heavy.crossed_budget=true;
            }
        }
        struct Meter {
            FuelTrace& trace;int phase;std::uint64_t before;
            Meter(FuelTrace& value,int index):trace(value),phase(index),before(value.measure()) {}
            ~Meter(){trace.spend(phase,before);}
        };
        bool start_model(const organization::Site& site,bool bank_ready,bool paths_ready,int subject) {
            if(heavy.pending || heavy_left==0)return false;
            heavy={};heavy.pending=true;heavy.round=cambc::round_number();heavy.subject=subject;
            heavy.serial=serial;
            heavy.group=site.group.id;heavy.lifecycle=static_cast<int>(site.group.lifecycle);
            heavy.known=site.known;heavy.all_visible=site.group.all_spawns_visible;
            heavy.bank_ready=bank_ready;heavy.paths_ready=paths_ready;heavy.before=cambc::fuel_remaining();
            return true;
        }
        void requested_model(const forecast::Request& request) {
            heavy.requested=cambc::fuel_remaining();
            heavy.members=request.group_count?request.groups[0].member_count:0;
            heavy.bank_known=request.bank_available.current(request.kit.clock,request.bank_waypoint)?
                (request.bank_available.value?1:-1):0;
            for(int i=0;i<4 && i<request.leg_count;++i)heavy.legs[i]=request.legs[i].distance.value;
        }
        void built_model(const forecast::Result& result) {
            heavy.built=cambc::fuel_remaining();heavy.finished=cambc::round_number();
            heavy.status=static_cast<int>(result.status);heavy.issue=static_cast<int>(result.issue);
            heavy.failure=static_cast<int>(result.abort);
        }
        void report() {
#if P6_REPLICAS_CALLER_PACKET
            // Fixed deferred packet, after action work, from cached facts only.
            // The ordinary report fuel check guards emission; no world query.
            if(caller_pending) {
                const auto remaining=cambc::fuel_remaining();
                caller_packet[51]=static_cast<std::int64_t>(remaining);
                if(remaining>=32000)runtime_trace::emit(caller_packet,'r');
                caller_pending=false;
                return;
            }
#endif
            const bool changed=stage!=last_stage || actions!=last_actions || project!=last_project ||
                phase!=last_phase || (reason && (reason!=reported_reason || group!=reported_group));
            if(round>4 && !receipt && !turnback_pending && round%32!=0 && (!changed || round%8!=0))return;
            auto remaining=cambc::fuel_remaining();
            if(emitted>=512 || remaining<20000) {++dropped;return;}
            if(turnback_pending && remaining>=32000) {
                runtime_trace::emit(turnback,'d');turnback_pending=false;
                remaining=cambc::fuel_remaining();
                if(remaining<20000) {++dropped;return;}
            }
            std::uint32_t reason_hash=0;
            if(reason) {
                reason_hash=2166136261u;
                for(const auto* c=reason;*c;++c)reason_hash=(reason_hash^static_cast<unsigned char>(*c))*16777619u;
            }
            const std::array<std::int64_t,43> fields{round,identity(cambc::self()),stage,obs_part,obs_abort,
                group,intent,participant,project,phase,model_status,model_issue,clear_ticks,action,target,actions,
                bag,gold,score,spawn,receipt,static_cast<std::int64_t>(values[2]),
                static_cast<std::int64_t>(values[3]),static_cast<std::int64_t>(values[5]),
                static_cast<std::int64_t>(costs[2]),static_cast<std::int64_t>(costs[3]),
                static_cast<std::int64_t>(costs[4]),static_cast<std::int64_t>(remaining),dropped,
                static_cast<std::int64_t>(print_max),builds,move_reason,model_group,reason_hash,
                static_cast<std::int64_t>(costs[0]),static_cast<std::int64_t>(costs[1]),
                static_cast<std::int64_t>(joint_before),joint_checks,joint_denials,
                static_cast<std::int64_t>(joint_max_gap),static_cast<std::int64_t>(values[4]),sites_visited,sites_cutoff};
            runtime_trace::emit(fields);
#if P6_JOINT_PREREQUISITES
            // Numeric emission follows ordinary action work, under a separate
            // quota/reserve. It cannot open an observation or model gate.
            if(joint_pending && cambc::fuel_remaining()>=32000) {
                joint_packet[17]=static_cast<std::int64_t>(cambc::fuel_remaining());
                runtime_trace::emit(joint_packet,'j');joint_pending=false;++joint_packets;
                joint_packet_round=round;
            }
#endif
            const auto after=cambc::fuel_remaining();if(after<=remaining)print_max=std::max(print_max,remaining-after);
            ++emitted;last_stage=stage;last_actions=actions;last_project=project;last_phase=phase;
            if(reason) {reported_reason=reason;reported_group=group;}
            if(heavy.pending) {heavy.pending=false;if(heavy_left>0)--heavy_left;}
        }
    } fuel_{};
    bool collision_=false;
    bool action_view_live_=true;
    bool walked_=false;
    int hold_group_=-1,hold_started_=-1;
    bool own_bank_started_=false;
    int own_bank_round_=-1;
    Gems own_bank_bag_=0,own_bank_score_=0;
    ProjectID reported_failure_project_{};
    Failure reported_failure_=Failure::none;
    runtime_observation::DamageHistory damage_;
    unsigned monster_shards_=0,jungle_shards_=0;
#if P6_CAPITAL_CALLER_PACKET
    struct CapitalTrace {
        bool pending=false,shop=false,recovering=false,active=false,empty=false;
        int caller=0,phase=-1,item=-1,amount=-1,can_buy=-1;
    } capital_;
    void capital_recovery(int code) {
        if(!capital_.pending)return;
        capital_.caller=code;capital_.shop=shop_pending_;capital_.recovering=recovering_;
        capital_.active=execution_.active;capital_.phase=static_cast<int>(execution_.phase);
        capital_.empty=execution_.empty_liquidation;
    }
    bool capital_report() {
        if(!capital_.pending)return false;
        capital_.pending=false;
        const auto remaining=cambc::fuel_remaining();
        if(remaining<32000)return false;
        const auto& in=input_;const auto& a=in.frame.own.actor;
        const int index=site_index(P6_CAPITAL_CALLER_PACKET==2?4:5);
        const auto* site=index>=0?&in.sites[index]:nullptr;
        runtime_trace::emit(std::array<std::int64_t,28>{fuel_.round,a.id,a.position.x,a.position.y,
            std::bit_cast<std::int64_t>(a.health),std::bit_cast<std::int64_t>(a.max_health),in.frame.own.gold,a.bag,
            in.safe_now,in.frame.own.at_spawn,capital_.shop,capital_.recovering,capital_.active,capital_.phase,
            capital_.empty,capital_.caller,capital_.item,capital_.amount,capital_.can_buy,
            site?site->known:0,site?static_cast<int>(site->group.lifecycle):-1,
            requested_group_,in.independent_group,execution_.active,static_cast<int>(execution_.phase),
            std::bit_cast<std::int64_t>(observed_incoming_),fuel_.actions,static_cast<std::int64_t>(remaining)},'k');
        return true;
    }
#endif
#if P6_REPLICAS_CALLER_PACKET
    void caller_recovery(int code) {
        if(!fuel_.caller_pending)return;
        fuel_.caller_code=code;fuel_.caller_recovering=recovering_;fuel_.caller_shop=shop_pending_;
    }
    void caller_snapshot() {
        if(!fuel_.caller_pending)return;
        const auto& in=input_;const auto& a=in.frame.own.actor;
        const int i=site_index(12);
        const auto* s=i>=0?&in.sites[i]:nullptr;
        const auto* cycle=s?s->cycle(a.id):nullptr;
        const bool current=i>=0 && evaluated_[i] && evaluated_[i]->matches_frame(in.frame);
        const auto* model=current?&evaluated_[i]->result():nullptr;
        fuel_.caller_packet={fuel_.round,a.id,fuel_.stage,fuel_.obs_abort,
            execution_.active,static_cast<int>(execution_.phase),in.current_group,requested_group_,
            in.proposed_group,in.independent_group,in.safe_now,fuel_.caller_recovering,
            fuel_.caller_shop,fuel_.caller_code,fuel_.caller_policy,fuel_.caller_intent,
            fuel_.caller_group,fuel_.caller_reason,a.can_move,a.can_attack,
            static_cast<int>(a.channel),a.channel_valid,a.position.x,a.position.y,
            in.frame.own.gold,a.bag,a.spawn_group,in.frame.clock.round==fuel_.round,
            i,s?s->known:0,s?s->group.all_spawns_visible:0,
            s?static_cast<int>(s->group.lifecycle):-1,s?s->blocked:0,
            i>=0?site_visited_[i]:0,i>=0?sites_[i].paths_ready:0,bank_ready_,
            i>=0?quotes_[i].available:0,i>=0?quote_deferred_[i]:0,
            i>=0?admission_gate_[i]:-1,cycle?cycle->solo:0,
            cycle?cycle->valid(in.frame,a.id,12):0,current,
            model?static_cast<int>(model->status):-2,model?static_cast<int>(model->issue):-1,
            static_cast<std::int64_t>(fuel_.values[2]),static_cast<std::int64_t>(fuel_.values[3]),
            static_cast<std::int64_t>(fuel_.values[4]),static_cast<std::int64_t>(fuel_.values[5]),
            static_cast<std::int64_t>(fuel_.values[6]),fuel_.actions,recovering_,0};
    }
#endif
    bool current_view() const {
        return action_view_live_ && input_.frame.clock.round==static_cast<int>(cambc::round_number());
    }
    int ordinal(Champion value) const {
        const auto& order=Policy::profile().roster;
        for(int i=0;i<5;++i) if(order[i]==value) return i;
        return -1;
    }
    int tile_index(Point p) const { return p.y*cambc::map::width()+p.x; }
    const cambc::camp_stats& camp_design(const cambc::camp& camp) const {
        const int kind=static_cast<int>(camp.kind());
        if(kind<0 || kind>=static_cast<int>(camp_designs_.size()))
            throw std::runtime_error("unknown camp kind in observation");
        auto& cached=camp_designs_[kind];
        if(!cached)cached=camp.base_stats();
        return *cached;
    }
    Point pad(Point center,int skip=0) const {
        auto available=terrain_.reachable_tiles(native(center),1)&~(terrain_|camp_spawns_);
        const auto positions=available.positions();
        if(positions.size()<=static_cast<std::size_t>(skip)) return center;
        return point(positions[skip]);
    }
    bool verifying(int round) const {
        if constexpr(requires { policy_.verification_active(0,0); })
            return verification_site_>=0 && !control_arm() &&
                policy_.verification_active(sites_[verification_site_].geometry.id,round);
        return false;
    }
    void initialize() {
        if(!initial_started_) {
            picks_=static_cast<int>(cambc::draft::current_draft(cambc::team::ally).size());
            terrain_=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|
                     cambc::map::spawn_region(cambc::team::enemy);
            auto groups=cambc::map::camp_groups();initial_sites_.reserve(groups.size());sites_.reserve(8);
            for(auto& group:groups) {
                camp_spawns_|=group.spawn_tiles();
                const auto spawn=point(group.camps().front().spawn_position());
                const auto id=static_cast<int>(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(group)));
                initial_sites_.push_back({std::move(group),0,spawn,id});
            }
            initial_origin_=point(cambc::self().position());initial_started_=true;
            if constexpr(!control_arm() && requires { policy_.verification_active(0,0); }) {
                const int own_ordinal=ordinal(type(cambc::self().champion_name()));
                verification_eligible_=picks_>=3 && own_ordinal>=0 && own_ordinal<3;
                if(verification_eligible_) {
                    const auto spawn=cambc::map::spawn_region(cambc::team::ally).positions();
                    if(spawn.empty())verification_eligible_=false;
                    else {
                        for(const auto p:spawn) {verification_origin_.x+=p.x;verification_origin_.y+=p.y;}
                        verification_origin_.x/=static_cast<int>(spawn.size());
                        verification_origin_.y/=static_cast<int>(spawn.size());
                        // Caravan's common source and bank route use the same
                        // public origin for all core actors, rather than three
                        // private spawn-position preferences.
                        if(Policy::profile().slug=="caravan")initial_origin_=verification_origin_;
                        for(int i=0;i<3;++i) {
                            for(const auto& design:cambc::champion_designs)
                                if(type(design.champion_name())==Policy::profile().roster[i]) {
                                    verification_ranges_[i]=design.base_stats().base_attack_range;break;
                                }
                            if(verification_ranges_[i]<=0)verification_eligible_=false;
                        }
                    }
                }
            }
            return;
        }
        while(initial_key_<initial_sites_.size()) {
            auto& group=initial_sites_[initial_key_];const auto members=group.source.camps();
            while(initial_member_<members.size()) {
                if(cambc::fuel_remaining()<65000)return;
                const auto& member=members[initial_member_++];const auto& stats=camp_design(member);
                if(verification_eligible_)group.spawns.push_back(point(member.spawn_position()));
                initial_health_+=stats.base_health;initial_strength_+=stats.base_strength;
            }
            group.gold=group.source.gold_value();group.gems=group.source.gem_value();
            group.health=initial_health_;group.strength=initial_strength_;
            group.members=static_cast<int>(members.size());
            group.key=initial_health_/100.+distance(initial_origin_,group.spawn);
            if(verification_eligible_ && group.gems>0 &&
               (Policy::profile().slug!="caravan" || group.members>=3)) {
                const auto pads=runtime_catalogue::verification_pads<Point>(group.spawns,verification_ranges_,
                    verification_origin_,[&](Point p) {
                        return p.x>=0 && p.y>=0 && p.x<cambc::map::width() && p.y<cambc::map::height() &&
                            !terrain_.is_set(native(p)) && !camp_spawns_.is_set(native(p));
                    });
                const double key=group.health/100.+distance(verification_origin_,group.spawn);
                if(pads.available && (key<verification_key_ || (key==verification_key_ &&
                   (verification_initial_<0 || group.id<initial_sites_[verification_initial_].id)))) {
                    verification_key_=key;verification_initial_=static_cast<int>(initial_key_);
                    verification_pads_=pads.pads;
                }
            }
            ++initial_key_;initial_member_=0;initial_health_=initial_strength_=0;
        }
        if(!initial_sorted_) {
            // Each immutable SDK-derived key was queried once. Sorting itself
            // performs only local comparisons, never repeated host calls.
            std::vector<runtime_catalogue::Site> choices;choices.reserve(initial_sites_.size());
            for(const auto& site:initial_sites_)choices.push_back({site.key,site.id,site.gold,site.gems,
                site.health,site.strength,site.members});
            initial_order_=runtime_catalogue::select(choices,
                static_cast<unsigned>(std::max(0,ordinal(type(cambc::self().champion_name())))));
            if(verification_initial_>=0) {
                bool retained=false;
                for(std::size_t i=0;i<initial_order_.count;++i)
                    retained |= initial_order_.indices[i]==static_cast<std::size_t>(verification_initial_);
                if(!retained) {
                    const auto at=initial_order_.count<8?initial_order_.count++:7;
                    initial_order_.indices[at]=static_cast<std::size_t>(verification_initial_);
                }
            }
            initial_sorted_=true;
        }
        while(sites_.size()<initial_order_.count) {
            if(cambc::fuel_remaining()<65000)return;
            const auto& group=initial_sites_[initial_order_.indices[sites_.size()]];
            sites_.emplace_back(group.source);auto& s=sites_.back();
            s.work={group.key,group.id,group.gold,group.gems,group.health,group.strength,group.members};
            s.geometry.id=group.id;s.geometry.spawn=group.spawn;
            s.geometry.approach=pad(s.geometry.spawn);
            s.geometry.retreat=pad(s.geometry.spawn,1);
            if(static_cast<int>(initial_order_.indices[sites_.size()-1])==verification_initial_) {
                verification_site_=static_cast<int>(sites_.size()-1);
            }
            s.members.reserve(s.source.camps().size());
            for(const auto& member:s.source) {
                const auto& stats=camp_design(member);const int id=identity(member);
                s.members.push_back({id,point(member.spawn_position()),{id,stats.base_health,stats.base_strength}});
            }
        }
        if(cambc::fuel_remaining()<65000)return;
        // At most two altar candidates. A static approach is not proof of an
        // unlocked altar; lock ownership is refreshed before banking.
        int nearest=1000000,count=0;
        for(const auto a:cambc::map::altars().positions()) {
            if(count++==2)break;
            for(const auto p:(around(point(a),cambc::constants::submit_gems_range)&~terrain_).positions())
                if(distance(initial_origin_,point(p))<nearest) {
                    nearest=distance(initial_origin_,point(p)); bank_=point(p);altar_=point(a);bank_exists_=true;
                }
        }
        if(verification_site_>=0)preferred_site_=proposed_site_=verification_site_;
        initialized_=true;initial_sites_.clear();
    }
    bool assignment_eligible(int group,int ordinal,int proposed) const {
        if(Policy::profile().slug=="owner-builder" && ordinal<3 && group%3!=ordinal)return false;
        if(Policy::profile().slug=="replicas" && ordinal>=1 && group==proposed)return false;
        return true;
    }
#if P6_WALKING_INSPECTION_REPAIR
    bool prepare_walking_component() {
        if(walking_component_.complete())return false;
        if(!walking_component_.started()) {
            // Dedicated fresh owner turn: copy only immutable public terrain.
            // No dynamic blocker or current-source fact survives this work.
            if(cambc::fuel_remaining()<80000)return true;
            if(!walking_component_.begin_packed(cambc::map::width(),cambc::map::height(),initial_origin_,
                terrain_.export_linear_buffer()))throw std::runtime_error("walking component initialization");
            return true;
        }
        while(!walking_component_.complete() && cambc::fuel_remaining()>=65000)
            walking_component_.advance(128);
        return true;
    }
#endif
    bool prepare_initial_quote() {
        if(initial_quote_checked_)return false;
        initial_quote_checked_=true;
        if(verification_site_>=0)return false;
        auto& me=cambc::self();
        // Speculative memory only, once before the first ordinary observation.
        // Never delay an owned conversion, a channel, danger response or bag.
        if(execution_.active || own_bank_started_ || me.gem_count()!=0 ||
           !cambc::map::spawn_region(cambc::team::ally).is_set(me.position()))return false;
        const double health=me.health(),maximum=me.max_health();
        if(health!=maximum || !cambc::vision::champions(cambc::team::enemy).empty())return false;
        for(const auto& camp:cambc::vision::camps()) {
            if(cambc::fuel_remaining()<70000 || camp.state().is_awake())return false;
        }
        std::array<runtime_catalogue::Site,8> choices{};
        for(std::size_t i=0;i<sites_.size();++i)choices[i]=sites_[i].work;
        const auto work=std::span<const runtime_catalogue::Site>{choices.data(),sites_.size()};
        const runtime_catalogue::Actor actor{health,maximum,me.strength(),me.attack_range()>1,-1};
        const auto proposed=runtime_catalogue::preferred(work,actor);
        if(proposed>=sites_.size())return false;
        const int ordinal=std::max(0,this->ordinal(type(me.champion_name())));
        runtime_catalogue::Order allowed;
        for(std::size_t i=0;i<sites_.size();++i)
            if(assignment_eligible(sites_[i].geometry.id,ordinal,sites_[proposed].geometry.id))
                allowed.indices[allowed.count++]=i;
        const auto selected=runtime_catalogue::preferred(work,allowed,actor);
        if(selected>=sites_.size() || evaluated_[selected])return false;
        const auto before=cambc::fuel_remaining();
        if(before<65000)return false;
        evaluated_[selected]=std::make_unique<forecast::EvaluatedQuote>();
#if P6_JOINT_PREPARE
        joint_initial_site_=static_cast<int>(selected);
#endif
        const auto after=cambc::fuel_remaining();
        fuel_.model_group=sites_[selected].geometry.id;
        fuel_.group=fuel_.model_group;fuel_.reason="initial-quote-storage";
        // Bounded once-only storage measurement, separate from model costs.
        if(after>=32000)runtime_trace::emit(std::array<std::int64_t,5>{
            cambc::round_number(),identity(me),fuel_.model_group,
            static_cast<std::int64_t>(before),static_cast<std::int64_t>(after)},'p');
        return true;
    }
#if P6_JOINT_PREPARE
    bool prepare_joint_storage() {
        if(joint_storage_checked_)return false;
        auto& me=cambc::self();
        if(execution_.active || own_bank_started_ || me.gem_count()!=0 ||
           !cambc::map::spawn_region(cambc::team::ally).is_set(me.position()) ||
           me.health()!=me.max_health() || !cambc::vision::champions(cambc::team::enemy).empty()) {
            joint_storage_checked_=true;return false;
        }
        for(const auto& camp:cambc::vision::camps()) {
            if(cambc::fuel_remaining()<70000)return true;
            if(camp.state().is_awake()) {joint_storage_checked_=true;return false;}
        }
        if(cambc::fuel_remaining()<70000)return true;
        const int primary=joint_initial_site_>=0?joint_initial_site_:preferred_site_;
        if(primary<0 || primary>=static_cast<int>(sites_.size())) {joint_storage_checked_=true;return false;}
        const int second=primary==0?1:0;
        for(const int site:{primary,second}) {
            if(site>=static_cast<int>(sites_.size()))continue;
            if(!bank_ready_ || !sites_[site].paths_ready) {prepare_paths(site);return true;}
            for(int actor=1;actor<=2;++actor) {
                if(!requests_.storage_ready(site,actor)) {requests_.prepare(site,actor);return true;}
                if(!models_.storage_ready(site,actor)) {models_.prepare(site,actor);return true;}
            }
        }
        joint_storage_checked_=true;return false;
    }
#endif
    bitboard blocked() const {
        auto b=terrain_|cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|
               cambc::vision::teleport_tiles();
        b.set(cambc::self().position(),false);return b;
    }
    void prepare_paths(int preferred) {
        if(cambc::fuel_remaining()<45000)return;
        bitboard visited;
        if(!bank_ready_ && bank_exists_) {
            bank_paths_.compute(terrain_,tile(bank_),visited,256);bank_ready_=true;
            return;
        }
        if(preferred<0 || preferred>=static_cast<int>(sites_.size()))return;
        auto& s=sites_[preferred];if(s.paths_ready)return;
        s.paths.compute(terrain_,tile(s.geometry.approach),visited,256);
        s.paths_ready=true;
    }
    bool prepare_verification_paths() {
        if(verification_site_<0 || verification_paths_ready_)return false;
        if(cambc::fuel_remaining()<65000)return true;
        if(!verification_paths_)verification_paths_=std::make_unique<cambc::distance_field>();
        bitboard visited;
        verification_paths_->compute(terrain_|camp_spawns_,
            tile(verification_pads_[ordinal(type(cambc::self().champion_name()))]),visited,256);
        verification_paths_ready_=true;return true;
    }
    Actor actor(const cambc::champion& a,Team team) const {
        // The pinned SDK getters each import this same fresh record. Only
        // current runtime handles from self/vision enter this adapter. Keep
        // the owning numeric copy local to this action-free observation.
        const auto handle=static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(a));
        if(handle==std::numeric_limits<std::uint32_t>::max())
            throw cambc::error{cambc::error_code::api_not_in_vision,"design champion has no live state"};
        cambc::detail::champion_info view{};
        cambc::detail::check(cambc::detail::sdk_champion(handle,&view));
        Actor result;result.id=identity(a);result.type=type(static_cast<cambc::champion_type>(view.type));
        result.pick_ordinal=ordinal(result.type);result.team=team;
        const auto state=cambc::champion_state::from_bits_retain(view.state);
        result.alive=!state.is_dead();result.position={view.x,view.y};
        result.health=view.health;result.max_health=view.max_health;result.strength=view.strength;
        result.attack_range=view.attack_range;result.ranged=result.attack_range>1;
        result.move_speed=view.move_speed;result.completed_turn=state.contains(cambc::champion_state::completed_turn);
        result.can_move=state.contains(cambc::champion_state::can_move);
        result.can_attack=state.contains(cambc::champion_state::can_attack);result.bag=a.gem_count();
        result.channel=state.contains(cambc::champion_state::submitting_gems)?Channel::tribute:
                       state.contains(cambc::champion_state::recalling)?Channel::recall:
                       state.contains(cambc::champion_state::teleporting)?Channel::teleport:Channel::none;
        result.channel_valid=result.channel!=Channel::none;
        std::optional<bitboard> attacks;
        for(const auto& s:sites_) {
            if(s.spawns.is_set(native(result.position))) {
                result.spawn_group=s.geometry.id;result.action=WitnessAction::occupied_spawn;
            }
        }
        if(result.can_attack && !attack_targets_.empty()) {
            // Preserve the prior last-eligible-site precedence, without scanning
            // every camp once per site for every observed actor.
            attacks.emplace(a.attack_tiles());
            if(result.id==input_.frame.clock.self)observed_self_attack_=*attacks;
            result.ready_attack_group=runtime_observation::ready_group(
                attack_targets_,*attacks,[](Point value){return native(value);});
            if(verification_site_>=0 && team==Team::ally && result.pick_ordinal>=0 &&
               result.pick_ordinal<3 && result.alive && result.channel==Channel::none) {
                if(runtime_observation::whole_basic_members(sites_[verification_site_].members,
                    [&](int id){return camp_view_.find(input_.frame.clock,id);},
                    [&](Point p){return attacks->is_set(native(p));}))
                    verification_mask_ |= static_cast<std::uint8_t>(1u<<result.pick_ordinal);
            }
        }
        if(result.id==input_.frame.clock.self && successful_attack_round_>=input_.frame.clock.round-1) {
            result.action=WitnessAction::useful_attack;result.action_group=successful_attack_group_;
        }
        return result;
    }
    bool altar_legal() const {
        if(!bank_exists_)return false;
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))
            if(point(lock.position())==altar_)return false;
        // If not personally visible, availability is unknown, never true.
        return cambc::vision::visible_tiles().is_set(native(altar_));
    }
    double incoming(Point p) const {
        double amount=0;
        for(const auto& m:camps_) if(m.state().is_awake() &&
            distance(p,point(m.position()))<=m.attack_range())amount+=m.strength();
        for(int i=0;i<input_.enemy_stress_count;++i)
            if(input_.enemy_stress[i].current(input_.frame.clock,input_.enemy_stress[i].subject))
                amount+=input_.enemy_stress[i].value;
        return amount;
    }
    runtime_threat::Enemy local_enemy(const cambc::champion& enemy,const Actor& actor,
        double stress,cambc::passive_type passive) const {
        runtime_threat::Enemy out;out.id=actor.id;out.position=actor.position;out.stress=stress;
        out.move_speed=actor.move_speed;out.attack_range=actor.attack_range;
        out.ranged=actor.ranged;out.completed_turn=actor.completed_turn;
        out.complete=actor.alive && actor.channel==Channel::none &&
            cambc::constants::tp_crystal_time>=2 && cambc::constants::recall_time>=2;
        // Retaliation depends on the attacker's action, not the retaliator's
        // geometric reach. Unresolved channels and lingering areas also keep
        // the full bound; this adapter has no pending-effect location oracle.
        if(passive==cambc::passive_type::retaliation)out.complete=false;
        if(passive==cambc::passive_type::splash_attack)
            out.splash_radius=cambc::constants::passive_abilities::varg_splash_radius;
        const auto& abilities=enemy.abilities();
        for(int i=0;i<2;++i) {
            const auto& info=abilities[i].info();if(!info)continue;
            auto& slot=out.slots[i];
            std::visit([&](const auto& value) {
                using T=std::remove_cvref_t<decltype(value)>;
                slot.exclusive_movement=value.exclusive_with_movement;
                if constexpr(std::is_same_v<T,cambc::ability_attack_reset>)slot.reset=true;
                else if constexpr(std::is_same_v<T,cambc::ability_dash>) {
                    slot.dash=true;slot.range=value.range;slot.radius=value.radius;
                    slot.harmful=value.modifier>0 || value.duration>0;
                } else if constexpr(std::is_same_v<T,cambc::ability_damage> ||
                                  std::is_same_v<T,cambc::ability_cc>) {
                    slot.harmful=true;slot.range=value.range;
                } else if constexpr(std::is_same_v<T,cambc::ability_aoe_damage>) {
                    slot.harmful=true;slot.range=value.range;slot.radius=value.radius;
                    if(value.duration!=0)out.complete=false;
                } else if constexpr(!(std::is_same_v<T,cambc::ability_heal> ||
                                     std::is_same_v<T,cambc::ability_shield>))slot.known=false;
            },*info);
        }
        return out;
    }
    Exposure immediate_exposure(Point p,double camp_incoming) const {
        auto actor=input_.frame.own.actor;actor.position=p;
        return runtime_threat::exposure(actor,input_.frame.clock,immediate_,camp_incoming);
    }
    bool observe() {
        const int observed_round=cambc::round_number();
        input_.reset_current();camp_view_.invalidate();immediate_.complete=false;
        observed_self_attack_.reset();observed_enemies_.clear();verification_mask_=0;
        auto& in=input_;auto& me=cambc::self();
        in.frame.clock={observed_round,identity(me),++revision_};
        auto budget=[&] {
            const bool ready=static_cast<int>(cambc::round_number())==observed_round && cambc::fuel_remaining()>=30000;
            if(!ready)fuel_.obs_abort=fuel_.obs_part;
            return ready;
        };
        if(!budget())return false;
        fuel_.obs_part=1;
        camps_=cambc::vision::camps();allies_=cambc::vision::champions(cambc::team::ally);
        enemies_=cambc::vision::champions(cambc::team::enemy);
        const auto visible=cambc::vision::visible_tiles();
        fuel_.obs_part=2;attack_targets_.clear();
        observed_camp_sources_=camps_;
        for(const auto& site:sites_)
            if(!runtime_observation::append_visible_members(observed_camp_sources_,site.source.camps(),
                [&](const auto& camp){return visible.is_set(camp.spawn_position());},
                [](const auto& camp){return identity(camp);},
                [](const auto& camp){return camp.is_visible();},budget))return false;
        if(!camp_view_.collect(observed_camp_sources_,in.frame.clock,[](const auto& camp){return identity(camp);},
            [&](const auto& camp){return camp_design(camp).base_attack_range;},budget,
            [](const auto& camp,runtime_observation::Camp& value) {
                // Match the pinned SDK's private numeric getters with one
                // owning read. Public lifecycle/visibility remains separate;
                // dead members still query strength but publish no live HP.
                cambc::detail::camp_state reading{};
                cambc::detail::check(cambc::detail::sdk_camp(
                    static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camp)),&reading));
                value.strength=reading.strength;
                if(value.alive) {
                    value.position={reading.x,reading.y};
                    value.health=reading.health;value.max_health=reading.max_health;
                }
            }))return false;
        for(const auto& m:camp_view_.values(in.frame.clock)) {
            if(m.alive)for(int i=0;i<static_cast<int>(sites_.size());++i)
                if(sites_[i].spawns.is_set(native(m.spawn)))
                    attack_targets_.push_back({i,sites_[i].geometry.id,m.position});
            damage_.observe(m.id,observed_round,m.health);
        }
        if(!budget())return false;
        fuel_.obs_part=3;in.frame.own.actor=actor(me,Team::ally);in.frame.own.gold=me.gold_count();
        in.frame.own.at_spawn=cambc::map::spawn_region(cambc::team::ally).is_set(native(in.frame.own.actor.position));
        in.frame.own.spawn_recovery_observed=in.frame.own.at_spawn && in.frame.own.actor.health==in.frame.own.actor.max_health;
        in.frame.own_team_score=cambc::global_submitted_gems(cambc::team::ally);
        fuel_.obs_part=4;
        for(const auto& list:{&allies_,&enemies_})for(const auto& a:*list) {
            if(identity(a)==in.frame.clock.self || in.frame.actor_count==max_actors)continue;
            if(!budget())return false;
            auto value=actor(a,list==&allies_?Team::ally:Team::enemy);
            in.frame.actors[in.frame.actor_count++]=seen(value,in.frame.clock,value.id);
        }
        fuel_.obs_part=5;target_ehp_={};double legal_hp=std::numeric_limits<double>::infinity();
        std::optional<bitboard> enemy_attack_tiles;
        if(in.frame.own.actor.can_attack && !enemies_.empty()) {
            if(!budget())return false;
            if(observed_self_attack_)enemy_attack_tiles.emplace(*observed_self_attack_);
            else enemy_attack_tiles.emplace(me.attack_tiles());
        }
        for(const auto& e:enemies_)if(in.enemy_stress_count<max_actors) {
            if(!budget())return false;
            const int id=identity(e);const auto* actor_view=in.frame.visible(id);
            Actor danger;
            if(actor_view)danger=*actor_view;
            else {
                // The actor catalogue is bounded; an omitted visible enemy
                // still contributes a complete fresh local danger record.
                cambc::detail::champion_info reading{};
                cambc::detail::check(cambc::detail::sdk_champion(
                    static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(e)),&reading));
                const auto state=cambc::champion_state::from_bits_retain(reading.state);
                danger.id=id;danger.position={reading.x,reading.y};danger.strength=reading.strength;
                danger.move_speed=reading.move_speed;danger.attack_range=reading.attack_range;
                danger.ranged=reading.attack_range>1;
                danger.alive=!state.is_dead();
                danger.completed_turn=state.contains(cambc::champion_state::completed_turn);
                danger.channel=state.contains(cambc::champion_state::teleporting)?Channel::teleport:
                    state.contains(cambc::champion_state::recalling)?Channel::recall:
                    state.contains(cambc::champion_state::submitting_gems)?Channel::tribute:Channel::none;
            }
            const auto position=danger.position;const double strength=danger.strength;
            const auto passive=e.passive();
            double cast=runtime_combat::enemy_spell_bound(e.abilities(),strength);
            if(passive==cambc::passive_type::retaliation)
                cast+=2*e.focus()*cambc::constants::passive_abilities::ren_retaliation_focus_ratio;
            if(passive==cambc::passive_type::missing_health_damage)
                cast=(1+cambc::constants::passive_abilities::heart_of_ruin_missing_health_damage_max_bonus)*cast+
                    strength*cambc::constants::passive_abilities::heart_of_ruin_missing_health_damage_max_bonus;
            observed_enemies_.push_back({id,position,strength,strength+cast});
            in.enemy_stress[in.enemy_stress_count++]=seen(strength+cast,in.frame.clock,id);
            in.local_enemies[in.local_enemy_count++]=seen(local_enemy(e,danger,strength+cast,passive),in.frame.clock,id);
            if(enemy_attack_tiles && enemy_attack_tiles->is_set(native(position))) {
                const double health=actor_view?actor_view->health:e.health();
                if(health<legal_hp || (health==legal_hp && id<in.legal_enemy_target)) {
                    legal_hp=health;in.legal_enemy_target=id;double hp=health;
                    for(const auto& buff:e.buffs())if(const auto* shield=std::get_if<cambc::buff_shield>(&buff))hp+=shield->value;
                    target_ehp_=seen(hp,in.frame.clock,id);
                }
            }
        }
        if(!budget())return false;
        if(in.legal_enemy_target>=0 && !in.frame.visible(in.legal_enemy_target))
            for(const auto& e:enemies_)if(identity(e)==in.legal_enemy_target && in.frame.actor_count>0) {
                if(!budget())return false;
                int replace=in.frame.actor_count-1;
                for(int i=0;i<in.frame.actor_count;++i)if(in.frame.actors[i].value.team==Team::enemy) {replace=i;break;}
                in.frame.actors[replace]=seen(actor(e,Team::enemy),in.frame.clock,identity(e));break;
            }
        in.ordinal=in.frame.own.actor.pick_ordinal;
        in.picks=picks_;
        in.settings={static_cast<int>(cambc::constants::max_rounds),static_cast<int>(cambc::constants::winning_gems),
                     cambc::constants::capture_time};
        const auto enemy_score=static_cast<int>(cambc::global_submitted_gems(cambc::team::enemy));
        history_.sample({in.frame.clock.round,in.frame.clock.self},enemy_score);
        in.windows=history_.windows(in.settings,{in.frame.clock.round,in.frame.clock.self},enemy_score);
        // This used to query every awake camp in an unguarded tail after a
        // costly enemy batch. Keep each live query batch inside the same turn.
        if(!budget())return false;
        double local_incoming=0;
        for(const auto& m:camp_view_.values(in.frame.clock)) {
            if(m.awake && distance(in.frame.own.actor.position,m.position)<=m.attack_range)
                local_incoming+=m.strength;
        }
        immediate_=organization::prepare_immediate(in);
        in.safe_now=survives(immediate_exposure(in.frame.own.actor.position,local_incoming));
        // Preserve the original full value for long-horizon owned milestones.
        for(int i=0;i<in.enemy_stress_count;++i)local_incoming+=in.enemy_stress[i].value;
        observed_incoming_=local_incoming;
        in.current_group=execution_.active?execution_.project.group:-1;in.phase=execution_.phase;
        in.restored=in.frame.own.spawn_recovery_observed;
        in.hidden_collision=collision_;
        const bool witnessed=execution_.failure==Failure::observed_unavailable ||
                             execution_.failure==Failure::observed_failed_action;
        in.observed_failure=witnessed && (execution_.project!=reported_failure_project_ || execution_.failure!=reported_failure_);
        in.bank_approach=bank_;in.at_bank=distance(in.frame.own.actor.position,altar_)<=cambc::constants::submit_gems_range;
        fuel_.obs_part=6;
        std::array<Group,8> geometry{};
        std::array<runtime_catalogue::Site,8> public_work{};
        for(int i=0;i<static_cast<int>(sites_.size());++i) {
            geometry[i]=sites_[i].geometry;public_work[i]=sites_[i].work;
        }
        const auto& own=in.frame.own.actor;
        const auto preference=runtime_catalogue::preferred(
            std::span<const runtime_catalogue::Site>{public_work.data(),sites_.size()},
            {own.health,own.max_health,own.strength,own.ranged,
                in.frame.own.at_spawn?-1:own.ready_attack_group});
        if(preference<sites_.size())preferred_site_=static_cast<int>(preference);
        if(proposed_site_<0 && !sites_.empty())proposed_site_=preferred_site_;
        const int requested=verifying(observed_round)?sites_[verification_site_].geometry.id:requested_group_>=0?requested_group_:
            sites_.empty()?-1:sites_[preferred_site_].geometry.id;
        int visits=0;
        const int required_visits=in.current_group>=0 && requested>=0 && requested!=in.current_group?2:1;
        // The complete self/enemy-danger frame is mandatory. Optional site
        // publication yields before spending the warm model/action reserve;
        // only owned/requested state may use the smaller observation reserve.
        auto optional_budget=[&] {
            const bool required=visits++<required_visits;
            if(!required) {
                // A complete requested production view is already useful. Do
                // not let unrelated publication repeatedly crowd out its cold
                // storage/model admission, even on an otherwise roomy turn.
                for(int i=0;i<in.site_count;++i)if((in.sites[i].group.id==requested ||
                    in.sites[i].group.id==in.current_group) && in.sites[i].known &&
                    in.sites[i].group.all_spawns_visible && in.sites[i].group.lifecycle!=Lifecycle::respawning) {
#if P6_JOINT_PREREQUISITES
                    fuel_.optional_stop=1;
#endif
                    return false;
                }
            }
            const auto reserve=required?30000u:85000u;
#if P6_JOINT_PREREQUISITES
            const bool current=static_cast<int>(cambc::round_number())==observed_round;
            const bool enough=cambc::fuel_remaining()>=reserve;
            if(!required && (!current || !enough))fuel_.optional_stop=current?2:3;
            return current && enough;
#else
            return static_cast<int>(cambc::round_number())==observed_round && cambc::fuel_remaining()>=reserve;
#endif
        };
        const auto pass=runtime_observation::publish_sites(in,
            std::span<const Group>{geometry.data(),sites_.size()},in.current_group,requested,optional_budget,
            [&](int index,runtime_observation::SiteReading& reading) {
            const auto& s=sites_[index];reading.clock=in.frame.clock;reading.group=s.geometry.id;
            reading.all_spawns_visible=true;
            for(const auto& m:s.members) {
                reading.original_spawn_visible|=visible.is_set(native(m.spawn));
                reading.all_spawns_visible&=visible.is_set(native(m.spawn));
            }
            if(reading.original_spawn_visible) {
                const auto state=s.source.state();
                reading.lifecycle=std::holds_alternative<cambc::map::camp_group_alive>(state)?Lifecycle::living:
                    std::holds_alternative<cambc::map::camp_group_reward_available>(state)?Lifecycle::reward_available:Lifecycle::respawning;
            }
            reading.uncontested_now=reading.all_spawns_visible;
            for(const auto& a:observed_enemies_)if(s.spawns.is_set(native(a.position)))reading.uncontested_now=false;
            for(const auto& m:s.members) {
                const auto* seen=camp_view_.find(in.frame.clock,m.id);
                if(seen && seen->alive && seen->health<seen->max_health)reading.damaged_now=true;
            }
            return static_cast<int>(cambc::round_number())==observed_round;
        });
        site_visited_=pass.visited_sites;
        fuel_.sites_visited=pass.visited;fuel_.sites_cutoff=pass.cutoff;
        if(!pass.ready() || (execution_.active && execution_.phase==Phase::hold && !pass.required_known)) {
            fuel_.obs_abort=6;return false;
        }
#if P6_JOINT_PREPARE
        // All numeric records came from this same complete mandatory personal
        // observation. Optional SDK publication keeps its original85k reserve.
        if(camp_view_.current(in.frame.clock))for(int i=0;i<in.site_count;++i)
            if(!site_visited_[i] && runtime_observation::reuse_living_site(in,i,sites_[i].members,
                [&](int id){return camp_view_.find(in.frame.clock,id);},observed_enemies_)) {
                site_visited_[i]=true;++fuel_.sites_visited;
            }
#endif
        if(!sites_.empty()) {
            in.proposed_group=sites_[proposed_site_].geometry.id;in.rally=sites_[proposed_site_].geometry.retreat;
            in.at_rally=in.frame.own.actor.position==in.rally;
        }
        if(verifying(observed_round) && verification_paths_ready_) {
            const auto& site=in.sites[verification_site_];
            in.proposed_group=site.group.id;in.rally=verification_pads_[in.ordinal];
            in.at_rally=in.frame.own.actor.position==in.rally;
            const auto length=verification_paths_->get(native(in.frame.own.actor.position));
            if(length!=cambc::distance_field::unreachable)in.verification_travel=length;
            bool complete=site.known && site.group.all_spawns_visible,live=false;
            for(const auto& member:sites_[verification_site_].members) {
                const auto* m=camp_view_.find(in.frame.clock,member.id);
                complete &= m!=nullptr;if(m)live |= m->alive;
            }
            if(complete && live)in.witness_core_basic=seen(verification_mask_,in.frame.clock,site.group.id);
        }
        // A prior HOLD plus actual resource change and lifecycle transition is
        // attributable only while still standing on that original region.
        if(execution_.active && execution_.phase==Phase::hold && execution_.previous_observed) {
            const auto* g=in.site(execution_.project.group);
            in.actual_payout=g && g->known && g->group.lifecycle==Lifecycle::respawning &&
                in.frame.own.actor.spawn_group==g->group.id &&
                (in.frame.own.actor.bag>execution_.previous_bag || in.frame.own.gold>execution_.previous_gold);
        }
        in.actual_bank=own_bank_started_ && in.frame.clock.round>own_bank_round_ &&
            in.frame.own.actor.bag==0 && in.frame.own_team_score-own_bank_score_>=own_bank_bag_;
        if(static_cast<int>(cambc::round_number())!=observed_round) {fuel_.obs_abort=6;return false;}
        // Only a complete current frame delivers one-shot notices. A budgeted
        // abort retains them; a durable receiver must accept before delivery
        // is acknowledged, independently of later policy/action deferrals.
        collision_=false;
        if(in.observed_failure && runtime_observation::deliver_failure(policy_,execution_.project,execution_.failure)) {
            reported_failure_project_=execution_.project;reported_failure_=execution_.failure;
        }
        if(in.actual_payout || in.actual_bank) {
            ReferenceInput receipt{.frame=in.frame};
            receipt.completion={in.frame.clock,execution_.project.group,in.actual_payout,in.actual_bank,true};
            record_completion(execution_,receipt);in.phase=execution_.phase;
            if(in.actual_bank && (!execution_.active || execution_.bank_seen))own_bank_started_=false;
        }
        fuel_.obs_part=7;
        return true;
    }
    void request_into(int index,const Frame& frame,forecast::Request& r,bool bank_only=false) const {
        const auto& s=sites_[index];const auto& site=input_.sites[index];
        const auto& a=frame.own.actor;
        r.group_count=r.enemy_cast_count=r.leg_count=0;r.bank_available={};
        r.shield={};r.recovery={};r.purchase_requested=false;
        r.shape=bank_only?forecast::Shape::bank_only:forecast::Shape::produce_bank;
        r.project_group=s.geometry.id;
        r.ambient_incoming={};
        // Subject alternatives share this executing actor's action-free view,
        // while retaining their own subject clock. An expired snapshot cannot
        // be restamped into a new current quote.
        if(!camp_view_.current(input_.frame.clock) || frame.clock.round!=input_.frame.clock.round ||
           frame.clock.revision!=input_.frame.clock.revision)return;
        r.timing={{input_.settings,Source::queried_setting},
            {cambc::constants::submit_gem_duration_multiplier,Source::queried_setting},
            {cambc::constants::submit_gem_base_duration,Source::queried_setting},
            {cambc::constants::submit_gem_minimum_duration,Source::queried_setting}};
        r.kit=seen(Productivity{a.strength,0,a.ranged,false,false,false},frame.clock,a.id);
        double ambient=0;
        // Keep full awake stress before work. The represented group publishes
        // the exact overlap below so the model can remove its old life only
        // after accounting for that source's modeled clearance.
        for(const auto& m:camp_view_.values(input_.frame.clock))if(m.awake)ambient+=m.strength;
        // Represented enemies are charged by the forecast; omitted enemies
        // remain full ambient stress. Neither receives range/cooldown savings.
        for(const auto& e:observed_enemies_) {
            const double cast=std::max(0.,e.stress-e.strength);
            bool represented=false;
            for(int i=0;i<frame.actor_count;++i)if(frame.actors[i].subject==e.id && frame.actors[i].value.team==Team::enemy)
                represented=true;
            if(represented)r.enemy_cast_stress[r.enemy_cast_count++]=seen(cast,frame.clock,e.id);
            else ambient+=e.strength+cast;
        }
        r.ambient_incoming=seen(ambient,frame.clock,a.id);
        r.bank_waypoint=100000+tile_index(bank_);r.bank_position=bank_;
        r.renewal_waypoint=s.geometry.id;r.renewal_position=s.geometry.approach;
        if(cambc::vision::visible_tiles().is_set(native(altar_)))r.bank_available=seen(altar_legal(),frame.clock,r.bank_waypoint);
        r.bank_lock_hits={static_cast<int>(cambc::constants::altar_lock_hits),Source::queried_setting};
        auto leg=[&](Point from,Point to,int d){r.legs[r.leg_count++]={from,to,{d,Source::public_map}};};
        if(!bank_only) {
            r.group_count=1;auto& g=r.groups[0];g.member_count=0;
            g.stationary_basic={};
            g.observed=seen(site.group,frame.clock,site.group.id);
            double overlap=0;
            for(const auto& m:camp_view_.values(input_.frame.clock))if(m.awake && s.spawns.is_set(native(m.spawn)))
                overlap+=m.strength;
            g.ambient_overlap=seen(overlap,frame.clock,site.group.id);
            if(s.spawns.is_set(native(a.position))) {
                g.observed.value.approach=a.position;g.observed.value.spawn=a.position;
            }
            g.membership_complete={true,Source::public_map};
            g.gems={s.source.gem_value(),Source::public_map};g.gold={s.source.gold_value(),Source::public_map};
            bool stationary=a.id==input_.frame.clock.self && a.ranged && a.can_attack && observed_self_attack_.has_value();
            bool living=false;
            for(const auto& m:s.members) {
                if(g.member_count==32) {g.member_count=33;break;}
                auto& member=g.members[g.member_count++];
                member.public_full={m.full,Source::public_map};
                member.current={};
                if(const auto* value=camp_view_.find(input_.frame.clock,m.id)) {
                    member.current=seen(MonsterWork{m.id,value->health,value->strength},frame.clock,m.id);
                    if(value->alive) {
                        living=true;
                        if(!observed_self_attack_ || !observed_self_attack_->is_set(native(value->position)))stationary=false;
                    }
                } else stationary=false;
            }
            stationary=stationary && g.member_count<=32 && g.member_count==static_cast<int>(s.members.size());
            int capture_distance=distance(g.observed.value.approach,g.observed.value.spawn);
            if(stationary && living) {
                // The capture walk remains a represented terrain route. Moving
                // the firing origin must not replace it by a wall-crossing
                // straight line from a ranged tile to the source spawn.
                const bool already_on_spawn=s.spawns.is_set(native(a.position));
                const int to_original=already_on_spawn?0:s.paths.get(native(a.position));
                const int to_spawn=already_on_spawn?0:distance(s.geometry.approach,s.geometry.spawn);
                if(to_original!=cambc::distance_field::unreachable && to_original+to_spawn<=256) {
                    capture_distance=to_original+to_spawn;g.observed.value.approach=a.position;
                    g.stationary_basic=seen(true,frame.clock,site.group.id);
                }
            }
            const auto& geometry=g.observed.value;
            leg(a.position,geometry.approach,a.position==geometry.approach?0:s.paths.get(native(a.position)));
            leg(geometry.approach,geometry.spawn,capture_distance);
            leg(geometry.spawn,bank_,bank_paths_.get(native(geometry.spawn)));
        } else leg(a.position,bank_,bank_paths_.get(native(a.position)));
        leg(bank_,s.geometry.approach,bank_paths_.get(native(s.geometry.approach)));
    }
    forecast::Request request(int index,const Frame& frame,bool bank_only=false) const {
        forecast::Request r;request_into(index,frame,r,bank_only);
        return r;
    }
    // One private current crew candidate; no grid of hypothetical arrivals or
    // per-participant solo completions is needed for this distinct capability.
    bool caravan_crew(int index) {
        auto& in=input_;const auto& s=sites_[index];const auto& site=in.sites[index];
        if(Policy::profile().slug!="caravan" || control_arm() || !crew_quote_ ||
           in.ordinal<0 || in.ordinal>=3 || !current_view() || !in.safe_now || !bank_ready_ ||
           !s.paths_ready || !site.known || !site.group.all_spawns_visible ||
           !site.group.uncontested_now || (site.group.lifecycle!=Lifecycle::living &&
           site.group.lifecycle!=Lifecycle::reward_available) || s.members.size()<3 || s.members.size()>32 ||
           cambc::fuel_remaining()<50000 ||
           !reference::admission_eligible(execution_,site.group.id,in.frame.clock.round))return false;
        const auto before=cambc::fuel_remaining();const int round=in.frame.clock.round;
        auto proceed=[&]{return current_view() && static_cast<int>(cambc::round_number())==round && cambc::fuel_remaining()>=30000;};
        std::array<const Actor*,3> actors{};
        int splash_radius=0;
        auto include_effects=[&](const auto& champion) {
            double radius=champion.passive()==cambc::passive_type::splash_attack?
                cambc::constants::passive_abilities::varg_splash_radius:0;
            for(const auto& ability:champion.abilities())if(ability.info() &&
                (ability.is_aoe_damage() || ability.is_cc())) {
                std::visit([&](const auto& info) {
                    if constexpr(requires {info.radius;})radius=std::max(radius,double(info.radius));
                },*ability.info());
            }
            if(!std::isfinite(radius) || radius<0 || radius>4)return false;
            splash_radius=std::max(splash_radius,static_cast<int>(std::ceil(radius)));return true;
        };
        for(int ordinal=0;ordinal<3;++ordinal) {
            actors[ordinal]=in.ordinal_actor(ordinal);const auto* a=actors[ordinal];
            if(!organization::ready(a) || a->attack_range<0 ||
               (site.group.lifecycle==Lifecycle::living && !a->can_attack && !a->completed_turn))return false;
        }
        for(int ordinal=0;ordinal<3;++ordinal) {
            if(!proceed())return false;
            const auto* a=actors[ordinal];
            bool unstunned=false;
            std::optional<bitboard> sight;
            if(a->id==in.frame.clock.self) {
                const auto& self=cambc::self();unstunned=!self.buffs().has_stun();
                if(unstunned && include_effects(self))sight.emplace(self.visible_tiles());
            } else for(const auto& ally:allies_)if(identity(ally)==a->id) {
                unstunned=!ally.buffs().has_stun();
                if(unstunned && include_effects(ally))sight.emplace(ally.visible_tiles());break;
            }
            if(!unstunned || !sight)return false;
            // SDK attack_tiles is this exact Chebyshev geometry; availability
            // is checked separately. Completed peers start contributing next
            // round, after the model's fully charged startup round.
            for(const auto& member:s.members) {
                const auto* value=camp_view_.find(in.frame.clock,member.id);
                if(!value || (value->alive && (!sight->is_set(native(value->position)) ||
                    distance(a->position,value->position)>a->attack_range)))return false;
            }
        }
        for(int a=0;a<in.frame.actor_count;++a) {
            const auto& seen=in.frame.actors[a];
            if(seen.value.team==Team::ally && seen.value.pick_ordinal>=3 &&
               s.spawns.is_set(native(seen.value.position)))return false;
        }
        // Ordinary splash must not wake a second damaging source which this
        // quote does not clear. Known zero-strength groups are harmless; an
        // omitted or damaging neighboring source remains unavailable.
        if(splash_radius)for(const auto& member:s.members) {
            if(!proceed())return false;
            const auto* current=camp_view_.find(in.frame.clock,member.id);
            if(!current || !current->alive)continue;
            const auto outsiders=around(current->position,splash_radius)&camp_spawns_&~s.spawns;
            for(const auto p:outsiders.positions()) {
                bool harmless=false;
                for(const auto& other:sites_)if(other.spawns.is_set(p)) {harmless=other.work.total_strength==0;break;}
                if(!harmless)return false;
            }
        }
        // Route through the existing terrain-proved canonical approach, then
        // along connected public source tiles. Three distinct pads prevent the
        // crew from all being told to occupy one capture tile.
        std::array<int,32> order{},steps{},queue{};steps.fill(-1);
        const int count=static_cast<int>(s.members.size());int origin=-1;
        for(int m=0;m<count;++m) {order[m]=m;if(s.members[m].spawn==s.geometry.spawn)origin=m;}
        if(origin<0)return false;
        std::sort(order.begin(),order.begin()+count,[&](int a,int b){
            const auto p=s.members[a].spawn,q=s.members[b].spawn;
            return p.y==q.y?p.x<q.x:p.y<q.y;
        });
        int head=0,tail=0;queue[tail++]=origin;steps[origin]=0;
        while(head<tail) {
            if(!proceed())return false;
            const int from=queue[head++];
            for(int to=0;to<count;++to)if(steps[to]<0 && distance(s.members[from].spawn,s.members[to].spawn)==1) {
                steps[to]=steps[from]+1;queue[tail++]=to;
            }
        }
        std::array<Point,3> pads{};std::array<int,3> captures{},banks{};
        for(int ordinal=0;ordinal<3;++ordinal) {
            if(!proceed())return false;
            const int m=order[ordinal];pads[ordinal]=s.members[m].spawn;
            const auto* a=actors[ordinal];
            const int approach=s.paths.get(native(a->position));
            if(approach==cambc::distance_field::unreachable || steps[m]<0)return false;
            captures[ordinal]=a->position==pads[ordinal]?0:
                approach+distance(s.geometry.approach,s.geometry.spawn)+steps[m];
            // A short, explicitly terrain-checked route avoids an unnecessary
            // walk through the canonical pad when already beside this holder's
            // own tile. Its positive distance still gets model slack.
            Point cursor=a->position;int direct=0;bool clear=true;
            while(cursor!=pads[ordinal] && direct<3) {
                const auto target=pads[ordinal];
                cursor.x+=(target.x>cursor.x)-(target.x<cursor.x);
                cursor.y+=(target.y>cursor.y)-(target.y<cursor.y);++direct;
                if(terrain_.is_set(native(cursor))) {clear=false;break;}
            }
            if(clear && cursor==pads[ordinal])captures[ordinal]=std::min(captures[ordinal],direct);
            banks[ordinal]=bank_paths_.get(native(pads[ordinal]));
            if(captures[ordinal]>256 || banks[ordinal]==cambc::distance_field::unreachable || banks[ordinal]>256)return false;
        }
        const auto prepared=cambc::fuel_remaining();
        crew_quote_->evaluate_into(in.frame,[&](forecast::CrewRequest& r) {
            r.ready_basics=seen(std::uint8_t{7},in.frame.clock,site.group.id);
            for(int ordinal=0;ordinal<3;++ordinal) {
                if(!proceed())return;
                const auto& a=*actors[ordinal];r.actors[ordinal]=a.id;
                const auto frame=a.id==in.frame.clock.self?in.frame:subject_frame(a);
                request_into(index,frame,r.routes[ordinal]);auto& route=r.routes[ordinal];
                auto& g=route.groups[0];g.stationary_basic={};
                g.observed.value.approach=a.position;g.observed.value.spawn=pads[ordinal];
                route.legs[0]={a.position,a.position,{0,Source::public_map}};
                route.legs[1]={a.position,pads[ordinal],{captures[ordinal],Source::public_map}};
                route.legs[2]={pads[ordinal],bank_,{banks[ordinal],Source::public_map}};
            }
        },proceed);
        ++fuel_.builds;
        const auto after=cambc::fuel_remaining();
        if(!current_view() || !crew_quote_->ready())return false;
        const auto& q=crew_quote_->result();
        const auto bit=q.status==Status::feasible?2u:1u;
        if(!(crew_logged_[index]&bit) && after>=30000) {
            const std::int64_t fields[]{round,in.frame.clock.self,site.group.id,static_cast<int>(q.status),
                static_cast<int>(q.issue),static_cast<int>(q.abort),q.clear_turns,q.capture_tick,
                q.durations[in.ordinal],actors[0]->id,actors[1]->id,actors[2]->id,
                static_cast<std::int64_t>(before),static_cast<std::int64_t>(prepared),static_cast<std::int64_t>(after),cambc::round_number()};
            runtime_trace::emit(fields,'n');crew_logged_[index]|=static_cast<std::uint8_t>(bit);
            // This one-time diagnostic turn spends no action after printing.
            return true;
        }
        if(q.status==Status::feasible && current_view()) {crew_site_=index;crew_current_=true;}
        return false;
    }
    Frame subject_frame(const Actor& a) const {
        Frame f=input_.frame;if(a.id==f.clock.self)return f;
        const auto own=f.own.actor;f.clock.self=a.id;f.own={a,0,false,false};
        f.actor_count=0;
        f.actors[f.actor_count++]=seen(own,f.clock,own.id);
        for(int i=0;i<input_.frame.actor_count;++i) {
            const auto& item=input_.frame.actors[i];
            if(item.subject!=a.id && f.actor_count<max_actors)
                f.actors[f.actor_count++]=seen(item.value,f.clock,item.subject);
        }
        return f;
    }
    bool candidates() {
        auto& in=input_;int inspected=0;
        // Measured request/cache/model setup can consume about 20k even for a
        // rejected quote. Admit it before actor/frame/cache preparation with
        // room left for the executor's 30k movement admission and local work.
        constexpr std::uint64_t model_admission=65000;
        constexpr std::uint64_t warm_self_admission=60000;
        bank_preferred_=false;in.role_ranks_current=true;
        admission_gate_.fill(0);
        quote_deferred_.fill(false);
        models_.clear_current();requests_.clear_current();
        for(auto& quote:quotes_)quote.available=false;
        crew_site_=-1;crew_current_=false;
        opportunities_now_.fill({});
        // Conversion already has an incumbent and actual resource receipts.
        // Optional production/economy/joint alternatives cannot replace that
        // stage; reserve this turn for its one fresh bank-only proof and action.
        if(execution_.active && (in.actual_payout || in.actual_bank || execution_.phase==Phase::bank ||
           execution_.bank_seen || execution_.empty_liquidation)) {
            in.independent_group=execution_.project.group;in.role_ranks_current=false;return false;
        }
        if(Policy::profile().slug=="caravan" && !control_arm()) {
            int chosen=site_index(in.current_group);
            if(chosen<0)chosen=site_index(in.proposed_group);
            if(chosen<0)chosen=preferred_site_;
            if(chosen>=0 && chosen<in.site_count && caravan_crew(chosen))return true;
        }
#if P6_JOINT_PREPARE
        // The frame is action-free for this whole candidate pass. Validate it
        // once, then reuse its exact current actor records for every source.
        const int working_group=in.frame.own.actor.ready_attack_group;
        bool current_pair=false;
        if(working_group>=0 && in.frame.valid() &&
           organization::useful(&in.frame.own.actor,working_group))
            for(int actor=0;actor<in.frame.actor_count;++actor) {
                const auto& ally=in.frame.actors[actor];
                if(ally.current(in.frame.clock,ally.subject) && ally.value.id==ally.subject &&
                   ally.value.team==Team::ally && organization::useful(&ally.value,working_group))current_pair=true;
            }
#endif
        std::array<int,8> order{};int order_count=0;
        auto add_index=[&](int i){if(i<0 || i>=in.site_count)return;
            for(int j=0;j<order_count;++j)if(order[j]==i)return;
            order[order_count++]=i;};
        add_index(site_index(in.current_group));add_index(preferred_site_);add_index(0);
        if(in.site_count)add_index((std::max(0,in.ordinal)+1)%in.site_count);
        for(int i=0;i<in.site_count;++i)add_index(i);
        for(int position=0;position<order_count;++position) {
            const int i=order[position];
            quotes_[i].available=false;auto& s=in.sites[i];
            if(crew_current_ && crew_site_==i)continue;
            if(!reference::admission_eligible(execution_,s.group.id,in.frame.clock.round)) {admission_gate_[i]=1;continue;}
            if(!opportunities_.available(i,s.group.id,in.frame.clock.round) &&
               !(reference::replacement_locked(execution_) && execution_.project.group==s.group.id)) {
                admission_gate_[i]=11;continue;
            }
            // Before reaching the approach, personal safe movement supplies
            // new visibility. Spending a full production forecast here would
            // leave too little fuel for the very move that makes it useful.
            const bool appointment=i==verification_site_ && verifying(in.frame.clock.round) &&
                in.verification_travel>=0;
            const bool personal_pad_basic=appointment && in.at_rally && in.ordinal>=0 && in.ordinal<3 &&
                in.witness_core_basic.current(in.frame.clock,s.group.id) &&
                (in.witness_core_basic.value & (1u<<in.ordinal));
#if P6_JOINT_PREPARE
            const bool foreign_continuation=current_pair && s.group.id!=working_group &&
                s.known && s.group.all_spawns_visible && s.group.lifecycle==Lifecycle::living;
#endif
            // Appointment travel is separate from the forecast's terrain-proved
            // approach and renewal legs. At the pad, only a current whole-source
            // basic mask can open the ordinary quote gate; all other admission
            // and execution checks still apply.
            if((appointment && !in.at_rally) || (!personal_pad_basic &&
               runtime_opportunities::defer_production_approach(s.known,s.group.lifecycle==Lifecycle::respawning,
               distance(in.frame.own.actor.position,s.group.approach),s.group.id,
               in.frame.own.actor.ready_attack_group,in.frame.own.actor.spawn_group))) {
                quote_deferred_[i]=true;admission_gate_[i]=2;
#if P6_JOINT_PREPARE
                if(!foreign_continuation)continue;
#else
                continue;
#endif
            }
            const auto site_admission=evaluated_[i]?warm_self_admission:model_admission;
            if(!bank_ready_) {admission_gate_[i]=3;continue;}
            if(!sites_[i].paths_ready) {admission_gate_[i]=4;continue;}
            if(!s.known) {admission_gate_[i]=5;continue;}
            if(!s.group.all_spawns_visible) {admission_gate_[i]=6;continue;}
            if(inspected==3) {admission_gate_[i]=7;continue;}
            if(cambc::fuel_remaining()<site_admission) {admission_gate_[i]=8;continue;}
            ++inspected;
            std::array<const Actor*,max_actors> actors{};int n=0;actors[n++]=&in.frame.own.actor;
            for(int j=0;j<in.frame.actor_count && n<max_actors;++j)
                if(in.frame.actors[j].value.team==Team::ally)actors[n++]=&in.frame.actors[j].value;
            auto build_actor=[&](int j,const Actor& a,const Frame& f) {
                const bool self=a.id==in.frame.clock.self;
                // Separate cold storage initialization from live forecasting.
                // The next owner turn observes again before using these bytes.
                if(self && !evaluated_[i]) {
                    admission_gate_[i]=9;
                    evaluated_[i]=std::make_unique<forecast::EvaluatedQuote>();return true;
                }
                const bool diagnostic=fuel_.start_model(s,bank_ready_,sites_[i].paths_ready,a.id);
                const forecast::Result* result=nullptr;
                if(self) {
                    { typename FuelTrace::Meter meter{fuel_,1};
                        evaluated_[i]->evaluate_into(f,[&](forecast::Request& r) {
                            {typename FuelTrace::Meter request_meter{fuel_,0};request_into(i,f,r);}
                            if(diagnostic)fuel_.requested_model(r);
                        });++fuel_.builds;
                    }
                    models_.borrow(i,j,evaluated_[i]->result());requests_.borrow(i,j,evaluated_[i]->request());
                    result=&evaluated_[i]->result();
                    admission_gate_[i]=10;
                    if(i==verification_site_)in.verification_model_complete=true;
                } else {
                    auto& r=requests_[i][j];
                    {typename FuelTrace::Meter meter{fuel_,0};request_into(i,f,r);}
                    if(diagnostic)fuel_.requested_model(r);
                    auto& model=models_[i][j];
                    { typename FuelTrace::Meter meter{fuel_,1};forecast::build_into(f,r,model);++fuel_.builds; }
                    // A visible subject's conditional alternative retains its
                    // conservative resources and the executing personal clock.
                    if(model.status==Status::feasible) {
                        model.schedule.actors[0].initial_bag.source=Source::visible_subject;
                        model.schedule.actors[0].initial_gold={0,Source::conservative};model.certificate.clock=in.frame.clock;
                    }
                    result=&model;
                }
                const auto& model=*result;
                fuel_.model_group=s.group.id;fuel_.model_status=static_cast<int>(model.status);
                fuel_.model_issue=static_cast<int>(model.issue);fuel_.clear_ticks=0;
                for(int phase=0;phase<model.phase_count;++phase)if(model.phases[phase].kind==forecast::PhaseKind::clear)
                    fuel_.clear_ticks+=model.phases[phase].finish-model.phases[phase].start;
                if(diagnostic)fuel_.built_model(model);
                if(model.status!=Status::feasible) {
                    if(self && !reference::replacement_locked(execution_))
                        opportunities_.reject(i,s.group.id,in.frame.clock.round);
                    return false;
                }
                if(self)opportunities_.inspect(i,s.group.id,in.frame.clock.round,
                    a.position==s.group.approach,true);
                const int k=s.actor_count++;s.actors[k]=a.id;s.cycles[k]=model.certificate;
                s.independent[k]=evaluate_schedule(model.schedule,in.windows,in.settings,
                    {in.frame.clock.round,in.frame.clock.self},in.frame.own_team_score);
                if(self) {
                    auto& quote=quotes_[i];quote.request=&evaluated_[i]->request();
                    quote.evaluated=evaluated_[i].get();quote.available=true;
                }
                return false;
            };
            for(int j=0;j<n;++j) {
                const auto& a=*actors[j];
#if P6_JOINT_PREPARE
                if(quote_deferred_[i] && foreign_continuation &&
                   (a.id==in.frame.clock.self || !organization::useful(&a,working_group)))continue;
#endif
                const auto admission=a.id==in.frame.clock.self && evaluated_[i]?
                    warm_self_admission:model_admission;
                if(cambc::fuel_remaining()<admission)break;
                if(a.id==in.frame.clock.self) {if(build_actor(j,a,in.frame))return true;}
                else {const auto f=subject_frame(a);if(build_actor(j,a,f))return true;}
                if(!current_view())return false;
            }
        }
        auto eligible=[&](int i){
            if(!reference::admission_eligible(execution_,in.sites[i].group.id,in.frame.clock.round))return false;
            if(!assignment_eligible(in.sites[i].group.id,in.ordinal,in.proposed_group))return false;
            for(int a=0;a<in.frame.actor_count;++a) {
                const auto& ally=in.frame.actors[a].value;
                if(ally.team==Team::ally && ally.pick_ordinal<in.ordinal &&
                    organization::useful(&ally,in.sites[i].group.id))return false;
            }
            return true;
        };
        for(int i=0;i<in.site_count;++i) {
            auto& candidate=opportunities_now_[i];const auto& site=in.sites[i];
            candidate.group=site.group.id;candidate.eligible=eligible(i);
            candidate.protected_incumbent=reference::replacement_locked(execution_) &&
                execution_.project.group==candidate.group;
            if(quotes_[i].available)candidate.kind=runtime_opportunities::Kind::quote;
            else if(admission_gate_[i]==11 || (site.known && site.group.lifecycle==Lifecycle::respawning))
                candidate.kind=runtime_opportunities::Kind::none;
            else if(quote_deferred_[i] || !site.known || !site.group.all_spawns_visible)
                candidate.kind=runtime_opportunities::Kind::inspection;
            else candidate.kind=runtime_opportunities::Kind::deferred;
#if P6_WALKING_INSPECTION_REPAIR
            // This opportunity's inspection adapter calls move(), which walks.
            // Keep quoted/crew work and nonwalking organization adapters out of
            // this refusal; never retire the source or infer a live blockage.
            runtime_routes::screen_walking_inspection(candidate,walking_component_,
                in.frame.own.actor.position,site.group.approach);
#endif
        }
        int inspection_preference=preferred_site_;
#if P6_INSPECTION_CONTINUATION_REPAIR
        inspection_preference=runtime_opportunities::inspection_preference(
            std::span<const runtime_opportunities::Candidate>{opportunities_now_.data(),static_cast<std::size_t>(in.site_count)},
            opportunities_,in.frame.clock.round,site_index(requested_group_),preferred_site_);
#endif
        const int selected=runtime_opportunities::select(
            std::span<const runtime_opportunities::Candidate>{opportunities_now_.data(),static_cast<std::size_t>(in.site_count)},
            opportunities_,in.frame.clock.round,[&](int a,int b){return compare(in.sites[a].independent[0],in.sites[b].independent[0])>0;},
            inspection_preference);
        const int best=selected>=0 && quotes_[selected].available?selected:-1;
        in.independent_group=selected>=0?sites_[selected].geometry.id:-1;
        if(best>=0 && in.frame.own.actor.bag>0 && cambc::fuel_remaining()>=70000) {
            runtime_economy::choose_into(economy_,in.frame,*quotes_[best].request,in.windows,in.settings,
                bank_paths_.get(native(in.frame.own.actor.position)),
                bank_paths_.get(native(sites_[best].geometry.approach)));
            bank_preferred_=economy_.bank_preferred;in.delivery_rank=economy_.bank_rank;
        }
        // Joint timelines are built only from presently useful visible allies;
        // solo certificates retain worst individual exposure even with support.
#if P6_JOINT_PREREQUISITES
        if(fuel_.joint_packets<24 && in.frame.clock.round-fuel_.joint_packet_round>=16) {
            int known=0,complete=0,self_models=0,foreign_models=0,available=0,pairs=0,continuations=0;
            for(int i=0;i<in.site_count;++i) {
                const auto& site=in.sites[i];known+=site.known;complete+=site.known && site.group.all_spawns_visible;
                available+=quotes_[i].available;
                for(int a=0;a<max_actors;++a)if(models_.current(i,a)) {
                    if(a==0)++self_models;else ++foreign_models;
                }
                int useful=organization::useful(&in.frame.own.actor,site.group.id);
                for(int a=0;a<in.frame.actor_count;++a) {
                    const auto* actor=in.frame.visible(in.frame.actors[a].subject);
                    if(actor && actor->team==Team::ally && organization::useful(actor,site.group.id))++useful;
                }
                if(useful>=2)++pairs;
                for(int a=1;a<max_actors;++a)if(models_.current(i,a))
                    for(int other=0;other<in.site_count;++other)if(other!=i && in.sites[other].known &&
                        models_.current(other,a))++continuations;
            }
            if(known || self_models || foreign_models || pairs) {
                fuel_.joint_packet={in.frame.clock.round,in.frame.clock.self,in.current_group,in.proposed_group,
                    in.independent_group,in.site_count,known,complete,self_models,foreign_models,available,pairs,
                    continuations,fuel_.optional_stop,static_cast<std::int64_t>(cambc::fuel_remaining()),
                    fuel_.sites_visited,fuel_.sites_cutoff,0};
                fuel_.joint_pending=true;
            }
        }
#endif
        runtime_joint::populate(in,models_,requests_,quotes_,Policy::profile().slug,
            [&]{const auto remaining=cambc::fuel_remaining();
                if(fuel_.joint_checks>0 && fuel_.joint_previous>=remaining)
                    fuel_.joint_max_gap=std::max(fuel_.joint_max_gap,fuel_.joint_previous-remaining);
                fuel_.joint_previous=remaining;
                if(fuel_.joint_checks++==0)fuel_.joint_before=remaining;
                if(remaining<70000)++fuel_.joint_denials;
                return remaining>=70000;});
        if(crew_current_ && crew_quote_->matches_frame(in.frame)) {
            auto& site=in.sites[crew_site_];const auto& q=crew_quote_->result();
            const auto rank=evaluate_schedule(q.schedule,in.windows,in.settings,
                {in.frame.clock.round,in.frame.clock.self},in.frame.own_team_score);
            if(rank.eligible()) {
                site.crew_current=true;site.actor_count=3;
                for(int a=0;a<3;++a) {site.actors[a]=crew_quote_->request().actors[a];site.cycles[a]=q.certificates[a];site.independent[a]={};}
                site.joint_rank=rank;
            } else crew_current_=false;
        }
        int renewal=best;
        if(renewal<0)for(int i=0;i<in.site_count;++i)if(sites_[i].paths_ready) {renewal=i;break;}
        const bool need_delivery=in.frame.own.actor.bag>0 && !in.delivery_rank.eligible();
        const bool need_intercept=Policy::profile().slug=="public-modes";
        if(renewal>=0 && bank_ready_ && need_delivery && cambc::fuel_remaining()>=65000) {
            if(!delivery_) {
                delivery_=std::make_unique<forecast::EvaluatedQuote>();return true;
            }
            delivery_->evaluate_into(in.frame,[&](forecast::Request& r){request_into(renewal,in.frame,r,true);});
            if(!current_view())return false;
            in.delivery_rank=runtime_economy::evaluate(delivery_->result(),in.frame,in.windows,in.settings);
        }
        if(renewal>=0 && bank_ready_ && need_intercept && cambc::fuel_remaining()>=70000) {
            const auto bank_request=request(renewal,in.frame,true);
            const auto intercept=runtime_intercept::quote(in,bank_request,target_ehp_,
                {cambc::constants::kill_gold,Source::queried_setting});
            in.contest_rank=intercept.rank;in.intercept_target=intercept.target;
        }
        return false;
    }
    void inspect_passage() {
        if(Policy::profile().slug!="bridgecrew" || !bank_ready_)return;
        const int width=cambc::map::width(),height=cambc::map::height();
        auto free=[&](Point p){return p.x>=0 && p.y>=0 && p.x<width && p.y<height && !terrain_.is_set(native(p));};
        auto degree=[&](Point p){
            int count=0;for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
                if((x||y) && free({p.x+x,p.y+y}))++count;
            return count;
        };
        const int index=site_index(input_.independent_group);if(index<0)return;
        Point current=sites_[index].geometry.approach,previous=current;
        std::array<Point,3> narrow{};int length=0;Point entry{};
        for(int step=0;step<256 && current!=bank_;++step) {
            // Passage data is optional. Leave the action reserve intact and
            // abandon partial evidence rather than suspend a stale scan.
            if(!current_view() || cambc::fuel_remaining()<50000)return;
            const auto delta=bank_paths_.next_move_from(native(current));if(!delta)return;
            const Point next{current.x+delta->x,current.y+delta->y};
            if(next==current)return;
            if(degree(current)==2 && !camp_spawns_.is_set(native(current))) {
                if(length==0)entry=previous;
                if(length==3) {length=0;return;}
                narrow[length++]=current;
            } else if(length) {
                if(entry==narrow[0] || camp_spawns_.is_set(native(entry)) || camp_spawns_.is_set(native(current)))return;
                auto& p=input_.passage;p.available=true;p.tiles=narrow;p.length=length;
                const bool returning=input_.frame.own.actor.bag>0;
                p.direction=returning?0:1;p.entry=returning?entry:current;p.exit=returning?current:entry;
                if(distance(input_.frame.own.actor.position,p.entry)>3 &&
                    distance(input_.frame.own.actor.position,p.exit)>3) {p={};return;}
                p.alternate=sites_[index].geometry.retreat;
                const auto visible=cambc::vision::visible_tiles();const auto occupied=blocked();
                p.personally_clear=true;
                for(int k=0;k<length;++k)p.personally_clear &= visible.is_set(native(narrow[k])) &&
                    (!occupied.is_set(native(narrow[k])) || input_.frame.own.actor.position==narrow[k]);
                p.exit_safe=visible.is_set(native(p.exit)) && !occupied.is_set(native(p.exit)) &&
                    survives(organization::local_exposure(input_.frame.own.actor,incoming(p.exit)));
                for(const auto& a:allies_)if(identity(a)!=input_.frame.clock.self && a.gem_count()>0 &&
                    distance(point(a.position()),entry)<=2)p.loaded_ally_returning=true;
                return;
            }
            previous=current;current=next;
        }
    }
    bool move(Point target,int contact=-1) {
        typename FuelTrace::Meter meter{fuel_,3};
        if(!fuel_.values[6])fuel_.mark(6);
        if(!current_view()) {fuel_.move_reason=1;return false;}
        auto& me=cambc::self();if(!me.can_move()) {fuel_.move_reason=2;return false;}
        if(point(me.position())==target) {fuel_.move_reason=3;return false;}
        if(cambc::fuel_remaining()<24000) {fuel_.move_reason=4;return false;}
        auto walls=blocked();auto goals=tile(target)&~walls;
        if(goals.is_empty())goals=around(target,1)&~walls;
        if(goals.is_empty()) {fuel_.move_reason=5;return false;}
        cambc::distance_field field;bitboard visited;
        field.compute_to(walls,goals,tile(point(me.position())),visited,256);
        // No game action occurs while candidate steps are compared. Query the
        // same live camp terms once, then apply the identical bound locally.
        struct Danger { Point position;int range;double strength; };
        std::vector<Danger> dangers;
        if(camp_view_.current(input_.frame.clock)) {
            const auto camps=camp_view_.values(input_.frame.clock);dangers.reserve(camps.size());
            for(const auto& m:camps)if(m.alive && m.awake)
                dangers.push_back({m.position,m.attack_range,m.strength});
        } else for(const auto& m:camps_) {
            if(!current_view() || cambc::fuel_remaining()<12000) {fuel_.move_reason=8;return false;}
            if(m.is_alive() && m.is_awake())dangers.push_back({point(m.position()),m.attack_range(),m.strength()});
        }
        auto step_exposure=[&](Point p) {
            double amount=0;
            for(const auto& danger:dangers)if(distance(p,danger.position)<=danger.range)amount+=danger.strength;
            return immediate_exposure(p,amount);
        };
        for(int retry=0;retry<3 && cambc::fuel_remaining()>=16000;++retry) {
            const auto legal=blocked().reachable_tiles(me.position(),1).positions();
            const auto* partner=input_.frame.visible(contact);
            auto best=point(me.position());auto best_d=field.get(me.position());
            const auto original_distance=best_d;bool best_contact=partner!=nullptr;
            auto current=input_.frame.own.actor;current.health=me.health();current.max_health=me.max_health();
            for(const auto p:legal) {
                if(!current_view() || cambc::fuel_remaining()<8000) {fuel_.move_reason=8;return false;}
                auto exposure=step_exposure(point(p));
                // Support/combat may have changed our HP before this move.
                // Enemy geometry remains a conservative current-turn snapshot.
                exposure.health=current.health;exposure.max_health=current.max_health;
                if(!survives(exposure))continue;
                const auto d=field.get(p);
                bool retains=partner && me.visible_tiles_from(p).is_set(native(partner->position));
                if((retains && !best_contact) || (retains==best_contact &&
                    (d<best_d || (d==best_d && d<original_distance && tile_index(point(p))<tile_index(best))))) {
                    best=point(p);best_d=d;best_contact=retains;
                }
            }
            if(best==point(me.position()) || best_d==cambc::distance_field::unreachable) {fuel_.move_reason=6;return false;}
            if(!current_view()) {fuel_.move_reason=1;return false;}
            camp_view_.invalidate();
            if(me.move_absolute(native(best))) {fuel_.actions|=1;fuel_.move_reason=9;walked_=true;++revision_;return true;}
            fuel_.move_reason=7;collision_=true;++revision_;
            // A hidden obstruction can reveal a hostile absent from the
            // complete observation. Fresh occupancy alone does not refresh its
            // damage bound; end this action view until next turn's observe().
            action_view_live_=false;return false;
        }
        if(fuel_.move_reason<0)fuel_.move_reason=8;return false;
    }
    template<class Target> bool fight(const Target& target,bool control_watch=false) {
        typename FuelTrace::Meter meter{fuel_,4};
        auto& me=cambc::self();if(!current_view() || cambc::fuel_remaining()<16000 || !target.is_visible() || target.health()<=0)return false;
        bool acted=false;
        const auto options=runtime_combat::observe_options(me,walked_);
        bool cc_eligible=!target.buffs().has_stun();
        if constexpr(std::is_same_v<Target,cambc::champion>)
            cc_eligible=cc_eligible && target.passive()!=cambc::passive_type::stun_immunity;
        const auto kind=std::is_same_v<Target,cambc::camp>?
            runtime_combat::TargetKind::camp:runtime_combat::TargetKind::enemy;
        const auto cast=options.choose_cast(target.position(),kind,control_watch,cc_eligible);
        if(cast && current_view()) {
            camp_view_.invalidate();
            acted=runtime_combat::use_cast(me.abilities()[cast.slot],target,cast.mode,walked_);
            if(acted) {fuel_.actions|=4;fuel_.target=identity(target);}
        }
        if(!target.is_visible() || target.health()<=0)return acted;
        if(me.can_attack() && me.attack_tiles().is_set(target.position())) {
            if(!current_view())return acted;
            camp_view_.invalidate();me.attack(target);fuel_.actions|=2;fuel_.target=identity(target);acted=true;
            if constexpr(std::is_same_v<Target,cambc::camp>) {
                for(const auto& s:sites_)if(s.spawns.is_set(target.spawn_position())) {
                    successful_attack_group_=s.geometry.id;successful_attack_round_=cambc::round_number();break;
                }
            }
        }
        if(target.is_visible() && target.health()>0 && !me.can_attack() && me.can_cast())
            for(const auto& a:me.abilities())if(a.info() && a.is_attack_reset() && a.is_ready() && a.can_target_self() &&
                !(walked_ && std::visit([](const auto& info){return info.exclusive_with_movement;},*a.info()))) {
                if(!current_view())return acted;
                camp_view_.invalidate();
                if(a.use()) {
                    fuel_.actions|=32;
                    if(me.can_attack() && target.is_visible() && target.health()>0 &&
                       me.attack_tiles().is_set(target.position()) && current_view()) {
                        me.attack(target);fuel_.actions|=2;fuel_.target=identity(target);
                        acted=true;
                        if constexpr(std::is_same_v<Target,cambc::camp>)
                            for(const auto& site:sites_)if(site.spawns.is_set(target.spawn_position())) {
                                successful_attack_group_=site.geometry.id;successful_attack_round_=cambc::round_number();break;
                            }
                    }
                }
                break;
            }
        return acted;
    }
    bool protect(int participant=-1) {
        auto& me=cambc::self();if(!current_view() || !me.can_cast() || cambc::fuel_remaining()<16000)return false;
        for(const auto& a:me.abilities()) {
            if(!a.info() || !a.is_ready() || (walked_ && std::visit([](const auto& info){return info.exclusive_with_movement;},*a.info())))continue;
            if(a.is_shield() && a.can_target_self() && incoming(point(me.position()))>0)
                if(current_view()) {camp_view_.invalidate();if(a.use())return true;}
            if(!a.is_heal())continue;
            if(a.can_target_self() && me.health()<me.max_health() && a.valid_tiles().is_set(me.position()) && a.heal_amount()>0)
                if(current_view()) {camp_view_.invalidate();if(a.use(me))return true;}
            for(const auto& ally:allies_)if(identity(ally)!=identity(me) && (participant<0 || identity(ally)==participant) &&
                ally.health()<ally.max_health() && a.can_target_allies() && a.valid_tiles().is_set(ally.position()) && a.heal_amount(ally)>0)
                if(current_view()) {camp_view_.invalidate();if(a.use(ally))return true;}
        }
        return false;
    }
    void shop() {
        const auto entered_round=cambc::round_number();
        auto& me=cambc::self();
        // Three bounded, currently affordable bundles. No future gold, refunds
        // or field Health healing is credited by the cycle forecast.
        for(int bundle=0;bundle<3;++bundle) {
            auto item=cambc::item_type::power_shard_of_strength;unsigned cap=100;
            if(monster_shards_<5) {item=cambc::item_type::monster_shard_of_strength;cap=5-monster_shards_;}
            else if(jungle_shards_<5) {item=cambc::item_type::jungle_shard_of_health;cap=5-jungle_shards_;}
            const auto amount=std::min(cap,me.gold_count()/item.cost());
            if(!amount)break;
            if(!me.can_buy(item,amount)) {
#if P6_CAPITAL_CALLER_PACKET
                if(capital_.pending) {capital_.item=static_cast<int>(static_cast<cambc::item_type::enum_type>(item));
                    capital_.amount=static_cast<int>(amount);capital_.can_buy=0;}
#endif
                shop_pending_=true;break;
            }
#if P6_CAPITAL_CALLER_PACKET
            if(capital_.pending) {capital_.item=static_cast<int>(static_cast<cambc::item_type::enum_type>(item));
                capital_.amount=static_cast<int>(amount);capital_.can_buy=1;}
#endif
            if(cambc::round_number()!=entered_round)return;
            camp_view_.invalidate();me.buy(item,amount);
            if(item==cambc::item_type::monster_shard_of_strength)monster_shards_+=amount;
            if(item==cambc::item_type::jungle_shard_of_health)jungle_shards_+=amount;
        }
    }
    void recover() {
        fuel_.stage=11;
        const bool cached=camp_view_.current(input_.frame.clock) && immediate_.complete &&
            immediate_.clock==input_.frame.clock;
        if(!current_view() || cambc::fuel_remaining()<(cached?20000u:30000u))return;
        auto& me=cambc::self();recovering_=true;
        if(input_.frame.own.at_spawn) {recovering_=false;return;}
        // No visible hostile champion is treated as harmless during recall.
        // Spell reach can exceed basic range, and its future motion is unknown.
        struct Danger {Point position;int range;double strength;};
        std::vector<Danger> dangers;
        if(cached) {
            const auto camps=camp_view_.values(input_.frame.clock);dangers.reserve(camps.size());
            for(const auto& m:camps)if(m.alive && m.awake)
                dangers.push_back({m.position,m.attack_range,m.strength});
        } else for(const auto& m:camps_) {
            if(!current_view() || cambc::fuel_remaining()<20000)return;
            if(m.is_alive() && m.is_awake())dangers.push_back({point(m.position()),m.attack_range(),m.strength()});
        }
        const auto origin=point(me.position());bool safe=enemies_.empty();
        for(const auto& danger:dangers)if(distance(origin,danger.position)<=danger.range+1)safe=false;
        if(safe) {if(current_view()) {camp_view_.invalidate();me.recall();fuel_.actions|=8;}return;}
        std::vector<Point> enemy_positions;enemy_positions.reserve(enemies_.size());
        if(cached)for(const auto& enemy:observed_enemies_)enemy_positions.push_back(enemy.position);
        else for(const auto& enemy:enemies_) {
            if(!current_view() || cambc::fuel_remaining()<20000)return;
            enemy_positions.push_back(point(enemy.position()));
        }
        const auto legal=blocked().reachable_tiles(me.position(),1).positions();
        auto clearance=[&](Point p){
            double damage=0;
            for(const auto& danger:dangers)if(distance(p,danger.position)<=danger.range)damage+=danger.strength;
            const auto exposure=immediate_exposure(p,damage);
            damage=exposure.observations_complete?exposure.incoming_total:
                std::numeric_limits<double>::infinity();
            double score=-damage*10000;
            for(const auto enemy:enemy_positions)score+=std::min(12,distance(p,enemy))*100;
            for(const auto& danger:dangers)score+=std::min(10,distance(p,danger.position));
            return score;
        };
        Point best=origin;double best_score=clearance(best);
        for(const auto p:legal) {
            if(!current_view() || cambc::fuel_remaining()<10000)return;
            const auto score=clearance(point(p));
            if(score>best_score) {best=point(p);best_score=score;}
        }
        if(best!=point(me.position()) && me.can_move()) {if(current_view()) {camp_view_.invalidate();me.move_absolute(native(best));}}
        else {
            const auto spawn=cambc::map::spawn_region(cambc::team::ally).positions();
            if(!spawn.empty())move(point(spawn.front()));
        }
    }
    bool bank() {
        fuel_.stage=12;
        auto& me=cambc::self();if(!current_view() || !me.gem_count() || !bank_exists_)return false;
        if(distance(point(me.position()),altar_)>cambc::constants::submit_gems_range) {move(bank_);return true;}
        if(!cambc::vision::visible_tiles().is_set(native(altar_)))return true;
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))if(point(lock.position())==altar_) {
            if(me.can_attack() && me.attack_tiles().is_set(lock.position()) && current_view()) {camp_view_.invalidate();me.attack(lock);}
            return true;
        }
        if(!current_view())return false;
        camp_view_.invalidate();me.submit_gems(native(altar_));fuel_.actions|=16;
        // submit_gems is void; successful return plus observed live channel is
        // the receipt. A lock-converted attack was excluded above.
        if(me.is_submitting_gems()) {
            own_bank_started_=true;own_bank_round_=input_.frame.clock.round;
            own_bank_bag_=input_.frame.own.actor.bag;own_bank_score_=input_.frame.own_team_score;
        }
        if(me.is_submitting_gems() && execution_.active) {
            execution_.phase=Phase::bank;execution_.last_round=input_.frame.clock.round;
            confirm_tribute_started(execution_,input_.frame,
                seen(Action::tribute,input_.frame.clock,input_.frame.clock.self));
        }
        return true;
    }
    int site_index(int group) const {
        for(int i=0;i<input_.site_count;++i)if(sites_[i].geometry.id==group)return i;
        return -1;
    }
    bool safe_hold(int index) const {
        const auto& s=input_.sites[index];const auto& a=input_.frame.own.actor;
        if(!s.known || s.group.lifecycle!=Lifecycle::reward_available || !s.group.uncontested_now || a.spawn_group!=s.group.id)return false;
        auto e=organization::local_exposure(a,incoming(a.position));
        e.incoming_total*=input_.settings.capture_rounds+1;return survives(e);
    }
#if P6_REPLICAS_STARTUP_DISPATCH_REPAIR
    void work(int group,bool* renewed=nullptr,bool explicit_startup=false) {
#else
    void work(int group,bool* renewed=nullptr) {
#endif
        const bool pending_renewal=renewed && execution_.active &&
            (execution_.bank_seen || execution_.empty_liquidation);
        const int proposed=group;
        group=reference::resolve_work_group(execution_,group,input_.independent_group,
            input_.frame.clock.round,[&](int candidate){
                const int index=site_index(candidate);if(index<0)return false;
                if(reference::replacement_locked(execution_) && execution_.project.group==candidate)return true;
                if(crew_current_ && crew_site_==index && crew_quote_ && crew_quote_->matches_frame(input_.frame))return true;
#if P6_REPLICAS_STARTUP_DISPATCH_REPAIR
                // The caller attests the policy's just-produced named startup
                // work. Bypass only independent assignment diversity; retain
                // admission, current opportunity kind, cooldown and all later
                // observation/quote/executor checks.
                if(explicit_startup && candidate==proposed) {
                    auto current=opportunities_now_[index];
                    current.eligible=reference::admission_eligible(execution_,candidate,input_.frame.clock.round);
                    return runtime_opportunities::usable(current,opportunities_,index,input_.frame.clock.round);
                }
#endif
                return runtime_opportunities::usable(opportunities_now_[index],opportunities_,index,input_.frame.clock.round);
            });
        fuel_.stage=10;fuel_.group=group;
        if(!current_view() || cambc::fuel_remaining()<14000)return;
        auto turnback=[&](int reason) {
            const int index=site_index(group>=0?group:proposed);
            if(index<0 || turnback_logged_[index] || fuel_.turnback_pending)return;
            const auto& site=input_.sites[index];const auto& actor=input_.frame.own.actor;
            const bool current=evaluated_[index] && evaluated_[index]->matches_frame(input_.frame);
            const auto* model=current?&evaluated_[index]->result():nullptr;
            fuel_.turnback={input_.frame.clock.round,actor.id,proposed,group,index,admission_gate_[index],
                site.known,site.group.all_spawns_visible,quote_deferred_[index],static_cast<int>(site.group.lifecycle),
                sites_[index].paths_ready,bank_ready_,current,model?static_cast<int>(model->status):-2,
                model?static_cast<int>(model->issue):-1,inspections_,distance(actor.position,site.group.approach),
                static_cast<std::int64_t>(fuel_.values[3]),static_cast<std::int64_t>(fuel_.values[5]),
                static_cast<std::int64_t>(fuel_.measure()),reason,actor.bag,execution_.active};
            turnback_logged_[index]=true;fuel_.turnback_pending=true;
        };
        const int i=site_index(group);if(i<0) {turnback(1);if(input_.frame.own.actor.bag>0)bank();return;}
        requested_group_=group;
        const bool crew=crew_current_ && crew_site_==i && crew_quote_ && crew_quote_->matches_frame(input_.frame);
        if(!crew && runtime_opportunities::deferred(opportunities_now_[i]) && !reference::replacement_locked(execution_)) {
            turnback(4);return;
        }
        if(evaluated_[i] && evaluated_[i]->matches_frame(input_.frame)) {
            fuel_.model_group=group;
            const auto& q=evaluated_[i]->result();fuel_.model_status=static_cast<int>(q.status);
            fuel_.model_issue=static_cast<int>(q.issue);fuel_.clear_ticks=0;
            for(int j=0;j<q.phase_count;++j)if(q.phases[j].kind==forecast::PhaseKind::clear)
                fuel_.clear_ticks+=q.phases[j].finish-q.phases[j].start;
        }
        auto& observed=input_.frame;auto& site=input_.sites[i];
        observed.group=seen(site.group,observed.clock,group);
        ReferenceInput actual{.frame=observed};actual.assigned_group=group;
        if(quotes_[i].available)actual.frame.group=quotes_[i].request->groups[0].observed;
        if(crew)actual.frame.group=crew_quote_->request().routes[input_.ordinal].groups[0].observed;
        actual.safe_now=input_.safe_now;actual.at_approach=distance(observed.own.actor.position,
            actual.frame.group.value.approach)<=1;
        actual.at_bank=input_.at_bank;actual.at_renewal=observed.own.actor.position==site.group.approach;
        actual.altar_legal=actual.at_bank && altar_legal();actual.recall_safe=observed_incoming_==0;
        actual.completion={observed.clock,group,input_.actual_payout,input_.actual_bank,
                           input_.actual_payout||input_.actual_bank};
        actual.inspection_failed=site.known && site.group.lifecycle==Lifecycle::respawning;
        auto& me=cambc::self();
        // No action occurred since observe(). Reuse that same complete camp
        // snapshot rather than query every unrelated visible camp before bind.
        if(observed.own.actor.can_attack) {
            for(const auto& target:attack_targets_)if(target.site==i) {
                if(!observed_self_attack_)observed_self_attack_=me.attack_tiles();
                if(observed_self_attack_->is_set(native(target.position)))actual.fresh_target_attack_legal=true;
            }
        }
        if(!current_view())return;
        std::optional<StepProposal> action;
        const auto bind_before=fuel_.measure();
        if(fuel_.heavy.pending && fuel_.heavy.serial==fuel_.serial)fuel_.heavy.prebind=bind_before;
        if((input_.actual_payout || execution_.payout_seen || execution_.phase==Phase::bank) &&
            observed.own.actor.bag>0 && bank_ready_ && sites_[i].paths_ready) {
            // Bank-only work uses the same owned, prepared quote slot. No cold
            // request/result object or second evaluator is needed here either.
            const auto* bank_quote=delivery_ && delivery_->ready() && delivery_->matches_frame(actual.frame) &&
                delivery_->request().shape==forecast::Shape::bank_only && delivery_->request().project_group==group?
                delivery_.get():nullptr;
            if(!bank_quote) {
                // Delivery itself remains legal without an expensive new full
                // cycle proof; never spend the movement reserve rebuilding it.
                if(!evaluated_[i] || cambc::fuel_remaining()<60000) {bank();return;}
                evaluated_[i]->evaluate_into(actual.frame,[&](forecast::Request& r){request_into(i,actual.frame,r,true);});
                bank_quote=evaluated_[i].get();
            }
            if(!current_view())return;
            action=reference::choose_forecast_replacing_into(execution_,actual,bank_quote);
        }
        else if(crew)
            action=reference::choose_crew_replacing_into(execution_,actual,crew_quote_.get());
        else if(quotes_[i].available)
            action=reference::choose_forecast_replacing_into(execution_,actual,quotes_[i].evaluated);
        else if(input_.actual_bank || execution_.bank_seen || execution_.empty_liquidation || input_.actual_payout)
            action=reference::choose(execution_,actual);
        fuel_.spend(2,bind_before);
        if(!current_view())return;
        if(renewed && action && reference::renewed_without_action(pending_renewal,execution_,actual,*action)) {
            *renewed=true;return;
        }
        if(!action) {
            if(safe_hold(i))return;
            if(observed.own.actor.bag>0) {bank();return;}
            // Seeing one member establishes the group's lifecycle, but does
            // not yet establish complete local occupancy/membership evidence.
            // Continue the same bounded safe inspection before asking a fresh
            // full forecast to authorize any attack or capture work.
            if(quote_deferred_[i] || !site.known || (!site.group.all_spawns_visible && site.group.lifecycle!=Lifecycle::respawning)) {
                if(last_inspection_group_!=group) {last_inspection_group_=group;inspections_=0;}
                const bool exact=observed.own.actor.position==site.group.approach;
                if(exact)++inspections_;
                if(opportunities_.inspect(i,group,observed.clock.round,exact,
                    site.known && site.group.all_spawns_visible,!site_visited_[i])) {
                    turnback(2);
                    return;
                }
                move(site.group.approach);return;
            }
            if(site.group.lifecycle==Lifecycle::respawning) {
                move(site.group.retreat);return;
            }
            turnback(3);return;
        }
        fuel_.action=static_cast<int>(action->action);
        switch(action->action) {
        case Action::tribute: bank();break;
        case Action::recall:
#if P6_CAPITAL_CALLER_PACKET
            capital_recovery(5);
#endif
#if P6_REPLICAS_CALLER_PACKET
            caller_recovery(5);
#endif
            recover();break;
        case Action::move:
            if(action->phase==Phase::bank)bank();
            else if(execution_.bank_seen || execution_.empty_liquidation)move(site.group.approach);
            else if(action->phase==Phase::hold)move(actual.frame.group.value.spawn);
            else move(site.group.approach);
            break;
        case Action::attack:case Action::inspect: {
            const auto options=runtime_combat::observe_options(me,walked_);
            const auto* target=runtime_combat::select_group_target(camps_,[&](const auto& m){
                return sites_[i].spawns.is_set(m.spawn_position());},options,
                [](const auto& m){return identity(m);});
            if(target) {
                fight(*target);
                if(target->is_visible() && target->is_alive() && !cambc::self().attack_tiles().is_set(target->position()))
                    move(point(target->position()));
                // Fresh masks after movement protect ranged move/attack sharing.
                if(target->is_visible() && target->is_alive())fight(*target);
            } else if(site.group.lifecycle==Lifecycle::reward_available)move(actual.frame.group.value.spawn);
            else move(site.group.approach);
            break;
        }
        default:break;
        }
    }
    void support(const organization::Decision& d) {
        const auto* owner=input_.frame.visible(d.participant);
        // Actual visible hostile interception; no unseen distress or chase.
        const cambc::champion* target=nullptr;
        for(const auto& e:enemies_)if(identity(e)==input_.legal_enemy_target && owner &&
            distance(point(e.position()),owner->position)<=e.attack_range()+1)target=&e;
        for(const auto& e:enemies_)if(owner && distance(point(e.position()),owner->position)<=e.attack_range()+1 &&
            (!target || (identity(*target)!=input_.legal_enemy_target &&
             (e.health()<target->health() || (e.health()==target->health() && identity(e)<identity(*target))))))target=&e;
        const bool scheduled=d.intent!=organization::Intent::control_watch || control_arm() || input_.frame.clock.round%3==0;
        if(target) {fight(*target,scheduled);protect(d.participant);
            if(target->is_visible() && target->health()>0)move(point(target->position()),d.participant);return;}
        if(d.intent==organization::Intent::control_watch) {
            if(scheduled)for(const auto& m:camps_)if(m.is_alive() && owner &&
                distance(point(m.position()),owner->position)<=m.attack_range()) {
                bool witnessed=false;
                for(const auto& record:damage_.records())if(record.id==identity(m) && record.damage_round>=input_.frame.clock.round-1)witnessed=true;
                for(const auto& site:sites_)if(site.spawns.is_set(m.spawn_position()) &&
                    successful_attack_group_==site.geometry.id && successful_attack_round_>=input_.frame.clock.round-1)witnessed=true;
                if(witnessed && fight(m,true))return;
            }
        }
        protect(d.participant);
        if(owner)move(owner->position,d.participant);
        else if(d.explicit_waypoint)move(d.waypoint);
        else work(input_.independent_group);
    }
    static constexpr bool control_arm() { return P6_CONTROL!=0; }
public:
    // All state has member defaults. A user-provided constructor avoids the
    // redundant whole-object zero pass imposed by make_unique value-init.
    Bot() {}
    void turn() {
        action_view_live_=true;
        fuel_.begin();
#if P6_CAPITAL_CALLER_PACKET
        capital_={};
        if constexpr(P6_CAPITAL_CALLER_PACKET==2)
            capital_.pending=input_.frame.own.actor.id==2 &&
                ((fuel_.round>=761 && fuel_.round<=765) || (fuel_.round>=1082 && fuel_.round<=1086));
        else capital_.pending=input_.frame.own.actor.id==5 && fuel_.round>=143 && fuel_.round<=147;
#endif
#if P6_REPLICAS_CALLER_PACKET
        fuel_.caller_pending=Policy::profile().slug=="replicas" && input_.frame.own.actor.id==6 &&
            (fuel_.round==98 || fuel_.round==99);
        fuel_.caller_policy=false;fuel_.caller_code=0;fuel_.caller_intent=fuel_.caller_group=-1;
        fuel_.caller_reason=0;fuel_.caller_recovering=recovering_;fuel_.caller_shop=false;
#endif
        struct Report { Bot& bot;~Report(){
            auto& f=bot.fuel_;f.project=bot.execution_.active?bot.execution_.project.group:-1;
            f.phase=static_cast<int>(bot.execution_.phase);
            if(bot.input_.frame.clock.round==f.round && f.stage>=5 && !f.obs_abort) {
                f.bag=bot.input_.frame.own.actor.bag;f.gold=bot.input_.frame.own.gold;
                f.score=bot.input_.frame.own_team_score;f.spawn=bot.input_.frame.own.actor.spawn_group;
                f.receipt=(bot.input_.actual_payout?1:0)|(bot.input_.actual_bank?2:0);
            }
#if P6_REPLICAS_CALLER_PACKET
            bot.caller_snapshot();
#endif
#if defined(P6_REPLICAS_STARTUP_PACKET) && P6_REPLICAS_STARTUP_PACKET
            if constexpr(requires { bot.policy_.startup_packet_pending; }) {
                auto& policy=bot.policy_;
                if(policy.startup_packet_pending) {
                    const auto remaining=cambc::fuel_remaining();
                    policy.startup_packet[39]=static_cast<std::int64_t>(remaining);
                    if(remaining>=32000)runtime_trace::emit(policy.startup_packet,'a');
                    policy.startup_packet_pending=false;
                }
            }
#endif
#if P6_GEM_MODEL_PACKET
            auto& packet=forecast::gem_failure_packet;
            if(packet.pending && packet.fields[1]==bot.input_.frame.clock.self && cambc::fuel_remaining()>=32000) {
                packet.fields[47]=f.round;runtime_trace::emit(packet.fields,'u');packet.pending=false;return;
            }
#endif
#if P6_CAPITAL_CALLER_PACKET
            if(bot.capital_report())return;
#endif
            f.report();
        }} report{*this};
        auto& me=cambc::self();
        walked_=false;shop_pending_=false;
        if(!me.is_alive()) {fuel_.stage=1;shop();if(execution_.active)execution_.retire(Failure::observed_unavailable);
            recovering_=false;own_bank_started_=false;hold_group_=-1;return;}
        // Static geometry work may span deliberately yielded rounds; gameplay
        // always starts from a fresh turn after initialization has completed.
        if(!initialized_) {fuel_.stage=2;initialize();fuel_.mark(1);return;}
        // Existing valid channels always survive clock/role deadlines.
        if(me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) {fuel_.stage=3;return;}
        // Stun forbids new actions. Retain pre-stun resource baselines so a
        // completed payout/channel can still be verified on the next free turn.
        if(me.buffs().has_stun())return;
#if P6_WALKING_INSPECTION_REPAIR
        if(prepare_walking_component()) {fuel_.stage=15;fuel_.reason="walking-component";return;}
#endif
        if(prepare_initial_quote()) {fuel_.stage=4;return;}
#if P6_JOINT_PREPARE
        if(prepare_joint_storage()) {fuel_.stage=14;fuel_.reason="joint-cold-storage";return;}
#endif
        if(Policy::profile().slug=="caravan" && !control_arm() && !crew_quote_) {
            if(cambc::fuel_remaining()>=70000)crew_quote_=std::make_unique<forecast::EvaluatedCrewQuote>();
            fuel_.stage=13;return;
        }
        if(prepare_verification_paths()) {fuel_.stage=4;return;}
        // Cold quote storage is prepared only when its observed local site or
        // carried bag needs it. That bounded preparation yields in candidates;
        // unrelated sites never delay the first safe movement.
        int preferred=preferred_site_;
        const int wanted=reference::replacement_locked(execution_)?execution_.project.group:
            verification_site_>=0 && verifying(cambc::round_number())?sites_[verification_site_].geometry.id:
            requested_group_>=0?requested_group_:execution_.active?execution_.project.group:-1;
        for(int i=0;i<static_cast<int>(sites_.size());++i)if(sites_[i].geometry.id==wanted) {preferred=i;break;}
        prepare_paths(preferred);
        if(cambc::fuel_remaining()<30000)return;
        fuel_.stage=5;fuel_.mark(2);if(!observe())return;fuel_.mark(3);fuel_.stage=6;
        if(!current_view())return;
        // Deliver one-shot notices before field shopping. A successful buy
        // yields; the next owner turn observes its new stats before forecasting.
        if(!input_.actual_payout && !input_.actual_bank && !input_.observed_failure &&
           !input_.hidden_collision && cambc::fuel_remaining()>=16000) {
            const auto old_gold=me.gold_count();
            if(!current_view())return;
            shop();
            if(!current_view())return;
            const auto new_gold=me.gold_count();if(!current_view())return;
            if(new_gold!=old_gold) {
                // Purchases can change live stats. Yield instead of paying for
                // a second observation and decision in this owner turn. Keep
                // prior resource baselines until the next fresh observation.
                return;
            }
        }
        struct Remember {
            ActorState& state;Frame& frame;
            ~Remember(){state.previous_observed=true;state.previous_bag=frame.own.actor.bag;
                state.previous_gold=frame.own.gold;state.previous_score=frame.own_team_score;
                state.previous_channel=frame.own.actor.channel;}
        } remember{execution_,input_.frame};
        if(input_.frame.own.at_spawn && me.health()<me.max_health())return;
        if(!input_.safe_now || recovering_) {
            // A safety diversion does not erase an already owned conversion
            // or its pending bank/renewal receipt. Production can be retired.
            const bool converting=input_.actual_payout || execution_.payout_seen || execution_.bank_seen ||
                execution_.empty_liquidation || execution_.phase==Phase::bank;
            if(execution_.active && !converting)execution_.retire(Failure::unsafe);
#if P6_REPLICAS_CALLER_PACKET
            caller_recovery(!input_.safe_now?1:2);
#endif
#if P6_CAPITAL_CALLER_PACKET
            capital_recovery(!input_.safe_now?1:2);
#endif
            recover();return;
        }
        // Gold-only work must reach an actual legal purchase. Finish any owned
        // capture/conversion/renewal first; an empty free actor may then return
        // to shop rather than retry unaffordable gem cycles with unchanged stats.
        if(shop_pending_ && !execution_.active && input_.frame.own.actor.bag==0 &&
           !input_.frame.own.at_spawn) {
            fuel_.reason="bootstrap-shop-return";
#if P6_REPLICAS_CALLER_PACKET
            caller_recovery(3);
#endif
#if P6_CAPITAL_CALLER_PACKET
            capital_recovery(3);
#endif
            recover();return;
        }
        // Legal winning tribute precedes optional organization work.
        if(input_.at_bank && altar_legal() && input_.frame.own.actor.bag>0 &&
           input_.frame.own_team_score+input_.frame.own.actor.bag>=input_.settings.win_gems) {bank();return;}
        const int active=site_index(input_.current_group);
        fuel_.stage=7;fuel_.mark(4);if(candidates())return;fuel_.mark(5);fuel_.stage=8;
        if(!current_view())return;
        inspect_passage();
        if(!current_view())return;
        auto d=policy_.choose(input_,control_arm());
#if P6_REPLICAS_CALLER_PACKET
        if(fuel_.caller_pending) {
            fuel_.caller_policy=true;fuel_.caller_intent=static_cast<int>(d.intent);fuel_.caller_group=d.group;
            if(d.reason) {
                fuel_.caller_reason=2166136261u;
                for(const auto* c=d.reason;*c;++c)fuel_.caller_reason=(fuel_.caller_reason^static_cast<unsigned char>(*c))*16777619u;
            }
        }
#endif
        if(!current_view())return;
        fuel_.stage=9;fuel_.reason=d.reason;fuel_.group=d.group;fuel_.intent=static_cast<int>(d.intent);fuel_.participant=d.participant;
        // Policies receive actual milestones before the executor finishes the
        // retained stage. Their bookkeeping cannot cancel safe capture/channel.
        if(active>=0 && safe_hold(active) && !input_.actual_payout) {
            if(hold_group_!=input_.current_group) {hold_group_=input_.current_group;hold_started_=input_.frame.clock.round;}
            if(input_.frame.clock.round-hold_started_<=2*(input_.settings.capture_rounds+1)+2) {
                execution_.phase=Phase::hold;return;
            }
            execution_.retire(Failure::expired);hold_group_=-1;
            if(!bank())move(input_.sites[active].group.retreat);return;
        }
        hold_group_=-1;
        if(execution_.active && (input_.actual_payout || input_.actual_bank || execution_.phase==Phase::bank || execution_.bank_seen || execution_.empty_liquidation)) {
            const bool movement=d.explicit_waypoint &&
                (d.intent==organization::Intent::approach || d.intent==organization::Intent::rally ||
                 d.intent==organization::Intent::alternate);
            const auto* participant=input_.frame.visible(d.participant);
            const bool support=participant && participant->team==Team::ally && participant->alive &&
                (d.intent==organization::Intent::escort || d.intent==organization::Intent::intercept ||
                 d.intent==organization::Intent::control_watch);
            bool renewed=false;
            // No fresh production quote was built during owned conversion.
            // Only an immediately dispatchable policy action can replace the
            // completed stage's idle fallback; independent recovery stays put.
#if P6_IDLE_RENEWAL_REPAIR
            const bool idle_yield=reference::idle_renewal_may_yield(execution_,input_.frame,
                d.intent==organization::Intent::independent,input_.safe_now,recovering_,shop_pending_);
            work(execution_.project.group,movement || support || idle_yield?&renewed:nullptr);
            // No quote was built during conversion. Yield after its physical
            // closure rather than dispatch the old independent choice again.
            if(idle_yield && renewed)return;
#else
            work(execution_.project.group,movement || support?&renewed:nullptr);
#endif
            if(!renewed || !current_view())return;
        }
        if(bank_preferred_ && d.intent!=organization::Intent::escort &&
            d.intent!=organization::Intent::intercept && d.intent!=organization::Intent::control_watch) {
            bank();return;
        }
        switch(d.intent) {
        case organization::Intent::bank:if(!bank())work(input_.independent_group);break;
        case organization::Intent::recover:
#if P6_CAPITAL_CALLER_PACKET
            capital_recovery(4);
#endif
#if P6_REPLICAS_CALLER_PACKET
            caller_recovery(4);
#endif
            recover();break;
        case organization::Intent::approach:case organization::Intent::rally:case organization::Intent::alternate:
            if(d.explicit_waypoint)move(d.waypoint,d.participant);else work(d.group);break;
        case organization::Intent::escort:case organization::Intent::intercept:case organization::Intent::control_watch:support(d);break;
        default:
#if P6_REPLICAS_STARTUP_DISPATCH_REPAIR
            if constexpr(requires { policy_.explicit_startup_work(input_,d); })
                work(d.group<0?input_.independent_group:d.group,nullptr,policy_.explicit_startup_work(input_,d));
            else work(d.group<0?input_.independent_group:d.group);
#else
            work(d.group<0?input_.independent_group:d.group);
#endif
            break;
        }
    }
};
template<class Policy> int run() {
    std::unique_ptr<Bot<Policy>> bot;
    while(true) {
        try {
            if(cambc::is_draft()) {
                const auto selected=cambc::draft::current_draft(cambc::team::ally);
                for(const auto choice:Policy::profile().roster) {
                    const auto value=draft_type(choice);
                    if(std::find(selected.begin(),selected.end(),value)==selected.end()) {cambc::draft::pick(value);break;}
                }
            } else {
                if(!bot)bot=std::make_unique<Bot<Policy>>();
                bot->turn();
            }
        } catch(const std::exception& e) {std::cout<<"p6 "<<Policy::profile().slug<<" error: "<<e.what()<<'\n';}
        catch(...) {std::cout<<"p6 "<<Policy::profile().slug<<" error: unknown exception\n";}
        cambc::end_turn();
    }
}
} // namespace p6::runtime
