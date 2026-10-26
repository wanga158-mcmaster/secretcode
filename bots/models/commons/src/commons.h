#pragma once
#include "../../common/pure.h"

namespace plan4::commons {
inline constexpr const char* version="plan4-commons/2";
inline constexpr int event_limit=8;
struct PolicySeal { std::uint64_t identity=0; bool inspectable=false,frozen=false,matches_rule=false; };
struct Priority {
    std::array<int,max_groups> groups{},ids{};
    int count=0;
    PolicySeal policy{};
    Point public_spawn{};
    bool frozen=false;
};
inline bool known_policy(const PolicySeal& policy){return policy.identity&&policy.inspectable&&policy.frozen&&policy.matches_rule;}
// Both the model and actual fixture use this frozen public ordering. It must be
// built from a supported public spawn anchor, never a hidden individual start.
inline bool freeze_priority(const Information& info,Point public_spawn,bool spawn_supported,PolicySeal policy,Priority& out){
    out.frozen=false;out.count=0;
    if(!spawn_supported||!known_policy(policy)||info.public_layout_truncated||info.group_count<0||info.group_count>max_groups)return false;
    std::array<int,max_groups> distances;
    for(int g=0;g<info.group_count;++g){
        if(!can_refine(info))return false;
        if(info.groups[g].id<0)return false;
        for(int p=0;p<g;++p)if(info.groups[p].id==info.groups[g].id)return false;
        distances[g]=distance(info,public_spawn,info.groups[g].position);if(distances[g]<0)return false;
        int place=out.count;
        while(place>0&&(distances[out.groups[place-1]]>distances[g]||
            (distances[out.groups[place-1]]==distances[g]&&info.groups[out.groups[place-1]].id>info.groups[g].id))){
            out.groups[place]=out.groups[place-1];out.ids[place]=out.ids[place-1];--place;
        }
        out.groups[place]=g;out.ids[place]=info.groups[g].id;++out.count;
    }
    if(!can_refine(info))return false;
    out.policy=policy;out.public_spawn=public_spawn;out.frozen=true;return true;
}
struct Stock {
    int ready_round=-1,generation=0;
    bool supported=false,regenerated=false;
};
struct Choice {int group=-1,next_cursor=0;};
inline Choice fixture_next(const Priority& priority,int cursor,const Stock* stock,int count,int now){
    if(!priority.frozen||!known_policy(priority.policy)||priority.count<0||priority.count>max_groups
        ||cursor<0||count<0||count>max_groups||(count&&!stock))return{};
    for(int offset=0;offset<priority.count;++offset){
        const int slot=(cursor+offset)%priority.count,g=priority.groups[slot];
        if(g>=0&&g<count&&stock[g].supported&&stock[g].ready_round>=0&&stock[g].ready_round<=now)
            return{g,(slot+1)%priority.count};
    }
    return{};
}
inline int fixture_altar(const Information& info,Point from,TravelLookup perspective={}){
    int chosen=-1,best=std::numeric_limits<int>::max();
    for(int a=0;a<std::min(info.altar_count,max_altars);++a)if(info.altars[a].accessible){
        if(!can_refine(info))return-1;
        const auto lookup=perspective.distance?perspective:info.travel;
        const int d=lookup.distance?lookup.distance(from,info.altars[a].position,lookup.context):-1;if(d<0)return-1;
        if(!can_refine(info))return-1;
        if(d<best||(d==best&&(chosen<0||point_less(info.altars[a].altar_position,info.altars[chosen].altar_position)))){chosen=a;best=d;}
    }
    return chosen;
}
} // namespace plan4::commons
#include "commons-process.h"
