#pragma once
#include "two-trip-runtime.h"

namespace plan3_runtime::two_trip {
inline constexpr int service_limit=8192;

// Hypothetical full-public-group service, never a live member observation or an
// executable Plan. Only the private immutable route may survive a callback.
struct FreshService {
    unsigned preparedRound=0;
    int groupKey=-1,groupGems=0,regenMax=0,width=0,height=0,altarTile=-1;
    cambc::vec2 decisionOrigin{},combatOrigin{},escapePoint{},captureTile{},endpoint{};
    int maximumSpawnToQ=0,QToP=0,effectiveRecall=0,freshHealingTurns=0,Cfresh=0,Rfresh=0;
    double maximumHealth=0,spawnHealing=0;
    plan3::EquipmentRecord equipment;
    model::Forecast accounting;
    bool assumedQuiet=true;
};
struct FreshServiceResult {
    Status status=Status::unknown;
    const char* reason="unknown";
    std::optional<FreshService> service;
};
struct PeriodService : FreshService {
    plan3::JobId currentJob;
    cambc::vec2 currentEndpoint{};
    int Rcurrent=0,currentHealingTurns=0;
};
struct ServiceResult {
    Status status=Status::unknown;
    const char* reason="unknown";
    std::optional<PeriodService> service;
};

class FreshServiceBuilder {
    std::optional<cambc::bitboard> walls_;
    std::size_t wallCursor_=0,groups_=0;
    std::shared_ptr<RouteField> route_;
    int groupKey_=-1,range_=0;
    cambc::vec2 decision_{},origin_{};
public:
    void reset_preparation() {route_.reset();groupKey_=-1;}
    template<class Ready> void advance_preparation(StaticPaths& shared,Ready&& ready) {
        if(route_)route_->advance(shared,ready);
    }
    template<class Ready>
    FreshServiceResult prepare(const PersonalInformation& personal,std::size_t groupIndex,
            const EquipmentStats& stats,StaticPaths& shared,const plan3::InformationState& state,
            Ready&& ready,const Candidate* hint=nullptr) {
        const auto fail=[](Status status,const char* why) {return FreshServiceResult{status,why,std::nullopt};};
        if(state.actorOrdinal!=static_cast<int>(cambc::champion_type::tavi) || state.carriedGems ||
           !personal.initialization_complete || groupIndex>=personal.groups.size())return fail(Status::unsupported,"actor/domain");
        if(!ready())return fail(Status::cutoff,"fuel");
        auto& me=cambc::self();const int width=personal.width,height=cambc::map::height();
        const auto p=me.position();const int range=me.attack_range();
        if(width<=0 || height<=0 || std::int64_t(width)*height>maximum_tiles ||
           p.x<0 || p.y<0 || p.x>=width || p.y>=height || range<1 || range>4 ||
           state.round<0 || state.round>INT_MAX-service_limit || cambc::round_number()!=unsigned(state.round) ||
           state.position.tile!=p.y*width+p.x || !me.is_alive() || me.gem_count() ||
           me.is_recalling() || me.is_submitting_gems() || me.is_teleporting())return fail(Status::unsupported,"actor/round");
        if(!model::scalar(stats.health) || stats.health<=0 || !model::scalar(stats.strength) ||
           !model::scalar(stats.focus) || !model::scalar(stats.campDamage) || stats.campDamage<=0 ||
           !model::scalar(stats.campMitigation) || !observed::close(me.max_health(),stats.health) ||
           !observed::close(me.strength(),stats.strength) || !observed::close(me.focus(),stats.focus) ||
           stats.equipment.items!=state.equipment.items)return fail(Status::unsupported,"gear/stats");
        const auto& group=personal.groups[groupIndex];const auto camps=group.group.camps();
        if(camps.size()<2 || camps.size()>model::maximum_members || group.members.size()!=camps.size() ||
           group.spawn_positions.size()!=camps.size() || group.gem_value<1 || group.gem_value>1000000 ||
           group.max_respawn_timer<0 || group.max_respawn_timer>service_limit || personal.spawn_positions.empty() ||
           personal.spawn_positions.size()>maximum_leg)return fail(Status::unsupported,"public group");
        model::Input input;input.round=state.round;input.roundLimit=state.round+service_limit;
        input.observationRound=state.round;input.groupKey=group.key;input.memberCount=unsigned(camps.size());
        input.actorKnown=input.groupGemsKnown=true;input.groupGems=group.gem_value;
        input.health=input.maximumHealth=stats.health;input.strength=stats.strength;input.focus=stats.focus;
        input.campDamage=stats.campDamage;input.campMitigation=stats.campMitigation;
        std::array<cambc::vec2,model::maximum_members> positions{};
        std::array<std::uint32_t,model::maximum_members> ids{};
        std::array<int,model::maximum_members> ranges{};
        int first=-1,minimum=-1;
        for(unsigned i=0;i<input.memberCount;++i) {
            if(!ready())return fail(Status::cutoff,"public members");
            const auto at=camps[i].spawn_position();const auto id=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camps[i]));
            if(at.x<0 || at.y<0 || at.x>=width || at.y>=height)return fail(Status::unsupported,"member tile");
            const int key=at.y*width+at.x;positions[i]=at;ids[i]=id;ranges[i]=camps[i].attack_range();
            if(ranges[i]<0 || ranges[i]>16)return fail(Status::unsupported,"member range");
            for(unsigned j=0;j<i;++j)if(ids[j]==id || input.members[j].key==key)return fail(Status::unsupported,"duplicate member");
            const PublicCampMember* full=nullptr;unsigned matches=0,spawns=0;
            for(const auto& member:group.members)if(member.spawn==at) {full=&member;++matches;}
            for(const auto atSpawn:group.spawn_positions)spawns+=atSpawn==at;
            if(matches!=1 || spawns!=1 || !group.spawns.is_set(at) || !full ||
               !model::scalar(full->health) || full->health<=0 || !model::scalar(full->strength))return fail(Status::unsupported,"public identity/stats");
            // These are hypothetical healthy future members, INCLUDING current
            // dead members. No observation::Snapshot or current lifecycle is made.
            input.members[i]={key,state.round,model::MemberState::alive,full->health,full->strength};
            if(minimum<0 || key<minimum) {minimum=key;first=int(i);}
        }
        if(minimum!=group.key || first<0)return fail(Status::unsupported,"group key");
        if(!walls_ || groups_!=personal.groups.size()) {
            walls_=personal.terrain;wallCursor_=0;groups_=personal.groups.size();reset_preparation();
        }
        unsigned work=0;
        while(wallCursor_<personal.groups.size() && work++<8) {
            if(!ready())return fail(Status::cutoff,"public walls");
            *walls_|=personal.groups[wallCursor_++].spawns;
        }
        if(wallCursor_<personal.groups.size())return fail(Status::pending,"public walls");
        if(groupKey_!=group.key || decision_!=p || range_!=range)reset_preparation();
        if(!route_) {
            const auto fits=[&](cambc::vec2 q) {
                for(unsigned i=0;i<input.memberCount;++i)if(distance(q,positions[i])>range)return false;
                return true;
            };
            // Choose canonical fresh Q from ALL public members first. A current
            // Plan's visible/current-living origin is only a cache hint, never
            // an alternative definition of the healthy future service.
            bool found=false;int best=0,bestKey=0;const auto anchor=positions[unsigned(first)];
            for(int dy=-range;dy<=range;++dy)for(int dx=-range;dx<=range;++dx) {
                if(!ready())return fail(Status::cutoff,"fresh origin");
                const cambc::vec2 q{anchor.x+dx,anchor.y+dy};
                if(q.x<0 || q.y<0 || q.x>=width || q.y>=height || walls_->is_set(q) || !fits(q))continue;
                const int d=distance(p,q),key=q.y*width+q.x;
                if(!found || std::pair{d,key}<std::pair{best,bestKey}) {found=true;best=d;bestKey=key;origin_=q;}
            }
            if(!found)return fail(Status::unsupported,"all-member origin");
            if(hint && hint->plan.groupKey==group.key && hint->plan.preparedRound==unsigned(state.round) &&
               hint->plan.decisionOrigin==p && hint->plan.route && hint->plan.route->ready() &&
               hint->plan.attackRange==range && hint->plan.origin==origin_)route_=hint->plan.route;
            else route_=std::make_shared<RouteField>(*walls_,origin_);
            groupKey_=group.key;decision_=p;range_=range;
        }
        route_->advance(shared,ready);if(!route_->ready())return fail(Status::pending,"fresh route");
        if(!ready())return fail(Status::cutoff,"fresh visibility");
        const auto vision=me.visible_tiles_from(origin_);
        for(unsigned i=0;i<input.memberCount;++i)if(!vision.is_set(positions[i]))return fail(Status::unsupported,"all-member vision");
        const int approach=route_->distance(p);if(approach<0 || approach>service_limit)return fail(Status::unsupported,"decision route");
        int returnDistance=0;
        for(const auto spawn:personal.spawn_positions) {
            if(!ready())return fail(Status::cutoff,"all spawn routes");
            const int d=route_->distance(spawn);if(d<0 || d>service_limit)return fail(Status::unsupported,"spawn route");
            returnDistance=std::max(returnDistance,d);
        }
        bool escaped=false;int escapeDistance=0,escapeKey=0;cambc::vec2 escapePoint;
        for(int dy=-int(maximum_escape);dy<=int(maximum_escape);++dy)for(int dx=-int(maximum_escape);dx<=int(maximum_escape);++dx) {
            if(!ready())return fail(Status::cutoff,"fresh escape");
            const cambc::vec2 q{origin_.x+dx,origin_.y+dy};const int d=route_->distance(q);
            if(d<1 || d>int(maximum_escape))continue;bool safe=true;
            for(unsigned i=0;i<input.memberCount;++i)if(int(i)!=first && distance(q,positions[i])<=ranges[i]+1)safe=false;
            const int key=q.y*width+q.x;
            if(safe && (!escaped || std::pair{d,key}<std::pair{escapeDistance,escapeKey})) {
                escaped=true;escapeDistance=d;escapeKey=key;escapePoint=q;
            }
        }
        if(!escaped)return fail(Status::unsupported,"fresh escape route");
        const cambc::vec2 captureTile{group.key%width,group.key/width};bool captured=false;int captureDistance=0,captureKey=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            if(!dx&&!dy)continue;const cambc::vec2 q{captureTile.x+dx,captureTile.y+dy};const int d=route_->distance(q);
            if(d<0 || d>=service_limit)continue;const int key=q.y*width+q.x;
            if(!captured || std::pair{d,key}<std::pair{captureDistance,captureKey}) {captured=true;captureDistance=d;captureKey=key;}
        }
        if(!captured)return fail(Status::unsupported,"fresh capture route");
        ++captureDistance;bool altar=false;int altarDistance=0,altarKey=0,endpointKey=0;cambc::vec2 endpoint;
        for(const auto tile:personal.altars)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            if(!ready())return fail(Status::cutoff,"fresh altar");if(!dx&&!dy)continue;
            const cambc::vec2 q{tile.x+dx,tile.y+dy};const int d=route_->distance(q);
            if(d<0 || std::int64_t(d)+captureDistance>service_limit)continue;const int key=q.y*width+q.x,tileKey=tile.y*width+tile.x;
            if(!altar || std::tuple{d,tileKey,key}<std::tuple{altarDistance,altarKey,endpointKey}) {
                altar=true;altarDistance=d;altarKey=tileKey;endpointKey=key;endpoint=q;
            }
        }
        if(!altar)return fail(Status::unsupported,"fresh altar route");
        // Same explicit no-enemy-lock supported domain as the current macro.
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))
            if(lock.position()==cambc::vec2{altarKey%width,altarKey/width})return fail(Status::unsupported,"fresh altar lock");
        for(auto& leg:input.legs) {leg.turns=0;leg.healthLoss=0;leg.supported=true;}
        input.legs[model::outward].turns=approach;input.legs[model::escape].turns=escapeDistance;
        input.legs[model::return_to_group].turns=returnDistance;input.legs[model::capture_approach].turns=captureDistance;
        input.legs[model::altar].turns=captureDistance+altarDistance;
        input.recallTicks=stats.recall;input.captureTicks=cambc::constants::capture_time;input.spawnHealing=cambc::constants::spawn_healing;
        input.tributeMultiplier=cambc::constants::submit_gem_duration_multiplier;input.tributeBase=cambc::constants::submit_gem_base_duration;
        input.tributeMinimum=cambc::constants::submit_gem_minimum_duration;input.safeRecall=true;
        input.quiet={true,true,true,true,true}; // Declared future premises, never live evidence.
        const auto calculation=model::evaluate(input);
        if(!calculation.forecast)return fail(Status::unsupported,"fresh complete two trip not admitted");
        const auto& f=*calculation.forecast;
        if(std::int64_t(escapeDistance)+f.recallTurns+f.healingTurns+returnDistance>=group.max_respawn_timer ||
           std::int64_t(f.recallTurns)+f.healingTurns<cambc::constants::camp_sleep_time)return fail(Status::unsupported,"fresh epoch/sleep");
        const auto healing=model::renewal_healing_turns(stats.health,f.terminalHealth,cambc::constants::spawn_healing);
        const std::int64_t recovery=healing?std::int64_t(f.recallTurns)+*healing+returnDistance+approach:service_limit+1;
        if(!healing || f.tau<1 || f.tau>service_limit || recovery>service_limit ||
           std::int64_t(f.tau)+recovery>service_limit || std::int64_t(f.tau)+group.max_respawn_timer>service_limit)
            return fail(Status::unsupported,"fresh service bound");
        if(!ready() || cambc::round_number()!=unsigned(state.round))return fail(Status::cutoff,"fresh publication fence");
        FreshService out;out.preparedRound=unsigned(state.round);out.groupKey=group.key;out.groupGems=group.gem_value;
        out.regenMax=group.max_respawn_timer;out.width=width;out.height=height;out.altarTile=altarKey;out.decisionOrigin=p;out.combatOrigin=origin_;
        out.escapePoint=escapePoint;out.captureTile=captureTile;out.endpoint=endpoint;out.maximumSpawnToQ=returnDistance;out.QToP=approach;
        out.effectiveRecall=f.recallTurns;out.freshHealingTurns=*healing;out.Cfresh=f.tau;out.Rfresh=int(recovery);
        out.maximumHealth=stats.health;out.spawnHealing=cambc::constants::spawn_healing;out.equipment=stats.equipment;out.accounting=f;
        return {Status::ready,"assumed healthy fresh two-trip service",std::move(out)};
    }
};

