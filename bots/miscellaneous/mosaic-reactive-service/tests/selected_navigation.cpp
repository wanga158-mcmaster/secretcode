#define MOSAIC_SELECTED_NAV_CORE_ONLY
#include "selected_navigation.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <vector>

namespace {
struct Pos { int x{},y{}; bool operator==(const Pos&) const = default; };
constexpr int side=8,count=64;
int index(Pos p) { return p.y*side+p.x; }
bool inside(Pos p) { return p.x>=0&&p.y>=0&&p.x<side&&p.y<side; }
struct Mask {
    std::uint64_t bits{};
    bool operator==(const Mask&) const = default;
    bool is_empty() const { return !bits; }
    bool is_set(Pos p) const { return inside(p)&&(bits&(std::uint64_t{1}<<index(p))); }
    void set(Pos p,bool on=true) {
        if(on)bits|=std::uint64_t{1}<<index(p);else bits&=~(std::uint64_t{1}<<index(p));
    }
    Mask& operator|=(const Mask& other) { bits|=other.bits;return *this; }
    Mask& operator&=(const Mask& other) { bits&=other.bits;return *this; }
    friend Mask operator~(Mask mask) { mask.bits=~mask.bits;return mask; }
};
struct Backend {
    struct Field {
        std::array<std::uint16_t,count> cells;
        static inline unsigned reads{};
        Field() { cells.fill(0xffff); }
        std::uint16_t get(Pos p) const { ++reads;return cells[index(p)]; }
    };
    int epoch{5},searches{},merges{},reachable_calls{};
    std::uint16_t last_cap{};
    std::uint64_t fuel{100000},chunk_count{1};
    bool drift_search{},drift_reachable{},drain_search{};
    bool same_epoch(int expected) const { return epoch==expected; }
    std::uint64_t remaining() const { return fuel; }
    std::uint64_t chunks(const Mask&) const { return chunk_count; }
    void search(Field& field,const Mask& blocked,const Mask& roots,Pos origin,
        Mask& visited,std::uint16_t cap) {
        ++searches;last_cap=cap;
        std::queue<Pos> pending;
        for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
            const Pos p{x,y};
            if(roots.is_set(p)&&!blocked.is_set(p)) {
                field.cells[index(p)]=0;visited.set(p);pending.push(p);
            }
        }
        while(!pending.empty()) {
            const auto p=pending.front();pending.pop();
            if(p==origin)break; // Single search destination, as in the native adapter.
            const auto d=field.get(p);
            if(d>=cap)continue;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                const Pos q{p.x+dx,p.y+dy};
                if((!dx&&!dy)||!inside(q)||blocked.is_set(q)||visited.is_set(q))continue;
                field.cells[index(q)]=d+1;visited.set(q);pending.push(q);
            }
        }
        if(drift_search)++epoch;
        if(drain_search)fuel=1000;
    }
    void merge(Field& target,const Field& source,const Mask& visited) {
        ++merges;
        for(int y=0;y<side;++y)for(int x=0;x<side;++x)
            if(visited.is_set({x,y}))target.cells[index({x,y})]=source.get({x,y});
    }
    std::vector<Pos> reachable(const Mask& blocked,Pos origin,int speed) {
        ++reachable_calls;
        if(drift_reachable)++epoch;
        std::vector<Pos> answer{origin};
        std::array<int,count> distance;distance.fill(-1);distance[index(origin)]=0;
        for(std::size_t i=0;i<answer.size();++i) {
            const auto p=answer[i];
            if(distance[index(p)]>=speed)continue;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                const Pos q{p.x+dx,p.y+dy};
                if((!dx&&!dy)||!inside(q)||blocked.is_set(q)||distance[index(q)]>=0)continue;
                distance[index(q)]=distance[index(p)]+1;answer.push_back(q);
            }
        }
        return answer;
    }
};
using namespace mosaic::selected_nav;
using Request=RequestT<Mask,Pos>;
using Service=ServiceT<Backend,Mask,Pos>;
using Failures=FailedEndpointsT<Pos>;
void require(bool ok,int line) {
    if(!ok){std::fprintf(stderr,"selected navigation assertion at %d\n",line);std::exit(1);}
}
#define CHECK(condition) require(bool(condition),__LINE__)
Mask goal(Pos p) { Mask mask;mask.set(p);return mask; }
Request request(const Mask& walls,const Mask& goals,Pos origin={0,0}) {
    return {walls,goals,origin,1,5,2,100,true,true,true};
}
Failures failures(const Request& r,const std::vector<Pos>& refused) {
    return {refused,r.epoch,r.actor,r.origin};
}

