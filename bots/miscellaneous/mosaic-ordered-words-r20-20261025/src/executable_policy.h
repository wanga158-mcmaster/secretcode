#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <algorithm>
#include <cmath>

namespace mosaic::executable_policy {
using NativeId=std::uint32_t;
using Revision=std::uint64_t;
inline constexpr unsigned max_successful=4,max_attempts=6,target_capacity=6,move_capacity=8,phase_capacity=24,trail_capacity=16;
inline constexpr auto invalid_cost=std::numeric_limits<std::uint64_t>::max();
struct Point {int x{},y{};friend bool operator==(Point a,Point b){return a.x==b.x&&a.y==b.y;}};
struct Stamp {
    Revision epoch{},frame{},effect{},observation{},host_attempt{};
    friend bool operator==(const Stamp&,const Stamp&)=default;
};
inline bool same_physical(const Stamp& a,const Stamp& b){return a.epoch==b.epoch&&a.frame==b.frame&&a.effect==b.effect&&a.host_attempt==b.host_attempt;}
struct Token {Stamp stamp{};Revision lease{};bool valid{};};
enum class ActionKind {basic,move,cast,observe,recall,bank,wait};
enum class TargetKind {none,self,champion,camp,ground};
enum class Channel {none,recall,bank,teleport};
enum class WaitReason {none,channel,capture};
enum class Purpose {work,capture,bank,recover,travel,observe};
enum class Stage {work,capture,bank,recover,complete};
enum class Outcome {accepted,refused,changed_premise};
enum class Failure {none,invalid_policy,stale,unfunded,changed_premise,refused,unsupported,bound_overrun,feedback_missing};
struct ServiceKey {
    Revision service{},job{},episode{};
    friend bool operator==(const ServiceKey&,const ServiceKey&)=default;
};
struct ActualAuthority {
    bool dead{true},raw_m{},raw_a{},raw_c{},walked{},completed{},recall_started{},bank_started{};
    Channel channel{Channel::none};std::uint8_t restrictions{};
    bool stunned{};
    friend bool operator==(const ActualAuthority&,const ActualAuthority&)=default;
};
struct TargetFact {TargetKind kind{TargetKind::none};NativeId id{};Point point{};bool actual{},visible{},alive{},ally{};
    bool health_actual{};double health{};bool shield_actual{};double shield{};};
struct MoveFact {Point point{};bool actual{},reachable{},open{};};
struct GroundFact {Point point{};bool actual{},visible{},in_bounds{},landing_open{};};
inline constexpr unsigned target_self=1,target_ally=2,target_enemy=4,target_camp=8,target_ground=16;
struct AbilityFact {bool actual{},ready{},exclusive_with_walk{},restores_attack{};unsigned target_mask{};int range{};
    bool cooldown_actual{};unsigned cooldown{};bool moves_caster{};};
struct Snapshot {
    Stamp stamp{};Revision authority_lease{};NativeId self{};Point pose{};ActualAuthority authority{};
    bool body_actual{},restrictions_actual{},channel_actual{};int attack_range{1};
    bool health_actual{};double health{};bool shield_actual{};double shield{};
    std::array<TargetFact,target_capacity> targets{};unsigned target_count{};
    std::array<MoveFact,move_capacity> moves{};unsigned move_count{};
    std::array<GroundFact,move_capacity> grounds{};unsigned ground_count{};
    std::array<AbilityFact,2> abilities{};
    bool gems_actual{};unsigned gems{};
    bool bank_target_actual{},bank_unlocked{};Point altar{};int bank_range{1};
    bool observation_actual{},observation_allowed{};Point observation_point{};
    bool capture_actual{},group_clear{},reward_available{},on_region{},uncontested{},contest_known{};NativeId group{};unsigned finite_capture_inputs{};
    bool channel_timer_actual{};unsigned channel_ticks{};
};
struct Action {
    ActionKind kind{ActionKind::wait};TargetKind target_kind{TargetKind::none};NativeId target_id{};
    Point point{};unsigned slot{};WaitReason wait_reason{WaitReason::none};
};
struct Word {std::array<Action,max_attempts> actions{};unsigned count{};};
enum class Guard {none,usable_move,usable_attack,usable_cast,channel_active,always};
struct GuardedWord {bool declared{};Guard guard{Guard::none};Word word{};bool exclude_point{};Point excluded{};};
struct Policy {
    Token token{};ServiceKey owner{};Purpose purpose{Purpose::travel};
    Word success{};GuardedWord on_refusal{},on_changed{};
    Revision actual_prefix{}; // actual receipt serial, never an observation nonce
};
struct Receipt {
    ServiceKey owner{};Action action{};Outcome outcome{Outcome::changed_premise};
    Stamp before{},after{};Point before_pose{},after_pose{};
    ActualAuthority before_authority{},after_authority{};
    Revision serial{};bool actual_attempt{},actual_body{},actual_targets{},actual_occupancy{};
    bool health_actual{},shield_actual{},gems_actual{};
    double before_health{},after_health{},before_shield{},after_shield{};unsigned before_gems{},after_gems{};
    bool target_life_actual{},target_alive{};NativeId target_id{};TargetKind target_kind{TargetKind::none};
};
enum class ServiceEvent {work,clear,reward,bank,recovered,loss};
struct ServiceReceipt {
    ServiceKey owner{};ServiceEvent event{ServiceEvent::work};Stamp stamp{};Revision serial{};
    bool actual{};NativeId group{};unsigned amount{};Point pose{};
    bool full_bank_actual{}; // actual complete-bag submission, never command/channel start
};
struct FailureLatch {
    Failure failure{Failure::none};Stamp stamp{};unsigned primitive{};
    bool record(Failure value,Stamp at,unsigned index){if(value==Failure::none||failure!=Failure::none)return false;failure=value;stamp=at;primitive=index;return true;}
};
enum class ValueState {unknown,conditional};
enum class Assurance {none,partial,certified};
struct ConditionalKey {Stamp stamp{};Revision world{},replies{};unsigned horizon{};friend bool operator==(const ConditionalKey&,const ConditionalKey&)=default;};
struct Valuation {ValueState state{ValueState::unknown};ConditionalKey key{};double value{};Assurance assurance{Assurance::none};};
using ConditionalUtilityQuote=Valuation;
// This is separately scoped evidence metadata. Neither a numeric utility nor
// an executable native word manufactures certified full-support assurance.
struct FullSupportAssurance {Assurance state{Assurance::none};ConditionalKey key{};Revision scope{};};
struct Comparison {Valuation incumbent{},alternative{};bool joint_priced{},optional_funded{};};
enum class Choice {incumbent,alternative};
struct Phase {Revision id{};unsigned sdk_queries{},commands{};std::uint64_t compute_bound{};friend bool operator==(const Phase&,const Phase&)=default;};
struct Price {std::uint64_t query_bound{},command_bound{},final_tail{};};
struct Branch {std::array<Phase,phase_capacity> phases{};unsigned count{};};
struct Funding {Branch success{};std::array<Branch,max_attempts> refusals{},changes{};unsigned refusal_count{},changed_count{};};
struct FundingQuote {bool valid{};std::uint64_t success{},refusal{},changed{},required{};};
enum class BranchKind {success,refusal,changed};

inline bool valid_policy(const Policy& policy);
inline bool valid_shape(const Policy& policy);
inline bool valid_policy(const Snapshot& snapshot,const Policy& policy);
inline bool authorize(const Snapshot& snapshot,const Action& action);
inline bool guard_allows(Guard guard,const Snapshot& snapshot);
inline Choice choose(const Policy& incumbent,const Policy& alternative,const Comparison& comparison);
inline Choice choose(const Snapshot& current,const Policy& incumbent,const Policy& alternative,const Comparison& comparison);
inline FundingQuote quote_funding(const Funding& funding,const Price& price);
inline bool token_matches(const Token& token,const Snapshot& snapshot){
    return token.valid&&token.lease&&token.lease==snapshot.authority_lease&&same_physical(token.stamp,snapshot.stamp)&&token.stamp.observation<=snapshot.stamp.observation;
}
// Consumes authority on any actual host attempt. Queries use Lease::observe;
// they never call this primitive or fabricate a new host-attempt receipt.
inline bool consume(Token& token,const Stamp& after){
    const bool was_valid=token.valid;token.valid=false;
    if(!was_valid||after.epoch!=token.stamp.epoch||after.frame!=token.stamp.frame||
       token.stamp.host_attempt==invalid_cost||after.host_attempt!=token.stamp.host_attempt+1||
       after.effect<token.stamp.effect||after.observation<token.stamp.observation)return false;
    return true;
}
inline bool begin_attempt(Token& token,Stamp& current){
    const bool allowed=token.valid&&token.lease&&same_physical(token.stamp,current)&&
        current.observation>=token.stamp.observation&&current.host_attempt!=invalid_cost;
    token.valid=false;if(!allowed)return false;++current.host_attempt;return true;
}
inline int distance(Point a,Point b){
    const auto dx=std::int64_t(a.x)-std::int64_t(b.x),dy=std::int64_t(a.y)-std::int64_t(b.y);
    return int(std::min<std::int64_t>(std::max(dx<0?-dx:dx,dy<0?-dy:dy),std::numeric_limits<int>::max()));
}
inline bool usable(const Snapshot& s){return s.body_actual&&s.restrictions_actual&&s.channel_actual&&!s.authority.dead&&!s.authority.completed;}
inline bool guard_allows(Guard guard,const Snapshot& s){
    if(!usable(s))return false;
    if(guard==Guard::channel_active)return s.authority.channel!=Channel::none;
    if(guard==Guard::always)return true;
    if(s.authority.channel!=Channel::none||s.authority.stunned)return false;
    if(guard==Guard::usable_move)return s.authority.raw_m&&!(s.authority.restrictions&1);
    if(guard==Guard::usable_attack)return s.authority.raw_a&&!(s.authority.restrictions&2);
    if(guard==Guard::usable_cast)return s.authority.raw_c&&!(s.authority.restrictions&4);
    return false;
}
inline const TargetFact* target(const Snapshot& s,const Action& a){
    if(s.target_count>target_capacity)return nullptr;
    for(unsigned n=0;n<s.target_count;++n)if(s.targets[n].kind==a.target_kind&&s.targets[n].id==a.target_id)return &s.targets[n];
    return nullptr;
}
inline bool authorize(const Snapshot& s,const Action& a){
    if(!usable(s))return false;
    if(a.kind==ActionKind::wait){
        if(a.wait_reason==WaitReason::channel)return s.authority.channel!=Channel::none&&s.channel_timer_actual;
        // Unknown contest is an explicitly finite capture probe, not proof of
        // uncontested completion. It has no numeric safety or reward credit.
        return a.wait_reason==WaitReason::capture&&s.authority.channel==Channel::none&&s.capture_actual&&s.group_clear&&
            s.reward_available&&s.on_region&&s.finite_capture_inputs&&(!s.contest_known||s.uncontested);
    }
    if(s.authority.channel!=Channel::none||s.authority.stunned)return false;
    if(a.kind==ActionKind::move){
        if(!guard_allows(Guard::usable_move,s)||s.move_count>move_capacity||a.point==s.pose)return false;
        for(unsigned n=0;n<s.move_count;++n){const auto& p=s.moves[n];if(p.point==a.point)return p.actual&&p.reachable&&p.open;}
        return false;
    }
    if(a.kind==ActionKind::basic){
        const auto* t=target(s,a);return guard_allows(Guard::usable_attack,s)&&t&&t->actual&&t->visible&&t->alive&&
            (a.target_kind==TargetKind::camp||(a.target_kind==TargetKind::champion&&!t->ally))&&distance(s.pose,t->point)<=s.attack_range;
    }
    if(a.kind==ActionKind::cast){
        if(a.slot>=2||!guard_allows(Guard::usable_cast,s))return false;const auto& ability=s.abilities[a.slot];
        if(!ability.actual||!ability.ready||(ability.cooldown_actual&&ability.cooldown)||
           (ability.exclusive_with_walk&&s.authority.walked))return false;
        Point at=s.pose;unsigned mask=target_self;
        if(a.target_kind==TargetKind::ground){at=a.point;mask=target_ground;
            if(s.ground_count>move_capacity)return false;bool actual=false;
            for(unsigned n=0;n<s.ground_count;++n){const auto& g=s.grounds[n];if(g.point==at)
                actual=g.actual&&g.visible&&g.in_bounds&&(!ability.moves_caster||(g.landing_open&&at!=s.pose));}
            if(!actual)return false;
        }
        else if(a.target_kind==TargetKind::champion||a.target_kind==TargetKind::camp){const auto* t=target(s,a);
            if(!t||!t->actual||!t->visible||!t->alive)return false;at=t->point;
            mask=a.target_kind==TargetKind::camp?target_camp:t->ally?target_ally:target_enemy;
        }else if(a.target_kind!=TargetKind::self&&a.target_kind!=TargetKind::none)return false;
        if(ability.moves_caster&&a.target_kind!=TargetKind::ground)return false;
        return ability.range>=0&&distance(s.pose,at)<=ability.range&&(ability.target_mask&mask);
    }
    if(a.kind==ActionKind::observe)return !(s.authority.restrictions&32)&&s.observation_actual&&s.observation_allowed&&a.point==s.observation_point;
    if(a.kind==ActionKind::recall)return !s.authority.recall_started;
    if(a.kind==ActionKind::bank)return !s.authority.bank_started&&s.gems_actual&&s.gems&&
        s.bank_target_actual&&s.bank_unlocked&&a.point==s.altar&&distance(s.pose,a.point)<=s.bank_range;
    return false;
}
inline unsigned successes(const Word& word){unsigned n=0;for(unsigned i=0;i<word.count&&i<max_attempts;++i)n+=word.actions[i].kind!=ActionKind::wait;return n;}
inline bool valid_word(const Word& word){
    if(!word.count||word.count>max_attempts||successes(word)>max_successful)return false;
    for(unsigned i=0;i<word.count;++i){const auto& a=word.actions[i];
        if(unsigned(a.kind)>unsigned(ActionKind::wait)||unsigned(a.target_kind)>unsigned(TargetKind::ground)||
           (a.kind==ActionKind::cast&&a.slot>=2)||(a.kind==ActionKind::wait&&(a.wait_reason==WaitReason::none||word.count!=1)))return false;}
    return true;
}
inline bool valid_shape(const Policy& p){
    if(!valid_word(p.success))return false;
    const auto valid_branch=[](const GuardedWord& branch){return !branch.declared||
        (branch.guard!=Guard::none&&valid_word(branch.word)&&(!branch.exclude_point||
         std::none_of(branch.word.actions.begin(),branch.word.actions.begin()+branch.word.count,
            [&](const Action& a){return a.kind==ActionKind::move&&a.point==branch.excluded;})));};
    if(!valid_branch(p.on_refusal)||!valid_branch(p.on_changed))return false;
    unsigned accepted=0;
    for(unsigned i=0;i<p.success.count;++i){const auto& a=p.success.actions[i];
        const bool can_refuse=a.kind==ActionKind::move||a.kind==ActionKind::cast||a.kind==ActionKind::observe;
        if(can_refuse&&(!p.on_refusal.declared||accepted+successes(p.on_refusal.word)>max_successful||i+1+p.on_refusal.word.count>max_attempts))return false;
        if(a.kind!=ActionKind::wait&&(!p.on_changed.declared||accepted+successes(p.on_changed.word)>max_successful||i+p.on_changed.word.count>max_attempts))return false;
        accepted+=a.kind!=ActionKind::wait;
    }
    return true;
}
inline bool valid_policy(const Policy& p){return p.token.valid&&p.token.lease&&valid_shape(p);}
inline Choice choose(const Policy&,const Policy&,const Comparison&){return Choice::incumbent;}
inline Choice choose(const Snapshot& current,const Policy& incumbent,const Policy& alternative,const Comparison& c){
    // This helper receives an already playable incumbent. No model result,
    // optional preparation failure, or unsupported alternative revokes it.
    if(!valid_policy(current,incumbent)||!valid_policy(current,alternative)||!token_matches(incumbent.token,current)||!token_matches(alternative.token,current)||
       !c.joint_priced||!c.optional_funded||c.incumbent.key.stamp!=current.stamp||
       c.incumbent.state!=ValueState::conditional||c.alternative.state!=ValueState::conditional||
       c.incumbent.key!=c.alternative.key||!std::isfinite(c.incumbent.value)||!std::isfinite(c.alternative.value)||
       incumbent.owner!=alternative.owner||incumbent.actual_prefix!=alternative.actual_prefix||
       incumbent.token.lease!=alternative.token.lease||!same_physical(incumbent.token.stamp,alternative.token.stamp)||
       !same_physical(c.incumbent.key.stamp,incumbent.token.stamp)||!same_physical(c.incumbent.key.stamp,alternative.token.stamp)||
       c.incumbent.key.stamp.observation<incumbent.token.stamp.observation||
       c.incumbent.key.stamp.observation<alternative.token.stamp.observation||!c.incumbent.key.horizon)return Choice::incumbent;
    return c.alternative.value>c.incumbent.value?Choice::alternative:Choice::incumbent;
}
class Lease {
public:
    bool issue(Snapshot& s,Revision id){
        if(!s.body_actual||!s.restrictions_actual||!s.channel_actual||!id||id<=last_id_||
           (issued_&&(s.stamp.epoch<floor_.epoch||s.stamp.host_attempt<floor_.host_attempt||s.stamp.effect<floor_.effect||
            s.stamp.observation<floor_.observation||(s.stamp.epoch==floor_.epoch&&s.stamp.frame<floor_.frame)))||
           (issued_&&same_physical(s.stamp,last_issued_))){s.authority_lease=0;return false;}
        token_={s.stamp,id,true};last_id_=id;last_issued_=floor_=s.stamp;issued_=true;
        pose_=s.pose;authority_=s.authority;self_=s.self;s.authority_lease=id;return true;
    }
    bool observe(Snapshot& s){
        if(!token_.valid||!s.body_actual||!s.restrictions_actual||!s.channel_actual||s.self!=self_||s.pose!=pose_||
           s.authority!=authority_||!same_physical(token_.stamp,s.stamp)||s.stamp.observation<token_.stamp.observation){revoke();s.authority_lease=0;return false;}
        token_.stamp.observation=s.stamp.observation;floor_.observation=s.stamp.observation;s.authority_lease=token_.lease;return true;
    }
    Token token()const{return token_;}bool valid()const{return token_.valid;}
    void revoke(){token_.valid=false;}
    bool before_attempt(Snapshot& s){
        const bool joined=token_matches(token_,s);bool advanced=false;
        if(joined){advanced=begin_attempt(token_,s.stamp);if(advanced)floor_=s.stamp;}else revoke();
        s.authority_lease=0;s.body_actual=s.restrictions_actual=s.channel_actual=false;
        return joined&&advanced;
    }
private:
    Token token_{};Revision last_id_{};Stamp last_issued_{},floor_{};bool issued_{};
    Point pose_{};ActualAuthority authority_{};NativeId self_{};
};
struct ResourceInterval {
    bool valid{},mac_known{};ActualAuthority before{},success{},refusal{};
};
inline ResourceInterval resource_interval(const Snapshot& s,const Action& a){
    ResourceInterval result{};if(!authorize(s,a))return result;
    result.valid=true;result.mac_known=true;result.before=result.success=result.refusal=s.authority;
    auto& post=result.success;
    if(a.kind==ActionKind::basic){post.raw_a=false;if(s.attack_range>1)post.raw_m=false;}
    else if(a.kind==ActionKind::move){post.raw_m=false;post.walked=true;if(s.attack_range>1)post.raw_a=false;}
    else if(a.kind==ActionKind::cast){post.raw_c=false;const auto& ability=s.abilities[a.slot];
        if(ability.exclusive_with_walk)post.raw_m=false;if(ability.restores_attack)post.raw_a=true;}
    else if(a.kind==ActionKind::recall){post.channel=Channel::recall;post.recall_started=true;result.mac_known=false;}
    else if(a.kind==ActionKind::bank){post.channel=Channel::bank;post.bank_started=true;result.mac_known=false;}
    return result;
}
struct ResourceState {ActualAuthority authority{};int attack_range{};std::array<AbilityFact,2> abilities{};};
inline bool guard_resources(Guard guard,const ResourceState& s){
    if(s.authority.dead||s.authority.completed)return false;
    if(guard==Guard::always)return true;
    if(guard==Guard::channel_active)return s.authority.channel!=Channel::none;
    if(s.authority.channel!=Channel::none||s.authority.stunned)return false;
    return guard==Guard::usable_move?s.authority.raw_m&&!(s.authority.restrictions&1):
        guard==Guard::usable_attack?s.authority.raw_a&&!(s.authority.restrictions&2):
        guard==Guard::usable_cast?s.authority.raw_c&&!(s.authority.restrictions&4):false;
}
inline bool apply_resources(ResourceState& s,const Action& action){
    auto& post=s.authority;if(post.dead||post.completed)return false;
    if(action.kind==ActionKind::wait)return action.wait_reason==WaitReason::channel?post.channel!=Channel::none:
        action.wait_reason==WaitReason::capture&&post.channel==Channel::none;
    if(post.channel!=Channel::none||post.stunned)return false;
    if(action.kind==ActionKind::basic){if(!guard_resources(Guard::usable_attack,s))return false;post.raw_a=false;if(s.attack_range>1)post.raw_m=false;}
    else if(action.kind==ActionKind::move){if(!guard_resources(Guard::usable_move,s))return false;post.raw_m=false;post.walked=true;if(s.attack_range>1)post.raw_a=false;}
    else if(action.kind==ActionKind::cast){
        if(action.slot>=2||!guard_resources(Guard::usable_cast,s))return false;auto& a=s.abilities[action.slot];
        if(!a.actual||!a.ready||(a.cooldown_actual&&a.cooldown)||(a.exclusive_with_walk&&post.walked))return false;
        post.raw_c=false;if(a.exclusive_with_walk)post.raw_m=false;if(a.restores_attack)post.raw_a=true;a.ready=false;
    }else if(action.kind==ActionKind::observe){if(post.restrictions&32)return false;}
    else if(action.kind==ActionKind::recall){if(post.recall_started)return false;post.recall_started=true;post.channel=Channel::recall;}
    else if(action.kind==ActionKind::bank){if(post.bank_started)return false;post.bank_started=true;post.channel=Channel::bank;}
    else return false;
    return true;
}
inline bool branch_resources(ResourceState state,const GuardedWord& branch){
    if(!branch.declared||!guard_resources(branch.guard,state))return false;
    for(unsigned n=0;n<branch.word.count;++n)if(!apply_resources(state,branch.word.actions[n]))return false;
    return true;
}
inline bool valid_policy(const Snapshot& current,const Policy& p){
    if(!valid_policy(p)||!token_matches(p.token,current)||!authorize(current,p.success.actions[0]))return false;
    // This checks only the native resource word, never incoming danger or
    // numeric utility. Targets/occupancy must still be refreshed after every
    // host attempt, and changed-premise continuation remains explicit.
    ResourceState projected{current.authority,current.attack_range,current.abilities};
    for(unsigned n=0;n<p.success.count;++n){const auto& action=p.success.actions[n];
        // Future target Life/occupancy may legitimately change because earlier
        // work killed a body. Only deterministic MAC is projected here.
        if(action.kind!=ActionKind::wait&&!branch_resources(projected,p.on_changed))return false;
        if((action.kind==ActionKind::move||action.kind==ActionKind::cast||action.kind==ActionKind::observe)&&
           !branch_resources(projected,p.on_refusal))return false;
        if(!apply_resources(projected,action))return false;
    }
    return true;
}
inline std::uint64_t add_cost(std::uint64_t a,std::uint64_t b){return b>invalid_cost-a?invalid_cost:a+b;}
inline std::uint64_t multiply_cost(std::uint64_t a,std::uint64_t b){return a&&b>invalid_cost/a?invalid_cost:a*b;}
inline std::uint64_t phase_cost(const Phase& phase,const Price& price){
    return add_cost(add_cost(multiply_cost(phase.sdk_queries,price.query_bound),multiply_cost(phase.commands,price.command_bound)),phase.compute_bound);
}
inline std::uint64_t branch_cost(const Branch& branch,const Price& price){
    if(!branch.count||branch.count>phase_capacity)return invalid_cost;auto result=price.final_tail;
    for(unsigned n=0;n<branch.count;++n){if(!branch.phases[n].id)return invalid_cost;
        for(unsigned j=0;j<n;++j)if(branch.phases[j].id==branch.phases[n].id)return invalid_cost;
        result=add_cost(result,phase_cost(branch.phases[n],price));}
    return result;
}
inline bool shared_prefix_only(const Branch& a,const Branch& b){
    unsigned common=0;
    while(common<std::min(a.count,b.count)&&a.phases[common].id==b.phases[common].id){
        if(a.phases[common]!=b.phases[common])return false;++common;
    }
    for(unsigned n=common;n<a.count;++n)for(unsigned j=0;j<b.count;++j)if(a.phases[n].id==b.phases[j].id)return false;
    for(unsigned n=common;n<b.count;++n)for(unsigned j=0;j<a.count;++j)if(b.phases[n].id==a.phases[j].id)return false;
    return true;
}
inline FundingQuote quote_funding(const Funding& f,const Price& p){
    FundingQuote result{};
    if(!p.query_bound||!p.command_bound||!p.final_tail||f.refusal_count>max_attempts||f.changed_count>max_attempts)return result;
    std::array<const Branch*,1+2*max_attempts> branches{};unsigned count=0;branches[count++]=&f.success;
    for(unsigned n=0;n<f.refusal_count;++n)branches[count++]=&f.refusals[n];
    for(unsigned n=0;n<f.changed_count;++n)branches[count++]=&f.changes[n];
    result.success=branch_cost(f.success,p);result.required=result.success;
    for(unsigned n=0;n<count;++n){const auto cost=branch_cost(*branches[n],p);if(cost==invalid_cost)return {};
        result.required=std::max(result.required,cost);
        for(unsigned j=0;j<n;++j)if(!shared_prefix_only(*branches[n],*branches[j]))return {};
    }
    for(unsigned n=0;n<f.refusal_count;++n)result.refusal=std::max(result.refusal,branch_cost(f.refusals[n],p));
    for(unsigned n=0;n<f.changed_count;++n)result.changed=std::max(result.changed,branch_cost(f.changes[n],p));
    result.valid=result.required!=invalid_cost;return result;
}
class Book {
public:
    bool reserve(const Funding& funding,const Price& price,std::uint64_t available){
        if(active_)return false;const auto quote=quote_funding(funding,price);
        if(!quote.valid||available<quote.required){failure_.record(Failure::unfunded,{},0);return false;}
        // The immutable validated plan must outlive this word. Normal static
        // workspaces may rewrite only active prefixes before the next reserve;
        // no whole-capacity plan copy or per-phase initialization occurs here.
        funding_=&funding;price_=price;paid_count_=0;selected_=false;selected_kind_=BranchKind::success;selected_index_=0;active_=true;return true;
    }
    std::uint64_t remaining(BranchKind kind,unsigned index=0)const{
        const auto* path=branch(kind,index);if(!active_||!path)return invalid_cost;auto result=price_.final_tail;
        for(unsigned n=0;n<path->count;++n)if(!paid(path->phases[n].id))result=add_cost(result,phase_cost(path->phases[n],price_));
        return result;
    }
    std::uint64_t remaining()const{
        if(!active_)return invalid_cost;if(selected_)return remaining(selected_kind_,selected_index_);
        auto result=remaining(BranchKind::success);
        for(unsigned n=0;n<funding_->refusal_count;++n)result=std::max(result,remaining(BranchKind::refusal,n));
        for(unsigned n=0;n<funding_->changed_count;++n)result=std::max(result,remaining(BranchKind::changed,n));return result;
    }
    bool funded(std::uint64_t available)const{const auto cost=remaining();return cost!=invalid_cost&&available>=cost;}
    bool phase_paid(Revision id,std::uint64_t actual_spent,bool actual=true){
        if(!active_||!actual||paid(id))return false;
        const auto* path=branch(selected_?selected_kind_:BranchKind::success,selected_?selected_index_:0);
        if(!path)return false;const Phase* next=nullptr;
        for(unsigned n=0;n<path->count;++n)if(!paid(path->phases[n].id)){next=&path->phases[n];break;}
        if(!next||next->id!=id)return false;
        paid_[paid_count_++]=id;
        if(actual_spent>phase_cost(*next,price_)){active_=false;failure_.record(Failure::bound_overrun,{},unsigned(paid_count_-1));return false;}
        return true;
    }
    bool follow(BranchKind kind,const Receipt& receipt,unsigned index=0){
        if(!active_||selected_||!receipt.serial||receipt.serial<=branch_receipt_||!branch(kind,index))return false;
        bool actual=false;
        if(kind==BranchKind::refusal)actual=receipt.actual_attempt&&receipt.outcome==Outcome::refused;
        else if(kind==BranchKind::changed)actual=!receipt.actual_attempt&&receipt.outcome==Outcome::changed_premise&&receipt.actual_body&&
            (receipt.actual_targets||receipt.actual_occupancy||receipt.before_pose!=receipt.after_pose||receipt.before_authority!=receipt.after_authority);
        else{actual=receipt.actual_attempt&&receipt.outcome==Outcome::accepted;
            // A prefix success cannot release escrow for a later false. Only
            // the fully paid success path may become the terminal selection.
            for(unsigned n=0;n<funding_->success.count;++n)if(!paid(funding_->success.phases[n].id))return false;
        }
        if(!actual)return false;
        const auto* path=branch(kind,index);
        // A selected path may not erase an already paid divergent effect.
        for(unsigned n=0;n<paid_count_;++n){bool contains=false;for(unsigned j=0;j<path->count;++j)contains=contains||path->phases[j].id==paid_[n];if(!contains)return false;}
        selected_=true;selected_kind_=kind;selected_index_=index;branch_receipt_=receipt.serial;return true;
    }
    bool finish(const Receipt& receipt){
        if(!active_||!receipt.actual_attempt||receipt.outcome!=Outcome::accepted||!receipt.actual_body||
           !receipt.serial||receipt.serial<=finish_receipt_)return false;
        const auto* path=branch(selected_?selected_kind_:BranchKind::success,selected_?selected_index_:0);
        for(unsigned n=0;path&&n<path->count;++n)if(!paid(path->phases[n].id))return false;
        active_=false;finish_receipt_=receipt.serial;return true;
    }
    const FailureLatch& failure()const{return failure_;}
private:
    const Branch* branch(BranchKind kind,unsigned index)const{
        if(!funding_)return nullptr;
        if(kind==BranchKind::success)return index?nullptr:&funding_->success;
        if(kind==BranchKind::refusal)return index<funding_->refusal_count?&funding_->refusals[index]:nullptr;
        return index<funding_->changed_count?&funding_->changes[index]:nullptr;
    }
    bool paid(Revision id)const{for(unsigned n=0;n<paid_count_;++n)if(paid_[n]==id)return true;return false;}
    const Funding* funding_{};Price price_{};bool active_{},selected_{};BranchKind selected_kind_{BranchKind::success};unsigned selected_index_{};
    std::array<Revision,phase_capacity*(1+2*max_attempts)> paid_{};unsigned paid_count_{};
    Revision branch_receipt_{},finish_receipt_{};FailureLatch failure_{};
};
enum class NextRequired {work,capture,bank,recover,none};
struct TrailStep {Point from{},to{};ActionKind kind{ActionKind::move};Revision receipt{};};
class ServiceOwner {
public:
    bool request(ServiceKey key,Purpose purpose=Purpose::work){
        if(has_key_&&key==key_)return false;key_=key;has_key_=true;
        stage_=purpose==Purpose::capture?Stage::capture:purpose==Purpose::bank?Stage::bank:purpose==Purpose::recover?Stage::recover:Stage::work;
        resume_=stage_;prediction_current_=false;return true;
    }
    bool accept(const Receipt& r){
        if(!has_key_||r.owner!=key_||!r.serial||r.serial<=last_receipt_||!r.actual_attempt||r.outcome==Outcome::changed_premise)return false;
        if((have_attempt_&&r.before.host_attempt<=last_attempt_before_)||r.before.host_attempt<last_stamp_.host_attempt||
           r.before.epoch<last_stamp_.epoch||r.before.effect<last_stamp_.effect||r.before.observation<last_stamp_.observation||
           (r.before.epoch==last_stamp_.epoch&&r.before.frame<last_stamp_.frame))return false;
        have_attempt_=true;last_attempt_before_=r.before.host_attempt;
        last_receipt_=r.serial;
        if(r.outcome==Outcome::refused){
            if(refusal_debt_!=std::numeric_limits<unsigned>::max())++refusal_debt_;
            failure_.record(Failure::refused,r.before,0);prediction_current_=false;
            // Native FALSE already exists even if feedback is missing. Never
            // refund that debt or advance geometry because validation failed.
            return r.actual_body&&r.before_pose==r.after_pose&&r.before_authority==r.after_authority&&
                r.after.epoch==r.before.epoch&&r.after.frame==r.before.frame&&r.before.host_attempt!=invalid_cost&&
                r.after.host_attempt==r.before.host_attempt+1&&r.after.effect==r.before.effect;
        }
        actual_prefix_=r.serial;prediction_current_=false;
        if(pose_known_&&r.before_pose!=pose_)trail_complete_=false;
        if(!r.actual_body||r.after.epoch!=r.before.epoch||r.after.frame!=r.before.frame||r.before.host_attempt==invalid_cost||
           r.after.host_attempt!=r.before.host_attempt+1||r.after.effect<r.before.effect||r.after.observation<r.before.observation){
            trail_complete_=false;pose_known_=false;failure_.record(Failure::feedback_missing,r.before,0);return false;}
        last_stamp_=r.after;pose_=r.after_pose;pose_known_=true;
        if(r.before_pose!=r.after_pose){
            if(trail_count_<trail_capacity)trail_[trail_count_++]={r.before_pose,r.after_pose,r.action.kind,r.serial};else trail_complete_=false;
        }
        return true;
    }
    void knowledge(Stamp stamp){knowledge_=stamp;prediction_current_=false;}
    // An unwrapped external owner may have changed pose/resources. Keep every
    // owned receipt, macro stage and false debt, but never claim full geometry
    // across that boundary. A new physical episode requires actual recovery.
    void handoff_unknown(){prediction_current_=false;trail_complete_=false;pose_known_=false;}
    bool service(const ServiceReceipt& r){
        if(!has_key_||r.owner!=key_||!r.actual||!r.serial||r.serial<=last_service_||
           r.stamp.host_attempt<last_stamp_.host_attempt||r.stamp.epoch<last_stamp_.epoch||
           r.stamp.effect<last_stamp_.effect||r.stamp.observation<last_stamp_.observation||
           (r.stamp.epoch==last_stamp_.epoch&&r.stamp.frame<last_stamp_.frame))return false;
        if((r.event==ServiceEvent::clear||r.event==ServiceEvent::reward)&&Revision(r.group)!=key_.job)return false;
        bool accepted=false;
        if(r.event==ServiceEvent::work)accepted=stage_==Stage::work;
        else if(r.event==ServiceEvent::clear&&stage_==Stage::work){stage_=Stage::capture;accepted=true;}
        else if(r.event==ServiceEvent::reward&&stage_==Stage::capture){
            if(r.amount)bank_pending_=true;
            stage_=r.amount?Stage::bank:Stage::recover;resume_=r.amount?Stage::bank:Stage::complete;accepted=true;
        }
        else if(r.event==ServiceEvent::bank&&(stage_==Stage::bank||bank_pending_)&&r.amount&&r.full_bank_actual){stage_=Stage::complete;bank_pending_=false;accepted=true;}
        // This explicit trusted native loss/death event is different from a
        // zero bag read, a model prediction or a newly nominated service key.
        else if(r.event==ServiceEvent::loss){bank_pending_=false;stage_=Stage::recover;resume_=Stage::work;accepted=true;}
        else if(r.event==ServiceEvent::recovered&&stage_==Stage::recover){stage_=resume_==Stage::recover?Stage::complete:resume_;refusal_debt_=0;
            trail_count_=0;trail_complete_=true;accepted=true;}
        if(!accepted)return false;last_service_=r.serial;last_stamp_=r.stamp;pose_=r.pose;pose_known_=true;prediction_current_=false;return true;
    }
    void recover(){if(stage_!=Stage::recover){resume_=stage_;stage_=Stage::recover;}}
    bool word_boundary(Revision actual_serial){if(!actual_serial||actual_serial!=actual_prefix_||actual_serial<=last_boundary_)return false;last_boundary_=actual_serial;++words_;return true;}
    Stage stage()const{return stage_;}ServiceKey key()const{return key_;}Revision actual_prefix()const{return actual_prefix_;}
    NextRequired next_required()const{
        return stage_==Stage::work?NextRequired::work:stage_==Stage::capture?NextRequired::capture:stage_==Stage::bank?NextRequired::bank:stage_==Stage::recover?NextRequired::recover:NextRequired::none;
    }
    unsigned refusal_debt()const{return refusal_debt_;}unsigned trail_size()const{return trail_count_;}
    bool bank_pending()const{return bank_pending_;}
    const TrailStep& trail(unsigned n)const{return trail_[n];}bool trail_complete()const{return trail_complete_;}
    bool pose_known()const{return pose_known_;}Point actual_pose()const{return pose_;}unsigned completed_words()const{return words_;}
    const FailureLatch& failure()const{return failure_;}
private:
    ServiceKey key_{};bool has_key_{};Stage stage_{Stage::work},resume_{Stage::work};
    Revision actual_prefix_{},last_receipt_{},last_service_{},last_boundary_{},last_attempt_before_{};bool have_attempt_{};Stamp last_stamp_{},knowledge_{};
    unsigned refusal_debt_{},trail_count_{},words_{};bool trail_complete_{true},pose_known_{},prediction_current_{},bank_pending_{};Point pose_{};
    std::array<TrailStep,trail_capacity> trail_{};FailureLatch failure_{};
};
} // namespace mosaic::executable_policy
