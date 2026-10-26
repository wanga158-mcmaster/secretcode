#include "service_route.h"
#include <array>
#include <cassert>
#include <cstdio>

namespace {
using namespace mosaic::service_route;
struct Board {
    int width{},height{};std::array<std::uint8_t,1250> terrain{},occupied{};
    std::array<Point,16> goals{};std::size_t goal_count{};
    std::size_t bytes() const {return (static_cast<std::size_t>(width)*height+7)/8;}
    void mark(Point p,bool observed=false) {
        const auto t=static_cast<std::size_t>(p.y*width+p.x);
        (observed?occupied:terrain)[t>>3]|=static_cast<std::uint8_t>(1u<<(t&7));
    }
    Input input(Point origin,unsigned speed,std::uint64_t generation=1,std::uint64_t epoch=10) const {
        return {width,height,origin,speed,std::span<const std::uint8_t>(terrain).first(bytes()),
                std::span<const std::uint8_t>(occupied).first(bytes()),
                std::span<const Point>(goals).first(goal_count),generation,epoch};
    }
};
struct Memory {
    // Deliberately uninitialized: the compiler's cursor initializes every read.
    std::array<Node,10000> nodes;std::array<Tile,10000> queue;std::array<Candidate,81> candidates;
    Workspace view() {return {nodes,queue,candidates};}
};
void finish(Compiler& compiler,std::size_t quota) {
    std::size_t calls=0;auto before=compiler.snapshot();
    while(before.status==Status::Working) {
        compiler.advance(quota);const auto after=compiler.snapshot();
        assert(after.work_units>before.work_units&&after.work_units-before.work_units<=quota);
        assert(after.initialized_cells>=before.initialized_cells);
        assert(!before.field_complete||after.field_complete);
        if(after.status==Status::Working)assert(!compiler.family().complete&&compiler.family().entries.empty());
        before=after;assert(++calls<500000);
    }
}
const Candidate* find(const ConditionalFamily& family,Point point) {
    for(const auto& candidate:family.entries)if(candidate.endpoint==point)return &candidate;
    return nullptr;
}
void equal_family(const ConditionalFamily& a,const ConditionalFamily& b) {
    assert(a.complete&&b.complete&&a.entries.size()==b.entries.size());
    for(std::size_t i=0;i<a.entries.size();++i) {
        assert(a.entries[i].endpoint==b.entries[i].endpoint&&a.entries[i].potential==b.entries[i].potential);
        assert(a.entries[i].walk_steps==b.entries[i].walk_steps&&a.entries[i].decreases==b.entries[i].decreases);
    }
}
}

