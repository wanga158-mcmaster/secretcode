// Synthetic owned-income interception forecasts. No SDK, native engine or games.
#include "../common/src/runtime_intercept.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
using namespace p6;
namespace ri=p6::runtime_intercept;
int checks=0;
void check(bool condition,const char* name) {
    ++checks;
    if (!condition) { std::fprintf(stderr,"FAIL runtime intercept: %s\n",name); std::exit(1); }
}
template<class T> Observation<T> seen(T value,ViewClock clock,int subject) {
    return {value,clock,subject,Knowledge::exact};
}
template<class T> Fact<T> setting(T value) { return {value,Source::queried_setting}; }
struct Fixture {
    organization::Input input{};
    forecast::Request bank{};
    Observation<double> effective_health{};
    Fact<Gems> kill_gold{};
};
std::unique_ptr<Fixture> fixture() {
    auto f=std::make_unique<Fixture>();
    auto& in=f->input;
    in.frame.clock={10,1,3}; in.settings={200,1000,5};
    in.windows.deadlines={60,60,60}; in.windows.windows={60,60,60};
    in.windows.sampled_round=10; in.windows.sampled_actor_id=1;
    in.windows.source=Source::synthetic_fixture; in.windows.valid=true;
    auto& self=in.frame.own.actor;
    self.id=1; self.team=Team::ally; self.pick_ordinal=0;
    self.position={0,0}; self.health=self.max_health=100;
    self.strength=10; self.attack_range=1;
    self.alive=self.can_attack=self.can_move=true;
    in.frame.own.gold=5;
    Actor target;
    target.id=99; target.team=Team::enemy; target.position={1,0};
    target.health=target.max_health=10; target.strength=1;
    target.alive=target.can_attack=true; target.bag=20;
    in.frame.actors[0]=seen(target,in.frame.clock,target.id); in.frame.actor_count=1;
    in.legal_enemy_target=target.id; in.bank_approach={2,0};
    in.enemy_stress[0]=seen(2.,in.frame.clock,target.id); in.enemy_stress_count=1;
    auto& r=f->bank;
    r.shape=forecast::Shape::bank_only; r.project_group=17;
    r.timing.settings=setting(in.settings); r.timing.tribute_multiplier=setting(.1);
    r.timing.tribute_base=setting(0); r.timing.tribute_minimum=setting(1);
    r.ambient_incoming=seen(1.,in.frame.clock,self.id);
    r.enemy_cast_stress[0]=seen(1.,in.frame.clock,target.id); r.enemy_cast_count=1;
    r.bank_waypoint=90; r.renewal_waypoint=91;
    r.bank_position=in.bank_approach; r.renewal_position={0,0};
    r.bank_available=seen(true,in.frame.clock,r.bank_waypoint);
    r.legs[0]={self.position,r.bank_position,{2,Source::public_map}};
    r.legs[1]={r.bank_position,r.renewal_position,{2,Source::public_map}}; r.leg_count=2;
    f->effective_health=seen(10.,in.frame.clock,target.id);
    f->kill_gold=setting<Gems>(40);
    return f;
}
ri::Result quote(const Fixture& f) {
    return ri::quote(f.input,f.bank,f.effective_health,f.kill_gold);
}
void rejected(const Fixture& f,const char* name) {
    const auto out=quote(f);
    check(!out.rank.eligible() && out.target==-1,name);
}
void owned_income() {
    auto f=fixture();
    const auto out=quote(*f);
    check(out.rank.eligible() && out.target==99,"visible legal loaded enemy has an interception quote");
    for (int s=0; s<3; ++s) {
        check(out.rank.own_score[s]==20,"stolen bag becomes one own scheduled bank");
        check(out.rank.denial[s]==0 && out.rank.margin[s]==20,"no additional opponent-denial score");
    }
    check(out.rank.occupancy==11 && out.rank.productive_arrival==11,
          "one kill turn plus travel, tribute and productive renewal are charged");
    check(out.rank.peak_bag==20,"stolen gems are not counted twice as initial and produced bag");
    check(!out.rank.immediate_legal_win && !out.rank.terminal_terms && out.rank.kills_delta==0,
          "hypothetical future kill supplies no immediate win or terminal kill claim");
    check(f->input.frame.own.actor.bag==0 && f->input.frame.own.actor.health==100 &&
          f->input.frame.actors[0].value.alive,"quote does not mutate actual observations or receipt a kill");
    f->input.frame.own.actor.bag=5;
    const auto loaded=quote(*f);
    check(loaded.rank.eligible() && loaded.rank.own_score[0]==25 && loaded.rank.peak_bag==25,
          "actual own bag and observed enemy bag each enter accounting once");
    f->input.frame.own_team_score=990;
    check(!quote(*f).rank.immediate_legal_win,"future interception cannot assert presently legal winning tribute");
}
void health_and_duration() {
    auto f=fixture();
    const auto plain=quote(*f);
    f->effective_health.value=20;
    const auto shield=quote(*f);
    check(shield.rank.eligible() && shield.rank.occupancy==plain.rank.occupancy+1 &&
          shield.rank.productive_arrival==plain.rank.productive_arrival+1,
          "current target shields increase basic-only kill time");
    f->effective_health.value=80;
    check(quote(*f).rank.eligible(),"eight basic kill turns remain within the bounded template");
    f->effective_health.value=80.01;
    rejected(*f,"a ninth required kill turn is unavailable");
    f=fixture(); f->input.frame.own.actor.ranged=true; f->input.frame.own.actor.attack_range=3;
    const auto ranged=quote(*f);
    check(ranged.rank.eligible() && ranged.rank.occupancy==plain.rank.occupancy+1,
          "ranged shared allowance uses half-strength basic rate");
    f=fixture(); f->input.enemy_stress[0].value=100;
    f->bank.enemy_cast_stress[0].value=99;
    rejected(*f,"full target basic plus cast stress makes a lethal interception unavailable");
    f=fixture(); f->input.frame.actors[0].value.health=f->input.frame.actors[0].value.max_health=30;
    f->effective_health.value=30;
    f->bank.ambient_incoming.value=6;
    rejected(*f,"individually survivable kill and bank phases cannot reset cumulative health");
    f=fixture();
    Actor other=f->input.frame.actors[0].value;
    other.id=100; other.bag=0; other.position={0,2};
    f->input.frame.actors[1]=seen(other,f->input.frame.clock,other.id);
    f->input.frame.actor_count=2;
    f->input.enemy_stress[1]=seen(8.,f->input.frame.clock,other.id); f->input.enemy_stress_count=2;
    f->bank.enemy_cast_stress[1]=seen(7.,f->input.frame.clock,other.id); f->bank.enemy_cast_count=2;
    rejected(*f,"a surviving enemy remains charged throughout bank and renewal");
}
void stale_unknown_and_illegal() {
    auto f=fixture(); f->input.frame.actors[0].value.bag=0;
    rejected(*f,"zero visible enemy resources do not produce a fake zero-score candidate");
    f=fixture(); f->kill_gold={}; rejected(*f,"missing kill-gold setting stays unknown");
    f=fixture(); f->kill_gold.source=Source::public_map;
    rejected(*f,"copied kill reward is not a queried setting");
    f=fixture(); f->input.legal_enemy_target=-1; rejected(*f,"target needs current legal attack witness");
    f=fixture(); f->input.legal_enemy_target=100; rejected(*f,"witness must identify this visible target");
    f=fixture(); f->input.frame.own.actor.can_attack=false;
    rejected(*f,"current attack allowance is required");
    f=fixture(); f->input.frame.actors[0].value.position={2,0};
    rejected(*f,"a named target outside basic range is unavailable");
    f=fixture(); f->input.bank_approach=f->bank.bank_position={20,0};
    f->bank.legs[0].to={20,0}; f->bank.legs[0].distance.value=20;
    f->bank.legs[1].from={20,0}; f->bank.legs[1].distance.value=20;
    rejected(*f,"interception target must be near the actual selected bank approach");
    f=fixture(); --f->effective_health.clock.round;
    rejected(*f,"stale effective HP and shield view cannot license interception");
    f=fixture(); --f->input.frame.actors[0].clock.round;
    rejected(*f,"stale enemy body and bag cannot license interception");
    f=fixture(); --f->input.enemy_stress[0].clock.round;
    rejected(*f,"stale enemy stress is not zero incoming");
    f=fixture(); f->bank.enemy_cast_count=0;
    rejected(*f,"missing visible-enemy cast bound stays unavailable");
    f=fixture(); f->input.enemy_stress[0].value=1;
    rejected(*f,"full stress cannot omit the represented cast damage");
    f=fixture(); f->bank.ambient_incoming={};
    rejected(*f,"missing awake-camp stress cannot be lowered to zero");
    f=fixture(); f->bank.shape=forecast::Shape::produce_bank;
    rejected(*f,"a camp cycle cannot be substituted for the owned-income bank template");
    f=fixture();
    f->input.enemy_stress[1]=seen(2.,f->input.frame.clock,100); f->input.enemy_stress_count=2;
    rejected(*f,"a body omitted by the actor cap still needs full ambient stress coverage");
    f->bank.ambient_incoming.value=3;
    check(quote(*f).rank.eligible(),"fresh omitted-enemy stress can remain in the ambient bound");
}
void bank_lock_cost() {
    auto f=fixture();
    f->bank.ambient_incoming.value=0;
    const auto unlocked=quote(*f);
    f->bank.bank_available={};
    rejected(*f,"unknown altar without a queried lock bound remains unavailable");
    f->bank.bank_lock_hits=setting(5);
    const auto locked=quote(*f);
    check(locked.rank.eligible() && locked.rank.occupancy==unlocked.rank.occupancy+6 &&
          locked.rank.productive_arrival==unlocked.rank.productive_arrival+6,
          "interception includes full unknown altar lock and inspection costs");
    check(locked.rank.own_score==unlocked.rank.own_score && locked.rank.denial==unlocked.rank.denial,
          "lock work changes timing and occupancy without inventing income or denial");
}
}
int main() {
    owned_income(); health_and_duration(); stale_unknown_and_illegal(); bank_lock_cost();
    std::printf("Plan 6 runtime interception: %d checks passed\n",checks);
}
