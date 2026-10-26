#pragma once
// Persisted DATA ONLY attribution around actual caller operations. Samples are
// supplied by the caller; no SDK/clock, allocation, guard, logging or destructor
// callback lives here. Observed costs/peaks NEVER authorize subsequent work.
#include <array>
#include <cstdint>
#include <limits>

namespace wb::capital::service_stage_meter {
enum class Stage : std::uint16_t {
    Capture=0,History,Cold,StaticChunk,GeometryCopy,Altar,Hypothesis,Order,
    Investment,RouteBegin,RouteAdvance,Vision,SimBegin,SimAdvance,Rebind,
    Activation,Readback,LegacyTail,Count,Unknown=65535
};
inline constexpr unsigned stage_count=unsigned(Stage::Count),nesting_limit=8;
struct Source {std::uint64_t id{},version{};bool known{};};
struct Epoch {int owner{-1},round{-1};std::uint64_t input{},effect{};bool known{};};
struct Sample {Source source;Epoch epoch;std::uint64_t remaining{};bool fuel_known{},fresh{};};
struct Cost {bool known{};std::uint64_t value{};};
enum class Exit : std::uint8_t {Returned,Threw,Incomplete};
enum Issue : std::uint32_t {
    None=0,UnknownSource=1u,ChangedSource=2u,UnknownEpoch=4u,StaleSample=8u,
    CrossRound=16u,ChangedEpoch=32u,UnknownFuel=64u,NonMonotone=128u,
    Thrown=256u,Unfinished=512u,OutOfOrder=1024u,InvalidToken=2048u,
    Capacity=4096u,UnknownStage=8192u
};
struct Report {
    Stage stage{Stage::Unknown};Sample before,after;Cost cost;
    std::uint32_t issues{};Exit exit{Exit::Incomplete};std::uint64_t serial{};
    bool closed{},overlapping{},active{};
};
struct Stats {
    std::uint64_t starts{},returned{},cross_round{},epoch_mismatch{},unknown{},
        threw{},incomplete{},overlapping{},rejected_finishes{},dropped_starts{};
    Cost last_cost,peak_cost;Source peak_source;Epoch peak_epoch;
    bool peak_overlapping{};std::uint32_t last_issues{};
};

class Recorder;
class Token {
    const Recorder* owner_{};std::uint64_t generation_{},serial_{};
    unsigned depth_{};Stage stage_{Stage::Unknown};friend class Recorder;
public:
    Token()=default;
    // Minted, not a query of current membership. Tokens must not outlive the
    // Recorder itself; finish validates membership in that retained lifetime.
    bool valid()const{return owner_&&generation_&&serial_;}
};

class Recorder {
    struct Frame {Stage stage{Stage::Unknown};Sample before;std::uint64_t serial{};bool overlapping{};};
    std::array<Frame,nesting_limit> stack_{};
    std::array<Stats,stage_count> stats_{};
    unsigned depth_{};std::uint64_t generation_{1},serial_{},rejected_{},dropped_{};
    bool exhausted_{};Report last_;
    static void inc(std::uint64_t& value)noexcept {
        if(value!=std::numeric_limits<std::uint64_t>::max())++value;
    }
    static bool valid(Stage stage)noexcept{return unsigned(stage)<stage_count;}
    static bool epoch_known(const Epoch& epoch)noexcept {
        // Owner0, round0, input0 and effect0 are valid when explicitly known.
        return epoch.known&&epoch.owner>=0&&epoch.round>=0;
    }
    static std::uint32_t compare(const Sample& before,const Sample& after)noexcept {
        std::uint32_t issues=0;
        if(!before.source.known||!after.source.known)issues|=UnknownSource;
        else if(before.source.id!=after.source.id||before.source.version!=after.source.version)issues|=ChangedSource;
        if(!before.fresh||!after.fresh)issues|=StaleSample;
        if(!epoch_known(before.epoch)||!epoch_known(after.epoch))issues|=UnknownEpoch;
        else if(before.epoch.round!=after.epoch.round)issues|=CrossRound;
        else if(before.epoch.owner!=after.epoch.owner||before.epoch.input!=after.epoch.input||before.epoch.effect!=after.epoch.effect)
            issues|=ChangedEpoch;
        if(!before.fuel_known||!after.fuel_known)issues|=UnknownFuel;
        else if(after.remaining>before.remaining)issues|=NonMonotone;
        return issues;
    }
    Report reject(const Token& token,std::uint32_t issue)noexcept {
        inc(rejected_);
        if(valid(token.stage_))inc(stats_[unsigned(token.stage_)].rejected_finishes);
        // Rejection cannot pop, relabel, or finish a different active interval.
        // Preserve the real active/last stage rather than attributing a stale
        // token's stage to the operation actually in progress.
        auto out=depth_?active():last_;out.closed=false;out.cost={};out.issues=issue;
        out.exit=Exit::Incomplete;last_=out;return out;
    }
public:
    Recorder()=default;
    Recorder(const Recorder&)=delete;Recorder& operator=(const Recorder&)=delete;
    Recorder(Recorder&&)=delete;Recorder& operator=(Recorder&&)=delete;
    unsigned depth()const noexcept{return depth_;}
    std::uint64_t rejected_finishes()const noexcept{return rejected_;}
    std::uint64_t dropped_starts()const noexcept{return dropped_;}
    const Stats* stats(Stage stage)const noexcept{return valid(stage)?&stats_[unsigned(stage)]:nullptr;}
    const Report& last()const noexcept{return last_;}
    Report active()const noexcept {
        Report out;if(!depth_)return out;
        const auto& frame=stack_[depth_-1];out.stage=frame.stage;out.before=frame.before;
        out.serial=frame.serial;out.overlapping=frame.overlapping;out.active=true;return out;
    }