int main() {
    int checks=0;
    // R18 observed body coordinates, in a SYNTHETIC empty static graph. This
    // is an occupancy regression, NOT a claim that actual Packs tile5,0 is quiet.
    Board actors{10,6};actors.goals[0]={9,0};actors.goal_count=1;
    constexpr std::array<Point,3> positions{{{3,0},{4,0},{4,1}}};
    for(auto p:positions)actors.mark(p,true);
    {
        Memory memory;Compiler compiler;auto input=actors.input(positions[0],1);
        assert(compiler.begin(input,memory.view()));++checks;
        assert(compiler.advance(0)==Status::Working&&compiler.snapshot().work_units==0);++checks;
        assert(!compiler.cell({3,0})&&!compiler.family().complete);++checks;
        compiler.advance(19);const auto progress=compiler.snapshot();
        auto later=input;later.occupancy_epoch=12345;
        assert(compiler.same_semantics(later.semantic_generation,later.origin,later.speed));++checks;
        assert(!compiler.begin(later,memory.view())&&compiler.snapshot().work_units==progress.work_units);++checks;
        assert(!compiler.reposition({3,1},1,1)&&compiler.snapshot().work_units==progress.work_units);++checks;
        finish(compiler,1);const auto family=compiler.family();
        assert(compiler.snapshot().status==Status::Ready&&family.complete);++checks;
        assert(!compiler.cell({3,0})->dated_observed_blocked);++checks;
        assert(compiler.cell({4,0})->dated_observed_blocked&&compiler.cell({4,1})->dated_observed_blocked);++checks;
        assert(!find(family,{3,0})&&!find(family,{4,0})&&!find(family,{4,1}));++checks;
        assert(family.entries.front().endpoint==Point(3,1)&&family.entries.front().decreases);++checks;
        assert(!compiler.snapshot().at_goal&&family.source_occupancy_epoch==10);++checks;
        bool nondecreasing_seen=false;
        for(const auto& candidate:family.entries) {
            assert(candidate.walk_steps==1&&candidate.endpoint!=input.origin);
            if(!candidate.decreases)nondecreasing_seen=true;
            else assert(!nondecreasing_seen);
        }++checks;
    }
    {
        Memory memory;Compiler compiler;assert(compiler.begin(actors.input(positions[0],4),memory.view()));
        finish(compiler,17);const auto family=compiler.family();const auto* alternate=find(family,{5,0});
        // Chebyshev distance2 is insufficient: the observed bodies force4steps.
        assert(alternate&&alternate->walk_steps==4&&alternate->decreases);++checks;
        assert(family.entries.size()<=80&&!find(family,{4,0})&&!find(family,{4,1}));++checks;
    }
    {
        Memory first,second;Compiler a,b;const auto input=actors.input(positions[0],4);
        assert(a.begin(input,first.view())&&b.begin(input,second.view()));finish(a,1);finish(b,17);
        equal_family(a.family(),b.family());
        for(int y=0;y<6;++y)for(int x=0;x<10;++x)assert(a.cell({x,y})->potential==b.cell({x,y})->potential);
        assert(a.snapshot().work_units==b.snapshot().work_units);++checks;
    }
    {
        Memory memory;Compiler compiler;assert(compiler.begin(actors.input(positions[0],2),memory.view()));finish(compiler,7);
        assert(!find(compiler.family(),{5,0}));++checks;
    }
    {
        Memory memory;Compiler compiler;assert(compiler.begin(actors.input(positions[0],1),memory.view()));finish(compiler,7);
        assert(compiler.reposition({3,1},1,1));finish(compiler,7);
        assert(compiler.family().origin==Point(3,1)&&compiler.family().graph_body_exclusion==Point(3,0));++checks;
        assert(compiler.family().projection_generation==2&&compiler.family().source_occupancy_epoch==10);++checks;
        assert(compiler.cell({4,0})->dated_observed_blocked&&compiler.cell({4,1})->dated_observed_blocked);++checks;
        assert(!compiler.reposition({4,0},1,1)&&compiler.snapshot().reason==Reason::DatedOriginBlocked);++checks;
        assert(compiler.cell({4,0})->dated_observed_blocked&&!compiler.family().complete);++checks;
    }
    for(std::size_t owner=1;owner<positions.size();++owner) {
        Memory memory;Compiler compiler;assert(compiler.begin(actors.input(positions[owner],1),memory.view()));finish(compiler,11);
        assert(!compiler.cell(positions[owner])->dated_observed_blocked);
        for(std::size_t other=0;other<positions.size();++other)if(other!=owner) {
            assert(compiler.cell(positions[other])->dated_observed_blocked&&!find(compiler.family(),positions[other]));
        }
        assert(find(compiler.family(),{5,0})&&find(compiler.family(),{5,0})->decreases);++checks;
    }
    {
        // Static quiet exclusion cannot be removed by observing a body or goal.
        // Tile5,0 is within the cited Packs wolf(8,2),range3 square; this fixture
        // tests that membership rule only, not a native map or route certificate.
        Board board{12,8};for(int y=0;y<=5;++y)for(int x=5;x<=11;++x)board.mark({x,y});
        board.goals[0]={5,0};board.goals[1]={3,4};board.goal_count=2;board.mark({3,0},true);
        Memory memory;Compiler compiler;assert(compiler.begin(board.input({3,0},4),memory.view()));finish(compiler,13);
        assert(compiler.snapshot().blocked_goals==1&&compiler.snapshot().accepted_goals==1);++checks;
        assert(compiler.cell({5,0})->static_blocked&&!find(compiler.family(),{5,0}));++checks;
    }
    {
        Board board{9,3};board.goals[0]={8,0};board.goal_count=1;
        // Continuous LSB packing: tile8 starts byte1; tile9 is next row/byte1bit1.
        board.mark({8,0});board.mark({0,1},true);board.mark({0,0},true);
        board.terrain[3]|=0xf8;board.occupied[3]|=0xf8; // unused27..31 ignored
        Memory memory;Compiler compiler;assert(compiler.begin(board.input({0,0},2),memory.view()));finish(compiler,1);
        assert(compiler.cell({8,0})->static_blocked&&compiler.cell({0,1})->dated_observed_blocked);++checks;
        assert(!compiler.cell({0,0})->dated_observed_blocked&&compiler.snapshot().blocked_goals==1);++checks;
        assert(compiler.snapshot().status==Status::UnreachableInModel&&compiler.snapshot().decreasing_count==0);++checks;
        for(const auto& candidate:compiler.family().entries)assert(candidate.potential==unreachable&&!candidate.decreases);
    }
    {
        Board board{5,5};board.goals[0]={4,4};board.goal_count=1;
        board.mark({1,0});board.mark({0,1});board.mark({1,1});
        Memory memory;Compiler compiler;assert(compiler.begin(board.input({0,0},4),memory.view()));finish(compiler,7);
        assert(compiler.snapshot().status==Status::UnreachableInModel&&compiler.family().complete&&compiler.family().entries.empty());++checks;
        assert(compiler.cell({4,4})->potential==0);++checks;
    }
    {
        Board board{3,3};board.goals[0]={0,0};board.goal_count=1;board.mark({0,0},true);
        Memory memory;Compiler compiler;assert(compiler.begin(board.input({0,0},0),memory.view()));finish(compiler,3);
        assert(compiler.snapshot().status==Status::Ready&&compiler.snapshot().at_goal&&compiler.family().entries.empty());++checks;
        assert(compiler.snapshot().local_discovered==1);++checks;
    }
    {
        Board board{3,3};board.goals[0]={2,2};board.goal_count=1;board.mark({0,0});board.mark({0,0},true);
        Memory memory;Compiler compiler;assert(compiler.begin(board.input({0,0},1),memory.view()));finish(compiler,3);
        assert(compiler.snapshot().status==Status::Unknown&&compiler.snapshot().reason==Reason::OriginStaticallyBlocked);++checks;
        assert(!compiler.family().complete&&compiler.family().entries.empty());++checks;
    }
    {
        Board board{3,3};board.goals[0]={1,1};board.goals[1]={1,1};board.goals[2]={2,2};board.goal_count=3;board.mark({2,2},true);
        Memory memory;Compiler compiler;assert(compiler.begin(board.input({0,0},1),memory.view()));finish(compiler,3);
        assert(compiler.snapshot().accepted_goals==1&&compiler.snapshot().blocked_goals==1);++checks;
        assert(find(compiler.family(),{1,1})->potential==0);++checks;
    }
    {
        Board board{11,11};board.goals[0]={10,10};board.goal_count=1;
        Memory memory;Compiler compiler;assert(compiler.begin(board.input({5,5},4),memory.view()));finish(compiler,17);
        assert(compiler.family().entries.size()==80&&compiler.snapshot().local_discovered==81);++checks;
        for(int y=1;y<=9;++y)for(int x=1;x<=9;++x)if(x!=5||y!=5) {
            const auto* candidate=find(compiler.family(),{x,y});assert(candidate&&candidate->walk_steps==std::max(std::abs(x-5),std::abs(y-5)));
        }
        ++checks;
    }
    {
        Board board{100,100};board.goals[0]={99,99};board.goal_count=1;
        Memory one,other;Compiler a,b;const auto input=board.input({0,0},4,7654,100);
        assert(a.begin(input,one.view())&&b.begin(input,other.view()));
        a.advance(10017);const auto partial=a.snapshot();
        assert(partial.initialized_cells==10000&&partial.reverse_discovered>0&&!partial.field_complete);++checks;
        auto later=input;later.occupancy_epoch=101;
        assert(!a.begin(later,one.view())&&a.snapshot().work_units==partial.work_units);++checks;
        finish(a,1);finish(b,17);equal_family(a.family(),b.family());
        assert(a.snapshot().work_units==b.snapshot().work_units&&a.snapshot().reverse_discovered==10000);++checks;
        for(int y=0;y<100;++y)for(int x=0;x<100;++x) {
            assert(a.cell({x,y})->potential==static_cast<unsigned>(std::max(99-x,99-y)));
            assert(a.cell({x,y})->potential==b.cell({x,y})->potential);
        }
        ++checks;
        assert(a.snapshot().origin_potential==99&&a.family().entries.front().endpoint==Point(4,4));++checks;
        assert(a.family().entries.front().potential==95&&a.family().entries.front().walk_steps==4&&a.family().entries.front().decreases);++checks;
        assert(a.family().entries.size()==24);++checks;
        const auto work=a.snapshot().work_units;assert(a.advance(17)==Status::Ready&&a.snapshot().work_units==work);++checks;
        const auto global_work=a.snapshot().global_work_units;const auto initial_endpoints=a.family().entries.size();
        std::uint64_t largest_projection=0;
        for(unsigned projection=0;projection<15;++projection) {
            const auto endpoint=a.family().entries.front();assert(endpoint.decreases);
            const auto before=a.snapshot().work_units;
            assert(a.reposition(endpoint.endpoint,4,7654)&&b.reposition(endpoint.endpoint,4,7654));
            assert(a.snapshot().status==Status::Working&&a.snapshot().field_complete&&!a.family().complete&&a.family().entries.empty());
            assert(a.snapshot().initialized_cells==10000&&a.snapshot().reverse_discovered==10000&&a.snapshot().global_work_units==global_work);
            finish(a,1);finish(b,17);equal_family(a.family(),b.family());
            assert(a.snapshot().global_work_units==global_work&&b.snapshot().global_work_units==global_work);
            assert(a.family().projection_generation==projection+2&&a.family().graph_body_exclusion==Point(0,0));
            assert(a.snapshot().work_units-before<5000);
            largest_projection=std::max(largest_projection,a.snapshot().work_units-before);
        }
        ++checks;
        assert(a.family().origin==Point(60,60)&&a.snapshot().origin_potential==39);++checks;
        const auto after_projection=a.snapshot();
        assert(a.reposition({60,60},4,7654)&&a.snapshot().work_units==after_projection.work_units&&a.family().projection_generation==16);++checks;
        assert(a.same_semantics(7654,{60,60},4)&&!a.same_semantics(7654,{0,0},4));++checks;
        std::printf("far field: %llu work units, %zu discovered, %zu initial endpoints\n",static_cast<unsigned long long>(work),a.snapshot().reverse_discovered,initial_endpoints);
        std::printf("15 projections: global work %llu unchanged; largest local %llu units\n",static_cast<unsigned long long>(global_work),static_cast<unsigned long long>(largest_projection));
    }
    {
        Board board{3,3};Memory memory;
        {Compiler c;auto input=board.input({0,0},1);input.width=101;assert(!c.begin(input,memory.view())&&c.snapshot().reason==Reason::Dimensions);}++checks;
        {Compiler c;auto input=board.input({0,0},1);input.width=0;assert(!c.begin(input,memory.view())&&!c.family().complete&&!c.cell({0,0}));}++checks;
        {Compiler c;auto input=board.input({0,0},1);input.dated_observed_blocked=input.dated_observed_blocked.first(1);assert(!c.begin(input,memory.view())&&c.snapshot().reason==Reason::PackedMaskLength);}++checks;
        {Compiler c;auto input=board.input({0,0},1);auto ws=memory.view();ws.queue=ws.queue.first(8);assert(!c.begin(input,ws)&&c.snapshot().reason==Reason::WorkspaceCapacity);}++checks;
        {Compiler c;auto input=board.input({0,0},1);auto ws=memory.view();ws.candidates=ws.candidates.first(80);assert(!c.begin(input,ws)&&c.snapshot().reason==Reason::WorkspaceCapacity);}++checks;
        {Compiler c;assert(!c.begin(board.input({0,0},5),memory.view())&&c.snapshot().reason==Reason::Speed);}++checks;
        {Compiler c;assert(!c.begin(board.input({-1,0},1),memory.view())&&c.snapshot().reason==Reason::Origin);}++checks;
        {Compiler c;board.goals[0]={3,0};board.goal_count=1;assert(c.begin(board.input({0,0},1),memory.view()));finish(c,1);assert(c.snapshot().reason==Reason::Goal);}++checks;
    }
    {
        Board board{3,3};board.goals[0]={2,2};board.goal_count=1;board.mark({1,0});
        Memory memory;Compiler compiler;assert(compiler.begin(board.input({0,0},1),memory.view()));finish(compiler,7);
        assert(!compiler.reposition({1,0},1,1)&&compiler.snapshot().reason==Reason::OriginStaticallyBlocked);++checks;
    }
    {
        Board board{3,3};board.goals[0]={2,2};board.goal_count=1;
        Memory memory;Compiler compiler;assert(compiler.begin(board.input({0,0},1),memory.view()));finish(compiler,7);
        assert(!compiler.reposition({1,1},1,2)&&compiler.snapshot().reason==Reason::SemanticGeneration);++checks;
    }
    std::printf("service route checks: %d passed; SDK calls: 0; conditional graph only\n",checks);
}
