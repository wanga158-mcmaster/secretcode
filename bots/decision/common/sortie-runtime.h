#pragma once
#include "two-trip-runtime.h"
#include "sortie-model.h"
#include <map>
#include <limits>

namespace plan3_runtime::sortie {
namespace model=plan3_sortie;
namespace observation=plan3_two_trip::observation;
using Status=two_trip::Status;
using Inventory=two_trip::Inventory;
inline constexpr unsigned maximum_leg=128,maximum_escape=8;
enum class Phase { approach,combat,escape,recall,healing,returning,capture,bank,conversion_wait };
struct Member {
    int key=-1,range=0;
    std::uint32_t native=0;
    cambc::vec2 spawn{};
    double fullHealth=0,incoming=0,lastHealth=0;
};
struct Plan {
    Phase phase=Phase::approach;
    int groupKey=-1,altarIndex=-1,deadline=0,attackRange=0;
    std::uint32_t nativeGroup=0;
    unsigned memberCount=0,preparedRound=0,lastRound=0,lastProgress=0,lastActionRound=UINT_MAX;
    unsigned sortie=0,attacksMade=0,recallRound=0,lastHoldRound=0,combatAt=0,remainingModelEvaluations=0;
    int pendingAttack=-1;
    bool started=false,combatBound=false,damageMade=false;
    cambc::vec2 decisionOrigin{},origin{},escapePoint{},captureTile{},altarTile{},altarApproach{},expectedPosition{};
    std::shared_ptr<two_trip::RouteField> route;
    std::array<Member,model::maximum_members> members{};
    std::array<unsigned,model::maximum_members> usedAttacks{};
    std::array<cambc::vec2,maximum_escape+1> escapePath{};
    std::array<cambc::vec2,maximum_leg+1> capturePath{},bankPath{};
    unsigned escapeCount=0,escapeAt=1,captureCount=0,captureAt=1,bankCount=0,bankAt=1;
    Inventory inventory{};
    EquipmentStats stats;
    model::Input input;
    model::Schedule accounting;
};
struct Candidate {
    Plan plan;
    model::Input input;
    model::Schedule accounting;
    // Complete PUBLIC full-health upper bound under the declared quiet model.
    // These quantities do not assert personally current hidden camp state.
    bool publicMemberBound=true,assumedQuiet=true;
};
struct ModelTrace {
    model::State state=model::State::unknown;
    model::Reason reason=model::Reason::evidence;
    std::uint64_t before=0,after=0;
    model::EvaluationWork work{};
    // Work is cumulative across chunks; owner-turn counts identify only the
    // private hypothetical computation, never physical champion actions.
    bool resumed=false,complete=false;
    unsigned chunkOwnerTurns=0,completedOwnerTurns=0;
};
struct PrepareResult {
    Status status=Status::unknown;
    const char* reason="unknown";
    std::unique_ptr<Candidate> candidate;
    std::optional<ModelTrace> model=std::nullopt;
};
using StepResult=two_trip::StepResult;
inline int distance(cambc::vec2 a,cambc::vec2 b){return two_trip::distance(a,b);}
inline bool close(double a,double b){return observation::close(a,b);}
// Attempt-local queried ability facts. No cooldown/allowance survives a
// callback or action through a Builder cache. Stun is deliberately uncredited.
template<class Ready> std::optional<model::TaviInput> tavi_input(const cambc::champion& me,
        const cambc::detail::champion_info& actor,const EquipmentStats& stats,Ready&& ready) {
    if(!ready())return std::nullopt;
    const auto abilities=me.abilities();
    if(!abilities[0].is_attack_reset() || !abilities[1].is_cc())return std::nullopt;
    const auto reset=abilities[0].get<cambc::ability_attack_reset>();
    const auto burst=abilities[1].get<cambc::ability_cc>();
    if(reset.enhanced_attack || reset.exclusive_with_movement ||
       !reset.valid_targets.contains(cambc::ability_target_mask::self) ||
       !burst.valid_targets.contains(cambc::ability_target_mask::camps) || !burst.exclusive_with_movement ||
       reset.max_cooldown<1 || burst.max_cooldown<1 || burst.duration<0 ||
       !model::scalar(burst.range) || burst.range<actor.attack_range || std::floor(burst.range)!=burst.range ||
       !model::scalar(burst.modifier) || burst.modifier<=0)return std::nullopt;
    if(!ready())return std::nullopt;
    model::TaviInput result;result.enabled=result.known=true;
    result.focus=stats.focus;result.burstModifier=burst.modifier;result.burstCampDamage=stats.campDamage;
    result.burstRange=int(burst.range);result.stunRounds=burst.duration;
    result.resetMaxCooldown=reset.max_cooldown;result.burstMaxCooldown=burst.max_cooldown;
    result.resetCooldown=abilities[0].cooldown();result.burstCooldown=abilities[1].cooldown();
    const auto state=cambc::champion_state::from_bits_retain(actor.state);
    result.initialCanAttack=state.contains(cambc::champion_state::can_attack);
    result.initialCanCast=state.contains(cambc::champion_state::can_cast);
    // An exclusive spell is not promised after walking, even if another
    // allowance happens to have been restored by an external action.
    result.initialBurstAllowed=state.contains(cambc::champion_state::can_move);
    if(!ready() || result.resetCooldown<0 || result.resetCooldown>result.resetMaxCooldown ||
       result.burstCooldown<0 || result.burstCooldown>result.burstMaxCooldown)return std::nullopt;
    return result;
}
// Immutable kit metadata was queried when private evaluation started. Only
// cooldowns/allowances are mutable; reading them needs no new design query.
template<class Ready> std::optional<model::TaviInput> live_tavi_input(const cambc::champion& me,
        const cambc::detail::champion_info& actor,const EquipmentStats& stats,
        const model::TaviInput& kit,Ready&& ready) {
    if(!ready() || !kit.enabled || !kit.known)return std::nullopt;
    auto result=kit;result.focus=stats.focus;result.burstCampDamage=stats.campDamage;
    const auto native=static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(me));
    cambc::detail::check(cambc::detail::sdk_champion_cooldown(native,0,&result.resetCooldown));
    cambc::detail::check(cambc::detail::sdk_champion_cooldown(native,1,&result.burstCooldown));
    const auto state=cambc::champion_state::from_bits_retain(actor.state);
    result.initialCanAttack=state.contains(cambc::champion_state::can_attack);
    result.initialCanCast=state.contains(cambc::champion_state::can_cast);
    result.initialBurstAllowed=state.contains(cambc::champion_state::can_move);
    if(!ready() || result.resetCooldown<0 || result.resetCooldown>result.resetMaxCooldown ||
       result.burstCooldown<0 || result.burstCooldown>result.burstMaxCooldown)return std::nullopt;
    return result;
}
inline bool target(const Plan& plan,const cambc::camp& camp) {
    const auto id=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camp));
    for(unsigned i=0;i<plan.memberCount;++i)if(plan.members[i].native==id)return true;
    return false;
}
inline bool exposed(const Plan& plan,cambc::vec2 p) {
    for(unsigned i=0;i<plan.memberCount;++i)
        if(plan.members[i].incoming>0 && distance(p,plan.members[i].spawn)<=plan.members[i].range)return true;
    return false;
}
template<class Ready> std::optional<unsigned> path_exposure(const Plan& plan,cambc::vec2 from,Ready&& ready) {
    unsigned exposure=0;
    for(unsigned n=0;from!=plan.origin;++n) {
        if(n>=maximum_leg || !ready())return std::nullopt;
        exposure+=exposed(plan,from);
        const auto next=plan.route->toward_origin(from);if(!next || *next==from)return std::nullopt;from=*next;
    }
    return exposure;
}
// Sparse COMPLETE suffix costs on one immutable fixed-Q route. Ordinary actor
// movement changes only the requested origin; it does not discard useful path
// work. No actor, observed member HP, inventory or forecast is retained here.
class PathExposure {
    struct PublicRange {cambc::vec2 spawn{};int range=0;bool damaging=false;};
    struct Departure {cambc::vec2 point{};bool exposed=false;};
    std::shared_ptr<two_trip::RouteField> route_;
    cambc::vec2 origin_{},requested_{},cursor_{};
    int width_=0,height_=0;
    unsigned memberCount_=0,depth_=0,suffix_=0;
    bool valid_=false,requestedValid_=false,active_=false,unwinding_=false,failed_=false;
    std::array<PublicRange,model::maximum_members> members_{};
    std::array<Departure,maximum_leg> path_{};
    std::map<int,unsigned> costs_;
    bool inside(cambc::vec2 p) const {return p.x>=0 && p.y>=0 && p.x<width_ && p.y<height_;}
    int key(cambc::vec2 p) const {return p.y*width_+p.x;}
    bool exposed_at(cambc::vec2 p) const {
        for(unsigned i=0;i<memberCount_;++i)
            if(members_[i].damaging && distance(p,members_[i].spawn)<=members_[i].range)return true;
        return false;
    }
public:
    static constexpr unsigned units_per_advance=8;
    PathExposure(const Plan& plan,int width,int height):route_(plan.route),origin_(plan.origin),width_(width),height_(height) {
        if(!route_ || !route_->ready() || width<1 || height<1 ||
           std::int64_t(width)*height>two_trip::maximum_tiles || !inside(origin_) ||
           route_->distance(origin_)!=0 || plan.memberCount>model::maximum_members)return;
        memberCount_=plan.memberCount;
        for(unsigned i=0;i<memberCount_;++i)
            members_[i]={plan.members[i].spawn,plan.members[i].range,plan.members[i].incoming>0};
        costs_.emplace(key(origin_),0);valid_=true;
    }
    bool request(cambc::vec2 from) {
        if(!valid_ || failed_ || !inside(from))return false;
        const int d=route_->distance(from);
        if(d<0 || d>int(maximum_leg))return false;
        requested_=from;requestedValid_=true;return true;
    }
    std::optional<unsigned> cost(cambc::vec2 from) const {
        if(!valid_ || failed_ || !inside(from))return std::nullopt;
        const int d=route_->distance(from);
        if(d<0 || d>int(maximum_leg))return std::nullopt;
        const auto found=costs_.find(key(from));
        if(found==costs_.end())return std::nullopt;
        return found->second;
    }
    bool failed() const {return !valid_ || failed_;}
    bool pending() const {return requestedValid_ && !failed() && !cost(requested_).has_value();}
    std::size_t cached_nodes() const {return costs_.size();}
    template<class Ready> void advance(Ready&& ready) {
        if(!valid_ || failed_)return;
        // Eight TOTAL traversal/reconstruction units per invocation. The
        // foreground and background can each invoke this in one callback.
        for(unsigned unit=0;unit<units_per_advance;++unit) {
            if(!ready())return;
            if(!active_) {
                if(!pending())return;
                cursor_=requested_;depth_=0;unwinding_=false;active_=true;
            }
            if(unwinding_) {
                const auto& departure=path_[depth_-1];
                const unsigned value=suffix_+unsigned(departure.exposed);
                // A route tile is inserted at most once; growth is bounded by
                // the map domain, without any map-sized eager initialization.
                costs_.emplace(key(departure.point),value);
                suffix_=value;--depth_;if(!depth_)active_=false;
                continue;
            }
            const auto found=costs_.find(key(cursor_));
            if(found!=costs_.end()) {
                suffix_=found->second;unwinding_=true;
                if(!depth_)active_=false;
                continue;
            }
            const int d=route_->distance(cursor_);
            const auto next=route_->toward_origin(cursor_);
            if(depth_==maximum_leg || d<1 || d>int(maximum_leg) || !next ||
               !inside(*next) || *next==cursor_ || route_->distance(*next)!=d-1) {
                failed_=true;active_=false;return;
            }
            const bool exposure=exposed_at(cursor_);
            if(!ready())return; // Retain only a fully checked departure unit.
            path_[depth_++]={cursor_,exposure};cursor_=*next;
        }
    }
};
// Cumulative diagnostics only. They survive private preparation invalidation;
// no counter participates in admission, scheduling or physical execution.
enum class PreparationStage : std::uint32_t { none,gate,public_walls,public_members,stationary_geometry,
    route_field,outward_exposure,escape_geometry,return_exposure,capture_altar_geometry,model,complete };
