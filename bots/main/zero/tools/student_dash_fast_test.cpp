// Full raw-row equivalence fixture. No engine, game, inference or fuel claim.
#include "../src/core/student_dash_fast.h"
#include "../src/core/catalog.h"
#include "../src/core/moves.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace zero;
struct Fixture { int id; Map map; Obs obs; };

std::uint64_t digest = 1469598103934665603ull;
std::uint64_t cases = 0, rows = 0, words = 0, generated_zero_rows = 0;
std::uint64_t generator_cases = 0, generator_tiles = 0;
std::uint64_t statuses[6]{};
std::string label;
void require(bool value, const char* why) {
    if (!value) throw std::runtime_error(label + ": " + why);
}
void word(std::uint32_t value) { digest ^= value; digest *= 1099511628211ull; ++words; }
std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }
UnitView unit(int id, Tile at, int type = 0, float hp = 100.0f) {
    const auto& d = catalog().champion(type);
    UnitView u; u.id=id; u.at=at; u.type=static_cast<std::uint8_t>(type);
    u.hp=hp; u.max_hp=d.health; u.strength=d.strength; u.focus=d.focus;
    u.cooldown[0]=u.cooldown[1]=0; return u;
}
// Generated from the retained owned deferred-dash fixtures by the frozen driver.
#include "student_dash_fast_fixtures.h"
void finish(Fixture& f) {
    auto& m=f.map; auto& o=f.obs;
    for (int y=0;y<m.h;++y) for(int x=0;x<m.w;++x) {
        Tile t{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)};
        const auto flags=m.at(t);
        if(flags&tf_spawn_a)m.spawn[0].push_back(t);
        if(flags&tf_spawn_b)m.spawn[1].push_back(t);
        if(flags&tf_altar)m.altars.push_back(t);
    }
    if(m.spawn[0].empty())m.spawn[0]={{0,0}};
    if(m.spawn[1].empty())m.spawn[1]={{static_cast<std::int16_t>(m.w-1),static_cast<std::int16_t>(m.h-1)}};
    Group group; group.gold=120;group.gems=10;
    for(auto& c:o.camps) { c.group=0;group.members.push_back({c.at,c.type}); }
    if(group.members.empty())group.members.push_back({m.spawn[1].front(),3});
    m.groups.push_back(group);m.finish();
    o.group_status.assign(m.groups.size(),gs_unknown);
    o.altar_lock.assign(m.altars.size(),lk_none);o.altar_lock_hits.assign(m.altars.size(),0);
}
void ready(Obs& o,int champion,bool exact) {
    const Tile at=o.self.at;
    o.self=unit(1000,at,champion);o.command_facts=exact;
    o.alive=o.can_cast=o.can_move=o.can_attack=true;
    o.can_cast_slot[0]=o.can_cast_slot[1]=true;
    o.utility_known_mask=kAllActions;o.can_priority=true;
    o.ward_arguments_complete=o.consumable_arguments_complete=true;
    o.can_buy=o.can_recall=o.can_tribute=o.can_cancel=true;
}
void compare(const Map& map,const Obs& o,const Memory& memory,ActionType type,bool expect_fresh_zero=false) {
    Context ctx{&map,nullptr,&o,&memory};
    std::vector<Cand> old(3),fast(1);
    const auto a=student::argument_candidates(ctx,type,old);
    const auto b=student::argument_candidates_dash_fast(ctx,type,fast);
    require(a.status==b.status && a.total==b.total,"status/total changed");
    require(old.size()==fast.size(),"row count changed");
    ++cases;statuses[static_cast<unsigned>(a.status)]++;
    word(type);word(static_cast<unsigned>(a.status));word(a.total);word(static_cast<std::uint32_t>(old.size()));
    for(std::size_t i=0;i<old.size();++i) {
        const auto& x=old[i];const auto& y=fast[i];
        require(x.kind==y.kind && x.target==y.target && x.count==y.count && x.at==y.at,"payload/order changed");
        word(x.kind);word(x.target);word(x.count);word(x.at.x);word(x.at.y);
        for(int k=0;k<student::kPhi;++k) {require(bits(x.phi[k])==bits(y.phi[k]),"feature float bits changed");word(bits(x.phi[k]));}
        if(expect_fresh_zero) {
            for(int k:{13,14,15,16,22})require(bits(y.phi[k])==0,"fresh zero-ground invariant failed");
            ++generated_zero_rows;
        }
        ++rows;
    }
}
Memory goal(int which,const Obs& o) {
    Memory m;
    switch(which) {
    case 0:break;
    case 1:m.goal_kind=g_farm;m.goal_target=0;break;
    case 2:m.goal_kind=g_bank;m.goal_target=0;break;
    case 3:m.goal_kind=g_recall;break;
    case 4:m.goal_kind=g_engage;m.goal_target=o.enemies.empty()?-77:o.enemies.front().id;break;
    case 5:m.goal_kind=g_assist;m.goal_target=o.allies.empty()?-77:o.allies.front().id;break;
    case 6:m.goal_kind=g_avoid;break;
    case 7:m.goal_kind=g_hold;break;
    case 8:m.goal_kind=g_none;break;
    case 9:m.goal_kind=g_farm;m.goal_target=100000;break;
    case 10:m.goal_kind=g_bank;m.goal_target=-1;break;
    case 11:m.goal_kind=g_engage;m.goal_target=-12345;break;
    case 12:m.goal_kind=g_assist;m.goal_target=-12345;break;
    case 13:m.goal_kind=-2;break;
    }
    return m;
}
void cached(Obs& o,const Map& map,int range,int mode) {
    legal_moves(map,o,range,true);
    switch(mode) {
    case 0:o.dash_destinations_known=false;break;
    case 1:break;
    case 2: {
        std::reverse(o.dash_ok.begin(),o.dash_ok.end());const auto copy=o.dash_ok;
        o.dash_ok.insert(o.dash_ok.end(),copy.begin(),copy.end());break;
    }
    case 3:o.active_dash_range=range+1;break;
    case 4:o.dash_ok.clear();break;
    case 5:
        o.dash_ok.clear();
        for(int y=o.self.at.y-range;y<=o.self.at.y+range;++y)
            for(int x=o.self.at.x+range;x>=o.self.at.x-range;--x) {
                Tile t{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)};
                o.dash_ok.push_back(t);o.dash_ok.push_back(t);
            }
        o.dash_ok.push_back({static_cast<std::int16_t>(o.self.at.x+range+1),o.self.at.y});break;
    case 6:o.dash_ok={o.self.at,{static_cast<std::int16_t>(o.self.at.x+range+1),o.self.at.y}};break;
    case 7:o.dash_ok={{-300,-300}};o.dash_destinations_known=false;break;
    }
}
std::vector<Fixture> synthetic() {
    std::vector<Fixture> out;
    // Crowded views include duplicate living/dead observations. Repeated += must
    // remain in original order (0.2f/0.05f accumulation is not reassociated).
    Fixture crowded{-100,{},{}};crowded.map.w=crowded.map.h=11;
    crowded.map.flags.assign(121,0);crowded.obs.self.at={5,5};
    crowded.map.flags[4*11+4]=tf_wall;crowded.map.flags[4*11+6]=tf_altar;
    crowded.map.flags[5*11+4]=tf_hole;crowded.map.flags[7*11+7]=tf_spawn_b;
    crowded.map.flags[3*11+5]=tf_bush;crowded.map.flags[5*11+5]=tf_spawn_a;
    for(int i=0;i<13;++i) {
        crowded.obs.enemies.push_back(unit(20+i,{6,5},0,i%4?100.0f:0.0f));
        crowded.obs.allies.push_back(unit(40+i,{5,4},0,i%3?100.0f:-1.0f));
        CampView cp;cp.id=60+i;cp.at={5,6};cp.type=3;cp.hp=i%5?100.0f:0.0f;
        crowded.obs.camps.push_back(cp);
    }
    crowded.obs.reserved_tiles={{6,6},{6,6},{5,4}};out.push_back(crowded);
    for(int side=0;side<2;++side) {
        Fixture edge{-101-side,{},{}};edge.map.w=3;edge.map.h=4;edge.map.flags.assign(12,0);
        edge.obs.self.at=side?Tile{2,3}:Tile{0,0};edge.obs.team=side;
        edge.obs.enemies={unit(1,edge.obs.self.at)};edge.obs.allies={unit(2,edge.obs.self.at)};
        edge.obs.reserved_tiles={edge.obs.self.at};out.push_back(edge);
    }
    Fixture singleton{-103,{},{}};singleton.map.w=singleton.map.h=1;
    singleton.map.flags={0};singleton.obs.self.at={0,0};out.push_back(singleton);
    Fixture outside{-104,{},{}};outside.map.w=3;outside.map.h=4;
    outside.map.flags.assign(12,0);outside.obs.self.at={-1,1};out.push_back(outside);
    return out;
}
void compare_generator(const Map& map,const Obs& o,int range) {
    std::vector<Tile> old(1),fast(3);
    generate_dash_destinations(map,o,range,old);
    generate_dash_destinations_canonical(map,o,range,fast);
    const auto less=[](Tile a,Tile b){return a.y!=b.y?a.y<b.y:a.x<b.x;};
    std::sort(old.begin(),old.end(),less);old.erase(std::unique(old.begin(),old.end()),old.end());
    require(old==fast,"canonical generator complete membership/order changed");
    for(Tile tile:fast) {word(tile.x);word(tile.y);++generator_tiles;}
    ++generator_cases;
}
std::vector<Fixture> generator_controls() {
    std::vector<Fixture> out;
    auto add=[&](int width,int height,Tile at,std::uint8_t flags=0)->Fixture& {
        Fixture f{-1000-static_cast<int>(out.size()),{},{}};
        f.map.w=width;f.map.h=height;f.map.flags.assign(width*height,flags);
        f.obs.self.at=at;out.push_back(f);return out.back();
    };
    for(int y:{0,5,10})for(int x:{0,5,10})add(11,11,{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)});
    for(Tile at:{Tile{-1,5},Tile{11,5},Tile{5,-1},Tile{5,11}})add(11,11,at);
    for(std::uint8_t flag:{tf_wall,tf_hole,tf_altar,tf_spawn_a,tf_spawn_b}) {
        auto& f=add(11,11,{5,5});f.map.flags[60]=flag;
    }
    for(int p:{0,4,8}) {add(9,1,{static_cast<std::int16_t>(p),0});add(1,9,{0,static_cast<std::int16_t>(p)});}
    for(int kind=0;kind<7;++kind) {
        auto& f=add(11,11,{5,5});
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy) {
            Tile t{static_cast<std::int16_t>(5+dx),static_cast<std::int16_t>(5+dy)};
            if(kind<3)f.map.flags[f.map.idx(t)]=kind==0?tf_wall:(kind==1?tf_hole:tf_altar);
            if(kind==3)f.obs.allies.push_back(unit(1,t));
            if(kind==4)f.obs.enemies.push_back(unit(1,t));
            if(kind==5){CampView c;c.at=t;c.type=3;c.hp=0;f.obs.camps.push_back(c);}
            if(kind==6)f.obs.reserved_tiles.push_back(t);
        }
    }
    auto& overlap=add(11,11,{5,5});overlap.obs.allies={unit(1,{6,5})};overlap.obs.enemies={unit(2,{6,5})};
    CampView cp;cp.at={6,5};cp.type=3;cp.hp=0;overlap.obs.camps={cp};overlap.obs.reserved_tiles={{6,5}};
    // One isolated pass cell at every relative index, plus deterministic mixed
    // windows, exercise row borders and the low/high word seam at bits63/64.
    for(int index=0;index<81;++index) {
        auto& f=add(9,9,{4,4},tf_wall);f.map.flags[index]=0;
    }
    std::uint32_t random=0x9e3779b9u;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(int i=0;i<128;++i) {
        auto& f=add(9,9,{static_cast<std::int16_t>(next()%11-1),static_cast<std::int16_t>(next()%11-1)});
        for(auto& flags:f.map.flags) {const auto r=next()%12;flags=r<6?static_cast<std::uint8_t>(1u<<r):0;}
        for(int j=0;j<8;++j) {
            Tile t{static_cast<std::int16_t>(next()%13-2),static_cast<std::int16_t>(next()%13-2)};
            if(j%4==0)f.obs.allies.push_back(unit(j,t,0,j%3?0.0f:100.0f));
            if(j%4==1)f.obs.enemies.push_back(unit(j,t));
            if(j%4==2){CampView c;c.at=t;c.type=3;c.hp=100;f.obs.camps.push_back(c);}
            if(j%4==3)f.obs.reserved_tiles.push_back(t);
        }
    }
    return out;
}
int run() {
    auto fixtures=retained_fixtures();const auto retained_count=fixtures.size();
    auto extra=synthetic();fixtures.insert(fixtures.end(),extra.begin(),extra.end());
    std::vector<std::pair<int,int>> slots;
    for(int c=0;c<catalog().n_champions;++c)for(int s=0;s<2;++s)
        if(catalog().champion(c).ability[s].kind==ab_dash)slots.emplace_back(c,s);
    require(slots.size()==5,"queried dash slot count changed; review fixture coverage");
    bool witness383=false,witness16984=false;
    for(auto f:fixtures) {
        finish(f);witness383|=f.id==383;witness16984|=f.id==16984;
        for(auto [champion,slot]:slots)for(bool exact:{false,true})for(int swap_team=0;swap_team<2;++swap_team) {
            Obs base=f.obs;ready(base,champion,exact);base.team^=swap_team;
            const auto& ability=catalog().champion(champion).ability[slot];
            const int range=static_cast<int>(ability.range);
            const auto action=slot==0?a_cast0:a_cast1;
            for(int cache=0;cache<8;++cache) {
                Obs o=base;cached(o,f.map,range,cache);
                for(int g=0;g<14;++g) {
                    label="fixture="+std::to_string(f.id)+" champ="+std::to_string(champion)+" exact="+std::to_string(exact)+" team="+std::to_string(o.team)+" cache="+std::to_string(cache)+" goal="+std::to_string(g);
                    compare(f.map,o,goal(g,o),action,(cache==0||cache==3||cache==7)&&ability.radius==0.0f);
                }
                for(int state=0;state<5;++state) {
                    Obs unavailable=o;
                    if(state==0)unavailable.alive=false;
                    if(state==1)unavailable.can_cast=false;
                    if(state==2)unavailable.self.cooldown[slot]=1;
                    if(state==3)unavailable.can_cast_slot[slot]=false;
                    if(state==4)unavailable.self.cooldown[slot]=-1;
                    label+=" state="+std::to_string(state);compare(f.map,unavailable,Memory{},action);
                }
            }
        }
    }
    require(witness383&&witness16984,"retained allied/altar transit witnesses missing");
    auto generators=generator_controls();generators.insert(generators.end(),fixtures.begin(),fixtures.end());
    for(const auto& f:generators)for(bool exact:{false,true})for(int team=0;team<2;++team)
        for(int state=0;state<4;++state)for(int range=-1;range<=5;++range) {
            Obs o=f.obs;ready(o,1,exact);o.team=team;o.alive=(state&1)==0;o.can_cast=(state&2)==0;
            label="generator fixture="+std::to_string(f.id)+" exact="+std::to_string(exact)+" team="+std::to_string(team)+" range="+std::to_string(range)+" state="+std::to_string(state);
            compare_generator(f.map,o,range);
        }
    auto control=fixtures.back();finish(control);
    // Every non-dash champion/slot and every non-cast family must delegate,
    // including unknown utility and invalid-action unsupported statuses.
    for(int champion=0;champion<catalog().n_champions;++champion)for(bool exact:{false,true})for(bool known:{false,true}) {
        Obs o=control.obs;ready(o,champion,exact);if(!known)o.utility_known_mask=0;
        legal_moves(control.map,o,2,true);
        for(int action=0;action<=a_count;++action) {
            if((action==a_cast0||action==a_cast1)&&catalog().champion(champion).ability[action==a_cast0?0:1].kind==ab_dash)continue;
            label="delegation champ="+std::to_string(champion)+" type="+std::to_string(action);
            compare(control.map,o,Memory{},static_cast<ActionType>(action));
        }
        compare(control.map,o,Memory{},static_cast<ActionType>(255));
    }
    std::printf("{\"pass\":true,\"retained_fixtures\":%zu,\"synthetic_fixtures\":%zu,\"cases\":%llu,\"rows\":%llu,\"raw_words\":%llu,\"fresh_zero_ground_rows\":%llu,\"digest_u32\":[%u,%u],\"statuses\":[",retained_count,extra.size(),static_cast<unsigned long long>(cases),static_cast<unsigned long long>(rows),static_cast<unsigned long long>(words),static_cast<unsigned long long>(generated_zero_rows),static_cast<unsigned>(digest),static_cast<unsigned>(digest>>32));
    for(int i=0;i<6;++i)std::printf("%s%llu",i?",":"",static_cast<unsigned long long>(statuses[i]));
    std::printf("],\"dash_slots\":[");
    for(std::size_t i=0;i<slots.size();++i)std::printf("%s[%d,%d]",i?",":"",slots[i].first,slots[i].second);
    std::printf("],\"generator_cases\":%llu,\"generator_tiles\":%llu,\"generator_fixtures\":%zu,\"witness383\":true,\"witness16984\":true}\n",static_cast<unsigned long long>(generator_cases),static_cast<unsigned long long>(generator_tiles),generators.size());return 0;
}
}  // namespace
int main() {try{return run();}catch(const std::exception& e){std::fprintf(stderr,"error: %s\n",e.what());return 1;}}
