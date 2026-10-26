#pragma once
#include "capital.h"
#include "executor.h"
#include "two-trip-observation.h"

namespace plan3_runtime::two_trip {
namespace model=plan3_two_trip;
namespace observed=plan3_two_trip::observation;
inline constexpr unsigned maximum_leg=128,maximum_escape=8,maximum_tiles=65536;
enum class Status { pending,ready,unsupported,unknown,cutoff,handled,abort,ordinary_handoff };
enum class Phase { approach,prefix,escape,recall,healing,returning,remaining,capture,bank,conversion_wait,handoff=conversion_wait };

// One fixed-root field, with the SAME work tickets as Campaign's StaticPaths.
// Allocation/export is separate from the 2x256 tile-work bound, as in StaticPaths.
class RouteField {
    int width_=0,height_=0,root_=-1,prepared_=0;
    std::vector<std::uint8_t> walls_;
    std::unique_ptr<int[]> distance_,parent_;
    std::vector<int> queue_;
    std::size_t head_=0;
    bool complete_=false;
public:
    RouteField(const cambc::bitboard& walls,cambc::vec2 root) {
        width_=cambc::map::width();height_=cambc::map::height();root_=root.y*width_+root.x;
        const int n=width_*height_;walls_=walls.export_linear_buffer();
        // The executable SDK vector overload allocates ceil(tiles/8) packed
        // bytes; its one-byte-per-tile @param prose is contradictory.
        if(walls_.size()!=(std::size_t(n)+7)/8)throw std::runtime_error("two-trip wall buffer ABI");
        distance_.reset(new int[n]);parent_.reset(new int[n]);queue_.reserve(unsigned(n));
    }
    bool ready() const {return complete_;}
    int key(cambc::vec2 p) const {return p.y*width_+p.x;}
    cambc::vec2 point(int key) const {return {key%width_,key/width_};}
    template<class Ready> void advance(StaticPaths& shared,Ready&& enough) {
        const int n=width_*height_;
        while(!complete_ && enough() && shared.take_work(65000)) {
            if(prepared_<n) {
                for(unsigned work=0;prepared_<n && work<256;++work) {
                    if(work%16==0 && !enough())return;
                    distance_[prepared_]=parent_[prepared_]=-1;++prepared_;
                }
                if(prepared_==n) {distance_[root_]=0;parent_[root_]=root_;queue_.push_back(root_);}
                continue;
            }
            for(unsigned work=0;head_<queue_.size() && work<256;++work) {
                if(work%16==0 && !enough())return;
                const int at=queue_[head_++],x=at%width_,y=at/width_;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    const int nx=x+dx,ny=y+dy;
                    if((!dx&&!dy) || nx<0 || ny<0 || nx>=width_ || ny>=height_)continue;
                    const int next=ny*width_+nx;
                    if(distance_[next]<0 && !((walls_[unsigned(next)/8]>>(unsigned(next)%8))&1u)) {
                        distance_[next]=distance_[at]+1;parent_[next]=at;queue_.push_back(next);
                    }
                }
            }
            complete_=head_==queue_.size();
        }
    }
    int distance(cambc::vec2 p) const {
        if(!complete_)return StaticPaths::unknown;
        if(p.x<0 || p.y<0 || p.x>=width_ || p.y>=height_)return StaticPaths::unreachable;
        return distance_[key(p)];
    }
    std::optional<cambc::vec2> toward_origin(cambc::vec2 p) const {
        if(distance(p)<0)return std::nullopt;return point(parent_[key(p)]);
    }
    template<std::size_t N>
    bool from_origin(cambc::vec2 end,std::array<cambc::vec2,N>& path,unsigned& count) const {
        const int d=distance(end);if(d<0 || unsigned(d)>=N)return false;
        count=unsigned(d)+1;int at=key(end);
        for(int i=d;i>=0;--i) {path[unsigned(i)]=point(at);at=parent_[at];}
        return true;
    }
};
using Inventory=std::array<unsigned,static_cast<unsigned>(cambc::item_type::count)>;
struct Plan {
    Phase phase=Phase::approach;
    int groupKey=-1,firstMemberKey=-1,altarIndex=-1;
    std::uint32_t nativeGroup=0,firstMemberId=0;
    cambc::vec2 decisionOrigin{},origin{},escapePoint{},captureTile{},altarTile{},altarApproach{};
    std::shared_ptr<RouteField> route;
    std::array<cambc::vec2,maximum_escape+1> escapePath{};
    std::array<cambc::vec2,maximum_leg+1> capturePath{},bankPath{};
    unsigned escapeCount=0,escapeAt=1,captureCount=0,captureAt=1,bankCount=0,bankAt=1,lastHoldRound=0;
    observed::Snapshot initial;
    Inventory inventory{};
    double maximumHealth=0,strength=0,focus=0,campDamage=1,campMitigation=1;
    unsigned preparedRound=0,lastRound=0,lastProgress=0,prefixRound=0,recallRound=0;
    int respawnTicks=0,recallTicks=0,deadline=0,attackRange=0;
    std::array<int,model::maximum_members> ranges{};
    std::array<double,model::maximum_members> lastMemberHealth{};
    bool started=false,prefixAttacked=false,prefixWitness=false;
};
struct Candidate {
    plan3::JobForecast job;
    Plan plan;
    model::Forecast accounting;
    observed::Snapshot source;
};
struct PrepareResult {
    Status status=Status::unknown;
    const char* reason="unknown";
    std::unique_ptr<Candidate> candidate;
};
struct StepResult {
    Status status=Status::pending;
    const char* reason="pending";
    bool progress=false;
    bool captured=false,banking=false;
};
inline int distance(cambc::vec2 a,cambc::vec2 b) {return int(a.chebyshev_distance(b));}
template<class Ready> std::optional<Inventory> inventory(Ready&& ready) {
    if(!ready())return std::nullopt;const auto counts=cambc::self().item_count();Inventory out{};
    for(unsigned i=0;i<out.size();++i) {
        if(i%8==0 && !ready())return std::nullopt;
        out[i]=counts[cambc::item_type{static_cast<cambc::item_type::enum_type>(i)}];
    }
    return out;
}
inline bool is_target(const Plan& plan,const cambc::camp& camp) {
    const auto id=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camp));
    for(unsigned i=0;i<plan.initial.memberCount;++i)if(plan.initial.nativeMemberIds[i]==id)return true;
    return false;
}
inline bool same_members(const observed::Snapshot& a,const observed::Snapshot& b) {
    if(a.groupKey!=b.groupKey || a.nativeGroup!=b.nativeGroup || a.groupGems!=b.groupGems || a.memberCount!=b.memberCount)return false;
    for(unsigned i=0;i<a.memberCount;++i)
        if(a.nativeMemberIds[i]!=b.nativeMemberIds[i] || a.members[i].key!=b.members[i].key)return false;
    return true;
}

