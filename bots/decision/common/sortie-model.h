#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <optional>
#include <utility>

// Complete conditional quiet-model Cycle. No SDK, implicit ability damage, partial camp
// rewards, partition search, or inference of observed members from absence.
namespace plan3_sortie {
inline constexpr unsigned maximum_members=8,maximum_sorties=8,maximum_attacks=512;
// Combat action storage is independent of recovery capacity. The extra slots
// cover burst-only member kills and the single initial unavailable-action tick.
inline constexpr unsigned maximum_combat_turns=524;
inline constexpr int maximum_turns=8192;
inline constexpr double maximum_scalar=1e12;
enum class State { admitted,rejected,unknown,invalid };
enum class Reason { none,shape,numeric,evidence,route,continuity,old_bag,
                    no_progress,unsafe_entry,unsafe_attack,unsafe_escape,
                    unsafe_return,sortie_bound,attack_bound,deadline,cutoff,combat_budget_bound,ability,turn_bound };
struct Member {
    int key=-1;
    double health=0,incoming=0;
    bool known=false; // HP0 is a declared dead member, never an absent query.
};
struct Leg {
    int turns=0,exposureTurns=0;
    bool known=false,supported=false;
};
// Optional queried Tavi kit. False knownness never supplies free ready casts.
// initialBurstAllowed additionally certifies the movement-exclusive slot may
// be used in the current owner turn; can_cast alone does not establish this.
struct TaviInput {
    bool enabled=false,known=false;
    double focus=0,burstModifier=0,burstCampDamage=1;
    int burstRange=0,stunRounds=0;
    int resetCooldown=0,burstCooldown=0,resetMaxCooldown=0,burstMaxCooldown=0;
    bool initialCanAttack=false,initialCanCast=false,initialBurstAllowed=false;
};
enum class TaviCast : std::uint8_t { none,burst,reset };
struct CombatTurn {
    std::uint8_t member=255,beforeBasic=0,afterBasic=0;
    TaviCast cast=TaviCast::none;
};
struct Input {
    int round=0,horizon=0;
    unsigned memberCount=0;
    std::array<Member,maximum_members> members{};
    double health=0,maximumHealth=0,strength=0,campDamage=1,mitigation=1;
    int gems=0,carriedGems=0;
    Leg outward,escape,returnToCamp,capture,bank;
    int recallTicks=0,captureTicks=0,tributeTicks=0;
    double spawnHealing=0;
    // Future route/quiet premises are explicit caller declarations, separate
    // from current actor/member evidence. This calculation proves neither.
    bool actorKnown=false,memberEvidenceKnown=false,stationarySupported=false;
    bool safeRecall=false,quietNoReset=false;
    TaviInput tavi{};
};
struct Sortie {
    std::array<unsigned,maximum_members> attacksByMember{};
    std::array<double,maximum_members> remainingHealth{};
    unsigned attacks=0;
    double startHealth=0,entryEndHealth=0,combatEndHealth=0,escapeEndHealth=0,returnHealth=0;
    double entryLoss=0,combatLoss=0,escapeLoss=0,returnLoss=0;
    int beganAt=0,combatStartedAt=0,combatEndedAt=0,escapeEndedAt=0;
    int recallEndedAt=0,healingEndedAt=0,endedAt=0;
    bool recovered=false;
    unsigned turnBegin=0,turnCount=0;
    int combatEntryResetCooldown=0,combatEntryBurstCooldown=0;
    bool combatEntryCanAttack=false,combatEntryCanCast=false,combatEntryBurstAllowed=false;
};
struct Conversion {int completedAt=0,gems=0;};
struct Schedule {
    std::array<Sortie,maximum_sorties> phases{};
    unsigned sortieCount=0,attackCount=0;
    int tau=0,deltaS=0,terminalBag=0;
    // Cumulative charged exposure may exceed initial HP because intermediate
    // healing is explicit. It is NOT a death indicator or terminal deficit.
    double healthLoss=0,minHealth=0,terminalHealth=0;
    Conversion conversion;
    // Flat storage avoids four large per-sortie action arrays. Indices refer
    // to ORIGINAL Input member slots; a turn never retargets after a kill.
    std::array<CombatTurn,maximum_combat_turns> turns{};
    unsigned turnCount=0,burstCount=0,resetCount=0;
    int remainingResetCooldown=0,remainingBurstCooldown=0;
    bool stunCredited=false; // Full pre-turn incoming is charged even after CC.
};
enum class EvaluationPhase { validation,sortie,entry,attack,escape,recall,healing,returning,conversion,publication };
// Model-work diagnostics only. Phase is the most recently entered code block,
// including the block whose Ready checkpoint denied work; not a native phase.
// Attack counters count committed modeled attacks, never physical actions.
struct EvaluationWork {
    unsigned checkpoints=0,prefixChecks=0,fastAttacks=0,scalarAttacks=0;
    EvaluationPhase phase=EvaluationPhase::validation;
};
struct Result {
    State state=State::unknown;
    Reason reason=Reason::evidence;
    std::optional<Schedule> schedule;
    EvaluationWork work{};
};
inline bool scalar(double value) {return std::isfinite(value) && value>=0 && value<=maximum_scalar;}
inline Result stop(State state,Reason reason){return {state,reason,std::nullopt};}

// Necessary bound for the EXISTING fixed key order, only in a small exact
// integer domain. Recovery pauses but never changes which member dies next.
// Legs/healing/time are ignored, so this cannot admit or score a job. With at
// most8 members and each quantity<=1e6, combatLoss<=36e12 and 4*loss fits uint64.
inline bool combat_budget_exceeded(const Input& in) {
    const auto integer=[](double x){return std::isfinite(x) && x>=0 && x<=1000000 && x==double(std::uint64_t(x));};
    const double damage=in.strength*in.campDamage;
    if(in.tavi.enabled || !in.memberCount || in.memberCount>maximum_members || !in.actorKnown || !in.memberEvidenceKnown || !in.quietNoReset ||
       in.carriedGems || !scalar(in.strength) || in.strength<=0 || !scalar(in.campDamage) || in.campDamage<=0 ||
       in.mitigation!=1 || !integer(damage) || damage<=0 || !integer(in.health) || in.health<=0 ||
       !integer(in.maximumHealth) || in.maximumHealth<=0 || in.health>in.maximumHealth)return false;
    std::array<unsigned,maximum_members> order{};std::uint64_t living=0;
    for(unsigned i=0;i<in.memberCount;++i) {
        if(!in.members[i].known || in.members[i].key<0 || !integer(in.members[i].health) || !integer(in.members[i].incoming))return false;
        for(unsigned j=0;j<i;++j)if(in.members[j].key==in.members[i].key)return false;
        order[i]=i;if(in.members[i].health>0)living+=std::uint64_t(in.members[i].incoming);
    }
    std::sort(order.begin(),order.begin()+in.memberCount,[&](unsigned a,unsigned b){return in.members[a].key<in.members[b].key;});
    const auto d=std::uint64_t(damage);std::uint64_t loss=0;
    for(unsigned n=0;n<in.memberCount;++n) {
        const auto& member=in.members[order[n]];const auto hp=std::uint64_t(member.health);if(!hp)continue;
        const auto attacks=hp/d+std::uint64_t(hp%d!=0);loss+=attacks*living;living-=std::uint64_t(member.incoming);
    }
    static_assert(maximum_sorties>=2);
    const auto available=4*std::uint64_t(in.health)+(3*maximum_sorties-4)*std::uint64_t(in.maximumHealth);
    return 4*loss>available;
}

// Certify a common exact binary64 lattice for a bounded attack prefix. The
// operands are the ALREADY rounded baseline doubles, not ideal decimal costs.
// 8192*max bounds every product (n<=512), sum and left-associated subtraction
// below with ample headroom. A <2^52 integer lattice makes all intermediate
// arithmetic exact; arbitrary fractional inputs keep the sequential fallback.
inline bool exact_attack_prefix(std::initializer_list<double> values) {
    static_assert(sizeof(double)==sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559 &&
        std::numeric_limits<double>::digits==53 && std::numeric_limits<double>::max_exponent==1024);
    int quantum=INT_MAX,largestExponent=INT_MIN;
    for(double value:values) {
        std::uint64_t bits=0;std::memcpy(&bits,&value,sizeof(bits));
        const unsigned exponent=unsigned((bits>>52)&0x7ffu);
        if(exponent==0x7ffu || ((bits>>63) && (bits<<1)))return false;
        const auto significand=(bits&0xfffffffffffffu)|(exponent?(std::uint64_t{1}<<52):0);
        if(!significand)continue;
        const int trailing=__builtin_ctzll(significand);
        const int unit=exponent?int(exponent)-1075:-1074;
        const int top=exponent?int(exponent)-1022:-1074+64-__builtin_clzll(significand);
        quantum=std::min(quantum,unit+trailing);
        largestExponent=std::max(largestExponent,top);
    }
    return quantum!=INT_MAX && largestExponent+13-quantum<=52;
}
template<class Ready> Result evaluate_basic(const Input& in,Ready&& ready) {
    EvaluationWork work;
    const auto checkpoint=[&]{++work.checkpoints;return ready();};
    const auto finish=[&](State state,Reason reason){return Result{state,reason,std::nullopt,work};};
    if(!checkpoint())return finish(State::unknown,Reason::cutoff);
    if(in.round<0 || in.horizon<0 || in.horizon>maximum_turns ||
       in.round>INT_MAX-in.horizon || !in.memberCount || in.memberCount>maximum_members ||
       in.gems<=0 || in.carriedGems<0)return finish(State::invalid,Reason::shape);
    if(!scalar(in.health) || !scalar(in.maximumHealth) || in.maximumHealth<=0 || in.health>in.maximumHealth ||
       !scalar(in.strength) || !scalar(in.campDamage) || !scalar(in.mitigation) ||
       !scalar(in.spawnHealing) || in.spawnHealing<=0 || in.spawnHealing>1 ||
       in.strength<=0 || in.campDamage<=0)return finish(State::invalid,Reason::numeric);
    bool missing=!in.actorKnown || !in.memberEvidenceKnown;
    std::array<double,maximum_members> hp{};
    double totalHealth=0,totalIncoming=0;
    for(unsigned i=0;i<in.memberCount;++i) {
        if(!checkpoint())return finish(State::unknown,Reason::cutoff);
        const auto& m=in.members[i];
        if(m.key<0 || !scalar(m.health) || !scalar(m.incoming))return finish(State::invalid,Reason::numeric);
        for(unsigned j=0;j<i;++j)if(in.members[j].key==m.key)return finish(State::invalid,Reason::shape);
        missing|=!m.known;hp[i]=m.health;totalHealth+=m.health;
        if(m.health>0)totalIncoming+=m.incoming;
    }
    if(!scalar(totalHealth) || !scalar(totalIncoming))return finish(State::invalid,Reason::numeric);
    bool supported=in.stationarySupported && in.safeRecall;
    for(const auto& leg:{in.outward,in.escape,in.returnToCamp,in.capture,in.bank}) {
        if(!checkpoint())return finish(State::unknown,Reason::cutoff);
        if(leg.turns<0 || leg.turns>maximum_turns || leg.exposureTurns<0 || leg.exposureTurns>leg.turns)
            return finish(State::invalid,Reason::numeric);
        missing|=!leg.known;supported&=leg.supported;
    }
    if(in.recallTicks<0 || in.recallTicks>maximum_turns || in.captureTicks<0 ||
       in.captureTicks>=maximum_turns || in.tributeTicks<1 || in.tributeTicks>maximum_turns)
        return finish(State::invalid,Reason::numeric);
    if(missing)return finish(State::unknown,Reason::evidence);
    if(!in.quietNoReset)return finish(State::unknown,Reason::continuity);
    if(!supported)return finish(State::rejected,Reason::route);
    if(in.carriedGems)return finish(State::rejected,Reason::old_bag);
    if(totalHealth<=0)return finish(State::rejected,Reason::no_progress);
    const double damage=in.strength*in.campDamage,healRate=in.spawnHealing*in.maximumHealth;
    if(!scalar(damage) || damage<=0 || !scalar(healRate) || healRate<=0)
        return finish(State::invalid,Reason::numeric);
    if(combat_budget_exceeded(in))return finish(State::rejected,Reason::combat_budget_bound);
    const double reserve=in.maximumHealth*.25;
    double health=in.health;
    Schedule schedule;schedule.minHealth=health;
    int elapsed=0;
    const auto advance=[&](int turns) {elapsed+=turns;return elapsed<=in.horizon;};
    const auto incoming=[&] {
        double sum=0;for(unsigned i=0;i<in.memberCount;++i)if(hp[i]>0)sum+=in.members[i].incoming;
        return sum;
    };
    const auto target=[&] {
        int best=-1;for(unsigned i=0;i<in.memberCount;++i)if(hp[i]>0 &&
            (best<0 || in.members[i].key<in.members[unsigned(best)].key))best=int(i);
        return best;
    };
    const auto charge=[&](double loss) {
        if(!scalar(loss))return false;
        health-=loss;schedule.healthLoss+=loss;schedule.minHealth=std::min(schedule.minHealth,health);
        return std::isfinite(health) && health>0 && health>=reserve;
    };
    for(unsigned number=0;number<maximum_sorties;++number) {
        work.phase=EvaluationPhase::sortie;
        if(!checkpoint())return finish(State::unknown,Reason::cutoff);
        auto& phase=schedule.phases[number];++schedule.sortieCount;
        phase.beganAt=elapsed;phase.startHealth=health;
        if(number==0) {
            work.phase=EvaluationPhase::entry;
            phase.entryLoss=incoming()*in.mitigation*in.outward.exposureTurns;
            if(!charge(phase.entryLoss))return finish(State::rejected,Reason::unsafe_entry);
            if(!advance(in.outward.turns))return finish(State::rejected,Reason::deadline);
        }
        phase.entryEndHealth=health;phase.combatStartedAt=elapsed;
        work.phase=EvaluationPhase::attack;
        while(target()>=0) {
            if(!checkpoint())return finish(State::unknown,Reason::cutoff);
            if(schedule.attackCount>=maximum_attacks)return finish(State::rejected,Reason::attack_bound);
            const unsigned at=unsigned(target());
            const double nextHP=std::max(0.0,hp[at]-damage);
            if(nextHP==hp[at])return finish(State::rejected,Reason::no_progress);
            // Conservatively charge the whole PRE-attack living group once.
            // A kill reduces incoming on subsequent attacks and on escape.
            const double live=incoming(),loss=live*in.mitigation;
            double afterIncoming=0;
            for(unsigned i=0;i<in.memberCount;++i)if(hp[i]>0 && (i!=at || nextHP>0))
                afterIncoming+=in.members[i].incoming;
            const double escapeLoss=afterIncoming*in.mitigation*in.escape.exposureTurns;
            if(!scalar(loss) || !scalar(escapeLoss))return finish(State::invalid,Reason::numeric);
            if(health-loss-escapeLoss<reserve)break;
            bool certified=false;
            if(nextHP>0 && reserve>0) {
                ++work.prefixChecks;certified=exact_attack_prefix({hp[at],damage,health,loss,escapeLoss,reserve,
                    schedule.healthLoss,phase.combatLoss});
            }
            if(certified) {
                // Aggregate only a nonkilling prefix. Its incoming/escape cost
                // stays constant. The final kill still uses the original path
                // below, charging the full PRE-kill incoming before it drops.
                unsigned low=0,high=maximum_attacks-schedule.attackCount+1;
                for(unsigned comparison=0;high-low>1 && comparison<10;++comparison) {
                    const unsigned n=low+(high-low)/2;
                    if(hp[at]-double(n)*damage>0 && (health-double(n)*loss)-escapeLoss>=reserve)low=n;
                    else high=n;
                }
                if(low>1) {
                    // Earlier allowed attacks would encounter the deadline
                    // before any later event. Attack-cap precedence is kept by
                    // returning to the original top-of-loop guard after a cap.
                    if(low>unsigned(in.horizon-elapsed))return finish(State::rejected,Reason::deadline);
                    const double charged=double(low)*loss;
                    health-=charged;schedule.healthLoss+=charged;phase.combatLoss+=charged;
                    schedule.minHealth=std::min(schedule.minHealth,health);hp[at]-=double(low)*damage;
                    elapsed+=int(low);phase.attacksByMember[at]+=low;phase.attacks+=low;schedule.attackCount+=low;
                    work.fastAttacks+=low;
                    continue;
                }
            }
            if(!charge(loss))return finish(State::rejected,Reason::unsafe_attack);
            if(!advance(1))return finish(State::rejected,Reason::deadline);
            phase.combatLoss+=loss;hp[at]=nextHP;
            ++phase.attacksByMember[at];++phase.attacks;++schedule.attackCount;
            ++work.scalarAttacks;
        }
        phase.remainingHealth=hp;phase.combatEndHealth=health;phase.combatEndedAt=elapsed;
        phase.escapeEndHealth=phase.returnHealth=health;
        phase.escapeEndedAt=phase.recallEndedAt=phase.healingEndedAt=phase.endedAt=elapsed;
        if(target()<0) {
            work.phase=EvaluationPhase::conversion;
            if(!checkpoint())return finish(State::unknown,Reason::cutoff);
            if(!advance(in.capture.turns) || !advance(in.captureTicks+1) ||
               !advance(in.bank.turns) || !advance(in.tributeTicks))return finish(State::rejected,Reason::deadline);
            schedule.tau=elapsed;schedule.deltaS=in.gems;schedule.terminalHealth=health;
            schedule.conversion={elapsed,in.gems};
            work.phase=EvaluationPhase::publication;
            if(!checkpoint())return finish(State::unknown,Reason::cutoff);
            return {State::admitted,Reason::none,std::move(schedule),work};
        }
        if(!phase.attacks)return finish(State::rejected,Reason::no_progress);
        if(number+1==maximum_sorties)return finish(State::rejected,Reason::sortie_bound);
        phase.recovered=true;
        work.phase=EvaluationPhase::escape;
        if(!checkpoint())return finish(State::unknown,Reason::cutoff);
        phase.escapeLoss=incoming()*in.mitigation*in.escape.exposureTurns;
        if(!charge(phase.escapeLoss))return finish(State::rejected,Reason::unsafe_escape);
        if(!advance(in.escape.turns))return finish(State::rejected,Reason::deadline);
        phase.escapeEndHealth=health;phase.escapeEndedAt=elapsed;
        work.phase=EvaluationPhase::recall;
        if(!checkpoint())return finish(State::unknown,Reason::cutoff);
        if(!advance(std::max(1,in.recallTicks)))return finish(State::rejected,Reason::deadline);
        phase.recallEndedAt=elapsed;
        work.phase=EvaluationPhase::healing;
        if(!checkpoint())return finish(State::unknown,Reason::cutoff);
        const double heal=health<in.maximumHealth ?
            std::max(1.0,std::ceil((in.maximumHealth-health)/healRate)):0;
        if(!std::isfinite(heal) || heal>maximum_turns)return finish(State::invalid,Reason::numeric);
        // Recall arrives at owner start. Each heal tick occupies a separate
        // spawn turn end; return starts at the following owner start.
        if(!advance(int(heal)))return finish(State::rejected,Reason::deadline);
        health=in.maximumHealth;phase.healingEndedAt=elapsed;
        work.phase=EvaluationPhase::returning;
        if(!checkpoint())return finish(State::unknown,Reason::cutoff);
        phase.returnLoss=incoming()*in.mitigation*in.returnToCamp.exposureTurns;
        if(!charge(phase.returnLoss))return finish(State::rejected,Reason::unsafe_return);
        if(!advance(in.returnToCamp.turns))return finish(State::rejected,Reason::deadline);
        phase.returnHealth=health;phase.endedAt=elapsed;
    }
    return finish(State::rejected,Reason::sortie_bound);
}
// Stationary, fixed-target owner-turn reference policy for queried Tavi kits.
// No stun mitigation is claimed: pre-turn whole-group incoming is charged
// once even if this turn's actions kill/stun a member. Cooldowns, unlike stun
// duration, count OWNER TURN ENDS and include every charged travel/recovery tick.
// Private hypothetical computation only. No SDK references or Ready callback
// survive an advance(). A denied checkpoint leaves the cursor pending; only a
// terminal result may expose its Schedule. Clock equality is deliberately not
// a semantic cache key: callers must freshly validate live facts, then take the
// complete result with their latest remaining horizon.
class TaviEvaluation {
    enum class Stage { idle,shape,members,legs,validated,sortie,combat,escape,recall,healing,returning,conversion,publication,done };
    Input in_{};
    Result result_{};
    Stage stage_=Stage::idle;
    std::array<double,maximum_members> hp_{};
    unsigned member_=0,leg_=0,number_=0,ownerTurns_=0;
    double totalHealth_=0,totalIncoming_=0,health_=0,damage_=0,burst_=0,healRate_=0,reserve_=0;
    int elapsed_=0,reset_=0,burstCD_=0;
    bool missing_=false,supported_=false,taken_=false;
    Schedule& out(){return *result_.schedule;}
    Sortie& phase(){return out().phases[number_];}
    bool finish(State state,Reason reason) {
        result_.state=state;result_.reason=reason;
        if(state!=State::admitted)result_.schedule.reset();
        stage_=Stage::done;return true;
    }
    bool wait(int n) {
        elapsed_+=n;reset_=std::max(0,reset_-n);burstCD_=std::max(0,burstCD_-n);
        return elapsed_<=in_.horizon;
    }
    double incoming() const {
        double n=0;for(unsigned i=0;i<in_.memberCount;++i)if(hp_[i]>0)n+=in_.members[i].incoming;
        return n;
    }
    int victim() const {
        int at=-1;
        for(unsigned i=0;i<in_.memberCount;++i)
            if(hp_[i]>0 && (at<0 || in_.members[i].key<in_.members[unsigned(at)].key))at=int(i);
        return at;
    }
    bool charge(double loss) {
        if(!scalar(loss))return false;
        health_-=loss;out().healthLoss+=loss;out().minHealth=std::min(out().minHealth,health_);
        return std::isfinite(health_) && health_>0 && health_>=reserve_;
    }
    static bool same_leg(const Leg& a,const Leg& b) {
        return a.turns==b.turns && a.exposureTurns==b.exposureTurns && a.known==b.known && a.supported==b.supported;
    }
public:
    void start(const Input& in) {
        in_=in;result_={};stage_=Stage::shape;hp_={};
        member_=leg_=number_=ownerTurns_=0;
        totalHealth_=totalIncoming_=health_=damage_=burst_=healRate_=reserve_=0;
        elapsed_=reset_=burstCD_=0;missing_=supported_=taken_=false;
    }
    bool terminal() const{return stage_==Stage::done;}
    const Result* result() const{return terminal() && !taken_?&result_:nullptr;}
    const EvaluationWork& work() const{return result_.work;}
    unsigned owner_turns() const{return ownerTurns_;}
    // No floating tolerance or byte/padding comparison: each modeled fact is
    // required unchanged. Inactive array slots have no model meaning.
    bool matches(const Input& b) const {
        if(stage_==Stage::idle || taken_ || b.round<0 || b.horizon<0 || b.horizon>maximum_turns ||
           b.round>INT_MAX-b.horizon || in_.memberCount!=b.memberCount || b.memberCount>maximum_members)return false;
        const auto& a=in_;
        if(a.health!=b.health || a.maximumHealth!=b.maximumHealth || a.strength!=b.strength ||
           a.campDamage!=b.campDamage || a.mitigation!=b.mitigation || a.gems!=b.gems ||
           a.carriedGems!=b.carriedGems || a.recallTicks!=b.recallTicks || a.captureTicks!=b.captureTicks ||
           a.tributeTicks!=b.tributeTicks || a.spawnHealing!=b.spawnHealing ||
           a.actorKnown!=b.actorKnown || a.memberEvidenceKnown!=b.memberEvidenceKnown ||
           a.stationarySupported!=b.stationarySupported || a.safeRecall!=b.safeRecall || a.quietNoReset!=b.quietNoReset)return false;
        for(unsigned i=0;i<a.memberCount;++i) {
            const auto& x=a.members[i];const auto& y=b.members[i];
            if(x.key!=y.key || x.health!=y.health || x.incoming!=y.incoming || x.known!=y.known)return false;
        }
        if(!same_leg(a.outward,b.outward) || !same_leg(a.escape,b.escape) ||
           !same_leg(a.returnToCamp,b.returnToCamp) || !same_leg(a.capture,b.capture) || !same_leg(a.bank,b.bank))return false;
        const auto& x=a.tavi;const auto& y=b.tavi;
        return x.enabled==y.enabled && x.known==y.known && x.focus==y.focus &&
            x.burstModifier==y.burstModifier && x.burstCampDamage==y.burstCampDamage &&
            x.burstRange==y.burstRange && x.stunRounds==y.stunRounds &&
            x.resetCooldown==y.resetCooldown && x.burstCooldown==y.burstCooldown &&
            x.resetMaxCooldown==y.resetMaxCooldown && x.burstMaxCooldown==y.burstMaxCooldown &&
            x.initialCanAttack==y.initialCanAttack && x.initialCanCast==y.initialCanCast &&
            x.initialBurstAllowed==y.initialBurstAllowed;
    }
    // A rejected/invalid result remains that result. Only complete admission
    // is rechecked against the current relative deadline, without re-running
    // arithmetic or granting a stale conversion. Taking consumes this result.
    std::optional<Result> take_complete(int latestHorizon) {
        if(!terminal() || taken_)return std::nullopt;
        if(result_.state==State::admitted) {
            if(latestHorizon<0 || latestHorizon>maximum_turns)finish(State::invalid,Reason::shape);
            else if(result_.schedule->tau>latestHorizon)finish(State::rejected,Reason::deadline);
        }
        taken_=true;return std::move(result_);
    }
    template<class Ready> bool advance(Ready&& ready,unsigned maxOwnerTurns=4) {
        if(terminal())return true;
        if(stage_==Stage::idle || !maxOwnerTurns)return false;
        maxOwnerTurns=std::min(maxOwnerTurns,maximum_combat_turns);
        unsigned completed=0;
        auto& work=result_.work;const auto& kit=in_.tavi;
        const auto checkpoint=[&]{++work.checkpoints;return ready();};
        while(true) {
            switch(stage_) {
            case Stage::shape:
                if(!checkpoint())return false;
                if(in_.round<0 || in_.horizon<0 || in_.horizon>maximum_turns || in_.round>INT_MAX-in_.horizon ||
                   !in_.memberCount || in_.memberCount>maximum_members || in_.gems<=0 || in_.carriedGems<0)
                    return finish(State::invalid,Reason::shape);
                if(!scalar(in_.health) || !scalar(in_.maximumHealth) || in_.maximumHealth<=0 || in_.health>in_.maximumHealth ||
                   !scalar(in_.strength) || in_.strength<=0 || !scalar(in_.campDamage) || in_.campDamage<=0 ||
                   !scalar(in_.mitigation) || !scalar(in_.spawnHealing) || in_.spawnHealing<=0 || in_.spawnHealing>1)
                    return finish(State::invalid,Reason::numeric);
                if(!kit.known)return finish(State::unknown,Reason::ability);
                if(!scalar(kit.focus) || !scalar(kit.burstModifier) || kit.burstModifier<=0 ||
                   !scalar(kit.burstCampDamage) || kit.burstCampDamage<=0 || kit.burstRange<1 || kit.burstRange>16 ||
                   kit.stunRounds<0 || kit.stunRounds>maximum_turns || kit.resetMaxCooldown<1 || kit.resetMaxCooldown>maximum_turns ||
                   kit.burstMaxCooldown<1 || kit.burstMaxCooldown>maximum_turns || kit.resetCooldown<0 ||
                   kit.resetCooldown>kit.resetMaxCooldown || kit.burstCooldown<0 || kit.burstCooldown>kit.burstMaxCooldown)
                    return finish(State::invalid,Reason::ability);
                missing_=!in_.actorKnown || !in_.memberEvidenceKnown;stage_=Stage::members;break;
            case Stage::members:
                if(member_<in_.memberCount) {
                    if(!checkpoint())return false;
                    const auto& m=in_.members[member_];
                    if(m.key<0 || !scalar(m.health) || !scalar(m.incoming))return finish(State::invalid,Reason::numeric);
                    for(unsigned j=0;j<member_;++j)if(m.key==in_.members[j].key)return finish(State::invalid,Reason::shape);
                    missing_|=!m.known;hp_[member_]=m.health;totalHealth_+=m.health;
                    if(m.health>0)totalIncoming_+=m.incoming;
                    ++member_;break;
                }
                if(!scalar(totalHealth_) || !scalar(totalIncoming_))return finish(State::invalid,Reason::numeric);
                supported_=in_.stationarySupported && in_.safeRecall;stage_=Stage::legs;break;
            case Stage::legs:
                if(leg_<5) {
                    if(!checkpoint())return false;
                    const Leg* legs[]={&in_.outward,&in_.escape,&in_.returnToCamp,&in_.capture,&in_.bank};
                    const auto& leg=*legs[leg_];
                    if(leg.turns<0 || leg.turns>maximum_turns || leg.exposureTurns<0 || leg.exposureTurns>leg.turns)
                        return finish(State::invalid,Reason::numeric);
                    missing_|=!leg.known;supported_&=leg.supported;++leg_;break;
                }
                stage_=Stage::validated;break;
            case Stage::validated:
                if(in_.recallTicks<0 || in_.recallTicks>maximum_turns || in_.captureTicks<0 || in_.captureTicks>=maximum_turns ||
                   in_.tributeTicks<1 || in_.tributeTicks>maximum_turns)return finish(State::invalid,Reason::numeric);
                if(missing_)return finish(State::unknown,Reason::evidence);
                if(!in_.quietNoReset)return finish(State::unknown,Reason::continuity);
                if(!supported_)return finish(State::rejected,Reason::route);
                if(in_.carriedGems)return finish(State::rejected,Reason::old_bag);
                if(totalHealth_<=0)return finish(State::rejected,Reason::no_progress);
                damage_=in_.strength*in_.campDamage;burst_=kit.focus*kit.burstModifier*kit.burstCampDamage;
                healRate_=in_.spawnHealing*in_.maximumHealth;reserve_=in_.maximumHealth*.25;
                if(!scalar(damage_) || damage_<=0 || !scalar(burst_) || !scalar(healRate_) || healRate_<=0)
                    return finish(State::invalid,Reason::numeric);
                result_.schedule.emplace();out().minHealth=in_.health;health_=in_.health;
                reset_=kit.resetCooldown;burstCD_=kit.burstCooldown;stage_=Stage::sortie;break;
            case Stage::sortie: {
                work.phase=EvaluationPhase::sortie;if(!checkpoint())return false;
                auto& p=phase();++out().sortieCount;p.beganAt=elapsed_;p.startHealth=health_;p.turnBegin=out().turnCount;
                if(number_==0) {
                    work.phase=EvaluationPhase::entry;p.entryLoss=incoming()*in_.mitigation*in_.outward.exposureTurns;
                    if(!charge(p.entryLoss))return finish(State::rejected,Reason::unsafe_entry);
                    if(!wait(in_.outward.turns))return finish(State::rejected,Reason::deadline);
                }
                p.entryEndHealth=health_;p.combatStartedAt=elapsed_;
                p.combatEntryResetCooldown=reset_;p.combatEntryBurstCooldown=burstCD_;
                p.combatEntryCanAttack=elapsed_>0 || kit.initialCanAttack;
                p.combatEntryCanCast=elapsed_>0 || kit.initialCanCast;
                p.combatEntryBurstAllowed=elapsed_>0 || kit.initialBurstAllowed;
                work.phase=EvaluationPhase::attack;stage_=Stage::combat;break;
            }
            case Stage::combat: {
                auto& p=phase();const int target=victim();
                if(target>=0) {
                    if(completed>=maxOwnerTurns && out().turnCount<maximum_combat_turns)return false;
                    if(!checkpoint())return false;
                    if(out().turnCount>=maximum_combat_turns)return finish(State::rejected,Reason::turn_bound);
                    const unsigned at=unsigned(target);CombatTurn turn;turn.member=std::uint8_t(at);
                    double after=hp_[at];unsigned basics=0;int nextReset=reset_,nextBurst=burstCD_;
                    const bool canAttack=elapsed_>0 || kit.initialCanAttack;
                    const bool canCast=elapsed_>0 || kit.initialCanCast;
                    const bool burstAllowed=elapsed_>0 || kit.initialBurstAllowed;
                    const auto basic=[&] {
                        if(out().attackCount+basics>=maximum_attacks)return false;
                        const double next=std::max(0.0,after-damage_);if(next==after)return false;
                        after=next;++basics;return true;
                    };
                    if(canCast && burstAllowed && burstCD_==0 && burst_>0) {
                        turn.cast=TaviCast::burst;nextBurst=kit.burstMaxCooldown;
                        after=std::max(0.0,after-burst_);
                        if(after>0 && canAttack) {
                            if(!basic())return finish(State::rejected,out().attackCount>=maximum_attacks?Reason::attack_bound:Reason::no_progress);
                            turn.afterBasic=1;
                        }
                    } else {
                        if(canAttack) {
                            if(!basic())return finish(State::rejected,out().attackCount>=maximum_attacks?Reason::attack_bound:Reason::no_progress);
                            turn.beforeBasic=1;
                        }
                        if(after>0 && canCast && reset_==0 && out().attackCount+basics<maximum_attacks) {
                            turn.cast=TaviCast::reset;nextReset=kit.resetMaxCooldown;
                            if(!basic())return finish(State::rejected,Reason::no_progress);
                            turn.afterBasic=1;
                        }
                    }
                    if(!basics && turn.cast==TaviCast::none && elapsed_>0)
                        return finish(State::rejected,out().attackCount>=maximum_attacks?Reason::attack_bound:Reason::no_progress);
                    if(after==hp_[at] && (basics || turn.cast!=TaviCast::none))return finish(State::rejected,Reason::no_progress);
                    const double loss=incoming()*in_.mitigation;
                    double surviving=0;
                    for(unsigned i=0;i<in_.memberCount;++i)if(hp_[i]>0 && (i!=at || after>0))surviving+=in_.members[i].incoming;
                    const double escapeLoss=surviving*in_.mitigation*in_.escape.exposureTurns;
                    if(!scalar(loss) || !scalar(escapeLoss))return finish(State::invalid,Reason::numeric);
                    if(health_-loss-escapeLoss>=reserve_) {
                        if(!charge(loss))return finish(State::rejected,Reason::unsafe_attack);
                        reset_=nextReset;burstCD_=nextBurst;
                        if(!wait(1))return finish(State::rejected,Reason::deadline);
                        hp_[at]=after;p.combatLoss+=loss;p.attacksByMember[at]+=basics;
                        p.attacks+=basics;out().attackCount+=basics;work.scalarAttacks+=basics;
                        out().burstCount+=turn.cast==TaviCast::burst;out().resetCount+=turn.cast==TaviCast::reset;
                        out().turns[out().turnCount++]=turn;++p.turnCount;++ownerTurns_;++completed;break;
                    }
                }
                p.remainingHealth=hp_;p.combatEndHealth=health_;p.combatEndedAt=elapsed_;
                p.escapeEndHealth=p.returnHealth=health_;
                p.escapeEndedAt=p.recallEndedAt=p.healingEndedAt=p.endedAt=elapsed_;
                if(target<0){stage_=Stage::conversion;break;}
                if(!p.turnCount)return finish(State::rejected,Reason::no_progress);
                if(number_+1==maximum_sorties)return finish(State::rejected,Reason::sortie_bound);
                p.recovered=true;work.phase=EvaluationPhase::escape;stage_=Stage::escape;break;
            }
            case Stage::escape: {
                if(!checkpoint())return false;
                auto& p=phase();p.escapeLoss=incoming()*in_.mitigation*in_.escape.exposureTurns;
                if(!charge(p.escapeLoss))return finish(State::rejected,Reason::unsafe_escape);
                if(!wait(in_.escape.turns))return finish(State::rejected,Reason::deadline);
                p.escapeEndHealth=health_;p.escapeEndedAt=elapsed_;
                work.phase=EvaluationPhase::recall;stage_=Stage::recall;break;
            }
            case Stage::recall:
                if(!checkpoint())return false;
                if(!wait(std::max(1,in_.recallTicks)))return finish(State::rejected,Reason::deadline);
                phase().recallEndedAt=elapsed_;work.phase=EvaluationPhase::healing;stage_=Stage::healing;break;
            case Stage::healing: {
                if(!checkpoint())return false;
                const double heal=health_<in_.maximumHealth?std::max(1.0,std::ceil((in_.maximumHealth-health_)/healRate_)):0;
                if(!std::isfinite(heal) || heal>maximum_turns)return finish(State::invalid,Reason::numeric);
                if(!wait(int(heal)))return finish(State::rejected,Reason::deadline);
                health_=in_.maximumHealth;phase().healingEndedAt=elapsed_;
                work.phase=EvaluationPhase::returning;stage_=Stage::returning;break;
            }
            case Stage::returning: {
                if(!checkpoint())return false;
                auto& p=phase();p.returnLoss=incoming()*in_.mitigation*in_.returnToCamp.exposureTurns;
                if(!charge(p.returnLoss))return finish(State::rejected,Reason::unsafe_return);
                if(!wait(in_.returnToCamp.turns))return finish(State::rejected,Reason::deadline);
                p.returnHealth=health_;p.endedAt=elapsed_;++number_;stage_=Stage::sortie;break;
            }
            case Stage::conversion:
                work.phase=EvaluationPhase::conversion;if(!checkpoint())return false;
                if(!wait(in_.capture.turns) || !wait(in_.captureTicks+1) || !wait(in_.bank.turns) || !wait(in_.tributeTicks))
                    return finish(State::rejected,Reason::deadline);
                out().tau=elapsed_;out().deltaS=in_.gems;out().terminalHealth=health_;out().conversion={elapsed_,in_.gems};
                out().remainingResetCooldown=reset_;out().remainingBurstCooldown=burstCD_;
                work.phase=EvaluationPhase::publication;stage_=Stage::publication;break;
            case Stage::publication:
                if(!checkpoint())return false;
                return finish(State::admitted,Reason::none);
            case Stage::done:return true;
            case Stage::idle:return false;
            }
        }
    }
};

template<class Ready> Result evaluate_tavi(const Input& in,Ready&& ready) {
    TaviEvaluation cursor;cursor.start(in);
    if(!cursor.advance(ready,maximum_combat_turns))return {State::unknown,Reason::cutoff,std::nullopt,cursor.work()};
    return std::move(*cursor.take_complete(in.horizon));
}

template<class Ready> Result evaluate(const Input& in,Ready&& ready) {
    return in.tavi.enabled?evaluate_tavi(in,ready):evaluate_basic(in,ready);
}
inline Result evaluate(const Input& in) {return evaluate(in,[]{return true;});}
} // namespace plan3_sortie
