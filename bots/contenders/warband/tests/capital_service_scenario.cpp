#include "../src/capital_service_scenario.h"
#include <cstdio>
#include <initializer_list>
#include <utility>

namespace s=wb::capital::service_scenario;
static unsigned checks=0,failures=0;
static void check(bool okay,const char* why){++checks;if(!okay){++failures;std::printf("FAIL %s\n",why);}}
struct Budget {unsigned left{},paid{};};
static bool pay(void* pointer,unsigned amount){if(!pointer)return true;auto& b=*static_cast<Budget*>(pointer);if(b.left<amount)return false;b.left-=amount;b.paid+=amount;return true;}
static bool revoke(void* pointer,unsigned){static_cast<s::Tracker*>(pointer)->invalidate();return true;}
struct ResetDuringWork {s::Tracker* tracker{};const s::Initial* initial{};unsigned calls{},after{};};
static bool reset_during_work(void* pointer,unsigned){auto& r=*static_cast<ResetDuringWork*>(pointer);if(++r.calls==r.after)r.tracker->reset(*r.initial);return true;}
static s::Facts closed(){return {s::Life::Alive,s::Channel::None,s::Effects::NoAutomatic};}
struct Fixture {
    s::Tracker tracker;s::Source source{11,22,33,{0,0}};std::array<s::Actor,3> roster{{{0,0},{1,1},{2,0}}};
    std::array<std::uint8_t,3> own{},foe{},visible{};std::array<s::Spawn,2> spawns{};std::array<s::Sample,3> samples{};
    static void mark(std::array<std::uint8_t,3>& mask,s::Point p){const auto at=unsigned(p.y*5+p.x);mask[at/8]|=std::uint8_t(1u<<(at%8));}
    Fixture(){mark(own,{0,0});mark(own,{1,0});mark(foe,{3,1});mark(foe,{3,2});mark(foe,{4,0});spawns={s::Spawn{0,own},s::Spawn{1,foe}};
        for(unsigned a=0;a<samples.size();++a){samples[a].actor=roster[a];samples[a].facts.life=s::Life::Alive;}
        samples[0].presence=s::Presence::Visible;samples[0].position={0,0};samples[0].facts=closed();samples[0].actual_binding=100;
        sight({{0,0}});check(tracker.reset(initial()).status==s::Status::Ready,"valid complete public roster and spawn reset");}
    void sight(std::initializer_list<s::Point> points){visible={};for(auto point:points)mark(visible,point);}
    s::Initial initial(){return {source,{0,0,0},5,4,roster,spawns,true,true};}
    s::Batch batch(s::Stamp stamp){return {source,stamp,visible,samples,true,true};}
    s::Update observe(s::Stamp stamp){return tracker.observe(batch(stamp));}
    s::Snapshot read(s::Stamp stamp){return tracker.read(source,stamp,{nullptr,&pay});}
};
static void spawn_history(){Fixture f;check(f.read({0,0,0}).status==s::Status::Empty,"reset without personal observation grants no current witness");
    const s::Stamp one{1,1,0};check(f.observe(one).status==s::Status::Applied,"complete first personal observation applies");auto out=f.read(one);
    check(out.status==s::Status::Ready&&out.count==3&&out.joint_positions_validated,"complete named roster output has a distinct-origin assignment");const auto& owner=out.actors[0];const auto& foe=out.actors[1];const auto& ally=out.actors[2];
    check(owner.geometry==s::Geometry::Supported&&owner.evidence==s::Evidence::ActualAtCurrent&&owner.point==s::Point{0,0}&&owner.required_class_usable,"positive owner sample is actual only at this exact input");
    check(foe.geometry==s::Geometry::Supported&&foe.evidence==s::Evidence::ConditionalFixedOrigin&&foe.point==s::Point{3,1},"unseen actor chooses lexicographic surviving public own-team spawn cell");
    check(foe.origin==s::Stamp{0,0,0}&&foe.last_consistent==one&&foe.origin_is_public_spawn&&!foe.last_actual.present,"unseen Hold position history begins at public game start, without hidden actual facts");
    check(!foe.required_class_usable&&foe.class_reason==s::ClassReason::ChannelUnknown,"geometric history does not close unknown channel/effects");
    check(ally.point==s::Point{1,0},"current negative visibility removes occupied-visible own spawn tile for hidden ally");
    f.sight({{0,0},{3,1}});check(f.observe({2,2,0}).status==s::Status::Applied,"later complete negative vision updates history");out=f.read({2,2,0});
    check(out.actors[1].point==s::Point{3,2}&&out.actors[1].origin==s::Stamp{0,0,0},"negative evidence removes one initial alternative without inventing a move");
    f.sight({{0,0},{3,2}});f.observe({3,3,0});out=f.read({3,3,0});check(out.actors[1].point==s::Point{4,0},"selected origin may change only between surviving since-start alternative histories");
    f.sight({{0,0},{4,0}});const auto empty=f.observe({4,4,0});out=f.read({4,4,0});
    check(empty.any_inconsistent&&out.actors[1].geometry==s::Geometry::Inconsistent&&out.actors[1].geometry_reason==s::GeometryReason::NoSurvivingSpawn,"all eliminated initial origins refute the constant-Hold family");
    check(out.actors[1].evidence==s::Evidence::None&&!out.actors[1].required_class_usable&&out.actors[1].point==s::Point{-1,-1},"empty history supplies neither hidden point nor usable class");
    f.sight({{0,0}});f.observe({5,5,0});check(f.read({5,5,0}).actors[1].geometry==s::Geometry::Inconsistent,"lost unseen histories cannot revive without new actual origin");
}
static void actual_origin_and_refutation(){Fixture f;f.observe({1,1,0});f.samples[1].presence=s::Presence::Visible;f.samples[1].position={2,2};f.samples[1].facts=closed();f.samples[1].actual_binding=700;f.sight({{0,0},{2,2}});
    const s::Stamp seen{2,2,0};f.observe(seen);auto out=f.read(seen);const auto actual=out.actors[1];
    check(actual.point==s::Point{2,2}&&actual.evidence==s::Evidence::ActualAtCurrent&&actual.origin==seen&&!actual.origin_is_public_spawn,"positive sample outside spawn begins a NEW actual-origin hypothesis");
    check(actual.last_actual.present&&actual.last_actual.binding==700&&actual.last_actual.position==s::Point{2,2}&&actual.last_actual.stamp==seen,"actual stat/kit provenance remains separately dated and owned");
    f.samples[1].presence=s::Presence::Hidden;f.samples[1].actual_binding=0;f.samples[1].facts={};f.sight({{0,0}});const s::Stamp hidden{3,3,0};f.observe(hidden);out=f.read(hidden);
    check(out.actors[1].point==s::Point{2,2}&&out.actors[1].origin==seen&&out.actors[1].last_consistent==hidden,"hidden observation keeps exact observed origin without fabricated travel");
    check(out.actors[1].evidence==s::Evidence::ConditionalFixedOrigin&&!out.actors[1].required_class_usable&&out.actors[1].last_actual.binding==700,"conditional hidden witness does not become actual or refresh old stats");
    f.sight({{0,0},{2,2}});f.observe({4,4,0});out=f.read({4,4,0});
    check(out.actors[1].geometry==s::Geometry::Inconsistent&&out.actors[1].geometry_reason==s::GeometryReason::ObservedOriginRefuted,"negative view of a missing actor refutes the fixed observed origin");
    f.samples[1].presence=s::Presence::Visible;f.samples[1].position={4,3};f.samples[1].facts=closed();f.samples[1].actual_binding=701;f.sight({{0,0},{4,3}});f.observe({5,5,0});out=f.read({5,5,0});
    check(out.actors[1].geometry==s::Geometry::Supported&&out.actors[1].point==s::Point{4,3}&&out.actors[1].origin==s::Stamp{5,5,0}&&out.actors[1].last_actual.binding==701,"later actual movement starts a new hypothesis rather than repairing the refuted old path");
}
static void temporal_and_atomic_inputs(){Fixture f;const s::Stamp one{1,1,0};f.observe(one);auto first=f.read(one);const auto generation=first.generation;
    check(f.observe(one).status==s::Status::Duplicate&&f.read(one).generation==generation,"identical duplicate stamp is idempotent");
    std::swap(f.samples[1],f.samples[2]);check(f.observe(one).status==s::Status::Duplicate,"duplicate batch fingerprint is roster-order independent");std::swap(f.samples[1],f.samples[2]);
    f.samples[0].actual_binding=101;check(f.observe(one).status==s::Status::Conflict&&f.read(one).actors[0].last_actual.binding==100,"same stamp conflicting actual facts reject atomically");f.samples[0].actual_binding=100;
    check(f.observe({0,0,0}).status==s::Status::Stale&&f.read(one).generation==generation,"stale input does not mutate current histories");
    check(f.observe({2,1,1}).status==s::Status::Stale,"same owner input cannot silently move to another round");
    const s::Stamp effect{1,1,1};f.samples[0].actual_binding=102;check(f.observe(effect).status==s::Status::Applied,"fresh same-input effect generation admits a new personal observation");
    check(f.read(one).status==s::Status::Stale&&f.read(effect).actors[0].last_actual.binding==102,"read requires exact current input/effect stamp");
    auto wrong=f.batch({2,2,0});wrong.source.map=999;check(f.tracker.observe(wrong).status==s::Status::Rejected&&f.read(effect).actors[0].last_actual.binding==102,"map/source mismatch cannot update owned history");
    wrong=f.batch({2,2,0});wrong.visible=std::span<const std::uint8_t>(f.visible.data(),2);check(f.tracker.observe(wrong).reason==s::Reason::VisibilityMask,"partial negative mask rejected before any actor update");
    check(f.read({2,2,0}).status==s::Status::Stale&&f.read(effect).status==s::Status::Ready,"rejected fresh batch cannot masquerade as a current witness");
    f.tracker.invalidate();check(f.read(effect).status==s::Status::Rejected,"explicit invalidation revokes source and every output");
    check(f.tracker.reset(f.initial()).status==s::Status::Ready&&f.read(effect).status==s::Status::Empty,"reset clears old observation authority");
}
static void malformed_batches(){Fixture f;const s::Stamp one{1,1,0};f.observe(one);auto malformed=f.batch({2,2,0});
    malformed.complete_roster=false;check(f.tracker.observe(malformed).reason==s::Reason::SampleCount,"declared incomplete roster cannot be pruned as complete");
    malformed=f.batch({2,2,0});malformed.complete_visibility=false;check(f.tracker.observe(malformed).reason==s::Reason::VisibilityMask,"declared partial negative vision cannot prune histories");
    malformed=f.batch({2,2,0});malformed.samples=std::span<const s::Sample>(f.samples.data(),2);check(f.tracker.observe(malformed).reason==s::Reason::SampleCount,"every roster actor requires a sample");
    auto saved=f.samples[2];f.samples[2].actor=f.samples[1].actor;check(f.observe({2,2,0}).reason==s::Reason::SampleActor,"duplicate actor cannot hide an omitted roster member");f.samples[2]=saved;
    f.samples[1].actor.team=0;check(f.observe({2,2,0}).reason==s::Reason::SampleActor,"actor identity binds actual team as well as numeric ID");f.samples[1].actor.team=1;
    f.samples[1].actor.entity=s::Entity::Npc;check(f.observe({2,2,0}).reason==s::Reason::Namespace,"same numeric NPC handle cannot impersonate champion actor");f.samples[1].actor.entity=s::Entity::Champion;
    f.samples[1].presence=s::Presence::Visible;f.samples[1].position={2,2};f.samples[1].actual_binding=700;
    check(f.observe({2,2,0}).reason==s::Reason::PositiveOutsideVision,"positive actor sample must belong to complete current personal visibility");
    f.sight({{0,0},{2,2}});f.samples[1].position={5,0};check(f.observe({2,2,0}).reason==s::Reason::SamplePosition,"actual point must lie within the map universe");
    f.samples[1].position={2,2};f.samples[1].actual_binding=0;check(f.observe({2,2,0}).reason==s::Reason::ActualBinding,"positive point cannot manufacture actual stat provenance");
    f.samples[1].presence=s::Presence::Hidden;f.samples[1].actual_binding=700;check(f.observe({2,2,0}).reason==s::Reason::ActualBinding,"hidden sample cannot claim newly observed actual stats");
    f.samples[1].actual_binding=0;f.visible[2]|=128;check(f.observe({2,2,0}).reason==s::Reason::VisibilityMask,"mask padding is not an unbounded extra cell universe");
    check(f.read(one).actors[0].last_actual.binding==100,"all malformed batches leave prior valid actors untouched");
}
static void class_facts_are_not_geometry(){Fixture f;f.samples[1].facts=closed();f.observe({1,1,0});auto out=f.read({1,1,0});
    check(out.actors[1].geometry==s::Geometry::Supported&&out.actors[1].required_class_usable&&out.actors[1].evidence==s::Evidence::ConditionalFixedOrigin,"supplied closed present facts still yield conditional hidden geometry");
    f.samples[1].facts.life=s::Life::Dead;f.observe({2,2,0});out=f.read({2,2,0});check(out.actors[1].geometry==s::Geometry::Supported&&!out.actors[1].required_class_usable&&out.actors[1].class_reason==s::ClassReason::Dead,"public death rejects required living class without inventing respawn travel");
    f.samples[1].presence=s::Presence::Visible;f.samples[1].position={2,2};f.samples[1].actual_binding=700;f.samples[1].facts=closed();f.samples[1].facts.channel=s::Channel::Active;f.sight({{0,0},{2,2}});f.observe({3,3,0});
    out=f.read({3,3,0});check(out.actors[1].last_actual.facts.channel==s::Channel::Active&&out.actors[1].class_reason==s::ClassReason::ActiveChannel,"observed active channel is explicit actual evidence");
    f.samples[1].presence=s::Presence::Hidden;f.samples[1].actual_binding=0;f.samples[1].facts=closed();f.sight({{0,0}});f.observe({4,4,0});out=f.read({4,4,0});
    check(!out.actors[1].required_class_usable&&out.actors[1].class_reason==s::ClassReason::ActiveChannel&&out.actors[1].last_actual.binding==700,"future hidden Hold does not silently cancel an observed active channel");
    f.samples[1].presence=s::Presence::Visible;f.samples[1].actual_binding=701;f.samples[1].facts=closed();f.samples[1].facts.effects=s::Effects::Unsupported;f.sight({{0,0},{2,2}});f.observe({5,5,0});
    f.samples[1].presence=s::Presence::Hidden;f.samples[1].actual_binding=0;f.samples[1].facts=closed();f.sight({{0,0}});f.observe({6,6,0});out=f.read({6,6,0});
    check(out.actors[1].class_reason==s::ClassReason::UnsupportedEffects&&!out.actors[1].required_class_usable,"observed unsupported effects also survive an unobserved Hold claim");
    f.samples[1].presence=s::Presence::Visible;f.samples[1].actual_binding=702;f.samples[1].facts=closed();f.sight({{0,0},{2,2}});f.observe({7,7,0});out=f.read({7,7,0});
    check(out.actors[1].required_class_usable&&out.actors[1].last_actual.binding==702,"new actual closed sample can establish a new usable fixed-origin class");
}
static void invalid_initial_and_ownership(){Fixture f;auto in=f.initial();in.complete_roster=false;check(f.tracker.reset(in).reason==s::Reason::Roster,"incomplete public initial roster rejected");
    in=f.initial();in.width=101;in.height=100;check(f.tracker.reset(in).status==s::Status::Capacity,"map cells bounded at ten thousand");
    in=f.initial();in.source.observer.entity=s::Entity::Npc;check(f.tracker.reset(in).reason==s::Reason::Namespace,"source observer is a champion handle");
    auto roster=f.roster;roster[2].id=1;in=f.initial();in.roster=roster;check(f.tracker.reset(in).reason==s::Reason::Roster,"global champion IDs cannot duplicate across teams");
    auto spawn=f.spawns;spawn[1].team=0;in=f.initial();in.spawns=spawn;check(f.tracker.reset(in).reason==s::Reason::SpawnMask,"each actual actor team needs its own public spawn region");
    auto bytes=f.foe;bytes[2]=128;spawn=f.spawns;spawn[1].cells=bytes;in=f.initial();in.spawns=spawn;check(f.tracker.reset(in).reason==s::Reason::SpawnMask,"public spawn mask must match exact map universe");
    f.foe={};f.own={};f.observe({1,1,0});check(f.read({1,1,0}).actors[1].point==s::Point{3,1},"Tracker owns spawn support instead of borrowing mutated input storage");
    f.visible={};auto retained=f.read({1,1,0});check(retained.actors[0].evidence==s::Evidence::ActualAtCurrent,"output owns facts after input visibility storage changes");
    auto wrong=f.source;wrong.game=999;check(f.tracker.read(wrong,{1,1,0}).reason==s::Reason::Source,"snapshot consumption binds the exact game identity");
}
static void joint_assignment_and_funding(){Fixture f;s::Tracker tracker;auto source=f.source;
    std::array<s::Actor,4> roster{{{0,0},{1,1},{3,1},{4,1}}};auto initial=f.initial();initial.roster=roster;
    check(tracker.reset(initial).status==s::Status::Ready,"four actual bodies share one public team spawn region");
    std::array<s::Sample,4> samples{};for(unsigned a=0;a<samples.size();++a){samples[a].actor=roster[a];samples[a].facts=closed();}samples[0]=f.samples[0];
    const s::Stamp stamp{1,1,0};s::Batch batch{source,stamp,f.visible,samples,true,true};check(tracker.observe(batch).status==s::Status::Applied,"joint assignment consumes one complete personal observation");
    auto unpaid=tracker.read(source,stamp);check(unpaid.status==s::Status::Unknown&&unpaid.assignment==s::Assignment::NeedFuel&&!unpaid.joint_positions_validated,"missing work budget cannot claim a joint hidden world");
    Budget small{3,0};auto partial=tracker.read(source,stamp,{&small,&pay});check(partial.status==s::Status::Unknown&&partial.assignment==s::Assignment::NeedFuel&&!partial.joint_positions_validated&&small.paid==3,"partial assignment remains Unknown at the charged boundary");
    Budget enough{10000,0};const auto out=tracker.read(source,stamp,{&enough,&pay});check(out.status==s::Status::Ready&&out.joint_positions_validated&&out.assignment==s::Assignment::Complete,"augmenting assignment constructs one complete distinct-origin world");
    bool distinct=true;for(unsigned a=0;a<out.count;++a)for(unsigned b=0;b<a;++b)distinct=distinct&&!(out.actors[a].point==out.actors[b].point);
    check(distinct&&out.all_required_classes_usable,"joint constant-Hold origins respect body occupancy");
    for(unsigned a=1;a<out.count;++a)check(out.actors[a].evidence==s::Evidence::ConditionalFixedOrigin&&out.actors[a].origin_is_public_spawn&&out.actors[a].point_assigned,"every hidden assigned origin has its own since-start hypothesis");
    auto repeated=tracker.read(source,stamp,{nullptr,&pay});bool deterministic=repeated.generation==out.generation;for(unsigned a=0;a<out.count;++a)deterministic=deterministic&&repeated.actors[a].point==out.actors[a].point;
    check(deterministic&&enough.paid>0,"matching traversal is deterministic and pays actual scan units without mutating observations");
    f.sight({{0,0},{3,1}});batch.stamp={2,2,0};batch.visible=f.visible;tracker.observe(batch);auto impossible=tracker.read(source,batch.stamp,{nullptr,&pay});
    check(impossible.status==s::Status::Unknown&&impossible.assignment==s::Assignment::Inconsistent&&!impossible.joint_positions_validated,"three actors cannot share two surviving fixed-origin tiles");
    bool individual=true;for(unsigned a=0;a<impossible.count;++a)individual=individual&&impossible.actors[a].geometry==s::Geometry::Supported;
    check(individual&&!impossible.all_required_classes_usable,"per-actor geometric support is not a coherent joint assignment");
    samples[1].presence=samples[2].presence=s::Presence::Visible;samples[1].position=samples[2].position={3,1};samples[1].actual_binding=601;samples[2].actual_binding=602;
    batch.stamp={3,3,0};tracker.observe(batch);const auto collision=tracker.read(source,batch.stamp,{nullptr,&pay});
    check(collision.assignment==s::Assignment::Inconsistent&&!collision.joint_positions_validated,"colliding actual/fixed observed origins refute this joint position world");
}
static void full_map_and_revocation(){static s::Tracker tracker;s::Source source{91,92,93,{0,0}};
    std::array<std::uint8_t,s::mask_bytes> own{},foe{},visible{};own[0]=visible[0]=1;
    for(unsigned y=95;y<100;++y){const unsigned cell=y*100+99;foe[cell/8]|=std::uint8_t(1u<<(cell%8));}
    std::array<s::Actor,6> roster{{{0,0},{1,1},{2,1},{3,1},{4,1},{5,1}}};std::array<s::Spawn,2> spawns{{{0,own},{1,foe}}};
    check(tracker.reset({source,{0,0,0},100,100,roster,spawns,true,true}).status==s::Status::Ready,"largest admitted map and all six named actors initialize");
    std::array<s::Sample,6> samples{};for(unsigned a=0;a<6;++a){samples[a].actor=roster[a];samples[a].facts.life=s::Life::Alive;}
    samples[0].presence=s::Presence::Visible;samples[0].position={0,0};samples[0].actual_binding=100;samples[0].facts=closed();const s::Stamp stamp{1,1,0};
    check(tracker.observe({source,stamp,visible,samples,true,true}).status==s::Status::Applied,"full map observation is copied as one atomic roster batch");
    Budget budget{6*6*10000+6,0};const auto out=tracker.read(source,stamp,{&budget,&pay});
    check(out.status==s::Status::Ready&&out.joint_positions_validated&&budget.paid<=6*6*10000+6,"six-actor augmenting search stays within declared charged cell bound");
    bool distinct=true;for(unsigned a=1;a<6;++a){distinct=distinct&&out.actors[a].point.x==99&&out.actors[a].point.y>=95;for(unsigned b=0;b<a;++b)distinct=distinct&&!(out.actors[a].point==out.actors[b].point);}
    check(distinct&&!out.all_required_classes_usable,"joint geometry does not invent hidden channel or effect closure");
    const auto revoked=tracker.read(source,stamp,{&tracker,&revoke});check(revoked.status==s::Status::Stale&&revoked.reason==s::Reason::Source&&!revoked.joint_positions_validated,"budget callback source revocation cannot complete an old assignment");
    Fixture f;auto initial=f.initial();initial.game_start={2,2,3};check(f.tracker.reset(initial).status==s::Status::Ready,"dated public start binds the beginning of supported history");
    check(f.observe({2,2,2}).reason==s::Reason::Stamp,"earlier same-input effect cannot precede the declared game-start boundary");
}
// Frozen dense traversal reference for fully funded assignment equivalence.
// It intentionally pays every old x/y map-cell visit. This is an independent
// representation of candidate sets, not native import/fuel measurement.
struct DenseSupport {bool fixed{};s::Point point{};std::array<std::uint8_t,s::mask_bytes> mask{};};
struct DenseResult {bool complete{};unsigned paid{};std::array<int,s::actor_limit> cells{{-1,-1,-1,-1,-1,-1}};};
static bool dense_augment(unsigned actor,unsigned width,unsigned height,unsigned count,const std::array<DenseSupport,s::actor_limit>& support,
                          DenseResult& out,std::array<bool,s::actor_limit>& visited){
    if(visited[actor])return false;visited[actor]=true;
    for(unsigned x=0;x<width;++x)for(unsigned y=0;y<height;++y){++out.paid;const unsigned cell=y*width+x;
        if(!(support[actor].mask[cell/8]&(1u<<(cell%8))))continue;unsigned owner=count;
        for(unsigned a=0;a<count;++a)if(out.cells[a]==int(cell))owner=a;
        if(owner==count){out.cells[actor]=int(cell);return true;}if(support[owner].fixed)continue;
        if(dense_augment(owner,width,height,count,support,out,visited)){out.cells[actor]=int(cell);return true;}}
    return false;
}
static DenseResult dense(unsigned width,unsigned height,unsigned count,const std::array<DenseSupport,s::actor_limit>& support){DenseResult out;
    for(unsigned a=0;a<count;++a)if(support[a].fixed){++out.paid;out.cells[a]=support[a].point.y*int(width)+support[a].point.x;
        for(unsigned b=0;b<a;++b)if(out.cells[a]==out.cells[b])return out;}
    for(unsigned a=0;a<count;++a)if(!support[a].fixed){std::array<bool,s::actor_limit> seen{};if(!dense_augment(a,width,height,count,support,out,seen))return out;}
    out.complete=true;return out;
}
static void sparse_equivalence(){static s::Tracker tracker;std::array<std::uint8_t,2> own{},foe{},visible{};own[0]=visible[0]=1;
    const s::Source source{201,202,203,{0,0}};std::array<s::Actor,4> roster{{{0,0},{1,1},{2,1},{3,1}}};std::array<s::Sample,4> samples{};
    for(unsigned a=0;a<4;++a){samples[a].actor=roster[a];samples[a].facts=closed();}samples[0].presence=s::Presence::Visible;samples[0].position={0,0};samples[0].actual_binding=100;
    // All subsets of eleven non-owner cells include sparse, dense, impossible,
    // and backtracking assignments. Negative removal is checked at a new input.
    for(unsigned pattern=1;pattern<2048;++pattern){foe={};for(unsigned cell=1;cell<12;++cell)if(pattern&(1u<<(cell-1)))foe[cell/8]|=std::uint8_t(1u<<(cell%8));
        std::array<s::Spawn,2> spawns{{{0,own},{1,foe}}};check(tracker.reset({source,{0,0,0},4,3,roster,spawns,true,true}).status==s::Status::Ready,"equivalence source reset");
        visible={};visible[0]=1;check(tracker.observe({source,{1,1,0},visible,samples,true,true}).status==s::Status::Applied,"equivalence complete observation");
        std::array<DenseSupport,s::actor_limit> support{};support[0].fixed=true;support[0].point={0,0};for(unsigned a=1;a<4;++a)for(unsigned b=0;b<2;++b)support[a].mask[b]=foe[b];
        auto expected=dense(4,3,4,support);Budget budget{1000,0};auto out=tracker.read(source,{1,1,0},{&budget,&pay});
        bool same=out.joint_positions_validated==expected.complete;if(expected.complete)for(unsigned a=0;a<4;++a)same=same&&out.actors[a].point==s::Point{expected.cells[a]%4,expected.cells[a]/4};
        check(same&&budget.paid<=expected.paid,"sparse candidate matching preserves exact fully funded dense traversal result");
        const unsigned removed=1+(pattern%11);visible[removed/8]|=std::uint8_t(1u<<(removed%8));tracker.observe({source,{2,2,0},visible,samples,true,true});
        for(unsigned a=1;a<4;++a)support[a].mask[removed/8]&=std::uint8_t(~(1u<<(removed%8)));expected=dense(4,3,4,support);budget={1000,0};out=tracker.read(source,{2,2,0},{&budget,&pay});
        same=out.joint_positions_validated==expected.complete;if(expected.complete)for(unsigned a=0;a<4;++a)same=same&&out.actors[a].point==s::Point{expected.cells[a]%4,expected.cells[a]/4};
        check(same&&budget.paid<=expected.paid,"negative compaction preserves dense reference assignment and order");
    }
}
static void sparse_far_spawn_and_poison(){static s::Tracker tracker;const s::Source source{301,302,303,{0,0}};
    std::array<std::uint8_t,121> own{},foe{},visible{};own[0]=visible[0]=1;
    for(unsigned x=28;x<31;++x)for(unsigned y=28;y<31;++y){const unsigned cell=y*31+x;foe[cell/8]|=std::uint8_t(1u<<(cell%8));}
    std::array<s::Actor,4> roster{{{0,0},{1,1},{2,1},{3,1}}};std::array<s::Sample,4> samples{};for(unsigned a=0;a<4;++a){samples[a].actor=roster[a];samples[a].facts=closed();}
    samples[0].presence=s::Presence::Visible;samples[0].position={0,0};samples[0].actual_binding=100;std::array<s::Spawn,2> spawns{{{0,own},{1,foe}}};
    check(tracker.reset({source,{0,0,0},31,31,roster,spawns,true,true}).status==s::Status::Ready,"far 3x3 spawn initializes real sparse candidate index");
    tracker.observe({source,{1,1,0},visible,samples,true,true});Budget sparse{1000,0};const auto out=tracker.read(source,{1,1,0},{&sparse,&pay});
    std::array<DenseSupport,s::actor_limit> support{};support[0].fixed=true;support[0].point={0,0};for(unsigned a=1;a<4;++a)for(unsigned b=0;b<foe.size();++b)support[a].mask[b]=foe[b];const auto old=dense(31,31,4,support);
    bool same=out.joint_positions_validated&&old.complete;for(unsigned a=0;a<4;++a)same=same&&out.actors[a].point==s::Point{old.cells[a]%31,old.cells[a]/31};
    check(same&&sparse.paid<=1+3*3*9&&sparse.paid<old.paid,"far-team matching charges surviving candidates rather than 900 empty map cells per scan");
    std::printf("scenario source-work far31x31 dense_cell_tests %u sparse_candidate_tests %u\n",old.paid,sparse.paid);
    // Keep only one hidden actor; remove eight of nine cells. An exact two-unit
    // read (one actual reservation, one candidate) must still complete.
    std::array<s::Actor,2> pair{{{0,0},{1,1}}};std::array<s::Sample,2> pair_samples{{samples[0],samples[1]}};tracker.reset({source,{0,0,0},31,31,pair,spawns,true,true});
    for(unsigned x=28;x<31;++x)for(unsigned y=28;y<31;++y)if(x!=30||y!=30){const unsigned cell=y*31+x;visible[cell/8]|=std::uint8_t(1u<<(cell%8));}
    tracker.observe({source,{1,1,0},visible,pair_samples,true,true});Budget exact{2,0};const auto one=tracker.read(source,{1,1,0},{&exact,&pay});
    check(one.joint_positions_validated&&one.actors[1].point==s::Point{30,30}&&exact.paid==2,"negative filtering removes candidates from charged active prefix");
    const unsigned last=30*31+30;visible[last/8]|=std::uint8_t(1u<<(last%8));tracker.observe({source,{2,2,0},visible,pair_samples,true,true});exact={2,0};const auto empty=tracker.read(source,{2,2,0},{&exact,&pay});
    check(empty.assignment==s::Assignment::Inconsistent&&!empty.joint_positions_validated&&exact.paid==0,"empty active prefix cannot expose poisoned removed entries");
    Fixture small;auto initial=small.initial();initial.source.game=304;initial.roster=std::span<const s::Actor>(small.roster.data(),2);
    check(tracker.reset(initial).status==s::Status::Ready,"large-to-small source reset overwrites active sparse index");
    auto batch=small.batch({1,1,0});batch.source=initial.source;batch.samples=std::span<const s::Sample>(small.samples.data(),2);tracker.observe(batch);exact={2,0};const auto reset=tracker.read(initial.source,batch.stamp,{&exact,&pay});
    check(reset.joint_positions_validated&&reset.actors[1].point==s::Point{3,1}&&exact.paid==2,"small reset cannot borrow old 31x31 origin or discarded index tail");
    small.samples[1].presence=s::Presence::Visible;small.samples[1].position={2,2};small.samples[1].actual_binding=700;small.sight({{0,0},{2,2}});batch.stamp={2,2,0};batch.visible=small.visible;batch.samples=std::span<const s::Sample>(small.samples.data(),2);tracker.observe(batch);
    small.samples[1].presence=s::Presence::Hidden;small.samples[1].actual_binding=0;small.sight({{0,0}});batch.stamp={3,3,0};batch.visible=small.visible;tracker.observe(batch);exact={2,0};const auto observed=tracker.read(initial.source,batch.stamp,{&exact,&pay});
    check(observed.joint_positions_validated&&observed.actors[1].point==s::Point{2,2}&&!observed.actors[1].origin_is_public_spawn&&exact.paid==2,"new actual-origin history ignores previously surviving spawn index");
    tracker.reset(initial);batch.stamp={1,1,0};small.samples[1].presence=s::Presence::Hidden;small.samples[1].actual_binding=0;batch.samples=std::span<const s::Sample>(small.samples.data(),2);tracker.observe(batch);
    auto replacement=initial;replacement.source.game=305;ResetDuringWork reset_work{&tracker,&replacement,0,2};const auto revoked=tracker.read(initial.source,batch.stamp,{&reset_work,&reset_during_work});
    check(reset_work.calls==2&&revoked.status==s::Status::Stale&&revoked.reason==s::Reason::Source&&!revoked.joint_positions_validated,"reset during sparse candidate callback revokes before accessing reallocated history");
    check(tracker.read(replacement.source,batch.stamp,{nullptr,&pay}).status==s::Status::Empty,"callback replacement source inherits no old observation or active-index authority");
}
static void large_spawn_and_sparse_budget(){static s::Tracker tracker;s::Source source{401,402,403,{0,0}};
    std::array<std::uint8_t,s::mask_bytes> own{},foe{},visible{};own[0]=visible[0]=1;foe.fill(255);foe[0]&=254;
    std::array<s::Actor,6> roster{{{0,0},{1,1},{2,1},{3,1},{4,1},{5,1}}};std::array<s::Sample,6> samples{};for(unsigned a=0;a<6;++a){samples[a].actor=roster[a];samples[a].facts=closed();}
    samples[0].presence=s::Presence::Visible;samples[0].position={0,0};samples[0].actual_binding=100;std::array<s::Spawn,2> spawns{{{0,own},{1,foe}}};
    check(tracker.reset({source,{0,0,0},100,100,roster,spawns,true,true}).status==s::Status::Ready,"9999-cell public spawn remains within admitted index capacity");
    const s::Stamp stamp{1,1,0};tracker.observe({source,stamp,visible,samples,true,true});Budget short_work{2,0};const auto pending=tracker.read(source,stamp,{&short_work,&pay});
    check(pending.assignment==s::Assignment::NeedFuel&&!pending.joint_positions_validated&&short_work.paid==2,"large-spawn partial matching keeps explicit budget Unknown");
    Budget full{6*6*10000+6,0};const auto out=tracker.read(source,stamp,{&full,&pay});check(out.joint_positions_validated&&full.paid<=6*6*10000+6,"large-spawn matching stays bounded and completes with sufficient paid work");
}
int main(){spawn_history();actual_origin_and_refutation();temporal_and_atomic_inputs();malformed_batches();class_facts_are_not_geometry();invalid_initial_and_ownership();joint_assignment_and_funding();full_map_and_revocation();sparse_equivalence();sparse_far_spawn_and_poison();large_spawn_and_sparse_budget();
    std::printf("capital_service_scenario checks %u failures %u\n",checks,failures);return failures?1:0;}
