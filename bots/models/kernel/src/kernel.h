#pragma once
#include "../../common/pure.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <tuple>

namespace plan4::kernel {
inline constexpr int sample_count = 8, bin_limit = 16, item_limit = 32, composition_limit = 32;
inline constexpr int phase_limit = 6, tape_limit = 8;
enum class Macro { one_camp, bank };
enum class Context { solo_visible_context, other_actor_visible };
enum class BagBin { empty, one_to_twenty_four, twenty_five_plus };
enum class TravelBin { at_most_eight, over_eight };
struct Item { int kind = -1; unsigned count = 0; bool operator==(const Item&) const = default; };
struct ExactKit {
    int champion = -1, first_ability = -1, second_ability = -1, attack_range = 0;
    float strength = 0, focus = 0;
    std::array<Item, item_limit> items{};
    int item_count = 0;
    bool known = false;
    bool operator==(const ExactKit& other) const {
        if (item_count < 0 || item_count > item_limit || other.item_count < 0 || other.item_count > item_limit) return false;
        return champion == other.champion && first_ability == other.first_ability && second_ability == other.second_ability &&
            attack_range == other.attack_range && strength == other.strength && focus == other.focus && known == other.known &&
            item_count == other.item_count && std::equal(items.begin(), items.begin() + item_count, other.items.begin());
    }
};
struct Composition {
    std::array<int, composition_limit> kinds{};
    int count = 0;
    bool known = false;
    bool operator==(const Composition& other) const {
        if (count < 0 || count > composition_limit || other.count < 0 || other.count > composition_limit) return false;
        return count == other.count && known == other.known && std::equal(kinds.begin(), kinds.begin() + count, other.kinds.begin());
    }
};
struct BinKey {
    Macro macro = Macro::one_camp;
    ExactKit kit{};
    Composition composition{};
    Context context = Context::solo_visible_context;
    BagBin bag = BagBin::empty;
    TravelBin travel = TravelBin::at_most_eight;
    int health_quarter = 0;
    bool valid = false;
    bool operator==(const BinKey&) const = default;
};
inline BinKey preaction_bin(Macro macro, ExactKit kit, Composition composition, Context context,
    unsigned bag, float health, float maximum_health, int travel) {
    BinKey key;
    if (!kit.known || kit.champion < 0 || kit.item_count < 0 || kit.item_count > item_limit || kit.attack_range < 0 ||
        !std::isfinite(kit.strength) || !std::isfinite(kit.focus) || kit.strength < 0 || kit.focus < 0 ||
        !composition.known || composition.count < 0 || composition.count > composition_limit ||
        !std::isfinite(health) || !std::isfinite(maximum_health) || maximum_health <= 0 || health <= 0 || health > maximum_health || travel < 0 ||
        (macro != Macro::one_camp && macro != Macro::bank) ||
        (context != Context::solo_visible_context && context != Context::other_actor_visible)) return key;
    if ((macro == Macro::bank && composition.count != 0) || (macro == Macro::one_camp && composition.count == 0)) return key;
    std::sort(kit.items.begin(), kit.items.begin() + kit.item_count, [](Item a, Item b) { return a.kind < b.kind; });
    for (int n = 0; n < kit.item_count; ++n)
        if (kit.items[n].kind < 0 || !kit.items[n].count || (n && kit.items[n].kind == kit.items[n - 1].kind)) return key;
    std::sort(composition.kinds.begin(), composition.kinds.begin() + composition.count);
    for (int n = 0; n < composition.count; ++n) if (composition.kinds[n] < 0) return key;
    key.macro = macro; key.kit = kit; key.composition = composition; key.context = context;
    key.bag = bag == 0 ? BagBin::empty : bag < 25 ? BagBin::one_to_twenty_four : BagBin::twenty_five_plus;
    key.health_quarter = int(std::ceil(double(health) * 4 / maximum_health));
    key.travel = travel <= 8 ? TravelBin::at_most_eight : TravelBin::over_eight; key.valid = true;
    return key;
}

struct StartKey {
    unsigned game = 0, actor = 0, round = 0, sequence = 0;
    bool operator==(const StartKey&) const = default;
    bool operator<(const StartKey& other) const {
        return std::tie(game, actor, round, sequence) < std::tie(other.game, other.actor, other.round, other.sequence);
    }
};
enum class Origin { reference_fit, reference_held_out, synthetic_test };
enum class PhaseKind { approach, clear, capture, conversion_approach, conversion, restore };
enum class EconomicKind { group_capture, solo_reward, whole_bag_conversion, whole_bag_loss };
enum class Symbol { target, altar, spawn, failed_approach };
struct Fraction { unsigned numerator, denominator; };
struct PhaseTemplate { PhaseKind kind; Fraction begin, end; };
struct EconomicEvent { EconomicKind kind; int phase; Fraction within; Phase engine_phase; bool death; };
struct StartedRow {
    BinKey bin{}; StartKey key{}; Origin origin = Origin::synthetic_test;
    int duration = 0, nominal_duration = 0;
    bool right_censored = true, required_receipts = false, solo_receipts = false;
    std::array<PhaseTemplate, phase_limit> phases;
    std::array<EconomicEvent, tape_limit> tape;
    int phase_count = 0, event_count = 0;
    double health_loss_fraction = 0;
    bool dead = false;
    Symbol endpoint = Symbol::failed_approach;
    StartedRow() noexcept {}
    StartedRow(const StartedRow& other) { *this = other; }
    StartedRow& operator=(const StartedRow& other) {
        if (this == &other) return *this;
        bin = other.bin; key = other.key; origin = other.origin;
        duration = other.duration; nominal_duration = other.nominal_duration;
        right_censored = other.right_censored; required_receipts = other.required_receipts; solo_receipts = other.solo_receipts;
        phase_count = other.phase_count; event_count = other.event_count;
        std::copy_n(other.phases.begin(), std::clamp(phase_count, 0, phase_limit), phases.begin());
        std::copy_n(other.tape.begin(), std::clamp(event_count, 0, tape_limit), tape.begin());
        health_loss_fraction = other.health_loss_fraction; dead = other.dead; endpoint = other.endpoint;
        return *this;
    }
};
enum class Support { supported, fewer_than_eight, censored, missing_receipt, nonsolo, malformed, duplicate_start };
enum class FreezeMode { production_fit_only, synthetic_tests_allowed };
inline bool fraction(Fraction x) { return x.denominator && x.numerator <= x.denominator; }
inline bool fraction_before(Fraction a, Fraction b) {
    return std::uint64_t(a.numerator) * b.denominator < std::uint64_t(b.numerator) * a.denominator;
}
inline bool row_shape(const StartedRow& row) {
    if (!row.bin.valid || row.duration < 0 || row.nominal_duration <= 0 || row.phase_count < 0 || row.phase_count > phase_limit ||
        row.event_count < 0 || row.event_count > tape_limit || !std::isfinite(row.health_loss_fraction) ||
        unsigned(row.endpoint) > unsigned(Symbol::failed_approach) || unsigned(row.origin) > unsigned(Origin::synthetic_test)) return false;
    unsigned used = 0;
    for (int p = 0; p < row.phase_count; ++p) {
        const auto& phase = row.phases[p];
        if (unsigned(phase.kind) >= phase_limit || !fraction(phase.begin) || !fraction(phase.end) || fraction_before(phase.end, phase.begin) ||
            (used & (1u << unsigned(phase.kind)))) return false;
        used |= 1u << unsigned(phase.kind);
        if (p && fraction_before(phase.begin, row.phases[p - 1].begin)) return false;
    }
    for (int p = 0; p < row.phase_count; ++p) for (int q = 0; q < row.phase_count; ++q) {
        const auto before_kind = row.phases[p].kind, after_kind = row.phases[q].kind;
        if (((before_kind == PhaseKind::approach && (after_kind == PhaseKind::clear || after_kind == PhaseKind::capture)) ||
            (before_kind == PhaseKind::clear && after_kind == PhaseKind::capture)) &&
            fraction_before(row.phases[q].begin, row.phases[p].end)) return false;
    }
    for (int e = 0; e < row.event_count; ++e) {
        const auto& event = row.tape[e];
        if (event.phase < 0 || event.phase >= row.phase_count || !fraction(event.within) ||
            unsigned(event.kind) > unsigned(EconomicKind::whole_bag_loss) || unsigned(event.engine_phase) > unsigned(Phase::owner_end)) return false;
        if ((event.kind == EconomicKind::group_capture || event.kind == EconomicKind::solo_reward) &&
            (row.bin.macro != Macro::one_camp || row.phases[event.phase].kind != PhaseKind::capture || event.engine_phase != Phase::capture)) return false;
        if (event.kind == EconomicKind::whole_bag_conversion &&
            (row.phases[event.phase].kind != PhaseKind::conversion || event.engine_phase != Phase::owner_start)) return false;
        if (event.death != (event.kind == EconomicKind::whole_bag_loss)) return false;
    }
    return true;
}

class FrozenTable {
public:
    struct Bin {
        BinKey key{};
        std::array<std::optional<StartedRow>, sample_count> rows{};
        unsigned started_coverage = 0, selected = 0;
        Support support = Support::fewer_than_eight;
        bool production_qualified = false, duplicate = false;
    };
private:
    std::array<Bin, bin_limit> bins_;
    int count_ = 0;
    bool frozen_ = false;
public:
    // Offline-only freezing. Declared bins precede outcomes; filtering is by
    // the fixed fit partition, never by completion, success or censor status.
    bool freeze(std::span<const BinKey> declared, std::span<const StartedRow> starts,
        FreezeMode mode = FreezeMode::production_fit_only) {
        if (frozen_ || declared.size() > bin_limit) return false;
        count_ = 0;
        for (const auto& key : declared) {
            if (!key.valid) return false;
            for (int b = 0; b < count_; ++b) if (bins_[b].key == key) return false;
            bins_[count_++].key = key;
        }
        for (const auto& row : starts) {
            if (row.origin == Origin::reference_held_out ||
                (row.origin == Origin::synthetic_test && mode != FreezeMode::synthetic_tests_allowed)) continue;
            for (int b = 0; b < count_; ++b) {
                auto& bin = bins_[b]; if (!(row.bin == bin.key)) continue;
                ++bin.started_coverage;
                int place = 0;
                while (place < int(bin.selected) && bin.rows[place]->key < row.key) ++place;
                if (place < int(bin.selected) && bin.rows[place]->key == row.key) { bin.duplicate = true; continue; }
                if (place >= sample_count) continue;
                const int last = std::min(int(bin.selected), sample_count - 1);
                for (int shift = last; shift > place; --shift) bin.rows[shift] = bin.rows[shift - 1];
                bin.rows[place] = row; bin.selected = std::min(unsigned(sample_count), bin.selected + 1);
            }
        }
        for (int b = 0; b < count_; ++b) {
            auto& bin = bins_[b]; bin.production_qualified = bin.selected == sample_count;
            if (bin.selected != sample_count) continue;
            bin.support = bin.duplicate ? Support::duplicate_start : Support::supported;
            for (int k = 0; k < sample_count; ++k) {
                const auto& row = *bin.rows[k];
                bin.production_qualified &= row.origin == Origin::reference_fit;
                if (bin.support != Support::supported) continue;
                if (row.right_censored) bin.support = Support::censored;
                else if (!row.required_receipts) bin.support = Support::missing_receipt;
                else if (!row.solo_receipts) bin.support = Support::nonsolo;
                else if (!row_shape(row)) bin.support = Support::malformed;
            }
            bin.production_qualified &= bin.support == Support::supported;
        }
        frozen_ = true; return true;
    }
    const Bin* find(const BinKey& key) const {
        if (!frozen_) return nullptr;
        for (int b = 0; b < count_; ++b) if (bins_[b].key == key) return &bins_[b];
        return nullptr;
    }
};

struct Binding { Point position{}; bool feasible = false; };
struct Candidate {
    const Information* information = nullptr;
    Job job{};
    BinKey bin{};
    int nominal_duration = 0, minimum_total_duration = 0;
    std::array<int, phase_limit> minimum_phase{{0, 0, 0, 0, 0, -1}};
    std::array<Binding, 4> endpoints{};
    bool continuation_required = false;
};
struct PathScore {
    int terminal_z = 0, first_conversion_round = 0, final_round = 0;
    std::uint64_t our_secured = 0, enemy_secured = 0;
    bool terminal_unknown = false;
};
struct Mapping {
    Forecast first;
    PathScore score{};
    Point endpoint{};
    int duration = 0, endpoint_round = 0, captured_round = -1;
    unsigned endpoint_bag = 0, endpoint_gold = 0;
    float endpoint_health = 0;
    bool alive = true, captured = false, reached_endpoint = false, stopped = false;
};
struct Expectation {
    std::int64_t z_sum = 0;
    std::uint64_t our_sum = 0, enemy_sum = 0, first_conversion_sum = 0;
    unsigned paths = 0, terminal_unknown_paths = 0;
};
enum class Failure { none, unsupported_bin, test_only_table, candidate, invalid_mapping, missing_continuation, invalid_continuation, budget };
struct PairPrediction {
    std::array<std::array<PathScore, sample_count>, 2> paths{};
    std::array<Expectation, 2> expectation{};
    bool use_m0_for_both = true, production_qualified = false;
    Failure failure = Failure::unsupported_bin;
};
enum class Use { production_only, synthetic_tests_allowed };
// The callback must run the SAME deterministic M0 fixed continuation from this
// physical endpoint/depletion state, returning its whole forecast with the exact
// first prefix retained. It never returns another outcome mixture. A production
// checkpoint binding is deliberately not supplied by this pure core.
using Continue = bool (*)(const Candidate&, const Mapping&, Forecast& whole, const void*);
inline bool budget(const Information& info) {
    return !info.work_budget.can_refine || info.work_budget.can_refine(info.work_budget.context);
}

class Kernel {
    std::array<Mapping, sample_count * 2> mapped_;
    Forecast continuation_;
    static bool rounded(unsigned numerator, unsigned denominator, int scale, int& out) {
        if (!denominator || scale < 0) return false;
        const auto product = std::uint64_t(numerator) * unsigned(scale);
        const auto value = product / denominator + (product % denominator != 0);
        if (value > unsigned(std::numeric_limits<int>::max())) return false;
        out = int(value); return true;
    }
    static bool event_equal(const Event& a, const Event& b) {
        return a.kind == b.kind && a.start.round == b.start.round && a.start.phase == b.start.phase &&
            a.end.round == b.end.round && a.end.phase == b.end.phase && a.group_id == b.group_id &&
            a.bag_before == b.bag_before && a.bag_after == b.bag_after && a.secured == b.secured &&
            a.gold_after == b.gold_after && a.health_after == b.health_after && a.status == b.status;
    }
    static int deadline(const Information& info) { return info.round + horizon(info); }
    static PathScore initial_score(const Information& info) {
        PathScore score; score.first_conversion_round = deadline(info) + 1; score.final_round = info.round;
        if (info.our_score >= info.settings.winning_gems && info.enemy_score >= info.settings.winning_gems) score.terminal_unknown = true;
        else if (info.our_score >= info.settings.winning_gems) score.terminal_z = 1;
        else if (info.enemy_score >= info.settings.winning_gems) score.terminal_z = -1;
        return score;
    }
    static bool terminal(const Information& info, const PathScore& score) {
        return score.terminal_z || (info.our_score >= info.settings.winning_gems && info.enemy_score >= info.settings.winning_gems);
    }
    static int legal_minimum(const Candidate& c, PhaseKind kind, unsigned bag) {
        const auto& i = *c.information; int value = c.minimum_phase[unsigned(kind)];
        if (value < 0) return -1;
        if (kind == PhaseKind::conversion) {
            if (!bag) return -1;
            value = std::max(value, tribute_ticks(bag, i.settings));
        } else if (kind == PhaseKind::capture) {
            const auto& group = i.groups[c.job.group];
            const bool own_witness = i.in_progress_group_id == group.id && i.own.position == group.position &&
                group.visible && group.provenance.status == Status::exact_now && group.provenance.observed_round == i.round &&
                group.lifecycle == Lifecycle::cleared && group.capture_started_round >= 0;
            const int remaining = own_witness ? std::max(0, group.capture_started_round + i.settings.capture_rounds - i.round)
                : i.settings.capture_rounds + 1;
            value = std::max(value, remaining);
        }
        else if (kind == PhaseKind::clear && c.bin.macro == Macro::one_camp && i.groups[c.job.group].health > 0) value = std::max(value, 1);
        return value;
    }
    bool map(const Candidate& c, const StartedRow& tape, const StartedRow& timing, const StartedRow& health, Mapping& out) {
        const auto& i = *c.information;
        out.first.reset(); out.score = initial_score(i); out.captured = false; out.captured_round = -1;
        out.reached_endpoint = out.stopped = false; out.alive = true;
        if (!row_shape(tape) || !rounded(unsigned(timing.duration), unsigned(timing.nominal_duration), c.nominal_duration, out.duration) ||
            out.duration < c.minimum_total_duration || out.duration > std::numeric_limits<int>::max() - i.round) return false;
        out.endpoint_round = i.round + out.duration;
        const auto& binding = c.endpoints[unsigned(tape.endpoint)]; if (!binding.feasible) return false;
        if ((tape.endpoint == Symbol::target && (c.bin.macro != Macro::one_camp || binding.position != i.groups[c.job.group].position)) ||
            (tape.endpoint == Symbol::altar && (c.job.altar < 0 || c.job.altar >= i.altar_count || !i.altars[c.job.altar].accessible ||
                binding.position != i.altars[c.job.altar].position)) ||
            (tape.endpoint == Symbol::spawn && binding.position != i.own_spawn)) return false;
        out.endpoint = binding.position;
        std::array<int, phase_limit> starts, ends;
        for (int p = 0; p < tape.phase_count; ++p)
            if (!rounded(tape.phases[p].begin.numerator, tape.phases[p].begin.denominator, out.duration, starts[p]) ||
                !rounded(tape.phases[p].end.numerator, tape.phases[p].end.denominator, out.duration, ends[p]) || ends[p] < starts[p]) return false;
        std::array<Moment, tape_limit> moments;
        bool death = false;
        for (int e = 0; e < tape.event_count; ++e) {
            const auto& event = tape.tape[e]; int within = 0;
            if (!rounded(event.within.numerator, event.within.denominator, ends[event.phase] - starts[event.phase], within)) return false;
            moments[e] = {i.round + starts[event.phase] + within, event.engine_phase};
            if (event.kind != EconomicKind::whole_bag_loss && within != ends[event.phase] - starts[event.phase]) return false;
            if (event.death && moments[e].round != out.endpoint_round) return false;
            if (e && before(moments[e], moments[e - 1])) return false;
            death |= event.death;
        }
        if (death != health.dead) return false;
        const double final_health = double(i.own.health) - health.health_loss_fraction * i.own.maximum_health;
        if (!std::isfinite(final_health) || final_health < -1e-6 || final_health > double(i.own.maximum_health) + 1e-6 ||
            (health.dead && std::abs(final_health) > 1e-6) || (!health.dead && final_health <= 0)) return false;
        bool restore = false;
        for (int p = 0; p < tape.phase_count; ++p) restore |= tape.phases[p].kind == PhaseKind::restore;
        if (health.health_loss_fraction < 0 && (!restore || tape.endpoint != Symbol::spawn)) return false;
        std::uint64_t bag = i.own.bag, gold = i.own.gold, acquired = 0, secured = 0, lost = 0;
        bool captured = false, received = false, dead = false;
        std::array<unsigned, phase_limit> phase_bags;
        for (int p = 0; p < tape.phase_count; ++p) phase_bags[p] = i.own.bag;
        // Validate the FULL mapped tape before applying the horizon. Mapping
        // validity may not condition on which outcomes happen to score in time.
        for (int e = 0; e < tape.event_count; ++e) {
            const auto& event = tape.tape[e];
            if (dead) return false;
            if (event.kind == EconomicKind::group_capture) { if (captured) return false; captured = true; }
            else if (event.kind == EconomicKind::solo_reward) {
                if (!captured || received || c.bin.macro != Macro::one_camp) return false;
                received = true; bag += i.groups[c.job.group].gems; gold += i.groups[c.job.group].gold; acquired += i.groups[c.job.group].gems;
            } else if (event.kind == EconomicKind::whole_bag_conversion) { if (!bag) return false; secured += bag; bag = 0; }
            else { lost += bag; bag = 0; dead = true; }
            if (bag > std::numeric_limits<unsigned>::max() || gold > std::numeric_limits<unsigned>::max()) return false;
        }
        if (captured != received || std::uint64_t(i.own.bag) + acquired != bag + secured + lost) return false;
        // Inventory at each channel START determines its legal timer, even if
        // a capture grows the bag later in the already-running channel.
        for (int p = 0; p < tape.phase_count; ++p) {
            std::uint64_t start_bag = i.own.bag;
            for (int e = 0; e < tape.event_count; ++e) {
                if (before({i.round + starts[p], Phase::owner_action}, moments[e])) break;
                const auto kind = tape.tape[e].kind;
                if (kind == EconomicKind::solo_reward) start_bag += i.groups[c.job.group].gems;
                else if (kind == EconomicKind::whole_bag_conversion || kind == EconomicKind::whole_bag_loss) start_bag = 0;
            }
            if (start_bag > std::numeric_limits<unsigned>::max()) return false;
            phase_bags[p] = unsigned(start_bag);
            const int minimum = legal_minimum(c, tape.phases[p].kind, phase_bags[p]);
            if (minimum < 0 || ends[p] - starts[p] < minimum) return false;
        }
        out.endpoint_bag = unsigned(bag); out.endpoint_gold = unsigned(gold);
        out.endpoint_health = float(std::clamp(final_health, 0.0, double(i.own.maximum_health))); out.alive = !health.dead;
        bag = i.own.bag; gold = i.own.gold;
        out.stopped = terminal(i, out.score); bool prefix_dead = false;
        for (int e = 0; e < tape.event_count && !out.stopped; ++e) {
            if (moments[e].round > deadline(i)) break;
            const auto& event = tape.tape[e]; const unsigned before_bag = unsigned(bag);
            if (event.kind == EconomicKind::group_capture) { out.captured = true; out.captured_round = moments[e].round; continue; }
            Event emitted; emitted.start = emitted.end = moments[e]; emitted.group_id = -1;
            emitted.bag_before = before_bag; emitted.secured = 0; emitted.status = Status::predicted;
            emitted.health_after = i.own.health;
            if (event.kind == EconomicKind::solo_reward) {
                bag += i.groups[c.job.group].gems; gold += i.groups[c.job.group].gold;
                emitted.kind = EventKind::capture; emitted.group_id = i.groups[c.job.group].id;
            } else if (event.kind == EconomicKind::whole_bag_conversion) {
                emitted.kind = EventKind::bank; emitted.secured = unsigned(bag); out.score.our_secured += bag;
                if (out.score.first_conversion_round == deadline(i) + 1) out.score.first_conversion_round = moments[e].round;
                bag = 0;
                if (std::uint64_t(i.our_score) + out.score.our_secured >= i.settings.winning_gems) { out.score.terminal_z = 1; out.stopped = true; }
            } else { emitted.kind = EventKind::abort; bag = 0; emitted.health_after = 0; prefix_dead = true; }
            emitted.bag_after = unsigned(bag); emitted.gold_after = unsigned(gold);
            out.first.events[out.first.event_count++] = emitted;
            out.score.final_round = moments[e].round;
        }
        out.reached_endpoint = !out.stopped && out.endpoint_round <= deadline(i);
        if (!out.stopped) out.score.final_round = std::min(out.endpoint_round, deadline(i));
        out.score.terminal_unknown |= !out.score.terminal_z && deadline(i) == i.settings.max_rounds;
        auto& f = out.first;
        f.macros_started = 1; f.final_round = out.score.final_round; f.first_conversion_round = out.score.first_conversion_round;
        f.our_secured = unsigned(out.score.our_secured); f.final_bag = unsigned(bag); f.final_gold = unsigned(gold);
        f.final_health = out.reached_endpoint || out.endpoint_round == out.score.final_round ? out.endpoint_health : (prefix_dead ? 0 : i.own.health);
        f.terminal_z = out.score.terminal_z; f.terminal_unknown = out.score.terminal_unknown;
        f.completed_conversion = out.score.first_conversion_round <= deadline(i); f.truncated = !out.reached_endpoint && !out.stopped;
        return true;
    }
    bool continuation(const Candidate& c, Mapping& mapping, Continue callback, const void* context) {
        const auto& i = *c.information;
        if (!c.continuation_required || !mapping.reached_endpoint || mapping.stopped || !mapping.alive) return true;
        if (!callback || !callback(c, mapping, continuation_, context)) return false;
        const auto& whole = continuation_;
        if (whole.event_count < mapping.first.event_count || whole.event_count > max_events ||
            whole.final_round < mapping.endpoint_round || whole.final_round > deadline(i) || whole.budget_exhausted) return false;
        for (int e = 0; e < mapping.first.event_count; ++e) if (!event_equal(whole.events[e], mapping.first.events[e])) return false;
        std::uint64_t bag = mapping.endpoint_bag, gold = mapping.endpoint_gold;
        std::array<int, max_events + 1> captured_ids, captured_rounds;
        int captured_count = 0;
        if (mapping.captured) {
            captured_ids[captured_count] = i.groups[c.job.group].id;
            captured_rounds[captured_count++] = mapping.captured_round;
        }
        Moment previous{mapping.endpoint_round, Phase::round_respawn};
        Moment previous_start=previous;
        EventKind previous_kind=EventKind::abort;
        int previous_group=-1;
        for (int e = mapping.first.event_count; e < whole.event_count && !mapping.stopped; ++e) {
            if ((e % 8) == 0 && !budget(i)) return false;
            const auto& event = whole.events[e];
            const bool matching_marker = event.start.round==previous_start.round && event.start.phase==previous_start.phase &&
                event.group_id==previous_group &&
                ((previous_kind==EventKind::capture_start&&event.kind==EventKind::capture)||
                 (previous_kind==EventKind::bank_start&&event.kind==EventKind::bank));
            if ((!matching_marker&&before(event.start, previous)) || before(event.end,previous) ||
                before(event.end, event.start) || event.end.round > deadline(i) ||
                event.bag_before != bag || !std::isfinite(event.health_after) || event.health_after < 0 || event.health_after > i.own.maximum_health) return false;
            if (event.kind == EventKind::capture) {
                const Group* group = nullptr;
                for (int g = 0; g < i.group_count; ++g) if (i.groups[g].id == event.group_id) group = &i.groups[g];
                if (!group) return false;
                int slot = 0;
                while (slot < captured_count && captured_ids[slot] != group->id) ++slot;
                const std::int64_t legal_capture = std::max<std::int64_t>(0, i.settings.capture_rounds) + 1;
                if (slot < captured_count) {
                    if (std::int64_t(event.end.round) < std::int64_t(captured_rounds[slot]) +
                        std::max(0, group->respawn_rounds) + legal_capture) return false;
                } else {
                    if ((group->lifecycle == Lifecycle::captured || group->lifecycle == Lifecycle::respawning) &&
                        (group->respawn_remaining < 0 || std::int64_t(event.end.round) <
                            std::int64_t(i.round) + group->respawn_remaining + legal_capture)) return false;
                    captured_ids[captured_count++] = group->id;
                }
                captured_rounds[slot] = event.end.round;
                bag += allocated_reward(group->gems, group->eligible_allied_occupants, group->own_is_lowest_occupant);
                gold += allocated_reward(group->gold, group->eligible_allied_occupants, group->own_is_lowest_occupant);
            } else if (event.kind == EventKind::bank) {
                if (!bag || event.secured != bag || event.end.round - event.start.round < tribute_ticks(unsigned(bag), i.settings)) return false;
                mapping.score.our_secured += bag;
                if (mapping.score.first_conversion_round == deadline(i) + 1) mapping.score.first_conversion_round = event.end.round;
                bag = 0;
                if (std::uint64_t(i.our_score) + mapping.score.our_secured >= i.settings.winning_gems) {
                    mapping.score.terminal_z = 1; mapping.stopped = true; mapping.score.final_round = event.end.round;
                }
            }
            if (bag > std::numeric_limits<unsigned>::max() || gold > std::numeric_limits<unsigned>::max() ||
                event.bag_after != bag || event.gold_after != gold) return false;
            previous = event.end;
            previous_start=event.start;previous_kind=event.kind;previous_group=event.group_id;
        }
        if (!mapping.stopped) mapping.score.final_round = whole.final_round;
        mapping.score.terminal_unknown |= whole.terminal_unknown && !mapping.score.terminal_z;
        return true;
    }
public:
    Kernel() noexcept {} // Actor-owned heap storage leaves unused event slots untouched.
    // Keep the bounded sixteen-path scratch on the actor-owned heap.
    PairPrediction predict_pair(const FrozenTable& table, const Candidate& candidate, Continue callback = nullptr,
        const void* context = nullptr, Use use = Use::production_only) {
        PairPrediction result;
        if (!candidate.information || !candidate.bin.valid || candidate.nominal_duration <= 0 || candidate.minimum_total_duration < 0) {
            result.failure = Failure::candidate; return result;
        }
        const auto& i = *candidate.information;
        if (!i.own.alive || i.own.channel != Channel::none || i.round < 0 || i.group_count < 0 || i.group_count > max_groups ||
            (candidate.bin.macro == Macro::one_camp && (candidate.job.kind != JobKind::farm || candidate.job.second_group >= 0 ||
                candidate.job.group < 0 || candidate.job.group >= i.group_count)) ||
            (candidate.bin.macro == Macro::bank && candidate.job.kind != JobKind::bank) ||
            !(preaction_bin(candidate.bin.macro, candidate.bin.kit, candidate.bin.composition, candidate.bin.context,
                i.own.bag, i.own.health, i.own.maximum_health, candidate.bin.travel == TravelBin::at_most_eight ? 8 : 9) == candidate.bin)) {
            result.failure = Failure::candidate; return result;
        }
        const auto* bin = table.find(candidate.bin);
        if (!bin || bin->support != Support::supported) return result;
        result.production_qualified = bin->production_qualified;
        if (!bin->production_qualified && use == Use::production_only) { result.failure = Failure::test_only_table; return result; }
        for (int arm = 0; arm < 2; ++arm) for (int k = 0; k < sample_count; ++k) {
            if (!budget(i)) { result.failure = Failure::budget; return result; }
            auto& mapped = mapped_[arm * sample_count + k];
            const int duration = arm == 0 ? k : (k + 1) % sample_count;
            const int health = arm == 0 ? k : (k + 3) % sample_count;
            if (!map(candidate, *bin->rows[k], *bin->rows[duration], *bin->rows[health], mapped)) {
                result.failure = Failure::invalid_mapping; return result;
            }
        }
        // No valid-row filtering: ALL mappings in BOTH arms must also admit
        // the same fixed M0 continuation, or neither arm gets empirical credit.
        for (int arm = 0; arm < 2; ++arm) for (int k = 0; k < sample_count; ++k) {
            if (!budget(i)) { result.failure = Failure::budget; return result; }
            auto& mapped = mapped_[arm * sample_count + k];
            if (candidate.continuation_required && mapped.reached_endpoint && !mapped.stopped && mapped.alive && !callback) {
                result.failure = Failure::missing_continuation; return result;
            }
            if (!continuation(candidate, mapped, callback, context)) {
                result.failure = !budget(i) ? Failure::budget : Failure::invalid_continuation; return result;
            }
        }
        for (int arm = 0; arm < 2; ++arm) for (int k = 0; k < sample_count; ++k) {
            const auto score = mapped_[arm * sample_count + k].score; result.paths[arm][k] = score;
            auto& mean = result.expectation[arm]; ++mean.paths; mean.z_sum += score.terminal_z;
            mean.our_sum += score.our_secured; mean.enemy_sum += score.enemy_secured;
            mean.first_conversion_sum += unsigned(score.first_conversion_round); mean.terminal_unknown_paths += score.terminal_unknown;
        }
        result.use_m0_for_both = false; result.failure = Failure::none; return result;
    }
    const Mapping& path(int arm, int index) const { return mapped_[arm * sample_count + index]; }
};
} // namespace plan4::kernel
