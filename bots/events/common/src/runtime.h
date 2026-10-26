#include <cambc.h>
#include <array>
#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>
#include "events.h"
#include "turn_completion.h"
#include "event_kernel.h"
#include "routes.h"
#include "focus_caller.h"
#include "planning_epoch.h"
#ifndef P11_EARLY_FALLBACK_DETAIL
#define P11_EARLY_FALLBACK_DETAIL 0
#endif
#if defined(P11_EARLY_FALLBACK_DETAIL) && P11_EARLY_FALLBACK_DETAIL
namespace p11_overlap {
// The public catalog emits strictly increasing, nonnegative group indices.
// At width three, its raw first-entry prefix is the first entry of the unique
// top-three list. Retain every screen, its stable ties and both promotions;
// unsupported supplied menus retain the original complete generic helper.
template<class Policy> std::vector<int> unique_three_frontier(
        const p11::State&s,std::span<const p11::Candidate> candidates,
        int limit,int incumbent,int explore) {
    if(limit!=3)return p11::shortlist<Policy>(s,candidates,limit,incumbent,explore);
    int previous=-1;
    for(const auto&c:candidates) {
        if(c.id<=previous)return p11::shortlist<Policy>(s,candidates,limit,incumbent,explore);
        previous=c.id;
    }
    struct Rank {double value;int id;};
    std::array<Rank,3> best{};int count=0;
    bool incumbent_finite=false,explore_finite=false;
    for(const auto&c:candidates) {
        const double value=Policy::screen(s,c);
        if(!std::isfinite(value))continue;
        incumbent_finite|=c.id==incumbent;explore_finite|=c.id==explore;
        int at=0;while(at<count&&!(value>best[at].value))++at;
        if(at>=3)continue;
        for(int i=std::min(count,2);i>at;--i)best[i]=best[i-1];
        best[at]={value,c.id};if(count<3)++count;
    }
    std::vector<int> order;order.reserve(std::min(std::size_t(3),candidates.size()));
    auto add=[&](int id) {
        if(id<0||order.size()>=3||std::find(order.begin(),order.end(),id)!=order.end())return;
        order.push_back(id);
    };
    if(count)add(best[0].id);
    if(incumbent_finite)add(incumbent);
    if(explore_finite)add(explore);
    for(int i=0;i<count;++i)add(best[i].id);
    return order;
}
}
#endif

#ifndef P11_BOUNDED_INITIALIZATION
#define P11_BOUNDED_INITIALIZATION 0
#endif
#ifndef P11_CACHE_PUBLIC_WORK
#define P11_CACHE_PUBLIC_WORK 0
#endif
#ifndef P11_MOVE_FUEL_GUARD
#define P11_MOVE_FUEL_GUARD 0
#endif
#ifndef P11_EXPEDITION_RECEIPT
#define P11_EXPEDITION_RECEIPT 0
#endif
#ifndef P11_CACHE_ALTAR_MINIMUM
#define P11_CACHE_ALTAR_MINIMUM 0
#endif
#ifndef P11_GOAL_RECEIPT
#define P11_GOAL_RECEIPT 0
#endif
#ifndef P11_GOAL_RECEIPT_ROUND
#define P11_GOAL_RECEIPT_ROUND 48
#endif
#ifndef P11_CONTEST_CONTINUATION
#define P11_CONTEST_CONTINUATION 0
#endif
#if P11_CONTEST_CONTINUATION
#include "contest_continuation.h"
#endif
#if P11_GOAL_RECEIPT
#include "goal_receipt.h"
#endif
#if P11_CACHE_ALTAR_MINIMUM
#include "altar_minimum.h"
#endif
#if P11_EXPEDITION_RECEIPT
#include "expedition_receipt.h"
#endif
#if P11_BOUNDED_INITIALIZATION
#include "initialization.h"
#endif
#if P11_CACHE_PUBLIC_WORK
#include "public_work.h"
#endif
#if P11_MOVE_FUEL_GUARD
#include "move_fuel.h"
#endif

