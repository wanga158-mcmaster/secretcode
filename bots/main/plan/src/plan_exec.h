#pragma once
#include <cambc.h>
#include <vector>
#include "econ_plan.h"
// Team plan executor (cfg::team_plan, plan_exec.cpp): free functions Brain's turn reads.
namespace mb::pexec {
// Holding on spawn this turn while the opening plan is prepared (point fields, then the search).
bool holding();
// A health recall (v53 rule) started or is being prepared this turn (gap diagnostics).
void note_lowhp();
// Draft phase (cfg::team_plan_draft): plan for our roster and write it to draft memory; each draft
// turn continues the search while fuel allows. `order`: the draft preference order.
void draft_turn(const std::vector<cambc::champion_draft>& order);
// Gameplay: the draft memory's plan remapped to roster slots (false: absent or another roster).
bool decode_draft(econ::Plan& out);
// Team replans (cfg::team_plan_sync): called at every turn start (alive or dead) before the slots
// are read; publishes our progress byte, runs the replan schedule, adopts new plans.
void sync(int round, bool alive);
void set_status(int status);
void activate_sync();  // an in-game opening plan (built on the same round everywhere) enables replans
// Plan mode slot protocol: the shared bytes carry plan progress; decode one into (group, status).
bool slots_active();
void decode_slot(std::size_t i, std::uint8_t byte, int& group, int& status);
} // namespace mb::pexec
