#pragma once
// Legal service attempts over one fresh selected Wait packet. A start certificate
// covers the declared next-camp-phase model, not uninterrupted completion of a
// multi-tick channel or safety from unseen/future enemy actions. Reobserve later.
#include "selected_transition.h"

namespace mosaic::conversion {
using Stamp=selected::Stamp;
enum class Kind {recall_start,recall_cancel,tribute_start,tribute_cancel,buy};
enum class Channel {none,recall,tribute,teleport};
struct Request {
    Kind kind{Kind::recall_start};Stamp stamp;std::uint64_t serial{};
    cambc::vec2 altar{};int item{-1};std::uint32_t count{};
};
inline bool same_request(const Request& a,const Request& b) {
    return a.kind==b.kind&&a.stamp.matches(b.stamp)&&a.serial==b.serial&&a.altar==b.altar&&a.item==b.item&&a.count==b.count;
}
struct History {
    int epoch{-1},actor{-1};
    bool recall_started{},recall_cancelled{},tribute_started{},tribute_cancelled{};
};
struct Facts {
    Request request;Stamp stamp;std::uint64_t packet_nonce{};
    spatial::Token layout;cambc::vec2 origin{};
    bool complete{},history_complete{},bag_complete{},channel_complete{},settings_complete{};
    bool altar_complete{},lock_complete{},altar_visible{};
    History history;Channel channel{Channel::none};
    int remaining{-1},gems{-1},recall_duration{-1},submit_range{-1},submit_base{},submit_min{-1};
    double submit_multiplier{-1};cambc::vec2 altar{};bool altar_valid{},enemy_lock{};
};
enum Flag : std::uint64_t {
    stale_request=1ull<<0,missing_facts=1ull<<1,unavailable=1ull<<2,
    repeated_start=1ull<<3,repeated_cancel=1ull<<4,wrong_channel=1ull<<5,
    altar_locked=1ull<<6,bad_altar=1ull<<7,unsafe_wait=1ull<<8,
    unmodelled_enemy=1ull<<9,conditional_completion=1ull<<10,egress_required=1ull<<11,
    unsupported_purchase=1ull<<12,bad_duration=1ull<<13
};
struct Result {
    selected::Status status{selected::Status::incomplete};
    bool admissible{},eligible{},requires_egress{},completion_is_conditional{};
    int duration{},expected_completion_epoch{-1},starting_bag{};
    std::uint64_t flags{};selected::OriginWait origin_wait;
};
inline std::optional<int> completion_epoch(int epoch,int duration) {
    const auto result=std::int64_t(epoch)+duration;
    if(epoch<0||duration<1||result>std::numeric_limits<int>::max())return std::nullopt;
    return static_cast<int>(result);
}
inline std::optional<int> tribute_duration(const Facts& facts) {
    if(facts.gems<1||facts.submit_min<1||facts.submit_base<0||facts.submit_range<0||
       !std::isfinite(facts.submit_multiplier)||facts.submit_multiplier<0)return std::nullopt;
    const double ticks=std::max(double(facts.submit_min),double(facts.submit_base)+std::ceil(double(facts.gems)*facts.submit_multiplier));
    if(!std::isfinite(ticks)||ticks<1||ticks>double(std::numeric_limits<int>::max()))return std::nullopt;
    return static_cast<int>(ticks);
}
// A clear after phase k arms at k+1 and pays at k+C+1. A recall begun at s
// arrives at s+L, after that round's capture phase. This is a timing helper only:
// continuous uncontested occupation, actual receipt and uninterrupted recall
// remain the macro's obligations. It is not permission to leave before payout.
inline std::optional<int> recall_start_for_capture(int clear_epoch,int capture_ticks,int recall_ticks) {
    if(clear_epoch<0||capture_ticks<1||recall_ticks<1)return std::nullopt;
    const auto result=std::max(std::int64_t(clear_epoch),std::int64_t(clear_epoch)+capture_ticks+1-recall_ticks);
    if(result>std::numeric_limits<int>::max())return std::nullopt;
    return static_cast<int>(result);
}
inline Channel observed_channel(const frame::Self& self) {
    return self.teleporting?Channel::teleport:self.recalling?Channel::recall:self.submitting?Channel::tribute:Channel::none;
}
inline Result evaluate(const selected::Packet& packet,const Facts& facts,const Request& request) {
    Result result;
    auto fail=[&](selected::Status status,Flag flag){result.status=status;result.flags|=flag;return result;};
    if(request.kind==Kind::buy)return fail(selected::Status::unsupported,unsupported_purchase);
    if(packet.request.command.kind!=transition::Kind::wait||!packet.matches(packet.stamp,packet.request)||
       !request.stamp.matches(packet.stamp)||!facts.stamp.matches(packet.stamp)||!same_request(facts.request,request)||
       facts.packet_nonce!=packet.nonce||facts.origin!=packet.self.pos||!selected::same_token(facts.layout,packet.layout))
        return fail(selected::Status::stale,stale_request);
    if(!facts.complete||!packet.self_complete||!facts.history_complete||!facts.channel_complete)
        return fail(selected::Status::incomplete,missing_facts);
    if(facts.history.epoch!=packet.stamp.epoch||facts.history.actor!=packet.stamp.actor)
        return fail(selected::Status::stale,stale_request);
    const auto& self=packet.self;
    if(!self.alive||self.stunned||!std::isfinite(self.hp)||self.hp<=0)return fail(selected::Status::rejected,unavailable);
    const int active=int(self.recalling)+int(self.submitting)+int(self.teleporting);
    if(active>1||facts.channel!=observed_channel(self)||(facts.channel!=Channel::none&&facts.remaining<0))
        return fail(selected::Status::incomplete,wrong_channel);
    result.origin_wait=selected::assess_origin_wait(packet,packet.request);
    bool visible_enemy=false;
    for(const auto& foe:packet.enemies)if(foe.visible&&foe.alive)visible_enemy=true;
    const bool quiet=result.origin_wait.complete&&result.origin_wait.safe&&result.origin_wait.next_volley==0&&!visible_enemy;
    const bool cancelling=request.kind==Kind::recall_cancel||request.kind==Kind::tribute_cancel;
    if(cancelling) {
        const auto expected=request.kind==Kind::recall_cancel?Channel::recall:Channel::tribute;
        if(facts.channel!=expected)return fail(selected::Status::rejected,wrong_channel);
        // V1 admits no second cancellation of either kind in one owner input.
        // This is conservative for the generic SDK E116; it does not assert a
        // native cross-kind exception from two separately named state flags.
        if(facts.history.recall_cancelled||facts.history.tribute_cancelled)
            return fail(selected::Status::rejected,repeated_cancel);
        result.admissible=result.eligible=true;result.status=selected::Status::ready;
        result.duration=facts.remaining;result.requires_egress=!quiet;
        if(!quiet)result.flags|=egress_required;
        if(visible_enemy)result.flags|=unmodelled_enemy;
        // A cancellation certificate authorizes only cancellation. It supplies
        // no safe-yield claim when requires_egress is true; parent must close the
        // cancellation -> fresh observation -> actual escape prefix.
        return result;
    }
    if(self.stunned||facts.channel!=Channel::none)return fail(selected::Status::rejected,unavailable);
    if(!facts.settings_complete)return fail(selected::Status::incomplete,missing_facts);
    if(request.kind==Kind::recall_start) {
        if(facts.history.recall_started)return fail(selected::Status::rejected,repeated_start);
        if(facts.recall_duration<1)return fail(selected::Status::unsupported,bad_duration);
        result.duration=facts.recall_duration;
    } else if(request.kind==Kind::tribute_start) {
        if(facts.history.tribute_started)return fail(selected::Status::rejected,repeated_start);
        if(!facts.bag_complete||facts.gems!=self.gems)return fail(selected::Status::incomplete,missing_facts);
        if(facts.gems<1)return fail(selected::Status::rejected,unavailable);
        if(!facts.altar_complete||!facts.lock_complete||!facts.altar_visible)return fail(selected::Status::incomplete,missing_facts);
        if(!facts.altar_valid||facts.altar!=request.altar||facts.submit_range<0||
            domain::distance(self.pos,request.altar)>facts.submit_range)return fail(selected::Status::rejected,bad_altar);
        if(facts.enemy_lock)return fail(selected::Status::rejected,altar_locked);
        const auto duration=tribute_duration(facts);
        if(!duration)return fail(selected::Status::unsupported,bad_duration);
        result.duration=*duration;result.starting_bag=facts.gems;
    } else return fail(selected::Status::unsupported,unavailable);
    const auto complete_at=completion_epoch(packet.stamp.epoch,result.duration);
    if(!complete_at)return fail(selected::Status::unsupported,bad_duration);
    result.expected_completion_epoch=*complete_at;result.admissible=true;
    if(!quiet) {
        if(visible_enemy)result.flags|=unmodelled_enemy;
        return fail(selected::Status::rejected,unsafe_wait);
    }
    result.status=selected::Status::ready;result.eligible=true;result.completion_is_conditional=true;
    result.flags|=conditional_completion;
    // The next phase's camp model is quiet. Later camp generations/modifiers,
    // enemy damage/stuns/locks/death, contest, recall placement and actual bag
    // at completion remain unproved. Reobserve; do not book a predicted receipt.
    return result;
}
struct Assessment;
class Certificate {
    Stamp stamp_;Request request_;selected::Request basis_;spatial::Token token_;
    cambc::vec2 origin_{};std::uint64_t nonce_{};bool consumed_{};
    Certificate(const selected::Packet& packet,const Request& request):stamp_(packet.stamp),request_(request),basis_(packet.request),
        token_(packet.layout),origin_(packet.self.pos),nonce_(packet.nonce){}
    friend Assessment assess(const selected::Packet&,const Facts&,const Request&);
public:
    Certificate(const Certificate&)=delete;Certificate& operator=(const Certificate&)=delete;
    Certificate(Certificate&& other) noexcept:stamp_(other.stamp_),request_(other.request_),basis_(other.basis_),
        token_(other.token_),origin_(other.origin_),nonce_(other.nonce_),consumed_(other.consumed_){other.consumed_=true;}
    Certificate& operator=(Certificate&&)=delete;
    std::optional<Request> consume(Stamp stamp,const Request& request,const selected::Request& basis,
                                  spatial::Token token,cambc::vec2 origin,std::uint64_t nonce) {
        if(consumed_)return std::nullopt;
        consumed_=true;
        if(!stamp_.matches(stamp)||!same_request(request_,request)||!selected::same_request(basis_,basis)||
           !selected::same_token(token_,token)||!token.valid(token.count)||origin_!=origin||nonce_!=nonce)return std::nullopt;
        return request_;
    }
};
struct Assessment {Result result;std::optional<Certificate> certificate;};
inline Assessment assess(const selected::Packet& packet,const Facts& facts,const Request& request) {
    auto result=evaluate(packet,facts,request);
    if(result.eligible)return {std::move(result),std::optional<Certificate>{Certificate{packet,request}}};
    return {std::move(result),std::nullopt};
}
// Call only after the corresponding void SDK primitive returned successfully;
// an exception/refusal is not evidence that this history flag was spent.
inline std::optional<History> advanced(History history,const Request& request) {
    if(history.epoch!=request.stamp.epoch||history.actor!=request.stamp.actor)return std::nullopt;
    switch(request.kind) {
    case Kind::recall_start:history.recall_started=true;break;
    case Kind::recall_cancel:history.recall_cancelled=true;break;
    case Kind::tribute_start:history.tribute_started=true;break;
    case Kind::tribute_cancel:history.tribute_cancelled=true;break;
    case Kind::buy:return std::nullopt;
    }
    return history;
}
} // namespace mosaic::conversion
