#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace h12_twin_interlock {
// These are dated self observations. In particular, cargo=false says nothing
// about a capture that happened after the sender's owner turn.
struct Report {
    int round=0,x=0,y=0;
    bool alive=false,cargo=false,safe=false;
    unsigned channel=0;
    bool epoch=false,valid=false;
};
using Packet=std::array<std::byte,4>;

inline Packet encode(const Report&r) {
    const bool fits=r.round>=0&&r.round<=2047&&r.x>=0&&r.x<=127
        &&r.y>=0&&r.y<=127&&r.channel<=3;
    const std::uint32_t word=(std::uint32_t(r.round)&2047u)
        |((std::uint32_t(r.x)&127u)<<11)|((std::uint32_t(r.y)&127u)<<18)
        |(std::uint32_t(r.alive)<<25)|(std::uint32_t(r.cargo)<<26)
        |(std::uint32_t(r.safe)<<27)|((r.channel&3u)<<28)
        |(std::uint32_t(r.epoch)<<30)|(std::uint32_t(r.valid&&fits)<<31);
    Packet bytes{};
    for(unsigned i=0;i<4;++i)bytes[i]=std::byte((word>>(8*i))&255u);
    return bytes;
}
inline Report decode(const Packet&bytes) {
    std::uint32_t word=0;
    for(unsigned i=0;i<4;++i)word|=std::uint32_t(bytes[i])<<(8*i);
    return {int(word&2047u),int((word>>11)&127u),int((word>>18)&127u),
        bool((word>>25)&1u),bool((word>>26)&1u),bool((word>>27)&1u),
        (word>>28)&3u,bool((word>>30)&1u),bool(word>>31)};
}
inline bool fresh(const Report&r,int now,int width,int height,int maxround) {
    return r.valid&&width>0&&width<=100&&height>0&&height<=100
        &&maxround>0&&maxround<=2047&&now>=1&&now<=maxround
        &&r.round>=1&&r.round<=maxround&&r.round<=now&&now-r.round<=1
        &&r.x>=0&&r.x<width&&r.y>=0&&r.y<height;
}
inline int owner_offset(int physical_identity) {
    return physical_identity==3?0:physical_identity==4?4:-1;
}
// Production adapters use these exact range operations. A cached eight-byte
// array is never written back, and an ordinary roster actor cannot zero it.
template<class Memory> std::array<Report,2> read_reports(const Memory&memory) {
    std::array<Report,2> reports{};
    if(memory.size()<8)return reports;
    std::array<std::byte,8> bytes{};
    memory.read(0,std::span<std::byte>{bytes});
    for(unsigned owner=0;owner<2;++owner) {
        Packet packet{};
        for(unsigned i=0;i<4;++i)packet[i]=bytes[owner*4+i];
        reports[owner]=decode(packet);
    }
    return reports;
}
template<class Memory> bool write_report(const Memory&memory,int physical_identity,const Report&r) {
    const int offset=owner_offset(physical_identity);
    if(offset<0||memory.size()<8)return false;
    const auto bytes=encode(r);
    memory.write(std::size_t(offset),std::span<const std::byte>{bytes});
    return true;
}
inline Report self_report(int round,int x,int y,bool alive,int bag,bool safe,
                          unsigned channel,bool epoch) {
    // Starting tribute retains the real positive bag. Only a later observed
    // zero bag clears cargo; neither the channel nor a promised bank does.
    return {round,x,y,alive,bag>0,safe,channel,epoch,true};
}

