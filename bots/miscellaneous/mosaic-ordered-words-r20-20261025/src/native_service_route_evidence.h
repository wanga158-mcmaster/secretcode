#pragma once
#include "native_service_types.h"
#include <cstdint>

namespace mosaic::native_service {

// Results describe the route exclusion, not SDK permission or actual safety.
// Newer accepted evidence with the same exclusion updates its epoch but returns
// unchanged. Conflict is always explicit; compare excluded() before/after if a
// caller needs to know whether a conflict changed its published route board.
enum class RouteUpdate : std::uint8_t { unchanged, changed, stale, conflict, invalid };

struct RouteCell {
    constexpr RouteUpdate observe(Epoch epoch,bool blocked) noexcept {
        if(!valid(epoch))return RouteUpdate::invalid;
        if((valid(invalidation_)&&before(epoch,invalidation_)) ||
           (valid(seen_)&&before(epoch,seen_)))return RouteUpdate::stale;
        if(epoch==seen_){
            if(state_==State::conflict)return RouteUpdate::conflict;
            if(known() && excluded()!=blocked){
                state_=State::conflict;
                return RouteUpdate::conflict;
            }
        }
        const bool was_excluded=excluded();
        seen_=epoch;state_=blocked?State::blocked:State::empty;
        return was_excluded!=excluded()?RouteUpdate::changed:RouteUpdate::unchanged;
    }

    // Revoke observations strictly older than this lifecycle boundary. An
    // unknown, nonexcluded cell is not an actual-empty observation. Preserve a
    // monotone boundary so a staged old bulk positive cannot resurrect it.
    constexpr RouteUpdate invalidate_before(Epoch epoch) noexcept {
        if(!valid(epoch))return RouteUpdate::invalid;
        if(valid(invalidation_)&&before(epoch,invalidation_))return RouteUpdate::stale;
        const bool was_excluded=excluded();invalidation_=epoch;
        if(valid(seen_)&&before(seen_,epoch))state_=State::unknown;
        return was_excluded!=excluded()?RouteUpdate::changed:RouteUpdate::unchanged;
    }

    // A contradictory same-epoch observation conservatively excludes the cell
    // while withholding known actual body truth until newer evidence arrives.
    constexpr bool excluded()const noexcept {return state_==State::blocked||state_==State::conflict;}
    constexpr bool known()const noexcept {return state_==State::blocked||state_==State::empty;}
    constexpr bool conflicted()const noexcept {return state_==State::conflict;}
    constexpr Epoch seen_epoch()const noexcept {return seen_;}
    constexpr Epoch invalidation_epoch()const noexcept {return invalidation_;}

private:
    enum class State : std::uint8_t { unknown,empty,blocked,conflict };
    Epoch seen_{},invalidation_{};
    State state_{State::unknown};
    static constexpr bool valid(Epoch epoch) noexcept {return epoch.round>=0;}
    static constexpr bool before(Epoch a,Epoch b) noexcept {
        return a.round!=b.round?a.round<b.round:
            a.revision!=b.revision?a.revision<b.revision:a.attempt<b.attempt;
    }
};

} // namespace mosaic::native_service
