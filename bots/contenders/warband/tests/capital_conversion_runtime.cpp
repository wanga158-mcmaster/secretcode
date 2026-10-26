// Real conversion_runtime wrapper with a host-free SDK boundary. This tests
// ownership/receipt composition; it is neither a native game nor a fuel proof.
#define WB_CONVERSION_NATIVE_HOST_FREE
#include "capital_conversion_native.h"
#include <array>
#include <cstdio>
#include <memory>

namespace wb::capital::conversion_native {
struct SdkHost {
    int who{4},now{10};unsigned available{100000};Packet state{};
    unsigned observations{},enemy_reads{},moves{},starts{};bool locked{};
    SdkHost() {
        state.own.position={2,2};state.own.bag=14;state.own.channel=tribute::Channel::None;
        state.own.alive=state.own.can_move=state.own.submit_permitted=true;
        state.hp=state.max_hp=100;state.move_speed=state.attack_range=state.submit_range=1;
        state.queried_ticks=2;
    }
    int owner(){return who;}int round(){return now;}unsigned fuel(){return available;}
    bool observe(Epoch,const Geometry&,Packet& out,bool enemies) {
        ++observations;out=state;if(enemies){++enemy_reads;out.enemies_complete=true;}return true;
    }
    bool altar(Epoch,const Packet& p,tribute::Point point,tribute::Context& c) {
        c.altar_known=true;c.adjacent=distance(p.own.position,point)<=p.submit_range;c.unlocked=!locked;return true;
    }
    bool move_legal(Epoch,const Packet& p,tribute::Point landing,const Geometry& g) {
        return p.own.can_move&&g.root_history_known&&!g.exclusive_cast_used&&distance(p.own.position,landing)==1;
    }
    bool move(tribute::Point landing) {
        ++moves;state.own.position=landing;state.own.can_move=state.own.can_attack=false;return true;
    }
    void submit(tribute::Point) {
        ++starts;state.own.channel=tribute::Channel::Submit;state.own.submit_started=true;
        state.own.remaining_known=true;state.own.remaining_ticks=state.queried_ticks;
    }
};
}
namespace fixture {
inline wb::capital::conversion_native::SdkHost* actual_host{};
}
// These declarations are the only cambc symbols used by the actual wrapper.
namespace cambc {
using handle_champion_t=int;
enum class team {enemy};
namespace constants {
inline constexpr int respawn_time=3,max_rounds=10000,winning_gems=1024;
inline constexpr int submit_gem_minimum_duration=1,submit_gem_base_duration=1;
inline constexpr double submit_gem_duration_multiplier=0.05;
}
struct Champion {
    int id{};explicit operator handle_champion_t() const{return id;}
    std::uint64_t gem_count() const{return fixture::actual_host->state.own.bag;}
};
inline Champion self(){return {fixture::actual_host->who};}
inline std::array<Champion,1> champions(team){return {{{8}}};}
}
#include "capital_conversion_runtime.h"

