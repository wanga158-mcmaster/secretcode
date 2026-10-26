// Actual SDK and queried public designs; explicit 8x8/action/monster/channel
// transition model. This fixture does not execute the native engine.
#if defined(__wasm__)
#undef __wasm__
#include <cambc.h>
#define __wasm__ 1
#else
#include <cambc.h>
#endif
#if defined(P3_TWO_BACKPRESSURE)
#include "../backpressure-common/runtime.h"
#elif defined(P3_TWO_INDEXER)
#include "../indexer-common/runtime.h"
#else
#include "../common/runtime.h"
#endif
#include "../common/two-trip-runtime.h"
#include "../common/two-trip-menu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "backpressure-config-v1.h"
std::int64_t (*optional_fuel_probe)()=nullptr;
namespace {
bool occupied_by_living_camp(int,int);
bool tile_visible(unsigned);
bool on_spawn();
void sync_stats();
double current_incoming();
std::array<unsigned,9> member_states{};
std::array<double,9> member_health{};
std::array<unsigned,9> camp_state_calls{};
unsigned camp_state_error_id=99;
int camp_state_error_code=0;
unsigned recall_timer=0,tribute_timer=0;
extern unsigned actions;
}
#include "backpressure-host-v1.h"
#include "two-trip-runtime-host.h"

