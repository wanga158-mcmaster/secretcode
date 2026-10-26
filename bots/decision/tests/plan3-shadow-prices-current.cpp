#include "../shadow-prices-common/adapter.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

extern "C" void* __cxa_allocate_exception(std::size_t size){return std::malloc(size);}
extern "C" void __cxa_throw(void*,void*,void(*)(void*)){std::abort();}
namespace s=plan3_shadow_prices;
static unsigned checks=0,scenarios=0;
static void require(bool condition,int line){++checks;if(!condition){std::fprintf(stderr,"shadow prices check failed at%d\n",line);std::exit(1);}}
#define REQUIRE(...) require(bool(__VA_ARGS__),__LINE__)
static s::Input base(unsigned H=8) {
    s::Input in;in.round=20;in.observationRound=20;in.roundLimit=20+int(H);in.budget={8,std::int64_t(H),8,8};return in;
}
static s::Job job(int key,int reward,s::Costs cost) {
    s::Job j;j.key={0,key,-1,100,-1};j.reward=reward;j.cost=cost;j.costsKnown=true;return j;
}
static void independent_scarcity() {
    for(unsigned scarce=0;scarce<s::resources;++scarce) {
        ++scenarios;auto in=base(scarce==s::time?2:8);in.budget[scarce]=2;
        in.rowCount=1;in.jobs[0]=job(10,10,{1,1,1,1});const auto out=s::evaluate(in);
        REQUIRE(out.certified && out.state==s::State::known && out.pivots<=4 && out.systemSolves<=14);
        REQUIRE(s::compare(out.objective,{20,1})==0 && s::compare(out.rows[0].allocation,{2,1})==0);
        for(unsigned r=0;r<s::resources;++r)REQUIRE(s::compare({out.dual.numerators[r],out.dual.denominator},{r==scarce?10:0,1})==0);
    }
}
static s::Input four_binding() {
    auto in=base(7);in.budget={4,7,4,4};in.rowCount=6;
    in.jobs[0]=job(10,8,{4,1,0,0});in.jobs[1]=job(20,12,{0,1,4,0});
    in.jobs[2]=job(30,16,{0,1,0,4});in.jobs[3]=job(40,20,{0,4,0,0});
    in.jobs[4]=job(50,19,{0,4,0,0});in.jobs[5]=job(60,7,{4,1,0,0});return in;
}
static void joint_certificate() {
    ++scenarios;auto in=four_binding();auto initial=in;initial.budget={100,7,100,100};
    const auto reference=s::evaluate(initial);REQUIRE(reference.certified && reference.controlFromCurrent);
    REQUIRE(reference.active.key==reference.control.key);
    REQUIRE(reference.dual.denominator==1 && reference.dual.numerators==std::array<s::Wide,4>({0,16,0,0}));
    in.fixedPrices=reference.dual;const auto out=s::evaluate(in);
    REQUIRE(out.certified && out.rowCount==6 && out.rows.size()==6 && out.eligibleCount==6);
    REQUIRE(out.pivots<210 && !out.controlFromCurrent && out.controlPrices.numerators==reference.dual.numerators);
    REQUIRE(out.dual.denominator==4 && out.dual.numerators==std::array<s::Wide,4>({3,20,7,11}));
    REQUIRE(s::compare(out.objective,{56,1})==0);
    for(unsigned n=0;n<6;++n) {
        REQUIRE(s::compare(out.rows[n].allocation,{n<4?1:0,1})==0);
        REQUIRE(s::compare(out.rows[n].surplus,{n<4?0:-1,1})==0);
    }
    REQUIRE(out.active.present && out.control.present && out.active.key==in.jobs[0].key && out.control.key==in.jobs[2].key);
    auto reversed=in;std::reverse(reversed.jobs.begin(),reversed.jobs.begin()+reversed.rowCount);const auto reordered=s::evaluate(reversed);
    REQUIRE(reordered.certified && reordered.canonicalKeys==out.canonicalKeys && reordered.dual.numerators==out.dual.numerators);
    REQUIRE(reordered.active.key==out.active.key && reordered.control.key==out.control.key);
    for(const auto& row:reordered.rows)for(const auto& expected:out.rows)if(row.key==expected.key) {
        REQUIRE(s::compare(row.allocation,expected.allocation)==0 && s::compare(row.surplus,expected.surplus)==0);
    }
}
static void all_cuts(const s::Input& in) {
    unsigned needed=0;
    const auto complete=s::evaluate(in,[&]{++needed;return true;});REQUIRE(complete.certified && needed<1000);
    for(unsigned cut=0;cut<needed;++cut) {
        ++scenarios;unsigned calls=0;const auto out=s::evaluate(in,[&]{return calls++<cut;});
        REQUIRE(out.state==s::State::cutoff && out.reason==s::Reason::cutoff && !out.certified);
        REQUIRE(out.rows.empty() && out.rowCount==0 && out.eligibleCount==0 && out.unknownCount==0 && out.rejectedCount==0);
        REQUIRE(!out.active.present && !out.control.present && out.pivots==0 && out.pricingChecks==0 && out.systemSolves==0);
        REQUIRE(out.dual.numerators==std::array<s::Wide,4>{} && out.primal.numerators==std::array<s::Wide,s::maximum_jobs>{});
    }
}
// Exact allocation-support tie regressions; strict arm score stays primary.
static s::Input support_tie_input() {
    auto in=base(4);in.budget={4,4,0,0};in.rowCount=4;
    in.jobs[0]=job(10,4,{4,1,0,0});in.jobs[1]=job(20,8,{2,4,0,0});
    in.jobs[2]=job(30,7,{3,3,0,0});in.jobs[3]=job(40,6,{1,4,0,0});return in;
}
[[gnu::noinline]] static void allocation_ties() {
    ++scenarios;auto in=support_tie_input();const auto out=s::evaluate(in);
    REQUIRE(out.certified && out.eligibleCount==4);
    REQUIRE(s::compare(out.objective,{28,3})==0);
    REQUIRE(out.dual.denominator==3 && out.dual.numerators==std::array<s::Wide,4>({2,5,0,0}));
    for(unsigned n=0;n<4;++n)REQUIRE(s::compare(out.rows[n].allocation,{n==2?4:0,n==2?3:1})==0);
    REQUIRE(s::compare(out.rows[1].surplus,{0,1})==0 && s::compare(out.rows[2].surplus,{0,1})==0);
    REQUIRE(out.active.key==in.jobs[2].key && out.control.key==in.jobs[2].key);
    auto samePrice=in;samePrice.fixedPrices=out.dual;const auto same=s::evaluate(samePrice);
    REQUIRE(same.certified && same.active.key==in.jobs[2].key && same.control.key==in.jobs[2].key);
    auto zeroPrice=in;zeroPrice.fixedPrices=s::Basis<4>{};const auto zero=s::evaluate(zeroPrice);
    // Support must NEVER override a strict arm-score improvement.
    REQUIRE(zero.certified && zero.active.key==in.jobs[2].key && zero.control.key==in.jobs[1].key);
    REQUIRE(zero.rows[1].allocation.numerator==0 && s::compare(zero.rows[1].controlScore,zero.rows[2].controlScore)>0);
    std::reverse(in.jobs.begin(),in.jobs.begin()+in.rowCount);const auto reversed=s::evaluate(in);
    REQUIRE(reversed.certified && reversed.active.key==out.active.key && reversed.control.key==out.control.key);
    REQUIRE(s::compare(reversed.objective,out.objective)==0 && reversed.dual==out.dual);

    ++scenarios;auto fractional=base(5);fractional.budget={4,5,0,0};fractional.rowCount=2;
    fractional.jobs[0]=job(10,5,{4,1,0,0});fractional.jobs[1]=job(20,5,{1,4,0,0});
    const auto fractions=s::evaluate(fractional);REQUIRE(fractions.certified);
    REQUIRE(s::compare(fractions.rows[0].allocation,{11,15})==0 && s::compare(fractions.rows[1].allocation,{16,15})==0);
    // Positive support below one is allowed; allocation magnitude is not a rank.
    REQUIRE(fractions.active.key==fractional.jobs[0].key && fractions.control.key==fractional.jobs[0].key);

    ++scenarios;auto rejected=support_tie_input();rejected.rowCount=5;
    rejected.jobs[4]=job(0,100,{5,1,0,0});const auto over=s::evaluate(rejected);
    REQUIRE(over.certified && !over.rows[4].eligible && over.rows[4].reason==s::Reason::individual_budget);
    REQUIRE(over.active.key==rejected.jobs[2].key && over.control.key==rejected.jobs[2].key);
    rejected.jobs[4]=job(0,100,{1,1,0,0});rejected.jobs[4].currentAdmitted=false;
    const auto notAdmitted=s::evaluate(rejected);
    REQUIRE(notAdmitted.certified && notAdmitted.rows[4].reason==s::Reason::current_admission);
    REQUIRE(notAdmitted.active.key==rejected.jobs[2].key && notAdmitted.control.key==rejected.jobs[2].key);
    all_cuts(support_tie_input());
}