void conditional_refusal_sequence() {
    Mask walls;
    const auto goals=goal({4,0});
    auto r=request(walls,goals);
    Service service;
    CHECK(service.prepare(r,16).status==Status::ready);
    std::vector<Pos> refused;
    auto first=service.nominate(r,failures(r,refused));
    CHECK(first.status==Status::candidate&&first.destination);
    CHECK(first.conditional_only&&first.strictly_lower_potential);
    CHECK(first.destination_potential<first.origin_potential);
    CHECK(first.epoch==r.epoch&&first.actor==r.actor&&first.revision==r.revision&&first.origin==r.origin);
    // A hypothetical physical occupancy refusal belongs to the caller, not to
    // the static service. Recollection advances revision while exclusions hold.
    refused.push_back(*first.destination);++r.revision;
    const auto second=service.nominate(r,failures(r,refused));
    CHECK(second.status==Status::candidate&&second.destination!=first.destination);
    refused.push_back(*second.destination);++r.revision;
    const auto exhausted=service.nominate(r,failures(r,refused));
    CHECK(exhausted.status==Status::local_candidates_exhausted&&!exhausted.destination);
    CHECK(!exhausted.strictly_lower_potential&&exhausted.conditional_only);
    CHECK(service.backend().searches==1);
    // Refusals do not persist into another epoch or origin without caller proof.
    const auto stale=failures(r,refused);
    ++r.epoch;++r.revision;service.backend().epoch=r.epoch;
    CHECK(service.nominate(r,stale).status==Status::deferred);
    refused.clear();
    CHECK(service.nominate(r,failures(r,refused)).destination==first.destination);
    r.actor=9;++r.revision;
    CHECK(service.prepare(r,0).status==Status::ready);
    CHECK(service.nominate(r,failures(r,refused)).actor==9);
}

void quiet_continuation_and_merge() {
    Mask walls;
    const auto goals=goal({7,7});
    auto r=request(walls,goals);
    Service service;
    auto progress=service.prepare(r,2);
    CHECK(progress.status==Status::need_compute&&!progress.covered);
    CHECK(progress.attempted_depth==2&&progress.next_depth==3&&service.backend().searches==1);
    CHECK(service.prepare(r,2).status==Status::need_compute&&service.backend().searches==1);
    CHECK(service.prepare(r,1).status==Status::need_compute&&service.backend().searches==1);
    std::vector<Pos> empty;
    CHECK(service.nominate(r,failures(r,empty)).status==Status::need_compute);
    // A different origin may admit a smaller search; masked union must preserve
    // every earlier proved tile rather than shrink the cache to the new radius.
    r.origin={0,7};++r.revision;
    CHECK(service.prepare(r,1).status==Status::need_compute);
    CHECK(service.backend().merges==1);
    r.origin={5,5};++r.revision;
    CHECK(service.prepare(r,0).covered&&service.backend().searches==2);
    CHECK(service.nominate(r,failures(r,empty)).status==Status::candidate);
    r.origin={0,0};++r.revision;
    progress=service.prepare(r,4);
    CHECK(progress.status==Status::need_compute&&progress.next_depth==5);
    CHECK(service.prepare(r,3).status==Status::need_compute&&service.backend().searches==3);
    progress=service.prepare(r,7);
    CHECK(progress.status==Status::ready&&progress.covered&&service.backend().searches==4);
    CHECK(service.nominate(r,failures(r,empty)).origin_potential==7);
    // Computation crossing a synthetic epoch is never permission to act. Its
    // exact static facts can be reused by a fresh origin-wait packet later.
    Service crossing;
    crossing.backend().drift_search=true;
    CHECK(crossing.prepare(r,7).status==Status::deferred);
    ++r.epoch;++r.revision;crossing.backend().drift_search=false;
    CHECK(crossing.prepare(r,0).covered&&crossing.backend().searches==1);
    CHECK(crossing.nominate(r,failures(r,empty)).status==Status::candidate);
    Service delayed_merge;
    r.epoch=delayed_merge.backend().epoch;
    CHECK(delayed_merge.prepare(r,2).status==Status::need_compute);
    delayed_merge.backend().drain_search=true;
    CHECK(delayed_merge.prepare(r,7).status==Status::deferred);
    CHECK(delayed_merge.backend().merges==0); // No costly merge with only 1K left.
    delayed_merge.backend().drain_search=false;delayed_merge.backend().fuel=100000;
    CHECK(delayed_merge.prepare(r,0).covered);
    CHECK(delayed_merge.backend().searches==2&&delayed_merge.backend().merges==1);
    CHECK(delayed_merge.nominate(r,failures(r,empty)).status==Status::candidate);
}

