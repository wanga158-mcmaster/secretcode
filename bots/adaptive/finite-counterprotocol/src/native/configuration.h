#pragma once
#include "distance.h"
#include "../policy.h"
#include "../../../common/src/coalition.h"
#include <bit>
namespace p8::finite_counterprotocol::native {
using namespace p8::native;
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
inline double focus_upper(double base,p6::Gems bag) {
    const double value=base+double(cambc::constants::passive_abilities::prism_focus_per_gem)*double(bag);
    return value+8.*std::numeric_limits<float>::epsilon()*std::max(1.,std::abs(value));
}
// Position is absent intentionally: every represented turn charges the full
// current visible rival attack plus its largest queried damaging cast. A buff
// whose attack amount is not exposed cannot enter that bound.
inline std::uint64_t rival_regime(bool& bounded) {
    bounded=true;std::uint64_t hash=1469598103934665603ULL;
    for(const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
        hash=hash_word(hash,std::uint64_t(sdk::identity(enemy))+1);
        for(const double value:{double(enemy.strength()),double(enemy.focus())}) {
            if(!std::isfinite(value)||value<0)bounded=false;hash=hash_word(hash,std::bit_cast<std::uint64_t>(value));
        }
        const auto buffs=enemy.buffs();if(buffs.has_enhanced_attack())bounded=false;
        hash=hash_word(hash,buffs.has_enhanced_attack());
        hash=hash_word(hash,sdk::equipment_identity(enemy));
        for(const auto& ability:enemy.abilities()) {
            // The shared single-attack bound has no second reset attack or
            // dash landing term. Keep those encounters unsupported here.
            if(ability.is_attack_reset()||ability.is_dash())bounded=false;
            hash=hash_word(hash,ability.is_attack_reset());hash=hash_word(hash,ability.is_dash());
            const bool represented=ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc());
            hash=hash_word(hash,represented);
            if(represented) {const double value=ability.damage_amount();if(!std::isfinite(value)||value<0)bounded=false;
                hash=hash_word(hash,std::bit_cast<std::uint64_t>(value));}
        }
    }
    return hash;
}
inline std::uint64_t physical_regime(int except,bool& bounded) {
    auto hash=rival_regime(bounded);
    for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake()&&group_id(camp.camp_group())!=except) {
        hash=hash_word(hash,std::uint64_t(sdk::identity(camp))+1);
        hash=hash_word(hash,std::bit_cast<std::uint64_t>(double(camp.strength())));
    }
    return hash;
}
inline bool independent_offer(Offer& out,const Solo& own,std::span<const Solo> models,const p6::Frame& frame) {
    const auto empty=std::make_unique<Offer>();out=*empty;out.plan=own.plan;out.schedule.target_id=own.plan.group;int prefix=0;bool self=false;
    for(const auto& model:models) {
        auto copy=std::make_unique<Solo>(model);if(!copy->feasible)return false;normalize(*copy,frame);
        self|=copy->plan.collector==frame.clock.self;
        if(!append(out.schedule,copy->model.schedule,0,prefix)||out.safety_count==p6::max_actors)return false;
        out.safety[out.safety_count++]=copy->model.certificate;prefix+=64;
    }
    return self&&queue_alternatives(out,models,frame);
}
inline ContextDeclaration declaration(const Layout& layout,const Inputs& in,p6::Gems payload) {
    ContextDeclaration d;d.kind=EpisodeKind::full_organization;d.require_supported_work=true;
    auto& k=d.key;const auto& state=in.own_state.value;const auto& plan=layout.collector;
    k.settings_id=in.settings_identity.value;k.actor_id=in.frame.clock.self;k.champion=int(in.frame.own.actor.type);k.role=1;
    k.source_group=plan.group;k.route_template=15;k.endpoint=plan.altar;k.threat_regime=in.threat_regime.value;
    k.route_hash=plan_route_hash(plan);k.return_route_hash=plan_return_hash(plan);k.participants=plan.crew;k.participant_count=2;
    std::sort(k.participants.begin(),k.participants.begin()+2);k.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};
    k.hp_band=health_band(state.health,state.max_health);
    const double focus=in.frame.own.actor.type==p6::Champion::prism?focus_upper(state.focus,payload):state.focus;
    d.envelope={{std::nextafter(0.,1.),state.max_health},{state.max_health,state.max_health},{state.strength,state.strength},
        {state.focus,focus},{state.growth,state.growth},{0,double(std::max(payload,state.bag))},0};return d;
}
inline bool field(Field& out,p6::Point at,const Routes& routes) {
    if(out.ready)return out.destination==at;if(cambc::fuel_remaining()<65000)return false;
    out.destination=at;cambc::bitboard visited;const int round=cambc::round_number();
    out.paths.compute(routes.terrain(),singleton(at),visited,256);
    out.ready=int(cambc::round_number())==round&&cambc::fuel_remaining()>22000;return out.ready;
}
inline int public_bank(const Routes& routes,int source) {
    int best=-1,cost=0;
    for(int b=0;b<int(routes.banks.size());++b) {
        const auto bank=routes.route(routes.sites[source].capture.destination,routes.banks[b].approach);
        const auto back=routes.route(routes.banks[b].approach.destination,routes.sites[source].approach);
        if(!route_valid(bank)||!route_valid(back))continue;const int here=walking(bank)+walking(back);
        if(best<0||here<cost||(here==cost&&sdk::altar_id(routes.banks[b].altar)<sdk::altar_id(routes.banks[best].altar))){best=b;cost=here;}
    }
    return best;
}
// Public immutable geometry. The second altar approach stays free for the
// rival-distance target; the escort occupies a third, distinct tile.
inline bool geometry(Layout& layout,Field& escort,Routes& routes,int source,int bank,int next,int next_bank,
                     const sdk::Snapshot& snapshot,RivalDistance& distances) {
    if(snapshot.draft.picks<3||source==next)return false;
    const auto& site=routes.sites[source];const auto& conversion=routes.banks[bank];
    if(site.rally_count<2||site.joint_capture_count<2||!site.joint_capture[1].ready)return false;
    auto& named=layout.site;named.opportunity=group_id(site.group);named.altar=sdk::altar_id(conversion.altar);
    named.width=cambc::map::width();named.height=cambc::map::height();named.altar_tile=conversion.altar;
    named.own_approaches[0]=conversion.approach.destination;named.approach_count=2;
    const auto enemy_terrain=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(cambc::team::ally);
    int found=0;p6::Point target{},pad{};
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        const p6::Point at{conversion.altar.x+x,conversion.altar.y+y};
        if((!x&&!y)||at.x<0||at.y<0||at.x>=named.width||at.y>=named.height||at==conversion.approach.destination||
            routes.terrain().is_set(tile(at))||enemy_terrain.is_set(tile(at))||cambc::map::bushes().is_set(tile(at)))continue;
        bool spawn=false;for(const auto& camp_group:cambc::map::camp_groups())spawn|=camp_group.spawn_tiles().is_set(tile(at));if(spawn)continue;
        if(found++==0)target=at;else if(found==2)pad=at;
    }
    if(found<2||!field(escort,pad,routes))return false;
    named.target=target;named.own_approaches[1]=target;named.map_identity={distances.map_identity(),p6::Source::public_map};
    const auto* owner=snapshot.frame.visible(snapshot.draft.actors[0]);const auto* helper=snapshot.frame.visible(snapshot.draft.actors[1]);
    if(!owner||!helper)return false;
    auto owner_frame=subject(snapshot.frame,*owner);owner_frame.own.actor.position=site.approach.destination;
    auto short_model=std::make_unique<Solo>(solo(routes,source,bank,owner_frame,false));if(!short_model->feasible)return false;
    layout.short_solo=short_model->plan;
    auto& collector=layout.collector;collector=layout.short_solo;collector.organization=Organization::H;
    collector.crew={snapshot.draft.actors[0],snapshot.draft.actors[1],-1,-1,-1,-1};collector.crew_count=2;
    collector.pads[0]=site.rally[0].destination;collector.pads[1]=site.rally[1].destination;collector.rally_legal=true;
    collector.ingress=via_pad(routes,site.approach.destination,site,collector.pads[0]);
    collector.capture=routes.route(collector.pads[0],site.capture);collector.forecast_rounds=120;
    auto& support=layout.helper;support=collector;
    support.ingress=via_pad(routes,routes.sites[next].approach.destination,site,support.pads[1]);
    support.capture=routes.route(support.pads[1],site.joint_capture[1]);support.release=routes.route(site.joint_capture[1].destination,escort);
    support.bank=support.release;support.renewal=stay(pad);support.forecast_rounds=120;
    auto helper_frame=subject(snapshot.frame,*helper);helper_frame.own.actor.position=pad;helper_frame.own.actor.bag=0;
    auto after=std::make_unique<Solo>(solo(routes,next,next_bank,helper_frame,false));if(!after->feasible)return false;
    layout.helper_after=after->plan;layout.escort_pad=pad;
    for(const auto* route:{&collector.bank,&collector.renewal})for(int i=0;i<route->count;++i)if(route->tiles[i]==pad)return false;
    for(int i=0;i<collector.capture.count;++i)for(int j=0;j<support.capture.count;++j)
        if(collector.capture.tiles[i]==support.capture.tiles[j])return false;
    // A fixed pad must really remain personally observable along conversion;
    // the trace still rechecks the actual live peer on every observation.
    double vision=-1;
    if(owner->id==snapshot.frame.clock.self)vision=cambc::self().base_stats().vision_radius;
    else for(const auto& ally:cambc::vision::champions(cambc::team::ally))if(sdk::identity(ally)==owner->id)vision=ally.base_stats().vision_radius;
    if(!std::isfinite(vision)||vision<1||vision>65535||cambc::map::bushes().is_set(tile(pad)))return false;
    const auto radius=static_cast<std::uint16_t>(std::floor(vision));
    for(int i=0;i<collector.bank.count;++i)
        if(!cambc::map::walls().shadow_cast(tile(collector.bank.tiles[i]),radius).is_set(tile(pad)))return false;
    economy::TributeTiming timing{{cambc::constants::submit_gem_duration_multiplier,p6::Source::queried_setting},
        {cambc::constants::submit_gem_base_duration,p6::Source::queried_setting},{cambc::constants::submit_gem_minimum_duration,p6::Source::queried_setting}};
    const int tribute=economy::tribute_ticks(site.group.gem_value(),timing);if(tribute<0)return false;
    layout.escort_wait_bound=walking(collector.bank)+tribute+12;
    return layout.valid(snapshot.draft)&&layout.escort_wait_bound<=120&&distances.prepare(named);
}
inline int event_tick(const p6::Schedule& schedule,int actor,p6::EventKind kind) {
    const int at=p6::actor_index(schedule,actor);if(at<0)return -1;
    for(int e=0;e<schedule.actors[at].event_count;++e)if(schedule.actors[at].events[e].kind==kind)return schedule.actors[at].events[e].tick;
    return -1;
}
inline bool independent_same(const p6::Schedule& a,const p6::Schedule& b,int id) {
    const int x=p6::actor_index(a,id),y=p6::actor_index(b,id);if(x<0||y<0)return false;
    const auto& l=a.actors[x];const auto& r=b.actors[y];if(l.event_count!=r.event_count||l.occupied_turns.value!=r.occupied_turns.value)return false;
    for(int e=0;e<l.event_count;++e)if(l.events[e].kind!=r.events[e].kind||l.events[e].tick!=r.events[e].tick||l.events[e].gold_cost.value!=r.events[e].gold_cost.value)return false;
    return true;
}
struct ConvoyModel {Offer offer{};bool feasible=false;};
inline void convoy(ConvoyModel& out,const Routes& routes,int source,int bank,int next,int next_bank,const Layout& layout,
                   const Inputs& in,std::span<const Solo> baseline,const Offer& independent) {
    out.feasible=false;if(!layout.valid(in.draft)||baseline.size()!=3||in.frame.own.actor.pick_ordinal>1)return;
    auto plans=std::make_unique<std::array<Plan,3>>();(*plans)[0]=layout.collector;(*plans)[1]=layout.helper;
    (*plans)[2]=layout.helper_after;
    auto models=std::make_unique<std::array<Solo,3>>();const auto& site=routes.sites[source];
    std::array<p6::Productivity,2> workers{};int assembly=0;
    for(int i=0;i<2;++i) {
        const auto* actor=in.frame.visible(in.draft.actors[i]);if(!actor||!actor->alive||actor->bag||actor->channel!=p6::Channel::none)return;
        const bool visible_owner_proposal=i==0&&in.frame.clock.self==in.draft.actors[1]&&actor->position==(*plans)[0].pads[0];
        if(actor->position!=(*plans)[i].ingress.tiles[0]&&!visible_owner_proposal)return;
        for(const auto& camp:site.group)if(distance((*plans)[i].pads[i],point(camp.spawn_position()))>actor->attack_range||
            !cambc::map::walls().shadow_cast(tile((*plans)[i].pads[i]),actor->attack_range).is_set(camp.spawn_position()))return;
        workers[i]={actor->strength,0,actor->ranged};assembly=std::max(assembly,walking((*plans)[i].ingress));if(assembly>8)return;
    }
    auto targets=std::make_unique<std::array<p6::MonsterWork,32>>();int count=0;double incoming=0;
    for(const auto& camp:site.group) {if(count==32)return;const auto stats=camp.base_stats();const double hp=camp.is_visible()?(camp.is_alive()?double(camp.health()):0.):double(stats.base_health);
        const double strength=camp.is_visible()?double(camp.strength()):double(stats.base_strength);(*targets)[count++]={sdk::identity(camp),hp,strength};if(hp>0)incoming+=strength;}
    const auto clear=p6::combat_work({targets->data(),std::size_t(count)},workers);if(!clear.resolved)return;
    const int finish=assembly+8+clear.rounds,payout=finish+walking(layout.collector.capture)+cambc::constants::capture_time+1;
    const int helper_arrival=finish+std::max(walking(layout.collector.capture),walking(layout.helper.capture))+1+walking(layout.helper.release);
    if(helper_arrival>=payout)return; // coverage must exist before own payout
    const int support=helper_arrival+layout.escort_wait_bound;
    const auto* owner=in.frame.visible(in.draft.actors[0]);const auto* helper=in.frame.visible(in.draft.actors[1]);
    (*models)[0]=solo(routes,source,bank,subject(in.frame,*owner),false);
    auto future=subject(in.frame,*helper);future.own.actor.position=layout.escort_pad;future.own.actor.bag=0;
    (*models)[1]=solo(routes,next,next_bank,future,false);
    for(const auto& model:baseline)if(model.plan.collector==in.draft.actors[2])(*models)[2]=model;
    if(!(*models)[0].feasible||!(*models)[1].feasible||!(*models)[2].feasible||(*models)[2].plan.group==layout.collector.group||
        (*models)[2].plan.group==layout.helper_after.group||plan_route_hash((*models)[1].plan)!=plan_route_hash(layout.helper_after))return;
    auto empty=std::make_unique<Offer>();out.offer=*empty;auto& offered=out.offer;
    offered.plan=in.frame.clock.self==in.draft.actors[0]?layout.collector:layout.helper;offered.schedule.target_id=layout.collector.group;
    std::array<economy::BankVisit,3> visits{};
    for(int i=0;i<3;++i) {
        auto& model=(*models)[i];normalize(model,in.frame);int delay=0;auto schedule=std::make_unique<p6::Schedule>(model.model.schedule);
        if(i==0) {delay=payout-schedule->productions[0].tick;for(int e=0;e<schedule->actors[0].event_count;++e)schedule->actors[0].events[e].tick+=delay;
            schedule->productions[0].tick+=delay;schedule->actors[0].occupied_turns.value+=delay;}
        if(!append(offered.schedule,*schedule,i==1?support:0,i*64))return;
        auto certificate=model.model.certificate;if(i<2) {
            certificate.group=layout.collector.group;
            const int extra=i==0?assembly+8+std::max(0,walking(layout.collector.capture)-walking(model.plan.capture)):support;
            for(auto* exposure:{&certificate.clear,&certificate.hold,&certificate.bank,&certificate.recovery})
                exposure->incoming_total+=incoming*extra+model.ambient_incoming*(i==0?std::max(0,delay):support);
        }
        if(!certificate.valid(in.frame,certificate.actor,certificate.group))return;
        offered.safety[offered.safety_count++]=certificate;const auto& plan=i==0?layout.collector:model.plan;
        visits[i]={model.plan.collector,plan.bank.tiles[plan.bank.count-1],solo_bank_bag(model),model.ambient_incoming,
            plan.renewal.tiles[std::min(1,plan.renewal.count-1)],{plan.bank.tiles.data(),std::size_t(plan.bank.count)}};
    }
    if(!apply_bank_queue(offered,visits,in.frame)||!independent_same(independent.schedule,offered.schedule,in.draft.actors[2])||
        event_tick(offered.schedule,in.draft.actors[0],p6::EventKind::tribute)-helper_arrival>=layout.escort_wait_bound)return;
    out.feasible=true;
}
} // namespace p8::finite_counterprotocol::native
