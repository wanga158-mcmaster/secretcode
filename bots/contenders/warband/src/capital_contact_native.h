#pragma once
#include <cambc.h>
#include "config.h"
#include "capital_contact_packets.h"
#include "capital_channel.h"
#include "capital_bank_native.h"
#include "capital_word_native.h"
#include "capital_work_bridge.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <iostream>
#include <span>
#include <vector>

// Native authority for a deliberately small contact-word family. The model
// ranks a local reply envelope; incomplete coverage never becomes a game proof.
namespace wb::capital::contact_native {
using cambc::vec2;
using cambc::team;
struct Risk {float damage=0;unsigned hits=0;};
struct Environment {
    vec2 objective{};
    void* context=nullptr;
    Risk (*npc)(void*,vec2)=nullptr;
    bool at_spawn=false;
};
struct Result {bool present=false,acted=false,moved=false;int choice=-1;contact::Status status=contact::Status::Invalid;};
inline int distance(vec2 a,vec2 b){return static_cast<int>(a.chebyshev_distance(b));}
inline contact::Point point(vec2 p){return {p.x,p.y};}
inline vec2 point(contact::Point p){return {p.x,p.y};}
struct Belief {contact::Foe foe{};int round=-1;};
struct Memory {
    int round=-1;bool walked=false,exclusive=false,handled=false,ward_attempted=false;
    int audit_stage=0;
    std::array<Belief,16> beliefs{};
    channel::Monitor recall;
    channel::Observation own{};
    std::optional<Snapshot> snapshot;
};
inline Memory& memory(){static Memory value;return value;}
inline void report_words();
inline void audit_stage(int stage){if constexpr(config::phase_audit)memory().audit_stage=stage;}
struct InputAudit {
    unsigned round=0;
    InputAudit(){if constexpr(config::phase_audit)round=cambc::round_number();}
    ~InputAudit() {
        if constexpr(config::phase_audit)if(cambc::round_number()!=round)
            std::cout<<"wb_contact_fuel_stage "<<static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()))
                <<' '<<round<<' '<<cambc::round_number()<<' '<<memory().audit_stage<<' '<<cambc::fuel_remaining()<<'\n';
    }
};
inline bool fuel(unsigned n=10000){return cambc::fuel_remaining()>n;}
inline channel::Observation own_observation(bool at_spawn) {
    channel::Observation o;
    o.owner=static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()));o.round=cambc::round_number();
    cambc::detail::champion_info raw{};cambc::detail::check(cambc::detail::sdk_champion(o.owner,&raw));
    const auto state=cambc::champion_state::from_bits_retain(raw.state);
    o.observed=true;o.alive=state.contains(cambc::champion_state::alive);
    o.position={raw.x,raw.y};o.hp=std::max(0.f,raw.health);o.at_spawn=at_spawn;
    o.channel=state.contains(cambc::champion_state::recalling)?channel::Channel::Recall:
        state.contains(cambc::champion_state::submitting_gems)||state.contains(cambc::champion_state::teleporting)?
        channel::Channel::Other:channel::Channel::None;
    if(o.alive) {
        const auto buffs=detail::buffs(o.owner);o.shield=buffs.shield_now;
        o.stunned=cambc::self().buffs().has_stun();
    }
    return o;
}
inline void observe_input(bool at_spawn) {
    auto& m=memory();const int round=cambc::round_number();
    if(m.round!=round){m.round=round;m.walked=m.exclusive=m.handled=m.ward_attempted=false;m.snapshot.reset();}
    m.own=own_observation(at_spawn);(void)m.recall.observe(m.own);
    if constexpr(config::word_contact_controller)report_words();
    if constexpr(config::contact_service_controller)bank_native::observe_input();
}
inline void invalidate_snapshot(){memory().snapshot.reset();}
inline void note_step(bool accepted,bool ordinary,bool at_spawn) {
    if(!accepted)return;
    auto& m=memory();m.snapshot.reset();if(ordinary)m.walked=true;
    m.own=own_observation(at_spawn);(void)m.recall.observe(m.own);
    (void)m.recall.changed_context(m.own,channel::Change::ActualStep);
    if constexpr(config::contact_service_controller)bank_native::actual_step();
}
inline bool handled(){return memory().handled;}
inline void add_beliefs(Snapshot& s) {
    auto& m=memory();const int now=s.frame.round;
    for(unsigned i=0;i<s.frame.foe_count;++i) {
        const auto id=s.frame.foes[i].id;
        if(id>=0&&id<static_cast<int>(m.beliefs.size()))m.beliefs[id]={s.frame.foes[i],now};
    }
    const auto self=point(s.frame.position);
    for(const auto& old:m.beliefs) {
        if(old.round<0||old.round==now||s.frame.foe_count>=contact::max_foes)continue;
        std::int32_t alive=0;cambc::detail::check(cambc::detail::sdk_champion_is_alive(old.foe.id,&alive));
        if(!alive)continue;
        auto f=old.foe;const int age=std::max(0,now-old.round);
        float dash=0;for(const auto& a:f.abilities)if(a.kind==contact::AbilityKind::Dash)dash=std::max(dash,a.range);
        const int radius=std::min(10000,age*std::max(1,f.move_speed)+static_cast<int>(dash)*((age+7)/8));
        // Once the old location has no local spatial information left, retain
        // unknown coverage rather than make every map tile an absorbing threat.
        if(radius>=distance(self,point(f.position))+3){s.frame.coverage_complete=false;continue;}
        f.position_radius=radius;f.hp=f.max_hp;f.shield=0;f.enhanced=false;f.stun_inputs=0;
        f.represented=false;f.first_fresh=true;f.can_move=f.can_attack=f.can_cast=true;
        if constexpr(config::ordered_contact_controller) {
            const auto window=order_native::window(f.id);f.opportunities=window.admitted?window.scheduled_upper:2;
        }
        for(auto& a:f.abilities)a.cooldown=std::max(0,a.cooldown-age-1);
        s.frame.foes[s.frame.foe_count++]=f;s.frame.coverage_complete=false;
    }
}
inline bool local_recall_quiet(bool at_spawn) {
    auto& m=memory();m.own=own_observation(at_spawn);(void)m.recall.observe(m.own);
    if(!m.own.alive||m.own.stunned)return false;
    const auto p=vec2{m.own.position.x,m.own.position.y};
    const int now=m.own.round,hold=cambc::constants::recall_time+1;
    for(const auto& old:m.beliefs) {
        if(old.round<0)continue;
        std::int32_t alive=0;cambc::detail::check(cambc::detail::sdk_champion_is_alive(old.foe.id,&alive));
        if(!alive)continue;
        const auto& f=old.foe;int reach=f.attack_range;float dash=0;
        for(const auto& a:f.abilities) {
            reach=std::max(reach,static_cast<int>(std::ceil(a.range+a.radius)));
            if(a.kind==contact::AbilityKind::Dash)dash=std::max(dash,a.range);
        }
        const int age=std::max(0,now-old.round);
        if(distance(p,point(f.position))<=reach+(age+hold)*std::max(1,f.move_speed)+static_cast<int>(dash))
            return false;
    }
    // This is conditional on the declared local history, not proof about every
    // never-observed opponent. The failed-channel monitor is independent.
    return m.recall.can_start(m.own,true);
}
inline void note_recall_accepted(){(void)memory().recall.accepted_start(memory().own,true);}
inline bool may_cast(const Snapshot& s,unsigned slot) {
    if(slot>=2||s.self_stunned||!s.self_state.contains(cambc::champion_state::can_cast)
        ||s.self_restrictions.contains(cambc::buff_restrictions::cast))return false;
    const auto& a=s.self_abilities[slot];
    return a.kind!=contact::AbilityKind::None&&a.cooldown==0&&(!a.excludes_walk||!memory().walked);
}
inline cambc::bitboard walking_blockers() {
    auto b=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|
        cambc::map::spawn_region(team::enemy)|cambc::vision::champion_tiles()|
        cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
    b.set(cambc::self().position(),false);return b;
}
enum class Kind {Hold,Move,Basic,Reset,Shield,Spell,Dash,Heal,Submit};
struct Step {Kind kind=Kind::Hold;vec2 destination{};int target=-1,slot=-1;};
struct Plan {
    contact::Choice model{};
    std::array<Step,4> steps{};unsigned count=0;
    std::optional<cambc::champion> ally;
};
inline void step(Plan& p,Step s){if(p.count<p.steps.size())p.steps[p.count++]=s;}
inline void effect(Plan& p,contact::Effect e){if(p.model.effect_count<p.model.effects.size())p.model.effects[p.model.effect_count++]=e;}
inline Plan base(const Snapshot& s,Environment env,vec2 endpoint,contact::Primitive primitive) {
    Plan p;p.model.primitive=primitive;p.model.endpoint=point(endpoint);
    p.model.objective_distance=distance(endpoint,env.objective);p.model.native_legal=true;p.model.prefix_complete=true;
    if(env.npc){const auto risk=env.npc(env.context,endpoint);p.model.npc_damage_upper=detail::upper(risk.damage*s.self_mitigation);p.model.npc_hit_count=risk.hits;}
    return p;
}
inline float separation(const contact::Frame& f,vec2 p) {
    float value=1000;for(unsigned i=0;i<f.foe_count;++i)
        value=std::min(value,float(std::max(0,distance(p,point(f.foes[i].position))-f.foes[i].position_radius)));
    return value;
}
inline void add_basic(Plan& p,const Snapshot& s,unsigned target) {
    step(p,{Kind::Basic,{},static_cast<int>(target),-1});
    const auto own_kind=static_cast<cambc::champion_type>(s.raw_self.type);
    for(unsigned i=0;i<s.enemies.size();++i) {
        if(i!=target&&(own_kind!=cambc::champion_type::varg||
            distance(point(s.frame.foes[i].position),point(s.frame.foes[target].position))>1))continue;
        const float raw=std::max(0.f,std::nextafter(s.raw_self.strength*s.foe_mitigation[i],-std::numeric_limits<float>::infinity()));
        effect(p,{contact::EffectKind::DamageFoe,i,raw,0});
        if(s.self_enhanced&&own_kind==cambc::champion_type::marcella)
            effect(p,{contact::EffectKind::StunFoe,i,0,1});
    }
}
inline bool execute(Plan& plan,Snapshot& s,Environment env) {
    auto& me=cambc::self();const auto round=cambc::round_number();bool acted=false;
    for(unsigned i=0;i<plan.count;++i) {
        if(cambc::round_number()!=round||!fuel(9500)||!me.is_alive()||me.buffs().has_stun())break;
        const auto& a=plan.steps[i];bool accepted=false;
        if(a.kind==Kind::Move) {
            if(!me.can_move()||me.restrictions().contains(cambc::buff_restrictions::move))break;
            const auto blocked=walking_blockers();
            if(!blocked.reachable_tiles(me.position(),me.base_stats().base_move_speed).is_set(a.destination))break;
            accepted=me.move_absolute(a.destination);note_step(accepted,true,env.at_spawn&&me.position()==point(s.frame.position));
        } else if(a.kind==Kind::Basic) {
            if(a.target<0||a.target>=static_cast<int>(s.enemies.size()))break;
            const auto& target=s.enemies[a.target];
            if(!target.is_visible()||!target.is_alive()||!me.can_attack()||
                me.restrictions().contains(cambc::buff_restrictions::attack)||distance(me.position(),target.position())>me.attack_range())break;
            me.attack(target);accepted=true;
        } else if(a.kind==Kind::Submit) {
            if(me.gem_count()==0||distance(me.position(),a.destination)>cambc::constants::submit_gems_range||
                cambc::vision::altar_lock_tiles(team::enemy).is_set(a.destination))break;
            if constexpr(config::contact_service_controller) {
                if(plan.model.continuation_covered)accepted=bank_native::execute(a.destination);
                else {me.submit_gems(a.destination);accepted=true;}
            } else {me.submit_gems(a.destination);accepted=true;}
        } else if(a.slot>=0&&a.slot<2) {
            const auto ability=me.abilities()[a.slot];
            if(!ability.is_ready()||me.restrictions().contains(cambc::buff_restrictions::cast))break;
            const bool exclusive=std::visit([](const auto& x){return x.exclusive_with_movement;},ability.info().value());
            if(exclusive&&memory().walked)break;
            if(a.kind==Kind::Reset||a.kind==Kind::Shield)accepted=ability.use();
            else if(a.kind==Kind::Dash) {accepted=ability.use(a.destination);note_step(accepted,false,false);}
            else if(a.kind==Kind::Heal) {
                if(!plan.ally||!plan.ally->is_visible()||!plan.ally->is_alive()||
                    !ability.valid_tiles().is_set(plan.ally->position()))break;
                accepted=ability.use(*plan.ally);
            } else if(a.kind==Kind::Spell) {
                if(a.target<0||a.target>=static_cast<int>(s.enemies.size()))break;
                const auto& target=s.enemies[a.target];
                if(!target.is_visible()||!target.is_alive()||!ability.valid_tiles().is_set(a.destination))break;
                accepted=ability.can_target_ground()?ability.use(a.destination):ability.use(target);
            }
            if(accepted&&exclusive)memory().exclusive=true;
        }
        if(!accepted)break;memory().snapshot.reset();acted=true;
    }
    return acted;
}
// The new compiler competes under an explicit conditional next-input model.
// It does not turn predicted effects or a known path into an observed receipt.
inline Result act_words(Snapshot& s,Environment env,Plan incumbent,
                        contact::Evaluation incumbent_value,Result out);
