#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Two workers compare their OWN terminal banked-value quotes. This module does
// not simulate a foreign body, reserve a group, infer Life, or grant an action.
namespace tt::production_assignment {
struct Key
{
    std::uint64_t pair_id{};
    std::uint32_t start{},horizon{};
    std::array<std::uint64_t,2> workers{},groups{},generations{};
    friend bool operator==(const Key&,const Key&)=default;
};
enum class Status { Ready,Unknown,Unsupported,Stale,Overflow,Refuted };
enum class RefutationKind { Physical,Calendar };
inline bool canonical(const Key& key)
{return key.start<=key.horizon && key.workers[0]<key.workers[1] && key.groups[0]<key.groups[1];}
// Quote reuse has a calendar/roster context; physical admission failures do not.
inline bool same_context(const Key& a,const Key& b)
{return a.start==b.start && a.horizon==b.horizon && a.workers==b.workers &&
    a.groups==b.groups && a.generations==b.generations;}
inline int group_index(const Key& key,std::uint64_t group)
{return group==key.groups[0]?0:group==key.groups[1]?1:-1;}
struct Value
{
    Key key;
    // Producer-owned semantic revisions, not query/epoch counters. Reusing a
    // quote requires these premises to remain applicable; a new binding never
    // upgrades a dated quote into actual Life, equipment or execution authority.
    std::uint64_t actor{},group{},generation{},body_revision{},life_revision{},geometry_revision{},word_revision{};
    std::int64_t banked{}; // terminal integer banked score through key.horizon; never carried gems
    bool complete{},supported{},conditional{},disjoint_supported{};
    std::uint64_t enabler{}; // stable named enabler identity, not a proposal id/round
};
inline Status validate(const Key& key,const Value& value)
{
    if(!canonical(key))return Status::Unknown;
    if(value.key!=key)return Status::Stale;
    if(value.actor!=key.workers[0] && value.actor!=key.workers[1])return Status::Unsupported;
    const auto group=group_index(key,value.group);
    if(group<0 || value.generation!=key.generations[static_cast<std::size_t>(group)])return Status::Stale;
    if(value.banked<0)return Status::Unsupported;
    if(!value.complete)return Status::Unknown;
    if(!value.supported || !value.disjoint_supported)return Status::Unsupported;
    return Status::Ready;
}
struct Difference
{
    Key key;
    std::uint64_t actor{},body_revision{};
    std::array<std::uint64_t,2> life_revisions{},geometry_revisions{},word_revisions{};
    std::int64_t delta{}; // own value(G) - own value(H), not an absolute team score
    bool complete{},supported{},conditional{},disjoint_supported{};
    std::array<std::uint64_t,2> enablers{};
};
struct Answer {Status status{Status::Unknown};Difference difference;};

// Fixed two-cell OWN cache and bounded semantic refutations. It never starts a
// producer when asked: an unanswered/incomplete peer request remains Unknown.
template<std::size_t MaxRefutations=8>
class OwnMenu
{
    struct Cell {bool present{};Value value;};
    struct Failure {Key context;Value value;RefutationKind kind{RefutationKind::Physical};};
public:
    OwnMenu()=default;
    OwnMenu(Key key,std::uint64_t self){bind(key,self);}
    Status bind(Key key,std::uint64_t self)
    {
        const bool next_bound=canonical(key) && (self==key.workers[0] || self==key.workers[1]);
        // Invalid request metadata cannot erase retained physical evidence.
        if(!next_bound){bound_=false;return Status::Unknown;}
        const bool same_owner=history_owned_ && self_==self;
        const bool retain=same_owner && key_.start==key.start && key_.horizon==key.horizon;
        const auto previous=cells_;for(auto& cell:cells_)cell.present=false;
        if(retain)
        {
            for(const auto& cell:previous)if(cell.present)
            {
                const auto index=group_index(key,cell.value.group);
                if(index>=0 && cell.value.generation==key.generations[static_cast<std::size_t>(index)])
                {cells_[static_cast<std::size_t>(index)]=cell;cells_[static_cast<std::size_t>(index)].value.key=key;}
            }
        }
        if(same_owner)
        {
            std::size_t kept=0;
            for(std::size_t i=0;i<failure_count_;++i)
            {
                // A new deadline can discharge a NoProfit/Dates failure. It
                // cannot discharge actual Work/ingress admission evidence, even
                // if this pair temporarily names other groups or a new peer.
                if(failures_[i].kind==RefutationKind::Physical ||
                    (failures_[i].context.start==key.start && failures_[i].context.horizon==key.horizon))
                    failures_[kept++]=failures_[i];
            }
            failure_count_=kept;
            if(!retain)calendar_overflow_=false;
        }
        else {failure_count_=0;physical_overflow_=calendar_overflow_=false;}
        key_=key;self_=self;bound_=history_owned_=true;return Status::Ready;
    }
    Status put(const Value& value)
    {
        if(!bound_)return Status::Unknown;
        if(value.key!=key_)return Status::Stale;
        if(value.actor!=self_)return Status::Unsupported;
        const auto index=group_index(key_,value.group);
        if(index<0 || value.generation!=key_.generations[static_cast<std::size_t>(index)])return Status::Stale;
        if(value.banked<0)return Status::Unsupported;
        cells_[static_cast<std::size_t>(index)]={true,value};
        return Status::Ready; // stored is distinct from an answerable completed quote
    }
    const Value* cached(std::uint64_t group)const
    {
        const auto index=group_index(key_,group);
        return bound_ && index>=0 && cells_[static_cast<std::size_t>(index)].present?
            &cells_[static_cast<std::size_t>(index)].value:nullptr;
    }
    // Physical means an actual admission/perch/ingress failure under these
    // facts, NOT an unprofitable conditional value prediction. Calendar is for
    // deadline/NoProfit failures. Neither kind grants current Life authority.
    Status refute(std::uint64_t group,RefutationKind kind=RefutationKind::Physical)
    {
        const auto* value=cached(group);if(!value)return Status::Unknown;
        for(std::size_t i=0;i<failure_count_;++i)
            if(failures_[i].kind==kind && matches(failures_[i],*value))return Status::Ready;
        if(failure_count_==MaxRefutations)
        {
            if(kind==RefutationKind::Physical)physical_overflow_=true;else calendar_overflow_=true;
            return Status::Overflow;
        }
        failures_[failure_count_++]={key_,*value,kind};return Status::Ready;
    }
    Status answer(Difference& out)const
    {
        if(!bound_)return Status::Unknown;
        if(physical_overflow_ || calendar_overflow_)return Status::Overflow;
        for(const auto& cell:cells_)if(!cell.present)return Status::Unknown;
        for(const auto& cell:cells_)
        {
            const auto status=validate(key_,cell.value);if(status!=Status::Ready)return status;
            for(std::size_t i=0;i<failure_count_;++i)if(matches(failures_[i],cell.value))return Status::Refuted;
        }
        const auto& g=cells_[0].value;const auto& h=cells_[1].value;
        if(g.body_revision!=h.body_revision)return Status::Stale;
        Difference result;result.key=key_;result.actor=self_;result.body_revision=g.body_revision;
        result.life_revisions={g.life_revision,h.life_revision};
        result.geometry_revisions={g.geometry_revision,h.geometry_revision};
        result.word_revisions={g.word_revision,h.word_revision};
        result.enablers={g.enabler,h.enabler};
        // Both values are nonnegative int64, so their difference is representable.
        result.delta=g.banked-h.banked;result.complete=result.supported=result.disjoint_supported=true;
        result.conditional=g.conditional || h.conditional;out=result;return Status::Ready;
    }
    Answer answer()const {Answer result;result.status=answer(result.difference);return result;}
private:
    static bool matches(const Failure& failure,const Value& value)
    {
        const auto& prior=failure.value;
        return (failure.kind==RefutationKind::Physical ||
                (failure.context.start==value.key.start && failure.context.horizon==value.key.horizon)) &&
            prior.actor==value.actor && prior.group==value.group &&
            prior.generation==value.generation && prior.body_revision==value.body_revision &&
            prior.life_revision==value.life_revision && prior.geometry_revision==value.geometry_revision &&
            prior.word_revision==value.word_revision && prior.enabler==value.enabler;
    }
    // An overflow may contain unretained physical evidence. A new calendar
    // cannot safely reset it into an answerable empty set.
    Key key_;std::uint64_t self_{};bool bound_{},history_owned_{},physical_overflow_{},calendar_overflow_{};
    std::array<Cell,2> cells_{};
    std::array<Failure,MaxRefutations> failures_{};std::size_t failure_count_{};
};

enum class Assignment { A_G_B_H,A_H_B_G };
struct Decision
{
    Status status{Status::Unknown};Assignment assignment{Assignment::A_G_B_H};
    std::array<std::uint64_t,2> groups{},generations{};
    bool conditional{},has_absolute_score{};std::int64_t banked{};
};
inline Decision selected(const Key& key,Assignment assignment,bool conditional)
{
    Decision result;result.status=Status::Ready;result.assignment=assignment;result.conditional=conditional;
    result.groups=assignment==Assignment::A_G_B_H?key.groups:std::array{key.groups[1],key.groups[0]};
    result.generations=assignment==Assignment::A_G_B_H?key.generations:
        std::array{key.generations[1],key.generations[0]};
    return result;
}
inline Status validate(const Key& key,const Difference& difference,std::uint64_t actor)
{
    if(!canonical(key))return Status::Unknown;
    if(difference.key!=key || difference.actor!=actor)return Status::Stale;
    // A difference between nonnegative int64 scores cannot equal INT64_MIN.
    if(difference.delta==std::numeric_limits<std::int64_t>::min())return Status::Overflow;
    if(!difference.complete)return Status::Unknown;
    return difference.supported && difference.disjoint_supported?Status::Ready:Status::Unsupported;
}
inline Decision combine_regrets(const Key& key,const Difference& a,const Difference& b)
{
    const auto first=validate(key,a,key.workers[0]);if(first!=Status::Ready)return {first};
    const auto second=validate(key,b,key.workers[1]);if(second!=Status::Ready)return {second};
    // Comparing directly gives sign(deltaA-deltaB) without overflowing its
    // mathematical difference. Exact ties use canonical A->G/B->H.
    return selected(key,a.delta>=b.delta?Assignment::A_G_B_H:Assignment::A_H_B_G,a.conditional || b.conditional);
}
struct PairScore {Status status{Status::Unknown};std::int64_t banked{};bool conditional{};};
inline PairScore score_disjoint(const Key& key,const Value& a,const Value& b)
{
    const auto first=validate(key,a);if(first!=Status::Ready)return {first};
    const auto second=validate(key,b);if(second!=Status::Ready)return {second};
    if(a.actor!=key.workers[0] || b.actor!=key.workers[1])return {Status::Stale};
    if(a.group==b.group && a.generation==b.generation)return {Status::Unsupported};
    if(b.banked>std::numeric_limits<std::int64_t>::max()-a.banked)return {Status::Overflow};
    return {Status::Ready,a.banked+b.banked,a.conditional || b.conditional};
}
// Arrays are canonical G,H order; each worker supplies its own two rows.
inline Decision combine(const Key& key,const std::array<Value,2>& a,const std::array<Value,2>& b)
{
    OwnMenu<> own_a{key,key.workers[0]},own_b{key,key.workers[1]};
    for(std::size_t i=0;i<2;++i)
    {
        if(a[i].group!=key.groups[i] || b[i].group!=key.groups[i])return {Status::Stale};
        const auto sa=own_a.put(a[i]);if(sa!=Status::Ready)return {sa};
        const auto sb=own_b.put(b[i]);if(sb!=Status::Ready)return {sb};
    }
    const auto da=own_a.answer(),db=own_b.answer();
    if(da.status!=Status::Ready)return {da.status};if(db.status!=Status::Ready)return {db.status};
    auto result=combine_regrets(key,da.difference,db.difference);if(result.status!=Status::Ready)return result;
    const auto left=score_disjoint(key,a[0],b[1]),right=score_disjoint(key,a[1],b[0]);
    if(left.status!=Status::Ready)return {left.status};if(right.status!=Status::Ready)return {right.status};
    result.has_absolute_score=true;result.banked=result.assignment==Assignment::A_G_B_H?left.banked:right.banked;
    return result;
}

// Signed 11-bit transport is a representation bound, not clipping or a value
// scale. Outputs are unchanged on failure; readiness/pair provenance travel in
// the caller's protocol and must still pass combine_regrets.
inline Status encode_signed11(std::int64_t delta,std::uint16_t& bits)
{
    if(delta<-1024 || delta>1023)return Status::Overflow;
    bits=static_cast<std::uint16_t>(delta<0?delta+2048:delta);return Status::Ready;
}
inline Status decode_signed11(std::uint16_t bits,std::int64_t& delta)
{
    if(bits>2047)return Status::Overflow;
    delta=bits>=1024?static_cast<std::int64_t>(bits)-2048:bits;return Status::Ready;
}
} // namespace tt::production_assignment
