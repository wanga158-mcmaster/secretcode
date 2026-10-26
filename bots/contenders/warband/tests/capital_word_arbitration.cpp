#include "capital_word_arbitration.h"
#include <cstdio>
#include <limits>
#include <type_traits>
using namespace wb::capital::word_arbitration;
static_assert(sizeof(BorrowedProof)==sizeof(const HorizonProof*));
static_assert(std::is_trivially_copyable_v<BorrowedProof>);
static_assert(std::is_nothrow_default_constructible_v<BorrowedProof>);
static_assert(std::is_constructible_v<BorrowedProof,const HorizonProof&>);
static_assert(!std::is_constructible_v<BorrowedProof,HorizonProof&&>);
static_assert(!std::is_constructible_v<BorrowedProof,const HorizonProof&&>);
static unsigned checks=0,failures=0;
static void check(bool value,const char* label){++checks;if(!value){if(failures<10)std::printf("FAIL %s\n",label);++failures;}}
static Offer idle(float hp=900) {
    Offer o;o.choice.native_legal=o.choice.prefix_complete=true;o.choice.endpoint={4,5};
    o.evaluation.valid=o.evaluation.prefix_alive=o.evaluation.survives=true;o.evaluation.hp_lower=hp;
    o.horizon={2,30,31,9,true,HorizonKind::NextScheduledOwnerInput};return o;
}
static Offer work(float hp=700,float quantity=40) {
    auto o=idle(hp);o.choice.camp_work=quantity;o.modeled_effect_count=1;
    auto& s=o.service;s.kind=ServiceKind::CampWork;s.horizon=o.horizon;s.target_kind=TargetKind::Camp;s.target_id=7;
    s.effect_count=1;s.quantity=quantity;s.modeled_useful=s.dated=s.bound=s.persistent=s.exit_known=s.exit_priced=true;
    return o;
}
static Offer channel(float hp=600) {
    auto o=idle(hp);o.choice.primitive=wb::capital::contact::Primitive::Submit;o.choice.channel_started=9;
    auto& s=o.service;s.kind=ServiceKind::ChannelProgress;s.horizon=o.horizon;s.target_kind=TargetKind::Altar;s.target_id=3;
    s.quantity=9;s.modeled_useful=s.dated=s.bound=true;return o;
}
static HorizonProof external_proof(Offer& o) {
    o.evaluation.horizon_admissible=true;HorizonProof proof;
    proof.supplied=true;proof.horizon=o.horizon;
    proof.endpoint=o.choice.endpoint;proof.effect_count=o.modeled_effect_count;
    static constexpr std::array<std::uint64_t,3> identity{17,21,23};
    o.binding={identity,true};proof.binding=o.binding;
    proof.bound_choice=o.choice;proof.bound_evaluation=o.evaluation;proof.bound_service=o.service;
    proof.bound_lost_inputs_upper=o.modeled_lost_inputs_upper;proof.bound_delay_priced=o.delay_priced;
    proof.word_dependencies_closed=proof.reply_domain_closed=proof.npc_closed=proof.active_effects_closed=
        proof.refusal_prefixes_closed=proof.fuel_closed=true;
    return proof;
}
int main() {
    auto a=work(),b=idle();auto c=compare(a,b);
    check(a.proof.get()==nullptr&&b.proof.get()==nullptr,"default model offers carry no proof storage");
    const auto model_copy=a;check(model_copy.proof.get()==nullptr,"copying a model offer retains only a null proof borrow");
    check(c.preference==Preference::Left&&c.basis==Basis::UsefulProgress,"useful dated work and priced exit can beat higher-HP idle");
    check(c.left_class==Classification::ConditionalModel&&c.right_class==Classification::ConditionalModel,"model progress certifies neither offer");
    a.evaluation.survives=false;a.evaluation.hp_lower=0;c=compare(a,b);
    check(c.preference==Preference::Right&&c.basis==Basis::Survival,"doomed work loses before service comparison");
    a.evaluation.prefix_alive=false;c=compare(a,b);
    check(c.preference==Preference::Right&&c.basis==Basis::PrefixAlive,"dead prefix is not repaired by service value");
    a=work();a.evaluation.may_stun_next=true;c=compare(a,b);
    check(c.preference==Preference::Right&&c.basis==Basis::InitialUninterruptedClass&&c.uninterrupted_class,"initial assurance class prefers usable next input");
    check(c.left_modeled_lost_input&&c.left_class==Classification::ConditionalModel,"possible stun exposed as modeled lost input, not universal illegality");
    c=compare(a,b,Class{false});
    check(c.preference==Preference::Right&&c.service_fallback&&c.basis==Basis::ResidualHP,"interruptible class needs a priced delay horizon");
    a.horizon.kind=b.horizon.kind=HorizonKind::NextUsableOwnerInputBound;a.horizon.end_round=b.horizon.end_round=33;
    a.service.horizon=a.horizon;a.modeled_lost_inputs_upper=2;a.delay_priced=true;c=compare(a,b,Class{false});
    check(c.preference==Preference::Left&&c.left_progress&&!c.uninterrupted_class,"explicit priced delay can admit conditional stunned work");
    check(c.left_class==Classification::ConditionalModel,"priced delay alone is not a certificate");
    a=work();b=idle();a.modeled_lost_inputs_upper=2;c=compare(a,b);
    check(c.preference==Preference::Right&&c.basis==Basis::InitialUninterruptedClass,"typed lost input also triggers initial assurance class without stun scalar");
    c=compare(a,b,Class{false});
    check(c.preference==Preference::Right&&c.service_fallback,"typed delay cannot bypass pricing through a false stun scalar");
    a=work();b=idle();a.choice.preserves_receipt=false;c=compare(a,b);
    check(c.preference==Preference::Right&&c.basis==Basis::Receipt,"receipt preservation precedes optional progress");
    a=channel();b=idle();c=compare(a,b);
    check(c.preference==Preference::Left&&c.left_progress,"dated channel start has conditional progress value");
    a.choice.banked_gems=1000;auto same=a;same.choice.banked_gems=0;c=compare(a,same);
    check(c.preference==Preference::Tie,"channel start does not create an already-banked score preference");
    check(classify(a)==Classification::ConditionalModel,"channel effects and apparent bank scalar cannot certify");
    a.service.quantity=10;c=compare(a,b);
    check(c.preference==Preference::Right&&c.service_fallback,"channel progress cannot exceed its bound starting bag");
    a=idle(700);a.choice.banked_gems=1000;c=compare(a,b);
    check(c.preference==Preference::Right&&c.basis==Basis::ResidualHP,"unbound bank scalar contributes no utility");
    a=work();a.service.exit_known=false;c=compare(a,b);
    check(c.preference==Preference::Right&&c.service_fallback,"unknown exit keeps residual fallback");
    a=work();a.service.exit_damage_upper=701;c=compare(a,b);
    check(c.preference==Preference::Right&&c.service_fallback,"priced exit must fit remaining modeled HP");
    a=work();a.service.dated=false;c=compare(a,b);
    check(c.preference==Preference::Right&&c.service_fallback,"undated work is not an economic operator");
    a=work();a.service.effect_count=2;c=compare(a,b);
    check(c.preference==Preference::Right&&c.service_fallback,"effect trace binding is required");
    a=work();a.choice.camp_work=0;c=compare(a,b);
    check(c.preference==Preference::Right&&c.service_fallback,"a useful-work flag cannot invent HP delta");
    a=work();b=channel();c=compare(a,b);
    check(c.preference==Preference::Left&&c.service_fallback&&c.basis==Basis::ResidualHP,"different service units retain HP fallback without exchange rates");
    a=work(700,400);b=work(800,10);b.service.target_id=8;c=compare(a,b);
    check(c.preference==Preference::Right&&c.service_fallback,"camp HP on distinct source identities is incomparable");
    a=work(700,40);b=work(600,60);c=compare(a,b);
    check(c.preference==Preference::Right&&c.basis==Basis::ProgressQuantity,"same source and horizon compare guaranteed persistent HP units");
    a=idle(700);b=idle(800);a.choice.objective_distance=0;b.choice.objective_distance=100;c=compare(a,b);
    check(c.preference==Preference::Right&&c.basis==Basis::ResidualHP,"mere proximity earns no service credit");
    a.evaluation.hp_lower=800;c=compare(a,b);check(c.preference==Preference::Tie,"distance does not break equal physical fallback");
    a=work();b=idle();a.horizon.generation++;a.service.horizon=a.horizon;c=compare(a,b);
    check(c.preference==Preference::Incomparable&&c.basis==Basis::Horizon,"different scene generations cannot share a response quote");
    a=work();a.horizon.round++;a.horizon.end_round++;a.service.horizon=a.horizon;c=compare(a,b);
    check(c.preference==Preference::Incomparable,"different owner-input horizons are not ranked");
    a=work();a.evaluation.horizon_admissible=true;
    check(classify(a)==Classification::ConditionalModel,"evaluation assurance alone never mints external closed proof");
    {
        const HorizonProof proof=external_proof(a);a.proof=BorrowedProof{proof};
        check(classify(a)==Classification::Certified,"scoped immutable fully matched proof is distinguished");
        const auto copy=a;
        check(copy.proof.get()==&proof&&classify(copy)==Classification::Certified,"offer copy borrows the same live immutable proof");
        a.proof={};
        check(classify(a)==Classification::ConditionalModel&&classify(copy)==Classification::Certified,"clearing one borrow neither mutates proof nor other offer");
    }
    {
        a=work();const auto proof=[&]{auto p=external_proof(a);p.refusal_prefixes_closed=false;return p;}();
        a.proof=BorrowedProof{proof};check(classify(a)==Classification::ConditionalModel,"unpriced refusal prefixes prevent certification");a.proof={};
    }
    {
        a=work();const auto proof=[&]{auto p=external_proof(a);p.fuel_closed=false;return p;}();
        a.proof=BorrowedProof{proof};check(classify(a)==Classification::ConditionalModel,"word arithmetic does not prove a fuel bound");a.proof={};
    }
    {
        a=work();const auto proof=[&]{auto p=external_proof(a);++p.effect_count;return p;}();
        a.proof=BorrowedProof{proof};check(classify(a)==Classification::ConditionalModel,"certificate binds exact modeled effect trace");a.proof={};
    }
    {
        a=work();const auto proof=[&]{auto p=external_proof(a);++p.horizon.generation;return p;}();
        a.proof=BorrowedProof{proof};check(classify(a)==Classification::ConditionalModel,"certificate cannot transfer across observations");a.proof={};
    }
    {
        a=work();const HorizonProof proof=external_proof(a);a.proof=BorrowedProof{proof};
        ++a.horizon.round;++a.horizon.end_round;a.service.horizon=a.horizon;
        check(classify(a)==Classification::ConditionalModel,"still-live prior-input proof is stale on a later owner horizon");a.proof={};
    }
    {
        a=work();a.choice.effect_count=1;a.choice.effects[0]={wb::capital::contact::EffectKind::DamageFoe,0,10,0};
        const HorizonProof proof=external_proof(a);a.proof=BorrowedProof{proof};
        a.choice.effects[0].amount=11;check(classify(a)==Classification::ConditionalModel,"same endpoint and count cannot substitute a different effect trace");a.proof={};
    }
    {
        a=work();const HorizonProof proof=external_proof(a);a.proof=BorrowedProof{proof};a.evaluation.hp_lower-=1;
        check(classify(a)==Classification::ConditionalModel,"closed proof binds exact evaluated bound");a.proof={};
    }
    {
        a=work();const HorizonProof proof=external_proof(a);a.proof=BorrowedProof{proof};a.service.target_id++;
        check(classify(a)==Classification::ConditionalModel,"closed proof binds exact service target");a.proof={};
    }
    {
        a=work();const HorizonProof proof=external_proof(a);a.proof=BorrowedProof{proof};
        const std::array<std::uint64_t,3> other_identity{17,21,24};a.binding={other_identity,true};
        check(classify(a)==Classification::ConditionalModel,"closed proof binds lossless word and dependency identity");a.proof={};
    }
    {
        a=work();b=idle();const HorizonProof proof=external_proof(b);b.proof=BorrowedProof{proof};c=compare(a,b);
        check(c.preference==Preference::Left&&c.left_class==Classification::ConditionalModel&&c.right_class==Classification::Certified,
            "model-risk choice remains explicitly conditional even against certified idle");b.proof={};
    }
    a=work();a.service.kind=ServiceKind::EnemyKill;a.service.target_kind=TargetKind::Enemy;
    a.service.quantity=1;a.evaluation.kills=1;c=compare(a,b);
    check(c.preference==Preference::Left&&c.left_progress,"whole bound modeled enemy kill can compete before residual HP");
    a.service.quantity=.5f;c=compare(a,b);
    check(c.preference==Preference::Right&&c.service_fallback,"partial damage is not a whole enemy kill");
    a=work();a.evaluation.hp_lower=std::numeric_limits<float>::quiet_NaN();c=compare(a,b);
    check(c.preference==Preference::Right&&c.basis==Basis::Validity,"nonfinite model offers cannot dominate");
    a=work();a.evaluation.hp_lower=0;c=compare(a,b);
    check(c.preference==Preference::Right&&c.basis==Basis::Validity,"inconsistent surviving zero-HP offer rejected");
    a=work();a.horizon.fresh=false;c=compare(a,b);check(c.preference==Preference::Right,"stale offer falls back to valid incumbent");
    b.horizon.fresh=false;c=compare(a,b);check(c.preference==Preference::Incomparable,"two invalid scopes make no progress claim");
    const auto cycle_a=work(700,40),cycle_b=work(600,60);auto cycle_c=work(650,50);cycle_c.service.target_id=8;
    check(better(cycle_b,cycle_a)&&better(cycle_c,cycle_b)&&better(cycle_a,cycle_c),
        "documented pair arbitration is not a strict sorting or nomination-independent order");
    std::printf("capital_word_arbitration: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