void exact_keys_and_guard_failures() {
    Mask walls;
    walls.set({6,6});
    auto goals=goal({3,0});
    auto r=request(walls,goals);
    Service service;
    std::vector<Pos> empty;
    CHECK(service.prepare(r,8).covered);
    goals.set({6,6});
    CHECK(service.prepare(r,0).covered&&service.backend().searches==1);
    goals.set({6,6},false);
    walls.set({6,6},false);walls.set({6,5});
    CHECK(service.nominate(r,failures(r,empty)).status==Status::need_compute);
    CHECK(service.prepare(r,8).covered&&service.backend().searches==2);
    goals=goal({4,0});
    CHECK(service.prepare(r,0).status==Status::need_compute);
    CHECK(service.nominate(r,failures(r,empty)).status==Status::need_compute);
    CHECK(service.prepare(r,8).covered&&service.backend().searches==3);
    auto stale=failures(r,empty);stale.actor=12;
    CHECK(service.nominate(r,stale).status==Status::deferred);
    stale=failures(r,empty);stale.origin={1,0};
    CHECK(service.nominate(r,stale).status==Status::deferred);
    r.can_move=false;
    CHECK(service.nominate(r,failures(r,empty)).status==Status::unavailable);
    r.can_move=true;r.speed=5;
    CHECK(service.nominate(r,failures(r,empty)).status==Status::deferred);
    r.speed=1;r.complete=false;
    CHECK(service.prepare(r,8).status==Status::deferred);
    CHECK(service.nominate(r,failures(r,empty)).status==Status::deferred);
    r.complete=true;service.backend().fuel=20000;
    CHECK(service.nominate(r,failures(r,empty)).status==Status::deferred);
    service.backend().fuel=100000;service.backend().drift_reachable=true;
    const auto revoked=service.nominate(r,failures(r,empty));
    CHECK(revoked.status==Status::deferred&&!revoked.destination&&!revoked.strictly_lower_potential);
    service.backend().drift_reachable=false;r.epoch=service.backend().epoch;
    goals=goal(r.origin);
    CHECK(service.prepare(r,0).status==Status::arrived);
    CHECK(service.nominate(r,failures(r,empty)).status==Status::arrived);
    walls.set(r.origin);
    CHECK(service.prepare(r,0).status==Status::no_static_goal);
    CHECK(service.nominate(r,failures(r,empty)).status==Status::no_static_goal);

    Mask plain;const auto far=goal({7,7});auto admitted_request=request(plain,far);
    Service admitted;
    admitted.backend().fuel=50000;admitted.backend().chunk_count=169;
    CHECK(admitted.prepare(admitted_request,200).covered);
    CHECK(admitted.backend().last_cap==131);
    Service low;low.backend().fuel=26000;low.backend().chunk_count=169;
    CHECK(low.prepare(admitted_request,200).status==Status::need_compute);
    CHECK(low.backend().searches==0);
}

