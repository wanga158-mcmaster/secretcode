// Actual Session guard/command composition with a selected-vision spy backend.
// This fixture proves no host import fuel or full-controller useful production.
#include "recon_execution.h"
#include <algorithm>
#include <array>
#include <cstdio>
using namespace tt::recon_execution;
namespace pr=tt::production_recon;
int checks{},failures{};
#define CHECK(x) do {++checks;if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(false)
struct Spy {
    static constexpr int width=12,height=5;
    Point pos{1,2};
    int round{70},moves{},legal_calls{},enemy_calls{},hypo_calls{},actual_calls{},position_calls{};
    int stale_position_call{},radius{2};
    Flags state{true,false,true};
    bool query_fuel{true},compute_fuel{true},step_fuel{true},enemy{},legal{true},accepted{true},lock_move{};
    bool auto_observe{true},epoch_in_hypo{},epoch_in_move{},position_in_legal{},epoch_in_legal{},no_effect{},reveal_blocker{};
    std::uint32_t actual_extra{},last_hypo_mask{};
    std::array<pr::Cell,width*height> cells{};
    std::array<std::uint32_t,width*height> views{};
    std::size_t index(Point p)const{return static_cast<std::size_t>(p.y*width+p.x);}
    int epoch()const{return round;}
    Point position(){if(++position_calls==stale_position_call)++round;return pos;}
    bool query_budget()const{return query_fuel;}
    bool compute_budget()const{return compute_fuel;}
    bool step_budget()const{return step_fuel;}
    Flags flags()const{return state;}
    bool quiet(Point p)const {
        return p.x>=0 && p.y>=0 && p.x<width && p.y<height && cells[index(p)]==pr::Cell::QuietOpen;
    }
    pr::Cell cell(Point p)const{return cells[index(p)];}
    std::uint32_t actual_visible(const Request&) {
        ++actual_calls;return actual_extra|(auto_observe?views[index(pos)]:0);
    }
    bool candidate(Point p,const Request& r)const {
        for(std::size_t i=0;i<r.member_count;++i)
            if((r.missing_members&(std::uint32_t{1}<<i)) &&
               std::max(std::abs(p.x-r.members[i].x),std::abs(p.y-r.members[i].y))<=radius)return true;
        return false;
    }
    std::uint32_t hypothetical_visible(Point p,const Request& r) {
        last_hypo_mask=r.missing_members;
        ++hypo_calls;if(epoch_in_hypo)++round;return views[index(p)];
    }
    bool enemies_present(){++enemy_calls;return enemy;}
    bool legal_step(Point,Point){++legal_calls;if(position_in_legal)++pos.y;if(epoch_in_legal)++round;return legal && state.can_move;}
    bool move(Point p) {
        ++moves;if(accepted && !no_effect)pos=p;
        if(accepted)state.can_move=false;
        if(!accepted && reveal_blocker)legal=false;
        if(epoch_in_move)++round;
        return accepted;
    }
    void next_turn(){++round;state.can_move=!lock_move;}
};
Request request(std::uint32_t mask=1) {
    Request r;r.key={7,11,13,17,19};r.member_count=2;
    r.members[0]={4,2};r.members[1]={9,2};r.missing_members=mask;return r;
}
Outcome step(Session& session,Spy& spy,Request& r) {
    const auto before=spy.hypo_calls;
    auto out=session.step(r,Spy::width,Spy::height,1000,spy);
    CHECK(spy.hypo_calls-before<=1);
    CHECK(out.hypothetical_queries<=1 && out.work_units<=256);
    return out;
}
Outcome advance(Session& s,Spy& spy,Request& r,int limit=1000) {
    Outcome out;
    for(int i=0;i<limit;++i) {
        out=step(s,spy,r);
        if(out.status!=Status::NeedCompute)return out;
        spy.next_turn();
    }
    CHECK(false);return out;
}
void actual_observation_and_negative_handshake() {
    Session s;Spy spy;auto r=request(3);
    spy.views[spy.index({3,2})]=1;spy.views[spy.index({8,2})]=2;
    auto out=advance(s,spy,r);
    CHECK(out.status==Status::Moved && out.moved && out.ordinary_walk && out.attempted);
    CHECK(out.hypothetical_queries==1); // positive nomination -> route -> actual step in this invocation
    CHECK((out.from==Point{1,2}) && out.to!=out.from && spy.enemy_calls>=2 && spy.legal_calls==1);
    bool first=false,second=false;
    std::size_t first_rank{};
    for(int i=0;i<1000;++i) {
        spy.next_turn();r.completed_negative=false;
        out=step(s,spy,r);
        if(out.status==Status::Observed) {
            CHECK(out.observed_members!=0 && out.handled);
            if(!first) {
                first=true;first_rank=out.attempted_origins;
                auto waiting=step(s,spy,r);
                CHECK(waiting.status==Status::AwaitingMenu && waiting.attempted_origins==first_rank);
                // Rounds/healing are not family inputs; no new start or rank reset.
                spy.next_turn();
                waiting=step(s,spy,r);
                CHECK(waiting.status==Status::AwaitingMenu && waiting.attempted_origins==first_rank);
            } else second=true;
            r.completed_negative=true;
            out=step(s,spy,r);
            CHECK(out.status!=Status::AwaitingMenu);
            if(second) {
                CHECK(out.status==Status::Resolved && !out.handled);break;
            }
        }
    }
    CHECK(first && second && spy.moves>0 && spy.last_hypo_mask==2);
    const auto terminal=step(s,spy,r);
    CHECK(terminal.status==Status::Resolved && terminal.attempted_origins>=first_rank);
    auto wrong_family=r;wrong_family.missing_members=1;
    CHECK(step(s,spy,wrong_family).status==Status::Stale);
}
void geometry_nomination_is_not_actual_visibility() {
    Session s;Spy spy;auto r=request();
    spy.auto_observe=false;spy.views[spy.index({3,2})]=1;
    Outcome out;
    for(int i=0;i<1000;++i) {
        spy.next_turn();out=step(s,spy,r);
        if(out.status==Status::AtFrontier)break;
    }
    CHECK(out.status==Status::AtFrontier && out.observed_members==0);
    const auto rank=out.attempted_origins;
    auto repeat=step(s,spy,r);
    CHECK(repeat.status==Status::AwaitingMenu && repeat.attempted_origins==rank);
    r.completed_negative=true;
    out=step(s,spy,r);
    CHECK(out.attempted_origins==rank+1 && out.status!=Status::AwaitingMenu);
}
void observed_corpse_is_only_information() {
    Session s;Spy spy;auto r=request();
    // The runtime reads visibility only, regardless of alive/dead/empty target.
    spy.actual_extra=1;
    auto out=step(s,spy,r);
    CHECK(out.status==Status::Observed && out.observed_members==1 && spy.moves==0);
    r.completed_negative=true;
    out=step(s,spy,r);
    CHECK(out.status==Status::Resolved && !out.handled && spy.moves==0);
    r.missing_members=0;
    CHECK(step(s,spy,r).status==Status::Unsupported && spy.moves==0);
    // A caller-confirmed new lifecycle generation must change the family.
    r.missing_members=1;++r.key.family;
    out=step(s,spy,r);
    CHECK(out.status==Status::Observed && out.observed_members==1);
}
void deferred_block_needs_explicit_new_fact() {
    Session s;Spy spy;auto r=request();spy.views[spy.index({3,2})]=1;
    spy.accepted=false;
    auto out=advance(s,spy,r);
    CHECK(out.status==Status::Blocked && out.attempted && !out.moved && !out.ordinary_walk && spy.moves==1);
    const auto rank=out.attempted_origins;
    const auto before_spent=spy.legal_calls;spy.state.can_move=false;
    out=step(s,spy,r);
    CHECK(out.status==Status::Unavailable && spy.legal_calls==before_spent && !out.observed_legal_transition);
    spy.next_turn();out=step(s,spy,r);
    CHECK(out.status==Status::Blocked && !out.handled && !out.attempted && spy.moves==1 && out.attempted_origins==rank);
    spy.accepted=true;r.retry_blocked=true;out=step(s,spy,r);
    CHECK(out.status==Status::Moved && spy.moves==2 && out.attempted_origins==rank);
    Session known;Spy blocked;auto r2=request();blocked.views[blocked.index({3,2})]=1;blocked.legal=false;
    out=advance(known,blocked,r2);
    CHECK(out.status==Status::Blocked && !out.attempted && blocked.moves==0);
    const auto known_rank=out.attempted_origins;
    blocked.next_turn();out=step(known,blocked,r2);
    CHECK(out.status==Status::Blocked && out.blocked_rechecked && !out.attempted);
    blocked.legal=true;blocked.next_turn();
    const auto legal_before=blocked.legal_calls;out=step(known,blocked,r2);
    CHECK(out.status==Status::Moved && out.blocked_rechecked && out.observed_legal_transition);
    CHECK(blocked.legal_calls==legal_before+1 && out.attempted_origins==known_rank);
    // A hidden refusal is followed by actual same-input revealed-blocker readback.
    Session hidden;Spy reveal;auto rr=request();reveal.views[reveal.index({3,2})]=1;
    reveal.accepted=false;reveal.reveal_blocker=true;
    out=advance(hidden,reveal,rr);
    CHECK(out.status==Status::Blocked && out.blocked_rechecked && reveal.legal_calls==2);
    reveal.next_turn();reveal.legal=true;reveal.accepted=true;
    out=step(hidden,reveal,rr);
    CHECK(out.status==Status::Moved && out.observed_legal_transition && reveal.moves==2);
}
void fresh_guards_and_bounds() {
    for(int guard=0;guard<9;++guard) {
        Session s;Spy spy;auto r=request();spy.views[spy.index({3,2})]=1;
        if(guard==0)spy.enemy=true;
        if(guard==1){spy.state.can_move=false;spy.lock_move=true;}
        if(guard==2)spy.query_fuel=false;
        if(guard==3)spy.compute_fuel=false;
        if(guard==4)spy.step_fuel=false;
        if(guard==5)spy.position_in_legal=true;
        if(guard==6)spy.epoch_in_hypo=true;
        if(guard==7)spy.state.channeling=true;
        if(guard==8)spy.state.alive=false;
        Outcome out;
        if(guard==2 || guard==3 || guard==4)out=step(s,spy,r);
        else out=advance(s,spy,r);
        CHECK(spy.moves==0);
        if(guard==0)CHECK(out.status==Status::Enemy && !out.handled);
        if(guard==1 || guard==7 || guard==8)CHECK(out.status==Status::Unavailable && !out.handled);
        if(guard==2 || guard==3 || guard==4)CHECK(out.status==Status::NeedCompute);
        if(guard==5 || guard==6)CHECK(out.status==Status::Stale);
    }
    Session suspended;Spy legal_stale;auto sr=request();
    legal_stale.views[legal_stale.index({3,2})]=1;legal_stale.legal=false;legal_stale.epoch_in_legal=true;
    auto suspended_out=advance(suspended,legal_stale,sr);
    CHECK(suspended_out.status==Status::Stale && !suspended_out.attempted && legal_stale.moves==0);
    Session stale;Spy spy;auto r=request();spy.views[spy.index({3,2})]=1;spy.epoch_in_move=true;
    auto out=advance(stale,spy,r);
    CHECK(out.status==Status::Stale && out.attempted && out.moved && out.ordinary_walk);
}
void enemy_during_compute_preserves_rank() {
    Session s;Spy spy;auto r=request();spy.auto_observe=false;
    spy.views[spy.index({3,2})]=1;spy.views[spy.index({4,1})]=1;
    Outcome out;
    for(int i=0;i<1000;++i) {
        out=step(s,spy,r);
        if(out.status==Status::AtFrontier)break;
        spy.next_turn();
    }
    CHECK(out.status==Status::AtFrontier);
    r.completed_negative=true;spy.compute_fuel=false;
    out=step(s,spy,r);
    CHECK(out.status==Status::NeedCompute && out.attempted_origins==2 && s.active());
    const auto rank=out.attempted_origins;
    const auto query_count=spy.hypo_calls,move_count=spy.moves;
    spy.enemy=true;spy.compute_fuel=true;spy.next_turn();
    out=step(s,spy,r);
    CHECK(out.status==Status::Enemy && !out.handled && !out.attempted && out.work_units==0);
    CHECK(out.attempted_origins==rank && spy.hypo_calls==query_count && spy.moves==move_count && s.active());
    // Actual fresh enemy absence resumes this SAME named search, not a new BFS.
    spy.enemy=false;r.completed_negative=false;spy.next_turn();
    out=advance(s,spy,r);
    CHECK(out.status==Status::Moved && out.attempted_origins==rank && spy.moves==move_count+1);
}
void honest_family_exhaustion_and_restart() {
    Session s;Spy spy;auto r=request();spy.radius=0;
    auto out=advance(s,spy,r);
    CHECK(out.status==Status::Exhausted && !out.handled && spy.moves==0);
    const auto old_hypo=spy.hypo_calls;
    spy.next_turn();out=step(s,spy,r);
    CHECK(out.status==Status::Exhausted && spy.hypo_calls==old_hypo);
    // A genuinely changed capability/goal family can begin fresh, not a new round.
    ++r.key.family;spy.radius=2;spy.views[spy.index({3,2})]=1;
    out=advance(s,spy,r);
    CHECK(out.status==Status::Moved);
    s.clear();CHECK(!s.active());
    Session unknown;Spy u;auto ur=request();u.cells[u.index(u.pos)]=pr::Cell::Unknown;
    out=step(unknown,u,ur);CHECK(out.status==Status::NeedObservation && !out.handled);
}
int main() {
    CHECK(valid_request(request(),12,5));
    auto invalid=request();invalid.missing_members=4;CHECK(!valid_request(invalid,12,5));
    invalid=request();invalid.member_count=33;CHECK(!valid_request(invalid,12,5));
    auto all=request();all.member_count=32;all.missing_members=std::uint32_t{1}<<31;
    for(auto& p:all.members)p={4,2};
    CHECK(valid_request(all,12,5));
    actual_observation_and_negative_handshake();
    geometry_nomination_is_not_actual_visibility();
    observed_corpse_is_only_information();
    deferred_block_needs_explicit_new_fact();
    fresh_guards_and_bounds();
    enemy_during_compute_preserves_rank();
    honest_family_exhaustion_and_restart();
    std::printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