namespace p11_runtime {
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

void draft() {
    constexpr std::array order{cambc::champion_draft::tavi, cambc::champion_draft::varg,
        cambc::champion_draft::vendetta, cambc::champion_draft::vesper_vale,
        cambc::champion_draft::marcella};
    const auto selected = cambc::draft::current_draft(team::ally);
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
#if P11_CACHE_PUBLIC_WORK
    p11::PublicHitWork public_work{};
#endif
#if P11_CACHE_ALTAR_MINIMUM
    p11::AltarMinimum altar_minimum{};
#endif
};

template<class Policy> class Bot {
    struct TargetObservation {camp target;float hp,incoming;};
    static auto& public_models() {static std::vector<std::vector<p11::Monster>> value;return value;}
    struct Routes {
        int range=-1;
        std::vector<std::vector<std::uint16_t>> entry,capture;
        std::vector<std::uint16_t> altar_field;
        std::vector<int> altar;
    };
    static auto& routes() {static Routes value;return value;}
    static auto& information_clock() {
        static typename Policy::InformationClock clock;
        return clock;
    }
    static auto& commitment_context() {
        struct Context {
            typename Policy::CommitmentDomain domain;
            typename Policy::CommitmentBoard board;
            std::vector<int> roster,job_codes,group_indices;
            int self_slot=-1;
            bool supported=false;
        };
        static Context value;return value;
    }
    bool publish_commitment(int group,int observed_round) {
        auto& context=commitment_context();
        if(!context.supported)return false;
        const int job=group<0?-1:context.job_codes[group];
        const auto result=Policy::publish_commitment(context.domain,context.self_slot,job,observed_round,
            []{return int(cambc::round_number());},[]{return cambc::fuel_remaining();},
            [] {
                std::uint64_t word=0;
                cambc::shared_memory.read(0,std::span<std::uint64_t>(&word,1));
                return word;
            },[](std::uint64_t word) {
                cambc::shared_memory.write(0,std::span<const std::uint64_t>(&word,1));
            });
        if(result.status!=Policy::CommitmentStatus::written)return false;
        context.board=result.board;return true;
    }
    bool committed_elsewhere(int group) const {
        const auto& context=commitment_context();
        return context.supported&&Policy::claimed(context.board,context.self_slot,context.job_codes[group]);
    }
    std::vector<Group> groups_;
    std::vector<int> group_at_tile_;
    std::vector<vec2> altars_;
    bitboard terrain_;
    bool initialized_ = false;
    bool recovering_ = false;
    bool partial_clear_ = false;
    bool has_non_monolith_gems_ = false;
    int target_ = -1;
    unsigned monster_shards_ = 0, jungle_shards_ = 0;
    unsigned identity_ = 0;
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
    struct TempoMoveRequest {
        const p11::State& state;
        const std::vector<champion>& enemies;
        int round;
        bool refused=false,aborted=false;
        p11_tempo::conversion::Input input{};
        std::array<p11_tempo::conversion::Threat,32> threats{};
        std::size_t count=0;
    };
    struct TempoMoveResult {bool moved=false,handled=false;};
    TempoMoveRequest* tempo_move_request_=nullptr;
    static_assert(P11_MOVE_FUEL_GUARD,"Tempo conversion guard requires the existing movement fuel guard");
#endif
#if P11_EXPEDITION_RECEIPT
    p11::expedition::Packet expedition_receipt_;
#endif
#if P11_GOAL_RECEIPT
    p11::goal_receipt::Packet goal_receipt_;
#endif
#if P11_CONTEST_CONTINUATION
    p11::contest::Continuation contest_continuation_;
#endif

#if P11_BOUNDED_INITIALIZATION
    bool initialization_started_ = false;
    std::vector<cambc::map::camp_group> initialization_groups_;
    std::size_t initialization_group_cursor_ = 0;
    std::vector<camp> initialization_members_;
    std::size_t initialization_member_cursor_ = 0;
    std::optional<Group> initializing_group_;

    // Public immutable metadata only: a voluntary owner-turn boundary never
    // carries a live observation or a partially executed gameplay action.
    // Group/member cursors advance only after their complete metadata write.
    bool initialize_bounded() {
        auto work = p11::initialization::Work{
            [] { return cambc::fuel_remaining(); },
            [&] { return initialized_; },
            [&] { return initialization_started_; },
            [&] {
            if constexpr(requires{Policy::bounded_scan;}) public_models().clear();
            if constexpr(requires{typename Policy::InformationClock;}) information_clock().reset();
            terrain_ = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
                | cambc::map::spawn_region(team::enemy);
            altars_ = cambc::map::altars().positions();
            group_at_tile_.assign(cambc::map::width() * cambc::map::height(), -1);
            initialization_groups_ = cambc::map::camp_groups();
            initialization_started_ = true;
            },
            [&] { return initialization_group_cursor_ < initialization_groups_.size(); },
            [&] { return bool(initializing_group_); },
            [&] {
                const auto group = initialization_groups_[initialization_group_cursor_];
                const auto members = group.camps();
                initialization_members_.assign(members.begin(), members.end());
                initializing_group_.emplace(Group{group, group.spawn_tiles(),
                    initialization_members_.front().spawn_position()});
#if P11_CACHE_ALTAR_MINIMUM
                initializing_group_->altar_minimum.begin(cambc::constants::submit_gems_range,altars_.size());
#endif
                initialization_member_cursor_ = 0;
                if constexpr(requires{Policy::bounded_scan;}) public_models().emplace_back();
            },
#if P11_CACHE_ALTAR_MINIMUM
            [&] { return initialization_member_cursor_ < initialization_members_.size()
                || !initializing_group_->altar_minimum.ready(); },
#else
            [&] { return initialization_member_cursor_ < initialization_members_.size(); },
#endif
            [&] {
#if P11_CACHE_ALTAR_MINIMUM
                // Each pure scalar altar step gets the existing member
                // reserve; the close callback never hides an altar loop.
                if(initialization_member_cursor_==initialization_members_.size()) {
                    auto&minimum=initializing_group_->altar_minimum;
                    minimum.append(distance(initializing_group_->anchor,altars_[minimum.cursor()]));
                    return;
                }
#endif
                const auto monster = initialization_members_[initialization_member_cursor_];
                const auto p = monster.spawn_position();
                group_at_tile_[p.y * cambc::map::width() + p.x] = static_cast<int>(groups_.size());
                const auto stats = monster.base_stats();
                if constexpr(requires{Policy::bounded_scan;})
                    public_models().back().push_back({stats.base_health,stats.base_strength,0});
                initializing_group_->health += stats.base_health;
                initializing_group_->incoming += stats.base_strength;
                initializing_group_->monolith |= monster.is_monolith();
                ++initialization_member_cursor_;
            },
            [&] {
            has_non_monolith_gems_ |= !initializing_group_->monolith
                && initializing_group_->group.gem_value() > 0;
            groups_.push_back(std::move(*initializing_group_));
            initializing_group_.reset();
            initialization_members_.clear();
            ++initialization_group_cursor_;
            },
            [&] {
        identity_ = static_cast<unsigned>(cambc::self().champion_name());
        if constexpr(requires{Policy::committed_assignment;}) {
            auto& context=commitment_context();
            context.roster.clear();
            for(const auto& ally:cambc::champions(team::ally))
                context.roster.push_back(int(static_cast<cambc::handle_champion_t>(ally)));
            std::sort(context.roster.begin(),context.roster.end());
            const int self_id=int(static_cast<cambc::handle_champion_t>(cambc::self()));
            const auto found=std::find(context.roster.begin(),context.roster.end(),self_id);
            context.self_slot=found==context.roster.end()?-1:int(found-context.roster.begin());
            context.domain={int(context.roster.size()),int(groups_.size()),int(cambc::constants::max_rounds),cambc::shared_memory.size()};
            context.supported=context.self_slot>=0
                &&std::adjacent_find(context.roster.begin(),context.roster.end())==context.roster.end()
                &&Policy::commitments_supported(context.domain,int(cambc::round_number()));
            std::vector<std::pair<unsigned,int>> order;order.reserve(groups_.size());
            for(int i=0;i<int(groups_.size());++i)
                order.push_back({unsigned(static_cast<cambc::handle_camp_group_t>(groups_[i].group)),i});
            std::sort(order.begin(),order.end());
            context.job_codes.resize(groups_.size());context.group_indices.resize(groups_.size());
            for(int ordinal=0;ordinal<int(order.size());++ordinal) {
                context.job_codes[order[ordinal].second]=ordinal;
                context.group_indices[ordinal]=order[ordinal].second;
            }
        }
        if constexpr(requires{Policy::geodesic;})routes()=Routes{};
        initialization_groups_.clear();
        initialized_ = true;
            }
        };
        return p11::initialization::resume(work);
    }
#endif

    void initialize() {
        if constexpr(requires{Policy::bounded_scan;}) public_models().clear();
        if constexpr(requires{typename Policy::InformationClock;}) information_clock().reset();
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
                if constexpr(requires{Policy::bounded_scan;}) {
                    auto&cache=public_models();if(cache.size()<=groups_.size())cache.emplace_back();
                    cache.back().push_back({stats.base_health,stats.base_strength,0});
                }
                entry.health += stats.base_health;
                entry.incoming += stats.base_strength;
                entry.monolith |= monster.is_monolith();
            }
            has_non_monolith_gems_ |= !entry.monolith && entry.group.gem_value() > 0;
            groups_.push_back(std::move(entry));
        }
        identity_ = static_cast<unsigned>(cambc::self().champion_name());
        if constexpr(requires{Policy::committed_assignment;}) {
            auto& context=commitment_context();
            context.roster.clear();
            for(const auto& ally:cambc::champions(team::ally))
                context.roster.push_back(int(static_cast<cambc::handle_champion_t>(ally)));
            std::sort(context.roster.begin(),context.roster.end());
            const int self_id=int(static_cast<cambc::handle_champion_t>(cambc::self()));
            const auto found=std::find(context.roster.begin(),context.roster.end(),self_id);
            context.self_slot=found==context.roster.end()?-1:int(found-context.roster.begin());
            context.domain={int(context.roster.size()),int(groups_.size()),int(cambc::constants::max_rounds),cambc::shared_memory.size()};
            context.supported=context.self_slot>=0
                &&std::adjacent_find(context.roster.begin(),context.roster.end())==context.roster.end()
                &&Policy::commitments_supported(context.domain,int(cambc::round_number()));
            std::vector<std::pair<unsigned,int>> order;order.reserve(groups_.size());
            for(int i=0;i<int(groups_.size());++i)
                order.push_back({unsigned(static_cast<cambc::handle_camp_group_t>(groups_[i].group)),i});
            std::sort(order.begin(),order.end());
            context.job_codes.resize(groups_.size());context.group_indices.resize(groups_.size());
            for(int ordinal=0;ordinal<int(order.size());++ordinal) {
                context.job_codes[order[ordinal].second]=ordinal;
                context.group_indices[ordinal]=order[ordinal].second;
            }
        }
        if constexpr(requires{Policy::geodesic;})routes()=Routes{};
        initialized_ = true;
    }

    bool prepare_routes() const {
        auto&cache=routes();const int range=cambc::self().attack_range();
        if(cache.range!=range){cache=Routes{};cache.range=range;}
        if(cache.altar_field.empty()) {
            if(cambc::fuel_remaining()<75000)return false;
            bitboard roots;for(const auto altar:altars_)roots|=around(altar,cambc::constants::submit_gems_range);
            roots&=~terrain_;cambc::distance_field field;bitboard visited;
            field.compute(terrain_,roots,visited);cache.altar_field=field.export_linear_buffer();
        }
        while(cache.entry.size()<groups_.size()) {
            // Immutable preprocessing is an explicit bounded activity. Never
            // suspend an ordinary turn inside a large setup loop.
            if(cambc::fuel_remaining()<75000)return false;
            const auto&e=groups_[cache.entry.size()];bitboard goals;
            std::vector<int> tiles;const int width=cambc::map::width();
            for(const auto p:e.spawns.positions()){goals|=around(p,range);tiles.push_back(p.y*width+p.x);}
            goals&=~terrain_;cambc::distance_field field;bitboard visited;
            field.compute(terrain_,goals,visited);cache.entry.push_back(field.export_linear_buffer());
            field.compute(terrain_,e.spawns,visited);cache.capture.push_back(field.export_linear_buffer());
            cache.altar.push_back(p11::minimum_distance(cache.altar_field,tiles));
        }
        return true;
    }

    void capital_shop() {
        auto&me=cambc::self();const int round=cambc::round_number();const unsigned gold=me.gold_count();
        const auto monster=item_type::monster_shard_of_strength,jungle=item_type::jungle_shard_of_health;
        const unsigned monster_price=monster.cost(),jungle_price=jungle.cost();
        if(gold<std::min(monster_price,jungle_price))return;
        const unsigned monster_required=monster.combine_count(),jungle_required=jungle.combine_count();
        if(monster_shards_>=monster_required&&jungle_shards_>=jungle_required) {
            const auto power=item_type::power_shard_of_strength;const unsigned count=std::min(100u,gold/power.cost());
            if(count&&me.can_buy(power,count)&&int(cambc::round_number())==round)me.buy(power,count);
            return;
        }
        if(cambc::fuel_remaining()<90000||(!me.can_buy(monster,1)&&!me.can_buy(jungle,1)))return;
        using Input=typename Policy::CapitalInput;using Job=typename Policy::CapitalJob;
        using Project=typename Policy::CapitalProject;
        const auto stats=me.base_stats();Input input;
        input.strength=me.strength()*stats.camp_damage_multiplier;input.maximum=me.max_health();
        input.strength_growth=stats.strength_growth*stats.camp_damage_multiplier;input.health_growth=stats.health_growth;
        input.gold=int(std::min(gold,unsigned(std::numeric_limits<int>::max())));
        input.monster_count=int(monster_shards_);input.jungle_count=int(jungle_shards_);
        input.monster_price=int(monster_price);input.jungle_price=int(jungle_price);
        input.monster_required=int(monster_required);input.jungle_required=int(jungle_required);
        input.monster_bonus=item_type::monster_emblem_of_strength.passive_magnitude();
        input.jungle_reduction=item_type::jungle_emblem_of_health.passive_magnitude();
        const bool alive=me.is_alive();const auto spawn=cambc::map::spawn_region(team::ally);
        const auto origin=alive?me.position():spawn.positions().front();
        int nearest=-1,heaviest=-1,richest=-1,best_distance=10000,best_gems=-1;float best_incoming=-1;
        for(std::size_t i=0;i<groups_.size();++i) {
            const auto&e=groups_[i];if(e.monolith||public_models()[i].size()>8)continue;
            const int d=distance(origin,e.anchor);if(d<best_distance){best_distance=d;nearest=int(i);}
            if(e.incoming>best_incoming){best_incoming=e.incoming;heaviest=int(i);}
            if(int(e.group.gem_value())>best_gems){best_gems=int(e.group.gem_value());richest=int(i);}
        }
        std::vector<Job> jobs;jobs.reserve(3);std::vector<int> included;
        for(const int id:{nearest,heaviest,richest})if(id>=0&&std::find(included.begin(),included.end(),id)==included.end()) {
            included.push_back(id);Job job;job.count=int(public_models()[id].size());
            std::copy(public_models()[id].begin(),public_models()[id].end(),job.monsters.begin());jobs.push_back(job);
        }
        const auto planned=p11::plan_in_round(round,[]{return cambc::round_number();},
            [&]{return Policy::choose_capital(input,jobs);});
        if(!planned||cambc::fuel_remaining()<10000)return;
        const auto purchase=*planned;
        if(purchase.project==Project::none||purchase.count<1||purchase.count>5)return;
        const auto item=purchase.project==Project::monster?monster:jungle;
        if(!me.can_buy(item,unsigned(purchase.count))||int(cambc::round_number())!=round)return;
        float before=0;
        if constexpr(!requires{Policy::reserve_action_budget;})before=alive?me.health():0;
        if(int(cambc::round_number())!=round)return;
        me.buy(item,unsigned(purchase.count));
        if(purchase.project==Project::monster)monster_shards_+=unsigned(purchase.count);
        else jungle_shards_+=unsigned(purchase.count);
        if constexpr(!requires{Policy::reserve_action_budget;})
            if(cambc::fuel_remaining()>15000&&int(cambc::round_number())==round)
                std::printf("p11capital actor=%u round=%u project=%d count=%d field=%d before=%g after=%g maximum=%g\n",unsigned(static_cast<cambc::handle_champion_t>(me)),cambc::round_number(),int(purchase.project),purchase.count,int(alive&&!spawn.is_set(origin)),double(before),double(alive?me.health():0),double(me.max_health()));
    }

    void shop() {
        if constexpr(requires{Policy::capital_shop;}){capital_shop();return;}
        auto& me = cambc::self();
        // A bulk buy completes one stage at a time; Tavi may do this in the field.
        for (int stage = 0; stage < 3; ++stage) {
            const auto build = (monster_shards_<5?0:(jungle_shards_<5?1:2));
            auto item = item_type::power_shard_of_strength;
            unsigned limit = 100;
            if (build == 0) {
                item = item_type::monster_shard_of_strength;
                limit = 5 - monster_shards_;
            } else if (build == 1) {
                item = item_type::jungle_shard_of_health;
                limit = 5 - jungle_shards_;
            }
            const unsigned count = std::min(limit, me.gold_count() / item.cost());
            if (!count || !me.can_buy(item, count)) return;
            me.buy(item, count);
            if (build == 0) monster_shards_ += count;
            if (build == 1) jungle_shards_ += count;
        }
    }

    bitboard blocked() const {
        auto result = terrain_ | cambc::vision::champion_tiles() | cambc::vision::camp_tiles()
            | cambc::vision::teleport_tiles();
        result.set(cambc::self().position(), false);
        return result;
    }

