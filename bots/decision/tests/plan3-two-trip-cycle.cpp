#include "../common/two-trip-cycle.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace q=plan3_two_trip;
unsigned checks=0;
void require(bool value) {++checks;if(!value) {std::fprintf(stderr,"check %u failed\n",checks);std::abort();}}
bool near(double a,double b) {return std::abs(a-b)<1e-7;}
// Analytical Tavi/three-wolf example. Public-like stats match captured designs;
// path lengths, starting HP, single ownership and all future continuity are
// explicitly hypothetical. Nothing here constructs a native game/setup.
q::Input example() {
    q::Input in;in.round=240;in.roundLimit=2000;in.observationRound=240;in.groupKey=168;
    in.memberCount=3;in.actorKnown=true;in.groupGemsKnown=true;in.groupGems=66;
    in.health=in.maximumHealth=1125;in.strength=80;in.focus=100;
    in.campDamage=1.1;in.campMitigation=.7;
    in.members[0]={30,240,q::MemberState::alive,1350,20};
    in.members[1]={10,240,q::MemberState::alive,1350,20};
    in.members[2]={20,240,q::MemberState::alive,1350,20};
    for(auto& leg:in.legs) {leg.turns=0;leg.healthLoss=0;leg.supported=true;}
    in.legs[q::outward].turns=10;in.legs[q::escape].turns=2;
    in.legs[q::return_to_group].turns=12;in.legs[q::altar].turns=10;
    in.recallTicks=5;in.captureTicks=5;in.spawnHealing=1;
    in.tributeMultiplier=.1;in.tributeBase=0;in.tributeMinimum=1;in.safeRecall=true;
    in.quiet={true,true,true,true,true};return in;
}
void fails(const q::Input& in,q::State state,q::Reason reason) {
    const auto result=q::evaluate(in);require(result.state==state);require(result.reason==reason);require(!result.forecast);
}
void accounting() {
    const auto in=example();const auto result=q::evaluate(in);require(result.state==q::State::admitted);require(result.forecast.has_value());
    const auto& f=*result.forecast;require(!f.oneTripAdmitted);require(f.firstMemberKey==10);
    require(f.firstClearTurns==13);require(f.secondClearTurns==25);
    require(near(f.firstClearLoss,630));require(near(f.escapeLoss,56));require(near(f.secondClearLoss,756));
    require(near(f.healthAtRecall,439));require(near(f.healthOnReturn,1125));require(near(f.terminalHealth,369));
    require(f.recallTurns==5);require(f.healingTurns==1);require(f.captureTurns==6);require(f.tributeTurns==7);
    int sum=0;for(const auto turns:f.legTurns)sum+=turns;
    sum+=f.firstClearTurns+f.secondClearTurns+f.recallTurns+f.healingTurns+f.captureTurns+f.tributeTurns;
    require(f.tau==91);require(f.tau==sum);require(f.deltaS==66);require(f.terminalBag==0);
    require(f.conversions.size()==1);require(f.conversions[0].completedAt==91);require(f.conversions[0].gems==66);
    require(f.firstTripGems==0);require(f.firstTripCampGold==0);require(f.goldUtilityCredit==0);require(f.assumedQuiet);
    auto x=in;x.roundLimit=x.round+f.tau;require(q::evaluate(x).state==q::State::admitted);
    --x.roundLimit;fails(x,q::State::rejected,q::Reason::deadline);
    x=in;x.spawnHealing=.25;const auto slower=q::evaluate(x);require(slower.forecast.has_value());
    require(slower.forecast->healingTurns==3);require(slower.forecast->tau==f.tau+2);
    x=in;x.recallTicks=0;x.tributeMultiplier=0;x.tributeMinimum=0;
    const auto instant=q::evaluate(x);require(instant.forecast.has_value());
    require(instant.forecast->recallTurns==1);require(instant.forecast->tributeTurns==1);
    x=in;x.legs[q::lock].turns=3;const auto locked=q::evaluate(x);
    require(locked.forecast.has_value());require(locked.forecast->tau==f.tau+3);
}
void canonical() {
    const auto in=example();std::array<unsigned,3> order{0,1,2};
    do {
        auto x=in;for(unsigned i=0;i<3;++i)x.members[i]=in.members[order[i]];
        const auto r=q::evaluate(x);require(r.forecast.has_value());require(r.forecast->firstMemberKey==10);
        require(r.forecast->tau==91);require(near(r.forecast->terminalHealth,369));
    } while(std::next_permutation(order.begin(),order.end()));
    auto x=in;x.memberCount=4;x.members[3]={1,240,q::MemberState::dead,0,0};
    require(q::evaluate(x).forecast->firstMemberKey==10);
    x.members[0]={30,240,q::MemberState::dead,0,0};x.members[2]={20,240,q::MemberState::dead,0,0};
    fails(x,q::State::rejected,q::Reason::no_second_trip);
    x=in;x.members[2].key=x.members[1].key;fails(x,q::State::invalid,q::Reason::shape);
    // Cancellation witness: independently summing the remaining members is
    // necessary even though the aggregate rounds away the tiny second HP.
    x=in;x.memberCount=2;x.health=x.maximumHealth=100;x.strength=1e12;x.focus=0;
    x.campDamage=1;x.campMitigation=1;
    x.members[0]={1,240,q::MemberState::alive,1e12,0};
    x.members[1]={2,240,q::MemberState::alive,1e-5,0};
    const auto tiny=q::evaluate(x);require(tiny.forecast.has_value());require(tiny.forecast->secondClearTurns==1);
}
void unknowns() {
    const auto in=example();auto x=in;x.actorKnown=false;fails(x,q::State::unknown,q::Reason::missing_evidence);
    x=in;x.groupGemsKnown=false;fails(x,q::State::unknown,q::Reason::missing_evidence);
    x=in;--x.observationRound;fails(x,q::State::unknown,q::Reason::missing_evidence);
    x=in;--x.members[1].observedRound;fails(x,q::State::unknown,q::Reason::missing_evidence);
    x=in;x.members[0].state=q::MemberState::unknown;fails(x,q::State::unknown,q::Reason::missing_evidence);
    x=in;x.safeRecall.reset();fails(x,q::State::unknown,q::Reason::missing_evidence);
    x=in;x.quiet.noExternalDamageOrReset=false;fails(x,q::State::unknown,q::Reason::continuity);
    x=in;x.quiet.stableMemberIdentity=false;fails(x,q::State::unknown,q::Reason::continuity);
    x=in;x.quiet.noDeathOrChannelInterruption=false;fails(x,q::State::unknown,q::Reason::continuity);
    x=in;x.quiet.unchangedEquipment=false;fails(x,q::State::unknown,q::Reason::continuity);
    x=in;x.quiet.exclusiveCaptureAndConversion=false;fails(x,q::State::unknown,q::Reason::continuity);
    for(unsigned i=0;i<q::leg_count;++i) {
        x=in;x.legs[i].turns.reset();fails(x,q::State::unknown,q::Reason::missing_evidence);
        x=in;x.legs[i].healthLoss.reset();fails(x,q::State::unknown,q::Reason::missing_evidence);
        x=in;x.legs[i].supported.reset();fails(x,q::State::unknown,q::Reason::missing_evidence);
        x=in;x.legs[i].supported=false;fails(x,q::State::rejected,q::Reason::route);
    }
    x=in;x.safeRecall=false;fails(x,q::State::rejected,q::Reason::route);
    x=in;x.carriedGems=1;fails(x,q::State::rejected,q::Reason::old_bag);
    x=in;x.strength=x.focus=0;fails(x,q::State::rejected,q::Reason::no_damage);
}
void unsafe() {
    const auto in=example();auto x=in;x.health=200;fails(x,q::State::rejected,q::Reason::unsafe_first);
    // The harmless target still wakes and exposes us to its dangerous neighbor.
    x=in;x.memberCount=2;x.health=30;x.maximumHealth=100;
    x.members[0]={1,240,q::MemberState::alive,1,0};x.members[1]={2,240,q::MemberState::alive,100,20};
    fails(x,q::State::rejected,q::Reason::unsafe_first);
    x=in;x.legs[q::escape].turns=100;fails(x,q::State::rejected,q::Reason::unsafe_escape);
    x=in;x.legs[q::return_to_group].healthLoss=1000;fails(x,q::State::rejected,q::Reason::unsafe_return);
    x=in;x.members[1].health=1;x.members[0].health=x.members[2].health=100000;
    fails(x,q::State::rejected,q::Reason::unsafe_second);
    x=in;x.legs[q::altar].healthLoss=100;fails(x,q::State::rejected,q::Reason::unsafe_finish);
}
void invalid() {
    const auto in=example();auto x=in;x.memberCount=33;fails(x,q::State::invalid,q::Reason::shape);
    x=in;x.memberCount=0;fails(x,q::State::invalid,q::Reason::shape);
    x=in;x.roundLimit=x.round-1;fails(x,q::State::invalid,q::Reason::shape);
    x=in;x.members[0].state=q::MemberState::dead;fails(x,q::State::invalid,q::Reason::shape);
    x=in;x.members[0].health=0;fails(x,q::State::invalid,q::Reason::shape);
    for(const double bad:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),1e13}) {
        x=in;x.strength=bad;fails(x,q::State::invalid,q::Reason::numeric);
        x=in;x.members[0].incoming=bad;fails(x,q::State::invalid,q::Reason::numeric);
        x=in;x.legs[q::lock].healthLoss=bad;fails(x,q::State::invalid,q::Reason::numeric);
    }
    x=in;x.spawnHealing=0;fails(x,q::State::invalid,q::Reason::numeric);
    x=in;x.spawnHealing=2;fails(x,q::State::invalid,q::Reason::numeric);
    x=in;x.spawnHealing=1e-320;fails(x,q::State::invalid,q::Reason::numeric);
    x=in;x.legs[q::return_to_group].turns=-1;fails(x,q::State::invalid,q::Reason::numeric);
    x=in;x.tributeMultiplier=std::numeric_limits<double>::infinity();fails(x,q::State::invalid,q::Reason::numeric);
    x=in;x.tributeMultiplier=1e12;fails(x,q::State::invalid,q::Reason::numeric);
    x=in;x.captureTicks=INT_MAX;fails(x,q::State::invalid,q::Reason::numeric);
    x=in;x.roundLimit=INT_MAX;x.legs[q::outward].turns=INT_MAX;
    fails(x,q::State::invalid,q::Reason::numeric);
    x=in;x.strength=.5;x.focus=0;x.campDamage=1;x.members[0].health=1e12;
    fails(x,q::State::invalid,q::Reason::numeric);
    x=in;x.memberCount=2;x.health=x.maximumHealth=100;x.strength=1e12;x.focus=0;
    x.campDamage=1e12;x.campMitigation=1;
    x.members[0]={1,240,q::MemberState::alive,1e-320,0};
    x.members[1]={2,240,q::MemberState::alive,1,0};
    fails(x,q::State::invalid,q::Reason::numeric);
    // Even known-missing input cannot conceal a malformed supplied quantity.
    x=in;x.actorKnown=false;x.members[0].health=-1;fails(x,q::State::invalid,q::Reason::numeric);
    x=in;const auto largest=q::clear(INT_MAX-2,0,x,1125);require(largest.has_value());
    x.strength=x.focus=0;require(!q::clear(double(INT_MAX),0,x,1125));
}
int main() {
    accounting();canonical();unknowns();unsafe();invalid();
    const auto f=*q::evaluate(example()).forecast;
    std::printf("TWO_TRIP_CYCLE {\"status\":\"pass\",\"checks\":%u,\"example\":{\"analyticalOnly\":true,\"oneTripAdmitted\":false,\"tau\":%d,\"converted\":%d,\"firstTripCampGems\":0,\"firstTripCampGold\":0,\"terminalBag\":0,\"terminalHealth\":%.9g},\"engineLaunches\":0,\"runtimeIntegrated\":false}\n",checks,f.tau,f.deltaS,f.terminalHealth);
}
