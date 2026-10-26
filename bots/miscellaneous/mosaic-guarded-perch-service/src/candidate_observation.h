#pragma once
// Selected-command observation. The catalog/index are immutable registration;
// rows are a sparse, owner-input-local overlay. No partial overlay is resumed as
// current on a later input, and collection readiness is not action admissibility.
#include "selected_transition.h"
#include <array>
#include <limits>

namespace mosaic::candidate {
using Stamp=selected::Stamp;
using Request=selected::Request;
using Packet=selected::Packet;
enum class Status {ready,incomplete,stale,unsupported};
struct History {bool moved{},attacked{},cast_spent{};};
struct Proposal {
    transition::Command command;
    std::uint64_t serial{};
    int nominated_epoch{-1};
    // A fresh Request remains only a request; it grants no observation authority.
    Request request(Stamp stamp) const {
        auto current=command;current.expected_epoch=stamp.epoch;current.expected_actor=stamp.actor;
        current.expected_revision=stamp.revision;return {current,serial};
    }
};
struct Work {
    std::size_t rows{},state_reads{},record_reads{},buff_reads{},enemy_records{},group_reads{},visibility_tests{};
};
struct Service {
    const frame::SourceWorld* owner{};
    bool registered{};
    std::size_t lifecycle_cursor{};
    std::uint64_t nonce{};
    Work work;
};
inline bool request_stamp(Stamp stamp,const Request& request) {
    return stamp.matches({request.command.expected_epoch,request.command.expected_actor,request.command.expected_revision});
}
inline void reset_packet(Packet& out,Stamp stamp,const Request& request,std::uint64_t nonce,
                         std::span<const frame::SourceCamp> catalog,spatial::Token layout) {
    out.complete=out.self_complete=out.enemies_complete=out.move_test_complete=out.move_legal=false;
    out.origin_complete=false;out.origin_stamp={};
    out.effect_champions_clear=false;
    out.rows.clear();out.groups.clear();out.enemies.clear();out.self={};
    out.stamp=stamp;out.request=request;out.nonce=nonce;out.catalog=catalog;out.layout=layout;
}

// Observer double seam: every role is assigned BEFORE a row is read. Origin
// threats are read first so an incomplete action packet can still price the
// actual wait caused by yielding, when that independent subset is complete.
template<class Observer>
Status collect(Observer& source,Packet& out,Stamp stamp,const Request& request,
               std::span<const frame::SourceCamp> catalog,spatial::Token layout,
               std::uint64_t nonce,History history={}) {
    reset_packet(out,stamp,request,nonce,catalog,layout);
    const auto stopped=[&](Status status=Status::incomplete) {
        if(!source.same_input()) {
            out.origin_complete=out.self_complete=out.enemies_complete=false;
            return Status::stale;
        }
        return status;
    };
    if(!request_stamp(stamp,request))return Status::stale;
    if(!selected::supported_kind(request.command.kind))return Status::unsupported;
    if(!layout.valid(catalog.size())||catalog.size()>spatial::capacity)return Status::unsupported;
    if(!source.begin(stamp)||!source.read_self(out.self))return stopped();
    out.self.moved=history.moved;out.self.attacked=history.attacked;out.self.cast_spent=history.cast_spent;
    out.self_complete=true;
    if(!source.read_enemies(out))return stopped();
    out.enemies_complete=true;
    if(!source.fence(25000))return stopped();
    if(!layout.valid(catalog.size()))return Status::stale;

    const auto& command=request.command;
    const auto origin=out.self.pos;
    const auto after=command.kind==transition::Kind::move?command.destination:origin;
    const auto origin_support=layout.index->threats(origin.x,origin.y);
    const auto after_support=layout.index->threats(after.x,after.y);
    if(!origin_support.complete||!after_support.complete)return stopped(Status::unsupported);
    spatial::Selection effects;
    int primary=-1,radius=0;
    if(command.kind==transition::Kind::basic) {
        if(command.target_kind!=transition::TargetKind::camp)return stopped(Status::unsupported);
        if(command.target_index>=0&&std::size_t(command.target_index)<catalog.size()&&
           catalog[std::size_t(command.target_index)].design.handle==command.target_id)primary=command.target_index;
        else for(std::size_t i=0;i<catalog.size();++i)if(catalog[i].design.handle==command.target_id){primary=int(i);break;}
        if(primary<0)return stopped(Status::unsupported);
        radius=out.self.splash_radius;
        if(radius<0||radius>1)return stopped(Status::unsupported);
        const auto center=catalog[std::size_t(primary)].design.spawn;
        effects=layout.index->hits(center.x,center.y,radius);
        if(!effects.complete)return stopped(Status::unsupported);
        out.effect_champions_clear=source.effect_clear(center,radius);
    }

    std::array<int,spatial::capacity> slots;slots.fill(-1);
    const auto add=[&](std::size_t index,int role) {
        if(index>=catalog.size())return false;
        if(slots[index]<0) {
            selected::Row row;row.index=index;row.camp=catalog[index].design;row.stamp=stamp;
            source.seed(index,row.camp);
            slots[index]=int(out.rows.size());out.rows.push_back(row);
        }
        auto& row=out.rows[std::size_t(slots[index])];
        if(role==0)row.retaliation_origin=true;
        if(role==1)row.effect=true;
        if(role==2)row.retaliation_after=true;
        return true;
    };
    for(auto i:origin_support)if(!add(i,0))return stopped(Status::unsupported);
    for(auto i:effects)if(!add(i,1))return stopped(Status::unsupported);
    for(auto i:after_support)if(!add(i,2))return stopped(Status::unsupported);
    for(const auto& row:out.rows)if(!selected::group(out,row.camp.group)) {
        frame::Group group;
        source.group_fact(row.camp.group,group);out.groups.push_back(std::move(group));
    }
    if(origin_support.size()==0) {
        if(!source.fence(20000))return stopped();
        out.origin_stamp=stamp;out.origin_complete=true;
    }
    std::size_t finished=0;
    for(auto& row:out.rows) {
        if(!source.fence(25000))return stopped();
        auto staged=row;
        bool read=source.read_body(staged);
        std::optional<frame::Group> staged_group;
        if(read&&staged.state_fresh&&!staged.camp.alive&&(staged.retaliation_origin||staged.retaliation_after)) {
            const auto* prior=selected::group(out,staged.camp.group);
            if(prior&&prior->observed_round!=stamp.epoch) {
                if constexpr(requires{source.refresh_dead_group(staged,*staged_group);}) {
                    staged_group=*prior;read=source.refresh_dead_group(staged,*staged_group);
                }
            }
        }
        const bool admitted=source.fence(20000);
        if(!admitted&&!source.same_input())return stopped();
        if(!read)return stopped();
        if constexpr(requires{source.validate_body(staged);})source.validate_body(staged);
        if(!layout.valid(catalog.size()))return Status::stale;
        row=staged;
        if(staged_group)for(auto& group:out.groups)if(group.id==staged_group->id){group=std::move(*staged_group);break;}
        if(++finished==origin_support.size()){out.origin_stamp=stamp;out.origin_complete=true;}
        if(!admitted)return stopped();
    }
    if(command.kind==transition::Kind::move) {
        if(!source.move_test(out.self,command.destination,out.move_legal))return stopped();
        out.move_test_complete=true;
    }
    if(!source.fence(20000))return stopped();
    if(!layout.valid(catalog.size()))return Status::stale;
    // Committing selected dated memory is after the whole one-input fence; a
    // failed partial read never contaminates the next input's dated base.
    source.commit(out);
    if(!source.fence(20000))return stopped();
    out.complete=true;
    return Status::ready;
}

inline bool current(Stamp stamp,std::uint64_t tail) {
    return cambc::fuel_remaining()>tail&&
        int(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(cambc::self())))==stamp.actor&&
        int(cambc::round_number())==stamp.epoch;
}

