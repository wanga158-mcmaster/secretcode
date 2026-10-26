#pragma once
#include "../../common/pure.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace plan4::covenant {
inline constexpr int premise_limit = 4, failure_limit = 16;
enum class Stage { approach, clear, capture, convert, restore, repair, done };
enum class Truth { satisfied, contradicted, unknown };
enum class PremiseKind { target_available, altar_unlocked, connector_open };
enum class Cause { none, unavailable_camp, blocked_conversion, approach_obstruction, deadline };
enum class RepairKind { none, preserve_progress, alternate_path, convert_retained, renew, replan, safe_retreat_renew };
struct JobKey {
    JobKind kind = JobKind::hold;
    int first_group = -1, second_group = -1, altar = -1;
    bool operator==(const JobKey&) const = default;
};
struct Premise {
    PremiseKind kind = PremiseKind::target_available;
    int subject = -1;
    Truth truth = Truth::unknown;
    Provenance provenance{};
    std::uint64_t revision = 0;
};
struct Acquisition {
    int group_id = -1;
    std::uint64_t stock_epoch = 0;
    Provenance provenance{};
};
struct Observation {
    int round = -1;
    Actor own{};
    Provenance physical_provenance{};
    std::uint64_t revision = 0;
    std::array<Premise, premise_limit> premises{};
    int premise_count = 0;
    bool live_capture = false;
    int capture_group = -1;
    Provenance capture_provenance{};
    // Only a current validated stock/own-receipt association can transfer a
    // completed milestone to a DIFFERENT job. Same-key history needs no replay.
    std::array<Acquisition, 2> acquisitions{};
    int acquisition_count = 0;
};
struct BankOption { int altar_id = -1, nominal_duration = -1; bool accessible = false; };
struct RepairQuotes {
    std::array<BankOption, 2> banks{};
    int bank_count = 0, alternate_path_duration = -1, renew_duration = -1, retreat_duration = -1;
};
struct Repair {
    RepairKind kind = RepairKind::none;
    int altar_id = -1, quoted_duration = -1;
    unsigned retained_bag = 0;
};
struct Suffix {
    Stage stage = Stage::done;
    int group = -1, altar = -1, connector = -1;
    bool operator==(const Suffix&) const = default;
};
struct Contract {
    JobKey key{};
    std::uint64_t epoch = 0, phase_sequence = 0;
    int actor = -1, phase_start = -1, nominal_duration = 0, deadline = -1, route = -1;
    Stage stage = Stage::done;
    int remaining_group = 0;
    std::array<Acquisition, 2> completed{};
    unsigned completed_mask = 0;
    bool converted = false, restored = false, failed = false;
    Suffix failed_suffix{};
    Cause failed_cause = Cause::none;
};
enum class Admit { accepted, retained, protected_progress, failed_suffix, invalid, capacity };
enum class ReceiptResult { advanced, mismatch, stale, unsupported };
enum class Milestone { arrival, clear, capture_receipt, conversion, restoration };
struct Receipt {
    JobKey key{};
    std::uint64_t epoch = 0, phase_sequence = 0;
    int actor = -1, round = -1;
    Milestone milestone = Milestone::arrival;
    Provenance observation{};
    bool observed = false, own_attribution = false;
    int group_id = -1;
    std::uint64_t stock_epoch = 0;
};
struct Guard {
    bool supported = false, activate = false, protected_progress = false, future_unknown = true;
    Cause cause = Cause::none;
    Moment at{0, Phase::owner_action};
    int repair_start_round = -1, blocked_until = -1;
    Repair repair{};
};
struct ProductState {
    Contract contract{};
    Observation physical{};
    Suffix remaining{};
    Guard guard{};
    bool skip_failed_suffix = false;
};
struct ProductPrediction {
    bool use_m0 = true, composed = false, binding_missing = false;
    Guard evidence{};
};
using Continue = bool (*)(const ProductState&, Forecast&, const void*);

