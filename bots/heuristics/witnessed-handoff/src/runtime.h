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
#include <string_view>
#include "../../common/src/decision.h"
#include "frontier.h"
#include "flow.h"
#include "../../common/src/draft.h"
#include "../../common/src/equipment.h"
#include "../../common/src/capture.h"

namespace h12_handoff_runtime {
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
    int canonical=0;
    float health = 0, incoming = 0;
    bool monolith = false;
    std::vector<vec2> positions;
    
    unsigned unavailable_until = 0;
    int observed_round=-1;
    bool reachable=true;
    h12_witnessed_handoff::FrontierMemory frontier_memory{};
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
    int solo_started_=-1;
    unsigned route_actions_=0;
    unsigned handoff_logs_=0;
    unsigned contract_logs_=0,witness_clears_=0,witness_pads_=0,witness_outside_=0,witness_near_=0;
    h12_witnessed_handoff::Controller handoff_;
    bool holder_bank_=false;
    int outside_goal_=-1,outside_since_=-1;
    int outside_departures_=0;
    h12_witnessed_handoff::Receipts receipts_;
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
                const int tile=p.y*cambc::map::width()+p.x+1;
                if(!entry.canonical||tile<entry.canonical)entry.canonical=tile;
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
            me.buy(item,count);handoff_.purchased();
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
        const int action_round=cambc::round_number();
        const int source=group_at_tile_[p.y*cambc::map::width()+p.x];
        const bool live_before=source>=0&&std::holds_alternative<cambc::map::camp_group_alive>(groups_[source].group.state());
        const auto finish=[&] {
            if(acted){route_progress_=cambc::round_number();if(live_before&&int(cambc::round_number())==action_round)maybe_offer(source);}
            return acted;
        };
        if(identity_==10)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const int x=p.x+dx,y=p.y+dy;
            if(x<0||y<0||x>=cambc::map::width()||y>=cambc::map::height())continue;
            const int neighbour=group_at_tile_[y*cambc::map::width()+x];
            if(neighbour>=0&&neighbour!=source&&groups_[neighbour].incoming>0) {
                groups_[source].unavailable_until=cambc::round_number()+8;
                target_=-1;route_target_=-1;return true;
            }
        }
        // Cheap same-target production sequence. Vesper can reset a ranged
        // allowance spent by walking as well as one spent by the first hit.
        if(me.can_attack()&&me.attack_tiles().is_set(p)){me.attack(target);acted=true;}
        if(cambc::fuel_remaining()<25000)return finish();
        if(!cambc::vision::in_vision(p)||target.state().is_dead()||!target.is_visible())return finish();
        if(me.can_cast())for(const auto&a:me.abilities()) {
            if(cambc::fuel_remaining()<24000)break;
            if(!a.is_ready())continue;
            if(a.is_attack_reset()&&a.can_target_self()&&distance(me.position(),p)<=me.attack_range()) {
                if(a.use()){acted=true;break;}
            }
            if(!a.can_target_camps()||!(a.is_damage()||a.is_cc()))continue;
            const bool exclusive=std::visit([](const auto&v){return v.exclusive_with_movement;},a.info().value());
            if(walked&&exclusive)continue;
            if(a.valid_tiles().is_set(p)){acted=a.use(target)||acted;break;}
        }
        if(cambc::fuel_remaining()>18000&&cambc::vision::in_vision(p)&&!target.state().is_dead()&&target.is_visible()&&me.can_attack()) {
            if(me.attack_tiles().is_set(p)){me.attack(target);acted=true;}
        }
        return finish();
    }
    float camp_reduction()const {
        return jungle_shards_>=5?1-item_type::jungle_emblem_of_focus.passive_magnitude():1;
    }
    void handoff_log(const char*event,int source=-1) {
        const bool contract=std::string_view(event)!="payout"&&std::string_view(event)!="bank"&&std::string_view(event)!="death"&&std::string_view(event)!="failed-hold";
        auto&count=contract?contract_logs_:handoff_logs_;
        if(count>=(contract?24u:20u)||cambc::fuel_remaining()<18000)return;
        ++count;
        std::printf("h12handoff actor=%u round=%u event=%s source=%d bag=%u gold=%u\n",
            identity_,cambc::round_number(),event,source,cambc::self().gem_count(),cambc::self().gold_count());
    }
    int canonical(int source)const {return source>=0&&source<int(groups_.size())?groups_[source].canonical:0;}
    int resolve(int tile)const {
        if(tile<=0||tile>65535||tile>int(group_at_tile_.size()))return -1;
        const int id=group_at_tile_[tile-1];
        return id>=0&&groups_[id].canonical==tile?id:-1;
    }
    struct Adapter {
        Bot&bot;
        std::uint64_t read()const {
            if(cambc::constants::shared_memory_bytes<8)return 0;
            std::array<std::byte,8> bytes{};cambc::shared_memory.read(0,bytes);
            std::uint64_t value=0;
            for(unsigned i=0;i<8;++i)value|=std::uint64_t(std::to_integer<unsigned>(bytes[i]))<<(8*i);
            return value;
        }
        bool write_if(std::uint64_t expected,std::uint64_t next,int owner_round)const {
            // Live host bytes, read/check/write in a short reserved helper.
            // No movement, scan or formatted logging may suspend this section.
            if(cambc::constants::shared_memory_bytes<8||cambc::fuel_remaining()<9000||
                int(cambc::round_number())!=owner_round)return false;
            if(read()!=expected||int(cambc::round_number())!=owner_round)return false;
            std::array<std::byte,8> bytes{};
            for(unsigned i=0;i<8;++i)bytes[i]=std::byte((next>>(8*i))&255);
            cambc::shared_memory.write(0,bytes);return true;
        }
        h12_witnessed_handoff::Observation observe(const h12_witnessed_handoff::Packet&p)const {
            h12_witnessed_handoff::Observation o;auto&me=cambc::self();
            o.round=cambc::round_number();o.type=bot.identity_;o.capture=cambc::constants::capture_time;
            o.alive=me.is_alive();o.bag=me.gem_count();o.hp=me.health();o.maximum=me.max_health();
            o.channel=me.is_recalling()||me.is_submitting_gems()||me.is_teleporting()||bot.recovering_;
            const int id=bot.resolve(p.tile);if(id<0)return o;
            const auto&g=bot.groups_[id];
            o.source_valid=g.reachable&&!g.monolith&&cambc::constants::max_rounds<=65535;
            if(!o.alive)return o;
            if(!h12_witnessed_handoff::source_observation_needed(p,o.round,o.type))return o;
            o.on_source=g.spawns.is_set(me.position());
            const auto visible=cambc::vision::visible_tiles();
            if(!(g.spawns&visible).is_empty()) {
                const auto state=g.group.state();
                o.reward=std::holds_alternative<cambc::map::camp_group_reward_available>(state);
                o.renewed=std::holds_alternative<cambc::map::camp_group_alive>(state);
            }
            const auto occupied=cambc::vision::champion_tiles();
            for(const auto q:g.positions)if(!bot.terrain_.is_set(q)) {
                ++o.usable_pads;
                if(!occupied.is_set(q)||q==me.position())o.approach=std::min(o.approach,distance(me.position(),q));
            }
            if(o.type==p.clearer&&cambc::fuel_remaining()>18000)
                for(const auto&a:cambc::vision::champions(team::ally))
                    if(unsigned(a.champion_name())==p.holder&&a.is_alive()&&g.spawns.is_set(a.position()))o.holder_visible_on_source=true;
            // A current unpaid capture, channel, cargo or recovery is a real
            // commitment. A short solo journey may yield to this one offer.
            const bool capture_commitment=bot.capture_watch_.source()>=0&&bot.capture_watch_.source()!=id;
            o.eligible=!bot.recovering_&&!capture_commitment&&o.bag==0&&o.hp>=o.maximum*.62f&&!o.channel;
            return o;
        }
        bool approach(int tile) {
            const int id=bot.resolve(tile);if(id<0||cambc::fuel_remaining()<42000)return false;
            bot.target_=id;return bot.move_to(bot.groups_[id].spawns);
        }
        bool depart(int tile,int outside) {
            const int id=bot.resolve(tile);if(id<0||outside<0||outside>=int(bot.groups_.size())||cambc::fuel_remaining()<62000)return false;
            // Target the nearest real outside tile; moving along a long source
            // is never sufficient. The controller re-observes actual position.
            return bot.move_to(~bot.groups_[id].spawns&~bot.terrain_);
        }
    };
    bool handoff_dispatch() {
        if(cambc::constants::shared_memory_bytes<8||cambc::fuel_remaining()<18000)return false;
        Adapter adapter{*this};const auto before=h12_witnessed_handoff::decode(adapter.read());
        const auto action=handoff_.dispatch(adapter);
        const auto after=h12_witnessed_handoff::decode(adapter.read());
        if(after.phase!=before.phase&&h12_witnessed_handoff::same_key(after,before)) {
            using P=h12_witnessed_handoff::Phase;
            if(after.phase==P::accept)handoff_log("accept",resolve(after.tile));
            if(after.phase==P::released)handoff_log("released",resolve(after.tile));
            if(after.phase==P::paid)handoff_log("holder-paid",resolve(after.tile));
            if(after.phase==P::cancel)handoff_log("cancel",resolve(after.tile));
        }
        using K=h12_witnessed_handoff::Kind;
        if(action.kind==K::outside) {
            if(outside_departures_!=handoff_.departures){outside_departures_=handoff_.departures;outside_goal_=action.outside;outside_since_=cambc::round_number();target_=-1;route_target_=-1;}
            return false;
        }
        if(action.kind==K::banking){
            auto&me=cambc::self();
            if(me.is_alive()&&!recovering_&&!me.is_recalling()&&!me.is_submitting_gems()&&!me.is_teleporting())bank();
            return true;
        }
        return action.kind==K::holding||action.kind==K::approaching;
    }
    void maybe_offer(int source) {
        auto&me=cambc::self();const int round=cambc::round_number();
        if(identity_!=10||source<0||cambc::fuel_remaining()<41000||cambc::constants::capture_time<4)return;
        const auto&g=groups_[source];
        if(!std::holds_alternative<cambc::map::camp_group_reward_available>(g.group.state()))return;
        ++witness_clears_;
        if(!g.spawns.is_set(me.position()))return;
        ++witness_pads_;
        int outside=-1,best=10000;
        const auto visible=cambc::vision::visible_tiles();
        for(std::size_t i=0;i<groups_.size();++i) {
            if(cambc::fuel_remaining()<36000)break;
            const auto&other=groups_[i];const int d=distance(me.position(),other.anchor);
            if(int(i)==source||!other.reachable||other.monolith||other.unavailable_until>unsigned(round)||d>8||d>=best||
                (other.spawns&visible).is_empty())continue;
            if(!std::holds_alternative<cambc::map::camp_group_alive>(other.group.state()))continue;
            const float dps=me.strength()*1.125f+me.focus()*.12f;
            if(!fundamentals::affordable_camp(me.health(),me.max_health(),other.health,other.incoming*camp_reduction(),dps))continue;
            outside=int(i);best=d;
        }
        if(outside<0||cambc::fuel_remaining()<35000)return;
        ++witness_outside_;
        const auto occupied=cambc::vision::champion_tiles();int pads=0;
        for(const auto q:g.positions)if(!terrain_.is_set(q))++pads;
        int holder=-1,approach=10000;
        for(const auto&a:cambc::vision::champions(team::ally)) {
            if(cambc::fuel_remaining()<28000)break;
            const int type=int(a.champion_name());
            if(type==10||type==3||type==4||!a.is_alive()||a.health()<a.max_health()*.62f)continue;
            int travel=10000;
            for(const auto q:g.positions)if(!terrain_.is_set(q)&&(!occupied.is_set(q)||q==a.position()))travel=std::min(travel,distance(a.position(),q));
            if(travel<approach){holder=type;approach=travel;}
        }
        if(holder<0||approach>1||int(cambc::round_number())!=round)return;
        ++witness_near_;
        Adapter adapter{*this};h12_witnessed_handoff::Proposal p;
        p.tile=g.canonical;p.holder=holder;p.outside=outside;p.approach=approach;p.pads=pads;
        p.capture=cambc::constants::capture_time;p.fresh_witness=true;p.visible_eligible_holder=true;p.outside_viable=true;
        if(handoff_.offer(adapter,p))handoff_log("offer",source);
    }
    struct Hold {int source=-1;bool reward=false,respawning=false;};
    Hold held_state()const {
        Hold h;auto&me=cambc::self();if(!me.is_alive())return h;
        const auto p=me.position();h.source=group_at_tile_[p.y*cambc::map::width()+p.x];
        if(h.source>=0) {
            // Our occupied member tile is visible. No hidden group query.
            const auto value=groups_[h.source].group.state();
            h.reward=std::holds_alternative<cambc::map::camp_group_reward_available>(value);
            h.respawning=std::holds_alternative<cambc::map::camp_group_respawning>(value);
        }
        return h;
    }
    void update_finance() {
        auto&me=cambc::self();const bool was_alive=receipts_.alive;
        const auto h=held_state();
        const auto gain=receipts_.sample(cambc::round_number(),me.gem_count(),me.gold_count(),me.is_alive(),me.is_submitting_gems(),h.source,h.reward,h.respawning);
        if(gain.payout) {
            Adapter adapter{*this};handoff_.sync_release(adapter.read(),cambc::round_number());
            handoff_.own_payout(canonical(gain.source),me.gem_count());
            handoff_log(outside_goal_==gain.source?"outside-income":"payout",gain.source);
            if(!handoff_.donation_debt()){outside_goal_=-1;outside_since_=-1;}
            target_=-1;route_target_=-1;
        }
        if(gain.bank){handoff_.banked();handoff_log("bank");target_=-1;route_target_=-1;}
        if(was_alive&&!me.is_alive()){handoff_.died();handoff_log("death");}
        policy_.forced_bank=false;if(!me.is_alive()||!me.gem_count())return;
        int altar=10000;for(const auto p:altars_)altar=std::min(altar,std::max(0,distance(me.position(),p)-1));
        const auto score=cambc::global_submitted_gems(team::ally),goal=cambc::constants::winning_gems;
        policy_.forced_bank=score>=goal||me.gem_count()>=goal-score||heuristics::deadline(state(altar));
    }

    template<class Target>bool fight(const Target&target,bool walked,bool dangerous) {
        if constexpr(std::is_same_v<Target,camp>)return ordinary_camp_fight(target,walked);
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
            if constexpr(Policy::enabled)return handoff_.needs_bank()||policy_.bank(state(std::max(0,nearest-1)));
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
                me.submit_gems(altar);
                if(me.is_submitting_gems())receipts_.submit(cambc::round_number(),me.gem_count(),cambc::constants::submit_gem_duration_multiplier);
                return true;
            }
        }
        // The walking goals are static. Rebuilding one bitboard per altar on
        // a conversion turn needlessly stacks geometry with shield observation.
        return move_to(altar_goals_);
    }

    bool capture_exit_work(int selected,const std::vector<camp>&camps) {
        auto&me=cambc::self();
        if(cambc::fuel_remaining()<26000)return false;
        // A blocked step must still allow an immediately cashable bag.
        if(me.gem_count()>0)
            for(const auto altar:altars_)if(distance(me.position(),altar)<=1) {
                if(bank())return true;
            }
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
                if(h12_witnessed_handoff::clear_route_blocker(4,false,true,groups_[group].incoming,true,splash_safe)
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
        if(entry.unavailable_until<=round&&!capture_watch_.expired(selected,round,cambc::constants::capture_time))return false;
        if(entry.unavailable_until<=round)entry.unavailable_until=round+12;
        handoff_log("failed-hold",selected);
        target_=-1;route_target_=-1;contest_since_=-1;
        h12_witnessed_handoff::leave_or_work([&]{return move_to(~entry.spawns&~terrain_);},
            [&]{return capture_exit_work(selected,camps);});
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
        const float dps = strength * 1.125f + me.focus() * .12f;
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
                        if(capture_timeout(int(i),round,camps))return;
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
                std::printf("h12 select actor=%u round=%u target=%d calls=%u changed=%u witness=%u,%u,%u,%u offers=%d releases=%d\n",identity_,round,choice,selections_,deviations_,witness_clears_,witness_pads_,witness_outside_,witness_near_,handoff_.offers,handoff_.releases);
        }
        partial_clear_ = selected < 0 && partial_target >= 0;
        trace("farm-choice",selected<0?partial_target:selected);
        if (partial_clear_) selected = partial_target;
        if(target_!=selected)solo_started_=round;
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
                if(h12_witnessed_handoff::clear_route_blocker(round-route_progress_,own_work,true,
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
            std::vector<h12_witnessed_handoff::Member> members;
            members.reserve(entry.group.camps().size());
            for(const auto&monster:entry.group) {
                const auto p=monster.spawn_position();
                members.push_back({p.x,p.y,visible.is_set(p)});
            }
            const auto frontier=h12_witnessed_handoff::goal_frontier(entry.frontier_memory,members,
                !(entry.spawns&visible).is_empty()&&!selected_reward,round,entry.group.max_respawn_timer());
#if HEURISTICS_DIAGNOSTIC
            diagnostic_goals_=frontier.goals.size();
            if(!frontier.goals.empty())diagnostic_target_={frontier.goals[0].x,frontier.goals[0].y};
#endif
            for(const auto&m:frontier.goals)goals.set({m.x,m.y});
            if(frontier.discover)goals=h12_witnessed_handoff::approach_perimeter(goals);
        }
        if constexpr(Policy::enabled)goals.set(origin,false);
        if (move_to(goals)) return;
        // A failed walk leaves the ordinary selector free to choose another job.

    }

public:
    void after_turn() {
        if(!initialized_||!topology_ready_||cambc::fuel_remaining()<5000)return;
        const auto h=held_state();auto&me=cambc::self();
        receipts_.after(me.gem_count(),me.gold_count(),me.is_alive(),h.source,h.reward);
    }
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
                altar_goals_=h12_witnessed_handoff::approach_perimeter(cambc::map::altars());
                topology_ready_=true;
                return;
            }
        }
        // Dead champions retain shopping turns, but have no usable position.
        update_finance();
        if (!me.is_alive()) { handoff_dispatch();shop(); recovering_ = false; target_ = -1; capture_watch_.reset(); return; }
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) {handoff_dispatch();return;}
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
        if (recovering_) {handoff_dispatch();recover(camps, enemies);return;}
        if(capture_watch_.source()>=0) {
            const int held=capture_watch_.source();const auto&entry=groups_[held];
            if(!entry.spawns.is_set(me.position()))capture_watch_.reset();
            else if(!std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state()))capture_watch_.reset();
            else if(capture_timeout(held,cambc::round_number(),camps))return;
        }
        // Physical hold continuity does not depend on the selected solo job.
        const auto held=held_state();
        if(held.source>=0&&held.reward&&capture_timeout(held.source,cambc::round_number(),camps))return;
        if(handoff_dispatch())return;
        if(held.source>=0&&held.reward&&!policy_.forced_bank) {
            if(capture_timeout(held.source,cambc::round_number(),camps))return;
            return;
        }
        if (bank()) return;
        // Defend against an adjacent threat, without chasing opponents.
        for (const auto& enemy : enemies)
            if (distance(me.position(), enemy.position()) <= 1 && fight(enemy, false, true)) return;
        if (!spawn && me.champion_name() != cambc::champion_type::tavi && me.gold_count() >= 300) {
            recovering_ = true;
            recover(camps, enemies);
            return;
        }
        if constexpr(Policy::enabled){enemy_positions_.clear();for(const auto&e:enemies)enemy_positions_.push_back(e.position());}
        const unsigned round=cambc::round_number();
        if(outside_goal_>=0&&round-outside_since_<35&&groups_[outside_goal_].unavailable_until<=round) {
            const int id=outside_goal_;auto&entry=groups_[id];
            const auto visible=cambc::vision::visible_tiles();
            bool reward=false,usable=true;
            if(!(entry.spawns&visible).is_empty()) {
                const auto status=entry.group.state();
                if(const auto*respawn=std::get_if<cambc::map::camp_group_respawning>(&status)) {
                    entry.unavailable_until=round+std::max(1,respawn->timer);usable=false;
                }
                reward=std::holds_alternative<cambc::map::camp_group_reward_available>(status);
            }
            if(usable) {target_=id;partial_clear_=false;work_source(id,reward,camps,visible,round,me.position());return;}
        }
        if(outside_goal_>=0){outside_goal_=-1;outside_since_=-1;}
        // A winning auction entry owns one bounded work ticket. Re-auctioning
        // after every partial observation can alternate between long rows
        // before either frontier is explored. Real damage or closer travel
        // extends progress, while the absolute ticket still expires.
        if(target_>=0&&route_target_==target_&&groups_[target_].unavailable_until<=round) {
            const int id=target_;auto&entry=groups_[id];
            if(round-route_progress_<12&&solo_started_>=0&&round-solo_started_<90) {
                const auto visible=cambc::vision::visible_tiles();bool reward=false,usable=true;
                if(!(entry.spawns&visible).is_empty()) {
                    const auto status=entry.group.state();
                    if(const auto*respawn=std::get_if<cambc::map::camp_group_respawning>(&status)) {
                        entry.unavailable_until=round+std::max(1,respawn->timer);entry.frontier_memory.reset();usable=false;
                    }
                    reward=std::holds_alternative<cambc::map::camp_group_reward_available>(status);
                }
                if(usable){work_source(id,reward,camps,visible,round,me.position());return;}
            } else entry.unavailable_until=round+12;
            target_=-1;route_target_=-1;solo_started_=-1;
        }
        farm(camps, cambc::vision::champions(team::ally));
    }
};

template<class Policy> int run() {
    Bot<Policy> bot;
    while (true) {
        try {
            if (cambc::is_draft()) draft<Policy>();
            else {bot.turn();bot.diagnostic();bot.after_turn();}
        } catch (const std::exception& error) {
            std::cout << "witnessed-handoff error: " << error.what() << '\n';
        } catch (...) {
            std::cout << "witnessed-handoff error: unknown exception\n";
        }
        cambc::end_turn();
    }
}

} // namespace h12_handoff_runtime
