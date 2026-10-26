#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace wb {

enum class LedgerStep { idle, pending, committed, failed };

// SDK-free geometry accounting. A dense stable member key owns one immutable
// position/range and caller-supplied alive/threat predicates. No observation,
// lifecycle, clock, or safety fact is inferred here.
class CampMaskLedger {
    struct Member {
        int x{}, y{}, range{};
        bool registered{}, alive{}, threat{};
    };
    enum class Phase { check_living, check_threat, apply_living, apply_threat, commit };
    struct Update {
        std::size_t key{}, tile{};
        int x{}, y{}, range{}, living_delta{}, threat_delta{};
        int left{}, right{}, top{}, bottom{}, cursor_x{}, cursor_y{};
        bool alive{}, threat{};
        Phase phase{Phase::check_living};
    } update_;

    int width_{}, height_{};
    bool healthy_{}, pending_{};
    std::size_t registered_{};
    std::vector<Member> members_;
    std::vector<std::uint16_t> living_count_, threat_count_;
    std::vector<std::uint8_t> living_bytes_, threat_bytes_;

    bool fail() { healthy_ = pending_ = false; return false; }
    std::size_t tile(int x, int y) const { return std::size_t(y)*std::size_t(width_)+std::size_t(x); }
    static bool can_adjust(std::uint16_t value, int delta) {
        return (delta >= 0 || value > 0)
            && (delta <= 0 || value < std::numeric_limits<std::uint16_t>::max());
    }
    static void adjust(std::vector<std::uint16_t>& counts, std::vector<std::uint8_t>& bytes,
                       std::size_t cell, int delta) {
        const auto before = counts[cell];
        const auto after = static_cast<std::uint16_t>(int(before)+delta);
        counts[cell] = after;
        const auto bit = static_cast<std::uint8_t>(1u << (cell&7));
        if (before == 0 && after != 0) bytes[cell>>3] |= bit;
        else if (before != 0 && after == 0) bytes[cell>>3] &= static_cast<std::uint8_t>(~bit);
    }
    void restart_cursor() { update_.cursor_x = update_.left; update_.cursor_y = update_.top; }
    bool next_cell() {
        if (update_.cursor_x < update_.right) { ++update_.cursor_x; return true; }
        update_.cursor_x = update_.left;
        return ++update_.cursor_y <= update_.bottom;
    }

public:
    static constexpr std::size_t max_cells_per_advance = 128;

    bool reset(int width, int height, std::size_t member_capacity = 160) {
        healthy_ = pending_ = false; registered_ = 0; width_ = height_ = 0;
        members_.clear(); living_count_.clear(); threat_count_.clear();
        living_bytes_.clear(); threat_bytes_.clear();
        if (width <= 0 || height <= 0
            || member_capacity > std::numeric_limits<std::uint16_t>::max()) return false;
        const auto cells64 = std::uint64_t(width)*std::uint64_t(height);
        if (cells64 > std::numeric_limits<std::size_t>::max()/sizeof(std::uint16_t)) return false;
        const auto cells = static_cast<std::size_t>(cells64);
        try {
            members_.resize(member_capacity);
            living_count_.assign(cells,0); threat_count_.assign(cells,0);
            living_bytes_.assign((cells+7)/8,0); threat_bytes_.assign((cells+7)/8,0);
        } catch (...) { return false; }
        width_ = width; height_ = height; healthy_ = true;
        return true;
    }

    int width() const { return width_; }
    int height() const { return height_; }
    std::size_t member_capacity() const { return members_.size(); }
    std::size_t registered_members() const { return registered_; }
    bool failed() const { return !healthy_; }
    bool pending_update() const { return pending_; }
    // Initial registration is a publication barrier: a partial cold world is
    // never advertised as an empty/complete mask. Capacity zero is an empty map.
    bool valid() const { return healthy_ && !pending_ && registered_ == members_.size(); }
    // Borrowed views are usable only while valid() remains true. Starting an
    // update invalidates every prior view until the transaction commits.
    std::span<const std::uint8_t> living_bytes() const {
        return valid() ? std::span<const std::uint8_t>{living_bytes_} : std::span<const std::uint8_t>{};
    }
    std::span<const std::uint8_t> threat_bytes() const {
        return valid() ? std::span<const std::uint8_t>{threat_bytes_} : std::span<const std::uint8_t>{};
    }

