#pragma once

#include "../../common/pure.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace plan4::foundry {

inline constexpr const char* version = "plan4-foundry/2";

// Eight fixed-menu sources plus at most four distinct observed ally sources.
inline constexpr int actor_limit = 5, group_limit = 12, record_limit = 2048;

enum class Ambiguity {
    none, missing_snapshot, inconsistent_snapshot, multiple_candidates,
    own_attack, interrupted_coverage, possible_other_source, unsupported_intent
};

// This is a record of legal observations, not an attack-event witness. In
// particular, lack of an observed spell is not proof that no spell occurred.
struct ParticipationEvidence {
    int actor_id = -1, group_id = -1;
    double previous_health = 0, current_health = 0;
    Provenance previous{}, current{};
    int eligible_nearby_allies = 0;
    bool candidate_eligible = false, own_intervening_attack = false;
    bool continuous_coverage = false;
    bool possible_spell = true, possible_departed_actor = true, possible_hidden_rival = true;
};
struct Participation {
    bool supported = false, ambiguous = true;
    Ambiguity reason = Ambiguity::missing_snapshot;
    Provenance provenance{};
};
inline Participation infer_participation(const ParticipationEvidence& e) {
    Participation r;
    const auto legal = [](Provenance p) {
        return p.source == Source::personal_vision && p.status == Status::exact_now && p.observed_round >= 0;
    };
    if (!legal(e.previous) || !legal(e.current)) return r;
    if (e.actor_id < 0 || e.group_id < 0 || e.previous.observed_round >= e.current.observed_round ||
        !std::isfinite(e.previous_health) || !std::isfinite(e.current_health) ||
        e.current_health < 0 || e.previous_health <= e.current_health) {
        r.reason = Ambiguity::inconsistent_snapshot; return r;
    }
    if (e.eligible_nearby_allies != 1 || !e.candidate_eligible) {
        r.reason = Ambiguity::multiple_candidates; return r;
    }
    if (e.own_intervening_attack) { r.reason = Ambiguity::own_attack; return r; }
    if (!e.continuous_coverage) { r.reason = Ambiguity::interrupted_coverage; return r; }
    if (e.possible_spell || e.possible_departed_actor || e.possible_hidden_rival) {
        r.reason = Ambiguity::possible_other_source; return r;
    }
    r.supported = true; r.ambiguous = false; r.reason = Ambiguity::none;
    r.provenance = {e.current.observed_round, Source::model, Status::predicted};
    return r;
}

enum class Intent { none, attack, hold, bank, tribute, recall };
enum class Mode { inactive, approach_work, attack, approach_capture, hold, approach_bank, tribute, recall,
                  done, dead, own_wait, own_escape, own_heal, waiting_stock };
struct GroupInput {
    int id = -1;
    Point position{};
    double health = 0, full_health = 0, incoming_per_round = 0;
    double full_incoming_per_round = -1; // Explicit full-respawn quote for cyclic own work.
    unsigned gems = 0, gold = 0;
    int respawn_rounds = 0, attack_range = 3;
    bool health_known = false, reward_available = false, awake = false;
    bool contested = false, unmodeled_occupant = false;
    // A known ongoing countdown is preserved; -1 starts only after occupancy.
    int capture_remaining = -1, respawn_remaining = -1;
    Provenance health_provenance{};
};
struct ActorToken {
    int id = -1, order = 0, group = -1;
    Intent intent = Intent::none;
    Point position{}, attack_position{}, capture_position{}, recall_destination{};
    double health = 0, maximum_health = 0, work_per_action = 0;
    unsigned bag = 0;
    bool own = false, visible = false, alive = true;
    bool capture_position_is_public_spawn = false, on_target_spawn = false;
    bool initial_turn_pending = true, initial_owner_start_pending = false;
    bool channel_type_known = false, channel_remaining_known = false;
    int channel_remaining = 0;
    Provenance observation{};
    ParticipationEvidence participation{};
};
struct Input {
    Settings settings{};
    int round = 0, horizon = max_horizon, actor_count = 0, group_count = 0, altar_count = 0;
    unsigned our_score = 0, enemy_score = 0;
    std::array<ActorToken, actor_limit> actors{};
    std::array<GroupInput, group_limit> groups{};
    std::array<Altar, max_altars> altars{};
    TravelLookup travel{};
    WorkBudget work_budget{};
};
struct OwnTarget {
    Point attack_position{}, capture_position{};
    // The adapter must quote each later target separately from legal public
    // geometry/current evidence; initial-target coordinates are never reused.
    bool supported = false, capture_position_is_public_spawn = false;
};
struct FixedPlan {
    const Information* information = nullptr;
    Menu menu{};
    Job first{};
    std::array<OwnTarget, group_limit> targets{}; // Input group indices.
    Point escape_endpoint{};
    bool escape_endpoint_supported = false;
};
struct ActorState {
    int id = -1, group = -1, occupies_group = -1, ready_round = 0, channel_remaining = 0, altar = -1;
    int departure_round = 0;
    Mode mode = Mode::inactive;
    Point position{};
    double health = 0;
    unsigned bag = 0;
    std::uint64_t initial_bag = 0, acquired_gems = 0, secured_gems = 0, lost_gems = 0, earned_gold = 0;
    bool supported = false, fallback = false, target_captured = false;
    Participation participation{};
};
struct GroupState {
    double health = 0, initial_health = 0, generated_health = 0, work_applied = 0;
    double incoming_per_round = 0;
    std::uint64_t generated_gems = 0, generated_gold = 0, allocated_gems = 0, allocated_gold = 0;
    unsigned stock_gems = 0, stock_gold = 0, captures = 0;
    int capture_remaining = -1, respawn_at = -1, clear_round = -1, capture_round = -1;
    bool present = false, reward_available = false, awake = false;
};
enum class RecordKind { work, clear, capture_start, capture, bank_start, bank, damage, death, arrival, respawn, recall_arrival };
// Only [0, event_count()) is initialized. Keeping this trivial avoids eager
// zeroing of a large event buffer on every candidate forecast.
struct Record {
    Moment at;
    RecordKind kind;
    int actor, group, owner_slot; // Actor/group are input indices; -1 denotes a global actor.
    double amount, health_after;
    unsigned bag_after, occupants;
    std::uint64_t earned_gold_after;
};
struct Quote {
    bool known = false, supported = false;
    Moment at{0, Phase::owner_action};
    int elapsed = 0;
};
struct Accounting {
    bool health_conserved = true, group_stock_conserved = true, colored_bags_conserved = true;
    bool allocation_conserved = true;
    bool independent_resource_copies = false, duplicated_physical_capture = false;
    bool complete = true;
};

