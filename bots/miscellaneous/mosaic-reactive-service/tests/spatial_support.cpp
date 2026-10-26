#include "spatial_support.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>

using namespace mosaic::spatial;
void same_selection(const Selection& a,const Selection& b){
    assert(a.complete==b.complete&&a.size()==b.size());
    for(std::size_t i=0;i<a.size();++i)assert(a[i]==b[i]);
}
void complete(Index& index,std::size_t batch){
    std::size_t calls=0;
    while(index.status()==BuildState::building){
        assert(!index.borrow().valid(index.count()));
        index.advance_build(batch);assert(++calls<=4096);
    }
    assert(index.status()==BuildState::ready&&index.valid());
}
int main() {
    int checks=0;
    const std::array entries{Entry{27,4,3,1},Entry{20,1,0,4},Entry{9,-2,3,0},Entry{6,4,3,5},Entry{4,100,0,3}};
    Index index;
    assert(!index.build(entries,false)&&!index.borrow().valid(entries.size()));++checks;
    assert(index.build(entries,true));const auto token=index.borrow();
    assert(token.valid(entries.size())&&!token.valid(entries.size()-1));++checks;
    const auto exact=index.hits(4,3,0);
    assert(exact.complete&&exact.size()==2&&exact[0]==0&&exact[1]==3);++checks;
    const auto edge=index.threats(0,0);
    assert(edge.complete&&edge.size()==2&&edge[0]==1&&edge[1]==3);++checks;
    const auto moved=index.threats(0,0,1);
    assert(moved.size()==2&&moved[0]==1&&moved[1]==3);++checks;
    // Each finite query must exactly match the defining tile/range relation,
    // including duplicate positions and nonmonotone native handles.
    for(int y=-5;y<=8;++y) {
      for(int x=-5;x<=8;++x) {
       for(int radius=0;radius<=5;++radius) {
        const auto selected=index.hits(x,y,radius);std::vector<std::size_t> expected;
        for(std::size_t i=0;i<entries.size();++i) {
            const auto& e=entries[i];const int dx=e.x>x?e.x-x:x-e.x,dy=e.y>y?e.y-y:y-e.y;
            if(dx<=radius&&dy<=radius)expected.push_back(i);
        }
        assert(selected.complete&&selected.size()==expected.size());
        for(std::size_t i=0;i<expected.size();++i)assert(selected[i]==expected[i]);
        const auto threats=index.threats(x,y,radius);expected.clear();
        for(std::size_t i=0;i<entries.size();++i) {
            const auto& e=entries[i];const int dx=e.x>x?e.x-x:x-e.x,dy=e.y>y?e.y-y:y-e.y;
            if(dx<=e.range+radius&&dy<=e.range+radius)expected.push_back(i);
        }
        assert(threats.complete&&threats.size()==expected.size());
        for(std::size_t i=0;i<expected.size();++i)assert(threats[i]==expected[i]);
       }
      }
    }
    ++checks;
    assert(index.observe(0,27,4,3,1)&&token.valid(entries.size()));++checks;
    assert(!index.observe(0,27,5,3,1)&&!token.valid(entries.size())&&!index.hits(4,3,1).complete);++checks;
    assert(index.build(entries,true)&&!token.valid(entries.size()));++checks;
    assert(!index.observe(0,20,4,3,1)&&!index.valid());++checks;
    assert(index.build(entries,true)&&!index.observe(0,27,4,3,2)&&!index.valid());++checks;
    std::array<Entry,161> overflow{};
    assert(!index.build(overflow,true)&&!index.borrow().valid(overflow.size()));++checks;
    const std::array duplicate{Entry{4,0,0,1},Entry{4,1,1,1}};
    assert(!index.build(duplicate,true));++checks;
    const std::array extreme{Entry{1,std::numeric_limits<int>::min(),0,0},Entry{2,std::numeric_limits<int>::max(),0,1}};
    assert(index.build(extreme,true)&&index.hits(std::numeric_limits<int>::min(),0,0).size()==1);++checks;
    assert(index.threats(std::numeric_limits<int>::max(),0).size()==1);++checks;
    // Atomic std::sort construction is an independent reference for the guarded
    // incremental merge builder, including every non-power-of-two tail shape.
    for(const std::size_t count:{0u,1u,2u,3u,7u,8u,9u,30u,51u,127u,128u,129u,160u}) {
        std::vector<Entry> layout;
        for(std::size_t i=0;i<count;++i)layout.push_back({int(1000+(i*37)%161),int((i*13)%31)-15,int((i*17)%23)-11,int(i%6)});
        Index reference;assert(reference.build(layout,true));
        for(const std::size_t batch:{1u,2u,7u,32u,257u}) {
            Index incremental;assert(incremental.begin_build(count,true));
            assert(!incremental.valid()&&!incremental.borrow().valid(count));
            const auto initial=incremental.status();assert(incremental.advance_build(0)==initial);
            for(std::size_t i=0;i<count;++i) {
                assert(incremental.input_count()==i&&incremental.status()==BuildState::collecting);
                assert(incremental.advance_build(100)==BuildState::collecting);
                assert(incremental.append(layout[i])&&incremental.input_count()==i+1&&!incremental.valid());
            }
            complete(incremental,batch);assert(incremental.borrow().valid(count));
            for(int y=-12;y<=12;y+=4)for(int x=-16;x<=16;x+=4)for(int radius=0;radius<=5;++radius) {
                same_selection(incremental.hits(x,y,radius),reference.hits(x,y,radius));
                same_selection(incremental.threats(x,y,radius),reference.threats(x,y,radius));
            }
        }
    }
    ++checks;
    const std::array restart_layout{Entry{40,3,4,3},Entry{2,1,0,4},Entry{99,5,4,0},Entry{8,0,0,3},Entry{7,2,2,5},
        Entry{3,3,1,1},Entry{10,1,1,2},Entry{4,2,0,3},Entry{20,4,3,4}};
    for(const std::size_t cut:{0u,1u,9u,35u,36u,37u,43u,44u,45u,79u,80u,81u,88u}) {
        Index incremental;assert(incremental.build(restart_layout,true));const auto old=incremental.borrow();
        assert(incremental.begin_build(restart_layout.size(),true)&&!old.valid(restart_layout.size()));
        for(auto entry:restart_layout)assert(incremental.append(entry));
        incremental.advance_build(cut);incremental.revoke();
        assert(incremental.status()==BuildState::failed&&incremental.advance_build(4096)==BuildState::failed&&
            !incremental.borrow().valid(restart_layout.size())&&!incremental.hits(0,0,9).complete);
        assert(incremental.begin_build(restart_layout.size(),true));
        for(auto entry:restart_layout)assert(incremental.append(entry));
        complete(incremental,1);assert(!old.valid(restart_layout.size()));
    }
    ++checks;
    assert(index.begin_build(160,true));
    for(int i=0;i<160;++i)assert(index.append({i==159?0:i,i%20,i/20,3}));
    while(index.status()==BuildState::building)index.advance_build(1);
    assert(index.status()==BuildState::failed&&!index.valid());++checks;
    assert(!index.begin_build(161,true)&&index.status()==BuildState::failed&&!index.valid());++checks;
    assert(!index.begin_build(1,false)&&index.status()==BuildState::failed);++checks;
    assert(index.begin_build(1,true)&&!index.append({-1,0,0,3})&&index.status()==BuildState::failed);++checks;
    assert(index.begin_build(1,true)&&!index.append({1,0,0,-1})&&index.status()==BuildState::failed);++checks;
    assert(index.begin_build(1,true)&&index.append({1,0,0,1}));complete(index,1);
    const auto before_extra=index.borrow();assert(!index.append({2,0,0,1})&&!before_extra.valid(1));++checks;
    assert(index.begin_build(2,true)&&index.append({8,0,0,1}));index.revoke();
    assert(index.status()==BuildState::failed&&index.advance_build(1)==BuildState::failed);
    assert(index.build(restart_layout,true));const auto rebuilt=index.hits(2,2,2);
    assert(rebuilt.size()==8&&rebuilt[2]==3&&rebuilt[7]==8);++checks;
    std::printf("spatial support checks: %d passed\n",checks);
}
