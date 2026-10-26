#include <cassert>
#include "../matching/src/policy.h"
#include "../matching/src/commitment.h"

#ifdef NDEBUG
#error This actual caller fixture requires assertions enabled.
#endif
namespace c=p11_matching::commitment;
using p11_matching::Policy;
using p11_matching::Worker;
using p11_matching::Job;
using p11::Candidate;

namespace {
constexpr c::Domain domain{5,20,2000,8};
Candidate reward(int id,int travel=0) {
    Candidate result;result.id=id;result.gold=20;result.ready=true;
    result.hp=100;result.incoming=0;result.travel=travel;return result;
}
std::uint64_t encoded(const c::Board&board,const c::Domain&d=domain) {
    const auto word=c::encode(board,d);assert(word);return *word;
}
struct Memory {
    std::uint64_t word=0;
    int round=40,fuel=30000,reads=0,writes=0,now_calls=0;
    int change_on_now=0;
    bool change_after_read=false,drain_after_read=false;
    c::PublishResult publish(int slot,int job,const c::Domain&d=domain) {
        const int observed=round;
        return c::read_advance_publish(d,slot,job,observed,[&]{
            ++now_calls;if(change_on_now&&now_calls==change_on_now)++round;return round;
        },[&]{return fuel;},[&]{
            ++reads;if(change_after_read)++round;if(drain_after_read)fuel=10000;return word;
        },[&](std::uint64_t value){++writes;word=value;});
    }
};
}

