#pragma once
#include "capital_service_forecast_native.h"
#include "capital_service_routes.h"
#include "capital_service_scenario.h"
#include "capital_service_execution.h"
#include "capital_service_storage.h"
#include "capital_service_static_geometry.h"
#include "capital_service_stage_meter.h"
#include <memory>
#include <type_traits>

// The first actual application is ONE body / ONE shared group / an explicit
// fixed-Hold conditional world. This is not a global acquisition comparison or
// a certificate against all hidden worlds. Geometry, forecasts, actual command
// authority and actual lifecycle reconciliation have separate lifetimes.
namespace wb::capital::service_controller_native {
namespace f=service_forecast;namespace n=service_forecast_native;namespace en=encounter_native;
namespace r=service_routes;namespace h=service_scenario;
namespace e=service_execution;
namespace ss=service_storage;
namespace g=service_static_geometry;
namespace sm=service_stage_meter;
enum class Stage {Cold,Idle,Routes,Views,Forecast,Ready,Executing};
enum class Stop {None,Funding,Observation,Scenario,Order,Geometry,Model,Stale,Incomplete};
struct Counters {std::uint64_t observed{},histories{},nominations{},routes{},begun{},quanta{},complete{},selected{},commands{},accepted{},reconciled{},handoffs{},renewed{};
    std::array<std::uint64_t,9> rejected{};std::uint64_t cold_cost{},capture_cost{},route_cost{},begin_cost{},advance_cost{},dispatch_cost{};
    std::uint64_t cold_measured{},cold_cross_round{},cold_component{},history_status{},history_reason{},capture_status{},capture_reason{},model_status{},model_reason{};
    std::uint64_t history_cost{},history_measured{},history_cross_round{},prepare_entries{},prepare_fuel_max{},capture_queries{},capture_entry_max{},background_units{},prepare_gate{};};
struct History {
    h::Tracker tracker;h::Source source{};h::Stamp stamp{};bool ready{},complete_from_first_input{};
    std::array<h::Actor,6> roster{};std::array<int,6> types{};unsigned count{};
    std::vector<std::uint8_t> spawn[2],visible;std::uint64_t sequence{},epoch{};
    struct Observed {bool visible{},completed{},m{},a{},c{},channel{};f::Point point{};float hp{};};
    std::array<Observed,6> observed{};
    std::array<bool,6> alive{};
};
struct Memory {
    Stage stage{Stage::Cold};Stop stop{Stop::None};Counters& counters;History& history;
    n::Capture& captured;n::ModelContext& model;f::Program& program;f::Program& rebound;f::Program* active_program;f::Simulator& simulator;f::World& route_world;
    r::Map& map;r::Request& request;r::Builder& builder;r::Rebinder& rebinder;r::Workspace& workspace;bool rebinding{};
    std::vector<std::uint8_t> blocked,spawn,foreign,occupied;
    std::vector<r::Altar> altars;std::array<r::Participant,6> participants{};
    std::array<f::Purchase,1> purchases{};std::uint64_t map_revision{},foreign_revision{};
    n::Epoch source{},funding_epoch{};int group{-1};unsigned view{},source_generation{1};bool discovery{};
    unsigned fallback_obligation{50000};std::uint64_t assumptions{},history_epoch{};
    e::Interpreter& interpreter;e::Frame& actual;e::Frame& prior;n::Capture& actual_capture;
    std::array<e::Receipt,8> receipts{};unsigned receipt_count{};std::uint64_t receipt_event{},bank_directive{},physical_input{};
    bool walked{},custody_closed{},income_closed{},capture_attributed{},bank_attributed{},bank_was_active{},physical_pending{};
    unsigned bank_bag{};int payout_round{-1},bank_start_round{-1},bank_due_round{-1},log_round{-1};
    f::Point bank_altar{};std::uint64_t bank_start_score{};
    g::Preparer geometry;g::Result geometry_result{};g::Source geometry_source{};
    std::vector<g::SourceNpc> geometry_catalogue;std::vector<std::uint8_t> geometry_spawn;
    std::uint64_t geometry_catalogue_revision{1};unsigned geometry_group_cursor{},geometry_member_cursor{};bool geometry_catalogue_complete{},geometry_catalogue_supported{true};
    // A facade exists ONLY after every real component constructor was admitted.
    // These references remain stable for the lifetime of the owned Storage.
    Memory(ss::Storage<History>& store,Counters& counts):counters(counts),history(store.history()),
        captured(*store.captured()),model(*store.model()),program(*store.program()),rebound(*store.rebound()),active_program(&program),
        simulator(*store.simulator()),route_world(*store.route_world()),map(*store.map()),request(*store.request()),builder(*store.builder()),
        rebinder(*store.rebinder()),workspace(*store.workspace()),interpreter(*store.interpreter()),actual(*store.actual()),prior(*store.prior()),actual_capture(*store.actual_capture()){}
};
struct Bootstrap {
    ss::Storage<History> components;Counters counters{};std::unique_ptr<Memory> facade;Stop stop{Stop::None};int log_round{-1};
    Bootstrap(){} // No enclosing value-zero of future map/program storage.
};
inline Bootstrap& bootstrap(){static Bootstrap value;return value;}
inline Memory* memory(){return bootstrap().facade.get();}
struct AuditState {
    sm::Recorder recorder;bool enabled{},pending{},final_emitted{};
    std::uint64_t invocation{};bool invocation_known{true};
    sm::Stage last_cross{sm::Stage::Unknown};int before_round{-1},after_round{-1};std::uint64_t serial{};
};
// Separate storage is never reached/constructed with the audit knob OFF.
inline AuditState& audit_state(){static AuditState value;return value;}
enum class ScenarioReason : unsigned char {NotSampled,NotReached,PlacementReady,Complete,HistoryNotReady,SnapshotJoint,
    WitnessUnassigned,WitnessGeometry,WitnessLife,MissingInitialObservation,ClassDead,ClassChannel,ClassEffects,
    VisiblePoseMismatch,ExternalCapacity,RosterSize,ExternalChannel,ExternalRestrictions,ExternalPassive,
    ExternalPoint,ForeignNpcRange,SelectedNpcRange,SnapshotActorGeometry,Count};
struct ScenarioStamp {ScenarioReason reason{ScenarioReason::NotSampled};int actor{-1},detail{-1},source_round{-1};bool overflow{};};
// A single bounded data word: reason8/actor16/detail16/source-round16, each
// signed sentinel encoded as0 and actual zero as1. Bit63 marks lost range.
// The round is the captured SOURCE round, not a fabricated current SDK clock.
inline std::uint64_t scenario_word(ScenarioStamp stamp){
    auto field=[&](int value){if(value<-1||value>65534){stamp.overflow=true;return std::uint64_t(0);}return std::uint64_t(value+1);};
    const auto actor=field(stamp.actor),detail=field(stamp.detail),round=field(stamp.source_round);
    return std::uint64_t(stamp.reason)|(actor<<8)|(detail<<24)|(round<<40)|(std::uint64_t(stamp.overflow)<<63);
}
inline ScenarioStamp scenario_stamp(std::uint64_t word){return {static_cast<ScenarioReason>(word&255),int((word>>8)&65535)-1,
    int((word>>24)&65535)-1,int((word>>40)&65535)-1,bool(word>>63)};}
inline bool scenario_word_valid(std::uint64_t word){return (word&255)<unsigned(ScenarioReason::Count)&&!(word&0x7f00000000000000ull);}
inline ScenarioStamp scenario_witness_fault(const h::Witness& witness,bool complete_from_first_input,int source_round){
    auto fail=[&](ScenarioReason reason,int detail=-1){return ScenarioStamp{reason,witness.actor.id,detail,source_round,false};};
    if(!witness.point_assigned)return fail(ScenarioReason::WitnessUnassigned);
    if(witness.geometry!=h::Geometry::Supported)return fail(ScenarioReason::WitnessGeometry,int(witness.geometry));
    if(witness.current.life!=h::Life::Alive)return fail(ScenarioReason::WitnessLife,int(witness.current.life));
    if(witness.origin_is_public_spawn&&!complete_from_first_input)return fail(ScenarioReason::MissingInitialObservation);
    if(witness.class_reason==h::ClassReason::Dead)return fail(ScenarioReason::ClassDead);
    if(witness.class_reason==h::ClassReason::ActiveChannel)return fail(ScenarioReason::ClassChannel);
    if(witness.class_reason==h::ClassReason::UnsupportedEffects)return fail(ScenarioReason::ClassEffects);
    return {};
}
inline ScenarioStamp scenario_external_fault(int actor,bool actual,unsigned faults,int source_round){
    if(actual){
        if(faults&en::Channel)return {ScenarioReason::ExternalChannel,actor,-1,source_round,false};
        if(faults&en::Restrictions)return {ScenarioReason::ExternalRestrictions,actor,-1,source_round,false};
        if(faults&en::UnknownPassive)return {ScenarioReason::ExternalPassive,actor,-1,source_round,false};
    }
    return {};
}
inline ScenarioStamp scenario_snapshot_fault(const h::Snapshot& snapshot,int source_round){
    for(unsigned i=0;i<snapshot.count&&i<snapshot.actors.size();++i)if(snapshot.actors[i].geometry==h::Geometry::Inconsistent)
        return {ScenarioReason::SnapshotActorGeometry,snapshot.actors[i].actor.id,int(snapshot.actors[i].geometry_reason),source_round,false};
    static_assert(unsigned(h::Status::Unknown)<16&&unsigned(h::Assignment::NeedFuel)<16&&unsigned(h::Reason::JointOrigins)<16);
    return {ScenarioReason::SnapshotJoint,-1,(int(snapshot.status)<<8)|(int(snapshot.assignment)<<4)|int(snapshot.reason),source_round,false};
}
// Separate optional storage preserves all prior Bootstrap/Memory layouts.
inline ScenarioStamp& scenario_state(){static ScenarioStamp value;return value;}
inline bool profile_active(){if constexpr(!config::service_forecast_stage_audit)return true;else return audit_state().enabled;}
inline void reject(Memory& m,Stop reason){m.stop=reason;++m.counters.rejected[static_cast<unsigned>(reason)];}
inline bool pack_cells(int width,int height,std::span<const std::uint8_t> cells,std::vector<std::uint8_t>& out) {
    namespace b=service_bitmaps;const auto shape=b::validate(b::CellView{width,height,cells});
    if(!shape.valid)return false;out.resize(shape.packed_bytes);
    return b::pack_cells(b::CellView{width,height,cells},b::PackedBuffer{width,height,out},{1,1}).state==b::State::Complete;
}
inline unsigned audit_line(std::span<char> line,std::span<const std::uint64_t> values){
    if(values.size()>112||line.size()<32)return 0;unsigned at=0;for(const char* name="wb_service_stage_v1";*name;++name)line[at++]=*name;
    for(const auto value:values){if(at+24>=line.size())return 0;line[at++]=' ';char digits[24];unsigned n=0;auto rest=value;
        do{digits[n++]=char('0'+rest%10);rest/=10;}while(rest);while(n)line[at++]=digits[--n];}
    line[at++]='\n';return at;
}
inline unsigned scenario_line(std::span<char> line,std::span<const std::uint64_t> values){
    const auto size=audit_line(line,values);if(size)line[sizeof("wb_service_stage_v1")-2]='2';return size;
}
struct CaptureDelta {bool known{};unsigned gems{};double gold{};};
struct ReceiptHead {e::Directive directive{};bool refuted{};};
inline ReceiptHead reconcile_head(e::Interpreter& interpreter,const e::Frame& actual,bool model_consistent) {
    auto directive=interpreter.choose(actual);
    if(directive.kind==e::Kind::Complete||directive.kind==e::Kind::Handoff||model_consistent)return {directive,false};
    if(directive.kind==e::Kind::Physical)interpreter.cancel(directive);
    interpreter.invalidate(e::Reason::ChangedPremise);
    directive.kind=e::Kind::Handoff;directive.reason=e::Reason::ChangedPremise;directive.id=0;directive.primitive={};
    return {directive,true};
}
inline CaptureDelta capture_delta(const f::World& initial,const e::Frame& prior,const e::Frame& now,bool income_closed) {
    if(!income_closed||initial.actors!=1||!prior.observed||!now.observed||!now.fresh||!now.body.alive
        ||!now.body.bag_known||!now.body.gold_known||!initial.bodies[0].bag_known||!initial.bodies[0].gold_known
        ||prior.stamp.owner!=initial.bodies[0].id||now.stamp.owner!=initial.bodies[0].id||now.stamp.input!=prior.stamp.input+1||now.stamp.round!=prior.stamp.round+1
        ||prior.group.id!=initial.group.id||now.group.id!=initial.group.id||prior.group.generation!=initial.group.generation||now.group.generation!=initial.group.generation
        ||prior.group.life!=f::Life::Reward||now.group.life!=f::Life::Respawning)return {};
    const auto& start=initial.bodies[0];
    if(now.body.bag<start.bag||now.body.gold<start.gold||(now.body.bag==start.bag&&now.body.gold==start.gold))return {};
    return {true,now.body.bag-start.bag,now.body.gold-start.gold};
}
#ifndef WB_SERVICE_CONTROLLER_NATIVE_HOST_FREE
inline sm::Sample stage_sample();
inline void stage_report(const sm::Report& report);
inline bool scenario_fail(Memory& m,ScenarioReason reason,int actor=-1,int detail=-1){
    if constexpr(config::service_forecast_scenario_audit)scenario_state()={reason,actor,detail,m.source.round,false};
    return false;
}
inline void audit_input_start(){if constexpr(config::service_forecast_stage_audit){auto& audit=audit_state();
    if(!audit.enabled&&audit.final_emitted)return;
    if(audit.recorder.depth()){audit.recorder.interrupt_all(stage_sample(),sm::Exit::Incomplete);stage_report(audit.recorder.last());}
    if(audit.invocation==std::numeric_limits<std::uint64_t>::max())audit.invocation_known=false;else ++audit.invocation;
    audit.enabled=config::service_forecast_stage_through<=0||static_cast<int>(cambc::round_number())<=config::service_forecast_stage_through;
}}
inline sm::Sample stage_sample(){const auto remaining=cambc::fuel_remaining();
    // Unfunded instrumentation stays UNKNOWN and performs no extra clock or
    // identity query. It neither authorizes nor forbids the underlying work.
    if(remaining<=18000)return {{0,1,true},{},remaining,true,false};const auto& owner=service_native::memory();const auto& audit=audit_state();
    // input is an explicitly established AUDIT INVOCATION label, never a
    // cached native/engine input ID. It is valid even before service::begin.
    const auto invocation=audit.invocation,effect=owner.effect_generation;
    const int actor_before=cambc::detail::sdk_my_champion();
    const int round_before=cambc::round_number();const auto fuel=cambc::fuel_remaining();
    const int actor_after=cambc::detail::sdk_my_champion();
    const int round_after=cambc::round_number();
    const bool identity=actor_before==actor_after&&actor_after>=0&&audit.invocation_known;
    const bool stable=identity&&round_before==round_after&&audit.invocation==invocation&&owner.effect_generation==effect;
    return {{0,1,true},{actor_after,round_after,invocation,owner.effect_generation,identity},fuel,stable,stable};}
inline void stage_report(const sm::Report& report){auto& audit=audit_state();
    if(report.closed&&report.before.fresh&&report.after.fresh&&report.before.epoch.known&&report.after.epoch.known&&report.before.epoch.round!=report.after.epoch.round
        &&(audit.before_round!=report.before.epoch.round||audit.after_round!=report.after.epoch.round)){
        audit.last_cross=report.stage;audit.before_round=report.before.epoch.round;audit.after_round=report.after.epoch.round;audit.serial=report.serial;audit.pending=true;}}
template<class Function> decltype(auto) measured(sm::Stage stage,Function&& function){
    if constexpr(!config::service_forecast_stage_audit)return std::forward<Function>(function)();
    else {
        if(!profile_active())return std::forward<Function>(function)();auto& recorder=audit_state().recorder;
        const auto token=recorder.begin(stage,stage_sample());
        try {
            if constexpr(std::is_void_v<std::invoke_result_t<Function>>){std::forward<Function>(function)();stage_report(recorder.finish(token,stage_sample()));return;}
            else {decltype(auto) result=std::forward<Function>(function)();stage_report(recorder.finish(token,stage_sample()));return result;}
        }catch(...){recorder.interrupt_all({},sm::Exit::Threw);throw;}
    }
}
inline bool audited_epoch(n::Epoch source){
    if constexpr(!config::service_forecast_stage_audit)return true;
    else {const auto& after=audit_state().recorder.last().after.epoch;
        return !after.known||(after.owner==source.owner&&after.round==source.round&&after.input==audit_state().invocation&&after.effect==source.effect);}
}
inline void log_audit_numbers(std::span<const std::uint64_t> values){
    // At most112 uint64 fields:112*21+name/newline is below this fixed buffer.
    // This separate audit-only emitter leaves the legacy512-byte path exact.
    char line[4096];const auto size=[&]{if constexpr(config::service_forecast_scenario_audit)return scenario_line(line,values);else return audit_line(line,values);}();if(size)std::cout.write(line,size);
}
inline void log_counters() {
    auto& boot=bootstrap();auto* pointer=memory();const int round=cambc::round_number();
    bool requested=round%128==0;
    if constexpr(config::service_forecast_stage_audit){const auto& audit=audit_state();requested=audit.pending||(audit.enabled?requested:!audit.final_emitted);}
    if(!requested||boot.log_round==round||cambc::fuel_remaining()<75000)return;boot.log_round=round;
    const auto& c=boot.counters;std::array<std::uint64_t,config::service_forecast_stage_audit?128:52> values{};unsigned at=0;
    for(const auto x:{std::uint64_t(unsigned(boot.components.history().source.observer.id)),std::uint64_t(unsigned(round)),
        std::uint64_t(pointer?pointer->stage:Stage::Cold),std::uint64_t(pointer?pointer->stop:boot.stop),
        c.observed,c.histories,c.nominations,c.routes,c.begun,c.quanta,c.complete,c.selected,c.commands,c.accepted,c.reconciled,c.handoffs,c.renewed,
        c.cold_cost,c.capture_cost,c.route_cost,c.begin_cost,c.advance_cost,c.dispatch_cost})values[at++]=x;
    for(const auto x:c.rejected)values[at++]=x;
    for(const auto x:{std::uint64_t(boot.components.completed()),c.cold_measured,c.cold_cross_round,c.cold_component,c.history_status,c.history_reason,
        c.capture_status,c.capture_reason,c.model_status,c.model_reason})values[at++]=x;
    for(const auto x:{c.history_cost,c.history_measured,c.history_cross_round,c.prepare_entries,c.prepare_fuel_max,c.capture_queries,c.capture_entry_max,c.background_units,c.prepare_gate})values[at++]=x;
    if constexpr(config::service_forecast_stage_audit){auto& audit=audit_state();
        for(const auto x:{std::uint64_t(audit.last_cross),std::uint64_t(unsigned(audit.before_round)),std::uint64_t(unsigned(audit.after_round)),audit.serial,std::uint64_t(audit.recorder.depth())})values[at++]=x;
        std::uint64_t known=0;for(unsigned stage=0;stage<sm::stage_count;++stage){const auto* stats=audit.recorder.stats(static_cast<sm::Stage>(stage));
            values[at++]=stats?stats->cross_round:0;values[at++]=stats&&stats->peak_cost.known?stats->peak_cost.value:0;values[at++]=stats?stats->unknown:0;
            if(stats&&stats->peak_cost.known)known|=std::uint64_t(1)<<stage;}values[at++]=known;
        if constexpr(config::service_forecast_scenario_audit)values[at++]=scenario_word(scenario_state());
        audit.pending=false;if(!audit.enabled)audit.final_emitted=true;
        log_audit_numbers({values.data(),at});
    }else en::log_numbers("wb_service_v3",{values.data(),at});
}
inline bool work(void* context,unsigned){const auto& m=*static_cast<const Memory*>(context);
    // Model quanta issue no physical command. The caller checks the complete
    // actual epoch before AND after the bounded batch; querying that epoch
    // several times per pure transition duplicates expensive host work.
    return cambc::fuel_remaining()>m.fallback_obligation;}
inline bool history_work(void*,unsigned){return cambc::fuel_remaining()>18000;}
inline bool initialize_history(History& x,Counters& counters) {
    const auto round=static_cast<int>(cambc::round_number());
    const auto owner=static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()));
    const auto own=cambc::champions(cambc::team::ally),foe=cambc::champions(cambc::team::enemy);
    if(own.empty()||own.size()+foe.size()>6){counters.history_status=unsigned(h::Status::Capacity);counters.history_reason=unsigned(h::Reason::Roster);return false;}
    x.source={1,1,1,{owner,cambc::detail::sdk_my_team(),h::Entity::Champion}};
    x.count=0;for(const auto side:{std::span<const cambc::champion>(own),std::span<const cambc::champion>(foe)})for(const auto& body:side){
        const auto id=static_cast<int>(static_cast<cambc::handle_champion_t>(body));cambc::detail::champion_identity identity{};
        cambc::detail::check(cambc::detail::sdk_champion_identity(id,&identity));
        x.roster[x.count]={id,identity.team,h::Entity::Champion};x.types[x.count++]=identity.type;
        x.source.roster=service_native::mix(x.source.roster,std::uint64_t(unsigned(id+1))<<32|unsigned(identity.type));}
    const int own_team=x.source.observer.team;
    // The SDK already exports ceil(area/8) packed bytes. Route maps use a
    // different, explicitly converted cell-byte representation below.
    x.spawn[own_team]=cambc::map::spawn_region(cambc::team::ally).export_linear_buffer();
    x.spawn[1-own_team]=cambc::map::spawn_region(cambc::team::enemy).export_linear_buffer();
    const std::array<h::Spawn,2> spawns{{{0,x.spawn[0]},{1,x.spawn[1]}}};
    h::Initial initial;initial.source=x.source;initial.game_start={round,0,0};initial.width=cambc::map::width();initial.height=cambc::map::height();
    initial.roster={x.roster.data(),x.count};initial.spawns=spawns;initial.complete_roster=initial.public_spawns_complete=true;
    // We can prove a history only from observations actually retained. Missing
    // early gameplay inputs do not become an initial-spawn observation.
    x.complete_from_first_input=false;
    const auto reset=x.tracker.reset(initial);counters.history_status=unsigned(reset.status);counters.history_reason=unsigned(reset.reason);
    x.ready=reset.status==h::Status::Ready;if(x.ready)++x.epoch;return x.ready;
}
inline void observe_history(bool new_input=true) {
    auto& boot=bootstrap();auto& x=boot.components.history();auto& counters=boot.counters;
    const auto entry_round=cambc::round_number();const auto entry_fuel=cambc::fuel_remaining();
    if(cambc::fuel_remaining()<30000){x.tracker.invalidate();x.ready=x.complete_from_first_input=false;return;}
    if(!x.ready&&!initialize_history(x,counters)){boot.stop=Stop::Observation;++counters.rejected[unsigned(Stop::Observation)];return;}
    if(new_input)++x.sequence;const int round=cambc::round_number();x.stamp={round,x.sequence,service_native::memory().effect_generation};
    x.visible=cambc::vision::visible_tiles().export_linear_buffer();std::array<h::Sample,6> samples{};
    for(unsigned i=0;i<x.count;++i){auto& sample=samples[i];sample.actor=x.roster[i];x.observed[i]={};
        std::int32_t alive=0;cambc::detail::check(cambc::detail::sdk_champion_is_alive(sample.actor.id,&alive));
        sample.facts.life=alive?h::Life::Alive:h::Life::Dead;x.alive[i]=alive!=0;if(!alive)continue;
        cambc::detail::champion_info raw{};const auto code=cambc::detail::sdk_champion(sample.actor.id,&raw);
        if(code==static_cast<int>(cambc::error_code::api_not_in_vision)||code==static_cast<int>(cambc::error_code::api_not_found))continue;
        cambc::detail::check(code);sample.presence=h::Presence::Visible;sample.position={raw.x,raw.y};
        sample.actual_binding=service_native::mix(service_native::mix(x.sequence,unsigned(sample.actor.id+1)),service_native::scalar(raw.health));
        const auto state=cambc::champion_state::from_bits_retain(raw.state);
        sample.facts.channel=(state.contains(cambc::champion_state::submitting_gems)||state.contains(cambc::champion_state::recalling)||state.contains(cambc::champion_state::teleporting))?h::Channel::Active:h::Channel::None;
        // The hypothesis producer closes no auto-effect family merely from a
        // position/kit. The actual capture and relevance producer owns that.
        sample.facts.effects=h::Effects::Unknown;
        x.observed[i]={true,state.contains(cambc::champion_state::completed_turn),state.contains(cambc::champion_state::can_move),
            state.contains(cambc::champion_state::can_attack),state.contains(cambc::champion_state::can_cast),sample.facts.channel==h::Channel::Active,{raw.x,raw.y},raw.health};
    }
    if(static_cast<int>(cambc::round_number())!=round){x.tracker.invalidate();x.ready=false;counters.history_status=unsigned(h::Status::Stale);counters.history_reason=unsigned(h::Reason::Stamp);++counters.history_cross_round;return;}
    const auto updated=x.tracker.observe({x.source,x.stamp,x.visible,{samples.data(),x.count},true,true});
    counters.history_status=unsigned(updated.status);counters.history_reason=unsigned(updated.reason);
    ++counters.observed;if(updated.status==h::Status::Applied||updated.status==h::Status::Duplicate){
        ++counters.histories;if(round==1&&x.sequence==1)x.complete_from_first_input=true;boot.stop=Stop::None;}
    else {x.tracker.invalidate();x.ready=x.complete_from_first_input=false;}
    const auto remaining=cambc::fuel_remaining();
    if(cambc::round_number()!=entry_round||remaining>entry_fuel)++counters.history_cross_round;
    else {++counters.history_measured;counters.history_cost=std::max(counters.history_cost,std::uint64_t(entry_fuel-remaining));}
}
struct ConstructionInput {Bootstrap* boot{};n::Epoch source{};unsigned before{};int round{-1};};
inline ss::Decision construction_before(void* context,ss::Component,std::size_t) {
    auto& input=*static_cast<ConstructionInput*>(context);
    if(!n::current(input.source)||!service_native::owned())return ss::Decision::Stale;
    // This is an empirical admission, separate from the funded rolling service.
    // One real constructor is the only cold operation admitted this input;
    // the native all-phase panel must qualify its actual allocator/DMI tail.
    if(cambc::fuel_remaining()<75000)return ss::Decision::Deferred;
    input.round=cambc::round_number();input.before=cambc::fuel_remaining();return ss::Decision::Allow;
}
inline ss::Decision construction_after(void* context,ss::Component component,std::size_t,ss::Construction) {
    auto& input=*static_cast<ConstructionInput*>(context);auto& counts=input.boot->counters;
    if(static_cast<int>(cambc::round_number())!=input.round){++counts.cold_cross_round;return ss::Decision::Stale;}
    const auto remaining=cambc::fuel_remaining();if(remaining>input.before){++counts.cold_cross_round;return ss::Decision::Stale;}
    ++counts.cold_measured;const auto cost=input.before-remaining;
    if(cost>counts.cold_cost){counts.cold_cost=cost;counts.cold_component=unsigned(component);}
    return n::current(input.source)?ss::Decision::Allow:ss::Decision::Stale;
}
inline bool funded_storage(n::Epoch source) {
    auto& boot=bootstrap();if(boot.facade)return true;
    if(!boot.components.history().ready)return false;
    ConstructionInput input{&boot,source};
    if(!boot.components.ready()){
        const auto result=measured(sm::Stage::Cold,[&]{return boot.components.advance({&input,&construction_before,&construction_after});});
        if(result.status==ss::Status::Deferred){boot.stop=Stop::Funding;++boot.counters.rejected[unsigned(Stop::Funding)];}
        return false; // Even the last real constructor retains its own tail.
    }
    if(construction_before(&input,ss::Component::Count,sizeof(Memory))!=ss::Decision::Allow)return false;
    measured(sm::Stage::Cold,[&]{boot.facade.reset(new Memory(boot.components,boot.counters));});
    construction_after(&input,ss::Component::Count,sizeof(Memory),ss::Construction::Constructed);
    return false; // The small reference facade is a distinct admitted stage.
}
inline bool premises_current(Memory& m) {
    const auto& history=m.history;if(!history.ready||history.epoch!=m.history_epoch)return false;
    const auto snapshot=history.tracker.read(history.source,history.stamp,{&m,&work});if(!snapshot.joint_positions_validated)return false;
    for(unsigned k=0;k<m.captured.world.externals;++k){const auto& original=m.captured.world.others[k];bool found=false;
        for(unsigned i=0;i<snapshot.count;++i)if(snapshot.actors[i].actor.id==original.id){const auto& witness=snapshot.actors[i];const auto& fresh=history.observed[i];found=true;
            if(!witness.point_assigned||witness.geometry!=h::Geometry::Supported||witness.point.x!=original.pose.x||witness.point.y!=original.pose.y
                ||witness.current.life!=h::Life::Alive||witness.class_reason==h::ClassReason::ActiveChannel||witness.class_reason==h::ClassReason::UnsupportedEffects)return false;
            if(fresh.visible){if(fresh.channel||(original.actual&&fresh.hp!=original.hp))return false;
                if(history.stamp.round>m.source.round&&(fresh.completed||history.stamp.round>m.source.round+1)&&(!fresh.m||!fresh.a||!fresh.c))return false;}
        }
        if(!found)return false;
    }
    return true;
}
inline bool query_placement_world(Memory& m) {
    auto& c=m.captured;auto& w=c.world;auto& history=m.history;
    if(!history.ready)return scenario_fail(m,ScenarioReason::HistoryNotReady);
    const auto snapshot=history.tracker.read(history.source,history.stamp,{&m,&work});
    if(!snapshot.joint_positions_validated||snapshot.assignment!=h::Assignment::Complete){
        if constexpr(config::service_forecast_scenario_audit){const auto fault=scenario_snapshot_fault(snapshot,m.source.round);return scenario_fail(m,fault.reason,fault.actor,fault.detail);}
        else return false;
    }
    for(unsigned i=0;i<snapshot.count;++i){const auto& witness=snapshot.actors[i];if(witness.actor.id==c.source.owner)continue;
        if constexpr(config::service_forecast_scenario_audit){
            const auto fault=scenario_witness_fault(witness,history.complete_from_first_input,m.source.round);
            if(fault.reason!=ScenarioReason::NotSampled)return scenario_fail(m,fault.reason,fault.actor,fault.detail);
        }else{
            if(!witness.point_assigned||witness.geometry!=h::Geometry::Supported||witness.current.life!=h::Life::Alive
                ||(witness.origin_is_public_spawn&&!history.complete_from_first_input))return false;
            if(witness.class_reason==h::ClassReason::Dead||witness.class_reason==h::ClassReason::ActiveChannel||witness.class_reason==h::ClassReason::UnsupportedEffects)return false;
        }
        // A current, fully captured visible actor keeps its ACTUAL point and
        // stats. A hidden fixed-origin hypothesis never acquires actual HP.
        f::External* body=nullptr;for(unsigned k=0;k<w.externals;++k)if(w.others[k].id==witness.actor.id)body=&w.others[k];
        if(body){if(!(body->pose==f::Point{witness.point.x,witness.point.y}))return scenario_fail(m,ScenarioReason::VisiblePoseMismatch,witness.actor.id);continue;}
        if(w.externals==f::external_limit)return scenario_fail(m,ScenarioReason::ExternalCapacity,witness.actor.id);auto& hypothesized=w.others[w.externals++];hypothesized={};
        hypothesized.id=witness.actor.id;hypothesized.team=witness.actor.team;hypothesized.pose={witness.point.x,witness.point.y};hypothesized.alive=true;
        hypothesized.actual=false;hypothesized.conditional_placement=true;hypothesized.placement_revision=service_native::mix(snapshot.generation,unsigned(witness.actor.id+1));
    }
    if(w.actors+w.externals!=history.count)return scenario_fail(m,ScenarioReason::RosterSize,-1,int(w.actors+w.externals));
    if constexpr(config::service_forecast_scenario_audit)scenario_state()={ScenarioReason::PlacementReady,-1,-1,m.source.round,false};
    return true;
}
inline bool closed_hold_relevance(Memory& m) {
    auto& w=m.captured.world;
    for(unsigned i=0;i<w.externals;++i){auto& x=w.others[i];
        // Known active channel/retaliation/effect faults cannot be replaced by
        // a no-op. The first class simply excludes every external from ALL
        // static NPC influence. The simulator also checks candidate Varg
        // collateral and route occupancy before producing a Complete result.
        if(x.actual){const auto& faults=m.captured.external_coverage[i];
            if constexpr(config::service_forecast_scenario_audit){
                const auto fault=scenario_external_fault(x.id,true,faults.faults,m.source.round);
                if(fault.reason!=ScenarioReason::NotSampled)return scenario_fail(m,fault.reason,fault.actor,fault.detail);
            }else if(faults.faults&(en::Channel|en::Restrictions|en::UnknownPassive))return false;
        }
        if(x.pose.x<0||x.pose.y<0||x.pose.x>=m.map.width||x.pose.y>=m.map.height)return scenario_fail(m,ScenarioReason::ExternalPoint,x.id);
        if(m.foreign[unsigned(x.pose.y*m.map.width+x.pose.x)])return scenario_fail(m,ScenarioReason::ForeignNpcRange,x.id);
        for(unsigned n=0;n<w.npcs;++n)if(w.camps[n].strength>0&&f::distance(x.pose,w.camps[n].pose)<=int(w.camps[n].range))
            return scenario_fail(m,ScenarioReason::SelectedNpcRange,x.id,w.camps[n].id);
        x.npc_effects_closed=x.actual; // A hidden hypothesis can never take modeled damage.
    }
    if constexpr(config::service_forecast_scenario_audit)scenario_state()={ScenarioReason::Complete,-1,-1,m.source.round,false};
    return true;
}
inline int draft_type(cambc::champion_draft pick) {
    const auto value=static_cast<int>(pick);
    if(pick==cambc::champion_draft::cassi_and_issac)return -1; // multiple bodies need their actual tie contract
    return value<static_cast<int>(cambc::champion_draft::cassi_and_issac)?value:value+1;
}
inline bool conditional_order(Memory& m) {
    auto& w=m.captured.world;const auto own=cambc::draft::current_draft(cambc::team::ally),foe=cambc::draft::current_draft(cambc::team::enemy);
    unsigned own_count=0,foe_count=0;for(unsigned k=0;k<m.history.count;++k)(m.history.roster[k].team==w.team?own_count:foe_count)++;
    if(own.size()!=own_count||foe.size()!=foe_count||own_count+foe_count>6)return false;
    for(unsigned k=0;k<m.history.count;++k){auto& p=m.participants[k];const auto actor=m.history.roster[k];p={};p.id=actor.id;p.team=actor.team;
        const auto& picks=actor.team==w.team?own:foe;int rank=-1;
        for(unsigned j=0;j<picks.size();++j)if(draft_type(picks[j])==m.history.types[k])rank=int(j);
        if(rank<0)return false;p.draft_rank=rank;
        std::int32_t current_priority=0,next_priority=0;
        const auto code=cambc::detail::sdk_bc_champion_priority(actor.id,0,&current_priority);
        if(code&&code!=static_cast<int>(cambc::error_code::api_not_in_vision))cambc::detail::check(code);
        const auto next_code=cambc::detail::sdk_bc_champion_priority(actor.id,1,&next_priority);
        if(next_code&&next_code!=static_cast<int>(cambc::error_code::api_not_in_vision))cambc::detail::check(next_code);
        // Unknown priority0 is a NAMED conditional schedule hypothesis, not an
        // observed value or a foreign commitment. Relevant ties still need
        // the full same-world outcome, and deployment rechecks actual epochs.
        p.current_priority=code?0:current_priority;p.next_priority=next_code?p.current_priority:next_priority;
    }
    std::array<unsigned,6> current{},next{};for(unsigned k=0;k<m.history.count;++k)current[k]=next[k]=k;
    const auto less=[&](unsigned a,unsigned b,bool future){const auto& x=m.participants[a];const auto& y=m.participants[b];
        if(x.team!=y.team)return x.team<y.team;const auto xp=future?x.next_priority:x.current_priority,yp=future?y.next_priority:y.current_priority;
        return xp!=yp?xp>yp:x.draft_rank<y.draft_rank;};
    std::sort(current.begin(),current.begin()+m.history.count,[&](auto a,auto b){return less(a,b,false);});
    std::sort(next.begin(),next.begin()+m.history.count,[&](auto a,auto b){return less(a,b,true);});
    for(unsigned i=0;i<m.history.count;++i)if(current[i]!=next[i])return false;
    w.slots=m.history.count;
    for(unsigned parity=0;parity<2;++parity){auto& slots=parity?w.odd:w.even;std::array<unsigned,6> sides[2];unsigned count[2]{},cursor[2]{},used=0;
        for(unsigned k=0;k<m.history.count;++k){const auto at=current[k];sides[m.participants[at].team][count[m.participants[at].team]++]=at;}
        const int starter=parity?0:1; // queried bundle's documented A-odd/B-even schedule
        while(used<w.slots)for(unsigned k=0;k<2;++k){const int side=starter^int(k);if(cursor[side]==count[side])continue;
            const auto& p=m.participants[sides[side][cursor[side]++]];slots[used++]={p.id==m.source.owner,0,p.id==m.source.owner?-1:p.id};}
    }
    const auto& slots=w.round%2?w.odd:w.even;bool found=false;
    for(unsigned k=0;k<w.slots;++k)if(slots[k].controlled){w.slot=k;found=true;}
    return found;
}
inline bool purchase_intent(Memory& m,cambc::item_type item) {
    auto& b=m.captured.world.bodies[0];const auto props=item.info();const auto index=static_cast<unsigned>(item);
    if(index>=f::item_limit||!props.purchasable||!props.cost||props.passive||!props.combine_count||b.items[index]+1>=props.combine_count)return false;
    if(b.gold+m.captured.world.group.gold<props.cost)return false;
    const auto* design=en::design(m.history.types[0]);
    for(unsigned k=0;k<m.history.count;++k)if(m.history.roster[k].id==m.source.owner)design=en::design(m.history.types[k]);
    if(!design)return false;auto& intent=m.purchases[0];intent={index,1,service_native::mix(m.captured.world.revision,index+1),double(props.cost)};
    n::PurchaseTransition row;row.actor=b.id;row.intent=intent;row.before=b.items;auto& q=row.effect;
    q.complete=true;q.revision=intent.quote_revision;q.cost=props.cost;q.items=b.items;++q.items[index];
    q.health_growth=f::fp_mul(float(props.health_points),design->base_stats.health_growth);
    q.strength_growth=f::fp_mul(float(props.strength_points),design->base_stats.strength_growth);
    q.focus_growth=f::fp_mul(float(props.focus_points),design->base_stats.focus_growth);
    q.camp_multiplier=b.camp_multiplier;q.taken_multiplier=b.taken_multiplier;q.npc_taken_multiplier=b.npc_taken_multiplier;q.gold_multiplier=b.gold_multiplier;
    q.effective_recall_ticks=b.effective_recall_ticks;m.model.purchases.clear();m.model.purchases.push_back(row);return true;
}
inline bool begin_candidate(Memory& m,n::Epoch source,int group,const cambc::map::camp_group& native,
                            const g::Product& geometry,std::span<const cambc::vec2> altars,cambc::item_type investment,bool reuse_geometry=false) {
    const auto entry_fuel=cambc::fuel_remaining();m.counters.capture_entry_max=std::max(m.counters.capture_entry_max,std::uint64_t(entry_fuel));
    if(!n::current(source)||entry_fuel<75000){reject(m,Stop::Funding);return false;}
    m.source=m.funding_epoch=source;m.group=group;m.stop=Stop::None;m.assumptions=service_native::mix(service_native::mix(source.input,source.effect),0x484f4c44u);m.history_epoch=m.history.epoch;
    if constexpr(config::service_forecast_scenario_audit)scenario_state()={ScenarioReason::NotReached,-1,-1,source.round,false};
    m.captured.reserve_floor=m.fallback_obligation;
    const auto horizon=std::min(source.round+512,int(cambc::constants::max_rounds)-1);
    std::array<int,6> roster{};for(unsigned k=0;k<m.history.count;++k)roster[k]=m.history.roster[k].id;
    const auto before=cambc::fuel_remaining();++m.counters.capture_queries;
    const auto status=measured(sm::Stage::Capture,[&]{return n::capture(m.captured,source,native,m.source_generation,{roster.data(),m.history.count},horizon);});
    m.counters.capture_status=unsigned(status);m.counters.capture_reason=unsigned(m.captured.reason);
    m.counters.capture_cost=std::max(m.counters.capture_cost,before-cambc::fuel_remaining());
    if(!audited_epoch(source)){reject(m,Stop::Stale);return false;}
    if(status!=n::Status::FactsReady&&!(status==n::Status::NeedModel&&m.captured.reason==n::Reason::ExternalUnknown&&m.captured.local_facts_complete)){
        reject(m,status==n::Status::NeedFuel?Stop::Funding:Stop::Observation);return false;}
    const int width=cambc::map::width(),height=cambc::map::height();const unsigned cells=unsigned(width*height);
    namespace b=service_bitmaps;
    const g::Source geometry_source{1,m.geometry_catalogue_revision,group};
    if(!m.geometry.current(geometry,geometry_source)){reject(m,Stop::Geometry);return false;}
    const auto blocked=geometry.blocked,own_spawn=geometry.own_spawn,foreign=geometry.foreign;
    for(const auto mask:{blocked,own_spawn,foreign})if(mask.width!=width||mask.height!=height||!b::validate(mask).valid){reject(m,Stop::Geometry);return false;}
    for(const auto mask:{geometry.blocked_cells,geometry.own_spawn_cells,geometry.foreign_cells})
        if(mask.width!=width||mask.height!=height||!b::validate(mask).valid||mask.bytes.size()!=cells){reject(m,Stop::Geometry);return false;}
    // The complete pure preparer already converted both representations. Own
    // these used-prefix copies so subsequent background work cannot mutate a
    // retained route/simulator through a borrowed geometry buffer.
    measured(sm::Stage::GeometryCopy,[&]{m.blocked.assign(geometry.blocked_cells.bytes.begin(),geometry.blocked_cells.bytes.end());
        m.spawn.assign(geometry.own_spawn_cells.bytes.begin(),geometry.own_spawn_cells.bytes.end());
        m.foreign.assign(geometry.foreign_cells.bytes.begin(),geometry.foreign_cells.bytes.end());});
    if(!audited_epoch(source)){reject(m,Stop::Stale);return false;}
    m.map_revision=1;m.foreign_revision=service_native::mix(group+1,m.source_generation);m.altars.clear();
    measured(sm::Stage::Altar,[&]{const auto locks=cambc::vision::altar_lock_tiles(cambc::team::enemy),vision=cambc::vision::visible_tiles();
        m.model.altars.clear();for(const auto p:altars){const bool seen=vision.is_set(p),locked=locks.is_set(p);
            m.altars.push_back({{p.x,p.y},locked?r::Lock::Locked:seen?r::Lock::Unlocked:r::Lock::Unknown});
            m.model.altars.push_back({{p.x,p.y},seen,!locked,!seen,m.assumptions});}});
    if(!audited_epoch(source)){reject(m,Stop::Stale);return false;}
    m.map={width,height,m.map_revision,1,m.foreign_revision,m.blocked,m.spawn,m.foreign,true,true,m.altars,source.round,source.input};
    const auto hypothesis=measured(sm::Stage::Hypothesis,[&]{return query_placement_world(m)&&closed_hold_relevance(m);});
    if(!audited_epoch(source)){reject(m,Stop::Stale);return false;}if(!hypothesis){reject(m,Stop::Scenario);return false;}
    const auto order=measured(sm::Stage::Order,[&]{return conditional_order(m);});
    if(!audited_epoch(source)){reject(m,Stop::Stale);return false;}if(!order){reject(m,Stop::Order);return false;}
    auto& w=m.captured.world;
    m.model.scope=n::Scope::ClosedConditionalModel;m.model.initial_world=w.revision;m.model.static_map=m.map_revision;
    m.model.reply=service_native::mix(m.assumptions,1);m.model.assumptions=m.assumptions;m.model.width=width;m.model.height=height;
    m.model.first_round=source.round;m.model.horizon=w.horizon;m.model.submit_range=cambc::constants::submit_gems_range;
    m.model.terrain.assign(blocked.bytes.begin(),blocked.bytes.end());m.model.spawn.assign(own_spawn.bytes.begin(),own_spawn.bytes.end());
    m.model.foreign_npc_exposure.assign(foreign.bytes.begin(),foreign.bytes.end());
    measured(sm::Stage::Altar,[&]{m.model.altar_tiles=cambc::map::altars().export_linear_buffer();m.model.views.clear();});
    if(!audited_epoch(source)){reject(m,Stop::Stale);return false;}
    m.model.foreign_source_mask_complete=m.model.all_relevant_occupants_represented=m.model.fixed_external_hold=m.model.no_pending_external_effects=m.model.no_new_altar_locks=true;
    m.model.work_context=&m;m.model.work_permit=&work;
    const auto capital=measured(sm::Stage::Investment,[&]{return purchase_intent(m,investment);});
    if(!audited_epoch(source)){reject(m,Stop::Stale);return false;}if(!capital){reject(m,Stop::Model);return false;}
    // A geometry-only projection deliberately omits external actors. No value
    // or physics result is computed on it; full-world Simulator admission below
    // reinstates every named external and the explicit conditional schedule.
    measured(sm::Stage::GeometryCopy,[&]{m.route_world=w;m.route_world.externals=0;m.route_world.slots=1;m.route_world.slot=0;
        m.route_world.even[0]=m.route_world.odd[0]={true,0,-1};m.participants[0]={w.bodies[0].id,w.team,0,0,0,true,true,true};
        m.request={};m.request.world=&m.route_world;m.request.source={m.map_revision,1,m.foreign_revision,unsigned(group+1),w.revision,source.input,source.effect,source.owner,source.round};
        m.request.family=r::Family::Solo;m.request.order={{m.participants.data(),1},0,source.owner,0,true,true};
        m.request.intent[0].shield=w.bodies[0].kit==f::Kit::Varg;m.request.intent[0].reset=w.bodies[0].kit!=f::Kit::Willow;
        m.request.intent[0].purchases=m.purchases;m.request.varg_splash_known=w.bodies[0].splash_radius_known;
        m.request.varg_splash_radius=unsigned(w.bodies[0].splash_radius);});
    if(!audited_epoch(source)){reject(m,Stop::Stale);return false;}
    m.rebinding=reuse_geometry&&m.builder.result().state==r::State::Ready
        &&measured(sm::Stage::Rebind,[&]{return m.rebinder.begin(m.builder.result(),m.map,m.request,m.rebound);})!=r::State::Unknown;
    if(reuse_geometry&&!audited_epoch(source)){reject(m,Stop::Stale);return false;}
    m.active_program=m.rebinding?&m.rebound:&m.program;
    if(!m.rebinding&&measured(sm::Stage::RouteBegin,[&]{return m.builder.begin(m.map,m.request,m.program,m.workspace);})==r::State::Unknown){reject(m,Stop::Geometry);return false;}
    if(!audited_epoch(source)){reject(m,Stop::Stale);return false;}
    m.stage=Stage::Routes;m.view=0;m.discovery=true;++m.counters.nominations;return true;
}
inline Stage advance(Memory& m,n::Epoch funded_input,unsigned units=64) {
    // Immutable historical snapshots may finish their bounded computation, but
    // NEVER become current command authority merely because computation ended.
    m.funding_epoch=funded_input;
    if(funded_input.owner!=m.source.owner||!n::current(funded_input)){reject(m,Stop::Stale);return m.stage;}
    if(m.stage==Stage::Routes){const auto before=cambc::fuel_remaining();const auto state=measured(sm::Stage::RouteAdvance,[&]{return m.rebinding?m.rebinder.advance(m.request.source,units,{&m,&work}):m.builder.advance(m.request.source,units,{&m,&work});});
        m.counters.route_cost=std::max(m.counters.route_cost,before-cambc::fuel_remaining());
        if(!audited_epoch(funded_input)){reject(m,Stop::Stale);m.stage=Stage::Idle;return m.stage;}
        if(state==r::State::Unknown){reject(m,Stop::Geometry);m.stage=Stage::Idle;return m.stage;}
        if(state!=r::State::Ready)return m.stage;++m.counters.routes;m.stage=Stage::Views;}
    if(m.stage==Stage::Views){if(!work(&m,1))return m.stage;const auto leg=m.active_program->bodies[0].legs[0];if(leg<0)return m.stage=Stage::Idle;
        const auto& route=m.active_program->legs[unsigned(leg)];const auto station=route.count?m.active_program->route[route.begin+route.count-1].destination:route.origin;
        const auto linear=measured(sm::Stage::Vision,[&]{return cambc::self().visible_tiles_from({station.x,station.y}).export_linear_buffer();});
        if(!audited_epoch(funded_input)){reject(m,Stop::Stale);return m.stage=Stage::Idle;}
        if(!service_bitmaps::validate(service_bitmaps::PackedView{m.map.width,m.map.height,linear}).valid){reject(m,Stop::Geometry);return m.stage=Stage::Idle;}
        m.model.views.push_back({m.source.owner,station,m.captured.world.bodies[0].kit,linear,true});
        const auto before=cambc::fuel_remaining();const f::Domain domain{m.captured.world.revision,m.map_revision,m.model.reply,1,m.assumptions,true,true,true,true,true,false};
        if(!measured(sm::Stage::SimBegin,[&]{return m.simulator.begin(m.captured.world,*m.active_program,domain,n::callbacks(m.model));})){reject(m,Stop::Model);return m.stage=Stage::Idle;}
        if(!audited_epoch(funded_input)){reject(m,Stop::Stale);return m.stage=Stage::Idle;}
        m.counters.begin_cost=std::max(m.counters.begin_cost,before-cambc::fuel_remaining());++m.counters.begun;m.stage=Stage::Forecast;}
    if(m.stage==Stage::Forecast){const auto before=cambc::fuel_remaining();measured(sm::Stage::SimAdvance,[&]{m.simulator.advance(units);});m.counters.quanta+=m.simulator.result().last_call_quanta;
        m.counters.advance_cost=std::max(m.counters.advance_cost,before-cambc::fuel_remaining());
        if(!audited_epoch(funded_input)){reject(m,Stop::Stale);return m.stage=Stage::Idle;}
        const auto& result=m.simulator.result();m.counters.model_status=unsigned(result.status);m.counters.model_reason=unsigned(result.reason);if(result.status==f::Status::Building)return m.stage;
        if(m.discovery&&result.bodies[0].renewal_hit_round>=m.source.round){
            m.discovery=false;m.captured.world.horizon=result.bodies[0].renewal_hit_round;m.model.horizon=m.captured.world.horizon;
            m.active_program->bodies[0].post_renewal=f::PostRenewal::ConditionalHold;const auto domain=result.domain;
            if(!measured(sm::Stage::SimBegin,[&]{return m.simulator.begin(m.captured.world,*m.active_program,domain,n::callbacks(m.model));})){reject(m,Stop::Model);return m.stage=Stage::Idle;}
            if(!audited_epoch(funded_input)){reject(m,Stop::Stale);return m.stage=Stage::Idle;}++m.counters.begun;return m.stage;}
        if(result.status==f::Status::Complete){++m.counters.complete;m.stage=Stage::Ready;}
        else {reject(m,Stop::Incomplete);m.stage=Stage::Idle;}}
    if(!n::current(funded_input)){reject(m,Stop::Stale);m.stage=Stage::Idle;}
    return m.stage;
}
inline bool sample_actual(Memory& m,n::Epoch source,const cambc::map::camp_group& group,const cambc::bitboard& spawn) {
    if(!n::current(source)||cambc::fuel_remaining()<35000)return false;
    auto& sampled=m.actual_capture;sampled.source=source;sampled.reserve_floor=18000;
    if(!n::own_body(sampled,false)||!n::current(source))return false;
    e::Frame x;x.body=sampled.world.bodies[0];auto& owner=service_native::memory();
    x.stamp={source.owner,source.round,source.input,++owner.sample,source.effect};
    if(m.physical_input!=source.input){m.physical_input=source.input;m.walked=false;}
    x.body.walked=m.walked;x.body.channel_start_bag=m.bank_bag;x.body.channel_target=m.bank_altar;
    x.group=m.captured.world.group;x.group.life=f::Life::Unknown;x.group.roster_complete=false;
    x.observed=x.fresh=x.permissions_known=true;x.in_group_known=x.on_spawn_known=true;
    x.on_spawn=spawn.is_set({x.body.pose.x,x.body.pose.y});
    for(const auto& member:group.camps())if(member.spawn_position()==cambc::vec2{x.body.pose.x,x.body.pose.y})x.in_group=true;
    const auto visible=cambc::vision::visible_tiles();bool any=false,all=true;
    for(const auto& member:group.camps()){const bool seen=visible.is_set(member.spawn_position());any=any||seen;all=all&&seen;}
    if(any){const auto life=group.state();
        x.group.life=std::holds_alternative<cambc::map::camp_group_alive>(life)?f::Life::Alive:
            std::holds_alternative<cambc::map::camp_group_reward_available>(life)?f::Life::Reward:f::Life::Respawning;
        if(m.capture_attributed&&x.group.life==f::Life::Alive){
            const auto age=source.round-m.payout_round;
            x.group.generation=age>=0&&unsigned(age)<2*m.captured.world.rules.respawn_delay?m.source_generation+1:0;
        }
        if(all||x.group.life!=f::Life::Alive){x.npcs=m.captured.world.npcs;x.roster_complete=x.group.roster_complete=true;
            for(unsigned k=0;k<x.npcs;++k){auto& camp=x.camps[k];camp=m.captured.world.camps[k];camp.actual=true;camp.shield=0;camp.shield_present=false;
                if(x.group.life!=f::Life::Alive){camp.hp=0;camp.awake=false;continue;}
                const cambc::camp* actual=nullptr;for(const auto& member:group.camps())if(static_cast<int>(static_cast<cambc::handle_camp_t>(member))==camp.id)actual=&member;
                if(!actual)return false;std::uint32_t state=0;cambc::detail::check(cambc::detail::sdk_camp_state(camp.id,&state));
                if(!state){camp.hp=0;camp.awake=false;continue;}
                if(state!=1&&state!=2)return false;camp.hp=actual->health();camp.strength=actual->strength();camp.range=actual->attack_range();camp.awake=state==2;
                if(!actual->buffs().is_empty())return false;
            }
        }
    }
    if(!n::current(source))return false;
    if(m.prior.observed&&source.input!=m.prior.stamp.input&&(source.input!=m.prior.stamp.input+1||source.round!=m.prior.stamp.round+1))m.custody_closed=m.income_closed=false;
    if(!m.history.ready||m.history.stamp.round!=source.round)m.income_closed=false;
    else for(unsigned k=0;k<m.history.count;++k)if(!m.history.alive[k])m.income_closed=false;
    m.receipt_count=0;
    const auto receipt=[&](e::ReceiptKind kind){auto& out=m.receipts[m.receipt_count++];out={};out.event=++m.receipt_event;
        out.program=m.active_program->id;out.observed=x.stamp;out.kind=kind;out.group=m.captured.world.group.id;out.generation=m.source_generation;
        out.evidence=e::Evidence::SupportedConservation;out.source_known=m.custody_closed;return &out;};
    const auto delta=capture_delta(m.captured.world,m.prior,x,m.custody_closed&&m.income_closed);
    if(!m.capture_attributed&&delta.known){
        auto* out=receipt(e::ReceiptKind::GroupPayout);out->amount_known=true;out->gems=delta.gems;
        out->gold=delta.gold;m.capture_attributed=true;m.payout_round=source.round;
    }
    if(m.custody_closed&&m.bank_was_active&&!m.bank_attributed&&x.body.channel==f::Channel::None&&x.body.bag==0&&x.body.alive
        &&source.round>=m.bank_due_round&&cambc::global_submitted_gems(cambc::team::ally)>=m.bank_start_score+m.bank_bag){
        auto* out=receipt(e::ReceiptKind::BankPayout);out->amount_known=true;out->gems=out->source_bag=m.bank_bag;out->source_directive=m.bank_directive;
        m.bank_attributed=true;m.bank_was_active=false;
    }
    x.receipts={m.receipts.data(),m.receipt_count};m.actual=x;return n::current(source);
}
#endif
inline void remember_actual(Memory& m){m.prior=m.actual;m.prior.receipts={};}
inline bool npc_only_prefix_not_refuted(const Memory& m) {
    const auto& now=m.actual;const auto& before=m.prior;
    // No supported NPC operation in this first class applies stun. A newly
    // observed stun refutes the declared no-control Hold world immediately.
    if(now.body.stun_present)return false;
    if(!before.observed)return true;
    if(now.stamp.input==before.stamp.input)return now.body.hp>=before.body.hp;
    if(now.stamp.round!=before.stamp.round+1)return false;
    float lower=before.body.hp,shield=before.body.shield_ticks?before.body.shield:0.0f;
    if(before.on_spawn_known&&before.on_spawn)lower=std::min(before.body.max_hp,f::fp_add(lower,f::fp_mul(before.body.max_hp,m.captured.world.rules.spawn_healing)));
    // All static selected sources, even dead/asleep ones, form a conservative
    // ONE-batch upper cost. Other source footprints are excluded by the actual
    // route/word producer. This is a refutation test, not a utility forecast or
    // a certificate that an undetected enemy reply did not happen.
    for(unsigned k=0;k<m.captured.world.npcs;++k){const auto& source=m.captured.world.camps[k];
        if(source.strength<=0||f::distance(before.body.pose,source.pose)>int(source.range))continue;
        const float amount=f::fp_mul(source.strength,before.body.npc_taken_multiplier),absorbed=std::min(shield,amount);
        shield=f::fp_sub(shield,absorbed);lower=std::max(0.0f,f::fp_sub(lower,f::fp_sub(amount,absorbed)));}
    return now.body.hp>=lower;
}
#ifndef WB_SERVICE_CONTROLLER_NATIVE_HOST_FREE
inline bool splash_has_no_champion(Memory& m,const e::Directive& directive) {
    const auto& body=m.actual.body;if(body.kit!=f::Kit::Varg||directive.primitive.action!=f::Action::Basic)return true;
    if(!body.splash_radius_known||body.splash_radius<0||body.splash_radius>100)return false;
    const f::Npc* target=nullptr;for(unsigned k=0;k<m.actual.npcs;++k)if(m.actual.camps[k].id==directive.primitive.target)target=&m.actual.camps[k];
    if(!target)return false;const auto visible=cambc::vision::visible_tiles(),foes=cambc::vision::champion_tiles(cambc::team::enemy);const int radius=int(std::ceil(body.splash_radius));
    for(int y=std::max(0,target->pose.y-radius);y<std::min(m.map.height,target->pose.y+radius+1);++y)
        for(int x=std::max(0,target->pose.x-radius);x<std::min(m.map.width,target->pose.x+radius+1);++x)
            if(!visible.is_set({x,y})||foes.is_set({x,y}))return false;
    return n::current(m.actual_capture.source);
}
#endif
} // namespace wb::capital::service_controller_native
