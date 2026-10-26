#pragma once
// A conditional two-owner valuation exchange. No SDK calls or action authority.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tt::assignment_protocol {

using RosterBytes=std::array<std::byte,16>;
inline RosterBytes roster_contract(std::array<std::uint8_t,5> actual_types,bool complete,unsigned team_size) {
    RosterBytes out{};
    constexpr std::array<unsigned,4> magic{0x54,0x54,0x41,0x31};
    for(std::size_t i=0;i<magic.size();++i)out[i]=static_cast<std::byte>(magic[i]);
    out[4]=std::byte{1};out[5]=std::byte{5};out[6]=static_cast<std::byte>(complete?1:0);
    out[7]=static_cast<std::byte>(team_size<=255?team_size:0);
    for(std::size_t i=0;i<actual_types.size();++i)out[8+i]=static_cast<std::byte>(actual_types[i]);
    unsigned checksum=0xa7;for(std::size_t i=0;i<13;++i)checksum^=std::to_integer<unsigned>(out[i]);
    out[13]=static_cast<std::byte>(checksum);return out;
}
inline bool roster_contract_valid(std::span<const std::byte> bytes,std::array<std::uint8_t,5> expected_types) {
    if(bytes.size()<16)return false;
    const auto expected=roster_contract(expected_types,true,5);
    for(std::size_t i=0;i<expected.size();++i)if(bytes[i]!=expected[i])return false;
    return true;
}

enum class ProgrammeAdmission { Waiting,Started,Withdrawn };
enum class ProgrammeHandoff { SelectedContinuation,SelectedHomeHeal,OrdinaryFallback };
enum class FrontierSeedFailure { None,Transient,Permanent };
// An initial body/bag/buff refusal owns no programme. A later funded fresh
// publication may collect again; it must not destroy a live model or lease.
inline bool frontier_seed_rearm(FrontierSeedFailure failure,bool publication_funded,
    bool has_model,bool has_seed,bool has_pinned_programme)
{
    return failure==FrontierSeedFailure::Transient && publication_funded &&
        !has_model && !has_seed && !has_pinned_programme;
}
// Caller seam shared with the real controller: a future dated local fallback is
// pending, not current NoShop ownership. Ordinary fallback irrevocably retires
// that ownership; only an explicit selected home/heal owner may retain it there.
class ProgrammeGate {
public:
    bool stage(std::uint32_t start,std::uint32_t horizon) {
        clear();if(start>=horizon)return false;
        start_=start;horizon_=horizon;pending_=true;return true;
    }
    ProgrammeAdmission admit(std::uint32_t now,bool fresh_self,bool named_enabler_supported) {
        if(!fresh_self || !named_enabler_supported || now>horizon_) {clear();return ProgrammeAdmission::Withdrawn;}
        if(owned_)return ProgrammeAdmission::Started;
        if(!pending_ || now>start_) {clear();return ProgrammeAdmission::Withdrawn;}
        if(now<start_)return ProgrammeAdmission::Waiting;
        pending_=false;owned_=true;return ProgrammeAdmission::Started;
    }
    void handoff(ProgrammeHandoff why) {
        if(why==ProgrammeHandoff::OrdinaryFallback)clear();
        else if(why==ProgrammeHandoff::SelectedHomeHeal)home_heal_=owned_;
        else home_heal_=false;
    }
    bool no_shop() const {return owned_;}
    bool selected_home_heal() const {return owned_&&home_heal_;}
    bool pending() const {return pending_;}
    std::uint32_t start() const {return start_;}
private:
    void clear() {pending_=owned_=home_heal_=false;start_=horizon_=0;}
    std::uint32_t start_{},horizon_{};
    bool pending_{},owned_{},home_heal_{};
};

