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
#include "../../common/src/draft.h"
#include "../../common/src/equipment.h"
#include "../../common/src/capture.h"

namespace h12_shield_runtime {
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
    const auto& order=heuristics_draft::choices;
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
    std::vector<vec2> positions;
    
    unsigned unavailable_until = 0;
    int observed_round=-1;
    bool reachable=true;
    h12_shield_window::FrontierMemory frontier_memory{};
};

template<class Policy> class Bot {
    Policy policy_;
    unsigned trace_round_=0;
    unsigned selections_=0, deviations_=0;
    std::vector<vec2> enemy_positions_;
    vec2 home_{};
    bool topology_ready_=false;
    int contest_since_=-1;
    heuristics::CaptureWatch capture_watch_;
    int route_target_=-1,route_progress_=0,route_best_=10000;
    unsigned route_actions_=0;
    unsigned shield_logs_=0,work_logs_=0,phase_logs_=0,cargo_logs_=0;
    int previous_bag_=0;
    bool pending_submit_=false;
    float witnessed_shield_=0,witnessed_hp_=0;
    int witnessed_round_=-1;
    std::vector<Group> groups_;
    std::vector<cambc::map::camp_group> pending_groups_;
    std::size_t metadata_next_=0;
    bool metadata_started_=false;
    std::optional<Group> metadata_group_;
    std::size_t metadata_member_=0;
    std::vector<int> group_at_tile_;
    std::vector<vec2> altars_;
    bitboard altar_goals_;
    bitboard terrain_;
    bitboard remembered_camps_;
    bool initialized_ = false;
    bool recovering_ = false;
    bool partial_clear_ = false;
    bool has_non_monolith_gems_ = false;
    int target_ = -1;
    unsigned monster_shards_ = 0, jungle_shards_ = 0;
    unsigned identity_ = 0;
#if HEURISTICS_DIAGNOSTIC
    int diagnostic_mode_=0;
    int diagnostic_goals_=0;
    int diagnostic_failure_=0,diagnostic_move_fuel_=0;
    vec2 diagnostic_target_{};
#endif
    void trace(const char*stage,int value=-1) const {
        if constexpr(requires{Policy::trace;})if constexpr(Policy::trace) {
            if(identity_==6&&trace_round_>=72&&trace_round_<=77&&cambc::fuel_remaining()>10000)
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
        if(!metadata_started_) {
            terrain_ = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
                | cambc::map::spawn_region(team::enemy);
            altars_ = cambc::map::altars().positions();
            group_at_tile_.assign(cambc::map::width() * cambc::map::height(), -1);
            pending_groups_=cambc::map::camp_groups();
            identity_=static_cast<unsigned>(cambc::self().champion_name());
            home_=cambc::map::spawn_region(team::ally).positions().front();
            metadata_started_=true;
            return;
        }
        // Public metadata is built over bounded setup turns. Even a large map
        // cannot stack every group, geometry sample and topology flood at once.
        unsigned completed=0,members=0;
        while(metadata_next_<pending_groups_.size()&&completed<6&&members<24&&cambc::fuel_remaining()>50000) {
            if(!metadata_group_) {
                const auto&group=pending_groups_[metadata_next_];
                metadata_group_.emplace(Group{group,bitboard{},group.camps().front().spawn_position()});
                metadata_member_=0;
            }
            auto&entry=*metadata_group_;
            const auto roster=entry.group.camps();
            while(metadata_member_<roster.size()&&members<24&&cambc::fuel_remaining()>50000) {
                const auto&monster=roster[metadata_member_++];++members;
                const auto p=monster.spawn_position();
                entry.positions.push_back(p);entry.spawns.set(p);
                group_at_tile_[p.y*cambc::map::width()+p.x]=static_cast<int>(groups_.size());
                const auto stats=monster.base_stats();
                entry.health+=stats.base_health;entry.incoming+=stats.base_strength;
                entry.monolith|=monster.is_monolith();
            }
            if(metadata_member_<roster.size())break;
            has_non_monolith_gems_|=!entry.monolith&&entry.group.gem_value()>0;
            groups_.push_back(std::move(entry));metadata_group_.reset();
            ++metadata_next_;++completed;
        }
        initialized_=metadata_next_==pending_groups_.size();
    }

    void shop() {
        auto&me=cambc::self();
        for(int stage=0;stage<3;++stage) {
            const auto plan=Policy::purchase(identity_,monster_shards_,jungle_shards_);
            using P=heuristics::Purchase;
            auto item=item_type::power_shard_of_strength;
            unsigned affiliation=2;
            switch(plan) {
                case P::monster_strength:item=item_type::monster_shard_of_strength;affiliation=0;break;
                case P::monster_focus:item=item_type::monster_shard_of_focus;affiliation=0;break;
                case P::monster_health:item=item_type::monster_shard_of_health;affiliation=0;break;
                case P::jungle_health:item=item_type::jungle_shard_of_health;affiliation=1;break;
                case P::jungle_focus:item=item_type::jungle_shard_of_focus;affiliation=1;break;
                case P::jungle_strength:item=item_type::jungle_shard_of_strength;affiliation=1;break;
                case P::power_focus:item=item_type::power_shard_of_focus;break;
                case P::power_health:item=item_type::power_shard_of_health;break;
                case P::power_strength:break;
            }
            const unsigned limit=affiliation==0?5-monster_shards_:affiliation==1?5-jungle_shards_:100;
            const unsigned count=std::min(limit,me.gold_count()/item.cost());
            if(!count||!me.can_buy(item,count))return;
            me.buy(item,count);
            if(affiliation==0)monster_shards_+=count;
            if(affiliation==1)jungle_shards_+=count;
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
                const auto d = field.get(p);
                if (d < best_distance) { best = p; best_distance = d; }
            }
            if (best == me.position()) return failed(5);
            if (me.move_absolute(best)) return true;
            if constexpr(Policy::enabled)walls.set(best);
        }
        return failed(6);
    }

    bool ordinary_camp_fight(const camp&target,bool walked) {
        auto&me=cambc::self();
        if(cambc::fuel_remaining()<30000||!target.is_visible()||!target.is_alive())return false;
        const auto p=target.spawn_position();bool acted=false;
        if(me.can_cast())for(const auto&a:me.abilities()) {
            if(cambc::fuel_remaining()<28000)break;
            if(!a.is_ready()||!a.can_target_camps()||!(a.is_damage()||a.is_cc()))continue;
            const bool exclusive=std::visit([](const auto&v){return v.exclusive_with_movement;},a.info().value());
            if(walked&&exclusive)continue;
            if(a.valid_tiles().is_set(p)){acted=a.use(target);break;}
        }
        if(cambc::vision::in_vision(p)&&!target.state().is_dead()&&target.is_visible()&&me.can_attack()) {
            if(me.attack_tiles().is_set(p)){me.attack(target);acted=true;}
        }
        if(acted)route_progress_=cambc::round_number();
        return acted;
    }
    std::optional<cambc::ability> shield_ability()const {
        for(const auto&a:cambc::self().abilities())if(a.is_shield()&&a.can_target_self())return a;
        return {};
    }
    float camp_reduction()const {
        return jungle_shards_>=5?1-item_type::jungle_emblem_of_focus.passive_magnitude():1;
    }
    void update_finance() {
        auto&me=cambc::self();const auto bag=me.gem_count();
        if(int(bag)!=previous_bag_&&cargo_logs_<8&&cambc::fuel_remaining()>60000) {
            ++cargo_logs_;
            std::printf("h12cargo actor=%u round=%u event=%s bag=%d>%u source=%d phase=%d\n",identity_,cambc::round_number(),
                int(bag)>previous_bag_?"gain":pending_submit_&&me.is_alive()&&bag==0?"bank":"loss",previous_bag_,bag,
                policy_.window.source,int(policy_.window.phase));
        }
        if(int(bag)<previous_bag_){target_=-1;route_target_=-1;policy_.current=-1;}
        previous_bag_=bag;pending_submit_=me.is_alive()&&me.is_submitting_gems();
        policy_.forced_bank=false;policy_.shield_ready=false;policy_.predicted_shield=0;
        if(!me.is_alive())return;
        if(const auto a=shield_ability()) {
            policy_.shield_ready=me.can_cast()&&a->is_ready();
            if(policy_.shield_ready)policy_.predicted_shield=a->shield_amount();
        }
        if(!bag)return;
        int altar=10000;const auto origin=me.position();
        for(const auto p:altars_)altar=std::min(altar,std::max(0,distance(origin,p)-1));
        const auto score=cambc::global_submitted_gems(team::ally),goal=cambc::constants::winning_gems;
        policy_.forced_bank=score>=goal||bag>=goal-score||heuristics::deadline(state(altar));
    }
    struct ShieldWorld {
        struct Threat {vec2 position;int range;float strength;};
        Bot*owner;camp target;std::vector<Threat> threats;bool escape=false;int escape_steps=2;
        std::optional<cambc::ability> shield;
        std::optional<bitboard> attack_mask;
        float before=0;bool made_progress=false;
        ShieldWorld(Bot*o,const camp&t):owner(o),target(t),shield(o->shield_ability()) {
            const auto origin=cambc::self().position();
            const auto&entry=owner->groups_[owner->target_];
            // Position/design/stat queries are cached once for this unchanged
            // action epoch. A self-shield cast cannot change these camp stats.
            for(const auto&c:cambc::vision::camps())if(c.is_alive()&&(entry.spawns.is_set(c.spawn_position())||c.is_awake())) {
                const float strength=c.strength();if(strength>0)threats.push_back({c.spawn_position(),c.attack_range(),strength});
            }
            // A ranged camp may need several steps to leave. Local clearance
            // must improve, and independent actual HP funds those exit phases
            // plus the boundary turn. Immediate damage need not fall in one step.
            auto walls=owner->terrain_|cambc::vision::champion_tiles()|cambc::vision::camp_tiles();walls.set(origin,false);
            auto clearance=[&](vec2 p){float n=0;for(const auto&c:threats)n+=std::min(distance(p,c.position),c.range+2)*c.strength;return n;};
            const float current=clearance(origin);
            for(const auto&c:threats)escape_steps=std::max(escape_steps,c.range+2-distance(origin,c.position));
            for(const auto p:walls.reachable_tiles(origin,1).positions())if(clearance(p)>current){escape=true;break;}
        }
        h12_shield_window::Observation observe() {
            using namespace h12_shield_window;Observation o;auto&me=cambc::self();
            o.round=cambc::round_number();o.source=owner->target_;o.bag=me.gem_count();o.hp=me.health();o.maximum=me.max_health();
            const auto p=target.spawn_position();const auto visible=cambc::vision::visible_tiles();
            o.visible=visible.is_set(p)&&!target.state().is_dead()&&target.is_visible();
            o.fresh=(owner->groups_[owner->target_].spawns&~visible).is_empty();o.escape=escape;o.escape_steps=escape_steps;
            o.forced=owner->policy_.forced_bank;o.cast=me.can_cast();
            if(me.can_attack()&&!attack_mask)attack_mask=me.attack_tiles();
            o.basic=me.can_attack()&&attack_mask&&attack_mask->is_set(p);
            const auto origin=me.position();
            for(const auto&c:threats)if(distance(origin,c.position)<=c.range+1)o.incoming+=c.strength;
            o.incoming*=owner->camp_reduction();
            o.ready=shield&&shield->is_ready()&&shield->can_target_self();
            if(o.ready)o.predicted=shield->shield_amount();
            const auto buffs=me.buffs();if(buffs.has_shield()){const auto b=buffs.get<cambc::buff_shield>();o.shield=b.value;o.duration=b.duration;}
            return o;
        }
        bool cast_shield() {
            auto&me=cambc::self();if(!shield||!me.can_cast()||!shield->is_ready()||!shield->can_target_self()||!shield->use())return false;
            const auto buffs=me.buffs();float value=0;int duration=0;
            if(buffs.has_shield()){const auto b=buffs.get<cambc::buff_shield>();value=b.value;duration=b.duration;}
            if(owner->shield_logs_<6&&cambc::fuel_remaining()>25000) {
                ++owner->shield_logs_;std::printf("h12shield actor=%u round=%u source=%d event=cast value=%.1f duration=%d hp=%.1f target=%.1f x=%d y=%d\n",owner->identity_,cambc::round_number(),owner->target_,value,duration,me.health(),target.health(),target.spawn_position().x,target.spawn_position().y);
            }
            owner->witnessed_shield_=value;owner->witnessed_hp_=me.health();owner->witnessed_round_=cambc::round_number();
            return true;
        }
        bool attack() {
            auto&me=cambc::self();const auto p=target.spawn_position();
            if(cambc::fuel_remaining()<14000||!cambc::vision::in_vision(p)||target.state().is_dead()||!target.is_visible()||!me.can_attack())return false;
            if(!attack_mask||!attack_mask->is_set(p))return false;
            before=target.health();me.attack(target);attack_mask.reset();
            const bool dead=target.state().is_dead();const float after=dead?0:target.health();made_progress=after<before;
            if(made_progress)owner->route_progress_=cambc::round_number();
            if(owner->work_logs_<8&&cambc::fuel_remaining()>14000){++owner->work_logs_;
                const auto buffs=me.buffs();float value=0;int duration=0;if(buffs.has_shield()){const auto b=buffs.get<cambc::buff_shield>();value=b.value;duration=b.duration;}
                std::printf("h12window actor=%u round=%u source=%d event=work hp=%.1f>%.1f shield=%.1f duration=%d selfhp=%.1f dead=%d\n",owner->identity_,cambc::round_number(),owner->target_,before,after,value,duration,me.health(),int(dead));}
            return true;
        }
        bool progressed()const{return made_progress;}
    };
    bool shield_fight(const camp&target,bool walked) {
        auto&me=cambc::self();
        if(target_<0||!target.is_visible()||!target.is_alive()||cambc::fuel_remaining()<45000)return false;
        if(identity_==10) {
            // Refuse cross-source awakening by Varg's basic splash. Other
            // already-awake visible camps enter the window's actual reserve.
            const auto p=target.spawn_position();
            const int radius=int(std::ceil(cambc::constants::passive_abilities::varg_splash_radius));
            for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx) {
                const int x=p.x+dx,y=p.y+dy;
                if(x<0||y<0||x>=cambc::map::width()||y>=cambc::map::height())continue;
                const int other=group_at_tile_[y*cambc::map::width()+x];
                if(other>=0&&other!=target_&&groups_[other].incoming>0) {
                    groups_[target_].unavailable_until=cambc::round_number()+8;
                    target_=-1;route_target_=-1;policy_.current=-1;policy_.window.exit(cambc::round_number());
                    return true;
                }
            }
        }
        // Harmless or full-clear work by unshielded roles uses a normal fallback.
        // Shield roles have a separate bounded physical-work controller.
        if((identity_!=12&&identity_!=10)||groups_[target_].incoming<=0)return ordinary_camp_fight(target,walked);
        // Movement/reveal and window preparation are separate bounded epochs.
        // Keep the source lease, then cast/work with fresh fuel next owner turn.
        if(walked)return false;
        if(!me.can_attack()||distance(me.position(),target.spawn_position())>me.attack_range())return false;
        ShieldWorld world(this,target);
        const auto result=h12_shield_window::execute(policy_.window,world);
        if(result.withdraw&&phase_logs_<10&&cambc::fuel_remaining()>20000){++phase_logs_;
            const auto o=world.observe();std::printf("h12window actor=%u round=%u source=%d event=withdraw shield=%.1f duration=%d hp=%.1f incoming=%.1f pulses=%d\n",identity_,o.round,target_,o.shield,o.duration,o.hp,o.incoming,policy_.window.pulses);}
        return result.acted||result.withdraw;
    }
    bool withdraw_window(const std::vector<camp>&camps,const std::vector<champion>&enemies) {
        using namespace h12_shield_window;
        if(policy_.window.phase!=Phase::withdraw)return false;
        auto&me=cambc::self();bool outside=true;const auto origin=me.position();
        for(const auto&c:camps)if(c.is_alive()&&c.is_awake()&&c.strength()>0&&distance(origin,c.spawn_position())<=c.attack_range()+1)outside=false;
        for(const auto&e:enemies)if(distance(origin,e.position())<=e.attack_range()+2)outside=false;
        if(!outside){recover(camps,enemies);return true;}
        policy_.window.outside(cambc::round_number());
        if(phase_logs_<10&&cambc::fuel_remaining()>40000){++phase_logs_;std::printf("h12window actor=%u round=%u source=%d event=renew hp=%.1f shieldready=%d\n",identity_,cambc::round_number(),policy_.window.source,me.health(),int(policy_.shield_ready));}
        target_=-1;route_target_=-1;policy_.current=-1;
        if(me.health()<me.max_health()*.75f){recovering_=true;recover(camps,enemies);return true;}
        return false;
    }

    template<class Target>bool fight(const Target&target,bool walked,bool dangerous) {
        if constexpr(std::is_same_v<Target,camp>)return shield_fight(target,walked);
        else {
            auto&me=cambc::self();
            if(cambc::fuel_remaining()<22000||!target.is_visible()||!target.is_alive())return false;
            bool acted=false;
            if(!walked&&me.can_cast())for(const auto&a:me.abilities())
                if((a.is_damage()||a.is_cc())&&a.can_target_enemies()&&a.is_ready()&&a.valid_tiles().is_set(target.position())){acted=a.use(target);break;}
            if(!target.is_visible()||!target.is_alive())return acted;
            if(me.can_attack()&&me.attack_tiles().is_set(target.position())) {
                if(dangerous&&me.can_cast())for(const auto&a:me.abilities())
                    if(a.is_shield()&&a.can_target_self()&&a.is_ready()){acted|=a.use();break;}
                me.attack(target);acted=true;
            }
            return acted;
        }
    }

    bool recover(const std::vector<camp>& camps, const std::vector<champion>& enemies) {
        auto& me = cambc::self();
        const auto origin = me.position();
        bool safe = true;
        bitboard danger;
        struct Threat {vec2 position;int range;float strength;};
        std::vector<Threat> threats;
        for (const auto& monster : camps) {
            const auto p=monster.spawn_position();
            if(distance(origin,p)>8||!monster.is_alive()||!monster.is_awake())continue;
            const float strength=monster.strength();if(strength<=0)continue;
            const int range=monster.attack_range();threats.push_back({p,range,strength});
            danger|=around(p,range);
            if(distance(origin,p)<=range+1)safe=false;
        }
        std::vector<vec2> opponents;
        for (const auto& enemy : enemies) {
            const auto p=enemy.position();opponents.push_back(p);
            danger |= around(p, enemy.attack_range() + 1);
            if (distance(origin,p) <= 8) safe = false;
        }
        if (safe) { me.recall(); return true; }
        // First get out of attack range. Maximise clearance locally if danger
        // surrounds us; a path whose starting tile is threatened may be blocked.
        auto legal = blocked().reachable_tiles(origin, 1);
        auto best = origin;
        float best_score = -1e30f;
        for (const auto p : legal.positions()) {
            float score = 0;
            for (const auto& monster : threats)
                score += std::min(distance(p, monster.position),monster.range+2)*monster.strength;
            for (const auto q : opponents) score += std::min(distance(p,q),9)*150;
            if (score > best_score) { best = p; best_score = score; }
        }
        if (best != origin && me.can_move() && me.move_absolute(best)) return true;
        move_to(cambc::map::spawn_region(team::ally), &danger);
        return true;
    }

    bool bank() {
        trace("bank-entry");
        auto& me = cambc::self();
        if (!me.gem_count() || altars_.empty()) return false;
        int nearest = 10000;const auto origin=me.position();
        for (const auto altar : altars_) nearest = std::min(nearest, distance(origin, altar));
        const bool should_bank=[&] {
            if constexpr(Policy::enabled)return policy_.bank(state(std::max(0,nearest-1)));
            else return fundamentals::should_bank(me.gem_count(), cambc::round_number(), nearest);
        }();
        if (!should_bank) return false;
        for (const auto altar : altars_) {
            if (distance(origin, altar) <= 1) {
                // Opposing locks take the queried engine hit count (five in this bundle). Friendly locks
                // allow tributing. Only use a fresh visible lock observation.
                for (const auto& lock : cambc::vision::altar_locks(team::enemy))
                    if (lock.position() == altar) {
                        if (me.can_attack()) me.attack(lock);
                        return true;
                    }
                me.submit_gems(altar);pending_submit_=me.is_submitting_gems();
                return true;
            }
        }
        // The walking goals are static. Rebuilding one bitboard per altar on
        // a conversion turn needlessly stacks geometry with shield observation.
        return move_to(altar_goals_);
    }

    bool capture_timeout(int selected,unsigned round) {
        auto&entry=groups_[selected];
        if(entry.unavailable_until<=round&&!capture_watch_.expired(selected,round,cambc::constants::capture_time))return false;
        if(entry.unavailable_until<=round)entry.unavailable_until=round+cambc::constants::capture_time+6;
        target_=-1;route_target_=-1;policy_.current=-1;contest_since_=-1;
        // Keep the clock until physical departure. A blocked exit must not
        // restart a fresh full hold on the same pad in the next owner turn.
        move_to(~entry.spawns&~terrain_);
        return true;
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
        const auto origin = me.position();
        const float health = me.health(), maximum = me.max_health();
        const float dps = h12_shield_window::camp_damage(strength,0,identity_,health,maximum) + me.focus() * .12f;
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
        if constexpr(Policy::enabled) {
            std::vector<heuristics::Detail> detail;
            detail.reserve(groups_.size());
            for(std::size_t i=0;i<groups_.size();++i)if(groups_[i].reachable&&!groups_[i].monolith&&groups_[i].unavailable_until<=round
                &&!(has_non_monolith_gems_&&monster_shards_>=5&&jungle_shards_>=5&&groups_[i].group.gem_value()==0))
                detail.push_back({int(i),distance(origin,groups_[i].anchor),true,groups_[i].incoming<=0});
            scan_order=heuristics::detail_order(std::move(detail),target_);
        }
        const auto count=Policy::enabled?scan_order.size():groups_.size();
        trace("farm-order",int(count));
        for (std::size_t slot = 0; slot < count; ++slot) {
            const std::size_t i=Policy::enabled?std::size_t(scan_order[slot]):slot;
            // Reserve fuel for movement/combat and yielding. Exhaustion resumes
            // midway through an old decision next round, so stop voluntarily.
            // The local Pareto screen and geometry executor need more reserve
            // than the predecessor's scalar selector. Admission precedes those
            // costs and the obstacle refresh inside move_to().
            if (cambc::fuel_remaining() < (jobs.empty()?55000:65000)) break;
            auto& entry = groups_[i];
            if constexpr(Policy::enabled)if(!entry.reachable)continue;
            if (entry.monolith || entry.unavailable_until > round) continue;
            // Gold establishes the first two emblems. Once equipped, pursue
            // score rather than farming safe gold clusters indefinitely. Maps
            // whose only gems are in monolith groups retain the gold tiebreak.
            if (has_non_monolith_gems_ && monster_shards_ >= 5 && jungle_shards_ >= 5
                && entry.group.gem_value() == 0) continue;
            const bool seen = !(entry.spawns & visible).is_empty();
            if constexpr(Policy::enabled)if(seen)entry.observed_round=int(round);
            bool reward = false;
            if (seen) {
                const auto state = entry.group.state();
                if (const auto* respawn = std::get_if<cambc::map::camp_group_respawning>(&state)) {
                    entry.unavailable_until = round + std::max(1, respawn->timer);
                    entry.frontier_memory.reset();
                    continue;
                }
                reward = std::holds_alternative<cambc::map::camp_group_reward_available>(state);
                // Hold a cleared camp through all five capture phases. Do not
                // start the altar journey merely because we carry older gems.
                if (reward && entry.spawns.is_set(origin)) {
                    if constexpr(Policy::enabled) {
                        if(capture_timeout(int(i),round))return;
                        const bool contested=std::any_of(enemy_positions_.begin(),enemy_positions_.end(),[&](vec2 p){return entry.spawns.is_set(p);});
                        if(contested){entry.unavailable_until=round+12;continue;}
                    }
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
            const float incoming = remaining_incoming * camp_reduction();
            const bool affordable = reward || fundamentals::affordable_camp(health, maximum,
                remaining_health, incoming, dps * (monster_shards_ >= 5 ? 1+item_type::monster_emblem_of_strength.passive_magnitude() : 1.0f));
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
            const int choice=heuristics::choose(policy_,snapshot,jobs);
            trace("farm-jobs",int(jobs.size()));
            ++selections_;deviations_+=choice!=baseline;
            selected=-1;partial_target=-1;selected_reward=false;
            for(const auto&j:jobs)if(j.id==choice){
                if(j.affordable){selected=choice;selected_reward=j.ready;}
                else if(health>=maximum*.75f||j.committed)partial_target=choice;
            }
            if(round%128==identity_%128&&cambc::fuel_remaining()>22000)
                std::printf("h12 select actor=%u round=%u target=%d calls=%u changed=%u phase=%d pulses=%d\n",identity_,round,choice,selections_,deviations_,int(policy_.window.phase),policy_.window.pulses);
        }
        partial_clear_ = selected < 0 && partial_target >= 0;
        trace("farm-choice",selected<0?partial_target:selected);
        if (partial_clear_) selected = partial_target;
        target_ = selected;
        if (selected < 0) {
            if (!cambc::map::spawn_region(team::ally).is_set(me.position())) recovering_ = true;
            return;
        }
        work_source(selected,selected_reward,camps,visible,round,origin);
    }

    void work_source(int selected,bool selected_reward,const std::vector<camp>&camps,
        const bitboard&visible,unsigned round,vec2 origin) {
        auto&me=cambc::self();
        auto& entry = groups_[selected];
        const int route_distance=distance(origin,entry.anchor);
        if(route_target_!=selected){route_target_=selected;route_best_=route_distance;route_progress_=round;}
        else if(route_distance<route_best_){route_best_=route_distance;route_progress_=round;}
        if (selected_reward) {
            if (!move_to(entry.spawns)&&(!Policy::enabled||cambc::fuel_remaining()>45000)) entry.unavailable_until = round + 3;
            return;
        }
        std::optional<camp> target;
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
                if(h12_shield_window::clear_route_blocker(round-route_progress_,own_work,true,
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
            std::vector<h12_shield_window::Member> members;
            members.reserve(entry.group.camps().size());
            for(const auto&monster:entry.group) {
                const auto p=monster.spawn_position();
                members.push_back({p.x,p.y,visible.is_set(p)});
            }
            const auto frontier=h12_shield_window::goal_frontier(entry.frontier_memory,members,
                !(entry.spawns&visible).is_empty()&&!selected_reward,round,entry.group.max_respawn_timer());
#if HEURISTICS_DIAGNOSTIC
            diagnostic_goals_=frontier.goals.size();
            if(!frontier.goals.empty())diagnostic_target_={frontier.goals[0].x,frontier.goals[0].y};
#endif
            for(const auto&m:frontier.goals)goals.set({m.x,m.y});
            if(frontier.discover)goals=h12_shield_window::approach_perimeter(goals);
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
        if constexpr(Policy::trace)trace_round_=cambc::round_number();
        auto& me = cambc::self();
        if (!initialized_) {initialize();return;}
        if constexpr(Policy::enabled) {
            if(!topology_ready_) {
                // A separate bounded setup turn avoids stacking a native flood
                // on map metadata and gameplay work. No mobility is assumed.
                if(cambc::fuel_remaining()<65000)return;
                cambc::distance_field field;bitboard reached;
                field.compute(terrain_,cambc::map::spawn_region(team::ally),reached);
                for(auto&g:groups_)g.reachable=!(g.spawns&reached).is_empty();
                altar_goals_=h12_shield_window::approach_perimeter(cambc::map::altars());
                topology_ready_=true;
                return;
            }
        }
        // Dead champions retain shopping turns, but have no usable position.
        update_finance();
        if (!me.is_alive()) { shop(); recovering_ = false; target_ = -1; capture_watch_.reset(); return; }
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
        bool leave_contest=false;
        if (cambc::fuel_remaining() < 30000) return;
        float incoming = 0;
        for (const auto& monster : camps)
            if (monster.is_alive() && monster.is_awake()
                && distance(me.position(), monster.position()) <= monster.attack_range()) incoming += monster.strength();
        for (const auto& enemy : enemies)
            if (distance(me.position(), enemy.position()) <= enemy.attack_range() + 1) incoming += enemy.strength();
        // Partial clears reserve roughly four incoming camp phases for escape;
        // ordinary safe fights retain the less conservative baseline threshold.
        const bool retreat=!spawn&&[&] {
            if constexpr(Policy::enabled)return policy_.retreat(me.health(),me.max_health(),incoming*(partial_clear_?2.f:1.f),me.gem_count());
            else return fundamentals::should_retreat(me.health(), me.max_health(),incoming * (partial_clear_ ? 2.0f : 1.0f));
        }();
        if (!spawn && retreat) recovering_ = true;
        if (recovering_) { recover(camps, enemies); return; }
        if(witnessed_round_>=0&&int(cambc::round_number())>witnessed_round_&&shield_logs_<10&&cambc::fuel_remaining()>50000) {
            const auto buffs=me.buffs();float value=0;int duration=0;if(buffs.has_shield()){const auto b=buffs.get<cambc::buff_shield>();value=b.value;duration=b.duration;}
            if(value<witnessed_shield_&&me.health()>=witnessed_hp_-.01f&&value>0){++shield_logs_;std::printf("h12shield actor=%u round=%u source=%d event=absorb value=%.1f>%.1f duration=%d hp=%.1f>%.1f\n",identity_,cambc::round_number(),policy_.window.source,witnessed_shield_,value,duration,witnessed_hp_,me.health());}
            witnessed_shield_=value;witnessed_hp_=me.health();witnessed_round_=cambc::round_number();
        }
        if(withdraw_window(camps,enemies))return;
        if(capture_watch_.source()>=0) {
            const int held=capture_watch_.source();const auto&entry=groups_[held];
            if(!entry.spawns.is_set(me.position()))capture_watch_.reset();
            else if(!std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state()))capture_watch_.reset();
            else if(capture_timeout(held,cambc::round_number()))return;
        }
        // Safe incumbent capture is protected, except urgent liquidation.
        if (target_ >= 0 && !policy_.forced_bank) {
            auto& entry = groups_[target_];
            if (entry.spawns.is_set(me.position()) && std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state())) {
                if constexpr(!Policy::enabled)return;
                else {
                    if(capture_timeout(target_,cambc::round_number()))return;
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
        if (bank()) return;
        // Defend against an adjacent threat, without chasing opponents.
        for (const auto& enemy : enemies)
            if (!leave_contest&&distance(me.position(), enemy.position()) <= 1 && fight(enemy, false, true)) return;
        if (!spawn && me.champion_name() != cambc::champion_type::tavi && me.gold_count() >= 300) {
            recovering_ = true;
            recover(camps, enemies);
            return;
        }
        if constexpr(Policy::enabled){enemy_positions_.clear();for(const auto&e:enemies)enemy_positions_.push_back(e.position());}
        const unsigned round=cambc::round_number();
        if(target_>=0) {
            auto&entry=groups_[target_];
            const bool admitted=entry.reachable&&entry.unavailable_until<=round&&!policy_.window.renewing();
            if(h12_shield_window::execution_lease(round,route_progress_,cambc::constants::capture_time,admitted)) {
                const auto visible=cambc::vision::visible_tiles();
                bool reward=false,valid=true;
                if(!(entry.spawns&visible).is_empty()) {
                    const auto status=entry.group.state();
                    if(const auto*respawn=std::get_if<cambc::map::camp_group_respawning>(&status)) {
                        entry.unavailable_until=round+std::max(1,respawn->timer);entry.frontier_memory.reset();valid=false;
                    }
                    reward=std::holds_alternative<cambc::map::camp_group_reward_available>(status);
                }
                if(valid){work_source(target_,reward,camps,visible,round,me.position());return;}
            }
        }
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
            std::cout << "shield-window error: " << error.what() << '\n';
        } catch (...) {
            std::cout << "shield-window error: unknown exception\n";
        }
        cambc::end_turn();
    }
}

} // namespace h12_shield_runtime
