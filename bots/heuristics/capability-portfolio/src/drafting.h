#pragma once
#include <cambc.h>
#include <cstdio>
#include "portfolio.h"
#include "../../common/src/draft.h"

namespace h12_capability_portfolio {
inline bool geometry_budget(){return cambc::fuel_remaining()>=140000;}
struct DraftFuel {std::uint64_t start=0,masks=0,metadata=0,before=0,after=0;};
inline DraftFuel draft_fuel;

inline Plan native_summary(const Grid&g,const std::vector<Group>&groups,Point start,Capability capacity){
    Plan fallback;
    const int stride=g.width+2,padded=stride*(g.height+2);
    auto cell=[&](Point p){return (p.y+1)*stride+p.x+1;};
    auto point=[&](int k){return Point{k%stride-1,k/stride-1};};
    const std::array<int,8> neighbor{-stride-1,-stride,-stride+1,-1,1,stride-1,stride,stride+1};
    if(cambc::fuel_remaining()<240000)return fallback;
    const auto walls=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(cambc::team::enemy);
    cambc::bitboard roots,home_visited;roots.draw_chebyshev({start.x,start.y},0);
    cambc::distance_field home_field;home_field.compute(walls,roots,home_visited);
    const auto home_values=home_field.export_linear_buffer();
    std::vector<std::uint8_t> flags(padded,Wall);
    std::vector<int> component(padded,-2),from_home(padded,-1),altar_distance(padded,-1),altar_owner(padded,-1);
    std::vector<int> holes,altars,home_altars;
    for(int y=0;y<g.height;++y){
        if(!geometry_budget())return fallback;
        for(int x=0;x<g.width;++x){
            const int raw=y*g.width+x,k=(y+1)*stride+x+1;const auto f=g.flags[raw];flags[k]=f;
            if(!(f&(Wall|Hole|EnemySpawn|Altar))){
                if(home_values[raw]!=cambc::distance_field::unreachable){component[k]=0;from_home[k]=home_values[raw];}
                else component[k]=-1;
            }
            if(f&Hole)holes.push_back(k);
            if(f&Altar)altars.push_back(k);
        }
    }
    cambc::bitboard altar_roots;
    for(int a=0;a<int(altars.size());++a){
        bool usable=false;
        for(const int offset:neighbor)if(component[altars[a]+offset]==0){usable=true;break;}
        if(!usable)continue;
        home_altars.push_back(a);const Point p=point(altars[a]);
        cambc::bitboard approach;approach.draw_chebyshev({p.x,p.y},1);altar_roots|=approach;
    }
    if(home_altars.empty()||cambc::fuel_remaining()<220000)return fallback;
    altar_roots&=home_visited&~walls;
    cambc::distance_field altar_field;cambc::bitboard altar_visited;
    altar_field.compute(walls,altar_roots,altar_visited,10);
    const auto altar_values=altar_field.export_linear_buffer();
    for(int y=0;y<g.height;++y){
        if(!geometry_budget())return fallback;
        for(int x=0;x<g.width;++x){
            const auto value=altar_values[y*g.width+x];
            if(value!=cambc::distance_field::unreachable)altar_distance[(y+1)*stride+x+1]=value;
        }
    }
    // A native multi-source field stores distances, not root identities. For
    // each possible production pad, follow at most ten local gradient steps
    // to bind the actual altar that generated its short bank path.
    for(const auto&s:groups)if(!s.pads.empty()){
        if(!geometry_budget())return fallback;
        const int origin=cell(s.pads.front());int k=origin;
        if(component[k]!=0||altar_distance[k]<0||altar_distance[k]>10)continue;
        for(int step=0;step<10&&altar_distance[k]>0;++step){
            int next=k;for(int offset:neighbor)if(component[k+offset]==0&&altar_distance[k+offset]==altar_distance[k]-1){next=k+offset;break;}
            if(next==k)break;k=next;
        }
        if(altar_distance[k]!=0)continue;
        for(int a:home_altars)if(separation(point(k),point(altars[a]))<=1){altar_owner[origin]=a;break;}
    }
    // Admit at most one complete remote certificate. Candidate discovery is
    // a bounded local byte scan around affordable remote pads, never another
    // guest flood or a native field for every source.
    int landing=-1,inspected=0;
    for(const auto&s:groups){
        if(++inspected>256||!geometry_budget())break;
        if(s.pads.empty()||(s.gems<4&&s.gold<capacity.shard_price)||capacity.strength<=0)continue;
        const int work=std::max(1,int(std::ceil(s.hp/capacity.strength)))+int(s.pads.size());
        if(work>25||s.strength*std::min(work,4)>capacity.health*.5f)continue;
        bool remote=true;for(Point p:s.pads)if(component[cell(p)]!=-1){remote=false;break;}
        if(!remote)continue;
        for(Point pad:s.pads){
            const int x0=std::max(0,pad.x-9),x1=std::min(g.width-1,pad.x+9);
            const int y0=std::max(0,pad.y-9),y1=std::min(g.height-1,pad.y+9);
            for(int y=y0;y<=y1&&landing<0;++y){
                if(!geometry_budget())break;
                for(int x=x0;x<=x1&&landing<0;++x){
                    const int middle=(y+1)*stride+x+1;
                    if(!(flags[middle]&Hole)||(flags[middle]&(Wall|EnemySpawn|Altar)))continue;
                    for(int offset:neighbor){
                        const int entry=middle-offset,land=middle+offset;
                        if(component[entry]==0&&component[land]==-1&&!(flags[entry]&Camp)&&!(flags[land]&Camp)
                            &&separation(point(land),pad)<=8){landing=land;break;}
                    }
                }
            }
            if(landing>=0)break;
        }
        if(landing>=0)break;
    }
    int components=1;
    if(landing>=0&&cambc::fuel_remaining()>=220000){
        const auto p=point(landing);cambc::bitboard root,reached;root.draw_chebyshev({p.x,p.y},0);
        cambc::distance_field remote_field;remote_field.compute(walls,root,reached);
        const auto values=remote_field.export_linear_buffer();
        for(int y=0;y<g.height;++y){
            if(!geometry_budget())return fallback;
            for(int x=0;x<g.width;++x){
                const int raw=y*g.width+x,k=(y+1)*stride+x+1;
                if(component[k]==-1&&values[raw]!=cambc::distance_field::unreachable)component[k]=1;
            }
        }
        components=2;
    }
    if(!geometry_budget())return fallback;
    return choose_summary(g,groups,{stride,components,flags,component,from_home,altar_distance,altar_owner,holes,altars,home_altars},geometry_budget,capacity);
}

inline Plan public_plan(){
    draft_fuel.start=cambc::fuel_remaining();
    Plan fallback;
    const int width=cambc::map::width(),height=cambc::map::height();
    if(width<=0||height<=0||width*height>10000||!geometry_budget())return fallback;
    Grid grid{width,height,std::vector<std::uint8_t>(std::size_t(width*height),0)};
    // A mask is exported exactly once. Hot geometry tests use local bytes,
    // never a host-backed is_set call per cell or neighbor.
    auto mark=[&](const cambc::bitboard&mask,std::uint8_t flag){
        for(const auto p:mask.positions())grid.flags[grid.index({p.x,p.y})]|=flag;
    };
    mark(cambc::map::walls(),Wall);
    mark(cambc::map::holes(),Hole);
    mark(cambc::map::spawn_region(cambc::team::enemy),EnemySpawn);
    mark(cambc::map::altars(),Altar);
    const auto homes=cambc::map::spawn_region(cambc::team::ally).positions();
    if(homes.empty()||!geometry_budget())return fallback;
    for(const auto p:homes)grid.flags[grid.index({p.x,p.y})]|=AllySpawn;
    draft_fuel.masks=cambc::fuel_remaining();

    // camp_groups() queries its host every time. Cache one complete public
    // result for the draft instance; design stats are cached by physical kind.
    static const auto public_groups=cambc::map::camp_groups();
    std::array<cambc::camp_stats,6> designs{};
    std::array<bool,6> loaded{};
    std::vector<Group> groups;groups.reserve(public_groups.size());
    for(const auto&s:public_groups){
        if(!geometry_budget())return fallback;
        Group group;
        group.id=int(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(s)));
        group.gems=int(s.gem_value());group.gold=int(s.gold_value());
        group.pads.reserve(s.camps().size());
        int members=0;
        for(const auto&member:s.camps()){
            if((members++%64)==0&&!geometry_budget())return fallback;
            const auto p=member.spawn_position();const int kind=int(member.kind());
            if(kind<0||kind>=int(designs.size()))return fallback;
            if(!loaded[kind]){designs[kind]=member.base_stats();loaded[kind]=true;}
            group.pads.push_back({p.x,p.y});grid.flags[grid.index({p.x,p.y})]|=Camp;
            group.hp+=designs[kind].base_health;group.strength+=designs[kind].base_strength;
        }
        groups.push_back(std::move(group));
    }
    const auto vesper=cambc::champion_designs[1].base_stats();
    const auto tavi=cambc::champion_designs[11].base_stats();
    Capability capacity;
    capacity.health=vesper.base_health;capacity.strength=vesper.base_strength;
    capacity.range=vesper.base_attack_range;
    capacity.tavi_health=tavi.base_health;capacity.tavi_strength=tavi.base_strength;
    capacity.shard_price=int(cambc::item_type::monster_shard_of_focus.cost());
    draft_fuel.metadata=cambc::fuel_remaining();
    if(!geometry_budget())return fallback;
    draft_fuel.before=cambc::fuel_remaining();
    auto plan=native_summary(grid,groups,{homes.front().x,homes.front().y},capacity);
    draft_fuel.after=cambc::fuel_remaining();return plan;
}

