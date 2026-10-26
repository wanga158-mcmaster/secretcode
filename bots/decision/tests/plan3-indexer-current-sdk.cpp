// Current Indexer regression through the pinned SDK and modeled host actions.
// Public unit/settings/catalogue bytes are captured. The 8x8 layout, vision,
// initial reward, capture ticks and recall arrival are declared fixture inputs,
// not native reachability, combat, timing or fuel evidence.
#if defined(__wasm__)
#undef __wasm__
#include <cambc.h>
#define __wasm__ 1
#else
#include <cambc.h>
#endif
#include "../indexer-common/runtime.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "indexer-config-v1.h"
std::int64_t (*prediction_fuel_probe)()=nullptr;
#include "indexer-host-v1.h"
#include "indexer-current-host.h"

namespace {
using namespace plan3_runtime;
using namespace plan3;
unsigned scenarios=0,flows=0,conversion_cases=0;
unsigned capture_ticks=0,captures=0,recall_ticks=0,seen_recalls=0;
Campaign* prediction_campaign=nullptr;
std::int64_t prediction_tail=1000000;
std::int64_t committed_fuel(){return prediction_campaign && prediction_campaign->active_?prediction_tail:-1;}

void reset_case(bool no_gems=false) {
    prediction_fuel_probe=nullptr;prediction_campaign=nullptr;
    reset();current_round=32;own_score=100;bag=0;gold=0;
    cambc::detail::champion_design design{};
    std::memcpy(&design,bp_config::champions[current_actor_type()].data(),sizeof(design));
    const auto& s=design.base_stats;
    actor={int(bp_side()),int(current_actor_type()),bp_axis(0),bp_axis(0),
        s.base_health,s.base_health,s.base_strength,s.base_focus,s.base_move_speed,s.base_attack_range,
        1|(1<<3)|(1<<4)};
    all_gold_map=no_gems;monolith_map=false;bp_hidden_collision=false;cost_zero_item=-1;
    group_state.fill(1);group_state[2]=2;
    raw_gems.fill(0);raw_gold.fill(105);
    if(!no_gems)raw_gems[0]=raw_gems[1]=66;
    visible.fill(0);set(visible.data(),bp_axis(0),bp_axis(0));
    set(visible.data(),bp_axis(2),bp_axis(3));
    for(unsigned i=6;i<9;++i)set(visible.data(),bp_axis(bp_cx(i)),bp_axis(bp_cy(i)));
    buys=submissions=recalls=actions=0;
    all_purchases_on_spawn=true;
    capture_ticks=captures=recall_ticks=seen_recalls=0;
    indexer_terminal_counter=nullptr;indexer_drop_after=0;
    query_trace.clear();fuel_trace.clear();++scenarios;
}

void initialize(Campaign& c,unsigned mode) {
    c.set_indexer_arm(mode);
    unsigned callbacks=0;
    while(!c.initialized_){verify(++callbacks<256);c.initialize_campaign();}
    while(c.field_cursor_<c.field_order_.size())c.register_fields();
    // Separate synthetic preparation callbacks, never an extra per-turn budget.
    for(unsigned i=0;i<512;++i){c.paths_.begin_turn();c.paths_.advance();}
    while(!c.capital_.advance_catalogue(4)){}
    c.personal_.observe(cambc::vision::camps(),{});
    c.planning_audits_=2;c.candidate_audit_=true;
    c.last_progress_=c.last_review_=current_round;
    verify(c.personal_.initialization_complete && c.indexer_universe_.ready);
    verify(c.group_index(bp_key(2))>=0);
}

void complete_opening(Campaign& c) {
    // Explicit initial fixture cash, not claimed earned income. Exercise real
    // shop counters and automatic combinations rather than seed inconsistent
    // inventory into a fresh executor whose purchase history starts at zero.
    gold=300;c.shop();
    verify(gold==0 && c.monster_shards()==5 && c.jungle_shards()==5);
    verify(items[unsigned(cambc::item_type::monster_emblem_of_strength)]==1);
    verify(items[unsigned(cambc::item_type::jungle_emblem_of_health)]==1);
}

// A bounded host model acknowledges the public reward transition after the
// queried capture countdown, and recall arrival after its queried countdown.
// It deliberately does not simulate monsters, ability effects or native time.
void next_round() {
    if(group_state[2]==2){
        bool occupies=false;
        for(unsigned i=6;i<9;++i)occupies|=actor.x==bp_axis(bp_cx(i)) && actor.y==bp_axis(bp_cy(i));
        capture_ticks=occupies?capture_ticks+1:0;
        if(capture_ticks==unsigned(cambc::constants::capture_time)){
            verify(raw_gems[2]==0);gold+=raw_gold[2];group_state[2]=3;++captures;
        }
    }
    if(recalls>seen_recalls){seen_recalls=recalls;recall_ticks=unsigned(cambc::constants::recall_time);}
    if(actor.state&(1<<1)){
        verify(recall_ticks>0);
        if(!--recall_ticks){actor.state&=~(1<<1);actor.x=bp_axis(0);actor.y=bp_axis(0);}
    }
    ++current_round;actor.state|=(1<<3)|(1<<4);
}

__attribute__((noinline)) void selection(unsigned mode,bool no_gems,bool opened=false) {
    reset_case(no_gems);
    Campaign c(Controller::ledger,false,"indexer-current-sdk");initialize(c,mode);
    if(opened)complete_opening(c);
    auto context=c.forecast_context(c.information());
    auto jobs=c.menu(context,false,cambc::vision::camps());
    auto ready=[](){return cambc::fuel_remaining()>=45000;};
    const auto selected=plan3_indexer::select_menu(context,c.indexer_universe_,jobs,mode,c.context_,false,ready,c.indexer_opening_unfinished());
    verify(!context.cutoff && selected.triple.eligibleCount==0);
    if(!opened){
        verify(selected.bootstrap && selected.priority && selected.intent.job && selected.companion.job);
        verify(*selected.intent.job==*selected.companion.job);
        verify(selected.intent.job->kind==JobKind::cycle && selected.intent.job->secondGroup<0);
        const auto job=std::find_if(jobs.begin(),jobs.end(),[&](const auto& j){return j.id==*selected.intent.job;});
        verify(job!=jobs.end() && job->rewardOnlyEndpoint && selected.intent.reason==DecisionReason::gold_fallback);
    }else{
        verify(!c.indexer_opening_unfinished() && !selected.bootstrap);
        verify(items[unsigned(cambc::item_type::power_shard_of_strength)]==0);
        if(!no_gems)verify(!selected.intent.job);
    }
    std::printf("IX_CURRENT {\"case\":\"selection\",\"side\":%u,\"actorType\":%u,\"mode\":%u,\"noGems\":%s,\"openingComplete\":%s,\"bootstrap\":%s}\n",
        bp_side(),current_actor_type(),mode,no_gems?"true":"false",opened?"true":"false",selected.bootstrap?"true":"false");
}

__attribute__((noinline)) void cutoff(unsigned mode) {
    reset_case();Campaign c(Controller::ledger,false,"indexer-current-sdk");initialize(c,mode);
    auto context=c.forecast_context(c.information());auto jobs=c.menu(context,false,cambc::vision::camps());
    context.cutoff=true;auto yes=[](){return true;};
    auto selected=plan3_indexer::select_menu(context,c.indexer_universe_,jobs,mode,c.context_,false,yes,true);
    verify(!selected.bootstrap && !selected.intent.job);
    context.cutoff=false;unsigned queries=0;auto no=[&](){++queries;return false;};
    selected=plan3_indexer::select_menu(context,c.indexer_universe_,jobs,mode,c.context_,false,no,true);
    verify(queries && !selected.bootstrap && !selected.intent.job);
    fuel=44999;c.turn();verify(!c.active_ && actions==0 && moves==0);fuel=1000000;
}

__attribute__((noinline)) void faster_double(unsigned mode) {
    reset_case(true);visible.fill(~std::uint64_t{0});group_state.fill(2);
    Campaign c(Controller::ledger,false,"indexer-current-sdk");initialize(c,mode);
    auto context=c.forecast_context(c.information());auto jobs=c.menu(context,false,cambc::vision::camps());
    double single=0,twice=0;
    for(const auto& j:jobs)if(j.rewardOnlyEndpoint && j.campGold>0 && admitted(context.info,j,false)){
        const auto* n=j.nominal();verify(n && n->tau>0);
        const double rate=double(j.campGold)/n->tau;
        if(j.id.kind==JobKind::cycle)single=std::max(single,rate);
        if(j.id.kind==JobKind::double_cycle)twice=std::max(twice,rate);
    }
    verify(single>0 && twice>single);
    auto ready=[](){return true;};
    const auto s=plan3_indexer::select_menu(context,c.indexer_universe_,jobs,mode,c.context_,false,ready,true);
    verify(s.bootstrap && s.intent.job && s.intent.job->kind==JobKind::cycle && s.intent.job->secondGroup<0);
    std::printf("IX_CURRENT {\"case\":\"faster-double-excluded\",\"side\":%u,\"actorType\":%u,\"mode\":%u,\"singleRate\":%.17g,\"doubleRate\":%.17g}\n",
        bp_side(),current_actor_type(),mode,single,twice);
}

__attribute__((noinline)) void full_flow(unsigned mode) {
    reset_case();Campaign c(Controller::ledger,false,"indexer-current-sdk");initialize(c,mode);
    unsigned callbacks=0;bool committed=false;
    while(!buys && callbacks++<64){
        c.turn();
        committed|=c.indexer_bootstrap_pending_shop_;
        if(!buys)next_round();
    }
    verify(callbacks<64 && committed && captures==1 && bag==0 && submissions==0);
    verify(buys==1 && gold==15);
    verify(c.monster_shards()==3 && c.jungle_shards()==0);
    const bool tavi=current_actor_type()==unsigned(cambc::champion_type::tavi);
    verify(tavi ? recalls==0 : recalls==1);
    verify(tavi ? !all_purchases_on_spawn : all_purchases_on_spawn);
    ++flows;
    std::printf("IX_CURRENT {\"case\":\"capture-cash-shop\",\"side\":%u,\"actorType\":%u,\"mode\":%u,\"callbacks\":%u,\"captureGold\":105,\"remainingGold\":%u,\"buys\":%u,\"recalls\":%u,\"modeledTransitionOnly\":true}\n",
        bp_side(),current_actor_type(),mode,callbacks,gold,buys,recalls);
}

__attribute__((noinline)) void shared_priorities(unsigned mode,unsigned which) {
    reset_case();Campaign c(Controller::ledger,false,"indexer-current-sdk");initialize(c,mode);
    if(which==0){bag=20;actor.state|=1<<2;}
    if(which==1)actor.state|=1<<1;
    if(which==2){bag=20;own_score=unsigned(cambc::constants::winning_gems)-20;}
    if(which==3)bag=20;
    if(which==4)actor.health=actor.max_health*.2f;
    const unsigned decisions=c.telemetry_.decisions;c.turn();
    if(which<2)verify(actions==0 && moves==0 && c.telemetry_.decisions==decisions);
    else if(which<4)verify(c.active_ && c.active_->id.kind==JobKind::bank && !c.indexer_bootstrap_pending_shop_);
    else verify(!c.indexer_bootstrap_pending_shop_ && (c.recovering_ || !c.active_ || c.active_->id.kind==JobKind::renew));
}

__attribute__((noinline)) void conversion(unsigned mode,unsigned variant) {
    reset_case();visible.fill(~std::uint64_t{0});group_state[0]=2;
    Campaign c(Controller::ledger,false,"indexer-current-sdk");initialize(c,mode);
    const int key=c.indexer_universe_.keys[0],g=c.group_index(key);
    c.personal_.observations[unsigned(g)].lifecycle=Lifecycle::reward;
    auto context=c.forecast_context(c.information());auto job=context.acquisition(g);
    verify(job.nominal() && job.campGems>0);
    c.active_=job;c.stage_=2;c.start_round_=current_round-2;c.indexer_episode_=7;
    bag=unsigned(job.campGems);actor.state|=1<<2;
    c.last_team_score_=own_score;c.last_score_round_=current_round;c.last_progress_=current_round;
    c.finish_observation();verify(c.conversion_snapshot_.valid && c.indexer_witness_.valid);
    const unsigned amount=bag;bag=0;actor.state&=~(1<<2);
    current_round+=variant==2?2:1;own_score+=amount+(variant==1 || variant==2?1:0);
    if(variant==3)++c.start_round_;
    c.score_observations();
    const bool expected=variant!=2 && variant!=3;
    verify(bool(c.active_)!=expected && c.telemetry_.converted==(variant==2?0:amount));
    const auto converted=c.telemetry_.converted;c.score_observations();verify(c.telemetry_.converted==converted);
    ++conversion_cases;
    std::printf("IX_CURRENT {\"case\":\"conversion\",\"side\":%u,\"actorType\":%u,\"mode\":%u,\"variant\":%u,\"completed\":%s,\"lowerBound\":%u,\"exactExpected\":%s}\n",
        bp_side(),current_actor_type(),mode,variant,expected?"true":"false",unsigned(converted),variant==0?"true":"false");
}

__attribute__((noinline)) void prediction_tail_cases(unsigned mode) {
    std::optional<JobForecast> healthy;
    for(std::int64_t tail:{1000000,44999,45000,29000}) {
        reset_case();Campaign c(Controller::ledger,false,"indexer-current-sdk");initialize(c,mode);
        prediction_campaign=&c;prediction_tail=tail;prediction_fuel_probe=committed_fuel;
        const bool turn=tail==29000;
        const unsigned omitted=c.telemetry_.details_omitted,cutoffs=c.telemetry_.cutoffs;
        std::printf("IX_TAIL_BEGIN {\"mode\":%u,\"fuel\":%lld,\"turn\":%s}\n",mode,static_cast<long long>(tail),turn?"true":"false");
        if(turn)c.turn();else c.choose(c.information(),false,cambc::vision::camps());
        prediction_fuel_probe=nullptr;prediction_campaign=nullptr;
        verify(c.active_ && c.telemetry_.activations==1 && actions==0 && moves==0);
        if(!healthy)healthy=c.active_;
        else {
            verify(c.active_->id==healthy->id && c.active_->campGems==healthy->campGems && c.active_->campGold==healthy->campGold);
            verify(c.active_->nominal() && healthy->nominal() && c.active_->nominal()->tau==healthy->nominal()->tau &&
                c.active_->nominal()->deltaS==healthy->nominal()->deltaS && c.active_->nominal()->healthLoss==healthy->nominal()->healthLoss);
        }
        verify(c.telemetry_.details_omitted==omitted+unsigned(tail<45000));
        const bool fresh=c.conversion_snapshot_.valid && c.conversion_snapshot_.round==unsigned(current_round) &&
            c.conversion_snapshot_.scoreRound==unsigned(current_round) && c.conversion_snapshot_.bag==bag;
        if(turn)verify(fresh && c.telemetry_.cutoffs==cutoffs+1);
        std::printf("IX_CURRENT {\"case\":\"prediction-tail\",\"side\":%u,\"actorType\":%u,\"mode\":%u,\"fuel\":%lld,\"turn\":%s,\"omitted\":%u,\"freshSnapshot\":%s,\"actions\":%u,\"moves\":%u}\n",
            bp_side(),current_actor_type(),mode,static_cast<long long>(tail),turn?"true":"false",c.telemetry_.details_omitted-omitted,fresh?"true":"false",actions,moves);
    }
}
}

int main() {
    for(unsigned mode=0;mode<3;++mode){
        selection(mode,false);selection(mode,true);selection(mode,false,true);selection(mode,true,true);
        cutoff(mode);faster_double(mode);full_flow(mode);
        for(unsigned which=0;which<5;++which)shared_priorities(mode,which);
        for(unsigned variant=0;variant<4;++variant)conversion(mode,variant);
        prediction_tail_cases(mode);
    }
    std::printf("IX_CURRENT_RESULT {\"status\":\"pass\",\"side\":%u,\"actorType\":%u,\"scenarios\":%u,\"flows\":%u,\"conversions\":%u,\"checks\":%u}\n",
        bp_side(),current_actor_type(),scenarios,flows,conversion_cases,checks);
}
