#include "../src/joint_catalogue.h"
#include "../src/joint_world.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <vector>

namespace joint_catalogue=mosaic::joint_catalogue;
namespace joint_world=mosaic::joint_world;
struct Pure {
#define MOSAIC_JOINT_CATALOGUE_RUNTIME_PURE_ONLY
#include "../src/joint_catalogue_runtime.h"
#undef MOSAIC_JOINT_CATALOGUE_RUNTIME_PURE_ONLY
};
struct PureFunding {
#define MOSAIC_JOINT_CONTACT_PURE_ONLY
#include "../src/joint_contact.h"
#undef MOSAIC_JOINT_CONTACT_PURE_ONLY
};
using Key=Pure::JointCatalogueGeometryKey;
using Template=Pure::JointCatalogueTemplate;
using Plan=Pure::JointCatalogueGeometryPlan<64>;
using Selection=joint_catalogue::Selection<64>;
using Index=joint_catalogue::Index<64,1024>;
using Frame=Pure::JointCatalogueFrame;
using Result=Pure::JointCatalogueFrameResult;
using Dependencies=Pure::JointCatalogueDependencies;
unsigned checks=0;
void check(bool ok,int line){++checks;if(!ok){std::fprintf(stderr,"joint-catalogue-runtime failed line%d check%u\n",line,checks);std::abort();}}
#define CHECK(value) check(bool(value),__LINE__)

