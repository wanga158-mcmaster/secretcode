#pragma once
#include <cambc.h>
#include <vector>

namespace tt {

// One draft turn: pick, then advance the planning job and write the plan into draft memory.
void play_draft();

// The bodies (champion types) one draft choice brings: two for Cassi and Issac.
std::vector<cambc::champion_type> bodies_of(cambc::champion_draft d);

} // namespace tt
