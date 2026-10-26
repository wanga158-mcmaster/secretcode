// SDK-free geometry/price fixture. Every source is permanently represented by
// its full raw upper; no sleeping, cleared camp, shield or old paid prefix is
// discounted. Native command legality, body modifiers and fuel remain unproved.
#include "../src/assignment_cash_routes.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

namespace {
namespace r=tt::assignment_cash_routes;
namespace s=tt::assignment_static_geometry;
namespace a=tt::assignment_geometry;
int checks{},failures{};
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
s::StaticSeed seed() {
    s::StaticSeed out;out.key={11,12,13,14,1,16,false,true};
    out.complete_potential=out.terrain_includes_altars=out.require_quiet_home=true;
    auto& grid=out.grid;grid.width=9;grid.height=7;grid.exclusive_ownership=true;
    grid.terrain.assign(63,0);grid.camp_group.assign(63,-1);grid.raw_upper.assign(63,0);
    grid.altars={{8,6}};grid.home_roots={{0,0}};
    for(const auto p:{a::Point{2,1},a::Point{2,2},a::Point{2,3},a::Point{6,2},a::Point{8,6}})
        grid.terrain[static_cast<std::size_t>(p.y)*9+p.x]=1;
    out.catalogue={{0,{{{4,3},100,10,3}}}};grid.camp_group[3*9+4]=0;
    for(int y=0;y<7;++y)for(int x=0;x<9;++x)
        if(a::detail::separation({x,y},{4,3})<=3)grid.raw_upper[static_cast<std::size_t>(y)*9+x]=10;
    return out;
}
std::shared_ptr<const s::StaticGeometry> geometry(s::StaticSeed input=seed()) {
    s::StaticBuilder b{std::move(input)};
    for(unsigned n=0;n<100000&&b.status()==a::Status::NeedCompute;++n)b.advance(7);
    check(b.status()==a::Status::Ready,"actual blocked-altar/full-source geometry seals");return b.result();
}
r::Goals goals(){return {{7,5},{0,0},{3,3},31,32,33};}
std::shared_ptr<const r::Routes> routes(const std::shared_ptr<const s::StaticGeometry>& g,r::Goals root=goals(),r::Limits limits={}) {
    r::Builder b{g,root,limits};
    check(b.status()==r::Status::NeedCompute&&b.work_used()==0&&!b.result(),
        "route constructor allocates/traverses no field or memo and publishes nothing");
    b.advance(0);check(b.last_work()==0&&b.work_used()==0&&!b.result(),"zero admitted work cannot publish a partial route");
    for(unsigned n=0;n<100000&&b.status()==r::Status::NeedCompute;++n) {
        const auto work=b.work_used();b.advance(1);
        check(b.last_work()==1&&b.work_used()==work+1,"one unit admits one reserve/cell/parent-tree search unit");
        if(b.status()!=r::Status::Ready)check(!b.result(),"all three fields and suffix sums stay behind Ready");
    }
    check(b.status()==r::Status::Ready,"finite route construction seals all three goals and suffix memos");return b.result();
}
std::uint32_t oracle_distance(const s::StaticGeometry& g,a::Point from,a::Point target) {
    if(!g.closed(g.index(from))||!g.closed(g.index(target)))return a::detail::none;
    std::vector<std::uint32_t> distance(g.size(),a::detail::none);std::queue<a::Point> q;
    distance[g.index(from)]=0;q.push(from);
    while(!q.empty()) {
        const auto p=q.front();q.pop();if(p==target)return distance[g.index(p)];
        for(const auto d:a::detail::directions) {
            const a::Point to{p.x+d.x,p.y+d.y};const auto i=g.index(to);
            if(!g.inside(to)||!g.closed(i)||distance[i]!=a::detail::none)continue;
            distance[i]=distance[g.index(p)]+1;q.push(to);
        }
    }
    return a::detail::none;
}
struct Walk {double sum{},maximum{};std::uint32_t edges{};};
Walk explicit_walk(const r::Routes& routes,r::Goal goal,a::Point origin) {
    const auto& g=*routes.static_geometry();const auto endpoint=r::detail::point(routes.goals(),goal);
    Walk out;out.maximum=*g.raw_upper(origin);
    for(unsigned unit=0;origin!=endpoint&&unit<10000;++unit) {
        const auto to=routes.next(goal,origin);check(to.has_value(),"every quoted forward step has an explicit parent edge");
        if(!to)break;
        check(g.closed(g.index(*to))&&a::detail::separation(origin,*to)==1,"future steps remain adjacent and exclude every NPC spawn/wall/altar");
        out.sum+=*g.raw_upper(*to);out.maximum=std::max(out.maximum,*g.raw_upper(*to));origin=*to;++out.edges;
    }
    check(origin==endpoint,"explicit parent walk reaches its exact immutable goal");return out;
}
void all_cells_match_explicit_paths() {
    const auto g=geometry();if(!g)return;const auto value=routes(g);if(!value)return;
    for(const auto goal:{r::Goal::Bank,r::Goal::Home,r::Goal::Perch})for(int y=0;y<g->grid().height;++y)for(int x=0;x<g->grid().width;++x) {
        const a::Point origin{x,y};if(!g->closed(g->index(origin)))continue;
        const auto expected=oracle_distance(*g,origin,r::detail::point(value->goals(),goal));
        const auto distance=value->distance(goal,origin);
        check((expected==a::detail::none)==!distance,"all reachable closed origins match an independent BFS");
        if(!distance)continue;
        check(*distance==expected,"sealed distance is shortest in the declared closed graph");
        const auto quote=value->price(goal,origin,3);check(quote.complete(),"complete memos price every supported origin without a map scan");
        if(!quote.complete())continue;
        const auto forward=explicit_walk(*value,goal,origin);
        const auto home=goal==r::Goal::Home?Walk{}:explicit_walk(*value,r::Goal::Home,r::detail::point(value->goals(),goal));
        const double channel=goal==r::Goal::Bank?30:0;
        const double maximum=std::max(forward.maximum,home.maximum);
        const double required=forward.sum+channel+home.sum+3*maximum;
        check(quote.forward_edges==forward.edges&&quote.home_edges==home.edges&&
            quote.points==static_cast<std::uint64_t>(forward.edges)+home.edges+1,"complete forward/home geometry is represented in the quote");
        check(quote.raw_forward>=forward.sum&&quote.raw_forward<forward.sum+1e-6&&
            quote.raw_home>=home.sum&&quote.raw_home<home.sum+1e-6,"memo charges equal explicit destination sums with upward rounding");
        check(quote.raw_required>=required&&quote.raw_required<required+1e-6&&quote.raw_max==maximum,
            "raw debt includes remaining channel, full home footer, two waits and one refusal");
        check(!quote.survives(quote.raw_required)&&quote.survives(std::nextafter(quote.raw_required,std::numeric_limits<double>::infinity())),
            "current HP must strictly exceed the complete remaining raw requirement");
    }
}
void current_origin_and_channel_suffix() {
    const auto g=geometry();if(!g)return;const auto value=routes(g);if(!value)return;
    const auto from_home=value->price(r::Goal::Bank,goals().home,3);
    const auto at_bank=value->price(r::Goal::Bank,goals().bank,3);
    check(at_bank.complete()&&at_bank.forward_edges==0&&at_bank.raw_forward==0&&at_bank.raw_channel>=30,
        "an actual current bank pose prices only the remaining channel and home suffix");
    check(!at_bank.survives(25),"25 HP cannot survive three remaining raw10 channel inputs even before paid exit");
    check(at_bank.raw_required<from_home.raw_required,"already incurred ingress is not charged again from the bank pose");
    const auto less=value->price(r::Goal::Bank,goals().bank,1);
    check(at_bank.raw_channel-less.raw_channel>=20-1e-6&&at_bank.raw_required-less.raw_required>=20-1e-6,
        "each fresh remaining-channel read changes only its future suffix");
    const auto next=value->next(r::Goal::Bank,goals().home);check(next.has_value(),"current-origin test has a forward edge");
    if(next) {
        const auto after=value->price(r::Goal::Bank,*next,3);
        check(after.raw_forward+*g->raw_upper(*next)>=from_home.raw_forward-1e-6&&
            after.raw_forward+*g->raw_upper(*next)<from_home.raw_forward+1e-6,
            "new current HP/pose never inherits a historical paid-prefix debt");
    }
    const auto perch=value->price(r::Goal::Perch,goals().perch,99);
    check(perch.complete()&&perch.raw_forward==0&&perch.raw_home>0&&perch.home_edges>0&&perch.raw_max>=10,
        "positive work perch retains its complete paid home reverse and ignores channel N");
    const auto home0=value->price(r::Goal::Home,goals().bank,0),home99=value->price(r::Goal::Home,goals().bank,99);
    check(home0.raw_required==home99.raw_required&&home99.raw_channel==0,"home return does not invent another bank channel");
    check(!perch.survives(std::numeric_limits<double>::infinity())&&!perch.survives(std::numeric_limits<double>::quiet_NaN()),
        "nonfinite HP is never a paid-current-body proof");
}
void spawn_origin_exception_and_representational_limits() {
    const auto g=geometry();if(!g)return;const auto value=routes(g);if(!value)return;
    const a::Point camp{4,3};const auto next=value->next(r::Goal::Home,camp);const auto q=value->price(r::Goal::Home,camp,0);
    check(next&&g->closed(g->index(*next))&&a::detail::separation(camp,*next)==1&&q.complete(),
        "a real catalogue spawn nominates only one adjacent closed initial exit");
    if(next)check(value->distance(r::Goal::Home,camp)==std::optional<std::uint32_t>{*value->distance(r::Goal::Home,*next)+1},
        "the spawn-origin exception adds exactly one ordinary edge");
    check(q.raw_max>=*g->raw_upper(camp)&&q.raw_reserve>=3*(*g->raw_upper(camp)),
        "NPC-origin exception retains every raw source for future refusal/wait support");
    for(const auto bad:{a::Point{2,2},a::Point{6,2},a::Point{8,6},a::Point{-1,0},a::Point{9,0}}) {
        check(!value->distance(r::Goal::Home,bad)&&!value->next(r::Goal::Home,bad)&&
            value->price(r::Goal::Home,bad,0).status==r::Status::Unsupported,
            "an arbitrary wall, hole, altar or outside origin cannot borrow the camp exit exception");
    }
    auto cap=r::Limits{};cap.points=1;const auto tiny=value->price(r::Goal::Bank,goals().home,3,cap);
    check(tiny.status==r::Status::Unsupported&&tiny.reason==r::Reason::Capacity&&!tiny.survives(1e9),
        "point capacity is explicit Unsupported rather than global unreachable or free partial exit");
    auto isolated=seed();for(const auto d:a::detail::directions)isolated.grid.terrain[static_cast<std::size_t>(3+d.y)*9+4+d.x]=1;
    const auto split=geometry(std::move(isolated));if(!split)return;
    auto alternative=goals();alternative.perch={0,2};const auto blocked=routes(split,alternative);if(!blocked)return;
    check(blocked->price(r::Goal::Home,camp,0).status==r::Status::Unreachable,
        "a surrounded actual camp origin cannot skip its missing adjacent closed first edge");
}
void identities_lifetimes_and_invalid_goals() {
    auto g=geometry();if(!g)return;const auto value=routes(g);if(!value)return;
    auto other=geometry();if(!other)return;
    r::Builder different{other,goals()};
    check(value->price(different.key(),r::Goal::Bank,goals().home,3).status==r::Status::Stale,
        "equal semantic keys on a different static object never silently rebind a pinned route");
    auto changed=value->key();++changed.goals.perch_id;
    check(value->price(changed,r::Goal::Perch,goals().home,0).status==r::Status::Stale,
        "goal identity is part of the complete route key");
    r::Builder partial{g,goals()};partial.advance(3);const auto work=partial.work_used();auto key=partial.key();++key.support.potential;
    check(partial.advance(key,7)==r::Status::Stale&&partial.last_work()==0&&partial.work_used()==work&&!partial.result(),
        "changed support withdraws a partial field before any mixed work or publication");
    auto bad=goals();bad.home={3,3};r::Builder hazardous{g,bad};
    check(hazardous.status()==r::Status::Unsupported,"a positive/ordinary nonspawn home cannot provide free healing");
    bad=goals();bad.home={0,5};r::Builder wrong_home{g,bad};
    check(wrong_home.status()==r::Status::Unsupported,"a quiet ordinary tile is not an owned spawn healing root");
    bad=goals();bad.bank={0,0};r::Builder wrong_bank{g,bad};
    check(wrong_bank.status()==r::Status::Unsupported,"a nonaltar stand cannot acquire bank semantics");
    auto small=r::Limits{};small.cells=62;r::Builder too_small{g,goals(),small};
    check(too_small.status()==r::Status::Unsupported&&too_small.reason()==r::Reason::Capacity,"map capacity rejects before allocation");
    std::weak_ptr<const s::StaticGeometry> life; r::Quote quote;
    {auto owned=geometry();life=owned;auto route=routes(owned);quote=route->price(r::Goal::Home,goals().bank,0);}
    check(!life.expired()&&quote.support_owner.get()==quote.key.object&&quote.complete(),"quote pins the exact immutable static object even after builder/routes destruction");
    quote={};check(life.expired(),"route quote ownership has no cycle");
}
}
int main() {
    all_cells_match_explicit_paths();current_origin_and_channel_suffix();spawn_origin_exception_and_representational_limits();
    identities_lifetimes_and_invalid_goals();
    std::printf("assignment cash routes (pure, no SDK actions): %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
