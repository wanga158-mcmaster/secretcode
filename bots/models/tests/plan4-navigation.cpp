#include "../common/nav.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <queue>
namespace {
unsigned permits=0;
bool fuel_available(){if(!permits)return false;--permits;return true;}
unsigned terrain_queries=0;
bool open_terrain(plan4::Point,const void*){++terrain_queries;return false;}

int cached(const plan4::PublicNavigation& navigation,plan4::Point a,plan4::Point b) {
    const auto lookup=navigation.lookup();
    return lookup.distance(a,b,lookup.context);
}

int independent_distance(plan4::Point origin,plan4::Point target,
                         const std::vector<plan4::Point>& blocked={}) {
    std::array<int,2500> distance;distance.fill(-1);
    std::array<bool,2500> wall{};
    for(const auto p:blocked)wall[p.y*50+p.x]=true;
    if(wall[origin.y*50+origin.x]||wall[target.y*50+target.x])return -1;
    std::queue<plan4::Point> frontier;frontier.push(origin);distance[origin.y*50+origin.x]=0;
    while(!frontier.empty()) {
        const auto p=frontier.front();frontier.pop();
        if(p==target)return distance[p.y*50+p.x];
        for(int y=p.y-1;y<=p.y+1;++y)for(int x=p.x-1;x<=p.x+1;++x) {
            if(x<0||y<0||x>=50||y>=50||wall[y*50+x]||distance[y*50+x]>=0)continue;
            distance[y*50+x]=distance[p.y*50+p.x]+1;frontier.push({x,y});
        }
    }
    return -1;
}

void topology_only_and_initial_limits() {
    namespace cm=cambc::mock;
    cm::reset();
    auto navigation=std::make_unique<plan4::PublicNavigation>();
    navigation->warm(cm::round,{1,1});
    assert(!navigation->initialized()); // An owner clock is required even without targets.
    navigation->begin_turn();navigation->warm(cm::round+1,{1,1});
    assert(!navigation->initialized());
    navigation->warm(cm::round,{1,1});
    assert(navigation->initialized()&&navigation->vertex_visits()==0&&navigation->pending_fields()==0);
    assert(navigation->completed_pairs()==0&&navigation->completed_route_entries()==0);
    const auto topology=navigation->topology();
    assert(topology.complete&&topology.width==50&&topology.height==50);
    assert(topology.walkable({1,1},topology.context));
    const auto queries=cm::queries;
    navigation->seal();navigation->warm(cm::round,{1,1});
    assert(cm::queries==queries&&navigation->vertex_visits()==0);

    // A caller can reserve every public vertex, or reduce its share before the
    // first topology initialization. Initialization must not restore 192.
    for(const int limit:{0,1,7,37,192}) {
        cm::reset();navigation=std::make_unique<plan4::PublicNavigation>();
        auto information=std::make_unique<plan4::Information>();
        information->round=cm::round;information->own.position={1,1};
        information->altar_count=1;information->altars[0].position={48,48};
        navigation->set_vertex_limit(limit);navigation->begin_turn();
        navigation->prepare(*information,50000,limit==37?8000:15000);
        assert(navigation->initialized()&&navigation->vertex_visits()==std::min(limit,32));
        navigation->warm(cm::round,information->own.position);
        assert(navigation->vertex_visits()<=limit);
        ++cm::round;cm::fuel=100000;navigation->begin_turn();
        navigation->warm(cm::round,information->own.position,50000,limit==37?8000:15000);
        assert(navigation->vertex_visits()<=limit);
    }
}

void complete_layout_is_owned_without_old_observations() {
    namespace cm=cambc::mock;
    cm::reset();auto navigation=std::make_unique<plan4::PublicNavigation>();
    auto information=std::make_unique<plan4::Information>();
    information->round=cm::round;information->own.position={48,1};
    information->own.health=0;information->own.bag=999;
    information->observation_truncated=true; // Dynamic incompleteness does not truncate public geometry.
    information->group_count=plan4::max_groups;information->altar_count=plan4::max_altars;
    for(auto& group:information->groups) {
        group.position={1,1};group.health=-1;group.gems=999;
        group.lifecycle=plan4::Lifecycle::unknown;group.occupied_spawn=true;
    }
    information->groups.back().position={48,48};
    for(auto& altar:information->altars){altar.position={49,49};altar.accessible=false;}
    information->altars.back().position={2,1};information->altars.back().accessible=true;
    assert(navigation->remember_public_routes(*information));
    // Destroy the source, including old own position, health, stock and occupancy.
    // Warming must use owned public endpoints and this call's fresh position.
    information.reset();navigation->set_vertex_limit(1);navigation->begin_turn();
    navigation->warm(cm::round,{1,1});
    assert(cached(*navigation,{1,1},{2,1})==1&&navigation->pending_fields()==0);
    assert(cached(*navigation,{48,1},{2,1})<0&&cached(*navigation,{1,1},{49,49})<0);
    navigation->set_vertex_limit(192);
    int turns=0;
    while(cached(*navigation,{48,48},{2,1})<0&&turns++<100) {
        ++cm::round;cm::fuel=100000;navigation->begin_turn();navigation->warm(cm::round,{1,1});
        assert(navigation->vertex_visits()<=192);
    }
    assert(cached(*navigation,{48,48},{2,1})==independent_distance({48,48},{2,1}));
    assert(cached(*navigation,{1,1},{48,48})==independent_distance({1,1},{48,48}));
}

void rejected_layouts_do_not_latch_or_replace_complete_layout() {
    namespace cm=cambc::mock;
    cm::reset();auto navigation=std::make_unique<plan4::PublicNavigation>();
    auto information=std::make_unique<plan4::Information>();
    information->group_count=1;information->groups[0].position={4,1};
    information->altar_count=1;information->altars[0].position={2,1};
    information->public_layout_truncated=true;
    assert(!navigation->remember_public_routes(*information));
    information->public_layout_truncated=false;
    for(const int count:{-1,plan4::max_groups+1}) {
        information->group_count=count;assert(!navigation->remember_public_routes(*information));
    }
    information->group_count=1;
    for(const int count:{-1,plan4::max_altars+1}) {
        information->altar_count=count;assert(!navigation->remember_public_routes(*information));
    }
    information->altar_count=1;assert(navigation->remember_public_routes(*information));
    information->groups[0].position={45,45};information->altars[0].position={46,46};
    information->public_layout_truncated=true;
    assert(!navigation->remember_public_routes(*information));
    information->public_layout_truncated=false;assert(navigation->remember_public_routes(*information));
    navigation->begin_turn();navigation->warm(cm::round,{1,1});navigation->seal();
    assert(cached(*navigation,{1,1},{2,1})==1&&cached(*navigation,{1,1},{4,1})==3);
    assert(cached(*navigation,{1,1},{46,46})<0&&navigation->pending_fields()==0);

    // An explicitly complete empty layout is legitimate and is retained once.
    navigation=std::make_unique<plan4::PublicNavigation>();
    information->group_count=information->altar_count=0;
    assert(navigation->remember_public_routes(*information));
    information->altar_count=1;assert(navigation->remember_public_routes(*information));
    navigation->begin_turn();navigation->warm(cm::round,{1,1});
    assert(navigation->initialized()&&navigation->vertex_visits()==0&&navigation->pending_fields()==0);
}

void warm_and_prepare_share_progress_and_fresh_origins() {
    namespace cm=cambc::mock;
    cm::reset();std::vector<plan4::Point> walls;
    // The public mock exports enemy spawn tiles as blocked topology. A vertical
    // barrier forces a long detour without adding any SDK test-only API.
    for(int y=0;y<44;++y){walls.push_back({20,y});cm::enemy_spawns.push_back({20,y});}
    auto mixed=std::make_unique<plan4::PublicNavigation>();
    auto ordinary=std::make_unique<plan4::PublicNavigation>();
    auto information=std::make_unique<plan4::Information>();
    information->altar_count=2;information->altars[0].position={42,4};information->altars[1].position={45,8};
    information->group_count=1;information->groups[0].position={4,40};
    mixed->set_vertex_limit(32);ordinary->set_vertex_limit(32);
    int turns=0;
    for(;turns<400;++turns) {
        cm::round=10+turns;cm::fuel=100000;information->round=cm::round;
        information->own.position={3+turns%2,4};
        mixed->begin_turn();ordinary->begin_turn();ordinary->prepare(*information);
        if(turns%2==0)mixed->prepare(*information);
        else mixed->warm(cm::round,information->own.position);
        assert(mixed->vertex_visits()==ordinary->vertex_visits()&&mixed->vertex_visits()<=32);
        assert(mixed->pending_fields()==ordinary->pending_fields());
        assert(mixed->completed_route_entries()==ordinary->completed_route_entries());
        if(turns<3)assert(mixed->pending_fields()==turns+1); // Fair cursor survives both callers.
        mixed->seal();ordinary->seal();
        const auto before=mixed->vertex_visits();const auto entries=mixed->completed_route_entries();
        const auto queries=cm::queries;
        bool complete=true;
        for(const auto origin:{plan4::Point{3,4},plan4::Point{4,4},plan4::Point{4,40}})
            for(const auto target:{plan4::Point{42,4},plan4::Point{45,8}}) {
                const int found=cached(*mixed,origin,target);
                assert(found==cached(*ordinary,origin,target));
                if(found<0){complete=false;continue;}
                assert(found==independent_distance(origin,target,walls));
                std::array<plan4::Point,128> route;
                const auto lookup=mixed->lookup();
                const int count=lookup.route_prefix(origin,target,route.data(),128,lookup.context);
                assert(count==found);auto previous=origin;
                for(int step=0;step<count;++step) {
                    const auto p=route[step];
                    assert(std::abs(p.x-previous.x)<=1&&std::abs(p.y-previous.y)<=1&&p!=previous);
                    assert(std::find(walls.begin(),walls.end(),p)==walls.end());previous=p;
                }
                assert(previous==target);
            }
        assert(mixed->vertex_visits()==before&&mixed->completed_route_entries()==entries&&cm::queries==queries);
        mixed->warm(cm::round,information->own.position); // A sealed phase cannot expand.
        assert(mixed->vertex_visits()==before&&mixed->completed_route_entries()==entries);
        if(complete)break;
    }
    assert(turns>3&&turns<400);
    assert(independent_distance({3,4},{42,4},walls)>39);
}

void fuel_deferral_and_round_jump_do_not_expand() {
    namespace cm=cambc::mock;
    cm::reset();auto navigation=std::make_unique<plan4::PublicNavigation>();
    auto information=std::make_unique<plan4::Information>();
    information->round=cm::round;information->own.position={1,1};
    information->altar_count=1;information->altars[0].position={48,48};
    cm::fuel=55999;navigation->begin_turn();navigation->prepare(*information);
    assert(!navigation->initialized()&&navigation->vertex_visits()==0&&cm::queries==0);
    cm::fuel=100000;cm::shift_after=1;navigation->warm(cm::round,{1,1});
    assert(cm::round==11&&!navigation->initialized()&&navigation->vertex_visits()==0);
    const auto queries=cm::queries;navigation->warm(cm::round,{1,1});
    assert(!navigation->initialized()&&navigation->vertex_visits()==0&&cm::queries==queries);
    navigation->begin_turn();navigation->warm(cm::round,{1,1});
    assert(navigation->initialized()&&navigation->vertex_visits()>0&&navigation->pending_fields()==1);
    const auto visits=navigation->vertex_visits();const auto entries=navigation->completed_route_entries();
    cm::fuel=54999;navigation->warm(cm::round,{1,1});
    assert(navigation->vertex_visits()==visits&&navigation->completed_route_entries()==entries);
    cm::fuel=100000;++cm::round;navigation->warm(cm::round,{1,1});
    assert(navigation->vertex_visits()==visits&&navigation->completed_route_entries()==entries);
    navigation->begin_turn();navigation->warm(cm::round-1,{1,1});
    assert(navigation->vertex_visits()==0&&navigation->completed_route_entries()==entries);
}

void interrupted_target_survives_capacity_saturation() {
    plan4::BoundedNavigation navigation;
    navigation.initialize(32,32,std::vector<unsigned char>(1024),fuel_available);
    const plan4::Point origin{0,0},first{31,31};
    permits=7;navigation.begin_turn(); // Allocate buffers and clear32 bitmap bytes.
    assert(navigation.distance(origin,first)<0&&navigation.vertex_visits()==0);
    assert(navigation.pending_fields()==1&&navigation.cached_distance(origin,first)<0);
    permits=100000;
    // Exactly the former counterexample: sixteen later targets cannot steal
    // the unpublished first slot. The fixed capacity remains sixteen total.
    for(int n=0;n<16;++n){navigation.begin_turn();navigation.distance(origin,{30-n,31});}
    assert(navigation.pending_fields()==16);
    navigation.begin_turn();
    int distance=navigation.distance(origin,first);
    assert(navigation.vertex_visits()>0&&navigation.vertex_visits()<=256);
    int turns=0;
    while(distance<0&&turns++<64){
        navigation.begin_turn();distance=navigation.distance(origin,first);
        assert(navigation.vertex_visits()<=256);
    }
    assert(distance==31&&navigation.cached_distance(first,origin)==31);
    navigation.begin_turn();
    assert(navigation.distance(origin,{15,31})<0&&navigation.vertex_visits()==0); // Seventeenth field was refused.
}

void reversed_initialization_stays_unpublished_until_seeded() {
    std::vector<unsigned char> walls(1024);walls[30*32+30]=1;
    plan4::BoundedNavigation navigation;
    navigation.initialize(32,32,std::move(walls),fuel_available);
    const plan4::Point origin{0,0},target{31,31},neighbor{31,30};
    std::array<plan4::Point,64> route;
    permits=7;navigation.begin_turn();
    assert(navigation.distance(origin,target)<0&&navigation.pending_fields()==1);
    navigation.seal();
    assert(navigation.cached_distance(origin,target)<0&&navigation.cached_distance(target,neighbor)<0);
    assert(navigation.route_prefix(origin,target,route.data(),64)<0); // Direct line hits a public wall.
    permits=100000;navigation.begin_turn();
    // Querying B->A resumes the stored target B, not the new second endpoint A.
    int distance=navigation.distance(target,origin);
    assert(distance<0&&navigation.cached_distance(target,neighbor)==1);
    assert(navigation.cached_distance(origin,target)<0); // No spurious zero-distance origin seed.
    int turns=0;
    while(distance<0&&turns++<64){
        navigation.begin_turn();distance=navigation.distance(target,origin);
        assert(navigation.vertex_visits()<=256&&navigation.pending_fields()==1);
    }
    assert(distance==32&&navigation.cached_distance(origin,target)==32);
    navigation.seal();const int before=navigation.vertex_visits();
    assert(navigation.route_prefix(origin,target,route.data(),64)==32);
    assert(navigation.vertex_visits()==before&&route[31]==target);
}

void colliding_targets_keep_read_only_quotes() {
    namespace cm=cambc::mock;
    cm::reset();terrain_queries=0;
    plan4::BoundedNavigation navigation;
    navigation.initialize(50,50,{},nullptr,open_terrain);
    // These targets share the last bucket, so the collision chain wraps.
    const std::array<plan4::Point,4> targets{{{21,0},{26,1},{44,2},{49,3}}};
    for(const auto target:targets) {
        navigation.begin_turn();
        assert(navigation.distance({0,0},target)<0&&navigation.vertex_visits()==32);
    }
    assert(navigation.completed_pairs()==0);
    navigation.seal();
    const auto visits=navigation.vertex_visits();const auto entries=navigation.completed_route_entries();
    const auto terrain=terrain_queries;const auto queries=cm::queries;
    for(const auto target:targets) {
        const plan4::Point neighbor{target.x,target.y+1};
        assert(navigation.cached_distance(neighbor,target)==independent_distance(neighbor,target));
        assert(navigation.cached_distance(target,neighbor)==1);
    }
    // A missing target in the same chain must remain unknown.
    assert(navigation.cached_distance({38,5},{39,5})<0);
    assert(navigation.vertex_visits()==visits&&navigation.completed_route_entries()==entries);
    assert(terrain_queries==terrain&&cm::queries==queries);
}

void reinitialization_discards_target_index() {
    namespace cm=cambc::mock;
    cm::reset();terrain_queries=0;
    plan4::BoundedNavigation navigation;
    plan4::Point previous{};
    // Vary the target across enough buckets that stale publications could
    // otherwise consume the table, then require a different field slot.
    for(int n=0;n<64;++n) {
        navigation.initialize(50,50,{},nullptr,open_terrain);
        if(n)assert(navigation.cached_distance({previous.x,previous.y-1},previous)<0);
        const plan4::Point target{n%50,40+n/50};
        navigation.begin_turn();
        assert(navigation.distance({0,0},target)<0);
        assert(navigation.cached_distance({target.x,target.y-1},target)==1);
        previous=target;
    }
    const plan4::Point other{48,10};
    navigation.begin_turn();assert(navigation.distance({0,0},other)<0);
    navigation.seal();
    const auto visits=navigation.vertex_visits();const auto entries=navigation.completed_route_entries();
    const auto terrain=terrain_queries;const auto queries=cm::queries;
    assert(navigation.cached_distance({previous.x,previous.y-1},previous)==1);
    assert(navigation.cached_distance({other.x,other.y-1},other)==1);
    assert(navigation.vertex_visits()==visits&&navigation.completed_route_entries()==entries);
    assert(terrain_queries==terrain&&cm::queries==queries);
}

void later_endpoint_field_supplies_missing_quote() {
    namespace cm=cambc::mock;
    cm::reset();terrain_queries=0;
    plan4::BoundedNavigation navigation;
    navigation.initialize(50,50,{},nullptr,open_terrain);
    const plan4::Point first{1,1},later{48,48};
    navigation.begin_turn();assert(navigation.distance({48,1},first)<0);
    navigation.begin_turn();assert(navigation.distance({48,1},later)<0);
    assert(navigation.cached_distance(first,later)<0);
    int turns=0;
    while(navigation.cached_distance(first,later)<0&&turns++<100) {
        navigation.begin_turn();navigation.distance({0,0},later);
        assert(navigation.vertex_visits()<=32);
    }
    assert(turns>1&&turns<100&&navigation.completed_pairs()==0);
    navigation.seal();
    const auto visits=navigation.vertex_visits();const auto entries=navigation.completed_route_entries();
    const auto terrain=terrain_queries;const auto queries=cm::queries;
    const int expected=independent_distance(first,later);
    std::array<plan4::Point,64> route;
    // Only the later field has reached the opposite endpoint. The earlier
    // matching field must not hide that completed quote in either direction.
    assert(navigation.route_prefix(later,first,route.data(),64)<0);
    assert(navigation.route_prefix(first,later,route.data(),64)==expected);
    assert(navigation.cached_distance(first,later)==expected);
    assert(navigation.cached_distance(later,first)==expected);
    // Prefix calls consult terrain; the forecast callback must not.
    const auto after_prefix=terrain_queries;
    assert(navigation.cached_distance(first,later)==expected);
    assert(navigation.vertex_visits()==visits&&navigation.completed_route_entries()==entries);
    assert(terrain_queries==after_prefix&&after_prefix>terrain&&cm::queries==queries);
}
}
int main() {
    topology_only_and_initial_limits();
    complete_layout_is_owned_without_old_observations();
    rejected_layouts_do_not_latch_or_replace_complete_layout();
    warm_and_prepare_share_progress_and_fresh_origins();
    fuel_deferral_and_round_jump_do_not_expand();
    interrupted_target_survives_capacity_saturation();
    reversed_initialization_stays_unpublished_until_seeded();
    colliding_targets_keep_read_only_quotes();
    reinitialization_discards_target_index();
    later_endpoint_field_supplies_missing_quote();
    namespace cm=cambc::mock;
    cm::reset();
    auto nav=std::make_unique<plan4::PublicNavigation>();
    auto info=std::make_unique<plan4::Information>();
    info->round=10;info->own.position={1,1};info->altar_count=1;
    info->altars[0].position={3,3};info->altars[0].accessible=true;
    std::array<plan4::Point,8> route;
    int cases=0;
    nav->prepare(*info);
    assert(!nav->initialized());++cases; // No owner-start source clock.
    nav->begin_turn();nav->prepare(*info);nav->seal();
    assert(nav->initialized());
    assert(plan4::PublicNavigation::route_prefix({1,1},{3,3},route.data(),8,nav.get())==2);++cases;
    assert(route[0]==(plan4::Point{2,2})&&route[1]==(plan4::Point{3,3}));
    info->travel=nav->lookup();
    assert(info->travel.route_prefix && info->travel.route_prefix({1,1},{3,3},route.data(),1,info->travel.context)==1);
    assert(route[0]==(plan4::Point{2,2}));++cases;
    info->own.id=3;info->own.health=info->own.maximum_health=1000;info->own.bag=10;
    info->altars[0].id=20;info->altars[0].lock_known=true;
    plan4::Menu menu;menu.count=1;menu.jobs[0].kind=plan4::JobKind::bank;menu.jobs[0].altar=0;
    auto prefix=std::make_unique<plan4::Forecast>();auto state=std::make_unique<plan4::SimulationState>();
    plan4::PhaseCheckpoint checkpoint;
    const auto prefix_visits=nav->vertex_visits();
    assert(plan4::simulate_first_until_into(*info,menu.jobs[0],menu,11,*prefix,*state,checkpoint));
    assert(state->position==(plan4::Point{2,2}) && state->now==11 && state->bag==10 &&
        checkpoint.phase==plan4::EventKind::travel && checkpoint.remaining==1 &&
        nav->vertex_visits()==prefix_visits);++cases;
    const auto visits=nav->vertex_visits();
    ++cm::round;route[0]={-7,-7};
    assert(info->travel.route_prefix({1,1},{3,3},route.data(),1,info->travel.context)==-1);
    assert(route[0]==(plan4::Point{-7,-7}) && nav->vertex_visits()==visits);++cases;
    assert(plan4::PublicNavigation::route_prefix({1,1},{3,3},route.data(),8,nav.get())==-1);
    assert(route[0]==(plan4::Point{-7,-7})&&nav->vertex_visits()==visits);++cases;
    assert(plan4::PublicNavigation::route_prefix({1,1},{1,1},route.data(),0,nav.get())==-1);++cases;
    info->round=11;nav->prepare(*info);
    assert(plan4::PublicNavigation::route_prefix({1,1},{3,3},route.data(),8,nav.get())==-1);++cases;
    nav->begin_turn();nav->prepare(*info);nav->seal();
    assert(plan4::PublicNavigation::route_prefix({1,1},{3,3},route.data(),8,nav.get())==2);++cases;
    cm::fuel=44999;
    assert(plan4::PublicNavigation::route_prefix({1,1},{3,3},route.data(),8,nav.get())==-1);++cases;
    assert(plan4::PublicNavigation::route_prefix({1,1},{1,1},route.data(),0,nav.get())==-1);++cases;
    cm::fuel=100000;
    assert(plan4::PublicNavigation::route_prefix({1,1},{1,1},route.data(),0,nav.get())==0);++cases;
    std::cout<<"navigation source-clock cases="<<cases<<"; reserved-target scenarios=2; warm scenarios=5; target-index scenarios=3\n";
}
