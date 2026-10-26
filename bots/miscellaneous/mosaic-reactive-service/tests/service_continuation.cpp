// Exercise the actual factored entry dispatcher against a fresh selected kernel
// and an independent tiny host. No prior packet/certificate is reused as fact.
#define main shared_continuation_transition_main
#include "transition.cpp"
#undef main
#include "service_continuation.h"
#include <stdexcept>
#include <string>
namespace cambc::detail {
extern "C" std::int32_t sdk_camp(std::uint32_t,camp_state* out){++sdk_calls;*out={};return 0;}
extern "C" std::int32_t sdk_camp_design(std::int32_t,camp_design* out){++sdk_calls;*out={};return 0;}
}
namespace {
using namespace mosaic;
struct Ops {
    Packet host=::packet();
    std::vector<frame::SourceCamp> catalog;
    spatial::Index index;
    selected::Packet captured;
    std::optional<selected::Assessment> assessment;
    selected::Stamp now{11,2,0};
    continuation::Context ctx;
    std::optional<Command> nominee;
    continuation::Attempt response{continuation::Attempt::applied};
    std::string order;
    int captures{},attempts{},nominations{},acquisitions{};
    std::uint64_t serial{},nonce{};
    bool target_valid{true},legal{true},incomplete{},cross_input{},changed_goal{},dead_target{},throw_attempt{},burn_unattempted{},cross_on_attempt{};
    bool cross_context{},cross_target{},unknown_row{};
    Ops() {
        host.actor=2;camp(host,7,1,1,0,100,0);
        const auto& c=host.camps[0];
        auto handle=cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(c.handle),
            static_cast<cambc::handle_camp_group_t>(c.group),cambc::camp_type::wolf,c.spawn);
        catalog.push_back({handle,c});const std::array entries{spatial::Entry{7,1,0,3}};
        assert(index.build(entries,true));ctx.group=1;ctx.layout=index.borrow();
    }
    continuation::Binding binding()const{return continuation::make_binding(now.actor,host.self,ctx);}
    Command move()const {auto result=command(host,Kind::move);result.destination={0,1};return result;}
    Command basic()const{return ::basic(host,7);}
    bool current(selected::Stamp stamp)const{return now.matches(stamp);}
    std::optional<Command> nominate(const continuation::Binding&){++nominations;order+='N';return nominee;}
    selected::Request request(Command value){order+='R';return {value,++serial};}
    bool valid_target(const Command&,const continuation::Binding&) {
        if(captures&&cross_target){cross_target=false;++now.epoch;}return target_valid;
    }
    continuation::Context context(const selected::Packet&) {
        if(cross_context){cross_context=false;++now.epoch;}return ctx;
    }
    const selected::Packet& packet()const{return captured;}
    selected::Assessment& capture(const selected::Request& request) {
        ++captures;order+='C';captured={};captured.stamp=now;captured.request=request;captured.nonce=++nonce;
        captured.origin_stamp=now;captured.origin_complete=true;captured.catalog=catalog;captured.layout=index.borrow();
        captured.self=host.self;captured.complete=captured.self_complete=captured.enemies_complete=true;
        captured.move_test_complete=captured.effect_champions_clear=true;captured.move_legal=legal;
        captured.rows.push_back({0,host.camps[0],now,true,true,true,true,true,true});
        if(unknown_row){captured.rows[0].camp.known=false;captured.rows[0].state_fresh=false;}
        frame::Group group;group.id=1;group.state=frame::GroupState::alive;group.observed_round=now.epoch;
        captured.groups.push_back(group);
        if(incomplete)captured.complete=false;
        assessment.emplace(selected::assess(captured,request));
        if(changed_goal)++ctx.goal_generation;
        if(dead_target)target_valid=false;
        if(cross_input)++now.epoch;
        return *assessment;
    }
    continuation::Attempt attempt(selected::Assessment& decision,const selected::Request& request) {
        ++attempts;order+='A';if(throw_attempt)throw std::runtime_error("host refusal exception");
        if(cross_on_attempt)++now.epoch;
        if(response==continuation::Attempt::not_attempted&&!burn_unattempted)return response;
        assert(decision.certificate);
        auto granted=decision.certificate->consume(now,request,index.borrow(),host.self.pos,captured.nonce);assert(granted);
        if(response==continuation::Attempt::not_attempted)return response;
        ++now.revision;captured.complete=false;
        if(response==continuation::Attempt::applied) {
            if(granted->kind==Kind::move){host.self.pos=granted->destination;host.self.can_move=false;}
            else {assert(granted->kind==Kind::basic&&host.camps[0].alive);
                host.camps[0].hp=std::max(0.0f,host.camps[0].hp-host.self.basic_damage);
                host.camps[0].alive=host.camps[0].hp>0;host.self.can_attack=false;}
            ++ctx.receipt_generation;
        }
        return response;
    }
};
}
int main() {
    using namespace mosaic;using namespace continuation;
    int checks=0;const auto initial=sdk_calls;
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(result.outcome==Outcome::applied&&result.captured&&result.attempted&&!result.origin_wait.complete&&!pending.active());++checks;
        assert(ops.order=="RCA"&&ops.captures==1&&ops.attempts==1&&ops.nominations==0&&ops.acquisitions==0);++checks;
        assert(ops.host.self.pos==cambc::vec2(0,1));++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.basic(),bound,10));
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(result.outcome==Outcome::applied&&ops.captures==1&&ops.host.camps[0].hp==60&&!pending.active());++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain_nominee(bound,10));ops.nominee=ops.move();
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(result.outcome==Outcome::applied&&ops.order=="NRCA"&&ops.nominations==1&&ops.captures==1);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain_nominee(bound,10));
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(result.outcome==Outcome::replan&&!result.origin_wait.complete&&!pending.active()&&
            ops.order=="N"&&ops.captures==0&&ops.acquisitions==0);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));
        ops.incomplete=true;const auto deferred=dispatch_first(pending,ops.now,bound,ops);
        assert(deferred.outcome==Outcome::deferred&&deferred.origin_wait.complete&&deferred.origin_wait.safe&&
            pending.active()&&ops.captures==1&&ops.attempts==0);++checks;
        ops.incomplete=false;++ops.now.epoch;ops.order.clear();
        const auto next=dispatch_first(pending,ops.now,bound,ops);
        assert(next.outcome==Outcome::applied&&ops.captures==2&&ops.attempts==1&&ops.order=="RCA");++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));
        ops.response=Attempt::not_attempted;ops.burn_unattempted=true;
        const auto deferred=dispatch_first(pending,ops.now,bound,ops);
        assert(deferred.outcome==Outcome::deferred&&!deferred.attempted&&pending.active()&&ops.captures==1);++checks;
        ++ops.now.epoch;ops.response=Attempt::applied;
        assert(dispatch_first(pending,ops.now,bound,ops).outcome==Outcome::applied&&ops.captures==2);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));ops.response=Attempt::refused;
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(result.outcome==Outcome::refused&&result.attempted&&!result.origin_wait.complete&&
            !pending.active()&&ops.host.self.pos==bound.origin);++checks;
        assert(dispatch_first(pending,ops.now,bound,ops).outcome==Outcome::replan&&ops.captures==1);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));ops.throw_attempt=true;
        try {dispatch_first(pending,ops.now,bound,ops);assert(false);}catch(const std::runtime_error&){}
        assert(!pending.active());++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));
        ops.response=Attempt::not_attempted;ops.cross_on_attempt=true;
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(result.outcome==Outcome::replan&&!result.origin_wait.complete&&!pending.active());++checks;
    }
    // Invalidate dated semantic bindings BEFORE any acquisition/capture.
    for(int change=0;change<10;++change) {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));auto other=bound;
        switch(change) {
            case 0:++other.actor;break;case 1:++other.origin.x;break;case 2:++other.bag;break;
            case 3:++other.context.mode;break;case 4:++other.context.group;break;case 5:++other.context.group_cycle;break;
            case 6:++other.context.goal_generation;break;case 7:++other.context.receipt_generation;break;
            case 8:++other.context.body_generation;break;case 9:++other.context.channel_generation;break;
        }
        const auto result=dispatch_first(pending,ops.now,other,ops);
        assert(result.outcome==Outcome::replan&&!pending.active()&&ops.captures==0&&ops.attempts==0);++checks;
    }
    // Fresh body changes cannot hide behind a matching dated parent context.
    for(int change=0;change<9;++change) {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));
        switch(change) {
            case 0:++ops.host.self.pos.x;break;case 1:++ops.host.self.gems;break;case 2:++ops.host.self.max_hp;break;
            case 3:++ops.host.self.strength;break;case 4:++ops.host.self.focus;break;
            case 5:++ops.host.self.range;break;case 6:++ops.host.self.move_speed;break;
            case 7:ops.host.self.recalling=true;break;case 8:ops.host.self.alive=false;break;
        }
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(result.outcome==Outcome::replan&&!pending.active()&&ops.captures==1&&ops.attempts==0);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));
        ops.host.self.hp-=10;
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(result.outcome==Outcome::applied&&ops.captures==1&&ops.attempts==1);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));
        ops.host.self.hp=30;ops.host.camps[0].strength=40;
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(result.outcome==Outcome::replan&&!pending.active()&&ops.captures==1&&ops.attempts==0&&
            result.origin_wait.complete&&!result.origin_wait.safe&&result.origin_wait.next_volley==40);++checks;
        assert(dispatch_first(pending,ops.now,bound,ops).outcome==Outcome::replan&&ops.captures==1);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));ops.legal=false;
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(result.outcome==Outcome::replan&&!pending.active()&&ops.captures==1&&ops.attempts==0&&
            result.origin_wait.complete&&result.origin_wait.safe);++checks;
        assert(dispatch_first(pending,ops.now,bound,ops).outcome==Outcome::replan&&ops.captures==1);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));ops.unknown_row=true;
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(ops.captured.complete&&ops.assessment->result.status==selected::Status::incomplete&&
            result.outcome==Outcome::replan&&!pending.active()&&!result.origin_wait.complete&&ops.attempts==0);++checks;
        assert(dispatch_first(pending,ops.now,bound,ops).outcome==Outcome::replan&&ops.captures==1);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));ops.changed_goal=true;
        assert(dispatch_first(pending,ops.now,bound,ops).outcome==Outcome::replan&&!pending.active()&&ops.attempts==0);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.basic(),bound,10));ops.dead_target=true;
        assert(dispatch_first(pending,ops.now,bound,ops).outcome==Outcome::replan&&!pending.active()&&ops.attempts==0);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));ops.cross_input=true;
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(result.outcome==Outcome::replan&&!result.origin_wait.complete&&!pending.active()&&ops.attempts==0);++checks;
    }
    for(bool target_callback:{false,true}) {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.move(),bound,10));
        ops.cross_context=!target_callback;ops.cross_target=target_callback;
        const auto result=dispatch_first(pending,ops.now,bound,ops);
        assert(result.outcome==Outcome::replan&&!result.origin_wait.complete&&!pending.active()&&ops.attempts==0&&
            ops.assessment&&!ops.assessment->certificate);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();auto wrong=ops.move();wrong.kind=Kind::wait;
        assert(!pending.retain(wrong,bound,10)&&!pending.active());++checks;
        assert(pending.retain(ops.move(),bound,10));ops.index.revoke();
        assert(dispatch_first(pending,ops.now,bound,ops).outcome==Outcome::replan&&ops.captures==0);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain_nominee(bound,10));
        auto unsupported=ops.move();unsupported.kind=Kind::cast;ops.nominee=unsupported;
        assert(dispatch_first(pending,ops.now,bound,ops).outcome==Outcome::replan&&!pending.active()&&ops.captures==0);++checks;
    }
    {
        Ops ops;Pending pending;const auto bound=ops.binding();assert(pending.retain(ops.basic(),bound,10));ops.target_valid=false;
        assert(dispatch_first(pending,ops.now,bound,ops).outcome==Outcome::replan&&!pending.active()&&ops.captures==0);++checks;
    }
    assert(sdk_calls==initial);++checks;
    std::printf("service continuation checks: %d passed; SDK calls: %d\n",checks,sdk_calls-initial);
}
