#pragma once
// Cold ownership for the ACTUAL service-controller components. This is storage,
// not a model, command lease, fuel bound or replacement for fresh observations.
// Each real constructor (including its member DMIs) needs its own admission.
#include "capital_service_forecast_native.h"
#include "capital_service_routes.h"
#include "capital_service_execution.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>

namespace wb::capital::service_storage {
namespace f=service_forecast;
namespace n=service_forecast_native;
namespace r=service_routes;
namespace e=service_execution;

enum class Component : unsigned {
    Captured,Model,Program,Rebound,Simulator,RouteWorld,Map,Request,Builder,
    Rebinder,Workspace,Interpreter,Actual,Prior,ActualCapture,Count
};
enum class Decision {Allow,Deferred,Stale};
enum class Construction {Constructed,AllocationFailure,CapacityFailure};
enum class Status {Building,Ready,Deferred,Stale,AllocationFailure,CapacityFailure,Busy};
struct Admission {
    void* context{};
    Decision (*before)(void*,Component,std::size_t){};
    Decision (*after)(void*,Component,std::size_t,Construction){};
};
struct Result {
    Status status{Status::Building};Component component{Component::Captured};
    unsigned completed{};bool constructed{},ready{};
};

// History is the controller's actual small History type, defined by its owner.
// It is the only inline payload. Construct this once before optional planning;
// do not copy a mutable SDK frame into a retained planner merely to make storage.
template<class History> class Storage {
    History history_;
    std::unique_ptr<n::Capture> captured_;
    std::unique_ptr<n::ModelContext> model_;
    std::unique_ptr<f::Program> program_,rebound_;
    std::unique_ptr<f::Simulator> simulator_;
    std::unique_ptr<f::World> route_world_;
    std::unique_ptr<r::Map> map_;
    std::unique_ptr<r::Request> request_;
    std::unique_ptr<r::Builder> builder_;
    std::unique_ptr<r::Rebinder> rebinder_;
    std::unique_ptr<r::Workspace> workspace_;
    std::unique_ptr<e::Interpreter> interpreter_;
    std::unique_ptr<e::Frame> actual_,prior_;
    std::unique_ptr<n::Capture> actual_capture_;
    unsigned completed_{};std::uint64_t attempts_{},failures_{},resets_{};
    bool busy_{};Result last_;

    template<class T> static void create(std::unique_ptr<T>& slot) {
        // Deliberately DEFAULT-initialize. make_unique<T>() would value-initialize
        // the enclosing T before an implicit default constructor. This preserves
        // Workspace's untouched cells, but does NOT suppress any member DMI in
        // Simulator/Program/Interpreter. Those real costs remain admitted here.
        slot.reset(new T);
    }
    void construct(Component component) {
        switch(component) {
        case Component::Captured:create(captured_);return;
        case Component::Model:create(model_);return;
        case Component::Program:create(program_);return;
        case Component::Rebound:create(rebound_);return;
        case Component::Simulator:create(simulator_);return;
        case Component::RouteWorld:create(route_world_);return;
        case Component::Map:create(map_);return;
        case Component::Request:create(request_);return;
        case Component::Builder:create(builder_);return;
        case Component::Rebinder:create(rebinder_);return;
        case Component::Workspace:create(workspace_);return;
        case Component::Interpreter:create(interpreter_);return;
        case Component::Actual:create(actual_);return;
        case Component::Prior:create(prior_);return;
        case Component::ActualCapture:create(actual_capture_);return;
        case Component::Count:return;
        }
    }
    Result finish(Status status,Component component,bool constructed=false) {
        last_={status,component,completed_,constructed,ready()};return last_;
    }
public:
    // USER-PROVIDED: new Storage<History>() must not value-zero an enclosing
    // Workspace/Simulator aggregate. No planner component exists at this point.
    Storage() {}
    Storage(const Storage&)=delete;Storage& operator=(const Storage&)=delete;
    Storage(Storage&&)=delete;Storage& operator=(Storage&&)=delete;

    History& history(){return history_;}const History& history()const{return history_;}
    bool ready()const{return completed_==unsigned(Component::Count);}
    Component next()const{return static_cast<Component>(completed_);}
    unsigned completed()const{return completed_;}
    std::uint64_t attempts()const{return attempts_;}
    std::uint64_t failures()const{return failures_;}
    std::uint64_t resets()const{return resets_;}
    const Result& last()const{return last_;}
    static constexpr std::size_t size(Component component) {
        switch(component) {
        case Component::Captured:case Component::ActualCapture:return sizeof(n::Capture);
        case Component::Model:return sizeof(n::ModelContext);
        case Component::Program:case Component::Rebound:return sizeof(f::Program);
        case Component::Simulator:return sizeof(f::Simulator);
        case Component::RouteWorld:return sizeof(f::World);
        case Component::Map:return sizeof(r::Map);
        case Component::Request:return sizeof(r::Request);
        case Component::Builder:return sizeof(r::Builder);
        case Component::Rebinder:return sizeof(r::Rebinder);
        case Component::Workspace:return sizeof(r::Workspace);
        case Component::Interpreter:return sizeof(e::Interpreter);
        case Component::Actual:case Component::Prior:return sizeof(e::Frame);
        case Component::Count:return 0;
        }
        return 0;
    }

    // AT MOST ONE actual constructor, with no hidden reset/retry loop. The
    // callback must reserve constructor + result writes + after/yield/fallback,
    // and own its actual invocation/round fence. There is no built-in fuel floor.
    // A stale/throwing after-hook keeps the legitimately constructed object and
    // committed stage; the next call cannot reconstruct it or count it twice.
    Result advance(Admission admission) {
        const auto component=next();
        if(busy_)return {Status::Busy,component,completed_,false,ready()};
        if(ready())return finish(Status::Ready,Component::Count);
        if(!admission.before)return finish(Status::Deferred,component);
        struct Guard {bool& busy;explicit Guard(bool& value):busy(value){busy=true;}~Guard(){busy=false;}} guard(busy_);
        const auto allowed=admission.before(admission.context,component,size(component));
        if(allowed!=Decision::Allow)
            return finish(allowed==Decision::Deferred?Status::Deferred:Status::Stale,component);
        ++attempts_;Construction event=Construction::Constructed;Status status=Status::Building;
        try {construct(component);++completed_;status=ready()?Status::Ready:Status::Building;}
        catch(const std::bad_alloc&) {++failures_;event=Construction::AllocationFailure;status=Status::AllocationFailure;}
        catch(const std::length_error&) {++failures_;event=Construction::CapacityFailure;status=Status::CapacityFailure;}
        const bool constructed=event==Construction::Constructed;
        // Publish before invoking user code, including when that code throws.
        finish(status,component,constructed);
        if(admission.after) {
            const auto after=admission.after(admission.context,component,size(component),event);
            if(after!=Decision::Allow)
                return finish(after==Decision::Deferred?Status::Deferred:Status::Stale,component,constructed);
        }
        return last_;
    }

    // Explicit caller-admitted teardown only, never implicit query invalidation.
    // All borrowed views/reference facades MUST be retired first. History stays
    // alive; default destruction of Storage itself also destroys actual objects.
    // Teardown, allocator work and the next construction require native payment.
    bool reset_components() {
        if(busy_)return false;
        actual_capture_.reset();prior_.reset();actual_.reset();interpreter_.reset();
        workspace_.reset();rebinder_.reset();builder_.reset();request_.reset();map_.reset();
        route_world_.reset();simulator_.reset();rebound_.reset();program_.reset();model_.reset();captured_.reset();
        completed_=0;++resets_;last_={};return true;
    }

#define WB_SERVICE_STORAGE_ACCESSOR(name,type) \
    type* name(){return name##_.get();}const type* name()const{return name##_.get();}
    WB_SERVICE_STORAGE_ACCESSOR(captured,n::Capture)
    WB_SERVICE_STORAGE_ACCESSOR(model,n::ModelContext)
    WB_SERVICE_STORAGE_ACCESSOR(program,f::Program)
    WB_SERVICE_STORAGE_ACCESSOR(rebound,f::Program)
    WB_SERVICE_STORAGE_ACCESSOR(simulator,f::Simulator)
    WB_SERVICE_STORAGE_ACCESSOR(route_world,f::World)
    WB_SERVICE_STORAGE_ACCESSOR(map,r::Map)
    WB_SERVICE_STORAGE_ACCESSOR(request,r::Request)
    WB_SERVICE_STORAGE_ACCESSOR(builder,r::Builder)
    WB_SERVICE_STORAGE_ACCESSOR(rebinder,r::Rebinder)
    WB_SERVICE_STORAGE_ACCESSOR(workspace,r::Workspace)
    WB_SERVICE_STORAGE_ACCESSOR(interpreter,e::Interpreter)
    WB_SERVICE_STORAGE_ACCESSOR(actual,e::Frame)
    WB_SERVICE_STORAGE_ACCESSOR(prior,e::Frame)
    WB_SERVICE_STORAGE_ACCESSOR(actual_capture,n::Capture)
#undef WB_SERVICE_STORAGE_ACCESSOR
};
} // namespace wb::capital::service_storage
