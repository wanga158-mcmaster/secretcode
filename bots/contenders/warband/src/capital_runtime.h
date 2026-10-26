#pragma once
#include <cambc.h>
#include "config.h"
#include <iostream>
#include "capital_micro.h"
#include "capital_model.h"
#include "capital_intent.h"
#include "capital_exit.h"
#include "capital_contact_native.h"
#include "capital_work_bridge.h"
#include "capital_conversion_runtime.h"
#include "capital_service_native.h"
#include "capital_encounter_native.h"
#include "capital_service_controller_native.h"
#include "capital_party_runtime.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <variant>
#include <vector>

// Complete rolling capital/work interpreter. Models rank finite services;
// they do not certify safety against unknown opponents. Persistent camp damage
// survives the body's actual recovery; a new episode never fabricates a clear.
namespace wb::capital {
using cambc::vec2;
using cambc::team;
inline int dist(vec2 a,vec2 b) {return static_cast<int>(a.chebyshev_distance(b));}
struct Report {
    std::uint32_t signature=0,round=0,owner=0,kind=0;
    std::int32_t x=-1,y=-1,group=-1,bag=0;
    float hp=0,max_hp=0,strength=0;
    std::uint32_t mode=0;
};
static_assert(sizeof(Report)==48);
class Runtime {
    struct Group {
        cambc::map::camp_group native;
        std::vector<vec2> positions;
        std::vector<float> hp,member_damage;
        std::vector<int> member_range;
        float total_hp=0,damage=0;
        int range=0,seen=-1,unavailable_until=0;
        bool reward=false,respawning=false;
        bool refused=false;
        float refused_max_hp=0,refused_strength=0,refused_focus=0,refused_work=0;
    };
    enum class Mode {Work,Return,Bank};
    std::vector<Group> groups_;
    std::vector<cambc::champion> allies_;
    std::vector<Report> reports_;
    cambc::bitboard terrain_,spawn_,altar_,static_camps_,enemy_spawn_,danger_,quiet_goals_,source_tiles_;
    cambc::distance_field exit_field_;
    std::vector<vec2> exit_route_;
    std::size_t exit_cursor_=0;
    unsigned exit_refusals_=0;
    std::vector<vec2> spawn_positions_,altars_;
    std::vector<float> hazard_;
    std::vector<std::uint8_t> terrain_bits_,camp_blocked_;
    std::vector<std::uint16_t> exit_distances_;
    bool exit_field_dirty_=false,release_active_=false;
    struct Threat {vec2 position;float damage;int reach;bool champion=false;};
    std::vector<Threat> threats_;
    std::vector<unsigned> awake_camps_;
    struct Npc {int id,group,member;vec2 position;int range;float damage;};
    std::vector<Npc> npc_;
    std::vector<int> tile_group_;
    int width_=0,height_=0,area_=0,stage_=0,selected_=-1,ordinal_=0;
    int previous_bag_=0,bank_round_=-1,last_retarget_=-100,stuck_=0,resume_=-1;
    std::size_t init_group_=0,observation_cursor_=0;
    vec2 last_position_{-1,-1};
    Mode mode_=Mode::Work;
    bool ready_=false;
    int audit_phase_=0;
    float owned_camp_multiplier_=1;
    unsigned investment_cost_=0,last_wallet_=0;
    struct NamedParty {
        using Host=party_runtime::Sdk;using Driver=party_runtime::Runtime<Host>;
        std::unique_ptr<Host> host;std::unique_ptr<Driver> driver;
        party_program::Contract contract{};
        std::vector<cambc::camp> catalogue;
        std::vector<std::uint8_t> cells;
        std::array<int,3> roles{{-1,-1,-1}};
        std::array<int,3> draft_ordinal{{-1,-1,-1}};
        std::array<int,3> quiet{{-1,-1,-1}},home{{-1,-1,-1}};
        party_program_native::Independent independent{};
        party_program_native::Price measured_price{};
        struct Measurement {
            party_program::Epoch source{};party_program_native::Primitive primitive{};
            party::Body before{},after{};party_program_native::FuelInterval whole{},guard{},command{},readback{};
            unsigned stage{},status{},phase{},entry{},required{},accepted{},receipts{};
            bool pending{},after_known{},measured{};
        } measurement{};
        std::uint64_t input{},grant{};unsigned round{},cursor{},stage{};int group{-1};
        unsigned catalogue_group{},nomination_group{},nomination_member{},nomination_home{},prepare_phase{};int nomination_distance{std::numeric_limits<int>::max()};
        unsigned measurements{},emitted{},owned_heads{};bool measurement_pending{},stop_requested{},physical_handoff{};
        unsigned state_emitted{},reported_stage{~0u},reported_phase{~0u},reported_prepare{~0u},reported_prepare_phase{~0u},report_round{};bool reported_handoff{};
        enum class Prepare {NotReached,Ineligible,Funding,Epoch,Promoted,Admitted,Ready};
        Prepare preparation{Prepare::NotReached};std::array<std::uint64_t,7> preparation_stops{};std::uint64_t preparation_calls{};
        unsigned preparation_fuel{},foreground_floor{};
        enum class Retirement {None,AcceptedOrder,Capacity,NoGroup,QuietPoints,Stations,LateStart};
        Retirement retirement{Retirement::None};unsigned accepted_order_count{},source_count{};
        bool supported{},retired{},ready{},metadata_armed{};
    };
    static NamedParty& named_party(){static NamedParty value;return value;}
    bool named_party_wire_reserved() const {
        if constexpr(!config::named_party_controller)return false;
        else {
            // This explicit protocol profile must have an executable pricing
            // class before it excludes the legacy wire/priority operators.
            if(!config::named_party_hold_probe||(!config::named_party_measure_now&&!named_party().measured_price.bounded))return false;
            if(ready_&&(npc_.size()>party_runtime::max_sources||spawn_positions_.size()<3||altars_.empty()))return false;
            if(named_party().supported)return true; // immutable accepted draft/identities, captured below
            if(allies_.size()!=party::actors)return false;std::array<bool,party::actors> roles{};
            for(const auto& actor:allies_){const auto type=actor.champion_name();
                const int role=type==cambc::champion_type::varg?0:type==cambc::champion_type::willow?1:type==cambc::champion_type::vendetta?2:-1;
                if(role<0||roles[static_cast<unsigned>(role)])return false;roles[static_cast<unsigned>(role)]=true;}
            return true;
        }
    }
    static party_program_native::FuelMark named_party_mark(unsigned epoch){
        const auto actor_before=cambc::self();const int before_id=static_cast<int>(static_cast<cambc::handle_champion_t>(actor_before));
        const auto round_before=cambc::round_number();const auto remaining=cambc::fuel_remaining();
        const int after_id=static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()));const auto round_after=cambc::round_number();
        return {static_cast<unsigned>(remaining),int(round_after),remaining<=std::numeric_limits<unsigned>::max()
            &&before_id==after_id&&round_before==epoch&&round_after==epoch};
    }
    static party_program_native::FuelInterval named_party_interval(party_program_native::FuelMark a,party_program_native::FuelMark b){
        const bool known=a.known&&b.known&&a.round==b.round&&b.remaining<=a.remaining;return {a,b,known?a.remaining-b.remaining:0,known};
    }
    void named_party_emit(unsigned epoch){
        if constexpr(!config::named_party_measure_now)return;
        auto& state=named_party();if(!state.measurement_pending||state.emitted>=12||cambc::fuel_remaining()<75000||!same_epoch(epoch))return;
        const auto& m=state.measurement;const auto flags=[](const party::Body& b){return unsigned(b.move)|(unsigned(b.attack)<<1)|(unsigned(b.cast)<<2);};
        std::cout<<"wb_party_cal_v1 "<<m.source.owner<<' '<<m.source.round<<' '<<m.source.input<<' '<<m.source.sample
            <<' '<<state.contract.id<<' '<<state.contract.group<<' '<<m.phase<<' '<<m.status<<' '<<m.stage
            <<' '<<unsigned(m.primitive.kind)<<' '<<m.primitive.target<<' '<<m.primitive.slot<<' '<<m.primitive.destination.x<<' '<<m.primitive.destination.y
            <<' '<<m.before.position.x<<' '<<m.before.position.y<<' '<<m.after.position.x<<' '<<m.after.position.y
            <<' '<<m.before.hp<<' '<<m.after.hp<<' '<<m.before.bag<<' '<<m.after.bag<<' '<<flags(m.before)<<' '<<flags(m.after)
            <<' '<<m.entry<<' '<<m.required<<' '<<m.accepted<<' '<<m.receipts<<' '<<m.after_known<<' '<<m.measured;
        for(const auto* interval:{&m.whole,&m.guard,&m.command,&m.readback})std::cout<<' '<<interval->known<<' '<<interval->before.round<<' '<<interval->after.round
            <<' '<<interval->before.remaining<<' '<<interval->after.remaining<<' '<<interval->cost;
        std::cout<<' '<<state.measurements<<' '<<state.owned_heads<<' '<<state.stop_requested<<'\n';state.measurement_pending=false;++state.emitted;
    }
    void named_party_report(unsigned epoch){
        if constexpr(!config::named_party_measure_now)return;
        auto& s=named_party();const unsigned phase=s.driver?unsigned(s.driver->session().program().phase()):~0u;
        if(s.state_emitted>=24||(s.reported_stage==s.stage&&s.reported_phase==phase&&s.reported_handoff==s.physical_handoff
            &&s.reported_prepare==unsigned(s.preparation)&&s.reported_prepare_phase==s.prepare_phase&&epoch-s.report_round<128)
            ||cambc::fuel_remaining()<75000||!same_epoch(epoch))return;
        std::cout<<"wb_party_state_v3 "<<self_id()<<' '<<epoch<<' '<<s.stage<<' '<<s.ready<<' '<<s.retired<<' '<<s.input<<' '<<s.owned_heads<<' '<<phase
            <<' '<<s.measurements<<' '<<s.stop_requested<<' '<<s.physical_handoff;
        if(s.driver){const auto& d=s.driver->session();const auto& c=s.driver->custody();std::cout<<' '<<d.wire_owned()<<' '<<d.needs_reconciliation()<<' '<<d.telemetry().entries<<' '<<d.telemetry().attempts
            <<' '<<c.source_gems<<' '<<c.credited<<' '<<c.broken;}
        else std::cout<<" 0 0 0 0 0 0 0";
        std::cout<<' '<<unsigned(s.preparation)<<' '<<s.preparation_calls<<' '<<s.preparation_fuel;
        for(const auto count:s.preparation_stops)std::cout<<' '<<count;
        std::cout<<' '<<s.prepare_phase<<' '<<s.cursor<<' '<<s.catalogue_group<<' '<<s.nomination_group<<' '<<s.nomination_member<<' '<<s.nomination_home;
        std::cout<<' '<<unsigned(s.retirement)<<' '<<s.accepted_order_count<<' '<<s.source_count<<' '<<spawn_positions_.size()<<' '<<altars_.size()<<' '<<s.foreground_floor;
        std::cout<<'\n';s.reported_stage=s.stage;s.reported_phase=phase;s.reported_handoff=s.physical_handoff;s.reported_prepare=unsigned(s.preparation);s.reported_prepare_phase=s.prepare_phase;s.report_round=epoch;++s.state_emitted;
    }
    static bool named_party_accept(void* context,const party_program::Frame& frame,party_program_native::Independent& out){
        auto& self=*static_cast<Runtime*>(context);auto& state=named_party();namespace sn=service_native;namespace sp=service_program;
        if(!sn::current()||sn::memory().owner!=frame.self.id||sn::memory().round!=frame.source.round)return false;
        auto& owner=sn::memory();if(!owner.state||owner.purchase)return false;
        // Native already returned these actual self quantities. No party
        // primitive buys/sells/uses inventory, so this input's initial kit and
        // inventory remain dated facts. World/source knowledge stays UNKNOWN;
        // pending forces a full SDK refresh before the rolling physical word.
        auto actual=owner.actual;actual.sample=++owner.sample;actual.pose={frame.self.position.x,frame.self.position.y};
        actual.alive=frame.self.alive;actual.hp=frame.self.hp;actual.stunned=frame.self.stunned;actual.bag=frame.self.bag;actual.gold=frame.self.gold;
        actual.channel=frame.self.channel==party::Channel::None?sp::Channel::None:frame.self.channel==party::Channel::Submit?sp::Channel::Submit:sp::Channel::Other;
        actual.permissions=(frame.self.move?unsigned(sp::Move):0u)|(frame.self.attack?unsigned(sp::Attack):0u)|(frame.self.cast?unsigned(sp::Cast):0u);
        actual.selected_group=actual.source_family=-1;actual.source_life=actual.source_revision=actual.route_revision=actual.occupancy_revision=actual.lock_revision=actual.foe_revision=0;
        actual.capability_revision=service_native::mix(owner.effect_generation,actual.permissions);actual.available_fuel=cambc::fuel_remaining();actual.fresh=actual.observed=true;
        if(!owner.state->observe(actual).accepted)return false;owner.actual=actual;owner.pending=true;
        const auto kind=frame.self.bag?sp::Kind::Bank:frame.self.hp<frame.self.max_hp?sp::Kind::Recovery:sp::Kind::Acquire;
        const auto grant=sn::publish(kind,{sp::FirstKind::FundedPreparation,++owner.action,0,24000,0});
        if(!sp::granted(grant)||!self.same_epoch(static_cast<unsigned>(frame.source.round)))return false;
        out={frame.source,grant.lease.serial,owner.input,true,true,true};state.independent=out;return true;
    }
    static bool named_party_verify(void*,const party_program_native::Independent& grant,const party_program::Frame& frame){
        const auto& actual=named_party().independent;return grant.source==frame.source&&grant.source==actual.source&&grant.grant==actual.grant
            &&grant.programme==actual.programme&&grant.complete&&grant.funded&&grant.accepted&&service_native::owned()
            &&service_native::memory().grant.lease.serial==grant.grant;
    }
    static bool named_party_price(void*,const party_program::Frame& frame,const party_program::Offer& offer,
                                  const party_program_native::Word& word,unsigned suffix,party_program_native::Price& out){
        // Until exact SDK-word calibration supplies this model, physical
        // words remain unpriced. Never manufacture a bounded zero-cost word.
        const auto& measured=named_party().measured_price;
        if constexpr(config::named_party_measure_now){
            auto& state=named_party();if(!state.host||!word.count||offer.source!=frame.source||word.source!=frame.source||suffix>=word.count)return false;
            const auto& observed=state.host->last_observe_cost();if(!observed.known||!observed.observed||observed.source!=frame.source||observed.after>observed.before)return false;
            out={};out.model=service_native::mix(state.contract.id,word.token);out.domain=state.contract.domain.id;
            out.measure_now=true;out.readback[0]=observed.before-observed.after;out.exit_tail=party_program_native::Options{}.command_reserve;return true;
        }
        if(!measured.bounded||!measured.model||measured.domain!=named_party().contract.domain.id||offer.source!=frame.source
            ||word.source!=frame.source||suffix>=word.count)return false;
        out=measured;return true;
    }
    static bool named_party_policy(void* context,const party_program::Contract& contract,const party_native::Observation& raw,party_runtime::PolicyInput& out){
        auto& self=*static_cast<Runtime*>(context);auto& state=named_party();namespace q=party_program;
        if constexpr(!config::named_party_hold_probe)return false;
        if(!raw.frame.fresh||raw.frame.owner!=self.self_id()||!self.same_epoch(static_cast<unsigned>(raw.frame.round))
            ||bank_native::state().promoted||!self.named_party_wire_reserved())return false;
        // This is the named conditional reply family. Freshly known contact
        // refutes it, including contact revealed by a preceding refused move.
        for(const auto& foe:cambc::champions(team::enemy))if(foe.is_visible())return false;
        out={};out.source.owner=raw.frame.owner;out.source.round=raw.frame.round;out.source.input=raw.frame.input;
        out.source.geometry=contract.domain.world;out.source.foes=contract.domain.replies;out.source.locks=contract.domain.assumptions;
        out.source.order=contract.id;out.source.life=contract.life;out.grant=++state.grant;out.risk=q::Risk::ConditionalModel;
        out.horizon_round=raw.frame.round+1;out.fresh=true;out.priority_excluded=true;
        out.station_refusals_closed=out.other_champions_excluded=out.foes_covered=true;
        out.stun_covered=out.locks_covered=out.ongoing_effects_covered=out.relocation_covered=true;
        out.economy_events_closed=raw.frame.round>=contract.start;
        if(!state.supported)return false;out.draft_ordinal=state.draft_ordinal;
        // Inventory/investment stays with the actually accepted rolling
        // interpreter. This probe never calls a future grant actual renewal.
        out.capital_obligation_clear=false;out.renewal_observed=0;return self.same_epoch(static_cast<unsigned>(raw.frame.round));
    }
    void named_party_prepare(unsigned epoch,unsigned foreground_floor){
        if constexpr(!config::named_party_controller)return;
        named_party_report(epoch);
        auto& state=named_party();++state.preparation_calls;state.foreground_floor=foreground_floor;
        const auto work_floor=std::max(18000u,foreground_floor);
        const auto stop=[&](NamedParty::Prepare reason){state.preparation=reason;++state.preparation_stops[unsigned(reason)];};
        if(state.ready){stop(NamedParty::Prepare::Ready);return;}
        if(state.retired||!ready_||!named_party_wire_reserved()){stop(NamedParty::Prepare::Ineligible);return;}
        state.preparation_fuel=static_cast<unsigned>(cambc::fuel_remaining());
        if(state.preparation_fuel<75000){stop(NamedParty::Prepare::Funding);return;}
        if(!same_epoch(epoch)){stop(NamedParty::Prepare::Epoch);return;}
        if(bank_native::state().promoted){stop(NamedParty::Prepare::Promoted);return;}
        stop(NamedParty::Prepare::Admitted);
        if(state.stage==0){
            for(const auto& actor:allies_){const auto type=actor.champion_name();const int role=type==cambc::champion_type::varg?0:type==cambc::champion_type::willow?1:2;
                state.roles[static_cast<unsigned>(role)]=static_cast<int>(static_cast<cambc::handle_champion_t>(actor));}
            // Gameplay current_draft is not our accepted-order authority.
            // prime() already validates the preserved accepted draft packet
            // against these actual own identities. Missing proof stays unknown.
            const auto& accepted=order_native::state();state.accepted_order_count=accepted.own.count;
            if(!accepted.primed||!accepted.own.complete||accepted.own.count!=3){state.retired=true;state.retirement=NamedParty::Retirement::AcceptedOrder;return;}
            std::array<bool,3> used{};
            for(unsigned role=0;role<3;++role){for(unsigned i=0;i<accepted.own.count;++i){const auto& member=accepted.own.members[i];
                    if(member.id==state.roles[role]&&member.tie_key_known&&member.tie_key>=0&&member.tie_key<3)state.draft_ordinal[role]=member.tie_key;}
                const int rank=state.draft_ordinal[role];if(rank<0||used[static_cast<unsigned>(rank)]){state.retired=true;state.retirement=NamedParty::Retirement::AcceptedOrder;return;}
                used[static_cast<unsigned>(rank)]=true;}
            unsigned total=0;for(const auto& group:groups_)total+=group.positions.size();
            state.source_count=total;
            if(total>party_runtime::max_sources||spawn_positions_.size()<3||altars_.empty()){state.retired=true;state.retirement=NamedParty::Retirement::Capacity;return;}
            state.catalogue.reserve(total);state.cells.reserve(static_cast<unsigned>(area_));state.supported=true;state.stage=1;return;
        }
        if(state.stage==1){
            if(state.prepare_phase==0){
                unsigned remaining=128;while(state.cursor<static_cast<unsigned>(area_)&&remaining--&&cambc::fuel_remaining()>work_floor){
                    const unsigned cell=state.cursor++;state.cells.push_back(std::uint8_t((terrain_bits_[cell/8]>>(cell%8))&1u));}
                if(state.cursor==static_cast<unsigned>(area_)){state.cursor=0;state.prepare_phase=1;}
                return; // Never attach catalogue/nomination to the last cell grant.
            }
            if(state.prepare_phase==1){
                // One actual public group export; the admitted whole catalogue
                // has at most32 entries. Allocation itself remains atomic.
                if(state.catalogue_group<groups_.size()){
                    const auto camps=groups_[state.catalogue_group++].native.camps();for(const auto& camp:camps)state.catalogue.push_back(camp);return;}
                state.prepare_phase=2;return;
            }
            unsigned remaining=128;while(state.nomination_group<groups_.size()&&remaining--&&cambc::fuel_remaining()>work_floor){
                const auto& candidate=groups_[state.nomination_group];const int id=static_cast<int>(static_cast<cambc::handle_camp_group_t>(candidate.native));
                if(id<0||id>31||candidate.positions.size()<3||candidate.positions.size()>party::max_camps){++state.nomination_group;state.nomination_member=state.nomination_home=0;continue;}
                const auto p=candidate.positions[state.nomination_member],home=spawn_positions_[state.nomination_home];const int score=dist(p,home);
                if(score<state.nomination_distance){state.nomination_distance=score;state.group=int(state.nomination_group);}
                if(++state.nomination_home==spawn_positions_.size()){state.nomination_home=0;if(++state.nomination_member==candidate.positions.size()){state.nomination_member=0;++state.nomination_group;}}
            }
            if(state.nomination_group!=groups_.size())return;
            if(state.group<0){state.retired=true;state.retirement=NamedParty::Retirement::NoGroup;return;}state.stage=2;return;
        }
        const auto& group=groups_[static_cast<unsigned>(state.group)];const auto anchor=group.positions.front();
        if(state.stage==2){
            unsigned remaining=128;while(state.cursor<static_cast<unsigned>(area_)&&remaining--&&cambc::fuel_remaining()>work_floor){
                const int cell=int(state.cursor++);if(state.cells[static_cast<unsigned>(cell)]||camp_blocked_[static_cast<unsigned>(cell)])continue;
                const vec2 p{cell%width_,cell/width_};
                auto insert=[&](auto& points,auto less){for(unsigned i=0;i<points.size();++i)if(points[i]<0||less(cell,points[i])){for(unsigned j=unsigned(points.size()-1);j>i;--j)points[j]=points[j-1];points[i]=cell;break;}};
                if(hazard_[static_cast<unsigned>(cell)]==0)insert(state.quiet,[&](int a,int b){const int da=dist({a%width_,a/width_},anchor),db=dist({b%width_,b/width_},anchor);return da!=db?da<db:a<b;});
                if(spawn_.is_set(p)&&hazard_[static_cast<unsigned>(cell)]==0)insert(state.home,[](int a,int b){return a<b;});
            }
            if(state.cursor!=static_cast<unsigned>(area_))return;
            if(state.quiet[2]<0||state.home[2]<0){state.retired=true;state.retirement=NamedParty::Retirement::QuietPoints;return;}state.stage=3;return;
        }
        if(state.stage==3){
            std::array<vec2,3> stations{};unsigned count=0;
            for(int dy=-1;dy<=1&&count<3;++dy)for(int dx=-1;dx<=1&&count<3;++dx){if(!dx&&!dy)continue;const vec2 p{anchor.x+dx,anchor.y+dy};
                if(inside(p)&&!state.cells[static_cast<unsigned>(index(p))]&&!camp_blocked_[static_cast<unsigned>(index(p))])stations[count++]=p;}
            if(count!=3){state.retired=true;state.retirement=NamedParty::Retirement::Stations;return;}
            auto& c=state.contract;c.group=static_cast<int>(static_cast<cambc::handle_camp_group_t>(group.native));
            c.life=1;c.capture_mask=7;c.require_useful_support=false;c.max_refusals=1;c.max_no_progress=static_cast<unsigned>(area_)+cambc::constants::capture_time;
            c.camp_count=static_cast<unsigned>(group.native.camps().size());for(unsigned i=0;i<c.camp_count;++i)c.camps[i]=static_cast<int>(static_cast<cambc::handle_camp_t>(group.native.camps()[i]));
            const auto point=[](vec2 p){return party::Point{p.x,p.y};};vec2 altar=altars_.front();for(const auto p:altars_)if(dist(p,anchor)<dist(altar,anchor))altar=p;
            std::array<bool,party::max_camps> assigned{};
            for(unsigned i=0;i<3;++i){const auto quiet=state.quiet[i],home=state.home[i];auto& m=c.members[i];m.id=state.roles[i];m.role=static_cast<party::Role>(i);
                unsigned capture=0;int closest=std::numeric_limits<int>::max();for(unsigned k=0;k<group.positions.size();++k)if(!assigned[k]){
                    const int distance=dist(stations[i],group.positions[k]);if(distance<closest){closest=distance;capture=k;}}
                assigned[capture]=true;
                m.rendezvous=m.release=m.renewal={quiet%width_,quiet/width_};m.spawn={home%width_,home/width_};m.station=point(stations[i]);m.capture=point(group.positions[capture]);m.altar=point(altar);
                m.rendezvous_quiet=m.release_quiet=m.renewal_quiet=true;}
            // Foreign NPC tiles stay blocked; selected tiles are revalidated
            // by the SDK and become actual capture destinations after clear.
            for(unsigned i=0;i<groups_.size();++i)if(int(i)!=state.group)for(const auto p:groups_[i].positions)state.cells[static_cast<unsigned>(index(p))]=1;
            state.stage=4;return;
        }
        if(state.stage==4){
            party_runtime::Hooks hooks{{this,&Runtime::named_party_price,&Runtime::named_party_accept,&Runtime::named_party_verify},&Runtime::named_party_policy,config::named_party_measure_now};
            state.host=std::make_unique<NamedParty::Host>(group.native.camps(),state.catalogue,party_runtime::Geometry{width_,height_,state.cells,true},hooks);
            state.stage=5;return;
        }
        auto& c=state.contract;const unsigned now=cambc::round_number();party_wire::Header header;
        if(self_id()==state.roles[0])header={static_cast<std::uint16_t>(now+3),static_cast<std::uint8_t>(c.group)};
        else {std::array<std::uint16_t,4> wire{};state.host->read_words(wire);header=party_wire::decode_header(wire[0]);if(header.start<=now||header.group!=c.group)return;}
        const int end=std::min(int(cambc::constants::max_rounds)-1,party_wire::max_round);if(header.start>=end){state.retired=true;state.retirement=NamedParty::Retirement::LateStart;return;}
        c.start=header.start;c.commit_by=c.arrive_by=c.work_by=c.capture_by=c.bank_by=c.renew_by=c.abort_by=end;
        c.id=service_native::mix(service_native::mix(1,unsigned(c.group+1)),unsigned(c.start));for(const auto id:state.roles)c.id=service_native::mix(c.id,unsigned(id+1));
        c.domain={c.id,1,c.id,c.id,c.id,c.id,c.start,end,party_program::Risk::ConditionalModel};
        party_program_native::Admission admission;auto& a=admission.programme;a.canonical=a.accepted_roles=a.exclusive_wire=a.legacy_publish_disabled=a.fixed_priority_protocol=true;
        a.conditional_risk_admitted=config::named_party_hold_probe;a.complete_independent_interpreters.fill(config::complete_service_controller);
        admission.trusted_milestone_producer=admission.final_fence_one_command=true;
        party_program_native::Options options;options.allow_measure_now=config::named_party_measure_now;options.allow_paid_exit_handoff=config::named_party_measure_now;
        state.driver=std::make_unique<NamedParty::Driver>(*state.host,c,self_id(),admission,options);state.ready=true;
    }
    bool named_party_dispatch(unsigned epoch){
        if constexpr(!config::named_party_controller)return false;
        named_party_report(epoch);
        auto& state=named_party();if(!state.ready||!state.driver||!same_epoch(epoch))return false;
        if constexpr(!config::named_party_measure_now)if(!state.measured_price.bounded)return false;
        named_party_emit(epoch);
        if(state.round!=epoch){state.round=epoch;++state.input;}auto& owner=service_native::memory();
        if constexpr(config::named_party_measure_now)if(state.stop_requested||state.owned_heads>=16)state.driver->request_abort();
        party_program::Epoch source{self_id(),int(epoch),state.input,0,owner.effect_generation,state.contract.id,state.contract.id,state.contract.id,state.contract.id,state.contract.life};
        auto head=state.driver->head(source);if(!same_epoch(epoch))return true;
        if(head.status==party_program_native::Status::NoOffer&&!head.claimed&&!head.wire_owned)return false;
        if(head.claimed)++state.owned_heads;
        bool physical=false;for(unsigned attempt=0;attempt<3;++attempt){
            // Session actions are outside the rolling primitive hooks. Mark
            // its memo unknown BEFORE an attempt, including a thrown command.
            ++owner.effect_generation;owner.pending=true;owner.active=false;
            const auto entries=state.driver->session().telemetry().entries;
            party_program_native::FuelMark before;if constexpr(config::named_party_measure_now)before=named_party_mark(epoch);
            const auto result=state.driver->step();physical=physical||result.accepted_steps!=0;
            if constexpr(config::named_party_measure_now){const auto after=named_party_mark(epoch);const auto& t=state.driver->session().telemetry();
                if(t.entries!=entries&&t.measured_word.price.measure_now&&t.measured_word.count==1){
                    auto& m=state.measurement;m={};m.source=t.source;m.primitive=t.measured_word.steps[0];m.before=t.measured_before;m.after=result.after_known?result.after.self:t.measured_after;
                    m.whole=named_party_interval(before,after);m.guard=t.guards[0];m.command=t.commands[0];m.readback=t.readbacks[0];
                    m.phase=unsigned(state.driver->session().program().phase());m.status=unsigned(result.status);m.stage=unsigned(t.stage);
                    m.entry=t.entry_fuel;m.required=t.required;m.accepted=result.accepted_steps;m.receipts=result.actual_receipts;m.after_known=result.after_known;m.measured=true;
                    state.measurement_pending=true;++state.measurements;}
                if(result.accepted_steps)state.stop_requested=true;}
            if(!same_epoch(epoch))return true;
            if(!result.attempted||result.status==party_program_native::Status::Pending||result.physical_released)break;
        }
        const auto phase=state.driver->session().program().phase();const bool pending=state.driver->session().needs_reconciliation();
        if(phase==party_program::Phase::Handoff||phase==party_program::Phase::Complete){
            if(!pending&&!state.physical_handoff){state.physical_handoff=true;const auto& actual=state.driver->session().actual().self;selected_=-1;resume_=-1;exit_route_.clear();
                mode_=actual.bag?Mode::Bank:actual.hp<actual.max_hp?Mode::Return:Mode::Work;(void)service_native::refresh();}
            return pending||physical;
        }
        return head.claimed||pending||physical;
    }
    void mark(int phase) {if constexpr(config::phase_audit) audit_phase_=phase;}
    bool same_epoch(unsigned epoch) const {
        const auto now=cambc::round_number();
        if(now==epoch) return true;
        if constexpr(config::phase_audit)
            std::cout<<"wb_fuel_stage "<<self_id()<<' '<<epoch<<' '<<now<<' '<<audit_phase_<<' '<<cambc::fuel_remaining()<<'\n';
        return false;
    }
    int self_id() const {return static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()));}
    int index(vec2 p) const {return p.y*width_+p.x;}
    bool inside(vec2 p) const {return p.x>=0&&p.y>=0&&p.x<width_&&p.y<height_;}
    float hazard(vec2 p) const {return inside(p)?hazard_[index(p)]:100000.f;}
    bool reserve(unsigned n=15000) const {return cambc::fuel_remaining()>n;}
    cambc::bitboard around(vec2 p,int range) const {
        cambc::bitboard out;out.clear();out.draw_chebyshev(p,range);return out;
    }
    bool init() {
        if(stage_==0) {
            width_=cambc::map::width();height_=cambc::map::height();area_=width_*height_;
            if(width_<=0||height_<=0||area_>10000) return false;
            if constexpr(config::word_contact_controller)work_bridge::clear_map(width_,height_);
            if constexpr(config::word_contact_controller)(void)word_native::prime_geometry();
            hazard_.assign(area_,0);camp_blocked_.assign(area_,0);tile_group_.assign(area_,-1);danger_.clear();
            enemy_spawn_=cambc::map::spawn_region(team::enemy);
            terrain_=cambc::map::walls()|cambc::map::holes()|enemy_spawn_;
            allies_=cambc::champions(team::ally);reports_.resize(allies_.size());
            if constexpr(config::ordered_contact_controller)order_native::prime(allies_);
            for(std::size_t i=0;i<allies_.size();++i)
                if(static_cast<int>(static_cast<cambc::handle_champion_t>(allies_[i]))==self_id()) ordinal_=static_cast<int>(i);
            const auto layout=cambc::map::camp_groups();
            for(const auto& group:layout) groups_.push_back(Group{group});
            static_camps_.clear();source_tiles_.clear();
            ++stage_;return false;
        }
        if(stage_==1) {spawn_=cambc::map::spawn_region(team::ally);spawn_positions_=spawn_.positions();++stage_;return false;}
        if(stage_==2) {altar_=cambc::map::altars();altars_=altar_.positions();terrain_|=altar_;terrain_bits_=terrain_.export_linear_buffer();++stage_;return false;}
        // Static member metadata and support are built one group/input;
        // the expensive layout packet was charged to first-body's larger input.
        if(init_group_<groups_.size()) {
            auto& out=groups_[init_group_++];
            for(const auto& camp:out.native.camps()) {
                const auto p=camp.spawn_position();const auto spec=camp.base_stats();
                const int gi=static_cast<int>(init_group_-1),mi=static_cast<int>(out.positions.size());
                if constexpr(config::word_contact_controller)work_bridge::add(camp,gi,mi,p);
                tile_group_[index(p)]=gi;
                if(spec.base_strength>0) npc_.push_back({static_cast<int>(static_cast<cambc::handle_camp_t>(camp)),gi,mi,p,spec.base_attack_range,spec.base_strength});
                out.positions.push_back(p);out.hp.push_back(spec.base_health);
                out.member_damage.push_back(spec.base_strength);out.member_range.push_back(spec.base_attack_range);
                out.total_hp+=spec.base_health;out.damage+=spec.base_strength;
                out.range=std::max(out.range,spec.base_attack_range);static_camps_.set(p);source_tiles_.set(p);camp_blocked_[index(p)]=1;
                if(spec.base_strength<=0) continue;
                danger_.draw_chebyshev(p,spec.base_attack_range);
                for(int y=std::max(0,p.y-spec.base_attack_range);y<=std::min(height_-1,p.y+spec.base_attack_range);++y)
                    for(int x=std::max(0,p.x-spec.base_attack_range);x<=std::min(width_-1,p.x+spec.base_attack_range);++x)
                        hazard_[y*width_+x]+=spec.base_strength;
            }
            return false;
        }
        quiet_goals_=(~terrain_&~danger_)|spawn_;
        cambc::bitboard visited;
        exit_field_.compute(terrain_|static_camps_,quiet_goals_&~static_camps_,visited,64);
        exit_distances_=exit_field_.export_linear_buffer();
        ready_=true;return true;
    }
    void communicate() {
        for(auto& r:reports_) r={};
        if constexpr(config::named_party_controller)if(named_party_wire_reserved())return;
        if(cambc::constants::shared_memory_bytes<8||allies_.size()>intent::actors) return;
        std::uint64_t word=0;cambc::shared_memory.read(0,std::span{&word,1});
        const auto round=cambc::round_number();
        for(std::size_t i=0;i<reports_.size();++i) {
            const auto entry=intent::decode(word,static_cast<unsigned>(i),round);
            if(!entry.present||entry.group<0||entry.group>=static_cast<int>(groups_.size())) continue;
            auto& r=reports_[i];const auto& ally=allies_[i];
            r.signature=0x57424331u;r.owner=static_cast<unsigned>(static_cast<cambc::handle_champion_t>(ally));
            r.kind=static_cast<unsigned>(ally.champion_name());r.group=entry.group;r.mode=entry.mode;
            // Modulo-age is only a nomination. Never manufacture hidden HP,
            // position or support from a short timestamp that can wrap.
            r.hp=ally.is_alive()?-1.f:0.f;
            if(ally.is_visible()&&ally.is_alive()) {
                const auto p=ally.position();r.x=p.x;r.y=p.y;
                r.hp=ally.health();r.max_hp=ally.max_health();r.strength=ally.strength();
            }
        }
    }
    void publish() {
        if constexpr(config::named_party_controller)if(named_party_wire_reserved())return;
        if(cambc::constants::shared_memory_bytes<8||allies_.size()>intent::actors) return;
        std::uint64_t word=0;cambc::shared_memory.read(0,std::span{&word,1});
        const unsigned mode=cambc::self().is_alive()?static_cast<unsigned>(mode_):3u;
        word=intent::encode(word,static_cast<unsigned>(ordinal_),selected_,mode,cambc::round_number());
        cambc::shared_memory.write(0,std::span{&word,1});
    }
    static service_program::InvestmentQuote service_capital(const cambc::enum_map<cambc::item_type>& counts) {
        namespace sn=service_native;namespace sp=service_program;
        auto item=cambc::item_type::power_shard_of_strength;
        if(counts[cambc::item_type::jungle_emblem_of_strength]==0)item=cambc::item_type::jungle_shard_of_strength;
        else if(counts[cambc::item_type::monster_emblem_of_strength]==0)item=cambc::item_type::monster_shard_of_strength;
        std::uint64_t inventory=1469598103934665603ull;for(const auto count:counts)inventory=sn::mix(inventory,count);
        const auto recipe=item.combine_count(),unit=item.cost();
        const auto missing=recipe?recipe-counts[item]%recipe:0u;
        sp::InvestmentQuote q;q.target=static_cast<int>(item);q.cost=std::uint64_t(missing)*unit;
        q.inventory_revision=inventory;q.revision=sn::mix(sn::mix(inventory,static_cast<std::uint32_t>(item)),q.cost);
        q.observed=q.cost>0;q.available=recipe&&unit&&item.is_purchasable();return q;
    }
    static void service_metadata(void* context,service_program::Frame& frame,bool preparation_only) {
        namespace sn=service_native;const auto& runtime=*static_cast<Runtime*>(context);
        auto& owner=sn::memory();
        if(!preparation_only) {
        const auto counts=cambc::self().item_count();frame.next_investment=service_capital(counts);
        frame.inventory_revision=frame.next_investment.inventory_revision;
        if(owner.state) {
            const auto retained=owner.state->investment_obligation(owner.actual);
            if(retained.active&&retained.target!=frame.next_investment.target) {
                const auto old=cambc::item_type(static_cast<std::uint32_t>(retained.target));
                const bool satisfied=(old==cambc::item_type::jungle_shard_of_strength&&counts[cambc::item_type::jungle_emblem_of_strength]>0)
                    ||(old==cambc::item_type::monster_shard_of_strength&&counts[cambc::item_type::monster_emblem_of_strength]>0);
                if(satisfied) {
                    // Actual owned output refutes the old package goal. A
                    // missing purchase readback never becomes a purchase event.
                    frame.next_investment.target=retained.target;frame.next_investment.cost=old.cost();
                    frame.next_investment.revision=sn::mix(frame.inventory_revision,static_cast<std::uint32_t>(old));
                    frame.next_investment.observed=frame.next_investment.cost>0;frame.next_investment.available=false;
                }
            }
        }
        if((!frame.alive||runtime.spawn_.is_set({frame.pose.x,frame.pose.y}))&&frame.next_investment.available
            &&cambc::self().can_buy(cambc::item_type(static_cast<std::uint32_t>(frame.next_investment.target)),1))
            frame.permissions|=service_program::Buy;
        }
        frame.selected_group=runtime.selected_;
        if(runtime.selected_>=0&&runtime.selected_<static_cast<int>(runtime.groups_.size())) {
            const auto& group=runtime.groups_[runtime.selected_];
            frame.source_family=runtime.selected_;
            frame.source_life=group.seen==frame.round?(group.respawning?3u:group.reward?2u:1u):0u;
            std::uint64_t revision=1469598103934665603ull;
            for(const auto hp:group.hp)revision=sn::mix(revision,sn::scalar(hp));
            frame.source_revision=sn::mix(revision,group.respawning?3u:group.reward?2u:1u);
        }
        std::uint64_t route=1469598103934665603ull;
        for(const auto q:runtime.exit_route_)route=sn::mix(sn::mix(route,static_cast<unsigned>(q.x)),static_cast<unsigned>(q.y));
        frame.route_revision=sn::mix(sn::mix(route,runtime.exit_cursor_),runtime.exit_refusals_);
        owner.stage=frame.bag?service_program::Kind::Bank:runtime.mode_==Mode::Return?service_program::Kind::Recovery:service_program::Kind::Acquire;
        if(!frame.bag&&runtime.mode_!=Mode::Return&&runtime.release_active_)owner.stage=service_program::Kind::Release;
        else if(!frame.bag&&runtime.mode_!=Mode::Return&&frame.selected_group>=0&&frame.source_life)
            owner.stage=frame.source_life==2?service_program::Kind::Capture:frame.source_life==1?service_program::Kind::Work:service_program::Kind::Acquire;
    }
    bool service_owned() const {
        if constexpr(config::complete_service_controller)return service_native::owned();
        return false;
    }
    template<class Command> bool service_command(std::uint32_t permission,std::uint64_t action,Command&& command,bool readback=true) {
        if constexpr(config::complete_service_controller) {
            const bool tracked=service_native::memory().state.has_value()&&service_native::current();
            bool programme=service_native::memory().executing;
            if(tracked&&!programme&&permission==service_program::Buy) {
                auto& owner=service_native::memory();
                const auto investment=owner.state->investment_obligation(owner.actual);
                const auto kind=investment.affordable?service_program::Kind::Investment:service_program::Kind::Recovery;
                const auto granted=service_native::publish(kind,{service_program::FirstKind::PhysicalWord,action,permission,0,18000});
                if(!service_program::granted(granted))return false;
                programme=true;
            }
            // before() is the final SDK freshness/fuel fence. Everything after
            // it up to the raw command is pure argument/ownership bookkeeping.
            if(programme&&!service_native::before(permission,action))return false;
            if(tracked) {
                if(!programme)++service_native::memory().effect_generation;
                service_native::memory().pending=true;
            }
            const bool accepted=command();if((programme||tracked)&&readback)service_native::after();return accepted;
        } else return command();
    }
    bool service_move(vec2 destination) {
        const auto identity=service_native::mix(2,(std::uint64_t(static_cast<unsigned>(destination.x))<<32)|static_cast<unsigned>(destination.y));
        return service_command(service_program::Move,identity,[&](){
            if constexpr(config::complete_service_controller)if(service_native::memory().executing) {
                std::int32_t accepted=0;cambc::detail::check(cambc::detail::sdk_move_to(destination.x,destination.y,&accepted));return accepted!=0;
            }
            return cambc::self().move_absolute(destination);
        });
    }
    bool service_recall() {
        return service_command(service_program::Recall,3,[&](){
            if constexpr(config::complete_service_controller)if(service_native::memory().executing) {
                cambc::detail::check(cambc::detail::sdk_recall());return true;
            }
            cambc::self().recall();return true;
        });
    }
    static bool service_micro_before(void*,const capital_micro::Attempt& attempt) {
        if constexpr(config::complete_service_controller)if(service_native::memory().executing) {
            const auto permission=attempt.kind==capital_micro::Primitive::Basic?service_program::Attack:
                attempt.kind==capital_micro::Primitive::Cast?service_program::Cast:service_program::Move;
            auto identity=service_native::mix(service_native::mix(static_cast<unsigned>(attempt.kind)+1,static_cast<unsigned>(attempt.target+1)),static_cast<unsigned>(attempt.slot+1));
            identity=service_native::mix(identity,(std::uint64_t(static_cast<unsigned>(attempt.destination.x))<<32)|static_cast<unsigned>(attempt.destination.y));
            return service_native::before(permission,identity);
        }
        return true;
    }
    static void service_micro_after(void*,const capital_micro::Attempt&,bool) {
        if constexpr(config::complete_service_controller)if(service_native::memory().pending)service_native::after();
    }
    static const capital_micro::Hooks* service_micro_hooks() {
        if constexpr(config::complete_service_controller) {
            static const capital_micro::Hooks hooks{nullptr,&Runtime::service_micro_before,&Runtime::service_micro_after};
            if(service_native::memory().executing)return &hooks;
        }
        return nullptr;
    }
    void shop() {
        auto& me=cambc::self();if(me.is_alive()&&!spawn_.is_set(me.position())) return;
        const auto counts=me.item_count();
        auto item=cambc::item_type::power_shard_of_strength;
        if(counts[cambc::item_type::jungle_emblem_of_strength]==0) item=cambc::item_type::jungle_shard_of_strength;
        else if(counts[cambc::item_type::monster_emblem_of_strength]==0) item=cambc::item_type::monster_shard_of_strength;
        const auto jungle=cambc::item_type::jungle_emblem_of_strength;
        owned_camp_multiplier_=counts[jungle]>0?1.f-jungle.passive_magnitude():1.f;
        unsigned missing=5;
        if(item==cambc::item_type::jungle_shard_of_strength||item==cambc::item_type::monster_shard_of_strength)
            missing=5u-counts[item];
        investment_cost_=missing*item.cost();
        unsigned count=me.gold_count()/item.cost();
        if(item==cambc::item_type::jungle_shard_of_strength||item==cambc::item_type::monster_shard_of_strength)
            count=std::min(count,5u-counts[item]);
        if(count&&me.can_buy(item,count)) {
            if constexpr(config::complete_service_controller) {
                const auto before=service_capital(counts);const auto gold=me.gold_count();
                const auto purchase_round=cambc::round_number();
                const auto identity=service_native::mix(service_native::mix(1,static_cast<std::uint32_t>(item)),count);
                const bool bought=service_command(service_program::Buy,identity,[&](){
                    if(service_native::memory().executing)cambc::detail::check(cambc::detail::sdk_buy_item(static_cast<std::uint32_t>(item),count));
                    else me.buy(item,count);return true;
                },false);
                if(bought) {
                    const auto after=service_capital(me.item_count());const auto remaining=me.gold_count();
                    if(cambc::round_number()==purchase_round&&after.inventory_revision!=before.inventory_revision&&remaining<gold) {
                        auto& native=service_native::memory();service_program::ActualPurchase receipt;
                        receipt.observed=true;receipt.event=++native.purchase_event;receipt.target=static_cast<int>(item);
                        receipt.owner=native.owner;receipt.round=native.round;receipt.input=native.input;
                        receipt.paid=gold-remaining;receipt.quote_revision=before.revision;
                        receipt.inventory_before=before.inventory_revision;receipt.inventory_after=after.inventory_revision;
                        native.purchase=receipt;
                    }
                    if(service_native::memory().pending)service_native::after();
                }
            } else me.buy(item,count);
        }
    }
    void observe() {
        const auto visible=cambc::vision::visible_tiles();const int round=cambc::round_number();
        const std::size_t cursor=observation_cursor_++%groups_.size();
        for(std::size_t gi=0;gi<groups_.size();++gi) {
            if(gi!=cursor&&(selected_<0||gi!=static_cast<std::size_t>(selected_))) continue;
            auto& g=groups_[gi];
            bool sees=false;for(const auto p:g.positions) if(visible.is_set(p)) {sees=true;break;}
            if(!sees) continue;
            const auto state=g.native.state();g.seen=round;
            g.reward=std::holds_alternative<cambc::map::camp_group_reward_available>(state);
            if(const auto* timer=std::get_if<cambc::map::camp_group_respawning>(&state)) {
                g.respawning=true;
                // Timer zero is still captured/unavailable while a body holds
                // any spawn tile. It is not an alive zero-work source.
                g.unavailable_until=round+std::max(1,timer->timer);g.total_hp=0;continue;
            }
            g.respawning=false;
            const bool respawned=g.unavailable_until>0;
            g.unavailable_until=0;g.total_hp=0;std::size_t m=0;
            for(const auto& c:g.native.camps()) {
                if(respawned) g.hp[m]=c.base_stats().base_health;
                // DEAD has lifecycle but no live combat packet. A live-info
                // visibility predicate must never gate observed death.
                if(visible.is_set(c.spawn_position())) {
                    std::uint32_t life=0;
                    const auto id=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(c));
                    const auto code=cambc::detail::sdk_camp_state(id,&life);
                    if(code==0) {
                        if(life==0) {
                            g.hp[m]=0;const auto at=c.spawn_position();
                            if(camp_blocked_[index(at)]) {camp_blocked_[index(at)]=0;static_camps_.set(at,false);exit_field_dirty_=true;}
                        } else if(life==1||life==2) {
                            g.hp[m]=c.health();const auto at=c.spawn_position();
                            if(!camp_blocked_[index(at)]) {camp_blocked_[index(at)]=1;static_camps_.set(at);exit_field_dirty_=true;}
                        }
                        else throw std::runtime_error("capital camp lifecycle");
                    } else if(code!=97) cambc::detail::check(code);
                }
                g.total_hp+=g.hp[m++];
            }
        }
    }
    vec2 center(const Group& g) const {return g.positions.front();}
    float party_power(int group) const {
        float power=0;const auto p=center(groups_[group]);
        for(const auto& r:reports_) if(r.signature&&r.owner!=static_cast<unsigned>(self_id())&&r.hp>0&&r.group==group
            &&r.mode==static_cast<unsigned>(Mode::Work)&&dist({r.x,r.y},p)<12) power+=r.strength;
        return power;
    }
    void choose() {
        auto& me=cambc::self();const auto p=me.position();const int round=cambc::round_number();
        if(selected_>=0&&selected_<static_cast<int>(groups_.size())) {
            auto& old=groups_[selected_];
            if(old.unavailable_until>round) selected_=-1;
            else if(round-last_retarget_<32||dist(p,center(old))<=old.range+2) return;
        }
        float best=-1;int choice=-1;const auto kind=me.champion_name();
        const auto own_range=me.attack_range();const auto own_strength=me.strength();
        const auto own_hp=me.health();const auto own_gold=me.gold_count();
        const auto own_max=me.max_health();const auto own_focus=me.focus();
        if(kind==cambc::champion_type::willow) {
            for(const auto& r:reports_) if(r.signature&&r.hp!=0&&r.group>=0&&r.group<static_cast<int>(groups_.size())
                &&r.kind==static_cast<unsigned>(cambc::champion_type::varg)&&r.mode==static_cast<unsigned>(Mode::Work)
                &&groups_[r.group].unavailable_until<=round&&!groups_[r.group].refused) {selected_=r.group;last_retarget_=round;return;}
        }
        for(int g=0;g<static_cast<int>(groups_.size());++g) {
            auto& group=groups_[g];if(group.unavailable_until>round) continue;
            if(group.refused) {
                if constexpr(config::complete_service_controller) {
                    auto& owner=service_native::memory();
                    if(static_cast<std::size_t>(g)>=owner.failures.size()||!owner.failures[g].key.valid)group.refused=false;
                    else {
                        const auto& prior=owner.failures[g].key;
                        auto target=prior.target;target.pose=owner.actual.pose;
                        auto actual=owner.actual;actual.available_fuel=cambc::fuel_remaining();
                        const auto current=service_program::failure(prior.reason,actual,target);
                        if(service_program::invalidated(prior,current.key))group.refused=false;
                    }
                    if(group.refused)continue;
                } else {
                const bool changed=group.refused_max_hp!=own_max||group.refused_strength!=own_strength
                    ||group.refused_focus!=own_focus||group.refused_work!=group.total_hp;
                if(!changed) continue;
                group.refused=false;
                }
            }
            const float travel=std::max(0,dist(p,center(group))-own_range);
            float effective=std::max(1.f,own_strength);
            if(kind==cambc::champion_type::varg) {
                unsigned adjacent=0;for(const auto q:group.positions) if(dist(q,center(group))<=1) ++adjacent;
                effective*=std::max(1u,adjacent);
            }
            const float other=party_power(g);
            const float work=group.reward?0:group.total_hp/(effective+other);
            float income=group.native.gem_value();
            if(own_gold<150) income+=group.native.gold_value()*.20f;
            if(other>0&&work<8&&!group.reward) income*=.2f;
            float score=income/(travel+work+static_cast<float>(cambc::constants::capture_time)+2.f);
            const float hits=work*group.damage;
            if(hits>own_hp*.65f) score*=std::max(.05f,own_hp*.65f/std::max(hits,1.f));
            if(kind==cambc::champion_type::varg)
                for(const auto& r:reports_) if(r.signature&&r.hp>0&&r.kind==static_cast<unsigned>(cambc::champion_type::willow)
                    &&dist({r.x,r.y},p)<12) score*=1.f+std::min(1.f,group.damage/60.f);
            if(score>best) {best=score;choice=g;}
        }
        selected_=choice;last_retarget_=round;
    }
    cambc::bitboard blockers() const {
        auto b=terrain_|cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
        b.set(cambc::self().position(),false);return b;
    }
    bool move(const cambc::bitboard& supplied,bool escape=false) {
        if constexpr(config::complete_service_controller) {return move_program(supplied,escape);}

        if constexpr(config::contact_controller)if(contact_native::handled())return false;
        auto& me=cambc::self();const auto epoch=cambc::round_number();
        if(!me.can_move()||me.restrictions().contains(cambc::buff_restrictions::move)||!reserve(static_cast<unsigned>(20000+80*((width_+7)/8)*((height_+7)/8)))) return false;
        auto blocked=blockers();auto goals=supplied&~blocked;
        if(goals.is_empty()) return false;
        auto p=me.position();if(goals.is_set(p)) return false;
        cambc::distance_field field;cambc::bitboard reached;
        field.compute_to(blocked,goals,around(p,0),reached,64);
        auto possible=blocked.reachable_tiles(p,me.base_stats().base_move_speed).positions();
        const auto here=field.get(p);float best=std::numeric_limits<float>::infinity();vec2 step=p;
        for(const auto q:possible) {
            const auto d=field.get(q);if(d==cambc::distance_field::unreachable) continue;
            // Strict distance-field descent permits geometric detours around
            // walls, but never an endpoint-risk two-cell return oscillation.
            if(d>=here) continue;
            const float score=d+(escape?3.f:.1f)*hazard(q);
            if(score<best&&q!=p) {best=score;step=q;}
        }
        if(step==p||!reserve()||cambc::round_number()!=epoch||me.position()!=p) return false;
        if constexpr(config::contact_controller) {
            if(!contact_native::handled()) {
                const auto decision=contact_native::act(contact_environment(),step);
                if(cambc::round_number()!=epoch||me.position()!=p)return false;
                if(decision.present)return decision.moved;
            }
        }
        const bool accepted=me.move_absolute(step);
        if constexpr(config::contact_controller)contact_native::note_step(accepted,true,spawn_.is_set(me.position()));
        return accepted; // false means hidden obstruction, not empty
    }
    bool move_program(const cambc::bitboard& supplied,bool escape=false) {
        if constexpr(config::contact_controller)if(!service_owned()&&contact_native::handled())return false;
        auto& me=cambc::self();const auto epoch=cambc::round_number();
        if(!me.can_move()||me.restrictions().contains(cambc::buff_restrictions::move)||!reserve(static_cast<unsigned>(20000+80*((width_+7)/8)*((height_+7)/8)))) return false;
        auto blocked=blockers();auto goals=supplied&~blocked;
        if(goals.is_empty()) return false;
        auto p=me.position();if(goals.is_set(p)) return false;
        cambc::distance_field field;cambc::bitboard reached;
        field.compute_to(blocked,goals,around(p,0),reached,64);
        auto possible=blocked.reachable_tiles(p,me.base_stats().base_move_speed).positions();
        const auto here=field.get(p);float best=std::numeric_limits<float>::infinity();vec2 step=p;
        for(const auto q:possible) {
            const auto d=field.get(q);if(d==cambc::distance_field::unreachable) continue;
            // Strict distance-field descent permits geometric detours around
            // walls, but never an endpoint-risk two-cell return oscillation.
            if(d>=here) continue;
            const float score=d+(escape?3.f:.1f)*hazard(q);
            if(score<best&&q!=p) {best=score;step=q;}
        }
        if(step==p||!reserve()||cambc::round_number()!=epoch||me.position()!=p) return false;
        if constexpr(config::encounter_service_controller) {
            const auto chosen=encounter_move(step,blocked,possible);
            if(chosen.has_value())return *chosen;
        }
        if constexpr(config::contact_controller)if(!service_owned()) {
            if(!contact_native::handled()) {
                const auto decision=contact_native::act(contact_environment(),step);
                if(cambc::round_number()!=epoch||me.position()!=p)return false;
                if(decision.present)return decision.moved;
            }
        }
        const bool accepted=service_move(step);
        if constexpr(config::contact_controller)contact_native::note_step(accepted,true,spawn_.is_set(me.position()));
        return accepted; // false means hidden obstruction, not empty
    }
    std::optional<bool> encounter_move(vec2 economic,const cambc::bitboard& blocked,std::span<const vec2> possible) {
        namespace en=encounter_native;
        if(!config::encounter_service_controller||!service_owned()||mode_==Mode::Return)return std::nullopt;
        auto& episode=en::memory();if(episode.restoring||episode.claimed_this_input)return std::nullopt;
        auto& me=cambc::self();const auto p=me.position();const auto epoch=cambc::round_number();
        if(me.gem_count()||service_native::memory().foes.is_empty())return std::nullopt;
        ++en::counters().nominations;
        // Keep the existing first word funded when optional model work cannot
        // finish. A large field is a coverage limit, not a no-defence proof.
        const auto field_work=static_cast<unsigned>(8000+24*((width_+7)/8)*((height_+7)/8));
        if(!reserve(50000+field_work)){++en::counters().rejected[static_cast<unsigned>(en::Rejection::Funding)];return std::nullopt;}
        const auto* own_design=en::design(static_cast<int>(me.champion_name()));
        if(!own_design)return std::nullopt;
        // First supported incumbent is Move-only: ranged walking spends A,
        // or A is already spent. Do not consume a melee Move->Work dispatch
        // tail's funded budget merely to price optional defence alternatives.
        if(own_design->definition.attack_type!=1&&me.can_attack()){++en::counters().rejected[static_cast<unsigned>(en::Rejection::Resource)];return std::nullopt;}
        auto goals=spawn_&~blocked;if(goals.is_empty())return std::nullopt;
        cambc::distance_field field;cambc::bitboard reached;
        field.compute_to(blocked,goals,around(p,0),reached,64);
        const auto here=field.get(p);auto best=here;vec2 restoration=p;
        for(const auto q:possible) {
            const auto d=field.get(q);if(q!=p&&d<best){best=d;restoration=q;}
        }
        if(restoration==p||restoration==economic||!same_epoch(epoch))return std::nullopt;
        std::vector<int> order;order.reserve(allies_.size());for(const auto& ally:allies_)order.push_back(static_cast<int>(static_cast<cambc::handle_champion_t>(ally)));
        std::vector<en::Source> sources;sources.reserve(npc_.size());
        for(const auto& n:npc_)sources.push_back({n.id,{n.position.x,n.position.y},n.range,n.damage,false});
        std::sort(sources.begin(),sources.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        const auto& authority=service_native::memory();en::Input input;
        input.owner=self_id();input.round=epoch;input.ordinal=ordinal_;input.width=width_;input.height=height_;
        input.input=authority.input;input.effect=authority.effect_generation;input.economic_step={economic.x,economic.y};input.recovery_step={restoration.x,restoration.y};
        input.own_order=order;input.sources=sources;input.walk_blocked=&blocked;input.own_spawn=&spawn_;input.enemy_spawn=&enemy_spawn_;
        input.camp_occupancy=&static_camps_;input.recovery_field=&field;
        const auto offer=en::prepare(input);
        if(!same_epoch(epoch)||me.position()!=p)return false;
        if(offer.status!=en::Status::Ready)return std::nullopt;
        if(authority.input!=offer.input||authority.effect_generation!=offer.effect||!service_owned())return false;
        en::log_selection(offer);
        // Preserve the actual prefix before the host. Refusal or changed
        // contact causes fresh restoration planning, never old-pose replay.
        episode.restoring=episode.claimed_this_input=true;episode.owner=self_id();episode.start_round=epoch;
        episode.input=offer.input;episode.assumptions=offer.assumptions;resume_=selected_;mode_=Mode::Return;
        const bool accepted=service_move(restoration);
        if(accepted){++episode.actual_prefixes;++en::counters().accepted;}else ++en::counters().refused;
        if constexpr(config::contact_controller)contact_native::note_step(accepted,true,spawn_.is_set(me.position()));
        return accepted;
    }
    bool encounter_restoration() {
        namespace en=encounter_native;auto& episode=en::memory();
        if(!config::encounter_service_controller||!episode.restoring)return false;
        auto& me=cambc::self();const auto epoch=cambc::round_number();
        if(episode.owner!=self_id()||!me.is_alive()) {episode.restoring=false;return false;}
        if(spawn_.is_set(me.position())&&me.health()>=me.max_health()) {
            episode.restoring=false;++en::counters().restored;mode_=Mode::Work;selected_=-1;resume_=-1;exit_route_.clear();return false;
        }
        mode_=Mode::Return;
        if(spawn_.is_set(me.position())) {publish();return true;} // real owner-end heal
        const auto required=static_cast<unsigned>(26000+80*((width_+7)/8)*((height_+7)/8));
        if(!me.can_move()||!reserve(required))return false; // existing complete Recovery remains the funded fallback
        auto blocked=blockers();auto goals=spawn_&~blocked;if(goals.is_empty())return false;
        cambc::distance_field field;cambc::bitboard reached;const auto p=me.position();
        field.compute_to(blocked,goals,around(p,0),reached,64);
        auto best=field.get(p);vec2 step=p;
        for(const auto q:blocked.reachable_tiles(p,me.base_stats().base_move_speed).positions()) {
            const auto d=field.get(q);if(q!=p&&d<best){best=d;step=q;}
        }
        if(step==p||!same_epoch(epoch))return false;
        episode.claimed_this_input=true;
        const bool accepted=service_move(step);if(accepted)++episode.actual_prefixes;
        if constexpr(config::contact_controller)contact_native::note_step(accepted,true,spawn_.is_set(me.position()));
        publish();return true;
    }
    void threats() {
        threats_.clear();awake_camps_.clear();
        for(const auto& camp:cambc::vision::camps()) {
            const auto state=camp.state();
            if(state.is_awake()) {
                awake_camps_.push_back(static_cast<unsigned>(static_cast<cambc::handle_camp_t>(camp)));
                threats_.push_back({camp.position(),camp.strength(),camp.attack_range()});
            }
        }
        std::sort(awake_camps_.begin(),awake_camps_.end());
        for(const auto& foe:cambc::vision::champions(team::enemy))
            if(foe.is_alive()) threats_.push_back({foe.position(),foe.strength()*2+foe.focus()*.4f,foe.attack_range()+2,true});
    }
    float incoming(vec2 p) const {
        float damage=0;for(const auto& t:threats_) if(dist(p,t.position)<=t.reach) damage+=t.damage;
        return damage;
    }
    static work_bridge::Offer nominate_word_work(void* context) {
        auto& runtime=*static_cast<Runtime*>(context);auto& table=work_bridge::registry();
        work_bridge::Offer out;out.owner=runtime.self_id();out.round=cambc::round_number();
        if(cambc::fuel_remaining()<42000||table.cells.empty())return out;
        // This first harmful-work family owns no carried receipt. Conversion
        // and recovery retain the already measured macro operators.
        if(runtime.mode_!=Mode::Work||cambc::self().gem_count()!=0)return out;
        // A primitive belongs to the retained economic service. It must not
        // replace acquisition with unrelated nearby HP damage. With no job,
        // fall through to the funded macro chooser/observation/route first;
        // its later fresh hit/move call may then offer this same service.
        if(runtime.selected_<0||runtime.selected_>=static_cast<int>(runtime.groups_.size()))return out;
        const auto& selected=runtime.groups_[runtime.selected_];
        if(selected.reward||selected.respawning||selected.refused||
            selected.unavailable_until>out.round)return out;
        cambc::detail::champion_info own{};cambc::detail::check(cambc::detail::sdk_champion(out.owner,&own));
        const vec2 p{own.x,own.y};const auto state=cambc::champion_state::from_bits_retain(own.state);
        const int move=state.contains(cambc::champion_state::can_move)?std::clamp(own.move_speed,0,2):0;
        const int reach=own.attack_range+move;
        if(reach<0||reach>8)return out;
        std::array<int,289> choices{};unsigned count=0;
        // Bounded static spatial nomination is never current life authority.
        for(int y=std::max(0,p.y-reach);y<=std::min(runtime.height_-1,p.y+reach);++y)
            for(int x=std::max(0,p.x-reach);x<=std::min(runtime.width_-1,p.x+reach);++x) {
                const int at=table.cells[y*runtime.width_+x];
                if(at>=0&&table.entries[at].group==runtime.selected_&&count<choices.size())choices[count++]=at;
            }
        if(!count)return out;
        int retained=-1;
        for(unsigned i=0;i<count;++i)if(choices[i]==table.preferred){retained=choices[i];break;}
        // Retain only nomination identity; life and HP are freshly queried.
        for(unsigned probe=0;probe<2&&cambc::fuel_remaining()>40000;++probe) {
            int index=probe==0&&retained>=0?retained:choices[table.cursor++%count];
            if(probe&&index==retained&&count>1)index=choices[table.cursor++%count];
            const auto& entry=table.entries[index];
            if(!entry.target.is_visible()||!entry.target.is_alive()) {
                if(index==table.preferred)table.preferred=-1;
                continue;
            }
            const float hp=entry.target.health();
            if(!(hp>0)||!std::isfinite(hp)||cambc::round_number()!=static_cast<unsigned>(out.round))return out;
            table.preferred=index;out.target=entry.target;out.group=entry.group;out.position=entry.position;
            out.observed_hp=hp;out.fresh=true;return out;
        }
        return out;
    }
    static bool word_npc_quiet(void* context,vec2 p,int guaranteed_dead_camp) {
        const auto& runtime=*static_cast<Runtime*>(context);
        // Every unknown source is potentially alive/awake. Only a nominated
        // source with guaranteed modeled lethal effects may be removed.
        for(const auto& source:runtime.npc_)if(source.id!=guaranteed_dead_camp&&dist(p,source.position)<=source.range)return false;
        return true;
    }
    static bool conversion_walkable(void* context,tribute::Point p) {
        const auto& runtime=*static_cast<Runtime*>(context);const vec2 q{p.x,p.y};
        return runtime.inside(q)&&!((runtime.terrain_bits_[runtime.index(q)>>3]>>(runtime.index(q)&7))&1u);
    }
    static conversion_refinement::Plan conversion_prepare(void* context,conversion_native::Epoch epoch,
                                  const conversion_native::Packet& packet,const tribute::Context& receipt,
                                  const conversion_native::Geometry& geometry,bool paid_ingress) {
        auto& memory=conversion_runtime::memory();
        if(!conversion_runtime::current(epoch))return {};
        const auto& own=packet.own;
        tribute::Point goal=receipt.altar;bool named=false;
        for(const auto p:geometry.altars)if(p==goal)named=true;
        if(!named) {
            int best=1000000;
            for(const auto p:geometry.altars)if(conversion_native::distance(own.position,p)<best)
                {goal=p;best=conversion_native::distance(own.position,p);}
        }
        std::array<tribute::Point,8> candidates{};unsigned count=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy) {
            const tribute::Point q{own.position.x+dx,own.position.y+dy};
            if(conversion_walkable(context,q))candidates[count++]=q;
        }
        const auto separation=[&](tribute::Point p) {
            int distance=1000000;
            for(unsigned i=0;i<packet.enemy_count;++i)if(packet.enemies[i].observed_now&&packet.enemies[i].alive)
                distance=std::min(distance,conversion_native::distance(p,packet.enemies[i].position));
            return distance;
        };
        std::sort(candidates.begin(),candidates.begin()+count,[&](auto a,auto b) {
            const int da=conversion_native::distance(a,goal),db=conversion_native::distance(b,goal);
            const bool aa=da<=packet.submit_range,bb=db<=packet.submit_range;
            if(aa!=bb)return aa;
            if(da!=db)return da<db;
            if(separation(a)!=separation(b))return separation(a)>separation(b);
            return a.y!=b.y?a.y<b.y:a.x<b.x;
        });
        // Ranked route nominations belong to this Bank policy. They do not
        // assert utility, no-foe coverage or a survival certificate.
        conversion_refinement::Options options;options.admitted_destinations={candidates.data(),count};
        return conversion_refinement::prepare(packet,receipt,geometry,epoch,paid_ingress,memory.host,options);
    }
    static bool conversion_execute(void*,conversion_refinement::Plan& plan,conversion_native::Epoch epoch,
                                   const conversion_native::Packet& packet,const tribute::Context& receipt,
                                   const conversion_native::Geometry& geometry) {
        auto& memory=conversion_runtime::memory();
        const auto result=conversion_refinement::execute(plan,packet,receipt,geometry,epoch,memory.host);
        if(result.moved)memory.fallback_moved=true;
        return result.moved||result.closed;
    }
    static void commit_word_work(void* context,const work_bridge::Offer& offer,float,float after) {
        auto& runtime=*static_cast<Runtime*>(context);const auto id=static_cast<int>(static_cast<cambc::handle_camp_t>(*offer.target));
        for(const auto& entry:work_bridge::registry().entries)if(static_cast<int>(static_cast<cambc::handle_camp_t>(entry.target))==id) {
            if(entry.group<0||entry.group>=static_cast<int>(runtime.groups_.size()))return;
            auto& group=runtime.groups_[entry.group];group.hp[entry.member]=after;
            group.reward=false;group.respawning=false;group.unavailable_until=0;
            runtime.selected_=entry.group;runtime.last_retarget_=cambc::round_number();
            // Actual one-member work never mints whole-group observations.
            // Existing return/conversion purpose is retained.
            if(runtime.mode_==Mode::Return)runtime.resume_=entry.group;
            return;
        }
    }
    static contact_native::Risk contact_npcs(void* context,vec2 endpoint) {
        const auto& runtime=*static_cast<Runtime*>(context);contact_native::Risk out;
        // All potential static camp footprints are counted during contact,
        // including unobserved activation. No ranged/helper immunity is invented.
        for(const auto& n:runtime.npc_) if(dist(endpoint,n.position)<=n.range) {
            if constexpr(config::word_contact_controller) {
                // NEXT SCHEDULED owner input crosses exactly one camp batch.
                out.damage+=n.damage*runtime.owned_camp_multiplier_;++out.hits;
            } else {out.damage+=2.f*n.damage*runtime.owned_camp_multiplier_;out.hits+=2;}
        }
        return out;
    }
    contact_native::Environment contact_environment() {
        auto& me=cambc::self();const auto p=me.position();auto goal=p;
        if(mode_==Mode::Return&&!spawn_positions_.empty()) {
            goal=spawn_positions_.front();for(const auto q:spawn_positions_)if(dist(p,q)<dist(p,goal))goal=q;
        } else if(me.gem_count()>0&&!altars_.empty()) {
            goal=altars_.front();for(const auto q:altars_)if(dist(p,q)<dist(p,goal))goal=q;
        } else if(selected_>=0) goal=center(groups_[selected_]);
        else if(!spawn_positions_.empty())goal=spawn_positions_.front();
        if constexpr(config::contact_service_controller) {
            bank_native::Geometry geometry;geometry.owner=self_id();geometry.round=cambc::round_number();geometry.position=p;
            geometry.npc_quiet=true;
            for(const auto& source:npc_)if(dist(p,source.position)<=source.range){geometry.npc_quiet=false;break;}
            if(me.gem_count()>0)for(const auto altar:altars_)
                if(dist(p,altar)<=cambc::constants::submit_gems_range) {
                    geometry.altar_known=true;geometry.altar=altar;break;
                }
            bank_native::geometry(geometry);
        }
        if constexpr(config::word_contact_controller)
            work_bridge::bind({this,&Runtime::nominate_word_work,&Runtime::word_npc_quiet,&Runtime::commit_word_work});
        return {goal,this,&Runtime::contact_npcs,spawn_.is_set(p)};
    }
    bool pressure(bool advance=true) {
        if constexpr(config::complete_service_controller) {return pressure_program(advance);}

        if constexpr(config::contact_controller) {
            (void)advance;return contact_native::act(contact_environment()).present;
        }
        auto& me=cambc::self();const auto p=me.position();
        auto foes=cambc::vision::champions(team::enemy);const cambc::champion* target=nullptr;float value=-1;
        for(const auto& foe:foes) if(foe.is_alive()&&dist(p,foe.position())<=5) {
            if(enemy_spawn_.is_set(foe.position())) {
                // Spawn restores health between inputs. Legal damage smaller
                // than one-input lethal capacity is not persistent kill work.
                float extra=0;
                for(const auto& a:me.abilities()) if(a.is_ready())
                    extra=std::max(extra,a.is_attack_reset()?me.strength():
                        (a.is_damage()||a.is_cc()||a.is_aoe_damage()?a.damage_amount():0.f));
                if(me.strength()+extra<foe.health()) continue;
            }
            const float next=1000.f/(foe.health()+1.f)-dist(p,foe.position())*.05f;
            if(next>value) {value=next;target=&foe;}
        }
        if(!target) return false;
        capital_micro::Context context{incoming(p),4};
        auto result=capital_micro::engage(*target,context);
        float allied=me.health();
        for(const auto& ally:cambc::vision::champions(team::ally))
            if(ally.is_alive()&&dist(p,ally.position())<=4) allied+=ally.health()*.5f;
        if(advance&&me.can_move()&&target->is_visible()&&target->is_alive()&&dist(me.position(),target->position())>me.attack_range()
            &&allied>target->health()&&me.health()>incoming(p)*5)
            move(around(target->position(),me.attack_range()));
        if(reserve()) (void)capital_micro::engage(*target,context);
        return result.acted()||(target->is_visible()&&dist(p,target->position())<=me.attack_range()+1);
    }
    bool pressure_program(bool advance=true) {
        // An HP envelope is not a complete alternative to the retained rolling
        // Work/capital/recovery programme. Missing coherent value keeps it.
        if constexpr(config::complete_service_controller)if(service_owned())return false;
        if constexpr(config::contact_controller) {
            (void)advance;return contact_native::act(contact_environment()).present;
        }
        auto& me=cambc::self();const auto p=me.position();
        auto foes=cambc::vision::champions(team::enemy);const cambc::champion* target=nullptr;float value=-1;
        for(const auto& foe:foes) if(foe.is_alive()&&dist(p,foe.position())<=5) {
            if(enemy_spawn_.is_set(foe.position())) {
                // Spawn restores health between inputs. Legal damage smaller
                // than one-input lethal capacity is not persistent kill work.
                float extra=0;
                for(const auto& a:me.abilities()) if(a.is_ready())
                    extra=std::max(extra,a.is_attack_reset()?me.strength():
                        (a.is_damage()||a.is_cc()||a.is_aoe_damage()?a.damage_amount():0.f));
                if(me.strength()+extra<foe.health()) continue;
            }
            const float next=1000.f/(foe.health()+1.f)-dist(p,foe.position())*.05f;
            if(next>value) {value=next;target=&foe;}
        }
        if(!target) return false;
        capital_micro::Context context{incoming(p),4};
        auto result=capital_micro::guarded::engage<true>(*target,context,service_micro_hooks());
        float allied=me.health();
        for(const auto& ally:cambc::vision::champions(team::ally))
            if(ally.is_alive()&&dist(p,ally.position())<=4) allied+=ally.health()*.5f;
        if(advance&&me.can_move()&&target->is_visible()&&target->is_alive()&&dist(me.position(),target->position())>me.attack_range()
            &&allied>target->health()&&me.health()>incoming(p)*5)
            move(around(target->position(),me.attack_range()));
        if(reserve()) (void)capital_micro::guarded::engage<true>(*target,context,service_micro_hooks());
        return result.acted()||(target->is_visible()&&dist(p,target->position())<=me.attack_range()+1);
    }
    struct ExitFailure {service_program::FailureReason reason{service_program::FailureReason::RouteUnavailable};unsigned required_fuel{};};
    bool exit_path(vec2 origin,std::vector<vec2>& path) {
        path.clear();path.push_back(origin);auto p=origin;
        if(hazard(origin)==0) return true;
        if(exit_field_dirty_) {
            if(!reserve(static_cast<unsigned>(20000+80*((width_+7)/8)*((height_+7)/8)+3*area_))) return false;
            cambc::bitboard visited;
            exit_field_.compute(terrain_|static_camps_,quiet_goals_&~static_camps_,visited,64);
            exit_distances_=exit_field_.export_linear_buffer();exit_field_dirty_=false;
        }
        const auto occupied=blockers();
        for(unsigned n=0;n<64&&hazard(p)>0;++n) {
            auto best=exit_distances_[index(p)];auto next=p;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                const vec2 q{p.x+dx,p.y+dy};
                if(!inside(q)) continue;
                const auto cell=index(q);
                if(((terrain_bits_[cell>>3]>>(cell&7))&1)||camp_blocked_[cell]) continue;
                // Only the next landing is live occupancy authority. Future
                // cursor inputs refresh and reprice their actual origin.
                if(path.size()==1&&occupied.is_set(q)) continue;
                const auto d=exit_distances_[cell];
                if(d<best) {best=d;next=q;}
            }
            if(next==p) return false;
            path.push_back(next);p=next;
        }
        return hazard(p)==0;
    }
    bool exit_path_observed(vec2 origin,std::vector<vec2>& path,ExitFailure* failure=nullptr) {
        path.clear();path.push_back(origin);auto p=origin;
        if(hazard(origin)==0) return true;
        if(exit_field_dirty_) {
            const auto required=static_cast<unsigned>(20000+80*((width_+7)/8)*((height_+7)/8)+3*area_);
            if(!reserve(required)) {
                if(failure)*failure={service_program::FailureReason::NeedFuel,required+1};
                return false;
            }
            cambc::bitboard visited;
            exit_field_.compute(terrain_|static_camps_,quiet_goals_&~static_camps_,visited,64);
            exit_distances_=exit_field_.export_linear_buffer();exit_field_dirty_=false;
        }
        const auto occupied=blockers();
        for(unsigned n=0;n<64&&hazard(p)>0;++n) {
            auto best=exit_distances_[index(p)];auto next=p;bool occupied_descent=false;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                const vec2 q{p.x+dx,p.y+dy};
                if(!inside(q)) continue;
                const auto cell=index(q);
                if(((terrain_bits_[cell>>3]>>(cell&7))&1)||camp_blocked_[cell]) continue;
                // Only the next landing is live occupancy authority. Future
                // cursor inputs refresh and reprice their actual origin.
                const auto d=exit_distances_[cell];
                if(path.size()==1&&occupied.is_set(q)) {occupied_descent|=d<best;continue;}
                if(d<best) {best=d;next=q;}
            }
            if(next==p) {
                if(failure&&occupied_descent)*failure={service_program::FailureReason::TransientOccupied,0};
                return false;
            }
            path.push_back(next);p=next;
        }
        return hazard(p)==0;
    }
    exit::Quote work_quote(const cambc::camp& target,const std::vector<vec2>& path) const {
        if(path.size()==1&&hazard(path.front())==0) {
            exit::Quote q;q.supported=q.affordable=q.survives=q.ends_quiet=true;q.hp_after=cambc::self().health();return q;
        }
        if(path.size()>16) return {};
        std::vector<exit::Source> sources;sources.reserve(32);
        const auto target_position=target.position();
        std::vector<bool> waking(groups_.size(),false);
        if(selected_>=0) waking[selected_]=true;
        if(cambc::self().champion_name()==cambc::champion_type::varg)
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                const vec2 q{target_position.x+dx,target_position.y+dy};
                if(inside(q)&&tile_group_[index(q)]>=0) waking[tile_group_[index(q)]]=true;
            }
        // Prepared static NPC packets exclude harmless members entirely. Only
        // current wake/life facts and owned mitigation enter the live quote.
        for(const auto& n:npc_) {
            const bool awake=std::binary_search(awake_camps_.begin(),awake_camps_.end(),static_cast<unsigned>(n.id));
            const bool wake=waking[n.group];
            if(!wake&&!awake) continue;
            sources.push_back({n.id,n.position.x,n.position.y,n.range,n.damage*owned_camp_multiplier_,
                groups_[n.group].hp[n.member]>0,awake,wake});
            if(sources.size()>32) return {};
        }
        std::sort(sources.begin(),sources.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        std::vector<exit::Point> states;for(const auto p:path) states.push_back({p.x,p.y});
        float shield=0;const auto buffs=cambc::self().buffs();
        for(const auto& buff:buffs) if(const auto* x=std::get_if<cambc::buff_shield>(&buff))
            if(x->duration>=static_cast<int>(path.size()+1)) shield+=x->value;
        return exit::quote(sources,states,cambc::self().health(),shield,1);
    }
    bool retained_exit(bool allow_replan=true) {
        if constexpr(config::complete_service_controller) {return retained_exit_program(allow_replan);}

        if constexpr(config::contact_controller)if(contact_native::handled())return true;
        auto& me=cambc::self();
        if(exit_route_.empty()) return false;
        while(exit_cursor_<exit_route_.size()&&exit_route_[exit_cursor_]==me.position()) ++exit_cursor_;
        if(exit_cursor_>=exit_route_.size()) {exit_route_.clear();return false;}
        const auto next=exit_route_[exit_cursor_];const auto p=me.position();
        if(!reserve(9000)) return true;
        const auto occupied=allow_replan?blockers():
            cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
        if(dist(p,next)!=1||occupied.is_set(next)||exit_refusals_>=1) {
            if(!allow_replan) return true;
            // Replan this actual origin, rather than repeat an unpriced refusal.
            if(!exit_path(p,exit_route_)) {exit_route_.clear();return false;}
            exit_cursor_=exit_route_.size()>1?1:0;exit_refusals_=0;
            if(exit_route_.size()<2) {exit_route_.clear();return false;}
        }
        const auto epoch=cambc::round_number();
        if constexpr(config::contact_controller)if(allow_replan) {
            const auto decision=contact_native::act(contact_environment(),exit_route_[exit_cursor_]);
            if(decision.present)return true;
        }
        if(me.can_move()&&!me.restrictions().contains(cambc::buff_restrictions::move)&&reserve()
            &&cambc::round_number()==epoch) {
            if(me.move_absolute(exit_route_[exit_cursor_])) {
                if constexpr(config::contact_controller)contact_native::note_step(true,true,spawn_.is_set(me.position()));
                ++exit_cursor_;exit_refusals_=0;
            }
            else ++exit_refusals_;
        }
        return true;
    }
    bool retained_exit_program(bool allow_replan=true) {
        if constexpr(config::contact_controller)if(contact_native::handled())return true;
        auto& me=cambc::self();
        if(exit_route_.empty()) return false;
        while(exit_cursor_<exit_route_.size()&&exit_route_[exit_cursor_]==me.position()) ++exit_cursor_;
        if(exit_cursor_>=exit_route_.size()) {exit_route_.clear();return false;}
        const auto next=exit_route_[exit_cursor_];const auto p=me.position();
        if(!reserve(9000)) return true;
        const auto occupied=allow_replan?blockers():
            cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
        if(dist(p,next)!=1||occupied.is_set(next)||exit_refusals_>=1) {
            if(!allow_replan) return true;
            // Replan this actual origin, rather than repeat an unpriced refusal.
            if(!exit_path(p,exit_route_)) {exit_route_.clear();return false;}
            exit_cursor_=exit_route_.size()>1?1:0;exit_refusals_=0;
            if(exit_route_.size()<2) {exit_route_.clear();return false;}
        }
        const auto epoch=cambc::round_number();
        if constexpr(config::contact_controller)if(allow_replan&&!service_owned()) {
            const auto decision=contact_native::act(contact_environment(),exit_route_[exit_cursor_]);
            if(decision.present)return true;
        }
        if(me.can_move()&&!me.restrictions().contains(cambc::buff_restrictions::move)&&reserve()
            &&cambc::round_number()==epoch) {
            if(service_move(exit_route_[exit_cursor_])) {
                if constexpr(config::contact_controller)contact_native::note_step(true,true,spawn_.is_set(me.position()));
                ++exit_cursor_;exit_refusals_=0;
            }
            else ++exit_refusals_;
        }
        return true;
    }
    void recover() {
        if constexpr(config::complete_service_controller) {recover_program();return;}

        auto& me=cambc::self();const auto p=me.position();
        if(spawn_.is_set(p)) {
            shop();if(me.health()>=me.max_health()) {mode_=Mode::Work;selected_=resume_;resume_=-1;}
            return;
        }
        float foe_pressure=0;for(const auto& t:threats_) if(t.champion&&dist(p,t.position)<=t.reach) foe_pressure+=t.damage;
        // Static camps cannot chase. Their queried actual footprint decides
        // whether a quiet recall is possible; an entry margin is not a veto.
        if(foe_pressure==0&&hazard(p)==0) {
            if constexpr(config::contact_controller) {
                if(contact_native::local_recall_quiet(false)) {me.recall();contact_native::note_recall_accepted();return;}
            } else {me.recall();return;}
        }
        if(!me.can_move()) return;
        if(retained_exit()) return;
        (void)move(spawn_,true);
        if constexpr(config::contact_controller)if(contact_native::handled())return;
        if(reserve()) (void)pressure(false);
        if constexpr(config::contact_controller)if(contact_native::handled())return;
        if(me.can_cast()&&me.health()<incoming(me.position())*3&&!spawn_positions_.empty()) {
            const bool dashed=capital_micro::gap_dash(spawn_positions_.front());
            if constexpr(config::contact_controller)contact_native::note_step(dashed,false,spawn_.is_set(me.position()));
        }
    }
    void recover_program() {
        auto& me=cambc::self();const auto p=me.position();
        if(spawn_.is_set(p)) {
            shop();if(me.health()>=me.max_health()) {mode_=Mode::Work;selected_=resume_;resume_=-1;}
            return;
        }
        float foe_pressure=0;for(const auto& t:threats_) if(t.champion&&dist(p,t.position)<=t.reach) foe_pressure+=t.damage;
        // Static camps cannot chase. Their queried actual footprint decides
        // whether a quiet recall is possible; an entry margin is not a veto.
        if(foe_pressure==0&&hazard(p)==0) {
            if constexpr(config::contact_controller) {
                bool permitted=false;
                if(service_owned()) {
                    auto& contact=contact_native::memory();contact.own=contact_native::own_observation(false);
                    (void)contact.recall.observe(contact.own);permitted=contact.recall.can_start(contact.own,true);
                } else permitted=contact_native::local_recall_quiet(false);
                if(permitted) {
                    if(service_recall())contact_native::note_recall_accepted();return;
                }
            } else {service_recall();return;}
        }
        if(!me.can_move()) return;
        if(retained_exit()) return;
        (void)move(spawn_,true);
        if constexpr(config::contact_controller)if(contact_native::handled())return;
        if(reserve()) (void)pressure(false);
        if constexpr(config::contact_controller)if(contact_native::handled())return;
        if(me.can_cast()&&me.health()<incoming(me.position())*3&&!spawn_positions_.empty()) {
            const bool dashed=capital_micro::guarded::gap_dash<true>(spawn_positions_.front(),service_micro_hooks());
            if constexpr(config::contact_controller)contact_native::note_step(dashed,false,spawn_.is_set(me.position()));
        }
    }
    void bank() {
        if constexpr(config::complete_service_controller) {bank_program();return;}

        auto& me=cambc::self();auto p=me.position();
        if(me.gem_count()==0) {mode_=Mode::Work;selected_=-1;bank_round_=cambc::round_number();return;}
        cambc::bitboard goals;goals.clear();
        const auto enemy_locks=cambc::vision::altar_lock_tiles(team::enemy);
        for(const auto altar:altars_) if(!enemy_locks.is_set(altar)) {
            if(dist(p,altar)<=cambc::constants::submit_gems_range) {me.submit_gems(altar);return;}
            goals|=around(altar,cambc::constants::submit_gems_range);
        }
        if(goals.is_empty()) {
            for(const auto& lock:cambc::vision::altar_locks(team::enemy))
                if(dist(p,lock.position())<=me.attack_range()&&me.can_attack()
                    &&!me.restrictions().contains(cambc::buff_restrictions::attack)) {me.attack(lock);return;}
            for(const auto altar:altars_) goals|=around(altar,me.attack_range());
        }
        // Conversion owns this input's movement. A ranged basic that
        // consumes M must not repeatedly steal an unfinished bank route.
        if(me.can_move()) move(goals);
        if(reserve()) (void)pressure(false);
    }
    void bank_program() {
        auto& me=cambc::self();auto p=me.position();
        if(me.gem_count()==0) {mode_=Mode::Work;selected_=-1;bank_round_=cambc::round_number();return;}
        cambc::bitboard goals;goals.clear();
        const auto enemy_locks=cambc::vision::altar_lock_tiles(team::enemy);
        for(const auto altar:altars_) if(!enemy_locks.is_set(altar)) {
            if(dist(p,altar)<=cambc::constants::submit_gems_range) {
                const auto identity=service_native::mix(4,(std::uint64_t(static_cast<unsigned>(altar.x))<<32)|static_cast<unsigned>(altar.y));
                (void)service_command(service_program::Submit,identity,[&](){
                    if constexpr(config::complete_service_controller)if(service_native::memory().executing) {
                        cambc::detail::check(cambc::detail::sdk_submit_gems(altar.x,altar.y));return true;
                    }
                    me.submit_gems(altar);return true;
                });return;
            }
            goals|=around(altar,cambc::constants::submit_gems_range);
        }
        if(goals.is_empty()) {
            for(const auto& lock:cambc::vision::altar_locks(team::enemy))
                if(dist(p,lock.position())<=me.attack_range()&&me.can_attack()
                    &&!me.restrictions().contains(cambc::buff_restrictions::attack)) {
                    cambc::detail::entity entity{cambc::detail::entity_kind::altar_lock,0};
                    if constexpr(config::complete_service_controller)if(service_native::memory().executing) {
                        cambc::detail::altar_lock_info actual{};const auto position=lock.position();
                        const auto code=cambc::detail::sdk_altar_lock(position.x,position.y,1-service_native::memory().team,&actual);
                        if(code==static_cast<int>(cambc::error_code::api_not_found)||code==static_cast<int>(cambc::error_code::api_not_in_vision))return;
                        cambc::detail::check(code);if(!service_native::current())return;
                        entity={cambc::detail::entity_kind::altar_lock,actual.handle};
                    }
                    const auto identity=service_native::mix(service_native::mix(5,static_cast<std::uint32_t>(entity.kind)),entity.handle);
                    (void)service_command(service_program::Attack,identity,[&](){
                        if constexpr(config::complete_service_controller)if(service_native::memory().executing) {
                            cambc::detail::check(cambc::detail::sdk_attack(static_cast<std::uint32_t>(entity.kind),entity.handle));return true;
                        }
                        me.attack(lock);return true;
                    });return;
                }
            for(const auto altar:altars_) goals|=around(altar,me.attack_range());
        }
        // Conversion owns this input's movement. A ranged basic that
        // consumes M must not repeatedly steal an unfinished bank route.
        if(me.can_move()) move(goals);
        if(reserve()) (void)pressure(false);
    }
    bool release_source() {
        auto& me=cambc::self();const auto p=me.position();
        if(release_active_&&!source_tiles_.is_set(p)) release_active_=false;
        if(!release_active_) for(const auto& g:groups_)
            if(g.respawning&&g.seen==static_cast<int>(cambc::round_number())&&
                std::find(g.positions.begin(),g.positions.end(),p)!=g.positions.end()) {release_active_=true;break;}
        if(!release_active_) return false;
        // Exclude every source footprint, so departure cannot alternate into
        // another captured region. Terminal is actual outside presence.
        const auto goals=~source_tiles_&~terrain_;
        if(me.can_move()) (void)move(goals);
        selected_=-1;return true;
    }
    void failed_prefix(bool supported) {
        auto& me=cambc::self();
        auto& g=groups_[selected_];
        const bool recoverable=supported&&me.health()<me.max_health();
        if(recoverable) resume_=selected_;
        else {
            // Recovery cannot fix unsupported geometry/operator classes or an
            // unaffordable full-health word. Preserve residual work as a
            // nomination, then resolve another family until actual premises change.
            g.refused=true;g.refused_max_hp=me.max_health();g.refused_strength=me.strength();
            g.refused_focus=me.focus();g.refused_work=g.total_hp;resume_=-1;
        }
        mode_=Mode::Return;(void)retained_exit(false);
    }
    void failed_prefix_observed(bool supported,ExitFailure failure) {
        (void)failure;
        auto& me=cambc::self();
        auto& g=groups_[selected_];
        const bool recoverable=supported&&me.health()<me.max_health();
        if(recoverable) resume_=selected_;
        else {
            if constexpr(config::complete_service_controller) {
                auto& owner=service_native::memory();
                if(owner.failures.size()!=groups_.size())owner.failures.resize(groups_.size());
                auto actual=owner.actual;actual.available_fuel=cambc::fuel_remaining();
                owner.failures[selected_]=service_program::failure(failure.reason,actual,
                    {service_program::Kind::Work,selected_,actual.pose,failure.required_fuel});
            }
            // Recovery cannot fix unsupported geometry/operator classes or an
            // unaffordable full-health word. Preserve residual work as a
            // nomination, then resolve another family until actual premises change.
            g.refused=true;g.refused_max_hp=me.max_health();g.refused_strength=me.strength();
            g.refused_focus=me.focus();g.refused_work=g.total_hp;resume_=-1;
        }
        mode_=Mode::Return;(void)retained_exit(false);
    }
    void work() {
        if constexpr(config::complete_service_controller) {work_program();return;}

        auto& me=cambc::self();mark(100);choose();if(selected_<0) return;
        auto& group=groups_[selected_];
        if(group.respawning) {
            if(group.seen==static_cast<int>(cambc::round_number())) {selected_=-1;return;}
            // Obtain a fresh lifecycle from outside every source footprint.
            // Expiration of the remembered timer does not mint live work.
            cambc::bitboard goals;goals.clear();
            for(const auto q:group.positions) goals|=around(q,1);
            goals&=~source_tiles_;
            if(me.can_move()) (void)move(goals);
            return;
        }
        if(group.reward) {
            cambc::bitboard goals;goals.clear();for(const auto q:group.positions) goals.set(q);
            if(!goals.is_set(me.position())) move(goals);
            return;
        }
        mark(101);
        const auto supported=capital_micro::support({incoming(me.position()),8});
        if constexpr(config::contact_controller)if(supported.acted())contact_native::invalidate_snapshot();
        mark(102);
        const cambc::camp* target=nullptr;float best=-std::numeric_limits<float>::infinity();
        for(const auto& camp:group.native.camps()) if(camp.is_visible()&&camp.is_alive()) {
            const float score=camp.strength()*3+200.f/std::max(1.f,camp.health())-dist(me.position(),camp.position());
            if(score>best) {best=score;target=&camp;}
        }
        mark(103);
        if(!target&&reserve(30000)) {
            // Being at nominal attack range is not arrival when bushes hide
            // every unfinished member. Obtain sight or advance into a finite
            // observation ingress; completed members do not seed its goals.
            const auto visible=cambc::vision::visible_tiles();
            vec2 unknown=me.position();int nearest=100000;
            for(std::size_t m=0;m<group.positions.size();++m)
                if(group.hp[m]>0&&!visible.is_set(group.positions[m])&&dist(me.position(),group.positions[m])<nearest)
                    {nearest=dist(me.position(),group.positions[m]);unknown=group.positions[m];}
            cambc::detail::ward_resources wards{};
            cambc::detail::check(cambc::detail::sdk_champion_wards(self_id(),&wards));
            if(nearest<100000&&wards.charges>0&&!me.restrictions().contains(cambc::buff_restrictions::ward)) {
                const auto options=me.ward_placement_tiles()&~terrain_&~static_camps_&~cambc::vision::ward_tiles(team::ally);
                vec2 place=me.position();
                for(const auto q:options.positions()) if(dist(q,unknown)<dist(place,unknown)) place=q;
                if(place!=me.position()&&reserve()) {
                    const bool placed=me.place_ward(place);
                    if constexpr(config::contact_controller)if(placed)contact_native::invalidate_snapshot();
                }
                for(const auto& camp:group.native.camps()) if(camp.is_visible()&&camp.is_alive()) {target=&camp;break;}
            }
        }
        auto hit=[&]() {
            const auto contact_epoch=cambc::round_number();
            if constexpr(config::contact_controller) {
                if(contact_native::handled())return;
                if(contact_native::act(contact_environment()).present)return;
                if(cambc::round_number()!=contact_epoch)return;
            }
            if(!me.can_attack()||!target||!target->is_visible()||!target->is_alive()||dist(me.position(),target->position())>me.attack_range()) return;
            // No second planning pass after A is spent. If this fresh input
            // cannot afford a new quote, execute the previously retained exit.
            if(!reserve(40000)) {
                if(!exit_route_.empty()) {resume_=selected_;mode_=Mode::Return;(void)retained_exit(false);}
                return;
            }
            const auto epoch=cambc::round_number();
            std::vector<vec2> path;
            mark(104);
            if(!exit_path(me.position(),path)) {failed_prefix(false);return;}
            if(!same_epoch(epoch)) return;
            if(!reserve(28000)) {exit_route_=path;exit_cursor_=path.size()>1?1:0;mode_=Mode::Return;(void)retained_exit(false);return;}
            mark(105);
            auto quote=work_quote(*target,path);
            if(!same_epoch(epoch)) return;
            if(quote.supported&&quote.ends_quiet&&!quote.affordable&&reserve(24000)) {
                // Re-quote only an actual changed resource state. Failed or
                // absent shields do not justify a second identical computation.
                mark(106);
                if constexpr(config::contact_controller)contact_native::invalidate_snapshot();
                const bool shielded=capital_micro::detail::shield_for_work(*target,{quote.damage,1},epoch);
                if(shielded&&reserve(22000)&&cambc::round_number()==epoch) {mark(107);quote=work_quote(*target,path);}
            }
            exit_route_=path;exit_cursor_=path.size()>1?1:0;exit_refusals_=0;
            if(!quote.supported||!quote.affordable) {
                failed_prefix(quote.supported);return;
            }
            if(!same_epoch(epoch)) return;
            // This family quotes a basic word, not arbitrary collateral spells.
            mark(108);
            if constexpr(config::contact_controller)contact_native::invalidate_snapshot();
            if(capital_micro::detail::basic(*target,epoch)&&target->is_visible()&&target->is_alive()
                &&capital_micro::detail::reset(*target,epoch))
                (void)capital_micro::detail::basic(*target,epoch);
        };
        hit();
        if constexpr(config::contact_controller)if(contact_native::handled())return;
        if(mode_==Mode::Return) return;
        if(me.can_move()) {
            cambc::bitboard goals;goals.clear();
            if(target&&target->is_visible()&&target->is_alive()) goals=around(target->position(),me.attack_range());
            else for(std::size_t m=0;m<group.positions.size();++m)
                if(group.hp[m]>0) goals.set(group.positions[m]);
            mark(109);move(goals);
        }
        if constexpr(config::contact_controller)if(contact_native::handled())return;
        if(target&&reserve()) hit();
        if(!target&&reserve()) for(const auto& camp:group.native.camps())
            if(camp.is_visible()&&camp.is_alive()) {target=&camp;hit();break;}
    }
    void work_program() {
        auto& me=cambc::self();mark(100);choose();if(selected_<0) return;
        auto& group=groups_[selected_];
        if(group.respawning) {
            if(group.seen==static_cast<int>(cambc::round_number())) {selected_=-1;return;}
            // Obtain a fresh lifecycle from outside every source footprint.
            // Expiration of the remembered timer does not mint live work.
            cambc::bitboard goals;goals.clear();
            for(const auto q:group.positions) goals|=around(q,1);
            goals&=~source_tiles_;
            if(me.can_move()) (void)move(goals);
            return;
        }
        if(group.reward) {
            cambc::bitboard goals;goals.clear();for(const auto q:group.positions) goals.set(q);
            if(!goals.is_set(me.position())) move(goals);
            return;
        }
        mark(101);
        const auto supported=capital_micro::guarded::support<true>({incoming(me.position()),8},service_micro_hooks());
        if constexpr(config::contact_controller)if(supported.acted())contact_native::invalidate_snapshot();
        mark(102);
        const cambc::camp* target=nullptr;float best=-std::numeric_limits<float>::infinity();
        for(const auto& camp:group.native.camps()) if(camp.is_visible()&&camp.is_alive()) {
            const float score=camp.strength()*3+200.f/std::max(1.f,camp.health())-dist(me.position(),camp.position());
            if(score>best) {best=score;target=&camp;}
        }
        mark(103);
        if(!target&&reserve(30000)) {
            // Being at nominal attack range is not arrival when bushes hide
            // every unfinished member. Obtain sight or advance into a finite
            // observation ingress; completed members do not seed its goals.
            const auto visible=cambc::vision::visible_tiles();
            vec2 unknown=me.position();int nearest=100000;
            for(std::size_t m=0;m<group.positions.size();++m)
                if(group.hp[m]>0&&!visible.is_set(group.positions[m])&&dist(me.position(),group.positions[m])<nearest)
                    {nearest=dist(me.position(),group.positions[m]);unknown=group.positions[m];}
            cambc::detail::ward_resources wards{};
            cambc::detail::check(cambc::detail::sdk_champion_wards(self_id(),&wards));
            if(nearest<100000&&wards.charges>0&&!me.restrictions().contains(cambc::buff_restrictions::ward)) {
                const auto options=me.ward_placement_tiles()&~terrain_&~static_camps_&~cambc::vision::ward_tiles(team::ally);
                vec2 place=me.position();
                for(const auto q:options.positions()) if(dist(q,unknown)<dist(place,unknown)) place=q;
                if(place!=me.position()&&reserve()) {
                    const auto identity=service_native::mix(6,(std::uint64_t(static_cast<unsigned>(place.x))<<32)|static_cast<unsigned>(place.y));
                    const bool placed=service_command(0,identity,[&](){
                        if constexpr(config::complete_service_controller)if(service_native::memory().executing) {
                            std::int32_t accepted=0;cambc::detail::check(cambc::detail::sdk_place_ward(place.x,place.y,&accepted));return accepted!=0;
                        }
                        return me.place_ward(place);
                    });
                    if constexpr(config::contact_controller)if(placed)contact_native::invalidate_snapshot();
                }
                for(const auto& camp:group.native.camps()) if(camp.is_visible()&&camp.is_alive()) {target=&camp;break;}
            }
        }
        auto hit=[&]() {
            if constexpr(config::encounter_service_controller)if(encounter_native::memory().claimed_this_input)return;
            const auto contact_epoch=cambc::round_number();
            if constexpr(config::contact_controller) {
                if(!service_owned()&&contact_native::handled())return;
                if(!service_owned()&&contact_native::act(contact_environment()).present)return;
                if(cambc::round_number()!=contact_epoch)return;
            }
            if(!me.can_attack()||!target||!target->is_visible()||!target->is_alive()||dist(me.position(),target->position())>me.attack_range()) return;
            // No second planning pass after A is spent. If this fresh input
            // cannot afford a new quote, execute the previously retained exit.
            if(!reserve(40000)) {
                if(!exit_route_.empty()) {resume_=selected_;mode_=Mode::Return;(void)retained_exit(false);}
                return;
            }
            const auto epoch=cambc::round_number();
            std::vector<vec2> path;
            mark(104);
            if constexpr(config::complete_service_controller) {
                ExitFailure failure;
                if(!exit_path_observed(me.position(),path,&failure)) {failed_prefix_observed(false,failure);return;}
            } else if(!exit_path(me.position(),path)) {failed_prefix(false);return;}
            if(!same_epoch(epoch)) return;
            if(!reserve(28000)) {exit_route_=path;exit_cursor_=path.size()>1?1:0;mode_=Mode::Return;(void)retained_exit(false);return;}
            mark(105);
            auto quote=work_quote(*target,path);
            if(!same_epoch(epoch)) return;
            if(quote.supported&&quote.ends_quiet&&!quote.affordable&&reserve(24000)) {
                // Re-quote only an actual changed resource state. Failed or
                // absent shields do not justify a second identical computation.
                mark(106);
                if constexpr(config::contact_controller)contact_native::invalidate_snapshot();
                const bool shielded=capital_micro::guarded::detail::shield_for_work<true>(*target,{quote.damage,1},epoch,service_micro_hooks());
                if(shielded&&reserve(22000)&&cambc::round_number()==epoch) {mark(107);quote=work_quote(*target,path);}
            }
            exit_route_=path;exit_cursor_=path.size()>1?1:0;exit_refusals_=0;
            if(!quote.supported||!quote.affordable) {
                if constexpr(config::complete_service_controller)failed_prefix_observed(quote.supported,ExitFailure{});
                else failed_prefix(quote.supported);
                return;
            }
            if(!same_epoch(epoch)) return;
            // This family quotes a basic word, not arbitrary collateral spells.
            mark(108);
            if constexpr(config::contact_controller)contact_native::invalidate_snapshot();
            if(capital_micro::guarded::detail::basic<true>(*target,epoch,service_micro_hooks())&&target->is_visible()&&target->is_alive()
                &&capital_micro::guarded::detail::reset<true>(*target,epoch,service_micro_hooks()))
                (void)capital_micro::guarded::detail::basic<true>(*target,epoch,service_micro_hooks());
        };
        hit();
        if constexpr(config::contact_controller)if(contact_native::handled())return;
        if(mode_==Mode::Return) return;
        if(me.can_move()) {
            cambc::bitboard goals;goals.clear();
            if(target&&target->is_visible()&&target->is_alive()) goals=around(target->position(),me.attack_range());
            else for(std::size_t m=0;m<group.positions.size();++m)
                if(group.hp[m]>0) goals.set(group.positions[m]);
            mark(109);move(goals);
        }
        if constexpr(config::contact_controller)if(contact_native::handled())return;
        if(target&&reserve()) hit();
        if(!target&&reserve()) for(const auto& camp:group.native.camps())
            if(camp.is_visible()&&camp.is_alive()) {target=&camp;hit();break;}
    }
    static service_forecast_native::Epoch forecast_epoch() {
        const auto& m=service_native::memory();return {m.owner,m.round,m.input,m.effect_generation};
    }
    void forecast_cache(const service_execution::Frame& actual,int group_index) {
        namespace f=service_forecast;if(group_index<0||group_index>=static_cast<int>(groups_.size())||actual.group.life==f::Life::Unknown)return;
        auto& group=groups_[group_index];group.seen=actual.stamp.round;group.reward=actual.group.life==f::Life::Reward;group.respawning=actual.group.life==f::Life::Respawning;
        if(actual.roster_complete){group.total_hp=0;for(unsigned k=0;k<group.positions.size();++k){
            for(unsigned n=0;n<actual.npcs;++n){if(actual.camps[n].pose==service_forecast::Point{group.positions[k].x,group.positions[k].y})group.hp[k]=actual.camps[n].hp;}
            group.total_hp+=group.hp[k];}}
    }
    bool forecast_execute(bool current_packet=false) {
        namespace c=service_controller_native;namespace e=service_execution;namespace f=service_forecast;namespace sn=service_native;namespace sp=service_program;
        if(!c::profile_active())return false;
        auto* pointer=c::memory();if(!pointer||pointer->stage!=c::Stage::Executing)return false;auto& m=*pointer;
        if(m.group<0||m.group>=static_cast<int>(groups_.size())){m.interpreter.invalidate();m.stage=c::Stage::Idle;return false;}
        selected_=m.group;auto& owner=sn::memory();const auto input=forecast_epoch();
        if(m.physical_pending){m.interpreter.invalidate(e::Reason::UnknownAttempt);m.stage=c::Stage::Idle;++m.counters.handoffs;return false;}
        const bool external_premise=c::premises_current(m);
        const auto stage=m.interpreter.stage();const auto kind=owner.actual.bag?sp::Kind::Bank:
            stage==f::Stage::Return||stage==f::Stage::Invest?sp::Kind::Recovery:sp::Kind::Acquire;
        if(!sp::granted(sn::publish(kind,{sp::FirstKind::FundedPreparation,++owner.action,0,24000,0})))return false;
        const bool memo=current_packet&&m.actual.stamp.owner==input.owner&&m.actual.stamp.round==input.round&&m.actual.stamp.input==input.input&&m.actual.stamp.effect==input.effect&&!owner.pending;
        if(!memo&&!c::measured(c::sm::Stage::Readback,[&]{return c::sample_actual(m,input,groups_[m.group].native,spawn_);})){m.interpreter.invalidate();m.stage=c::Stage::Idle;++m.counters.handoffs;return false;}
        forecast_cache(m.actual,m.group);
        // Actual receipts are historical prefix facts. Consume them before a
        // changed combat premise can withdraw the remaining physical policy.
        const auto receipt_head=c::reconcile_head(m.interpreter,m.actual,external_premise&&c::npc_only_prefix_not_refuted(m));
        const auto head=receipt_head.directive;bool head_pending=true;
        if(head.kind==e::Kind::Complete){++m.counters.renewed;m.stage=c::Stage::Idle;selected_=-1;c::remember_actual(m);return false;}
        if(receipt_head.refuted){m.stage=c::Stage::Idle;c::reject(m,c::Stop::Model);++m.counters.handoffs;c::remember_actual(m);return false;}
        unsigned issued_count=0;
        // All physical words in this class have at most ordinary Move, one
        // cast and two Basics. The observational entrance floor is paid once;
        // the remaining empirical command/readback obligations shrink after
        // each actual prefix. Full native health still qualifies these costs.
        unsigned remaining=4;
        if(memo){remaining=0;const auto& result=m.simulator.result();for(unsigned k=0;k<result.event_count;++k){const auto& event=result.events[k];
            if(event.round!=input.round||event.actor!=input.owner)continue;
            if(event.kind==f::EventKind::Move||event.kind==f::EventKind::Basic||event.kind==f::EventKind::Reset||event.kind==f::EventKind::Shield||event.kind==f::EventKind::BankStarted||event.kind==f::EventKind::Purchased)++remaining;}
            remaining=std::clamp(remaining,1u,4u);}
        else if(m.actual.body.kit==f::Kit::Willow)remaining=m.actual.body.m?1u:m.actual.body.a?1u:0u;
        while(issued_count<4) {
            const auto directive=head_pending?head:m.interpreter.choose(m.actual);head_pending=false;
            if(directive.kind==e::Kind::Complete){++m.counters.renewed;m.stage=c::Stage::Idle;selected_=-1;c::remember_actual(m);return issued_count!=0;}
            if(directive.kind==e::Kind::Handoff){++m.counters.handoffs;m.stage=c::Stage::Idle;c::remember_actual(m);return issued_count!=0;}
            if(directive.kind!=e::Kind::Physical){c::remember_actual(m);return true;}
            const auto required=35000u+9000u*remaining;
            if(cambc::fuel_remaining()<required||!c::splash_has_no_champion(m,directive)){
                m.interpreter.cancel(directive);m.interpreter.invalidate();m.stage=c::Stage::Idle;c::reject(m,c::Stop::Funding);++m.counters.handoffs;
                c::remember_actual(m);return issued_count!=0;
            }
            const auto before=m.actual;const auto primitive=directive.primitive;bool issued=false,accepted=false;
            mode_=directive.stage==f::Stage::Return||directive.stage==f::Stage::Invest?Mode::Return:
                directive.stage==f::Stage::Altar?Mode::Bank:Mode::Work;
            const auto cost_start=cambc::fuel_remaining();
            const auto issue=[&](auto&& command,std::uint32_t permission) {
                return service_command(permission,directive.id,[&](){issued=m.physical_pending=true;++m.counters.commands;return command();});
            };
            switch(primitive.action) {
            case f::Action::Move:{const auto p=vec2{primitive.destination.x,primitive.destination.y};
                const auto legal=blockers().reachable_tiles({m.actual.body.pose.x,m.actual.body.pose.y},1);if(!legal.is_set(p))break;
                accepted=issue([&](){std::int32_t ok=0;cambc::detail::check(cambc::detail::sdk_move_to(p.x,p.y,&ok));return ok!=0;},sp::Move);break;}
            case f::Action::Basic:{
                const auto visible=cambc::vision::visible_tiles();const f::Npc* target=nullptr;for(unsigned k=0;k<m.actual.npcs;++k)if(m.actual.camps[k].id==primitive.target)target=&m.actual.camps[k];
                if(!target||target->hp<=0||!visible.is_set({target->pose.x,target->pose.y})||dist(cambc::self().position(),{target->pose.x,target->pose.y})>cambc::self().attack_range())break;
                accepted=issue([&](){cambc::detail::check(cambc::detail::sdk_attack(static_cast<unsigned>(cambc::detail::entity_kind::camp),primitive.target));return true;},sp::Attack);break;}
            case f::Action::Reset:case f::Action::Shield:{const auto slot=primitive.slot;if(slot>1||!cambc::self().abilities()[slot].is_ready())break;
                accepted=issue([&](){std::int32_t ok=0;cambc::detail::check(cambc::detail::sdk_cast(slot,0,0,-1,-1,&ok));return ok!=0;},sp::Cast);break;}
            case f::Action::Bank:{const auto p=vec2{primitive.destination.x,primitive.destination.y};
                if(dist(cambc::self().position(),p)>cambc::constants::submit_gems_range||cambc::vision::altar_lock_tiles(team::enemy).is_set(p))break;
                accepted=issue([&](){cambc::detail::check(cambc::detail::sdk_submit_gems(p.x,p.y));return true;},sp::Submit);break;}
            case f::Action::Buy:{const auto item=cambc::item_type(primitive.target);const auto count=directive.purchase.count;
                if(!cambc::self().can_buy(item,count))break;
                accepted=issue([&](){cambc::detail::check(cambc::detail::sdk_buy_item(static_cast<unsigned>(item),count));return true;},sp::Buy);break;}
            default:break;
            }
            if(!issued){m.interpreter.cancel(directive);m.interpreter.invalidate();m.stage=c::Stage::Idle;++m.counters.handoffs;return issued_count!=0;}
            if(accepted)++m.counters.accepted;
            ++issued_count;if(remaining)--remaining;
            if(accepted&&primitive.action==f::Action::Move)m.walked=true;
            if(!c::measured(c::sm::Stage::Readback,[&]{return c::sample_actual(m,forecast_epoch(),groups_[m.group].native,spawn_);}))return true;
            forecast_cache(m.actual,m.group);
            if(primitive.action==f::Action::Move)c::observe_history(false);
            if(accepted&&primitive.action==f::Action::Bank&&m.actual.body.channel==f::Channel::Bank){m.bank_directive=directive.id;m.bank_bag=before.body.bag;
                m.bank_altar=primitive.destination;m.bank_start_round=directive.source.round;m.bank_due_round=directive.source.round+int(m.actual.body.channel_ticks);
                m.bank_start_score=cambc::global_submitted_gems(team::ally);m.bank_was_active=true;}
            if(accepted&&primitive.action==f::Action::Buy){auto& receipt=m.receipts[m.receipt_count++];receipt={};receipt.event=++m.receipt_event;
                receipt.program=m.active_program->id;receipt.source_directive=directive.id;receipt.observed=m.actual.stamp;receipt.kind=e::ReceiptKind::Purchase;
                receipt.evidence=e::Evidence::ActualDirect;receipt.source_known=receipt.amount_known=true;receipt.group=m.captured.world.group.id;receipt.generation=m.source_generation;
                receipt.item=directive.purchase.item;receipt.count=directive.purchase.count;receipt.cost=before.body.gold-m.actual.body.gold;
                m.actual.receipts={m.receipts.data(),m.receipt_count};}
            if(!service_forecast_native::current(forecast_epoch()))return true;
            const bool supported_prefix=c::npc_only_prefix_not_refuted(m);m.physical_pending=false;
            if(!m.interpreter.attempt(directive,accepted?e::Attempt::Accepted:e::Attempt::Refused,m.actual)){
                ++m.counters.handoffs;m.stage=c::Stage::Idle;c::remember_actual(m);return true;}
            ++m.counters.reconciled;
            m.counters.dispatch_cost=std::max(m.counters.dispatch_cost,cost_start-cambc::fuel_remaining());
            c::remember_actual(m);
            if(!supported_prefix||!sn::owned()||!c::premises_current(m)){m.interpreter.invalidate();m.stage=c::Stage::Idle;++m.counters.handoffs;return true;}
        }
        return true;
    }
    bool forecast_prepare(unsigned bag) {
        namespace c=service_controller_native;namespace n=service_forecast_native;namespace e=service_execution;namespace f=service_forecast;
        if(!c::profile_active())return false;
        if(bag||mode_==Mode::Return)return false;
        if(!c::funded_storage(forecast_epoch()))return false;
        auto* pointer=c::memory();if(!pointer)return false;auto& m=*pointer;
        ++m.counters.prepare_entries;m.counters.prepare_fuel_max=std::max(m.counters.prepare_fuel_max,std::uint64_t(cambc::fuel_remaining()));
        if(m.stage==c::Stage::Executing)return forecast_execute();
        // The rolling programme owns acquisition. An optional forecast must
        // not change its winner or retarget clock merely by returning NoOffer.
        // Public geometry for its retained nomination is prepared separately.
        if(selected_<0||selected_>=static_cast<int>(groups_.size()))return false;
        const auto source=forecast_epoch();
        if(m.stage==c::Stage::Cold||m.stage==c::Stage::Idle||m.group!=selected_||m.stage==c::Stage::Ready){
            // Pure geometry owns its continuation after physical incumbent
            // work. Fresh publication pays the unchanged75K once, before
            // capture, without repeating SDK board/item collection.
            const c::g::Source geometry_source{1,m.geometry_catalogue_revision,selected_};
            const auto geometry=m.geometry.product(geometry_source);
            if(!geometry||!m.geometry.current(*geometry,geometry_source)){m.counters.prepare_gate=1;return false;}
            const auto& actual=service_native::memory().actual;const auto& investment=actual.next_investment;
            if(!investment.observed||investment.target<0||unsigned(investment.target)>=f::item_limit
                ||investment.inventory_revision!=actual.inventory_revision){m.counters.prepare_gate=2;return false;}
            // Necessary premises of the existing ready-input/full-vision
            // class are already paid facts, not another SDK collection.
            namespace sp=service_program;constexpr auto ready=sp::Move|sp::Attack|sp::Cast;
            if(!actual.alive||actual.stunned||actual.completed_turn||actual.channel!=sp::Channel::None
                ||(actual.permissions&ready)!=ready){m.counters.prepare_gate=3;return false;}
            const auto& history=m.history;
            if(!history.ready||history.source.observer.id!=source.owner||history.stamp.round!=source.round||history.stamp.effect!=source.effect){m.counters.prepare_gate=4;return false;}
            for(const auto point:groups_[selected_].positions){const auto seen=service_bitmaps::bit(service_bitmaps::PackedView{width_,height_,history.visible},point.x,point.y);
                if(!seen.valid||!seen.value){m.counters.prepare_gate=5;return false;}}
            m.counters.prepare_gate=0;
            const auto item=cambc::item_type(static_cast<std::uint32_t>(investment.target));
            if(!c::begin_candidate(m,source,selected_,groups_[selected_].native,*geometry,altars_,item,m.stage==c::Stage::Ready))return false;
        }
        for(unsigned iteration=0;iteration<64&&cambc::fuel_remaining()>m.fallback_obligation;++iteration){
            const auto prior=m.counters.quanta+m.builder.result().work+m.rebinder.result().work;const auto stage=m.stage;const auto discovery=m.discovery;
            c::advance(m,source,64);if(m.stage==c::Stage::Ready||m.stage==c::Stage::Idle)break;
            if(prior==m.counters.quanta+m.builder.result().work+m.rebinder.result().work&&stage==m.stage&&discovery==m.discovery)break;
        }
        if(m.stage!=c::Stage::Ready||!n::current(m.source)||cambc::fuel_remaining()<65000)return false;
        // Reuse only this immutable observed packet inside its exact physical
        // owner/input/effect epoch. Every physical attempt revokes the memo.
        e::Frame initial;initial.body=m.captured.world.bodies[0];initial.group=m.captured.world.group;initial.npcs=m.captured.world.npcs;initial.camps=m.captured.world.camps;
        initial.stamp={source.owner,source.round,source.input,++service_native::memory().sample,source.effect};initial.observed=initial.fresh=initial.permissions_known=initial.roster_complete=initial.in_group_known=initial.on_spawn_known=true;
        initial.on_spawn=spawn_.is_set({initial.body.pose.x,initial.body.pose.y});for(unsigned k=0;k<initial.npcs;++k)initial.in_group=initial.in_group||initial.body.pose==initial.camps[k].pose;
        const e::Binding bind{initial.stamp,m.captured.world.group.id,m.source_generation,m.active_program->id,m.captured.world.revision,m.assumptions,m.captured.world.horizon};
        if(!c::measured(c::sm::Stage::Activation,[&]{return m.interpreter.begin(initial,*m.active_program,m.simulator.result(),bind,m.captured.world);}))return false;
        if(!c::audited_epoch(source))return false;
        m.stage=c::Stage::Executing;m.actual=initial;m.prior={};m.capture_attributed=m.bank_attributed=m.bank_was_active=m.physical_pending=false;
        m.custody_closed=m.income_closed=cambc::constants::respawn_time>1;m.payout_round=-1;m.receipt_count=0;m.physical_input=source.input;m.walked=false;++m.counters.selected;c::remember_actual(m);
        return forecast_execute(true);
    }
    struct ForecastBackground {service_forecast_native::Epoch source{};};
    static bool forecast_background_work(void* context,const service_static_geometry::Step&) {
        const auto& background=*static_cast<ForecastBackground*>(context);
        // The freshness queries are paid work too. Keep the cheap early veto,
        // then establish the remaining publication/yield floor AFTER them.
        if(cambc::fuel_remaining()<=18000||!service_forecast_native::current(background.source))return false;
        return cambc::fuel_remaining()>18000;
    }
    void forecast_background() {
        namespace c=service_controller_native;namespace g=service_static_geometry;namespace b=service_bitmaps;
        if(!c::profile_active())return;
        auto* pointer=c::memory();if(!pointer||pointer->stage==c::Stage::Executing||!ready_)return;auto& m=*pointer;
        if(!m.geometry_catalogue_supported)return;
        ForecastBackground context{forecast_epoch()};if(!forecast_background_work(&context,{}))return;
        // Reserve empty raw vector capacity once. Later small appends cannot
        // unexpectedly clone a growing catalogue inside one work quantum.
        if(!m.geometry_catalogue.capacity()){
            if(area_<=0||unsigned(area_)>g::cell_limit)return;
            m.geometry_catalogue.reserve(unsigned(area_));++m.counters.background_units;return;
        }
        // Own immutable spawn bytes separately: history reset may replace its
        // SDK-export vector on a later input while pure preparation is retained.
        if(m.geometry_spawn.empty()){
            const int own_team=m.history.source.observer.team;if(own_team<0||own_team>1)return;
            const auto& spawn=m.history.spawn[own_team];if(!b::validate(b::PackedView{width_,height_,spawn}).valid)return;
            m.geometry_spawn=spawn;return;
        }
        if(!m.geometry_catalogue_complete){
            unsigned copied=0;while(m.geometry_group_cursor<groups_.size()&&copied<16){
                if(!forecast_background_work(&context,{}))return;const auto group=m.geometry_group_cursor;const auto& source=groups_[group];
                if(m.geometry_member_cursor>=source.positions.size()){++m.geometry_group_cursor;m.geometry_member_cursor=0;continue;}
                if(m.geometry_catalogue.size()==m.geometry_catalogue.capacity()){
                    m.geometry_catalogue_supported=false;m.geometry_result.state=g::State::Unknown;m.geometry_result.reason=g::Reason::Capacity;return;}
                const auto member=m.geometry_member_cursor++;const auto point=source.positions[member];
                m.geometry_catalogue.push_back({int(group),{point.x,point.y},float(source.member_range[member]),source.member_damage[member]>0});
                auto& revision=m.geometry_catalogue_revision;
                for(const auto value:{unsigned(group),unsigned(point.x),unsigned(point.y),unsigned(source.member_range[member]),unsigned(source.member_damage[member]>0)})revision=service_native::mix(revision,value);
                ++copied;
            }
            m.geometry_catalogue_complete=m.geometry_group_cursor==groups_.size();return;
        }
        if(selected_<0||selected_>=static_cast<int>(groups_.size()))return;
        // Finish one retained pure job before another nomination. It changes
        // no incumbent state. An old-group product can publish only for that
        // same group; no all-alternative completion barrier is introduced.
        if(m.geometry_result.state!=g::State::Building&&m.geometry_result.state!=g::State::Deferred){
            const g::Source requested{1,m.geometry_catalogue_revision,selected_};
            if(m.geometry.product(requested))return;m.geometry_source=requested;
            m.geometry_result=m.geometry.begin({requested,b::PackedView{width_,height_,terrain_bits_},
                b::PackedView{width_,height_,m.geometry_spawn},m.geometry_catalogue,true});
        }
        for(unsigned step=0;step<16&&(m.geometry_result.state==g::State::Building||m.geometry_result.state==g::State::Deferred);++step){
            if constexpr(config::service_forecast_leaf_controller)
                m.geometry_result=m.geometry.advance_guarded(m.geometry_source,128,{&context,&Runtime::forecast_background_work});
            else m.geometry_result=m.geometry.advance(m.geometry_source,128,{&context,&Runtime::forecast_background_work});
            m.counters.background_units+=m.geometry_result.last_units;if(m.geometry_result.state==g::State::Deferred)break;
        }
    }
    bool complete_service(unsigned input_epoch,unsigned bag) {
        namespace sn=service_native;namespace sp=service_program;
        if((!sn::rebind_preparation()&&!sn::refresh())||!same_epoch(input_epoch))return false;
        auto& owner=sn::memory();
        auto kind=bag?sp::Kind::Bank:mode_==Mode::Return?sp::Kind::Recovery:sp::Kind::Acquire;
        if(!bag&&mode_!=Mode::Return&&selected_>=0&&owner.actual.source_life) {
            const auto& group=groups_[selected_];kind=group.reward?sp::Kind::Capture:sp::Kind::Work;
        }
        // Publish the implemented rolling economic programme before optional
        // contact enrichment. This first funded stage samples at most selected
        // + one cursor group; raw distance is never counted as completed work.
        constexpr conversion_native::Options obligations{};
        const auto offer=sn::publish(kind,{sp::FirstKind::FundedPreparation,++owner.action,0,15000u+obligations.observation_reserve,0});
        if(!sp::granted(offer))return false;
        // Admit one canonical, pure preparation stage while its entry remains
        // funded. It cannot retarget the retained rolling winner. Every later
        // physical word still receives the ordinary fresh remaining-tail guard.
        if constexpr(config::named_party_controller){named_party_prepare(input_epoch,offer.lease.minimum_fuel);if(!same_epoch(input_epoch))return true;}
        if constexpr(config::service_forecast_controller) {if(forecast_prepare(bag))return true;if(!same_epoch(input_epoch))return true;}
        mark(3);if constexpr(config::service_forecast_stage_audit)service_controller_native::measured(service_controller_native::sm::Stage::LegacyTail,[&]{observe();});else observe();if(!same_epoch(input_epoch))return true;
        mark(4);if constexpr(config::service_forecast_stage_audit)service_controller_native::measured(service_controller_native::sm::Stage::LegacyTail,[&]{threats();});else threats();if(!same_epoch(input_epoch))return true;
        if(!sn::rebind_preparation()||!same_epoch(input_epoch))return true;
        if constexpr(config::encounter_service_controller)if(encounter_restoration())return true;
        const auto p=cambc::self().position();auto& me=cambc::self();
        float foe_cost=0;for(const auto& threat:threats_)if(threat.champion&&dist(p,threat.position)<=threat.reach)foe_cost+=threat.damage;
        if(mode_!=Mode::Return&&foe_cost>0&&me.health()<foe_cost){resume_=selected_;mode_=Mode::Return;}
        const auto investment=owner.state->investment_obligation(owner.actual);
        const bool released=selected_<0||groups_[selected_].unavailable_until>static_cast<int>(cambc::round_number());
        if(!bag&&investment.active&&investment.affordable&&(released||mode_==Mode::Return)) {
            resume_=-1;exit_route_.clear();mode_=Mode::Return;
        }
        if(mode_!=Mode::Return&&spawn_.is_set(p)&&me.health()<me.max_health()) {
            // Actual healing is a closed scheduled input, with Buy handled by
            // shop above. The retained programme/obligations survive this wait.
            publish();return true;
        }
        if(mode_==Mode::Return){mark(5);if constexpr(config::service_forecast_stage_audit)service_controller_native::measured(service_controller_native::sm::Stage::LegacyTail,[&]{recover();});else recover();}
        else if(bag){mode_=Mode::Bank;mark(6);if constexpr(config::service_forecast_stage_audit)service_controller_native::measured(service_controller_native::sm::Stage::LegacyTail,[&]{bank();});else bank();}
        else {
            mark(7);const bool releasing=[&]{if constexpr(config::service_forecast_stage_audit)return service_controller_native::measured(service_controller_native::sm::Stage::LegacyTail,[&]{return release_source();});else return release_source();}();if(!same_epoch(input_epoch))return true;
            if(!releasing){mark(8);const bool alternative=[&]{if constexpr(config::service_forecast_stage_audit)return service_controller_native::measured(service_controller_native::sm::Stage::LegacyTail,[&]{return pressure();});else return pressure();}();if(!same_epoch(input_epoch))return true;
                if(!alternative){if constexpr(config::service_forecast_stage_audit)service_controller_native::measured(service_controller_native::sm::Stage::LegacyTail,[&]{work();});else work();}}
        }
        if(!same_epoch(input_epoch))return true;
        if(me.position()==last_position_)++stuck_;else stuck_=0;
        last_position_=me.position();
        if(stuck_>24&&mode_==Mode::Work&&selected_>=0&&!groups_[selected_].reward){selected_=-1;stuck_=0;}
        if constexpr(config::service_forecast_controller&&config::service_forecast_leaf_controller){
            // Pure geometry changes none of the intent fields. Complete the
            // physical incumbent's publication before optional preparation.
            publish();if(!same_epoch(input_epoch))return true;
        }
        if constexpr(config::service_forecast_controller){service_controller_native::measured(service_controller_native::sm::Stage::StaticChunk,[&]{forecast_background();});if(!same_epoch(input_epoch))return true;}
        if constexpr(!(config::service_forecast_controller&&config::service_forecast_leaf_controller))publish();
        return true;
    }
    void play_service() {
        auto& me=cambc::self();
        // Retained diagnostic data is emitted before optional broad work can
        // consume its serialization tail. It grants no current action lease.
        if constexpr(config::named_party_measure_now){const auto epoch=cambc::round_number();named_party_emit(epoch);named_party_report(epoch);}
        if constexpr(config::service_forecast_controller) {namespace c=service_controller_native;c::audit_input_start();
            if(c::profile_active())c::measured(c::sm::Stage::History,[&]{c::observe_history();});
            else if(auto* memory=c::memory()){memory->interpreter.invalidate(service_execution::Reason::ChangedPremise);memory->stage=c::Stage::Idle;}
            if constexpr(config::service_forecast_stage_audit){if(c::profile_active()||!c::audit_state().final_emitted)c::log_counters();}
            else c::log_counters();}
        if(!ready_) {init();return;}
        if constexpr(config::named_party_controller)bank_native::priority_protocol_reserved()=named_party_wire_reserved();
        unsigned input_epoch=0;
        if constexpr(config::contact_service_controller)input_epoch=cambc::round_number();
        if constexpr(config::contact_controller)contact_native::observe_input(me.is_alive()&&spawn_.is_set(me.position()));
        if constexpr(!config::contact_service_controller)input_epoch=cambc::round_number();
        if constexpr(config::contact_service_controller)if(!same_epoch(input_epoch))return;
        [[maybe_unused]] service_native::Scope<config::complete_service_controller> economic_scope;
        conversion_native::Epoch conversion_epoch{};
        conversion_native::Geometry conversion_geometry{};
        std::array<tribute::Point,conversion_native::max_altars> conversion_altars{};
        if constexpr(config::conversion_controller) {
            conversion_epoch=conversion_runtime::begin(self_id(),static_cast<int>(input_epoch));
            if(altars_.size()<=conversion_altars.size()) {
                for(unsigned i=0;i<altars_.size();++i)conversion_altars[i]={altars_[i].x,altars_[i].y};
                conversion_geometry=conversion_runtime::geometry(conversion_epoch,{conversion_altars.data(),altars_.size()},this,&Runtime::conversion_walkable);
                const auto observed=conversion_runtime::advance(conversion_epoch,conversion_geometry);
                if(!same_epoch(input_epoch))return;
                if(observed.claimed&&!observed.fresh){publish();return;}
            }
        }
        if constexpr(config::complete_service_controller) {
            if(!service_native::begin(self_id(),static_cast<int>(input_epoch),this,&Runtime::service_metadata))return;
            if(!same_epoch(input_epoch))return;
            if constexpr(config::encounter_service_controller) {
                auto& episode=encounter_native::memory();episode.claimed_this_input=false;
                if(!service_native::memory().actual.alive)episode.restoring=false;
                encounter_native::log_counters(self_id(),static_cast<int>(input_epoch));
            }
        }
        // Head observes custody/channel completion before every ordinary
        // communication, death, channel or stun early return. The Session's
        // own protocol is the sole eight-byte writer throughout this probe.
        if constexpr(config::named_party_controller)if(named_party_dispatch(input_epoch))return;
        mark(1);communicate();if(!same_epoch(input_epoch)) return;
        if(!me.is_alive()) {
            if constexpr(config::service_forecast_controller)if(auto* m=service_controller_native::memory()){m->interpreter.invalidate(service_execution::Reason::Death);m->stage=service_controller_native::Stage::Idle;}
            if constexpr(config::complete_service_controller) {
                namespace sn=service_native;namespace sp=service_program;
                auto& owner=sn::memory();const auto investment=owner.state->investment_obligation(owner.actual);
                if(investment.affordable) {
                    const auto offered=sn::publish(sp::Kind::Investment,{sp::FirstKind::PhysicalWord,++owner.action,sp::Buy,0,18000});
                    if(sp::granted(offered))shop();
                }
                if(!same_epoch(input_epoch))return;
            }
            if constexpr(config::contact_service_controller)previous_bag_=0;
            selected_=-1;mode_=Mode::Return;publish();return;
        }
        unsigned bag=0,wallet=0;bool newly_funded=false;
        const auto reconcile_economy=[&]() {
            bag=me.gem_count();wallet=me.gold_count();newly_funded=wallet>last_wallet_;last_wallet_=wallet;
            if(previous_bag_>0&&bag==0) {exit_route_.clear();bank_round_=cambc::round_number();mode_=Mode::Work;selected_=-1;}
            previous_bag_=bag;
        };
        // Actual bag/wallet/receipt effects precede contact's early return.
        if constexpr(config::contact_service_controller)reconcile_economy();
        if constexpr(config::service_forecast_controller)if(forecast_execute()){publish();return;}
        if(me.is_recalling()||me.is_submitting_gems()||me.is_teleporting()) {publish();return;}
        if(me.buffs().has_stun()) {publish();return;}
        mark(2);shop();if(!same_epoch(input_epoch)) return;
        if constexpr(config::contact_service_controller) {wallet=me.gold_count();last_wallet_=wallet;}
        if constexpr(config::conversion_controller) {
            if(bag&&bank_native::ticks(bag)>=2&&conversion_geometry.altars_queried) {
                const auto selected=conversion_runtime::choose(conversion_epoch,conversion_geometry,{this,&Runtime::conversion_prepare,&Runtime::conversion_execute});
                if(!same_epoch(input_epoch))return;
                if(selected.moved||conversion_runtime::memory().fallback_moved)
                    contact_native::note_step(true,true,spawn_.is_set(me.position()));
                if(!same_epoch(input_epoch))return;
                if(selected.claimed){mode_=Mode::Bank;publish();return;}
                if(conversion_runtime::operational_handoff(selected)) {
                    // Current bag conversion remains playable after attribution
                    // continuity is lost. The old receipt stays Unknown/credit0;
                    // it supplies neither a submit certificate nor new credit.
                    mode_=Mode::Bank;mark(6);bank();publish();return;
                }
            }
        }
        if constexpr(config::complete_service_controller) {
            if(complete_service(input_epoch,bag))return;
            if(!same_epoch(input_epoch))return;
        }
        // Contact gets its measured planning reserve before economic observation
        // and route compilation. Static NPC footprints are already initialized.
        if constexpr(config::contact_controller) {
            if(contact_native::act(contact_environment()).present){publish();return;}
            if(!same_epoch(input_epoch))return;
        }
        mark(3);observe();if(!same_epoch(input_epoch)) return;
        mark(4);threats();if(!same_epoch(input_epoch)) return;
        const auto p=me.position();
        if constexpr(!config::contact_service_controller)reconcile_economy();
        float foe_cost=0;for(const auto& t:threats_) if(t.champion&&dist(p,t.position)<=t.reach) foe_cost+=t.damage;
        if(mode_!=Mode::Return&&foe_cost>0&&me.health()<foe_cost) {resume_=selected_;mode_=Mode::Return;}
        if(mode_!=Mode::Return&&spawn_.is_set(p)&&me.health()<me.max_health()) {publish();return;}
        if constexpr(config::contact_controller)if(mode_==Mode::Return||bag>0) {
            if(contact_native::act(contact_environment()).present) {publish();return;}
        }
        if(mode_==Mode::Return) {mark(5);recover();}
        else if(bag>0) {mode_=Mode::Bank;mark(6);bank();}
        else {
            // A gold-only actual receipt can finance a discrete emblem even
            // when no gem channel occurs. Investment is a complete return/shop
            // suffix, not a weight that mistakes unspent gold for score.
            const bool service_released=selected_<0||groups_[selected_].unavailable_until>static_cast<int>(cambc::round_number());
            if(investment_cost_>0&&wallet>=investment_cost_&&
                ((newly_funded&&service_released)||(bank_round_>=0&&cambc::round_number()-bank_round_<8))) {
                resume_=-1;exit_route_.clear();mode_=Mode::Return;
            }
            if(mode_==Mode::Return) {mark(5);recover();}
            else {
                mark(7);const bool releasing=release_source();
                if(!same_epoch(input_epoch)) return;
                if(!releasing) {
                    mark(8);const bool fighting=pressure();
                    if(!same_epoch(input_epoch)) return;
                    if(!fighting) work();
                }
            }
        }
        if(!same_epoch(input_epoch)) return;
        if(me.position()==last_position_) ++stuck_;else stuck_=0;
        last_position_=me.position();
        if(stuck_>24&&mode_==Mode::Work&&selected_>=0&&!groups_[selected_].reward) {selected_=-1;stuck_=0;}
        publish();
    }
