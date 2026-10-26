#pragma once

// Plan 3 analytical-v4. No SDK calls or hidden-state access belong in this file.
// Arithmetic tests establish model invariants, never gameplay accuracy. Runtime
// adapters must label predictions and satisfy the separately recorded job audit.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace plan3 {

inline constexpr const char* model_version = "p3-analytical-v17";
inline constexpr unsigned single_job_cap = 16, double_job_cap = 8;
inline constexpr unsigned nonharvest_job_cap = 4, outcome_cap = 12;
inline constexpr unsigned renewal_loop_cap = 8;
inline constexpr double comparison_tolerance = 1e-9;

enum class Provenance { unknown, public_design, personally_observed,
                        mechanically_calculated, empirically_calibrated, assumed };
template<class T> struct Record {
    std::optional<T> value;
    Provenance provenance = Provenance::unknown;
    int observedRound = -1;
};
enum class DistanceStatus { unknown, unreachable, known };
struct Distance {
    DistanceStatus status = DistanceStatus::unknown;
    int turns = 0;
    static Distance known(int n) {
        return n < 0 ? Distance{DistanceStatus::unknown, 0}
                     : Distance{DistanceStatus::known, n};
    }
    bool reachable() const { return status == DistanceStatus::known && turns >= 0; }
};
struct Position { int tile = -1; };
struct EquipmentRecord {
    // Exact SDK item ordinals and quantities, not an inferred build tier.
    std::vector<std::pair<int, unsigned>> items;
    Provenance provenance = Provenance::unknown;
};
struct GroupObservation {
    int minimumSpawnTile = -1;
    Record<int> lifecycle;
    std::vector<Record<double>> memberHealth;
    // A group query requires one visible member spawn; each live camp query
    // needs its own visibility. The adapter supplies these independently.
    bool memberSpawnCurrentlyVisible = false;
};
struct ChampionObservation {
    int championOrdinal = -1;
    Record<Position> position;
    Record<double> health;
    // There is deliberately no other-champion gold field.
};
struct InformationState {
    int round = 0, roundLimit = 0, winningGems = 0;
    int ownPermanentScore = 0, opponentPermanentScore = 0;
    int carriedGems = 0, gold = 0, actorOrdinal = 0, teamSize = 1;
    double health = 0, maximumHealth = 0;
    Position position;
    EquipmentRecord equipment;
    std::vector<GroupObservation> groups;
    std::vector<ChampionObservation> champions;
    int horizon() const { return std::max(0, std::min(128, roundLimit - round)); }
};

// Turns from the current owner-turn start to the owner-turn start at which
// completion is observable. The starting turn supplies the first end tick;
// thus a 1-tick tribute is observed one owner turn later, never immediately.
inline std::optional<int> tribute_completion_turns(int bag, double multiplier,
                                                int base, int minimum) {
    if (bag <= 0 || !std::isfinite(multiplier) || multiplier < 0 || minimum < 0)
        return std::nullopt;
    const double n = std::ceil(bag * multiplier) + base;
    if (!std::isfinite(n) || n > std::numeric_limits<int>::max()) return std::nullopt;
    return std::max(1, std::max(minimum, static_cast<int>(n)));
}
struct ClearEstimate { int turns = 0; double healthLoss = 0; bool admitted = false; };
inline ClearEstimate estimate_clear(double remainingHealth, double incoming,
                                    double strength, double focus,
                                    double ownedCampDamageMultiplier,
                                    double ownedIncomingMultiplier,
                                    double health, double maximumHealth) {
    ClearEstimate r;
    if (!std::isfinite(remainingHealth+incoming+strength+focus+ownedCampDamageMultiplier+
                       ownedIncomingMultiplier+health+maximumHealth) ||
        remainingHealth < 0 || incoming < 0 || strength < 0 || focus < 0 ||
        ownedCampDamageMultiplier <= 0 || ownedIncomingMultiplier < 0 || maximumHealth <= 0)
        return r;
    const double rate = std::max(1.0, (1.125 * strength + 0.12 * focus) *
                                      ownedCampDamageMultiplier);
    const double duration=std::ceil(remainingHealth/rate);
    if (duration>std::numeric_limits<int>::max()) return r;
    r.turns = static_cast<int>(duration);
    r.healthLoss = incoming * ownedIncomingMultiplier * (r.turns + 2);
    r.admitted = std::isfinite(r.healthLoss) && health - r.healthLoss >= maximumHealth * 0.25;
    return r;
}
struct RenewalLoop {
    int groupId = -1;
    int firstConversion = 0, period = 0, gems = 0;
    // Includes recurring recall/heal/return, and required regeneration wait.
    bool supported = false;
    Provenance provenance = Provenance::assumed;
};
inline std::int64_t loop_yield(const RenewalLoop& loop, int duration) {
    if (!loop.supported || loop.firstConversion <= 0 || loop.period <= 0 || loop.gems < 0 ||
        duration < loop.firstConversion) return 0;
    return std::int64_t(loop.gems) * (1LL + (duration - loop.firstConversion) / loop.period);
}
inline std::int64_t renewal_yield(const std::vector<RenewalLoop>& loops, int duration) {
    std::int64_t result = 0;
    for (std::size_t i = 0; i < std::min<std::size_t>(loops.size(), renewal_loop_cap); ++i)
        result = std::max(result, loop_yield(loops[i], duration));
    return result;
}
inline double renewal_rate(const std::vector<RenewalLoop>& loops) {
    double result = 0;
    for (std::size_t i = 0; i < std::min<std::size_t>(loops.size(), renewal_loop_cap); ++i)
        if (loops[i].supported && loops[i].firstConversion > 0 && loops[i].period > 0 && loops[i].gems >= 0)
            result = std::max(result, double(loops[i].gems) / loops[i].period);
    return result;
}
struct PotentialState {
    int bag = 0;
    double health = 0, maximumHealth = 0;
    Position position;
    EquipmentRecord equipment;
    // Cashout endpoint position is the productive position used by the loops.
    // With no bag cashoutTurns is ignored. Missing route never means zero.
    std::optional<int> cashoutTurns;
    // One-time escape/recall/heal/return to cashout endpoint (or current p).
    // Recurring recovery is separately part of each RenewalLoop.period.
    std::optional<int> preparationTurns;
    std::vector<RenewalLoop> loops;
    bool loopSupportUnavailable = false;
};
inline double potential(const PotentialState& state, int duration, int scoreCapacity) {
    if (duration < 0 || scoreCapacity <= 0 || state.bag < 0) return 0;
    int elapsed = 0;
    double bag = 0;
    if (state.bag > 0) {
        if (!state.cashoutTurns || *state.cashoutTurns <= 0 || *state.cashoutTurns > duration)
            return 0;
        elapsed = *state.cashoutTurns;
        bag = state.bag;
    }
    if (bag >= scoreCapacity) return scoreCapacity;
    const bool healthy = state.maximumHealth > 0 && state.health >= state.maximumHealth;
    int preparation = 0;
    if (!healthy) {
        if (!state.preparationTurns || *state.preparationTurns < 0)
            return std::min<double>(bag, scoreCapacity);
        preparation = *state.preparationTurns;
    }
    const int productive = std::max(0, duration - elapsed - preparation);
    const double future = elapsed + preparation <= duration
        ? double(renewal_yield(state.loops, productive)) : 0;
    return std::min<double>(scoreCapacity, bag + future);
}

