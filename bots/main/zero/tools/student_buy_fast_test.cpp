// Pure production-builder parity. No SDK, game, fuel or strength claim.
#include "../src/core/student_buy_fast.h"
#include "../src/core/student_net.h"
#include "../src/core/student_context.h"
#include "../src/core/catalog_data.h"
#include "../src/core/moves.h"
#include "../src/core/rules.h"
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
// Separate test-owned catalogs are initialized once, then never mutated by cases.
// The production const catalog and generated arrays are never cast or changed.
struct CatalogFixture { std::array<ItemDesign,kMaxItems> items{}; Catalog value{}; };
std::array<CatalogFixture,5> catalogs;
const Catalog* active_catalog{};
}
namespace zero {
const Catalog& catalog() { if(!active_catalog)throw std::runtime_error("test catalog not selected");return *active_catalog; }
}
namespace {
std::string label;
std::uint64_t digest=1469598103934665603ull,words=0,cases=0,rows=0,quantity_rows=0;
std::uint64_t network_cases=0,delegation_cases=0,decode_cases=0,one_hot_cases=0;
std::uint64_t statuses[6]{},catalog_rows[5]{},max_rows=0,zero_price_rows=0,high_id_rows=0;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(label+": "+why);}
std::uint32_t bits(float value){return std::bit_cast<std::uint32_t>(value);}
void word(std::uint32_t value){digest^=value;digest*=1099511628211ull;++words;}

void prepare_catalogs(){
    const auto base_count=static_cast<int>(sizeof(data::kItems)/sizeof(data::kItems[0]));
    require(base_count<=kMaxItems,"catalog exceeds existing item capacity");
    for(auto& f:catalogs){
        for(int i=0;i<kMaxItems;++i){
            if(i<base_count)f.items[i]=data::kItems[i];
            else{auto& d=f.items[i];d.name="synthetic_other";d.id=static_cast<std::uint8_t>(i);
                d.cost=static_cast<std::uint32_t>(i+1);d.result=-1;d.purchasable=true;}
        }
        f.value={data::kSettings,data::kChampions,static_cast<int>(sizeof(data::kChampions)/sizeof(data::kChampions[0])),
            data::kCamps,static_cast<int>(sizeof(data::kCamps)/sizeof(data::kCamps[0])),f.items.data(),base_count,
            data::kPassives,static_cast<int>(sizeof(data::kPassives)/sizeof(data::kPassives[0]))};
    }
    catalogs[1].value.n_items=kMaxItems;
    for(auto& d:catalogs[1].items){d.purchasable=true;d.cost=0;d.passive_magnitude=-0.0f;}
    catalogs[2].value.n_items=kMaxItems;
    for(int i=0;i<kMaxItems;++i){auto& d=catalogs[2].items[i];
        d.purchasable=i%5!=0;d.cost=i%7==0?0u:i%7==1?1u:i%7==2?0xffffffffu:static_cast<std::uint32_t>(i*17);
        d.combine_count=i%3==0?0xffffffffu:static_cast<std::uint32_t>(i%6);
        d.combine_cost=i%4==0?0xffffffffu:static_cast<std::uint32_t>(i*13);
        d.result=static_cast<std::int16_t>(i%4==0?-1:i%4==1?kMaxItems-1:i%4==2?kMaxItems:0);
        d.health=i*41;d.strength=i*3;d.focus=i*7;d.passive_magnitude=static_cast<float>(i-20)/32.0f;
    }
    catalogs[3].value.n_items=1;catalogs[3].items[0].purchasable=true;catalogs[3].items[0].cost=1;
    catalogs[3].items[0].result=kMaxItems-1;catalogs[3].items[0].combine_count=5;
    catalogs[4].value.n_items=0;
}

UnitView unit(int id,Tile at,int type=0){const auto& d=catalog().champion(type);UnitView u;
    u.id=id;u.at=at;u.type=static_cast<std::uint8_t>(type);u.hp=u.max_hp=d.health;
    u.strength=d.strength;u.focus=d.focus;u.cooldown[0]=u.cooldown[1]=0;return u;}
Map make_map(){Map m;m.w=9;m.h=7;m.flags.assign(63,0);m.flags[0]=tf_spawn_a;m.flags[62]=tf_spawn_b;
    m.flags[4+3*9]=tf_bush;m.flags[2+2*9]=tf_altar;m.flags[6+4*9]=tf_altar;
    m.spawn[0]={{0,0}};m.spawn[1]={{8,6}};m.altars={{2,2},{6,4}};
    Group g;g.gold=120;g.gems=19;g.members={{{5,3},3},{{5,4},0}};m.groups.push_back(g);m.finish();return m;}
