// Included inside Runtime. return_policy.h is included at file scope.
// This is a CAMP-only, finite-horizon policy. Dormancy forecasts assume that
// no other actor wakes a currently observed sleeping group before continuation.
// That assumption is a model, never a certificate of absent future danger.
using ModelFrame=return_policy::Frame;
using ModelCandidate=return_policy::Candidate;
using ModelPrimitive=return_policy::Primitive;
using ModelQuote=return_policy::Quote;
struct ModelReturnState {
    enum class Kind {move,basic,unit_cast,ground_cast,shield,recall};
    struct Offer {Kind kind{Kind::move};int member{-1},slot{-1};vec2 center{};bool walks{};};
    ModelFrame frame;
    std::array<ModelCandidate,return_policy::candidate_capacity> candidates;
    std::array<Offer,return_policy::candidate_capacity> offers;
    std::array<std::size_t,return_policy::source_capacity> member{};
    std::array<int,return_policy::source_capacity> group{};
    std::array<std::array<double,7>,return_policy::source_capacity> incoming;
    std::size_t group_count{},candidate_count{},steps{},full_steps{};
    std::size_t evaluated_count{};
    return_policy::Selection best,risk;
    std::vector<std::size_t> overflow;
    return_route::Route route;
    vec2 origin{};
    std::uint64_t observation{},support_revision{};
    std::uint64_t effect{},word_revision{};
    return_funding::Tail funding;
    unsigned funding_overrun{};
    int funding_reason{};
    int owner_epoch{-1},last_reason{},last_kind{-1},last_member{-1};
    int attack_range{};
    unsigned source_reads{},query_cursor{},model_risk{},actual_false{},actual_work{},actual_moves{},actual_recalls{};
    std::array<unsigned,6> offered{},selected{},applied{};
    ModelQuote last_quote;
    bool truncated{},has_visible_enemy{};
    // Nomination, optional offers and execution are distinct paid phases.
    static constexpr std::uint64_t command_tail=16000,refresh_tail=35000;
};
static ModelReturnState& model_return_state(){static constinit ModelReturnState value{};return value;}
struct ModelResourceFailure {
    bool present{};return_funding::Phase phase{};return_funding::Reason reason{};
    std::uint64_t before{},after{},bound{};
    return_funding::Token start{},actual{};
};
static ModelResourceFailure& model_resource_failure(){static constinit ModelResourceFailure value{};return value;}
std::uint64_t model_offer_tail(std::size_t index)const{
    const auto& model=model_return_state();const auto kind=model.offers[index].kind;
    if(kind==ModelReturnState::Kind::move||kind==ModelReturnState::Kind::recall)return ModelReturnState::command_tail;
    const auto& primitive=model.candidates[index].word[0];unsigned affected=0,groups_hit=0;
    for(std::size_t source=0;source<model.frame.source_count;++source)
        if(primitive.damage[source]>0||primitive.stun_phases[source])++affected;
    auto mask=primitive.wakes_groups;while(mask){mask&=mask-1;++groups_hit;}
    std::size_t move_phases=0;
    for(std::size_t n=0;n<model.candidates[index].scenario_count;++n)
        move_phases+=model.candidates[index].scenarios[n].phase_count;
    const auto move_eval=2500+std::uint64_t(model.frame.source_count)*(80+move_phases*70);
    // Actual prefix + source/body/group feedback + physical route reprice and
    // fresh Move evaluation + Move/false closure. Already completed nomination
    // and current candidate evaluation are deliberately absent from this tail.
    return 6000+(4000+std::uint64_t(affected)*1000+std::uint64_t(groups_hit)*1200)+
        (6000+move_eval)+ModelReturnState::command_tail;
}
return_funding::Bounds model_work_bounds(std::size_t index)const{
    const auto& model=model_return_state();const auto& candidate=model.candidates[index];
    const auto& primitive=candidate.word[0];unsigned affected=0,groups_hit=0;bool physical=false;
    for(std::size_t source=0;source<model.frame.source_count;++source)
        if(primitive.damage[source]>0||primitive.stun_phases[source])++affected;
    for(std::size_t n=0;n<candidate.primitive_count;++n)
        physical=physical||candidate.word[n].action==return_policy::Action::move;
    auto mask=primitive.wakes_groups;while(mask){mask&=mask-1;++groups_hit;}
    std::size_t phases=0;for(std::size_t n=0;n<candidate.scenario_count;++n)phases+=candidate.scenarios[n].phase_count;
    const auto evaluation=return_funding::affine_bound(2500,80,70,model.frame.source_count,phases);
    return {6000,4000+std::uint64_t(affected)*1000+std::uint64_t(groups_hit)*1200,
        6000+evaluation.amount,physical?ModelReturnState::command_tail:0,evaluation.known,physical};
}
return_funding::Token model_funding_token()const{
    const auto& model=model_return_state();return {model.word_revision,std::uint64_t(epoch),model.effect,model.observation};
}
bool model_record_finish_phase(return_funding::Phase phase,const return_funding::Witness& witness){
    auto& model=model_return_state();
    const bool active=model.funding.valid()&&model.funding.inflight()&&model.funding.next()==phase;
    const auto before=model.funding.phase_start_fuel(),bound=model.funding.phase_bound(phase);
    const auto start=model.funding.token(),actual=model_funding_token();
    const auto after=fuel_remaining(); // the existing finish endpoint, captured once
    const auto result=model.funding.finish(phase,actual,after,witness);
    auto& first=model_resource_failure();
    if(active&&result!=return_funding::Reason::ok&&!first.present)
        first={true,phase,result,before,after,bound,start,actual};
    model.funding_reason=int(result);
    if(result==return_funding::Reason::overrun)++model.funding_overrun;
    return result==return_funding::Reason::ok;
}
bool model_finish_phase(return_funding::Phase phase,const return_funding::Witness& witness){
    if constexpr(cfg::trace>=3)return model_record_finish_phase(phase,witness);
    auto& model=model_return_state();
    const auto result=model.funding.finish(phase,model_funding_token(),fuel_remaining(),witness);
    model.funding_reason=int(result);
    if(result==return_funding::Reason::overrun)++model.funding_overrun;
    return result==return_funding::Reason::ok;
}
std::uint64_t model_retained_tail()const{
    const auto& model=model_return_state();
    const auto index=model.best.index!=std::numeric_limits<std::size_t>::max()?model.best.index:model.risk.index;
    return index==std::numeric_limits<std::size_t>::max()?ModelReturnState::command_tail:model_offer_tail(index);
}
static return_policy::PhaseMask model_phase_mask(int duration){
    const auto n=std::clamp(duration,0,int(return_policy::phase_capacity));
    return n==8?return_policy::PhaseMask(255):return_policy::PhaseMask((1u<<n)-1u);
}
unsigned model_group(int actual){
    auto& model=model_return_state();
    for(std::size_t i=0;i<model.group_count;++i)if(model.group[i]==actual)return unsigned(i);
    if(model.group_count>=model.group.size())return 64;
    model.group[model.group_count]=actual;return unsigned(model.group_count++);
}
double model_source_hit(std::size_t index,vec2 position)const{
    const auto& member=members[index];
    if(dist(position,member.pos)>member.range)return 0;
    // The catalog's potential strength remains present for hidden sources.
    // One unit of slack also bounds individually rounded native camp hits.
    const double strength=std::max(double(member.base_strength),
        member.seen==epoch&&member.alive?double(member.strength):0.0);
    const double scaled=std::max(strength*double(camp_taken),double(float(strength)*camp_taken));
    return strength>0?std::ceil(scaled)+1.0:0.0;
}
bool model_member_relevant(std::size_t index)const{
    const auto& model=model_return_state();const auto& member=members[index];
    if(member.base_strength<=0)return false;
    if(dist(model.origin,member.pos)<=member.range)return true;
    for(std::size_t n=1;n<=model.steps;++n)
        if(dist(vp(model.route.node(n).point),member.pos)<=member.range)return true;
    return false;
}
bool model_dead_through_horizon(const Member& member)const{
    const auto& group=memory[std::size_t(member.group)];
    if(group.seen!=epoch)return false;
    if((group.state==2||group.state==3)&&group.until>epoch+int(return_policy::phase_capacity))return true;
    return group.state==1&&int(groups[std::size_t(member.group)].max_respawn_timer())+
        int(constants::capture_time)>int(return_policy::phase_capacity);
}
void model_read_shield(const Info& me){
    auto& frame=model_return_state().frame;frame.shield={};
    if(!fuel(ModelReturnState::command_tail+3000))return;
    const auto buffs=self().buffs();
    if(buffs.has_shield()){
        const auto shield=buffs.get<buff_shield>();frame.shield.amount=shield.value;
        // At this ready owner input positive duration covers that many future
        // pre-owner-start CAMP batches. Zero credit is conservative for zero.
        frame.shield.active_phases=model_phase_mask(shield.duration);
    }
    (void)me;
}
bool model_collect_sources(const Info& me){
    auto& model=model_return_state();auto& frame=model.frame;
    frame.frame=std::uint64_t(epoch);frame.observation=++model.observation;
    frame.health=me.health;frame.source_count=0;frame.coverage_complete=true;
    frame.no_exogenous_wake=true;frame.refusal_used=return_refusals>0||model.route.refusal_used();
    const auto flags=state(me);const auto restrictions=self().restrictions();
    frame.can_move=flags.contains(champion_state::can_move)&&!restrictions.contains(buff_restrictions::move);
    frame.can_attack=flags.contains(champion_state::can_attack)&&!restrictions.contains(buff_restrictions::attack);
    frame.can_cast=flags.contains(champion_state::can_cast)&&!restrictions.contains(buff_restrictions::cast);
    model_read_shield(me);model.group_count=0;model.overflow.clear();model.source_reads=0;
    model.has_visible_enemy=!vision::champions(team::enemy).empty();
    // Rotate observation order without changing catalog coverage. Unqueried
    // sources are still represented as awake potential, including overflow.
    const auto catalog=members.size();const auto start=catalog?std::size_t(model.query_cursor)%catalog:0;
    for(std::size_t n=0;n<catalog;++n){
        const auto index=(start+n)%catalog;if(!model_member_relevant(index))continue;
        if(frame.source_count>=return_policy::source_capacity-1){model.overflow.push_back(index);continue;}
        const auto at=frame.source_count++;model.member[at]=index;
        auto& source=frame.sources[at];source={};source.alive=true;
        const auto& member=members[index];source.group=model_group(member.group);
        source.group_known=source.group<return_policy::source_capacity;
    }
    model.query_cursor=catalog?unsigned((start+std::max(1u,model.source_reads))%catalog):0;
    if(!model.overflow.empty()){
        const auto at=frame.source_count++;model.member[at]=std::numeric_limits<std::size_t>::max();
        frame.sources[at]={};frame.sources[at].alive=true;frame.sources[at].group_known=false;
        // This aggregate is ALWAYS potential. No effect, dormancy or health
        // deduction is permitted for its unknown individual members.
    }
    const bool current_false=frame.can_move&&model.steps&&!sees(model_route_position(1));
    const auto first_phases=frame.can_move?model.steps+unsigned(!frame.refusal_used):
        std::size_t(std::max(1,int(constants::recall_time)));
    const auto failure_phases=current_false?model.steps+1:0;
    const auto phase_sum=first_phases+failure_phases;
    const auto evaluation=2500+std::uint64_t(frame.source_count)*(80+phase_sum*70);
    const auto fill=1000+std::uint64_t(frame.source_count)*phase_sum*20;
    // Complete catalog/aggregate coverage is established BEFORE any optional
    // query. Each read preserves the first actual candidate evaluation and
    // execution, rather than discovering too late that the menu is unfunded.
    for(std::size_t source_index=0;source_index<frame.source_count;++source_index){
        if(model.member[source_index]==std::numeric_limits<std::size_t>::max())continue;
        const auto& member=members[model.member[source_index]];auto& source=frame.sources[source_index];
        if(!sees(member.pos)||!fuel(ModelReturnState::command_tail+evaluation+fill+3000))continue;
        const auto life=member.handle.state();++model.source_reads;
        source.fresh=true;source.alive=life.is_alive();source.asleep=life.is_asleep();
        if(!source.alive){
            // A fresh dead body is not an eternal absence. If the whole-group
            // lifecycle does not exclude respawn in this horizon, keep it.
            if(memory[std::size_t(member.group)].seen!=epoch&&
                fuel(ModelReturnState::command_tail+evaluation+fill+6000))refresh_group(member.group);
            if(!model_dead_through_horizon(member)){source.fresh=false;source.alive=true;source.asleep=false;}
        }
        if(source.alive&&source.fresh&&dist(point(me),member.pos)<=std::max(me.attack_range,spell_reach)&&
            fuel(ModelReturnState::command_tail+evaluation+fill+6500))model_target_health(source_index);
    }
    model.support_revision=support_detail.revision();model_cache_incoming();return frame.coverage_complete;
}
double model_uncached_incoming(std::size_t source,vec2 position)const{
    const auto& model=model_return_state();
    if(model.member[source]!=std::numeric_limits<std::size_t>::max())return model_source_hit(model.member[source],position);
    double sum=0;for(const auto index:model.overflow)sum+=model_source_hit(index,position);
    return sum;
}
void model_cache_incoming(){
    auto& model=model_return_state();
    for(std::size_t source=0;source<model.frame.source_count;++source)
        for(std::size_t n=0;n<=model.steps;++n)
            model.incoming[source][n]=model_uncached_incoming(source,vp(model.route.node(n).point));
}
double model_incoming(std::size_t source,vec2 position)const{
    const auto& model=model_return_state();
    for(std::size_t n=0;n<=model.steps;++n)if(vp(model.route.node(n).point)==position)return model.incoming[source][n];
    // No candidate currently nominates a position outside this retained route.
    // Retain potential rather than silently publishing zero if a new caller
    // violates that cache-domain contract.
    return model_uncached_incoming(source,position);
}
vec2 model_route_position(std::size_t n)const{
    const auto& model=model_return_state();return vp(model.route.node(std::min(n,model.steps)).point);
}
bool model_fill_scenario(ModelCandidate& candidate,return_policy::Scenario& scenario,
                         bool current_move,bool refused,unsigned prefix){
    auto& model=model_return_state();const auto& frame=model.frame;
    scenario.applied_prefix=prefix;scenario.refused=refused;
    scenario.refused_primitive=refused?prefix:0;scenario.additional_refusals=0;
    scenario.route_complete=true;scenario.future_failures_bounded=true;
    scenario.declared_escape_postcondition=model.truncated;
    scenario.requires_no_damage=false;
    const bool wait_at_origin=!current_move||refused;
    // The success envelope prices one future failure at ANY remaining origin.
    // It takes a per-source maximum of the two possible shifted positions;
    // this can overcount distinct failure locations, but never erase damage.
    const bool future_wait=!frame.refusal_used&&!refused;
    scenario.additional_refusals=refused||future_wait?1:0;
    const auto phases=model.steps+unsigned(wait_at_origin)+unsigned(future_wait);
    if(!phases||phases>return_policy::phase_capacity)return false;
    scenario.phase_count=phases;scenario.owner_waits=unsigned(phases);
    for(std::size_t phase=0;phase<phases;++phase){
        auto& batch=scenario.phases[phase];batch.bounds_valid=true;batch.observed_open=true;batch.channel_interruption=false;
        const auto index=phase+(wait_at_origin?0:1);
        const auto a=std::min(index,model.steps);
        const auto b=future_wait&&index>0?std::min(index-1,model.steps):a;
        for(std::size_t source=0;source<frame.source_count;++source)
            batch.incoming[source]=std::max(model.incoming[source][a],model.incoming[source][b]);
    }
    (void)candidate;return true;
}
void model_clear_candidate(ModelCandidate& candidate){
    const auto& frame=model_return_state().frame;
    candidate.frame=frame.frame;candidate.observation=frame.observation;
    candidate.purpose=model_return_state().truncated?return_policy::Purpose::model_escape:return_policy::Purpose::return_exit;
    candidate.primitive_count=0;candidate.scenario_count=0;candidate.preparation={};candidate.useful_service=1;
}
bool model_finish_candidate(ModelCandidate& candidate,bool wants_move){
    auto& model=model_return_state();const auto& frame=model.frame;
    bool moving=false;
    if(wants_move&&frame.can_move&&model.steps){
        bool available=frame.can_move;
        for(std::size_t i=0;i<candidate.primitive_count;++i)
            if(candidate.word[i].action==return_policy::Action::move||candidate.word[i].consumes_move)available=false;
        if(available){
            auto& move=candidate.word[candidate.primitive_count++];move={};
            move.action=return_policy::Action::move;move.observed_legal=true;
            move.consumes_attack=model.attack_range>1;
            move.may_refuse=!sees(model_route_position(1));moving=true;
            if(move.may_refuse&&frame.refusal_used)return false;
        }
    }
    const auto success_phases=model.steps+unsigned(!moving)+unsigned(!frame.refusal_used);
    const auto failure_phases=moving&&candidate.word[candidate.primitive_count-1].may_refuse?model.steps+1:0;
    const auto fill=1000+std::uint64_t(frame.source_count)*(success_phases+failure_phases)*20;
    if(!fuel(model_retained_tail()+fill))return false;
    candidate.scenario_count=1;
    if(!model_fill_scenario(candidate,candidate.scenarios[0],moving,false,unsigned(candidate.primitive_count)))return false;
    for(std::size_t i=0;i<candidate.primitive_count;++i)if(candidate.word[i].may_refuse){
        if(candidate.scenario_count>=return_policy::scenario_capacity)return false;
        // Current visible casts are either known legal or not offered. Only
        // an unseen landing has the SDK's ordinary hidden-obstruction false.
        if(!model_fill_scenario(candidate,candidate.scenarios[candidate.scenario_count++],false,true,unsigned(i)))return false;
    }
    return true;
}
return_policy::Mask model_affected_groups(vec2 center,int radius,bool splash)const{
    const auto& model=model_return_state();return_policy::Mask result=0;
    for(const auto& member:members){
        if(dist(center,member.pos)>radius)continue;
        // Include every catalog body, including off-route and currently hidden
        // collateral. A potentially hit sibling is sufficient to wake a group.
        for(std::size_t g=0;g<model.group_count;++g)if(model.group[g]==member.group)
            result|=return_policy::Mask(1)<<g;
    }
    (void)splash;return result;
}
void model_damage(ModelPrimitive& primitive,vec2 center,int radius,double primary_damage,bool basic){
    auto& model=model_return_state();primitive.wakes_groups=model_affected_groups(center,radius,basic);
    primitive.exact_damage=false; // target-dependent modifiers are an estimate.
    for(std::size_t source=0;source<model.frame.source_count;++source){
        if(model.member[source]==std::numeric_limits<std::size_t>::max())continue;
        const auto& member=members[model.member[source]];const auto d=dist(center,member.pos);
        if(d>radius)continue;
        double damage=primary_damage;
        // Varg splash equals the primary Strength damage, not a fractional
        // modifier. The primary body is included exactly once in this array.
        primitive.damage[source]=damage;
    }
}
bool model_target_health(std::size_t source){
    auto& model=model_return_state();auto& observed=model.frame.sources[source];
    if(!observed.fresh||!observed.alive||model.member[source]==std::numeric_limits<std::size_t>::max()||
       !fuel(model_retained_tail()+12000))return false;
    const auto& member=members[model.member[source]];
    detail::camp_state current{};detail::check(detail::sdk_camp(std::uint32_t(handle_camp_t(member.handle)),&current));
    double shield=0;const auto buffs=member.handle.buffs();
    if(buffs.has_shield())shield=buffs.get<buff_shield>().value;
    observed.health=double(current.health)+shield;observed.health_known=true;observed.exact_health=false;
    if(buffs.has_stun())observed.stun_phases=model_phase_mask(buffs.get<buff_stun>().duration);
    return true;
}
bool model_offer_move(const Info& me){
    auto& model=model_return_state();if(!model.frame.can_move||!model.steps)return false;
    auto& candidate=model.candidates[model.candidate_count];model_clear_candidate(candidate);
    if(!model_finish_candidate(candidate,true))return false;
    model.offers[model.candidate_count]={ModelReturnState::Kind::move,-1,-1,{},true};
    ++model.candidate_count;++model.offered[0];(void)me;return true;
}
bool model_offer_basic(const Info& me){
    auto& model=model_return_state();if(!model.frame.can_attack||model.candidate_count>=return_policy::candidate_capacity)return false;
    std::size_t chosen=return_policy::source_capacity;double best=-1;
    for(std::size_t source=0;source<model.frame.source_count;++source){
        const auto& q=model.frame.sources[source];if(!q.fresh||!q.alive||model.member[source]==std::numeric_limits<std::size_t>::max())continue;
        const auto& member=members[model.member[source]];if(dist(point(me),member.pos)>me.attack_range)continue;
        const double damage=double(me.strength)*camp_multiplier;
        const double value=q.health_known?std::min(q.health,damage):damage;
        if(value>best){chosen=source;best=value;}
    }
    if(chosen==return_policy::source_capacity)return false;
    auto& candidate=model.candidates[model.candidate_count];model_clear_candidate(candidate);
    auto& basic=candidate.word[0];basic={};basic.action=return_policy::Action::basic;
    basic.observed_legal=true;basic.consumes_move=me.attack_range>1;
    const auto target=members[model.member[chosen]].pos;
    model_damage(basic,target,passive==passive_type::splash_attack?int(constants::passive_abilities::varg_splash_radius):0,
        double(float(me.strength*camp_multiplier)),true);
    candidate.primitive_count=1;candidate.purpose=return_policy::Purpose::work_exit;
    if(!model_finish_candidate(candidate,true))return false;
    if(model.truncated)candidate.purpose=return_policy::Purpose::model_escape;
    model.offers[model.candidate_count]={ModelReturnState::Kind::basic,int(model.member[chosen]),-1,target,false};
    ++model.candidate_count;++model.offered[1];return true;
}
bool model_offer_cast(const Info& me,int slot){
    auto& model=model_return_state();if(!model.frame.can_cast||model.candidate_count>=return_policy::candidate_capacity||
        !fuel(model_retained_tail()+14000))return false;
    const auto abilities=self().abilities();const auto& ability=abilities[std::size_t(slot)];
    if(!ability.info()||ability.cooldown()!=0)return false;
    bool damage=false,shield=false,cc=false,ground=false,camps=false,exclusive=false,self_target=false;
    int range=0,radius=0,duration=0;
    std::visit([&](const auto& info){using T=std::decay_t<decltype(info)>;
        exclusive=info.exclusive_with_movement;
        ground=info.valid_targets.contains(ability_target_mask::ground);
        camps=info.valid_targets.contains(ability_target_mask::camps);
        self_target=info.valid_targets.contains(ability_target_mask::self);
        if constexpr(std::is_same_v<T,ability_damage>||std::is_same_v<T,ability_cc>||std::is_same_v<T,ability_aoe_damage>){
            damage=true;range=int(info.range);
            if constexpr(std::is_same_v<T,ability_cc>){cc=true;duration=info.duration;}
            if constexpr(std::is_same_v<T,ability_aoe_damage>)radius=int(info.radius);
        }else if constexpr(std::is_same_v<T,ability_shield>){shield=true;duration=info.duration;}
    },*ability.info());
    if(exclusive&&walked)return false;
    auto& candidate=model.candidates[model.candidate_count];model_clear_candidate(candidate);
    auto& spell=candidate.word[0];spell={};spell.action=return_policy::Action::cast;
    spell.observed_legal=true;spell.consumes_move=exclusive;candidate.primitive_count=1;
    ModelReturnState::Offer offer;offer.slot=slot;
    if(shield&&self_target){
        spell.replaces_shield=true;spell.exact_control=true;
        spell.shield_after.amount=ability.shield_amount();
        // Native owner-phase evidence qualifies the next CAMP batch. Longer
        // protection is refreshed from the actual resulting buff after cast.
        spell.shield_after.active_phases=duration>0?return_policy::PhaseMask(1):return_policy::PhaseMask{};
        offer.kind=ModelReturnState::Kind::shield;
    }else if(damage&&(ground||camps)){
        std::size_t chosen=return_policy::source_capacity;vec2 center{};double best=-1;
        const double amount=double(float(ability.damage_amount()*camp_multiplier));
        for(std::size_t source=0;source<model.frame.source_count;++source){
            const auto& q=model.frame.sources[source];if(!q.fresh||!q.alive||model.member[source]==std::numeric_limits<std::size_t>::max())continue;
            const auto target=members[model.member[source]].pos;const int distance=dist(point(me),target);
            vec2 at=target;
            if(!ground&&distance>range)continue;
            if(ground){
                if(distance>range+radius)continue;
                at={std::clamp(target.x,me.x-range,me.x+range),std::clamp(target.y,me.y-range,me.y+range)};
                if(!sees(at)||static_blocked(at)||dist(at,target)>radius)continue;
            }
            const double value=(q.health_known?std::min(q.health,amount):amount)+(cc?model_source_hit(model.member[source],point(me)):0);
            if(value>best){best=value;chosen=source;center=at;}
        }
        if(chosen==return_policy::source_capacity)return false;
        const auto primary=members[model.member[chosen]].pos;
        model_damage(spell,ground?center:primary,ground?radius:0,amount,false);
        spell.exact_control=cc;
        if(cc)for(std::size_t source=0;source<model.frame.source_count;++source)
            if(model.member[source]==model.member[chosen])spell.stun_phases[source]=model_phase_mask(duration);
        offer.kind=ground?ModelReturnState::Kind::ground_cast:ModelReturnState::Kind::unit_cast;
        offer.member=int(model.member[chosen]);offer.center=ground?center:primary;
        candidate.purpose=return_policy::Purpose::work_exit;
    }else return false;
    if(!model_finish_candidate(candidate,true))return false;
    if(model.truncated)candidate.purpose=return_policy::Purpose::model_escape;
    model.offers[model.candidate_count]=offer;++model.candidate_count;
    ++model.offered[std::size_t(offer.kind)];return true;
}
bool model_offer_recall(const Info& me){
    auto& model=model_return_state();if(model.candidate_count>=return_policy::candidate_capacity||
        last_action_round==epoch||self().restrictions().bits()!=0)return false;
    const auto flags=state(me);
    if(flags.is_dead()||flags.contains(champion_state::recalling)||flags.contains(champion_state::submitting_gems)||
       flags.contains(champion_state::teleporting))return false;
    const auto phases=unsigned(std::max(1,int(constants::recall_time)));
    if(phases>return_policy::phase_capacity)return false;
    if(!fuel(model_retained_tail()+1000+std::uint64_t(model.frame.source_count)*phases*20))return false;
    auto& candidate=model.candidates[model.candidate_count];model_clear_candidate(candidate);
    candidate.purpose=return_policy::Purpose::model_escape;candidate.useful_service=2;
    candidate.primitive_count=1;auto& channel=candidate.word[0];channel={};
    channel.action=return_policy::Action::channel;channel.observed_legal=true;
    candidate.scenario_count=1;auto& scenario=candidate.scenarios[0];
    scenario.applied_prefix=1;scenario.refused=false;scenario.refused_primitive=0;scenario.additional_refusals=0;
    scenario.phase_count=phases;scenario.route_complete=true;scenario.future_failures_bounded=true;
    scenario.declared_escape_postcondition=true;scenario.requires_no_damage=true;scenario.owner_waits=phases;
    for(std::size_t p=0;p<phases;++p){auto& batch=scenario.phases[p];
        batch.bounds_valid=true;batch.observed_open=true;batch.channel_interruption=false;
        for(std::size_t source=0;source<model.frame.source_count;++source)batch.incoming[source]=model.incoming[source][0];
    }
    model.offers[model.candidate_count]={ModelReturnState::Kind::recall,-1,-1,{},false};
    ++model.candidate_count;++model.offered[5];return true;
}
std::uint64_t model_evaluation_cost(const ModelCandidate& candidate)const{
    const auto& model=model_return_state();std::size_t phases=0;
    for(std::size_t n=0;n<candidate.scenario_count;++n)phases+=candidate.scenarios[n].phase_count;
    // Bounded guest operation admission, separately subject to native health
    // qualification. It is not a general engine/SDK fuel theorem.
    return 2500+std::uint64_t(model.frame.source_count)*(80+std::uint64_t(phases)*70);
}
return_policy::Selection model_select(){
    auto& model=model_return_state();auto& best=model.best;auto& risk=model.risk;
    for(std::size_t n=model.evaluated_count;n<model.candidate_count;++n){
        if(!fuel(model_retained_tail()+model_evaluation_cost(model.candidates[n])))break;
        const auto quote=return_policy::evaluate(model.frame,model.candidates[n]);
        model.evaluated_count=n+1;
        // A model may prefer work, but a work winner is not executable unless
        // its WHOLE actual prefix/refresh/reprice/command tail remains funded.
        if(!fuel(model_offer_tail(n)))continue;
        if(!quote.valid||!quote.supported||!quote.estimated_complete)continue;
        auto better=[&](const return_policy::Selection& old){
            if(old.index==std::numeric_limits<std::size_t>::max())return true;
            if(quote.estimated_alive!=old.quote.estimated_alive)return quote.estimated_alive;
            if(quote.estimated_loss!=old.quote.estimated_loss)return quote.estimated_loss<old.quote.estimated_loss;
            if(quote.owner_waits!=old.quote.owner_waits)return quote.owner_waits<old.quote.owner_waits;
            if(quote.useful_damage!=old.quote.useful_damage)return quote.useful_damage>old.quote.useful_damage;
            if(quote.useful_service!=old.quote.useful_service)return quote.useful_service>old.quote.useful_service;
            return unsigned(quote.assurance)>unsigned(old.quote.assurance);
        };
        if(better(risk)){risk.index=n;risk.quote=quote;}
        if(quote.estimated_alive&&better(best)){best.index=n;best.quote=quote;}
    }
    if(best.index!=std::numeric_limits<std::size_t>::max())return best;
    if(risk.index!=std::numeric_limits<std::size_t>::max())++model.model_risk;
    return risk;
}
bool model_actual_move(const Info& before){
    auto& model=model_return_state();if(!fuel(ModelReturnState::command_tail)||!model.steps)return false;
    const auto actual=body();if(point(actual)!=model.origin||!state(actual).contains(champion_state::can_move)||
        self().restrictions().contains(buff_restrictions::move))return false;
    if(model.frame.frame!=std::uint64_t(epoch)||model.support_revision!=support_detail.revision()||
        model.frame.observation!=model.observation)return false;
    const auto next=model_route_position(1);
    auto blocked=*walls|vision::champion_tiles()|vision::camp_tiles()|vision::teleport_tiles();blocked.set(model.origin,false);
    if(blocked.is_set(next)||!blocked.reachable_tiles(model.origin,1).is_set(next)){model.last_reason=7;return false;}
    const auto previous_origin=model.origin;const bool applied=self().move_absolute(next);
    planned_route().observe_step(rp(previous_origin),rp(next),applied);
    if(applied){
        if(exit_trace.at(rp(previous_origin)))exit_trace.observe_step(rp(previous_origin),rp(next),exposure(next),true);
        else if(exposure(next).amount==0)exit_trace.reset_quiet(rp(next),exposure(next));
        walked=true;last_action_round=epoch;++model.actual_moves;++model.effect;
        if(exposure(next).amount==0)returning=false;
        model.last_reason=8;return true;
    }
    ++return_refusals;++model.actual_false;model.last_reason=9;
    // The actual false preserves pose/M, but revokes the old observation.
    // No retry of this stale landing occurs in this owner input.
    const auto after=body();if(fuel(5000)){
        auto visible_now=vision::visible_tiles();visible_now.export_linear_buffer(visible);++model.observation;
    }
    if(point(after)!=previous_origin)planned_route().clear();
    (void)before;return false;
}
bool model_refresh_geometry(const Info& actual){
    auto& model=model_return_state();if(point(actual)!=model.origin)return false;
    const auto current=exposure(model.origin);
    if(current.valid&&current.amount==0){
        planned_route().reset_at_quiet(rp(model.origin),current,std::uint64_t(epoch));
        exit_trace.reset_quiet(rp(model.origin),current);return_refusals=0;returning=false;
        model.route=planned_route();model.steps=model.full_steps=0;model.truncated=false;
        model.support_revision=support_detail.revision();model_cache_incoming();return true;
    }
    auto blocked=*walls|vision::champion_tiles()|vision::camp_tiles()|vision::teleport_tiles();
    blocked.set(model.origin,false);auto& route=planned_route();
    if(!route.valid()||!route.at(rp(model.origin)))return false;
    std::array<return_budget::Damage,return_route::Route::capacity> damage{};
    std::array<bool,return_route::Route::capacity> open{};
    for(std::size_t n=0;n<route.size();++n){const auto p=vp(route.node(n).point);
        damage[n]=exposure(p);open[n]=!blocked.is_set(p);
    }
    // This refresh preserves the exposed episode's actual refusal and pending
    // wait. Fresh knowledge is never a freshly unrefused physical history.
    const auto count=route.size();
    if(route.refresh_damage({damage.data(),count},{open.data(),count},std::uint64_t(epoch))!=return_route::Reason::ok)return false;
    model.route=route;model.full_steps=route.size()-1;model.steps=std::min<std::size_t>(model.full_steps,6);
    model.truncated=model.steps<model.full_steps;model.support_revision=support_detail.revision();model_cache_incoming();return true;
}
void model_refresh_after_work(const Info& actual,const ModelPrimitive& primitive){
    auto& model=model_return_state();auto& frame=model.frame;
    frame.frame=std::uint64_t(epoch);frame.observation=++model.observation;frame.health=actual.health;
    const auto flags=state(actual);const auto restrictions=self().restrictions();
    frame.can_move=flags.contains(champion_state::can_move)&&!restrictions.contains(buff_restrictions::move);
    frame.can_attack=flags.contains(champion_state::can_attack)&&!restrictions.contains(buff_restrictions::attack);
    frame.can_cast=flags.contains(champion_state::can_cast)&&!restrictions.contains(buff_restrictions::cast);
    model_read_shield(actual);
    for(std::size_t group=0;group<model.group_count;++group)
        if((primitive.wakes_groups&(return_policy::Mask(1)<<group))&&fuel(24000))refresh_group(model.group[group]);
    for(std::size_t source=0;source<frame.source_count;++source){
        auto& q=frame.sources[source];
        const bool was_fresh_alive=q.fresh&&q.alive;
        if((q.group_known&&(primitive.wakes_groups&(return_policy::Mask(1)<<q.group)))||
            (!q.group_known&&primitive.wakes_groups))q.asleep=false;
        if(model.member[source]==std::numeric_limits<std::size_t>::max()||
           (primitive.damage[source]<=0&&!primitive.stun_phases[source]))continue;
        q.health_known=false;q.exact_health=false;q.stun_phases=0;
        const auto& member=members[model.member[source]];
        if(!sees(member.pos)||!fuel(ModelReturnState::command_tail+6000)){q.fresh=false;q.alive=true;continue;}
        const auto life=member.handle.state();q.fresh=true;q.alive=life.is_alive();
        // Any own damaging hit conservatively keeps the whole affected group
        // awake even if delayed damage has not materialized in this snapshot.
        q.asleep=life.is_asleep()&&!(primitive.wakes_groups&(return_policy::Mask(1)<<q.group));
        const bool observed_kill=was_fresh_alive&&!q.alive&&
            int(groups[std::size_t(member.group)].max_respawn_timer())+int(constants::capture_time)>
                int(return_policy::phase_capacity);
        if(!q.alive&&!observed_kill&&!model_dead_through_horizon(member)){q.fresh=false;q.alive=true;}
        if(q.alive&&fuel(ModelReturnState::command_tail+6500)){
            if(primitive.stun_phases[source]){
                const auto buffs=member.handle.buffs();
                if(buffs.has_stun())q.stun_phases=model_phase_mask(buffs.get<buff_stun>().duration);
            }
            if(int(model.member[source])==model.last_member){
                // Actual primary HP feedback, never an assumed predicted kill.
                q.health=member.handle.health();q.health_known=true;q.exact_health=false;
            }
        }
    }
    model.support_revision=support_detail.revision();
    auto fresh=vision::visible_tiles();fresh.export_linear_buffer(visible);
}
bool model_execute(std::size_t selected,const ModelQuote& quote){
    auto& model=model_return_state();const auto offer=model.offers[selected];
    model.last_quote=quote;model.last_kind=int(offer.kind);model.last_member=offer.member;++model.selected[std::size_t(offer.kind)];
    if(offer.kind==ModelReturnState::Kind::move){
        const bool moved=model_actual_move(body());if(moved)++model.applied[0];return moved;
    }
    if(!fuel(model_offer_tail(selected))){
        // Optional menu work cannot turn an already funded physical owner into
        // an empty input. Discard an unfunded work winner, retain actual Move.
        if(model.frame.can_move&&model.steps)return model_actual_move(body());
        return false;
    }
    const bool work=offer.kind!=ModelReturnState::Kind::recall;
    if(work){
        ++model.word_revision;
        const auto started=model.funding.start(model_funding_token(),model_work_bounds(selected),fuel_remaining());
        model.funding_reason=int(started);
        if(started!=return_funding::Reason::ok||
           !model.funding.begin(return_funding::Phase::action,model_funding_token(),fuel_remaining())){
            model.last_reason=16;
            return model.frame.can_move&&model.steps?model_actual_move(body()):false;
        }
    }
    auto me=body();if(point(me)!=model.origin)return false;
    const auto flags=state(me);const auto restrictions=self().restrictions();
    if(flags.is_dead()||flags.contains(champion_state::recalling)||flags.contains(champion_state::submitting_gems)||
       flags.contains(champion_state::teleporting)||self().buffs().has_stun())return false;
    if(offer.kind==ModelReturnState::Kind::recall){
        if(last_action_round==epoch||restrictions.bits()!=0)return false;
        self().recall();last_action_round=epoch;recovering=true;task=Task::recover;
        ++model.actual_recalls;++model.effect;++model.applied[5];model.last_reason=10;return true;
    }
    bool applied=false;
    if(offer.kind==ModelReturnState::Kind::basic){
        if(!flags.contains(champion_state::can_attack)||restrictions.contains(buff_restrictions::attack)||offer.member<0)return false;
        const auto& member=members[std::size_t(offer.member)];
        if(!sees(member.pos)||dist(point(me),member.pos)>me.attack_range||!member.handle.state().is_alive())return false;
        self().attack(member.handle);attacked=true;applied=true;
    }else{
        if(!flags.contains(champion_state::can_cast)||restrictions.contains(buff_restrictions::cast)||offer.slot<0)return false;
        const auto abilities=self().abilities();const auto& ability=abilities[std::size_t(offer.slot)];
        if(ability.cooldown()!=0)return false;
        if(offer.kind==ModelReturnState::Kind::shield)applied=ability.use(self());
        else if(offer.kind==ModelReturnState::Kind::ground_cast){
            if(!sees(offer.center)||static_blocked(offer.center))return false;
            applied=ability.use(offer.center);
        }else{
            if(offer.member<0||!sees(members[std::size_t(offer.member)].pos)||
                !members[std::size_t(offer.member)].handle.state().is_alive())return false;
            applied=ability.use(members[std::size_t(offer.member)].handle);
        }
    }
    if(applied){
        ++model.effect;last_action_round=epoch;++model.actual_work;++model.applied[std::size_t(offer.kind)];
    }else{++model.actual_false;model.last_reason=11;}
    return_funding::Witness action_receipt;action_receipt.actual_attempt=true;action_receipt.applied=applied;
    model_finish_phase(return_funding::Phase::action,action_receipt);
    // Receipt: the actual SDK effect exists. Predicted kills/control are NOT
    // copied into the actual frame. Query life/HP/buffs and actual MAC first.
    if(model.funding.valid())model.funding.begin(return_funding::Phase::feedback,model_funding_token(),fuel_remaining());
    me=body();
    if(applied)model_refresh_after_work(me,model.candidates[selected].word[0]);
    else{
        auto fresh=vision::visible_tiles();fresh.export_linear_buffer(visible);++model.observation;
        model.frame.observation=model.observation;
        const auto actual_flags=state(me);const auto actual_restrictions=self().restrictions();
        model.frame.can_move=actual_flags.contains(champion_state::can_move)&&!actual_restrictions.contains(buff_restrictions::move);
        model.support_revision=support_detail.revision();
    }
    return_funding::Witness feedback;feedback.actual_feedback=true;feedback.failure_closed=!applied;
    feedback.movement_ready=model.frame.can_move;feedback.movement_spent=!model.frame.can_move;
    model_finish_phase(return_funding::Phase::feedback,feedback);
    if(model.funding.valid())model.funding.begin(return_funding::Phase::reprice,model_funding_token(),fuel_remaining());
    const bool geometry=model_refresh_geometry(me);
    if(!geometry){model.last_reason=14;return applied;}
    if(!model.steps){
        return_funding::Witness terminal;terminal.actual_reprice=true;terminal.return_complete=true;
        model_finish_phase(return_funding::Phase::reprice,terminal);model.last_reason=15;return applied;
    }
    if(!model.frame.can_move){
        return_funding::Witness repriced;repriced.actual_reprice=true;
        model_finish_phase(return_funding::Phase::reprice,repriced);model.last_reason=12;return applied;
    }
    model.candidate_count=0;model.evaluated_count=0;model.best={};model.risk={};model_offer_move(me);
    if(!model.candidate_count){model.last_reason=12;return applied;}
    auto continuation=model_select();
    return_funding::Witness repriced;repriced.actual_reprice=true;
    model_finish_phase(return_funding::Phase::reprice,repriced);
    if(continuation.index!=std::numeric_limits<std::size_t>::max()){
        model.last_quote=continuation.quote;
        if(model.funding.valid())model.funding.begin(return_funding::Phase::physical,model_funding_token(),fuel_remaining());
        const bool moved=model_actual_move(me);
        return_funding::Witness physical;physical.actual_attempt=model.last_reason==8||model.last_reason==9;
        physical.applied=moved;physical.actual_feedback=!moved&&model.last_reason==9;
        physical.failure_closed=physical.actual_feedback;
        model_finish_phase(return_funding::Phase::physical,physical);
        return applied||moved;
    }
    return applied;
}
bool modeled_return_step(const Info& original){
    auto& model=model_return_state();
    if(model.owner_epoch==epoch)return false;
    model.owner_epoch=epoch;model.last_reason=0;model.candidate_count=0;
    model.evaluated_count=0;model.best={};model.risk={};
    auto me=body();const auto flags=state(me);
    if(flags.is_dead()||flags.contains(champion_state::recalling)||flags.contains(champion_state::submitting_gems)||
        flags.contains(champion_state::teleporting)||self().buffs().has_stun())return false;
    if(!fuel(62000)){model.last_reason=1;return planned_return_step(original);}
    auto fresh=vision::visible_tiles();fresh.export_linear_buffer(visible);
    if(!prepare_route(me)){model.last_reason=2;return false;}
    model.route=planned_route();model.origin=point(me);model.attack_range=me.attack_range;
    if(!model.route.valid()||!model.route.fresh()||!model.route.at(rp(model.origin))){model.last_reason=2;return false;}
    model.full_steps=model.route.size()-1;model.steps=std::min<std::size_t>(model.full_steps,6);
    model.truncated=model.steps<model.full_steps;
    if(!model.steps){returning=false;model.last_reason=3;return false;}
    // A truncated forecast is explicitly a progressing model escape: the full
    // observed geometric Route remains retained, and every actual step reduces
    // its remaining suffix. It never receives a quiet-terminal certificate.
    if(!model_collect_sources(me)){model.last_reason=4;return false;}
    // Pay a REAL executable incumbent's price before any optional target or
    // spell menus. Later quotes are evaluated once; their search cannot force
    // a repeated evaluation of this already retained incumbent.
    model_offer_move(me);model_select();model_offer_recall(me);model_select();
    if(fuel(model_retained_tail()+20000)){model_offer_basic(me);model_select();}
    for(int slot=0;slot<2&&fuel(model_retained_tail()+22000);++slot){model_offer_cast(me,slot);model_select();}
    const auto selected=model_select();
    if(selected.index==std::numeric_limits<std::size_t>::max()){
        // A relevant ward may expose a hidden dangerous source, never pretend
        // it asleep. An accepted ward is actual information work; subsequent
        // owner input rebuilds the complete quote from fresh facts.
        for(std::size_t source=0;source<model.frame.source_count;++source)
            if(!model.frame.sources[source].fresh&&model.member[source]!=std::numeric_limits<std::size_t>::max()&&fuel(40000)){
                if(reveal_work(int(model.member[source]),me)){model.last_reason=13;return true;}
                break;
            }
        model.last_reason=5;return false;
    }
    (void)original;return model_execute(selected.index,selected.quote);
}
bool modeled_return_owns_input(){
    if(!returning)return false;
    const auto me=body();const auto flags=state(me);
    if(flags.is_dead()||flags.contains(champion_state::recalling)||flags.contains(champion_state::submitting_gems)||
        flags.contains(champion_state::teleporting)||self().buffs().has_stun())return false;
    if(exposure(point(me)).amount==0){returning=false;return false;}
    diag_stage(8);modeled_return_step(me);return true;
}
