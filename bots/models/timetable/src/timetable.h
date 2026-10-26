#pragma once
#include "../../common/pure.h"

namespace plan4::timetable {
inline constexpr const char* version = "plan4-timetable/1";
inline constexpr int max_actors = 4, max_input_actors = 16, max_route_steps = 8, max_waits = 2;
enum class Tile { walkable, blocked, unknown };
struct Topology {
    // Immutable public geometry only; this callback must not query the SDK.
    Tile (*at)(Point,const void*) = nullptr;
    const void* context = nullptr;
    Tile operator()(Point p) const { return at ? at(p,context) : Tile::unknown; }
};
struct TurnOrder {
    bool known = false;
    bool already_acted_current_round = false;
    bool before_own_next_round = false;
};
struct ActorSample {
    int id = -1;
    Point position{};
    Provenance provenance{};
    TurnOrder order{};
    bool personally_visible = false;
};
struct Track {
    ActorSample current{};
    Point previous{};
    int previous_round = -1;
    bool has_previous = false;
};
struct RouteWindow {
    int observed_round = -1, first_arrival_round = -1;
    Point origin{};
    const Point* steps = nullptr;
    int count = 0;
    bool public_route_validated = false, ordinary_movement = false;
};
struct Projection {
    Point position{};
    int moves_before_arrival = 0;
    Status status = Status::unknown;
    bool extrapolated = false, unsupported = false, stopped_at_public_obstacle = false;
};
struct Conflict {
    int actor_id = -1, arrival_offset = -1, step = -1;
    Point destination{}, other_position{};
    bool edge_swap_risky = false;
};
struct Quote {
    std::array<Conflict,max_actors*2> conflicts{};
    int conflict_count = 0, wait_phases = 0, compared_arrivals = 0;
    bool supported = false, unknown_motion = false, observation_truncated = false;
    bool outside_horizon = false, fallback_after_two = false;
};
class State {
    std::array<Track,max_actors> tracks_{};
    int count_ = 0, round_ = -1, actor_ = -1;
    Point own_position_{};
    bool enabled_ = true, truncated_ = false;
    static int distance(Point a, Point b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
    static bool valid(const ActorSample& sample, int round, int own) {
        return sample.id >= 0 && sample.id != own && sample.personally_visible
            && sample.position.x >= 0 && sample.position.y >= 0
            && sample.provenance.observed_round == round && sample.provenance.status == Status::exact_now
            && sample.provenance.source == Source::personal_vision;
    }
    static Point velocity(const Track& track) {
        return {(track.current.position.x > track.previous.x) - (track.current.position.x < track.previous.x),
                (track.current.position.y > track.previous.y) - (track.current.position.y < track.previous.y)};
    }
public:
    explicit State(bool enabled = true) : enabled_(enabled) {}
    int count() const { return count_; }
    const Track* track(int ordinal) const { return ordinal >= 0 && ordinal < count_ ? &tracks_[ordinal] : nullptr; }
    bool observe(int round, int actor, Point own_position, const ActorSample* samples, int count, bool truncated = false) {
        truncated_ = truncated;
        if (round < 0 || actor < 0 || own_position.x < 0 || own_position.y < 0
            || count < 0 || count > max_input_actors || (count && !samples)) {
            count_ = 0; truncated_ = true; return false;
        }
        if (actor_ != actor || round < round_) count_ = 0;
        std::array<ActorSample,max_actors> selected{};
        int selected_count = 0;
        for (int n = 0; n < count; ++n) {
            const auto& sample = samples[n]; if (!valid(sample,round,actor)) continue;
            bool duplicate = false;
            for (int p = 0; p < n; ++p) if (samples[p].id == sample.id && valid(samples[p],round,actor)) duplicate = true;
            if (duplicate) { count_ = 0; round_ = round; actor_ = actor; truncated_ = true; return false; }
            int place = 0;
            while (place < selected_count && (distance(own_position,selected[place].position) < distance(own_position,sample.position)
                || (distance(own_position,selected[place].position) == distance(own_position,sample.position)
                    && selected[place].id < sample.id))) ++place;
            if (place >= max_actors) continue;
            const int last = std::min(selected_count,max_actors-1);
            for (int p = last; p > place; --p) selected[p] = selected[p-1];
            selected[place] = sample; selected_count = std::min(max_actors,selected_count+1);
        }
        std::array<Track,max_actors> next{};
        for (int n = 0; n < selected_count; ++n) {
            next[n].current = selected[n];
            for (int old = 0; old < count_; ++old) if (tracks_[old].current.id == selected[n].id) {
                if (tracks_[old].current.provenance.observed_round < round) {
                    next[n].previous = tracks_[old].current.position;
                    next[n].previous_round = tracks_[old].current.provenance.observed_round;
                    next[n].has_previous = true;
                } else {
                    next[n].previous = tracks_[old].previous; next[n].previous_round = tracks_[old].previous_round;
                    next[n].has_previous = tracks_[old].has_previous;
                }
                break;
            }
        }
        tracks_ = next; count_ = selected_count; round_ = round; actor_ = actor; own_position_ = own_position;
        return true;
    }
    Projection project(int ordinal, int arrival_offset, Topology topology) const {
        Projection result;
        if (ordinal < 0 || ordinal >= count_ || arrival_offset < 0 || arrival_offset > 1) return result;
        const auto& track = tracks_[ordinal]; result.position = track.current.position;
        result.status = arrival_offset ? Status::predicted : Status::exact_now;
        if (!arrival_offset) return result; // No other actor can act inside our current ordinary step.
        const int age = round_ - track.previous_round;
        if (!enabled_ || !track.has_previous || age < 1 || age > 2 || !track.current.order.known) {
            result.unsupported = enabled_; return result;
        }
        result.moves_before_arrival = (!track.current.order.already_acted_current_round ? 1 : 0)
            + (track.current.order.before_own_next_round ? 1 : 0);
        const auto delta = velocity(track);
        for (int move = 0; move < result.moves_before_arrival; ++move) {
            const Point next{result.position.x+delta.x,result.position.y+delta.y};
            const auto legal = topology(next);
            if (legal == Tile::unknown) {
                result.position = track.current.position; result.extrapolated = false; result.unsupported = true; return result;
            }
            if (legal == Tile::blocked) { result.stopped_at_public_obstacle = true; break; }
            result.position = next; result.extrapolated = true;
        }
        return result;
    }
    Quote quote(const RouteWindow& route, Topology topology) const {
        Quote result; result.observation_truncated = truncated_;
        if (round_ < 0 || actor_ < 0 || route.observed_round != round_ || route.origin != own_position_
            || route.first_arrival_round < round_ || !route.public_route_validated
            || !route.ordinary_movement || route.count < 0 || route.count > max_route_steps
            || (route.count && !route.steps) || topology(route.origin) != Tile::walkable) return result;
        Point previous = route.origin;
        for (int n = 0; n < route.count; ++n) {
            if (route.steps[n] == previous || distance(route.steps[n],previous) != 1 || topology(route.steps[n]) != Tile::walkable) return result;
            previous = route.steps[n];
        }
        result.supported = true;
        int step = 0;
        while (step < route.count && result.wait_phases < max_waits) {
            const int offset = route.first_arrival_round-round_ + step + result.wait_phases;
            if (offset > 1) { result.outside_horizon = true; break; }
            ++result.compared_arrivals;
            bool blocked = false;
            const Point own_previous = step ? route.steps[step-1] : route.origin;
            for (int n = 0; n < count_; ++n) {
                const auto predicted = project(n,offset,topology);
                result.unknown_motion |= predicted.unsupported;
                if (predicted.position != route.steps[step]) continue;
                // Stable public native-ID rank resolves future predicted
                // contention. An occupied immediate move never gains permission
                // from rank; the shared fresh SDK movement mask still decides.
                if (offset && actor_ < tracks_[n].current.id) continue;
                blocked = true;
                if (result.conflict_count < int(result.conflicts.size())) {
                    auto& conflict = result.conflicts[result.conflict_count++];
                    conflict = {tracks_[n].current.id,offset,step,route.steps[step],predicted.position,false};
                    if (enabled_ && offset && !predicted.unsupported && tracks_[n].has_previous && tracks_[n].current.order.known
                        && predicted.moves_before_arrival == 0 && tracks_[n].current.position == route.steps[step]) {
                        const auto delta = velocity(tracks_[n]);
                        conflict.edge_swap_risky = Point{tracks_[n].current.position.x+delta.x,
                            tracks_[n].current.position.y+delta.y} == own_previous;
                    }
                }
            }
            if (blocked) ++result.wait_phases;
            else ++step;
        }
        result.fallback_after_two = result.wait_phases == max_waits;
        return result;
    }
};

struct WaitLedger { std::uint64_t decision = 0; int used = 0; bool initialized = false; };
struct WaitAction { bool wait_one_owner_phase = false, use_fresh_ordinary_route = false; };
// Called only when the common executor actually accepts a safe wait. A quote
// never moves a unit, reserves a tile or installs another actor's future path.
inline WaitAction bounded_wait(const Quote& quote, WaitLedger& ledger, std::uint64_t decision, bool safe_to_wait) {
    if (!ledger.initialized || ledger.decision != decision) { ledger = {decision,0,true}; }
    if (ledger.used < 0) ledger.used = 0;
    if (!quote.supported || quote.wait_phases < 0 || quote.wait_phases > max_waits
        || !safe_to_wait || ledger.used >= max_waits) return {false,true};
    if (!quote.wait_phases) return {false,false};
    ++ledger.used; return {true,false};
}
} // namespace plan4::timetable
