#pragma once
// Service cannot preempt escape, retained acquisition, quiet planning or transit.
// Actual movement freshness and route legality remain caller obligations.
namespace tt::production_schedule {
enum class Gate { ContinueService, YieldToOwner, Handled, Transit, Acquisition };
struct Input
{
    bool escape_required{},return_known{},assembly_pending{},route_pending{},reposition_positive{};
    // Only computation/publication/refresh comparison owns this gate. Waiting
    // for new facts is false so the caller may obtain its next fresh observation.
    bool acquisition_pending{};
};
template<class Escape>
Gate enter(Input input,Escape escape)
{
    if (input.escape_required)
        return input.return_known && escape() ? Gate::Handled : Gate::YieldToOwner;
    if (input.acquisition_pending) return Gate::Acquisition;
    if (input.assembly_pending || input.route_pending)
        return Gate::YieldToOwner;
    if (input.reposition_positive) return Gate::Transit;
    return Gate::ContinueService;
}
inline bool withdraw_authority(bool ready,bool& escape_required)
{
    if (ready) return false;
    escape_required=true;
    return true;
}
} // namespace tt::production_schedule
