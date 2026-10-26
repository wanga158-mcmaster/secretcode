#define TT_NAVIGATION_FIXTURE
#include "../src/independent_navigation.cpp"
#include <cstdio>
#include <memory>

namespace {
struct Point { int x{},y{}; friend bool operator==(Point,Point)=default; };
struct Actor
{
    Point pos{17,29};
    bool allowed{true},accepted{true};
    int absolute_calls{},relative_calls{};
    Point last{};
    Point position() const { return pos; }
    bool can_move() const { return allowed; }
    bool move_absolute(Point destination)
    { ++absolute_calls; last=destination; if(accepted)pos=destination; return accepted; }
    bool move(Point delta)
    { ++relative_calls; pos={pos.x+delta.x,pos.y+delta.y}; return true; }
};
struct PositionSuspensionActor : Actor
{
    int* epoch{};
    mutable int position_queries{};
    Point position() const
    {
        ++position_queries;
        if (position_queries==2) ++*epoch;
        return pos;
    }
};
int failures{};
void check(bool value,const char* label) { if(!value){ ++failures; std::printf("FAIL %s\n",label); } }

struct GeometrySpy
{
    using Stage=tt::navigation_detail::RouteStage;
    using Result=tt::navigation_detail::GeometryResult;
    struct Native {std::vector<std::uint16_t> values{65535,0,1,2};};
    std::shared_ptr<Native> native;
    std::vector<std::uint16_t> published;
    std::size_t wall_count{600},root_count{5};
    int wall_begins{},root_begins{},wall_units{},root_units{},native_calls{},export_calls{};
    bool current{true},wall_budget{true},root_budget{true},native_budget{true},export_budget{true};
    bool root_present{true},suspend_native{},suspend_export{},suspend_root_finish{},malformed_export{},unreachable{};
    bool fresh() const {return current;}
    bool headroom(Stage stage) const {
        if(stage==Stage::Walls)return wall_budget;
        if(stage==Stage::Roots)return root_budget;
        if(stage==Stage::Native)return native_budget;
        return export_budget;
    }
    void begin_walls(){++wall_begins;}
    bool walls_done(const tt::navigation_detail::RouteProgress& p) const{return p.group>=wall_count;}
    void wall_unit(tt::navigation_detail::RouteProgress& p){++p.group;++wall_units;}
    void begin_roots(){++root_begins;}
    bool roots_done(const tt::navigation_detail::RouteProgress& p) const{return p.root>=root_count;}
    void root_unit(tt::navigation_detail::RouteProgress& p){++p.root;++root_units;}
    bool finish_roots(){if(suspend_root_finish)current=false;return root_present;}
    Result compute(){
        ++native_calls;native=std::make_shared<Native>();
        if(suspend_native)current=false;
        return unreachable?Result::Unreachable:Result::Ready;
    }
    Result export_field(){
        ++export_calls;
        if(!native)return Result::NeedObservation;
        if(suspend_export)current=false;
        if(malformed_export)return Result::NeedObservation;
        published=native->values;return Result::Ready;
    }
};

void retained_route_geometry_contract()
{
    using namespace tt::navigation_detail;
    GeometrySpy spy;RouteProgress progress;
    check(advance_route_geometry(progress,spy)==GeometryResult::NeedCompute &&
          progress.stage==RouteStage::Walls && progress.group==route_units && spy.wall_begins==1,
          "staged walls execute bounded units and retain their exact cursor");
    spy.wall_budget=false;
    check(advance_route_geometry(progress,spy)==GeometryResult::NeedCompute && progress.group==route_units &&
          spy.wall_units==static_cast<int>(route_units) && spy.wall_begins==1,
          "insufficient wall headroom preserves work without restarting masks");
    spy.wall_budget=true;
    advance_route_geometry(progress,spy);
    check(progress.group==2*route_units && spy.wall_begins==1,"next wall input resumes the retained cursor");
    check(advance_route_geometry(progress,spy)==GeometryResult::NeedCompute && progress.stage==RouteStage::Roots &&
          spy.wall_units==600 && spy.root_begins==0 && spy.native_calls==0,
          "completed wall phase never begins roots or native work in that input");
    check(advance_route_geometry(progress,spy)==GeometryResult::NeedCompute && progress.stage==RouteStage::Native &&
          spy.root_begins==1 && spy.root_units==5 && spy.native_calls==0,
          "completed roots own an input and publish no field");
    spy.native_budget=false;
    check(advance_route_geometry(progress,spy)==GeometryResult::NeedCompute && !spy.native &&
          progress.stage==RouteStage::Native,"native reserve withdrawal retains completed masks and roots");
    spy.native_budget=true;
    check(advance_route_geometry(progress,spy)==GeometryResult::NeedCompute && progress.stage==RouteStage::Export &&
          spy.native_calls==1 && spy.export_calls==0 && spy.published.empty(),
          "completed native owner is retained while Ready and export remain withheld");
    const auto owner=spy.native.get();std::weak_ptr<GeometrySpy::Native> weak=spy.native;
    spy.export_budget=false;
    check(advance_route_geometry(progress,spy)==GeometryResult::NeedCompute && spy.native.get()==owner &&
          !weak.expired() && spy.native_calls==1 && spy.export_calls==0,
          "separate export budget seam preserves the exact native owner without recomputation");
    spy.export_budget=true;
    check(advance_route_geometry(progress,spy)==GeometryResult::Ready && progress.stage==RouteStage::Published &&
          spy.native.get()==owner && spy.native_calls==1 && spy.export_calls==1 &&
          spy.published==std::vector<std::uint16_t>({65535,0,1,2}),
          "only complete exported shape publishes field including a dash-useful sentinel origin");
    check(advance_route_geometry(progress,spy)==GeometryResult::Ready && spy.native_calls==1 &&
          spy.export_calls==1 && spy.wall_begins==1 && spy.root_begins==1,
          "published repeated entry performs no completed native or geometry work twice");
    spy.native.reset();
    check(weak.expired(),"dropping the retained owner releases native storage without a borrowed invocation lifetime");

    GeometrySpy stale;stale.wall_count=stale.root_count=0;RouteProgress drift;
    advance_route_geometry(drift,stale);advance_route_geometry(drift,stale);
    stale.suspend_native=true;
    check(advance_route_geometry(drift,stale)==GeometryResult::Stale && drift.stage==RouteStage::Withdrawn &&
          stale.native_calls==1 && stale.export_calls==0 && stale.published.empty(),
          "same-position native suspension withdraws instead of exporting an old owner on resumed input");
    stale.current=true;
    check(advance_route_geometry(drift,stale)==GeometryResult::NeedObservation && stale.export_calls==0,
          "withdrawn native work cannot resume as a new dated route without a new owner");

    GeometrySpy shape;shape.wall_count=shape.root_count=0;RouteProgress invalid;
    advance_route_geometry(invalid,shape);advance_route_geometry(invalid,shape);advance_route_geometry(invalid,shape);
    shape.malformed_export=true;
    check(advance_route_geometry(invalid,shape)==GeometryResult::NeedObservation &&
          invalid.stage==RouteStage::Withdrawn && shape.published.empty(),
          "malformed full export never turns a native-complete route Ready");

    GeometrySpy roots;roots.wall_count=roots.root_count=0;roots.root_present=false;RouteProgress empty;
    advance_route_geometry(empty,roots);roots.suspend_root_finish=true;
    check(advance_route_geometry(empty,roots)==GeometryResult::Stale && roots.native_calls==0,
          "freshness withdrawal wins over an empty-root result after a suspending native query");
}

void retained_route_key_contract()
{
    using namespace tt::navigation_detail;
    int mask_a{},mask_b{},empty_a{},empty_b{};
    RouteKey<Point> key{0,19,std::uint64_t{0x100000001},{8,13},&mask_a,&empty_a,{{12,14},{13,14}},{0,1,2}};
    const auto test=[&](RouteKey<Point> changed){return !same_route_key(key,changed);};
    check(same_route_key(key,key),"unchanged frozen request key resumes across later quiet owner inputs");
    auto changed=key;++changed.kind;check(test(changed),"route kind drift withdraws retained geometry");
    changed=key;++changed.group;check(test(changed),"named group drift withdraws retained geometry");
    changed=key;changed.graph=1;check(test(changed),"full-width graph revision drift withdraws retained geometry");
    changed=key;++changed.origin.x;check(test(changed),"actual origin drift withdraws retained geometry");
    changed=key;changed.goal_owner=&mask_b;check(test(changed),"goal mask owner replacement withdraws retained geometry");
    changed=key;changed.empty_owner=&empty_b;check(test(changed),"opening mask owner replacement withdraws retained geometry");
    changed=key;++changed.roots[0].y;check(test(changed),"same-size explicit root value mutation withdraws retained geometry");
    changed=key;changed.roots.push_back({14,14});check(test(changed),"root membership drift withdraws retained geometry");
    changed=key;++changed.known[1];check(test(changed),"known-availability opening drift withdraws retained geometry");
}

void planning_two_turn_contract()
{
    using namespace tt::navigation_detail;
    constexpr std::uint64_t owner_turn_fuel=100000,observation_cost=36000,assumed_planning_cost=62000;
    struct CachedRequest { bool pending{},ready{}; Point origin; int group{}; } request;
    request.origin={17,29}; request.group=6;
    // Turn N observes useful work, constructs a request, and lacks planning headroom.
    // Repeating this same late call next turn would repeat NeedCompute indefinitely.
    const auto after_observation=owner_turn_fuel-observation_cost;
    request.pending=true;
    check(!planning_headroom(after_observation) && request.pending && !request.ready,
          "turn N defers and retains a request after observation");
    check(!planning_headroom(owner_turn_fuel-observation_cost),
          "repeating observation first would starve the same request");
    // Turn N+1 owns the cached planning phase before optional observation. Given the
    // stated finite cost premise, it can publish a graph while retaining the tail.
    // This model fixture establishes the scheduling contract, not native cost safety.
    std::uint64_t remaining=owner_turn_fuel;
    bool observed=false,physical_command=false;
    int planning_calls=0;
    if (request.pending && planning_headroom(remaining))
    {
        ++planning_calls;
        remaining-=assumed_planning_cost;
        request.ready=true; request.pending=false;
    }
    check(request.ready && !request.pending && planning_calls==1 && remaining>14000,
          "turn N+1 completes the retained request in its fresh planning phase");
    check(!observed && !physical_command && request.origin==Point{17,29} && request.group==6,
          "planning publishes no movement and retains the conditional origin and job");
}

void post_action_movement_contract()
{
    using namespace tt::navigation_detail;
    enum class Frame { Ready, NeedObservation, NeedCompute, Stale };
    Frame frame=Frame::NeedObservation;
    Actor actor;
    int epoch=53,mask_reads=0;
    const Point origin=actor.pos,destination{18,30};
    MoveResult result=MoveResult::Illegal;
    if (accepts_movement_frame(frame,Frame::Ready,Frame::NeedObservation))
        result=apply_destination(actor,origin,destination,epoch,[&]{return epoch;},
            [&](Point){++mask_reads; return true;});
    check(result==MoveResult::Applied && actor.pos==destination && mask_reads==1,
          "post-basic NeedObservation permits freshly guarded same-turn egress");
    check(frame==Frame::NeedObservation && actor.absolute_calls==1 && actor.relative_calls==0,
          "egress never republishes global observation Ready");
    actor=Actor{}; actor.allowed=false;
    check(apply_destination(actor,origin,destination,epoch,[&]{return epoch;},[](Point){return true;})==MoveResult::Unavailable,
          "post-action frame cannot restore spent ranged movement allowance");
    check(!accepts_movement_frame(Frame::NeedCompute,Frame::Ready,Frame::NeedObservation) &&
          !accepts_movement_frame(Frame::Stale,Frame::Ready,Frame::NeedObservation),
          "incomplete or stale frame cannot enter fresh movement refinement");
}

void final_position_query_epoch_contract()
{
    using namespace tt::navigation_detail;
    int epoch=71;
    PositionSuspensionActor actor;
    actor.epoch=&epoch;
    const Point origin=actor.pos,destination{18,30};
    const auto result=apply_destination(actor,origin,destination,71,[&]{return epoch;},[](Point){return true;});
    check(result==MoveResult::Stale && epoch==72 && actor.position_queries==2 && actor.pos==origin &&
          actor.absolute_calls==0 && actor.relative_calls==0,
          "unchanged-origin final position query suspension revokes the move before command");
}

void static_proposal_contract()
{
    using namespace tt::navigation_detail;
    constexpr auto unknown=std::numeric_limits<std::uint16_t>::max();
    std::vector<std::uint16_t> field(25,unknown);
    const Point origin{2,2};
    field[12]=3; field[17]=2; field[18]=2; field[11]=2;
    const auto no_refusal=[](Point){return false;};
    const auto propose=[&](const auto& values,Point actual,int stamp,int epoch,bool ready=true,bool eligible=true) {
        return field_proposals(values,5,5,origin,actual,stamp,epoch,ready,eligible,no_refusal);
    };
    auto proposals=propose(field,origin,61,61);
    check(proposals.size()==3 && proposals[0]==Point{2,3} && proposals[1]==Point{3,3} && proposals[2]==Point{1,2},
          "static proposals use absolute adjacent destinations in native compass order");
    Actor actor; actor.pos=origin;
    check(!proposals.empty() && apply_destination(actor,origin,proposals.front(),61,[]{return 61;},[](Point){return true;})==MoveResult::Applied &&
          actor.pos==Point{2,3} && actor.relative_calls==0 && actor.absolute_calls==1,
          "actual proposal caller still applies an absolute destination");
    check(propose(field,origin,61,62).empty() && propose(field,Point{3,2},61,61).empty(),
          "proposal authority rejects stale epochs and displaced origins");
    check(propose(field,origin,61,61,false).empty() && propose(field,origin,61,61,true,false).empty(),
          "unready route and ineligible frame publish no proposals");
    auto invalid=field; invalid.pop_back();
    check(propose(invalid,origin,61,61).empty(),"invalid exported field shape publishes no proposals");
    auto arrived=field; arrived[12]=0;
    check(propose(arrived,origin,61,61).empty(),"distance zero is arrival rather than a proposed move");
    auto unknown_origin=field; unknown_origin[12]=unknown;
    check(propose(unknown_origin,origin,61,61).empty(),"unknown distance never gets a geometric fallback");
    auto filtered=field_proposals(field,5,5,origin,origin,61,61,true,true,
        [](Point tile){return tile==Point{2,3};});
    check(filtered.size()==2 && filtered.front()==Point{3,3},"round-scoped refusal removes only its proposed destination");
    actor=Actor{}; actor.pos=origin; actor.allowed=false;
    check(!proposals.empty() && apply_destination(actor,origin,proposals.front(),61,[]{return 61;},[](Point){return true;})==MoveResult::Unavailable,
          "static proposal does not assert current movement allowance");
}

void joint_capture_phase_contract()
{
    using namespace tt::navigation_detail;
    const Point origin{65,6};
    struct Certificate { int group{-1},revision{-1}; Point origin{-1,-1}; bool pending{},published{}; } certificate;
    int native_fields=0,exports=0,work_fields=0;
    auto key_matches=[&] {return capture_key_matches(certificate.group,certificate.revision,certificate.origin,6,9,origin);};
    check(capture_phase(key_matches(),certificate.pending,certificate.published)==CapturePhase::Native,
          "new capture key requires native computation");
    ++native_fields; certificate={6,9,origin,true,false};
    check(!certificate.published && capture_phase(key_matches(),certificate.pending,certificate.published)==CapturePhase::Export &&
          native_fields==1 && exports==0 && work_fields==0,
          "completed native capture field remains unpublished until a separate export phase");
    ++exports;certificate.pending=false;certificate.published=true;
    check(capture_phase(key_matches(),certificate.pending,certificate.published)==CapturePhase::Published && work_fields==0,
          "capture export publication yields before Work field construction");
    if(capture_phase(key_matches(),certificate.pending,certificate.published)==CapturePhase::Published)++work_fields;
    check(native_fields==1 && exports==1 && work_fields==1 &&
          capture_phase(key_matches(),certificate.pending,certificate.published)==CapturePhase::Published,
          "repeated same-key planning computes Work without exporting capture twice");
    check(!capture_key_matches(certificate.group,certificate.revision,certificate.origin,7,9,origin) &&
          !capture_key_matches(certificate.group,certificate.revision,certificate.origin,6,10,origin) &&
          !capture_key_matches(certificate.group,certificate.revision,certificate.origin,6,9,Point{64,7}),
          "capture certificate expires on group graph-revision or origin change");
    check(capture_phase(false,certificate.pending,certificate.published)==CapturePhase::Native,
          "key change cannot inherit old vector publication");
    check(!capture_key_matches(6,std::uint64_t{0x100000001},origin,6,std::uint64_t{1},origin),
          "capture revision key retains all64bits");
}

void capture_frontier_coverage_contract()
{
    using namespace tt::navigation_detail;
    constexpr auto unknown=std::numeric_limits<std::uint16_t>::max();
    std::vector<std::uint16_t> field(100,unknown);
    const Point old_origin{1,1},frontier{5,1},root{3,1};
    field[11]=2;field[12]=1;field[13]=0;
    struct Check {Point frontier{-1,-1},position{-1,-1};std::uint16_t remaining{unknown};bool started{};} retained;
    const auto roots=[&](Point point){return point==root;};
    const auto advance=[&](Point point,std::size_t units,bool fuel=true) {
        return advance_capture_gradient(field,10,10,point,retained.frontier,retained.position,
            retained.remaining,retained.started,roots,units,[&]{return fuel;});
    };
    check(advance(old_origin,3)==CaptureCoverage::Ready,"old quiet origin has a complete published gradient to selected group root");
    check(advance(frontier,3)==CaptureCoverage::Unknown,
          "old-origin compute_to Ready does not claim coverage at an uncovered future Work frontier");
    // Model the later native solve+export under the SAME retained graph. Only
    // these new finite cells become available; no dynamic occupancy is inferred.
    field[15]=2;field[14]=1;retained=Check{};
    check(advance(frontier,0)==CaptureCoverage::NeedCompute && retained.position==frontier,
          "zero pure units cannot publish frontier root coverage");
    check(advance(frontier,1,false)==CaptureCoverage::NeedCompute && retained.position==frontier,
          "budget withdrawal retains frontier check without progress or publication");
    check(advance(frontier,1)==CaptureCoverage::NeedCompute && retained.position==Point{4,1},
          "first bounded coverage input retains its strict d minus1 cursor");
    check(advance(frontier,1)==CaptureCoverage::NeedCompute && retained.position==root,
          "arrival at zero still requires the explicit selected-region root check");
    check(advance(frontier,1)==CaptureCoverage::Ready,
          "only complete strict gradient to actual group region publishes frontier Ready");
    retained=Check{};
    check(advance_capture_gradient(field,10,10,frontier,retained.frontier,retained.position,
          retained.remaining,retained.started,[](Point){return false;},3,[]{return true;})==CaptureCoverage::Unknown,
          "distance zero without the selected actual group root premise is Unknown");
    field[14]=unknown;retained=Check{};
    check(advance(frontier,3)==CaptureCoverage::Unknown,"malformed finite frontier gradient cannot borrow old-origin proof");
    field.pop_back();retained=Check{};
    check(advance(frontier,3)==CaptureCoverage::Unknown,"truncated exported field cannot publish frontier coverage");
}

void complete_approach_contract()
{
    using namespace tt::navigation_detail;
    constexpr auto unknown=std::numeric_limits<std::uint16_t>::max();
    std::vector<std::uint16_t> field(100,unknown);
    const Point origin{1,5},first{2,5},frontier{4,5},wolf{5,5};
    field[51]=3; field[52]=2; field[53]=1; field[54]=0;
    const std::vector<Point> history{{1,5},{0,5}};
    const auto make=[&](const auto& values,Point selected,std::span<const Point> actual,std::size_t cap=8,bool ready=true) {
        return approach_word(values,10,10,ready,origin,selected,actual,cap);
    };
    const std::vector<Point> expected{{1,5},{2,5},{3,5},{4,5},{3,5},{2,5},{1,5},{0,5}};
    auto word=make(field,first,history);
    check(word==expected,"complete ingress reaches frontier then reverses all planned ingress before actual return");
    check(std::max(std::abs(origin.x-wolf.x),std::abs(origin.y-wolf.y))==4 &&
          std::max(std::abs(word[2].x-wolf.x),std::abs(word[2].y-wolf.y))==2 &&
          std::max(std::abs(frontier.x-wolf.x),std::abs(frontier.y-wolf.y))==1,
          "outside wolf range3 word includes range2 approach and own range1 work frontier");
    const auto zero=std::find_if(word.begin(),word.end(),[&](Point point){return distance_at(field,10,10,point.x,point.y)==0;});
    check(zero!=word.end() && std::distance(word.begin(),zero)==3 && *zero==frontier,
          "caller can find first distance0 frontier index for a separate work phase charge");
    check(std::adjacent_find(word.begin(),word.end(),[](Point a,Point b){return !adjacent(a,b);})==word.end(),
          "whole physical word contains adjacent edges without duplicate frontier wait nodes");
    auto alternatives=field;alternatives[63]=1;alternatives[64]=0;
    word=make(alternatives,first,history);
    check(word==std::vector<Point>{{1,5},{2,5},{3,6},{4,6},{3,6},{2,5},{1,5},{0,5}},
          "continuation descents deterministically prefer native NE before E");
    check(make(field,first,history,7).empty(),"capacity one node short gives no truncated admission witness");
    check(make(field,first,history,0).empty(),"zero word capacity is unknown");
    check(make(field,first,history,std::numeric_limits<std::size_t>::max())==expected,
          "large supplied cap does not overflow size arithmetic or allocate the cap");
    check(approach_word(field,std::numeric_limits<int>::max(),std::numeric_limits<int>::max(),true,
          origin,first,std::span<const Point>{history},std::numeric_limits<std::size_t>::max()).empty(),
          "malformed dimensions cannot wrap the required field cell count");
    check(make(field,Point{3,5},history).empty(),"nonadjacent first landing is not an ingress word");
    check(make(field,Point{1,6},history).empty(),"first landing must match an actual finite d minus1 gradient");
    check(make(field,first,{}).empty(),"no historical return cannot certify a quiet suffix");
    const std::vector<Point> wrong_origin{{2,5},{1,5}};
    check(make(field,first,wrong_origin).empty(),"historical return must begin at current origin");
    const std::vector<Point> jumping{{1,5},{0,3}};
    check(make(field,first,jumping).empty(),"nonadjacent historical suffix invalidates the whole word");
    const std::vector<Point> outside{{1,5},{0,5},{-1,5}};
    check(make(field,first,outside,20).empty(),"out of bounds historical suffix supplies no word");
    auto malformed=field;malformed.pop_back();
    check(make(malformed,first,history).empty(),"truncated field buffer is unknown");
    malformed=field;malformed[53]=unknown;
    check(make(malformed,first,history).empty(),"gradient dead end cannot produce a partial ingress return");
    malformed=field;malformed[53]=2;
    check(make(malformed,first,history).empty(),"equal distance loop does not terminate in a fabricated frontier");
    malformed=field;malformed[51]=unknown;
    check(make(malformed,first,history).empty(),"unreachable origin gives empty unknown witness");
    malformed=field;malformed[51]=0;
    check(make(malformed,first,history).empty(),"already arrived origin has no proposed ingress first step");
    check(make(field,first,history,8,false).empty(),"unready route does not publish geometry");
}
}