Obs make_obs(const Map& map,int team){Obs o;o.revision=17;o.round=67;o.team=team;o.self=unit(9,{4,3},3);
    o.command_facts=o.alive=o.can_move=o.can_attack=o.can_cast=true;o.can_cast_slot[0]=o.can_cast_slot[1]=true;
    o.can_buy=o.can_recall=o.can_tribute=o.can_cancel=o.can_priority=true;o.utility_known_mask=kAllActions;
    o.ward_arguments_complete=o.consumable_arguments_complete=true;o.on_spawn=false;o.self.gold=40;o.self.gems=36;
    o.allies={unit(4,{3,3})};o.enemies={unit(11,{6,3},0)};o.allies_alive=4;o.enemies_alive=3;
    CampView cp;cp.id=7;cp.at={5,3};cp.type=3;cp.hp=100;cp.group=0;cp.awake=true;o.camps.push_back(cp);
    o.group_status={gs_alive};o.altar_lock={lk_none,lk_theirs};o.altar_lock_hits={0,4};
    o.consumables={1,2};o.ward_charges=1;o.priority=3;o.command_index=2;o.banked_us=151;o.banked_them=173;
    append_buy_consumable_utility(o,a_buy_consumable,0,40);append_sell_utility(o,a_sell,0,20);
    append_utility(o,a_ward,-1,{4,4});append_utility(o,a_use_consumable,1,{2,2});
    canonical_observation_order(o);legal_moves(map,o,2,false);return o;}
Memory goal(int which,const Obs& o){Memory m;m.goal_since=3;
    switch(which){case 1:m.goal_kind=g_farm;m.goal_target=0;break;case 2:m.goal_kind=g_bank;m.goal_target=0;break;
    case 3:m.goal_kind=g_recall;break;case 4:m.goal_kind=g_engage;m.goal_target=o.enemies.front().id;break;
    case 5:m.goal_kind=g_assist;m.goal_target=o.allies.front().id;break;case 6:m.goal_kind=g_avoid;break;
    case 7:m.goal_kind=g_hold;break;case 8:m.goal_kind=g_none;break;case 9:m.goal_kind=g_farm;m.goal_target=999;break;
    case 10:m.goal_kind=g_bank;m.goal_target=-1;break;case 11:m.goal_kind=g_engage;m.goal_target=-8;break;
    case 12:m.goal_kind=g_assist;m.goal_target=-9;break;case 13:m.goal_kind=-2;break;default:break;}return m;}
void permission(Obs& o,int mode){switch(mode){case 1:o.command_facts=false;break;case 2:o.can_buy=false;break;
    case 3:o.alive=false;o.self.hp=0;break;case 4:o.self.stunned=true;break;case 5:o.self.recalling=true;break;
    case 6:o.self.tributing=true;o.teleporting=true;break;case 7:o.restriction_bits=0xff;break;}}
void inventory(Obs& o,int mode){for(int i=0;i<kMaxItems;++i){const auto count=mode==0?0u:mode==1?4u:
    mode==2?static_cast<std::uint32_t>(i%7):0xffffffffu;o.item_counts[i]=count;o.items[i]=static_cast<std::uint8_t>(std::min(255u,count));}}
void buyable(Obs& o,int mode,int one=-1){for(int i=0;i<kMaxItems;++i){const auto& cat=catalog();
    o.buyable_items[i]=one>=0?i==one:mode==0?false:mode==1?true:mode==2?i%2==0:
        i<cat.n_items&&cat.item(i).cost>0&&cat.item(i).cost<=o.self.gold;}}

void cand_equal(const Cand& a,const Cand& b,bool count_quantity=false){
    require(a.kind==b.kind&&a.target==b.target&&a.count==b.count&&a.at==b.at,"payload/order changed");
    word(a.kind);word(a.target);word(a.count);word(a.at.x);word(a.at.y);
    for(int k=0;k<student::kPhi;++k){require(bits(a.phi[k])==bits(b.phi[k]),"raw float word changed");word(bits(a.phi[k]));}
    if(count_quantity)++quantity_rows;else ++rows;
}
std::int32_t expected_max(const Context& c,int id){const auto& cat=catalog();if(id<0||id>=cat.n_items||id>=kMaxItems)return 0;
    const auto& d=cat.item(id);if(!d.purchasable||!d.cost)return 0;
    return static_cast<std::int32_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(c.obs->self.gold)/d.cost,0x7fffffffu));}
void quantity(const Context& c,const Cand& a,const Cand& b){Cand x,y;student::quantity_candidate(c,a,x);student::quantity_candidate(c,b,y);
    cand_equal(x,y,true);const auto maximum=max_affordable_quantity(c,a.target);require(maximum==expected_max(c,a.target),"quantity maximum contract changed");
    const bool ready=maximum>0&&c.obs->command_facts&&c.obs->can_buy&&a.target>=0&&a.target<kMaxItems&&c.obs->buyable_items[a.target];
    if(a.target>=0&&a.target<catalog().n_items&&a.target<kMaxItems)
        require(bits(x.phi[0])==bits(ready?1.0f:0.0f),"quantity availability marker changed");
    word(static_cast<std::uint32_t>(maximum));}

