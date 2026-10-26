#include "../src/native_service_execution.h"
#include "../src/native_service_egress.h"
#include <array>
#include <cassert>
#include <cstdio>

namespace N=mosaic::native_service;
struct Fake {
    N::Epoch now{10,1,0};N::SelfFact world{},cached{};N::CampFact camp{},cached_camp{};
    std::array<N::AbilityFact,2> abilities{};
    std::uint64_t remaining{200000},query_cost{50},command_cost{50},pure_cost{},commit_cost{};
    unsigned queries{},commands{},feedbacks{},commits{},hook_effects{},invalidations{},alternative_calls{};
    unsigned fail_feedback{},fail_commit{},false_moves{},false_casts{},overrun_query{},fail_fresh{};
    bool throw_command{},preflight_failure{},command_started{},throw_commit_after_ack{},ack_after_false{},bad_mask{},restrict_after_cast{},kill_on_basic{},invisible_move{};
    std::array<N::Primitive,8> log{};std::uint64_t known_ack{};
    N::EgressState egress{};unsigned recorded_calls{};
    Fake() {
        world.epoch=now;world.id=1;world.pose={10,10};world.hp=world.max_hp=100;
        world.attack_range=1;world.move_speed=1;world.strength=20;world.focus=10;
        world.raw_m=world.raw_a=world.raw_c=world.alive=true;
        world.body_actual=world.restrictions_actual=world.buffs_actual=world.channel_actual=world.walked_actual=true;
        camp.epoch=now;camp.id=7;camp.pose={11,10};camp.hp=camp.max_hp=100;camp.life=N::Life::awake;
        camp.life_actual=camp.body_actual=camp.buffs_actual=true;
        for(auto& a:abilities){a.epoch=now;a.cooldown_actual=true;a.descriptor.actual=a.descriptor.present=true;a.descriptor.range=4;}
        abilities[0].descriptor.kind=N::AbilityKind::reset;
        abilities[1].descriptor.kind=N::AbilityKind::shield;abilities[1].descriptor.target_mask=N::target_self;
    }
    static std::uint64_t clock(void* p){return static_cast<Fake*>(p)->remaining;}
    void recorded(const N::Receipt& receipt,std::uint64_t prefix){
        ++recorded_calls;egress.observe_work({receipt.action.kind,
            receipt.applied==N::Truth::yes?N::EgressEffect::yes:receipt.applied==N::Truth::no?N::EgressEffect::no:N::EgressEffect::unknown,
            receipt.serial,prefix,receipt.before_epoch,receipt.before_pose,{0,0,receipt.action.target_id}});
    }
    N::Budget budget(){return {this,clock,remaining};}
    N::Epoch epoch()const{return now;}
    void spend(std::uint64_t cost){assert(remaining>=cost);remaining-=cost;}
    bool query(N::Budget& b) {
        if(!b.before_query())return false;
        ++queries;spend(overrun_query==queries?b.query_bound+1:query_cost);return b.after_query();
    }
    bool self(N::Budget& b,N::SelfFact& out,bool force=false) {
        if(!force&&cached.actual(now)){out=cached;return true;}
        for(unsigned i=0;i<3;++i)if(!query(b))return false;
        world.epoch=now;out=cached=world;return true;
    }
    bool fresh(const N::Primitive& p,N::Budget& b,N::Validation& out) {
        spend(pure_cost);
        if(fail_fresh&&--fail_fresh==0)return false;
        if(!self(b,out.self))return false;
        if(N::target_primitive(p.kind)){
            if(!cached_camp.actual(now)){for(unsigned i=0;i<3;++i)if(!query(b))return false;camp.epoch=now;cached_camp=camp;}
            out.camp=cached_camp;
        }
        if(p.kind==N::PrimitiveKind::reset||p.kind==N::PrimitiveKind::shield||p.kind==N::PrimitiveKind::cast_unit||p.kind==N::PrimitiveKind::cast_ground){
            if(p.slot>=2)return true;
            if(!query(b))return false;abilities[p.slot].epoch=now;out.ability=abilities[p.slot];
        }
        if(p.kind==N::PrimitiveKind::move){for(unsigned i=0;i<(invisible_move?1u:4u);++i)if(!query(b))return false;
            out.move_actual=out.move_attempt_permitted=true;out.move_epoch=bad_mask?N::Epoch{9,1,0}:now;
            out.occupancy_actual=out.occupancy_open=!invisible_move;}
        if(p.kind==N::PrimitiveKind::bank){
            if(!query(b)||!query(b))return false;out.self.gems_actual=true;out.self.gems=30;
            out.bank_altar_actual=out.bank_lock_actual=out.bank_unlocked=true;out.bank_range=1;
        }
        return true;
    }
    bool attempt(const N::Primitive& p) {
        command_started=false;if(preflight_failure)throw 44;command_started=true;
        assert(commands<log.size());log[commands++]=p;spend(command_cost);
        if(throw_command)throw 42;
        if(p.kind==N::PrimitiveKind::move&&false_moves){--false_moves;return false;}
        if(p.kind!=N::PrimitiveKind::basic&&p.kind!=N::PrimitiveKind::move&&false_casts){--false_casts;return false;}
        switch(p.kind){
        case N::PrimitiveKind::basic:world.raw_a=false;if(world.ranged)world.raw_m=false;
            if(kill_on_basic){camp.life=N::Life::dead;camp.hp=0;}else camp.hp-=20;break;
        case N::PrimitiveKind::reset:world.raw_c=false;world.raw_a=true;abilities[p.slot].cooldown=8;break;
        case N::PrimitiveKind::move:world.raw_m=false;world.pose=p.point;world.ordinary_walked=true;if(world.ranged)world.raw_a=false;break;
        case N::PrimitiveKind::recall:world.recalling=true;world.recall_started=true;world.channel_time=5;world.channel_time_actual=true;break;
        case N::PrimitiveKind::bank:world.submitting=true;world.submit_started=true;world.channel_time=3;world.channel_time_actual=true;break;
        default:world.raw_c=false;abilities[p.slot].cooldown=8;if(restrict_after_cast)world.move_restricted=true;break;
        }
        return true;
    }
    bool attempt_started()const{return command_started;}
    void invalidate(N::EffectScope scope) {
        ++invalidations;++now.attempt;cached.body_actual=false;
        if(scope==N::EffectScope::position_or_damage){++now.revision;cached_camp.life_actual=false;}
    }
    bool feedback(N::Budget& b,N::SelfFact& out) {
        ++feedbacks;if(fail_feedback&&feedbacks==fail_feedback)return false;
        return self(b,out,true);
    }
    bool commit(const N::Receipt& r) {
        ++commits;spend(commit_cost);
        if(fail_commit&&commits==fail_commit){if(ack_after_false){known_ack=r.serial;++hook_effects;}return false;}
        known_ack=r.serial;++hook_effects;if(throw_commit_after_ack)throw 43;return true;
    }
    bool commit_known(std::uint64_t serial)const{return known_ack==serial;}
    bool alternative_move(const N::Primitive&,unsigned refusal,N::Primitive& out) {
        ++alternative_calls;out={N::PrimitiveKind::move,0,{10,refusal%2?9:11},0};return true;
    }
};
static N::Primitive basic(){return {N::PrimitiveKind::basic,7,{11,10},0};}
static N::Primitive reset(){return {N::PrimitiveKind::reset,0,{},0};}
static N::Primitive move(){return {N::PrimitiveKind::move,0,{9,10},0};}
static N::Word word(Fake& f,std::initializer_list<N::Primitive> actions) {
    N::Word w{};w.epoch=f.now;w.target_id=7;w.useful_work=true;
    for(const auto& p:actions){w.actions[w.count++]=p;w.move_after_work=w.move_after_work||p.kind==N::PrimitiveKind::move;}
    return w;
}
static unsigned cases;
static void actual_word() {
    Fake f;N::ExecutionState s;auto b=f.budget();const auto out=N::execute(f,b,s,word(f,{basic(),reset(),basic(),move()}));
    assert(out.status==N::ExecutionStatus::complete&&out.successes==4&&f.commands==4);
    assert(f.log[0].kind==N::PrimitiveKind::basic&&f.log[1].kind==N::PrimitiveKind::reset&&f.log[2].kind==N::PrimitiveKind::basic&&f.log[3].kind==N::PrimitiveKind::move);
    assert(f.world.pose==N::Point(9,10)&&!f.world.raw_m&&!f.world.raw_a&&!f.world.raw_c);
    assert(s.accepted_prefix==4&&s.reverse_count==1&&!s.pending&&f.feedbacks==4&&f.invalidations==4);++cases;
}
static void false_and_alternatives() {
    Fake f;f.false_moves=2;N::ExecutionState s;auto b=f.budget();const auto out=N::execute(f,b,s,word(f,{move()}));
    assert(out.status==N::ExecutionStatus::complete&&out.attempts==3&&out.refusals==2&&out.successes==1);
    // Both FALSE attempts retain M/C and physical debt; the third fresh-mask
    // command can consume the preserved movement.
    assert(!f.world.raw_m&&f.world.raw_c&&s.refusal_debt==2&&s.reverse_count==1&&f.feedbacks==3);++cases;
    Fake g;g.false_moves=1;N::ExecutionState t;auto c=g.budget();const auto done=N::execute(g,c,t,word(g,{move()}));
    assert(done.status==N::ExecutionStatus::complete&&done.attempts==2&&done.successes==1&&t.refusal_debt==1);
    assert(t.reverse_count==1&&t.reverse_history[0].from==N::Point(10,10)&&t.reverse_history[0].to==N::Point(10,9));++cases;
    Fake h;h.false_moves=2;N::ExecutionState u;auto d=h.budget();const auto six=N::execute(h,d,u,word(h,{basic(),reset(),basic(),move()}));
    assert(six.status==N::ExecutionStatus::complete&&six.attempts==6&&six.successes==4&&six.refusals==2&&h.commands==6);
    assert(u.accepted_prefix==4&&u.refusal_debt==2&&u.reverse_count==1&&h.feedbacks==6);++cases;
}
static void false_feedback_pending() {
    Fake f;f.false_moves=1;f.fail_feedback=1;N::ExecutionState s;auto b=f.budget();auto out=N::execute(f,b,s,word(f,{move()}));
    assert(out.status==N::ExecutionStatus::pending&&s.pending&&s.pending_receipt.returned&&s.pending_receipt.applied==N::Truth::no);
    assert(s.refusal_debt==1&&f.world.raw_m&&f.world.raw_c&&f.commands==1);
    // A new work request is superseded by pending closure, in the SAME input.
    const auto closed=N::execute(f,b,s,word(f,{basic()}));
    assert(closed.status==N::ExecutionStatus::reconciled&&closed.feedback_closed&&!s.pending&&f.commands==1&&s.refusal_debt==1);
    assert(s.pending_receipt.unknown_feedback&&s.pending_receipt.applied==N::Truth::no);++cases;
}
static void explicit_shorter_refusal_classes() {
    for(unsigned allowed=0;allowed<3;++allowed){
        Fake f;f.false_moves=2;N::ExecutionState s;auto b=f.budget();
        const auto out=N::execute(f,b,s,word(f,{move()}),{allowed,false});
        assert(out.attempts==allowed+1&&f.feedbacks==allowed+1&&!s.pending);
        assert(out.refusals==std::min(2u,allowed+1)&&s.refusal_debt==out.refusals);
        assert(allowed==2?out.successes==1:out.status==N::ExecutionStatus::refused);++cases;
    }
    Fake f;f.remaining=40000;f.false_moves=1;N::ExecutionState s;auto b=f.budget();
    const auto out=N::execute(f,b,s,word(f,{move()}),{2,true});
    assert(out.status==N::ExecutionStatus::refused&&out.attempts==1&&out.refusals==1);
    assert(f.world.raw_m&&f.world.raw_c&&s.refusal_debt==1&&!s.pending);++cases;
    Fake g;g.remaining=56000;g.false_moves=2;N::ExecutionState t;auto c=g.budget();
    const auto second=N::execute(g,c,t,word(g,{move()}),{2,true});
    assert(second.status==N::ExecutionStatus::refused&&second.attempts==2&&second.refusals==2&&!t.pending);++cases;
}
static void invisible_movement_is_conditional() {
    Fake f;f.invisible_move=true;N::ExecutionState s;auto b=f.budget();
    const auto empty=N::execute(f,b,s,word(f,{move()}),{0,false});
    assert(empty.successes==1&&empty.attempts==1&&f.feedbacks==1&&!s.pending&&!f.world.raw_m);++cases;
    Fake g;g.invisible_move=true;g.false_moves=1;N::ExecutionState t;auto c=g.budget();
    const auto blocked=N::execute(g,c,t,word(g,{move()}),{0,false});
    assert(blocked.status==N::ExecutionStatus::refused&&blocked.attempts==1&&!blocked.successes);
    assert(g.world.raw_m&&g.world.raw_c&&t.refusal_debt==1&&!t.pending&&g.feedbacks==1);++cases;
    Fake h;h.invisible_move=true;h.false_moves=1;N::ExecutionState u;auto d=h.budget();
    const auto retry=N::execute(h,d,u,word(h,{move()}),{1,false});
    assert(retry.successes==1&&retry.attempts==2&&retry.refusals==1&&h.feedbacks==2);
    assert(u.refusal_debt==1&&u.reverse_count==1&&!u.pending&&!h.world.raw_m&&h.world.raw_c);++cases;
}
static void actual_prefix_owes_typed_egress_before_feedback() {
    Fake f;f.fail_feedback=1;N::ExecutionState s;auto b=f.budget();
    const auto work=N::execute(f,b,s,word(f,{basic()}));
    assert(work.status==N::ExecutionStatus::pending&&f.egress.outstanding()&&f.egress.accepted());
    assert(f.recorded_calls==1&&f.egress.latest().serial==s.pending_receipt.serial);++cases;
    const auto closed=N::reconcile(f,b,s);assert(closed.feedback_closed&&!s.pending&&f.recorded_calls==1);
    assert(f.egress.direct_required());++cases;
    f.bad_mask=true;const auto changed=N::execute(f,b,s,word(f,{move()}),{0,false});
    assert(changed.status==N::ExecutionStatus::changed&&f.egress.direct_required());++cases;
    Fake g;g.throw_command=true;N::ExecutionState t;auto c=g.budget();bool threw=false;
    try{N::execute(g,c,t,word(g,{basic()}));}catch(int){threw=true;}
    assert(threw&&g.egress.outstanding()&&g.egress.conditional()&&!g.egress.accepted());
    assert(g.recorded_calls==1&&t.pending&&t.accepted_prefix==0);++cases;
    Fake h;h.false_casts=1;N::ExecutionState u;auto d=h.budget();
    auto cast=word(h,{{N::PrimitiveKind::cast_unit,h.camp.id,h.camp.pose,0}});
    h.abilities[0].descriptor.kind=N::AbilityKind::damage;h.abilities[0].descriptor.target_mask=N::target_camps;
    const auto refused=N::execute(h,d,u,cast);assert(refused.refusals==1&&!h.egress.outstanding());++cases;
}
static void true_feedback_pending() {
    Fake f;f.fail_feedback=1;N::ExecutionState s;auto b=f.budget();const auto out=N::execute(f,b,s,word(f,{basic(),move()}));
    assert(out.status==N::ExecutionStatus::pending&&s.pending&&s.accepted_prefix==1&&f.commands==1);
    ++f.now.round;f.remaining=100000;f.world.pose={20,20};auto c=f.budget();const auto closed=N::execute(f,c,s,word(f,{move()}));
    assert(closed.status==N::ExecutionStatus::reconciled&&f.commands==1&&s.accepted_prefix==1);
    assert(!s.pending_receipt.after_actual&&s.pending_receipt.after_pose==N::Point{});++cases;
}
static void acknowledged_exactly_once() {
    Fake f;f.fail_commit=1;f.ack_after_false=true;N::ExecutionState s;auto b=f.budget();auto out=N::execute(f,b,s,word(f,{basic()}));
    assert(out.status==N::ExecutionStatus::pending&&s.pending&&f.hook_effects==1);
    const auto original=s.pending_receipt;
    out=N::execute(f,b,s,word(f,{move()}));
    assert(out.status==N::ExecutionStatus::reconciled&&f.commits==1&&f.hook_effects==1&&s.accepted_prefix==1);
    assert(s.pending_receipt.after_actual&&s.pending_receipt.after_pose==original.after_pose);++cases;
    Fake g;g.throw_commit_after_ack=true;N::ExecutionState t;auto c=g.budget();bool caught=false;
    try{N::execute(g,c,t,word(g,{basic()}));}catch(int){caught=true;}
    assert(caught&&t.pending&&g.hook_effects==1&&t.accepted_prefix==1);
    g.throw_commit_after_ack=false;const auto closed=N::reconcile(g,c,t);
    assert(closed.status==N::ExecutionStatus::reconciled&&g.commits==1&&g.hook_effects==1);++cases;
    Fake h;h.fail_feedback=1;N::ExecutionState u;auto d=h.budget();N::execute(h,d,u,word(h,{basic()}));
    h.fail_feedback=0;h.fail_commit=1;h.commit_cost=6001;
    const auto poisoned=N::reconcile(h,d,u);
    assert(poisoned.status==N::ExecutionStatus::overrun&&d.poisoned&&u.pending&&u.input_blocked&&u.accepted_prefix==1);++cases;
}
static void actual_false_cast_preserves_c() {
    Fake f;f.false_casts=1;N::ExecutionState s;auto b=f.budget();const N::Primitive shield{N::PrimitiveKind::shield,0,{},1};
    const auto out=N::execute(f,b,s,word(f,{shield,basic()}));
    assert(out.status==N::ExecutionStatus::refused&&out.refusals==1&&f.commands==1&&f.world.raw_m&&f.world.raw_a&&f.world.raw_c);
    assert(s.pending_receipt.applied==N::Truth::no&&!s.pending);++cases;
}
static void actual_restrictions_and_ranged() {
    Fake f;f.restrict_after_cast=true;N::ExecutionState s;auto b=f.budget();const N::Primitive shield{N::PrimitiveKind::shield,0,{},1};
    const auto out=N::execute(f,b,s,word(f,{shield,move()}));
    assert(out.status==N::ExecutionStatus::changed&&f.commands==1&&f.world.raw_m&&f.world.move_restricted);++cases;
    Fake g;g.world.ranged=true;g.world.attack_range=3;N::ExecutionState t;auto c=g.budget();const auto ranged=N::execute(g,c,t,word(g,{basic(),reset(),basic(),move()}));
    assert(ranged.status==N::ExecutionStatus::changed&&ranged.successes==3&&g.commands==3&&!g.world.raw_m);++cases;
}
static void live_fresh_target_and_masks() {
    Fake f;f.kill_on_basic=true;N::ExecutionState s;auto b=f.budget();const auto out=N::execute(f,b,s,word(f,{basic(),reset(),basic(),move()}));
    assert(out.status==N::ExecutionStatus::changed&&f.commands==2&&s.accepted_prefix==2&&s.next==N::NextInput::exit);++cases;
    Fake g;g.camp.life=N::Life::unknown;g.camp.life_actual=false;N::ExecutionState t;auto c=g.budget();const auto hidden=N::execute(g,c,t,word(g,{basic()}));
    assert(hidden.status==N::ExecutionStatus::changed&&g.commands==0);++cases;
    Fake h;h.bad_mask=true;N::ExecutionState u;auto d=h.budget();const auto stale=N::execute(h,d,u,word(h,{move()}));
    assert(stale.status==N::ExecutionStatus::changed&&h.commands==0);++cases;
    Fake j;j.fail_fresh=2;N::ExecutionState v;auto e=j.budget();const auto missing=N::execute(j,e,v,word(j,{basic(),move()}));
    assert(missing.status==N::ExecutionStatus::changed&&missing.applied&&j.commands==1&&missing.next==N::NextInput::exit);++cases;
}
static void abi_and_enablers() {
    Fake f;auto b=f.budget();N::Validation v{};v.self=f.world;v.camp=f.camp;v.ability=f.abilities[0];
    assert(!N::authorize(reset(),v,f.now));v.self.raw_a=false;
    assert(N::authorize(reset(),v,f.now));v.ability.descriptor.kind=N::AbilityKind::shield;
    assert(!N::authorize(reset(),v,f.now));++cases;
    v.ability.descriptor.kind=N::AbilityKind::aoe_damage;v.ability.descriptor.target_mask=N::target_ground;v.ability.descriptor.radius=1;
    const N::Primitive ground{N::PrimitiveKind::cast_ground,7,{11,10},0},unit{N::PrimitiveKind::cast_unit,7,{11,10},0};
    assert(N::authorize(ground,v,f.now)&&!N::authorize(unit,v,f.now));++cases;
    v.ability.descriptor.target_mask|=N::target_camps;assert(!N::authorize(unit,v,f.now));++cases;
    v.ability.descriptor.exclusive_with_movement=true;v.self.walked_actual=false;assert(!N::authorize(ground,v,f.now));
    v.self.walked_actual=true;v.self.ordinary_walked=true;assert(!N::authorize(ground,v,f.now));
    v.self.ordinary_walked=false;assert(N::authorize(ground,v,f.now));
    v.ability.epoch.attempt=1;assert(!N::authorize(ground,v,f.now));assert(b.queries==0);++cases;
}
static void macro_start_and_conditional_move() {
    Fake f;N::ExecutionState s;s.bank_obligation=true;auto b=f.budget();
    const N::Primitive bank{N::PrimitiveKind::bank,0,{11,10},0};
    const auto out=N::execute(f,b,s,N::single_primitive(bank,f.now));
    assert(out.successes==1&&f.commands==1&&s.pending_receipt.submitting_after&&s.pending_receipt.channel_time_after==3);
    assert(s.bank_obligation&&f.world.submitting&&!f.world.completed);++cases;
    auto again=N::execute(f,b,s,N::single_primitive(bank,f.now));assert(!again.attempted&&f.commands==1);++cases;
    Fake g;N::ExecutionState t;auto c=g.budget();const N::Primitive recall{N::PrimitiveKind::recall,0,{},0};
    const auto recalled=N::execute(g,c,t,N::single_primitive(recall,g.now));
    assert(recalled.successes==1&&t.pending_receipt.recalling_after&&t.pending_receipt.channel_time_actual&&g.world.pose==N::Point(10,10));++cases;
    N::Validation v{};v.self=f.world;v.self.submitting=false;v.self.submit_started=false;v.self.gems_actual=true;v.self.gems=30;
    v.bank_altar_actual=v.bank_lock_actual=true;v.bank_unlocked=false;v.bank_range=1;
    assert(!N::authorize(bank,v,f.now));v.bank_unlocked=true;assert(N::authorize(bank,v,f.now));
    v.self.stunned=true;assert(!N::authorize(bank,v,f.now));++cases;
    N::Validation m{};m.self=g.world;m.self.recalling=false;m.move_epoch=g.now;m.move_actual=true;m.move_attempt_permitted=true;
    assert(N::authorize(move(),m,g.now));assert(!m.occupancy_actual&&!m.occupancy_open);
    m.occupancy_actual=true;m.occupancy_open=false;assert(!N::authorize(move(),m,g.now));++cases;
}
static void whole_tail_and_poisoning() {
    Fake f;N::ExecutionState s;auto b=f.budget();auto w=word(f,{basic(),reset(),basic(),move()});
    const auto tail=N::word_tail(w,0,b,2,true);f.remaining=tail+b.pure_bound+b.yield_bound;
    b.entry=b.last=f.remaining;const auto out=N::execute(f,b,s,w);
    assert(out.status==N::ExecutionStatus::unfunded&&f.commands==0&&f.queries==0);++cases;
    Fake g;N::ExecutionState t;auto c=g.budget();g.spend(c.pure_bound+1);
    const auto cold=N::execute(g,c,t,word(g,{basic()}));
    assert(cold.status==N::ExecutionStatus::overrun&&g.queries==0&&g.commands==0&&c.poisoned);++cases;
    Fake h;h.overrun_query=1;N::ExecutionState u;auto d=h.budget();const auto query=N::execute(h,d,u,word(h,{basic()}));
    assert(query.status==N::ExecutionStatus::overrun&&h.commands==0&&d.poisoned);++cases;
    Fake j;j.command_cost=2001;N::ExecutionState v;auto e=j.budget();const auto command=N::execute(j,e,v,word(j,{basic(),move()}));
    assert(command.status==N::ExecutionStatus::overrun&&j.commands==1&&v.pending&&v.accepted_prefix==1);
    assert(v.pending_receipt.applied==N::Truth::yes&&v.pending_receipt.returned);++cases;
    Fake k;auto z=k.budget();assert(z.before_query());k.spend(50);assert(z.after_query());
    k.spend(z.pure_bound+1);assert(!z.before_command()&&z.poisoned);++cases;
    Fake m;m.remaining=100000;N::ExecutionState a;auto mb=m.budget();
    const auto large=N::execute(m,mb,a,word(m,{basic(),reset(),basic(),move()}));
    assert(large.status==N::ExecutionStatus::unfunded&&m.commands==0&&m.queries==0);
    const auto reachable=N::execute(m,mb,a,word(m,{basic(),move()}));
    assert(reachable.status==N::ExecutionStatus::complete&&reachable.successes==2&&m.commands==2&&m.world.pose==N::Point(9,10));++cases;
    Fake n;n.remaining=50000;N::ExecutionState q;auto nb=n.budget();nb.reserve(25000);
    const auto protected_tail=N::execute(n,nb,q,word(n,{basic()}));
    assert(protected_tail.status==N::ExecutionStatus::unfunded&&n.commands==0&&n.queries==0&&nb.held_tail==25000);++cases;
    Fake o;o.remaining=60000;N::ExecutionState r;auto ob=o.budget();ob.reserve(25000);
    const auto paid_tail=N::execute(o,ob,r,word(o,{basic()}));
    assert(paid_tail.successes==1&&ob.held_tail==25000&&o.remaining>25000+ob.yield_bound);++cases;
}
static void error_and_persistence() {
    Fake f;f.throw_command=true;N::ExecutionState s;s.bank_obligation=true;auto b=f.budget();bool caught=false;
    try{N::execute(f,b,s,word(f,{basic()}));}catch(int){caught=true;}
    assert(caught&&s.pending&&s.pending_receipt.attempted&&!s.pending_receipt.returned&&s.pending_receipt.applied==N::Truth::unknown);
    assert(f.invalidations==1&&b.poisoned&&f.commands==1&&s.accepted_prefix==0);++cases;
    f.throw_command=false;++f.now.round;f.remaining=100000;auto c=f.budget();const auto out=N::execute(f,c,s,word(f,{basic()}));
    assert(out.status==N::ExecutionStatus::reconciled&&f.commands==1&&s.bank_obligation&&s.accepted_prefix==0);++cases;
    Fake g;g.false_moves=1;N::ExecutionState t;auto d=g.budget();N::execute(g,d,t,word(g,{move()}));
    const auto prefix=t.accepted_prefix;const auto debt=t.refusal_debt;const auto edge=t.reverse_history[0];t.bank_obligation=true;
    ++g.now.round;t.begin(g.now.round);
    assert(t.accepted_prefix==prefix&&t.refusal_debt==debt&&t.bank_obligation&&t.reverse_history[0].serial==edge.serial);++cases;
    Fake h;h.preflight_failure=true;N::ExecutionState u;auto e=h.budget();bool stopped=false;
    try{N::execute(h,e,u,word(h,{basic()}));}catch(int){stopped=true;}
    assert(stopped&&h.commands==0&&u.input_attempts==0&&!u.pending&&!u.pending_receipt.attempted&&u.pending_receipt.applied==N::Truth::no);
    assert(h.invalidations==0&&u.input_blocked&&e.poisoned);++cases;
    Fake j;j.throw_command=true;N::ExecutionState v;auto z=j.budget();
    try{N::execute(j,z,v,word(j,{move()}));}catch(int){}
    ++j.now.round;j.remaining=200000;j.throw_command=false;auto next=j.budget();
    const auto unknown=N::reconcile(j,next,v);
    assert(unknown.status==N::ExecutionStatus::reconciled&&v.reverse_uncertain&&v.reverse_count==0&&v.accepted_prefix==0);++cases;
}
static void bounded_history_is_not_complete_history() {
    Fake f;N::ExecutionState s;
    for(unsigned turn=0;turn<65;++turn){
        ++f.now.round;f.remaining=100000;f.world.raw_m=true;f.world.ordinary_walked=false;f.commands=0;
        auto b=f.budget();const N::Primitive step{N::PrimitiveKind::move,0,{f.world.pose.x-1,f.world.pose.y},0};
        const auto out=N::execute(f,b,s,word(f,{step}));assert(out.successes==1&&!s.pending);
    }
    assert(s.accepted_prefix==65&&s.reverse_count==64&&s.reverse_truncated);++cases;
}
int main() {
    actual_word();false_and_alternatives();explicit_shorter_refusal_classes();invisible_movement_is_conditional();actual_prefix_owes_typed_egress_before_feedback();false_feedback_pending();true_feedback_pending();acknowledged_exactly_once();
    actual_false_cast_preserves_c();actual_restrictions_and_ranged();live_fresh_target_and_masks();abi_and_enablers();macro_start_and_conditional_move();whole_tail_and_poisoning();error_and_persistence();bounded_history_is_not_complete_history();
    std::printf("native service execution: %u behavior/resource cases passed\n",cases);
}
