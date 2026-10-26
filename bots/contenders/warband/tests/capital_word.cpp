#include "capital_word.h"
#include <cstdio>
#include <vector>
using namespace wb::capital::word;
static unsigned checks=0,failures=0;
static void check(bool value,const char* label){++checks;if(!value){if(failures<12)std::printf("FAIL %s\n",label);++failures;}}
static Input scene(bool ranged=false) {
    Input in;in.fresh=in.owner_action_phase=in.effects_complete=in.owner_effects_closed=true;
    auto& s=in.initial;s.owner=3;s.round=20;s.generation=7;s.active=s.m=s.a=s.c=true;s.position={0,0};s.target_count=2;
    s.targets[0]={10,enemy,{1,0},{500,500},{},true,false,0};
    s.targets[1]={11,enemy,{1,1},{500,500},{},true,false,0};
    in.rules.ranged=ranged;in.rules.attack_range=ranged?3:1;in.rules.kill_effects_known=true;
    for(auto& d:in.rules.basic_damage)d={100,100};
    auto& reset=in.rules.abilities[0];reset.kind=AbilityKind::Reset;reset.target_mask=self;
    auto& dash=in.rules.abilities[1];dash.kind=AbilityKind::Dash;dash.target_mask=ground;dash.range=2;dash.radius=1;
    for(auto& d:dash.damage)d={50,50};
    return in;
}
struct Op {Kind kind;int slot=-1,primary=-1;Point destination{};};
static Validation path(const Input& in,std::initializer_list<Op> operations) {
    std::vector<Candidate> candidates;auto state=in.initial;unsigned n=0;
    for(const auto& op:operations) {
        auto c=bind(state,op.kind,op.slot,op.primary,op.destination,n==0?Permission::Observed:Permission::Derived);
        candidates.push_back(c);const auto result=apply_candidate(in,state,c,++n);
        if(!result.admitted)break;state=result.after;
    }
    return validate_word(in,candidates);
}
static Menu nominations(const State& state,void*) {
    Menu out;out.partial=true;
    const auto add=[&](Kind kind,int slot=-1,int primary=-1,Point destination=Point{}) {
        if(out.count<max_candidates)out.candidates[out.count++]=bind(state,kind,slot,primary,destination,
            state.prefix==0?Permission::Observed:Permission::Derived);
    };
    if(state.m)add(Kind::Move,-1,-1,{state.position.x+1,state.position.y});
    for(unsigned i=0;i<state.target_count;++i)if(state.targets[i].hp.lower>0)add(Kind::Basic,-1,static_cast<int>(i));
    if(state.c){add(Kind::Reset,0);add(Kind::Dash,1,-1,{state.position.x+2,state.position.y});}
    return out;
}
int main() {
    // Exhaust initial allowances, attack type, walk provenance and both cast
    // exclusivity/current-cooldown states. No can-move surrogate for walking.
    for(unsigned mask=0;mask<8;++mask)for(unsigned ranged=0;ranged<2;++ranged)
    for(unsigned walked=0;walked<2;++walked)for(unsigned exclusive=0;exclusive<2;++exclusive)
    for(unsigned cd=0;cd<2;++cd) {
        auto in=scene(ranged!=0);auto& s=in.initial;s.m=mask&1;s.a=mask&2;s.c=mask&4;s.walked=walked!=0;
        s.exclusive_cast=exclusive!=0;in.rules.abilities[0].excludes_walk=true;s.cooldown[0]=static_cast<int>(cd);
        auto basic=apply_candidate(in,s,bind(s,Kind::Basic,-1,0,{},Permission::Observed));
        check(basic.admitted==bool(mask&2),"basic follows actual A");
        if(basic.admitted)check(!basic.after.a&&(!ranged||!basic.after.m)&&basic.after.walked==s.walked,"basic consumption preserves actual-walk provenance");
        auto move=apply_candidate(in,s,bind(s,Kind::Move,-1,-1,{0,1},Permission::Observed));
        check(move.admitted==bool((mask&1)&&!exclusive),"move follows M and exclusive cast");
        if(move.admitted)check(!move.after.m&&move.after.walked&&(!ranged||!move.after.a),"ranged walk consumes A and M");
        auto reset=apply_candidate(in,s,bind(s,Kind::Reset,0,-1,{},Permission::Observed));
        check(reset.admitted==bool((mask&4)&&!cd&&!walked),"exclusive reset uses actual walk and slot cooldown");
        if(reset.admitted)check(reset.after.a&&reset.after.m==(s.m&&!in.rules.abilities[0].excludes_walk)&&!reset.after.c&&reset.after.cast_used,"reset restores only A; exclusive cast may consume M");
    }
    auto in=scene();in.initial.targets[0].position={2,0};
    auto v=path(in,{{Kind::Move,-1,-1,{1,0}},{Kind::Basic,-1,0},{Kind::Reset,0},{Kind::Basic,-1,0}});
    check(v.admitted&&v.word.count==4&&v.word.after.targets[0].hp.lower==300,"melee Move Basic Reset Basic");
    check(v.word.trace[0].may_refuse&&v.word.conditional,"observed landing remains a conditional native attempt");
    in=scene(true);in.initial.targets[0].position={2,0};
    v=path(in,{{Kind::Move,-1,-1,{1,0}},{Kind::Reset,0},{Kind::Basic,-1,0}});
    check(v.admitted&&v.word.after.targets[0].hp.lower==400&&!v.word.after.m&&v.word.after.walked,"ranged Move Reset Basic without restored M");
    v=path(in,{{Kind::Move,-1,-1,{1,0}},{Kind::Basic,-1,0}});
    check(!v.admitted&&v.reason==Reason::Resource,"ranged Move Basic requires a reset");
    in=scene();in.initial.targets[0].position={3,0};
    v=path(in,{{Kind::Dash,1,-1,{2,0}},{Kind::Basic,-1,0}});
    check(v.admitted&&v.word.after.targets[0].hp.lower==350&&v.word.after.cast_used,"Dash Basic has actual ordered landing damage");
    check(v.word.after.m&&!v.word.after.walked,"compatible dash does not invent ordinary walk");
    in=scene();v=path(in,{{Kind::Basic,-1,0},{Kind::Dash,1,-1,{0,2}},{Kind::Move,-1,-1,{1,2}}});
    check(v.admitted&&v.word.after.position==Point{1,2}&&v.word.after.targets[0].hp.lower==400,"Basic Dash Move");
    in=scene();v=path(in,{{Kind::Basic,-1,0},{Kind::Reset,0},{Kind::Basic,-1,0}});
    check(v.admitted&&v.word.after.targets[0].hp.lower==300&&v.word.effect_count==2,"standing Basic Reset Basic exact two effects");
    in=scene(true);auto& spell=in.rules.abilities[1];spell.kind=AbilityKind::Damage;spell.target_mask=enemy;spell.excludes_walk=true;spell.range=4;
    for(auto& d:spell.damage)d={120,120};
    v=path(in,{{Kind::Spell,1,0,{1,0}},{Kind::Basic,-1,0}});
    check(v.admitted&&v.word.after.targets[0].hp.lower==280,"stationary exclusive spell then basic");
    v=path(in,{{Kind::Basic,-1,0},{Kind::Spell,1,0,{1,0}}});
    check(v.admitted&&!v.word.after.walked,"M spent by ranged Basic does not bar exclusive cast");
    v=path(in,{{Kind::Move,-1,-1,{0,1}},{Kind::Spell,1,0,{1,0}}});
    check(!v.admitted&&v.reason==Reason::WalkExcluded,"actual ordinary walk bars exclusive cast");
    in=scene();auto& shield=in.rules.abilities[1];shield.kind=AbilityKind::Shield;shield.target_mask=self;shield.shield_after={137,137};
    v=path(in,{{Kind::Shield,1},{Kind::Basic,-1,0}});
    check(v.admitted&&v.word.after.self_shield.lower==137&&v.word.effects[0].kind==EffectKind::ShieldSelf,"shield Basic uses supplied post-cast shield");
    v=path(in,{{Kind::Shield,1},{Kind::Reset,0}});
    check(!v.admitted&&v.reason==Reason::Resource,"two ready slots still permit one successful cast");
    in=scene();in.initial.targets[0].hp={100,100};
    v=path(in,{{Kind::Basic,-1,0},{Kind::Reset,0},{Kind::Basic,-1,0}});
    check(!v.admitted&&v.reason==Reason::Target&&v.word.effect_count==1,"dead primary cannot receive imaginary second basic");
    v=path(in,{{Kind::Basic,-1,0},{Kind::Reset,0},{Kind::Basic,-1,1}});
    check(v.admitted&&v.word.after.targets[0].hp.upper==0&&v.word.after.targets[1].hp.lower==400,"self reset permits explicit live retarget");
    check(v.word.requires_renomination&&v.word.trace[1].renomination_before&&v.word.after.geometry_revision==1,"death marks actual-renomination geometry barrier");
    in.rules.kill_effects_known=false;
    v=path(in,{{Kind::Basic,-1,0},{Kind::Reset,0},{Kind::Basic,-1,1}});
    check(!v.admitted&&v.reason==Reason::UnknownKillEffects,"unrepresented on-kill changes stop old word class");
    in.initial.targets[0].hp={0,0};in.initial.a=false;in.initial.kill_barrier=false;
    v=path(in,{{Kind::Reset,0},{Kind::Basic,-1,1}});
    check(v.admitted&&v.word.after.targets[1].hp.lower==400,"fresh post-kill root can reset then attack another live target");
    in=scene();in.initial.targets[0].hp={100,100};in.rules.basic_damage[0]={90,110};
    v=path(in,{{Kind::Basic,-1,0},{Kind::Reset,0},{Kind::Basic,-1,0}});
    check(!v.admitted&&v.reason==Reason::Target&&v.word.after.targets[0].hp.lower==0&&v.word.after.targets[0].hp.upper==10,"possibly dead primary also requires retarget or fresh observation");
    in=scene();in.initial.targets[0].hp={100,100};in.rules.any_kill_refresh=true;in.initial.cooldown={3,4};
    v=path(in,{{Kind::Basic,-1,0},{Kind::Dash,1,-1,{0,2}}});
    check(v.admitted&&!v.word.after.c&&v.word.after.cast_used,"Basic kill refresh permits previously cooling dash");
    in.initial.targets[1].position={1,2};in.initial.targets[1].hp={50,50};
    v=path(in,{{Kind::Basic,-1,0},{Kind::Dash,1,-1,{0,2}},{Kind::Reset,0}});
    check(!v.admitted&&v.reason==Reason::Resource&&v.word.after.cooldown[0]==0&&!v.word.after.c,"kill cooldown reset never restores spent single cast");
    in=scene();in.rules.abilities[0].enhanced_reset=true;in.rules.enhanced_basic_stun=1;
    v=path(in,{{Kind::Basic,-1,0},{Kind::Reset,0},{Kind::Basic,-1,0}});
    check(v.admitted&&v.word.effect_count==3&&v.word.effects[2].kind==EffectKind::StunTarget&&!v.word.after.enhanced,"reset enhancement consumed by actual next basic");
    in.initial.targets[0].stun_immune=true;v=path(in,{{Kind::Reset,0},{Kind::Basic,-1,0}});
    check(v.admitted&&v.word.effect_count==1,"enhancement cannot stun immune target");
    // Sequential f32 subtraction, not HP <= damage * hits.
    in=scene();in.initial.target_count=1;in.initial.targets[0].hp={649,649};in.rules.basic_damage[0]={129.8f,129.8f};
    for(unsigned hit=1;hit<=6;++hit) {
        v=path(in,{{Kind::Basic,-1,0}});check(v.admitted,"f32 chain actual basic admitted");
        if(hit==5)check(v.word.after.targets[0].hp.lower>0,"649 at129.8 still lives after five f32 subtractions");
        if(hit==6)check(v.word.after.targets[0].hp.upper==0,"sixth f32 subtraction kills");
        in.initial=v.word.after;in.initial.prefix=0;in.initial.generation++;in.initial.round++;in.initial.a=in.initial.m=in.initial.c=true;in.initial.cast_used=in.initial.kill_barrier=false;
    }
    in=scene();auto c=bind(in.initial,Kind::Basic,-1,0,{},Permission::Observed);c.before.generation--;
    auto t=apply_candidate(in,in.initial,c);check(!t.admitted&&t.reason==Reason::StaleDependency&&t.effect_count==0,"stale exact dependency grants no effect");
    c=bind(in.initial,Kind::Move,-1,-1,{1,0},Permission::Blocked);t=apply_candidate(in,in.initial,c);
    check(!t.admitted&&t.effect_count==0&&t.after.position==in.initial.position,"blocked/refused permission grants no modeled motion");
    c=bind(in.initial,Kind::Basic,-1,0,{},Permission::Observed);t=apply_candidate(in,in.initial,c);
    auto stale=bind(t.after,Kind::Reset,0,-1,{},Permission::Observed);
    auto rejected=apply_candidate(in,t.after,stale);check(!rejected.admitted&&rejected.reason==Reason::ObservedSuffix,"current native mask cannot be relabeled as hypothetical suffix permission");
    stale=bind(t.after,Kind::Reset,0);stale.before.geometry_revision++;
    check(!apply_candidate(in,t.after,stale).admitted,"geometry witness bound to exact prefix revision");
    in=scene();in.initial.targets[0].position={2,0};
    const std::array<Pattern,3> patterns{{{{Kind::Move,Kind::Basic,Kind::Reset,Kind::Basic},4},
        {{Kind::Dash,Kind::Basic},2},{{Kind::Basic,Kind::Reset,Kind::Basic},3}}};
    auto compiled=compile_templates(in,patterns,&nominations);
    bool four=false;for(unsigned i=0;i<compiled.count;++i)four|=compiled.words[i].count==4;
    check(compiled.admitted&&four&&compiled.attempted<=64&&compiled.count<=16&&compiled.provider_partial,"named-template compiler reaches depth4 within shared bounded budget");
    auto generic=compile(in,&nominations,nullptr,Limits{64,2,4});
    check(generic.admitted&&generic.count<=2&&generic.attempted<=64&&generic.word_limit,"generic family has explicit bounded omission");
    auto limited=compile_templates(in,patterns,&nominations,nullptr,Limits{1,16,4});
    check(limited.transition_limit&&limited.attempted==1,"transition cap applies across all templates");
    in.fresh=false;check(!compile(in,&nominations).admitted,"stale root never starts compiler");
    in=scene();in.initial.targets[1].id=in.initial.targets[0].id;in.initial.targets[1].kind=camp;
    v=path(in,{{Kind::Basic,-1,0},{Kind::Reset,0},{Kind::Basic,-1,1}});
    check(v.admitted,"camp and champion namespaces may share numeric handle");
    in.initial.targets[1].kind=enemy;check(!compile(in,&nominations).admitted,"duplicate kind-id identity rejected");
    in=scene();in.owner_effects_closed=false;
    check(!compile(in,&nominations).admitted&&compile(in,&nominations).reason==Reason::UnsupportedClass,"unmodeled reactive owner effects do not promise a word");
    std::printf("capital_word: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
