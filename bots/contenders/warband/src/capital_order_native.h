#pragma once
#include <cambc.h>
#include "capital_order.h"
#include "config.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace wb::capital::order_native {
// Actual accepted OWN draft picks supply the documented draft tie order.
// Unexpected/multiple-body identities disable the mapping, never use handles.
struct DraftOrder {std::uint32_t magic=0x57424f31u,count=0;std::array<std::int32_t,6> types{};};
inline int type(cambc::champion_draft pick) {
    using D=cambc::champion_draft;using C=cambc::champion_type;
    switch(pick) {
    case D::varg:return static_cast<int>(C::varg);
    case D::willow:return static_cast<int>(C::willow);
    case D::vendetta:return static_cast<int>(C::vendetta);
    case D::marcella:return static_cast<int>(C::marcella);
    case D::ryth:return static_cast<int>(C::ryth);
    default:return -1;
    }
}
inline void accepted_pick(std::span<const cambc::champion_draft> prior,cambc::champion_draft pick) {
    if(prior.size()>=6||cambc::constants::draft_memory_bytes<sizeof(DraftOrder))return;
    DraftOrder packet;packet.count=static_cast<unsigned>(prior.size()+1);
    for(unsigned i=0;i<prior.size();++i)packet.types[i]=type(prior[i]);
    packet.types[prior.size()]=type(pick);
    for(unsigned i=0;i<packet.count;++i) {
        if(packet.types[i]<0)return;
        for(unsigned j=0;j<i;++j)if(packet.types[i]==packet.types[j])return;
    }
    cambc::draft_memory=std::span<const DraftOrder>{&packet,1};
}
struct State {
    order::Roster own{};
    order::Packet packet{};
    order::Result result{};
    int owner=-1,round=-1,prime_round=-1,pivot=-1;
    unsigned enemy_count=0;
    bool primed=false;
};
inline State& state(){static State value;return value;}
inline int id(const cambc::champion& c){return static_cast<int>(static_cast<cambc::handle_champion_t>(c));}
inline void prime(std::span<const cambc::champion> allies) {
    auto& out=state();out={};const auto epoch=cambc::round_number();
    if(allies.empty()||allies.size()>order::max_team)return;
    DraftOrder draft;bool tie_proven=false;
    if(cambc::draft_memory.size()>=sizeof(DraftOrder)) {
        std::memcpy(&draft,cambc::draft_memory.data(),sizeof draft);
        tie_proven=draft.magic==0x57424f31u&&draft.count==allies.size();
        for(unsigned i=0;i<draft.count&&tie_proven;++i)for(unsigned j=0;j<i;++j)
            if(draft.types[i]==draft.types[j])tie_proven=false;
    }
    out.own.count=static_cast<unsigned>(allies.size());out.own.complete=true;
    out.enemy_count=static_cast<unsigned>(cambc::champions(cambc::team::enemy).size());out.prime_round=epoch;
    for(unsigned i=0;i<allies.size();++i) {
        auto& member=out.own.members[i];member.id=id(allies[i]);
        std::int32_t next=0;const auto code=cambc::detail::sdk_bc_champion_priority(member.id,1,&next);
        if(code==0){member.next_priority_known=true;member.next_priority=next;member.commitment=order::Commitment::CallerFixed;}
        else if(code!=static_cast<int>(cambc::error_code::api_not_in_vision))cambc::detail::check(code);
        // CallerFixed requires the COMPLETE capital branch to keep its current
        // no-set_priority policy, including dead/recovery/contact inputs.
        if(tie_proven) {
            const auto kind=static_cast<int>(allies[i].champion_name());
            for(unsigned j=0;j<draft.count;++j)if(draft.types[j]==kind){member.tie_key_known=true;member.tie_key=j;}
            if(!member.tie_key_known)tie_proven=false;
        }
    }
    if(!tie_proven)for(auto& member:out.own.members)member.tie_key_known=false;
    else for(const auto& member:out.own.members)if(member.tie_key==static_cast<int>(out.own.count)-1)out.pivot=member.id;
    out.primed=cambc::round_number()==epoch;
}
inline void begin(std::span<const cambc::champion> enemies) {
    auto& out=state();out.packet={};out.result={};out.round=cambc::round_number();out.owner=id(cambc::self());
    auto& p=out.packet;p.own=out.own;p.self_id=out.owner;p.owner_action_phase=true;
    if constexpr(config::contact_service_controller) {
        // Only the designated last-draft body can promote/restore priority.
        // Other observers never retain its old priority as a fixed premise.
        for(unsigned i=0;i<p.own.count;++i)if(p.own.members[i].id==out.pivot) {
            auto& member=p.own.members[i];member.commitment=order::Commitment::Unknown;
            if(member.id==out.owner) {
                member.next_priority=cambc::self().priority();member.next_priority_known=true;
                member.commitment=order::Commitment::CallerFixed;
            }
        }
    }
    p.fresh=out.primed;p.enemy.complete=enemies.size()>0&&enemies.size()<=order::max_team;
    if(p.enemy.complete) {
        p.enemy.count=static_cast<unsigned>(enemies.size());
        for(unsigned i=0;i<enemies.size();++i)p.enemy.members[i].id=id(enemies[i]);
    }
    const auto team=cambc::detail::sdk_my_team();
    p.next_starter_known=out.round>=1&&(team==0||team==1);
    p.own_starts_next=team==(out.round&1); // pinned A-odd/B-even schedule
}
inline void observe(int foe,const cambc::detail::champion_info& raw) {
    auto& p=state().packet;
    for(unsigned i=0;i<p.enemy.count;++i)if(p.enemy.members[i].id==foe) {
        auto& member=p.enemy.members[i];
        const auto flags=cambc::champion_state::from_bits_retain(raw.state);
        if(flags.contains(cambc::champion_state::completed_turn)) {
            // False may also describe an earlier suspended input. It remains
            // upper-one unknown, not an assertion of an exact current slot.
            member.completion_known=member.completed_current=true;
            std::int32_t next=0;const auto code=cambc::detail::sdk_bc_champion_priority(foe,1,&next);
            if(code==0){member.next_priority_known=true;member.next_priority=next;member.commitment=order::Commitment::CompletedInput;}
            else if(code!=static_cast<int>(cambc::error_code::api_not_in_vision))cambc::detail::check(code);
        }
        return;
    }
}
inline const order::Result& finish() {
    auto& out=state();out.packet.fresh&=out.round==static_cast<int>(cambc::round_number());
    out.result=order::derive(out.packet);return out.result;
}
struct Tail {
    bool admitted=false;
    std::int32_t prior_priority=0,request_priority=0;
};
inline Tail tail_before_bank() {
    Tail answer;const auto& s=state();const int now=cambc::round_number();
    if(!s.primed||s.pivot!=id(cambc::self())||now<=s.prime_round
        ||s.enemy_count==0||s.enemy_count>s.own.count)return answer;
    const auto team=cambc::detail::sdk_my_team();
    if(team<0||team>1||team!=(now&1))return answer; // enemy starts CURRENT round
    auto current=s.own;unsigned at=current.count;
    std::int32_t highest=std::numeric_limits<std::int32_t>::min();
    for(unsigned i=0;i<current.count;++i) {
        auto& m=current.members[i];
        if(!m.next_priority_known||!m.tie_key_known)return answer;
        m.commitment=order::Commitment::CallerFixed;
        if(m.id==s.pivot){at=i;m.next_priority=cambc::self().prev_priority();}
        else highest=std::max(highest,m.next_priority);
    }
    if(at==current.count||highest==std::numeric_limits<std::int32_t>::max())return answer;
    const auto rank=order::rank(current,at);
    if(!rank.valid||rank.min!=current.count-1||rank.max!=rank.min)return answer;
    // All current actors have finished before this final own input. All other
    // own next priorities are fixed and now committed. A STRICTLY higher
    // accepted request makes our next owner-start precede every actor input.
    answer.prior_priority=cambc::self().priority();answer.request_priority=highest+1;
    answer.admitted=cambc::round_number()==static_cast<unsigned>(now);return answer;
}
inline order::Window window(int foe) {
    const auto& s=state();
    if(s.round!=static_cast<int>(cambc::round_number())||s.owner!=id(cambc::self()))return {};
    return s.result.for_id(foe);
}
}
