#pragma once
#include "../../common/src/organization.h"
namespace p6::cadence {
// Marcella is the independent owner, avoiding the linked Cassi/Issac death and
// actor-count complication. Ren and Prism take the first two control watches.
inline constexpr organization::Profile profile_value{"cadence",
    {Champion::ren,Champion::prism,Champion::marcella,Champion::varg,Champion::willow}};
}
