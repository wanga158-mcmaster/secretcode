// Actual static compiler and cash Builder; no SDK calls, native actions or
// fuel-safety claims. Forecast/body/Life resets below are deliberately external
// to the cash cache's complete static dependency key.
#include "../src/assignment_cash_cache.h"
#include <cstdio>
#include <memory>
#include <utility>

namespace {
namespace c=tt::assignment_cash_cache;
namespace r=tt::assignment_cash_routes;
namespace s=tt::assignment_static_geometry;
namespace a=tt::assignment_geometry;
int checks{},failures{};
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}

s::StaticSeed seed(std::uint64_t potential=16) {
    s::StaticSeed out;out.key={11,12,13,14,1,potential,false,true};
    out.complete_potential=out.terrain_includes_altars=out.require_quiet_home=true;
    auto& g=out.grid;g.width=7;g.height=6;g.exclusive_ownership=true;
    g.terrain.assign(42,0);g.camp_group.assign(42,-1);g.raw_upper.assign(42,0);
    g.altars={{6,5}};g.home_roots={{0,0}};g.terrain[41]=1;
    out.catalogue={{0,{{{3,3},100,10,1}}}};g.camp_group[24]=0;
    for(int y=0;y<6;++y)for(int x=0;x<7;++x)
        if(a::detail::separation({x,y},{3,3})<=1)g.raw_upper[static_cast<std::size_t>(y)*7+x]=10;
    return out;
}
std::shared_ptr<const s::StaticGeometry> geometry(std::uint64_t potential=16) {
    s::StaticBuilder b{seed(potential)};
    for(unsigned n=0;n<10000&&b.status()==a::Status::NeedCompute;++n)b.advance(11);
    check(b.status()==a::Status::Ready,"real sealed static input is complete");return b.result();
}
r::Goals goals(){return {{5,4},{0,0},{2,2},101,102,103};}
bool finish(c::Cache& cache) {
    for(unsigned n=0;n<10000&&cache.status()==r::Status::NeedCompute;++n)cache.advance(1);
    check(cache.status()==r::Status::Ready,"cache completes the actual bounded three-field Builder");
    return cache.status()==r::Status::Ready;
}

void same_dependencies_survive_short_forecasts() {
    const auto g=geometry();if(!g)return;
    c::Cache retained;
    check(retained.status()==r::Status::Unsupported&&retained.reason()==r::Reason::Shape&&
          retained.work_used()==0&&!retained.result(),"empty facade publishes no pretend route");
    retained.advance(0);check(retained.last_work()==0,"empty zero-unit call does no work");
    retained.ensure(g,goals());
    check(retained.status()==r::Status::NeedCompute&&retained.work_used()==0&&!retained.result(),
          "ensure owns only a cold Builder and never traverses or publishes");
    retained.advance(0);
    check(retained.status()==r::Status::NeedCompute&&retained.work_used()==0&&!retained.result(),
          "zero units do not publish a partial cash field");
    // With the old body-query lifetime, every window loses this real Builder
    // after one unit. The persistent facade gets exactly that same work slice.
    struct Forecast {unsigned body{},life{},date{};};
    unsigned windows{};
    for(;windows<10000&&retained.status()==r::Status::NeedCompute;++windows) {
        Forecast forecast{windows+1,windows%3,100+windows};
        check(forecast.body!=0&&forecast.date>=100,"forecast resets are real separate short lifetimes");
        r::Builder old_query{g,goals()};old_query.advance(1);
        check(old_query.work_used()==1&&old_query.status()==r::Status::NeedCompute&&!old_query.result(),
              "one-input current-query Builder never reaches Ready before its destruction");
        const auto before=retained.work_used();
        retained.ensure(g,goals());
        check(retained.work_used()==before&&retained.last_work()==0,
              "new body/Life/date forecast cannot erase matching cache progress");
        retained.advance(1);
        check(retained.work_used()==before+1&&retained.last_work()==1,
              "one admitted unit strictly advances the same actual cash Builder");
        if(retained.status()!=r::Status::Ready)check(!retained.result(),"unsealed memo remains unavailable");
    }
    check(windows>1&&retained.status()==r::Status::Ready,"retained work finishes across many shorter body/Life windows");
    const auto ready=retained.result();if(!ready)return;
    const auto work=retained.work_used();
    check(ready->key()==retained.key()&&ready->static_geometry()==g,"published object binds exact support and goals");
    for(unsigned n=0;n<20;++n) {
        retained.ensure(g,goals());retained.advance(9);
        check(retained.result()==ready&&retained.work_used()==work&&retained.last_work()==0,
              "Ready reuse retains the exact sealed object and consumes zero construction work");
    }
}

