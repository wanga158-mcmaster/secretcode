#ifndef ACTOR_RUNTIME_HEADER
#define ACTOR_RUNTIME_HEADER "fixture-runtime.h"
#endif
#include ACTOR_RUNTIME_HEADER
#include "../catalyst-forge/src/c-policy.h"
#include <cassert>
#include <iostream>
extern "C" {
__attribute__((import_module("fixture"),import_name("reset"))) void fixture_reset(int scenario);
__attribute__((import_module("fixture"),import_name("metric"))) int fixture_metric(int metric);
}
using namespace p10;
struct Policy {Directive step(const World&,Arm){return wait();}};
static World frame(){World w;w.round=7;w.self.id=0;w.self.type=Champion::varg;w.self.position={1,1};w.self.alive=w.self.visible=w.self.ally=w.self.can_attack=w.self.can_cast=true;w.self.hp=w.self.max_hp=1000;w.geometry_complete=w.actor_observation_complete=w.observation_complete=w.opposition_complete=true;return w;}
static Directive attack(){Directive d;d.task=Task::attack;d.actor=1;d.cast_slot=0;d.strict_tank=true;d.valid_round=7;return d;}
static void invoke(Arm arm,int scenario,const World&w,const Directive&d){fixture_reset(scenario);Runtime<Policy> runtime(Profile{},arm);runtime.execute(w,d);}
static void expect(int basics,int casts,int attempts,int sequence){assert(fixture_metric(0)==basics);assert(fixture_metric(1)==casts);assert(fixture_metric(2)==attempts);assert(fixture_metric(3)==sequence);}
static void both_arms(){for(Arm arm:{Arm::active,Arm::control}){auto w=frame();auto d=attack();
 invoke(arm,0,w,d);expect(2,1,1,121);
 invoke(arm,1,w,d);expect(1,1,1,21);
 invoke(arm,2,w,d);expect(1,0,0,1);
 invoke(arm,3,w,d);expect(1,0,0,1);
 invoke(arm,4,w,d);expect(1,0,1,1);
 invoke(arm,5,w,d);expect(0,0,0,0);
 invoke(arm,6,w,d);expect(0,0,0,0);
 invoke(arm,7,w,d);expect(1,0,0,1);
 invoke(arm,8,w,d);expect(1,1,1,12);
 auto collateral=w;Group g;g.id=4;g.observed=g.complete=true;Monster m;m.alive=m.observed=true;m.hp=100;m.position={2,2};m.strength=400;m.range=3;g.monsters={m};collateral.groups={g};invoke(arm,0,collateral,d);expect(0,0,0,0);
 auto unknown=collateral;unknown.groups[0].complete=false;invoke(arm,0,unknown,d);expect(0,0,0,0);
 auto certified=d;certified.safety_verified=true;certified.safety_round=7;certified.minimum_start_hp=1000;invoke(arm,0,w,certified);expect(2,1,1,121);
 certified.safety_round=6;invoke(arm,0,w,certified);expect(0,0,0,0);
 certified.safety_round=7;certified.minimum_start_hp=1001;invoke(arm,0,w,certified);expect(0,0,0,0);
 auto stale=d;stale.valid_round=6;invoke(arm,0,w,stale);expect(0,0,0,0);
 certified.minimum_start_hp=1000;auto no_geometry=w;no_geometry.geometry_complete=false;invoke(arm,0,no_geometry,certified);expect(0,0,0,0);
 auto no_actors=w;no_actors.actor_observation_complete=false;invoke(arm,0,no_actors,certified);expect(0,0,0,0);
 auto local=w;local.observation_complete=false;certified.local_safety_verified=true;invoke(arm,0,local,certified);expect(2,1,1,121);
 auto damage=d;damage.cast_slot=1;invoke(arm,0,w,damage);expect(1,1,1,21);
 auto cast=d;cast.task=Task::cast;invoke(arm,0,w,cast);expect(0,0,0,0);invoke(arm,1,w,cast);expect(0,1,1,2);
 }std::cout<<"PASS actual staged Runtime::execute both arms actor basic/reset/basic, spent allowance, cooldown, first-hit kill, failed reset, range, channel, round drift, collateral and fresh-certificate constraints; Task::cast unchanged\n";}
