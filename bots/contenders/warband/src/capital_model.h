#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace wb::capital {
// Bounded abstract work ranking only. Access to every member is assumed;
// actual movement/allowances, outside camps, pressure and complete economic
// suffixes must be priced by the runtime. No helper tanking/heal/shield is borrowed.
inline constexpr std::size_t max_camps=30;
inline constexpr unsigned max_work_rounds=128;
struct Camp { int id{-1},x{},y{};float hp{},strength{};int range{};bool alive{true}; };
struct Body {
    int id{-1};float hp{},max_hp{},strength{};
    float camp_damage_multiplier{1},incoming_multiplier{1};
    int basic_splash{};
    unsigned first_hit_delay{},entry_intervals{},exit_intervals{};
};
struct WorkEstimate {
    bool supported{},cleared{},needs_recovery{},geometry_modelled{},survives{};
    unsigned rounds{},first_target_round{},basic_hits{},damaged_members{};
    int first_target{-1};
    float hp_cost{},exit_cost{},hp_after{},remaining_hp{};
    std::array<float,max_camps> residual_hp{};
};
struct PartyEstimate {
    bool supported{},cleared{},needs_recovery{},geometry_modelled{},survives{};
    unsigned rounds{};std::size_t bodies{};
    std::array<WorkEstimate,2> body{};
    std::array<float,max_camps> residual_hp{};float remaining_hp{};
};
namespace detail {
inline bool nonnegative(float x){return std::isfinite(x)&&x>=0;}
inline bool valid(const Body& b) {
    return b.id>=0&&std::isfinite(b.hp)&&b.hp>0&&std::isfinite(b.max_hp)&&b.hp<=b.max_hp
      &&std::isfinite(b.strength)&&b.strength>0&&std::isfinite(b.camp_damage_multiplier)
      &&b.camp_damage_multiplier>0&&std::isfinite(b.incoming_multiplier)&&b.incoming_multiplier>0
      &&b.incoming_multiplier<=1&&b.basic_splash>=0&&b.basic_splash<=1
      &&b.first_hit_delay<=max_work_rounds&&b.entry_intervals<=max_work_rounds
      &&b.exit_intervals<=max_work_rounds&&std::isfinite(b.strength*b.camp_damage_multiplier);
}
inline bool valid(std::span<const Camp> c) {
    if(c.empty()||c.size()>max_camps)return false;
    for(std::size_t i=0;i<c.size();++i){
      if(c[i].id<0||!nonnegative(c[i].hp)||!nonnegative(c[i].strength)||c[i].range<0
        ||c[i].range>100||(c[i].alive&&!(c[i].hp>0)))return false;
      for(std::size_t j=0;j<i;++j)if(c[i].id==c[j].id)return false;
    }return true;
}
inline int distance(const Camp& a,const Camp& b) {
    auto x=std::int64_t(a.x)-b.x,y=std::int64_t(a.y)-b.y;
    return int(std::min<std::int64_t>(std::numeric_limits<int>::max(),std::max(x<0?-x:x,y<0?-y:y)));
}
inline float sum(std::span<const Camp> c,const std::array<float,max_camps>& hp,bool threat) {
    float n=0;for(std::size_t i=0;i<c.size();++i)if(hp[i]>0)n+=threat?c[i].strength:hp[i];return n;
}
inline int target(std::span<const Camp> c,const std::array<float,max_camps>& hp) {
    int best=-1;double rank=-1;
    for(std::size_t i=0;i<c.size();++i)if(hp[i]>0){
      double r=double(c[i].strength)/hp[i];
      if(best<0||r>rank){best=int(i);rank=r;}
    }
    return best;
}
inline void retreat(WorkEstimate& r,const Body& w,float threat) {
    const float hit=threat*w.incoming_multiplier;
    for(unsigned i=0;i<w.exit_intervals;++i){r.hp_after-=hit;r.exit_cost+=hit;}
}
}
inline PartyEstimate estimate_party(std::span<const Camp> c,std::span<const Body> workers,
                                    unsigned horizon=max_work_rounds) {
    PartyEstimate out;
    if(!detail::valid(c)||workers.empty()||workers.size()>2||!horizon||horizon>max_work_rounds)return out;
    for(std::size_t b=0;b<workers.size();++b){
      if(!detail::valid(workers[b]))return out;
      for(std::size_t j=0;j<b;++j)if(workers[b].id==workers[j].id)return out;
    }
    for(std::size_t i=0;i<c.size();++i)out.residual_hp[i]=c[i].alive?c[i].hp:0;
    float initial=detail::sum(c,out.residual_hp,true);
    if(!std::isfinite(initial)||!std::isfinite(detail::sum(c,out.residual_hp,false)))return out;
    for(const auto& w:workers)if(!std::isfinite(initial*w.incoming_multiplier*float(1+w.entry_intervals+w.exit_intervals)))return out;
    out.supported=true;out.bodies=workers.size();
    std::array<bool,2> stopped{};
    for(std::size_t b=0;b<workers.size();++b){
      auto& r=out.body[b];const auto& w=workers[b];r.supported=true;r.hp_after=w.hp;
      float hit=initial*w.incoming_multiplier;
      for(unsigned i=0;i<w.entry_intervals;++i){r.hp_after-=hit;r.hp_cost+=hit;}
      if(r.hp_after<=0){stopped[b]=true;r.needs_recovery=true;}
    }
    for(unsigned round=0;round<horizon&&detail::sum(c,out.residual_hp,false)>0;++round){
      float threat=detail::sum(c,out.residual_hp,true);bool any=false;
      // Charge all living selected camps to EACH present body BEFORE both hits.
      // This is deliberately conservative about nearest-target sharing.
      for(std::size_t b=0;b<workers.size();++b){
        auto& r=out.body[b];const auto& w=workers[b];if(stopped[b]||round<w.first_hit_delay)continue;
        float hit=threat*w.incoming_multiplier,reserve=hit*float(w.exit_intervals);
        if(r.hp_after-hit-reserve<=0){detail::retreat(r,w,threat);stopped[b]=true;r.needs_recovery=true;continue;}
        r.hp_after-=hit;r.hp_cost+=hit;any=true;
      }
      for(std::size_t b=0;b<workers.size();++b){
        auto& r=out.body[b];const auto& w=workers[b];if(stopped[b]||round<w.first_hit_delay)continue;
        int target=detail::target(c,out.residual_hp);if(target<0)break;
        if(r.first_target<0){r.first_target=target;r.first_target_round=round;}++r.basic_hits;
        float damage=w.strength*w.camp_damage_multiplier;
        for(std::size_t i=0;i<c.size();++i)if(out.residual_hp[i]>0&&(i==std::size_t(target)
          ||(w.basic_splash&&detail::distance(c[i],c[target])<=w.basic_splash))){
          float before=out.residual_hp[i];
          out.residual_hp[i]=std::max(0.0f,before-damage);
          if(out.residual_hp[i]<before)++r.damaged_members;
        }
      }
      out.rounds=round+1;
      if(!any){bool later=false;for(std::size_t b=0;b<workers.size();++b)later|=!stopped[b]&&round<workers[b].first_hit_delay;if(!later)break;}
    }
    out.remaining_hp=detail::sum(c,out.residual_hp,false);out.cleared=out.remaining_hp==0;out.survives=true;
    for(std::size_t b=0;b<workers.size();++b){
      auto& r=out.body[b];const auto& w=workers[b];
      if(!stopped[b])detail::retreat(r,w,detail::sum(c,out.residual_hp,true));
      r.rounds=out.rounds;r.cleared=out.cleared;r.residual_hp=out.residual_hp;r.remaining_hp=out.remaining_hp;
      r.survives=std::isfinite(r.hp_after)&&r.hp_after>0;
      r.needs_recovery|=!out.cleared||r.hp_after<w.max_hp;
      out.survives&=r.survives;out.needs_recovery|=r.needs_recovery;
    }return out;
}
inline WorkEstimate estimate(std::span<const Camp> c,const Body& w,unsigned horizon=max_work_rounds) {
    return estimate_party(c,std::span<const Body>(&w,1),horizon).body[0];
}
// These supplied values include approach/work/capture/receipt/conversion/
// recovery/renewal. Unobserved independent alternatives cannot prove a benefit.
struct CompleteAlternative {int body{-1};unsigned horizon{};bool complete_observed{};double converted_value{};};
struct Benefit {bool comparable{},cooperation_better{};double party_value{},independent_value{},difference{};};
inline Benefit compare_complete(int first,int second,unsigned horizon,double value,bool complete,
                                 const CompleteAlternative& a,const CompleteAlternative& b) {
    Benefit out;
    if(first<0||second<0||first==second||!horizon||!complete||!a.complete_observed||!b.complete_observed
      ||a.body!=first||b.body!=second||a.horizon!=horizon||b.horizon!=horizon||!std::isfinite(value)
      ||!std::isfinite(a.converted_value)||!std::isfinite(b.converted_value)||value<0
      ||a.converted_value<0||b.converted_value<0||!std::isfinite(a.converted_value+b.converted_value))return out;
    out.comparable=true;out.party_value=value;out.independent_value=a.converted_value+b.converted_value;
    out.difference=value-out.independent_value;out.cooperation_better=out.difference>0;return out;
}
}
