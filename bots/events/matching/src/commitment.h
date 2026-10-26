#pragma once
#include <cstddef>
#include <optional>
#include "assignment.h"

namespace p11_matching::commitment {
struct Domain {
    int actors=0,groups=0,max_rounds=2000;
    std::size_t bytes=8;
};
struct Row {int job=-1,age=2;};
struct Board {
    int round=-1;
    std::array<Row,5> rows{};
    bool valid=false;
};
enum class Status {written,unsupported,stale,reserve,conflict};
struct PublishResult {Status status=Status::unsupported;Board board{};};

inline bool supported(const Domain&domain,int round) {
    return domain.bytes>=8&&domain.actors>0&&domain.actors<=5
        &&domain.groups>0&&domain.groups<=31&&domain.max_rounds>0
        &&domain.max_rounds<2047&&round>=0&&round<2047;
}
inline Board empty(const Domain&domain,int round) {
    Board board;
    if(supported(domain,round)){board.round=round;board.valid=true;}
    return board;
}

// All timestamps are absolute. Ages0/1 survive; older leases are empty.
// The wire schema uses46 payload bits, then3 actor/5 group/8 schema bits.
inline Board decode_advance(std::uint64_t word,const Domain&domain,int round) {
    if(!supported(domain,round)||!word||(word>>62)!=0
        ||((word>>54)&255)!=0xB1||int((word>>46)&7)!=domain.actors
        ||int((word>>49)&31)!=domain.groups)return {};
    const int epoch=int(word&2047);
    if(!epoch||epoch-1>round)return {};
    const int delta=round-(epoch-1);
    auto board=empty(domain,round);
    std::uint32_t used=0;
    for(int actor=0;actor<5;++actor) {
        const unsigned row=unsigned((word>>(11+7*actor))&127);
        if(actor>=domain.actors){if(row)return {};continue;}
        const int code=int(row&31),age=int(row>>5);
        if(code>domain.groups||(!code&&age))return {};
        if(!code||age+delta>=2)continue;
        const auto bit=std::uint32_t{1}<<(code-1);
        if(used&bit)return {};
        used|=bit;board.rows[actor]={code-1,age+delta};
    }
    return board;
}
inline std::optional<std::uint64_t> encode(const Board&board,const Domain&domain) {
    if(!board.valid||!supported(domain,board.round))return std::nullopt;
    std::uint64_t word=std::uint64_t(board.round+1)
        |(std::uint64_t(domain.actors)<<46)|(std::uint64_t(domain.groups)<<49)
        |(std::uint64_t{0xB1}<<54);
    std::uint32_t used=0;
    for(int actor=0;actor<5;++actor) {
        const auto&row=board.rows[actor];
        if(row.age<0||row.age>3||row.job< -1||row.job>=domain.groups)return std::nullopt;
        if(actor>=domain.actors){if(row.job>=0)return std::nullopt;continue;}
        if(row.job<0||row.age>=2)continue;
        const auto bit=std::uint32_t{1}<<row.job;
        if(used&bit)return std::nullopt;
        used|=bit;
        const auto packed=std::uint64_t(row.job+1)|(std::uint64_t(row.age)<<5);
        word|=packed<<(11+7*actor);
    }
    return word;
}
inline std::uint32_t fixed_jobs(const Board&board,int self_slot) {
    if(!board.valid||self_slot<0||self_slot>=5)return 0;
    std::uint32_t jobs=0;
    for(int actor=0;actor<5;++actor) {
        const auto&row=board.rows[actor];
        if(actor!=self_slot&&row.job>=0&&row.job<31&&row.age>=0&&row.age<2)
            jobs|=std::uint32_t{1}<<row.job;
    }
    return jobs;
}
inline bool claimed(const Board&board,int self_slot,int job) {
    return job>=0&&job<31&&(fixed_jobs(board,self_slot)&(std::uint32_t{1}<<job));
}

// Peer leases are fixed rows/columns already eliminated from the residual.
// The unchanged actual DP and selector see only self's completed details.
template<class Policy>
int select(const p11::State&state,std::span<const p11::Candidate>candidates,
           Worker self,const Board&board,int self_slot) {
    if(candidates.size()>capacity)return -1;
    std::array<p11::Candidate,capacity> available{};
    std::array<Job,capacity> jobs{};int count=0;
    for(const auto&candidate:candidates)if(!claimed(board,self_slot,candidate.id)) {
        available[count]=candidate;jobs[count]={candidate,0,0};++count;
    }
    const int assigned=p11_matching::assign(state,std::span<const Job>{jobs.data(),std::size_t(count)},
        std::span<const Worker>{&self,1},self.id);
    return Policy::select(state,std::span<const p11::Candidate>{available.data(),std::size_t(count)},assigned);
}

// Guarded serial RMW, not CAS: a suspended old write can lose newer leases.
// Absolute epochs prevent revival after expiry. Reserve10000 is admission,
// not an unproved universal host-cost bound. No retry, allocation or yield.
template<class Now,class Fuel,class Read8,class Write8>
PublishResult read_advance_publish(const Domain&domain,int self_slot,int job,int observed_round,
                                 Now now,Fuel fuel,Read8 read,Write8 write) {
    if(!supported(domain,observed_round)||self_slot<0||self_slot>=domain.actors
        ||job< -1||job>=domain.groups)return {};
    if(now()!=observed_round)return {Status::stale,{}};
    if(fuel()<=10000)return {Status::reserve,{}};
    const auto word=read();
    if(now()!=observed_round)return {Status::stale,{}};
    auto board=decode_advance(word,domain,observed_round);
    if(!board.valid)board=empty(domain,observed_round);
    if(job>=0&&claimed(board,self_slot,job))return {Status::conflict,board};
    board.rows[self_slot]={job,job>=0?0:2};
    const auto encoded=encode(board,domain);
    if(!encoded)return {};
    if(now()!=observed_round)return {Status::stale,{}};
    if(fuel()<=10000)return {Status::reserve,{}};
    if(now()!=observed_round)return {Status::stale,{}};
    write(*encoded);
    return {Status::written,board};
}
} // namespace p11_matching::commitment