enum class ResetReason : std::uint32_t { none,origin,inventory,semantic,geometry,contest,deadline };
enum class MetricIndex : unsigned { stageHighWater,lastStage,outwardBindings,modelStarts,modelTerminals,
    modelSchedules,modelResets,lostOwnerTurns,resetMask,lastReset,lastModelState,lastModelReason,maxOwnerTurns,
    candidateSnapshots,forecasts,episodesStarted,episodesConverted,episodesAborted,lastGroup,count };
struct PreparationMetrics {
    PreparationStage stageHighWater=PreparationStage::none,lastStage=PreparationStage::none;
    std::uint32_t outwardBindings=0,modelStarts=0,modelTerminals=0,modelSchedules=0,modelResets=0,lostOwnerTurns=0;
    std::uint32_t resetMask=0,maxOwnerTurns=0,candidateSnapshots=0,forecasts=0,episodesStarted=0,episodesConverted=0,episodesAborted=0;
    ResetReason lastReset=ResetReason::none;
    int lastModelState=-1,lastModelReason=-1,lastGroup=-1;
    static void increment(std::uint32_t& value,std::uint32_t amount=1) {
        const auto maximum=std::numeric_limits<std::uint32_t>::max();
        value=amount>maximum-value?maximum:value+amount;
    }
    void enter(PreparationStage stage) {
        lastStage=stage;if(stage>stageHighWater)stageHighWater=stage;
    }
    void reset(ResetReason reason,unsigned ownerTurns) {
        increment(modelResets);increment(lostOwnerTurns,ownerTurns);
        lastReset=reason;resetMask|=1u<<static_cast<unsigned>(reason);
    }
    // sx:1 ordering; signed storage represents the unknown -1 sentinels while
    // preserving every saturated uint32 value exactly and without formatting.
    std::array<std::int64_t,static_cast<unsigned>(MetricIndex::count)> snapshot() const {
        return {std::int64_t(stageHighWater),std::int64_t(lastStage),outwardBindings,modelStarts,modelTerminals,
            modelSchedules,modelResets,lostOwnerTurns,resetMask,std::int64_t(lastReset),lastModelState,lastModelReason,
            maxOwnerTurns,candidateSnapshots,forecasts,episodesStarted,episodesConverted,episodesAborted,lastGroup};
    }
};
class Builder {
    PreparationMetrics metrics_;
    struct StaticGeometry {
        cambc::vec2 escapePoint{},captureTile{},altarTile{},altarApproach{};
        int altarIndex=-1,escapeTurns=0,escapeExposure=0,returnTurns=0,returnExposure=0,captureTurns=0,bankTurns=0;
        std::array<cambc::vec2,maximum_escape+1> escapePath{};
        std::array<cambc::vec2,maximum_leg+1> capturePath{},bankPath{};
        unsigned escapeCount=0,captureCount=0,bankCount=0;
    };
    struct OutwardGeometry {cambc::vec2 origin{};int turns=0;unsigned exposure=0;};
    std::unique_ptr<StaticGeometry> geometry_;
    std::optional<OutwardGeometry> outward_;
    std::unique_ptr<PathExposure> exposure_;
    std::unique_ptr<model::TaviEvaluation> evaluation_;
    bool evaluationTerminalCounted_=false;
    const StaticGeometry* evaluationGeometry_=nullptr;
    const two_trip::RouteField* evaluationRoute_=nullptr;
    cambc::vec2 evaluationOrigin_{},evaluationQ_{};
    int evaluationWidth_=0,evaluationHeight_=0,evaluationGroupKey_=-1;
    std::uint32_t evaluationGroup_=0;
    Inventory evaluationInventory_{};
    std::vector<cambc::vec2> evaluationOwnedSpawns_;
    model::Input evaluationInput_{};
    EquipmentStats evaluationStats_{};
    int evaluationAttackRange_=0,evaluationGold_=0;
    std::array<std::uint32_t,model::maximum_members> evaluationMembers_{};
    std::array<cambc::vec2,model::maximum_members> evaluationSpawns_{};
    std::array<int,model::maximum_members> evaluationKinds_{};
    std::array<Member,model::maximum_members> evaluationPrototypes_{};
    std::optional<cambc::bitboard> walls_;
    const PersonalInformation* wallsOwner_=nullptr;
    const std::uint64_t* wallsTerrain_=nullptr;
    int wallsWidth_=0,wallsHeight_=0;
    std::uint64_t wallsGeneration_=0;
    std::size_t wallCursor_=0,groups_=0;
    // Immutable PUBLIC design prototypes, never personally observed HP. Commit
    // one fully validated member at a time so a fuel boundary retains progress.
    // Q/actor-position resets do not invalidate this physical-group cache.
    int publicWidth_=0,publicHeight_=0,publicKey_=-1;
    std::uint32_t publicGroup_=0;
    unsigned publicCount_=0,publicCursor_=0;
    bool publicStarted_=false;
    std::array<Member,model::maximum_members> publicMembers_{};
    std::array<std::uint32_t,model::maximum_members> publicIds_{};
    std::array<cambc::vec2,model::maximum_members> publicSpawns_{};
    std::array<int,model::maximum_members> publicKinds_{};
    int groupKey_=-1,range_=-1;
    cambc::vec2 origin_{};
    std::shared_ptr<two_trip::RouteField> route_;
    // Hypothetical vision is an expensive STATIC geometry query. A completed
    // Q unit survives a callback boundary; no actor HP/forecast is retained.
    unsigned qCursor_=0,qMemberCount_=0;
    static constexpr unsigned qCapacity=81,qUnitsPerCall=8;
    bool qStarted_=false,qFound_=false,qComplete_=false;
    unsigned qFeasibleCount_=0;
    std::array<cambc::vec2,qCapacity> qFeasible_{};
    std::optional<cambc::bitboard> qVisionScratch_;
    std::uint64_t qWallsGeneration_=0;
    int qBestDistance_=0,qBestKey_=0,qWidth_=0,qHeight_=0,qChampion_=-1;
    std::uint32_t qNativeGroup_=0;
    cambc::vec2 qDecisionOrigin_{};
    std::array<int,model::maximum_members> qMemberKeys_{};
    std::array<std::uint32_t,model::maximum_members> qMemberIds_{};
    std::array<cambc::vec2,model::maximum_members> qMemberSpawns_{};
    unsigned escapeCursor_=0;
    bool escapeFound_=false;
    int escapeDistance_=0,escapeKey_=0,returnDistance_=0,returnExposure_=0;
    cambc::vec2 escapePoint_{};
    std::size_t spawnCursor_=0;
    void invalidate_evaluation(ResetReason reason=ResetReason::geometry) {
        if(evaluation_)metrics_.reset(reason,evaluation_->owner_turns());
        evaluation_.reset();evaluationTerminalCounted_=false;evaluationGeometry_=nullptr;evaluationRoute_=nullptr;
    }
    void account_evaluation() {
        metrics_.maxOwnerTurns=std::max(metrics_.maxOwnerTurns,std::uint32_t(evaluation_->owner_turns()));
        const auto* result=evaluation_->result();if(!result)return;
        metrics_.lastModelState=int(result->state);metrics_.lastModelReason=int(result->reason);
        if(!evaluationTerminalCounted_) {
            PreparationMetrics::increment(metrics_.modelTerminals);
            if(result->schedule)PreparationMetrics::increment(metrics_.modelSchedules);
            evaluationTerminalCounted_=true;
        }
    }
    void reset_route_preparation() {
        invalidate_evaluation();route_.reset();geometry_.reset();outward_.reset();exposure_.reset();
        escapeCursor_=0;escapeFound_=false;spawnCursor_=0;
        escapeDistance_=escapeKey_=returnDistance_=returnExposure_=0;
    }
    template<class Ready> void advance_q_feasibility(Ready&& ready) {
        if(!qStarted_ || qComplete_ || !walls_ || qWallsGeneration_!=wallsGeneration_)return;
        const unsigned scanWidth=unsigned(2*range_+1),slots=scanWidth*scanWidth;
        // Public Q visibility uses only the immutable champion DESIGN and map:
        // SDK visible_tiles_from_into -> board_vision_mask(type,x,y,out).
        // It does not include a live handle, origin, equipment, buffs or wards.
        for(unsigned work=0;qCursor_<slots && work<qUnitsPerCall;++work) {
            if(!ready())return;
            const int dy=int(qCursor_/scanWidth)-range_,dx=int(qCursor_%scanWidth)-range_;
            const cambc::vec2 q{qMemberSpawns_[0].x+dx,qMemberSpawns_[0].y+dy};
            bool fits=q.x>=0 && q.y>=0 && q.x<qWidth_ && q.y<qHeight_;
            if(fits)fits=!walls_->is_set(q);
            for(unsigned i=0;fits && i<qMemberCount_;++i)fits=distance(q,qMemberSpawns_[i])<=range_;
            if(fits) {
                if(!qVisionScratch_)qVisionScratch_.emplace(cambc::uninitialized);
                // Scratch is fully overwritten before its bits are read. It
                // is storage reuse, never retained personal/live visibility.
                cambc::detail::check(cambc::detail::sdk_hypothetical_vision(qChampion_,q.x,q.y,qVisionScratch_->ptr()));
                for(unsigned i=0;fits && i<qMemberCount_;++i)fits=qVisionScratch_->is_set(qMemberSpawns_[i]);
            }
            if(!ready())return; // Commit only a completely checked public Q.
            if(fits)qFeasible_[qFeasibleCount_++]=q;
            ++qCursor_;
        }
        if(qCursor_==slots){qComplete_=true;qFound_=qFeasibleCount_!=0;}
    }
    template<class Ready> PrepareResult publish_complete(const PersonalInformation& personal,std::size_t index,
            const EquipmentStats& stats,Ready&& ready) {
        metrics_.enter(PreparationStage::complete);
        const auto pending=[](const char* why){return PrepareResult{Status::pending,why,nullptr};};
        const auto stale=[&](ResetReason reason,const char* why){invalidate_evaluation(reason);return pending(why);};
        if(!ready())return pending("completed publication fuel");
        const unsigned round=cambc::round_number();
        const auto& me=cambc::self();const int width=cambc::map::width(),height=cambc::map::height();
        if(wallsOwner_!=&personal || wallsTerrain_!=personal.terrain.ptr() ||
           wallsWidth_!=width || wallsHeight_!=height || groups_!=personal.groups.size() ||
           qWallsGeneration_!=wallsGeneration_)return stale(ResetReason::geometry,"completed static map key");
        if(!personal.initialization_complete || index>=personal.groups.size() || round>=cambc::constants::max_rounds ||
           width!=evaluationWidth_ || height!=evaluationHeight_ || !me.is_alive() ||
           me.champion_name()!=cambc::champion_type::tavi || me.gem_count())
            return stale(round>=cambc::constants::max_rounds?ResetReason::deadline:
                (!personal.initialization_complete || index>=personal.groups.size() || width!=evaluationWidth_ || height!=evaluationHeight_)?
                ResetReason::geometry:ResetReason::semantic,"completed actor/map key");
        const auto nativeSelf=static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(me));
        cambc::detail::champion_info initial{};
        cambc::detail::check(cambc::detail::sdk_champion(nativeSelf,&initial));
        const auto channelMask=cambc::champion_state::recalling|cambc::champion_state::submitting_gems|cambc::champion_state::teleporting;
        if((cambc::champion_state::from_bits_retain(initial.state)&channelMask).bits() || me.buffs().has_stun() ||
           cambc::vec2{initial.x,initial.y}!=evaluationOrigin_ || initial.health!=evaluationInput_.health ||
           initial.attack_range!=evaluationAttackRange_ || initial.max_health!=stats.health ||
           initial.strength!=stats.strength || initial.focus!=stats.focus ||
           stats.health!=evaluationStats_.health || stats.strength!=evaluationStats_.strength || stats.focus!=evaluationStats_.focus ||
           stats.campDamage!=evaluationStats_.campDamage || stats.campMitigation!=evaluationStats_.campMitigation ||
           stats.recall!=evaluationStats_.recall || stats.captureGoldMultiplier!=evaluationStats_.captureGoldMultiplier)
            return stale(cambc::vec2{initial.x,initial.y}!=evaluationOrigin_?ResetReason::origin:ResetReason::semantic,"completed actor/stats key");
        const auto inventory=two_trip::inventory(ready);if(!inventory)return pending("completed inventory fuel");
        const auto abilities=live_tavi_input(me,initial,stats,evaluationInput_.tavi,ready);
        if(!ready())return pending("completed ability fuel");
        if(!abilities || *inventory!=evaluationInventory_)
            return stale(*inventory!=evaluationInventory_?ResetReason::inventory:ResetReason::semantic,"completed inventory/ability key");
        const auto& group=personal.groups[index];const auto& camps=group.group.camps();
        metrics_.lastGroup=group.key;
        const auto nativeGroup=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(group.group));
        const unsigned count=evaluationInput_.memberCount;
        if(nativeGroup!=evaluationGroup_ || group.key!=evaluationGroupKey_ || group.gem_value!=evaluationInput_.gems ||
           group.gold_value!=evaluationGold_ || camps.size()!=count || group.members.size()!=count || group.spawn_positions.size()!=count ||
           personal.spawn_positions!=evaluationOwnedSpawns_ || groups_!=personal.groups.size() ||
           !geometry_ || !route_ || !route_->ready() || geometry_.get()!=evaluationGeometry_ || route_.get()!=evaluationRoute_ ||
           origin_!=evaluationQ_ || geometry_->altarIndex<0 || unsigned(geometry_->altarIndex)>=personal.altars.size() ||
           personal.altars[unsigned(geometry_->altarIndex)]!=geometry_->altarTile)return stale(ResetReason::geometry,"completed geometry/group key");
        std::uint32_t actualCount{};
        cambc::detail::check(cambc::detail::sdk_map_camp_group_num_camps(nativeGroup,&actualCount));
        if(actualCount!=count)return stale(ResetReason::geometry,"completed membership count");
        std::array<bool,model::maximum_members> seen{};
        for(const auto& camp:camps) {
            if(!ready())return pending("completed member fuel");
            const auto id=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camp));unsigned slot=count;
            for(unsigned i=0;i<count;++i)if(evaluationMembers_[i]==id)slot=i;
            if(slot==count || seen[slot])return stale(ResetReason::geometry,"completed member identity");
            seen[slot]=true;
            cambc::detail::camp_info info{};std::uint32_t owner{};
            cambc::detail::check(cambc::detail::sdk_map_camp_info(id,&info));
            cambc::detail::check(cambc::detail::sdk_map_camp_group_of(id,&owner));
            const auto& member=evaluationPrototypes_[slot];
            if(owner!=nativeGroup || cambc::vec2{info.x,info.y}!=evaluationSpawns_[slot] || info.type!=evaluationKinds_[slot] ||
               !group.spawns.is_set(member.spawn))return stale(ResetReason::geometry,"completed member/spawn key");
            unsigned matches=0;for(const auto& design:group.members)
                if(design.spawn==member.spawn && design.health==member.fullHealth && design.strength==member.incoming)++matches;
            if(matches!=1)return stale(ResetReason::geometry,"completed public design key");
        }
        if(!cambc::vision::champions(cambc::team::enemy).empty())return stale(ResetReason::contest,"completed enemy");
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))
            if(lock.position()==geometry_->altarTile)return stale(ResetReason::contest,"completed altar lock");
        auto input=evaluationInput_;input.round=int(round);
        input.horizon=int(std::min<unsigned>(model::maximum_turns,cambc::constants::max_rounds-round));
        input.health=initial.health;input.maximumHealth=stats.health;input.strength=stats.strength;
        input.campDamage=stats.campDamage;input.mitigation=stats.campMitigation;input.tavi=*abilities;
        input.recallTicks=stats.recall;input.captureTicks=cambc::constants::capture_time;input.spawnHealing=cambc::constants::spawn_healing;
        const double tribute=std::ceil(double(group.gem_value)*cambc::constants::submit_gem_duration_multiplier+cambc::constants::submit_gem_base_duration);
        if(!model::scalar(tribute) || tribute>model::maximum_turns)return stale(ResetReason::semantic,"completed tribute key");
        input.tributeTicks=std::max(cambc::constants::submit_gem_minimum_duration,int(tribute));
        if(!evaluation_->matches(input))return stale(ResetReason::semantic,"completed semantic key");
        const auto* result=evaluation_->result();
        if(!result || !result->schedule || result->schedule->tau>input.horizon)return stale(ResetReason::deadline,"completed latest deadline");
        const auto finalGear=two_trip::inventory(ready);if(!finalGear)return pending("completed final gear fuel");
        cambc::detail::champion_info final{};
        cambc::detail::check(cambc::detail::sdk_champion(nativeSelf,&final));
        const auto finalAbilities=live_tavi_input(me,final,stats,evaluationInput_.tavi,ready);
        if(!ready() || cambc::round_number()!=round)return pending("completed final round/fuel");
        if(!finalAbilities || *finalGear!=*inventory || me.gem_count()!=0 || final.x!=initial.x || final.y!=initial.y ||
           final.health!=initial.health || final.max_health!=initial.max_health || final.strength!=initial.strength || final.focus!=initial.focus ||
           final.attack_range!=initial.attack_range || final.state!=initial.state)
            return stale(*finalGear!=*inventory?ResetReason::inventory:
                (final.x!=initial.x || final.y!=initial.y)?ResetReason::origin:ResetReason::semantic,"completed final actor key");
        input.tavi=*finalAbilities;if(!evaluation_->matches(input))return stale(ResetReason::semantic,"completed final ability key");
        if(!ready() || cambc::round_number()!=round)return pending("completed publication round/fuel");
        auto candidate=std::make_unique<Candidate>();auto& plan=candidate->plan;const auto& geometry=*geometry_;
        plan.groupKey=group.key;plan.nativeGroup=nativeGroup;plan.memberCount=count;plan.members=evaluationPrototypes_;
        plan.inventory=*inventory;plan.stats=stats;plan.attackRange=initial.attack_range;plan.preparedRound=round;
        plan.decisionOrigin={initial.x,initial.y};plan.expectedPosition=plan.decisionOrigin;plan.origin=evaluationQ_;plan.route=route_;
        plan.escapePoint=geometry.escapePoint;plan.captureTile=geometry.captureTile;plan.altarTile=geometry.altarTile;
        plan.altarApproach=geometry.altarApproach;plan.altarIndex=geometry.altarIndex;
        plan.escapePath=geometry.escapePath;plan.capturePath=geometry.capturePath;plan.bankPath=geometry.bankPath;
        plan.escapeCount=geometry.escapeCount;plan.captureCount=geometry.captureCount;plan.bankCount=geometry.bankCount;
        candidate->input=input;candidate->accounting=*result->schedule;plan.input=input;plan.accounting=candidate->accounting;
        plan.deadline=input.round+input.horizon;
        ModelTrace trace;trace.state=result->state;trace.reason=result->reason;trace.work=result->work;
        trace.resumed=trace.complete=true;trace.completedOwnerTurns=evaluation_->owner_turns();
        trace.before=trace.after=cambc::fuel_remaining();
        if(!ready() || cambc::round_number()!=round)return pending("completed copied publication fuel/round");
        PreparationMetrics::increment(metrics_.candidateSnapshots);
        return {Status::ready,"fresh completed private bound",std::move(candidate),trace};
    }