enum class JobKind { cycle, double_cycle, bank, renew, invest, observe };
// Cycle and Double retain their acquisition ID while executing conversion.
// Only these stages and an explicit Bank satisfy the winning-bank obligation.
inline bool executing_bank(JobKind kind, unsigned stage) {
    return kind==JobKind::bank ||
        ((kind==JobKind::cycle || kind==JobKind::double_cycle) && stage>=2);
}
struct JobId {
    JobKind kind = JobKind::renew;
    // Physical group IDs are minimum public spawn tile indices. Empty = -1.
    int firstGroup = -1, secondGroup = -1, altarTile = -1, equipmentBlock = -1;
    bool operator<(const JobId& b) const {
        return std::tie(kind, firstGroup, secondGroup, altarTile, equipmentBlock) <
               std::tie(b.kind, b.firstGroup, b.secondGroup, b.altarTile, b.equipmentBlock);
    }
    bool operator==(const JobId& b) const {
        return std::tie(kind, firstGroup, secondGroup, altarTile, equipmentBlock) ==
               std::tie(b.kind, b.firstGroup, b.secondGroup, b.altarTile, b.equipmentBlock);
    }
};
enum class ResponseWorld { production, race, interception, conversion_denial };
struct Conversion { int completedAt = 0, gems = 0; };
struct Outcome {
    ResponseWorld response = ResponseWorld::production;
    int lifecycle = 0;
    double assumedWeight = 1, conditionalLifecycleWeight = 1;
    int deltaS = 0, tau = 0;
    double healthLoss = 0, goldSpent = 0, goldEarned = 0;
    PotentialState terminal;
    std::vector<Conversion> conversions;
    Provenance provenance = Provenance::assumed;
    // Complete phase schedules may include healing between damage episodes.
    // Terminal respawn health cannot establish survival; keep their minimum
    // explicit. Absence retains the one-sortie interpretation of healthLoss.
    std::optional<double> minimumHealth = std::nullopt;
};
inline double outcome_value(const InformationState& info, const Outcome& o) {
    if (o.tau < 0 || o.deltaS < 0) return -std::numeric_limits<double>::infinity();
    const int H = info.horizon();
    if (o.tau > H) {
        int inTime = 0;
        for (const auto& c : o.conversions)
            if (c.completedAt >= 0 && c.completedAt <= H && c.gems > 0) inTime += c.gems;
        return inTime; // No hypothetical post-endpoint continuation outside H.
    }
    const int capacity = std::max(0, info.winningGems - info.ownPermanentScore - o.deltaS);
    return o.deltaS + potential(o.terminal, H - o.tau, capacity);
}
struct InvestmentForecast {
    bool available = false, exactEquipmentReconstruction = false;
    // Checkpoint outputs include current bag/funding conversions once.
    std::array<double, 3> output{};
    std::array<double, 3> deferOutput{};
    bool inheritedNextBlock = false;
    bool positive_payback() const {
        return available && exactEquipmentReconstruction &&
            output[1] > deferOutput[1] && output[2] > deferOutput[2];
    }
    double checkpoint_utility() const { return .25*output[0] + .25*output[1] + .5*output[2]; }
};
enum class InvestmentBlock { defer, monster_strength_emblem, jungle_health_emblem,
                             power_strength_shard, power_focus_shard };
