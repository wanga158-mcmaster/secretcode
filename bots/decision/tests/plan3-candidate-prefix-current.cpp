#include "../common/candidate-prefix.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

// This pure WASI fixture does not exercise C++ exception unwinding.
extern "C" void* __cxa_allocate_exception(std::size_t size) { return std::malloc(size); }
extern "C" void __cxa_throw(void*, void*, void(*)(void*)) { std::abort(); }

namespace cp = plan3_runtime::candidate_prefix;
unsigned checks = 0;
void require(bool condition) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "candidate prefix check %u failed\n", checks); std::abort(); }
}
void unique_bounded(const std::vector<cp::Entry>& result) {
    require(result.size() <= 16);
    for (std::size_t i = 0; i < result.size(); ++i) {
        require(result[i].distance >= 0 && result[i].index >= 0 && result[i].key >= 0);
        for (std::size_t j = 0; j < i; ++j) require(!cp::same_group(result[i], result[j]));
    }
}

int main() {
    std::vector<cp::Entry> input;
    for (int i = 0; i < 24; ++i) input.push_back({i, i, 100+i, false});
    for (int i = 0; i < 4; ++i) input.push_back({100+i, 24+i, 200+i, true});
    const auto dense = cp::select(input);
    unique_bounded(dense);
    require(dense.size() == 16);
    require(std::count_if(dense.begin(), dense.end(), [](const auto& e) { return e.gem; }) == 4);
    for (int i = 0; i < 4; ++i) {
        require(dense[2*i].index == 24+i);
        require(dense[2*i+1].index == i);
    }
    for (std::size_t i = 8; i < dense.size(); ++i) require(dense[i].index == static_cast<int>(i)-4);
    std::reverse(input.begin(), input.end());
    require(cp::select(input) == dense);
    for (std::size_t rotation = 0; rotation < input.size(); ++rotation) {
        std::rotate(input.begin(), input.begin()+1, input.end());
        require(cp::select(input) == dense);
    }

    for (bool gem : {false, true}) {
        input.clear();
        for (int i = 31; i >= 0; --i) input.push_back({i, i, 100+i, gem});
        const auto one = cp::select(input);
        unique_bounded(one);
        require(one.size() == 16);
        for (std::size_t i = 0; i < one.size(); ++i) require(one[i].index == static_cast<int>(i) && one[i].gem == gem);
    }
    input.clear();
    for (int i = 0; i < 20; ++i) {
        input.push_back({100+i, i, 100+i, true});
        input.push_back({i, 20+i, 200+i, false});
    }
    const auto balanced = cp::select(input);
    unique_bounded(balanced);
    require(balanced.size() == 16);
    for (std::size_t i = 0; i < balanced.size(); ++i) {
        require(balanced[i].gem == (i%2 == 0));
        require(balanced[i].index == static_cast<int>(i/2) + (i%2 ? 20 : 0));
    }

    input = {{5, 2, 20, true}, {5, 1, 10, true}, {5, 0, 10, true},
             {8, 1, 10, true}, {0, 3, 30, false}, {0, 3, 30, false}};
    const auto ties = cp::select(input);
    unique_bounded(ties);
    require(ties.size() == 3 && ties[0].index == 0 && ties[1].index == 3 && ties[2].index == 2);
    std::reverse(input.begin(), input.end());
    require(cp::select(input) == ties);

    // The same physical group cannot appear twice, even across reward strata.
    input = {{1, 1, 10, true}, {1, 1, 10, false}, {2, 2, 20, false},
             {-1, 3, 30, true}, {0, -1, 40, true}, {0, 4, -1, false}};
    const auto duplicate = cp::select(input);
    unique_bounded(duplicate);
    require(duplicate.size() == 2 && duplicate[0].index == 1 && duplicate[1].index == 2);
    require(cp::select(std::span<const cp::Entry>{}).empty());
    input = {{std::numeric_limits<int>::max(), 0, 30, true},
             {std::numeric_limits<int>::max(), 1, 20, true}};
    require(cp::select(input).front().key == 20); // Comparisons never subtract overflowing ints.
    std::printf("{\"status\":\"pass\",\"checks\":%u,\"maximumCandidates\":16,\"nativeGames\":0}\n", checks);
}
