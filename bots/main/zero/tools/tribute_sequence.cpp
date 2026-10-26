// One controlled warm trajectory. The production caller is included unchanged.
#include "tribute_sequence_wire.h"
#ifdef ZERO_TEACHER_WASM
#include "../src/bot.cpp"
#include "../src/core/rules.h"
#include <cstddef>
#include <stdexcept>

extern "C" __attribute__((import_module("env"), import_name("zero_student_probe")))
void zero_student_probe(std::int32_t, const void*, std::uint32_t);
volatile zero_sequence::Control sequence_control{{'z','e','r','o','.','t','r','i','b','u','t','e','.','v','1',0}};

namespace {
using zero_sequence::Span;
template<class T> void span(std::vector<Span>& out,const char* name,const T& value,int index=-1){
    out.push_back({static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(name)),
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&value)),sizeof value,index});
}
template<class T> void vector_span(std::vector<Span>& out,const char* name,const std::vector<T>& value){
    out.push_back({static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(name)),
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value.data())),
        static_cast<std::uint32_t>(value.size()*sizeof(T)),-1});
}
void unit_spans(std::vector<Span>& out,const char* prefix,const zero::UnitView& u,int index){
    (void)prefix;
#define U(field) span(out,"unit." #field,u.field,index)
    U(id);U(roster);U(type);U(at);U(hp);U(max_hp);U(strength);U(focus);U(gems);U(gold);
    U(cooldown);U(recalling);U(tributing);U(stunned);U(shield);
#undef U
}
void emit_frame(int phase){
    auto& s=state();const auto& o=s.obs;const auto& m=s.mem;
    std::vector<Span> fields;
    fields.reserve(256+(o.allies.size()+o.enemies.size()+1)*16+o.camps.size()*7+o.utility_actions.size()*4+m.enemies.size()*9);
#define O(field) span(fields,"obs." #field,o.field)
    O(revision);O(round);O(team);O(command_index);unit_spans(fields,"self",o.self,-1);
    O(alive);O(can_move);O(can_attack);O(can_cast);O(on_spawn);O(recall_started);
    O(command_facts);O(teleporting);O(can_recall);O(can_tribute);O(can_cancel);O(can_buy);O(can_priority);
    O(can_cast_slot);O(state_bits);O(utility_known_mask);O(utility_known_consumables);O(utility_capability_limits);
    O(utility_affected_limits);O(ward_arguments_complete);O(consumable_arguments_complete);O(restriction_bits);
    O(channel_remaining);O(ward_charges);O(ward_cooldown);O(priority);O(consumables);O(item_counts);O(buyable_items);
    O(ally_ward_facts);O(respawn_in);O(items);O(ward_ready);O(dash_destinations_known);O(active_dash_range);
    O(reservation_facts);O(banked_us);O(banked_them);O(allies_alive);O(enemies_alive);O(claim_kind);O(claim_target);
#undef O
#define V(field) vector_span(fields,"obs." #field,o.field)
    V(wards);V(ally_ward_tiles);V(step_ok);V(dash_ok);V(reserved_tiles);V(group_status);V(altar_lock);V(altar_lock_hits);
#undef V
    for(std::size_t i=0;i<o.utility_actions.size();++i){const auto& u=o.utility_actions[i];
        span(fields,"obs.utility.type",u.type,i);span(fields,"obs.utility.target",u.target,i);
        span(fields,"obs.utility.at",u.at,i);span(fields,"obs.utility.phi",u.phi,i);}
    for(std::size_t i=0;i<o.allies.size();++i)unit_spans(fields,"ally",o.allies[i],static_cast<int>(i));
    for(std::size_t i=0;i<o.enemies.size();++i)unit_spans(fields,"enemy",o.enemies[i],100000+static_cast<int>(i));
    for(std::size_t i=0;i<o.camps.size();++i){const auto& c=o.camps[i];
#define C(field) span(fields,"obs.camp." #field,c.field,i)
        C(id);C(at);C(type);C(hp);C(awake);C(group);
#undef C
    }
#define M(field) span(fields,"memory." #field,m.field)
    M(banked_us_hist);M(banked_them_hist);M(hist_round);M(hist_head);M(deaths);M(was_alive);
    M(goal_kind);M(goal_target);M(goal_since);M(goal_recall_p);M(goal_decided);M(prev_at);M(prev_valid);M(dir_x);M(dir_y);
#undef M
#define MV(field) vector_span(fields,"memory." #field,m.field)
    MV(respawn_round);MV(seen_round);MV(enemy_near_round);
#undef MV
    for(std::size_t i=0;i<m.enemies.size();++i){const auto& e=m.enemies[i];
#define E(field) span(fields,"memory.enemy." #field,e.field,i)
        E(id);E(type);E(at);E(hp);E(max_hp);E(strength);E(focus);E(gems);E(round);
#undef E
    }
    span(fields,"state.rng",s.rng);span(fields,"state.lines",s.lines);span(fields,"state.turns",s.turns);span(fields,"state.init_stage",s.init_stage);
    if(phase){const auto& r=student_runtime();
        span(fields,"runtime.arguments",r.arguments.argument);span(fields,"runtime.known",r.arguments.known_mask);
#define R(field) span(fields,"runtime." #field,r.field)
        R(root_bound);R(dispatch);R(dispatch_known);R(refresh);R(refresh_known);
        R(learned_end);R(rule_end);R(forced_end);R(attempted);R(rejected);R(replans);
#undef R
    }
    const auto& observer=zero::sdk::last_observer_fuel();zero_sequence::Frame frame{};
    frame.phase=phase;frame.interval=sequence_control.interval;frame.round=o.round;frame.actor=o.self.id;frame.team=o.team;
    frame.x=o.self.at.x;frame.y=o.self.at.y;frame.gems=o.self.gems;frame.bank_us=o.banked_us;frame.bank_them=o.banked_them;
    frame.channel=o.self.recalling?1:o.self.tributing?2:o.teleporting?3:0;frame.remaining=o.channel_remaining;
    frame.command_index=o.command_index;frame.init_stage=s.init_stage;frame.turns=s.turns;
    frame.path_fields=s.paths?s.paths->fields_built():-1;frame.cache_prepared=s.camp_context_cache.prepared_for(s.map);
    frame.observer_complete=observer.complete;frame.observer_reset=observer.fuel_reset;frame.observer_revision=observer.revision;
    frame.spans=reinterpret_cast<std::uintptr_t>(fields.data());frame.count=static_cast<std::uint32_t>(fields.size());
    zero_student_probe(20,&frame,sizeof frame);
}
}