inline bool finite_equal(double a, double b);
inline bool strictly_better(double a, double b);
struct InvestmentSchedule {
    InvestmentBlock block=InvestmentBlock::defer;
    InvestmentForecast forecast;
    bool blockAlreadyCompleted=false;
};
inline std::optional<InvestmentSchedule> best_investment_schedule(
    const std::vector<InvestmentSchedule>& schedules, bool control=false) {
    const InvestmentSchedule* best=nullptr;
    for (const auto& s:schedules) {
        if (s.blockAlreadyCompleted || !s.forecast.available ||
            !s.forecast.exactEquipmentReconstruction) continue;
        if (s.block!=InvestmentBlock::defer && (!s.forecast.positive_payback() ||
            (control && !s.forecast.inheritedNextBlock))) continue;
        if (!best || strictly_better(s.forecast.checkpoint_utility(),best->forecast.checkpoint_utility()) ||
            (finite_equal(s.forecast.checkpoint_utility(),best->forecast.checkpoint_utility()) &&
             s.block<best->block)) best=&s;
    }
    return best ? std::optional<InvestmentSchedule>(*best):std::nullopt;
}
struct JobForecast {
    JobId id;
    std::vector<Outcome> outcomes;
    std::size_t nominalIndex = 0;
    // Scout freezes conservative nominal admission before testing a target.
    // This metadata has no probability mass and is not another scored world.
    std::optional<int> admissionTau;
    bool complete = true, fullClearAdmitted = true, routeKnown = true;
    bool mandatorySafety = false;
    bool renewalRequired = true;
    // Amendment 002: empty-bag gold-only work ends at observed capture, with
    // zero converted score. Its gold rate is never added to a gem objective.
    bool rewardOnlyEndpoint = false;
    int projectedRenewalAndReturnTime = 0;
    int campGems = 0, campGold = 0;
    double travelPlusClear = 0;
    // Relative times, supported only by personal lifecycle/opponent evidence.
    std::optional<int> releaseTime, opponentDeadline;
    double supportedDenialGems = 0;
    bool personalDeadlineEvidence = false, personalReleaseEvidence = false;
    bool owned = false;
    // Rate, inherited shopping, bank, expiring opportunity.
    unsigned proposerMask = 0;
    InvestmentForecast investment;
    std::optional<double> observationNetValue;
    bool observationRevealsTarget = false, observationSafe = false;
    const Outcome* nominal() const {
        return nominalIndex < outcomes.size() ? &outcomes[nominalIndex] : nullptr;
    }
};
struct HorizonUtility { double winPayoff = -1, terminalMargin = 0, duration = 0; bool assumedShare = false; };
// One cache per immutable decision menu. Active/control evaluations can share
// it, but must clear it before changing any job, state, score history, or menu lifetime.
// Rows are bounded by three horizons times the declared 28-job menu. Scalar
// objectives never populate this cache merely to discard unused world values.
struct EvaluationCache {
    struct Row {
        const JobForecast* job=nullptr;
        int horizon=0,winningGems=0,ownScore=0;
        std::array<double,outcome_cap> values{};
        std::array<bool,outcome_cap> known{};
    };
    struct HorizonRow {
        const JobForecast* job=nullptr;
        std::optional<HorizonUtility> value;
    };
    std::vector<Row> rows;
    std::vector<HorizonRow> horizonRows;
    unsigned computations=0,hits=0;
    unsigned horizonComputations=0,horizonHits=0;
    void clear() {
        rows.clear(); horizonRows.clear(); computations=hits=0;
        horizonComputations=horizonHits=0;
    }
};
// Row pointers are local to one candidate: another row insertion may reallocate.
inline EvaluationCache::Row* outcome_cache_row(const InformationState& info,
                                              const JobForecast& job,EvaluationCache* cache) {
    if(!cache)return nullptr;
    for (auto& candidate:cache->rows) if(candidate.job==&job && candidate.horizon==info.horizon() &&
        candidate.winningGems==info.winningGems && candidate.ownScore==info.ownPermanentScore) {
        return &candidate;
    }
    constexpr unsigned cap=3*(single_job_cap+double_job_cap+nonharvest_job_cap);
    if(cache->rows.size()>=cap)return nullptr;
    cache->rows.push_back({&job,info.horizon(),info.winningGems,info.ownPermanentScore,{},{}});
    return &cache->rows.back();
}
inline double cached_outcome_value_row(const InformationState& info,const JobForecast& job,
                                       std::size_t index,EvaluationCache* cache,EvaluationCache::Row* row) {
    if(index>=job.outcomes.size())return -std::numeric_limits<double>::infinity();
    if(!cache || index>=outcome_cap)return outcome_value(info,job.outcomes[index]);
    if(!row) { ++cache->computations;return outcome_value(info,job.outcomes[index]); }
    if(row->known[index]) { ++cache->hits; return row->values[index]; }
    ++cache->computations;
    row->known[index]=true;
    return row->values[index]=outcome_value(info,job.outcomes[index]);
}
inline double cached_outcome_value(const InformationState& info,const JobForecast& job,
                                   std::size_t index,EvaluationCache* cache) {
    if(index>=job.outcomes.size())return -std::numeric_limits<double>::infinity();
    auto* row=cache && index<outcome_cap?outcome_cache_row(info,job,cache):nullptr;
    return cached_outcome_value_row(info,job,index,cache,row);
}
inline std::vector<double> cached_outcome_values(const InformationState& info,const JobForecast& job,
                                                EvaluationCache* cache) {
    std::vector<double> values; values.reserve(job.outcomes.size());
    auto* row=job.outcomes.empty()?nullptr:outcome_cache_row(info,job,cache);
    for(std::size_t i=0;i<job.outcomes.size();++i)values.push_back(cached_outcome_value_row(info,job,i,cache,row));
    return values;
}
inline bool productive(JobKind k) { return k == JobKind::cycle || k == JobKind::double_cycle; }
inline bool finite_equal(double a, double b) {
    return std::isfinite(a) && std::isfinite(b) &&
        std::abs(a-b) <= comparison_tolerance * std::max({1.0, std::abs(a), std::abs(b)});
}
inline bool strictly_better(double a, double b) {
    return std::isfinite(a) && (!std::isfinite(b) || (a > b && !finite_equal(a,b)));
}
inline double nominal_rate(const JobForecast& job) {
    const auto* n = job.nominal();
    return n ? double(n->deltaS) / std::max(1, n->tau + job.projectedRenewalAndReturnTime) : 0;
}
inline double immediate_rate(const JobForecast& job) {
    const auto* n = job.nominal();
    return n ? double(n->deltaS) / std::max(1, n->tau) : 0;
}
inline double tempo_value(const JobForecast& job, bool control) {
    double result = nominal_rate(job);
    const auto* n = job.nominal();
    if (control || !n || !job.personalDeadlineEvidence || !job.personalReleaseEvidence ||
        !job.releaseTime || !job.opponentDeadline ||
        !productive(job.id.kind) || (job.releaseTime && n->tau < *job.releaseTime) ||
        n->tau >= *job.opponentDeadline) return result;
    const double denied = std::clamp(job.supportedDenialGems, 0.0, double(job.campGems));
    return result + .5 * denied / (1 + std::max(0, *job.opponentDeadline - n->tau));
}

inline std::vector<double> normalized_weights(const JobForecast& job,
                                            const std::array<double,4>* belief = nullptr) {
    std::vector<double> result;
    double total = 0;
    for (const auto& o : job.outcomes) {
        if (static_cast<unsigned>(o.response)>=4) return {};
        double w = belief ? (*belief)[static_cast<unsigned>(o.response)] * o.conditionalLifecycleWeight
                          : o.assumedWeight;
        if (!std::isfinite(w) || w < 0) return {};
        result.push_back(w); total += w;
    }
    if (!(total > 0) || !std::isfinite(total)) return {};
    for (auto& w : result) w /= total;
    return result;
}
inline double weighted_mean(const std::vector<double>& values, const std::vector<double>& weights) {
    if (values.size() != weights.size() || values.empty()) return -std::numeric_limits<double>::infinity();
    double r = 0;
    for (std::size_t i=0;i<values.size();++i) {
        if (!std::isfinite(values[i]) || !std::isfinite(weights[i]) || weights[i] < 0)
            return -std::numeric_limits<double>::infinity();
        r += values[i]*weights[i];
    }
    return r;
}
inline double lower_tail_mean(const std::vector<double>& values,
                              const std::vector<double>& weights, double mass = .5) {
    if (values.size() != weights.size() || values.empty() || mass <= 0 || mass > 1)
        return -std::numeric_limits<double>::infinity();
    std::vector<std::pair<double,double>> ordered;
    double total = 0;
    for (std::size_t i=0;i<values.size();++i) {
        if (!std::isfinite(values[i]) || !std::isfinite(weights[i]) || weights[i] < 0)
            return -std::numeric_limits<double>::infinity();
        ordered.emplace_back(values[i],weights[i]); total += weights[i];
    }
    if (!(total > 0) || !std::isfinite(total)) return -std::numeric_limits<double>::infinity();
    std::sort(ordered.begin(),ordered.end());
    double remaining = mass, sum = 0;
    for (const auto& [v,w] : ordered) {
        const double take = std::min(remaining,w/total);
        sum += take*v; remaining -= take;
        if (remaining <= 1e-14) break;
    }
    return sum/mass;
}

