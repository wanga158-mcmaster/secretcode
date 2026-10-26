#include <bit>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#ifndef MATCHMAKER_DRAFT_HEADER
#define MATCHMAKER_DRAFT_HEADER "../assignment-common/draft.h"
#endif
#include MATCHMAKER_DRAFT_HEADER

extern "C" void* __cxa_allocate_exception(std::size_t size) { return std::malloc(size); }
extern "C" void __cxa_throw(void*,void*,void(*)(void*)) { std::abort(); }
namespace a=plan3_assignment;
static unsigned checks=0;
static void require(bool ok,int line) {
    ++checks;
    if(!ok){std::fprintf(stderr,"public matrix check failed at %d\n",line);std::exit(1);}
}
#define REQUIRE(...) require(bool(__VA_ARGS__),__LINE__)
static void ints(const std::vector<int>& v) {
    std::printf("[");for(std::size_t i=0;i<v.size();++i)std::printf("%s%d",i?",":"",v[i]);std::printf("]");
}
static a::Result solve(a::Policy policy,const a::Matrix& matrix,unsigned budget) {
    a::Solver solver(policy);solver.start(matrix);unsigned steps=0;
    while(solver.status()==a::KernelStatus::pending) {
        REQUIRE(++steps<100000);
        solver.step(budget);REQUIRE(solver.last_step_units()<=budget);
    }
    REQUIRE(solver.status()==a::KernelStatus::complete && solver.result().complete);
    return solver.result();
}
static void result(const char* name,const a::Result& r) {
    std::printf("\"%s\":{\"complete\":%s,\"feasible\":%s,\"total\":%" PRIu64 ",\"owners\":",name,r.complete?"true":"false",r.feasible?"true":"false",r.total);ints(r.owners);std::printf("}");
}
int main() {
    static_assert(sizeof(cambc::detail::game_settings)==120 && sizeof(cambc::detail::metadata)==28);
    static_assert(sizeof(cambc::detail::champion_design)==128 && sizeof(cambc::detail::camp_design)==124);
    a::DraftAllocator allocator(a::Policy::matchmaker_exact);
    unsigned callbacks=0;
    for(;;) {
        REQUIRE(++callbacks<=128);
        const auto status=allocator.step();
        REQUIRE(allocator.last_work_units()<=allocator.maximum_callback_work());
        REQUIRE(status!=a::DraftStatus::unavailable);
        if(status==a::DraftStatus::published)break;
    }
    const auto& matrix=allocator.matrix();
    const auto exact=solve(a::Policy::matchmaker_exact,matrix,8);
    const auto slow=solve(a::Policy::matchmaker_exact,matrix,1);
    const auto greedy=solve(a::Policy::matchmaker_greedy,matrix,8);
    const auto greedySlow=solve(a::Policy::matchmaker_greedy,matrix,1);
    REQUIRE(exact.owners==slow.owners && exact.total==slow.total);
    REQUIRE(greedy.owners==greedySlow.owners && greedy.total==greedySlow.total);
    REQUIRE(exact.owners==allocator.result().owners && exact.total==allocator.result().total);
    REQUIRE(exact.owners==allocator.plan().owners && allocator.plan().complete && allocator.plan().feasible);
    const auto readback=a::read_plan(a::Policy::matchmaker_exact,5);
    REQUIRE(readback.complete && readback.owners==exact.owners && readback.matrixFingerprint==allocator.matrix_fingerprint());
    REQUIRE(allocator.estimates().size()==matrix.cells.size());
    std::printf("{\"schema\":\"matchmaker-production-matrix/v1\",\"checks\":%u,\"callbacks\":%u,\"fingerprint\":%u,\"horizon\":128,\"actors\":",checks,callbacks,allocator.matrix_fingerprint());
    ints(matrix.actors);std::printf(",\"groups\":");ints(matrix.groups);std::printf(",\"cells\":[");
    for(std::size_t i=0;i<matrix.cells.size();++i) {
        const auto& c=matrix.cells[i];const auto& e=allocator.estimates()[i];
        std::printf("%s{\"state\":%u,\"utility\":%u,\"reason\":%u,\"duration\":%d,\"healthLossBits\":\"%016" PRIx64 "\"}",i?",":"",unsigned(c.state),c.utility,unsigned(e.reason),e.duration,std::bit_cast<std::uint64_t>(e.healthLoss));
    }
    std::printf("],");result("exact",exact);std::printf(",");result("greedy",greedy);std::printf("}\n");
}
