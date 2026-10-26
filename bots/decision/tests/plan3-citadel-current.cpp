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
#include "../citadel-common/runtime.h"
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

namespace h=plan3_citadel;
static unsigned cc=0;
static void require_c(bool value,int line){++cc;if(!value){std::fprintf(stderr,"Citadel check failed at%d\n",line);std::exit(1);}}
#define CC(...) require_c(bool(__VA_ARGS__),__LINE__)
namespace {
struct Probe {h::Runtime runtime;h::Table table;h::Result result;JobId ledger,atlas;unsigned calls=0;bool control=false;};
void print_key(const JobId& k){std::printf("[%d,%d,%d,%d,%d]",int(k.kind),k.firstGroup,k.secondGroup,k.altarTile,k.equipmentBlock);}
JobIntent probe_select(void* opaque,bool control,const InformationState& info,const std::vector<JobForecast>& jobs,const ControllerContext& context) {
 auto& p=*static_cast<Probe*>(opaque);
 if(control==p.control&&!p.calls) {
  p.table=h::map_menu(info,jobs,[]{return true;});p.result=h::evaluate(p.table);
  const auto l=plan3::select(Controller::ledger,false,info,jobs,context),a=plan3::select(Controller::atlas,false,info,jobs,context);CC(l.job&&a.job);p.ledger=*l.job;p.atlas=*a.job;
  std::printf("CITADEL_TABLE {\"side\":%u,\"arm\":%u,\"round\":%d,\"covered\":%u,\"rows\":[",bp_side(),unsigned(control),info.round,p.table.covered);
  bool comma=false;for(const auto& r:p.table.rows){if(comma)std::putchar(',');comma=true;std::printf("{\"id\":");print_key(r.job);std::printf(",\"v\":[%.17g,%.17g,%.17g,%.17g]}",r.value[0],r.value[1],r.value[2],r.value[3]);}
  std::printf("],\"security\":");print_key(p.result.security.job);std::printf(",\"mean\":");print_key(p.result.mean.job);std::printf(",\"ledger\":");print_key(p.ledger);std::printf(",\"atlas\":");print_key(p.atlas);std::puts("}");
 }
 if(control==p.control)++p.calls;return h::select(&p.runtime,control,info,jobs,context);
}
void probe_observe(void* opaque,const PolicyEvent& event){h::observe(&static_cast<Probe*>(opaque)->runtime,event);}
void setup_world() {
 reset_case();group_state={2,2,3};member_states.fill(0);member_health.fill(0);raw_gems={50,66,0};bag=1;
 // Legal declared spawn/stock/reward state; no supplied forecast or choice.
 show_enemy=true;enemy.x=bp_axis(6);enemy.y=bp_axis(7);enemy.team=1-int(bp_side());
}
void actual_caller(bool control) {
 setup_world();auto c=std::make_unique<Campaign>(Controller::ledger,control,"p3-citadel-sdk");Probe p;p.control=control;p.runtime.campaign=c.get();
 c->supply_selector(probe_select,&p,false,probe_observe);initialize(*c);c->turn();
 CC(p.calls&&p.table.state==h::State::known&&p.table.covered==15&&p.table.rows.size()>=3);
 CC(p.result.security.job!=p.result.mean.job);
 CC(p.result.security.job!=p.ledger&&p.result.security.job!=p.atlas);
 const auto expected=control?p.result.mean.job:p.result.security.job;CC(c->active_&&c->active_->id==expected&&p.runtime.commits==1);
 const auto original=c->active_->id;const auto token=c->active_policy_token_;
 for(unsigned n=0;n<180&&!p.runtime.exactEndpoints;++n){advance_round();c->turn();if(!p.runtime.exactEndpoints)CC(c->active_&&c->active_->id==original);}
 CC(p.runtime.exactEndpoints==1&&p.runtime.exactGems==own_score&&own_score==(control?117u:1u));
 CC(submissions==1&&captures==(control?2u:0u)&&!bag&&p.runtime.aborts==0);
 const auto endpoints=p.runtime.exactEndpoints;
 h::observe(&p.runtime,{PolicyEvent::Kind::completed,token,original,32,int(current_round),own_score,true});
 h::observe(&p.runtime,{PolicyEvent::Kind::completed,0,original,32,int(current_round),999,true});CC(p.runtime.exactEndpoints==endpoints);
 setup_world();auto aborted=std::make_unique<Campaign>(Controller::ledger,control,"p3-citadel-censor-sdk");Probe negative;negative.control=control;negative.runtime.campaign=aborted.get();
 aborted->supply_selector(probe_select,&negative,false,probe_observe);initialize(*aborted);aborted->turn();CC(aborted->active_&&negative.runtime.commits==1);
 aborted->abort("citadel-censor-fixture");CC(negative.runtime.exactEndpoints==0&&negative.runtime.censored==1&&negative.runtime.aborts==1);
 std::printf("CITADEL_CALLER {\"status\":\"pass\",\"side\":%u,\"arm\":%u,\"checks\":%u,\"exactGems\":%llu,\"exactEndpoints\":%u,\"censored\":%u,\"originalJob\":",bp_side(),unsigned(control),cc,static_cast<unsigned long long>(p.runtime.exactGems),p.runtime.exactEndpoints,negative.runtime.censored);print_key(original);std::puts(",\"differsLedgerAndAtlas\":true,\"nativeGames\":0}");
}
JobForecast lifecycle_job(int key,int low,int high) {
 JobForecast j;j.id={JobKind::cycle,key,-1,26,-1};
 for(int life=0;life<2;++life){Outcome o;o.lifecycle=life;o.conditionalLifecycleWeight=.5;o.response=ResponseWorld::production;o.deltaS=life?high:low;o.tau=2;o.conversions={{2,o.deltaS}};o.terminal.health=o.terminal.maximumHealth=100;j.outcomes.push_back(o);}return j;
}
void finite_cases() {
 InformationState info;info.round=10;info.roundLimit=200;info.health=info.maximumHealth=100;info.winningGems=1000;
 const std::vector<JobForecast> jobs={lifecycle_job(10,0,100),lifecycle_job(20,40,40)};
 const auto table=h::map_menu(info,jobs,[]{return true;});const auto result=h::evaluate(table);
 CC(result.state==h::State::known&&result.columns==1&&result.security.job.firstGroup==10&&result.mean.job==result.security.job);
 const auto robust=plan3::select(Controller::robust,false,info,jobs,{});CC(robust.job&&robust.job->firstGroup==20);
 // This real lifecycle-weight aggregation differs from minimum single outcome.
 auto t=table;t.covered=3;t.rows={{jobs[0].id,{50,50,0,0},3},{jobs[1].id,{120,0,0,0},3}};
 auto r=h::evaluate(t);CC(r.security.job==jobs[0].id&&r.mean.job==jobs[1].id);
 t.rows[1].value={50,50,0,0};CC(h::evaluate(t).security.job==jobs[0].id);std::reverse(t.rows.begin(),t.rows.end());CC(h::evaluate(t).security.job==jobs[0].id);
 t.rows[0].job=jobs[0].id;t.rows[0].job.altarTile=27;CC(h::evaluate(t).security.job.altarTile==26);
 auto malformed=jobs;malformed[0].outcomes[1].lifecycle=0;const auto unknown=h::map_menu(info,malformed,[]{return true;});CC(unknown.state==h::State::unavailable&&unknown.rows.empty()&&!unknown.covered);
 auto invalid=t;invalid.rows.push_back(invalid.rows[0]);CC(h::evaluate(invalid).state==h::State::invalid);
 unsigned calls=0;h::evaluate(t,[&]{++calls;return true;});for(unsigned cut=0;cut<calls;++cut){unsigned at=0;const auto no=h::evaluate(t,[&]{return at++<cut;});CC(no.state==h::State::cutoff&&!no.security.present&&!no.mean.present&&!no.covered);}
 std::printf("CITADEL_FINITE {\"checks\":%u,\"oneColumnCollapsed\":true,\"differentFromIndividualOutcomeRobust\":true}\n",cc);
}
}
int main(){actual_caller(false);actual_caller(true);finite_cases();std::printf("CITADEL_RESULT {\"status\":\"pass\",\"side\":%u,\"checks\":%u,\"nativeGames\":0}\n",bp_side(),cc);}
