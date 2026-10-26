#pragma once
#include "events.h"
#include <cstdio>
#include <type_traits>

namespace p11::expedition {
struct Position {int x=0,y=0;};
// A diagnostic packet, not a new decision rule. All arrays have explicit
// presence/count/truncation. Unknown route/visibility is never a false fact.
struct Packet {
    State state{};
    Candidate preview{};
    std::array<Monster,4> model{};
    std::array<int,3> order{};
    std::array<Position,4> allies{};
    bool retained=false,captured=false,flush_attempted=false;
    bool ordinary_shortlist=false,preview_feasible=false;
    bool order_truncated=false,model_truncated=false,allies_truncated=false;
    bool gate_passed=false,monolith=false,packing_ready=false;
    bool round_checked=false,round_current=false;
    int actor=-1,group=-1,native_group=-1,order_position=-1;
    int order_count=0,model_count=0,ally_count=0,range=0;
    int origin_x=0,origin_y=0,anchor_x=0,anchor_y=0,target=-1;
    unsigned detail_fuel=0,unavailable_until=0;
    // Stateful production entry: start a fresh eligible attempt, not a pure
    // eligibility query. Keep this caller spelling stable in runtime.h.
    bool eligible(unsigned identity,int round){return begin_attempt(identity,round);}
    bool begin_attempt(unsigned identity,int round) {
        if(identity!=7||round>8||captured||flush_attempted)return false;
        // A retained but uncaptured attempt must not supply the next one's
        // candidate or finite-shortlist provenance.
        retained=false;ordinary_shortlist=false;preview_feasible=false;
        preview.id=-1;group=-1;native_group=-1;order_position=-1;
        round_checked=false;round_current=false;
        return true;
    }
    void retain(const Candidate&candidate){preview=candidate;retained=true;preview_feasible=true;}
    template<class Positions>
    void capture(const State&s,std::span<const int>frontier,std::span<const Monster>public_model,
                 const Positions&positions,unsigned fuel,bool passed) {
        state=s;detail_fuel=fuel;gate_passed=passed;captured=true;
        order_count=int(frontier.size());order_truncated=frontier.size()>order.size();
        for(std::size_t i=0;i<std::min(frontier.size(),order.size());++i)order[i]=frontier[i];
        order_position=-1;
        for(std::size_t i=0;i<std::min(frontier.size(),order.size());++i)if(retained&&frontier[i]==preview.id)order_position=int(i);
        model_count=int(public_model.size());model_truncated=public_model.size()>model.size();
        for(std::size_t i=0;i<std::min(public_model.size(),model.size());++i)model[i]=public_model[i];
        ally_count=int(positions.size());allies_truncated=positions.size()>allies.size();
        for(std::size_t i=0;i<std::min(positions.size(),allies.size());++i)allies[i]={positions[i].x,positions[i].y};
    }
    bool finite_preview_by_membership()const{return retained&&ordinary_shortlist&&order_position>=0;}
    // Structural bounds only; this says nothing about known/reward/route or
    // whether the reconstructed detailed policy would accept this candidate.
    bool bounded_inputs_complete()const{return captured&&retained&&model_count>0&&!order_truncated&&!model_truncated&&!allies_truncated;}
    template<class Emit>bool flush(int round,Emit emit) {
        if(!captured||flush_attempted||round<=state.round)return false;
        flush_attempted=true;emit(*this,round);return true;
    }
};
static_assert(std::is_trivially_copyable_v<State>&&std::is_trivially_copyable_v<Candidate>);
static_assert(std::is_trivially_copyable_v<Packet>);
inline void print(const Packet&p,int flush_round) {
    const auto f=[](float value){return std::bit_cast<unsigned>(value);};
    std::printf("p11exp actor=%d round=%d flush=%d fuel=%u pass=%d candidate=%d group=%d retained=%d preview_feasible=%d ordinary=%d order_pos=%d finite_by_membership=%d order_count=%d model_count=%d ally_count=%d truncated=%d%d%d bounded_complete=%d range=%d packing=%d origin=%d,%d anchor=%d,%d target=%d unavailable=%u monolith=%d visibility=unknown reward=unknown route=not_evaluated\n",
        p.actor,p.state.round,flush_round,p.detail_fuel,int(p.gate_passed),p.preview.id,p.native_group,int(p.retained),int(p.preview_feasible),int(p.ordinary_shortlist),p.order_position,int(p.finite_preview_by_membership()),p.order_count,p.model_count,p.ally_count,int(p.order_truncated),int(p.model_truncated),int(p.allies_truncated),int(p.bounded_inputs_complete()),p.range,int(p.packing_ready),p.origin_x,p.origin_y,p.anchor_x,p.anchor_y,p.target,p.unavailable_until,int(p.monolith));
    const auto&s=p.state;
    std::printf("p11exp_epoch checked=%d current=%d\n",int(p.round_checked),int(p.round_current));
    std::printf("p11exp_state round=%d limit=%d bag=%d altar=%d capture=%d recall=%d identity=%d gold=%d hp=%u maximum=%u strength=%u focus=%u monster_shards=%d jungle_shards=%d needed_gems=%d mature=%d field_shop=%d tribute_multiplier=%llu tribute_base=%d tribute_minimum=%d\n",
        s.round,s.limit,s.bag,s.altar,s.capture,s.recall,s.identity,s.gold,f(s.hp),f(s.maximum),f(s.strength),f(s.focus),s.monster_shards,s.jungle_shards,s.needed_gems,int(s.mature),int(s.field_shop),static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(s.tribute_multiplier)),s.tribute_base,s.tribute_minimum);
    const auto&c=p.preview;
    std::printf("p11exp_candidate id=%d travel=%d clear=%d capture=%d altar=%d gems=%d gold=%d allies=%d enemy_eta=%d renewal=%d age=%d loss=%u hp=%u incoming=%u ready=%d known=%d committed=%d\n",
        c.id,c.travel,c.clear,c.capture,c.altar,c.gems,c.gold,c.allies,c.enemy_eta,c.renewal,c.age,f(c.loss),f(c.hp),f(c.incoming),int(c.ready),int(c.known),int(c.committed));
    for(int i=0;i<std::min(p.order_count,int(p.order.size()));++i)std::printf("p11exp_order slot=%d id=%d\n",i,p.order[unsigned(i)]);
    for(int i=0;i<std::min(p.model_count,int(p.model.size()));++i){const auto&m=p.model[unsigned(i)];std::printf("p11exp_model member=%d hp=%u strength=%u approach=%d\n",i,f(m.hp),f(m.strength),m.approach);}
    for(int i=0;i<std::min(p.ally_count,int(p.allies.size()));++i){const auto&a=p.allies[unsigned(i)];std::printf("p11exp_ally member=%d x=%d y=%d\n",i,a.x,a.y);}
}
}