public:
    void play() {
        if constexpr(config::contact_service_controller) {play_service();return;}
        auto& me=cambc::self();
        if(!ready_) {init();return;}
        if constexpr(config::contact_controller)contact_native::observe_input(me.is_alive()&&spawn_.is_set(me.position()));
        const auto input_epoch=cambc::round_number();
        mark(1);communicate();if(!same_epoch(input_epoch)) return;
        if(!me.is_alive()) {selected_=-1;mode_=Mode::Return;publish();return;}
        if(me.is_recalling()||me.is_submitting_gems()||me.is_teleporting()) {publish();return;}
        if(me.buffs().has_stun()) {publish();return;}
        mark(2);shop();if(!same_epoch(input_epoch)) return;
        // Contact gets its measured planning reserve before economic observation
        // and route compilation. Static NPC footprints are already initialized.
        if constexpr(config::contact_controller) {
            if(contact_native::act(contact_environment()).present){publish();return;}
            if(!same_epoch(input_epoch))return;
        }
        mark(3);observe();if(!same_epoch(input_epoch)) return;
        mark(4);threats();if(!same_epoch(input_epoch)) return;
        const auto p=me.position();const auto bag=me.gem_count();
        const auto wallet=me.gold_count();const bool newly_funded=wallet>last_wallet_;last_wallet_=wallet;
        if(previous_bag_>0&&bag==0) {exit_route_.clear();bank_round_=cambc::round_number();mode_=Mode::Work;selected_=-1;}
        previous_bag_=bag;
        float foe_cost=0;for(const auto& t:threats_) if(t.champion&&dist(p,t.position)<=t.reach) foe_cost+=t.damage;
        if(mode_!=Mode::Return&&foe_cost>0&&me.health()<foe_cost) {resume_=selected_;mode_=Mode::Return;}
        if(mode_!=Mode::Return&&spawn_.is_set(p)&&me.health()<me.max_health()) {publish();return;}
        if constexpr(config::contact_controller)if(mode_==Mode::Return||bag>0) {
            if(contact_native::act(contact_environment()).present) {publish();return;}
        }
        if(mode_==Mode::Return) {mark(5);recover();}
        else if(bag>0) {mode_=Mode::Bank;mark(6);bank();}
        else {
            // A gold-only actual receipt can finance a discrete emblem even
            // when no gem channel occurs. Investment is a complete return/shop
            // suffix, not a weight that mistakes unspent gold for score.
            const bool service_released=selected_<0||groups_[selected_].unavailable_until>static_cast<int>(cambc::round_number());
            if(investment_cost_>0&&wallet>=investment_cost_&&
                ((newly_funded&&service_released)||(bank_round_>=0&&cambc::round_number()-bank_round_<8))) {
                resume_=-1;exit_route_.clear();mode_=Mode::Return;
            }
            if(mode_==Mode::Return) {mark(5);recover();}
            else {
                mark(7);const bool releasing=release_source();
                if(!same_epoch(input_epoch)) return;
                if(!releasing) {
                    mark(8);const bool fighting=pressure();
                    if(!same_epoch(input_epoch)) return;
                    if(!fighting) work();
                }
            }
        }
        if(!same_epoch(input_epoch)) return;
        if(me.position()==last_position_) ++stuck_;else stuck_=0;
        last_position_=me.position();
        if(stuck_>24&&mode_==Mode::Work&&selected_>=0&&!groups_[selected_].reward) {selected_=-1;stuck_=0;}
        publish();
    }
};
inline void play() {static Runtime runtime;runtime.play();}
inline void draft() {
    const auto selected=cambc::draft::current_draft(team::ally);
    const std::array choices{cambc::champion_draft::varg,cambc::champion_draft::willow,
        cambc::champion_draft::vendetta,cambc::champion_draft::marcella,cambc::champion_draft::ryth};
    for(const auto choice:choices) if(std::find(selected.begin(),selected.end(),choice)==selected.end()) {
        cambc::draft::pick(choice);
        if constexpr(config::ordered_contact_controller)order_native::accepted_pick(selected,choice);
        return;
    }
}
} // namespace wb::capital