    bool begin_transition(std::size_t key, int x, int y, int range,
                          bool old_alive, bool old_threat, bool new_alive, bool new_threat) {
        if (!healthy_ || pending_ || key >= members_.size() || x < 0 || y < 0
            || x >= width_ || y >= height_ || range < 0
            || (old_threat && !old_alive) || (new_threat && !new_alive)) return fail();
        const auto& member = members_[key];
        if (member.registered) {
            if (member.x != x || member.y != y || member.range != range
                || member.alive != old_alive || member.threat != old_threat) return fail();
        } else if (old_alive || old_threat) return fail();

        update_ = {};
        update_.key = key; update_.tile = tile(x,y);
        update_.x = x; update_.y = y; update_.range = range;
        update_.living_delta = int(new_alive)-int(old_alive);
        update_.threat_delta = int(new_threat)-int(old_threat);
        update_.alive = new_alive; update_.threat = new_threat;
        update_.left = static_cast<int>(std::max<std::int64_t>(0,std::int64_t(x)-range));
        update_.right = static_cast<int>(std::min<std::int64_t>(width_-1,std::int64_t(x)+range));
        update_.top = static_cast<int>(std::max<std::int64_t>(0,std::int64_t(y)-range));
        update_.bottom = static_cast<int>(std::min<std::int64_t>(height_-1,std::int64_t(y)+range));
        restart_cursor(); pending_ = true;
        return true;
    }

    // At most min(max_cells,128) tile visits per call, across preflight and
    // application combined. No allocation/import occurs. A complete preflight
    // precedes every mutation; partial application remains unpublishable.
    LedgerStep advance(std::size_t max_cells = max_cells_per_advance) {
        if (!healthy_) return LedgerStep::failed;
        if (!pending_) return LedgerStep::idle;
        auto remaining = std::min(max_cells,max_cells_per_advance);
        if (remaining == 0) return LedgerStep::pending;
        for (;;) {
            switch (update_.phase) {
            case Phase::check_living:
                if (update_.living_delta != 0) {
                    if (remaining == 0) return LedgerStep::pending;
                    --remaining;
                    if (!can_adjust(living_count_[update_.tile],update_.living_delta)) {
                        fail(); return LedgerStep::failed;
                    }
                }
                update_.phase = Phase::check_threat; restart_cursor();
                break;
            case Phase::check_threat:
                if (update_.threat_delta == 0) { update_.phase = Phase::apply_living; break; }
                if (remaining == 0) return LedgerStep::pending;
                --remaining;
                if (!can_adjust(threat_count_[tile(update_.cursor_x,update_.cursor_y)],update_.threat_delta)) {
                    fail(); return LedgerStep::failed;
                }
                if (!next_cell()) update_.phase = Phase::apply_living;
                break;
            case Phase::apply_living:
                if (update_.living_delta != 0) {
                    if (remaining == 0) return LedgerStep::pending;
                    --remaining;
                    adjust(living_count_,living_bytes_,update_.tile,update_.living_delta);
                }
                update_.phase = Phase::apply_threat; restart_cursor();
                break;
            case Phase::apply_threat:
                if (update_.threat_delta == 0) { update_.phase = Phase::commit; break; }
                if (remaining == 0) return LedgerStep::pending;
                --remaining;
                adjust(threat_count_,threat_bytes_,tile(update_.cursor_x,update_.cursor_y),update_.threat_delta);
                if (!next_cell()) update_.phase = Phase::commit;
                break;
            case Phase::commit: {
                auto& member = members_[update_.key];
                if (!member.registered) ++registered_;
                member = {update_.x,update_.y,update_.range,true,update_.alive,update_.threat};
                pending_ = false;
                return LedgerStep::committed;
            }
            }
        }
    }
};

} // namespace wb
