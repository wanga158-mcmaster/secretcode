#include "runtime.h"
#include "config.h"
#include "service_execution.h"
#include "service_price.h"
#include "production_walk.h"
#include "production_potential.h"
#include "production_schedule.h"
#include "production_transit.h"
#include "production_capture.h"
#include "production_history.h"
#include "production_comparison.h"
#include "production_admission.h"
#include "capture_handoff.h"
#include "capture_service.h"
#include "capital_execution.h"
#include "dash_service.h"
#include "recon_execution.h"
#include "assignment_api.h"
#include "assignment_owner.h"
#include "assignment_probe.h"
#include "assignment_stage_probe.h"
#include "production_frontier_handoff.h"
#include "production_word_clock.h"
#include "assignment_static_world.h"
#include "assignment_static_canonical.h"
#include "assignment_static_menu.h"
#include "assignment_fresh_binding.h"
#include "assignment_bound_programme.h"
#include "assignment_cash_service.h"
#include "assignment_cash_routes.h"
#include "assignment_cash_cache.h"
#include "assignment_publication.h"
#include "assignment_reachability_probe.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace tt {
namespace {
enum class Phase { Acquire, Work, Capture, Bank, Exit, Recover };
enum class OfferStatus { Hypothesis, NeedAlternativePath };
struct Offer { float gem_rate{-1},gold_rate{-1},time{}; int group{-1}; OfferStatus status{}; };
enum class AcquisitionStage { Group, Visible, Living, Price, Publish, Waiting, RefreshVisible, RefreshLiving };
struct Acquisition
{
    explicit Acquisition(Observation&& value) : packet{std::move(value)} {}
    // Dated owned query inputs. They never authorize a later game command.
    Observation packet;
    Offer best;
    AcquisitionStage stage{AcquisitionStage::Group};
    int group{},reach{},availability_revision{};
    std::uint64_t generation{},graph{},field{},walk{};
    int next_eligibility{std::numeric_limits<int>::max()};
    bool unsupported{};
    production_recon::MenuCause negative_cause{production_recon::MenuCause::Incomplete};
    int unresolved_group{-1},unresolved_distance{std::numeric_limits<int>::max()};
    std::uint32_t unresolved_members{};
    std::optional<Observation> refresh;
    std::optional<production_comparison::Comparison> comparison;
    std::optional<mb::production::service::Pricer> price;
};
struct AdmissionOffer
{
    Offer price;
    production_admission::Family family{production_admission::Family::Observe};
    std::uint64_t generation{};
    bool present{};
};
struct AdmissionRuntime
{
    std::vector<AdmissionOffer> offers;
    std::vector<std::vector<vec2>> observation_refuted;
    std::vector<bool> observation_exhausted;
    std::optional<Acquisition> withheld;
    int selected{-1},reprice_group{-1};
    std::size_t wait_cursor{};
    std::size_t request_refutation_cursor{};
    production_admission::Family selected_family{production_admission::Family::Observe};
    bool repricing{},reprice_capture{},wait_refresh{};
};
template<bool Enabled> struct AdmissionStorage {};
template<> struct AdmissionStorage<true> { AdmissionRuntime admission; };
template<bool Enabled> struct CaptureStorage {};
template<> struct CaptureStorage<true> { capture_service::State capture_owner; };
struct DeferredFamily
{
    production_transit::Key key;
    vec2 origin{-1,-1};
    int reach{},availability_revision{};
    std::vector<std::uint8_t> visible,living;
    production_history::Snapshot history;
    std::vector<std::uint16_t> dated_field;
    std::optional<production_transit::Probe> probe;
};
struct Memory
{
    Availability availability{Availability::Unknown};
    std::array<float,mb::service::max_members> hp{};
    std::array<bool,mb::service::max_members> exact{};
    int last_seen{-1},respawn_timer{-1};
    bool route_failed{};
    std::uint64_t generation{};
    recon_execution::Status observation_status{recon_execution::Status::NeedCompute};
    vec2 route_origin{-1,-1};
    int route_reach{},route_revision{};
    std::optional<DeferredFamily> alternative_needed;
};
enum class AssignmentBuild { Self,Terrain,Cells,Camps,Groups,Roots,Model,Ready,Unsupported };
struct AssignmentRuntime
{
    AssignmentBuild build{AssignmentBuild::Self};
    std::optional<assignment_api::SelfInputs> self;
    std::optional<assignment_api::SelfInputs> validated;
    std::unique_ptr<assignment_owner::Seed> seed;
    std::conditional_t<cfg::assignment_frontier,std::shared_ptr<assignment_owner::Runtime>,
        std::unique_ptr<assignment_owner::Runtime>> menu;
    assignment_api::CanonicalGroups canonical;
    assignment_api::Runtime mailbox;
    std::optional<assignment_owner::Choice> choice;
    std::optional<assignment_owner::Choice> pending_choice;
    assignment_protocol::ProgrammeGate programme;
    std::vector<std::uint8_t> terrain;
    std::size_t cell{},group{},member{},root{};
    std::uint64_t field{},body{},query{};
    std::array<unsigned,31> life_epoch{};
    std::array<Availability,31> current_life{};
    int serviced_epoch{-1},validated_epoch{-1},choice_local{-1};
    bool ready_reported{},terrain_packed{},nominal_traffic{true},pending_paired{};
    std::uint64_t nominations{},commits{},withdraw_deferred{};
};
template<bool Enabled> struct AssignmentStorage {};
template<> struct AssignmentStorage<true> { AssignmentRuntime assignment; };
struct AssignmentFrontierRuntime
{
    assignment_owner::FrontierCursor cursor;
    std::array<std::optional<assignment_owner::Handle>,assignment_owner::max_candidates> handles;
    std::array<std::shared_ptr<assignment_owner::IndexedProgramme>,assignment_owner::max_candidates> indexed;
    std::array<assignment_owner::PhysicalRefutation,assignment_owner::max_candidates> refutations;
    std::optional<assignment_owner::ProgrammeLease> pending,active;
    std::shared_ptr<const assignment_owner::IndexedProgramme> indexed_active;
    assignment_protocol::FrontierSeedFailure seed_failure{assignment_protocol::FrontierSeedFailure::Transient};
    std::size_t count{},refutation_count{};
    int funded_epoch{-1};
    std::uint64_t accepted{},stale_body{},fallbacks{};
};
template<bool Enabled> struct AssignmentFrontierStorage {};
template<> struct AssignmentFrontierStorage<true> { AssignmentFrontierRuntime assignment_frontier; };
struct AssignmentHandoffRuntime
{
    production_frontier_handoff::Receipt receipt;
    std::optional<assignment_api::SelfCheckpoint> anchor;
    std::uint64_t publication_query{};
};
template<bool Enabled> struct AssignmentHandoffStorage {};
template<> struct AssignmentHandoffStorage<true> { AssignmentHandoffRuntime assignment_handoff; };
struct AssignmentStaticRuntime
{
    std::unique_ptr<assignment_static_canonical::CanonicalBuilder<World>> canonical_work;
    std::vector<std::uint8_t> terrain;
    unsigned cold_stage{};
    std::unique_ptr<assignment_static_world::Builder<World,mb::production::potential::Field>> world;
    std::shared_ptr<const assignment_static_geometry::StaticGeometry> geometry;
    std::unique_ptr<assignment_static_menu::Menu> menu;
    std::uint64_t field{};
    std::optional<assignment_api::SelfCheckpoint> anchor;
    std::optional<assignment_api::SelfInputs> actual;
    std::optional<assignment_fresh_binding::Current> companion;
    std::unique_ptr<assignment_static_geometry::OriginBuilder> origin;
    std::unique_ptr<assignment_fresh_binding::Builder> binding;
    std::shared_ptr<const assignment_static_geometry::TargetGeometry> target;
    std::shared_ptr<assignment_bound_programme::Programme> model,active;
    assignment_cash_cache::Cache cash_cache;
    std::shared_ptr<const assignment_cash_routes::Routes> cash_routes,active_routes;
    assignment_cash_service::State cash;
    std::uint64_t active_programme{},cash_observation{},cash_bank_total{};
    int cash_contact_epoch{-1};
    int cash_renewal_not_before{-1},cash_timing_epoch{-1};
    bool cash_receipt_seen{};
    std::uint64_t publication_query{},serial{};
    std::uint64_t candidate{},candidate_support{};
    std::uint32_t nomination_mask{};
    std::uint64_t preparation_body{};
    std::uint64_t unsupported_collection{};
    int nomination_group{-1};
    std::size_t nomination_scan{},nomination_member{};
    int nomination_best{-1},nomination_distance{std::numeric_limits<int>::max()};
    std::size_t reconstruction_cursor{};
    bool independent_preparation{};
    int incumbent_group{-1},incumbent_phase{-1},checked_epoch{-1};
    bool nominated{},preparing{},unsupported_world{};
};
template<bool Enabled> struct AssignmentStaticStorage {};
template<> struct AssignmentStaticStorage<true> { AssignmentStaticRuntime assignment_static; };
struct AssignmentProbeState
{
    std::uint32_t resets{},collect_stale{},collect_compute{},seeds{},rank_restarts{};
    int collect_epoch{-1},reset_epoch{-1},self_epoch{-1},loop_epoch{-1};
    std::uint32_t collect_status{255},collect_reason{255},collect_stage{15},reset_cause{},self_status{255};
    std::uint32_t rank_start{},loop_units{},loop_site{255};
    std::uint64_t loop_entry_fuel{},loop_last_fuel{};
    bool loop_guard_seen{};
    std::uint32_t attempts{},prior_epoch{0xffffffffu},prior_cost{0xffffffffu},prior_flags{};
};
template<bool Enabled> struct AssignmentProbeStorage {};
template<> struct AssignmentProbeStorage<true> { AssignmentProbeState assignment_probe; };
struct Controller : AdmissionStorage<cfg::admission_service>,CaptureStorage<cfg::capture_service>,
    AssignmentStorage<cfg::assignment_service>,AssignmentProbeStorage<cfg::assignment_probe>,
    AssignmentFrontierStorage<cfg::assignment_service && cfg::assignment_frontier>,
    AssignmentHandoffStorage<cfg::assignment_service && cfg::assignment_frontier && cfg::assignment_handoff>,
    AssignmentStaticStorage<cfg::assignment_service && cfg::assignment_frontier && cfg::assignment_static>
{
    Phase phase{Phase::Acquire};
    int group{-1},last_epoch{-1},service_reach{1};
    vec2 expected{-1,-1},resolution_previous{-1,-1};
    bool initialised{},close_front{},needs_healing{},basic_next{},service_deferred{},service_needs_route{},escape_required{},partial_progress{},walked_this_turn{};
    Route route;
    std::optional<RouteRequest> pending_route;
    std::optional<RequestAssembly> request_assembly;
    std::optional<production_walk::Trail> walk;
    std::optional<mb::production::potential::Field> potential;
    std::uint64_t potential_generation{};
    std::optional<production_transit::Ledger> transit;
    std::optional<production_capture::CaptureWord> capture_word;
    std::optional<production_walk::Retention> capture_retention;
    std::uint64_t capture_word_generation{};
    int capture_group{-1};
    std::uint64_t capture_graph{};
    vec2 capture_frontier{-1,-1};
    std::uint32_t capture_bag{};
    std::optional<production_transit::Probe> transit_probe;
    std::optional<production_history::Snapshot> publication_history;
    std::optional<Acquisition> acquisition;
    std::optional<recon_execution::Request> recon_request;
    std::uint64_t recon_generation{};
    bool recon_negative{},recon_menu_pending{};
    std::uint64_t acquisition_generation{};
    production_transit::Key transit_probe_key;
    std::vector<production_walk::Point> transit_ingress;
    vec2 transit_build_origin{-1,-1};
    bool transit_capacity_checked{};
    std::vector<std::uint8_t> route_visible,route_living;
    int route_reach{},route_availability_revision{};
    float last_hp{},last_max_hp{};
    bool planning_quiet{};
    std::vector<Memory> memory;
    std::vector<Availability> availability;
    int availability_revision{};
    int graph_known_revision{-1};
    std::uint64_t graph_revision{};
    std::vector<std::uint8_t> graph_openings;
    const char* trace_stage{};
    int trace_epoch{},trace_status{},trace_group{-1},trace_detail{},trace_role{};
    std::uint64_t trace_fuel{};
    std::uint64_t transit_fuel0{},transit_fuel1{},transit_fuel2{};
    std::size_t transit_nodes{};
};
template<class State> AdmissionRuntime& admission_data(State& state)
{
    if constexpr(cfg::admission_service) return state.admission;
    else { static AdmissionRuntime unused; return unused; }
}
template<class State> capture_service::State& capture_owner(State& state)
{
    if constexpr(cfg::capture_service)return state.capture_owner;
    else {static capture_service::State unused;return unused;}
}
template<class State> AssignmentRuntime& assignment_data(State& state)
{
    if constexpr(cfg::assignment_service)return state.assignment;
    else {static AssignmentRuntime unused;return unused;}
}
template<class State> AssignmentFrontierRuntime& frontier_data(State& state)
{
    if constexpr(cfg::assignment_service && cfg::assignment_frontier)return state.assignment_frontier;
    else {static AssignmentFrontierRuntime unused;return unused;}
}
template<class State> AssignmentHandoffRuntime& handoff_data(State& state)
{
    if constexpr(cfg::assignment_service && cfg::assignment_frontier && cfg::assignment_handoff)return state.assignment_handoff;
    else {static AssignmentHandoffRuntime unused;return unused;}
}
template<class State> AssignmentStaticRuntime& static_data(State& state)
{
    if constexpr(cfg::assignment_service && cfg::assignment_frontier && cfg::assignment_static)return state.assignment_static;
    else {static AssignmentStaticRuntime unused;return unused;}
}
[[maybe_unused]] void retire_static_ownership(Controller&);
[[maybe_unused]] void static_assignment_published(Controller&,const assignment_api::SelfCheckpoint&,std::uint64_t,int);
[[maybe_unused]] bool static_assignment_input(Controller&);
[[maybe_unused]] bool static_preparation_input(Controller&);
[[maybe_unused]] bool static_cash_input(Controller&);
[[maybe_unused]] void advance_static_tail(Controller&);
[[maybe_unused]] bool static_audit_budget(std::uint64_t work,int epoch,unsigned site)
{
    if constexpr(cfg::assignment_reachability_audit) {
        const auto fuel=cambc::fuel_remaining();
        assignment_reachability_probe::entry_budget(assignment_reachability_probe::role(),epoch,site,14000+work,fuel);
        return fuel>14000+work;
    } else return budget(work);
}
template<class Assembly> void force_assignment_perch(Assembly& assembly,vec2 point)
{
    if constexpr(cfg::assignment_service)
    {assembly.forced_work_root=true;assembly.forced_perch=point;assembly.reach=0;}
}
template<class State> void probe_collect(State& state,int epoch,const assignment_api::SelfInputs& result,unsigned stage)
{
    if constexpr(cfg::assignment_probe)
    {
        auto& p=state.assignment_probe;p.collect_epoch=epoch;p.collect_status=static_cast<unsigned>(result.status);
        p.collect_reason=static_cast<unsigned>(result.reason);p.collect_stage=stage;
        if(result.status==assignment_api::CollectStatus::Stale)++p.collect_stale;
        if(result.status==assignment_api::CollectStatus::NeedCompute)++p.collect_compute;
    }
}
template<class State> void probe_reset(State& state,unsigned cause)
{if constexpr(cfg::assignment_probe){auto& p=state.assignment_probe;++p.resets;p.reset_epoch=state.last_epoch;p.reset_cause=cause;}}
template<class State> void probe_self(State& state,int epoch,Status status)
{if constexpr(cfg::assignment_probe){state.assignment_probe.self_epoch=epoch;state.assignment_probe.self_status=static_cast<unsigned>(status);}}
template<class State> void probe_seed(State& state)
{if constexpr(cfg::assignment_probe)++state.assignment_probe.seeds;}
template<class State> void probe_loop_begin(State& state,int epoch,unsigned site)
{
    if constexpr(cfg::assignment_probe)
    {auto& p=state.assignment_probe;p.loop_epoch=epoch;p.loop_site=site;p.loop_units=0;p.loop_guard_seen=false;p.loop_entry_fuel=p.loop_last_fuel=0;}
}
template<class State> void probe_rank_begin(State& state,int epoch,std::uint32_t start)
{
    if constexpr(cfg::assignment_probe)
    {
        auto& p=state.assignment_probe;if(p.rank_start!=start)++p.rank_restarts;
        p.rank_start=start;probe_loop_begin(state,epoch,1);
    }
}
template<class State> bool probe_loop_budget(State& state,std::uint64_t work)
{
    if constexpr(cfg::assignment_probe)
    {
        // Reuses the EXISTING budget() import and identical threshold. No extra
        // pre-dispatch SDK read is introduced by this diagnostic cache.
        const auto fuel=cambc::fuel_remaining();auto& p=state.assignment_probe;
        if(!p.loop_guard_seen){p.loop_entry_fuel=fuel;p.loop_guard_seen=true;}
        p.loop_last_fuel=fuel;return fuel>14000+work;
    }
    else return budget(work);
}
template<class State> void probe_loop_unit(State& state)
{if constexpr(cfg::assignment_probe)++state.assignment_probe.loop_units;}
Controller& controller() { static Controller value; return value; }
[[maybe_unused]] int assignment_probe_write(const char* data,std::size_t size) noexcept
{
#if defined(_WIN32)
    return ::_write(1,data,static_cast<unsigned>(size));
#else
    return static_cast<int>(::write(1,data,size));
#endif
}
template<class State> void emit_assignment_probe(State& state) noexcept
{
    if constexpr(cfg::assignment_probe && cfg::assignment_service)
    {
        namespace packet=assignment_probe_detail;
        // Cached ordinary epoch is checked BEFORE any diagnostic SDK query.
        const int cached=state.last_epoch;
        if(!packet::selected_epoch(cached))return;
        auto& metrics=state.assignment_probe;
        const auto attempt=std::uint32_t{1}<<static_cast<unsigned>(cached/512-1);
        if(metrics.attempts&attempt)return;
        try
        {
            if(cambc::fuel_remaining()<packet::tail_reserve)return;
            if(cambc::self().champion_name()!=cambc::champion_type::marcella)return;
            const auto epoch=cambc::round_number();if(static_cast<int>(epoch)!=cached)return;
            const auto before=cambc::fuel_remaining();
            if(before<packet::tail_reserve || before>std::numeric_limits<std::uint32_t>::max())return;
            const auto& owner=state.assignment; // Existing EBO owner only; no creating accessor.
            const auto progress=owner.menu?owner.menu->progress():assignment_owner::Progress{};
            const auto detail=owner.menu?owner.menu->diagnostic():assignment_owner::Diagnostic{};
            packet::Words words{};words[0]=packet::magic;words[1]=epoch;words[2]=7;
            const auto byte=[](std::uint32_t value){return value&255u;};
            const auto half=[](std::uint32_t value){return value&65535u;};
            const auto group=state.group>=0?static_cast<std::uint32_t>(state.group)+1:0u;
            std::uint32_t flags=(owner.self?1u:0u)|(owner.seed?2u:0u)|(owner.menu?4u:0u)|
                (owner.choice?8u:0u)|(owner.pending_choice?16u:0u)|(owner.validated_epoch==cached?32u:0u)|
                (state.acquisition?64u:0u)|(owner.pending_paired?128u:0u)|(owner.programme.no_shop()?256u:0u)|
                (owner.programme.pending()?512u:0u)|(owner.nominal_traffic?1024u:0u)|(owner.terrain_packed?2048u:0u)|
                (detail.rank_requested?4096u:0u)|(detail.rank_complete?8192u:0u)|(progress.model_complete?16384u:0u)|
                (metrics.loop_guard_seen?32768u:0u)|(metrics.collect_status!=255?65536u:0u)|
                (metrics.self_status!=255?131072u:0u)|(metrics.loop_epoch==cached?262144u:0u)|
                (metrics.loop_site==1?524288u:0u);
            const bool fits=group<255 && owner.root<(1u<<24) &&
                metrics.loop_entry_fuel<(1u<<18) && owner.field<(1u<<14) && state.acquisition_generation<65536 &&
                metrics.collect_stale<65536 && metrics.collect_compute<65536 && metrics.seeds<65536 && metrics.rank_restarts<65536 &&
                detail.event<1024 && detail.repeat<16384 && detail.copy_leg<256;
            if(!fits)flags|=1u<<23;
            words[3]=byte(static_cast<unsigned>(owner.build))|(byte(static_cast<unsigned>(state.phase))<<8)|
                ((owner.menu?byte(detail.phase):255u)<<16)|((state.acquisition?byte(static_cast<unsigned>(state.acquisition->stage)):255u)<<24);
            words[4]=flags|(byte(group)<<24);
            const auto generation=state.acquisition?state.acquisition->generation:std::numeric_limits<std::uint64_t>::max();
            words[5]=static_cast<std::uint32_t>(generation);words[6]=static_cast<std::uint32_t>(generation>>32);
            words[7]=half(static_cast<std::uint32_t>(state.acquisition_generation))|
                (half(metrics.self_epoch>=0?static_cast<unsigned>(metrics.self_epoch)+1:0)<<16);
            words[8]=static_cast<std::uint32_t>(owner.query);words[9]=static_cast<std::uint32_t>(owner.cell);
            words[10]=half(static_cast<std::uint32_t>(owner.group))|(half(static_cast<std::uint32_t>(owner.member))<<16);
            words[11]=(static_cast<std::uint32_t>(owner.root)&0xffffffu)|(byte(metrics.loop_site)<<24);
            words[12]=static_cast<std::uint32_t>(progress.units);words[13]=static_cast<std::uint32_t>(progress.units>>32);
            words[14]=byte(static_cast<unsigned>(progress.groups_scanned))|(byte(static_cast<unsigned>(progress.candidates))<<8)|
                (byte(static_cast<unsigned>(progress.ready))<<16)|(byte(static_cast<unsigned>(progress.refuted))<<24);
            words[15]=owner.menu?(byte(detail.group)|(byte(detail.family)<<8)|(half(detail.candidate)<<16)):packet::none;
            words[16]=owner.menu?detail.scan_cell:packet::none;words[17]=metrics.loop_units;
            words[18]=owner.menu?(byte(detail.copy_leg)|((detail.event&1023u)<<8)|((detail.repeat&16383u)<<18)):packet::none;
            words[19]=owner.menu?detail.prefix_cursor:packet::none;
            words[20]=(owner.menu?half(detail.rank_start):65535u)|
                (half(metrics.loop_epoch>=0?static_cast<unsigned>(metrics.loop_epoch)+1:0)<<16);
            words[21]=(owner.menu?half(detail.rank_horizon):65535u)|
                (half(metrics.reset_epoch>=0?static_cast<unsigned>(metrics.reset_epoch)+1:0)<<16);
            words[22]=owner.menu?detail.rank_cursor:packet::none;
            words[23]=static_cast<std::uint32_t>(detail.producer_work);words[24]=static_cast<std::uint32_t>(detail.producer_work>>32);
            words[25]=(static_cast<std::uint32_t>(owner.field)&16383u)|((static_cast<std::uint32_t>(metrics.loop_entry_fuel)&262143u)<<14);
            words[26]=static_cast<std::uint32_t>(owner.body);words[27]=static_cast<std::uint32_t>(owner.body>>32);
            words[28]=metrics.resets;words[29]=half(metrics.collect_stale)|(half(metrics.collect_compute)<<16);
            words[30]=half(metrics.seeds)|(half(metrics.rank_restarts)<<16);
            words[31]=metrics.collect_epoch>=0?static_cast<unsigned>(metrics.collect_epoch):packet::none;
            words[32]=byte(metrics.collect_status)|(byte(metrics.collect_reason)<<8)|(byte(metrics.self_status)<<16)|
                (((metrics.collect_stage&15u)|((metrics.reset_cause&15u)<<4))<<24);
            words[33]=metrics.loop_guard_seen?static_cast<std::uint32_t>(metrics.loop_last_fuel):packet::none;
            words[34]=static_cast<std::uint32_t>(before);words[35]=metrics.prior_epoch;
            words[36]=metrics.prior_cost;words[37]=metrics.prior_flags;
            words[38]=cambc::round_number();if(words[38]!=epoch)return;
            words[39]=(owner.menu?byte(detail.reason):255u)|((owner.menu?byte(detail.producer_status):255u)<<8)|
                ((owner.menu?byte(detail.status):255u)<<16)|(1u<<24);
            metrics.attempts|=attempt;metrics.prior_epoch=epoch;metrics.prior_cost=packet::none;metrics.prior_flags=1u|64u;
            // Every field and the epoch fence are captured. No SDK, allocation,
            // formatting library or integer conversion occurs through raw write.
            const auto line=packet::encode(words);
            const auto written=assignment_probe_write(line.data(),line.size());
            if(written==static_cast<int>(line.size()))metrics.prior_flags|=2u;
            else {constexpr char error[]="error assignment probe write\n";assignment_probe_write(error,sizeof(error)-1);}
            const auto after=cambc::fuel_remaining();const auto after_epoch=cambc::round_number();
            if(after_epoch==epoch)metrics.prior_flags|=4u;
            if(after<=before)
            {
                metrics.prior_flags|=8u;const auto cost=before-after;
                if(cost<=std::numeric_limits<std::uint32_t>::max())
                {metrics.prior_cost=static_cast<std::uint32_t>(cost);metrics.prior_flags|=16u;}
            }
            metrics.prior_flags|=32u;
        }
        catch(...)
        {
            metrics.prior_flags&=~32u;
            constexpr char error[]="error assignment probe\n";assignment_probe_write(error,sizeof(error)-1);
        }
    }
}
template<class State> void emit_assignment_stage_packet(State& state) noexcept
{
    if constexpr(cfg::assignment_stage_packet&&cfg::assignment_static&&cfg::assignment_frontier&&cfg::assignment_service)
    {
        namespace packet=assignment_stage_probe;
        const int cached=state.last_epoch;
        if(cached<0||!packet::selected_epoch(cached))return;
        auto& metrics=packet::record();
        if(metrics.prior_transport_epoch==static_cast<std::uint32_t>(cached))return;
        try {
            // A paid diagnostic epilogue only. No planning/admission floor is
            // reduced, no observation is requested, and every field is captured
            // before the single unformatted write.
            if(cambc::fuel_remaining()<18000||cambc::self().champion_name()!=cambc::champion_type::marcella)return;
            const auto epoch=cambc::round_number();if(static_cast<int>(epoch)!=cached)return;
            const auto before=cambc::fuel_remaining();if(before<18000||before>=262144)return;
            if(metrics.epoch!=epoch)packet::mark(epoch,0);
            const auto& data=state.assignment_static;
            packet::Words words{};words[0]=packet::magic;words[1]=epoch;words[2]=metrics.stage;
            const auto byte=[](std::uint32_t value){return value&255u;};
            words[3]=byte(metrics.collector_site)|(byte(metrics.collector_status)<<8)|
                (byte(metrics.collector_reason)<<16)|(byte(metrics.flags)<<24);
            words[4]=metrics.guard_floor;words[5]=metrics.guard_fuel;
            std::uint32_t flags=(data.world?1u:0u)|(data.geometry?2u:0u)|(data.menu?4u:0u)|
                (data.anchor?8u:0u)|(data.actual?16u:0u)|(data.binding?32u:0u)|(data.model?64u:0u)|
                (data.active?128u:0u)|(data.cash_routes?256u:0u)|(data.preparing?512u:0u)|
                (data.independent_preparation?1024u:0u)|(state.assignment.choice?2048u:0u)|
                (metrics.collector_epoch==epoch?4096u:0u)|(metrics.collector_context?8192u:0u)|
                (data.candidate?16384u:0u)|(data.target?32768u:0u)|
                (data.model&&data.model->valuation_ready()?65536u:0u)|
                (data.menu&&data.menu->ready_count()?131072u:0u);
            const auto group=state.group>=0?static_cast<unsigned>(state.group)+1:0u;
            const auto nomination=data.nomination_group>=0?static_cast<unsigned>(data.nomination_group)+1:0u;
            const auto phase=static_cast<unsigned>(state.phase),cash=static_cast<unsigned>(data.cash_cache.status());
            const auto model=data.model?static_cast<unsigned>(data.model->status()):15u;
            if(group>=256||nomination>=256||phase>=256||cash>=16||model>=16)flags|=1u<<29;
            words[7]=byte(phase)|(byte(group)<<8)|((cash&15u)<<16)|((model&15u)<<20)|(byte(nomination)<<24);
            const auto work=packet::fit(data.cash_cache.work_used()),query=packet::fit(data.publication_query);
            if(!work.fits)flags|=1u<<30;if(!query.fits)flags|=1u<<31;
            words[6]=flags;words[8]=work.value;words[9]=query.value;
            words[10]=static_cast<std::uint32_t>(before)|((metrics.prior_transport_flags&127u)<<18);
            words[11]=metrics.prior_transport_cost;
            if(cambc::round_number()!=epoch)return;
            metrics.prior_transport_epoch=epoch;metrics.prior_transport_cost=packet::none;metrics.prior_transport_flags=1;
            const auto line=packet::encode(words);
            const auto written=assignment_probe_write(line.data(),line.size());
            if(written==static_cast<int>(line.size()))metrics.prior_transport_flags|=2;
            else {constexpr char error[]="error static stage packet write\n";assignment_probe_write(error,sizeof(error)-1);}
            const auto after=cambc::fuel_remaining();const auto after_epoch=cambc::round_number();
            if(after_epoch==epoch)metrics.prior_transport_flags|=4;
            if(after<=before) {
                metrics.prior_transport_flags|=8;const auto cost=packet::fit(before-after);
                if(cost.fits){metrics.prior_transport_cost=cost.value;metrics.prior_transport_flags|=16;}
            }
            metrics.prior_transport_flags|=32;
        } catch(...) {
            constexpr char error[]="error static stage packet\n";assignment_probe_write(error,sizeof(error)-1);
        }
    }
}
template<class State> void emit_assignment_reachability_packet(State& state) noexcept
{
    if constexpr(cfg::assignment_reachability_audit&&cfg::assignment_static&&cfg::assignment_frontier&&cfg::assignment_service) {
        namespace audit=assignment_reachability_probe;
        const auto role=audit::role();const int cached=state.last_epoch;
        if(cached<0||!audit::should_emit(role,cached))return;
        try {
            // Diagnostic transport only. No gameplay observation or planning
            // gate is added. Encode a captured fixed array, then write once.
            if(cambc::fuel_remaining()<22000)return;
            const auto epoch=cambc::round_number();if(static_cast<int>(epoch)!=cached)return;
            const auto before=cambc::fuel_remaining();if(before<22000)return;
            const auto words=audit::snapshot(role,epoch);
            if(cambc::round_number()!=epoch)return;
            const auto line=audit::encode(words);
            const auto written=assignment_probe_write(line.data(),line.size());
            const auto after=cambc::fuel_remaining();const auto after_epoch=cambc::round_number();
            std::uint32_t flags=(written==static_cast<int>(line.size())?1u:0u)|(after_epoch==epoch?2u:0u)|(after<=before?4u:0u);
            audit::note_transport(role,epoch,after<=before?before-after:audit::none,flags);
        } catch(...) {
            constexpr char error[]="error reachability packet\n";assignment_probe_write(error,sizeof(error)-1);
        }
    }
}
struct AssignmentProbeScope {Controller& state;~AssignmentProbeScope(){emit_assignment_probe(state);emit_assignment_stage_packet(state);emit_assignment_reachability_packet(state);}};
struct DispatchPacketScope
{
    ~DispatchPacketScope()
    {
        if constexpr(cfg::dispatch_packet)
        {
            // This runs after the ordinary dispatcher, including its early
            // returns. Before the chosen input it performs no diagnostic SDK
            // import or output; unused fuel does not carry to the next input.
            const auto& s=controller();
            if(s.last_epoch!=cfg::packet_round)return;
            try
            {
                if(static_cast<int>(cambc::self().champion_name())!=cfg::packet_role ||
                   cambc::fuel_remaining()<20000)return;
                const auto* q=s.acquisition?&*s.acquisition:nullptr;
                const auto* a=s.request_assembly?&*s.request_assembly:nullptr;
                const auto* p=s.pending_route?&*s.pending_route:nullptr;
                const auto* m=s.group>=0 && static_cast<std::size_t>(s.group)<s.memory.size()?
                    &s.memory[static_cast<std::size_t>(s.group)]:nullptr;
                std::printf("dispatch-packet r%d role%d phase%d g%d pos%d,%d route%d rg%d ro%d,%d rn%zu asm%d ag%d ap%zu pending%d pg%d aq%d ai%d best%d unsupported%d eligibility%d av%d seen%d timer%d failed%d alternative%d escaped%d deferred%d needroute%d hp%.3f fuel%llu\n",
                    s.last_epoch,cfg::packet_role,static_cast<int>(s.phase),s.group,s.expected.x,s.expected.y,
                    static_cast<int>(s.route.status),s.route.group,s.route.origin.x,s.route.origin.y,s.route.distances.size(),
                    a!=nullptr,a?a->group:-1,a?a->phase:0,p!=nullptr,p?p->group:-1,
                    q?static_cast<int>(q->stage):-1,q?q->group:-1,q?q->best.group:-1,q?q->unsupported:false,
                    q?q->next_eligibility:-1,m?static_cast<int>(m->availability):-1,m?m->last_seen:-1,
                    m?m->respawn_timer:-1,m?m->route_failed:false,m?m->alternative_needed.has_value():false,
                    s.escape_required,s.service_deferred,s.service_needs_route,static_cast<double>(s.last_hp),
                    static_cast<unsigned long long>(cambc::fuel_remaining()));
            }
            catch(const std::exception& error)
            { std::printf("diagnostic error: %s\n",error.what()); }
            catch(...) { std::printf("diagnostic error: unknown\n"); }
        }
    }
};
void mark(Controller& state,const char* stage,int epoch,Status status,int detail=0)
{
    if constexpr (cfg::trace_entry)
    {
        state.trace_stage=stage; state.trace_epoch=epoch; state.trace_status=static_cast<int>(status);
        state.trace_group=state.group; state.trace_detail=detail; state.trace_fuel=cambc::fuel_remaining();
    }
}
constexpr vec2 directions[]={{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}};
float residual(const Memory& memory,const GroupSpec& group)
{
    float result=0;
    for (std::size_t i=0;i<group.members.size();++i)
        result+=memory.exact[i] ? memory.hp[i] : group.members[i].max_hp;
    return result;
}
void invalidate(Controller& state,bool retain_capture=false)
{
    state.acquisition.reset();
    state.route=Route{};
    state.pending_route.reset();
    state.request_assembly.reset();
    state.transit.reset();
    state.transit_probe.reset();
    state.publication_history.reset();
    state.transit_ingress.clear();
    state.transit_capacity_checked=false;
    state.capture_retention.reset();
    state.route_visible.clear(); state.route_living.clear();
    if (!retain_capture)
    {
        state.capture_word.reset(); state.capture_group=-1;
        state.capture_frontier={-1,-1};
    }
}
void enter_capture(Controller& state,std::uint32_t bag)
{
    state.phase=Phase::Capture;
    state.capture_bag=bag;
    const bool prepared=state.capture_word && state.capture_group==state.group &&
        state.capture_word->status()==production_capture::Status::Ready;
    invalidate(state,prepared);
}
void remember(Controller& state,const WorkObservation& work)
{
    auto& memory=state.memory[static_cast<std::size_t>(work.group)];
    if (work.availability!=Availability::Unknown)
    {
        if (memory.availability!=work.availability) ++state.availability_revision;
        if constexpr(cfg::recon_service)
            if(work.availability==Availability::Alive && memory.availability!=Availability::Alive)
            {++memory.generation;memory.observation_status=recon_execution::Status::NeedCompute;}
        memory.availability=work.availability;
        memory.respawn_timer=work.respawn_timer;
        state.availability[static_cast<std::size_t>(work.group)]=work.availability;
        if (work.availability==Availability::Respawning) memory.exact.fill(false);
    }
    for (std::size_t i=0;i<work.state.member_count;++i)
    {
        const auto& member=work.state.members[i];
        if (member.hp_exact && member.id>=0 && static_cast<std::size_t>(member.id)<memory.hp.size())
        {
            if (member.hp<=0 && (!memory.exact[member.id] || memory.hp[member.id]>0))
                ++state.availability_revision;
            memory.hp[member.id]=member.hp; memory.exact[member.id]=true;
        }
    }
    // Dead members of a consumed generation are not zero-cost residual work in a
    // future generation. Eligibility after timer0 still requires an actual observation.
    if (work.availability==Availability::Respawning) memory.exact.fill(false);
    memory.last_seen=work.epoch;
}
void remember_group(Controller& state,const GroupObservation& group)
{
    auto& memory=state.memory[static_cast<std::size_t>(group.group)];
    if constexpr(cfg::capture_service)
        if(state.phase==Phase::Capture && group.availability==Availability::Alive && memory.availability!=Availability::Alive)
            memory.exact.fill(false); // captured generation's zero HP is not new Work
    if constexpr(cfg::admission_service)
    {
        if(group.availability==Availability::Alive && memory.availability!=Availability::Alive)
            memory.exact.fill(false); // old Reward zeros are not new-life work HP
        auto& retained=admission_data(state);
        if(group.availability!=Availability::Unknown && static_cast<std::size_t>(group.group)<retained.observation_refuted.size())
        {retained.observation_refuted[static_cast<std::size_t>(group.group)].clear();retained.observation_exhausted[static_cast<std::size_t>(group.group)]=false;}
    }
    if (group.availability!=Availability::Unknown)
    {
        if (memory.availability!=group.availability) ++state.availability_revision;
        if constexpr(cfg::recon_service)
            if(group.availability==Availability::Alive && memory.availability!=Availability::Alive)
            {++memory.generation;memory.observation_status=recon_execution::Status::NeedCompute;}
        memory.availability=group.availability;
        memory.respawn_timer=group.respawn_timer;
        state.availability[static_cast<std::size_t>(group.group)]=group.availability;
    }
    if (group.availability==Availability::Respawning) memory.exact.fill(false);
    else for (std::size_t i=0;i<group.member_count;++i)
        if (group.visible[i])
        {
            if (!group.living[i])
            {
                if (!memory.exact[i] || memory.hp[i]>0) ++state.availability_revision;
                memory.hp[i]=0; memory.exact[i]=true;
            }
            // A formerly dead member observed alive belongs to fresh residual work;
            // an old generation's zero cannot survive the presence contradiction.
            else if (memory.exact[i] && memory.hp[i]<=0)
            {
                memory.exact[i]=false; ++state.availability_revision;
                if constexpr(cfg::recon_service){++memory.generation;memory.observation_status=recon_execution::Status::NeedCompute;}
            }
        }
    memory.last_seen=group.epoch;
    if constexpr(cfg::assignment_service&&cfg::assignment_frontier&&cfg::assignment_static) {
        auto& data=static_data(state);
        if(data.active&&data.cash_receipt_seen&&assignment_data(state).choice_local==group.group&&
           group.status==Status::Ready&&current(group.epoch)) {
            if(group.availability==Availability::Respawning&&group.respawn_timer>=0) {
                const auto earliest=static_cast<std::int64_t>(group.epoch)+group.respawn_timer+1;
                if(earliest<=std::numeric_limits<int>::max()) {
                    data.cash_renewal_not_before=static_cast<int>(earliest);data.cash_timing_epoch=group.epoch;
                }
            } else if(group.availability==Availability::Alive||group.availability==Availability::Reward) {
                data.cash_renewal_not_before=group.epoch;data.cash_timing_epoch=group.epoch;
            }
        }
    }
}
mb::service::State observed_subset(const WorkObservation& work)
{
    auto result=work.state;
    result.member_count=0;
    for (std::size_t i=0;i<work.state.member_count;++i)
        if (work.state.members[i].hp_exact)
            result.members[result.member_count++]=work.state.members[i];
    // Unknown and external sources are carried by cover_prefix's separate reserve.
    result.incoming_complete=true;
    return result;
}
void rebase_self(Observation& observation,const WorkObservation& work)
{
    const auto& body=work.state.body;
    observation.pos={body.pos.x,body.pos.y};
    observation.hp=body.hp; observation.max_hp=body.max_hp;
    observation.attack_range=body.attack_range;
    observation.can_move=body.can_move;
    observation.can_attack=body.can_attack;
    observation.can_cast=body.can_cast;
    // External masks remain an earlier observation. The safety/navigation adapters
    // independently refresh their required fields; this does not publish global freshness.
}
bool better(Offer a,Offer b)
{
    if (a.gem_rate!=b.gem_rate) return a.gem_rate>b.gem_rate;
    return a.gold_rate>b.gold_rate;
}
[[maybe_unused]] void reselect_admission(Controller& state)
{
    if constexpr(cfg::admission_service)
    {
        auto& query=*state.acquisition; query.best=Offer{};
        for(const auto& row:admission_data(state).offers)
            if(row.present && row.generation==query.generation && better(row.price,query.best))query.best=row.price;
    }
}
void retain_admission_offer(Controller& state,Offer offer,production_admission::Family family)
{
    if constexpr(cfg::admission_service)
    {
        auto& rows=admission_data(state).offers;
        auto& retained=admission_data(state);
        if(rows.size()!=World::get().groups.size())
        {rows.resize(World::get().groups.size());retained.observation_refuted.resize(rows.size());retained.observation_exhausted.resize(rows.size());}
        rows[static_cast<std::size_t>(offer.group)]={offer,family,state.acquisition->generation,
            family!=production_admission::Family::Observe || !retained.observation_exhausted[static_cast<std::size_t>(offer.group)]};
    }
}
Offer priced_hypothesis(const Observation& observation,int group,double basics,double incoming)
{
    const auto& info=World::get().groups[static_cast<std::size_t>(group)];
    int home=100000;
    for (auto tile:World::get().spawn_tiles) home=std::min(home,distance(info.center,tile));
    const float refill=static_cast<float>(incoming)*observation.damage_multiplier/std::max(1.0f,observation.max_hp);
    const float time=distance(observation.pos,info.center)+static_cast<float>(basics)+cambc::constants::capture_time+
        refill*(home+cambc::constants::recall_time+2)+1;
    // Restricted fixed-primary-until-death BASIC nomination; neither the per-basic
    // execution target policy nor an execution certificate.
    return {info.gems/time,info.gold/time,time,group};
}
void next_acquisition_group(Acquisition& query)
{
    ++query.group; query.stage=AcquisitionStage::Group;
    query.comparison.reset(); query.price.reset();
}
void retain_negative_obligation(Controller& state,int group,production_recon::MenuCause cause)
{
    if constexpr(!cfg::recon_service)return;
    auto& query=*state.acquisition;
    const auto& world=World::get();
    if(group<0 || static_cast<std::size_t>(group)>=world.groups.size())return;
    const auto& info=world.groups[static_cast<std::size_t>(group)];
    const auto& memory=state.memory[static_cast<std::size_t>(group)];
    using RS=recon_execution::Status;
    if(memory.observation_status==RS::Resolved || memory.observation_status==RS::Exhausted ||
        memory.observation_status==RS::Unsupported || memory.observation_status==RS::UnknownGeometry)return;
    std::array<production_recon::MissingMember,recon_execution::max_members> members{};
    if(info.members.size()>members.size())return;
    for(std::size_t i=0;i<info.members.size();++i)
        members[i]={{info.members[i].pos.x,info.members[i].pos.y},memory.exact[i] && memory.hp[i]<=0};
    const auto missing=production_recon::missing_members(world.width,world.height,
        std::span<const production_recon::MissingMember>{members.data(),info.members.size()},query.packet.visible_bits);
    if(missing.status!=production_recon::MissingStatus::Ready || missing.members==0)return;
    const auto range=distance(query.packet.pos,info.center);
    if(query.unresolved_group<0 || range<query.unresolved_distance)
    {
        query.unresolved_group=group;query.unresolved_members=missing.members;
        query.unresolved_distance=range;query.negative_cause=cause;
    }
}
void nominate_recon(Controller& state)
{
    if constexpr(!cfg::recon_service)return;
    const auto& query=*state.acquisition;
    // Every completed empty menu acknowledges the active information request,
    // even when its last missing member is now visible or work is respawning.
    if(state.recon_request)
    {
        state.recon_negative=true;state.recon_menu_pending=false;
        const auto active=state.recon_request->key.group;
        if(active>=0 && static_cast<std::size_t>(active)<state.memory.size() &&
            state.recon_generation==state.memory[static_cast<std::size_t>(active)].generation &&
            state.recon_request->key.field==state.potential_generation)return;
        state.recon_request.reset();cancel_named_recon();
    }
    const auto resolver=production_recon::select_resolver(query.negative_cause);
    if(resolver==production_recon::Resolver::WaitEligibility ||
        resolver==production_recon::Resolver::ResumeMenu)return;
    // Even an unsupported work class can have a named missing fact. This
    // operator obtains that fact only; a fully seen unsupported kit is excluded.
    if(query.unresolved_group<0 || query.unresolved_members==0)return;
    const auto group=query.unresolved_group;
    const auto& memory=state.memory[static_cast<std::size_t>(group)];
    const auto actor=cambc::self();
    const auto body=(static_cast<std::uint64_t>(actor.champion_name())<<32)^
        static_cast<std::uint64_t>(actor.base_stats().vision_radius);
    // Keep the original missing mask/rank through unchanged lifecycle, even
    // after one requested member has actually become visible.
    if(!state.recon_request || state.recon_request->key.group!=group ||
        state.recon_generation!=memory.generation ||
        state.recon_request->key.body!=body ||
        state.recon_request->key.field!=state.potential_generation)
    {
        recon_execution::Request request;
        request.key={group,state.potential_generation,state.potential_generation,body,
            memory.generation*1099511628211ull ^ query.unresolved_members};
        const auto& info=World::get().groups[static_cast<std::size_t>(group)];
        request.member_count=info.members.size();request.missing_members=query.unresolved_members;
        for(std::size_t i=0;i<info.members.size();++i)
            request.members[i]={info.members[i].pos.x,info.members[i].pos.y};
        state.recon_request=std::move(request);state.recon_generation=memory.generation;
        cancel_named_recon();
    }
    state.recon_negative=true;
}
void prepare_acquisition_price(Controller& state)
{
    auto& query=*state.acquisition;
    const auto& info=World::get().groups[static_cast<std::size_t>(query.group)];
    const auto& memory=state.memory[static_cast<std::size_t>(query.group)];
    namespace price=mb::production::service;
    std::array<price::Member,mb::service::max_members> members{};
    for (std::size_t i=0;i<info.members.size();++i)
    {
        const auto& member=info.members[i];
        members[i]={static_cast<int>(i),{member.pos.x,member.pos.y},
            memory.exact[i]?memory.hp[i]:member.max_hp,member.strength,memory.exact[i]};
    }
    query.price.emplace(std::span<const price::Member>{members.data(),info.members.size()},
        price::Body{std::max(1.0f,query.packet.strength),query.packet.splash?1:0});
    query.stage=AcquisitionStage::Price;
}
// One bounded pure unit, with owned dated inputs. No SDK call or action occurs.
void acquisition_unit(Controller& state)
{
    auto& query=*state.acquisition;
    const auto& world=World::get();
    if (query.stage==AcquisitionStage::Group)
    {
        if (query.group>=static_cast<int>(world.groups.size()))
        { query.stage=AcquisitionStage::Publish; return; }
        const auto& info=world.groups[static_cast<std::size_t>(query.group)];
        const auto& memory=state.memory[static_cast<std::size_t>(query.group)];
        if (info.members.size()>mb::service::max_members)
        { query.unsupported=true; retain_negative_obligation(state,query.group,production_recon::MenuCause::UnsupportedModel);
          next_acquisition_group(query); return; }
        if (!cfg::admission_service && memory.availability==Availability::Respawning &&
            query.packet.epoch+distance(query.packet.pos,info.center)<=memory.last_seen+std::max(0,memory.respawn_timer))
        {
            query.next_eligibility=std::min(query.next_eligibility,
                memory.last_seen+std::max(0,memory.respawn_timer)-distance(query.packet.pos,info.center)+1);
            next_acquisition_group(query); return;
        }
        // Admission retains a rejection in the dated menu that produced this
        // exact request. The legacy cache omits graph/mask provenance and must
        // not suppress a new nomination after meaningful input changes.
        if (!cfg::admission_service && memory.route_failed && memory.route_origin==query.packet.pos && memory.route_reach==query.reach &&
            memory.route_revision==state.availability_revision && memory.availability!=Availability::Reward)
        {
            retain_negative_obligation(state,query.group,production_recon::MenuCause::MenuExhausted);
            next_acquisition_group(query); return;
        }
        if (memory.availability==Availability::Reward)
        {
            const auto offer=priced_hypothesis(query.packet,query.group,0,0);
            retain_admission_offer(state,offer,production_admission::Family::Capture);
            if (better(offer,query.best)) query.best=offer;
            next_acquisition_group(query); return;
        }
        if (memory.alternative_needed)
        {
            const auto& family=*memory.alternative_needed;
            const auto history=state.walk?state.walk->nodes():std::span<const production_walk::Point>{};
            if (family.key.group==query.group && !family.dated_field.empty() &&
                family.origin==query.packet.pos && family.reach==query.reach &&
                family.key.graph==state.graph_revision && family.key.field==state.potential_generation &&
                family.availability_revision==state.availability_revision &&
                state.walk && state.walk->status()==production_walk::Status::Known && family.history.matches(history) &&
                family.probe && family.probe->exhausted() &&
                family.probe->matches(family.key,{query.packet.pos.x,query.packet.pos.y},query.packet.hp))
            {
                query.comparison.emplace(family.visible.size(),query.packet.visible_bits.size(),
                    production_comparison::Key{query.generation,static_cast<std::uint64_t>(2*query.group)});
                query.stage=AcquisitionStage::Visible; return;
            }
        }
        prepare_acquisition_price(state); return;
    }
    if (query.stage==AcquisitionStage::Visible || query.stage==AcquisitionStage::Living)
    {
        const auto& saved=state.memory[static_cast<std::size_t>(query.group)].alternative_needed;
        if (!saved || !query.comparison)
        { query.unsupported=true; retain_negative_obligation(state,query.group,production_recon::MenuCause::UnsupportedModel);
          next_acquisition_group(query); return; }
        const bool visible=query.stage==AcquisitionStage::Visible;
        const auto& lhs=visible?saved->visible:saved->living;
        const auto& rhs=visible?query.packet.visible_bits:query.packet.living_bits;
        const auto result=query.comparison->advance(lhs,rhs,
            {query.generation,static_cast<std::uint64_t>(2*query.group+(visible?0:1))},64);
        if (result==production_comparison::Status::NeedCompute) return;
        if (result==production_comparison::Status::Unknown)
        { query.unsupported=true; retain_negative_obligation(state,query.group,production_recon::MenuCause::UnsupportedModel);
          next_acquisition_group(query); return; }
        if (result==production_comparison::Status::Different)
        { query.comparison.reset(); prepare_acquisition_price(state); return; }
        if (visible)
        {
            query.comparison.emplace(saved->living.size(),query.packet.living_bits.size(),
                production_comparison::Key{query.generation,static_cast<std::uint64_t>(2*query.group+1)});
            query.stage=AcquisitionStage::Living; return;
        }
        // Exact same exhausted representative family: request another physical
        // offer, without changing its HP failures into global infeasibility.
        retain_negative_obligation(state,query.group,production_recon::MenuCause::MenuExhausted);
        next_acquisition_group(query); return;
    }
    if (query.stage==AcquisitionStage::Price)
    {
        if (!query.price) { query.unsupported=true; retain_negative_obligation(state,query.group,production_recon::MenuCause::UnsupportedModel);
          next_acquisition_group(query); return; }
        const auto result=query.price->advance(1);
        if (result==mb::production::service::Status::NeedCompute) return;
        if (result==mb::production::service::Status::Ready)
        {
            const auto& value=query.price->result();
            const auto offer=priced_hypothesis(query.packet,query.group,
                static_cast<double>(value.basic_attacks),value.projected_incoming);
            retain_admission_offer(state,offer,state.memory[static_cast<std::size_t>(query.group)].availability==Availability::Alive?
                production_admission::Family::Work:production_admission::Family::Observe);
            if (better(offer,query.best)) query.best=offer;
        }
        else {
            query.unsupported=true;
            retain_negative_obligation(state,query.group,production_recon::MenuCause::UnsupportedModel);
        }
        if constexpr(cfg::admission_service)
            if(admission_data(state).repricing)
            {
                admission_data(state).repricing=false;
                query.price.reset();query.comparison.reset();query.group=static_cast<int>(world.groups.size());
                query.stage=AcquisitionStage::Publish;reselect_admission(state);return;
            }
        next_acquisition_group(query);
    }
}
[[maybe_unused]] GroupObservation admission_lifecycle(Controller& state,int group,int epoch,vec2 origin)
{
    GroupObservation result;result.epoch=epoch;result.group=group;
    if(!budget(10000)){result.status=Status::NeedCompute;return result;}
    try
    {
        const auto life=World::get().handles[static_cast<std::size_t>(group)].state();
        if(std::holds_alternative<cambc::map::camp_group_alive>(life))result.availability=Availability::Alive;
        else if(std::holds_alternative<cambc::map::camp_group_reward_available>(life))result.availability=Availability::Reward;
        else {result.availability=Availability::Respawning;result.respawn_timer=std::get<cambc::map::camp_group_respawning>(life).timer;}
    }
    catch(const cambc::error& error)
    {if(error.code()!=cambc::error_code::api_not_in_vision)throw;}
    result.status=cambc::self().position()==origin && current(epoch)?Status::Ready:Status::Stale;
    if(result.status==Status::Ready && result.availability!=Availability::Unknown)remember_group(state,result);
    return result;
}
[[maybe_unused]] bool assignment_programme(Controller& state)
{
    if constexpr(cfg::assignment_service)
    {
        const auto& owner=assignment_data(state);
        if(!owner.choice || !owner.programme.no_shop())return false;
        if(state.phase==Phase::Recover || state.phase==Phase::Exit)return owner.programme.selected_home_heal();
        if(state.phase==Phase::Bank)return true;
        return (state.phase==Phase::Work || state.phase==Phase::Capture) && state.group==owner.choice_local;
    }
    return false;
}
[[maybe_unused]] bool assignment_perch(Controller& state)
{
    if constexpr(cfg::assignment_service)
    {
        const auto& owner=assignment_data(state);
        return owner.choice && owner.programme.no_shop() && state.phase==Phase::Work && state.group==owner.choice_local;
    }
    return false;
}
[[maybe_unused]] bool assignment_at_perch(Controller& state,vec2 point)
{
    if(!assignment_perch(state))return false;
    const auto& perch=assignment_data(state).choice->perch;
    return point.x==perch.x && point.y==perch.y;
}
[[maybe_unused]] void retire_assignment(Controller& state,bool physical_refutation)
{
    if constexpr(cfg::assignment_service)
    {
        auto& owner=assignment_data(state);
        if constexpr(cfg::assignment_frontier && cfg::assignment_static)retire_static_ownership(state);
        if constexpr(cfg::assignment_frontier && cfg::assignment_handoff)
        {auto& handoff=handoff_data(state);handoff.receipt={};handoff.anchor.reset();}
        if constexpr(cfg::assignment_frontier)
        {
            auto& frontier=frontier_data(state);
            if(owner.choice && physical_refutation)
            {
                bool retained=false;
                for(std::size_t i=0;i<frontier.refutation_count;++i)
                    retained|=frontier.refutations[i].matches(*owner.choice);
                if(!retained && frontier.refutation_count<frontier.refutations.size())
                    frontier.refutations[frontier.refutation_count++]=assignment_owner::PhysicalRefutation::from(*owner.choice);
            }
            frontier.pending.reset();frontier.active.reset();frontier.indexed_active.reset();
        }
        if(owner.choice && physical_refutation && owner.menu)owner.menu->refute(owner.choice->candidate_id);
        owner.choice.reset();owner.pending_choice.reset();owner.choice_local=-1;owner.pending_paired=false;
        owner.programme.handoff(assignment_protocol::ProgrammeHandoff::OrdinaryFallback);
    }
}
[[maybe_unused]] void reset_assignment_model(Controller& state,unsigned probe_cause=1)
{
    probe_reset(state,probe_cause);
    auto& owner=assignment_data(state);
    retire_assignment(state,false);owner.validated.reset();owner.validated_epoch=-1;
    owner.menu.reset();owner.seed.reset();owner.terrain.clear();
    if constexpr(cfg::assignment_frontier)
    {
        auto& frontier=frontier_data(state);
        frontier.cursor={};frontier.count=0;for(auto& handle:frontier.handles)handle.reset();
        for(auto& index:frontier.indexed)index.reset();
        frontier.seed_failure=assignment_protocol::FrontierSeedFailure::Transient;
        // Physical refutations survive query/calendar replacement. Different
        // body/geometry/word dependencies stop matching without a date reset.
    }
    // Keep the immutable own roster role until any pending withdrawal is sent.
    owner.build=AssignmentBuild::Self;owner.cell=owner.group=owner.member=owner.root=0;
    owner.ready_reported=false;owner.body=owner.field=0;
}
[[maybe_unused]] std::uint64_t assignment_hash(const assignment_api::SelfInputs& self)
{
    return assignment_publication::semantic_hash(self);
}
[[maybe_unused]] bool assignment_cooldowns(const production_job::Snapshot& before,
    const production_job::Snapshot& actual)
{
    return assignment_publication::cooldowns_match(before,actual);
}
[[maybe_unused]] Status assignment_self_matches(Controller& state,int epoch)
{
    auto& owner=assignment_data(state);
    if(!owner.self || !state.potential || owner.field!=state.potential_generation)return Status::NeedObservation;
    if(!budget(10000))return Status::NeedCompute;
    const auto& snapshot=owner.self->snapshot;const auto& actor=cambc::self();
    const auto position=actor.position();const auto flags=actor.state();
    if(position!=vec2{snapshot.body.pos.x,snapshot.body.pos.y} || actor.health()!=snapshot.body.hp ||
       actor.max_health()!=snapshot.body.max_hp || actor.strength()!=snapshot.raw_strength ||
       actor.focus()!=snapshot.raw_focus || actor.attack_range()!=snapshot.body.attack_range ||
       actor.gold_count()!=snapshot.gold || actor.gem_count()!=snapshot.carried_gems ||
       !flags.contains(cambc::champion_state::alive) || flags.contains(cambc::champion_state::recalling) ||
       flags.contains(cambc::champion_state::submitting_gems) || flags.contains(cambc::champion_state::teleporting))
        return current(epoch)?Status::NeedObservation:Status::Stale;
    const auto counts=actor.item_count();std::size_t index{};
    if(counts.size()!=owner.self->inventory_size)return Status::NeedObservation;
    for(const auto count:counts)if(count!=owner.self->inventory[index++])return Status::NeedObservation;
    const auto abilities=actor.abilities();
    for(std::size_t i=0;i<abilities.size();++i)
    {
        if(!abilities[i].info())continue;
        const auto cooldown=abilities[i].cooldown();
        const auto ready=static_cast<std::uint64_t>(snapshot.epoch)+snapshot.cooldown[i];
        if(cooldown<0 || (cooldown>0 && static_cast<std::uint64_t>(epoch)+static_cast<unsigned>(cooldown)!=ready) ||
           (cooldown==0 && static_cast<std::uint64_t>(epoch)<ready))return Status::NeedObservation;
    }
    return actor.position()==position && current(epoch)?Status::Ready:Status::Stale;
}
[[maybe_unused]] std::uint64_t assignment_life_revision(const Memory& memory)
{
    return (memory.generation<<2)^static_cast<std::uint64_t>(memory.availability);
}
[[maybe_unused]] bool assignment_blocked(Controller& state,std::uint8_t canonical)
{
    const auto& owner=assignment_data(state);
    if(canonical>=owner.canonical.count)return true;
    const int local=owner.canonical.local_by_canonical[canonical];
    return local<0 || static_cast<std::size_t>(local)>=state.memory.size() ||
        state.memory[static_cast<std::size_t>(local)].availability==Availability::Respawning;
}
[[maybe_unused]] std::uint64_t assignment_premise(Controller& state,const assignment_protocol::Key& key)
{
    const auto& owner=assignment_data(state);std::uint64_t value=owner.body;
    for(const auto group:{key.group_g,key.group_h})
    {
        if(!owner.menu)return 0;
        const auto choice=owner.menu->best(group,key.start,key.horizon);if(!choice)return 0;
        for(const auto item:{choice->key.generation,choice->key.life_revision,choice->key.geometry_revision,
                            choice->key.word_revision,static_cast<std::uint64_t>(choice->candidate_id)})
            value^=item+0x9e3779b97f4a7c15ull+(value<<6)+(value>>2);
    }
    return value?value:1;
}
[[maybe_unused]] GroupObservation assignment_lifecycle_ticket(Controller& state,std::uint8_t canonical,int epoch)
{
    auto& owner=assignment_data(state);GroupObservation out;out.epoch=epoch;
    if(canonical>=owner.canonical.count)return out;
    out.group=owner.canonical.local_by_canonical[canonical];
    if(owner.life_epoch[canonical]==static_cast<unsigned>(epoch)+1)
    {out.status=Status::Ready;out.availability=owner.current_life[canonical];return out;}
    const auto origin=cambc::self().position();
    out=admission_lifecycle(state,out.group,epoch,origin);
    if(out.status==Status::Ready && current(epoch))
    {
        owner.life_epoch[canonical]=static_cast<unsigned>(epoch)+1;owner.current_life[canonical]=out.availability;
        if(state.acquisition)state.acquisition->availability_revision=state.availability_revision;
        if(owner.menu)
        {
            const auto& memory=state.memory[static_cast<std::size_t>(out.group)];
            owner.menu->refresh_group(canonical,memory.generation,assignment_life_revision(memory));
        }
    }
    return out;
}
struct AssignmentCallback {Controller* state{};int epoch{};};
[[maybe_unused]] bool assignment_ready_callback(void* context,const assignment_protocol::Prepared& prepared)
{
    auto& call=*static_cast<AssignmentCallback*>(context);auto& state=*call.state;
    auto& owner=assignment_data(state);
    if(owner.validated_epoch!=call.epoch || !owner.validated || state.escape_required ||
       state.phase!=Phase::Acquire || !state.acquisition || !owner.menu ||
       prepared.actor!=owner.validated->role || assignment_blocked(state,prepared.key.group_g) ||
       assignment_blocked(state,prepared.key.group_h) || prepared.premise!=assignment_premise(state,prepared.key))return false;
    if(owner.life_epoch[prepared.key.group_g]!=static_cast<unsigned>(call.epoch)+1 ||
       owner.life_epoch[prepared.key.group_h]!=static_cast<unsigned>(call.epoch)+1)return false;
    const auto point=cambc::self().position();
    return point==vec2{owner.validated->snapshot.body.pos.x,owner.validated->snapshot.body.pos.y} && current(call.epoch);
}
[[maybe_unused]] std::optional<assignment_protocol::Prepared> assignment_quote_callback(
    void* context,const assignment_protocol::Key& key)
{
    auto& call=*static_cast<AssignmentCallback*>(context);auto& state=*call.state;
    auto& owner=assignment_data(state);
    if(!owner.menu || !owner.validated || owner.validated_epoch!=call.epoch)return std::nullopt;
    for(const auto group:{key.group_g,key.group_h})
    {
        const auto actual=assignment_lifecycle_ticket(state,group,call.epoch);
        if(actual.status!=Status::Ready || assignment_blocked(state,group))return std::nullopt;
    }
    production_assignment::Key common;
    common.start=key.start;common.horizon=key.horizon;common.workers={key.owner_a,key.owner_b};
    common.groups={key.group_g,key.group_h};
    // These identify the explicit common FULL-generation hypothetical scenario.
    // Neither field is an observed native group generation or a foreign fact.
    common.generations={(static_cast<std::uint64_t>(key.start)<<32)|(static_cast<std::uint64_t>(key.horizon)<<16)|key.group_g,
                        (static_cast<std::uint64_t>(key.start)<<32)|(static_cast<std::uint64_t>(key.horizon)<<16)|key.group_h};
    common.pair_id=common.generations[0]^(common.generations[1]<<1)^(static_cast<std::uint64_t>(key.owner_a)<<8)^key.owner_b;
    const auto answer=owner.menu->quotePair(common);
    if(answer.status!=production_assignment::Status::Ready)return std::nullopt;
    return assignment_api::prepared(answer.difference,assignment_premise(state,key),true,
        assignment_protocol::Scenario::FullGenerationAtCommonStart);
}
[[maybe_unused]] void assignment_withdraw(Controller& state)
{
    if constexpr(cfg::assignment_service)
    {
        auto& owner=assignment_data(state);const auto phase=owner.mailbox.protocol.phase;
        if(!owner.self || phase==assignment_protocol::LocalPhase::Idle || phase==assignment_protocol::LocalPhase::Closed)return;
        if(!budget(12000)) {++owner.withdraw_deferred;return;}
        assignment_api::ControllerContext context;
        context.epoch=static_cast<int>(cambc::round_number());context.role=owner.self->role;
        context.canonical_group_count=owner.canonical.count;context.roster_verified=owner.self->roster_verified;
        context.quiet=false;
        assignment_api::step(owner.mailbox,context);
    }
}
struct AssignmentTail
{
    Controller& state;
    ~AssignmentTail()
    {
        if constexpr(cfg::assignment_service)
        {
            auto& owner=assignment_data(state);
            const auto phase=owner.mailbox.protocol.phase;
            if(phase==assignment_protocol::LocalPhase::Idle || phase==assignment_protocol::LocalPhase::Closed)return;
            // Tail only: physical Return/contact/Work has already run. A missed
            // optional publication is withdrawn when current headroom permits.
            try
            {
                if(owner.serviced_epoch!=static_cast<int>(cambc::round_number()))assignment_withdraw(state);
            }
            catch(const std::exception&)
            {std::printf("error assignment withdrawal\n");}
        }
    }
};
[[maybe_unused]] bool assignment_build_unit(Controller& state,int epoch)
{
    namespace aj=production_job;
    auto& owner=assignment_data(state);const auto& world=World::get();
    if(owner.build==AssignmentBuild::Model)
    {
        if constexpr(cfg::assignment_frontier && cfg::assignment_handoff)
        {
            const auto result=owner.menu->advance_word_programmes(1);
            if(result==assignment_owner::Status::Ready)owner.build=AssignmentBuild::Ready;
            else if(result!=assignment_owner::Status::NeedCompute)owner.build=AssignmentBuild::Unsupported;
            return true;
        }
        const auto result=cfg::assignment_frontier?owner.menu->advance_programmes(1):owner.menu->advance(1);
        if(result==assignment_owner::Status::Ready)owner.build=AssignmentBuild::Ready;
        else if(result!=assignment_owner::Status::NeedCompute)owner.build=AssignmentBuild::Unsupported;
        return true;
    }
    if(!owner.seed)return false;
    auto& seed=*owner.seed;
    const auto cells=static_cast<std::size_t>(world.width)*world.height;
    if(owner.build==AssignmentBuild::Terrain)
    {
        if(!budget(40000))return false;
        owner.terrain=world.terrain.export_linear_buffer();
        // The pinned SDK's actual linear_buffer_size() is ceil(cells/8), even
        // though one nearby comment says one byte/tile. Validate the returned
        // shape and decode it explicitly instead of treating packed bits as cells.
        const auto packed_bytes=cells/8+(cells%8!=0);
        owner.terrain_packed=owner.terrain.size()==packed_bytes;
        if(!owner.terrain_packed && owner.terrain.size()!=cells) {owner.build=AssignmentBuild::Unsupported;return false;}
        seed.grid.terrain.reserve(cells);seed.grid.camp_group.reserve(cells);seed.grid.raw_upper.reserve(cells);
        owner.build=AssignmentBuild::Cells;return current(epoch);
    }
    if(owner.build==AssignmentBuild::Cells)
    {
        if(owner.cell<cells)
        {
            const auto tile=owner.cell++;
            const auto upper=state.potential->at({static_cast<int>(tile%world.width),static_cast<int>(tile/world.width)});
            if(upper.status!=mb::production::potential::Status::Ready || !std::isfinite(upper.upper) || upper.upper<0)
            {owner.build=AssignmentBuild::Unsupported;return false;}
            const bool blocked=owner.terrain_packed?
                (owner.terrain[tile/8]&(std::uint8_t{1}<<(tile%8)))!=0:owner.terrain[tile]!=0;
            seed.grid.terrain.push_back(blocked);seed.grid.camp_group.push_back(-1);seed.grid.raw_upper.push_back(upper.upper);
            return true;
        }
        owner.terrain.clear();owner.group=owner.member=0;owner.build=AssignmentBuild::Camps;return true;
    }
    if(owner.build==AssignmentBuild::Camps)
    {
        if(owner.group<owner.canonical.count)
        {
            const auto local=owner.canonical.local_by_canonical[owner.group];const auto& group=world.groups[static_cast<std::size_t>(local)];
            if(owner.member<group.members.size())
            {
                const auto point=group.members[owner.member++].pos;
                const auto index=static_cast<std::size_t>(point.y)*world.width+point.x;
                if(point.x<0 || point.y<0 || point.x>=world.width || point.y>=world.height || index>=cells ||
                   seed.grid.camp_group[index]>=0) {owner.build=AssignmentBuild::Unsupported;return false;}
                seed.grid.camp_group[index]=static_cast<int>(owner.group);return true;
            }
            ++owner.group;owner.member=0;return true;
        }
        owner.group=0;seed.groups.reserve(owner.canonical.count);owner.build=AssignmentBuild::Groups;return true;
    }
    if(owner.build==AssignmentBuild::Groups)
    {
        if(owner.group<owner.canonical.count)
        {
            const auto canonical=owner.group++;const int local=owner.canonical.local_by_canonical[canonical];
            const auto& catalogue=world.groups[static_cast<std::size_t>(local)];const auto& memory=state.memory[static_cast<std::size_t>(local)];
            assignment_owner::Group group;group.canonical=static_cast<std::uint8_t>(canonical);group.geometry_group=static_cast<int>(canonical);
            auto& job=group.metadata;job.group=canonical;job.generation=memory.generation;
            job.life_revision=assignment_life_revision(memory);job.geometry_revision=owner.field;job.word_revision=owner.body;
            job.width=world.width;job.height=world.height;job.member_count=catalogue.members.size();
            group.supported=job.member_count>0 && job.member_count<=aj::max_members;
            if(!group.supported) {owner.build=AssignmentBuild::Unsupported;return false;}
            for(std::size_t i=0;i<job.member_count;++i)
            {
                const auto& m=catalogue.members[i];job.full_hp[i]=m.max_hp;
                job.members[i]={static_cast<int>(i),static_cast<int>(canonical),{m.pos.x,m.pos.y},m.max_hp,m.strength,m.range,true,0,false};
            }
            job.gems=catalogue.gems;job.gold=catalogue.gold;job.respawn_inputs=static_cast<std::uint32_t>(std::max(0,catalogue.respawn));
            job.capture_payout_inputs=static_cast<std::uint32_t>(std::max(1,cambc::constants::capture_time));
            job.submit_minimum=static_cast<std::uint32_t>(std::max(0,cambc::constants::submit_gem_minimum_duration));
            job.submit_base=static_cast<std::uint32_t>(std::max(0,cambc::constants::submit_gem_base_duration));
            job.submit_multiplier=static_cast<float>(cambc::constants::submit_gem_duration_multiplier);
            job.spawn_healing=cambc::constants::spawn_healing;
            job.heal_inputs=job.spawn_healing>0?static_cast<std::uint32_t>(std::min(1000000.0,std::ceil(1.0/static_cast<double>(job.spawn_healing)))):1;
            job.purchase_readback_inputs=1;job.purchase_when_home=false; // Complete, explicitly bound NoShop incumbent.
            job.gold_multiplier=1+seed.self.shop.effects.treasure;
            if(seed.self.actor==3)job.gold_multiplier+=static_cast<float>(cambc::constants::passive_abilities::vesper_vale_bonus_gold_percent)/100;
            if(seed.self.actor==1)job.bank_gold_per_gem=static_cast<float>(cambc::constants::passive_abilities::marcella_tribute_gold_per_gem);
            job.life=static_cast<aj::Life>(memory.availability);job.enabler=aj::Enabler::ObserveAlive;
            job.observed_round=memory.last_seen>=0?static_cast<std::uint32_t>(memory.last_seen):0;
            job.full_group_known=true;job.conditional_hp=true;job.life_supported=memory.availability!=Availability::Unknown;
            job.continuation_supported=true;job.repeat=true;job.common_full_generation=true;
            seed.groups.push_back(std::move(group));return true;
        }
        owner.root=0;owner.build=AssignmentBuild::Roots;return true;
    }
    if(owner.build==AssignmentBuild::Roots)
    {
        if(owner.root<world.altars.size()) {const auto p=world.altars[owner.root++];seed.grid.altars.push_back({p.x,p.y});return true;}
        const auto index=owner.root-world.altars.size();
        if(index<world.spawn_tiles.size()) {const auto p=world.spawn_tiles[index];seed.grid.home_roots.push_back({p.x,p.y});++owner.root;return true;}
        seed.complete_npc_catalogue=true;seed.conditional_no_future_traffic=true;
        owner.menu=std::make_unique<assignment_owner::Runtime>(std::move(seed));owner.seed.reset();owner.build=AssignmentBuild::Model;return true;
    }
    return false;
}
[[maybe_unused]] bool publish_pending_assignment(Controller& state,int epoch)
{
    namespace ap=assignment_protocol;
    auto& owner=assignment_data(state);
    if(!owner.pending_choice || !owner.validated || owner.validated_epoch!=epoch)return false;
    const auto pending=*owner.pending_choice;
    if(static_cast<std::uint32_t>(epoch)<pending.start)
    {
        const auto admission=owner.programme.admit(static_cast<std::uint32_t>(epoch),true,
            !assignment_blocked(state,pending.group));
        if(admission==ap::ProgrammeAdmission::Waiting)return true;
        retire_assignment(state,false);return true;
    }
    const auto life=assignment_lifecycle_ticket(state,pending.group,epoch);
    if(life.status!=Status::Ready || !current(epoch))return true;
    const auto admission=owner.programme.admit(static_cast<std::uint32_t>(epoch),true,
        life.availability!=Availability::Respawning && !assignment_blocked(state,pending.group));
    if(admission!=ap::ProgrammeAdmission::Started)
    {
        const bool unavailable=life.availability==Availability::Respawning;
        retire_assignment(state,false);
        return unavailable; // Retain all other prepared alternatives after actual unavailability.
    }
    const auto rebound=owner.menu->best(pending.group,pending.start,pending.horizon);
    if(!rebound || (owner.pending_paired && rebound->candidate_id!=pending.candidate_id) ||
       !owner.menu->select(rebound->candidate_id))
    {retire_assignment(state,false);return false;}
    const bool paired=owner.pending_paired;
    const auto best=*rebound;const int local=owner.canonical.local_by_canonical[best.group];
    owner.choice=best;owner.choice_local=local;owner.pending_choice.reset();owner.pending_paired=false;++owner.nominations;
    state.group=local;state.phase=life.availability==Availability::Reward?Phase::Capture:Phase::Work;
    if(state.phase==Phase::Capture)state.capture_bag=owner.validated->snapshot.carried_gems;
    state.close_front=false;state.basic_next=false;state.escape_required=false;state.partial_progress=false;
    state.acquisition.reset();invalidate(state);
    if(budget(10000))std::printf("assignment nominate r%d start%u role%u g%d canonical%u perch%d,%d family%d bank%llu paired%u programme=noshop traffic=conditional\n",
        epoch,best.start,owner.validated->role,local,best.group,best.perch.x,best.perch.y,static_cast<int>(best.family),
        static_cast<unsigned long long>(best.banked_gems_by_H),static_cast<unsigned>(paired));
    return true;
}
[[maybe_unused]] bool assignment_input(Controller& state,int epoch)
{
    namespace ao=assignment_owner;namespace ap=assignment_protocol;
    auto& owner=assignment_data(state);const auto& world=World::get();
    if(!state.acquisition || state.escape_required || !state.potential ||
       state.potential->status()!=mb::production::potential::Status::Ready)return false;
    owner.serviced_epoch=epoch;
    if(owner.pending_choice && static_cast<std::uint32_t>(epoch)>owner.pending_choice->start)
    {retire_assignment(state,false);return false;}
    if(owner.build==AssignmentBuild::Unsupported && owner.query==state.acquisition->generation)return false;
    if(owner.self && owner.build!=AssignmentBuild::Ready && owner.build!=AssignmentBuild::Self &&
       owner.build!=AssignmentBuild::Unsupported)
    {
        const auto matches=assignment_self_matches(state,epoch);
        probe_self(state,epoch,matches);
        if(matches==Status::NeedCompute || matches==Status::Stale)return true;
        if(matches!=Status::Ready)
        {
            assignment_withdraw(state);reset_assignment_model(state);
        }
    }
    if(owner.build==AssignmentBuild::Self || (owner.build==AssignmentBuild::Unsupported && owner.query!=state.acquisition->generation))
    {
        const auto self=assignment_api::collect_self(state.acquisition->packet,epoch,1);
        probe_collect(state,epoch,self,0);
        if(self.status==assignment_api::CollectStatus::NeedCompute)return true;
        if(self.status==assignment_api::CollectStatus::Stale)
        {
            if(current(epoch)) {assignment_withdraw(state);reset_assignment_model(state,2);state.acquisition.reset();}
            return true;
        }
        owner.query=state.acquisition->generation;
        if(self.status!=assignment_api::CollectStatus::Ready) {owner.build=AssignmentBuild::Unsupported;return false;}
        owner.canonical=assignment_api::canonical_groups(world);
        if(!owner.canonical.supported || !self.roster_verified || owner.canonical.count<2 ||
           self.snapshot.carried_gems || world.width>256 || world.height>256)
        {owner.build=AssignmentBuild::Unsupported;return false;}
        owner.self=self;owner.body=assignment_hash(self);owner.self->snapshot.body_revision=owner.body;
        owner.field=state.potential_generation;owner.choice.reset();owner.choice_local=-1;owner.validated_epoch=-1;
        owner.seed=std::make_unique<ao::Seed>();auto& seed=*owner.seed;
        probe_seed(state);
        seed.self=owner.self->snapshot;seed.grid.width=world.width;seed.grid.height=world.height;
        seed.grid.exclusive_ownership=true;seed.grid.require_quiet_bank=true;
        seed.stamp={owner.field,owner.field,owner.body,owner.body,static_cast<int>(seed.self.epoch),{seed.self.body.pos.x,seed.self.body.pos.y}};
        seed.model_start=seed.self.epoch;
        for(const auto cooldown:seed.self.cooldown)seed.model_start=std::max(seed.model_start,seed.self.epoch+cooldown);
        if constexpr(cfg::assignment_frontier && cfg::assignment_handoff)
        {
            const auto clock=production_word_clock::project(seed.self.epoch,seed.self.spells,seed.self.cooldown);
            if(clock.status!=production_word_clock::Status::Ready) {owner.build=AssignmentBuild::Unsupported;return false;}
            seed.model_start=clock.earliest_ready;
        }
        seed.horizon=cambc::constants::max_rounds;
        seed.handoff_inputs.fill(1); // Explicit conditional publication/observer handoffs; no native compute bound claimed.
        owner.cell=owner.group=owner.member=owner.root=0;owner.build=AssignmentBuild::Terrain;
        return true; // Full self collector owns this quiet input; no resource-prefix retry.
    }
    if(owner.build!=AssignmentBuild::Ready)
    {
        probe_loop_begin(state,epoch,0);
        while(probe_loop_budget(state,50000) && current(epoch))
        {const bool progress=assignment_build_unit(state,epoch);probe_loop_unit(state);if(!progress)break;}
        if(owner.build==AssignmentBuild::Unsupported)return false;
        return true; // Fresh publication validation belongs to the NEXT input prefix.
    }
    // No optional model work precedes this full current-self validation input.
    const auto actual=assignment_api::collect_self(state.acquisition->packet,epoch,owner.body);
    probe_collect(state,epoch,actual,1);
    if(actual.status==assignment_api::CollectStatus::NeedCompute)return true;
    if(actual.status==assignment_api::CollectStatus::Stale)
    {
        if(current(epoch)) {assignment_withdraw(state);reset_assignment_model(state,3);state.acquisition.reset();}
        return true;
    }
    if(actual.status!=assignment_api::CollectStatus::Ready || assignment_hash(actual)!=owner.body ||
       !assignment_cooldowns(owner.self->snapshot,actual.snapshot))
    {
        assignment_withdraw(state);owner.build=AssignmentBuild::Unsupported;owner.query=state.acquisition->generation;
        return false;
    }
    owner.validated=actual;owner.validated_epoch=epoch;
    for(std::size_t canonical=0;canonical<owner.canonical.count;++canonical)
    {
        const auto local=owner.canonical.local_by_canonical[canonical];const auto& memory=state.memory[static_cast<std::size_t>(local)];
        owner.menu->refresh_group(static_cast<std::uint8_t>(canonical),memory.generation,assignment_life_revision(memory));
    }
    // A staged local fallback owns this exact date. Do not rank it again at
    // epoch+4 on every wait input or let it act before its quoted start.
    if(owner.pending_choice)return publish_pending_assignment(state,epoch);
    const auto horizon=cambc::constants::max_rounds;const auto start=static_cast<std::uint32_t>(epoch+4);
    if(start>=horizon)return false;
    if(owner.menu->rank(start,horizon)==ao::Status::Unsupported)return false;
    probe_rank_begin(state,epoch,start);
    while(owner.menu->ranking_status(start,horizon)!=ao::Status::Ready && probe_loop_budget(state,36000) && current(epoch))
    {owner.menu->advance(1);probe_loop_unit(state);}
    if(owner.menu->ranking_status(start,horizon)!=ao::Status::Ready || !current(epoch))return true;
    if(!owner.ready_reported && budget(10000))
    {
        const auto progress=owner.menu->progress();
        std::printf("assignment menu r%d role%u groups%zu candidates%zu ready%zu units%llu programme=noshop traffic=conditional\n",
            epoch,actual.role,progress.groups_scanned,progress.candidates,progress.ready,static_cast<unsigned long long>(progress.units));
        owner.ready_reported=true;
    }
    std::optional<ao::Choice> best,second;
    for(std::size_t group=0;group<owner.canonical.count;++group)
    {
        if(assignment_blocked(state,static_cast<std::uint8_t>(group)))continue;
        const auto choice=owner.menu->best(static_cast<std::uint8_t>(group),start,horizon);
        if(!choice || !choice->banked_gems_by_H)continue;
        if(!best || choice->banked_gems_by_H>best->banked_gems_by_H ||
           (choice->banked_gems_by_H==best->banked_gems_by_H && choice->group<best->group)) {second=best;best=choice;}
        else if(!second || choice->banked_gems_by_H>second->banked_gems_by_H)second=choice;
    }
    if(!best)return false;
    AssignmentCallback callback{&state,epoch};assignment_api::ControllerContext context;
    context.epoch=epoch;context.role=actual.role;context.canonical_group_count=owner.canonical.count;
    context.roster_verified=actual.roster_verified;context.quiet=true;context.validator_context=&callback;
    context.ready_validator=assignment_ready_callback;context.quote_provider=assignment_quote_callback;
    const auto protocol_phase=owner.mailbox.protocol.phase;
    if(protocol_phase!=ap::LocalPhase::Idle && protocol_phase!=ap::LocalPhase::Closed && owner.mailbox.protocol.retained)
        context.own=assignment_quote_callback(&callback,owner.mailbox.protocol.retained->key);
    else if(second && actual.role<4)
    {
        const ap::Key key{static_cast<std::uint16_t>(start),static_cast<std::uint16_t>(horizon),
            static_cast<std::uint8_t>(actual.role),static_cast<std::uint8_t>(actual.role+1),
            std::min(best->group,second->group),std::max(best->group,second->group)};
        context.own=assignment_quote_callback(&callback,key);
    }
    const auto exchange=assignment_api::step(owner.mailbox,context);
    if(exchange.transport!=assignment_api::CollectStatus::Ready)return true;
    if(exchange.decision.status==ap::Status::Published || exchange.decision.status==ap::Status::Waiting ||
       exchange.decision.status==ap::Status::NeedQuotes)return true;
    if(exchange.decision.nomination)
    {
        const auto& nomination=*exchange.decision.nomination;
        const auto paired=owner.menu->best(nomination.group,nomination.key.start,nomination.key.horizon);
        if(paired) {best=paired;++owner.commits;}
    }
    const auto life=assignment_lifecycle_ticket(state,best->group,epoch);
    if(!current(epoch) || life.status!=Status::Ready)return true;
    state.acquisition->availability_revision=state.availability_revision;
    if(life.availability==Availability::Respawning)return true; // retained alternatives are selected next input
    if(const auto refreshed=owner.menu->best(best->group,best->start,best->horizon))best=refreshed;
    else return false;
    retire_assignment(state,false);
    if(!owner.programme.stage(best->start,best->horizon))return false;
    owner.pending_choice=best;owner.pending_paired=exchange.decision.nomination.has_value();
    return publish_pending_assignment(state,epoch);
}
template<class Pointer> std::shared_ptr<const assignment_owner::Runtime> frontier_pin(const Pointer& pointer)
{
    if constexpr(cfg::assignment_frontier)return pointer;
    else return {};
}
template<class Pointer> std::shared_ptr<assignment_owner::Runtime> mutable_frontier_pin(const Pointer& pointer)
{
    if constexpr(cfg::assignment_frontier)return pointer;
    else return {};
}
[[maybe_unused]] void poll_assignment_frontier(Controller& state)
{
    auto& owner=assignment_data(state);auto& frontier=frontier_data(state);
    if(!owner.menu)return;
    if(frontier.count<frontier.handles.size())
    {
        const auto row=owner.menu->poll_programme(frontier_pin(owner.menu),frontier.cursor);
        if(row.state==assignment_owner::FrontierState::Candidate && row.handle)
        {
            const auto slot=frontier.count++;frontier.handles[slot]=*row.handle;
            frontier.indexed[slot]=assignment_owner::IndexedProgramme::open(mutable_frontier_pin(owner.menu),*row.handle);
            if(!frontier.indexed[slot])owner.menu->resume_dense(*row.handle);
        }
    }
    // Only the newest row can block producer enumeration. One checked index
    // unit per optional poll; ProducerReady alone supplies no valuation.
    if(frontier.count)
    {
        const auto slot=frontier.count-1;auto& indexed=frontier.indexed[slot];
        if(indexed)
        {
            const auto value=indexed->advance(1);
            if(value==production_calendar::Status::Ready)indexed->release_dense();
            else if(value==production_calendar::Status::DenseFallback || value==production_calendar::Status::Invalid)
                owner.menu->resume_dense(*frontier.handles[slot]);
        }
    }
}
[[maybe_unused]] bool frontier_refuted(Controller& state,const assignment_owner::Choice& choice)
{
    const auto& frontier=frontier_data(state);
    for(std::size_t i=0;i<frontier.refutation_count;++i)
        if(frontier.refutations[i].matches(choice))return true;
    return false;
}
[[maybe_unused]] void seed_assignment_frontier(Controller& state,const assignment_api::SelfInputs& self,
    std::uint64_t query)
{
    namespace ao=assignment_owner;
    auto& owner=assignment_data(state);const auto& world=World::get();
    owner.query=query;owner.canonical=assignment_api::canonical_groups(world);
    if(!owner.canonical.supported || !self.roster_verified || owner.canonical.count<2 ||
       world.width>256 || world.height>256)
    {
        frontier_data(state).seed_failure=assignment_protocol::FrontierSeedFailure::Permanent;
        owner.build=AssignmentBuild::Unsupported;return;
    }
    if(self.snapshot.carried_gems)
    {
        frontier_data(state).seed_failure=assignment_protocol::FrontierSeedFailure::Transient;
        owner.build=AssignmentBuild::Unsupported;return;
    }
    owner.self=self;owner.body=assignment_hash(self);owner.self->snapshot.body_revision=owner.body;
    owner.field=state.potential_generation;owner.validated_epoch=-1;
    owner.seed=std::make_unique<ao::Seed>();auto& seed=*owner.seed;
    seed.self=owner.self->snapshot;seed.grid.width=world.width;seed.grid.height=world.height;
    seed.grid.exclusive_ownership=true;seed.grid.require_quiet_bank=true;
    seed.stamp={owner.field,owner.field,owner.body,owner.body,static_cast<int>(seed.self.epoch),
        {seed.self.body.pos.x,seed.self.body.pos.y}};
    seed.model_start=seed.self.epoch;
    for(const auto cooldown:seed.self.cooldown)seed.model_start=std::max(seed.model_start,seed.self.epoch+cooldown);
    if constexpr(cfg::assignment_handoff)
    {
        const auto clock=production_word_clock::project(seed.self.epoch,seed.self.spells,seed.self.cooldown);
        if(clock.status!=production_word_clock::Status::Ready)
        {owner.build=AssignmentBuild::Unsupported;frontier_data(state).seed_failure=assignment_protocol::FrontierSeedFailure::Permanent;return;}
        seed.model_start=clock.earliest_ready;
    }
    seed.horizon=cambc::constants::max_rounds;
    seed.handoff_inputs.fill(1); // Includes this publication; not a native planning-time bound.
    owner.cell=owner.group=owner.member=owner.root=0;owner.build=AssignmentBuild::Terrain;
    frontier_data(state).seed_failure=assignment_protocol::FrontierSeedFailure::None;
    probe_seed(state);
}
// Called only AFTER ordinary acquisition publication was funded. No SDK action
// has occurred between the own-only checkpoint and this optional replacement.
// Incomplete models, low headroom or invalid forecasts leave that incumbent in
// place; they do not acquire an input merely to finish all groups or rank them.
[[maybe_unused]] void assignment_frontier_published(Controller& state,
    const assignment_api::SelfCheckpoint& anchor,std::uint64_t query,int epoch)
{
    if constexpr(cfg::assignment_static)
    {static_assignment_published(state,anchor,query,epoch);return;}
    namespace ao=assignment_owner;namespace ap=assignment_protocol;
    auto& owner=assignment_data(state);auto& frontier=frontier_data(state);
    frontier.funded_epoch=epoch;++frontier.fallbacks;owner.serviced_epoch=epoch;
    if(!state.potential || state.potential->status()!=mb::production::potential::Status::Ready ||
       state.escape_required || !current(epoch))return;
    if(owner.build==AssignmentBuild::Unsupported && ap::frontier_seed_rearm(frontier.seed_failure,
       frontier.funded_epoch==epoch,static_cast<bool>(owner.menu),static_cast<bool>(owner.seed),
       frontier.pending.has_value() || frontier.active.has_value() || static_cast<bool>(frontier.indexed_active)))
        owner.build=AssignmentBuild::Self;
    if(owner.self && owner.build!=AssignmentBuild::Self)
    {
        const auto matches=assignment_self_matches(state,epoch);probe_self(state,epoch,matches);
        if(matches==Status::NeedCompute || matches==Status::Stale)return;
        if(matches!=Status::Ready)
        {++frontier.stale_body;assignment_withdraw(state);reset_assignment_model(state);}
    }
    if(owner.build==AssignmentBuild::Self)
    {
        const auto actual=assignment_api::collect_self(anchor,epoch,1);probe_collect(state,epoch,actual,0);
        if(actual.status==assignment_api::CollectStatus::NeedCompute || actual.status==assignment_api::CollectStatus::Stale)return;
        if(actual.status!=assignment_api::CollectStatus::Ready)
        {
            owner.query=query;owner.build=AssignmentBuild::Unsupported;
            frontier.seed_failure=actual.reason==assignment_api::SelfReason::Roster ||
                actual.reason==assignment_api::SelfReason::MemoryCapacity?
                ap::FrontierSeedFailure::Permanent:ap::FrontierSeedFailure::Transient;
            return;
        }
        seed_assignment_frontier(state,actual,query);return;
    }
    if(!owner.menu)return;
    poll_assignment_frontier(state);
    if(!frontier.count)return;
    bool valued=false;
    for(std::size_t i=0;i<frontier.count && !valued;++i)
    {
        if(frontier.indexed[i])valued=frontier.indexed[i]->valuation_ready();
        else if(frontier.handles[i])
        {
            const auto& handle=*frontier.handles[i];const auto pin=handle.runtime.lock();
            if(pin)valued=pin->dense_valuation_ready(handle);
        }
    }
    if(!valued)return;
    // This is a fresh owner prefix, before any optional unit loop. A failed
    // collector cannot starve legacy publication: it already happened above.
    const auto actual=assignment_api::collect_self(anchor,epoch,owner.body);probe_collect(state,epoch,actual,1);
    if(actual.status!=assignment_api::CollectStatus::Ready || !current(epoch))return;
    if(!owner.self || assignment_hash(actual)!=owner.body || owner.field!=state.potential_generation ||
       !assignment_cooldowns(owner.self->snapshot,actual.snapshot))
    {++frontier.stale_body;assignment_withdraw(state);reset_assignment_model(state);seed_assignment_frontier(state,actual,query);return;}
    owner.validated=actual;owner.validated_epoch=epoch;
    // Complete LOCAL fallback: actual current publication date, no four-input
    // communication claim. The declared first handoff is this publication.
    const auto start=static_cast<std::uint32_t>(epoch),horizon=static_cast<std::uint32_t>(cambc::constants::max_rounds);
    if(start>=horizon)return;
    for(std::size_t i=0;i<frontier.count && budget(20000) && current(epoch);++i)
    {
        if(!frontier.handles[i])continue;
        const auto& handle=*frontier.handles[i];
        const auto pin=handle.runtime.lock();if(!pin)continue;
        auto support=handle.programme_key;
        support.actor=actual.snapshot.actor;support.body_revision=owner.body;
        support.geometry_revision=owner.field;support.word_revision=owner.body;
        const auto& indexed=frontier.indexed[i];
        const auto exact=indexed?indexed->lookup_exact(start,horizon,support):pin->lookup_dense_qualified(handle,start,horizon,support);
        if(!exact || !exact->banked_gems_by_H || frontier_refuted(state,*exact))continue;
        const auto life=assignment_lifecycle_ticket(state,exact->group,epoch);
        if(life.status!=Status::Ready || !current(epoch))return;
        if(life.availability==Availability::Respawning)continue; // Timer zero is not Alive; retain alternatives.
        // Unknown authorizes only paid ingress to the existing fresh Work
        // observer. Neither the original scenario nor a peer supplies Life.
        const auto lease=indexed?std::optional<ao::ProgrammeLease>{}:pin->programme(handle,start,horizon,support);
        if((!indexed && !lease) || frontier_refuted(state,*exact))continue;
        if(!owner.programme.stage(start,horizon) ||
           owner.programme.admit(start,true,true)!=ap::ProgrammeAdmission::Started)return;
        owner.choice=*exact;owner.choice_local=owner.canonical.local_by_canonical[exact->group];
        owner.pending_choice.reset();owner.pending_paired=false;
        frontier.indexed_active=indexed;frontier.active=lease;
        ++owner.nominations;++frontier.accepted;
        state.group=owner.choice_local;state.phase=life.availability==Availability::Reward?Phase::Capture:Phase::Work;
        if(state.phase==Phase::Capture)state.capture_bag=actual.snapshot.carried_gems;
        state.close_front=false;state.basic_next=false;state.escape_required=false;state.partial_progress=false;
        state.acquisition.reset();invalidate(state); // Never erases the actual reverse Trail.
        if(budget(10000))std::printf("assignment frontier r%d start%u role%u g%d canonical%u candidate%zu perch%d,%d family%d bank%llu programme=noshop paired=0 coverage=partial life=%u valuation=%s\n",
            epoch,start,actual.role,state.group,exact->group,exact->candidate_id,exact->perch.x,exact->perch.y,
            static_cast<int>(exact->family),static_cast<unsigned long long>(exact->banked_gems_by_H),
            static_cast<unsigned>(life.availability),indexed&&indexed->status()==production_calendar::Status::Ready?"index":"dense");
        return;
    }
}
[[maybe_unused]] void arm_assignment_handoff(Controller& state,
    const assignment_api::SelfCheckpoint& anchor,std::uint64_t query,int epoch)
{
    if constexpr(cfg::assignment_service && cfg::assignment_frontier && cfg::assignment_handoff)
    {
        auto& owner=assignment_data(state);auto& handoff=handoff_data(state);
        // This first adapter extends ordinary Work publication only. Retained
        // Capture/Return owners continue to run through their existing guards.
        if(owner.choice || !owner.self || state.phase!=Phase::Work || state.group<0 ||
           owner.build==AssignmentBuild::Unsupported || (!owner.seed && !owner.menu))return;
        const auto& self=owner.self->snapshot;
        if(anchor.pos!=vec2{self.body.pos.x,self.body.pos.y} || anchor.hp!=self.body.hp ||
           anchor.max_hp!=self.body.max_hp || anchor.raw_strength!=self.raw_strength ||
           anchor.focus!=self.raw_focus || anchor.gold!=self.gold || anchor.gems!=self.carried_gems)return;
        handoff.anchor=anchor;
        handoff.publication_query=query;
        // Model identity may predate this newly funded ordinary publication.
        // Keep those query IDs distinct instead of inventing a model refresh.
        handoff.receipt.arm({epoch,owner.query,owner.body,owner.field,
            {anchor.pos.x,anchor.pos.y},static_cast<int>(state.phase),state.group});
    }
}
[[maybe_unused]] bool assignment_handoff_input(Controller& state)
{
    namespace fh=production_frontier_handoff;
    if constexpr(!(cfg::assignment_service && cfg::assignment_frontier && cfg::assignment_handoff))return false;
    else
    {
        auto& owner=assignment_data(state);auto& handoff=handoff_data(state);
        if(!handoff.receipt.pending() || !handoff.anchor)return false;
        const int epoch=static_cast<int>(cambc::round_number());
        const auto& actor=cambc::self();const auto origin=actor.position();
        fh::Frame frame;frame.epoch=epoch;frame.query=owner.query;frame.body=owner.body;
        frame.field=state.potential_generation;frame.origin={origin.x,origin.y};
        frame.phase=static_cast<int>(state.phase);frame.group=state.group;
        frame.route_ready=state.route.status==Status::Ready && state.route.graph_revision==state.graph_revision;
        frame.quiet=!state.escape_required && immutable_camp_quiet(origin);
        frame.before_command=state.last_epoch==epoch && !state.walked_this_turn;
        frame.fresh_input=current(epoch);
        if(frame.route_ready && frame.quiet && frame.before_command)
        {
            const auto flags=actor.state();
            frame.before_command=frame.before_command && flags.contains(cambc::champion_state::alive) &&
                flags.contains(cambc::champion_state::can_move) && flags.contains(cambc::champion_state::can_attack) &&
                flags.contains(cambc::champion_state::can_cast) &&
                !flags.contains(cambc::champion_state::recalling) &&
                !flags.contains(cambc::champion_state::submitting_gems) &&
                !flags.contains(cambc::champion_state::teleporting);
            frame.no_enemy=cambc::vision::champions(cambc::team::enemy).empty();
        }
        const auto& frontier=frontier_data(state);
        for(std::size_t i=0;i<frontier.count;++i)
        {
            if(frontier.handles[i])frame.readiness=fh::Readiness::ProgrammeOnly;
            bool valued=frontier.indexed[i] && frontier.indexed[i]->valuation_ready();
            if(!valued && frontier.handles[i])
            {
                const auto pin=frontier.handles[i]->runtime.lock();
                valued=pin && pin->dense_valuation_ready(*frontier.handles[i]);
            }
            if(valued) {frame.readiness=fh::Readiness::Valued;break;}
        }
        if(!current(epoch))return true;
        const auto opportunity=handoff.receipt.opportunity(frame);
        if(opportunity!=fh::Opportunity::Validate)return false;
        // One expensive attempt per funded publication. A failed collector
        // leaves the physical incumbent available and cannot own later inputs.
        const auto anchor=*handoff.anchor;const auto query=owner.query;
        handoff.receipt.consume();handoff.anchor.reset();
        // The shared publication path performs the actual all-slot self check
        // and collector. Do not spend that mandatory prefix twice here.
        assignment_frontier_published(state,anchor,query,epoch);
        return owner.choice.has_value() || !current(epoch);
    }
}
[[maybe_unused]] void advance_assignment_frontier_tail(Controller& state)
{
    auto& owner=assignment_data(state);
    if(!owner.self || !state.potential || owner.field!=state.potential_generation ||
       owner.build==AssignmentBuild::Self || state.escape_required ||
       state.phase==Phase::Exit || state.phase==Phase::Recover || state.phase==Phase::Bank)return;
    const int epoch=static_cast<int>(cambc::round_number());
    if(state.last_epoch!=epoch)return; // Never continue optional work in a suspended later input.
    // Every physical owner has returned. Retained pure work uses the existing
    // unit admission, not a reduced fuel constant or a promised cheap unit.
    while(budget(50000) && current(epoch))
    {
        poll_assignment_frontier(state);
        if(owner.build==AssignmentBuild::Ready || owner.build==AssignmentBuild::Unsupported)break;
        if(!assignment_build_unit(state,epoch))break;
    }
    poll_assignment_frontier(state);
}
struct AssignmentFrontierTail
{
    Controller& state;
    ~AssignmentFrontierTail()
    {
        if constexpr(cfg::assignment_service && cfg::assignment_frontier)
            try {
                if(!std::uncaught_exceptions()) {
                    if constexpr(cfg::assignment_static)advance_static_tail(state);
                    else advance_assignment_frontier_tail(state);
                }
            }
            catch(const std::exception&) {std::printf("error assignment frontier tail\n");}
    }
};
#include "assignment_static_controller.h"
[[maybe_unused]] bool refine_admission_publication(Controller& state,int epoch)
{
    if constexpr(cfg::admission_service)
    {
        auto& query=*state.acquisition;auto& retained=admission_data(state);
        reselect_admission(state);
        if(retained.reprice_group>=0)
        {
            query.group=retained.reprice_group;retained.reprice_group=-1;
            if(retained.reprice_capture)
            {
                retained.reprice_capture=false;
                retain_admission_offer(state,priced_hypothesis(query.packet,query.group,0,0),production_admission::Family::Capture);
                query.group=static_cast<int>(World::get().groups.size());reselect_admission(state);return false;
            }
            retained.repricing=true;
            prepare_acquisition_price(state);return false;
        }
        if(query.best.group<0)return true;
        const int group=query.best.group;
        const auto fresh=admission_lifecycle(state,group,epoch,query.packet.pos);
        if(fresh.status!=Status::Ready)return false;
        query.availability_revision=state.availability_revision;
        auto& row=retained.offers[static_cast<std::size_t>(group)];
        if(fresh.availability==Availability::Respawning)
        {
            // This actual observation resolves only the selected Work/Capture/
            // Observe nomination in this menu. Other prepared offers survive.
            row.present=false;reselect_admission(state);return false;
        }
        if(fresh.availability==Availability::Alive && row.family!=production_admission::Family::Work)
        {
            row.present=false;query.group=group;retained.repricing=true;
            prepare_acquisition_price(state);return false;
        }
        if(fresh.availability==Availability::Reward && row.family!=production_admission::Family::Capture)
        {
            const auto offer=priced_hypothesis(query.packet,group,0,0);
            retain_admission_offer(state,offer,production_admission::Family::Capture);
            reselect_admission(state);return false;
        }
        if(fresh.availability==Availability::Unknown && row.family!=production_admission::Family::Capture)
        {
            row.family=production_admission::Family::Observe;
            if(retained.observation_exhausted[static_cast<std::size_t>(group)])
            {row.present=false;reselect_admission(state);return false;}
        }
    }
    return true;
}
void publish_acquisition(Controller& state,int current_epoch)
{
    if constexpr(cfg::admission_service)
        if(!refine_admission_publication(state,current_epoch))return;
    const auto& query=*state.acquisition;
    const auto& observation=query.packet;
    const auto best=query.best;
    int walk=100000;
    for (auto altar:World::get().altars) walk=std::min(walk,std::max(0,distance(observation.pos,altar)-cambc::constants::submit_gems_range));
    const float channel=std::max(static_cast<float>(cambc::constants::submit_gem_minimum_duration),
        static_cast<float>(std::ceil(observation.gems*cambc::constants::submit_gem_duration_multiplier)+cambc::constants::submit_gem_base_duration));
    const float bank_time=walk+channel+1;
    const float bank_rate=observation.gems>0?observation.gems/bank_time:-1.0f;
    if (observation.gems>0 && (bank_rate>=best.gem_rate ||
        current_epoch+bank_time+best.time>=cambc::constants::max_rounds))
    { state.phase=Phase::Bank; state.group=-1; }
    else if (best.group>=0)
    {
        state.group=best.group;
        state.phase=state.memory[static_cast<std::size_t>(best.group)].availability==Availability::Reward?Phase::Capture:Phase::Work;
        if constexpr(cfg::admission_service)
        {
            const auto family=admission_data(state).offers[static_cast<std::size_t>(best.group)].family;
            state.phase=family==production_admission::Family::Work?Phase::Work:Phase::Capture;
            if(state.phase==Phase::Capture)state.capture_bag=observation.gems;
        }
        if constexpr(cfg::capture_service)
            if(state.phase==Phase::Capture)state.capture_bag=observation.gems;
    }
    else
    {
        // Preserve the completed negative query. Fresh physical facts or a
        // scheduled eligibility boundary may resolve it; a new round alone
        // must not repeat all estimates or become an instruction to recover.
        state.acquisition->stage=AcquisitionStage::Waiting;
        nominate_recon(state);
        return;
    }
    state.close_front=false; state.basic_next=false; state.escape_required=false;state.partial_progress=false;
    if constexpr(cfg::recon_service)
    {state.recon_request.reset();state.recon_negative=false;state.recon_menu_pending=false;cancel_named_recon();}
    if constexpr(cfg::admission_service)
    {
        auto& retained=admission_data(state);
        retained.selected=state.group;
        if(state.group>=0)
        {
            retained.selected_family=retained.offers[static_cast<std::size_t>(state.group)].family;
            retained.withheld.emplace(std::move(*state.acquisition));
        }
        else retained.withheld.reset();
    }
    state.acquisition.reset();
    invalidate(state);
}
bool same_query_body(const Observation& a,const Observation& b)
{
    return production_comparison::same_raw_body(a,b.pos,b.hp,b.max_hp,b.raw_strength,
        b.attack_range,b.gems,b.gold) && a.strength==b.strength &&
        a.splash==b.splash && a.damage_multiplier==b.damage_multiplier;
}
bool start_acquisition(Controller& state,Observation&& packet)
{
    if (state.acquisition_generation==std::numeric_limits<std::uint64_t>::max()) return false;
    ++state.acquisition_generation;
    state.acquisition.emplace(std::move(packet));
    auto& query=*state.acquisition;
    query.generation=state.acquisition_generation;
    query.reach=cfg::prefix_service?state.service_reach:query.packet.attack_range;
    query.availability_revision=state.availability_revision;
    query.graph=state.graph_revision; query.field=state.potential_generation;
    if constexpr(cfg::admission_service)
    {auto& retained=admission_data(state);retained.repricing=false;retained.reprice_capture=false;retained.reprice_group=-1;retained.wait_cursor=0;retained.wait_refresh=false;retained.withheld.reset();}
    return true;
}
void retain_acquisition_packet(Controller& state,Observation&& packet)
{
    if (!state.acquisition) { start_acquisition(state,std::move(packet)); return; }
    auto& query=*state.acquisition;
    if (query.stage!=AcquisitionStage::Waiting) return;
    if (!same_query_body(query.packet,packet) || query.graph!=state.graph_revision ||
        query.field!=state.potential_generation || query.availability_revision!=state.availability_revision ||
        packet.epoch>=query.next_eligibility)
    { state.acquisition.reset(); start_acquisition(state,std::move(packet)); return; }
    query.refresh.emplace(std::move(packet));
    query.comparison.emplace(query.packet.visible_bits.size(),query.refresh->visible_bits.size(),
        production_comparison::Key{query.generation,static_cast<std::uint64_t>(2*World::get().groups.size())});
    query.stage=AcquisitionStage::RefreshVisible;
}
std::vector<vec2> return_witness(const Controller&,vec2);
bool potential_exit(Controller&,const std::vector<vec2>&);
[[maybe_unused]] void resume_admission(Controller& state,bool revalue_work,bool revalue_capture=false)
{
    if constexpr(cfg::admission_service)
    {
        const int rejected=state.group;auto& retained=admission_data(state);
        state.phase=Phase::Acquire;state.group=-1;invalidate(state);
        if(rejected>=0 && retained.withheld && retained.selected==rejected)
        {
            state.acquisition.emplace(std::move(*retained.withheld));retained.withheld.reset();
            auto& query=*state.acquisition;
            auto& row=retained.offers[static_cast<std::size_t>(rejected)];
            if(row.generation==query.generation)row.present=false;
            query.stage=AcquisitionStage::Publish;query.price.reset();query.comparison.reset();
            query.availability_revision=state.availability_revision;
            retained.reprice_group=revalue_work || revalue_capture?rejected:-1;
            retained.reprice_capture=revalue_capture;retained.repricing=false;
            reselect_admission(state);
        }
        const auto actual=cambc::self().position();
        if(!contact_immutable_quiet(actual))
        {
            state.escape_required=true;
            const auto path=return_witness(state,actual);
            if(!path.empty())potential_exit(state,path);
        }
    }
}
void queue_request(Controller& state,Observation& observation)
{
    state.request_assembly.emplace();
    auto& assembly=*state.request_assembly;
    assembly.kind=state.phase==Phase::Bank ? RouteKind::Bank :
        state.phase==Phase::Capture ? RouteKind::Capture : RouteKind::Work;
    if constexpr(cfg::admission_service)
        if(state.phase==Phase::Capture && admission_data(state).selected_family==production_admission::Family::Observe)
            assembly.kind=RouteKind::Work;
    assembly.group=state.group;
    assembly.reach=state.phase==Phase::Bank ?
        (state.close_front ? observation.attack_range : cambc::constants::submit_gems_range) :
        (state.close_front ? 1 : cfg::prefix_service?state.service_reach:observation.attack_range);
    if constexpr(cfg::admission_service)
        if(state.phase==Phase::Capture && assembly.kind==RouteKind::Work)assembly.reach=1;
    if constexpr(cfg::assignment_service)
        if(assignment_perch(state))
        {
            const auto& point=assignment_data(state).choice->perch;
            force_assignment_perch(assembly,vec2{point.x,point.y});
        }
    if constexpr(cfg::admission_service)admission_data(state).request_refutation_cursor=0;
    assembly.epoch=observation.epoch;
    assembly.origin=observation.pos;
    assembly.visible_bits=std::move(observation.visible_bits);
    assembly.living_bits=std::move(observation.living_bits);
}
[[maybe_unused]] Status filter_observation_frontiers(Controller& state,RequestAssembly& assembly)
{
    if constexpr(cfg::admission_service)
    {
        auto& retained=admission_data(state);
        if(state.phase!=Phase::Capture || retained.selected_family!=production_admission::Family::Observe ||
            assembly.kind!=RouteKind::Work || !assembly.request.goal_mask)return Status::Ready;
        const auto group=static_cast<std::size_t>(assembly.group);
        if(group>=retained.observation_refuted.size())return Status::Ready;
        const auto& failed=retained.observation_refuted[group];
        unsigned units=0;
        while(retained.request_refutation_cursor<failed.size() && units++<8 && budget(12000))
            assembly.request.goal_mask->set(failed[retained.request_refutation_cursor++],false);
        if(retained.request_refutation_cursor<failed.size())return Status::NeedCompute;
        if(assembly.request.goal_mask->is_empty())
        {
            // Exhausted only this represented adjacent-observation frontier
            // family. Work/Capture remain eligible on actual positive readback.
            retained.observation_exhausted[group]=true;resume_admission(state,false);return Status::Unreachable;
        }
    }
    return Status::Ready;
}
[[maybe_unused]] bool refuse_unknown_frontier(Controller& state,const CaptureAuthority& fresh,int epoch,vec2 origin)
{
    if constexpr(cfg::admission_service)
    {
        auto& retained=admission_data(state);
        if(retained.selected_family!=production_admission::Family::Observe || state.route.kind!=RouteKind::Work ||
            state.route.status!=Status::Ready || fresh.status!=Status::NeedObservation || fresh.availability!=Availability::Unknown ||
            fresh.group!=state.group || fresh.epoch!=epoch || fresh.pos!=origin || !current(epoch) || cambc::self().position()!=origin)return false;
        const auto index=static_cast<std::size_t>(origin.y)*World::get().width+origin.x;
        if(index>=state.route.distances.size() || state.route.distances[index]!=0)return false;
        auto& failed=retained.observation_refuted[static_cast<std::size_t>(state.group)];
        if(std::find(failed.begin(),failed.end(),origin)==failed.end())failed.push_back(origin);
        resume_admission(state,false);
        return true;
    }
    return false;
}
// Called only after an actual successful ordinary move. Dash/displacement
// invalidates spatial support but neither erases nor invents this owner-turn fact.
production_walk::Status record_applied_ordinary_step(Controller& state,production_walk::Point from,
    production_walk::Point to,bool from_quiet,bool to_quiet)
{
    if constexpr(cfg::assignment_reachability_audit)
        if(static_data(state).active)assignment_reachability_probe::command_receipt(
            assignment_reachability_probe::role(),state.last_epoch,4,1);
    if constexpr(cfg::dash_service || cfg::contact_service)state.walked_this_turn=true;
    return state.walk?state.walk->record(from,to,from_quiet,to_quiet):production_walk::Status::Unknown;
}

void move_and_remember(Controller& state,Observation& observation,vec2 destination)
{
    const auto origin=observation.pos;
    const bool moved=move_absolute(observation,destination);
    mark(state,"move",observation.epoch,observation.status,moved);
    if (moved)
    {
        state.expected=observation.pos;
        if (state.walk) record_applied_ordinary_step(state,{origin.x,origin.y},{observation.pos.x,observation.pos.y},
            immutable_camp_quiet(origin),immutable_camp_quiet(observation.pos));
    }
    else
    {
        if (state.walk) state.walk->invalidate();
        invalidate(state);
    }
}
PrefixCoverage covered(Controller& state,const Observation& observation,const WorkObservation* work,
                       const mb::service::Primitive* action,vec2 prospective={-1,-1})
{
    const bool proposed=prospective.x>=0 || prospective.y>=0;
    const auto point=proposed ? prospective : observation.pos;
    // This sufficient family needs no live camp-source reconstruction: the
    // completed field includes EVERY harmful design, even asleep/dead/external
    // members, and all actuator-supported service primitives are stationary.
    // Thus any collateral wake still contributes zero at this same pose. The
    // actuator separately refreshes target, allowance and command epoch.
    if (!proposed && action && work && work->status==Status::Ready &&
        work->epoch==observation.epoch && observation.enemies.empty() &&
        std::isfinite(observation.hp) && observation.hp>0 && state.potential)
    {
        const auto charge=state.potential->at({point.x,point.y});
        const bool stationary=action->kind==mb::service::PrimitiveKind::basic ||
            (action->kind==mb::service::PrimitiveKind::cast && action->slot>=0 &&
             static_cast<std::size_t>(action->slot)<work->state.spells.size() &&
             work->state.spells[static_cast<std::size_t>(action->slot)].kind!=mb::service::SpellKind::none);
        if (stationary && charge.status==mb::production::potential::Status::Ready && charge.upper==0 &&
            budget(0) && cambc::self().position()==point && current(observation.epoch))
        {
            PrefixCoverage result; result.status=Status::Ready; result.safe=true;
            result.trace_stage="work-potential-zero";
            return result;
        }
        if constexpr(cfg::prefix_service && cfg::direct_service)
        {
            if (stationary && charge.status==mb::production::potential::Status::Ready && state.walk)
            {
                const auto history=state.walk->make_return({point.x,point.y});
                if (!history.empty())
                {
                    std::vector<vec2> path;path.reserve(history.size());
                    for(const auto node:history)path.push_back({node.x,node.y});
                    // All potentially harmful static camp sources already cover
                    // collateral wake; a stationary supported primitive cannot
                    // create a source outside that envelope. Recheck only actual
                    // HP/pose/epoch and this retained return instead of rebuilding
                    // the full world before every damaging command.
                    const auto authority=potential_authority(*state.potential,path);
                    if(authority.status==Status::Ready || authority.status==Status::NeedCompute ||
                       authority.status==Status::Stale)
                    {
                        PrefixCoverage result;result.status=authority.status;
                        result.safe=authority.status==Status::Ready && authority.safe_service;
                        result.current_damage=static_cast<float>(charge.upper);
                        result.trace_stage="work-potential-return";result.exit_path=std::move(path);
                        return result;
                    }
                }
            }
        }
    }
    if (immutable_camp_quiet(point)) return cover_prefix(observation,work,action,prospective);
    if (state.walk)
    {
        std::optional<mb::service::Point> next;
        if (proposed) next=mb::service::Point{prospective.x,prospective.y};
        const auto historic=state.walk->make_return({observation.pos.x,observation.pos.y},next);
        std::vector<vec2> path;
        path.reserve(historic.size());
        for (const auto node:historic) path.push_back({node.x,node.y});
        if (!path.empty())
        {
            auto witnessed=cover_witness(observation,work,action,path,prospective);
            if (witnessed.witness_blocked) state.walk->invalidate();
            if (witnessed.status==Status::Ready) return witnessed;
            // A failed sufficient witness is not a rejection of the job. The
            // existing broader guard remains an independently checked fallback.
            const auto fallback=cover_prefix(observation,work,action,prospective);
            return fallback.status==Status::Ready ? fallback : witnessed;
        }
    }
    return cover_prefix(observation,work,action,prospective);
}
void begin_exit(Controller& state,Observation& observation,const PrefixCoverage& coverage)
{
    if constexpr(cfg::assignment_service)retire_assignment(state,false);
    state.phase=Phase::Exit;
    state.needs_healing=observation.hp<observation.max_hp;
    invalidate(state);
    // A phase transition does not consume the egress opportunity. Apply the first
    // freshly legal exit now whenever the actual movement allowance is still available.
    if (observation.can_move && coverage.status==Status::Ready && coverage.exit_path.size()>1)
        move_and_remember(state,observation,coverage.exit_path[1]);
}
template<bool Enabled> struct WorkAuditScope {
    WorkAuditScope(Controller&,int,const Applied&) {}
};
template<> struct WorkAuditScope<true> {
    bool selected{};int epoch{};Applied before;const Applied& after;
    WorkAuditScope(Controller& state,int date,const Applied& applied)
        :selected(static_data(state).active!=nullptr),epoch(date),before(applied),after(applied){}
    ~WorkAuditScope() {
        const auto bits=(after.attacked&&!before.attacked?2u:0u)|(after.cast&&!before.cast?4u:0u);
        if(selected&&bits)assignment_reachability_probe::command_receipt(assignment_reachability_probe::role(),epoch,1,bits);
    }
};
void execute_work(Controller& state,Observation& observation,WorkObservation work,Applied& applied)
{
    const int epoch=observation.epoch;
    WorkAuditScope<cfg::assignment_reachability_audit> audit{state,epoch,applied};
    state.service_deferred=false;
    state.service_needs_route=false;
    if constexpr(cfg::assignment_service)
        if(assignment_perch(state) && !assignment_at_perch(state,observation.pos))
        {state.service_needs_route=true;return;}
    if constexpr(cfg::capital_service)
        if(!assignment_programme(state) && state.potential && try_capital_offer(observation,work,*state.potential))return;
    for (int count=0;count<3 && budget(30000);++count)
    {
        const auto subset=observed_subset(work);
        // Empty action allowances are a yield, including after a packed
        // basic/cast fragment. Only a real failed safe prefix rejects work.
        if (!subset.body.can_attack && !subset.body.can_cast) return;
        const auto basic_choice=[&] {
            auto basic=subset;
            basic.body.can_cast=false;
            return mb::service::choose_first_command(basic,0);
        };
        const bool ordered_basic=state.basic_next;
        auto choice=ordered_basic ? basic_choice() : mb::service::choose_first_command(subset,0);
        if (ordered_basic && choice.status!=mb::service::ExecutionStatus::Ready)
        {
            state.basic_next=false; // explicit withdrawal: no current basic candidate
            choice=mb::service::choose_first_command(subset,0);
        }
        mark(state,"work-choice",epoch,Status::Ready,static_cast<int>(choice.status));
        const auto disposition=mb::service::execution_disposition(choice.status);
        if (disposition==mb::service::ExecutionDisposition::Reposition)
        {
            if constexpr(cfg::prefix_service)
                if (mb::service::future_damage_reaches(work.state)) return;
            state.service_needs_route=true; return;
        }
        if (disposition==mb::service::ExecutionDisposition::Observe)
        { state.service_deferred=true; return; }
        if (disposition==mb::service::ExecutionDisposition::Yield) return;
        if (disposition==mb::service::ExecutionDisposition::WaitReward)
        {
            for (std::size_t i=0;i<work.state.member_count;++i)
                state.service_needs_route|=work.state.members[i].hp>0;
            return;
        }
        if (disposition==mb::service::ExecutionDisposition::Recover)
        {
            const auto coverage=covered(state,observation,&work,nullptr);
            if (coverage.status!=Status::Ready) { state.service_deferred=true; return; }
            begin_exit(state,observation,coverage);
            return;
        }
        auto coverage=covered(state,observation,&work,&choice.primitive);
        mark(state,coverage.trace_stage ? coverage.trace_stage : "work-cover",epoch,coverage.status,
            coverage.trace_stage ? coverage.trace_detail : coverage.safe);
        if (coverage.status!=Status::Ready) { state.service_deferred=true; return; }
        if (!coverage.safe)
        {
            // Coverage belongs to this exact primitive and wake closure.
            // A rejected AoE's reserve cannot reject an untested basic.
            if (choice.primitive.kind!=mb::service::PrimitiveKind::basic)
            {
                const auto alternative=basic_choice();
                if (alternative.status==mb::service::ExecutionStatus::Ready)
                {
                    state.basic_next=true; // ordering only, never a cached certificate
                    const auto alternative_coverage=covered(state,observation,&work,&alternative.primitive);
                    if (alternative_coverage.status!=Status::Ready) { state.service_deferred=true; return; }
                    if (alternative_coverage.safe)
                    { choice=alternative; coverage=alternative_coverage; }
                }
                else
                {
                    const auto alternative_disposition=mb::service::execution_disposition(alternative.status);
                    if (alternative_disposition!=mb::service::ExecutionDisposition::Recover)
                    {
                        // An unsafe optional cast does not turn a temporarily
                        // spent basic into unsafe residual work. Await a fresh
                        // input rather than abandoning the production task.
                        state.basic_next=true;
                        state.service_needs_route=alternative_disposition==mb::service::ExecutionDisposition::Reposition;
                        return;
                    }
                }
            }
            if (!coverage.safe)
            {
                const auto exit=covered(state,observation,&work,nullptr);
                if (exit.status!=Status::Ready) { state.service_deferred=true; return; }
                begin_exit(state,observation,exit);
                return;
            }
        }
        const auto commanded=apply_command(observation,work,choice.primitive,applied);
        mark(state,"work-apply",epoch,commanded,static_cast<int>(choice.primitive.kind));
        if (commanded!=Status::Ready) { state.service_deferred=true; return; }
        state.basic_next=false;
        if constexpr(cfg::direct_service)
        {
            const auto kind=choice.primitive.kind;
            const auto spell=kind==mb::service::PrimitiveKind::cast?
                work.state.spells[static_cast<std::size_t>(choice.primitive.slot)].kind:mb::service::SpellKind::none;
            if(kind==mb::service::PrimitiveKind::basic || spell==mb::service::SpellKind::damage ||
               spell==mb::service::SpellKind::aoe || spell==mb::service::SpellKind::cc)
                state.partial_progress=true;
        }
        if constexpr(cfg::direct_service && cfg::local_service)
        {
            const bool invalidated=applied.moved || work.state.body.moved_this_turn;
            work=invalidated?observe_work(observation,state.group,applied.moved):
                observe_after_stationary(observation,work,choice.primitive,false);
        }
        else work=observe_work(observation,state.group,applied.moved);
        if (work.status!=Status::Ready) { state.service_deferred=true; return; }
        rebase_self(observation,work);
        remember(state,work);
        if (work.availability==Availability::Reward)
        { enter_capture(state,observation.gems); return; }
    }
    // Exhausting this input's allowed service work is a physical wait. The
    // operating caller must use its reserved exit or leave a one-input ticket.
    state.service_deferred=true;
}
std::vector<vec2> return_witness(const Controller& state,vec2 origin)
{
    if (!state.walk) return {};
    const auto nodes=state.walk->make_return({origin.x,origin.y});
    std::vector<vec2> result;
    result.reserve(nodes.size());
    for (const auto point:nodes) result.push_back({point.x,point.y});
    return result;
}
// Explicit bounded-risk resolving operator when an entire historical-return
// family is blocked/unsupported. It reserves two raw static volleys at the next
// landing, but does not claim a complete future escape proof. Each later input
// must resolve again. This prevents an unsupported certificate becoming idle.
bool resolve_exit_step(Controller& state,const std::vector<vec2>& historical,bool capture_only=false)
{
    if (!state.potential || !budget(20000)) return false;
    const int epoch=static_cast<int>(cambc::round_number());
    auto& actor=cambc::self();const auto origin=actor.position();
    const bool enemy=!cambc::vision::champions(cambc::team::enemy).empty();
    if (!current(epoch)) return true;
    if (enemy) return false; // existing complete observer/threat operator owns it
    if (immutable_camp_quiet(origin))
    {state.escape_required=false;invalidate(state);return false;}
    const auto anchor=historical.empty()?World::get().spawn_tiles.front():historical.back();
    struct Step {vec2 pos;double charge;int length;bool back;};
    std::array<Step,8> steps{};std::size_t count=0;
    for(const auto direction:directions)
    {
        const vec2 point{origin.x+direction.x,origin.y+direction.y};
        const auto charge=state.potential->at({point.x,point.y});
        if(charge.status==mb::production::potential::Status::Ready)
            steps[count++]={point,charge.upper,distance(point,anchor),point==state.resolution_previous};
    }
    std::sort(steps.begin(),steps.begin()+static_cast<std::ptrdiff_t>(count),[](const auto& a,const auto& b){
        if(a.charge!=b.charge)return a.charge<b.charge;
        if(a.length!=b.length)return a.length<b.length;
        return a.back!=b.back && !a.back;
    });
    for(std::size_t i=0;i<count && budget(12000);++i)
    {
        const auto& step=steps[i];
        const auto authority=transit_authority(2.0*step.charge,origin,step.pos);
        mark(state,"resolve-exit",epoch,authority.status,authority.moved);
        if(authority.status==Status::Ready && authority.moved)
        {
            state.resolution_previous=origin;state.expected=authority.pos;
            const bool quiet=immutable_camp_quiet(authority.pos);
            if(state.walk)record_applied_ordinary_step(state,{origin.x,origin.y},{authority.pos.x,authority.pos.y},false,quiet);
            state.escape_required=!quiet;
            if(!capture_only && state.phase!=Phase::Bank)
            {
                if constexpr(cfg::assignment_service)retire_assignment(state,false);
                state.phase=Phase::Exit;
            }
            state.needs_healing=authority.hp<state.last_max_hp;
            invalidate(state);return true;
        }
        if(authority.status==Status::NeedCompute || authority.status==Status::Stale)return true;
        if(authority.status==Status::Ready && !authority.can_move)return true;
        // Capture's fallback does not retry another NPC-only command after a
        // refusal that may reveal a champion. Its next owner input reobserves.
        if(capture_only && authority.status==Status::NeedObservation)return true;
    }
    return false;
}
bool potential_exit(Controller& state,const std::vector<vec2>& path)
{
    if (!state.potential || path.empty()) return false;
    if constexpr(cfg::prefix_service)
    {
        if (!budget(5000)) return true;
        const int epoch=static_cast<int>(cambc::round_number());
        const bool enemy=!cambc::vision::champions(cambc::team::enemy).empty();
        if (!current(epoch)) return true;
        if (enemy)
        {
            // Relinquish all optional service owners so the current global
            // observation and threat actuator are reached in THIS input.
            if constexpr(cfg::assignment_service)retire_assignment(state,false);
            state.escape_required=false;state.phase=Phase::Recover;state.group=-1;
            invalidate(state);return false;
        }
    }
    const auto authority=potential_authority(*state.potential,path,PotentialUse::BestEffortExit);
    mark(state,"potential-exit",authority.epoch,authority.status,authority.moved);
    if constexpr(cfg::prefix_service)
    {
        if (authority.status==Status::NeedObservation && budget(12000))
        {
            // A dynamically occupied historical edge is not a command to idle.
            // Test bounded adjacent quiet exits with their own fresh authority.
            const auto origin=cambc::self().position();
            if (immutable_camp_quiet(origin))
            { state.escape_required=false; invalidate(state); return false; }
            for (const auto direction:directions)
            {
                const vec2 destination{origin.x+direction.x,origin.y+direction.y};
                if (!immutable_camp_quiet(destination)) continue;
                const std::array<vec2,2> detour{origin,destination};
                const auto alternative=potential_authority(*state.potential,detour,PotentialUse::BestEffortExit);
                if (alternative.status==Status::Ready && alternative.moved)
                {
                    state.expected=alternative.pos;
                    record_applied_ordinary_step(state,{origin.x,origin.y},{destination.x,destination.y},false,true);
                    if constexpr(cfg::assignment_service)retire_assignment(state,false);
                    state.escape_required=false;state.phase=Phase::Exit;
                    state.needs_healing=alternative.hp<state.last_max_hp;
                    invalidate(state);return true;
                }
                if (alternative.status==Status::NeedCompute || alternative.status==Status::Stale) return true;
            }
        }
    }
    if (authority.status==Status::NeedObservation)
    {
        // A refused/blocked leg revokes execution authority, not the fact that
        // this suffix was walked. A later fresh input must revalidate it.
        state.escape_required=true;
        invalidate(state);
    }
    if constexpr(cfg::prefix_service)
        if ((authority.status==Status::NeedObservation ||
             (authority.status==Status::Ready && !authority.safe_exit)) &&
            resolve_exit_step(state,path)) return true;
    if (authority.status!=Status::Ready) return true;
    if (!authority.safe_exit) return false; // unsupported sufficient bound, retain full fallback
    if (authority.moved)
    {
        const auto to=path[1];
        state.expected=to;
        record_applied_ordinary_step(state,{path.front().x,path.front().y},{to.x,to.y},
            immutable_camp_quiet(path.front()),immutable_camp_quiet(to));
        if (state.phase!=Phase::Bank)
        {
            if constexpr(cfg::assignment_service)retire_assignment(state,false);
            state.phase=Phase::Exit;
        }
        state.needs_healing=authority.hp<state.last_max_hp;
        invalidate(state);
    }
    if (path.size()==1)
    {
        state.escape_required=false;
        return false;
    }
    return true;
}
bool acquisition_input(Controller& state,const World& world)
{
    if (!state.acquisition || (!cfg::admission_service && state.acquisition->stage==AcquisitionStage::Waiting)) return false;
    if (!budget(5000)) return true;
    const int epoch=static_cast<int>(cambc::round_number()); // before own-state imports
    auto& actor=cambc::self();
    const auto origin=actor.position();
    const auto hp=actor.health(),max_hp=actor.max_health(),strength=actor.strength();
    const auto reach=actor.attack_range();
    const auto gems=actor.gem_count(),gold=actor.gold_count();
    const auto flags=actor.state();
    const bool enemy=!cambc::vision::champions(cambc::team::enemy).empty();
    if (actor.position()!=origin || !current(epoch)) return true;
    const auto& packet=state.acquisition->packet;
    const bool same=production_comparison::same_raw_body(packet,origin,hp,max_hp,strength,reach,gems,gold) &&
        state.acquisition->availability_revision==state.availability_revision &&
        state.acquisition->graph==state.graph_revision && state.acquisition->field==state.potential_generation;
    const bool quiet=immutable_camp_quiet(origin);
    if (!same || !quiet || enemy || !flags.contains(cambc::champion_state::alive) ||
        flags.contains(cambc::champion_state::recalling) || flags.contains(cambc::champion_state::submitting_gems) ||
        flags.contains(cambc::champion_state::teleporting))
    {
        state.acquisition.reset();
        if (!quiet)
        {
            state.escape_required=true;
            const auto path=return_witness(state,origin);
            if (!path.empty()) potential_exit(state,path);
            return true;
        }
        return false; // meaningful changed facts require one new global packet
    }
    if constexpr(cfg::admission_service)
    {
        if(state.acquisition->stage==AcquisitionStage::Waiting)
        {
            auto& retained=admission_data(state);auto& query=*state.acquisition;
            if(retained.wait_refresh)
            {
                // A completed finite readback pass hands a fresh input to the
                // ordinary mask observer. Repeated unresolved local reads must
                // not monopolise every input or hide changed global premises.
                retained.wait_refresh=false;
                return false;
            }
            // One named lifecycle read per quiet owner input. A clock is never
            // evidence of Alive; visible reappearance need not pay a whole new
            // ranking query before its retained family can be repriced.
            while(retained.wait_cursor<world.groups.size() && budget(24000))
            {
                const auto group=retained.wait_cursor++;
                const bool absent_row=group<retained.offers.size() &&
                    retained.offers[group].generation==query.generation && !retained.offers[group].present;
                const bool observation_omission=(group<retained.observation_exhausted.size() &&
                    retained.observation_exhausted[group]) ||
                    (absent_row && retained.offers[group].family==production_admission::Family::Observe);
                if(state.memory[group].availability!=Availability::Respawning && state.memory[group].availability!=Availability::Unknown &&
                    !observation_omission && !absent_row)continue;
                const auto before=state.memory[group].availability;
                const auto fresh=admission_lifecycle(state,static_cast<int>(group),epoch,origin);
                if(fresh.status!=Status::Ready)return true;
                query.availability_revision=state.availability_revision;
                if(fresh.availability==Availability::Alive &&
                    (before!=Availability::Alive || observation_omission))
                {retained.reprice_capture=false;retained.reprice_group=static_cast<int>(group);query.stage=AcquisitionStage::Publish;}
                else if(fresh.availability==Availability::Reward &&
                    (before!=Availability::Reward || observation_omission))
                {
                    retain_admission_offer(state,priced_hypothesis(query.packet,static_cast<int>(group),0,0),
                        production_admission::Family::Capture);
                    reselect_admission(state);query.stage=AcquisitionStage::Publish;
                }
                // Unknown remains a named observation obligation. It never
                // reopens the same rejected route/frontier or changes remembered
                // absence into Work. Positive readback or new semantic inputs
                // must resolve the retained rejection before nomination resumes.
                mark(state,"admission-wait-readback",epoch,fresh.status,static_cast<int>(group));
                return true;
            }
            if(retained.wait_cursor>=world.groups.size())
            {retained.wait_cursor=0;retained.wait_refresh=true;}
            return true;
        }
    }
    if (state.acquisition->stage==AcquisitionStage::Publish)
    {
        if constexpr(cfg::assignment_service)
        {
            if constexpr(cfg::assignment_frontier)
            {
                // Fund the ordinary incumbent BEFORE optional collection/model
                // work. Publication contains no game command; it may be replaced
                // only by an exact complete freshly admitted programme below.
                const auto anchor=assignment_api::checkpoint(state.acquisition->packet);
                const auto query=state.acquisition->generation;
                retire_assignment(state,false);
                publish_acquisition(state,epoch);
                const bool published=state.phase!=Phase::Acquire ||
                    (state.acquisition && state.acquisition->stage==AcquisitionStage::Waiting);
                if(published && !state.escape_required && flags.contains(cambc::champion_state::can_move) &&
                   flags.contains(cambc::champion_state::can_attack) && flags.contains(cambc::champion_state::can_cast))
                {
                    assignment_frontier_published(state,anchor,query,epoch);
                    if constexpr(cfg::assignment_handoff && !cfg::assignment_static)arm_assignment_handoff(state,anchor,query,epoch);
                }
                mark(state,"acquire-publish",epoch,Status::Ready);
                return true;
            }
            else
            {
                if(!state.escape_required && flags.contains(cambc::champion_state::can_move) &&
                   flags.contains(cambc::champion_state::can_attack) && flags.contains(cambc::champion_state::can_cast) &&
                   assignment_input(state,epoch))return true;
                retire_assignment(state,false); // Ordinary incumbent owns an unsupported/refused model case.
            }
        }
        publish_acquisition(state,epoch);
        mark(state,"acquire-publish",epoch,Status::Ready);
        return true; // publication tail owns an input; action recaptures afterward
    }
    while (budget(24000))
    {
        auto& query=*state.acquisition;
        if (query.stage==AcquisitionStage::Publish || query.stage==AcquisitionStage::Waiting) break;
        if (query.stage==AcquisitionStage::RefreshVisible || query.stage==AcquisitionStage::RefreshLiving)
        {
            if (!query.refresh || !query.comparison) { state.acquisition.reset(); return false; }
            const bool visible=query.stage==AcquisitionStage::RefreshVisible;
            const auto& lhs=visible?query.packet.visible_bits:query.packet.living_bits;
            const auto& rhs=visible?query.refresh->visible_bits:query.refresh->living_bits;
            const auto result=query.comparison->advance(lhs,rhs,
                {query.generation,static_cast<std::uint64_t>(2*world.groups.size()+(visible?0:1))},64);
            if (result==production_comparison::Status::Different)
            {
                auto changed=std::move(*query.refresh);
                state.acquisition.reset(); start_acquisition(state,std::move(changed));
                mark(state,"acquire-new-facts",epoch,Status::NeedCompute); return true;
            }
            if (result==production_comparison::Status::Unknown)
            { state.acquisition.reset(); return false; }
            if (result==production_comparison::Status::Equal)
            {
                if (visible)
                {
                    query.comparison.emplace(query.packet.living_bits.size(),query.refresh->living_bits.size(),
                        production_comparison::Key{query.generation,static_cast<std::uint64_t>(2*world.groups.size()+1)});
                    query.stage=AcquisitionStage::RefreshLiving;
                }
                else
                { query.refresh.reset(); query.comparison.reset(); query.stage=AcquisitionStage::Waiting; }
            }
        }
        else acquisition_unit(state);
        if (!current(epoch)) return true;
    }
    mark(state,"acquire-compute",epoch,Status::NeedCompute,state.acquisition?state.acquisition->group:-1);
    return true;
}
[[maybe_unused]] void reconcile_capture_life(Controller& state,const CaptureAuthority& fresh)
{
    if constexpr(cfg::admission_service || cfg::capture_service)
        if((fresh.status==Status::Ready || (cfg::capture_service && (fresh.status==Status::NeedObservation || fresh.status==Status::NeedCompute))) && fresh.availability!=Availability::Unknown &&
            fresh.group>=0 && current(fresh.epoch))
        {
            if constexpr(cfg::capture_service)
                if(fresh.availability==Availability::Alive && state.memory[static_cast<std::size_t>(fresh.group)].availability!=Availability::Alive)
                    state.memory[static_cast<std::size_t>(fresh.group)].exact.fill(false);
            GroupObservation reading;reading.status=Status::Ready;reading.epoch=fresh.epoch;
            reading.group=fresh.group;reading.availability=fresh.availability;
            // CaptureAuthority does not contain the actual timer. -1 is Unknown,
            // never a fabricated zero/deadline; the Wait resolver can read it.
            reading.respawn_timer=-1;remember_group(state,reading);
        }
    if constexpr(cfg::assignment_service&&cfg::assignment_frontier&&cfg::assignment_static) {
        auto& data=static_data(state);
        if(data.active&&!data.cash_receipt_seen&&fresh.group>=0&&fresh.group==assignment_data(state).choice_local&&
           fresh.gems>state.capture_bag&&fresh.hp>0&&current(fresh.epoch)&&cambc::self().position()==fresh.pos) {
            data.cash_receipt_seen=true;data.cash_renewal_not_before=data.cash_timing_epoch=-1;
            // This actual named timer supplies only an earliest-arrival bound.
            // Timer0 is never promoted to future Alive or capture authority.
            admission_lifecycle(state,fresh.group,fresh.epoch,fresh.pos);
        }
    }
}
[[maybe_unused]] capture_handoff::Decision capture_admission(Controller& state,const CaptureAuthority& fresh,
                                                            int epoch,vec2 origin,bool on_member)
{
    return capture_handoff::decide({state.phase==Phase::Capture && state.group>=0,
        fresh.group==state.group && fresh.epoch==epoch && fresh.pos==origin &&
            cambc::self().position()==origin && current(epoch),
        on_member,contact_immutable_quiet(origin),fresh.status==Status::Ready,
        fresh.status==Status::NeedObservation,fresh.availability==Availability::Unknown,
        fresh.availability==Availability::Alive,fresh.availability==Availability::Respawning});
}
namespace cs=capture_service;
[[maybe_unused]] cs::Point capture_point(vec2 p){return {p.x,p.y};}
using CaptureFlow=cs::Flow;
[[maybe_unused]] bool reward_preparation_current(Controller& state,int epoch,vec2 origin)
{
    if constexpr(cfg::capture_service)
    {
        const auto& owner=capture_owner(state);
        return state.phase==Phase::Capture && state.group==owner.group && owner.mode==cs::Mode::RewardPreparation &&
            owner.preparation_epoch==epoch && owner.expected==capture_point(origin) && owner.field==state.potential_generation;
    }
    return false;
}
struct CaptureBackend
{
    Controller& state;
    bool fresh(const cs::Frame& frame) const
    {return budget(0) && cambc::self().position()==vec2{frame.pos.x,frame.pos.y} && current(frame.epoch);}
    bool headroom() const {return budget(16000);}
    cs::Contact refresh_contact(const cs::Frame& frame) const
    {
        if(!budget(6000))return cs::Contact::Revalue;
        const bool enemy=!cambc::vision::champion_tiles(cambc::team::enemy).is_empty();
        if(!fresh(frame))return cs::Contact::Stale;
        return enemy?cs::Contact::Revalue:cs::Contact::Clear;
    }
    std::vector<vec2> candidates(const cs::Frame& frame) const
    {
        if(!state.walk || !state.potential || !frame.route_ready)return {};
        Observation stamp;stamp.status=Status::NeedObservation;stamp.epoch=frame.epoch;stamp.pos={frame.pos.x,frame.pos.y};
        return proposed_steps(state.route,stamp);
    }
    cs::Step attempt(const cs::Frame& frame,vec2 destination)
    {
        const vec2 origin{frame.pos.x,frame.pos.y};
        const auto index=static_cast<std::size_t>(destination.y)*World::get().width+destination.x;
        if(index>=state.route.distances.size() || state.route.distances[index]>=frame.distance)return {};
        // Include the COMPLETE prospective reverse even for a quiet landing:
        // a refused edge must not lose the still-current historical return.
        const auto tail=state.walk->make_return({frame.pos.x,frame.pos.y},
            production_walk::Point{destination.x,destination.y});
        if(tail.empty())return {};
        std::vector<vec2> word;word.reserve(tail.size()+1);word.push_back(origin);
        for(const auto p:tail)word.push_back({p.x,p.y});
        const auto guarded=potential_authority(*state.potential,word,PotentialUse::CertifiedEntry,destination,2);
        mark(state,"capture-observe-edge",frame.epoch,guarded.status,guarded.moved);
        if(guarded.status==Status::Ready && guarded.moved)
        {
            state.expected=guarded.pos;state.escape_required=false;
            const auto recorded=record_applied_ordinary_step(state,{frame.pos.x,frame.pos.y},
                {guarded.pos.x,guarded.pos.y},contact_immutable_quiet(origin),contact_immutable_quiet(guarded.pos));
            if(recorded!=production_walk::Status::Known)return {cs::StepStatus::SupportLost,capture_point(guarded.pos)};
            return {cs::StepStatus::Applied,capture_point(guarded.pos),static_cast<int>(state.route.distances[index]),
                contact_immutable_quiet(guarded.pos)};
        }
        if(guarded.status==Status::Stale)return {cs::StepStatus::Stale};
        if(guarded.status==Status::NeedCompute)return {cs::StepStatus::NeedCompute};
        if(guarded.status==Status::Ready && !guarded.can_move)return {cs::StepStatus::Spent};
        return {guarded.status==Status::NeedObservation?cs::StepStatus::Refused:cs::StepStatus::Denied};
    }
    cs::Step advance(const cs::Frame& frame) {return cs::guarded_advance(frame,*this);}
    cs::Step retreat(const cs::Frame& frame)
    {
        const auto contact=refresh_contact(frame);
        if(contact==cs::Contact::Stale)return {cs::StepStatus::Stale};
        if(contact!=cs::Contact::Clear)return {cs::StepStatus::NeedContact};
        if(!state.walk || !state.potential)return {};
        const auto path=return_witness(state,{frame.pos.x,frame.pos.y});
        if(path.size()<2)return {};
        const auto guarded=potential_authority(*state.potential,path,PotentialUse::CertifiedEntry);
        mark(state,"capture-observe-return",frame.epoch,guarded.status,guarded.moved);
        if(guarded.status==Status::Ready && guarded.moved)
        {
            state.expected=guarded.pos;state.escape_required=false;
            const auto recorded=record_applied_ordinary_step(state,{frame.pos.x,frame.pos.y},
                {guarded.pos.x,guarded.pos.y},contact_immutable_quiet(path.front()),contact_immutable_quiet(guarded.pos));
            if(recorded!=production_walk::Status::Known)return {cs::StepStatus::SupportLost,capture_point(guarded.pos)};
            return {cs::StepStatus::Applied,capture_point(guarded.pos),-1,contact_immutable_quiet(guarded.pos)};
        }
        if(guarded.status==Status::Stale)return {cs::StepStatus::Stale};
        if(guarded.status==Status::NeedCompute)return {cs::StepStatus::NeedCompute};
        if(guarded.status==Status::Ready && !guarded.can_move)return {cs::StepStatus::Spent};
        if(guarded.status==Status::NeedObservation)
        {
            const auto revealed=refresh_contact(frame);
            if(revealed==cs::Contact::Stale)return {cs::StepStatus::Stale};
            if(revealed!=cs::Contact::Clear)return {cs::StepStatus::NeedContact};
        }
        return {guarded.status==Status::NeedObservation?cs::StepStatus::Refused:cs::StepStatus::Denied};
    }
};
[[maybe_unused]] CaptureFlow capture_service_input(Controller& state);
[[maybe_unused]] CaptureFlow capture_reward_input(Controller& state,const cs::Frame& frame,CaptureBackend& backend)
{
    // Selected Capture only: re-establish actual Reward for ONE Step/Hold. The
    // immutable route/word nominates a point, never grants command authority.
    const vec2 origin{frame.pos.x,frame.pos.y};vec2 destination=origin;
    bool on_member=false;for(const auto& member:World::get().groups[frame.group].members)on_member|=member.pos==origin;
    bool word_leg=false;
    if(!on_member)
    {
        if(state.capture_word)
        {
            const auto leg=state.capture_word->leg({origin.x,origin.y},state.group,state.capture_graph,true);
            if(!leg){capture_owner(state).mode=cs::Mode::Returning;capture_owner(state).reason=cs::Reason::Refuted;return CaptureFlow::Handled;}
            destination={leg->destination.x,leg->destination.y};word_leg=true;
        }
        else if(frame.route_ready)
        {
            const auto candidates=backend.candidates(frame);
            if(candidates.empty())
            {capture_owner(state).mode=cs::Mode::Returning;capture_owner(state).reason=cs::Reason::Refuted;return CaptureFlow::Handled;}
            destination=candidates.front();
        }
        else return CaptureFlow::Dispatcher; // changed geometry must obtain a new guarded owner input
    }
    if(!backend.fresh(frame))return CaptureFlow::Handled;
    const auto fresh=capture_authority(frame.group,origin,destination);
    reconcile_capture_life(state,fresh);
    mark(state,"capture-service-reward",frame.epoch,fresh.status,fresh.moved);
    const bool sampled=fresh.epoch==frame.epoch && fresh.group==frame.group && fresh.hp>0 && current(frame.epoch);
    auto& owner=capture_owner(state);
    const bool receipt=sampled && fresh.gems>state.capture_bag;
    if(fresh.moved)
    {
        state.expected=fresh.pos;
        const auto recorded=record_applied_ordinary_step(state,{origin.x,origin.y},{fresh.pos.x,fresh.pos.y},
            contact_immutable_quiet(origin),contact_immutable_quiet(fresh.pos));
        const bool consumed=!word_leg || state.capture_word->consume_applied({origin.x,origin.y},
            {fresh.pos.x,fresh.pos.y},state.group,state.capture_graph);
        if(receipt || recorded!=production_walk::Status::Known || !consumed)
        {owner.group=frame.group;owner.mode=cs::Mode::Returning;owner.reason=receipt?cs::Reason::Bank:cs::Reason::Refuted;owner.expected=capture_point(fresh.pos);}
        return CaptureFlow::Handled; // accepted movement always ends this input
    }
    if(fresh.status==Status::Stale || !current(frame.epoch))
    {if(!frame.quiet){owner.group=frame.group;owner.mode=cs::Mode::Returning;owner.reason=cs::Reason::Refuted;}return CaptureFlow::Handled;}
    if(receipt || (sampled && (fresh.availability==Availability::Alive || fresh.availability==Availability::Respawning)))
    {
        owner.group=frame.group;owner.mode=cs::Mode::Returning;
        owner.reason=receipt?cs::Reason::Bank:fresh.availability==Availability::Alive?cs::Reason::Revalue:cs::Reason::Refuted;
        return capture_service_input(state); // body-only fresh return; no source observer
    }
    if(fresh.status==Status::Ready && fresh.safe_input)return CaptureFlow::Handled;
    if(fresh.status==Status::NeedCompute)
    {
        cs::defer(owner,frame.epoch);
        if(!frame.quiet || owner.deferrals>=2)
        {owner.group=frame.group;owner.mode=cs::Mode::Returning;owner.reason=cs::Reason::Refuted;}
        return CaptureFlow::Handled;
    }
    // A refused real Step/Hold may reveal contact. Refresh before either an
    // alternative command or actual return; preserving Trail is independent.
    const auto contact=backend.refresh_contact(frame);
    if(contact==cs::Contact::Stale)return CaptureFlow::Handled;
    if(contact!=cs::Contact::Clear)return CaptureFlow::Contact;
    if(sampled && fresh.availability==Availability::Unknown)
    {owner.mode=cs::Mode::Dormant;return CaptureFlow::Handled;} // next fresh owner admits/continues Unknown
    owner.group=frame.group;owner.mode=cs::Mode::Returning;owner.reason=cs::Reason::Refuted;
    return capture_service_input(state);
}
[[maybe_unused]] CaptureFlow capture_service_input(Controller& state)
{
    if constexpr(cfg::capture_service)
    {
        auto& owner=capture_owner(state);const bool returning=owner.mode==cs::Mode::Returning;
        if(!returning && (state.phase!=Phase::Capture || state.group<0))return CaptureFlow::Legacy;
        if(!state.potential || state.potential->status()!=mb::production::potential::Status::Ready || !state.walk)
            return CaptureFlow::Dispatcher;
        const int epoch=static_cast<int>(cambc::round_number());auto& actor=cambc::self();
        const auto origin=actor.position();const auto flags=actor.state();
        cs::Frame frame;frame.epoch=epoch;frame.group=returning?owner.group:state.group;frame.pos=capture_point(origin);
        frame.alive=flags.contains(cambc::champion_state::alive);
        frame.channeling=flags.contains(cambc::champion_state::recalling) || flags.contains(cambc::champion_state::submitting_gems) ||
            flags.contains(cambc::champion_state::teleporting);
        frame.quiet=contact_immutable_quiet(origin);frame.field=state.potential_generation;
        frame.graph=state.route.graph_revision;
        frame.route_ready=state.route.status==Status::Ready && state.route.kind==RouteKind::Capture && state.route.group==frame.group;
        bool on_member=false;for(const auto& member:World::get().groups[frame.group].members)on_member|=member.pos==origin;
        frame.needs_geometry=!on_member && !state.capture_word && !frame.route_ready;
        if(state.request_assembly)
        {
            const auto& assembly=*state.request_assembly;
            frame.geometry={1,assembly.phase,assembly.empty_group,assembly.empty_member,assembly.root_target,assembly.root_cell};
        }
        else if(state.pending_route)frame.geometry.stage=2;
        else if(frame.route_ready)frame.geometry.stage=3;
        const auto index=static_cast<std::size_t>(origin.y)*World::get().width+origin.x;
        if(frame.route_ready && index<state.route.distances.size() && state.route.distances[index]!=cambc::distance_field::unreachable)
            frame.distance=state.route.distances[index];
        if(actor.position()!=origin || !current(epoch))return CaptureFlow::Handled;
        if(!frame.alive){owner.reset();return CaptureFlow::Dispatcher;}
        if(frame.channeling)return CaptureFlow::Dispatcher;
        // Pure frozen Capture geometry may finish only at actual P0. No capture
        // observation/action is prepaid here; the later action input re-reads it.
        const bool capture_assembly=state.request_assembly && state.request_assembly->kind==RouteKind::Capture &&
            state.request_assembly->group==frame.group;
        const bool capture_pending=state.pending_route && state.pending_route->kind==RouteKind::Capture &&
            state.pending_route->group==frame.group;
        if(!returning && owner.mode==cs::Mode::Approach && frame.alive && !frame.channeling && frame.quiet &&
            (capture_assembly || capture_pending))
        {state.planning_quiet=true;return CaptureFlow::Geometry;}
        if(returning)frame.read=cs::Read::Fresh;
        else
        {
            const auto fresh=capture_authority(frame.group,origin,origin,true);
            // Early headroom withdrawal has no capture-result stamp. The own
            // body frame above is still fenced; classify this as unavailable,
            // not an eternal mismatched packet that bypasses owner accounting.
            if(fresh.status==Status::NeedCompute)frame.read=cs::Read::Unavailable;
            else if(fresh.epoch!=epoch || fresh.pos!=origin || !current(epoch))frame.read=cs::Read::Stale;
            else frame.read=fresh.status==Status::Stale?cs::Read::Stale:
                fresh.status==Status::Ready || fresh.status==Status::NeedObservation?cs::Read::Fresh:cs::Read::Unavailable;
            const bool sampled=fresh.epoch==epoch && fresh.group==frame.group && fresh.pos==origin && fresh.hp>0 && current(epoch);
            if(sampled)
            {
                frame.receipt=fresh.gems>state.capture_bag;
                reconcile_capture_life(state,fresh); // factual life survives a later payment-model withdrawal
            }
            if(frame.read==cs::Read::Fresh)
            {
                if(fresh.availability==Availability::Alive)frame.life=cs::Life::Alive;
                else if(fresh.availability==Availability::Reward)frame.life=cs::Life::Reward;
                else if(fresh.availability==Availability::Respawning)frame.life=cs::Life::Respawning;
                frame.safe_capture=fresh.status==Status::Ready && fresh.safe_input;
            }
        }
        // Unknown capture packets omit this query. Never interpret their default
        // visible_enemy=false as a complete fresh negative personal observation.
        frame.enemy=!cambc::vision::champion_tiles(cambc::team::enemy).is_empty();frame.enemy_known=true;
        CaptureBackend backend{state};
        const auto outcome=cs::dispatch(owner,frame,backend);
        if(outcome==cs::Outcome::Contact)return CaptureFlow::Contact;
        if(outcome==cs::Outcome::LegacyCapture)return capture_reward_input(state,frame,backend);
        if(outcome==cs::Outcome::Geometry || outcome==cs::Outcome::RewardGeometry)
        {
            if(state.route.status==Status::Ready && state.route.kind!=RouteKind::Capture)invalidate(state);
            state.expected=origin;
            state.planning_quiet=frame.quiet || outcome==cs::Outcome::RewardGeometry;
            return CaptureFlow::Geometry;
        }
        if(outcome==cs::Outcome::YieldDispatcher)return CaptureFlow::Dispatcher;
        if(outcome==cs::Outcome::NeedResolver)
        {
            const auto actual=actor.position();const auto path=return_witness(state,actual);
            if(resolve_exit_step(state,path,true))return CaptureFlow::Handled;
            state.escape_required=true;return CaptureFlow::Dispatcher;
        }
        if(outcome==cs::Outcome::Revalue || outcome==cs::Outcome::Refuted || outcome==cs::Outcome::Bank)
        {
            state.phase=outcome==cs::Outcome::Bank?Phase::Bank:Phase::Acquire;state.group=-1;
            state.escape_required=false;state.close_front=false;state.basic_next=false;state.partial_progress=false;
            state.expected=actor.position();invalidate(state);return CaptureFlow::Handled;
        }
        return CaptureFlow::Handled;
    }
    return CaptureFlow::Legacy;
}
bool capture_input(Controller& state)
{
    if (state.phase!=Phase::Capture || state.group<0) return false;
    if constexpr(cfg::prefix_service && cfg::direct_service)
    {
        if(!state.capture_word)
        {
            const int epoch=static_cast<int>(cambc::round_number());
            const auto origin=cambc::self().position();
            bool on_spawn=false;
            for(const auto& member:World::get().groups[state.group].members)on_spawn|=member.pos==origin;
            if(on_spawn || state.route.status!=Status::Ready || state.route.kind!=RouteKind::Capture)
            {
                const auto fresh=capture_authority(state.group,origin,origin,!on_spawn);
                reconcile_capture_life(state,fresh);
                mark(state,"direct-capture-stage",epoch,fresh.status,on_spawn);
                if(fresh.status==Status::Ready && fresh.gems>state.capture_bag)
                {state.phase=Phase::Bank;state.group=-1;state.escape_required=!immutable_camp_quiet(origin);invalidate(state);return true;}
                if constexpr(cfg::admission_service)
                {
                    if(fresh.status==Status::Ready && fresh.availability==Availability::Reward &&
                        admission_data(state).selected_family==production_admission::Family::Observe)
                    {resume_admission(state,false,true);return true;}
                    if(refuse_unknown_frontier(state,fresh,epoch,origin))return true;
                    const auto decision=capture_admission(state,fresh,epoch,origin,on_spawn);
                    if(decision==capture_handoff::Decision::ObservationIngress)
                    {state.planning_quiet=true;return false;}
                    if(decision==capture_handoff::Decision::RevalueWork || decision==capture_handoff::Decision::RefuteNominee)
                    {resume_admission(state,decision==capture_handoff::Decision::RevalueWork);return true;}
                }
                if(fresh.status==Status::Ready && fresh.availability==Availability::Respawning)
                {state.phase=Phase::Acquire;state.group=-1;invalidate(state);return true;}
                if(fresh.status==Status::Ready && fresh.availability==Availability::Alive)
                {state.phase=Phase::Work;invalidate(state);return true;}
                if(fresh.status==Status::Ready && fresh.safe_input)
                {
                    if(on_spawn)return true;
                    // Reward+zero external sources makes this current staging
                    // input quiet although the selected group's design field
                    // still contains its now-dead members. Recheck next input.
                    state.planning_quiet=true;return false;
                }
                if(fresh.status==Status::NeedCompute || fresh.status==Status::Stale)return true;
                if(!immutable_camp_quiet(origin))
                {
                    state.escape_required=true;const auto path=return_witness(state,origin);
                    return path.empty()?true:potential_exit(state,path);
                }
                if constexpr(cfg::admission_service){resume_admission(state,false);return true;}
                else {state.phase=Phase::Acquire;state.group=-1;invalidate(state);return false;}
            }
        }
    }
    if constexpr(cfg::prefix_service)
    {
        if (!state.capture_word && state.route.kind==RouteKind::Capture && state.route.status==Status::Ready)
        {
            const int fresh_epoch=static_cast<int>(cambc::round_number());
            const auto here=cambc::self().position();
            Observation stamp;stamp.status=Status::NeedObservation;stamp.epoch=fresh_epoch;stamp.pos=here;
            auto destinations=proposed_steps(state.route,stamp);
            for (const auto& member:World::get().groups[state.group].members)
                if(member.pos==here){destinations={here};break;}
            for (const auto destination:destinations)
            {
                const auto fresh=capture_authority(state.group,here,destination);
                reconcile_capture_life(state,fresh);
                mark(state,"prefix-capture",fresh_epoch,fresh.status,fresh.moved);
                if(fresh.moved)
                {
                    state.expected=fresh.pos;
                    if(state.walk)record_applied_ordinary_step(state,{here.x,here.y},{fresh.pos.x,fresh.pos.y},
                        immutable_camp_quiet(here),immutable_camp_quiet(fresh.pos));
                }
                if(fresh.status==Status::Ready && fresh.gems>state.capture_bag)
                {state.phase=Phase::Bank;state.group=-1;state.escape_required=!immutable_camp_quiet(fresh.pos);invalidate(state);return true;}
                if constexpr(cfg::admission_service)
                {
                    bool on_member=false;for(const auto& member:World::get().groups[state.group].members)on_member|=member.pos==here;
                    const auto decision=capture_admission(state,fresh,fresh_epoch,here,on_member);
                    if(decision==capture_handoff::Decision::ObservationIngress)
                    {state.planning_quiet=true;return false;}
                    if(decision==capture_handoff::Decision::RevalueWork || decision==capture_handoff::Decision::RefuteNominee)
                    {resume_admission(state,decision==capture_handoff::Decision::RevalueWork);return true;}
                }
                if(fresh.status==Status::Ready && fresh.availability==Availability::Respawning)
                {state.phase=Phase::Acquire;state.group=-1;invalidate(state);return true;}
                if(fresh.status==Status::Ready && fresh.availability==Availability::Alive)
                {state.phase=Phase::Work;invalidate(state);return true;}
                // Fresh Reward excludes the cleared group; the adapter accepts
                // only zero external NPC exposure and no visible enemies.
                // Every later hold re-establishes that exact one-input premise.
                if(fresh.status==Status::Ready && fresh.safe_input)return true;
                if(fresh.status==Status::NeedCompute || fresh.status==Status::Stale)return true;
            }
            // A refused fresh capture guard cannot fall through to the legacy
            // on-tile hold. Leave a harmful pose via actual return, or relinquish
            // the quiet task so global observation/threat handling owns the input.
            if (!immutable_camp_quiet(here))
            {
                state.escape_required=true;
                const auto path=return_witness(state,here);
                if (!path.empty()) potential_exit(state,path);
                return true;
            }
            if constexpr(cfg::admission_service){resume_admission(state,false);return true;}
            else {state.phase=Phase::Acquire;state.group=-1;invalidate(state);}
            return false;
        }
    }
    if (!state.capture_word) return false;
    const int epoch=static_cast<int>(cambc::round_number());
    const auto origin=cambc::self().position();
    if (!current(epoch)) { state.escape_required=true; return true; }
    const auto exit=[&] {
        state.escape_required=true;
        const auto actual=cambc::self().position();
        if (!current(epoch)) return true;
        const auto path=return_witness(state,actual);
        if (!path.empty()) potential_exit(state,path);
        return true;
    };
    // This is a conditional geometry offer only. The adapter below must freshly
    // establish Reward before granting this input's Step or Hold authority.
    const auto candidate=state.capture_word->leg({origin.x,origin.y},state.group,state.capture_graph,true);
    if (!candidate) return exit();
    const vec2 destination{candidate->destination.x,candidate->destination.y};
    const auto authority=capture_authority(state.group,origin,destination);
    reconcile_capture_life(state,authority);
    mark(state,"capture-input",epoch,authority.status,authority.moved);
    // Every actual successful edge changes the historical return premise,
    // including a step whose pre-move bag observation also reveals a receipt.
    if (authority.moved)
    {
        state.expected=authority.pos;
        if (!state.walk || record_applied_ordinary_step(state,{origin.x,origin.y},{authority.pos.x,authority.pos.y},
                immutable_camp_quiet(origin),immutable_camp_quiet(authority.pos))!=production_walk::Status::Known ||
            !state.capture_word->consume_applied({origin.x,origin.y},
                {authority.pos.x,authority.pos.y},state.group,state.capture_graph)) return exit();
    }
    if (authority.status==Status::Ready && authority.gems>state.capture_bag)
    {
        // The actual private bag is a conversion resource. Geometry, an elapsed
        // counter and disappearance of a task never supply a receipt.
        state.phase=Phase::Bank; state.group=-1;
        state.escape_required=!immutable_camp_quiet(authority.pos);
        invalidate(state); return true;
    }
    if constexpr(cfg::admission_service)
    {
        bool on_member=false;for(const auto& member:World::get().groups[state.group].members)on_member|=member.pos==origin;
        const auto decision=capture_admission(state,authority,epoch,origin,on_member);
        if(decision==capture_handoff::Decision::ObservationIngress)
        {state.planning_quiet=true;return false;}
        if(decision==capture_handoff::Decision::RevalueWork || decision==capture_handoff::Decision::RefuteNominee)
        {resume_admission(state,decision==capture_handoff::Decision::RevalueWork);return true;}
    }
    if (authority.status==Status::Ready && authority.availability==Availability::Respawning)
    { state.phase=Phase::Acquire; state.group=-1; invalidate(state); return true; }
    if (authority.status==Status::Ready && authority.availability==Availability::Alive)
    { state.phase=Phase::Work; invalidate(state); return true; }
    if constexpr(cfg::admission_service)
        if(contact_immutable_quiet(origin) && !authority.moved && authority.availability==Availability::Reward &&
            (authority.status==Status::Ready || authority.status==Status::NeedObservation) && !authority.safe_input)
        {resume_admission(state,false);return true;}
    if (authority.status!=Status::Ready || !authority.safe_input) return exit();
    return true; // each later hold/step resolves fresh lifecycle and authority
}
Status prepare_capture(Controller& state,const World& world,int epoch,vec2 origin)
{
    if (state.transit_ingress.empty()) return Status::NeedObservation;
    const auto last=state.transit_ingress.back();
    const vec2 frontier{last.x,last.y};
    if (state.capture_word && (state.capture_group!=state.group ||
        state.capture_graph!=state.route.graph_revision || state.capture_frontier!=frontier))
        state.capture_word.reset();
    if (!state.capture_word)
    {
        const auto resolved=resolve_capture_frontier(state.route,frontier,origin,epoch);
        if (resolved!=Status::Ready) return resolved;
        state.capture_group=state.group;
        state.capture_graph=state.route.graph_revision;
        state.capture_frontier=frontier;
        state.capture_word.emplace(world.width,world.height,state.group,state.capture_graph,
            std::move(state.route.capture_distances),128);
        // A different representative may need its own dated coverage. Its
        // native field remains retained and may export in another quiet input.
        state.route.capture_export_pending=true;
        state.route.capture_status=Status::NeedCompute;
        state.capture_word->begin({frontier.x,frontier.y},state.group,state.capture_graph);
        return Status::NeedCompute;
    }
    const auto result=state.capture_word->advance(32);
    if (cambc::self().position()!=origin || !current(epoch)) return Status::Stale;
    if (result==production_capture::Status::Unknown) return Status::NeedObservation;
    return result==production_capture::Status::Ready?Status::Ready:Status::NeedCompute;
}
bool prefix_transit(Controller& state,int epoch,vec2 origin)
{
    if (!state.walk || !state.potential || state.route.status!=Status::Ready) return false;
    const bool enemy=!cambc::vision::champions(cambc::team::enemy).empty();
    if (!current(epoch)) return true;
    if (enemy)
    {
        if (immutable_camp_quiet(origin)) return false;
        state.escape_required=true;
        const auto path=return_witness(state,origin);
        if (!path.empty()) potential_exit(state,path);
        return true;
    }
    Observation stamp;stamp.status=Status::NeedObservation;stamp.epoch=epoch;stamp.pos=origin;
    const auto candidates=proposed_steps(state.route,stamp);
    for (const auto destination:candidates)
    {
        if (!budget(16000)) { state.escape_required=!immutable_camp_quiet(origin);return true; }
        const auto word=production_walk::prefix_word(*state.walk,{origin.x,origin.y},
            {destination.x,destination.y},[&](auto point){return immutable_camp_quiet({point.x,point.y});});
        if (word.empty()) continue;
        std::vector<vec2> path;path.reserve(word.size());
        for (const auto point:word) path.push_back({point.x,point.y});
        // The two future service inputs are explicitly charged at the landing.
        const auto authority=potential_authority(*state.potential,path,PotentialUse::CertifiedEntry,destination,2);
        mark(state,"prefix-entry",epoch,authority.status,authority.moved);
        if (authority.status==Status::Ready && authority.moved)
        {
            state.expected=authority.pos;state.escape_required=false;
            if (record_applied_ordinary_step(state,{origin.x,origin.y},{authority.pos.x,authority.pos.y},
                immutable_camp_quiet(origin),immutable_camp_quiet(authority.pos))!=production_walk::Status::Known)
                state.escape_required=true;
            return true;
        }
        if (authority.status==Status::NeedCompute || authority.status==Status::Stale)
        {state.escape_required=!immutable_camp_quiet(origin);return true;}
        if (authority.status==Status::Ready && !authority.can_move) return true;
    }
    // No current productive ingress edge: physically resolve to the retained
    // quiet suffix before any new global computation; refusal is not infeasibility.
    const auto back=return_witness(state,origin);
    if (!immutable_camp_quiet(origin))
    {state.escape_required=true;if(!back.empty())potential_exit(state,back);return true;}
    return false; // retain the old full-trip family as an additional alternative
}

bool transit_input(Controller& state,const World& world,int epoch,vec2 origin,
                   const std::vector<vec2>& return_path)
{
    if constexpr (cfg::trace_entry) state.transit_fuel0=cambc::fuel_remaining();
    if (return_path.empty()) return false; // unsupported history: full guard remains
    if (!budget(12000)) { state.escape_required=true; return true; }
    if (!cambc::vision::champions(cambc::team::enemy).empty())
    {
        if (immutable_camp_quiet(origin)) return false;
        state.escape_required=true;
        return potential_exit(state,return_path);
    }
    if (!current(epoch)) { state.escape_required=true; return true; }
    const auto key=production_transit::Key{state.group,state.graph_revision,state.potential_generation,
        state.walk->revision()};
    const auto withdraw=[&] {
        invalidate(state);
        if (immutable_camp_quiet(origin)) return true;
        state.escape_required=true;
        potential_exit(state,return_path);
        return true;
    };
    if (state.walk->status()!=production_walk::Status::Known) return withdraw();
    if (state.transit_probe)
    {
        const auto hp=cambc::self().health();
        if (cambc::self().position()!=origin || !current(epoch)) return withdraw();
        if (!state.transit_probe->matches(key,{origin.x,origin.y},hp))
        {
            state.transit_probe.reset(); state.transit.reset();
            state.transit_ingress.clear(); state.transit_capacity_checked=false;
            state.capture_retention.reset(); state.publication_history.reset();
        }
    }
    if (state.transit)
    {
        if (!state.transit->matches(key)) return withdraw();
        if (state.transit->status()!=production_transit::Status::Ready)
        {
            // Pure word/capacity/charge construction owns QUIET inputs. Its
            // partial cursors survive yields; no global observer can replace it.
            if (origin!=state.transit_build_origin || !immutable_camp_quiet(origin)) return withdraw();
            if (!state.transit_capacity_checked)
            {
                const bool retained=state.walk->can_retain_prefix(
                    std::span<const production_walk::Point>{state.transit_ingress},
                    [&](production_walk::Point point) {
                        const auto value=state.potential->at({point.x,point.y});
                        return value.status==mb::production::potential::Status::Ready && value.upper==0;
                    });
                if (cambc::self().position()!=origin || !current(epoch)) return withdraw();
                if (!retained) return withdraw();
                state.transit_capacity_checked=true;
                mark(state,"transit-capacity",epoch,Status::NeedCompute);
                return true;
            }
            while (budget(24000) && state.transit->status()==production_transit::Status::NeedCompute)
            {
                state.transit->advance(*state.potential,32);
                if (!current(epoch)) return withdraw();
            }
            if (cambc::self().position()!=origin || !current(epoch)) return withdraw();
            if (state.transit->status()==production_transit::Status::Unknown) return withdraw();
            mark(state,"transit-charges",epoch,state.transit->status()==production_transit::Status::Ready ? Status::Ready : Status::NeedCompute);
            return true; // publication is pure; actual authority starts next input
        }
        const auto leg=state.transit->leg(key,{origin.x,origin.y},
            state.walk->status()==production_walk::Status::Known);
        if (!leg) return withdraw();
        if (!(cfg::prefix_service && cfg::direct_service) && origin==state.transit_build_origin &&
            (!state.capture_word || state.capture_word->status()!=production_capture::Status::Ready))
        {
            if (!immutable_camp_quiet(origin)) return withdraw();
            const auto result=prepare_capture(state,world,epoch,origin);
            mark(state,"capture-prepare",epoch,result);
            if (result==Status::NeedObservation || result==Status::Stale) return withdraw();
            return true; // publication and native authority own separate inputs
        }
        if (!(cfg::prefix_service && cfg::direct_service) && origin==state.transit_build_origin &&
            (!state.capture_retention || state.capture_retention->status()!=production_walk::RetentionStatus::Ready))
        {
            // Retain the entire future actual walk, including post-clear
            // capture steps. The proof is staged at the unchanged quiet pose.
            if (!immutable_camp_quiet(origin) || !state.capture_word) return withdraw();
            if (!state.capture_retention)
            {
                const auto capture=state.capture_word->path();
                if (capture.empty() || state.transit_ingress.empty() ||
                    state.transit_ingress.back()!=production_walk::Point{capture.front().x,capture.front().y} ||
                    state.capture_word_generation==std::numeric_limits<std::uint64_t>::max()) return withdraw();
                std::vector<production_walk::Point> whole;
                whole.reserve(state.transit_ingress.size()+capture.size()-1);
                whole.insert(whole.end(),state.transit_ingress.begin(),state.transit_ingress.end());
                for (std::size_t i=1;i<capture.size();++i) whole.push_back({capture[i].x,capture[i].y});
                ++state.capture_word_generation; // binds these exact points and immutable potential lifetime
                state.capture_retention.emplace(*state.walk,std::move(whole),
                    production_walk::RetentionKey{state.walk->revision(),state.capture_word_generation},257);
                mark(state,"capture-retention-word",epoch,Status::NeedCompute);
                return true;
            }
            const auto result=state.capture_retention->advance(
                {state.walk->revision(),state.capture_word_generation},
                [&](production_walk::Point point) {
                    const auto value=state.potential->at({point.x,point.y});
                    return value.status==mb::production::potential::Status::Ready && value.upper==0;
                },16);
            if (cambc::self().position()!=origin || !current(epoch)) return withdraw();
            if (result==production_walk::RetentionStatus::Unknown) return withdraw();
            mark(state,"capture-retention",epoch,result==production_walk::RetentionStatus::Ready?Status::Ready:Status::NeedCompute);
            return true;
        }
        if (!(cfg::prefix_service && cfg::direct_service) && origin==state.transit_build_origin &&
            (!state.capture_retention || !state.capture_retention->matches(
                {state.walk->revision(),state.capture_word_generation}))) return withdraw();
        const vec2 destination{leg->destination.x,leg->destination.y};
        const auto authority=transit_authority(leg->required_hp,origin,destination);
        mark(state,"transit-step",epoch,authority.status,authority.moved);
        if (authority.status==Status::Ready && authority.moved)
        {
            state.expected=authority.pos;
            const auto recorded=record_applied_ordinary_step(state,{origin.x,origin.y},{authority.pos.x,authority.pos.y},
                immutable_camp_quiet(origin),immutable_camp_quiet(authority.pos));
            const auto next_key=production_transit::Key{state.group,state.graph_revision,state.potential_generation,
                state.walk->revision()};
            if (recorded!=production_walk::Status::Known ||
                !state.transit->consume_applied({origin.x,origin.y},{authority.pos.x,authority.pos.y},next_key))
            { state.escape_required=true; invalidate(state); return true; }
            state.transit_probe.reset();
            state.escape_required=false;
            return true;
        }
        if (authority.status==Status::Ready && !authority.safe_return)
        {
            // A COMPLETE bound refutes this exact deterministic word only.
            // Its quiet probe keeps the other first-endpoint words available.
            if (state.transit_probe && origin==state.transit_build_origin && immutable_camp_quiet(origin) &&
                state.transit_probe->matches(key,{origin.x,origin.y},authority.hp) &&
                state.transit_probe->reject_completed({destination.x,destination.y},leg->required_hp,authority.hp))
            {
                state.transit.reset(); state.transit_ingress.clear(); state.transit_capacity_checked=false;
                state.capture_word.reset(); state.capture_group=-1;
                state.capture_retention.reset();
                mark(state,"transit-word-reject",epoch,Status::Ready);
                return true;
            }
            return withdraw();
        }
        if (authority.status==Status::NeedObservation) return withdraw();
        state.escape_required=!immutable_camp_quiet(origin);
        return true; // deferral/spent allowance cannot start an optional observer
    }
    // Construct the dated, pure whole-word record once at a quiet pose. Later
    // inputs separately prove capacity and charges before any actual movement.
    if (!immutable_camp_quiet(origin))
    { state.escape_required=true; potential_exit(state,return_path); return true; }
    Observation stamp;
    stamp.status=Status::NeedObservation; stamp.epoch=epoch; stamp.pos=origin;
    if (!state.transit_probe)
    {
        const auto candidates=proposed_steps(state.route,stamp);
        if (candidates.empty()) return withdraw();
        std::vector<production_transit::Point> firsts;
        firsts.reserve(candidates.size());
        for (const auto point:candidates) firsts.push_back({point.x,point.y});
        const auto hp=cambc::self().health();
        if (cambc::self().position()!=origin || !current(epoch)) return withdraw();
        state.transit_probe.emplace(key,production_transit::Point{origin.x,origin.y},hp,std::move(firsts));
        state.transit_probe_key=key;
    }
    if (state.transit_probe->exhausted())
    {
        // Exhaustion is a resolving-operator result for this finite declared
        // family. Retain its exact dated field/input/history proof, then query
        // other nominations. No route, camp or unrepresented path is banned.
        if (!state.publication_history)
        {
            state.publication_history.emplace();
            state.publication_history->begin(state.walk->nodes().size(),state.walk->revision());
        }
        const auto copied=state.publication_history->advance(state.walk->nodes(),state.walk->revision(),16);
        if (cambc::self().position()!=origin || !current(epoch)) return withdraw();
        if (copied==production_history::Status::Unknown) return withdraw();
        if (copied!=production_history::Status::Ready)
        { mark(state,"family-history",epoch,Status::NeedCompute); return true; }
        // Publication has a fixed-size history value and only moves the owned
        // variable-size records. It does not allocate a new history vector.
        DeferredFamily family;
        family.key=state.transit_probe_key; family.origin=origin;
        family.reach=state.route_reach; family.availability_revision=state.route_availability_revision;
        family.visible=std::move(state.route_visible); family.living=std::move(state.route_living);
        family.history=*state.publication_history;
        family.dated_field=std::move(state.route.distances);
        family.probe=std::move(state.transit_probe);
        state.memory[static_cast<std::size_t>(state.group)].alternative_needed.emplace(std::move(family));
        state.phase=Phase::Acquire; state.group=-1;
        invalidate(state);
        mark(state,"transit-alternative",epoch,Status::NeedObservation);
        return true;
    }
    const auto first=state.transit_probe->next();
    if (!first) return withdraw();
    const std::array<vec2,1> candidates{{{first->x,first->y}}};
    for (const auto destination:candidates)
    {
        const auto finite_walk=approach_witness(state.route,origin,destination,return_path,
            2*state.walk->max_nodes()+1);
        if constexpr (cfg::trace_entry)
        { state.transit_fuel1=cambc::fuel_remaining(); state.transit_nodes=finite_walk.size(); }
        if (finite_walk.empty()) continue; // finite witness family Unknown, not infeasible
        std::vector<production_walk::Point> ingress;
        std::vector<production_transit::Point> word;
        ingress.reserve(finite_walk.size());
        word.reserve(finite_walk.size());
        for (const auto point:finite_walk) word.push_back({point.x,point.y});
        bool found=false;
        for (const auto point:finite_walk)
        {
            ingress.push_back({point.x,point.y});
            const auto index=static_cast<std::size_t>(point.y)*world.width+point.x;
            if (index<state.route.distances.size() && state.route.distances[index]==0)
            { found=true; break; }
        }
        if (!found) continue;
        if (cambc::self().position()!=origin || !current(epoch)) return withdraw();
        state.transit.emplace(std::move(word),ingress.size()-1,2,key,2*state.walk->max_nodes()+1);
        state.transit_ingress=std::move(ingress);
        state.transit_build_origin=origin;
        state.transit_capacity_checked=false;
        mark(state,"transit-word",epoch,Status::NeedCompute,static_cast<int>(finite_walk.size()));
        return true;
    }
    // A blocked/refused frontier revokes the route, never actual walk history.
    // One failure phase is reserved; repeated/permanent blocking remains Unknown.
    return withdraw();
}
bool service_input(Controller& state,const World& world)
{
    namespace potential=mb::production::potential;
    if (!state.potential || state.potential->status()!=potential::Status::Ready || !state.walk) return false;
    const int epoch=static_cast<int>(cambc::round_number());
    const auto origin=cambc::self().position();
    if (!current(epoch)) return true;
    const auto path=return_witness(state,origin);
    bool reposition_positive=false;
    if (state.phase==Phase::Work && state.route.status==Status::Ready)
    {
        const auto index=static_cast<std::size_t>(origin.y)*world.width+origin.x;
        if (index<state.route.distances.size() && state.route.distances[index]>0 &&
            state.route.distances[index]!=cambc::distance_field::unreachable) reposition_positive=true;
    }
    if constexpr(cfg::assignment_service)
        if(assignment_perch(state) && !assignment_at_perch(state,origin) && state.route.status==Status::Ready)
            reposition_positive=true;
    if constexpr(cfg::prefix_service && cfg::direct_service)
    {
        if(reposition_positive && state.group>=0 && budget(5000) &&
           (!assignment_perch(state) || assignment_at_perch(state,origin)))
        {
            const auto reach=cambc::self().attack_range();
            if(cambc::self().can_attack())
                for(const auto& member:world.groups[state.group].members)
                    if(distance(origin,member.pos)<=reach && cambc::vision::has_camp(member.pos))
                    {reposition_positive=false;break;}
            if(!current(epoch))return true;
        }
    }
    const auto gate=production_schedule::enter({state.escape_required,!path.empty(),
        state.request_assembly.has_value(),state.pending_route.has_value(),reposition_positive,
        state.acquisition && state.acquisition->stage!=AcquisitionStage::Waiting},
        [&]{return potential_exit(state,path);});
    if (gate==production_schedule::Gate::Acquisition) return acquisition_input(state,world);
    if (gate==production_schedule::Gate::Transit)
    {
        if constexpr(cfg::prefix_service)
            if (prefix_transit(state,epoch,origin)) return true;
        return transit_input(state,world,epoch,origin,path);
    }
    if (gate!=production_schedule::Gate::ContinueService) return gate==production_schedule::Gate::Handled;
    if constexpr(cfg::assignment_service)
        if(assignment_perch(state) && !assignment_at_perch(state,origin))return false;
    if (state.phase!=Phase::Work || state.group<0 || path.empty()) return false;
    bool front=false;
    const int reach=cfg::prefix_service?state.service_reach:cambc::self().attack_range();
    for (const auto& member:world.groups[state.group].members)
        front|=distance(origin,member.pos)<=reach;
    if (!front && immutable_camp_quiet(origin)) return false;
    const auto authority=potential_authority(*state.potential,path);
    if (production_schedule::withdraw_authority(authority.status==Status::Ready,state.escape_required))
    {
        // Even this small authority computation can withdraw. Its input is
        // physical waiting; never repeat global observation before egress.
        state.escape_required=true;
        return true;
    }
    if (!authority.safe_service)
    {
        state.escape_required=true;
        return potential_exit(state,path);
    }
    // The finite wait bound permits at most this service-observation input. Any
    // compute withdrawal commits the next input to cheap egress before observing
    // the global world again; it cannot silently become an unbounded hold.
    auto observation=observe_service();
    mark(state,"service-observe",epoch,observation.status);
    if (observation.status!=Status::Ready || !observation.enemies.empty())
    {
        state.escape_required=true;
        potential_exit(state,path);
        return true;
    }
    state.last_max_hp=observation.max_hp;
    auto work=observe_work(observation,state.group);
    mark(state,"service-work",epoch,work.status);
    if (work.status!=Status::Ready)
    {
        state.escape_required=true;
        potential_exit(state,path);
        return true;
    }
    remember(state,work);
    if (work.availability==Availability::Reward)
    { enter_capture(state,observation.gems); return true; }
    if (work.availability==Availability::Respawning)
    {
        if constexpr(cfg::admission_service)resume_admission(state,false);
        else {state.phase=Phase::Acquire;state.group=-1;invalidate(state);}
        return true;
    }
    Applied applied;
    execute_work(state,observation,std::move(work),applied);
    if (state.service_needs_route)
    {
        if constexpr(cfg::assignment_service)
            if(assignment_at_perch(state,observation.pos))
            {
                // Actual failure of this named perch/word retains its siblings.
                retire_assignment(state,true);
                state.phase=Phase::Acquire;state.group=-1;invalidate(state);
                if(!immutable_camp_quiet(observation.pos))
                {state.escape_required=true;potential_exit(state,path);}
                return true;
            }
        if (immutable_camp_quiet(observation.pos))
        {
            // A fresh service packet contains the exact mask fields required
            // by frozen route construction, even though omitted presence
            // families remain Unknown. Post-command packets wait for a new
            // fresh input; old global masks never regain publication authority.
            if (observation.status==Status::Ready)
            {
                if constexpr(cfg::prefix_service)
                {
                    const auto index=static_cast<std::size_t>(observation.pos.y)*world.width+observation.pos.x;
                    if (state.route.status==Status::Ready && index<state.route.distances.size() &&
                        state.route.distances[index]==0) state.close_front=true;
                }
                invalidate(state);
                state.planning_quiet=true;
                state.expected=observation.pos;
                state.last_hp=observation.hp;
                queue_request(state,observation);
                mark(state,"service-reposition",epoch,Status::NeedCompute);
            }
            return true;
        }
        state.escape_required=true;
        potential_exit(state,path);
        return true;
    }
    if (state.service_deferred || state.phase==Phase::Exit)
    {
        state.escape_required=true;
        potential_exit(state,path);
    }
    return true;
}
void record_route_failure(Controller& state,vec2 origin)
{
    if (state.group<0) return;
    auto& memory=state.memory[static_cast<std::size_t>(state.group)];
    // This refutes only one current frozen graph/root request. It is not a permanent
    // task ban, and it is withdrawn by origin, attack-front or known-availability change.
    memory.route_failed=true;
    memory.route_origin=origin;
    memory.route_reach=assignment_perch(state)?0:
        state.close_front ? 1 : cfg::prefix_service?state.service_reach:cambc::self().attack_range();
    memory.route_revision=state.availability_revision;
    if constexpr(cfg::assignment_service)if(assignment_perch(state))retire_assignment(state,true);
}
// A retained geometry request is an owner, not an optional tail after service
// observations. At a fresh immutable-P0 pose it may finish its bounded pure/native
// phase before any work observer or historical-return construction consumes fuel.
// Publication is a dated nomination; the next service/movement input still
// refreshes its own authority. Positive-field poses keep the existing exit owner.
bool geometry_owner_input(Controller& state)
{
    if (state.escape_required || (!state.request_assembly && !state.pending_route) ||
        !state.planning_quiet || !state.potential ||
        state.potential->status()!=mb::production::potential::Status::Ready) return false;
    if (!budget(10000)) return true;
    const int epoch=static_cast<int>(cambc::round_number());
    auto& actor=cambc::self();
    const auto origin=actor.position();
    const auto flags=actor.state();
    if (!flags.contains(cambc::champion_state::alive) ||
        flags.contains(cambc::champion_state::recalling) ||
        flags.contains(cambc::champion_state::submitting_gems) ||
        flags.contains(cambc::champion_state::teleporting)) return false;
    const auto charge=state.potential->at({origin.x,origin.y});
    if (charge.status!=mb::production::potential::Status::Ready || charge.upper!=0) return false;
    const auto hp=actor.health();
    const bool enemy=!cambc::vision::champion_tiles(cambc::team::enemy).is_empty();
    if (actor.position()!=origin || !current(epoch)) { invalidate(state); return true; }
    if (origin!=state.expected || hp<state.last_hp || enemy) return false;
    if (state.request_assembly)
    {
        auto& assembly=*state.request_assembly;
        if (assembly.origin!=origin) { state.request_assembly.reset(); return false; }
        const auto result=advance_request(assembly);
        mark(state,"geometry-request",epoch,result);
        if (actor.position()!=origin || !current(epoch)) { invalidate(state); return true; }
        if (result==Status::Ready)
        {
            if constexpr(cfg::admission_service)
                if(filter_observation_frontiers(state,assembly)!=Status::Ready)return true;
            if constexpr(cfg::admission_service)
                if(actor.position()!=origin || !current(epoch)){invalidate(state);return true;}
            if (assembly.empty_bits!=state.graph_openings ||
                state.graph_known_revision!=state.availability_revision)
            {
                state.graph_openings=std::move(assembly.empty_bits);
                state.graph_known_revision=state.availability_revision;
                ++state.graph_revision;
            }
            assembly.request.graph_revision=state.graph_revision;
            state.route_visible=std::move(assembly.visible_bits);
            state.route_living=std::move(assembly.living_bits);
            state.route_reach=assembly.reach;
            state.route_availability_revision=state.availability_revision;
            state.pending_route=std::move(assembly.request);
            state.request_assembly.reset();
        }
        else if (result!=Status::NeedCompute) state.request_assembly.reset();
        return true;
    }
    const auto result=plan_route(state.route,*state.pending_route,origin,epoch,state.availability);
    mark(state,"geometry-route",epoch,result);
    if (actor.position()!=origin || !current(epoch)) { invalidate(state); return true; }
    if (result==Status::Ready) state.pending_route.reset();
    else if (result==Status::Unreachable)
    {
        record_route_failure(state,origin);
        if constexpr(cfg::admission_service)resume_admission(state,false);
        else {state.phase=Phase::Acquire;state.group=-1;invalidate(state);}
    }
    else if (result!=Status::NeedCompute) state.pending_route.reset();
    return true;
}
#include "assignment_cash_controller.h"
} // namespace