enum class BeliefEvent { none, hostile_lock_during_tribute, two_sighting_pursuit,
                         shared_camp_progress };
struct BeliefEvidence {
    // The adapter sets these only from personal observations; priority is fixed.
    bool newlyVisibleHostileLock = false, observedTributeTransition = false;
    bool enemyMovedTowardPreviouslyFixedActorLocationTwice = false;
    bool enemyProgressTowardPreviouslyVisibleCamp = false;
};
inline BeliefEvent classify_event(const BeliefEvidence& e) {
    if (e.newlyVisibleHostileLock && e.observedTributeTransition)
        return BeliefEvent::hostile_lock_during_tribute;
    if (e.enemyMovedTowardPreviouslyFixedActorLocationTwice) return BeliefEvent::two_sighting_pursuit;
    if (e.enemyProgressTowardPreviouslyVisibleCamp) return BeliefEvent::shared_camp_progress;
    return BeliefEvent::none;
}
struct BeliefState {
    std::array<double,4> weights{.25,.25,.25,.25};
    int lastEvidenceBlock = -1, nextDecayRound = 16;
    int lastEvidenceRound = -1;
};
inline void belief_update(BeliefState& state, int round, BeliefEvent event, bool control = false) {
    if (round < 0) return;
    // Advance deadlines, never apply the same complete interval twice.
    while (round >= state.nextDecayRound) {
        if (!control) for (auto& w : state.weights) w = .5*w + .125;
        state.nextDecayRound += 16;
    }
    if (event != BeliefEvent::none && round/4 != state.lastEvidenceBlock) {
        state.lastEvidenceBlock = round/4;
        state.lastEvidenceRound = round;
        state.nextDecayRound = round + 16;
        if (!control) {
            const unsigned target = event == BeliefEvent::hostile_lock_during_tribute ? 3 :
                                    event == BeliefEvent::two_sighting_pursuit ? 2 : 1;
            state.weights[target] *= 3;
            double sum = 0; for (double w : state.weights) sum += w;
            for (auto& w : state.weights) w = .8*w/sum + .05;
        }
    }
    if (control) state.weights = {.25,.25,.25,.25};
}

// Allocation is computed from public draft capabilities, never teammate views.
struct AllocationEdge {
    int actorOrdinal = -1, groupId = -1, tau = 0, securedGems = 0;
    bool safe = false, complete = true;
};
struct Allocation {
    bool complete = false;
    std::vector<int> consideredGroups, ownerOrdinals, actorLoads;
};
struct MarketScan {
    std::size_t nextEdge=0, bestEdge=0, bestGroup=0;
    double bestValue=-1;
    bool hasBest=false;
};
enum class MarketWorkStatus { progress, assigned, complete, invalid };
inline Allocation market_allocation_start(std::vector<int> groups,int actors) {
    Allocation r;
    if (actors<=0) return r;
    std::sort(groups.begin(),groups.end());
    groups.erase(std::unique(groups.begin(),groups.end()),groups.end());
    if (groups.size()>64) groups.resize(64);
    if (!groups.empty() && groups.front()<0) return r;
    r.consideredGroups=groups; r.ownerOrdinals.assign(groups.size(),-1);
    r.actorLoads.assign(actors,0);
    return r;
}
inline MarketWorkStatus market_allocate_work(Allocation& state,
                                              const std::vector<AllocationEdge>& edges,
                                              MarketScan& scan,unsigned maxEdges=8) {
    if (state.complete) return MarketWorkStatus::complete;
    const auto invalid=[&]() { state=Allocation{}; scan=MarketScan{}; return MarketWorkStatus::invalid; };
    if (state.actorLoads.empty() || state.consideredGroups.size()>64 ||
        state.ownerOrdinals.size()!=state.consideredGroups.size() || scan.nextEdge>edges.size()) return invalid();
    unsigned work=0;
    while (scan.nextEdge<edges.size() && work++<maxEdges) {
        const auto current=scan.nextEdge++;
        const auto& e=edges[current];
        if (!e.complete) return invalid(); // No unfinished plan is published.
        if (!e.safe || e.tau<=0 || e.tau>128 || e.securedGems<=0 || e.actorOrdinal<0 ||
            std::size_t(e.actorOrdinal)>=state.actorLoads.size() ||
            state.actorLoads[e.actorOrdinal]+e.tau>128) continue;
        const auto it=std::lower_bound(state.consideredGroups.begin(),state.consideredGroups.end(),e.groupId);
        if (it==state.consideredGroups.end() || *it!=e.groupId) continue;
        const auto g=std::size_t(it-state.consideredGroups.begin());
        if (state.ownerOrdinals[g]>=0) continue;
        const double value=double(e.securedGems)/e.tau;
        if (!scan.hasBest || strictly_better(value,scan.bestValue) ||
            (finite_equal(value,scan.bestValue) && std::tie(e.groupId,e.actorOrdinal)<
                std::tie(edges[scan.bestEdge].groupId,edges[scan.bestEdge].actorOrdinal))) {
            scan.hasBest=true; scan.bestEdge=current; scan.bestGroup=g; scan.bestValue=value;
        }
    }
    if (scan.nextEdge<edges.size()) return MarketWorkStatus::progress;
    if (!scan.hasBest) { state.complete=true; return MarketWorkStatus::complete; }
    const auto& best=edges[scan.bestEdge];
    state.ownerOrdinals[scan.bestGroup]=best.actorOrdinal;
    state.actorLoads[best.actorOrdinal]+=best.tau;
    scan=MarketScan{};
    return MarketWorkStatus::assigned;
}
inline bool market_allocate_step(Allocation& state,const std::vector<AllocationEdge>& edges) {
    MarketScan scan;
    return market_allocate_work(state,edges,scan,static_cast<unsigned>(edges.size()))==MarketWorkStatus::assigned;
}
inline Allocation market_allocate(std::vector<int> groups, int actors,
                                  const std::vector<AllocationEdge>& edges,
                                  bool calculationComplete = true) {
    if (!calculationComplete || actors<=0) return {};
    auto r=market_allocation_start(std::move(groups),actors);
    MarketScan scan;
    for (;;) {
        const auto status=market_allocate_work(r,edges,scan);
        if (status==MarketWorkStatus::complete || status==MarketWorkStatus::invalid) return r;
    }
}