namespace zero::student::probe {
void install_profile(FuelBounds& arguments,std::uint64_t& root,std::uint64_t* dispatch,std::uint32_t& dispatch_known,
                     std::uint64_t* refresh,std::uint32_t& refresh_known){
    if(sequence_control.installed)throw std::logic_error("sequence profile installed twice");
    root=sequence_control.root;arguments.known_mask=sequence_control.known;dispatch_known=sequence_control.dispatch_known;
    refresh_known=sequence_control.refresh_known;
    for(int i=0;i<a_count;++i){arguments.argument[i]=sequence_control.arguments[i];dispatch[i]=sequence_control.dispatch[i];refresh[i]=sequence_control.refresh[i];}
    sequence_control.installed=1;
}
void trace(void*,const DecisionTrace& e){
    zero_sequence::Trace w{};w.stage=static_cast<int>(e.stage);w.head=e.head;w.command=e.command;w.label=e.label;w.natural=e.natural_label;
    w.forced=e.forced;w.status=static_cast<int>(e.decision_status);w.rule=e.rule_mask;w.admitted=e.admitted_mask;w.used=e.used_mask;
    w.state=reinterpret_cast<std::uintptr_t>(e.state);w.contexts=reinterpret_cast<std::uintptr_t>(e.context.data());
    w.context_count=e.context.size();w.context_stride=sizeof(CampContext);w.group_offset=offsetof(CampContext,group);w.context_phi=offsetof(CampContext,phi);
    w.rows=reinterpret_cast<std::uintptr_t>(e.rows.data());w.row_count=e.rows.size();w.row_stride=sizeof(Cand);
    w.kind=offsetof(Cand,kind);w.target=offsetof(Cand,target);w.count=offsetof(Cand,count);w.x=offsetof(Cand,at)+offsetof(Tile,x);
    w.y=offsetof(Cand,at)+offsetof(Tile,y);w.phi=offsetof(Cand,phi);w.scores=reinterpret_cast<std::uintptr_t>(e.scores.data());w.score_count=e.scores.size();w.scalar=e.predicted_scalar;
    zero_student_probe(23,&w,sizeof w);
}
void configure_options(const Context&,DecisionOptions& options){options.trace=trace;}
const TeacherSource* source(const Context& ctx){
    ++sequence_control.sources;
    if(sequence_control.sources!=1||ctx.obs->command_index!=0)throw std::logic_error("sequence unexpected extra decision/history");
    static TeacherSource chosen;chosen={};const auto round=ctx.obs->round;
    if(round==83){
        if(ctx.obs->self.at!=Tile{29,25}||!ctx.obs->self.recalling||!ctx.obs->can_cancel||ctx.obs->self.gems!=36)
            throw std::logic_error("sequence CANCEL prefix prestate mismatch");
        chosen.type=a_cancel;chosen.command.kind=a_cancel;chosen.command.target=-1;chosen.command.at=ctx.obs->self.at;chosen.match_fields=match_all;
    }else if(round==84){
        if(ctx.obs->self.at!=Tile{29,25}||ctx.obs->self.recalling||!ctx.obs->can_move||ctx.obs->self.gems!=36)
            throw std::logic_error("sequence MOVE prefix prestate mismatch");
        chosen.type=a_move;chosen.command.kind=0;chosen.command.target=-1;chosen.command.at={28,26};chosen.match_fields=match_all;
    }else if(round==85){
        if(ctx.obs->self.gems!=36||!ctx.obs->can_tribute||ctx.obs->self.at!=Tile{28,26}||
           cheb(ctx.obs->self.at,Tile{27,27})>catalog().settings.submit_gems_range||sequence_control.altar<0||
           static_cast<std::size_t>(sequence_control.altar)>=ctx.obs->altar_lock.size()||ctx.obs->altar_lock[sequence_control.altar]==lk_theirs)
            throw std::logic_error("sequence branch prestate mismatch");
        sequence_control.preflight=1;
        if(sequence_control.mode==zero_sequence::natural)return nullptr;
        if(sequence_control.mode==zero_sequence::treatment){chosen.type=a_tribute;chosen.command.kind=a_tribute;chosen.command.target=sequence_control.altar;chosen.command.at={27,27};chosen.match_fields=match_kind|match_target|match_tile;}
        else{const auto& p=sequence_control.mirror;chosen.type=static_cast<ActionType>(p.type);chosen.command.kind=p.kind;chosen.command.target=p.target;chosen.command.count=p.count;chosen.command.at={static_cast<std::int16_t>(p.x),static_cast<std::int16_t>(p.y)};chosen.match_fields=match_all;}
    }else return nullptr;
    return &chosen;
}
void decision(const Context& ctx,const Decision& d){
    ++sequence_control.decisions;
    zero_sequence::Choice w{};w.payload={d.type,d.command.kind,d.command.target,d.command.count,d.command.at.x,d.command.at.y,static_cast<int>(d.status)};
    w.round=ctx.obs->round;w.command_index=ctx.obs->command_index;w.natural=d.natural_command;w.admitted=d.admitted_greedy;
    w.rule=d.rule_mask;w.used=d.used_mask;w.admitted_mask=d.admitted_mask;
    const std::uint64_t cost[]={d.fuel.state,d.fuel.encode,d.fuel.rule_mask,d.fuel.controller,d.fuel.argument,d.fuel.trace,d.fuel.total};
    for(int i=0;i<7;++i)w.fuel[i]=cost[i];
    sequence_control.choice.type=w.payload.type;sequence_control.choice.kind=w.payload.kind;sequence_control.choice.target=w.payload.target;
    sequence_control.choice.count=w.payload.count;sequence_control.choice.x=w.payload.x;sequence_control.choice.y=w.payload.y;sequence_control.choice.status=w.payload.status;
    if(d.status!=DecisionStatus::command&&d.status!=DecisionStatus::learned_end&&d.status!=DecisionStatus::rule_end)sequence_control.failed=100+static_cast<int>(d.status);
    zero_student_probe(21,&w,sizeof w);
}
void dispatched(const Context& ctx,ActionType type,const Cand& c,int receipt,std::uint64_t before,std::uint64_t after){
    ++sequence_control.dispatches;sequence_control.last_receipt=receipt;
    zero_sequence::Dispatch w{{type,c.kind,c.target,c.count,c.at.x,c.at.y,0},receipt,ctx.obs->round,ctx.obs->command_index,before,after};
    if(receipt!=1)sequence_control.failed=200+receipt;
    zero_student_probe(22,&w,sizeof w);
}
void init_stage(int stage,std::uint64_t,std::uint64_t){sequence_control.last_init=stage;}
int draft_pick(const float*,const CandSet&,const float*,int natural){return natural;}
void drafted(int,int){}
}

