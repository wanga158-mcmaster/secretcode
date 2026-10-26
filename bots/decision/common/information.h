#pragma once
#include <cambc.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#ifndef PLAN3_STARTUP_LAYOUT_PHASE
#define PLAN3_STARTUP_LAYOUT_PHASE 0
#endif
#ifndef PLAN3_OBSERVATION_IDENTITY_JOIN
#define PLAN3_OBSERVATION_IDENTITY_JOIN 0
#endif
#ifndef PLAN3_OBSERVATION_VISIBLE_JOIN
#define PLAN3_OBSERVATION_VISIBLE_JOIN 0
#endif
#if PLAN3_OBSERVATION_VISIBLE_JOIN
#include <array>
#endif

namespace plan3_runtime {
struct PublicCampMember {
    cambc::vec2 spawn;
    double health = 0, strength = 0;
};
struct GroupDesign {
    cambc::map::camp_group group;
    cambc::bitboard spawns;
    cambc::vec2 anchor;
    int key = -1, field = -1;
    double full_health = 0, incoming = 0;
    bool monolith = false;
    int gem_value = 0, gold_value = 0, max_respawn_timer = 0;
    std::vector<cambc::vec2> spawn_positions;
    std::vector<PublicCampMember> members;
};
enum class Lifecycle { alive, reward, respawning };
struct GroupObservation {
    std::optional<Lifecycle> lifecycle;
    unsigned round = 0;
    int respawn_remaining = 0;
    double remaining_health = 0, incoming = 0;
    bool all_members_visible = false;
};
struct EnemyObservation {
    unsigned round = 0;
    int identity = -1;
    cambc::vec2 position;
    double strength = 0;
    int range = 0;
};
class PersonalInformation {
    std::size_t observation_cursor_ = 0;
#if PLAN3_OBSERVATION_VISIBLE_JOIN
    struct VisibleJoin {
        static constexpr std::size_t maximum_members=64,capacity=128;
        static constexpr std::size_t empty=static_cast<std::size_t>(-1);
        std::array<std::size_t,capacity> first;
    };
    static std::size_t visible_identity_hash(const cambc::camp& camp) {
        const auto spawn=camp.spawn_position();
        auto value=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camp));
        value^=static_cast<std::uint32_t>(spawn.x)*0x9e3779b1U;
        value=(value^(value>>16))*0x85ebca6bU;
        value^=static_cast<std::uint32_t>(spawn.y)*0xc2b2ae35U;
        return (value^(value>>16))&(VisibleJoin::capacity-1);
    }
    static bool prepare_visible_join(VisibleJoin& join,const std::vector<cambc::camp>& camps,
                                     std::uint64_t minimumFuel) {
        if(camps.size()>VisibleJoin::maximum_members || cambc::fuel_remaining()<minimumFuel)return false;
        join.first.fill(VisibleJoin::empty);
        // At most64 insertions into128 slots. No live state or host query is
        // cached. Equal pairs retain the first original visible-list ordinal.
        for(std::size_t i=0;i<camps.size();++i) {
            auto at=visible_identity_hash(camps[i]);bool inserted=false;
            for(std::size_t step=0;step<VisibleJoin::capacity;++step) {
                if(cambc::fuel_remaining()<minimumFuel)return false;
                const auto prior=join.first[at];
                if(prior==VisibleJoin::empty){join.first[at]=i;inserted=true;break;}
                if(camps[prior]==camps[i] && camps[prior].spawn_position()==camps[i].spawn_position()) {
                    inserted=true;break;
                }
                at=(at+1)&(VisibleJoin::capacity-1);
            }
            if(!inserted)return false;
        }
        return true;
    }
    static const cambc::camp* visible_join_first(const VisibleJoin& join,
            const std::vector<cambc::camp>& camps,const cambc::camp& stored,
            std::uint64_t minimumFuel,bool& finished) {
        auto at=visible_identity_hash(stored);
        for(std::size_t step=0;step<VisibleJoin::capacity;++step) {
            if(cambc::fuel_remaining()<minimumFuel){finished=false;return nullptr;}
            const auto ordinal=join.first[at];
            if(ordinal==VisibleJoin::empty)return nullptr;
            const auto& camp=camps[ordinal];
            if(camp==stored && camp.spawn_position()==stored.spawn_position())return &camp;
            at=(at+1)&(VisibleJoin::capacity-1);
        }
        // An incomplete or corrupt table never licenses an absence claim.
        finished=false;return nullptr;
    }
