// Actor-local timer observations and one bounded denial episode. Included in Bot.
enum class Denial { farm, approach_due, suppress, release };
struct TimerObservation { unsigned round=0, due=0; int timer=-1; bool observed=false; };
std::vector<TimerObservation> rw_timers_;
std::vector<unsigned> rw_blacklist_;
std::optional<Field> rw_ally_field_, rw_enemy_field_;
Denial rw_state_=Denial::farm;
int rw_group_=-1, rw_spawn_=-1, rw_exit_=-1;
unsigned rw_due_=0, rw_started_=0, rw_first_occupied_=0, rw_last_occupied_=0;
unsigned rw_eligible_=0, rw_early_=0, rw_travel_=0;
bool rw_witnessed_=false, rw_probe_done_=false, rw_waiting_productive_=false;
unsigned rw_release_round_=0,rw_last_gold_=0,rw_last_gems_=0;
unsigned rw_total_eligible_=0,rw_total_early_=0,rw_total_travel_=0,rw_releases_=0;
std::string core_summary()const{return integer("eligiblePhases",rw_total_eligible_)+","+integer("earlyWait",rw_total_early_)
    +","+integer("approach",rw_total_travel_)+","+integer("releases",rw_releases_);}
int rw_probe_phase_=0;
unsigned rw_probe_captures_=0, rw_probe_last_bag_=0;

long long rw_lead() const { return static_cast<long long>(cambc::global_submitted_gems(team::ally))
    - static_cast<long long>(cambc::global_submitted_gems(team::enemy)); }
bool rw_actor() const { return is_actor(cambc::champion_type::varg); }
bool rw_side_a() const { return cambc::map::spawn_region(team::ally).is_set(tile_position(44)); }
int rw_probe_id() const { return rw_side_a()?profile::probe_group_a:profile::probe_group_b; }
int rw_probe_exit() const { return rw_side_a()?profile::probe_exit_a:profile::probe_exit_b; }
std::string rw_fields() const { return group_field(groups_[rw_group_].id)+","+integer("predictedDue",rw_due_)
    +","+integer("eligibleHoldPhases",rw_eligible_)+","+integer("earlyWaitTurns",rw_early_)
    +","+integer("approachTurns",rw_travel_)+","+integer("lead",rw_lead()); }