inline bool exact(Provenance p, int round) {
    return p.observed_round == round && p.status == Status::exact_now &&
        (p.source == Source::personal_vision || p.source == Source::own_state || p.source == Source::own_occupancy);
}
inline bool current(const Observation& o) {
    return o.round >= 0 && o.own.id >= 0 && o.premise_count >= 0 && o.premise_count <= premise_limit &&
        o.acquisition_count >= 0 && o.acquisition_count <= 2 &&
        std::isfinite(o.own.health) && std::isfinite(o.own.maximum_health) && o.own.health >= 0 && o.own.health <= o.own.maximum_health &&
        o.physical_provenance.source == Source::own_state && exact(o.physical_provenance, o.round);
}
inline bool protected_progress(const Observation& o) {
    return current(o) && o.own.alive && (o.own.channel != Channel::none ||
        (o.live_capture && o.capture_group >= 0 && o.capture_provenance.source == Source::own_occupancy && exact(o.capture_provenance, o.round)));
}
inline int promise_deadline(int start, int nominal) {
    if (start < 0 || nominal < 0) return -1;
    const auto value = std::int64_t(start) + (std::int64_t(3) * nominal + 1) / 2 + 2;
    return value <= std::numeric_limits<int>::max() ? int(value) : -1;
}
inline int connector_id(Point point) {
    constexpr int side = 16384;
    return point.x >= 0 && point.x < side && point.y >= 0 && point.y < side ? point.y * side + point.x : -1;
}
inline Repair minimal_repair(Cause cause, const Observation& o, const RepairQuotes& quotes, int blocked_altar = -1) {
    Repair result; result.retained_bag = o.own.bag;
    if (protected_progress(o)) { result.kind = RepairKind::preserve_progress; result.quoted_duration = 0; return result; }
    if (cause == Cause::none) { result.quoted_duration = 0; return result; }
    if (cause == Cause::approach_obstruction) {
        result.kind = RepairKind::alternate_path; result.quoted_duration = quotes.alternate_path_duration; return result;
    }
    if (cause == Cause::blocked_conversion) {
        if (o.own.bag && quotes.bank_count >= 2 && quotes.banks[1].accessible && quotes.banks[1].altar_id != blocked_altar) {
            result.kind = RepairKind::convert_retained; result.altar_id = quotes.banks[1].altar_id;
            result.quoted_duration = quotes.banks[1].nominal_duration; return result;
        }
        result.kind = RepairKind::safe_retreat_renew; result.quoted_duration = quotes.retreat_duration; return result;
    }
    if (o.own.bag) {
        for (int a = 0; a < std::clamp(quotes.bank_count, 0, 2); ++a) if (quotes.banks[a].accessible) {
            result.kind = RepairKind::convert_retained; result.altar_id = quotes.banks[a].altar_id;
            result.quoted_duration = quotes.banks[a].nominal_duration; return result;
        }
        result.kind = RepairKind::safe_retreat_renew; result.quoted_duration = quotes.retreat_duration;
    } else if (o.own.health < o.own.maximum_health) {
        result.kind = RepairKind::renew; result.quoted_duration = quotes.renew_duration;
    } else { result.kind = RepairKind::replan; result.quoted_duration = 1; }
    return result;
}

