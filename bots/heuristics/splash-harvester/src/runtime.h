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

namespace h12_splash_runtime {
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
    float density=1;
    unsigned unavailable_until = 0;
    int observed_round=-1;
    bool reachable=true;
    h12_splash_harvester::FrontierMemory frontier_memory{};
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
    unsigned splash_actions_=0,splash_hits_=0,splash_kills_=0,flow_logs_=0;
    std::vector<Group> groups_;
    std::vector<cambc::map::camp_group> pending_groups_;
    std::size_t metadata_next_=0;
    bool metadata_started_=false;
    std::optional<Group> metadata_group_;
    std::size_t metadata_member_=0;
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
#if HEURISTICS_DIAGNOSTIC
    int diagnostic_mode_=0;
    int diagnostic_goals_=0;
    int diagnostic_failure_=0,diagnostic_move_fuel_=0;
    vec2 diagnostic_target_{};
#endif
    void trace(const char*stage,int value=-1) const {
        if constexpr(requires{Policy::trace;})if constexpr(Policy::trace) {
            if(cambc::round_number()<=30&&cambc::fuel_remaining()>10000)
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
            // A source's prior samples six public members, never all pairs.
            const unsigned sample=std::min(std::size_t(6),entry.positions.size());
            for(unsigned a=0;a<sample;++a) {
                unsigned hits=0;for(unsigned b=0;b<sample;++b)hits+=distance(entry.positions[a],entry.positions[b])<=1;
                entry.density=std::max(entry.density,float(hits));
            }
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

    // Local hit-set executor; this fork deliberately has no reset chain.
    bool splash_fight(const camp&target,bool walked) {
        using namespace h12_splash_harvester;
        auto&me=cambc::self();
        if(cambc::fuel_remaining()<40000||!target.is_visible())return false;
        if constexpr(Policy::trace)std::printf("splashfuel actor=%u round=%u stage=entry fuel=%d\n",identity_,cambc::round_number(),int(cambc::fuel_remaining()));
        auto abilities=me.abilities();
        std::optional<cambc::ability> area,direct;
        if(me.can_cast())for(const auto&a:abilities) {
            if(!a.is_ready())continue;
            if(a.is_aoe_damage()&&a.can_target_ground()&&(!walked||!a.get<cambc::ability_aoe_damage>().exclusive_with_movement))area=a;
            if((a.is_damage()||a.is_cc())&&a.can_target_camps()) {
                const bool exclusive=std::visit([](const auto&v){return v.exclusive_with_movement;},a.info().value());
                if(!walked||!exclusive)direct=a;
            }
        }
        if(!me.can_attack()&&!area&&!direct)return false;
        // An available allowance is insufficient admission: a distant primary
        // must not pay for the complete geometry menu before its walking turn.
        const auto initial_visible=cambc::vision::visible_tiles();
        std::optional<bitboard>initial_attack,initial_area,initial_direct;
        bool executable=false;
        if(me.can_attack()&&distance(me.position(),target.spawn_position())<=me.attack_range()) {
            initial_attack=me.attack_tiles();executable|=initial_attack->is_set(target.spawn_position());
        }
        if(direct){initial_direct=direct->valid_tiles();executable|=initial_direct->is_set(target.spawn_position());}
        if(area) {
            initial_area=area->valid_tiles();const auto p=target.spawn_position();
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                const vec2 center{p.x+dx,p.y+dy};
                if(center.x>=0&&center.y>=0&&center.x<int(cambc::map::width())&&center.y<int(cambc::map::height())
                    &&initial_visible.is_set(center)&&!terrain_.is_set(center)&&initial_area->is_set(center))executable=true;
            }
        }
        if(!executable)return false;
        struct World {
            Bot*owner;std::vector<camp>targets;std::vector<vec2>positions;
            std::vector<Point>forbidden;
            std::optional<cambc::ability>area,direct;
            std::optional<bitboard>attack_mask,area_mask,direct_mask;
            bitboard visible;
            Point anchor;bool splash=false;
            float area_damage=0,direct_damage=0,radius=0;
            bool enough_fuel()const{return cambc::fuel_remaining()>40000;}
            bool available(bool basic,bool cast)const {
                auto&me=cambc::self();
                return (basic&&me.can_attack())||(cast&&me.can_cast()
                    &&((area&&area->is_ready())||(direct&&direct->is_ready())));
            }
            Menu observe() {
                Menu m;auto&me=cambc::self();
                if constexpr(Policy::trace)std::printf("splashfuel actor=%u round=%u stage=observe fuel=%d\n",owner->identity_,cambc::round_number(),int(cambc::fuel_remaining()));
                m.basic=me.is_alive()&&me.can_attack();
                const bool ac=me.can_cast()&&area&&area->is_ready();
                const bool dc=me.can_cast()&&direct&&direct->is_ready();
                m.cast=ac||dc;m.splash=splash;m.basic_damage=me.strength();
                m.area_damage=ac?area_damage:0;m.direct_damage=dc?direct_damage:0;m.radius=radius;
                if(m.basic&&!attack_mask)attack_mask=me.attack_tiles();
                if(ac&&!area_mask)area_mask=area->valid_tiles();
                if(dc&&!direct_mask)direct_mask=direct->valid_tiles();
                m.count=targets.size();
                for(unsigned i=0;i<m.count;++i) {
                    const auto p=positions[i];auto&t=m.targets[i];t.p={p.x,p.y};
                    if(!visible.is_set(p)||targets[i].state().is_dead()||!targets[i].is_visible())continue;
                    t.hp=targets[i].health();t.incoming=targets[i].strength();
                    t.basic=m.basic&&attack_mask->is_set(p);t.direct=dc&&direct_mask->is_set(p);
                    for(const auto q:forbidden) {
                        const int d=h12_splash_harvester::distance(t.p,q);
                        if(d==0)m.safe_direct[i]=false;
                        if(d<=(splash?1:0))m.safe_basic[i]=false;
                    }
                }
                unsigned n=0;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx,++n) {
                    auto&c=m.centers[n];c.p={anchor.x+dx,anchor.y+dy};const vec2 p{c.p.x,c.p.y};
                    c.legal=ac&&p.x>=0&&p.y>=0&&p.x<int(cambc::map::width())&&p.y<int(cambc::map::height())
                        &&visible.is_set(p)&&!owner->terrain_.is_set(p)&&area_mask->is_set(p);
                    for(const auto q:forbidden)if(h12_splash_harvester::distance(c.p,q)<=radius)m.safe_area[n]=false;
                }
                if constexpr(Policy::trace)std::printf("splashfuel actor=%u round=%u stage=menu fuel=%d\n",owner->identity_,cambc::round_number(),int(cambc::fuel_remaining()));
                return m;
            }
            bool legal(const Plan&p) {
                auto&me=cambc::self();if(!me.is_alive())return false;
                if(p.kind==Kind::area) {
                    const vec2 center{anchor.x+p.index%3-1,anchor.y+p.index/3-1};
                    return me.can_cast()&&area&&area->is_ready()&&area->can_target_ground()
                        &&visible.is_set(center)&&area_mask&&area_mask->is_set(center);
                }
                if(p.index<0||p.index>=int(targets.size())||!targets[p.index].is_visible())return false;
                if(p.kind==Kind::basic)return me.can_attack()&&attack_mask&&attack_mask->is_set(positions[p.index]);
                return me.can_cast()&&direct&&direct->is_ready()&&direct->can_target_camps()
                    &&direct_mask&&direct_mask->is_set(positions[p.index]);
            }
            bool act(const Plan&p) {
                bool used=false;
                if(p.kind==Kind::basic){cambc::self().attack(targets[p.index]);used=true;}
                else if(p.kind==Kind::area)used=area->use(vec2{anchor.x+p.index%3-1,anchor.y+p.index/3-1});
                else used=direct->use(targets[p.index]);
                if(used){attack_mask.reset();area_mask.reset();direct_mask.reset();}
                return used;
            }
            std::pair<unsigned,unsigned>effect(const Menu&before) {
                if constexpr(Policy::trace)std::printf("splashfuel actor=%u round=%u stage=effect fuel=%d\n",owner->identity_,cambc::round_number(),int(cambc::fuel_remaining()));
                unsigned hits=0,kills=0;std::array<float,6>after{};
                std::array<int,2>witness{-1,-1};
                visible=cambc::vision::visible_tiles();
                for(unsigned i=0;i<before.count;++i) {
                    if(before.targets[i].hp<=0||!visible.is_set(positions[i]))continue;
                    const bool dead=targets[i].state().is_dead();
                    after[i]=dead?0:targets[i].health();
                    if(after[i]<before.targets[i].hp) {
                        if(hits<2)witness[hits]=int(i);
                        ++hits;
                    }
                    kills+=dead;
                }
                if(hits>=2) {
                    ++owner->splash_actions_;owner->splash_hits_+=hits;owner->splash_kills_+=kills;
                    if(owner->flow_logs_<3&&cambc::fuel_remaining()>25000) {
                        ++owner->flow_logs_;
                        const int a=witness[0],b=witness[1];
                        std::printf("h12splash actor=%u round=%u source=%d hits=%u kills=%u a=%d,%d hp=%.1f>%.1f b=%d,%d hp=%.1f>%.1f\n",
                            owner->identity_,cambc::round_number(),owner->target_,hits,kills,
                            positions[a].x,positions[a].y,before.targets[a].hp,after[a],
                            positions[b].x,positions[b].y,before.targets[b].hp,after[b]);
                    }
                }
                return {hits,kills};
            }
        }world{this,{}, {},{},area,direct,initial_attack,initial_area,initial_direct,initial_visible};
        const auto anchor=target.spawn_position();world.anchor={anchor.x,anchor.y};world.splash=identity_==10;
        if(area){world.area_damage=area->damage_amount();world.radius=area->get<cambc::ability_aoe_damage>().radius;}
        if(direct)world.direct_damage=direct->damage_amount();
        world.targets.push_back(target);world.positions.push_back(anchor);
        // All nearby groups participate in risk admission, even if their camps
        // do not fit in the six-target damage-scoring menu.
        const int focus=group_at_tile_[anchor.y*cambc::map::width()+anchor.x];
        const float dps=std::max(1.f,me.strength()+me.focus()*.15f);
        const float hp=me.health(),maximum=me.max_health();
        for(unsigned g=0;g<groups_.size();++g)if(int(g)!=focus) {
            const auto&entry=groups_[g];
            if(fundamentals::affordable_camp(hp,maximum,entry.health,entry.incoming,dps))continue;
            // Centers lie within one tile of the anchor and scoring victims
            // within three. Distant groups cannot intersect either hit region.
            for(const auto p:entry.positions)if(distance(p,anchor)<=4)
                world.forbidden.push_back({p.x,p.y});
        }
        for(const auto&t:cambc::vision::camps()) {
            if(world.targets.size()>=6)break;
            const auto p=t.spawn_position();
            if(t!=target&&distance(p,anchor)<=3){world.targets.push_back(t);world.positions.push_back(p);}
        }
        if constexpr(Policy::trace)std::printf("splashfuel actor=%u round=%u stage=execute fuel=%d\n",identity_,cambc::round_number(),int(cambc::fuel_remaining()));
        const auto flow=h12_splash_harvester::execute(world);
        if(flow.observed_hits)route_progress_=cambc::round_number();
        return flow.actions>0;
    }

