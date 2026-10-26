#pragma once
#include "routes.h"
#include "sdk.h"
#include "policy.h"
#include "draft.h"
#include "native_economy.h"
#include "solo_storage.h"
#include "forecast_probe.h"
#include <memory>

#ifndef P8_SOLO_INPUT_STATUS
#define P8_SOLO_INPUT_STATUS 0
#endif

namespace p8::native {
#if P8_SOLO_INPUT_STATUS
struct SoloInputStatus {
    int phase=0,reason=0,round=-1,source_bits=0,field_bits=0,route_bits=0;
    std::array<int,4> route_counts{};
    int groups=0,members=0,legs=0;
    unsigned long long fuel=0;
};
#endif
inline bool append(p6::Schedule& to,const p6::Schedule& from,int delay=0,int prefix=0) {
    if (from.actor_count!=1 || to.actor_count==p6::max_actors ||
        to.production_count+from.production_count>p6::max_productions) return false;
    auto actor=from.actors[0];const int offset=to.production_count;
    if (!actor.occupied_turns.known()) return false;
    actor.occupied_turns.value+=delay;
    for (int e=0;e<actor.event_count;++e) {
        actor.events[e].tick+=delay;actor.events[e].event_id+=prefix;
        if (actor.events[e].production_index>=0) actor.events[e].production_index+=offset;
    }
    for (int p=0;p<from.production_count;++p) {
        auto production=from.productions[p];production.tick+=delay;production.event_id+=prefix;
        to.productions[to.production_count++]=production;
    }
    to.actors[to.actor_count++]=actor;return true;
}
inline p6::Gems solo_bank_bag(const Solo& model) {
    if (model.model.schedule.actor_count!=1)return -1;
    p6::Gems bag=model.model.schedule.actors[0].initial_bag.value;
    for (int p=0;p<model.model.schedule.production_count;++p)
        if (!p6::add(bag,model.model.schedule.productions[p].gems.value,bag))return -1;
    return bag;
}
inline bool apply_bank_queue(common::Offer& offer,std::span<const economy::BankVisit> visits,const p6::Frame& frame,
                             std::array<std::uint64_t,3>* queue_budget=nullptr,const p6::Frame* observed_allies=nullptr) {
    if(queue_budget)(*queue_budget)[0]=cambc::fuel_remaining();
    if (!economy::queue_exits_clear(visits))return false;
    // A visible outsider already occupying this exact approach has no represented
    // release commitment. Reject that coupling instead of assuming a free tile.
    if(observed_allies) {
        if(int(cambc::round_number())!=frame.clock.round||visits.size()>p6::max_actors)return false;
        std::array<p6::Point,p6::max_actors> approaches{};
        for(std::size_t i=0;i<visits.size();++i)approaches[i]=visits[i].approach;
        if(!sdk::observed_allied_approaches_clear(*observed_allies,frame.clock,offer.schedule,
            {approaches.data(),visits.size()}))return false;
    } else {
        for (const auto& ally:cambc::vision::champions(cambc::team::ally))
            if (ally.is_alive()&&p6::actor_index(offer.schedule,sdk::identity(ally))<0)
                for (const auto& visit:visits)if (point(ally.position())==visit.approach)return false;
    }
    economy::TributeTiming timing{{cambc::constants::submit_gem_duration_multiplier,p6::Source::queried_setting},
        {cambc::constants::submit_gem_base_duration,p6::Source::queried_setting},
        {cambc::constants::submit_gem_minimum_duration,p6::Source::queried_setting}};
    const auto result=economy::queue_schedule(offer.schedule,visits,
        {offer.safety.data(),std::size_t(offer.safety_count)},timing,
#if P8_ORDINARY_COMPLETE_CYCLE
        common::ordinary_complete_offer(offer,frame.clock.self)?
            std::min(offer.plan.remaining_rounds,int(cambc::constants::max_rounds)-frame.clock.round):
            std::min(common::forecast_window,int(cambc::constants::max_rounds)-frame.clock.round),
        common::ordinary_complete_offer(offer,frame.clock.self));
#else
        std::min(common::forecast_window,int(cambc::constants::max_rounds)-frame.clock.round));
#endif
    if (!result.valid)return false;
    if(queue_budget)(*queue_budget)[1]=cambc::fuel_remaining();
    for (int c=0;c<offer.safety_count;++c)
        if (!offer.safety[c].valid(frame,offer.safety[c].actor,offer.safety[c].group))return false;
    const int self=p6::actor_index(offer.schedule,frame.clock.self);if(self<0)return false;
    offer.plan.forecast_rounds=int(offer.schedule.actors[self].occupied_turns.value);
    if(queue_budget)(*queue_budget)[2]=cambc::fuel_remaining();
    return plan_valid(offer.plan,frame.clock.self)&&
        (!observed_allies||int(cambc::round_number())==frame.clock.round);
}
inline bool queue_alternatives(common::Offer& offer,std::span<const Solo> alternatives,const p6::Frame& frame,
                              std::array<std::uint64_t,3>* queue_budget=nullptr,const p6::Frame* observed_allies=nullptr) {
    if(queue_budget)queue_budget->fill(0);
    std::array<economy::BankVisit,p6::max_actors> visits{};
    if(offer.safety_count<1||offer.safety_count>p6::max_actors)return false;
    for (int c=0;c<offer.safety_count;++c) {
        const Solo* found=nullptr;
        for (const auto& model:alternatives)
            if(model.model.schedule.actor_count==1&&model.model.schedule.actors[0].actor_id==offer.safety[c].actor)found=&model;
        if (!found||!route_valid(found->plan.bank))return false;
        if(!route_valid(found->plan.renewal))return false;
        visits[c]={offer.safety[c].actor,found->plan.bank.tiles[found->plan.bank.count-1],solo_bank_bag(*found),found->ambient_incoming,
            found->plan.renewal.tiles[std::min(1,found->plan.renewal.count-1)],
            {found->plan.bank.tiles.data(),std::size_t(found->plan.bank.count)}};
    }
    return apply_bank_queue(offer,{visits.data(),std::size_t(offer.safety_count)},frame,queue_budget,observed_allies);
}
inline p6::Frame subject(const p6::Frame& frame,const p6::Actor& actor) {
    if (frame.clock.self==actor.id) return frame;
    p6::Frame out=frame;out.clock.self=actor.id;out.own={actor,0,false,false};out.actor_count=0;
    auto add=[&](const p6::Actor& a) {if (a.id!=actor.id && out.actor_count<p6::max_actors)
        out.actors[out.actor_count++]={a,out.clock,a.id,p6::Knowledge::exact};};
    add(frame.own.actor);for (int i=0;i<frame.actor_count;++i) add(frame.actors[i].value);return out;
}
inline p6::Group group(const Site& s,const p6::ViewClock& clock) {
    p6::Group g;g.id=group_id(s.group);g.spawn=s.capture.destination;g.approach=s.approach.destination;
    const auto current=[&]{return int(cambc::round_number())==clock.round&&cambc::fuel_remaining()>=16000;};
    if(!current())return g;
    g.retreat=s.approach.destination;const auto visible=cambc::vision::visible_tiles();
    g.all_spawns_visible=true;
    for (const auto& m:s.group) {
        if(!current())return g;
        g.original_spawn_visible|=visible.is_set(m.spawn_position());
        g.all_spawns_visible&=visible.is_set(m.spawn_position());
    }
    if (g.original_spawn_visible) {
        if(!current())return g;
        const auto state=s.group.state();
        g.lifecycle=std::holds_alternative<cambc::map::camp_group_alive>(state)?p6::Lifecycle::living:
            std::holds_alternative<cambc::map::camp_group_reward_available>(state)?p6::Lifecycle::reward_available:p6::Lifecycle::respawning;
    }
    // A visible terrain tile in a bush does not reveal a hidden occupant.
    // Match Adapter's live group semantics before calling the forecaster.
    g.uncontested_now=g.all_spawns_visible && (s.group.spawn_tiles()&cambc::map::bushes()).is_empty();
    for (const auto& a:cambc::vision::champions(cambc::team::enemy)) {
        if(!current())return g;
        if (s.group.spawn_tiles().is_set(a.position()))g.uncontested_now=false;
    }
    return g;
}
#if P8_SOLO_INPUT_STATUS
#define P8_SOLO_CURRENT(phase) current(phase)
#else
#define P8_SOLO_CURRENT(phase) current()
#endif
inline void solo_into(const Routes& routes,int site_index,int bank_index,const p6::Frame& input,bool spawn_visit,
                      bool public_draft_scenario,Solo& out,p6::forecast::Request* storage=nullptr,
                      std::array<unsigned long long,3>* budget=nullptr,forecast_probe::BuildProbe probe={},
                      bool owned_forecast=false
#if P8_SOLO_INPUT_STATUS
                      ,SoloInputStatus* status=nullptr
#endif
                      ) {
    if(budget)budget->fill(0);
    reset_solo(out);if(storage)reset_request(*storage);
    out.site=site_index;out.bank=bank_index;
    const auto& site=routes.sites[site_index];const auto& bank=routes.banks[bank_index];
    const auto& a=input.own.actor;
#if P8_SOLO_INPUT_STATUS
    const auto failed=[&](int phase,int reason,int observed,unsigned long long fuel) {
        if(!status||status->phase)return;
        status->phase=phase;status->reason=reason;status->round=observed;status->fuel=fuel;
        const auto& p=out.plan;status->route_counts={p.ingress.count,p.capture.count,p.bank.count,p.renewal.count};
        if(storage){status->groups=storage->group_count;status->members=storage->groups[0].member_count;status->legs=storage->leg_count;}
    };
    const auto current=[&](int phase) {
        if(public_draft_scenario)return true;
        const int observed=int(cambc::round_number());
        if(observed!=input.clock.round){failed(phase,1,observed,0);return false;}
        const auto fuel=cambc::fuel_remaining();
        if(fuel<20000){failed(phase,1,observed,fuel);return false;}
        return true;
    };
    const auto refusal=[&](int phase,int reason) {
        if(status)failed(phase,reason,int(cambc::round_number()),cambc::fuel_remaining());
    };
#else
    const auto current=[&]{return public_draft_scenario||
        (int(cambc::round_number())==input.clock.round&&cambc::fuel_remaining()>=20000);};
#endif
    if(!P8_SOLO_CURRENT(1))return;
    for(const auto& m:site.group)
        if(a.attack_range<0 || distance(site.approach.destination,point(m.spawn_position()))>a.attack_range) {
#if P8_SOLO_INPUT_STATUS
            refusal(2,2);
#endif
            return;
        }
    const auto sight=cambc::map::walls().shadow_cast(tile(site.approach.destination),a.attack_range);
    for(const auto& m:site.group)if(!sight.is_set(m.spawn_position())) {
#if P8_SOLO_INPUT_STATUS
        refusal(3,3);
#endif
        return;
    }
    if(!P8_SOLO_CURRENT(4))return;
    auto g=public_draft_scenario?p6::Group{}:group(site,input.clock);
    if (public_draft_scenario) {
        // Explicit public starting-state forecast. This never enters a runtime
        // ledger or masquerades as a personally observed live camp state.
        g.id=group_id(site.group);g.spawn=site.capture.destination;g.approach=site.approach.destination;
        g.retreat=g.approach;g.lifecycle=p6::Lifecycle::living;
        g.original_spawn_visible=g.all_spawns_visible=g.uncontested_now=true;
    }
#if P8_SOLO_INPUT_STATUS
    if(status) {
        status->source_bits=int(g.original_spawn_visible)|(int(g.all_spawns_visible)<<1)|(int(g.uncontested_now)<<2)|(int(g.lifecycle)<<3);
        status->field_bits=int(site.approach.ready)|(int(site.capture.ready)<<1)|(int(bank.approach.ready)<<2)|(int(routes.spawn.ready)<<3);
    }
#endif
    if (!g.original_spawn_visible || !site.approach.ready || !site.capture.ready ||
        !bank.approach.ready || (spawn_visit&&!routes.spawn.ready)) {
#if P8_SOLO_INPUT_STATUS
        refusal(5,4);
#endif
        return;
    }
    if(!P8_SOLO_CURRENT(6))return;
    auto& p=out.plan;p.organization=Organization::U;p.group=g.id;p.collector=a.id;
    p.altar=bank.altar.y*cambc::map::width()+bank.altar.x;p.crew[0]=a.id;p.crew_count=1;
    if(budget)(*budget)[0]=cambc::fuel_remaining();
    routes.route_into(a.position,site.approach,p.ingress);
    const auto fixed=routes.cycle_paths(site_index,bank_index,spawn_visit);
    if(fixed.complete(spawn_visit)) {
        copy_route_payload_into(p.capture,*fixed.capture);copy_route_payload_into(p.bank,*fixed.bank);
        if(spawn_visit)Routes::join_into(*fixed.home,*fixed.back,p.renewal);
        else copy_route_payload_into(p.renewal,*fixed.renewal);
    }
    if (!route_valid(p.ingress)||!route_valid(p.capture)||!route_valid(p.bank)||!route_valid(p.renewal)) {
#if P8_SOLO_INPUT_STATUS
        refusal(7,5);
#endif
        return;
    }
#if P8_SOLO_INPUT_STATUS
    if(status)status->route_bits=15;
#endif
    if(budget)(*budget)[1]=cambc::fuel_remaining();
    if(!P8_SOLO_CURRENT(8))return;
    std::unique_ptr<p6::forecast::Request> owned;
    if(!storage)owned=make_default<p6::forecast::Request>();
    auto& r=storage?*storage:*owned;r.project_group=g.id;
    r.timing={{sdk::settings(),p6::Source::queried_setting},
        {cambc::constants::submit_gem_duration_multiplier,p6::Source::queried_setting},
        {cambc::constants::submit_gem_base_duration,p6::Source::queried_setting},
        {cambc::constants::submit_gem_minimum_duration,p6::Source::queried_setting}};
    r.kit={{a.strength,0,a.ranged,false,false,false},input.clock,a.id,p6::Knowledge::exact};
    if(!P8_SOLO_CURRENT(9))return;
    // Every visible awake monster and enemy receives full ambient stress,
    // including a target already awake before ingress. Later target exposure
    // may be counted twice; omitting pre-clear ingress damage would be unsafe.
    double ambient=0;
    if (!public_draft_scenario) for (const auto& m:cambc::vision::camps()) {
        if(!P8_SOLO_CURRENT(10))return;
        if (m.is_alive()&&m.is_awake())ambient+=m.strength();
    }
    if (!public_draft_scenario) for (const auto& e:cambc::vision::champions(cambc::team::enemy)) {
        if(!P8_SOLO_CURRENT(11))return;
        double cast=0;for (const auto& ability:e.abilities())if (ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))
            cast=std::max(cast,double(ability.damage_amount()));
        ambient+=e.strength()+cast;
    }
    r.ambient_incoming={ambient,input.clock,a.id,p6::Knowledge::exact};
    out.ambient_incoming=ambient;
    r.bank_position=bank.approach.destination;r.bank_waypoint=p.altar;
    r.renewal_position=site.approach.destination;r.renewal_waypoint=g.id;
    r.bank_lock_hits={int(cambc::constants::altar_lock_hits),p6::Source::queried_setting};
    r.group_count=1;auto& target=r.groups[0];target.observed={g,input.clock,g.id,p6::Knowledge::exact};
    target.membership_complete={true,p6::Source::public_map};
    target.gems={site.group.gem_value(),p6::Source::public_map};target.gold={site.group.gold_value(),p6::Source::public_map};
    for (const auto& m:site.group) {
        if(!P8_SOLO_CURRENT(12))return;
        if (target.member_count==32) {
#if P8_SOLO_INPUT_STATUS
            refusal(12,6);
#endif
            return;
        }
        auto& member=target.members[target.member_count++];const auto stats=m.base_stats();
        member.public_full={{sdk::identity(m),stats.base_health,stats.base_strength},p6::Source::public_map};
        member.current={};
        if (!public_draft_scenario && m.is_visible())member.current={{sdk::identity(m),m.is_alive()?double(m.health()):0.,double(m.strength())},input.clock,sdk::identity(m),p6::Knowledge::exact};
    }
    auto leg=[&](const Route& path) {r.legs[r.leg_count++]={path.tiles[0],path.tiles[path.count-1],{path.count-1,p6::Source::public_map}};};
    leg(p.ingress);leg(p.capture);leg(p.bank);
    if (spawn_visit) {
        leg(*fixed.home);leg(*fixed.back);r.recovery={true,false,1000000,routes.spawn.destination,{1,p6::Source::conservative}};
    } else leg(p.renewal);
    if(!P8_SOLO_CURRENT(13))return;
    if(budget)(*budget)[2]=cambc::fuel_remaining();
    if(owned_forecast||probe.mark)forecast_probe::build_into(input,r,out.model,probe);
    else p6::forecast::build_into(input,r,out.model);
    if (out.model.status!=p6::Status::feasible) return;
    p.forecast_rounds=int(out.model.schedule.actors[0].occupied_turns.value);
    p.remaining_rounds=std::min(common::forecast_window,int(cambc::constants::max_rounds)-input.clock.round);
