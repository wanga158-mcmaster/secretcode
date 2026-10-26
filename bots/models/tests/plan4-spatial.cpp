#include "../common/spatial-evidence.h"
#include <cassert>
#include <iostream>
#include <memory>
namespace cm=cambc::mock;
using namespace plan4;
using namespace plan4::runtime;
int main() {
    auto info=std::make_unique<Information>();info->round=10;info->own.id=0;info->own.alive=info->own.recall_safe=true;
    auto context=std::make_unique<SpatialEvidence>();int cases=0;Menu menu;
    const PublicTopology topology{50,50,true,[](Point p,const void*){return p.x>=0&&p.y>=0&&p.x<50&&p.y<50&&p!=(Point{2,2});},nullptr};
    std::vector<cambc::camp> camps;std::vector<cambc::champion> enemies;
    auto setup=[&]{cm::reset();cm::query_cost=25;cm::visible.fill(true);info->round=10;info->observation_truncated=false;
        info->own.recall_safe=true;camps.clear();enemies.clear();};
    auto collect=[&]{context->start(*info,menu,topology,camps,enemies,nullptr,nullptr,77,40000);};
    setup();cm::actors.push_back({7,{3,4},true,true,cambc::team::ally});cm::camp_blocks={{5,5}};cm::teleport_blocks={{6,6}};
    cm::visible[cm::tile({9,9})]=false;collect();const auto& f=context->frame();
    assert(f.current_complete&&f.own_recall_here&&f.graph_quota==77);
    assert(f.reference_vertices_used==0&&f.optional_fuel_floor==40000);++cases;
    context->start(*info,menu,topology,camps,enemies,nullptr,nullptr,77,40000,115);
    assert(f.reference_vertices_used==115&&f.optional_fuel_floor==40000);++cases;
    const int callbacks_before=cm::queries;
    assert(f.occupancy({0,0},f.occupancy_context)==TileOccupancy::free);
    for(const Point p:std::array<Point,3>{{{3,4},{5,5},{6,6}}})assert(f.occupancy(p,f.occupancy_context)==TileOccupancy::occupied);
    assert(f.occupancy({9,9},f.occupancy_context)==TileOccupancy::unknown);
    assert(f.occupancy({50,0},f.occupancy_context)==TileOccupancy::unknown);
    assert(!f.topology.walkable({2,2},f.topology.context));++cases;
    assert(cm::queries==callbacks_before); // Pure model lookups issue zero host calls.
    setup();camps.emplace_back(0,cambc::vec2{2,0});collect();assert(!f.own_recall_here&&f.current_complete);++cases;
    setup();camps.emplace_back(0,cambc::vec2{2,0},100,0);collect();assert(f.own_recall_here);++cases;
    setup();camps.emplace_back(0,cambc::vec2{4,0});collect();assert(f.own_recall_here);++cases;
    setup();camps.emplace_back(0,cambc::vec2{4,0});cm::visible[cm::tile({4,0})]=false;collect();assert(!f.own_recall_here&&!cm::unauthorized);++cases;
    setup();cm::actors.push_back({8,{8,0},true,true,cambc::team::enemy});enemies.emplace_back(8);collect();assert(!f.own_recall_here);++cases;
    setup();cm::actors.push_back({8,{9,0},true,true,cambc::team::enemy});enemies.emplace_back(8);collect();assert(f.own_recall_here);++cases;
    setup();cm::actors.push_back({8,{9,0},true,false,cambc::team::enemy});enemies.emplace_back(8);collect();assert(!f.own_recall_here&&!cm::unauthorized);++cases;
    setup();info->observation_truncated=true;collect();assert(!f.own_recall_here&&f.current_complete);++cases;
    setup();info->own.recall_safe=false;collect();assert(!f.own_recall_here);++cases;
    setup();auto absent=topology;absent.complete=false;context->start(*info,menu,absent,camps,enemies,nullptr,nullptr,999,40000);
    assert(!f.current_complete&&f.graph_quota==192&&f.occupancy({0,0},f.occupancy_context)==TileOccupancy::unknown);++cases;
    setup();camps.emplace_back(0,cambc::vec2{4,0});cm::actors.push_back({8,{9,0},true,true,cambc::team::enemy});enemies.emplace_back(8);
    collect();const int calls=cm::queries;
    for(int cut=1;cut<=calls;++cut) {
        setup();camps.emplace_back(0,cambc::vec2{4,0});cm::actors.push_back({8,{9,0},true,true,cambc::team::enemy});enemies.emplace_back(8);
        cm::stop_after=cut;collect();assert(!f.own_recall_here&&!cm::unauthorized);++cases;
        setup();camps.emplace_back(0,cambc::vec2{4,0});cm::actors.push_back({8,{9,0},true,true,cambc::team::enemy});enemies.emplace_back(8);
        cm::shift_after=cut;collect();assert(!f.current_complete&&!f.own_recall_here&&!cm::unauthorized);++cases;
    }
    setup();cm::fuel=40000;collect();assert(!f.current_complete&&!f.own_recall_here&&!cm::queries);++cases;
    std::cout<<"plan4 spatial evidence "<<cases<<" cases passed\n";
}