void compare(const Map& map,const Obs& o,const Memory& m,int catalog_id,ActionType type=a_buy,bool network=false){
    Context c{&map,nullptr,&o,&m};std::array<float,student::kState> before{},after{};student::state_vector(c,before.data());
    const auto root=student::rule_mask(c,kAllActions);const auto old_buyable=o.buyable_items;const auto old_counts=o.item_counts;
    std::vector<Cand> a(3),b(2);if(cases%3==1)b.reserve(kMaxItems+9);if(cases%3==2)a.reserve(kMaxItems+11);
    for(auto& row:a){row.target=123;row.phi[5]=-7;}for(auto& row:b){row.target=456;row.phi[9]=8;}
    const auto x=student::argument_candidates(c,type,a),y=student::argument_candidates_buy_fast(c,type,b);
    require(x.status==y.status&&x.total==y.total&&a.size()==b.size(),"build status/count changed");
    ++statuses[static_cast<unsigned>(x.status)];word(type);word(static_cast<unsigned>(x.status));word(x.total);word(a.size());
    int prior=-1;for(std::size_t i=0;i<a.size();++i){cand_equal(a[i],b[i]);
        if(type==a_buy){require(a[i].kind==1&&a[i].target>prior&&a[i].at==Tile{}&&a[i].count==1,"BUY canonical payload/order invariant failed");
            prior=a[i].target;const auto& d=catalog().item(prior);if(!d.cost)++zero_price_rows;if(prior>=42)++high_id_rows;
            quantity(c,a[i],b[i]);++catalog_rows[catalog_id];}}
    max_rows=std::max(max_rows,static_cast<std::uint64_t>(a.size()));
    student::state_vector(c,after.data());require(before==after&&root==student::rule_mask(c,kAllActions),"root input/mask mutated");
    require(o.buyable_items==old_buyable&&o.item_counts==old_counts,"input inventory facts mutated");
    for(int k=0;k<student::kState;++k){require(bits(before[k])==bits(after[k]),"state bits changed");word(bits(before[k]));}word(root);
    if(network&&type==a_buy&&!a.empty()){
        static const std::vector<float> weights=[] {std::vector<float>w(student::net::kWeightCapacity);
            for(int k=student::net::layout().begin;k<student::net::attention_layout().total;++k)w[k]=static_cast<float>((k%19)-9)/256.0f;return w;}();
        std::vector<student::CampContext> camps;student::camp_context(c,camps);student::net::Embedding embedding;
        require(student::net::embed_attention(weights.data(),before.data(),camps,embedding),"nonzero embedding invalid");
        student::net::Projection shop,q;require(student::net::project(weights.data(),embedding,h_shop,shop),"shop projection invalid");
        require(student::net::project(weights.data(),embedding,h_quantity,q),"quantity projection invalid");
        int best_a=0,best_b=0;float high_a=-std::numeric_limits<float>::infinity(),high_b=high_a;
        for(std::size_t i=0;i<a.size();++i){const auto p=student::net::score(shop,a[i].phi),v=student::net::score(shop,b[i].phi);
            require(bits(p)==bits(v),"shop score bits changed");if(p>high_a){high_a=p;best_a=static_cast<int>(i);}if(v>high_b){high_b=v;best_b=static_cast<int>(i);}}
        require(best_a==best_b,"natural pointer choice changed");Cand p,v;student::quantity_candidate(c,a[best_a],p);student::quantity_candidate(c,b[best_b],v);
        const float p_score=student::net::score(q,p.phi),v_score=student::net::score(q,v.phi);require(bits(p_score)==bits(v_score),"quantity score changed");
        const auto maximum=max_affordable_quantity(c,a[best_a].target);
        require(decode_quantity(p_score,maximum)==decode_quantity(v_score,maximum),"quantity decode changed");++network_cases;
    }
    ++cases;
}
void quantity_edges(const Map& map,Obs o){Memory m;Context c{&map,nullptr,&o,&m};
    for(int id:{-1,0,catalog().n_items-1,catalog().n_items,kMaxItems-1,kMaxItems}){Cand a;a.kind=1;a.target=id;quantity(c,a,a);}
    const std::array<float,13> scalars={std::numeric_limits<float>::quiet_NaN(),-std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),-1000000,-10,-0.01f,-0.0f,0,0.01f,1,10,100,1000000};
    for(int maximum:{std::numeric_limits<std::int32_t>::min(),-1,0,1,2,17,std::numeric_limits<std::int32_t>::max()})for(float scalar:scalars){
        const auto value=decode_quantity(scalar,maximum);require(maximum<=0?value==0:value>=1&&value<=maximum,"quantity decoded outside bounds");
        if(maximum>0&&!std::isfinite(scalar))require(value==(scalar>0?maximum:1),"nonfinite quantity decode changed");++decode_cases;}
}
}

