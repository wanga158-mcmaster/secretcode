#pragma once
// Compose service facts with a sealed current Wait packet. No combat overlay,
// vision export, move search or whole-world refresh is repeated here. Facts are
// published only after the final owner-input fence. Starts remain conditional
// channel attempts; they do not promise uninterrupted future completion.
#include "candidate_observation.h"
#include "selected_conversion.h"

namespace mosaic::conversion_observation {
using Stamp=selected::Stamp;
using Packet=selected::Packet;
using Facts=conversion::Facts;
using Request=conversion::Request;
using History=conversion::History;
enum class Status {ready,incomplete,stale,unsupported};
struct Work {
    std::size_t self_records{},timer_reads{},inventory_reads{},item_info_reads{},vision_reads{},lock_reads{},tile_tests{};
};
struct Cache {const frame::SourceWorld* owner{};Work work;};

inline void revoke(Packet& packet) {
    packet.complete=packet.origin_complete=packet.self_complete=packet.enemies_complete=false;
}
inline History merge(History parent,const History& observed) {
    parent.recall_started|=observed.recall_started;parent.recall_cancelled|=observed.recall_cancelled;
    parent.tribute_started|=observed.tribute_started;parent.tribute_cancelled|=observed.tribute_cancelled;
    return parent;
}
inline std::optional<int> recall_duration(int base,double home) {
    if(base<1||!std::isfinite(home)||home<0||home>1)return std::nullopt;
    const auto ticks=std::ceil(double(base)*(1-home));
    if(ticks<1||ticks>double(std::numeric_limits<int>::max()))return std::nullopt;
    return int(ticks);
}

// Checks the already represented core state; it does not replace the caller's
// obligation to advance revision after EVERY attempted primitive. Buff/inventory
// mutations cannot be detected solely from a champion core record.
inline bool core_matches(const native_observation::ChampionRecord& record,const frame::Self& self) {
    const auto state=record.state();
    return record.position()==self.pos&&record.health()==self.hp&&record.max_health()==self.max_hp&&
        record.strength()==self.strength&&record.focus()==self.focus&&record.attack_range()==self.range&&
        record.move_speed()==self.move_speed&&state.contains(cambc::champion_state::alive)==self.alive&&
        state.contains(cambc::champion_state::can_move)==self.can_move&&
        state.contains(cambc::champion_state::can_attack)==self.can_attack&&
        state.contains(cambc::champion_state::can_cast)==self.can_cast&&
        state.contains(cambc::champion_state::recalling)==self.recalling&&
        state.contains(cambc::champion_state::submitting_gems)==self.submitting&&
        state.contains(cambc::champion_state::teleporting)==self.teleporting;
}

template<class Observer>
Status collect(Observer& source,Packet& packet,const Request& request,const History& parent,Facts& out) {
    out={};
    if(request.kind==conversion::Kind::buy)return Status::unsupported;
    if(packet.request.command.kind!=transition::Kind::wait||!packet.matches(packet.stamp,packet.request)||
       !request.stamp.matches(packet.stamp))return Status::stale;
    if(parent.epoch!=packet.stamp.epoch||parent.actor!=packet.stamp.actor)return Status::incomplete;
    if(!packet.self_complete||!packet.self.alive)return Status::unsupported;
    const auto failed=[&](Status status=Status::incomplete) {
        if(status==Status::stale||!source.same_input()) {revoke(packet);return Status::stale;}
        return status;
    };
    if(!source.begin(packet.stamp))return failed();
    Facts staged;staged.request=request;staged.stamp=packet.stamp;staged.packet_nonce=packet.nonce;
    staged.layout=packet.layout;staged.origin=packet.self.pos;
    History observed;
    const auto history_status=source.read_history(packet,observed);
    if(history_status!=Status::ready)return failed(history_status);
    if(!source.fence(18000))return failed();
    if(observed.epoch!=packet.stamp.epoch||observed.actor!=packet.stamp.actor)return failed(Status::stale);
    staged.history=merge(parent,observed);staged.history_complete=true;
    staged.gems=packet.self.gems;staged.bag_complete=true;
    const int active=int(packet.self.recalling)+int(packet.self.submitting)+int(packet.self.teleporting);
    if(active>1)return failed(Status::unsupported);
    staged.channel=conversion::observed_channel(packet.self);
    if(staged.channel!=conversion::Channel::none) {
        const auto channel_status=source.read_channel(staged.remaining);
        if(channel_status!=Status::ready)return failed(channel_status);
    }
    staged.channel_complete=staged.channel==conversion::Channel::none||staged.remaining>=0;
    if(request.kind==conversion::Kind::recall_start||request.kind==conversion::Kind::tribute_start) {
        const auto settings_status=source.read_settings(request,staged);
        if(settings_status!=Status::ready)return failed(settings_status);
        staged.settings_complete=true;
    }
    if(request.kind==conversion::Kind::tribute_start) {
        const auto altar_status=source.read_altar(request,staged);
        if(altar_status!=Status::ready)return failed(altar_status);
    }
    if(!source.fence(18000))return failed();
    if(!packet.layout.valid(packet.catalog.size()))return failed(Status::stale);
    staged.complete=true;out=std::move(staged);return Status::ready;
}

class NativeSource {
    frame::SourceWorld& world_;
    Cache& cache_;
    Stamp stamp_;
public:
    NativeSource(frame::SourceWorld& world,Cache& cache):world_(world),cache_(cache){}
    bool begin(Stamp stamp){stamp_=stamp;cache_.work={};return fence(25000);}
    bool fence(std::uint64_t tail)const{return candidate::current(stamp_,tail);}
    bool same_input()const {
        return int(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(cambc::self())))==stamp_.actor&&
            int(cambc::round_number())==stamp_.epoch;
    }
    Status read_history(const Packet& packet,History& history) {
        const auto record=native_observation::read_champion(cambc::self(),{stamp_.epoch,stamp_.actor,stamp_.revision});
        ++cache_.work.self_records;
        if(!core_matches(record,packet.self))return Status::stale;
        const auto state=record.state();history.epoch=stamp_.epoch;history.actor=stamp_.actor;
        history.recall_started=state.contains(cambc::champion_state::recall_started);
        history.recall_cancelled=state.contains(cambc::champion_state::recall_cancelled);
        history.tribute_started=state.contains(cambc::champion_state::submit_gems_started);
        history.tribute_cancelled=state.contains(cambc::champion_state::submit_gems_cancelled);
        return Status::ready;
    }
    Status read_channel(int& remaining) {
        // Public remaining_channel_time() first rereads info(). The sealed Wait
        // plus read_history's fresh self record already establish that same live
        // identity/visibility premise; preserve the checked timer import itself.
        cambc::detail::check(cambc::detail::sdk_champion_channel_time(std::uint32_t(stamp_.actor),&remaining));
        ++cache_.work.timer_reads;return Status::ready;
    }
    Status read_settings(const Request& request,Facts& facts) {
        using namespace cambc;
        facts.submit_range=constants::submit_gems_range;facts.submit_base=constants::submit_gem_base_duration;
        facts.submit_min=constants::submit_gem_minimum_duration;facts.submit_multiplier=constants::submit_gem_duration_multiplier;
        if(request.kind!=conversion::Kind::recall_start)return Status::ready;
        // Runtime settings and current inventory, with duplicate family passives
        // nonstacking (pinned rules). Future actual channel timer is authoritative.
        const auto inventory=self().item_count();++cache_.work.inventory_reads;double home=0;
        for(int i=0;i<int(item_type::count);++i) {
            const item_type item{static_cast<item_type::enum_type>(i)};if(inventory[item]==0)continue;
            if(!fence(18000))return Status::incomplete;
            if(!world_.item_passives.contains(i)) {
                const auto info=item.info();++cache_.work.item_info_reads;
                world_.item_passives.emplace(i,std::pair{info.passive,info.passive_magnitude});
            }
            const auto [passive,magnitude]=world_.item_passives.at(i);
            if(passive==item_passive::home) {
                if(!std::isfinite(magnitude)||magnitude<0||magnitude>1)return Status::unsupported;
                home=std::max(home,double(magnitude));
            }
        }
        const auto duration=recall_duration(constants::recall_time,home);
        if(!duration)return Status::unsupported;
        facts.recall_duration=*duration;return Status::ready;
    }
    Status read_altar(const Request& request,Facts& facts) {
        using namespace cambc;
        facts.altar=request.altar;facts.altar_complete=true;
        const auto& altars=map::altars();
        const auto pos=request.altar;
        const bool in_map=pos.x>=0&&pos.y>=0&&pos.x<altars.width()&&pos.y<altars.height();
        ++cache_.work.tile_tests;facts.altar_valid=in_map&&altars.is_set(pos);
        if(!facts.altar_valid)return Status::ready;
        if(!fence(18000))return Status::incomplete;
        const auto visible=vision::visible_tiles();++cache_.work.vision_reads;
        ++cache_.work.tile_tests;facts.altar_visible=visible.is_set(pos);
        // A hidden lock is not an absent lock. No ally/enemy inference from the
        // roster, and no auto-attack against a lock is smuggled into tribute.
        if(!facts.altar_visible)return Status::ready;
        const auto enemy_locks=vision::altar_lock_tiles(team::enemy);++cache_.work.lock_reads;
        ++cache_.work.tile_tests;facts.enemy_lock=enemy_locks.is_set(pos);facts.lock_complete=true;
        return Status::ready;
    }
};

inline Status capture(frame::SourceWorld& world,Cache& cache,Packet& packet,const Request& request,
                      const History& history,Facts& out) {
    out={};
    if(request.kind==conversion::Kind::buy)return Status::unsupported;
    if((cache.owner&&cache.owner!=&world)||!world.initialized||packet.catalog.data()!=world.camps.data()||
       packet.catalog.size()!=world.camps.size()||packet.layout.index!=&world.spatial_support) {
        revoke(packet);return Status::stale;
    }
    cache.owner=&world;NativeSource source(world,cache);
    return collect(source,packet,request,history,out);
}
} // namespace mosaic::conversion_observation