// A single frozen table and horizon supply both conditional and prior maxima.
// labels partition exactly the lifecycle information revealed by this step.
struct ObservationTable {
    std::vector<std::vector<double>> jobOutcomeValues;
    std::vector<double> weights;
    std::vector<int> labels;
    int horizon = 0;
    std::string origin;
};
inline std::optional<double> observation_net_value(const ObservationTable& t,
                                                  double rho, double additionalCost) {
    if (t.weights.empty() || t.weights.size()!=t.labels.size() || t.jobOutcomeValues.empty() ||
        rho < 0 || additionalCost < 0 || !std::isfinite(rho+additionalCost)) return std::nullopt;
    double total = 0;
    for (double w : t.weights) { if (!std::isfinite(w) || w<0) return std::nullopt; total+=w; }
    if (!(total>0) || !std::isfinite(total)) return std::nullopt;
    for (const auto& row : t.jobOutcomeValues) {
        if (row.size()!=t.weights.size()) return std::nullopt;
        for (double v : row) if (!std::isfinite(v)) return std::nullopt;
    }
    std::vector<int> labels=t.labels;
    std::sort(labels.begin(),labels.end()); labels.erase(std::unique(labels.begin(),labels.end()),labels.end());
    double prior=-std::numeric_limits<double>::infinity(), posterior=0;
    for (const auto& row : t.jobOutcomeValues) {
        double v=0; for (std::size_t i=0;i<row.size();++i) v += row[i]*t.weights[i]/total;
        prior=std::max(prior,v);
    }
    for (int label : labels) {
        double best=-std::numeric_limits<double>::infinity();
        for (const auto& row : t.jobOutcomeValues) {
            double v=0; for (std::size_t i=0;i<row.size();++i)
                if (t.labels[i]==label) v+=row[i]*t.weights[i]/total;
            best=std::max(best,v);
        }
        posterior+=best;
    }
    return posterior-prior-rho-additionalCost;
}

struct ScoreBlock {
    int startRound = 0, endRound = 0;
    int ownIncrement = 0, opponentIncrement = 0, personallyCompletedSubmissions = 0;
    bool personalCoverageComplete = false;
};
inline bool horizon_ready(const std::vector<ScoreBlock>& history,
                          int currentRound=std::numeric_limits<int>::max()) {
    if (history.size()<4) return false;
    if (history.back().endRound>currentRound) return false;
    for (std::size_t i=history.size()-4;i<history.size();++i) {
        const auto& b=history[i];
        if (b.endRound-b.startRound!=32 || b.ownIncrement<0 || b.opponentIncrement<0 ||
            (i>history.size()-4 && history[i-1].endRound!=b.startRound)) return false;
    }
    return true;
}
inline std::optional<HorizonUtility> horizon_utility(const InformationState& info,
                                                    const JobForecast& job,
                                                    const std::vector<ScoreBlock>& history) {
    const auto* o=job.nominal();
    if (!horizon_ready(history,info.round) || !o || o->tau<0) return std::nullopt;
    HorizonUtility result; result.winPayoff=0; result.duration=o->tau;
    const double limit=std::max(0,info.roundLimit-info.round);
    for (std::size_t k=history.size()-4;k<history.size();++k) {
        const auto& b=history[k];
        const double ownRate=b.ownIncrement/32.0, enemyRate=b.opponentIncrement/32.0;
        const bool measured=b.personalCoverageComplete && b.ownIncrement>0;
        const double share=measured ? std::clamp(double(b.personallyCompletedSubmissions)/b.ownIncrement,0.0,1.0)
                                    : 1.0/std::max(1,info.teamSize);
        result.assumedShare|=!measured;
        const double otherRate=ownRate*(1-share);
        std::vector<Conversion> events=o->conversions;
        if (events.empty() && o->deltaS>0) events.push_back({o->tau,o->deltaS});
        events.erase(std::remove_if(events.begin(),events.end(),[&](const Conversion& e){
            return e.completedAt<0 || e.completedAt>o->tau || e.gems<0;
        }),events.end());
        std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){ return a.completedAt<b.completedAt; });
        const auto own_at=[&](double t) {
            double v=info.ownPermanentScore+otherRate*std::min(t,double(o->tau))+
                     ownRate*std::max(0.0,t-o->tau);
            for (const auto& e:events) if (e.completedAt<=t) v+=e.gems;
            return v;
        };
        const double infinity=std::numeric_limits<double>::infinity();
        double ownWin=info.ownPermanentScore>=info.winningGems ? 0 : infinity;
        double enemyWin=info.opponentPermanentScore>=info.winningGems ? 0 :
                        enemyRate>0 ? (info.winningGems-info.opponentPermanentScore)/enemyRate : infinity;
        std::vector<double> times{0,limit,std::min(limit,double(o->tau))};
        for (const auto& e:events) if (e.completedAt<=limit) times.push_back(e.completedAt);
        std::sort(times.begin(),times.end()); times.erase(std::unique(times.begin(),times.end()),times.end());
        for (std::size_t i=0;i<times.size();++i) {
            const double t=times[i], value=own_at(t);
            if (value>=info.winningGems) ownWin=std::min(ownWin,t);
            if (i+1<times.size()) {
                const double rate=t<o->tau ? otherRate : ownRate;
                if (rate>0 && value<info.winningGems) {
                    const double cross=t+(info.winningGems-value)/rate;
                    if (cross<=times[i+1]) ownWin=std::min(ownWin,cross);
                }
            }
        }
        if (ownWin>limit) ownWin=infinity;
        if (enemyWin>limit) enemyWin=infinity;
        double payoff=-1;
        if (std::isfinite(ownWin) || std::isfinite(enemyWin)) {
            // Simultaneous predicted crossings are unresolved; lower payoff.
            if (ownWin<enemyWin && !finite_equal(ownWin,enemyWin)) payoff=1;
        } else {
            const double margin=own_at(limit)-(info.opponentPermanentScore+enemyRate*limit);
            payoff=margin>0 && !finite_equal(margin,0) ? 1 : -1;
        }
        result.winPayoff+=payoff/4;
        const double end=std::min(limit,std::min(ownWin,enemyWin));
        result.terminalMargin+=(own_at(end)-(info.opponentPermanentScore+enemyRate*end))/4;
    }
    return result;
}

