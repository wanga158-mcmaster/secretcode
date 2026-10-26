#include "service_program.h"
#include "service_cursor.h"
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using namespace mosaic;namespace cursor=service_cursor;
using program::Phase;using program::Purpose;
int checks{},failures{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
struct Routing {
    Phase& phase;std::vector<std::string> trace;int remaining=20,currents{};
    bool continue_compile{},continue_dispatch{},compile_to_dispatch=true,cross_dispatch{};
    explicit Routing(Phase& value):phase(value){}
    bool current(){++currents;return remaining-->0;}
    void discovery(){trace.push_back("discovery");phase=Phase::route_capture;}
    void route_capture(){trace.push_back("route_capture");phase=Phase::goals;}
    void goals(){trace.push_back("goals");phase=Phase::compile;}
    bool compile(){trace.push_back("compile");if(compile_to_dispatch)phase=Phase::dispatch;return continue_compile;}
    bool dispatch(){trace.push_back("dispatch");if(cross_dispatch)remaining=0;return continue_dispatch;}
    void inspect(){trace.push_back("inspect");phase=Phase::capture;}
    void capture(){trace.push_back("capture");phase=Phase::tribute;}
    void channel(){trace.push_back("channel");phase=Phase::discovery;}
    void unsupported(){trace.push_back("unsupported");}
};
cursor::Key key(cursor::Point origin={3,0},std::uint64_t cycle=1,std::uint64_t body=0){
    return {0,origin,{9,1,1,1},int(Purpose::work),2,cycle,7,body,0};
}
// Deliberately independent conditional physical model. Its callbacks model
// receipt/channel/return events; no SDK mechanics, timers or certificates are
// claimed. Every command observation is freshly read from Host, never copied
// from a predicted command result.
struct Host {
    int epoch=1,revision{},camp_hp=40,bag{},score{},moves{},basics{},packets{},job_cycle=1,body_generation{};
    cursor::Point position{3,0};bool can_move=true,can_basic=true;
    enum class Channel {none,tribute,recall} channel=Channel::none;
    struct Packet {int epoch,revision,hp,bag;cursor::Point position;bool can_move,can_basic;};
    void next(){++epoch;revision=0;can_move=can_basic=true;}
    Packet read(){++packets;return {epoch,revision,camp_hp,bag,position,can_move,can_basic};}
    bool current(const Packet& p)const{return p.epoch==epoch&&p.revision==revision&&p.hp==camp_hp&&
        p.bag==bag&&p.position==position&&p.can_move==can_move&&p.can_basic==can_basic;}
    bool move(const Packet& p,cursor::Point to){
        if(!current(p)||!can_move)return false;
        if(to==cursor::Point{4,1})return false; // explicit observed blocker
        position=to;can_move=false;++moves;++revision;++body_generation;return true;
    }
    bool basic(const Packet& p){
        if(!current(p)||!can_basic||position!=cursor::Point{4,0}||camp_hp<=0)return false;
        camp_hp=0;can_basic=false;++basics;++revision;return true;
    }
    bool receipt(){if(camp_hp!=0||bag!=0)return false;bag=28;return true;}
    bool tribute_start(){if(bag<=0||channel!=Channel::none)return false;channel=Channel::tribute;return true;}
    bool tribute_finish(){if(channel!=Channel::tribute)return false;score+=bag;bag=0;channel=Channel::none;return true;}
    bool recall_start(){if(bag!=0||channel!=Channel::none)return false;channel=Channel::recall;return true;}
    bool home(){if(channel!=Channel::recall)return false;position={0,0};++body_generation;channel=Channel::none;can_move=can_basic=true;return true;}
    void new_job(){++job_cycle;camp_hp=40;}
};
struct Service {
    Phase phase=Phase::dispatch;Purpose purpose=Purpose::work;Host host;cursor::Cursor progress;
    cursor::Key family=key();std::vector<cursor::Raw> order{cursor::Raw::move({4,1}),cursor::Raw::move({4,0})};
    std::vector<std::string> trace;std::vector<int> observed_revisions;
    enum class Attempt {reject,partial,admission,apply,refuse,temporal} attempt=Attempt::reject;
    bool admitted=true,allow_retry{},allow_compile_continue=true;int capture_calls{},discovery_calls{},waits{},acquisitions{};
    std::optional<cursor::Selection> last_selected;
    Service(){progress.bind(family,{order,true});}
    bool current(){return admitted;}
    void discovery(){++discovery_calls;trace.push_back("discovery");family.group_cycle=std::uint64_t(host.job_cycle);
        family.body_generation=std::uint64_t(host.body_generation);family.origin=host.position;
        progress.bind(family,{order,true});phase=Phase::route_capture;}
    void route_capture(){trace.push_back("route_capture");++acquisitions;phase=Phase::goals;}
    void goals(){trace.push_back("goals");phase=Phase::compile;}
    bool compile(){trace.push_back("compile");phase=Phase::dispatch;return allow_compile_continue&&host.revision>0;}
    bool dispatch(){
        trace.push_back("dispatch");const auto view=progress.peek();if(!view.selection)return false;
        const auto selected=*view.selection;last_selected=selected;
        if(attempt==Attempt::admission){progress.report(selected,cursor::Outcome::admission_deferred);return false;}
        if(attempt==Attempt::partial){++capture_calls;progress.report(selected,cursor::Outcome::partial_capture);return false;}
        const auto packet=host.read();++capture_calls;observed_revisions.push_back(packet.revision);
        progress.acknowledge_fresh_packet(family);
        // These protocol-outcome injections test Cursor/scheduler composition;
        // they are not selected assessments of the model packet permissions.
        if(attempt==Attempt::temporal){progress.report(selected,cursor::Outcome::temporal_deferred);return false;}
        if(attempt==Attempt::reject){progress.report(selected,cursor::Outcome::complete_rejected);return allow_retry;}
        if(attempt==Attempt::refuse){progress.report(selected,cursor::Outcome::sdk_refused);return allow_retry;}
        const bool applied=selected.raw.kind==cursor::Kind::move?
            host.move(packet,selected.raw.destination):host.basic(packet);
        progress.report(selected,applied?cursor::Outcome::applied:cursor::Outcome::sdk_refused);
        if(!applied)return allow_retry;
        if(selected.raw.kind==cursor::Kind::basic_camp){phase=Phase::capture;return false;}
        // A physical-origin change creates a new semantic candidate family;
        // old move authority/cursor selections are not carried into Basic.
        family.origin=host.position;family.body_generation=std::uint64_t(host.body_generation);order={cursor::Raw::basic(0)};
        progress.rebase(family,{order,true},cursor::RebaseReason::physical_phase_committed);
        phase=Phase::compile;return true;
    }
    void inspect(){trace.push_back("inspect");phase=Phase::capture;}
    void capture(){trace.push_back("capture");if(host.receipt()){purpose=Purpose::bank;phase=Phase::tribute;}}
    void channel(){trace.push_back("channel");
        if(phase==Phase::tribute){if(host.tribute_start())phase=Phase::channel;}
        else if(phase==Phase::channel){if(host.tribute_finish()){purpose=Purpose::home;phase=Phase::recall;}}
        else if(phase==Phase::recall){if(host.recall_start())phase=Phase::home;}
        else if(phase==Phase::home){if(host.home())phase=Phase::discovery;}
    }
    void unsupported(){trace.push_back("unsupported");}
    void input(){host.next();program::run(phase,*this);}
};
void routing(){
    const std::array phases{Phase::discovery,Phase::route_capture,Phase::goals,Phase::inspect,Phase::capture,
        Phase::tribute,Phase::channel,Phase::recall,Phase::home,Phase::registration,Phase::layout,Phase::unsupported};
    const std::array<std::string,12> expected{"discovery","route_capture","goals","inspect","capture",
        "channel","channel","channel","channel","unsupported","unsupported","unsupported"};
    for(std::size_t i=0;i<phases.size();++i){auto phase=phases[i];Routing ops{phase};program::run(phase,ops);
        check(ops.trace==std::vector<std::string>{expected[i]}&&ops.currents==1,"noncontinuing phase executes once then returns even after changing phase");}
    {auto phase=Phase::dispatch;Routing ops{phase};ops.remaining=0;program::run(phase,ops);check(ops.trace.empty(),"failed current admission calls no phase");}
    {auto phase=Phase::dispatch;Routing ops{phase};program::run(phase,ops,0);check(ops.trace.empty()&&ops.currents==0,"zero limit performs no admission or phase work");}
    {auto phase=Phase::dispatch;Routing ops{phase};ops.continue_dispatch=true;program::run(phase,ops);
        check(ops.trace.size()==6&&ops.currents==6,"default limit bounds continuing dispatch to six callbacks");}
    {auto phase=Phase::dispatch;Routing ops{phase};ops.continue_dispatch=true;program::run(phase,ops,2);
        check(ops.trace.size()==2&&ops.currents==2,"explicit continuation limit remains exact");}
    {auto phase=Phase::dispatch;Routing ops{phase};ops.continue_dispatch=true;program::run(phase,ops,1);
        check(ops.trace.size()==1&&ops.currents==1,"single iteration limit calls a continuing branch once");}
    {auto phase=Phase::dispatch;Routing ops{phase};ops.continue_dispatch=true;ops.cross_dispatch=true;program::run(phase,ops);
        check(ops.trace.size()==1&&ops.currents==2,"continuation rechecks current after callback invalidates admission");}
    {auto phase=Phase::compile;Routing ops{phase};ops.continue_compile=true;program::run(phase,ops);
        check(ops.trace==std::vector<std::string>{"compile","dispatch"},"newly dispatch compile can continue to actual dispatch");}
    {auto phase=Phase::compile;Routing ops{phase};program::run(phase,ops);
        check(ops.trace==std::vector<std::string>{"compile"}&&phase==Phase::dispatch,"compile ready alone does not grant same-input continuation");}
    {auto phase=Phase::compile;Routing ops{phase};ops.continue_compile=true;ops.compile_to_dispatch=false;program::run(phase,ops);
        check(ops.trace==std::vector<std::string>{"compile"},"compile continuation bool cannot bypass required dispatch phase");}
    {auto phase=static_cast<Phase>(99);Routing ops{phase};program::run(phase,ops);
        check(ops.trace==std::vector<std::string>{"unsupported"},"unrecognized phase reaches only unsupported callback");}
}
void retained_work(){
    Service service;const auto first=*service.progress.peek().selection;program::run(service.phase,service);
    const auto next=service.progress.peek();
    check(next.status==cursor::Status::deferred&&next.selection&&next.selection->index==1&&next.selection->generation==first.generation,
          "complete rejected first nominee consumes once and retains next across input");
    check(service.trace==std::vector<std::string>{"dispatch"}&&service.waits==0&&service.acquisitions==0&&service.capture_calls==1,
          "dispatch begins directly with one fresh packet, no Wait or route acquisition prefix");
    const auto bound=service.progress.bind(service.family,{service.order,true});
    check(bound.selection&&bound.selection->index==1&&bound.status==cursor::Status::deferred,
          "same semantic family bind cannot restart rejected first nominee");
    service.attempt=Service::Attempt::partial;service.input();auto partial=service.progress.peek();
    check(partial.selection&&partial.selection->index==1&&partial.deferred_by==cursor::DeferredBy::partial_capture,
          "partial next packet retains exact selected index and generation");
    service.attempt=Service::Attempt::admission;const int observed=service.host.packets;service.input();auto admission=service.progress.peek();
    check(admission.selection&&admission.selection->index==1&&admission.selection->generation==first.generation&&service.host.packets==observed,
          "admission deferral retains selection and does not fabricate a fresh packet");
    service.attempt=Service::Attempt::temporal;service.input();
    check(service.progress.peek().selection->index==1&&service.progress.peek().deferred_by==cursor::DeferredBy::temporal_state,
          "injected temporal-deferred protocol outcome retains candidate rather than consuming it");
    service.attempt=Service::Attempt::apply;service.trace.clear();const int before=service.host.packets;service.input();
    check(service.trace==std::vector<std::string>{"dispatch","compile","dispatch"}&&service.host.moves==1&&service.host.basics==1&&service.host.camp_hp==0,
          "successful Move continues compile then freshly observed Basic in same input");
    check(service.host.packets-before==2&&service.observed_revisions[service.observed_revisions.size()-2]==0&&service.observed_revisions.back()==1&&service.phase==Phase::capture,
          "Basic has a new post-Move physical packet/revision, never reused Move observation");
    check(!service.progress.report(first,cursor::Outcome::complete_rejected).accepted,
          "origin-changing physical family invalidates old selection generation");
    const auto finished=*service.last_selected;service.trace.clear();service.input();
    check(service.trace==std::vector<std::string>{"capture"}&&service.host.bag==28&&service.phase==Phase::tribute,
          "external modeled receipt changes purpose and returns before tribute stage");
    service.input();check(service.phase==Phase::channel&&service.host.channel==Host::Channel::tribute&&service.host.score==0,
          "tribute start does not complete channel or skip owner input");
    service.input();check(service.host.score==28&&service.host.bag==0&&service.phase==Phase::recall,
          "modeled actual bank receipt precedes recall stage");
    service.input();check(service.phase==Phase::home&&service.host.channel==Host::Channel::recall,
          "recall start remains distinct from home return event");
    service.input();check(service.phase==Phase::discovery&&service.host.position==cursor::Point{0,0},
          "actual modeled home endpoint precedes new discovery");
    const auto old_generation=finished.generation;service.host.new_job();service.input();
    check(service.phase==Phase::route_capture&&service.progress.peek().selection->generation!=old_generation&&
          !service.progress.report(finished,cursor::Outcome::applied).accepted&&service.discovery_calls==1,
          "returned origin/body plus new job family invalidates prior candidate without implicit cursor restart");
}
void other_boundaries(){
    Service starting;starting.phase=Phase::compile;starting.attempt=Service::Attempt::apply;
    program::run(starting.phase,starting);
    check(starting.trace==std::vector<std::string>{"compile"}&&starting.phase==Phase::dispatch&&starting.host.packets==0,
          "real fixture compile wrapper on revision zero stops before fresh dispatch");
    Service refused;refused.attempt=Service::Attempt::refuse;program::run(refused.phase,refused);
    check(refused.progress.peek().selection->index==1&&refused.progress.peek().deferred_by==cursor::DeferredBy::fresh_reobservation&&refused.host.moves==0,
          "modeled refusal consumes nominee and requires another fresh packet");
    Service blocked;blocked.admitted=false;const auto prior=*blocked.progress.peek().selection;program::run(blocked.phase,blocked);
    check(blocked.capture_calls==0&&blocked.progress.peek().selection->generation==prior.generation&&blocked.progress.peek().selection->index==prior.index,
          "top-level admission failure leaves selected semantic progress untouched");
    Service rejected;rejected.allow_retry=true;program::run(rejected.phase,rejected);
    check(rejected.capture_calls==2&&rejected.host.packets==2&&rejected.progress.peek().status==cursor::Status::exhausted&&rejected.host.moves==0,
          "bounded same-input retry uses a fresh packet for every complete rejection and exhausts finite catalogue");
}
}
int main(){routing();retained_work();other_boundaries();std::printf("service program: %d checks, %d failures\n",checks,failures);return failures?1:0;}
