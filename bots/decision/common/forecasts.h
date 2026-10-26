#pragma once
#include "information.h"
#include "paths.h"
#include "model.h"
#include <cmath>
#include <numeric>
#include <map>

#ifndef PLAN3_BANK_MATERIALIZATION_FENCE
#define PLAN3_BANK_MATERIALIZATION_FENCE 0
#endif

namespace plan3_runtime {
inline int chebyshev(cambc::vec2 a, cambc::vec2 b) { return static_cast<int>(a.chebyshev_distance(b)); }
inline cambc::bitboard radius(cambc::vec2 p, int r) {
    cambc::bitboard b; b.draw_chebyshev(p, r); return b;
}
// Fresh full-health renewal features do not depend on observations or current
// health. Keep their bounded partial preparation across otherwise fresh menu
// contexts; publish a value only after every public group has been checked.
struct ForecastLoopCache {
    struct Entry {
        std::size_t cursor=0;
        bool complete=false;
        std::vector<plan3::RenewalLoop> loops;
    };
    std::vector<double> signature;
    std::map<int,Entry> entries;
};
// Geometry is exact, gear independent and owned by one fresh callback context.
// Hypothetical equipment forks share it, never a completed gear-specific loop.
struct ForecastLoopGeometryCache {
    struct Group { bool ready=false,reachable=false; int altar=-1,back=0,to_spawn=0,channel=0,regeneration=0; cambc::vec2 end; };
    std::vector<Group> groups;
    std::map<std::pair<int,int>,int> travel;
};
struct PreparedAcquisition {
    plan3::JobForecast job;
    plan3::Outcome nominal;
    int first=-1,second=-1,travel=0,first_capture=0,capture=0,bag=0,race_back=-1,production_return=-1;
    double first_loss=0,heal_rate=0;
    std::array<plan3::ClearEstimate,2> fresh;
    std::array<int,2> respawn{};
    std::vector<int> route_arrival;
    bool denial=false,ready=false,response_support=false;
    plan3::PotentialState race_terminal,cleared_race_terminal,death_terminal;
    int race_duration=0,cleared_race_duration=0,passive_gold=0,death_delay=0,denial_delay=0;
};
struct ForecastContext {
    const PersonalInformation& personal;
    const StaticPaths& paths;
    plan3::InformationState info;
    double strength = 0, focus = 0, camp_damage = 1, camp_mitigation = 1, capture_gold = 1;
    int recall = 0, heal = 0;
    unsigned monster_shards = 0, jungle_shards = 0;
    int uncertain_group = -1;
    // Endpoint-only consumers may omit generic renewal potential.
    bool terminal_renewal_loops = true;
    // Direct nominal-rate consumers may omit acquisition response worlds and
    // terminal continuation. Other producers and the default path stay full.
    bool nominal_acquisitions_only = false;
    mutable std::map<int,std::vector<plan3::RenewalLoop>> loop_cache;
    ForecastLoopCache* shared_loop_cache=nullptr;
    mutable bool cutoff = false;
    std::shared_ptr<ForecastLoopGeometryCache> geometry_cache=std::make_shared<ForecastLoopGeometryCache>();
    mutable unsigned geometry_hits=0,geometry_misses=0,prepared_acquisitions=0,materialized_acquisitions=0,terminal_evaluations=0,loop_evaluations=0;
    std::vector<int> lock_delays;
    ForecastContext(const PersonalInformation& p,const StaticPaths& d,plan3::InformationState s)
        :personal(p),paths(d),info(std::move(s)),lock_delays(p.altars.size(),0) {
        for (const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))
            for (std::size_t a=0;a<p.altars.size();++a)
                if (p.altars[a]==lock.position()) lock_delays[a]=static_cast<int>(lock.hits_remaining())+1;
    }
    // Explicit metadata fork: no SDK/vision query, no local loop vector copy,
    // and no shared gear-specific continuation. Geometry lifetime is callback local.
    struct MetadataOnly {};
    ForecastContext(const ForecastContext& c,MetadataOnly)
        :personal(c.personal),paths(c.paths),info(c.info),strength(c.strength),focus(c.focus),
         camp_damage(c.camp_damage),camp_mitigation(c.camp_mitigation),capture_gold(c.capture_gold),
         recall(c.recall),heal(c.heal),monster_shards(c.monster_shards),jungle_shards(c.jungle_shards),
         uncertain_group(c.uncertain_group),terminal_renewal_loops(c.terminal_renewal_loops),
         nominal_acquisitions_only(c.nominal_acquisitions_only),
         geometry_cache(c.geometry_cache),lock_delays(c.lock_delays) {}
    ForecastContext fork_without_loop_cache() const { return ForecastContext(*this,MetadataOnly{}); }
    int healing_turns(double health) const {
        const double rate = cambc::constants::spawn_healing * info.maximumHealth;
        if (health >= info.maximumHealth) return 0;
        return rate > 0 ? static_cast<int>(std::ceil((info.maximumHealth-std::max(0.0,health))/rate))+1 : info.roundLimit;
    }
    std::vector<int> lifecycle_values() const { return lifecycle_values_for(uncertain_group); }
    std::vector<int> lifecycle_values_for(int target) const {
        if(target<0)return {0};
        const auto& o=personal.observations[target];
        const auto& d=personal.groups[target];
        if(o.lifecycle && o.round<=personal.observation_round) {
            const int age=personal.observation_round-o.round;
            if(o.lifecycle==Lifecycle::respawning && age<o.respawn_remaining)return {2};
            if(o.lifecycle==Lifecycle::reward && age<d.max_respawn_timer)return {2,1};
            if(o.lifecycle==Lifecycle::alive && age<cambc::constants::capture_time+1)return {0,1};
        }
        return {0,1,2};
    }
    int tribute(int bag) const {
        if (bag==0) return 0; // No channel exists for an empty bag.
        const auto value = plan3::tribute_completion_turns(bag,
            cambc::constants::submit_gem_duration_multiplier,
            cambc::constants::submit_gem_base_duration,
            cambc::constants::submit_gem_minimum_duration);
        return value.value_or(-1);
    }
    int field_distance(int field, cambc::vec2 p) const { return paths.get(field, p); }
    int altar_for(const cambc::bitboard& from, int bag) const {
        return altar_for_distances(bag,[&](int field) { return paths.minimum(field,from); });
    }
    int altar_for(int from_field, int bag) const {
        return altar_for_distances(bag,[&](int field) { return paths.minimum_between(field,from_field); });
    }
    int altar_for(cambc::vec2 position, int bag) const {
        return altar_for_distances(bag,[&](int field) { return paths.get(field,position); });
    }
    template<class Distance> int altar_for_distances(int bag,Distance distance) const {
        int best=-1, duration=0;
        const int infinity=std::numeric_limits<int>::max();
        std::array<std::pair<int,int>,2> reachable{{{infinity,-1},{infinity,-1}}};
        for(std::size_t a=0;a<personal.altars.size();++a) {
            const int d=distance(personal.altar_fields[a]);
            if(d<0)continue;
            const auto value=std::pair{d,static_cast<int>(a)};
            if(value<reachable[0]) { reachable[1]=reachable[0];reachable[0]=value; }
            else if(value<reachable[1])reachable[1]=value;
        }
        const int channel=tribute(bag);
        for(const auto& [d,a]:reachable) {
            if(a<0)continue;
            const int candidate=d+channel+(bag>0?lock_delay(a):0);
            if(best<0 || candidate<duration) { best=a; duration=candidate; }
        }
        return best;
    }
    cambc::vec2 altar_endpoint(int altar, int from_field) const {
        const auto result=paths.closest_root(personal.altar_fields[altar],from_field);
        if(result)return *result;
        cutoff=true; return personal.altars[altar];
    }
    cambc::vec2 reached_approach(int altar,cambc::vec2 start) const {
        const auto result=paths.endpoint(personal.altar_fields[altar],start);
        if(result)return *result;
        cutoff=true; return start;
    }
    plan3::ClearEstimate clear(std::size_t i, double health, int lifecycle = -1) const {
        const auto& d = personal.groups[i];
        const auto* o = personal.current(i);
        double hp = o ? o->remaining_health : d.full_health;
        double incoming = o ? o->incoming : d.incoming;
        if (lifecycle == 1 || (o && o->lifecycle == Lifecycle::reward)) hp = incoming = 0;
        // A respawning world waits for a full fresh group, not an empty camp.
        if (lifecycle == 0 || lifecycle == 2 || (o && o->lifecycle==Lifecycle::respawning)) {
            hp = d.full_health; incoming = d.incoming;
        }
        return plan3::estimate_clear(hp, incoming, strength, focus,
            camp_damage, camp_mitigation, health, info.maximumHealth);
    }
    std::vector<plan3::RenewalLoop> loops(cambc::vec2 position) const {
        ++loop_evaluations;
        const int tile = position.y*personal.width+position.x;
        if (const auto found = loop_cache.find(tile); found != loop_cache.end()) return found->second;
        const int return_from_spawn = paths.get(personal.spawn_field,position);
        if (return_from_spawn < 0) return {};
        ForecastLoopCache::Entry local;
        auto* entry=&local;
        if(shared_loop_cache) {
            std::vector<double> signature{info.maximumHealth,strength,focus,camp_damage,
                camp_mitigation,capture_gold,static_cast<double>(recall),static_cast<double>(paths.generation())};
            for(const auto& [item,count]:info.equipment.items) { signature.push_back(item); signature.push_back(count); }
            signature.push_back(-1); // Separate variable-length gear and lock records.
            for(const auto delay:lock_delays)signature.push_back(delay);
            if(signature!=shared_loop_cache->signature) {
                shared_loop_cache->entries.clear(); shared_loop_cache->signature=std::move(signature);
            }
            if(!shared_loop_cache->entries.contains(tile) && shared_loop_cache->entries.size()>=128)
                shared_loop_cache->entries.erase(shared_loop_cache->entries.begin());
            entry=&shared_loop_cache->entries[tile];
        }
        if(entry->complete) { loop_cache[tile]=entry->loops; return entry->loops; }
        auto& result=entry->loops;
        for (; entry->cursor < personal.groups.size(); ++entry->cursor) {
            if (cambc::fuel_remaining() < 45000) { cutoff=true; return {}; }
            const auto& d = personal.groups[entry->cursor];
            if (!d.gem_value) continue;
            const auto c = clear(entry->cursor, info.maximumHealth, 0);
            const auto route_key=std::pair{tile,static_cast<int>(entry->cursor)};
            const auto found_travel=geometry_cache->travel.find(route_key);
            const int travel=found_travel!=geometry_cache->travel.end()?found_travel->second:paths.get(d.field,position);
            if(found_travel==geometry_cache->travel.end() && geometry_cache->travel.size()<4096)
                geometry_cache->travel.emplace(route_key,travel);
            if (!c.admitted || travel < 0) continue;
            if(geometry_cache->groups.size()!=personal.groups.size())geometry_cache->groups.resize(personal.groups.size());
            auto& geometry=geometry_cache->groups[entry->cursor];
            if(!geometry.ready) {
                ++geometry_misses;
                geometry.altar=altar_for(d.field,d.gem_value);
                if(geometry.altar>=0) {
                    geometry.back=paths.minimum_between(personal.altar_fields[geometry.altar],d.field);
                    geometry.end=altar_endpoint(geometry.altar,d.field);
                    geometry.to_spawn=paths.get(personal.spawn_field,geometry.end);
                    geometry.channel=tribute(d.gem_value)+lock_delay(geometry.altar);
                    geometry.regeneration=std::max(0,d.max_respawn_timer);
                    geometry.reachable=geometry.back>=0 && geometry.to_spawn>=0;
                }
                if(cutoff)return {};
                geometry.ready=true;
            } else ++geometry_hits;
            if(!geometry.reachable)continue;
            const int cycle=travel+c.turns+cambc::constants::capture_time+1+geometry.back+geometry.channel;
            const int regeneration=geometry.regeneration;
            // The extra full cycle after regeneration is deliberately
            // conservative; both recurring recovery and respawn are charged.
            const int period = std::max(cycle + recall + healing_turns(info.maximumHealth-c.healthLoss) + return_from_spawn,
                                        regeneration + cycle);
            result.push_back({d.key, cycle, period, d.gem_value, true,
                              plan3::Provenance::assumed});
        }
        std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            const double ar = double(a.gems) / a.period, br = double(b.gems) / b.period;
            return plan3::finite_equal(ar, br) ? a.groupId < b.groupId : ar > br;
        });
        if (result.size() > 8) result.resize(8);
        entry->complete=true;
        loop_cache[tile] = result;
        return result;
    }
    plan3::PotentialState terminal(cambc::vec2 p, double health, int bag = 0) const {
        ++terminal_evaluations;
        plan3::PotentialState s;
        s.bag = bag; s.health = std::max(0.0, health); s.maximumHealth = info.maximumHealth;
        s.equipment = info.equipment;
        if (bag > 0) {
            const int a = altar_for(p, bag);
            if (a >= 0) {
                s.cashoutTurns = paths.get(personal.altar_fields[a], p) + tribute(bag) + lock_delay(a);
                p=reached_approach(a,p);
            }
        }
        s.position.tile = p.y * personal.width + p.x;
        if (terminal_renewal_loops) s.loops = loops(p);
        s.loopSupportUnavailable = s.loops.empty();
        const int to_spawn = paths.get(personal.spawn_field, p);
        if (to_spawn >= 0) s.preparationTurns = recall + healing_turns(health) + to_spawn;
        return s;
    }
    plan3::PotentialState acquisition_terminal(cambc::vec2 p,double health) const {
        if(!nominal_acquisitions_only)return terminal(p,health);
        ++terminal_evaluations;
        plan3::PotentialState s;
        s.health=std::max(0.0,health);s.maximumHealth=info.maximumHealth;s.equipment=info.equipment;
        s.position.tile=p.y*personal.width+p.x;
        s.loopSupportUnavailable=true;
        // No bag remains at either acquisition endpoint. Future loops and
        // preparation time are unavailable, not zero-cost renewal forecasts.
        return s;
    }
    int lock_delay(int altar) const {
        return altar>=0 && static_cast<std::size_t>(altar)<lock_delays.size()?lock_delays[altar]:0;
    }
    // Exact nominal conversion timing needs no renewal loops or response worlds.
    // Unknown or unfinished route fields remain unavailable, never zero travel.
    std::optional<int> bank_completion(int altar) const {
        if(altar<0 || static_cast<std::size_t>(altar)>=personal.altar_fields.size() || info.carriedGems<=0)
            return std::nullopt;
        const int travel=paths.get(personal.altar_fields[altar],cambc::self().position());
        if(travel<0)return std::nullopt;
        return travel+tribute(info.carriedGems)+lock_delay(altar);
    }
    plan3::JobForecast bank(int altar) const {
        plan3::JobForecast j;
#if PLAN3_BANK_MATERIALIZATION_FENCE
        const auto ready=[&] {
            if(cambc::fuel_remaining()>=45000)return true;
            cutoff=true;j.complete=false;j.outcomes.clear();return false;
        };
        if(!ready())return j;
#endif
        if(cutoff) { j.complete=false;return j; }
        const auto p = cambc::self().position();
        const int travel = paths.get(personal.altar_fields[altar], p);
        j.id = {plan3::JobKind::bank, -1, -1,
            personal.altars[altar].y * personal.width + personal.altars[altar].x};
        j.routeKnown = travel >= 0;
        if (!j.routeKnown || info.carriedGems <= 0) return j;
        plan3::Outcome o; o.deltaS = info.carriedGems;
        o.tau = *bank_completion(altar);
        o.conversions.push_back({o.tau, o.deltaS});
        o.terminal = terminal(reached_approach(altar,p), info.health);
        if(cutoff) { j.complete=false;return j; }
        j.outcomes.reserve(plan3::outcome_cap);
        j.outcomes.push_back(o); j.proposerMask = 4;
        for(int world=1;world<4;++world) {
#if PLAN3_BANK_MATERIALIZATION_FENCE
            if(!ready())return j;
#endif
            auto outcome=world==3?std::move(o):o;
            outcome.response=static_cast<plan3::ResponseWorld>(world);
            if(world==2 && !personal.enemies.empty()) {
                auto cell=p; int intercept=-1;
                for(int sample=0;sample<8;++sample) {
                    for(const auto& enemy:personal.enemies)
                        if(chebyshev(enemy.position,cell)<=sample && intercept<0)intercept=sample;
                    const auto next=paths.next_step(personal.altar_fields[altar],cell);
                    if(!next || *next==cell)break; cell=*next;
                }
                if(intercept>=0) {
                    outcome.deltaS=0; outcome.conversions.clear();
                    outcome.tau=intercept+cambc::constants::respawn_time;
                    outcome.healthLoss=info.health;
                    outcome.terminal=terminal(personal.spawn_positions.front(),info.maximumHealth);
                }
            }
            if(world==3) {
                bool supported=false;
                for(const auto& enemy:personal.enemies)
                    supported|=chebyshev(enemy.position,personal.altars[altar])<=8;
                if(supported) {
                    outcome.tau+=cambc::constants::altar_lock_hits+1;
                    outcome.conversions[0].completedAt=outcome.tau;
                }
            }
            j.outcomes.push_back(std::move(outcome));
        }
        if(uncertain_group>=0) {
            auto base=std::move(j.outcomes);
            j.outcomes.clear();
            const auto labels=lifecycle_values();
            j.outcomes.reserve(labels.size()*base.size());
            for(std::size_t l=0;l<labels.size();++l)for(std::size_t w=0;w<base.size();++w) {
#if PLAN3_BANK_MATERIALIZATION_FENCE
                if(!ready())return j;
#endif
                auto outcome=l+1==labels.size()?std::move(base[w]):base[w];
                const int lifecycle=labels[l];
                outcome.lifecycle=lifecycle; j.outcomes.push_back(std::move(outcome));
            }
            for(auto& outcome:j.outcomes)outcome.conditionalLifecycleWeight=1.0/labels.size();
        }
        j.complete=!cutoff;
        return j;
    }
    plan3::JobForecast renewal(bool emergency) const {
        plan3::JobForecast j; j.id.kind = plan3::JobKind::renew;
        j.mandatorySafety = emergency;
        const auto p = cambc::self().position();
        const auto& spawns = personal.spawn_positions;
        const bool at_spawn=personal.own_spawn.is_set(p);
        j.renewalRequired=!at_spawn || info.health<info.maximumHealth;
        plan3::Outcome o; o.tau = recall + healing_turns(info.health);
        if (at_spawn) o.tau = healing_turns(info.health);
        else {
            int escape = 0;
            for (const auto& e : personal.enemies) if (chebyshev(p, e.position) <= 8) escape = std::max(escape, 9-chebyshev(p, e.position));
            for (const auto& camp:cambc::vision::camps())
                if (camp.is_alive() && camp.is_awake() && camp.strength()>0)
                    escape=std::max(escape,camp.attack_range()+2-chebyshev(p,camp.position()));
            o.tau += escape;
        }
        // A no-op recovery still consumes the owner turn before its observed
        // endpoint; it cannot provide a free zero-time continuation or move an
        // already-spawned actor to a different canonical tile.
        o.tau=std::max(1,o.tau);
        o.terminal = terminal(at_spawn?p:spawns.front(), info.maximumHealth, info.carriedGems);
        j.outcomes.push_back(std::move(o)); j.complete=!cutoff; return j;
    }
    bool current_reward_contested(int group) const {
        if(info.round<0 || personal.observation_round!=static_cast<unsigned>(info.round))return false;
        const auto* observed=personal.current(std::size_t(group));
        if(!observed || observed->lifecycle!=Lifecycle::reward)return false;
        const auto& region=personal.groups[std::size_t(group)].spawns;
        return std::any_of(personal.enemies.begin(),personal.enemies.end(),[&](const auto& enemy) {
            return enemy.round==personal.observation_round && region.is_set(enemy.position);
        });
    }
    PreparedAcquisition prepare_acquisition(int first,int second=-1,bool responses=true) const {
        ++prepared_acquisitions;
        PreparedAcquisition feature;feature.first=first;feature.second=second;feature.response_support=responses;
        auto& j=feature.job;
        if(cutoff) { j.complete=false;return feature; }
        const auto& g = personal.groups[first];
        const auto* observed = personal.current(first);
        const auto p = cambc::self().position();
        const auto c1 = clear(first, info.health);
        const int travel = paths.get(g.field, p);
        const GroupDesign& last = second < 0 ? g : personal.groups[second];
        const int bag = info.carriedGems + g.gem_value + (second < 0 ? 0 : last.gem_value);
        j.id = {second < 0 ? plan3::JobKind::cycle : plan3::JobKind::double_cycle, g.key,
                second < 0 ? -1 : last.key, -1};
        j.campGems = bag-info.carriedGems;
        j.campGold = g.gold_value + (second < 0 ? 0 : last.gold_value);
        j.fullClearAdmitted = c1.admitted;
        j.routeKnown = travel >= 0;
        // Positive current personal occupancy invalidates immediate capture.
        // A completed empty forecast is rejected by admission/review; remembered
        // enemies and future renewal production remain separate assumptions.
        if(current_reward_contested(first) || (second>=0 && current_reward_contested(second)))return feature;
        if(j.fullClearAdmitted && travel==StaticPaths::unknown) { j.complete=false; return feature; }
        if(!j.fullClearAdmitted || !j.routeKnown)return feature;
        const int altar = altar_for(last.field, bag);
        if(altar>=0)j.id.altarTile=personal.altars[altar].y*personal.width+personal.altars[altar].x;
        j.routeKnown = travel >= 0 && altar >= 0;
        if (!j.routeKnown) return feature;
        const int hold = cambc::constants::capture_time + 1;
        int wait = observed && observed->lifecycle == Lifecycle::respawning ? std::max(0, observed->respawn_remaining)
            :personal.remembered_respawn_minimum(first);
        int capture = std::max(travel, wait) + c1.turns + hold;
        const int first_capture=capture;
        double loss = c1.healthLoss;
        if (second >= 0) {
            const int between = paths.minimum_between(last.field, g.field);
            const auto c2 = clear(second, info.health-loss);
            if (between < 0) j.routeKnown = false;
            j.fullClearAdmitted &= c2.admitted;
            if(!j.fullClearAdmitted || !j.routeKnown)return feature;
            const auto* o2 = personal.current(second);
            const int w2 = o2 && o2->lifecycle == Lifecycle::respawning ? std::max(0, o2->respawn_remaining)
                :personal.remembered_respawn_minimum(second);
            capture = std::max(capture + std::max(0,between),w2) + c2.turns + hold;
            loss += c2.healthLoss;
        }
        const int back = paths.minimum_between(personal.altar_fields[altar], last.field);
        if (back < 0) { j.routeKnown = false; return feature; }
        plan3::Outcome nominal;
        nominal.deltaS = bag; nominal.tau = capture + back + tribute(bag) + lock_delay(altar);
        nominal.healthLoss = loss;
        nominal.goldEarned = std::floor(j.campGold*capture_gold)+cambc::constants::passive_gold*nominal.tau;
        if (bag>0) nominal.conversions.push_back({nominal.tau,bag});
        if (bag==0) {
            j.rewardOnlyEndpoint=true;
            nominal.tau=capture;
            nominal.terminal=terminal(last.anchor,info.health-loss);
        } else {
            nominal.terminal=terminal(altar_endpoint(altar,last.field),info.health-loss);
        }
        // A partially prepared terminal feature set is retained privately by
        // ForecastLoopCache. Do not spend the remaining turn cloning worlds
        // for a job that cannot yet be published as complete.
        if(cutoff) { j.complete=false;return feature; }
        nominal.goldEarned=std::floor(j.campGold*capture_gold)+cambc::constants::passive_gold*nominal.tau;
        const int return_to_prior_position=paths.get(personal.spawn_field,p);
        if (return_to_prior_position<0) j.routeKnown=false;
        j.projectedRenewalAndReturnTime = recall + healing_turns(info.health-loss)
            + std::max(0,return_to_prior_position);
        j.travelPlusClear = travel + c1.turns;
        j.releaseTime = wait;
        j.personalReleaseEvidence = observed && (second < 0 || personal.current(second));
        for (const auto& enemy : personal.enemies) {
            // Enemy travel is only an optimistic Chebyshev lower bound.
            const int arrival = chebyshev(enemy.position,g.anchor);
            if (!j.opponentDeadline || arrival < *j.opponentDeadline) j.opponentDeadline = arrival;
        }
        j.personalDeadlineEvidence = j.opponentDeadline.has_value();
        j.supportedDenialGems = j.personalDeadlineEvidence ? g.gem_value : 0;
        // Cache route timing features once. Every response is then rebuilt
        // from its lifecycle production record; an aborted race contains no
        // clear episode to subtract when a hidden camp is already cleared.
        std::vector<int> route_arrival;
        auto cell=last.anchor;
        for(int sample=0;sample<8 && responses && !personal.enemies.empty() && !j.rewardOnlyEndpoint;++sample) {
            int earliest=std::numeric_limits<int>::max();
            for(const auto& enemy:personal.enemies)earliest=std::min(earliest,chebyshev(enemy.position,cell));
            route_arrival.push_back(earliest);
            const auto next=paths.next_step(personal.altar_fields[altar],cell);
            if(!next || *next==cell)break; cell=*next;
        }
        bool denial=false;
        for(const auto& enemy:personal.enemies)denial|=chebyshev(enemy.position,personal.altars[altar])<=8;
        const int race_back=paths.get(personal.altar_fields[altar],g.anchor);
        if(race_back<0)j.routeKnown=false;
        feature.nominal=std::move(nominal);feature.travel=travel;feature.first_capture=first_capture;
        feature.capture=capture;feature.bag=bag;feature.first_loss=c1.healthLoss;feature.race_back=race_back;
        feature.route_arrival=std::move(route_arrival);feature.denial=denial;
        feature.production_return=paths.get(personal.spawn_field,
            {feature.nominal.terminal.position.tile%personal.width,feature.nominal.terminal.position.tile/personal.width});
        feature.heal_rate=cambc::constants::spawn_healing*info.maximumHealth;
        feature.passive_gold=cambc::constants::passive_gold;feature.death_delay=cambc::constants::respawn_time;
        feature.denial_delay=cambc::constants::altar_lock_hits+1;
        feature.fresh[0]=clear(first,info.health,0);feature.respawn[0]=g.max_respawn_timer;
        if(second>=0) { feature.fresh[1]=clear(second,info.health,0);feature.respawn[1]=last.max_respawn_timer; }
        if(responses && j.opponentDeadline) {
            const int abort_at=std::max(travel,*j.opponentDeadline);
            if(info.carriedGems>0 && race_back>=0) {
                const auto reached=reached_approach(altar,g.anchor);
                feature.race_duration=feature.cleared_race_duration=abort_at+race_back+tribute(info.carriedGems)+lock_delay(altar);
                feature.race_terminal=terminal(reached,info.health-c1.healthLoss);
                feature.cleared_race_terminal=terminal(reached,info.health);
            } else if(j.rewardOnlyEndpoint) {
                feature.race_duration=feature.cleared_race_duration=abort_at;
                feature.race_terminal=terminal(g.anchor,info.health-c1.healthLoss);
                feature.cleared_race_terminal=terminal(g.anchor,info.health);
            } else {
                feature.race_duration=abort_at+recall+healing_turns(info.health-c1.healthLoss);
                feature.cleared_race_duration=abort_at+recall+healing_turns(info.health);
                feature.race_terminal=terminal(personal.spawn_positions.front(),info.maximumHealth);
                feature.cleared_race_terminal=feature.race_terminal;
            }
        }
        if(!feature.route_arrival.empty())feature.death_terminal=terminal(personal.spawn_positions.front(),info.maximumHealth);
        j.complete=!cutoff;feature.ready=j.complete;return feature;
    }
    struct ProductionRow { plan3::Outcome outcome;double first_loss=0;int first_capture=0,final_capture=0; };
    ProductionRow production_for(const PreparedAcquisition& f,int target,int lifecycle,double mass) const {
        ProductionRow row;row.outcome=f.nominal;row.first_loss=f.first_loss;
        auto& production=row.outcome;production.lifecycle=lifecycle;
        const int dependency=target==f.first?0:target==f.second?1:-1;
        int change=0;
        if(lifecycle && dependency>=0) {
            const auto full=f.fresh[static_cast<unsigned>(dependency)];
            change=lifecycle==1?-full.turns:f.respawn[static_cast<unsigned>(dependency)];
            if(lifecycle==1) {
                production.healthLoss=std::max(0.0,production.healthLoss-full.healthLoss);
                if(target==f.first)row.first_loss=0;
            }
            production.tau+=change;
            for(auto& conversion:production.conversions)conversion.completedAt+=change;
            production.terminal.health=std::max(0.0,info.health-production.healthLoss);
            if(f.production_return>=0) {
                const int heal=production.terminal.health>=info.maximumHealth?0:f.heal_rate>0?
                    static_cast<int>(std::ceil((info.maximumHealth-std::max(0.0,production.terminal.health))/f.heal_rate))+1:info.roundLimit;
                production.terminal.preparationTurns=recall+heal+f.production_return;
            }
        }
        production.conditionalLifecycleWeight=mass;
        production.goldEarned=std::floor(f.job.campGold*capture_gold)+f.passive_gold*production.tau;
        row.first_capture=f.first_capture+(target==f.first?change:0);row.final_capture=f.capture+change;
        return row;
    }
    // Validity consumer only: preserve the legacy first production lifecycle
    // row, including remembered-reward {2,1} ordering. Never score this one-row
    // record or confuse it with Scout's independent admission reference.
    plan3::JobForecast acquisition_review(int first,int second=-1) const {
        auto prepared=prepare_acquisition(first,second,false);auto job=prepared.job;
        if(!prepared.ready)return job;
        const auto labels=lifecycle_values();
        job.outcomes.push_back(production_for(prepared,uncertain_group,labels.front(),1.0/labels.size()).outcome);
        return job;
    }
    // Copy metadata without first cloning outcomes that will be replaced.
    // This is Scout-only bookkeeping, not another scored/admission world.
    static plan3::JobForecast scout_metadata(const plan3::JobForecast& source) {
        plan3::JobForecast job;
        job.id=source.id;job.nominalIndex=source.nominalIndex;job.admissionTau=source.admissionTau;
        job.complete=source.complete;job.fullClearAdmitted=source.fullClearAdmitted;job.routeKnown=source.routeKnown;
        job.mandatorySafety=source.mandatorySafety;job.renewalRequired=source.renewalRequired;
        job.rewardOnlyEndpoint=source.rewardOnlyEndpoint;
        job.projectedRenewalAndReturnTime=source.projectedRenewalAndReturnTime;
        job.campGems=source.campGems;job.campGold=source.campGold;job.travelPlusClear=source.travelPlusClear;
        job.releaseTime=source.releaseTime;job.opponentDeadline=source.opponentDeadline;
        job.supportedDenialGems=source.supportedDenialGems;
        job.personalDeadlineEvidence=source.personalDeadlineEvidence;job.personalReleaseEvidence=source.personalReleaseEvidence;
        job.owned=source.owned;job.proposerMask=source.proposerMask;job.investment=source.investment;
        job.observationNetValue=source.observationNetValue;
        job.observationRevealsTarget=source.observationRevealsTarget;job.observationSafe=source.observationSafe;
        return job;
    }
    // The caller owns readiness. Default callers stay pure and unrestricted.
    template<class Ready>
    plan3::JobForecast scout_response_aliases(const plan3::JobForecast& frozen,
            const std::vector<int>& labels,Ready&& ready) const {
        auto job=scout_metadata(frozen);
        const auto stopped=[&] { cutoff=true;job.complete=false;job.outcomes.clear();return std::move(job); };
        if(labels.empty() || labels.size()>3 || !frozen.nominal())return stopped();
        job.outcomes.reserve(labels.size()*4);
        for(const int label:labels)for(int response=0;response<4;++response) {
            const plan3::Outcome* source=nullptr;
            for(const auto& outcome:frozen.outcomes)if(static_cast<int>(outcome.response)==response) { source=&outcome;break; }
            if(!source && frozen.id.kind==plan3::JobKind::renew)source=frozen.nominal();
            if(!source || !ready())return stopped();
            auto outcome=*source;outcome.lifecycle=label;outcome.response=static_cast<plan3::ResponseWorld>(response);
            outcome.conditionalLifecycleWeight=1.0/labels.size();job.outcomes.push_back(std::move(outcome));
        }
        job.nominalIndex=0;return job;
    }
    plan3::JobForecast scout_response_aliases(const plan3::JobForecast& frozen,
            const std::vector<int>& labels) const {
        return scout_response_aliases(frozen,labels,[] {return true;});
    }
    // No route, self/vision, clear or loop query is allowed below. Every table
    // uses one target and at most three lifecycle labels x four responses.
    template<class Ready>
    plan3::JobForecast materialize_acquisition(const PreparedAcquisition& f,int target,
                                               const std::vector<int>& labels,Ready&& ready) const {
        ++materialized_acquisitions;
        auto j=f.job;
        const auto stopped=[&] { cutoff=true;j.complete=false;j.outcomes.clear();return std::move(j); };
        if(!f.response_support) { j.complete=false;return j; }
        if(!f.ready || !j.complete || !j.routeKnown || !j.fullClearAdmitted)return j;
        if(labels.empty() || labels.size()>3) { j.complete=false;return j; }
        unsigned seen=0;
        for(const int label:labels) {
            if(label<0 || label>2 || (seen&(1u<<label))) { j.complete=false;return j; }
            seen|=1u<<label;
        }
        j.outcomes.reserve(labels.size()*4);
        for(const int lifecycle:labels) {
            if(!ready())return stopped(); // production_for clones the nominal terminal.
            auto production=production_for(f,target,lifecycle,1.0/labels.size());
            const auto first_loss=production.first_loss;
            const auto first_capture=production.first_capture,final_capture=production.final_capture;
            for(int world=0;world<4;++world) {
                if(!ready())return stopped();
                auto o=world==3?std::move(production.outcome):production.outcome;o.response=static_cast<plan3::ResponseWorld>(world);
                if(world==1 && j.opponentDeadline && *j.opponentDeadline<=first_capture) {
                    const bool cleared=first_loss==0;
                    o.deltaS=info.carriedGems;o.conversions.clear();o.healthLoss=first_loss;
                    o.tau=cleared?f.cleared_race_duration:f.race_duration;
                    if(!ready())return stopped();
                    o.terminal=cleared?f.cleared_race_terminal:f.race_terminal;
                    if(o.deltaS>0 && f.race_back>=0)o.conversions.push_back({o.tau,o.deltaS});
                    o.goldEarned=f.passive_gold*o.tau;
                }
                if(world==2 && !j.rewardOnlyEndpoint) {
                    int intercept=-1;
                    for(std::size_t n=0;n<f.route_arrival.size();++n)
                        if(f.route_arrival[n]<=final_capture+static_cast<int>(n)) { intercept=final_capture+static_cast<int>(n);break; }
                    if(intercept>=0) {
                        o.deltaS=0;o.conversions.clear();o.tau=intercept+f.death_delay;
                        o.healthLoss=info.health;
                        if(!ready())return stopped();
                        o.terminal=f.death_terminal;
                        o.goldEarned=std::floor(j.campGold*capture_gold)+f.passive_gold*o.tau;
                    }
                }
                if(world==3 && f.denial && f.bag>0) {
                    o.tau+=f.denial_delay;
                    if(!o.conversions.empty())o.conversions[0].completedAt=o.tau;
                    o.goldEarned=std::floor(j.campGold*capture_gold)+f.passive_gold*o.tau;
                }
                j.outcomes.push_back(std::move(o));
            }
        }
        return j;
    }
    plan3::JobForecast materialize_acquisition(const PreparedAcquisition& f,int target,
                                               const std::vector<int>& labels) const {
        return materialize_acquisition(f,target,labels,[] {return true;});
    }
    plan3::JobForecast acquisition(int first, int second = -1) const {
        if(cutoff) { plan3::JobForecast j;j.complete=false;return j; }
        const auto& g = personal.groups[first];
        const auto* observed = personal.current(first);
        const auto p = cambc::self().position();
        const auto c1 = clear(first, info.health);
        const int travel = paths.get(g.field, p);
        const GroupDesign& last = second < 0 ? g : personal.groups[second];
        const int bag = info.carriedGems + g.gem_value + (second < 0 ? 0 : last.gem_value);
        plan3::JobForecast j;
        j.id = {second < 0 ? plan3::JobKind::cycle : plan3::JobKind::double_cycle, g.key,
                second < 0 ? -1 : last.key, -1};
        j.campGems = bag-info.carriedGems;
        j.campGold = g.gold_value + (second < 0 ? 0 : last.gold_value);
        j.fullClearAdmitted = c1.admitted;
        j.routeKnown = travel >= 0;
        // Positive current personal occupancy invalidates immediate capture.
        // A completed empty forecast is rejected by admission/review; remembered
        // enemies and future renewal production remain separate assumptions.
        if(current_reward_contested(first) || (second>=0 && current_reward_contested(second)))return j;
        if(j.fullClearAdmitted && travel==StaticPaths::unknown) { j.complete=false; return j; }
        if(!j.fullClearAdmitted || !j.routeKnown)return j;
        const int altar = altar_for(last.field, bag);
        if(altar>=0)j.id.altarTile=personal.altars[altar].y*personal.width+personal.altars[altar].x;
        j.routeKnown = travel >= 0 && altar >= 0;
        if (!j.routeKnown) return j;
        const int hold = cambc::constants::capture_time + 1;
        int wait = observed && observed->lifecycle == Lifecycle::respawning ? std::max(0, observed->respawn_remaining)
            :personal.remembered_respawn_minimum(first);
        int capture = std::max(travel, wait) + c1.turns + hold;
        const int first_capture=capture;
        double loss = c1.healthLoss;
        if (second >= 0) {
            const int between = paths.minimum_between(last.field, g.field);
            const auto c2 = clear(second, info.health-loss);
            if (between < 0) j.routeKnown = false;
            j.fullClearAdmitted &= c2.admitted;
            if(!j.fullClearAdmitted || !j.routeKnown)return j;
            const auto* o2 = personal.current(second);
            const int w2 = o2 && o2->lifecycle == Lifecycle::respawning ? std::max(0, o2->respawn_remaining)
                :personal.remembered_respawn_minimum(second);
            capture = std::max(capture + std::max(0,between),w2) + c2.turns + hold;
            loss += c2.healthLoss;
        }
        const int back = paths.minimum_between(personal.altar_fields[altar], last.field);
        if (back < 0) { j.routeKnown = false; return j; }
        plan3::Outcome nominal;
        nominal.deltaS = bag; nominal.tau = capture + back + tribute(bag) + lock_delay(altar);
        nominal.healthLoss = loss;
        nominal.goldEarned = std::floor(j.campGold*capture_gold)+cambc::constants::passive_gold*nominal.tau;
        if (bag>0) nominal.conversions.push_back({nominal.tau,bag});
        if (bag==0) {
            j.rewardOnlyEndpoint=true;
            nominal.tau=capture;
            nominal.terminal=acquisition_terminal(last.anchor,info.health-loss);
        } else {
            nominal.terminal=acquisition_terminal(altar_endpoint(altar,last.field),info.health-loss);
        }
        // A partially prepared terminal feature set is retained privately by
        // ForecastLoopCache. Do not spend the remaining turn cloning worlds
        // for a job that cannot yet be published as complete.
        if(cutoff) { j.complete=false;return j; }
        nominal.goldEarned=std::floor(j.campGold*capture_gold)+cambc::constants::passive_gold*nominal.tau;
        const int return_to_prior_position=paths.get(personal.spawn_field,p);
        if (return_to_prior_position<0) j.routeKnown=false;
        j.projectedRenewalAndReturnTime = recall + healing_turns(info.health-loss)
            + std::max(0,return_to_prior_position);
        j.travelPlusClear = travel + c1.turns;
        j.releaseTime = wait;
        j.personalReleaseEvidence = observed && (second < 0 || personal.current(second));
        for (const auto& enemy : personal.enemies) {
            // Enemy travel is only an optimistic Chebyshev lower bound.
            const int arrival = chebyshev(enemy.position,g.anchor);
            if (!j.opponentDeadline || arrival < *j.opponentDeadline) j.opponentDeadline = arrival;
        }
        j.personalDeadlineEvidence = j.opponentDeadline.has_value();
        j.supportedDenialGems = j.personalDeadlineEvidence ? g.gem_value : 0;
        // Cache route timing features once. Every response is then rebuilt
        // from its lifecycle production record; an aborted race contains no
        // clear episode to subtract when a hidden camp is already cleared.
        std::vector<int> route_arrival;
        auto cell=last.anchor;
        for(int sample=0;sample<8 && !nominal_acquisitions_only && !personal.enemies.empty() && !j.rewardOnlyEndpoint;++sample) {
            int earliest=std::numeric_limits<int>::max();
            for(const auto& enemy:personal.enemies)earliest=std::min(earliest,chebyshev(enemy.position,cell));
            route_arrival.push_back(earliest);
            const auto next=paths.next_step(personal.altar_fields[altar],cell);
            if(!next || *next==cell)break; cell=*next;
        }
        bool denial=false;
        if(!nominal_acquisitions_only)
            for(const auto& enemy:personal.enemies)denial|=chebyshev(enemy.position,personal.altars[altar])<=8;
        const int race_back=paths.get(personal.altar_fields[altar],g.anchor);
        if(race_back<0)j.routeKnown=false;
        const auto labels=lifecycle_values();
        j.outcomes.reserve(nominal_acquisitions_only?1:labels.size()*4);
        for(std::size_t l=0;l<labels.size();++l) {
            const int lifecycle=labels[l];
            auto production=l+1==labels.size()?std::move(nominal):nominal;
            production.lifecycle=lifecycle;
            int change=0; double first_loss=c1.healthLoss;
            if(lifecycle && (uncertain_group==first || uncertain_group==second)) {
                const auto full=clear(uncertain_group,info.health,0);
                change=lifecycle==1?-full.turns:personal.groups[uncertain_group].max_respawn_timer;
                if(lifecycle==1) {
                    production.healthLoss=std::max(0.0,production.healthLoss-full.healthLoss);
                    if(uncertain_group==first)first_loss=0;
                }
                production.tau+=change;
                for(auto& conversion:production.conversions)conversion.completedAt+=change;
                production.terminal.health=std::max(0.0,info.health-production.healthLoss);
                const auto tile=production.terminal.position.tile;
                const int back_to_production=nominal_acquisitions_only?-1:
                    paths.get(personal.spawn_field,{tile%personal.width,tile/personal.width});
                if(back_to_production>=0)production.terminal.preparationTurns=recall+
                    healing_turns(production.terminal.health)+back_to_production;
            }
            production.conditionalLifecycleWeight=1.0/labels.size();
            production.goldEarned=std::floor(j.campGold*capture_gold)+cambc::constants::passive_gold*production.tau;
            const int life_first_capture=first_capture+(uncertain_group==first?change:0);
            const int life_final_capture=capture+change;
            if(nominal_acquisitions_only) {
                // Preserve the exact first nominal production lifecycle used
                // by Ledger. This is one assumed outcome, not full coverage.
                j.outcomes.push_back(std::move(production));j.complete=!cutoff;return j;
            }
            for(int world=0;world<4;++world) {
                auto o=world==3?std::move(production):production;
                o.response=static_cast<plan3::ResponseWorld>(world);
                if(world==1 && j.opponentDeadline && *j.opponentDeadline<=life_first_capture) {
                    const int abort_at=std::max(travel,*j.opponentDeadline);
                    o.deltaS=info.carriedGems; o.conversions.clear(); o.healthLoss=first_loss;
                    if(o.deltaS>0 && race_back>=0) {
                        o.tau=abort_at+race_back+tribute(o.deltaS)+lock_delay(altar);
                        o.conversions.push_back({o.tau,o.deltaS});
                        o.terminal=terminal(reached_approach(altar,g.anchor),info.health-first_loss);
                    } else if(j.rewardOnlyEndpoint) {
                        o.tau=abort_at; o.terminal=terminal(g.anchor,info.health-first_loss);
                    } else {
                        o.tau=abort_at+recall+healing_turns(info.health-first_loss);
                        o.terminal=terminal(personal.spawn_positions.front(),info.maximumHealth);
                    }
                    o.goldEarned=cambc::constants::passive_gold*o.tau;
                }
                if(world==2 && !j.rewardOnlyEndpoint) {
                    int intercept=-1;
                    for(std::size_t n=0;n<route_arrival.size();++n)
                        if(route_arrival[n]<=life_final_capture+static_cast<int>(n)){intercept=life_final_capture+static_cast<int>(n);break;}
                    if(intercept>=0) {
                        o.deltaS=0; o.conversions.clear(); o.tau=intercept+cambc::constants::respawn_time;
                        o.healthLoss=info.health;
                        o.terminal=terminal(personal.spawn_positions.front(),info.maximumHealth);
                        o.goldEarned=std::floor(j.campGold*capture_gold)+cambc::constants::passive_gold*o.tau;
                    }
                }
                if(world==3 && denial && bag>0) {
                    o.tau+=cambc::constants::altar_lock_hits+1;
                    if(!o.conversions.empty())o.conversions[0].completedAt=o.tau;
                    o.goldEarned=std::floor(j.campGold*capture_gold)+cambc::constants::passive_gold*o.tau;
                }
                j.outcomes.push_back(std::move(o));
            }
        }
        j.complete=!cutoff;
        return j;
    }
};
} // namespace plan3_runtime
