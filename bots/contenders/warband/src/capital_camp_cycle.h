#pragma once
#include "capital_reply_cache.h"

namespace wb::capital::contact_native {
inline void word_event(const char* tag,std::initializer_list<std::uint64_t> values) {
    char buffer[384];unsigned n=0;while(*tag&&n+1<sizeof(buffer))buffer[n++]=*tag++;
    for(auto value:values) {
        char digits[20];unsigned count=0;do{digits[count++]=static_cast<char>('0'+value%10);value/=10;}while(value);
        if(n+count+2>=sizeof(buffer))return;buffer[n++]=' ';while(count)buffer[n++]=digits[--count];
    }
    buffer[n++]='\n';std::cout.write(buffer,n);
}
inline contact::Evaluation cached_no_foe_value(reply_cache::Session& cache,reply_cache::Binding binding,
                                        const contact::Choice& choice,bool complete) {
    // This helper is used ONLY after the immutable frame's foe_count==0
    // check. Endpoint then has no reply-physics role; actual endpoint-specific
    // NPC exposure is retained in the request and the service keeps its pose.
    reply_cache::RiskRequest request;request.endpoint={0,0};request.effect_count=choice.effect_count;
    for(unsigned i=0;i<choice.effect_count&&i<request.effects.size();++i)request.effects[i]=choice.effects[i];
    request.npc_damage_upper=choice.npc_damage_upper;request.npc_hit_count=choice.npc_hit_count;
    const auto result=cache.query(binding,request);const auto& core=result.core;contact::Evaluation out;
    if(result.reused)++word_counters().quote_reuses;
    out.valid=core.valid;out.prefix_alive=core.prefix_alive;out.survives=core.survives;
    out.hp_lower=core.hp_lower;out.damage_upper=core.damage_upper;out.reply_hits=core.reply_hits;
    out.kills=core.kills;out.may_stun_next=core.may_stun_next;
    out.horizon_admissible=complete&&choice.prefix_complete&&out.survives&&!out.may_stun_next;
    out.continuation_admissible=out.horizon_admissible&&choice.continuation_covered&&out.hp_lower>choice.continuation_damage_upper;
    return out;
}
// The first application class is a current NPC job with no modeled foe body.
// Missing family/offer returns to the complete economic interpreter. It does
// not authorize an unpriced harmful prefix or certify hidden-world safety.
inline CampCycleResult try_camp_cycle(Environment env,const work_bridge::Offer& work) {
    CampCycleResult result;auto& m=memory();auto& counters=word_counters();
    const auto epoch=cambc::round_number();
    const auto stale=[&](){if(cambc::round_number()==epoch)return false;m.handled=true;result.claimed=true;result.result.present=true;return true;};
    if(!work.fresh||!work.target||!fuel(65000))return result;
    auto& geometry=word_native::geometry();
    if(!geometry.identity_checked||geometry.own_type!=static_cast<int>(cambc::champion_type::vendetta))return result;
    cambc::detail::champion_info own{};cambc::detail::check(cambc::detail::sdk_champion(work.owner,&own));
    const auto state=cambc::champion_state::from_bits_retain(own.state);
    if(stale()||!state.contains(cambc::champion_state::can_attack))return result;
    const vec2 actual_origin{own.x,own.y};
    const bool stationary=distance(actual_origin,work.position)<=own.attack_range&&work_bridge::npc_quiet(actual_origin);
    // Runtime already has the complete, freshly guarded stationary economic
    // word (Basic / Basic->Reset->Basic) with a zero-length paid exit here.
    // Preserve its planning/dispatch funds instead of rebuilding an idle-only
    // comparison and consuming the macro work gate before returning NoOffer.
    // StationaryCampWork remains a pure modeled family for a later comparison
    // that includes the same complete service and future resource obligations.
    if(stationary)return result;
    int dash=-1;
    for(unsigned slot=0;slot<2;++slot)if(static_cast<cambc::ability_kind>(geometry.design.definition.abilities[slot].kind)==cambc::ability_kind::dash)dash=slot;
    if(dash<0||!state.contains(cambc::champion_state::can_cast))return result;
    // Necessary-only preflight: a cooling dash needs this basic to kill.
    if(cambc::self().abilities()[dash].cooldown()>0) {
        const float upper=detail::upper(own.strength*word_native::camp_multiplier());
        if(stale()||work.observed_hp>upper)return result;
    }
    ++counters.inputs;++counters.offers;
    if(!m.snapshot){m.snapshot=capture();if(stale())return result;add_beliefs(*m.snapshot);}
    if(stale())return result;
    const auto& observed=*m.snapshot;
    if(!observed.frame.fresh||observed.frame.foe_count)return result;
    auto snapshot=observed;snapshot.frame.shield=snapshot.self_shield_now;snapshot.frame.shield_valid_through_horizon=true;
    word_native::Provenance provenance{m.walked,m.exclusive};
    // Funding reserves TWO risk quotes, actual per-primitive refresh/dispatch,
    // and bounded telemetry. The no-foe class needs no hostile reply search.
    constexpr unsigned tail=30000;
    word_native::RootView root;auto mark=cambc::fuel_remaining();
    const auto root_status=word_native::prepare_root(root,snapshot,provenance,{tail,true});
    if(stale())return result;
    counters.scene_cost=std::max(counters.scene_cost,mark-cambc::fuel_remaining());
    if(root_status!=word_native::Status::Ready)return result;
    reply_cache::Session cache;const reply_cache::Binding binding{snapshot.frame.owner,snapshot.frame.round,root.generation};
    if(!cache.reset(snapshot.frame,binding))return result;
    // A cheap priced ordinary escape is retained without another SDK flood.
    const auto origin=point(snapshot.frame.position);vec2 escape=origin;
    float cost=env.npc?env.npc(env.context,origin).damage:0;
    for(const auto q:root.move_positions) {
        if(distance(origin,q)>2||!root.visible.get({q.x,q.y}))continue;
        const float next=env.npc?env.npc(env.context,q).damage:0;
        if(next<cost||(next==cost&&distance(q,env.objective)<distance(escape,env.objective))){escape=q;cost=next;}
    }
    auto incumbent=base(snapshot,env,escape,escape==origin?contact::Primitive::Hold:contact::Primitive::Move);
    if(escape!=origin)step(incumbent,{Kind::Move,escape,-1,-1});
    if(stale()||!fuel(tail+8000))return result;
    // One complete incumbent must reach value and dispatch before optional
    // exit siblings can consume its reserve. A deferred enumeration may keep
    // useful words, but it has already spent the mandatory tail by definition.
    word_native::OptionsFamily options;options.dispatch_reserve=tail;options.max_exits=1;
    options.exit_filter=[](void*,word::Point p,int killed){return work_bridge::npc_quiet({p.x,p.y},killed);};
    word_native::Scene scene;mark=cambc::fuel_remaining();
    const auto prepared=word_native::prepare(scene,snapshot,provenance,{env.objective.x,env.objective.y},&*work.target,&root,options);
    if(stale())return result;
    counters.scene_cost=std::max(counters.scene_cost,mark-cambc::fuel_remaining());
    if(prepared!=word_native::Status::Ready)return result;
    ++counters.ready;
    if(!fuel(tail+6000))return result;
    mark=cambc::fuel_remaining(); // Includes the first raw workspace allocation.
    auto& storage=word_native::workspace();word_native::WorkspaceLease lease(storage,scene);
    if(!lease)return result;
    const auto& words=word_native::compile_family_into(scene,lease,scene.prepared_family,[](void*){return fuel(tail);},nullptr);
    if(stale()||!word_native::epoch_matches(scene))return result;
    counters.compile_cost=std::max(counters.compile_cost,mark-cambc::fuel_remaining());counters.compiled+=words.count;
    if(scene.compile_deferred)++counters.deferred;
    if(!words.count||!fuel(tail))return result;
    // The mandatory complete family is compiled before spending its paired
    // value/dispatch reserve. Exact no-foe risk identities share this quote.
    const auto incumbent_value=cached_no_foe_value(cache,binding,incumbent.model,false);
    if(stale()||!incumbent_value.valid)return result;
    using namespace word_arbitration;
    const Horizon horizon{snapshot.frame.owner,snapshot.frame.round,snapshot.frame.round+1,root.generation,true};
    Offer winner;winner.choice=incumbent.model;winner.evaluation=incumbent_value;winner.horizon=horizon;
    int selected=-1;
    // The admitted tail funds the first candidate's quote, prefix and dispatch.
    // Its mandatory incumbent quote has already consumed part of that reserve;
    // requiring the original unspent amount here would discard funded work.
    // Only an optional sibling must acquire a new whole-tail reserve. Native
    // dispatch still checks actual per-primitive fuel and closes partial words.
    for(unsigned i=0;i<std::min(2u,words.count)&&(i==0||fuel(tail));++i) {
        const auto& word=words.words[i];Offer candidate;candidate.choice=word_effects(snapshot,env,scene,word);
        candidate.horizon=horizon;candidate.modeled_effect_count=word.effect_count;
        mark=cambc::fuel_remaining();candidate.evaluation=cached_no_foe_value(cache,binding,candidate.choice,false);
        if(stale())return result;
        counters.value_cost=std::max(counters.value_cost,mark-cambc::fuel_remaining());++counters.evaluated;
        if(!candidate.evaluation.valid||candidate.choice.camp_work<=0)continue;
        ++counters.work_models;if(candidate.evaluation.survives)++counters.model_survives;
        if(candidate.evaluation.may_stun_next)++counters.possible_stun;
        mark=cambc::fuel_remaining();
        auto refused=base(snapshot,env,origin,contact::Primitive::Hold).model;
        for(unsigned step=0;step<word.count;++step) {
            const auto& action=word.trace[step].candidate;
            if(action.kind!=word::Kind::Move&&action.kind!=word::Kind::Dash)continue;
            const auto from=action.before.position;const int dx=action.destination.x-from.x,dy=action.destination.y-from.y;
            const int length=std::max(std::abs(dx),std::abs(dy));
            for(int at=1;at<=length;++at) {
                const vec2 pose{from.x+dx*at/length,from.y+dy*at/length};
                const auto risk=env.npc?env.npc(env.context,pose):Risk{};
                refused.npc_damage_upper=std::max(refused.npc_damage_upper,detail::upper(risk.damage*snapshot.self_mitigation));
                refused.npc_hit_count=std::max(refused.npc_hit_count,risk.hits);
            }
        }
        const auto prefix=cached_no_foe_value(cache,binding,refused,false);
        if(stale())return result;
        counters.prefix_cost=std::max(counters.prefix_cost,mark-cambc::fuel_remaining());
        const int killed=word.after.targets[scene.camp_index].hp.upper<=0?scene.bindings[scene.camp_index].id:-1;
        const bool known=word_native::whole_path_known_free(scene,word)&&work_bridge::npc_quiet({word.after.position.x,word.after.position.y},killed);
        if(known)++counters.known_exits;
        const bool priced=known&&prefix.valid&&prefix.survives&&!prefix.may_stun_next;if(priced)++counters.priced_exits;
        auto& service=candidate.service;service.kind=ServiceKind::CampWork;service.horizon=horizon;service.target_kind=TargetKind::Camp;
        service.target_id=scene.bindings[scene.camp_index].id;service.effect_count=word.effect_count;service.quantity=candidate.choice.camp_work;
        service.modeled_useful=service.dated=service.bound=service.persistent=true;service.exit_known=known;service.exit_priced=priced;
        if(better(candidate,winner,Class{true})){winner=candidate;selected=i;}
    }
    if(selected<0)return result;
    ++counters.selected;m.handled=true;result.claimed=true;result.result.present=true;
    cache.invalidate();mark=cambc::fuel_remaining();
    struct NoFoeGuard {int owner;unsigned round;};
    NoFoeGuard dependency{snapshot.frame.owner,epoch};
    word_execute::Hooks hooks;hooks.context=&dependency;
    hooks.dependency=[](void* context) {
        const auto& expected=*static_cast<NoFoeGuard*>(context);
        if(static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()))!=expected.owner||
            cambc::round_number()!=expected.round)return false;
        const auto visible=cambc::vision::champion_tiles(team::enemy);
        return visible.is_empty()&&cambc::round_number()==expected.round;
    };
    const auto executed=word_execute::execute(scene,words.words[selected],provenance,hooks);
    if(stale())return result;
    counters.execute_cost=std::max(counters.execute_cost,mark-cambc::fuel_remaining());
    m.walked=provenance.walked;m.exclusive=provenance.exclusive_cast;invalidate_snapshot();
    if(executed.moved&&fuel(14000))note_step(true,false,false);
    result.result.acted=executed.acted;result.result.moved=executed.moved;result.result.status=contact::Status::Incomplete;
    if(executed.acted)++counters.accepted;if(executed.refused)++counters.refused;if(executed.killed)++counters.kills;
    if(executed.stop==word_execute::Stop::Complete)++counters.completed;else ++counters.stopped;
    const auto id=static_cast<int>(static_cast<cambc::handle_camp_t>(*work.target));
    for(unsigned i=0;i<executed.camp_receipt_count;++i) {
        const auto& receipt=executed.camp_receipts[i];if(!receipt.observed||receipt.camp_id!=id||receipt.hp_damage<=0)continue;
        work_bridge::commit(work,receipt.hp_before,receipt.hp_after);counters.actual_hp_work+=receipt.hp_damage;
        if(counters.receipt_logs<4&&fuel(9000)) {
            ++counters.receipt_logs;unsigned signature=0;
            for(unsigned j=0;j<words.words[selected].count;++j)signature=signature*8+static_cast<unsigned>(words.words[selected].trace[j].candidate.kind)+1;
            word_event("wb_word_receipt_v2",{static_cast<unsigned>(snapshot.frame.owner),epoch,static_cast<unsigned>(id),
                std::bit_cast<std::uint32_t>(receipt.hp_before),std::bit_cast<std::uint32_t>(receipt.hp_after),signature,executed.accepted_steps,static_cast<unsigned>(executed.stop)});
        }
    }
    // The incumbent was quoted at ROOT, not at an arbitrary partial endpoint.
    // It may serve a refused/short stationary prefix only while the actual
    // pose still matches and fresh known-contact dependencies still hold.
    // Changed-pose words return with their actual receipts/remaining MAC for
    // the next fresh nomination instead of borrowing this old escape quote.
    if(!executed.dependency_changed&&executed.stop!=word_execute::Stop::Complete&&fuel(12000)) {
        const auto actual=cambc::self().position();
        if(!stale()&&actual==origin&&hooks.dependency(hooks.context)&&!stale()) {
            word_native::invalidate(root);(void)execute(incumbent,snapshot,env);
        }
    }
    return result;
}
} // namespace wb::capital::contact_native
