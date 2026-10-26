#pragma once
#include "opt/common.hpp"

namespace opt::qualification {
// Paired rows retain both arms; an unhealthy key can never qualify via filtering.
Json summarize(const Json& rows, std::uint64_t seed, std::size_t bootstrap_samples = 2000);
Json case_plan(const Json& config, const fs::path& repo);
}

namespace opt { int run_qualification(const Options&); }