// Pure callback-local binding: the current job remains unchanged on every
// outcome. The adapter may copy its corrected Rcurrent only after success.
inline ServiceResult bind_current(const FreshService& fresh,const plan3::JobForecast& job,const plan3::InformationState& state) {
    const auto fail=[](Status status,const char* why) {return ServiceResult{status,why,std::nullopt};};
    if(!fresh.assumedQuiet)return fail(Status::unknown,"fresh quiet premises");
    if(state.round<0 || unsigned(state.round)!=fresh.preparedRound || state.carriedGems ||
       fresh.width<=0 || fresh.height<=0 || std::int64_t(fresh.width)*fresh.height>maximum_tiles ||
       fresh.decisionOrigin.x<0 || fresh.decisionOrigin.y<0 || fresh.decisionOrigin.x>=fresh.width || fresh.decisionOrigin.y>=fresh.height ||
       state.position.tile!=fresh.decisionOrigin.y*fresh.width+fresh.decisionOrigin.x ||
       state.equipment.items!=fresh.equipment.items || !observed::close(state.maximumHealth,fresh.maximumHealth))
        return fail(Status::unknown,"current origin/gear/round");
    if(job.id.kind!=plan3::JobKind::cycle || job.id.firstGroup!=fresh.groupKey || job.id.secondGroup>=0 ||
       !plan3::admitted(state,job,false) || job.projectedRenewalAndReturnTime<0)return fail(Status::unsupported,"current admission");
    const auto* nominal=job.nominal();
    if(!nominal || nominal->tau<1 || nominal->tau>state.horizon() || nominal->deltaS!=fresh.groupGems ||
       !observed::close(nominal->terminal.maximumHealth,fresh.maximumHealth) ||
       !model::alive_after(nominal->terminal.health,fresh.maximumHealth) ||
       nominal->terminal.equipment.items!=fresh.equipment.items || nominal->terminal.bag ||
       nominal->terminal.position.tile<0 || nominal->terminal.position.tile>=fresh.width*fresh.height)
        return fail(Status::unsupported,"current endpoint/accounting");
    if(fresh.groupGems<1 || fresh.groupGems>1000000 || fresh.maximumSpawnToQ<0 || fresh.maximumSpawnToQ>service_limit ||
       fresh.QToP<0 || fresh.QToP>service_limit || fresh.effectiveRecall<1 || fresh.effectiveRecall>service_limit ||
       fresh.freshHealingTurns<0 || fresh.freshHealingTurns>service_limit)return fail(Status::unsupported,"fresh recovery domain");
    const auto healing=model::renewal_healing_turns(fresh.maximumHealth,nominal->terminal.health,fresh.spawnHealing);
    const std::int64_t recovery=healing?std::int64_t(fresh.effectiveRecall)+*healing+fresh.maximumSpawnToQ+fresh.QToP:service_limit+1;
    if(!healing || fresh.Cfresh<1 || fresh.Cfresh>service_limit || fresh.Rfresh<0 || fresh.Rfresh>service_limit ||
       fresh.regenMax<0 || fresh.regenMax>service_limit || recovery<0 || recovery>service_limit ||
       std::max({std::int64_t(nominal->tau),std::int64_t(fresh.Cfresh)+fresh.Rfresh,
                 std::int64_t(fresh.Cfresh)+recovery,std::int64_t(fresh.Cfresh)+fresh.regenMax})>service_limit ||
       std::int64_t(nominal->tau)+recovery>service_limit)return fail(Status::unsupported,"current service bound");
    PeriodService out;static_cast<FreshService&>(out)=fresh;out.currentJob=job.id;
    out.currentEndpoint={nominal->terminal.position.tile%fresh.width,nominal->terminal.position.tile/fresh.width};
    out.Rcurrent=int(recovery);out.currentHealingTurns=*healing;
    return {Status::ready,"assumed same-group current/fresh service",std::move(out)};
}
} // namespace plan3_runtime::two_trip
