#include "../common/src/native_nav.h"
#include <cstdio>
#include <cstdlib>

namespace {
using p7::live::NativeNav;
int guard_calls = 0, guard_allowances = 0;
bool fuel_guard() { return guard_calls++ < guard_allowances; }
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); std::abort(); \
} } while (false)

void fields() {
    NativeNav nav; nav.init(4, 3, std::vector<unsigned char>(12, 0));
    auto d = nav.field({0});
    CHECK(nav.last_complete && nav.last_exhausted && !nav.last_invalid);
    CHECK(d[0] == 0 && d[3] == 3 && d[8] == 2 && d[11] == 3);
    d = nav.field({0, 11, 0, -1, 99}); CHECK(d[0] == 0 && d[11] == 0 && d[6] == 1);
    d = nav.field({0}, 0);
    CHECK(!nav.last_complete && !nav.last_exhausted && nav.last_expansions == 0);
    CHECK(d[0] == 0 && d[11] == -1);
    d = nav.field({0}, 1); CHECK(nav.last_expansions == 1 && !nav.last_complete);
    CHECK(d[1] == 1 && d[5] == 1 && d[11] == -1);
    d = nav.field({}); CHECK(nav.last_complete && nav.last_exhausted && d[0] == -1);
    nav.init(1, 1, {0}); d = nav.field({0}, 1);
    CHECK(nav.last_complete && nav.last_exhausted && nav.last_expansions == 1);
    nav.init(3, 3, {0}); CHECK(nav.last_invalid);
    CHECK(nav.field({0}).empty() && nav.last_invalid && !nav.last_complete);
}
void routes() {
    NativeNav nav; nav.init(4, 3, std::vector<unsigned char>(12, 0));
    auto r = nav.route(0, 11); CHECK(r.front() == 0 && r.back() == 11 && r.size() == 4);
    CHECK(nav.last_found && nav.last_complete && !nav.last_exhausted);
    CHECK(nav.route(0, 0) == std::vector<int>{0}); CHECK(nav.last_expansions == 0);
    CHECK(nav.route(0, 11, nullptr, 0).empty()); CHECK(!nav.last_complete && !nav.last_exhausted);
    std::vector<unsigned char> extra(12, 0); extra[11] = 1;
    CHECK(nav.route(0, 11, &extra).empty()); CHECK(nav.last_complete && nav.last_exhausted);
    extra.resize(3); CHECK(nav.route(0, 11, &extra).empty()); CHECK(nav.last_invalid && !nav.last_complete);
    // Diagonals have no invented corner prohibition.
    nav.init(2, 2, {0, 1, 1, 0}); CHECK(nav.route(0, 3) == (std::vector<int>{0, 3}));
    nav.init(3, 1, {0, 1, 0}); CHECK(nav.route(0, 2).empty());
    CHECK(nav.last_complete && nav.last_exhausted && !nav.last_found);
    CHECK(nav.route(-1, 2).empty() && nav.last_invalid);
}
void independent_exits() {
    NativeNav nav; nav.init(5, 3, std::vector<unsigned char>(15, 0));
    const auto first = nav.route(5, 9); auto second = nav.disjoint(5, 9, 9);
    CHECK(!first.empty() && !second.empty() && first != second);
    CHECK(nav.last_complete && nav.last_found && !nav.last_exhausted);
    for (std::size_t i = 1; i + 1 < first.size(); ++i)
        for (int tile : second) CHECK(tile != first[i]);
    second = nav.disjoint(5, 9, 14); CHECK(!second.empty() && second.back() == 14);
    nav.init(5, 1, std::vector<unsigned char>(5, 0));
    CHECK(nav.disjoint(0, 4, 4).empty() && nav.last_complete && nav.last_exhausted);
    nav.init(3, 3, std::vector<unsigned char>(9, 0));
    CHECK(nav.disjoint(0, 8, 8, 1).empty() && !nav.last_complete);
    CHECK(nav.last_expansions <= 1);
    // An identical adjacent route is not a distinct exit even though its
    // interior is empty. The alternate takes another real edge.
    nav.init(2, 2, std::vector<unsigned char>(4, 0));
    second = nav.disjoint(0, 3, 3); CHECK(second.size() == 3 && second[1] != 3);
    nav.init(2, 1, {0, 0}); CHECK(nav.disjoint(0, 1, 1).empty() && nav.last_exhausted);
}
void mobility() {
    NativeNav nav; nav.init(3, 1, {0, 1, 0});
    auto r = nav.mobility_route(0, 2, {0, 1, 0});
    CHECK(r.complete && r.tiles == (std::vector<int>{0, 2}));
    CHECK(r.crossings == 1 && r.via == (std::vector<int>{-1, 1}));
    CHECK(nav.last_found && !nav.last_exhausted);
    r = nav.mobility_route(0, 2, {0, 1, 0}, 1);
    CHECK(r.complete && r.tiles.empty() && nav.last_exhausted);
    r = nav.mobility_route(0, 2, {0, 0, 0});
    CHECK(r.complete && r.tiles.empty()); // An ordinary wall cannot become a dash path.
    r = nav.mobility_route(0, 2, {0, 1, 0}, 2, 0);
    CHECK(!r.complete && r.tiles.empty() && !nav.last_complete);
    nav.init(5, 1, {0, 1, 0, 1, 0});
    r = nav.mobility_route(0, 4, {0, 1, 0, 1, 0});
    CHECK(r.complete && r.tiles == (std::vector<int>{0, 2, 4}) && r.crossings == 2);
    nav.init(3, 1, {0, 1, 1});
    r = nav.mobility_route(0, 2, {0, 1, 0}); CHECK(r.complete && r.tiles.empty()); // Blocked landing.
    nav.init(3, 1, {0, 0, 0});
    r = nav.mobility_route(0, 2, {0, 0, 0});
    CHECK(r.complete && r.crossings == 0 && r.tiles == (std::vector<int>{0, 1, 2}));
    r = nav.mobility_route(0, 2, {0}); CHECK(!r.complete && nav.last_invalid);
}
void fuel_cutoffs() {
    NativeNav nav; nav.set_guard(fuel_guard);
    nav.init(40, 1, std::vector<unsigned char>(40, 0)); // Guard survives init.
    guard_calls = 0; guard_allowances = 1;
    CHECK(nav.route(0, 39).empty());
    CHECK(guard_calls == 2 && nav.last_expansions == 16);
    CHECK(!nav.last_complete && !nav.last_exhausted && !nav.last_found);
    guard_calls = 0; guard_allowances = 1;
    auto d = nav.field({0}); CHECK(d[0] == 0 && d[16] == 16 && d[39] == -1);
    CHECK(guard_calls == 2 && nav.last_expansions == 16 && !nav.last_complete && !nav.last_exhausted);
    guard_calls = 0; guard_allowances = 1;
    auto r = nav.mobility_route(0, 39, std::vector<unsigned char>(40, 0));
    CHECK(r.tiles.empty() && !r.complete && !nav.last_complete && !nav.last_exhausted);
    CHECK(guard_calls == 2 && nav.last_expansions == 16);
    guard_calls = 0; guard_allowances = 0;
    CHECK(nav.route(0, 39).empty() && nav.last_expansions == 0 && guard_calls == 1);
    nav.set_guard(nullptr);
    CHECK(!nav.route(0, 39).empty() && nav.last_complete && nav.last_found);
    d = nav.field({0}); CHECK(d[39] == 39 && nav.last_complete && nav.last_exhausted);
}
} // namespace
int main() {
    fields(); routes(); independent_exits(); mobility(); fuel_cutoffs();
    std::puts("plan7 native navigation checks passed");
}