// A single candidate path. The caller supplies one explicit own target and only
// legally supported ally continuations. This class never chooses another ally
// target, pools bags/gold, reserves an altar, or credits an unfinished channel.
// Both arms execute the same cursor; only stock ownership/occupancy differs.
class PathState {
    Input input_{};
    std::array<ActorState, actor_limit> actors_{};
    std::array<GroupState, group_limit * actor_limit> stocks_{};
    std::array<int, actor_limit> order_{};
    Record records_[record_limit];
    int record_count_ = 0, now_ = 0, cutoff_ = 0, actor_cursor_ = 0;
    Phase phase_ = Phase::owner_start;
    bool coupled_ = true, valid_ = false, stopped_ = false, censored_ = false, initial_round_ = true;
    bool won_ = false, initial_terminal_ = false, ambiguous_initial_terminal_ = false;
    std::uint64_t secured_ = 0;
    FixedPlan fixed_{};
    int own_ = -1, macros_ = 0, continuation_ = 0, second_ = -1, own_altar_ = -1;
    bool fixed_enabled_ = false, fixed_failed_ = false, own_hold_macro_ = false;

    int slot(int actor, int group) const { return (coupled_ ? 0 : actor * group_limit) + group; }
    GroupState& stock(int actor, int group) { return stocks_[slot(actor, group)]; }
    const GroupState& stock(int actor, int group) const { return stocks_[slot(actor, group)]; }
    int distance(Point from, Point to) const {
        if (from == to) return 0;
        const int value = input_.travel.distance ? input_.travel.distance(from, to, input_.travel.context) : unreachable;
        return value >= 0 && value <= std::numeric_limits<int>::max() - now_ - 1 ? value : unreachable;
    }
    bool inside(int group) const { return group >= 0 && group < input_.group_count; }
    int local_group(int source_index) const {
        if (!fixed_.information || source_index < 0 || source_index >= fixed_.information->group_count) return -1;
        const int id = fixed_.information->groups[source_index].id;
        for (int g = 0; g < input_.group_count; ++g) if (input_.groups[g].id == id) return g;
        return -1;
    }
    int local_altar(int source_index) const {
        if (!fixed_.information || source_index < 0 || source_index >= fixed_.information->altar_count) return -1;
        const int id = fixed_.information->altars[source_index].id;
        for (int k = 0; k < input_.altar_count; ++k) if (input_.altars[k].id == id) return k;
        return -1;
    }
    void abort_own_macro() {
        fixed_failed_ = true;
        auto& s = actors_[own_]; s.supported = true; s.mode = Mode::own_wait; s.ready_round = now_ + 1;
        second_ = -1;
    }
    bool own_available(const Job& job) const {
        const int g = local_group(job.group);
        if (!inside(g)) return false;
        const auto& st = stock(own_, g);
        return st.present && (st.reward_available || st.respawn_at >= 0) &&
            !(actors_[own_].occupies_group == g && st.captures > 0) && !input_.groups[g].contested;
    }
    static bool fixed_available(const Job& job, const void* context) {
        return static_cast<const PathState*>(context)->own_available(job);
    }
    void begin_own_target(int g) {
        auto& s = actors_[own_];
        if (!inside(g) || !fixed_.targets[g].supported || !fixed_.targets[g].capture_position_is_public_spawn ||
            input_.groups[g].contested || input_.groups[g].unmodeled_occupant) { abort_own_macro(); return; }
        auto& st = stock(own_, g);
        if (!st.present || (!st.reward_available && st.respawn_at < 0)) { abort_own_macro(); return; }
        for (int a = 0; a < input_.actor_count; ++a) if (a != own_) {
            const auto& other = actors_[a];
            if (st.reward_available && live(a) && other.supported && other.group == g && !other.target_captured &&
                other.mode != Mode::done && other.mode != Mode::inactive &&
                input_.actors[a].capture_position_is_public_spawn &&
                input_.actors[a].capture_position == fixed_.targets[g].capture_position) { abort_own_macro(); return; }
        }
        auto& t = input_.actors[own_];
        t.group = s.group = g;
        t.attack_position = st.health > 0 ? fixed_.targets[g].attack_position : fixed_.targets[g].capture_position;
        t.capture_position = fixed_.targets[g].capture_position; t.capture_position_is_public_spawn = true;
        s.target_captured = false; s.supported = true;
        if (!st.reward_available) {
            // Waiting on the target spawn would itself block replenishment.
            if (s.occupies_group == g || s.position == t.capture_position) { abort_own_macro(); return; }
            s.mode = Mode::waiting_stock; s.ready_round = std::max(now_, st.respawn_at); return;
        }
        const int d = distance(s.position, t.attack_position);
        if (d < 0) { abort_own_macro(); return; }
        s.ready_round = now_ + d; s.mode = d ? Mode::approach_work : Mode::attack;
    }
    void start_own_job(const Job& job) {
        ++macros_; second_ = -1; own_altar_ = -1;
        own_hold_macro_ = job.kind == JobKind::hold;
        auto& s = actors_[own_]; s.supported = true;
        if (job.kind == JobKind::farm) {
            second_ = job.second_group < 0 ? -1 : local_group(job.second_group);
            own_altar_ = local_altar(job.altar);
            if (own_altar_ < 0 || (job.second_group >= 0 && second_ < 0)) { abort_own_macro(); return; }
            begin_own_target(local_group(job.group));
        } else if (job.kind == JobKind::bank) {
            own_altar_ = local_altar(job.altar);
            go_to_bank(own_);
        } else if (job.kind == JobKind::renew) {
            const auto& info = *fixed_.information;
            if (s.health >= input_.actors[own_].maximum_health) {
                s.mode = Mode::own_wait; s.ready_round = now_ + 1;
            } else if (s.position == info.own_spawn) s.mode = Mode::own_heal;
            else {
                const double loss = double(info.own.escape_incoming) * (double(info.own.escape_rounds) + 1.0);
                if (!info.own.recall_safe || !fixed_.escape_endpoint_supported || info.own.escape_rounds < 0 || info.own.escape_rounds > max_horizon ||
                    distance(s.position, fixed_.escape_endpoint) != info.own.escape_rounds ||
                    !std::isfinite(loss) || loss < 0 || s.health <= loss) {
                    abort_own_macro(); return;
                }
                s.health -= loss; s.mode = Mode::own_escape; s.ready_round = now_ + info.own.escape_rounds;
            }
        } else {
            // Preserve only the initially supplied legal channel/hold. A later
            // HOLD is the same one-round wait used by the common reference.
            const bool initial_protected = macros_ == 1 &&
                (s.mode == Mode::tribute || s.mode == Mode::recall || s.mode == Mode::hold);
            if (!initial_protected) { s.mode = Mode::own_wait; s.ready_round = now_ + 1; }
        }
    }
    void next_own_job() {
        const int index = next_fixed_job(fixed_.menu, now_, cutoff_, macros_, continuation_, fixed_available, this);
        if (index >= 0) start_own_job(fixed_.menu.jobs[index]);
    }
    void target_finished(int a) {
        if (fixed_enabled_ && a == own_ && own_hold_macro_) actors_[a].mode = Mode::done;
        else if (fixed_enabled_ && a == own_ && second_ >= 0) {
            const int next = second_; second_ = -1; begin_own_target(next);
        } else go_to_bank(a);
    }
    bool live(int a) const { return actors_[a].health > 0 && actors_[a].mode != Mode::dead; }
    void fail() { censored_ = stopped_ = true; }
    void record(RecordKind kind, int a, int g, double amount = 0, unsigned occupants = 0) {
        if (record_count_ == record_limit) { fail(); return; }
        records_[record_count_++] = {{now_, phase_}, kind, a, g,
            int(phase_) >= int(Phase::owner_start) ? actor_cursor_ : -1, amount,
            a >= 0 ? actors_[a].health : 0, a >= 0 ? actors_[a].bag : 0, occupants,
            a >= 0 ? actors_[a].earned_gold : 0};
    }
    void fallback(int a) {
        actors_[a].supported = false; actors_[a].fallback = true; actors_[a].mode = Mode::inactive;
    }
    bool add_bag(int a, unsigned amount) {
        if (amount > std::numeric_limits<unsigned>::max() - actors_[a].bag) { fail(); return false; }
        actors_[a].bag += amount; actors_[a].acquired_gems += amount; return true;
    }
    bool occupant(int a, int g, int copy_owner) const {
        return live(a) && actors_[a].supported && actors_[a].occupies_group == g && (coupled_ || a == copy_owner);
    }
    unsigned occupants(int g, int copy_owner, int& lowest) const {
        unsigned count = 0; lowest = -1;
        for (int a = 0; a < input_.actor_count; ++a) if (occupant(a, g, copy_owner)) {
            ++count;
            if (lowest < 0 || actors_[a].id < actors_[lowest].id) lowest = a;
        }
        return count;
    }
    void go_to_capture(int a, bool action_spent = false) {
        auto& s = actors_[a]; const auto& token = input_.actors[a];
        if (!token.capture_position_is_public_spawn) { fallback(a); return; }
        const int d = distance(s.position, token.capture_position);
        if (d < 0) { fallback(a); return; }
        s.ready_round = now_ + d + int(d > 0 && action_spent);
        s.departure_round = now_ + int(action_spent);
        s.mode = d == 0 ? Mode::hold : Mode::approach_capture;
        if (d == 0) s.occupies_group = s.group;
    }
    void go_to_bank(int a) {
        auto& s = actors_[a]; int best = -1, best_d = std::numeric_limits<int>::max();
        if (fixed_enabled_ && a == own_) {
            if (own_altar_ < 0 || own_altar_ >= input_.altar_count || !input_.altars[own_altar_].accessible ||
                input_.altars[own_altar_].hostile_lock) { abort_own_macro(); return; }
            best = own_altar_; best_d = distance(s.position, input_.altars[best].position);
            if (best_d < 0) { abort_own_macro(); return; }
        }
        for (int k = 0; k < input_.altar_count && !(fixed_enabled_ && a == own_); ++k) {
            const auto& altar = input_.altars[k];
            if (!altar.accessible || altar.hostile_lock) continue;
            const int d = distance(s.position, altar.position);
            if (d >= 0 && (d < best_d || (d == best_d && (best < 0 || altar.id < input_.altars[best].id)))) {
                best = k; best_d = d;
            }
        }
        if (best < 0) { fallback(a); return; }
        s.altar = best; s.ready_round = now_ + best_d; s.mode = Mode::approach_bank;
    }
    void capture_group(int g, int copy_owner, GroupState& st, unsigned count, int lowest) {
        const unsigned gems = st.stock_gems, gold = st.stock_gold;
        // Preflight bag arithmetic before mutating any colored allocation.
        for (int a = 0; a < input_.actor_count; ++a) if (occupant(a, g, copy_owner)) {
            const unsigned share = allocated_reward(gems, count, a == lowest);
            if (share > std::numeric_limits<unsigned>::max() - actors_[a].bag) { fail(); return; }
        }
        st.stock_gems = st.stock_gold = 0; st.allocated_gems += gems; st.allocated_gold += gold;
        st.reward_available = false; st.capture_remaining = -1; st.capture_round = now_; ++st.captures;
        const int respawn = std::max(0, input_.groups[g].respawn_rounds);
        st.respawn_at = respawn > cutoff_ - now_ ? cutoff_ + 1 : now_ + respawn;
        for (int a = 0; a < input_.actor_count; ++a) if (occupant(a, g, copy_owner)) {
            const unsigned share = allocated_reward(gems, count, a == lowest);
            if (!add_bag(a, share)) return;
            actors_[a].earned_gold += allocated_reward(gold, count, a == lowest);
            record(RecordKind::capture, a, g, share, count);
        }
        for (int a = 0; a < input_.actor_count; ++a) {
            auto& actor = actors_[a];
            if (!actor.supported || actor.group != g || (!coupled_ && a != copy_owner)) continue;
            actor.target_captured = true;
            // Nonoccupants get no share, but their pre-existing colored bag is
            // not discarded. A supported attack/hold continuation still ends
            // at its own bank. In-flight actors first finish their quoted leg.
            if (actor.mode == Mode::attack || actor.mode == Mode::hold) target_finished(a);
            // Existing channels retain their timer; completed channels do not
            // invent a second bank or an ally's next target.
        }
    }
    void global_respawn() {
        const int copies = coupled_ ? 1 : input_.actor_count;
        for (int c = 0; c < copies; ++c) for (int g = 0; g < input_.group_count; ++g) {
            auto& st = stock(c, g); const auto& source = input_.groups[g];
            if (!st.present || st.respawn_at < 0 || now_ < st.respawn_at) continue;
            int lowest = -1;
            if (source.unmodeled_occupant || occupants(g, c, lowest)) continue;
            st.health = std::max(0.0, source.full_health); st.generated_health += st.health;
            st.incoming_per_round = source.full_incoming_per_round >= 0 ? source.full_incoming_per_round : source.incoming_per_round;
            st.stock_gems = source.gems; st.stock_gold = source.gold;
            st.generated_gems += source.gems; st.generated_gold += source.gold;
            st.reward_available = true; st.awake = false; st.respawn_at = -1;
            record(RecordKind::respawn, -1, g);
        }
    }
    void global_capture() {
        const int copies = coupled_ ? 1 : input_.actor_count;
        for (int c = 0; c < copies && !stopped_; ++c) for (int g = 0; g < input_.group_count && !stopped_; ++g) {
            auto& st = stock(c, g); const auto& source = input_.groups[g];
            if (!st.present || st.health > 0 || !st.reward_available) continue;
            int lowest = -1; const unsigned count = occupants(g, c, lowest);
            if (count == 0 || source.contested || source.unmodeled_occupant) { st.capture_remaining = -1; continue; }
            if (st.capture_remaining < 0) {
                st.capture_remaining = std::max(0, input_.settings.capture_rounds);
                record(RecordKind::capture_start, lowest, g, st.capture_remaining, count);
            } else if (st.capture_remaining > 0) --st.capture_remaining;
            if (st.capture_remaining == 0) capture_group(g, c, st, count, lowest);
        }
    }
    void damage_actor(int a, int g, double amount) {
        auto& s = actors_[a]; const double damage = std::min(s.health, std::max(0.0, amount));
        s.health -= damage; record(RecordKind::damage, a, g, damage);
        if (s.health <= 0) {
            s.lost_gems += s.bag; s.bag = 0; s.mode = Mode::dead; s.occupies_group = -1;
            record(RecordKind::death, a, g);
        } else if (s.mode == Mode::recall && damage > 0) {
            // The observed continuation was interrupted. Do not invent a retry.
            fallback(a);
        }
    }
    void global_camp_action() {
        const int copies = coupled_ ? 1 : input_.actor_count;
        for (int c = 0; c < copies && !stopped_; ++c) for (int g = 0; g < input_.group_count && !stopped_; ++g) {
            const auto& st = stock(c, g); const auto& source = input_.groups[g];
            if (!st.present || !st.awake || st.health <= 0 || st.incoming_per_round <= 0) continue;
            int target = -1, closest = std::numeric_limits<int>::max();
            for (int a = 0; a < input_.actor_count; ++a) {
                const auto& s = actors_[a];
                if (!live(a) || (!coupled_ && a != c) || s.group != g ||
                    s.mode == Mode::approach_work || s.mode == Mode::approach_capture || s.mode == Mode::approach_bank ||
                    s.mode == Mode::own_escape) continue;
                const int d = std::max(std::abs(s.position.x - source.position.x), std::abs(s.position.y - source.position.y));
                if (d <= source.attack_range && (d < closest || (d == closest && (target < 0 || s.id < actors_[target].id)))) {
                    target = a; closest = d;
                }
            }
            if (target >= 0) damage_actor(target, g, st.incoming_per_round);
        }
    }
    void owner_start(int a) {
        auto& s = actors_[a]; if (!live(a) || !s.supported) return;
        if (s.mode == Mode::tribute && s.channel_remaining == 0) {
            const unsigned amount = s.bag;
            s.bag = 0; s.secured_gems += amount; secured_ += amount; s.mode = Mode::done; s.occupies_group = -1;
            record(RecordKind::bank, a, s.group, amount);
            if (std::uint64_t(input_.our_score) + secured_ >= input_.settings.winning_gems) won_ = stopped_ = true;
        } else if (s.mode == Mode::recall && s.channel_remaining == 0) {
            s.position = input_.actors[a].recall_destination; s.occupies_group = -1;
            s.mode = fixed_enabled_ && a == own_ ? Mode::own_heal : Mode::done;
            record(RecordKind::recall_arrival, a, s.group);
        }
    }
    void owner_action(int a) {
        auto& s = actors_[a]; if (!live(a) || !s.supported) return;
        if (s.mode == Mode::approach_capture && now_ >= s.departure_round &&
            s.position != input_.actors[a].capture_position) s.occupies_group = -1;
        if (fixed_enabled_ && a == own_) {
            if (s.mode == Mode::own_wait && now_ >= s.ready_round) s.mode = Mode::done;
            if (s.mode == Mode::done) next_own_job();
            if (s.mode == Mode::waiting_stock && now_ >= s.ready_round) begin_own_target(s.group);
            if (s.mode == Mode::own_escape && now_ >= s.ready_round) {
                s.occupies_group = -1;
                s.position = fixed_.escape_endpoint;
                input_.actors[a].recall_destination = fixed_.information->own_spawn;
                s.channel_remaining = std::max(1, input_.settings.recall_rounds); s.mode = Mode::recall;
            }
        }
        if (s.mode == Mode::approach_work && now_ >= s.ready_round) {
            s.position = input_.actors[a].attack_position; s.mode = Mode::attack;
            record(RecordKind::arrival, a, s.group);
        }
        if (s.mode == Mode::attack) {
            auto& st = stock(a, s.group);
            if (!st.present) { fallback(a); return; }
            if (s.target_captured) target_finished(a);
            else if (!st.reward_available) { s.mode = Mode::done; return; }
            else if (st.health > 0) {
                const double work = std::min(st.health, input_.actors[a].work_per_action);
                st.health -= work; st.work_applied += work; st.awake = st.awake || work > 0;
                record(RecordKind::work, a, s.group, work);
                if (st.health <= 0) {
                    st.clear_round = now_; record(RecordKind::clear, a, s.group);
                    // Every already participating attacker now moves toward its
                    // own supplied public spawn tile, on its next legal action.
                    for (int other = 0; other < input_.actor_count; ++other)
                        if (actors_[other].supported && actors_[other].group == s.group &&
                            actors_[other].mode == Mode::attack && (coupled_ || other == a)) {
                            bool spent = other == a;
                            for (int k = 0; k < actor_cursor_; ++k) spent |= order_[k] == other;
                            go_to_capture(other, spent);
                        }
                }
            } else go_to_capture(a);
        }
        if (s.mode == Mode::approach_capture && now_ >= s.ready_round) {
            s.position = input_.actors[a].capture_position;
            record(RecordKind::arrival, a, s.group);
            if (s.target_captured) target_finished(a);
            else { s.occupies_group = s.group; s.mode = Mode::hold; }
        }
        if (s.mode == Mode::approach_bank) {
            // Departure happens on the owner's action, after this round's
            // global respawn/capture/camp phases.
            if (s.position != input_.altars[s.altar].position) s.occupies_group = -1;
            if (now_ >= s.ready_round) {
                s.position = input_.altars[s.altar].position;
                record(RecordKind::arrival, a, s.group);
                if (s.bag == 0) { s.mode = Mode::done; return; }
                s.channel_remaining = tribute_ticks(s.bag, input_.settings);
                s.mode = Mode::tribute; record(RecordKind::bank_start, a, s.group, s.channel_remaining);
            }
        }
        // An old spawn remains occupied through global phases; departure is an
        // owner action, including a departure toward the second farm target.
        if ((s.mode == Mode::approach_work || s.mode == Mode::attack) &&
            s.occupies_group >= 0 && s.occupies_group != s.group) s.occupies_group = -1;
        if (s.mode == Mode::own_escape) s.occupies_group = -1;
    }
    void owner_end(int a) {
        auto& s = actors_[a]; if (!live(a) || !s.supported) return;
        if ((s.mode == Mode::tribute || s.mode == Mode::recall) && s.channel_remaining > 0) --s.channel_remaining;
        if (fixed_enabled_ && a == own_ && s.mode == Mode::own_heal) {
            const double recovery = input_.settings.spawn_heal_fraction * input_.actors[a].maximum_health;
            if (!std::isfinite(recovery) || recovery <= 0) { abort_own_macro(); return; }
            s.health = std::min(input_.actors[a].maximum_health, s.health + recovery);
            if (s.health >= input_.actors[a].maximum_health) s.mode = Mode::done;
        }
        if (s.mode == Mode::done && input_.actors[a].intent == Intent::recall) {
            const double recovery = input_.settings.spawn_heal_fraction * input_.actors[a].maximum_health;
            s.health = std::min(input_.actors[a].maximum_health, s.health + std::max(0.0, recovery));
        }
    }
    void next_actor() {
        ++actor_cursor_;
        if (actor_cursor_ == input_.actor_count) {
            actor_cursor_ = 0; ++now_; initial_round_ = false; phase_ = Phase::round_respawn;
            if (now_ > cutoff_) stopped_ = true;
        } else phase_ = Phase::owner_start;
    }
public:
    // User-provided: value-initializing PathState must not zero records_.
    PathState() noexcept {}
    PathState(const PathState& other) { *this = other; }
    PathState& operator=(const PathState& other) {
        if (this == &other) return *this;
        input_ = other.input_; actors_ = other.actors_; stocks_ = other.stocks_; order_ = other.order_;
        record_count_ = other.record_count_; now_ = other.now_; cutoff_ = other.cutoff_; actor_cursor_ = other.actor_cursor_;
        phase_ = other.phase_; coupled_ = other.coupled_; valid_ = other.valid_; stopped_ = other.stopped_;
        censored_ = other.censored_; initial_round_ = other.initial_round_; won_ = other.won_;
        initial_terminal_ = other.initial_terminal_; ambiguous_initial_terminal_ = other.ambiguous_initial_terminal_;
        secured_ = other.secured_;
        fixed_ = other.fixed_; own_ = other.own_; macros_ = other.macros_; continuation_ = other.continuation_;
        second_ = other.second_; own_altar_ = other.own_altar_;
        fixed_enabled_ = other.fixed_enabled_; fixed_failed_ = other.fixed_failed_;
        own_hold_macro_ = other.own_hold_macro_;
        for (int k = 0; k < record_count_; ++k) records_[k] = other.records_[k];
        return *this;
    }
    bool initialize(const Input& input, bool coupling_enabled = true) {
        input_ = input; actors_ = {}; stocks_ = {}; record_count_ = 0; secured_ = 0;
        now_ = input.round; actor_cursor_ = 0; phase_ = Phase::owner_start;
        coupled_ = coupling_enabled; valid_ = false; stopped_ = censored_ = won_ = false; initial_round_ = true;
        initial_terminal_ = ambiguous_initial_terminal_ = false;
        fixed_enabled_ = fixed_failed_ = own_hold_macro_ = false; own_ = -1; macros_ = continuation_ = 0;
        second_ = own_altar_ = -1;
        if (input.actor_count < 1 || input.actor_count > actor_limit || input.group_count < 0 || input.group_count > group_limit ||
            input.altar_count < 0 || input.altar_count > max_altars || input.round < 0 ||
            input.round > std::numeric_limits<int>::max() - max_horizon - 1 || input.settings.max_rounds <= 0 ||
            input.settings.capture_rounds < 0 || input.settings.winning_gems == 0 ||
            !std::isfinite(input.settings.tribute_multiplier) || input.settings.tribute_multiplier < 0) return false;
        // Match M0's inclusive completion deadline. At the round-limit boundary
        // the final winner still needs the external gold/kills/tie comparison.
        cutoff_ = std::min(input.round + std::clamp(input.horizon, 0, max_horizon), input.settings.max_rounds);
        int own_count = 0;
        for (int a = 0; a < input.actor_count; ++a) {
            const auto& t = input.actors[a];
            if (t.id < 0 || !std::isfinite(t.health) || !std::isfinite(t.maximum_health) || !std::isfinite(t.work_per_action) ||
                t.health < 0 || t.maximum_health < t.health || t.work_per_action < 0 || t.channel_remaining < 0) return false;
            own_count += t.own;
            if (t.own) own_ = a;
            for (int b = 0; b < a; ++b) {
                const auto& prior = input.actors[b];
                if (prior.id == t.id || prior.order == t.order ||
                    (t.alive && prior.alive && t.health > 0 && prior.health > 0 && t.position == prior.position)) return false;
                const auto plans_occupancy = [](const ActorToken& token) {
                    return token.intent == Intent::attack || token.intent == Intent::hold || token.on_target_spawn;
                };
                // A legal tile alone does not permit two champions to occupy
                // it together. Ask the wrapper for distinct supplied spawn
                // targets, or let it fall back to M0; never invent another tile.
                if (t.group >= 0 && t.group == prior.group && plans_occupancy(t) && plans_occupancy(prior) &&
                    t.capture_position_is_public_spawn && prior.capture_position_is_public_spawn &&
                    t.capture_position == prior.capture_position) return false;
            }
            order_[a] = a;
        }
        if (own_count != 1) return false;
        std::sort(order_.begin(), order_.begin() + input.actor_count,
            [&](int a, int b) { return input.actors[a].order < input.actors[b].order; });
        for (int g = 0; g < input.group_count; ++g) {
            const auto& source = input.groups[g];
            if (source.id < 0 || !std::isfinite(source.health) || !std::isfinite(source.full_health) ||
                !std::isfinite(source.incoming_per_round) || source.health < 0 || source.full_health < source.health ||
                source.incoming_per_round < 0 || !std::isfinite(source.full_incoming_per_round) ||
                source.full_incoming_per_round < -1 || source.capture_remaining < -1 || source.respawn_remaining < -1 ||
                source.respawn_remaining > std::numeric_limits<int>::max() - now_) return false;
            for (int h = 0; h < g; ++h) if (source.id == input.groups[h].id) return false;
            for (int c = 0; c < (coupled_ ? 1 : input.actor_count); ++c) {
                auto& st = stock(c, g); st.present = source.health_known;
                st.health = st.initial_health = source.health;
                st.incoming_per_round = source.incoming_per_round;
                st.reward_available = source.health_known && source.reward_available;
                if (st.reward_available) {
                    st.stock_gems = source.gems; st.stock_gold = source.gold;
                    st.generated_gems = source.gems; st.generated_gold = source.gold;
                }
                st.awake = source.awake; st.capture_remaining = source.capture_remaining;
                if (source.health_known && !source.reward_available && source.respawn_remaining >= 0)
                    st.respawn_at = now_ + source.respawn_remaining;
                if (st.present && st.health == 0) st.clear_round = now_;
            }
        }
        for (int a = 0; a < input.actor_count; ++a) {
            const auto& t = input.actors[a]; auto& s = actors_[a];
            s.id = t.id; s.position = t.position; s.group = t.group; s.health = t.health;
            s.bag = t.bag; s.initial_bag = t.bag;
            if (!t.alive || t.health == 0) { s.mode = Mode::dead; s.lost_gems = s.bag; s.bag = 0; continue; }
            const bool observed = t.own || (t.visible && t.observation.observed_round == now_ &&
                t.observation.source == Source::personal_vision && t.observation.status == Status::exact_now);
            if (!observed) { fallback(a); continue; }
            if (t.on_target_spawn && inside(t.group) && t.capture_position_is_public_spawn && t.position == t.capture_position)
                s.occupies_group = t.group;
            if (t.intent == Intent::none) continue;
            if (t.intent == Intent::tribute || t.intent == Intent::recall) {
                if (!t.own && (!t.channel_type_known || !t.channel_remaining_known)) { fallback(a); continue; }
                s.supported = true; s.channel_remaining = t.channel_remaining;
                s.mode = t.intent == Intent::tribute ? Mode::tribute : Mode::recall; continue;
            }
            if (t.intent == Intent::bank) {
                if (!t.own) { fallback(a); continue; }
                s.supported = true; go_to_bank(a); continue;
            }
            if (!inside(t.group) || !stock(a, t.group).present || !stock(a, t.group).reward_available) { fallback(a); continue; }
            if (t.intent == Intent::hold) {
                if (!t.on_target_spawn || s.occupies_group != t.group || stock(a, t.group).health != 0) { fallback(a); continue; }
                s.supported = true; s.mode = Mode::hold; continue;
            }
            if (!t.own) {
                s.participation = infer_participation(t.participation);
                if (!s.participation.supported || t.participation.actor_id != t.id ||
                    t.participation.group_id != input.groups[t.group].id ||
                    t.participation.current.observed_round != now_ ||
                    t.participation.current_health != input.groups[t.group].health || t.position != t.attack_position) {
                    fallback(a); continue;
                }
            }
            if (t.work_per_action <= 0) { fallback(a); continue; }
            const int d = distance(t.position, t.attack_position);
            if (d < 0 || (!t.own && d != 0)) { fallback(a); continue; }
            s.supported = true; s.ready_round = now_ + d; s.mode = d == 0 ? Mode::attack : Mode::approach_work;
        }
        valid_ = true;
        const bool ours = input.our_score >= input.settings.winning_gems, theirs = input.enemy_score >= input.settings.winning_gems;
        initial_terminal_ = ours || theirs; ambiguous_initial_terminal_ = ours && theirs;
        won_ = ours && !theirs;
        if (initial_terminal_ || now_ > cutoff_) stopped_ = true;
        return true;
    }
    bool initialize_fixed(const Input& input, const FixedPlan& plan, bool coupling_enabled = true) {
        if (!plan.information || !initialize(input, coupling_enabled)) return false;
        const auto& info = *plan.information;
        if (info.public_layout_truncated || info.round != input.round || info.group_count < 0 || info.group_count > max_groups ||
            info.altar_count < 0 || info.altar_count > max_altars || input.actors[own_].id != info.own.id ||
            input.actors[own_].health != info.own.health || input.actors[own_].maximum_health != info.own.maximum_health ||
            input.actors[own_].bag != info.own.bag || input.actors[own_].position != info.own.position ||
            input.our_score != info.our_score || input.enemy_score != info.enemy_score || cutoff_ != info.round + horizon(info) ||
            input.settings.max_rounds != info.settings.max_rounds || input.settings.winning_gems != info.settings.winning_gems ||
            input.settings.capture_rounds != info.settings.capture_rounds || input.settings.recall_rounds != info.settings.recall_rounds ||
            input.settings.tribute_multiplier != info.settings.tribute_multiplier || input.settings.tribute_base != info.settings.tribute_base ||
            input.settings.tribute_minimum != info.settings.tribute_minimum || input.settings.spawn_heal_fraction != info.settings.spawn_heal_fraction ||
            plan.menu.count < 1 || plan.menu.count > max_jobs || plan.menu.single_count < 0 || plan.menu.single_count > 8 ||
            plan.menu.single_count > plan.menu.count) return false;
        fixed_ = plan;
        for (int g = 0; g < input.group_count; ++g) if (input.groups[g].full_incoming_per_round < 0) return false;
        for (int g = 0; g < input.group_count; ++g) {
            const auto& local = input.groups[g];
            const Group* observed = nullptr;
            for (int k = 0; k < info.group_count; ++k) if (info.groups[k].id == input.groups[g].id) observed = &info.groups[k];
            if (!observed || local.full_health != observed->full_health || local.gems != observed->gems ||
                local.gold != observed->gold || local.respawn_rounds != observed->respawn_rounds ||
                local.full_incoming_per_round != observed->full_incoming_damage) return false;
            if (!local.health_known) continue;
            const bool stock = observed->lifecycle == Lifecycle::living ||
                (observed->lifecycle == Lifecycle::cleared && observed->reward_available);
            unsigned modeled_occupants = 0;
            for (int a = 0; a < input.actor_count; ++a) {
                const auto& token = input.actors[a];
                modeled_occupants += token.group == g && token.on_target_spawn && token.capture_position_is_public_spawn &&
                    token.position == token.capture_position && token.alive && token.health > 0 &&
                    (token.intent == Intent::hold || token.intent == Intent::attack);
            }
            if (!observed->visible || observed->provenance.observed_round != info.round || observed->provenance.source != Source::personal_vision ||
                observed->provenance.status != Status::exact_now || observed->lifecycle == Lifecycle::unknown) return false;
            if (local.health != observed->health || local.incoming_per_round != observed->incoming_damage ||
                local.reward_available != stock || local.contested != observed->contested || !observed->occupancy_known ||
                (modeled_occupants && !observed->occupied_spawn) ||
                local.unmodeled_occupant != (observed->occupied_spawn &&
                    (!modeled_occupants || observed->eligible_allied_occupants > modeled_occupants)) ||
                (local.respawn_remaining >= 0 && local.respawn_remaining != observed->respawn_remaining)) return false;
            if (local.capture_remaining >= 0 && (observed->capture_started_round < 0 ||
                observed->position != info.own.position ||
                local.capture_remaining != std::max(0, observed->capture_started_round + info.settings.capture_rounds - info.round))) return false;
            if ((observed->lifecycle == Lifecycle::living || observed->lifecycle == Lifecycle::cleared) &&
                (input.groups[g].health_provenance.observed_round != info.round ||
                 input.groups[g].health_provenance.source != Source::personal_vision ||
                 input.groups[g].health_provenance.status != Status::exact_now ||
                 observed->health_provenance.observed_round != info.round ||
                 observed->health_provenance.source != Source::personal_vision ||
                 observed->health_provenance.status != Status::exact_now)) return false;
        }
        if (input.altar_count != info.altar_count) return false;
        for (int a = 0; a < info.altar_count; ++a) {
            const int k = local_altar(a);
            if (k < 0 || input.altars[k].position != info.altars[a].position ||
                input.altars[k].altar_position != info.altars[a].altar_position ||
                input.altars[k].accessible != info.altars[a].accessible ||
                input.altars[k].hostile_lock != info.altars[a].hostile_lock) return false;
        }
        bool admitted = false;
        for (int j = 0; j < plan.menu.count; ++j) {
            const auto& job = plan.menu.jobs[j];
            if (unsigned(job.kind) > unsigned(JobKind::hold)) return false;
            admitted |= job.kind == plan.first.kind && job.group == plan.first.group &&
                job.second_group == plan.first.second_group && job.altar == plan.first.altar;
            if (job.kind == JobKind::farm) {
                if (local_group(job.group) < 0 || (job.second_group >= 0 && local_group(job.second_group) < 0) || local_altar(job.altar) < 0)
                    return false;
                if (j < plan.menu.single_count && job.second_group >= 0) return false;
                for (int target : {job.group, job.second_group}) if (target >= 0) {
                    bool in_singles = false;
                    for (int k = 0; k < plan.menu.single_count; ++k) in_singles |= plan.menu.jobs[k].group == target;
                    if (!in_singles) return false;
                }
            } else if (j < plan.menu.single_count || (job.kind == JobKind::bank && local_altar(job.altar) < 0)) return false;
        }
        if (!admitted) return false;
        fixed_enabled_ = true;
        if (!stopped_) start_own_job(plan.first);
        return true;
    }
    // One bounded phase step. A denied work budget pauses with the cursor
    // unchanged; callers can resume the same path rather than restarting it.
    bool advance_event() {
        if (!valid_ || stopped_) return false;
        if (input_.work_budget.can_refine && !input_.work_budget.can_refine(input_.work_budget.context)) return false;
        if (phase_ == Phase::round_respawn) { global_respawn(); phase_ = Phase::capture; }
        else if (phase_ == Phase::capture) { global_capture(); phase_ = Phase::camp_action; }
        else if (phase_ == Phase::camp_action) { global_camp_action(); phase_ = Phase::owner_start; }
        else {
            const int a = order_[actor_cursor_]; const auto& token = input_.actors[a];
            if (initial_round_ && !token.initial_turn_pending) { next_actor(); return true; }
            if (phase_ == Phase::owner_start) {
                if (!initial_round_ || token.initial_owner_start_pending) owner_start(a);
                phase_ = Phase::owner_action;
            } else if (phase_ == Phase::owner_action) { owner_action(a); phase_ = Phase::owner_end; }
            else { owner_end(a); next_actor(); }
        }
        return true;
    }
    void advance_to(int through_round) { while (!stopped_ && now_ <= through_round && advance_event()) {} }
    void run_to_horizon() { advance_to(cutoff_); }
    Quote clear_quote(int actor, int group) const {
        Quote q;
        if (actor < 0 || actor >= input_.actor_count || !inside(group)) return q;
        q.supported = actors_[actor].supported && actors_[actor].group == group;
        const int round = stock(actor, group).clear_round;
        if (q.supported && round >= 0) { q.known = true; q.at = {round, Phase::owner_action}; q.elapsed = round - input_.round; }
        return q;
    }
    Quote capture_quote(int actor, int group) const {
        Quote q;
        if (actor < 0 || actor >= input_.actor_count || !inside(group)) return q;
        q.supported = actors_[actor].supported && actors_[actor].group == group;
        const int round = stock(actor, group).capture_round;
        if (q.supported && round >= 0) { q.known = true; q.at = {round, Phase::capture}; q.elapsed = round - input_.round; }
        return q;
    }
    Quote advance_until_clear(int actor, int group) {
        while (!clear_quote(actor, group).known && advance_event()) {}
        return clear_quote(actor, group);
    }
    Quote advance_until_capture(int actor, int group) {
        while (!capture_quote(actor, group).known && advance_event()) {}
        return capture_quote(actor, group);
    }
    const ActorState& actor(int index) const { return actors_[index]; }
    const GroupState& group(int index, int actor_index = 0) const { return stock(actor_index, index); }
    const Record& event(int index) const { return records_[index]; }
    int event_count() const { return record_count_; }
    int own_macros_started() const { return macros_; }
    bool fixed_premise_failed() const { return fixed_failed_; }
    int round() const { return now_; }
    int cutoff_round() const { return cutoff_; }
    bool valid() const { return valid_; }
    bool stopped() const { return stopped_; }
    bool censored() const { return censored_; }
    bool won() const { return won_; }
    bool initial_terminal() const { return initial_terminal_; }
    bool ambiguous_initial_terminal() const { return ambiguous_initial_terminal_; }
    bool terminal_comparison_unavailable() const {
        return stopped_ && !won_ && !initial_terminal_ && cutoff_ == input_.settings.max_rounds;
    }
    std::uint64_t secured_output() const { return secured_; }
    Accounting accounting() const {
        Accounting r; r.independent_resource_copies = !coupled_; r.complete = valid_ && !censored_;
        if (!valid_) return r;
        std::uint64_t received_gems = 0, received_gold = 0, banked = 0, issued_gems = 0, issued_gold = 0;
        for (int a = 0; a < input_.actor_count; ++a) {
            const auto& s = actors_[a];
            if (s.initial_bag + s.acquired_gems != std::uint64_t(s.bag) + s.secured_gems + s.lost_gems) r.colored_bags_conserved = false;
            received_gems += s.acquired_gems; received_gold += s.earned_gold; banked += s.secured_gems;
        }
        for (int g = 0; g < input_.group_count; ++g) {
            unsigned captured_copies = 0;
            for (int c = 0; c < (coupled_ ? 1 : input_.actor_count); ++c) {
                const auto& st = stock(c, g); if (!st.present) continue;
                if (std::abs(st.initial_health + st.generated_health - st.health - st.work_applied) > 1e-7) r.health_conserved = false;
                if (st.generated_gems != st.allocated_gems + st.stock_gems || st.generated_gold != st.allocated_gold + st.stock_gold)
                    r.group_stock_conserved = false;
                issued_gems += st.allocated_gems; issued_gold += st.allocated_gold;
                captured_copies += st.captures > 0;
            }
            if (!coupled_ && captured_copies > 1) r.duplicated_physical_capture = true;
        }
        r.allocation_conserved = received_gems == issued_gems && received_gold == issued_gold && banked == secured_;
        return r;
    }
};

