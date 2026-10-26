"""Run production observation/layout methods against a synthetic fuel double.

This is a source-bound budget/visibility regression, not a native fuel proof.
Only the called SDK surface is modeled; the observation algorithm, visibility
masking, scalar reset, and snapshot types come from the production headers.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def method(source, signature):
    start = source.index(signature)
    # A function's default argument may itself be `{}`. Start at the body,
    # not at the first brace in its declaration.
    if signature.startswith("struct "):
        opening = source.index("{", start)
    else:
        match = re.search(r"\)\s*(?:const\s*)?\{", source[start:])
        if not match:
            raise RuntimeError(f"missing production body: {signature}")
        opening = start + match.end() - 1
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PRELUDE = r'''
#define MODEL4_OBSERVER_NO_SDK
#include "OBSERVER_HEADER"
#include "RUNTIME_HEADER"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <variant>
struct Simulation {
    unsigned fuel=100000,round=10,minimum=100000;
    int setup_cost=11400,lifecycle_cost=1500,member_cost=500;
    int lifecycle_reads=0,member_reads=0,hidden_reads=0,actions=0,finishes=0;
    int champion_packets=0,camp_packets=0;
    int fuel_reads=0,fail_fuel_read=-1;
    unsigned fail_fuel_value=0;
    std::vector<unsigned> fuel_trace;
    unsigned dynamic_before=0;
    bool jump=false;
    void spend(unsigned cost){if(cost>fuel)std::abort();fuel-=cost;minimum=std::min(minimum,fuel);}
    void dynamic(bool visible,int cost){
        if(!visible){++hidden_reads;std::abort();}
        if(!dynamic_before)dynamic_before=fuel;
        spend(unsigned(cost));
    }
} sim;
namespace cambc {
using handle_champion_t=int;
using handle_camp_t=int;
struct vec2 {int x=0,y=0;bool operator==(const vec2&)const=default;};
enum class team {ally,enemy};
class champion_state {
    unsigned bits_=0;
public:
    constexpr champion_state(unsigned bits=0):bits_(bits){}
    static constexpr champion_state from_bits_retain(unsigned bits){return champion_state(bits);}
    constexpr bool contains(champion_state other)const{return (bits_&other.bits_)==other.bits_;}
    static const champion_state recalling,submitting_gems,teleporting;
};
inline constexpr champion_state champion_state::recalling{1u<<1},champion_state::submitting_gems{1u<<2},
    champion_state::teleporting{1u<<12};
struct State {bool is_dead()const{return false;}};
struct Item {
    int key;float passive_magnitude()const{return 0;}
    static const Item monster_emblem_of_strength,jungle_emblem_of_health;
};
const Item Item::monster_emblem_of_strength{0},Item::jungle_emblem_of_health{1};
using item_type=Item;
struct Items {unsigned operator[](Item)const{return 0;}};
struct bitboard {
    enum Kind {visible,empty,spawn}kind=empty;
    bool is_set(vec2 p)const{return kind==visible?p.x%2==0:kind==spawn&&p==vec2{0,0};}
};
struct Actor {
    explicit operator handle_champion_t()const{return 7;}
    bool is_alive()const{return true;}
    vec2 position()const{return {0,0};}
    float health()const{sim.spend(4000);return 1000;}
    float max_health()const{return 1000;}
    unsigned gem_count()const{return 120;}
    unsigned gold_count()const{return 0;}
    champion_state state()const{return {};}
    int remaining_channel_time()const{return 0;}
    Items item_count()const{return {};}
    float strength()const{return 100;}
    float focus()const{return 0;}
    int attack_range()const{return 1;}
};
Actor& self(){static Actor actor;return actor;}
std::uint64_t fuel_remaining(){
    if(++sim.fuel_reads==sim.fail_fuel_read){sim.fuel=sim.fail_fuel_value;sim.minimum=std::min(sim.minimum,sim.fuel);}
    sim.fuel_trace.push_back(sim.fuel);return sim.fuel;
}
unsigned round_number(){return sim.round;}
unsigned global_submitted_gems(team){return 0;}
namespace detail {
struct champion_info {
    std::int32_t team,type,x,y;
    float health,max_health,strength,focus;
    std::int32_t move_speed,attack_range,state;
};
struct camp_state {
    std::int32_t x,y;
    float health,max_health,strength,focus;
    std::int32_t cooldowns[2];
};
static_assert(sizeof(champion_info)==44&&sizeof(camp_state)==32);
int sdk_champion(std::uint32_t handle,champion_info* out){
    if(handle!=7)return 43;
    ++sim.champion_packets;sim.spend(4000);
    *out={0,7,0,0,1000,1000,100,0,1,1,0};return 0;
}
int sdk_camp(std::uint32_t handle,camp_state* out){
    sim.dynamic(handle%2==0,sim.member_cost);++sim.camp_packets;
    *out={int(handle),2,50,100,0,0,{0,0}};return 0;
}
void check(int code){if(code)throw std::runtime_error("synthetic SDK packet error");}
}
namespace constants {
constexpr int max_rounds=3000,capture_time=5,recall_time=5,submit_gem_base_duration=0,submit_gem_minimum_duration=1;
constexpr unsigned winning_gems=1000;
constexpr double submit_gem_duration_multiplier=.1;
constexpr float spawn_healing=.1f;
}
namespace map {
bitboard spawn_region(team){return {bitboard::spawn};}
struct camp_group_alive{};struct camp_group_reward_available{};struct camp_group_respawning{int timer;};
struct Member {
    vec2 spawn{};
    explicit operator handle_camp_t()const{return spawn.x;}
    int kind()const{return 0;}
    vec2 spawn_position()const{return spawn;}
    State state()const{sim.dynamic(spawn.x%2==0,sim.member_cost);++sim.member_reads;return {};}
    float health()const{sim.dynamic(spawn.x%2==0,sim.member_cost);return 50;}
    float strength()const{sim.dynamic(spawn.x%2==0,sim.member_cost);return 0;}
};
struct camp_group {
    std::vector<Member> values;
    const auto& camps()const{return values;}
    auto begin()const{return values.begin();}auto end()const{return values.end();}
    std::variant<camp_group_alive,camp_group_reward_available,camp_group_respawning> state()const{
        sim.dynamic(!values.empty()&&values[0].spawn.x%2==0,sim.lifecycle_cost);++sim.lifecycle_reads;
        if(sim.jump)++sim.round;
        return camp_group_alive{};
    }
};
}
namespace vision {
bitboard visible_tiles(){return {bitboard::visible};}
bitboard champion_tiles(team){return {};}
std::vector<Actor> champions(team side){if(side==team::enemy)sim.spend(unsigned(sim.setup_cost-4000));return {};}
bitboard altar_lock_tiles(team){sim.spend(250);return {};}
}
}
namespace plan4 {
class OnlineObserver {
    struct Metadata {cambc::map::camp_group sdk;Group group;std::vector<MemberObservation> members;std::vector<int> ranges;};
    std::vector<Metadata> groups_;
    std::vector<Altar> altars_;
    std::uint64_t fuel_floor_=65000;
    mutable ObserverDiagnostics diagnostics_{};
    Point own_spawn_{};
    bool initialized_=true,truncated_=false;
    bool initialize(){return false;}
    static Point point(cambc::vec2 p){return {p.x,p.y};}
    static cambc::vec2 tile(Point p){return {p.x,p.y};}
    static int chebyshev(Point a,Point b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
'''

SETUP = r'''
public:
    explicit OnlineObserver(int count=2){
        for(int n=0;n<count;++n){Metadata row;row.group.id=100+n;row.group.position={n+2,2};
            row.group.full_health=row.group.health=100;row.group.full_clear_rounds=2;
            row.group.lifecycle=Lifecycle::unknown;row.group.provenance.status=Status::unknown;
            row.group.health_provenance.status=Status::unknown;
            row.sdk.values.push_back({{n+2,2}});row.members.push_back({100,0,false});row.ranges.push_back(1);
            groups_.push_back(row);}
        for(int n=0;n<2;++n){Altar altar;altar.id=n;altar.position={2*n+2,4};altar.altar_position={2*n+2,5};altars_.push_back(altar);}
    }
    std::uint64_t floor()const{return fuel_floor_;}
    void fixture_recall(){groups_[0].members[0].incoming=2;groups_[0].ranges[0]=3;}
    template<class Visitor>void fixture_provider(Visitor visit){
        const cambc::bitboard snapshot{cambc::bitboard::visible};bool truncated=false;
        Provider provider{groups_[0],snapshot,&truncated,*this};visit(provider,truncated);
    }
'''

TEST = r'''
};
} // namespace plan4
struct Caller {
    void macro_finish(){++sim.finishes;}
    bool commit(unsigned started_round){CALLER_GUARD ++sim.actions;return false;}
};
int checks=0;
void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
void reset(unsigned fuel=100000){sim={};sim.fuel=sim.minimum=fuel;}
bool metadata(const plan4::Information& info,int count){return info.group_count==count&&info.altar_count==2&&
    !info.public_layout_truncated&&info.groups[0].id==100&&info.altars[1].id==1;}
bool same_diagnostics(const plan4::ObserverDiagnostics& a,const plan4::ObserverDiagnostics& b){
    return std::tie(a.stop_stage,a.stop_group,a.stop_member,a.groups,a.visited,a.collection_fuel,
        a.stop_floor,a.stop_fuel,a.stop_margin)==std::tie(b.stop_stage,b.stop_group,b.stop_member,b.groups,
        b.visited,b.collection_fuel,b.stop_floor,b.stop_fuel,b.stop_margin);
}
plan4::ObserverDiagnostics cached_diagnostics(const plan4::OnlineObserver& observer){
    const int fuel_reads=sim.fuel_reads,lifecycle=sim.lifecycle_reads,members=sim.member_reads,
        own_packets=sim.champion_packets,camp_packets=sim.camp_packets;
    const auto result=observer.diagnostics();
    check(sim.fuel_reads==fuel_reads&&sim.lifecycle_reads==lifecycle&&sim.member_reads==members&&
        sim.champion_packets==own_packets&&sim.camp_packets==camp_packets,"diagnostics getter uses only cached fields");
    return result;
}
void expect_stop(const plan4::OnlineObserver& observer,plan4::ObserverStage stage,int group,int member,
                 unsigned fuel,unsigned floor,unsigned margin,int visited,unsigned collection,int groups=2){
    const auto value=cached_diagnostics(observer);
    const bool matches=value.stop_stage==int(stage)&&value.stop_group==group&&value.stop_member==member&&value.groups==groups&&
        value.visited==visited&&value.collection_fuel==collection&&value.stop_fuel==fuel&&
        value.stop_floor==floor&&value.stop_margin==margin;
    if(!matches)std::fprintf(stderr,"diagnostic actual stage/group/member=%d/%d/%d groups/visited=%d/%d collection=%llu fuel/floor/margin=%lld/%llu/%u expected=%d/%d/%d groups/visited=%d/%d collection=%u fuel/floor/margin=%u/%u/%u\n",
        value.stop_stage,value.stop_group,value.stop_member,value.groups,value.visited,
        static_cast<unsigned long long>(value.collection_fuel),static_cast<long long>(value.stop_fuel),
        static_cast<unsigned long long>(value.stop_floor),value.stop_margin,int(stage),group,member,groups,visited,collection,fuel,floor,margin);
    check(matches,"first failed guard records its exact stage/public group/member/fuel/floor/margin/prefix");
}
int diagnostic_distance(plan4::Point a,plan4::Point b,const void*){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
void diagnostic_cases(){
    using namespace plan4;
    static_assert(int(ObserverStage::none)==0&&int(ObserverStage::admission)==1&&int(ObserverStage::initialization)==2&&
        int(ObserverStage::own)==3&&int(ObserverStage::public_groups)==4&&int(ObserverStage::public_altars)==5&&
        int(ObserverStage::collection)==6&&int(ObserverStage::group)==7&&int(ObserverStage::visibility)==8&&
        int(ObserverStage::lifecycle)==9&&int(ObserverStage::members)==10&&int(ObserverStage::occupancy)==11&&
        int(ObserverStage::recall)==12&&int(ObserverStage::altar_admission)==13&&int(ObserverStage::altars)==14);
    struct Fault {int read;ObserverStage stage;int group,member;unsigned fuel,floor,margin;int visited;};
    const Fault faults[]{
        {1,ObserverStage::admission,-1,-1,51999,40000,12000,0},
        {2,ObserverStage::own,-1,-1,47000,40000,8000,0},
        {3,ObserverStage::public_groups,100,-1,42000,40000,3000,0},
        {5,ObserverStage::public_altars,-1,-1,41000,40000,2000,0},
        {8,ObserverStage::collection,-1,-1,70599,62600,8000,0},
        {9,ObserverStage::group,100,-1,68599,62600,6000,0},
        {10,ObserverStage::visibility,100,0,64599,62600,2000,1},
        {12,ObserverStage::lifecycle,100,-1,64599,62600,2000,1},
        {13,ObserverStage::members,100,-1,64599,62600,2000,1},
        {14,ObserverStage::members,100,0,64599,62600,2000,1},
        {15,ObserverStage::members,100,0,64599,62600,2000,1},
        {16,ObserverStage::occupancy,100,0,64599,62600,2000,1},
        {20,ObserverStage::altar_admission,-1,-1,66599,62600,4000,2},
        {21,ObserverStage::altars,-1,-1,64599,62600,2000,2},
    };
    auto info=std::make_unique<Information>();const TravelLookup travel{diagnostic_distance,nullptr};
    for(const auto fault:faults){
        reset();sim.fail_fuel_read=fault.read;sim.fail_fuel_value=fault.fuel;OnlineObserver observer;
        const bool snapshot=observer.observe_into(*info,travel,40000);
        check(snapshot==(int(fault.stage)>=int(ObserverStage::collection)),"diagnostics preserve early/public-complete caller return behavior");
        check(sim.fuel_trace.size()>=std::size_t(fault.read)&&sim.fuel_trace[fault.read-1]==fault.fuel,
            "fault injection lands on the unchanged existing fuel-query ordinal");
        expect_stop(observer,fault.stage,fault.group,fault.member,fault.fuel,fault.floor,fault.margin,
            fault.visited,fault.read>=8?88600:0);
        const auto first=cached_diagnostics(observer);
        sim.fuel=41000;Menu menu{};menu.single_count=menu.count=1;menu.jobs[0].group=0;
        (void)observer.layout_for_menu(*info,menu);
        check(same_diagnostics(first,cached_diagnostics(observer)),"outside layout guards cannot replace the first observation stop");
    }
    reset();OnlineObserver recall;recall.fixture_recall();sim.fail_fuel_read=18;sim.fail_fuel_value=64599;
    check(recall.observe_into(*info,travel,40000),"recall guard stop preserves the public snapshot");
    expect_stop(recall,ObserverStage::recall,100,0,64599,62600,2000,1,88600);
    reset();OnlineObserver reused;sim.fail_fuel_read=9;sim.fail_fuel_value=68599;
    check(reused.observe_into(*info,travel,40000),"first collection records a group admission stop");
    expect_stop(reused,ObserverStage::group,100,-1,68599,62600,6000,0,88600);
    const auto window=cached_diagnostics(reused);
    check(window.collection_fuel-window.stop_floor-window.stop_margin==20000,
        "26k allowance leaves the unchanged6k group admission a20k nominal window");
    reset();++sim.round;check(reused.observe_into(*info,travel,40000),"same observer resumes under a fresh observation");
    const auto reset_value=cached_diagnostics(reused);
    check(reset_value.stop_stage==0&&reset_value.stop_group==-1&&reset_value.stop_member==-1&&reset_value.stop_fuel==-1&&
        reset_value.stop_floor==0&&reset_value.stop_margin==0&&reset_value.groups==2&&reset_value.visited==2&&
        reset_value.collection_fuel==88600,"new observation resets cause and publishes a fresh complete prefix");
    const auto menu=make_menu(*info);
    check(!menu.truncated&&menu.single_count>0&&menu.count>menu.single_count,"successful diagnostic caller retains the real menu alternatives");
    sim.fuel=41000;Menu layout_menu{};layout_menu.single_count=layout_menu.count=1;layout_menu.jobs[0].group=0;
    check(reused.layout_for_menu(*info,layout_menu).truncated,"outside layout guard still rejects insufficient margin");
    check(same_diagnostics(reset_value,cached_diagnostics(reused)),"stage-none layout failure cannot fabricate an observation stop");
    reset();OnlineObserver empty(0);check(empty.observe_into(*info,travel,40000),"empty public roster completes its observation");
    const auto empty_value=cached_diagnostics(empty);
    check(empty_value.groups==0&&empty_value.visited==0&&empty_value.collection_fuel==88600&&empty_value.stop_stage==0&&
        empty_value.stop_fuel==-1,"empty observation has a successful zero-group diagnostic prefix");
    struct CallerFloor {unsigned entry,requested,collection,floor,fault;};
    const CallerFloor caller_floors[]{
        {76000,1,64600,40000,45999},
        {100000,65000,88600,65000,70999},
        {100000,80000,88600,80000,85999},
    };
    for(const auto caller:caller_floors){
        reset(caller.entry);OnlineObserver protected_floor;sim.fail_fuel_read=9;sim.fail_fuel_value=caller.fault;
        check(protected_floor.observe_into(*info,travel,caller.requested),"caller-floor stop still returns complete public metadata");
        expect_stop(protected_floor,ObserverStage::group,100,-1,caller.fault,caller.floor,6000,0,caller.collection);
        check(protected_floor.floor()==caller.floor&&sim.minimum>=caller.floor&&info->observation_truncated&&!info->own.recall_safe,
            "26k collection cannot lower the40k clamp or explicit65k/80k caller reserve");
        const auto guarded_menu=make_menu(*info);
        check(guarded_menu.truncated&&guarded_menu.single_count>0&&guarded_menu.count>guarded_menu.single_count,
            "caller-floor censor preserves the real menu and its fallback alternatives");
    }
    reset(66000);OnlineObserver direct;
    direct.fixture_provider([&](auto& provider,bool& truncated){
        const int before=sim.fuel_reads;check(!provider.visible()&&truncated,"provider rejects its first low-budget visibility test");
        check(sim.fuel_reads==before+1,"failed provider budget performs the original single fuel query");
        expect_stop(direct,ObserverStage::visibility,100,0,66000,65000,2000,0,0);
        const auto first=cached_diagnostics(direct);sim.fuel=60000;
        check(!provider.can_continue()&&provider.read_lifecycle().state==Lifecycle::unknown,
            "later member/lifecycle failures retain their existing return behavior");
        check(sim.fuel_reads==before+3&&same_diagnostics(first,cached_diagnostics(direct)),
            "later failure queries run once each and cannot overwrite first cause");
    });
    std::printf("observer diagnostics: %zu actual guard faults plus recall/reset/empty/cached-getter/first-cause/layout and40k/65k/80k caller-floor cases passed\n",
        std::size(faults));
}
int main(){
    using namespace plan4;
    auto info=std::make_unique<Information>();
#ifdef HISTORICAL
    reset();OnlineObserver old;
    check(!old.observe_into(*info,{},40000),"historical11400 setup already blocks public-copy3000 admission");
    check(info->observation_truncated&&sim.lifecycle_reads==0&&sim.member_reads==0,
        "historical11400 setup cannot reach dynamic observation");
    check(old.floor()==86000&&sim.fuel==88600,"historical11400 setup leaves only2600 phase headroom");
    reset();sim.setup_cost=11000;OnlineObserver narrow;
    check(narrow.observe_into(*info,{},40000)&&metadata(*info,2),"historical11000 setup completes public metadata at3000 headroom");
    check(info->observation_truncated&&sim.lifecycle_reads==0&&sim.member_reads==0,
        "historical14k cap starts before setup and then refuses8000 dynamic admission");
    check(narrow.floor()==86000&&sim.fuel==89000,"historical public-complete variant still cannot enter dynamic phase");
    reset(76000);OnlineObserver clamped;
    check(!clamped.observe_into(*info,{},40000)&&sim.fuel==76000&&clamped.floor()==65000,
        "historical hard floor ignores caller40000 and rejects before setup");
    std::printf("observer budget historical contrast: %d checks; synthetic fuel only\n",checks);
#else
    reset();OnlineObserver normal;
    check(normal.observe_into(*info,{},40000),"actual observer returns complete supplied snapshot");
    check(metadata(*info,2)&&!info->observation_truncated,"public metadata completes before separate dynamic phase");
    check(sim.dynamic_before==88600,"11400 setup does not spend dynamic26k allowance");
    check(sim.lifecycle_reads==1&&sim.member_reads==1&&sim.hidden_reads==0,"visible group queried once; hidden group never queried");
    check(sim.champion_packets==1&&sim.camp_packets==1,"own fields share one checked packet; living camp fields share one after its dead gate");
    check(info->groups[0].lifecycle==Lifecycle::living&&info->groups[0].health==50&&
        info->groups[0].health_provenance.status==Status::exact_now,"real pure observation publishes current visible HP");
    check(info->groups[1].lifecycle==Lifecycle::unknown&&info->groups[1].health==100&&
        info->groups[1].health_provenance.status==Status::unknown,"hidden group keeps full conservative unknown HP");
    check(sim.dynamic_before-sim.fuel<=26000&&sim.minimum>=40000,"dynamic work stays within26k phase/caller floor");
    check(info->altars[0].lock_known&&info->altars[1].lock_known,"fresh visible altar masks can resolve locks after groups");
    const std::vector<unsigned> baseline_fuel_queries{100000,96000,88600,88600,88600,88600,88600,88600,
        88600,88600,88600,88600,87100,87100,87100,86100,86100,86100,86100,86100,85600,85600};
    check(sim.fuel_trace==baseline_fuel_queries,"successful caller preserves the baseline22 fuel queries in exact order/value");
    const auto complete=cached_diagnostics(normal);
    check(complete.stop_stage==0&&complete.stop_group==-1&&complete.stop_member==-1&&complete.groups==2&&complete.visited==2&&
        complete.collection_fuel==88600&&complete.stop_fuel==-1&&complete.stop_floor==0&&complete.stop_margin==0,
        "successful observation reports a complete prefix with no fabricated stop");

    Menu menu{};menu.single_count=menu.count=1;menu.jobs[0].group=0;
    const auto layout_at=[&](OnlineObserver& observer,unsigned fuel){
        sim.spend(sim.fuel-fuel);
        const int lifecycle_reads=sim.lifecycle_reads,member_reads=sim.member_reads;
        auto frame=observer.layout_for_menu(*info,menu);
        check(sim.lifecycle_reads==lifecycle_reads&&sim.member_reads==member_reads&&sim.hidden_reads==0,
            "immutable layout performs no live member or lifecycle queries");
        return frame;
    };
    const auto layout=layout_at(normal,65000);
    check(!layout.truncated&&layout.group_count==1&&layout.member_count==1&&layout.groups[0].complete,
        "later layout uses caller40000 after dynamic phase floor62600 has expired");
    check(normal.floor()==40000&&layout.members[0].group_id==100&&layout.members[0].spawn==Point{2,2},
        "caller reserve is restored and public composition remains exact");
    const auto low_layout=layout_at(normal,45000);
    check(low_layout.truncated&&low_layout.group_count==0&&low_layout.member_count==0,
        "layout still rejects genuinely insufficient caller reserve plus startup margin");

    reset();OnlineObserver explicit_reserve;
    check(explicit_reserve.observe_into(*info,{},65000)&&explicit_reserve.floor()==65000,
        "successful observation restores explicit65000 reserve rather than lowering it");
    const auto explicit_layout=layout_at(explicit_reserve,72000);
    check(!explicit_layout.truncated&&explicit_layout.group_count==1&&explicit_layout.member_count==1,
        "layout honors explicit65000 reserve when its margin fits");
    check(layout_at(explicit_reserve,70000).truncated,"layout retains explicit65000 startup protection");

    reset();OnlineObserver default_reserve;
    check(default_reserve.observe_into(*info)&&default_reserve.floor()==65000,
        "successful default observation restores conservative65000 reserve");
    ++sim.round;
    const auto stale_layout=layout_at(default_reserve,72000);
    check(stale_layout.truncated&&stale_layout.group_count==0&&stale_layout.member_count==0,
        "immutable layout still rejects a stale observation round");

    reset(76000);OnlineObserver lower;
    check(lower.observe_into(*info,{},40000)&&metadata(*info,2),"caller40000 is honored below old65000 reserve");
    check(lower.floor()==40000&&sim.fuel<65000&&sim.minimum>=50600&&sim.lifecycle_reads==1,
        "lower common reserve permits bounded actual dynamic observation");
    reset(76000);OnlineObserver ordinary;
    check(!ordinary.observe_into(*info)&&ordinary.floor()==65000&&sim.fuel==76000&&sim.lifecycle_reads==0,
        "omitted argument retains conservative65000 default and performs no reads below admission");
    reset(76000);OnlineObserver minimum;
    check(minimum.observe_into(*info,{},1)&&minimum.floor()==40000&&sim.minimum>=50600,
        "requested reserve below40000 cannot bypass minimum common floor");

    reset(59000);OnlineObserver shortage;
    check(shortage.observe_into(*info,{},40000)&&metadata(*info,2),"insufficient dynamic budget still returns complete public metadata");
    check(info->observation_truncated&&!info->own.recall_safe&&shortage.floor()==40000,
        "dynamic admission shortage explicitly truncates and withdraws recall assurance");
    check(sim.lifecycle_reads==0&&sim.member_reads==0&&info->groups[0].lifecycle==Lifecycle::unknown&&
        info->groups[0].health_provenance.status==Status::unknown,"dynamic stop does not relabel skipped current stock or HP");
    check(sim.fuel==47600&&sim.minimum>=40000,"shortage exits before hidden queries and reserve breach");

    reset();sim.lifecycle_cost=2500;sim.member_cost=1000;OnlineObserver prefix(16);
    check(prefix.observe_into(*info,{},40000)&&metadata(*info,16),"large dynamic prefix preserves all public metadata");
    check(info->observation_truncated&&sim.lifecycle_reads>0&&sim.lifecycle_reads<8&&sim.hidden_reads==0,
        "actual provider guards stop bounded dynamic prefix without hidden reads");
    check(sim.dynamic_before-sim.fuel<=26000&&sim.minimum>=sim.dynamic_before-26000&&prefix.floor()==40000,
        "dynamic cap remains enforced while its caller reserve is restored on a truncated return");
    check(info->groups[14].provenance.status==Status::unknown&&info->groups[14].health_provenance.status==Status::unknown,
        "unvisited suffix remains unknown rather than previous exact evidence");
    const auto prefix_layout=prefix.layout_for_menu(*info,menu);
    check(!prefix_layout.truncated&&info->observation_truncated&&!info->own.recall_safe&&
        info->groups[14].provenance.status==Status::unknown,
        "public layout availability does not clear a stopped dynamic observation or certify its unknown suffix");
    expect_stop(prefix,ObserverStage::group,109,-1,66100,62600,6000,9,88600,16);

    reset();sim.jump=true;OnlineObserver stale;
    const unsigned started_round=sim.round;stale.observe_into(*info,{},40000);
    Caller caller;check(caller.commit(started_round)&&sim.actions==0&&sim.finishes==1,
        "actual macro_decide owner-round guard refuses an action after synthetic SDK suspension");
    diagnostic_cases();
    std::printf("observer budget: %d source-bound checks; synthetic fuel only\n",checks);
#endif
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--node", required=True)
    parser.add_argument("--contrast-source", type=Path, help="Optional retained old observer.h; never rewritten")
    args = parser.parse_args()
    cache = (ROOT / ".cache/models-tests").resolve()
    cache.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="plan4-observer-budget-", dir=cache)).resolve()
    if not scratch.is_relative_to(cache):
        raise RuntimeError("scratch escaped repository cache")
    succeeded = False
    try:
        current = ROOT / "bots/models/common/observer.h"
        diagnostics_getter = method(current.read_text(), "const ObserverDiagnostics& diagnostics()")
        macro = (ROOT / "bots/models/common/macro-methods.h").read_text()
        guard = "if (cambc::round_number() != started_round) { macro_finish(); return true; }"
        if guard not in macro:
            raise RuntimeError("production caller owner-round guard changed; update focused harness deliberately")
        targets = [("current", current)]
        if args.contrast_source:
            targets.append(("historical", args.contrast_source.resolve()))
        for label, path in targets:
            source = path.read_text()
            cpp, wasm = scratch / f"{label}.cpp", scratch / f"{label}.wasm"
            cpp.write_text(PRELUDE.replace("OBSERVER_HEADER", current.as_posix())
                .replace("RUNTIME_HEADER", (ROOT / "bots/models/common/model-runtime.h").as_posix())
                + method(source, "struct ModelLayoutStorage") + ";\n"
                + "std::unique_ptr<ModelLayoutStorage> model_layout_;\n"
                + method(source, "bool has_budget(") + "\n" + method(source, "struct Provider") + ";\n"
                + SETUP + diagnostics_getter + "\n" + method(source, "runtime::PublicLayoutFrame layout_for_menu(") + "\n"
                + method(source, "bool observe_into(") + TEST.replace("CALLER_GUARD", guard))
            eh = Path(args.compiler).resolve().parents[1] / "share/wasi-sysroot/lib/wasm32-wasip1/eh"
            command = [args.compiler, "-std=c++20", "-O3", "-Wall", "-Wextra", "-Werror",
                "-fwasm-exceptions", "-mllvm", "-wasm-use-legacy-eh=false", "--no-wasm-opt", f"-L{eh}", "-lunwind"]
            if label == "historical":
                command.append("-DHISTORICAL")
            subprocess.run(command + [str(cpp), "-o", str(wasm)], check=True, cwd=ROOT)
            host = 'import fs from "node:fs";import {WASI} from "node:wasi";const w=new WASI({version:"preview1",args:[],env:{},preopens:{},returnOnExit:true});const m=await WebAssembly.compile(fs.readFileSync(process.argv[1]));const i=await WebAssembly.instantiate(m,w.getImportObject());process.exitCode=w.start(i);'
            subprocess.run([args.node, "--no-warnings", "--input-type=module", "-e", host, str(wasm)], check=True, cwd=ROOT)
        succeeded = True
    finally:
        # A failed generated case is retained for diagnosis, not erased as cleanup.
        if succeeded:
            shutil.rmtree(scratch)
            if scratch.exists():
                raise RuntimeError("scratch cleanup failed")
            print("Verified observer-budget scratch cleanup", flush=True)
        else:
            print(f"Retained failed observer-budget harness: {scratch}", flush=True)


if __name__ == "__main__":
    main()