namespace {
using namespace plan3_runtime;
using namespace plan3;
namespace tt=plan3_runtime::two_trip;
std::array<unsigned,2> cooldown{};
std::array<unsigned,9> stuns{};
std::array<unsigned,3> absent{},capture_count{},respawn_count{};
unsigned attacks=0,casts=0,monster_hits=0,captures=0,healing_ticks=0,owner_ends=0;
unsigned scenarios=0,completed_paths=0,boundary_cases=0,bootstrap_cases=0,optional_gate_cases=0;
unsigned cold_cases=0,cold_paths=0;
std::array<unsigned,2> cold_funded_paths{};
unsigned low_health_cases=0;
unsigned service_cases=0;
bool fixture_stunned=false;
double camp_damage=1,camp_mitigation=1;
bool tile_visible(unsigned id) {return (visible[0]>>(bp_axis(bp_y[id])*8+bp_axis(bp_x[id])))&1u;}
bool on_spawn() {return actor.x==bp_axis(0) && actor.y==bp_axis(0);}
bool occupied_by_living_camp(int x,int y) {
    for(unsigned i=0;i<9;++i)if(member_states[i] && x==bp_axis(bp_x[i]) && y==bp_axis(bp_y[i]))return true;
    return false;
}
void sync_stats() {
    TreasuryPlanner planner;while(!planner.advance_catalogue(4)){}
    const auto s=planner.exact_stats();verify(s.has_value());
    const double old=actor.max_health;actor.max_health=s->health;actor.strength=s->strength;actor.focus=s->focus;
    camp_damage=s->campDamage;camp_mitigation=s->campMitigation;
    if(on_spawn())actor.health+=std::max(0.,actor.max_health-old);
}
double current_incoming() {
    double result=0;
    for(unsigned i=0;i<9;++i)if(member_states[i]==1 && !stuns[i] &&
        std::max(std::abs(actor.x-bp_axis(bp_x[i])),std::abs(actor.y-bp_axis(bp_y[i])))<=3) {
        cambc::detail::camp_design d{};std::memcpy(&d,bp_config::camps[unsigned(bp_kind(i))].data(),sizeof(d));
        result+=d.base_stats.base_strength*camp_mitigation;
    }
    return result;
}
void damage(unsigned id,double amount) {
    verify(id<9 && member_states[id] && tile_visible(id) && std::isfinite(amount) && amount>0);
    for(unsigned i=id/3*3;i<id/3*3+3;++i)if(member_states[i])member_states[i]=1;
    member_health[id]=std::max(0.,member_health[id]-amount);
    if(member_health[id]==0)member_states[id]=0;
    bool living=false;for(unsigned i=id/3*3;i<id/3*3+3;++i)living|=member_states[i]!=0;
    if(!living){group_state[id/3]=2;capture_count[id/3]=0;}
}
void reset_case() {
    optional_fuel_probe=nullptr;reset();++scenarios;current_round=32;bag=0;gold=0;own_score=0;
    const char* champion=std::getenv("P3_BP_ACTOR");const int type=champion && !std::strcmp(champion,"varg")?10:11;
    cambc::detail::champion_design d{};std::memcpy(&d,bp_config::champions[unsigned(type)].data(),sizeof(d));
    const auto& s=d.base_stats;
    actor={int(bp_side()),type,bp_axis(0),bp_axis(0),s.base_health,s.base_health,s.base_strength,s.base_focus,s.base_move_speed,s.base_attack_range,1|(1<<3)|(1<<4)|(1<<11)};
    enemy.team=1-int(bp_side());member_states.fill(0);member_health.fill(0);group_state={3,1,3};
    raw_gems={66,66,0};raw_gold={270,270,105};all_gold_map=false;cost_zero_item=-1;bp_hidden_collision=false;
    for(unsigned i=3;i<6;++i){cambc::detail::camp_design camp{};std::memcpy(&camp,bp_config::camps[unsigned(bp_kind(i))].data(),sizeof(camp));member_states[i]=2;member_health[i]=camp.base_stats.base_health;}
    cooldown.fill(0);stuns.fill(0);absent.fill(0);capture_count.fill(0);respawn_count.fill(0);
    attacks=casts=monster_hits=captures=healing_ticks=owner_ends=0;buys=submissions=recalls=actions=0;
    recall_timer=tribute_timer=0;camp_damage=camp_mitigation=1;fixture_stunned=false;
    camp_state_calls.fill(0);camp_state_error_id=99;camp_state_error_code=0;
}
template<class C> void initialize(C& c) {
    unsigned count=0;while(!c.initialized_){verify(++count<128);c.initialize_campaign();}
    while(c.field_cursor_<c.field_order_.size())c.register_fields();
    for(unsigned i=0;i<64;++i){c.paths_.begin_turn();c.paths_.advance();}
    while(!c.capital_.advance_catalogue(4)){}
    c.personal_.observe(cambc::vision::camps(),{});
    c.last_progress_=current_round;c.last_review_=current_round;
    if constexpr(requires{c.candidate_audit_;}){c.candidate_audit_=true;verify(c.candidate_audit_);}
}
void arm(Campaign& c,unsigned mode) {
#if defined(P3_TWO_BACKPRESSURE)
    c.set_backpressure_arm(mode!=0);
#elif defined(P3_TWO_INDEXER)
    c.set_indexer_arm(mode);
#else
    verify(mode==0);(void)c;
#endif
}
[[maybe_unused]] void trip_counters(Campaign& c,unsigned completed,unsigned aborted) {
    verify(c.telemetry_.two_trip_started==1 && c.telemetry_.two_trip_completed==completed && c.telemetry_.two_trip_aborted==aborted);
    const unsigned lines=c.telemetry_.lines_;c.telemetry_.summary(true);verify(c.telemetry_.lines_==lines+1);
    std::printf("TWO_TRIP_RUNTIME_RECORD {\"kind\":\"trip-counters\",\"side\":%u,\"started\":1,\"completed\":%u,\"aborted\":%u}\n",bp_side(),completed,aborted);
}
void opening(Campaign& c,unsigned power) {
    gold=300+30*power;
    c.shop();verify(c.monster_shards()==5 && c.jungle_shards()==5);
    verify(items[unsigned(cambc::item_type::power_shard_of_strength)]==power);
    verify(gold==0 && on_spawn());actor.health=actor.max_health;
}
// Explicit owner-end and next owner-start transition. Captures precede channels;
// zero channel timers are resolved ONLY at the following start.
void advance_round() {
    ++owner_ends;
    for(auto& value:cooldown)if(value)--value;
    if((actor.state&(1<<1)) && recall_timer)--recall_timer;
    if((actor.state&(1<<2)) && tribute_timer)--tribute_timer;
    if(on_spawn() && actor.health<actor.max_health){actor.health=std::min(double(actor.max_health),actor.health+double(cambc::constants::spawn_healing)*actor.max_health);++healing_ticks;}
    ++current_round;
    for(unsigned g=0;g<3;++g) {
        bool present=false,in_range=false;
        for(unsigned i=g*3;i<g*3+3;++i){present|=actor.x==bp_axis(bp_x[i]) && actor.y==bp_axis(bp_y[i]);in_range|=std::max(std::abs(actor.x-bp_axis(bp_x[i])),std::abs(actor.y-bp_axis(bp_y[i])))<=3;}
        if(group_state[g]==2) {
            if(!present)capture_count[g]=0;
            else if(!capture_count[g])capture_count[g]=unsigned(cambc::constants::capture_time)+1;
            else if(--capture_count[g]==1){bag+=raw_gems[g];gold+=raw_gold[g];group_state[g]=3;respawn_count[g]=40;++captures;}
        }
        if(group_state[g]==1) {
            absent[g]=in_range?0:absent[g]+1;
            if(absent[g]>=unsigned(cambc::constants::camp_sleep_time))for(unsigned i=g*3;i<g*3+3;++i)if(member_states[i])member_states[i]=2;
        }
        if(group_state[g]==3 && respawn_count[g] && !present)--respawn_count[g];
    }
    const double incoming=current_incoming();if(incoming){verify(!(actor.state&(1<<1)));actor.health-=incoming;++monster_hits;verify(actor.health>0);}
    for(auto& value:stuns)if(value)--value;
    if((actor.state&(1<<1)) && recall_timer==0){actor.x=bp_axis(0);actor.y=bp_axis(0);actor.state&=~(1<<1);}
    if((actor.state&(1<<2)) && tribute_timer==0){own_score+=bag;bag=0;actor.state&=~(1<<2);}
    actor.state&=~((1<<3)|(1<<4)|(1<<11));
    if(!(actor.state&((1<<1)|(1<<2))))actor.state|=(1<<3)|(1<<4)|(1<<11);
}
Campaign* cold_campaign=nullptr;bool cold_latched=false,cold_interrupted=false;
template<class C> bool private_route_progress(const C& campaign) {
    const auto partial=[](const auto& route){return route && route->prepared_>0 && !route->ready();};
    if constexpr(requires{campaign.two_trip_menu_.current_;}) {
        for(const auto& current:campaign.two_trip_menu_.current_)if(partial(current.builder_.route_))return true;
        for(const auto& fresh:campaign.two_trip_menu_.fresh_)if(partial(fresh.route_))return true;
        return false;
    } else return partial(campaign.two_trip_menu_.builder_.route_);
}
std::int64_t cold_route_budget() {
    if(cold_campaign && !cold_interrupted) {
        if(private_route_progress(*cold_campaign))cold_latched=cold_interrupted=true;
    }
    return cold_latched?64999:-1;
}
void cold_commit_state(const tt::Plan& plan,const JobForecast& job,unsigned mode,unsigned funding,unsigned power) {
    std::printf("TWO_TRIP_RUNTIME_RECORD {\"kind\":\"cold-commit-state\",\"side\":%u,\"mode\":%u,\"fundingBudget\":%u,\"powerShards\":%u,\"round\":%u,\"preparedRound\":%u,\"sourceRound\":%u,\"job\":[%d,%d,%d,%d,%d],\"groupKey\":%d,\"nativeGroup\":%u,\"gems\":%d,\"origin\":[%d,%d],\"stats\":[%.17g,%.17g,%.17g,%.17g,%.17g],\"inventory\":[",
        bp_side(),mode,funding,power,current_round,plan.preparedRound,plan.initial.round,int(job.id.kind),job.id.firstGroup,job.id.secondGroup,job.id.altarTile,job.id.equipmentBlock,
        plan.groupKey,plan.nativeGroup,plan.initial.groupGems,plan.origin.x,plan.origin.y,plan.maximumHealth,plan.strength,plan.focus,plan.campDamage,plan.campMitigation);
    for(unsigned i=0;i<plan.inventory.size();++i)std::printf("%s%u",i?",":"",plan.inventory[i]);
    std::fputs("],\"members\":[",stdout);
    for(unsigned i=0;i<plan.initial.memberCount;++i) {
        const auto& member=plan.initial.members[i];
        std::printf("%s{\"nativeId\":%u,\"key\":%d,\"state\":%u,\"health\":%.17g,\"incoming\":%.17g}",i?",":"",plan.initial.nativeMemberIds[i],member.key,plan.initial.memberStates[i],member.health,member.incoming);
    }
    std::fputs("]}\n",stdout);
}
// No initialize(), prewarmed paths, direct Builder call or manually committed
// Plan. Equipment is explicitly conditional: hypothetical360/420 cash is spent by
// the real shop; this is not a native funding/visibility opportunity claim.
__attribute__((noinline)) void cold_startup(unsigned mode,unsigned funding) {
    verify(funding==360 || funding==420);const unsigned power=(funding-300)/30;
    reset_case();gold=funding;fuel=100000;
    auto c=std::make_unique<Campaign>(Controller::ledger,false,"cold-two-trip");arm(*c,mode);
    cold_campaign=c.get();cold_latched=cold_interrupted=false;optional_fuel_probe=cold_route_budget;
    unsigned callbacks=0,maximum_work=0,first_private_round=0,first_commit_round=0;
#if defined(P3_TWO_INDEXER)
    int cursor_expected=-1;unsigned original_episode=0,cursor_advances=0;
#endif
    for(;callbacks<256 && !c->telemetry_.two_trip_completed;++callbacks) {
        const bool already_interrupted=cold_interrupted;cold_latched=false;
        c->turn();maximum_work=std::max(maximum_work,c->paths_.work_);verify(c->paths_.work_<=2);
        if(!already_interrupted && cold_interrupted) {
            first_private_round=current_round;verify(!c->two_trip_episode_ && !c->telemetry_.two_trip_started);
        }
        if(c->two_trip_episode_ && !first_commit_round) {
            first_commit_round=current_round;
            verify(c->two_trip_episode_->preparedRound==current_round && c->two_trip_episode_->initial.round==current_round);
            verify(c->two_trip_episode_->maximumHealth==actor.max_health && c->two_trip_episode_->strength==actor.strength);
            verify(c->active_ && c->active_->nominal()->deltaS==66);
            verify(c->monster_shards()==5 && c->jungle_shards()==5 && items[unsigned(cambc::item_type::power_shard_of_strength)]==power);
            verify(actor.max_health==1125 && actor.strength==60+5*power && actor.focus==100);
            cold_commit_state(*c->two_trip_episode_,*c->active_,mode,funding,power);
#if defined(P3_TWO_INDEXER)
            original_episode=c->indexer_episode_;cursor_advances=c->indexer_circuit_advances_;verify(c->indexer_universe_.count>=2);
            for(unsigned i=0;i<c->indexer_universe_.count;++i)if(c->indexer_universe_.keys[i]==c->active_->id.firstGroup)
                cursor_expected=c->indexer_universe_.keys[(i+1)%c->indexer_universe_.count];
            verify(cursor_expected>=0);
#endif
        }
#if defined(P3_TWO_INDEXER)
        if(c->two_trip_episode_)verify(c->indexer_episode_==original_episode);
#endif
        verify(!c->telemetry_.two_trip_aborted);
        if(!c->telemetry_.two_trip_completed)advance_round();
    }
    optional_fuel_probe=nullptr;cold_campaign=nullptr;fuel=1000000;
    verify(callbacks<256 && cold_interrupted && first_private_round && first_commit_round>first_private_round && maximum_work==2);
    // The completion callback may immediately spend its earned270 gold on
    // further Power purchases; the fixed loadout was checked at commitment.
    verify(c->monster_shards()==5 && c->jungle_shards()==5);
    verify(!c->two_trip_episode_ && c->telemetry_.converted==66 && own_score==66 && !bag);
#if defined(P3_TWO_INDEXER)
    verify(c->indexer_universe_.cursor_key()==cursor_expected && !c->indexer_witness_.valid && c->indexer_circuit_advances_==cursor_advances+1);
    const auto converted=c->telemetry_.converted;c->score_observations();
    verify(c->indexer_universe_.cursor_key()==cursor_expected && c->telemetry_.converted==converted && c->indexer_circuit_advances_==cursor_advances+1);
#endif
    trip_counters(*c,1,0);++cold_cases;++cold_paths;++completed_paths;++cold_funded_paths[funding==420];
    std::printf("TWO_TRIP_RUNTIME_RECORD {\"kind\":\"cold-startup\",\"side\":%u,\"mode\":%u,\"callbacks\":%u,\"privateRound\":%u,\"commitRound\":%u,\"maximumSharedTickets\":%u,\"converted\":66,\"fundingBudget\":%u,\"powerShards\":%u,\"funding\":\"assumed%u-real-shop\",\"nativeOpportunityClaim\":false}\n",bp_side(),mode,callbacks,first_private_round,first_commit_round,maximum_work,funding,power,funding);
}
#if !defined(P3_TWO_INDEXER)
std::unique_ptr<tt::Menu> ready_menu(Campaign& c,InformationState& state) {
    auto menu=std::make_unique<tt::Menu>();
    for(unsigned callback=0;callback<8;++callback) {
        c.paths_.begin_turn();c.personal_.observe(cambc::vision::camps(),{});state=c.information();
        const auto prefix=tt::Menu::single_prefix(c.personal_,c.paths_,{actor.x,actor.y});
        const auto status=menu->prepare_early(c.personal_,c.capital_.exact_stats(),c.paths_,state,prefix,[] {return true;});
        verify(c.paths_.work_<=2);
        if(status==tt::Status::ready){verify(bool(menu->candidate_));return menu;}
        verify(status==tt::Status::pending);advance_round();
    }
    verify(false);return nullptr;
}
void same_jobs(const std::vector<JobForecast>& a,const std::vector<JobForecast>& b) {
    verify(a.size()==b.size());
    for(unsigned i=0;i<a.size();++i) {
        verify(a[i].id==b[i].id && a[i].complete==b[i].complete && a[i].routeKnown==b[i].routeKnown && a[i].fullClearAdmitted==b[i].fullClearAdmitted);
        verify(a[i].campGold==b[i].campGold && a[i].campGems==b[i].campGems && a[i].projectedRenewalAndReturnTime==b[i].projectedRenewalAndReturnTime);
        verify(a[i].outcomes.size()==b[i].outcomes.size());
        for(unsigned j=0;j<a[i].outcomes.size();++j)verify(a[i].outcomes[j].deltaS==b[i].outcomes[j].deltaS && a[i].outcomes[j].tau==b[i].outcomes[j].tau && a[i].outcomes[j].healthLoss==b[i].outcomes[j].healthLoss);
    }
}
// Explicit owned-row API boundaries. The sixteen ID variants are synthetic
// cap inputs, not sixteen physical groups asserted in this8x8 world.
__attribute__((noinline)) void menu_merge_boundaries() {
    reset_case();auto c=std::make_unique<Campaign>(Controller::ledger,false,"two-trip-merge");arm(*c,0);initialize(*c);opening(*c,4);
    JobForecast gold_job;gold_job.id={JobKind::cycle,bp_key(2),-1,bp_axis(3)*8+bp_axis(2),-1};
    gold_job.rewardOnlyEndpoint=true;gold_job.campGold=105;Outcome outcome;outcome.tau=10;gold_job.outcomes.push_back(outcome);
    for(unsigned variant=0;variant<7;++variant) {
        InformationState state;auto menu=ready_menu(*c,state);const auto id=menu->candidate_->job.id;
        std::vector<JobForecast> jobs{gold_job};
        if(variant==1)jobs.push_back(menu->candidate_->job); // Already admitted same-group Single.
        if(variant==2){jobs.clear();for(int key=0;key<16;++key){auto row=gold_job;row.id.firstGroup=key;jobs.push_back(row);}}
        const auto before=jobs;
        if(variant==3 || variant==6)advance_round();
        if(variant==5 || variant==6) {
            const auto prefix=tt::Menu::single_prefix(c->personal_,c->paths_,{actor.x,actor.y});
            const auto status=menu->prepare_early(c->personal_,c->capital_.exact_stats(),c->paths_,state,prefix,[&] {return variant!=5;});
            verify(status==tt::Status::cutoff);
        }
        const auto status=menu->merge(state,jobs);
        if(variant==0 || variant==4) {
            verify(status==tt::Status::ready && jobs.size()==before.size()+1 && menu->owns(id));
            if(variant==4){advance_round();verify(menu->owns(id) && !menu->take(id) && !menu->owns(id));}
            else {verify(bool(menu->take(id)) && !menu->owns(id) && !menu->take(id));}
        } else {
            verify(status==((variant==1 || variant==2)?tt::Status::unsupported:tt::Status::cutoff));
            same_jobs(jobs,before);verify(!menu->owns(id) && !menu->take(id));
        }
        verify(std::count_if(jobs.begin(),jobs.end(),[](const auto& row){return row.id.kind==JobKind::cycle;})<=16);++cold_cases;
    }
    InformationState state;auto menu=ready_menu(*c,state);const auto id=menu->candidate_->job.id;
    std::vector<JobForecast> full;
    for(int key=0;key<16;++key){auto row=gold_job;row.id.firstGroup=key;full.push_back(std::move(row));}
    full[7].id=id;full[7].fullClearAdmitted=false;full[7].outcomes.clear();
    const auto before=full;verify(!admitted(state,full[7],false));
    verify(menu->merge(state,full)==tt::Status::ready && full.size()==16 && menu->owns(id));
    verify(full[7].id==id && admitted(state,full[7],false));
    for(unsigned i=0;i<full.size();++i)if(i!=7)same_jobs({full[i]},{before[i]});
    verify(bool(menu->take(id)) && !menu->owns(id) && !menu->take(id));++cold_cases;
}
#endif
std::unique_ptr<tt::Candidate> prepare(Campaign& c,const char* label) {
    tt::Builder builder;auto stats=c.capital_.exact_stats();verify(stats.has_value());
    const auto index=std::size_t(c.group_index(bp_key(1)));
    for(unsigned n=0;n<16;++n) {
        c.paths_.begin_turn();auto p=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});
        if(p.status==tt::Status::pending){advance_round();continue;}
        std::printf("TWO_TRIP_RUNTIME_RECORD {\"kind\":\"candidate\",\"label\":\"%s\",\"side\":%u,\"actor\":%d,\"status\":%u,\"reason\":\"%s\",\"health\":%.17g,\"strength\":%.17g}\n",label,bp_side(),actor.type,unsigned(p.status),p.reason,actor.max_health,actor.strength);
        if(!p.candidate)return nullptr;
        auto out=std::move(p.candidate);
        const auto& f=out->accounting;const auto* nominal=out->job.nominal();verify(nominal);
        verify(!f.oneTripAdmitted && f.deltaS==66 && nominal->deltaS==66 && nominal->tau==f.tau);
        int charged=f.firstClearTurns+f.secondClearTurns+f.recallTurns+f.healingTurns+f.captureTurns+f.tributeTurns;
        for(const auto leg:f.legTurns)charged+=leg;
        verify(charged==f.tau && f.firstTripGems==0 && f.firstTripCampGold==0 && f.goldUtilityCredit==0 && f.terminalBag==0);
        verify(f.healthOnReturn==actor.max_health && f.terminalHealth==actor.max_health-f.secondClearLoss);
        verify(nominal->terminal.health==f.terminalHealth && nominal->healthLoss==f.firstClearLoss+f.escapeLoss+f.secondClearLoss);
        const int final_heal=f.terminalHealth>=actor.max_health?0:int(std::ceil((actor.max_health-f.terminalHealth)/(double(cambc::constants::spawn_healing)*actor.max_health)))+1;
        verify(out->job.projectedRenewalAndReturnTime==f.recallTurns+final_heal+int(out->plan.route->distance({bp_axis(0),bp_axis(0)}))+out->plan.route->distance(out->plan.decisionOrigin));
        std::printf("TWO_TRIP_RUNTIME_RECORD {\"kind\":\"accounting\",\"side\":%u,\"tau\":%d,\"firstLoss\":%.17g,\"escapeLoss\":%.17g,\"secondLoss\":%.17g,\"terminalHealth\":%.17g,\"projectedR\":%d}\n",bp_side(),f.tau,f.firstClearLoss,f.escapeLoss,f.secondClearLoss,f.terminalHealth,out->job.projectedRenewalAndReturnTime);
        return out;
    }
    verify(false);return nullptr;
}
__attribute__((noinline)) void affordable_low_health() {
    reset_case();auto c=std::make_unique<Campaign>(Controller::ledger,false,"two-power-low-health");arm(*c,0);initialize(*c);opening(*c,2);
    verify(actor.max_health==1125 && actor.strength==70 && actor.focus==100);
    auto builder=std::make_unique<tt::Builder>();const auto stats=c->capital_.exact_stats();verify(stats.has_value());
    const auto index=std::size_t(c->group_index(bp_key(1)));
    std::unique_ptr<tt::Candidate> full;
    for(unsigned callback=0;callback<16;++callback) {
        c->paths_.begin_turn();auto result=builder->prepare(c->personal_,index,*stats,c->paths_,[]{return true;});
        if(result.status==tt::Status::pending){advance_round();continue;}
        verify(result.status==tt::Status::ready && result.candidate);full=std::move(result.candidate);break;
    }
    verify(bool(full) && full->job.nominal()->deltaS==66 && full->plan.initial.memberCount==3);
    const auto& f=full->accounting;verify(f.terminalHealth>=actor.max_health*.25);
    // Lower only current health after immutable route preparation. There is no
    // owner-end/spawn-heal tick before the actual fresh Builder read below.
    const unsigned before_actions=actions;actor.health=800;
    const bool unsafe_first=actor.health-f.firstClearLoss<actor.max_health*.25;
    const bool unsafe_escape=actor.health-f.firstClearLoss-f.escapeLoss<actor.max_health*.25;
    verify(unsafe_first || unsafe_escape);
    const auto low=builder->prepare(c->personal_,index,*stats,c->paths_,[]{return true;});
    verify(low.status==tt::Status::unsupported && !std::strcmp(low.reason,"complete two trip not admitted") &&
        !low.candidate && actor.health==800 && actions==before_actions);
    std::printf("TWO_TRIP_RUNTIME_RECORD {\"kind\":\"affordable-low-health\",\"side\":%u,\"fundingBudget\":360,\"powerShards\":2,\"currentHealth\":800,\"maximumHealth\":%.17g,\"strength\":%.17g,\"focus\":%.17g,\"firstLoss\":%.17g,\"escapeLoss\":%.17g,\"fullTerminalHealth\":%.17g,\"unsafeFirst\":%s,\"unsafeEscape\":%s,\"status\":%u,\"reason\":\"%s\",\"nativeOpportunityClaim\":false}\n",bp_side(),double(actor.max_health),double(actor.strength),double(actor.focus),f.firstClearLoss,f.escapeLoss,f.terminalHealth,unsafe_first?"true":"false",unsafe_escape?"true":"false",unsigned(low.status),low.reason);
    ++low_health_cases;
}
__attribute__((noinline)) void probe(unsigned power,unsigned mode,unsigned interruption=0) {
    (void)interruption;
    reset_case();auto c=std::make_unique<Campaign>(Controller::ledger,false,"two-trip-sdk");arm(*c,mode);initialize(*c);opening(*c,power);
    auto candidate=prepare(*c,power?"fixed-opening-plus-four-power":"fixed-opening");
    if(!power || actor.type!=11){verify(!candidate);return;}
    verify(bool(candidate));
    advance_round();c->paths_.begin_turn();c->personal_.observe(cambc::vision::camps(),{});
    c->choose(c->information(),false,cambc::vision::camps());
    verify(c->active_ && c->active_->id==candidate->job.id && bool(c->two_trip_episode_));
    unsigned previous_phase=999;bool intervened=false,cut_seen=false,conversion_boundary=false;
    for(unsigned n=0;n<128 && c->active_;++n) {
        verify(c->telemetry_.completed==0 && c->telemetry_.converted==0);
        const auto phase=unsigned(c->two_trip_episode_->phase);
        if(phase!=previous_phase){std::printf("TWO_TRIP_RUNTIME_RECORD {\"kind\":\"phase\",\"side\":%u,\"mode\":%u,\"round\":%u,\"phase\":%u,\"health\":%.17g,\"bag\":%u,\"score\":%u}\n",bp_side(),mode,current_round,phase,actor.health,bag,own_score);previous_phase=phase;}
        if(!intervened && interruption==1 && c->two_trip_episode_->phase==tt::Phase::escape){++items[unsigned(cambc::item_type::power_shard_of_strength)];sync_stats();intervened=true;}
        if(!intervened && interruption==2 && c->two_trip_episode_->phase==tt::Phase::recall){actor.state&=~(1<<1);intervened=true;}
        const bool cut=!intervened && interruption==3 && c->two_trip_episode_->phase==tt::Phase::prefix;
        const unsigned before=actions;if(cut)fuel=44000;
        // Capture the macro's own channel-end boundary before the completion
        // callback is allowed to select and execute a subsequent ordinary job.
        if(own_score==66) {
            verify(!bag && recalls==1 && submissions==1 && captures==1 && healing_ticks>=1);
            conversion_boundary=true;
        }
        c->turn();
        if(cut){verify(actions==before && c->active_ && c->telemetry_.completed==0 && c->telemetry_.cutoffs>0);fuel=1000000;intervened=cut_seen=true;}
        if(intervened && (interruption==1 || interruption==2)){verify(!c->two_trip_episode_ && c->telemetry_.completed==0 && c->telemetry_.converted==0 && !captures && !own_score);trip_counters(*c,0,1);return;}
        if(cut_seen && !c->two_trip_episode_){verify(c->telemetry_.completed==0 && c->telemetry_.converted==0 && !captures && !own_score);trip_counters(*c,0,1);return;}
        if(c->telemetry_.completed)break;
        verify(bool(c->two_trip_episode_));advance_round();
    }
    verify(!c->two_trip_episode_ && c->telemetry_.completed==1 && c->telemetry_.converted==66 && own_score==66 && !bag);
    verify(conversion_boundary);++completed_paths;
    trip_counters(*c,1,0);
    verify(interruption!=3 || cut_seen);
}
#if defined(P3_TWO_INDEXER)
struct ServiceFixture {
    std::unique_ptr<Campaign> campaign;
    std::unique_ptr<tt::FreshServiceBuilder> builder;
    std::optional<tt::FreshService> fresh;
    JobForecast current;
    InformationState state;
    std::size_t index=0;
};
// Current death/reward states are explicit assumed observations. Future service
// must restore ALL public members instead of copying that easier current state.
std::unique_ptr<ServiceFixture> service_fixture(unsigned variant) {
    reset_case();auto out=std::make_unique<ServiceFixture>();
    out->campaign=std::make_unique<Campaign>(Controller::ledger,false,"indexer-service");
    auto& c=*out->campaign;arm(c,0);initialize(c);opening(c,variant==0?0:4);
    out->index=std::size_t(c.group_index(bp_key(1)));
    unsigned first=3;for(unsigned id=4;id<6;++id)
        if(bp_axis(bp_y[id])*8+bp_axis(bp_x[id])<bp_axis(bp_y[first])*8+bp_axis(bp_x[first]))first=id;
    if(variant<2){member_states[first]=0;member_health[first]=0;}
    else {for(unsigned id=3;id<6;++id){member_states[id]=0;member_health[id]=0;}group_state[1]=2;}
    if(variant==1){actor.x=bp_axis(0);actor.y=bp_axis(7);}
    out->builder=std::make_unique<tt::FreshServiceBuilder>();
    bool near_end=false;
    for(unsigned callback=0;callback<24;++callback) {
        c.paths_.begin_turn();c.personal_.observe(cambc::vision::camps(),{});out->state=c.information();
        const auto stats=c.capital_.exact_stats();verify(stats.has_value());
        auto context=c.forecast_context(out->state);
        auto fresh=out->builder->prepare(c.personal_,out->index,*stats,c.paths_,out->state,[]{return true;});
        verify(c.paths_.work_<=2);
        if(fresh.status==tt::Status::pending){advance_round();continue;}
        if(variant==0) {
            verify(fresh.status==tt::Status::unsupported && !fresh.service);
            // The actual current two-live-member job is executable even though
            // its future fully restored three-member service is unsupported.
            auto current=prepare(c,"known-dead-current-macro");verify(bool(current));
            c.personal_.observe(cambc::vision::camps(),{});out->state=c.information();
            auto joined=c.forecast_context(out->state);
            verify(!joined.clear(out->index,out->state.maximumHealth,0).admitted);
            plan3_indexer::Universe family;verify(family.advance(c.personal_,[]{return true;}));
            plan3_indexer::ServiceOverrides overrides;
            overrides[0].key=bp_key(1);overrides[0].status=tt::Status::unsupported;
            const std::vector<JobForecast> jobs{current->job};
            for(unsigned mode=0;mode<3;++mode) {
                const auto selected=plan3_indexer::select_menu(joined,family,jobs,mode,{},false,[]{return true;},false,&overrides);
                verify(selected.triple.state==plan3_indexer::State::known && !selected.triple.eligibleCount && !selected.intent.job);
            }
            std::printf("TWO_TRIP_RUNTIME_RECORD {\"kind\":\"service-rejected-fresh\",\"side\":%u,\"currentMacroAdmitted\":true,\"publicMembers\":3,\"currentLivingMembers\":2,\"freshStatus\":%u}\n",bp_side(),unsigned(fresh.status));
            ++service_cases;return out;
        }
        if(fresh.status!=tt::Status::ready)std::fprintf(stderr,"Fresh service unavailable: variant=%u status=%u reason=%s p=%d,%d\n",variant,unsigned(fresh.status),fresh.reason,actor.x,actor.y);
        verify(fresh.status==tt::Status::ready && fresh.service.has_value());
        out->current=context.acquisition(int(out->index));
        if(!admitted(out->state,out->current,false)) {
            const auto clear=context.clear(int(out->index),out->state.health);const auto& observed=c.personal_.observations[out->index];
            std::fprintf(stderr,"Current service not admitted: variant=%u complete=%d route=%d fullClear=%d outcomes=%zu clear=%d loss=%.17g observedHP=%.17g incoming=%.17g observationRound=%u/%u health=%.17g cutoff=%d travel=%d\n",variant,out->current.complete,out->current.routeKnown,out->current.fullClearAdmitted,out->current.outcomes.size(),clear.admitted,clear.healthLoss,observed.remaining_health,observed.incoming,observed.round,c.personal_.observation_round,out->state.health,context.cutoff,c.paths_.get(c.personal_.groups[out->index].field,{actor.x,actor.y}));
        }
        verify(admitted(out->state,out->current,false) && out->current.nominal()->deltaS==66);
        verify(!context.clear(out->index,out->state.maximumHealth,0).admitted);
        if(variant==2 && !near_end) {
            const int tau=out->current.nominal()->tau;verify(tau>0 && tau<128);
            current_round=unsigned(cambc::constants::max_rounds)-unsigned(tau);near_end=true;continue;
        }
        out->fresh=std::move(fresh.service);return out;
    }
    verify(false);return nullptr;
}
void empty_triple(const plan3_indexer::Triple& triple) {
    verify(triple.state==plan3_indexer::State::cutoff && !triple.rowCount && !triple.eligibleCount &&
        !triple.unknownCount && !triple.rejectedCount && !triple.active.present && !triple.rate.present &&
        !triple.circuit.present && !triple.projectedCount);
}
__attribute__((noinline)) void service_features() {
    auto rejected=service_fixture(0);rejected.reset();
    for(unsigned variant=1;variant<=2;++variant) {
        auto fixture=service_fixture(variant);auto& c=*fixture->campaign;
        const auto& fresh=*fixture->fresh;const auto& f=fresh.accounting;
        int charged=f.firstClearTurns+f.secondClearTurns+f.recallTurns+f.healingTurns+f.captureTurns+f.tributeTurns;
        for(const auto leg:f.legTurns)charged+=leg;
        verify(fresh.Cfresh==charged && f.deltaS==66 && !f.oneTripAdmitted);
        verify(fresh.decisionOrigin!=fresh.combatOrigin && fresh.QToP>0);
        if(variant==1) {
            // A living-only stationary origin can miss the restored dead
            // member. The published fresh origin must cover all three.
            const int dead_key=bp_key(1);
            const cambc::vec2 living_only{dead_key%8+(dead_key%8<4?actor.attack_range:-actor.attack_range),dead_key/8+actor.attack_range+1};
            unsigned excluded=0;
            for(unsigned id=3;id<6;++id) {
                const cambc::vec2 at{bp_axis(bp_x[id]),bp_axis(bp_y[id])};
                verify(tt::distance(fresh.combatOrigin,at)<=actor.attack_range);
                if(!member_states[id])excluded+=tt::distance(living_only,at)>actor.attack_range;
                else verify(tt::distance(living_only,at)<=actor.attack_range);
            }
            verify(excluded==1 && fresh.combatOrigin!=living_only);
        }
        const auto final_heal=[&](double health){return health>=actor.max_health?0:int(std::ceil((actor.max_health-health)/(double(cambc::constants::spawn_healing)*actor.max_health)))+1;};
        verify(fresh.freshHealingTurns==final_heal(f.terminalHealth));
        verify(fresh.Rfresh==fresh.effectiveRecall+fresh.freshHealingTurns+fresh.maximumSpawnToQ+fresh.QToP);
        const auto bound=tt::bind_current(fresh,fixture->current,fixture->state);
        verify(bound.status==tt::Status::ready && bound.service.has_value());
        const auto& service=*bound.service;
        verify(service.currentJob==fixture->current.id && service.currentHealingTurns==final_heal(fixture->current.nominal()->terminal.health));
        verify(service.Rcurrent==fresh.effectiveRecall+service.currentHealingTurns+fresh.maximumSpawnToQ+fresh.QToP);
        auto not_quiet=fresh;not_quiet.assumedQuiet=false;
        const auto unsupported=tt::bind_current(not_quiet,fixture->current,fixture->state);
        verify(unsupported.status==tt::Status::unknown && !unsupported.service);
        auto hint=std::make_unique<tt::Candidate>();
        hint->plan.groupKey=fresh.groupKey;hint->plan.preparedRound=fresh.preparedRound;
        hint->plan.decisionOrigin=fresh.decisionOrigin;hint->plan.origin=fresh.combatOrigin;
        hint->plan.attackRange=actor.attack_range;hint->plan.route=fixture->builder->route_;
        auto hinted_builder=std::make_unique<tt::FreshServiceBuilder>();
        const auto hinted=hinted_builder->prepare(c.personal_,fixture->index,*c.capital_.exact_stats(),c.paths_,fixture->state,[]{return true;},hint.get());
        verify(hinted.status==tt::Status::ready && hinted.service);
        verify(hinted.service->combatOrigin==fresh.combatOrigin && hinted.service->endpoint==fresh.endpoint &&
            hinted.service->altarTile==fresh.altarTile && hinted.service->Cfresh==fresh.Cfresh && hinted.service->Rfresh==fresh.Rfresh);
        fixture->current.projectedRenewalAndReturnTime=service.Rcurrent;
        plan3_indexer::Universe family;verify(family.advance(c.personal_,[]{return true;}));
        plan3_indexer::ServiceOverrides overrides;overrides[0]={bp_key(1),tt::Status::ready,false,service};
        const std::vector<JobForecast> jobs{fixture->current};
        auto context=c.forecast_context(fixture->state);
        const auto mapped=plan3_indexer::map_menu(context,family,jobs,[]{return true;},&overrides);
        int slot=-1;for(unsigned i=0;i<family.count;++i)if(family.keys[i]==bp_key(1))slot=int(i);
        verify(slot>=0);const auto& row=mapped.input.projects[unsigned(slot)];
        verify(row.state==plan3_indexer::State::known && row.Cfresh==fresh.Cfresh && row.Rfresh==fresh.Rfresh && row.Rcurrent==service.Rcurrent);
        const auto& geometry=mapped.geometry[unsigned(slot)];
        verify(geometry.originTile==fresh.decisionOrigin.y*8+fresh.decisionOrigin.x &&
            geometry.endpointTile==fresh.endpoint.y*8+fresh.endpoint.x &&
            geometry.currentEndpointTile==service.currentEndpoint.y*8+service.currentEndpoint.x);
        int altar_field=-1;for(std::size_t a=0;a<c.personal_.altars.size();++a)
            if(c.personal_.altars[a].y*8+c.personal_.altars[a].x==fresh.altarTile)altar_field=c.personal_.altar_fields[a];
        verify(altar_field>=0 && geometry.altarField==altar_field);
        const auto period=std::max({row.tau,row.Cfresh+row.Rfresh,row.Cfresh+row.Rcurrent,row.Cfresh+row.regenMax});
        const auto duration=row.tau+row.Rcurrent;
        const auto horizon=fixture->state.horizon();verify(row.tau<=horizon);
        if(variant==2)verify(row.tau==horizon && horizon<row.Cfresh);
        for(unsigned mode=0;mode<3;++mode) {
            unsigned calls=0;const auto selected=plan3_indexer::select_menu(context,family,jobs,mode,{},false,[&]{++calls;return true;},false,&overrides);
            verify(selected.triple.state==plan3_indexer::State::known && selected.triple.eligibleCount==1 && selected.intent.job==fixture->current.id);
            const auto& ranked=selected.triple.rows[unsigned(slot)];
            verify(ranked.P==period && ranked.D==duration && ranked.N==1+(horizon-row.tau)/period);
            for(unsigned cut=0;cut<calls;++cut) {
                unsigned seen=0;const auto stopped=plan3_indexer::select_menu(context,family,jobs,mode,{},false,[&]{return seen++!=cut;},false,&overrides);
                empty_triple(stopped.triple);verify(!stopped.intent.job && !stopped.companion.job);
            }
            std::printf("TWO_TRIP_RUNTIME_RECORD {\"kind\":\"indexer-service\",\"side\":%u,\"variant\":%u,\"mode\":%u,\"round\":%u,\"H\":%d,\"tau\":%lld,\"Cfresh\":%lld,\"Rfresh\":%lld,\"Rcurrent\":%lld,\"regenMax\":%lld,\"P\":%u,\"D\":%u,\"N\":%u,\"p\":[%d,%d],\"Q\":[%d,%d],\"freshEndpoint\":[%d,%d],\"currentEndpoint\":[%d,%d],\"freshAltar\":%d,\"maximumSpawnToQ\":%d,\"QToP\":%d,\"recall\":%d,\"freshHealing\":%d,\"currentHealing\":%d,\"freshTerminalHealth\":%.17g,\"currentTerminalHealth\":%.17g,\"maxHealth\":%.17g,\"spawnHealing\":%.17g,\"freshTerms\":[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d],\"readyCuts\":%u}\n",
                bp_side(),variant,mode,current_round,horizon,(long long)row.tau,(long long)row.Cfresh,(long long)row.Rfresh,(long long)row.Rcurrent,(long long)row.regenMax,ranked.P,ranked.D,ranked.N,fresh.decisionOrigin.x,fresh.decisionOrigin.y,fresh.combatOrigin.x,fresh.combatOrigin.y,fresh.endpoint.x,fresh.endpoint.y,service.currentEndpoint.x,service.currentEndpoint.y,fresh.altarTile,fresh.maximumSpawnToQ,fresh.QToP,fresh.effectiveRecall,fresh.freshHealingTurns,service.currentHealingTurns,f.terminalHealth,fixture->current.nominal()->terminal.health,actor.max_health,double(cambc::constants::spawn_healing),f.firstClearTurns,f.secondClearTurns,f.recallTurns,f.healingTurns,f.captureTurns,f.tributeTurns,f.legTurns[0],f.legTurns[1],f.legTurns[2],f.legTurns[3],f.legTurns[4],f.legTurns[5],calls);
        }
        auto wrong=overrides;++wrong[0].service->currentJob.altarTile;
        const auto unmatched=plan3_indexer::map_menu(context,family,jobs,[]{return true;},&wrong);
        verify(unmatched.input.projects[unsigned(slot)].state==plan3_indexer::State::unknown);
        const auto cut=fixture->builder->prepare(c.personal_,fixture->index,*c.capital_.exact_stats(),c.paths_,fixture->state,[]{return false;});
        verify(cut.status==tt::Status::cutoff && !cut.service);
        unsigned publication_calls=0;
        const auto complete=fixture->builder->prepare(c.personal_,fixture->index,*c.capital_.exact_stats(),c.paths_,fixture->state,[&]{++publication_calls;return true;});
        verify(complete.status==tt::Status::ready && complete.service && publication_calls>0);
        unsigned visited=0;
        const auto denied=fixture->builder->prepare(c.personal_,fixture->index,*c.capital_.exact_stats(),c.paths_,fixture->state,[&]{return ++visited!=publication_calls;});
        verify(denied.status==tt::Status::cutoff && !denied.service);
        ++service_cases;
    }
    // A metadata-only cap fixture: nine copied group records with declared
    // distinct keys do not claim nine physical SDK groups in this8x8 world.
    reset_case();auto c=std::make_unique<Campaign>(Controller::ledger,false,"fixed-eight");initialize(*c);
    auto personal=std::make_unique<PersonalInformation>(c->personal_);
    personal->groups.clear();for(int key=0;key<9;++key){auto group=c->personal_.groups[0];group.key=key;group.gem_value=66;personal->groups.push_back(std::move(group));}
    plan3_indexer::Universe capped;verify(capped.advance(*personal,[]{return true;}));
    verify(capped.count==8 && capped.keys[0]==0 && capped.keys[7]==7 && !capped.contains(8));
    auto input=capped.physical(int(current_round),int(current_round),int(cambc::constants::max_rounds),8,8);
    input.projectCount=8;for(unsigned i=0;i<8;++i){input.projects[i].key=int(i);input.projects[i].state=plan3_indexer::State::unavailable;}
    const auto triple=plan3_indexer::evaluate(input);verify(triple.state==plan3_indexer::State::known && triple.rowCount==8 && !triple.eligibleCount);++service_cases;
}
__attribute__((noinline)) void dead_observation() {
    for(unsigned availability=0;availability<5;++availability) {
        reset_case();auto c=std::make_unique<Campaign>(Controller::ledger,false,"dead-observation");initialize(*c);opening(*c,4);
        unsigned dead=3;for(unsigned id=4;id<6;++id)
            if(bp_axis(bp_y[id])*8+bp_axis(bp_x[id])<bp_axis(bp_y[dead])*8+bp_axis(bp_x[dead]))dead=id;
        if(availability!=4){member_states[dead]=0;member_health[dead]=0;}
        if(availability==1 || availability==2) {
            camp_state_error_id=dead;
            camp_state_error_code=int(availability==1?cambc::error_code::api_not_in_vision:cambc::error_code::api_not_found);
        }
        if(availability==3)visible[0]&=~(std::uint64_t{1}<<(bp_axis(bp_y[dead])*8+bp_axis(bp_x[dead])));
        auto camps=cambc::vision::camps();
        if(availability==4)camps.erase(std::remove_if(camps.begin(),camps.end(),[&](const auto& camp){return unsigned(static_cast<cambc::handle_camp_t>(camp))==dead;}),camps.end());
        camp_state_calls.fill(0);c->personal_.observe(camps,{});
        const auto index=std::size_t(c->group_index(bp_key(1)));const auto& observed=c->personal_.observations[index];
        verify(observed.round==current_round && observed.lifecycle==Lifecycle::alive);
        const auto& group=c->personal_.groups[index];
        if(availability==0)verify(observed.remaining_health==2700 && observed.incoming==40 && camp_state_calls[dead]>0);
        else verify(observed.remaining_health==group.full_health && observed.incoming==group.incoming);
        if(availability!=0)verify(!observed.all_members_visible);
        if(availability==3)verify(camp_state_calls[dead]==0);
        std::printf("TWO_TRIP_RUNTIME_RECORD {\"kind\":\"dead-observation\",\"side\":%u,\"availability\":%u,\"nativeDeadId\":%u,\"health\":%.17g,\"incoming\":%.17g,\"deadStateQueries\":%u}\n",bp_side(),availability,dead,observed.remaining_health,observed.incoming,camp_state_calls[dead]);
        ++service_cases;
    }
}
#endif
__attribute__((noinline)) void boundaries() {
    reset_case();auto c=std::make_unique<Campaign>(Controller::ledger,false,"two-trip-boundary");arm(*c,0);initialize(*c);opening(*c,4);
    auto candidate=prepare(*c,"boundary-source");verify(bool(candidate));
    for(unsigned mode=0;mode<8;++mode) {
        auto plan=std::make_unique<tt::Plan>(candidate->plan);
        const auto saved=actor;const auto saved_items=items;const auto saved_health=member_health;const auto saved_visible=visible;const unsigned saved_round=current_round;
        if(mode==1){++items[unsigned(cambc::item_type::power_shard_of_strength)];sync_stats();}
        if(mode==2){plan->started=true;plan->lastRound=current_round-2;}
        if(mode==3){plan->phase=tt::Phase::prefix;actor.x=plan->origin.x;actor.y=plan->origin.y;member_health[plan->firstMemberId]-=1;}
        if(mode==4){fixture_stunned=true;}
        if(mode==5){plan->phase=tt::Phase::returning;plan->prefixWitness=true;plan->prefixRound=current_round;current_round+=40;}
        if(mode==6){set(visible.data(),bp_axis(bp_x[plan->firstMemberId]),bp_axis(bp_y[plan->firstMemberId]),false);}
        if(mode==7){plan->phase=tt::Phase::healing;actor.x=bp_axis(2);actor.y=bp_axis(0);}
        const unsigned before=actions;
        const auto r=tt::step(*plan,*c,c->personal_,c->paths_,cambc::vision::camps(),{},[&]{return mode!=0;});
        if(r.status!=(mode==0?tt::Status::cutoff:tt::Status::abort))std::fprintf(stderr,"Boundary mode%u status%u reason%s\n",mode,unsigned(r.status),r.reason);
        verify(r.status==(mode==0?tt::Status::cutoff:tt::Status::abort));verify(actions==before && !captures && !own_score);
        actor=saved;items=saved_items;member_health=saved_health;visible=saved_visible;current_round=saved_round;fixture_stunned=false;++boundary_cases;
    }
    // Exact SDK wrapper uses packed bytes. Bit9 crosses byte0 and blocks the
    // only two-step diagonal path. No path may go through the fixed wall.
    cambc::bitboard walls;walls.set({1,1});StaticPaths tickets;tickets.initialize(cambc::bitboard{});
    auto field=std::make_unique<tt::RouteField>(walls,cambc::vec2{0,0});
    for(unsigned n=0;!field->ready() && n<4;++n){tickets.begin_turn();field->advance(tickets,[]{return true;});}
    verify(field->ready() && field->distance({1,1})==-1 && field->distance({2,2})==3);
    std::array<cambc::vec2,129> route{};unsigned count=0;verify(field->from_origin({2,2},route,count));
    for(unsigned i=0;i<count;++i)verify(route[i]!=cambc::vec2{1,1});++boundary_cases;
}
#if !defined(P3_TWO_INDEXER)
__attribute__((noinline)) void pending_preparation() {
    reset_case();
    // Both real public compositions start asleep. The first group's geometry is
    // unsupported. One previously spent ticket forces a real resumable boundary.
    group_state[0]=1;
    for(unsigned i=0;i<3;++i){member_states[i]=2;cambc::detail::camp_design d{};std::memcpy(&d,bp_config::camps[0].data(),sizeof(d));member_health[i]=d.base_stats.base_health;}
    auto c=std::make_unique<Campaign>(Controller::ledger,false,"pending-two-trip");arm(*c,0);initialize(*c);opening(*c,4);
    c->paths_.begin_turn();verify(c->paths_.take_work(65000));
    const unsigned before=actions;c->choose(c->information(),false,cambc::vision::camps());
    verify(c->active_ && opening_bootstrap::single_gold_eligible(c->information(),*c->active_));
    verify(!c->two_trip_episode_ && actions==before && c->two_trip_menu_.builder_.route_ && !c->two_trip_menu_.builder_.route_->ready());
    const auto ordinary_id=c->active_->id;
    // A legal protected recall callback still advances existing immutable BFS;
    // it publishes neither a fresh candidate nor a paired decision.
    cambc::self().recall();advance_round();c->turn();
    verify(c->active_ && c->active_->id==ordinary_id && !c->two_trip_episode_ && c->two_trip_menu_.builder_.route_->ready());
    verify(c->telemetry_.two_trip_started==0 && !c->telemetry_.completed);
    std::printf("TWO_TRIP_RUNTIME_RECORD {\"kind\":\"pending-with-ordinary\",\"side\":%u,\"ordinarySelected\":true,\"privateRouteReady\":true,\"twoTripStarted\":0}\n",bp_side());++boundary_cases;
}
#endif
void ordinary_gate_predicate() {
    InformationState state;state.round=32;state.roundLimit=232;state.health=state.maximumHealth=1000;
    JobForecast gold_job;gold_job.id={JobKind::cycle,10,-1,1,-1};gold_job.rewardOnlyEndpoint=true;gold_job.campGold=105;
    Outcome outcome;outcome.tau=10;gold_job.outcomes.push_back(outcome);
    for(const auto status:{tt::Status::pending,tt::Status::cutoff}) {
        verify(!tt::Menu::defer_selection(status,state,{gold_job}));
        verify(tt::Menu::defer_selection(status,state,{}));
        for(unsigned invalid=0;invalid<7;++invalid) {
            auto bad=gold_job;
            if(invalid==0)bad.id.kind=JobKind::double_cycle;
            if(invalid==1)bad.id.secondGroup=20;
            if(invalid==2)bad.complete=false;
            if(invalid==3)bad.routeKnown=false;
            if(invalid==4)bad.campGold=0;
            if(invalid==5)bad.projectedRenewalAndReturnTime=-1;
            if(invalid==6)bad.outcomes[0].tau=201;
            verify(tt::Menu::defer_selection(status,state,{bad}));
        }
        auto productive=gold_job;productive.outcomes[0].deltaS=1;verify(!tt::Menu::defer_selection(status,state,{productive}));
        auto safety=gold_job;safety.id.kind=JobKind::renew;safety.mandatorySafety=true;verify(!tt::Menu::defer_selection(status,state,{safety}));
        state.carriedGems=1;verify(tt::Menu::defer_selection(status,state,{gold_job}));state.carriedGems=0;
        ++optional_gate_cases;
    }
    for(const auto status:{tt::Status::ready,tt::Status::unsupported,tt::Status::unknown})verify(!tt::Menu::defer_selection(status,state,{}));
}
__attribute__((noinline)) void cached_actor_guard() {
    reset_case();auto c=std::make_unique<Campaign>(Controller::ledger,false,"cached-actor-guard");arm(*c,0);initialize(*c);
    tt::Menu extension;const auto state=c->information();const auto stats=c->capital_.exact_stats();
    const auto prefix=tt::Menu::single_prefix(c->personal_,c->paths_,{actor.x,actor.y});
    unsigned ready_calls=0;query_trace.clear();
    const auto status=extension.prepare_early(c->personal_,stats,c->paths_,state,prefix,[&]{++ready_calls;return false;});
    const bool tavi=actor.type==11;
    verify(status==(tavi?tt::Status::cutoff:tt::Status::unsupported) && ready_calls==unsigned(tavi) && query_trace.empty() && !extension.candidate_ && !extension.owned_);++optional_gate_cases;
}
#if defined(P3_TWO_BACKPRESSURE)
Campaign* ordinary_campaign=nullptr;bool ordinary_latched=false;std::int64_t ordinary_tail=46200;
std::int64_t after_ordinary_menu() {
    if(ordinary_campaign && ordinary_campaign->planning_trace_.current_.round==current_round &&
       ordinary_campaign->planning_trace_.current_.fuel[unsigned(PlanningGateTrace::Point::menu)]>=0)ordinary_latched=true;
    return ordinary_latched?ordinary_tail:-1;
}
__attribute__((noinline)) void ordinary_gold_tail(unsigned mode) {
    std::optional<JobId> healthy;
    for(unsigned variant=0;variant<4;++variant) {
        reset_case();group_state[2]=2;
        auto c=std::make_unique<Campaign>(Controller::ledger,false,"ordinary-gold-tail");arm(*c,mode);initialize(*c);
        const auto state=c->information();const auto camps=cambc::vision::camps();auto context=c->forecast_context(state);
#if defined(P3_TWO_INDEXER)
        const auto ordinary=c->menu(context,false,camps);
#else
        const auto prefix=tt::Menu::single_prefix(c->personal_,c->paths_,cambc::self().position());
        const auto ordinary=c->menu(context,false,camps,prefix);
#endif
        const auto canonical=canonical_menu(ordinary);
        const auto* gold_job=opening_bootstrap::single_gold(state,canonical.jobs);
        verify(!context.cutoff && gold_job && c->personal_.any_gems && raw_gems[1]>0);
        const auto expected=gold_job->id;
        const bool old_would_defer=std::none_of(ordinary.begin(),ordinary.end(),[&](const auto& job){return admitted(state,job,false) && job.nominal()->deltaS>0;});
        verify(old_would_defer && !tt::Menu::defer_selection(tt::Status::cutoff,state,ordinary));
        // Allowing ordinary ranking does not override its unknown-gem gate.
        const auto unknown=plan3_backpressure::select_menu(c->personal_,state,ordinary,mode!=0,false,true,c->context_,true,[]{return true;});
        verify(unknown.intent.status!=IntentStatus::selected && unknown.companion.status!=IntentStatus::selected);
        if(variant==3){actor.state|=1<<1;recall_timer=3;}
        ordinary_campaign=c.get();ordinary_latched=false;ordinary_tail=variant==2?44999:46200;
        if(variant)optional_fuel_probe=after_ordinary_menu;
        std::printf("OPTIONAL_GOLD_BEGIN {\"mode\":%u,\"variant\":%u}\n",mode,variant);
        c->turn();optional_fuel_probe=nullptr;ordinary_campaign=nullptr;
        const bool selected=variant<2;
        verify(bool(c->active_)==selected && !c->two_trip_episode_ && !c->telemetry_.two_trip_started);
        if(selected){verify(c->active_->id==expected && c->active_->id.secondGroup<0 && c->telemetry_.activations==1 && c->telemetry_.disagreements==0);if(!healthy)healthy=c->active_->id;else verify(c->active_->id==*healthy);}
        else verify(!actions && !moves);
        verify(ordinary_latched==(variant==1 || variant==2));
        if(variant==1 || variant==2)verify(c->planning_trace_.current_.fuel[unsigned(PlanningGateTrace::Point::extended)]==ordinary_tail);
        if(variant==1)verify(c->planning_trace_.current_.fuel[unsigned(PlanningGateTrace::Point::paired)]==ordinary_tail);
        verify(c->conversion_snapshot_.valid && c->conversion_snapshot_.round==current_round && c->conversion_snapshot_.scoreRound==current_round);
        std::printf("OPTIONAL_GOLD_RESULT {\"mode\":%u,\"variant\":%u,\"selected\":%s,\"latched\":%s,\"oldWouldDefer\":true,\"publicGemStock\":%u,\"expectedJob\":[%d,%d,%d,%d,%d]}\n",mode,variant,selected?"true":"false",ordinary_latched?"true":"false",raw_gems[1],int(expected.kind),expected.firstGroup,expected.secondGroup,expected.altarTile,expected.equipmentBlock);++optional_gate_cases;
    }
}
#endif
void single_gold_horizon() {
    InformationState state;state.round=32;state.roundLimit=232;state.health=state.maximumHealth=1000;state.winningGems=1000;
    std::vector<JobForecast> jobs(3);
    for(unsigned i=0;i<3;++i){auto& j=jobs[i];j.id={i==1?JobKind::double_cycle:JobKind::cycle,int(10+i),i==1?20:-1,1,-1};j.rewardOnlyEndpoint=true;j.campGold=i==0?1:i==1?5:1000;Outcome o;o.tau=i==0?5:i==1?8:129;j.outcomes.push_back(o);}
    JobIntent a{IntentStatus::selected,jobs[1].id,1,DecisionReason::gold_fallback},b=a;
    verify(opening_bootstrap::prefer_single(state,jobs,a,b,true) && a.job==jobs[0].id && b.job==a.job);
    a={IntentStatus::selected,jobs[1].id,1,DecisionReason::gold_fallback};b=a;
    verify(opening_bootstrap::prefer_single(state,jobs,a,b,false) && a.job==jobs[2].id && b.job==a.job);++bootstrap_cases;
}
void pending_flag(Campaign& c,bool value) {
#if defined(P3_TWO_INDEXER)
    c.indexer_bootstrap_pending_shop_=value;
#else
    c.bootstrap_pending_shop_=value;
#endif
}
__attribute__((noinline)) void shop_return() {
    reset_case();auto c=std::make_unique<Campaign>(Controller::ledger,false,"opening-return");arm(*c,0);initialize(*c);
    actor.x=bp_axis(0);actor.y=bp_axis(7);gold=30;
    bool pending=true;verify(!opening_bootstrap::return_needed(pending,*c,c->personal_,true));
    bag=1;verify(!opening_bootstrap::return_needed(pending,*c,c->personal_,false));bag=0;
    pending_flag(*c,true);c->turn();
    if(actor.type==11){verify(buys==1 && c->monster_shards()==1 && recalls==0);}
    else {
        verify(recalls==1 && (actor.state&(1<<1)) && buys==0);
        bool arrived=false;for(unsigned n=0;n<8 && !buys;++n){advance_round();arrived|=on_spawn();c->turn();}
        verify(arrived && buys==1 && c->monster_shards()==1);
    }
    ++bootstrap_cases;
    reset_case();c=std::make_unique<Campaign>(Controller::ledger,false,"protected-return");arm(*c,0);initialize(*c);
    actor.x=bp_axis(0);actor.y=bp_axis(7);gold=30;pending_flag(*c,true);actor.state|=1<<1;recall_timer=3;
    const unsigned before=actions;c->turn();verify(actions==before && !buys && !moves && !recalls);++bootstrap_cases;
    reset_case();c=std::make_unique<Campaign>(Controller::ledger,false,"finished-opening");arm(*c,0);initialize(*c);opening(*c,0);
    actor.x=bp_axis(0);actor.y=bp_axis(7);pending=true;gold=30;
    verify(!opening_bootstrap::return_needed(pending,*c,c->personal_,false) && !pending);++bootstrap_cases;
}
__attribute__((noinline)) void gold_capture_to_shop(unsigned mode) {
    reset_case();all_gold_map=true;raw_gems={0,0,0};raw_gold={105,105,105};group_state={2,2,2};member_states.fill(0);member_health.fill(0);
    auto c=std::make_unique<Campaign>(Controller::ledger,false,"gold-capture-return");arm(*c,mode);initialize(*c);
    for(unsigned n=0;n<64 && !buys;++n){c->turn();if(buys)break;advance_round();}
    verify(captures==1 && c->telemetry_.completed==1 && !c->telemetry_.converted && !own_score && !bag && buys==1 && c->monster_shards()==3);
    verify(recalls==(actor.type==11?0u:1u));
    std::printf("TWO_TRIP_RUNTIME_RECORD {\"kind\":\"gold-capture-to-shop\",\"side\":%u,\"mode\":%u,\"actor\":%d,\"captures\":%u,\"completed\":%u,\"buys\":%u,\"monsterShards\":%u,\"recalls\":%u}\n",bp_side(),mode,actor.type,captures,c->telemetry_.completed,buys,c->monster_shards(),recalls);++bootstrap_cases;
}
}
namespace cambc::detail {extern "C" {
std::int32_t sdk_hypothetical_vision(std::int32_t type,std::int32_t x,std::int32_t y,std::uint64_t* out){record("sdk_hypothetical_vision");verify(type==actor.type && x>=0 && x<8 && y>=0 && y<8);*out=~std::uint64_t{0};return 0;}
std::int32_t sdk_champion_buff_data(std::uint32_t id,buff_data* out,std::uint32_t capacity,std::uint32_t* count){record("sdk_champion_buff_data");verify(id==0);*count=fixture_stunned?1:0;if(*count && capacity){out[0]={};out[0].type=unsigned(buff_kind::stun);out[0].data.stun.duration=1;}return 0;}
std::int32_t sdk_map_camp_group_num_camps(std::uint32_t group,std::uint32_t* out){record("sdk_map_camp_group_num_camps");verify(group<3);*out=3;return 0;}
std::int32_t sdk_champion_cooldown(std::uint32_t id,std::uint32_t slot,std::int32_t* out){record("sdk_champion_cooldown");verify(id==0 && slot<2);*out=int(cooldown[slot]);return 0;}
std::int32_t sdk_attack(std::uint32_t kind,std::uint32_t id){
    record("sdk_attack");verify(kind==2 && id<9 && (actor.state&1) && (actor.state&(1<<4)) && !(actor.state&((1<<1)|(1<<2)|(1<<12))));
    verify(std::max(std::abs(actor.x-bp_axis(bp_x[id])),std::abs(actor.y-bp_axis(bp_y[id])))<=actor.attack_range);
    damage(id,actor.strength*camp_damage);actor.state&=~((1<<3)|(1<<4));++attacks;++actions;return 0;
}
std::int32_t sdk_cast(std::uint32_t slot,std::uint32_t kind,std::uint32_t id,std::int32_t x,std::int32_t y,std::int32_t* success){
    record("sdk_cast");verify(actor.type==11 && slot<2 && (actor.state&(1<<11)) && !cooldown[slot] && !(actor.state&((1<<1)|(1<<2)|(1<<12))));
    champion_design design{};std::memcpy(&design,bp_config::champions[11].data(),sizeof(design));
    const auto& ability=design.definition.abilities[slot];
    if(slot==0){verify(ability.kind==1 && (ability.targets&ability_target_self) && kind==0 && id==0 && x==-1 && y==-1);actor.state|=1<<4;}
    else {
        verify(ability.kind==4);
        verify(ability.targets&ability_target_camps);
        if(!(kind==2 && id<9 && x==-1 && y==-1))std::fprintf(stderr,"Cast target mismatch: slot=%u kind=%u id=%u x=%d y=%d\n",slot,kind,id,x,y);
        verify(kind==2 && id<9 && x==-1 && y==-1);
        verify(std::max(std::abs(actor.x-bp_axis(bp_x[id])),std::abs(actor.y-bp_axis(bp_y[id])))<=ability.payload.cc.range);
        damage(id,actor.focus*ability.payload.cc.modifier*camp_damage);stuns[id]=unsigned(ability.payload.cc.duration);actor.state&=~(1<<3);
    }
    actor.state&=~(1<<11);cooldown[slot]=unsigned(ability.cooldown);*success=1;++casts;++actions;return 0;
}
}}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const unsigned modes=
#if defined(P3_TWO_BACKPRESSURE)
        2;