bool contact_immutable_quiet(vec2 point)
{
    const auto& state=controller();
    if(!state.potential)return false;
    const auto charge=state.potential->at({point.x,point.y});
    return charge.status==mb::production::potential::Status::Ready && charge.upper==0;
}

void play_service_draft()
{
    const auto selected=cambc::draft::current_draft(cambc::team::ally);
    constexpr auto choices=std::array{cambc::champion_draft::varg,cambc::champion_draft::marcella,
        cambc::champion_draft::ryth,cambc::champion_draft::vesper_vale,cambc::champion_draft::heart_of_ruin};
    if constexpr(cfg::assignment_service)
    {
        const auto team_size=cambc::draft::team_size();
        const bool prefix=selected.size()<choices.size() &&
            std::equal(selected.begin(),selected.end(),choices.begin());
        for(const auto pick:choices)
            if(std::find(selected.begin(),selected.end(),pick)==selected.end())
            {
                cambc::draft::pick(pick); // Only a successful accepted pick can publish completion.
                assignment_api::publish_roster_contract(prefix && team_size==5 && selected.size()+1==choices.size(),team_size);
                return;
            }
        return;
    }
    for (auto pick : choices)
        if (std::find(selected.begin(),selected.end(),pick)==selected.end())
        { cambc::draft::pick(pick); return; }
}

