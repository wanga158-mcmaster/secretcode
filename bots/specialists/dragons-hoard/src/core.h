// Actor-local inventory controller. Included inside Bot after ordinary B0 helpers.
struct HoardAltar { int index=-1,travel=unreachable; unsigned lock_hits=0; };
bool hoard_pending_=false,hoard_observed_=false,hoard_previous_submit_=false;
bool hoard_previous_alive_=true;
unsigned hoard_bag_=0,hoard_score_=0,hoard_first_bag_round_=0;
float hoard_focus_=0,hoard_raw_=0;
#if DRAGONS_CAPITAL_ROUTE
std::vector<hoard::PublicMonolith> hoard_public_monoliths_;
std::optional<bitboard> hoard_monolith_mask_;
std::size_t hoard_monolith_cursor_=0;
#endif
#if DRAGONS_LOADED_ESCAPE
float hoard_previous_health_=0,hoard_recent_damage_=0,hoard_current_health_=0,hoard_current_maximum_=0;
unsigned hoard_previous_health_turn_=0;
bool hoard_health_observed_=false;
vec2 hoard_escape_origin_{};
std::vector<hoard::EscapeContact> hoard_escape_contacts_;
#if DRAGONS_CAPITAL_RECOVERY
hoard::CapitalRecovery hoard_capital_recovery_;
#endif
#endif
unsigned hoard_first_bank_=0,hoard_last_blocked_log_=0;
std::vector<unsigned> hoard_altar_blacklist_;
HoardAltar hoard_selected_;
unsigned hoard_plan_round_=0;
bool hoard_plan_complete_=false;
bool probe_initialized_=false,probe_valid_=false,probe_done_=false;
bool probe_cast_done_=false,probe_timeout_=false;
unsigned probe_captures_=0;
int probe_active_group_=-1,probe_cast_index_=-1,probe_cast_tile_=-1;
std::array<int,6> probe_circuit_{};

bool holding_prism() const {
    return is_actor(cambc::champion_type::prism) && profile::core_enabled && !profile::draft_only;
}
void core_shop() {}
void probe_initialize() {
    probe_initialized_=true;
    if(cambc::map::width()!=31 || cambc::map::height()!=31) return;
    const auto own=cambc::map::spawn_region(team::ally).positions();
    const auto other=cambc::map::spawn_region(team::enemy).positions();
    if(own.empty() || other.empty()) return;
    const bool mirror=own.front().x>other.front().x;
    const auto reflect=[&](int tile) {const auto p=tile_position(tile);return tile_index({mirror?30-p.x:p.x,p.y});};
    for(std::size_t i=0;i<probe_circuit_.size();++i) {
        const int index=find_group(reflect(profile::probe_circuit[i]));
        if(index<0 || groups_[index].group.gem_value()!=24 || groups_[index].incoming!=0 || groups_[index].group.camps().size()!=1) return;
        if(!groups_[index].group.camps().front().is_gem_cluster()) return;
        probe_circuit_[i]=index;
    }
    probe_cast_index_=find_group(reflect(profile::probe_cast_group));
    probe_cast_tile_=reflect(profile::probe_cast_tile);
    if(probe_cast_index_<0 || groups_[probe_cast_index_].incoming!=0 || groups_[probe_cast_index_].health<400) return;
    probe_valid_=true;
}