class Link {
    bool own_alive_=false,had_life_=false,epoch_=false,resumed_=false;
    bool peer_seen_=false,peer_epoch_=false,require_change_=false,death_peer_epoch_=false;
    int born_=0,accepted_=0;
public:
    // Call from actual own is_alive observations, including dead owner turns.
    bool observe_life(bool alive,int now) {
        if(!alive) {
            if(own_alive_) {
                require_change_=peer_seen_;
                death_peer_epoch_=peer_epoch_;
            }
            own_alive_=false;
            return false;
        }
        if(own_alive_)return false;
        own_alive_=true;
        if(had_life_)epoch_=!epoch_;
        born_=now;accepted_=now;
        const bool newlife=had_life_;
        resumed_=newlife;
        had_life_=true;
        return newlife;
    }
    void observe_peer(const Report&r) {
        if(r.valid){peer_seen_=true;peer_epoch_=r.epoch;}
    }
    bool accept(const Report&r,int now,int width,int height,int maxround,bool hidden_alive) {
        if(!own_alive_||!hidden_alive||!r.alive||!fresh(r,now,width,height,maxround))return false;
        // The packet cannot resurrect the hidden partner. After a real own
        // respawn demand a fresh report from the new cohort, including a changed
        // epoch when an old peer epoch was observed before linked death.
        if(resumed_&&r.round<born_)return false;
        if(require_change_&&r.epoch==death_peer_epoch_)return false;
        require_change_=false;
        observe_peer(r);
        // A prior-turn zero is an old observation, not permission: a capture
        // can fill that private bag before this owner reads the report. Do not
        // let a stream of prior-zero reports reset bounded unknown fallback.
        if(r.round==now||r.cargo||!r.safe)accepted_=now;
        return true;
    }
    bool epoch()const{return epoch_;}
    int unknown_age(int now)const{return std::max(0,now-accepted_);}
    void note_accepted(int now){accepted_=now;}
};

enum class Mode {SelfRetreat,PartnerRetreat,Protect,Unknown,Work};
inline Mode mode(bool self_unsafe,bool accepted,const Report&peer) {
    if(self_unsafe)return Mode::SelfRetreat;
    if(!accepted)return Mode::Unknown;
    if(!peer.safe)return Mode::PartnerRetreat;
    if(peer.cargo)return Mode::Protect;
    return Mode::Work;
}
inline Mode mode_for_report(bool self_unsafe,bool accepted,const Report&peer,int now) {
    if(self_unsafe)return Mode::SelfRetreat;
    if(!accepted)return Mode::Unknown;
    if(!peer.safe)return Mode::PartnerRetreat;
    if(peer.cargo)return Mode::Protect;
    return peer.round==now?Mode::Work:Mode::Unknown;
}
inline bool may_damage(Mode current,int unknown_age,float hp,float maximum,
                       float measured_loss,bool enemies,bool harmless) {
    if(current==Mode::SelfRetreat||current==Mode::PartnerRetreat)return false;
    if(harmless)return true;
    if(current==Mode::Protect)return false;
    if(current==Mode::Work)return true;
    // Unknown never means permission immediately. A bounded wait eventually
    // permits conservative own production, without a ghost partner deadlock.
    return unknown_age>=6&&maximum>0&&hp>=maximum*.82f&&!enemies
        &&measured_loss>=0&&measured_loss<=maximum*.12f;
}
inline bool may_provoke(Mode current,int unknown_age,float hp,float maximum,
                        float measured_loss,bool enemies,bool harmless_source) {
    // The hazard belongs to the entire group being awakened or approached,
    // not to this cast's damage. A zero-damage stun still provokes a monster.
    return may_damage(current,unknown_age,hp,maximum,measured_loss,enemies,harmless_source);
}
inline bool safe_landing(Mode current,bool exposed) {
    // A zero-damage dash can still put a linked body in lethal range. The
    // caller derives exposure from fresh actual threats, never promised cover.
    return !exposed||current==Mode::Work;
}
inline bool packet_admitted(std::uint64_t remaining){return remaining>=42000;}
inline bool component_admitted(std::uint64_t remaining){return remaining>=80000;}
inline bool component_needs_refresh(bool ready,bool origin_inside) {
    // Recall and each body's own respawn can move the origin to a different
    // static walking component without changing the map's immutable terrain.
    return !ready||!origin_inside;
}
inline bool cancel_for_exposure(bool unsafe,float incoming) {
    // Low HP alone must not restart a safe in-progress recall or tribute.
    return unsafe&&incoming>0;
}
inline int quarantine_after_refusal(int round){return round+48;}
inline bool has_action(bool move,bool attack,bool cast){return move||attack||cast;}
inline bool cast_compatible(bool ready,bool excludes_walking,bool walked) {
    return ready&&!(excludes_walking&&walked);
}
} // namespace h12_twin_interlock
