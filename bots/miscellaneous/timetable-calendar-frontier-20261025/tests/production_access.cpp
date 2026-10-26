#include "production_access.h"
#include <array>
#include <cstdio>
#include <limits>

namespace {
using namespace tt::production_access;
int checks{},failures{};
void check(bool condition,const char* label)
{
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n",label); }
}
Stamp at(std::int64_t round,Point pos={5,4}) { return {static_cast<std::uint64_t>(round),round,pos}; }
Acknowledgment observed(int group,std::int64_t round)
{
    Acknowledgment ack;
    ack.group=group; ack.stamp=at(round);
    ack.clear_observed=true; ack.region_observed=true;
    ack.in_region=true; ack.uncontested=true; ack.uninterrupted=true;
    return ack;
}
}

int main()
{
    // Abstract isolated three-spawn region behind ordinary walls. The caller's
    // exact graph certificate is independent from ranged station work geometry.
    std::array<Member,3> members{{{{2,1},true,true,Life::Dead,0},
                                 {{2,2},true,true,Life::Dead,0},
                                 {{3,2},true,true,Life::Alive,55}}};
    Frame frame;
    frame.stamp=at(59); frame.members=members; frame.members_complete=true;
    frame.ordinary=Ordinary::Blocked; frame.attack_range=3; frame.attack_ready=true;
    frame.has_dash=true; frame.cast_ready=true; frame.dash_range=2;
    frame.landing={{3,2},true,Fact::No,Fact::No};
    auto result=assess(frame,frame.stamp);
    check(result.residual==Residual::Living && result.work==Work::InRangeCandidate &&
          result.ordinary==Ordinary::Blocked && result.entry==Entry::NeedClear,
          "ranged work on the final member does not imply ordinary capture access");
    frame.attack_ready=false;
    check(assess(frame,frame.stamp).work==Work::TemporarilyUnavailable,"spent basic is not no residual work");
    frame.attack_ready=true;
    members[2].life=Life::Dead; members[2].hp=0;
    check(assess(frame,frame.stamp).entry==Entry::Unknown,"a planned lethal hit does not certify its occupied landing");
    frame.landing.empty=Fact::Yes; frame.landing.legal_dash=Fact::Yes;
    result=assess(frame,frame.stamp);
    check(result.residual==Residual::Clear && result.work==Work::None && result.entry==Entry::DashCandidate,
          "actual cleared empty range-two spawn supplies a dash entry candidate");
    frame.cast_ready=false;
    check(assess(frame,frame.stamp).entry==Entry::TemporarilyUnavailable,"spent C is temporary unavailability");
    frame.landing.legal_dash=Fact::Unknown;
    check(assess(frame,frame.stamp).entry==Entry::TemporarilyUnavailable,"spent C does not require future legal dash evidence");
    frame.landing.legal_dash=Fact::Yes;
    frame.cast_ready=true; frame.dash_cooldown=1;
    check(assess(frame,frame.stamp).entry==Entry::TemporarilyUnavailable,"cooldown is temporary unavailability");
    frame.dash_cooldown=0; frame.landing.visible=false;
    check(assess(frame,frame.stamp).entry==Entry::Unknown,"hidden landing stays unknown");
    frame.landing.visible=true; frame.landing.empty=Fact::No;
    check(assess(frame,frame.stamp).entry==Entry::Unknown,"occupied landing stays unknown rather than unreachable");
    frame.landing.empty=Fact::Yes; frame.landing.legal_dash=Fact::Unknown;
    check(assess(frame,frame.stamp).entry==Entry::Unknown,"geometry alone cannot authorize a dash");
    frame.landing.legal_dash=Fact::Yes; frame.landing.pos={4,3};
    check(assess(frame,frame.stamp).entry==Entry::Unknown,"empty legal nonregion landing is not capture entry");
    frame.landing.pos={2,1};
    check(assess(frame,frame.stamp).entry==Entry::Unknown,"selected spawn outside actual dash range is not a candidate");
    frame.landing.pos={3,2}; frame.has_dash=false;
    check(assess(frame,frame.stamp).entry==Entry::ConditionalOrdinaryBlocked,"ordinary blocked fact has exact model scope");
    frame.ordinary=Ordinary::Reachable; frame.cast_ready=false;
    check(assess(frame,frame.stamp).entry==Entry::OrdinaryGraphCandidate,"ordinary capture candidate survives spent or absent C");
    frame.ordinary=Ordinary::Blocked; frame.has_dash=true; frame.cast_ready=true;
    members[0].visible=false;
    check(assess(frame,frame.stamp).residual==Residual::Unknown && assess(frame,frame.stamp).entry==Entry::Unknown,
          "hidden sibling cannot certify a complete clear");
    members[2].life=Life::Alive; members[2].hp=55;
    check(assess(frame,frame.stamp).work==Work::InRangeCandidate,"known visible work can coexist with unknown siblings");
    frame.attack_range=1;
    check(assess(frame,frame.stamp).work==Work::Unknown,"hidden siblings prevent a complete out of range claim");
    frame.attack_range=3;
    members[0].visible=true; members[2].hp=0;
    check(assess(frame,frame.stamp).residual==Residual::Unknown,"inconsistent life and HP do not prove clear");
    members[2].life=Life::Dead; frame.members_complete=false;
    check(assess(frame,frame.stamp).residual==Residual::Unknown,"incomplete member inventory cannot certify clear");
    frame.members_complete=true;
    auto stale=frame.stamp; ++stale.epoch;
    check(assess(frame,stale).ordinary==Ordinary::Unknown,"new epoch revokes frame facts");
    stale=frame.stamp; ++stale.origin.x;
    check(assess(frame,stale).entry==Entry::Unknown,"displacement revokes entry facts");

    check(recall_start(59,5,5)==60,"fresh solo entry normal recall starts at k plus one");
    check(recall_start(59,5,3)==62,"fresh solo entry Home recall starts at k plus three");
    check(recall_start(59,5,20)==60,"long recall still never starts on fresh solo entry turn");
    check(!recall_start(-1,5,5) && !recall_start(1,0,5) && !recall_start(1,5,0),"invalid phase parameters are unknown");
    check(!recall_start(std::numeric_limits<std::int64_t>::max(),5,5),"phase arithmetic rejects overflow");

    Cycle cycle{2,5,5,EntryMethod::Dash};
    auto ack=observed(2,59);
    check(cycle.advance(ack,ack.stamp)==Phase::NeedEntry,"observed group clear begins entry stage");
    ack.entry_observed=true;
    check(cycle.advance(ack,ack.stamp)==Phase::NeedEntry,"intended dash cannot establish actual entry");
    ack.dash_applied=true;
    check(cycle.advance(ack,ack.stamp)==Phase::Hold && cycle.entered_round()==59,"actual dash plus region occupation starts hold");
    check(cycle.advance(ack,ack.stamp)==Phase::Hold,"new solo entry is not immediately recall eligible");
    ack=observed(2,60);
    check(cycle.advance(ack,ack.stamp)==Phase::RecallEligible,"next fresh uninterrupted owner turn admits normal recall timing");
    for (int round=61;round<=70;++round) { ack=observed(2,round); cycle.advance(ack,ack.stamp); }
    check(cycle.phase()==Phase::RecallEligible && cycle.completed()==0,"expired capture arithmetic without receipt cannot advance");
    ack=observed(2,71); ack.in_region=false; ack.receipt_observed=true;
    check(cycle.advance(ack,ack.stamp)==Phase::Receipt,"actual payout can precede same-boundary recall departure");
    ack.receipt_observed=false;
    check(cycle.advance(ack,ack.stamp)==Phase::Receipt,"receipt alone does not invent carried gems");
    ack.carried_gems_observed=true;
    check(cycle.advance(ack,ack.stamp)==Phase::NeedBank,"actual carried receipt creates conversion work");
    ack=observed(2,108); ack.refill_observed=true;
    check(cycle.advance(ack,ack.stamp)==Phase::NeedBank,"refill without actual bank acknowledgment cannot finish conversion");
    ack.bank_observed=true;
    check(cycle.advance(ack,ack.stamp)==Phase::Renew && cycle.completed()==0,"actual bank leaves own-resource renewal outstanding");
    ack=observed(2,113);
    check(cycle.advance(ack,ack.stamp)==Phase::Renew,"return deadline does not prove resource refill");
    ack.refill_observed=true;
    check(cycle.advance(ack,ack.stamp)==Phase::Work && cycle.completed()==1,"actual refill closes one acknowledged cycle only");

    Cycle home{2,5,3,EntryMethod::Ordinary};
    ack=observed(2,10); home.advance(ack,ack.stamp); ack.entry_observed=true; home.advance(ack,ack.stamp);
    ack=observed(2,11); check(home.advance(ack,ack.stamp)==Phase::Hold,"Home cannot recall at k plus one");
    ack=observed(2,12); check(home.advance(ack,ack.stamp)==Phase::Hold,"Home cannot recall at k plus two");
    ack=observed(2,13); check(home.advance(ack,ack.stamp)==Phase::RecallEligible,"Home conditional recall begins at k plus three");
    ack=observed(2,14); ack.uncontested=false;
    check(home.advance(ack,ack.stamp)==Phase::NeedEntry && !home.entered_round(),"contest revokes uninterrupted hold evidence");
    ack=observed(2,15); ack.entry_observed=true; home.advance(ack,ack.stamp);
    ack=observed(2,16); ack.region_observed=false;
    check(home.advance(ack,ack.stamp)==Phase::NeedEntry,"unknown region observation cannot sustain the timing premise");
    ack=observed(3,17); ack.entry_observed=true; ack.dash_applied=true;
    check(home.advance(ack,ack.stamp)==Phase::NeedEntry,"other group acknowledgments do not advance this operation");
    ack=observed(2,17); ack.entry_observed=true; stale=ack.stamp; ++stale.epoch;
    check(home.advance(ack,stale)==Phase::NeedEntry,"stale acknowledgment cannot enter region");
    ack=observed(2,9); ack.entry_observed=true;
    check(home.advance(ack,ack.stamp)==Phase::NeedEntry,"out of order acknowledgment cannot restore an older hold");
    ack=observed(2,18); ack.entry_observed=true; home.advance(ack,ack.stamp);
    ack=observed(2,19); ack.clear_observed=false;
    check(home.advance(ack,ack.stamp)==Phase::NeedEntry,"unobserved clear cannot sustain capture eligibility");
    std::printf("production access: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
