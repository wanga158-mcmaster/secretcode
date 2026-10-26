// Included inside Runtime; joint_world.h is included at file scope.
// This adapter nominates a bounded joint model. It never owns an input, and a
// quote is not authority to issue an SDK command. The caller retains its paid
// executable fallback while this observation/menu prefix is being prepared.
struct JointContactBudget {
    std::uint64_t command_tail{},query_bound{},enumeration_bound{},evaluation_bound{};
    bool declared{};
};
// Used by BOTH SDK preparation and the pure resource regression. Until one
// admitted incumbent is priced, every preparation step retains its first full
// evaluation. Pricing itself charges that evaluation once, never twice.
struct JointContactFunding {
    std::uint64_t command{},evaluation{};bool incumbent{};
    static std::uint64_t add(std::uint64_t a,std::uint64_t b){
        const auto bad=std::numeric_limits<std::uint64_t>::max();return b>bad-a?bad:a+b;
    }
    static bool valid(const JointContactBudget& budget){
        return budget.declared&&budget.command_tail&&budget.query_bound&&budget.enumeration_bound&&budget.evaluation_bound&&
            add(budget.command_tail,budget.evaluation_bound)!=std::numeric_limits<std::uint64_t>::max();
    }
    std::uint64_t retained()const{return incumbent?command:add(command,evaluation);}
    bool preparation(std::uint64_t fuel,std::uint64_t cost)const{
        const auto total=add(retained(),cost);return command&&evaluation&&total!=std::numeric_limits<std::uint64_t>::max()&&fuel>total;
    }
    bool price(std::uint64_t fuel)const{
        const auto total=add(command,evaluation);return command&&evaluation&&total!=std::numeric_limits<std::uint64_t>::max()&&fuel>total;
    }
    bool family(std::uint64_t fuel,std::uint64_t build_cost)const{
        const auto total=add(add(command,evaluation),build_cost);
        return command&&evaluation&&total!=std::numeric_limits<std::uint64_t>::max()&&fuel>total;
    }
    bool executable(std::uint64_t fuel)const{return incumbent&&command&&fuel>command;}
};
#ifndef MOSAIC_JOINT_CONTACT_PURE_ONLY
struct JointContactRequest {
    // Optional nominated itinerary, excluding the actual origin. No candidate
    // family below requires this itinerary to exist.
    std::array<vec2,5> path{};unsigned path_count{},horizon{6};
    int capture_group{-1};vec2 altar{};bool altar_known{},altar_unlocked{};
    vec2 spawn{};bool spawn_known{};
    bool no_exogenous_wake{},allow_approximate{},allow_fatal{};
    unsigned remaining_refusals{1};
    std::uint64_t observation_intent{};unsigned observation_cursor{},observation_total{};
    vec2 ward_point{};bool ward_relevant{};
    joint_world::Purpose path_purpose{joint_world::Purpose::flee};
};
enum class JointContactStatus {empty,partial,ready,no_offers,capacity,invalid_budget,overrun,stale,unsupported};
struct JointContactState {
    struct Record {
        std::uint32_t handle{};int absolute_team{},type{},seen{-1};
        bool identity{},design_known{},ever_seen{},fresh{},details{},alive_known{},alive{};
        bool priorities{},completed{};int current_priority{},next_priority{};
        std::uint8_t restrictions{};
        unsigned next_rank_min{},next_rank_max{},current_opportunity_min{},current_opportunity_max{},next_opportunity_min{},next_opportunity_max{};
        Info body{};detail::champion_design design{};
        joint_world::Actor last{};
        float queried_damage[2]{};bool damage_queried[2]{};
    };
    enum class Phase:unsigned {roster,bodies,details,camps,frontiers,menu,evaluation,count};
    struct Cost {
        std::uint64_t spent{},max_phase{},max_query{},queries{},enumerations{},max_enumeration{},overruns{};
    };
    joint_world::World world;
    std::array<joint_world::Candidate,joint_world::candidate_capacity> candidates;
    std::array<Record,joint_world::actor_capacity> records;
    std::array<std::size_t,joint_world::camp_capacity> camp_members{};
    std::array<std::uint32_t,joint_world::actor_capacity> handles{};
    std::array<std::array<unsigned,3>,joint_world::actor_capacity> reply_index{};
    std::array<Cost,unsigned(Phase::count)> costs{};
    std::array<detail::item_info,std::size_t(item_type::enum_type::count)> item_info{};
    std::array<bool,std::size_t(item_type::enum_type::count)> item_known{};
    std::optional<bitboard> occupied,move_blocked,dash_blocked,own_moves;
    std::array<std::optional<bitboard>,2> own_dashes;
    std::array<int,joint_world::camp_capacity> group_ids{};
    std::array<unsigned,joint_world::camp_capacity> group_states{};
    std::array<bool,joint_world::camp_capacity> group_known{};
    JointContactRequest request;JointContactBudget budget;
    JointContactFunding funding;
    joint_world::Selection selection;
    unsigned count{},allies{},enemies{},roster_stage{},candidate_count{},group_count{};
    unsigned body_cursor{},camp_queries{},reply_classes{},unsearched_classes{},unknown_power{};
    unsigned world_revision{},effect_revision{},observation_revision{},declared_bounds_overrun{},evaluated_count{};
    unsigned prepared{},partial{},no_offers{},quoted{},selected{};
    unsigned actual_effects{},actual_refusals{},actual_refreshes{};
    int owner_epoch{-1};Phase phase{Phase::roster};JointContactStatus status{JointContactStatus::empty};
    JointContactStatus enrichment_status{JointContactStatus::empty};
    std::uint64_t phase_start{};
    bool roster_ready{},omitted_enemy_support{},unknown_schedule{},visible_ready{},refusal_branch{};
    bool count_derived_tail{true};
};
static JointContactState& joint_contact_state(){static constinit JointContactState value{};return value;}
static joint_world::Point joint_point(vec2 p){return {p.x,p.y};}
static vec2 joint_vec(joint_world::Point p){return {p.x,p.y};}
std::uint64_t joint_contact_candidate_tail(unsigned count)const{
    return joint_contact_state().count_derived_tail?joint_contact_word_tail(count):joint_contact_state().budget.command_tail;
}
unsigned joint_contact_bootstrap_words(const Info& me)const{
    const auto& contact=joint_contact_state();const auto own=std::uint32_t(handle_champion_t(self()));
    const auto flags=state(me);
    for(const auto& record:contact.records)if(record.identity&&record.design_known&&record.handle==own){
        bool reset=false;for(const auto& ability:record.design.definition.abilities)
            reset=reset||ability.kind==unsigned(ability_kind::attack_reset);
        return flags.contains(champion_state::can_cast)&&reset?
            (flags.contains(champion_state::can_attack)?3u:2u):1u;
    }
    // Cold immutable design is genuinely unknown. Its first guarded discovery
    // retains the full class; cached later inputs do not pay that maximum.
    return 4;
}
unsigned joint_contact_family_words(unsigned family)const{
    const auto& contact=joint_contact_state();const auto& owner=contact.world.actors[contact.world.owner];
    if constexpr(cfg::local_observation)if(family==0)
        return 1+unsigned(contact.request.path_count&&owner.can_move&&!owner.ranged);
    if constexpr(cfg::local_observation)if(family==6)return 1;
    bool reset=false;for(const auto& ability:owner.abilities)reset=reset||
        (owner.can_cast&&ability.enabled&&!ability.cooldown&&ability.kind==joint_world::AbilityKind::reset);
    if(family==0){unsigned count=owner.can_attack?(reset?3u:1u):(reset?2u:1u);
        return count+unsigned(contact.request.path_count&&owner.can_move&&!owner.ranged);}
    if(family==7)return 3;
    if(family==6)return 2;
    if(family==1||family==2){const auto& ability=owner.abilities[family-1];
        return 1+unsigned(contact.request.path_count&&owner.can_move&&!ability.exclusive_with_walk&&
            ability.kind!=joint_world::AbilityKind::dash);}
    return 1;
}
bool joint_contact_funded(std::uint64_t extra)const{
    const auto& contact=joint_contact_state();const auto now=fuel_remaining();
    return contact.owner_epoch==epoch&&int(round_number())==epoch&&
        contact.funding.preparation(now,extra);
}
void joint_contact_phase(JointContactState::Phase phase){
    auto& contact=joint_contact_state();const auto now=fuel_remaining();
    if(contact.phase_start){auto& cost=contact.costs[unsigned(contact.phase)];
        const auto spent=contact.phase_start>=now?contact.phase_start-now:0;
        cost.spent+=spent;cost.max_phase=std::max(cost.max_phase,spent);}
    contact.phase=phase;contact.phase_start=now;
}
template<class F> bool joint_contact_query(F&& query,int& code){
    auto& contact=joint_contact_state();if(!joint_contact_funded(contact.budget.query_bound)){
        contact.status=JointContactStatus::partial;return false;}
    const auto before=fuel_remaining();code=query();const auto after=fuel_remaining();
    auto& cost=contact.costs[unsigned(contact.phase)];const auto spent=before>=after?before-after:0;
    ++cost.queries;cost.max_query=std::max(cost.max_query,spent);++contact.observation_revision;
    if(spent>contact.budget.query_bound||!contact.funding.preparation(after,0)||int(round_number())!=epoch){
        ++cost.overruns;++contact.declared_bounds_overrun;contact.status=JointContactStatus::overrun;return false;}
    return true;
}
static bool joint_contact_hidden(int code){return code==int(error_code::api_not_in_vision);}
template<class F> bool joint_contact_compute(F&& compute){
    auto& contact=joint_contact_state();if(!joint_contact_funded(contact.budget.enumeration_bound)){
        contact.status=JointContactStatus::partial;return false;}
    const auto before=fuel_remaining();compute();const auto after=fuel_remaining();
    auto& cost=contact.costs[unsigned(contact.phase)];const auto spent=before>=after?before-after:0;
    ++cost.enumerations;cost.max_enumeration=std::max(cost.max_enumeration,spent);
    if(spent>contact.budget.enumeration_bound||!contact.funding.preparation(after,0)||int(round_number())!=epoch){
        ++cost.overruns;++contact.declared_bounds_overrun;contact.status=JointContactStatus::overrun;return false;}
    return true;
}
bool joint_contact_roster(){
    auto& contact=joint_contact_state();int code=0;
    if(contact.roster_ready)return true;
    if(contact.roster_stage==0){std::uint32_t count=0;
        if(!joint_contact_query([&]{return detail::sdk_num_allies(&count);},code))return false;
        detail::check(code);contact.allies=count;contact.roster_stage=1;}
    if(contact.roster_stage==1){std::uint32_t count=0;
        if(!joint_contact_query([&]{return detail::sdk_num_enemies(&count);},code))return false;
        detail::check(code);contact.enemies=count;contact.roster_stage=2;}
    if(contact.allies+contact.enemies>joint_world::actor_capacity){contact.status=JointContactStatus::capacity;return false;}
    if(contact.roster_stage==2){std::uint32_t count=contact.allies;
        if(!joint_contact_query([&]{return detail::sdk_allies(contact.handles.data(),contact.allies,&count);},code))return false;
        detail::check(code);if(count!=contact.allies){contact.status=JointContactStatus::capacity;return false;}
        contact.roster_stage=3;}
    if(contact.roster_stage==3){std::uint32_t count=contact.enemies;
        if(!joint_contact_query([&]{return detail::sdk_enemies(contact.handles.data()+contact.allies,contact.enemies,&count);},code))return false;
        detail::check(code);if(count!=contact.enemies){contact.status=JointContactStatus::capacity;return false;}
        contact.count=contact.allies+contact.enemies;contact.roster_stage=4;}
    for(unsigned index=0;index<contact.count;++index){auto& record=contact.records[index];record.handle=contact.handles[index];
        if(!record.identity){detail::champion_identity identity{};
            if(!joint_contact_query([&]{return detail::sdk_champion_identity(record.handle,&identity);},code))return false;
            detail::check(code);record.absolute_team=identity.team;record.type=identity.type;record.identity=true;}
        if(!record.design_known){
            if(!joint_contact_query([&]{return detail::sdk_champion_design(record.type,&record.design);},code))return false;
            detail::check(code);record.design_known=true;}}
    contact.roster_ready=true;return true;
}
static unsigned joint_contact_targets(std::uint32_t bits){
    unsigned result=0;
    if(bits&detail::ability_target_self)result|=joint_world::target_self;
    if(bits&detail::ability_target_allies)result|=joint_world::target_ally;
    if(bits&detail::ability_target_enemies)result|=joint_world::target_enemy;
    if(bits&detail::ability_target_camps)result|=joint_world::target_camp;
    if(bits&detail::ability_target_ground)result|=joint_world::target_ground;
    return result;
}
static void joint_contact_ability(joint_world::Ability& out,const detail::ability_props& props,double focus){
    out={};out.enabled=props.kind!=0;out.cooldown_on_cast=props.cooldown;
    out.target_mask=joint_contact_targets(props.targets);out.ground=(props.targets&detail::ability_target_ground)!=0;
    out.exclusive_with_walk=(props.attributes&detail::ability_attribute_exclusive_with_movement)!=0;
    if(!props.kind)return;
    switch(ability_kind(props.kind)){
    case ability_kind::attack_reset:out.kind=joint_world::AbilityKind::reset;
        out.enhance_next_stun=props.payload.enhanced_attack?1:0;break;
    case ability_kind::damage:out.kind=joint_world::AbilityKind::damage;
        out.range=int(std::floor(props.payload.damage.range));out.damage=focus*props.payload.damage.modifier;break;
    case ability_kind::aoe_damage:out.kind=joint_world::AbilityKind::damage;
        out.range=int(std::floor(props.payload.aoe_damage.range));out.radius=int(std::floor(props.payload.aoe_damage.radius));
        out.damage=focus*props.payload.aoe_damage.modifier;out.stun_ticks=props.payload.aoe_damage.duration;break;
    case ability_kind::cc:out.kind=joint_world::AbilityKind::control;
        out.range=int(std::floor(props.payload.cc.range));out.damage=focus*props.payload.cc.modifier;
        out.stun_ticks=props.payload.cc.duration;break;
    case ability_kind::dash:out.kind=joint_world::AbilityKind::dash;
        out.range=int(std::floor(props.payload.dash.range));out.radius=int(std::floor(props.payload.dash.radius));
        out.damage=focus*props.payload.dash.modifier;out.stun_ticks=props.payload.dash.duration;break;
    case ability_kind::shield:out.kind=joint_world::AbilityKind::shield;
        out.range=int(std::floor(props.payload.shield.range));out.shield=focus*props.payload.shield.modifier;
        out.shield_ticks=props.payload.shield.duration;break;
    default:out.enabled=false;break;
    }
}
void joint_contact_actor_base(unsigned index){
    auto& contact=joint_contact_state();auto& record=contact.records[index];auto& actor=contact.world.actors[index];
    actor={};actor.id=record.handle;actor.team=unsigned(record.absolute_team);
    const auto& design=record.design;const auto& stats=design.base_stats;
    const Info* current=record.fresh?&record.body:record.ever_seen?&record.body:nullptr;
    actor.health=current?std::max(0.0f,current->health):stats.base_health;
    actor.max_health=current?std::max(1.0f,current->max_health):std::max(1.0f,stats.base_health);
    actor.basic_damage=current?std::max(0.0f,current->strength):std::max(0.0f,stats.base_strength);
    actor.attack_range=current?current->attack_range:stats.base_attack_range;actor.ranged=actor.attack_range>1;
    actor.move_speed=current?current->move_speed:stats.base_move_speed;
    actor.camp_damage_multiplier=stats.camp_damage_multiplier;
    const auto passive=passive_type(design.definition.passive_type);
    actor.stun_immune=passive==passive_type::stun_immunity;
    actor.missing_health_bonus=passive==passive_type::missing_health_damage?
        constants::passive_abilities::heart_of_ruin_missing_health_damage_max_bonus:0;
    actor.retaliation=passive==passive_type::retaliation?
        (current?current->focus:stats.base_focus)*constants::passive_abilities::ren_retaliation_focus_ratio:0;
    actor.basic_splash_radius=passive==passive_type::splash_attack?int(constants::passive_abilities::varg_splash_radius):0;
    actor.refresh_cooldowns_on_kill=passive==passive_type::reset_cooldowns_on_kill;
    const double focus=current?current->focus:stats.base_focus;
    for(unsigned slot=0;slot<2;++slot)joint_contact_ability(actor.abilities[slot],design.definition.abilities[slot],focus);
    if(record.fresh){const auto flags=state(record.body);actor.position=joint_point(point(record.body));
        actor.belief_center=actor.position;actor.knowledge=record.handle==std::uint32_t(handle_champion_t(self()))?
            joint_world::Knowledge::owner:joint_world::Knowledge::fresh_champion;
        actor.alive=flags.contains(champion_state::alive);actor.can_move=flags.contains(champion_state::can_move);
        actor.can_attack=flags.contains(champion_state::can_attack);actor.can_cast=flags.contains(champion_state::can_cast);
        actor.walked=actor.ranged&&!actor.can_move&&!actor.can_attack;
    }else{
        actor.knowledge=joint_world::Knowledge::retained_hidden;actor.can_move=actor.can_attack=actor.can_cast=true;
        actor.position=record.ever_seen?joint_point(point(record.body)):joint_point(contact.request.spawn_known?contact.request.spawn:previous);
        actor.belief_center=actor.position;int dash=0;
        for(const auto& ability:actor.abilities)if(ability.enabled&&ability.kind==joint_world::AbilityKind::dash)dash=std::max(dash,ability.range);
        const int age=record.ever_seen?std::max(0,epoch-record.seen):int(walls->width()+walls->height());
        actor.belief_radius=std::min(int(walls->width()+walls->height()),age*(std::max(1,actor.move_speed)+dash)+std::max(1,actor.move_speed));
        // Last observed equipment is an estimate, not an upper bound on future
        // purchases. Never-seen power and all hidden growth stay explicit.
        ++contact.unknown_power;contact.world.effects_complete=false;
    }
    if(record.alive_known&&!record.alive){
        actor.alive=false;contact.world.no_respawn=false;contact.world.effects_complete=false;
    }
    if(!record.fresh&&record.alive_known&&record.alive&&actor.health<=0)actor.health=actor.max_health;
    if(record.handle==std::uint32_t(handle_champion_t(self())))contact.world.owner=index;
}
bool joint_contact_bodies(const Info& me){
    auto& contact=joint_contact_state();contact.world.actor_count=contact.count;int code=0;
    for(unsigned index=0;index<contact.count;++index){auto& record=contact.records[index];
        record.fresh=false;record.details=false;record.priorities=false;record.damage_queried[0]=record.damage_queried[1]=false;
        Info current{};
        if(record.handle==std::uint32_t(handle_champion_t(self())))current=me;
        else{
            if(!joint_contact_query([&]{return detail::sdk_champion(record.handle,&current);},code))return false;
            if(!joint_contact_hidden(code))detail::check(code);
        }
        if(record.handle==std::uint32_t(handle_champion_t(self()))||!joint_contact_hidden(code)){
            record.body=current;record.seen=epoch;record.ever_seen=true;record.fresh=true;
            record.completed=state(current).contains(champion_state::completed_turn);
            if(!joint_contact_query([&]{return detail::sdk_bc_champion_priority(record.handle,0,&record.current_priority);},code))return false;
            detail::check(code);
            if(!joint_contact_query([&]{return detail::sdk_bc_champion_priority(record.handle,1,&record.next_priority);},code))return false;
            detail::check(code);record.priorities=true;
        }else contact.unknown_schedule=true;
        std::int32_t alive=0;
        if(!joint_contact_query([&]{return detail::sdk_champion_is_alive(record.handle,&alive);},code))return false;
        detail::check(code);record.alive_known=true;record.alive=alive!=0;
        joint_contact_actor_base(index);++contact.body_cursor;
    }
    return true;
}
bool joint_contact_items(unsigned index){
    auto& contact=joint_contact_state();auto& actor=contact.world.actors[index];const auto& record=contact.records[index];
    std::array<std::uint32_t,std::size_t(item_type::enum_type::count)> counts{};int code=0;
    if(!joint_contact_query([&]{return detail::sdk_champion_items(record.handle,counts.data());},code))return false;
    if(joint_contact_hidden(code)){contact.world.effects_complete=false;return true;}detail::check(code);
    double durability=0,jungle=0,monster=0;
    for(std::size_t item=0;item<counts.size();++item)if(counts[item]){
        if(!contact.item_known[item]){
            if(!joint_contact_query([&]{return detail::sdk_item_info(std::uint32_t(item),&contact.item_info[item]);},code))return false;
            detail::check(code);contact.item_known[item]=true;
        }
        const auto& info=contact.item_info[item];const auto magnitude=std::clamp(double(info.passive_magnitude),0.0,1.0);
        if(info.passive==unsigned(item_passive::durability))durability=std::max(durability,magnitude);
        if(info.passive==unsigned(item_passive::jungle))jungle=std::max(jungle,magnitude);
        if(info.passive==unsigned(item_passive::monster))monster=std::max(monster,magnitude);
    }
    actor.damage_taken=1.0-durability;actor.camp_damage_taken=1.0-jungle;
    actor.camp_damage_multiplier*=1.0+monster;return true;
}
bool joint_contact_details(unsigned index){
    auto& contact=joint_contact_state();auto& record=contact.records[index];auto& actor=contact.world.actors[index];
    if(!record.fresh)return true;int code=0;std::uint8_t restrictions=0;
    if(!joint_contact_query([&]{return detail::sdk_champion_restrictions(record.handle,&restrictions);},code))return false;
    detail::check(code);actor.can_move=actor.can_move&&!(restrictions&1);actor.can_attack=actor.can_attack&&!(restrictions&2);
    record.restrictions=restrictions;
    actor.can_cast=actor.can_cast&&!(restrictions&4);
    std::array<detail::buff_data,8> buffs{};std::uint32_t count=0;
    if(!joint_contact_query([&]{return detail::sdk_champion_buff_data(record.handle,buffs.data(),buffs.size(),&count);},code))return false;
    detail::check(code);if(count>buffs.size()){contact.world.effects_complete=false;return true;}
    for(unsigned n=0;n<count;++n){const auto& buff=buffs[n];
        if(buff.type==unsigned(detail::buff_kind::shield)){
            actor.shield.amount+=std::max(0.0f,buff.data.shield.value);
            actor.shield.ticks=actor.shield.ticks<0?buff.data.shield.duration:std::min(actor.shield.ticks,buff.data.shield.duration);
        }else if(buff.type==unsigned(detail::buff_kind::stun))actor.stun_ticks=std::max(actor.stun_ticks,buff.data.stun.duration);
        else if(buff.type==unsigned(detail::buff_kind::enhanced_attack)){
            actor.enhanced_attack_ready=true;
            for(const auto& ability:actor.abilities)actor.enhanced_stun_ticks=std::max(actor.enhanced_stun_ticks,ability.enhance_next_stun);
        }
    }
    for(unsigned slot=0;slot<2;++slot){detail::ability_props props{};
        if(!joint_contact_query([&]{return detail::sdk_champion_ability_props(record.handle,slot,&props);},code))return false;
        detail::check(code);joint_contact_ability(actor.abilities[slot],props,record.body.focus);
        if(!actor.abilities[slot].enabled){if(props.kind)contact.world.effects_complete=false;continue;}
        if(!joint_contact_query([&]{return detail::sdk_champion_cooldown(record.handle,slot,&actor.abilities[slot].cooldown);},code))return false;
        detail::check(code);
        // The metadata product is the unscaled branch-dependent model input.
        // The host query is telemetry, never blindly multiplied by Heart again.
        if(actor.abilities[slot].damage>0){
            if(!joint_contact_query([&]{return detail::sdk_champion_ability_damage(record.handle,slot,&record.queried_damage[slot]);},code))return false;
            detail::check(code);record.damage_queried[slot]=true;
        }
    }
    if(!joint_contact_items(index))return false;
    if(!joint_contact_query([&]{return detail::sdk_champion_gems(record.handle,&actor.gems);},code))return false;
    detail::check(code);
    const auto flags=state(record.body);
    if(flags.contains(champion_state::recalling)||flags.contains(champion_state::submitting_gems)){
        if(!joint_contact_query([&]{return detail::sdk_champion_channel_time(record.handle,&actor.channel.ticks);},code))return false;
        detail::check(code);actor.channel.kind=flags.contains(champion_state::recalling)?joint_world::ChannelKind::recall:joint_world::ChannelKind::bank;
        actor.channel.destination=joint_point(contact.request.spawn);
    }
    record.last=actor;record.details=true;return true;
}
bool joint_contact_relevant(std::size_t index)const{
    const auto& contact=joint_contact_state();const auto& member=members[index];const auto origin=joint_vec(contact.world.actors[contact.world.owner].position);
    int extra=std::max(0,contact.world.actors[contact.world.owner].move_speed);for(const auto& ability:contact.world.actors[contact.world.owner].abilities)
        if(ability.enabled&&ability.kind==joint_world::AbilityKind::dash)extra=std::max(extra,ability.range);
    if(dist(origin,member.pos)<=member.range+extra)return true;
    for(unsigned n=0;n<contact.request.path_count;++n)if(dist(contact.request.path[n],member.pos)<=member.range+extra)return true;
    if(member.group==contact.request.capture_group||member.group==job)return true;
    for(unsigned n=0;n<contact.count;++n){const auto& actor=contact.world.actors[n];
        if(actor.team==contact.world.actors[contact.world.owner].team||!actor.alive)continue;
        if(joint_world::distance(actor.belief_center,joint_point(member.pos))<=member.range+actor.belief_radius+
            int(contact.request.horizon)*std::max(1,actor.move_speed))return true;}
    return false;
}
bool joint_contact_hits_own_itinerary(std::size_t index)const{
    const auto& contact=joint_contact_state();const auto& member=members[index];
    const auto& owner=contact.world.actors[contact.world.owner];int extra=std::max(0,owner.move_speed);
    for(const auto& ability:owner.abilities)if(ability.enabled&&ability.kind==joint_world::AbilityKind::dash)extra=std::max(extra,ability.range);
    if(joint_world::distance(owner.position,joint_point(member.pos))<=member.range+extra)return true;
    for(unsigned n=0;n<contact.request.path_count;++n)if(dist(contact.request.path[n],member.pos)<=member.range+extra)return true;
    return false;
}
unsigned joint_contact_group(int group){
    auto& contact=joint_contact_state();for(unsigned n=0;n<contact.group_count;++n)if(contact.group_ids[n]==group)return n;
    if(contact.group_count==joint_world::camp_capacity)return unsigned(joint_world::camp_capacity);
    const auto result=contact.group_count++;contact.group_ids[result]=group;contact.group_known[result]=false;return result;
}
bool joint_contact_camps(){
    auto& contact=joint_contact_state();auto& world=contact.world;world.camp_count=0;world.aggregate={};
    contact.omitted_enemy_support=false;contact.group_count=0;int code=0;
    // Coverage is acquired before optional fresh queries. The aggregate never
    // receives an individual kill/stun/sleep credit, including collateral wake.
    std::array<int,joint_world::camp_capacity> scores{};
    for(std::size_t index=0;index<members.size();++index){
        if((index&15u)==0&&!joint_contact_funded(contact.budget.enumeration_bound)){contact.status=JointContactStatus::partial;return false;}
        if(!joint_contact_relevant(index))continue;const auto& member=members[index];
        int score=joint_world::distance(world.actors[world.owner].position,joint_point(member.pos))-member.range;
        for(unsigned n=0;n<contact.request.path_count;++n)score=std::min(score,dist(contact.request.path[n],member.pos)-member.range);
        if(member.group==job)score-=1000;if(member.group==contact.request.capture_group)score-=2000;
        unsigned at=0;while(at<world.camp_count&&scores[at]<=score)++at;
        if(at<joint_world::camp_capacity){const auto end=std::min(world.camp_count,unsigned(joint_world::camp_capacity-1));
            for(unsigned n=end;n>at;--n){scores[n]=scores[n-1];contact.camp_members[n]=contact.camp_members[n-1];}
            scores[at]=score;contact.camp_members[at]=index;world.camp_count=std::min(world.camp_count+1,unsigned(joint_world::camp_capacity));
        }
    }
    for(std::size_t index=0;index<members.size();++index){
        if((index&15u)==0&&!joint_contact_funded(contact.budget.enumeration_bound)){contact.status=JointContactStatus::partial;return false;}
        if(!joint_contact_relevant(index))continue;bool included=false;
        for(unsigned n=0;n<world.camp_count;++n)included=included||contact.camp_members[n]==index;
        if(included)continue;contact.omitted_enemy_support=true;const auto& member=members[index];
        if(joint_contact_hits_own_itinerary(index)&&member.base_strength>0){world.aggregate.present=true;
            world.aggregate.damage+=std::ceil(std::max(double(member.base_strength),member.seen==epoch?double(member.strength):0.0))+1;}
    }
    for(unsigned n=0;n<world.camp_count;++n){const auto& member=members[contact.camp_members[n]];auto& camp=world.camps[n];camp={};
        camp.id=std::uint32_t(handle_camp_t(member.handle));camp.group=unsigned(member.group);camp.position=joint_point(member.pos);
        camp.range=member.range;camp.damage=std::max(0.0f,member.base_strength);camp.health=member.base_hp;
        camp.alive=true;camp.health_known=false;camp.knowledge=joint_world::CampKnowledge::unknown;}
    world.aggregate.covers_omitted=true;world.aggregate.center=world.actors[world.owner].position;
    world.aggregate.reach=int(walls->width()+walls->height());
    if(contact.omitted_enemy_support)world.effects_complete=false;
    for(unsigned source=0;source<world.camp_count;++source){auto& camp=world.camps[source];const auto& member=members[contact.camp_members[source]];
        if(!sees(member.pos))continue;std::uint32_t flags=0;
        if(!joint_contact_query([&]{return detail::sdk_camp_state(camp.id,&flags);},code))return false;
        if(joint_contact_hidden(code))continue;detail::check(code);++contact.camp_queries;
        const auto group=joint_contact_group(member.group);
        if(group<joint_world::camp_capacity&&!contact.group_known[group]){
            if(!joint_contact_query([&]{return detail::sdk_camp_group_state(
                std::uint32_t(handle_camp_group_t(groups[std::size_t(member.group)])),&contact.group_states[group]);},code))return false;
            if(!joint_contact_hidden(code)){detail::check(code);contact.group_known[group]=true;}
        }
        if(flags==0){
            const bool no_respawn=group<joint_world::camp_capacity&&contact.group_known[group]&&
                (contact.group_states[group]==1||contact.group_states[group]==2)&&
                groups[std::size_t(member.group)].max_respawn_timer()>int(contact.request.horizon);
            if(no_respawn){camp.alive=false;camp.health=0;camp.health_known=true;}
            else world.no_respawn=false;
            continue;
        }
        camp.knowledge=flags==2?joint_world::CampKnowledge::fresh_asleep:joint_world::CampKnowledge::fresh_awake;
        detail::camp_state current{};
        if(!joint_contact_query([&]{return detail::sdk_camp(camp.id,&current);},code))return false;
        if(joint_contact_hidden(code)){camp.knowledge=joint_world::CampKnowledge::unknown;continue;}detail::check(code);
        camp.damage=std::max(0.0f,current.strength);camp.health=std::max(0.0f,current.health);
        std::array<detail::camp_buff_data,8> buffs{};std::uint32_t count=0;
        if(!joint_contact_query([&]{return detail::sdk_camp_buffs(camp.id,buffs.data(),buffs.size(),&count);},code))return false;
        if(joint_contact_hidden(code))continue;detail::check(code);
        if(count>buffs.size()){world.effects_complete=false;continue;}
        camp.health_known=true;
        for(unsigned n=0;n<count;++n){const auto& buff=buffs[n];
            if(buff.type==unsigned(detail::buff_kind::stun))camp.stun_ticks=std::max(camp.stun_ticks,buff.timer);
            else if(buff.type==unsigned(detail::buff_kind::shield)){camp.shield.amount+=std::max(0.0f,buff.magnitude);
                camp.shield.ticks=camp.shield.ticks<0?buff.timer:std::min(camp.shield.ticks,buff.timer);}
        }
    }
    return true;
}
static void joint_contact_append(joint_world::Word& word,joint_world::Action action){
    if(word.count<joint_world::attempt_capacity)word.actions[word.count++]=action;
}
static joint_world::Action joint_contact_action(joint_world::ActionKind kind,joint_world::Target target,
    unsigned index,joint_world::Point point={}){
    joint_world::Action result{};result.kind=kind;result.target=target;result.index=index;result.point=point;
    result.target_observed=true;result.path_legal=true;result.landing_observed_free=true;return result;
}
void joint_contact_envelope(unsigned index){
    auto& contact=joint_contact_state();auto& actor=contact.world.actors[index];auto& envelope=actor.remaining_reply;
    envelope={};envelope.valid=true;int dash=0,max_range=actor.attack_range,stun=actor.enhanced_stun_ticks;
    double damage=0;bool reset=false;
    for(const auto& ability:actor.abilities)if(ability.enabled){
        if(ability.kind==joint_world::AbilityKind::dash)dash=std::max(dash,ability.range);
        max_range=std::max(max_range,ability.range+ability.radius);damage=std::max(damage,ability.damage);
        stun=std::max(stun,ability.stun_ticks);envelope.defensive_shield=std::max(envelope.defensive_shield,ability.shield);
        reset=reset||ability.kind==joint_world::AbilityKind::reset;
    }
    envelope.reach=std::max(actor.move_speed+max_range,actor.move_speed+dash+actor.attack_range);
    envelope.travel=std::max(0,actor.move_speed)+dash;envelope.stun_ticks=stun;
    if(reset&&actor.basic_damage>damage){envelope.hits=2;envelope.damage[0]=actor.basic_damage;envelope.damage[1]=actor.basic_damage;}
    else if(damage>0){envelope.hits=2;envelope.damage[0]=damage;envelope.damage[1]=actor.basic_damage;}
    else{envelope.hits=1;envelope.damage[0]=actor.basic_damage;}
    if(contact.omitted_enemy_support&&actor.missing_health_bonus>0){
        // Omitted NPC damage can lower Heart's health before a reply. This
        // envelope takes the full missing-health multiplier; it is not copied
        // into the explicit word actor or applied twice by the pure transition.
        envelope.full_missing_health=true;
    }
    actor.frontier_complete=actor.knowledge!=joint_world::Knowledge::retained_hidden;
}
bool joint_contact_replies(){
    auto& contact=joint_contact_state();auto& world=contact.world;world.reply_count=0;const auto owner=world.owner;
    for(unsigned index=0;index<world.actor_count;++index){auto& actor=world.actors[index];
        if(actor.team==world.actors[owner].team)continue;joint_contact_envelope(index);
        int dash=-1,spell=-1,reset=-1,shield=-1;double strongest=-1;
        for(unsigned slot=0;slot<2;++slot){const auto& ability=actor.abilities[slot];if(!ability.enabled)continue;
            if(ability.kind==joint_world::AbilityKind::dash)dash=int(slot);
            else if(ability.kind==joint_world::AbilityKind::reset)reset=int(slot);
            else if(ability.kind==joint_world::AbilityKind::shield)shield=int(slot);
            else if(ability.damage+ability.stun_ticks*actor.basic_damage>strongest){strongest=ability.damage+ability.stun_ticks*actor.basic_damage;spell=int(slot);}}
        for(unsigned family=0;family<3;++family){
            if(world.reply_count==joint_world::reply_capacity){contact.status=JointContactStatus::capacity;return false;}
            contact.reply_index[index][family]=world.reply_count;auto& word=world.replies[world.reply_count++];word.count=0;
            auto move=joint_contact_action(joint_world::ActionKind::move,joint_world::Target::actor,owner);
            move.follow_target=true;move.optional_if_unavailable=true;
            auto basic=joint_contact_action(joint_world::ActionKind::basic,joint_world::Target::actor,owner);basic.optional_if_unavailable=true;
            auto cast=[&](int slot,bool follows){auto action=joint_contact_action(joint_world::ActionKind::cast,
                follows?(actor.abilities[unsigned(slot)].ground?joint_world::Target::ground:joint_world::Target::actor):joint_world::Target::self,follows?owner:index);
                action.slot=unsigned(slot);action.follow_target=follows;action.optional_if_unavailable=true;
                joint_contact_append(word,action);};
            if(family==0){ // Compatible walking/dash contacts are actual words.
                joint_contact_append(word,move);if(dash>=0)cast(dash,true);else if(spell>=0)cast(spell,true);
                joint_contact_append(word,basic);
            }else if(family==1){ // A standing hit/reset/hit is not movement.
                if(reset>=0){joint_contact_append(word,basic);cast(reset,false);joint_contact_append(word,basic);}
                else{if(spell>=0)cast(spell,true);else if(dash>=0)cast(dash,true);joint_contact_append(word,basic);}
                joint_contact_append(word,move);
            }else{if(shield>=0)cast(shield,false);joint_contact_append(word,move);
                if(shield<0&&dash>=0)cast(dash,true);joint_contact_append(word,basic);}
            ++contact.reply_classes;
        }
    }
    // These finite nominations do not exhaust adversarial response choices.
    // Separate envelope frontiers retain the unsearched equipped prefixes.
    world.effects_complete=false;return true;
}
bool joint_contact_event(joint_world::Frontier& frontier,joint_world::Event event){
    if(frontier.event_count==joint_world::event_capacity){joint_contact_state().status=JointContactStatus::capacity;return false;}
    frontier.events[frontier.event_count++]=event;return true;
}
bool joint_contact_frontier(unsigned at,bool late,unsigned family,bool envelope){
    auto& contact=joint_contact_state();auto& world=contact.world;auto& frontier=world.frontiers[at];
    frontier.event_count=0;frontier.position_overrides=0;frontier.envelope_actor_mask=0;frontier.complete=true;
    frontier.refused_word=frontier.refused_action=-1;
    const auto owner=world.owner;std::array<unsigned,joint_world::actor_capacity> order{},next_order{};unsigned count=0;
    for(unsigned actor=0;actor<world.actor_count;++actor)if(world.actors[actor].team!=world.actors[owner].team){
        order[count++]=actor;if(envelope)frontier.envelope_actor_mask|=std::uint16_t(1u<<actor);}
    // Current committed priorities are used where available. Unknown ranks
    // remain explicitly uncertain; no observed next priority is called fixed.
    std::sort(order.begin(),order.begin()+count,[&](unsigned a,unsigned b){const auto& x=contact.records[a];const auto& y=contact.records[b];
        if(x.priorities&&y.priorities&&x.current_priority!=y.current_priority)return x.current_priority>y.current_priority;
        return x.handle<y.handle;});
    next_order=order;std::sort(next_order.begin(),next_order.begin()+count,[&](unsigned a,unsigned b){const auto& x=contact.records[a];const auto& y=contact.records[b];
        if(x.priorities&&y.priorities&&x.next_priority!=y.next_priority)return x.next_priority>y.next_priority;
        return x.handle<y.handle;});
    auto hostile=[&](bool current,unsigned interval,bool after){for(unsigned n=0;n<count;++n){const auto actor=current?order[n]:next_order[n];const auto& record=contact.records[actor];
        if(current&&record.fresh&&record.completed)continue;
        if(!current){const bool before=interval==0?(late?record.next_opportunity_min!=0:record.next_opportunity_max!=0):!late;
            if(before==after)continue;}
        if(!joint_contact_event(frontier,{joint_world::EventKind::champion_input,actor,contact.reply_index[actor][family]}))return false;}
        return true;};
    if(!hostile(true,0,false))return false;
    for(unsigned interval=0;interval<contact.request.horizon;++interval){
        if(!joint_contact_event(frontier,{joint_world::EventKind::camp_batch,0,joint_world::no_word}))return false;
        if(!hostile(false,interval,false))return false;
        if(!joint_contact_event(frontier,{joint_world::EventKind::champion_input,owner,joint_world::no_word}))return false;
        if(interval+1<contact.request.horizon&&!hostile(false,interval,true))return false;
    }
    return true;
}
bool joint_contact_frontiers(){
    auto& contact=joint_contact_state();auto& world=contact.world;
    if(!joint_contact_funded(contact.budget.enumeration_bound)){contact.status=JointContactStatus::partial;return false;}
    const auto owner=world.owner;
    // Each still-pending body may change next priority. A rank interval counts
    // only peers whose completed-input priority is committed as definitely
    // ahead/behind; it does not mistake priority() for current rank.
    for(unsigned actor=0;actor<world.actor_count;++actor){auto& record=contact.records[actor];
        unsigned low=0,high=0;
        const bool fixed=actor==owner||(record.fresh&&record.completed&&record.priorities);
        for(unsigned peer=0;peer<world.actor_count;++peer)if(peer!=actor&&world.actors[peer].team==world.actors[actor].team){
            const auto& other=contact.records[peer];
            if(fixed&&other.fresh&&other.completed&&other.priorities){const bool ahead=other.next_priority>record.next_priority||
                (other.next_priority==record.next_priority&&other.handle<record.handle);low+=ahead;high+=ahead;}
            else ++high;}
        record.next_rank_min=low;record.next_rank_max=high;
        record.current_opportunity_min=record.fresh?unsigned(!record.completed):0;
        record.current_opportunity_max=record.fresh?unsigned(!record.completed):1;
    }
    for(unsigned actor=0;actor<world.actor_count;++actor){auto& record=contact.records[actor];const auto& own=contact.records[owner];
        const bool starts=unsigned(((epoch+1)&1)?0:1)==world.actors[actor].team;
        record.next_opportunity_max=record.next_rank_min<own.next_rank_max||(record.next_rank_min==own.next_rank_max&&starts);
        record.next_opportunity_min=record.next_rank_max<own.next_rank_min||(record.next_rank_max==own.next_rank_min&&starts);
    }
    if(!joint_contact_replies())return false;
    if(!joint_contact_frontier(0,false,0,false)||!joint_contact_frontier(1,false,1,false)||
       !joint_contact_frontier(2,false,0,true)||!joint_contact_frontier(3,true,0,true))return false;
    world.frontier_count=4;
    // Both envelope timings are supplied, rather than treating the current
    // sampled priority as immutable. Intermediate schedules/orderings and
    // other actors' policies remain approximation, not a universal bound.
    world.schedule_complete=false;contact.unknown_schedule=true;return true;
}
bool joint_contact_free(vec2 point,bool require_seen)const{
    const auto& contact=joint_contact_state();if(static_blocked(point)||(require_seen&&!sees(point)))return false;
    if(!contact.occupied||contact.occupied->is_set(point))return false;
    for(unsigned n=0;n<contact.world.actor_count;++n){const auto& actor=contact.world.actors[n];
        if(n!=contact.world.owner&&actor.alive&&actor.knowledge!=joint_world::Knowledge::retained_hidden&&actor.position==joint_point(point))return false;}
    for(unsigned n=0;n<contact.world.camp_count;++n){const auto& camp=contact.world.camps[n];
        if(camp.alive&&camp.knowledge!=joint_world::CampKnowledge::unknown&&camp.position==joint_point(point))return false;}
    return true;
}
bool joint_contact_adjacent(vec2 to)const{
    const auto& contact=joint_contact_state();const auto& owner=contact.world.actors[contact.world.owner];
    return owner.can_move&&owner.move_speed>=1&&joint_world::distance(owner.position,joint_point(to))==1&&
        joint_contact_free(to,true)&&(!contact.own_moves||contact.own_moves->is_set(to));
}
bool joint_contact_suffix(joint_world::Candidate& candidate,unsigned consumed){
    auto& contact=joint_contact_state();auto position=contact.world.actors[contact.world.owner].position;
    if(consumed)position=joint_point(contact.request.path[consumed-1]);
    for(unsigned path=consumed;path<contact.request.path_count&&candidate.word_count<joint_world::word_capacity;++path){
        const auto to=contact.request.path[path];if(!joint_contact_free(to,true))return false;
        // This first adapter supports adjacent future edges; a farther free
        // destination is not silently called a reachable future path.
        if(joint_world::distance(position,joint_point(to))>1)return false;
        auto& word=candidate.words[candidate.word_count++];word.count=0;
        auto action=joint_contact_action(joint_world::ActionKind::move,joint_world::Target::ground,0,joint_point(to));
        action.target_observed=sees(to);action.landing_observed_free=sees(to);joint_contact_append(word,action);position=joint_point(to);
    }
    return true;
}
joint_world::Candidate* joint_contact_offer(joint_world::Purpose purpose,unsigned service=0){
    auto& contact=joint_contact_state();if(contact.candidate_count==joint_world::candidate_capacity){++contact.unsearched_classes;return nullptr;}
    auto& candidate=contact.candidates[contact.candidate_count];candidate.frame=contact.world.frame;
    candidate.observation=contact.world.observation;candidate.effect=contact.world.effect;candidate.purpose=purpose;
    candidate.useful_service=service;candidate.word_count=1;candidate.words[0].count=0;return &candidate;
}
static bool joint_contact_same(const joint_world::Candidate& a,const joint_world::Candidate& b){
    if(a.purpose!=b.purpose||a.useful_service!=b.useful_service||a.word_count!=b.word_count)return false;
    for(unsigned w=0;w<a.word_count;++w){if(a.words[w].count!=b.words[w].count)return false;
        for(unsigned n=0;n<a.words[w].count;++n){const auto& x=a.words[w].actions[n];const auto& y=b.words[w].actions[n];
            if(x.kind!=y.kind||x.target!=y.target||x.index!=y.index||x.slot!=y.slot||x.point!=y.point||
               x.target_observed!=y.target_observed||x.path_legal!=y.path_legal||x.landing_observed_free!=y.landing_observed_free||
               x.only_after_refusal!=y.only_after_refusal||x.follow_target!=y.follow_target||x.duration!=y.duration||
               x.observation_intent!=y.observation_intent||x.cursor_before!=y.cursor_before||x.cursor_after!=y.cursor_after||
               x.cursor_total!=y.cursor_total||x.relevant_observation!=y.relevant_observation)return false;
        }}
    return true;
}
void joint_contact_commit(){
    auto& contact=joint_contact_state();const auto& fresh=contact.candidates[contact.candidate_count];
    for(unsigned n=0;n<contact.candidate_count;++n)if(joint_contact_same(fresh,contact.candidates[n]))return;
    ++contact.candidate_count;
}
bool joint_contact_append_move(joint_world::Candidate& candidate,bool preserves_m){
    auto& contact=joint_contact_state();const auto initial_words=candidate.word_count;
    auto suffix=[&](unsigned consumed){if(joint_contact_suffix(candidate,consumed))return true;
        candidate.word_count=initial_words;return candidate.words[0].count>0;};
    if(!preserves_m||!contact.request.path_count)return suffix(0);
    const auto to=contact.request.path[0];
    if(!joint_contact_adjacent(to))
        return suffix(0);
    auto move=joint_contact_action(joint_world::ActionKind::move,joint_world::Target::ground,0,joint_point(to));
    move.target_observed=sees(to);move.landing_observed_free=sees(to);joint_contact_append(candidate.words[0],move);
    return suffix(1);
}
bool joint_contact_capture(){
    auto& contact=joint_contact_state();auto& capture=contact.world.capture;capture={};
    const auto group=contact.request.capture_group;if(group<0||std::size_t(group)>=memory.size())return true;
    const auto& stored=memory[std::size_t(group)];const auto count=stored.end-stored.begin;
    if(!count||count>joint_world::region_capacity){++contact.unsearched_classes;return true;}
    bool all_dead=true,all_represented=true,group_fresh=false,reward=false;unsigned camp_mask=0;
    for(unsigned g=0;g<contact.group_count;++g)if(contact.group_ids[g]==group&&contact.group_known[g])
        {group_fresh=contact.group_states[g]==1||contact.group_states[g]==2;reward=contact.group_states[g]==2;}
    for(std::size_t index=stored.begin;index<stored.end;++index){bool represented=false;
        for(unsigned n=0;n<contact.world.camp_count;++n)if(contact.camp_members[n]==index){represented=true;
            camp_mask|=1u<<n;all_dead=all_dead&&!contact.world.camps[n].alive&&contact.world.camps[n].health_known;}
        all_dead=all_dead&&represented;all_represented=all_represented&&represented;
    }
    capture.enabled=group_fresh;capture.group_clear=reward&&all_dead;capture.camp_mask=std::uint16_t(camp_mask);
    capture.duration=constants::capture_time;capture.ticks=-1;
    // The native capture timer is not exposed by this SDK. Starting from its
    // full duration is a model estimate; native receipt remains completion.
    for(std::size_t index=stored.begin;index<stored.end;++index)capture.region[capture.region_count++]=joint_point(members[index].pos);
    capture.group_complete=group_fresh&&all_represented;capture.gems=groups[std::size_t(group)].gem_value();return true;
}
bool joint_contact_menu(unsigned family){
    if constexpr(cfg::local_observation)if(family==0)
        return joint_observation_plain_basic(joint_contact_state().request.path_count!=0);
    auto& contact=joint_contact_state();auto& world=contact.world;const auto owner_index=world.owner;const auto& owner=world.actors[owner_index];
    if(family==6&&contact.request.path_count&&owner.can_move){if(auto* candidate=joint_contact_offer(contact.request.path_purpose,1)){
        if(joint_contact_append_move(*candidate,true)&&candidate->words[0].count){
            if(candidate->purpose==joint_world::Purpose::bank&&candidate->word_count==contact.request.path_count&&
               contact.request.altar_known&&contact.request.altar_unlocked&&owner.gems){
                const auto endpoint=contact.request.path[contact.request.path_count-1];
                const double ticks=std::ceil(double(owner.gems)*constants::submit_gem_duration_multiplier)+constants::submit_gem_base_duration;
                if(dist(endpoint,contact.request.altar)<=constants::submit_gems_range&&std::isfinite(ticks)&&ticks<=8){
                    auto bank=joint_contact_action(joint_world::ActionKind::bank,joint_world::Target::ground,0,joint_point(contact.request.altar));
                    bank.duration=std::max(constants::submit_gem_minimum_duration,int(std::max(0.0,ticks)));
                    auto& last=candidate->words[candidate->word_count-1];
                    if(last.count<joint_world::successful_capacity)joint_contact_append(last,bank);
                    else if(candidate->word_count<joint_world::word_capacity){auto& next=candidate->words[candidate->word_count++];next.count=0;joint_contact_append(next,bank);}
                }
            }
            joint_contact_commit();}}}
    const auto origin=joint_vec(owner.position);
    const double bank_ticks=std::ceil(double(owner.gems)*constants::submit_gem_duration_multiplier)+constants::submit_gem_base_duration;
    if(family==3&&owner.gems&&contact.request.altar_known&&contact.request.altar_unlocked&&
       dist(origin,contact.request.altar)<=constants::submit_gems_range&&std::isfinite(bank_ticks)&&bank_ticks<=8){
        if(auto* candidate=joint_contact_offer(joint_world::Purpose::bank,owner.gems)){
            auto bank=joint_contact_action(joint_world::ActionKind::bank,joint_world::Target::ground,0,joint_point(contact.request.altar));
            bank.duration=std::max(constants::submit_gem_minimum_duration,int(std::max(0.0,bank_ticks)));
            joint_contact_append(candidate->words[0],bank);joint_contact_commit();}}
    if(family==4&&world.capture.enabled&&world.capture.group_clear){bool present=false;
        for(unsigned n=0;n<world.capture.region_count;++n)present=present||owner.position==world.capture.region[n];
        if(present)if(auto* candidate=joint_contact_offer(joint_world::Purpose::capture,world.capture.gems)){
            joint_contact_append(candidate->words[0],joint_contact_action(joint_world::ActionKind::wait,joint_world::Target::self,owner_index));joint_contact_commit();}}
    if(family==5&&contact.request.spawn_known&&owner.alive&&owner.channel.kind==joint_world::ChannelKind::none){
        if(auto* candidate=joint_contact_offer(joint_world::Purpose::recall,owner.gems?1:0)){
            auto recall=joint_contact_action(joint_world::ActionKind::recall,joint_world::Target::self,owner_index,joint_point(contact.request.spawn));
            recall.duration=constants::recall_time;joint_contact_append(candidate->words[0],recall);joint_contact_commit();}}
    int reset_slot=-1;
    for(unsigned slot=0;slot<2;++slot)if(owner.can_cast&&owner.abilities[slot].enabled&&owner.abilities[slot].cooldown==0&&
        owner.abilities[slot].kind==joint_world::AbilityKind::reset)reset_slot=int(slot);
    if(family==0){
    unsigned target=joint_world::no_word;auto target_kind=joint_world::Target::self;double target_value=-1;
    auto consider=[&](unsigned index,joint_world::Target kind,joint_world::Point position,double health,unsigned gems){
        const auto damage=owner.basic_damage*(kind==joint_world::Target::camp?owner.camp_damage_multiplier:1);
        const int distance=joint_world::distance(owner.position,position);
        if(distance>owner.attack_range)return;
        const double value=std::min(std::max(0.0,health),damage)+gems*2.0;
        if(value>target_value){target=index;target_kind=kind;target_value=value;}};
    for(unsigned index=0;index<world.actor_count;++index){const auto& actor=world.actors[index];
        if(actor.team!=owner.team&&actor.alive&&actor.knowledge==joint_world::Knowledge::fresh_champion)
            consider(index,joint_world::Target::actor,actor.position,actor.health+actor.shield.amount,actor.gems);}
    for(unsigned index=0;index<world.camp_count;++index){const auto& camp=world.camps[index];
        if(camp.alive&&camp.health_known&&camp.knowledge!=joint_world::CampKnowledge::unknown)
            consider(index,joint_world::Target::camp,camp.position,camp.health+camp.shield.amount,0);}
    if((owner.can_attack||reset_slot>=0)&&target!=joint_world::no_word){
        if(auto* candidate=joint_contact_offer(target_kind==joint_world::Target::camp?joint_world::Purpose::work:joint_world::Purpose::counteraction,1)){
            const auto basic=joint_contact_action(joint_world::ActionKind::basic,target_kind,target);
            if(!owner.can_attack){auto reset=joint_contact_action(joint_world::ActionKind::cast,joint_world::Target::self,owner_index);
                reset.slot=unsigned(reset_slot);joint_contact_append(candidate->words[0],reset);}
            joint_contact_append(candidate->words[0],basic);
            if(owner.can_attack&&reset_slot>=0){
                auto cast=joint_contact_action(joint_world::ActionKind::cast,joint_world::Target::self,owner_index);cast.slot=unsigned(reset_slot);
                joint_contact_append(candidate->words[0],cast);joint_contact_append(candidate->words[0],basic);}
            if(joint_contact_append_move(*candidate,!owner.ranged))joint_contact_commit();}}
    }
    // Ordinary ranged movement spends A but leaves C. Nominate its actual
    // Move -> Reset -> Basic continuation, rather than declaring the move a
    // whole input merely because the initial target was outside basic range.
    if(family==7&&owner.ranged&&owner.can_move&&reset_slot>=0&&contact.request.path_count&&
       joint_contact_adjacent(contact.request.path[0])){
        unsigned victim=joint_world::no_word;auto kind=joint_world::Target::self;double best=-1;
        const auto landing=joint_point(contact.request.path[0]);
        auto nominate=[&](unsigned index,joint_world::Target category,joint_world::Point position,double health){
            if(joint_world::distance(landing,position)>owner.attack_range)return;
            const double value=std::min(health,owner.basic_damage*(category==joint_world::Target::camp?owner.camp_damage_multiplier:1));
            if(value>best){best=value;victim=index;kind=category;}};
        for(unsigned index=0;index<world.actor_count;++index){const auto& actor=world.actors[index];
            if(actor.team!=owner.team&&actor.alive&&actor.knowledge==joint_world::Knowledge::fresh_champion)
                nominate(index,joint_world::Target::actor,actor.position,actor.health+actor.shield.amount);}
        for(unsigned index=0;index<world.camp_count;++index){const auto& camp=world.camps[index];
            if(camp.alive&&camp.health_known&&camp.knowledge!=joint_world::CampKnowledge::unknown)
                nominate(index,joint_world::Target::camp,camp.position,camp.health+camp.shield.amount);}
        if(victim!=joint_world::no_word)if(auto* candidate=joint_contact_offer(kind==joint_world::Target::camp?joint_world::Purpose::work:joint_world::Purpose::counteraction,1)){
            joint_contact_append(candidate->words[0],joint_contact_action(joint_world::ActionKind::move,joint_world::Target::ground,0,landing));
            auto reset=joint_contact_action(joint_world::ActionKind::cast,joint_world::Target::self,owner_index);reset.slot=unsigned(reset_slot);
            joint_contact_append(candidate->words[0],reset);joint_contact_append(candidate->words[0],joint_contact_action(joint_world::ActionKind::basic,kind,victim));
            const auto words=candidate->word_count;if(!joint_contact_suffix(*candidate,1))candidate->word_count=words;joint_contact_commit();}
    }
    // Offer both slots independently; they compete with complete transport,
    // capture and bank words, not with a precondition requiring an escape route.
    for(unsigned slot=0;slot<2;++slot){const auto& ability=owner.abilities[slot];
        if(family!=1+slot&&family!=8+slot)continue;
        const bool escape_family=family==8+slot;
        if(escape_family&&ability.kind!=joint_world::AbilityKind::dash)continue;
        if(!owner.can_cast||!ability.enabled||ability.cooldown||ability.kind==joint_world::AbilityKind::reset)continue;
        if(!escape_family&&ability.kind==joint_world::AbilityKind::shield){
            if(!(ability.target_mask&joint_world::target_self))continue;
            if(auto* candidate=joint_contact_offer(joint_world::Purpose::flee,1)){
                auto cast=joint_contact_action(joint_world::ActionKind::cast,joint_world::Target::self,owner_index);cast.slot=slot;
                joint_contact_append(candidate->words[0],cast);
                if(joint_contact_append_move(*candidate,!ability.exclusive_with_walk))joint_contact_commit();}
            continue;
        }
        if(escape_family&&ability.kind==joint_world::AbilityKind::dash&&contact.own_dashes[slot]){
            vec2 best_point{};double best_value=-std::numeric_limits<double>::infinity();bool found=false;
            const int radius=std::min(ability.range,3);if(radius<ability.range)++contact.unsearched_classes;
            for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x){const vec2 point{origin.x+x,origin.y+y};
                if((x==0&&y==0)||!contact.own_dashes[slot]->is_set(point)||!joint_contact_free(point,true))continue;
                int nearest=int(walls->width()+walls->height());
                for(unsigned enemy=0;enemy<world.actor_count;++enemy)if(world.actors[enemy].team!=owner.team&&world.actors[enemy].alive)
                    nearest=std::min(nearest,std::max(0,joint_world::distance(joint_point(point),world.actors[enemy].belief_center)-world.actors[enemy].belief_radius));
                double potential=0;for(unsigned source=0;source<world.camp_count;++source){const auto& camp=world.camps[source];
                    if(camp.alive&&joint_world::distance(joint_point(point),camp.position)<=camp.range&&
                       (camp.knowledge!=joint_world::CampKnowledge::fresh_asleep||!world.no_exogenous_wake))potential+=camp.damage;}
                if(world.aggregate.present)potential+=world.aggregate.damage;
                const double progress=contact.request.path_count?-dist(point,contact.request.path[contact.request.path_count-1]):0;
                const double value=nearest*4.0-potential+progress;
                if(value>best_value){best_value=value;best_point=point;found=true;}}
            if(found)if(auto* candidate=joint_contact_offer(joint_world::Purpose::flee,1)){
                auto dash=joint_contact_action(joint_world::ActionKind::cast,joint_world::Target::ground,0,joint_point(best_point));dash.slot=slot;
                joint_contact_append(candidate->words[0],dash);
                for(unsigned n=0;n<contact.request.path_count;++n)if(contact.request.path[n]==best_point){joint_contact_suffix(*candidate,n+1);break;}
                joint_contact_commit();}
        }
        if(escape_family)continue;
        unsigned victim=joint_world::no_word;auto kind=joint_world::Target::self;joint_world::Point center{};double best=-1;
        auto nominate=[&](unsigned index,joint_world::Target category,joint_world::Point position,double health){
            const auto bit=category==joint_world::Target::camp?joint_world::target_camp:joint_world::target_enemy;
            if(!(ability.target_mask&bit)&&!ability.ground)return;
            if(joint_world::distance(owner.position,position)>ability.range+ability.radius)return;
            auto aim=position;
            if(ability.ground&&joint_world::distance(owner.position,aim)>ability.range){
                const int dx=aim.x-owner.position.x,dy=aim.y-owner.position.y,d=std::max(std::abs(dx),std::abs(dy));
                aim={owner.position.x+dx*ability.range/d,owner.position.y+dy*ability.range/d};}
            if(ability.kind==joint_world::AbilityKind::dash){
                // A dash cannot land on its victim. Enumerate fresh legal
                // neighboring centers, preserving the landing AoE footprint.
                bool found=false;for(int y=-ability.radius;y<=ability.radius&&!found;++y)for(int x=-ability.radius;x<=ability.radius;++x){
                    const vec2 p{position.x+x,position.y+y};
                    if(joint_point(p)==owner.position||!contact.own_dashes[slot]||!contact.own_dashes[slot]->is_set(p)||!joint_contact_free(p,true))continue;
                    aim=joint_point(p);found=true;break;}if(!found)return;
            }else if(ability.ground&&!sees(joint_vec(aim)))return;
            const double value=std::min(health,ability.damage)+ability.stun_ticks*owner.basic_damage;
            if(value>best){best=value;victim=index;kind=category;center=aim;}};
        for(unsigned index=0;index<world.actor_count;++index){const auto& actor=world.actors[index];
            if(actor.team!=owner.team&&actor.alive&&actor.knowledge==joint_world::Knowledge::fresh_champion)
                nominate(index,joint_world::Target::actor,actor.position,actor.health+actor.shield.amount);}
        for(unsigned index=0;index<world.camp_count;++index){const auto& camp=world.camps[index];
            if(camp.alive&&camp.health_known&&camp.knowledge!=joint_world::CampKnowledge::unknown)
                nominate(index,joint_world::Target::camp,camp.position,camp.health+camp.shield.amount);}
        if(victim==joint_world::no_word)continue;
        if(auto* candidate=joint_contact_offer(kind==joint_world::Target::camp?joint_world::Purpose::work:joint_world::Purpose::counteraction,1)){
            auto cast=joint_contact_action(joint_world::ActionKind::cast,ability.ground?joint_world::Target::ground:kind,victim,center);cast.slot=slot;
            joint_contact_append(candidate->words[0],cast);
            // A damaging dash has changed origin. Its native executor must
            // reprice from actual landing before a further physical primitive.
            const bool dash=ability.kind==joint_world::AbilityKind::dash;
            if(dash||joint_contact_append_move(*candidate,!ability.exclusive_with_walk))joint_contact_commit();}}
    if(family==10&&contact.request.ward_relevant&&contact.request.observation_intent&&contact.request.observation_cursor<contact.request.observation_total&&
       dist(origin,contact.request.ward_point)<=int(constants::ward_placement_radius)&&joint_contact_free(contact.request.ward_point,false)){
        // Requesting observation is only a nomination. The caller must possess
        // a fresh charge/restriction receipt and actually place/query the ward;
        // this finite cursor is never a lease to claim unchanged inputs.
        if(auto* candidate=joint_contact_offer(joint_world::Purpose::flee,0)){
            auto observation=joint_contact_action(joint_world::ActionKind::observe,joint_world::Target::ground,0,joint_point(contact.request.ward_point));
            observation.observation_intent=contact.request.observation_intent;observation.cursor_before=contact.request.observation_cursor;
            observation.cursor_after=contact.request.observation_cursor+1;observation.cursor_total=contact.request.observation_total;
            observation.relevant_observation=true;joint_contact_append(candidate->words[0],observation);joint_contact_commit();}}
    return true;
}
bool joint_contact_price_pending(){
    auto& contact=joint_contact_state();joint_contact_phase(JointContactState::Phase::evaluation);
    while(contact.evaluated_count<contact.candidate_count){
        if(contact.owner_epoch!=epoch||int(round_number())!=epoch){contact.status=JointContactStatus::stale;return false;}
        const unsigned index=contact.evaluated_count;
        const auto candidate_tail=joint_contact_candidate_tail(contact.candidates[index].words[0].count);
        auto pricing=contact.funding;pricing.command=std::max(contact.funding.incumbent?contact.funding.command:0,candidate_tail);
        if(!pricing.price(fuel_remaining())){contact.status=JointContactStatus::partial;return false;}
        const auto before=fuel_remaining();const auto quote=joint_world::evaluate(contact.world,contact.candidates[index]);
        const auto after=fuel_remaining();const auto spent=before>=after?before-after:0;
        if constexpr(cfg::indexed_catalogue&&cfg::trace>=6)joint_catalogue_quote_record(quote,spent);
        ++contact.quoted;++contact.evaluated_count;
        if(spent>contact.budget.evaluation_bound||after<=pricing.command||int(round_number())!=epoch){
            ++contact.costs[unsigned(JointContactState::Phase::evaluation)].overruns;++contact.declared_bounds_overrun;
            contact.selection={};contact.funding.incumbent=false;contact.status=JointContactStatus::overrun;return false;
        }
        if(!quote.valid||!quote.bounded||(!contact.request.allow_approximate&&quote.approximate)||
           (!contact.request.allow_fatal&&(!quote.alive||!quote.complete)))continue;
        if(contact.selection.index==std::numeric_limits<std::size_t>::max()||joint_world::prefer(quote,contact.candidates[index],
           contact.selection.quote,contact.candidates[contact.selection.index])){
            contact.selection.index=index;contact.selection.quote=quote;contact.funding.incumbent=true;contact.funding.command=candidate_tail;
        }
    }
    return true;
}
bool joint_contact_family(unsigned family){
    auto& contact=joint_contact_state();
    if(!joint_contact_price_pending())return false;
    const auto next=joint_contact_candidate_tail(joint_contact_family_words(family));
    const auto incumbent=contact.funding.incumbent?contact.funding.command:0;
    const auto old=contact.funding.command;contact.funding.command=std::max(incumbent,next);
    if(!contact.funding.family(fuel_remaining(),contact.budget.enumeration_bound)){
        contact.funding.command=old;contact.status=JointContactStatus::partial;return false;}
    joint_contact_phase(JointContactState::Phase::menu);bool complete=false;
    if(!joint_contact_compute([&]{complete=joint_contact_menu(family);})||!complete){if(contact.funding.incumbent)contact.funding.command=incumbent;return false;}
    const bool priced=joint_contact_price_pending();
    if(contact.funding.incumbent)contact.funding.command=joint_contact_candidate_tail(
        contact.candidates[contact.selection.index].words[0].count);
    return priced;
}
JointContactStatus joint_contact_finish(JointContactStatus status){
    auto& contact=joint_contact_state();const auto now=fuel_remaining();
    if(contact.phase_start){auto& cost=contact.costs[unsigned(contact.phase)];const auto spent=contact.phase_start>=now?contact.phase_start-now:0;
        cost.spent+=spent;cost.max_phase=std::max(cost.max_phase,spent);contact.phase_start=0;}
    if(contact.owner_epoch!=epoch||int(round_number())!=epoch)status=JointContactStatus::stale;
    else if(now<=contact.funding.command){++contact.declared_bounds_overrun;status=JointContactStatus::overrun;}
    if(status==JointContactStatus::ready&&(!contact.funding.executable(now)||
       contact.selection.index==std::numeric_limits<std::size_t>::max()))status=JointContactStatus::no_offers;
    contact.status=status;if(status==JointContactStatus::partial)++contact.partial;
    if(status==JointContactStatus::no_offers)++contact.no_offers;return status;
}
JointContactStatus joint_contact_prepare(const Info& me,const JointContactRequest& request,const JointContactBudget& budget){
    auto& contact=joint_contact_state();contact.owner_epoch=epoch;contact.budget=budget;contact.phase_start=0;
    contact.status=JointContactStatus::partial;contact.request=request;contact.request.path_count=0;
    contact.candidate_count=0;contact.evaluated_count=0;contact.selection={};contact.unsearched_classes=0;
    contact.funding={budget.command_tail,budget.evaluation_bound,false};
    contact.count_derived_tail=true;
    if(!JointContactFunding::valid(budget))
        return contact.status=JointContactStatus::invalid_budget;
    contact.funding.command=joint_contact_candidate_tail(joint_contact_bootstrap_words(me));
    if(!joint_contact_funded(budget.enumeration_bound))return joint_contact_finish(JointContactStatus::partial);
    if(request.path_count>request.path.size()||!request.horizon||request.horizon>8||
       request.observation_total>32||request.observation_cursor>request.observation_total)
        return joint_contact_finish(JointContactStatus::capacity);
    const auto current_flags=state(me);
    if(current_flags.is_dead()||current_flags.contains(champion_state::teleporting)||
       current_flags.contains(champion_state::recalling)||current_flags.contains(champion_state::submitting_gems))
        return joint_contact_finish(JointContactStatus::no_offers);
    auto& world=contact.world;world.frame=++contact.world_revision;world.effect=contact.effect_revision;
    world.actor_count=world.camp_count=world.reply_count=world.frontier_count=world.blocked_count=0;
    world.capture={};world.aggregate={};world.terrain_complete=false;world.effects_complete=true;
    world.sources_complete=true;world.schedule_complete=false;world.no_exogenous_wake=request.no_exogenous_wake;
    world.no_respawn=true;world.horizon_intervals=request.horizon;world.operation_quota=30000;
    world.observation_intent=request.observation_intent;world.observation_cursor=request.observation_cursor;
    world.own_spawn=joint_point(request.spawn);world.spawn_valid=request.spawn_known;world.camp_orders_known=0;
    contact.body_cursor=contact.camp_queries=contact.reply_classes=contact.unknown_power=0;
    contact.unknown_schedule=false;contact.visible_ready=false;contact.refusal_branch=false;
    // Geometry and sibling menus are optional AFTER the first origin quote.
    // Clear dated masks, rather than funding them before an executable family.
    contact.own_moves.reset();contact.move_blocked.reset();contact.dash_blocked.reset();
    for(auto& dash:contact.own_dashes)dash.reset();
    joint_contact_phase(JointContactState::Phase::roster);
    if(!joint_contact_roster())return joint_contact_finish(contact.status);
    joint_contact_phase(JointContactState::Phase::bodies);
    if(!joint_contact_bodies(me))return joint_contact_finish(contact.status);
    if(world.owner>=world.actor_count||world.actors[world.owner].id!=std::uint32_t(handle_champion_t(self())))
        return joint_contact_finish(JointContactStatus::capacity);
    joint_contact_phase(JointContactState::Phase::details);
    if(!joint_contact_details(world.owner))return joint_contact_finish(contact.status);
    contact.funding.command=joint_contact_candidate_tail(joint_contact_family_words(0));
    for(unsigned index=0;index<world.actor_count;++index){
        if(index==world.owner)continue;
        if(world.actors[index].team==world.actors[world.owner].team&&index!=world.owner)continue;
        if(!joint_contact_details(index))return joint_contact_finish(contact.status);
    }
    int code=0;
    if(!joint_contact_query([&]{auto sight=vision::visible_tiles();sight.export_linear_buffer(visible);return 0;},code))
        return joint_contact_finish(contact.status);
    contact.visible_ready=true;
    // Full fresh occupancy is separate from the bounded modeled actor/source
    // set. Omitted visible NPCs and teleport reservations still block commands.
    if(!joint_contact_query([&]{contact.occupied=vision::champion_tiles();return 0;},code)||
       !joint_contact_query([&]{*contact.occupied|=vision::camp_tiles();return 0;},code)||
       !joint_contact_query([&]{*contact.occupied|=vision::teleport_tiles();return 0;},code))
        return joint_contact_finish(contact.status);
    contact.occupied->set(point(me),false);
    if(contact.request.ward_relevant){detail::ward_resources wards{};
        if(!joint_contact_query([&]{return detail::sdk_champion_wards(std::uint32_t(handle_champion_t(self())),&wards);},code))
            return joint_contact_finish(contact.status);
        detail::check(code);contact.request.ward_relevant=wards.charges>0&&!(contact.records[world.owner].restrictions&32);
    }
    joint_contact_phase(JointContactState::Phase::camps);
    if(!joint_contact_camps())return joint_contact_finish(contact.status);
    joint_contact_phase(JointContactState::Phase::frontiers);
    if(!joint_contact_frontiers())return joint_contact_finish(contact.status);
    if(!joint_contact_funded(budget.enumeration_bound))return joint_contact_finish(JointContactStatus::partial);
    world.observation=contact.observation_revision;
    if(!joint_contact_compute([&]{joint_contact_capture();}))return joint_contact_finish(contact.status);
    for(unsigned family=0;family<6;++family){if(!joint_contact_family(family))break;
        if(contact.funding.incumbent)break;}
    if(contact.status==JointContactStatus::overrun||contact.status==JointContactStatus::stale)return joint_contact_finish(contact.status);
    ++contact.prepared;return joint_contact_finish(contact.funding.incumbent?JointContactStatus::ready:
        contact.evaluated_count?JointContactStatus::no_offers:JointContactStatus::partial);
}
JointContactStatus joint_contact_enrich(const JointContactRequest& request){
    auto& contact=joint_contact_state();
    if(contact.status!=JointContactStatus::ready||contact.owner_epoch!=epoch||int(round_number())!=epoch||
       !contact.funding.executable(fuel_remaining()))return contact.status;
    auto keep=[&](JointContactStatus detail){contact.enrichment_status=detail;++contact.unsearched_classes;
        return joint_contact_finish(JointContactStatus::ready);};
    const auto& old=contact.request;const auto& owner=contact.world.actors[contact.world.owner];
    if(request.path_count>request.path.size()||request.horizon!=old.horizon||request.capture_group!=old.capture_group||
       request.no_exogenous_wake!=old.no_exogenous_wake||request.spawn_known!=old.spawn_known||
       (request.spawn_known&&request.spawn!=old.spawn)||request.allow_approximate!=old.allow_approximate||
       request.allow_fatal!=old.allow_fatal||request.observation_intent!=old.observation_intent||
       request.observation_cursor!=old.observation_cursor)return keep(JointContactStatus::unsupported);
    // The minimal world covers the entire first movement/dash halo. Triangle
    // inequality proves no omitted NPC newly reaches an itinerary inside it.
    // A longer itinerary needs a new world; do not mutate a paid old quote.
    int halo=std::max(0,owner.move_speed);
    for(const auto& ability:owner.abilities)if(ability.enabled&&ability.kind==joint_world::AbilityKind::dash)
        halo=std::max(halo,ability.range);
    auto previous=owner.position;
    for(unsigned n=0;n<request.path_count;++n){const auto p=joint_point(request.path[n]);
        if(joint_world::distance(owner.position,p)>halo||joint_world::distance(previous,p)>1||
           !joint_contact_free(request.path[n],true))return keep(JointContactStatus::unsupported);
        previous=p;
    }
    unsigned geometry=1;
    for(unsigned slot=0;slot<2;++slot)if(owner.abilities[slot].enabled&&
        owner.abilities[slot].kind==joint_world::AbilityKind::dash&&!contact.own_dashes[slot])++geometry;
    std::uint64_t build=0;for(unsigned n=0;n<=geometry;++n)build=JointContactFunding::add(build,contact.budget.enumeration_bound);
    if(!contact.funding.family(fuel_remaining(),build))return keep(JointContactStatus::partial);
    contact.request=request;
    if(!joint_contact_compute([&]{contact.move_blocked=*walls|*contact.occupied;
        contact.dash_blocked=*dash_walls|*contact.occupied;
        contact.own_moves=contact.move_blocked->reachable_tiles(joint_vec(owner.position),std::uint16_t(std::max(0,owner.move_speed)));}))
        return contact.status==JointContactStatus::overrun?joint_contact_finish(contact.status):keep(contact.status);
    for(unsigned slot=0;slot<2;++slot){const auto& ability=owner.abilities[slot];
        if(ability.enabled&&ability.kind==joint_world::AbilityKind::dash&&!contact.own_dashes[slot])
            if(!joint_contact_compute([&]{contact.own_dashes[slot]=contact.dash_blocked->reachable_tiles(
                joint_vec(owner.position),std::uint16_t(std::max(0,ability.range)));}))
                return contact.status==JointContactStatus::overrun?joint_contact_finish(contact.status):keep(contact.status);
    }
    const std::array<unsigned,9> families{0,1,2,3,6,7,8,9,10};
    for(const auto family:families)if(!joint_contact_family(family))break;
    if(contact.status==JointContactStatus::overrun||contact.status==JointContactStatus::stale)return joint_contact_finish(contact.status);
    contact.enrichment_status=contact.status;return joint_contact_finish(JointContactStatus::ready);
}
// ONE measured build owns the caller's cheap geometry nomination AND its
// first composed family. No second enumeration charge for a paid path prefix.
// The callback may use cached visibility/occupancy/static terrain, not query
// new world facts or issue actions. Current SDK validation remains executor-owned.
template<class Nominate>
JointContactStatus joint_contact_enrich_geometry(JointContactRequest request,Nominate&& nominate,
                                                unsigned family=joint_world::no_word){
    auto& contact=joint_contact_state();
    if(contact.status!=JointContactStatus::ready||contact.owner_epoch!=epoch||int(round_number())!=epoch||
       !contact.funding.executable(fuel_remaining()))return contact.status;
    const auto original=contact.request;const auto incumbent_tail=contact.funding.command;
    if(family==joint_world::no_word){const auto& action=contact.candidates[contact.selection.index].words[0].actions[0];
        family=action.kind==joint_world::ActionKind::basic?0:
            action.kind==joint_world::ActionKind::cast?
                (contact.world.actors[contact.world.owner].abilities[action.slot].kind==joint_world::AbilityKind::reset?0:1+action.slot):6;
    }
    if(family>10){contact.enrichment_status=JointContactStatus::unsupported;return contact.status;}
    const auto& owner=contact.world.actors[contact.world.owner];unsigned words=joint_contact_family_words(family);
    if(family==0&&owner.can_move&&!owner.ranged&&!contact.request.path_count)++words;
    if((family==1||family==2)&&owner.can_move&&!owner.abilities[family-1].exclusive_with_walk&&
       owner.abilities[family-1].kind!=joint_world::AbilityKind::dash)words=2;
    contact.funding.command=std::max(incumbent_tail,joint_contact_candidate_tail(words));
    if(!contact.funding.family(fuel_remaining(),contact.budget.enumeration_bound)){
        contact.funding.command=incumbent_tail;contact.enrichment_status=JointContactStatus::partial;return contact.status;}
    const unsigned before=contact.candidate_count;bool supported=true;
    joint_contact_phase(JointContactState::Phase::menu);
    const bool built=joint_contact_compute([&]{
        nominate(request);
        supported=request.path_count<=request.path.size()&&request.horizon==original.horizon&&
            request.capture_group==original.capture_group&&request.no_exogenous_wake==original.no_exogenous_wake&&
            request.allow_approximate==original.allow_approximate&&request.allow_fatal==original.allow_fatal&&
            request.spawn_known==original.spawn_known&&(!request.spawn_known||request.spawn==original.spawn)&&
            request.observation_intent==original.observation_intent&&request.observation_cursor==original.observation_cursor&&
            request.observation_total==original.observation_total&&request.remaining_refusals==original.remaining_refusals;
        int halo=std::max(0,owner.move_speed);
        for(const auto& ability:owner.abilities)if(ability.enabled&&ability.kind==joint_world::AbilityKind::dash)halo=std::max(halo,ability.range);
        auto last=owner.position;
        for(unsigned n=0;supported&&n<request.path_count;++n){const auto point=joint_point(request.path[n]);
            supported=joint_world::distance(owner.position,point)<=halo&&joint_world::distance(last,point)==1&&
                joint_contact_free(request.path[n],true);last=point;}
        if(!supported)return;
        contact.request=request;joint_contact_menu(family);
    });
    if(!built){contact.funding.command=incumbent_tail;
        return contact.status==JointContactStatus::overrun?joint_contact_finish(contact.status):joint_contact_finish(JointContactStatus::ready);}
    if(!supported){contact.request=original;contact.funding.command=incumbent_tail;
        contact.enrichment_status=JointContactStatus::unsupported;++contact.unsearched_classes;return joint_contact_finish(JointContactStatus::ready);}
    for(unsigned n=before;n<contact.candidate_count;++n)if(contact.candidates[n].words[0].count>words){
        contact.candidate_count=before;contact.funding.command=incumbent_tail;contact.enrichment_status=JointContactStatus::unsupported;
        return joint_contact_finish(JointContactStatus::ready);}
    joint_contact_price_pending();
    if(contact.status==JointContactStatus::overrun||contact.status==JointContactStatus::stale)return joint_contact_finish(contact.status);
    contact.funding.command=joint_contact_candidate_tail(contact.candidates[contact.selection.index].words[0].count);
    contact.enrichment_status=contact.status;return joint_contact_finish(JointContactStatus::ready);
}
// The caller supplies a bound for ONE complete pure evaluation, including all
// supplied frontier fills/transition work, and retains the real command tail.
// Each candidate is evaluated once; no already paid incumbent is recharged.
joint_world::Selection joint_contact_select(){
    auto& contact=joint_contact_state();
    if(contact.status!=JointContactStatus::ready||contact.owner_epoch!=epoch||int(round_number())!=epoch||
       !contact.funding.executable(fuel_remaining()))return {};
    if(contact.selection.index!=std::numeric_limits<std::size_t>::max())++contact.selected;
    return contact.selection;
}
// Same-input post-refusal risk reprice. This is a new actual world, not a
// reuse of the failed quote. Other bodies cannot take inputs during ours;
// unsampled damage/control effects nevertheless receive no removal credit.
joint_world::Selection joint_contact_reprice_refusal(){
    auto& contact=joint_contact_state();const auto& execution=joint_execution_state();
    const auto refusal=execution.refusal;const auto fallback=execution.fallback_budget;
    const auto empty=joint_world::Selection{};const auto start=fuel_remaining();
    if(!fallback.declared||!refusal.pending||refusal.fallback_used||refusal.epoch!=epoch||
       int(round_number())!=epoch||contact.world.owner>=contact.world.actor_count||
       contact.world.horizon_intervals!=refusal.horizon)return empty;
    const auto command=joint_execution_reserve(fallback.word,1);
    const auto rebase=contact.budget.enumeration_bound,evaluation=contact.budget.evaluation_bound;
    const auto required=JointContactFunding::add(rebase,evaluation);
    const auto whole=JointContactFunding::add(command,fallback.reprice_bound);
    if(!rebase||!evaluation||required>fallback.reprice_bound||whole==std::numeric_limits<std::uint64_t>::max()||start<=whole)
        return empty;
    const auto coverage_origin=contact.world.actors[contact.world.owner].position;
    int halo=std::max(0,contact.world.actors[contact.world.owner].move_speed);
    for(const auto& ability:contact.world.actors[contact.world.owner].abilities)
        if(ability.enabled&&ability.kind==joint_world::AbilityKind::dash)halo=std::max(halo,ability.range);
    contact.owner_epoch=epoch;contact.budget.command_tail=command;contact.count_derived_tail=false;
    contact.funding={command,evaluation,false};contact.selection={};contact.candidate_count=contact.evaluated_count=0;
    contact.status=JointContactStatus::partial;contact.phase_start=0;
    contact.request.path_count=0;contact.request.horizon=refusal.horizon;
    contact.request.allow_approximate=true;contact.request.allow_fatal=fallback.allow_risk;contact.request.remaining_refusals=0;
    joint_contact_phase(JointContactState::Phase::bodies);int code=0;Info actual{};
    const auto owner=contact.world.owner;auto& record=contact.records[owner];
    if(!joint_contact_query([&]{return detail::sdk_champion(record.handle,&actual);},code))return empty;
    detail::check(code);if(joint_point(point(actual))!=refusal.origin||state(actual).is_dead())return empty;
    record.body=actual;record.fresh=true;record.ever_seen=true;record.seen=epoch;joint_contact_actor_base(owner);
    if(!joint_contact_details(owner))return empty;
    if(!contact.world.actors[owner].can_move)return empty;
    if(!joint_contact_query([&]{auto sight=vision::visible_tiles();sight.export_linear_buffer(visible);return 0;},code))return empty;
    std::optional<bitboard> live_camps;
    if(!joint_contact_query([&]{live_camps=vision::camp_tiles();return 0;},code)||
       !joint_contact_query([&]{contact.occupied=vision::champion_tiles();*contact.occupied|=*live_camps;return 0;},code)||
       !joint_contact_query([&]{*contact.occupied|=vision::teleport_tiles();return 0;},code))return empty;
    contact.occupied->set(point(actual),false);
    auto& world=contact.world;
    for(unsigned index=0;index<world.actor_count;++index)if(index!=owner){auto& actor=world.actors[index];
        // Keep every retained identity. No old predicted kill or stun can
        // delete an actual reply after a partial own prefix.
        if(actor.team!=world.actors[owner].team){
            actor.health=std::max(actor.health,actor.max_health);actor.stun_ticks=-1;
            for(auto& ability:actor.abilities)ability.cooldown=0;
            actor.alive=true;actor.knowledge=joint_world::Knowledge::retained_hidden;
            actor.frontier_complete=false;joint_contact_envelope(index);
            actor.frontier_complete=false;actor.remaining_reply.full_missing_health=true;}
    }
    for(unsigned index=0;index<world.camp_count;++index){auto& camp=world.camps[index];
        camp.health_known=false;camp.stun_ticks=-1;camp.knowledge=joint_world::CampKnowledge::unknown;
        camp.alive=!(sees(joint_vec(camp.position))&&!live_camps->is_set(joint_vec(camp.position)));
        camp.damage=std::max(camp.damage,double(members[contact.camp_members[index]].base_strength));
    }
    world.no_respawn=false;world.effects_complete=false;world.capture.enabled=false;
    world.frame=++contact.world_revision;world.effect=std::max<std::uint64_t>(contact.effect_revision,refusal.effect);
    world.observation=std::max<std::uint64_t>(contact.observation_revision,refusal.observation);
    if(world.frame<=refusal.frame||world.horizon_intervals!=refusal.horizon)return empty;
    if(!joint_contact_compute([&]{contact.move_blocked=*walls|*contact.occupied;
        contact.own_moves=contact.move_blocked->reachable_tiles(point(actual),std::uint16_t(std::max(0,actual.move_speed)));}))return empty;
    const auto before_prices=fuel_remaining();
    if(start-before_prices>rebase){++contact.declared_bounds_overrun;joint_contact_finish(JointContactStatus::overrun);return empty;}
    // Eight adjacent actual legal alternatives form the bounded nomination
    // frontier. More evaluations are optional only within the PREPAID helper.
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        if(!x&&!y)continue;const vec2 to{actual.x+x,actual.y+y};
        if(joint_point(to)==refusal.destination||joint_world::distance(coverage_origin,joint_point(to))>halo||
           !joint_contact_free(to,true)||!contact.own_moves->is_set(to))continue;
        const auto spent=start>=fuel_remaining()?start-fuel_remaining():fallback.reprice_bound;
        if(spent>=fallback.reprice_bound||fallback.reprice_bound-spent<evaluation||
           !contact.funding.price(fuel_remaining()))break;
        auto* candidate=joint_contact_offer(joint_world::Purpose::flee,0);if(!candidate)break;
        joint_contact_append(candidate->words[0],joint_contact_action(joint_world::ActionKind::move,joint_world::Target::ground,0,joint_point(to)));
        joint_contact_commit();if(!joint_contact_price_pending())break;
    }
    if(start-fuel_remaining()>fallback.reprice_bound){++contact.declared_bounds_overrun;joint_contact_finish(JointContactStatus::overrun);return empty;}
    joint_contact_finish(contact.funding.incumbent?JointContactStatus::ready:JointContactStatus::unsupported);
    return joint_contact_select();
}
// Invoke only AFTER an actual command and its native feedback/failure closure.
// This revokes every old projection; it is not a synthetic effect receipt.
void joint_contact_actual_feedback(bool effect_applied,bool refused){
    auto& contact=joint_contact_state();if(effect_applied){++contact.effect_revision;++contact.actual_effects;}
    if(refused)++contact.actual_refusals;++contact.observation_revision;++contact.actual_refreshes;
    contact.status=JointContactStatus::stale;contact.selection={};
}
#endif