int main(){
    std::setvbuf(stdout,nullptr,_IOLBF,0);Bot bot;
    try{
        bot.play_turn();
        if(state().init_stage!=6||sequence_control.last_init!=5)throw std::logic_error("sequence warm initializer incomplete");
        int matches=0;for(std::size_t i=0;i<state().map.altars.size();++i)if(state().map.altars[i]==zero::Tile{27,27}){sequence_control.altar=static_cast<int>(i);++matches;}
        if(matches!=1||zero::rules::tribute_turns(36)!=4)throw std::logic_error("sequence target/rules differ");
        emit_frame(0);sequence_control.phase=1;cambc::end_turn();
        for(int interval=0;interval<7;++interval){
            sequence_control.interval=interval;sequence_control.sources=0;sequence_control.decisions=0;sequence_control.dispatches=0;sequence_control.last_receipt=-1;
            const auto round=cambc::round_number();bot.play_turn();
            if(cambc::round_number()!=round)throw std::logic_error("sequence caller suspended");
            emit_frame(1);sequence_control.phase=2;cambc::end_turn();
        }
    }catch(const std::exception& e){sequence_control.failed=999;sequence_control.phase=-1;std::printf("sequence error: %s\n",e.what());}
    catch(...){sequence_control.failed=998;sequence_control.phase=-1;std::printf("sequence error: unknown exception\n");}
    while(true)cambc::end_turn();
}
#else
#include "teacher_force.cpp"
#include "../src/core/student_context.h"
#include <cctype>
#include <iostream>

