#include "../common/src/mobile-work.h"
#include "../catalyst-forge/src/b-policy.h"
#include <cassert>
#include <cstdio>
using namespace p10;
namespace mobile=p10_mobile;
static mobile::Options both(){mobile::Options o;o.max_traces=2;return o;}
static World base(Champion type=Champion::vendetta){
 World w;w.round=10;w.self.id=1;w.self.pick=0;w.self.type=type;w.self.position={1,1};w.self.visible=w.self.alive=w.self.ally=true;w.self.can_move=w.self.can_attack=w.self.can_cast=true;w.self.hp=w.self.max_hp=1000;w.self.strength=70;w.self.focus=100;w.self.range=1;w.spawn={0,1};return w;
}
static Monster monster(int id,int group,Point p,double hp,double damage=0,int range=0){Monster m;m.id=id;m.group=group;m.position=p;m.hp=m.max_hp=hp;m.strength=damage;m.range=range;m.observed=m.alive=true;return m;}
static Group source(int id,std::vector<Monster>mobs,std::vector<Point>pads){
 Group g;g.id=id;g.monsters=std::move(mobs);g.approach_pads=std::move(pads);g.observed=g.complete=g.route_known=g.living=g.program_available=true;g.travel=0;g.to_bank=2;g.to_spawn=2;g.gems=30;
 for(const auto&m:g.monsters){g.spawns.push_back(m.position);g.hp+=m.hp;g.incoming+=m.strength;}g.position=g.spawns[0];g.approach=g.approach_pads[0];return g;
}
static World square(){
 auto w=base();auto g=source(3,{monster(100,3,{2,1},140,20,3),monster(101,3,{3,1},70),monster(102,3,{2,2},70),monster(103,3,{3,2},70)},{{1,1},{1,2}});w.groups={g};w.visible_open_tiles=g.approach_pads;
 auto&r=w.self.abilities[0];r.reset=r.ready=r.targets_self=true;r.max_cooldown=8;return w;
}
static World line(Champion type=Champion::vendetta,int range=1){
 auto w=base(type);w.self.range=range;w.self.position={0,1};w.groups={source(3,{monster(100,3,{4,1},70)},{{0,1},{1,1},{2,1},{3,1}})};w.visible_open_tiles=w.groups[0].approach_pads;return w;
}
static World single(){auto w=base(Champion::varg);w.groups={source(3,{monster(100,3,{2,1},70)},{{1,1}})};w.visible_open_tiles=w.groups[0].approach_pads;return w;}
static void check_moves(const mobile::Result&r){for(int i=0;i<r.frames_used;++i){const auto&f=r.frames[i];assert(f.casts<=1&&f.basics<=2);if(f.moved){assert(mobile::manhattan(f.from,f.to)==1);assert(f.basics==0&&f.casts==0);}}}
static void clearance_and_prefix(){
 auto w=square();const auto stationary=stationary_work(w,w.groups[0],w.self.position);assert(!stationary.feasible);
 auto budget=mobile::budget_for(w);auto r=mobile::mobile_work(w,w.groups[0],budget,both());assert(r.feasible&&r.clear_turns==4&&r.moves==1&&budget.spent==2);assert(r.frames[0].casts==1&&r.frames[0].basics==2&&r.frames[0].source_hp==210);assert(r.frames[0].first_basic==100&&r.frames[0].second_basic==100);assert(r.first.task==Task::attack&&r.first.monster==100&&r.first.cast_slot==0&&r.first.second_monster==100);assert(fresh_certificate(w,r.first));check_moves(r);
 bool opened=false;for(int i=1;i<r.frames_used;++i)if(r.frames[i].moved){opened=true;assert((r.frames[i].to==Point{2,1}||r.frames[i].to==Point{2,2})&&r.frames[i-1].source_hp<=140);}assert(opened);
 auto exhausted=mobile::mobile_work(w,w.groups[0],budget);assert(!exhausted.feasible&&budget.spent==2&&exhausted.failure==mobile::Failure::trace_budget);
 auto stale=w;stale.round++;assert(!fresh_certificate(stale,r.first));auto stale_budget=mobile::mobile_work(stale,stale.groups[0],budget);assert(stale_budget.failure==mobile::Failure::stale_budget);
 // The actual prefix re-queries readiness: no initial ability allowance means
 // no reset, even though Vendetta's first kill may refresh future cooldowns.
 auto single_trace=mobile::budget_for(w);auto first=mobile::mobile_work(w,w.groups[0],single_trace);assert(first.feasible&&single_trace.spent==1);mobile::Options alternative;alternative.mode=mobile::Mode::open_spawns;assert(mobile::mobile_work(w,w.groups[0],single_trace,alternative).feasible&&single_trace.spent==2);
 auto no_cast=w;no_cast.self.can_cast=false;auto b=mobile::budget_for(no_cast);auto plain=mobile::mobile_work(no_cast,no_cast.groups[0],b);assert(plain.feasible&&plain.frames[0].casts==0&&plain.frames[0].basics==1&&plain.first.cast_slot==-1);
}
static void cardinal_routes_and_occupancy(){
 auto w=line();auto b=mobile::budget_for(w);auto r=mobile::mobile_work(w,w.groups[0],b);assert(r.feasible&&r.first.task==Task::move&&r.first.destination==Point(1,1));assert(r.frames[0].source_hp==70&&r.frames[0].basics==0);check_moves(r);
 auto blocked=w;Actor ally=blocked.self;ally.id=2;ally.position={1,1};blocked.allies={ally};auto bb=mobile::budget_for(blocked);assert(!mobile::mobile_work(blocked,blocked.groups[0],bb).feasible);
 auto overlap=w;ally.position=w.self.position;overlap.allies={ally};auto ob=mobile::budget_for(overlap);assert(mobile::mobile_work(overlap,overlap.groups[0],ob).failure==mobile::Failure::occupied);
 auto enemy=w;ally.position={1,1};ally.ally=false;enemy.enemies={ally};auto eb=mobile::budget_for(enemy);assert(mobile::mobile_work(enemy,enemy.groups[0],eb).failure==mobile::Failure::enemy);
 auto spell_enemy=w;ally.position={5,1};ally.abilities[0].cc=true;ally.abilities[0].range=5;ally.abilities[0].exclusive=true;spell_enemy.enemies={ally};auto se=mobile::budget_for(spell_enemy);assert(mobile::mobile_work(spell_enemy,spell_enemy.groups[0],se).failure==mobile::Failure::enemy);
 auto corner=base();corner.self.position={0,0};corner.groups={source(3,{monster(100,3,{2,2},70)},{{0,0},{1,1}})};auto cb=mobile::budget_for(corner);assert(!mobile::mobile_work(corner,corner.groups[0],cb).feasible);
 corner.groups[0].approach_pads.push_back({1,0});auto legal=mobile::budget_for(corner);auto around=mobile::mobile_work(corner,corner.groups[0],legal);assert(around.feasible&&around.first.destination==Point(1,0));check_moves(around);
 // A currently dead spawn is a usable bridge, unless a current visible body
 // occupies it. No planned partner vacate is credited.
 auto bridge=base();auto dead=monster(100,3,{2,1},0);dead.alive=false;bridge.groups={source(3,{dead,monster(101,3,{3,1},70)},{{1,1}})};auto br=mobile::budget_for(bridge);auto crossing=mobile::mobile_work(bridge,bridge.groups[0],br);assert(crossing.feasible&&crossing.first.task==Task::move&&crossing.first.destination==Point(2,1));
 ally.position={2,1};bridge.allies={ally};auto occupied_bridge=mobile::budget_for(bridge);assert(!mobile::mobile_work(bridge,bridge.groups[0],occupied_bridge).feasible);
}
static void ranged_movement_and_spell_masks(){
 auto w=line(Champion::tavi,3);auto b=mobile::budget_for(w);auto r=mobile::mobile_work(w,w.groups[0],b);assert(r.feasible&&r.clear_turns==2&&r.first.task==Task::move&&r.frames[0].basics==0&&r.frames[0].source_hp==70);check_moves(r);
 // Ground permission must be supplied explicitly from SDK metadata. A ranged
 // cast may reach before a basic, but a ground-only slot is not a camp target.
 auto spell=single();spell.self.type=Champion::stellar;spell.self.range=1;spell.self.strength=30;spell.groups[0].monsters[0].hp=spell.groups[0].monsters[0].max_hp=120;
 auto&a=spell.self.abilities[1];a.aoe=a.ready=true;a.targets_camps=a.targets_self=false;a.range=4;a.radius=1;a.modifier=1.4;a.max_cooldown=8;a.exclusive=true;
 auto none=mobile::budget_for(spell);auto basics=mobile::mobile_work(spell,spell.groups[0],none);assert(basics.feasible&&basics.first.task==Task::attack);
 mobile::Options opts;opts.ground_targets[1]=true;auto allowed=mobile::budget_for(spell);auto cast=mobile::mobile_work(spell,spell.groups[0],allowed,opts);assert(cast.feasible&&cast.clear_turns==1&&cast.first.task==Task::cast&&cast.first.cast_slot==1&&cast.first.monster==-1&&cast.first.destination==Point(2,1));
 a.targets_ground=true;auto observedMask=mobile::budget_for(spell);auto actual=mobile::mobile_work(spell,spell.groups[0],observedMask);assert(actual.feasible&&actual.first.task==Task::cast&&actual.first.monster==-1&&actual.first.destination==Point(2,1));
 spell.self.focus=85;spell.self.can_attack=false;auto boundary_budget=mobile::budget_for(spell);auto boundary=mobile::mobile_work(spell,spell.groups[0],boundary_budget,opts);assert(boundary.feasible&&boundary.clear_turns==2&&boundary.frames[0].source_hp>.9&&boundary.frames[0].source_hp<1.1&&boundary.first.second_monster==-1);
 spell.self.can_attack=true;spell.self.can_move=false;auto walked=mobile::budget_for(spell);auto exclusive=mobile::mobile_work(spell,spell.groups[0],walked,opts);assert(exclusive.feasible&&exclusive.frames[0].casts==0);
}
static Group neighbor(double hp=500,int damage=15){auto n=source(4,{monster(200,4,{5,1},hp,damage,6)},{{4,1}});n.monsters[0].awake=true;return n;}
static void neighbors_and_collateral(){
 auto w=square();w.groups.push_back(neighbor());auto b=mobile::budget_for(w);auto r=mobile::mobile_work(w,w.groups[0],b);assert(r.feasible&&r.hp==1000-15*r.frames_used);for(int i=0;i<r.frames_used;++i)assert(r.frames[i].neighbor_damage==15);
 auto unknown=w;unknown.groups[1].observed=unknown.groups[1].complete=false;auto ub=mobile::budget_for(unknown);auto refused=mobile::mobile_work(unknown,unknown.groups[0],ub);assert(!refused.feasible&&refused.failure==mobile::Failure::unknown_hazard);
 auto hidden=w;hidden.groups[0].monsters[1].observed=false;auto hb=mobile::budget_for(hidden);assert(!mobile::mobile_work(hidden,hidden.groups[0],hb).feasible);
 auto splash=single();splash.self.strength=100;auto n=neighbor(500,30);n.monsters[0].position={3,1};n.monsters[0].range=3;n.monsters[0].awake=false;splash.groups.push_back(n);auto sb=mobile::budget_for(splash);auto woke=mobile::mobile_work(splash,splash.groups[0],sb);assert(woke.feasible&&woke.hp==910);for(int i=0;i<woke.frames_used;++i)assert(woke.frames[i].neighbor_damage==30);
 splash.groups[1].complete=false;auto unsafe=mobile::budget_for(splash);assert(!mobile::mobile_work(splash,splash.groups[0],unsafe).feasible);
}
static void shield_stun_and_death_phases(){
 auto w=single();w.groups.push_back(neighbor());auto&a=w.self.abilities[1];a.shield=a.ready=a.targets_self=true;a.targets_camps=false;a.modifier=2.5;a.duration=1;a.max_cooldown=8;
 auto short_budget=mobile::budget_for(w);auto short_shield=mobile::mobile_work(w,w.groups[0],short_budget);assert(short_shield.feasible&&short_shield.hp==970&&short_shield.first.task==Task::cast&&short_shield.first.cast_slot==1);
 w.self.abilities[1].duration=3;auto long_budget=mobile::budget_for(w);auto long_shield=mobile::mobile_work(w,w.groups[0],long_budget);assert(long_shield.feasible&&long_shield.hp==1000);
 w.self.abilities={};w.self.shield=100;w.self.shield_until=w.round;auto expired_budget=mobile::budget_for(w);auto expired=mobile::mobile_work(w,w.groups[0],expired_budget);assert(expired.feasible&&expired.hp==955);
 w.self.shield_until=w.round+1;auto live_budget=mobile::budget_for(w);auto live=mobile::mobile_work(w,w.groups[0],live_budget);assert(live.feasible&&live.hp==970);
 w.self.shield=0;w.self.shield_until=-1;w.groups[1].monsters[0].stun=3;auto stun_budget=mobile::budget_for(w);auto stunned=mobile::mobile_work(w,w.groups[0],stun_budget);assert(stunned.feasible&&stunned.hp==985);
 w.groups[1].monsters[0].stun=1;auto last_budget=mobile::budget_for(w);assert(mobile::mobile_work(w,w.groups[0],last_budget).hp==955);
 auto cc=single();cc.self.type=Champion::tavi;cc.self.range=3;cc.self.strength=45;auto&mob=cc.groups[0].monsters[0];mob.hp=mob.max_hp=300;mob.strength=40;mob.range=3;
 auto&c=cc.self.abilities[1];c.cc=c.ready=c.targets_camps=true;c.targets_self=false;c.exclusive=true;c.modifier=1;c.range=4;c.duration=3;c.max_cooldown=8;
 auto long_cc=mobile::budget_for(cc);auto safe=mobile::mobile_work(cc,cc.groups[0],long_cc);assert(safe.feasible&&safe.frames[0].casts==1&&safe.frames[0].basics==1&&safe.hp==920);
 cc.self.abilities[1].duration=1;auto short_cc=mobile::budget_for(cc);auto exposed=mobile::mobile_work(cc,cc.groups[0],short_cc);assert(exposed.feasible&&exposed.hp==840);
 // Different member death times remove only their own actual reach/damage.
 auto mixed=base();mixed.groups={source(3,{monster(100,3,{2,1},140,20,1),monster(101,3,{3,1},210,40,4)},{{1,1}})};auto&reset=mixed.self.abilities[0];reset.ready=reset.reset=reset.targets_self=true;reset.max_cooldown=8;
 auto mixed_budget=mobile::budget_for(mixed);auto phases=mobile::mobile_work(mixed,mixed.groups[0],mixed_budget);assert(phases.feasible&&phases.hp==880&&phases.frames[0].camp_damage==40&&phases.frames[3].camp_damage==0);
 // Killing the only damaging member before camp action removes its volley.
 auto kill=single();kill.groups[0].monsters[0].strength=9999;kill.groups[0].monsters[0].range=4;auto kb=mobile::budget_for(kill);auto instant=mobile::mobile_work(kill,kill.groups[0],kb);assert(instant.feasible&&instant.hp==1000);
 kill.groups[0].monsters[0].hp=140;kill.self.hp=10;auto lb=mobile::budget_for(kill);assert(!mobile::mobile_work(kill,kill.groups[0],lb).feasible);
}
static int fuel_calls=0;
static int low_fuel(){return 39000;}
static int falling_fuel(){return ++fuel_calls<15?100000:39000;}
static mobile::Budget* tracked_budget=nullptr;
static int late_falling_fuel(){return tracked_budget&&tracked_budget->spent>=2?39000:100000;}
static void isolated_ranged_origin(){
 auto w=base(Champion::tavi);w.self.position={2,2};w.self.range=3;w.groups={source(3,{monster(100,3,{5,2},70)},{{4,1},{4,2}})};
 auto b=mobile::budget_for(w);auto r=mobile::mobile_work(w,w.groups[0],b);assert(r.feasible&&r.first.task==Task::attack&&r.first.monster==100&&r.moves==0);
 w.groups[0].monsters.push_back(monster(101,3,{6,2},70));w.groups[0].spawns.push_back({6,2});auto blocked=mobile::budget_for(w);assert(!mobile::mobile_work(w,w.groups[0],blocked).feasible);
}
static void constrained_work(){
 auto w=square();w.self.type=Champion::varg;w.self.strength=40;w.groups[0].monsters[0].hp=120;w.groups[0].monsters[1].hp=80;w.groups[0].monsters[2].hp=80;w.groups[0].monsters[3].hp=80;
 mobile::Options o;o.preferred_monster=102;o.suppress_reset=true;auto b=mobile::budget_for(w);auto r=mobile::mobile_work(w,w.groups[0],b,o);assert(r.feasible&&r.first.monster==102&&r.frames[0].casts==0&&r.frames[0].basics==1);for(int i=0;i<r.frames_used;++i)assert(r.frames[i].cast_slot!=0);
 // A missing/out-of-range preference has an explicitly documented fallback;
 // callers must inspect the returned exact target before claiming preparation.
 o.preferred_monster=999;auto f=mobile::budget_for(w);auto fallback=mobile::mobile_work(w,w.groups[0],f,o);assert(fallback.feasible&&fallback.first.monster!=999);
}
static void limits(){
 auto w=square();auto b=mobile::budget_for(w,low_fuel);auto low=mobile::mobile_work(w,w.groups[0],b);assert(!low.feasible&&!low.first.safety_verified&&b.spent==0);
 mobile::SkeletonCache local_cache;auto warm=mobile::budget_for(w);assert(mobile::mobile_work(w,w.groups[0],warm,local_cache).feasible);fuel_calls=0;
 auto mid=mobile::budget_for(w,falling_fuel);auto stopped=mobile::mobile_work(w,w.groups[0],mid,local_cache);assert(!stopped.feasible&&stopped.failure==mobile::Failure::fuel&&mid.spent<=2);
 fuel_calls=0;auto late=mobile::budget_for(w,late_falling_fuel);tracked_budget=&late;auto retained=mobile::mobile_work(w,w.groups[0],late,local_cache,both());assert(retained.feasible&&late.spent==2);tracked_budget=nullptr;
 mobile::Options cap;cap.turn_cap=1;auto capped=mobile::budget_for(w);assert(!mobile::mobile_work(w,w.groups[0],capped,cap).feasible);
 auto input=w;input.geometry_complete=false;auto invalid=mobile::budget_for(input);assert(!mobile::mobile_work(input,input.groups[0],invalid).feasible);
}
static int modeled_fuel=45000;
static int modeled_reader(){const int before=modeled_fuel;modeled_fuel-=100;return before;}
static void trusted_geometry(Group&g){
 auto owner=std::make_shared<SourceGeometry>();owner->source=g.id;owner->spawns=g.spawns;owner->approach_pads=g.approach_pads;
 for(const auto&m:g.monsters)owner->members.push_back({m.id,m.position});g.source_geometry=std::move(owner);
}
static void trusted_forge_geometry_callers(){
 auto w=single();w.self.position={0,1};w.groups[0].approach_pads.clear();for(int y=0;y<8;++y)for(int x=0;x<8;++x)w.groups[0].approach_pads.push_back({x,y});trusted_geometry(w.groups[0]);
 mobile::default_cache={};p10_b_forge::BPolicy forge;forge.set_fuel_reader(modeled_reader);bool productive=false;int warming=0;
 for(int turn=0;turn<80&&!productive;++turn){w.round=40+turn;modeled_fuel=45000;const auto d=forge.step(w,Arm::active);
  assert(forge.fresh_traces<=2);if(d.safety_verified){assert(fresh_certificate(w,d)&&(d.task==Task::move||d.task==Task::attack)&&forge.fresh_traces==1);productive=true;}
  else{assert(d.task==Task::inspect&&forge.fresh_traces<=1);if(mobile::call_diagnostics.failure==mobile::Failure::geometry_warmup)++warming;}
 }
 assert(productive&&warming>1);for(const auto&s:mobile::default_cache.slots)if(s.source==w.groups[0].id)assert(s.ready&&s.geometry_owner==w.groups[0].source_geometry);
 std::printf("Forge trusted-owner caller: %d bounded warmup passes then productive current certified prefix PASS\n",warming);
}
static void same_positioned_trace(const mobile::Result&a,const mobile::Result&b){
 assert(a.feasible&&b.feasible&&a.moves==0&&b.moves==0&&a.clear_turns==b.clear_turns&&a.hp==b.hp&&a.frames_used==b.frames_used);
 assert(a.first.task==b.first.task&&a.first.monster==b.first.monster&&a.first.cast_slot==b.first.cast_slot&&a.first.second_monster==b.first.second_monster);
 for(int i=0;i<a.frames_used;++i){const auto&x=a.frames[i];const auto&y=b.frames[i];assert(x.from==y.from&&x.to==y.to&&x.casts==y.casts&&x.basics==y.basics&&x.first_basic==y.first_basic&&x.second_basic==y.second_basic&&x.hp==y.hp&&x.shield==y.shield&&x.source_hp==y.source_hp&&x.camp_damage==y.camp_damage&&x.neighbor_damage==y.neighbor_damage);}
}
static void positioned_first_income(){
 auto w=single();w.self.strength=40;w.groups[0].monsters[0].hp=120;w.groups[0].approach_pads.clear();for(int y=0;y<8;++y)for(int x=0;x<8;++x)w.groups[0].approach_pads.push_back({x,y});
 mobile::SkeletonCache reference;auto rb=mobile::budget_for(w);const auto full=mobile::mobile_work(w,w.groups[0],rb,reference,both());assert(full.feasible&&rb.spent==2); // ordinary graph ordering retained for opposite-mode requests
 mobile::SkeletonCache untouched;auto fb=mobile::budget_for(w);const auto fast=mobile::mobile_work(w,w.groups[0],fb,untouched);same_positioned_trace(fast,full);assert(fb.spent==1);for(const auto&s:untouched.slots)assert(s.source<0);
 // The real Forge caller used to spend this modeled first-income allowance
 // warming immutable terrain despite an already executable harmless cluster.
 mobile::default_cache={};modeled_fuel=45000;p10_b_forge::BPolicy forge;forge.set_fuel_reader(modeled_reader);const auto d=forge.step(w,Arm::active);assert(d.task==Task::attack&&d.monster==100&&fresh_certificate(w,d)&&forge.fresh_traces==1&&modeled_fuel>=40000);for(const auto&s:mobile::default_cache.slots)assert(s.source<0);
 // The ranged target is itself a possible spell center. A neighbor beyond the
 // origin-only bbox still belongs to the complete collateral catalogue.
 auto spell=base(Champion::stellar);spell.self.range=3;spell.self.strength=20;spell.groups={source(3,{monster(100,3,{4,1},100)},{{3,1}}),source(4,{monster(200,4,{5,1},500,50,5)},{{5,0}})};
 auto&ability=spell.self.abilities[1];ability.ready=ability.damage=ability.aoe=ability.targets_camps=true;ability.targets_self=false;ability.modifier=1;ability.range=3;ability.radius=1;ability.max_cooldown=8;
 mobile::SkeletonCache spell_cache;auto sb=mobile::budget_for(spell);const auto splash=mobile::mobile_work(spell,spell.groups[0],sb,spell_cache);assert(splash.feasible&&splash.first.task==Task::cast&&splash.first.cast_slot==1&&splash.hp==850&&sb.spent==1);for(const auto&s:spell_cache.slots)assert(s.source<0);
 mobile::SkeletonCache spell_reference;auto sr=mobile::budget_for(spell);same_positioned_trace(splash,mobile::mobile_work(spell,spell.groups[0],sr,spell_reference,both()));
 auto unknown=spell;unknown.groups[1].complete=false;auto ub=mobile::budget_for(unknown);const auto refused=mobile::mobile_work(unknown,unknown.groups[0],ub);assert(!refused.feasible&&refused.failure==mobile::Failure::unknown_hazard&&!refused.first.safety_verified);
 auto hurt=spell;hurt.self.hp=100;auto hb=mobile::budget_for(hurt);const auto unsafe=mobile::mobile_work(hurt,hurt.groups[0],hb);assert(!unsafe.feasible&&unsafe.failure==mobile::Failure::health&&!unsafe.first.safety_verified);
 auto low=mobile::budget_for(w,low_fuel);const auto stopped=mobile::mobile_work(w,w.groups[0],low);assert(!stopped.feasible&&stopped.failure==mobile::Failure::fuel&&low.spent==0);
 fuel_calls=0;auto falling=mobile::budget_for(w,falling_fuel);const auto aborted=mobile::mobile_work(w,w.groups[0],falling);assert(!aborted.feasible&&aborted.failure==mobile::Failure::fuel&&!aborted.first.safety_verified&&falling.spent<=1);
}
static void partial_camp_observation_callers(){
 auto w=single();w.self.strength=40;w.groups[0].monsters[0].hp=120;w.observation_complete=false;w.safe_now=false;
 auto unknown=source(4,{monster(200,4,{30,30},2400,60,4)},{{29,30}});unknown.observed=unknown.complete=false;unknown.monsters[0].observed=false;w.groups.push_back(unknown);
 mobile::default_cache={};p10_b_forge::BPolicy forge;const auto d=forge.step(w,Arm::active);
 assert(d.task==Task::attack&&d.group==3&&d.monster==100&&d.safety_verified&&d.local_safety_verified&&fresh_certificate(w,d)&&forge.fresh_traces==1);
 // A global partial camp catalogue is not itself an unknown local hazard.
 // Moving that same positive-Strength unknown into this actor's range is.
 auto reaching=w;reaching.groups[1].monsters[0].position={5,1};reaching.groups[1].spawns={{5,1}};reaching.groups[1].position={5,1};reaching.groups[1].approach={4,1};reaching.groups[1].approach_pads={{4,1}};
 auto rb=mobile::budget_for(reaching);const auto reach=mobile::mobile_work(reaching,reaching.groups[0],rb);assert(!reach.feasible&&reach.failure==mobile::Failure::unknown_hazard&&!reach.first.safety_verified&&!reach.first.local_safety_verified&&rb.spent==1);
 p10_b_forge::BPolicy exposed;const auto no=exposed.step(reaching,Arm::active);assert(no.task!=Task::attack&&!no.safety_verified&&!no.local_safety_verified&&!fresh_certificate(reaching,no));
 // Outside the unknown monster's own range still fails if Varg would strike
 // it as collateral. This checks the complete source-center footprint.
 auto splash=w;splash.groups[1].monsters[0].position={3,1};splash.groups[1].monsters[0].range=0;splash.groups[1].spawns={{3,1}};splash.groups[1].position={3,1};splash.groups[1].approach={3,0};splash.groups[1].approach_pads={{3,0}};
 auto sb=mobile::budget_for(splash);const auto hit=mobile::mobile_work(splash,splash.groups[0],sb);assert(!hit.feasible&&hit.failure==mobile::Failure::unknown_hazard&&!hit.first.safety_verified&&!hit.first.local_safety_verified&&sb.spent==1);
 p10_b_forge::BPolicy collateral;const auto skipped=collateral.step(splash,Arm::active);assert(skipped.task!=Task::attack&&!skipped.safety_verified&&!skipped.local_safety_verified&&!fresh_certificate(splash,skipped));
 auto missing_actors=w;missing_actors.actor_observation_complete=false;auto ab=mobile::budget_for(missing_actors);const auto incomplete=mobile::mobile_work(missing_actors,missing_actors.groups[0],ab);assert(!incomplete.feasible&&ab.spent==0&&!incomplete.first.safety_verified&&!incomplete.first.local_safety_verified);
 p10_b_forge::BPolicy blind;const auto refused=blind.step(missing_actors,Arm::active);assert(blind.fresh_traces==0&&!refused.safety_verified&&!refused.local_safety_verified&&!fresh_certificate(missing_actors,d));
 Directive legacy=farm(3);legacy.safety_verified=true;legacy.safety_round=w.round;legacy.minimum_start_hp=w.self.hp;assert(!fresh_certificate(w,legacy));auto complete=w;complete.observation_complete=true;assert(fresh_certificate(complete,legacy));complete.actor_observation_complete=false;assert(!fresh_certificate(complete,legacy));
}
static void immutable_warmup(){
 auto w=single();w.self.position={0,1};w.groups[0].approach_pads.clear();for(int y=0;y<8;++y)for(int x=0;x<8;++x)w.groups[0].approach_pads.push_back({x,y});
 mobile::SkeletonCache cache;bool completed=false;int warming=0;
 for(int round=0;round<40&&!completed;++round){w.round=20+round;modeled_fuel=45000;auto budget=mobile::budget_for(w,modeled_reader);const auto r=mobile::mobile_work(w,w.groups[0],budget,cache);
  if(r.failure==mobile::Failure::geometry_warmup){++warming;assert(budget.spent==0);}else {assert(r.feasible);assert(budget.spent==1);completed=true;}}
 assert(completed&&warming>1);std::printf("immutable geometry warmup: %d bounded 45k/40k passes; no live snapshot cached\n",warming);int ready_count=0;for(const auto&s:cache.slots)if(s.source==3&&s.ready){++ready_count;assert(s.count<=63);}assert(ready_count==1);
 // Only geometry survives. A new live HP head is over the same immutable graph
 // and must not inherit the old one-hit proof.
 w.groups[0].monsters[0].hp=100000;auto changed=mobile::budget_for(w);auto large=mobile::mobile_work(w,w.groups[0],changed,cache);assert(!large.feasible&&large.failure==mobile::Failure::turn_cap);
 w.groups[0].monsters[0].hp=70;w.groups[0].monsters[0].observed=false;auto hidden=mobile::budget_for(w);assert(!mobile::mobile_work(w,w.groups[0],hidden,cache).feasible);w.groups[0].monsters[0].observed=true;
 // A near current origin attaches cheaply without becoming cached geometry.
 auto near=line();mobile::SkeletonCache near_cache;auto pre=mobile::budget_for(near);assert(mobile::mobile_work(near,near.groups[0],pre,near_cache).feasible);near.self.position={0,2};auto attached=mobile::budget_for(near);auto step=mobile::mobile_work(near,near.groups[0],attached,near_cache);assert(step.feasible&&step.first.task==Task::move&&step.first.destination==Point(0,1));for(const auto&slot:near_cache.slots)if(slot.source==3)assert(slot.count==5);
 mobile::SkeletonCache eviction;for(int id=10;id<15;++id){auto entry=line();entry.groups[0].id=id;entry.groups[0].monsters[0].group=id;auto eb=mobile::budget_for(entry);assert(mobile::mobile_work(entry,entry.groups[0],eb,eviction).feasible);}int filled=0;for(const auto&slot:eviction.slots)if(slot.source>=0){++filled;assert(slot.source!=10);}assert(filled==4);
 // A remote current origin is not inserted into a global routing graph.
 w.self.position={50,50};auto remote=mobile::budget_for(w);assert(mobile::mobile_work(w,w.groups[0],remote,cache).failure==mobile::Failure::blocked);
 // A geometry identity change invalidates the old source skeleton even when
 // the source ID stayed equal; no old path survives a changed immutable pad.
 auto corridor=line();mobile::SkeletonCache keyed;auto initial=mobile::budget_for(corridor);assert(mobile::mobile_work(corridor,corridor.groups[0],initial,keyed).feasible);
 corridor.groups[0].approach_pads={{0,1}};auto removed=mobile::budget_for(corridor);assert(!mobile::mobile_work(corridor,corridor.groups[0],removed,keyed).feasible);
}
static int identity_reads=0;
static int identity_reader(){++identity_reads;return 100000;}
static void trusted_owner_identity(){
 // A retained owner resumes immutable work even while the new live snapshot
 // changes. No HP/awake/stun value is part of the stored geometry identity.
 auto large=line();large.groups[0].approach_pads.clear();for(int y=0;y<8;++y)for(int x=0;x<8;++x)large.groups[0].approach_pads.push_back({x,y});trusted_geometry(large.groups[0]);
 mobile::SkeletonCache resumable;mobile::Skeleton*slot=nullptr;bool complete=false;int copied=0,warming=0;
 for(int turn=0;turn<80&&!complete;++turn){large.round=100+turn;large.groups[0].monsters[0].hp=70+turn;large.groups[0].monsters[0].awake=turn%2;large.groups[0].monsters[0].stun=turn%3;modeled_fuel=45000;auto b=mobile::budget_for(large,modeled_reader);const auto state=mobile::prepare(resumable,large.groups[0],b,slot);
  assert(slot&&slot->geometry_owner==large.groups[0].source_geometry&&slot->copied>=copied);copied=slot->copied;if(state==mobile::Failure::geometry_warmup){++warming;assert(b.spent==0);}else{assert(state==mobile::Failure::none&&slot->ready);complete=true;}}
 assert(complete&&warming>1);identity_reads=0;auto ready=mobile::budget_for(large,identity_reader);assert(mobile::prepare(resumable,large.groups[0],ready,slot)==mobile::Failure::none&&identity_reads==0);
 // Equal counts with a different const object/control block must reset. The
 // cached strong owner keeps the old object alive until that replacement.
 std::weak_ptr<const SourceGeometry>old=large.groups[0].source_geometry;auto replacement=std::make_shared<SourceGeometry>(*large.groups[0].source_geometry);large.groups[0].source_geometry.reset();assert(!old.expired());large.groups[0].source_geometry=replacement;
 auto low=mobile::budget_for(large,low_fuel);assert(mobile::prepare(resumable,large.groups[0],low,slot)==mobile::Failure::geometry_warmup);assert(slot->geometry_owner==replacement&&!slot->ready&&old.expired());
 auto rebuild=mobile::budget_for(large);assert(mobile::prepare(resumable,large.groups[0],rebuild,slot)==mobile::Failure::none&&slot->ready);
 // Mutable geometry arrays may no longer overwrite trusted coordinate data.
 auto w=line();trusted_geometry(w.groups[0]);mobile::SkeletonCache keyed;auto first=mobile::budget_for(w);const auto before=mobile::mobile_work(w,w.groups[0],first,keyed);assert(before.feasible);
 w.groups[0].spawns[0]={99,99};w.groups[0].approach_pads[1]={98,98};auto current=mobile::budget_for(w);const auto owned=mobile::mobile_work(w,w.groups[0],current,keyed);assert(owned.feasible&&owned.first.destination==before.first.destination&&mobile::raw_point(w.groups[0],0).p==Point(4,1));
 w.groups[0].monsters[0].hp=100000;auto hp=mobile::budget_for(w);const auto fresh=mobile::mobile_work(w,w.groups[0],hp,keyed);assert(!fresh.feasible&&fresh.failure==mobile::Failure::turn_cap);w.groups[0].monsters[0].hp=70;
 auto bad_id=w;++bad_id.groups[0].monsters[0].id;auto ib=mobile::budget_for(bad_id);assert(mobile::mobile_work(bad_id,bad_id.groups[0],ib,keyed).failure==mobile::Failure::input&&ib.spent==0);
 auto positioned=single();positioned.self.range=3;trusted_geometry(positioned.groups[0]);positioned.groups[0].monsters[0].position={3,1};auto pb=mobile::budget_for(positioned);assert(mobile::mobile_work(positioned,positioned.groups[0],pb).failure==mobile::Failure::input&&pb.spent==0);
 auto bad_count=w;bad_count.groups[0].spawns.clear();auto sb=mobile::budget_for(bad_count);assert(mobile::mobile_work(bad_count,bad_count.groups[0],sb,keyed).failure==mobile::Failure::input&&sb.spent==0);
 bad_count=w;bad_count.groups[0].approach_pads.push_back({97,97});auto ab=mobile::budget_for(bad_count);assert(mobile::mobile_work(bad_count,bad_count.groups[0],ab,keyed).failure==mobile::Failure::input&&ab.spent==0);
 // Ownerless fixture changes of an earlier copied prefix still invalidate the
 // old path even when the source ID and every vector count stay unchanged.
 auto mutable_world=line();mobile::SkeletonCache mutable_cache;auto mb=mobile::budget_for(mutable_world);assert(mobile::mobile_work(mutable_world,mutable_world.groups[0],mb,mutable_cache).feasible);mutable_world.groups[0].approach_pads[1]={50,50};auto changed=mobile::budget_for(mutable_world);const auto rejected=mobile::mobile_work(mutable_world,mutable_world.groups[0],changed,mutable_cache);assert(!rejected.feasible&&rejected.failure==mobile::Failure::blocked);
 std::puts("trusted geometry: resumable owner identity, replacement/ABA retention, mutable fallback prefix, fresh HP and member binding PASS");
}
int main(){trusted_forge_geometry_callers();trusted_owner_identity();isolated_ranged_origin();constrained_work();positioned_first_income();partial_camp_observation_callers();immutable_warmup();clearance_and_prefix();cardinal_routes_and_occupancy();ranged_movement_and_spell_masks();neighbors_and_collateral();shield_stun_and_death_phases();limits();
 auto incomplete=single();incomplete.self.combat_details_complete=false;auto b=mobile::budget_for(incomplete);
 assert(!mobile::mobile_work(incomplete,incomplete.groups[0],b).feasible&&b.spent==0);
 assert(!solo_program(incomplete.self,incomplete.groups[0],incomplete.self.position).feasible);
 incomplete.self.combat_details_complete=true;incomplete.self.equipment_details_complete=false;b=mobile::budget_for(incomplete);
 assert(!mobile::mobile_work(incomplete,incomplete.groups[0],b).feasible&&b.spent==0);
 assert(!solo_program(incomplete.self,incomplete.groups[0],incomplete.self.position).feasible);
 std::puts("mobile-work: exact prefixes, immutable active geometry, current hazards, missing detail refusal, fuel/caps PASS");return 0;}
