// Actual SDK wrappers and explicit8x8 host; no native engine or injected job.
int (*observation_camp_probe)(unsigned)=nullptr;
double observation_shield=0;
unsigned observation_shield_ticks=0;
#include "sortie-fixture.h"

namespace {
unsigned observer_cases=0,ordinary_reviews=0,epoch_yields=0;
unsigned observer_probe_id=99,observer_mode=0;
int observer_code=0;
int observer_probe(unsigned id) {
    if(id!=observer_probe_id)return 0;
    if(observer_mode==1)return observer_code;
    if(observer_mode==2){++current_round;observer_mode=0;}
    if(observer_mode==3)fuel=29999;
    return 0;
}
void helper_cases() {
    for(unsigned variant=0;variant<10;++variant) {
        reset_case();auto c=std::make_unique<Campaign>(Controller::ledger,false,"observation-helper");initialize(*c);
        const auto index=std::size_t(c->group_index(bp_key(1)));const auto& design=c->personal_.groups[index];
        if(variant<5){member_states[3]=0;member_health[3]=0;}
        if(variant==1 || variant==2){camp_state_error_id=3;camp_state_error_code=variant==1?97:98;}
        if(variant==3)visible[0]&=~(std::uint64_t{1}<<(bp_axis(bp_y[3])*8+bp_axis(bp_x[3])));
        auto camps=cambc::vision::camps();
        if(variant==4)camps.erase(std::remove_if(camps.begin(),camps.end(),[](const auto& camp){return unsigned(static_cast<cambc::handle_camp_t>(camp))==4;}),camps.end());
        if(variant==5 || variant==6){observer_probe_id=3;observer_mode=1;observer_code=variant==5?97:98;observation_camp_probe=observer_probe;}
        if(variant==7)fuel=59999;
        if(variant==8){observer_probe_id=3;observer_mode=3;observation_camp_probe=observer_probe;member_health[3]-=100;}
        if(variant==9){observer_probe_id=3;observer_mode=2;observation_camp_probe=observer_probe;}
        const auto old=c->personal_.observations[index];const unsigned began=current_round;
        const bool result=c->personal_.observe_group(index,camps,cambc::vision::visible_tiles(),variant==7?60000:30000);
        observation_camp_probe=nullptr;observer_mode=0;fuel=1000000;
        if(variant==7) {
            verify(!result && c->personal_.observations[index].round==old.round);
            fuel=59999;
            verify(c->personal_.observe_group(index,camps,cambc::vision::visible_tiles(),30000));
            fuel=1000000;
        } else if(variant==9) {
            verify(!result && current_round==began+1 && c->personal_.observations[index].round==old.round &&
                c->personal_.observations[index].remaining_health==old.remaining_health);
        } else {
            const auto& o=c->personal_.observations[index];verify(result && o.round==began && o.lifecycle==Lifecycle::alive);
            if(variant==0)verify(o.remaining_health==design.full_health-design.members[0].health && o.incoming==design.incoming-design.members[0].strength);
            else if(variant==4)verify(o.remaining_health==design.full_health-design.members[0].health && !o.all_members_visible);
            else if(variant==8)verify(o.remaining_health==design.full_health-100 && o.incoming==design.incoming && !o.all_members_visible);
            else verify(o.remaining_health==design.full_health && o.incoming==design.incoming && !o.all_members_visible);
        }
        ++observer_cases;
    }
    // Unexpected errors must still escape through the unchanged SDK boundary.
    if(const char* mode=std::getenv("P3_OBSERVATION_ERROR")) {
        reset_case();auto c=std::make_unique<Campaign>(Controller::ledger,false,"observation-error");initialize(*c);
        auto camps=cambc::vision::camps();const auto index=std::size_t(c->group_index(bp_key(1)));
        if(mode[0]=='s'){camp_state_error_id=3;camp_state_error_code=1;}
        else{observer_probe_id=3;observer_mode=1;observer_code=1;observation_camp_probe=observer_probe;}
        std::fprintf(stderr,"OBSERVATION_EXPECTED_ERROR %s\n",mode);
        (void)c->personal_.observe_group(index,camps,cambc::vision::visible_tiles(),30000);verify(false);
    }
}
std::unique_ptr<Campaign> ordinary_caller() {
    reset_case();member_states[4]=member_states[5]=0;member_health[4]=member_health[5]=0;
    observation_shield=0;observation_shield_ticks=0;
    auto c=std::make_unique<Campaign>(Controller::ledger,false,"ordinary-observation-caller");
    for(unsigned n=0;n<128;++n){c->turn();advance_round();if(attacks && actor.health<actor.max_health)break;}
    verify(c->active_ && c->active_->id.kind==JobKind::cycle && c->active_->id.secondGroup<0 &&
        c->active_->id.firstGroup==bp_key(1) && attacks>0 && !c->sortie_episode_ && !c->two_trip_episode_);
    verify(member_health[3]>0 && member_health[3]<c->personal_.groups[std::size_t(c->group_index(bp_key(1)))].members[0].health);
    verify(monster_hits>0 && actor.health<actor.max_health);return c;
}
void fuel_deferred_review() {
    auto c=ordinary_caller();const auto id=c->active_->id;
    const auto index=std::size_t(c->group_index(id.firstGroup));const auto& design=c->personal_.groups[index];
    const auto stats=c->capital_.exact_stats();verify(stats.has_value());
    const auto stale=plan3::estimate_clear(design.full_health,design.incoming,stats->strength,stats->focus,
        stats->campDamage,stats->campMitigation,actor.health,actor.max_health);
    verify(!stale.admitted); // This is the old false full-clear-invalid path.
    const double expectedHealth=float(member_health[3]);const double expectedIncoming=design.members[0].strength;
    c->last_review_=current_round-8;const unsigned aborted=c->telemetry_.aborted;
    fuel=60000; // Normal rotating scan's65000 gate defers; priority floor30000 runs.
    c->turn();fuel=1000000;
    const auto* fresh=c->personal_.current(index);
    verify(fresh && fresh->round==current_round && fresh->remaining_health==expectedHealth && fresh->incoming==expectedIncoming);
    verify(c->active_ && c->active_->id==id && c->telemetry_.aborted==aborted && c->last_review_==current_round);
    verify(attacks>1);++ordinary_reviews;
    std::printf("OBSERVATION_REVIEW {\"side\":%u,\"normalScan\":\"fuel-deferred-at60000\",\"groups\":%zu,\"remainingHP\":%.17g,\"incoming\":%.17g,\"actorHP\":%.17g,\"oldFullBoundRejected\":true,\"activeRetained\":true}\n",bp_side(),c->personal_.groups.size(),fresh->remaining_health,fresh->incoming,double(actor.health));
}
void cross_group_epoch_yield() {
    auto c=ordinary_caller();const auto id=c->active_->id;const unsigned now=current_round;
    const auto index=std::size_t(c->group_index(id.firstGroup));const auto previous=(index+c->personal_.groups.size()-1)%c->personal_.groups.size();
    c->personal_.observation_cursor_=previous;
    const unsigned actionBefore=actions,aborted=c->telemetry_.aborted,converted=c->telemetry_.converted;
    observer_probe_id=3;observer_mode=2;observation_camp_probe=observer_probe;
    c->turn();observation_camp_probe=nullptr;observer_mode=0;
    verify(current_round==now+1 && c->personal_.observations[previous].round==now);
    verify(actions==actionBefore && c->active_ && c->active_->id==id && c->telemetry_.aborted==aborted && c->telemetry_.converted==converted);
    verify(c->telemetry_.cutoffs>0);++epoch_yields;
}
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);helper_cases();
    fuel_deferred_review();cross_group_epoch_yield();
    std::printf("OBSERVATION_RESULT {\"status\":\"pass\",\"side\":%u,\"checks\":%u,\"helperCases\":%u,\"ordinaryReviews\":%u,\"epochYields\":%u,\"nativeGames\":0}\n",bp_side(),checks,observer_cases,ordinary_reviews,epoch_yields);
}
