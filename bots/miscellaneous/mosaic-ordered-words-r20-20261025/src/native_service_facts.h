// Included inside Runtime, after native_service_types.h at file scope.
// Only explicitly selected scalar facts are imported. Each acquired field is
// published independently, including a partial/Unknown answer.
struct NativeFactsLogic {
    static void reject(native_service::SelfFact& out) {
        out.epoch.round=-1;
        out.body_actual=out.restrictions_actual=out.buffs_actual=out.channel_actual=out.channel_time_actual=false;
        out.custody_actual=out.gems_actual=out.gold_actual=out.walked_actual=false;
    }
    static void reject(native_service::CampFact& out) {
        out.epoch.round=-1;out.life=native_service::Life::unknown;
        out.life_actual=out.body_actual=out.buffs_actual=false;
    }
    static void reject(native_service::GroupFact& out) {
        out.epoch.round=-1;out.life=native_service::GroupLife::unknown;
        out.actual=out.respawn_actual=out.visibility_actual=false;
    }
    static void reject(native_service::AbilityFact& out) {out.epoch.round=-1;out.cooldown_actual=false;}
    static void reject(native_service::BankFact& out) {
        out.epoch.round=-1;out.custody_actual=out.submitted_actual=out.channel_actual=false;
    }
    template<class Budget> static bool hard_poison(Budget& budget) {
        if constexpr(requires {bool(budget.poisoned());})return bool(budget.poisoned());
        else {
            static_assert(requires {bool(budget.poisoned);},"native query budget requires a poisoned field or method");
            return bool(budget.poisoned);
        }
    }
    template<class Budget,class Fact> static bool query_before(Budget& budget,Fact& out,native_service::Facts* retained=nullptr) {
        if(budget.before_query())return true;
        reject(out);
        // Declining optional work does not revoke a previously acquired action.
        if(retained&&hard_poison(budget))retained->reject();
        return false;
    }
    template<class Budget,class Fact> static bool query_after(Budget& budget,Fact& out,native_service::Facts* retained=nullptr) {
        if(budget.after_query())return true;reject(out);
        // A span already ran: failed closure is an overrun/stale-input failure.
        if(retained)retained->reject();return false;
    }
    template<class Raw> static void self_body(native_service::SelfFact& out,const Raw& q) {
        out.team=q.team;out.pose={q.x,q.y};out.hp=q.health;out.max_hp=q.max_health;
        out.strength=q.strength;out.focus=q.focus;out.attack_range=q.attack_range;out.move_speed=q.move_speed;
        const auto bits=std::uint32_t(q.state);
        out.alive=(bits&1u)!=0;out.recalling=(bits&2u)!=0;out.submitting=(bits&4u)!=0;
        out.raw_m=(bits&8u)!=0;out.raw_a=(bits&16u)!=0;out.completed=(bits&32u)!=0;
        out.recall_started=(bits&64u)!=0;out.submit_started=(bits&128u)!=0;
        out.raw_c=(bits&2048u)!=0;out.teleporting=(bits&4096u)!=0;
        out.ranged=q.attack_range>1;out.body_actual=out.channel_actual=true;
        out.channel_time_actual=false;
        if(!out.recalling&&!out.submitting&&!out.teleporting){out.channel_time=0;out.channel_time_actual=true;}
    }
    static void restrictions(native_service::SelfFact& out,std::uint8_t bits) {
        out.move_restricted=(bits&1u)!=0;out.attack_restricted=(bits&2u)!=0;out.cast_restricted=(bits&4u)!=0;
        out.restrictions_actual=true;
    }
    template<class Raw> static bool self_buffs(native_service::SelfFact& out,const Raw* raw,unsigned count) {
        out.buffs_actual=false;out.shield=0;out.shield_time=out.stun_time=out.enhanced_time=0;
        out.stunned=out.enhanced=false;if(count>3)return false;
        for(unsigned i=0;i<count;++i)switch(raw[i].type){
        case 0:out.shield=raw[i].data.shield.value;out.shield_time=raw[i].data.shield.duration;break;
        case 1:out.stunned=true;out.stun_time=raw[i].data.stun.duration;break;
        case 2:out.enhanced=true;out.enhanced_time=raw[i].data.enhanced_attack.duration;break;
        default:return false;
        }
        out.buffs_actual=true;return true;
    }
    static bool camp_life(native_service::CampFact& out,unsigned value) {
        out.body_actual=out.buffs_actual=false;
        out.life_actual=value<=2;
        out.life=value==0?native_service::Life::dead:value==1?native_service::Life::awake:
            value==2?native_service::Life::asleep:native_service::Life::unknown;
        // Dead establishes life, not a successful current-body/buff import.
        if(value==0)out.hp=out.shield=0;
        return out.life_actual;
    }
    template<class Raw> static void camp_body(native_service::CampFact& out,const Raw& q) {
        out.pose={q.x,q.y};out.hp=q.health;out.max_hp=q.max_health;out.strength=q.strength;out.body_actual=true;
    }
    template<class Raw> static bool camp_buffs(native_service::CampFact& out,const Raw* raw,unsigned count) {
        out.buffs_actual=false;out.shield=0;out.shield_time=out.stun_time=0;out.stunned=false;
        if(count>3)return false;
        for(unsigned i=0;i<count;++i)switch(raw[i].type){
        case 0:out.shield=raw[i].magnitude;out.shield_time=raw[i].timer;break;
        case 1:out.stunned=true;out.stun_time=raw[i].timer;break;
        case 2:break; // Enhancement does not change the camp's published HP/strength.
        default:return false;
        }
        out.buffs_actual=true;return true;
    }
    static bool group_life(native_service::GroupFact& out,unsigned value) {
        out.actual=value>=1&&value<=3;out.respawn_actual=false;
        out.life=value==1?native_service::GroupLife::alive:value==2?native_service::GroupLife::reward:
            value==3?native_service::GroupLife::respawning:native_service::GroupLife::unknown;
        return value<=3;
    }
    template<class Raw> static bool descriptor(native_service::AbilityDescriptor& out,const Raw& raw) {
        out={};out.target_mask=raw.targets;out.max_cooldown=raw.cooldown;
        out.exclusive_with_movement=(raw.attributes&1u)!=0;
        if(raw.kind>7)return false;
        out.kind=static_cast<native_service::AbilityKind>(raw.kind);out.present=raw.kind!=0;
        switch(raw.kind){
        case 0:break;
        case 1:out.enhanced_attack=raw.payload.enhanced_attack!=0;break;
        case 2:out.range=raw.payload.damage.range;out.modifier=raw.payload.damage.modifier;break;
        case 3:out.range=raw.payload.aoe_damage.range;out.modifier=raw.payload.aoe_damage.modifier;
            out.radius=raw.payload.aoe_damage.radius;out.duration=raw.payload.aoe_damage.duration;break;
        case 4:out.range=raw.payload.cc.range;out.modifier=raw.payload.cc.modifier;out.duration=raw.payload.cc.duration;break;
        case 5:out.range=raw.payload.heal.range;out.modifier=raw.payload.heal.modifier;break;
        case 6:out.range=raw.payload.dash.range;out.modifier=raw.payload.dash.modifier;
            out.radius=raw.payload.dash.radius;out.duration=raw.payload.dash.duration;break;
        case 7:out.range=raw.payload.shield.range;out.modifier=raw.payload.shield.modifier;out.duration=raw.payload.shield.duration;break;
        }
        out.actual=true;return true;
    }
    static void ability_begin(native_service::AbilityFact& out,const native_service::AbilityFact& cached,
                              native_service::Epoch epoch) {
        // The common caller writes straight into the cached slot itself.
        const auto descriptor=cached.descriptor;out={};out.epoch=epoch;out.descriptor=descriptor;
    }
};

