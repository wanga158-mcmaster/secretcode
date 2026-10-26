// Suite executor fork: advances to unseen members of partly cleared groups.
#pragma once
#include <cambc.h>
#include <array>
#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <variant>
#include <vector>
#include <cstdio>
#include "../../common/src/decision.h"
#include "frontier.h"
#include "flow.h"
#if RESET_VISIBLE_SAFETY
#include "safety.h"
#endif
#if RESET_RANGED_DEFENCE
#include "defence.h"
#endif
#if RESET_BANK_RETURNABILITY
#include "returnability.h"
#endif

namespace h12_reset_runtime {
using cambc::vec2;
using cambc::team;
using cambc::champion;
using cambc::camp;
using cambc::bitboard;
using cambc::item_type;

int distance(vec2 a, vec2 b) { return static_cast<int>(a.chebyshev_distance(b)); }
bitboard around(vec2 p, int radius) {
    bitboard result;
    result.draw_chebyshev(p, radius);
    return result;
}

template<class Policy> void draft() {
    constexpr std::array order{cambc::champion_draft::tavi, cambc::champion_draft::varg,
        cambc::champion_draft::vendetta, cambc::champion_draft::vesper_vale,
        cambc::champion_draft::marcella};
    const auto selected = cambc::draft::current_draft(team::ally);
    if constexpr (Policy::enabled) {
        for (int n : Policy::roster) {
            const auto choice = order[n];
            if (std::find(selected.begin(), selected.end(), choice) == selected.end()) {
                cambc::draft::pick(choice); return;
            }
        }
    }
    for (const auto choice : order)
        if (std::find(selected.begin(), selected.end(), choice) == selected.end()) {
            cambc::draft::pick(choice);
            return;
        }
}

struct Group {
    cambc::map::camp_group group;
    bitboard spawns;
    vec2 anchor;
    float health = 0, incoming = 0;
    bool monolith = false;
    unsigned unavailable_until = 0;
    int observed_round=-1;
    bool reachable=true;
    h12_reset_pursuit::FrontierMemory frontier_memory{};
};

template<class Policy> class Bot {
    Policy policy_;
    unsigned selections_=0, deviations_=0;
    std::vector<vec2> enemy_positions_;
    vec2 home_{};
    bool topology_ready_=false;
    int contest_since_=-1;
    int route_target_=-1,route_progress_=0,route_best_=10000;
    unsigned route_actions_=0;
    unsigned reset_refreshes_=0,reset_spends_=0,retarget_hits_=0,flow_logs_=0;
    std::vector<Group> groups_;
    std::vector<int> group_at_tile_;
    std::vector<vec2> altars_;
    bitboard terrain_;
    bitboard remembered_camps_;
    bool initialized_ = false;
    bool recovering_ = false;
    bool partial_clear_ = false;
    bool has_non_monolith_gems_ = false;
    int target_ = -1;
    unsigned monster_shards_ = 0, jungle_shards_ = 0;
    unsigned identity_ = 0;
#if RESET_VISIBLE_SAFETY
    std::vector<h12_reset_pursuit::VisibleThreat> visible_threats_;
    bitboard visible_danger_;
    float safety_health_=0,safety_maximum_=0;
    int safety_bag_=0;
    bool safety_active_=false;
    bool unsafe_damage(float damage)const {
        return policy_.retreat(safety_health_,safety_maximum_,damage,safety_bag_);
    }
    bool safe_step(vec2 p)const {
        return !safety_active_||h12_reset_pursuit::visible_route_safe(
            visible_threats_,{p.x,p.y},[&](float damage){return unsafe_damage(damage);});
    }
    void observe_threats(const std::vector<champion>& enemies) {
        if(!h12_reset_pursuit::begin_visible_observation(!enemies.empty(),safety_active_,visible_threats_,
                [&]{visible_danger_.clear();}))return;
        auto&me=cambc::self();
        safety_health_=me.health();safety_maximum_=me.max_health();safety_bag_=me.gem_count();
        visible_threats_.reserve(enemies.size());
        for(const auto&enemy:enemies) {
            if(cambc::fuel_remaining()<50000) {
                visible_threats_.clear();return; // retain the complete old observation if this optional scan cannot finish
            }
            const auto p=enemy.position();
            const int step=std::max(1,int(enemy.base_stats().base_move_speed));
            h12_reset_pursuit::VisibleThreat threat{{p.x,p.y},enemy.attack_range()+step,0,enemy.strength(),0};
            for(const auto&ability:enemy.abilities()) {
                if(!ability.info()||ability.cooldown()>1)continue;
                int radius=0;float damage=0;
                if(ability.is_damage())radius=int(std::ceil(ability.get<cambc::ability_damage>().range));
                else if(ability.is_aoe_damage()) {
                    const auto a=ability.get<cambc::ability_aoe_damage>();radius=int(std::ceil(a.range+a.radius));
                } else if(ability.is_cc())radius=int(std::ceil(ability.get<cambc::ability_cc>().range));
                else if(ability.is_dash()) {
                    const auto a=ability.get<cambc::ability_dash>();radius=int(std::ceil(a.range+a.radius));
                    threat.basic_range=std::max(threat.basic_range,int(std::ceil(a.range))+enemy.attack_range());
                } else continue;
                // These getters require current personal visibility, already
                // supplied by vision::champions. No hidden cooldown is queried.
                damage=ability.damage_amount();
                if(ability.is_cc()||(ability.is_dash()&&ability.get<cambc::ability_dash>().duration>0))
                    damage+=threat.basic; // reserve one denied response opportunity
                if(damage>threat.burst){threat.burst=damage;threat.burst_range=radius;}
            }
            visible_threats_.push_back(threat);
        }
        safety_active_=!visible_threats_.empty();
        h12_reset_pursuit::draw_visible_danger(visible_threats_,
            [&](float damage){return unsafe_damage(damage);},
            [&](auto p,int radius){visible_danger_|=around({p.x,p.y},radius);});
    }
#endif
#if HEURISTICS_DIAGNOSTIC
    int diagnostic_mode_=0;
    int diagnostic_goals_=0;
    int diagnostic_failure_=0,diagnostic_move_fuel_=0;
    vec2 diagnostic_target_{};
#endif
    void trace(const char*stage,int value=-1) const {
        if constexpr(requires{Policy::trace;})if constexpr(Policy::trace) {
            if(cambc::round_number()<=3&&cambc::fuel_remaining()>10000)
                std::printf("h12stage actor=%u round=%u stage=%s fuel=%d value=%d\n",identity_,cambc::round_number(),stage,int(cambc::fuel_remaining()),value);
        }
    }

    heuristics::State state(int altar=0) const {
        auto& me=cambc::self();
        heuristics::State s;
        s.round=cambc::round_number();s.limit=cambc::constants::max_rounds;
        s.bag=me.gem_count();s.gold=me.gold_count();s.altar=altar;s.identity=identity_;
        s.hp=me.health();s.maximum=me.max_health();
        s.capture=cambc::constants::capture_time;s.recall=cambc::constants::recall_time;
        s.monster=monster_shards_;s.jungle=jungle_shards_;s.mature=monster_shards_>=5&&jungle_shards_>=5;
        s.tribute=cambc::constants::submit_gem_duration_multiplier;
        return s;
    }

    void initialize() {
        terrain_ = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
            | cambc::map::spawn_region(team::enemy);
        altars_ = cambc::map::altars().positions();
        group_at_tile_.assign(cambc::map::width() * cambc::map::height(), -1);
        for (auto group : cambc::map::camp_groups()) {
            Group entry{group, group.spawn_tiles(), group.camps().front().spawn_position()};
            for (const auto& monster : group) {
                const auto p = monster.spawn_position();
                group_at_tile_[p.y * cambc::map::width() + p.x] = static_cast<int>(groups_.size());
                // All metadata here is public; no hidden live-state queries.
                const auto stats = monster.base_stats();
                entry.health += stats.base_health;
                entry.incoming += stats.base_strength;
                entry.monolith |= monster.is_monolith();
            }
            has_non_monolith_gems_ |= !entry.monolith && entry.group.gem_value() > 0;
            groups_.push_back(std::move(entry));
        }
        identity_ = static_cast<unsigned>(cambc::self().champion_name());
        if constexpr(Policy::enabled)home_=cambc::map::spawn_region(team::ally).positions().front();
        initialized_ = true;
    }

    void shop() {
        auto& me = cambc::self();
        // A bulk buy completes one stage at a time; Tavi may do this in the field.
        for (int stage = 0; stage < 3; ++stage) {
            const auto build = [&] {
                if constexpr(Policy::enabled)return policy_.build(monster_shards_,jungle_shards_);
                else return fundamentals::build_stage(monster_shards_, jungle_shards_);
            }();
            auto item = item_type::power_shard_of_strength;
            unsigned limit = 100;
            if (build == fundamentals::BuildStage::monster_strength) {
                item = item_type::monster_shard_of_strength;
                limit = 5 - monster_shards_;
            } else if (build == fundamentals::BuildStage::jungle_health) {
                item = item_type::jungle_shard_of_health;
                limit = 5 - jungle_shards_;
            }
            const unsigned count = std::min(limit, me.gold_count() / item.cost());
            if (!count || !me.can_buy(item, count)) return;
            me.buy(item, count);
            if (build == fundamentals::BuildStage::monster_strength) monster_shards_ += count;
            if (build == fundamentals::BuildStage::jungle_health) jungle_shards_ += count;
        }
    }

    bitboard blocked() {
        // Camps cannot move. Losing sight of a living barricade does not make
        // it traversable; only a fresh visible empty tile removes that obstacle.
        const auto visible=cambc::vision::visible_tiles();
        const auto living=cambc::vision::camp_tiles();
        remembered_camps_=(remembered_camps_&~visible)|living;
        auto result = terrain_ | cambc::vision::champion_tiles() | remembered_camps_
            | cambc::vision::teleport_tiles();
        result.set(cambc::self().position(), false);
        return result;
    }

    bool move_to(bitboard goals, const bitboard* danger = nullptr) {
#if HEURISTICS_DIAGNOSTIC
        diagnostic_failure_=0;
        diagnostic_move_fuel_=cambc::fuel_remaining();
#endif
        const auto failed=[&](int reason) {
#if HEURISTICS_DIAGNOSTIC
            diagnostic_failure_=reason;
#else
            (void)reason;
#endif
            return false;
        };
        trace("move-entry",int(goals.is_empty()));
        auto& me = cambc::self();
        if (!me.can_move() || goals.is_set(me.position())) return failed(1);
        auto walls = blocked();
        if (danger) walls |= *danger;
#if RESET_VISIBLE_SAFETY
        if constexpr(Policy::enabled)if(safety_active_)walls|=visible_danger_;
#endif
        walls.set(me.position(), false);
        goals &= ~walls;
        trace("move-walls",int(goals.is_empty()));
        if (goals.is_empty()) return failed(2);
        if (cambc::fuel_remaining() < (Policy::enabled?35000:30000)) return failed(3);
        // One native field, stopped at our tile, with path distance capped at 256.
        // Never build fields inside candidate-scoring loops.
        cambc::distance_field field;
        bitboard visited;
        field.compute_to(walls, goals, around(me.position(), 0), visited, 256);
        trace("move-field",int(field.get(me.position())));
        if (field.get(me.position()) == cambc::distance_field::unreachable) return failed(4);
        // Retry alternatives after a hidden occupant is revealed. The engine
        // preserves the movement allowance on an unsuccessful hidden collision.
        for (int retry = 0; retry < 3; ++retry) {
            const auto fresh_walls = blocked() | walls;
            auto best = me.position();
            auto best_distance = field.get(best);
            for (const auto p : fresh_walls.reachable_tiles(me.position(), 1).positions()) {
#if RESET_VISIBLE_SAFETY
                if constexpr(Policy::enabled)if(!safe_step(p))continue;
#endif
                const auto d = field.get(p);
                if (d < best_distance) { best = p; best_distance = d; }
            }
            if (best == me.position()) return failed(5);
            if (me.move_absolute(best)) return true;
            if constexpr(Policy::enabled)walls.set(best);
        }
        return failed(6);
    }

    // Live SDK adapter for the same bounded flow called by focused fixtures.
    template<class Target>
    bool fight(const Target& target, bool walked, bool dangerous) {
        auto& me = cambc::self();
        if (cambc::fuel_remaining() < (Policy::enabled?22000:16000)) return false;
        auto valid = [&] { return me.is_alive()&&target.is_visible() && target.health() > 0; };
        if (!valid()) return false;
        bool acted = false;
        auto abilities = me.abilities();
        if (!walked) {
            // Tavi/Marcella burst from range before choosing to walk.
            for (const auto& ability : abilities)
                if ((ability.is_damage() || ability.is_cc()) && me.can_cast()&&ability.is_ready()
                    && ((std::is_same_v<Target,camp>&&ability.can_target_camps())
                        ||(std::is_same_v<Target,champion>&&ability.can_target_enemies()))
                    && ability.valid_tiles().is_set(target.position())) {
                    acted |= ability.use(target);
                    break;
                }
        }
        if (!valid()) return acted;
        const bool in_range = me.attack_tiles().is_set(target.position());
        if (!in_range) return acted;
        if (dangerous)
            for (const auto& ability : abilities)
                if (ability.is_shield()&&ability.can_target_self()&&me.can_cast()&&ability.is_ready()) { acted |= ability.use(); break; }
        if(!me.can_attack())return acted;
        struct World {
            std::vector<Target> targets;
            std::vector<vec2> positions;
            std::optional<cambc::ability> reset_ability;
            mutable std::optional<bitboard> attack_mask;
            const bitboard& mask()const {
                if(!attack_mask)attack_mask=cambc::self().attack_tiles();
                return *attack_mask;
            }
            bool enough_fuel()const{return cambc::fuel_remaining()>14000;}
            bool can_attack()const{return cambc::self().is_alive()&&cambc::self().can_attack();}
            bool can_cast()const{return cambc::self().is_alive()&&cambc::self().can_cast();}
            bool legal(int n)const{
                const auto&t=targets[n];
                return cambc::self().is_alive()&&t.is_visible()&&t.health()>0
                    &&mask().is_set(t.position());
            }
            bool alive(int n)const {
                if constexpr(std::is_same_v<Target,camp>)
                    return !cambc::vision::in_vision(positions[n])||targets[n].is_alive();
                else return targets[n].is_alive();
            }
            int cooldown()const{return reset_ability?reset_ability->cooldown():-1;}
            bool reset_ready()const{return reset_ability&&reset_ability->is_ready();}
            bool reset(){
                const bool used=reset_ability&&can_cast()&&reset_ability->is_ready()&&reset_ability->use();
                if(used)attack_mask.reset();return used;
            }
            bool basic(int n){
                if(!can_attack()||!legal(n))return false;
                cambc::self().attack(targets[n]);attack_mask.reset();return true;
            }
            int choose(int excluded)const{
                int best=-1;float hp=std::numeric_limits<float>::infinity();
                const auto& observed_mask=mask();
                for(std::size_t i=0;i<targets.size();++i)if(int(i)!=excluded) {
                    const auto&t=targets[i];
                    if(!t.is_visible()||!t.is_alive()||!observed_mask.is_set(t.position()))continue;
                    const float health=t.health();
                    if(health>0&&health<hp){hp=health;best=int(i);}
                }
                return best;
            }
        } world;
        for(const auto&a:abilities)if(a.is_attack_reset()&&a.can_target_self()){world.reset_ability=a;break;}
        world.targets.push_back(target);
        if constexpr(std::is_same_v<Target,camp>) {
            const auto spawn=target.spawn_position();
            const int selected=group_at_tile_[spawn.y*cambc::map::width()+spawn.x];
            const auto visible=cambc::vision::visible_tiles();
            for(const auto&t:groups_[selected].group) {
                if(world.targets.size()>=6)break;
                if(t==target)continue;
                const auto p=t.spawn_position();
                if(visible.is_set(p))world.targets.push_back(t);
            }
        } else {
            for(const auto&t:cambc::vision::champions(team::enemy)) {
                if(world.targets.size()>=6)break;
                if(t!=target)world.targets.push_back(t);
            }
        }
        for(const auto&t:world.targets) {
            if constexpr(std::is_same_v<Target,camp>)world.positions.push_back(t.spawn_position());
            else world.positions.push_back(t.position());
        }
        const auto flow=h12_reset_pursuit::execute(world);
        reset_refreshes_+=flow.refreshes;reset_spends_+=flow.casts;retarget_hits_+=flow.different_hits;
        if(flow.first_refresh&&flow.different_hits&&flow_logs_<3&&cambc::fuel_remaining()>10000) {
            ++flow_logs_;
            const auto a=world.positions[flow.first],b=world.positions[flow.second];
            std::printf("h12reset actor=%u round=%u source=%d a=%d,%d b=%d,%d refresh=%u spend=%u different=%u basics=%u casts=%u attempts=%u\n",
                identity_,cambc::round_number(),target_,a.x,a.y,b.x,b.y,reset_refreshes_,reset_spends_,retarget_hits_,flow.basics,flow.casts,flow.attempts);
        }
        return acted||flow.basics>0||flow.casts>0;
    }

    bool recover(const std::vector<camp>& camps, const std::vector<champion>& enemies) {
        auto& me = cambc::self();
        const auto origin = me.position();
        bool safe = true;
        bitboard danger;
        for (const auto& monster : camps)
            if (monster.is_alive() && monster.is_awake() && monster.strength() > 0) {
                danger |= around(monster.position(), monster.attack_range());
                if (distance(origin, monster.position()) <= monster.attack_range() + 1) safe = false;
            }
        for (const auto& enemy : enemies) {
            danger |= around(enemy.position(), enemy.attack_range() + 1);
            if (distance(origin, enemy.position()) <= 8) safe = false;
        }
        if (safe) { me.recall(); return true; }
#if RESET_VISIBLE_SAFETY
        if constexpr(Policy::enabled)if(safety_active_) {
            auto threats=visible_threats_;
            for(const auto&monster:camps)if(monster.is_alive()&&monster.is_awake()&&monster.strength()>0) {
                const auto p=monster.position();
                threats.push_back({{p.x,p.y},monster.attack_range(),0,monster.strength(),0});
            }
            std::vector<h12_reset_pursuit::SafetyPoint> steps;
            for(const auto p:blocked().reachable_tiles(origin,1).positions())steps.push_back({p.x,p.y});
            const auto step=h12_reset_pursuit::visible_escape({origin.x,origin.y},steps,threats,{home_.x,home_.y});
            if((step.x!=origin.x||step.y!=origin.y)&&me.can_move()&&me.move_absolute({step.x,step.y}))return true;
            move_to(cambc::map::spawn_region(team::ally),&danger);return true;
        }
#endif
        // First get out of attack range. Maximise clearance locally if danger
        // surrounds us; a path whose starting tile is threatened may be blocked.
        auto legal = blocked().reachable_tiles(origin, 1);
        auto best = origin;
        float best_score = -1e30f;
        for (const auto p : legal.positions()) {
            float score = 0;
            for (const auto& monster : camps)
                if (monster.is_alive() && monster.is_awake() && monster.strength() > 0)
                    score += std::min(distance(p, monster.position()), 6) * monster.strength();
            for (const auto& enemy : enemies) score += std::min(distance(p, enemy.position()), 9) * 150;
            if (score > best_score) { best = p; best_score = score; }
        }
        if (best != origin && me.can_move() && me.move_absolute(best)) return true;
        move_to(cambc::map::spawn_region(team::ally), &danger);
        return true;
    }

#if RESET_BANK_RETURNABILITY
    bool bank(const std::vector<champion>& enemies) {
#else
    bool bank() {
#endif
        auto& me = cambc::self();
        if (!me.gem_count() || altars_.empty()) return false;
        int nearest = 10000;
        for (const auto altar : altars_) nearest = std::min(nearest, distance(me.position(), altar));
        const bool should_bank=[&] {
            if constexpr(Policy::enabled)return policy_.bank(state(std::max(0,nearest-1)));
            else return fundamentals::should_bank(me.gem_count(), cambc::round_number(), nearest);
        }();
        if (!should_bank) return false;
        bitboard goals;
        for (const auto altar : altars_) {
            if (distance(me.position(), altar) <= 1) {
                // Opposing locks take the queried engine hit count (five in this bundle). Friendly locks
                // allow tributing. Only use a fresh visible lock observation.
                for (const auto& lock : cambc::vision::altar_locks(team::enemy))
                    if (lock.position() == altar) {
                        if (me.can_attack()) me.attack(lock);
                        return true;
                    }
                me.submit_gems(altar);
                return true;
            }
            goals |= around(altar, 1);
        }
#if RESET_BANK_RETURNABILITY
        if constexpr(Policy::enabled) {
            // Query only at an admitted bank movement, never during unrelated
            // farming, channels or an already reached altar. Five enemies bound
            // this optional work; the existing movement fuel floor is unchanged.
            if(me.can_move()&&!enemies.empty()&&enemies.size()<=5&&cambc::fuel_remaining()>50000) {
                std::vector<h12_reset_pursuit::BankOpponent> opponents;
                opponents.reserve(enemies.size());
                for(const auto& enemy:enemies) {
                    const auto p=enemy.position();
                    const auto stats=enemy.base_stats();
                    h12_reset_pursuit::BankOpponent observed{{p.x,p.y},
                        std::max(0,int(std::floor(stats.base_move_speed))),enemy.attack_range(),
                        stats.base_attack_range==1}; // current queried catalog: melee1/ranged3
                    const auto abilities=enemy.abilities();
                    for(std::size_t i=0;i<abilities.size();++i) {
                        const auto& ability=abilities[i];
                        if(!ability.info()||ability.cooldown()!=0)continue;
                        auto& cast=observed.casts[i];cast.ready=true;
                        using Kind=h12_reset_pursuit::BankCastKind;
                        const auto tiles=[](float value){return std::max(0,int(std::floor(value)));};
                        if(ability.is_attack_reset()) {
                            const auto info=ability.get<cambc::ability_attack_reset>();
                            cast.kind=Kind::reset;cast.exclusive=info.exclusive_with_movement;
                        } else if(ability.is_damage()&&ability.can_target_enemies()) {
                            const auto info=ability.get<cambc::ability_damage>();
                            cast.kind=Kind::damage;cast.exclusive=info.exclusive_with_movement;
                            cast.range=tiles(info.range);cast.damages=info.modifier>0;
                        } else if(ability.is_aoe_damage()&&ability.can_target_enemies()) {
                            const auto info=ability.get<cambc::ability_aoe_damage>();
                            cast.kind=Kind::area;cast.exclusive=info.exclusive_with_movement;
                            cast.range=tiles(info.range);cast.radius=tiles(info.radius);cast.damages=info.modifier>0;
                        } else if(ability.is_cc()&&ability.can_target_enemies()) {
                            const auto info=ability.get<cambc::ability_cc>();
                            cast.kind=Kind::cc;cast.exclusive=info.exclusive_with_movement;
                            cast.range=tiles(info.range);cast.damages=info.modifier>0;cast.stuns=info.duration>0;
                        } else if(ability.is_dash()) {
                            const auto info=ability.get<cambc::ability_dash>();
                            cast.kind=Kind::dash;cast.exclusive=info.exclusive_with_movement;
                            cast.range=tiles(info.range);cast.radius=tiles(info.radius);
                            cast.damages=info.modifier>0;cast.stuns=info.duration>0;
                        }
                    }
                    opponents.push_back(observed);
                }
                bitboard danger;
                const auto p=me.position();
                if(h12_reset_pursuit::bank_returnability_mask({p.x,p.y},opponents,
                    [&](auto centre,int radius){danger|=around({centre.x,centre.y},radius);}))
                    return move_to(goals,&danger);
            }
        }
#endif
        return move_to(goals);
    }

    void farm(const std::vector<camp>& camps, const std::vector<champion>& allies) {
        trace("farm-entry");
        auto& me = cambc::self();
        const auto visible = cambc::vision::visible_tiles();
        const unsigned round = cambc::round_number();
        int selected = -1;
        float best_score = -1;
        int partial_target = -1;
        float partial_score = -1;
        bool selected_reward = false;
        const float strength = me.strength();
        const float dps = strength * 1.125f + me.focus() * 0.12f;
        const auto origin = me.position();
        const float health = me.health(), maximum = me.max_health();
        std::vector<heuristics::Job> jobs;
        heuristics::State snapshot;
        if constexpr(Policy::enabled){snapshot=state();jobs.reserve(groups_.size());}
        std::vector<float> observed_health(groups_.size(), 0), observed_strength(groups_.size(), 0);
        for (const auto& monster : camps) {
            const auto p = monster.spawn_position();
            const int group = group_at_tile_[p.y * cambc::map::width() + p.x];
            if (group >= 0 && monster.is_alive()) {
                observed_health[group] += monster.health();
                observed_strength[group] += monster.strength();
            }
        }
        std::vector<vec2> ally_positions;
        for (const auto& ally : allies) if (ally != me) ally_positions.push_back(ally.position());
        std::vector<int> scan_order;
#if RESET_MENU_DIAGNOSTIC
        int menu_eligible=0,menu_visited=0,menu_choice=-2;
        bool menu_budget_break=false;
        std::array<int,12> menu_stages{};
        const bool menu_capture=policy_.menu_diagnostic.wants(int(round),int(identity_));
        if(menu_capture)policy_.menu_diagnostic.begin(int(round),int(identity_),int(groups_.size()),int(cambc::fuel_remaining()),
            {policy_.current,policy_.progress_round,policy_.best_travel,policy_.best_hp});
        struct MenuCaptureExit {
            Bot* owner;const heuristics::State&state;const std::vector<heuristics::Job>&jobs;
            const std::vector<int>&order;const std::array<int,12>&stages;
            int&eligible;int&visited;bool&budget;int&choice;bool active;
            ~MenuCaptureExit(){if(active)owner->policy_.menu_diagnostic.finish(state,jobs,order,stages,eligible,visited,budget,choice,owner->target_,
                {owner->policy_.current,owner->policy_.progress_round,owner->policy_.best_travel,owner->policy_.best_hp},
                int(cambc::round_number()),int(cambc::fuel_remaining()),owner->groups_);
                if(active){owner->policy_.menu_diagnostic.frame.copy_end_round=int(cambc::round_number());
                    owner->policy_.menu_diagnostic.frame.copy_end_fuel=int(cambc::fuel_remaining());}}
        } menu_exit{this,snapshot,jobs,scan_order,menu_stages,menu_eligible,menu_visited,menu_budget_break,menu_choice,menu_capture};
#endif
        if constexpr(Policy::enabled) {
            std::vector<heuristics::Detail> detail;
            detail.reserve(groups_.size());
            for(std::size_t i=0;i<groups_.size();++i)if(groups_[i].reachable&&!groups_[i].monolith&&groups_[i].unavailable_until<=round
                &&!(has_non_monolith_gems_&&monster_shards_>=5&&jungle_shards_>=5&&groups_[i].group.gem_value()==0))
                detail.push_back({int(i),distance(origin,groups_[i].anchor),true,groups_[i].incoming<=0});
#if RESET_MENU_DIAGNOSTIC
            if(menu_capture){menu_eligible=int(detail.size());policy_.menu_diagnostic.capture_eligible(detail);}
#endif
            scan_order=heuristics::detail_order(std::move(detail),target_);
        }
        const auto count=Policy::enabled?scan_order.size():groups_.size();
        trace("farm-order",int(count));
        for (std::size_t slot = 0; slot < count; ++slot) {
            const std::size_t i=Policy::enabled?std::size_t(scan_order[slot]):slot;
            // Reserve fuel for movement/combat and yielding. Exhaustion resumes
            // midway through an old decision next round, so stop voluntarily.
#if RESET_MENU_DIAGNOSTIC
            if(menu_capture){menu_visited=int(slot)+1;menu_stages[slot]=1;}
#endif
            if (cambc::fuel_remaining() < 55000)
#if RESET_MENU_DIAGNOSTIC
            {if(menu_capture){menu_budget_break=true;menu_stages[slot]=2;}break;}
#else
                break;
#endif
            auto& entry = groups_[i];
            if constexpr(Policy::enabled)if(!entry.reachable)
#if RESET_MENU_DIAGNOSTIC
            {if(menu_capture)menu_stages[slot]=3;continue;}
#else
                continue;
#endif
            if (entry.monolith || entry.unavailable_until > round)
#if RESET_MENU_DIAGNOSTIC
            {if(menu_capture)menu_stages[slot]=4;continue;}
#else
                continue;
#endif
            // Gold establishes the first two emblems. Once equipped, pursue
            // score rather than farming safe gold clusters indefinitely. Maps
            // whose only gems are in monolith groups retain the gold tiebreak.
            if (has_non_monolith_gems_ && monster_shards_ >= 5 && jungle_shards_ >= 5
                && entry.group.gem_value() == 0)
#if RESET_MENU_DIAGNOSTIC
            {if(menu_capture)menu_stages[slot]=5;continue;}
#else
                continue;
#endif
            const bool seen = !(entry.spawns & visible).is_empty();
            if constexpr(Policy::enabled)if(seen)entry.observed_round=int(round);
            bool reward = false;
            if (seen) {
                const auto state = entry.group.state();
                if (const auto* respawn = std::get_if<cambc::map::camp_group_respawning>(&state)) {
                    entry.unavailable_until = round + std::max(1, respawn->timer);
                    entry.frontier_memory.reset();
#if RESET_MENU_DIAGNOSTIC
                    if(menu_capture)menu_stages[slot]=6;
#endif
                    continue;
                }
                reward = std::holds_alternative<cambc::map::camp_group_reward_available>(state);
                // Hold a cleared camp through all five capture phases. Do not
                // start the altar journey merely because we carry older gems.
                if (reward && entry.spawns.is_set(origin)) {
                    if constexpr(Policy::enabled) {
                        const bool contested=std::any_of(enemy_positions_.begin(),enemy_positions_.end(),[&](vec2 p){return entry.spawns.is_set(p);});
                        if(contested){entry.unavailable_until=round+12;
#if RESET_MENU_DIAGNOSTIC
                            if(menu_capture)menu_stages[slot]=7;
#endif
                            continue;}
                    }
#if RESET_MENU_DIAGNOSTIC
                    if(menu_capture)menu_stages[slot]=8;
#endif
                    target_ = static_cast<int>(i); return;
                }
            }
            float remaining_health = entry.health;
            float remaining_incoming = entry.incoming;
            // Once all spawn tiles are visible, use remaining monsters rather
            // than repeatedly budgeting a full-health fight after every hit.
            if (seen && (entry.spawns & ~visible).is_empty()) {
                remaining_health = observed_health[i];
                remaining_incoming = observed_strength[i];
            }
            const float incoming = remaining_incoming * (jungle_shards_ >= 5 ? 0.7f : 1.0f);
            const bool affordable = reward || fundamentals::affordable_camp(health, maximum,
                remaining_health, incoming, dps * (monster_shards_ >= 5 ? 1.1f : 1.0f));
            float travel = static_cast<float>(distance(origin, entry.anchor));
            // Visible allies already farming/capturing discourage splitting
            // resources; a small deterministic bias spreads the opening.
            for (const auto p : ally_positions)
                if (distance(p, entry.anchor) <= 3) travel += 12;
            travel += static_cast<float>((i * 3 + identity_ * 11) % 7);
            const float value = entry.group.gem_value() * 2.0f + entry.group.gold_value() * 0.4f;
            float score = value / (travel + (reward ? 0 : remaining_health / std::max(1.0f, dps)) + 10);
            if (target_ == static_cast<int>(i)) score *= 1.2f;
            if constexpr(Policy::enabled) {
                heuristics::Job j;
                j.id=int(i);j.travel=distance(origin,entry.anchor);j.gems=entry.group.gem_value();
                j.gold=entry.group.gold_value();j.ready=reward;j.known=seen&&(entry.spawns&~visible).is_empty();
                j.age=entry.observed_round<0?int(round)+1:int(round)-entry.observed_round;
                j.committed=target_==int(i);j.incoming=incoming;j.remaining_hp=remaining_health;
                j.original_hp=entry.health;j.work=reward?0:remaining_health/std::max(1.f,dps);
                j.loss=incoming*(j.work+2);j.affordable=affordable;
                for(const auto p:ally_positions)j.allies+=distance(p,entry.anchor)<=3;
                j.altar=10000;for(const auto a:altars_)j.altar=std::min(j.altar,std::max(0,distance(entry.anchor,a)-1));
                // Private visible-rival features are cached once per farm call.
                for(const auto p:enemy_positions_)j.enemy_eta=std::min(j.enemy_eta,distance(p,entry.anchor));
                j.home=distance(entry.anchor,home_);
                jobs.push_back(j);
#if RESET_MENU_DIAGNOSTIC
                if(menu_capture)menu_stages[slot]=j.affordable?9:10;
#endif
            }
            if (!affordable) {
                // Damage persists while camps sleep. If there is no affordable
                // full clear, make progress in several attack/recall trips.
                // Stay on a chosen attempt until the reactive retreat policy
                // fires, instead of abandoning it after the first damage tick.
                if ((health >= maximum * 0.75f || target_ == static_cast<int>(i)) && score > partial_score) {
                    partial_target = static_cast<int>(i);
                    partial_score = score;
                }
                continue;
            }
            if (score > best_score) { best_score = score; selected = static_cast<int>(i); selected_reward = reward; }
        }
        if constexpr(Policy::enabled) {
            const int baseline=selected>=0?selected:partial_target;
#if RESET_MENU_DIAGNOSTIC
            if(menu_capture)policy_.menu_diagnostic.frame.fuel_before_choose=int(cambc::fuel_remaining());
#endif
            const int choice=heuristics::choose(policy_,snapshot,jobs);
#if RESET_MENU_DIAGNOSTIC
            if(menu_capture){menu_choice=choice;policy_.menu_diagnostic.frame.fuel_after_choose=int(cambc::fuel_remaining());}
#endif
            trace("farm-jobs",int(jobs.size()));
            ++selections_;deviations_+=choice!=baseline;
            selected=-1;partial_target=-1;selected_reward=false;
            for(const auto&j:jobs)if(j.id==choice){
                if(j.affordable){selected=choice;selected_reward=j.ready;}
                else if(health>=maximum*.75f||j.committed)partial_target=choice;
            }
            if(round%128==identity_%128&&cambc::fuel_remaining()>22000)
                std::printf("h12 select actor=%u round=%u target=%d calls=%u changed=%u refresh=%u spend=%u different=%u\n",identity_,round,choice,selections_,deviations_,reset_refreshes_,reset_spends_,retarget_hits_);
        }
        partial_clear_ = selected < 0 && partial_target >= 0;
        trace("farm-choice",selected<0?partial_target:selected);
        if (partial_clear_) selected = partial_target;
        target_ = selected;
        if (selected < 0) {
            if (!cambc::map::spawn_region(team::ally).is_set(me.position())) recovering_ = true;
            return;
        }
        auto& entry = groups_[selected];
        if (selected_reward) {
            if (!move_to(entry.spawns)&&(!Policy::enabled||cambc::fuel_remaining()>45000)) entry.unavailable_until = round + 3;
            return;
        }
        std::optional<camp> target;
        const int route_distance=distance(origin,entry.anchor);
        if(route_target_!=selected){route_target_=selected;route_best_=route_distance;route_progress_=round;}
        else if(route_distance<route_best_){route_best_=route_distance;route_progress_=round;}
        float weakest = std::numeric_limits<float>::max();
        for (const auto& monster : camps)
            if (entry.spawns.is_set(monster.spawn_position()) && monster.is_alive()) {
                const auto score = monster.health() + distance(me.position(), monster.position())
                    * (partial_clear_ ? 1000.0f : 100.0f);
                if (score < weakest) { weakest = score; target = monster; }
            }
        if(target) {
            std::size_t member=0;
            for(const auto&m:entry.group){if(m==*target){entry.frontier_memory.note_alive(member,round,entry.group.max_respawn_timer());break;}++member;}
            if(fight(*target, false, entry.incoming > 0))return;
        }
        if(round-route_progress_>=4&&me.can_attack()&&cambc::fuel_remaining()>30000) {
            const auto mask=me.attack_tiles();
            const bool own_work=target&&mask.is_set(target->position());
            for(const auto&blocker:camps) {
                if(own_work)break;
                const auto p=blocker.spawn_position();
                const int group=group_at_tile_[p.y*cambc::map::width()+p.x];
                if(group<0||group==selected||groups_[group].incoming>0)continue;
                if(!mask.is_set(p))continue;
                bool splash_safe=true;
                if(identity_==static_cast<unsigned>(cambc::champion_type::varg))
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                        const int x=p.x+dx,y=p.y+dy;
                        if(x<0||y<0||x>=cambc::map::width()||y>=cambc::map::height())continue;
                        const int neighbour=group_at_tile_[y*cambc::map::width()+x];
                        if(neighbour>=0&&groups_[neighbour].incoming>0)splash_safe=false;
                    }
                if(h12_reset_pursuit::clear_route_blocker(round-route_progress_,own_work,true,
                    groups_[group].incoming,true,splash_safe)&&blocker.is_alive()) {
                    me.attack(blocker);
                        ++route_actions_;
                        if(route_actions_<=4&&cambc::fuel_remaining()>15000)
                            std::printf("h12route actor=%u round=%u source=%d blocker=%d action=%u\n",identity_,round,selected,group,route_actions_);
                        return;
                }
                if(cambc::fuel_remaining()<28000)break;
            }
        }
#if HEURISTICS_DIAGNOSTIC
        diagnostic_mode_=target?1:2;
        if(target)diagnostic_target_=target->position();
#endif
        bitboard goals;
        if (target) goals = around(target->position(), me.attack_range());
        else {
            // A partly cleared long group can have no living member in our
            // current vision. Revisit unseen members, not already-cleared pads:
            // the latter makes all workers circle indefinitely inside a group.
            std::vector<h12_reset_pursuit::Member> members;
            members.reserve(entry.group.camps().size());
            for(const auto&monster:entry.group) {
                const auto p=monster.spawn_position();
                members.push_back({p.x,p.y,visible.is_set(p)});
            }
            const auto frontier=h12_reset_pursuit::goal_frontier(entry.frontier_memory,members,
                !(entry.spawns&visible).is_empty()&&!selected_reward,round,entry.group.max_respawn_timer());
#if HEURISTICS_DIAGNOSTIC
            diagnostic_goals_=frontier.goals.size();
            if(!frontier.goals.empty())diagnostic_target_={frontier.goals[0].x,frontier.goals[0].y};
#endif
            for(const auto&m:frontier.goals)goals.set({m.x,m.y});
            if(frontier.discover)goals=h12_reset_pursuit::approach_perimeter(goals);
        }
        if constexpr(Policy::enabled)goals.set(origin,false);
        if (move_to(goals)) {
            if constexpr(Policy::enabled)if(cambc::fuel_remaining()<30000)return;
            if(!me.can_attack())return;
            // Movement can reveal the group behind a bush or wall.
            const auto attack_mask=me.attack_tiles();
            for (const auto& monster : cambc::vision::camps())
                if ((!Policy::enabled||cambc::fuel_remaining()>26000)&&entry.spawns.is_set(monster.spawn_position()) && monster.is_alive()
                    && attack_mask.is_set(monster.position())) {
                    fight(monster, true, entry.incoming > 0);
                    break;
                }
        } else if (me.can_move()&&(!Policy::enabled||cambc::fuel_remaining()>45000)) entry.unavailable_until = round + 4;
    }

public:
    void diagnostic() const {
#if HEURISTICS_DIAGNOSTIC
        const unsigned round=cambc::round_number();
        if(round>=80&&round<=90&&cambc::fuel_remaining()>15000) {
            const auto p=cambc::self().position();
            const auto a=target_>=0?groups_[target_].anchor:vec2{};
            const auto dead=target_>=0?std::count(groups_[target_].frontier_memory.dead.begin(),groups_[target_].frontier_memory.dead.end(),1):0;
            std::printf("h12target actor=%u round=%u group=%d x=%d y=%d ax=%d ay=%d mode=%d tx=%d ty=%d calls=%u rec=%d goals=%d dead=%d fail=%d admission=%d left=%d\n",identity_,round,target_,p.x,p.y,a.x,a.y,diagnostic_mode_,diagnostic_target_.x,diagnostic_target_.y,selections_,int(recovering_),diagnostic_goals_,int(dead),diagnostic_failure_,diagnostic_move_fuel_,int(cambc::fuel_remaining()));
        }
#endif
    }
    void turn() {
#if RESET_MENU_DIAGNOSTIC
        if(policy_.menu_diagnostic.frame.captured&&!policy_.menu_diagnostic.flushed
            &&policy_.menu_diagnostic.flush(int(cambc::round_number()),int(cambc::fuel_remaining())))return;
#endif
        auto& me = cambc::self();
        if (!initialized_) initialize();
        if constexpr(Policy::enabled) {
            if(!topology_ready_) {
                // A separate bounded setup turn avoids stacking a native flood
                // on map metadata and gameplay work. No mobility is assumed.
                if(cambc::fuel_remaining()<65000)return;
                cambc::distance_field field;bitboard reached;
                field.compute(terrain_,cambc::map::spawn_region(team::ally),reached);
                for(auto&g:groups_)g.reachable=!(g.spawns&reached).is_empty();
                topology_ready_=true;
                return;
            }
        }
        // Dead champions retain shopping turns, but have no usable position.
        if (!me.is_alive()) { shop(); recovering_ = false; target_ = -1; return; }
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) return;
        if (!me.can_move() && !me.can_attack() && !me.can_cast()) return;
        shop();
        const bool spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
        if (spawn) {
            recovering_ = false;
            // Recovery happens at turn end; leaving on recall arrival prevents it.
            if (me.health() < me.max_health()) return;
        }
        const auto camps = cambc::vision::camps();
        const auto enemies = cambc::vision::champions(team::enemy);
#if RESET_VISIBLE_SAFETY
        if constexpr(Policy::enabled)observe_threats(enemies);
#endif
        bool leave_contest=false;
        if (cambc::fuel_remaining() < 30000) return;
        float incoming = 0;
        for (const auto& monster : camps)
            if (monster.is_alive() && monster.is_awake()
                && distance(me.position(), monster.position()) <= monster.attack_range()) incoming += monster.strength();
        for (const auto& enemy : enemies)
            if (distance(me.position(), enemy.position()) <= enemy.attack_range() + 1) incoming += enemy.strength();
#if RESET_VISIBLE_SAFETY
        if constexpr(Policy::enabled)if(safety_active_) {
            float observed_basic=0;
            for(const auto&enemy:enemies)if(distance(me.position(),enemy.position())<=enemy.attack_range()+1)
                observed_basic+=enemy.strength();
            incoming-=observed_basic;
            const auto p=me.position();
            incoming+=h12_reset_pursuit::visible_exposure(visible_threats_,{p.x,p.y});
        }
#endif
        // Partial clears reserve roughly four incoming camp phases for escape;
        // ordinary safe fights retain the less conservative baseline threshold.
        const bool retreat=!spawn&&[&] {
            if constexpr(Policy::enabled)return policy_.retreat(me.health(),me.max_health(),incoming*(partial_clear_?2.f:1.f),me.gem_count());
            else return fundamentals::should_retreat(me.health(), me.max_health(),incoming * (partial_clear_ ? 2.0f : 1.0f));
        }();
        if (!spawn && retreat) recovering_ = true;
        if (recovering_) { recover(camps, enemies); return; }
        // Cleared camp ownership takes precedence over banking old gems.
        if (target_ >= 0) {
            auto& entry = groups_[target_];
            if (entry.spawns.is_set(me.position()) && std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state())) {
                if constexpr(!Policy::enabled)return;
                else {
                    bool contested=false,can_hit=false;
                    for(const auto&e:enemies)if(entry.spawns.is_set(e.position())){contested=true;can_hit|=distance(me.position(),e.position())<=me.attack_range();}
                    if(!contested){contest_since_=-1;return;}
                    const int round=cambc::round_number();if(contest_since_<0)contest_since_=round;
                    const auto action=heuristics::capture_action(true,can_hit,round-contest_since_);
                    if(action==heuristics::CaptureAction::engage)
                        for(const auto&e:enemies)if(entry.spawns.is_set(e.position())&&fight(e,false,true))return;
                    entry.unavailable_until=round+12;target_=-1;leave_contest=true;contest_since_=-1;
                }
            }
        }
#if RESET_BANK_RETURNABILITY
        if (bank(enemies)) return;
#else
        if (bank()) return;
#endif
        // Defend in place; this never adds pursuit movement or replaces the reset flow.
#if RESET_RANGED_DEFENCE
        if (!leave_contest && h12_reset_pursuit::defend_in_place(enemies, me.attack_range(),
                [&](const auto& enemy) { return distance(me.position(), enemy.position()); },
                [&](const auto& enemy) { return fight(enemy, false, true); })) return;
#else
        for (const auto& enemy : enemies)
            if (!leave_contest&&distance(me.position(), enemy.position()) <= 1 && fight(enemy, false, true)) return;
#endif
        if (!spawn && me.champion_name() != cambc::champion_type::tavi && me.gold_count() >= 300) {
            recovering_ = true;
            recover(camps, enemies);
            return;
        }
        if constexpr(Policy::enabled){enemy_positions_.clear();for(const auto&e:enemies)enemy_positions_.push_back(e.position());}
        farm(camps, cambc::vision::champions(team::ally));
    }
};

template<class Policy> int run() {
    Bot<Policy> bot;
    while (true) {
        try {
            if (cambc::is_draft()) draft<Policy>();
            else {bot.turn();bot.diagnostic();}
        } catch (const std::exception& error) {
            std::cout << "fundamentals error: " << error.what() << '\n';
        } catch (...) {
            std::cout << "fundamentals error: unknown exception\n";
        }
        cambc::end_turn();
    }
}

} // namespace h12_reset_runtime
