// Pure production-code parity; no game, engine observation, fuel or strength claim.
#include "../src/core/ground_scene.h"
#include "../src/core/student.h"
#include "../src/core/student_context.h"
#include "../src/core/student_net.h"
#include "../src/core/catalog.h"
#include "../src/core/moves.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace zero;
struct Fixture { int id; Map map; Obs obs; };
std::string label;
std::uint64_t digest=1469598103934665603ull, words=0, cases=0;
std::uint64_t utility_rows=0, student_rows=0, stale_cases=0, delegation_cases=0, network_cases=0;
std::uint64_t partial_contracts=0, max_ward_rows=0, max_lock_rows=0;
std::uint64_t statuses[6]{};
void require(bool value,const char* message) { if(!value)throw std::runtime_error(label+": "+message); }
void word(std::uint32_t value) { digest^=value;digest*=1099511628211ull;++words; }
std::uint32_t bits(float x) { return std::bit_cast<std::uint32_t>(x); }
UnitView unit(int id,Tile at,int type=0) {
    const auto& d=catalog().champion(type);UnitView u;u.id=id;u.at=at;u.type=static_cast<std::uint8_t>(type);
    u.hp=u.max_hp=d.health;u.strength=d.strength;u.focus=d.focus;u.cooldown[0]=u.cooldown[1]=0;return u;
}
// SDK-origin terrain/occupant geometry; utility states below are explicit synthetic variants.
#include "ground_scene_fixtures.h"

void finish(Fixture& f) {
    auto& m=f.map;auto& o=f.obs;
    for(int y=0;y<m.h;++y)for(int x=0;x<m.w;++x) {
        const Tile at{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)};
        if(m.at(at)&tf_spawn_a)m.spawn[0].push_back(at);
        if(m.at(at)&tf_spawn_b)m.spawn[1].push_back(at);
        if(m.at(at)&tf_altar)m.altars.push_back(at);
    }
    if(m.spawn[0].empty())m.spawn[0]={{0,0}};
    if(m.spawn[1].empty())m.spawn[1]={{static_cast<std::int16_t>(m.w-1),static_cast<std::int16_t>(m.h-1)}};
    // Deliberately non-geometric altar order, including a duplicate, exercises original stable payload order.
    std::reverse(m.altars.begin(),m.altars.end());
    if(!m.altars.empty())m.altars.push_back(m.altars.front());
    Group group;group.gold=175;group.gems=23;
    for(auto& c:o.camps){c.group=0;group.members.push_back({c.at,c.type});}
    if(group.members.empty())group.members.push_back({m.spawn[1].front(),3});
    m.groups.push_back(group);m.finish();
    o.group_status.assign(m.groups.size(),gs_unknown);
    o.altar_lock.assign(m.altars.size(),lk_none);o.altar_lock_hits.assign(m.altars.size(),0);
    o.revision=12;o.round=123;o.self=unit(100,o.self.at);
    o.command_facts=o.alive=o.can_move=o.can_attack=o.can_cast=true;
    o.can_cast_slot[0]=o.can_cast_slot[1]=true;
    o.can_buy=o.can_recall=o.can_tribute=o.can_cancel=o.can_priority=true;
    o.ward_charges=2;o.ward_cooldown=7;o.ally_ward_facts=o.reservation_facts=true;
    o.consumables={1,3};o.utility_known_mask=kAllActions;
    o.self.gold=217;o.self.gems=37;o.priority=4;o.command_index=2;
    o.allies_alive=4;o.enemies_alive=3;o.banked_us=151;o.banked_them=173;
    o.item_counts[0]=o.items[0]=2;o.buyable_items[0]=true;
    o.utility_actions.clear();
    append_buy_consumable_utility(o,a_buy_consumable,0,40);
    append_sell_utility(o,a_sell,0,22);
    // These stale ground rows must be replaced, without disturbing the preserved non-ground prefix.
    append_utility(o,a_ward,77,{-20,-20});append_utility(o,a_use_consumable,0,{-21,-21});
    canonical_observation_order(o);legal_moves(m,o,2,false);
}

