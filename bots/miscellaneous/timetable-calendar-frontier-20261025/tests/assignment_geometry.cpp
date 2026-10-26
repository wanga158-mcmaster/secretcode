#include "assignment_geometry.h"
#include <cstdio>
#include <limits>
#include <type_traits>

namespace {
namespace g=tt::assignment_geometry;
int checks{},failures{};
void check(bool good,const char* message){++checks;if(!good){++failures;std::printf("FAIL %s\n",message);}}
g::Stamp stamp(g::Point origin={0,0}){return {11,12,13,14,5,origin};}
g::Grid grid(int width=9,int height=7) {
    // Topology fixture weights are declared inputs, not a native NPC-range simulation.
    g::Grid value;value.width=width;value.height=height;const auto count=static_cast<std::size_t>(width)*height;
    value.terrain.assign(count,0);value.camp_group.assign(count,-1);value.raw_upper.assign(count,0);
    value.altars={{0,height-1}};value.home_roots={{0,0}};value.exclusive_ownership=true;return value;
}
std::size_t index(const g::Grid& value,g::Point p){return static_cast<std::size_t>(p.y)*value.width+p.x;}
void camp(g::Grid& value,int group,g::Point p,double upper=20){value.camp_group[index(value,p)]=group;value.raw_upper[index(value,p)]=upper;}
void wall(g::Grid& value,g::Point p){value.terrain[index(value,p)]=1;}
g::Status finish(g::Cache& cache,g::Stamp key,std::size_t units=7) {
    for(unsigned i=0;i<200000&&cache.status()==g::Status::NeedCompute;++i) {
        const auto before=cache.work();const auto state=cache.advance(key,units);
        check(cache.last_work()<=units,"cache obeys explicit work slice");
        check(state!=g::Status::NeedCompute||cache.work()>before,"positive cache slice cannot own an input without progress");
    }
    return cache.status();
}
std::shared_ptr<g::Cache> prepared(g::Grid value,g::Stamp key) {
    auto cache=std::make_shared<g::Cache>(std::move(value),key);
    check(finish(*cache,key)==g::Status::Ready,"fixture immutable closed fields become Ready");return cache;
}
g::Request request(g::Stamp key,g::Point perch,std::vector<g::Point> selected) {
    g::Request r;r.stamp=key;r.selected_group=7;r.selected_tiles=std::move(selected);r.perch=perch;
    r.kit={g::Family::Basic,1,0,0,{}};r.handoff_inputs={1,2,1,2,1,2,1};
    r.disjoint_inputs_supported=true;r.exclusive_ownership=true;return r;
}
g::Status finish(g::Plan& plan,g::Stamp key,std::size_t units=7) {
    for(unsigned i=0;i<200000&&plan.status()==g::Status::NeedCompute;++i) {
        const auto before=plan.result().pure_units;const auto state=plan.advance(key,units);
        check(plan.last_work()<=units,"plan obeys explicit work slice");
        check(state!=g::Status::NeedCompute||plan.result().pure_units>before,"positive plan slice consumes retained work");
        if(state==g::Status::Ready)for(const auto& path:plan.result().legs)check(path.complete,"Ready waits for every full path publication");
    }
    return plan.status();
}
void complete_paths(const g::Cache& cache,const g::Result& r) {
    const auto selected_group=7;
    for(std::size_t k=0;k<g::leg_count;++k) {
        const auto& path=r.legs[k];check(path.complete&&path.disjoint_supported&&!path.points.empty(),"every nominated leg is complete and explicitly disjoint");
        check(path.edge_inputs+1==path.points.size(),"one distinct ordinary input per represented edge");
        for(std::size_t i=0;i<path.points.size();++i) {
            const auto p=path.points[i].point;check(cache.inside(p)&&cache.hard_open(cache.index(p)),"no wall altar or out-of-bounds landing is fabricated");
            const auto owner=cache.grid().camp_group[cache.index(p)];
            const bool permitted=k==1?(owner<0||owner==selected_group):k==2?(i==0?owner==selected_group:owner<0):owner<0;
            check(permitted,"only declared Capture opens selected spawns; Release is one-way and all later legs close them");
            if(i)check(g::detail::separation(path.points[i-1].point,p)==1,"published edges are adjacent without row wrapping or teleporting");
            check(path.points[i].raw_upper==cache.grid().raw_upper[cache.index(p)],"all-NPC point upper survives post-clear geometry");
        }
        long double exact=0;double maximum=0;
        for(std::size_t i=0;i<path.points.size();++i){maximum=std::max(maximum,path.points[i].raw_upper);if(i)exact+=path.points[i].raw_upper;}
        check(static_cast<long double>(path.destination_upper)>=exact&&path.max_point_upper==maximum,"charges retain destination inputs and maximum refusal-origin support with upward bounds");
    }
    const auto start=[&](g::Leg leg){return r.leg(leg).points.front().point;};
    const auto end=[&](g::Leg leg){return r.leg(leg).points.back().point;};
    check(start(g::Leg::Initial)==cache.stamp().origin&&end(g::Leg::Initial)==r.perch,"actual-origin ingress reaches the fixed perch");
    check(start(g::Leg::Capture)==r.perch&&end(g::Leg::Capture)==r.capture_tile,"post-clear leg reaches a real selected member");
    check(start(g::Leg::Release)==r.capture_tile&&end(g::Leg::Release)==r.release_tile&&r.leg(g::Leg::Release).edge_inputs==1,"capture endpoint has a real adjacent release edge");
    check(start(g::Leg::Bank)==r.release_tile&&end(g::Leg::Bank)==r.bank_stand,"closed release component reaches a bank stand");
    check(start(g::Leg::Renewal)==r.bank_stand&&end(g::Leg::Renewal)==r.perch,"renewal is bank to the same work perch");
    check(start(g::Leg::Home)==r.bank_stand&&end(g::Leg::Home)==r.home,"home is an actual ordinary path from bank");
    check(start(g::Leg::Redeploy)==r.home&&end(g::Leg::Redeploy)==r.perch,"redeploy is an actual ordinary home to perch path");
    bool bank=false;for(const auto altar:cache.grid().altars)bank|=g::detail::separation(altar,r.bank_stand)==1;
    check(bank,"bank endpoint is in actual range-one altar geometry");
}
void complete_and_bounded() {
    auto value=grid();camp(value,7,{5,2});camp(value,7,{5,3});camp(value,8,{3,2});
    const auto key=stamp();auto cache=prepared(std::move(value),key);
    const auto cache_work=cache->work();auto r=request(key,{4,2},{{5,2},{5,3}});g::Plan plan{cache,std::move(r)};
    check(plan.advance(key,0)==g::Status::NeedCompute&&plan.last_work()==0&&plan.result().pure_units==0,"zero slice neither restarts nor fabricates progress");
    auto later=key;std::uint64_t total=0;
    while(plan.status()==g::Status::NeedCompute&&total<200000) {
        ++later.epoch;plan.advance(later,1);total+=plan.last_work();check(plan.last_work()==1,"one-unit resume also bounds reconstruction and path publication");
    }
    check(plan.status()==g::Status::Ready&&plan.result().pure_units==total,"staged validation BFS trace and export finish without hidden bulk work");
    check(plan.result().dated.epoch==key.epoch&&plan.result().progress_inputs==total,"date is retained; later input numbers do not restart immutable work");
    check(cache->work()==cache_work,"per-group plan reuses the exact completed origin/bank/home fields");
    complete_paths(*cache,plan.result());
    check(plan.result().leg(g::Leg::Capture).points.back().raw_upper==20,"declared selected opening does not erase conservative NPC charge");
    for(std::size_t i=0;i<g::leg_count;++i)check(plan.result().legs[i].handoff_inputs==std::array<std::uint32_t,7>{1,2,1,2,1,2,1}[i],"geometry copies explicit paid handoff counts without inventing ticks");
    const auto done=plan.result().pure_units;check(plan.advance(later,10)==g::Status::Ready&&plan.result().pure_units==done,"completed work is never restarted by polling");
    ++later.query;check(plan.advance(later,1)==g::Status::Stale,"a new semantic query withdraws old path authority nominations");
}
void graph_separation_and_reachability() {
    auto value=grid(8,6);value.home_roots={{0,3}};
    for(const auto p:std::vector<g::Point>{{3,0},{4,0},{5,0},{3,1},{5,1},{3,2},{5,2}})wall(value,p);
    camp(value,7,{4,2});const auto key=stamp({0,3});auto cache=prepared(std::move(value),key);
    auto basic=request(key,{4,1},{{4,2}});g::Plan isolated{cache,std::move(basic)};
    check(finish(isolated,key)==g::Status::Unreachable,"near Basic perch is not priced from center distance across a sealed NPC boundary");
    auto area=request(key,{2,3},{{4,2}});area.kit={g::Family::Area,1,3,0,{4,2}};g::Plan reachable{cache,std::move(area)};
    check(finish(reachable,key)==g::Status::Ready,"farther actual AoE perch supplies a full physical continuation");complete_paths(*cache,reachable.result());
    auto wrong=request(key,{2,3},{{4,2}});g::Plan outside_basic{cache,std::move(wrong)};
    check(finish(outside_basic,key)==g::Status::Unsupported&&outside_basic.result().reason==g::Reason::Kit,"fixed Basic reach cannot masquerade as AoE kit support");

    auto channel=grid(8,3);channel.home_roots={{0,1}};channel.altars={{0,0}};
    for(int x=0;x<8;++x){wall(channel,{x,0});wall(channel,{x,2});}camp(channel,7,{3,1});
    const auto channel_key=stamp({0,1});auto closed=prepared(std::move(channel),channel_key);
    auto across=request(channel_key,{4,1},{{3,1}});g::Plan bridge{closed,std::move(across)};
    check(finish(bridge,channel_key)==g::Status::Unreachable,"selected NPC cannot be opened prematurely to connect pre-clear ingress");

    auto divided=grid();for(int y=0;y<7;++y)wall(divided,{3,y});camp(divided,7,{6,2});
    auto dc=prepared(std::move(divided),stamp());auto dr=request(stamp(),{5,2},{{6,2}});g::Plan disconnected{dc,std::move(dr)};
    check(finish(disconnected,stamp())==g::Status::Unreachable,"a full terrain separator produces no fabricated goal path");
}
void targeted_is_a_distinct_service_envelope() {
    const std::vector<g::Point> selected{{5,2},{5,3}};const g::Point perch{1,2};
    g::Kit basic{g::Family::Basic,1,4,0,{5,2}};
    g::Kit targeted{g::Family::Targeted,1,4,0,{}};
    g::Kit area{g::Family::Area,1,4,0,{5,2}};
    check(g::supports_perch(basic,selected,perch)==g::PerchSupport::Unsupported,"a range-four spell never upgrades standing Basic range-one service");
    check(g::supports_perch(targeted,selected,perch)==g::PerchSupport::Supported,"single-target spell geometry checks every selected member from the fixed perch");
    check(g::supports_perch(area,selected,perch)==g::PerchSupport::Unsupported,"targeted support is not one AoE center with a fabricated radius");
    area.area_radius=1;check(g::supports_perch(area,selected,perch)==g::PerchSupport::Supported,"actual area aim/radius remains its own supported family");
    targeted.cast_range=3;check(g::supports_perch(targeted,selected,perch)==g::PerchSupport::Unsupported,"one farther selected member refutes only this targeted perch");
    targeted.cast_range=4;
    auto value=grid();for(const auto p:selected)camp(value,7,p);auto cache=prepared(std::move(value),stamp());
    auto r=request(stamp(),perch,selected);r.kit=targeted;
    check(g::supports_perch(r)==g::PerchSupport::Supported,"request overload permits a bounded candidate tile scan without full Plan construction");
    g::Plan plan{cache,std::move(r)};check(finish(plan,stamp())==g::Status::Ready,"Targeted future hit geometry integrates with complete ingress/capture/release/bank/home paths");complete_paths(*cache,plan.result());
    std::vector<g::Point> oversized(g::perch_check_capacity+1,{5,2});
    check(g::supports_perch(targeted,oversized,perch)==g::PerchSupport::Capacity,"single-call candidate scan has an explicit member work bound");
    check(g::supports_perch(targeted,std::span<const g::Point>{},perch)==g::PerchSupport::Invalid,"an empty selected set cannot manufacture a supported job");
    auto invalid=targeted;invalid.family=static_cast<g::Family>(99);
    check(g::supports_perch(invalid,selected,perch)==g::PerchSupport::Invalid,"unknown service families remain explicit invalid input");
}
void boundary_bank_home_and_diagonals() {
    auto value=grid();std::vector<g::Point> selected{{4,3}};
    for(int y=2;y<=4;++y)for(int x=3;x<=5;++x){camp(value,7,{x,y});if(g::Point{x,y}!=g::Point{4,3})selected.push_back({x,y});}
    auto cache=prepared(std::move(value),stamp());auto r=request(stamp(),{2,3},selected);r.kit={g::Family::Area,1,5,1,{4,3}};
    g::Plan boundary{cache,std::move(r)};check(finish(boundary,stamp())==g::Status::Ready,"an interior member without a release neighbor does not hide valid boundary endpoints");
    check(boundary.result().capture_tile!=g::Point{4,3},"fully surrounded capture tile is not published as a release origin");complete_paths(*cache,boundary.result());

    auto banks=grid(9,6);banks.altars={{7,1},{0,5}};camp(banks,7,{4,3});
    for(const auto d:g::detail::directions)wall(banks,{7+d.x,1+d.y});
    auto bc=prepared(std::move(banks),stamp());auto br=request(stamp(),{3,3},{{4,3}});g::Plan bank{bc,std::move(br)};
    check(finish(bank,stamp())==g::Status::Ready&&g::detail::separation(bank.result().bank_stand,{0,5})==1,"a surrounded nearer altar cannot fabricate a bank root");complete_paths(*bc,bank.result());

    auto diagonal=grid(4,4);diagonal.altars={{3,3}};wall(diagonal,{1,0});wall(diagonal,{0,1});camp(diagonal,7,{2,2});
    auto cc=prepared(std::move(diagonal),stamp());check(cc->distance({1,1})==1,"SDK destination legality permits a real diagonal between blocked flank tiles");
    check(cc->distance({3,0})!=1,"linear index adjacency cannot wrap a row edge");

    auto no_home=grid();for(int y=0;y<7;++y)wall(no_home,{6,y});no_home.home_roots={{8,0}};camp(no_home,7,{4,3});
    auto hc=prepared(std::move(no_home),stamp());auto hr=request(stamp(),{3,3},{{4,3}});g::Plan home{hc,std::move(hr)};
    check(finish(home,stamp())==g::Status::Unreachable,"bank feasibility alone cannot stand in for complete home/redeploy support");

    auto altar_target=grid();camp(altar_target,7,{5,2});altar_target.altars.push_back({5,2});
    auto ac=prepared(std::move(altar_target),stamp());auto ar=request(stamp(),{4,2},{{5,2}});g::Plan hard_goal{ac,std::move(ar)};
    check(finish(hard_goal,stamp())==g::Status::Unreachable,"selected post-clear opening never opens a hard altar goal");
}
void quiet_bank_goal_support() {
    auto value=grid(10,6);value.altars={{5,2},{0,5}};camp(value,7,{7,2});
    for(const auto d:g::detail::directions)value.raw_upper[index(value,{5+d.x,2+d.y})]=10;
    check(!value.require_quiet_bank,"bank quiet support defaults false without changing earlier nominations");
    auto strict=value;strict.require_quiet_bank=true;
    auto old=prepared(std::move(value),stamp());auto quiet=prepared(std::move(strict),stamp());
    auto legacy_request=request(stamp(),{6,2},{{7,2}});g::Plan legacy{old,std::move(legacy_request)};
    check(finish(legacy,stamp())==g::Status::Ready&&g::detail::separation(legacy.result().bank_stand,{5,2})==1&&old->grid().raw_upper[old->index(legacy.result().bank_stand)]>0,"default false retains the nearer hazardous bank root");
    auto quiet_request=request(stamp(),{6,2},{{7,2}});g::Plan safe{quiet,std::move(quiet_request)};
    check(finish(safe,stamp())==g::Status::Ready&&g::detail::separation(safe.result().bank_stand,{0,5})==1&&quiet->grid().raw_upper[quiet->index(safe.result().bank_stand)]==0,"quiet goal support selects a reachable quiet alternative rather than discarding the complete job");
    complete_paths(*quiet,safe.result());
    check(quiet->closed(quiet->index({6,2}))&&quiet->distance({6,2})==old->distance({6,2})&&quiet->grid().raw_upper[quiet->index({6,2})]==10,"quiet bank filtering preserves hazardous travel legality and charges");
    check(quiet->grid().camp_group[quiet->index({7,2})]==7&&!quiet->closed(quiet->index({7,2})),"quiet bank support never opens an NPC spawn or erases occupancy");
    const auto prior=quiet->bank_field().distance(quiet->index({6,2}));auto next=stamp({1,0});++next.query;++next.epoch;
    check(quiet->reorigin(next)==g::Status::NeedCompute&&finish(*quiet,next)==g::Status::Ready&&quiet->grid().require_quiet_bank&&quiet->bank_field().distance(quiet->index({6,2}))==prior,"reorigin retains the same immutable quiet bank root field");
    auto none=grid(10,6);none.altars={{5,2},{0,5}};none.require_quiet_bank=true;camp(none,7,{7,2});
    for(const auto altar:none.altars)for(const auto d:g::detail::directions){const g::Point p{altar.x+d.x,altar.y+d.y};if(p.x>=0&&p.y>=0&&p.x<none.width&&p.y<none.height)none.raw_upper[index(none,p)]=10;}
    g::Cache unsupported{std::move(none),stamp()};
    check(finish(unsupported,stamp())==g::Status::Unsupported&&unsupported.reason()==g::Reason::Roots,"all hazardous bank goals are unsupported under this declared class, without claiming physical infeasibility");
}
void invalid_and_stale() {
    static_assert(!std::is_constructible_v<g::Cache,g::Grid&,g::Stamp>);
    static_assert(!std::is_copy_constructible_v<g::Cache> && !std::is_move_constructible_v<g::Plan>);
    auto alias=grid();alias.exclusive_ownership=false;g::Cache borrowed{std::move(alias),stamp()};
    check(borrowed.status()==g::Status::Unsupported&&borrowed.reason()==g::Reason::Ownership,"mutable borrowed backing is explicitly unsupported");
    g::Grid huge;huge.width=std::numeric_limits<int>::max();huge.height=std::numeric_limits<int>::max();huge.exclusive_ownership=true;
    g::Cache overflow{std::move(huge),stamp()};check(overflow.status()==g::Status::Unsupported,"dimension/capacity overflow is rejected before allocation");
    g::Grid negative;negative.width=-1;negative.height=2;negative.exclusive_ownership=true;g::Cache invalid_dimensions{std::move(negative),stamp()};
    check(invalid_dimensions.status()==g::Status::Unsupported,"negative dimensions cannot wrap into a grid capacity");
    auto shape=grid();shape.raw_upper.pop_back();g::Cache malformed{std::move(shape),stamp()};check(malformed.status()==g::Status::Unsupported,"mismatched grid planes cannot produce a field");
    auto number=grid();number.raw_upper[3]=std::numeric_limits<double>::infinity();g::Cache bad_number{std::move(number),stamp()};
    check(finish(bad_number,stamp())==g::Status::Unsupported,"nonfinite point charges cannot become zero exposure");
    auto occupied=grid();camp(occupied,7,{0,0});g::Cache bad_origin{std::move(occupied),stamp()};
    check(finish(bad_origin,stamp())==g::Status::Unsupported&&bad_origin.reason()==g::Reason::Origin,"actual blocked origin is not silently opened in the pre-clear graph");
    auto separate_home=grid();separate_home.home_roots={{0,1}};camp(separate_home,7,{0,0});
    g::Cache distinct_origin{std::move(separate_home),stamp()};
    check(finish(distinct_origin,stamp())==g::Status::Unsupported&&distinct_origin.reason()==g::Reason::Origin,"blocked origin remains unsupported even with a valid independent home root");
    auto blocked_home=grid();blocked_home.home_roots={{0,1}};camp(blocked_home,7,{0,1});
    g::Cache distinct_home{std::move(blocked_home),stamp()};
    check(finish(distinct_home,stamp())==g::Status::Unsupported&&distinct_home.reason()==g::Reason::Roots,"valid origin does not conceal an independently blocked future home root");
    auto altar_origin=grid();altar_origin.altars.push_back({0,0});g::Cache blocked_altar{std::move(altar_origin),stamp()};
    check(finish(blocked_altar,stamp())==g::Status::Unsupported&&blocked_altar.reason()==g::Reason::Origin,"origin validation waits for all hard altar obstacles and never opens one");
    auto value=grid();camp(value,7,{5,2});camp(value,7,{5,3});auto cache=prepared(std::move(value),stamp());
    auto omitted=request(stamp(),{4,2},{{5,2}});g::Plan incomplete{cache,std::move(omitted)};
    check(finish(incomplete,stamp())==g::Status::Unsupported&&incomplete.result().reason==g::Reason::Group,"partial selected ownership cannot open an undeclared group member");
    auto duplicate=request(stamp(),{4,2},{{5,2},{5,2}});g::Plan repeated{cache,std::move(duplicate)};
    check(finish(repeated,stamp())==g::Status::Unsupported,"aliased selected endpoints are explicitly invalid");
    auto live=request(stamp(),{4,2},{{5,2},{5,3}});g::Plan changing{cache,std::move(live)};changing.advance(stamp(),19);
    auto drift=stamp();drift.origin={1,0};check(changing.advance(drift,1)==g::Status::Stale,"actual-origin drift withdraws staged geometry without rebuilding it");
    auto another=request(stamp(),{4,2},{{5,2},{5,3}});g::Plan dependency{cache,std::move(another)};auto old=stamp();--old.epoch;
    check(dependency.advance(old,1)==g::Status::Stale,"an older input cannot refresh a dated query");
    auto support=stamp();++support.potential;check(cache->advance(support,1)==g::Status::Stale,"changed source potential invalidates the retained whole origin cache");

    auto more=grid();camp(more,7,{5,2});auto retained=prepared(std::move(more),stamp());
    const auto old_bank=retained->bank_field().distance(retained->index({2,3}));
    auto old_request=request(stamp(),{4,2},{{5,2}});g::Plan old_plan{retained,std::move(old_request)};
    auto next=stamp({1,0});++next.query;++next.epoch;
    check(retained->reorigin(next)==g::Status::NeedCompute,"new actual origin explicitly resumes only the origin field on cached immutable geometry");
    check(old_plan.advance(stamp(),1)==g::Status::Stale,"reorigin cannot leave a prior Plan bound to different fields");
    check(finish(*retained,next)==g::Status::Ready&&retained->distance({1,0})==0,"bounded reorigin publishes the new exact actual-origin field");
    check(retained->bank_field().distance(retained->index({2,3}))==old_bank,"immutable bank/home support survives reorigin without rebuilding");
    auto fresh=request(next,{4,2},{{5,2}});g::Plan chronology{retained,std::move(fresh)};
    auto forward=next;forward.epoch+=3;chronology.advance(forward,1);--forward.epoch;
    check(chronology.advance(forward,1)==g::Status::Stale&&chronology.result().reason==g::Reason::Epoch,"a resumed query cannot rewind to an input newer than birth but older than its last slice");
    auto future=next;future.epoch+=5;retained->advance(future,0);--future.epoch;
    check(retained->advance(future,1)==g::Status::Stale&&retained->reason()==g::Reason::Epoch,"cached origin fields also reject an epoch rewind after a later observation");

    auto costly=grid();camp(costly,7,{5,2});std::fill(costly.raw_upper.begin(),costly.raw_upper.end(),std::numeric_limits<double>::max());
    auto fc=prepared(std::move(costly),stamp());auto fr=request(stamp(),{4,2},{{5,2}});g::Plan charge{fc,std::move(fr)};
    check(finish(charge,stamp())==g::Status::Unsupported&&charge.result().reason==g::Reason::Number,"unrepresentable finite path sum is explicit Unsupported rather than a capped/free charge");
}
}
int main() {
    complete_and_bounded();graph_separation_and_reachability();targeted_is_a_distinct_service_envelope();boundary_bank_home_and_diagonals();quiet_bank_goal_support();invalid_and_stale();
    std::printf("assignment geometry: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
