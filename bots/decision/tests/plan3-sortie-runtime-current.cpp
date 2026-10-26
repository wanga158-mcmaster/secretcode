// Actual pinned SDK wrappers with the existing explicit, local 8x8 host model.
// The imported old fixture main is NOT executed. No native engine is loaded.
#include <array>
namespace {extern std::array<unsigned,3> respawn_count;}
bool sortie_dynamic_respawn=false;
int (*sortie_champion_probe)(unsigned,void*)=nullptr;
void (*sortie_alive_probe)()=nullptr;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main historical_two_trip_fixture_main
#include "plan3-two-trip-runtime-sdk.cpp"
#undef main
#pragma clang diagnostic pop
#include "../common/sortie-runtime.h"

// SDK3.0.4 additions, modeled explicitly for the inherited8x8 host. These
// functions do not delegate to any engine or infer missing live camp state.
namespace cambc::detail {extern "C" {
std::int32_t sdk_camp_buffs(std::uint32_t id,camp_buff_data*,std::uint32_t,std::uint32_t* count) {
    record("sdk_camp_buffs");camp_state current{};const auto code=sdk_camp(id,&current);if(code)return code;*count=0;return 0;
}
std::int32_t sdk_DistanceField_Get(const std::uint16_t* field,std::int32_t x,std::int32_t y,std::uint16_t* out) {
    record("sdk_DistanceField_Get");verify(x>=0 && y>=0 && x<8 && y<8);*out=field[y*8+x];return 0;
}
std::int32_t sdk_DistanceField_ComputeToWithExpansionCount(const std::uint64_t* walls,const std::uint64_t* roots,
        const std::uint64_t* destinations,std::uint64_t* visited,std::uint16_t* field,std::uint16_t max_distance,std::uint16_t* count) {
    record("sdk_DistanceField_ComputeToWithExpansionCount");verify(!(*walls&*roots));
    std::fill_n(field,64,std::uint16_t{65535});std::array<unsigned,64> queue{};unsigned head=0,tail=0;*visited=0;*count=0;
    for(unsigned n=0;n<64;++n)if((*roots>>n)&1u){field[n]=0;queue[tail++]=n;*visited|=std::uint64_t{1}<<n;}
    while(head<tail && (!*destinations || (*visited&*destinations)!=*destinations)) {
        const unsigned at=queue[head++];++*count;if(field[at]>=max_distance)continue;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const int x=int(at%8)+dx,y=int(at/8)+dy;if((!dx&&!dy) || x<0 || y<0 || x>=8 || y>=8)continue;
            const unsigned next=unsigned(y*8+x);if(((*walls>>next)&1u) || field[next]!=65535)continue;
            field[next]=std::uint16_t(field[at]+1);verify(tail<64);queue[tail++]=next;*visited|=std::uint64_t{1}<<next;
        }
    }
    return 0;
}
}}

