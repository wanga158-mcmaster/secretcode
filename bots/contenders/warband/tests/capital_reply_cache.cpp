#include "capital_reply_cache.h"
#include <cstdio>
#include <type_traits>
namespace c=wb::capital::contact;
namespace r=wb::capital::reply_cache;
static unsigned checks=0,failed=0;
static void check(bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL %s\n",why);}}
static c::Frame frame() {
    c::Frame f;f.owner=2;f.round=33;f.position={20,20};f.hp=750;f.max_hp=1000;f.shield=40;
    f.fresh=f.coverage_complete=f.shield_valid_through_horizon=true;
    f.foe_count=1;auto& e=f.foes[0];e.id=7;e.position={21,20};e.hp=500;e.max_hp=1000;e.basic_damage=40;
    return f;
}
static r::Binding binding(const c::Frame& f,std::uint64_t generation=1){return {f.owner,f.round,generation};}
static r::RiskRequest request(){r::RiskRequest q;q.endpoint={20,20};return q;}
static bool same(const r::RiskCore& a,const c::Evaluation& b) {
    return a.valid==b.valid&&a.prefix_alive==b.prefix_alive&&a.survives==b.survives
        &&a.hp_lower==b.hp_lower&&a.damage_upper==b.damage_upper&&a.reply_hits==b.reply_hits
        &&a.kills==b.kills&&a.may_stun_next==b.may_stun_next;
}
static c::Evaluation oracle(const c::Frame& f,const r::RiskRequest& q) {
    c::Choice x;x.primitive=c::Primitive::Hold;x.endpoint=q.endpoint;x.effects=q.effects;x.effect_count=q.effect_count;
    x.npc_damage_upper=q.npc_damage_upper;x.npc_hit_count=q.npc_hit_count;x.native_legal=x.prefix_complete=true;
    return c::detail::evaluate(f,x,false);
}
static void parity_grid() {
    // Different complete kits, ordered effects, statuses, geometry and NPC
    // envelopes exercise the cache against the independent uncached call.
    for(unsigned kit=0;kit<4;++kit)for(int distance=0;distance<=7;++distance)
    for(unsigned status=0;status<8;++status)for(unsigned effect=0;effect<7;++effect)for(unsigned npc=0;npc<3;++npc) {
        auto f=frame();auto& e=f.foes[0];e.position.x=20+distance;e.opportunities=status%3;
        e.first_fresh=(status&1u)!=0;e.can_move=(status&2u)!=0;e.can_attack=(status&4u)!=0;e.can_cast=status!=7;
        e.stun_inputs=status==6?1:0;f.self_stun_immune=status==5;f.shield_valid_through_horizon=status!=4;
        if(kit==0)e.abilities[0]={c::AbilityKind::Reset,0,8,0,0,0,0,false,false};
        if(kit==1){e.heart=true;e.abilities[1]={c::AbilityKind::Dash,0,8,2,1,150,1,false,false};}
        if(kit==2){e.ranged=true;e.attack_range=3;e.abilities[0]={c::AbilityKind::Reset,0,8,0,0,0,0,false,false};}
        if(kit==3){e.abilities[0]={c::AbilityKind::Area,0,8,4,1,100,1,true,false};e.cooldowns_may_refresh=status==3;}
        auto q=request();q.npc_damage_upper=npc*50.f;q.npc_hit_count=npc;
        if(effect==1){q.effect_count=1;q.effects[0]={c::EffectKind::DamageFoe,0,200,0};}
        if(effect==2){q.effect_count=1;q.effects[0]={c::EffectKind::StunFoe,0,0,1};}
        if(effect==3){q.effect_count=2;q.effects[0]={c::EffectKind::ShieldSelf,0,80,0};q.effects[1]={c::EffectKind::DamageSelf,0,50,0};}
        if(effect==4){q.effect_count=2;q.effects[0]={c::EffectKind::DamageSelf,0,50,0};q.effects[1]={c::EffectKind::ShieldSelf,0,80,0};}
        if(effect==5){q.effect_count=2;q.effects[0]={c::EffectKind::DamageSelf,0,1000,0};q.effects[1]={c::EffectKind::HealSelf,0,1000,0};}
        if(effect==6){q.effect_count=2;q.effects[0]={c::EffectKind::DamageFoe,0,500,0};q.effects[1]={c::EffectKind::HealSelf,0,100,0};}
        r::Session session;const auto b=binding(f);check(session.reset(f,b),"grid valid capture");
        const auto expected=oracle(f,q);const auto first=session.query(b,q),hit=session.query(b,q);
        check(first.evaluated&&!first.reused&&same(first.core,expected),"uncached reply parity");
        check(hit.reused&&!hit.evaluated&&same(hit.core,expected)&&session.evaluations()==1,"exact cached reply parity");
    }
}
static void key_and_lifetime() {
    auto f=frame();auto q=request();auto b=binding(f);r::Session s;
    check(s.reset(f,b),"reset base");check(s.query(b,q).evaluated,"base miss");
    auto changed=q;changed.endpoint.x++;check(s.query(b,changed).evaluated,"endpoint key");
    changed=q;changed.npc_damage_upper=1;check(s.query(b,changed).evaluated,"NPC damage key");
    changed=q;changed.npc_hit_count=1;check(s.query(b,changed).evaluated,"NPC hits key");
    changed=q;changed.effect_count=1;changed.effects[0]={c::EffectKind::DamageFoe,0,1,0};
    check(s.query(b,changed).evaluated,"effect count key");
    changed.effects[0].amount=2;check(s.query(b,changed).evaluated,"effect amount key");
    changed.effects[0].kind=c::EffectKind::StunFoe;check(s.query(b,changed).evaluated,"effect kind key");
    changed.effects[0].stun_inputs=1;check(s.query(b,changed).evaluated,"effect stun key");
    changed.endpoint.y++;check(s.query(b,changed).evaluated&&s.size()==r::capacity,"bounded full cache still evaluates");
    check(s.query(b,changed).evaluated&&!s.query(b,changed).reused,"overflow is uncached");
    check(s.query(b,q).reused,"overflow retains prior exact entries");
    auto wrong=b;wrong.generation++;check(s.query(wrong,q).status==r::Status::WrongBinding,"same owner round wrong generation");
    wrong=b;wrong.owner++;check(s.query(wrong,q).status==r::Status::WrongBinding,"wrong owner");
    wrong=b;wrong.round++;check(s.query(wrong,q).status==r::Status::WrongBinding,"wrong round");
    s.invalidate();check(!s.active()&&s.query(b,q).status==r::Status::Inactive,"attempt revokes old binding");
    check(!s.reset(f,b),"revoked generation cannot rearm old packet");
    // A caller mutation cannot change the captured frame through an alias.
    const auto original=f;b=binding(f,2);check(s.reset(f,b),"copy reset");f.hp=1;f.foes[0].basic_damage=900;
    check(same(s.query(b,q).core,oracle(original,q)),"immutable value frame survives caller mutation");
    for(unsigned field=0;field<16;++field) {
        f=original;
        switch(field) {
        case 0:++f.owner;break;case 1:++f.round;break;case 2:++f.position.x;break;case 3:f.hp=123;break;
        case 4:f.max_hp=2000;break;case 5:f.shield=300;break;case 6:++f.foes[0].position.x;break;
        case 7:f.foes[0].basic_damage=80;break;case 8:f.foes[0].abilities[0]={c::AbilityKind::Reset,1,8,0,0,0,0,false,false};break;
        case 9:f.foes[0].can_attack=false;f.foes[0].first_fresh=false;break;case 10:f.foes[0].hp=1;break;
        case 11:f.foes[0].opportunities=0;break;case 12:f.foes[0].represented=false;f.coverage_complete=false;break;
        case 13:f.self_stun_immune=true;break;case 14:f.foes[0].cooldowns_may_refresh=true;break;
        case 15:f.foes[0].position_radius=4;break;
        }
        b=binding(f,field+3);check(s.reset(f,b)&&s.size()==0,"reset replaces all old entries");
        const auto out=s.query(b,q);check(out.evaluated&&same(out.core,oracle(f,q)),"new capture fields exactly evaluated");
    }
    // All ordered effect fields, including target index, belong to the key.
    f=original;f.foe_count=2;f.foes[1]=f.foes[0];f.foes[1].id=8;f.foes[1].hp=100;
    b=binding(f,50);check(s.reset(f,b),"two foe reset");q=request();q.effect_count=1;q.effects[0]={c::EffectKind::DamageFoe,0,200,0};
    check(s.query(b,q).evaluated,"first foe effect");q.effects[0].foe=1;
    check(s.query(b,q).evaluated&&s.query(b,q).core.kills==1,"foe index key");
    q.effect_count=2;q.effects[0]={c::EffectKind::ShieldSelf,0,100,0};q.effects[1]={c::EffectKind::DamageSelf,0,50,0};
    const auto shield_first=s.query(b,q);std::swap(q.effects[0],q.effects[1]);const auto damage_first=s.query(b,q);
    check(shield_first.evaluated&&damage_first.evaluated&&shield_first.core.hp_lower!=damage_first.core.hp_lower,"effect order key changes physics");
    auto tail=q;tail.effects[c::max_effects-1].amount=17;
    check(s.query(b,tail).reused,"inactive array tail not key");
}
static void validation_and_economics() {
    auto f=frame();auto b=binding(f);auto q=request();r::Session s;check(s.reset(f,b),"validation reset");
    auto bad=q;bad.npc_damage_upper=std::numeric_limits<float>::quiet_NaN();check(s.query(b,bad).status==r::Status::InvalidRequest,"reject NaN before lookup");
    bad=q;bad.effect_count=c::max_effects+1;check(s.query(b,bad).status==r::Status::InvalidRequest,"reject oversize effects");
    bad=q;bad.effect_count=1;bad.effects[0]={c::EffectKind::DamageFoe,1,1,0};check(s.query(b,bad).status==r::Status::InvalidRequest,"reject absent effect foe");
    bad.effects[0].foe=0;bad.effects[0].amount=std::numeric_limits<float>::infinity();check(s.query(b,bad).status==r::Status::InvalidRequest,"reject infinite effect");
    bad.effects[0].amount=1;bad.effects[0].kind=static_cast<c::EffectKind>(99);check(s.query(b,bad).status==r::Status::InvalidRequest,"reject unknown effect kind");
    bad=q;bad.npc_hit_count=513;check(s.query(b,bad).status==r::Status::InvalidRequest,"reject NPC hit overflow");
    for(unsigned field=0;field<8;++field) {
        f=frame();switch(field) {
        case 0:f.fresh=false;break;case 1:f.hp=0;break;case 2:f.foe_count=c::max_foes+1;break;
        case 3:f.foes[0].abilities[0].cooldown=-1;break;case 4:f.foe_count=2;f.foes[1]=f.foes[0];break;
        case 5:f.shield=std::numeric_limits<float>::quiet_NaN();break;case 6:f.position.x=101;break;
        case 7:f.foes[0].opportunities=3;break;
        }
        check(!s.reset(f,binding(f,10+field))&&!s.active()&&s.size()==0,"bad reset revokes previous capture");
    }
    f=frame();b=binding(f);b.generation=0;check(!s.reset(f,b),"zero generation rejected");
    b=binding(f);++b.owner;check(!s.reset(f,b),"capture binding mismatch rejected");
    // Economics can differ while risk physics is identical. RiskCore has no
    // certificate fields and no implicit conversion into a contact Evaluation.
    static_assert(!std::is_convertible_v<r::RiskCore,c::Evaluation>);
    f=frame();f.coverage_complete=false;f.foes[0].represented=false;b=binding(f,30);
    check(s.reset(f,b),"incomplete coverage remains model input");
    q=request();const auto a=s.query(b,q);c::Choice choice;choice.endpoint=q.endpoint;choice.native_legal=choice.prefix_complete=true;
    choice.primitive=c::Primitive::Submit;choice.banked_gems=9;choice.channel_started=1;choice.camp_work=999;
    choice.continuation_covered=true;choice.continuation_damage_upper=1;
    const auto priced=c::detail::evaluate(f,choice,false);const auto reused=s.query(b,q);
    check(reused.reused&&same(a.core,priced),"economics excluded from reply physics");
    check(!priced.horizon_admissible&&!priced.continuation_admissible,"uncached false completeness grants no certificate");
    f=frame();f.foe_count=c::max_foes;f.hp=f.max_hp=1000000000.f;
    for(unsigned i=0;i<f.foe_count;++i){f.foes[i]=frame().foes[0];f.foes[i].id=static_cast<int>(i);f.foes[i].basic_damage=1000000000.f;}
    b=binding(f,31);check(s.reset(f,b),"large valid input reset");const auto aggregate=s.query(b,request());
    check(aggregate.core.valid&&aggregate.core.damage_upper>1000000000.f,"aggregate damage above input cap remains valid");
}
int main(){parity_grid();key_and_lifetime();validation_and_economics();std::printf("capital_reply_cache checks=%u failed=%u\n",checks,failed);return failed?1:0;}
