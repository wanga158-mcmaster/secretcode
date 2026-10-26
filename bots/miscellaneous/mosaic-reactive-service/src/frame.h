#pragma once
// One observation packet per own-action revision. Complete covers the advertised
// Domain family in one owner epoch; remote/hidden facts stay explicitly unknown.
// Candidate ranking reads data only and checks Domain support before certification.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>
#include "spatial_support.h"
#include "native_observation.h"
#include "observation_domain.h"

namespace mosaic::frame {

enum class GroupState { unknown = 0, alive = 1, reward = 2, respawning = 3 };

struct Self {
    cambc::vec2 pos{};
    float hp{}, max_hp{}, strength{}, focus{}, shield{}, camp_multiplier{1.0f};
    float basic_damage{}, camp_taken_multiplier{1.0f}, champion_taken_multiplier{1.0f};
    int range{}, move_speed{}, gold{}, gems{}, shield_duration{}, splash_radius{};
    bool alive{}, can_move{}, can_attack{}, can_cast{}, moved{}, attacked{}, cast_spent{};
    bool recalling{}, submitting{}, teleporting{}, stunned{}, enhanced_attack{};
    bool basic_damage_known{}, damage_modifiers_known{};
    cambc::passive_type passive{};
};
struct Ability {
    int slot{-1}, cooldown{}, max_cooldown{}, duration{};
    float range{}, radius{}, damage_amount{}, shield_amount{}, heal_amount{};
    bool ready{}, exclusive{}, reset{}, dash{}, heal{}, shield{}, damage{}, aoe{}, cc{};
    bool camps{}, enemies{}, allies{}, self{}, ground{}, enhanced{};
    std::vector<cambc::vec2> legal_targets;
};
struct Camp {
    int handle{-1}, group{-1}, member{-1}, observed_round{-1};
    cambc::vec2 pos{}, spawn{};
    float hp{}, strength{}, design_hp{}, design_strength{}, shield{};
    int range{}, stun_duration{}, shield_duration{};
    // Flags describe the advertised observation family. A remote/outside-domain
    // row is unknown even if its tile is visible in the separately captured board.
    // alive/awake are authoritative only when known. An unknown sibling remains possible.
    bool visible{}, alive{}, awake{}, known{}, possible_alive{true}, buffs_known{}, stunned{};
};
struct Enemy {
    int handle{-1};
    cambc::vec2 pos{};
    float hp{}, max_hp{}, strength{}, focus{};
    int range{}, move_speed{};
    bool alive{}, visible{};
};
struct Group {
    int id{-1}, observed_round{-1}, cooldown{}, gems{}, gold{};
    GroupState state{GroupState::unknown};
    bool possibly_woken{};
    std::vector<std::size_t> members;
};
struct MemberMemory {
    float hp{}, strength{};
    int observed_round{-1};
    bool alive{}, awake{};
};
struct GroupMemory {
    GroupState state{GroupState::unknown};
    int observed_round{-1}, cooldown{}, ban_until{};
    std::vector<MemberMemory> members;
};

template<class Board> struct Snapshot {
    int epoch{-1}, actor{-1};
    std::uint64_t revision{};
    bool complete{}, captured_complete{}, layout_initialized{};
    bool layout_started{};
    std::size_t layout_group_cursor{}, layout_camp_cursor{};
    Self self;
    std::array<Ability,2> abilities;
    std::vector<Camp> camps;
    std::vector<Enemy> enemies;
    std::vector<Group> groups;
    std::vector<cambc::vec2> move_destinations, attack_targets;
    // Native collectors preserve SourceWorld's exact camp order/handle/count. A
    // token is not transferable to a manually reordered or unrelated packet.
    spatial::Token spatial_support;
    domain::ObservationDomain domain;
    Board visible, static_blocked, dynamic_blocked;

    void invalidate() { complete = false; }
    bool matches(int round, int owner, std::uint64_t version) const {
        return complete && epoch == round && actor == owner && revision == version;
    }
    const Camp* camp(int handle) const {
        const auto it = std::find_if(camps.begin(),camps.end(),[=](const Camp& c){return c.handle==handle;});
        return it==camps.end()?nullptr:&*it;
    }
    const Group* group(int id) const {
        if(id>=0 && static_cast<std::size_t>(id)<groups.size() && groups[static_cast<std::size_t>(id)].id==id)
            return &groups[static_cast<std::size_t>(id)];
        const auto it = std::find_if(groups.begin(),groups.end(),[=](const Group& g){return g.id==id;});
        return it==groups.end()?nullptr:&*it;
    }
};
using Frame = Snapshot<cambc::bitboard>;

struct Stamp {
    int epoch{-1}, actor{-1};
    std::uint64_t revision{};
    bool moved{}, attacked{}, cast_spent{};
};

struct CampDamageSupport {
    cambc::vec2 center{};
    int radius{};
    std::vector<int> groups;
    bool complete{};
};

// A deliberately narrow effect closure: one successful stationary camp-target hit,
// with exact radius zero or one and no champion in its footprint. This does not
// predict damage, death, wake, cooldown resets or rewards from projected effects.
template<class Board>
CampDamageSupport camp_damage_support(const Snapshot<Board>& prior,int target_handle,int radius) {
    CampDamageSupport support;
    if(!prior.captured_complete||radius<0||radius>1)return support;
    if(!prior.domain.matches({prior.epoch,prior.actor,prior.revision},prior.spatial_support,prior.camps.size(),
        mosaic::domain::geometry(prior.self,prior.abilities)))return support;
    const auto* target=prior.camp(target_handle);
    if(!target||!target->known||!target->visible||!target->alive)return support;
    support.center=target->pos;support.radius=radius;
    if(!prior.domain.covers_hits(support.center,radius))return support;
    const auto inside=[&](cambc::vec2 p){return std::max(std::abs(p.x-support.center.x),
        std::abs(p.y-support.center.y))<=radius;};
    for(const auto& enemy:prior.enemies)if(enemy.alive&&enemy.visible&&inside(enemy.pos))return support;
    for(const auto& camp:prior.camps)if(camp.known&&camp.visible&&camp.alive&&inside(camp.pos)) {
        if(!prior.group(camp.group))return support;
        if(std::find(support.groups.begin(),support.groups.end(),camp.group)==support.groups.end())
            support.groups.push_back(camp.group);
    }
    support.complete=!support.groups.empty();
    return support;
}

// Observer stages occur once each, independent of the number of candidate evaluations.
// An observer supplies a final epoch/actor fence; it must not silently skip a failed stage.
template<class Observer, class Board>
bool collect(Observer& observer, Snapshot<Board>& out, Stamp stamp) {
    out.invalidate();
    out.captured_complete=false;
    out.spatial_support={};
    out.domain={};
    out.epoch=stamp.epoch; out.actor=stamp.actor; out.revision=stamp.revision;
    if constexpr(requires{observer.reset_packet(out);})observer.reset_packet(out);
    else {
        out.self={}; out.abilities={}; out.camps.clear(); out.enemies.clear(); out.groups.clear();
        out.move_destinations.clear(); out.attack_targets.clear();
    }
    if(!observer.begin(stamp))return false;
    if constexpr(requires{observer.prepare_layout(out);})if(!observer.prepare_layout(out))return false;
    if(!observer.read_self(out)||!observer.read_abilities(out)||!observer.read_boards(out))return false;
    if constexpr(requires{observer.prepare_domain(out);})if(!observer.prepare_domain(out))return false;
    if(!observer.read_groups_and_camps(out)||!observer.read_enemies(out)||!observer.finish(out))return false;
    if(!out.domain.limited) {
        out.domain.stamp={stamp.epoch,stamp.actor,stamp.revision};
        out.domain.layout=out.spatial_support;out.domain.geometry=mosaic::domain::geometry(out.self,out.abilities);
    }
    if(!out.domain.matches({stamp.epoch,stamp.actor,stamp.revision},out.spatial_support,out.camps.size(),
        mosaic::domain::geometry(out.self,out.abilities)))return false;
    out.self.moved=stamp.moved; out.self.attacked=stamp.attacked; out.self.cast_spent=stamp.cast_spent;
    out.complete=out.captured_complete=true;
    return true;
}

struct SourceCamp {
    cambc::camp handle;
    Camp design;
};
struct SourceGroup {
    cambc::map::camp_group handle;
    Group design;
};
struct SourceWorld {
    // This object must keep its address while any complete Frame borrows its index.
    // A copied world's frames must be recaptured/rebound, not given the old token.
    spatial::Index spatial_support;
    std::vector<SourceCamp> camps;
    std::vector<SourceGroup> groups;
    std::vector<GroupMemory> memory;
    std::vector<GroupMemory> scratch_memory;
    std::unordered_map<int,std::size_t> camp_indices;
    std::vector<cambc::map::camp_group> pending;
    std::optional<cambc::bitboard> static_blocked;
    std::optional<cambc::champion_stats> self_design;
    std::unordered_map<int,int> enemy_speeds;
    std::array<std::optional<cambc::camp_stats>,6> camp_designs;
    std::unordered_map<int,std::pair<std::optional<cambc::item_passive>,float>> item_passives;
    std::vector<int> remembered_woken;
    std::vector<std::uint8_t> visibility_linear;
    bool loaded{}, initialized{};
    bool local_support{};
    void set_local_support(bool enabled){local_support=enabled;}
    bool memory_scratch_ready{};
    std::size_t memory_scratch_cursor{};
    bool spatial_model_accepted{}, spatial_started{}, spatial_built{}, spatial_unsupported{}, spatial_model_violated{}, spatial_violation_logged{};
    // Empirical entry admission only. Setup is now guarded canonical append and
    // bounded sorting batches, with no full temporary array or monolithic sort.
    // It owns separate inputs; these guards are not a numerical worst-case proof.
    static constexpr std::uint64_t spatial_initialization_admission=90000;
    void set_spatial_model(bool accept) {
        if(spatial_model_accepted==accept)return;
        spatial_model_accepted=accept;
        spatial_support.revoke();spatial_started=false;spatial_built=false;spatial_unsupported=false;
    }
    bool spatial_initialization_pending() const {
        return spatial_model_accepted&&initialized&&!spatial_built&&!spatial_unsupported&&!spatial_model_violated;
    }
    template<class Guard> bool prepare_spatial_support(Guard&& admitted) {
        if(!spatial_model_accepted||!initialized||spatial_unsupported||spatial_model_violated)return false;
        if(spatial_built)return spatial_support.valid();
        using State=spatial::BuildState;
        if(!spatial_started) {
            if(!admitted())return false;
            if(!spatial_support.begin_build(camps.size(),true)){spatial_unsupported=true;return false;}
            spatial_started=true;
        }
        // Canonical immutable layout only. No current visibility, HP or status
        // is collected or retained as a fresh fact while setup spans inputs.
        while(spatial_support.status()==State::collecting) {
            if(!admitted())return false;
            const auto i=spatial_support.input_count();
            if(i>=camps.size()){spatial_unsupported=true;spatial_support.revoke();return false;}
            const auto& camp=camps[i].design;
            if(!spatial_support.append({camp.handle,camp.spawn.x,camp.spawn.y,camp.range})) {
                spatial_unsupported=true;return false;
            }
        }
        // Explicit conditional pinned-AI-completeness model, enabled only by the
        // existing opt-in controller. Dynamic life/wake/HP/buffs remain observed.
        while(spatial_support.status()==State::building) {
            if(!admitted())return false;
            spatial_support.advance_build(32);
        }
        spatial_built=spatial_support.status()==State::ready;
        spatial_unsupported=spatial_support.status()==State::failed;
        return spatial_built;
    }
    // False means yield this input without a dynamic capture. Even a completed
    // build has its own phase; only a later input may proceed to observation.
    template<class Guard> bool advance_spatial_initialization(std::uint64_t remaining,Guard&& admitted) {
        if(!initialized)return false;
        if(!spatial_initialization_pending())return true;
        if(remaining<=spatial_initialization_admission)return false;
        prepare_spatial_support(admitted);
        return false;
    }
    bool observe_spatial_position(std::size_t index,int handle,cambc::vec2 position,int range) {
        if(!spatial_support.valid())return false;
        if(spatial_support.observe(index,handle,position.x,position.y,range))return false;
        spatial_model_violated=true;
        return true;
    }
    bool observe_spatial(std::size_t index,const Camp& camp) {
        return observe_spatial_position(index,camp.handle,camp.pos,camp.range);
    }
    // Cold storage copies only prior committed, explicitly dated memory. It never
    // reads live facts and never publishes a partial current-epoch packet.
    template<class Guard> bool advance_memory_scratch(Guard&& admitted) {
        if(!initialized)return false;
        if(memory_scratch_ready)return true;
        if(!admitted())return false;
        if(memory_scratch_cursor==0&&scratch_memory.empty())scratch_memory.reserve(memory.size());
        while(memory_scratch_cursor<memory.size()) {
            if(!admitted())return false;
            scratch_memory.push_back(memory[memory_scratch_cursor]);
            ++memory_scratch_cursor;
        }
        memory_scratch_ready=true;
        return false;
    }
    void remember_wake(int group) {
        if(group>=0 && std::find(remembered_woken.begin(),remembered_woken.end(),group)==remembered_woken.end())
            remembered_woken.push_back(group);
    }
};

// Static packet allocation/copy is its own monotone startup phase. False always
// forbids dynamic collection on the same input, including a successful setup.
template<class Board,class Groups,class Camps,class Guard>
bool advance_packet_layout_guarded(Snapshot<Board>& out,const Board& static_blocked,
                                   const Groups& groups,const Camps& camps,Guard&& admitted) {
    if(out.layout_initialized&&out.groups.size()==groups.size()&&out.camps.size()==camps.size())return true;
    if(out.layout_initialized)out.layout_started=false;
    out.invalidate();out.captured_complete=false;out.layout_initialized=false;
    if(!out.layout_started) {
        if(!admitted())return false;
        out.static_blocked=static_blocked;
        out.groups.clear();out.camps.clear();
        out.groups.reserve(groups.size());out.camps.reserve(camps.size());
        out.layout_group_cursor=out.layout_camp_cursor=0;out.layout_started=true;
    }
    while(out.layout_group_cursor<groups.size()) {
        if(!admitted())return false;
        out.groups.push_back(groups[out.layout_group_cursor].design);
        ++out.layout_group_cursor;
    }
    while(out.layout_camp_cursor<camps.size()) {
        if(out.layout_camp_cursor%16==0&&!admitted())return false;
        out.camps.push_back(camps[out.layout_camp_cursor].design);
        ++out.layout_camp_cursor;
    }
    out.layout_initialized=true;
    return false;
}

template<class Board,class Groups,class Camps>
bool advance_packet_layout(Snapshot<Board>& out,const Board& static_blocked,
                           const Groups& groups,const Camps& camps,bool admitted) {
    return advance_packet_layout_guarded(out,static_blocked,groups,camps,[=]{return admitted;});
}

template<class Board>
void export_linear_visibility(const Board& visible,std::vector<std::uint8_t>& bytes) {
    bytes.resize(Board::linear_buffer_size());
    visible.export_linear_buffer(std::span<std::uint8_t>{bytes});
}

// Enumerate only the already captured current occupancy mask. The reader returns
// a checked native tile identity, not a newly reconstructed camp metadata object.
template<class Board,class Reader,class Seen>
bool enumerate_visible_camp_indices(const Board& camp_tiles,const std::unordered_map<int,std::size_t>& indices,
                                    std::size_t count,std::vector<bool>& enumerated,Reader&& read_handle,Seen&& seen) {
    enumerated.assign(count,false);
    for(const auto position:camp_tiles.positions()) {
        const int handle=read_handle(position);
        const auto found=indices.find(handle);
        if(found==indices.end()||found->second>=count)return false;
        enumerated[found->second]=true;
        seen(found->second,handle,position);
    }
    return true;
}

template<class Board,class Reader>
bool enumerate_visible_camp_indices(const Board& camp_tiles,const std::unordered_map<int,std::size_t>& indices,
                                    std::size_t count,std::vector<bool>& enumerated,Reader&& read_handle) {
    return enumerate_visible_camp_indices(camp_tiles,indices,count,enumerated,read_handle,
        [](std::size_t,int,cambc::vec2){});
}

inline void restore_camp_memory(Camp& camp,const MemberMemory& memory) {
    camp.hp=memory.hp;camp.strength=memory.strength;camp.alive=memory.alive;camp.awake=memory.awake;
    camp.observed_round=memory.observed_round;camp.pos=camp.spawn;
    camp.known=camp.visible=camp.buffs_known=camp.stunned=false;camp.possible_alive=true;
    camp.stun_duration=0;camp.shield=0;camp.shield_duration=0;
}

inline bool linear_visible(std::span<const std::uint8_t> bytes,int width,int height,cambc::vec2 p) {
    if(width<=0||height<=0||p.x<0||p.y<0||p.x>=width||p.y>=height)return false;
    const auto tile=static_cast<std::size_t>(p.y)*static_cast<std::size_t>(width)+static_cast<std::size_t>(p.x);
    // Pinned packed row-major LSB layout checked against native Managed bitboards
    // and the actual guest SDK export/is_set/vision wrapper, including odd widths.
    return tile/8<bytes.size() && (bytes[tile/8]&(std::uint8_t{1}<<(tile%8)))!=0;
}

// Native collection is below the data-only contract. Its guards are empirical admission
// checks, not certified worst-case fuel bounds. No candidate evaluator calls these methods.
class NativeObserver {
    SourceWorld& world_;
    Stamp stamp_;
    std::span<const int> woken_;
    std::vector<GroupMemory>& staged_memory_;
    std::vector<bool> spawn_visible_;
    std::vector<bool> enumerated_camps_;
    std::vector<cambc::champion> visible_enemies_;

