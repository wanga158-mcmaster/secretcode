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
#include <type_traits>
#include "../../common/src/decision.h"
#include "frontier.h"
#include "service.h"
#include "../../common/src/draft.h"
#if CAPTURE_WATCHDOG
#include "capture.h"
#endif

namespace heuristics_runtime {
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
    const auto&order=heuristics_draft::choices;
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
    h12_altar_options::FrontierMemory frontier_memory{};
};

template<class Policy> class Bot {
    Policy policy_;
    unsigned selections_=0, deviations_=0;
    std::vector<vec2> enemy_positions_;
    vec2 home_{};
    bool topology_ready_=false;
    int contest_since_=-1;
#if CAPTURE_WATCHDOG
    heuristics::CaptureWatch capture_watch_;
#endif
    int route_target_=-1,route_progress_=0,route_best_=10000;
    unsigned route_actions_=0;
    std::vector<Group> groups_;
    std::vector<int> group_at_tile_;
    std::vector<vec2> altars_;
    std::vector<cambc::distance_field> altar_fields_;
    bitboard terrain_;
    bitboard reachable_;
    h12_altar_options::Service service_;
    unsigned service_events_=0;
    bitboard remembered_camps_;
    bool initialized_ = false;
    bool metadata_started_=false;
    std::vector<cambc::map::camp_group> pending_groups_;
    std::size_t metadata_next_=0,metadata_member_=0;
    std::optional<Group> metadata_group_;
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
        if(!metadata_started_) {
            terrain_=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()
                |cambc::map::spawn_region(team::enemy);
            altars_=cambc::map::altars().positions();
            group_at_tile_.assign(cambc::map::width()*cambc::map::height(),-1);
            pending_groups_=cambc::map::camp_groups();
            identity_=static_cast<unsigned>(cambc::self().champion_name());
            home_=cambc::map::spawn_region(team::ally).positions().front();
            metadata_started_=true;return;
        }
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
                const auto pos=monster.spawn_position();entry.spawns.set(pos);
                group_at_tile_[pos.y*cambc::map::width()+pos.x]=static_cast<int>(groups_.size());
                const auto stats=monster.base_stats();entry.health+=stats.base_health;
                entry.incoming+=stats.base_strength;entry.monolith|=monster.is_monolith();
            }
            if(metadata_member_<roster.size())break;
            has_non_monolith_gems_|=!entry.monolith&&entry.group.gem_value()>0;
            groups_.push_back(std::move(entry));metadata_group_.reset();
            ++metadata_next_;++completed;
        }
        initialized_=metadata_next_==pending_groups_.size();
    }

    void shop() {
        auto& me = cambc::self();
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
            policy_.next_price=int(item.cost());
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

    // A single visible target is attacked; refresh live validity after every
    // hit because kills and Varg splash can invalidate an earlier camp view.
    template<class Target>
    bool fight(const Target& target, bool walked, bool dangerous) {
        auto& me = cambc::self();
        if (cambc::fuel_remaining() < (Policy::enabled?22000:16000)) return false;
        auto valid = [&] { return target.is_visible() && target.health() > 0; };
        if (!valid()) return false;
        if constexpr(std::is_same_v<Target,camp>) {
            if(identity_==static_cast<unsigned>(cambc::champion_type::varg)) {
                const auto p=target.spawn_position();
                const int own=group_at_tile_[p.y*cambc::map::width()+p.x];
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    const int x=p.x+dx,y=p.y+dy;
                    if(x<0||y<0||x>=cambc::map::width()||y>=cambc::map::height())continue;
                    const int other=group_at_tile_[y*cambc::map::width()+x];
                    if(other>=0&&other!=own&&groups_[other].incoming>0) {
                        if(own>=0)groups_[own].unavailable_until=cambc::round_number()+8;
                        return false;
                    }
                }
            }
        }
        bool acted = false;
        auto abilities = me.abilities();
        if (!walked) {
            // Tavi/Marcella burst from range before choosing to walk.
            for (const auto& ability : abilities)
                if ((ability.is_damage() || ability.is_cc()) && ability.is_ready()
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
                if (ability.is_shield() && ability.is_ready()) { acted |= ability.use(); break; }
        if (me.can_attack()) { me.attack(target); acted = true; }
        if (!valid()) return acted;
        // Includes ranged champions that walked this turn: a reset explicitly
        // restores their attack allowance before the extra attack.
        if (!me.can_attack())
            for (const auto& ability : abilities)
                if (ability.is_attack_reset() && ability.is_ready()) {
                    if (ability.use() && me.can_attack() && valid()) me.attack(target);
                    acted = true;
                    break;
                }
        return acted;
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

    struct ServiceWorld {
        Bot&bot;
        int round()const{return int(cambc::round_number());}
        int bag()const{return int(cambc::self().gem_count());}
        int score()const{return int(cambc::global_submitted_gems(team::ally));}
        bool alive()const{return cambc::self().is_alive();}
        bool submitting()const{return cambc::self().is_submitting_gems();}
        bool channeling()const {
            const auto&me=cambc::self();
            return me.is_recalling()||me.is_submitting_gems()||me.is_teleporting();
        }
        bool ranged()const{return cambc::self().attack_range()>1;}
        int tribute_ticks()const {
            return std::max(cambc::constants::submit_gem_minimum_duration,
                cambc::constants::submit_gem_base_duration+int(std::ceil(bag()*cambc::constants::submit_gem_duration_multiplier)));
        }
        h12_altar_options::Altar observe(int id)const {
            using A=h12_altar_options::Altar;A a;a.id=id;
            if(id<0||std::size_t(id)>=bot.altar_fields_.size()||!alive())return a;
            const auto&me=cambc::self();const auto pos=bot.altars_[id];
            const auto d=bot.altar_fields_[id].get(me.position());
            a.reachable=d!=cambc::distance_field::unreachable;
            a.travel=a.reachable?int(d):10000;
            a.visible=cambc::vision::visible_tiles().is_set(pos);
            a.hits=a.visible?0:-1;
            if(a.visible)for(const auto&lock:cambc::vision::altar_locks(team::enemy))
                if(lock.position()==pos){a.hits=int(lock.hits_remaining());break;}
            a.in_attack=me.attack_tiles().is_set(pos);
            a.in_submit=distance(me.position(),pos)<=cambc::constants::submit_gems_range;
            return a;
        }
        std::array<h12_altar_options::Altar,2> options()const {
            std::array<h12_altar_options::Altar,2> result{};
            std::array<int,2> ids{-1,-1};std::array<int,2> costs{10000,10000};
            const auto pos=cambc::self().position();
            for(std::size_t i=0;i<bot.altar_fields_.size();++i) {
                const auto d=bot.altar_fields_[i].get(pos);
                if(d==cambc::distance_field::unreachable)continue;
                if(int(d)<costs[0]){costs[1]=costs[0];ids[1]=ids[0];costs[0]=d;ids[0]=int(i);}
                else if(int(d)<costs[1]){costs[1]=d;ids[1]=int(i);}
            }
            for(int k=0;k<2;++k)if(ids[k]>=0)result[k]=observe(ids[k]);
            return result;
        }
        bool unsafe(int id)const {
            if(id<0||std::size_t(id)>=bot.altars_.size())return true;
            const auto&me=cambc::self();
            if(me.health()<me.max_health()*.28f)return true;
            for(const auto p:bot.enemy_positions_)
                if(distance(p,bot.altars_[id])<=4&&me.health()<me.max_health()*.65f)return true;
            return false;
        }
        bool can_attack()const{return cambc::self().can_attack();}
        bool reset_ready()const {
            const auto&me=cambc::self();if(!me.can_cast())return false;
            for(const auto&a:me.abilities())if(a.is_attack_reset()&&a.is_ready())return true;
            return false;
        }
        bool walk(int id) {
            if(id<0||std::size_t(id)>=bot.altars_.size())return false;
            return bot.move_to(around(bot.altars_[id],cambc::constants::submit_gems_range));
        }
        bool hit(int id) {
            auto&me=cambc::self();
            if(cambc::fuel_remaining()<18000||!me.can_attack())return false;
            const auto pos=bot.altars_[id];const auto mask=me.attack_tiles();
            if(!mask.is_set(pos))return false;
            for(const auto&lock:cambc::vision::altar_locks(team::enemy))if(lock.position()==pos) {
                me.attack(lock);return true;
            }
            return false;
        }
        bool reset() {
            auto&me=cambc::self();if(cambc::fuel_remaining()<18000||!me.can_cast())return false;
            for(const auto&a:me.abilities())if(a.is_attack_reset()&&a.is_ready())return a.use()&&me.can_attack();
            return false;
        }
        bool submit(int id) {
            if(cambc::fuel_remaining()<12000)return false;
            const auto a=observe(id);if(!a.visible||a.hits!=0||!a.in_submit||bag()<=0)return false;
            cambc::self().submit_gems(bot.altars_[id]);
            return cambc::self().is_submitting_gems();
        }
        void event(const char*name,int id,int value) {
            if(bot.service_events_>=32||cambc::fuel_remaining()<14000)return;
            ++bot.service_events_;
            std::printf("h12service actor=%u round=%u event=%s altar=%d value=%d bag=%u\n",
                bot.identity_,cambc::round_number(),name,id,value,cambc::self().gem_count());
        }
    };

    bool bank() {
        ServiceWorld world{*this};
        int nearest=10000;
        if(cambc::self().is_alive())for(const auto&field:altar_fields_) {
            const auto d=field.get(cambc::self().position());
            if(d!=cambc::distance_field::unreachable)nearest=std::min(nearest,int(d));
        }
        return service_.tick(world,policy_.bank(state(nearest)));
    }

#if CAPTURE_WATCHDOG
    bool capture_exit_work(int selected,const std::vector<camp>&camps) {
        auto&me=cambc::self();
        if(cambc::fuel_remaining()<26000)return false;
        // A blocked step must still allow an immediately cashable bag.
        if(me.gem_count()>0)
            for(const auto altar:altars_)if(distance(me.position(),altar)<=1&&bank())return true;
        if(me.can_attack()) {
            const auto mask=me.attack_tiles();
            for(const auto&blocker:camps) {
                if(cambc::fuel_remaining()<22000)break;
                const auto p=blocker.spawn_position();
                const int group=group_at_tile_[p.y*cambc::map::width()+p.x];
                if(group<0||group==selected||groups_[group].incoming>0||!mask.is_set(p))continue;
                bool splash_safe=true;
                if(identity_==static_cast<unsigned>(cambc::champion_type::varg))
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                        const int x=p.x+dx,y=p.y+dy;
                        if(x<0||y<0||x>=cambc::map::width()||y>=cambc::map::height())continue;
                        const int neighbour=group_at_tile_[y*cambc::map::width()+x];
                        if(neighbour>=0&&groups_[neighbour].incoming>0)splash_safe=false;
                    }
                if(h12_altar_options::clear_route_blocker(4,false,true,groups_[group].incoming,true,splash_safe)
                    &&blocker.is_alive()){me.attack(blocker);return true;}
            }
        }
        // Recall is an alternate physical exit, not a new capture hold. Use
        // the existing recovery rule's visible-threat clearance before it.
        if(cambc::fuel_remaining()<20000)return false;
        const auto origin=me.position();
        for(const auto&monster:camps) {
            if(cambc::fuel_remaining()<17000)return false;
            if(monster.is_alive()&&monster.is_awake()&&monster.strength()>0
                &&distance(origin,monster.position())<=monster.attack_range()+1)return false;
        }
        if(cambc::fuel_remaining()<17000)return false;
        for(const auto&enemy:cambc::vision::champions(team::enemy)) {
            if(cambc::fuel_remaining()<14000)return false;
            if(distance(origin,enemy.position())<=8)return false;
        }
        if(cambc::fuel_remaining()<12000)return false;
        me.recall();return true;
    }

    bool capture_timeout(int selected,unsigned round,const std::vector<camp>&camps) {
        auto&entry=groups_[selected];
        if(!h12_altar_options::leave_capture(capture_watch_,selected,round,
            cambc::constants::capture_time,entry.unavailable_until>round))return false;
        if(entry.unavailable_until<=round)entry.unavailable_until=round+12;
        target_=-1;route_target_=-1;contest_since_=-1;
        h12_altar_options::leave_or_work([&]{return move_to(~entry.spawns&~terrain_);},
            [&]{return capture_exit_work(selected,camps);});
        return true;
    }
#endif

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
            if (cambc::fuel_remaining() < 55000) break;
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
#if CAPTURE_WATCHDOG
                    if(capture_timeout(static_cast<int>(i),round,camps))return;
#endif
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
                std::printf("h12 select actor=%u round=%u target=%d calls=%u changed=%u\n",identity_,round,choice,selections_,deviations_);
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
                if(h12_altar_options::clear_route_blocker(round-route_progress_,own_work,true,
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
            std::vector<h12_altar_options::Member> members;
            members.reserve(entry.group.camps().size());
            for(const auto&monster:entry.group) {
                const auto p=monster.spawn_position();
                members.push_back({p.x,p.y,visible.is_set(p)});
            }
            const auto frontier=h12_altar_options::goal_frontier(entry.frontier_memory,members,
                !(entry.spawns&visible).is_empty()&&!selected_reward,round,entry.group.max_respawn_timer());
#if HEURISTICS_DIAGNOSTIC
            diagnostic_goals_=frontier.goals.size();
            if(!frontier.goals.empty())diagnostic_target_={frontier.goals[0].x,frontier.goals[0].y};
#endif
            for(const auto&m:frontier.goals)goals.set({m.x,m.y});
            if(frontier.discover)goals=h12_altar_options::approach_perimeter(goals);
        }
        if constexpr(Policy::enabled)goals.set(origin,false);
        if (move_to(goals)) {
            if constexpr(Policy::enabled)if(cambc::fuel_remaining()<30000)return;
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
                reachable_=reached;
                topology_ready_=true;
                return;
            }
            // Cache exact public-terrain approach distances over separate turns.
            // Dynamic blockers remain observed action failures, not hidden facts.
            if(altar_fields_.size()<altars_.size()) {
                if(cambc::fuel_remaining()<65000)return;
                cambc::distance_field field;bitboard visited;
                const auto goals=around(altars_[altar_fields_.size()],cambc::constants::submit_gems_range)&~terrain_;
                field.compute(terrain_,goals,visited);
                altar_fields_.push_back(std::move(field));return;
            }
        }
        ServiceWorld service_world{*this};service_.sync(service_world);
        // Dead champions retain shopping turns, but have no usable position.
        if (!me.is_alive()) { shop(); recovering_ = false; target_ = -1;
#if CAPTURE_WATCHDOG
            capture_watch_.reset();
#endif
            return; }
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
        enemy_positions_.clear();for(const auto&e:enemies)enemy_positions_.push_back(e.position());
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
#if CAPTURE_WATCHDOG
        if(capture_watch_.source()>=0) {
            const int held=capture_watch_.source();const auto&entry=groups_[held];
            const bool on_source=entry.spawns.is_set(me.position());
            const bool reward=on_source&&std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state());
            h12_altar_options::observe_capture(capture_watch_,on_source,reward);
            if(capture_watch_.source()>=0&&capture_timeout(held,cambc::round_number(),camps))return;
        }
#endif
        // Cleared camp ownership takes precedence over banking old gems.
        if (target_ >= 0) {
            auto& entry = groups_[target_];
            if (entry.spawns.is_set(me.position()) && std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state())) {
                if constexpr(!Policy::enabled)return;
                else {
#if CAPTURE_WATCHDOG
                    if(capture_timeout(target_,cambc::round_number(),camps))return;
#endif
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
        // Amortise the shop trip. The selector's next-purchase financing lot
        // does not promise an immediate recall for every individual shard.
        if (!spawn && me.champion_name() != cambc::champion_type::tavi && me.gold_count() >= unsigned(policy_.next_price*6)) {
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

} // namespace heuristics_runtime