inline std::optional<HorizonUtility> cached_horizon_utility(
    const InformationState& info, const JobForecast& job,
    const std::vector<ScoreBlock>& history, EvaluationCache* cache) {
    if (!cache) return horizon_utility(info,job,history);
    for (const auto& row:cache->horizonRows) if (row.job==&job) {
        ++cache->horizonHits; return row.value;
    }
    ++cache->horizonComputations;
    const auto value=horizon_utility(info,job,history);
    if (cache->horizonRows.size()<single_job_cap+double_job_cap+nonharvest_job_cap)
        cache->horizonRows.push_back({&job,value});
    return value;
}

enum class Controller { ledger, atlas, tempo, horizon, options, treasury,
                        market, council, belief, scout, robust, insurance };
struct ControllerContext {
    BeliefState belief;
    std::vector<ScoreBlock> scoreHistory;
    int ownerTurn=0, lastObservationTurn=-1000000, turnsWithoutOwnedJob=0;
    bool allowOneUnownedJob=false, b0BankRecommended=false;
    std::optional<JobId> activeJob;
    bool activeJobValid=true, protectedChannelOrCapture=false;
    bool acquisitionMilestone=false, sharedAbort=false;
    bool newlyFeasibleWinningBank=false, menuComplete=true;
};
enum class IntentStatus { selected, preserve, yield };
enum class DecisionReason { objective, warmup_ledger, protected_obligation,
                            options_commitment, incomplete_menu, no_admitted_job,
                            robust_degeneracy, mandatory_recovery, immediate_winning_conversion,
                            gold_fallback };
