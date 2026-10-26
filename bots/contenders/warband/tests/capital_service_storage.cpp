#define WB_SERVICE_CONTROLLER_NATIVE_HOST_FREE
#define WB_SERVICE_FORECAST_NATIVE_HOST_FREE
#define WB_ENCOUNTER_NATIVE_HOST_FREE
#define WB_SERVICE_NATIVE_HOST_FREE
#include "../src/capital_service_controller_native.h"
#include "../src/capital_service_storage.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>

namespace s=wb::capital::service_storage;
namespace c=wb::capital::service_controller_native;
namespace f=wb::capital::service_forecast;
namespace r=wb::capital::service_routes;
namespace e=wb::capital::service_execution;
namespace n=wb::capital::service_forecast_native;
using Store=s::Storage<c::History>;

// Standard allocation instrumentation only. Every object is constructed by a
// real new-expression and destroyed by its matching delete, never type-punned.
namespace allocation {
bool watch{},admitted{},fail_next{};
std::size_t expected{},last_size{};
unsigned calls{},bad_order{},deletes{},tracked_count{};
std::array<void*,128> tracked{};
}
void* operator new(std::size_t size) {
    if(allocation::watch) {
        ++allocation::calls;allocation::last_size=size;
        if(!allocation::admitted||size!=allocation::expected)++allocation::bad_order;
        if(allocation::fail_next){allocation::fail_next=false;throw std::bad_alloc();}
    }
    if(auto* pointer=std::malloc(size?size:1)) {
        if(allocation::watch&&allocation::tracked_count<allocation::tracked.size())
            allocation::tracked[allocation::tracked_count++]=pointer;
        return pointer;
    }
    throw std::bad_alloc();
}
void operator delete(void* pointer)noexcept {
    if(pointer)for(unsigned i=0;i<allocation::tracked_count;++i)if(allocation::tracked[i]==pointer){
        allocation::tracked[i]=nullptr;++allocation::deletes;break;}
    std::free(pointer);
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete[](void* pointer)noexcept{::operator delete(pointer);}

namespace {
unsigned checks{},failures{};
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){++failures;if(failures<25)std::printf("FAIL %u: %s\n",__LINE__,#__VA_ARGS__);}}while(false)
std::array<const void*,unsigned(s::Component::Count)> pointers(const Store& x) {
    return {{x.captured(),x.model(),x.program(),x.rebound(),x.simulator(),x.route_world(),x.map(),
        x.request(),x.builder(),x.rebinder(),x.workspace(),x.interpreter(),x.actual(),x.prior(),x.actual_capture()}};
}
void prefix(const Store& x,unsigned count){
    const auto p=pointers(x);for(unsigned i=0;i<p.size();++i)CHECK((p[i]!=nullptr)==(i<count));
}
struct HookFailure {};
struct Gate {
    Store* storage{};s::Decision decision{s::Decision::Allow},after_decision{s::Decision::Allow};
    unsigned before_calls{},after_calls{};s::Component component{s::Component::Count};
    s::Construction event{s::Construction::Constructed};bool throw_before{},throw_after{},reenter{};
    static s::Decision before(void* pointer,s::Component component,std::size_t size){
        auto& self=*static_cast<Gate*>(pointer);++self.before_calls;self.component=component;
        CHECK(component==self.storage->next()&&size==Store::size(component)&&size>0);
        if(self.reenter){CHECK(self.storage->advance({&self,&before,&after}).status==s::Status::Busy);CHECK(!self.storage->reset_components());}
        if(self.throw_before)throw HookFailure{};
        allocation::expected=size;allocation::admitted=self.decision==s::Decision::Allow;
        return self.decision;
    }
    static s::Decision after(void* pointer,s::Component component,std::size_t size,s::Construction event){
        auto& self=*static_cast<Gate*>(pointer);++self.after_calls;self.event=event;
        CHECK(component==self.component&&size==Store::size(component));
        CHECK(self.storage->last().constructed==(event==s::Construction::Constructed));
        allocation::admitted=false;
        if(self.throw_after)throw HookFailure{};
        return self.after_decision;
    }
    s::Admission admission(){return {this,&before,&after};}
};
s::Result step(Store& store,Gate& gate){
    allocation::admitted=false;allocation::watch=true;
    const auto out=store.advance(gate.admission());allocation::watch=false;return out;
}
void shell_and_denial(){
    Store store;Gate gate{&store};prefix(store,0);
    static_assert(sizeof(Store)<sizeof(r::Workspace)/4);
    static_assert(std::is_same_v<decltype(store.history()),c::History&>);
    static_assert(std::is_same_v<decltype(store.program()),f::Program*>);
    static_assert(std::is_same_v<decltype(store.simulator()),f::Simulator*>);
    static_assert(std::is_same_v<decltype(store.captured()),n::Capture*>);
    static_assert(std::is_same_v<decltype(store.interpreter()),e::Interpreter*>);
    CHECK(!store.ready()&&store.next()==s::Component::Captured&&store.completed()==0&&store.attempts()==0);
    CHECK(!store.history().ready&&store.history().sequence==0);
    store.history().sequence=77;store.history().count=1;store.history().roster[0]={42,0,wb::capital::service_scenario::Entity::Champion};
    CHECK(store.advance({}).status==s::Status::Deferred&&store.history().sequence==77);
    const auto calls=allocation::calls;gate.decision=s::Decision::Deferred;
    CHECK(step(store,gate).status==s::Status::Deferred);CHECK(allocation::calls==calls&&gate.after_calls==0);
    gate.decision=s::Decision::Stale;CHECK(step(store,gate).status==s::Status::Stale);
    CHECK(allocation::calls==calls&&store.completed()==0&&store.attempts()==0);prefix(store,0);
}
void actual_components(){
    Store store;Gate gate{&store};gate.reenter=true;
    for(unsigned i=0;i<unsigned(s::Component::Count);++i){
        const auto calls=allocation::calls;const auto out=step(store,gate);
        CHECK(out.constructed&&out.completed==i+1&&out.component==static_cast<s::Component>(i));
        CHECK(allocation::calls==calls+1&&allocation::last_size==Store::size(out.component));
        CHECK(out.status==(i+1==unsigned(s::Component::Count)?s::Status::Ready:s::Status::Building));
        prefix(store,i+1);
    }
    CHECK(store.ready()&&store.completed()==unsigned(s::Component::Count));
    CHECK(gate.before_calls==unsigned(s::Component::Count)&&gate.after_calls==gate.before_calls);
    const auto calls=allocation::calls;CHECK(step(store,gate).status==s::Status::Ready&&allocation::calls==calls);
    // These are real member defaults, not claimed to have been erased by new T.
    CHECK(store.captured()->status==n::Status::NeedObservation&&store.captured()->world.actors==0);
    CHECK(store.model()->scope==n::Scope::Unknown&&store.model()->views.empty());
    CHECK(store.program()->route_count==0&&store.program()->leg_count==0&&store.program()->bodies[0].legs[0]==-1);
    CHECK(store.program()->route[0].destination==f::Point{}&&store.program()->route[f::route_limit-1].destination==f::Point{});
    CHECK(store.rebound()!=store.program()&&store.rebound()->id==0);
    CHECK(store.simulator()->result().status==f::Status::Empty&&store.simulator()->result().event_count==0);
    CHECK(store.simulator()->result().events[f::event_limit-1].pose==f::Point{});
    CHECK(store.simulator()->world().bodies[0].id==-1&&store.route_world()->actors==0);
    CHECK(store.interpreter()->state()==e::State::Empty&&store.interpreter()->reason()==e::Reason::None);
    CHECK(store.actual()->body.id==-1&&!store.actual()->observed&&store.prior()!=store.actual());
    CHECK(store.actual_capture()!=store.captured()&&!store.actual_capture()->local_facts_complete);
    CHECK(!store.workspace()->cold_ready(1)&&store.workspace()->cold_work_remaining(r::cell_limit)==r::cell_limit);
    // Exercise legitimate initialized-prefix reuse without reading uninitialized
    // representations: only Search writes make an active cell readable.
    {
        std::array<std::uint8_t,4> open{};r::Map map;map.width=4;map.height=1;map.blocked=map.own_spawn=map.foreign_positive=open;
        f::World world;world.actors=1;world.bodies[0].pose={0,0};r::Query query;query.origin={0,0};query.goal=r::Goal::Point;query.point={3,0};
        r::Search first;first.begin(map,world,query,*store.workspace());
        for(unsigned i=0;i<30&&!first.ready();++i)first.unit();
        CHECK(first.ready()&&first.found()&&first.destination()==3&&store.workspace()->cold_ready(4));
        query.origin={3,0};query.point={0,0};r::Search second;second.begin(map,world,query,*store.workspace());
        for(unsigned i=0;i<30&&!second.ready();++i)second.unit();
        CHECK(second.ready()&&second.found()&&second.destination()==0&&first.unknown());
        CHECK(store.workspace()->cold_work_remaining(r::cell_limit)==r::cell_limit-4);
    } // All borrowed Search views end before the explicit component teardown.
    const auto deleted=allocation::deletes;store.history().sequence=1234;
    CHECK(store.reset_components()&&allocation::deletes==deleted+unsigned(s::Component::Count));
    CHECK(store.completed()==0&&!store.ready()&&store.resets()==1&&store.history().sequence==1234);prefix(store,0);
    CHECK(step(store,gate).constructed&&store.captured()->status==n::Status::NeedObservation);prefix(store,1);
}
void refusal_failure_and_after(){
    Store store;Gate gate{&store};
    allocation::fail_next=true;const auto failed=step(store,gate);
    CHECK(failed.status==s::Status::AllocationFailure&&!failed.constructed&&store.completed()==0);
    CHECK(store.attempts()==1&&store.failures()==1&&gate.event==s::Construction::AllocationFailure);
    CHECK(step(store,gate).constructed&&store.completed()==1&&store.attempts()==2);prefix(store,1);
    gate.after_decision=s::Decision::Stale;const auto stale=step(store,gate);
    CHECK(stale.status==s::Status::Stale&&stale.constructed&&stale.completed==2&&store.model());
    auto* model=store.model();gate.after_decision=s::Decision::Allow;gate.throw_after=true;
    bool caught=false;try{step(store,gate);}catch(const HookFailure&){allocation::watch=false;caught=true;}
    CHECK(caught&&store.completed()==3&&store.last().constructed&&store.program()&&store.model()==model);prefix(store,3);
    gate.throw_after=false;CHECK(step(store,gate).component==s::Component::Rebound&&store.completed()==4);
    gate.throw_before=true;caught=false;const auto attempts=store.attempts();
    try{step(store,gate);}catch(const HookFailure&){allocation::watch=false;caught=true;}
    CHECK(caught&&store.completed()==4&&store.attempts()==attempts&&!store.simulator());
    gate.throw_before=false;CHECK(step(store,gate).component==s::Component::Simulator&&store.completed()==5);
}
}

// Separate exported O2 probes make allocation/constructor initialization visible
// in retained LLVM codegen without a fake clock or a native fuel assertion.
extern "C" __attribute__((noinline,used)) Store* wb_storage_shell_probe(){return new Store;}
extern "C" __attribute__((noinline,used)) r::Workspace* wb_storage_workspace_probe(){return new r::Workspace;}
extern "C" __attribute__((noinline,used)) f::Program* wb_storage_program_probe(){return new f::Program;}
extern "C" __attribute__((noinline,used)) f::Simulator* wb_storage_simulator_probe(){return new f::Simulator;}
extern "C" __attribute__((noinline,used)) e::Interpreter* wb_storage_interpreter_probe(){return new e::Interpreter;}

int main(){
    shell_and_denial();actual_components();refusal_failure_and_after();
    CHECK(allocation::bad_order==0);
    std::printf("storage_sizes shell=%zu history=%zu workspace=%zu program=%zu simulator=%zu interpreter=%zu capture=%zu frame=%zu\n",
        sizeof(Store),sizeof(c::History),sizeof(r::Workspace),sizeof(f::Program),sizeof(f::Simulator),sizeof(e::Interpreter),sizeof(n::Capture),sizeof(e::Frame));
    std::printf("capital_service_storage %u checks %u failures\n",checks,failures);return failures?1:0;
}