    template<class Target>bool fight(const Target&target,bool walked,bool dangerous) {
        if constexpr(std::is_same_v<Target,camp>)return splash_fight(target,walked);
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

    bool bank() {
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
                // Public spawn geometry is only a coarse speed prior. Actual
                // clipped hit sets, legality and death drive combat itself.
                if(identity_==10)j.work/=1+.6f*(entry.density-1);
                if(identity_==5||identity_==0)j.work/=1+.2f*(entry.density-1);
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
                std::printf("h12 select actor=%u round=%u target=%d calls=%u changed=%u splash=%u hits=%u kills=%u\n",identity_,round,choice,selections_,deviations_,splash_actions_,splash_hits_,splash_kills_);
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
                if(h12_splash_harvester::clear_route_blocker(round-route_progress_,own_work,true,
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
            std::vector<h12_splash_harvester::Member> members;
            members.reserve(entry.group.camps().size());
            for(const auto&monster:entry.group) {
                const auto p=monster.spawn_position();
                members.push_back({p.x,p.y,visible.is_set(p)});
            }
            const auto frontier=h12_splash_harvester::goal_frontier(entry.frontier_memory,members,
                !(entry.spawns&visible).is_empty()&&!selected_reward,round,entry.group.max_respawn_timer());
#if HEURISTICS_DIAGNOSTIC
            diagnostic_goals_=frontier.goals.size();
            if(!frontier.goals.empty())diagnostic_target_={frontier.goals[0].x,frontier.goals[0].y};
#endif
            for(const auto&m:frontier.goals)goals.set({m.x,m.y});
            if(frontier.discover)goals=h12_splash_harvester::approach_perimeter(goals);
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
            const bool admitted=entry.reachable&&entry.unavailable_until<=round;
            if(h12_splash_harvester::execution_lease(round,route_progress_,cambc::constants::capture_time,admitted)) {
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
            std::cout << "fundamentals error: " << error.what() << '\n';
        } catch (...) {
            std::cout << "fundamentals error: unknown exception\n";
        }
        cambc::end_turn();
    }
}

} // namespace h12_splash_runtime