enum class Phase : std::uint8_t { Propose, Reply, Commit, Withdraw };
struct Key {
    std::uint16_t start{}, horizon{};
    std::uint8_t owner_a{}, owner_b{}, group_g{}, group_h{};
    friend bool operator==(const Key&,const Key&)=default;
};
struct Packet {
    Key key;
    int delta_a{},delta_b{}; // Own banked(G)-banked(H), never foreign state.
    std::uint8_t ready{};
    Phase phase{Phase::Propose};
    friend bool operator==(const Packet&,const Packet&)=default;
};
struct Limits {
    unsigned shared_bytes{},max_rounds{},groups{};
    bool roster_verified{};
};
inline bool supported(const Limits& l) {
    return l.shared_bytes==8 && l.max_rounds>0 && l.max_rounds<=2047 &&
        l.groups>=2 && l.groups<=31 && l.roster_verified;
}
inline bool valid(const Key& k) {
    return k.start>=4 && k.start<k.horizon && k.horizon<=2047 &&
        k.owner_a<k.owner_b && k.owner_b<5 && k.group_g<k.group_h && k.group_h<31;
}
inline bool fits_delta(int d) {return d>=-1024 && d<=1023;}
inline bool valid(const Packet& p) {
    if(!valid(p.key) || !fits_delta(p.delta_a) || !fits_delta(p.delta_b))return false;
    switch(p.phase) {
    case Phase::Propose:return p.ready==1 && p.delta_b==0;
    case Phase::Reply:case Phase::Commit:return p.ready==3;
    case Phase::Withdraw:return p.ready==0;
    }
    return false;
}
// Exactly 64 bits: start11, H11, owners3+3, canonical groups5+5,
// signed differences11+11, readiness2, phase2. All-zero is invalid.
inline std::optional<std::uint64_t> encode(const Packet& p) {
    if(!valid(p))return std::nullopt;
    std::uint64_t w=p.key.start | (std::uint64_t{p.key.horizon}<<11);
    w|=std::uint64_t{p.key.owner_a}<<22;w|=std::uint64_t{p.key.owner_b}<<25;
    w|=std::uint64_t{p.key.group_g}<<28;w|=std::uint64_t{p.key.group_h}<<33;
    w|=(static_cast<std::uint64_t>(p.delta_a)&2047u)<<38;
    w|=(static_cast<std::uint64_t>(p.delta_b)&2047u)<<49;
    w|=std::uint64_t{p.ready}<<60;w|=static_cast<std::uint64_t>(p.phase)<<62;
    return w;
}
inline std::optional<Packet> decode(std::uint64_t w) {
    Packet p;
    p.key={static_cast<std::uint16_t>(w&2047u),static_cast<std::uint16_t>((w>>11)&2047u),
        static_cast<std::uint8_t>((w>>22)&7u),static_cast<std::uint8_t>((w>>25)&7u),
        static_cast<std::uint8_t>((w>>28)&31u),static_cast<std::uint8_t>((w>>33)&31u)};
    const auto signed11=[](unsigned n) {return n>=1024?static_cast<int>(n)-2048:static_cast<int>(n);};
    p.delta_a=signed11(static_cast<unsigned>((w>>38)&2047u));
    p.delta_b=signed11(static_cast<unsigned>((w>>49)&2047u));
    p.ready=static_cast<std::uint8_t>((w>>60)&3u);p.phase=static_cast<Phase>((w>>62)&3u);
    return valid(p)?std::optional<Packet>{p}:std::nullopt;
}
inline std::array<std::byte,8> bytes(std::uint64_t w) {
    std::array<std::byte,8> out{};
    for(unsigned i=0;i<8;++i)out[i]=static_cast<std::byte>((w>>(8*i))&255u);
    return out;
}
inline std::uint64_t word(const std::array<std::byte,8>& value) {
    std::uint64_t out{};
    for(unsigned i=0;i<8;++i)out|=std::uint64_t{std::to_integer<unsigned>(value[i])}<<(8*i);
    return out;
}
inline bool same_attempt(const Key& a,const Key& b) {
    return a.horizon==b.horizon && a.owner_a==b.owner_a && a.owner_b==b.owner_b &&
        a.group_g==b.group_g && a.group_h==b.group_h;
}
// This wire has one explicit common hypothetical scenario: at commonStart each
// named source begins a full-HP generation, with possible sources conservatively
// awake at the work perch. It is enabled only by actual
// private ObserveAlive at work, with actual release/renewal for later cycles.
// It is never either sender's current factual Life/generation observation.
enum class Scenario { Unspecified,FullGenerationAtCommonStart };
struct Prepared {
    Key key;
    std::uint64_t premise{}; // Complete private body/life/geometry/menu revisions.
    // Completed OWN conditional values through EXACT key.start/key.horizon.
    // A caller may rebase a shift-qualified event trace, never reuse an aggregate
    // from an earlier start. Communication's four inputs are calendar time.
    int delta{};
    bool complete{},fresh{};
    Scenario scenario{Scenario::Unspecified};
    std::uint8_t actor{5}; // Stable actual SELF party role, retained through transport.
};
enum class LocalPhase { Idle, Proposed, Replied, Committed, Closed };
struct State {
    struct Attempt {Key key;std::uint64_t premise{};};
    static constexpr std::size_t history_capacity=32;
    LocalPhase phase{LocalPhase::Idle};
    std::optional<Packet> retained;
    std::optional<Key> attempted;
    std::uint64_t attempted_premise{};
    std::array<Attempt,history_capacity> history{};
    // Overflow refuses this optional protocol. Never evict/reforget a premise
    // and thereby turn an alternating pair sequence into an unbounded retry.
    std::size_t history_count{};
};
enum class Status { Idle, Unsupported, Busy, NeedQuotes, Waiting, Published, Withdrawn, Expired, Nominated };
struct Nomination {
    Key key;
    std::uint8_t self{},group{};
    std::uint64_t own_premise{};
    // Neither this report nor its foreign scalar authorizes any command.
};
struct Input {
    Limits limits;
    unsigned epoch{},self{};
    bool quiet{}; // Caller excludes mandatory Return/Work/contact and channels.
    std::optional<Prepared> own;
};
struct Decision {
    Status status{Status::Idle};
    std::optional<Packet> write;
    std::optional<Key> needed;
    std::optional<Nomination> nomination;
};
inline bool own_matches(const Input& in,const Key& key) {
    return in.quiet && in.own && in.own->complete && in.own->fresh &&
        in.own->scenario==Scenario::FullGenerationAtCommonStart && in.own->actor==in.self &&
        in.own->key==key && fits_delta(in.own->delta) &&
        key.horizon==in.limits.max_rounds && key.group_h<in.limits.groups;
}
inline bool repeated(const State& s,const Prepared& own) {
    for(std::size_t i=0;i<s.history_count;++i)
        if(same_attempt(s.history[i].key,own.key) && s.history[i].premise==own.premise)return true;
    return false;
}
inline void attempted(State& s,const Prepared& own) {
    s.attempted=own.key;s.attempted_premise=own.premise;
    if(s.history_count<s.history.size())s.history[s.history_count++]={own.key,own.premise};
}
inline Decision close(State& s,const Input& in,const std::optional<Packet>& read,Status status) {
    Decision out;out.status=status;
    if(s.retained && read && read->key==s.retained->key &&
       (in.self==read->key.owner_a || in.self==read->key.owner_b) && in.epoch<=read->key.start) {
        auto withdrawal=*read;withdrawal.phase=Phase::Withdraw;withdrawal.ready=0;
        out.write=withdrawal;
    }
    s.phase=LocalPhase::Closed;s.retained.reset();return out;
}
inline Decision advance(State& s,const Input& in,const std::optional<Packet>& incoming) {
    Decision out;
    if(!supported(in.limits) || in.self>=5 || in.epoch>in.limits.max_rounds) {
        out.status=Status::Unsupported;return out;
    }
    const auto read=incoming && valid(*incoming)?incoming:std::optional<Packet>{};
    const bool active=s.phase!=LocalPhase::Idle && s.phase!=LocalPhase::Closed;
    if(active) {
        if(!s.retained || !own_matches(in,s.retained->key) ||
           in.own->premise!=s.attempted_premise)
            return close(s,in,read,Status::Withdrawn);
        const auto key=s.retained->key;
        if((in.self!=key.owner_a && in.self!=key.owner_b) ||
           in.own->delta!=(in.self==key.owner_a?s.retained->delta_a:s.retained->delta_b))
            return close(s,in,read,Status::Withdrawn);
        if(in.epoch>key.start)return close(s,in,read,Status::Expired);
        if(!read || read->key!=key || read->phase==Phase::Withdraw)
            return close(s,in,read,Status::Withdrawn);
        if(s.phase==LocalPhase::Proposed && read->phase==Phase::Reply) {
            if(in.epoch>key.start-2 || read->delta_a!=in.own->delta)
                return close(s,in,read,Status::Expired);
            auto commit=*read;commit.phase=Phase::Commit;
            s.retained=commit;s.phase=LocalPhase::Committed;
            out.write=commit;out.status=Status::Published;return out;
        }
        if(s.phase==LocalPhase::Replied && read->phase==Phase::Commit) {
            if(read->delta_a!=s.retained->delta_a || read->delta_b!=s.retained->delta_b)
                return close(s,in,read,Status::Withdrawn);
            if(in.epoch>key.start-1 || read->delta_b!=in.own->delta)
                return close(s,in,read,Status::Expired);
            s.retained=*read;s.phase=LocalPhase::Committed;
        }
        if((s.phase==LocalPhase::Proposed && in.epoch>key.start-2) ||
           (s.phase==LocalPhase::Replied && in.epoch>key.start-1))
            return close(s,in,read,Status::Expired);
        if(s.phase==LocalPhase::Committed) {
            if(read->phase!=Phase::Commit || *read!=*s.retained)
                return close(s,in,read,Status::Withdrawn);
            if(in.epoch==key.start) {
                const bool a_gets_g=read->delta_a>=read->delta_b;
                const bool mine_g=(in.self==key.owner_a)==a_gets_g;
                out.nomination=Nomination{key,static_cast<std::uint8_t>(in.self),
                    mine_g?key.group_g:key.group_h,in.own->premise};
                out.status=Status::Nominated;s.phase=LocalPhase::Closed;s.retained.reset();return out;
            }
        }
        out.status=Status::Waiting;return out;
    }
    // A late/occupied packet cannot be overwritten while either party may still
    // be reading it in its commonStart input. Foreign actors never write it.
    if(read && read->phase!=Phase::Withdraw && in.epoch<=read->key.start) {
        if(read->phase==Phase::Propose && in.self==read->key.owner_b &&
           in.epoch>=read->key.start-4 && in.epoch<=read->key.start-3) {
            if(!own_matches(in,read->key)) {
                out.status=Status::NeedQuotes;out.needed=read->key;return out;
            }
            if(repeated(s,*in.own)) {out.status=Status::Busy;return out;}
            if(s.history_count==s.history.size()) {out.status=Status::Unsupported;return out;}
            auto reply=*read;reply.phase=Phase::Reply;reply.ready=3;reply.delta_b=in.own->delta;
            attempted(s,*in.own);s.retained=reply;s.phase=LocalPhase::Replied;
            out.write=reply;out.status=Status::Published;return out;
        }
        out.status=Status::Busy;return out;
    }
    if(!in.own || !valid(in.own->key) || !own_matches(in,in.own->key) ||
       in.self!=in.own->key.owner_a || in.own->key.start!=in.epoch+4 || repeated(s,*in.own))return out;
    if(s.history_count==s.history.size()) {out.status=Status::Unsupported;return out;}
    Packet propose{in.own->key,in.own->delta,0,1,Phase::Propose};
    attempted(s,*in.own);s.retained=propose;s.phase=LocalPhase::Proposed;
    out.write=propose;out.status=Status::Published;return out;
}
} // namespace tt::assignment_protocol
