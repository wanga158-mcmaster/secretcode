#pragma once
// Test-build observation/control hooks only. Normal production builds contain no hook calls.
// A lifecycle probe must charge all executed hook work and preserve engine turn budgets/admission.
#if defined(ZERO_STUDENT_PROBE)
#include "student_decision.h"
namespace zero::student::probe {
void install_profile(FuelBounds&, std::uint64_t& root, std::uint64_t* dispatch,
                     std::uint32_t& dispatch_known, std::uint64_t* refresh, std::uint32_t& refresh_known);
void configure_options(const Context&, DecisionOptions&);
const TeacherSource* source(const Context&);
void decision(const Context&, const Decision&);
void dispatched(const Context&, ActionType, const Cand&, int receipt,
                std::uint64_t before_fuel, std::uint64_t after_fuel);
void init_stage(int stage, std::uint64_t before_fuel, std::uint64_t after_fuel);
int draft_pick(const float* state, const CandSet& candidates, const float* scores, int natural_index);
void drafted(int index, int champion);
}  // namespace zero::student::probe
#endif