void core_observe() {
    if(!is_actor(cambc::champion_type::prism)) return;
    auto& me=cambc::self();
    const unsigned round=cambc::round_number(),bag=me.gem_count(),score=cambc::global_submitted_gems(team::ally);
    const bool alive=me.is_alive(),submitting=me.is_submitting_gems();
    const float live_focus=me.focus(),live_raw=me.abilities()[0].damage_amount();
    if(profile::probe_mode && !probe_initialized_) probe_initialize();
    if(!hoard_observed_) {
        hoard_observed_=true;hoard_bag_=bag;hoard_focus_=live_focus;hoard_raw_=live_raw;
        hoard_score_=score;hoard_previous_alive_=alive;hoard_previous_submit_=submitting;
        hoard_altar_blacklist_.assign(altars_.size(),0);
    }
    if(bag!=hoard_bag_) {
        const float focus=live_focus,raw=live_raw;
        const float expected=(static_cast<float>(bag)-static_cast<float>(hoard_bag_))*cambc::constants::passive_abilities::prism_focus_per_gem;
        const float residual=(focus-hoard_focus_)-expected;
        const bool conversion=alive && hoard_previous_submit_ && !submitting && bag==0 && score>hoard_score_;
        if(bag>hoard_bag_ && !hoard_first_bag_round_) hoard_first_bag_round_=round;
        if(profile::probe_mode && probe_valid_ && bag>hoard_bag_ && probe_active_group_>=0) {
            const auto& g=groups_[probe_active_group_];const auto visible=cambc::vision::visible_tiles();
            if(g.spawns.is_set(me.position()) && !(g.spawns&visible).is_empty()
                && std::holds_alternative<cambc::map::camp_group_respawning>(g.group.state()) && bag-hoard_bag_==24) {
                ++probe_captures_;probe_active_group_=-1;target_=-1;
            } else {
                probe_done_=true;
                telemetry_.event("mechanism_failed","hoard_unattributed_reward",integer("bag",bag),true);
            }
        }
        telemetry_.event("observation","hoard_bag_change",integer("bagBefore",hoard_bag_)+","+integer("bag",bag)
            +","+number("focusBefore",hoard_focus_)+","+number("focus",focus)+","+number("rawBefore",hoard_raw_)+","+number("rawDamage",raw)
            +","+number("expectedFocusDelta",expected)+","+number("focusResidual",residual)+","+integer("captures",probe_captures_)
            +",\"converted\":"+(conversion?"true":"false")+","+integer("publicScoreDelta",score>=hoard_score_?score-hoard_score_:0)
            +","+integer("firstBank",hoard_first_bank_),conversion);
        if(conversion) {
            hoard_pending_=false;
            if(profile::probe_mode && !probe_done_ && probe_cast_done_ && probe_captures_==6) {
                const bool observed=holding_prism()?hoard_bag_>=profile::bank_threshold:hoard_bag_==24;
                telemetry_.event(observed?"mechanism_success":"mechanism_failed",holding_prism()?"hoard_144_converted":"hoard_control_six_banks",
                    integer("observedConvertedBag",hoard_bag_)+","+integer("publicScore",score)
                    +","+integer("exposureDuration",hoard_first_bag_round_?round-hoard_first_bag_round_:0),true);
                probe_done_=true;
            }
            hoard_first_bag_round_=0;
        }
        hoard_bag_=bag;hoard_focus_=focus;hoard_raw_=raw;
    }
    if(hoard_previous_alive_ && !alive) {
        telemetry_.event("observation","hoard_death",integer("previousBag",previous_bag_),true);
        hoard_pending_=false;hoard_first_bag_round_=0;if(profile::probe_mode) probe_done_=true;
    }
    hoard_score_=score;hoard_previous_alive_=alive;hoard_previous_submit_=submitting;
    // Immediately preceding snapshots distinguish inventory effects from any
    // unrelated stat change. No bag change is inferred from allied vision.
    if(alive && bag==hoard_bag_) {hoard_focus_=live_focus;hoard_raw_=live_raw;}
    if(profile::probe_mode && !probe_done_ && !probe_timeout_ && round>profile::max_setup_rounds && !probe_cast_done_) {
        probe_timeout_=true;probe_done_=true;
        telemetry_.event("not_exercised","hoard_setup_or_conversion_cap",integer("captures",probe_captures_)+","+integer("bag",bag)
            +",\"castObserved\":"+(probe_cast_done_?"true":"false"),true);
    }
    if(profile::probe_mode && !probe_done_ && !probe_timeout_ && round>=profile::probe_round_limit) {
        probe_timeout_=true;probe_done_=true;
        telemetry_.event("not_exercised","hoard_conversion_untested_at_cap",integer("captures",probe_captures_)+","+integer("bag",bag),true);
    }
}

