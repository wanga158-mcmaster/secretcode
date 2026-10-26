#include "../common/allocation-wire.h"
#include "../common/model.h"
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"Allocation check failed: %s\n",message); std::exit(1); }
}
void resign(plan3_runtime::MarketBytes& bytes) {
    const auto sum=plan3_runtime::market_checksum(bytes);
    for(unsigned n=0;n<4;++n) bytes[20+n]=std::byte((sum>>(8*n))&255u);
}
}

int main() {
    using namespace plan3_runtime;
    MarketPlan sample;
    sample.complete=true; sample.width=100; sample.height=100;
    sample.actorOrdinals={2,10,11}; sample.groupIds={0,400,9999};
    sample.ownerOrdinals={11,-1,2};
    const auto encoded=encode_market_plan(sample);
    check(bool(encoded),"valid plan encoded");
    const auto decoded=decode_market_plan(*encoded,100,100);
    check(decoded.complete,"complete plan decoded");
    check(decoded.groupIds==sample.groupIds && decoded.ownerOrdinals==sample.ownerOrdinals,
          "stable group ids and physical owner bytes round trip");
    check(decoded.owner(0)==11 && decoded.owner(400)==-1 && decoded.owner(9999)==2,
          "owned and intentionally unowned lookups");
    check(decoded.owner(401)==-1,"absent group stays unowned");
    check(!decode_market_plan(*encoded,99,100).complete,"map identity mismatch rejected");
    check(!decode_market_plan(std::span<const std::byte>(encoded->data(),1023),100,100).complete,
          "truncated memory rejected");
    for(unsigned n=0;n<encoded->size();++n) {
        auto bad=*encoded; bad[n]^=std::byte{1};
        check(!decode_market_plan(bad,100,100).complete,"single-byte corruption rejected");
    }
    auto bad=*encoded; bad[5]=std::byte{0}; resign(bad);
    check(!decode_market_plan(bad,100,100).complete,"unfinished plan not accepted after resigning");
    bad=*encoded; bad[4]=std::byte{2}; resign(bad);
    check(!decode_market_plan(bad,100,100).complete,"unknown version rejected");
    bad=*encoded; bad[16]=std::byte{2}; resign(bad);
    check(!decode_market_plan(bad,100,100).complete,"unknown model tag rejected");
    bad=*encoded; bad[34]=std::byte{7}; resign(bad);
    check(!decode_market_plan(bad,100,100).complete,"owner outside fixed roster rejected");
    bad=*encoded; bad[36]=bad[32]; bad[37]=bad[33]; resign(bad);
    check(!decode_market_plan(bad,100,100).complete,"duplicate stable group id rejected");
    bad=*encoded; bad[25]=bad[24]; resign(bad);
    check(!decode_market_plan(bad,100,100).complete,"duplicate actor rejected");
    bad=*encoded; bad[1023]=std::byte{1}; resign(bad);
    check(!decode_market_plan(bad,100,100).complete,"unversioned trailing payload rejected");
    auto incomplete=sample; incomplete.complete=false;
    check(!encode_market_plan(incomplete),"partial allocation cannot be encoded");
    auto invalid=sample; invalid.groupIds[2]=10000;
    check(!encode_market_plan(invalid),"tile outside map rejected");
    invalid=sample; invalid.ownerOrdinals.pop_back();
    check(!encode_market_plan(invalid),"owner count mismatch rejected");
    invalid=sample; invalid.width=101;
    check(!encode_market_plan(invalid),"unsupported dimensions rejected");
    auto large=sample;
    large.actorOrdinals={1,2,7,10,11}; large.groupIds.clear(); large.ownerOrdinals.clear();
    for(int n=0;n<64;++n) { large.groupIds.push_back(n*100); large.ownerOrdinals.push_back(n%2 ? -1 : 11); }
    const auto full=encode_market_plan(large);
    check(bool(full) && decode_market_plan(*full,100,100).groupIds.size()==64,"64 groups and five actors fit");
    large.groupIds.push_back(9999); large.ownerOrdinals.push_back(11);
    check(!encode_market_plan(large),"65 groups cannot silently overflow");
    auto empty=sample; empty.groupIds.clear(); empty.ownerOrdinals.clear();
    check(bool(encode_market_plan(empty)),"complete no-feasible-groups result is representable");

    // The draft uses the same incremental pure allocator as the offline model.
    // Owner indices are mapped to sorted physical actor ordinals only at publish.
    std::vector<plan3::AllocationEdge> edges{{0,10,100,40,true,true},{1,10,20,40,true,true},
        {2,20,20,40,true,true},{0,30,60,30,true,true},{1,30,80,30,true,true}};
    const auto all=plan3::market_allocate({30,20,10},3,edges);
    auto incremental=plan3::market_allocation_start({30,20,10},3);
    plan3::MarketScan scan;
    for(unsigned n=0;n<100 && !incremental.complete;++n) {
        const auto status=plan3::market_allocate_work(incremental,edges,scan,1);
        check(status!=plan3::MarketWorkStatus::invalid,"bounded scan remains valid");
        if (!incremental.complete) {
            MarketPlan pending; pending.width=100; pending.height=100;
            pending.actorOrdinals={2,10,11};
            check(!encode_market_plan(pending),"no intermediate scan can publish an allocation");
        }
    }
    check(incremental.complete && incremental.ownerOrdinals==all.ownerOrdinals &&
          incremental.actorLoads==all.actorLoads,"incremental allocation equals complete pure scan");
    check(all.ownerOrdinals==std::vector<int>({1,2,0}),"one ownership decision per group");
    for(const int load:all.actorLoads) check(load<=128,"individual actor load respected");
    std::printf("Plan 3 allocation: %u synthetic protocol and continuation checks passed.\n",checks);
}