// Project one chronological path into the compact common ranking type. The
// path retains all actor-colored receipts; team output is never appended after
// a separately completed own rollout.
inline bool predict_fixed_into(const Input& input, const FixedPlan& plan, bool coupling_enabled,
                               PathState& path, Forecast& out) {
    out.reset();
    if (!path.initialize_fixed(input, plan, coupling_enabled)) {
        out.stock_unknown = true; out.abort_reason = AbortReason::unknown_stock; return false;
    }
    path.run_to_horizon();
    const auto& info = *plan.information;
    const int deadline = std::min(input.settings.max_rounds, input.round + std::clamp(input.horizon, 0, max_horizon));
    out.first_conversion_round = deadline + 1;
    out.final_round = std::min(path.round(), deadline);
    out.macros_started = path.own_macros_started();
    out.truncated = info.observation_truncated || info.public_layout_truncated || plan.menu.truncated;
    out.budget_exhausted = !path.stopped() && path.round() <= deadline;
    out.truncated |= out.budget_exhausted;
    int own = -1;
    for (int a = 0; a < input.actor_count; ++a) if (input.actors[a].own) own = a;
    const auto& actor = path.actor(own);
    if (path.secured_output() > std::numeric_limits<unsigned>::max() ||
        actor.earned_gold > std::numeric_limits<unsigned>::max() - info.own.gold) return false;
    out.our_secured = unsigned(path.secured_output()); out.final_bag = actor.bag;
    out.final_gold = info.own.gold + unsigned(actor.earned_gold); out.final_health = float(actor.health);
    out.terminal_z = path.won() ? 1 : input.enemy_score >= input.settings.winning_gems &&
        input.our_score < input.settings.winning_gems ? -1 : 0;
    out.terminal_unknown = path.ambiguous_initial_terminal() || path.terminal_comparison_unavailable();
    for (int n = 0; n < path.event_count(); ++n) {
        const auto& record = path.event(n);
        if (record.kind == RecordKind::bank) {
            out.completed_conversion = true;
            out.first_conversion_round = std::min(out.first_conversion_round, record.at.round);
        }
        if (record.actor != own || (record.kind != RecordKind::capture && record.kind != RecordKind::bank)) continue;
        if (out.event_count == max_events || record.earned_gold_after > std::numeric_limits<unsigned>::max() - info.own.gold) {
            out.truncated = true; return false;
        }
        Event event; event.kind = record.kind == RecordKind::capture ? EventKind::capture : EventKind::bank;
        event.start = event.end = record.at;
        event.group_id = record.group < 0 ? -1 : input.groups[record.group].id;
        event.bag_after = record.bag_after;
        event.bag_before = record.kind == RecordKind::bank ? unsigned(record.amount) : record.bag_after - unsigned(record.amount);
        event.secured = record.kind == RecordKind::bank ? unsigned(record.amount) : 0;
        event.health_after = float(record.health_after); event.status = Status::predicted;
        event.gold_after = info.own.gold + unsigned(record.earned_gold_after);
        out.events[out.event_count++] = event;
    }
    out.objective = {1, out.terminal_z, out.our_secured, 0, std::uint64_t(out.first_conversion_round)};
    // Complete the entire identical menu before common choose(). If any job
    // is unsupported, fall back for the decision, never only for one candidate
    // or by privately deleting a difficult menu row.
    return !path.censored() && !path.fixed_premise_failed() && !out.budget_exhausted;
}

} // namespace plan4::foundry