// Startup only. Reuses the existing monotone metadata/index/scratch phases;
// ordinary capture never calls NativeObserver::begin or clones world.memory.
inline Status advance_registration(frame::SourceWorld& world,Service& service,Packet& out,Stamp stamp) {
    out.complete=false;
    if(service.owner&&service.owner!=&world)return Status::unsupported;
    service.owner=&world;
    if(service.registered)return world.spatial_support.valid()?Status::ready:Status::unsupported;
    frame::NativeObserver registration(world,{});
    if(!registration.begin({stamp.epoch,stamp.actor,stamp.revision}))return Status::incomplete;
    if(!world.initialized||!world.spatial_support.valid()||!world.self_design)return Status::unsupported;
    out.rows.reserve(world.camps.size());out.groups.reserve(world.groups.size());out.enemies.reserve(10);
    service.registered=true;
    return Status::incomplete; // allocation/registration completion owns this input
}

class NativeSource {
    frame::SourceWorld& world_;
    Service& service_;
    Stamp stamp_;
    std::optional<cambc::bitboard> visible_,enemy_tiles_;
    bool visibility() {
        if(!visible_)visible_=cambc::vision::visible_tiles();
        return fence(20000);
    }
    bool visible(cambc::vec2 pos) {
        ++service_.work.visibility_tests;
        return pos.x>=0&&pos.y>=0&&pos.x<visible_->width()&&pos.y<visible_->height()&&visible_->is_set(pos);
    }
public:
    NativeSource(frame::SourceWorld& world,Service& service):world_(world),service_(service){}
    bool begin(Stamp stamp){stamp_=stamp;service_.work={};return fence(52000);}
    bool fence(std::uint64_t tail)const{return current(stamp_,tail);}
    bool same_input()const {
        return int(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(cambc::self())))==stamp_.actor&&
            int(cambc::round_number())==stamp_.epoch;
    }
    void seed(std::size_t index,frame::Camp& camp)const {
        const auto& design=world_.camps[index].design;
        const auto& memory=world_.memory[std::size_t(design.group)].members[std::size_t(design.member)];
        frame::restore_camp_memory(camp,memory);
    }
    void group_fact(int id,frame::Group& group)const {
        group.id=id;const auto& memory=world_.memory[std::size_t(id)];
        group.state=memory.state;group.observed_round=memory.observed_round;group.cooldown=memory.cooldown;
        const auto& design=world_.groups[std::size_t(id)].design;group.gems=design.gems;group.gold=design.gold;
        group.possibly_woken=std::find(world_.remembered_woken.begin(),world_.remembered_woken.end(),id)!=world_.remembered_woken.end();
    }
    bool read_self(frame::Self& out) {
        using namespace cambc;
        auto& me=self();out.alive=me.is_alive();if(!out.alive)return fence(20000);
        const auto record=native_observation::read_champion(me,{stamp_.epoch,stamp_.actor,stamp_.revision});
        const auto state=record.state();out.pos=record.position();out.hp=record.health();out.max_hp=record.max_health();
        out.strength=record.strength();out.focus=record.focus();out.range=record.attack_range();out.move_speed=record.move_speed();
        out.gold=int(me.gold_count());out.gems=int(me.gem_count());out.passive=me.passive();
        out.can_move=state.contains(champion_state::can_move);out.can_attack=state.contains(champion_state::can_attack);
        out.can_cast=state.contains(champion_state::can_cast);out.recalling=state.contains(champion_state::recalling);
        out.submitting=state.contains(champion_state::submitting_gems);out.teleporting=state.contains(champion_state::teleporting);
        const auto buffs=me.buffs();out.stunned=buffs.has_stun();out.enhanced_attack=buffs.has_enhanced_attack();
        if(buffs.has_shield()){const auto shield=buffs.get<buff_shield>();out.shield=shield.value;out.shield_duration=shield.duration;}
        out.splash_radius=out.passive==passive_type::splash_attack?int(constants::passive_abilities::varg_splash_radius):0;
        out.camp_multiplier=world_.self_design->camp_damage_multiplier;
        const auto items=me.item_count();float monster=0,jungle=0,durability=0;
        for(int i=0;i<int(item_type::count);++i) {
            const item_type item{static_cast<item_type::enum_type>(i)};if(items[item]==0)continue;
            if(!world_.item_passives.contains(i)){const auto info=item.info();world_.item_passives.emplace(i,std::pair{info.passive,info.passive_magnitude});}
            const auto [passive,magnitude]=world_.item_passives.at(i);
            if(passive==item_passive::monster)monster=std::max(monster,magnitude);
            if(passive==item_passive::jungle)jungle=std::max(jungle,magnitude);
            if(passive==item_passive::durability)durability=std::max(durability,magnitude);
        }
        out.camp_multiplier*=1+monster;out.camp_taken_multiplier=(1-jungle)*(1-durability);
        out.champion_taken_multiplier=1-durability;out.basic_damage=out.strength*out.camp_multiplier;
        out.basic_damage_known=!out.enhanced_attack;out.damage_modifiers_known=true;
        return fence(20000);
    }
    bool read_enemies(Packet& out) {
        using namespace cambc;
        enemy_tiles_=vision::champion_tiles(team::enemy);
        for(auto pos:enemy_tiles_->positions()) {
            if(!fence(25000))return false;
            std::uint32_t handle{};detail::check(detail::sdk_champion_at(pos.x,pos.y,&handle));
            std::int32_t alive{};detail::check(detail::sdk_champion_is_alive(handle,&alive));
            frame::Enemy enemy;enemy.handle=int(handle);enemy.visible=true;enemy.alive=alive!=0;
            if(enemy.alive) {
                const auto record=native_observation::read_champion(static_cast<handle_champion_t>(handle),
                    {stamp_.epoch,stamp_.actor,stamp_.revision});
                ++service_.work.enemy_records;enemy.pos=record.position();enemy.hp=record.health();enemy.max_hp=record.max_health();
                enemy.strength=record.strength();enemy.focus=record.focus();enemy.range=record.attack_range();enemy.move_speed=record.move_speed();
            }
            out.enemies.push_back(enemy);
        }
        return fence(20000);
    }
    bool effect_clear(cambc::vec2 center,int radius) {
        if(!visibility()||!enemy_tiles_)return false;
        int tested=0;
        for(int y=center.y-radius;y<=center.y+radius;++y)for(int x=center.x-radius;x<=center.x+radius;++x) {
            if(tested++%4==0&&!fence(20000))return false;
            const cambc::vec2 pos{x,y};
            // Outside-map footprint has no actor; every actual in-map tile must
            // be personally visible before claiming absence of enemy collateral.
            if(x<0||y<0||x>=visible_->width()||y>=visible_->height())continue;
            if(!visible(pos)||enemy_tiles_->is_set(pos))return false;
        }
        return fence(20000);
    }
    bool read_body(selected::Row& row) {
        using namespace cambc;
        ++service_.work.rows;
        if(!visibility())return false;
        const auto& source=world_.camps[row.index];
        if(!visible(source.design.spawn))return true; // explicit unknown, no design bound
        const auto state=source.handle.state();++service_.work.state_reads;
        auto& camp=row.camp;row.state_fresh=true;camp.known=camp.visible=true;
        camp.alive=state.is_alive();camp.awake=state.is_awake();camp.possible_alive=camp.alive;
        camp.hp=0;camp.pos=camp.spawn;camp.observed_round=stamp_.epoch;
        if(camp.alive) {
            const auto record=native_observation::read_camp(source.handle,{stamp_.epoch,stamp_.actor,stamp_.revision});
            ++service_.work.record_reads;row.record_fresh=true;
            camp.pos=record.position();camp.hp=record.health();camp.strength=record.strength();
            if(row.effect) {
                const auto buffs=source.handle.buffs();++service_.work.buff_reads;row.buffs_fresh=camp.buffs_known=true;
                camp.stunned=buffs.has_stun();if(camp.stunned)camp.stun_duration=buffs.get<buff_stun>().duration;
                if(buffs.has_shield()){const auto shield=buffs.get<buff_shield>();camp.shield=shield.value;camp.shield_duration=shield.duration;}
            }
        }
        return true;
    }
    void validate_body(const selected::Row& row) {
        if(row.record_fresh&&world_.observe_spatial(row.index,row.camp)&&!world_.spatial_violation_logged) {
            world_.spatial_violation_logged=true;std::puts("error spatial model violation");
        }
    }
    bool refresh_dead_group(const selected::Row& row,frame::Group& group) {
        using namespace cambc;
        const auto& source=world_.groups[std::size_t(row.camp.group)];
        if(!row.state_fresh||row.camp.alive||!row.camp.visible||
           std::find(source.design.members.begin(),source.design.members.end(),row.index)==source.design.members.end())return false;
        const native_observation::Stamp stamp{stamp_.epoch,stamp_.actor,stamp_.revision};
        const auto state=native_observation::read_group(source.handle,stamp,
            {stamp,static_cast<handle_camp_group_t>(source.handle),true}).value;
        ++service_.work.group_reads;group.state=frame::GroupState::alive;group.cooldown=0;
        if(std::holds_alternative<map::camp_group_reward_available>(state))group.state=frame::GroupState::reward;
        else if(const auto* respawn=std::get_if<map::camp_group_respawning>(&state)) {
            group.state=frame::GroupState::respawning;group.cooldown=respawn->timer;
        }
        group.observed_round=stamp_.epoch;return true;
    }
    bool move_test(const frame::Self& self,cambc::vec2 destination,bool& legal) {
        using namespace cambc;
        if(!enemy_tiles_||!fence(25000))return false;
        auto dynamic=vision::champion_tiles(team::ally)|*enemy_tiles_|vision::camp_tiles()|vision::teleport_tiles();
        auto blocked=*world_.static_blocked|dynamic;blocked.set(self.pos,false);
        const auto reachable=blocked.reachable_tiles(self.pos,static_cast<std::uint16_t>(self.move_speed));
        legal=destination.x>=0&&destination.y>=0&&destination.x<reachable.width()&&destination.y<reachable.height()&&
            reachable.is_set(destination);
        return fence(20000);
    }
    void commit(const Packet& out) {
        for(const auto& row:out.rows)if(row.state_fresh) {
            const auto& camp=row.camp;
            world_.memory[std::size_t(camp.group)].members[std::size_t(camp.member)]=
                {camp.hp,camp.strength,stamp_.epoch,camp.alive,camp.awake};
        }
        for(const auto& group:out.groups)if(group.observed_round==stamp_.epoch) {
            auto& memory=world_.memory[std::size_t(group.id)];
            memory.state=group.state;memory.observed_round=stamp_.epoch;memory.cooldown=group.cooldown;
        }
    }
};

