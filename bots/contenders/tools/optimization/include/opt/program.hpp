#pragma once
#include "common.hpp"
#include <cstddef>

namespace opt::program {
constexpr int feature_count = 12;
const std::vector<std::string>& feature_names();
void validate(const Json& tree, int max_depth = 6);
std::size_t nodes(const Json& tree);
int depth(const Json& tree);
float factor(const Json& tree, const std::vector<float>& features);
Json random_tree(std::mt19937_64& rng, int max_depth);
Json mutate(const Json& tree, std::mt19937_64& rng, int max_depth, double sigma = 0.3);
Json crossover(const Json& a, const Json& b, std::mt19937_64& rng, int max_depth);
std::string cpp_header(const Json& tree);
// Synthetic job-ranking probes, not native trajectory equivalence.
std::string ranking_signature(const Json& tree);
} // namespace opt::program
