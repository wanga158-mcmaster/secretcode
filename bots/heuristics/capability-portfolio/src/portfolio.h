#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace h12_capability_portfolio {
enum class Package : std::uint8_t { Ordinary, Access, Supply };
enum class Role : std::uint8_t { Ordinary, Access, Supply };
struct Point { int x=0,y=0; friend bool operator==(Point,Point)=default; };
inline int separation(Point a,Point b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
struct Witness {
    int group_id=-1;
    Point entry{},landing{},pad{},altar{};
    int detour=0,work=0,gems=0,gold=0;
};
struct Plan {
    Package package=Package::Ordinary;
    std::array<int,5> picks{3,2,4,1,7};
    std::array<int,5> identities{1,2,7,10,8};
    std::array<Role,5> roles{};
    int pick_count=0,expected_picks=3;
    Witness witness{};
};
inline Plan ordinary_plan(int expected=3){Plan p;p.expected_picks=std::clamp(expected,3,5);return p;}
inline Role role_for(const Plan&p,int identity){
    if(p.pick_count!=p.expected_picks||p.expected_picks<3||p.expected_picks>5)return Role::Ordinary;
    for(int i=0;i<p.pick_count;++i)if(p.identities[i]==identity)return p.roles[i];
    return Role::Ordinary;
}

// An explicit wire format avoids SDK startup-snapshot and C++ padding traps.
// The unused tail is always zeroed; short engine writes can retain old bytes.
using Envelope=std::array<std::uint8_t,1024>;
inline constexpr std::uint32_t plan_magic=0x33465043u;
inline constexpr int plan_version=1,plan_length=140;
inline std::uint32_t plan_checksum(std::span<const std::uint8_t> bytes){
    std::uint32_t h=2166136261u;
    for(std::size_t i=0;i<bytes.size();++i)if(i<12||i>=16){h^=bytes[i];h*=16777619u;}
    return h;
}
inline Envelope encode(const Plan&p){
    Envelope out{};int at=0;
    auto put=[&](std::uint32_t v){for(int i=0;i<4;++i)out[at++]=std::uint8_t(v>>(8*i));};
    put(plan_magic);put(plan_version);put(plan_length);put(0);
    put(unsigned(p.package));put(p.pick_count);put(p.expected_picks);
    for(int i=0;i<5;++i){put(p.picks[i]);put(p.identities[i]);put(unsigned(p.roles[i]));}
    put(p.witness.group_id);
    for(Point q:{p.witness.entry,p.witness.landing,p.witness.pad,p.witness.altar}){put(q.x);put(q.y);}
    put(p.witness.detour);put(p.witness.work);put(p.witness.gems);put(p.witness.gold);
    const auto checksum=plan_checksum(out);
    at=12;put(checksum);return out;
}
inline bool decode(std::span<const std::uint8_t> bytes,Plan&out){
    if(bytes.size()!=1024)return false;
    int at=0;
    auto get=[&](){std::uint32_t v=0;for(int i=0;i<4;++i)v|=std::uint32_t(bytes[at++])<<(8*i);return v;};
    if(get()!=plan_magic||get()!=plan_version||get()!=plan_length)return false;
    if(get()!=plan_checksum(bytes))return false;
    Plan p;p.package=Package(get());p.pick_count=int(get());p.expected_picks=int(get());
    if(unsigned(p.package)>2||p.expected_picks<3||p.expected_picks>5||p.pick_count<0||p.pick_count>p.expected_picks)return false;
    int specialists=0;
    constexpr std::array<int,12> identities{11,10,2,1,7,6,12,8,5,0,9,3};
    for(int i=0;i<5;++i){
        p.picks[i]=int(get());p.identities[i]=int(get());p.roles[i]=Role(get());
        if(p.picks[i]<0||p.picks[i]>=12||p.picks[i]==11||p.identities[i]!=identities[p.picks[i]]||unsigned(p.roles[i])>2)return false;
        for(int j=0;j<i;++j)if(p.picks[i]==p.picks[j])return false;
        if(p.roles[i]!=Role::Ordinary){
            ++specialists;
            if(i>=p.expected_picks)return false;
            if((p.roles[i]==Role::Access&&(p.package!=Package::Access||p.identities[i]!=1))
                ||(p.roles[i]==Role::Supply&&(p.package!=Package::Supply||p.identities[i]!=11)))return false;
        }
    }
    p.witness.group_id=int(get());
    for(Point*q:{&p.witness.entry,&p.witness.landing,&p.witness.pad,&p.witness.altar}){q->x=int(get());q->y=int(get());}
    p.witness.detour=int(get());p.witness.work=int(get());p.witness.gems=int(get());p.witness.gold=int(get());
    if((p.package==Package::Ordinary&&specialists)||(p.package!=Package::Ordinary&&(specialists!=1||p.witness.group_id<0)))return false;
    if(p.package!=Package::Ordinary){
        for(Point q:{p.witness.entry,p.witness.landing,p.witness.pad,p.witness.altar})
            if(q.x<0||q.y<0||q.x>=100||q.y>=100)return false;
        if(p.witness.work<1||p.witness.work>28||p.witness.gems<0||p.witness.gold<0||p.witness.detour<0
            ||(p.witness.gems==0&&p.witness.gold==0))return false;
    }
    // Checksum covers the full envelope; require its defined unused tail too.
    for(std::size_t i=std::size_t(at);i<bytes.size();++i)if(bytes[i])return false;
    out=p;return true;
}

enum Terrain : std::uint8_t { Wall=1,Hole=2,EnemySpawn=4,Altar=8,Camp=16,AllySpawn=32 };
struct Grid {
    int width=0,height=0;
    std::vector<std::uint8_t> flags;
    Grid()=default;
    Grid(int w,int h,std::vector<std::uint8_t> cells):width(w),height(h),flags(std::move(cells)){}
    bool contains(Point p)const{return p.x>=0&&p.y>=0&&p.x<width&&p.y<height;}
    int index(Point p)const{return p.y*width+p.x;}
    Point point(int i)const{return {i%width,i/width};}
    bool walkable(Point p)const{return contains(p)&&!(flags[index(p)]&(Wall|Hole|EnemySpawn|Altar));}
    bool landing(Point p)const{return walkable(p)&&!(flags[index(p)]&Camp);}
};
struct Group {int id=-1;std::vector<Point> pads;float hp=0,strength=0;int gems=0,gold=0;};
struct Capability {
    float health=1000,strength=55,tavi_health=900,tavi_strength=35;
    int range=3,shard_price=30;
};
struct TopologyView {
    int stride=0,components=0;
    std::span<const std::uint8_t> flags;
    std::span<const int> component,from_home,altar_distance,altar_owner,holes,altars,home_altars;
};
// Both the pure fixture flood and the production native-field adapter call
// this exact capability and roster admission procedure.
inline Plan choose_summary(const Grid&g,const std::vector<Group>&groups,const TopologyView&t,
                           bool(*budget_ok)()=nullptr,Capability capacity={}){
    Plan result;if(t.home_altars.empty()||t.components<1)return result;
    const int stride=t.stride;constexpr int home=0;int steps=0;
    const auto&flags=t.flags;const auto&component=t.component;const auto&from_home=t.from_home;
    const auto&altar_distance=t.altar_distance;const auto&altar_owner=t.altar_owner;
    const auto&holes=t.holes;const auto&altars=t.altars;const auto&home_altars=t.home_altars;
    auto cell=[&](Point p){return (p.y+1)*stride+p.x+1;};
    auto point=[&](int k){return Point{k%stride-1,k/stride-1};};
    auto permitted=[&](){return !budget_ok||(++steps%128)!=0||budget_ok();};
    const std::array<int,8> neighbor{-stride-1,-stride,-stride+1,-1,1,stride-1,stride,stride+1};
    std::vector<int> candidate(t.components,-1),work(groups.size(),0);
    for(int id=0;id<int(groups.size());++id){
        const auto&s=groups[id];if(s.pads.empty()||(s.gems<4&&s.gold<capacity.shard_price)||capacity.strength<=0)continue;
        work[id]=std::max(1,int(std::ceil(s.hp/capacity.strength)))+int(s.pads.size());
        if(work[id]>25||s.strength*std::min(work[id],4)>capacity.health*.5f)continue;
        int region=-1;bool all=true;
        for(Point p:s.pads){
            if(!g.contains(p)){all=false;break;}const int k=cell(p),c=component[k];
            if(c<0||c==home||c>=t.components){all=false;break;}
            if(region<0)region=c;else if(c!=region){all=false;break;}
            bool approach=false;
            for(const int offset:neighbor)if(component[k+offset]==c&&!(flags[k+offset]&Camp)){approach=true;break;}
            if(!approach){all=false;break;}
        }
        if(!all||region<0)continue;
        const int old=candidate[region];
        if(old<0||s.gems*4+s.gold>groups[old].gems*4+groups[old].gold)candidate[region]=id;
    }
    double best=-1;
    for(const int middle:holes){
        if(!permitted())return Plan{};
        if(flags[middle]&(Wall|EnemySpawn|Altar))continue;
        for(const int offset:neighbor){
            const int entry_cell=middle-offset,land_cell=middle+offset;
            if(component[entry_cell]!=home||flags[entry_cell]&Camp||flags[land_cell]&Camp)continue;
            const int remote=component[land_cell];
            if(remote<0||remote==home||remote>=t.components||candidate[remote]<0)continue;
            const int id=candidate[remote];const auto&s=groups[id];
            const Point entry=point(entry_cell),land=point(land_cell);
            int pad_distance=10000;Point pad{};
            for(Point p:s.pads)if(separation(land,p)<pad_distance){pad_distance=separation(land,p);pad=p;}
            if(pad_distance>8||from_home[entry_cell]<0)continue;
            int a=home_altars.front(),bank_distance=10000;
            for(int possible:home_altars){const int d=separation(entry,point(altars[possible]));if(d<bank_distance){bank_distance=d;a=possible;}}
            const double value=(4.*s.gems+s.gold)/(10.+from_home[entry_cell]+2*pad_distance+work[id]+bank_distance);
            if(value<=best)continue;best=value;
            result.package=Package::Access;result.roles[0]=Role::Access;
            result.witness={s.id,entry,land,pad,point(altars[a]),10000,work[id],s.gems,s.gold};
            return result; // One executable certificate, no crossing optimizer.
        }
    }
    if(result.package==Package::Access)return result;
    best=-1;
    for(const auto&s:groups){
        if(s.pads.empty()||s.gold<capacity.shard_price||capacity.tavi_strength<=0)continue;
        const int work_turns=std::max(1,int(std::ceil(s.hp/capacity.tavi_strength)))+int(s.pads.size());
        if(work_turns>28||s.strength*std::min(work_turns,4)>capacity.tavi_health*.5f)continue;
        bool all=true;for(Point p:s.pads)if(!g.contains(p)||component[cell(p)]!=home){all=false;break;}
        if(!all)continue;
        const Point pad=s.pads.front();const int k=cell(pad),home_trip=from_home[k],bank_trip=altar_distance[k];
        if(home_trip<14||bank_trip<0||bank_trip>10||altar_owner[k]<0)continue;
        const double value=(4.*s.gems+s.gold)/(8.+home_trip+work_turns+bank_trip);
        if(value<=best)continue;best=value;
        result.package=Package::Supply;result.picks={3,0,4,1,7};result.identities={1,11,7,10,8};
        result.roles[1]=Role::Supply;
        result.witness={s.id,pad,pad,pad,point(altars[altar_owner[k]]),home_trip*2,work_turns,s.gems,s.gold};
    }
    return result;
}

// Padded local integer arrays keep the complete topology pass cheap. The home
// component simultaneously supplies walking distances; one bounded altar wave
// supplies actual paths of at most ten steps. No field is built per source.
inline Plan summarize(const Grid&g,const std::vector<Group>&groups,Point start,
                      bool(*budget_ok)()=nullptr,Capability capacity={}){
    Plan result;
    const int count=g.width*g.height;
    if(count<=0||count>10000||g.flags.size()!=std::size_t(count)||!g.walkable(start))return result;
    const int stride=g.width+2,padded=stride*(g.height+2);
    std::vector<std::uint8_t> flags(padded,Wall);
    // -2 means immutable obstruction, -1 means an unvisited walking tile.
    // Sentinel rows/columns eliminate every inner-loop coordinate check.
    std::vector<int> component(padded,-2),queue(count),from_home(padded,-1);
    std::vector<int> altar_distance(padded,-1),altar_owner(padded,-1);
    std::vector<int> holes,altars;
    auto cell=[&](Point p){return (p.y+1)*stride+p.x+1;};
    for(int y=0;y<g.height;++y)for(int x=0;x<g.width;++x){
        const int k=(y+1)*stride+x+1;const auto f=g.flags[y*g.width+x];flags[k]=f;
        if(!(f&(Wall|Hole|EnemySpawn|Altar)))component[k]=-1;
        if(f&Hole)holes.push_back(k);
        if(f&Altar)altars.push_back(k);
    }
    const std::array<int,8> neighbor{-stride-1,-stride,-stride+1,-1,1,stride-1,stride,stride+1};
    int steps=0;
    auto permitted=[&](){return !budget_ok||(++steps%128)!=0||budget_ok();};
    auto flood=[&](int seed,int label){
        int head=0,tail=1;queue[0]=seed;component[seed]=label;
        if(label==0)from_home[seed]=0;
        while(head<tail){
            if(!permitted())return false;
            const int here=queue[head++];
            for(const int offset:neighbor){
                const int k=here+offset;
                if(component[k]==-1){
                    component[k]=label;queue[tail++]=k;
                    if(label==0)from_home[k]=from_home[here]+1;
                }
            }
        }
        return true;
    };
    // The first flood is the actual allied spawn component, with distances.
    int serial=0;
    if(!flood(cell(start),serial++))return result;
    for(int y=1;y<=g.height;++y)for(int x=1;x<=g.width;++x){
        const int seed=y*stride+x;
        if(component[seed]==-1&&!flood(seed,serial++))return result;
    }
    constexpr int home=0;
    // All home-component altar approaches seed a single wave. Unlike static
    // component labels, ten-step absence is used only for this bounded local
    // Supply program, never as a permanent unreachable certificate.
    int head=0,tail=0;
    std::vector<int> home_altars;
    for(int a=0;a<int(altars.size());++a){
        bool usable=false;
        for(const int offset:neighbor){
            const int k=altars[a]+offset;
            if(component[k]!=home)continue;usable=true;
            if(altar_distance[k]<0){altar_distance[k]=0;altar_owner[k]=a;queue[tail++]=k;}
        }
        if(usable)home_altars.push_back(a);
    }
    while(head<tail){
        if(!permitted())return result;
        const int here=queue[head++],next_distance=altar_distance[here]+1;
        if(next_distance>10)continue;
        for(const int offset:neighbor){
            const int k=here+offset;
            if(component[k]==home&&altar_distance[k]<0){
                altar_distance[k]=next_distance;altar_owner[k]=altar_owner[here];queue[tail++]=k;
            }
        }
    }
    return choose_summary(g,groups,{stride,serial,flags,component,from_home,altar_distance,altar_owner,holes,altars,home_altars},budget_ok,capacity);
}
} // namespace h12_capability_portfolio
