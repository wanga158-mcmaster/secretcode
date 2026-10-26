#include "../archipelago/src/archipelago.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace plan4;
namespace a = plan4::archipelago;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks; if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
void finish(a::Graph& graph, int quantum = 17) {
    int turns = 0;
    while (!graph.complete() && turns++ < 20000) {
        const auto p = graph.advance(quantum);
        check(p.work <= quantum && p.vertex_visits <= p.work,"decomposition respects aggregate bounded work");
    }
    check(graph.complete(),"iterative decomposition eventually completes");
}
void route(a::RouteSearch& search, const a::Graph& graph, Point from, Point to,
           const Point* avoided = nullptr, int count = 0, int quantum = 5) {
    check(search.begin(graph,from,to,avoided,count),"valid ordinary BFS request accepted");
    for (int turns = 0; search.result().status == a::RouteStatus::pending && turns < 20000; ++turns) {
        const auto p = search.advance(quantum);
        check(p.work <= quantum && p.vertex_visits <= p.work,"ordinary BFS/reconstruction shares bounded work");
    }
    check(search.result().status != a::RouteStatus::pending,"ordinary BFS eventually resolves");
}
std::vector<unsigned char> line(int width) {
    std::vector<unsigned char> walls(std::size_t(width)*3,1);
    for (int x = 0; x < width; ++x) walls[width+x] = 0;
    return walls;
}
void articulation_and_coupling() {
    auto graph = std::make_unique<a::Graph>();
    check(graph->initialize(7,3,line(7)),"public line initialized"); finish(*graph,3);
    check(graph->component_count() == 6,"each bridge edge is one biconnected block");
    for (int x = 0; x < 7; ++x)
        check(graph->articulation({x,1}) == (x>0 && x<6),"true articulation follows eight-neighbor topology");
    check(graph->edge_component({1,1},{2,1}) == graph->edge_component({2,1},{1,1})
        && graph->edge_component({1,1},{2,1}) != graph->edge_component({2,1},{3,1}),
          "block labels are undirected and separate bridges");
    auto first = std::make_unique<a::RouteSearch>(), second = std::make_unique<a::RouteSearch>();
    route(*first,*graph,{0,1},{5,1}); route(*second,*graph,{0,1},{6,1});
    check(first->result().distance == 5 && first->result().complete_path,"base route is exact and complete");
    auto active = std::make_unique<a::State>(), control = std::make_unique<a::State>(false);
    for (auto* state : {active.get(),control.get()}) {
        check(state->bind_route(0,*graph,first->result()) && state->bind_route(1,*graph,second->result()),
              "identical job routes enter both arms");
    }
    a::BlockedTile blocker{{3,1},true,true,{10,Source::personal_vision,Status::exact_now}};
    active->observe(*graph,10,7,&blocker,1); control->observe(*graph,10,7,&blocker,1);
    const auto attempted = active->repair_plan(*graph,0,0,10);
    const auto dependent = active->repair_plan(*graph,1,0,10);
    const auto isolated = control->repair_plan(*graph,1,0,10);
    check(attempted.repair && dependent.repair && !isolated.repair,
          "fresh connector evidence propagates to both dependent jobs only in active arm");
    check(control->repair_plan(*graph,0,0,10).repair,"common attempted-route occupancy repair preserved in control");
    auto repair = std::make_unique<a::RouteSearch>();
    route(*repair,*graph,{0,1},{6,1},dependent.avoid.data(),dependent.avoid_count);
    check(repair->result().status == a::RouteStatus::unreachable,"blocked true articulation disconnects conditional current graph");
    active->observe(*graph,11,7,nullptr,0); control->observe(*graph,11,7,nullptr,0);
    const auto lost = active->repair_plan(*graph,1,0,11);
    check(!lost.repair && lost.uncertain_risk && lost.base_distance == 6,
          "loss of sight leaves uncertain risk without disconnected-world fact");
    route(*repair,*graph,{0,1},{6,1});
    check(repair->result().distance == 6,"temporary occupant never enters permanent terrain");
    blocker.blocked = false; blocker.provenance.observed_round = 12;
    active->observe(*graph,12,7,&blocker,1);
    check(!active->repair_plan(*graph,1,0,12).uncertain_risk,"fresh free connector clears historical risk");
    blocker.blocked = true; blocker.provenance = {13,Source::model,Status::predicted};
    active->observe(*graph,13,7,&blocker,1);
    check(!active->repair_plan(*graph,1,0,13).repair,"predicted blocker is never fresh exact occupancy");
    check(!active->observe(*graph,14,7,nullptr,1),"missing blocker prefix rejects safely");
    check(!active->repair_plan(*graph,1,0,14).repair,"malformed prefix cannot retain exact blockage");
}
void alternate_route_and_diagonal() {
    auto graph = std::make_unique<a::Graph>();
    std::vector<unsigned char> walls(35,1);
    for (int x = 0; x < 7; ++x) walls[7+x] = walls[21+x] = 0;
    walls[14] = walls[20] = 0;
    graph->initialize(7,5,std::move(walls)); finish(*graph);
    check(!graph->articulation({3,1}),"second physical route removes articulation premise");
    check(graph->component_count() == 1,"ring is a single biconnected block");
    auto base = std::make_unique<a::RouteSearch>(); route(*base,*graph,{0,1},{6,1});
    auto active = std::make_unique<a::State>(), control = std::make_unique<a::State>(false);
    for (auto* state : {active.get(),control.get()}) {
        state->bind_route(0,*graph,base->result()); state->bind_route(1,*graph,base->result());
        a::BlockedTile blocker{{3,1},true,true,{4,Source::personal_vision,Status::exact_now}};
        state->observe(*graph,4,7,&blocker,1);
        const auto plan = state->repair_plan(*graph,0,0,4);
        check(plan.repair && !state->repair_plan(*graph,1,0,4).repair,
              "non-connector blockage stays attempted-route evidence in both arms");
        auto search = std::make_unique<a::RouteSearch>();
        route(*search,*graph,{0,1},{6,1},plan.avoid.data(),plan.avoid_count);
        check(search->result().status == a::RouteStatus::found && search->result().distance == 8,
              "same base BFS selects the available legal alternative");
        for (int n = 0; n < search->result().count; ++n)
            check(search->result().points[n] != Point{3,1},"alternative never crosses a current known blocker");
    }
    graph->initialize(2,2,{0,1,1,0}); finish(*graph);
    route(*base,*graph,{0,0},{1,1});
    check(base->result().distance == 1 && graph->component_count() == 1,
          "destination-only diagonal semantics do not invent corner walls");
    check(!active->repair_plan(*graph,0,0,4).supported,"terrain reinitialization invalidates old route dependencies");
    check(!graph->initialize(2,2,{0,1,1,0},a::Diagonals::unsupported),"unsupported diagonal semantics use fallback");
}
bool deny(const void*) { return false; }
int connected_components(unsigned walkable, int removed = -1) {
    unsigned visited = 0; int components = 0;
    for (int start = 0; start < 9; ++start) {
        if (start == removed || !(walkable & (1u<<start)) || (visited & (1u<<start))) continue;
        ++components; std::array<int,9> queue{}; int count = 1; queue[0] = start; visited |= 1u<<start;
        for (int head = 0; head < count; ++head) {
            const int current = queue[head];
            for (int next = 0; next < 9; ++next) {
                if (next == removed || !(walkable & (1u<<next)) || (visited & (1u<<next))) continue;
                if (std::abs(current%3-next%3) <= 1 && std::abs(current/3-next/3) <= 1) {
                    visited |= 1u<<next; queue[count++] = next;
                }
            }
        }
    }
    return components;
}
void exhaustive_small_topology() {
    auto graph = std::make_unique<a::Graph>();
    for (unsigned mask = 0; mask < 512; ++mask) {
        std::vector<unsigned char> walls(9);
        for (int v = 0; v < 9; ++v) walls[v] = !(mask & (1u<<v));
        graph->initialize(3,3,std::move(walls));
        for (int turn = 0; !graph->complete() && turn < 10; ++turn) graph->advance();
        check(graph->complete(),"all 3x3 topology decompositions finish under finite bounded work");
        const int before = connected_components(mask);
        for (int v = 0; v < 9; ++v) if (mask & (1u<<v))
            check(graph->articulation({v%3,v/3}) == (connected_components(mask,v) > before),
                  "articulation agrees with independent vertex-removal connectivity on every 3x3 map");
    }
}
void progress_and_limits() {
    auto graph = std::make_unique<a::Graph>();
    graph->initialize(50,50,std::vector<unsigned char>(2500,0));
    const auto denied = graph->advance(256,{deny,nullptr});
    check(denied.work == 0 && denied.budget_exhausted,"denied preprocessing performs no graph work");
    finish(*graph,256);
    check(graph->component_count() == 1 && !graph->articulation({25,25}),"large open map has no false connector");
    auto search = std::make_unique<a::RouteSearch>(); route(*search,*graph,{0,0},{49,49},nullptr,0,256);
    check(search->result().distance == 49 && search->result().count == 50,"large bounded BFS remains shortest");
    graph->initialize(300,3,line(300)); finish(*graph,256);
    route(*search,*graph,{0,1},{299,1},nullptr,0,256);
    check(search->result().distance == 299 && !search->result().complete_path,
          "long route preserves exact distance but exposes bounded dependency-path limit");
    auto state = std::make_unique<a::State>();
    check(!state->bind_route(0,*graph,search->result()),"incomplete dependency path falls back without fabricated access");
    check(!graph->initialize(0,3,{}),"invalid public dimensions rejected");
    check(!graph->initialize(2,2,{0}),"incomplete public topology rejected");
    graph->initialize(1,1,{0}); finish(*graph);
    check(graph->component_count() == 1 && !graph->articulation({0,0}),"isolated walkable vertex is its own block");
}
}
int main() {
    articulation_and_coupling(); alternate_route_and_diagonal(); exhaustive_small_topology(); progress_and_limits();
    std::printf("archipelago checks=%d synthetic-only legal-witness=pending runtime-leg-binding=pending\n",checks);
}