struct Member {
    joint_catalogue::Footprint physical;
    float base_hp{},base_strength{},strength{};
    int seen{-1};bool visible{},work{};
};
struct Expected {
    std::vector<joint_world::Camp> rows;
    std::vector<std::size_t> members;
    std::size_t overflow{};bool omitted{},present{};double damage{};
    std::size_t target{std::numeric_limits<std::size_t>::max()};unsigned target_source{joint_world::no_word};
};
std::int64_t distance(joint_world::Point a,const joint_catalogue::Footprint& b){
    const auto dx=std::int64_t(a.x)-b.x,dy=std::int64_t(a.y)-b.y;
    return std::max(dx<0?-dx:dx,dy<0?-dy:dy);
}
joint_world::Camp literal_row(const Member& member){
    joint_world::Camp camp{};camp.position={member.physical.x,member.physical.y};
    camp.group=unsigned(member.physical.group);camp.id=member.physical.physical_id;
    camp.health=member.base_hp;camp.damage=std::max(0.0f,member.base_strength);camp.range=member.physical.range;
    camp.knowledge=joint_world::CampKnowledge::unknown;camp.alive=true;camp.health_known=false;return camp;
}
// This is an independent literal OLD source traversal. It does not read the
// index, Selection labels, cache plan, templates or materializer's output.
Expected literal(const std::vector<Member>& members,joint_world::Point origin,int speed,int job,int capture,int attack,int epoch){
    Expected out;
    for(unsigned pass=0;pass<2;++pass)for(std::size_t id=0;id<members.size();++id){
        const auto& member=members[id];const bool selected=member.physical.group==job||member.physical.group==capture;
        if(selected!=(pass==0))continue;
        const bool incoming=distance(origin,member.physical)<=std::int64_t(member.physical.range)+std::max(0,speed);
        if(!selected&&!incoming){out.omitted=true;continue;}
        if(out.rows.size()==16){
            out.omitted=true;++out.overflow;
            if(incoming&&member.base_strength>0){out.present=true;
                out.damage+=std::ceil(std::max(double(member.base_strength),member.seen==epoch?double(member.strength):0.0))+1;}
            continue;
        }
        const auto row=unsigned(out.rows.size());out.rows.push_back(literal_row(member));out.members.push_back(id);
        if(member.visible&&distance(origin,member.physical)<=attack&&member.work&&
           (out.target==std::numeric_limits<std::size_t>::max()||
            (member.physical.group==job&&members[out.target].physical.group!=job))){
            out.target=id;out.target_source=row;
        }
    }
    return out;
}
bool same_double(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
void same_camp(const joint_world::Camp& actual,const joint_world::Camp& expected){
    CHECK(actual.position==expected.position);CHECK(actual.group==expected.group);CHECK(actual.id==expected.id);
    CHECK(actual.knowledge==expected.knowledge);CHECK(actual.alive==expected.alive);CHECK(actual.health_known==expected.health_known);
    CHECK(same_double(actual.health,expected.health));CHECK(same_double(actual.damage,expected.damage));
    CHECK(actual.range==expected.range);CHECK(actual.stun_ticks==expected.stun_ticks);
    CHECK(same_double(actual.shield.amount,expected.shield.amount));CHECK(actual.shield.ticks==expected.shield.ticks);
}
void poison(std::array<joint_world::Camp,16>& rows,std::array<std::size_t,16>& members){
    for(std::size_t n=0;n<rows.size();++n){auto& row=rows[n];
        row.position={-900-int(n),800+int(n)};row.group=900+unsigned(n);row.id=7000+unsigned(n);
        row.knowledge=joint_world::CampKnowledge::fresh_asleep;row.alive=false;row.health_known=true;
        row.health=std::numeric_limits<double>::quiet_NaN();row.damage=-27.0-double(n);row.range=-19;
        row.stun_ticks=17;row.shield.amount=900.25+double(n);row.shield.ticks=7;members[n]=9000+n;
    }
}
Key key_for(std::size_t records,joint_world::Point origin,int speed,int job,int capture){
    Key key{};key.map_identity=17;key.map_generation=2;key.records=records;key.width=key.height=32;
    key.x=origin.x;key.y=origin.y;key.raw_speed=speed;key.job=job;key.capture=capture;return key;
}
std::vector<Template> templates_for(const std::vector<Member>& members){
    std::vector<Template> result;result.reserve(members.size());
    for(const auto& member:members)result.push_back(Template::make(member.physical,member.base_hp,member.base_strength));
    return result;
}
void build(Index& index,const std::vector<Member>& members){
    // The source span is a separate temporary catalogue. Once ready, poisoning
    // and destroying it must not change the fully owned immutable index.
    std::vector<joint_catalogue::Footprint> source;source.reserve(members.size());
    for(const auto& member:members)source.push_back(member.physical);
    auto progress=index.begin(32,32,source);CHECK(!progress.failed());
    std::size_t steps=0;while(!progress.ready()){
        progress=index.buildStep(7);CHECK(!progress.failed());CHECK(++steps<100000);
    }
    for(auto& row:source)row={-1000,-1000,-1,-1,0};
}
void capture(Index& index,Selection& selected,Plan& plan,const Key& key,std::span<const Template> templates){
    plan.invalidate();CHECK(!plan.matches(key));
    CHECK(index.query({key.x,key.y},key.raw_speed,key.job,key.capture,16,selected)==joint_catalogue::QueryStatus::ok);
    // Unused output slots are legal poison. No later helper may read them.
    for(std::size_t n=selected.represented_count;n<selected.represented.size();++n)selected.represented[n]=std::numeric_limits<std::size_t>::max();
    for(std::size_t n=selected.overflow_count;n<selected.overflow.size();++n)selected.overflow[n]=std::numeric_limits<std::size_t>::max();
    CHECK(plan.capture(key,selected,templates));CHECK(plan.matches(key));
}
Result fill(const Plan& plan,std::span<const Template> templates,std::vector<Member>& source,
            std::array<joint_world::Camp,16>& rows,std::array<std::size_t,16>& ids,
            joint_world::Point origin,int attack,int job,int epoch){
    return Frame::fill(plan,templates,std::span<joint_world::Camp>(rows),std::span<std::size_t>(ids),origin,attack,job,epoch,
        [&](std::size_t id)->const Member&{CHECK(id<source.size());return source[id];},
        [&](std::size_t id){CHECK(id<source.size());return source[id].visible&&source[id].work;},
        []{return true;});
}
void equivalent(const Plan& plan,const std::vector<Template>& templates,std::vector<Member>& source,const Key& key,int attack,int epoch){
    std::array<joint_world::Camp,16> rows;std::array<std::size_t,16> ids;poison(rows,ids);
    const auto before=rows;const auto ids_before=ids;const auto expected=literal(source,{key.x,key.y},key.raw_speed,key.job,key.capture,attack,epoch);
    const auto actual=fill(plan,templates,source,rows,ids,{key.x,key.y},attack,key.job,epoch);
    CHECK(actual.complete);CHECK(actual.rows==expected.rows.size());CHECK(actual.overflow_seen==expected.overflow);
    CHECK(actual.aggregate_present==expected.present);CHECK(same_double(actual.aggregate_damage,expected.damage));
    CHECK(actual.target_member==expected.target);CHECK(actual.target_source==expected.target_source);
    CHECK(plan.omitted_any==expected.omitted);CHECK(actual.mutable_reads<=plan.contributor_count);
    for(std::size_t n=0;n<expected.rows.size();++n){same_camp(rows[n],expected.rows[n]);CHECK(ids[n]==expected.members[n]);}
    for(std::size_t n=expected.rows.size();n<rows.size();++n){same_camp(rows[n],before[n]);CHECK(ids[n]==ids_before[n]);}
    // Previously refreshed live rows must be replaced too, rather than relying
    // on a source-frame equality/cache hit to leave last input's effects behind.
    for(std::size_t n=0;n<actual.rows;++n){rows[n].alive=false;rows[n].health_known=true;rows[n].health=-1;
        rows[n].shield={71.5,2};rows[n].stun_ticks=8;rows[n].knowledge=joint_world::CampKnowledge::fresh_awake;}
    const auto reused=fill(plan,templates,source,rows,ids,{key.x,key.y},attack,key.job,epoch);
    CHECK(reused.complete);CHECK(reused.target_member==expected.target);CHECK(same_double(reused.aggregate_damage,expected.damage));
    for(std::size_t n=0;n<expected.rows.size();++n)same_camp(rows[n],expected.rows[n]);
}
std::vector<Member> varied_members(){
    std::vector<Member> result;result.reserve(48);
    for(unsigned n=0;n<48;++n){Member member{};
        member.physical={int((n*7+2)%32),int((n*11+4)%32),int(n%5),int(n%8),9000-n*37};
        member.base_hp=38.25f+float(n)*.125f;member.base_strength=n%7==0?0.0f:float(n%13)*.75f+.1f;
        member.strength=member.base_strength+2.25f;member.seen=10;member.visible=n%3!=0;member.work=n%4!=0;
        result.push_back(member);
    }
    result[0].physical={30,30,0,4,999};result[1].physical={8,8,1,2,77};
    result[2].physical={9,8,1,3,77};result[3].physical={8,9,0,4,13};
    result[1].visible=result[1].work=true;result[3].visible=result[3].work=true;return result;
}
void fresh_mutable_reuse(){
    auto members=varied_members();auto templates=templates_for(members);Index index;build(index,members);Selection selected;Plan plan;
    auto key=key_for(members.size(),{8,8},1,2,4);capture(index,selected,plan,key,templates);
    for(int epoch=9;epoch<13;++epoch)for(int attack=0;attack<6;++attack){
        CHECK(plan.matches(key));equivalent(plan,templates,members,key,attack,epoch);
        for(std::size_t n=0;n<members.size();++n){auto& member=members[n];
            member.visible=(n+unsigned(epoch)+unsigned(attack))%3!=0;member.work=(n+unsigned(epoch))%4!=0;
            member.seen=int(n%3)+9;member.strength=member.base_strength+float(attack*7+int(n%4));}
    }
    // Changing a bag/epoch/effect/HP is deliberately absent from the key. The
    // key contains only immutable identity and requested geometric selection.
    const auto unchanged=key;members[1].seen=500;members[1].strength=100000;members[1].visible=false;members[1].work=false;
    CHECK(plan.matches(unchanged));equivalent(plan,templates,members,key,3,500);
}
void keys_and_changed_selection(){
    auto members=varied_members();auto templates=templates_for(members);Index index;build(index,members);Selection selected;Plan plan;
    auto key=key_for(members.size(),{8,8},1,2,4);capture(index,selected,plan,key,templates);
    auto different=key;
    ++different.map_identity;CHECK(!plan.matches(different));different=key;++different.map_generation;CHECK(!plan.matches(different));
    different=key;++different.records;CHECK(!plan.matches(different));different=key;++different.width;CHECK(!plan.matches(different));
    different=key;++different.height;CHECK(!plan.matches(different));different=key;++different.x;CHECK(!plan.matches(different));
    different=key;++different.y;CHECK(!plan.matches(different));different=key;++different.raw_speed;CHECK(!plan.matches(different));
    different=key;++different.job;CHECK(!plan.matches(different));different=key;++different.capture;CHECK(!plan.matches(different));
    for(int x=2;x<30;x+=5)for(int speed=-1;speed<4;++speed)for(int job=-1;job<8;job+=3){
        auto changed=key_for(members.size(),{x,16},speed,job,job==2?2:4);
        capture(index,selected,plan,changed,templates);equivalent(plan,templates,members,changed,4,10);
    }
    plan.invalidate();CHECK(!plan.matches(key));
    CHECK(index.query({-1,0},1,2,4,16,selected)==joint_catalogue::QueryStatus::invalid_origin);
    CHECK(selected.represented_count==0&&selected.overflow_count==0&&!plan.matches(key));
    // A failed query has no valid labels or cache commit. Its empty output is
    // not a new observation of an empty world and cannot materialize a frame.
    std::array<joint_world::Camp,16> rows;std::array<std::size_t,16> ids;poison(rows,ids);
    const auto absent=fill(plan,templates,members,rows,ids,{8,8},3,2,10);CHECK(!absent.complete&&absent.rows==0);
    // Reject corrupt returned counts before reading any unused array payload.
    selected.represented_count=17;
    CHECK(!plan.capture(key,selected,templates));CHECK(!plan.matches(key));
}
void selected_remote_and_zero_damage(){
    std::vector<Member> members;members.reserve(40);
    for(unsigned n=0;n<40;++n){Member member{};member.physical={n%3?30:8,n%3?30:8,0,7,500-n};
        member.base_hp=100;member.base_strength=n%5?3.125f:0.0f;member.strength=10000;member.seen=33;
        member.visible=member.work=true;members.push_back(member);}
    auto templates=templates_for(members);Index index;build(index,members);Selection selected;Plan plan;
    auto key=key_for(members.size(),{8,8},0,7,7);capture(index,selected,plan,key,templates);
    CHECK(selected.represented_count==16&&selected.overflow_count==24&&plan.omitted_any);
    bool remote_overflow=false,local_zero=false;
    for(std::size_t n=0;n<selected.overflow_count;++n){const auto id=selected.overflow[n];
        if(!selected.overflow_incoming[n])remote_overflow=true;
        if(selected.overflow_incoming[n]&&members[id].base_strength==0)local_zero=true;}
    CHECK(remote_overflow&&local_zero);equivalent(plan,templates,members,key,1,33);equivalent(plan,templates,members,key,1,34);
}
void empty_active_rows_and_guard_failure(){
    auto members=varied_members();auto templates=templates_for(members);Index index;build(index,members);Selection selected;Plan plan;
    auto key=key_for(members.size(),{0,0},0,-1,-1);capture(index,selected,plan,key,templates);
    equivalent(plan,templates,members,key,0,10);
    CHECK(selected.represented_count>0);
    std::array<joint_world::Camp,16> rows;std::array<std::size_t,16> ids;poison(rows,ids);const auto before=rows;
    unsigned guards=0;
    const auto failed=Frame::fill(plan,templates,std::span<joint_world::Camp>(rows),std::span<std::size_t>(ids),
        joint_world::Point{key.x,key.y},1,key.job,10,[&](std::size_t id)->const Member&{return members[id];},
        [&](std::size_t id){return members[id].visible&&members[id].work;},[&]{++guards;return false;});
    CHECK(!failed.complete&&guards>0);CHECK(failed.rows==0);
    for(std::size_t n=0;n<rows.size();++n)same_camp(rows[n],before[n]);
    std::vector<Member> remote(1);remote[0].physical={31,31,0,7,42};remote[0].base_hp=17;remote[0].base_strength=3;
    auto remote_templates=templates_for(remote);Index remote_index;build(remote_index,remote);Selection empty_selection;Plan empty_plan;
    const auto empty_key=key_for(remote.size(),{0,0},0,-1,-1);
    capture(remote_index,empty_selection,empty_plan,empty_key,remote_templates);
    CHECK(empty_selection.represented_count==0&&empty_selection.overflow_count==0&&empty_plan.omitted_any);
    equivalent(empty_plan,remote_templates,remote,empty_key,0,10);
}
void sequential_double_order(){
    // These large finite operands are an arithmetic discriminator, not an
    // engine catalog/power claim. Pre-summing small cached weights would change
    // the specified old sequential double result at this magnitude.
    std::vector<Member> members(40);
    for(std::size_t n=0;n<members.size();++n){auto& member=members[n];member.physical={8,8,0,7,unsigned(600-n)};
        member.base_hp=100;member.base_strength=n==16?0x1p54f:2.0f;member.seen=-1;member.visible=member.work=true;}
    auto templates=templates_for(members);Index index;build(index,members);Selection selected;Plan plan;
    const auto key=key_for(members.size(),{8,8},0,7,7);capture(index,selected,plan,key,templates);
    const auto expected=literal(members,{8,8},0,7,7,1,10);
    const double grouped=std::ceil(double(members[16].base_strength))+1+23.0*3.0;
    CHECK(!same_double(expected.damage,grouped));equivalent(plan,templates,members,key,1,10);
}
template<std::size_t N> bool sequential(std::uint64_t fuel,const std::array<std::uint64_t,N>& spending){
    for(const auto cost:spending){if(cost>=fuel)return false;fuel-=cost;}return true;
}
void dependency_funding(){
    using Funding=PureFunding::JointContactFunding;
    const Funding funding{42000,16000,false};
    const auto hit=Dependencies::prepare_cost(6000,true),miss=Dependencies::prepare_cost(6000,false);
    CHECK(hit==6000&&miss==12000);
    for(std::uint64_t fuel=62000;fuel<=76000;fuel+=137){
        CHECK(funding.preparation(fuel,hit)==sequential(fuel,std::array<std::uint64_t,3>{42000,16000,6000}));
        CHECK(funding.preparation(fuel,miss)==sequential(fuel,std::array<std::uint64_t,4>{42000,16000,6000,6000}));
    }
    CHECK(!funding.preparation(64000,hit)&&funding.preparation(64001,hit));
    CHECK(!funding.preparation(70000,miss)&&funding.preparation(70001,miss));
    const std::uint64_t after_paid_query=70001-6000;
    CHECK(funding.preparation(after_paid_query,hit));
    CHECK(!funding.preparation(after_paid_query,miss)); // Charging that query again would reject the real residual tail.
    CHECK(sequential(after_paid_query,std::array<std::uint64_t,3>{6000,16000,42000}));
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    CHECK(Dependencies::prepare_cost(0,false)==maximum&&Dependencies::prepare_cost(0,true)==maximum);
    CHECK(Dependencies::prepare_cost(maximum/2+1,false)==maximum);
    CHECK(!funding.preparation(maximum,Dependencies::prepare_cost(maximum/2+1,false)));
}
void publication_failure_closure(){
    auto members=varied_members();auto templates=templates_for(members);Index index;build(index,members);Selection selected;Plan plan;
    const auto a=key_for(members.size(),{8,8},1,2,4);auto b=a;++b.x;
    capture(index,selected,plan,a,templates);CHECK(plan.matches(a)&&!plan.matches(b));
    const PureFunding::JointContactFunding funding{42000,16000,false};
    // A rejected B admission has not touched the borrowed Selection. Keeping A
    // is legitimate here, unlike retaining A across an actual B rewrite.
    CHECK(!funding.preparation(70000,Dependencies::prepare_cost(6000,false)));CHECK(plan.matches(a));
    plan.invalidate();CHECK(!plan.matches(a));
    CHECK(index.query({-1,0},b.raw_speed,b.job,b.capture,16,selected)==joint_catalogue::QueryStatus::invalid_origin);
    CHECK(!plan.matches(a)&&!plan.matches(b));
    capture(index,selected,plan,a,templates);
    plan.invalidate();
    CHECK(index.query({b.x,b.y},b.raw_speed,b.job,b.capture,16,selected)==joint_catalogue::QueryStatus::ok);
    CHECK(plan.capture(b,selected,templates));CHECK(plan.matches(b)&&!plan.matches(a));
    // A query/commit that subsequently overruns its declaration is revoked;
    // neither its B payload nor the old A key is still published.
    plan.invalidate();CHECK(!plan.matches(a)&&!plan.matches(b));
}
int main(){
    fresh_mutable_reuse();keys_and_changed_selection();selected_remote_and_zero_damage();empty_active_rows_and_guard_failure();sequential_double_order();
    dependency_funding();publication_failure_closure();
    std::printf("joint-catalogue-runtime: %u checks passed\n",checks);return 0;
}