struct CampCycleResult {bool claimed=false;Result result{};};
inline CampCycleResult try_camp_cycle(Environment,const work_bridge::Offer&);
inline Result act(Environment env,std::optional<vec2> proposed={}) {
    Result out;auto& m=memory();if(m.handled){out.present=true;return out;}
    InputAudit audit;audit_stage(0);
    const auto entry_round=cambc::round_number();
    const auto stale=[&]() {
        if(cambc::round_number()==entry_round)return false;
        m.handled=true;out.present=true;return true;
    };
    if constexpr(config::contact_service_controller) {
        const auto offer=bank_native::prepare();
        if(stale())return out;
        if(offer.claimed) {
            out.present=true;m.handled=true;
            if(offer.plan) {
                // A complete dated conversion word competes with the existing
                // uncovered escape family. Start is progress, never banked score.
                Plan word;word.model.primitive=contact::Primitive::Submit;
                word.model.endpoint={offer.plan->before.position.x,offer.plan->before.position.y};
                word.model.native_legal=word.model.prefix_complete=true;
                word.model.continuation_covered=true;word.model.continuation_damage_upper=0;
                word.model.channel_started=static_cast<unsigned>(offer.plan->before.bag);
                word.model.displaced_inputs=1;word.model.banked_gems=0;
                step(word,{Kind::Submit,{offer.plan->context.altar.x,offer.plan->context.altar.y},-1,-1});
                Snapshot own;own.frame.owner=offer.plan->before.owner;own.frame.round=offer.plan->before.round;
                own.frame.position=word.model.endpoint;
                out.acted=execute(word,own,env);out.status=contact::Status::Complete;
            }
            return out;
        }
    }
    const auto visible=cambc::vision::champions(team::enemy);
    if(stale())return out;
    bool recent=false;for(const auto& b:m.beliefs)if(b.round>=0&&static_cast<int>(cambc::round_number())-b.round<8)recent=true;
    if(visible.empty()&&!recent) {
        if constexpr(config::word_contact_controller) {
            const auto offer=work_bridge::nominate();
            if(stale())return out;
            if(offer.fresh) {
                auto cycle=try_camp_cycle(env,offer);
                if(cycle.claimed)return cycle.result;
            }
        }
        return out;
    }
    // A deferred known-contact input cannot authorize cheaper macro fallback.
    if(!fuel(55000)){m.handled=true;out.present=true;return out;}
    if(!m.snapshot){audit_stage(1);m.snapshot=capture();if(stale())return out;audit_stage(2);add_beliefs(*m.snapshot);}
    if(stale())return out;
    auto s=*m.snapshot;
    if constexpr(config::word_contact_controller) {
        // This is a fresh post-owner-start shield. Its duration-one value lasts
        // through all pre-next-owner-start replies; that start removes it.
        s.frame.shield=s.self_shield_now;s.frame.shield_valid_through_horizon=true;
    }
    if(!s.frame.fresh||s.frame.round!=static_cast<int>(cambc::round_number())) {m.handled=true;out.present=true;return out;}
    if(!s.frame.foe_count)return out;
    const auto origin=point(s.frame.position);const auto round=cambc::round_number();
    // Price current/ingress exposure before offering a work transition.
    auto exposure_env=env;exposure_env.npc=nullptr;
    auto probe=base(s,exposure_env,proposed.value_or(origin),contact::Primitive::Hold);
    audit_stage(3);
    const auto exposure=contact::detail::evaluate(s.frame,probe.model,false);
    if(cambc::round_number()!=round){out.present=true;m.handled=true;return out;}
    if(exposure.valid&&exposure.damage_upper==0&&!exposure.may_stun_next)return out;
    // Once contact exposure is found, even budget-deferred planning owns this
    // input. It must never fall through into unpriced macro actions.
    out.present=true;m.handled=true;
    std::vector<Plan> plans;plans.reserve(8);
    const bool can_move=s.self_state.contains(cambc::champion_state::can_move)&&
        !s.self_restrictions.contains(cambc::buff_restrictions::move)&&!m.exclusive;
    const bool can_attack=s.self_state.contains(cambc::champion_state::can_attack)&&
        !s.self_restrictions.contains(cambc::buff_restrictions::attack);
    audit_stage(4);auto blocked=walking_blockers();
    std::vector<vec2> landings;if(can_move)landings=blocked.reachable_tiles(origin,s.raw_self.move_speed).positions();
    vec2 away=origin,toward=origin;float far=separation(s.frame,origin);
    for(const auto q:landings) {
        if(q==origin)continue;
        const auto sep=separation(s.frame,q);
        if(sep>far||(sep==far&&distance(q,env.objective)<distance(away,env.objective))){far=sep;away=q;}
        if(distance(q,env.objective)<distance(toward,env.objective))toward=q;
    }
    // Compatible dash plus an ordinary step is a real word. Candidate search
    // uses known path blockers, permits crossing holes, and forbids hole landings.
    int dash_slot=-1;vec2 dash_landing=origin,dash_end=away;
    audit_stage(5);
    for(unsigned slot=0;slot<2;++slot)if(may_cast(s,slot)&&s.self_abilities[slot].kind==contact::AbilityKind::Dash) {
        auto dash_block=cambc::map::walls()|cambc::map::altars()|cambc::map::spawn_region(team::enemy)|
            cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
        dash_block.set(origin,false);const auto holes=cambc::map::holes();
        const auto candidates=dash_block.reachable_tiles(origin,static_cast<unsigned>(s.self_abilities[slot].range)).positions();
        // Nominate one legal dash landing, then compile its ordinary suffix.
        // Enumerating a fresh SDK flood from every possible landing was an
        // unbounded-cost search relative to the measured native turn reserve.
        for(const auto q:candidates)if(q!=origin&&!holes.is_set(q)) {
            const auto sep=separation(s.frame,q);
            if(sep>far||(sep==far&&dash_slot>=0&&distance(q,env.objective)<distance(dash_landing,env.objective))){
                far=sep;dash_slot=slot;dash_landing=q;dash_end=q;
            }
        }
    }
    if(dash_slot>=0&&can_move) {
        const auto holes=cambc::map::holes();
        for(const auto q:blocked.reachable_tiles(dash_landing,s.raw_self.move_speed).positions())
            if(!holes.is_set(q)&&separation(s.frame,q)>separation(s.frame,dash_end))dash_end=q;
    }
    if(dash_slot>=0) {
        auto p=base(s,env,dash_end,contact::Primitive::Dash);step(p,{Kind::Dash,dash_landing,-1,dash_slot});
        if(dash_end!=dash_landing)step(p,{Kind::Move,dash_end,-1,-1});
        plans.push_back(std::move(p));
    } else if(away!=origin) {
        auto p=base(s,env,away,contact::Primitive::Move);step(p,{Kind::Move,away,-1,-1});plans.push_back(std::move(p));
    }
    if(toward!=origin&&toward!=away) {
        auto p=base(s,env,toward,contact::Primitive::Move);step(p,{Kind::Move,toward,-1,-1});plans.push_back(std::move(p));
    }
    // Establish an executable priced incumbent BEFORE optional offensive and
    // ally-response enumeration. Budget deferral keeps this continuation.
    if(plans.empty()) {
        auto p=base(s,env,origin,contact::Primitive::Hold);
        for(unsigned slot=0;slot<2;++slot)if(may_cast(s,slot)&&s.self_abilities[slot].kind==contact::AbilityKind::Shield) {
            p=base(s,env,origin,contact::Primitive::Cast);
            step(p,{Kind::Shield,{},-1,static_cast<int>(slot)});
            effect(p,{contact::EffectKind::ShieldSelf,0,cambc::self().abilities()[slot].shield_amount(),0});break;
        }
        plans.push_back(std::move(p));
    }
    int chosen=0;
    audit_stage(6);
    auto winner=contact::detail::evaluate(s.frame,plans[0].model,s.frame.coverage_complete);
    if(cambc::round_number()!=round||!winner.valid)return out;
    if constexpr(config::word_contact_controller)
        return act_words(s,env,std::move(plans[0]),winner,out);
    auto finish=[&]() {
        audit_stage(10);
        out.choice=chosen;out.status=s.frame.coverage_complete?contact::Status::Complete:contact::Status::Incomplete;
        const auto before=cambc::self().position();out.acted=execute(plans[chosen],s,env);
        out.moved=cambc::self().position()!=before;return out;
    };
    if(!fuel(50000))return finish();
    // A single visible target is nominated; its actual prefix is revalidated
    // after every effect. This nomination is not a full optimal combat search.
    int target=-1;float best=-std::numeric_limits<float>::infinity();
    audit_stage(7);
    for(unsigned i=0;i<s.enemies.size();++i) {
        const auto& f=s.frame.foes[i];
        const float score=2*s.raw_self.strength-(f.hp+f.shield)-20*distance(origin,point(f.position));
        if(score>best){best=score;target=i;}
    }
    if(target>=0) {
        const auto tp=point(s.frame.foes[target].position);
        if(can_attack&&distance(origin,tp)<=s.raw_self.attack_range) {
            auto p=base(s,env,origin,contact::Primitive::Basic);add_basic(p,s,target);
            const float primary_upper=detail::upper(s.raw_self.strength*s.foe_mitigation[target]);
            for(unsigned slot=0;slot<2;++slot)if(s.frame.foes[target].hp+s.frame.foes[target].shield>primary_upper&&
                may_cast(s,slot)&&s.self_abilities[slot].kind==contact::AbilityKind::Reset) {
                step(p,{Kind::Reset,{},-1,static_cast<int>(slot)});add_basic(p,s,target);
                if(s.self_abilities[slot].enhanced_reset)effect(p,{contact::EffectKind::StunFoe,static_cast<unsigned>(target),0,1});
                break;
            }
            plans.push_back(std::move(p));
        }
        for(unsigned slot=0;slot<2;++slot)if(may_cast(s,slot)) {
            const auto& ability=s.self_abilities[slot];
            if(ability.kind==contact::AbilityKind::Shield) {
                auto p=base(s,env,origin,contact::Primitive::Cast);
                step(p,{Kind::Shield,{},-1,static_cast<int>(slot)});
                effect(p,{contact::EffectKind::ShieldSelf,0,cambc::self().abilities()[slot].shield_amount(),0});
                if(can_attack&&distance(origin,tp)<=s.raw_self.attack_range)add_basic(p,s,target);
                plans.push_back(std::move(p));
            } else if(ability.kind==contact::AbilityKind::Damage||ability.kind==contact::AbilityKind::Area||ability.kind==contact::AbilityKind::Stun) {
                if(distance(origin,tp)>ability.range)continue;
                const auto sdk=cambc::self().abilities()[slot];
                if(!sdk.valid_tiles().is_set(tp))continue;
                auto p=base(s,env,origin,contact::Primitive::Cast);step(p,{Kind::Spell,tp,target,static_cast<int>(slot)});
                for(unsigned j=0;j<s.enemies.size();++j)if(j==static_cast<unsigned>(target)||(ability.radius>0&&distance(point(s.frame.foes[j].position),tp)<=ability.radius)) {
                    const float lower=std::max(0.f,std::nextafter(ability.damage*s.foe_mitigation[j],-std::numeric_limits<float>::infinity()));
                    effect(p,{contact::EffectKind::DamageFoe,j,lower,0});
                    if(ability.stun_inputs>0)effect(p,{contact::EffectKind::StunFoe,j,0,ability.stun_inputs});
                }
                const float primary_upper=detail::upper(ability.damage*s.foe_mitigation[target]);
                if(can_attack&&distance(origin,tp)<=s.raw_self.attack_range&&
                    s.frame.foes[target].hp+s.frame.foes[target].shield>primary_upper)add_basic(p,s,target);
                plans.push_back(std::move(p));
            }
        }
    }
    // Actual healing is an immediate effect on a separately evaluated body.
    for(unsigned slot=0;slot<2;++slot)if(may_cast(s,slot)&&s.self_abilities[slot].kind==contact::AbilityKind::Heal) {
        const auto ability=cambc::self().abilities()[slot];const auto mask=ability.valid_tiles();
        std::optional<cambc::champion> ally;float useful=0;
        for(const auto& a:cambc::vision::champions(team::ally))if(static_cast<int>(static_cast<cambc::handle_champion_t>(a))!=s.frame.owner&&a.is_alive()&&mask.is_set(a.position())) {
            const float value=std::min(ability.heal_amount(),a.max_health()-a.health());
            if(value>useful){useful=value;ally=a;}
        }
        if(ally&&useful>0&&plans.size()<contact::max_choices&&fuel(35000)) {
            auto p=base(s,env,origin,contact::Primitive::HealAlly);p.ally=ally;step(p,{Kind::Heal,{},-1,static_cast<int>(slot)});
            auto virtual_frame=s.frame;virtual_frame.owner=static_cast<int>(static_cast<cambc::handle_champion_t>(*ally));
            virtual_frame.position=point(ally->position());virtual_frame.hp=ally->health();virtual_frame.max_hp=ally->max_health();
            const auto buffs=detail::buffs(virtual_frame.owner);virtual_frame.shield=buffs.shield_stable;
            bool represented=true;const float mitigation=detail::mitigation(virtual_frame.owner,false,represented);
            for(unsigned j=0;j<virtual_frame.foe_count;++j) {
                virtual_frame.foes[j].basic_damage*=mitigation/s.self_mitigation;
                for(auto& a:virtual_frame.foes[j].abilities)a.damage*=mitigation/s.self_mitigation;
            }
            auto hold=base(s,env,ally->position(),contact::Primitive::Hold);
            audit_stage(8);
            const auto before=contact::detail::evaluate(virtual_frame,hold.model,virtual_frame.coverage_complete);
            if(cambc::round_number()!=round)return out;
            // Healing changes only the recipient's prefix HP. Reuse the same
            // full reply envelope; its rounded-up damage makes this analytic
            // after-heal lower bound conservative without a second reply tree.
            auto after=before;
            const float stable=virtual_frame.shield_valid_through_horizon?virtual_frame.shield:0.f;
            const double hp_ulp=std::nextafter(virtual_frame.max_hp,std::numeric_limits<float>::infinity())-virtual_frame.max_hp;
            const double shield_ulp=std::nextafter(stable,std::numeric_limits<float>::infinity())-stable;
            const double lower=std::min(virtual_frame.max_hp,virtual_frame.hp+useful)-
                std::max(0.0,static_cast<double>(before.damage_upper)-stable)-
                (static_cast<double>(before.reply_hits)+1)*(hp_ulp+shield_ulp);
            after.hp_lower=lower>0?std::max(0.f,std::nextafter(static_cast<float>(lower),-std::numeric_limits<float>::infinity())):0.f;
            after.survives=after.hp_lower>0;
            // The current NPC callback contains this owner's equipment.
            // Until an actual per-ally packet is supplied, do not credit a
            // rescued ally exposed to any potential NPC footprint.
            const bool ally_npc_clear=!env.npc||env.npc(env.context,ally->position()).damage<=0;
            p.model.ally_effect_covered=ally_npc_clear&&represented&&virtual_frame.coverage_complete&&before.valid&&after.valid;
            p.model.ally_rescues=!before.survives&&after.survives?1:0;p.model.ally_hp_margin=std::max(0.f,after.hp_lower-before.hp_lower);
            plans.push_back(std::move(p));
        }
    }
    plans.push_back(base(s,env,origin,contact::Primitive::Hold));
    if(cambc::round_number()!=round)return out;
    // Each complete reply evaluation is indivisible. The small candidate set
    // and empirical reserve need native measurement; omitted options confer no
    // exhaustive/optimal claim. Escape is deliberately evaluated first.
    const unsigned limit=std::min<unsigned>(6,plans.size());
    for(unsigned i=1;i<limit&&fuel(40000);++i) {
        audit_stage(9);
        const auto evaluation=contact::detail::evaluate(s.frame,plans[i].model,s.frame.coverage_complete);
        if(cambc::round_number()!=round)return out;
        if(evaluation.valid&&(chosen<0||contact::detail::better(plans[i].model,evaluation,plans[chosen].model,winner))) {
            chosen=static_cast<int>(i);winner=evaluation;
        }
    }
    return finish();
}
} // namespace wb::capital::contact_native
#include "capital_words_controller.h"
#include "capital_camp_cycle.h"