static void retained_camp_compound(){for(Arm arm:{Arm::active,Arm::control}){fixture_reset(0);Runtime<Policy> runtime(Profile{},arm);auto w=frame();w.self.type=Champion::vendetta;auto d=attack();d.actor=-1;d.monster=100;d.second_monster=101;d.group=4;d.reserve_reset=true;std::vector<cambc::camp> camps;
 for(int id:{100,101})camps.push_back(cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(id),static_cast<cambc::handle_camp_group_t>(4),cambc::camp_type::wolf,{2,id==100?1:2}));
 auto group=cambc::detail::handle_access::make_camp_group(static_cast<cambc::handle_camp_group_t>(4),40,14,40,std::move(camps));runtime.nav_.sources.push_back({std::move(group),{},{},{}});runtime.execute(w,d);expect(2,1,1,121);
 }std::cout<<"PASS actual Runtime camp basic/reset/second-monster compound retained for both arms\n";}
static void deferred_partial_reporting(){for(Arm arm:{Arm::active,Arm::control}){fixture_reset(0);Runtime<p10_c_forge::CPolicy> runtime(Profile{},arm);auto&s=runtime.policy_.partial_diagnostic_;s.sequence=1;s.actor=0;s.round=6;s.completed=true;s.calls=1;s.source=118;s.proof.completed=true;s.proof.failure=p10_partial::Failure::fuel;s.proof.stage=p10_partial::ProofStage::escape;s.runtime_gate=p10_c_forge::CPolicy::RuntimeGate::execute_floor;
 runtime.critical_turn_=true;runtime.partial_report(7);assert(runtime.partial_report_pending_&&runtime.partial_report_.round==6);
 s.sequence=2;s.round=7;s.source=151;s.proof.failure=p10_partial::Failure::none;s.proof.stage=p10_partial::ProofStage::complete;runtime.partial_report(8);assert(runtime.partial_report_.round==6&&runtime.partial_report_.source==118);
 runtime.critical_turn_=false;runtime.partial_report(9);assert(!runtime.partial_report_pending_&&runtime.partial_report_round_==9&&runtime.partial_report_failure_==int(p10_partial::Failure::fuel));
 runtime.partial_gate(7,2,41000);assert(s.runtime_gate==p10_c_forge::CPolicy::RuntimeGate::execute_floor&&s.execute_floor==41000);runtime.partial_gate(8,3);assert(s.runtime_gate==p10_c_forge::CPolicy::RuntimeGate::execute_floor);
 runtime.partial_gate(7,5,41000);runtime.partial_gate(7,6,41000);assert(s.execute_before==100000&&s.execute_after==100000);assert(!runtime.execution_handoff(7,100001)&&s.completed&&s.runtime_gate==p10_c_forge::CPolicy::RuntimeGate::handoff_floor);s.round=6;assert(!runtime.execution_handoff(6,41000)&&!s.completed&&s.runtime_gate==p10_c_forge::CPolicy::RuntimeGate::suspended);s.round=7;assert(runtime.execution_handoff(7,41000));expect(0,0,0,0);
 }std::cout<<"PASS actual Runtime deferred partial snapshot survives critical escape, matched-round gates and cross-owner cost censoring; no actions\n";}
static void bounded_immutable_envelopes(){Navigation nav;nav.complete=true;for(int id:{72,73}){Group g;g.id=id;for(int i=0;i<2;++i){Monster m;m.id=id*10+i;m.position={10+i,8};m.range=3;g.monsters.push_back(m);}auto geometry=std::make_shared<SourceGeometry>();geometry->source=id;for(const auto&m:g.monsters)geometry->members.push_back({m.id,m.position});g.source_geometry=geometry;auto raw=cambc::detail::handle_access::make_camp_group(static_cast<cambc::handle_camp_group_t>(id),0,0,20,{});nav.sources.push_back({std::move(raw),{},{},g});}
 fixture_reset(10);assert(!nav.prepare_envelopes(7,41000));assert(nav.envelope_cursor==1&&nav.envelope_member==1);assert(nav.sources[0].metadata.hazard_envelope.members==2&&nav.sources[1].metadata.hazard_envelope.members==1);assert(!hazard_envelope_disjoint(nav.sources[1].metadata,7,0,5,0,4,0));
 fixture_reset(11);assert(nav.prepare_envelopes(8,41000));for(const auto&source:nav.sources){auto current=source.metadata;assert(current.hazard_envelope.valid&&current.hazard_envelope.members==2);current.hazard_envelope.round=8;assert(hazard_envelope_disjoint(current,8,0,5,0,4,0));auto rebuilt=current;begin_hazard_envelope(rebuilt,8);for(std::size_t i=0;i<rebuilt.monsters.size();++i)extend_hazard_envelope(rebuilt,rebuilt.monsters[i],i);assert(rebuilt.hazard_envelope.minimum==current.hazard_envelope.minimum&&rebuilt.hazard_envelope.maximum==current.hazard_envelope.maximum);}
 expect(0,0,0,0);std::cout<<"PASS actual Navigation immutable envelope warmup resumes after falling fuel on a fresh owner; partial bounds stay unavailable\n";}
