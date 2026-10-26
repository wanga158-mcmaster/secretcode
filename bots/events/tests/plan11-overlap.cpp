#include <array>
#include <cassert>
#include <optional>
#include "../overlap/src/policy.h"
#include "../common/src/event_kernel.h"

using Policy=p11_overlap::Policy;

namespace {
// Independent phase/action simulation of a free straight line with one
// stationary target, ordinary basics and fresh turn allowances. It starts
// AFTER round zero's capture phase, at the current action phase. No prior
// allied capture progress survives; no walls, allies, enemies or abilities
// occur. Live targets start at distance>=1, since their tile is occupied.
int simulated_capture_round(int distance,int hits,int range,bool ready) {
    assert(distance>0&&range>=1&&(ready||hits>0));
    int remaining_hits=ready?0:hits;
    std::optional<int> capture_remaining;
    for(int round=0;round<128;++round) {
        if(round>0) {
            if(remaining_hits==0&&distance==0) {
                if(!capture_remaining)capture_remaining=5;
                else if(--*capture_remaining==0)return round;
            } else capture_remaining.reset();
        }
        bool move=true,attack=true;
        const auto walk=[&] {
            assert(move&&distance>0);
            --distance;move=false;
            if(range>1)attack=false;
        };
        const auto basic=[&] {
            assert(attack&&remaining_hits>0&&distance<=range);
            --remaining_hits;attack=false;
            if(range>1)move=false;
        };
        if(remaining_hits>0) {
            if(distance<=range) {
                basic();
                // An in-range melee lethal can still claim the spawn tile.
                if(remaining_hits==0&&move&&distance>0)walk();
            } else {
                walk();
                // A melee arrival can attack, but its walk is already spent:
                // a one-hit arrival cannot claim the occupied tile yet.
                if(attack&&distance<=range)basic();
            }
        } else if(distance>0)walk();
    }
    assert(false);return -1;
}

// Start at the action phase of the capture-award round with a free altar
// route. Channel countdown runs at owner end and completion at owner start.
int simulated_bank_round(int altar_walk,int starting_bag) {
    using namespace p11::kernel;
    ChannelSnapshot channel;
    for(int round=0;round<32;++round) {
        if(round>0&&owner_start(channel,starting_bag).banked==starting_bag)return round;
        if(!preserve_channel(channel)) {
            if(altar_walk>0)--altar_walk;
            if(altar_walk==0)channel={ChannelKind::tribute,p11::tribute_ticks(starting_bag),starting_bag};
        }
        owner_end(channel);
    }
    assert(false);return -1;
}

p11::Candidate activity(int id,int distance,int hits,int range,bool ready=false) {
    p11::Candidate candidate;
    candidate.id=id;candidate.travel=std::max(0,distance-(ready?0:range));
    candidate.clear=ready?0:hits;candidate.capture=6;
    candidate.gems=10;candidate.ready=ready;candidate.known=true;
    return candidate;
}

void unchanged_except_capture(const p11::Candidate&before,p11::Candidate after) {
    after.capture=before.capture;
    assert(before.id==after.id&&before.travel==after.travel&&before.clear==after.clear);
    assert(before.capture==after.capture&&before.altar==after.altar&&before.gems==after.gems);
    assert(before.gold==after.gold&&before.allies==after.allies&&before.enemy_eta==after.enemy_eta);
    assert(before.renewal==after.renewal&&before.age==after.age&&before.loss==after.loss);
    assert(before.hp==after.hp&&before.incoming==after.incoming&&before.ready==after.ready);
    assert(before.known==after.known&&before.committed==after.committed);
}
} // namespace