std::vector<Fixture> synthetic() {
    std::vector<Fixture> out;
    for(int shape=0;shape<8;++shape) {
        Fixture f{-100-shape,{},{}};f.map.w=shape==6?1:9;f.map.h=shape==7?1:9;
        f.map.flags.assign(static_cast<std::size_t>(f.map.w*f.map.h),0);
        f.obs.self.at={static_cast<std::int16_t>(f.map.w/2),static_cast<std::int16_t>(f.map.h/2)};
        if(shape==1)f.obs.self.at={0,0};
        if(shape==2)f.obs.self.at={static_cast<std::int16_t>(f.map.w-1),static_cast<std::int16_t>(f.map.h-1)};
        if(shape==3)f.obs.self.at={-1,0};
        if(shape==4)f.obs.self.at={static_cast<std::int16_t>(f.map.w),static_cast<std::int16_t>(f.map.h)};
        for(int y=0;y<f.map.h;++y)for(int x=0;x<f.map.w;++x) {
            const int key=x+7*y;
            if(key%17==0)f.map.flags[y*f.map.w+x]|=tf_altar;
            if(shape==5&&key%5==0)f.map.flags[y*f.map.w+x]|=tf_hole;
            if(shape==5&&key%7==0)f.map.flags[y*f.map.w+x]|=tf_wall;
            if(key%23==0)f.map.flags[y*f.map.w+x]|=tf_spawn_a;
            if(key%29==0)f.map.flags[y*f.map.w+x]|=tf_spawn_b;
        }
        f.obs.allies={unit(1,{1,1})};f.obs.enemies={unit(2,{2,2},3)};
        f.obs.reserved_tiles={{3,3},{3,3},{4,4}};out.push_back(f);
    }
    return out;
}