namespace {
using namespace zero_sequence;
struct Named {std::string name;int index;std::vector<std::uint8_t> bytes;bool operator==(const Named&)const=default;};
struct Captured {Frame frame{};std::vector<Named> fields;std::vector<Choice> choices;std::vector<Dispatch> dispatches;std::vector<std::vector<std::uint8_t>> traces;};
Captured current_capture;
template<class T>void append_bytes(std::vector<std::uint8_t>& v,const T& value){const auto* b=reinterpret_cast<const std::uint8_t*>(&value);v.insert(v.end(),b,b+sizeof value);}
std::string guest_name(const Guest& g,std::uint32_t p){std::string s;for(int i=0;i<128;++i){if(!g.ok(p+i,1))die("sequence name outside memory");const char c=g.base[p+i];if(!c)return s;s.push_back(c);}die("sequence name too long");return{};}
void add_memory(std::vector<std::uint8_t>& target,const Guest& memory,std::uint32_t p,std::size_t n){if(n&&!memory.ok(p,n))die("sequence data outside memory");target.insert(target.end(),memory.base+p,memory.base+p+n);}
wasm_trap_t* sequence_probe(wasmtime_caller_t* caller,Raw* args,std::size_t){
    const auto memory=guest(caller,bot_of(caller));const int event=args[0].i32;const auto p=static_cast<std::uint32_t>(args[1].i32);
    if(event==20){const auto f=probe_read<Frame>(memory,p);if(f.count>100000)die("sequence too many field spans");current_capture.frame=f;current_capture.fields.clear();
        for(std::uint32_t i=0;i<f.count;++i){const auto s=probe_read<Span>(memory,f.spans+i*sizeof(Span));if(s.bytes>10000000)die("sequence span too large");Named n{guest_name(memory,s.name),s.index,{}};add_memory(n.bytes,memory,s.data,s.bytes);current_capture.fields.push_back(std::move(n));}
    }else if(event==21)current_capture.choices.push_back(probe_read<Choice>(memory,p));
    else if(event==22)current_capture.dispatches.push_back(probe_read<Dispatch>(memory,p));
    else if(event==23){const auto t=probe_read<Trace>(memory,p);if(t.context_count>100000||t.row_count>100000||t.score_count>100000)die("sequence trace too large");
        std::vector<std::uint8_t>b;for(int v:{t.stage,t.head,t.command,t.label,t.natural,t.status})append_bytes(b,v);
        append_bytes(b,t.rule);append_bytes(b,t.admitted);append_bytes(b,t.used);append_bytes(b,t.scalar);add_memory(b,memory,t.state,48*4);
        append_bytes(b,t.context_count);for(std::uint32_t i=0;i<t.context_count;++i){const auto at=t.contexts+i*t.context_stride;add_memory(b,memory,at+t.group_offset,4);add_memory(b,memory,at+t.context_phi,zero::student::kCampContextPhi*4);}
        append_bytes(b,t.row_count);for(std::uint32_t i=0;i<t.row_count;++i){const auto at=t.rows+i*t.row_stride;
            append_bytes(b,probe_read<std::uint8_t>(memory,at+t.kind));append_bytes(b,probe_read<std::int32_t>(memory,at+t.target));append_bytes(b,probe_read<std::int32_t>(memory,at+t.count));append_bytes(b,probe_read<std::int16_t>(memory,at+t.x));append_bytes(b,probe_read<std::int16_t>(memory,at+t.y));add_memory(b,memory,at+t.phi,24*4);}
        append_bytes(b,t.score_count);add_memory(b,memory,t.scores,t.score_count*4);current_capture.traces.push_back(std::move(b));
    }else die("unexpected sequence probe event");return nullptr;
}
std::uint64_t digest(const std::vector<std::uint8_t>& bytes){std::uint64_t h=1469598103934665603ull;for(auto c:bytes){h^=c;h*=1099511628211ull;}return h;}
bool healthy_log(std::string log){for(auto& c:log)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return log.find("error")==std::string::npos&&log.find("suspend")==std::string::npos&&log.find("fault")==std::string::npos&&log.find("exhaust")==std::string::npos;}
std::string payload_json(const Payload& p){std::ostringstream o;o<<'['<<p.type<<','<<p.kind<<','<<p.target<<','<<p.count<<','<<p.x<<','<<p.y<<','<<p.status<<']';return o.str();}
struct Interval {Captured captured;std::vector<std::uint8_t> before,after,before_full,after_full;std::uint64_t used{};int reason{};std::string log;};
struct Arm {std::string name,failure;bool pass{};std::vector<Interval> intervals;Captured setup;std::uint64_t setup_used{};std::string setup_log;};
bool same_choice(const Choice&a,const Choice&b){return std::memcmp(&a.payload,&b.payload,sizeof(Payload))==0&&a.round==b.round&&a.command_index==b.command_index&&a.natural==b.natural&&a.admitted==b.admitted&&a.rule==b.rule&&a.used==b.used&&a.admitted_mask==b.admitted_mask;}
bool same_dispatch(const Dispatch&a,const Dispatch&b){return std::memcmp(&a.payload,&b.payload,sizeof(Payload))==0&&a.receipt==b.receipt&&a.round==b.round&&a.command_index==b.command_index;}
bool equivalent(const Captured&a,const Captured&b,std::string& reason){
    if(a.fields!=b.fields){std::size_t i=0;while(i<a.fields.size()&&i<b.fields.size()&&a.fields[i]==b.fields[i])++i;reason="named state mismatch at "+(i<a.fields.size()?a.fields[i].name:std::string("field count"));return false;}
    if(a.frame.path_fields!=b.frame.path_fields||a.frame.cache_prepared!=b.frame.cache_prepared||a.frame.observer_complete!=b.frame.observer_complete||a.frame.observer_reset!=b.frame.observer_reset||a.frame.observer_revision!=b.frame.observer_revision){reason="observable SDK/path/cache state mismatch";return false;}
    if(a.choices.size()!=b.choices.size()||a.dispatches.size()!=b.dispatches.size()){reason="choice/dispatch count mismatch";return false;}
    for(std::size_t i=0;i<a.choices.size();++i)if(!same_choice(a.choices[i],b.choices[i])){reason="decision semantic mismatch";return false;}
    for(std::size_t i=0;i<a.dispatches.size();++i)if(!same_dispatch(a.dispatches[i],b.dispatches[i])){reason="dispatch semantic mismatch";return false;}
    if(a.traces!=b.traces){reason="root/argument inputs, masks, scores or labels mismatch";return false;}
    return true;
}
Arm run_arm(Shared& shared,Game& game,EngineRt& engine,std::int64_t handle,std::int64_t timeline,std::uint32_t snap,Control control,const Arm* reference){
    Arm arm;arm.name=control.mode==natural?"natural":control.mode==placebo?"placebo":"treatment";
    ready_prefix(engine,handle,timeline,2520,snap);replay_call(engine,"Game_Visualiser_Timeline_Branch",{I64(handle),I64(timeline),I32(2520),U(engine.s_out)});
    if(engine.call(F_GetState,{I64(handle),U(engine.s_out)})||u32(engine,engine.s_out)!=1||u32(engine,engine.s_out+4)!=1||u32(engine,engine.s_out+8)!=83||u32(engine,engine.s_out+12)!=7){arm.failure="fixed owner-turn boundary mismatch";return arm;}
    const auto setup_before=snapshot_bytes(engine,handle,snap),setup_full=snapshot_bytes(engine,handle,snap,true);
    auto bot=new_bot(shared,game,1,7);auto* memory=wasmtime_memory_data(bot->ctx,&bot->memory);const auto size=wasmtime_memory_data_size(bot->ctx,&bot->memory);
    const auto* found=std::search(memory,memory+size,magic,magic+16);if(found==memory+size)die("sequence control missing");const auto offset=found-memory;
    if(static_cast<std::size_t>(offset)+sizeof(Control)>size||std::search(found+1,static_cast<const std::uint8_t*>(memory+size),magic,magic+16)!=memory+size)die("sequence control ambiguous");
    std::memcpy(control.marker,magic,16);std::memcpy(memory+offset,&control,sizeof control);
    extra_probe_handler=sequence_probe;current_capture={};game.log[0].clear();game.log[1].clear();game.fault_text.clear();
    const int setup_reason=run_turn(game,*bot,1000000,arm.setup_used);memory=wasmtime_memory_data(bot->ctx,&bot->memory);std::memcpy(&control,memory+offset,sizeof control);arm.setup=current_capture;arm.setup_log=game.log[0]+game.log[1];
    if(setup_reason||arm.setup_used>992000||control.failed||control.phase!=1||arm.setup.frame.init_stage!=6||!healthy_log(arm.setup_log)||!game.fault_text.empty()||snapshot_bytes(engine,handle,snap)!=setup_before||snapshot_bytes(engine,handle,snap,true)!=setup_full){arm.failure="warm setup health/init/snapshot failure";extra_probe_handler=nullptr;return arm;}
    if(reference&&!equivalent(reference->setup,arm.setup,arm.failure)){extra_probe_handler=nullptr;return arm;}
    for(int interval=0;interval<7;++interval){
        if(engine.call(F_GetState,{I64(handle),U(engine.s_out)})||u32(engine,engine.s_out)!=1||u32(engine,engine.s_out+4)!=1||u32(engine,engine.s_out+8)!=static_cast<unsigned>(83+interval)||u32(engine,engine.s_out+12)!=7){arm.failure="owner interval/round boundary mismatch";break;}
        Interval item;item.before=snapshot_bytes(engine,handle,snap);item.before_full=snapshot_bytes(engine,handle,snap,true);
        if(reference&&control.mode==treatment&&interval==2&&(item.before!=reference->intervals[interval].before||item.before_full!=reference->intervals[interval].before_full)){arm.failure="treatment common prefix engine mismatch before intervention";break;}
        current_capture={};game.log[0].clear();game.log[1].clear();game.fault_text.clear();
        item.reason=run_turn(game,*bot,100000,item.used);memory=wasmtime_memory_data(bot->ctx,&bot->memory);std::memcpy(&control,memory+offset,sizeof control);
        item.captured=current_capture;item.log=game.log[0]+game.log[1];item.after=snapshot_bytes(engine,handle,snap);item.after_full=snapshot_bytes(engine,handle,snap,true);arm.intervals.push_back(std::move(item));auto& row=arm.intervals.back();
        const auto& c=row.captured;
        if(row.reason||row.used>92000||control.failed||control.phase!=2||control.sources!=1||control.decisions!=1||c.choices.size()!=1||!healthy_log(row.log)||!game.fault_text.empty()||!c.frame.observer_complete||c.frame.observer_reset){arm.failure="ordinary interval health/admission/history failure";break;}
        const auto& choice=c.choices[0];const bool command=choice.payload.status==0;
        if((command&&(c.dispatches.size()!=1||c.dispatches[0].receipt!=1))||(!command&&!c.dispatches.empty())){arm.failure="decision/receipt completion mismatch";break;}
        if(interval<2){const int expected=interval==0?8:1;if(choice.payload.type!=expected||!command||c.frame.command_index!=1||c.frame.gems!=36||(interval==0&&(c.frame.x!=29||c.frame.y!=25))){arm.failure="controlled prefix delivery mismatch";break;}}
        if(interval==2&&!control.preflight){arm.failure="branch preflight absent";break;}
        if(reference&&(control.mode!=treatment||interval<2)){const auto& r=reference->intervals[interval];if(row.before!=r.before||row.before_full!=r.before_full||row.after!=r.after||row.after_full!=r.after_full){arm.failure="full engine common-prefix/placebo mismatch";break;}if(!equivalent(r.captured,c,arm.failure))break;}
        if(engine.call(F_EndTurn,{I64(handle),I32(0)})){arm.failure="owner END rejected";break;}
        if(interval==6){arm.pass=true;break;}
        bool ready=false;for(int step=0;step<1024;++step){if(engine.call(F_Advance,{I64(handle)})||engine.call(F_GetState,{I64(handle),U(engine.s_out)})){arm.failure="engine advance/state failure";break;}const int phase=u32(engine,engine.s_out),owner=u32(engine,engine.s_out+12);if(phase!=1||u32(engine,engine.s_out+8)>89){arm.failure="fixture terminal/horizon boundary";break;}if(owner==7){if(u32(engine,engine.s_out+4)!=1)arm.failure="wrong fixture actor team";else ready=true;break;}if(owner<0||u32(engine,engine.s_out+4)>1||engine.call(F_EndTurn,{I64(handle),I32(0)})){arm.failure="foreign END/state failure";break;}}
        if(!ready){if(arm.failure.empty())arm.failure="foreign progression bound";break;}
    }
    extra_probe_handler=nullptr;return arm;
}
std::string arm_json(const Arm& arm){std::ostringstream out;out<<"{\"name\":\""<<arm.name<<"\",\"pass\":"<<(arm.pass?"true":"false")<<",\"failure\":\""<<json_escape(arm.failure)<<"\",\"setupUsed\":"<<arm.setup_used<<",\"setupLog\":\""<<json_escape(arm.setup_log)<<"\",\"intervals\":[";
    for(std::size_t i=0;i<arm.intervals.size();++i){const auto& r=arm.intervals[i];const auto& f=r.captured.frame;out<<(i?",":"")<<"{\"round\":"<<f.round<<",\"used\":"<<r.used<<",\"reason\":"<<r.reason<<",\"fromInputPrefix\":2520,\"positionObserved\":["<<f.x<<','<<f.y<<"],\"gemsObserved\":"<<f.gems<<",\"bankObserved\":"<<f.bank_us<<",\"channelObserved\":"<<f.channel<<",\"timerObserved\":"<<f.remaining<<",\"commandIndex\":"<<f.command_index<<",\"namedFields\":"<<r.captured.fields.size()<<",\"traceFrames\":"<<r.captured.traces.size()<<",\"engineBeforeDigest\":"<<digest(r.before_full)<<",\"engineAfterDigest\":"<<digest(r.after_full)<<",\"decisions\":[";
        for(std::size_t j=0;j<r.captured.choices.size();++j){const auto& c=r.captured.choices[j];out<<(j?",":"")<<"{\"payload\":"<<payload_json(c.payload)<<",\"natural\":"<<c.natural<<",\"admitted\":"<<c.admitted<<",\"rule\":"<<c.rule<<",\"used\":"<<c.used<<",\"fuel\":[";for(int k=0;k<7;++k)out<<(k?",":"")<<c.fuel[k];out<<"]}";}out<<"],\"receipts\":[";for(std::size_t j=0;j<r.captured.dispatches.size();++j)out<<(j?",":"")<<r.captured.dispatches[j].receipt;out<<"],\"log\":\""<<json_escape(r.log)<<"\"}";}
    out<<"]}";return out.str();}
}

