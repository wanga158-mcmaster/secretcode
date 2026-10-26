#pragma once
#include "policy.h"
#include <cstring>

// Query-only exact reuse across owner turns. A complete catalogue is published
// only while every physical input and every ranking window still matches. Time
// may only advance, and all successful time tests must remain above their saved
// floors. Crossing any boundary starts a new catalogue; no partial best escapes.
namespace p5::runoff {
enum class PreparationStatus {incomplete,complete,invalid};
struct PreparationDomain {int position=-1;std::uint64_t geometry=0;};
// The first rejected compatibility check, in evaluation order. Later inputs
// may differ too; this trace never adds a second comparison or quote pass.
enum class PreparationCause {
    none,inactive,profile,shortcut_mode,domain_position,domain_geometry,
    time_round_before_frozen,time_round_before_last,time_game_increase,time_official_increase,
    horizon_game,horizon_official,window_values,window_endpoint,physical,invalid,shortcut
};
enum class PreparationPhysical {
    none,team_score,actual_marcella,bank_safe,spawn_safe,
    resource_id,resource_self,resource_visible,resource_own_team,resource_bag,resource_gold,
    actor,settings,ledger,A_to_B,current_to_altar,current_to_spawn,altar_to_spawn,
    spawn_rest,spawn_heal,purchase_count,
    group_id,group_gems,group_gold,group_current,group_available,group_no_other_holder,group_count,
    group_from_current,group_from_altar,group_from_spawn,group_to_altar,group_to_spawn,group_certificate_return,
    group_monster,purchase
};
struct PreparationTrace {
    PreparationCause cause=PreparationCause::none;
    PreparationPhysical physical=PreparationPhysical::none;
    int index=-1,detail=-1; // group/purchase index; monster index for group_monster
    PreparationStatus before_status=PreparationStatus::invalid,after_status=PreparationStatus::invalid;
    int before_cursor=0,after_cursor=0,before_count=0,after_count=0;
    unsigned before_restarts=0,after_restarts=0;
    bool began=false,quoted=false;
};
namespace preparation_detail {
inline bool mismatch(PreparationTrace* trace,PreparationCause cause){
    if(trace)trace->cause=cause;return false;
}
inline bool physical_mismatch(PreparationTrace* trace,PreparationPhysical physical,int index=-1,int detail=-1){
    if(trace){trace->cause=PreparationCause::physical;trace->physical=physical;trace->index=index;trace->detail=detail;}return false;
}
inline bool same(const double& a,const double& b){return std::memcmp(&a,&b,sizeof(double))==0;}
inline bool same(const RouteLeg& a,const RouteLeg& b){
    return a.destination==b.destination && a.distance==b.distance &&
        a.terrain_and_diagonals_checked==b.terrain_and_diagonals_checked && a.safety_checked==b.safety_checked;
}
inline bool same(const CombatActor& a,const CombatActor& b){
    // Spell/heal fields are never read by this basic-only quote core.
    return a.id==b.id && a.self==b.self && a.visible==b.visible && a.ranged==b.ranged &&
        a.enemy_cast_bound_known==b.enemy_cast_bound_known && same(a.health,b.health) && same(a.max_health,b.max_health) &&
        same(a.strength,b.strength) && same(a.basic_modifier,b.basic_modifier) && same(a.shield,b.shield) &&
        a.shield_expiry_tick==b.shield_expiry_tick && same(a.visible_enemy_next_phase,b.visible_enemy_next_phase);
}
inline bool same(const CombatMonster& a,const CombatMonster& b){
    return a.id==b.id && a.visible==b.visible && same(a.public_health,b.public_health) &&
        same(a.observed_health,b.observed_health) && same(a.effective_strength,b.effective_strength);
}
inline bool same(const Group& a,const Group& b,PreparationTrace* trace=nullptr,int index=-1){
    if(a.id!=b.id)return physical_mismatch(trace,PreparationPhysical::group_id,index);
    if(a.gems!=b.gems)return physical_mismatch(trace,PreparationPhysical::group_gems,index);
    if(a.gold!=b.gold)return physical_mismatch(trace,PreparationPhysical::group_gold,index);
    if(a.current!=b.current)return physical_mismatch(trace,PreparationPhysical::group_current,index);
    if(a.available!=b.available)return physical_mismatch(trace,PreparationPhysical::group_available,index);
    if(a.no_other_holder!=b.no_other_holder)return physical_mismatch(trace,PreparationPhysical::group_no_other_holder,index);
    if(a.count!=b.count)return physical_mismatch(trace,PreparationPhysical::group_count,index);
    if(!same(a.from_current,b.from_current))return physical_mismatch(trace,PreparationPhysical::group_from_current,index);
    if(!same(a.from_altar,b.from_altar))return physical_mismatch(trace,PreparationPhysical::group_from_altar,index);
    if(!same(a.from_spawn,b.from_spawn))return physical_mismatch(trace,PreparationPhysical::group_from_spawn,index);
    if(!same(a.to_altar,b.to_altar))return physical_mismatch(trace,PreparationPhysical::group_to_altar,index);
    if(!same(a.to_spawn,b.to_spawn))return physical_mismatch(trace,PreparationPhysical::group_to_spawn,index);
    if(!same(a.certificate_return,b.certificate_return))return physical_mismatch(trace,PreparationPhysical::group_certificate_return,index);
    for(std::size_t i=0;i<a.count;++i){
        if(!same(a.monsters[i],b.monsters[i]))return physical_mismatch(trace,PreparationPhysical::group_monster,index,int(i));
    }
    return true;
}
inline bool same(const Purchase& a,const Purchase& b){
    return a.item==b.item && a.family==b.family && a.cost==b.cost && a.shards==b.shards &&
        same(a.strength,b.strength) && same(a.health,b.health) && same(a.spawn_heal,b.spawn_heal) &&
        same(a.basic_ratio,b.basic_ratio) && a.legal_on_spawn==b.legal_on_spawn && a.no_new_income_passive==b.no_new_income_passive;
}
inline bool same(const Ledger& a,const Ledger& b){
    return a.captures==b.captures && a.banks==b.banks && a.purchases==b.purchases && a.consumed==b.consumed && a.liquidated==b.liquidated;
}
inline bool same(const EconomicSettings& a,const EconomicSettings& b){
    return a.capture_rounds==b.capture_rounds && a.tribute_gems_per_tick==b.tribute_gems_per_tick &&
        a.tribute_min_ticks==b.tribute_min_ticks && a.round_limit==b.round_limit &&
        a.winning_gems==b.winning_gems && a.marcella_gold_per_gem==b.marcella_gold_per_gem;
}
inline bool physical(const Observation& a,const Observation& b,PreparationTrace* trace=nullptr){
    // valid_input() proves both private values have current owner-turn stamps;
    // their old stamp numbers are not simulation inputs.
    if(a.team_score!=b.team_score)return physical_mismatch(trace,PreparationPhysical::team_score);
    if(a.actual_marcella!=b.actual_marcella)return physical_mismatch(trace,PreparationPhysical::actual_marcella);
    if(a.bank_safe!=b.bank_safe)return physical_mismatch(trace,PreparationPhysical::bank_safe);
    if(a.spawn_safe!=b.spawn_safe)return physical_mismatch(trace,PreparationPhysical::spawn_safe);
    if(a.resources.id!=b.resources.id)return physical_mismatch(trace,PreparationPhysical::resource_id);
    if(a.resources.self!=b.resources.self)return physical_mismatch(trace,PreparationPhysical::resource_self);
    if(a.resources.visible!=b.resources.visible)return physical_mismatch(trace,PreparationPhysical::resource_visible);
    if(a.resources.own_team!=b.resources.own_team)return physical_mismatch(trace,PreparationPhysical::resource_own_team);
    if(a.resources.bag.value!=b.resources.bag.value)return physical_mismatch(trace,PreparationPhysical::resource_bag);
    if(a.resources.gold.value!=b.resources.gold.value)return physical_mismatch(trace,PreparationPhysical::resource_gold);
    if(!same(a.actor,b.actor))return physical_mismatch(trace,PreparationPhysical::actor);
    if(!same(a.settings,b.settings))return physical_mismatch(trace,PreparationPhysical::settings);
    if(!same(a.ledger,b.ledger))return physical_mismatch(trace,PreparationPhysical::ledger);
    if(!same(a.A_to_B,b.A_to_B))return physical_mismatch(trace,PreparationPhysical::A_to_B);
    if(!same(a.current_to_altar,b.current_to_altar))return physical_mismatch(trace,PreparationPhysical::current_to_altar);
    if(!same(a.current_to_spawn,b.current_to_spawn))return physical_mismatch(trace,PreparationPhysical::current_to_spawn);
    if(!same(a.altar_to_spawn,b.altar_to_spawn))return physical_mismatch(trace,PreparationPhysical::altar_to_spawn);
    if(a.spawn_rest!=b.spawn_rest)return physical_mismatch(trace,PreparationPhysical::spawn_rest);
    if(!same(a.spawn_heal_per_tick,b.spawn_heal_per_tick))return physical_mismatch(trace,PreparationPhysical::spawn_heal);
    if(a.purchase_count!=b.purchase_count)return physical_mismatch(trace,PreparationPhysical::purchase_count);
    for(int i=0;i<group_cap;++i)if(!same(a.groups[i],b.groups[i],trace,i))return false;
    for(int i=0;i<a.purchase_count;++i){
        if(!same(a.purchases[i],b.purchases[i]))return physical_mismatch(trace,PreparationPhysical::purchase,i);
    }
    return true;
}
inline bool windows_valid(const FinishWindows& w){
    if(!w.valid || w.official_remaining<0)return false;
    for(int i=0;i<3;++i)if(w.window[i]<0 || w.window[i]>120 || w.window[i]>w.deadline[i] || w.deadline[i]>w.official_remaining)return false;
    return true;
}
inline bool endpoint(const FinishWindows& w){
    return w.window[0]==w.official_remaining && w.window[1]==w.official_remaining && w.window[2]==w.official_remaining;
}
inline void canonicalize(Quote& q){
    if(!q.feasible)return;std::fill(q.steps.begin()+q.count,q.steps.end(),Step{});
    std::fill(q.schedule.events.begin()+q.schedule.event_count,q.schedule.events.end(),AccountingEvent{});
}
} // namespace preparation_detail

struct Preparation {
    PreparationStatus status=PreparationStatus::invalid;
    int cursor=0;
    unsigned restarts=0;
    HorizonProof proof{};
private:
    struct Candidate {Profile profile{};int prefix=0,purchase=-1,before=0;bool control=false;};
    Observation frozen_{};Profile profile_{};PreparationDomain domain_{};
    std::array<Candidate,24> catalogue_{};int count_=0;
    Quote control_{},selected_{},scratch_{};Decision shortcut_{};
    bool active_=false,shortcut_mode_=false;
    int last_round_=-1,last_game_=0,last_official_=0;
    bool shortcut(const Observation& fresh,Profile profile,PreparationDomain domain){
        if(fresh.execution.channel==execution::Channel::none && !detail::winning_bank_candidate(fresh))return false;
        shortcut_={};shortcut_.input=fresh;shortcut_.profile=profile;
        if(!detail::shortcut_into(fresh,shortcut_))return false;
        if(active_ && !shortcut_mode_)++restarts;
        active_=shortcut_mode_=true;profile_=profile;domain_=domain;cursor=count_=0;proof={};status=PreparationStatus::complete;return true;
    }
    bool valid(const Observation& fresh)const{
        return valid_input(fresh) && preparation_detail::windows_valid(fresh.windows);
    }
    bool compatible(const Observation& fresh,PreparationDomain domain,PreparationTrace* trace=nullptr)const{
        const int game=fresh.settings.round_limit-fresh.round;
        if(!active_)return preparation_detail::mismatch(trace,PreparationCause::inactive);
        if(shortcut_mode_)return preparation_detail::mismatch(trace,PreparationCause::shortcut_mode);
        if(domain.position!=domain_.position)return preparation_detail::mismatch(trace,PreparationCause::domain_position);
        if(domain.geometry!=domain_.geometry)return preparation_detail::mismatch(trace,PreparationCause::domain_geometry);
        if(fresh.round<frozen_.round)return preparation_detail::mismatch(trace,PreparationCause::time_round_before_frozen);
        if(fresh.round<last_round_)return preparation_detail::mismatch(trace,PreparationCause::time_round_before_last);
        if(game>last_game_)return preparation_detail::mismatch(trace,PreparationCause::time_game_increase);
        if(fresh.windows.official_remaining>last_official_)return preparation_detail::mismatch(trace,PreparationCause::time_official_increase);
        if(game<proof.game_floor)return preparation_detail::mismatch(trace,PreparationCause::horizon_game);
        if(fresh.windows.official_remaining<proof.official_floor)return preparation_detail::mismatch(trace,PreparationCause::horizon_official);
        if(fresh.windows.window!=frozen_.windows.window)return preparation_detail::mismatch(trace,PreparationCause::window_values);
        if(preparation_detail::endpoint(fresh.windows)!=preparation_detail::endpoint(frozen_.windows))
            return preparation_detail::mismatch(trace,PreparationCause::window_endpoint);
        return preparation_detail::physical(fresh,frozen_,trace);
    }
    PreparationStatus traced_status(PreparationTrace* trace)const{
        if(trace){trace->after_status=status;trace->after_cursor=cursor;trace->after_count=count_;trace->after_restarts=restarts;}
        return status;
    }
    void accept_time(const Observation& fresh){
        last_round_=fresh.round;last_game_=fresh.settings.round_limit-fresh.round;last_official_=fresh.windows.official_remaining;
    }
    void add_arm(Profile profile,bool control){
        int remaining=0;for(int i=0;i<segment_cap;++i)remaining+=!(frozen_.ledger.consumed&(1u<<i));
        const int limit=profile.arm==Arm::finite_harvest && frozen_.ledger.liquidated?0:remaining;
        // Original catalogue order, including impossible purchases. Evaluating
        // these invalid quotes retains the exhaustive selector's strict ties.
        for(int prefix=0;prefix<=limit;++prefix){
            catalogue_[count_++]={profile,prefix,-1,0,control};
            if(!frozen_.ledger.purchases)for(int pi=0;pi<frozen_.purchase_count;++pi)for(int n=0;n<prefix;++n)
                catalogue_[count_++]={profile,prefix,pi,n,control};
        }
    }
    PreparationStatus begin(const Observation& fresh,Profile profile,PreparationDomain domain){
        if(active_)++restarts;active_=true;shortcut_mode_=false;frozen_=fresh;profile_=profile;domain_=domain;
        cursor=count_=0;proof={};control_={};selected_={};scratch_={};accept_time(fresh);
        add_arm({Arm::bank_each_control},true);if(profile.arm!=Arm::bank_each_control)add_arm(profile,false);
        status=PreparationStatus::incomplete;return status;
    }
public:
    void reset(){active_=shortcut_mode_=false;cursor=count_=0;proof={};status=PreparationStatus::invalid;}
    // Beginning/restarting is its own unit. A later call performs at most one
    // quote, always against the same immutable frozen observation.
    PreparationStatus advance(const Observation& fresh,Profile profile={},PreparationDomain domain={},PreparationTrace* trace=nullptr){
        if(trace){*trace={};trace->before_status=status;trace->before_cursor=cursor;trace->before_count=count_;trace->before_restarts=restarts;}
        if(shortcut(fresh,profile,domain)){if(trace)trace->cause=PreparationCause::shortcut;return traced_status(trace);}
        if(!valid(fresh)){if(trace)trace->cause=PreparationCause::invalid;reset();return traced_status(trace);}
        const bool profile_changed=profile.arm!=profile_.arm;
        if(profile_changed || !compatible(fresh,domain,trace)){
            if(trace){if(profile_changed)trace->cause=PreparationCause::profile;trace->began=true;}
            begin(fresh,profile,domain);return traced_status(trace);
        }
        accept_time(fresh);if(status==PreparationStatus::complete)return traced_status(trace);
        const auto& candidate=catalogue_[cursor];
        detail::quote_into(frozen_,candidate.profile,candidate.prefix,candidate.purchase,candidate.before,scratch_,&proof);
        auto& best=candidate.control?control_:selected_;if(better(scratch_,best))best=scratch_;++cursor;if(trace)trace->quoted=true;
        // A newly discovered successful branch may already have expired while
        // preparing. Recompute from this fresh frame instead of publishing it.
        if(!compatible(fresh,domain,trace)){
            if(trace)trace->began=true;begin(fresh,profile,domain);return traced_status(trace);
        }
        if(cursor==count_){preparation_detail::canonicalize(control_);preparation_detail::canonicalize(selected_);status=PreparationStatus::complete;}
        return traced_status(trace);
    }
    PreparationStatus publish_into(const Observation& fresh,Decision& out,PreparationDomain domain={}){
        out={};if(!active_){status=PreparationStatus::invalid;return status;}
        if(shortcut(fresh,profile_,domain)){out=shortcut_;return status;}
        if(!valid(fresh)){reset();return status;}
        if(!compatible(fresh,domain))return begin(fresh,profile_,domain);
        accept_time(fresh);if(status!=PreparationStatus::complete)return status;
        out.input=fresh;out.profile=profile_;out.control=control_;out.selected=profile_.arm==Arm::bank_each_control?control_:selected_;
        out.feasible=out.selected.feasible;
        if(out.feasible){out.job=out.selected.prefix?Job::harvest:
            fresh.resources.bag.value?Job::liquidate:Job::productive_fallback;}
        return status;
    }
};
} // namespace p5::runoff