void install(Obs& o,const UtilityBuildContract& c) {
    o.utility_known_consumables=c.known_consume_mask;o.utility_capability_limits=c.limitations;
    o.utility_affected_limits=c.affected_limits;o.ward_arguments_complete=c.ward_complete;
    o.consumable_arguments_complete=c.consume_complete;
}
void contract_equal(const UtilityBuildContract& a,const UtilityBuildContract& b) {
    require(a.known_consume_mask==b.known_consume_mask&&a.limitations==b.limitations&&a.affected_limits==b.affected_limits,
            "contract masks changed");
    require(a.ward_complete==b.ward_complete&&a.consume_complete==b.consume_complete&&a.ward_count==b.ward_count&&a.lock_count==b.lock_count,
            "contract completeness/count changed");
    for(auto value:{a.known_consume_mask,a.limitations,a.affected_limits,static_cast<unsigned>(a.ward_complete),
                    static_cast<unsigned>(a.consume_complete),static_cast<unsigned>(a.ward_count),static_cast<unsigned>(a.lock_count)})word(value);
}
void utility_equal(const std::vector<UtilityAction>& a,const std::vector<UtilityAction>& b) {
    require(a.size()==b.size(),"utility row count changed");word(static_cast<unsigned>(a.size()));
    for(std::size_t i=0;i<a.size();++i) {
        require(a[i].type==b[i].type&&a[i].target==b[i].target&&a[i].at==b[i].at,"utility payload/order changed");
        word(a[i].type);word(a[i].target);word(a[i].at.x);word(a[i].at.y);
        for(int k=0;k<24;++k){require(bits(a[i].phi[k])==bits(b[i].phi[k]),"utility feature bits changed");word(bits(a[i].phi[k]));}
        ++utility_rows;
    }
}
void metadata_equal(const Obs& a,const Obs& b) {
    require(a.utility_known_mask==b.utility_known_mask&&a.utility_known_consumables==b.utility_known_consumables&&
            a.utility_capability_limits==b.utility_capability_limits&&a.utility_affected_limits==b.utility_affected_limits&&
            a.ward_arguments_complete==b.ward_arguments_complete&&a.consumable_arguments_complete==b.consumable_arguments_complete,
            "installed observation metadata changed");
}
std::vector<UtilityAction> selected(const Obs& o,ActionType type) {
    std::vector<UtilityAction> out;for(const auto& a:o.utility_actions)if(a.type==type)out.push_back(a);return out;
}
void candidates_equal(const Context& old,const Context& compact,const GroundUtilityScene& scene,ActionType type) {
    std::vector<Cand> a(3),b(1);
    const auto x=student::argument_candidates(old,type,a),y=student::argument_candidates_ground_scene(compact,scene,type,b);
    require(x.status==y.status&&x.total==y.total&&a.size()==b.size(),"student build status/count changed");
    ++statuses[static_cast<unsigned>(x.status)];word(type);word(static_cast<unsigned>(x.status));word(x.total);word(static_cast<unsigned>(a.size()));
    for(std::size_t i=0;i<a.size();++i) {
        require(a[i].kind==b[i].kind&&a[i].target==b[i].target&&a[i].count==b[i].count&&a[i].at==b[i].at,
                "student payload/order changed");
        word(a[i].kind);word(a[i].target);word(a[i].count);word(a[i].at.x);word(a[i].at.y);
        for(int k=0;k<24;++k){require(bits(a[i].phi[k])==bits(b[i].phi[k]),"student feature bits changed");word(bits(a[i].phi[k]));}
        ++student_rows;
    }
}
Memory goal(int which,const Obs& o) {
    Memory m;m.goal_since=4;
    switch(which%8) {
    case 1:m.goal_kind=g_farm;m.goal_target=0;break;
    case 2:m.goal_kind=g_bank;m.goal_target=0;break;
    case 3:m.goal_kind=g_recall;break;
    case 4:m.goal_kind=g_engage;m.goal_target=o.enemies.empty()?-7:o.enemies.front().id;break;
    case 5:m.goal_kind=g_assist;m.goal_target=o.allies.empty()?-8:o.allies.front().id;break;
    case 6:m.goal_kind=g_avoid;break;
    case 7:m.goal_kind=g_farm;m.goal_target=100000;break;
    default:break;
    }return m;
}
void input_equal(const Context& old,const Context& compact,const GroundUtilityScene& scene,bool network) {
    std::array<float,student::kState>a{},b{};student::state_vector(old,a.data());student::state_vector(compact,b.data());
    for(int k=0;k<student::kState;++k){require(bits(a[k])==bits(b[k]),"state48 changed");word(bits(a[k]));}
    std::vector<student::CampContext>x,y;student::camp_context(old,x);student::camp_context(compact,y);
    require(x.size()==y.size(),"camp context size changed");
    for(std::size_t i=0;i<x.size();++i){require(x[i].group==y[i].group,"camp group changed");word(x[i].group);
        for(int k=0;k<student::kCampContextPhi;++k){require(bits(x[i].phi[k])==bits(y[i].phi[k]),"camp context bits changed");word(bits(x[i].phi[k]));}}
    for(auto allowed:{kAllActions,action_bit(a_ward)|action_bit(a_use_consumable),action_bit(a_ward),action_bit(a_use_consumable),0u}) {
        const auto r=student::rule_mask(old,allowed),s=student::rule_mask_ground_scene(compact,scene,allowed);
        require(r==s,"root rule mask changed");word(r);
    }
    if(network) {
        std::vector<float>w(student::net::kWeightCapacity);
        const auto& l=student::net::layout();const auto& at=student::net::attention_layout();
        for(int k=l.begin;k<at.total;++k)w[k]=static_cast<float>((k%17)-8)/128.0f;
        // Nonzero attention/encoder/controller. Logits compare within each runtime; only raw
        // input words enter the cross-runtime digest, so no cross-libm equality is assumed.
        student::net::Embedding e,f;require(student::net::embed_attention(w.data(),a.data(),x,e),"old attention invalid");
        require(student::net::embed_attention(w.data(),b.data(),y,f),"compact attention invalid");
        float p[a_count],q[a_count];student::net::command_logits(w.data(),e,p);student::net::command_logits(w.data(),f,q);
        for(int k=0;k<a_count;++k)require(bits(p[k])==bits(q[k]),"controller logit bits changed");
        ++network_cases;
    }
}
void state(Obs& o,int which) {
    switch(which) {
    case 1:o.command_facts=false;break;case 2:o.alive=false;break;case 3:o.self.stunned=true;break;
    case 4:o.self.recalling=true;break;case 5:o.self.tributing=true;break;case 6:o.teleporting=true;break;
    case 7:o.state_bits|=1u<<1;break;case 8:o.state_bits|=1u<<2;break;case 9:o.state_bits|=1u<<5;break;
    case 10:o.state_bits|=1u<<12;break;case 11:o.restriction_bits|=1u<<5;break;case 12:o.restriction_bits|=1u<<4;break;
    case 13:o.ward_charges=0;break;case 14:o.consumables={0,0};break;case 15:o.consumables={2,0};break;
    case 16:o.consumables={0,4};break;case 17:o.ally_ward_facts=false;break;case 18:o.reservation_facts=false;break;
    case 19:o.altar_lock.clear();break;case 20:o.ward_charges=-1;break;case 21:o.restriction_bits=0xff;break;
    }
}
UtilityRules scenario(Obs& o,int which) {
    UtilityRules r{2.75f,2.0f,61,kConsumeLock};
    switch(which) {
    case 1:r.ward_radius=0;break;case 2:r.ward_radius=1.5f;r.lock_radius=1.5f;break;
    case 3:r.known_consumable_mask=0;break;case 4:r.known_consumable_mask=kConsumeTp|kConsumeLock;o.ally_ward_tiles={o.self.at,o.self.at};break;
    case 5:r.ward_radius=-1;r.lock_radius=-1;break;
    case 6:r.ward_radius=std::numeric_limits<float>::quiet_NaN();r.lock_radius=std::numeric_limits<float>::infinity();break;
    case 7:r.ward_radius=32;r.lock_radius=32;break;
    }
    for(std::size_t a=0;a<o.altar_lock.size();++a)o.altar_lock[a]=static_cast<Lock>((a+which)%3);
    return r;
}
void check(const Map& map,const Obs& input,const UtilityRules& rules,const Memory& memory,bool network=false,bool delegate=false) {
    Obs old=input,compact=input;
    const auto contract=append_ground_utilities(map,old,rules);install(old,contract);
    GroundUtilityScene scene;build_ground_utility_scene(map,compact,rules,scene);
    require(scene.fresh_for(map,compact),"new scene not fresh");contract_equal(contract,scene.contract);
    max_ward_rows=std::max(max_ward_rows,static_cast<std::uint64_t>(contract.ward_count));
    max_lock_rows=std::max(max_lock_rows,static_cast<std::uint64_t>(contract.lock_count));
    if(!contract.ward_complete||!contract.consume_complete)++partial_contracts;
    require(apply_ground_utility_contract(compact,scene),"fresh contract application refused");metadata_equal(old,compact);
    std::vector<UtilityAction> remaining;for(const auto& r:old.utility_actions)if(r.type!=a_ward&&r.type!=a_use_consumable)remaining.push_back(r);
    utility_equal(remaining,compact.utility_actions);
    for(auto type:{a_ward,a_use_consumable}) {
        std::vector<UtilityAction> rows(2);require(materialize_ground_utility_scene(map,compact,scene,type,rows),"fresh materialization refused");
        utility_equal(selected(old,type),rows);
    }
    Context a{&map,nullptr,&old,&memory},b{&map,nullptr,&compact,&memory};input_equal(a,b,scene,network);
    for(auto type:{a_ward,a_use_consumable})candidates_equal(a,b,scene,type);
    if(delegate)for(int k=0;k<a_count+2;++k) {
        const auto type=static_cast<ActionType>(k<a_count?k:k==a_count?a_count:255);
        if(type==a_ward||type==a_use_consumable)continue;
        candidates_equal(a,b,scene,type);++delegation_cases;
    }
    ++cases;
}
void stale(const Map& map,Obs input) {
    GroundUtilityScene scene;UtilityRules rules{2,2,61,kConsumeLock};build_ground_utility_scene(map,input,rules,scene);
    require(apply_ground_utility_contract(input,scene),"initial apply failed");
    Memory memory;Context ctx{&map,nullptr,&input,&memory};
    Obs other=input;require(!scene.fresh_for(map,other),"equal-copy Obs accepted");
    Map other_map=map;require(!scene.fresh_for(other_map,input),"equal-copy Map accepted");
    ++input.revision;const auto mask=input.utility_known_mask;const auto count=input.utility_actions.size();
    require(!scene.fresh_for(map,input)&&!apply_ground_utility_contract(input,scene),"stale revision accepted");
    require(input.utility_known_mask==mask&&input.utility_actions.size()==count,"stale apply mutated output");
    require(student::rule_mask_ground_scene(ctx,scene,kAllActions)==action_bit(a_end),"stale root did not fail closed");
    for(auto type:{a_ward,a_use_consumable}) {
        std::vector<UtilityAction> raw(3);require(!materialize_ground_utility_scene(map,input,scene,type,raw)&&raw.empty(),"stale rows leaked");
        std::vector<Cand> rows(3);const auto build=student::argument_candidates_ground_scene(ctx,scene,type,rows);
        require(build.status==ActionBuildStatus::mismatch&&rows.empty(),"stale student rows leaked");
    }
    build_ground_utility_scene(map,input,rules,scene);require(scene.fresh_for(map,input),"rebuild failed");
    std::vector<UtilityAction> wrong(3);require(!materialize_ground_utility_scene(map,input,scene,a_move,wrong)&&wrong.empty(),"wrong family materialized");
    input.ward_charges=0;input.consumables={0,0};++input.revision;
    Obs old=input;const auto empty=append_ground_utilities(map,old,rules);install(old,empty);
    build_ground_utility_scene(map,input,rules,scene);contract_equal(empty,scene.contract);
    require(scene.ward_tiles.empty()&&scene.lock_rows.empty(),"rebuild retained stale destinations");
    require(apply_ground_utility_contract(input,scene),"empty rebuild refused");metadata_equal(old,input);
    for(auto type:{a_ward,a_use_consumable}){std::vector<UtilityAction> rows(3);require(materialize_ground_utility_scene(map,input,scene,type,rows)&&rows.empty(),"empty rebuild rows leaked");}
    word(1);++stale_cases;
}
} // namespace
int main() {
    try {
        auto fixtures=retained_fixtures();const auto retained=fixtures.size();auto extra=synthetic();fixtures.insert(fixtures.end(),extra.begin(),extra.end());
        for(auto f:fixtures) {
            finish(f);
            for(int team=0;team<2;++team)for(int s=0;s<22;++s)for(int r=0;r<8;++r) {
                Obs o=f.obs;o.team=team;state(o,s);const auto rules=scenario(o,r);const auto memory=goal(r,o);
                label="fixture="+std::to_string(f.id)+" team="+std::to_string(team)+" state="+std::to_string(s)+" scenario="+std::to_string(r);
                check(f.map,o,rules,memory,s==0&&r==0,s==0&&r==0);
            }
            stale(f.map,f.obs);
        }
        // These malformed map facts are covered only by the utility contract, not by core Map preconditions.
        for(int mode=0;mode<4;++mode) {
            auto f=fixtures.back();finish(f);auto m=f.map;auto o=f.obs;
            if(mode==0)m.flags.clear();if(mode==1)m.w=0;if(mode==2)o.team=-1;if(mode==3)o.team=2;
            label="invalid map mode="+std::to_string(mode);Obs old=o,compact=o;UtilityRules r{2,2,61,kConsumeLock};
            const auto c=append_ground_utilities(m,old,r);install(old,c);GroundUtilityScene scene;build_ground_utility_scene(m,compact,r,scene);
            contract_equal(c,scene.contract);require(apply_ground_utility_contract(compact,scene),"invalid-map contract not installable");metadata_equal(old,compact);
            for(auto type:{a_ward,a_use_consumable}){std::vector<UtilityAction> rows;require(materialize_ground_utility_scene(m,compact,scene,type,rows),"empty invalid-map rows refused");utility_equal(selected(old,type),rows);}
        }
        require(max_ward_rows>24&&partial_contracts>0,"no complete uncapped/partial coverage");
        for(auto status:{ActionBuildStatus::ok,ActionBuildStatus::unavailable,ActionBuildStatus::unsupported,ActionBuildStatus::truncated})
            require(statuses[static_cast<unsigned>(status)]>0,"missing build-status coverage");
        std::printf("{\"pass\":true,\"cases\":%llu,\"retained_geometry_fixtures\":%zu,\"synthetic_fixtures\":%zu,\"utility_rows\":%llu,\"student_rows\":%llu,\"stale_cases\":%llu,\"delegation_cases\":%llu,\"network_cases\":%llu,\"invalid_map_contract_cases\":4,\"raw_words\":%llu,\"partial_contracts\":%llu,\"max_ward_rows\":%llu,\"max_lock_rows\":%llu,\"digest_u32\":[%u,%u],\"statuses\":[",
            static_cast<unsigned long long>(cases),retained,extra.size(),static_cast<unsigned long long>(utility_rows),static_cast<unsigned long long>(student_rows),
            static_cast<unsigned long long>(stale_cases),static_cast<unsigned long long>(delegation_cases),static_cast<unsigned long long>(network_cases),static_cast<unsigned long long>(words),
            static_cast<unsigned long long>(partial_contracts),static_cast<unsigned long long>(max_ward_rows),static_cast<unsigned long long>(max_lock_rows),static_cast<unsigned>(digest),static_cast<unsigned>(digest>>32));
        for(int k=0;k<6;++k)std::printf("%s%llu",k?",":"",static_cast<unsigned long long>(statuses[k]));std::printf("]}\n");return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"error ground scene parity: %s\n",e.what());return 1; }
}