#include "move_to.inc"

    // A single visible target is attacked; refresh live validity after every
    // hit because kills and Varg splash can invalidate an earlier camp view.
    bool tactical_fight(const camp&target,bool walked,bool dangerous) {
        using Input=typename Policy::MicroInput;
        using Action=typename Policy::MicroAction;
        using Kind=typename Policy::MicroSpellKind;
        auto&me=cambc::self();const int round=cambc::round_number();
        const auto abilities=me.abilities();bool acted=false;
        if(cambc::fuel_remaining()>25000&&target.is_visible()&&target.is_alive()) {
            Input input;input.hp=target.health();
            input.basic_damage=me.strength()*(monster_shards_>=5?1.1f:1.f);
            input.basic_ready=me.can_attack();input.basic_in_range=me.attack_tiles().is_set(target.position());
            input.cast_ready=me.can_cast();input.ranged=me.attack_range()>1;
            input.moved=walked;input.movement_available=me.can_move();
            for(std::size_t i=0;i<abilities.size();++i) {
                const auto&a=abilities[i];if(!a.info()||!a.is_ready())continue;
                auto&slot=input.slots[i];
                std::visit([&](const auto&props){slot.exclusive_with_movement=props.exclusive_with_movement;},*a.info());
                if(a.is_attack_reset()&&!a.template get<cambc::ability_attack_reset>().enhanced_attack) {
                    slot.kind=Kind::reset;slot.ready=true;slot.in_range=true;
                } else if((a.is_damage()||a.is_cc())&&a.can_target_camps()) {
                    slot.kind=Kind::damage;slot.ready=true;slot.in_range=a.valid_tiles().is_set(target.position());
                    slot.damage=a.damage_amount()*(monster_shards_>=5?1.1f:1.f);
                }
            }
            if(int(cambc::round_number())!=round)return false;
            const auto plan=Policy::choose_micro(input);
            const int completed=Policy::execute_micro(plan,round,[]{return int(cambc::round_number());},
                []{return cambc::fuel_remaining();},[&](int,Action action,Action next) {
                if(int(cambc::round_number())!=round||!target.is_visible()||!target.is_alive())return 0;
                const float before=target.health();bool following_basic=false,success=false;
                int consumed=1;
                if(action==Action::basic) {
                    if(!me.can_attack()||!me.attack_tiles().is_set(target.position())
                        ||int(cambc::round_number())!=round)return 0;
                    me.attack(target);success=true;
                } else {
                    if(action!=Action::cast0&&action!=Action::cast1)return 0;
                    const int slot=action==Action::cast0?0:1;
                    if(!me.can_cast()||!abilities[slot].is_ready())return 0;
                    if(input.slots[slot].kind==Kind::reset) {
                        if(int(cambc::round_number())!=round)return 0;
                        success=abilities[slot].use();
                        // Complete reset/basic without rebuilding the search.
                        if(success&&next==Action::basic&&int(cambc::round_number())==round&&cambc::fuel_remaining()>10000
                            &&target.is_visible()&&target.is_alive()&&me.can_attack()
                            &&me.attack_tiles().is_set(target.position())&&int(cambc::round_number())==round) {
                            me.attack(target);following_basic=true;consumed=2;
                        }
                    } else {
                        if(!abilities[slot].valid_tiles().is_set(target.position())
                            ||int(cambc::round_number())!=round)return 0;
                        success=abilities[slot].use(target);
                    }
                }
                if(!success)return 0;
                if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;}) {
                    if(int(cambc::round_number())==round&&round%64==int(identity_%64)&&cambc::fuel_remaining()>18000) {
                        const float after=target.is_alive()?target.health():0.f;
                        std::printf("p11micro actor=%u round=%d action=%d following_basic=%d before=%g after=%g planned_damage=%g\n",
                            unsigned(static_cast<cambc::handle_champion_t>(me)),round,int(action),int(following_basic),double(before),double(after),double(plan.damage));
                    }
                }
                return consumed;
            });
            acted=completed>0;
        }
        // Shield only after the offensive exchange still leaves a live threat.
        // No speculative shield absorption was credited by the search.
        if(int(cambc::round_number())==round&&dangerous&&cambc::fuel_remaining()>18000&&target.is_alive()&&me.can_cast())
            for(const auto&a:abilities)if(a.is_shield()&&a.is_ready()){acted|=a.use();break;}
        return acted;
    }

    template<class Target>
    bool fight(const Target& target, bool walked, bool dangerous) {
        if constexpr(requires{Policy::tactical_search;}) {
            if constexpr(std::is_same_v<Target,camp>)return tactical_fight(target,walked,dangerous);
        }
        auto& me = cambc::self();
        if (cambc::fuel_remaining() < 16000) return false;
        auto valid = [&] { return target.is_visible() && target.health() > 0; };
        if (!valid()) return false;
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

    bool event_recover(const std::vector<camp>&camps,const std::vector<champion>&enemies) {
        using Threat=typename Policy::EscapeThreat;
        using Tile=typename Policy::EscapeTile;
        auto&me=cambc::self();const auto origin=me.position();const int round=cambc::round_number();
        auto walls=blocked();const auto legal=walls.reachable_tiles(origin,1).positions();
        const auto spawn=cambc::map::spawn_region(team::ally).positions();
        std::vector<Threat> threats;threats.reserve(std::min(std::size_t(32),camps.size()+enemies.size()));
        bitboard danger;bool safe=true,complete=true;
        for(const auto&m:camps) {
            if(threats.size()>=32||cambc::fuel_remaining()<45000){complete=false;break;}
            if(!m.is_alive()||!m.is_awake())continue;
            const float strength=m.strength();if(strength<=0)continue;
            const auto p=m.position();const int range=m.attack_range();
            threats.push_back({p.x,p.y,range,strength*(jungle_shards_>=5?.7f:1.f)});
            danger|=around(p,range+1);if(distance(origin,p)<=range+1)safe=false;
        }
        for(const auto&e:enemies) {
            if(threats.size()>=32||cambc::fuel_remaining()<45000){complete=false;break;}
            if(!e.is_alive())continue;
            const auto p=e.position();const int range=e.attack_range();
            threats.push_back({p.x,p.y,range+1,e.strength()});
            danger|=around(p,8);if(distance(origin,p)<=8)safe=false;
        }
        if(int(cambc::round_number())!=round)return true;
        if(complete&&safe){me.recall();return true;}
        if(!me.can_move())return true;
        cambc::distance_field field;bitboard visited;
        const auto goals=~danger&~walls;const bool have_field=!goals.is_empty();
        if(have_field)field.compute_to(walls,goals,around(origin,0),visited,64);
        std::vector<Tile> tiles;tiles.reserve(9);
        for(const auto p:legal) {
            int home=10000;for(const auto q:spawn)home=std::min(home,distance(p,q));
            tiles.push_back({p.x,p.y,have_field?int(field.get(p)):10000,home});
        }
        const float hp=me.health();
        for(int retry=0;retry<3&&!tiles.empty();++retry) {
            if(cambc::fuel_remaining()<30000||int(cambc::round_number())!=round)return true;
            const int chosen=Policy::choose_escape(hp,tiles,threats);
            if(chosen<0)return true;
            const vec2 destination{tiles[chosen].x,tiles[chosen].y};
            if(destination==origin)return true;
            const bool occupied=blocked().is_set(destination);
            if(!occupied&&me.can_move()&&me.move_absolute(destination)) {
                if(cambc::fuel_remaining()>8000)
                    std::printf("p11escape actor=%u round=%d modeled=%u complete=%d x=%d y=%d\n",unsigned(static_cast<cambc::handle_champion_t>(me)),round,unsigned(threats.size()),int(complete),destination.x,destination.y);
                return true;
            }
            tiles.erase(tiles.begin()+chosen);
        }
        return true;
    }

    bool recover(const std::vector<camp>& camps, const std::vector<champion>& enemies
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
                 ,const p11_tempo::conversion::Input* conversion=nullptr,
                 std::span<const p11_tempo::conversion::Threat> conversion_threats={}
#endif
                 ) {
        if constexpr(requires{Policy::event_escape;})return event_recover(camps,enemies);
        [[maybe_unused]] int observed_round=0;
        if constexpr(requires{typename Policy::RecoveryBankInput;})observed_round=cambc::round_number();
        auto& me = cambc::self();
        const auto origin = me.position();
        bool safe = true;
        bitboard danger;
        for (const auto& monster : camps)
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
        {
            if(conversion&&cambc::fuel_remaining()<25000)return true;
#endif
            if (monster.is_alive() && monster.is_awake() && monster.strength() > 0) {
                danger |= around(monster.position(), monster.attack_range());
                if (distance(origin, monster.position()) <= monster.attack_range() + 1) safe = false;
            }
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
        }
#endif
        for (const auto& enemy : enemies) {
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
            if(conversion&&cambc::fuel_remaining()<25000)return true;
#endif
            danger |= around(enemy.position(), enemy.attack_range() + 1);
            if (distance(origin, enemy.position()) <= 8) safe = false;
        }
        if (safe) {
            if constexpr(requires{typename Policy::RecoveryBankInput;}) {
                // A recovery commitment must not hide an already reachable
                // winning event. Do not walk or clear a lock for this option.
                if(cambc::fuel_remaining()>35000&&me.gem_count()>0) {
                    typename Policy::RecoveryBankInput input;
                    input.bag=int(me.gem_count());
                    const auto goal=cambc::constants::winning_gems;
                    input.needed=int(goal-std::min(goal,cambc::global_submitted_gems(team::ally)));
                    input.tribute_ticks=p11::tribute_ticks(input.bag,cambc::constants::submit_gem_duration_multiplier,
                        cambc::constants::submit_gem_base_duration,cambc::constants::submit_gem_minimum_duration);
                    input.recall_ticks=cambc::constants::recall_time;
                    input.remaining_rounds=int(cambc::constants::max_rounds)-observed_round;
                    input.safe=true;input.full_health=me.health()>=me.max_health();
                    input.channel_free=!p11::kernel::preserve_channel(channel_snapshot());
                    const auto locks=cambc::vision::altar_locks(team::enemy);
                    for(const auto altar:altars_) {
                        input.in_range=distance(origin,altar)<=cambc::constants::submit_gems_range;
                        input.unlocked=std::none_of(locks.begin(),locks.end(),[&](const auto&lock){return lock.position()==altar;});
                        input.fresh=int(cambc::round_number())==observed_round;
                        if(!Policy::can_bank_before_recall(input))continue;
                        if(int(cambc::round_number())!=observed_round)return true;
                        me.submit_gems(altar);trace_channel(channel_snapshot(),true);
                        if(cambc::fuel_remaining()>15000)
                            std::printf("p11recoverbank actor=%u round=%d bag=%d needed=%d ticks=%d\n",
                                unsigned(static_cast<cambc::handle_champion_t>(me)),observed_round,input.bag,input.needed,input.tribute_ticks);
                        return true;
                    }
                }
                if(int(cambc::round_number())!=observed_round)return true;
            }
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
            if(conversion&&(cambc::fuel_remaining()<25000||int(cambc::round_number())!=conversion->observed_round))return true;
#endif
            me.recall();return true;
        }
        // First get out of attack range. Maximise clearance locally if danger
        // surrounds us; a path whose starting tile is threatened may be blocked.
        auto legal = blocked().reachable_tiles(origin, 1);
        auto best = origin;
        float best_score = -1e30f;
        for (const auto p : legal.positions()) {
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
            if(conversion&&cambc::fuel_remaining()<25000)return true;
            if(conversion&&p11_tempo::conversion::unpaid_lethal_step(*conversion,{p.x,p.y},conversion_threats))continue;
#endif
            float score = 0;
            for (const auto& monster : camps)
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
            {
                if(conversion&&cambc::fuel_remaining()<25000)return true;
#endif
                if (monster.is_alive() && monster.is_awake() && monster.strength() > 0)
                    score += std::min(distance(p, monster.position()), 6) * monster.strength();
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
            }
#endif
            for (const auto& enemy : enemies) score += std::min(distance(p, enemy.position()), 9) * 150;
            if (score > best_score) { best = p; best_score = score; }
        }
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
        if(conversion) {
            const bool movable=best!=origin&&me.can_move();
            if(cambc::fuel_remaining()<25000||int(cambc::round_number())!=conversion->observed_round)return true;
            if(movable)me.move_absolute(best);
            return true; // never substitute an unfiltered failed-exit route
        }
#endif
        if (best != origin && me.can_move() && me.move_absolute(best)) return true;
        move_to(cambc::map::spawn_region(team::ally), &danger);
        return true;
    }

    p11::kernel::ChannelSnapshot channel_snapshot() const {
        const auto&me=cambc::self();
        p11::kernel::ChannelSnapshot channel;
        if(me.is_recalling())channel.kind=p11::kernel::ChannelKind::recall;
        else if(me.is_submitting_gems())channel.kind=p11::kernel::ChannelKind::tribute;
        else if(me.is_teleporting())channel.kind=p11::kernel::ChannelKind::teleport;
        if(p11::kernel::preserve_channel(channel)) {
            channel.remaining=me.remaining_channel_time();
            channel.bag=int(me.gem_count());
        }
        return channel;
    }

    void trace_channel(const p11::kernel::ChannelSnapshot&channel,bool started) const {
        if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;}) {
            if(cambc::fuel_remaining()>20000&&channel.remaining)
                std::printf("p11channel actor=%u round=%u kind=%d start=%d timer=%d bag=%d due=%u\n",
                    unsigned(static_cast<cambc::handle_champion_t>(cambc::self())),cambc::round_number(),
                    int(channel.kind),int(started),*channel.remaining,channel.bag,
                    cambc::round_number()+unsigned(std::max(0,*channel.remaining)));
        }
    }