#ifndef MOSAIC_NATIVE_SERVICE_FACTS_PURE_ONLY
static_assert(std::uint32_t(native_service::AbilityKind::reset)==std::uint32_t(ability_kind::attack_reset)&&
    std::uint32_t(native_service::AbilityKind::aoe_damage)==std::uint32_t(ability_kind::aoe_damage)&&
    std::uint32_t(native_service::AbilityKind::shield)==std::uint32_t(ability_kind::shield));
static_assert(native_service::target_camps==detail::ability_target_camps&&native_service::target_enemies==detail::ability_target_enemies&&
    native_service::target_allies==detail::ability_target_allies&&native_service::target_self==detail::ability_target_self&&
    native_service::target_ground==detail::ability_target_ground);
static native_service::Facts& native_facts() {
    static constinit native_service::Facts value{};return value;
}
static native_service::Facts& native_getstate(){return native_facts();}
template<class Budget,class Fact> static bool native_query_before(Budget& budget,Fact& out) {
    return NativeFactsLogic::query_before(budget,out,&native_facts());
}
template<class Budget,class Fact> static bool native_query_after(Budget& budget,Fact& out) {
    return NativeFactsLogic::query_after(budget,out,&native_facts());
}
void native_begin_input(){native_facts().begin(epoch);}
void native_after_attempt(native_service::EffectScope scope=native_service::EffectScope::position_or_damage) {
    native_facts().attempted(scope);
}
static bool native_hidden(int code) {
    return code==int(error_code::api_not_in_vision)||code==int(error_code::api_not_found);
}
template<class Budget> bool native_read_self(Budget& budget,native_service::SelfFact& out,bool gems=false,bool gold=false) {
    out={};out.epoch=native_facts().epoch;out.id=std::uint32_t(handle_champion_t(self()));
    out.camp_damage_multiplier=camp_multiplier;
    out.stun_immune=passive==passive_type::stun_immunity;out.basic_splash=passive==passive_type::splash_attack;
    out.ordinary_walked=walked;out.walked_actual=true;
    if(!native_query_before(budget,out))return false;
    detail::champion_info raw{};const auto code=detail::sdk_champion(out.id,&raw);
    const bool body_paid=native_query_after(budget,out);detail::check(code);if(!body_paid)return false;
    NativeFactsLogic::self_body(out,raw);
    if(!native_query_before(budget,out))return false;
    std::uint8_t restrictions{};const auto rc=detail::sdk_champion_restrictions(out.id,&restrictions);
    const bool restrictions_paid=native_query_after(budget,out);detail::check(rc);if(!restrictions_paid)return false;
    NativeFactsLogic::restrictions(out,restrictions);
    if(!native_query_before(budget,out))return false;
    std::array<detail::buff_data,3> buffs{};std::uint32_t count{};
    const auto bc=detail::sdk_champion_buff_data(out.id,buffs.data(),buffs.size(),&count);
    const bool buffs_paid=native_query_after(budget,out);detail::check(bc);if(!buffs_paid)return false;
    if(!NativeFactsLogic::self_buffs(out,buffs.data(),count))throw std::runtime_error("error unknown native self buff");
    if(!out.channel_time_actual){
        if(!native_query_before(budget,out))return false;
        const auto cc=detail::sdk_champion_channel_time(out.id,&out.channel_time);
        const bool paid=native_query_after(budget,out);detail::check(cc);if(!paid)return false;out.channel_time_actual=true;
    }
    if(gems){
        if(!native_query_before(budget,out))return false;
        std::uint32_t value{};const auto gc=detail::sdk_champion_gems(out.id,&value);
        const bool paid=native_query_after(budget,out);detail::check(gc);if(!paid)return false;
        out.gems=int(value);out.gems_actual=out.custody_actual=true;
    }
    if(gold){
        if(!native_query_before(budget,out))return false;
        std::uint32_t value{};const auto xc=detail::sdk_champion_gold(out.id,&value);
        const bool paid=native_query_after(budget,out);detail::check(xc);if(!paid)return false;
        out.gold=value;out.gold_actual=true;
    }
    return true;
}
template<class Budget> bool native_read_camp(Budget& budget,int ordinal,native_service::CampFact& out) {
    out={};out.epoch=native_facts().epoch;out.member_ordinal=ordinal;
    if(ordinal<0||std::size_t(ordinal)>=members.size())return false;
    const auto& member=members[std::size_t(ordinal)];out.group_ordinal=member.group;
    out.id=std::uint32_t(handle_camp_t(member.handle));out.pose={member.pos.x,member.pos.y};out.range=member.range;
    if(member.group<0||std::size_t(member.group)>=groups.size())return false;
    out.group_id=std::uint32_t(handle_camp_group_t(groups[std::size_t(member.group)]));
    if(!native_query_before(budget,out))return false;
    std::uint32_t life{};const auto code=detail::sdk_camp_state(out.id,&life);
    const bool life_paid=native_query_after(budget,out);if(native_hidden(code))return life_paid;
    detail::check(code);if(!life_paid)return false;
    if(!NativeFactsLogic::camp_life(out,life))throw std::runtime_error("error unknown native camp life");
    if(out.life==native_service::Life::dead)return true;
    if(!native_query_before(budget,out))return false;
    detail::camp_state body{};const auto bc=detail::sdk_camp(out.id,&body);const bool body_paid=native_query_after(budget,out);
    if(native_hidden(bc))return body_paid;detail::check(bc);if(!body_paid)return false;NativeFactsLogic::camp_body(out,body);
    if(!native_query_before(budget,out))return false;
    std::array<detail::camp_buff_data,3> buffs{};std::uint32_t count{};
    const auto fc=detail::sdk_camp_buffs(out.id,buffs.data(),buffs.size(),&count);const bool paid=native_query_after(budget,out);
    if(native_hidden(fc))return paid;detail::check(fc);if(!paid)return false;
    if(!NativeFactsLogic::camp_buffs(out,buffs.data(),count))throw std::runtime_error("error unknown native camp buff");
    return paid;
}
template<class Budget> bool native_read_group(Budget& budget,int ordinal,native_service::GroupFact& out,int witness=-1) {
    out={};out.epoch=native_facts().epoch;out.ordinal=ordinal;
    if(ordinal<0||std::size_t(ordinal)>=groups.size())return false;
    const auto& group=groups[std::size_t(ordinal)];out.id=std::uint32_t(handle_camp_group_t(group));
    out.gem_value=group.gem_value();out.gold_value=group.gold_value();
    if(witness<0){const auto& stored=memory[std::size_t(ordinal)];if(stored.begin<stored.end)witness=int(stored.begin);}
    if(witness<0||std::size_t(witness)>=members.size()||members[std::size_t(witness)].group!=ordinal)return true;
    if(!native_query_before(budget,out))return false;
    const auto at=members[std::size_t(witness)].pos;std::int32_t visible{};
    const auto vc=detail::sdk_is_visible(at.x,at.y,&visible);const bool visibility_paid=native_query_after(budget,out);
    detail::check(vc);if(!visibility_paid)return false;out.visibility_actual=true;out.visible=visible!=0;
    if(!out.visible)return true;
    if(!native_query_before(budget,out))return false;
    std::uint32_t life{};const auto code=detail::sdk_camp_group_state(out.id,&life);const bool paid=native_query_after(budget,out);
    if(native_hidden(code))return paid;detail::check(code);if(!paid)return false;
    if(!NativeFactsLogic::group_life(out,life))throw std::runtime_error("error unknown native group life");
    if(out.life!=native_service::GroupLife::respawning)return true;
    if(!native_query_before(budget,out))return false;
    const auto rc=detail::sdk_camp_group_cooldown(out.id,&out.respawn_time);const bool cooldown_paid=native_query_after(budget,out);
    if(native_hidden(rc))return cooldown_paid;detail::check(rc);if(!cooldown_paid)return false;out.respawn_actual=true;return true;
}
template<class Budget> bool native_read_ability(Budget& budget,unsigned slot,native_service::AbilityFact& out) {
    if(slot>=2)return false;
    auto& cached=native_facts().abilities[slot];NativeFactsLogic::ability_begin(out,cached,native_facts().epoch);
    const auto owner=std::uint32_t(handle_champion_t(self()));
    if(!out.descriptor.actual){
        if(!native_query_before(budget,out))return false;
        detail::ability_props props{};const auto code=detail::sdk_champion_ability_props(owner,slot,&props);
        const bool paid=native_query_after(budget,out);detail::check(code);if(!paid)return false;
        if(!NativeFactsLogic::descriptor(out.descriptor,props))throw std::runtime_error("error unknown native ability kind");
        cached.descriptor=out.descriptor;
    }
    if(!out.descriptor.present){cached=out;return true;}
    if(!native_query_before(budget,out))return false;
    const auto code=detail::sdk_champion_cooldown(owner,slot,&out.cooldown);const bool paid=native_query_after(budget,out);
    detail::check(code);if(!paid)return false;out.cooldown_actual=true;cached=out;return true;
}
template<class Budget> bool native_read_cooldown(Budget& budget,unsigned slot,native_service::AbilityFact& out) {
    return native_read_ability(budget,slot,out);
}
void native_capture_at(const native_service::SelfFact& self_fact,const native_service::GroupFact& group,
                       int member_ordinal,native_service::CaptureFact& out)const {
    out={};out.epoch=native_facts().epoch;out.group_id=group.id;
    out.life=group.life;out.life_actual=group.current(out.epoch);
    if(!self_fact.actual(out.epoch)||member_ordinal<0||std::size_t(member_ordinal)>=members.size())return;
    const auto& member=members[std::size_t(member_ordinal)];
    if(member.group!=group.ordinal)return;
    if(self_fact.pose==native_service::Point{member.pos.x,member.pos.y})out.on_spawn_actual=out.on_spawn=true;
}
template<class Budget> bool native_read_bank(Budget& budget,const native_service::SelfFact& self_fact,native_service::BankFact& out) {
    out={};out.epoch=native_facts().epoch;
    if(self_fact.epoch==out.epoch){out.gems=self_fact.gems;out.custody_actual=self_fact.gems_actual;
        out.submitting=self_fact.submitting;out.channel_actual=self_fact.channel_actual;}
    if(self_fact.epoch!=out.epoch||!self_fact.body_actual){NativeFactsLogic::reject(out);return false;}
    if(!native_query_before(budget,out))return false;
    const auto code=detail::sdk_bc_submitted_gems(std::uint32_t(self_fact.team),&out.submitted);
    const bool paid=native_query_after(budget,out);detail::check(code);if(!paid)return false;out.submitted_actual=true;return true;
}
#endif
