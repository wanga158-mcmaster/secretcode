#include "return_policy.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <type_traits>

using namespace mosaic::return_policy;
unsigned checks=0;
void check(bool ok,int line){++checks;if(!ok){std::fprintf(stderr,"return-policy failed at %d check%u\n",line,checks);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)

// Real controllers likewise keep the large bounded storage off their turn stack.
static Frame frame;
static std::array<Candidate,candidate_capacity> menu;
void reset(double hp=10,std::size_t sources=1){
    frame=Frame{};menu={};frame.frame=40;frame.observation=7;frame.health=hp;
    frame.can_move=frame.can_attack=frame.can_cast=true;
    frame.source_count=sources;frame.coverage_complete=true;
    for(std::size_t i=0;i<sources&&i<source_capacity;++i){
        auto& source=frame.sources[i];source.group=unsigned(i);source.fresh=true;
        source.alive=source.health_known=source.exact_health=true;source.health=35;
    }
    for(auto& candidate:menu){candidate.frame=40;candidate.observation=7;}
}
void suffix(Scenario& scenario,unsigned prefix,std::initializer_list<double> damage){
    scenario=Scenario{};scenario.applied_prefix=prefix;scenario.route_complete=true;
    scenario.future_failures_bounded=true;scenario.phase_count=damage.size();
    unsigned p=0;for(double hit:damage){
        auto& phase=scenario.phases[p++];phase.bounds_valid=phase.observed_open=true;phase.incoming[0]=hit;
    }
}
Primitive& primitive(Candidate& candidate,unsigned index,Action action){
    candidate.primitive_count=std::max(candidate.primitive_count,std::size_t(index+1));
    auto& primitive=candidate.word[index];primitive.action=action;primitive.observed_legal=true;
    return primitive;
}
void retreat_and_kill(){
    reset();auto& retreat=menu[0];retreat.scenario_count=1;
    primitive(retreat,0,Action::move);suffix(retreat.scenarios[0],1,{20,0});
    auto old=evaluate(frame,retreat);CHECK(old.valid&&!old.upper_alive&&old.upper_loss==20);
    // Sole wolf HP35, Basic40, actual M+A. A legal post-kill Move reaches the
    // newly free cell. Unchanged support rejects20>=HP10; the complete effect
    // removes that sole attacker before its next batch, even on refused Move.
    auto& work=menu[1];work.purpose=Purpose::work_exit;work.scenario_count=2;
    auto& hit=primitive(work,0,Action::basic);hit.damage[0]=40;hit.exact_damage=true;
    primitive(work,1,Action::move).may_refuse=true;
    suffix(work.scenarios[0],2,{20});suffix(work.scenarios[1],1,{20,0});
    work.scenarios[1].refused=true;work.scenarios[1].refused_primitive=1;
    work.scenarios[1].additional_refusals=1;
    auto q=evaluate(frame,work);
    CHECK(q.valid&&q.upper_alive&&q.upper_complete&&q.upper_loss==0);
    CHECK(q.assurance==Assurance::conditional_bound&&q.useful_damage==35);
    CHECK(choose(frame,menu.data(),2,true).index==1);
    // No exact damage contract: useful kill remains a supported prediction,
    // never a conservative certificate and never actual executor state.
    hit.exact_damage=false;q=evaluate(frame,work);
    CHECK(q.valid&&!q.upper_alive&&q.estimated_alive&&q.estimated_loss==0);
    CHECK(q.assurance==Assurance::estimated&&choose(frame,menu.data(),2,false).index==std::numeric_limits<std::size_t>::max());
    CHECK(choose(frame,menu.data(),2,true).index==1);
    Receipt receipt{40,7,1,true,true,true,true};CHECK(!continuation_ready(work,q,receipt));
    receipt.observation=8;receipt.actual_effects_match=false;CHECK(!continuation_ready(work,q,receipt));
    receipt.actual_effects_match=true;receipt.route_refreshed=false;CHECK(!continuation_ready(work,q,receipt));
    receipt.route_refreshed=true;CHECK(continuation_ready(work,q,receipt));
    CHECK(!continuation_ready(retreat,q,receipt)); // another same-frame word is not this quote
    receipt.frame=41;CHECK(!continuation_ready(work,q,receipt));
    // Actual M matters. A ranged Basic consumes it; reset alone restores A,
    // so neither a fictitious Move nor an unready second Basic may be priced.
    hit.consumes_move=true;CHECK(evaluate(frame,work).reason==Reason::unavailable_primitive);
    hit.consumes_move=false;frame.can_move=false;CHECK(evaluate(frame,work).reason==Reason::unavailable_primitive);
    frame.can_move=true;frame.can_attack=false;CHECK(evaluate(frame,work).reason==Reason::unavailable_primitive);
}
void refusal_prefixes(){
    reset();auto& candidate=menu[0];candidate.scenario_count=2;
    auto& kill=primitive(candidate,0,Action::cast);kill.damage[0]=40;kill.exact_damage=true;kill.may_refuse=true;
    suffix(candidate.scenarios[0],1,{20});suffix(candidate.scenarios[1],0,{20,0});
    candidate.scenarios[1].refused=true;candidate.scenarios[1].refused_primitive=0;
    candidate.scenarios[1].additional_refusals=1;
    auto q=evaluate(frame,candidate);CHECK(q.valid&&!q.upper_alive&&!q.estimated_alive&&q.upper_loss==20);
    // A refused killing cast cannot erase its source. Success-only omission,
    // wrong prefix and a reopened previously consumed refusal are rejected.
    candidate.scenario_count=1;CHECK(evaluate(frame,candidate).reason==Reason::missing_failure_case);
    candidate.scenario_count=2;candidate.scenarios[1].applied_prefix=1;
    CHECK(evaluate(frame,candidate).reason==Reason::invalid_scenario);
    candidate.scenarios[1].applied_prefix=0;frame.refusal_used=true;
    CHECK(evaluate(frame,candidate).reason==Reason::refusal_budget);
    frame.refusal_used=false;candidate.scenarios[1].additional_refusals=2;
    CHECK(evaluate(frame,candidate).reason==Reason::refusal_budget);
    candidate.scenarios[1].additional_refusals=1;candidate.scenarios[1].future_failures_bounded=false;
    q=evaluate(frame,candidate);CHECK(q.valid&&!q.supported&&q.assurance==Assurance::unsupported);
}
void allowance_words(){
    reset();auto& candidate=menu[0];candidate.scenario_count=1;
    // Ranged Move spends M+A; a compatible self-cast restores A (not M),
    // permitting a real Move -> Reset -> Basic without invented movement.
    primitive(candidate,0,Action::move).consumes_attack=true;
    primitive(candidate,1,Action::cast).resets_attack=true;
    auto& hit=primitive(candidate,2,Action::basic);hit.consumes_move=true;
    hit.damage[0]=40;hit.exact_damage=true;suffix(candidate.scenarios[0],3,{20});
    CHECK(evaluate(frame,candidate).upper_alive);
    primitive(candidate,3,Action::move);candidate.scenarios[0].applied_prefix=4;
    CHECK(evaluate(frame,candidate).reason==Reason::unavailable_primitive);
    candidate.primitive_count=3;candidate.scenarios[0].applied_prefix=3;
    candidate.word[1].resets_attack=false;CHECK(evaluate(frame,candidate).reason==Reason::unavailable_primitive);
}
void group_wake_and_sleep(){
    reset(10,3);frame.no_exogenous_wake=true;
    for(unsigned i=0;i<3;++i){frame.sources[i].asleep=true;frame.sources[i].health=100;}
    frame.sources[0].health=35;frame.sources[1].group=0;
    auto& candidate=menu[0];candidate.scenario_count=1;candidate.purpose=Purpose::work_exit;
    auto& hit=primitive(candidate,0,Action::basic);hit.damage[0]=40;hit.exact_damage=true;
    suffix(candidate.scenarios[0],1,{20,0});candidate.scenarios[0].phases[0].incoming[1]=7;
    candidate.scenarios[0].phases[0].incoming[2]=4;
    auto q=evaluate(frame,candidate);
    CHECK(q.valid&&q.estimated_loss==7&&q.estimated_alive); // same group sibling woke
    CHECK(q.upper_loss==11&&!q.upper_alive); // potential upper never assumes sleep
    hit.damage[2]=1;q=evaluate(frame,candidate); // explicit Varg collateral group
    CHECK(q.estimated_loss==11&&!q.estimated_alive);
    hit.damage[2]=0;frame.no_exogenous_wake=false;
    CHECK(evaluate(frame,candidate).estimated_loss==11);
    frame.no_exogenous_wake=true;frame.sources[2].fresh=false;
    CHECK(evaluate(frame,candidate).estimated_loss==11); // hidden/stale is not asleep0
    frame.sources[2].fresh=true;frame.sources[0].group_known=false;
    CHECK(evaluate(frame,candidate).estimated_loss==11); // unknown hit group may wake all
    // Pure defensive control is not damage and cannot invent group wake.
    hit.damage[0]=0;frame.sources[1].group_known=false;frame.sources[1].group=99;hit.replaces_shield=true;
    hit.shield_after={0,0};q=evaluate(frame,candidate);
    CHECK(q.estimated_loss==0);
    // No work, fresh known sleeping source: estimate0 is explicitly conditional
    // on no exogenous wake, while its potential damage remains20.
    reset();frame.no_exogenous_wake=true;frame.sources[0].asleep=true;
    auto& sleep=menu[0];sleep.scenario_count=1;suffix(sleep.scenarios[0],0,{20,0});
    q=evaluate(frame,sleep);CHECK(q.upper_loss==20&&q.estimated_loss==0&&q.assurance==Assurance::estimated);
    frame.no_exogenous_wake=false;CHECK(!evaluate(frame,sleep).estimated_alive);
    frame.no_exogenous_wake=true;frame.sources[0].fresh=false;CHECK(!evaluate(frame,sleep).estimated_alive);
    // An off-route AoE/splash victim can share a group with the only source
    // on this priced route. Omitting that off-frame body's direct damage must
    // never erase its group-wide wake of the represented sibling.
    reset();frame.no_exogenous_wake=true;frame.sources[0].asleep=true;frame.sources[0].group=3;
    auto& collateral=menu[0];collateral.scenario_count=1;
    auto& cast=primitive(collateral,0,Action::cast);cast.wakes_groups=Mask(1)<<3;
    suffix(collateral.scenarios[0],1,{20,0});q=evaluate(frame,collateral);
    CHECK(q.estimated_loss==20&&!q.estimated_alive);
    cast.wakes_groups=0;CHECK(evaluate(frame,collateral).estimated_loss==0);
}
void timed_effects(){
    reset(21);frame.sources[0].health=100;
    auto& candidate=menu[0];candidate.scenario_count=1;
    suffix(candidate.scenarios[0],0,{20,20,0});
    // Owner shield with supplied next-batch coverage, including timer0 when
    // that owner's expiry is later, absorbs exactly the first camp batch.
    frame.shield={20,PhaseMask(1)};
    auto q=evaluate(frame,candidate);CHECK(q.upper_loss==20&&q.upper_alive);
    frame.shield.active_phases=0;CHECK(evaluate(frame,candidate).upper_loss==40);
    frame.shield={};frame.sources[0].stun_phases=1;
    CHECK(evaluate(frame,candidate).upper_loss==20); // active next camp phase
    frame.sources[0].stun_phases=0;
    CHECK(evaluate(frame,candidate).upper_loss==40); // camp timer0 expired BEFORE action
    auto& stun=primitive(candidate,0,Action::cast);stun.stun_phases[0]=1;stun.exact_control=true;
    candidate.scenarios[0].applied_prefix=1;
    q=evaluate(frame,candidate);CHECK(q.upper_loss==20&&q.upper_alive);
    stun.stun_phases[0]=0;CHECK(!evaluate(frame,candidate).upper_alive);
    stun.stun_phases[0]=1;stun.exact_control=false;
    q=evaluate(frame,candidate);CHECK(q.upper_loss==40&&q.estimated_loss==20&&q.assurance==Assurance::estimated);
    // A shield cast also competes as an entire operator, not an HP threshold.
    stun.stun_phases[0]=0;stun.replaces_shield=true;stun.shield_after={20,PhaseMask(1)};stun.exact_control=true;
    CHECK(evaluate(frame,candidate).upper_loss==20);
    suffix(candidate.scenarios[0],1,{20,20,20,0});
    CHECK(evaluate(frame,candidate).upper_loss==40); // added wait shifts exposure after expiry
    // Temporary suppression cannot manufacture a quiet terminal postcondition.
    suffix(candidate.scenarios[0],1,{20});stun.replaces_shield=false;stun.stun_phases[0]=1;
    q=evaluate(frame,candidate);CHECK(q.upper_loss==0&&q.upper_alive&&!q.upper_complete);
}
void recall_completion(){
    reset(1000);frame.no_exogenous_wake=true;frame.sources[0].asleep=true;
    auto& recall=menu[0];recall.purpose=Purpose::model_escape;recall.scenario_count=1;
    primitive(recall,0,Action::channel);
    // Five is this fixture's declared runtime recall horizon; the model does
    // not contain a rule constant. Home or future rules supply another count.
    suffix(recall.scenarios[0],1,{20,20,20,20,20});
    recall.scenarios[0].declared_escape_postcondition=true;recall.scenarios[0].requires_no_damage=true;
    auto q=evaluate(frame,recall);
    CHECK(q.valid&&q.upper_alive&&!q.upper_complete&&q.estimated_complete);
    CHECK(q.upper_loss==100&&q.estimated_loss==0&&q.assurance==Assurance::estimated);
    frame.sources[0].asleep=false;q=evaluate(frame,recall);
    CHECK(q.estimated_alive&&!q.estimated_complete&&q.assurance==Assurance::none);
    CHECK(choose_risk(frame,menu.data(),1).index==std::numeric_limits<std::size_t>::max());
    frame.shield={200,PhaseMask(31)};q=evaluate(frame,recall);
    CHECK(q.estimated_loss==0&&!q.estimated_complete); // survival/absorption is not uninterrupted recall
    frame.sources[0].asleep=true;recall.scenarios[0].phases[2].channel_interruption=true;
    CHECK(!evaluate(frame,recall).estimated_complete);
    recall.scenarios[0].phases[2].channel_interruption=false;
    primitive(recall,1,Action::move);CHECK(evaluate(frame,recall).reason==Reason::invalid_word);
    recall.primitive_count=1;recall.scenarios[0].requires_no_damage=false;
    CHECK(evaluate(frame,recall).reason==Reason::invalid_scenario);
}
void finite_observation_and_selection(){
    reset(21);auto& observation=menu[0];observation.purpose=Purpose::observation;
    observation.preparation={900,900,1,2,3,true,true};observation.scenario_count=1;
    suffix(observation.scenarios[0],0,{20});observation.scenarios[0].owner_waits=1;
    auto q=evaluate(frame,observation);
    CHECK(q.valid&&q.upper_alive&&q.upper_complete&&q.assurance==Assurance::conditional_bound);
    observation.preparation.completed_after=1;CHECK(evaluate(frame,observation).reason==Reason::nonprogressing_observation);
    observation.preparation.completed_after=2;observation.preparation.previous_intent=899;
    CHECK(evaluate(frame,observation).reason==Reason::nonprogressing_observation);
    observation.preparation.previous_intent=900;observation.preparation.actually_advanced=false;
    CHECK(evaluate(frame,observation).reason==Reason::nonprogressing_observation);
    observation.preparation.actually_advanced=true;frame.health=20;CHECK(!evaluate(frame,observation).estimated_alive);
    // Utility is separate from assurance: estimated harmless sleeping return
    // beats a conservative20-loss return when estimates are explicitly allowed.
    reset(100);frame.no_exogenous_wake=true;frame.sources[0].asleep=true;
    auto& conservative=menu[0];conservative.scenario_count=1;
    suffix(conservative.scenarios[0],0,{20,0});conservative.scenarios[0].owner_waits=2;
    auto& estimated=menu[1];estimated.scenario_count=1;suffix(estimated.scenarios[0],0,{20});
    CHECK(evaluate(frame,conservative).assurance==Assurance::conditional_bound);
    CHECK(evaluate(frame,estimated).assurance==Assurance::estimated);
    CHECK(choose(frame,menu.data(),2,true).index==1); // fewer waits, same modeled loss0
    CHECK(choose(frame,menu.data(),2,false).index==0);
    estimated.scenarios[0].owner_waits=2;
    CHECK(choose(frame,menu.data(),2,true).index==0); // assurance only final full tie
}
void invalid_bounds(){
    reset(100);auto& candidate=menu[0];candidate.scenario_count=1;suffix(candidate.scenarios[0],0,{20,0});
    frame.coverage_complete=false;auto q=evaluate(frame,candidate);
    CHECK(q.valid&&!q.supported&&q.assurance==Assurance::unsupported);
    CHECK(choose(frame,menu.data(),1,true).index==std::numeric_limits<std::size_t>::max());
    frame.coverage_complete=true;frame.source_count=source_capacity+1;
    CHECK(evaluate(frame,candidate).reason==Reason::capacity);
    frame.source_count=1;candidate.scenarios[0].phase_count=phase_capacity+1;
    CHECK(evaluate(frame,candidate).reason==Reason::invalid_scenario);
    candidate.scenarios[0].phase_count=2;candidate.scenarios[0].phases[0].incoming[0]=std::numeric_limits<double>::quiet_NaN();
    CHECK(evaluate(frame,candidate).reason==Reason::invalid_amount);
    candidate.scenarios[0].phases[0].incoming[0]=20;candidate.observation=6;
    CHECK(evaluate(frame,candidate).reason==Reason::stale_frame);
    CHECK(choose(frame,menu.data(),candidate_capacity+1,true).index==std::numeric_limits<std::size_t>::max());
    // Independent fractional counterexample: the mathematical sum of these
    // supplied binary0.1 upper bounds exceeds one. Naive repeated double sums
    // can round below one and incorrectly certify the exactly1HP actor alive.
    reset(1,10);auto& fractional=menu[0];fractional.scenario_count=1;
    suffix(fractional.scenarios[0],0,{0,0});
    for(unsigned i=0;i<10;++i)fractional.scenarios[0].phases[0].incoming[i]=0.1;
    q=evaluate(frame,fractional);
    CHECK(q.valid&&q.upper_loss>=1&&!q.upper_alive&&!q.estimated_alive);
}
void explicit_risk_fallback(){
    reset();auto& less_loss=menu[0];auto& more_loss=menu[1];
    less_loss.scenario_count=more_loss.scenario_count=1;
    suffix(less_loss.scenarios[0],0,{20,0});less_loss.scenarios[0].owner_waits=2;
    suffix(more_loss.scenarios[0],0,{40,0});more_loss.useful_service=100;
    const auto none=std::numeric_limits<std::size_t>::max();
    // Both complete geometric exits are fatal at HP10. Ordinary admission is
    // unchanged; the explicit fallback nominates lower modeled loss, without
    // letting speed/service utility turn the forecast into a survival claim.
    CHECK(choose(frame,menu.data(),2,true).index==none);
    auto selected=choose_risk(frame,menu.data(),2);
    CHECK(selected.index==0&&selected.quote.estimated_loss==20);
    CHECK(!selected.quote.estimated_alive&&selected.quote.assurance==Assurance::none);
    // A surviving forecast wins even with unfavorable waits and service score.
    auto& alive=menu[2];alive.scenario_count=1;suffix(alive.scenarios[0],0,{9,0});
    alive.scenarios[0].owner_waits=8;
    selected=choose_risk(frame,menu.data(),3);
    CHECK(selected.index==2&&selected.quote.estimated_alive);
    // An unsupported zero-loss forecast cannot displace a complete supported
    // fatal forecast; nor can a missing endpoint or interrupted channel.
    less_loss.scenarios[0].phases[0].incoming[0]=0;
    less_loss.scenarios[0].phases[0].bounds_valid=false;
    CHECK(choose_risk(frame,menu.data(),2).index==1);
    less_loss.scenarios[0].phases[0].bounds_valid=true;
    less_loss.scenarios[0].phases[1].incoming[0]=1;
    CHECK(!evaluate(frame,less_loss).estimated_complete&&choose_risk(frame,menu.data(),2).index==1);
    less_loss.scenarios[0].phases[1].incoming[0]=0;less_loss.scenarios[0].route_complete=false;
    CHECK(choose_risk(frame,menu.data(),2).index==1);
    more_loss.scenarios[0].route_complete=false;
    CHECK(choose_risk(frame,menu.data(),2).index==none);
    CHECK(choose_risk(frame,nullptr,2).index==none);
    CHECK(choose_risk(frame,menu.data(),candidate_capacity+1).index==none);
}
void scratch_initialization_and_reuse(){
    static_assert(std::is_aggregate_v<Primitive>&&std::is_aggregate_v<Phase>&&
        std::is_aggregate_v<Scenario>&&std::is_aggregate_v<Candidate>);
    static std::array<Candidate,2> reused;
    static Candidate reference;
    const double poison=std::numeric_limits<double>::quiet_NaN();
    // Deliberately initialize ALL scratch bytes that this fixture might later
    // inspect. Inactive payload is poisoned, not read while indeterminate.
    for(auto& candidate:reused){
        candidate.frame=999;candidate.observation=999;candidate.purpose=Purpose::observation;
        candidate.primitive_count=primitive_capacity;candidate.scenario_count=scenario_capacity;
        candidate.preparation={4,3,9,8,0,false,false};candidate.useful_service=999;
        for(auto& word:candidate.word){
            word={};word.action=Action::channel;word.damage.fill(poison);
            word.stun_phases.fill(PhaseMask(255));word.shield_after.amount=poison;
        }
        for(auto& scenario:candidate.scenarios){
            scenario.applied_prefix=99;scenario.refused=true;scenario.refused_primitive=99;
            scenario.additional_refusals=2;scenario.phase_count=phase_capacity+1;
            scenario.route_complete=false;scenario.future_failures_bounded=false;
            scenario.declared_escape_postcondition=false;scenario.requires_no_damage=true;
            scenario.owner_waits=999;
            for(auto& phase:scenario.phases){phase.incoming.fill(poison);
                phase.bounds_valid=false;phase.observed_open=false;phase.channel_interruption=true;}
        }
    }
    const auto same_quote=[](const Quote& a,const Quote& b){
        CHECK(a.valid==b.valid&&a.supported==b.supported&&a.reason==b.reason&&a.assurance==b.assurance);
        CHECK(a.frame==b.frame&&a.observation==b.observation);
        CHECK(a.upper_alive==b.upper_alive&&a.estimated_alive==b.estimated_alive);
        CHECK(a.upper_complete==b.upper_complete&&a.estimated_complete==b.estimated_complete);
        CHECK(a.requires_post_effect_refresh==b.requires_post_effect_refresh);
        CHECK(a.upper_loss==b.upper_loss&&a.estimated_loss==b.estimated_loss&&a.useful_damage==b.useful_damage);
        CHECK(a.max_phases==b.max_phases&&a.owner_waits==b.owner_waits&&
            a.successful_primitives==b.successful_primitives&&a.useful_service==b.useful_service);
    };
    struct Shape {unsigned sources,phases,words;};
    constexpr std::array<Shape,7> shapes{{{4,4,2},{1,2,1},{3,1,0},{0,3,1},{5,3,2},{2,2,0},{1,1,1}}};
    unsigned cycle=0;
    for(const auto shape:shapes){
        reset(10000,shape.sources);frame.frame=100+cycle;frame.observation=20+cycle;
        reference={}; // independently fully value-initialized reference
        // This follows the adapter's active-only construction: reset metadata,
        // {} each used primitive, and fill only committed scenario/source rows.
        const auto write_active=[&](Candidate& candidate){
            candidate.frame=frame.frame;candidate.observation=frame.observation;
            candidate.purpose=Purpose::work_exit;candidate.primitive_count=shape.words;
            candidate.scenario_count=shape.words==2?2:1;candidate.preparation={};candidate.useful_service=1;
            for(unsigned p=0;p<shape.words;++p){
                auto& word=candidate.word[p];word={};word.observed_legal=true;
                word.action=p==0&&shape.sources?Action::basic:Action::move;
                if(word.action==Action::basic){word.damage[0]=40;word.exact_damage=true;}
                if(shape.words==2&&p==1)word.may_refuse=true;
            }
            for(unsigned n=0;n<candidate.scenario_count;++n){
                auto& scenario=candidate.scenarios[n];scenario.applied_prefix=n?1:shape.words;
                scenario.refused=n!=0;scenario.refused_primitive=n?1:0;
                scenario.additional_refusals=n?1:0;scenario.phase_count=shape.phases+n;
                scenario.route_complete=true;scenario.future_failures_bounded=true;
                scenario.declared_escape_postcondition=false;scenario.requires_no_damage=false;
                scenario.owner_waits=unsigned(scenario.phase_count);
                for(unsigned p=0;p<scenario.phase_count;++p){
                    auto& phase=scenario.phases[p];phase.bounds_valid=true;
                    phase.observed_open=true;phase.channel_interruption=false;
                    for(unsigned source=0;source<shape.sources;++source)
                        phase.incoming[source]=p+1==scenario.phase_count?0:double((p+1)*(source+1)+n);
                }
            }
        };
        write_active(reused[0]);write_active(reference);
        const auto actual=evaluate(frame,reused[0]),expected=evaluate(frame,reference);
        same_quote(actual,expected);CHECK(actual.valid&&actual.supported);
        CHECK(actual.subject==&reused[0]&&expected.subject==&reference);
        // Independent integer batch oracle: Basic kills source0 before every
        // success/failure suffix; the other sources remain awake and alive.
        double oracle=0;
        for(unsigned n=0;n<(shape.words==2?2u:1u);++n){
            double loss=0;
            for(unsigned p=0;p+1<shape.phases+n;++p)
                for(unsigned source=shape.words&&shape.sources?1:0;source<shape.sources;++source)
                    loss+=double((p+1)*(source+1)+n);
            oracle=std::max(oracle,loss);
        }
        CHECK(actual.upper_loss==oracle&&actual.estimated_loss==oracle);
        // A failed offer can leave a partially written NaN row in the next
        // slot. Until committed, candidate_count excludes that stale payload.
        auto& failed=reused[1];failed.frame=frame.frame;failed.observation=frame.observation;
        failed.purpose=Purpose::return_exit;failed.preparation={};failed.useful_service=0;
        failed.primitive_count=1;failed.word[0]={};failed.word[0].action=Action::move;
        failed.word[0].observed_legal=true;failed.scenario_count=1;
        auto& partial=failed.scenarios[0];partial.applied_prefix=1;partial.refused=false;
        partial.refused_primitive=0;partial.additional_refusals=0;partial.phase_count=1;
        partial.route_complete=true;partial.future_failures_bounded=true;
        partial.declared_escape_postcondition=false;partial.requires_no_damage=false;partial.owner_waits=1;
        partial.phases[0].bounds_valid=true;partial.phases[0].observed_open=true;
        partial.phases[0].channel_interruption=false;partial.phases[0].incoming.fill(poison);
        const auto picked=choose(frame,reused.data(),1,true),clean=choose(frame,&reference,1,true);
        CHECK(picked.index==clean.index&&picked.index==0);same_quote(picked.quote,clean.quote);
        if(shape.sources)CHECK(!evaluate(frame,failed).valid);
        ++cycle;
    }
    // Resetting an existing poisoned aggregate with {} still clears EVERY
    // nested payload, including inactive words, scenarios, phases and sources.
    reused[0]={};
    for(const auto& word:reused[0].word)for(unsigned i=0;i<source_capacity;++i)
        CHECK(word.damage[i]==0&&word.stun_phases[i]==0);
    for(const auto& scenario:reused[0].scenarios)for(const auto& phase:scenario.phases)
        for(double incoming:phase.incoming)CHECK(incoming==0);
}
int main(){
    retreat_and_kill();refusal_prefixes();allowance_words();group_wake_and_sleep();timed_effects();
    recall_completion();finite_observation_and_selection();invalid_bounds();explicit_risk_fallback();scratch_initialization_and_reuse();
    std::printf("%u return-policy checks passed\n",checks);
}