void read_only_potential() {
    Mask walls;
    auto goals=goal({4,0});
    auto r=request(walls,goals);
    Service service;
    CHECK(service.prepare(r,8).covered);
    const auto reads=Backend::Field::reads;
    const auto suffix=service.potential(r,Pos{2,0});
    CHECK(suffix&&suffix->value==2&&suffix->conditional_only);
    CHECK(suffix->epoch==r.epoch&&suffix->actor==r.actor&&suffix->revision==r.revision);
    CHECK(suffix->origin==r.origin&&suffix->point==Pos(2,0));
    CHECK(Backend::Field::reads==reads+1);
    const auto direct=service.potential(r);
    CHECK(direct&&direct->value==4&&direct->point==r.origin);
    const auto at_goal=service.potential(r,Pos{4,0});
    CHECK(at_goal&&at_goal->value==0);
    const auto proved_reads=Backend::Field::reads;
    CHECK(!service.potential(r,Pos{7,7})); // Bounded field has no value here.
    CHECK(Backend::Field::reads==proved_reads);
    goals=goal({3,0});
    CHECK(!service.potential(r,Pos{2,0}));
    CHECK(Backend::Field::reads==proved_reads);
    goals=goal({4,0});walls.set({6,6});
    CHECK(!service.potential(r,Pos{2,0}));
    CHECK(Backend::Field::reads==proved_reads);
    walls.set({6,6},false);
    ++service.backend().epoch;
    CHECK(!service.potential(r,Pos{2,0}));
    CHECK(Backend::Field::reads==proved_reads);
    r.epoch=service.backend().epoch;++r.revision;r.complete=false;
    CHECK(!service.potential(r,Pos{2,0}));
    r.complete=true;r.alive=false;
    CHECK(!service.potential(r,Pos{2,0}));
    r.alive=true;service.backend().fuel=20000;
    CHECK(!service.potential(r,Pos{2,0}));
    CHECK(Backend::Field::reads==proved_reads);
    service.backend().fuel=100000;r.can_move=false;
    const auto fresh=service.potential(r,Pos{2,0});
    CHECK(fresh&&fresh->revision==r.revision&&fresh->epoch==r.epoch);
    CHECK(service.backend().searches==1&&service.backend().reachable_calls==0);
}

void dated_hypothesis_is_not_actor_authority() {
    Mask walls;
    auto goals=goal({4,0});
    auto r=request(walls,goals);
    Service service;
    CHECK(service.prepare(r,8).covered);
    // No fresh alive/movement/body record is supplied by this entry point.
    HypothesisT<Mask,Pos> h{walls,goals,{0,0},1,5,2,700,true};
    const auto proposed=service.nominate_hypothesis(h);
    static_assert(decltype(proposed)::requires_fresh_capture);
    static_assert(!decltype(proposed)::actual_status_known);
    CHECK(proposed.proposal.status==Status::candidate&&proposed.proposal.destination);
    CHECK(proposed.proposal.conditional_only&&proposed.proposal.strictly_lower_potential);
    CHECK(proposed.proposal.origin==h.expected_origin&&proposed.proposal.epoch==h.epoch);
    CHECK(proposed.proposal.actor==h.actor&&proposed.proposal.revision==h.revision);
    CHECK(proposed.requires_fresh_capture&&!proposed.actual_status_known);
    const auto reads=Backend::Field::reads;
    goals=goal({3,0});
    CHECK(service.nominate_hypothesis(h).proposal.status==Status::need_compute);
    CHECK(Backend::Field::reads==reads);
    goals=goal({4,0});--h.epoch;
    CHECK(service.nominate_hypothesis(h).proposal.status==Status::deferred);
    CHECK(Backend::Field::reads==reads);
    ++h.epoch;h.expected_origin={7,7};
    CHECK(service.nominate_hypothesis(h).proposal.status==Status::need_compute);
    CHECK(Backend::Field::reads==reads);
    h.expected_origin={0,0};h.geometry_known=false;
    CHECK(service.nominate_hypothesis(h).proposal.status==Status::deferred);
    CHECK(Backend::Field::reads==reads);
    h.geometry_known=true;h.expected_speed=0;
    const auto unknown_body=service.nominate_hypothesis(h);
    CHECK(unknown_body.proposal.status==Status::unavailable&&!unknown_body.actual_status_known);
    CHECK(unknown_body.requires_fresh_capture&&!unknown_body.proposal.destination);
    CHECK(service.backend().searches==1); // Nomination never computes a field.
}
} // namespace

int main() {
    conditional_refusal_sequence();
    quiet_continuation_and_merge();
    exact_keys_and_guard_failures();
    read_only_potential();
    dated_hypothesis_is_not_actor_authority();
    std::puts("selected navigation: conditional refusals, quiet progress, read-only potentials and dated hypotheses pass");
}
