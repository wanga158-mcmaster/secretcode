#pragma once

namespace p11::movement {
// Reserves cover a cold atomic SDK operation plus a bounded unwind. Their
// adequacy is measured on native maps; a fixture cannot prove arbitrary SDK
// operation costs. No resource refusal is reported as a blocked camp.
inline constexpr int start_reserve=40000;
inline constexpr int retry_reserve=22000;
inline constexpr int refresh_reserve=30000;
inline constexpr int target_reserve=20000;
struct Yield {};
template<class GetFuel> void reserve(int minimum,GetFuel remaining){
    const auto value=remaining();
    if(value<static_cast<decltype(value)>(minimum))throw Yield{};
}
} // namespace p11::movement