#if P8_ORDINARY_COMPLETE_CYCLE
    // A complete ordinary U return is physical work, not a probe or a grant
    // of evidence. Draft and spawn-shop campaigns retain their old bound.
    if(!public_draft_scenario&&!spawn_visit&&ordinary_long_cycle(p))
        p.remaining_rounds=r.timing.settings.value.max_rounds-input.clock.round;
#endif
    p.capture_window=cambc::constants::capture_time;p.gem_bearing=p.complete_verified=true;
    p.pads[0]=site.approach.destination;
    out.feasible=plan_valid(p,a.id);
    return;
}
#undef P8_SOLO_CURRENT
inline Solo solo(const Routes& routes,int site_index,int bank_index,const p6::Frame& input,bool spawn_visit,
                 bool public_draft_scenario=false) {
    Solo out=exact_default<Solo>();
    solo_into(routes,site_index,bank_index,input,spawn_visit,public_draft_scenario,out);
    return out;
}
inline std::vector<common::DraftCandidate> draft_candidates(int prospective_actor,int ordinal,bool spawn_visit) {
    // The draft guest persists across picks. Spend its larger FIRST allowance
    // once, then reuse public starting-state forecasts on the 100k pick turns.
    // This is one bounded map-aware opening subset, never gameplay evidence.
    struct DraftCache {
        std::unique_ptr<Routes> routes;
        std::vector<common::DraftCandidate> candidates;
        int actor=-1;
        bool spawn_visit=false,attempted=false;
    };
    static auto cache=std::make_unique<DraftCache>();
    std::vector<common::DraftCandidate> out;
    if(!cambc::is_draft()||prospective_actor<0||ordinal<0||ordinal>=5)return out;
    const auto picked=cambc::draft::current_draft(cambc::team::ally);
    if(int(picked.size())!=ordinal)return out;
    auto available=[&](p6::Champion champion) {
        return std::find(picked.begin(),picked.end(),sdk::draft_type(champion))==picked.end();
    };
    auto retained=[&]() {
        std::vector<common::DraftCandidate> result;result.reserve(cache->candidates.size());
        if(cache->actor==prospective_actor&&cache->spawn_visit==spawn_visit)
            for(const auto& candidate:cache->candidates)if(available(candidate.champion))result.push_back(candidate);
        return result;
    };
    if(cache->attempted)return retained();
    // Never begin expensive geometry in a later small draft allowance. The
    // caller's mandatory supported-pool fallback must retain room to pick.
    if(cambc::fuel_remaining()<450000)return out;
    cache->attempted=true;cache->actor=prospective_actor;cache->spawn_visit=spawn_visit;
    const int round=cambc::round_number();
    auto current=[&]() {
        return cambc::is_draft()&&int(cambc::round_number())==round&&
            cambc::draft::current_draft(cambc::team::ally).size()==picked.size();
    };
    auto stage=[&](const char* name) {
        if(cambc::fuel_remaining()>100000&&current())
            std::printf("p8 DRAFT_FUEL stage=%s remaining=%llu candidates=%u\n",name,
                static_cast<unsigned long long>(cambc::fuel_remaining()),static_cast<unsigned>(cache->candidates.size()));
    };
    cache->routes=std::make_unique<Routes>();auto& routes=*cache->routes;routes.initialize(1);
    stage("initialize");
    if(!current()||routes.sites.empty()||routes.banks.empty()||cambc::fuel_remaining()<250000)return out;
    // The same nearest source and altar are offered to all eight kits. No
    // champion gets a free route, fabricated reward, or a fixed-roster bonus.
    const int source=0;int bank=0;
    for(int b=1;b<int(routes.banks.size());++b)
        if(distance(routes.sites[source].capture.destination,routes.banks[b].approach.destination)<
            distance(routes.sites[source].capture.destination,routes.banks[bank].approach.destination))bank=b;
    auto prepare=[&](Field& field) {
        if(!current()||cambc::fuel_remaining()<250000)return false;
        cambc::bitboard visited;
        field.paths.compute(routes.terrain(),singleton(field.destination),visited,256);
        field.ready=current()&&cambc::fuel_remaining()>=75000;return field.ready;
    };
    if(!prepare(routes.sites[source].approach)||!prepare(routes.sites[source].capture)||
        !prepare(routes.banks[bank].approach)||(spawn_visit&&!prepare(routes.spawn)))return out;
    stage("selected_fields");
    const auto& site=routes.sites[source];const auto& conversion=routes.banks[bank];
    auto short_leg=[](const Field& field,p6::Point from) {return field.paths.get(tile(from))<=24;};
    if(!short_leg(site.approach,routes.spawn.destination)||!short_leg(site.capture,site.approach.destination)||
        !short_leg(conversion.approach,site.capture.destination)||!short_leg(site.approach,conversion.approach.destination)||
        (spawn_visit&&(!short_leg(routes.spawn,conversion.approach.destination)||!short_leg(site.approach,routes.spawn.destination))))return out;
    const auto settings=sdk::settings();
    p6::ScenarioWindows windows;windows.valid=true;windows.source=p6::Source::public_map;
    windows.deadlines=windows.windows={common::forecast_window,common::forecast_window,common::forecast_window};
    windows.sampled_actor_id=prospective_actor;windows.sampled_round=0;
    cache->candidates.reserve(common::champion_pool.size());
    for (const auto& design:cambc::champion_designs) {
        const auto champion=sdk::type(design.champion_name());
        if(!common::supported_champion(champion)||!available(champion))continue;
        if(!current()||cambc::fuel_remaining()<200000)break;
        const auto stats=design.base_stats();p6::Frame frame;
        frame.clock={0,prospective_actor,0};auto& a=frame.own.actor;
        a.id=prospective_actor;a.pick_ordinal=ordinal;a.type=champion;a.team=p6::Team::ally;
        a.position=routes.spawn.destination;a.health=a.max_health=stats.base_health;a.strength=stats.base_strength;
        a.attack_range=stats.base_attack_range;a.ranged=a.attack_range>1;a.alive=a.can_move=a.can_attack=true;
        frame.own.at_spawn=true;frame.own.spawn_recovery_observed=true;
        const auto candidate=std::make_unique<Solo>(solo(routes,source,bank,frame,spawn_visit,true));
        if(!current()||cambc::fuel_remaining()<75000)break;
        if(!candidate->feasible)continue;
        const auto score=p6::evaluate_schedule(candidate->model.schedule,windows,settings,{0,prospective_actor},0);
        if(!score.eligible())continue;
        common::DraftCandidate best;best.champion=champion;best.first_income=candidate->plan;
        best.complete_schedule=candidate->model.schedule;const auto& c=candidate->model.certificate;
        best.cumulative_survival={c.clear,c.hold,c.bank,c.recovery};
        best.starting_health={a.health,p6::Source::queried_setting};best.starting_max_health={a.max_health,p6::Source::queried_setting};
        best.public_geometry_verified=best.ability_schedule_legal=true;cache->candidates.push_back(std::move(best));
    }
    stage("forecasts");
    if(!current()||cambc::fuel_remaining()<40000)return out;
    return retained();
}
} // namespace p8::native