#include "bank_caller.inc"

    p11::State state(int altar) const {
        const auto& me=cambc::self();p11::State s;
        s.round=cambc::round_number();s.limit=cambc::constants::max_rounds;
        s.bag=me.gem_count();s.altar=altar;s.hp=me.health();s.maximum=me.max_health();
        s.strength=me.strength();s.focus=me.focus();s.gold=me.gold_count();
        s.capture=cambc::constants::capture_time;s.recall=cambc::constants::recall_time;
        s.identity=identity_;s.monster_shards=monster_shards_;s.jungle_shards=jungle_shards_;
        s.mature=monster_shards_>=5 && jungle_shards_>=5;
        s.field_shop=me.champion_name()==cambc::champion_type::tavi;
        s.tribute_multiplier=cambc::constants::submit_gem_duration_multiplier;
        s.tribute_base=cambc::constants::submit_gem_base_duration;
        s.tribute_minimum=cambc::constants::submit_gem_minimum_duration;
        if constexpr(requires{Policy::terminal_goal;})
            s.needed_gems=int(cambc::constants::winning_gems-std::min(cambc::constants::winning_gems,cambc::global_submitted_gems(team::ally)));
        return s;
    }

    // A farm snapshot is taken after shopping/banking paths and before any
    // farm action. Do not carry these live fields across an owner-round or a
    // mutation; the executor still obtains fresh action masks and target HP.
    template<class Observation>
    p11::State observed_farm_state(int altar,const Observation& observation) const {
        const auto& me=cambc::self();p11::State s;
        s.round=cambc::round_number();s.limit=cambc::constants::max_rounds;
        s.bag=me.gem_count();s.altar=altar;s.hp=observation.hp;s.maximum=observation.maximum;
        s.strength=observation.strength;s.focus=observation.focus;s.gold=me.gold_count();
        s.capture=cambc::constants::capture_time;s.recall=cambc::constants::recall_time;
        s.identity=identity_;s.monster_shards=monster_shards_;s.jungle_shards=jungle_shards_;
        s.mature=monster_shards_>=5 && jungle_shards_>=5;
        s.field_shop=observation.type==int(cambc::champion_type::tavi);
        s.tribute_multiplier=cambc::constants::submit_gem_duration_multiplier;
        s.tribute_base=cambc::constants::submit_gem_base_duration;
        s.tribute_minimum=cambc::constants::submit_gem_minimum_duration;
        return s;
    }

    void finish_clear_turn(Group&entry,int observed_round) {
        if(cambc::fuel_remaining()<35000||int(cambc::round_number())!=observed_round)return;
        auto&me=cambc::self();if(!me.can_move())return;
        const bool ready=std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state());
        const bool on_spawn=entry.spawns.is_set(me.position());
        if(p11::finish_clear({ready,on_spawn,true,true})!=p11::ClearContinuation::enter_spawn)return;
        const bool moved=move_to(entry.spawns);
        if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;}) {
            if(cambc::fuel_remaining()>12000)
                std::printf("p11clear actor=%u round=%d group=%u free_move=%d on_spawn=%d\n",unsigned(static_cast<cambc::handle_champion_t>(me)),observed_round,unsigned(static_cast<cambc::handle_camp_group_t>(entry.group)),int(moved),int(entry.spawns.is_set(me.position())));
        }
    }

#if defined(P11_TRIAGE_POSTCAST_BASIC) && P11_TRIAGE_POSTCAST_BASIC
#include "postcast_basic.inc"
#endif

#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
    // This request exists only around an already selected live source move.
    // The normal movement planner supplies its actual proposed first tile.
    [[clang::noinline]] bool tempo_admit_step(vec2 next) {
        using namespace p11_tempo::conversion;
        auto&r=*tempo_move_request_;const auto&s=r.state;auto&me=cambc::self();
        const auto abort=[&]{r.aborted=true;return false;};
        if(int(cambc::round_number())!=r.round)return abort();
        if(r.enemies.size()>16||cambc::fuel_remaining()<50000)return true;
        r.count=0;bool potential=false;
        for(const auto&e:r.enemies) {
            if(cambc::fuel_remaining()<25000)return abort();
            // Ability metadata is already owned by the visible champion view.
            // Do not add a live observation for a kit without a damaging dash.
            bool observed_ready=false;cambc::detail::champion_info observed{};
            for(const auto&a:e.abilities()) {
                if(!a.is_dash())continue;
                const auto props=a.template get<cambc::ability_dash>();
                if(props.modifier<=0||props.radius<=0||props.range<1)continue;
                if(!observed_ready) {
                    const auto code=cambc::detail::sdk_champion(std::uint32_t(static_cast<cambc::handle_champion_t>(e)),&observed);
                    if(int(cambc::round_number())!=r.round)return abort();
                    cambc::detail::check(code);observed_ready=true;
                }
                if(observed.health<=0||a.cooldown()!=0)continue;
                if(distance(next,{observed.x,observed.y})>int(std::floor(props.range+props.radius)))continue;
                const int walking=props.exclusive_with_movement?0:std::max(0,observed.move_speed);
                const int radius=int(std::ceil(props.range+props.radius))+walking;
                const float damage=std::min(a.damage_amount(),observed.focus*props.modifier);
                if(damage<s.hp)continue;
                r.threats[r.count++]={{observed.x,observed.y},radius,damage,true};
                // Admission uses a presently executable dash, not the broad
                // possible walk+dash envelope used to protect the first exit.
                if(distance(next,{observed.x,observed.y})<=int(std::floor(props.range+props.radius)))potential=true;
            }
        }
        if(int(cambc::round_number())!=r.round)return abort();
        if(!potential)return true;
        float shield=0;const auto buffs=me.buffs();
        if(buffs.has_shield())shield=buffs.template get<cambc::buff_shield>().value;
        float ready_shield=0;
        for(const auto&a:me.abilities())if(a.is_shield()&&a.is_ready())ready_shield=std::max(ready_shield,a.shield_amount());
        r.input={s.bag,s.hp,shield+ready_shield,s.capture+1,r.round};
        const auto exposed=std::span<const Threat>(r.threats.data(),r.count);
        if(int(cambc::round_number())!=r.round)return abort();
        if(!unpaid_lethal_step(r.input,{next.x,next.y},exposed))return true;
        // Certify a complete, personally visible, empty landing path. Future
        // walking is not assumed to license refusal of the nominal work step.
        const auto origin=me.position();const auto visible=cambc::vision::visible_tiles();
        const auto ally_spawn=cambc::map::spawn_region(team::ally);
        if(ally_spawn.is_set(next))return true; // spawn healing is a different payment word
        const auto occupied=blocked();bool licensed=false;
        for(const auto&e:r.enemies)for(const auto&a:e.abilities()) {
            if(cambc::fuel_remaining()<25000)return abort();
            if(!a.is_dash()||a.cooldown()!=0)continue;
            const auto props=a.template get<cambc::ability_dash>();
            if(props.modifier<=0||props.radius<=0||props.range<1||props.range>4
                ||!props.valid_targets.contains(cambc::ability_target_mask::ground))continue;
            cambc::detail::champion_info observed{};
            const auto code=cambc::detail::sdk_champion(std::uint32_t(static_cast<cambc::handle_champion_t>(e)),&observed);
            if(int(cambc::round_number())!=r.round)return abort();
            cambc::detail::check(code);
            if(observed.health<=0)continue;
            const auto flags=cambc::champion_state::from_bits_retain(std::uint32_t(observed.state));
            if(flags.contains(cambc::champion_state::teleporting)||flags.contains(cambc::champion_state::recalling)
                ||flags.contains(cambc::champion_state::submitting_gems))continue;
            const auto buffs=e.buffs();
            if(buffs.has_stun()&&buffs.template get<cambc::buff_stun>().duration>0)continue;
            const float damage=std::min(a.damage_amount(),observed.focus*props.modifier);
            if(!exhausts_defence(r.input,damage))continue;
            const vec2 from{observed.x,observed.y};
            if(distance(next,from)>int(std::floor(props.range+props.radius)))continue;
            // Our movement terrain alone does not forbid our spawn to an
            // enemy. Extra blockers on its own spawn are conservative here.
            auto walls=occupied|~visible;
            walls.set(from,false);walls.set(next,true);
            if(next!=origin)walls.set(origin,false);
            walls|=ally_spawn;
            auto landings=walls.reachable_tiles(from,static_cast<std::uint16_t>(std::floor(props.range)))
                &around(next,int(std::floor(props.radius)))&~walls;
            landings.set(from,false);
            if(!landings.is_empty()){licensed=true;break;}
        }
        if(int(cambc::round_number())!=r.round)return abort();
        if(!licensed)return true;
        // The exit must avoid every visible lethal dash envelope, including
        // an enemy which could not hit the originally proposed work tile.
        r.count=0;
        for(const auto&e:r.enemies) {
            if(cambc::fuel_remaining()<25000)return abort();
            bool observed_ready=false;cambc::detail::champion_info observed{};
            for(const auto&a:e.abilities()) {
                if(!a.is_dash())continue;
                const auto props=a.template get<cambc::ability_dash>();
                if(props.modifier<=0||props.radius<=0||props.range<1||a.cooldown()!=0)continue;
                if(!observed_ready) {
                    const auto code=cambc::detail::sdk_champion(std::uint32_t(static_cast<cambc::handle_champion_t>(e)),&observed);
                    if(int(cambc::round_number())!=r.round)return abort();
                    cambc::detail::check(code);observed_ready=true;
                }
                if(observed.health<=0)continue;
                const int walking=props.exclusive_with_movement?0:std::max(0,observed.move_speed);
                const int radius=int(std::ceil(props.range+props.radius))+walking;
                if(distance(origin,{observed.x,observed.y})>radius+1)continue;
                const float damage=std::min(a.damage_amount(),observed.focus*props.modifier);
                if(exhausts_defence(r.input,damage))
                    r.threats[r.count++]={{observed.x,observed.y},radius,damage,true};
            }
        }
        const auto exits=std::span<const Threat>(r.threats.data(),r.count);
        bool exit=false;
        for(const auto p:occupied.reachable_tiles(origin,1).positions())
            if(p!=origin&&!unpaid_lethal_step(r.input,{p.x,p.y},exits)){exit=true;break;}
        if(int(cambc::round_number())!=r.round)return abort();
        if(!exit)return true;
        r.refused=true;return false;
    }

    [[clang::noinline]] TempoMoveResult tempo_move_live_source(bitboard goals,const p11::State&s,
            const std::vector<camp>&camps,const std::vector<champion>&enemies,int round) {
        TempoMoveRequest request{s,enemies,round};bool moved=false;
        {
            struct Scope {Bot&bot;~Scope(){bot.tempo_move_request_=nullptr;}}scope{*this};
            tempo_move_request_=&request;moved=move_to(goals);
        }
        if(request.aborted||int(cambc::round_number())!=round)return {false,true};
        if(!request.refused)return {moved,false};
        if(cambc::fuel_remaining()<25000)return {false,true};
        recovering_=true;
        recover(camps,enemies,&request.input,std::span<const p11_tempo::conversion::Threat>(request.threats.data(),request.count));
        return {false,true};
    }