static void finite_basic_calibration(){for(Arm arm:{Arm::active,Arm::control}){
 fixture_reset(14);Runtime<p10_c_forge::CPolicy> runtime(Profile{},arm);runtime.reserve_=41000;
 auto w=frame();w.self.type=Champion::vendetta;w.self.strength=40;w.self.range=1;w.action_reserve=41000;w.fuel=52000;w.visible_open_tiles={{1,1}};
 Group g;g.id=4;g.observed=g.complete=g.living=g.route_known=true;g.position={2,1};g.spawns={{2,1}};g.approach_pads={{1,1}};g.travel=g.to_bank=0;
 Monster m;m.id=100;m.group=4;m.position={2,1};m.hp=m.max_hp=40;m.alive=m.observed=true;g.monsters={m};w.groups={g};runtime.policy_.source=4;
 // The real CPolicy chooses its prefix, proves it, and stamps only that result.
 const auto d=runtime.policy_.step(w,arm);assert(d.task==Task::attack&&d.finite_basic_prefix&&d.cast_slot<0&&d.second_monster<0);
 assert(runtime.policy_.partial_snapshot().proof.action_reserve==30000&&runtime.execution_floor(w,d)==30000);
 auto bad=d;bad.cast_slot=0;assert(runtime.execution_floor(w,bad)==41000);bad=d;bad.strict_tank=false;assert(runtime.execution_floor(w,bad)==41000);
 bad=d;bad.actor=1;assert(runtime.execution_floor(w,bad)==41000);bad=d;bad.priority=1;assert(runtime.execution_floor(w,bad)==41000);
 bad=d;bad.safety_round=6;assert(runtime.execution_floor(w,bad)==41000);bad=d;bad.monster=101;assert(runtime.execution_floor(w,bad)==41000);
 auto missing=w;missing.groups[0].complete=false;assert(runtime.execution_floor(missing,d)==41000);
 auto melee=w;melee.self.type=Champion::varg;assert(runtime.execution_floor(melee,d)==41000);
 std::vector<cambc::camp> camps{cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(100),static_cast<cambc::handle_camp_group_t>(4),cambc::camp_type::wolf,{2,1})};
 auto native=cambc::detail::handle_access::make_camp_group(static_cast<cambc::handle_camp_group_t>(4),40,0,40,std::move(camps));runtime.nav_.sources.push_back({std::move(native),{},{},g});
 fixture_reset(13);assert(!runtime.execution_handoff(7,runtime.execution_floor(w,d)));expect(0,0,0,0);
 fixture_reset(12);assert(runtime.execution_handoff(7,runtime.execution_floor(w,d)));runtime.execute(w,d);expect(1,0,0,1);
 fixture_reset(15);Runtime<p10_c_forge::CPolicy> declining(Profile{},arm);declining.reserve_=41000;declining.policy_.source=4;
 const auto fallback=declining.policy_.step(w,arm);assert(fallback.task==Task::wait&&!fallback.finite_basic_prefix);
 const auto&proof=declining.policy_.partial_snapshot().proof;assert(proof.action_reserve==30000&&!proof.feasible&&proof.failure==p10_partial::Failure::fuel);
 assert(declining.execution_floor(w,fallback)==41000);expect(0,0,0,0);
 fixture_reset(17);Runtime<p10_c_forge::CPolicy> suffix(Profile{},arm);suffix.reserve_=41000;suffix.policy_.source=4;
 auto exposed=w;exposed.visible_open_tiles.push_back({0,1});auto&threat=exposed.groups[0].monsters[0];threat.hp=threat.max_hp=80;threat.range=1;threat.strength=40;
 const auto held=suffix.policy_.step(exposed,arm);const auto&held_proof=suffix.policy_.partial_snapshot().proof;
 assert(held.task==Task::wait&&!held.finite_basic_prefix&&!suffix.policy_.partial_pending&&held_proof.feasible&&held_proof.action_reserve==30000);
 assert(suffix.execution_floor(exposed,held)==41000);expect(0,0,0,0);
 }std::cout<<"PASS actual CPolicy one-basic witness to Runtime calibrated handoff/execute; compound, actor, movement, priority, stale and missing-detail refusals; failed planning restores ordinary floor. Native cost unmeasured\n";}
int main(){both_arms();retained_camp_compound();deferred_partial_reporting();bounded_immutable_envelopes();finite_basic_calibration();}