int main(int argc,char** argv){
    if(argc!=7)die("tribute_sequence ENGINE PROBE REPLAY CACHE WEIGHTS PROFILE");
    Control initial{};std::ifstream profile(argv[6]);if(!(profile>>initial.root>>initial.known>>initial.dispatch_known))die("sequence profile prefix");for(auto& x:initial.arguments)if(!(profile>>x))die("sequence argument profile");for(auto& x:initial.dispatch)if(!(profile>>x))die("sequence dispatch profile");if(!(profile>>initial.refresh_known))die("sequence refresh profile");for(auto& x:initial.refresh)if(!(profile>>x))die("sequence refresh bounds");
    if(initial.root!=42000||initial.refresh_known||initial.known&((1u<<3)|(1u<<4)|(1u<<5)))die("sequence unsupported profile");
    Shared shared;shared.engine_engine=wasm_engine_new_with_config(wasm_config_new());auto* config=wasm_config_new();wasmtime_config_consume_fuel_set(config,true);wasmtime_config_async_stack_size_set(config,8u<<20);shared.bot_engine=wasm_engine_new_with_config(config);
    shared.engine_module=load_module(shared.engine_engine,argv[1],argv[4],"engine");link_engine(shared);shared.programs.resize(1);auto& program=shared.programs[0];program.path=argv[2];program.name="zero-tribute-sequence";program.module=load_module(shared.bot_engine,program.path,argv[4],"bot");link_probe_bot(shared,program);find_params(shared,program);auto weights_value=read_weights(argv[5]);auto engine=new_engine(shared);auto& E=*engine;
    const auto replay=read_file(argv[3]);const auto bytes=E.malloc(replay.size());std::memcpy(E.mem()+bytes,replay.data(),replay.size());replay_call(E,"Game_FromReplayFileBytes",{U(bytes),U(replay.size()),U(E.s_out)});std::int64_t handle,timeline;std::memcpy(&handle,E.mem()+E.s_out,8);replay_call(E,"Game_Visualiser_Timeline_GetMain",{I64(handle),U(E.s_out)});std::memcpy(&timeline,E.mem()+E.s_out,8);
    const auto snap=E.malloc(184760);Options options;Game game;prepare_game(game,E,options,program,handle);game.weights[0]=game.weights[1]=&weights_value;
    auto baseline=run_arm(shared,game,E,handle,timeline,snap,initial,nullptr);bool placebo_ran=false,treatment_ran=false;Arm placebo_arm,treatment_arm;std::string failure=baseline.failure;bool pass=false,continuity=false;int benefit=0;
    if(baseline.pass&&baseline.intervals.size()==7&&baseline.intervals[2].captured.choices.size()==1){initial.mode=placebo;initial.mirror=baseline.intervals[2].captured.choices[0].payload;placebo_ran=true;placebo_arm=run_arm(shared,game,E,handle,timeline,snap,initial,&baseline);continuity=placebo_arm.pass;if(!continuity)failure=placebo_arm.failure;
        else{initial.mode=treatment;treatment_ran=true;treatment_arm=run_arm(shared,game,E,handle,timeline,snap,initial,&baseline);if(!treatment_arm.pass)failure=treatment_arm.failure;else{benefit=treatment_arm.intervals.back().captured.frame.bank_us-baseline.intervals.back().captured.frame.bank_us;pass=benefit>0;if(!pass)failure="no paired banking benefit";}}}
    std::cout<<"{\"sequenceResult\":{\"pass\":"<<(pass?"true":"false")<<",\"failure\":\""<<json_escape(failure)<<"\",\"warmOnly\":true,\"setupBudget\":1000000,\"ordinaryBudget\":100000,\"reserve\":8000,\"startPrefix\":2520,\"startRound\":83,\"branchRound\":85,\"lastRound\":89,\"originalSourceTrajectory\":false,\"coldQualified\":false,\"trainingQualified\":false,\"strengthQualified\":false,\"sdkComparison\":\"persistent real SDK; complete named Obs and revisions, not unavailable private-cache export\",\"equalityExcludes\":\"pointer addresses, probe control/forced flag and measured fuel diagnostics; actual Memory, Obs, adaptive bounds and caller counters compared exactly\",\"placeboRan\":"<<(placebo_ran?"true":"false")<<",\"placeboContinuity\":"<<(continuity?"true":"false")<<",\"treatmentRan\":"<<(treatment_ran?"true":"false")<<",\"bankBenefit\":"<<benefit<<",\"baseline\":"<<arm_json(baseline);
    if(placebo_ran)std::cout<<",\"placebo\":"<<arm_json(placebo_arm);if(treatment_ran)std::cout<<",\"treatment\":"<<arm_json(treatment_arm);std::cout<<"}}\n";return 0;
}
#endif