#endif

    void farm(const std::vector<camp>& camps, const std::vector<champion>& allies
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
              ,const std::vector<champion>& enemies
#endif
              ) {
#if P11_CONTEST_CONTINUATION
        // Re-entering source selection ends any old physical contest detour.
        contest_continuation_.clear();
#endif
        int entered=0,post_state=0,post_snap=0,post_preview=0,post_sort=0;
        if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;})entered=cambc::fuel_remaining();
        auto& me=cambc::self();const auto visible=cambc::vision::visible_tiles();
#if defined(P11_COHERENT_FARM)
        const int round=cambc::round_number();
        const auto observation=Policy::observe_self([&] {
            cambc::detail::champion_info record{};
            cambc::detail::check(cambc::detail::sdk_champion(
                std::uint32_t(static_cast<cambc::handle_champion_t>(me)),&record));
            return record;
        });
        if(int(cambc::round_number())!=round)return;
        const vec2 origin{observation.x,observation.y};
        const auto s=observed_farm_state(0,observation);
        if(int(cambc::round_number())!=round)return;
#else
        const int round=cambc::round_number();const auto origin=me.position();
        const auto s=state(0);
#endif
#if P11_EXPEDITION_RECEIPT
        const bool expedition_target=expedition_receipt_.eligible(identity_,round);
        int expedition_preview_index=-1;
#endif
        std::vector<p11::Candidate> candidates;
        if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;})post_state=cambc::fuel_remaining();
        candidates.reserve(groups_.size());
        std::vector<int> order;int screened=0,cached_range=0;
        [[maybe_unused]] bool packing_ready=false;
        std::vector<vec2> ally_positions;
        std::vector<std::vector<p11::Monster>> observed_models;
#if defined(P11_EARLY_FALLBACK_DETAIL) && P11_EARLY_FALLBACK_DETAIL
        std::vector<p11::Candidate> early_preview;
        auto& preview=early_preview;
        const auto complete_frontier=[&](int fallback) {
            const int explore=groups_.empty()?-1:int((unsigned(round/8)+identity_)%groups_.size());
            const int owned=target_>=0?target_:fallback;
            const int alternative=fallback>=0&&fallback!=owned?fallback:explore;
            if constexpr(requires{Policy::detail_frontier(s,preview,Policy::window_size,target_,fallback,explore);})
                order=Policy::detail_frontier(s,preview,Policy::window_size,target_,fallback,explore);
            else if constexpr(requires{typename Policy::InformationClock;})
                order=information_clock().template frontier<Policy>(s,preview,Policy::window_size,target_,fallback);
            else {
                order=p11_overlap::unique_three_frontier<Policy>(s,preview,Policy::window_size,owned,alternative);
#if P11_EXPEDITION_RECEIPT
                if(expedition_target)expedition_receipt_.ordinary_shortlist=true;
#endif
            }
            // Evaluate a conservative completed fallback before speculative
            // improvements; a voluntary reserve cutoff must not suppress it.
            auto promote=[&](int id){auto found=std::find(order.begin(),order.end(),id);if(found!=order.end())std::rotate(order.begin(),found,found+1);};
            promote(target_);promote(fallback);
            if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;})post_sort=cambc::fuel_remaining();
        };
        bool early_pending=false;
        int early_fallback=-1;
#endif
        auto target_observations=[&] {
            if constexpr(requires{Policy::target_horizon;} || requires{Policy::reuse_farm_observations;})
                return std::vector<std::vector<TargetObservation>>(groups_.size());
            else return std::monostate{};
        }();
        if constexpr(requires{Policy::bounded_scan;}) {
#if defined(P11_COHERENT_FARM)
            cached_range=observation.range;
#else
            cached_range=me.attack_range();
#endif
            if constexpr(requires{Policy::packed_clock;})packing_ready=me.can_move()&&me.can_attack();
            ally_positions.reserve(allies.size());
            for(const auto&ally:allies)if(ally!=me)ally_positions.push_back(ally.position());
            if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;})post_snap=cambc::fuel_remaining();
#if !defined(P11_EARLY_FALLBACK_DETAIL) || !P11_EARLY_FALLBACK_DETAIL
            std::vector<p11::Candidate> preview;
#endif
            preview.reserve(groups_.size());
            int fallback=-1,best_fallback=100000;
            for(std::size_t i=0;i<groups_.size();++i) {
                const auto&e=groups_[i];++screened;
                if(e.monolith||e.unavailable_until>unsigned(round))continue;
                if constexpr(requires{Policy::committed_assignment;})if(committed_elsewhere(int(i)))continue;
                p11::Candidate c;c.id=int(i);c.travel=std::max(0,distance(origin,e.anchor)-cached_range);
#if P11_CACHE_PUBLIC_WORK
                c.clear=e.public_work.get(e.health,s.strength*(monster_shards_>=5?1.1f:1.f),
                    [](float health,float damage){return p11::attacks(health,damage);});
                c.capture=s.capture+1;
#else
                c.clear=p11::attacks(e.health,s.strength*(monster_shards_>=5?1.1f:1.f));c.capture=s.capture+1;
#endif
                c.hp=e.health;c.incoming=e.incoming;c.gems=e.group.gem_value();c.gold=e.group.gold_value();c.committed=target_==int(i);
                c.loss=e.incoming*(c.clear+2)*(jungle_shards_>=5?.7f:1.f);
#if P11_CACHE_ALTAR_MINIMUM
                c.altar=e.altar_minimum.get([&] {
                    int minimum=10000;for(const auto altar:altars_)minimum=std::min(minimum,std::max(0,distance(e.anchor,altar)-cambc::constants::submit_gems_range));
                    return minimum;
                });
#else
                c.altar=10000;for(const auto altar:altars_)c.altar=std::min(c.altar,std::max(0,distance(e.anchor,altar)-cambc::constants::submit_gems_range));
#endif
                if constexpr(requires{Policy::geodesic;}) {
                    c.travel=std::min(10000,int(routes().entry[i][origin.y*cambc::map::width()+origin.x]));c.altar=routes().altar[i];
                }
                if constexpr(requires{Policy::packed_clock;})
                    c=Policy::pack(s,c,cached_range,distance(origin,e.anchor),int(public_models()[i].size()),packing_ready);
                if(p11::feasible(s,c)&&c.travel+c.clear<best_fallback){best_fallback=c.travel+c.clear;fallback=c.id;
#if P11_EXPEDITION_RECEIPT
                    if(expedition_target)expedition_preview_index=int(preview.size());
#endif
                }
                preview.push_back(c);
            }
            if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;})post_preview=cambc::fuel_remaining();
#if defined(P11_EARLY_FALLBACK_DETAIL) && P11_EARLY_FALLBACK_DETAIL
            // Preserve the original first detailed job, but prepare it before
            // full ranking can consume its unchanged 50k entry reserve. The
            // complete original frontier still precedes final choice/actions.
            early_fallback=fallback;
            const auto first=std::find_if(preview.begin(),preview.end(),
                [fallback](const auto& candidate){return candidate.id==fallback;});
            early_pending=first!=preview.end()&&std::isfinite(Policy::screen(s,*first));
            if(early_pending)order.push_back(fallback);
            else complete_frontier(early_fallback);
