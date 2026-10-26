#pragma once
// One QUERY-ONLY transaction: one captured slot, one native reach menu, one
// final body/context fence, then at most one cast. This replaces the costly
// public nominate -> attempt re-nomination path in dash_sdk.h.
#include "dash_sdk.h"
#include "dash_lean_contract.h"
namespace mosaic::dash_word::lean {
enum class Outcome {no_dash,unavailable,unsupported,stale,deferred,no_candidate,declined,refused,applied};
inline Outcome outcome(Status s) {
 switch(s) {
  case Status::no_dash:return Outcome::no_dash;
  case Status::unavailable:return Outcome::unavailable;
  case Status::unsupported:return Outcome::unsupported;
  case Status::stale:case Status::stale_after_attempt:return Outcome::stale;
  case Status::deferred:return Outcome::deferred;
  default:return Outcome::no_candidate;
 }
}
// Allocate ONCE behind the caller's setup admission. These three buffers are
// fully overwritten by ReachableMaskWithScratch; no per-call map allocation,
// copy, zero, complement or vector positions() is performed here.
struct Scratch {
 cambc::bitboard frontier{cambc::uninitialized};
 cambc::bitboard work{cambc::uninitialized};
 cambc::bitboard reachable{cambc::uninitialized};
 std::array<cambc::vec2,max_cells> positions{};
 std::array<Endpoint,max_cells> endpoints{};
};
struct Result {
 Outcome status{Outcome::no_candidate};
 bool nominated{},attempted{},applied{},requires_refresh{};
 std::optional<Snapshot> before;
 std::optional<Choice> choice;
};
inline bool actual_owner(const MaskStamp& s) {
 return s.epoch>=0&&int(cambc::round_number())==s.epoch&&
  std::uint32_t(cambc::handle_champion_t(cambc::self()))==s.actor;
}
// Preconditions:
// * transit_blocked = static walls|altars|enemy spawn (NO holes) plus FRESH
//   observed champions/camps/teleport reservations; only actual self is cleared.
// * mask_stamp captures the actual epoch/actor and caller's physical-attempt
//   revision when that mask was built. revision() must increment after EVERY
//   command attempt, including false/refusal/reveal. No command may intervene.
// * floor_free reads immutable ordinary static terrain, so holes are rejected.
// * support_at supplies COMPLETE potential CAMP support, not a local hot set.
// * loose_distance is a hole-permissive goal field, NOMINATION ONLY. A finite
//   value proves no future walking/dash/cooldown route.
// * accept compares this endpoint against the already-built ordinary incumbent;
//   it and all other callbacks are QUERY-ONLY. Same actual input has no enemy or
//   camp interposition. The caller must reserve measured setup+ranking+dispatch
//   fuel and make guard fence that reserve and owner epoch.
// Any cast result, including false, invalidates vision/body/path-dependent
// facts. No retry; caller refreshes actual body+vision before another word.
// A success does not mutate ordinary walked, or invent an ordinary reverse edge.
template<class Guard,class Revision,class FloorFree,class SupportAt,class DistanceAt,class Accept>
Result try_quiet_dash(int known_slot,bool walked,const MaskStamp& mask_stamp,
 const cambc::bitboard& transit_blocked,Scratch& scratch,Guard&& guard,Revision&& revision,
 FloorFree&& floor_free,SupportAt&& support_at,DistanceAt&& loose_distance,Accept&& accept) {
 Result r;
 if(!guard()){r.status=Outcome::deferred;return r;}
 if(revision()!=mask_stamp.revision||!actual_owner(mask_stamp)){r.status=Outcome::stale;return r;}
 auto captured=sdk_detail::capture_slot(known_slot,walked,true,guard);
 if(captured.status!=Status::ready){r.status=outcome(captured.status);return r;}
 const auto s=*captured.value;r.before=s;
 if(s.body.epoch!=mask_stamp.epoch||s.body.actor!=mask_stamp.actor||
    revision()!=mask_stamp.revision){r.status=Outcome::stale;return r;}
 const auto origin=sdk_detail::native(s.body.pos);
 transit_blocked.reachable_tiles_into(origin,std::uint16_t(s.dash.steps),
  scratch.frontier,scratch.work,scratch.reachable);
 if(!guard()){r.status=Outcome::deferred;return r;}
 if(revision()!=mask_stamp.revision||!actual_owner(mask_stamp)){r.status=Outcome::stale;return r;}
 const auto count=scratch.reachable.positions_into(scratch.positions);
 std::size_t used=0;
 // At range <=4 the entire Chebyshev square, including origin, has <=81
 // positions. Therefore positions_into cannot truncate a legal menu.
 for(std::size_t i=0;i<count;++i) {
  const Point p{scratch.positions[i].x,scratch.positions[i].y};
  if(p==s.body.pos||!floor_free(p))continue;
  const auto support=support_at(p);
  if(!support.valid||support.amount!=0)continue;
  scratch.endpoints[used++]={p,loose_distance(p),support,true};
 }
 const auto best=rank_quiet(s.body.pos,loose_distance(s.body.pos),
  std::span<const Endpoint>(scratch.endpoints.data(),used));
 if(!guard()){r.status=Outcome::deferred;return r;}
 if(revision()!=mask_stamp.revision||!actual_owner(mask_stamp)){r.status=Outcome::stale;return r;}
 if(!best)return r;
 r.nominated=true;r.choice=best;
 if(!accept(*best)){r.status=Outcome::declined;return r;}
 // Initial cooldown/props/buffs remain authoritative ONLY because no command
 // intervened and the owner epoch/revision are unchanged. One final fresh body
 // also catches changed pose/MAC/channel/stun before using the retained slot.
 const auto final=sdk_detail::read_body(guard);
 if(!final){r.status=Outcome::deferred;return r;}
 if(!same_context(s.body,*final)||revision()!=mask_stamp.revision||
    !guard()||!actual_owner(mask_stamp)){r.status=Outcome::stale;return r;}
 r.attempted=true;r.requires_refresh=true;
 r.applied=cambc::self().abilities()[std::size_t(s.dash.slot)].use(sdk_detail::native(best->pos));
 r.status=r.applied?Outcome::applied:Outcome::refused;
 return r;
}
} // namespace mosaic::dash_word::lean