int main() {
    static_assert(Policy::bounded_scan&&Policy::window_size==3&&Policy::trace_scan);
    static_assert(Policy::packed_clock&&Policy::capture_tribute);
    p11::State state;state.mature=true;state.round=100;

    // Present both real alternatives to the actual selector. Far is one basic
    // hit instead of three, but the ranged walk after its kill reverses the
    // original action-count preference. IDs0/7 have equal Tempo spread.
    std::array<p11::Candidate,2> menu{activity(0,1,3,3),activity(7,4,1,3)};
    assert(p11::choose<p11_tempo::Policy>(state,menu)==7);
    menu[0]=Policy::pack(state,menu[0],3,1,1,true);
    menu[1]=Policy::pack(state,menu[1],3,4,1,true);
    assert(menu[0].capture==6&&menu[1].capture==8);
    assert(p11::choose<Policy>(state,menu)==0);
    assert(Policy::score(state,menu[0])==p11_tempo::Policy::score(state,menu[0]));
    assert(Policy::screen(state,menu[1])==p11_tempo::Policy::score(state,menu[1]));
    assert(Policy::bank(state)==p11_tempo::Policy::bank(state));
    assert(Policy::retreat(200,1000,50)==p11_tempo::Policy::retreat(200,1000,50));

    // Phase correction can cross the same hard bank horizon in either
    // direction. Choosing is still done by the production menu selector.
    state.round=1993;
    std::array<p11::Candidate,1> horizon{activity(0,1,0,3,true)};
    assert(horizon[0].bank_time(state)==8&&p11::choose<Policy>(state,horizon)==-1);
    horizon[0]=Policy::pack(state,horizon[0],3,1,1,true);
    assert(horizon[0].bank_time(state)==7&&p11::choose<Policy>(state,horizon)==0);
    state.round=1991;horizon[0]=activity(0,3,1,3);
    assert(horizon[0].bank_time(state)==8&&p11::choose<Policy>(state,horizon)==0);
    horizon[0]=Policy::pack(state,horizon[0],3,3,1,true);
    assert(horizon[0].bank_time(state)==10&&p11::choose<Policy>(state,horizon)==-1);

    // A capture-award action phase can take its first return step, and the
    // arrival turn starts tribute immediately. The actual policy/chooser now
    // admits a gem activity at the deadline against a real gold alternative.
    state.round=1992;
    std::array<p11::Candidate,2> bank_menu{
        Policy::pack(state,activity(0,1,1,1),1,1,1,true),activity(7,1,1,1)};
    bank_menu[0].altar=2;bank_menu[1].gems=0;bank_menu[1].gold=1;
    assert(bank_menu[0].payout()==6&&bank_menu[0].bank_time(state)==9);
    assert(p11::choose<p11_tempo::Policy>(state,bank_menu)==7);
    assert(p11::choose<Policy>(state,bank_menu)==0);
    assert(simulated_bank_round(0,7)==1);
    assert(simulated_bank_round(1,7)==1);
    assert(simulated_bank_round(4,11)==5); // four walks, then two owner-end ticks
    assert(simulated_bank_round(4,11)==4+p11::tribute_ticks(11)-1);
    const auto gold_value=Policy::score(state,bank_menu[1]);
    bank_menu[1].altar=100;
    assert(Policy::score(state,bank_menu[1])==gold_value);

    // Every observed field except capture survives packing. Unsupported
    // allowance/member/clear cases keep even that cost unchanged.
    auto original=activity(6,5,4,1);
    original.altar=7;original.gold=19;original.allies=2;original.enemy_eta=8;
    original.renewal=40;original.age=9;original.loss=123.5f;
    original.hp=450;original.incoming=25;original.committed=true;
    auto packed=Policy::pack(state,original,1,5,1,true);
    assert(packed.capture==4);unchanged_except_capture(original,packed);
    for(const auto members:std::array{0,2,32}) {
        packed=Policy::pack(state,original,1,5,members,true);
        assert(packed.capture==original.capture);unchanged_except_capture(original,packed);
    }
    packed=Policy::pack(state,original,1,5,1,false);
    assert(packed.capture==original.capture);unchanged_except_capture(original,packed);
    original.clear=10000;packed=Policy::pack(state,original,1,5,1,true);
    assert(packed.capture==original.capture);unchanged_except_capture(original,packed);
    original.clear=4;original.capture=1;
    assert(Policy::pack(state,original,1,5,1,true).capture==0);
    original.clear=0;original.capture=6;
    assert(Policy::pack(state,original,3,5,1,true).capture==6);

    //312 small conditional clock comparisons:288 live and24 ready approaches.
    // Ready distance0 has an unknown current timer; its unchanged estimate is
    // conservative, so it is checked for invariance, not asserted exact.
    int comparisons=0;
    for(int range:std::array{1,3}) {
        for(int distance=1;distance<=12;++distance) {
            for(int hits=1;hits<=12;++hits) {
                const auto candidate=activity(0,distance,hits,range);
                const auto adjusted=Policy::pack(state,candidate,range,distance,1,true);
                assert(adjusted.payout()==simulated_capture_round(distance,hits,range,false));
                unchanged_except_capture(candidate,adjusted);++comparisons;
            }
            const auto candidate=activity(0,distance,0,range,true);
            const auto adjusted=Policy::pack(state,candidate,range,distance,1,true);
            assert(adjusted.payout()==simulated_capture_round(distance,0,range,true));
            ++comparisons;
        }
        const auto standing=activity(0,0,0,range,true);
        assert(Policy::pack(state,standing,range,0,1,true).capture==standing.capture);
    }
    assert(comparisons==312&&comparisons<=500);

    const auto allowed=[](bool on_spawn=true,bool reward=true,bool contested=false,
                          bool channel=false,int bag=7,int altar_distance=1,
                          int submit_range=1,bool locked=false) {
        return Policy::can_overlap_capture_tribute(on_spawn,reward,contested,
            channel,bag,altar_distance,submit_range,locked);
    };
    assert(allowed());
    assert(!allowed(false)&&!allowed(true,false)&&!allowed(true,true,true));
    assert(!allowed(true,true,false,true)&&!allowed(true,true,false,false,0));
    assert(!allowed(true,true,false,false,7,2));
    assert(!allowed(true,true,false,false,7,1,1,true));
    assert(!allowed(true,true,false,false,7,-1));
    assert(!allowed(true,true,false,false,7,0,-1));

    // Conditional timing theory through the shared event kernel: the last
    // capture phase enlarges the current bag before a one-tick tribute's
    // next owner-start completion. No map/native coverage is implied.
    using namespace p11::kernel;
    CaptureProgress capture;
    assert(capture_start(capture,5,true).status==Status::applied);
    for(int i=0;i<4;++i)assert(!capture_phase(capture,true).completed);
    assert(capture.remaining&&*capture.remaining==1);
    ChannelSnapshot tribute{ChannelKind::tribute,p11::tribute_ticks(7),7};
    const auto tick=owner_end(tribute);
    assert(tick.status==Status::applied&&tick.banked==0&&tribute.remaining==0);
    assert(preserve_channel(tribute));
    assert((p11::Event{11,p11::Phase::capture}<p11::Event{11,p11::Phase::owner_start}));
    assert(capture_phase(capture,true).completed);
    const int enlarged_bag=7+7;
    const auto completion=owner_start(tribute,enlarged_bag);
    assert(completion.status==Status::applied&&completion.banked==14);
    assert(!preserve_channel(tribute)&&tribute.bag==0);
}