#endif
#if PLAN3_OBSERVATION_IDENTITY_JOIN
    struct IdentityMember {
        cambc::vec2 spawn;
        std::uint32_t handle;
    };
    struct IdentityJoin {
        std::size_t group_index;
        std::vector<IdentityMember> original;
        std::vector<std::size_t> order;
    };
    // Separate from GroupDesign: forecasts keep their original public payload.
    // No current position, state, health, visibility or clock is stored here.
    std::vector<IdentityJoin> identity_joins_;
    static bool identity_spawn_less(cambc::vec2 a,cambc::vec2 b) {
        return a.x<b.x || (a.x==b.x && a.y<b.y);
    }
    const IdentityJoin* prepare_identity_join(std::size_t index,std::uint64_t minimumFuel) {
        constexpr std::size_t maximum_members=64,maximum_groups=64;
        const auto members=groups[index].group.camps();
        if(members.size()>maximum_members)return nullptr;
        auto existing=std::find_if(identity_joins_.begin(),identity_joins_.end(),
            [index](const auto& join){return join.group_index==index;});
        if(existing!=identity_joins_.end() && existing->original.size()==members.size()) {
            bool same=true;
            // Validate values, not storage addresses: assignment may reuse a
            // vector allocation while changing its identities or spawn tiles.
            for(std::size_t i=0;i<members.size();++i) {
                if(cambc::fuel_remaining()<minimumFuel)return nullptr;
                const auto& prior=existing->original[i];
                if(prior.spawn!=members[i].spawn_position() || prior.handle!=
                    static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(members[i]))) {
                    same=false;break;
                }
            }
            if(same)return &*existing;
        }
        if(existing==identity_joins_.end() && identity_joins_.size()>=maximum_groups)return nullptr;
        if(cambc::fuel_remaining()<minimumFuel)return nullptr;
        IdentityJoin next{index,{},{}};
        next.original.reserve(members.size());next.order.reserve(members.size());
        // At most64 insertions and2016 shifts. Every construction step keeps
        // the caller's existing reserve; incomplete indices are never used.
        for(std::size_t i=0;i<members.size();++i) {
            if(cambc::fuel_remaining()<minimumFuel)return nullptr;
            const auto spawn=members[i].spawn_position();
            next.original.push_back({spawn,static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(members[i]))});
            next.order.push_back(i);
            std::size_t at=next.order.size()-1;
            while(at) {
                if(cambc::fuel_remaining()<minimumFuel)return nullptr;
                if(!identity_spawn_less(spawn,next.original[next.order[at-1]].spawn))break;
                next.order[at]=next.order[at-1];--at;
            }
            next.order[at]=i;
        }
        if(existing!=identity_joins_.end()) {*existing=std::move(next);return &*existing;}
        identity_joins_.push_back(std::move(next));return &identity_joins_.back();
    }
    static const cambc::camp* identity_join_stored(const IdentityJoin& join,
            std::span<const cambc::camp> members,cambc::vec2 spawn,unsigned& matches) {
        // Two bounded binary searches preserve every duplicate, including two
        // identical handles at one spawn. Only exactly one match is usable.
        const auto first=std::lower_bound(join.order.begin(),join.order.end(),spawn,
            [&](std::size_t i,cambc::vec2 value){return identity_spawn_less(join.original[i].spawn,value);});
        const auto last=std::upper_bound(first,join.order.end(),spawn,
            [&](cambc::vec2 value,std::size_t i){return identity_spawn_less(value,join.original[i].spawn);});
        matches=static_cast<unsigned>(last-first);
        return matches==1?&members[*first]:nullptr;
    }