    bool current(std::uint64_t tail=14000) const {
        return int(cambc::round_number())==stamp_.epoch &&
            int(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(cambc::self())))==stamp_.actor &&
            cambc::fuel_remaining()>tail;
    }
    native_observation::Stamp observation_stamp() const {
        return {stamp_.epoch,stamp_.actor,stamp_.revision};
    }
    void observe_spatial_tile(std::size_t index,int handle,cambc::vec2 position) {
        if(world_.observe_spatial_position(index,handle,position,world_.camps[index].design.range)&&
           !world_.spatial_violation_logged) {
            world_.spatial_violation_logged=true;std::puts("error spatial model violation");
        }
    }
    bool initialize() {
        using namespace cambc;
        if(world_.initialized) {
            if(!current())return false;
            return world_.advance_spatial_initialization(fuel_remaining(),[&]{return current(45000);});
        }
        if(!current(55000))return false;
        if(!world_.loaded) {
            world_.pending=map::camp_groups();
            world_.static_blocked=map::walls()|map::holes()|map::altars()|map::spawn_region(team::enemy);
            world_.self_design=self().base_stats();
            world_.loaded=true;
        }
        // Only immutable metadata is retained across an incomplete initialization.
        while(world_.groups.size()<world_.pending.size()) {
            if(!current(45000))return false;
            const auto index=world_.groups.size();
            const auto& source=world_.pending[index];
            Group group;
            group.id=static_cast<int>(index); group.gems=static_cast<int>(source.gem_value());
            group.gold=static_cast<int>(source.gold_value());
            GroupMemory memory;
            int member=0;
            for(const auto& c:source.camps()) {
                const auto kind=static_cast<std::size_t>(c.kind());
                if(kind>=world_.camp_designs.size())return false;
                if(!world_.camp_designs[kind])world_.camp_designs[kind]=c.base_stats();
                const auto stats=*world_.camp_designs[kind];
                Camp design;
                design.handle=int(static_cast<std::uint32_t>(static_cast<handle_camp_t>(c)));
                design.group=group.id; design.member=member++;
                design.pos=design.spawn=c.spawn_position();
                design.hp=design.design_hp=stats.base_health;
                design.strength=design.design_strength=stats.base_strength;
                design.range=stats.base_attack_range;
                group.members.push_back(world_.camps.size());
                world_.camp_indices.emplace(design.handle,world_.camps.size());
                world_.camps.push_back({c,design});
                memory.members.push_back({design.hp,design.strength,-1,false,false});
            }
            world_.groups.push_back({source,std::move(group)});
            world_.memory.push_back(std::move(memory));
        }
        world_.initialized=true;
        // Metadata completion is monotone. Do not stack the independent index
        // stage on this prefix, even on the larger first gameplay allowance.
        return !world_.spatial_initialization_pending()&&current();
    }

public:
    NativeObserver(SourceWorld& world,std::span<const int> woken):world_(world),woken_(woken),staged_memory_(world.scratch_memory){}
    void reset_packet(Frame& out) const {
        out.invalidate();out.captured_complete=false;
        out.self={};
        for(auto& ability:out.abilities) {
            auto targets=std::move(ability.legal_targets);
            targets.clear();ability={};ability.legal_targets=std::move(targets);
        }
        // Immutable group member-index vectors and physical camp identity rows stay
        // allocated. Every dynamic field is overwritten before complete publication.
        out.enemies.clear();out.move_destinations.clear();out.attack_targets.clear();
    }
    bool begin(Stamp stamp) {
        stamp_=stamp;
        if(stamp.epoch<0||stamp.actor<0||!current(52000)||!initialize())return false;
        if(!current(52000))return false;
        if(!world_.advance_memory_scratch([&]{return current(45000);}))return false;
        staged_memory_=world_.memory;
        return current(45000);
    }
    bool prepare_layout(Frame& out) {
        if(out.layout_initialized&&out.groups.size()==world_.groups.size()&&out.camps.size()==world_.camps.size())return true;
        // Empirical setup admission; no numerical worst-case claim. This phase
        // retains its buffers but always yields before live collection.
        if(!current(80000))return false;
        world_.visibility_linear.resize(cambc::bitboard::linear_buffer_size());
        return advance_packet_layout_guarded(out,*world_.static_blocked,world_.groups,world_.camps,
            [&]{return current(45000);});
    }
    bool begin_self_revision(Stamp stamp) {
        stamp_=stamp;
        return world_.initialized && world_.self_design.has_value() && current(25000);
    }
    bool begin_stationary_revision(Stamp stamp,const CampDamageSupport& support) {
        stamp_=stamp;
        if(!support.complete||!world_.initialized||!world_.self_design||!current(30000))return false;
        staged_memory_=world_.memory;
        for(int group:support.groups)world_.remember_wake(group);
        return current(25000);
    }
    bool finish_self_revision(Frame& out) const {
        out.spatial_support=world_.spatial_support.borrow();
        return current();
    }
    bool read_self(Frame& out) {
        using namespace cambc;
        auto& me=self();
        auto& s=out.self;
        s.alive=me.is_alive();
        if(!s.alive)return current();
        const auto record=native_observation::read_champion(me,observation_stamp());
        const auto state=record.state();
        s.pos=record.position(); s.hp=record.health(); s.max_hp=record.max_health();
        s.strength=record.strength(); s.focus=record.focus(); s.range=record.attack_range();
        s.gold=static_cast<int>(me.gold_count()); s.gems=static_cast<int>(me.gem_count());
        s.move_speed=record.move_speed();
        s.camp_multiplier=world_.self_design->camp_damage_multiplier;
        s.passive=me.passive();
        s.can_move=state.contains(champion_state::can_move); s.can_attack=state.contains(champion_state::can_attack);
        s.can_cast=state.contains(champion_state::can_cast); s.recalling=state.contains(champion_state::recalling);
        s.submitting=state.contains(champion_state::submitting_gems); s.teleporting=state.contains(champion_state::teleporting);
        const auto buffs=me.buffs(); s.stunned=buffs.has_stun();
        s.enhanced_attack=buffs.has_enhanced_attack();
        if(buffs.has_shield()){const auto shield=buffs.get<buff_shield>();s.shield=shield.value;s.shield_duration=shield.duration;}
        s.splash_radius=s.passive==passive_type::splash_attack?
            static_cast<int>(constants::passive_abilities::varg_splash_radius):0;
        const auto items=me.item_count();
        float monster=0.0f,jungle=0.0f,durability=0.0f;
        for(int i=0;i<int(item_type::count);++i) {
            const item_type item{static_cast<item_type::enum_type>(i)};
            if(items[item]==0)continue;
            if(world_.item_passives.find(i)==world_.item_passives.end()) {
                const auto info=item.info();
                world_.item_passives.emplace(i,std::pair{info.passive,info.passive_magnitude});
            }
            const auto [passive,magnitude]=world_.item_passives.at(i);
            if(passive==item_passive::monster)monster=std::max(monster,magnitude);
            if(passive==item_passive::jungle)jungle=std::max(jungle,magnitude);
            if(passive==item_passive::durability)durability=std::max(durability,magnitude);
        }
        s.camp_multiplier*=1.0f+monster;
        s.camp_taken_multiplier=(1.0f-jungle)*(1.0f-durability);
        s.champion_taken_multiplier=1.0f-durability;
        s.basic_damage=s.strength*s.camp_multiplier;
        s.basic_damage_known=!s.enhanced_attack;
        s.damage_modifiers_known=true;
        out.attack_targets=record.attack_tiles().positions();
        return current();
    }
    bool read_boards(Frame& out) {
        using namespace cambc;
        out.visible=vision::visible_tiles();
        // One real SDK export replaces one native predicate call per static member;
        // every spawn is still represented and both group/member gates reuse it.
        export_linear_visibility(out.visible,world_.visibility_linear);
        spawn_visible_.resize(world_.camps.size());
        for(std::size_t ci=0;ci<world_.camps.size();++ci) {
            if(ci%16==0&&!current(20000))return false;
            spawn_visible_[ci]=linear_visible(world_.visibility_linear,out.visible.width(),out.visible.height(),world_.camps[ci].design.spawn);
        }
        const auto camp_tiles=vision::camp_tiles();
        out.dynamic_blocked=vision::champion_tiles()|camp_tiles|vision::teleport_tiles();
        visible_enemies_=vision::champions(team::enemy);
        if(!enumerate_visible_camp_indices(camp_tiles,world_.camp_indices,world_.camps.size(),enumerated_camps_,
            [](vec2 position){return int(static_cast<std::uint32_t>(native_observation::camp_handle_at(position)));},
            [&](std::size_t index,int handle,vec2 position){observe_spatial_tile(index,handle,position);}))return false;
        if(out.self.alive) {
            auto blocked=out.static_blocked|out.dynamic_blocked;
            blocked.set(out.self.pos,false);
            out.move_destinations=blocked.reachable_tiles(out.self.pos,static_cast<std::uint16_t>(out.self.move_speed)).positions();
        }
        return current();
    }
    bool prepare_domain(Frame& out) {
        out.spatial_support=world_.spatial_support.borrow();
        const mosaic::domain::Stamp stamp{stamp_.epoch,stamp_.actor,stamp_.revision};
        const auto geometry=mosaic::domain::geometry(out.self,out.abilities);
        out.domain={};out.domain.stamp=stamp;out.domain.layout=out.spatial_support;out.domain.geometry=geometry;
        if(world_.local_support&&out.self.alive) {
            auto limited=mosaic::domain::make_limited(stamp,out.spatial_support,out.camps.size(),geometry);
            if(limited.complete)out.domain=limited;
            // Invalid layout/geometry chooses the full scan BEFORE any member read.
        }
        return current();
    }
    bool read_stationary_boards(Frame& out,const cambc::bitboard& prior_visible,const CampDamageSupport& support) {
        using namespace cambc;
        auto visible=vision::visible_tiles();
        if(!(visible==prior_visible))return false;
        export_linear_visibility(visible,world_.visibility_linear);
        // A hidden actor cannot be dismissed as unaffected merely because its old
        // position was outside the footprint. Require the entire footprint visible.
        for(int y=support.center.y-support.radius;y<=support.center.y+support.radius;++y)
            for(int x=support.center.x-support.radius;x<=support.center.x+support.radius;++x)
                if(x>=0&&y>=0&&x<visible.width()&&y<visible.height()&&
                   !linear_visible(world_.visibility_linear,visible.width(),visible.height(),{x,y}))return false;
        out.visible=std::move(visible);
        spawn_visible_.assign(world_.camps.size(),false);
        for(int group:support.groups) {
            if(group<0||static_cast<std::size_t>(group)>=world_.groups.size()||!current(18000))return false;
            for(auto ci:world_.groups[static_cast<std::size_t>(group)].design.members)
                spawn_visible_[ci]=linear_visible(world_.visibility_linear,out.visible.width(),out.visible.height(),world_.camps[ci].design.spawn);
        }
        const auto camp_tiles=vision::camp_tiles();
        out.dynamic_blocked=vision::champion_tiles()|camp_tiles|vision::teleport_tiles();
        if(!enumerate_visible_camp_indices(camp_tiles,world_.camp_indices,world_.camps.size(),enumerated_camps_,
            [](vec2 position){return int(static_cast<std::uint32_t>(native_observation::camp_handle_at(position)));},
            [&](std::size_t index,int handle,vec2 position){observe_spatial_tile(index,handle,position);}))return false;
        if(out.domain.limited&&!out.domain.layout.valid(out.camps.size()))return false;
        auto blocked=out.static_blocked|out.dynamic_blocked;
        blocked.set(out.self.pos,false);
        out.move_destinations=blocked.reachable_tiles(out.self.pos,static_cast<std::uint16_t>(out.self.move_speed)).positions();
        return current();
    }
    bool read_stationary_groups(Frame& out,const CampDamageSupport& support) {
        return read_groups_and_camps(out,support.groups);
    }
    bool read_groups_and_camps(Frame& out,std::span<const int> selected={}) {
        using namespace cambc;
        const auto includes=[&](int id){return selected.empty()||std::find(selected.begin(),selected.end(),id)!=selected.end();};
        std::size_t selected_groups=0;
        for(std::size_t i=0;i<world_.groups.size();++i) {
            if(!includes(static_cast<int>(i)))continue;
            if(selected_groups++%8==0&&!current(20000))return false;
            const auto& source=world_.groups[i];
            auto& group=out.groups[i];
            auto& memory=staged_memory_[i];
            group.state=memory.state; group.observed_round=-1; group.cooldown=memory.cooldown;
            group.possibly_woken=std::find(woken_.begin(),woken_.end(),group.id)!=woken_.end() ||
                std::find(world_.remembered_woken.begin(),world_.remembered_woken.end(),group.id)!=world_.remembered_woken.end();
            bool in_vision=false;
            for(auto ci:group.members)if(spawn_visible_[ci])in_vision=true;
            if(in_vision) {
                const auto stamp=observation_stamp();
                const auto state=native_observation::read_group(source.handle,stamp,
                    {stamp,static_cast<handle_camp_group_t>(source.handle),in_vision}).value;
                group.observed_round=stamp_.epoch;
                if(std::holds_alternative<map::camp_group_reward_available>(state))group.state=GroupState::reward;
                else if(const auto* s=std::get_if<map::camp_group_respawning>(&state)){group.state=GroupState::respawning;group.cooldown=s->timer;}
                else group.state=GroupState::alive;
                memory.state=group.state; memory.observed_round=group.observed_round; memory.cooldown=group.cooldown;
            }
        }
        std::size_t selected_members=0;
        for(std::size_t ci=0;ci<world_.camps.size();++ci) {
            if(!includes(world_.camps[ci].design.group))continue;
            if(selected_members++%16==0&&!current(20000))return false;
            const auto& source=world_.camps[ci];
            auto& camp=out.camps[ci];
            auto& memory=staged_memory_[static_cast<std::size_t>(camp.group)].members[static_cast<std::size_t>(camp.member)];
            restore_camp_memory(camp,memory);
            if(!out.domain.contains(ci))continue;
            const bool enumerated=enumerated_camps_[ci];
            if(enumerated || spawn_visible_[ci]) {
                const auto state=source.handle.state();
                camp.known=true;camp.visible=enumerated;camp.alive=state.is_alive();camp.awake=state.is_awake();
                camp.pos=camp.spawn;camp.hp=0.0f;camp.strength=source.design.strength;
                if(camp.alive) {
                    const auto record=native_observation::read_camp(source.handle,observation_stamp());
                    camp.pos=record.position();camp.hp=record.health();camp.strength=record.strength();
                    if(world_.observe_spatial(ci,camp)&&!world_.spatial_violation_logged) {
                        world_.spatial_violation_logged=true;
                        std::puts("error spatial model violation");
                    }
                    if(out.domain.limited&&!out.domain.layout.valid(out.camps.size()))return false;
                    const auto buffs=source.handle.buffs();
                    camp.buffs_known=true;camp.stunned=buffs.has_stun();
                    if(camp.stunned)camp.stun_duration=buffs.get<buff_stun>().duration;
                    if(buffs.has_shield()) {
                        const auto shield=buffs.get<buff_shield>();
                        camp.shield=shield.value;camp.shield_duration=shield.duration;
                    }
                }
                camp.possible_alive=camp.alive;
                camp.observed_round=stamp_.epoch;
                memory={camp.hp,camp.strength,stamp_.epoch,camp.alive,camp.awake};
            } else {
                // Old zero HP or a deadline cannot prove a hidden member remains dead.
                camp.known=false;camp.visible=false;camp.possible_alive=true;
            }
        }
        return current(20000);
    }
    bool read_enemies(Frame& out) {
        using namespace cambc;
        out.enemies.reserve(visible_enemies_.size());
        for(const auto& c:visible_enemies_) {
            if(!current())return false;
            Enemy enemy;
            enemy.handle=int(static_cast<std::uint32_t>(static_cast<handle_champion_t>(c)));
            enemy.visible=true;enemy.alive=c.is_alive();
            if(enemy.alive) {
                const auto record=native_observation::read_champion(c,observation_stamp());
                enemy.pos=record.position();enemy.hp=record.health();enemy.max_hp=record.max_health();
                enemy.strength=record.strength();enemy.focus=record.focus();enemy.range=record.attack_range();
                const auto it=world_.enemy_speeds.find(enemy.handle);
                if(it==world_.enemy_speeds.end())world_.enemy_speeds.emplace(enemy.handle,c.base_stats().base_move_speed);
                enemy.move_speed=world_.enemy_speeds.at(enemy.handle);
            }
            out.enemies.push_back(enemy);
        }
        return current();
    }
    bool read_abilities(Frame& out) {
        using namespace cambc;
        if(!out.self.alive)return current();
        for(const auto& source:self().abilities()) {
            if(!current())return false;
            auto& a=out.abilities[source.slot()];a.slot=static_cast<int>(source.slot());
            const auto& info=source.info();if(!info)continue;
            const auto targets=source.valid_targets();
            a.camps=targets.contains(ability_target_mask::camps);a.enemies=targets.contains(ability_target_mask::enemies);
            a.allies=targets.contains(ability_target_mask::allies);a.self=targets.contains(ability_target_mask::self);
            a.ground=targets.contains(ability_target_mask::ground);
            a.reset=source.is_attack_reset();a.dash=source.is_dash();a.heal=source.is_heal();a.shield=source.is_shield();
            a.damage=source.is_damage();a.aoe=source.is_aoe_damage();a.cc=source.is_cc();
            std::visit([&](const auto& data){
                a.exclusive=data.exclusive_with_movement;a.max_cooldown=data.max_cooldown;
                if constexpr(requires{data.range;})a.range=data.range;
                if constexpr(requires{data.radius;})a.radius=data.radius;
                if constexpr(requires{data.duration;})a.duration=data.duration;
                if constexpr(requires{data.enhanced_attack;})a.enhanced=data.enhanced_attack;
            },*info);
            a.cooldown=source.cooldown();a.ready=a.cooldown==0 && out.self.can_cast;
            if(a.damage||a.aoe||a.cc||a.dash)a.damage_amount=source.damage_amount();
            if(a.shield)a.shield_amount=source.shield_amount();
            if(a.heal)a.heal_amount=source.heal_amount();
            if(!a.reset)a.legal_targets=source.valid_tiles().positions();
        }
        return current();
    }
    bool finish(Frame& out) {
        if(!current())return false;
        const auto token=world_.spatial_support.borrow();
        if(!out.domain.matches({stamp_.epoch,stamp_.actor,stamp_.revision},token,out.camps.size(),
            mosaic::domain::geometry(out.self,out.abilities)))return false;
        for(auto& group:out.groups) {
            bool resolved=group.observed_round==stamp_.epoch &&
                (group.state==GroupState::reward||group.state==GroupState::respawning);
            if(!resolved)resolved=std::all_of(group.members.begin(),group.members.end(),[&](std::size_t member){
                const auto& camp=out.camps[member];
                return camp.known && (!camp.alive||!camp.awake);
            });
            if(resolved) {
                group.possibly_woken=false;
                auto& remembered=world_.remembered_woken;
                remembered.erase(std::remove(remembered.begin(),remembered.end(),group.id),remembered.end());
            }
        }
        if(!current())return false;
        world_.memory.swap(staged_memory_);
        out.spatial_support=world_.spatial_support.borrow();
        return current();
    }
};

