#pragma once
#include <memory>
#include <type_traits>

namespace p8::native {
// Copy the exact type's compile-time defaults. Large nested input arrays would
// otherwise execute hundreds of sentinel stores in the first gameplay turn.
// Each guest receives its own mutable object; the seed holds no live state.
template<class T> inline constexpr T immutable_defaults{};
template<class T> const T& exact_default() {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::is_trivially_destructible_v<T>);
    return immutable_defaults<T>;
}
template<class T> std::unique_ptr<T> make_default() {
    return std::make_unique<T>(exact_default<T>());
}
} // namespace p8::native
