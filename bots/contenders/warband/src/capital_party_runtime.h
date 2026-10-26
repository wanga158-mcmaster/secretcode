#pragma once
#include "capital_party_program_native.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

// First conditional, operational Varg/Willow/Vendetta class. The contract is
// canonical public input; nothing here selects a private contract under a wire
// header. Unknown prices/policy never become model authority. This is neither
// a player-target certificate nor a comparison with displaced solo service.
namespace wb::capital::party_runtime {
namespace n=party_program_native;namespace q=party_program;namespace p=party;namespace w=party_wire;
inline constexpr unsigned max_sources=32,max_route_nodes=512;
struct PolicyInput {
    q::Epoch source{};q::Revision grant{};q::Risk risk{q::Risk::Unknown};int horizon_round{-1};
    bool fresh{},priority_excluded{},station_refusals_closed{},other_champions_excluded{},foes_covered{};
    bool stun_covered{},locks_covered{},ongoing_effects_covered{},relocation_covered{};
    bool capital_obligation_clear{},economy_events_closed{};
    // Actual caller-observed investment/service restoration, never a grant
    // promising that the independent interpreter will perform it later.
    q::Revision renewal_observed{};
    std::array<int,p::actors> draft_ordinal{-1,-1,-1};
    // Loss through the relevant next phase, including all permitted player
    // choices in the admitted conditional domain. Never pool these numbers.
    std::array<float,p::actors> foe_damage_upper{};
};
inline bool same_input(const q::Epoch& a,const q::Epoch& b){return a.owner==b.owner&&a.round==b.round&&a.input==b.input
    &&a.geometry==b.geometry&&a.foes==b.foes&&a.locks==b.locks&&a.order==b.order&&a.life==b.life;}
inline bool policy_current(const PolicyInput& a,const q::Frame& f,const q::Contract& c){return a.fresh&&a.grant&&a.source==f.source
    &&a.risk!=q::Risk::Unknown&&a.risk==c.domain.risk&&a.horizon_round==f.source.round+1&&a.priority_excluded
    &&a.station_refusals_closed&&a.other_champions_excluded&&a.foes_covered&&a.stun_covered&&a.locks_covered
    &&a.ongoing_effects_covered&&a.relocation_covered;}
using ResourcePrice=bool(*)(void*,const q::Frame&,const q::Offer&,const n::Word&,unsigned,n::Price&);
using AcceptIndependent=bool(*)(void*,const q::Frame&,n::Independent&);
using VerifyIndependent=bool(*)(void*,const n::Independent&,const q::Frame&);
struct Callbacks {void* context{};ResourcePrice price{};AcceptIndependent accept{};VerifyIndependent verify{};};
enum class Life {Unknown,Alive,Reward,Respawning};
struct MonitorSample {
    q::Epoch source{};Life life{Life::Unknown};std::uint64_t reward_gems{},reward_gold{},passive_gold{};
    bool all_capture{},capture_history_closed{},economy_closed{},bank_history_closed{};
};
struct Custody {
    MonitorSample previous{};q::Frame before{};bool initialized{},source_known{},bank_known{},broken{};
    std::uint64_t source_gems{},source_gold{},credited{},foreign{};
    // One owner allocator (Host.event) is shared with primitive effects. A gap
    // makes historical attribution unknown; it does not prohibit banking the
    // body's actual current bag through a complete independent interpreter.
    template<class Host> unsigned observe(Host& host,const q::Contract& c,const q::Frame& f,const MonitorSample& s,std::span<q::Actual> receipts){
        unsigned count=0;if(s.source!=f.source)return 0;
        const bool consecutive=initialized&&same_input(previous.source,f.source)?previous.source.sample<=f.source.sample:
            initialized&&f.source.owner==previous.source.owner&&f.source.round==previous.source.round+1;
        if(initialized&&!consecutive){broken=true;bank_known=false;}
        auto add=[&](q::ReceiptKind kind,std::uint64_t gems,std::uint64_t gold){if(count==receipts.size())return false;
            auto& a=receipts[count++];a={};a.kind=kind;a.event=host.event();a.contract=c.id;a.life=c.life;a.actor=f.self.id;a.group=c.group;
            a.gems=gems;a.gold=gold;a.observed_actual=a.accepted=true;a.source_covered=kind==q::ReceiptKind::GroupPayout;
            a.bank_source_attribution_known=kind==q::ReceiptKind::BankPayout;return true;};
        if(initialized&&consecutive&&!broken&&!source_known&&previous.life==Life::Reward&&s.life==Life::Respawning
            &&previous.all_capture&&s.all_capture&&previous.capture_history_closed&&s.capture_history_closed
            &&previous.economy_closed&&s.economy_closed&&before.self.alive&&f.self.alive){
            unsigned rank=0;for(const auto& member:c.members)rank+=member.id<f.self.id;
            const auto gems=s.reward_gems/p::actors+(rank==0?s.reward_gems%p::actors:0);
            const auto gold=s.reward_gold/p::actors+(rank==0?s.reward_gold%p::actors:0);
            const auto elapsed=static_cast<std::uint64_t>(f.source.round-before.source.round);
            const bool passive_ok=!s.passive_gold||elapsed<=UINT64_MAX/s.passive_gold;
            const auto passive=passive_ok?elapsed*s.passive_gold:0;
            if(passive_ok&&UINT64_MAX-gold>=passive&&f.self.bag>=before.self.bag&&f.self.gold>=before.self.gold
                &&f.self.bag-before.self.bag==gems&&f.self.gold-before.self.gold==gold+passive){
                source_known=bank_known=true;source_gems=gems;source_gold=gold;foreign=before.self.bag;(void)add(q::ReceiptKind::GroupPayout,gems,gold);
            }else broken=true;
        }
        if(initialized&&source_known&&bank_known&&!broken&&credited<source_gems&&consecutive&&before.self.alive&&f.self.alive){
            // Last head normally sees 1: owner end changes 1->0, then the
            // following owner start resolves the channel before our head.
            const bool completion=before.self.channel==p::Channel::Submit&&before.self.channel_remaining<=1
                &&f.source.round==before.source.round+1&&f.self.channel==p::Channel::None&&f.self.bag==0
                &&before.self.bag==foreign+source_gems-credited&&f.self.team_score>=before.self.team_score
                &&f.self.team_score-before.self.team_score>=before.self.bag;
            if(completion&&previous.bank_history_closed&&s.bank_history_closed){const auto due=source_gems-credited;
                if(add(q::ReceiptKind::BankPayout,due,0)){credited+=due;foreign=0;}}
            else if(f.self.bag<before.self.bag||f.self.bag>before.self.bag+((count&&receipts[0].kind==q::ReceiptKind::GroupPayout)?source_gems:0))bank_known=false;
        }
        if(initialized&&(!f.self.alive||!s.bank_history_closed))bank_known=false;
        if(initialized&&!source_known&&s.life==Life::Respawning&&previous.life==Life::Reward)broken=true;
        previous=s;before=f;initialized=true;return count;
    }
};
struct Head {bool wire_owned{},pending{},claimed{};q::Phase lifecycle{q::Phase::Commit};n::Status status{n::Status::NoOffer};};
// An actual scoped query sample. known says only that the two fuel readings
// belong to one unchanged round; it does not admit a future resource price.
struct ObservationCost {bool known{},observed{};unsigned before{},after{};std::array<int,4> rounds{-1,-1,-1,-1};int requested_round{-1};q::Epoch source{};};
inline ObservationCost observation_cost(q::Epoch requested,q::Epoch captured,unsigned before,unsigned after,std::array<int,4> rounds,bool observed){
    ObservationCost out;out.observed=observed;out.before=before;out.after=after;out.rounds=rounds;out.requested_round=requested.round;
    out.known=requested.round>=0&&before>=after;for(const auto round:rounds)out.known&=round==requested.round;
    if(observed){out.known&=captured.owner==requested.owner&&captured.round==requested.round&&captured.input==requested.input&&captured.sample;
        if(out.known)out.source=captured;}
    return out;
}
inline bool supported(const q::Contract& c,int owner,n::Admission a){return a.valid()&&q::detail::valid(c)&&q::detail::member(c,owner)>=0
    &&!c.require_useful_support&&c.capture_mask==7&&c.camp_count<=n::max_receipts&&c.start>0&&c.abort_by<=w::max_round;}

// Host extends Native's genuine Session host with admit(Epoch,Contract),
// nominate(Contract,Offer,Frame,Word,Quote), monitor(Frame,MonitorSample), and
// independent_grant(Frame,Independent). The concrete SDK implementation below
// supplies these; fixtures inject only observations/commands, never a second VM.
template<class Host> class Runtime {
    Host& host_;q::Contract contract_;n::Admission admission_;int owner_;n::Session<Host> session_;
    Custody custody_{};Head head_{};bool activated_{},armed_{},retired_{},cleanup_published_{},abort_requested_{},withdrawn_head_{},allow_measure_now_{},stop_requested_{},allow_paid_exit_handoff_{};
    q::Epoch epoch_{};w::Phase last_published_{w::Phase::Unpublished};int complete_seen_round_{-1};
    w::Phase own_phase()const{const auto i=q::detail::member(contract_,owner_);if(i<0)return w::Phase::Unpublished;
        const auto phase=session_.program().phase();if(phase==q::Phase::Abort||phase==q::Phase::Handoff)return w::Phase::Abort;
        const auto m=session_.program().completion().members[static_cast<unsigned>(i)];
        if(!m.committed)return w::Phase::Unpublished;if(!m.arrived)return w::Phase::Committed;if(!m.entered)return w::Phase::Arrived;
        if(!(m.positive_work||m.useful_support))return w::Phase::Entered;if(!m.payout)return w::Phase::WorkPrefix;
        if(!m.released)return w::Phase::Payout;if(!m.banked)return w::Phase::Released;if(!m.recovered)return w::Phase::Banked;
        return m.renewed?w::Phase::Renewed:w::Phase::Recovered;}
    Head result(n::Status status){head_.status=status;head_.lifecycle=session_.program().phase();head_.pending=activated_&&session_.needs_reconciliation();
        head_.wire_owned=activated_&&!retired_&&(armed_||epoch_.round<contract_.start)&&session_.wire_owned();
        head_.claimed=head_.pending||session_.claimed()||head_.wire_owned;return head_;}
    void delayed(){MonitorSample sample;if(!host_.monitor(session_.actual(),sample))return;std::array<q::Actual,2> receipts{};
        const auto count=custody_.observe(host_,contract_,session_.actual(),sample,receipts);
        for(unsigned i=0;i<count;++i)if(!session_.receipt(receipts[i])){custody_.broken=true;abort_requested_=true;}
        if(custody_.broken||(custody_.source_known&&!custody_.bank_known))abort_requested_=true;}
public:
    Runtime(Host& host,const q::Contract& canonical,int owner,n::Admission admission,n::Options options={})
        :host_(host),contract_(canonical),admission_(admission),owner_(owner),session_(host,canonical,owner,admission,options),allow_measure_now_(options.allow_measure_now),allow_paid_exit_handoff_(options.allow_paid_exit_handoff){}
    const Custody& custody()const{return custody_;}const n::Session<Host>& session()const{return session_;}
    // Pure durable cancellation request. No observation, effect, release or
    // acknowledgement is created here. Fresh head reconciliation comes first.
    void request_abort(){stop_requested_=true;}
    bool stopping()const{return stop_requested_;}
    Head head(q::Epoch epoch){epoch_=epoch;if(retired_||!supported(contract_,owner_,admission_))return result(n::Status::NoOffer);
        if(stop_requested_&&!activated_)return result(n::Status::NoOffer);
        if(!activated_){if(!host_.admit(epoch,contract_))return result(n::Status::NoOffer);activated_=true;}
        const auto begun=session_.begin(epoch);if(begun==n::Status::NoOffer||begun==n::Status::Stale)return result(begun);
        if(session_.needs_reconciliation()){const auto reconciled=session_.reconcile_pending();if(session_.needs_reconciliation())return result(reconciled.status);}
        if(!armed_){const auto status=session_.arm();if(status==n::Status::Prestart||status==n::Status::Ready)armed_=true;
            else if(epoch.round>=contract_.start){retired_=true;return result(n::Status::NoOffer);}else return result(status);}
        if(epoch.round<contract_.start)return result(n::Status::Prestart);
        const auto observed=session_.observe();if(observed!=n::Status::Ready)return result(observed);withdrawn_head_=false;
        if(session_.own_abort_visible())last_published_=w::Phase::Abort;
        delayed();return result(n::Status::Ready);
    }
    n::Result step(){n::Result out;out.status=head_.status;out.claimed=head_.claimed;
        if(retired_||!activated_)return out;
        if(session_.needs_reconciliation()){out.status=n::Status::Pending;out.claimed=true;return out;}
        if(withdrawn_head_){out.status=n::Status::Aborting;out.claimed=true;return out;}
        if(!armed_){if(stop_requested_){out.status=n::Status::Aborting;return out;}
            if(owner_!=contract_.members[0].id||epoch_.round+2!=contract_.start)return out;
            out=session_.propose(true);if(out.accepted)armed_=true;(void)result(out.status);return out;}
        if(head_.status==n::Status::Prestart)return out;
        if(head_.status!=n::Status::Ready)return out;
        const auto phase=session_.program().phase();
        if(phase==q::Phase::Complete||phase==q::Phase::Handoff){
            if(phase==q::Phase::Complete&&complete_seen_round_<0)complete_seen_round_=epoch_.round;
            // The last local Renewed can make this VM complete before any
            // peer has seen its record. Publish it once; CleanupDone is only
            // a cleanup acknowledgement and cannot stand in for that fact.
            if(phase==q::Phase::Complete&&last_published_!=w::Phase::Renewed){out=session_.publish();
                if(out.accepted)last_published_=w::Phase::Renewed;(void)result(out.status);return out;}
            // Leave the actual Renewed predicates available through a whole
            // round of the admitted, bounded owner inputs before cleanup.
            // A suspended peer may still take the honest abort/handoff path;
            // elapsed time is not an acknowledgement of that peer's reading.
            if(phase==q::Phase::Complete&&epoch_.round<=complete_seen_round_){out.status=n::Status::Complete;out.claimed=true;(void)result(out.status);return out;}
            if(!cleanup_published_){out=session_.cleanup();cleanup_published_=out.status==n::Status::Complete;}
            else {out.status=n::Status::Complete;out.claimed=session_.wire_owned();}
            (void)result(out.status);return out;}
        if((abort_requested_||stop_requested_)&&phase!=q::Phase::Abort){out=session_.withdraw();abort_requested_=false;withdrawn_head_=true;
            if(out.accepted)last_published_=w::Phase::Abort;(void)result(out.status);return out;}
        if(phase==q::Phase::Abort)abort_requested_=false;
        if(phase==q::Phase::Abort&&allow_paid_exit_handoff_){
            // Expose the actual withdrawal before transferring the outstanding
            // physical service. Metadata remains exclusively owned until every
            // real participant acknowledges cleanup.
            if(last_published_!=w::Phase::Abort){out=session_.publish();if(out.accepted)last_published_=w::Phase::Abort;(void)result(out.status);return out;}
            n::Independent grant;if(host_.independent_grant(session_.actual(),grant)&&session_.handoff_paid_exit(grant)){
                out.status=n::Status::Handoff;out.claimed=true;out.physical_released=true;(void)result(out.status);return out;}}
        const auto nominee=session_.nominate();
        if(nominee.status==q::Status::Handoff){n::Independent grant;if(host_.independent_grant(session_.actual(),grant)&&session_.handoff(grant)){
                out.status=n::Status::Handoff;out.claimed=true;out.physical_released=true;}else {out.status=n::Status::NeedModel;abort_requested_=true;}
            (void)result(out.status);return out;}
        if(nominee.status!=q::Status::Ready){out.status=nominee.status==q::Status::NeedObservation?n::Status::NeedObservation:n::Status::NeedModel;
            if(epoch_.round>contract_.abort_by)abort_requested_=true;(void)result(out.status);return out;}
        if(((nominee.offer.kind==q::Kind::Recover||nominee.offer.kind==q::Kind::HoldRecovery)&&session_.actual().self.at_spawn
                &&session_.actual().self.hp==session_.actual().self.max_hp&&!session_.actual().capital_obligation_clear)
            ||(nominee.offer.kind==q::Kind::Renew&&!host_.tail_complete(session_.actual()))){
            abort_requested_=true;out.status=n::Status::NeedModel;(void)result(out.status);return out;}
        // Publish a newly established own milestone before starting the next
        // word; never replace CleanupDone with a lower phase.
        if(own_phase()!=last_published_&&nominee.offer.kind!=q::Kind::PublishCommit){
            const auto published=session_.publish();if(published.accepted)last_published_=own_phase();
            if(published.wire==w::Status::AbortSticky){withdrawn_head_=true;out=published;out.status=n::Status::Aborting;(void)result(out.status);return out;}
            if(published.attempted||session_.needs_reconciliation()){(void)result(published.status);return published;}}
        n::Word word;q::Quote quote;if(!host_.nominate(contract_,nominee.offer,session_.actual(),word,quote)){
            out.status=n::Status::NeedModel;abort_requested_=true;(void)result(out.status);return out;}
        if(!word.count&&!word.price.bounded&&!allow_measure_now_){out.status=n::Status::NeedModel;abort_requested_=true;(void)result(out.status);return out;}
        if(nominee.offer.kind==q::Kind::Renew){n::Independent grant;if(!host_.independent_grant(session_.actual(),grant)){
                out.status=n::Status::NeedModel;abort_requested_=true;}else out=session_.renew(nominee.offer,quote,grant);}
        else out=session_.execute(nominee.offer,quote,word);
        // Seed custody from the actual accepted/refused command readback too.
        // A one-tick Submit may resolve before the next input head; its actual
        // channel start must survive that interval. Acceptance earns no bank
        // receipt here: only the later observed completion can do that.
        if(out.after_known&&!session_.needs_reconciliation())delayed();
        if(nominee.offer.kind==q::Kind::PublishCommit&&out.accepted)last_published_=w::Phase::Committed;
        // Refusals stay actual refused attempts; the Program bounds repeated
        // failures and owns the typed abort. A model/pricing failure cannot
        // retain useful-work authority indefinitely.
        if(out.status==n::Status::NeedModel)abort_requested_=true;
        (void)result(out.status);return out;
    }
};

struct Source {int id{-1},group{-1};p::Point position{};int range{-1};float damage{};bool next_dead{};};
// Catalogue completeness and static walkability are explicit caller admission.
// blocked includes walls, holes, altars and hostile spawn, never live occupancy.
struct Geometry {int width{},height{};std::span<const std::uint8_t> blocked{};bool complete_catalogue{};};
struct Planning {
    PolicyInput policy{};std::array<Source,max_sources> sources{};unsigned source_count{};
    std::array<std::int32_t,p::actors> next_priority{};std::array<bool,p::actors> priority_known{};
    std::array<p::Cast,2> casts{};std::array<bool,2> cast_ready{};int attack_range{},move_speed{},submit_range{};
    std::span<const std::uint8_t> blocked{},visible{},occupied{};int width{},height{};
    std::span<const std::uint64_t> visible_raw{},occupied_raw{};unsigned chunk_stride{};
    bool catalogue_complete{},packet_current{},attack_type_known{},melee{};
};
inline bool inside(const Planning& x,p::Point a){return a.x>=0&&a.y>=0&&a.x<x.width&&a.y<x.height;}
inline unsigned tile(const Planning& x,p::Point a){return static_cast<unsigned>(a.y*x.width+a.x);}
inline bool raw_bit(std::span<const std::uint64_t> raw,unsigned stride,p::Point a){const auto chunk=static_cast<unsigned>(a.y/8)*stride+static_cast<unsigned>(a.x/8);
    return chunk<raw.size()&&((raw[chunk]>>(static_cast<unsigned>(a.y%8)*8+static_cast<unsigned>(a.x%8)))&1u);}
inline bool seen(const Planning& x,p::Point a){return !x.visible_raw.empty()?raw_bit(x.visible_raw,x.chunk_stride,a):tile(x,a)<x.visible.size()&&x.visible[tile(x,a)];}
inline bool occupied(const Planning& x,p::Point a){return !x.occupied_raw.empty()?raw_bit(x.occupied_raw,x.chunk_stride,a):tile(x,a)>=x.occupied.size()||x.occupied[tile(x,a)];}
inline bool quiet(const Planning& x,p::Point a){if(!x.catalogue_complete)return false;
    for(unsigned i=0;i<x.source_count;++i){if(x.sources[i].damage>0&&p::distance(x.sources[i].position,a)<=x.sources[i].range)return false;}
    return true;}
inline bool next_step(const Planning& x,p::Point from,p::Point destination,p::Point& next){
    if(from==destination)return false;if(!inside(x,from)||!inside(x,destination)||x.move_speed<=0||x.width*x.height>10000
        ||x.blocked.size()!=static_cast<unsigned>(x.width*x.height)||(x.visible_raw.empty()&&x.visible.size()!=x.blocked.size())
        ||(x.occupied_raw.empty()&&x.occupied.size()!=x.blocked.size()))return false;
    if(x.blocked[tile(x,destination)]||occupied(x,destination)||!seen(x,destination))return false;
    // Bounded reverse BFS uses only one already captured geometry packet. The
    // ultimate destination may be an altar; route to a submit-adjacent point is
    // selected by nominate(), rather than walking onto an altar.
    std::array<p::Point,max_route_nodes> queue{};std::array<int,10000> parent;parent.fill(-1);unsigned head=0,tail=1;
    queue[0]=destination;parent[tile(x,destination)]=static_cast<int>(tile(x,destination));
    while(head<tail&&tail<max_route_nodes){const auto at=queue[head++];for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx&&!dy)continue;
            const p::Point pnt{at.x+dx,at.y+dy};if(!inside(x,pnt))continue;const auto index=tile(x,pnt);
            if(parent[index]>=0||(pnt!=from&&(x.blocked[index]||occupied(x,pnt)||!seen(x,pnt))))continue;
            parent[index]=static_cast<int>(tile(x,at));if(pnt==from){next=at;return next!=from;}if(tail==max_route_nodes)return false;queue[tail++]=pnt;}}
    return false;
}
inline bool survival(const q::Contract& c,const q::Frame& f,const Planning& x,p::Point own_terminal,bool require_peers){
    if(!x.packet_current||!x.catalogue_complete||x.source_count>max_sources||!policy_current(x.policy,f,c))return false;
    const int own=q::detail::member(c,f.self.id);if(own<0)return false;std::array<p::Point,p::actors> poses{};std::array<float,p::actors> hp{};
    bool all=true;for(unsigned i=0;i<p::actors;++i){const auto* body=n::peer(f,c.members[i].id);all&=body&&body->alive&&x.priority_known[i];
        if(body){poses[i]=body->position;hp[i]=body->hp;}if(!p::detail::finite(x.policy.foe_damage_upper[i]))return false;}
    poses[static_cast<unsigned>(own)]=own_terminal;
    if(require_peers&&!all)return false;const bool partition=require_peers&&all;
    // When peers are not personally visible, no nearest-target ownership is
    // claimed: charge every in-range source to this body. No peer gold/private
    // readiness is a physical prohibition on quiet assembly or release.
    for(unsigned i=0;i<x.source_count;++i){const auto& source=x.sources[i];if(!p::detail::finite(source.damage)||source.range<0)return false;
        if(source.next_dead||source.damage==0)continue;int target=-1,nearest=std::numeric_limits<int>::max();
        for(unsigned j=0;j<p::actors;++j){if(!partition&&j!=static_cast<unsigned>(own))continue;const int distance=p::distance(source.position,poses[j]);if(distance>source.range)continue;
            bool earlier=target>=0&&(x.next_priority[j]>x.next_priority[static_cast<unsigned>(target)]
                ||(x.next_priority[j]==x.next_priority[static_cast<unsigned>(target)]&&x.policy.draft_ordinal[j]<x.policy.draft_ordinal[static_cast<unsigned>(target)]));
            if(distance<nearest||(distance==nearest&&earlier)){target=static_cast<int>(j);nearest=distance;}}
        if(target>=0){auto& capacity=hp[static_cast<unsigned>(target)];capacity=std::nextafter(capacity-source.damage,-std::numeric_limits<float>::infinity());if(capacity<=0)return false;}}
    for(unsigned i=0;i<p::actors;++i){if(!partition&&i!=static_cast<unsigned>(own))continue;
        if(partition){if(x.policy.draft_ordinal[i]<0)return false;for(unsigned j=0;j<i;++j)if(x.policy.draft_ordinal[i]==x.policy.draft_ordinal[j]
                ||((x.policy.draft_ordinal[i]<x.policy.draft_ordinal[j])!=(c.members[i].id<c.members[j].id))||poses[i]==poses[j])return false;}
        // Existing shields may expire at owner start; new shield/heal effects
        // are also deliberately uncredited until a later actual snapshot.
        if(hp[i]<=x.policy.foe_damage_upper[i])return false;}
    return true;
}
inline bool nominate(const q::Contract& c,const q::Offer& offer,const q::Frame& f,const Planning& x,const Callbacks& hooks,q::Revision token,n::Word& word,q::Quote& quote,bool allow_measure_now=false){
    if(offer.source!=f.source||!token||!policy_current(x.policy,f,c))return false;
    word={};word.source=offer.source;word.contract=c.id;word.token=token;const auto role=q::detail::member(c,f.self.id);if(role<0)return false;
    const auto* target=n::camp(f,offer.target);auto basic=[&](int id){word.steps[word.count++]={n::PrimitiveKind::Basic,{},id,-1};};
    auto slot=[&](p::Cast cast){for(unsigned i=0;i<2;++i)if(x.casts[i]==cast&&x.cast_ready[i])return static_cast<int>(i);return -1;};
    const bool work=offer.kind==q::Kind::WorkTank||offer.kind==q::Kind::Strike||offer.kind==q::Kind::Heal;
    if(work){if(!f.group.observed_now||!survival(c,f,x,f.self.position,true))return false;
        if(offer.kind==q::Kind::Heal){const int heal=slot(p::Cast::Heal);const auto* recipient=n::peer(f,offer.target);
            if(heal<0||!recipient||recipient->hp>=recipient->max_hp||!f.self.cast||!f.self.attack||p::distance(f.self.position,recipient->position)>f.self.heal_range)return false;
            int camp=-1;for(unsigned i=0;i<f.group.count;++i){const auto& a=f.group.camps[i];if(a.observed_now&&a.alive&&p::distance(f.self.position,a.position)<=x.attack_range&&(camp<0||a.id<camp))camp=a.id;}
            if(camp<0)return false;word.selected_cast=p::Cast::Heal;word.steps[word.count++]={n::PrimitiveKind::Heal,{},offer.target,heal};word.camp_target=camp;basic(camp);
        }else{if(!target||!target->observed_now||!target->alive||!f.self.attack||x.attack_range<0||x.attack_range>8)return false;
            if(p::distance(f.self.position,target->position)>x.attack_range){if(!f.self.move)return false;bool found=false;p::Point destination{};int best=std::numeric_limits<int>::max();
                for(int dy=-x.attack_range;dy<=x.attack_range;++dy)for(int dx=-x.attack_range;dx<=x.attack_range;++dx){const p::Point at{target->position.x+dx,target->position.y+dy};
                    if(!inside(x,at)||x.blocked[tile(x,at)]||occupied(x,at)||!seen(x,at))continue;const int distance=p::distance(f.self.position,at);
                    if(distance<best){destination=at;best=distance;found=true;}}
                p::Point next;if(!found||!next_step(x,f.self.position,destination,next))return false;word.steps[word.count++]={n::PrimitiveKind::Move,next,-1,-1};
                // The queried design, not role or attack range, decides whether
                // walking leaves the basic allowance. Native rechecks that
                // allowance again on the actual post-move packet.
                if(x.attack_type_known&&x.melee&&p::distance(next,target->position)<=x.attack_range)basic(offer.target);
            }else {if(offer.kind==q::Kind::WorkTank&&f.self.cast&&f.self.shield_ready&&slot(p::Cast::Shield)>=0){word.selected_cast=p::Cast::Shield;
                word.steps[word.count++]={n::PrimitiveKind::Shield,{},-1,slot(p::Cast::Shield)};}basic(offer.target);}
        }
    }else if(offer.kind!=q::Kind::PublishCommit&&offer.kind!=q::Kind::Renew&&offer.kind!=q::Kind::HoldChannel&&offer.kind!=q::Kind::HoldRecovery){
        if(offer.kind==q::Kind::Bank&&f.self.altar_adjacent&&f.self.bag&&f.altar_unlocked_known&&f.altar_unlocked)
            word.steps[word.count++]={n::PrimitiveKind::Submit,offer.destination,-1,-1};
        else {p::Point destination=offer.destination;
            if(offer.kind==q::Kind::Bank){if(x.submit_range<0||x.submit_range>8)return false;
                bool found=false;int best=std::numeric_limits<int>::max();for(int dy=-x.submit_range;dy<=x.submit_range;++dy)for(int dx=-x.submit_range;dx<=x.submit_range;++dx){
                    const p::Point a{destination.x+dx,destination.y+dy};if(!inside(x,a)||x.blocked[tile(x,a)]||occupied(x,a)||!seen(x,a))continue;
                    const int distance=p::distance(f.self.position,a);if(distance<best){best=distance;word.steps[0].destination=a;found=true;}}
                if(!found)return false;destination=word.steps[0].destination;}
            if(f.self.position!=destination){p::Point next;if(!f.self.move||!next_step(x,f.self.position,destination,next))return false;
                word.steps[word.count++]={n::PrimitiveKind::Move,next,-1,-1};}}
    }
    p::Point terminal=f.self.position;for(unsigned i=0;i<word.count;++i)if(word.steps[i].kind==n::PrimitiveKind::Move)terminal=word.steps[i].destination;
    // Check refusal (the actual old pose) AND successful move. A beneficial
    // future shield/heal/kill never funds either branch.
    if(!survival(c,f,x,f.self.position,work)||!survival(c,f,x,terminal,work))return false;
    const auto& member=c.members[static_cast<unsigned>(role)];bool destination_closed=false;
    if(f.fresh&&f.self.position==offer.destination){switch(offer.kind){
        case q::Kind::Capture:{bool clear=f.group_life_known&&f.group.id==c.group&&f.group.count==c.camp_count&&f.group.observed_now&&f.group.whole_clear;
            for(unsigned i=0;i<f.group.count&&i<p::max_camps;++i){bool canonical=false;for(unsigned j=0;j<c.camp_count;++j)canonical|=f.group.camps[i].id==c.camps[j];
                clear&=canonical&&f.group.camps[i].observed_now&&!f.group.camps[i].alive&&f.group.camps[i].hp==0;
                for(unsigned j=0;j<i;++j)clear&=f.group.camps[j].id!=f.group.camps[i].id;}
            destination_closed=f.self.at_capture&&offer.destination==member.capture&&clear;break;}
        case q::Kind::Assemble:destination_closed=offer.destination==member.rendezvous;break;
        case q::Kind::Enter:destination_closed=f.self.at_station&&offer.destination==member.station;break;
        case q::Kind::Release:case q::Kind::AbortExit:destination_closed=f.source_released&&f.self.npc_quiet&&offer.destination==member.release;break;
        case q::Kind::Recover:destination_closed=f.self.at_spawn&&offer.destination==member.spawn;break;
        default:break;}}
    // These are actual closed inputs, not future physical words. In particular
    // Capture merely waits on the observed cleared source; only a later actual
    // payout can credit it. Recovery likewise awaits actual owner-end healing.
    const bool empty_control=allow_measure_now&&!word.count&&f.fresh&&(destination_closed||offer.kind==q::Kind::PublishCommit
        ||(offer.kind==q::Kind::HoldChannel&&f.self.channel==p::Channel::Submit)
        ||(offer.kind==q::Kind::HoldRecovery&&f.self.channel==p::Channel::Recall)||offer.kind==q::Kind::Renew);
    if(empty_control){quote={offer.source,c.id,offer.serial,word.token,c.domain,true,true,true,true,true,true,true,0,word.camp_target};return n::bound(offer,quote,word);}
    n::Price price;if(!hooks.price||!hooks.price(hooks.context,f,offer,word,0,price)||!price.model||price.domain!=c.domain.id)return false;
    if(price.measure_now){
        if(!allow_measure_now||price.bounded||!word.count)return false;const auto model=price.model,domain=price.domain;
        // Calibration samples exactly one genuine command. No removed basic,
        // secondary target or future cast may survive in the priced word.
        word.count=1;word.camp_target=-1;switch(word.steps[0].kind){
        case n::PrimitiveKind::Shield:word.selected_cast=p::Cast::Shield;break;
        case n::PrimitiveKind::Reset:word.selected_cast=p::Cast::Reset;break;
        case n::PrimitiveKind::Heal:word.selected_cast=p::Cast::Heal;break;
        case n::PrimitiveKind::Dash:word.selected_cast=p::Cast::Dash;break;
        default:word.selected_cast=p::Cast::None;break;}
        for(unsigned i=1;i<n::max_steps;++i)word.steps[i]={};
        price={};if(!hooks.price(hooks.context,f,offer,word,0,price)||!price.measure_now||price.bounded||price.model!=model||price.domain!=domain)return false;
    }else if(!price.bounded)return false;
    // In measure mode no Price.bounded assertion is manufactured. Native's
    // separate opt-in keeps its physical fuel floors and records the attempt;
    // this conditional class makes no universal computation/safety claim.
    word.price=price;
    quote={offer.source,c.id,offer.serial,word.token,c.domain,true,true,true,true,true,true,true,0,word.camp_target};return n::bound(offer,quote,word);
}