static void boundaries() {
    ++scenarios;auto in=base();in.rowCount=1;in.jobs[0]=job(10,10,{1,1,1,1});
    in.jobs[0].costsKnown=false;auto out=s::evaluate(in);
    REQUIRE(!out.certified && out.state==s::State::known && out.unknownCount==1 && out.rows[0].reason==s::Reason::cost_unknown);
    in.jobs[0].costsKnown=true;in.jobs[0].cost[s::capital]=9;out=s::evaluate(in);
    REQUIRE(!out.certified && out.rejectedCount==1 && out.rows[0].reason==s::Reason::individual_budget);
    in.jobs[0].cost[s::capital]=8;REQUIRE(s::evaluate(in).certified);
    in.jobs[0].cost[s::lost_actions]=-1;out=s::evaluate(in);REQUIRE(out.rows[0].reason==s::Reason::numeric);
    in.jobs[0]=job(10,10,{1,1,1,1});in.observationRound=19;out=s::evaluate(in);
    REQUIRE(out.state==s::State::unavailable && out.rows.empty());
    in.observationRound=20;in.carriedBag=1;REQUIRE(s::evaluate(in).state==s::State::unavailable);
    in.carriedBag=0;in.rowCount=2;in.jobs[1]=in.jobs[0];REQUIRE(s::evaluate(in).state==s::State::unavailable);
    in.rowCount=1;in.cutoff=true;REQUIRE(s::evaluate(in).state==s::State::cutoff);
    in.cutoff=false;in.budget[s::time]=7;REQUIRE(s::evaluate(in).state==s::State::unavailable);
    in.budget[s::time]=8;in.fixedPrices=s::Basis<4>{};in.fixedPrices->numerators[0]=-1;
    REQUIRE(s::evaluate(in).state==s::State::unavailable);
    in=base(0);out=s::evaluate(in);REQUIRE(!out.certified && out.reason==s::Reason::no_job && out.rows.empty());
}
static void full_menu() {
    ++scenarios;auto in=four_binding();in.rowCount=s::maximum_jobs;
    for(unsigned n=6;n<s::maximum_jobs;++n)in.jobs[n]=job(int(10+10*n),1,{4,7,4,4});
    unsigned readyCalls=0;const auto out=s::evaluate(in,[&]{++readyCalls;return true;});
    REQUIRE(out.certified && out.rowCount==28 && out.eligibleCount==28 && out.rows.size()==28);
    REQUIRE(out.canonicalKeys[27]==in.jobs[27].key && s::compare(out.objective,{56,1})==0);
    REQUIRE(out.dual.denominator==4 && out.dual.numerators==std::array<s::Wide,4>({3,20,7,11}));
    REQUIRE(out.pivots<35960 && out.pricingChecks<32*35960 && readyCalls<1000);
    for(unsigned n=6;n<28;++n)REQUIRE(s::compare(out.rows[n].allocation,{0,1})==0 && out.rows[n].surplus.numerator<0);
    all_cuts(in);
    // The last physical slot is retained and can change the winner: no six-row prefix.
    in.jobs[27]=job(280,100,{0,1,0,0});const auto last=s::evaluate(in);
    REQUIRE(last.certified && last.active.key==in.jobs[27].key && s::compare(last.objective,{700,1})==0);
}
static void wide_comparisons() {
    ++scenarios;const s::Wide large=s::Wide{1}<<110,den=s::Wide{1}<<80;
    REQUIRE(s::compare({large,den},{large+1,den+1})>0);
    REQUIRE(s::compare({-large,den},{-large-1,den+1})<0);
    REQUIRE(s::compare({large,den},{s::Wide{1}<<30,1})==0);
    REQUIRE(s::decimal(s::Wide{1}<<100)=="1267650600228229401496703205376");
    const auto exact=s::product(~s::UnsignedWide{0},~s::UnsignedWide{0});
    REQUIRE(exact==std::array<std::uint32_t,8>({1,0,0,0,UINT32_MAX-1,UINT32_MAX,UINT32_MAX,UINT32_MAX}));
}
static plan3::InformationState information() {
    plan3::InformationState state;state.round=20;state.roundLimit=40;state.health=state.maximumHealth=100;
    state.gold=6;state.winningGems=1000;state.position.tile=0;return state;
}
static plan3::JobForecast forecast(plan3::JobId id,int gems,int tau,double loss=0,double spend=0) {
    plan3::JobForecast job;job.id=id;plan3::Outcome nominal;nominal.tau=tau;nominal.deltaS=gems;
    nominal.healthLoss=loss;nominal.goldSpent=spend;nominal.terminal.health=nominal.terminal.maximumHealth=100;
    if(gems)nominal.conversions.push_back({tau,gems});job.outcomes.push_back(nominal);return job;
}
static plan3::JobForecast investment() {
    auto job=forecast({plan3::JobKind::invest,-1,-1,100,3},0,0,0,5.2);
    job.investment.available=job.investment.exactEquipmentReconstruction=true;
    job.investment.output={20,30,40};job.outcomes[0].terminal.loops.push_back({1,1,1,2,true,plan3::Provenance::assumed});return job;
}
static plan3::JobIntent test_selector(void* opaque,bool control,const plan3::InformationState& info,
        const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& context) {
    auto mapped=s::map_menu(info,jobs,context.menuComplete);auto& policy=*static_cast<s::Policy*>(opaque);
    return s::policy_intent(mapped,policy.solve(mapped,[]{return true;}),control);
}
static void mapping_and_policy() {
    ++scenarios;auto info=information();auto ordinary=forecast({plan3::JobKind::cycle,10,-1,100,-1},10,3,1.2);
    ordinary.projectedRenewalAndReturnTime=2;auto invest=investment();
    const std::vector<plan3::JobForecast> jobs{ordinary,invest};auto mapped=s::map_menu(info,jobs);
    REQUIRE(mapped.state==s::State::known && mapped.canonicalCount==2 && mapped.input.rowCount==2 && mapped.unknownCount==0);
    REQUIRE(mapped.input.budget==s::Costs({75,20,6,20}));
    REQUIRE(mapped.input.jobs[0].cost==s::Costs({2,5,0,2}) && mapped.input.jobs[0].reward==10);
    REQUIRE(mapped.input.jobs[1].cost==s::Costs({0,0,6,0}) && mapped.input.jobs[1].reward==40); // Same Y mapping credits actual represented post-purchase output.
    auto policyOwner=std::make_unique<s::Policy>();auto& policy=*policyOwner;const auto first=policy.solve(mapped,[]{return true;});
    REQUIRE(first.certified && policy.reference() && first.controlFromCurrent && first.active.key==first.control.key);
    const auto frozen=*policy.reference();const unsigned solves=policy.solves;
    REQUIRE(policy.solve(mapped,[]{return true;}).certified && policy.solves==solves); // Same owned input reused only inside its exact snapshot.
    info.gold=5;const auto poorer=s::map_menu(info,jobs);REQUIRE(poorer.overBudget==1 && poorer.input.jobs[1].cost[s::capital]==6);
    const auto later=policy.solve(poorer,[]{return true;});REQUIRE(later.certified && *policy.reference()==frozen && policy.solves==solves+1);
    info=information();auto incomplete=jobs;incomplete[1].outcomes[0].terminal.loopSupportUnavailable=true;
    const auto missing=s::map_menu(info,incomplete);auto notReadyOwner=std::make_unique<s::Policy>();auto& notReady=*notReadyOwner;
    REQUIRE(missing.unknownCount==1 && notReady.solve(missing,[]{return true;}).state==s::State::unknown && !notReady.reference());
    REQUIRE(!s::map_menu(info,jobs,false).completeMenu);
    auto unfinished=std::make_unique<s::Policy>();REQUIRE(!unfinished->solve(s::map_menu(info,jobs,false),[]{return true;}).certified && !unfinished->reference());
    auto malformed=jobs;malformed[0].outcomes[0].conversions.clear();REQUIRE(s::map_menu(info,malformed).unknownCount==1);
    malformed=jobs;malformed[0].outcomes[0].provenance=plan3::Provenance::unknown;
    REQUIRE(s::map_menu(info,malformed).unknownCount==1);
    malformed=jobs;malformed[1].outcomes[0].terminal.loops[0].provenance=plan3::Provenance::unknown;
    REQUIRE(s::map_menu(info,malformed).unknownCount==1);
    info.carriedGems=24;auto bank1=forecast({plan3::JobKind::bank,-1,-1,100,-1},24,5);
    auto bank2=forecast({plan3::JobKind::bank,-1,-1,101,-1},24,3);
    REQUIRE(s::bank_prefix(info,{bank1,bank2}).job==bank2.id && s::map_menu(info,jobs).state==s::State::unavailable);
    // Zero-immediate-score Invest genuinely reaches the shared Treasury bridge
    // instead of being intercepted as resource-only gold fallback.
    info=information();auto gold=forecast({plan3::JobKind::cycle,5,-1,-1,-1},0,2);gold.rewardOnlyEndpoint=true;gold.campGold=10;
    auto governing=std::make_unique<s::Policy>();plan3::ControllerContext context;
    const auto selected=plan3::select_policy(test_selector,governing.get(),false,info,{gold,invest},context,plan3::Controller::treasury);
    REQUIRE(selected.status==plan3::IntentStatus::selected && selected.job==invest.id && governing->reference());
    auto bypass=std::make_unique<s::Policy>();const auto ledger=plan3::select_policy(test_selector,bypass.get(),false,info,{gold,invest},context,plan3::Controller::ledger);
    REQUIRE(ledger.reason==plan3::DecisionReason::gold_fallback && !bypass->reference());
    context.activeJob=ordinary.id;context.activeJobValid=true;context.protectedChannelOrCapture=true;
    auto protectedState=std::make_unique<s::Policy>();const auto held=plan3::select_policy(test_selector,protectedState.get(),false,info,jobs,context,plan3::Controller::treasury);
    REQUIRE(held.status==plan3::IntentStatus::preserve && !protectedState->reference());
    unsigned required=0;auto measured=std::make_unique<s::Policy>();REQUIRE(measured->solve(mapped,[&]{++required;return true;}).certified);
    for(unsigned cut=0;cut<required;++cut) {
        ++scenarios;auto interrupted=std::make_unique<s::Policy>();unsigned calls=0;const auto& result=interrupted->solve(mapped,[&]{return calls++<cut;});
        REQUIRE(!result.certified && !interrupted->reference() && result.rows.empty());
    }
    std::vector<plan3::JobForecast> all;
    for(int n=0;n<16;++n)all.push_back(forecast({plan3::JobKind::cycle,n,-1,100,-1},1,1));
    for(int n=0;n<8;++n)all.push_back(forecast({plan3::JobKind::double_cycle,n,n+1,100,-1},2,2));
    all.push_back(bank1);all.push_back(bank2);all.push_back(forecast({plan3::JobKind::renew,-1,-1,-1,-1},0,1));all.push_back(invest);
    const auto full=s::map_menu(info,all);REQUIRE(full.canonicalCount==28 && full.input.rowCount==28 && !full.candidatePrefix);
    REQUIRE(full.input.jobs[27].key==s::key(invest.id));
}
int main() {
    independent_scarcity();joint_certificate();all_cuts(four_binding());allocation_ties();boundaries();wide_comparisons();full_menu();mapping_and_policy();
    std::printf("{\"status\":\"pass\",\"scenarios\":%u,\"checks\":%u,\"resourceDimensions\":4,\"nativeGames\":0}\n",scenarios,checks);
}
