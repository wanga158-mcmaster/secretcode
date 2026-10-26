#include "service_model.h"
#include <cstdio>
namespace tt {
using vec2=mb::service::Point;
inline int distance(vec2 a,vec2 b) { return mb::service::distance(a,b); }
enum class Status { Ready,NeedCompute,NeedObservation,Stale,Unreachable };
struct Observation { Status status{Status::Ready}; int epoch{7}; vec2 pos{4,4}; };
struct WorkObservation { Status status{Status::Ready}; int epoch{7}; mb::service::State state; };
struct Applied { bool moved{},attacked{},cast{}; };
}
#define TT_ACTIONS_FIXTURE
#include "../src/independent_actions.cpp"

namespace {
using namespace tt;
using namespace mb::service;
int failures{};
void check(bool value,const char* text) { if (!value) { std::printf("FAIL %s\n",text); ++failures; } }
struct Backend
{
    int now{7},attacks{},casts{},last_group{-1},last_member{-1};
    vec2 origin{4,4},last_aim;
    bool allowance{true},cast_allowance{true},visible{true},living{true},fuel{true},cross_epoch{},refuse{};
    float hp{50};
    actions_detail::CastInfo info{SpellKind::damage,true,false,true,false,false,4};
    int epoch() const { return now; }
    vec2 position() const { return origin; }
    bool budget() const { return fuel; }
    bool alive() const { return true; }
    bool channeling() const { return false; }
    bool can_attack() const { return allowance; }
    bool can_cast() const { return cast_allowance; }
    int attack_range() const { return 1; }
    bool inside(vec2 p) const { return p.x>=0 && p.y>=0 && p.x<10 && p.y<10; }
    actions_detail::Target target(int,int)
    { if (cross_epoch) ++now; return {visible,living,{5,4},hp}; }
    actions_detail::CastInfo cast_info(int) const { return info; }
    void attack(int group,int member) { ++attacks; last_group=group; last_member=member; }
    bool cast(int,SpellKind,int group,int member,vec2 aim)
    { last_group=group; last_member=member; last_aim=aim; if (refuse) return false; ++casts; return true; }
};
WorkObservation frame()
{
    WorkObservation work;
    work.state.body.pos={4,4}; work.state.member_count=1;
    work.state.members[0]={13,2,{5,4},50,20,3,false,0,true};
    return work;
}
}
int main()
{
    using namespace tt;
    using namespace mb::service;
    check(!actions_detail::stun_protects(false,5) && !actions_detail::stun_protects(false,0),
          "unverified global stun never removes a possible incoming camp source");
    check(actions_detail::stun_protects(true,5) && !actions_detail::stun_protects(true,0) &&
          !actions_detail::stun_protects(true,-1),"only exact positive remaining stun credits protection");
    Observation observation; auto work=frame(); Applied applied; Backend backend;
    Primitive basic{PrimitiveKind::basic,13,-1,{5,4}};
    check(actions_detail::guarded_apply(observation,work,basic,applied,backend)==Status::Ready &&
          backend.attacks==1 && backend.last_group==2 && backend.last_member==13 && applied.attacked &&
          observation.status==Status::NeedObservation,"actual caller resolves sparse stable ID and requires observation after applied basic");
    // The caller collected a NEW active work frame, not a new global observation.
    work=frame(); work.state.members[0].hp=10; work.state.body.can_attack=false;
    backend.hp=10; backend.allowance=false;
    backend.info={SpellKind::reset,true,true,false,false,false,0};
    Primitive continuation_reset{PrimitiveKind::cast,-1,0,{4,4}};
    check(observation.status==Status::NeedObservation &&
          actions_detail::guarded_apply(observation,work,continuation_reset,applied,backend)==Status::Ready &&
          backend.casts==1 && applied.cast && observation.status==Status::NeedObservation,
          "fresh active work authorizes same-turn reset without falsely republishing global observation Ready");
    work=frame(); work.state.members[0].hp=10; backend.allowance=true;
    check(actions_detail::guarded_apply(observation,work,basic,applied,backend)==Status::Ready &&
          backend.attacks==2 && observation.status==Status::NeedObservation,
          "fresh post-reset work permits next basic under still-invalid global snapshot");
    work=frame();
    observation={}; applied={}; backend={}; backend.origin={7,7};
    check(actions_detail::guarded_apply(observation,work,basic,applied,backend)==Status::NeedObservation && !backend.attacks,
          "displacement invalidates frame before attack");
    observation={}; backend={}; backend.cross_epoch=true;
    check(actions_detail::guarded_apply(observation,work,basic,applied,backend)==Status::Stale && !backend.attacks,
          "fuel-style epoch change during target query cannot authorize later action");
    observation={}; backend={}; backend.hp=70;
    check(actions_detail::guarded_apply(observation,work,basic,applied,backend)==Status::NeedObservation && !backend.attacks,
          "actual target residual mismatch requires new work observation");
    observation={}; backend={}; backend.visible=false;
    check(actions_detail::guarded_apply(observation,work,basic,applied,backend)==Status::NeedObservation && !backend.attacks,
          "hidden target cannot be attacked from cached frame");
    observation={}; backend={}; backend.allowance=false;
    check(actions_detail::guarded_apply(observation,work,basic,applied,backend)==Status::NeedObservation && !backend.attacks,
          "live attack allowance is checked after planning");
    observation={}; backend={}; backend.fuel=false;
    check(actions_detail::guarded_apply(observation,work,basic,applied,backend)==Status::NeedCompute && !backend.attacks,
          "compute withdrawal never manufactures applied command");
    observation={}; backend={}; backend.info={SpellKind::aoe,true,false,false,true,true,4}; applied={};
    Primitive aoe{PrimitiveKind::cast,-1,0,{7,6}};
    check(actions_detail::guarded_apply(observation,work,aoe,applied,backend)==Status::Ready && backend.casts==1 &&
          backend.last_aim==vec2{7,6} && applied.cast,"AoE passes absolute destination through actual caller, never displacement delta");
    observation={}; backend={}; backend.info={SpellKind::aoe,true,false,false,true,true,4}; applied={true,false,false};
    check(actions_detail::guarded_apply(observation,work,aoe,applied,backend)==Status::NeedObservation && !backend.casts,
          "exclusive ability is rejected after actual move flag");
    observation={}; backend={}; backend.info={SpellKind::reset,true,true,false,false,false,0}; applied={};
    Primitive reset{PrimitiveKind::cast,-1,0,{4,4}};
    check(actions_detail::guarded_apply(observation,work,reset,applied,backend)==Status::NeedObservation && !backend.casts,
          "unspent basic cannot justify reset");
    backend.allowance=false;
    check(actions_detail::guarded_apply(observation,work,reset,applied,backend)==Status::Ready && backend.casts==1 && applied.cast,
          "actual spent allowance admits self reset");
    observation={}; backend={}; backend.info={SpellKind::shield,true,true,false,false,false,0}; backend.refuse=true; applied={};
    check(actions_detail::guarded_apply(observation,work,reset,applied,backend)==Status::NeedObservation && !applied.cast && !backend.casts,
          "refused cast does not consume a fabricated successful allowance");
    std::printf("independent actions: %d failures\n",failures);
    return failures ? 1 : 0;
}
