// Movement: one legal step per turn along distance fields, avoiding awake camps we are not
// fighting, never stepping onto camp spawn tiles unless capturing them.
#include "bot.h"
#include "config.h"
#include "nav_journal.h"

namespace wb {

const bitboard& Bot::blocked() {
    if (!blocked_ready_) {
        blocked_ = world_.terrain | cambc::vision::champion_tiles() | cambc::vision::camp_tiles()
            | cambc::vision::teleport_tiles();
        blocked_.set(pos_, false);
        blocked_ready_ = true;
    }
    return blocked_;
}

bool Bot::free_tile(vec2 p) {
    if (!in_bounds(p)) return false;
    for (const auto& b : bad_tiles_)
        if (b.until >= round_ && b.tile == p) return false;
    return !blocked().is_set(p);
}

void Bot::mark_bad(vec2 p) {
    std::size_t slot = 0;
    for (std::size_t i = 0; i < bad_tiles_.size(); ++i)
        if (bad_tiles_[i].until < bad_tiles_[slot].until) slot = i;
    bad_tiles_[slot] = {p, round_ + 5};
    blocked_.set(p, true);
}

// A refused step to a tile our vision called free: remember it and let the caller try another.
// Returns true when the refusal was of that kind (not a bug in our state).
bool Bot::move_error(const cambc::error& e, vec2 to) {
    if (e.code() == cambc::error_code::invalid_action_move_bad_destination) {
        mark_bad(to);
        return true;
    }
    log_error("warband error move r%d (%d,%d)->(%d,%d): %s\n", round_, pos_.x, pos_.y, to.x, to.y, e.what());
    return false;
}

bool Bot::in_camp_danger(vec2 p, int ignore_group) const {
    for (const auto& a : awake_list_)
        if (a.group != ignore_group && a.range > 0 && cheb(p, a.tile) <= a.range) return true;
    return false;
}

// Step to the neighbour with the smallest field value below ours. Camp tiles are entered only
// when they belong to the group we are capturing (stop_at 0 on that group's field).
bool Bot::step_field(const cambc::distance_field& f, int stop_at, int avoid_group) {
    nav_journal_call(3,avoid_group,-1,-1,stop_at);
    auto& me = cambc::self();
    if (!me.can_move()) return false;
    const auto here_raw = f.get(pos_);
    int here = here_raw == cambc::distance_field::unreachable ? (1 << 20) : here_raw;
    if (here <= stop_at) return false;
    const bool danger_here = in_camp_danger(pos_, avoid_group);
    // An ally standing on our only better tile would block us for good: then a sideways step to an
    // equal tile (not the one we came from) goes around it.
    bool ally_blocks = false;
    for (auto d : kDirs) {
        const vec2 q{pos_.x + d.x, pos_.y + d.y};
        if (!in_bounds(q) || world_.terrain.is_set(q) || free_tile(q)) continue;
        const auto v = f.get(q);
        if (v != cambc::distance_field::unreachable && static_cast<int>(v) < here
            && cambc::vision::has_champion(q) && !world_.camp_tiles.is_set(q))
            ally_blocks = true;
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        vec2 best{-1, -1};
        int best_score = 1 << 30;
        for (auto d : kDirs) {
            const vec2 q{pos_.x + d.x, pos_.y + d.y};
            if (!free_tile(q)) continue;
            const auto v = f.get(q);
            if (v == cambc::distance_field::unreachable) continue;
            const bool better = static_cast<int>(v) < here || here >= (1 << 20);
            const bool sideways = ally_blocks && static_cast<int>(v) == here && q != last_pos_;
            if (!better && !sideways) continue;
            if (world_.camp_tiles.is_set(q) && !(stop_at == 0 && world_.group_of(q) == avoid_group)) continue;
            int score = static_cast<int>(v) * 10 + (better ? 0 : 5);
            if (!danger_here && in_camp_danger(q, avoid_group)) score += 1000;
            if (score < best_score) { best_score = score; best = q; }
        }
        if (best.x < 0) return false;
        try {
            if (me.move_absolute(best)) {
                nav_journal_step(pos_.x,pos_.y,best.x,best.y);
                last_pos_ = pos_;
                pos_ = best;
                blocked_ready_ = false;
                return true;
            }
        } catch (const cambc::error& e) {
            if (!move_error(e, best)) return false;
            continue;
        }
        // A hidden occupant was revealed: mark it and try the next best tile.
        blocked_.set(best, true);
    }
    return false;
}

bool Bot::goto_tile(vec2 target, int within) {
    nav_journal_call(1,-1,target.x,target.y,within);
    if (!in_bounds(target) || cheb(pos_, target) <= within) return false;
    if (fuel() < 15000) return false;
    // Bounded search from the target to us over static walls and living camps.
    bitboard walls = world_.terrain | cambc::vision::camp_tiles();
    walls.set(target, false);
    walls.set(pos_, false);
    bitboard roots;
    roots.set(target);
    bitboard dest;
    dest.set(pos_);
    bitboard visited;
    cambc::distance_field f;
    const int bound = std::min(250, 2 * cheb(pos_, target) + 12);
    f.compute_to(walls, roots, dest, visited, static_cast<std::uint16_t>(bound));
    // A detour longer than the bound (walls between us): search the whole map once more, which
    // stops as soon as our tile is reached, when the turn can afford a full field.
    if (f.get(pos_) == cambc::distance_field::unreachable && bound < 250 && fuel() > world_.field_cost() + 25000)
        f.compute_to(walls, roots, dest, visited, 250);
    if (f.get(pos_) == cambc::distance_field::unreachable) {
        // Unreachable within the bound: greedy step by straight-line distance.
        auto& me = cambc::self();
        if (!me.can_move()) return false;
        vec2 best{-1, -1};
        int best_d = cheb(pos_, target);
        for (auto d : kDirs) {
            const vec2 q{pos_.x + d.x, pos_.y + d.y};
            if (!free_tile(q) || world_.camp_tiles.is_set(q)) continue;
            if (cheb(q, target) < best_d) { best_d = cheb(q, target); best = q; }
        }
        if (best.x < 0) return false;
        try {
            if (me.move_absolute(best)) { nav_journal_step(pos_.x,pos_.y,best.x,best.y); pos_ = best; blocked_ready_ = false; return true; }
        } catch (const cambc::error&) {}
        return false;
    }
    return step_field(f, within, world_.group_of(target));
}

bool Bot::goto_group(int g, int within) {
    nav_journal_call(2,g,-1,-1,within);
    if (const auto* f = world_.group_field(g, fuel() > world_.field_cost() + cfg::fuel_reserve + 10000))
        return step_field(*f, within, g);
    // No static field affordable now: a bounded search rooted at the group's tiles.
    if (fuel() < 15000) return false;
    const auto& grp = world_.groups[static_cast<std::size_t>(g)];
    const auto& gt = world_.tiles(g);
    bitboard walls = world_.terrain | (world_.camp_tiles & ~gt);
    walls.set(pos_, false);
    bitboard dest;
    dest.set(pos_);
    bitboard visited;
    cambc::distance_field f;
    const int bound = std::min(250, 2 * cheb(pos_, grp.center) + 12);
    f.compute_to(walls, gt, dest, visited, static_cast<std::uint16_t>(bound));
    if (f.get(pos_) == cambc::distance_field::unreachable) return goto_tile(grp.center, within + 1);
    return step_field(f, within, g);
}

bool Bot::goto_altar() {
    if (const auto* f = world_.altar_field()) return step_field(*f, 0, -1);
    vec2 best{-1, -1};
    for (auto a : world_.altars)
        if (best.x < 0 || cheb(a, pos_) < cheb(best, pos_)) best = a;
    return best.x >= 0 && goto_tile(best, 1);
}

bool Bot::goto_home() {
    if (const auto* f = world_.home_field()) return step_field(*f, 0, -1);
    return goto_tile(world_.my_spawn_c, 0);
}

// Retreat step: maximise distance from threats (saturating), with a pull towards home and, when
// it lies away from the threats, the pack anchor.
bool Bot::step_away(const std::vector<vec2>& threats, int toward_anchor) {
    auto& me = cambc::self();
    if (!me.can_move()) return false;
    const auto* home = world_.home_field(false);
    const vec2 anchor{pack_.ax, pack_.ay};
    const bool use_anchor = toward_anchor && pack_.has_anchor() && !i_lead_;
    auto score = [&](vec2 q) {
        float s = 0;
        for (auto t : threats) s += 10.0f * static_cast<float>(std::min(cheb(q, t), 7));
        if (home) {
            const auto v = home->get(q);
            if (v != cambc::distance_field::unreachable) s -= 1.5f * static_cast<float>(v);
        }
        if (use_anchor) s -= 2.0f * static_cast<float>(cheb(q, anchor));
        if (in_camp_danger(q, -1)) s -= 40.0f;
        return s;
    };
    const float here = score(pos_);
    for (int attempt = 0; attempt < 3; ++attempt) {
        vec2 best{-1, -1};
        float best_s = here;
        for (auto d : kDirs) {
            const vec2 q{pos_.x + d.x, pos_.y + d.y};
            if (!free_tile(q) || world_.camp_tiles.is_set(q)) continue;
            const float s = score(q);
            if (s > best_s) { best_s = s; best = q; }
        }
        if (best.x < 0) return false;
        try {
            if (me.move_absolute(best)) { nav_journal_step(pos_.x,pos_.y,best.x,best.y); pos_ = best; blocked_ready_ = false; return true; }
        } catch (const cambc::error& e) {
            if (!move_error(e, best)) return false;
            continue;
        }
        blocked_.set(best, true);
    }
    return false;
}

} // namespace wb
