#pragma once

#include "../../common/pure.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace plan4::governor {

enum class Phase5 : unsigned { travel, clear, capture, bank, restore };
inline constexpr unsigned phase_count = 5;
inline constexpr unsigned all_phases = (1u << phase_count) - 1u;
constexpr unsigned phase_mask(Phase5 phase) { return 1u << unsigned(phase); }
enum class Censor { none, interrupted, lost_sight, ambiguous_endpoint, game_end, mode_change, safety, stat_change };
enum class ReceiptResult { learned, censored, mismatch, duplicate_or_stale, no_start };

struct PhaseStart {
    Phase5 phase = Phase5::travel;
    int actor = -1;
    std::uint64_t job_epoch = 0, phase_sequence = 0;
    int start_round = -1, nominal_duration = 0, mode = -1;
    std::uint32_t stat_epoch = 0;
    bool emergency = false;
};
struct PhaseReceipt {
    Phase5 phase = Phase5::travel;
    int actor = -1;
    std::uint64_t job_epoch = 0, phase_sequence = 0;
    int start_round = -1, end_round = -1, nominal_duration = 0;
    int requested_mode = -1, executed_mode = -1;
    std::uint32_t stat_epoch = 0;
    bool completed = false, endpoint_observed = false, emergency = false;
    Censor censor = Censor::none;
    int legal_wait_rounds = 0, waiting_bound = 0;
    bool waiting_legal = true;
};
struct Entry {
    double residual = 0;
    std::uint32_t completions = 0;
    bool pending = false, frozen = false, has_latest = false;
    PhaseStart start{};
    std::uint64_t latest_job = 0, latest_sequence = 0;
};

// One actor's fixed five-entry table. Both arms run every matching/update step;
// coupling_enabled changes only whether its residual influences a forecast.
class Governor {
    std::array<Entry, phase_count> entries_{};
    bool coupling_enabled_ = true, emergency_ = false;
    std::uint32_t stat_epoch_ = 0;
    int actor_ = -1;
    static bool valid(Phase5 phase) { return unsigned(phase) < phase_count; }
public:
    explicit Governor(bool coupling_enabled = true, std::uint32_t stat_epoch = 0)
        : coupling_enabled_(coupling_enabled), stat_epoch_(stat_epoch) {}

    const Entry& entry(Phase5 phase) const { return entries_[unsigned(phase)]; }
    std::uint32_t stat_epoch() const { return stat_epoch_; }

    bool begin_phase(const PhaseStart& start) {
        if (!valid(start.phase) || start.actor < 0 || start.start_round < 0 ||
            start.nominal_duration <= 0 || start.mode < 0 || start.stat_epoch != stat_epoch_ ||
            (actor_ >= 0 && start.actor != actor_)) return false;
        auto& e = entries_[unsigned(start.phase)];
        if (e.has_latest && (start.job_epoch < e.latest_job ||
            (start.job_epoch == e.latest_job && start.phase_sequence <= e.latest_sequence))) return false;
        actor_ = start.actor;
        e.latest_job = start.job_epoch; e.latest_sequence = start.phase_sequence; e.has_latest = true;
        e.start = start; e.pending = true; e.frozen = emergency_ || start.emergency;
        return true;
    }

    ReceiptResult observe_phase_receipt(const PhaseReceipt& receipt) {
        if (!valid(receipt.phase)) return ReceiptResult::mismatch;
        auto& e = entries_[unsigned(receipt.phase)];
        if (!e.pending) {
            const bool old = e.has_latest && (receipt.job_epoch < e.latest_job ||
                (receipt.job_epoch == e.latest_job && receipt.phase_sequence <= e.latest_sequence));
            return old ? ReceiptResult::duplicate_or_stale : ReceiptResult::no_start;
        }
        const auto& start = e.start;
        if (receipt.actor != start.actor || receipt.job_epoch != start.job_epoch ||
            receipt.phase_sequence != start.phase_sequence || receipt.start_round != start.start_round ||
            receipt.nominal_duration != start.nominal_duration || receipt.requested_mode != start.mode ||
            receipt.stat_epoch != start.stat_epoch) return ReceiptResult::mismatch;
        e.pending = false;
        const bool waiting_ok = receipt.legal_wait_rounds >= 0 && receipt.waiting_bound >= receipt.legal_wait_rounds &&
            receipt.waiting_legal && receipt.end_round >= receipt.start_round &&
            receipt.legal_wait_rounds <= receipt.end_round - receipt.start_round;
        if (!receipt.completed || !receipt.endpoint_observed || receipt.censor != Censor::none ||
            receipt.emergency || emergency_ || e.frozen || receipt.executed_mode != start.mode ||
            receipt.end_round < start.start_round || !waiting_ok || start.stat_epoch != stat_epoch_)
            return ReceiptResult::censored;
        const double d = double(start.nominal_duration);
        const double actual = double(receipt.end_round - start.start_round);
        e.residual = std::clamp(0.75 * e.residual + 0.25 * (actual - d), -d / 2, d / 2);
        if (e.completions < std::numeric_limits<std::uint32_t>::max()) ++e.completions;
        return ReceiptResult::learned;
    }

    int quote_phase(Phase5 phase, int nominal, int legal_minimum) const {
        // Zero duration denotes no phase (already at a destination/restored),
        // not a delayed production phase to which a learned residual applies.
        if (nominal <= 0) return std::max(nominal, legal_minimum);
        double correction = 0;
        if (valid(phase)) {
            const auto& e = entries_[unsigned(phase)];
            if (coupling_enabled_ && e.completions >= 3)
                correction = std::clamp(e.residual, -double(nominal) / 2, double(nominal) / 2);
        }
        const double quoted = std::ceil(std::max({1.0, double(legal_minimum), double(nominal) + correction}));
        return quoted >= double(std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max() : int(quoted);
    }

    void set_emergency(bool active) {
        emergency_ = active;
        if (active) for (auto& e : entries_) if (e.pending) e.frozen = true;
    }
    void on_stat_change(std::uint32_t new_epoch, unsigned affected = all_phases) {
        if (new_epoch == stat_epoch_) return;
        stat_epoch_ = new_epoch;
        for (unsigned p = 0; p < phase_count; ++p) {
            auto& e = entries_[p];
            // Even an unaffected in-flight receipt straddles a changed epoch;
            // close it conservatively, retaining unaffected completed history.
            e.pending = false; e.frozen = false;
            if (affected & (1u << p)) { e.residual = 0; e.completions = 0; }
        }
    }

    static int phase_duration(EventKind kind, int, int nominal, int minimum, int,
                              const Information&, const void* model, void*) {
        if (!model) return std::max(nominal, minimum);
        Phase5 phase;
        switch (kind) {
            case EventKind::travel: phase = Phase5::travel; break;
            case EventKind::clear: phase = Phase5::clear; break;
            case EventKind::capture: phase = Phase5::capture; break;
            case EventKind::bank: phase = Phase5::bank; break;
            case EventKind::heal: phase = Phase5::restore; break;
            default: return std::max(nominal, minimum);
        }
        return static_cast<const Governor*>(model)->quote_phase(phase, nominal, minimum);
    }
    ForecastHooks hooks() const {
        ForecastHooks result;
        result.model = this;
        result.phase_duration = phase_duration;
        return result;
    }
};

} // namespace plan4::governor
