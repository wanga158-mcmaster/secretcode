#include "../src/camp_mask_ledger.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#ifdef NDEBUG
#error CampMaskLedger tests require assertions.
#endif

namespace {
std::size_t raster_checks{}, transitions{}, advances{}, rejected{};
struct Member { int x{},y{},range{};bool registered{},alive{},threat{}; };
struct Oracle {
    int width{},height{};
    std::vector<Member> members;
    bool ready() const { return std::all_of(members.begin(),members.end(),[](auto m){return m.registered;}); }
    // Recompute each output cell from all current members. This is independent
    // of incremental counts, transition footprints and the ledger's cursors.
    std::vector<std::uint8_t> raster(bool threat) const {
        std::vector<std::uint8_t> bytes((std::size_t(width)*height+7)/8,0);
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
            bool on=false;
            for(const auto& m:members) if(m.registered&&m.alive) {
                if(!threat) on=on||(x==m.x&&y==m.y);
                else if(m.threat) {
                    const auto dx=std::int64_t(x)-m.x,dy=std::int64_t(y)-m.y;
                    on=on||(dx>=-std::int64_t(m.range)&&dx<=m.range
                             &&dy>=-std::int64_t(m.range)&&dy<=m.range);
                }
            }
            if(on) {const auto bit=std::size_t(y)*width+x;bytes[bit/8]|=std::uint8_t(1u<<(bit%8));}
        }
        return bytes;
    }
};
void hidden(const wb::CampMaskLedger& ledger) {
    assert(!ledger.valid()&&ledger.living_bytes().empty()&&ledger.threat_bytes().empty());
}
void failed(wb::CampMaskLedger& ledger) {
    assert(ledger.failed()&&!ledger.pending_update());hidden(ledger);
    assert(ledger.advance()==wb::LedgerStep::failed);
}
struct Fixture {
    wb::CampMaskLedger ledger;
    Oracle model;
    Fixture(int width,int height,std::size_t capacity) { reset(width,height,capacity); }
    void reset(int width,int height,std::size_t capacity) {
        assert(ledger.reset(width,height,capacity));
        model={width,height,std::vector<Member>(capacity)};
        assert(!ledger.failed()&&!ledger.pending_update());check();
    }
    void check() const {
        ++raster_checks;
        assert(!ledger.failed()&&ledger.width()==model.width&&ledger.height()==model.height);
        if(ledger.pending_update()||!model.ready()) {hidden(ledger);return;}
        assert(ledger.valid());
        const auto living=model.raster(false),threat=model.raster(true);
        assert(std::equal(living.begin(),living.end(),ledger.living_bytes().begin(),ledger.living_bytes().end()));
        assert(std::equal(threat.begin(),threat.end(),ledger.threat_bytes().begin(),ledger.threat_bytes().end()));
    }
    std::size_t update(std::size_t key,int x,int y,int range,bool alive,bool threat,std::size_t budget=128) {
        const auto old=model.members.at(key);
        assert(ledger.begin_transition(key,x,y,range,old.alive,old.threat,alive,threat));++transitions;
        assert(ledger.pending_update()&&!ledger.failed());hidden(ledger);
        std::size_t steps=0;
        while(ledger.pending_update()) {
            assert(++steps<100000);++advances;
            const auto result=ledger.advance(budget);
            if(result==wb::LedgerStep::pending) {assert(ledger.pending_update()&&!ledger.failed());hidden(ledger);}
            else {assert(result==wb::LedgerStep::committed&&!ledger.pending_update()&&!ledger.failed());}
        }
        model.members[key]={x,y,range,true,alive,threat};check();return steps;
    }
    std::size_t state(std::size_t key,bool alive,bool threat,std::size_t budget=128) {
        const auto m=model.members.at(key);assert(m.registered);
        return update(key,m.x,m.y,m.range,alive,threat,budget);
    }
};
struct Case {const char* name;std::size_t checks,updates,steps,rejections;};
std::vector<Case> cases;
void record(const char* name) {cases.push_back({name,raster_checks,transitions,advances,rejected});}
template<class Invalid> void reject_registered(Invalid invalid) {
    Fixture f(5,3,1);f.update(0,2,1,1,true,true);
    assert(!invalid(f.ledger));++rejected;failed(f.ledger);
    assert(!f.ledger.begin_transition(0,2,1,1,true,true,false,false));
    f.reset(3,2,1);f.update(0,0,0,0,false,false);f.check();
}
}