// The SAME actual monitor and repair table run in both arms. enabled_ affects
// only product prediction. No method here invokes an engine action or changes
// a physical bag, channel, health value or completed acquisition.
class Covenant {
    struct Failure {
        Suffix suffix{}; Cause cause = Cause::none;
        int failed_round = -1, until = -1;
        std::uint64_t revision = 0;
        bool used = false;
    };
    Contract contract_{};
    std::array<Failure, failure_limit> failures_{};
    bool accepted_ = false, enabled_ = true;
    bool approach_connector_bound_ = false;
    Point approach_origin_{};
    int last_round_ = -1;
    std::uint64_t epoch_ = 0;
    static Suffix suffix(const Contract& contract) {
        if (contract.failed) return contract.failed_suffix;
        const bool camp = contract.stage == Stage::approach || contract.stage == Stage::clear || contract.stage == Stage::capture;
        const int group = camp && contract.key.kind == JobKind::farm ?
            (contract.remaining_group == 0 ? contract.key.first_group : contract.key.second_group) : -1;
        return {contract.stage, group,
            contract.stage == Stage::convert || (contract.stage == Stage::approach && contract.key.kind == JobKind::bank) ? contract.key.altar : -1,
            contract.stage == Stage::approach || contract.stage == Stage::convert ? contract.route : -1};
    }
    static bool relevant(const Premise& premise, Suffix target) {
        if (premise.kind == PremiseKind::target_available)
            return (target.stage == Stage::approach || target.stage == Stage::clear || target.stage == Stage::capture) && premise.subject == target.group;
        if (premise.kind == PremiseKind::altar_unlocked) return target.stage == Stage::convert && premise.subject == target.altar;
        return (target.stage == Stage::approach || target.stage == Stage::convert) && target.connector >= 0 && premise.subject == target.connector;
    }
    static Cause contradiction(const Observation& o, Suffix target) {
        Cause selected = Cause::none;
        for (int p = 0; p < o.premise_count; ++p) {
            const auto& premise = o.premises[p];
            if (!relevant(premise, target) || premise.truth != Truth::contradicted || !exact(premise.provenance, o.round)) continue;
            const Cause cause = premise.kind == PremiseKind::target_available ? Cause::unavailable_camp :
                premise.kind == PremiseKind::altar_unlocked ? Cause::blocked_conversion : Cause::approach_obstruction;
            if (selected == Cause::none || unsigned(cause) < unsigned(selected)) selected = cause;
        }
        return selected;
    }
    static bool resolved(const Failure& failure, const Observation& o) {
        if (o.round >= failure.until) return true;
        for (int p = 0; p < o.premise_count; ++p) {
            const auto& premise = o.premises[p];
            if (premise.truth != Truth::satisfied || !exact(premise.provenance, o.round) || premise.revision <= failure.revision) continue;
            if (failure.cause == Cause::deadline && premise.kind == PremiseKind::connector_open && relevant(premise, failure.suffix)) return true;
            if (failure.cause == Cause::unavailable_camp && premise.kind == PremiseKind::target_available && relevant(premise, failure.suffix)) return true;
            if (failure.cause == Cause::blocked_conversion && premise.kind == PremiseKind::altar_unlocked && relevant(premise, failure.suffix)) return true;
            if (failure.cause == Cause::approach_obstruction && premise.kind == PremiseKind::connector_open && relevant(premise, failure.suffix)) return true;
        }
        return false;
    }
    void reconcile_failures(const Observation& o) { for (auto& failure : failures_) if (failure.used && resolved(failure, o)) failure.used = false; }
    const Failure* blocked(Suffix target, const Observation& o) const {
        for (const auto& failure : failures_) if (failure.used && failure.suffix == target && !resolved(failure, o)) return &failure;
        return nullptr;
    }
    bool blocks_admission(Suffix target, const Observation& o) const {
        for (const auto& failure : failures_) {
            if (!failure.used || resolved(failure, o) || failure.suffix.stage != target.stage ||
                failure.suffix.group != target.group || failure.suffix.altar != target.altar) continue;
            if (failure.suffix.connector == target.connector) return true;
            // Changing an integer route identity cannot restart an expired
            // promise. A different connector needs a newly exact route proof;
            // it cannot resolve an unavailable camp or blocked altar.
            bool route_proved = false;
            if ((failure.cause == Cause::deadline || failure.cause == Cause::approach_obstruction) &&
                target.connector >= 0 && contradiction(o, target) == Cause::none) {
                for (int p = 0; p < o.premise_count; ++p) {
                    const auto& premise = o.premises[p];
                    route_proved |= premise.kind == PremiseKind::connector_open && premise.subject == target.connector &&
                        premise.truth == Truth::satisfied && premise.revision > failure.revision && exact(premise.provenance, o.round);
                }
            }
            if (!route_proved) return true;
        }
        return false;
    }
    bool fail(Suffix target, Cause cause, const Observation& o) {
        for (auto& failure : failures_) if (failure.used && failure.suffix == target) return true; // Never extend a failed promise by retrying it.
        for (auto& failure : failures_) if (!failure.used || resolved(failure, o)) {
            failure = {target, cause, o.round, o.round <= std::numeric_limits<int>::max() - 8 ? o.round + 8 : std::numeric_limits<int>::max(), o.revision, true};
            return true;
        }
        return false;
    }
    static bool valid_stage(Stage stage) { return unsigned(stage) <= unsigned(Stage::done) && stage != Stage::repair; }
public:
    explicit Covenant(bool enabled = true) : enabled_(enabled) {}
    void set_enabled(bool enabled) { enabled_ = enabled; }
    const Contract& contract() const { return contract_; }
    bool accepted() const { return accepted_; }
    Suffix remaining() const { return suffix(contract_); }
    bool bind_approach_connector(int connector, const Observation& o) {
        if (!accepted_ || !current(o) || !o.own.alive || o.own.id != contract_.actor || o.round < last_round_ ||
            connector < 0 || connector > connector_id({16383, 16383}) || protected_progress(o) ||
            suffix(contract_).stage != Stage::approach) return false;
        // A proposed route binds the observed obstruction without renewing the
        // promise. An already failed suffix cannot be renamed by a proposal.
        if (contract_.failed) return contract_.failed_cause == Cause::approach_obstruction &&
            contract_.failed_suffix.connector == connector && contract_.route == connector && approach_connector_bound_;
        contract_.route = connector; approach_origin_ = o.own.position; approach_connector_bound_ = true;
        last_round_ = o.round; return true;
    }
    bool alternate_route_progress(int old_connector, int new_connector, int remaining, const Observation& o) {
        if (!accepted_ || !contract_.failed || contract_.stage != Stage::repair ||
            contract_.failed_cause != Cause::approach_obstruction || contract_.failed_suffix.stage != Stage::approach ||
            contract_.failed_suffix.connector != old_connector || contract_.route != old_connector ||
            !approach_connector_bound_ || !current(o) || !o.own.alive || o.own.id != contract_.actor ||
            o.round < last_round_ || protected_progress(o) || o.own.position == approach_origin_ ||
            old_connector < 0 || new_connector < 0 || old_connector == new_connector ||
            connector_id(o.own.position) != new_connector || remaining < 0 || promise_deadline(o.round, remaining) < 0) return false;
        Failure* resolved_failure = nullptr;
        for (auto& failure : failures_) if (failure.used && failure.cause == Cause::approach_obstruction &&
            failure.suffix == contract_.failed_suffix && o.revision > failure.revision) {
            for (int p = 0; p < o.premise_count; ++p) {
                const auto& premise = o.premises[p];
                if (premise.kind == PremiseKind::connector_open && premise.subject == new_connector &&
                    premise.truth == Truth::satisfied && premise.revision > failure.revision && exact(premise.provenance, o.round) &&
                    (premise.provenance.source == Source::own_state || premise.provenance.source == Source::own_occupancy))
                    resolved_failure = &failure;
            }
        }
        if (!resolved_failure) return false;
        resolved_failure->used = false;
        contract_.failed = false; contract_.failed_cause = Cause::none; contract_.stage = Stage::approach;
        contract_.route = new_connector; contract_.phase_start = o.round; contract_.nominal_duration = remaining;
        contract_.deadline = promise_deadline(o.round, remaining); ++contract_.phase_sequence;
        approach_origin_ = o.own.position; last_round_ = o.round; return true;
    }
    Admit accept(JobKey key, Stage stage, int nominal, int route, const Observation& o) {
        if (!current(o) || !o.own.alive || o.round < last_round_ || !valid_stage(stage) || nominal < 0 || promise_deadline(o.round, nominal) < 0 ||
            (key.kind == JobKind::farm && (key.first_group < 0 || key.first_group == key.second_group || key.altar < 0)) ||
            (key.kind == JobKind::bank && key.altar < 0) ||
            (accepted_ && contract_.actor != o.own.id)) return Admit::invalid;
        last_round_ = o.round;
        if (protected_progress(o) && (!accepted_ || contract_.key != key)) return Admit::protected_progress;
        reconcile_failures(o);
        if (accepted_ && contract_.key == key && !contract_.failed && contract_.stage != Stage::done) return Admit::retained;
        Contract next;
        if (accepted_ && contract_.key == key && contract_.failed) {
            next = contract_; next.stage = contract_.failed_suffix.stage; next.failed = false;
        } else {
            next.key = key; next.stage = stage; next.actor = o.own.id; next.route = route;
            for (int a = 0; a < o.acquisition_count; ++a) {
                const auto& acquisition = o.acquisitions[a];
                if (acquisition.group_id < 0 || !acquisition.stock_epoch || !exact(acquisition.provenance, o.round)) continue;
                if (acquisition.group_id == key.first_group) { next.completed[0] = acquisition; next.completed_mask |= 1u; }
                if (acquisition.group_id == key.second_group) { next.completed[1] = acquisition; next.completed_mask |= 2u; }
            }
            if (key.kind == JobKind::farm && next.completed_mask & 1u) {
                if (key.second_group >= 0 && !(next.completed_mask & 2u)) { next.remaining_group = 1; next.stage = Stage::approach; }
                else next.stage = Stage::convert;
            }
        }
        next.route = route;
        if (blocks_admission(suffix(next), o)) return Admit::failed_suffix;
        next.epoch = ++epoch_; ++next.phase_sequence; next.phase_start = o.round; next.nominal_duration = nominal;
        next.deadline = promise_deadline(o.round, nominal); next.failed = false; next.failed_cause = Cause::none;
        contract_ = next; accepted_ = true; approach_connector_bound_ = false; return Admit::accepted;
    }
    ReceiptResult complete(const Receipt& receipt, int next_nominal, int next_route, const Observation& o,
        bool restoration_requested = false) {
        if (!accepted_ || !current(o) || !o.own.alive || o.own.id != contract_.actor || o.round < last_round_ ||
            receipt.key != contract_.key || receipt.actor != contract_.actor ||
            receipt.epoch != contract_.epoch || receipt.phase_sequence != contract_.phase_sequence) return ReceiptResult::mismatch;
        if (receipt.round != o.round || receipt.round < contract_.phase_start) return ReceiptResult::stale;
        if (!receipt.observed || !exact(receipt.observation, o.round) || next_nominal < 0 || promise_deadline(o.round, next_nominal) < 0)
            return ReceiptResult::unsupported;
        const Stage stage = contract_.failed ? contract_.failed_suffix.stage : contract_.stage;
        const auto completed_suffix = suffix(contract_);
        const Milestone expected = stage == Stage::approach ? Milestone::arrival : stage == Stage::clear ? Milestone::clear :
            stage == Stage::capture ? Milestone::capture_receipt : stage == Stage::convert ? Milestone::conversion : Milestone::restoration;
        if (stage == Stage::done || receipt.milestone != expected) return ReceiptResult::mismatch;
        if (stage == Stage::capture) {
            const int group = contract_.remaining_group == 0 ? contract_.key.first_group : contract_.key.second_group;
            if (!receipt.own_attribution || !receipt.stock_epoch || receipt.group_id != group ||
                (receipt.observation.source != Source::own_state && receipt.observation.source != Source::own_occupancy)) return ReceiptResult::unsupported;
            contract_.completed[contract_.remaining_group] = {group, receipt.stock_epoch, receipt.observation};
            contract_.completed_mask |= 1u << contract_.remaining_group;
        }
        last_round_ = o.round;
        if (stage == Stage::approach) contract_.stage = contract_.key.kind == JobKind::farm ? Stage::clear :
            contract_.key.kind == JobKind::bank ? Stage::convert : Stage::restore;
        else if (stage == Stage::clear) contract_.stage = Stage::capture;
        else if (stage == Stage::capture && contract_.remaining_group == 0 && contract_.key.second_group >= 0 && !(contract_.completed_mask & 2u)) {
            contract_.remaining_group = 1; contract_.stage = Stage::approach;
        } else if (stage == Stage::capture) contract_.stage = Stage::convert;
        else if (stage == Stage::convert) { contract_.converted = true; contract_.stage = restoration_requested ? Stage::restore : Stage::done; }
        else { contract_.restored = true; contract_.stage = Stage::done; }
        // An exact progress receipt resolves the old suffix even if it arrives
        // after a failed duration promise. Physical progress is never rolled back.
        for (auto& failure : failures_) if (failure.used && failure.suffix == completed_suffix) failure.used = false;
        contract_.failed = false; contract_.failed_cause = Cause::none; contract_.route = next_route;
        contract_.phase_start = o.round; contract_.nominal_duration = next_nominal;
        contract_.deadline = promise_deadline(o.round, next_nominal); ++contract_.phase_sequence;
        approach_connector_bound_ = false;
        return ReceiptResult::advanced;
    }
    Guard observe(const Observation& o, const RepairQuotes& quotes) {
        Guard guard; guard.at = {o.round, Phase::owner_action};
        if (!accepted_ || !current(o) || o.round < last_round_ || contract_.actor != o.own.id || !o.own.alive) return guard;
        last_round_ = o.round;
        reconcile_failures(o);
        if (protected_progress(o)) { guard.protected_progress = true; guard.repair = minimal_repair(Cause::none, o, quotes); return guard; }
        const auto target = suffix(contract_);
        Cause cause = contradiction(o, target);
        if (cause == Cause::none && contract_.failed) {
            if (blocked(target, o)) cause = contract_.failed_cause;
            else return guard; // Resolved/aged failure needs explicit re-admission, not a fresh automatic timeout.
        }
        if (cause == Cause::none && contract_.stage != Stage::done && o.round > contract_.deadline) cause = Cause::deadline;
        if (cause == Cause::none) return guard;
        guard.supported = guard.activate = true; guard.future_unknown = false; guard.cause = cause; guard.repair_start_round = o.round;
        guard.repair = minimal_repair(cause, o, quotes, target.altar);
        if (!fail(target, cause, o)) { guard.supported = false; return guard; }
        contract_.failed_suffix = target; contract_.failed_cause = cause; contract_.failed = true; contract_.stage = Stage::repair;
        if (const auto* failure = blocked(target, o)) guard.blocked_until = failure->until;
        return guard;
    }
    ReceiptResult complete_repair(const Receipt& receipt,const Observation& o) {
        if (!accepted_ || !contract_.failed || !current(o) || !o.own.alive || receipt.key!=contract_.key ||
            receipt.epoch!=contract_.epoch || receipt.phase_sequence!=contract_.phase_sequence ||
            receipt.actor!=contract_.actor || o.own.id!=contract_.actor) return ReceiptResult::mismatch;
        if (receipt.round!=o.round || receipt.round<last_round_) return ReceiptResult::stale;
        if (!receipt.observed || !receipt.own_attribution || receipt.observation.source!=Source::own_state ||
            !exact(receipt.observation,o.round) || o.own.channel!=Channel::none) return ReceiptResult::unsupported;
        if (receipt.milestone==Milestone::conversion && o.own.bag==0) contract_.converted=true;
        else if (receipt.milestone==Milestone::restoration && o.own.health==o.own.maximum_health) contract_.restored=true;
        else return ReceiptResult::unsupported;
        // Repair closes the running contract, not the failure's eight-turn
        // memory. It grants no acquisition or score and cannot restart a promise.
        contract_.stage=Stage::done; contract_.failed=false; contract_.failed_cause=Cause::none;
        last_round_=o.round; ++contract_.phase_sequence; return ReceiptResult::advanced;
    }
    Guard forecast_guard(const JobKey& key, Suffix target, int predicted_next_finish, const Observation& o, const RepairQuotes& quotes) const {
        Guard guard; guard.at = {o.round, Phase::owner_action};
        if (!current(o) || o.round < last_round_ || !o.own.alive) return guard;
        if (protected_progress(o)) { guard.protected_progress = true; guard.repair = minimal_repair(Cause::none, o, quotes); return guard; }
        const Cause cause = contradiction(o, target);
        if (cause != Cause::none) {
            guard.supported = guard.activate = true; guard.future_unknown = false; guard.cause = cause; guard.repair_start_round = o.round;
        } else if (accepted_ && contract_.key == key && suffix(contract_) == target && contract_.failed && blocked(target, o)) {
            guard.supported = guard.activate = true; guard.future_unknown = false; guard.cause = contract_.failed_cause; guard.repair_start_round = o.round;
        } else if (accepted_ && contract_.key == key && suffix(contract_) == target && !contract_.failed && contract_.stage != Stage::done && contract_.deadline >= 0 &&
            predicted_next_finish >= 0 && predicted_next_finish > contract_.deadline) {
            guard.supported = guard.activate = true; guard.future_unknown = false; guard.cause = Cause::deadline;
            guard.at = {std::max(o.round, contract_.deadline), Phase::owner_end};
            guard.repair_start_round = guard.at.round < std::numeric_limits<int>::max() ? guard.at.round + 1 : guard.at.round;
        }
        if (guard.activate) guard.repair = minimal_repair(guard.cause, o, quotes, target.altar);
        if (const auto* failure = blocked(target, o)) guard.blocked_until = failure->until;
        return guard;
    }
    ProductPrediction predict_product(const JobKey& key, Suffix target, int predicted_next_finish, const Observation& o,
        const RepairQuotes& quotes, const Forecast& flat_m0, Forecast& out, Continue continuation = nullptr, const void* context = nullptr) const {
        ProductPrediction result; result.evidence = forecast_guard(key, target, predicted_next_finish, o, quotes);
        out = flat_m0;
        if (!enabled_ || !result.evidence.activate || result.evidence.protected_progress || !result.evidence.supported) return result;
        if (!continuation || result.evidence.repair.quoted_duration < 0) { result.binding_missing = true; return result; }
        ProductState product{contract_, o, target, result.evidence, true};
        if (!continuation(product, out, context)) { out = flat_m0; result.binding_missing = true; return result; }
        result.use_m0 = false; result.composed = true; return result;
    }
};
} // namespace plan4::covenant