inline bool capture(SourceWorld& world,Frame& out,int epoch,int actor,std::uint64_t revision,
                    bool moved=false,bool attacked=false,bool cast_spent=false,
                    std::span<const int> remembered_woken_groups={}) {
    NativeObserver observer(world,remembered_woken_groups);
    return collect(observer,out,{epoch,actor,revision,moved,attacked,cast_spent});
}

// PRECONDITION: the only applied primitive since prior's revision was an attack reset,
// self-only shield, or purchase proved to affect no other entity, visibility or geometry.
// Ordinary movement, damage, AoE, reveal, refusal or uncertain exception requires capture().
// Invalidation revokes prior's authority but preserves its coherent factual packet for this
// explicitly restricted refresh. A failed or interrupted capture never sets captured_complete.
template<class Observer,class Board>
bool refresh_self_packet(Observer& observer,const Snapshot<Board>& prior,Snapshot<Board>& out,Stamp stamp) {
    if(!prior.captured_complete || prior.epoch!=stamp.epoch || prior.actor!=stamp.actor ||
       stamp.revision<=prior.revision) {out.invalidate();return false;}
    if(!observer.begin_self_revision(stamp)){out.invalidate();return false;}
    const auto prior_position=prior.self.pos;
    const int prior_speed=prior.self.move_speed;
    const auto prior_geometry=mosaic::domain::geometry(prior.self,prior.abilities);
    if(&prior!=&out)out=prior;
    out.invalidate();out.captured_complete=false;
    out.epoch=stamp.epoch;out.actor=stamp.actor;out.revision=stamp.revision;
    out.self={};out.abilities={};
    if(!observer.read_self(out)||out.self.pos!=prior_position||out.self.move_speed!=prior_speed||
       !observer.read_abilities(out)||!observer.finish_self_revision(out))return false;
    const auto actual_geometry=mosaic::domain::geometry(out.self,out.abilities);
    if(!mosaic::domain::same_geometry(prior_geometry,actual_geometry)||
       !out.domain.rebind({stamp.epoch,stamp.actor,stamp.revision},out.spatial_support,out.camps.size(),actual_geometry))return false;
    out.self.moved=stamp.moved;out.self.attacked=stamp.attacked;out.self.cast_spent=stamp.cast_spent;
    out.complete=out.captured_complete=true;
    return true;
}