struct JobIntent {
    IntentStatus status=IntentStatus::yield;
    std::optional<JobId> job;
    double objective=0;
    DecisionReason reason=DecisionReason::no_admitted_job;
    bool assumedShare=false, candidateTruncated=false;
    // Only an actual commit may turn this private selector ticket into feedback.
    // Shared priority intents leave it zero.
    std::uint64_t policyToken=0;
};
struct CandidateMenu {
    std::vector<const JobForecast*> jobs;
    bool truncated=false;
};
inline CandidateMenu canonical_menu(const std::vector<JobForecast>& input) {
    CandidateMenu r;
    std::vector<const JobForecast*> sorted;
    for (const auto& j:input) sorted.push_back(&j);
    std::stable_sort(sorted.begin(),sorted.end(),[](auto a,auto b){ return a->id<b->id; });
    unsigned singles=0,doubles=0,other=0,banks=0,renewals=0,specific=0;
    std::optional<JobId> previous;
    for (const auto* j:sorted) {
        if (previous && *previous==j->id) continue;
        previous=j->id;
        unsigned* count=productive(j->id.kind) ? (j->id.kind==JobKind::cycle ? &singles:&doubles) : &other;
        const unsigned cap=j->id.kind==JobKind::cycle ? single_job_cap :
                           j->id.kind==JobKind::double_cycle ? double_job_cap:nonharvest_job_cap;
        if ((j->id.kind==JobKind::bank && banks>=2) ||
            (j->id.kind==JobKind::renew && renewals>=1) ||
            ((j->id.kind==JobKind::invest || j->id.kind==JobKind::observe) && specific>=1)) {
            r.truncated=true; continue;
        }
        if (*count>=cap) { r.truncated=true; continue; }
        if (j->id.kind==JobKind::bank) ++banks;
        if (j->id.kind==JobKind::renew) ++renewals;
        if (j->id.kind==JobKind::invest || j->id.kind==JobKind::observe) ++specific;
        ++*count; r.jobs.push_back(j);
    }
    return r;
}
inline bool admitted(const InformationState& info, const JobForecast& j, bool yBased) {
    const auto* n=j.nominal();
    if (!j.complete || !j.routeKnown || !n || n->tau<0 || j.outcomes.empty() ||
        j.outcomes.size()>outcome_cap || (productive(j.id.kind) && !j.fullClearAdmitted) ||
        (j.id.kind==JobKind::bank && info.carriedGems<=0)) return false;
    if (j.id.kind==JobKind::renew && !j.renewalRequired && !j.mandatorySafety) return false;
    const int admissionTau=j.admissionTau.value_or(n->tau);
    if (admissionTau<0) return false;
    if (j.mandatorySafety) return true;
    if (admissionTau>info.roundLimit-info.round) return false;
    return !yBased || admissionTau<=info.horizon();
}
inline std::vector<JobForecast> council_proposals(const InformationState& info,
                                                 const std::vector<JobForecast>& candidates) {
    // Four independent finite scans, no successor generation. Proposer identity
    // is discarded by the active critic; its input is only forecast features.
    std::vector<JobForecast> result;
    const auto menu=canonical_menu(candidates);
    std::array<const JobForecast*,4> proposals{};
    std::array<double,4> scores;
    scores.fill(-std::numeric_limits<double>::infinity());
    for (const auto* j:menu.jobs) {
        if (!admitted(info,*j,true)) continue;
        const auto nominate=[&](unsigned p,double value) {
            if (std::isfinite(value) && (!proposals[p] || strictly_better(value,scores[p]))) {
                proposals[p]=j; scores[p]=value;
            }
        };
        if (productive(j->id.kind)) nominate(0,nominal_rate(*j));
        if (j->id.kind==JobKind::invest && j->investment.inheritedNextBlock &&
            j->investment.positive_payback()) nominate(1,j->investment.checkpoint_utility());
        if (j->id.kind==JobKind::bank) nominate(2,immediate_rate(*j));
        if (productive(j->id.kind) && j->personalDeadlineEvidence && j->personalReleaseEvidence &&
            j->releaseTime && j->opponentDeadline &&
            j->supportedDenialGems>0 && (!j->releaseTime || j->nominal()->tau>=*j->releaseTime) &&
            j->nominal()->tau<*j->opponentDeadline) nominate(3,tempo_value(*j,false));
    }
    for (unsigned p=0;p<4;++p) if (proposals[p]) {
        auto found=std::find_if(result.begin(),result.end(),[&](const auto& j){return j.id==proposals[p]->id;});
        if (found==result.end()) {
            result.push_back(*proposals[p]); result.back().proposerMask=1u<<p;
        } else found->proposerMask|=1u<<p;
    }
    // Shared safety/no-gem fallback is outside the four-proposer critic.
    // Retain these unmarked records so a filtered Council menu cannot erase it.
    const JobForecast* renewal=nullptr;
    const JobForecast* gold=nullptr;
    double goldRate=-1;
    for (const auto* j:menu.jobs) if (admitted(info,*j,true)) {
        if (j->id.kind==JobKind::renew && !renewal) renewal=j;
        if (j->rewardOnlyEndpoint && j->campGold>0) {
            const double rate=double(j->campGold)/std::max(1,j->nominal()->tau+j->projectedRenewalAndReturnTime);
            if (!gold || strictly_better(rate,goldRate)) {gold=j;goldRate=rate;}
        }
    }
    for (const auto* fallback:{renewal,gold}) if (fallback &&
        std::none_of(result.begin(),result.end(),[&](const auto& j){return j.id==fallback->id;})) {
        result.push_back(*fallback); result.back().proposerMask=0;
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return result;
}
inline JobIntent select(Controller controller, bool control, const InformationState& info,
                        const std::vector<JobForecast>& input, const ControllerContext& ctx={},
                        EvaluationCache* cache=nullptr,
                        const std::vector<const JobForecast*>* evaluatedPrefix=nullptr) {
    EvaluationCache localCache;
    if(!cache)cache=&localCache;
    JobIntent r;
    if (ctx.activeJob && !ctx.sharedAbort && ctx.activeJobValid) {
        if (ctx.protectedChannelOrCapture) return {IntentStatus::preserve,ctx.activeJob,0,
                                                  DecisionReason::protected_obligation};
        if (controller==Controller::options && !control && !ctx.newlyFeasibleWinningBank)
            return {IntentStatus::preserve,ctx.activeJob,0,DecisionReason::options_commitment};
    }
    if (!ctx.menuComplete) {
        if (ctx.activeJob && ctx.activeJobValid && !ctx.sharedAbort)
            return {IntentStatus::preserve,ctx.activeJob,0,DecisionReason::incomplete_menu};
        r.reason=DecisionReason::incomplete_menu; return r;
    }
    bool warmup=controller==Controller::horizon && !horizon_ready(ctx.scoreHistory,info.round);
    if(warmup)control=false; // Both Horizon arms use the same active Ledger warm-up.
    const auto effective=warmup ? Controller::ledger:controller;
    const bool yBased=effective!=Controller::ledger && effective!=Controller::tempo &&
                      effective!=Controller::market && effective!=Controller::horizon;
    // An adapter may freeze the proposal set explicitly. If it has not done
    // that, construct the exact same four proposals here for both arms.
    const bool explicitProposals=std::any_of(input.begin(),input.end(),[](const auto& j){return j.proposerMask!=0;});
    const auto generatedProposals=effective==Controller::council && !explicitProposals ?
        council_proposals(info,input):std::vector<JobForecast>{};
    // Generated forecasts live only for this call. Their addresses cannot key
    // the caller's cache after return, even when the next stack allocation
    // happens to reuse an address for a different proposal.
    if(effective==Controller::council && !explicitProposals)cache=&localCache;
    auto menu=canonical_menu(effective==Controller::council && !explicitProposals ? generatedProposals:input);
    r.candidateTruncated=menu.truncated;
    if (ctx.newlyFeasibleWinningBank && info.carriedGems>=
        std::max(0,info.winningGems-info.ownPermanentScore)) {
        const JobForecast* winning=nullptr;
        for (const auto* j:menu.jobs) if (j->id.kind==JobKind::bank && admitted(info,*j,false) &&
            j->nominal()->deltaS>=info.winningGems-info.ownPermanentScore &&
            (!winning || j->nominal()->tau<winning->nominal()->tau)) winning=j;
        if (winning) return {IntentStatus::selected,winning->id,double(winning->nominal()->deltaS),
                             DecisionReason::immediate_winning_conversion,false,menu.truncated};
    }
    bool immediateGemJob=false,treasuryCheckpointChoice=false;
    const JobForecast* goldFallback=nullptr;
    double goldRate=-1;
    for (const auto* j:menu.jobs) if (admitted(info,*j,yBased)) {
        if (j->mandatorySafety) return {IntentStatus::selected,j->id,0,DecisionReason::mandatory_recovery};
        if (j->nominal()->deltaS>0) immediateGemJob=true;
        // Amendment003: a purchase endpoint may convert zero gems while its
        // completed post-purchase loops have positive checkpoint payback.
        // Let that eligible schedule reach Treasury's gem-unit comparison;
        // its utility is never compared to the resource-only gold rate.
        if(effective==Controller::treasury && j->id.kind==JobKind::invest &&
            j->investment.positive_payback() && (!control || j->investment.inheritedNextBlock) &&
            std::isfinite(j->investment.checkpoint_utility()))treasuryCheckpointChoice=true;
        if (info.carriedGems==0 && j->rewardOnlyEndpoint && productive(j->id.kind) &&
            j->campGold>0 && j->nominal()->deltaS==0) {
            const double rate=double(j->campGold)/std::max(1,j->nominal()->tau+j->projectedRenewalAndReturnTime);
            if (!goldFallback || strictly_better(rate,goldRate)) { goldFallback=j; goldRate=rate; }
        }
    }
    if (!immediateGemJob && goldFallback && !treasuryCheckpointChoice)
        return {IntentStatus::selected,goldFallback->id,goldRate,DecisionReason::gold_fallback,false,menu.truncated};
    // Cheap shared priorities inspect the full canonical menu. In particular,
    // an unscored gem job must still prevent a false gold-only fallback.
    // Only risk objectives are restricted to whole evaluated candidates.
    if(evaluatedPrefix && (effective==Controller::insurance || effective==Controller::robust || effective==Controller::belief))
        std::erase_if(menu.jobs,[&](const auto* job) {
            return std::find(evaluatedPrefix->begin(),evaluatedPrefix->end(),job)==evaluatedPrefix->end();
        });
    const JobForecast* best=nullptr;
    const JobForecast* renewal=nullptr;
    const JobForecast* safestBank=nullptr;
    double bestValue=-std::numeric_limits<double>::infinity();
    HorizonUtility bestRace;
    bool allProductiveMinimumZero=true;
    bool ownedEligible=false;
    for (const auto* j:menu.jobs)
        if (j->owned && productive(j->id.kind) && admitted(info,*j,false)) ownedEligible=true;
    const bool ownedFallback=!ownedEligible && ctx.turnsWithoutOwnedJob>=8 && ctx.allowOneUnownedJob;
    for (const auto* j:menu.jobs) {
        if (!admitted(info,*j,yBased)) continue;
        if (j->id.kind==JobKind::renew) renewal=j;
        if (j->mandatorySafety) return {IntentStatus::selected,j->id,0,DecisionReason::mandatory_recovery};
        if (j->id.kind==JobKind::bank && (!safestBank ||
            j->nominal()->healthLoss<safestBank->nominal()->healthLoss ||
            (finite_equal(j->nominal()->healthLoss,safestBank->nominal()->healthLoss) &&
             j->nominal()->tau<safestBank->nominal()->tau))) safestBank=j;
        if (effective!=Controller::treasury && j->id.kind==JobKind::invest &&
            !(effective==Controller::council && j->investment.inheritedNextBlock)) continue;
        if (effective!=Controller::scout && j->id.kind==JobKind::observe) continue;
        if (effective==Controller::market && !control && productive(j->id.kind) &&
            !j->owned && !ownedFallback) continue;
        if (effective==Controller::ledger && control && ctx.b0BankRecommended &&
            j->id.kind!=JobKind::bank && j->id.kind!=JobKind::renew) continue;
        if (effective==Controller::ledger && control && !ctx.b0BankRecommended &&
            j->id.kind==JobKind::bank) continue;
        double value=-std::numeric_limits<double>::infinity();
        std::optional<HorizonUtility> race;
        switch(effective) {
        case Controller::ledger:
            if (j->id.kind==JobKind::renew) break;
            value=control && productive(j->id.kind) ?
                (2.0*j->campGems+.4*j->campGold)/(j->travelPlusClear+10) : nominal_rate(*j);
            break;
        case Controller::atlas: value=control ? immediate_rate(*j):cached_outcome_value(info,*j,j->nominalIndex,cache); break;
        case Controller::tempo:
            if (j->id.kind!=JobKind::renew) value=tempo_value(*j,control);
            break;
        case Controller::horizon:
            race=cached_horizon_utility(info,*j,ctx.scoreHistory,cache);
            if (race) value=control ? race->terminalMargin:race->winPayoff;
            break;
        case Controller::options: value=cached_outcome_value(info,*j,j->nominalIndex,cache); break;
        case Controller::treasury:
            if (j->id.kind==JobKind::invest) {
                if (!j->investment.positive_payback() || (control && !j->investment.inheritedNextBlock)) break;
                value=j->investment.checkpoint_utility();
            } else {
                value=0;
                const std::array<int,3> checkpoints{info.horizon()/4,info.horizon()/2,info.horizon()};
                const std::array<double,3> checkpointWeights{.25,.25,.5};
                for (unsigned c=0;c<3;++c) {
                    InformationState checkpointInfo;
                    checkpointInfo.round=info.round;
                    checkpointInfo.roundLimit=info.round+checkpoints[c];
                    checkpointInfo.winningGems=info.winningGems;
                    checkpointInfo.ownPermanentScore=info.ownPermanentScore;
                    value+=checkpointWeights[c]*cached_outcome_value(checkpointInfo,*j,j->nominalIndex,cache);
                }
            }
            break;
        case Controller::market:
            if (j->id.kind!=JobKind::renew) value=nominal_rate(*j);
            break;
        case Controller::council:
            if (!j->proposerMask) break;
            if (!control) value=cached_outcome_value(info,*j,j->nominalIndex,cache);
            else { for (unsigned p=0;p<4;++p) if (j->proposerMask&(1u<<p)) { value=4-p; break; } }
            break;
        case Controller::belief:
            value=weighted_mean(cached_outcome_values(info,*j,cache),
                normalized_weights(*j,control ? nullptr:&ctx.belief.weights));
            break;
        case Controller::scout:
            if (j->id.kind==JobKind::observe) {
                if (control || !j->observationNetValue || *j->observationNetValue<=0 ||
                    !j->observationRevealsTarget || !j->observationSafe ||
                    ctx.ownerTurn-ctx.lastObservationTurn<16 || ctx.activeJob) break;
                // EVSI is an incremental gain over the frozen best economic
                // alternative. Compare by adding it to that same prior value.
                double prior=-std::numeric_limits<double>::infinity();
                for (const auto* economic:menu.jobs) if (economic->id.kind!=JobKind::observe &&
                    economic->id.kind!=JobKind::invest && admitted(info,*economic,true)) {
                    prior=std::max(prior,weighted_mean(cached_outcome_values(info,*economic,cache),
                        normalized_weights(*economic,&ctx.belief.weights)));
                }
                value=prior+*j->observationNetValue;
            } else value=weighted_mean(cached_outcome_values(info,*j,cache),normalized_weights(*j,&ctx.belief.weights));
            break;
        case Controller::robust:
            if(control)value=cached_outcome_value(info,*j,j->nominalIndex,cache);
            else {
                const auto values=cached_outcome_values(info,*j,cache);
                const double minimum=*std::min_element(values.begin(),values.end());
                if (productive(j->id.kind) && minimum>0) allProductiveMinimumZero=false;
                value=minimum;
            }
            break;
        case Controller::insurance: {
            const auto values=cached_outcome_values(info,*j,cache);
            const auto weights=normalized_weights(*j);
            value=control ? weighted_mean(values,weights):lower_tail_mean(values,weights);
            break;
        }
        }
        bool better=!best || strictly_better(value,bestValue);
        if (best && finite_equal(value,bestValue) && effective==Controller::horizon && race) {
            if (!control && !finite_equal(race->terminalMargin,bestRace.terminalMargin))
                better=race->terminalMargin>bestRace.terminalMargin;
            else if (!finite_equal(race->duration,bestRace.duration)) better=race->duration<bestRace.duration;
        }
        if (std::isfinite(value) && better) {
            best=j; bestValue=value;
            if (race) bestRace=*race;
        }
    }
    if (effective==Controller::robust && !control && allProductiveMinimumZero) {
        best=safestBank ? safestBank:renewal;
        bestValue=0; r.reason=DecisionReason::robust_degeneracy;
    }
    if (!best) { best=renewal; bestValue=0; }
    if (best) {
        r.status=IntentStatus::selected; r.job=best->id; r.objective=bestValue;
        if (r.reason!=DecisionReason::robust_degeneracy)
            r.reason=warmup ? DecisionReason::warmup_ledger:DecisionReason::objective;
        r.assumedShare=bestRace.assumedShare;
    }
    return r;
}

} // namespace plan3
