#if defined(P3_HINDSIGHT_SDK)
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
#include "../hindsight-common/runtime.h"
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
#else
#include "../hindsight-common/regret.h"
#include <cstdio>
#include <cstdlib>
extern "C" void* __cxa_allocate_exception(std::size_t n){return std::malloc(n);}
extern "C" void __cxa_throw(void*,void*,void(*)(void*)){std::abort();}
#endif

namespace h=plan3_hindsight;
static unsigned hchecks=0;
static void require_h(bool value,int line){++hchecks;if(!value){std::fprintf(stderr,"Hindsight check failed at%d\n",line);std::exit(1);}}
#define HC(...) require_h(bool(__VA_ARGS__),__LINE__)
static plan3::JobId jid(int key){return {plan3::JobKind::cycle,key,-1,1,-1};}
static void kernel_cases() {
    h::Table t;t.state=h::State::known;t.covered=t.represented=15;
    t.rows={{jid(10),{100,1,100,1},15},{jid(20),{90,9,90,9},15}};
    auto r=h::evaluate(t);HC(r.state==h::State::known && r.regret.job==jid(10) && r.security.job==jid(20));
    HC(r.regret.value==8 && r.security.value==9);
    t.rows={{jid(10),{10,0,10,0},15},{jid(20),{6,6,6,6},15},{jid(30),{0,10,0,10},15}};
    r=h::evaluate(t);HC(r.regret.job==jid(20) && r.regret.value==4);
    // Independent pairwise foregone-opportunity oracle, without column maxima.
    for(const auto& row:t.rows) {
        double loss=0;for(const auto& other:t.rows)for(unsigned w=0;w<4;++w)loss=std::max(loss,other.value[w]-row.value[w]);
        if(row.job==r.regret.job)HC(loss==r.regret.value);else HC(loss>r.regret.value);
    }
    std::reverse(t.rows.begin(),t.rows.end());HC(h::evaluate(t).regret.job==r.regret.job);
    t.rows={{jid(20),{2,2,2,2},15},{jid(10),{2,2,2,2},15}};r=h::evaluate(t);
    HC(r.regret.job==jid(10) && r.security.job==jid(10));
    t.covered=1;t.rows[0].mask=1;t.rows[1].mask=1;HC(h::evaluate(t).regret.job==jid(10));
    t.covered=15;HC(h::evaluate(t).state==h::State::invalid);
    t.rows.clear();for(int n=0;n<28;++n)t.rows.push_back({jid(n),{double(n),double(27-n),double(n),double(27-n)},15});
    r=h::evaluate(t);HC(r.state==h::State::known && r.regret.job==jid(13));
    unsigned calls=0;h::evaluate(t,[&]{++calls;return true;});
    for(unsigned cut=0;cut<calls;++cut){unsigned at=0;auto v=h::evaluate(t,[&]{return at++<cut;});HC(v.state==h::State::cutoff && !v.regret.present && !v.security.present && !v.covered);}
}
#if !defined(P3_HINDSIGHT_SDK)
static plan3::JobForecast forecast(int key,unsigned mask) {
    plan3::JobForecast j;j.id=jid(key);
    for(unsigned w=0;w<4;++w)if(mask&(1u<<w)) {
        plan3::Outcome o;o.response=static_cast<plan3::ResponseWorld>(w);o.tau=2;o.deltaS=10+int(w);
        o.conversions={{2,o.deltaS}};o.terminal.health=o.terminal.maximumHealth=100;j.outcomes.push_back(o);
    }
    return j;
}
static void mapping_cases() {
    plan3::InformationState i;i.round=10;i.roundLimit=200;i.health=i.maximumHealth=100;i.winningGems=1000;
    std::vector<plan3::JobForecast> jobs={forecast(10,15),forecast(20,1)};
    const auto ready=[]{return true;};auto t=h::map_menu(i,jobs,ready);
    HC(t.state==h::State::known && t.covered==1 && t.represented==15 && t.rows.size()==2);
    jobs[1]=forecast(20,2);jobs[0]=forecast(10,1);t=h::map_menu(i,jobs,ready);
    HC(t.state==h::State::unavailable && t.reason==h::Reason::no_common_world && t.rows.empty());
    jobs={forecast(10,15)};auto copy=jobs[0].outcomes[0];copy.lifecycle=1;copy.deltaS=30;copy.conversions={{2,30}};
    copy.conditionalLifecycleWeight=.25;jobs[0].outcomes[0].conditionalLifecycleWeight=.75;jobs[0].outcomes.push_back(copy);
    t=h::map_menu(i,jobs,ready);HC(t.state==h::State::known && t.rows[0].value[0]==15);
    jobs[0].outcomes.back().lifecycle=0;t=h::map_menu(i,jobs,ready);HC(t.state==h::State::unavailable && t.rows.empty());
    jobs={forecast(10,15),forecast(20,15)};
    for(unsigned bad=0;bad<5;++bad){auto v=jobs;
        if(bad==0)v[1].outcomes[1].conditionalLifecycleWeight=0;
        if(bad==1)v[1].outcomes[1].response=static_cast<plan3::ResponseWorld>(4);
        if(bad==2)v[1].outcomes[1].conversions[0].gems=99;
        if(bad==3)v[1].outcomes[1].healthLoss=std::numeric_limits<double>::quiet_NaN();
        if(bad==4)v[1].outcomes[1].minimumHealth=-1;
        t=h::map_menu(i,v,ready);HC(t.state==h::State::unavailable && t.rows.empty() && !t.covered);
    }
    unsigned calls=0;h::map_menu(i,jobs,[&]{++calls;return true;});
    for(unsigned cut=0;cut<calls;++cut){unsigned at=0;t=h::map_menu(i,jobs,[&]{return at++<cut;});HC(t.state==h::State::cutoff && t.rows.empty() && !t.covered);}
}
int main(){kernel_cases();mapping_cases();std::printf("HINDSIGHT_RESULT {\"status\":\"pass\",\"checks\":%u,\"kernel\":true,\"nativeGames\":0}\n",hchecks);}
#else
namespace {
struct Probe {h::Runtime runtime;h::Table table;h::Result result;plan3::JobId nominal;unsigned calls=0;bool control=false;};
plan3::JobIntent probe_select(void* opaque,bool control,const plan3::InformationState& i,const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& c) {
    auto& probe=*static_cast<Probe*>(opaque);
    if(control==probe.control && !probe.calls) {
        probe.table=h::map_menu(i,jobs,[]{return true;});probe.result=h::evaluate(probe.table);
        const auto nominal=plan3::select(plan3::Controller::atlas,false,i,jobs,c);HC(nominal.job);probe.nominal=*nominal.job;
        std::printf("HINDSIGHT_TABLE {\"side\":%u,\"round\":%d,\"covered\":%u,\"rows\":[",bp_side(),i.round,probe.table.covered);
        bool comma=false;for(const auto& row:probe.table.rows){if(comma)std::putchar(',');comma=true;
            std::printf("{\"key\":[%d,%d,%d,%d,%d],\"v\":[%.17g,%.17g,%.17g,%.17g]}",int(row.job.kind),row.job.firstGroup,row.job.secondGroup,row.job.altarTile,row.job.equipmentBlock,row.value[0],row.value[1],row.value[2],row.value[3]);}
        const auto key=[](const plan3::JobId& j){std::printf("[%d,%d,%d,%d,%d]",int(j.kind),j.firstGroup,j.secondGroup,j.altarTile,j.equipmentBlock);};
        std::printf("],\"regret\":");key(probe.result.regret.job);std::printf(",\"security\":");key(probe.result.security.job);
        std::printf(",\"nominal\":");key(probe.nominal);std::puts("}");
    }
    if(control==probe.control)++probe.calls;return h::select(&probe.runtime,control,i,jobs,c);
}
void probe_observe(void* opaque,const plan3::PolicyEvent& e){h::observe(&static_cast<Probe*>(opaque)->runtime,e);}
void setup_world() {
    reset_case();group_state={2,2,3};member_states.fill(0);member_health.fill(0);raw_gems={50,66,0};bag=20;
    show_enemy=true;enemy.x=bp_axis(6);enemy.y=bp_axis(7);enemy.team=1-int(bp_side());
}
void actual_caller(bool control) {
    setup_world();
    auto c=std::make_unique<Campaign>(Controller::ledger,control,"p3-hindsight-sdk");Probe probe;probe.control=control;probe.runtime.campaign=c.get();
    c->supply_selector(probe_select,&probe,false,probe_observe);initialize(*c);
    c->turn();HC(probe.calls && probe.table.state==h::State::known && probe.table.covered==15);
    const auto expected=control?probe.result.security.job:probe.result.regret.job;
    HC(c->active_ && c->active_->id==expected && probe.runtime.commits==1);
    HC(probe.result.regret.job!=probe.result.security.job || probe.result.regret.job!=probe.nominal);
    const auto committed=c->active_->id;const auto committed_token=c->active_policy_token_;
    for(unsigned n=0;n<180 && !probe.runtime.exactEndpoints;++n){advance_round();c->turn();}
    HC(probe.runtime.exactEndpoints==1 && probe.runtime.exactGems==own_score && own_score==(control?20u:136u));
    HC(submissions==1 && captures==(control?0u:2u) && !bag && probe.runtime.aborts==0);
    const auto before=probe.runtime.exactEndpoints;
    h::observe(&probe.runtime,{plan3::PolicyEvent::Kind::completed,committed_token,committed,32,int(current_round),own_score,true});
    HC(probe.runtime.exactEndpoints==before);
    // An unmatched priority/token0 endpoint cannot become policy feedback.
    h::observe(&probe.runtime,{plan3::PolicyEvent::Kind::completed,0,committed,32,int(current_round),999,true});
    HC(probe.runtime.exactEndpoints==before);
    // A separate actually committed copy is interrupted before any endpoint.
    setup_world();auto interrupted=std::make_unique<Campaign>(Controller::ledger,control,"p3-hindsight-censor-sdk");
    Probe negative;negative.control=control;negative.runtime.campaign=interrupted.get();
    interrupted->supply_selector(probe_select,&negative,false,probe_observe);initialize(*interrupted);interrupted->turn();
    HC(interrupted->active_ && negative.runtime.commits==1);
    interrupted->abort("hindsight-censor-fixture");HC(negative.runtime.exactEndpoints==0 && negative.runtime.censored==1 && negative.runtime.aborts==1);
    std::printf("HINDSIGHT_RESULT {\"status\":\"pass\",\"checks\":%u,\"side\":%u,\"arm\":%u,\"generatedFork\":true,\"exactEndpoints\":%u,\"exactGems\":%llu,\"censored\":%u,\"nativeGames\":0}\n",hchecks,bp_side(),unsigned(control),probe.runtime.exactEndpoints,static_cast<unsigned long long>(probe.runtime.exactGems),negative.runtime.censored);
}
}
int main(){kernel_cases();actual_caller(false);actual_caller(true);}
#endif