inline Status capture(frame::SourceWorld& world,Service& service,Packet& out,Stamp stamp,
                      const Request& request,History history={}) {
    if(service.owner!=&world||!service.registered) {
        reset_packet(out,stamp,request,++service.nonce,{world.camps.data(),world.camps.size()},world.spatial_support.borrow());
        return Status::incomplete;
    }
    NativeSource source(world,service);
    return collect(source,out,stamp,request,{world.camps.data(),world.camps.size()},world.spatial_support.borrow(),++service.nonce,history);
}

// Independent dated discovery. Rotation retains opportunities with explicit
// latency; it is not a globally fresh snapshot or a substitute for a selected
// reward recheck. Every row commits only after its own current-input fence.
inline Status lifecycle_batch(frame::SourceWorld& world,Service& service,Stamp stamp,std::size_t limit=2,
                              int selected_group=-1) {
    using namespace cambc;
    if(!service.registered||service.owner!=&world)return Status::incomplete;
    if(!current(stamp,45000))return Status::incomplete;
    if(world.groups.empty())return Status::ready;
    const auto visible=vision::visible_tiles();
    for(std::size_t n=0;n<std::min(limit,world.groups.size());++n) {
        if(!current(stamp,25000))return Status::incomplete;
        const auto index=selected_group>=0?std::size_t(selected_group):service.lifecycle_cursor%world.groups.size();
        if(index>=world.groups.size())return Status::unsupported;
        const auto& source=world.groups[index];bool in_vision=false;std::size_t tested=0;
        for(auto ci:source.design.members) {
            if(tested++%4==0&&!current(stamp,25000))return Status::incomplete;
            if(visible.is_set(world.camps[ci].design.spawn)){in_vision=true;break;}
        }
        if(in_vision) {
            const native_observation::Stamp observation{stamp.epoch,stamp.actor,stamp.revision};
            const auto state=native_observation::read_group(source.handle,observation,
                {observation,static_cast<handle_camp_group_t>(source.handle),true}).value;
            frame::GroupState value=frame::GroupState::alive;int cooldown=0;
            if(std::holds_alternative<map::camp_group_reward_available>(state))value=frame::GroupState::reward;
            else if(const auto* respawn=std::get_if<map::camp_group_respawning>(&state)){value=frame::GroupState::respawning;cooldown=respawn->timer;}
            if(!current(stamp,20000))return Status::incomplete;
            auto& memory=world.memory[index];memory.state=value;memory.cooldown=cooldown;memory.observed_round=stamp.epoch;
            ++service.work.group_reads;
            if(value==frame::GroupState::reward||value==frame::GroupState::respawning) {
                auto& wake=world.remembered_woken;wake.erase(std::remove(wake.begin(),wake.end(),int(index)),wake.end());
            }
        }
        if(selected_group>=0)return Status::ready;
        service.lifecycle_cursor=(index+1)%world.groups.size();
    }
    return current(stamp,20000)?Status::ready:Status::incomplete;
}
} // namespace mosaic::candidate
