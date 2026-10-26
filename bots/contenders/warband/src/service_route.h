#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace wb::route {

// Pure planning data. A route never authorizes a current native command.
enum class Role { WorkMember, Capture, Bank, Spawn };
enum class State { Empty, Searching, Reconstructing, Complete, Unsupported };
struct Member { int identity = -1; int tile = -1; bool alive = true; };
struct Segment {
    Role role = Role::WorkMember;
    int member_index = -1;
    int goal = -1;
    std::vector<int> path; // source excluded, destination included
};
struct Grid {
    int width = 0, height = 0;
    // blocked includes terrain, altars, enemy spawn and NONSELECTED static camps.
    // Selected members are represented separately and become free only in the
    // hypothetical completed itinerary. A member cannot open permanent terrain.
    std::vector<std::uint8_t> blocked, quiet, spawn, bank;
};

class Planner {
public:
    static constexpr int max_area = 10000, max_members = 30;
    static constexpr int max_path_cells = 20000;

    bool begin(const Grid& grid, int source, std::span<const Member> members) {
        state_ = State::Unsupported;
        result_.clear();
        grid_ = &grid;
        if (grid.width <= 0 || grid.height <= 0 || grid.width > 100 || grid.height > 100)
            return false;
        area_ = grid.width * grid.height;
        if (area_ <= 0 || area_ > max_area || members.empty() || members.size() > max_members ||
            grid.blocked.size() != static_cast<std::size_t>(area_) ||
            grid.quiet.size() != static_cast<std::size_t>(area_) ||
            grid.spawn.size() != static_cast<std::size_t>(area_) ||
            grid.bank.size() != static_cast<std::size_t>(area_) || !inside(source) || !grid.quiet[source])
            return false;
        members_.assign(members.begin(), members.end());
        for (std::size_t i = 0; i < members_.size(); ++i) {
            if (members_[i].identity < 0 || !inside(members_[i].tile) ||
                grid.blocked[members_[i].tile] || !grid.quiet[members_[i].tile])
                return false;
            for (std::size_t j = 0; j < i; ++j)
                if (members_[j].identity == members_[i].identity || members_[j].tile == members_[i].tile)
                    return false;
        }
        occupied_ = grid.blocked;
        for (const auto& member : members_) occupied_[member.tile] = member.alive;
        seen_.assign(area_, 0);
        goal_stamp_.assign(area_, 0);
        goal_member_.resize(area_);
        parent_.resize(area_);
        queue_.clear(); queue_.reserve(area_);
        source_ = source;
        occupied_[source_] = 0;
        epoch_ = 0; total_path_ = 0; phase_ = Role::WorkMember;
        search_nodes_ = 0; reconstruct_nodes_ = 0;
        start_search();
        return state_ != State::Unsupported;
    }

    // A separately admitted residual service may already own a real receipt.
    // Its route preparation repeats only the remaining bank/spawn suffix.
    bool begin_suffix(const Grid& grid, int source, Role role = Role::Bank) {
        state_ = State::Unsupported; result_.clear(); members_.clear();
        grid_ = &grid;
        if ((role != Role::Bank && role != Role::Spawn) || grid.width <= 0 || grid.height <= 0 ||
            grid.width > 100 || grid.height > 100) return false;
        area_ = grid.width * grid.height;
        if (grid.blocked.size() != static_cast<std::size_t>(area_) ||
            grid.quiet.size() != static_cast<std::size_t>(area_) ||
            grid.spawn.size() != static_cast<std::size_t>(area_) ||
            grid.bank.size() != static_cast<std::size_t>(area_) || !inside(source) || !grid.quiet[source]) return false;
        occupied_ = grid.blocked;
        seen_.assign(area_,0);goal_stamp_.assign(area_,0);
        goal_member_.resize(area_);parent_.resize(area_);
        queue_.clear();queue_.reserve(area_);
        source_=source;epoch_=0;total_path_=0;phase_=role;
        search_nodes_=reconstruct_nodes_=0;
        start_search();return state_ != State::Unsupported;
    }

    // One unit visits at most eight search neighbors or one reconstruction cell;
    // completing a segment also marks at most 30*8 next-stage goal cells.
    // Callers choose a bounded slice and pay every preparation input physically.
    State advance(unsigned units = 32) {
        while (units-- && (state_ == State::Searching || state_ == State::Reconstructing)) {
            if (state_ == State::Reconstructing) {
                if (backtrack_ == source_) { finish_segment(); continue; }
                if (!inside(backtrack_) || reverse_.size() >= static_cast<std::size_t>(max_path_cells) ||
                    parent_[backtrack_] < 0) { state_ = State::Unsupported; break; }
                reverse_.push_back(backtrack_);
                backtrack_ = parent_[backtrack_];
                ++reconstruct_nodes_;
                continue;
            }
            if (head_ == queue_.size()) { state_ = State::Unsupported; break; }
            const int tile = queue_[head_++];
            ++search_nodes_;
            if (is_goal(tile)) {
                found_ = tile; backtrack_ = tile; reverse_.clear();
                found_member_ = phase_ == Role::WorkMember ? goal_member_[tile] : -1;
                state_ = State::Reconstructing;
                continue;
            }
            neighbors(tile, [&](int next) {
                if (seen_[next] != epoch_ && free(next)) {
                    seen_[next] = epoch_; parent_[next] = tile; queue_.push_back(next);
                }
            });
        }
        return state_;
    }

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] const std::vector<Segment>& result() const { return result_; }
    [[nodiscard]] std::uint64_t search_nodes() const { return search_nodes_; }
    [[nodiscard]] std::uint64_t reconstruct_nodes() const { return reconstruct_nodes_; }
    [[nodiscard]] std::size_t pending_frontier() const { return queue_.size() - head_; }

