#include "production_assignment.h"
#include <cstdio>
#include <limits>

namespace {
namespace pa=tt::production_assignment;
int checks{},failures{};
void check(bool good,const char* message)
{++checks;if(!good){++failures;std::printf("FAIL %s\n",message);}}
pa::Key key()
{return {7,0,30,{10,20},{100,200},{3,9}};}
pa::Value value(const pa::Key& k,std::size_t worker,std::size_t group,std::int64_t banked)
{
    return {k,k.workers[worker],k.groups[group],k.generations[group],11+worker,
        20+group,30+group,40+group,banked,true,true,true,true};
}
std::array<pa::Value,2> values(const pa::Key& k,std::size_t worker,std::int64_t g,std::int64_t h)
{return {value(k,worker,0,g),value(k,worker,1,h)};}
void relative_assignment_and_terminal_values()
{
    const auto k=key();
    // These are each worker's OWN already-completed quotes through common H30:
    // A: G22 banked by20, H11 by12. B: G22 by21, H0 throughH. The assignment
    // module does not infer those completion dates or produce another actor.
    const auto a=values(k,0,22,11),b=values(k,1,22,0);
    const auto selected=pa::combine(k,a,b);
    check(selected.status==pa::Status::Ready && selected.assignment==pa::Assignment::A_H_B_G &&
        selected.groups==std::array<std::uint64_t,2>{200,100} &&
        selected.generations==std::array<std::uint64_t,2>{9,3} && selected.has_absolute_score && selected.banked==33,
        "relative worker advantage assigns H to A and G to B instead of both preferring G");
    pa::OwnMenu<> own_a{k,10},own_b{k,20};
    for(const auto& quote:a)own_a.put(quote);for(const auto& quote:b)own_b.put(quote);
    const auto da=own_a.answer(),db=own_b.answer();
    check(da.status==pa::Status::Ready && db.status==pa::Status::Ready &&
        da.difference.delta==11 && db.difference.delta==22,
        "each worker computes its own signed canonical G minus H value difference");
    const auto relative=pa::combine_regrets(k,da.difference,db.difference);
    check(relative.status==pa::Status::Ready && relative.assignment==selected.assignment &&
        !relative.has_absolute_score && relative.conditional,
        "signed differences choose the allocation without manufacturing an absolute team score");
    const auto custody=pa::combine(k,values(k,0,11,10),values(k,1,22,1));
    check(custody.status==pa::Status::Ready && custody.assignment==pa::Assignment::A_H_B_G && custody.banked==32,
        "different terminal custody values for the same physical group remain worker-specific integers");
    auto carried_only=values(k,0,0,11); // the producer banked zero from G byH, even if it could carry22
    const auto no_carried_score=pa::combine(k,carried_only,b);
    check(no_carried_score.status==pa::Status::Ready && no_carried_score.banked==33,
        "a zero terminal banked value is never promoted from carried resource or intrinsic group reward");
    const auto tie=pa::combine(k,values(k,0,22,11),values(k,1,22,11));
    check(tie.status==pa::Status::Ready && tie.assignment==pa::Assignment::A_G_B_H && tie.banked==33,
        "economic ties use canonical worker A to group G deterministically");
    auto resolved_a=a,resolved_b=b;
    for(auto& quote:resolved_a)quote.conditional=false;for(auto& quote:resolved_b)quote.conditional=false;
    check(pa::combine(k,resolved_a,resolved_b).status==pa::Status::Ready &&
        !pa::combine(k,resolved_a,resolved_b).conditional,
        "conditional is informational and fully resolved current premises remain admissible");
}
void own_cache_requires_both_ready_quotes()
{
    const auto k=key();pa::OwnMenu<> menu{k,10};const auto a=values(k,0,22,11);
    check(menu.answer().status==pa::Status::Unknown,"a fresh peer request does not start negotiation or job computation");
    menu.put(a[0]);
    check(menu.answer().status==pa::Status::Unknown && menu.cached(100)->banked==22 && !menu.cached(200),
        "one existing ready quote cannot answer a two-group difference");
    auto unfinished=a[1];unfinished.complete=false;menu.put(unfinished);
    check(menu.answer().status==pa::Status::Unknown,"an incomplete second quote is cached but never answered as ready");
    unfinished.complete=true;unfinished.supported=false;menu.put(unfinished);
    check(menu.answer().status==pa::Status::Unsupported,"a completed unsupported operator is explicit, not a zero-valued job");
    unfinished.supported=true;unfinished.disjoint_supported=false;menu.put(unfinished);
    check(menu.answer().status==pa::Status::Unsupported,"unsupported team interaction cannot borrow a disjoint assignment assumption");
    menu.put(a[1]);check(menu.answer().status==pa::Status::Ready,"both existing completed own quotes answer without any producer callback");
    auto foreign=a[0];foreign.actor=20;
    check(menu.put(foreign)==pa::Status::Unsupported && menu.answer().status==pa::Status::Ready,
        "an own cache cannot be populated by pretending to simulate the foreign worker");
    auto mismatch=a[1];++mismatch.body_revision;menu.put(mismatch);
    check(menu.answer().status==pa::Status::Stale,"two groups must be quoted from the same own body revision");
    auto updated=a[0];++updated.body_revision;menu.put(updated);
    check(menu.answer().status==pa::Status::Ready && menu.answer().difference.life_revisions[0]!=
        menu.answer().difference.life_revisions[1],
        "matching own body revisions permit naturally different per-group lifecycle and geometry revisions");
}
void stale_keys_and_provenance()
{
    const auto k=key();const auto a=values(k,0,22,11),b=values(k,1,22,0);pa::OwnMenu<> menu{k,10};
    auto wrong=a[0];++wrong.key.horizon;
    check(menu.put(wrong)==pa::Status::Stale,"a different common horizon cannot enter the active own cache");
    wrong=a[0];++wrong.generation;
    check(menu.put(wrong)==pa::Status::Stale,"group generation must match the canonical pair's physical generation");
    wrong=a[0];wrong.group=999;
    check(menu.put(wrong)==pa::Status::Stale,"an unrelated group is not an alternative in this bounded pair");
    wrong=a[0];wrong.banked=-1;
    check(menu.put(wrong)==pa::Status::Unsupported,"negative terminal banked values are not representable jobs");
    for(const auto& quote:a)menu.put(quote);
    pa::OwnMenu<> peer{k,20};for(const auto& quote:b)peer.put(quote);
    auto da=menu.answer().difference,db=peer.answer().difference;
    ++db.key.pair_id;
    check(pa::combine_regrets(k,da,db).status==pa::Status::Stale,"wire replies from another pair publication are rejected");
    db=peer.answer().difference;++db.key.start;
    check(pa::combine_regrets(k,da,db).status==pa::Status::Stale,"start and horizon are a common calendar, not per-worker service durations");
    db=peer.answer().difference;db.actor=10;
    check(pa::combine_regrets(k,da,db).status==pa::Status::Stale,"two reports from one worker do not constitute two worker quotes");
    auto unordered=k;unordered.workers={20,10};
    check(!pa::canonical(unordered) && pa::combine(unordered,a,b).status!=pa::Status::Ready,
        "canonical worker and group ordering is explicit rather than guessed by the receiver");
    auto past=k;past.start=past.horizon+1;
    check(!pa::canonical(past),"a terminal horizon before common start is invalid");
    auto zero=k;zero.workers={0,1};zero.groups={0,1};zero.generations={0,0};zero.start=zero.horizon;
    check(pa::canonical(zero),"identity zero, first generation zero and a zero-length terminal calendar remain representable");
}
void semantic_refutation_retains_alternatives()
{
    const auto k=key();const auto a=values(k,0,22,11);pa::OwnMenu<> menu{k,10};
    for(const auto& quote:a)menu.put(quote);
    check(menu.refute(100)==pa::Status::Ready && menu.answer().status==pa::Status::Refuted && menu.cached(200)->banked==11,
        "refuting one group/operator leaves the other cached family intact instead of banning or clearing the pair");
    auto request=k;++request.pair_id;
    menu.bind(request,10);
    check(menu.answer().status==pa::Status::Refuted && menu.cached(200)->key==request,
        "a new request id reuses ready own values and cannot erase unchanged semantic rejection");
    auto other_changed=request;++other_changed.generations[1];menu.bind(other_changed,10);
    check(menu.cached(100) && !menu.cached(200),
        "changing H generation preserves the existing G quote and invalidates only H");
    menu.put(value(other_changed,0,1,11));
    check(menu.answer().status==pa::Status::Refuted,
        "changing the OTHER group's generation cannot discharge unchanged G semantic rejection");
    auto same=*menu.cached(100);same.banked=100;same.conditional=false;menu.put(same);
    check(menu.answer().status==pa::Status::Refuted,
        "changing claimed value or conditional flag does not erase a physical/operator refutation");
    ++same.word_revision;menu.put(same);
    check(menu.answer().status==pa::Status::Ready,"a changed operator word reopens only that group's quote");
    for(unsigned dependency=0;dependency<3;++dependency)
    {
        pa::OwnMenu<> fresh{k,10};for(const auto& quote:a)fresh.put(quote);fresh.refute(100);
        auto changed=a[0];
        if(dependency==0){++changed.body_revision;auto h=a[1];++h.body_revision;fresh.put(h);}
        if(dependency==1)++changed.life_revision;
        if(dependency==2)++changed.geometry_revision;
        fresh.put(changed);
        check(fresh.answer().status==pa::Status::Ready,"changed relevant body/lifecycle/geometry semantics can reopen a rejected word");
    }
    pa::OwnMenu<1> bounded{k,10};for(const auto& quote:a)bounded.put(quote);
    bounded.refute(100);
    check(bounded.refute(200)==pa::Status::Overflow && bounded.answer().status==pa::Status::Overflow,
        "refutation capacity is an explicit representation limit, never an empty or zero-value answer");
    bounded.bind(request,10);
    check(bounded.answer().status==pa::Status::Overflow,"renaming the same semantic pair cannot hide exhausted representation capacity");
    pa::OwnMenu<> renewal{k,10};for(const auto& quote:a)renewal.put(quote);renewal.refute(100);
    auto renewed=k;++renewed.generations[0];renewal.bind(renewed,10);renewal.put(value(renewed,0,0,22));
    check(renewal.answer().status==pa::Status::Ready && renewal.cached(200),
        "actual renewed G generation retires G's old failure without clearing H's existing quote");
    auto later=k;++later.horizon;bounded.bind(later,10);
    const auto next=values(later,0,22,11);for(const auto& quote:next)bounded.put(quote);
    check(bounded.answer().status==pa::Status::Overflow,
        "a new terminal calendar cannot hide unretained physical failure evidence after capacity overflow");
    pa::OwnMenu<1> dated{k,10};for(const auto& quote:a)dated.put(quote);
    dated.refute(100,pa::RefutationKind::Calendar);dated.refute(200,pa::RefutationKind::Calendar);
    dated.bind(later,10);for(const auto& quote:next)dated.put(quote);
    check(dated.answer().status==pa::Status::Ready,
        "only calendar-scoped capacity and expired calendar failures may clear on a changed start or horizon");
}
void physical_and_calendar_refutations_are_distinct()
{
    const auto k=key();auto a=values(k,0,22,11);
    // Actual Respawning0 is a producer/admission fact represented by revision77;
    // this module neither derives it from a timer nor treats a prediction as it.
    a[0].life_revision=77;a[0].enabler=3;
    pa::OwnMenu<> physical{k,10};for(const auto& quote:a)physical.put(quote);
    check(physical.refute(100)==pa::Status::Ready && physical.answer().status==pa::Status::Refuted,
        "an actual Respawning0 admission/perch failure is physically scoped by default");
    auto later=k;++later.pair_id;later.start=5;later.horizon=35;later.workers[1]=30;
    physical.bind(later,10);
    check(physical.answer().status==pa::Status::Unknown,
        "new calendar values need their own ready quotes rather than reusing old terminal totals");
    auto refreshed=a;for(auto& quote:refreshed)quote.key=later;
    for(const auto& quote:refreshed)physical.put(quote);
    check(physical.answer().status==pa::Status::Refuted && physical.cached(200)->banked==11,
        "new start horizon proposal and peer cannot reopen unchanged physical G while H remains an alternate");
    auto changed=refreshed[0];++changed.life_revision;physical.put(changed);
    check(physical.answer().status==pa::Status::Ready,
        "actual changed lifecycle revision reopens G under the new calendar");
    changed=refreshed[0];++changed.word_revision;physical.put(changed);
    check(physical.answer().status==pa::Status::Ready,
        "a genuinely changed operator word reopens that word without clock-based physical amnesia");
    changed=refreshed[0];++changed.enabler;physical.put(changed);
    check(physical.answer().status==pa::Status::Ready && physical.answer().difference.enablers[0]==changed.enabler,
        "a changed named enabler is an explicit semantic dependency and survives into the difference provenance");
    auto other_pair=later;other_pair.groups={200,300};other_pair.generations={9,12};
    physical.bind(other_pair,10);physical.bind(later,10);physical.put(refreshed[0]);
    check(physical.answer().status==pa::Status::Refuted && physical.cached(200)->banked==11,
        "temporarily naming another group pair does not erase a physical failure when G is nominated again");
    auto invalid=later;invalid.horizon=invalid.start-1;
    check(physical.bind(invalid,10)==pa::Status::Unknown,"invalid proposal metadata stays explicit Unknown");
    physical.bind(later,10);
    check(physical.answer().status==pa::Status::Refuted,
        "an invalid proposal cannot be used to erase retained physical evidence");
    pa::OwnMenu<> calendar{k,10};for(const auto& quote:a)calendar.put(quote);
    calendar.refute(100,pa::RefutationKind::Calendar);
    check(calendar.answer().status==pa::Status::Refuted,"deadline or NoProfit failures may be scoped to their actual calendar");
    calendar.bind(later,10);for(const auto& quote:refreshed)calendar.put(quote);
    check(calendar.answer().status==pa::Status::Ready,
        "a new calendar can reopen an unchanged physical operator that was merely unprofitable by an old deadline");
    auto zero=refreshed[0];zero.banked=0;zero.conditional=true;calendar.put(zero);
    check(calendar.answer().status==pa::Status::Ready,
        "a zero-valued conditional full-generation prediction does not itself manufacture physical rejection");
}
void disjointness_arithmetic_and_wire_limits()
{
    const auto k=key();const auto g_a=value(k,0,0,22),g_b=value(k,1,0,22);
    check(pa::score_disjoint(k,g_a,g_b).status==pa::Status::Unsupported,
        "two workers cannot consume the same physical group generation twice");
    auto h_b=value(k,1,1,11);h_b.generation=k.generations[0];
    check(pa::score_disjoint(k,g_a,h_b).status==pa::Status::Stale,"a renamed generation cannot launder group identity");
    const auto max=std::numeric_limits<std::int64_t>::max();
    check(pa::score_disjoint(k,value(k,0,0,max),value(k,1,1,1)).status==pa::Status::Overflow &&
        pa::combine(k,values(k,0,max,0),values(k,1,0,max)).status==pa::Status::Overflow,
        "absolute terminal score addition refuses int64 overflow");
    pa::OwnMenu<> own_a{k,10},own_b{k,20};
    for(const auto& quote:values(k,0,max,0))own_a.put(quote);
    for(const auto& quote:values(k,1,0,max))own_b.put(quote);
    const auto compared=pa::combine_regrets(k,own_a.answer().difference,own_b.answer().difference);
    check(compared.status==pa::Status::Ready && compared.assignment==pa::Assignment::A_G_B_H && !compared.has_absolute_score,
        "difference comparison needs no overflowing delta subtraction or fabricated absolute total");
    auto impossible=own_a.answer().difference;impossible.delta=std::numeric_limits<std::int64_t>::min();
    check(pa::combine_regrets(k,impossible,own_b.answer().difference).status==pa::Status::Overflow,
        "an impossible difference of nonnegative int64 terminal scores is rejected");
    bool roundtrip=true;
    for(std::int64_t original=-1024;original<=1023;++original)
    {
        std::uint16_t bits=4095;std::int64_t decoded=99999;
        roundtrip&=pa::encode_signed11(original,bits)==pa::Status::Ready && bits<=2047 &&
            pa::decode_signed11(bits,decoded)==pa::Status::Ready && decoded==original;
    }
    check(roundtrip,"all signed 11-bit representable differences roundtrip without clipping or scaling");
    std::uint16_t unchanged=77;std::int64_t delta=88;
    check(pa::encode_signed11(-1025,unchanged)==pa::Status::Overflow && unchanged==77 &&
        pa::encode_signed11(1024,unchanged)==pa::Status::Overflow && unchanged==77 &&
        pa::decode_signed11(2048,delta)==pa::Status::Overflow && delta==88,
        "capped protocol messages refuse unrepresentable values and preserve caller outputs on failure");
}
}
int main()
{
    relative_assignment_and_terminal_values();own_cache_requires_both_ready_quotes();stale_keys_and_provenance();
    semantic_refutation_retains_alternatives();physical_and_calendar_refutations_are_distinct();
    disjointness_arithmetic_and_wire_limits();
    std::printf("production assignment: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