void rw_end(const char* reason) {
    if(rw_group_<0)return;
    telemetry_.event("observation","suppression_exit",rw_fields()+",\"reason\":\""+reason+"\"",true);
    rw_blacklist_[rw_group_]=cambc::round_number()+std::max(1,groups_[rw_group_].group.max_respawn_timer());
    rw_state_=Denial::release;
}
void core_shop() {}
void core_observe() {
    if(rw_timers_.empty()){rw_timers_.resize(groups_.size());rw_blacklist_.resize(groups_.size());}
    auto& me=cambc::self();const auto now=cambc::round_number();
    const auto personally_visible=cambc::vision::visible_tiles();
    if(rw_actor())for(std::size_t i=0;i<groups_.size();++i){
        if(cambc::fuel_remaining()<45000)break;
#if RESERVOIR_SPAWN_VISIBILITY
        if(!groups_[i].group.gem_value()||!any_spawn_visible(groups_[i],personally_visible))continue;
#else
        if(!groups_[i].group.gem_value()||(groups_[i].spawns&personally_visible).is_empty())continue;
#endif
        const auto state=groups_[i].group.state();
        if(const auto* r=std::get_if<cambc::map::camp_group_respawning>(&state)){
            auto& o=rw_timers_[i];o={now,now+static_cast<unsigned>(std::max(0,r->timer)),r->timer,true};
        }
    }
    if(rw_waiting_productive_&&(me.gold_count()>rw_last_gold_||me.gem_count()>rw_last_gems_)){
        telemetry_.event("observation","next_productive_event",integer("elapsedSinceRelease",now-rw_release_round_)
            +","+integer("goldGain",me.gold_count()>rw_last_gold_?me.gold_count()-rw_last_gold_:0)
            +","+integer("gemGain",me.gem_count()>rw_last_gems_?me.gem_count()-rw_last_gems_:0),true);
        rw_waiting_productive_=false;
    }
    rw_last_gold_=me.gold_count();rw_last_gems_=me.gem_count();
    if(!me.is_alive()&&rw_state_!=Denial::farm){rw_end("death");rw_state_=Denial::farm;rw_group_=-1;}
    if constexpr(profile::probe_mode){
        const unsigned bag=me.gem_count();
        if(bag>rw_probe_last_bag_){
            ++rw_probe_captures_;
            telemetry_.event("observation","setup_capture",integer("bag",bag)+","+integer("captureOrdinal",rw_probe_captures_),true);
            if(rw_actor()&&rw_probe_phase_==0)rw_probe_phase_=1;
        }
        rw_probe_last_bag_=bag;
        if(rw_actor()&&rw_probe_phase_==1&&bag==0&&rw_probe_captures_){
            rw_probe_phase_=2;
            telemetry_.event("observation","setup_banked",integer("lead",rw_lead()),true);
        }
        if(!rw_actor()){
            const unsigned required=is_actor(cambc::champion_type::tavi)?2:(is_actor(cambc::champion_type::vendetta)?1:0);
            if(!required||(rw_probe_captures_>=required&&bag==0))rw_probe_done_=true;
        }
        if(!rw_probe_done_&&rw_probe_phase_<3&&now>profile::probe_setup_limit){
            rw_probe_done_=true;
            telemetry_.event("not_exercised","reservoir_probe", "\"reason\":\"legal_setup_cap_without_release_predicate\"",true);
        }
    }
}
bool core_bank_request(int nearest) {
    if constexpr(profile::probe_mode){
        if(winning_bag())return true;
        if(rw_actor())return rw_probe_phase_==1;
        if(is_actor(cambc::champion_type::tavi))return rw_probe_captures_>=2;
        if(is_actor(cambc::champion_type::vendetta))return rw_probe_captures_>=1;
        return false;
    }
    return base_bank_request(nearest);
}
bool core_move(bitboard,const bitboard*){return false;}
bool core_group_allowed(int index,float) {
    if constexpr(profile::probe_mode)return groups_[index].id!=rw_probe_id();
    return true;
}
bool core_partial_allowed()const{return true;}
bool core_fork(const std::vector<Choice>&){return false;}
void core_selected(int){}
bool core_retreat(const std::vector<champion>&)const{return false;}
bool core_departing(int)const{return false;}
void core_recovery(){
    if(rw_state_==Denial::approach_due||rw_state_==Denial::suppress){
        rw_end("recovery");
        if(rw_group_>=0&&groups_[rw_group_].spawns.is_set(cambc::self().position())){
            const auto avoid=danger(cambc::vision::camps(),cambc::vision::champions(team::enemy));
            rw_step_exit(avoid);
        }
    }
}
bool rw_static_fields(){
    if(rw_ally_field_&&rw_enemy_field_)return true;
    if(!rw_ally_field_){Field f;if(!field(f,cambc::map::spawn_region(team::ally),terrain_))return false;rw_ally_field_=std::move(f);}
    if(!rw_enemy_field_){
        bitboard roots;for(auto p:cambc::map::spawn_region(team::enemy).positions())roots|=around(p,1);
        roots&=~terrain_;Field f;if(!field(f,roots,terrain_))return false;rw_enemy_field_=std::move(f);
    }
    return true;
}
int rw_safe_exit(int index,int spawn,const bitboard& avoid)const{
    const auto& g=groups_[index];const auto walls=blocked()|avoid;int best=-1;
    for(auto p:around(tile_position(spawn),1).positions())
        if(!g.spawns.is_set(p)&&!walls.is_set(p)&&(best<0||tile_index(p)<best))best=tile_index(p);
    return best;
}
bool rw_enemy_near_region(const Group& g,const std::vector<champion>& enemies)const{
    for(auto p:g.spawns.positions())if(enemy_near(p,enemies))return true;return false;
}
void rw_step_exit(const bitboard& avoid){
    if(rw_group_<0||!groups_[rw_group_].spawns.is_set(cambc::self().position()))return;
    const int safe=rw_safe_exit(rw_group_,tile_index(cambc::self().position()),avoid);
    if(safe>=0){rw_exit_=safe;ordinary_move(around(tile_position(safe),0),&avoid);}
}
bool rw_denial(const std::vector<camp>& camps,const std::vector<champion>& enemies,const std::vector<champion>& allies,int forced=-1){
    auto& me=cambc::self();const auto now=cambc::round_number();
    const auto avoid=danger(camps,enemies);
    if(rw_state_==Denial::release){
        if(groups_[rw_group_].spawns.is_set(me.position())){rw_step_exit(avoid);return true;}
        telemetry_.event("observation","suppression_released",rw_fields()+","+integer("tile",tile_index(me.position())),true);
        rw_blacklist_[rw_group_]=now+std::max(1,groups_[rw_group_].group.max_respawn_timer());
        rw_waiting_productive_=true;rw_release_round_=now;++rw_releases_;
        rw_state_=Denial::farm;rw_group_=-1;
        if constexpr(profile::probe_mode){rw_probe_done_=true;rw_probe_phase_=4;}
        return false;
    }
    if(rw_state_==Denial::farm){
        if(cambc::fuel_remaining()<45000)return false;
        if(!reservoir::enabled_by_score(rw_lead(),me.gem_count())||(!profile::core_enabled&&!profile::probe_mode))return false;
        if(me.gold_count()>=300&&!cambc::map::spawn_region(team::ally).is_set(me.position()))return false;
        if(!rw_static_fields())return true;
        // Cached static fields cost at most two fields on initialization. Defer
        // candidate path work when that turn's field allowance was consumed.
        if(fields_>=2)return true;
        if(avoid.is_set(me.position()))return false;
        // Prove known safe arrival, not just optimistic terrain reachability.
        auto safe_walls=blocked()|avoid;safe_walls.set(me.position(),false);
        Field safe_origin;if(!field(safe_origin,around(me.position(),0),safe_walls))return true;
        origin_field_=std::move(safe_origin);origin_field_position_=me.position();
        int selected=-1,spawn=-1,exit=-1,best_travel=unreachable;unsigned best_gems=0;
        for(std::size_t i=0;i<groups_.size();++i){
            if(cambc::fuel_remaining()<45000)break;
            auto& g=groups_[i];const auto& o=rw_timers_[i];
            if((forced>=0&&static_cast<int>(i)!=forced)||!g.group.gem_value()||rw_blacklist_[i]>now||!o.observed||!seen(g))continue;
            const int a=min_distance(*rw_ally_field_,g.spawns),e=min_distance(*rw_enemy_field_,g.spawns);
            if(a==unreachable||e==unreachable||e+1>a)continue;
            const auto state=g.group.state();if(!std::holds_alternative<cambc::map::camp_group_respawning>(state))continue;
            if(rw_enemy_near_region(g,enemies))continue;
            bool occupied=false;for(const auto& ally:allies)if(ally!=me&&g.spawns.is_set(ally.position()))occupied=true;
            if(occupied)continue;
            int landing=-1,safe_exit=-1,travel=unreachable;bool group_complete=true;
            const auto fresh_blocked=blocked()|avoid;
            for(auto p:g.spawns.positions()){
                if(cambc::fuel_remaining()<45000){group_complete=false;break;}
                if(fresh_blocked.is_set(p)&&p!=me.position())continue;
                const int proposed_travel=origin_field_->get(p);
                if(proposed_travel==unreachable||!reservoir::due_window(now,o.due,proposed_travel))continue;
                const int ex=rw_safe_exit(static_cast<int>(i),tile_index(p),avoid);
                if(ex>=0&&(landing<0||tile_index(p)<landing)){landing=tile_index(p);safe_exit=ex;travel=proposed_travel;}
            }
            // A cut-off multi-spawn comparison cannot establish its canonical
            // landing. Retain only an earlier fully completed group decision.
            if(!group_complete)break;
            if(landing<0)continue;
            if(selected<0||g.group.gem_value()>best_gems||(g.group.gem_value()==best_gems&&(travel<best_travel||(travel==best_travel&&g.id<groups_[selected].id)))){
                selected=static_cast<int>(i);spawn=landing;exit=safe_exit;best_gems=g.group.gem_value();best_travel=travel;
            }
        }
        if(selected<0)return false;
        rw_group_=selected;rw_spawn_=spawn;rw_exit_=exit;rw_due_=rw_timers_[selected].due;rw_started_=now;
        rw_first_occupied_=rw_last_occupied_=rw_eligible_=rw_early_=rw_travel_=0;rw_witnessed_=false;rw_state_=Denial::approach_due;
        telemetry_.event("opportunity","timer_due",rw_fields()+","+integer("observedRound",rw_timers_[selected].round)
            +","+integer("observedTimer",rw_timers_[selected].timer)+","+integer("travelPrediction",best_travel));
    }
    auto& g=groups_[rw_group_];
    if(!reservoir::enabled_by_score(rw_lead(),me.gem_count())){rw_end("lead_or_bag");return true;}
    if(!seen(g)){rw_end("visibility_unknown");return true;}
    const auto state=g.group.state();const auto* respawn=std::get_if<cambc::map::camp_group_respawning>(&state);
    if(!respawn){rw_end("already_respawned_or_reward");return true;}
    if(rw_enemy_near_region(g,enemies)){rw_end("enemy_within_eight");return true;}
    for(const auto& ally:allies)if(ally!=me&&g.spawns.is_set(ally.position())){rw_end("other_ally_blocker");return true;}
    if(avoid.is_set(me.position())){rw_end("unsafe_arrival_or_hold");rw_step_exit(avoid);return true;}
    if(rw_state_==Denial::approach_due){
        if(!g.spawns.is_set(me.position())){
            if(now>=rw_due_){rw_end("missed_predicted_due");return true;}
            if(now-rw_started_>=reservoir::maximum_approach){rw_end("approach_cap");return true;}
            ++rw_travel_;++rw_total_travel_;ordinary_move(around(tile_position(rw_spawn_),0),&avoid);
            if(!g.spawns.is_set(me.position()))return true;
        }
        // We just entered using fresh visibility, occupancy and threat masks.
        rw_first_occupied_=now;rw_last_occupied_=now;rw_state_=Denial::suppress;
        telemetry_.event("activation","suppression_entry",rw_fields()+","+integer("tile",tile_index(me.position()))
            +","+integer("eligibilityStart",reservoir::eligibility_start(rw_due_,rw_first_occupied_)));
        if constexpr(profile::probe_mode)rw_probe_phase_=3;
    }
    if(!g.spawns.is_set(me.position())){rw_end("lost_occupancy");return true;}
    if(!profile::core_enabled&&now+1>=rw_due_){
        rw_end("disabled_exit_before_eligible_phase");rw_step_exit(avoid);return true;
    }
    const unsigned start=reservoir::eligibility_start(rw_due_,rw_first_occupied_);
    if(now<start){++rw_early_;++rw_total_early_;}
    if(reservoir::eligible_phase(now,rw_last_occupied_,start,respawn->timer)){
        ++rw_eligible_;++rw_total_eligible_;
        if(!rw_witnessed_){rw_witnessed_=true;telemetry_.event("mechanism_success","eligible_occupancy_witness",rw_fields()+","+integer("observedTimer",respawn->timer)+",\"pairedRespawnComparisonRequired\":true,\"denialUtilityProven\":false",true);}
    }
    rw_last_occupied_=now;
    if(rw_eligible_>=reservoir::maximum_hold||reservoir::elapsed_hold_limit(now,start)){
        rw_end("eligible_hold_limit");rw_step_exit(avoid);
    }
    return true;
}
bool rw_probe(const std::vector<camp>& camps,const std::vector<champion>& enemies,const std::vector<champion>& allies){
    auto& me=cambc::self();if(rw_probe_done_)return true;
    if(rw_actor()){
        const int index=find_group(rw_probe_id());if(index<0)return true;
        if(rw_probe_phase_==0){
            // Approach the untouched target before funding completes; only
            // damage/capture waits for the public score and round marker.
            const auto staging=tile_position(rw_probe_exit());
            if(rw_probe_captures_==0&&cambc::round_number()<profile::probe_earliest_clear&&me.position()!=staging){
                const auto avoid=danger(camps,enemies);ordinary_move(around(staging,0),&avoid);return true;
            }
            if(cambc::round_number()<profile::probe_earliest_clear||rw_lead()<reservoir::lead_required)return true;
            return farm_group(index,camps);
        }
        if(rw_probe_phase_==1)return true; // forced bank handled before core turn
        if(rw_probe_phase_==2){
            const auto exit=tile_position(rw_probe_exit());
            if(me.position()!=exit){const auto avoid=danger(camps,enemies);ordinary_move(around(exit,0),&avoid);return true;}
        }
        rw_denial(camps,enemies,allies,index);return true;
    }
    if(is_actor(cambc::champion_type::tavi)){
        if(rw_probe_captures_>=2)return true;
        const int id=rw_side_a()?(rw_probe_captures_?384:215):(rw_probe_captures_?1286:1445);
        return farm_group(find_group(id),camps);
    }
    if(is_actor(cambc::champion_type::vendetta)){
        if(rw_probe_captures_)return true;
        return farm_group(find_group(rw_side_a()?552:1126),camps);
    }
    return true;
}
bool core_turn(const std::vector<camp>& camps,const std::vector<champion>& enemies,const std::vector<champion>& allies){
    if constexpr(profile::probe_mode)return rw_probe(camps,enemies,allies);
    return rw_actor()&&rw_denial(camps,enemies,allies);
}
