#include "../common/src/combat.h"
#include "../common/src/guard.h"
#include <cassert>
#include <cstdio>
using namespace p10;
static int depleted_work_fuel(){return 0;}
static void fresh_envelope_boundaries(){
 Group g;g.id=72;Monster a;a.id=1;a.position={10,8};a.range=3;a.alive=false;Monster b=a;b.id=2;b.position={20,8};b.range=1;g.monsters={a,b};
 auto geometry=std::make_shared<SourceGeometry>();geometry->source=g.id;for(const auto&m:g.monsters)geometry->members.push_back({m.id,m.position});g.source_geometry=geometry;
 begin_hazard_envelope(g,10);extend_hazard_envelope(g,a,0);assert(!hazard_envelope_disjoint(g,10,0,6,0,4,0)); // unfinished builders cannot skip
 extend_hazard_envelope(g,b,1);assert(g.hazard_envelope.minimum==Point(7,5)&&g.hazard_envelope.maximum==Point(21,11));
 assert(hazard_envelope_disjoint(g,10,0,6,5,11,0));assert(!hazard_envelope_disjoint(g,10,0,7,5,11,0));assert(!hazard_envelope_disjoint(g,10,0,6,5,11,1)); // touching reach/collateral stays exact
 assert(!hazard_envelope_disjoint(g,11,0,6,5,11,0));assert(!hazard_envelope_disjoint(g,10,0,6,5,11,-1));assert(!hazard_envelope_disjoint(g,10,0,6,5,11,std::numeric_limits<double>::infinity()));
 g.source_geometry=std::make_shared<SourceGeometry>(*geometry);assert(!hazard_envelope_disjoint(g,10,0,6,5,11,0));
 begin_hazard_envelope(g,11);auto invalid=a;invalid.range=-1;extend_hazard_envelope(g,invalid,0);assert(!g.hazard_envelope.valid);
 begin_hazard_envelope(g,11);invalid=a;invalid.range=std::numeric_limits<int>::max();extend_hazard_envelope(g,invalid,0);assert(!g.hazard_envelope.valid);
 begin_hazard_envelope(g,11);invalid=a;invalid.id=99;extend_hazard_envelope(g,invalid,0);assert(!g.hazard_envelope.valid);
 begin_hazard_envelope(g,11);extend_hazard_envelope(g,b,1);assert(!g.hazard_envelope.valid);
 g.source_geometry.reset();begin_hazard_envelope(g,11);assert(!g.hazard_envelope.valid);
}
int main(){
 fresh_envelope_boundaries();
 World w;w.tribute_multiplier=.1;assert(w.ticks(1)==1&&w.ticks(10)==1&&w.ticks(11)==2);
 assert(team_starts(false,1)&&!team_starts(true,1)&&!team_starts(false,2)&&team_starts(true,2));
 assert(equivalent_shards(4,0)==4&&equivalent_shards(0,1)==5&&equivalent_shards(2,2)==12&&equivalent_shards(2,2)%5==2);
 w.tribute_base=2;w.tribute_minimum=4;assert(w.ticks(1)==4&&w.ticks(21)==5);
 int round=7,moves=0;assert(guarded_action(7,[&]{return round;},[]{return true;},[&]{++moves;}));
 assert(moves==1);assert(!guarded_action(7,[&]{return round;},[&]{++round;return true;},[&]{++moves;}));assert(moves==1);
 assert(!guarded_action(8,[&]{return round;},[]{return false;},[&]{++moves;}));assert(moves==1);
 assert(action_reserve(100000)==40000&&action_reserve(200000)==80000&&action_reserve(100000,45000,5000)==50000);
 Actor a;a.hp=a.max_hp=100;a.strength=10;a.range=1;
 Group g;g.complete=g.program_available=true;Monster near;near.alive=true;near.hp=near.max_hp=20;near.strength=10;near.range=1;near.position={1,0};g.monsters.push_back(near);
 Monster distant=near;distant.position={5,0};distant.strength=100;distant.range=3;g.monsters.push_back(distant);
 auto f=solo_program(a,g,{0,0});assert(!f.feasible); // cannot silently omit unreachable member
 g.monsters.pop_back();f=solo_program(a,g,{0,0});assert(f.feasible&&f.turns==2&&f.hp==90); // kill precedes next camp phase
 g.monsters.front().range=0;f=solo_program(a,g,{0,0});assert(f.feasible&&f.hp==100); // actual reach per member
 g.complete=false;assert(!solo_program(a,g,{0,0}).feasible);g.complete=true;
 g.monsters.resize(65,near);assert(!solo_program(a,g,{0,0}).feasible); // whole oversized program unavailable
 // Expiry is absolute and real design cooldowns govern reuse. A stale shield
 // must not certify damage, while a current one expires after its real phase.
 g.monsters={near};g.monsters[0].hp=40;g.monsters[0].strength=30;
 a.hp=a.max_hp=50;a.shield=100;a.shield_until=101;
 assert(!solo_program(a,g,{0,0},16,101).feasible);
 assert(!solo_program(a,g,{0,0},16,100).feasible);
 a.shield_until=104;assert(solo_program(a,g,{0,0},16,100).feasible);
 a={};a.hp=a.max_hp=100;a.strength=10;a.range=1;a.focus=10;
 a.abilities[0].damage=true;a.abilities[0].range=1;a.abilities[0].modifier=1;a.abilities[0].max_cooldown=20;
 g.monsters[0].id=17;g.monsters[0].hp=20;g.monsters[0].strength=10;
 auto compound=solo_program(a,g,{0,0},16,100);assert(compound.feasible&&compound.turns==1);
 assert(compound.first.task==Task::cast&&compound.first.cast_slot==0&&compound.first.second_monster==17&&compound.first.reserve_reset);
 a.abilities[0].targets_camps=false;assert(solo_program(a,g,{0,0},16,100).turns==2);a.abilities[0].targets_camps=true;
 g.monsters[0].hp=500;a.hp=a.max_hp=10000;a.focus=100;
 a.abilities[0].max_cooldown=20;const auto slow=solo_program(a,g,{0,0},64,100);
 a.abilities[0].max_cooldown=8;const auto fast=solo_program(a,g,{0,0},64,100);
 assert(slow.feasible&&fast.feasible&&slow.turns>fast.turns);
 a.abilities={};a.type=Champion::ren;g.monsters={near,near};g.monsters[0].hp=g.monsters[1].hp=10;g.monsters[0].strength=g.monsters[1].strength=0;
 assert(solo_program(a,g,{0,0},8,100).turns==2); // harmless attackers pay no retaliation
 a.type=Champion::other;g.monsters={near};g.monsters[0].id=17;g.monsters[0].hp=20;
 a.hp=a.max_hp=100;World proof;proof.round=100;proof.self=a;proof.self.position={0,0};g.id=7;g.observed=g.route_known=true;g.monsters[0].observed=true;proof.groups={g};
 auto certified=stationary_work(proof,g,{0,0});assert(certified.feasible&&fresh_certificate(proof,certified.first));
 proof.observation_complete=false;assert(fresh_certificate(proof,certified.first));
 auto legacy=certified.first;legacy.local_safety_verified=false;assert(!fresh_certificate(proof,legacy));
 proof.actor_observation_complete=false;assert(!fresh_certificate(proof,certified.first)&&!stationary_work(proof,g,{0,0}).feasible);proof.actor_observation_complete=true;
 Group remote=g;remote.id=9;remote.observed=remote.complete=false;remote.travel=1;remote.monsters[0].position={20,20};remote.monsters[0].observed=false;remote.monsters[0].strength=100;
 proof.groups.push_back(remote);assert(stationary_work(proof,g,{0,0}).feasible&&partial_walk_safe(proof,{0,1}));
 proof.groups.back().monsters[0].position={1,1};assert(!stationary_work(proof,g,{0,0}).feasible&&!partial_walk_safe(proof,{0,1})&&possible_phase_damage(proof,{0,0})==100);
 proof.groups.back().complete=true;proof.groups.back().monsters[0].observed=true;proof.groups.back().monsters[0].awake=false;assert(partial_walk_safe(proof,{0,1})&&possible_phase_damage(proof,{0,0})==0);
 int movement_reads=0;assert(!partial_walk_safe(proof,{0,1},[&]{return ++movement_reads<2;}));assert(movement_reads==2);
 auto movement=proof;movement.self.max_hp=100;movement.groups.front().monsters.clear();auto&hazard=movement.groups.back().monsters.front();hazard.awake=true;hazard.strength=10;
 movement.self.hp=28;assert(!partial_walk_safe(movement,{0,1}));movement.self.hp=28.1;assert(partial_walk_safe(movement,{0,1}));
 movement.groups.back().complete=false;assert(!partial_walk_safe(movement,{0,1})); // unknowns never receive an optimistic fuel/safety stamp
 proof.groups.pop_back();proof.observation_complete=true;
 set_work_fuel_reader(depleted_work_fuel);assert(!stationary_work(proof,g,{0,0}).feasible);set_work_fuel_reader(nullptr);
 proof.round++;assert(!fresh_certificate(proof,certified.first));proof.round--;
 Group neighbor=g;neighbor.id=8;neighbor.monsters[0].strength=100;proof.groups.push_back(neighbor);
 assert(!stationary_work(proof,g,{0,0}).feasible);
 EconomicInbox inbox;inbox.note({Receipt::Kind::payout,3,7,1});inbox.note({Receipt::Kind::purchase,4,-1,1});
 std::vector<Receipt> history(8);inbox.append(history);assert(history.size()==10&&inbox.has(Receipt::Kind::payout)&&inbox.has(Receipt::Kind::purchase));
 inbox.acknowledge();assert(!inbox.has(Receipt::Kind::payout)&&!inbox.has(Receipt::Kind::purchase));
 a={};a.hp=a.max_hp=100;a.strength=10;a.range=1;
 g.monsters.resize(1);g.monsters.front().hp=100;g.monsters.front().strength=90;g.monsters.front().range=1;assert(!pulse_survives(a,g,{0,0},{0,1}));
 g.id=1;g.observed=g.complete=true;g.monsters.front().observed=true;g.monsters.front().awake=true;g.monsters.front().strength=20;w.groups={g};
 Group other=g;other.id=2;other.monsters.front().strength=30;w.groups.push_back(other);
 assert(pulse_phase_damage(w,1,{0,0})==50); // source plus every known awake overlapping group
 assert(!collateral_survives(w,{1,0},1,1,{0,0},80));
 w.groups[1].complete=false;assert(!collateral_survives(w,{1,0},1,1,{0,0},10000));
 w.groups[1].complete=true;w.groups[1].monsters.front().position={5,0};
 assert(collateral_survives(w,{5,0},1,1,{0,0},80));
 assert(!collateral_survives(w,{5,0},1,1,{4,0},80)); // dash landing prices its new exposure point
 World collection;collection.self.position={0,0};Group far;far.id=30;far.travel=1;Group focus=far;focus.id=20;focus.travel=10;Group held=far;held.id=10;held.travel=50;Group threat=far;threat.id=40;threat.travel=5;threat.monsters={near};threat.monsters[0].position={1,0};threat.monsters[0].strength=20;threat.monsters[0].range=1;
 collection.groups={far,focus,held,threat};assert((observation_order(collection,10,20)==std::vector<std::size_t>{2,3,1,0}));
 collection.groups[0].observed=collection.groups[3].observed=true;
 assert((observation_order(collection,10,20,true)==std::vector<std::size_t>{3,0}));
 assert(collection.groups.size()==4&&!collection.groups[1].complete); // filtering work never removes unknown catalogue hazards
 assert(observation_required(collection,held,10,20)&&observation_required(collection,focus,10,20)&&observation_required(collection,threat,10,20));
 assert(!observation_required(collection,far,10,20));
 Group exit_neighbor=threat;exit_neighbor.id=50;exit_neighbor.monsters[0].position={-6,0};exit_neighbor.monsters[0].range=4;
 assert(!observation_required(collection,exit_neighbor,10,20));
 collection.visible_open_tiles={{0,0},{-1,0},{-2,0},{-3,0},{-4,0}};
 assert(observation_required(collection,exit_neighbor,10,20)); // awake/unknown phases along a proposed retreat still need fresh reads
 exit_neighbor.monsters[0].position={-20,0};assert(!observation_required(collection,exit_neighbor,10,20));
 World custody;custody.round=11;custody.self.alive=true;custody.self.position={1,1};Group site;site.spawns={{1,1}};site.gems=10;
 assert(observe_hold(custody,site,10,10,0,100,1)==HoldObservation::unknown);
 custody.round=12;site.observed=site.complete=true;site.respawning=true;custody.self.bag=10;
 assert(observe_hold(custody,site,11,10,0,100,1)==HoldObservation::payout);
 custody.self.bag=0;site.gems=0;custody.own_gold=102;assert(observe_hold(custody,site,11,10,0,100,1)==HoldObservation::lost); // passive income alone cannot certify ownership
 custody.own_gold=103;assert(observe_hold(custody,site,11,10,0,100,1)==HoldObservation::payout);
 site.respawning=false;site.cleared=true;assert(observe_hold(custody,site,11,10,0,100,1)==HoldObservation::capturing);
 site.contested=true;assert(observe_hold(custody,site,11,10,0,100,1)==HoldObservation::lost);
 site.complete=false;custody.self.position={2,1};assert(observe_hold(custody,site,11,10,0,100,1)==HoldObservation::lost);
 // Capture has an initial start phase followed by the configured countdown;
 // a bank uses its starting bag, then converts the actual bag at owner start.
 int capture_start=10,first_payout=capture_start+w.capture_turns+1;assert(first_payout==16);
 const int channel=w.ticks(11);const Gems at_completion=18;assert(channel==4&&at_completion>11);
 std::puts("plan10 common: ranges, complete groups, phase ordering, tribute boundaries, stale actions and fuel guards passed");
}
