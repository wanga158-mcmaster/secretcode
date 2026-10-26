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

namespace h12_inventory_runtime {
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
    h12_working_inventory::FrontierMemory frontier_memory{};
};

template<class Policy> class Bot {
    Policy policy_;
    unsigned trace_round_=0;
    unsigned selections_=0, deviations_=0;
    std::vector<vec2> enemy_positions_;
    vec2 home_{};
    bool topology_ready_=false;
    int contest_since_=-1;
    int route_target_=-1,route_progress_=0,route_best_=10000;
    unsigned route_actions_=0;
    unsigned inventory_casts_=0,inventory_kills_=0,flow_logs_=0,stock_logs_=0,work_logs_=0;
    h12_working_inventory::Cargo cargo_;
    std::optional<vec2> stock_target_;
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

    // Small primary-target executor: fixed two spell slots, six targets,
    // no hit-set maximization, reset chain or per-candidate route field.
    struct CombatWorld {
        Bot*owner;
        std::vector<camp> targets;
        std::vector<vec2> positions;
        std::array<std::optional<cambc::ability>,2> spells{};
        std::array<std::optional<bitboard>,2> masks{};
        std::array<bool,2> area{};
        std::array<float,2> damage{},empty{},radius{};
        std::optional<bitboard> attack_mask;
        bitboard visible;
        float owner_hp=0,owner_maximum=0,owner_dps=1,basic_damage=0;
        std::vector<signed char> affordable;
        CombatWorld(Bot*o,const std::vector<camp>&candidates,bool walked):owner(o),visible(cambc::vision::visible_tiles()) {
            owner->trace("world-start",int(candidates.size()));
            auto&me=cambc::self();
            const float focus=me.focus();
            const auto origin=me.position();const auto attack_range=me.attack_range();const bool can_attack=me.can_attack();
            const float empty_focus=std::max(0.f,focus-(owner->identity_==6?me.gem_count()*cambc::constants::passive_abilities::prism_focus_per_gem:0));
            const float monster_bonus=owner->monster_shards_>=5?item_type::monster_emblem_of_focus.passive_magnitude():0;
            if(me.can_cast())for(const auto&a:me.abilities()) {
                const unsigned slot=a.slot();if(slot>=2||!a.is_ready())continue;
                const bool exclusive=std::visit([](const auto&v){return v.exclusive_with_movement;},a.info().value());
                if(walked&&exclusive)continue;
                area[slot]=a.is_aoe_damage()&&a.can_target_ground();
                if(!area[slot]&&!((a.is_damage()||a.is_cc())&&a.can_target_camps()))continue;
                spells[slot]=a;masks[slot]=a.valid_tiles();
                damage[slot]=h12_working_inventory::camp_damage(a.damage_amount(),monster_bonus);
                empty[slot]=focus>0?damage[slot]*empty_focus/focus:0;
                if(area[slot])radius[slot]=a.get<cambc::ability_aoe_damage>().radius;
            }
            for(const auto&t:candidates) {
                if(targets.size()>=6||cambc::fuel_remaining()<45000)break;
                const auto p=t.spawn_position();
                if(!visible.is_set(p)||!t.is_visible()||!t.is_alive())continue;
                bool contact=can_attack&&distance(origin,p)<=attack_range;
                for(unsigned k=0;k<2;++k)contact|=spells[k]&&masks[k]->is_set(p);
                if(contact){targets.push_back(t);positions.push_back(p);}
            }
            if(!targets.empty()&&me.can_attack())attack_mask=me.attack_tiles();
            owner->trace("world-end",int(targets.size()));
        }
        bool enough_fuel()const{return cambc::fuel_remaining()>40000;}
        bool available(bool basic,bool cast)const {
            auto&me=cambc::self();return (basic&&me.can_attack())||(cast&&me.can_cast()&&((spells[0]&&spells[0]->is_ready())||(spells[1]&&spells[1]->is_ready())));
        }
        bool safe_area(vec2 p,unsigned slot) {
            const int own=owner->group_at_tile_[p.y*cambc::map::width()+p.x];
            const int r=int(std::ceil(radius[slot]));
            for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx) {
                const int x=p.x+dx,y=p.y+dy;
                if(x<0||y<0||x>=int(cambc::map::width())||y>=int(cambc::map::height()))continue;
                const int g=owner->group_at_tile_[y*cambc::map::width()+x];
                if(g>=0&&g!=own) {
                    auto&cached=affordable[g];
                    if(cached<0){const auto&e=owner->groups_[g];cached=fundamentals::affordable_camp(owner_hp,owner_maximum,e.health,e.incoming,owner_dps);}
                    if(!cached)return false;
                }
            }
            return true;
        }
        h12_working_inventory::Menu observe() {
            using namespace h12_working_inventory;
            owner->trace("menu-start");
            auto&me=cambc::self();Menu m;m.basic=me.can_attack();m.cast=me.can_cast();m.count=targets.size();
            // One fresh owner snapshot per observation epoch; all tile checks
            // within that unchanged epoch share its whole-source admission.
            owner_hp=me.health();owner_maximum=me.max_health();
            const float strength=me.strength(),focus=me.focus();
            owner_dps=std::max(1.f,strength+focus*.15f);
            basic_damage=camp_damage(strength,owner->monster_shards_>=5?item_type::monster_emblem_of_focus.passive_magnitude():0);
            affordable.assign(owner->groups_.size(),-1);
            if(m.basic&&!attack_mask)attack_mask=me.attack_tiles();
            for(unsigned i=0;i<m.count;++i) {
                const auto p=positions[i];auto&t=m.targets[i];
                if(!visible.is_set(p)||targets[i].state().is_dead()||!targets[i].is_visible())continue;
                t.hp=targets[i].health();
                t.basic_damage=basic_damage;
                t.basic=m.basic&&attack_mask&&attack_mask->is_set(p);
                for(unsigned k=0;k<2;++k) {
                    t.loaded[k]=damage[k];t.empty[k]=empty[k];
                    t.spell[k]=m.cast&&spells[k]&&spells[k]->is_ready()&&masks[k]->is_set(p)
                        &&(!area[k]||(!owner->terrain_.is_set(p)&&safe_area(p,k)));
                }
            }
            owner->trace("menu-end");return m;
        }
        bool legal(const h12_working_inventory::Plan&p) {
            using namespace h12_working_inventory;
            auto&me=cambc::self();if(!me.is_alive()||p.index<0||p.index>=int(targets.size()))return false;
            const auto q=positions[p.index];
            if(!visible.is_set(q)||targets[p.index].state().is_dead()||!targets[p.index].is_visible())return false;
            if(p.kind==Kind::basic)return me.can_attack()&&attack_mask&&attack_mask->is_set(q);
            return p.slot>=0&&p.slot<2&&me.can_cast()&&spells[p.slot]&&spells[p.slot]->is_ready()
                &&masks[p.slot]->is_set(q)&&(!area[p.slot]||(!owner->terrain_.is_set(q)&&safe_area(q,p.slot)));
        }
        bool act(const h12_working_inventory::Plan&p) {
            using namespace h12_working_inventory;
            if(p.kind==Kind::basic){cambc::self().attack(targets[p.index]);attack_mask.reset();return true;}
            return area[p.slot]?spells[p.slot]->use(positions[p.index]):spells[p.slot]->use(targets[p.index]);
        }
        std::pair<unsigned,unsigned>effect(const h12_working_inventory::Menu&before,const h12_working_inventory::Plan&p) {
            const auto q=positions[p.index];visible=cambc::vision::visible_tiles();
            if(!visible.is_set(q))return {0,0};
            const bool dead=targets[p.index].state().is_dead();
            const float hp=dead?0:targets[p.index].health();
            const bool hit=hp<before.targets[p.index].hp;
            if(hit){owner->route_progress_=cambc::round_number();owner->policy_.inventory.note_progress(cambc::round_number());}
            if(p.stock&&hit) {
                ++owner->inventory_casts_;owner->inventory_kills_+=dead;
                if(owner->flow_logs_<6&&owner->policy_.inventory.phase==h12_working_inventory::Phase::work&&cambc::fuel_remaining()>25000) {
                    ++owner->flow_logs_;
                    std::printf("h12stockcast actor=%u round=%u source=%d bag=%u focus=%.1f slot=%d hp=%.1f>%.1f loaded=%.2f empty=%.2f threshold=%d dead=%d x=%d y=%d phase=work\n",
                        owner->identity_,cambc::round_number(),owner->target_,cambc::self().gem_count(),cambc::self().focus(),p.slot,
                        before.targets[p.index].hp,hp,before.targets[p.index].loaded[p.slot],before.targets[p.index].empty[p.slot],int(p.kill_threshold),int(dead),q.x,q.y);
                }
            }
            return {unsigned(hit),unsigned(dead)};
        }
    };
    bool inventory_fight(const camp&target,bool walked) {
        trace("combat-entry");
        if(cambc::fuel_remaining()<40000||!target.is_visible()||!target.is_alive())return false;
        std::vector<camp> candidates{target};
        CombatWorld world(this,candidates,walked);
        trace("combat-world");
        if(world.targets.empty())return false;
        return h12_working_inventory::execute(world).actions>0;
    }
    void observe_stock_receipt() {
        auto&me=cambc::self();const int bag=me.gem_count();const float focus=me.focus();
        const int previous_bag=cargo_.bag;const float previous_focus=cargo_.focus;
        const auto event=cargo_.observe(bag,focus,me.is_alive(),me.is_submitting_gems());
        if(event!=h12_working_inventory::Receipt::none&&stock_logs_<10&&cambc::fuel_remaining()>60000) {
            ++stock_logs_;
            std::printf("h12stock actor=%u round=%u event=%s bag=%d>%d focus=%.1f>%.1f phase=%d\n",identity_,cambc::round_number(),
                event==h12_working_inventory::Receipt::gain?"gain":event==h12_working_inventory::Receipt::bank?"bank":"loss",previous_bag,bag,previous_focus,focus,int(policy_.inventory.phase));
        }
        if(bag<previous_bag) {
            target_=-1;route_target_=-1;policy_.current=-1;policy_.inventory.source=-1;
            stock_target_.reset();
        }
    }
    void update_inventory(const std::vector<camp>&camps,float incoming) {
        using namespace h12_working_inventory;
        trace("stock-entry");
        auto&me=cambc::self();
        if(!me.gem_count()) {
            policy_.forced_bank=false;
            StockObservation empty;empty.round=cambc::round_number();empty.capture=cambc::constants::capture_time;
            policy_.inventory.update(empty);stock_target_.reset();return;
        }
        int altar=10000;const auto origin=me.position();
        for(const auto p:altars_)altar=std::min(altar,std::max(0,distance(origin,p)-1));
        const auto s=state(altar);const auto score=cambc::global_submitted_gems(team::ally),goal=cambc::constants::winning_gems;
        const bool winning=score>=goal||me.gem_count()>=goal-score;
        policy_.forced_bank=winning||heuristics::deadline(s)||(me.gem_count()>0&&(me.health()<me.max_health()*.5f||incoming*4>me.health()));
        StockObservation o;o.round=s.round;o.bag=s.bag;o.capture=s.capture;o.prism=identity_==6;o.forced=policy_.forced_bank;
        o.cashable=s.limit-s.round>altar+int(std::ceil(s.bag*s.tribute))+s.capture+4;
        if(target_>=0&&groups_[target_].unavailable_until<=unsigned(s.round)) {
            const auto visible=cambc::vision::visible_tiles();const auto&e=groups_[target_];
            if(!(visible&e.spawns).is_empty())o.pending_capture=std::holds_alternative<cambc::map::camp_group_reward_available>(e.group.state())&&distance(me.position(),e.anchor)<=s.capture;
        }
        int useful_source=-1;
        if(o.prism&&o.bag>0&&!o.forced&&policy_.inventory.phase!=Phase::liquidate&&me.can_cast()&&cambc::fuel_remaining()>65000) {
            CombatWorld world(this,camps,false);trace("stock-world");const auto menu=world.observe();trace("stock-menu");
            for(unsigned i=0;i<menu.count;++i)for(unsigned k=0;k<2;++k)if(stock_useful(menu.targets[i],k)) {
                const auto p=world.positions[i];const int g=group_at_tile_[p.y*cambc::map::width()+p.x];
                if(g<0||!groups_[g].reachable)continue;
                const auto&e=groups_[g];
                if(!fundamentals::affordable_camp(s.hp,s.maximum,e.health,e.incoming,std::max(1.f,me.strength()+me.focus()*.15f)))continue;
                o.useful=true;useful_source=g;stock_target_=p;break;
            }
        }
        const auto old=policy_.inventory.phase;policy_.inventory.update(o);
        trace("stock-end",int(policy_.inventory.phase));
        if(policy_.inventory.phase==Phase::work&&useful_source>=0)policy_.inventory.source=useful_source;
        const bool work_entry=old!=policy_.inventory.phase&&policy_.inventory.phase==Phase::work&&work_logs_<3;
        if(old!=policy_.inventory.phase&&(stock_logs_<10||work_entry)&&cambc::fuel_remaining()>50000) {
            if(work_entry)++work_logs_;else ++stock_logs_;
            std::printf("h12phase actor=%u round=%u phase=%d bag=%d useful=%d pending=%d forced=%d source=%d\n",
                identity_,s.round,int(policy_.inventory.phase),s.bag,int(o.useful),int(o.pending_capture),int(o.forced),policy_.inventory.source);
        }
    }

    template<class Target>bool fight(const Target&target,bool walked,bool dangerous) {
        if constexpr(std::is_same_v<Target,camp>)return inventory_fight(target,walked);
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
                me.submit_gems(altar);cargo_.started_submit(me.is_submitting_gems());
                return true;
            }
        }
        // The walking goals are static. Rebuilding one bitboard per altar on
        // a conversion turn needlessly stacks geometry with inventory menus.
        return move_to(altar_goals_);
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
                std::printf("h12 select actor=%u round=%u target=%d calls=%u changed=%u stock=%u kills=%u\n",identity_,round,choice,selections_,deviations_,inventory_casts_,inventory_kills_);
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
        if(policy_.inventory.phase==h12_working_inventory::Phase::work&&policy_.inventory.source==selected&&stock_target_)
            for(const auto&monster:camps)if(monster.spawn_position()==*stock_target_&&monster.is_visible()&&monster.is_alive()){target=monster;break;}
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
                if(h12_working_inventory::clear_route_blocker(round-route_progress_,own_work,true,
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
            std::vector<h12_working_inventory::Member> members;
            members.reserve(entry.group.camps().size());
            for(const auto&monster:entry.group) {
                const auto p=monster.spawn_position();
                members.push_back({p.x,p.y,visible.is_set(p)});
            }
            const auto frontier=h12_working_inventory::goal_frontier(entry.frontier_memory,members,
                !(entry.spawns&visible).is_empty()&&!selected_reward,round,entry.group.max_respawn_timer());
#if HEURISTICS_DIAGNOSTIC
            diagnostic_goals_=frontier.goals.size();
            if(!frontier.goals.empty())diagnostic_target_={frontier.goals[0].x,frontier.goals[0].y};
#endif
            for(const auto&m:frontier.goals)goals.set({m.x,m.y});
            if(frontier.discover)goals=h12_working_inventory::approach_perimeter(goals);
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
                altar_goals_=h12_working_inventory::approach_perimeter(cambc::map::altars());
                topology_ready_=true;
                return;
            }
        }
        // Dead champions retain shopping turns, but have no usable position.
        observe_stock_receipt();
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
        update_inventory(camps,incoming);
        // Safe incumbent capture is protected, except urgent liquidation.
        if (target_ >= 0 && !policy_.forced_bank) {
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
        if(policy_.inventory.phase==h12_working_inventory::Phase::work&&policy_.inventory.source>=0) {
            target_=policy_.inventory.source;partial_clear_=false;
        }
        if(target_>=0) {
            auto&entry=groups_[target_];
            const bool admitted=entry.reachable&&entry.unavailable_until<=round;
            const bool stock_redirect=policy_.inventory.phase==h12_working_inventory::Phase::work&&policy_.inventory.source>=0&&policy_.inventory.source!=target_;
            if(!stock_redirect&&h12_working_inventory::execution_lease(round,route_progress_,cambc::constants::capture_time,admitted)) {
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
            std::cout << "working-inventory error: " << error.what() << '\n';
        } catch (...) {
            std::cout << "working-inventory error: unknown exception\n";
        }
        cambc::end_turn();
    }
}

} // namespace h12_inventory_runtime