#else
            const int explore=groups_.empty()?-1:int((unsigned(round/8)+identity_)%groups_.size());
            const int owned=target_>=0?target_:fallback;
            const int alternative=fallback>=0&&fallback!=owned?fallback:explore;
            if constexpr(requires{Policy::detail_frontier(s,preview,Policy::window_size,target_,fallback,explore);})
                order=Policy::detail_frontier(s,preview,Policy::window_size,target_,fallback,explore);
            else if constexpr(requires{typename Policy::InformationClock;})
                order=information_clock().template frontier<Policy>(s,preview,Policy::window_size,target_,fallback);
            else {
                order=p11::shortlist<Policy>(s,preview,Policy::window_size,owned,alternative);
#if P11_EXPEDITION_RECEIPT
                if(expedition_target)expedition_receipt_.ordinary_shortlist=true;
#endif
            }
            // Evaluate a conservative completed fallback before speculative
            // improvements; a voluntary reserve cutoff must not suppress it.
            auto promote=[&](int id){auto found=std::find(order.begin(),order.end(),id);if(found!=order.end())std::rotate(order.begin(),found,found+1);};
            promote(target_);promote(fallback);
            if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;})post_sort=cambc::fuel_remaining();
#endif
#if P11_EXPEDITION_RECEIPT
            if(expedition_target&&expedition_preview_index>=0)
                expedition_receipt_.retain(preview[unsigned(expedition_preview_index)]);
#endif
        }
        if(int(cambc::round_number())!=round)return;
        if constexpr(requires{Policy::bounded_scan;}) {
            observed_models.resize(groups_.size());
            for(const auto&m:camps) {
                const auto p=m.spawn_position();const int group=group_at_tile_[p.y*cambc::map::width()+p.x];
#if defined(P11_EARLY_FALLBACK_DETAIL) && P11_EARLY_FALLBACK_DETAIL
                const bool in_frontier=early_pending||std::find(order.begin(),order.end(),group)!=order.end();
                if(group>=0&&in_frontier&&m.is_alive()) {
#else
                if(group>=0&&std::find(order.begin(),order.end(),group)!=order.end()&&m.is_alive()) {
#endif
                    if constexpr(requires{Policy::target_horizon;} || requires{Policy::reuse_farm_observations;}) {
                        const float hp=m.health(),incoming=m.strength();
                        observed_models[group].push_back({hp,incoming,0});
                        target_observations[group].push_back({m,hp,incoming});
                    } else observed_models[group].push_back({m.health(),m.strength(),0});
                }
            }
        }
        std::size_t planned=groups_.size();
        if constexpr(requires{Policy::bounded_scan;}) planned=order.size();
#if defined(P11_EARLY_FALLBACK_DETAIL) && P11_EARLY_FALLBACK_DETAIL
        if(early_pending)planned=2; // one real job followed by frontier completion
#endif
        unsigned scanned=0;int scan_start=0;
        if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;}) scan_start=cambc::fuel_remaining();
        for(std::size_t slot=0;slot<planned;++slot) {
#if defined(P11_EARLY_FALLBACK_DETAIL) && P11_EARLY_FALLBACK_DETAIL
            if(slot==1&&early_pending) {
                if(int(cambc::round_number())!=round)return;
                complete_frontier(early_fallback);early_pending=false;planned=order.size();
                if(order.empty()||order.front()!=early_fallback)return;
                if(slot>=planned)break;
            }
#endif
#if P11_EXPEDITION_RECEIPT
            const unsigned expedition_gate_fuel=cambc::fuel_remaining();
            const bool expedition_gate_passed=expedition_gate_fuel>=50000;
            if(expedition_target&&slot==0) {
                auto&p=expedition_receipt_;
                p.actor=int(static_cast<cambc::handle_champion_t>(me));p.group=p.preview.id;
                p.range=cached_range;p.packing_ready=packing_ready;p.origin_x=origin.x;p.origin_y=origin.y;p.target=target_;
                if(p.retained) {
                    const auto&e=groups_[unsigned(p.group)];
                    p.native_group=int(static_cast<cambc::handle_camp_group_t>(e.group));
                    p.anchor_x=e.anchor.x;p.anchor_y=e.anchor.y;p.unavailable_until=e.unavailable_until;p.monolith=e.monolith;
                    p.capture(s,order,public_models()[unsigned(p.group)],ally_positions,expedition_gate_fuel,expedition_gate_passed);
                } else p.capture(s,order,std::span<const p11::Monster>{},ally_positions,expedition_gate_fuel,expedition_gate_passed);
            }
            if(!expedition_gate_passed)break;
#else
            if(cambc::fuel_remaining()<50000) break;
#endif
#if P11_EXPEDITION_RECEIPT
            const bool expedition_round_current=int(cambc::round_number())==round;
            if(expedition_target&&slot==0){expedition_receipt_.round_checked=true;expedition_receipt_.round_current=expedition_round_current;}
            if(!expedition_round_current)return;
#else
            if(int(cambc::round_number())!=round)return;
#endif
            ++scanned;
            std::size_t i=slot;
            if constexpr(requires{Policy::bounded_scan;}) i=std::size_t(order[slot]);
            auto& entry=groups_[i];
            if(entry.monolith || entry.unavailable_until>unsigned(round)) continue;
            const bool seen=!(entry.spawns&visible).is_empty();
            const bool known=(entry.spawns&~visible).is_empty();bool reward=false;
            if(seen) {
                const auto live=entry.group.state();
                if(int(cambc::round_number())!=round)return;
                if(const auto* respawn=std::get_if<cambc::map::camp_group_respawning>(&live)) {
                    entry.unavailable_until=round+std::max(1,respawn->timer);
                    if constexpr(requires{typename Policy::InformationClock;}) information_clock().observed(int(i));
                    continue;
                }
                reward=std::holds_alternative<cambc::map::camp_group_reward_available>(live);
                if(reward&&entry.spawns.is_set(origin)){
#if defined(P11_EARLY_FALLBACK_DETAIL) && P11_EARLY_FALLBACK_DETAIL
                    if(early_pending) {complete_frontier(early_fallback);early_pending=false;}
                    if(int(cambc::round_number())!=round)return;
#endif
                    if constexpr(requires{typename Policy::InformationClock;}) information_clock().observed(int(i));
                    target_=int(i);
                    if constexpr(requires{Policy::committed_assignment;})publish_commitment(int(i),round);
                    return;
                }
            }
            std::vector<p11::Monster> model;
            if(known) {
                if constexpr(requires{Policy::bounded_scan;})model=observed_models[i];
                else for(const auto&m:camps) if(entry.spawns.is_set(m.spawn_position())&&m.is_alive())
                        model.push_back({m.health(),m.strength(),0});
            } else {
                if constexpr(requires{Policy::bounded_scan;}) model=public_models()[i];
                else for(const auto&m:entry.group) {
                        const auto stats=m.base_stats();model.push_back({stats.base_health,stats.base_strength,0});
                    }
            }
            float strength=0;int range=0;
            if constexpr(requires{Policy::bounded_scan;}) {strength=s.strength;range=cached_range;}
            else {strength=me.strength();range=reward?0:me.attack_range();}
            const auto clear=p11::clear_schedule(model,strength*(monster_shards_>=5?1.1f:1.f),jungle_shards_>=5?.7f:1.f);
            if(int(cambc::round_number())!=round)return;
            p11::Candidate c;c.id=int(i);c.known=known;c.ready=reward;c.committed=target_==int(i);
            c.travel=std::max(0,distance(origin,entry.anchor)-(reward?0:range));
            c.clear=reward?0:clear.turns;c.capture=cambc::constants::capture_time+1;
            // Fully observed fights execute the model's weighted kill order.
            // Hidden members retain the pessimistic all-live reserve.
            const float mitigation=jungle_shards_>=5?.7f:1.f;
            c.loss=reward?0:(known?p11::ordered_exposure(model,clear,mitigation):p11::exposure_reserve(model,clear.turns,mitigation));
            c.gems=entry.group.gem_value();c.gold=entry.group.gold_value();
            c.hp=entry.health;c.incoming=entry.incoming;c.altar=10000;
            if constexpr(requires{Policy::renewal_model;}) c.renewal=entry.group.max_respawn_timer();
#if P11_CACHE_ALTAR_MINIMUM
            c.altar=entry.altar_minimum.get([&] {
                int minimum=10000;for(const auto altar:altars_)minimum=std::min(minimum,std::max(0,distance(entry.anchor,altar)-cambc::constants::submit_gems_range));
                return minimum;
            });
#else
            for(const auto altar:altars_) c.altar=std::min(c.altar,std::max(0,distance(entry.anchor,altar)-cambc::constants::submit_gems_range));
#endif
            if constexpr(requires{Policy::geodesic;}) {
                c.travel=std::min(10000,int((reward?routes().capture[i]:routes().entry[i])[origin.y*cambc::map::width()+origin.x]));
                c.altar=routes().altar[i];
            }
            if constexpr(requires{Policy::bounded_scan;}) {
                for(const auto p:ally_positions)if(distance(p,entry.anchor)<=3)++c.allies;
            } else for(const auto&ally:allies) if(ally!=me&&distance(ally.position(),entry.anchor)<=3) ++c.allies;
            if constexpr(requires{Policy::packed_clock;})
                c=Policy::pack(s,c,cached_range,distance(origin,entry.anchor),int(public_models()[i].size()),packing_ready);
            if constexpr(requires{Policy::committed_assignment;}) {
                // A real capture occupant reserves the job even if it could
                // not publish an intent. Extra capturers do not speed capture.
                if(c.ready&&std::any_of(ally_positions.begin(),ally_positions.end(),
                        [&](vec2 position){return entry.spawns.is_set(position);}))continue;
            }
            candidates.push_back(c);
            if constexpr(requires{typename Policy::InformationClock;}) {
                // Suspended work did not complete a current information visit.
                // A fuel cutoff before the pending probe retains that probe.
                if(int(cambc::round_number())!=round)return;
                information_clock().observed(int(i));
            }
        }
        if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;}) {
            if((round<=3||round%64==int(identity_%64))&&cambc::fuel_remaining()>40000) {
                const int next=scanned<order.size()?int(static_cast<cambc::handle_camp_group_t>(groups_[order[scanned]].group)):-1;
                std::printf("p11scan id=%u round=%d start=%d stop=%d scanned=%u total=%u modeled=%u next=%d screened=%d planned=%u fuel_cut=%d enter=%d state=%d snap=%d preview=%d sort=%d\n",identity_,round,scan_start,int(cambc::fuel_remaining()),scanned,unsigned(groups_.size()),unsigned(candidates.size()),next,screened,unsigned(order.size()),int(scanned<order.size()),entered,post_state,post_snap,post_preview,post_sort);
            }
        }
#if defined(P11_EARLY_FALLBACK_DETAIL) && P11_EARLY_FALLBACK_DETAIL
        if(early_pending)return; // no final choice from an unfinished frontier