private:
    const Grid* grid_ = nullptr;
    int area_ = 0, source_ = -1, found_ = -1, found_member_ = -1, backtrack_ = -1;
    unsigned epoch_ = 0;
    std::size_t head_ = 0;
    int total_path_ = 0;
    State state_ = State::Empty;
    Role phase_ = Role::WorkMember;
    std::vector<Member> members_;
    std::vector<std::uint8_t> occupied_;
    std::vector<unsigned> seen_, goal_stamp_;
    std::vector<int> goal_member_, parent_, queue_, reverse_;
    std::vector<Segment> result_;
    std::uint64_t search_nodes_ = 0, reconstruct_nodes_ = 0;

    [[nodiscard]] bool inside(int tile) const { return tile >= 0 && tile < area_; }
    [[nodiscard]] bool free(int tile) const { return grid_->quiet[tile] && !occupied_[tile]; }
    template<class Fn> void neighbors(int tile, Fn&& fn) const {
        const int x = tile % grid_->width, y = tile / grid_->width;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int nx = x + dx, ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < grid_->width && ny < grid_->height)
                    fn(ny * grid_->width + nx);
            }
    }
    [[nodiscard]] bool is_goal(int tile) const {
        switch (phase_) {
        case Role::WorkMember: return goal_stamp_[tile] == epoch_;
        case Role::Capture: {
            if (!selected(tile)) return false;
            // Choose a boundary capture cell. The delivery suffix must be able
            // to leave without stepping through another potentially respawned
            // member, including when the final work station was interior.
            bool exit = false;
            neighbors(tile, [&](int next) {
                if (grid_->quiet[next] && !grid_->blocked[next] && !selected(next)) exit = true;
            });
            return exit;
        }
        case Role::Bank: return grid_->bank[tile] != 0;
        case Role::Spawn: return grid_->spawn[tile] != 0;
        }
        return false;
    }
    [[nodiscard]] bool selected(int tile) const {
        return std::any_of(members_.begin(), members_.end(),
            [&](const Member& member) { return member.tile == tile; });
    }
    void start_search() {
        if (++epoch_ == 0) { state_ = State::Unsupported; return; }
        if (phase_ == Role::WorkMember) {
            const bool any = std::any_of(members_.begin(), members_.end(), [](const Member& m) { return m.alive; });
            if (!any) phase_ = Role::Capture;
        }
        if (phase_ == Role::WorkMember) {
            // Adjacent stations deliberately support all ordinary basic kits.
            // Live visibility/attack geometry is still required by the actuator.
            for (std::size_t i = 0; i < members_.size(); ++i) if (members_[i].alive)
                neighbors(members_[i].tile, [&](int tile) {
                    if (free(tile) && goal_stamp_[tile] != epoch_) {
                        goal_stamp_[tile] = epoch_; goal_member_[tile] = static_cast<int>(i);
                    }
                });
        }
        head_ = 0; queue_.clear(); queue_.push_back(source_);
        seen_[source_] = epoch_; parent_[source_] = -1;
        state_ = State::Searching;
    }
    void finish_segment() {
        if (total_path_ + static_cast<int>(reverse_.size()) > max_path_cells) {
            state_ = State::Unsupported; return;
        }
        Segment segment{phase_, found_member_, found_, {}};
        segment.path.assign(reverse_.rbegin(), reverse_.rend());
        total_path_ += static_cast<int>(segment.path.size());
        result_.push_back(std::move(segment));
        source_ = found_;
        if (phase_ == Role::WorkMember) {
            members_[found_member_].alive = false;
            occupied_[members_[found_member_].tile] = 0;
        } else if (phase_ == Role::Capture) {
            // Delivery and return do not borrow walkability from a dead camp:
            // the group can respawn before that suffix finishes.
            for (const auto& member : members_) occupied_[member.tile] = 1;
            phase_ = Role::Bank;
        }
        else if (phase_ == Role::Bank) phase_ = Role::Spawn;
        else { state_ = State::Complete; return; }
        start_search();
    }
};
} // namespace wb::route