public:
    const PreparationMetrics& metrics() const {return metrics_;}
    void note_forecast(){PreparationMetrics::increment(metrics_.forecasts);}
    void note_episode_started(){PreparationMetrics::increment(metrics_.episodesStarted);}
    void note_episode_converted(){PreparationMetrics::increment(metrics_.episodesConverted);}
    void note_episode_aborted(){PreparationMetrics::increment(metrics_.episodesAborted);}
    bool has_complete_work() const {
        const auto* result=evaluation_?evaluation_->result():nullptr;
        return result && result->schedule.has_value();
    }
    void reset_preparation(){
        reset_route_preparation();groupKey_=-1;range_=-1;
        qCursor_=qMemberCount_=qFeasibleCount_=0;qStarted_=qFound_=qComplete_=false;qBestDistance_=qBestKey_=0;
        qWidth_=qHeight_=0;qChampion_=-1;qNativeGroup_=0;qWallsGeneration_=0;origin_={};
    }
    template<class Ready> void advance_preparation(StaticPaths& shared,Ready&& ready) {
        if(qStarted_ && !qComplete_)metrics_.enter(PreparationStage::stationary_geometry);
        advance_q_feasibility(ready);
        if(route_ && ready()) {
            if(!route_->ready())metrics_.enter(PreparationStage::route_field);
            route_->advance(shared,ready);
        }
        if(exposure_) {
            if(exposure_->pending())metrics_.enter(PreparationStage::outward_exposure);
            exposure_->advance(ready);
        }
    }
    template<class Ready> PrepareResult prepare(const PersonalInformation& personal,std::size_t index,
            const EquipmentStats& stats,StaticPaths& shared,Ready&& ready,bool allowAdvance=true) {
        metrics_.enter(PreparationStage::gate);
        const auto stop=[](Status s,const char* why){return PrepareResult{s,why,nullptr};};
        if(has_complete_work())return publish_complete(personal,index,stats,ready);
        if(!allowAdvance)return stop(Status::pending,"preparation cadence");
        if(!ready())return stop(Status::cutoff,"fuel");
        const auto& me=cambc::self();const int width=cambc::map::width(),height=cambc::map::height();
        const unsigned round=cambc::round_number();
        if(width<1 || height<1 || std::int64_t(width)*height>two_trip::maximum_tiles || !personal.initialization_complete ||
           index>=personal.groups.size() || !me.is_alive() || me.champion_name()!=cambc::champion_type::tavi || me.gem_count())
            return stop(Status::unsupported,"actor/domain");
        // Owned attempt-local records replace repeated live getters. They are
        // never cached or reused after an action/callback; errors match info().
        const auto nativeSelf=static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(me));
        cambc::detail::champion_info initial{};
        cambc::detail::check(cambc::detail::sdk_champion(nativeSelf,&initial));
        const auto channelMask=cambc::champion_state::recalling|cambc::champion_state::submitting_gems|cambc::champion_state::teleporting;
        if((cambc::champion_state::from_bits_retain(initial.state)&channelMask).bits() || me.buffs().has_stun())
            return stop(Status::unsupported,"actor/domain");
        if(!model::scalar(stats.health) || !model::scalar(stats.strength) || !model::scalar(stats.focus) ||
           !model::scalar(stats.campDamage) || !model::scalar(stats.campMitigation) || stats.campDamage<=0 || stats.strength<=0 ||
           !close(initial.max_health,stats.health) || !close(initial.strength,stats.strength) || !close(initial.focus,stats.focus))
            return stop(Status::unsupported,"stats");
        const auto& group=personal.groups[index];const auto& camps=group.group.camps();
        if(camps.empty() || camps.size()>model::maximum_members || camps.size()!=group.members.size() ||
           camps.size()!=group.spawn_positions.size() || group.gem_value<=0 || personal.spawn_positions.empty() ||
           personal.spawn_positions.size()>maximum_leg || group.key<0 || personal.altars.empty())
            return stop(Status::unsupported,"public shape");
        const int range=initial.attack_range;
        if(range<1 || range>4 || round>=cambc::constants::max_rounds)return stop(Status::unsupported,"range/deadline");
        if(!cambc::vision::champions(cambc::team::enemy).empty())return stop(Status::unsupported,"enemy");
        if(cambc::round_number()!=round)return stop(Status::cutoff,"source round");
        // PersonalInformation owns immutable terrain and camp spawn metadata
        // after initialization. Its owner/buffer identity and map dimensions
        // bind this per-bot, per-bundle wall snapshot; no live state is cached.
        if(!walls_ || wallsOwner_!=&personal || wallsTerrain_!=personal.terrain.ptr() ||
           wallsWidth_!=width || wallsHeight_!=height || groups_!=personal.groups.size()) {
            walls_=personal.terrain;wallsOwner_=&personal;wallsTerrain_=personal.terrain.ptr();
            wallsWidth_=width;wallsHeight_=height;++wallsGeneration_;
            wallCursor_=0;groups_=personal.groups.size();reset_preparation();
        }
        metrics_.enter(PreparationStage::public_walls);
        for(unsigned work=0;wallCursor_<groups_ && work<8;++work) {
            if(!ready())return stop(Status::cutoff,"public walls");
            *walls_|=personal.groups[wallCursor_].spawns;++wallCursor_;
        }
        if(wallCursor_<groups_)return stop(Status::pending,"public walls");
        const auto nativeGroup=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(group.group));
        bool samePublic=publicStarted_ && publicWidth_==width && publicHeight_==height && publicKey_==group.key &&
            publicGroup_==nativeGroup && publicCount_==camps.size();
        for(unsigned i=0;samePublic && i<camps.size();++i)
            samePublic=publicIds_[i]==static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camps[i])) &&
                publicSpawns_[i]==camps[i].spawn_position() && publicKinds_[i]==static_cast<int>(camps[i].kind());
        if(!samePublic) {
            publicStarted_=true;publicWidth_=width;publicHeight_=height;publicKey_=group.key;publicGroup_=nativeGroup;
            publicCount_=unsigned(camps.size());publicCursor_=0;reset_preparation();
            for(unsigned i=0;i<publicCount_;++i) {
                publicIds_[i]=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camps[i]));
                publicSpawns_[i]=camps[i].spawn_position();publicKinds_[i]=static_cast<int>(camps[i].kind());
            }
        }
        metrics_.enter(PreparationStage::public_members);metrics_.lastGroup=group.key;
        while(publicCursor_<publicCount_) {
            if(!ready())return stop(Status::cutoff,"public members");
            const auto& camp=camps[publicCursor_];Member member;member.spawn=camp.spawn_position();
            member.key=member.spawn.y*width+member.spawn.x;member.native=publicIds_[publicCursor_];
            const auto base=camp.base_stats();member.range=base.base_attack_range;member.fullHealth=member.lastHealth=base.base_health;
            member.incoming=base.base_strength;
            if(member.spawn.x<0 || member.spawn.y<0 || member.spawn.x>=width || member.spawn.y>=height ||
               member.range<0 || member.range>16 || !model::scalar(member.fullHealth) || member.fullHealth<=0 ||
               !model::scalar(member.incoming))return stop(Status::unsupported,"public member numeric");
            unsigned matches=0;
            for(const auto& m:group.members)if(m.spawn==member.spawn && close(m.health,member.fullHealth) && close(m.strength,member.incoming))++matches;
            if(matches!=1 || !group.spawns.is_set(member.spawn))return stop(Status::unknown,"public member join");
            for(unsigned j=0;j<publicCursor_;++j)if(publicMembers_[j].key==member.key || publicMembers_[j].native==member.native)
                return stop(Status::unknown,"duplicate public member");
            publicMembers_[publicCursor_++]=member;
        }
        auto candidate=std::make_unique<Candidate>();auto& plan=candidate->plan;
        plan.groupKey=group.key;plan.nativeGroup=nativeGroup;
        plan.memberCount=publicCount_;plan.stats=stats;plan.attackRange=range;
        plan.preparedRound=round;plan.decisionOrigin={initial.x,initial.y};plan.expectedPosition=plan.decisionOrigin;
        plan.members=publicMembers_; // Value ownership; live execution cannot mutate prototypes.
        std::sort(plan.members.begin(),plan.members.begin()+plan.memberCount,[](const auto& a,const auto& b){return a.key<b.key;});
        if(plan.members[0].key!=group.key)return stop(Status::unknown,"public group key");
        const int champion=static_cast<int>(me.champion_name());
        bool sameGeometry=groupKey_==group.key && range_==range && qWidth_==width && qHeight_==height &&
            qNativeGroup_==plan.nativeGroup && qMemberCount_==plan.memberCount && qChampion_==champion &&
            qWallsGeneration_==wallsGeneration_;
        for(unsigned i=0;sameGeometry && i<plan.memberCount;++i)
            sameGeometry=qMemberKeys_[i]==plan.members[i].key && qMemberIds_[i]==plan.members[i].native &&
                qMemberSpawns_[i]==plan.members[i].spawn;
        if(!sameGeometry) {
            reset_preparation();groupKey_=group.key;range_=range;qWidth_=width;qHeight_=height;
            qNativeGroup_=plan.nativeGroup;qMemberCount_=plan.memberCount;qChampion_=champion;
            qWallsGeneration_=wallsGeneration_;qStarted_=true;
            for(unsigned i=0;i<plan.memberCount;++i) {
                qMemberKeys_[i]=plan.members[i].key;qMemberIds_[i]=plan.members[i].native;
                qMemberSpawns_[i]=plan.members[i].spawn;
            }
        }
        metrics_.enter(PreparationStage::stationary_geometry);
        advance_q_feasibility(ready);
        if(!qComplete_)return stop(ready()?Status::pending:Status::cutoff,"stationary geometry");
        if(!qFound_)return stop(Status::unsupported,"stationary Q");
        // Static feasibility never depends on the decision origin. First
        // bind the COMPLETE set using this attempt's fresh actor snapshot.
        // The declared Q/route then survives ordinary movement and recovery;
        // outward costs and the modeled actor input still bind freshly below.
        if(!route_) {
            if(!ready() || cambc::round_number()!=round)return stop(Status::cutoff,"stationary ranking round/fuel");
            bool ranked=false;cambc::vec2 best{};int bestDistance=0,bestKey=0;
            for(unsigned i=0;i<qFeasibleCount_;++i) {
                const auto q=qFeasible_[i];const int d=distance(plan.decisionOrigin,q),key=q.y*width+q.x;
                if(!ranked || std::pair{d,key}<std::pair{bestDistance,bestKey}){ranked=true;best=q;bestDistance=d;bestKey=key;}
            }
            if(!ready() || cambc::round_number()!=round)return stop(Status::cutoff,"stationary ranking round/fuel");
            qDecisionOrigin_=plan.decisionOrigin;qBestDistance_=bestDistance;qBestKey_=bestKey;
            reset_route_preparation();origin_=best;
            route_=std::make_shared<two_trip::RouteField>(*walls_,origin_);
        }
        metrics_.enter(PreparationStage::route_field);
        route_->advance(shared,ready);if(!route_->ready())return stop(Status::pending,"route field");
        plan.route=route_;plan.origin=origin_;
        metrics_.enter(PreparationStage::outward_exposure);
        if(!outward_ || outward_->origin!=plan.decisionOrigin) {
            const int turns=route_->distance(plan.decisionOrigin);
            if(turns<0 || turns>int(maximum_leg))return stop(Status::unsupported,"outward route");
            if(!exposure_)exposure_=std::make_unique<PathExposure>(plan,width,height);
            if(!exposure_->request(plan.decisionOrigin))return stop(Status::unsupported,"outward route");
            auto exposure=exposure_->cost(plan.decisionOrigin);
            if(!exposure) {
                exposure_->advance(ready);
                exposure=exposure_->cost(plan.decisionOrigin);
            }
            if(exposure_->failed())return stop(Status::unsupported,"outward route");
            if(!exposure)return stop(ready()?Status::pending:Status::cutoff,"outward exposure");
            outward_=OutwardGeometry{plan.decisionOrigin,turns,*exposure};
            PreparationMetrics::increment(metrics_.outwardBindings);
        }
        if(!geometry_) {
        metrics_.enter(PreparationStage::escape_geometry);
        constexpr unsigned escapeWidth=2*maximum_escape+1;
        while(escapeCursor_<escapeWidth*escapeWidth) {
            if(!ready())return stop(Status::pending,"escape geometry");
            const int dy=int(escapeCursor_/escapeWidth)-int(maximum_escape),dx=int(escapeCursor_%escapeWidth)-int(maximum_escape);
            ++escapeCursor_;const cambc::vec2 p{origin_.x+dx,origin_.y+dy};
            const int d=route_->distance(p);if(d<1 || d>int(maximum_escape))continue;
            bool safe=true;for(unsigned i=0;i<plan.memberCount;++i)
                if(plan.members[i].incoming>0 && distance(p,plan.members[i].spawn)<=plan.members[i].range+1)safe=false;
            const int key=p.y*width+p.x;
            if(safe && (!escapeFound_ || std::pair{d,key}<std::pair{escapeDistance_,escapeKey_})) {
                escapeFound_=true;escapeDistance_=d;escapeKey_=key;escapePoint_=p;
            }
        }
        plan.escapePoint=escapePoint_;
        if(!escapeFound_ || !route_->from_origin(plan.escapePoint,plan.escapePath,plan.escapeCount))return stop(Status::unsupported,"safe escape");
        int escapeExposure=0;for(unsigned i=0;i+1<plan.escapeCount;++i)escapeExposure+=exposed(plan,plan.escapePath[i]);
        metrics_.enter(PreparationStage::return_exposure);
        while(spawnCursor_<personal.spawn_positions.size()) {
            if(!ready())return stop(Status::pending,"spawn routes");const auto p=personal.spawn_positions[spawnCursor_];const int d=route_->distance(p);
            if(d<0 || d>int(maximum_leg))return stop(Status::unsupported,"every spawn return");
            const auto exposure=path_exposure(plan,p,ready);if(!exposure)return stop(Status::pending,"return exposure");
            returnDistance_=std::max(returnDistance_,d);returnExposure_=std::max(returnExposure_,int(*exposure));++spawnCursor_;
        }
        metrics_.enter(PreparationStage::capture_altar_geometry);
        plan.captureTile=plan.members[0].spawn;bool captureFound=false;int captureDistance=0,captureKey=0;cambc::vec2 neighbor{};
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            if(!ready())return stop(Status::cutoff,"capture geometry");if(!dx&&!dy)continue;
            const cambc::vec2 p{plan.captureTile.x+dx,plan.captureTile.y+dy};const int d=route_->distance(p);
            if(d<0 || d>=int(maximum_leg))continue;const int key=p.y*width+p.x;
            if(!captureFound || std::pair{d,key}<std::pair{captureDistance,captureKey}) {captureFound=true;captureDistance=d;captureKey=key;neighbor=p;}
        }
        if(!captureFound || !route_->from_origin(neighbor,plan.capturePath,plan.captureCount) || plan.captureCount>=plan.capturePath.size())
            return stop(Status::unsupported,"capture path");
        plan.capturePath[plan.captureCount++]=plan.captureTile;++captureDistance;
        bool altarFound=false;int altarDistance=0,altarKey=0;
        for(unsigned a=0;a<personal.altars.size();++a)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            if(!ready())return stop(Status::cutoff,"altar geometry");if(!dx&&!dy)continue;
            const auto altar=personal.altars[a];const cambc::vec2 p{altar.x+dx,altar.y+dy};const int d=route_->distance(p);
            if(d<0 || d+captureDistance>int(maximum_leg))continue;const int key=p.y*width+p.x;
            if(!altarFound || std::tuple{d,altar.y*width+altar.x,key}<std::tuple{altarDistance,altarKey,plan.altarApproach.y*width+plan.altarApproach.x}) {
                altarFound=true;altarDistance=d;altarKey=altar.y*width+altar.x;plan.altarIndex=int(a);plan.altarTile=altar;plan.altarApproach=p;
            }
        }
        if(!altarFound)return stop(Status::unsupported,"altar route");
        for(unsigned i=plan.captureCount;i>0;--i)plan.bankPath[plan.bankCount++]=plan.capturePath[i-1];
        std::array<cambc::vec2,maximum_leg+1> altarPath{};unsigned altarCount=0;
        if(!route_->from_origin(plan.altarApproach,altarPath,altarCount))return stop(Status::unsupported,"altar path");
        for(unsigned i=1;i<altarCount;++i)plan.bankPath[plan.bankCount++]=altarPath[i];
        auto complete=std::make_unique<StaticGeometry>();
        complete->escapePoint=plan.escapePoint;complete->captureTile=plan.captureTile;complete->altarTile=plan.altarTile;
        complete->altarApproach=plan.altarApproach;complete->altarIndex=plan.altarIndex;
        complete->escapePath=plan.escapePath;complete->capturePath=plan.capturePath;complete->bankPath=plan.bankPath;
        complete->escapeCount=plan.escapeCount;complete->captureCount=plan.captureCount;complete->bankCount=plan.bankCount;
        complete->escapeTurns=escapeDistance_;complete->escapeExposure=escapeExposure;
        complete->returnTurns=returnDistance_;complete->returnExposure=returnExposure_;
        complete->captureTurns=captureDistance;complete->bankTurns=captureDistance+altarDistance;
        geometry_=std::move(complete); // Only complete immutable paths/costs survive.
        }
        const auto& geometry=*geometry_;
        plan.escapePoint=geometry.escapePoint;plan.captureTile=geometry.captureTile;plan.altarTile=geometry.altarTile;
        plan.altarApproach=geometry.altarApproach;plan.altarIndex=geometry.altarIndex;
        plan.escapePath=geometry.escapePath;plan.capturePath=geometry.capturePath;plan.bankPath=geometry.bankPath;
        plan.escapeCount=geometry.escapeCount;plan.captureCount=geometry.captureCount;plan.bankCount=geometry.bankCount;
        if(!ready() || cambc::round_number()!=round)return stop(Status::pending,"static geometry ready");
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))if(lock.position()==plan.altarTile)return stop(Status::unsupported,"altar lock");
        // Finish reusable immutable geometry before spending live inventory
        // and kit queries. These facts remain attempt-local and same-round;
        // an unsupported kit may prepare geometry, but never a model or job.
        if(!ready() || cambc::round_number()!=round)return stop(Status::cutoff,"combat budget check");
        const auto inventory=two_trip::inventory(ready);if(!inventory)return stop(Status::cutoff,"inventory");
        const auto abilities=tavi_input(me,initial,stats,ready);
        if(!abilities)return stop(ready()?Status::unsupported:Status::cutoff,"Tavi ability facts");
        model::Input budget;budget.memberCount=publicCount_;budget.health=initial.health;budget.maximumHealth=stats.health;
        budget.strength=stats.strength;budget.campDamage=stats.campDamage;budget.mitigation=stats.campMitigation;
        budget.tavi=*abilities; // Basic-only impossibility is not an ability bound.
        budget.actorKnown=budget.memberEvidenceKnown=budget.quietNoReset=true;
        for(unsigned i=0;i<publicCount_;++i)budget.members[i]={publicMembers_[i].key,publicMembers_[i].fullHealth,publicMembers_[i].incoming,true};
        const bool impossible=model::combat_budget_exceeded(budget);
        if(!ready() || cambc::round_number()!=round)return stop(Status::cutoff,"combat budget check");
        if(impossible)return stop(Status::unsupported,"combat budget bound");
        plan.inventory=*inventory;
        auto& input=candidate->input;input.round=int(round);input.horizon=int(std::min<unsigned>(model::maximum_turns,cambc::constants::max_rounds-round));
        input.tavi=*abilities;
        input.memberCount=plan.memberCount;input.health=initial.health;input.maximumHealth=stats.health;input.strength=stats.strength;
        input.campDamage=stats.campDamage;input.mitigation=stats.campMitigation;input.gems=group.gem_value;
        for(unsigned i=0;i<plan.memberCount;++i)input.members[i]={plan.members[i].key,plan.members[i].fullHealth,plan.members[i].incoming,true};
        input.outward={outward_->turns,int(outward_->exposure),true,true};input.escape={geometry.escapeTurns,geometry.escapeExposure,true,true};
        input.returnToCamp={geometry.returnTurns,geometry.returnExposure,true,true};input.capture={geometry.captureTurns,0,true,true};input.bank={geometry.bankTurns,0,true,true};
        input.recallTicks=stats.recall;input.captureTicks=cambc::constants::capture_time;input.spawnHealing=cambc::constants::spawn_healing;
        const double tribute=std::ceil(double(group.gem_value)*cambc::constants::submit_gem_duration_multiplier+cambc::constants::submit_gem_base_duration);
        if(!model::scalar(tribute) || tribute>model::maximum_turns)return stop(Status::unsupported,"tribute duration");
        input.tributeTicks=std::max(cambc::constants::submit_gem_minimum_duration,int(tribute));
        input.actorKnown=input.memberEvidenceKnown=input.stationarySupported=input.safeRecall=input.quietNoReset=true;
        if(!ready() || cambc::round_number()!=round)return stop(Status::cutoff,"source round");
        // The private hypothetical cursor is keyed to fresh semantic inputs,
        // not cached observations. Absolute time is rechecked at publication.
        bool sameEvaluation=evaluation_ && evaluation_->matches(input) &&
            evaluationGeometry_==geometry_.get() && evaluationRoute_==route_.get() &&
            evaluationOrigin_==plan.decisionOrigin && evaluationQ_==plan.origin &&
            evaluationWidth_==width && evaluationHeight_==height && evaluationGroup_==plan.nativeGroup &&
            evaluationGroupKey_==plan.groupKey && evaluationInventory_==plan.inventory;
        for(unsigned i=0;sameEvaluation && i<plan.memberCount;++i)
            sameEvaluation=evaluationMembers_[i]==plan.members[i].native && evaluationSpawns_[i]==plan.members[i].spawn;
        if(!sameEvaluation) {
            ResetReason reason=ResetReason::semantic;
            if(evaluation_) {
                if(evaluationOrigin_!=plan.decisionOrigin)reason=ResetReason::origin;
                else if(evaluationInventory_!=plan.inventory)reason=ResetReason::inventory;
                else if(evaluationGeometry_!=geometry_.get() || evaluationRoute_!=route_.get() ||
                        evaluationQ_!=plan.origin || evaluationWidth_!=width || evaluationHeight_!=height ||
                        evaluationGroup_!=plan.nativeGroup || evaluationGroupKey_!=plan.groupKey)reason=ResetReason::geometry;
                else for(unsigned i=0;i<plan.memberCount;++i)
                    if(evaluationMembers_[i]!=plan.members[i].native || evaluationSpawns_[i]!=plan.members[i].spawn)
                        reason=ResetReason::geometry;
            }
            const bool replacing=bool(evaluation_);
            const unsigned lost=replacing?evaluation_->owner_turns():0;
            evaluation_=std::make_unique<model::TaviEvaluation>();evaluation_->start(input);
            if(replacing)metrics_.reset(reason,lost);
            evaluationTerminalCounted_=false;PreparationMetrics::increment(metrics_.modelStarts);
            evaluationGeometry_=geometry_.get();evaluationRoute_=route_.get();
            evaluationOrigin_=plan.decisionOrigin;evaluationQ_=plan.origin;
            evaluationWidth_=width;evaluationHeight_=height;evaluationGroup_=plan.nativeGroup;evaluationGroupKey_=plan.groupKey;
            evaluationInventory_=plan.inventory;
            evaluationOwnedSpawns_=personal.spawn_positions;
            evaluationInput_=input;evaluationStats_=stats;evaluationAttackRange_=range;evaluationGold_=group.gold_value;
            evaluationPrototypes_=plan.members;
            for(unsigned i=0;i<plan.memberCount;++i){
                evaluationMembers_[i]=plan.members[i].native;evaluationSpawns_[i]=plan.members[i].spawn;
                for(unsigned j=0;j<publicCount_;++j)if(publicIds_[j]==plan.members[i].native)evaluationKinds_[i]=publicKinds_[j];
            }
        }
        // At most four combat-owner-turn projections per callback. Existing
        // batched Ready polling and the direct publication fences are retained.
        unsigned modelChecks=0;bool modelAllowed=true;
        const auto modelReady=[&] {
            if(!modelAllowed)return false;
            if((modelChecks++%8)==0)modelAllowed=ready();
            return modelAllowed;
        };
        ModelTrace trace;trace.before=cambc::fuel_remaining();trace.resumed=sameEvaluation;trace.reason=model::Reason::none;
        const unsigned beforeOwnerTurns=evaluation_->owner_turns();
        metrics_.enter(PreparationStage::model);
        trace.complete=evaluation_->advance(modelReady,4);
        account_evaluation();
        trace.completedOwnerTurns=evaluation_->owner_turns();trace.chunkOwnerTurns=trace.completedOwnerTurns-beforeOwnerTurns;
        trace.work=evaluation_->work();trace.after=cambc::fuel_remaining();
        const auto* result=evaluation_->result();
        if(result){trace.state=result->state;trace.reason=result->reason;}
        const auto modelStop=[&](Status s,const char* why){return PrepareResult{s,why,nullptr,trace};};
        if(!ready() || cambc::round_number()!=round)return modelStop(Status::pending,"model round");
        if(!result)return modelStop(Status::pending,"private model pending");
        if(!result->schedule)return modelStop(result->state==model::State::unknown?Status::unknown:Status::unsupported,"full sortie bound");
        if(result->schedule->tau>input.horizon)return modelStop(Status::unsupported,"latest model deadline");
        const auto gear=two_trip::inventory(ready);if(!gear)return modelStop(Status::pending,"final gear");
        cambc::detail::champion_info final{};
        cambc::detail::check(cambc::detail::sdk_champion(nativeSelf,&final));
        const auto finalAbilities=tavi_input(me,final,stats,ready);
        if(!ready())return modelStop(Status::pending,"final actor");
        if(!finalAbilities || finalAbilities->burstModifier!=abilities->burstModifier ||
           finalAbilities->burstRange!=abilities->burstRange || finalAbilities->stunRounds!=abilities->stunRounds ||
           finalAbilities->resetMaxCooldown!=abilities->resetMaxCooldown || finalAbilities->burstMaxCooldown!=abilities->burstMaxCooldown ||
           finalAbilities->resetCooldown!=abilities->resetCooldown ||
           finalAbilities->burstCooldown!=abilities->burstCooldown ||
           finalAbilities->initialCanAttack!=abilities->initialCanAttack ||
           finalAbilities->initialCanCast!=abilities->initialCanCast ||
           finalAbilities->initialBurstAllowed!=abilities->initialBurstAllowed ||
           *gear!=plan.inventory || me.gem_count()!=0 || cambc::vec2{final.x,final.y}!=plan.decisionOrigin || final.health!=initial.health ||
           !close(final.max_health,stats.health) || !close(final.strength,stats.strength) || !close(final.focus,stats.focus) ||
           final.attack_range!=initial.attack_range || (cambc::champion_state::from_bits_retain(final.state)&channelMask).bits() ||
           cambc::round_number()!=round)return modelStop(Status::unknown,"changed decision actor");
        candidate->accounting=*result->schedule;plan.input=input;plan.accounting=candidate->accounting;plan.deadline=input.round+input.horizon;
        metrics_.enter(PreparationStage::complete);PreparationMetrics::increment(metrics_.candidateSnapshots);
        return {Status::ready,"assumed quiet public member bound",std::move(candidate),trace};
    }
};

