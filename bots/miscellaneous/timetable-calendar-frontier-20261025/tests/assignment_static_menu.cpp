#include "../src/assignment_static_menu.h"
#include <cstdio>
#include <limits>
#include <memory>
#include <type_traits>

namespace {
namespace m=tt::assignment_static_menu;
namespace s=tt::assignment_static_geometry;
namespace g=tt::assignment_geometry;
namespace p=tt::production_job;
static_assert(!std::is_convertible_v<g::Status,m::CompletedVerdict>);
int checks{},failures{};
void check(bool good,const char* message) {
    ++checks;if(!good){++failures;std::printf("FAIL %s\n",message);}
}
g::Stamp stamp(g::Point origin={1,2}) {return {12,16,23,24,5,origin};}
s::StaticSeed seed(bool isolated=false) {
    s::StaticSeed out;out.key={11,12,13,14,1,16,true};out.complete_potential=true;
    auto& grid=out.grid;grid.width=9;grid.height=7;grid.exclusive_ownership=grid.require_quiet_bank=true;
    grid.terrain.assign(63,0);grid.camp_group.assign(63,-1);grid.raw_upper.assign(63,0);
    grid.altars={{0,6}};grid.home_roots={{0,0}};
    out.catalogue={{0,{{{3,2},100,2,1}}},{1,{{{6,4},200,4,1},{{8,4},200,4,1}}}};
    if(isolated)for(const auto point:std::array<g::Point,7>{{{1,0},{2,0},{3,0},{1,1},{3,1},{1,2},{2,2}}})
        grid.terrain[static_cast<std::size_t>(point.y)*9+point.x]=1;
    for(const auto& group:out.catalogue)for(const auto& member:group.members) {
        grid.camp_group[static_cast<std::size_t>(member.pos.y)*9+member.pos.x]=group.canonical;
        for(int y=0;y<7;++y)for(int x=0;x<9;++x)
            if(g::detail::separation({x,y},member.pos)<=member.range)
                grid.raw_upper[static_cast<std::size_t>(y)*9+x]+=member.strength;
    }
    return out;
}
std::shared_ptr<const s::StaticGeometry> geometry(bool isolated=false,std::uint64_t layout=12) {
    auto input=seed(isolated);input.key.layout=layout;
    s::StaticBuilder builder{std::move(input)};
    for(unsigned i=0;i<100000&&builder.status()==g::Status::NeedCompute;++i)builder.advance(13);
    check(builder.status()==g::Status::Ready,"synthetic fixed catalogue seals before menu construction");
    return builder.result();
}
p::Snapshot self(bool all=true) {
    p::Snapshot out;out.actor=7;out.body_revision=8;out.epoch=5;
    out.body.pos={1,2};out.body.hp=70;out.body.max_hp=100;out.body.attack_range=1;
    if(all) {
        out.spells[0].kind=mb::service::SpellKind::damage;out.spells[0].damage=15;out.spells[0].range=3;
        out.spells[1].kind=mb::service::SpellKind::aoe;out.spells[1].damage=20;out.spells[1].range=3;out.spells[1].radius=1;
    }
    return out;
}
m::Request request(int group=0,bool all=true,g::Point origin={1,2}) {
    m::Request out;out.canonical_group=group;out.self=self(all);out.nomination=stamp(origin);
    out.handoff_inputs={1,2,1,2,1,2,1};return out;
}
void finish(m::Menu& menu,std::size_t slice=7) {
    bool bounded=true;
    for(unsigned i=0;i<200000&&!menu.complete();++i) {
        menu.advance(slice);bounded=bounded&&menu.last_work()<=slice;
    }
    check(bounded&&menu.complete(),"persistent finite menu completes within bounded slices");
}
void paths(const s::TargetGeometry& target) {
    check(target.result().status==g::Status::Ready&&target.result().conditional_occupancy&&
        target.result().conditional_selected_clear,"published target remains conditional geometry");
    check(!target.has_initial()&&!target.result().leg(g::Leg::Initial).complete&&
        target.result().leg(g::Leg::Initial).points.empty(),"static target explicitly omits old Initial authority");
    for(std::size_t leg=1;leg<g::leg_count;++leg) {
        const auto& path=target.result().legs[leg];
        check(path.complete&&!path.points.empty()&&path.edge_inputs+1==path.points.size(),
            "Ready target contains all six complete future paths rather than partial build storage");
    }
    for(const auto point:target.selected_tiles())
        check(g::detail::supports_member(target.kit(),target.perch(),point),"target Kit supports every selected group member");
}
void first_ready_and_reuse() {
    const auto data=geometry();m::Menu menu{data};auto input=request();
    const auto queued=menu.request(input);
    check(queued.status==m::RequestStatus::Queued&&queued.queued==3&&menu.entries().size()==3,
        "Basic Targeted Area queue independent persistent family entries");
    check(menu.advance(0)==m::Status::NeedCompute&&menu.last_work()==0&&menu.ready_count()==0,
        "zero slice publishes no target and performs no scan");
    bool bounded=true;for(unsigned i=0;i<100000&&!menu.ready_count();++i) {
        menu.advance(1);bounded=bounded&&menu.last_work()<=1;
    }
    auto first=menu.first_ready(0,input.self);
    check(bounded&&first&&!menu.complete(),"first complete target is visible before other family builders finish");
    if(!first)return;
    check(first->static_geometry().get()==data.get(),"target pins the exact sealed static object");paths(*first);
    check(menu.progress().scanned_cells<data->size()*3,"first target does not wait for a full all-family perch scan");
    const auto before=menu.progress();const auto old_stamp=first->stamp();
    auto changed=input;changed.self.body.hp=1;changed.self.gold=999;changed.self.carried_gems=19;
    changed.self.actor=99;changed.self.body_revision=999;changed.self.epoch=99;changed.self.cooldown.fill(99);
    changed.self.body.can_move=changed.self.body.can_attack=changed.self.body.can_cast=false;
    changed.nomination.body=50;changed.nomination.query=60;changed.nomination.epoch=99;changed.nomination.origin={0,0};
    const auto reused=menu.request(changed);
    check(reused.status==m::RequestStatus::Reused&&reused.reused==3&&reused.queued==0,
        "HP wallet actor cooldown origin body query and date do not replace geometry work");
    check(menu.progress().units==before.units&&menu.first_ready(0,changed.self)==first,
        "reuse preserves progress and the incumbent shared target");
    auto reordered=changed;std::swap(reordered.self.spells[0],reordered.self.spells[1]);
    reordered.self.spells[0].damage=200;reordered.self.spells[1].damage=150;
    check(menu.request(reordered).reused==3,"spell slot order and positive damage magnitude are not target geometry");
    check(first->stamp().origin==old_stamp.origin&&first->stamp().epoch==old_stamp.epoch&&
        first->stamp().body==old_stamp.body&&first->stamp().query==old_stamp.query,
        "retained target stamp is never retagged to the new body or origin");
    check(!first->has_initial()&&first->result().leg(g::Leg::Initial).points.empty(),
        "reusing static future geometry never creates an Initial for a changed origin");
    check(first->distance_to_perch({0,0})!=g::detail::none,
        "pinned perch field supplies fresh-origin tracing without reoriginating the target");
    finish(menu);
    menu.advance(100);check(menu.last_work()==0,"idle Ready menu reports zero work for a funded caller tail");
    bool families[3]{};
    for(const auto& entry:menu.entries())if(entry.target) {
        families[static_cast<std::size_t>(entry.geometry.family)]=true;
        check(entry.progress.scanned_cells<=data->size(),"one entry never rescans an already considered perch");
    }
    check(families[0]&&families[1]&&families[2],"all three geometry families can independently publish");
    auto poison=input;poison.canonical_group=-1;check(menu.request(poison).status==m::RequestStatus::Invalid,"invalid group is typed");
    poison=input;++poison.nomination.geometry;check(menu.request(poison).status==m::RequestStatus::Stale,"changed static support is typed stale");
    poison=input;poison.capture_policy=2;check(menu.request(poison).status==m::RequestStatus::Unsupported,"unrepresented capture policy is typed unsupported");
    poison=input;poison.self.body.attack_range=-1;check(menu.request(poison).status==m::RequestStatus::Invalid,"poisoned geometry is typed invalid");
    poison=input;poison.self.spells[0].damage=std::numeric_limits<float>::quiet_NaN();
    check(menu.request(poison).status==m::RequestStatus::Invalid,"nonfinite relevant spell geometry cannot enter cache");
    check(menu.first_ready(0,input.self)==first,"invalid requests never erase a ready incumbent");
    auto handoff=input;++handoff.handoff_inputs[3];const auto fresh=menu.request(handoff);
    check(fresh.queued==3&&fresh.reused==0,"handoff change queues distinct target keys");finish(menu);
    bool changed_key=false;for(const auto& entry:menu.entries())if(entry.target&&entry.handoff_inputs==handoff.handoff_inputs)
        changed_key=entry.target->key().handoff_inputs==handoff.handoff_inputs;
    check(changed_key&&first->key().handoff_inputs==input.handoff_inputs,"new handoff targets do not mutate pinned old keys");
}
void area_and_key_geometry() {
    const auto data=geometry();m::Menu menu{data};auto input=request(1);input.self.body.attack_range=0;
    input.self.spells[0]={};input.self.spells[1].range=1;
    const auto result=menu.request(input);check(result.queued==2,"Area and zero-range Basic remain separate finite searches");
    finish(menu);auto target=menu.first_ready(1,input.self);
    check(target&&target->kit().family==g::Family::Area,"an empty Basic family does not block the playable Area family");
    if(!target)return;
    paths(*target);check(target->kit().aim.x==7,"AoE center lies in the intersection of every member radius box");
    check(g::detail::separation(target->perch(),target->kit().aim)<=1,"intersection center is actually castable from the perch");
    auto changed=input;changed.self.spells[1].radius=0;
    check(!menu.first_ready(1,changed.self),"changed Kit geometry cannot retrieve an old Area target");
    const auto queued=menu.request(changed);check(queued.queued==1&&queued.reused==1,
        "radius change queues only its changed family and preserves Basic progress");
    finish(menu);check(menu.ready_count()==1,"unsupported new Area geometry leaves the old ready alternative intact");
    check(menu.first_ready(1,input.self)==target,"older valid geometry remains available for its exact key");
}
void failures_progress_and_capacity() {
    const auto data=geometry(true);m::Menu menu{data,m::Limits{1}};auto input=request(0,false,{0,0});
    check(menu.request(input).queued==1,"single family fits one cache entry");
    menu.advance(1);const auto progress=menu.progress();auto changed=input;
    changed.nomination.origin={0,3};++changed.nomination.epoch;changed.self.body.hp=5;
    check(menu.request(changed).reused==1&&menu.progress().units==progress.units,
        "date-only requests never reset an active geometry builder");
    finish(menu);auto first=menu.first_ready(0,input.self);
    check(first&&menu.entries()[0].progress.failed>=1&&menu.entries()[0].failure_count>=1,
        "an unreachable first perch is remembered while later alternatives continue");
    if(!first)return;
    check(!(first->perch()==g::Point{2,1}),"failed isolated perch is never published as a playable target");
    check(menu.entries()[0].failures[0].status==g::Status::Unreachable,
        "failed target retains its typed cause rather than marking the group impossible");
    const auto deferred=menu.request(request(1,false,{0,0}));
    check(deferred.status==m::RequestStatus::Deferred&&deferred.deferred==1&&menu.first_ready(0,input.self)==first,
        "full capacity defers a new family and preserves the pinned incumbent");
    m::Menu two{data,m::Limits{2}};auto other=request(1,false,{0,0});other.self.body.attack_range=3;
    two.request(input);two.request(other);finish(two);
    auto pinned=two.first_ready(0,input.self);check(pinned&&two.ready_count()==2,"two independent group families complete");
    auto different=input;++different.handoff_inputs[0];const auto replaced=two.request(different);
    check(replaced.status==m::RequestStatus::Deferred&&replaced.deferred==1&&two.first_ready(0,input.self)==pinned,
        "pressure without a completed current-support verdict cannot evict a Ready publication");
    two.advance(20);check(two.last_work()==0&&!two.complete(),"capacity keeps refinement owed without an idle work loop");
    check(pinned&&pinned->key().handoff_inputs==input.handoff_inputs,"capacity never mutates the pinned target");
}
void lifetimes_and_generations() {
    std::shared_ptr<const s::TargetGeometry> pinned;const s::StaticGeometry* original=nullptr;std::uint64_t original_publication{};
    {
        auto data=geometry();original=data.get();m::Menu menu{data,m::Limits{1}};auto input=request(0,false);
        menu.request(input);finish(menu);pinned=menu.first_ready(0,input.self);
        for(const auto& entry:menu.entries())if(entry.target==pinned)original_publication=entry.publication_id;
        data.reset();
    }
    check(pinned&&pinned->static_geometry().get()==original,"target outlives the menu and its original seed owner");
    if(!pinned)return;
    auto replacement=geometry();m::Menu same_keys{replacement,m::Limits{1}};auto input=request(0,false);
    same_keys.request(input);finish(same_keys);auto fresh=same_keys.first_ready(0,input.self);
    check(fresh&&fresh->static_geometry().get()!=original&&fresh!=pinned,
        "equal scalar static keys never alias a different sealed object");
    check(same_keys.decline(original_publication,111,m::CompletedVerdict::Unsupported).status==m::DeclineStatus::UnknownPublication,
        "a delayed completion from an old Menu generation cannot alias a new publication");
    auto changed=geometry(false,112);m::Menu next{changed,m::Limits{1}};
    check(next.request(input).status==m::RequestStatus::Stale,"generation replacement needs matching new static stamps");
    input.nomination.geometry=112;check(next.request(input).queued==1,"new generation starts independently");finish(next);
    const auto next_ready=next.first_ready(0,input.self);
    check(pinned->static_key().layout==12&&next_ready&&next_ready->static_key().layout==112,
        "new static generation leaves the old pinned geometry truthful");
    m::Menu absent{{}};check(absent.status()==m::Status::Unsupported&&absent.request(input).status==m::RequestStatus::Unsupported,
        "unknown static object cannot create cache entries");
    m::Menu zero{changed,m::Limits{0}};check(zero.status()==m::Status::Unsupported,"zero cache capacity is rejected explicitly");
    m::Menu perch_only{changed,m::Limits{1}};auto on_member=request(0,false,{3,2});on_member.nomination.geometry=112;
    check(perch_only.request(on_member).queued==1,"nomination pose on a camp is only an enumeration hint");finish(perch_only);
    const auto future=perch_only.first_ready(0,on_member.self);
    check(future&&!future->has_initial()&&future->stamp().origin==g::Point{3,2},
        "future-only publication neither requires old-origin reachability nor counterfeits the original pose");
}
std::uint64_t publication(const m::Menu& menu,const std::shared_ptr<const s::TargetGeometry>& target) {
    for(const auto& entry:menu.entries())if(entry.target==target)return entry.publication_id;
    return 0;
}
const m::Entry* resident(const m::Menu& menu,std::uint64_t id) {
    for(const auto& entry:menu.entries())if(entry.publication_id==id&&entry.target)return &entry;
    return nullptr;
}
void completed_support_refinement() {
    const auto data=geometry();m::Menu menu{data,m::Limits{3}};auto input=request(0,false);
    menu.request(input);finish(menu);auto first=menu.first_ready(0,input.self,111);
    check(bool(first),"support-qualified first publication is available before refinement");if(!first)return;
    const auto id=publication(menu,first);const auto old_cursor=menu.families()[0].cursor;
    const auto old_stamp=first->stamp();const auto old_perch=first->perch();
    check(id&&menu.eligible(id,111)&&menu.eligible(id,222),"publication has no invented support verdict");
    const auto invalid=menu.decline(id,0,m::CompletedVerdict::Unsupported);
    check(invalid.status==m::DeclineStatus::Invalid&&!invalid.verdict_recorded&&menu.eligible(id,111),
        "unknown semantic support cannot record a verdict");
    check(menu.decline(id,111,static_cast<m::CompletedVerdict>(99)).status==m::DeclineStatus::Invalid,
        "only typed completed unsupported or refuted outcomes are admitted");
    const auto declined=menu.decline(id,111,m::CompletedVerdict::Unsupported);
    check(declined.verdict_recorded&&declined.resumed&&declined.status==m::DeclineStatus::Recorded,
        "completed unsupported resumes only the retained next-perch cursor");
    check(!menu.eligible(id,111)&&menu.eligible(id,222)&&!menu.first_ready(0,input.self,111)&&
        menu.first_ready(0,input.self,222)==first,"one support decline preserves old geometry under another support");
    const auto cursor=menu.families()[0].cursor;const auto units=menu.progress().units;
    check(menu.decline(id,111,m::CompletedVerdict::Refuted).status==m::DeclineStatus::AlreadyRecorded&&
        menu.families()[0].cursor==cursor&&menu.progress().units==units,
        "duplicate completed results neither reset nor repeat static work");
    finish(menu);auto second=menu.first_ready(0,input.self,111);
    check(second&&second!=first&&!(second->perch()==old_perch)&&menu.ready_count()==2,
        "next complete perch publishes while the earlier immutable target remains resident");
    check(menu.families()[0].cursor>old_cursor&&first->stamp().origin==old_stamp.origin&&
        first->stamp().epoch==old_stamp.epoch&&first->perch()==old_perch,
        "forward family history advances without retagging a pinned target");
    auto changed=input;changed.self.body.hp=1;changed.self.gold=321;changed.nomination.origin={0,0};
    changed.nomination.body=87;changed.nomination.query=90;++changed.nomination.epoch;
    const auto before=menu.families()[0].cursor;
    check(menu.request(changed).reused==1&&menu.families()[0].cursor==before&&
        menu.first_ready(0,changed.self,222)==first,"new body origin wallet and Life support may reuse the old publication");
    const auto another=menu.decline(id,222,m::CompletedVerdict::Refuted);
    check(another.verdict_recorded&&!another.resumed&&menu.first_ready(0,input.self,222)==second,
        "an already resident alternative prevents unnecessary family enrichment");
    check(menu.decline(std::numeric_limits<std::uint64_t>::max(),111,m::CompletedVerdict::Unsupported).status==
        m::DeclineStatus::UnknownPublication,"a result cannot be attached to an unknown publication");
}
void explicit_pressure_and_reconstruction() {
    const auto data=geometry();m::Menu menu{data,m::Limits{1,4,4}};auto input=request(0,false);
    menu.request(input);finish(menu);auto pinned=menu.first_ready(0,input.self,111);if(!pinned){check(false,"pressure fixture has a first target");return;}
    const auto id=publication(menu,pinned);const auto old_stamp=pinned->stamp();const auto old_perch=pinned->perch();
    const auto first_residency=resident(menu,id)->residency;std::weak_ptr<const s::TargetGeometry> released=pinned;
    const auto deferred=menu.decline(id,111,m::CompletedVerdict::Unsupported);
    check(deferred.status==m::DeclineStatus::Deferred&&deferred.verdict_recorded&&!deferred.resumed&&
        !menu.complete()&&menu.deferred(),"capacity retains an owed next candidate rather than declaring Done or Unsupported");
    const auto cursor=menu.families()[0].cursor;
    menu.advance(100);check(menu.last_work()==0&&menu.families()[0].cursor==cursor,
        "a blocked publication capacity has zero work, not an infinite no-progress scan");
    const auto again=menu.request(input);check(again.reused==1&&again.deferred==1&&menu.families()[0].cursor==cursor,
        "repeated requests preserve the owed cursor and report capacity");
    check(menu.decline(id,111,m::CompletedVerdict::Unsupported).status==m::DeclineStatus::AlreadyRecorded,
        "repeated completed unsupported is consumed only once");
    check(menu.retire(id,111,id).status==m::RetireStatus::Incumbent,"the named current incumbent cannot be retired");
    check(menu.retire(id,111,0).status==m::RetireStatus::Pinned,"a binder or Producer pin prevents pressure retirement");
    pinned.reset();
    check(menu.retire(id,222,0).status==m::RetireStatus::NoCompletedVerdict,
        "a verdict under another support is not permission to retire for the current support");
    check(menu.retire(id,111,0).status==m::RetireStatus::Retired&&released.expired()&&menu.retired_count()==1,
        "explicit retirement releases unpinned path storage but retains a bounded geometry recipe");
    check(menu.retire(id,111,0).status==m::RetireStatus::AlreadyRetired&&
        menu.decline(id,111,m::CompletedVerdict::Unsupported).status==m::DeclineStatus::AlreadyRecorded,
        "duplicate pressure/result delivery after retirement is idempotent");
    const auto metadata=menu.retired_at(0);
    check(metadata&&metadata->publication_id==id&&!metadata->target&&metadata->state==m::EntryState::Retired&&
        metadata->nomination.origin==old_stamp.origin,"retirement is provenance metadata, never a new geometry refutation");
    finish(menu);auto next=menu.first_ready(0,input.self,111);check(bool(next),"retirement lets the next forward perch complete");if(!next)return;
    const auto next_id=publication(menu,next);check(next_id!=id&&menu.families()[0].cursor>cursor,
        "new publication uses the retained forward cursor rather than retrying the rejected perch");
    check(menu.reconstruct(id,111).status==m::ReconstructStatus::DeclinedForSupport,
        "retired geometry cannot reconstruct an already completed unsupported support");
    const auto next_cursor=menu.families()[0].cursor;
    const auto owed=menu.reconstruct(id,222);
    check(owed.status==m::ReconstructStatus::Deferred&&owed.owed&&!menu.complete(),
        "changed support can owe reconstruction even while resident capacity is full");
    menu.decline(next_id,222,m::CompletedVerdict::Refuted);next.reset();
    check(menu.retire(next_id,222,0).status==m::RetireStatus::Retired,"completed current-support refutation can release a non-incumbent alternative");
    finish(menu);auto fresh=menu.first_ready(0,input.self,222);const auto rebuilt=resident(menu,id);
    check(fresh&&rebuilt&&rebuilt->target==fresh&&rebuilt->residency==first_residency+1&&
        fresh->perch()==old_perch&&fresh->stamp().origin==old_stamp.origin&&fresh->stamp().epoch==old_stamp.epoch,
        "reconstruction creates a fresh object under its unchanged immutable recipe and truthful original stamp");
    check(menu.families()[0].cursor==next_cursor&&!menu.eligible(id,111)&&menu.eligible(id,222),
        "support reconstruction neither rewinds the forward cursor nor forgets earlier support evidence");
    check(menu.reconstruct(id,222).status==m::ReconstructStatus::AlreadyResident,
        "repeated changed-support reconstruction reuses the resident publication");
    fresh.reset();check(menu.retire(id,222,0).status==m::RetireStatus::NoCompletedVerdict,
        "another publication's verdict cannot retire the restored recipe");
}
void finite_family_and_verdict_capacity() {
    const auto data=geometry();m::Menu menu{data,m::Limits{1,4,16}};auto input=request(0,false);
    menu.request(input);unsigned publications=0;std::uint64_t saved=0;
    for(unsigned attempt=0;attempt<20;++attempt) {
        finish(menu);std::uint64_t id=0;for(const auto& entry:menu.entries())if(entry.target)id=entry.publication_id;
        if(!id)break;if(!saved)saved=id;++publications;
        const auto outcome=menu.decline(id,111,m::CompletedVerdict::Unsupported);
        if(outcome.status==m::DeclineStatus::Exhausted)break;
        check(menu.retire(id,111,0).status==m::RetireStatus::Retired,"finite unsupported perch releases only its own resident storage");
    }
    check(publications>1&&publications<20&&menu.complete()&&menu.families()[0].cursor==data->size()&&
        menu.families()[0].progress.scanned_cells==data->size(),"completed declines walk each perch at most once and exhaust only that static family");
    if(saved) {
        const auto reconstruction=menu.reconstruct(saved,222);
        check(reconstruction.owed&&reconstruction.status==m::ReconstructStatus::Queued,
            "forward exhaustion does not ban an earlier recipe under changed support");
        finish(menu);check(bool(menu.first_ready(0,input.self,222))&&menu.families()[0].cursor==data->size(),
            "exhausted family can reconstruct changed support without resetting its static history");
    }
    m::Menu bounded{data,m::Limits{1}};bounded.request(input);finish(bounded);
    std::uint64_t id=0;for(const auto& entry:bounded.entries())if(entry.target)id=entry.publication_id;
    for(std::size_t n=0;n<m::remembered_supports;++n)
        check(bounded.decline(id,100+n,m::CompletedVerdict::Unsupported).verdict_recorded,
            "bounded exact-support verdict history records each completed result");
    const auto full=bounded.decline(id,999,m::CompletedVerdict::Unsupported);
    check(full.status==m::DeclineStatus::Deferred&&!full.verdict_recorded&&bounded.eligible(id,999)&&
        !bounded.complete(),"full support metadata defers without inventing or forgetting a verdict");
    m::Menu records{data,m::Limits{1,4,1}};records.request(input);finish(records);
    std::uint64_t first=0;for(const auto& entry:records.entries())if(entry.target)first=entry.publication_id;
    records.decline(first,111,m::CompletedVerdict::Unsupported);
    check(records.retire(first,111,0).status==m::RetireStatus::Retired,"one declared metadata record can preserve one retired recipe");
    finish(records);std::uint64_t second=0;for(const auto& entry:records.entries())if(entry.target)second=entry.publication_id;
    records.decline(second,111,m::CompletedVerdict::Unsupported);
    check(records.retire(second,111,0).status==m::RetireStatus::Deferred&&records.deferred()&&!records.complete()&&
        resident(records,second),"metadata pressure cannot erase a recipe or turn refinement into Done");
    records.advance(20);check(records.last_work()==0,"full metadata plus resident capacity yields a finite zero-progress tail");
}
}
int main() {
    first_ready_and_reuse();area_and_key_geometry();failures_progress_and_capacity();lifetimes_and_generations();
    completed_support_refinement();explicit_pressure_and_reconstruction();finite_family_and_verdict_capacity();
    std::printf("assignment_static_menu: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