#ifndef WB_PARTY_PROGRAM_NATIVE_HOST_FREE
namespace legacy=party_native;
using PolicyProducer=bool(*)(void*,const q::Contract&,const legacy::Observation&,PolicyInput&);
struct Hooks {Callbacks callbacks{};PolicyProducer policy{};bool measure_now{};};
// Genuine SDK snapshots/legality and finalFence -> ONE raw command remain in
// Native::SdkHost. No new backend, SDK substitute or scheduling writer exists.
class Sdk {
    std::vector<cambc::camp> catalogue_;std::vector<std::uint8_t> terrain_;std::vector<std::uint64_t> visible_raw_,occupied_raw_;int width_{},height_{};Hooks hooks_{};bool catalogue_admitted_{};
    n::SdkHost native_;Planning planning_{};MonitorSample monitor_{};n::Independent independent_{};q::Contract contract_{};q::Revision words_{};ObservationCost last_observe_cost_{};
    int attack_owner_{-1};bool attack_type_known_{},melee_{};
    static bool produce_cb(void* context,const q::Contract& c,const legacy::Observation& raw,q::Frame& f){return static_cast<Sdk*>(context)->produce(c,raw,f);}
    static bool remaining_cb(void* context,const q::Offer& o,const n::Word& word,unsigned step,const q::Frame& f,n::Guard& guard){return static_cast<Sdk*>(context)->remaining(o,word,step,f,guard);}
    static bool independent_cb(void* context,const n::Independent& grant,const q::Frame& f){return static_cast<Sdk*>(context)->independent(grant,f);}
    bool current(const q::Frame& f){return native_.owner()==f.source.owner&&native_.round()==f.source.round;}
    bool produce(const q::Contract& c,const legacy::Observation& raw,q::Frame& f){
        contract_=c;PolicyInput input;if(!hooks_.policy||!hooks_.policy(hooks_.callbacks.context,c,raw,input))input={};
        if(input.fresh&&(input.source.owner!=f.source.owner||input.source.round!=f.source.round||input.source.input!=f.source.input||input.source.life!=c.life))return false;
        f.source.geometry=input.source.geometry;f.source.foes=input.source.foes;f.source.locks=input.source.locks;f.source.order=input.source.order;f.source.life=c.life;
        input.source=f.source;planning_={};planning_.policy=input;planning_.width=width_;planning_.height=height_;planning_.blocked=terrain_;
        planning_.move_speed=raw.move_speed;planning_.attack_range=raw.attack_range;planning_.submit_range=cambc::constants::submit_gems_range;
        if(!attack_type_known_||attack_owner_!=f.self.id){cambc::detail::champion_identity identity{};
            cambc::detail::check(cambc::detail::sdk_champion_identity(static_cast<unsigned>(f.self.id),&identity));if(!current(f))return false;
            cambc::detail::champion_design design{};cambc::detail::check(cambc::detail::sdk_champion_design(identity.type,&design));if(!current(f))return false;
            if(design.definition.attack_type>1)return false;attack_owner_=f.self.id;attack_type_known_=true;melee_=design.definition.attack_type==0;}
        planning_.attack_type_known=attack_type_known_;planning_.melee=melee_;
        planning_.catalogue_complete=catalogue_admitted_&&!catalogue_.empty()&&catalogue_.size()<=max_sources;
        monitor_={};monitor_.source=f.source;monitor_.passive_gold=cambc::constants::passive_gold;std::int32_t respawn_timer=-1;
        if(f.group.observed_now){std::uint32_t state=0;cambc::detail::check(cambc::detail::sdk_camp_group_state(static_cast<unsigned>(c.group),&state));if(!current(f))return false;
            monitor_.life=state==1?Life::Alive:state==2?Life::Reward:state==3?Life::Respawning:Life::Unknown;
            f.group_life_known=monitor_.life!=Life::Unknown;f.group.claimed=monitor_.life==Life::Respawning;
            if(monitor_.life==Life::Respawning){cambc::detail::check(cambc::detail::sdk_camp_group_cooldown(static_cast<unsigned>(c.group),&respawn_timer));if(!current(f))return false;}
            for(const auto& candidate:catalogue_)if(static_cast<int>(static_cast<cambc::handle_camp_t>(candidate))==c.camps[0]){
                const auto actual=candidate.camp_group();if(static_cast<int>(static_cast<cambc::handle_camp_group_t>(actual))!=c.group)return false;
                monitor_.reward_gems=actual.gem_value();monitor_.reward_gold=actual.gold_value();break;}}
        for(unsigned i=0;i<2;++i){planning_.casts[i]=raw.abilities[i].kind;planning_.cast_ready[i]=raw.abilities[i].cooldown==0&&!raw.abilities[i].charged;}
        for(unsigned i=0;i<catalogue_.size()&&i<max_sources;++i){const auto& camp=catalogue_[i];const auto stats=camp.base_stats();const auto pos=camp.spawn_position();
            auto& source=planning_.sources[planning_.source_count++];source={static_cast<int>(static_cast<cambc::handle_camp_t>(camp)),-1,{pos.x,pos.y},stats.base_attack_range,
                stats.base_strength==0&&stats.strength_growth==0?0.f:std::numeric_limits<float>::max(),false};
            // Never treat base strength as the live round/generation strength.
            // Hidden harmful sources retain an effectively unpayable bound;
            // only a fresh visible runtime stat can admit an in-range body.
            if(source.damage>0&&camp.is_visible()){source.damage=camp.strength();if(!current(f))return false;}
            for(unsigned j=0;j<f.group.count;++j)if(f.group.camps[j].id==source.id&&f.group.camps[j].observed_now&&!f.group.camps[j].alive)
                source.next_dead=monitor_.life==Life::Alive||monitor_.life==Life::Reward||(monitor_.life==Life::Respawning&&respawn_timer>0);
            if(!current(f))return false;}
        for(unsigned i=0;i<p::actors;++i){const auto* body=n::peer(f,c.members[i].id);if(body&&body->alive){std::int32_t priority=0;
                cambc::detail::check(cambc::detail::sdk_bc_champion_priority(static_cast<unsigned>(body->id),true,&priority));if(!current(f))return false;
                planning_.next_priority[i]=priority;planning_.priority_known[i]=true;}}
        const auto visible=cambc::vision::visible_tiles();if(!current(f))return false;
        const auto occupied=cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();if(!current(f))return false;
        const auto& layout=cambc::detail::layout();if(width_!=layout.width||height_!=layout.height||layout.board_bytes%8)return false;
        // Copy the actual padded 8x8 chunk layout, then inspect only the bounded
        // route's cells in pure code. No per-tile SDK flood or packed-layout
        // assumption that discards the queried chunk stride.
        visible_raw_.assign(visible.ptr(),visible.ptr()+layout.board_bytes/8);occupied_raw_.assign(occupied.ptr(),occupied.ptr()+layout.board_bytes/8);
        if(!current(f))return false;planning_.visible_raw=visible_raw_;planning_.occupied_raw=occupied_raw_;planning_.chunk_stride=layout.chunk_stride;planning_.packet_current=true;
        f.self.npc_quiet=quiet(planning_,f.self.position);f.source_released=f.self.npc_quiet&&!f.self.on_any_source;
        for(auto& peer:f.peers)if(peer.observed_now)peer.body.npc_quiet=quiet(planning_,peer.body.position);
        f.priority_contract_current=input.fresh&&input.priority_excluded;f.capital_obligation_clear=input.fresh&&input.capital_obligation_clear;
        monitor_.all_capture=true;for(unsigned i=0;i<p::actors;++i){const auto* body=n::peer(f,c.members[i].id);bool on_member=false;
            if(body&&body->alive)for(unsigned j=0;j<f.group.count;++j)on_member|=body->position==f.group.camps[j].position;
            monitor_.all_capture&=body&&body->alive&&on_member;}
        monitor_.capture_history_closed=policy_current(input,f,c);monitor_.economy_closed=input.fresh&&input.economy_events_closed;
        monitor_.bank_history_closed=policy_current(input,f,c)&&input.economy_events_closed;
        const auto& member=c.members[static_cast<unsigned>(q::detail::member(c,f.self.id))];const cambc::vec2 altar{member.altar.x,member.altar.y};
        f.altar_unlocked_known=cambc::vision::in_vision(altar);if(!current(f))return false;
        if(f.altar_unlocked_known){f.altar_unlocked=!cambc::vision::altar_lock_tiles(cambc::team::enemy).is_set(altar);if(!current(f))return false;}
        f.nominated_target=-1;float least=0;for(unsigned i=0;i<f.group.count;++i){const auto& a=f.group.camps[i];if(a.observed_now&&a.alive&&(f.nominated_target<0||a.hp<least||(a.hp==least&&a.id<f.nominated_target))){f.nominated_target=a.id;least=a.hp;}}
        independent_={};f.independent_interpreter_ready=hooks_.callbacks.accept&&hooks_.callbacks.accept(hooks_.callbacks.context,f,independent_)
            &&independent(independent_,f);return current(f);
    }
    bool remaining(const q::Offer& offer,const n::Word& word,unsigned step,const q::Frame& f,n::Guard& out){
        if(step>=word.count||planning_.policy.source!=f.source||!policy_current(planning_.policy,f,contract_))return false;
        p::Point terminal=f.self.position;for(unsigned i=step;i<word.count;++i)if(word.steps[i].kind==n::PrimitiveKind::Move)terminal=word.steps[i].destination;
        const bool work=offer.kind==q::Kind::WorkTank||offer.kind==q::Kind::Strike||offer.kind==q::Kind::Heal;
        if(!survival(contract_,f,planning_,f.self.position,work)||!survival(contract_,f,planning_,terminal,work))return false;
        const bool measure=hooks_.measure_now&&word.price.measure_now&&!word.price.bounded&&word.count==1&&step==0;
        if(!measure&&(!word.price.bounded||word.price.measure_now))return false;
        n::Price price;if(!hooks_.callbacks.price||!hooks_.callbacks.price(hooks_.callbacks.context,f,offer,word,step,price)
            ||price.bounded!=word.price.bounded||price.measure_now!=word.price.measure_now||price.model!=word.price.model||price.domain!=word.price.domain||price.guard!=word.price.guard
            ||price.command!=word.price.command||price.readback!=word.price.readback||price.exit_tail!=word.price.exit_tail)return false;
        if(!current(f))return false;out={f.source,word.token,step,true,true,true,true};return true;
    }
public:
    Sdk(std::span<const cambc::camp> nominated_group,std::span<const cambc::camp> catalogue,Geometry geometry,Hooks hooks)
        :catalogue_(catalogue.begin(),catalogue.end()),terrain_(geometry.blocked.begin(),geometry.blocked.end()),width_(geometry.width),height_(geometry.height),hooks_(hooks),catalogue_admitted_(geometry.complete_catalogue),
        native_(nominated_group,{this,&produce_cb,&remaining_cb,&independent_cb}){
        if(width_<=0||height_<=0||width_>100||height_>100||terrain_.size()!=static_cast<unsigned>(width_*height_)){width_=height_=0;terrain_.clear();}
    }
    int owner(){return native_.owner();}int round(){return native_.round();}unsigned fuel(){return native_.fuel();}q::Revision event(){return native_.event();}
    void read_words(std::array<std::uint16_t,4>& words){native_.read_words(words);}bool write_word(unsigned offset,std::uint16_t value,n::Fence fence){return native_.write_word(offset,value,fence);}
    const ObservationCost& last_observe_cost()const{return last_observe_cost_;}
    bool observe(q::Epoch e,const q::Contract& c,q::Frame& f){last_observe_cost_={};if(!hooks_.measure_now)return native_.observe(e,c,f);
        const int before_round=native_.round();const unsigned before=native_.fuel();const int before_fence=native_.round();
        last_observe_cost_.before=before;last_observe_cost_.rounds={before_round,before_fence,-1,-1};last_observe_cost_.requested_round=e.round;
        if(before_round!=e.round||before_fence!=e.round)return false;
        const bool observed=native_.observe(e,c,f);const int after_round=native_.round();const unsigned after=native_.fuel();const int after_fence=native_.round();
        last_observe_cost_=observation_cost(e,f.source,before,after,{before_round,before_fence,after_round,after_fence},observed);
        return observed&&last_observe_cost_.known;
    }
    bool guard(const q::Offer& o,const n::Word& word,unsigned step,const q::Frame& f,n::Guard& out){return native_.guard(o,word,step,f,out);}
    bool execute(const n::Primitive& primitive,n::Fence fence,bool& accepted){return native_.execute(primitive,fence,accepted);}
    bool effects(const q::Frame& a,const q::Frame& b,const n::Primitive& primitive,bool accepted,const q::Lease& lease,std::span<q::Actual> out,unsigned& count){return native_.effects(a,b,primitive,accepted,lease,out,count);}
    bool independent(const n::Independent& grant,const q::Frame& f){return grant.source==f.source&&grant.grant&&grant.programme&&grant.complete&&grant.funded&&grant.accepted
        &&hooks_.callbacks.verify&&hooks_.callbacks.verify(hooks_.callbacks.context,grant,f)&&current(f);}
    bool independent_grant(const q::Frame& f,n::Independent& grant){grant=independent_;return independent(grant,f);}
    bool tail_complete(const q::Frame& f){return planning_.policy.source==f.source&&planning_.policy.renewal_observed&&f.capital_obligation_clear;}
    bool admit(q::Epoch e,const q::Contract& c){q::Frame f;if(!width_||!catalogue_admitted_||catalogue_.empty()||catalogue_.size()>max_sources
            ||c.abort_by>=static_cast<int>(cambc::constants::max_rounds)||!hooks_.callbacks.price||!hooks_.callbacks.accept||!hooks_.callbacks.verify)return false;
        for(unsigned i=0;i<catalogue_.size();++i)for(unsigned j=0;j<i;++j)if(catalogue_[i]==catalogue_[j])return false;
        for(unsigned i=0;i<c.camp_count;++i){bool found=false;
            for(const auto& camp:catalogue_){if(static_cast<int>(static_cast<cambc::handle_camp_t>(camp))==c.camps[i]){
                const auto group=camp.camp_group();if(static_cast<int>(static_cast<cambc::handle_camp_group_t>(group))!=c.group)return false;found=true;}}
            if(!found)return false;}
        for(unsigned i=0;i<p::actors;++i){bool on_member=false;for(const auto& camp:catalogue_)for(unsigned j=0;j<c.camp_count;++j)
                if(static_cast<int>(static_cast<cambc::handle_camp_t>(camp))==c.camps[j]){const auto at=camp.spawn_position();on_member|=c.members[i].capture==p::Point{at.x,at.y};}
            if(!on_member)return false;for(unsigned j=0;j<i;++j)if(c.members[i].capture==c.members[j].capture)return false;}
        return observe(e,c,f)&&policy_current(planning_.policy,f,c)&&f.independent_interpreter_ready;}
    bool nominate(const q::Contract& c,const q::Offer& offer,const q::Frame& f,n::Word& word,q::Quote& quote){return party_runtime::nominate(c,offer,f,planning_,hooks_.callbacks,++words_,word,quote,hooks_.measure_now);}
    bool monitor(const q::Frame& f,MonitorSample& out){if(monitor_.source!=f.source)return false;out=monitor_;return true;}
};
#endif
}