#endif
        int selected=-1;
        if constexpr(requires{Policy::committed_assignment;}) {
            const int self_id=int(static_cast<cambc::handle_champion_t>(me));
            const typename Policy::AssignmentWorker worker{self_id,origin.x,origin.y,s.hp,s.maximum,s.strength,true,s.identity};
            auto& context=commitment_context();
            auto board=context.board;
            // Board jobs use sorted public handles; keep the policy's original
            // candidate IDs and utility spread when eliminating fixed columns.
            for(auto& row:board.rows)if(row.job>=0)row.job=context.group_indices[row.job];
            selected=Policy::select_committed(s,candidates,worker,board,context.self_slot);
            if(int(cambc::round_number())!=round)return;
            if(selected<0){target_=-1;partial_clear_=false;return;}
        } else if constexpr(requires{Policy::local_assignment;}) {
            if(cambc::fuel_remaining()<35000)return;
            using Worker=typename Policy::AssignmentWorker;
            using Job=typename Policy::AssignmentJob;
            std::vector<Worker> workers;workers.reserve(3);
            const int self_id=int(static_cast<cambc::handle_champion_t>(me));
            workers.push_back({self_id,origin.x,origin.y,s.hp,s.maximum,s.strength,true,s.identity});
            std::vector<champion> peers;
            for(const auto&ally:allies)if(ally!=me)peers.push_back(ally);
            std::sort(peers.begin(),peers.end(),[](const auto&a,const auto&b){return static_cast<cambc::handle_champion_t>(a)<static_cast<cambc::handle_champion_t>(b);});
            std::optional<bitboard> recovery_region;
            for(const auto&a:peers) {
                if(workers.size()>=3)break;
                const int peer_id=int(static_cast<cambc::handle_champion_t>(a));
                const auto observed=Policy::observe_worker(peer_id,[](int handle)
                        ->std::optional<cambc::detail::champion_info> {
                    // This is the same checked, vision-gated record import used
                    // by SDK champion accessors. Read once, not once per field.
                    cambc::detail::champion_info record{};
                    const auto code=cambc::detail::sdk_champion(std::uint32_t(handle),&record);
                    if(code==97)return std::nullopt; // SDK api_not_in_vision convention
                    cambc::detail::check(code);return record;
                });
                if(int(cambc::round_number())!=round)return;
                if(!observed)continue;
                const auto flags=cambc::champion_state::from_bits_retain(std::uint32_t(observed->state));
                if(!flags.contains(cambc::champion_state::alive))continue;
                auto worker=observed->worker;const vec2 p{worker.x,worker.y};
                if(p.x<0||p.y<0||p.x>=cambc::map::width()||p.y>=cambc::map::height())continue;
                bool busy=flags.contains(cambc::champion_state::recalling)
                    ||flags.contains(cambc::champion_state::submitting_gems)||flags.contains(cambc::champion_state::teleporting);
                if(worker.hp<worker.maximum) {
                    if(!recovery_region)recovery_region=cambc::map::spawn_region(team::ally);
                    busy|=recovery_region->is_set(p);
                }
                const int occupied_group=group_at_tile_[p.y*cambc::map::width()+p.x];
                if(occupied_group>=0&&visible.is_set(p))
                    busy|=std::holds_alternative<cambc::map::camp_group_reward_available>(groups_[occupied_group].group.state());
                worker.available=!busy;workers.push_back(worker);
            }
            std::vector<Job> jobs;jobs.reserve(candidates.size());
            for(const auto&c:candidates)jobs.push_back({c,groups_[c.id].anchor.x,groups_[c.id].anchor.y});
            const int assigned=Policy::assign(s,jobs,workers,self_id);
            if(int(cambc::round_number())!=round)return;
            selected=Policy::select(s,candidates,assigned);
            if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;}) {
                if(round%64==int(identity_%64)&&cambc::fuel_remaining()>30000)
                    std::printf("p11match actor=%d round=%d workers=%u available=%u jobs=%u assigned=%d selected=%d\n",self_id,round,unsigned(workers.size()),unsigned(std::count_if(workers.begin(),workers.end(),[](const auto&w){return w.available;})),unsigned(jobs.size()),assigned,selected);
            }
            // The solver's private wait is a real activity, not permission to
            // duplicate another local worker's job through partial fallback.
            if(int(cambc::round_number())!=round)return;
            if(assigned<0){target_=-1;partial_clear_=false;return;}
        } else if constexpr(requires{Policy::route_select;}) {
            using Job=typename Policy::RouteJob;
            std::vector<Job> jobs;jobs.reserve(candidates.size());
            for(const auto&c:candidates)jobs.push_back({c,groups_[c.id].anchor.x,groups_[c.id].anchor.y});
            const auto planned=p11::plan_in_round(round,[]{return cambc::round_number();},
                [&]{return Policy::select_route(s,jobs);});
            if(!planned)return;
            selected=*planned;
            if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;}) {
                if(round%64==int(identity_%64)&&cambc::fuel_remaining()>30000)
                    std::printf("p11route actor=%u round=%d jobs=%u selected=%d\n",unsigned(static_cast<cambc::handle_champion_t>(me)),round,unsigned(jobs.size()),selected);
            }
            if(int(cambc::round_number())!=round)return;
            if constexpr(requires{Policy::private_wait;}) {
                if(selected<0){target_=-1;partial_clear_=false;return;}
            }
        } else {
            const auto planned=p11::plan_in_round(round,[]{return cambc::round_number();},
                [&]{return p11::choose<Policy>(s,candidates);});
            if(!planned)return;
            selected=*planned;
        }
        bool partial=false;
        // A retained partial clear is a fallback, not a claimed full-clear
        // forecast. Preserve baseline progress when every full clear is unsafe.
        if(selected<0) {
            selected=p11::choose_partial<Policy>(s,candidates);
            partial=selected>=0;
        }
        if constexpr(requires(const p11::State&a,const p11::Candidate&b){Policy::admissible(a,b);}) {
            for(const auto&c:candidates) if(c.id==selected&&!p11::feasible(s,c)) partial=true;
        }
        if(int(cambc::round_number())!=round)return;
        target_=selected;partial_clear_=partial;
#if P11_GOAL_RECEIPT
        if(goal_receipt_.current(round)){
            goal_receipt_.selected=selected;
            goal_receipt_.native_group=selected>=0?int(static_cast<cambc::handle_camp_group_t>(groups_[selected].group)):-1;
            goal_receipt_.origin_x=origin.x;goal_receipt_.origin_y=origin.y;
            goal_receipt_.partial=partial;goal_receipt_.select_fuel=cambc::fuel_remaining();goal_receipt_.stage=1;
        }
#endif
        if constexpr(requires{typename Policy::InformationClock;}) {
            if(round%64==int(identity_%64)&&cambc::fuel_remaining()>40000)
                std::printf("p11info actor=%u round=%d first=%d second=%d third=%d scanned=%u selected=%d\n",
                    unsigned(static_cast<cambc::handle_champion_t>(me)),round,
                    order.size()>0?order[0]:-1,order.size()>1?order[1]:-1,order.size()>2?order[2]:-1,scanned,selected);
        }
        if(selected<0) {
            if(!cambc::map::spawn_region(team::ally).is_set(origin)) recovering_=true;
            return;
        }
        auto&entry=groups_[selected];bool selected_reward=false;
        for(const auto&c:candidates) if(c.id==selected) selected_reward=c.ready;
#if P11_GOAL_RECEIPT
        if(goal_receipt_.current(round))goal_receipt_.reward=selected_reward;
#endif
        if(selected_reward){
#if P11_GOAL_RECEIPT
            if(goal_receipt_.current(round))goal_receipt_.stage=2;
#endif
            const bool moved=move_to(entry.spawns);
#if P11_GOAL_RECEIPT
            if(goal_receipt_.current(round)){
                goal_receipt_.moved=moved;goal_receipt_.move_after=cambc::fuel_remaining();
                goal_receipt_.goal_round=int(cambc::round_number());
            }
#endif
            if constexpr(requires{Policy::committed_assignment;})if(moved)publish_commitment(selected,round);
            if(!moved&&int(cambc::round_number())==round)entry.unavailable_until=round+3;
            return;
        }
        std::optional<camp> target;
        if constexpr(requires{Policy::target_horizon;}) {
            // Reuse legal live observations already collected for the detailed
            // frontier. Repeating is_alive/health/strength here spent host fuel
            // before the target search could start. Their owner-round is checked
            // both before admission and immediately before execution.
            if(int(cambc::round_number())!=round)return;
            const bool attack_ready=me.can_attack();
            const int before_mask=cambc::fuel_remaining();
            const bool mask_known=attack_ready&&before_mask>45000;
            bitboard attackable;if(mask_known)attackable=me.attack_tiles();
            std::vector<p11::focus::View> views;views.reserve(target_observations[selected].size());
            for(const auto&observed:target_observations[selected])
                views.push_back({int(static_cast<cambc::handle_camp_t>(observed.target)),observed.hp,
                    observed.incoming,true,mask_known&&attackable.is_set(observed.target.spawn_position())});
            const int search_fuel=cambc::fuel_remaining();
            const auto decision=p11::focus::select<Policy>(views,s.strength*(monster_shards_>=5?1.1f:1.f),
                attack_ready,search_fuel,round,int(cambc::round_number()));
            if(int(cambc::round_number())!=round)return;
            for(const auto&observed:target_observations[selected])
                if(int(static_cast<cambc::handle_camp_t>(observed.target))==decision.selected){target=observed.target;break;}
            if constexpr(requires{Policy::trace_scan;} && !requires{Policy::reserve_action_budget;}) {
                static int first_visible=0,first_planned=0;
                const bool planned=decision.reason==p11::focus::Reason::planned;
                if(cambc::fuel_remaining()>30000&&
                   ((decision.visible>0&&first_visible<4)||(planned&&first_planned<4)||round%64==int(identity_%64))) {
                    if(decision.visible>0&&first_visible<4)++first_visible;
                    if(planned&&first_planned<4)++first_planned;
                    std::printf("p11triage actor=%u round=%d seen=%d jobs=%d planned=%d focus=%d baseline=%d reason=%d fuel=%d mask=%d\n",
                        unsigned(static_cast<cambc::handle_champion_t>(me)),round,decision.visible,decision.jobs,
                        int(planned),decision.selected,decision.baseline,int(decision.reason),search_fuel,int(mask_known));
                }
            }
        } else if constexpr(requires{Policy::reuse_farm_observations;}) {
            std::vector<typename Policy::BasicTargetView> views;
            views.reserve(target_observations[selected].size());
            for(const auto& observed:target_observations[selected])
                views.push_back({int(static_cast<cambc::handle_camp_t>(observed.target)),observed.hp,observed.incoming});
            const auto selected_target=Policy::baseline_target(views,s.strength*(monster_shards_>=5?1.1f:1.f),
                round,int(cambc::round_number()));
            if(!selected_target)return;
            for(const auto& observed:target_observations[selected])
                if(int(static_cast<cambc::handle_camp_t>(observed.target))==*selected_target){target=observed.target;break;}
        } else {
            float weakest = std::numeric_limits<float>::max();
            for (const auto& monster : camps)
                if (entry.spawns.is_set(monster.spawn_position()) && monster.is_alive()) {
                    const float effective=me.strength()*(monster_shards_>=5?1.1f:1.f);
                    const auto score = p11::target_rank(monster.health(),monster.strength(),effective);
                    if(int(cambc::round_number())!=round)return;
                    if (score < weakest) { weakest = score; target = monster; }
                }
        }
        if(int(cambc::round_number())!=round)return;