int main() {
    {
        Fixture empty(5,3,0);assert(empty.ledger.valid()&&empty.ledger.advance()==wb::LedgerStep::idle);
        Fixture f(5,3,3);hidden(f.ledger);
        f.update(2,4,2,0,false,false);hidden(f.ledger);
        f.update(0,0,0,1,true,true);hidden(f.ledger);
        f.update(1,2,1,2,false,false);assert(f.ledger.valid());
        assert(f.ledger.living_bytes().size()==2&&f.ledger.threat_bytes().size()==2);
        record("cold-registration-barrier-zero-capacity-and-packed-padding");
    }
    {
        Fixture f(7,3,2);f.update(0,1,1,2,true,true);f.update(1,3,1,2,true,true);
        f.state(0,false,false,1);f.state(1,false,false,3);
        record("overlap-removal-keeps-other-threat-until-last-owner");
    }
    {
        Fixture f(5,3,2);f.update(0,2,1,1,true,false);f.update(1,2,1,2,true,false);
        f.state(0,false,false);assert(f.ledger.living_bytes()[0]&(1u<<7));
        f.state(1,false,false);record("coincident-living-centers-have-independent-owners");
    }
    {
        Fixture f(7,5,1);f.update(0,3,2,2,true,true);
        f.state(0,false,false);f.state(0,true,true);f.state(0,false,false);f.state(0,true,false);
        record("death-then-caller-unknown-restoration");
    }
    {
        Fixture f(5,5,1);f.update(0,2,2,1,true,false);
        f.state(0,true,true);f.state(0,true,false);f.state(0,false,false);
        record("sleep-awake-transitions-change-threat-without-living-drift");
    }
    {
        Fixture f(9,4,4);
        f.update(0,1,1,1,false,false);f.update(1,3,1,1,true,false);
        f.update(2,5,1,2,false,false);f.update(3,8,3,1,true,true);
        f.state(0,true,true);f.state(2,true,true);f.state(3,false,false);
        record("partial-hidden-member-restoration-preserves-other-known-members");
    }
    {
        Fixture f(5,3,2);f.update(0,0,0,2,true,true);f.update(1,4,2,1,true,true);
        f.state(0,false,false);f.state(1,false,false);
        Fixture narrow(1,7,1);narrow.update(0,0,3,3,true,true);narrow.state(0,false,false);
        Fixture flat(7,1,1);flat.update(0,6,0,std::numeric_limits<int>::max(),true,true);
        flat.state(0,false,false);record("clipped-nonsquare-one-dimensional-and-huge-positive-range");
    }
    {
        Fixture a(17,11,160),b(17,11,160),c(17,11,160);
        for(std::size_t i=0;i<160;++i) {
            const std::array<std::size_t,3> keys{i,159-i,(37*i)%160};
            std::array<Fixture*,3> fs{&a,&b,&c};
            for(std::size_t k=0;k<fs.size();++k) {
                const auto key=keys[k];fs[k]->update(key,int(key%17),int(key/17),int(key%4),true,key%3!=0,k==2?7:128);
            }
        }
        assert(std::equal(a.ledger.living_bytes().begin(),a.ledger.living_bytes().end(),b.ledger.living_bytes().begin()));
        assert(std::equal(a.ledger.threat_bytes().begin(),a.ledger.threat_bytes().end(),c.ledger.threat_bytes().begin()));
        for(std::size_t i=0;i<160;++i) {a.state((37*i)%160,false,false,1);b.state(i,false,false,3);c.state(159-i,false,false,128);}
        a.check();b.check();c.check();record("all160-registration-order-independence-and-complete-cancellation");
    }
    {
        Fixture f(20,20,1);f.update(0,10,10,std::numeric_limits<int>::max(),false,false);
        assert(f.ledger.begin_transition(0,10,10,std::numeric_limits<int>::max(),false,false,true,true));++transitions;
        assert(f.ledger.advance(0)==wb::LedgerStep::pending);++advances;hidden(f.ledger);
        std::size_t calls=0;
        while(f.ledger.pending_update()) {
            const auto result=f.ledger.advance(std::numeric_limits<std::size_t>::max());++calls;++advances;
            if(calls==1) {assert(result==wb::LedgerStep::pending);hidden(f.ledger);}
            if(result==wb::LedgerStep::pending) hidden(f.ledger);
            else assert(result==wb::LedgerStep::committed);
            assert(calls<1000);
        }
        // One living cell and400 threat cells, each checked and applied:802
        // visits require at least7 calls when every request clamps to128.
        assert(calls>=(802+127)/128);
        f.model.members[0].alive=f.model.members[0].threat=true;f.check();
        const auto remove_calls=f.state(0,false,false,3);assert(remove_calls>=(802+2)/3);
        record("zero-budget-pause-clamp128-and-small-budget-resumable-large-add-remove");
    }
    {
        reject_registered([](auto& l){return l.begin_transition(0,2,1,1,false,false,false,false);});
        reject_registered([](auto& l){return l.begin_transition(0,3,1,1,true,true,false,false);});
        reject_registered([](auto& l){return l.begin_transition(0,2,2,1,true,true,false,false);});
        reject_registered([](auto& l){return l.begin_transition(0,2,1,2,true,true,false,false);});
        Fixture f(3,3,1);f.update(0,1,1,1,true,true);f.state(0,false,false);
        assert(!f.ledger.begin_transition(0,1,1,1,true,true,false,false));++rejected;failed(f.ledger);
        record("wrong-old-state-immutable-geometry-range-and-repeated-remove-underflow-reject");
    }
    {
        const auto bad=[](int x,int y,int r,bool oa,bool ot,bool na,bool nt) {
            wb::CampMaskLedger l;assert(l.reset(3,2,1));
            assert(!l.begin_transition(0,x,y,r,oa,ot,na,nt));++rejected;failed(l);
        };
        bad(-1,0,1,false,false,true,false);bad(0,-1,1,false,false,true,false);
        bad(3,0,1,false,false,true,false);bad(0,2,1,false,false,true,false);
        bad(0,0,-1,false,false,true,false);bad(0,0,1,false,false,false,true);
        bad(0,0,1,false,true,true,false);bad(0,0,1,true,false,false,false);
        reject_registered([](auto& l){return l.begin_transition(1,0,0,0,false,false,true,false);});
        reject_registered([](auto& l){return l.begin_transition(std::numeric_limits<std::size_t>::max(),0,0,0,false,false,true,false);});
        record("malformed-predicates-coordinates-first-old-state-and-key-overflow-fail-closed");
    }
    {
        wb::CampMaskLedger l;
        for(const auto dims:std::array<std::array<int,2>,4>{{{0,2},{2,0},{-1,2},{2,-1}}}) {
            assert(!l.reset(dims[0],dims[1],1));++rejected;failed(l);
            assert(l.reset(2,2,0)&&l.valid());
        }
        assert(!l.reset(1,1,65536));++rejected;failed(l);
        assert(l.reset(1,1,65535)&&!l.failed());hidden(l);
        assert(l.reset(2,2,0)&&l.valid());
        record("bad-reset-and-capacity-overflow-latch-failure-reset-recovers");
    }
    {
        Fixture f(20,20,1);f.update(0,10,10,99,false,false);
        assert(f.ledger.begin_transition(0,10,10,99,false,false,true,true));++transitions;
        for(int i=0;i<5;++i) {assert(f.ledger.advance(128)==wb::LedgerStep::pending);++advances;hidden(f.ledger);}
        assert(!f.ledger.begin_transition(0,10,10,99,false,false,false,false));++rejected;failed(f.ledger);
        f.reset(20,20,1);f.update(0,10,10,99,false,false);f.check();
        assert(f.ledger.begin_transition(0,10,10,99,false,false,true,true));++transitions;
        assert(f.ledger.advance(1)==wb::LedgerStep::pending);++advances;
        f.reset(5,3,0);assert(f.ledger.advance()==wb::LedgerStep::idle);f.check();
        record("busy-rejection-hides-partial-application-and-reset-cancels-pending-work");
    }
    assert(cases.size()==13);
    std::printf("{\"passed\":true,\"assertionsEnabled\":true,\"caseCount\":%zu,\"rasterChecks\":%zu,\"queuedTransitions\":%zu,\"advanceCalls\":%zu,\"rejectedRequests\":%zu,\"cases\":[",
                cases.size(),raster_checks,transitions,advances,rejected);
    for(std::size_t i=0;i<cases.size();++i) {
        const auto& c=cases[i];
        std::printf("%s{\"name\":\"%s\",\"cumulativeRasterChecks\":%zu,\"cumulativeUpdates\":%zu,\"cumulativeAdvanceCalls\":%zu,\"cumulativeRejections\":%zu}",
            i?",":"",c.name,c.checks,c.updates,c.steps,c.rejections);
    }
    std::puts("]}");
}
