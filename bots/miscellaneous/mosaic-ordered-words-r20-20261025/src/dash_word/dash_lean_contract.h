#pragma once
#include "dash_contract.h"
#include <limits>
#include <optional>
namespace mosaic::dash_word::lean {
inline constexpr int unreachable=65535;
struct MaskStamp {int epoch{-1};std::uint32_t actor{};std::uint64_t revision{};};
struct CampSupport {std::uint32_t amount{};bool valid{};};
struct Endpoint {
 Point pos;int remaining{unreachable};
 CampSupport support;bool floor_free{};
};
struct Choice {
 Point pos;int remaining{unreachable},dash_steps{};
};
inline std::optional<Choice> rank_quiet(Point origin,int origin_remaining,std::span<const Endpoint> endpoints) {
 std::optional<Choice> best;
 for(const auto& e:endpoints) {
  const int steps=distance(origin,e.pos);
  if(!e.floor_free||!e.support.valid||e.support.amount!=0||steps<1||steps>max_steps||
     e.remaining<0||e.remaining>=unreachable||
     (origin_remaining<unreachable&&e.remaining>=origin_remaining))continue;
  const Choice c{e.pos,e.remaining,steps};
  if(!best||c.remaining<best->remaining||
     (c.remaining==best->remaining&&(c.dash_steps<best->dash_steps||
      (c.dash_steps==best->dash_steps&&(c.pos.y<best->pos.y||
       (c.pos.y==best->pos.y&&c.pos.x<best->pos.x))))))best=c;
 }
 return best;
}
} // namespace mosaic::dash_word::lean