void play_service_turn()
{
    DispatchPacketScope dispatch_packet;
    auto& state=controller();
    AssignmentProbeScope assignment_probe{state};
    AssignmentFrontierTail assignment_frontier_tail{state};
    AssignmentTail assignment_tail{state};
    if constexpr(cfg::dash_service || cfg::contact_service)
    {
        const int epoch=static_cast<int>(cambc::round_number());
        if(state.last_epoch!=epoch){state.last_epoch=epoch;state.walked_this_turn=false;}
    }
    if constexpr(cfg::bounded_bootstrap)
        if(!World::advance_bootstrap())return;
    auto& world=World::get();
    if (!state.initialised)
    {
        if constexpr(cfg::prefix_service)
        {
            state.service_reach=cambc::self().attack_range();
            for (const auto& ability:cambc::self().abilities())
                if (ability.info())
                    std::visit([&](const auto& typed){
                        using T=std::decay_t<decltype(typed)>;
                        if constexpr(std::is_same_v<T,cambc::ability_damage> || std::is_same_v<T,cambc::ability_cc>)
                        {if(typed.valid_targets.contains(cambc::ability_target_mask::camps) && ability.damage_amount()>0)
                            state.service_reach=std::max(state.service_reach,static_cast<int>(typed.range));}
                        else if constexpr(std::is_same_v<T,cambc::ability_aoe_damage>)
                        {if(typed.valid_targets.contains(cambc::ability_target_mask::camps) && ability.damage_amount()>0)
                            state.service_reach=std::max(state.service_reach,static_cast<int>(typed.range)+static_cast<int>(typed.radius));}
                    },*ability.info());
        }
        state.memory.resize(world.groups.size());
        state.availability.assign(world.groups.size(),Availability::Unknown);
        // This is a per-input witness capacity. Overflow is Unknown and retains
        // the full guard fallback; it is not a physical route-distance claim.
        state.walk.emplace(std::min<std::size_t>(static_cast<std::size_t>(world.width)*world.height,128));
        if constexpr (cfg::trace_entry) state.trace_role=static_cast<int>(cambc::self().champion_name());
        state.initialised=true;
        return;
    }
    if constexpr (cfg::trace_entry)
        if (state.trace_stage && state.trace_epoch<=200 && cambc::fuel_remaining()>95000)
            std::printf("stage r%d %s status%d g%d detail%d left%llu actor%d tf%llu,%llu,%llu tn%zu\n",state.trace_epoch,
                state.trace_stage,state.trace_status,state.trace_group,state.trace_detail,
                static_cast<unsigned long long>(state.trace_fuel),state.trace_role,
                static_cast<unsigned long long>(state.transit_fuel0),
                static_cast<unsigned long long>(state.transit_fuel1),
                static_cast<unsigned long long>(state.transit_fuel2),state.transit_nodes);
    if constexpr (cfg::trace_entry)
    {
        state.trace_stage=nullptr;
        state.transit_fuel0=state.transit_fuel1=state.transit_fuel2=0; state.transit_nodes=0;
    }
    namespace potential=mb::production::potential;
    if (!state.potential)
    {
        // This immutable geometry operator owns a quiet input, separate from
        // live observation and command authority. Storage allocation is once.
        if (!immutable_camp_quiet(cambc::self().position())) return;
        std::vector<potential::Source> sources;
        for (const auto& group:world.groups)
            for (const auto& member:group.members)
                sources.push_back({{member.pos.x,member.pos.y},member.strength,member.range});
        state.potential.emplace(world.width,world.height,std::move(sources));
        ++state.potential_generation;
        return;
    }
    if (state.potential->status()==potential::Status::NeedCompute)
    {
        const int epoch=static_cast<int>(cambc::round_number());
        const auto origin=cambc::self().position();
        if (!current(epoch) || !immutable_camp_quiet(origin)) return;
        while (budget(24000) && state.potential->status()==potential::Status::NeedCompute)
        {
            state.potential->advance(128);
            if (!current(epoch)) return;
        }
        mark(state,"potential-build",epoch,state.potential->status()==potential::Status::Ready ? Status::Ready : Status::NeedCompute);
        return;
    }
    if constexpr(cfg::capital_service)
    {
        if(capital_active())
        {
            const auto capital=step_capital(*state.potential);
            if(capital.returned || capital.aborted)
            {
                const auto actual=cambc::self().position();
                const bool named_return=capital.returned && capital.group>=0 &&
                    static_cast<std::size_t>(capital.group)<world.groups.size() &&
                    actual==vec2{capital.perch.x,capital.perch.y};
                state.phase=named_return?Phase::Work:Phase::Acquire;
                state.group=named_return?capital.group:-1;
                state.expected=actual;state.close_front=false;state.partial_progress=false;
                state.escape_required=!immutable_camp_quiet(actual);
                invalidate(state);
                if(state.walk)
                {
                    state.walk->invalidate();
                    if(!state.escape_required)state.walk->anchor_quiet({actual.x,actual.y});
                }
                // A recovered abort may finish on an actual ordinary step in
                // this input. Do not let that movement history reach an exclusive
                // cast through the fresh service dispatcher.
                if(capital.moved)return;
                // Return is delivered after the final return step's owner turn.
                // Fresh ordinary service may use the remaining current input;
                // the capital forecast grants no action authority.
            }
            else if(capital.handled)return;
        }
    }
    if constexpr(cfg::assignment_service && cfg::assignment_frontier && cfg::assignment_static) {
        if(static_cash_input(state))return;
        if(static_preparation_input(state))return;
        if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::fallback(assignment_reachability_probe::role(),state.last_epoch);
    }
    if constexpr(cfg::route_service)
        if(geometry_owner_input(state))return;
    if constexpr(cfg::recon_service)
    {
        // This named information operator refreshes its own selected visibility
        // before global masks. Observation is not a work certificate.
        if(state.phase==Phase::Acquire && state.recon_request && !state.recon_menu_pending &&
            (!state.acquisition || state.acquisition->stage==AcquisitionStage::Waiting))
        {
            auto request=*state.recon_request;
            request.completed_negative=state.recon_negative;
            const auto recon=step_named_recon(request,*state.potential);
            using RS=recon_execution::Status;
            if(recon.moved)
            {
                state.expected={recon.to.x,recon.to.y};
                // A suspension after an accepted move cannot manufacture the
                // current owner turn's ordinary-walk fact.
                if(recon.epoch==recon.observed_epoch && current(recon.epoch))
                    record_applied_ordinary_step(state,{recon.from.x,recon.from.y},{recon.to.x,recon.to.y},true,true);
                else if(state.walk)state.walk->invalidate();
                invalidate(state);state.recon_negative=false;
                return;
            }
            if(recon.status==RS::Observed || recon.status==RS::AtFrontier)
            {
                // A newly observed fact or actually reached nominated frontier
                // requests an actual menu. Hypothetical vision is not evidence
                // of an actual observed member, nor a reason to skip the menu.
                invalidate(state);state.recon_negative=false;state.recon_menu_pending=true;
            }
            else if(recon.status==RS::Resolved || recon.status==RS::Exhausted ||
                recon.status==RS::Unsupported || recon.status==RS::UnknownGeometry)
            {
                if(request.key.group>=0 && static_cast<std::size_t>(request.key.group)<state.memory.size())
                    state.memory[static_cast<std::size_t>(request.key.group)].observation_status=recon.status;
                state.recon_request.reset();state.recon_negative=false;state.recon_menu_pending=false;cancel_named_recon();
                invalidate(state); // another named class, never the same negative again
            }
            else if(recon.status==RS::Stale)
            {
                state.recon_request.reset();state.recon_negative=false;state.recon_menu_pending=false;cancel_named_recon();
                invalidate(state);
            }
            else if(recon.handled)return;
        }
    }
    // At most one same-owner redispatch after pure route publication. It
    // grants no command authority; every selected adapter refreshes its inputs.
    bool route_redispatched=false;
fresh_route_dispatch:
    if constexpr(cfg::assignment_service && cfg::assignment_frontier && cfg::assignment_static)
        {if(static_assignment_input(state))return;
         if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::fallback(assignment_reachability_probe::role(),state.last_epoch);}
    else if constexpr(cfg::assignment_service && cfg::assignment_frontier && cfg::assignment_handoff)
        if(assignment_handoff_input(state))return;
    bool capture_contact=false,capture_geometry=false;
    if constexpr(cfg::capture_service)
        if(capture_owner(state).mode==cs::Mode::Returning || (state.phase==Phase::Capture && state.group>=0))
        {
            bool quiet=false;
            if(state.potential)
            {
                const auto actual=cambc::self().position();const auto value=state.potential->at({actual.x,actual.y});
                quiet=value.status==potential::Status::Ready && value.upper==0;
            }
            if(cs::before_optional(capture_owner(state),state.phase==Phase::Capture,quiet))
            {
                const auto flow=capture_service_input(state);
                const auto handoff=cs::caller(flow);
                if(handoff==cs::Caller::Stop)return;
                if(handoff==cs::Caller::Legacy)
                {if(capture_input(state))return;capture_geometry=true;}
                else if(handoff==cs::Caller::Geometry)capture_geometry=true;
                else capture_contact=true; // direct fresh observer; no optional dash/planning gate
            }
        }
    if(!capture_contact && !capture_geometry)
    {
    if constexpr(cfg::admission_service)
        if(state.phase==Phase::Acquire && state.acquisition && state.acquisition->stage==AcquisitionStage::Waiting &&
            acquisition_input(state,world))return;
    if constexpr(cfg::dash_service)
    {
        const bool named=state.phase==Phase::Work || state.phase==Phase::Capture || state.phase==Phase::Bank;
        if(named && !state.escape_required && state.route.status==Status::Ready &&
            state.route.graph_revision==state.graph_revision)
        {
            const auto dash=try_dash_progress(state.route,*state.potential,state.walked_this_turn);
            if(dash.attempted || dash.accepted)
            {
                // Spatial mutation never records an ordinary edge or erases
                // earlier ordinary walking. A refusal may reveal a blocker.
                const auto actual=cambc::self().position();
                state.expected=actual;state.escape_required=!immutable_camp_quiet(actual);
                invalidate(state);
                if(state.walk)
                {
                    state.walk->invalidate();
                    if(!state.escape_required)state.walk->anchor_quiet({actual.x,actual.y});
                }
                mark(state,"dash-progress",dash.epoch,
                    dash.accepted?Status::Ready:Status::NeedObservation,dash.slot);
                return;
            }
            if(dash.status==dash_service::Status::NeedCompute || dash.status==dash_service::Status::Stale)return;
        }
    }
    if (state.escape_required && service_input(state,world)) return;
    if (!state.escape_required)
    {
        if constexpr(cfg::capture_service)
        {
            const auto flow=capture_service_input(state);
            const auto handoff=cs::caller(flow);
            if(handoff==cs::Caller::Stop)return;
            if(handoff==cs::Caller::FreshObservation)capture_contact=true;
            else if(handoff==cs::Caller::Geometry)capture_geometry=true;
            else if(capture_input(state))return;
        }
        else if(capture_input(state))return;
    }
    if(!capture_contact && !capture_geometry && service_input(state,world))return;
    }
    // Planning owns a fresh, quiet owner phase. Missing search budget after an
    // observer never repeats the same expensive observer before trying the search.
    if(!capture_contact)
    {
    const int planning_epoch=static_cast<int>(cambc::round_number());
    if (state.request_assembly && state.planning_quiet && cambc::self().is_alive() && current(planning_epoch))
    {
        const auto origin=cambc::self().position();
        const auto flags=cambc::self().state();
        const bool quiet_prefix=origin==state.expected &&
            (reward_preparation_current(state,planning_epoch,origin) || cambc::self().health()>=state.last_hp) &&
            !flags.contains(cambc::champion_state::recalling) &&
            !flags.contains(cambc::champion_state::submitting_gems) &&
            !flags.contains(cambc::champion_state::teleporting) &&
            cambc::vision::champions(cambc::team::enemy).empty();
        if (quiet_prefix && current(planning_epoch))
        {
            auto& assembly=*state.request_assembly;
            const auto result=advance_request(assembly);
            mark(state,"request-build",planning_epoch,result);
            const auto final_origin=cambc::self().position();
            if (final_origin!=origin || !current(planning_epoch))
            { invalidate(state); return; }
            if (result==Status::Ready)
            {
                if constexpr(cfg::admission_service)
                    if(filter_observation_frontiers(state,assembly)!=Status::Ready)return;
                if constexpr(cfg::admission_service)
                    if(cambc::self().position()!=origin || !current(planning_epoch)){invalidate(state);return;}
                if (assembly.empty_bits!=state.graph_openings ||
                    state.graph_known_revision!=state.availability_revision)
                {
                    state.graph_openings=std::move(assembly.empty_bits);
                    state.graph_known_revision=state.availability_revision;
                    ++state.graph_revision;
                }
                assembly.request.graph_revision=state.graph_revision;
                state.route_visible=std::move(assembly.visible_bits);
                state.route_living=std::move(assembly.living_bits);
                state.route_reach=assembly.reach;
                state.route_availability_revision=state.availability_revision;
                state.pending_route=std::move(assembly.request);
                state.request_assembly.reset();
            }
            else if (result!=Status::NeedCompute)
            {
                state.request_assembly.reset();
                if constexpr(cfg::capture_service)
                    if(reward_preparation_current(state,planning_epoch,origin))
                    {capture_owner(state).mode=cs::Mode::Returning;capture_owner(state).reason=cs::Reason::Refuted;}
            }
            return;
        }
        state.request_assembly.reset();
    }
    if (state.pending_route && state.planning_quiet && cambc::self().is_alive() && current(planning_epoch))
    {
        const auto origin=cambc::self().position();
        const auto flags=cambc::self().state();
        const bool quiet_prefix=origin==state.expected &&
            (reward_preparation_current(state,planning_epoch,origin) || cambc::self().health()>=state.last_hp) &&
            !flags.contains(cambc::champion_state::recalling) &&
            !flags.contains(cambc::champion_state::submitting_gems) &&
            !flags.contains(cambc::champion_state::teleporting) &&
            cambc::vision::champions(cambc::team::enemy).empty();
        if (quiet_prefix && current(planning_epoch))
        {
            const auto result=plan_route(state.route,*state.pending_route,origin,planning_epoch,state.availability);
            mark(state,"plan-head",planning_epoch,result);
            if (result==Status::Ready)
            {
                state.pending_route.reset();
                if constexpr(cfg::eager_service)
                {
                    // Pure geometry completion needs no End. The published field
                    // is nomination only; revisit dash/capture/service with fresh
                    // predicates, including sentinel-origin dash opportunities.
                    if(!route_redispatched && budget(60000) &&
                        cambc::self().position()==origin && current(planning_epoch))
                    {
                        route_redispatched=true;
                        goto fresh_route_dispatch;
                    }
                }
            }
            else if (result==Status::Unreachable)
            {
                record_route_failure(state,origin);
                if constexpr(cfg::capture_service)
                    if(reward_preparation_current(state,planning_epoch,origin))
                    {
                        capture_owner(state).mode=cs::Mode::Returning;capture_owner(state).reason=cs::Reason::Refuted;
                        state.pending_route.reset();return;
                    }
                if constexpr(cfg::admission_service)resume_admission(state,false);
                else {state.phase=Phase::Acquire;state.group=-1;invalidate(state);}
            }
            return;
        }
        state.pending_route.reset();
    }
    }
    std::uint64_t observation_fuel{};
    if constexpr (cfg::trace_entry) observation_fuel=cambc::fuel_remaining();
    auto observation=observe();
    mark(state,"observe",observation.epoch,observation.status);
    if constexpr (cfg::trace_entry)
        if (observation.epoch<=8 && cambc::fuel_remaining()>16000)
            std::printf("entry r%d status%d used%llu left%llu\n",observation.epoch,
                static_cast<int>(observation.status),
                static_cast<unsigned long long>(observation_fuel-cambc::fuel_remaining()),
                static_cast<unsigned long long>(cambc::fuel_remaining()));
    if (observation.status!=Status::Ready) return;
    const int epoch=observation.epoch;
    state.last_hp=observation.hp;
    state.last_max_hp=observation.max_hp;
    // Other bodies can wake a sleeping group before our next input. A quiet
    // compute phase therefore requires the immutable potential envelope to be0.
    state.planning_quiet=observation.enemies.empty() && immutable_camp_quiet(observation.pos);
    const bool reward_staging=cfg::capture_service && observation.enemies.empty() &&
        reward_preparation_current(state,epoch,observation.pos);
    if(reward_staging)state.planning_quiet=true;
    for (const auto& camp : observation.camps)
        if (!(reward_staging && camp.group==state.group) && camp.awake && camp.strength>0 && distance(observation.pos,camp.pos)<=camp.range)
            state.planning_quiet=false;
    if (!cambc::self().is_alive())
    {
        if constexpr(cfg::assignment_service)retire_assignment(state,false);
        purchase(observation);
        state.phase=Phase::Acquire; state.group=-1; invalidate(state);
        if (state.walk) state.walk->invalidate();
        state.escape_required=false;
        state.expected={-1,-1};
        return;
    }
    if (state.expected.x>=0 && state.expected!=observation.pos)
    {
        invalidate(state);
        if (state.walk) state.walk->invalidate();
    }
    state.expected=observation.pos;
    if (state.walk && immutable_camp_quiet(observation.pos))
        state.walk->anchor_quiet({observation.pos.x,observation.pos.y});
    if (observation.channeling) return;
    if constexpr(cfg::assignment_service)
        if((state.phase==Phase::Recover || state.phase==Phase::Exit) &&
           !assignment_data(state).programme.selected_home_heal())retire_assignment(state,false);
    if (observation.on_spawn)
    {
        // This selected programme is the priced NoShop continuation. Existing
        // active capital/readback owners already ran before this dispatcher;
        // ordinary purchase remains available for every unselected fallback.
        const auto purchased=assignment_programme(state)?Status::Ready:purchase(observation);
        mark(state,"purchase",epoch,purchased);
        if (purchased==Status::NeedObservation) return;
        if (observation.hp<observation.max_hp) return;
        if (state.phase==Phase::Recover || state.phase==Phase::Exit)
        {
            const bool resume=cfg::direct_service && state.partial_progress && state.group>=0;
            state.phase=resume?Phase::Work:Phase::Acquire;
            if(!resume)state.group=-1;
            state.close_front=false;state.partial_progress=false;invalidate(state);
        }
    }
    // Temporary lack of an action is neither unsafe work nor an instruction to recall.
    if (!observation.can_move && !observation.can_attack && !observation.can_cast) return;
    Applied applied;
    if constexpr(cfg::contact_service)applied.moved=state.walked_this_turn;
    [[maybe_unused]] const auto threat_origin=observation.pos;
    if (!observation.enemies.empty() && service_threat(observation,applied))
    {
        if constexpr(cfg::contact_service)
        {
            const auto actual=cambc::self().position();
            const auto& history=contact_history();
            const auto quiet=[&](vec2 point) {
                if(!state.potential)return false;
                const auto value=state.potential->at({point.x,point.y});
                return value.status==potential::Status::Ready && value.upper==0;
            };
            if(history.epoch==epoch && current(epoch))
            {
                if(history.walked)
                    record_applied_ordinary_step(state,
                        {history.walk_from.x,history.walk_from.y},{history.walk_to.x,history.walk_to.y},
                        quiet(history.walk_from),quiet(history.walk_to));
                if(history.dashed || (history.walked && actual!=history.walk_to))
                {
                    if(state.walk)state.walk->invalidate();
                    if(state.walk && quiet(actual))state.walk->anchor_quiet({actual.x,actual.y});
                }
            }
            else if(state.walk)state.walk->invalidate();
            state.expected=actual;
            state.escape_required=!quiet(actual);
        }
        else
        {
            if constexpr(cfg::capture_service)
                if((state.phase==Phase::Capture || capture_contact || capture_owner(state).mode!=cs::Mode::Dormant) && applied.moved)
                {
                    // The unchanged legacy contact actor can make one ordinary
                    // edge. Record that ACTUAL edge for this Capture owner;
                    // stale/displaced output invalidates support, never invents it.
                    const auto actual=cambc::self().position();
                    if(current(epoch) && distance(threat_origin,actual)==1)
                        record_applied_ordinary_step(state,{threat_origin.x,threat_origin.y},{actual.x,actual.y},
                            contact_immutable_quiet(threat_origin),contact_immutable_quiet(actual));
                    else if(state.walk)state.walk->invalidate();
                    auto& owner=capture_owner(state);owner.expected=capture_point(actual);owner.mode=cs::Mode::Returning;
                    if(owner.reason!=cs::Reason::Bank)owner.reason=cs::Reason::Refuted;
                    state.expected=actual;state.escape_required=!contact_immutable_quiet(actual);
                }
                else state.expected=observation.pos;
            else state.expected=observation.pos;
        }
        invalidate(state); return;
    }
    if (!current(epoch) || observation.status!=Status::Ready) return;
    if constexpr(cfg::capture_service)
        if(capture_contact && !observation.enemies.empty())return; // unsupported contact stays with its fresh owner

    if (state.phase==Phase::Exit || state.phase==Phase::Recover)
    {
        if constexpr(cfg::assignment_service)
            if(!assignment_data(state).programme.selected_home_heal())retire_assignment(state,false);
        const auto coverage=covered(state,observation,nullptr,nullptr);
        if (coverage.status!=Status::Ready) return;
        if (coverage.current_damage>0)
        { begin_exit(state,observation,coverage); return; }
        if (state.needs_healing && !observation.on_spawn)
        {
            // Coverage is evidence from this exact input, not permission to use
            // after a suspended observer. Query origin before the final epoch
            // fence and retain the common native-command tail allowance.
            if (!budget(3000) || cambc::self().position()!=observation.pos || !current(epoch)) return;
            cambc::self().recall(); state.phase=Phase::Recover; return;
        }
        const bool resume=cfg::direct_service && state.partial_progress && state.group>=0;
        state.phase=resume?Phase::Work:Phase::Acquire;
        if(!resume)state.group=-1;
        state.close_front=false;state.partial_progress=false;invalidate(state);
    }
    if (state.phase==Phase::Acquire)
    {
        if (!state.planning_quiet)
        {
            state.escape_required=true;
            const auto path=return_witness(state,observation.pos);
            if (!path.empty()) potential_exit(state,path);
            return;
        }
        retain_acquisition_packet(state,std::move(observation));
        mark(state,"acquire-retain",epoch,Status::NeedCompute);
        return; // observing, ranking, publication and action are separate owners
    }
    if (state.phase==Phase::Recover) return;
    if (state.phase==Phase::Bank)
    {
        if (observation.gems==0)
        { state.phase=Phase::Acquire; invalidate(state); return; }
        for (auto altar : world.altars)
            if (distance(observation.pos,altar)<=cambc::constants::submit_gems_range)
            {
                if (cambc::vision::has_altar_lock(altar))
                {
                    const auto lock=cambc::vision::altar_lock_at(altar);
                    if (lock.is_enemy())
                    {
                        if (cambc::self().can_attack() && distance(observation.pos,lock.position())<=cambc::self().attack_range())
                        { cambc::self().attack(lock); return; }
                        state.close_front=true;
                        invalidate(state);
                        break;
                    }
                }
                cambc::self().submit_gems(altar);
                return;
            }
    }
    else if (state.group>=0)
    {
        bool visible_member=false;
        for (const auto& member : world.groups[static_cast<std::size_t>(state.group)].members)
            if (observation.visible.is_set(member.pos)) { visible_member=true; break; }
        // An unseen group's exact HP/lifecycle cannot be resolved from here. Keep
        // the declared global packet and travel; invoke the resolving operator on entry.
        if (visible_member)
        {
        const auto group=observe_group(observation,state.group);
        mark(state,"group-observe",epoch,group.status);
        if (group.status!=Status::Ready) return;
        remember_group(state,group);
        if constexpr(cfg::capture_service)
            if(state.phase==Phase::Capture && (group.availability==Availability::Alive || group.availability==Availability::Respawning))
            {
                // This ordinary observer is another fresh factual entry seam.
                // Preserve the actual return before retiring/repricing Capture;
                // the old Capture quote never becomes a free full-HP Work job.
                auto& owner=capture_owner(state);owner.group=state.group;owner.mode=cs::Mode::Returning;
                owner.reason=observation.gems>state.capture_bag?cs::Reason::Bank:
                    group.availability==Availability::Alive?cs::Reason::Revalue:cs::Reason::Refuted;
                capture_service_input(state);return;
            }
        if (group.availability==Availability::Respawning)
        {
            for (const auto& member : world.groups[state.group].members)
                if (member.pos==observation.pos)
                {
                    const auto steps=legal_steps(observation);
                    if (!steps.empty()) move_and_remember(state,observation,steps.front());
                    break;
                }
            if constexpr(cfg::admission_service)resume_admission(state,false);
            else {state.phase=Phase::Acquire; state.group=-1; invalidate(state);}
            return;
        }
        if (group.availability==Availability::Reward && state.phase!=Phase::Capture)
        { enter_capture(state,observation.gems); }
        if (group.availability==Availability::Alive && state.phase==Phase::Capture)
        {
            if constexpr(cfg::admission_service){resume_admission(state,true);return;}
            else {state.phase=Phase::Work;invalidate(state);}
        }
        if (state.phase==Phase::Capture)
        {
            for (const auto& member : world.groups[state.group].members)
                if (member.pos==observation.pos) return;
            for (const auto& hero : observation.allies)
                for (const auto& member : world.groups[state.group].members)
                    if (hero.pos==member.pos)
                    {
                        if constexpr(cfg::capture_service)
                        {
                            auto& owner=capture_owner(state);owner.group=state.group;owner.mode=cs::Mode::Returning;
                            owner.reason=observation.gems>state.capture_bag?cs::Reason::Bank:cs::Reason::Refuted;
                            capture_service_input(state);return;
                        }
                        state.phase=Phase::Acquire; state.group=-1; invalidate(state); return;
                    }
        }
        else
        {
            bool in_reach=false;
            const auto& spec=world.groups[static_cast<std::size_t>(state.group)];
            for (std::size_t i=0;i<group.member_count;++i)
                in_reach|=group.visible[i] && group.living[i] &&
                    distance(observation.pos,spec.members[i].pos)<=(cfg::prefix_service?state.service_reach:observation.attack_range);
            if (in_reach && (!assignment_perch(state) || assignment_at_perch(state,observation.pos)))
            {
                auto work=observe_work(observation,state.group);
                mark(state,"work-observe",epoch,work.status);
                if (work.status!=Status::Ready) return;
                remember(state,work);
                if (work.availability!=Availability::Alive)
                { invalidate(state); return; }
                execute_work(state,observation,std::move(work),applied);
                return;
            }
            if (state.route.status==Status::Ready)
            {
                const auto index=static_cast<std::size_t>(observation.pos.y)*world.width+observation.pos.x;
                if (index<state.route.distances.size() && state.route.distances[index]==0)
                {
                    if constexpr(cfg::assignment_service)
                        if(assignment_at_perch(state,observation.pos))
                        {retire_assignment(state,true);state.phase=Phase::Acquire;state.group=-1;invalidate(state);return;}
                    state.close_front=true; invalidate(state);
                }
            }
        }
        }
        if (!visible_member && state.phase==Phase::Work && state.route.status==Status::Ready)
        {
            const auto index=static_cast<std::size_t>(observation.pos.y)*world.width+observation.pos.x;
            if (index<state.route.distances.size() && state.route.distances[index]==0)
            {
                if constexpr(cfg::assignment_service)
                    if(assignment_at_perch(state,observation.pos))
                    {retire_assignment(state,true);state.phase=Phase::Acquire;state.group=-1;invalidate(state);return;}
                state.close_front=true; invalidate(state);
            }
        }
    }
    if (state.route.status!=Status::Ready)
    {
        if (!state.planning_quiet)
        {
            const auto coverage=covered(state,observation,nullptr,nullptr);
            if (coverage.status==Status::Ready && coverage.current_damage>0)
            { begin_exit(state,observation,coverage); return; }
        }
        // Transfer only cheap frozen inputs here. Native mask imports and bounded
        // root construction own a later quiet phase before observation/search.
        queue_request(state,observation);
        mark(state,"request-queued",epoch,Status::NeedCompute);
        return;
    }
    const auto candidates=proposed_steps(state.route,observation);
    mark(state,"descent",epoch,Status::Ready,static_cast<int>(candidates.size()));
    for (auto destination : candidates)
    {
        if (state.potential && state.potential->status()==potential::Status::Ready && state.walk)
        {
            const auto tail=state.walk->make_return({observation.pos.x,observation.pos.y},
                mb::service::Point{destination.x,destination.y});
            if (!tail.empty())
            {
                // The proposed first edge has fresh local authority; every
                // later edge is the reverse actual walk suffix. Potential
                // damage covers entry and return, including another body's wake.
                std::vector<vec2> finite_walk;
                finite_walk.reserve(tail.size()+1);
                finite_walk.push_back(observation.pos);
                for (const auto point:tail) finite_walk.push_back({point.x,point.y});
                const auto authority=potential_authority(*state.potential,finite_walk,PotentialUse::CertifiedEntry);
                mark(state,"potential-entry",observation.epoch,authority.status,authority.moved);
                if (authority.status==Status::Ready && authority.moved)
                {
                    const auto from=observation.pos;
                    observation.pos=authority.pos;
                    observation.status=Status::NeedObservation;
                    state.expected=authority.pos;
                    state.escape_required=false;
                    record_applied_ordinary_step(state,{from.x,from.y},{authority.pos.x,authority.pos.y},
                        immutable_camp_quiet(from),immutable_camp_quiet(authority.pos));
                    return;
                }
                if (authority.status==Status::NeedObservation)
                {
                    // A rejected first landing does not ban the frozen graph
                    // or erase actual history. Other same-gradient candidates
                    // obtain their own fresh point authority in this input.
                    state.escape_required=!immutable_camp_quiet(observation.pos);
                    continue;
                }
                if (authority.status==Status::Stale)
                { state.escape_required=true; return; }
                if (authority.status==Status::NeedCompute)
                { state.escape_required=true; return; }
                if (!authority.can_move) return;
            }
        }
        // Safety is freshly established from this destination, not inferred from the
        // graph. No new camp wake is credited merely for walking into an attack frontier.
        const auto coverage=covered(state,observation,nullptr,nullptr,destination);
        mark(state,coverage.trace_stage ? coverage.trace_stage : "approach-cover",epoch,coverage.status,
            coverage.trace_stage ? coverage.trace_detail : coverage.safe);
        if (coverage.status==Status::NeedObservation) continue;
        if (coverage.status!=Status::Ready) return;
        double refusal_charge=0;
        if (state.potential && state.potential->status()==potential::Status::Ready)
        {
            refusal_charge=state.potential->at({observation.pos.x,observation.pos.y}).upper;
            for (const auto point:coverage.exit_path)
                refusal_charge=std::max(refusal_charge,state.potential->at({point.x,point.y}).upper);
        }
        if (!coverage.safe || observation.hp-coverage.known_next_damage<=coverage.reserve+refusal_charge)
        {
            const auto exit=covered(state,observation,nullptr,nullptr);
            begin_exit(state,observation,exit);
            return;
        }
        move_and_remember(state,observation,destination);
        return;
    }
    invalidate(state);
}
} // namespace tt
