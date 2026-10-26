#pragma once
// Test-only lifecycle/trace adapter. No feature reconstruction, free windows or production policy.
#include <cstddef>

namespace zero_lifecycle {
struct Control {
    char magic[16];
    std::int32_t enabled, phase, installed, last_init, decisions, primitive_accepted, failed, last_status, trace_enabled;
    std::int32_t draft_mode, draft_raw_pick, draft_index, draft_target, draft_done;
    std::int32_t mode, priority_done, normal_defaults;
    std::uint64_t root_bound, known, dispatch_known, refresh_known;
    std::uint64_t arguments[15], dispatch[15], refresh[15];
};
struct Init { std::int32_t stage, round; std::uint64_t before, after; };
struct Choice {
    std::int32_t status, type, kind, target, count, x, y, round, command_index;
    std::uint32_t rule, admitted, used;
    std::uint64_t fuel[13];
    std::uint64_t context_build,context_attention,context_destroy;
    std::uint32_t context_entities;
};
struct Dispatch {
    std::int32_t type, kind, target, count, x, y, receipt, round, command_index;
    std::uint64_t before, after;
};
struct Trace {
    std::int32_t stage, head, command, count, label, natural, forced, status;
    std::uint32_t rule, admitted, used, state, rows, scores, stride, kind_offset, target_offset, count_offset, x_offset, y_offset, phi_offset;
    float scalar;
};
struct PriorityParity {
    std::uint32_t local,reference,local_words,reference_words,fact_words,history_words;
    std::int32_t channel,remaining,priority,command_index;
    std::uint64_t previous_revision,local_revision,full_revision,local_fuel,full_fuel;
};
inline constexpr char magic[16] = {'z','e','r','o','.','l','i','f','e','c','y','c','l','e',0,0};
}

#ifdef ZERO_TEACHER_WASM
#if defined(ZERO_STUDENT_PROBE)
#include "../src/core/student_probe.h"
extern "C" __attribute__((import_module("env"), import_name("zero_student_probe")))
void zero_student_probe(std::int32_t, const void*, std::uint32_t);
volatile zero_lifecycle::Control lifecycle_control{{'z','e','r','o','.','l','i','f','e','c','y','c','l','e',0,0}};

