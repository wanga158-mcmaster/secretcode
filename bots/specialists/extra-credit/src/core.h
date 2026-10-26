// Included inside Bot. Every field belongs to one physical actor instance.
bool pair_loaded_ = false;
bool pair_wait_logged_ = false, pair_recover_logged_ = false;
int pair_fallback_ = -1, selected_logged_ = -1, held_group_ = -1;
unsigned held_bag_ = 0, held_gold_ = 0, pair_captures_ = 0;
bool probe_done_ = false, probe_banking_ = false, probe_submitted_ = false;
unsigned probe_bank_bag_ = 0, probe_score_before_ = 0, probe_conversions_ = 0;
PairPlan pair_plan_{};
std::vector<float> pair_loss_;
std::vector<bool> pair_productive_;
bool safe_pair() const { return pair_actor() && !profile::draft_only; }
bool pair_cassi() const { return is_actor(cambc::champion_type::cassi); }
bool partition(int index) const {
    return extra_credit::owns(groups_[index].canonical_non_monolith,pair_cassi());
}
void load_pair() {
    pair_loaded_ = true; pair_loss_.assign(groups_.size(),0);pair_productive_.assign(groups_.size(),false);
    if (!safe_pair()) return;
    bool own = false;
    for (std::size_t i=0;i<groups_.size();++i) {
        const auto& g=groups_[i]; if(g.monolith) continue;
        own |= partition(static_cast<int>(i));
        // Integer-scaled B0 reward numerator: 2*gems + 0.4*gold.
        if(pair_fallback_<0 || g.gems*5+g.gold < groups_[pair_fallback_].gems*5+groups_[pair_fallback_].gold)
            pair_fallback_=static_cast<int>(i);
    }
    if(own) pair_fallback_=-1;
    else if(pair_fallback_>=0) telemetry_.event("observation","pair_partition_overlap",group_field(groups_[pair_fallback_].id)
            +",\"reason\":\"empty_partition\"");
    if constexpr(profile::probe_mode) {
        if(cambc::draft_memory.size()>=sizeof pair_plan_) std::memcpy(&pair_plan_,cambc::draft_memory.data(),sizeof pair_plan_);
        if(pair_plan_.magic!=0x45435831 || pair_plan_.status!=1 || find_group(pair_plan_.even)<0 || find_group(pair_plan_.odd)<0) {
            probe_done_=true;
            telemetry_.event("not_exercised", "pair_no_legal_probe_jobs",integer("selectorStatus",pair_plan_.status),true);
        } else telemetry_.event("opportunity","pair_opening",integer("evenGroup",pair_plan_.even)
            +","+integer("oddGroup",pair_plan_.odd)+",\"independent\":"+(profile::core_enabled?"true":"false"));
    }
}
void core_shop() {}
void pair_damage(const camp& target,float before,float after) {
    if(!safe_pair() || before<=after) return;
    const auto spawn=target.spawn_position();
    for(std::size_t i=0;i<groups_.size();++i) if(groups_[i].spawns.is_set(spawn)) {
        if(!pair_productive_[i]) {
            pair_productive_[i]=true;
            telemetry_.event("action_result","pair_first_productive_attack",group_field(groups_[i].id)
                +","+integer("tile",tile_index(cambc::self().position()))+","+number("healthBefore",before)
                +","+number("healthAfter",after));
        }
        break;
    }
}
void core_observe() {
    if(!pair_loaded_) load_pair();
    if(!safe_pair()) return;
    auto& me=cambc::self();
    if(!me.is_alive()) {
        if(!previous_alive_) return;
        telemetry_.event("observation","pair_own_linked_death",integer("ownLostBag",previous_bag_)
            +",\"partnerState\":null",true);
        held_group_=-1;
        if constexpr(profile::probe_mode) {probe_done_=true;telemetry_.event("mechanism_failed","pair_probe_own_death","",true);}
        return;
    }
    const auto position=me.position(); const auto bag=me.gem_count(),gold=me.gold_count();
    if(held_group_>=0 && seen(groups_[held_group_])) {
        const auto state=groups_[held_group_].group.state();
        if(std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
            const auto bag_delta=static_cast<long long>(bag)-held_bag_;
            const auto gold_delta=static_cast<long long>(gold)-held_gold_;
            if(bag_delta>0 || gold_delta>0) {
                ++pair_captures_;
                telemetry_.event("observation","pair_own_reward",group_field(groups_[held_group_].id)
                    +","+integer("gemsDelta",bag_delta)+","+integer("goldDelta",gold_delta)
                    +","+integer("bag",bag)+","+integer("captureNumber",pair_captures_),true);
                if constexpr(profile::probe_mode) probe_banking_=true;
            }
            held_group_=-1;
        }
    }
    if(held_group_<0) for(std::size_t i=0;i<groups_.size();++i) if(groups_[i].spawns.is_set(position) && reward(groups_[i])) {
        held_group_=static_cast<int>(i); held_bag_=bag; held_gold_=gold;
        telemetry_.event("action_result","pair_capture_enter",group_field(groups_[i].id)+","+integer("tile",tile_index(position))
            +","+integer("bag",bag)); break;
    }
    if constexpr(profile::probe_mode) {
        if(probe_submitted_ && !me.is_submitting_gems() && bag==0 && cambc::global_submitted_gems(team::ally)>probe_score_before_) {
            ++probe_conversions_; probe_banking_=false;probe_submitted_=false;
            telemetry_.event("mechanism_success","extra_credit_body_converted",integer("ownConvertedBag",probe_bank_bag_)
                +","+integer("captureCount",pair_captures_)+","+integer("conversionCount",probe_conversions_)
                +",\"independent\":"+(profile::core_enabled?"true":"false"),true);
            if(profile::core_enabled || pair_captures_>=2) probe_done_=true;
        }
        if(turn_>=240 && !pair_captures_ && !probe_done_) {
            telemetry_.event("not_exercised","pair_setup_cap","",true);probe_done_=true;
        }
    }
}
bool core_bank_request(int nearest) { return base_bank_request(nearest); }
bool core_move(bitboard,const bitboard*) { return false; }
bool core_group_allowed(int index,float incoming,float loss,float health,float maximum) {
    if(!safe_pair()) return true;
    pair_loss_[index]=loss;
    return (partition(index)||index==pair_fallback_) && extra_credit::may_enter(health,maximum,incoming,loss);
}
bool core_partial_allowed() const { return !safe_pair(); }
bool core_fork(const std::vector<Choice>&) { return false; }
void core_selected(int index) {
    pair_wait_logged_=false;pair_recover_logged_=false;
    if(!safe_pair()||selected_logged_==index) return;
    selected_logged_=index;
    telemetry_.event("activation","pair_safe_job",group_field(groups_[index].id)
        +","+integer("partition",pair_cassi()?0:1)+","+number("predictedLoss",pair_loss_[index])
        +","+number("health",cambc::self().health())+","+number("maximum",cambc::self().max_health()));
}
void core_no_job() {
    if(!safe_pair() || pair_wait_logged_) return;
    pair_wait_logged_=true;
    telemetry_.event("observation","pair_no_safe_partition_job",integer("partition",pair_cassi()?0:1)
        +",\"fallback\":"+(pair_fallback_>=0?"true":"false"));
}
bool core_retreat(const std::vector<champion>& enemies) const {
    if(!safe_pair()) return false;
    auto& me=cambc::self();
    return extra_credit::retreat(me.health(),me.max_health(),enemy_near(me.position(),enemies,8));
}
void core_recovery() {
    if(safe_pair()&&!pair_recover_logged_) {
        pair_recover_logged_=true;
        telemetry_.event("observation","pair_recover",number("health",cambc::self().health()));
    }
}
bool core_departing(int) const {return false;}
bool core_turn(const std::vector<camp>& camps,const std::vector<champion>&,const std::vector<champion>&) {
    if constexpr(profile::probe_mode) {
        if(!safe_pair()) return true;
        if(probe_done_) return true;
        auto& me=cambc::self();
        if(probe_banking_) { if(me.gem_count()) bank(); return true; }
        const int id=profile::core_enabled ? (pair_cassi()?pair_plan_.even:pair_plan_.odd)
            : (pair_captures_?pair_plan_.odd:pair_plan_.even);
        const int index=find_group(id);
        if(index<0) return true;
        target_=index;partial_clear_=false;
        if(selected_logged_!=index) {
            selected_logged_=index;
            telemetry_.event("activation","pair_probe_job",group_field(id)+","+integer("partition",pair_cassi()?0:1)
                +",\"independent\":"+(profile::core_enabled?"true":"false"));
        }
        farm_group(index,camps);return true;
    }
    return false;
}