HoardAltar plan_hoard_altar() {
    const unsigned round=cambc::round_number();
    if(hoard_plan_round_==round) return hoard_selected_;
    hoard_selected_={};hoard_plan_round_=round;hoard_plan_complete_=false;
    if(!ensure_origin()) return hoard_selected_;
    hoard_plan_complete_=true;
    const auto visible=cambc::vision::visible_tiles();const auto locks=cambc::vision::altar_locks();
    for(std::size_t i=0;i<altars_.size();++i) {
        if(i<hoard_altar_blacklist_.size() && hoard_altar_blacklist_[i]>round) continue;
#if DRAGONS_ACTION_RESERVE
        // This is still the complete altar set. An incomplete scan authorizes
        // no destination and resumes from a fresh frame next owner turn.
        if(cambc::fuel_remaining()<30000){hoard_plan_complete_=false;hoard_selected_={};return {};}
        int travel=unreachable;
        const int radius=cambc::constants::submit_gems_range;
        for(int y=std::max(0,altars_[i].y-radius);y<=std::min(int(cambc::map::height())-1,altars_[i].y+radius);++y)
            for(int x=std::max(0,altars_[i].x-radius);x<=std::min(int(cambc::map::width())-1,altars_[i].x+radius);++x)
                if(!terrain_.is_set({x,y}))travel=std::min(travel,int(origin_field_->get({x,y})));
#else
        const auto goals=around(altars_[i],cambc::constants::submit_gems_range)&~terrain_;
        const int travel=min_distance(*origin_field_,goals);
#endif
        if(travel==unreachable || (hoard_selected_.index>=0 && travel>=hoard_selected_.travel)) continue;
        unsigned allowance=visible.is_set(altars_[i])?0:cambc::constants::altar_lock_hits;
        for(const auto& lock:locks) if(lock.position()==altars_[i]) allowance=lock.is_enemy()?lock.hits_remaining():0;
        hoard_selected_={static_cast<int>(i),travel,allowance};
    }
    return hoard_selected_;
}
bool core_bank_request(int nearest) {
    if(!holding_prism()) return base_bank_request(nearest);
    auto& me=cambc::self();const unsigned bag=me.gem_count();
    if(!bag) {hoard_pending_=false;return false;}
    if(hoard_pending_) return true;
#if DRAGONS_ACTION_RESERVE
    // ensure_origin accepts a complete field only below its 256-expansion
    // cap. Every reachable altar approach is therefore within this conservative
    // bound. Skip an expensive scan only when even that route cannot make the
    // unchanged projected deadline due; winning/threshold/hard exits still win.
    const unsigned round=cambc::round_number(),maximum=cambc::constants::max_rounds;
    const unsigned duration=static_cast<unsigned>(channel_duration(bag));
#if DRAGONS_LOADED_RETURNABILITY
    if(!hoard::projection_required(bag,round,maximum,cambc::global_submitted_gems(team::ally),
        cambc::constants::winning_gems,duration,cambc::constants::altar_lock_hits,
        profile::bank_threshold,profile::deadline_reserve,profile::projection_reserve))return false;
#endif
    const auto immediate=hoard::request(true,bag,round,maximum,
        cambc::global_submitted_gems(team::ally),cambc::constants::winning_gems,nearest,false,0,duration,0,
        profile::bank_threshold,profile::deadline_reserve,profile::projection_reserve);
    if(immediate==hoard::Reason::none&&origin_field_&&origin_field_position_==me.position()&&
        !hoard::projection_due(round,maximum,true,256,duration,cambc::constants::altar_lock_hits,profile::projection_reserve))return false;
#endif
    const auto selected=plan_hoard_altar();
    const auto reason=hoard::request(true,bag,cambc::round_number(),cambc::constants::max_rounds,
        cambc::global_submitted_gems(team::ally),cambc::constants::winning_gems,nearest,selected.index>=0,
        selected.travel==unreachable?0:static_cast<unsigned>(selected.travel),static_cast<unsigned>(channel_duration(bag)),selected.lock_hits,
        profile::bank_threshold,profile::deadline_reserve,profile::projection_reserve);
    if(reason==hoard::Reason::none) return false;
    hoard_pending_=true;
    telemetry_.event("activation","hoard_convert_requested",integer("bag",bag)+",\"reason\":\""+hoard::name(reason)
        +"\",\"reachable\":"+(selected.index>=0?"true":"false")+","+integer("staticTravel",selected.index>=0?selected.travel:-1)
        +","+integer("channelDuration",channel_duration(bag))+","+integer("lockAllowance",selected.lock_hits)
        +",\"terrainSearchComplete\":"+(hoard_plan_complete_?"true":"false"));
    return true;
}
bool hoard_bank() {
    auto& me=cambc::self();const unsigned round=cambc::round_number();const auto selected=plan_hoard_altar();
    if(!hoard_plan_complete_) return true;
    if(selected.index<0) {
        if(!hoard_last_blocked_log_ || round-hoard_last_blocked_log_>=8) {
            telemetry_.event("observation","conversion_blocked",integer("bag",me.gem_count())+",\"reachable\":false",true);
            hoard_last_blocked_log_=round;
        }
        if(!cambc::map::spawn_region(team::ally).is_set(me.position()))
            recover(cambc::vision::camps(),cambc::vision::champions(team::enemy));
        return true;
    }
    const auto altar=altars_[selected.index];
    if(distance(me.position(),altar)<=cambc::constants::submit_gems_range) {
        for(const auto& lock:cambc::vision::altar_locks(team::enemy)) if(lock.position()==altar) {
            if(me.can_attack() && me.attack_tiles().is_set(altar)) me.attack(lock);return true;
        }
        const unsigned bag=me.gem_count();me.submit_gems(altar);previous_submit_=true;hoard_previous_submit_=true;
        if(!hoard_first_bank_) hoard_first_bank_=round;
        telemetry_.event("action_result","tribute_start",integer("bag",bag)+","+integer("altarTile",tile_index(altar))
            +","+integer("channelDuration",channel_duration(bag))+","+integer("firstBank",hoard_first_bank_),true);return true;
    }
    if(cambc::fuel_remaining()<30000 || fields_>=2 || !me.can_move()) return true;
    const bool moved=ordinary_move(around(altar,cambc::constants::submit_gems_range)&~terrain_);
    if(!moved && cambc::fuel_remaining()>=16000) {
        hoard_altar_blacklist_[selected.index]=round+profile::altar_retry_rounds;
        telemetry_.event("observation","hoard_route_failure",integer("altarTile",tile_index(altar))
            +","+integer("retryAfter",round+profile::altar_retry_rounds)+",\"pending\":true",true);
    }
    return true;
}

