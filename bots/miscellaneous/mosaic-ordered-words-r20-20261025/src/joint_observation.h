// Included inside Runtime after joint_contact.h. A receipt changes knowledge,
// never physical history. Dynamic packets are freshly acquired on every call.
struct JointObservationCursor {
    unsigned next{},total{};std::uint64_t revision{};
    void reset(unsigned count){if(total!=count){total=count;next=0;++revision;}}
    bool commit(unsigned index,bool actual_receipt){
        if(!actual_receipt||index!=next||next>=total)return false;
        ++next;++revision;return true;
    }
    bool complete()const{return next==total;}
};
struct JointObservationFunding {
    static bool before(std::uint64_t fuel,std::uint64_t tail,std::uint64_t evaluation,std::uint64_t component){
        const auto max=std::numeric_limits<std::uint64_t>::max();
        if(!tail||!evaluation||evaluation>max-tail)return false;
        const auto retained=tail+evaluation;
        return component<=max-retained&&fuel>retained+component;
    }
    static bool after(std::uint64_t fuel,std::uint64_t tail,std::uint64_t evaluation){
        return before(fuel,tail,evaluation,0);
    }
};
#ifndef MOSAIC_JOINT_OBSERVATION_PURE_ONLY
struct JointObservationState {
    enum class Stage:unsigned {roster,own_packet,enemy_bodies,immutable,visibility,sources,target,frontier,offer,count};
    struct Cost {std::uint64_t spent{},maximum{},calls{},overruns{};};
    std::array<Cost,unsigned(Stage::count)> costs{};
    JointObservationCursor immutable;
    std::array<int,joint_world::actor_capacity> body_epoch{};
    std::array<std::uint64_t,joint_world::actor_capacity> body_frame{};
    unsigned calls{},partial{},ready{},raw_own{},raw_enemies{},raw_allies{},source_count{},source_overflow{};
    unsigned receipts{},group_receipts{},target_receipts{},unknown_identities{},stage_progress{};
    unsigned first_source_handle{},first_source_group{},target_handle{},target_life{};
    Info own_body{};std::uint8_t own_restrictions{};
    std::array<detail::buff_data,3> own_buffs{};unsigned own_buff_count{};
    detail::camp_state target_body{};std::array<detail::camp_buff_data,8> target_buffs{};unsigned target_buff_count{};
    int own_receipt_epoch{-1},target_receipt_epoch{-1};
    unsigned gems{};int gems_receipt_epoch{-1};std::uint64_t gems_effect{};
    std::uint64_t own_frame{},own_effect{},own_observation{},target_frame{},target_effect{},target_observation{};
    bool target_body_complete{},target_buffs_complete{};
    std::size_t target_member{std::numeric_limits<std::size_t>::max()};
    unsigned target_source{joint_world::no_word},facts{};int epoch{-1};
    Stage stage{Stage::roster};std::uint64_t started{};
};
static JointObservationState& joint_observation_state(){static constinit JointObservationState value{};return value;}
// The controller already queries the bag to choose its common horizon. Reuse
// that actual same-input receipt instead of adding a duplicate mandatory query.
// Call immediately after that query, before any host action.
bool joint_observation_gems_receipt(unsigned gems){
    if(int(round_number())!=epoch)return false;auto& observation=joint_observation_state();
    observation.gems=gems;observation.gems_receipt_epoch=epoch;
    observation.gems_effect=joint_contact_state().effect_revision;return true;
}
void joint_observation_stage(JointObservationState::Stage stage){
    auto& observation=joint_observation_state();const auto now=fuel_remaining();
    if(observation.started){auto& cost=observation.costs[unsigned(observation.stage)];
        const auto spent=observation.started>=now?observation.started-now:0;
        cost.spent+=spent;cost.maximum=std::max(cost.maximum,spent);++cost.calls;}
    observation.stage=stage;observation.started=now;
}
JointContactStatus joint_observation_finish(JointContactStatus status){
    auto& observation=joint_observation_state();joint_observation_stage(observation.stage);observation.started=0;
    status=joint_contact_finish(status);if(status==JointContactStatus::ready)++observation.ready;else ++observation.partial;
    return status;
}
template<class F> bool joint_observation_query(F&& query,int& code){
    auto& contact=joint_contact_state();auto& observation=joint_observation_state();
    if(!JointObservationFunding::before(fuel_remaining(),contact.funding.command,
            contact.budget.evaluation_bound,contact.budget.query_bound)){
        contact.status=JointContactStatus::partial;return false;}
    const auto before=fuel_remaining();const bool result=joint_contact_query(std::forward<F>(query),code);
    const auto after=fuel_remaining();if(!result&&before>=after&&before-after>contact.budget.query_bound)
        ++observation.costs[unsigned(observation.stage)].overruns;
    return result;
}
// Handles are stable. The roster is acquired once, while immutable identities
// and kits have a separate monotone cursor; a missing kit never deletes a foe.
bool joint_observation_roster(){
    auto& contact=joint_contact_state();int code=0;
    if(contact.roster_stage==0){std::uint32_t count=0;
        if(!joint_observation_query([&]{return detail::sdk_num_allies(&count);},code))return false;
        detail::check(code);contact.allies=count;contact.roster_stage=1;}
    if(contact.roster_stage==1){std::uint32_t count=0;
        if(!joint_observation_query([&]{return detail::sdk_num_enemies(&count);},code))return false;
        detail::check(code);contact.enemies=count;contact.roster_stage=2;}
    if(contact.allies+contact.enemies>joint_world::actor_capacity){contact.status=JointContactStatus::capacity;return false;}
    if(contact.roster_stage==2){std::uint32_t count=contact.allies;
        if(!joint_observation_query([&]{return detail::sdk_allies(contact.handles.data(),count,&count);},code))return false;
        detail::check(code);if(count!=contact.allies){contact.status=JointContactStatus::capacity;return false;}contact.roster_stage=3;}
    if(contact.roster_stage==3){std::uint32_t count=contact.enemies;
        if(!joint_observation_query([&]{return detail::sdk_enemies(contact.handles.data()+contact.allies,count,&count);},code))return false;
        detail::check(code);if(count!=contact.enemies){contact.status=JointContactStatus::capacity;return false;}
        contact.count=contact.allies+contact.enemies;contact.roster_stage=4;}
    for(unsigned n=0;n<contact.count;++n)contact.records[n].handle=contact.handles[n];
    joint_observation_state().immutable.reset(contact.count);return true;
}
bool joint_observation_design(unsigned index){
    auto& contact=joint_contact_state();auto& record=contact.records[index];int code=0;
    if(!record.identity){detail::champion_identity identity{};
        if(!joint_observation_query([&]{return detail::sdk_champion_identity(record.handle,&identity);},code))return false;
        detail::check(code);record.type=identity.type;record.absolute_team=identity.team;record.identity=true;}
    if(!record.design_known){
        for(unsigned n=0;n<contact.count;++n)if(n!=index&&contact.records[n].identity&&
            contact.records[n].design_known&&contact.records[n].type==record.type){
            record.design=contact.records[n].design;record.design_known=true;return true;}
        if(!joint_observation_query([&]{return detail::sdk_champion_design(record.type,&record.design);},code))return false;
        detail::check(code);record.design_known=true;}
    return true;
}
void joint_observation_actor(unsigned index){
    auto& contact=joint_contact_state();auto& record=contact.records[index];auto& actor=contact.world.actors[index];
    if(record.design_known)joint_contact_actor_base(index);
    else{
        actor={};actor.id=record.handle;actor.team=unsigned(record.absolute_team);
        actor.knowledge=joint_world::Knowledge::retained_hidden;actor.health=actor.max_health=joint_world::unknown_damage;
        actor.basic_damage=joint_world::unknown_damage;actor.attack_range=int(walls->width()+walls->height());
        actor.move_speed=int(walls->width()+walls->height());actor.can_move=actor.can_attack=actor.can_cast=true;
        actor.belief_center=joint_point(record.ever_seen?point(record.body):previous);
        actor.position=actor.belief_center;actor.belief_radius=int(walls->width()+walls->height());
        if(record.fresh){actor.position=joint_point(point(record.body));actor.belief_center=actor.position;actor.belief_radius=0;
            actor.health=std::max(0.0f,record.body.health);actor.max_health=std::max(1.0f,record.body.max_health);
            actor.basic_damage=std::max(0.0f,record.body.strength);actor.attack_range=record.body.attack_range;
            actor.move_speed=record.body.move_speed;actor.ranged=actor.attack_range>1;
            actor.knowledge=joint_world::Knowledge::fresh_champion;actor.alive=state(record.body).contains(champion_state::alive);}
        // Current RAW strength/range do not describe an unknown ability kit.
        // Keep an explicit unsearched reply envelope until its catalogue
        // receipt exists; never infer "only Basic" from omitted metadata.
        actor.abilities[0].enabled=true;actor.abilities[0].kind=joint_world::AbilityKind::damage;
        actor.abilities[0].range=int(walls->width()+walls->height());
        actor.abilities[0].target_mask=joint_world::target_enemy;actor.abilities[0].damage=joint_world::unknown_damage;
        ++joint_observation_state().unknown_identities;
    }
    // An unobserved ally is never an NPC shield at its old pose. The separate
    // capture sampler may retain actual presence, without lending MAC/gear.
    if(index!=contact.world.owner&&index<contact.allies){actor.knowledge=joint_world::Knowledge::retained_hidden;
        actor.frontier_complete=false;}
    if(!record.fresh)actor.frontier_complete=false;
    contact.world.effects_complete=false;
}
bool joint_observation_own(const Info& nominal){
    auto& contact=joint_contact_state();auto& observation=joint_observation_state();int code=0;
    const auto handle=std::uint32_t(handle_champion_t(self()));unsigned owner=contact.count;
    for(unsigned n=0;n<contact.count;++n)if(contact.handles[n]==handle){owner=n;break;}
    if(owner==contact.count){contact.status=JointContactStatus::capacity;return false;}
    contact.world.owner=owner;auto& record=contact.records[owner];Info current{};
    // nominal is only an entry-class hint. Model HP and allowances come from
    // this one fresh RAW body, not champion wrapper property cascades.
    (void)nominal;
    if(!joint_observation_query([&]{return detail::sdk_champion(handle,&current);},code))return false;
    detail::check(code);++observation.raw_own;record.body=current;record.type=current.type;record.absolute_team=current.team;
    record.identity=true;record.ever_seen=record.fresh=record.alive_known=true;record.seen=epoch;
    record.priorities=false;
    record.alive=state(current).contains(champion_state::alive);record.completed=state(current).contains(champion_state::completed_turn);
    if(!joint_observation_design(owner))return false;
    joint_contact_actor_base(owner);auto& actor=contact.world.actors[owner];
    std::uint8_t restrictions=0;
    if(!joint_observation_query([&]{return detail::sdk_champion_restrictions(handle,&restrictions);},code))return false;
    detail::check(code);record.restrictions=restrictions;
    actor.can_move=actor.can_move&&!(restrictions&1);actor.can_attack=actor.can_attack&&!(restrictions&2);
    actor.can_cast=actor.can_cast&&!(restrictions&4);
    std::array<detail::buff_data,3> buffs{};std::uint32_t count=0;
    if(!joint_observation_query([&]{return detail::sdk_champion_buff_data(handle,buffs.data(),buffs.size(),&count);},code))return false;
    detail::check(code);if(count>buffs.size()){contact.status=JointContactStatus::capacity;return false;}
    observation.own_body=current;observation.own_restrictions=restrictions;observation.own_buff_count=count;
    for(unsigned n=0;n<count;++n)observation.own_buffs[n]=buffs[n];
    observation.own_receipt_epoch=epoch;observation.own_frame=contact.world.frame;
    observation.own_effect=contact.effect_revision;observation.own_observation=contact.observation_revision;
    for(unsigned n=0;n<count;++n){const auto& buff=buffs[n];
        if(buff.type>=unsigned(detail::buff_kind::count)){contact.status=JointContactStatus::unsupported;return false;}
        if(buff.type==unsigned(detail::buff_kind::shield)){actor.shield.amount+=std::max(0.0f,buff.data.shield.value);
            actor.shield.ticks=actor.shield.ticks<0?buff.data.shield.duration:std::min(actor.shield.ticks,buff.data.shield.duration);}
        else if(buff.type==unsigned(detail::buff_kind::stun))actor.stun_ticks=std::max(actor.stun_ticks,buff.data.stun.duration);
        else if(buff.type==unsigned(detail::buff_kind::enhanced_attack)){actor.enhanced_attack_ready=true;
            for(const auto& ability:actor.abilities)actor.enhanced_stun_ticks=std::max(actor.enhanced_stun_ticks,ability.enhance_next_stun);}}
    // Raw body has no cooldowns. Unknown is not CD0; first plain Basic does not
    // borrow an unqueried cast/reset. A later detailed family must acquire CDs.
    for(auto& ability:actor.abilities)ability.enabled=false;
    if(observation.gems_receipt_epoch==epoch&&observation.gems_effect==contact.effect_revision)actor.gems=observation.gems;
    const auto flags=state(current);
    if(flags.contains(champion_state::recalling)||flags.contains(champion_state::submitting_gems)){
        if(!joint_observation_query([&]{return detail::sdk_champion_channel_time(handle,&actor.channel.ticks);},code))return false;
        detail::check(code);actor.channel.kind=flags.contains(champion_state::recalling)?joint_world::ChannelKind::recall:joint_world::ChannelKind::bank;
        actor.channel.destination=joint_point(contact.request.spawn);}
    record.details=true;record.last=actor;observation.body_epoch[owner]=epoch;observation.body_frame[owner]=contact.world.frame;
    observation.facts|=1u;return true;
}
bool joint_observation_bodies(){
    auto& contact=joint_contact_state();auto& observation=joint_observation_state();int code=0;
    const auto own_team=contact.world.actors[contact.world.owner].team;
    for(unsigned n=0;n<contact.count;++n){if(n==contact.world.owner)continue;auto& record=contact.records[n];
        record.fresh=record.details=record.priorities=false;record.damage_queried[0]=record.damage_queried[1]=false;
        if(!record.identity)record.absolute_team=int(n<contact.allies?own_team:1u-own_team);
        // Allies are queried only for a current capture/share question. They
        // remain hidden in CAMP targeting, so missing allies cannot protect us.
        const int capture_group=contact.request.capture_group;
        const bool capture=capture_group>=0&&std::size_t(capture_group)<memory.size()&&
            memory[std::size_t(capture_group)].state==2;
        if(n>=contact.allies||capture){Info current{};
            if(!joint_observation_query([&]{return detail::sdk_champion(record.handle,&current);},code))return false;
            if(n>=contact.allies)++observation.raw_enemies;else ++observation.raw_allies;
            if(!joint_contact_hidden(code))detail::check(code);
            const bool fresh=!joint_contact_hidden(code)&&current.x>=0&&current.y>=0&&current.x<walls->width()&&current.y<walls->height();
            if(fresh){record.body=current;record.type=current.type;record.absolute_team=current.team;
                record.identity=record.ever_seen=record.fresh=record.alive_known=true;record.seen=epoch;
                record.completed=state(current).contains(champion_state::completed_turn);
                record.alive=state(current).contains(champion_state::alive);}
            else record.alive_known=false;
            observation.body_epoch[n]=epoch;observation.body_frame[n]=contact.world.frame;
        }
        joint_observation_actor(n);
        if(!record.fresh){auto& actor=contact.world.actors[n];actor.alive=true;
            if(actor.health<=0)actor.health=actor.max_health;
        }
        // A current death is retained in Record, but its unknown respawn timer
        // cannot remove the identity's whole-horizon potential reply.
        auto& actor=contact.world.actors[n];if(!actor.alive){actor.alive=true;actor.health=actor.max_health;
            actor.knowledge=joint_world::Knowledge::retained_hidden;actor.frontier_complete=false;}
        ++contact.body_cursor;
    }
    return true;
}
// One real immutable acquisition per input. Already paid receipts can advance
// the cursor too; changing epoch alone cannot invent an acquired identity/kit.
void joint_observation_catalog(){
    auto& contact=joint_contact_state();auto& cursor=joint_observation_state().immutable;
    while(!cursor.complete()){
        const unsigned index=cursor.next;auto& record=contact.records[index];
        if(record.identity&&record.design_known){cursor.commit(index,true);continue;}
        if(!joint_observation_design(index))return;
        cursor.commit(index,true);++joint_observation_state().stage_progress;
        // Commit immutable cache only. A retained quote's World is immutable;
        // the newly learned kit is applied on the next fresh input.
        return;
    }
    contact.roster_ready=true;
}
// NO-query handoff for actual executor feedback. Call only immediately after
// a successful native life query with the current personal-visibility premise.
// A missing live payload publishes LIFE only; it does not certify current HP
// or shield. Neither writer mutates a previously priced World or physical trace.
bool joint_observation_commit_member(std::size_t index,unsigned flags,
    const detail::camp_state* live=nullptr,const detail::camp_buff_data* buffs=nullptr,
    unsigned count=0,bool buffs_complete=false){
    auto& contact=joint_contact_state();auto& observation=joint_observation_state();
    if(index>=members.size()||flags>2||count>8||(count&&!buffs)||contact.owner_epoch!=epoch||
       int(round_number())!=epoch||!sees(members[index].pos))return false;
    auto& member=members[index];const auto& group=memory[std::size_t(member.group)];
    observation.target_member=index;observation.target_handle=std::uint32_t(handle_camp_t(member.handle));observation.target_life=flags;
    observation.target_receipt_epoch=epoch;observation.target_frame=contact.world.frame;
    observation.target_effect=contact.effect_revision;observation.target_observation=contact.observation_revision;
    observation.target_body_complete=flags&&live;observation.target_buffs_complete=flags&&buffs_complete;
    if(live)observation.target_body=*live;observation.target_buff_count=buffs_complete?count:0;
    if(buffs_complete)for(unsigned n=0;n<count;++n)observation.target_buffs[n]=buffs[n];
    member.seen=epoch;member.alive=flags!=0;member.awake=flags==1;
    if(flags){
        if(live){member.hp=live->health;member.strength=live->strength;}
        if(buffs_complete){member.shield=0;for(unsigned n=0;n<count;++n)
            if(buffs[n].type==unsigned(detail::buff_kind::shield))member.shield+=std::max(0.0f,buffs[n].magnitude);}
        member.work.observe_alive(epoch);if(support_detail.ready())support_detail.restore(std::size_t(member.group),epoch);
    }else{
        member.hp=member.shield=0;int after=epoch+1;
        if(group.seen==epoch){if(group.state==1||group.state==2)after=epoch+std::max(1,groups[std::size_t(member.group)].max_respawn_timer()-1);
            else if(group.state==3)after=std::max(epoch+1,group.until-1);}
        member.work.observe_dead(epoch,after);
    }
    ++observation.target_receipts;++observation.receipts;observation.facts|=4u;return true;
}
bool joint_observation_commit_group(int group,unsigned status,int until){
    auto& contact=joint_contact_state();auto& observation=joint_observation_state();
    if(group<0||std::size_t(group)>=groups.size()||!status||status>3||contact.owner_epoch!=epoch||int(round_number())!=epoch)return false;
    auto& receipt=memory[std::size_t(group)];bool current_visible=false;
    for(std::size_t n=receipt.begin;n<receipt.end;++n)current_visible=current_visible||sees(members[n].pos);
    if(!current_visible||(status==3&&until<epoch))return false;
    if(status==1)until=0;else if(status==2)until=epoch+std::max(1,groups[std::size_t(group)].max_respawn_timer()-1);
    if(status==1&&(receipt.state==2||receipt.state==3)&&epoch>receipt.seen)
        for(std::size_t n=receipt.begin;n<receipt.end;++n)members[n].work.new_generation(epoch);
    receipt.state=int(status);receipt.seen=epoch;receipt.until=until;
    if(support_detail.ready()){
        if(status==1)support_detail.restore(std::size_t(group),epoch);
        else support_detail.absence(std::size_t(group),epoch,std::max(epoch,until-1));}
    if(status==2||status==3)for(std::size_t n=receipt.begin;n<receipt.end;++n){auto& member=members[n];member.seen=epoch;
        member.alive=member.awake=false;member.hp=member.shield=0;member.work.observe_dead(epoch,std::max(epoch+1,until-1));}
    ++observation.group_receipts;++observation.receipts;observation.facts|=2u;return true;
}
// Query and stage the complete group lifecycle before publishing its receipt.
bool joint_observation_group(int group){
    auto& contact=joint_contact_state();
    if(group<0||std::size_t(group)>=groups.size())return true;
    const auto& saved=memory[std::size_t(group)];bool visible_group=false;
    for(std::size_t n=saved.begin;n<saved.end;++n)visible_group=visible_group||sees(members[n].pos);
    if(!visible_group)return true;
    int code=0;std::uint32_t status=0;
    if(!joint_observation_query([&]{return detail::sdk_camp_group_state(
        std::uint32_t(handle_camp_group_t(groups[std::size_t(group)])),&status);},code))return false;
    if(joint_contact_hidden(code))return true;detail::check(code);if(!status)return true;
    if(status>3){contact.status=JointContactStatus::unsupported;return false;}
    int until=0;
    if(status==3){std::int32_t timer=0;
        if(!joint_observation_query([&]{return detail::sdk_camp_group_cooldown(
            std::uint32_t(handle_camp_group_t(groups[std::size_t(group)])),&timer);},code))return false;
        detail::check(code);until=epoch+std::max(0,timer);
    }else if(status==2)until=epoch+std::max(1,groups[std::size_t(group)].max_respawn_timer()-1);
    if(!joint_observation_commit_group(group,status,until)){contact.status=JointContactStatus::stale;return false;}
    const auto g=joint_contact_group(group);if(g<joint_world::camp_capacity){contact.group_known[g]=true;contact.group_states[g]=status;}
    if(status==2||status==3){
        for(unsigned n=0;n<contact.world.camp_count;++n)if(contact.world.camps[n].group==unsigned(group)){
            auto& camp=contact.world.camps[n];camp.alive=false;camp.health=0;camp.health_known=true;}
        if(status==3&&until<=epoch+int(contact.request.horizon))contact.world.no_respawn=false;
    }
    return true;
}
bool joint_observation_sources(){
    if constexpr(cfg::indexed_catalogue)if(joint_catalogue_usable())return joint_indexed_sources();
    auto& contact=joint_contact_state();auto& observation=joint_observation_state();auto& world=contact.world;
    world.camp_count=0;world.aggregate={};contact.group_count=0;contact.omitted_enemy_support=false;
    observation.target_member=std::numeric_limits<std::size_t>::max();observation.target_source=joint_world::no_word;
    const auto& owner=world.actors[world.owner];const auto origin=joint_vec(owner.position);const int halo=std::max(0,owner.move_speed);
    // Static source rows come from Runtime's completed finite map catalogue.
    // Preserve selected group rows first; every omitted in-halo source goes to
    // an always-potential aggregate with no proposed kill/sleep/control credit.
    for(unsigned pass=0;pass<2;++pass)for(std::size_t index=0;index<members.size();++index){
        if((index&15u)==0&&!joint_contact_funded(contact.budget.enumeration_bound)){contact.status=JointContactStatus::partial;return false;}
        const auto& member=members[index];const bool selected=member.group==contact.request.capture_group||member.group==job;
        if(selected!=(pass==0))continue;const bool incoming=dist(origin,member.pos)<=member.range+halo;
        if(!selected&&!incoming){contact.omitted_enemy_support=true;continue;}
        if(world.camp_count==joint_world::camp_capacity){contact.omitted_enemy_support=true;++observation.source_overflow;
            if(incoming&&member.base_strength>0){world.aggregate.present=true;
                world.aggregate.damage+=std::ceil(std::max(double(member.base_strength),member.seen==epoch?double(member.strength):0.0))+1;}continue;}
        const unsigned n=world.camp_count++;contact.camp_members[n]=index;auto& camp=world.camps[n];camp={};
        camp.id=std::uint32_t(handle_camp_t(member.handle));camp.group=unsigned(member.group);camp.position=joint_point(member.pos);
        camp.health=member.base_hp;camp.damage=std::max(0.0f,member.base_strength);camp.range=member.range;
        camp.knowledge=joint_world::CampKnowledge::unknown;camp.alive=true;camp.health_known=false;
        if(n==0){observation.first_source_handle=camp.id;observation.first_source_group=camp.group;}
        if(sees(member.pos)&&dist(origin,member.pos)<=owner.attack_range&&work_candidate(member)&&
            (observation.target_member==std::numeric_limits<std::size_t>::max()||
             (member.group==job&&members[observation.target_member].group!=job))){
            observation.target_member=index;observation.target_source=n;}
    }
    world.aggregate.covers_omitted=true;world.aggregate.center=owner.position;
    world.aggregate.reach=int(walls->width()+walls->height());observation.source_count=world.camp_count;
    return true;
}
bool joint_observation_target(){
    auto& contact=joint_contact_state();auto& observation=joint_observation_state();auto& world=contact.world;
    const auto target=observation.target_member;
    const int selected=contact.request.capture_group>=0?contact.request.capture_group:job;
    if(!joint_observation_group(selected))return false;
    if(target==std::numeric_limits<std::size_t>::max())return true;
    auto& member=members[target];auto& camp=world.camps[observation.target_source];
    if(member.group!=selected&&!joint_observation_group(member.group))return false;
    const auto& group=memory[std::size_t(member.group)];
    if(group.seen==epoch&&(group.state==2||group.state==3))return true;
    int code=0;std::uint32_t flags=0;
    if(!joint_observation_query([&]{return detail::sdk_camp_state(camp.id,&flags);},code))return false;
    if(joint_contact_hidden(code))return true;detail::check(code);
    if(flags>2){contact.status=JointContactStatus::unsupported;return false;}
    observation.target_handle=camp.id;observation.target_life=flags;
    // Read ALL required live payload before publishing its receipt. A partial
    // body/buff acquisition grants neither a refreshed target nor kill credit.
    detail::camp_state current{};std::array<detail::camp_buff_data,8> buffs{};std::uint32_t count=0;
    if(flags){
        if(!joint_observation_query([&]{return detail::sdk_camp(camp.id,&current);},code))return false;
        if(joint_contact_hidden(code))return true;detail::check(code);
        if(!joint_observation_query([&]{return detail::sdk_camp_buffs(camp.id,buffs.data(),buffs.size(),&count);},code))return false;
        if(joint_contact_hidden(code))return true;detail::check(code);
        if(count>buffs.size()){contact.status=JointContactStatus::capacity;return false;}
    }
    if(!joint_observation_commit_member(target,flags,flags?&current:nullptr,buffs.data(),count,true)){
        contact.status=JointContactStatus::stale;return false;}
    camp.alive=flags!=0;camp.health_known=true;camp.health=0;
    if(flags){
        camp.health=std::max(0.0f,current.health);camp.damage=std::max(0.0f,current.strength);
        camp.knowledge=flags==2?joint_world::CampKnowledge::fresh_asleep:joint_world::CampKnowledge::fresh_awake;
        for(unsigned n=0;n<count;++n){const auto& buff=buffs[n];
            if(buff.type==unsigned(detail::buff_kind::shield)){
                camp.shield.amount+=std::max(0.0f,buff.magnitude);
                camp.shield.ticks=camp.shield.ticks<0?buff.timer:std::min(camp.shield.ticks,buff.timer);}
            else if(buff.type==unsigned(detail::buff_kind::stun))camp.stun_ticks=std::max(camp.stun_ticks,buff.timer);}
    }else{
        if(group.seen!=epoch||group.state==1)world.no_respawn=false;
    }
    return true;
}
// The first class is literally Basic (+ optional adjacent M). It never grows
// into a reset/cast family under a two-primitive command declaration.
bool joint_observation_plain_basic(bool append_adjacent=false){
    auto& contact=joint_contact_state();auto& world=contact.world;const auto& owner=world.actors[world.owner];
    if(!owner.can_attack||owner.channel.kind!=joint_world::ChannelKind::none)return true;
    unsigned target=joint_world::no_word;auto kind=joint_world::Target::self;double value=-1;
    auto consider=[&](unsigned index,joint_world::Target type,joint_world::Point position,double health){
        if(joint_world::distance(owner.position,position)>owner.attack_range)return;
        const double damage=owner.basic_damage*(type==joint_world::Target::camp?owner.camp_damage_multiplier:1);
        const double score=std::min(health,damage);if(score>value){value=score;target=index;kind=type;}};
    for(unsigned n=0;n<world.camp_count;++n){const auto& camp=world.camps[n];
        if(camp.alive&&camp.health_known&&camp.knowledge!=joint_world::CampKnowledge::unknown)
            consider(n,joint_world::Target::camp,camp.position,camp.health+camp.shield.amount);}
    for(unsigned n=0;n<world.actor_count;++n){const auto& actor=world.actors[n];
        if(actor.team!=owner.team&&actor.alive&&actor.knowledge==joint_world::Knowledge::fresh_champion)
            consider(n,joint_world::Target::actor,actor.position,actor.health+actor.shield.amount);}
    if(target==joint_world::no_word)return true;
    if(auto* candidate=joint_contact_offer(kind==joint_world::Target::camp?joint_world::Purpose::work:joint_world::Purpose::counteraction,1)){
        joint_contact_append(candidate->words[0],joint_contact_action(joint_world::ActionKind::basic,kind,target));
        if(append_adjacent&&!owner.ranged&&contact.request.path_count&&joint_contact_adjacent(contact.request.path[0]))
            joint_contact_append(candidate->words[0],joint_contact_action(joint_world::ActionKind::move,joint_world::Target::ground,0,joint_point(contact.request.path[0])));
        joint_contact_commit();}
    return true;
}
JointContactStatus joint_observation_prepare(const Info& me,const JointContactRequest& request,const JointContactBudget& budget){
    auto& contact=joint_contact_state();auto& observation=joint_observation_state();++observation.calls;
    observation.epoch=epoch;observation.facts=0;observation.started=0;observation.stage=JointObservationState::Stage::roster;
    observation.first_source_handle=observation.first_source_group=observation.target_handle=observation.target_life=0;
    observation.own_receipt_epoch=observation.target_receipt_epoch=-1;
    observation.target_body_complete=observation.target_buffs_complete=false;
    contact.owner_epoch=epoch;contact.budget=budget;contact.phase_start=0;contact.status=JointContactStatus::partial;
    contact.request=request;contact.request.path_count=0;contact.candidate_count=contact.evaluated_count=0;contact.selection={};
    contact.count_derived_tail=true;contact.funding={joint_contact_candidate_tail(1),budget.evaluation_bound,false};
    if(!JointContactFunding::valid(budget))return joint_observation_finish(JointContactStatus::invalid_budget);
    if(!request.horizon||request.horizon>8||request.path_count>request.path.size())
        return joint_observation_finish(JointContactStatus::capacity);
    if(!JointObservationFunding::before(fuel_remaining(),contact.funding.command,budget.evaluation_bound,budget.enumeration_bound))
        return joint_observation_finish(JointContactStatus::partial);
    auto& world=contact.world;world.frame=++contact.world_revision;world.effect=contact.effect_revision;
    world.actor_count=world.camp_count=world.reply_count=world.frontier_count=world.blocked_count=0;
    world.capture={};world.aggregate={};world.terrain_complete=false;world.effects_complete=false;
    world.sources_complete=true;world.schedule_complete=false;world.no_exogenous_wake=request.no_exogenous_wake;
    world.no_respawn=true;world.horizon_intervals=request.horizon;world.operation_quota=30000;
    world.observation_intent=request.observation_intent;world.observation_cursor=request.observation_cursor;
    world.own_spawn=joint_point(request.spawn);world.spawn_valid=request.spawn_known;world.camp_orders_known=0;
    contact.body_cursor=contact.camp_queries=contact.reply_classes=contact.unknown_power=0;
    contact.unknown_schedule=true;contact.visible_ready=false;contact.refusal_branch=false;
    contact.own_moves.reset();contact.move_blocked.reset();contact.dash_blocked.reset();for(auto& dash:contact.own_dashes)dash.reset();
    joint_observation_stage(JointObservationState::Stage::roster);joint_contact_phase(JointContactState::Phase::roster);
    if(!joint_observation_roster())return joint_observation_finish(contact.status);world.actor_count=contact.count;
    joint_observation_stage(JointObservationState::Stage::own_packet);joint_contact_phase(JointContactState::Phase::details);
    if(!joint_observation_own(me))return joint_observation_finish(contact.status);
    const auto& own=world.actors[world.owner];
    if(!own.alive||state(contact.records[world.owner].body).contains(champion_state::teleporting)||own.channel.kind!=joint_world::ChannelKind::none)
        return joint_observation_finish(JointContactStatus::no_offers);
    joint_observation_stage(JointObservationState::Stage::enemy_bodies);joint_contact_phase(JointContactState::Phase::bodies);
    if(!joint_observation_bodies())return joint_observation_finish(contact.status);
    // This World switch describes CAMP respawn, not an unseen champion's
    // lifetime. Champion uncertainty stays in its retained actor/envelope.
    world.no_respawn=true;
    joint_observation_stage(JointObservationState::Stage::visibility);int code=0;
    if(!joint_observation_query([&]{auto sight=vision::visible_tiles();sight.export_linear_buffer(visible);return 0;},code))return joint_observation_finish(contact.status);
    contact.visible_ready=true;
    if(!joint_observation_query([&]{contact.occupied=vision::champion_tiles();return 0;},code)||
       !joint_observation_query([&]{*contact.occupied|=vision::camp_tiles();return 0;},code)||
       !joint_observation_query([&]{*contact.occupied|=vision::teleport_tiles();return 0;},code))return joint_observation_finish(contact.status);
    contact.occupied->set(joint_vec(own.position),false);
    joint_observation_stage(JointObservationState::Stage::sources);joint_contact_phase(JointContactState::Phase::camps);
    if constexpr(cfg::indexed_catalogue)
        if(joint_catalogue_usable()&&!joint_catalogue_source_prepare())return joint_observation_finish(contact.status);
    bool sources_complete=false;
    if(!joint_contact_compute([&]{sources_complete=joint_observation_sources();})||!sources_complete)
        return joint_observation_finish(contact.status);
    joint_observation_stage(JointObservationState::Stage::target);
    if(!joint_observation_target())return joint_observation_finish(contact.status);
    joint_observation_stage(JointObservationState::Stage::frontier);joint_contact_phase(JointContactState::Phase::frontiers);
    bool frontier_complete=false;
    if(!joint_contact_compute([&]{frontier_complete=joint_contact_frontiers();})||!frontier_complete)
        return joint_observation_finish(contact.status);
    world.observation=contact.observation_revision;
    joint_observation_stage(JointObservationState::Stage::offer);joint_contact_phase(JointContactState::Phase::menu);
    if(!joint_contact_compute([&]{joint_contact_capture();joint_observation_plain_basic();}))return joint_observation_finish(contact.status);
    if(!joint_contact_price_pending())return joint_observation_finish(contact.status);
    // Reward/capture remains a complete route-free alternative when Basic has
    // no live target. No unknown ability/gear menu is mandatory for this class.
    if(!contact.funding.incumbent&&world.capture.enabled&&world.capture.group_clear)
        joint_contact_family(4);
    if(contact.status==JointContactStatus::overrun||contact.status==JointContactStatus::stale)return joint_observation_finish(contact.status);
    // The first useful score and command tail now exist. Immutable enrichment
    // cannot starve that first quote and does not mutate its acquired World.
    // If there is no quote yet, a bounded catalogue step is still real finite
    // preparation, but partial status does not claim the input.
    joint_observation_stage(JointObservationState::Stage::immutable);joint_observation_catalog();
    if(contact.status==JointContactStatus::overrun||contact.status==JointContactStatus::stale)return joint_observation_finish(contact.status);
    ++contact.prepared;return joint_observation_finish(contact.funding.incumbent?JointContactStatus::ready:
        contact.evaluated_count?JointContactStatus::no_offers:JointContactStatus::partial);
}
#endif
