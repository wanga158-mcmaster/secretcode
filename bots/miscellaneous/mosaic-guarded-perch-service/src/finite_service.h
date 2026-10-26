#pragma once
// A conditional MODEL prefix, not an action certificate or a complete service.
// All borrowed input spans must remain immutable for this builder's lifetime.
// No SDK, allocation, observation, command, or runtime fuel claim is made here.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace mosaic::finite_service {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "The model requires IEEE754 binary32 float operations.");
using Tile = std::uint32_t;
constexpr Tile no_tile = std::numeric_limits<Tile>::max();
constexpr std::size_t no_index = std::numeric_limits<std::size_t>::max();
constexpr std::size_t max_cells = 16384, max_camps = 512;
enum class Status { Idle, Working, PrefixReady, Unknown, Unsupported };
enum class Reason {
    None, InvalidBounds, WorkspaceCapacity, UnsupportedClass, InitialBag,
    StationNotFrontier, NoRoute, HealthBound, Horizon, NoFloatProgress,
    RemovalLimit, CaptureRecallBankRenewalNotPorted
};
enum class Phase {
    Init, Static, Camps, Members, Frontier, Field, Search, Trace, Ingress,
    HarmfulRemoval, Finished,
    // Extension seam only: no current transition constructs these stages.
    SatelliteApproach, SatelliteRemoval, CaptureApproach, CaptureHold,
    LoadedQuietRecall, RecallArrivalObserved, BankApproach, Tribute,
    ReturnSpawn, RenewalObserved
};
enum class Op { Move, Basic };
enum Guard : std::uint32_t {
    ImmutableProblem = 1u << 0, FreshBodyAndElapsedClock = 1u << 1,
    SoundAllPotentialCampBounds = 1u << 2, NoUnmodeledInterference = 1u << 3,
    FreshMoveLegal = 1u << 4, FreshBasicLegalAndReady = 1u << 5,
    ActualTargetVisible = 1u << 6, FreshDeathBeforeThreatReduction = 1u << 7
};
constexpr std::uint32_t common_guards = ImmutableProblem | FreshBodyAndElapsedClock |
    SoundAllPotentialCampBounds | NoUnmodeledInterference;
struct Camp {
    int native_id{-1}; Tile tile{}; float hp{}, shield{};
    // Keep the supplied raw bound in double until ceil; narrowing first could
    // turn e.g.20.0000001 into20 and understate the integer energy bound.
    double strength{}; int range{};
};
struct Body { float health{}, max_health{}, strength{}; int range{}; bool single_target_basic{}; };
struct Identity { std::uint64_t problem{}, episode{}; };
struct Problem {
    Identity identity{}; std::uint32_t width{}, height{}; Tile start{}, station{};
    std::span<const Tile> static_blocked{}, opaque{};
    std::span<const Camp> camps{};
    int group_native_id{-1}; std::span<const int> member_native_ids{};
    std::uint32_t group_gems{}, initial_bag{}, remaining_inputs{}, max_basic_inputs{10000};
    Body body{};
};
struct CampResidual { int native_id{-1}; float hp{}, shield{}; bool model_dead{}; };
struct ExpectedState {
    Tile tile{}; std::uint32_t health_lower{}, bag{}, elapsed_inputs{}, selected_model_dead{};
    float target_hp{}, target_shield{};
};
struct ModelStep {
    Op op{}; Phase phase{}; ExpectedState before{}, after{};
    std::uint32_t pre_action_camp_loss{}, actual_guards{};
    int target_native_id{-1}, fresh_death_required_after{-1};
    // Every prefix input retains ALL potential camp threats/occupied tiles.
    // A predicted death is not observed death and never authorizes a reduction.
    bool all_potential_threats{true};
};
struct Node {
    std::uint64_t loss{std::numeric_limits<std::uint64_t>::max()};
    std::uint32_t moves{std::numeric_limits<std::uint32_t>::max()}, incoming{};
    Tile parent{no_tile}; std::size_t heap_position{no_index};
    bool blocked{}, opaque{}, settled{};
};
struct Workspace {
    std::span<Node> nodes{}; std::span<Tile> heap{}, route{};
    std::span<CampResidual> camps{}; std::span<ModelStep> steps{};
};
struct BasicEffect { float hp{}, shield{}; bool valid{}; };
inline BasicEffect basic_effect(float hp, float shield, float damage) {
    // Explicit binary32 operation boundaries match the frozen Python model.
    if (!std::isfinite(hp) || !std::isfinite(shield) || !std::isfinite(damage) ||
        hp < 0.0f || shield < 0.0f || damage < 0.0f) return {hp, shield, false};
    const float absorbed = std::min(shield, damage);
    volatile float remaining_damage = damage - absorbed;
    volatile float next_hp = hp - remaining_damage;
    volatile float next_shield = shield - absorbed;
    return {std::max(0.0f, static_cast<float>(next_hp)), std::max(0.0f, static_cast<float>(next_shield)), true};
}
struct Summary {
    Status status{}; Reason reason{}; Phase phase{}; Identity identity{};
    std::uint64_t work_units{}, nominal_loss{};
    std::uint32_t nominal_health{}, two_delay_reserve{}, reserved_health{};
    std::size_t inputs{}, moves{}, basics{}; int target_native_id{-1};
};