inline bool refresh_self_revision(SourceWorld& world,const Frame& prior,Frame& out,int epoch,int actor,
                                  std::uint64_t revision,bool moved=false,bool attacked=false,bool cast_spent=false) {
    NativeObserver observer(world,{});
    return refresh_self_packet(observer,prior,out,{epoch,actor,revision,moved,attacked,cast_spent});
}

// PRECONDITION: exactly one known successful stationary CAMP-target primitive since
// prior, whose complete actual effect footprint has the supplied radius (0 or 1).
// Supported examples are ordinary camp basics, Varg camp splash, Marcella camp damage,
// and Ryth camp AoE. Movement/dash, PvP, refusal, channels and uncertain effects are not
// supported. Failure revokes authority; callers may yield rather than force a costly
// full capture. No forecast HP is ever promoted to observation.
template<class Observer,class Board>
bool refresh_stationary_packet(Observer& observer,const Snapshot<Board>& prior,Snapshot<Board>& out,
                               Stamp stamp,int target_handle,int radius) {
    const auto support=camp_damage_support(prior,target_handle,radius);
    if(!support.complete||prior.epoch!=stamp.epoch||prior.actor!=stamp.actor||stamp.revision<=prior.revision) {
        out.invalidate();out.captured_complete=false;return false;
    }
    const auto old_self=prior.self;
    const auto old_geometry=mosaic::domain::geometry(prior.self,prior.abilities);
    if(!observer.begin_stationary_revision(stamp,support)){out.invalidate();out.captured_complete=false;return false;}
    const auto old_visible=prior.visible;
    if(&prior!=&out)out=prior;
    out.invalidate();out.captured_complete=false;
    out.epoch=stamp.epoch;out.actor=stamp.actor;out.revision=stamp.revision;
    out.self={};out.abilities={};out.move_destinations.clear();out.attack_targets.clear();
    if(!observer.read_self(out)||!out.self.alive||out.self.pos!=old_self.pos||
       out.self.range!=old_self.range||out.self.move_speed!=old_self.move_speed||out.self.passive!=old_self.passive||
       !observer.read_abilities(out))return false;
    const auto actual_geometry=mosaic::domain::geometry(out.self,out.abilities);
    if(!mosaic::domain::same_geometry(old_geometry,actual_geometry)||
       !out.domain.rebind({stamp.epoch,stamp.actor,stamp.revision},out.spatial_support,out.camps.size(),actual_geometry))return false;
    if(!observer.read_stationary_boards(out,old_visible,support)||
       !observer.read_stationary_groups(out,support)||!observer.finish(out)||
       !out.domain.matches({stamp.epoch,stamp.actor,stamp.revision},out.spatial_support,out.camps.size(),actual_geometry))return false;
    if(!out.domain.limited)out.domain.layout=out.spatial_support;
    out.self.moved=stamp.moved;out.self.attacked=stamp.attacked;out.self.cast_spent=stamp.cast_spent;
    out.complete=out.captured_complete=true;
    return true;
}

inline bool refresh_stationary_camp_damage(SourceWorld& world,const Frame& prior,Frame& out,int epoch,int actor,
                                          std::uint64_t revision,int target_handle,int radius,
                                          bool moved=false,bool attacked=false,bool cast_spent=false) {
    NativeObserver observer(world,{});
    return refresh_stationary_packet(observer,prior,out,{epoch,actor,revision,moved,attacked,cast_spent},target_handle,radius);
}

} // namespace mosaic::frame
