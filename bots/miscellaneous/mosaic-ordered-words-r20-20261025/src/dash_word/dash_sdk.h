#pragma once
// Generic one-attempt dash. The guard must fence actual epoch/actor and a
// measured fuel tail. Menus nominate under current known occupancy; hidden
// occupants may refuse. No future-path, health or fuel guarantee is inferred.
#include <cambc.h>
#include <cmath>
#include <optional>
#include <utility>
#include "dash_contract.h"
namespace mosaic::dash_word {
struct Capture {Status status{Status::no_dash};std::optional<Snapshot> value;};
struct Menu {Status status{Status::no_candidate};Snapshot snapshot;std::optional<cambc::bitboard> destinations;};
struct Attempt {
 Status status{Status::unavailable};bool attempted{},applied{},requires_refresh{};
 Snapshot before;std::optional<Body> after;int cooldown_after{-1};
};
namespace sdk_detail {
inline cambc::vec2 native(Point p){return {p.x,p.y};}
template<class Guard>std::optional<Body> read_body(Guard&& guard) {
 if(!guard())return {};
 const int epoch=int(cambc::round_number());
 const auto actor=std::uint32_t(cambc::handle_champion_t(cambc::self()));
 cambc::detail::champion_info q{};cambc::detail::check(cambc::detail::sdk_champion(actor,&q));
 if(!guard())return {};
 const auto s=cambc::champion_state::from_bits_retain(std::uint32_t(q.state));
 Body b;b.epoch=epoch;b.actor=actor;b.pos={q.x,q.y};b.alive=!s.is_dead();
 b.can_move=s.contains(cambc::champion_state::can_move);
 b.can_attack=s.contains(cambc::champion_state::can_attack);
 b.can_cast=s.contains(cambc::champion_state::can_cast);
 b.channelled=s.contains(cambc::champion_state::recalling)||
  s.contains(cambc::champion_state::submitting_gems)||s.contains(cambc::champion_state::teleporting);
 b.completed=s.contains(cambc::champion_state::completed_turn);
 if(b.alive&&b.can_cast)b.stunned=cambc::self().buffs().has_stun();
 if(!guard()||int(cambc::round_number())!=epoch||
    std::uint32_t(cambc::handle_champion_t(cambc::self()))!=actor)return {};
 return b;
}
template<class Guard>Capture capture_slot(int slot,bool walked,bool pure_only,Guard&& guard) {
 Capture r;
 if(slot<0||slot>1){r.status=Status::unsupported;return r;}
 const auto b=read_body(guard);if(!b){r.status=Status::deferred;return r;}
 if(!b->alive||!b->can_cast||b->stunned||b->channelled||b->completed){r.status=Status::unavailable;return r;}
 const auto a=cambc::self().abilities()[std::size_t(slot)];
 if(!a.is_dash())return r;
 const auto info=a.get<cambc::ability_dash>();
 if(!std::isfinite(info.range)||info.range<1||info.range>=float(max_steps+1)||
    !std::isfinite(info.radius)||info.radius<0||!std::isfinite(info.modifier)||info.modifier<0||info.duration<0) {
  r.status=Status::unsupported;return r;
 }
 if(!guard()){r.status=Status::deferred;return r;}
 Dash d;d.slot=slot;d.steps=int(std::floor(info.range));d.exclusive=info.exclusive_with_movement;
 d.radius=info.radius;d.modifier=info.modifier;d.stun_duration=info.duration;
 d.pure_movement=info.modifier==0&&info.duration==0;d.cooldown=a.cooldown();
 d.ground=a.valid_targets().contains(cambc::ability_target_mask::ground);
 if(!guard()||int(cambc::round_number())!=b->epoch||
    std::uint32_t(cambc::handle_champion_t(cambc::self()))!=b->actor){r.status=Status::stale;return r;}
 r.status=admission(*b,d,walked,pure_only);if(r.status==Status::ready)r.value=Snapshot{*b,d};return r;
}
} // namespace sdk_detail
// M and A are independent of dash admission: a fresh capture after ranged Basic
// may be ready with MAC=001. Ordinary walked history is never mutated here.
template<class Guard>Capture capture(bool walked,Guard&& guard,bool pure_only=true) {
 Capture last;
 for(int slot=0;slot<2;++slot) {
  auto r=sdk_detail::capture_slot(slot,walked,pure_only,guard);
  if(r.status==Status::ready||r.status==Status::deferred||r.status==Status::stale)return r;
  if(r.status!=Status::no_dash)last=std::move(r);
 }
 return last;
}
// Immutable terrain inputs: transit_static=walls|altars|enemy_spawn, with holes
// omitted; landing_static=transit_static|holes. Fresh observed units and teleport
// reservations are added internally. Only actual self is removed.
template<class Guard>Menu nominate(const Snapshot& s,const cambc::bitboard& transit_static,
 const cambc::bitboard& landing_static,bool walked,Guard&& guard,bool pure_only=true) {
 Menu r;r.snapshot=s;
 auto now=sdk_detail::capture_slot(s.dash.slot,walked,pure_only,guard);
 if(now.status!=Status::ready){r.status=now.status;return r;}
 if(!same_context(s.body,now.value->body)||!same_dash(s.dash,now.value->dash)){r.status=Status::stale;return r;}
 if(!guard()){r.status=Status::deferred;return r;}
 auto occupied=cambc::vision::champion_tiles();
 if(!guard()){r.status=Status::deferred;return r;}occupied|=cambc::vision::camp_tiles();
 if(!guard()){r.status=Status::deferred;return r;}occupied|=cambc::vision::teleport_tiles();
 if(!guard()){r.status=Status::deferred;return r;}
 auto transit=transit_static|occupied;auto landing=landing_static|occupied;
 const auto origin=sdk_detail::native(now.value->body.pos);transit.set(origin,false);landing.set(origin,false);
 if(!guard()){r.status=Status::deferred;return r;}
 auto points=transit.reachable_tiles(origin,std::uint16_t(now.value->dash.steps));
 if(!guard()){r.status=Status::deferred;return r;}points&=~landing;points.set(origin,false);
 auto end=sdk_detail::capture_slot(s.dash.slot,walked,pure_only,guard);
 if(end.status!=Status::ready){r.status=end.status;return r;}
 if(!same_context(s.body,end.value->body)||!same_dash(s.dash,end.value->dash)){r.status=Status::stale;return r;}
 if(!guard()){r.status=Status::deferred;return r;}
 const bool empty=points.is_empty();if(!guard()){r.status=Status::deferred;return r;}
 r.status=empty?Status::no_candidate:Status::ready;if(!empty)r.destinations=std::move(points);return r;
}
// Rebuild a fresh known-path menu and recheck current self/slot before ONE cast.
// A false result may reveal occupants and MUST invalidate dependent facts too.
template<class Guard>Attempt attempt(const Snapshot& s,Point destination,bool walked,
 const cambc::bitboard& transit_static,const cambc::bitboard& landing_static,Guard&& guard,bool pure_only=true) {
 Attempt r;r.before=s;
 auto menu=nominate(s,transit_static,landing_static,walked,guard,pure_only);
 if(menu.status!=Status::ready){r.status=menu.status;return r;}
 if(!guard()){r.status=Status::deferred;return r;}
 const auto p=sdk_detail::native(destination);
 if(destination.x<0||destination.y<0||destination.x>=menu.destinations->width()||
    destination.y>=menu.destinations->height()||!menu.destinations->is_set(p)){r.status=Status::no_candidate;return r;}
 auto now=sdk_detail::capture_slot(s.dash.slot,walked,pure_only,guard);
 if(now.status!=Status::ready){r.status=now.status;return r;}
 if(!same_context(s.body,now.value->body)||!same_dash(s.dash,now.value->dash)){r.status=Status::stale;return r;}
 if(!guard()){r.status=Status::deferred;return r;}
 r.attempted=true;r.requires_refresh=true;
 r.applied=cambc::self().abilities()[std::size_t(s.dash.slot)].use(p);
 r.status=r.applied?Status::applied:Status::refused;
 r.after=sdk_detail::read_body(guard);
 if(r.after) {
  if(r.after->epoch!=s.body.epoch||r.after->actor!=s.body.actor)r.status=Status::stale_after_attempt;
  else if(guard())r.cooldown_after=cambc::self().abilities()[std::size_t(s.dash.slot)].cooldown();
 }
 return r;
}
} // namespace mosaic::dash_word
