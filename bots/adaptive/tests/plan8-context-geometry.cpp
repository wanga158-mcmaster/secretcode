// Pure geometry identity checks. Native Routes/formation staging is covered by
// the real caller; this fixture supplies no SDK observations or episode proof.
#include "../common/src/context_geometry.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
namespace {
using namespace p8;
using namespace p8::native;
int checks=0;
void check(bool condition,const char* message) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL context geometry: %s\n",message);std::exit(1);}
}
void route(Route& out,std::initializer_list<p6::Point> tiles) {
    out=Route{};out.count=int(tiles.size());out.verified=true;
    int next=0;for(const auto tile:tiles)out.tiles[next++]=tile;
}
void selected(Plan& p) {
    p.group=17;p.collector=90;p.altar=85;p.crew[0]=90;p.crew_count=1;p.pads[0]={2,2};
    p.forecast_rounds=73;p.remaining_rounds=120;p.capture_window=5;
    p.gem_bearing=p.complete_verified=p.rally_legal=true;
    route(p.ingress,{{0,0},{1,1},{2,2}});route(p.capture,{{2,2},{3,2}});
    route(p.bank,{{3,2},{4,2},{5,2}});route(p.renewal,{{5,2},{4,2},{3,2},{2,2}});
}
void normalize(Plan& to,const Plan& from) {
    to=from;route(to.ingress,{{2,2}});
}
struct Plans {Plan selected{},changed{},fixed{},other{};};
void defaults_and_routes() {
    ContextGeometry cache;
    check(!cache.frozen()&&!cache.complete()&&!cache.ready(),"default cache publishes no prepared identity");
    check(cache.site==-1&&cache.bank==-1&&cache.self==-1&&cache.source_group==-1&&cache.endpoint==-1,"default source intent is absent");
    check(!cache.fixed_hash&&!cache.return_hash&&cache.shape_count==0,"default public hashes are unavailable");
    for(const auto& shape:cache.supported) {
        check(!shape.valid&&!shape.hash&&shape.count==0,"default H description is not ready");
        for(const auto participant:shape.participants)check(participant==-1,"default H crew has no inferred member");
    }
    Route a,b;route(a,{{2,2},{3,2}});b=a;
    check(context_route_equal(a,b),"identical active route matches");
    b.tiles[127]={99,99};check(context_route_equal(a,b),"inactive tile payload cannot change identity");
    b.tiles[1]={3,3};check(!context_route_equal(a,b),"active tile change rejects match");
    b=a;b.blocker_assumptions=1;check(!context_route_equal(a,b),"blocker assumption change rejects match");
    b=a;b.verified=false;check(!context_route_equal(a,b),"verification gate change rejects match");
    b=a;b.count=1;check(!context_route_equal(a,b),"active count change rejects match");
    a.count=b.count=-1;check(!context_route_equal(a,b),"negative route counts reject before payload access");
    a.count=b.count=max_route_tiles+1;check(!context_route_equal(a,b),"overbound route counts reject without truncation");
    a.count=b.count=0;b.verified=true;b.blocker_assumptions=7;
    check(context_route_equal(a,b),"empty optional route has no active flags or payload");
    check(route_hash(a)==0&&route_hash(b)==0,"empty route equality supplies no verified route hash");
    route(a,{{2,2},{4,2}});b=a;
    check(context_route_equal(a,b)&&!route_valid(a)&&route_hash(a)==0,"equality alone cannot legalize malformed geometry");
}
void rosters() {
    common::DraftRoster a;
    a.picks=5;a.actors={90,50,10,70,30};
    a.champions={p6::Champion::tavi,p6::Champion::marcella,p6::Champion::varg,p6::Champion::vendetta,p6::Champion::prism};
    check(a.valid(),"fixture uses distinct actual IDs from the supported champion pool");
    auto b=a;check(context_roster_equal(a,b),"same public draft matches");
    b.actors[2]=11;check(!context_roster_equal(a,b),"changed named helper cannot match frozen membership");
    b=a;std::swap(b.actors[2],b.actors[3]);std::swap(b.champions[2],b.champions[3]);
    check(b.valid()&&!context_roster_equal(a,b),"actual helper3 rebase is distinct from helper2");
    auto c=a;std::swap(c.actors[2],c.actors[4]);std::swap(c.champions[2],c.champions[4]);
    check(c.valid()&&!context_roster_equal(b,c),"helper3 and helper4 descriptions stay distinct");
    b=a;b.champions[2]=p6::Champion::willow;check(!context_roster_equal(a,b),"champion identity change rejects frozen roster match");
    b=a;b.picks=4;check(!context_roster_equal(a,b),"team-size change rejects match");
    b=a;b.actors[2]=b.actors[0];
    check(!b.valid()&&context_roster_equal(b,b),"roster equality is not independent membership validation");
}
void hash_normalization() {
    auto p=std::make_unique<Plans>();selected(p->selected);normalize(p->fixed,p->selected);
    const auto fixed_hash=plan_route_hash(p->fixed),return_hash=plan_return_hash(p->fixed);
    check(fixed_hash&&return_hash,"normalized productive-entry geometry has both hashes");
    check(plan_route_hash(p->selected)!=fixed_hash,"first income ingress is distinct from repeatable productive entry");
    check(plan_return_hash(p->selected)==return_hash,"initial ingress cannot change exact bank and productive return identity");
    p->changed=p->selected;route(p->changed.ingress,{{1,0},{1,1},{2,2}});normalize(p->other,p->changed);
    check(plan_route_hash(p->other)==fixed_hash,"different first departure normalizes to same retained productive entry");
    check(context_route_equal(p->fixed.capture,p->changed.capture)&&context_route_equal(p->fixed.bank,p->changed.bank)&&
        context_route_equal(p->fixed.renewal,p->changed.renewal),"normalized match retains actual active capture and return payload");
    p->other=p->fixed;p->other.forecast_rounds=-777;p->other.remaining_rounds=-1;p->other.capture_window=999;
    p->other.gem_bearing=p->other.complete_verified=p->other.rally_legal=false;
    check(plan_route_hash(p->other)==fixed_hash&&plan_return_hash(p->other)==return_hash,"forecast timing and permission flags supply no geometric hash evidence");
    check(!plan_valid(p->other,90),"geometric identity alone cannot admit work or supply a forecast");
    p->other=p->fixed;p->other.capture.tiles[127]={98,99};p->other.renewal.tiles[127]={97,96};
    check(plan_route_hash(p->other)==fixed_hash,"inactive route cells cannot alter normalized context hash");
    p->other=p->fixed;p->other.group=18;
    check(plan_route_hash(p->other)!=fixed_hash&&plan_return_hash(p->other)!=return_hash,"different source cannot borrow either hash");
    p->other=p->fixed;p->other.bank.tiles[1]={4,3};
    check(plan_route_hash(p->other)!=fixed_hash&&plan_return_hash(p->other)!=return_hash,"changed bank path changes both identities");
    p->other=p->fixed;p->other.renewal.tiles[1]={4,3};
    check(plan_route_hash(p->other)!=fixed_hash&&plan_return_hash(p->other)!=return_hash,"changed productive return changes both identities");
    p->other=p->fixed;p->other.capture.blocker_assumptions=1;
    check(plan_route_hash(p->other)!=fixed_hash&&plan_return_hash(p->other)==return_hash,"capture assumption affects full route without relabeling return component");
    p->other=p->fixed;route(p->other.release,{{2,2},{1,2}});
    check(plan_route_hash(p->other)!=fixed_hash&&plan_return_hash(p->other)==return_hash,"actual release is bound to full route and not added to bank component");
    p->other=p->fixed;p->other.bank.count=max_route_tiles+1;
    check(plan_route_hash(p->other)==0&&plan_return_hash(p->other)==0,"malformed route cannot publish a usable context hash");
}
}
int main() {
    defaults_and_routes();rosters();hash_normalization();
    std::printf("PASS plan8 context geometry %d checks\n",checks);
}