#if P11_GOAL_RECEIPT
        if(goal_receipt_.current(round)){
            goal_receipt_.target=target?int(static_cast<cambc::handle_camp_t>(*target)):-1;
            goal_receipt_.stage=3;
        }
#endif
        if (target && fight(*target, false, entry.incoming > 0)) {
            if constexpr(requires{Policy::committed_assignment;})publish_commitment(selected,round);
            // A melee lethal attack may leave its movement available. Claim
            // the cleared tile now instead of discarding that action token.
            if(cambc::fuel_remaining()>35000&&!target->is_alive())finish_clear_turn(entry,round);
#if defined(P11_TRIAGE_POSTCAST_BASIC) && P11_TRIAGE_POSTCAST_BASIC
            if constexpr(requires{Policy::triage_postcast_basic;})postcast_basic(entry,*target,round);
#endif
            return;
        }
        bitboard goals;
#if P11_GOAL_RECEIPT
        if(goal_receipt_.current(round))goal_receipt_.goal_before=cambc::fuel_remaining();
#endif
#if P11_GOAL_RECEIPT
        if (target) {
            const auto target_position=target->position();const int target_range=me.attack_range();
            goals=around(target_position,target_range);
            if(goal_receipt_.current(round)){
                goal_receipt_.target_x=target_position.x;goal_receipt_.target_y=target_position.y;
                goal_receipt_.radius=target_range;
            }
        } else {
            for (const auto& monster : entry.group) {
                goals |= around(monster.spawn_position(), 1);
                if(goal_receipt_.current(round))++goal_receipt_.members;
            }
        }
        if(goal_receipt_.current(round)){
            goal_receipt_.goal_after=cambc::fuel_remaining();
            goal_receipt_.goal_round=int(cambc::round_number());goal_receipt_.stage=4;
        }
#else
        if (target) goals = around(target->position(), me.attack_range());
        else {
            for (const auto& monster : entry.group) goals |= around(monster.spawn_position(), 1);
        }
#endif
#include "farm_move.inc"

    }

public:
    void turn() {
        auto& me = cambc::self();
#if P11_BOUNDED_INITIALIZATION
        // Even the completing initialization turn yields. The next turn starts
        // gameplay with fresh fuel and no suspended initialization locals.
        if (!initialized_) { initialize_bounded(); return; }
#else
        if (!initialized_) initialize();
#endif
        const int round=cambc::round_number();
#if P11_GOAL_RECEIPT
        if(goal_receipt_.eligible(identity_,round,P11_GOAL_RECEIPT_ROUND)){
            goal_receipt_.begin(round,-2,-1,-1,-1,partial_clear_,cambc::fuel_remaining());
            goal_receipt_.stage=0;goal_receipt_.retained_target=target_;goal_receipt_.recovering_enter=recovering_;
        }
        goal_receipt_.flush(round,cambc::fuel_remaining());
#endif
#if P11_EXPEDITION_RECEIPT
        if(expedition_receipt_.flush(round,[](const auto&packet,int flush_round){p11::expedition::print(packet,flush_round);}))return;
#endif
        if constexpr(requires{Policy::committed_assignment;}) {
            // Release this owner's old intent before death, channel, recovery,
            // banking or a new farm decision. Successful work renews it below.
            if(commitment_context().supported&&!publish_commitment(-1,round))return;
        }
        // Dead champions retain shopping turns, but have no usable position.
        if (!me.is_alive()) {
#if P11_CONTEST_CONTINUATION
            contest_continuation_.clear();
#endif
            shop(); recovering_ = false; target_ = -1; return;
        }
        const auto channel=channel_snapshot();
        if(p11::kernel::preserve_channel(channel)) {
#if P11_CONTEST_CONTINUATION
            contest_continuation_.clear();
#endif
#if P11_GOAL_RECEIPT
            if(goal_receipt_.current(round))goal_receipt_.stage=6;
#endif
            trace_channel(channel,false);return;
        }
#if defined(P11_GEODESIC_FRESH_ROUTE_TURN) && P11_GEODESIC_FRESH_ROUTE_TURN
        if constexpr(requires{Policy::geodesic;}) {
            // Immutable route preparation keeps its own turn. Even the turn
            // that completes the cache yields before ordinary planning.
            const bool ready=routes().entry.size()==groups_.size()&&!routes().altar_field.empty();
            if(!prepare_routes())return;
            if(!ready)return;
        }
#else
        if constexpr(requires{Policy::geodesic;})if(!prepare_routes())return;
#endif
#line 1574 "D:/files/battlecode/bots/events/common/src/runtime.h"
        if(int(cambc::round_number())!=round)return;
        if (!me.can_move() && !me.can_attack() && !me.can_cast()) return;
        shop();
        if(int(cambc::round_number())!=round)return;
        if constexpr(requires{Policy::capital_shop;})
            if(cambc::fuel_remaining()<30000)return;
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
        if (!spawn && Policy::retreat(me.health(), me.max_health(),
            incoming * (partial_clear_ ? 2.0f : 1.0f))) recovering_ = true;
        if (recovering_) {
#if P11_CONTEST_CONTINUATION
            contest_continuation_.clear();
#endif
#if P11_GOAL_RECEIPT
            if(goal_receipt_.current(round)){goal_receipt_.stage=11;goal_receipt_.recovering_now=true;}
#endif
            recover(camps, enemies); return;
        }
        // Cleared camp ownership takes precedence over banking old gems.
#if P11_CONTEST_CONTINUATION
        // Leaving the spawn mask to reach a current contester must not switch
        // the very next turn to the unrelated nearest-spawn re-entry program.
        // Only this owner's prior successful contest step can license this
        // branch; fresh personal enemy visibility licenses the group query.
        if(target_>=0&&contest_continuation_.pending(target_)) {
            auto&entry=groups_[target_];
            const bool on_source=entry.spawns.is_set(me.position());
            if(on_source)contest_continuation_.clear();
            else {
                std::optional<champion> contester;
                for(const auto&enemy:enemies) if(enemy.is_alive()&&entry.spawns.is_set(enemy.position())) {
                    if(!contester||enemy.health()<contester->health())contester=enemy;
                }
                const bool reward=contester&&std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state());
                const bool fresh=int(cambc::round_number())==round;
                if(!fresh){contest_continuation_.clear();return;}
                if(contest_continuation_.permitted(target_,on_source,bool(contester),reward,fresh)) {
                    if constexpr(requires{Policy::committed_assignment;})publish_commitment(target_,round);
                    if(int(cambc::round_number())!=round){contest_continuation_.clear();return;}
                    if(fight(*contester,false,true))return;
                    const auto goals=around(contester->position(),me.attack_range())&entry.spawns;
                    const bool moved=move_to(goals);
                    const bool same=int(cambc::round_number())==round;
                    contest_continuation_.departed(target_,moved,moved&&entry.spawns.is_set(me.position()),same);
                    if(!same)return;
                    if(moved) {
                        if(contester->is_visible()&&contester->is_alive())fight(*contester,true,true);
                    }
                    return;
                }
                contest_continuation_.clear();
            }
        }
#endif
        if (target_ >= 0) {
            auto& entry = groups_[target_];
            if (entry.spawns.is_set(me.position()) && std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state())) {
                if constexpr(requires{Policy::committed_assignment;})publish_commitment(target_,round);
                std::optional<champion> contester;
                for(const auto&enemy:enemies) if(entry.spawns.is_set(enemy.position())) {
                    if(!contester || enemy.health()<contester->health()) contester=enemy;
                }
                if(p11::capture_action(bool(contester))==p11::CaptureAction::contest) {
                    if(fight(*contester,false,true)) return;
                    auto goals=around(contester->position(),me.attack_range())&entry.spawns;
#if P11_CONTEST_CONTINUATION
                    const bool moved=move_to(goals);
                    if(moved) {
                        const bool same=int(cambc::round_number())==round;
                        contest_continuation_.departed(target_,true,entry.spawns.is_set(me.position()),same);
                        if(!same)return;
                        if(contester->is_visible()&&contester->is_alive())fight(*contester,true,true);
                    }
#else
                    if(move_to(goals)&&contester->is_visible()&&contester->is_alive()) fight(*contester,true,true);
#endif
                }
                if constexpr(requires{Policy::capture_tribute;}) {
                    if(!contester&&me.gem_count()>0&&cambc::fuel_remaining()>30000) {
                        const auto locks=cambc::vision::altar_locks(team::enemy);
                        for(const auto altar:altars_) {
                            const bool locked=std::any_of(locks.begin(),locks.end(),[&](const auto&lock){return lock.position()==altar;});
                            if(Policy::can_overlap_capture_tribute(true,true,false,false,int(me.gem_count()),distance(me.position(),altar),cambc::constants::submit_gems_range,locked)) {
                                me.submit_gems(altar);trace_channel(channel_snapshot(),true);
                                if(cambc::fuel_remaining()>15000)
                                    std::printf("p11overlap actor=%u round=%u group=%u\n",unsigned(static_cast<cambc::handle_champion_t>(me)),cambc::round_number(),unsigned(static_cast<cambc::handle_camp_group_t>(entry.group)));
                                return;
                            }
                        }
                    }
                }
                return;
            }
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
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
        farm(camps,cambc::vision::champions(team::ally),enemies);
#else
        farm(camps, cambc::vision::champions(team::ally));
#endif
    }
};

template<class Policy> int run() {
    Bot<Policy> bot;
    while (true) {
        try {
            if (cambc::is_draft()) draft();
            else bot.turn();
        }
#if P11_MOVE_FUEL_GUARD
        catch (const p11::movement::Yield&) {
            // A completed move remains completed; fresh locals and vision
            // start next owner turn. Expected fuel yields are not errors.
        }
#endif
        catch (const std::exception& error) {
            std::cout << "p11 error: " << error.what() << '\n';
        } catch (...) {
            std::cout << "p11 error: unknown exception\n";
        }
        cambc::end_turn();
    }
}

} // namespace p11_runtime