// Complete current reads are used only at stationary Q, where every member's
// public spawn is personally visible. Expected unavailable states abort this
// episode; unexpected engine errors still reach the Campaign turn boundary.
template<class Ready> Status read_members(Plan& plan,const GroupDesign& group,unsigned round,Ready&& ready,bool& allDead) {
    if(!ready())return Status::cutoff;
    const auto visible=cambc::vision::visible_tiles();
    for(unsigned i=0;i<plan.memberCount;++i)if(!visible.is_set(plan.members[i].spawn))return Status::abort;
    if(cambc::round_number()!=round)return Status::abort;
    std::uint32_t lifecycle{};const auto code=cambc::detail::sdk_camp_group_state(plan.nativeGroup,&lifecycle);
    if(code==std::int32_t(cambc::error_code::api_not_in_vision) || code==std::int32_t(cambc::error_code::api_not_found))return Status::abort;
    cambc::detail::check(code);if(lifecycle>3)throw cambc::error{cambc::error_code::api_value_out_of_range,"sortie group state"};
    if(lifecycle!=1 && lifecycle!=2)return Status::abort;
    if(lifecycle==2) {
        for(unsigned i=0;i<plan.memberCount;++i) {
            if(!ready())return Status::cutoff;if(!visible.is_set(plan.members[i].spawn))return Status::abort;
            std::uint32_t s{};const auto code=cambc::detail::sdk_camp_state(plan.members[i].native,&s);
            if(code==std::int32_t(cambc::error_code::api_not_in_vision) || code==std::int32_t(cambc::error_code::api_not_found))return Status::abort;
            cambc::detail::check(code);if(s>2)throw cambc::error{cambc::error_code::api_value_out_of_range,"sortie camp state"};
            if(s)return Status::abort;plan.members[i].lastHealth=0;
        }
        allDead=true;return cambc::round_number()==round?Status::ready:Status::abort;
    }
    const auto result=observation::read(group,round,ready);
    if(!result.snapshot)return result.state==observation::State::cutoff?Status::cutoff:Status::abort;
    const auto& snapshot=*result.snapshot;if(snapshot.memberCount!=plan.memberCount || snapshot.nativeGroup!=plan.nativeGroup)return Status::abort;
    allDead=true;
    for(unsigned i=0;i<plan.memberCount;++i) {
        if(!ready())return Status::cutoff;const auto& member=plan.members[i];int match=-1;
        for(unsigned j=0;j<snapshot.memberCount;++j)if(snapshot.nativeMemberIds[j]==member.native && snapshot.members[j].key==member.key)match=int(j);
        if(match<0)return Status::abort;const auto& current=snapshot.members[unsigned(match)];
        if(current.health>member.lastHealth && !close(current.health,member.lastHealth))return Status::abort;
        if(plan.input.tavi.enabled && plan.damageMade && current.health<member.lastHealth &&
           !close(current.health,member.lastHealth) && plan.pendingAttack!=int(i))return Status::abort;
        if(plan.pendingAttack==int(i) && current.health>=member.lastHealth)return Status::abort;
        if(current.health>0 && !close(current.incoming,member.incoming))return Status::abort;
        plan.members[i].lastHealth=current.health;allDead&=current.health==0;
    }
    plan.pendingAttack=-1;return Status::ready;
}
template<class Ready> StepResult step(Plan& plan,Executor& executor,const PersonalInformation& personal,StaticPaths&,
        const std::vector<cambc::camp>& camps,const std::vector<cambc::champion>& enemies,Ready&& ready) {
    bool captured=false;
    const auto stop=[&](Status s,const char* reason,bool progress=false){return StepResult{s,reason,progress,captured,plan.phase==Phase::bank || plan.phase==Phase::conversion_wait};};
    if(!ready())return stop(Status::cutoff,"fuel");const auto& me=cambc::self();const unsigned round=cambc::round_number();
    const bool banking=plan.phase==Phase::bank || plan.phase==Phase::conversion_wait;
    if(!me.is_alive() || !model::scalar(me.health()) || me.health()<=0 || me.is_teleporting() || me.buffs().has_stun() ||
       (me.is_recalling() && plan.phase!=Phase::recall) || (me.is_submitting_gems() && !banking) ||
       (!banking && plan.phase!=Phase::capture && me.gem_count()))return stop(Status::abort,"actor/death/channel/bag");
    if(round>=unsigned(plan.deadline) || (plan.started && (round<plan.lastRound || std::uint64_t(round)>std::uint64_t(plan.lastRound)+1)))
        return stop(Status::abort,"deadline/observation gap");
    if(plan.lastActionRound==round)return stop(Status::handled,"one action callback");
    const auto gear=two_trip::inventory(ready);if(!gear)return stop(Status::cutoff,"gear fuel");
    if(*gear!=plan.inventory || !close(me.max_health(),plan.stats.health) || !close(me.strength(),plan.stats.strength) ||
       !close(me.focus(),plan.stats.focus) || me.attack_range()!=plan.attackRange)return stop(Status::abort,"gear/stats changed");
    if(!enemies.empty())return stop(Status::abort,"enemy interference");
    if(plan.started && plan.phase!=Phase::recall && me.position()!=plan.expectedPosition)return stop(Status::abort,"external displacement");
    if(!plan.started && (round!=plan.preparedRound || me.position()!=plan.decisionOrigin))return stop(Status::abort,"stale start");
    if(!plan.started){plan.started=true;plan.lastProgress=round;}plan.lastRound=round;
    if(round-plan.lastProgress>=8 && plan.phase!=Phase::recall && plan.phase!=Phase::healing)return stop(Status::abort,"no progress");
    const GroupDesign* group=nullptr;
    for(const auto& g:personal.groups){if(!ready())return stop(Status::cutoff,"group lookup");if(g.key==plan.groupKey){group=&g;break;}}
    if(!group || static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(group->group))!=plan.nativeGroup)return stop(Status::abort,"group identity");
    for(const auto& ally:cambc::vision::champions(cambc::team::ally))if(ally!=me && group->spawns.is_set(ally.position()))return stop(Status::abort,"capture sharing");
    for(const auto& camp:camps) {
        if(!ready())return stop(Status::cutoff,"visible danger");
        if(!target(plan,camp) && camp.is_alive() && camp.is_awake() && distance(me.position(),camp.position())<=camp.attack_range()+1)
            return stop(Status::abort,"other camp danger");
    }
    const auto move=[&](cambc::vec2 p,const char* reason) {
        if(!ready() || cambc::round_number()!=round)return stop(Status::cutoff,"move round");
        for(const auto& camp:camps)if(!target(plan,camp) && camp.is_alive() && camp.is_awake() &&
            distance(p,camp.position())<=camp.attack_range()+1)return stop(Status::abort,"next route danger");
        if(!banking && plan.phase!=Phase::capture && exposed(plan,me.position())) {
            double incoming=0;for(unsigned i=0;i<plan.memberCount;++i)if(plan.members[i].lastHealth>0)incoming+=plan.members[i].incoming;
            if(me.health()-incoming*plan.stats.campMitigation<plan.stats.health*.25)return stop(Status::abort,"route exposure reserve");
        }
        const bool moved=executor.move_one(p);if(moved){plan.expectedPosition=p;plan.lastProgress=round;plan.lastActionRound=round;}
        return stop(Status::handled,reason,moved);
    };
    if(plan.phase==Phase::recall) {
        if(me.is_recalling())return stop(Status::handled,"recalling");
        if(!personal.own_spawn.is_set(me.position()) || round<plan.recallRound+unsigned(std::max(1,plan.input.recallTicks)))return stop(Status::abort,"recall interrupted");
        plan.expectedPosition=me.position();plan.phase=Phase::healing;plan.lastProgress=round;
    }
    if(plan.phase==Phase::healing) {
        if(!personal.own_spawn.is_set(me.position()))return stop(Status::abort,"healing displaced");
        if(me.health()<me.max_health())return stop(Status::handled,"full spawn healing");
        ++plan.sortie;if(plan.sortie>=model::maximum_sorties)return stop(Status::abort,"sortie cap");
        // The first full-health owner start can immediately begin the charged
        // return leg. No unmodeled extra idle callback is inserted here.
        plan.phase=Phase::returning;plan.combatBound=false;plan.lastProgress=round;
    }
    if(plan.phase==Phase::approach || plan.phase==Phase::returning) {
        if(me.position()!=plan.origin) {
            const auto next=plan.route->toward_origin(me.position());if(!next || *next==me.position())return stop(Status::abort,"fixed Q route");
            return move(*next,plan.phase==Phase::approach?"outward":"returning");
        }
        plan.phase=Phase::combat;plan.combatBound=false;plan.usedAttacks.fill(0);plan.lastProgress=round;
    }
    if(plan.phase==Phase::combat) {
        bool allDead=false;const auto read=read_members(plan,*group,round,ready,allDead);
        if(read!=Status::ready)return stop(read,"current complete members");
        if(allDead){plan.phase=Phase::capture;plan.captureAt=1;plan.lastProgress=round;}
        else {
            if(plan.input.tavi.enabled && !plan.combatBound) {
                // Bind the ORIGINAL complete quiet schedule. A changed entry
                // can abort/recover; it never starts an atomic model at Q.
                if(plan.sortie>=plan.accounting.sortieCount)return stop(Status::abort,"cached phase missing");
                const auto& phase=plan.accounting.phases[plan.sortie];
                if(!phase.turnCount || phase.turnBegin+phase.turnCount>plan.accounting.turnCount ||
                   plan.accounting.tau<phase.combatStartedAt ||
                   std::int64_t(round)+plan.accounting.tau-phase.combatStartedAt>plan.deadline)
                    return stop(Status::abort,"cached phase deadline/shape");
                cambc::detail::champion_info actor{};
                cambc::detail::check(cambc::detail::sdk_champion(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(me)),&actor));
                const auto current=tavi_input(me,actor,plan.stats,ready);
                if(!current)return stop(ready()?Status::abort:Status::cutoff,"current Tavi ability facts");
                const auto& kit=plan.input.tavi;
                if(cambc::vec2{actor.x,actor.y}!=plan.origin || actor.health<phase.entryEndHealth ||
                   !close(actor.max_health,plan.stats.health) || !close(actor.strength,plan.stats.strength) || !close(actor.focus,plan.stats.focus) ||
                   current->focus!=kit.focus || current->burstModifier!=kit.burstModifier || current->burstCampDamage!=kit.burstCampDamage ||
                   current->burstRange!=kit.burstRange || current->stunRounds!=kit.stunRounds ||
                   current->resetMaxCooldown!=kit.resetMaxCooldown || current->burstMaxCooldown!=kit.burstMaxCooldown ||
                   current->resetCooldown>phase.combatEntryResetCooldown || current->burstCooldown>phase.combatEntryBurstCooldown)
                    return stop(Status::abort,"cached phase actor/cooldown");
                unsigned first=model::maximum_members;
                for(unsigned i=0;i<plan.memberCount;++i) {
                    const double bound=plan.sortie?plan.accounting.phases[plan.sortie-1].remainingHealth[i]:plan.input.members[i].health;
                    if(plan.members[i].lastHealth>bound)return stop(Status::abort,"cached phase member bound");
                    if(first==model::maximum_members && plan.members[i].lastHealth>0)first=i;
                }
                const auto& action=plan.accounting.turns[phase.turnBegin];
                if(action.member!=first || (action.beforeBasic && !current->initialCanAttack) ||
                   (action.afterBasic && action.cast!=model::TaviCast::reset && !current->initialCanAttack) ||
                   (action.cast!=model::TaviCast::none && !current->initialCanCast) ||
                   (action.cast==model::TaviCast::burst && !current->initialBurstAllowed))
                    return stop(Status::abort,"cached phase action allowance");
                for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))
                    if(lock.position()==plan.altarTile)return stop(Status::abort,"cached phase altar lock");
                if(!ready() || cambc::round_number()!=round)return stop(Status::cutoff,"cached phase round");
                plan.combatBound=true;plan.combatAt=0;plan.usedAttacks.fill(0);
            }
            if(!plan.input.tavi.enabled && !plan.combatBound) {
                auto input=plan.input;input.round=int(round);input.horizon=plan.deadline-int(round);input.health=me.health();input.outward={0,0,true,true};
                for(unsigned i=0;i<plan.memberCount;++i)input.members[i].health=plan.members[i].lastHealth;
                if(!ready())return stop(Status::cutoff,"remaining model fuel");++plan.remainingModelEvaluations;const auto remaining=model::evaluate(input,ready);
                if(!ready() || cambc::round_number()!=round)return stop(Status::cutoff,"remaining model round");
                if(remaining.reason==model::Reason::cutoff)return stop(Status::cutoff,"remaining model cutoff");
                if(!remaining.schedule || remaining.schedule->sortieCount>model::maximum_sorties-plan.sortie ||
                   remaining.schedule->attackCount>model::maximum_attacks-plan.attacksMade)return stop(Status::abort,"remaining whole Cycle bound");
                plan.accounting=*remaining.schedule;plan.combatBound=true;plan.usedAttacks.fill(0);plan.combatAt=0;
            }
            const auto& phase=plan.accounting.phases[plan.input.tavi.enabled?plan.sortie:0];
            if(plan.input.tavi.enabled && plan.combatAt<phase.turnCount) {
                const auto& turn=plan.accounting.turns[phase.turnBegin+plan.combatAt];
                if(turn.member>=plan.memberCount || turn.beforeBasic>1 || turn.afterBasic>1 ||
                   unsigned(turn.beforeBasic)+turn.afterBasic>2)return stop(Status::abort,"Tavi action plan");
                unsigned first=model::maximum_members;
                for(unsigned i=0;i<plan.memberCount;++i)if(plan.members[i].lastHealth>0){first=i;break;}
                if(first!=turn.member)return stop(Status::abort,"cached target sequence changed");
                const unsigned next=turn.member;const cambc::camp* victim=nullptr;
                for(const auto& camp:camps)if(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camp))==plan.members[next].native)victim=&camp;
                if(!victim || !victim->is_alive() || victim->position()!=plan.members[next].spawn ||
                   distance(me.position(),victim->position())>plan.attackRange)return stop(Status::abort,"visible Tavi target");
                const auto& abilities=plan.input.tavi;
                const double basic=plan.stats.strength*plan.stats.campDamage;
                const double burst=turn.cast==model::TaviCast::burst?abilities.focus*abilities.burstModifier*abilities.burstCampDamage:0;
                const double damage=burst+basic*(unsigned(turn.beforeBasic)+turn.afterBasic);
                double incoming=0,after=0;
                for(unsigned i=0;i<plan.memberCount;++i)if(plan.members[i].lastHealth>0) {
                    incoming+=plan.members[i].incoming;
                    if(i!=next || plan.members[i].lastHealth>damage)after+=plan.members[i].incoming;
                }
                if(me.health()-plan.stats.campMitigation*(incoming+after*plan.input.escape.exposureTurns)<plan.stats.health*.25)
                    return stop(Status::abort,"Tavi turn and escape reserve");
                // Refresh after EACH actual hit. A shortened target cannot be
                // hit again, and no planned burst is credited after a failure.
                const auto endpoint=[&] {
                    if(!ready() || cambc::round_number()!=round)return false;
                    std::uint32_t state{};const auto code=cambc::detail::sdk_camp_state(plan.members[next].native,&state);
                    if(code==std::int32_t(cambc::error_code::api_not_in_vision) || code==std::int32_t(cambc::error_code::api_not_found))return false;
                    cambc::detail::check(code);if(state>2)throw cambc::error{cambc::error_code::api_value_out_of_range,"Tavi attack camp state"};
                    if(!state){plan.members[next].lastHealth=0;plan.pendingAttack=-1;return cambc::round_number()==round;}
                    cambc::detail::camp_state current{};const auto result=cambc::detail::sdk_camp(plan.members[next].native,&current);
                    if(result==std::int32_t(cambc::error_code::api_not_in_vision) || result==std::int32_t(cambc::error_code::api_not_found))return false;
                    cambc::detail::check(result);
                    if(!model::scalar(current.health) || current.health<=0 || current.health>=plan.members[next].lastHealth)return false;
                    plan.members[next].lastHealth=current.health;plan.pendingAttack=-1;
                    return ready() && cambc::round_number()==round;
                };
                bool acted=false;
                const auto attack=[&] {
                    if(plan.members[next].lastHealth<=0)return true;
                    if(!ready() || cambc::round_number()!=round || !me.can_attack() ||
                       plan.attacksMade>=model::maximum_attacks)return false;
                    me.attack(*victim);++plan.attacksMade;++plan.usedAttacks[next];plan.pendingAttack=int(next);acted=true;plan.damageMade=true;
                    return endpoint();
                };
                const auto cast=[&](unsigned slot) {
                    if(plan.members[next].lastHealth<=0)return true;
                    if(!ready() || cambc::round_number()!=round)return false;
                    const auto current=me.abilities();const auto& ability=current[slot];
                    if(!ability.is_ready())return false;
                    bool success=false;
                    if(slot==1) {
                        if(!ability.is_cc() || !me.can_move() || !ability.can_target_camps() ||
                           !ability.valid_tiles().is_set(victim->position()))return false;
                        success=ability.use(*victim);acted|=success;
                        if(!success)return false;
                        plan.pendingAttack=int(next);plan.damageMade=true;return endpoint();
                    }
                    if(!ability.is_attack_reset())return false;
                    success=ability.use();acted|=success;
                    return success && ready() && cambc::round_number()==round;
                };
                if(turn.cast==model::TaviCast::burst && !cast(1))return stop(Status::abort,"planned burst unavailable/failed");
                if(turn.beforeBasic && !attack())return stop(Status::abort,"planned basic unavailable/failed");
                if(turn.cast==model::TaviCast::reset && !cast(0))return stop(Status::abort,"planned reset unavailable/failed");
                if(turn.afterBasic && !attack())return stop(Status::abort,"planned reset basic unavailable/failed");
                if(!ready() || cambc::round_number()!=round)return stop(Status::abort,"Tavi action round");
                ++plan.combatAt;plan.lastActionRound=round;if(acted)plan.lastProgress=round;
                return stop(Status::handled,"canonical Tavi ability turn",acted);
            }
            int next=-1;
            if(!plan.input.tavi.enabled)
            for(unsigned i=0;i<plan.memberCount;++i)if(plan.members[i].lastHealth>0 && plan.usedAttacks[i]<phase.attacksByMember[i]){next=int(i);break;}
            if(next<0) {plan.phase=Phase::escape;plan.escapeAt=1;plan.lastProgress=round;}
            else {
                if(plan.attacksMade>=model::maximum_attacks || plan.sortie>=model::maximum_sorties)return stop(Status::abort,"attack/sortie cap");
                const cambc::camp* victim=nullptr;
                for(const auto& camp:camps)if(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camp))==plan.members[unsigned(next)].native)victim=&camp;
                if(!victim || !victim->is_alive() || victim->position()!=plan.members[unsigned(next)].spawn ||
                   distance(me.position(),victim->position())>plan.attackRange)return stop(Status::abort,"visible stationary target");
                double incoming=0,after=0;const double damage=plan.stats.strength*plan.stats.campDamage;
                for(unsigned i=0;i<plan.memberCount;++i)if(plan.members[i].lastHealth>0) {
                    incoming+=plan.members[i].incoming;
                    if(i!=unsigned(next) || plan.members[i].lastHealth>damage)after+=plan.members[i].incoming;
                }
                if(me.health()-plan.stats.campMitigation*(incoming+after*plan.input.escape.exposureTurns)<plan.stats.health*.25)
                    return stop(Status::abort,"next attack and escape reserve");
                if(!me.can_attack())return stop(Status::handled,"basic allowance");
                if(!ready() || cambc::round_number()!=round)return stop(Status::cutoff,"attack round");
                me.attack(*victim);++plan.usedAttacks[unsigned(next)];++plan.attacksMade;plan.lastProgress=round;plan.lastActionRound=round;plan.pendingAttack=next;
                // Record the actual post-attack lower endpoint so a later reset
                // cannot hide behind the pre-attack full-health bound.
                if(ready() && cambc::round_number()==round) {
                    std::uint32_t state{};const auto code=cambc::detail::sdk_camp_state(plan.members[unsigned(next)].native,&state);
                    if(code==std::int32_t(cambc::error_code::api_not_in_vision) || code==std::int32_t(cambc::error_code::api_not_found))
                        return stop(Status::abort,"attack endpoint unavailable");
                    cambc::detail::check(code);if(state>2)throw cambc::error{cambc::error_code::api_value_out_of_range,"sortie attack camp state"};
                    if(!state){plan.members[unsigned(next)].lastHealth=0;plan.pendingAttack=-1;}
                    else {
                        cambc::detail::camp_state endpoint{};const auto result=cambc::detail::sdk_camp(plan.members[unsigned(next)].native,&endpoint);
                        if(result==std::int32_t(cambc::error_code::api_not_in_vision) || result==std::int32_t(cambc::error_code::api_not_found))
                            return stop(Status::abort,"attack health unavailable");
                        cambc::detail::check(result);
                        if(!model::scalar(endpoint.health) || endpoint.health<=0 || endpoint.health>=plan.members[unsigned(next)].lastHealth)
                            return stop(Status::abort,"attack made no observed progress");
                        plan.members[unsigned(next)].lastHealth=endpoint.health;plan.pendingAttack=-1;
                    }
                }
                return stop(Status::handled,"canonical basic attack",true);
            }
        }
    }
    if(plan.phase==Phase::escape) {
        if(me.position()!=plan.escapePoint) {
            if(plan.escapeAt>=plan.escapeCount || me.position()!=plan.escapePath[plan.escapeAt-1])return stop(Status::abort,"escape displacement");
            double incoming=0;for(unsigned i=0;i<plan.memberCount;++i)if(plan.members[i].lastHealth>0)incoming+=plan.members[i].incoming;
            if(me.health()-incoming*plan.stats.campMitigation<plan.stats.health*.25)return stop(Status::abort,"escape reserve");
            const auto result=move(plan.escapePath[plan.escapeAt],"escape");if(result.progress)++plan.escapeAt;return result;
        }
        for(unsigned i=0;i<plan.memberCount;++i)if(plan.members[i].lastHealth>0 && plan.members[i].incoming>0 &&
            distance(me.position(),plan.members[i].spawn)<=plan.members[i].range)return stop(Status::abort,"unsafe recall geometry");
        for(const auto& camp:camps)if(camp.is_alive() && camp.is_awake() && distance(me.position(),camp.position())<=camp.attack_range()+1)
            return stop(Status::abort,"unsafe recall current");
        if(!ready() || cambc::round_number()!=round)return stop(Status::cutoff,"recall round");
        me.recall();plan.recallRound=round;plan.phase=Phase::recall;plan.lastProgress=round;plan.lastActionRound=round;
        return stop(Status::handled,"recall started",true);
    }
    if(plan.phase==Phase::capture) {
        if((group->spawns&cambc::vision::visible_tiles()).is_empty())return stop(Status::abort,"capture visibility");
        std::uint32_t lifecycle{};const auto code=cambc::detail::sdk_camp_group_state(plan.nativeGroup,&lifecycle);
        if(code==std::int32_t(cambc::error_code::api_not_in_vision) || code==std::int32_t(cambc::error_code::api_not_found))return stop(Status::abort,"capture lifecycle unavailable");
        cambc::detail::check(code);if(lifecycle>3)throw cambc::error{cambc::error_code::api_value_out_of_range,"sortie capture state"};
        if(lifecycle==3) {
            if(me.position()!=plan.captureTile || plan.lastHoldRound+1!=round || me.gem_count()!=unsigned(plan.input.gems))return stop(Status::abort,"capture witness gap/split");
            if(!ready() || cambc::round_number()!=round)return stop(Status::cutoff,"capture witness round");
            plan.phase=Phase::bank;plan.lastProgress=round;captured=true;
        } else {
            if(lifecycle!=2)return stop(Status::abort,"capture lifecycle");
            if(me.position()!=plan.captureTile) {
                if(plan.captureAt>=plan.captureCount || me.position()!=plan.capturePath[plan.captureAt-1])return stop(Status::abort,"capture path");
                const auto result=move(plan.capturePath[plan.captureAt],"capture approach");
                if(result.progress){++plan.captureAt;if(me.position()==plan.captureTile)plan.lastHoldRound=round;}return result;
            }
            plan.lastHoldRound=round;return stop(Status::handled,"capture hold");
        }
    }
    if(plan.phase==Phase::bank) {
        if(me.gem_count()!=unsigned(plan.input.gems))return stop(Status::abort,"captured bag changed");
        if(me.position()!=plan.altarApproach) {
            if(plan.bankAt>=plan.bankCount || me.position()!=plan.bankPath[plan.bankAt-1])return stop(Status::abort,"bank path");
            const auto result=move(plan.bankPath[plan.bankAt],"bank approach");if(result.progress)++plan.bankAt;return result;
        }
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))if(lock.position()==plan.altarTile)return stop(Status::abort,"altar lock changed");
        if(!ready() || cambc::round_number()!=round)return stop(Status::cutoff,"tribute round");
        me.submit_gems(plan.altarTile);plan.phase=Phase::conversion_wait;plan.lastProgress=round;plan.lastActionRound=round;
        return stop(Status::ordinary_handoff,"shared physical conversion witness",true);
    }
    if(plan.phase==Phase::conversion_wait) {
        if(me.is_submitting_gems())return stop(Status::ordinary_handoff,"tribute channel");
        return stop(Status::abort,"conversion requires shared witness");
    }
    return stop(Status::handled,"phase transition");
}
} // namespace plan3_runtime::sortie