bool core_move(bitboard,const bitboard*) {return false;}
bool core_group_allowed(int,float) {return true;}
bool core_partial_allowed() const {return true;}
bool core_fork(const std::vector<Choice>&) {return false;}
void core_selected(int) {}
bool core_retreat(const std::vector<champion>&) const {return false;}
void core_recovery() {}
bool core_departing(int) const {return false;}
bool core_turn(const std::vector<camp>& camps,const std::vector<champion>&,const std::vector<champion>&) {
    if(!profile::probe_mode) return false;
    if(!is_actor(cambc::champion_type::prism)) return true;
    if(!probe_valid_ || probe_done_) return true;
    auto& me=cambc::self();
    if(probe_captures_>=profile::probe_capture_count_before_cast && !probe_cast_done_) {
        const auto cast_tile=tile_position(probe_cast_tile_);
        if(me.position()!=cast_tile) {ordinary_move(around(cast_tile,0));return true;}
        const auto ability=me.abilities()[0];if(!ability.is_ready()) return true;
        std::optional<camp> target;
        for(const auto& m:camps) if(groups_[probe_cast_index_].spawns.is_set(m.spawn_position())) {target=m;break;}
        if(!target || !target->is_visible() || !target->is_alive()) return true;
        const float health=target->health(),raw=ability.damage_amount(),focus=me.focus();const unsigned bag=me.gem_count();
        const auto inventory=me.item_count();bool empty=true;
        for(unsigned i=0;i<static_cast<unsigned>(item_type::count);++i) if(inventory[static_cast<item_type>(i)]) empty=false;
        if(health<=raw || !empty || !ability.can_target_camps() || !ability.valid_tiles().is_set(target->position())) {
            telemetry_.event("not_exercised","hoard_cast_predicate_failed",number("healthBefore",health)+","+number("rawDamage",raw),true);
            probe_done_=true;return true;
        }
        telemetry_.event("opportunity","hoard_non_overkill_cast",integer("bag",bag)+","+number("focus",focus)
            +","+number("rawDamage",raw)+","+number("healthBefore",health)+","+integer("captures",probe_captures_));
        const bool used=ability.use(*target),visible=target->is_visible();const float after=visible?target->health():health;
        const bool focus_exact=std::abs(focus-me.base_stats().base_focus-static_cast<float>(bag)*cambc::constants::passive_abilities::prism_focus_per_gem)<0.01f;
        const bool observed=used && visible && health>raw && std::abs((health-after)-raw)<0.01f && focus_exact;
        telemetry_.event(observed?"mechanism_success":"mechanism_failed","hoard_inventory_power",integer("bag",bag)+","+number("focus",focus)
            +","+number("rawDamage",raw)+","+number("healthBefore",health)+","+(visible?number("healthAfter",after):"\"healthAfter\":null")
            +",\"usefulCast\":"+(observed?"true":"false")+",\"focusMatchesBag\":"+(focus_exact?"true":"false"),true);
        probe_cast_done_=true;if(!observed) probe_done_=true;return true;
    }
    if(probe_captures_>=probe_circuit_.size()) return true;
    probe_active_group_=probe_circuit_[probe_captures_];farm_group(probe_active_group_,camps);return true;
}
