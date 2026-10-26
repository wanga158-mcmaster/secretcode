#pragma once
#include "capital_word_execute.h"
#include "capital_word_arbitration.h"
#include <bit>

namespace wb::capital::contact_native {
struct WordCounters {
    unsigned inputs=0,offers=0,ready=0,compiled=0,selected=0,accepted=0,refused=0,kills=0,completed=0,stopped=0,deferred=0;
    unsigned evaluated=0,work_models=0,known_exits=0,priced_exits=0,model_survives=0,possible_stun=0,quote_reuses=0;
    std::uint64_t scene_cost=0,compile_cost=0,value_cost=0,prefix_cost=0,execute_cost=0,log_cost=0;
    std::array<unsigned,7> rejected{};
    float actual_hp_work=0;
    int logged_round=-1;unsigned receipt_logs=0;
};
inline WordCounters& word_counters(){static WordCounters value;return value;}
inline void report_words() {
    auto& c=word_counters();const int now=cambc::round_number();
    if(now%128||c.logged_round==now||!fuel(24000))return;
    c.logged_round=now;
    const auto before=cambc::fuel_remaining();char line[768];unsigned used=0;
    for(const char* p="wb_words_v2";*p;++p)line[used++]=*p;
    const auto append=[&](std::uint64_t value) {
        char digits[20];unsigned count=0;
        do {digits[count++]=static_cast<char>('0'+value%10);value/=10;}while(value);
        if(used+count+2>=sizeof(line))return;
        line[used++]=' ';while(count)line[used++]=digits[--count];
    };
    append(static_cast<std::uint64_t>(static_cast<cambc::handle_champion_t>(cambc::self())));append(now);
    for(const auto value:{c.inputs,c.offers,c.ready,c.compiled,c.selected,c.accepted,c.refused,c.kills,
        std::bit_cast<std::uint32_t>(c.actual_hp_work),c.completed,c.stopped,c.deferred})append(value);
    for(const auto value:{c.scene_cost,c.compile_cost,c.value_cost,c.prefix_cost,c.execute_cost,c.log_cost})append(value);
    for(const auto value:{c.evaluated,c.work_models,c.known_exits,c.priced_exits,c.model_survives,c.possible_stun,c.quote_reuses})append(value);
    for(const auto value:c.rejected)append(value);
    line[used++]='\n';std::cout.write(line,used);
    if(cambc::round_number()==static_cast<unsigned>(now))c.log_cost=std::max(c.log_cost,before-cambc::fuel_remaining());
}
inline contact::Choice word_effects(const Snapshot& snapshot,Environment env,
                                   const word_native::Scene& scene,const word::Word& word) {
    auto result=base(snapshot,env,{word.after.position.x,word.after.position.y},contact::Primitive::Basic).model;
    result.displaced_inputs=1;
    for(unsigned i=0;i<word.effect_count&&result.effect_count<result.effects.size();++i) {
        const auto& e=word.effects[i];
        if(e.kind==word::EffectKind::ShieldSelf) {
            result.effects[result.effect_count++]={contact::EffectKind::ShieldSelf,0,e.amount.lower,0};continue;
        }
        if(e.target>=scene.input.initial.target_count)continue;
        const auto& target=scene.bindings[e.target];
        if(target.kind!=word::enemy||target.enemy_index<0)continue;
        result.effects[result.effect_count++]={e.kind==word::EffectKind::DamageTarget?
            contact::EffectKind::DamageFoe:contact::EffectKind::StunFoe,
            static_cast<unsigned>(target.enemy_index),e.amount.lower,e.stun_inputs};
    }
    if(scene.camp_index>=0) {
        const auto index=static_cast<unsigned>(scene.camp_index);
        result.camp_work=std::max(0.f,scene.input.initial.targets[index].hp.lower-word.after.targets[index].hp.upper);
        const int killed=word.after.targets[index].hp.upper<=0?scene.bindings[index].id:-1;
        if(work_bridge::npc_quiet({word.after.position.x,word.after.position.y},killed)) {
            result.npc_damage_upper=0;result.npc_hit_count=0;
        }
    }
    // Completion after refusal and future economic receipts are not supplied
    // by this compiler. Its predictions retain their conditional model type.
    result.continuation_covered=false;
    return result;
}
inline contact::Evaluation refused_prefixes(const Snapshot& snapshot,Environment env,
                                          std::span<const word::Word> words) {
    auto frame=snapshot.frame;
    const auto terminal=point(snapshot.frame.position);
    auto envelope=base(snapshot,env,terminal,contact::Primitive::Hold).model;
    // A refusal may leave any prior actual prefix. Omit every beneficial own
    // effect and union the per-foe closest prefix distances. This is deliberately
    // a damage/interrupt upper envelope, not one simultaneous enemy policy.
    std::array<vec2,13> poses{};unsigned count=1;poses[0]=terminal;
    for(const auto& word:words)for(unsigned i=0;i<word.count;++i) {
        const auto& c=word.trace[i].candidate;
        if(c.kind==word::Kind::Move||c.kind==word::Kind::Dash) {
            const auto from=c.before.position;const int dx=c.destination.x-from.x,dy=c.destination.y-from.y;
            const int length=std::max(std::abs(dx),std::abs(dy));
            // Supported native rays are at most two cells. Include an actual
            // partial endpoint as well as a refused, unstarted movement.
            for(int at=1;at<=length&&count<poses.size();++at)
                poses[count++]={from.x+dx*at/length,from.y+dy*at/length};
        }
    }
    for(unsigned foe=0;foe<frame.foe_count;++foe) {
        auto& f=frame.foes[foe];int closest=distance(terminal,point(f.position));
        for(unsigned i=0;i<count;++i)closest=std::min(closest,distance(poses[i],point(f.position)));
        f.position_radius+=distance(terminal,point(f.position))-closest;
        f.hp_may_fall=true;f.stun_inputs=0;
    }
    if(env.npc)for(unsigned i=0;i<count;++i) {
        const auto risk=env.npc(env.context,poses[i]);
        envelope.npc_damage_upper=std::max(envelope.npc_damage_upper,detail::upper(risk.damage*snapshot.self_mitigation));
        envelope.npc_hit_count=std::max(envelope.npc_hit_count,risk.hits);
    }
    return contact::detail::evaluate(frame,envelope,false);
}
inline Result act_words(Snapshot& snapshot,Environment env,Plan incumbent,
                        contact::Evaluation incumbent_value,Result out) {
    auto& counters=word_counters();++counters.inputs;
    const auto epoch=cambc::round_number();
    word_native::Scene* active_scene=nullptr;
    auto fallback=[&]() {
        if(cambc::round_number()!=epoch)return out;
        if(active_scene)word_native::invalidate(*active_scene);
        const auto before=cambc::self().position();out.acted=execute(incumbent,snapshot,env);
        out.moved=cambc::self().position()!=before;out.status=contact::Status::Incomplete;
        report_words();return out;
    };
    if(!fuel(51000))return fallback();
    auto work=work_bridge::nominate();if(work.fresh)++counters.offers;
    if(cambc::round_number()!=epoch)return out;
    word_native::Options options;options.require_known_paths=true;options.dispatch_reserve=30000;
    // Root-move attack views are optional funded observations, not pure-search
    // SDK calls or a reason to abandon the already priced escape incumbent.
    options.nominate_root_moves=fuel(74000);options.hypothetical_visions=options.nominate_root_moves?2:0;
    word_native::Scene scene;
    active_scene=&scene;
    auto mark=cambc::fuel_remaining();
    const auto prepared=word_native::prepare(scene,snapshot,{memory().walked,memory().exclusive},
        {env.objective.x,env.objective.y},work.fresh?&*work.target:nullptr,options);
    if(cambc::round_number()!=epoch)return out;
    counters.scene_cost=std::max(counters.scene_cost,mark-cambc::fuel_remaining());
    if(prepared!=word_native::Status::Ready) {
        const auto reason=static_cast<unsigned>(prepared);if(reason<counters.rejected.size())++counters.rejected[reason];
        return fallback();
    }
    ++counters.ready;
    auto& workspace=word_native::workspace();
    word_native::WorkspaceLease lease(workspace,scene);
    if(!lease)return fallback();
    mark=cambc::fuel_remaining();
    const auto& words=word_native::compile_into(scene,lease,{24,6,4},[](void*){return fuel(42000);});counters.compiled+=words.count;
    if(!word_native::epoch_matches(scene))return out;
    counters.compile_cost=std::max(counters.compile_cost,mark-cambc::fuel_remaining());
    if(scene.compile_deferred)++counters.deferred;
    const unsigned considered=std::min(3u,words.count);
    using namespace word_arbitration;
    const Horizon horizon{snapshot.frame.owner,snapshot.frame.round,snapshot.frame.round+1,
        scene.input.initial.generation,true};
    Offer winner;winner.choice=incumbent.model;winner.evaluation=incumbent_value;winner.horizon=horizon;
    int selected=-1;
    for(unsigned i=0;i<considered&&fuel(36000);++i) {
        const auto& word=words.words[i];
        Offer candidate;candidate.choice=word_effects(snapshot,env,scene,word);candidate.horizon=horizon;
        candidate.modeled_effect_count=word.effect_count;
        mark=cambc::fuel_remaining();
        candidate.evaluation=contact::detail::evaluate(snapshot.frame,candidate.choice,
            snapshot.frame.coverage_complete&&word.effects_complete);
        if(cambc::round_number()!=epoch)return out;
        counters.value_cost=std::max(counters.value_cost,mark-cambc::fuel_remaining());
        if(!candidate.evaluation.valid)continue;
        ++counters.evaluated;if(candidate.evaluation.survives)++counters.model_survives;
        if(candidate.evaluation.may_stun_next)++counters.possible_stun;
        contact::Evaluation prefix_value;
        // Sibling geometry cannot veto this nominated word's failure branches.
        // Price only this word, retaining complete own effects separately above.
        bool same_reply=candidate.choice.effect_count==0&&candidate.choice.camp_work>0&&
            work_bridge::npc_quiet(point(snapshot.frame.position));
        for(unsigned step=0;step<word.count&&same_reply;++step) {
            const auto kind=word.trace[step].candidate.kind;
            same_reply=kind!=word::Kind::Move&&kind!=word::Kind::Dash;
        }
        if(same_reply) {
            // Stationary camp-only effects, with every possible NPC source
            // out of range, leave this exact reply frame unchanged on refusal.
            prefix_value=candidate.evaluation;prefix_value.horizon_admissible=prefix_value.continuation_admissible=false;
            ++counters.quote_reuses;
        } else if((candidate.choice.camp_work>0||candidate.evaluation.kills)&&fuel(40000)) {
            mark=cambc::fuel_remaining();
            prefix_value=refused_prefixes(snapshot,env,std::span<const word::Word>{&word,1});
            if(cambc::round_number()!=epoch)return out;
            counters.prefix_cost=std::max(counters.prefix_cost,mark-cambc::fuel_remaining());
        }
        if(scene.camp_index>=0&&candidate.choice.camp_work>0) {
            ++counters.work_models;
            const int killed=word.after.targets[scene.camp_index].hp.upper<=0?scene.bindings[scene.camp_index].id:-1;
            bool exit=word_native::whole_path_known_free(scene,word)&&
                work_bridge::npc_quiet({word.after.position.x,word.after.position.y},killed);
            if(exit)++counters.known_exits;
            exit=exit&&prefix_value.valid&&prefix_value.survives&&!prefix_value.may_stun_next;
            if(exit)++counters.priced_exits;
            if(cambc::round_number()!=epoch)return out;
            auto& service=candidate.service;service.kind=ServiceKind::CampWork;service.horizon=horizon;
            service.target_kind=TargetKind::Camp;service.target_id=scene.bindings[scene.camp_index].id;
            service.effect_count=word.effect_count;service.quantity=candidate.choice.camp_work;
            service.modeled_useful=service.dated=service.bound=service.persistent=true;
            service.exit_known=service.exit_priced=exit;service.exit_damage_upper=0;
        } else if(candidate.evaluation.kills&&prefix_value.valid&&prefix_value.survives&&!prefix_value.may_stun_next) {
            for(unsigned target=0;target<scene.input.initial.target_count;++target)
                if(scene.bindings[target].kind==word::enemy&&scene.input.initial.targets[target].hp.lower>0&&
                   word.after.targets[target].hp.upper<=0) {
                    auto& service=candidate.service;service.kind=ServiceKind::EnemyKill;service.horizon=horizon;
                    service.target_kind=TargetKind::Enemy;service.target_id=scene.bindings[target].id;
                    service.effect_count=word.effect_count;service.quantity=1;
                    service.modeled_useful=service.dated=service.bound=service.persistent=true;break;
                }
        }
        if(better(candidate,winner,Class{true})){winner=candidate;selected=static_cast<int>(i);}
    }
    if(selected<0)return fallback();
    ++counters.selected;
    word_native::Provenance provenance{memory().walked,memory().exclusive};
    mark=cambc::fuel_remaining();
    const auto result=word_execute::execute(scene,words.words[selected],provenance);
    if(cambc::round_number()==epoch)counters.execute_cost=std::max(counters.execute_cost,mark-cambc::fuel_remaining());
    memory().walked=provenance.walked;memory().exclusive=provenance.exclusive_cast;invalidate_snapshot();
    if(result.moved&&cambc::round_number()==epoch&&fuel(15000))note_step(true,false,false);
    out.acted=result.acted;out.moved=result.moved;out.choice=selected;
    out.status=contact::Status::Incomplete;
    if(result.acted)++counters.accepted;if(result.refused)++counters.refused;if(result.killed)++counters.kills;
    if(result.stop==word_execute::Stop::Complete)++counters.completed;else ++counters.stopped;
    if(work.fresh&&work.target&&cambc::round_number()==epoch) {
        const auto id=static_cast<int>(static_cast<cambc::handle_camp_t>(*work.target));
        for(unsigned i=0;i<result.camp_receipt_count;++i) {
            const auto& receipt=result.camp_receipts[i];
            if(!receipt.observed||receipt.camp_id!=id||receipt.hp_damage<=0)continue;
            work_bridge::commit(work,receipt.hp_before,receipt.hp_after);
            counters.actual_hp_work+=receipt.hp_damage;
            if(counters.receipt_logs<4&&fuel(22000)) {
                ++counters.receipt_logs;unsigned signature=0;
                for(unsigned j=0;j<words.words[selected].count;++j)
                    signature=signature*8+static_cast<unsigned>(words.words[selected].trace[j].candidate.kind)+1;
                std::cout<<"wb_word_receipt "<<snapshot.frame.owner<<' '<<epoch<<' '<<id
                    <<' '<<receipt.hp_before<<' '<<receipt.hp_after<<' '<<signature
                    <<' '<<result.accepted_steps<<' '<<static_cast<unsigned>(result.stop)<<'\n';
            }
        }
    }
    // A refused or truncated suffix retains any remaining legal escape action.
    // Its permissions/path are freshly checked by the native incumbent executor.
    if(result.stop!=word_execute::Stop::Complete&&cambc::round_number()==epoch&&fuel(14000)) {
        if(!cambc::self().can_cast()&&cambc::self().can_move()) {
            Plan remaining;remaining.model=incumbent.model;
            for(unsigned i=0;i<incumbent.count;++i)if(incumbent.steps[i].kind==Kind::Move)
                step(remaining,incumbent.steps[i]);
            out.acted=execute(remaining,snapshot,env)||out.acted;
        } else out.acted=execute(incumbent,snapshot,env)||out.acted;
    }
    if(cambc::round_number()==epoch)out.moved=cambc::self().position()!=point(snapshot.frame.position);
    report_words();return out;
}
} // namespace wb::capital::contact_native
