#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

// Finite complete POLICY comparison, not an SDK admission or a channel monitor.
// The producer owns the legal, coherent reply forecasts and their complete
// scoring/service/recovery tails. No maximum-damage envelope becomes a forecast.
// Times use one caller-defined, totally ordered round/order domain. In particular
// owner-start resolution and score receipt must have different orders from start.
namespace wb::capital::conversion_value {
inline constexpr std::size_t scenario_limit=16,score_event_limit=32;
struct Time {int round{-1};std::uint32_t order{};};
inline constexpr bool operator==(Time a,Time b){return a.round==b.round&&a.order==b.order;}
inline constexpr bool operator<(Time a,Time b){return a.round<b.round||(a.round==b.round&&a.order<b.order);}
template<class T> struct Known {T value{};bool known{};};

struct Domain {
    std::uint64_t binding{},rules{},world{},replies{},services{},recovery{},assumptions{};
    bool known{};
};
struct Context {
    int owner{-1};Time start{},horizon{};
    std::uint64_t input{},observation{},effect{};Domain domain{};
    // Scores immediately BEFORE events at start. Carried capital is separate.
    std::uint64_t initial_own_score{},initial_foe_score{},initial_carried{};
    bool initial_known{};
    Known<std::uint64_t> race_target{};
};
struct ReplyKey {std::uint64_t id{},revision{};};
inline constexpr bool operator==(ReplyKey a,ReplyKey b){return a.id==b.id&&a.revision==b.revision;}
enum class Evidence {Unknown,ModelForecast,Actual,SupportedConservation,EnvelopeOnly};
enum class Side {Own,Foe};
enum class Custody {Unknown,Held,Credited,TransferredToOwn,TransferredToFoe,Lost};
enum class Channel {Unknown,None,Started,Interrupted,Completed};
struct ActualPrefix {
    bool supplied{};Evidence evidence{Evidence::Unknown};Time at{};
    unsigned attempted{},applied{},refused{};
    Channel channel{Channel::Unknown};Custody custody{Custody::Unknown};
    Known<bool> alive{};Known<std::uint64_t> carried{};
};
// A scored payout only. A start, kill, transfer or retained bag cannot be put
// here merely because it is useful. Evidence/attribution comes from the caller.
struct ScoreEvent {Time when{};Side side{Side::Own};std::uint64_t amount{};};
struct DisplacedService {
    bool own_known{},foe_known{},included_in_scores{};
    // Audit quantities in SCORED GEMS at this same H, already included in the
    // complete policy timeline. They are never debited twice or converted to HP.
    std::uint64_t own_forgone{},foe_forgone{};
};
struct TailFacts {
    bool liquidation_known{},recovery_known{};
    DisplacedService displaced{};
    // Equal nonempty residual bags need the same known continuation. Unequal
    // residual bags remain a tradeoff; no exchange rate with score is invented.
    std::uint64_t liquidation_class{};
};
struct Terminal {
    Known<std::uint64_t> own_carried{},foe_carried{};
    Known<bool> owner_alive{};
    Known<Time> own_service_ready{},foe_service_ready{};
};
struct Outcome {
    ReplyKey reply{};Evidence evidence{Evidence::Unknown};
    bool coherent{},legal_reply{},complete{};
    ActualPrefix prefix{};
    std::span<const ScoreEvent> scores{};
    Terminal terminal{};TailFacts tail{};
};
struct Metadata {
    // Deliberately excluded from utility and the approximate callback view.
    std::uint64_t assurance{},envelope{};
};
struct Policy {
    std::uint64_t id{};Context context{};std::span<const Outcome> outcomes{};
    bool complete{},complete_economic_incumbent{};Metadata metadata{};
};
enum class Preference {Candidate,Incumbent,Tie,Incomparable};
enum class Reason {
    Pareto,Race,Equivalent,ModelCallback,MissingCompleteIncumbent,MissingPolicy,
    InvalidContext,ContextMismatch,InvalidOutcome,MissingReply,DuplicateReply,
    EvidenceMismatch,EnvelopeExcluded,UnknownService,UnknownTail,ResidualCapital,
    EconomicTradeoff,UnresolvedRace,Overflow
};
struct Comparison {
    Preference preference{Preference::Incomparable};Reason reason{Reason::MissingPolicy};
    bool approximate_choice{};
};
// This boundary is available only AFTER context, reply pairing, forecast kind,
// complete tails and explicit service costs are checked. The callback states
// its own approximate policy; this module supplies no probabilities or weights.
struct PairedOutcome {ReplyKey reply{};const Outcome* candidate{};const Outcome* incumbent{};};
struct ModelPair {const Context& context;std::span<const PairedOutcome> outcomes;};
using ApproximatePolicy=Preference (*)(const ModelPair&,void*);
struct Options {ApproximatePolicy approximate{};void* user{};};

namespace detail {
inline constexpr bool valid_time(Time t){return t.round>=0&&t.round<=1000000000;}
inline constexpr bool within(Time t,const Context& c){return valid_time(t)&&!(t<c.start)&&!(c.horizon<t);}
inline bool same(const Domain& a,const Domain& b){
    return a.known==b.known&&a.binding==b.binding&&a.rules==b.rules&&a.world==b.world&&
        a.replies==b.replies&&a.services==b.services&&a.recovery==b.recovery&&a.assumptions==b.assumptions;
}
inline bool valid(const Context& c){
    return c.owner>=0&&valid_time(c.start)&&valid_time(c.horizon)&&!(c.horizon<c.start)&&
        c.input&&c.observation&&c.domain.known&&c.domain.binding&&c.initial_known&&
        (!c.race_target.known||c.race_target.value>0);
}
inline bool same(const Context& a,const Context& b){
    return a.owner==b.owner&&a.start==b.start&&a.horizon==b.horizon&&a.input==b.input&&
        a.observation==b.observation&&a.effect==b.effect&&same(a.domain,b.domain)&&
        a.initial_own_score==b.initial_own_score&&a.initial_foe_score==b.initial_foe_score&&
        a.initial_carried==b.initial_carried&&a.initial_known==b.initial_known&&
        a.race_target.known==b.race_target.known&&(!a.race_target.known||a.race_target.value==b.race_target.value);
}
inline bool add(std::uint64_t& a,std::uint64_t b){
    if(b>std::numeric_limits<std::uint64_t>::max()-a)return false;a+=b;return true;
}
inline bool established(Evidence e){return e==Evidence::Actual||e==Evidence::SupportedConservation;}
inline bool forecast(Evidence e){return e==Evidence::ModelForecast;}
inline bool prefix_valid(const ActualPrefix& p,const Context& c){
    if(!p.supplied)return p.attempted==0&&p.applied==0&&p.refused==0;
    return established(p.evidence)&&within(p.at,c)&&p.applied<=p.attempted&&
        p.refused<=p.attempted-p.applied&&p.applied+p.refused==p.attempted&&
        static_cast<unsigned>(p.channel)<=static_cast<unsigned>(Channel::Completed)&&
        static_cast<unsigned>(p.custody)<=static_cast<unsigned>(Custody::Lost)&&
        p.channel!=Channel::Unknown&&p.custody!=Custody::Unknown&&p.alive.known&&p.carried.known;
}
inline Reason validate(const Outcome& o,const Context& c){
    if(o.evidence==Evidence::EnvelopeOnly)return Reason::EnvelopeExcluded;
    if(!o.reply.id||!o.coherent||!o.legal_reply||!o.complete||
        (!forecast(o.evidence)&&!established(o.evidence))||!prefix_valid(o.prefix,c))return Reason::InvalidOutcome;
    if(!o.tail.displaced.own_known||!o.tail.displaced.foe_known||!o.tail.displaced.included_in_scores)
        return Reason::UnknownService;
    if(!o.tail.liquidation_known||!o.tail.recovery_known||!o.terminal.own_carried.known||
        !o.terminal.foe_carried.known||!o.terminal.owner_alive.known||
        !o.terminal.own_service_ready.known||!o.terminal.foe_service_ready.known||
        !within(o.terminal.own_service_ready.value,c)||!within(o.terminal.foe_service_ready.value,c))return Reason::UnknownTail;
    if((o.terminal.own_carried.value||o.terminal.foe_carried.value)&&!o.tail.liquidation_class)return Reason::UnknownTail;
    if(o.scores.size()>score_event_limit)return Reason::InvalidOutcome;
    auto ours=c.initial_own_score,foes=c.initial_foe_score;Time previous=c.start;
    for(const auto& e:o.scores){
        if(!within(e.when,c)||e.when<previous||static_cast<unsigned>(e.side)>static_cast<unsigned>(Side::Foe))
            return Reason::InvalidOutcome;
        if(!add(e.side==Side::Own?ours:foes,e.amount))return Reason::Overflow;
        previous=e.when;
    }
    return Reason::Equivalent;
}
struct Relation {bool better{},worse{};};
inline void larger(Relation& r,std::uint64_t a,std::uint64_t b){r.better|=a>b;r.worse|=a<b;}
inline void earlier(Relation& r,Time a,Time b){r.better|=a<b;r.worse|=b<a;}
inline void scores(Relation& r,const Context& context,const Outcome& a,const Outcome& b){
    std::size_t ai=0,bi=0;auto ao=context.initial_own_score,af=context.initial_foe_score;
    auto bo=ao,bf=af;
    while(ai<a.scores.size()||bi<b.scores.size()){
        const auto when=bi==b.scores.size()||(ai<a.scores.size()&&a.scores[ai].when<b.scores[bi].when)?
            a.scores[ai].when:b.scores[bi].when;
        while(ai<a.scores.size()&&a.scores[ai].when==when){const auto& e=a.scores[ai++];(e.side==Side::Own?ao:af)+=e.amount;}
        while(bi<b.scores.size()&&b.scores[bi].when==when){const auto& e=b.scores[bi++];(e.side==Side::Own?bo:bf)+=e.amount;}
        larger(r,ao,bo);larger(r,bf,af);
    }
}
enum class Winner {Unresolved,Own,Foe,Simultaneous};
struct Race {Winner winner{Winner::Unresolved};Time at{};};
inline Race race(const Context& c,const Outcome& o){
    if(!c.race_target.known)return {};
    auto ours=c.initial_own_score,foes=c.initial_foe_score;std::size_t at=0;Time when=c.start;
    for(;;){
        const bool own=ours>=c.race_target.value,foe=foes>=c.race_target.value;
        if(own||foe)return {own?(foe?Winner::Simultaneous:Winner::Own):Winner::Foe,when};
        if(at==o.scores.size())return {};
        when=o.scores[at].when;
        while(at<o.scores.size()&&o.scores[at].when==when){const auto& e=o.scores[at++];(e.side==Side::Own?ours:foes)+=e.amount;}
    }
}
inline bool race_relation(Relation& r,Race a,Race b){
    if(a.winner==Winner::Unresolved&&b.winner==Winner::Unresolved)return false;
    if(a.winner==Winner::Simultaneous||b.winner==Winner::Simultaneous)return false;
    if(a.winner==b.winner){if(a.winner==Winner::Own)earlier(r,a.at,b.at);return true;}
    if(a.winner==Winner::Own||b.winner==Winner::Foe)r.better=true;
    else if(b.winner==Winner::Own||a.winner==Winner::Foe)r.worse=true;
    return true;
}
inline Preference preference(Relation r){
    return r.better?(r.worse?Preference::Incomparable:Preference::Candidate):
        (r.worse?Preference::Incumbent:Preference::Tie);
}
}

// Scored PAYOUT deltas only. Neither prefix.channel==Started nor terminal bag
// can contribute here. Unknown/overflow evidence remains explicitly unknown.
inline Known<std::uint64_t> payout_total(const Outcome& o,Side side){
    if((!detail::forecast(o.evidence)&&!detail::established(o.evidence))||
        static_cast<unsigned>(side)>static_cast<unsigned>(Side::Foe)||o.scores.size()>score_event_limit)return {};
    std::uint64_t sum=0;for(const auto& e:o.scores){
        if(static_cast<unsigned>(e.side)>static_cast<unsigned>(Side::Foe))return {};
        if(e.side==side&&!detail::add(sum,e.amount))return {};
    }
    return {sum,true};
}
inline Comparison compare(const Policy& candidate,const Policy& incumbent,Options options={}){
    if(!incumbent.complete||!incumbent.complete_economic_incumbent)return {Preference::Incomparable,Reason::MissingCompleteIncumbent};
    if(!candidate.complete||!candidate.id||!incumbent.id||candidate.outcomes.empty()||incumbent.outcomes.empty()||
        candidate.outcomes.size()>scenario_limit||incumbent.outcomes.size()>scenario_limit)return {Preference::Incomparable,Reason::MissingPolicy};
    if(!detail::valid(candidate.context)||!detail::valid(incumbent.context))return {Preference::Incomparable,Reason::InvalidContext};
    if(!detail::same(candidate.context,incumbent.context))return {Preference::Incomparable,Reason::ContextMismatch};
    if(candidate.outcomes.size()!=incumbent.outcomes.size())return {Preference::Incomparable,Reason::MissingReply};
    bool all_model=true,race_used=false,residual=false;int evidence_class=-1;detail::Relation relation;
    std::array<PairedOutcome,scenario_limit> paired{};
    for(std::size_t i=0;i<candidate.outcomes.size();++i){
        const auto& a=candidate.outcomes[i];
        for(std::size_t j=0;j<i;++j)if(a.reply==candidate.outcomes[j].reply)return {Preference::Incomparable,Reason::DuplicateReply};
        for(std::size_t j=0;j<i;++j)if(incumbent.outcomes[i].reply==incumbent.outcomes[j].reply)return {Preference::Incomparable,Reason::DuplicateReply};
        const Outcome* matched=nullptr;for(const auto& o:incumbent.outcomes)if(o.reply==a.reply){matched=&o;break;}
        if(!matched)return {Preference::Incomparable,Reason::MissingReply};const auto& b=*matched;
        auto why=detail::validate(a,candidate.context);if(why!=Reason::Equivalent)return {Preference::Incomparable,why};
        why=detail::validate(b,incumbent.context);if(why!=Reason::Equivalent)return {Preference::Incomparable,why};
        if(detail::forecast(a.evidence)!=detail::forecast(b.evidence))return {Preference::Incomparable,Reason::EvidenceMismatch};
        const int current_class=detail::forecast(a.evidence)?1:0;
        if(evidence_class>=0&&evidence_class!=current_class)return {Preference::Incomparable,Reason::EvidenceMismatch};
        evidence_class=current_class;paired[i]={a.reply,&a,&b};
        all_model=all_model&&detail::forecast(a.evidence)&&detail::forecast(b.evidence);
        detail::Relation branch;
        const auto candidate_race=detail::race(candidate.context,a),incumbent_race=detail::race(incumbent.context,b);
        // Equal timestamps do not supply the engine's winner/tie resolution.
        // Require a finer caller order instead of inventing that game result.
        if(candidate_race.winner==detail::Winner::Simultaneous||incumbent_race.winner==detail::Winner::Simultaneous)
            return {Preference::Incomparable,Reason::UnresolvedRace};
        if(detail::race_relation(branch,candidate_race,incumbent_race))race_used=true;
        else{
            const bool same_capital=a.terminal.own_carried.value==b.terminal.own_carried.value&&
                a.terminal.foe_carried.value==b.terminal.foe_carried.value&&
                (!(a.terminal.own_carried.value||a.terminal.foe_carried.value)||a.tail.liquidation_class==b.tail.liquidation_class);
            if(!same_capital)residual=true;
            detail::scores(branch,candidate.context,a,b);
            detail::earlier(branch,a.terminal.own_service_ready.value,b.terminal.own_service_ready.value);
            detail::earlier(branch,b.terminal.foe_service_ready.value,a.terminal.foe_service_ready.value);
            detail::larger(branch,std::uint64_t(a.terminal.owner_alive.value),std::uint64_t(b.terminal.owner_alive.value));
        }
        relation.better|=branch.better;relation.worse|=branch.worse;
    }
    auto preferred=residual?Preference::Incomparable:detail::preference(relation);
    if(preferred==Preference::Incomparable&&all_model&&options.approximate){
        const auto chosen=options.approximate({candidate.context,{paired.data(),candidate.outcomes.size()}},options.user);
        if(chosen==Preference::Candidate||chosen==Preference::Incumbent||chosen==Preference::Tie)
            return {chosen,Reason::ModelCallback,true};
    }
    return {preferred,residual?Reason::ResidualCapital:preferred==Preference::Tie?Reason::Equivalent:
        preferred==Preference::Incomparable?Reason::EconomicTradeoff:race_used?Reason::Race:Reason::Pareto};
}
// Tie, missing facts and incomparable tradeoffs all KEEP the complete economic
// incumbent. This does not replace it with an idle or HP-only escape policy.
inline const Policy& select(const Policy& candidate,const Policy& incumbent,Options options={}){
    return compare(candidate,incumbent,options).preference==Preference::Candidate?candidate:incumbent;
}
// Returned Policy and every borrowed outcome/event must outlive its consumer.
inline const Policy& select(const Policy&&,const Policy&,Options={})=delete;
inline const Policy& select(const Policy&,const Policy&&,Options={})=delete;
inline const Policy& select(const Policy&&,const Policy&&,Options={})=delete;
}
