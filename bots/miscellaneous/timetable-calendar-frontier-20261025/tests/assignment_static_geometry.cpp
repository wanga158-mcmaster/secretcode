#include "../src/assignment_static_geometry.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>

namespace {
namespace s=tt::assignment_static_geometry;
namespace g=tt::assignment_geometry;
int checks{},failures{};
void check(bool good,const char* text) {
    ++checks;if(!good){++failures;std::printf("FAIL %s\n",text);}
}
g::Stamp stamp(g::Point origin={1,2}) {return {12,16,23,24,5,origin};}
s::StaticSeed seed() {
    s::StaticSeed v;v.key={11,12,13,14,1,16,true};v.complete_potential=true;
    auto& grid=v.grid;grid.width=9;grid.height=7;grid.exclusive_ownership=grid.require_quiet_bank=true;
    grid.terrain.assign(63,0);grid.camp_group.assign(63,-1);grid.raw_upper.assign(63,0);
    grid.altars={{0,6}};grid.home_roots={{0,0}};grid.terrain[4]=1;
    v.catalogue={{0,{{{3,2},100,2,1}}},{1,{{{7,4},200,4,1}}}};
    for(const auto& group:v.catalogue)for(const auto& member:group.members) {
        grid.camp_group[static_cast<std::size_t>(member.pos.y)*9+member.pos.x]=group.canonical;
        for(int y=0;y<7;++y)for(int x=0;x<9;++x)
            if(g::detail::separation({x,y},member.pos)<=member.range)
                grid.raw_upper[static_cast<std::size_t>(y)*9+x]+=member.strength;
    }
    return v; // Complete synthetic all-source envelope, never discounted for Life.
}
g::Request request(g::Stamp dated=stamp()) {
    g::Request v;v.stamp=dated;v.selected_group=0;v.selected_tiles={{3,2}};v.perch={2,2};
    v.kit={g::Family::Basic,1,0,0,{}};v.handoff_inputs={1,2,1,2,1,2,1};
    v.disjoint_inputs_supported=v.exclusive_ownership=true;return v;
}
g::Status finish(s::StaticBuilder& b) {
    for(unsigned n=0;n<200000&&b.status()==g::Status::NeedCompute;++n)b.advance(7);
    return b.status();
}
g::Status finish(s::OriginBuilder& b,g::Stamp current) {
    for(unsigned n=0;n<200000&&b.status()==g::Status::NeedCompute;++n)b.advance(current,7);
    return b.status();
}
g::Status finish(s::TargetBuilder& b,g::Stamp current) {
    for(unsigned n=0;n<200000&&b.status()==g::Status::NeedCompute;++n)b.advance(current,7);
    return b.status();
}
std::shared_ptr<const s::StaticGeometry> geometry() {
    s::StaticBuilder b{seed()};check(!b.result(),"unsealed static work publishes no grid");
    check(b.advance(0)==g::Status::NeedCompute&&!b.result()&&b.last_work()==0,
        "zero static slice cannot publish partial fields");
    check(finish(b)==g::Status::Ready,"complete fixed catalogue seals static fields");return b.result();
}
std::shared_ptr<const s::OriginView> origin(std::shared_ptr<const s::StaticGeometry> data,g::Stamp dated,
    s::OriginMode mode=s::OriginMode::Field,std::shared_ptr<const s::OriginView> reuse={}) {
    s::OriginBuilder b{std::move(data),dated,mode,std::move(reuse)};
    check(finish(b,dated)==g::Status::Ready,"fresh supported origin seals its own view");return b.result();
}
std::shared_ptr<const s::TargetGeometry> target(std::shared_ptr<const s::OriginView> view,g::Stamp dated) {
    s::TargetBuilder b{std::move(view),request(dated)};
    check(finish(b,dated)==g::Status::Ready,"exact selected group and kit seal all target paths");return b.result();
}
void paths(const s::TargetGeometry& value,g::Point captured_origin) {
    const auto& data=*value.static_geometry();const auto& result=value.result();
    check(result.status==g::Status::Ready&&result.conditional_occupancy&&result.conditional_selected_clear,
        "Ready target remains a conditional geometry nomination");
    for(std::size_t leg=0;leg<g::leg_count;++leg) {
        const auto& path=result.legs[leg];
        check(path.complete&&path.disjoint_supported&&!path.points.empty(),"every one of seven legs is fully published");
        if(path.points.empty())return; // Report a broken Ready contract without fixture UB.
        check(path.edge_inputs+1==path.points.size()&&path.handoff_inputs==request().handoff_inputs[leg],
            "ordinary edge inputs and declared handoffs are retained");
        long double destinations=0;double maximum=0;
        for(std::size_t i=0;i<path.points.size();++i) {
            const auto point=path.points[i].point;const auto tile=data.index(point);
            check(data.inside(point)&&data.hard_open(tile),"every path point excludes walls altars and outside cells");
            if(!data.inside(point))continue;
            const int group=data.grid().camp_group[tile];
            const bool allowed=leg==1?(group<0||group==0):leg==2?(i==0?group==0:group<0):group<0;
            check(allowed,"Capture alone opens selected spawns and Release leaves them in one direction");
            check(path.points[i].raw_upper==*data.raw_upper(point),"all-NPC raw upper survives every graph handoff");
            if(i) {check(g::detail::separation(path.points[i-1].point,point)==1,"every represented edge is adjacent");
                destinations+=path.points[i].raw_upper;}
            maximum=std::max(maximum,path.points[i].raw_upper);
        }
        check(static_cast<long double>(path.destination_upper)>=destinations&&path.max_point_upper==maximum,
            "destination charge is conservative and refusal-origin maximum is retained");
    }
    const auto start=[&](g::Leg l){return result.leg(l).points.front().point;};
    const auto end=[&](g::Leg l){return result.leg(l).points.back().point;};
    check(start(g::Leg::Initial)==captured_origin&&end(g::Leg::Initial)==result.perch,
        "stored initial leg belongs only to the captured origin");
    check(start(g::Leg::Capture)==result.perch&&end(g::Leg::Capture)==result.capture_tile&&result.capture_tile==g::Point{3,2},
        "post-clear capture reaches an actual selected member");
    check(start(g::Leg::Release)==result.capture_tile&&end(g::Leg::Release)==result.release_tile&&
        result.leg(g::Leg::Release).edge_inputs==1,"release has a real single adjacent closed landing");
    check(start(g::Leg::Bank)==result.release_tile&&end(g::Leg::Bank)==result.bank_stand&&
        start(g::Leg::Renewal)==result.bank_stand&&end(g::Leg::Renewal)==result.perch,
        "bank and renewal connect the same released component and perch");
    check(start(g::Leg::Home)==result.bank_stand&&end(g::Leg::Home)==result.home&&
        start(g::Leg::Redeploy)==result.home&&end(g::Leg::Redeploy)==result.perch,"home and redeploy are complete continuations");
    check(*value.raw_upper(result.bank_stand)==0&&g::detail::separation(result.bank_stand,{0,6})==1,
        "quiet bank root remains an actual adjacent altar stand");
    check(result.leg(g::Leg::Capture).points.back().raw_upper==2,
        "counterfactual selected-clear geometry never removes selected-source charge");
}
void sealed_parity_and_fresh_binding() {
    auto data=geometry();if(!data)return;
    auto dated=stamp();g::Grid old_grid=data->grid();g::Cache old{std::move(old_grid),dated};
    for(unsigned n=0;n<200000&&old.status()==g::Status::NeedCompute;++n)old.advance(dated,7);
    check(old.status()==g::Status::Ready,"old closed-graph baseline completes on identical declared inputs");
    for(std::uint32_t i=0;i<data->size();++i) {
        check(data->closed(i)==old.closed(i)&&data->hard_open(i)==old.hard_open(i)&&
            data->grid().raw_upper[i]==old.grid().raw_upper[i],"sealed graph and all-source upper match old semantics per cell");
        check(data->bank_field().distance(i)==old.bank_field().distance(i)&&
            data->home_field().distance(i)==old.home_field().distance(i),"reusable quiet-bank and home fields match old roots");
    }
    check(!data->closed(data->index({3,2}))&&!data->closed(data->index({7,4}))&&
        *data->raw_upper({6,4})==4,"all NPC spawns close geometry and nonselected sources remain in potential");
    auto first=origin(data,dated);if(!first)return;
    check(first->has_field()&&first->distance({2,2})==1,"actual-origin field has the correct ordinary distance");
    s::TargetBuilder building{first,request(dated)};
    check(!building.result()&&building.advance(dated,0)==g::Status::NeedCompute&&!building.result(),
        "unknown or partially built target cannot expose paths");
    auto later=dated;later.body=93;later.query=94;later.origin={0,3};later.epoch=6;
    check(finish(building,later)==g::Status::Ready,"body/query/actual-origin changes do not invalidate immutable target work");
    auto value=building.result();if(!value)return;
    check(value->stamp().body==dated.body&&value->stamp().query==dated.query&&value->stamp().origin==dated.origin,
        "target completion never restamps the old captured origin as fresh");paths(*value,dated.origin);
    auto second=origin(data,later,s::OriginMode::Field,first);
    auto bound=origin(data,later,s::OriginMode::BindOnly,first);if(!second||!bound)return;
    check(second->static_geometry()==first->static_geometry()&&second->origin_field()!=first->origin_field()&&
        second->distance({2,2})==2&&first->distance({2,2})==1&&first->stamp().origin==dated.origin,
        "new body/origin reuses sealed static fields without mutating the old actual-origin field");
    check(!bound->has_field()&&bound->stamp().origin==later.origin,
        "BindOnly fresh view does not borrow another origin's field");
    auto point=bound->stamp().origin;unsigned edges=0;
    while(point!=value->perch()&&edges<data->size()) {
        const auto before=value->distance_to_perch(point);const auto next=value->next_to_perch(point);
        check(next&&data->closed(data->index(*next))&&g::detail::separation(point,*next)==1&&
            value->distance_to_perch(*next)+1==before,"fresh initial binding follows a legal decreasing closed-from-perch edge");
        if(!next)break;point=*next;++edges;
    }
    check(point==value->perch()&&edges==2&&value->result().leg(g::Leg::Initial).points.front().point==dated.origin,
        "fresh reverse initial reaches perch while old initial keeps its original date and origin");
    check(!value->next_to_perch({3,2})&&!value->next_to_perch({-1,0}),"closed-perch binder refuses NPC and unknown positions");
}
void invalid_inputs_and_stamps() {
    for(unsigned which=0;which<4;++which) {
        auto v=seed();if(which==0)v.key.map=0;else if(which==1)v.complete_potential=false;
        else if(which==2)v.key.require_quiet_bank=false;else v.catalogue[0].members[0].pos={2,2};
        s::StaticBuilder b{std::move(v)};
        check(finish(b)==g::Status::Unsupported&&!b.result(),"invalid static identity/completeness/quiet-policy/catalogue cannot seal");
    }
    auto data=geometry();if(!data)return;const auto dated=stamp();
    s::OriginBuilder partial{data,dated};check(!partial.result(),"origin partial work has no Ready view");
    auto changed=dated;++changed.body;
    check(partial.advance(changed,1)==g::Status::Stale&&!partial.result(),"fresh-origin builder rejects different actual support");
    s::OriginBuilder absent{{},dated};check(absent.status()==g::Status::Unsupported&&!absent.result(),"unknown static geometry cannot be rebound");
    auto view=origin(data,dated);if(!view)return;
    for(unsigned which=0;which<4;++which) {
        auto r=request(dated);if(which==0)r.selected_tiles={{7,4}};else if(which==1)r.selected_group=1;
        else if(which==2)r.kit.basic_range=0;else r.selected_tiles.push_back({7,4});
        s::TargetBuilder b{view,std::move(r)};
        check(finish(b,dated)==g::Status::Unsupported&&!b.result(),"target must match exact catalogue group membership and supporting kit");
    }
    s::TargetBuilder changed_static{view,request(dated)};changed=dated;++changed.potential;
    check(changed_static.advance(changed,1)==g::Status::Stale&&!changed_static.result(),"different static potential cannot continue old target work");
    s::TargetBuilder absent_origin{{},request(dated)};
    check(absent_origin.status()==g::Status::Unsupported&&!absent_origin.result(),"unknown origin cannot fabricate target readiness");
}
void pinned_lifetime() {
    auto data=geometry();if(!data)return;auto view=origin(data,stamp());if(!view)return;
    auto value=target(view,stamp());if(!value)return;
    std::weak_ptr<const s::StaticGeometry> static_life=data;std::weak_ptr<const s::OriginView> own_life=view;
    data.reset();view.reset();
    check(own_life.expired()&&!static_life.expired()&&value->next_to_perch({0,3}).has_value(),
        "sealed target pins static witness without retaining stale own-origin view");
    value.reset();check(static_life.expired(),"sealed static and target owners have no reference cycle");
}
void future_only_has_no_obsolete_origin_dependency() {
    auto input=seed();
    for(int y=0;y<input.grid.height;++y)input.grid.terrain[static_cast<std::size_t>(y)*9+1]=1;
    input.grid.altars.push_back({8,0});input.grid.home_roots.push_back({8,6});++input.key.roots;
    s::StaticBuilder compile{std::move(input)};
    check(finish(compile)==g::Status::Ready,"split-component static fields seal");
    const auto data=compile.result();if(!data)return;
    const auto dated=stamp({0,2});auto actual=origin(data,dated,s::OriginMode::BindOnly);if(!actual)return;
    s::TargetBuilder with_initial{actual,request(dated)};
    check(finish(with_initial,dated)==g::Status::Unreachable&&!with_initial.result(),
        "default target still requires its original actual Initial reachability");
    s::TargetBuilder future{data,request(dated),s::TargetMode::FutureOnly};
    check(!future.result(),"partial future template is not published");
    check(finish(future,dated)==g::Status::Ready,"future target ignores obsolete origin component");
    const auto value=future.result();if(!value)return;
    check(value->future_only()&&!value->has_initial()&&value->result().leg(g::Leg::Initial).points.empty()&&
        !value->result().leg(g::Leg::Initial).complete,"future Ready explicitly lacks physical Initial");
    for(std::size_t leg=1;leg<g::leg_count;++leg)
        check(value->result().legs[leg].complete&&!value->result().legs[leg].points.empty(),"six future legs complete");
    check(value->stamp().origin==dated.origin&&value->stamp().body==dated.body,"original query stamp retained");
    check(value->distance_to_perch({0,2})==g::detail::none&&value->distance_to_perch({2,3})==1&&
        value->next_to_perch({2,3})==std::optional<g::Point>{{2,2}},"new actual ingress must have its own perch path");
    s::TargetBuilder forbidden{data,request(dated),s::TargetMode::WithInitial};
    check(forbidden.status()==g::Status::Unsupported,"static pointer cannot invent WithInitial origin");
}
void actual_altar_union_and_staged_storage() {
    auto blocked=seed();blocked.grid.terrain[6*9]=1;
    s::StaticBuilder undeclared{std::move(blocked)};
    check(finish(undeclared)==g::Status::Unsupported&&undeclared.reason()==g::Reason::Roots,
        "legacy clear-altar representation still rejects an undeclared blocked root");
    auto actual=seed();actual.grid.terrain[6*9]=1;actual.terrain_includes_altars=true;
    s::StaticBuilder builder{std::move(actual)};
    check(builder.work_used()==0&&!builder.result(),"static constructor performs no admitted mask work");
    builder.advance(0);
    check(builder.work_used()==0&&!builder.result(),"zero admission performs no allocation or fill");
    for(unsigned n=0;n<3;++n) {
        builder.advance(1);
        check(builder.last_work()==1&&!builder.result(),"each mask reserve has its own admitted stage");
    }
    check(finish(builder)==g::Status::Ready,"actual blocked-altar terrain representation seals");
    const auto data=builder.result();if(!data)return;
    check(data->grid().terrain[6*9]==1&&!data->hard_open(6*9)&&!data->closed(6*9),
        "altar provenance never clears its actual blocked terrain bit");
    check(data->grid().terrain[4]==1&&!data->hard_open(4),"other blocked terrain remains blocked");
    check(data->bank_field().distance(data->index({1,5}))==0,
        "actual altar still supplies its adjacent quiet bank root");
    s::Search staged;staged.begin(1,{0});
    const auto step=[&]{staged.unit([](g::Point p){return p==g::Point{0,0};},
        [](g::Point){return std::uint32_t{0};},[](std::uint32_t){return g::Point{0,0};},
        [](std::uint32_t){return true;});};
    for(unsigned n=0;n<4;++n){step();check(!staged.ready(),"each search allocation is a distinct incomplete stage");}
    for(unsigned n=0;n<8&&!staged.ready();++n)step();
    check(staged.ready()&&staged.distance(0)==0&&staged.parent(0)==0&&staged.root(0)==0,
        "staged storage preserves the complete search root witness");
}
}
int main() {
    sealed_parity_and_fresh_binding();invalid_inputs_and_stamps();pinned_lifetime();
    future_only_has_no_obsolete_origin_dependency();
    actual_altar_union_and_staged_storage();
    std::printf("assignment static geometry (pure, no SDK actions): %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
