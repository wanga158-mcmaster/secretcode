#pragma once
// The lifetime of a static cash field is independent of an actor's dated
// forecast. Keep this object outside body/Life/query reset state. Its only
// dependencies are the exact sealed static object/support, goals and limits.
// Ready routes remain immutable nominations, never physical action authority.
#include "assignment_cash_routes.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace tt::assignment_cash_cache {
namespace routes=assignment_cash_routes;
namespace geometry=assignment_static_geometry;
using Status=routes::Status;
using Reason=routes::Reason;
using Goals=routes::Goals;
using Key=routes::Key;
using Limits=routes::Limits;

class Cache {
public:
    Cache()=default;
    Cache(const Cache&)=delete;Cache& operator=(const Cache&)=delete;
    Cache(Cache&&)=delete;Cache& operator=(Cache&&)=delete;

    // Matching a failed request also retains its typed failure: a new date or
    // body input cannot retry/restart allocation or manufacture route progress.
    // Creating a Builder does no map traversal; advance owns its admitted work.
    Status ensure(std::shared_ptr<const geometry::StaticGeometry> support,
                  Goals goals,Limits limits={}) {
        last_work_=0;
        Key requested{support.get(),support?support->key():geometry::StaticKey{},goals};
        if(bound_&&requested==key_&&same_limits(limits,limits_))return status();
        builder_.reset();
        geometry_=std::move(support);key_=requested;limits_=limits;bound_=true;
        status_=Status::Unsupported;reason_=Reason::None;
        try {
            builder_=std::make_unique<routes::Builder>(geometry_,goals,limits_);
            status_=builder_->status();reason_=builder_->reason();
        } catch(const std::bad_alloc&) {reason_=Reason::Allocation;}
          catch(const std::length_error&) {reason_=Reason::Capacity;}
        return status_;
    }

    Status advance(std::size_t units) {
        last_work_=0;
        if(builder_) {
            status_=builder_->advance(units);reason_=builder_->reason();
            last_work_=builder_->last_work();
        }
        return status_;
    }
    Status status() const{return status_;}
    Reason reason() const{return reason_;}
    std::size_t last_work() const{return last_work_;}
    // Work is the current exact dependency lifetime, not a session-wide sum.
    std::uint64_t work_used() const{return builder_?builder_->work_used():0;}
    std::shared_ptr<const routes::Routes> result() const {
        return status_==Status::Ready&&builder_?builder_->result():nullptr;
    }
    const Key& key() const{return key_;}
    const Limits& limits() const{return limits_;}

    // Explicit world/support handoff only. Destroying a body forecast is not a
    // reason to call this. Caller-admitted teardown may release large fields;
    // previously returned shared Routes continue to pin their exact geometry.
    void reset() {
        builder_.reset();geometry_.reset();key_={};limits_={};bound_=false;
        status_=Status::Unsupported;reason_=Reason::Shape;last_work_=0;
    }
private:
    static bool same_limits(const Limits& a,const Limits& b) {
        return a.cells==b.cells&&a.points==b.points;
    }
    std::shared_ptr<const geometry::StaticGeometry> geometry_;
    std::unique_ptr<routes::Builder> builder_;
    Key key_;Limits limits_;
    Status status_{Status::Unsupported};Reason reason_{Reason::Shape};
    bool bound_{};std::size_t last_work_{};
};
}
