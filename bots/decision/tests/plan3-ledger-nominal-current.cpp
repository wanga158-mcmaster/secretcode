#include <array>
namespace {extern std::array<unsigned,3> respawn_count;}
bool sortie_dynamic_respawn=false;
int (*sortie_champion_probe)(unsigned,void*)=nullptr;
void (*sortie_alive_probe)()=nullptr;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main unused_two_trip_fixture_main
#include "plan3-two-trip-runtime-sdk.cpp"
#undef main
#pragma clang diagnostic pop
// Current SDK3.0.4 imports used by the inherited8x8 explicit host.
namespace cambc::detail {extern "C" {
std::int32_t sdk_camp_buffs(std::uint32_t id,camp_buff_data*,std::uint32_t,std::uint32_t* count) {
    camp_state current{};const auto code=sdk_camp(id,&current);if(code)return code;*count=0;return 0;
}
std::int32_t sdk_DistanceField_Get(const std::uint16_t* field,std::int32_t x,std::int32_t y,std::uint16_t* out) {
    verify(x>=0 && y>=0 && x<8 && y<8);*out=field[y*8+x];return 0;
}
std::int32_t sdk_DistanceField_ComputeToWithExpansionCount(const std::uint64_t* walls,const std::uint64_t* roots,
        const std::uint64_t* destinations,std::uint64_t* visited,std::uint16_t* field,std::uint16_t max_distance,std::uint16_t* count) {
    verify(!(*walls&*roots));std::fill_n(field,64,std::uint16_t{65535});std::array<unsigned,64> queue{};unsigned head=0,tail=0;*visited=0;*count=0;
    for(unsigned n=0;n<64;++n)if((*roots>>n)&1u){field[n]=0;queue[tail++]=n;*visited|=std::uint64_t{1}<<n;}
    while(head<tail && (!*destinations || (*visited&*destinations)!=*destinations)) {
        const auto at=queue[head++];++*count;if(field[at]>=max_distance)continue;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const int x=int(at%8)+dx,y=int(at/8)+dy;if((!dx&&!dy) || x<0 || y<0 || x>=8 || y>=8)continue;
            const auto next=unsigned(y*8+x);if(((*walls>>next)&1u) || field[next]!=65535)continue;
            field[next]=std::uint16_t(field[at]+1);verify(tail<64);queue[tail++]=next;*visited|=std::uint64_t{1}<<next;
        }
    }
    return 0;
}
}}

