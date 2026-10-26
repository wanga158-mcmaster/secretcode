#include "capital_contact.h"
#include <cstdio>
#include <cmath>
using namespace wb::capital::contact;
static unsigned checks=0,failed=0;
static void check(bool ok,const char* what) {++checks;if(!ok){++failed;std::printf("FAIL %s\n",what);}}
static Frame base() {
    Frame f;f.owner=2;f.round=33;f.position={17,9};f.hp=f.max_hp=1000;
    f.fresh=f.coverage_complete=f.shield_valid_through_horizon=true;return f;
}
static Choice hold(const Frame& f) {
    Choice c;c.endpoint=f.position;c.native_legal=c.prefix_complete=true;c.objective_distance=5;return c;
}
static Foe heart(int distance=3) {
    Foe e;e.id=3;e.position={17+distance,9};e.hp=e.max_hp=1000;e.basic_damage=30;
    e.heart=true;e.opportunities=1;e.abilities[0].kind=AbilityKind::Shield;e.abilities[0].cooldown=6;
    e.abilities[1]={AbilityKind::Dash,0,8,2,1,150,1,false,false};return e;
}
static Evaluation evaluate(Frame f,Choice c) {
    const auto d=choose(f,std::span<const Choice>{&c,1});check(d.choice==0,"legal choice retained");return d.evaluation;
}
int main() {
    // Exact first-contact native witness: equipped-empty Heart reaches from3
    // with dash+move/basic, then a later basic. Lower HP is outward conservative.
    auto f=base();f.foe_count=1;f.foes[0]=heart();auto c=hold(f);
    auto e=evaluate(f,c);
    check(e.damage_upper>=180&&e.damage_upper<180.001f,"Heart one opportunity dash/basic180");
    check(e.hp_lower<=820&&e.hp_lower>819.99f,"native R34 HP820 enclosed");
    check(e.may_stun_next&&!e.horizon_admissible,"dash stun blocks next operator");
    f.foes[0].opportunities=2;e=evaluate(f,c);
    check(e.damage_upper>=210&&e.damage_upper<210.001f,"spent dash cannot repeat second opportunity");
    check(e.hp_lower<=790&&e.hp_lower>789.99f,"native first-free R35 HP790 enclosed");
    // Native observed M0/A1/C1/completed at R33 is not next-input readiness.
    f.foes[0].can_move=f.foes[0].can_attack=f.foes[0].can_cast=false;
    f.foes[0].first_fresh=true;e=evaluate(f,c);
    check(e.damage_upper>=210,"fresh future enemy owner restores M/A/C");
    f.foes[0].first_fresh=false;f.foes[0].opportunities=1;e=evaluate(f,c);
    check(e.damage_upper==0,"current spent allowances are respected");
    // Do not damage Heart merely to score damage: its next raw reply grows.
    f=base();f.foe_count=1;f.foes[0]=heart(1);f.foes[0].abilities[1].cooldown=8;
    c=hold(f);auto unhurt=evaluate(f,c);
    c.effects[0]={EffectKind::DamageFoe,0,100,0};c.effect_count=1;e=evaluate(f,c);
    check(e.damage_upper>unhurt.damage_upper,"Heart response increases after health damage");
    f.foes[0].shield=150;e=evaluate(f,c);
    check(e.damage_upper==unhurt.damage_upper,"shield-only damage does not amplify Heart");
    f.foes[0].hp_may_fall=true;e=evaluate(f,c);
    check(e.damage_upper>=45&&e.damage_upper<45.001f,"uncovered future damage uses1.5 envelope");
    // Complete one-cast word boundaries and ranged shared M/A.
    f=base();f.foe_count=1;auto& foe=f.foes[0];foe=heart(4);foe.heart=false;foe.basic_damage=70;
    foe.abilities[0]={AbilityKind::Reset,0,8,0,0,0,0,false,false};
    foe.abilities[1]={AbilityKind::Dash,0,8,2,0,0,0,false,false};c=hold(f);e=evaluate(f,c);
    check(e.damage_upper>=70&&e.damage_upper<70.001f,"Vendetta cannot dash plus reset same opportunity");
    foe.position={18,9};e=evaluate(f,c);
    check(e.damage_upper>=140&&e.damage_upper<140.001f,"standing melee basic/reset/basic");
    foe.position={21,9};foe.ranged=true;foe.attack_range=3;foe.basic_damage=55;
    foe.abilities[1]={};e=evaluate(f,c);
    check(e.damage_upper>=55&&e.damage_upper<55.001f,"ranged move/reset/basic exactly one basic");
    foe.abilities[0].cooldown=8;e=evaluate(f,c);
    check(e.damage_upper==0,"ranged move cannot also basic without reset");
    foe=heart(1);foe.heart=false;foe.basic_damage=70;foe.opportunities=2;
    foe.abilities[0]={AbilityKind::Reset,0,8,0,0,0,0,false,false};
    foe.abilities[1]={AbilityKind::Reset,0,8,0,0,0,0,false,false};e=evaluate(f,c);
    check(e.damage_upper>=280&&e.damage_upper<280.001f,"maximal word domain retains alternate-slot resets");
    // Actual Marcella range4 and Ryth range4-center/radius1 exceed melee proxy3.
    foe=heart(4);foe.heart=false;foe.abilities[0]={};
    foe.abilities[1]={AbilityKind::Damage,0,8,4,0,120,0,true,false};e=evaluate(f,c);
    check(e.damage_upper>=120&&e.damage_upper<120.001f,"Marcella spell covers gap4");
    foe.position={22,9};foe.abilities[1]={AbilityKind::Area,0,8,4,1,100,1,true,false};e=evaluate(f,c);
    check(e.damage_upper>=100&&e.may_stun_next,"Ryth optimistic ground center covers gap5");
    foe.opportunities=1;
    // A one-input stun or cooldown1 does not cover two foe opportunities.
    foe=heart();foe.stun_inputs=1;foe.opportunities=2;e=evaluate(f,c);
    check(e.damage_upper>=180&&e.damage_upper<180.001f,"one disabled foe input expires before second");
    foe.stun_inputs=0;foe.abilities[1].cooldown=1;e=evaluate(f,c);
    check(e.damage_upper>=180,"cooldown1 ready on second opportunity");
    f.self_stun_immune=true;e=evaluate(f,c);
    check(!e.may_stun_next&&e.horizon_admissible,"actual Marcella immunity retained");
    // An actual lethal prefix removes that actor's future opportunities.
    f=base();f.foe_count=1;f.foes[0]=heart();c=hold(f);
    c.effects[0]={EffectKind::DamageFoe,0,1000,0};c.effect_count=1;e=evaluate(f,c);
    check(e.kills==1&&e.damage_upper==0,"observed modeled lethal removes response");
    c.effects[0]={EffectKind::DamageSelf,0,1000,0};
    c.effects[1]={EffectKind::HealSelf,0,1000,0};c.effect_count=2;e=evaluate(f,c);
    check(!e.prefix_alive,"later healing cannot revive lethal prefix");
    // Missing coverage still returns a ranked escape fallback, not a hold.
    f=base();f.coverage_complete=false;f.foe_count=1;f.foes[0]=heart();
    std::array<Choice,2> choices{hold(f),hold(f)};
    choices[0].objective_distance=0;choices[1].primitive=Primitive::Dash;
    choices[1].endpoint={15,9};choices[1].objective_distance=7;
    auto decision=choose(f,choices);
    check(decision.status==Status::Incomplete&&decision.choice==1,"incomplete fallback values separation before greedy work");
    check(!decision.evaluation.horizon_admissible,"incomplete is never a certificate");
    // New Submit is only channel progress; it is not an already banked receipt.
    f=base();choices={hold(f),hold(f)};choices[1].primitive=Primitive::Submit;
    choices[1].channel_started=28;decision=choose(f,choices);
    check(decision.choice==1&&choices[1].banked_gems==0,"channel-start distinct from actual bank");
    choices[0].continuation_covered=choices[1].continuation_covered=true;
    choices[0].continuation_damage_upper=0;choices[1].continuation_damage_upper=0;
    decision=choose(f,choices);
    check(decision.evaluation.continuation_admissible,"explicit finite continuation remains separate");
    choices={hold(f),hold(f)};choices[1].primitive=Primitive::HealAlly;
    choices[1].ally_rescues=1;choices[1].ally_hp_margin=200;choices[1].ally_effect_covered=true;
    decision=choose(f,choices);check(decision.choice==1,"actual own covered heal rescue precedes optional progress");
    choices[1].ally_effect_covered=false;choices[1].objective_distance=10;
    decision=choose(f,choices);check(decision.choice==0,"uncovered ally rescue receives no credit");
    choices[1].ally_effect_covered=true;choices[1].effects[0]={EffectKind::DamageSelf,0,1000,0};choices[1].effect_count=1;
    decision=choose(f,choices);check(decision.choice==0,"ally rescue never pools HP to revive self");
    // Invalid domains cannot silently omit bodies, duplicates or nonfinite hits.
    f.foe_count=7;check(choose(f,choices).status==Status::Invalid,"roster cap rejects overflow");
    f=base();f.foe_count=2;f.foes[0]=heart();f.foes[1]=heart();
    check(choose(f,choices).status==Status::Invalid,"duplicate physical foe rejected");
    f=base();f.foe_count=1;f.foes[0]=heart();f.foes[0].basic_damage=INFINITY;
    check(choose(f,choices).status==Status::Invalid,"nonfinite raw hit rejected");
    f=base();f.fresh=false;check(choose(f,choices).choice<0,"stale root rejected");
    f=base();f.foe_count=1;f.foes[0]=heart();f.foes[0].abilities[0].kind=static_cast<AbilityKind>(99);
    check(choose(f,choices).status==Status::Invalid,"unknown ability kind cannot silently disappear");
    f=base();c=hold(f);c.effects[0].kind=static_cast<EffectKind>(99);c.effect_count=1;
    check(choose(f,std::span<const Choice>{&c,1}).status==Status::Invalid,"unknown prefix effect rejected");
    // Personal visibility loss leaves a reachable belief ball, not quietness.
    f=base();f.position={14,19};f.foe_count=1;f.foes[0]=heart();
    auto& hidden=f.foes[0];hidden.position={17,20};hidden.position_radius=2;
    hidden.heart=false;hidden.basic_damage=115;hidden.represented=false;
    hidden.abilities[0]={AbilityKind::Reset,0,8,0,0,0,0,false,false};hidden.abilities[1]={};
    c=hold(f);c.primitive=Primitive::Recall;decision=choose(f,std::span<const Choice>{&c,1});
    check(decision.status==Status::Incomplete,"stale hidden foe remains incomplete");
    check(decision.evaluation.damage_upper>=230&&decision.evaluation.damage_upper<230.001f,"hidden Ryth movement ball still reaches recall cell");
    c.endpoint={12,17};e=evaluate(f,c);
    check(e.damage_upper==0,"candidate endpoint uses conservative distance to same belief ball");
    hidden.position_radius=10001;check(choose(f,std::span<const Choice>{&c,1}).status==Status::Invalid,"belief radius bound enforced");
    hidden.position_radius=-1;check(choose(f,std::span<const Choice>{&c,1}).status==Status::Invalid,"negative belief radius rejected");
    // Prefix hit arithmetic remains source-ordered float32 under shields.
    f=base();f.shield=50;c=hold(f);c.effects[0]={EffectKind::DamageSelf,0,70,0};c.effect_count=1;e=evaluate(f,c);
    check(e.hp_lower<=980&&e.hp_lower>979.99f,"prefix shield absorption observed order");
    std::printf("capital_contact: %u checks, %u failures\n",checks,failed);
    return failed?1:0;
}
