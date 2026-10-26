#pragma once
#include <array>
#include <cstdint>

// Eight bytes with DISJOINT writers: proposer owns bytes[0,2), role i owns
// bytes[2+2*i,4+2*i). No whole-word write/RMW exists. Each prepared write is ONE
// SDK range write of uint16_t. Native range-write/read atomicity still needs
// qualification; this pure protocol does not prove SDK/fuel behavior.
//
// A saved actual Frame dates the producer predicate BEFORE a fresh whole-eight-
// byte SDK read. Its immutable round is never replaced after suspension. A new
// header must be ACTUALLY seen while strictly future to arm it. Thus any old
// predicate has F<=old-header-read<new-publication<=future-arm<new-start and
// cannot pass date>=new-start, however late its disjoint write completes.
// These are causal events; their integer round numbers may be equal between
// read/publication/arming. The final strict future inequality still excludes F.
// The sole proposer VM must finish/discard a pending header before preparing
// another; it arms only from actual post-write readback, never the planned word.
//
// Wire phases/predicate bits are source claims, not current Body/HP, effects,
// tapes, numeric receipts or permission to act. A canonical contract and typed
// receipt consumer remain external. Work-prefix semantics/cleanup/reset
// admission must be established by that producer; timestamps alone do not
// identify arbitrary support slots or prove historical effects.
namespace wb::capital::party_wire {
inline constexpr int max_round=2047;inline constexpr unsigned roles_count=3;
inline constexpr std::uint16_t round_mask=2047;
enum class Phase : std::uint8_t {
    Unpublished,Committed,Arrived,Entered,WorkPrefix,Payout,Released,Banked,
    Recovered,Renewed,Abort,CleanupDone,ClosedClean,ClosedFailed
};
inline bool abort_branch(Phase phase){return phase==Phase::Abort||phase==Phase::CleanupDone||phase==Phase::ClosedFailed;}
struct Header {std::uint16_t start=0;std::uint8_t group=0;};
struct Record {std::uint16_t round=0;Phase phase=Phase::Unpublished;bool predicate=false;};
struct Encoded {bool valid=false;std::uint16_t word=0;};
inline Encoded encode_header(int start,unsigned group) {
    if(start<=0||start>max_round||group>31)return {};
    return {true,static_cast<std::uint16_t>(static_cast<unsigned>(start)|(group<<11))};
}
inline Header decode_header(std::uint16_t word) {
    return {static_cast<std::uint16_t>(word&round_mask),static_cast<std::uint8_t>(word>>11)};
}
inline Encoded encode_record(int round,Phase phase,bool predicate) {
    if(round<0||round>max_round||static_cast<unsigned>(phase)>static_cast<unsigned>(Phase::ClosedFailed))return {};
    if(phase==Phase::Unpublished)return !predicate&&round==0?Encoded{true,0}:Encoded{};
    return {true,static_cast<std::uint16_t>(static_cast<unsigned>(round)|(static_cast<unsigned>(phase)<<11)|(predicate?32768u:0u))};
}
inline Record decode_record(std::uint16_t word) {
    return {static_cast<std::uint16_t>(word&round_mask),static_cast<Phase>((word>>11)&15u),(word&32768u)!=0};
}
struct Roles {
    std::array<int,roles_count> ids{-1,-1,-1};
    bool accepted=false,exclusive_writers=false,legacy_publish_disabled=false;
};
struct Frame {int owner=-1,round=-1;std::uint64_t input=0;bool fresh=false;};
struct Snapshot {
    Frame source{};std::array<std::uint16_t,4> words{};
    bool whole_read=false;
    // Caller reconstructed THIS header's unique contract from public static
    // data plus the fixed accepted roles. Private observations cannot silently
    // select another contract under the same group/start.
    bool canonical_contract=false;
};
enum class Status {
    Unpublished,ArmedFuture,Active,UnarmedLate,NeedContract,NeedProposerReadback,
    Stale,Unsupported,NotParticipant,Invalid,HeaderChanged,ReusedEpoch,
    PendingWrite,NotProposer,ResetNotAdmitted,TooEarly,Prepared,Published,AbortSticky
};
enum class ClaimStatus {Unpublished,DatedPayload,EarlierEpoch,FutureRecord,Invalid};
struct Claim {ClaimStatus status=ClaimStatus::Unpublished;unsigned role=0;int actor=-1;Record payload{};};
struct View {
    Status status=Status::Invalid;Header header{};std::uint16_t raw_header=0;
    std::array<Claim,roles_count> claims{};
    bool armed=false,abort_seen=false;
};
enum class WriteKind {Header,Role};
struct Write {
    Status status=Status::Invalid;WriteKind kind=WriteKind::Role;
    unsigned byte_offset=0;std::uint16_t word=0,expected_header=0;
    Frame source{};std::uint64_t serial=0;bool prepared=false;
};
struct ProposalAdmission {bool canonical_contract=false,reset_admitted=false;};
namespace detail {
inline bool valid(const Roles& roles) {
    if(!roles.accepted||!roles.exclusive_writers||!roles.legacy_publish_disabled)return false;
    for(unsigned i=0;i<roles_count;++i) {
        if(roles.ids[i]<0)return false;
        for(unsigned j=0;j<i;++j)if(roles.ids[i]==roles.ids[j])return false;
    }
    return true;
}
inline bool same(const Frame& a,const Frame& b) {
    return a.fresh&&b.fresh&&a.owner==b.owner&&a.round==b.round&&a.input==b.input&&a.input!=0;
}
inline bool same(const Write& a,const Write& b) {
    return a.prepared&&b.prepared&&a.kind==b.kind&&a.byte_offset==b.byte_offset&&a.word==b.word
        &&a.expected_header==b.expected_header&&a.serial==b.serial&&same(a.source,b.source);
}
}
class Endpoint {
    Roles roles_{};int owner_=-1,role_=-1,last_round_=-1,last_armed_start_=0,last_issued_start_=0;
    std::uint16_t armed_=0;
    std::array<bool,roles_count> aborted_{};
    bool disabled_=false,header_pending_=false,record_pending_=false,local_abort_=false;
    Write pending_header_{},pending_record_{};std::uint64_t serial_=0;
    Status source(const Snapshot& s,const Frame& actual) {
        if(disabled_)return Status::Unsupported;
        if(!detail::valid(roles_)||role_<0)return Status::NotParticipant;
        if(actual.fresh&&actual.owner==owner_&&actual.round>max_round) {
            disabled_=true;armed_=0;return Status::Unsupported;
        }
        if(!s.whole_read||!detail::same(s.source,actual)||actual.owner!=owner_||actual.round<0
           ||actual.round<last_round_)return Status::Stale;
        last_round_=actual.round;return Status::Active;
    }
    void arm(std::uint16_t raw) {
        armed_=raw;last_armed_start_=decode_header(raw).start;aborted_={};local_abort_=false;
    }
    bool aborted() const {bool any=local_abort_;for(bool value:aborted_)any|=value;return any;}
    View observed(const Snapshot& s,const Frame& actual,bool proposer_readback) {
        View out;out.status=source(s,actual);if(out.status!=Status::Active)return out;
        out.raw_header=s.words[0];out.header=decode_header(out.raw_header);
        if(out.header.start==0) {armed_=0;out.status=Status::Unpublished;return out;}
        if(armed_&&armed_!=out.raw_header)armed_=0;
        if(!s.canonical_contract){out.status=Status::NeedContract;return out;}
        if(!armed_) {
            if(out.header.start<=last_armed_start_){out.status=Status::ReusedEpoch;return out;}
            if(actual.round>=out.header.start){out.status=Status::UnarmedLate;return out;}
            if(role_==0&&!proposer_readback){out.status=Status::NeedProposerReadback;return out;}
            arm(out.raw_header);
        }
        out.armed=true;out.abort_seen=aborted();
        if(actual.round<out.header.start){out.status=Status::ArmedFuture;return out;}
        out.status=Status::Active;
        for(unsigned i=0;i<roles_count;++i) {
            auto& claim=out.claims[i];claim.role=i;claim.actor=roles_.ids[i];
            const auto raw=s.words[i+1];claim.payload=decode_record(raw);
            if(raw==0)continue;
            if(claim.payload.round<out.header.start){claim.status=ClaimStatus::EarlierEpoch;continue;}
            if(claim.payload.round>actual.round){claim.status=ClaimStatus::FutureRecord;continue;}
            if(claim.payload.phase==Phase::Unpublished||static_cast<unsigned>(claim.payload.phase)>static_cast<unsigned>(Phase::ClosedFailed)) {
                claim.status=ClaimStatus::Invalid;continue;
            }
            claim.status=ClaimStatus::DatedPayload;
            // A fixed writer may withdraw without manufacturing any positive
            // gameplay predicate. Cleanup/failed closure cannot hide that abort.
            if(abort_branch(claim.payload.phase))aborted_[i]=true;
        }
        out.abort_seen=aborted();return out;
    }
public:
    Endpoint(Roles roles,int owner):roles_(roles),owner_(owner) {
        for(unsigned i=0;i<roles_count;++i)if(roles_.ids[i]==owner)role_=static_cast<int>(i);
    }
    int role() const{return role_;}
    std::uint16_t armed_header() const{return armed_;}
    bool abort_seen() const{return aborted();}
    bool pending_write() const{return header_pending_||record_pending_;}
    Write pending_header_write() const{return header_pending_?pending_header_:Write{};}
    Write pending_record_write() const{return record_pending_?pending_record_:Write{};}
    // External deadline/failure closure. A previously armed start cannot rearm
    // merely because its bytes remain in memory. It needs a strictly new epoch.
    void disarm(){armed_=0;}
    View observe(const Snapshot& s,const Frame& actual){return observed(s,actual,false);}
    Write prepare_proposal(const Snapshot& s,const Frame& actual,unsigned group,ProposalAdmission admission) {
        Write out;out.kind=WriteKind::Header;out.byte_offset=0;out.status=source(s,actual);
        if(out.status!=Status::Active)return out;
        if(role_!=0){out.status=Status::NotProposer;return out;}
        if(header_pending_||record_pending_){out.status=Status::PendingWrite;return out;}
        if(!admission.canonical_contract){out.status=Status::NeedContract;return out;}
        if(!admission.reset_admitted){out.status=Status::ResetNotAdmitted;return out;}
        const auto encoded=encode_header(actual.round+2,group);
        if(!encoded.valid){out.status=Status::Unsupported;return out;}
        const auto start=decode_header(encoded.word).start,prior=decode_header(s.words[0]).start;
        if(start<=last_issued_start_||start<=last_armed_start_||start<=prior){out.status=Status::ReusedEpoch;return out;}
        out.status=Status::Prepared;out.prepared=true;out.word=encoded.word;out.expected_header=s.words[0];out.source=actual;out.serial=++serial_;
        last_issued_start_=start;pending_header_=out;header_pending_=true;armed_=0;return out;
    }
    // Call ONLY after the actual sole header SDK write has returned and a new
    // whole-word readback has been fenced against actual current input. Late
    // completion clears the pending PC but grants no planned/future arming.
    View confirm_proposal(const Write& write,const Snapshot& post,const Frame& actual) {
        View out;
        if(!header_pending_||!detail::same(write,pending_header_)){out.status=Status::Invalid;return out;}
        header_pending_=false;
        out.status=source(post,actual);if(out.status!=Status::Active)return out;
        if(post.words[0]!=write.word){armed_=0;out.status=Status::HeaderChanged;return out;}
        return observed(post,actual,true);
    }
    Write prepare_record(const Snapshot& s,const Frame& actual,Phase phase,bool predicate) {
        Write out;out.kind=WriteKind::Role;out.status=source(s,actual);
        if(out.status!=Status::Active)return out;
        if(header_pending_||record_pending_){out.status=Status::PendingWrite;return out;}
        const auto view=observe(s,actual);out.status=view.status;
        if(view.status!=Status::Active)return out;
        if((aborted()&&!abort_branch(phase))||(phase==Phase::ClosedClean&&aborted())){out.status=Status::AbortSticky;return out;}
        const auto encoded=encode_record(actual.round,phase,predicate);
        if(!encoded.valid||phase==Phase::Unpublished){out.status=Status::Invalid;return out;}
        if(abort_branch(phase))local_abort_=true;
        out.status=Status::Prepared;out.prepared=true;out.word=encoded.word;out.expected_header=view.raw_header;
        out.byte_offset=2u+2u*static_cast<unsigned>(role_);out.source=actual;out.serial=++serial_;
        pending_record_=out;record_pending_=true;return out;
    }
    // Publication confirmation is metadata only. It does not confirm any game
    // effect, HP, movement, channel, commitment predicate or cleanup quantity.
    Status confirm_record(const Write& write,const Snapshot& post,const Frame& actual) {
        if(!record_pending_||!detail::same(write,pending_record_))return Status::Invalid;
        record_pending_=false;
        const auto state=source(post,actual);if(state!=Status::Active)return state;
        if(post.words[0]!=write.expected_header){armed_=0;return Status::HeaderChanged;}
        if(post.words[static_cast<unsigned>(role_)+1]!=write.word)return Status::Invalid;
        return Status::Published;
    }
    // Native adapter may discard ONLY when it knows the SDK write was never
    // issued. A suspended/pending SDK call must finish first; same-VM seriality
    // is a producer premise, not something cancellation can manufacture.
    bool discard_unissued(const Write& write) {
        if(header_pending_&&detail::same(write,pending_header_)){header_pending_=false;return true;}
        if(record_pending_&&detail::same(write,pending_record_)){record_pending_=false;return true;}
        return false;
    }
};
}
