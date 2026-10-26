#pragma once
#include "context.h"
#include <algorithm>

namespace p8 {
struct Interval {
    int lower = 0, upper = 0;
    bool valid() const { return lower >= 0 && upper >= lower; }
    bool disjoint(Interval other) const { return upper < other.lower || other.upper < lower; }
};
enum class Mechanism { common_schedule, reposition_two, reposition_four, declared_fourth };
struct Hypothesis {
    Mechanism mechanism = Mechanism::common_schedule;
    Interval offset{};
    PremiseMask required_premises = all_premises;
};
enum class EpisodeKind { return_component, full_organization };
struct ContextDeclaration {
    ContextKey key{};
    TransitionEnvelope envelope{};
    EpisodeKind kind = EpisodeKind::return_component;
    std::array<Hypothesis, max_hypotheses> hypotheses{{
        {Mechanism::common_schedule,{0,0},all_premises},
        {Mechanism::reposition_two,{2,2},all_premises},
        {Mechanism::reposition_four,{4,4},all_premises},
        {Mechanism::declared_fourth,{0,0},all_premises}}};
    int hypothesis_count = 3;
    // Card-scoped endpoints. Existing P8-01 declarations leave these false.
    bool require_spawn_shop = false, require_supported_work = false;
};
enum class Stage { production, return_and_conversion, renewal };
struct EpisodeRequest {
    int context = -1, issue_round = -1, deadline = -1, action = -1;
    int nominal_duration = -1;
    Stage stage = Stage::production;
    Premises premises{};
};
enum class ObservationKind { completed, censored, unavailable, context_invalid, safety_failure };
enum class DurationSource { unavailable, personally_exact, legally_bracketed, personally_unfinished_through };
struct DurationReceipt {
    Interval interval{};
    DurationSource source = DurationSource::unavailable;
    // Unfinished-through gives only [observed owner turns + 1, infinity].
    // Losing vision cannot supply this receipt for subsequent turns.
    int unfinished_through = -1;
    bool continuous_personal_observation = false;
};
enum class ReceiptSource { unavailable, own_observation };
struct CycleReceipts {
    ReceiptSource source = ReceiptSource::unavailable;
    bool production = false, payout_owned = false, conversion = false, renewed = false;
    bool attribution_excludes_other_bag_loss = false;
    // Strictly increasing actor-local receipt serials, including across contexts.
    // These are issued by own-action observation, never battle event/replay IDs.
    int payout_event = -1, conversion_event = -1, renewal_event = -1;
    int production_round = -1, payout_round = -1, conversion_round = -1, renewal_round = -1;
    int positive_reward_lower = 0;
    bool spawn_visited = false, spawn_shop_legal = false;
    bool purchase_attempted = false, purchase_confirmed = false;
    bool supported_roles_witnessed = false;
    int spawn_round = -1;
    bool full_positive() const {
        return source == ReceiptSource::own_observation && production && payout_owned &&
            conversion && renewed && attribution_excludes_other_bag_loss &&
            payout_event >= 0 && conversion_event >= 0 && renewal_event >= 0 &&
            positive_reward_lower > 0;
    }
};
struct Observation {
    int round = -1;
    ObservationKind kind = ObservationKind::unavailable;
    Premises premises{};
    DurationReceipt duration{};
    CycleReceipts receipts{};
};
enum class Status { issued, completed, censored, unavailable, context_invalid, safety_failed };
struct Episode {
    std::uint32_t serial = 0;
    int context = -1, issue_round = -1, deadline = -1, action = -1, latched_epoch = -1;
    Stage stage = Stage::production;
    Premises premises{};
    std::array<Interval, max_hypotheses> prediction{};
    Observation terminal{};
    Status status = Status::issued;
};
struct Outcome {
    std::uint32_t serial = 0;
    int issue_round = -1, terminal_round = -1, latched_epoch = -1;
    Status status = Status::unavailable;
    bool applicable = false, eligible = false, contradiction = false, full_positive = false;
    Episode episode{};
};
struct SlotState {
    ContextDeclaration declaration{};
    std::array<bool, max_hypotheses> consistent{true,true,true,true};
    std::array<Outcome, max_outcomes> outcomes{};
    std::uint16_t attempts = 0, completed = 0, failures = 0, unresolved = 0;
    std::uint16_t consecutive_unresolved = 0, positive_completions = 0;
    int ring_next = 0, latest_terminal = -1, previous_positive = -1, latest_positive = -1;
    int latest_payout_event = -1, latest_conversion_event = -1, latest_renewal_event = -1;
    int eligible_epoch = -1, retired_epoch = -1;
    unsigned eligible_bits = 0, eligible_count = 0;
    bool model_set_failed = false, safety_suspended = false, unresolved_suspended = false;
    bool campaign_started = false, campaign_failed = false, material_premise_changed = false;
    bool rebuild_reserved = false;
    std::uint16_t campaign_episodes = 0;
};
struct CampaignAdmission {
    Premises premises{};
    bool actual_members_visible = false, presently_affordable = false;
    bool individually_safe = false;
    bool actual_spawn = false, confirmed_rebuild = false;
};
struct Resolution {
    bool accepted = false, applicable = false, eligible = false, full_positive = false;
    bool contradiction = false, model_set_failed = false, prediction_supported = false;
    Status status = Status::unavailable;
    unsigned eliminated = 0;
};
class EvidenceLedger {
    std::array<SlotState, max_contexts> slots_{};
    int owner_ = -1, size_ = 0, latest_round_ = -1;
    bool declarations_frozen_ = false, active_present_ = false;
    std::uint32_t next_serial_ = 1;
    Episode active_{};
    std::uint16_t lifetime_failures_ = 0, capacity_exclusions_ = 0;
    int latest_payout_event_ = -1, latest_conversion_event_ = -1, latest_renewal_event_ = -1;
    struct RebuildReservation {
        std::uint32_t token=0;
        int component=-1,organization=-1,reserved_round=-1,proof_round=-1;
        ContextKey source{},target{};
        bool active=false;
    } rebuild_{};
    std::uint32_t next_rebuild_token_=1;
    bool slot_exists(int slot) const { return slot >= 0 && slot < size_; }
    bool current(const SlotState& s, const ContextKey& key, int round) const {
        return key == s.declaration.key && round >= 0 && !suspended_index(s, round / 64);
    }
    static bool suspended_index(const SlotState& s, int epoch) {
        return s.model_set_failed || s.safety_suspended || s.unresolved_suspended ||
            s.campaign_failed || s.material_premise_changed || s.retired_epoch == epoch;
    }
    static bool qualified_index(const SlotState& s, int round) {
        return s.previous_positive >= 0 && s.latest_positive >= s.previous_positive &&
            s.latest_positive - s.previous_positive < qualification_rounds &&
            round >= s.latest_positive && round - s.latest_positive < qualification_rounds;
    }
    static bool declared(const ContextDeclaration& d) {
        if (!valid(d.key) || !d.envelope.valid() || d.hypothesis_count < 1 ||
            d.hypothesis_count > max_hypotheses) return false;
        const auto& b = d.key.build;
        const auto& e = d.envelope;
        const double half = b.max_health * .5, three_quarters = b.max_health * .75;
        const double band_lower = d.key.hp_band == HealthBand::three_quarters_to_full ? three_quarters :
            d.key.hp_band == HealthBand::half_to_three_quarters ? half :
            std::nextafter(0.,std::numeric_limits<double>::infinity());
        const double band_upper = d.key.hp_band == HealthBand::three_quarters_to_full ? b.max_health :
            std::nextafter(d.key.hp_band == HealthBand::half_to_three_quarters ? three_quarters : half,0.);
        if (band_upper < band_lower || e.health.lower > band_lower || e.health.upper < band_upper ||
            !e.max_health.contains(b.max_health) || !e.strength.contains(b.strength) ||
            !e.focus.contains(b.focus) || !e.growth.contains(b.growth)) return false;
        for (int i = 0; i < d.hypothesis_count; ++i) {
            const auto& h = d.hypotheses[i];
            if (!h.offset.valid() || (h.required_premises & all_premises) != all_premises ||
                (h.required_premises & ~all_premises)) return false;
            if (i < 3 && (int(h.mechanism) != i ||
                std::int64_t(h.offset.lower) != std::int64_t(d.hypotheses[0].offset.lower) + i * 2 ||
                std::int64_t(h.offset.upper) != std::int64_t(d.hypotheses[0].offset.upper) + i * 2)) return false;
            for (int j = 0; j < i; ++j)
                if (h.mechanism == d.hypotheses[j].mechanism) return false;
        }
        return d.hypotheses[0].mechanism == Mechanism::common_schedule;
    }
    void failed(SlotState& slot) {
        saturating_increment(slot.failures); saturating_increment(lifetime_failures_);
    }
public:
    explicit EvidenceLedger(int owner) : owner_(owner) {}
    // Called during profile setup, before any issue. Duplicate identities and
    // allocation after observations are rejected; failure slots are never evicted.
    int predeclare(const ContextDeclaration& d) {
        if (declarations_frozen_ || !declared(d) || d.key.actor_id != owner_) return -1;
        for (int i = 0; i < size_; ++i)
            if (slots_[i].declaration.key == d.key && slots_[i].declaration.kind == d.kind) return -1;
        if (size_ == max_contexts) { saturating_increment(capacity_exclusions_); return -1; }
        slots_[size_].declaration = d; return size_++;
    }
    int size() const { return size_; }
    const SlotState* state(int slot) const { return slot_exists(slot) ? &slots_[slot] : nullptr; }
    const Episode* active() const { return active_present_ ? &active_ : nullptr; }
    std::uint16_t lifetime_failure_count() const { return lifetime_failures_; }
    std::uint16_t capacity_exclusions() const { return capacity_exclusions_; }
    bool suspended(int slot, int latched_epoch) const {
        return !slot_exists(slot) || suspended_index(slots_[slot], latched_epoch);
    }
    bool component_qualified(int slot, const ContextKey& key, int round) const {
        return slot_exists(slot) && slots_[slot].declaration.kind == EpisodeKind::return_component &&
            current(slots_[slot], key, round) && qualified_index(slots_[slot], round);
    }
    bool organization_qualified(int slot, const ContextKey& key, int round) const {
        return slot_exists(slot) && slots_[slot].declaration.kind == EpisodeKind::full_organization &&
            current(slots_[slot], key, round) && qualified_index(slots_[slot], round);
    }
    bool campaign_exhausted(int slot) const {
        return !slot_exists(slot) || slots_[slot].campaign_failed ||
            slots_[slot].campaign_episodes >= 2;
    }
    // Invoke on each decision's fresh personally observed context, even while no
    // episode is active. Returning to an earlier key does not resurrect a status
    // that already expired on a material change. New contexts need a predeclared
    // slot; neither a death nor a changed epoch clears this record.
    Applicability observe_context(int index, const Premises& premises, int round) {
        if (!slot_exists(index) || round < latest_round_) return Applicability::unknown;
        auto& slot = slots_[index];
        const auto app = applicability(slot.declaration.key, slot.declaration.envelope, premises, false);
        if (app == Applicability::changed && slot.campaign_started && !slot.material_premise_changed) {
            slot.material_premise_changed = true;
            if (active_present_ && active_.context == index) {
                Observation changed; changed.round = round; changed.kind = ObservationKind::context_invalid;
                changed.premises = premises; resolve(active_.serial, changed);
            } else failed(slot);
        }
        latest_round_ = round;
        return app;
    }
    bool begin_organization_campaign(int component, int organization, int round,
                                     const CampaignAdmission& admission) {
        if (active_present_ || !slot_exists(component) || !slot_exists(organization) ||
            round < latest_round_ || !admission.actual_members_visible ||
            !admission.presently_affordable || !admission.individually_safe) return false;
        auto& target = slots_[organization];
        const auto& source = slots_[component];
        if (target.declaration.kind != EpisodeKind::full_organization || target.campaign_started || target.rebuild_reserved ||
            !component_qualified(component, source.declaration.key, round) ||
            !route_component_matches(source.declaration.key, target.declaration.key) ||
            applicability(target.declaration.key, target.declaration.envelope, admission.premises, true) != Applicability::applicable ||
            suspended(organization, round / 64)) return false;
        target.campaign_started = true; latest_round_ = round; declarations_frozen_ = true;
        return true;
    }
    // Cards which explicitly permit initial commissioning may start a new
    // full organization without a component gate. This is only permission to
    // collect at most two actual episodes, never initial qualification.
    bool begin_initial_campaign(int organization,int round,const CampaignAdmission& admission) {
        if (active_present_ || rebuild_.active || !slot_exists(organization) || round<latest_round_ ||
            !admission.actual_members_visible || !admission.presently_affordable || !admission.individually_safe) return false;
        auto& target=slots_[organization];
        if (target.declaration.kind!=EpisodeKind::full_organization || target.campaign_started ||
            target.rebuild_reserved || target.positive_completions || suspended(organization,round/64) ||
            applicability(target.declaration.key,target.declaration.envelope,admission.premises,true)!=Applicability::applicable)
            return false;
        target.campaign_started=true;latest_round_=round;declarations_frozen_=true;return true;
    }
    // An explicitly declared cross-build experiment is not a transfer of
    // component/organization qualification. Reserve it before any real sale;
    // the old exact-build proof may then expire during the planned rebuild.
    // Only the card's confirmed own transaction chain can begin the new trial.
    std::uint32_t reserve_declared_rebuild(int component,int organization,int round,const ContextKey& target_key) {
        if (active_present_ || rebuild_.active || !next_rebuild_token_ ||
            !slot_exists(component) || !slot_exists(organization) || round<latest_round_) return 0;
        const auto& source=slots_[component];auto& target=slots_[organization];
        if (!component_qualified(component,source.declaration.key,round) ||
            target.declaration.kind!=EpisodeKind::full_organization || target.declaration.key!=target_key ||
            target.campaign_started || target.rebuild_reserved || target.positive_completions ||
            suspended(organization,round/64) ||
            source.declaration.key.actor_id!=target_key.actor_id ||
            source.declaration.key.champion!=target_key.champion ||
            source.declaration.key.settings_id!=target_key.settings_id) return 0;
        rebuild_={next_rebuild_token_++,component,organization,round,source.latest_positive,
            source.declaration.key,target_key,true};
        target.rebuild_reserved=true;declarations_frozen_=true;latest_round_=round;
        return rebuild_.token;
    }
    bool begin_after_declared_rebuild(std::uint32_t token,int round,const CampaignAdmission& admission) {
        if (!token || !rebuild_.active || token!=rebuild_.token || active_present_ ||
            round<latest_round_ || round<rebuild_.reserved_round ||
            round-rebuild_.proof_round>=qualification_rounds ||
            !admission.actual_spawn || !admission.confirmed_rebuild ||
            !admission.actual_members_visible || !admission.presently_affordable || !admission.individually_safe) return false;
        auto& target=slots_[rebuild_.organization];
        if (target.declaration.key!=rebuild_.target || target.campaign_started ||
            target.positive_completions || suspended(rebuild_.organization,round/64) ||
            applicability(target.declaration.key,target.declaration.envelope,admission.premises,true)!=Applicability::applicable)
            return false;
        target.campaign_started=true;rebuild_.active=false;latest_round_=round;
        return true;
    }
    void abandon_declared_rebuild(std::uint32_t token) {
        if (!token || !rebuild_.active || token!=rebuild_.token) return;
        slots_[rebuild_.organization].campaign_failed=true;rebuild_.active=false;
    }
    std::uint32_t issue(const EpisodeRequest& request) {
        if (active_present_ || !slot_exists(request.context) || !next_serial_ ||
            request.issue_round < 0 || request.issue_round < latest_round_ || request.action < 0 || request.nominal_duration < 0 ||
            request.deadline <= request.issue_round || request.deadline - request.issue_round > 120)
            return 0;
        auto& slot = slots_[request.context];
        const auto& d = slot.declaration;
        if (request.issue_round <= slot.latest_terminal || suspended(request.context, request.issue_round / 64) ||
            applicability(d.key, d.envelope, request.premises, true) != Applicability::applicable)
            return 0;
        const bool qualified = qualified_index(slot, request.issue_round);
        if (d.kind == EpisodeKind::return_component) {
            if (slot.campaign_episodes >= 2 || request.premises.state.bag != 0) return 0;
        } else if (!slot.campaign_started || (slot.campaign_episodes >= 2 && !qualified)) return 0;
        Episode next;
        next.serial = next_serial_; next.context = request.context;
        next.issue_round = request.issue_round; next.deadline = request.deadline;
        next.action = request.action; next.stage = request.stage; next.premises = request.premises;
        next.latched_epoch = request.issue_round / 64;
        for (int i = 0; i < d.hypothesis_count; ++i) {
            const auto offset = d.hypotheses[i].offset;
            if (request.nominal_duration > std::numeric_limits<int>::max() - offset.upper) return 0;
            next.prediction[i] = {request.nominal_duration + offset.lower, request.nominal_duration + offset.upper};
        }
        active_ = next; active_present_ = true; declarations_frozen_ = true;
        latest_round_ = request.issue_round; ++next_serial_;
        slot.campaign_started = true;
        if (!qualified) saturating_increment(slot.campaign_episodes);
        saturating_increment(slot.attempts);
        return next.serial;
    }
    Resolution resolve(std::uint32_t serial, const Observation& observation) {
        Resolution out;
        if (!active_present_ || serial != active_.serial || observation.round < latest_round_ ||
            observation.round < active_.issue_round) return out;
        auto& slot = slots_[active_.context];
        const auto& d = slot.declaration;
        out.accepted = true;
        const auto app = applicability(d.key, d.envelope, observation.premises, false);
        out.applicable = app == Applicability::applicable;
        out.status = observation.kind == ObservationKind::safety_failure ? Status::safety_failed :
            observation.kind == ObservationKind::context_invalid || app == Applicability::changed ? Status::context_invalid :
            observation.kind == ObservationKind::censored ? Status::censored :
            observation.kind == ObservationKind::completed && out.applicable &&
                observation.round <= active_.deadline ? Status::completed : Status::unavailable;
        Interval observed = observation.duration.interval;
        bool known_duration = observed.valid() &&
            (observation.duration.source == DurationSource::legally_bracketed ||
             (observation.duration.source == DurationSource::personally_exact && observed.lower == observed.upper));
        if (out.status == Status::censored) {
            known_duration = observation.duration.source == DurationSource::personally_unfinished_through &&
                observation.duration.continuous_personal_observation &&
                observation.duration.unfinished_through >= 0 &&
                observation.duration.unfinished_through <= observation.round - active_.issue_round &&
                observation.duration.unfinished_through < std::numeric_limits<int>::max();
            if (known_duration)
                observed = {observation.duration.unfinished_through + 1, std::numeric_limits<int>::max()};
        }
        if (out.status == Status::completed && !known_duration) out.status = Status::unavailable;
        // No observation/context change is never a zero-duration sample. A
        // censor can only remove predictors contradicted by its observed bound.
        if (out.applicable && known_duration &&
            (out.status == Status::completed || out.status == Status::censored)) {
            for (int i = 0; i < d.hypothesis_count; ++i) {
                const auto required = d.hypotheses[i].required_premises;
                if ((observation.premises.personally_observed & required) != required) continue;
                if (active_.prediction[i].disjoint(observed)) {
                    if (slot.consistent[i]) { slot.consistent[i] = false; out.eliminated |= 1u << i; }
                    if (i == 0 && out.status == Status::completed) out.contradiction = true;
                }
            }
            bool any = false;
            for (int i = 0; i < d.hypothesis_count; ++i) any |= slot.consistent[i];
            if (!any) { slot.model_set_failed = true; out.model_set_failed = true; }
        }
        out.eligible = out.status == Status::completed && known_duration && out.applicable;
        if (out.eligible) {
            for (int i = 0; i < d.hypothesis_count; ++i)
                out.prediction_supported |= slot.consistent[i] &&
                    observed.lower >= active_.prediction[i].lower && observed.upper <= active_.prediction[i].upper;
        }
        if (app == Applicability::changed || observation.kind == ObservationKind::context_invalid)
            slot.material_premise_changed = true;
        const bool fresh_events = observation.receipts.payout_event > latest_payout_event_ &&
            observation.receipts.conversion_event > latest_conversion_event_ &&
            observation.receipts.renewal_event > latest_renewal_event_;
        const auto& receipt = observation.receipts;
        const bool ordered_receipts = active_.issue_round <= receipt.production_round &&
            receipt.production_round <= receipt.payout_round && receipt.payout_round <= receipt.conversion_round &&
            receipt.conversion_round <= receipt.renewal_round && receipt.renewal_round <= observation.round;
        const bool required_endpoints = (!d.require_spawn_shop ||
            (receipt.spawn_visited && receipt.spawn_shop_legal &&
             receipt.conversion_round <= receipt.spawn_round && receipt.spawn_round <= receipt.renewal_round &&
             (!receipt.purchase_attempted || receipt.purchase_confirmed))) &&
            (!d.require_supported_work || receipt.supported_roles_witnessed);
        out.full_positive = out.eligible && out.prediction_supported &&
            receipt.full_positive() && fresh_events && ordered_receipts && required_endpoints &&
            !slot.model_set_failed && !slot.safety_suspended;
        if (out.status == Status::safety_failed) slot.safety_suspended = true;
        if (out.eligible) {
            saturating_increment(slot.completed);
            if (slot.eligible_epoch != active_.latched_epoch) {
                slot.eligible_epoch = active_.latched_epoch; slot.eligible_bits = 0; slot.eligible_count = 0;
            }
            slot.eligible_bits = ((slot.eligible_bits << 1) | unsigned(out.contradiction)) & 7u;
            slot.eligible_count = std::min(3u, slot.eligible_count + 1);
            const unsigned contradictions = (slot.eligible_bits & 1u) + ((slot.eligible_bits >> 1) & 1u) +
                ((slot.eligible_bits >> 2) & 1u);
            if (contradictions >= 2) slot.retired_epoch = active_.latched_epoch;
        }
        if (out.full_positive) {
            slot.previous_positive = slot.latest_positive; slot.latest_positive = observation.round;
            slot.latest_payout_event = observation.receipts.payout_event;
            slot.latest_conversion_event = observation.receipts.conversion_event;
            slot.latest_renewal_event = observation.receipts.renewal_event;
            latest_payout_event_ = observation.receipts.payout_event;
            latest_conversion_event_ = observation.receipts.conversion_event;
            latest_renewal_event_ = observation.receipts.renewal_event;
            saturating_increment(slot.positive_completions); slot.consecutive_unresolved = 0;
            if (out.contradiction) failed(slot);
        } else {
            failed(slot);
            const bool unresolved = out.status == Status::unavailable || out.status == Status::context_invalid ||
                out.status == Status::censored || (out.eligible &&
                    (!receipt.full_positive() || !fresh_events || !ordered_receipts || !out.prediction_supported || !required_endpoints));
            if (unresolved) {
                saturating_increment(slot.unresolved); saturating_increment(slot.consecutive_unresolved);
                if (slot.consecutive_unresolved >= 3) slot.unresolved_suspended = true;
            } else slot.consecutive_unresolved = 0;
            if (slot.campaign_episodes <= 2 && !qualified_index(slot, active_.issue_round))
                slot.campaign_failed = true;
        }
        Outcome outcome{serial, active_.issue_round, observation.round, active_.latched_epoch,
            out.status, out.applicable, out.eligible, out.contradiction, out.full_positive};
        active_.terminal = observation; active_.status = out.status;
        outcome.episode = active_;
        slot.outcomes[slot.ring_next] = outcome; slot.ring_next = (slot.ring_next + 1) % max_outcomes;
        slot.latest_terminal = observation.round;
        // Even an incomplete/invalid own observation consumes its receipt IDs.
        // A later context cannot rebrand already witnessed payout as new work.
        if (receipt.source == ReceiptSource::own_observation) {
            latest_payout_event_ = std::max(latest_payout_event_, receipt.payout_event);
            latest_conversion_event_ = std::max(latest_conversion_event_, receipt.conversion_event);
            latest_renewal_event_ = std::max(latest_renewal_event_, receipt.renewal_event);
        }
        active_present_ = false; latest_round_ = observation.round;
        return out;
    }
    // Death/recall/fuel invalidates the outstanding job only. The actor object,
    // issued serial, failure history, qualification and spent campaigns survive.
    Resolution invalidate_active(int round) {
        if (!active_present_) return {};
        Observation unavailable; unavailable.round = round;
        unavailable.kind = ObservationKind::unavailable;
        return resolve(active_.serial, unavailable);
    }
};
} // namespace p8
