// Included inside Bot only for the optional Triage support experiment. Keep
// the original first target, fight and clear/move attempt outside this helper.
void postcast_basic(Group& entry,const camp& dead_target,int observed_round) {
    if constexpr(requires{Policy::triage_postcast_basic;}) {
        if constexpr(!Policy::triage_postcast_basic)return;
        auto& me=cambc::self();
        const auto fresh=[&] {
            return int(cambc::round_number())==observed_round&&cambc::fuel_remaining()>=35000;
        };
        const auto ready=[&] {
            return fresh()&&me.can_attack()&&!me.can_move()&&fresh();
        };
        if(!fresh()||me.champion_name()!=cambc::champion_type::tavi||!fresh())return;
        // A conservative support cap, not an asserted catalog maximum. The
        // cached span owns membership; never reload a group for each camp.
        const auto members=entry.group.camps();
        if(members.empty()||members.size()>64)return;
        std::array<cambc::handle_camp_t,64> identities{};
        std::size_t count=0;
        bool contains_dead=false;
        for(const auto& member:members) {
            const auto id=static_cast<cambc::handle_camp_t>(member);
            if(std::find(identities.begin(),identities.begin()+count,id)!=identities.begin()+count)return;
            identities[count++]=id;
            if(!cambc::map::is_in_bounds(member.spawn_position())||!entry.spawns.is_set(member.spawn_position()))return;
            contains_dead|=id==static_cast<cambc::handle_camp_t>(dead_target);
        }
        // Dead camps need not have a visible live position. In particular do
        // not feed a dead target's (-1,-1) position into a range board.
        if(!contains_dead||!fresh()||dead_target.is_alive()||!fresh()||!ready())return;
        const float damage=me.strength()*(monster_shards_>=5?1.1f:1.f);
        if(!fresh()||!std::isfinite(damage)||damage<=0)return;
        const auto eligible=[&](const camp& member,float& hp) {
            if(!fresh()||!member.is_visible()||!fresh())return false;
            if(!member.is_alive()||!fresh())return false;
            hp=member.health();
            if(!fresh()||!std::isfinite(hp)||hp<=0)return false;
            const auto position=member.position();
            if(!fresh()||!cambc::map::is_in_bounds(position))return false;
            const auto attack_tiles=me.attack_tiles();
            return fresh()&&attack_tiles.is_set(position);
        };
        std::optional<camp> next;
        float weakest=std::numeric_limits<float>::max();
        for(const auto& member:members) {
            if(static_cast<cambc::handle_camp_t>(member)==static_cast<cambc::handle_camp_t>(dead_target))continue;
            if(!ready())return;
            float hp=0;
            if(!eligible(member,hp)) {
                if(!fresh())return;
                continue;
            }
            const float incoming=member.strength();
            if(!fresh())return;
            if(!std::isfinite(incoming)||incoming<0)continue;
            const float rank=p11::target_rank(hp,incoming,damage);
            if(std::isfinite(rank)&&rank<weakest){weakest=rank;next=member;}
        }
        float hp=0;
        if(!next||!ready()||!eligible(*next,hp)||!ready())return;
        me.attack(*next);
    }
}