namespace {
namespace sr=plan3_runtime::sortie;
unsigned sortie_cases=0,sortie_paths=0,sortie_faults=0,hidden_prepares=0,cold_sortie_paths=0;
unsigned q_resume_cases=0,q_resume_callbacks=0,q_cold_interruptions=0,gold_exit_cases=0,gold_exit_paths=0;
unsigned member_cache_cases=0,member_cache_interruptions=0,member_cache_cold_paths=0;
unsigned model_poll_cases=0,model_direct_checks=0,model_batched_polls=0;
unsigned snapshot_cases=0,snapshot_old_calls=0,snapshot_new_calls=0;
unsigned budget_caller_paths=0;
unsigned geometry_cache_cases=0,geometry_cold_interruptions=0,geometry_first_ready=0,geometry_cached_ready=0;
unsigned trace_hash(const char* name){unsigned h=2166136261u;for(;*name;++name)h=(h^unsigned(*name))*16777619u;return h;}
unsigned calls(const char* name){return unsigned(std::count(query_trace.begin(),query_trace.end(),trace_hash(name)));}
struct SortieFixture {
    std::unique_ptr<Campaign> campaign;
    std::unique_ptr<sr::Candidate> candidate;
    unsigned qVisionCalls=0;
};
SortieFixture sortie_fixture(bool hidden=true) {
    ++sortie_cases;reset_case();auto campaign=std::make_unique<Campaign>(Controller::ledger,false,"sortie-current-sdk");
    initialize(*campaign);opening(*campaign,2);
    auto builder=std::make_unique<sr::Builder>();const auto stats=campaign->capital_.exact_stats();verify(stats.has_value());
    const auto index=std::size_t(campaign->group_index(bp_key(1)));verify(index<campaign->personal_.groups.size());
    if(hidden)visible[0]=0;
    const unsigned state_before=calls("sdk_camp_state"),hp_before=calls("sdk_camp"),vision_before=calls("sdk_hypothetical_vision");
    std::unique_ptr<sr::Candidate> candidate;
    for(unsigned n=0;n<64;++n) {
        campaign->paths_.begin_turn();auto result=builder->prepare(campaign->personal_,index,*stats,campaign->paths_,[]{return true;});
        if(result.status==sr::Status::pending){advance_round();continue;}
        if(result.status!=sr::Status::ready)std::fprintf(stderr,"sortie prepare status=%u reason=%s round=%u\n",unsigned(result.status),result.reason,current_round);
        verify(result.status==sr::Status::ready && result.candidate);candidate=std::move(result.candidate);break;
    }
    verify(bool(candidate) && candidate->publicMemberBound && candidate->assumedQuiet);
    verify(candidate->accounting.deltaS==66 && candidate->accounting.sortieCount>=2 && candidate->accounting.sortieCount<=4);
    verify(candidate->accounting.attackCount<=512 && candidate->input.memberCount==3);
    if(hidden){verify(calls("sdk_camp_state")==state_before && calls("sdk_camp")==hp_before);++hidden_prepares;}
    visible[0]=~std::uint64_t{0};return {std::move(campaign),std::move(candidate),calls("sdk_hypothetical_vision")-vision_before};
}
sr::StepResult sortie_step(SortieFixture& fixture) {
    auto& c=*fixture.campaign;c.paths_.begin_turn();
    return sr::step(fixture.candidate->plan,c,c.personal_,c.paths_,cambc::vision::camps(),{},[]{return true;});
}
void arrive_at_q(SortieFixture& fixture) {
    auto& plan=fixture.candidate->plan;
    for(unsigned n=0;cambc::self().position()!=plan.origin && n<128;++n) {
        const auto result=sortie_step(fixture);verify(result.status==sr::Status::handled);advance_round();
    }
    verify(cambc::self().position()==plan.origin && plan.phase==sr::Phase::approach && attacks==0);
}
void sortie_complete_path() {
    auto fixture=sortie_fixture();auto& plan=fixture.candidate->plan;
    const unsigned began=current_round;const int modeled=fixture.candidate->accounting.tau;
    bool partial=false,retained=false,full_return=false,captured=false,channel=false;
    double retained_hp=0;unsigned partial_id=99;
    for(unsigned n=0;n<512 && own_score!=66;++n) {
        const auto phase=plan.phase;const bool full_spawn=on_spawn() && actor.health==actor.max_health;
        const unsigned before_moves=moves,before_actions=actions;
        const auto result=sortie_step(fixture);
        if(result.status==sr::Status::abort || result.status==sr::Status::unsupported || result.status==sr::Status::cutoff)
            std::fprintf(stderr,"sortie step status=%u phase=%u reason=%s round=%u hp=%.17g attacks=%u\n",unsigned(result.status),unsigned(plan.phase),result.reason,current_round,double(actor.health),attacks);
        verify(result.status==sr::Status::handled || result.status==sr::Status::ordinary_handoff);
        verify(own_score==0 && casts==0 && attacks<=512 && plan.sortie<4);
        if(plan.phase==sr::Phase::escape && !partial) {
            for(unsigned i=3;i<6;++i)if(member_states[i] && member_health[i]>0 && member_health[i]<plan.members[0].fullHealth) {
                partial=true;partial_id=i;retained_hp=member_health[i];break;
            }
            verify(partial && !bag && !captures && !submissions);
        }
        if(partial && (plan.phase==sr::Phase::recall || plan.phase==sr::Phase::healing)) {
            verify(member_states[partial_id] && member_health[partial_id]==retained_hp && !bag && !own_score);retained=true;
        }
        if(phase==sr::Phase::healing && full_spawn) {
            verify(plan.phase==sr::Phase::returning && moves==before_moves+1);full_return=true;
        }
        if(result.captured){verify(!captured && captures==1 && bag==66);captured=true;}
        if(result.status==sr::Status::ordinary_handoff) {
            verify(plan.phase==sr::Phase::conversion_wait && submissions==1 && bag==66 && own_score==0);channel=true;
            if(phase==sr::Phase::conversion_wait)verify(actions==before_actions);
        }
        advance_round();
    }
    verify(own_score==66 && bag==0 && partial && retained && full_return && captured && channel);
    verify(captures==1 && submissions==1 && recalls>=1 && healing_ticks>=1 && current_round-began<=unsigned(modeled));
    const auto actions_before=actions;const auto endpoint=sortie_step(fixture);
    verify(endpoint.status==sr::Status::abort && !endpoint.captured && actions==actions_before);
    verify(plan.phase==sr::Phase::conversion_wait); // Never self-completes scored work.
    ++sortie_paths;
    std::printf("SORTIE_CALLBACK_RECORD {\"side\":%u,\"actualTurns\":%u,\"modeledTurns\":%d,\"attacks\":%u,\"recalls\":%u,\"gems\":66,\"nativeEngine\":false}\n",bp_side(),current_round-began,modeled,attacks,recalls);
}
void sortie_availability() {
    for(int code:{97,98}) {
        auto fixture=sortie_fixture();arrive_at_q(fixture);
        camp_state_error_id=fixture.candidate->plan.members[0].native;camp_state_error_code=code;
        const unsigned before=actions,attacked=attacks;const auto result=sortie_step(fixture);
        verify(result.status==sr::Status::abort && actions==before && attacks==attacked);++sortie_faults;
    }
    auto fixture=sortie_fixture();arrive_at_q(fixture);const auto p=fixture.candidate->plan.members[0].spawn;
    set(visible.data(),p.x,p.y,false);const unsigned before=actions,states=calls("sdk_camp_state");const auto result=sortie_step(fixture);
    verify(result.status==sr::Status::abort && actions==before && calls("sdk_camp_state")==states);++sortie_faults;
}
void sortie_interruption() {
    for(unsigned fault=0;fault<4;++fault) {
        auto fixture=sortie_fixture();arrive_at_q(fixture);
        const auto first=sortie_step(fixture);verify(first.status==sr::Status::handled && attacks==1);advance_round();
        if(fault==0) {const unsigned id=fixture.candidate->plan.members[0].native;member_health[id]=fixture.candidate->plan.members[0].fullHealth;}
        if(fault==1) {++items[unsigned(cambc::item_type::power_shard_of_strength)];sync_stats();}
        if(fault==2)actor.health=0;
        const unsigned before=actions;auto& c=*fixture.campaign;
        const auto enemy=cambc::detail::handle_access::make_champion(static_cast<cambc::handle_champion_t>(1));
        const auto enemies=fault==3?std::vector<cambc::champion>{enemy}:std::vector<cambc::champion>{};
        const auto result=sr::step(fixture.candidate->plan,c,c.personal_,c.paths_,cambc::vision::camps(),enemies,[]{return true;});
        verify(result.status==sr::Status::abort && actions==before);++sortie_faults;
    }
    auto fixture=sortie_fixture();unsigned n=0;
    while(fixture.candidate->plan.phase!=sr::Phase::recall && ++n<256){verify(sortie_step(fixture).status==sr::Status::handled);advance_round();}
    verify(fixture.candidate->plan.phase==sr::Phase::recall && (actor.state&(1<<1)));
    actor.state&=~(1<<1);const unsigned before=actions;const auto result=sortie_step(fixture);
    verify(result.status==sr::Status::abort && actions==before);++sortie_faults;
}
void sortie_campaign_cold() {
    ++sortie_cases;reset_case();gold=360; // Explicit fixture starting cash; every buy uses the legal host action.
    auto c=std::make_unique<Campaign>(Controller::ledger,false,"sortie-cold-caller");
    bool selected=false,executed=false;JobId selected_id;
    for(unsigned n=0;n<384 && c->telemetry_.converted!=66;++n) {
        c->turn();
        if(c->sortie_episode_) {
            verify(c->active_ && c->active_->id.kind==JobKind::cycle && c->active_->nominal());
            if(!selected){selected_id=c->active_->id;selected=true;}
            verify(c->active_->id==selected_id);
            executed|=c->sortie_episode_->started && (moves>0 || attacks>0);
        }
        if(c->telemetry_.converted==66)break;
        advance_round();
    }
    verify(selected && executed && c->telemetry_.decisions>0 && c->telemetry_.completed>=1 && c->telemetry_.converted==66);
    verify(own_score==66 && !bag && captures==1 && submissions==1 && !c->sortie_episode_);
    ++cold_sortie_paths;
}
void q_resume_direct() {
    ++q_resume_cases;auto fixture=sortie_fixture();auto& c=*fixture.campaign;
    const auto index=std::size_t(c.group_index(bp_key(1)));auto stats=c.capital_.exact_stats();verify(stats.has_value());
    sr::Builder builder;visible[0]=0;
    const unsigned initial_vision=calls("sdk_hypothetical_vision"),initial_hp=calls("sdk_camp"),initial_state=calls("sdk_camp_state");
    std::unique_ptr<sr::Candidate> resumed;unsigned interruptions=0,previous_cursor=0;
    for(unsigned callback=0;callback<96;++callback) {
        const unsigned limit=calls("sdk_hypothetical_vision")+1;
        struct Ready {
            unsigned limit;Ready(unsigned n):limit(n){}Ready(const Ready&)=delete;
            bool operator()(){return calls("sdk_hypothetical_vision")<limit;}
        } ready(limit);
        c.paths_.begin_turn();auto result=builder.prepare(c.personal_,index,*stats,c.paths_,ready);++q_resume_callbacks;
        if(result.candidate){resumed=std::move(result.candidate);break;}
        verify(result.status==sr::Status::cutoff || result.status==sr::Status::pending);
        if(!builder.route_ && builder.qStarted_) {
            verify(builder.qCursor_>previous_cursor);previous_cursor=builder.qCursor_;++interruptions;
            verify(builder.qCursor_<=unsigned((2*actor.attack_range+1)*(2*actor.attack_range+1)));
        }
        const unsigned held=builder.qCursor_;const auto no=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return false;});
        verify(no.status==sr::Status::cutoff && !no.candidate && builder.qCursor_==held);
        advance_round();
    }
    verify(resumed && interruptions>=2 && q_resume_callbacks<=fixture.qVisionCalls+3);
    verify(calls("sdk_hypothetical_vision")-initial_vision==fixture.qVisionCalls); // No repeated costly Q query.
    verify(calls("sdk_camp")==initial_hp && calls("sdk_camp_state")==initial_state);
    verify(resumed->plan.origin==fixture.candidate->plan.origin && resumed->plan.escapePoint==fixture.candidate->plan.escapePoint);
    verify(resumed->accounting.tau==fixture.candidate->accounting.tau && resumed->accounting.attackCount==fixture.candidate->accounting.attackCount);
    verify(resumed->accounting.healthLoss==fixture.candidate->accounting.healthLoss && resumed->plan.preparedRound==current_round);
    visible[0]=~std::uint64_t{0};

    // An unfinished nearest-Q scan must not retain the old ranking anchor.
    ++q_resume_cases;sr::Builder moving;const unsigned limit=calls("sdk_hypothetical_vision")+1;
    c.paths_.begin_turn();auto partial=moving.prepare(c.personal_,index,*stats,c.paths_,[&]{return calls("sdk_hypothetical_vision")<limit;});
    verify(!partial.candidate && moving.qStarted_ && !moving.route_ && moving.qCursor_>0);
    const auto destination=cambc::vec2{bp_axis(0),bp_axis(1)};verify(cambc::self().move_absolute(destination));advance_round();
    sr::Builder fresh;std::unique_ptr<sr::Candidate> expected,changed;
    for(unsigned n=0;n<16 && (!expected || !changed);++n) {
        c.paths_.begin_turn();if(!expected){auto row=fresh.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});expected=std::move(row.candidate);}
        c.paths_.begin_turn();if(!changed){auto row=moving.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});changed=std::move(row.candidate);}
        if(!expected || !changed)advance_round();
    }
    verify(expected && changed && moving.qDecisionOrigin_==destination && changed->plan.decisionOrigin==destination);
    verify(changed->plan.origin==expected->plan.origin && changed->accounting.tau==expected->accounting.tau);
}
Campaign* q_probe_campaign=nullptr;unsigned q_probe_cursor=0,q_probe_vision=0;bool q_probe_cut=false;
std::int64_t q_scan_fuel() {
    if(q_probe_campaign) {
        const auto& builder=q_probe_campaign->sortie_builder_;
        if(!builder.route_ && builder.qStarted_ && builder.qCursor_>q_probe_cursor && calls("sdk_hypothetical_vision")>q_probe_vision) {
            q_probe_cut=true;return 44000;
        }
    }
    return -1;
}
std::optional<cambc::vec2> public_q_oracle(const Campaign& campaign,int key,cambc::vec2 anchor) {
    const auto& groups=campaign.personal_.groups;
    const auto group=std::find_if(groups.begin(),groups.end(),[&](const auto& g){return g.key==key;});
    verify(group!=groups.end());
    auto blocked=campaign.personal_.terrain;for(const auto& g:groups)blocked|=g.spawns;
    const int width=cambc::map::width(),height=cambc::map::height(),range=cambc::self().attack_range();
    std::optional<cambc::vec2> best;std::pair<int,int> rank{};
    // Independent whole-board enumeration, not Builder's local scan/cursor.
    for(int tile=0;tile<width*height;++tile) {
        const cambc::vec2 q{tile%width,tile/width};if(blocked.is_set(q))continue;
        bool supported=true;
        for(const auto& member:group->group.camps())if(int(q.chebyshev_distance(member.spawn_position()))>range)supported=false;
        if(!supported)continue;
        const auto visible=cambc::self().visible_tiles_from(q);
        for(const auto& member:group->group.camps())if(!visible.is_set(member.spawn_position()))supported=false;
        const std::pair candidate{int(q.chebyshev_distance(anchor)),tile};
        if(supported && (!best || candidate<rank)){best=q;rank=candidate;}
    }
    return best;
}
void q_resume_campaign() {
    ++q_resume_cases;int expected_group=-1;
    {auto baseline=sortie_fixture();expected_group=baseline.candidate->plan.groupKey;}
    reset_case();gold=360;auto c=std::make_unique<Campaign>(Controller::ledger,false,"sortie-q-resume-cold");
    q_probe_campaign=c.get();optional_fuel_probe=q_scan_fuel;bool selected=false;
    unsigned selected_round=0;
    for(unsigned n=0;n<256 && c->telemetry_.converted!=66;++n) {
        q_probe_cursor=c->sortie_builder_.qCursor_;q_probe_vision=calls("sdk_hypothetical_vision");q_probe_cut=false;
        c->turn();if(q_probe_cut)++q_cold_interruptions;
        if(c->sortie_episode_) {
            verify(c->active_ && c->active_->id.kind==JobKind::cycle && c->active_->id.firstGroup==expected_group);
            if(!selected){
                const auto expected_q=public_q_oracle(*c,expected_group,c->sortie_builder_.qDecisionOrigin_);
                verify(expected_q && c->sortie_episode_->origin==*expected_q);
                selected_round=current_round;verify(c->sortie_episode_->preparedRound==current_round);selected=true;
            }
            optional_fuel_probe=nullptr;
        }
        if(c->telemetry_.converted==66)break;advance_round();
    }
    optional_fuel_probe=nullptr;q_probe_campaign=nullptr;
    verify(selected && q_cold_interruptions>=2 && q_cold_interruptions<=81 && selected_round<160);
    verify(c->telemetry_.opportunities>0 && c->telemetry_.converted==66 && own_score==66 && !bag);
    verify(captures==1 && submissions==1 && !c->sortie_episode_);++cold_sortie_paths;
}
void member_cache_direct() {
    ++member_cache_cases;auto baseline=sortie_fixture();auto& c=*baseline.campaign;
    const auto index=std::size_t(c.group_index(bp_key(1)));const auto stats=c.capital_.exact_stats();verify(bool(stats));
    sr::Builder builder;visible[0]=0;
    const unsigned designs=calls("sdk_camp_design"),live=calls("sdk_camp"),states=calls("sdk_camp_state");
    for(unsigned member=0;member<3;++member) {
        const unsigned limit=calls("sdk_camp_design")+1;c.paths_.begin_turn();
        const auto partial=builder.prepare(c.personal_,index,*stats,c.paths_,[&]{return calls("sdk_camp_design")<limit;});
        verify(!partial.candidate && partial.status==sr::Status::cutoff && builder.publicCursor_==member+1);
        verify(calls("sdk_camp_design")==limit);++member_cache_interruptions;
        const auto held=builder.publicCursor_;const auto denied=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return false;});
        verify(!denied.candidate && denied.status==sr::Status::cutoff && builder.publicCursor_==held);
        advance_round();
    }
    auto prepare=[&](const EquipmentStats& current) {
        std::unique_ptr<sr::Candidate> result;
        for(unsigned n=0;n<32 && !result;++n) {
            c.paths_.begin_turn();auto row=builder.prepare(c.personal_,index,current,c.paths_,[]{return true;});
            verify(row.status==sr::Status::pending || row.status==sr::Status::ready);
            result=std::move(row.candidate);if(!result)advance_round();
        }
        verify(bool(result));return result;
    };
    auto first=prepare(*stats);verify(calls("sdk_camp_design")==designs+3);
    verify(calls("sdk_camp")==live && calls("sdk_camp_state")==states);
    verify(first->plan.origin==baseline.candidate->plan.origin && first->accounting.tau==baseline.candidate->accounting.tau);
    verify(first->plan.preparedRound==current_round && first->input.health==actor.health);
    const auto full=first->plan.members[0].fullHealth;first->plan.members[0].lastHealth=1;
    actor.health-=1;advance_round(); // One point remains missing away from a spawn after the move below.
    const auto destination=cambc::vec2{bp_axis(0),bp_axis(1)};verify(cambc::self().move_absolute(destination));advance_round();actor.health=actor.max_health-1;
    builder.reset_preparation();auto fresh=prepare(*stats);
    verify(calls("sdk_camp_design")==designs+3 && fresh->input.health==actor.health && fresh->input.health==stats->health-1);
    verify(fresh->plan.members[0].lastHealth==full && fresh->plan.members[0].fullHealth==full && first->plan.members[0].lastHealth==1);
    verify(fresh->plan.decisionOrigin==destination && fresh->plan.preparedRound==current_round);
    ++items[unsigned(cambc::item_type::power_shard_of_strength)];sync_stats();
    c.paths_.begin_turn();const auto stale=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});
    verify(!stale.candidate && stale.status==sr::Status::unsupported && std::string_view(stale.reason)=="stats");
    const auto changed=c.capital_.exact_stats();verify(changed && changed->strength>stats->strength);actor.health=actor.max_health;
    auto upgraded=prepare(*changed);verify(upgraded->input.strength==actor.strength && upgraded->plan.inventory!=fresh->plan.inventory);
    verify(calls("sdk_camp_design")==designs+3);
    const auto other=std::size_t(c.group_index(bp_key(0)));const unsigned limit=calls("sdk_camp_design")+1;c.paths_.begin_turn();
    const auto replaced=builder.prepare(c.personal_,other,*changed,c.paths_,[&]{return calls("sdk_camp_design")<limit;});
    verify(!replaced.candidate && replaced.status==sr::Status::cutoff && builder.publicKey_==bp_key(0) && builder.publicCursor_==1);
    verify(calls("sdk_camp_design")==designs+4);visible[0]=~std::uint64_t{0};
}
Campaign* member_probe_campaign=nullptr;unsigned member_probe_cursor=0,member_probe_designs=0;bool member_probe_cut=false;
std::int64_t member_scan_fuel() {
    if(member_probe_campaign) {
        const auto& builder=member_probe_campaign->sortie_builder_;
        if(builder.publicStarted_ && builder.publicCursor_>member_probe_cursor && calls("sdk_camp_design")>member_probe_designs) {
            member_probe_cut=true;return 44000;
        }
    }
    return -1;
}
void member_cache_campaign() {
    ++member_cache_cases;reset_case();gold=360;
    auto c=std::make_unique<Campaign>(Controller::ledger,false,"sortie-public-members-cold");
    member_probe_campaign=c.get();optional_fuel_probe=member_scan_fuel;
    bool selected=false,cache_complete=false;unsigned loaded_designs=0,interruptions=0;int key=-1;
    for(unsigned n=0;n<256 && c->telemetry_.converted!=66;++n) {
        member_probe_cursor=c->sortie_builder_.publicCursor_;member_probe_designs=calls("sdk_camp_design");member_probe_cut=false;
        c->turn();const auto& builder=c->sortie_builder_;
        if(member_probe_cut) {
            ++interruptions;verify(!c->sortie_candidate_ && !c->sortie_episode_);
            verify(builder.publicCursor_==member_probe_cursor+1);
        }
        if(builder.publicStarted_ && builder.publicCursor_==builder.publicCount_ && !cache_complete) {
            cache_complete=true;loaded_designs=calls("sdk_camp_design");key=builder.publicKey_;
        }
        if(cache_complete && !selected)verify(builder.publicKey_==key && calls("sdk_camp_design")==loaded_designs);
        if(c->sortie_episode_) {
            verify(cache_complete && c->active_ && c->active_->id.kind==JobKind::cycle && c->active_->id.firstGroup==key);
            if(!selected) {
                const auto expected=public_q_oracle(*c,key,builder.qDecisionOrigin_);
                verify(expected && c->sortie_episode_->origin==*expected && c->sortie_episode_->preparedRound==current_round);
            }
            selected=true;optional_fuel_probe=nullptr;
        }
        if(c->telemetry_.converted==66)break;advance_round();
    }
    optional_fuel_probe=nullptr;member_probe_campaign=nullptr;
    verify(selected && interruptions==3 && c->telemetry_.opportunities>0 && c->telemetry_.converted==66 && own_score==66 && !bag);
    verify(captures==1 && submissions==1 && !c->sortie_episode_);
    member_cache_interruptions+=interruptions;++member_cache_cold_paths;
}
void same_schedule(const plan3_sortie::Schedule& a,const plan3_sortie::Schedule& b) {
    verify(std::tie(a.sortieCount,a.attackCount,a.tau,a.deltaS,a.terminalBag,a.healthLoss,a.minHealth,a.terminalHealth,
        a.conversion.completedAt,a.conversion.gems)==
        std::tie(b.sortieCount,b.attackCount,b.tau,b.deltaS,b.terminalBag,b.healthLoss,b.minHealth,b.terminalHealth,
        b.conversion.completedAt,b.conversion.gems));
    for(unsigned i=0;i<plan3_sortie::maximum_sorties;++i) {
        const auto& x=a.phases[i];const auto& y=b.phases[i];
        verify(x.attacksByMember==y.attacksByMember && x.remainingHealth==y.remainingHealth);
        verify(std::tie(x.attacks,x.startHealth,x.entryEndHealth,x.combatEndHealth,x.escapeEndHealth,x.returnHealth,
            x.entryLoss,x.combatLoss,x.escapeLoss,x.returnLoss,x.beganAt,x.combatStartedAt,x.combatEndedAt,
            x.escapeEndedAt,x.recallEndedAt,x.healingEndedAt,x.endedAt,x.recovered)==
            std::tie(y.attacks,y.startHealth,y.entryEndHealth,y.combatEndHealth,y.escapeEndHealth,y.returnHealth,
            y.entryLoss,y.combatLoss,y.escapeLoss,y.returnLoss,y.beganAt,y.combatStartedAt,y.combatEndedAt,
            y.escapeEndedAt,y.recallEndedAt,y.healingEndedAt,y.endedAt,y.recovered));
    }
}
unsigned model_boundary_read=0,model_boundary_fault=0;
std::int64_t model_boundary_probe() {
    if(fuel_reads==model_boundary_read) {
        if(model_boundary_fault==0)fuel=44000;
        if(model_boundary_fault==1)++current_round;
        if(model_boundary_fault==2)actor.health-=1;
        if(model_boundary_fault==3){++items[unsigned(cambc::item_type::power_shard_of_strength)];sync_stats();}
    }
    return -1;
}
void model_poll_direct() {
    ++model_poll_cases;auto fixture=sortie_fixture();auto& c=*fixture.campaign;
    const auto index=std::size_t(c.group_index(bp_key(1)));auto stats=c.capital_.exact_stats();verify(bool(stats));
    sr::Builder builder;std::unique_ptr<sr::Candidate> warm;
    for(unsigned n=0;n<32 && !warm;++n) {
        c.paths_.begin_turn();auto row=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});
        verify(row.status==sr::Status::ready || row.status==sr::Status::pending);warm=std::move(row.candidate);
        if(!warm)advance_round();
    }
    verify(bool(warm));const unsigned began=fuel_reads;
    struct Ready {
        unsigned began,inside=0;explicit Ready(unsigned n):began(n){}Ready(const Ready&)=delete;
        bool operator()(){if(fuel_reads==began+1)++inside;return true;}
    } ready(began);
    c.paths_.begin_turn();auto complete=builder.prepare(c.personal_,index,*stats,c.paths_,ready);
    verify(complete.status==sr::Status::ready && complete.candidate && complete.model);
    verify(fuel_reads==began+2 && complete.model->before==1000000 && complete.model->after==1000000);
    verify(complete.model->state==plan3_sortie::State::admitted && complete.model->reason==plan3_sortie::Reason::none);
    unsigned direct=0;const auto expected=plan3_sortie::evaluate(complete.candidate->input,[&]{++direct;return true;});
    verify(expected.schedule && ready.inside==(direct+7)/8 && ready.inside>1 && ready.inside<direct);
    verify(complete.model->work.checkpoints==direct && complete.model->work.fastAttacks+complete.model->work.scalarAttacks==complete.candidate->accounting.attackCount);
    verify(complete.model->work.fastAttacks>0 && complete.model->work.phase==plan3_sortie::EvaluationPhase::publication);
    const auto& raw=complete.candidate->input;double living=0;for(unsigned i=0;i<raw.memberCount;++i)if(raw.members[i].health>0)living+=raw.members[i].incoming;
    const auto& firstPhase=complete.candidate->accounting.phases[0];const double product=raw.strength*raw.campDamage,loss=living*raw.mitigation;
    const bool certificate=plan3_sortie::exact_attack_prefix({raw.members[0].health,product,firstPhase.entryEndHealth,loss,loss*raw.escape.exposureTurns,raw.maximumHealth*.25,firstPhase.entryLoss,0});
    verify(c.monster_shards()==5);
    std::printf("SORTIE_RAW_MODEL_RECORD {\"side\":%u,\"legalMonsterShards\":5,\"passiveMagnitude\":%.17g,\"strength\":%.17g,\"campDamage\":%.17g,\"damage\":%.17g,\"maximumHealth\":%.17g,\"mitigation\":%.17g,\"firstCertificate\":%s,\"checkpoints\":%u,\"prefixChecks\":%u,\"fastAttacks\":%u,\"scalarAttacks\":%u,\"phase\":%u}\n",bp_side(),double(cambc::item_type::monster_emblem_of_strength.passive_magnitude()),raw.strength,raw.campDamage,product,raw.maximumHealth,raw.mitigation,certificate?"true":"false",complete.model->work.checkpoints,complete.model->work.prefixChecks,complete.model->work.fastAttacks,complete.model->work.scalarAttacks,unsigned(complete.model->work.phase));
    same_schedule(*expected.schedule,complete.candidate->accounting);model_direct_checks+=direct;model_batched_polls+=ready.inside;
    // A denial on the second actual model poll must remain cutoff, even when
    // the caller would subsequently allow work. No partial schedule publishes.
    ++model_poll_cases;const unsigned sticky_start=fuel_reads;unsigned polls=0;
    c.paths_.begin_turn();const auto denied=builder.prepare(c.personal_,index,*stats,c.paths_,[&] {
        if(fuel_reads==sticky_start+1)return ++polls!=2;return true;
    });
    verify(!denied.candidate && denied.status==sr::Status::cutoff && denied.model && polls==2);
    verify(denied.model->state==plan3_sortie::State::unknown && denied.model->reason==plan3_sortie::Reason::cutoff);
    verify(denied.model->work.checkpoints==9 && denied.model->work.phase==plan3_sortie::EvaluationPhase::validation);
    // Early phases carry no fabricated model result.
    const auto early=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return false;});
    verify(!early.candidate && !early.model && early.status==sr::Status::cutoff);
    for(unsigned fault=0;fault<4;++fault) {
        ++model_poll_cases;const auto saved_actor=actor;const auto saved_items=items;const unsigned saved_round=current_round;
        model_boundary_read=fuel_reads+2;model_boundary_fault=fault;optional_fuel_probe=model_boundary_probe;c.paths_.begin_turn();
        const auto rejected=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return fuel>=45000;});
        optional_fuel_probe=nullptr;
        verify(!rejected.candidate && rejected.model && rejected.model->state==plan3_sortie::State::admitted &&
            rejected.model->reason==plan3_sortie::Reason::none);
        if(fault<2)verify(rejected.status==sr::Status::cutoff && std::string_view(rejected.reason)=="model round");
        else verify(rejected.status==sr::Status::unknown && std::string_view(rejected.reason)=="changed decision actor");
        verify(rejected.model->before==1000000 && rejected.model->after==(fault?1000000:44000));
        actor=saved_actor;items=saved_items;current_round=saved_round;fuel=1000000;
    }
    ++model_poll_cases;const auto healthy=actor.health;actor.health=1;c.paths_.begin_turn();
    const auto unsafe=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});actor.health=healthy;
    verify(!unsafe.candidate && unsafe.status==sr::Status::unsupported && unsafe.model &&
        unsafe.model->state==plan3_sortie::State::rejected && unsafe.model->reason==plan3_sortie::Reason::unsafe_entry);
}
unsigned snapshot_probe_calls=0,snapshot_fault_at=0,snapshot_fault=0;
int snapshot_error_code=0;
int snapshot_probe(unsigned id,void* raw) {
    if(id || ++snapshot_probe_calls!=snapshot_fault_at)return 0;
    auto& out=*static_cast<cambc::detail::champion_info*>(raw);
    if(snapshot_error_code) {
        std::fprintf(stderr,"SORTIE_EXPECTED_SNAPSHOT_ERROR query=%u code=%d\n",snapshot_probe_calls,snapshot_error_code);
        return snapshot_error_code;
    }
    if(snapshot_fault==1)++current_round;
    if(snapshot_fault==2)--out.health;
    if(snapshot_fault==3)++out.x;
    if(snapshot_fault==4)--out.max_health;
    if(snapshot_fault==5)--out.strength;
    if(snapshot_fault==6)--out.focus;
    if(snapshot_fault==7)++out.attack_range;
    if(snapshot_fault==8)out.state|=int(cambc::champion_state::recalling.bits());
    if(snapshot_fault==9)fuel=44000;
    return 0;
}
void snapshot_alive_round_probe(){++current_round;sortie_alive_probe=nullptr;}
void champion_snapshot_direct() {
    auto fixture=sortie_fixture();auto& c=*fixture.campaign;sr::Builder builder;
    const auto index=std::size_t(c.group_index(bp_key(1)));const auto stats=c.capital_.exact_stats();verify(bool(stats));
    std::unique_ptr<sr::Candidate> warm;
    for(unsigned n=0;n<32 && !warm;++n) {
        c.paths_.begin_turn();auto row=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});
        verify(row.status==sr::Status::ready || row.status==sr::Status::pending);warm=std::move(row.candidate);
        if(!warm)advance_round();
    }
    verify(bool(warm));
    if(const char* error=std::getenv("P3_SORTIE_SNAPSHOT_ERROR")) {
        snapshot_fault_at=unsigned(error[0]-'0');snapshot_error_code=std::atoi(error+2);snapshot_probe_calls=0;
        verify((snapshot_fault_at==1 || snapshot_fault_at==2) && snapshot_error_code>0);
        sortie_champion_probe=snapshot_probe;c.paths_.begin_turn();
        (void)builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});
        verify(false); // Nonzero raw SDK status must escape, never publish.
    }
    ++snapshot_cases;const unsigned before=calls("sdk_champion");c.paths_.begin_turn();
    const auto complete=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});
    verify(complete.status==sr::Status::ready && complete.candidate && calls("sdk_champion")==before+2);
    same_schedule(warm->accounting,complete.candidate->accounting);snapshot_new_calls+=calls("sdk_champion")-before;
    // Independently execute the exact15 getter reads replaced on the original
    // stable successful path, rather than inferring its count from a comment.
    const auto& me=cambc::self();const unsigned old=calls("sdk_champion");
    verify(!me.is_recalling() && !me.is_submitting_gems() && !me.is_teleporting());
    verify(sr::close(me.max_health(),stats->health) && sr::close(me.strength(),stats->strength) && sr::close(me.focus(),stats->focus));
    verify(me.attack_range()==warm->plan.attackRange);verify(sr::close(me.health(),warm->input.health));
    verify(me.position()==warm->plan.decisionOrigin);verify(sr::close(me.health(),warm->input.health));
    verify(me.position()==warm->plan.decisionOrigin && sr::close(me.health(),warm->input.health) &&
        sr::close(me.max_health(),stats->health) && sr::close(me.strength(),stats->strength) && sr::close(me.focus(),stats->focus));
    verify(calls("sdk_champion")==old+15);snapshot_old_calls+=calls("sdk_champion")-old;
    const auto saved_actor=actor;const unsigned saved_round=current_round;
    // Dead state rejects before the live-info import; channels retain their
    // distinct query domain and never produce a partial candidate.
    ++snapshot_cases;actor.state&=~1;const auto dead_before=calls("sdk_champion");
    auto denied=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});actor=saved_actor;
    verify(denied.status==sr::Status::unsupported && !denied.candidate && calls("sdk_champion")==dead_before);
    for(const auto flag:{cambc::champion_state::recalling,cambc::champion_state::submitting_gems,cambc::champion_state::teleporting}) {
        ++snapshot_cases;actor.state|=int(flag.bits());denied=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});actor=saved_actor;
        verify(denied.status==sr::Status::unsupported && !denied.candidate && !denied.model);
    }
    for(unsigned fault=0;fault<3;++fault) {
        ++snapshot_cases;if(fault==0)--actor.max_health;if(fault==1)--actor.strength;if(fault==2)--actor.focus;
        denied=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});actor=saved_actor;
        verify(denied.status==sr::Status::unsupported && !denied.candidate && !denied.model);
    }
    // An earlier separate eligibility probe may suspend before the first
    // record; the expected round must already have been captured.
    ++snapshot_cases;sortie_alive_probe=snapshot_alive_round_probe;
    denied=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});current_round=saved_round;
    verify(denied.status==sr::Status::cutoff && !denied.candidate && !denied.model);
    for(unsigned at=1;at<=2;++at)for(unsigned fault=1;fault<=9;++fault) {
        if(at==1 && fault!=1)continue;
        ++snapshot_cases;snapshot_probe_calls=0;snapshot_fault_at=at;snapshot_fault=fault;sortie_champion_probe=snapshot_probe;
        denied=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return fuel>=45000;});
        sortie_champion_probe=nullptr;current_round=saved_round;actor=saved_actor;fuel=1000000;
        verify(!denied.candidate && snapshot_probe_calls==at);
        if(at==1)verify(denied.status==sr::Status::cutoff && !denied.model);
        else if(fault==9)verify(denied.status==sr::Status::cutoff && denied.model && std::string_view(denied.reason)=="final actor");
        else verify(denied.status==sr::Status::unknown && denied.model && std::string_view(denied.reason)=="changed decision actor");
    }
    // Neither cached geometry nor previous attempts retain actor health.
    ++snapshot_cases;--actor.health;
    const auto fresh=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});
    verify(fresh.candidate && fresh.candidate->input.health==actor.health && fresh.candidate->input.health!=warm->input.health);
    const auto independent=plan3_sortie::evaluate(fresh.candidate->input);verify(independent.schedule.has_value());
    same_schedule(*independent.schedule,fresh.candidate->accounting);actor=saved_actor;
    std::printf("SORTIE_SNAPSHOT_RECORD {\"side\":%u,\"cases\":%u,\"originalGetters\":%u,\"attemptSnapshots\":%u}\n",bp_side(),snapshot_cases,snapshot_old_calls,snapshot_new_calls);
}
Campaign* budget_probe_campaign=nullptr;unsigned budget_probe_vision=0;
std::int64_t budget_gate_fuel() {
    if(budget_probe_campaign) {
        const auto& builder=budget_probe_campaign->sortie_builder_;
        if(builder.publicStarted_ && builder.publicCursor_==builder.publicCount_)
            return calls("sdk_hypothetical_vision")==budget_probe_vision?65000:44000;
    }
    return -1;
}
void combat_budget_campaign() {
    reset_case();gold=120;
    // Explicit legal fixture loadout, not an injected candidate/decision or a
    // claim that the fixed opening buys this order. Stop at the earned gold.
    auto& me=cambc::self();verify(me.can_buy(cambc::item_type::monster_shard_of_strength,2));
    me.buy(cambc::item_type::monster_shard_of_strength,2);
    verify(me.can_buy(cambc::item_type::jungle_shard_of_health,2));me.buy(cambc::item_type::jungle_shard_of_health,2);
    verify(!gold && actor.max_health==900 && actor.health==900 && actor.strength==45 && camp_damage==1 && camp_mitigation==1);
    group_state[2]=1;
    for(unsigned id=6;id<9;++id){cambc::detail::camp_design design{};std::memcpy(&design,bp_config::camps[unsigned(bp_kind(id))].data(),sizeof(design));member_states[id]=2;member_health[id]=design.base_stats.base_health;}
    auto c=std::make_unique<Campaign>(Controller::ledger,false,"sortie-combat-budget-caller");initialize(*c);
    const auto stats=c->capital_.exact_stats();verify(stats && stats->health==900 && stats->strength==45);
    budget_probe_campaign=c.get();budget_probe_vision=calls("sdk_hypothetical_vision");optional_fuel_probe=budget_gate_fuel;
    bool selected=false,acted=false;const unsigned before_actions=actions;
    for(unsigned n=0;n<256 && !captures;++n) {
        c->turn();
        verify(!c->sortie_candidate_ && !c->sortie_episode_ && !c->sortie_builder_.qStarted_ && !c->sortie_builder_.route_);
        verify(calls("sdk_hypothetical_vision")==budget_probe_vision);
        if(c->active_) {
            const int index=c->group_index(c->active_->id.firstGroup);
            verify(index>=0 && c->active_->id.kind==JobKind::cycle && c->personal_.groups[std::size_t(index)].gem_value==0);
            selected=true;acted|=actions>before_actions;
        }
        if(!captures)advance_round();
    }
    optional_fuel_probe=nullptr;budget_probe_campaign=nullptr;
    verify(selected && acted && captures==1 && gold>0 && !own_score && !bag && c->telemetry_.decisions>0);
    verify(!c->sortie_target_ && c->sortie_builder_.publicCursor_==3);++budget_caller_paths;
}
void geometry_cache_direct() {
    ++geometry_cache_cases;auto baseline=sortie_fixture();auto& c=*baseline.campaign;
    const auto index=std::size_t(c.group_index(bp_key(1)));const auto stats=c.capital_.exact_stats();verify(bool(stats));
    sr::Builder builder;unsigned first=0;bool retained=false;
    for(unsigned n=0;n<32 && !retained;++n) {
        c.paths_.begin_turn();const auto row=builder.prepare(c.personal_,index,*stats,c.paths_,[&]{++first;return !builder.geometry_;});
        verify(!row.candidate && !row.model && row.status==sr::Status::pending);
        if(builder.geometry_){verify(std::string_view(row.reason)=="static geometry ready");retained=true;}
        else advance_round();
    }
    verify(retained && builder.outward_);const auto* stored=builder.geometry_.get();
    const unsigned designs=calls("sdk_camp_design"),visions=calls("sdk_hypothetical_vision"),locks=calls("sdk_altar_locks");
    unsigned reused=0;c.paths_.begin_turn();auto complete=builder.prepare(c.personal_,index,*stats,c.paths_,[&]{++reused;return true;});
    verify(complete.status==sr::Status::ready && complete.candidate && complete.model && builder.geometry_.get()==stored);
    verify(reused<first && calls("sdk_camp_design")==designs && calls("sdk_hypothetical_vision")==visions);
    verify(calls("sdk_altar_locks")>locks);same_schedule(complete.candidate->accounting,baseline.candidate->accounting);
    geometry_first_ready+=first;geometry_cached_ready+=reused;
    const auto capture=complete.candidate->plan.capturePath;const auto bank=complete.candidate->plan.bankPath;
    complete.candidate->plan.capturePath[0]={-99,-99};complete.candidate->plan.phase=sr::Phase::bank;complete.candidate->plan.usedAttacks[0]=99;
    c.paths_.begin_turn();auto owned=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});
    verify(owned.candidate && owned.candidate->plan.capturePath==capture && owned.candidate->plan.bankPath==bank);
    verify(owned.candidate->plan.phase==sr::Phase::approach && !owned.candidate->plan.usedAttacks[0]);
    const auto old_outward=owned.candidate->input.outward.turns;verify(old_outward>0);
    const auto destination=*builder.route_->toward_origin(cambc::self().position());
    verify(cambc::self().move_absolute(destination));advance_round();actor.health=actor.max_health-1;
    c.paths_.begin_turn();auto moved=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});
    verify(moved.candidate && builder.geometry_.get()==stored && builder.outward_->origin==destination);
    verify(moved.candidate->input.outward.turns==builder.route_->distance(destination) && moved.candidate->input.outward.turns!=old_outward);
    verify(moved.candidate->input.health==actor.health && moved.candidate->plan.preparedRound==current_round);
    unsigned exposure=0;auto point=destination;
    for(unsigned step=0;point!=moved.candidate->plan.origin;++step) {
        verify(step<sr::maximum_leg);bool exposed=false;
        for(unsigned i=0;i<moved.candidate->plan.memberCount;++i){const auto& member=moved.candidate->plan.members[i];exposed|=member.incoming>0 && int(point.chebyshev_distance(member.spawn))<=member.range;}
        exposure+=exposed;point=*builder.route_->toward_origin(point);
    }
    verify(moved.candidate->input.outward.exposureTurns==int(exposure));
    show_enemy=true;c.paths_.begin_turn();const auto enemy=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return true;});show_enemy=false;
    verify(!enemy.candidate && enemy.status==sr::Status::unsupported && std::string_view(enemy.reason)=="enemy" && builder.geometry_.get()==stored);
    const auto denied=builder.prepare(c.personal_,index,*stats,c.paths_,[]{return false;});
    verify(!denied.candidate && !denied.model && denied.status==sr::Status::cutoff && builder.geometry_.get()==stored);
    builder.reset_preparation();verify(!builder.geometry_ && !builder.outward_ && builder.publicCursor_==3);
}
Campaign* geometry_probe_campaign=nullptr;bool geometry_any_cut=false,geometry_turn_cut=false;
std::int64_t geometry_cache_fuel() {
    if(geometry_turn_cut)return 44000;
    if(geometry_probe_campaign && !geometry_any_cut && geometry_probe_campaign->sortie_builder_.geometry_) {
        geometry_any_cut=geometry_turn_cut=true;return 44000;
    }
    return -1;
}
void geometry_cache_campaign() {
    ++geometry_cache_cases;reset_case();gold=360;
    auto c=std::make_unique<Campaign>(Controller::ledger,false,"sortie-static-geometry-cold");
    geometry_probe_campaign=c.get();geometry_any_cut=geometry_turn_cut=false;optional_fuel_probe=geometry_cache_fuel;
    bool selected=false;const void* retained=nullptr;
    for(unsigned n=0;n<256 && c->telemetry_.converted!=66;++n) {
        geometry_turn_cut=false;c->turn();
        if(geometry_turn_cut) {
            ++geometry_cold_interruptions;retained=c->sortie_builder_.geometry_.get();
            verify(retained && !c->sortie_candidate_ && !c->sortie_episode_);
        }
        if(c->sortie_episode_) {
            verify(retained && c->sortie_builder_.geometry_.get()==retained && c->active_ && c->active_->id.kind==JobKind::cycle);
            if(!selected)verify(c->sortie_episode_->preparedRound==current_round);
            selected=true;optional_fuel_probe=nullptr;
        }
        if(c->telemetry_.converted==66)break;advance_round();
    }
    optional_fuel_probe=nullptr;geometry_probe_campaign=nullptr;
    verify(selected && geometry_cold_interruptions==1 && c->telemetry_.opportunities>0 && c->telemetry_.converted==66);
    verify(own_score==66 && !bag && captures==1 && submissions==1 && !c->sortie_episode_);++cold_sortie_paths;
}
std::array<unsigned,3> exit_respawn_ticks{};
void exit_advance_round() {
    const auto before=group_state;
    std::array<bool,3> due{};
    for(unsigned g=0;g<3;++g) {
        bool occupied=false;for(unsigned i=g*3;i<g*3+3;++i)occupied|=actor.x==bp_axis(bp_x[i]) && actor.y==bp_axis(bp_y[i]);
        due[g]=group_state[g]==3 && respawn_count[g]==1 && !occupied;
    }
    advance_round();
    for(unsigned g=0;g<3;++g)if(before[g]==2 && group_state[g]==3)respawn_count[g]=exit_respawn_ticks[g];
    for(unsigned g=0;g<3;++g)if(due[g]) {
        verify(respawn_count[g]==0 && group_state[g]==3);group_state[g]=1;
        for(unsigned i=g*3;i<g*3+3;++i) {
            cambc::detail::camp_design design{};std::memcpy(&design,bp_config::camps[unsigned(bp_kind(i))].data(),sizeof(design));
            member_states[i]=2;member_health[i]=design.base_stats.base_health;
        }
    }
}
void captured_gold_campaign() {
    ++gold_exit_cases;reset_case();sortie_dynamic_respawn=true;gold=360;
    const auto public_groups=cambc::map::camp_groups();verify(public_groups.size()==3);
    for(unsigned g=0;g<3;++g){exit_respawn_ticks[g]=public_groups[g].max_respawn_timer();verify(exit_respawn_ticks[g]>0);}
    group_state={3,3,1};member_states.fill(0);member_health.fill(0);respawn_count={exit_respawn_ticks[0],exit_respawn_ticks[1],0};
    for(unsigned id=6;id<9;++id) {
        cambc::detail::camp_design design{};std::memcpy(&design,bp_config::camps[unsigned(bp_kind(id))].data(),sizeof(design));
        member_states[id]=2;member_health[id]=design.base_stats.base_health;
    }
    auto c=std::make_unique<Campaign>(Controller::ledger,false,"captured-gold-cold-caller");
    bool captured_gold=false,left_gold=false,gem_job=false,pending_exit=false;
    for(unsigned n=0;n<512 && c->telemetry_.converted!=66;++n) {
        const auto prior=cambc::self().position();const auto decisions=c->telemetry_.decisions;const unsigned before_moves=moves;
        c->turn();
        if(pending_exit && !left_gold) {
            verify(captured_gold && !bag && group_state[2]==3 && respawn_count[2]==exit_respawn_ticks[2]);
            verify(cambc::self().position()!=prior && moves==before_moves+1 && !c->active_);
            verify(c->telemetry_.decisions==decisions && !c->sortie_candidate_ && !c->sortie_forecast_ && !c->sortie_episode_);
            bool public_camp=false;for(const auto& group:c->personal_.groups)public_camp|=group.spawns.is_set(cambc::self().position());
            verify(!public_camp);left_gold=true;
        }
        if(c->sortie_episode_) {verify(left_gold && c->active_ && c->active_->id.kind==JobKind::cycle);gem_job=true;}
        if(c->telemetry_.converted==66)break;
        const auto old=group_state[2];exit_advance_round();
        if(old==2 && group_state[2]==3) {captured_gold=true;pending_exit=true;}
    }
    verify(captured_gold && left_gold && gem_job && c->telemetry_.converted==66 && own_score==66 && !bag);
    verify(captures>=2 && submissions==1);++gold_exit_paths;sortie_dynamic_respawn=false;
}
std::unique_ptr<Campaign> captured_gold_setup(unsigned lifecycle,unsigned carried=0) {
    reset_case();auto c=std::make_unique<Campaign>(Controller::ledger,false,"captured-gold-gate");initialize(*c);opening(*c,2);
    actor.x=bp_axis(bp_x[6]);actor.y=bp_axis(bp_y[6]);group_state[2]=lifecycle;bag=carried;
    if(lifecycle==1) {
        member_states[6]=0;member_health[6]=0;
        for(unsigned id=7;id<9;++id){cambc::detail::camp_design design{};std::memcpy(&design,bp_config::camps[unsigned(bp_kind(id))].data(),sizeof(design));member_states[id]=2;member_health[id]=design.base_stats.base_health;}
    }
    return c;
}
void captured_gold_negatives() {
    for(const auto lifecycle:{1u,2u}) {
        ++gold_exit_cases;auto c=captured_gold_setup(lifecycle);bool captured=true;const unsigned before=actions;
        verify(!c->leave_captured_gold(captured) && !captured && actions==before);
    }
    ++gold_exit_cases;auto bagged=captured_gold_setup(3,24);bool captured=true;const unsigned before=actions;
    verify(!bagged->leave_captured_gold(captured) && !captured && actions==before && bag==24);
    ++gold_exit_cases;auto blocked=captured_gold_setup(3);const auto position=cambc::self().position();
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
        const cambc::vec2 next{position.x+dx,position.y+dy};
        if((dx||dy) && next.x>=0 && next.y>=0 && next.x<8 && next.y<8)blocked->personal_.terrain.set(next);
    }
    captured=false;const unsigned blocked_actions=actions;
    verify(!blocked->leave_captured_gold(captured) && captured && actions==blocked_actions);
    blocked->choose(blocked->information(),false,cambc::vision::camps());
    verify(actions==blocked_actions && !blocked->sortie_candidate_ && !blocked->sortie_forecast_ && !blocked->sortie_episode_);
    // A hidden occupant invalidates that local observation; no old-state menu
    // or invented route is published after the unsuccessful SDK move.
    ++gold_exit_cases;auto hidden=captured_gold_setup(3);bp_hidden_collision=true;
    const unsigned hidden_actions=actions,decisions=hidden->telemetry_.decisions;
    hidden->choose(hidden->information(),false,cambc::vision::camps());
    verify(actions==hidden_actions && hidden->telemetry_.decisions==decisions && !hidden->active_);
    verify(hidden->last_move_status()==Executor::MoveStatus::hidden_collision && !hidden->sortie_candidate_ && !hidden->sortie_forecast_);
    bp_hidden_collision=false;
}
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(std::getenv("P3_SORTIE_SNAPSHOT_ERROR")){champion_snapshot_direct();verify(false);}
    const bool q_only=std::getenv("P3_SORTIE_Q_ONLY");
    const bool exit_only=std::getenv("P3_SORTIE_EXIT_ONLY");
    const bool members_only=std::getenv("P3_SORTIE_MEMBERS_ONLY");
    const bool model_only=std::getenv("P3_SORTIE_MODEL_ONLY");
    const bool budget_only=std::getenv("P3_SORTIE_BUDGET_ONLY");
    const bool geometry_only=std::getenv("P3_SORTIE_GEOMETRY_ONLY");
    if(geometry_only){geometry_cache_direct();geometry_cache_campaign();model_poll_direct();}
    else if(budget_only){combat_budget_campaign();model_poll_direct();sortie_campaign_cold();}
    else if(model_only){champion_snapshot_direct();model_poll_direct();sortie_campaign_cold();}
    else if(members_only){member_cache_direct();member_cache_campaign();}
    else if(exit_only){captured_gold_campaign();captured_gold_negatives();}
    else if(q_only){q_resume_direct();q_resume_campaign();}
    else {sortie_complete_path();sortie_availability();sortie_interruption();sortie_campaign_cold();q_resume_direct();q_resume_campaign();}
    std::printf("SORTIE_CALLBACK_RESULT {\"status\":\"pass\",\"side\":%u,\"checks\":%u,\"scenarios\":%u,\"fullPaths\":%u,\"coldPaths\":%u,\"faults\":%u,\"hiddenPrepares\":%u,\"qResumeCases\":%u,\"qResumeCallbacks\":%u,\"qColdInterruptions\":%u,\"goldExitCases\":%u,\"goldExitPaths\":%u,\"memberCacheCases\":%u,\"memberCacheInterruptions\":%u,\"memberCacheColdPaths\":%u,\"modelPollCases\":%u,\"modelDirectChecks\":%u,\"modelBatchedPolls\":%u,\"budgetCallerPaths\":%u,\"geometryCacheCases\":%u,\"geometryColdInterruptions\":%u,\"geometryFirstReady\":%u,\"geometryCachedReady\":%u,\"nativeGames\":0}\n",bp_side(),checks,sortie_cases,sortie_paths,cold_sortie_paths,sortie_faults,hidden_prepares,q_resume_cases,q_resume_callbacks,q_cold_interruptions,gold_exit_cases,gold_exit_paths,member_cache_cases,member_cache_interruptions,member_cache_cold_paths,model_poll_cases,model_direct_checks,model_batched_polls,budget_caller_paths,geometry_cache_cases,geometry_cold_interruptions,geometry_first_ready,geometry_cached_ready);
}