namespace n=wb::capital::conversion_native;
namespace t=wb::capital::tribute;
namespace r=wb::capital::conversion_refinement;
namespace runtime=wb::capital::conversion_runtime;
unsigned checks{},failures{};
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
const std::array<t::Point,1> altars{{{4,2}}};
const std::array<int,1> enemies{{8}};
bool walkable(void*,t::Point){return true;}
n::Geometry geometry(n::Epoch e){return {e,altars,true,nullptr,walkable,enemies,true,true,false};}
runtime::Memory& reset() {
    auto& m=runtime::memory();std::destroy_at(&m);std::construct_at(&m);fixture::actual_host=&m.host;return m;
}
struct HooksState {unsigned prepares{},executes{};};
r::Plan prepare(void* opaque,n::Epoch e,const n::Packet& p,const t::Context& c,const n::Geometry& g,bool paid) {
    ++static_cast<HooksState*>(opaque)->prepares;
    const std::array<t::Point,1> landings{{{p.own.position.x,p.own.position.y+1}}};
    r::Options options;options.admitted_destinations=landings;
    return r::prepare(p,c,g,e,paid,runtime::memory().host,options);
}
bool execute(void* opaque,r::Plan& plan,n::Epoch e,const n::Packet& p,const t::Context& c,const n::Geometry& g) {
    ++static_cast<HooksState*>(opaque)->executes;
    const auto out=r::execute(plan,p,c,g,e,runtime::memory().host);
    return out.closed||out.moved;
}
runtime::Hooks hooks(HooksState& state){return {&state,&prepare,&execute};}
n::Epoch input(runtime::Memory& m,int round) {
    m.host.now=round;m.host.state.own.submit_started=m.host.state.own.submit_cancelled=false;
    return runtime::begin(m.host.who,round);
}
void unknown_original_releases_physics() {
    auto& m=reset();HooksState work;
    auto e=input(m,10);auto g=geometry(e);const auto tracking=runtime::advance(e,g);
    CHECK(tracking.fresh&&m.tracking&&m.monitor.state()==t::State::Tracking);
    CHECK(m.token==1&&m.original==14&&m.next_token==1&&m.monitor.original_presence()==t::Presence::Held);
    // The actor is actually observed alive with 14, but round11 was not
    // observed. That invalidates provenance of the OLD original receipt.
    for(int round=12;round<=16;++round) {
        e=input(m,round);g=geometry(e);const auto observed=runtime::advance(e,g);
        CHECK(observed.fresh&&observed.receipt.state==t::State::Blocked);
        CHECK(observed.receipt.original_presence==t::Presence::Unknown&&observed.receipt.credited_original==0);
        const auto selected=runtime::choose(e,g,hooks(work));
        CHECK(selected.fresh&&!selected.claimed&&runtime::operational_handoff(selected));
        CHECK(selected.actual.own.alive&&selected.actual.own.bag==14&&selected.actual.own.channel==t::Channel::None);
        CHECK((selected.actual.own.position==t::Point{2,2}));
        CHECK(m.monitor.state()==t::State::Blocked&&m.monitor.original_presence()==t::Presence::Unknown);
        CHECK(m.token==1&&m.original==14&&m.next_token==1&&m.tracking);
        CHECK(m.host.moves==0&&m.host.starts==0&&work.prepares==0&&work.executes==0);
        CHECK(m.counters.fallbacks==0&&m.counters.conservation_payouts==0&&m.counters.credited_original==0);
        const auto repeated=runtime::choose(e,g,hooks(work));
        CHECK(runtime::operational_handoff(repeated)&&!repeated.claimed&&m.next_token==1&&work.prepares==0);
    }
}
void handoff_is_typed() {
    n::Result value;value.fresh=true;value.actual.own.alive=true;value.actual.own.bag=14;
    value.actual.own.channel=t::Channel::None;value.receipt.state=t::State::Blocked;
    value.receipt.original_presence=t::Presence::Unknown;
    CHECK(runtime::operational_handoff(value));
    auto no=value;no.fresh=false;CHECK(!runtime::operational_handoff(no));
    no=value;no.claimed=true;CHECK(!runtime::operational_handoff(no));
    no=value;no.actual.own.alive=false;CHECK(!runtime::operational_handoff(no));
    no=value;no.actual.own.bag=0;CHECK(!runtime::operational_handoff(no));
    no=value;no.actual.own.stunned=true;CHECK(!runtime::operational_handoff(no));
    no=value;no.actual.own.channel=t::Channel::Submit;CHECK(!runtime::operational_handoff(no));
    no=value;no.receipt.state=t::State::Tracking;CHECK(!runtime::operational_handoff(no));
    no=value;no.receipt.original_presence=t::Presence::Held;CHECK(!runtime::operational_handoff(no));
}
void actual_move_start_middle_payout() {
    auto& m=reset();HooksState work;auto e=input(m,10);auto g=geometry(e);
    CHECK(runtime::advance(e,g).fresh);
    const auto start=runtime::choose(e,g,hooks(work));
    CHECK(start.fresh&&start.claimed&&start.moved&&start.started&&m.host.moves==1&&m.host.starts==1);
    CHECK((start.actual.own.position==t::Point{3,2}));
    CHECK(start.receipt.credited_original==0&&start.receipt.evidence==t::Evidence::ModeledExpected);
    CHECK(m.monitor.expected_round()==12&&m.token==1&&m.original==14);
    CHECK(!start.actual.own.can_move&&!start.actual.own.can_attack&&!start.actual.own.can_cast);
    CHECK(work.prepares==0&&work.executes==0&&!runtime::operational_handoff(start));
    e=input(m,11);g=geometry(e);m.host.state.own.remaining_ticks=1;
    const auto middle=runtime::advance(e,g);
    CHECK(middle.fresh&&middle.receipt.outcome==t::Outcome::PendingChannel);
    CHECK(middle.actual.own.channel==t::Channel::Submit&&middle.actual.own.remaining_ticks==1);
    CHECK(middle.receipt.credited_original==0&&!runtime::operational_handoff(middle));
    CHECK(!runtime::choose(e,g,hooks(work)).claimed&&m.host.starts==1&&m.host.moves==1);
    e=input(m,12);g=geometry(e);m.host.state.own.channel=t::Channel::None;
    m.host.state.own.remaining_known=false;m.host.state.own.bag=0;m.host.state.own.team_score=14;
    const auto paid=runtime::advance(e,g);
    CHECK(paid.fresh&&paid.physical_released&&paid.receipt.credited_original==14);
    CHECK(paid.receipt.evidence==t::Evidence::ConservationSupported);
    CHECK(m.counters.conservation_payouts==1&&m.counters.credited_original==14);
    CHECK(!m.tracking&&m.archived&&m.empty_boundary&&m.next_token==1);
    CHECK(!runtime::operational_handoff(paid));
    (void)runtime::choose(e,g,hooks(work));
    CHECK(m.counters.conservation_payouts==1&&m.counters.credited_original==14&&m.next_token==1);
}
int main() {
    unknown_original_releases_physics();handoff_is_typed();actual_move_start_middle_payout();
    std::printf("capital_conversion_runtime: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