class Builder {
    std::optional<cambc::bitboard> walls_;
    std::size_t wallCursor_=0,groups_=0;
    std::shared_ptr<RouteField> route_;
    cambc::vec2 origin_{};
    int groupKey_=-1;
public:
    void reset_preparation() {route_.reset();groupKey_=-1;}
    template<class Ready> void advance_preparation(StaticPaths& shared,Ready&& ready) {
        if(route_)route_->advance(shared,ready);
    }
    template<class Ready>
    PrepareResult prepare(const PersonalInformation& personal,std::size_t groupIndex,
                          const EquipmentStats& stats,StaticPaths& shared,Ready&& ready) {
        const auto fail=[](Status s,const char* why) {return PrepareResult{s,why,nullptr};};
        if(!ready())return fail(Status::cutoff,"fuel");
        auto& me=cambc::self();const int width=cambc::map::width(),height=cambc::map::height();
        if(width<=0 || height<=0 || std::int64_t(width)*height>maximum_tiles ||
           !personal.initialization_complete || groupIndex>=personal.groups.size() ||
           me.champion_name()!=cambc::champion_type::tavi || !me.is_alive() || me.gem_count() ||
           me.is_recalling() || me.is_submitting_gems() || me.is_teleporting())return fail(Status::unsupported,"actor/domain");
        if(!model::scalar(stats.health) || !model::scalar(stats.strength) || !model::scalar(stats.focus) ||
           !model::scalar(stats.campDamage) || !model::scalar(stats.campMitigation) || stats.campDamage<=0 ||
           !observed::close(me.max_health(),stats.health) || !observed::close(me.strength(),stats.strength) ||
           !observed::close(me.focus(),stats.focus))return fail(Status::unsupported,"stats");
        const auto& group=personal.groups[groupIndex];
        if(group.max_respawn_timer<=0 || group.gem_value<=0 || personal.spawn_positions.empty() ||
           personal.spawn_positions.size()>maximum_leg)return fail(Status::unsupported,"target/recovery");
        if(!cambc::vision::champions(cambc::team::enemy).empty())return fail(Status::unsupported,"enemy");
        if(!walls_ || groups_!=personal.groups.size()) {
            walls_=personal.terrain;wallCursor_=0;groups_=personal.groups.size();reset_preparation();
        }
        unsigned work=0;
        while(wallCursor_<personal.groups.size() && work++<8) {
            if(!ready())return fail(Status::cutoff,"public walls");
            *walls_|=personal.groups[wallCursor_++].spawns;
        }
        if(wallCursor_<personal.groups.size())return fail(Status::pending,"public walls");
        if(groupKey_!=group.key)reset_preparation();
        if(!route_) {
            const auto snapshot=observed::read(group,cambc::round_number(),ready);
            if(!snapshot.snapshot)return fail(snapshot.state==observed::State::cutoff?Status::cutoff:Status::unknown,"snapshot");
            const int range=me.attack_range();if(range<1 || range>4)return fail(Status::unsupported,"attack range");
            const auto current=me.position(),first=group.group.camps().front().spawn_position();
            const auto visible=cambc::vision::visible_tiles();bool found=false;int best=0,bestKey=0;
            for(int dy=-range;dy<=range;++dy)for(int dx=-range;dx<=range;++dx) {
                if(!ready())return fail(Status::cutoff,"origin");
                const cambc::vec2 p{first.x+dx,first.y+dy};
                if(p.x<0 || p.y<0 || p.x>=width || p.y>=height || !visible.is_set(p) || walls_->is_set(p))continue;
                bool fits=true;
                for(unsigned i=0;i<snapshot.snapshot->memberCount;++i)if(snapshot.snapshot->members[i].state==model::MemberState::alive &&
                    distance(p,group.group.camps()[i].spawn_position())>range)fits=false;
                const int d=distance(current,p),key=p.y*width+p.x;
                if(fits && (!found || std::pair{d,key}<std::pair{best,bestKey})) {found=true;best=d;bestKey=key;origin_=p;}
            }
            if(!found)return fail(Status::unsupported,"stationary origin");
            groupKey_=group.key;route_=std::make_shared<RouteField>(*walls_,origin_);
        }
        route_->advance(shared,ready);if(!route_->ready())return fail(Status::pending,"route field");
        // Everything below is fresh THIS callback. No live score survives the
        // multi-callback geometry preparation above.
        const unsigned round=cambc::round_number();const auto read=observed::read(group,round,ready);
        if(!read.snapshot)return fail(read.state==observed::State::cutoff?Status::cutoff:Status::unknown,"fresh snapshot");
        const auto& snapshot=*read.snapshot;const auto equipment=inventory(ready);
        if(!equipment)return fail(Status::cutoff,"inventory");
        if(!ready())return fail(Status::cutoff,"origin visibility");
        const auto originVision=me.visible_tiles_from(origin_);
        for(const auto& camp:group.group.camps())if(!originVision.is_set(camp.spawn_position()))return fail(Status::unsupported,"origin visibility");
        if(!me.is_alive() || me.gem_count() || !observed::close(me.max_health(),stats.health) ||
           !observed::close(me.strength(),stats.strength) || !observed::close(me.focus(),stats.focus))return fail(Status::unknown,"changed actor");
        for(const auto& camp:cambc::vision::camps()) {
            if(!ready())return fail(Status::cutoff,"observed danger");
            const auto id=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camp));bool target=false;
            for(unsigned i=0;i<snapshot.memberCount;++i)target|=id==snapshot.nativeMemberIds[i];
            if(!target && camp.is_alive() && camp.is_awake() && camp.strength()>0)return fail(Status::unsupported,"observed other danger");
        }
        const auto decisionOrigin=me.position();const int approach=route_->distance(decisionOrigin);
        if(approach<0 || approach>int(maximum_leg))return fail(Status::unsupported,"approach route");
        // The fixed paths/member arrays belong to the candidate on the heap,
        // rather than occupying prepare() and its returned value on WASI's stack.
        auto owned=std::make_unique<Candidate>();auto& plan=owned->plan;
        plan.route=route_;plan.decisionOrigin=decisionOrigin;plan.origin=origin_;plan.groupKey=group.key;plan.nativeGroup=snapshot.nativeGroup;
        plan.initial=snapshot;plan.inventory=*equipment;plan.maximumHealth=stats.health;plan.strength=stats.strength;
        plan.focus=stats.focus;plan.campDamage=stats.campDamage;plan.campMitigation=stats.campMitigation;plan.preparedRound=round;
        plan.respawnTicks=group.max_respawn_timer;plan.recallTicks=stats.recall;
        plan.attackRange=me.attack_range();
        int first=-1;unsigned living=0;std::array<int,model::maximum_members> ranges{};
        for(unsigned i=0;i<snapshot.memberCount;++i) {
            if(!ready())return fail(Status::cutoff,"member geometry");
            ranges[i]=group.group.camps()[i].attack_range();
            plan.ranges[i]=ranges[i];
            plan.lastMemberHealth[i]=snapshot.members[i].health;
            if(ranges[i]<0 || ranges[i]>16)return fail(Status::unsupported,"camp range");
            if(snapshot.members[i].state==model::MemberState::alive) {
                ++living;if(snapshot.memberStates[i]!=2 || distance(origin_,group.group.camps()[i].spawn_position())>me.attack_range())
                    return fail(Status::unsupported,"awake/outside stationary range");
                if(first<0 || snapshot.members[i].key<snapshot.members[unsigned(first)].key)first=int(i);
            }
        }
        if(living<2)return fail(Status::unsupported,"not two trip");
        plan.firstMemberKey=snapshot.members[unsigned(first)].key;plan.firstMemberId=snapshot.nativeMemberIds[unsigned(first)];
        // One deterministic nearest safe escape endpoint, not a search over
        // combat partitions. All source geometry is immutable public data.
        bool safeFound=false;int escapeDistance=0,escapeKey=0;
        for(int dy=-int(maximum_escape);dy<=int(maximum_escape);++dy)for(int dx=-int(maximum_escape);dx<=int(maximum_escape);++dx) {
            if(!ready())return fail(Status::cutoff,"escape geometry");
            const cambc::vec2 p{origin_.x+dx,origin_.y+dy};const int d=route_->distance(p);
            if(d<1 || d>int(maximum_escape))continue;bool safe=true;
            for(unsigned i=0;i<snapshot.memberCount;++i)if(int(i)!=first && snapshot.members[i].state==model::MemberState::alive &&
                distance(p,group.group.camps()[i].spawn_position())<=ranges[i]+1)safe=false;
            const int key=p.y*width+p.x;
            if(safe && (!safeFound || std::pair{d,key}<std::pair{escapeDistance,escapeKey})) {
                safeFound=true;escapeDistance=d;escapeKey=key;plan.escapePoint=p;
            }
        }
        if(!safeFound || !route_->from_origin(plan.escapePoint,plan.escapePath,plan.escapeCount))return fail(Status::unsupported,"escape route");
        int returnDistance=0;
        for(const auto p:personal.spawn_positions) {
            if(!ready())return fail(Status::cutoff,"spawn routes");const int d=route_->distance(p);
            if(d<0 || d>int(maximum_leg))return fail(Status::unsupported,"all spawn returns");returnDistance=std::max(returnDistance,d);
        }
        plan.captureTile={group.key%width,group.key/width};bool captureFound=false;int captureDistance=0,captureKey=0;
        cambc::vec2 captureNeighbor;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            if(!dx&&!dy)continue;const cambc::vec2 p{plan.captureTile.x+dx,plan.captureTile.y+dy};
            const int d=route_->distance(p);if(d<0 || d>=int(maximum_leg))continue;const int key=p.y*width+p.x;
            if(!captureFound || std::pair{d,key}<std::pair{captureDistance,captureKey}) {captureFound=true;captureDistance=d;captureKey=key;captureNeighbor=p;}
        }
        if(!captureFound || !route_->from_origin(captureNeighbor,plan.capturePath,plan.captureCount) ||
           plan.captureCount>=plan.capturePath.size())return fail(Status::unsupported,"capture route");
        plan.capturePath[plan.captureCount++]=plan.captureTile;++captureDistance;
        bool altarFound=false;int altarDistance=0,altarKey=0;
        for(unsigned a=0;a<personal.altars.size();++a)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            if(!ready())return fail(Status::cutoff,"altar route");if(!dx&&!dy)continue;
            const auto t=personal.altars[a];const cambc::vec2 p{t.x+dx,t.y+dy};const int d=route_->distance(p);
            if(d<0 || d+captureDistance>int(maximum_leg))continue;const int key=p.y*width+p.x;
            if(!altarFound || std::tuple{d,t.y*width+t.x,key}<std::tuple{altarDistance,altarKey,plan.altarApproach.y*width+plan.altarApproach.x}) {
                altarFound=true;altarDistance=d;altarKey=t.y*width+t.x;plan.altarIndex=int(a);plan.altarTile=t;plan.altarApproach=p;
            }
        }
        if(!altarFound)return fail(Status::unsupported,"altar route");
        for(unsigned i=plan.captureCount;i>0;--i)plan.bankPath[plan.bankCount++]=plan.capturePath[i-1];
        std::array<cambc::vec2,maximum_leg+1> altarPath{};unsigned altarCount=0;
        if(!route_->from_origin(plan.altarApproach,altarPath,altarCount))return fail(Status::unsupported,"altar path");
        for(unsigned i=1;i<altarCount;++i)plan.bankPath[plan.bankCount++]=altarPath[i];
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))if(lock.position()==plan.altarTile)return fail(Status::unsupported,"locked altar");
        model::Input input;input.round=int(round);input.roundLimit=int(std::min<std::int64_t>(cambc::constants::max_rounds,std::int64_t(round)+128));
        input.observationRound=int(round);input.groupKey=group.key;input.memberCount=snapshot.memberCount;input.members=snapshot.members;
        input.actorKnown=input.groupGemsKnown=true;input.groupGems=snapshot.groupGems;
        input.health=me.health();input.maximumHealth=stats.health;input.strength=stats.strength;input.focus=stats.focus;
        input.campDamage=stats.campDamage;input.campMitigation=stats.campMitigation;
        for(auto& leg:input.legs) {leg.turns=0;leg.healthLoss=0;leg.supported=true;}
        input.legs[model::outward].turns=approach;input.legs[model::escape].turns=escapeDistance;
        input.legs[model::return_to_group].turns=returnDistance;input.legs[model::capture_approach].turns=captureDistance;
        input.legs[model::altar].turns=captureDistance+altarDistance;
        input.recallTicks=stats.recall;input.captureTicks=cambc::constants::capture_time;input.spawnHealing=cambc::constants::spawn_healing;
        input.tributeMultiplier=cambc::constants::submit_gem_duration_multiplier;input.tributeBase=cambc::constants::submit_gem_base_duration;
        input.tributeMinimum=cambc::constants::submit_gem_minimum_duration;input.safeRecall=true;
        // Forecast assumptions, never observations/certificates. Execution
        // revalidates and aborts on observed interference or missed deadlines.
        input.quiet={true,true,true,true,true};const auto calculation=model::evaluate(input);
        if(!calculation.forecast)return fail(Status::unsupported,"complete two trip not admitted");
        const auto& f=*calculation.forecast;
        if(f.oneTripAdmitted)return fail(Status::unsupported,"ordinary clear already admitted");
        if(escapeDistance+f.recallTurns+f.healingTurns+returnDistance>=group.max_respawn_timer ||
           f.recallTurns+f.healingTurns<cambc::constants::camp_sleep_time)return fail(Status::unsupported,"epoch/sleep window");
        plan.deadline=input.roundLimit;
        auto& job=owned->job;job.id={plan3::JobKind::cycle,group.key,-1,altarKey,-1};
        job.campGems=group.gem_value;job.campGold=group.gold_value;job.travelPlusClear=approach+f.firstClearTurns+f.secondClearTurns;
        const auto finalHealing=model::renewal_healing_turns(stats.health,f.terminalHealth,cambc::constants::spawn_healing);
        if(!finalHealing || std::int64_t(f.recallTurns)+*finalHealing+returnDistance+approach>INT_MAX)
            return fail(Status::unsupported,"final recovery");
        // Internal return_to_group ends at Q. FINAL renewal must return to the
        // original decision p: worst spawn->Q plus the fixed Q->p path.
        job.projectedRenewalAndReturnTime=f.recallTurns+*finalHealing+returnDistance+approach;
        plan3::Outcome nominal;nominal.deltaS=f.deltaS;nominal.tau=f.tau;
        nominal.healthLoss=f.firstClearLoss+f.escapeLoss+f.secondClearLoss;
        nominal.terminal.health=f.terminalHealth;nominal.terminal.maximumHealth=stats.health;nominal.terminal.bag=0;
        nominal.terminal.position={plan.altarApproach.y*width+plan.altarApproach.x};nominal.terminal.equipment=stats.equipment;
        nominal.terminal.preparationTurns=job.projectedRenewalAndReturnTime;
        nominal.terminal.loopSupportUnavailable=true; // No invented renewal production.
        nominal.conversions.push_back({f.tau,f.deltaS});job.outcomes.push_back(std::move(nominal));
        if(!ready() || cambc::round_number()!=round)return fail(Status::cutoff,"publication fence");
        owned->accounting=f;owned->source=snapshot;
        return {Status::ready,"assumed quiet nominal only",std::move(owned)};
    }
};