#elif defined(P3_TWO_INDEXER)
        3;
#else
        1;
#endif
    const char* name=std::getenv("P3_BP_ACTOR");const bool varg=name && !std::strcmp(name,"varg");
    if(!varg){
#if defined(P3_TWO_INDEXER)
        dead_observation();
        service_features();
#endif
        for(unsigned funding:{360u,420u})for(unsigned mode=0;mode<modes;++mode)cold_startup(mode,funding);
        affordable_low_health();
#if !defined(P3_TWO_INDEXER)
        menu_merge_boundaries();
#endif
        for(unsigned mode=0;mode<modes;++mode){probe(0,mode);probe(4,mode);}boundaries();
#if !defined(P3_TWO_INDEXER)
        probe(4,0,1);probe(4,0,2);probe(4,0,3);pending_preparation();
#endif
    }
    ordinary_gate_predicate();cached_actor_guard();single_gold_horizon();shop_return();for(unsigned mode=0;mode<modes;++mode)gold_capture_to_shop(mode);
#if defined(P3_TWO_BACKPRESSURE)
    for(unsigned mode=0;mode<modes;++mode)ordinary_gold_tail(mode);
#endif
    std::printf("TWO_TRIP_RUNTIME_RESULT {\"status\":\"pass\",\"side\":%u,\"actor\":\"%s\",\"checks\":%u,\"scenarios\":%u,\"completedPaths\":%u,\"boundaryCases\":%u,\"bootstrapCases\":%u,\"optionalGateCases\":%u,\"coldCases\":%u,\"coldCompletedPaths\":%u,\"cold360CompletedPaths\":%u,\"cold420CompletedPaths\":%u,\"lowHealthCases\":%u,\"serviceCases\":%u,\"engineLaunches\":0}\n",bp_side(),varg?"varg":"tavi",checks,scenarios,completed_paths,boundary_cases,bootstrap_cases,optional_gate_cases,cold_cases,cold_paths,cold_funded_paths[0],cold_funded_paths[1],low_health_cases,service_cases);
}