#endif
public:
    std::vector<GroupDesign> groups;
    std::vector<GroupObservation> observations;
    std::vector<EnemyObservation> enemies, previous_enemies;
    std::vector<cambc::vec2> altars;
    std::vector<int> altar_fields;
    cambc::bitboard terrain;
    cambc::bitboard own_spawn;
    std::vector<cambc::vec2> spawn_positions;
    int width = 0, team_size = 0;
    unsigned observation_round = 0;
    int spawn_field = -1;
    bool any_gems = false;
    std::vector<cambc::map::camp_group> initialization_groups;
    std::optional<GroupDesign> initialization_group;
    std::size_t initialization_at=0, initialization_member=0;
    bool initialization_layout=false, initialization_complete=false;
    bool initialize_step() {
        if(initialization_complete)return true;
        using cambc::team;
        if(!initialization_layout) {
            terrain=cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
                | cambc::map::spawn_region(team::enemy);
            altars=cambc::map::altars().positions(); width=cambc::map::width();
            team_size=static_cast<int>(cambc::champions(team::ally).size());
            own_spawn=cambc::map::spawn_region(team::ally); spawn_positions=own_spawn.positions();
            initialization_groups=cambc::map::camp_groups(); initialization_layout=true;
            return false;
        }
        if(initialization_at>=initialization_groups.size()) {
            std::sort(groups.begin(),groups.end(),[](const auto& a,const auto& b){return a.key<b.key;});
            observations.resize(groups.size()); initialization_complete=true;
#if PLAN3_STARTUP_LAYOUT_PHASE
            // Publish the immutable layout on the next callback. The caller's
            // public route ordering must not share this final sort's fuel.
            return false;
#else
            return true;
#endif
        }
        if(cambc::fuel_remaining()<65000)return false;
        const auto& group=initialization_groups[initialization_at];
        if(!initialization_group) {
            initialization_group.emplace(GroupDesign{group,cambc::bitboard{},cambc::vec2{},-1,-1,0,0,false,
                static_cast<int>(group.gem_value()),static_cast<int>(group.gold_value()),group.max_respawn_timer(),{},{}});
            initialization_member=0;
        }
        auto& d=*initialization_group;
        for(unsigned work=0;work<4 && initialization_member<group.camps().size() && cambc::fuel_remaining()>=65000;++work) {
            const auto& camp=group.camps()[initialization_member++];
            const auto stats=camp.base_stats(); const auto spawn=camp.spawn_position();
            const int tile=spawn.y*width+spawn.x;
            d.spawns.set(spawn); d.spawn_positions.push_back(spawn);
            if(d.key<0 || tile<d.key) { d.key=tile; d.anchor=spawn; }
            d.full_health+=stats.base_health; d.incoming+=stats.base_strength;
            d.monolith|=camp.is_monolith(); d.members.push_back({spawn,stats.base_health,stats.base_strength});
        }
        if(initialization_member==group.camps().size()) {
            if(d.key>=0) { any_gems|=!d.monolith && d.gem_value>0; groups.push_back(std::move(d)); }
            initialization_group.reset(); ++initialization_at;
        }
        return false;
    }
    // Publish one conservative observation in this callback's existing clock.
    // Only immutable membership is reused; every live quantity is read afresh.
    bool observe_group(std::size_t index,const std::vector<cambc::camp>& camps,
                       const cambc::bitboard& visible,std::uint64_t minimumFuel=60000) {
        const unsigned round=observation_round;
        if(index>=groups.size() || index>=observations.size() || cambc::fuel_remaining()<minimumFuel ||
           cambc::round_number()!=round)return false;
        const auto& d=groups[index];
        if((d.spawns&visible).is_empty())return false;
        GroupObservation o;o.round=round;
        const auto state=d.group.state(); // Guarded by a personally visible spawn.
        if(const auto* r=std::get_if<cambc::map::camp_group_respawning>(&state)) {
            o.lifecycle=Lifecycle::respawning;o.respawn_remaining=r->timer;
        } else if(std::holds_alternative<cambc::map::camp_group_reward_available>(state))o.lifecycle=Lifecycle::reward;
        else o.lifecycle=Lifecycle::alive;
        o.all_members_visible=(d.spawns&~visible).is_empty();
        if(o.lifecycle==Lifecycle::alive){o.remaining_health=d.full_health;o.incoming=d.incoming;}
        const auto unavailable=[](std::int32_t code) {
            return code==static_cast<std::int32_t>(cambc::error_code::api_not_in_vision) ||
                   code==static_cast<std::int32_t>(cambc::error_code::api_not_found);
        };
#if PLAN3_OBSERVATION_IDENTITY_JOIN
        const auto* identity_join=prepare_identity_join(index,minimumFuel);
#endif
#if PLAN3_OBSERVATION_VISIBLE_JOIN
        VisibleJoin visible_join;
        bool visible_join_attempted=false,visible_join_ready=false;
#endif
        for(const auto& design:d.members) {
            if(cambc::fuel_remaining()<minimumFuel){o.all_members_visible=false;break;}
            // The public group supplies the exact identity for this spawn.
            // An unfinished/ambiguous join retains its full public quantities.
            const cambc::camp* stored=nullptr;unsigned matches=0;bool finished=true;
#if PLAN3_OBSERVATION_IDENTITY_JOIN
            if(identity_join)stored=identity_join_stored(*identity_join,d.group.camps(),design.spawn,matches);
            else
#endif
            for(const auto& member:d.group.camps()) {
                if(cambc::fuel_remaining()<minimumFuel){finished=false;break;}
                if(member.spawn_position()==design.spawn){stored=&member;++matches;}
            }
            if(!finished){o.all_members_visible=false;break;}
            if(matches!=1 || !stored){o.all_members_visible=false;continue;}
            const cambc::camp* found=nullptr;
#if PLAN3_OBSERVATION_VISIBLE_JOIN
            if(!visible_join_attempted) {
                visible_join_attempted=true;
                visible_join_ready=prepare_visible_join(visible_join,camps,minimumFuel);
            }
            if(visible_join_ready)found=visible_join_first(visible_join,camps,*stored,minimumFuel,finished);
            else
#endif
            for(const auto& camp:camps) {
                if(cambc::fuel_remaining()<minimumFuel){finished=false;break;}
                if(camp==*stored && camp.spawn_position()==design.spawn){found=&camp;break;}
            }
            if(!finished){o.all_members_visible=false;break;}
            if(!found && !(o.lifecycle==Lifecycle::alive && visible.is_set(design.spawn)))continue;
            if(cambc::round_number()!=round)return false;
            if(cambc::fuel_remaining()<minimumFuel){o.all_members_visible=false;break;}
            std::uint32_t value{};
            const auto native=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(*stored));
            const auto code=cambc::detail::sdk_camp_state(native,&value);
            if(unavailable(code)){o.all_members_visible=false;continue;}
            cambc::detail::check(code);
            if(value>2)throw cambc::error{cambc::error_code::api_value_out_of_range,"unknown engine camp state"};
            if(cambc::round_number()!=round)return false;
            if(value==0) {
                if(o.lifecycle==Lifecycle::alive){o.remaining_health-=design.health;o.incoming-=design.strength;}
                continue;
            }
            if(!found){o.all_members_visible=false;continue;}
            if(cambc::fuel_remaining()<minimumFuel){o.all_members_visible=false;break;}
            cambc::detail::camp_state live{};
            const auto liveCode=cambc::detail::sdk_camp(native,&live);
            if(unavailable(liveCode)){o.all_members_visible=false;continue;}
            cambc::detail::check(liveCode);
            if(o.lifecycle==Lifecycle::alive){o.remaining_health-=design.health;o.incoming-=design.strength;}
            o.remaining_health+=live.health;o.incoming+=live.strength;
        }
        if(cambc::round_number()!=round)return false;
        observations[index]=o;return true;
    }
    void observe(const std::vector<cambc::camp>& camps,
                 const std::vector<cambc::champion>& visible_enemies) {
        observation_round = cambc::round_number();
        const auto visible = cambc::vision::visible_tiles();
        previous_enemies = enemies;
        enemies.clear();
        for (const auto& enemy : visible_enemies) {
            // Every live field comes from this actor's current vision only.
            enemies.push_back({observation_round, static_cast<int>(enemy.champion_name()),
                enemy.position(), enemy.strength(), enemy.attack_range()});
        }
        // Inspection shares the turn with candidate construction. Rotate a
        // bounded public-group scan instead of consuming the whole planning
        // reserve on maps with many groups. Unscanned old records remain
        // history, never current visibility; the active job is refreshed
        // separately by the executor's guarded priority observation.
        for (std::size_t work = 0; work < std::min<std::size_t>(4,groups.size()); ++work) {
            if (cambc::fuel_remaining() < 65000) break;
            const auto i = observation_cursor_;
            observation_cursor_ = (observation_cursor_+1)%groups.size();
            observe_group(i,camps,visible,60000);
        }
    }
    const GroupObservation* current(std::size_t i) const {
        const auto& o = observations[i];
        return o.lifecycle && o.round == observation_round ? &o : nullptr;
    }
    int remembered_respawn_minimum(std::size_t i) const {
        const auto& o=observations[i];
        if(o.lifecycle!=Lifecycle::respawning || o.round>observation_round)return 0;
        return std::max(0,o.respawn_remaining-static_cast<int>(observation_round-o.round));
    }
};
} // namespace plan3_runtime