void capture_export_owner_contract()
{
    using namespace tt::navigation_detail;
    struct Native
    {
        int calls{};
        std::vector<std::uint16_t> export_linear_buffer() { ++calls; return {2,1,0}; }
    };
    auto native=std::make_shared<Native>();
    std::vector<std::uint16_t> published;
    check(export_capture_buffer(native,published,3,[]{return true;})==CaptureExport::Ready &&
          native && native->calls==1 && published==std::vector<std::uint16_t>{2,1,0},
          "first actual export retains the immutable native owner");
    const auto first_word=std::move(published);
    check(first_word.size()==3 && published.empty(),"one representative may own the derived buffer");
    check(export_capture_buffer(native,published,3,[]{return true;})==CaptureExport::Ready &&
          native && native->calls==2 && published==first_word,
          "next candidate re-exports the SAME retained native source after buffer transfer");
    check(export_capture_buffer(native,published,3,[]{return false;})==CaptureExport::Stale &&
          published==first_word && native,"stale export cannot replace publication or consume owner");
    check(export_capture_buffer(native,published,4,[]{return true;})==CaptureExport::ShapeMismatch &&
          published==first_word && native,"bad shape cannot replace publication or consume owner");
    native.reset();
    check(export_capture_buffer(native,published,3,[]{return true;})==CaptureExport::MissingOwner,
          "true route invalidation withdraws all further export capability");
}
int main()
{
    using namespace tt::navigation_detail;
    Actor actor;
    int epoch=41;
    const Point origin{17,29},destination{18,30};
    auto clock=[&]{return epoch;};
    auto legal=[](Point){return true;};
    check(apply_destination(actor,origin,destination,41,clock,legal)==MoveResult::Applied,"absolute command applies");
    check(actor.pos==destination && actor.last==destination && actor.absolute_calls==1 && actor.relative_calls==0,
          "destination is never interpreted as a delta");
    actor=Actor{};
    check(apply_destination(actor,origin,destination,40,clock,legal)==MoveResult::Stale && actor.absolute_calls==0,"old owner epoch rejected");
    actor.pos={16,29};
    check(apply_destination(actor,origin,destination,41,clock,legal)==MoveResult::Stale && actor.absolute_calls==0,"displacement rejected");
    actor=Actor{}; actor.allowed=false;
    check(apply_destination(actor,origin,destination,41,clock,legal)==MoveResult::Unavailable && actor.absolute_calls==0,"spent allowance rejected");
    actor=Actor{};
    check(apply_destination(actor,origin,Point{19,30},41,clock,legal)==MoveResult::Illegal && actor.absolute_calls==0,"two-step command rejected");
    check(apply_destination(actor,origin,destination,41,clock,[](Point){return false;})==MoveResult::Illegal && actor.absolute_calls==0,"blocked landing rejected");
    check(apply_destination(actor,origin,destination,41,clock,[&](Point){++epoch; return true;})==MoveResult::Stale && actor.absolute_calls==0,
          "planning-prefix suspension revokes command");
    epoch=41; actor=Actor{};
    check(apply_destination(actor,origin,destination,41,clock,[&](Point){actor.pos={16,28}; return true;})==MoveResult::Stale && actor.absolute_calls==0,
          "legality-prefix displacement revokes command");
    epoch=41; actor=Actor{}; actor.accepted=false;
    check(apply_destination(actor,origin,destination,41,clock,legal)==MoveResult::Refused && actor.pos==origin,"hidden refusal preserves origin");
    check(adjacent(Point{0,0},Point{1,1}) && !adjacent(Point{0,0},Point{0,0}),"eight-direction one-step relation");
    const std::vector<std::uint16_t> field{0,1,2,1,2,3};
    check(distance_at(field,3,2,2,1)==3,"row-major distances use complete exported shape");
    check(distance_at(field,2,2,1,1)==65535 && distance_at(field,3,2,-1,0)==65535,"shape and bounds mismatch remain unknown");
    check(descends(3,2) && !descends(3,1) && !descends(0,0) && !descends(65535,2),"only exact field descent certifies a route step");
    planning_two_turn_contract();
    post_action_movement_contract();
    final_position_query_epoch_contract();
    static_proposal_contract();
    complete_approach_contract();
    joint_capture_phase_contract();
    capture_frontier_coverage_contract();
    capture_export_owner_contract();
    retained_route_geometry_contract();
    retained_route_key_contract();
    std::printf("independent navigation: %d failures\n",failures);
    return failures ? 1 : 0;
}