int main(){try{
    prepare_catalogs();const std::array<std::uint32_t,12> gold={0,1,29,30,31,39,40,59,60,1000,0x7fffffffu,0xffffffffu};
    for(int cat=0;cat<5;++cat){active_catalog=&catalogs[cat].value;const auto map=make_map();
        const int goals=cat==0?14:2;const int perms=cat==0?8:4;
        for(int team=0;team<2;++team)for(auto cash:gold)for(int counts=0;counts<4;++counts)for(int perm=0;perm<perms;++perm)
            for(int g=0;g<goals;++g)for(int mask=0;mask<4;++mask){Obs o=make_obs(map,team);o.self.gold=cash;inventory(o,counts);permission(o,perm);buyable(o,mask);
                const auto memory=goal(g==1&&cat!=0?9:g,o);label="catalog="+std::to_string(cat)+" case="+std::to_string(cases);
                compare(map,o,memory,cat,a_buy,cases%137==0);}
        for(int team=0;team<2;++team)for(int item=0;item<kMaxItems;++item)for(int perm=0;perm<3;++perm){Obs o=make_obs(map,team);
            o.self.gold=gold[(item+cat)%gold.size()];inventory(o,(item+perm)%4);permission(o,perm);buyable(o,0,item);
            const auto memory=goal((item*3+team)%14,o);label="one-hot catalog="+std::to_string(cat)+" item="+std::to_string(item);
            compare(map,o,memory,cat);++one_hot_cases;}
        Obs o=make_obs(map,0);o.self.gold=0xffffffffu;inventory(o,3);buyable(o,1);quantity_edges(map,o);
    }
    active_catalog=&catalogs[0].value;const auto map=make_map();
    for(int team=0;team<2;++team)for(int g=0;g<14;++g)for(int perm=0;perm<4;++perm){Obs o=make_obs(map,team);o.self.gold=217;
        inventory(o,1);permission(o,perm);buyable(o,1);const auto memory=goal(g,o);
        for(int k=0;k<a_count+2;++k){const auto type=static_cast<ActionType>(k<a_count?k:k==a_count?a_count:255);if(type==a_buy)continue;
            label="delegate="+std::to_string(k);compare(map,o,memory,0,type);++delegation_cases;}}
    require(statuses[static_cast<unsigned>(ActionBuildStatus::ok)]&&statuses[static_cast<unsigned>(ActionBuildStatus::unavailable)]&&
            statuses[static_cast<unsigned>(ActionBuildStatus::unsupported)],"required BUY statuses not covered");
    require(zero_price_rows&&high_id_rows&&max_rows>24&&network_cases,"required uncapped/catalog/network coverage missing");
    std::printf("{\"pass\":true,\"cases\":%llu,\"rows\":%llu,\"quantity_rows\":%llu,\"raw_words\":%llu,\"digest_u32\":[%u,%u],\"max_rows\":%llu,\"zero_price_rows\":%llu,\"high_id_rows\":%llu,\"network_cases\":%llu,\"one_hot_cases\":%llu,\"delegation_cases\":%llu,\"decode_cases\":%llu,\"statuses\":[%llu,%llu,%llu,%llu,%llu,%llu],\"catalog_rows\":[%llu,%llu,%llu,%llu,%llu]}\n",
        static_cast<unsigned long long>(cases),static_cast<unsigned long long>(rows),static_cast<unsigned long long>(quantity_rows),static_cast<unsigned long long>(words),
        static_cast<unsigned>(digest),static_cast<unsigned>(digest>>32),static_cast<unsigned long long>(max_rows),static_cast<unsigned long long>(zero_price_rows),
        static_cast<unsigned long long>(high_id_rows),static_cast<unsigned long long>(network_cases),static_cast<unsigned long long>(one_hot_cases),static_cast<unsigned long long>(delegation_cases),
        static_cast<unsigned long long>(decode_cases),static_cast<unsigned long long>(statuses[0]),static_cast<unsigned long long>(statuses[1]),static_cast<unsigned long long>(statuses[2]),
        static_cast<unsigned long long>(statuses[3]),static_cast<unsigned long long>(statuses[4]),static_cast<unsigned long long>(statuses[5]),static_cast<unsigned long long>(catalog_rows[0]),
        static_cast<unsigned long long>(catalog_rows[1]),static_cast<unsigned long long>(catalog_rows[2]),static_cast<unsigned long long>(catalog_rows[3]),static_cast<unsigned long long>(catalog_rows[4]));return 0;
}catch(const std::exception& e){std::fprintf(stderr,"parity failure: %s\n",e.what());return 1;}}
