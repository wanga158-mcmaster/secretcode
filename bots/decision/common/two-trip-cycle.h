#pragma once
#include "model.h"
#include <array>
#include <climits>
#include <optional>

// A conditional quiet-model accounting calculation, not a runtime policy or a
// claim that future routes, capture ownership, health or target identity are known.
namespace plan3_two_trip {
inline constexpr unsigned maximum_members=32;
inline constexpr double maximum_scalar=1e12;
enum class State { admitted, rejected, unknown, invalid };
enum class Reason { none, shape, numeric, missing_evidence, continuity, route,
                    old_bag, no_second_trip, no_damage, unsafe_first, unsafe_escape,
                    unsafe_return, unsafe_second, unsafe_finish, deadline };
enum class MemberState { unknown, alive, dead };
struct Member {
    int key=-1, observedRound=-1;
    MemberState state=MemberState::unknown;
    double health=0, incoming=0;
};
// All costs are supplied projections from queried public/current information.
// supported is a caller-owned route/survival premise, not validated by this model.
// healthLoss covers exposure beyond the explicit clear and escape calculations.
struct Leg {
    std::optional<int> turns;
    std::optional<double> healthLoss;
    std::optional<bool> supported;
};
enum LegIndex { outward, escape, return_to_group, capture_approach, altar, lock, leg_count };
struct QuietAssumptions {
    bool stableMemberIdentity=false, noExternalDamageOrReset=false;
    bool unchangedEquipment=false, noDeathOrChannelInterruption=false;
    bool exclusiveCaptureAndConversion=false;
    bool complete() const {
        return stableMemberIdentity && noExternalDamageOrReset && unchangedEquipment &&
               noDeathOrChannelInterruption && exclusiveCaptureAndConversion;
    }
};
struct Input {
    // groupKey is the minimum ALL-public-member spawn key, not the first
    // surviving member. Member keys likewise remain stable across both trips.
    int round=0, roundLimit=0, observationRound=-1, groupKey=-1;
    unsigned memberCount=0;
    std::array<Member,maximum_members> members{};
    bool actorKnown=false, groupGemsKnown=false;
    int carriedGems=0, groupGems=0;
    double health=0, maximumHealth=0, strength=0, focus=0;
    double campDamage=1, campMitigation=1;
    std::array<Leg,leg_count> legs{};
    // Raw queried countdown settings. Capture includes its starting phase;
    // recall/tribute finish at a later owner-turn start, never immediately.
    std::optional<int> recallTicks, captureTicks, tributeBase, tributeMinimum;
    std::optional<double> spawnHealing, tributeMultiplier;
    std::optional<bool> safeRecall;
    QuietAssumptions quiet;
};
struct Forecast {
    int groupKey=-1, firstMemberKey=-1;
    int firstClearTurns=0, secondClearTurns=0;
    int recallTurns=0, healingTurns=0, captureTurns=0, tributeTurns=0;
    std::array<int,leg_count> legTurns{};
    double firstClearLoss=0, escapeLoss=0, secondClearLoss=0;
    double healthAtRecall=0, healthOnReturn=0, terminalHealth=0;
    int tau=0, deltaS=0, terminalBag=0;
    // Camp rewards only. Passive gold may accrue; this model neither credits
    // gold utility nor changes equipment during the job.
    int firstTripGems=0, firstTripCampGold=0, goldUtilityCredit=0;
    std::array<plan3::Conversion,1> conversions{};
    bool oneTripAdmitted=false, assumedQuiet=true;
};
struct Result {
    State state=State::unknown;
    Reason reason=Reason::missing_evidence;
    std::optional<Forecast> forecast; // No partial reward/cost result on failure.
};
inline bool scalar(double v) {return std::isfinite(v) && v>=0 && v<=maximum_scalar;}
inline Result stop(State state,Reason reason) {return {state,reason,std::nullopt};}
inline bool alive_after(double health,double maximum) {
    return std::isfinite(health) && health>0 && health>=maximum*.25;
}
// Match ForecastContext::healing_turns for FINAL renewal: full health costs0;
// otherwise charge ceil(deficit/rate)+1. This is separate from the internal
// trip-one spawn hold, whose nonoverlapping end ticks are counted below.
inline std::optional<int> renewal_healing_turns(double maximum,double health,double fraction) {
    if(!scalar(maximum) || maximum<=0 || !scalar(health) || health>maximum ||
       !scalar(fraction) || fraction<=0)return std::nullopt;
    if(health>=maximum)return 0;
    const double rate=fraction*maximum,deficit=maximum-health;
    const double ticks=std::ceil(deficit/rate)+1;
    if(!std::isfinite(rate) || rate<=0 || deficit/rate==0 || !std::isfinite(ticks) || ticks>INT_MAX)
        return std::nullopt;
    return int(ticks);
}
// estimate_clear adds two exposure turns using signed int. Validate that
// arithmetic before invoking the shared model, including supported large inputs.
inline std::optional<plan3::ClearEstimate> clear(double hp,double incoming,const Input& in,double health) {
    // Inherited positive-rate analytical model, not a certificate of actual
    // attacks/casts/DPS. A zero-damage actor cannot inherit its floor of one.
    const double rawRate=(1.125*in.strength+.12*in.focus)*in.campDamage;
    const double rate=std::max(1.0,rawRate),fraction=hp/rate;
    const double duration=std::ceil(fraction);
    if(rawRate<=0 || !std::isfinite(rate) || (hp>0 && fraction==0) ||
       !std::isfinite(duration) || duration>INT_MAX-2)return std::nullopt;
    const auto result=plan3::estimate_clear(hp,incoming,in.strength,in.focus,in.campDamage,
                                           in.campMitigation,health,in.maximumHealth);
    if(!std::isfinite(result.healthLoss))return std::nullopt;
    return result;
}
inline Result evaluate(const Input& in) {
    if(in.memberCount>maximum_members || !in.memberCount || in.round<0 || in.roundLimit<in.round ||
       in.groupKey<0 || in.carriedGems<0 || in.groupGems<0)return stop(State::invalid,Reason::shape);
    if(!scalar(in.health) || !scalar(in.maximumHealth) || in.maximumHealth<=0 || in.health>in.maximumHealth ||
       !scalar(in.strength) || !scalar(in.focus) || !scalar(in.campDamage) || in.campDamage<=0 ||
       !scalar(in.campMitigation))return stop(State::invalid,Reason::numeric);
    bool missing=!in.actorKnown || !in.groupGemsKnown || in.observationRound!=in.round;
    int first=-1;unsigned living=0;double hp=0,incoming=0;
    for(unsigned i=0;i<in.memberCount;++i) {
        const auto& m=in.members[i];
        if(m.key<0 || !scalar(m.health) || !scalar(m.incoming))return stop(State::invalid,Reason::numeric);
        for(unsigned j=0;j<i;++j)if(m.key==in.members[j].key)return stop(State::invalid,Reason::shape);
        if(m.state!=MemberState::unknown && m.state!=MemberState::alive && m.state!=MemberState::dead)
            return stop(State::invalid,Reason::shape);
        if(m.state==MemberState::unknown || m.observedRound!=in.round)missing=true;
        if(m.state==MemberState::dead && (m.health!=0 || m.incoming!=0))return stop(State::invalid,Reason::shape);
        if(m.state==MemberState::alive) {
            if(m.health<=0)return stop(State::invalid,Reason::shape);
            ++living;hp+=m.health;incoming+=m.incoming;
            if(first<0 || m.key<in.members[unsigned(first)].key)first=int(i);
        }
    }
    if(!scalar(hp) || !scalar(incoming))return stop(State::invalid,Reason::numeric);
    bool unsupported=false;
    for(const auto& leg:in.legs) {
        if(leg.turns && *leg.turns<0)return stop(State::invalid,Reason::numeric);
        if(leg.healthLoss && !scalar(*leg.healthLoss))return stop(State::invalid,Reason::numeric);
        missing|=!leg.turns || !leg.healthLoss || !leg.supported;
        unsupported|=leg.supported && !*leg.supported;
    }
    for(const auto value:{in.recallTicks,in.captureTicks,in.tributeBase,in.tributeMinimum}) {
        if(value && *value<0)return stop(State::invalid,Reason::numeric);
        missing|=!value;
    }
    if(in.spawnHealing && (!scalar(*in.spawnHealing) || *in.spawnHealing<=0 || *in.spawnHealing>1))
        return stop(State::invalid,Reason::numeric);
    if(in.tributeMultiplier && !scalar(*in.tributeMultiplier))return stop(State::invalid,Reason::numeric);
    missing|=!in.spawnHealing || !in.tributeMultiplier || !in.safeRecall;
    if(missing)return stop(State::unknown,Reason::missing_evidence);
    if(!in.quiet.complete())return stop(State::unknown,Reason::continuity);
    if(unsupported || !*in.safeRecall)return stop(State::rejected,Reason::route);
    if(in.carriedGems)return stop(State::rejected,Reason::old_bag);
    if(living<2 || in.groupGems<=0)return stop(State::rejected,Reason::no_second_trip);
    if(in.strength==0 && in.focus==0)return stop(State::rejected,Reason::no_damage);
    // Do not subtract a potentially much larger first member from a rounded
    // aggregate: that could erase a positive remaining member's health/damage.
    double remainingHealth=0,remainingIncoming=0;
    for(unsigned i=0;i<in.memberCount;++i)if(int(i)!=first && in.members[i].state==MemberState::alive) {
        remainingHealth+=in.members[i].health;remainingIncoming+=in.members[i].incoming;
    }
    Forecast f;f.groupKey=in.groupKey;f.firstMemberKey=in.members[unsigned(first)].key;
    for(unsigned i=0;i<leg_count;++i)f.legTurns[i]=*in.legs[i].turns;
    const double firstHealth=in.health-*in.legs[outward].healthLoss;
    const auto one=clear(hp,incoming,in,firstHealth);
    // Killing ONE member wakes the entire group. Charge all currently living
    // incoming throughout this prefix, not just the first member's attacks.
    const auto prefix=clear(in.members[unsigned(first)].health,incoming,in,firstHealth);
    if(!one || !prefix)return stop(State::invalid,Reason::numeric);
    f.oneTripAdmitted=one->admitted;
    if(!alive_after(firstHealth,in.maximumHealth) || !prefix->admitted)
        return stop(State::rejected,Reason::unsafe_first);
    f.firstClearTurns=prefix->turns;f.firstClearLoss=prefix->healthLoss;
    f.escapeLoss=remainingIncoming*in.campMitigation*f.legTurns[escape]+*in.legs[escape].healthLoss;
    if(!std::isfinite(f.escapeLoss))return stop(State::invalid,Reason::numeric);
    f.healthAtRecall=firstHealth-f.firstClearLoss-f.escapeLoss;
    if(!alive_after(f.healthAtRecall,in.maximumHealth))return stop(State::rejected,Reason::unsafe_escape);
    f.recallTurns=std::max(1,*in.recallTicks);
    const double healingRate=*in.spawnHealing*in.maximumHealth;
    const double heal=std::ceil((in.maximumHealth-f.healthAtRecall)/healingRate);
    if(!std::isfinite(heal) || heal>INT_MAX)return stop(State::invalid,Reason::numeric);
    // Each heal tick is a separate spawn turn end. Return movement begins at
    // the following owner start, so none of these ticks overlap return travel.
    f.healingTurns=int(heal);
    f.healthOnReturn=in.maximumHealth-*in.legs[return_to_group].healthLoss;
    if(!alive_after(f.healthOnReturn,in.maximumHealth))return stop(State::rejected,Reason::unsafe_return);
    const auto finish=clear(remainingHealth,remainingIncoming,in,f.healthOnReturn);
    if(!finish)return stop(State::invalid,Reason::numeric);
    if(!finish->admitted)return stop(State::rejected,Reason::unsafe_second);
    f.secondClearTurns=finish->turns;f.secondClearLoss=finish->healthLoss;
    f.terminalHealth=f.healthOnReturn-f.secondClearLoss-*in.legs[capture_approach].healthLoss-
        *in.legs[altar].healthLoss-*in.legs[lock].healthLoss;
    if(!alive_after(f.terminalHealth,in.maximumHealth))return stop(State::rejected,Reason::unsafe_finish);
    if(*in.captureTicks==INT_MAX)return stop(State::invalid,Reason::numeric);
    f.captureTurns=*in.captureTicks+1;
    const auto tribute=plan3::tribute_completion_turns(in.groupGems,*in.tributeMultiplier,
                                                     *in.tributeBase,*in.tributeMinimum);
    if(!tribute)return stop(State::invalid,Reason::numeric);
    f.tributeTurns=*tribute;
    std::uint64_t duration=0;
    for(const auto turns:f.legTurns)duration+=unsigned(turns);
    for(const int turns:{f.firstClearTurns,f.secondClearTurns,f.recallTurns,f.healingTurns,
                        f.captureTurns,f.tributeTurns})duration+=unsigned(turns);
    if(duration>INT_MAX)return stop(State::invalid,Reason::numeric);
    f.tau=int(duration);
    if(f.tau>in.roundLimit-in.round)return stop(State::rejected,Reason::deadline);
    f.deltaS=in.groupGems;f.conversions[0]={f.tau,in.groupGems};
    return {State::admitted,Reason::none,std::move(f)};
}
} // namespace plan3_two_trip