void every_real_dependency_replaces_without_retagging() {
    const auto g=geometry();if(!g)return;c::Cache cache;cache.ensure(g,goals());
    if(!finish(cache))return;
    auto pinned=cache.result();const auto old_key=pinned->key();
    const auto old_quote=pinned->price(r::Goal::Home,goals().bank,0);
    auto changed=goals();++changed.perch_id;
    cache.ensure(g,changed);
    check(cache.status()==r::Status::NeedCompute&&cache.work_used()==0&&!cache.result()&&
          cache.key().goals==changed,"changed goal identity starts a new exact builder");
    cache.advance(3);const auto partial=cache.work_used();cache.ensure(g,changed);
    check(cache.work_used()==partial,"same goal identity retains partial replacement progress");
    changed.perch={1,2};cache.ensure(g,changed);
    check(cache.work_used()==0&&cache.key().goals.perch==a::Point{1,2},"changed goal coordinate is not hidden by equal IDs");
    auto limits=r::Limits{};--limits.points;cache.ensure(g,changed,limits);
    check(cache.work_used()==0&&cache.limits().points==limits.points,"point limit is an exact cache dependency");
    cache.advance(2);--limits.cells;cache.ensure(g,changed,limits);
    check(cache.work_used()==0&&cache.limits().cells==limits.cells,"cell limit also replaces an unfinished builder");
    if(!finish(cache))return;
    const auto replacement=cache.result();
    check(replacement!=pinned&&replacement->key().goals==changed,"new Ready has its own immutable provenance");
    check(pinned->key()==old_key&&pinned->price(r::Goal::Home,goals().bank,0).raw_required==old_quote.raw_required,
          "replacement never retags or mutates a pinned old route");
    const auto equal_object=geometry();if(!equal_object)return;
    check(equal_object.get()!=g.get()&&equal_object->key()==g->key(),"fixture has distinct real objects with equal static key");
    cache.ensure(equal_object,changed,limits);
    check(cache.work_used()==0&&cache.key().object==equal_object.get()&&!cache.result(),
          "equal descriptive support cannot substitute a different static object");
    const auto changed_support=geometry(17);if(!changed_support)return;
    cache.ensure(changed_support,changed,limits);
    check(cache.key().support.potential==17&&cache.work_used()==0,"changed static support owns a fresh builder");
}

void typed_failures_reset_and_pinned_lifetime() {
    auto g=geometry();if(!g)return;c::Cache cache;auto limit=r::Limits{};limit.cells=g->size()-1;
    cache.ensure(g,goals(),limit);
    check(cache.status()==r::Status::Unsupported&&cache.reason()==r::Reason::Capacity&&!cache.result(),
          "capacity failure is typed and makes no group absence verdict");
    cache.ensure(g,goals(),limit);cache.advance(100);
    check(cache.status()==r::Status::Unsupported&&cache.reason()==r::Reason::Capacity&&
          cache.last_work()==0&&cache.work_used()==0,"same failed dependency is not a restart loop");
    cache.ensure({},goals());
    check(cache.status()==r::Status::Unsupported&&cache.reason()==r::Reason::Shape&&cache.key().object==nullptr,
          "missing sealed support is typed rather than dereferenced or fabricated");
    auto invalid=goals();invalid.home_id=0;cache.ensure(g,invalid);
    check(cache.status()==r::Status::Unsupported&&cache.reason()==r::Reason::Goal,"exact invalid goal IDs retain Builder semantics");
    cache.ensure(g,goals());if(!finish(cache))return;
    auto pinned=cache.result();const auto key=pinned->key();
    std::weak_ptr<const s::StaticGeometry> life=g;g.reset();cache.reset();
    check(cache.status()==r::Status::Unsupported&&cache.reason()==r::Reason::Shape&&
          cache.key().object==nullptr&&cache.work_used()==0&&!cache.result(),"explicit world handoff clears only facade ownership");
    check(!life.expired()&&pinned->key()==key&&pinned->static_geometry().get()==key.object&&
          pinned->price(r::Goal::Home,goals().bank,0).complete(),"pinned old Ready remains immutable and alive after reset");
    pinned.reset();check(life.expired(),"cache and routes form no lifetime cycle");
    auto fresh=geometry();if(!fresh)return;cache.ensure(fresh,goals());
    check(cache.work_used()==0&&cache.status()==r::Status::NeedCompute,"explicit handoff can begin a new world lifetime");
}
}
int main() {
    same_dependencies_survive_short_forecasts();every_real_dependency_replaces_without_retagging();
    typed_failures_reset_and_pinned_lifetime();
    std::printf("assignment cash cache (pure, no SDK actions): %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
