#include "../twin-interlock/src/interlock.h"
#include "../twin-interlock/src/policy.h"
#include <cassert>
#include <vector>

namespace twin=h12_twin_interlock;

struct Memory {
    mutable std::array<std::byte,8> bytes{};
    mutable unsigned reads=0,writes=0,last_offset=99,last_size=0;
    std::size_t capacity=8;
    std::size_t size()const{return capacity;}
    void read(std::size_t at,std::span<std::byte> out)const {
        ++reads;
        for(std::size_t i=0;i<out.size();++i)out[i]=bytes[at+i];
    }
    void write(std::size_t at,std::span<const std::byte> in)const {
        ++writes;last_offset=unsigned(at);last_size=unsigned(in.size());
        for(std::size_t i=0;i<in.size();++i)bytes[at+i]=in[i];
    }
};

int main() {
    auto peer=twin::self_report(40,17,28,true,0,true,0,false);
    const auto original=twin::encode(peer);
    const auto decoded=twin::decode(original);
    assert(decoded.round==40&&decoded.x==17&&decoded.y==28&&decoded.alive
        &&!decoded.cargo&&decoded.safe&&decoded.channel==0&&!decoded.epoch&&decoded.valid);
    assert(twin::fresh(decoded,40,80,80,2000));
    assert(twin::fresh(decoded,41,80,80,2000));
    assert(!twin::fresh(decoded,39,80,80,2000));
    assert(!twin::fresh(decoded,42,80,80,2000));
    assert(!twin::fresh(decoded,40,17,80,2000));
    assert(!twin::fresh(decoded,40,80,28,2000));
    assert(!twin::fresh(decoded,40,101,80,2000));
    assert(!twin::fresh(decoded,40,80,80,2048));
    assert(!twin::fresh(twin::decode(twin::Packet{}),1,80,80,2000));
    for(int field=0;field<4;++field) {
        auto bad=peer;
        if(field==0)bad.round=2048;
        if(field==1)bad.x=128;
        if(field==2)bad.y=-1;
        if(field==3)bad.channel=4;
        assert(!twin::decode(twin::encode(bad)).valid);
    }
    // Every field survives explicit little-endian packing at its edge.
    auto edge=twin::decode(twin::encode({2047,127,126,true,true,true,3,true,true}));
    assert(edge.round==2047&&edge.x==127&&edge.y==126&&edge.alive&&edge.cargo
        &&edge.safe&&edge.channel==3&&edge.epoch&&edge.valid);
    {
        Memory memory;
        assert(twin::write_report(memory,3,peer));
        assert(memory.last_offset==0&&memory.last_size==4);
        const auto cassi=memory.bytes;
        auto issac=peer;issac.x=38;issac.cargo=true;issac.epoch=true;
        assert(twin::write_report(memory,4,issac));
        assert(memory.last_offset==4&&memory.last_size==4);
        for(unsigned i=0;i<4;++i)assert(memory.bytes[i]==cassi[i]);
        const auto first=twin::read_reports(memory);
        assert(memory.reads==1&&first[0].x==17&&first[1].x==38&&first[1].cargo);
        // The production helper reads the host again; previously returned
        // arrays remain stale values and must never be treated as live.
        issac.x=39;assert(twin::write_report(memory,4,issac));
        const auto second=twin::read_reports(memory);
        assert(memory.reads==2&&first[1].x==38&&second[1].x==39);
        // A sender may retain an old read while the other owner publishes.
        // Its subsequent four-byte publication must retain that newer peer.
        peer.round=41;
        assert(twin::write_report(memory,3,peer));
        assert(twin::read_reports(memory)[1].x==39&&memory.reads==3);
        const auto before=memory.bytes;const auto writes=memory.writes;
        for(int identity:{0,1,2,5,6,7,8,9,10,11,12})assert(!twin::write_report(memory,identity,peer));
        assert(memory.bytes==before&&memory.writes==writes);
        memory.capacity=7;
        assert(!twin::write_report(memory,3,peer));
        assert(!twin::read_reports(memory)[0].valid&&memory.reads==3);
    }
    peer.round=40;
    {
        twin::Link link;
        assert(!link.observe_life(true,40)&&!link.epoch());
        assert(link.accept(peer,40,80,80,2000,true));
        assert(!link.accept(peer,40,80,80,2000,false)); // hidden is_alive owns reality
        auto old=peer;old.round=41;
        assert(!link.observe_life(false,41));
        // Each body observes its own life and publishes independently. The
        // earlier owner's epoch cannot be copied into the later owner's slot.
        assert(link.observe_life(true,101)&&link.epoch());
        old.round=101;
        assert(!link.accept(old,101,80,80,2000,true)); // same-round old epoch
        auto next=old;next.epoch=true;
        assert(!link.accept(next,101,80,80,2000,false));
        next.round=100;
        assert(!link.accept(next,101,80,80,2000,true)); // before own new cohort
        next.round=101;
        assert(link.accept(next,101,80,80,2000,true));
        assert(link.unknown_age(102)==1);
        assert(link.accept(next,102,80,80,2000,true));
        assert(link.unknown_age(102)==1); // prior zero does not restart the clock
        assert(!link.accept(next,103,80,80,2000,true));
        assert(link.unknown_age(109)==8);
        assert(!link.observe_life(false,110));
        assert(link.observe_life(true,170)&&!link.epoch());
        next.round=170;
        assert(!link.accept(next,170,80,80,2000,true));
        next.epoch=false;
        assert(link.accept(next,170,80,80,2000,true));
    }
    {
        twin::Link initially_unknown;
        initially_unknown.observe_life(true,40);
        auto prior=peer;prior.round=39;
        assert(initially_unknown.accept(prior,40,80,80,2000,true));
        twin::Link never_observed;
        never_observed.observe_life(true,40);
        never_observed.observe_life(false,41);
        never_observed.observe_life(true,101);
        prior.round=100;prior.epoch=true;
        assert(!never_observed.accept(prior,101,80,80,2000,true));
        prior.round=101;
        assert(never_observed.accept(prior,101,80,80,2000,true));
    }
    {
        twin::Link cassi,issac;
        cassi.observe_life(true,1);issac.observe_life(true,1);
        auto c=twin::self_report(1,5,5,true,3,true,0,cassi.epoch());
        auto i=twin::self_report(1,6,5,true,0,true,0,issac.epoch());
        assert(cassi.accept(i,1,80,80,2000,true)&&issac.accept(c,1,80,80,2000,true));
        cassi.observe_life(false,10);issac.observe_life(false,10);
        cassi.observe_life(true,70);
        c=twin::self_report(70,5,5,true,0,true,0,cassi.epoch());
        assert(!cassi.accept(i,70,80,80,2000,false));
        // No shared-turn assumption: Issac's later owner-start must observe
        // its own resurrection and then make its own fresh report.
        issac.observe_life(true,70);
        assert(issac.accept(c,70,80,80,2000,true));
        i=twin::self_report(70,6,5,true,0,true,0,issac.epoch());
        assert(cassi.accept(i,71,80,80,2000,true));
    }
    using M=twin::Mode;
    peer.cargo=true;peer.safe=false;
    assert(twin::mode(true,true,peer)==M::SelfRetreat);
    assert(twin::mode(false,true,peer)==M::PartnerRetreat);
    peer.safe=true;
    assert(twin::mode(false,true,peer)==M::Protect);
    assert(!twin::may_damage(M::Protect,500,1000,1000,1,false,false));
    assert(twin::may_damage(M::Protect,0,1000,1000,0,false,true));
    assert(twin::mode(false,false,peer)==M::Unknown); // stale positive is no current veto
    peer.round=39;
    assert(twin::mode_for_report(true,true,peer,40)==M::SelfRetreat);
    assert(twin::mode_for_report(false,false,peer,40)==M::Unknown);
    assert(twin::mode_for_report(false,true,peer,40)==M::Protect);
    peer.safe=false;
    assert(twin::mode_for_report(false,true,peer,40)==M::PartnerRetreat);
    peer.safe=true;peer.cargo=false;
    assert(twin::mode_for_report(false,true,peer,40)==M::Unknown);
    peer.round=40;
    assert(twin::mode_for_report(false,true,peer,40)==M::Work);
    assert(!twin::may_damage(M::Unknown,5,1000,1000,1,false,false));
    assert(twin::may_damage(M::Unknown,6,1000,1000,120,false,false));
    assert(!twin::may_damage(M::Unknown,6,819,1000,1,false,false));
    assert(!twin::may_damage(M::Unknown,6,1000,1000,121,false,false));
    assert(!twin::may_damage(M::Unknown,6,1000,1000,1,true,false));
    assert(!twin::may_damage(M::SelfRetreat,500,1000,1000,0,false,true));
    assert(!twin::may_damage(M::PartnerRetreat,500,1000,1000,0,false,true));
    // Numeric cast damage is not the admission criterion: Cassi's pure stun
    // provokes the same dangerous source, and Issac's zero-damage dash can
    // enter the same danger radius while the partner's actual bag is loaded.
    assert(!twin::may_provoke(M::Protect,500,1000,1000,100,false,false));
    assert(twin::may_provoke(M::Protect,0,1000,1000,0,false,true));
    assert(!twin::may_provoke(M::Unknown,5,1000,1000,100,false,false));
    assert(twin::may_provoke(M::Unknown,6,1000,1000,100,false,false));
    assert(!twin::may_provoke(M::SelfRetreat,500,1000,1000,0,false,true));
    assert(!twin::safe_landing(M::Protect,true));
    assert(!twin::safe_landing(M::Unknown,true));
    assert(!twin::safe_landing(M::SelfRetreat,true));
    assert(!twin::safe_landing(M::PartnerRetreat,true));
    assert(twin::safe_landing(M::Work,true));
    for(const auto current:{M::SelfRetreat,M::PartnerRetreat,M::Protect,M::Unknown,M::Work})
        assert(twin::safe_landing(current,false));
    // The production adapter checks these before reading packet inputs or
    // constructing a static component, rather than after paying those costs.
    assert(!twin::packet_admitted(41999)&&twin::packet_admitted(42000));
    assert(!twin::component_admitted(79999)&&twin::component_admitted(80000));
    assert(twin::component_needs_refresh(false,false));
    assert(twin::component_needs_refresh(false,true));
    assert(!twin::component_needs_refresh(true,true));
    assert(twin::component_needs_refresh(true,false)); // fresh recall/respawn origin outside old component
    assert(!twin::cancel_for_exposure(true,0)); // low HP alone preserves the ongoing channel
    assert(twin::cancel_for_exposure(true,1));
    assert(!twin::cancel_for_exposure(false,1));
    assert(!twin::cancel_for_exposure(false,0));
    assert(twin::quarantine_after_refusal(500)==548);
    assert(twin::quarantine_after_refusal(500)>500);
    assert(twin::quarantine_after_refusal(500)-500<=48);
    for(int bits=0;bits<8;++bits)
        assert(twin::has_action(bool(bits&1),bool(bits&2),bool(bits&4))==bool(bits));
    {
        twin::Link link;link.observe_life(true,40);
        // A zero report may precede an engine capture before the reader's
        // turn. The reader cannot query that hidden bag or convert the old
        // zero into current permission. Repeated prior zeros remain unknown.
        for(int now=41;now<=46;++now) {
            const auto prior_zero=twin::self_report(now-1,9,9,true,0,true,0,false);
            assert(link.accept(prior_zero,now,80,80,2000,true));
            const auto current=twin::mode_for_report(false,true,prior_zero,now);
            assert(current==M::Unknown&&link.unknown_age(now)==now-40);
            assert(!twin::safe_landing(current,true));
            assert(twin::may_provoke(current,link.unknown_age(now),1000,1000,100,false,false)==(now==46));
        }
        // The bounded fallback still rejects larger measured danger; unknown
        // observation never becomes a permanent safety or no-work guarantee.
        assert(!twin::may_provoke(M::Unknown,6,1000,1000,121,false,false));
        auto positive=twin::self_report(46,9,9,true,1,true,0,false);
        assert(link.accept(positive,47,80,80,2000,true));
        assert(link.unknown_age(47)==0);
        assert(twin::mode_for_report(false,true,positive,47)==M::Protect);
        auto unsafe=twin::self_report(47,9,9,true,0,false,0,false);
        assert(link.accept(unsafe,48,80,80,2000,true)&&link.unknown_age(48)==0);
        assert(twin::mode_for_report(false,true,unsafe,48)==M::PartnerRetreat);
        auto current_zero=twin::self_report(49,9,9,true,0,true,0,false);
        assert(link.accept(current_zero,49,80,80,2000,true)&&link.unknown_age(49)==0);
        assert(twin::mode_for_report(false,true,current_zero,49)==M::Work);
    }
    auto submitting=twin::self_report(50,5,5,true,12,true,2,false);
    assert(submitting.cargo&&twin::mode(false,true,submitting)==M::Protect);
    auto paid=twin::self_report(52,5,5,true,0,true,0,false);
    assert(!paid.cargo&&twin::mode(false,true,paid)==M::Work);
    {
        twin::Policy policy;
        // Both bags can be positive at once. Neither body waits for a zero
        // peer report before converting its own bag, even when healthy and
        // sixteen walking steps from the altar.
        for(const int physical:{3,4}) {
            heuristics::State state;
            state.identity=physical;state.round=500;state.limit=2000;
            state.hp=state.maximum=1000;state.bag=1;state.altar=16;
            twin::Link link;link.observe_life(true,500);
            auto partner=twin::self_report(500,8,8,true,1,true,0,false);
            assert(link.accept(partner,500,80,80,2000,true));
            assert(twin::mode(false,true,partner)==M::Protect);
            assert(policy.bank(state));
            auto own=twin::self_report(501,7,8,true,state.bag,true,2,link.epoch());
            assert(own.cargo&&twin::mode(false,true,own)==M::Protect);
            // A successful tribute start still leaves positive cargo. Only
            // the subsequent actual observed zero bag releases the other body.
            state.bag=0;
            own=twin::self_report(502,7,8,true,state.bag,true,0,link.epoch());
            assert(!policy.bank(state));
            assert(!own.cargo&&twin::mode(false,true,own)==M::Work);
        }
    }
    assert(twin::cast_compatible(true,false,true)); // Issac reset/dash, Cassi stun
    assert(!twin::cast_compatible(true,true,true)); // Cassi damage after walk
    assert(twin::cast_compatible(true,true,false));
    assert(!twin::cast_compatible(false,false,false));
    return 0;
}