static unsigned nchecks=0;
static void nc(bool value,int line){++nchecks;if(!value){std::fprintf(stderr,"Nominal acquisition check failed at%d\n",line);std::exit(1);}}
#define NC(...) nc(bool(__VA_ARGS__),__LINE__)
static void key(const JobId& j){std::printf("[%d,%d,%d,%d,%d]",int(j.kind),j.firstGroup,j.secondGroup,j.altarTile,j.equipmentBlock);}
static void setup() {
    reset_case();group_state={2,2,3};member_states.fill(0);member_health.fill(0);raw_gems={50,66,0};bag=0;
    show_enemy=true;enemy.x=bp_axis(6);enemy.y=bp_axis(7);enemy.team=1-int(bp_side());
}
static std::unique_ptr<Campaign> campaign(bool control) {
    setup();auto c=std::make_unique<Campaign>(Controller::ledger,control,"nominal-acquisition-fixture");initialize(*c);return c;
}
static void equal_consumed(const JobForecast& a,const JobForecast& b) {
    NC(a.id==b.id && a.complete==b.complete && a.routeKnown==b.routeKnown && a.fullClearAdmitted==b.fullClearAdmitted);
    NC(a.admissionTau==b.admissionTau && a.campGems==b.campGems && a.campGold==b.campGold && a.rewardOnlyEndpoint==b.rewardOnlyEndpoint);
    NC(a.projectedRenewalAndReturnTime==b.projectedRenewalAndReturnTime && a.travelPlusClear==b.travelPlusClear);
    NC(a.releaseTime==b.releaseTime && a.opponentDeadline==b.opponentDeadline && a.personalReleaseEvidence==b.personalReleaseEvidence && a.personalDeadlineEvidence==b.personalDeadlineEvidence && a.supportedDenialGems==b.supportedDenialGems);
    NC(a.nominal()&&b.nominal());const auto& x=*a.nominal();const auto& y=*b.nominal();
    NC(x.response==y.response && x.lifecycle==y.lifecycle && x.conditionalLifecycleWeight==y.conditionalLifecycleWeight);
    NC(x.deltaS==y.deltaS && x.tau==y.tau && x.healthLoss==y.healthLoss && x.goldSpent==y.goldSpent && x.goldEarned==y.goldEarned);
    NC(x.terminal.health==y.terminal.health && x.terminal.maximumHealth==y.terminal.maximumHealth && x.terminal.position.tile==y.terminal.position.tile);
    NC(x.conversions.size()==y.conversions.size());for(std::size_t i=0;i<x.conversions.size();++i)NC(x.conversions[i].completedAt==y.conversions[i].completedAt && x.conversions[i].gems==y.conversions[i].gems);
    NC(nominal_rate(a)==nominal_rate(b));
}
static void actual_caller(bool control) {
    auto c=campaign(control);const auto state=c->information();auto full=c->forecast_context(state);full.nominal_acquisitions_only=false;
    auto fast=full.fork_without_loop_cache();fast.nominal_acquisitions_only=true;NC(fast.fork_without_loop_cache().nominal_acquisitions_only);
    const auto camps=cambc::vision::camps();const auto prefix=tt::Menu::single_prefix(c->personal_,c->paths_,cambc::self().position());
    const auto fulljobs=c->menu(full,false,camps,prefix);const auto fastjobs=c->menu(fast,false,camps,prefix);
    auto selection_context=c->context_;int altar_distance=10000;for(const auto altar:c->personal_.altars)altar_distance=std::min(altar_distance,chebyshev(cambc::self().position(),altar));
    selection_context.b0BankRecommended=fundamentals::should_bank(state.carriedGems,state.round,altar_distance);
    NC(!full.cutoff&&!fast.cutoff);const auto fm=canonical_menu(fulljobs),nm=canonical_menu(fastjobs);NC(fm.jobs.size()==nm.jobs.size() && fm.jobs.size()>=5);
    unsigned acquisition=0;std::printf("NOMINAL_TABLE {\"side\":%u,\"arm\":%u,\"b0Bank\":%s,\"rows\":[",bp_side(),unsigned(control),selection_context.b0BankRecommended?"true":"false");bool comma=false;
    for(std::size_t i=0;i<fm.jobs.size();++i) {
        const auto& a=*fm.jobs[i];const auto& b=*nm.jobs[i];equal_consumed(a,b);NC(admitted(state,a,false)==admitted(state,b,false));
        if(productive(a.id.kind)) {
            ++acquisition;NC(a.outcomes.size()>=4 && b.outcomes.size()==1 && b.nominalIndex==0);
            NC(b.nominal()->terminal.loops.empty() && b.nominal()->terminal.loopSupportUnavailable && !b.nominal()->terminal.preparationTurns);
        }
        if(comma)std::putchar(',');comma=true;std::printf("{\"id\":");key(a.id);
        std::printf(",\"tau\":%d,\"gems\":%d,\"loss\":%.17g,\"gold\":%.17g,\"renewal\":%d,\"travelClear\":%.17g,\"campGems\":%d,\"campGold\":%d,\"fullOutcomes\":%zu,\"fastOutcomes\":%zu,\"rate\":%.17g}",a.nominal()->tau,a.nominal()->deltaS,a.nominal()->healthLoss,a.nominal()->goldEarned,a.projectedRenewalAndReturnTime,a.travelPlusClear,a.campGems,a.campGold,a.outcomes.size(),b.outcomes.size(),nominal_rate(a));
    }
    NC(acquisition>=3 && full.loop_evaluations>fast.loop_evaluations);
    const auto reference=select(Controller::ledger,control,state,fulljobs,selection_context);const auto result=select(Controller::ledger,control,state,fastjobs,selection_context);
    NC(reference.status==IntentStatus::selected && reference.job && productive(reference.job->kind) && reference.job==result.job && reference.objective==result.objective);
    std::printf("],\"selected\":");key(*reference.job);std::printf(",\"objective\":%.17g,\"fullLoopEvaluations\":%u,\"fastLoopEvaluations\":%u}\n",reference.objective,full.loop_evaluations,fast.loop_evaluations);
    const JobForecast* expected=nullptr;for(const auto& j:fulljobs)if(j.id==*reference.job)expected=&j;NC(expected&&expected->nominal());const bool milestone=reference.job->kind==JobKind::double_cycle;const int first=c->group_index(reference.job->firstGroup);NC(first>=0);const unsigned expected_gems=milestone?unsigned(c->personal_.groups[unsigned(first)].gem_value):unsigned(expected->nominal()->deltaS);
    c->turn();NC(c->active_&&c->active_->id==*reference.job && c->telemetry_.decisions>0);const auto original=c->active_->id;
    for(unsigned n=0;n<180 && !c->telemetry_.completed;++n){advance_round();c->turn();if(!c->telemetry_.completed){NC(c->active_);if(c->active_->id!=original)NC(milestone && c->telemetry_.aborted==1 && c->active_->id.kind==JobKind::bank);}}
    NC(c->telemetry_.aborted==unsigned(milestone));
    NC(c->telemetry_.completed==1 && c->telemetry_.converted==expected_gems && own_score==expected_gems && !bag && submissions==1);
    std::printf("NOMINAL_CALLER {\"side\":%u,\"arm\":%u,\"checks\":%u,\"job\":",bp_side(),unsigned(control),nchecks);key(original);
    std::printf(",\"exactGems\":%u,\"captures\":%u,\"submissions\":%u,\"originalMilestoneCensored\":%s,\"nativeGames\":0}\n",own_score,captures,submissions,milestone?"true":"false");
}
static void negatives() {
    for(unsigned mode=0;mode<4;++mode) {
        auto c=campaign(false);const int index=c->group_index(bp_key(1));NC(index>=0);
        if(mode==0)actor.health=actor.max_health*.25f-1;
        if(mode==1)c->paths_.fields_[unsigned(c->personal_.groups[unsigned(index)].field)].complete=false;
        if(mode==2){group_state[1]=1;for(unsigned i=3;i<6;++i){member_states[i]=2;member_health[i]=1350;}c->personal_.observe(cambc::vision::camps(),{});}
        if(mode==3)fuel=44000;
        const auto state=c->information();auto full=c->forecast_context(state);full.nominal_acquisitions_only=false;auto fast=full.fork_without_loop_cache();fast.nominal_acquisitions_only=true;
        if(mode<3){const auto a=full.acquisition(index),b=fast.acquisition(index);NC(!admitted(state,a,false)&&!admitted(state,b,false));NC(a.complete==b.complete&&a.routeKnown==b.routeKnown&&a.fullClearAdmitted==b.fullClearAdmitted);}
        else {const auto prefix=tt::Menu::single_prefix(c->personal_,c->paths_,cambc::self().position());const auto jobs=c->menu(fast,false,cambc::vision::camps(),prefix);NC(std::none_of(jobs.begin(),jobs.end(),[](const auto& j){return productive(j.id.kind);}));const auto before=actions;c->turn();NC(!c->active_&&actions==before&&!attacks&&!captures);}
    }
    std::printf("NOMINAL_NEGATIVES {\"side\":%u,\"count\":4,\"checks\":%u}\n",bp_side(),nchecks);
}
#ifndef P3_NOMINAL_HELPERS_ONLY
int main(){actual_caller(false);actual_caller(true);negatives();std::printf("NOMINAL_RESULT {\"status\":\"pass\",\"side\":%u,\"checks\":%u,\"nativeGames\":0}\n",bp_side(),nchecks);}
#endif