int main() {
    assert(!c::decode_advance(0,domain,40).valid);
    auto board=c::empty(domain,40);board.rows[0]={2,0};board.rows[4]={19,0};
    const auto word=encoded(board);
    auto current=c::decode_advance(word,domain,40);
    assert(current.valid&&current.rows[0].job==2&&current.rows[4].job==19);
    assert(c::claimed(current,0,19)&&!c::claimed(current,0,2));
    auto previous=c::decode_advance(word,domain,41);
    assert(previous.valid&&previous.rows[0].age==1&&previous.rows[4].age==1);
    auto expired=c::decode_advance(word,domain,42);
    assert(expired.valid&&c::fixed_jobs(expired,0)==0&&expired.rows[0].job==-1);
    assert(c::fixed_jobs(c::decode_advance(word,domain,2000),0)==0);
    assert(!c::decode_advance(word,domain,39).valid); // Future absolute epoch.

    // Schema, domain, reserved bits, code bounds, duplicate live claims and
    // unused physical slots are all checked before accepting delegation.
    assert(!c::decode_advance(word^(std::uint64_t{1}<<54),domain,40).valid);
    assert(!c::decode_advance(word|(std::uint64_t{1}<<62),domain,40).valid);
    auto wrong=domain;wrong.actors=4;assert(!c::decode_advance(word,wrong,40).valid);
    wrong=domain;wrong.groups=19;assert(!c::decode_advance(word,wrong,40).valid);
    const auto invalid_code=(word&~(std::uint64_t{31}<<11))|(std::uint64_t{31}<<11);
    assert(!c::decode_advance(invalid_code,domain,40).valid);
    const auto duplicate=(word&~(std::uint64_t{127}<<39))|(std::uint64_t{3}<<39);
    assert(!c::decode_advance(duplicate,domain,40).valid);
    auto two=domain;two.actors=2;
    auto only_two=c::empty(two,40);only_two.rows[0]={1,0};
    assert(!c::decode_advance(encoded(only_two,two)|(std::uint64_t{1}<<39),two,40).valid);
    board.rows[4]={2,0};assert(!c::encode(board,domain));

    auto last=c::empty(domain,2000);last.rows[4]={19,0};
    assert(c::decode_advance(encoded(last),domain,2000).rows[4].job==19);
    assert(c::decode_advance(encoded(c::empty(domain,0)),domain,0).valid);
    assert(!c::supported(domain,2047));
    wrong=domain;wrong.bytes=7;assert(!c::supported(wrong,40));
    wrong=domain;wrong.actors=6;assert(!c::supported(wrong,40));
    wrong=domain;wrong.groups=32;assert(!c::supported(wrong,40));
    wrong=domain;wrong.max_rounds=2047;assert(!c::supported(wrong,40));
    auto maximal=domain;maximal.groups=31;
    auto max_job=c::empty(maximal,40);max_job.rows[4]={30,0};
    assert(c::decode_advance(encoded(max_job,maximal),maximal,40).rows[4].job==30);

    Memory memory;
    auto first=memory.publish(0,2);assert(first.status==c::Status::written&&memory.writes==1);
    auto fifth=memory.publish(4,19);assert(fifth.status==c::Status::written&&fifth.board.rows[0].job==2);
    const auto before_conflict=memory.word;
    auto conflict=memory.publish(1,2);assert(conflict.status==c::Status::conflict&&memory.word==before_conflict);
    auto released=memory.publish(0,-1);
    assert(released.status==c::Status::written&&released.board.rows[0].job==-1&&released.board.rows[4].job==19);
    ++memory.round;
    auto renewal=memory.publish(1,3);
    assert(renewal.board.rows[4].age==1&&renewal.board.rows[1].age==0);
    ++memory.round;
    auto aged=memory.publish(1,3);
    assert(aged.board.rows[4].job==-1&&aged.board.rows[1].age==0);

    Memory after_read;after_read.change_after_read=true;
    assert(after_read.publish(0,2).status==c::Status::stale&&after_read.reads==1&&after_read.writes==0);
    Memory before_write;before_write.change_on_now=4;
    assert(before_write.publish(0,2).status==c::Status::stale&&before_write.writes==0);
    Memory low;low.fuel=10000;
    assert(low.publish(0,2).status==c::Status::reserve&&low.reads==0&&low.writes==0);
    Memory drained;drained.drain_after_read=true;
    assert(drained.publish(0,2).status==c::Status::reserve&&drained.reads==1&&drained.writes==0);
    Memory unsupported;wrong=domain;wrong.bytes=7;
    assert(unsupported.publish(0,2,wrong).status==c::Status::unsupported&&unsupported.reads==0&&unsupported.writes==0);

    // Exact execution-consistency counterexample: locally optimal peer edges
    // consume job2 in Tavi's menu, but Varg's own private menu only contains3.
    p11::State state;state.round=40;state.field_shop=true;state.identity=11;
    Worker tavi{0,10,0,1000,1000,100,true,11};
    Worker varg{1,0,0,1000,1000,100,true,10};
    std::array<Worker,2> workers{{tavi,varg}};
    std::array<Job,1> tavi_jobs{{{reward(2,10),0,0}}};
    std::array<Job,1> varg_jobs{{{reward(3),0,0}}};
    const int legacy_tavi=Policy::assign(state,tavi_jobs,workers,tavi.id);
    state.identity=10;
    const int legacy_varg=Policy::assign(state,varg_jobs,workers,varg.id);
    assert(legacy_tavi==-1&&legacy_varg==3); // Nobody executes2.
    std::array<Candidate,1> tavi_menu{{tavi_jobs[0].candidate}},varg_menu{{varg_jobs[0].candidate}};
    Memory serial;
    state.identity=11;
    assert(c::select<Policy>(state,tavi_menu,tavi,{},0)==2);
    assert(serial.publish(0,2).status==c::Status::written);
    state.identity=10;
    const auto leases=c::decode_advance(serial.word,domain,serial.round);
    assert(c::select<Policy>(state,varg_menu,varg,leases,1)==3);
    assert(serial.publish(1,3).status==c::Status::written);
    auto both=c::decode_advance(serial.word,domain,40);
    assert(both.rows[0].job==2&&both.rows[1].job==3);

    // Fixed peer columns are removed before the actual selector. Own lease
    // remains usable; no finite hypothetical peer score or missing menu waits.
    std::array<Candidate,2> alternatives{{reward(2),reward(3)}};
    assert(c::select<Policy>(state,alternatives,varg,leases,1)==3);
    assert(c::select<Policy>(state,tavi_menu,varg,leases,1)==-1);
    state.identity=11;
    assert(c::select<Policy>(state,tavi_menu,tavi,leases,0)==2);
    auto busy=tavi;busy.available=false;
    assert(c::select<Policy>(state,tavi_menu,busy,{},0)==-1);
    assert(c::select<Policy>(state,tavi_menu,tavi,{},0)==Policy::select(state,tavi_menu,2));

    // Forced hostile stale writes illustrate the stated limitation: old age
    // cannot revive pastR+1, but stale whole images can erase newer leases.
    const auto old_word=serial.word;
    ++serial.round;assert(serial.publish(4,4).status==c::Status::written);
    serial.word=old_word;
    auto lost=c::decode_advance(serial.word,domain,serial.round);
    assert(lost.rows[0].age==1&&lost.rows[4].job==-1);
    assert(c::fixed_jobs(c::decode_advance(old_word,domain,42),4)==0);
}