// One supplied station candidate, with eight-neighbor walking and lexicographic
// (origin camp loss, moves, tile) Dijkstra. advance(q) performs at most q units.
// A unit touches one grid/field cell, one route/word element, one neighbor, or
// one camp/member (bounded <2*max_camps comparisons); heap work is O(log(max_cells)).
// This abstract quota is not a bound on native WASM fuel or real elapsed time.
class CandidateBuilder {
public:
    bool begin(const Problem& problem, Workspace workspace) {
        if (bound_) return false; // Re-entering cannot erase unfinished work.
        bound_ = true; p_ = problem; w_ = workspace;
        const auto n = static_cast<std::uint64_t>(p_.width) * p_.height;
        if (!p_.width || !p_.height || n > max_cells || p_.start >= n || p_.station >= n ||
            p_.static_blocked.size() > n || p_.opaque.size() > n ||
            p_.camps.empty() || p_.camps.size() > max_camps || p_.member_native_ids.empty() ||
            p_.member_native_ids.size() > max_camps || !valid_body())
            return fail(Status::Unknown, Reason::InvalidBounds);
        n_ = static_cast<std::size_t>(n);
        if (w_.nodes.size() < n_ || w_.heap.size() < n_ || w_.route.size() < n_ ||
            w_.camps.size() < p_.camps.size() || w_.steps.empty())
            return fail(Status::Unknown, Reason::WorkspaceCapacity);
        if (!p_.body.single_target_basic || p_.group_gems == 0 || p_.group_native_id < 0 ||
            p_.body.range < 1 || p_.max_basic_inputs == 0 || p_.max_basic_inputs > 10000)
            return fail(Status::Unsupported, Reason::UnsupportedClass);
        if (p_.initial_bag != 0) return fail(Status::Unsupported, Reason::InitialBag);
        initial_health_ = static_cast<std::uint32_t>(std::floor(p_.body.health));
        current_ = {p_.start, initial_health_, p_.initial_bag, 0, 0, 0.0f, 0.0f};
        status_ = Status::Working; phase_ = Phase::Init; return true;
    }
    Status advance(std::size_t quota) {
        while (quota-- && status_ == Status::Working) { ++work_; unit(); }
        return status_;
    }
    Summary summary() const {
        const auto reserve = max_wait_loss_ + max_move_loss_;
        return {status_, reason_, phase_, p_.identity, work_, total_loss_, current_.health_lower,
                reserve, current_.health_lower > reserve ? current_.health_lower - reserve : 0,
                step_count_, move_count_, basic_count_, target_ == no_index ? -1 : p_.camps[target_].native_id};
    }
    std::span<const ModelStep> word() const { return w_.steps.first(step_count_); }
    std::span<const CampResidual> camp_residuals() const {
        return w_.camps.first(residual_count_);
    }
    // Deliberately no Complete/certificate/action permission interface. Future
    // composition must finish satellites, capture, loaded recall, every actual
    // spawn landing's bank leg and renewal, with observed residual guards.
    static constexpr Status whole_service_status() { return Status::Unsupported; }
    static constexpr Reason whole_service_reason() { return Reason::CaptureRecallBankRenewalNotPorted; }
private:
    Problem p_{}; Workspace w_{}; Status status_{Status::Idle}; Reason reason_{Reason::None};
    Phase phase_{Phase::Init}; bool bound_{}; std::size_t n_{}, index_{}, subindex_{}, target_{no_index};
    std::size_t harmful_count_{}, heap_size_{}, neighbor_{}, route_size_{}, route_cursor_{};
    std::size_t step_count_{}, move_count_{}, basic_count_{}, residual_count_{}; Tile active_{no_tile}, trace_{no_tile};
    int line_x_{}, line_y_{}, line_dx_{}, line_dy_{}, field_x_{}, field_y_{};
    std::uint64_t work_{}, total_loss_{};
    std::uint32_t initial_health_{}, max_wait_loss_{}, max_move_loss_{};
    ExpectedState current_{};
    bool fail(Status status, Reason reason) { status_ = status; reason_ = reason; return false; }
    bool valid_body() const {
        const auto& b = p_.body;
        return std::isfinite(b.health) && std::isfinite(b.max_health) && std::isfinite(b.strength) &&
            b.health >= 1.0f && b.health <= b.max_health && b.max_health <= 1048576.0f &&
            b.strength > 0.0f && b.strength <= 1048576.0f && b.range >= 0 &&
            static_cast<std::uint32_t>(b.range) <= std::max(p_.width, p_.height);
    }
    bool key_less(Tile a, Tile b) const {
        const auto& x = w_.nodes[a]; const auto& y = w_.nodes[b];
        if (x.loss != y.loss) return x.loss < y.loss;
        if (x.moves != y.moves) return x.moves < y.moves;
        return a < b;
    }
    void swap_heap(std::size_t a, std::size_t b) {
        std::swap(w_.heap[a], w_.heap[b]);
        w_.nodes[w_.heap[a]].heap_position = a; w_.nodes[w_.heap[b]].heap_position = b;
    }
    void offer(Tile tile) {
        auto pos = w_.nodes[tile].heap_position;
        if (pos == no_index) { pos = heap_size_++; w_.heap[pos] = tile; w_.nodes[tile].heap_position = pos; }
        while (pos && key_less(w_.heap[pos], w_.heap[(pos - 1) / 2])) { swap_heap(pos, (pos - 1) / 2); pos = (pos - 1) / 2; }
    }
    Tile pop() {
        const Tile result = w_.heap[0]; w_.nodes[result].heap_position = no_index;
        if (--heap_size_) {
            w_.heap[0] = w_.heap[heap_size_]; w_.nodes[w_.heap[0]].heap_position = 0;
            std::size_t pos = 0;
            while (2 * pos + 1 < heap_size_) {
                auto best = 2 * pos + 1;
                if (best + 1 < heap_size_ && key_less(w_.heap[best + 1], w_.heap[best])) ++best;
                if (!key_less(w_.heap[best], w_.heap[pos])) break;
                swap_heap(best, pos); pos = best;
            }
        }
        return result;
    }
    bool valid_camp(const Camp& c) const {
        return c.native_id >= 0 && c.tile < n_ && c.range >= 0 &&
            static_cast<std::uint32_t>(c.range) <= std::max(p_.width, p_.height) &&
            std::isfinite(c.hp) && c.hp >= 0.0f && c.hp <= 1048576.0f &&
            std::isfinite(c.shield) && c.shield >= 0.0f && c.shield <= 1048576.0f &&
            std::isfinite(c.strength) && c.strength >= 0.0f && c.strength <= 1048576.0f;
    }
    bool append(Op op, Tile destination = no_tile) {
        if (step_count_ >= w_.steps.size()) return fail(Status::Unknown, Reason::WorkspaceCapacity);
        const auto loss = w_.nodes[current_.tile].incoming;
        if (loss >= current_.health_lower) return fail(Status::Unknown, Reason::HealthBound);
        BasicEffect next{};
        if (op == Op::Basic) {
            const auto& camp = w_.camps[target_]; next = basic_effect(camp.hp, camp.shield, p_.body.strength);
            if (!next.valid) return fail(Status::Unknown, Reason::InvalidBounds);
            if (next.hp == camp.hp && next.shield == camp.shield) return fail(Status::Unknown, Reason::NoFloatProgress);
        }
        ModelStep step{}; step.op = op; step.phase = phase_; step.before = current_;
        step.pre_action_camp_loss = loss; step.actual_guards = common_guards;
        current_.health_lower -= loss; ++current_.elapsed_inputs; total_loss_ += loss;
        max_wait_loss_ = std::max(max_wait_loss_, loss);
        if (op == Op::Move) {
            current_.tile = destination; ++move_count_;
            max_move_loss_ = std::max(max_move_loss_, loss); step.actual_guards |= FreshMoveLegal;
        } else {
            auto& camp = w_.camps[target_];
            camp.hp = next.hp; camp.shield = next.shield; current_.target_hp = next.hp; current_.target_shield = next.shield;
            step.target_native_id = camp.native_id; ++basic_count_;
            step.actual_guards |= FreshBasicLegalAndReady | ActualTargetVisible;
            if (next.hp == 0.0f) {
                camp.model_dead = true; current_.selected_model_dead = 1;
                step.fresh_death_required_after = camp.native_id;
                step.actual_guards |= FreshDeathBeforeThreatReduction;
            }
        }
        step.after = current_; w_.steps[step_count_++] = step; return true;
    }
    void unit() {
        switch (phase_) {
        case Phase::Init:
            w_.nodes[index_++] = Node{};
            if (index_ == n_) { index_ = subindex_ = 0; phase_ = Phase::Static; }
            break;
        case Phase::Static:
            if (index_ < p_.static_blocked.size()) {
                const auto t = p_.static_blocked[index_++];
                if (t >= n_) { fail(Status::Unknown, Reason::InvalidBounds); break; }
                w_.nodes[t].blocked = true;
            } else if (subindex_ < p_.opaque.size()) {
                const auto t = p_.opaque[subindex_++];
                if (t >= n_) { fail(Status::Unknown, Reason::InvalidBounds); break; }
                w_.nodes[t].opaque = true;
            } else { index_ = 0; phase_ = Phase::Camps; }
            break;
        case Phase::Camps: {
            const auto& c = p_.camps[index_];
            if (!valid_camp(c)) { fail(Status::Unknown, Reason::InvalidBounds); break; }
            for (std::size_t i = 0; i < index_; ++i) if (p_.camps[i].native_id == c.native_id) { fail(Status::Unknown, Reason::InvalidBounds); return; }
            w_.nodes[c.tile].blocked = true; w_.camps[index_] = {c.native_id, c.hp, c.shield, false}; ++residual_count_;
            if (++index_ == p_.camps.size()) { index_ = 0; phase_ = Phase::Members; }
            break;
        }
        case Phase::Members: {
            const int id = p_.member_native_ids[index_]; std::size_t found = no_index;
            for (std::size_t i = 0; i < index_; ++i) if (p_.member_native_ids[i] == id) { fail(Status::Unknown, Reason::InvalidBounds); return; }
            for (std::size_t i = 0; i < p_.camps.size(); ++i) if (p_.camps[i].native_id == id) { found = i; break; }
            if (found == no_index) { fail(Status::Unknown, Reason::InvalidBounds); break; }
            if (p_.camps[found].strength > 0.0f) { ++harmful_count_; target_ = found; }
            if (++index_ == p_.member_native_ids.size()) {
                if (harmful_count_ != 1) { fail(Status::Unsupported, Reason::UnsupportedClass); break; }
                if (w_.nodes[p_.start].blocked || w_.nodes[p_.station].blocked ||
                    w_.nodes[p_.station].opaque || w_.nodes[p_.camps[target_].tile].opaque) {
                    fail(Status::Unsupported, Reason::StationNotFrontier); break;
                }
                const int sx = static_cast<int>(p_.station % p_.width), sy = static_cast<int>(p_.station / p_.width);
                const int tx = static_cast<int>(p_.camps[target_].tile % p_.width), ty = static_cast<int>(p_.camps[target_].tile / p_.width);
                if ((sx != tx && sy != ty) || std::max(std::abs(sx - tx), std::abs(sy - ty)) > p_.body.range) {
                    fail(Status::Unsupported, Reason::StationNotFrontier); break;
                }
                line_dx_ = (tx > sx) - (tx < sx); line_dy_ = (ty > sy) - (ty < sy);
                line_x_ = sx + line_dx_; line_y_ = sy + line_dy_; phase_ = Phase::Frontier;
                current_.target_hp = p_.camps[target_].hp; current_.target_shield = p_.camps[target_].shield;
            }
            break;
        }
        case Phase::Frontier: {
            const auto tile = static_cast<Tile>(line_y_ * static_cast<int>(p_.width) + line_x_);
            if (tile == p_.camps[target_].tile) { index_ = 0; field_x_ = field_y_ = -1; phase_ = Phase::Field; }
            else if (w_.nodes[tile].opaque) fail(Status::Unsupported, Reason::StationNotFrontier);
            else { line_x_ += line_dx_; line_y_ += line_dy_; }
            break;
        }
        case Phase::Field: {
            if (index_ == p_.camps.size()) {
                auto& start = w_.nodes[p_.start]; start.loss = 0; start.moves = 0; offer(p_.start);
                active_ = no_tile; phase_ = Phase::Search; break;
            }
            const auto& c = p_.camps[index_];
            if (c.strength == 0.0f) { ++index_; field_x_ = field_y_ = -1; break; }
            const int cx = static_cast<int>(c.tile % p_.width), cy = static_cast<int>(c.tile / p_.width);
            const int x0 = std::max(0, cx - c.range), y0 = std::max(0, cy - c.range);
            const int x1 = std::min(static_cast<int>(p_.width) - 1, cx + c.range), y1 = std::min(static_cast<int>(p_.height) - 1, cy + c.range);
            if (field_x_ < 0) { field_x_ = x0; field_y_ = y0; }
            auto& incoming = w_.nodes[static_cast<std::size_t>(field_y_) * p_.width + field_x_].incoming;
            const auto loss = static_cast<std::uint32_t>(std::ceil(c.strength));
            if (incoming > (1u << 30) - loss) { fail(Status::Unknown, Reason::InvalidBounds); break; }
            incoming += loss;
            if (++field_x_ > x1) { field_x_ = x0; if (++field_y_ > y1) { ++index_; field_x_ = field_y_ = -1; } }
            break;
        }
        case Phase::Search: {
            if (active_ == no_tile) {
                if (!heap_size_) { fail(Status::Unknown, Reason::NoRoute); break; }
                active_ = pop(); w_.nodes[active_].settled = true; neighbor_ = 0;
                if (active_ == p_.station) { trace_ = active_; phase_ = Phase::Trace; }
                break;
            }
            // The Python set ordering is immaterial: heap ties use tile identity
            // and equal (loss,moves) paths never overwrite a parent.
            const int dx = static_cast<int>(neighbor_ % 3) - 1, dy = static_cast<int>(neighbor_ / 3) - 1;
            if (++neighbor_ == 9) { neighbor_ = 0; }
            const int x = static_cast<int>(active_ % p_.width) + dx, y = static_cast<int>(active_ / p_.width) + dy;
            if ((dx || dy) && x >= 0 && y >= 0 && x < static_cast<int>(p_.width) && y < static_cast<int>(p_.height)) {
                const auto next = static_cast<Tile>(y * static_cast<int>(p_.width) + x); auto& to = w_.nodes[next]; const auto& from = w_.nodes[active_];
                const auto loss = from.loss + from.incoming; const auto moves = from.moves + 1;
                if (!to.blocked && !to.settled && (loss < to.loss || (loss == to.loss && moves < to.moves))) {
                    to.loss = loss; to.moves = moves; to.parent = active_; offer(next);
                }
            }
            if (neighbor_ == 0) active_ = no_tile;
            break;
        }
        case Phase::Trace:
            if (route_size_ >= w_.route.size()) { fail(Status::Unknown, Reason::WorkspaceCapacity); break; }
            w_.route[route_size_++] = trace_;
            if (trace_ == p_.start) { route_cursor_ = route_size_ - 1; phase_ = Phase::Ingress; }
            else { trace_ = w_.nodes[trace_].parent; if (trace_ == no_tile) fail(Status::Unknown, Reason::NoRoute); }
            break;
        case Phase::Ingress:
            if (route_cursor_) { if (append(Op::Move, w_.route[route_cursor_ - 1])) --route_cursor_; }
            else phase_ = Phase::HarmfulRemoval;
            break;
        case Phase::HarmfulRemoval:
            if (w_.camps[target_].model_dead) {
                if (current_.health_lower <= max_wait_loss_ + max_move_loss_) { fail(Status::Unknown, Reason::HealthBound); break; }
                if (step_count_ + 2 > p_.remaining_inputs) { fail(Status::Unknown, Reason::Horizon); break; }
                status_ = Status::PrefixReady; phase_ = Phase::Finished;
            } else if (basic_count_ >= p_.max_basic_inputs) fail(Status::Unknown, Reason::RemovalLimit);
            else append(Op::Basic);
            break;
        case Phase::Finished: case Phase::SatelliteApproach: case Phase::SatelliteRemoval:
        case Phase::CaptureApproach: case Phase::CaptureHold: case Phase::LoadedQuietRecall:
        case Phase::RecallArrivalObserved: case Phase::BankApproach: case Phase::Tribute:
        case Phase::ReturnSpawn: case Phase::RenewalObserved: break;
        }
    }
};
} // namespace mosaic::finite_service