inline int suite_index(cambc::champion_draft choice){
    for(int i=0;i<int(heuristics_draft::choices.size());++i)if(heuristics_draft::choices[i]==choice)return i;
    return -1;
}
inline int physical_identity(int suite){
    constexpr std::array<int,12> identities{11,10,2,1,7,6,12,8,5,0,9,3};
    return suite>=0&&suite<int(identities.size())?identities[suite]:-1;
}

inline void draft(){
    static bool initialized=false;
    static Plan plan;
    const auto selected=cambc::draft::current_draft(cambc::team::ally);
    const int expected=std::clamp(int(cambc::draft::team_size()),3,5);
    if(!initialized){
        // The SDK draft_memory object is captured startup data. Keep our own
        // local plan after this single optional initial read.
        bool restored=false;
        if(!selected.empty()&&cambc::draft_memory.size()==1024){
            Envelope bytes{};
            for(std::size_t i=0;i<bytes.size();++i)bytes[i]=std::to_integer<std::uint8_t>(cambc::draft_memory[i]);
            restored=decode(bytes,plan);
        }
        if(!restored)plan=selected.empty()?public_plan():ordinary_plan(expected);
        plan.expected_picks=expected;initialized=true;
    }
    bool prefix=selected.size()<=std::size_t(expected);
    for(std::size_t i=0;i<selected.size()&&i<5;++i)prefix&=suite_index(selected[i])==plan.picks[i];
    if(!prefix){
        // An interrupted or externally replaced draft admits no missing role.
        // Preserve actual allied choices and append unique ordinary workers.
        Plan repaired=ordinary_plan(expected);int at=0;
        for(auto choice:selected)if(at<5){repaired.picks[at]=suite_index(choice);repaired.identities[at]=physical_identity(repaired.picks[at]);++at;}
        const auto fallback=ordinary_plan(expected);
        for(int candidate:fallback.picks){
            if(at>=5)break;bool used=false;for(int i=0;i<at;++i)used|=repaired.picks[i]==candidate;
            if(!used){repaired.picks[at]=candidate;repaired.identities[at]=physical_identity(candidate);++at;}
        }
        plan=repaired;
    }
    if(selected.size()>=std::size_t(expected))return;
    const int next=plan.picks[selected.size()];
    // Submit a full zero-filled envelope before the pick. No memory action
    // depends on whether the last pick immediately closes the draft phase.
    // The count includes the exact choice submitted immediately below; live
    // readers additionally bind these identities to their actual roster.
    plan.pick_count=int(selected.size())+1;
    if(selected.empty()&&cambc::fuel_remaining()>30000)
        std::printf("portfolio draft package=%d source=%d count=%d expected=%d start=%llu masks=%llu metadata=%llu before=%llu after=%llu left=%llu\n",
            int(plan.package),plan.witness.group_id,plan.pick_count,plan.expected_picks,
            static_cast<unsigned long long>(draft_fuel.start),static_cast<unsigned long long>(draft_fuel.masks),
            static_cast<unsigned long long>(draft_fuel.metadata),static_cast<unsigned long long>(draft_fuel.before),
            static_cast<unsigned long long>(draft_fuel.after),static_cast<unsigned long long>(cambc::fuel_remaining()));
    cambc::draft_memory=encode(plan);
    cambc::draft::pick(heuristics_draft::choices[next]);
}
} // namespace h12_capability_portfolio
