#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Scheduled owner inputs before our NEXT owner input, not attack opportunities
// or a safety certificate. The caller supplies one fresh observation epoch in
// the current owner's action phase, the COMPLETE actual rosters (extra bodies
// included), the pinned round-reset AND normal/forced-input-completion meaning
// of completed_turn, and priority
// commitments. Higher priority precedes lower; a known tie_key is a proven
// total tie order, never a draft-position/handle guess. Teams interleave their
// ranked actors and append the longer roster's remaining actors.
//
// CompletedInput means that actor has ended THIS round's input, so it cannot
// change its next priority again before next round. CallerFixed instead names
// an external policy guarantee of no intervening priority mutation. Observation
// of a pending actor's next priority alone supplies NEITHER commitment.
namespace wb::capital::order {
inline constexpr std::size_t max_team=6;
enum class Commitment {Unknown,CompletedInput,CallerFixed};
struct Member {
    int id=-1;
    // Known means the caller admits the current-round completion contract for
    // this observation, including relevant suspension paths; seeing one raw
    // flag without that contract is not enough to set completion_known.
    bool completion_known=false,completed_current=false;
    bool next_priority_known=false;
    std::int32_t next_priority=0;
    Commitment commitment=Commitment::Unknown;
    bool tie_key_known=false;
    std::int32_t tie_key=0;
};
struct Roster {
    std::array<Member,max_team> members{};
    unsigned count=0;
    bool complete=false;
};
struct Rank {
    unsigned min=0,max=max_team-1;
    bool valid=false;
};
struct Packet {
    Roster own{},enemy{};
    int self_id=-1;
    bool fresh=false,owner_action_phase=false;
    bool next_starter_known=false,own_starts_next=false;
    // An independent caller proof may narrow own next rank without a numeric
    // tie-key mapping. Example: actual current rank, unchanged observed own
    // prev/next priorities, fixed roster, no later own setter, and the admitted
    // engine completion/suspension contract. A completion count alone is NOT
    // such a proof. The interval is checked and intersected with roster facts.
    bool own_rank_override_admitted=false;
    Rank own_rank_override{};
};
// Exact refers ONLY to every per-foe scheduled-input count. It does not mean
// exact future ranks, usable actions, complete hidden state or certified safety.
enum class Status {Fallback,Bounded,Exact};
enum class Reason {None,Stale,WrongPhase,IncompleteRoster,InvalidRoster,MissingSelf,InconsistentCommitment,InvalidOwnRank};
struct Window {
    int id=-1;
    bool admitted=false;
    Rank next_rank{};
    unsigned current_min=0,current_max=1,next_min=0,next_max=1;
    unsigned scheduled_lower=0,scheduled_upper=2;
};
struct Result {
    Status status=Status::Fallback;
    Reason reason=Reason::None;
    Rank own_next_rank{};
    std::array<Window,max_team> foes{};
    unsigned count=0;
    // Failure or a missing identity never silently becomes a zero-response word.
    [[nodiscard]] Window for_id(int id) const {
        if(status!=Status::Fallback)for(unsigned i=0;i<count;++i)if(foes[i].id==id)return foes[i];
        Window fallback;fallback.id=id;return fallback;
    }
};
namespace detail {
inline bool committed(const Member& m) {
    return m.next_priority_known&&m.commitment!=Commitment::Unknown;
}
inline bool valid_commitment(const Member& m) {
    if(static_cast<unsigned>(m.commitment)>static_cast<unsigned>(Commitment::CallerFixed))return false;
    if(m.commitment==Commitment::CompletedInput&&(!m.completion_known||!m.completed_current))return false;
    return m.commitment==Commitment::Unknown||m.next_priority_known;
}
// A pair relation is {definitely before, possibly before}. Unknown priority or
// unknown tie order retains BOTH possible relative orders. Numeric priority
// extrema can make that broad interval loose, but can never invalidate it.
inline std::array<bool,2> before(const Member& peer,const Member& target) {
    if(!committed(peer)||!committed(target))return {false,true};
    if(peer.next_priority>target.next_priority)return {true,true};
    if(peer.next_priority<target.next_priority)return {false,false};
    if(!peer.tie_key_known||!target.tie_key_known)return {false,true};
    const bool value=peer.tie_key<target.tie_key;return {value,value};
}
inline Reason validate_roster(const Roster& r) {
    if(!r.complete)return Reason::IncompleteRoster;
    if(r.count==0||r.count>max_team)return Reason::InvalidRoster;
    for(unsigned i=0;i<r.count;++i) {
        const auto& m=r.members[i];if(m.id<0)return Reason::InvalidRoster;
        if(!valid_commitment(m))return Reason::InconsistentCommitment;
        for(unsigned j=0;j<i;++j) {
            if(m.id==r.members[j].id)return Reason::InvalidRoster;
            if(m.tie_key_known&&r.members[j].tie_key_known&&m.tie_key==r.members[j].tie_key)
                return Reason::InvalidRoster;
        }
    }
    return Reason::None;
}
}
// This interval encloses every ranking compatible with the supplied pair facts.
// Correlations among unknown priorities may make it wider than the exact hull.
[[nodiscard]] inline Rank rank(const Roster& roster,unsigned at) {
    if(detail::validate_roster(roster)!=Reason::None||at>=roster.count)return {};
    Rank out{0,0,true};const auto& target=roster.members[at];
    for(unsigned i=0;i<roster.count;++i)if(i!=at) {
        const auto relation=detail::before(roster.members[i],target);
        out.min+=relation[0]?1u:0u;out.max+=relation[1]?1u:0u;
    }
    return out;
}
[[nodiscard]] inline Result derive(const Packet& p) {
    Result out;
    if(!p.fresh){out.reason=Reason::Stale;return out;}
    if(!p.owner_action_phase){out.reason=Reason::WrongPhase;return out;}
    const auto own_reason=detail::validate_roster(p.own),enemy_reason=detail::validate_roster(p.enemy);
    if(own_reason!=Reason::None){out.reason=own_reason;return out;}
    if(enemy_reason!=Reason::None){out.reason=enemy_reason;return out;}
    unsigned self=p.own.count;
    for(unsigned i=0;i<p.own.count;++i)if(p.own.members[i].id==p.self_id)self=i;
    if(self==p.own.count){out.reason=Reason::MissingSelf;return out;}
    if(p.own.members[self].completion_known&&p.own.members[self].completed_current) {
        out.reason=Reason::WrongPhase;return out;
    }
    for(unsigned i=0;i<p.own.count;++i)for(unsigned j=0;j<p.enemy.count;++j)
        if(p.own.members[i].id==p.enemy.members[j].id){out.reason=Reason::InvalidRoster;return out;}
    out.own_next_rank=rank(p.own,self);
    if(p.own_rank_override_admitted) {
        const auto& supplied=p.own_rank_override;
        if(!supplied.valid||supplied.min>supplied.max||supplied.max>=p.own.count) {
            out.reason=Reason::InvalidOwnRank;return out;
        }
        if(supplied.min>out.own_next_rank.min)out.own_next_rank.min=supplied.min;
        if(supplied.max<out.own_next_rank.max)out.own_next_rank.max=supplied.max;
        if(out.own_next_rank.min>out.own_next_rank.max) {out.reason=Reason::InvalidOwnRank;return out;}
    }
    out.count=p.enemy.count;
    bool exact=true;
    for(unsigned i=0;i<p.enemy.count;++i) {
        const auto& foe=p.enemy.members[i];auto& window=out.foes[i];window.id=foe.id;window.admitted=true;
        window.next_rank=rank(p.enemy,i);
        if(foe.completion_known)window.current_min=window.current_max=foe.completed_current?0u:1u;
        // The count of next-round slots before ours is monotone in both ranks.
        // This comparison also covers unequal rosters: all lower-ranked slots
        // of a shorter opposing roster precede our appended later slot.
        const bool may_tie_enemy_first=!p.next_starter_known||!p.own_starts_next;
        const bool must_tie_enemy_first=p.next_starter_known&&!p.own_starts_next;
        window.next_max=(window.next_rank.min<out.own_next_rank.max||
            (window.next_rank.min==out.own_next_rank.max&&may_tie_enemy_first))?1u:0u;
        window.next_min=(window.next_rank.max<out.own_next_rank.min||
            (window.next_rank.max==out.own_next_rank.min&&must_tie_enemy_first))?1u:0u;
        window.scheduled_lower=window.current_min+window.next_min;
        window.scheduled_upper=window.current_max+window.next_max;
        exact&=window.scheduled_lower==window.scheduled_upper;
    }
    out.status=exact?Status::Exact:Status::Bounded;return out;
}
}