    // Commit this marker BEFORE the real heavy call. It survives a suspension
    // or throw until the caller supplies a matching finish/explicit interruption.
    Token begin(Stage stage,const Sample& before)noexcept {
        if(!valid(stage)||depth_==nesting_limit||exhausted_||serial_==std::numeric_limits<std::uint64_t>::max()) {
            inc(dropped_);if(valid(stage))inc(stats_[unsigned(stage)].dropped_starts);
            last_={};last_.stage=stage;last_.before=before;last_.issues=valid(stage)?Capacity:UnknownStage;return {};
        }
        if(depth_)for(unsigned i=0;i<depth_;++i)stack_[i].overlapping=true;
        auto& frame=stack_[depth_];frame={stage,before,++serial_,depth_!=0};
        Token token;token.owner_=this;token.generation_=generation_;token.serial_=frame.serial;
        token.depth_=depth_;token.stage_=stage;++depth_;inc(stats_[unsigned(stage)].starts);
        last_=active();return token;
    }

    // Strict LIFO. Same-stage tokens are still distinct; stale/reused/foreign
    // tokens never consume a newer interval. A Returned call with bad samples
    // counts as a return, but its COST IS UNKNOWN. Threw/Incomplete always have
    // unknown cost even if the supplied samples appear comparable.
    Report finish(Token token,const Sample& after,Exit exit=Exit::Returned)noexcept {
        if(token.owner_!=this||!token.valid()||token.generation_!=generation_||token.depth_>=depth_)
            return reject(token,InvalidToken);
        const auto& candidate=stack_[token.depth_];
        if(candidate.serial!=token.serial_||candidate.stage!=token.stage_)return reject(token,InvalidToken);
        if(token.depth_+1!=depth_)return reject(token,OutOfOrder);
        const auto frame=candidate;--depth_;
        Report out;out.stage=frame.stage;out.before=frame.before;out.after=after;out.serial=frame.serial;
        out.exit=exit;out.closed=true;out.overlapping=frame.overlapping;out.issues=compare(frame.before,after);
        if(exit==Exit::Threw)out.issues|=Thrown;
        else if(exit!=Exit::Returned)out.issues|=Unfinished;
        if(out.issues==None)out.cost={true,frame.before.remaining-after.remaining};
        auto& values=stats_[unsigned(out.stage)];values.last_cost=out.cost;values.last_issues=out.issues;
        if(exit==Exit::Returned)inc(values.returned);
        else if(exit==Exit::Threw)inc(values.threw);
        else inc(values.incomplete);
        if(out.issues&CrossRound)inc(values.cross_round);
        if(out.issues&ChangedEpoch)inc(values.epoch_mismatch);
        if(!out.cost.known)inc(values.unknown);
        if(out.overlapping)inc(values.overlapping);
        if(out.cost.known&&(!values.peak_cost.known||out.cost.value>values.peak_cost.value)) {
            values.peak_cost=out.cost;values.peak_source=out.before.source;values.peak_epoch=out.before.epoch;
            values.peak_overlapping=out.overlapping;
        }
        // Costs are INCLUSIVE intervals. There is deliberately no total or
        // exclusive-cost arithmetic: nested intervals overlap and cannot be
        // summed as unique work. Every peak retains its measurement provenance.
        last_=out;return out;
    }

    void interrupt_all(const Sample& after={},Exit exit=Exit::Incomplete)noexcept {
        // This API cannot turn abandoned intervals into successful measurements.
        if(exit==Exit::Returned)exit=Exit::Incomplete;
        while(depth_) {
            const auto& frame=stack_[depth_-1];Token token;token.owner_=this;token.generation_=generation_;
            token.serial_=frame.serial;token.depth_=depth_-1;token.stage_=frame.stage;
            finish(token,after,exit);
        }
    }
    void restart(const Sample& after={})noexcept {
        interrupt_all(after,Exit::Incomplete);
        // Retain counters and last stage, invalidate every old token. Neither
        // counter/identity saturation nor a restart can wrap into a reused ID.
        if(generation_==std::numeric_limits<std::uint64_t>::max())exhausted_=true;
        else ++generation_;
    }
};
} // namespace wb::capital::service_stage_meter
