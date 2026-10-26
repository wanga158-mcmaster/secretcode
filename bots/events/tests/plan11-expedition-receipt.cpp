#include <cassert>
#include <stdexcept>
#include <vector>
#include "../common/src/expedition_receipt.h"
int main(){
    using namespace p11::expedition;
    Packet packet;assert(packet.eligible(7,2)&&!packet.eligible(10,2)&&!packet.eligible(7,9));
    assert(packet.begin_attempt(7,2));
    p11::State state;state.round=2;state.identity=7;state.hp=1200;state.maximum=1200;state.strength=70;
    p11::Candidate candidate;candidate.id=14;candidate.travel=30;candidate.clear=12;candidate.loss=80;candidate.gems=11;
    packet.retain(candidate);packet.ordinary_shortlist=true;
    const std::vector<int>frontier{14,2,4};
    const std::vector<p11::Monster>model{{200,10,0},{500,0,0}};
    const std::vector<Position>positions{{2,12},{3,12},{4,12},{2,13}};
    packet.capture(state,frontier,model,positions,42000,false);
    assert(packet.captured&&!packet.gate_passed&&packet.detail_fuel==42000);
    assert(packet.state.hp==1200&&packet.preview.clear==12&&packet.preview.loss==80);
    assert(packet.order_position==0&&packet.finite_preview_by_membership()&&packet.bounded_inputs_complete());
    assert(packet.model[1].hp==500&&packet.allies[3].y==13);
    assert(!packet.round_checked&&!packet.round_current);
    assert(!packet.eligible(7,3));
    int emissions=0;
    assert(!packet.flush(2,[&](const auto&,int){++emissions;}));
    assert(packet.flush(3,[&](const auto&p,int round){assert(p.flush_attempted&&round==3);++emissions;}));
    assert(!packet.flush(4,[&](const auto&,int){++emissions;}));assert(emissions==1);

    Packet absent;absent.capture(state,frontier,{},positions,51000,true);
    assert(!absent.retained&&!absent.bounded_inputs_complete()&&!absent.finite_preview_by_membership());
    Packet excluded;excluded.retain(candidate);excluded.ordinary_shortlist=true;
    excluded.capture(state,std::vector<int>{1,2,4},model,positions,43000,false);
    assert(excluded.preview_feasible&&excluded.order_position==-1&&!excluded.finite_preview_by_membership());
    Packet nonordinary;nonordinary.retain(candidate);nonordinary.capture(state,frontier,model,positions,43000,false);
    assert(!nonordinary.finite_preview_by_membership());

    Packet truncated;truncated.retain(candidate);
    truncated.capture(state,std::vector<int>{0,1,2,14},std::vector<p11::Monster>(5),std::vector<Position>(5),49999,false);
    assert(truncated.order_truncated&&truncated.model_truncated&&truncated.allies_truncated&&!truncated.bounded_inputs_complete());
    assert(truncated.order_position==-1); // id14 lies beyond the stored frontier.
    Packet empty;empty.retain(candidate);empty.capture(state,frontier,{},positions,49999,false);assert(!empty.bounded_inputs_complete());
    // First attempt retains a candidate, then aborts before capture. A later
    // eligible attempt with no fallback must not combine old/new provenance.
    Packet retry;assert(retry.eligible(7,2));retry.retain(candidate);retry.ordinary_shortlist=true;
    assert(retry.eligible(7,3));state.round=3;
    retry.capture(state,frontier,{},positions,42000,false);
    assert(!retry.retained&&!retry.preview_feasible&&!retry.ordinary_shortlist&&retry.preview.id==-1);
    assert(!retry.finite_preview_by_membership()&&!retry.bounded_inputs_complete());
    Packet failed;failed.retain(candidate);failed.capture(state,frontier,model,positions,49999,false);
    try{failed.flush(4,[](const auto&,int){throw std::runtime_error("fixture write failed");});assert(false);}catch(const std::runtime_error&){}
    assert(failed.flush_attempted&&!failed.flush(5,[](const auto&,int){assert(false);}));
}