template<class Ready>
StepResult step(Plan& plan,Executor& executor,const PersonalInformation& personal,StaticPaths&,
                const std::vector<cambc::camp>& camps,const std::vector<cambc::champion>& enemies,Ready&& ready) {
    bool capturedNow=false;
    const auto result=[&](Status s,const char* why,bool progress=false) {
        return StepResult{s,why,progress,capturedNow,plan.phase==Phase::bank || plan.phase==Phase::conversion_wait};
    };
    if(!ready())return result(Status::cutoff,"fuel");auto& me=cambc::self();const auto round=cambc::round_number();
    const bool banking=plan.phase==Phase::bank || plan.phase==Phase::conversion_wait;
    if(!me.is_alive() || !model::scalar(me.health()) || me.health()<=0 || (!banking && me.gem_count() && plan.phase!=Phase::capture) || me.is_teleporting() ||
       (me.is_recalling() && plan.phase!=Phase::recall) || (me.is_submitting_gems() && !banking))return result(Status::abort,"actor/death/channel/old bag");
    if(me.buffs().has_stun())return result(Status::abort,"stunned episode interrupted");
    if(round>=unsigned(plan.deadline))return result(Status::abort,"complete job deadline");
    if(plan.started && (round<plan.lastRound || std::uint64_t(round)>std::uint64_t(plan.lastRound)+1))return result(Status::abort,"owner observation gap");
    const auto gear=inventory(ready);if(!gear)return result(Status::cutoff,"gear fuel");
    if(*gear!=plan.inventory || !observed::close(me.max_health(),plan.maximumHealth) ||
       !observed::close(me.strength(),plan.strength) || !observed::close(me.focus(),plan.focus) ||
       me.attack_range()!=plan.attackRange)return result(Status::abort,"gear/stats changed");
    if(!enemies.empty())return result(Status::abort,"enemy interference");
    if(plan.prefixWitness && (plan.phase==Phase::escape || plan.phase==Phase::recall || plan.phase==Phase::healing || plan.phase==Phase::returning) &&
       round-plan.prefixRound>=unsigned(plan.respawnTicks))return result(Status::abort,"epoch window");
    if(!plan.started) {plan.started=true;plan.lastProgress=round;}
    plan.lastRound=round;
    if(round-plan.lastProgress>=8 && plan.phase!=Phase::recall && plan.phase!=Phase::healing)return result(Status::abort,"no progress");
    const GroupDesign* group=nullptr;
    for(const auto& g:personal.groups) {if(!ready())return result(Status::cutoff,"group lookup");if(g.key==plan.groupKey) {group=&g;break;}}
    if(!group)return result(Status::abort,"group missing");
    for(const auto& ally:cambc::vision::champions(cambc::team::ally))if(ally!=me && group->spawns.is_set(ally.position()))return result(Status::abort,"shared capture");
    for(const auto& camp:camps)if(!is_target(plan,camp) && camp.is_alive() && camp.is_awake() &&
        distance(me.position(),camp.position())<=camp.attack_range()+1)return result(Status::abort,"other camp danger");
    const auto read=[&] {return observed::read(*group,round,ready);};
    const auto validate=[&](const observed::Snapshot& s,bool firstDead) {
        if(!same_members(s,plan.initial))return false;
        for(unsigned i=0;i<s.memberCount;++i) {
            const bool first=s.nativeMemberIds[i]==plan.firstMemberId;
            if(first) {
                if(firstDead && s.members[i].state!=model::MemberState::dead)return false;
                if(s.members[i].state==model::MemberState::alive &&
                   !observed::close(s.members[i].incoming,plan.initial.members[i].incoming))return false;
            }
            else if(s.members[i].state!=plan.initial.members[i].state || !observed::close(s.members[i].health,plan.initial.members[i].health) ||
                    !observed::close(s.members[i].incoming,plan.initial.members[i].incoming))return false;
        }
        return true;
    };
    if(plan.phase==Phase::approach || plan.phase==Phase::prefix) {
        const auto snapshot=read();if(!snapshot.snapshot)return result(snapshot.state==observed::State::cutoff?Status::cutoff:Status::abort,"lost member view");
        if(!validate(*snapshot.snapshot,false))return result(Status::abort,"members changed");
        unsigned first=0;while(first<snapshot.snapshot->memberCount && snapshot.snapshot->nativeMemberIds[first]!=plan.firstMemberId)++first;
        if(first==snapshot.snapshot->memberCount)return result(Status::abort,"first identity");
        if(plan.phase==Phase::approach) {
            for(unsigned i=0;i<snapshot.snapshot->memberCount;++i)if(snapshot.snapshot->members[i].state==model::MemberState::alive && snapshot.snapshot->memberStates[i]!=2)
                return result(Status::abort,"premature wake");
            if(me.position()!=plan.origin) {
                const auto next=plan.route->toward_origin(me.position());
                if(!next)return result(Status::abort,"approach route");
                const bool moved=executor.move_one(*next);if(moved)plan.lastProgress=round;return result(Status::handled,"approach",moved);
            }
            plan.phase=Phase::prefix;plan.lastProgress=round;
        }
        if(me.position()!=plan.origin)return result(Status::abort,"stationary origin changed");
        const auto& target=snapshot.snapshot->members[first];
        if(!plan.prefixAttacked && !observed::close(target.health,plan.initial.members[first].health))
            return result(Status::abort,"prefix changed before attack");
        if(target.state==model::MemberState::dead) {
            if(!plan.prefixAttacked)return result(Status::abort,"unwitnessed prefix");
            plan.prefixWitness=true;plan.prefixRound=round;plan.phase=Phase::escape;plan.lastProgress=round;
        } else {
            if(target.health>plan.lastMemberHealth[first] || me.health()<plan.maximumHealth*.25)return result(Status::abort,"prefix health");
            plan.lastMemberHealth[first]=target.health;
            double incoming=0;for(unsigned i=0;i<snapshot.snapshot->memberCount;++i)incoming+=snapshot.snapshot->members[i].incoming;
            model::Input combat;combat.strength=plan.strength;combat.focus=plan.focus;combat.campDamage=plan.campDamage;
            combat.campMitigation=plan.campMitigation;combat.maximumHealth=plan.maximumHealth;
            const auto safe=model::clear(target.health,incoming,combat,me.health());
            if(!safe || !safe->admitted)return result(Status::abort,"prefix no longer safe");
            const bool acted=executor.fight(group->group.camps()[first],false,true);
            plan.prefixAttacked|=acted;if(acted)plan.lastProgress=round;return result(Status::handled,"prefix",acted);
        }
    }
    if(plan.phase==Phase::escape) {
        if(me.health()<plan.maximumHealth*.25)return result(Status::abort,"escape health");
        if(me.position()!=plan.escapePoint) {
            if(plan.escapeAt>=plan.escapeCount)return result(Status::abort,"escape path");
            if(me.position()!=plan.escapePath[plan.escapeAt-1])return result(Status::abort,"escape displacement");
            double exposure=0;const auto next=plan.escapePath[plan.escapeAt];
            for(unsigned i=0;i<plan.initial.memberCount;++i)if(plan.initial.nativeMemberIds[i]!=plan.firstMemberId &&
                plan.initial.members[i].state==model::MemberState::alive && distance(next,group->group.camps()[i].spawn_position())<=plan.ranges[i])
                exposure+=plan.initial.members[i].incoming*plan.campMitigation;
            if(me.health()-exposure<plan.maximumHealth*.25)return result(Status::abort,"next escape exposure");
            const bool moved=executor.move_one(plan.escapePath[plan.escapeAt]);
            if(moved) {++plan.escapeAt;plan.lastProgress=round;}return result(Status::handled,"escape",moved);
        }
        for(const auto& camp:camps)if(camp.is_alive() && camp.is_awake() && distance(me.position(),camp.position())<=camp.attack_range()+1)
            return result(Status::abort,"recall unsafe");
        me.recall();plan.recallRound=round;plan.phase=Phase::recall;plan.lastProgress=round;
        return result(Status::handled,"recall started",true);
    }
    if(plan.phase==Phase::recall) {
        if(me.is_recalling())return result(Status::handled,"recalling");
        if(!personal.own_spawn.is_set(me.position()) || round<plan.recallRound+unsigned(std::max(1,plan.recallTicks)))return result(Status::abort,"recall interrupted");
        plan.phase=Phase::healing;plan.lastProgress=round;
    }
    if(plan.phase==Phase::healing) {
        if(!personal.own_spawn.is_set(me.position()))return result(Status::abort,"spawn recovery interrupted");
        if(me.health()<me.max_health())return result(Status::handled,"spawn healing");
        plan.phase=Phase::returning;plan.lastProgress=round;
    }
    if(plan.phase==Phase::returning) {
        if(me.position()!=plan.origin) {
            for(const auto& camp:camps)if(is_target(plan,camp) && camp.is_alive() && camp.is_awake())return result(Status::abort,"return target awake");
            const auto next=plan.route->toward_origin(me.position());if(!next)return result(Status::abort,"return route");
            const bool moved=executor.move_one(*next);if(moved)plan.lastProgress=round;return result(Status::handled,"returning",moved);
        }
        const auto snapshot=read();if(!snapshot.snapshot)return result(snapshot.state==observed::State::cutoff?Status::cutoff:Status::abort,"return visibility");
        if(!validate(*snapshot.snapshot,true))return result(Status::abort,"return epoch/members changed");
        plan.phase=Phase::remaining;plan.lastProgress=round;
    }
    if(plan.phase==Phase::remaining || plan.phase==Phase::capture) {
        const auto visible=cambc::vision::visible_tiles();
        if((visible&group->spawns).is_empty())return result(Status::abort,"capture visibility");
        const auto lifecycle=group->group.state();
        if(cambc::round_number()!=round)return result(Status::abort,"mixed round lifecycle");
        if(plan.phase==Phase::remaining) {
            if(me.position()!=plan.origin)return result(Status::abort,"remaining origin");
            if(std::holds_alternative<cambc::map::camp_group_alive>(lifecycle)) {
                const auto snapshot=read();if(!snapshot.snapshot)return result(snapshot.state==observed::State::cutoff?Status::cutoff:Status::abort,"remaining view");
                if(!same_members(*snapshot.snapshot,plan.initial))return result(Status::abort,"remaining identity");
                int target=-1;
                for(unsigned i=0;i<snapshot.snapshot->memberCount;++i) {
                    const auto& member=snapshot.snapshot->members[i];
                    if(snapshot.snapshot->nativeMemberIds[i]==plan.firstMemberId && member.state!=model::MemberState::dead)
                        return result(Status::abort,"prefix respawned");
                    if(member.health>plan.lastMemberHealth[i] || member.incoming>plan.initial.members[i].incoming)
                        return result(Status::abort,"remaining reset");
                    plan.lastMemberHealth[i]=member.health;
                    if(member.state==model::MemberState::alive && (target<0 || member.key<snapshot.snapshot->members[unsigned(target)].key))target=int(i);
                }
                if(target<0 || me.health()<plan.maximumHealth*.25)return result(Status::abort,"remaining health/lifecycle");
                double hp=0,incoming=0;for(unsigned i=0;i<snapshot.snapshot->memberCount;++i) {
                    hp+=snapshot.snapshot->members[i].health;incoming+=snapshot.snapshot->members[i].incoming;
                }
                model::Input combat;combat.strength=plan.strength;combat.focus=plan.focus;combat.campDamage=plan.campDamage;
                combat.campMitigation=plan.campMitigation;combat.maximumHealth=plan.maximumHealth;
                const auto safe=model::clear(hp,incoming,combat,me.health());
                if(!safe || !safe->admitted)return result(Status::abort,"remaining no longer safe");
                const bool acted=executor.fight(group->group.camps()[unsigned(target)],false,true);
                if(acted)plan.lastProgress=round;return result(Status::handled,"remaining clear",acted);
            }
            if(!std::holds_alternative<cambc::map::camp_group_reward_available>(lifecycle))return result(Status::abort,"remaining lifecycle changed");
            plan.phase=Phase::capture;plan.lastProgress=round;
        }
        if(std::holds_alternative<cambc::map::camp_group_respawning>(lifecycle)) {
            if(me.position()!=plan.captureTile || plan.lastHoldRound+1!=round || me.gem_count()!=unsigned(plan.initial.groupGems))
                return result(Status::abort,"capture witness gap/split");
            plan.phase=Phase::bank;plan.lastProgress=round;capturedNow=true;
        } else {
            if(!std::holds_alternative<cambc::map::camp_group_reward_available>(lifecycle))return result(Status::abort,"capture lifecycle changed");
            if(me.position()!=plan.captureTile) {
                if(plan.captureAt>=plan.captureCount || me.position()!=plan.capturePath[plan.captureAt-1])return result(Status::abort,"capture displacement");
                const bool moved=executor.move_one(plan.capturePath[plan.captureAt]);
                if(moved) {++plan.captureAt;plan.lastProgress=round;if(me.position()==plan.captureTile)plan.lastHoldRound=round;}
                return result(Status::handled,"capture approach",moved);
            }
            plan.lastHoldRound=round;return result(Status::handled,"capture hold");
        }
    }
    if(plan.phase==Phase::bank) {
        if(me.gem_count()!=unsigned(plan.initial.groupGems))return result(Status::abort,"captured bag changed");
        if(me.position()!=plan.altarApproach) {
            if(plan.bankAt>=plan.bankCount || me.position()!=plan.bankPath[plan.bankAt-1])return result(Status::abort,"bank displacement");
            const bool moved=executor.move_one(plan.bankPath[plan.bankAt]);
            if(moved) {++plan.bankAt;plan.lastProgress=round;}return result(Status::handled,"bank route",moved);
        }
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))if(lock.position()==plan.altarTile)return result(Status::abort,"altar changed");
        me.submit_gems(plan.altarTile);plan.phase=Phase::conversion_wait;plan.lastProgress=round;
        return result(Status::ordinary_handoff,"shared physical conversion witness",true);
    }
    if(plan.phase==Phase::conversion_wait) {
        if(me.is_submitting_gems())return result(Status::ordinary_handoff,"tribute channel");
        return result(Status::abort,"conversion requires shared witness");
    }
    return result(Status::pending,"phase");
}
} // namespace plan3_runtime::two_trip
