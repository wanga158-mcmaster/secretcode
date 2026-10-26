#pragma once
#include "../../distributed-common/src/observe.h"
namespace p8::finite_counterprotocol {
enum class Response { unknown,approach,stay };
enum class ResponseFailure { none,identity,visibility,dead,timing,path,changed_site,interrupted };
struct Site {
    int opportunity=-1,altar=-1,width=0,height=0;
    p6::Point altar_tile{},target{};
    std::array<p6::Point,2> own_approaches{};
    int approach_count=0;
    p6::Fact<std::uint64_t> map_identity{};
};
inline bool map_source(p6::Source source) { return source==p6::Source::public_map||source==p6::Source::synthetic_fixture; }
inline bool site_valid(const Site& site) {
    if (site.opportunity<0||site.altar<0||site.width<1||site.height<1||site.width>1000||site.height>1000||
        !site.map_identity.value||!map_source(site.map_identity.source)||site.approach_count<1||site.approach_count>2) return false;
    const auto inside=[&](p6::Point p) {return p.x>=0&&p.y>=0&&p.x<site.width&&p.y<site.height;};
    if (!inside(site.altar_tile)||!inside(site.target)) return false;
    bool named=false;
    for (int i=0;i<site.approach_count;++i) {
        const auto p=site.own_approaches[i];
        if (!inside(p)||p==site.altar_tile||std::abs(p.x-site.altar_tile.x)>1||std::abs(p.y-site.altar_tile.y)>1) return false;
        named|=p==site.target;
        for (int j=0;j<i;++j) if (p==site.own_approaches[j]) return false;
    }
    return named;
}
inline bool same_site(const Site& a,const Site& b) {
    return a.opportunity==b.opportunity&&a.altar==b.altar&&a.width==b.width&&a.height==b.height&&a.altar_tile==b.altar_tile&&
        a.target==b.target&&a.own_approaches==b.own_approaches&&a.approach_count==b.approach_count&&
        a.map_identity.value==b.map_identity.value&&a.map_identity.source==b.map_identity.source;
}
// Audited static-map seam, not a Chebyshev estimate or a claim about hidden
// movement. Native callers must supply the exact queried-map shortest distance
// and current path/obstruction knowledge; unsupported paths stay unknown.
struct StaticDistance {
    int opportunity=-1;
    std::uint64_t map_identity=0;
    p6::Point from{},target{};
    p6::Fact<int> distance{};
    bool shortest_path_known=false,unobstructed=false;
};
using DistanceObservation=p6::Observation<StaticDistance>;
inline ResponseFailure sample_failure(const p6::Frame& frame,const Site& site,int rival,const DistanceObservation& observed) {
    if (!frame.valid()||!frame.own.actor.alive) return ResponseFailure::dead;
    if (!site_valid(site)||observed.value.opportunity!=site.opportunity||observed.value.map_identity!=site.map_identity.value||
        !(observed.value.target==site.target)) return ResponseFailure::changed_site;
    if (rival<0||observed.subject!=rival) return ResponseFailure::identity;
    const auto* actor=frame.visible(rival);
    if (!actor||!observed.current(frame.clock,rival)) return ResponseFailure::visibility;
    if (actor->id!=rival||actor->team!=p6::Team::enemy) return ResponseFailure::identity;
    if (!actor->alive||actor->health<=0) return ResponseFailure::dead;
    const auto& d=observed.value;
    if (d.from!=actor->position||d.from==frame.own.actor.position||d.from.x<0||d.from.y<0||d.from.x>=site.width||d.from.y>=site.height||
        !d.shortest_path_known||!d.unobstructed||!map_source(d.distance.source)||d.distance.value<0||
        d.distance.value>=site.width*site.height||d.distance.value<std::max(std::abs(d.from.x-site.target.x),std::abs(d.from.y-site.target.y))) return ResponseFailure::path;
    return ResponseFailure::none;
}
struct Encounter {
    std::uint32_t serial=0;
    int owner=-1,rival=-1,opportunity=-1,baseline_round=-1,terminal_round=-1,next_observations=0;
    std::uint64_t map_identity=0;
    std::array<p6::Point,4> positions{};
    std::array<int,4> distances{-1,-1,-1,-1};
    Response response=Response::unknown;
    ResponseFailure failure=ResponseFailure::none;
    Site named_site{};
};
struct ResponseWindow {
    Encounter record{}; Site site{};
    bool active=false,finished=false,reduced=false;
    int last_round=-1;
    void unknown(ResponseFailure reason,int round) {
        record.response=Response::unknown; record.failure=reason; record.terminal_round=round; active=false; finished=true;
    }
    bool begin(const p6::Frame& frame,const Site& named,int rival,const DistanceObservation& observed,std::uint32_t serial=1) {
        if (active||!serial) return false;
        record={}; site=named; record.serial=serial; record.owner=frame.clock.self; record.rival=rival;
        record.named_site=named;
        record.opportunity=named.opportunity; record.map_identity=named.map_identity.value; record.baseline_round=frame.clock.round;
        last_round=frame.clock.round; active=true; finished=reduced=false;
        const auto reason=sample_failure(frame,named,rival,observed);
        if (reason!=ResponseFailure::none) { unknown(reason,frame.clock.round); return false; }
        record.positions[0]=observed.value.from; record.distances[0]=observed.value.distance.value; return true;
    }
    void observe(const p6::Frame& frame,const DistanceObservation& observed) {
        if (!active||frame.clock.round==last_round) return;
        if (frame.clock.self!=record.owner||frame.clock.round!=last_round+1) { unknown(ResponseFailure::timing,frame.clock.round); return; }
        const auto reason=sample_failure(frame,site,record.rival,observed);
        if (reason!=ResponseFailure::none) { unknown(reason,frame.clock.round); return; }
        const int i=++record.next_observations;
        record.positions[i]=observed.value.from; record.distances[i]=observed.value.distance.value;
        // Reduction is between consecutive personally observed distances. This
        // labels a movement segment, not motive, response to us, or hidden acts.
        reduced|=record.distances[i]<record.distances[i-1]; last_round=frame.clock.round;
        if (i==3) { record.response=reduced?Response::approach:Response::stay; record.terminal_round=last_round; active=false; finished=true; }
    }
};
class ResponseBook {
    std::array<Encounter,4> history_{};
    int next_=0,owner_=-1,rival_=-1,opportunity_=-1,last_matching_=-1,latest_terminal_=-1;
    std::uint64_t map_=0;
    Site matching_site_{};
    std::uint32_t serial_=1,delivered_=0;
    std::uint16_t encounters_=0,unknowns_=0,matching_=0;
    Response rule_=Response::unknown;
    void consume() {
        if (!window.finished||window.record.serial==delivered_) return;
        delivered_=window.record.serial; const auto& r=window.record; history_[next_]=r; next_=(next_+1)%4;
        latest_terminal_=std::max(latest_terminal_,r.terminal_round);
        if (encounters_!=65535) ++encounters_;
        if (r.response==Response::unknown) { if (unknowns_!=65535) ++unknowns_; clear_permission(); return; }
        const bool same=r.owner==owner_&&r.rival==rival_&&r.opportunity==opportunity_&&r.map_identity==map_&&same_site(r.named_site,matching_site_)&&r.response==rule_&&
            last_matching_>=0&&r.terminal_round>last_matching_&&r.terminal_round-last_matching_<64;
        matching_=same?std::uint16_t(std::min(65535,int(matching_)+1)):1;
        owner_=r.owner; rival_=r.rival; opportunity_=r.opportunity; map_=r.map_identity; matching_site_=r.named_site; rule_=r.response; last_matching_=r.terminal_round;
    }
public:
    ResponseWindow window{};
    std::uint16_t encounters() const { return encounters_; }
    std::uint16_t unknowns() const { return unknowns_; }
    std::uint16_t matching() const { return matching_; }
    int latest() const { return last_matching_; }
    Response rule() const { return rule_; }
    const std::array<Encounter,4>& history() const { return history_; }
    void clear_permission() { matching_=0; rule_=Response::unknown; last_matching_=-1; }
    bool begin(const p6::Frame& frame,const Site& site,int rival,const DistanceObservation& observed) {
        if (window.active||!serial_||frame.clock.round<=latest_terminal_) return false;
        const auto current=serial_++; const bool started=window.begin(frame,site,rival,observed,current); consume(); return started;
    }
    void observe(const p6::Frame& frame,const DistanceObservation& observed) { window.observe(frame,observed); consume(); }
    void interrupted(int round) { if (window.active) { window.unknown(ResponseFailure::interrupted,round); consume(); } else clear_permission(); }
    bool approach_current(int owner,int round) const {
        return !window.active&&owner==owner_&&rule_==Response::approach&&matching_>=2&&last_matching_>=0&&round>=last_matching_&&round-last_matching_<64;
    }
};
} // namespace p8::finite_counterprotocol
