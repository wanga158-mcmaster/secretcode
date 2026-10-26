#pragma once
// A dated Capture nomination and current capture authority are different facts.
// This helper selects only the next owner of an input; it issues no command,
// edits no task/memory record and never supplies capture or movement authority.
namespace tt::capture_handoff {
enum class Decision { KeepGuardedCapture,ObservationIngress,RevalueWork,RefuteNominee };
struct Facts
{
    bool dated_capture{};
    // Caller binds the selected group, actual origin and current owner input to
    // the returned fresh guard. A new round or an old remembered lifecycle does
    // not establish this premise. Fresh ordinary authority is still required.
    bool same_group_input{};
    bool on_member{};
    // Complete immutable all-source P0, not selected-group exclusion or sleep.
    bool quiet_origin{};
    bool ready{},need_observation{};
    bool unknown{},alive{},respawning{};
};

inline Decision decide(const Facts& facts) noexcept
{
    if(!facts.dated_capture || !facts.same_group_input ||
       (facts.ready && facts.need_observation) ||
       (static_cast<unsigned>(facts.unknown)+static_cast<unsigned>(facts.alive)+
        static_cast<unsigned>(facts.respawning)>1))return Decision::KeepGuardedCapture;
    if(facts.ready && facts.alive)return Decision::RevalueWork;
    if(facts.ready && facts.respawning)return Decision::RefuteNominee;
    if(facts.need_observation && facts.unknown && !facts.on_member && facts.quiet_origin)
        return Decision::ObservationIngress;
    // In particular fresh Reward remains with the real Step/Hold guard. This
    // decision cannot create a receipt, an empty source, a legal landing or a
    // safe hold, nor credit a remembered Reward after a refused fresh guard.
    return Decision::KeepGuardedCapture;
}
} // namespace tt::capture_handoff
