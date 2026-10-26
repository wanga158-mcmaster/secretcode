#pragma once
#include <cambc.h>
#include <optional>
#include <vector>
#include <cmath>
namespace wb::capital::work_bridge {
struct Offer { std::optional<cambc::camp> target; int owner=-1,round=-1,group=-1; cambc::vec2 position{}; float observed_hp=0; bool fresh=false; };
struct Host { void* context=nullptr; Offer (*nominate)(void*)=nullptr; bool (*npc_quiet)(void*,cambc::vec2,int)=nullptr; void (*commit)(void*,const Offer&,float,float)=nullptr; };
struct Entry {cambc::camp target;int group,member;cambc::vec2 position;};
struct Registry {Host host{};int round=-1,width=0,height=0,preferred=-1;unsigned calls=0,cursor=0;std::vector<Entry> entries;std::vector<int> cells;};
inline Registry& registry(){static Registry value;return value;}
inline void bind(Host host){registry().host=host;}
inline void clear_map(int width,int height) {auto& r=registry();r.entries.clear();r.cursor=0;r.preferred=-1;r.width=width;r.height=height;if(width>0&&height>0&&width*height<=10000)r.cells.assign(width*height,-1);}
inline void add(cambc::camp camp,int group,int member,cambc::vec2 p) {auto& r=registry();if(p.x<0||p.y<0||p.x>=r.width||p.y>=r.height||r.cells.empty())return;r.cells[p.y*r.width+p.x]=static_cast<int>(r.entries.size());r.entries.push_back({camp,group,member,p});}
inline Offer nominate() {auto& r=registry();const int now=cambc::round_number();if(r.round!=now){r.round=now;r.calls=0;}if(!r.host.nominate||r.calls>=2)return {};++r.calls;auto o=r.host.nominate(r.host.context);if(o.owner!=static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()))||o.round!=static_cast<int>(cambc::round_number()))o.fresh=false;return o;}
inline bool npc_quiet(cambc::vec2 p,int killed=-1) {const auto& r=registry();return r.host.npc_quiet&&r.host.npc_quiet(r.host.context,p,killed);}
inline void commit(const Offer& o,float before,float after) {const auto& r=registry();if(!o.fresh||!o.target||!r.host.commit||!std::isfinite(before)||!std::isfinite(after)||before<=after||after<0||o.round!=static_cast<int>(cambc::round_number()))return;r.host.commit(r.host.context,o,before,after);}
}
