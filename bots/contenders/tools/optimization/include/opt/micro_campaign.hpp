#pragma once
#include "opt/common.hpp"

namespace opt::micro {
bool campaign_requested(const Json& config);
// Uses only retained native input boundaries. Every selected case remains a
// separately resumable invocation of this same executable's single-case search.
}
namespace opt {
bool micro_campaign_requested(const Json&);
int run_micro_campaign(const Options&);
}