namespace zero::student::probe {
void trace(void*, const DecisionTrace&);
int draft_pick(const float* state,const CandSet& rows,const float* scores,int natural)
{
    if(!lifecycle_control.draft_mode)return natural;
    int selected=-1;for(int i=0;i<rows.n;++i)if(rows.c[i].target==lifecycle_control.draft_raw_pick){selected=i;break;}
    lifecycle_control.draft_index=selected;
    if(selected<0){lifecycle_control.failed=1;return -1;}
    DecisionTrace event;event.stage=TraceStage::argument;event.head=h_draft;event.command=static_cast<ActionType>(-1);
    event.state=state;event.rows=std::span<const Cand>(rows.c,rows.n);event.scores=std::span<const float>(scores,rows.n);
    event.label=selected;event.natural_label=natural;event.forced=true;event.decision_status=DecisionStatus::command;
    trace(nullptr,event);return selected;
}
void drafted(int index,int target)
{
    if(!lifecycle_control.draft_mode)return;
    lifecycle_control.draft_target=target;lifecycle_control.draft_done=1;
    if(index!=lifecycle_control.draft_index||target!=lifecycle_control.draft_raw_pick)lifecycle_control.failed=1;
    const std::int32_t wire[]={index,target};zero_student_probe(6,wire,sizeof wire);
}
void trace(void*, const DecisionTrace& event)
{
    if (!lifecycle_control.trace_enabled) return;
    zero_lifecycle::Trace wire{};
    wire.stage=static_cast<int>(event.stage);wire.head=event.head;wire.command=lifecycle_control.draft_mode?-1:event.command;
    wire.count=static_cast<int>(event.rows.size());wire.label=event.label;wire.natural=event.natural_label;
    wire.forced=event.forced;wire.status=static_cast<int>(event.decision_status);
    wire.rule=event.rule_mask;wire.admitted=event.admitted_mask;wire.used=event.used_mask;
    wire.state=reinterpret_cast<std::uintptr_t>(event.state);wire.rows=reinterpret_cast<std::uintptr_t>(event.rows.data());
    wire.scores=reinterpret_cast<std::uintptr_t>(event.scores.data());wire.stride=sizeof(Cand);
    wire.kind_offset=offsetof(Cand,kind);wire.target_offset=offsetof(Cand,target);wire.count_offset=offsetof(Cand,count);
    wire.x_offset=offsetof(Cand,at)+offsetof(Tile,x);wire.y_offset=offsetof(Cand,at)+offsetof(Tile,y);wire.phi_offset=offsetof(Cand,phi);
    wire.scalar=event.predicted_scalar;
    zero_student_probe(lifecycle_control.draft_mode?5:4,&wire,sizeof wire);
}
void install_profile(FuelBounds& bounds,std::uint64_t& root,std::uint64_t* dispatch,
                     std::uint32_t& dispatch_known,std::uint64_t* refresh,std::uint32_t& refresh_known)
{
    if (!lifecycle_control.enabled || lifecycle_control.installed) return;
    if(lifecycle_control.normal_defaults){lifecycle_control.installed=2;return;}
    bounds.known_mask=static_cast<std::uint32_t>(lifecycle_control.known);
    dispatch_known=static_cast<std::uint32_t>(lifecycle_control.dispatch_known);
    refresh_known=static_cast<std::uint32_t>(lifecycle_control.refresh_known);
    root=lifecycle_control.root_bound;
    for(int i=0;i<a_count;++i){bounds.argument[i]=lifecycle_control.arguments[i];dispatch[i]=lifecycle_control.dispatch[i];refresh[i]=lifecycle_control.refresh[i];}
    lifecycle_control.installed=1;
}
void configure_options(const Context&,DecisionOptions& options)
{
    if(lifecycle_control.enabled&&lifecycle_control.trace_enabled)options.trace=trace;
}
const TeacherSource* source(const Context&)
{
    if(!lifecycle_control.enabled)return nullptr;
    static TeacherSource control;
    control={};
    if(lifecycle_control.mode==2){control.type=lifecycle_control.phase==0?a_move:a_end;control.match_fields=0;}
    else if(lifecycle_control.phase==0&&lifecycle_control.mode==1){control.type=a_priority;control.command.kind=a_priority;control.command.target=9;control.match_fields=match_kind|match_target;}
    else if(lifecycle_control.phase==0){control.type=a_replan;control.command.kind=g_farm;control.command.target=0;control.match_fields=match_kind|match_target;}
    else{control.type=a_move;control.match_fields=0;}
    return &control;
}
void decision(const Context& ctx,const Decision& result)
{
    ++lifecycle_control.decisions;lifecycle_control.last_status=static_cast<int>(result.status);
    const bool move_done_end=lifecycle_control.mode==2&&lifecycle_control.primitive_accepted&&result.type==a_end&&
        (result.status==DecisionStatus::learned_end||result.status==DecisionStatus::rule_end);
    if(result.status!=DecisionStatus::command&&!move_done_end)lifecycle_control.failed=1;
    zero_lifecycle::Choice wire{};
    wire.status=static_cast<int>(result.status);wire.type=result.type;wire.kind=result.command.kind;wire.target=result.command.target;
    wire.count=result.command.count;wire.x=result.command.at.x;wire.y=result.command.at.y;wire.round=ctx.obs->round;wire.command_index=ctx.obs->command_index;
    wire.rule=result.rule_mask;wire.admitted=result.admitted_mask;wire.used=result.used_mask;
    const std::uint64_t fields[]={result.fuel.state,result.fuel.encode,result.fuel.rule_mask,result.fuel.controller,result.fuel.argument,result.fuel.trace,result.fuel.total,
        result.fuel.argument_build,result.fuel.argument_project,result.fuel.argument_scan,result.fuel.argument_route,result.fuel.argument_quantity,result.fuel.argument_destroy};
    for(int i=0;i<13;++i)wire.fuel[i]=fields[i];
    wire.context_build=result.fuel.context_build;wire.context_attention=result.fuel.context_attention;wire.context_destroy=result.fuel.context_destroy;wire.context_entities=result.fuel.context_entities;
    zero_student_probe(2,&wire,sizeof wire);
}
void dispatched(const Context& ctx,ActionType type,const Cand& command,int receipt,std::uint64_t before,std::uint64_t after)
{
    if(lifecycle_control.mode==1&&type==a_priority){if(receipt==1){lifecycle_control.priority_done=1;lifecycle_control.phase=1;}else lifecycle_control.failed=1;}
    else if(type==a_replan)lifecycle_control.phase=1;
    else if(receipt==1){lifecycle_control.primitive_accepted=1;lifecycle_control.phase=2;}
    else lifecycle_control.failed=1;
    zero_lifecycle::Dispatch wire{type,command.kind,command.target,command.count,command.at.x,command.at.y,receipt,ctx.obs->round,ctx.obs->command_index,before,after};
    zero_student_probe(3,&wire,sizeof wire);
}
void init_stage(int stage,std::uint64_t before,std::uint64_t after)
{
    lifecycle_control.last_init=stage;
    const zero_lifecycle::Init wire{stage,static_cast<int>(cambc::round_number()),before,after};
    zero_student_probe(1,&wire,sizeof wire);
}
} // namespace zero::student::probe
#endif
#else
namespace {
std::vector<std::string> lifecycle_events;
using ExtraProbeHandler = wasm_trap_t* (*)(wasmtime_caller_t*,Raw*,std::size_t);
ExtraProbeHandler extra_probe_handler{};
int lifecycle_command_choices{}, lifecycle_dispatches{};
std::vector<std::uint8_t> native_draft_body;
int native_draft_rows{},native_draft_label{-1},native_draft_natural{-1},native_draft_frames{},native_drafted_index{-1},native_drafted_target{-1};
bool native_draft_bits_equal{};
template<class T>void draft_append(T value){const auto* p=reinterpret_cast<const std::uint8_t*>(&value);native_draft_body.insert(native_draft_body.end(),p,p+sizeof value);}
template<class T>T probe_read(const Guest& memory,std::uint32_t p){T value{};if(!memory.ok(p,sizeof value))die("probe event outside memory");std::memcpy(&value,memory.base+p,sizeof value);return value;}
wasm_trap_t* probe_host(void*,wasmtime_caller_t* caller,Raw* args,std::size_t)
{
    auto* bot=bot_of(caller);const auto memory=guest(caller,bot);const int event=args[0].i32;const auto p=static_cast<std::uint32_t>(args[1].i32);
    if(event==10){const auto w=probe_read<zero_lifecycle::PriorityParity>(memory,p);
        if(!memory.ok(w.local,w.local_words*4)||!memory.ok(w.reference,w.reference_words*4))die("priority parity body outside memory");
        const bool equal=w.local_words==w.reference_words&&std::memcmp(memory.base+w.local,memory.base+w.reference,w.local_words*4)==0;
        std::uint32_t first=0;while(first<std::min(w.local_words,w.reference_words)&&probe_read<std::uint32_t>(memory,w.local+first*4)==probe_read<std::uint32_t>(memory,w.reference+first*4))++first;
        std::printf("{\"priorityRefreshParity\":{\"pass\":%s,\"localWords\":%u,\"fullWords\":%u,\"factWords\":%u,\"historyWords\":%u,\"firstDifference\":%u,\"channel\":%d,\"remaining\":%d,\"priority\":%d,\"commandIndex\":%d,\"previousRevision\":%llu,\"localRevision\":%llu,\"fullRevision\":%llu,\"localFuel\":%llu,\"fullFuel\":%llu}}\n",equal?"true":"false",w.local_words,w.reference_words,w.fact_words,w.history_words,first,w.channel,w.remaining,w.priority,w.command_index,static_cast<unsigned long long>(w.previous_revision),static_cast<unsigned long long>(w.local_revision),static_cast<unsigned long long>(w.full_revision),static_cast<unsigned long long>(w.local_fuel),static_cast<unsigned long long>(w.full_fuel));return nullptr;}
    if(event>=7){if(!extra_probe_handler)die("unexpected extra probe event");return extra_probe_handler(caller,args,3);}
    std::ostringstream out;out.precision(9);out<<"{\"probeEvent\":"<<event;
    if(event==1){const auto w=probe_read<zero_lifecycle::Init>(memory,p);out<<",\"stage\":"<<w.stage<<",\"round\":"<<w.round<<",\"before\":"<<w.before<<",\"after\":"<<w.after;}
    else if(event==2){const auto w=probe_read<zero_lifecycle::Choice>(memory,p);if(w.status==0)++lifecycle_command_choices;out<<",\"status\":"<<w.status<<",\"type\":"<<w.type<<",\"payload\":["<<w.kind<<','<<w.target<<','<<w.count<<','<<w.x<<','<<w.y<<"],\"round\":"<<w.round<<",\"commandIndex\":"<<w.command_index<<",\"ruleMask\":"<<w.rule<<",\"admittedMask\":"<<w.admitted<<",\"usedMask\":"<<w.used<<",\"fuel\":[";for(int i=0;i<13;++i)out<<(i?",":"")<<w.fuel[i];out<<"],\"contextBuild\":"<<w.context_build<<",\"contextAttention\":"<<w.context_attention<<",\"contextDestroy\":"<<w.context_destroy<<",\"contextEntities\":"<<w.context_entities;}
    else if(event==3){const auto w=probe_read<zero_lifecycle::Dispatch>(memory,p);++lifecycle_dispatches;out<<",\"type\":"<<w.type<<",\"payload\":["<<w.kind<<','<<w.target<<','<<w.count<<','<<w.x<<','<<w.y<<"],\"receipt\":"<<w.receipt<<",\"round\":"<<w.round<<",\"commandIndex\":"<<w.command_index<<",\"before\":"<<w.before<<",\"after\":"<<w.after;}
    else if(event==6){native_drafted_index=probe_read<std::int32_t>(memory,p);native_drafted_target=probe_read<std::int32_t>(memory,p+4);out<<",\"index\":"<<native_drafted_index<<",\"target\":"<<native_drafted_target;}
    else if(event==4||event==5){const auto w=probe_read<zero_lifecycle::Trace>(memory,p);
        if(event==5){
            if(w.head!=6||w.command!=-1||w.count<=0||w.count>24||w.label<0||w.label>=w.count||!memory.ok(w.state,48*4))die("invalid actual draft frame");
            ++native_draft_frames;native_draft_rows=w.count;native_draft_label=w.label;native_draft_natural=w.natural;
            native_draft_body.assign(memory.base+w.state,memory.base+w.state+48*4);native_draft_bits_equal=true;
            for(int i=0;i<w.count;++i){const auto r=w.rows+i*w.stride;
                draft_append<std::int32_t>(probe_read<std::uint8_t>(memory,r+w.kind_offset));draft_append(probe_read<std::int32_t>(memory,r+w.target_offset));draft_append(probe_read<std::int32_t>(memory,r+w.count_offset));draft_append<std::int32_t>(probe_read<std::int16_t>(memory,r+w.x_offset));draft_append<std::int32_t>(probe_read<std::int16_t>(memory,r+w.y_offset));
                if(!memory.ok(r+w.phi_offset,24*4))die("draft phi outside memory");
                native_draft_body.insert(native_draft_body.end(),memory.base+r+w.phi_offset,memory.base+r+w.phi_offset+24*4);
                native_draft_bits_equal&=std::memcmp(native_draft_body.data()+48*4+i*(5*4+24*4)+5*4,memory.base+r+w.phi_offset,24*4)==0;
            }
            native_draft_bits_equal&=std::memcmp(native_draft_body.data(),memory.base+w.state,48*4)==0;
        }
        out<<",\"stage\":"<<w.stage<<",\"head\":"<<w.head<<",\"command\":"<<w.command<<",\"count\":"<<w.count<<",\"label\":"<<w.label<<",\"natural\":"<<w.natural<<",\"ruleMask\":"<<w.rule<<",\"admittedMask\":"<<w.admitted<<",\"usedMask\":"<<w.used<<",\"state\":[";for(int i=0;i<48;++i)out<<(i?",":"")<<probe_read<float>(memory,w.state+4*i);out<<"],\"rows\":[";
        for(int i=0;i<w.count;++i){const auto r=w.rows+i*w.stride;out<<(i?",":"")<<"{\"payload\":["<<static_cast<int>(probe_read<std::uint8_t>(memory,r+w.kind_offset))<<','<<probe_read<std::int32_t>(memory,r+w.target_offset)<<','<<probe_read<std::int32_t>(memory,r+w.count_offset)<<','<<probe_read<std::int16_t>(memory,r+w.x_offset)<<','<<probe_read<std::int16_t>(memory,r+w.y_offset)<<"],\"phi\":[";for(int j=0;j<24;++j)out<<(j?",":"")<<probe_read<float>(memory,r+w.phi_offset+4*j);out<<"]}";}out<<']';const int n=w.stage==0?15:w.count;if(w.scores&&memory.ok(w.scores,n*4)){out<<",\"logits\":[";for(int i=0;i<n;++i)out<<(i?",":"")<<probe_read<float>(memory,w.scores+i*4);out<<']';}}
    out<<'}';lifecycle_events.push_back(out.str());return nullptr;
}
void link_probe_bot(Shared& shared,Program& program)
{
    program.linker=wasmtime_linker_new(shared.bot_engine);wasm_importtype_vec_t imports;wasmtime_module_imports(program.module,&imports);
    for(std::size_t i=0;i<imports.size;++i){const auto* item=imports.data[i];const auto module=name_of(wasm_importtype_module(item)),name=name_of(wasm_importtype_name(item));const auto* type=wasm_externtype_as_functype_const(wasm_importtype_type(item));if(!type)die("non-function probe import");
        if(module=="env"&&name=="yield"){ok(wasmtime_linker_define_async_func(program.linker,module.data(),module.size(),name.data(),name.size(),type,h_yield,nullptr,nullptr),"probe yield");continue;}
        if(module=="env"&&name=="zero_student_probe"){ok(wasmtime_linker_define_func_unchecked(program.linker,module.data(),module.size(),name.data(),name.size(),type,probe_host,nullptr,nullptr),"probe trace");continue;}
        const ImportEntry* found=nullptr;if(module=="env"){for(const auto& entry:kImports)if(name==entry.name){found=&entry;break;}}else if(module=="wasi_snapshot_preview1"){for(const auto& entry:kWasi)if(name==entry.name){found=&entry;break;}}
        if(!found)die("unsupported probe import "+module+"."+name);ok(wasmtime_linker_define_func_unchecked(program.linker,module.data(),module.size(),name.data(),name.size(),type,found->fn,nullptr,nullptr),"probe import");
    }
    wasm_importtype_vec_delete(&imports);ok(wasmtime_linker_instantiate_pre(program.linker,program.module,&program.pre),"probe instantiate_pre");
}
}
#endif
