// The real Campaign and real journal run over the suite's explicit SDK host.
// These are modeled callback/transport checks, never native health evidence.
#define P3_NOMINAL_HELPERS_ONLY
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "plan3-ledger-nominal-current.cpp"
#pragma clang diagnostic pop

namespace {
Campaign* continuation_probe_campaign=nullptr;
bool continuation_probe_low=false;

std::int64_t continuation_probe() {
    if(!continuation_probe_campaign || !continuation_probe_low)return -1;
    const auto& personal=continuation_probe_campaign->personal_;
    const bool complete=personal.observation_round==current_round &&
        std::all_of(personal.observations.begin(),personal.observations.end(),
            [](const auto& observation){return observation.round==current_round;});
    // This is the existing modeled host fuel seam. All observations finish
    // before the ordinary pre-execution reserve refuses further action.
    return complete?29999:100000;
}

template<class Values> unsigned continuation_hash(const Values& values) {
    unsigned hash=2166136261u;
    for(const auto value:values) {
        const auto word=static_cast<std::uint64_t>(value);
        for(unsigned shift=0;shift<64;shift+=8)
            hash=(hash^unsigned((word>>shift)&255u))*16777619u;
    }
    return hash;
}

void continuation_caller(bool low,unsigned round,bool marcella) {
    reset_case();
    actor.type=int(marcella?cambc::champion_type::marcella:cambc::champion_type::tavi);
    // The inherited host's cast transition models Tavi/Varg only. No cast is
    // available in this generic move/attack caller case.
    actor.state&=~(1<<11);
    auto campaign=std::make_unique<Campaign>(Controller::tempo,false,"continuation-journal-fixture");
    initialize(*campaign);
    current_round=round;
    const auto information=campaign->information();
    NC(information.actorOrdinal==actor.type);
    const int group=campaign->group_index(bp_key(1));
    NC(group>=0 && group_state[1]==1);
    JobForecast job;
    job.id={JobKind::cycle,bp_key(1),-1,-1,-1};
    campaign->active_=job;
    campaign->stage_=0;
    campaign->start_round_=round;
    campaign->last_review_=round;
    campaign->last_progress_=round;
    campaign->last_health_=actor.health;
    campaign->last_position_={actor.x,actor.y};
    campaign->last_bag_=bag;
    campaign->last_gold_=gold;
    query_trace.clear();fuel_trace.clear();fuel_reads=identity_reads=0;
    moves=actions=attacks=casts=0;
    fuel=100000;
    continuation_probe_campaign=campaign.get();continuation_probe_low=low;
    optional_fuel_probe=continuation_probe;
    campaign->turn();
    optional_fuel_probe=nullptr;continuation_probe_campaign=nullptr;continuation_probe_low=false;
    NC(campaign->active_ && campaign->active_->id==job.id);
    NC(campaign->personal_.observation_round==round);
    NC(campaign->personal_.current(unsigned(group)));
    NC(!casts && !captures && !submissions && !campaign->telemetry_.completed);
    if(low)NC(!actions && !moves && campaign->telemetry_.cutoffs>0);
    else NC(moves>0 && actions>0 && !campaign->telemetry_.cutoffs);
#if PLAN3_CONTINUATION_JOURNAL
    const auto& rows=campaign->continuation_journal_.rows();
    const bool scoped=marcella && (round==134 || round==141 || round==143);
    for(unsigned i=0;i<rows.size();++i) {
        const auto& row=rows[i];
        if(!scoped || round!=ContinuationJournal::sample_rounds[i]) {NC(!row.seen);continue;}
        NC(row.seen && row.round==round && row.checkpoint==1);
        NC(row.kind==int(job.id.kind) && row.first==job.id.firstGroup && row.second==-1);
        NC(row.altar==-1 && row.equipment==-1 && row.stage==0 && row.epoch==round);
        NC(row.fuel==unsigned(low?29999:100000));
        NC(row.execute_reached==!low);
        if(low)NC(!row.target_scanned && row.lifecycle==-2 && row.move==-2);
        else {
            NC(row.target_scanned && row.target!=~0u);
            NC(row.observed_round==round && row.lifecycle==int(Lifecycle::alive));
            NC(row.move==int(Executor::MoveStatus::moved));
        }
    }
#endif
    // Parent registration compares these exact physical summaries across
    // default OFF, matched frame/emit0 and matched frame/emit1 compilations.
    std::printf("CONTINUATION_CALLER {\"side\":%u,\"round\":%u,\"marcella\":%s,\"low\":%s,\"queries\":%zu,\"queryHash\":%u,\"fuelReads\":%u,\"fuelHash\":%u,\"identityReads\":%u,\"actions\":%u,\"moves\":%u,\"attacks\":%u,\"casts\":%u,\"x\":%d,\"y\":%d,\"cutoffs\":%u,\"nativeGames\":0}\n",
        bp_side(),round,marcella?"true":"false",low?"true":"false",query_trace.size(),
        continuation_hash(query_trace),fuel_reads,continuation_hash(fuel_trace),identity_reads,
        actions,moves,attacks,casts,actor.x,actor.y,campaign->telemetry_.cutoffs);
}

void continuation_direct() {
#if PLAN3_CONTINUATION_JOURNAL
    const auto before_queries=query_trace.size();
    const auto before_fuel_reads=fuel_reads,before_identity_reads=identity_reads;
    ContinuationJournal journal;
    journal.identity(false);
    journal.begin(134,0,11,-1,17,-1,0,133);
    journal.boundary(90000,0,11,-1,17,-1,0,134);
    journal.execute();journal.observation(134,134,0);journal.target(true,3);journal.move(7);
    NC(std::none_of(journal.rows().begin(),journal.rows().end(),[](const auto& row){return row.seen;}));
    journal.identity(true);
    journal.begin(133,0,11,-1,17,-1,0,133);
    NC(std::none_of(journal.rows().begin(),journal.rows().end(),[](const auto& row){return row.seen;}));
    journal.begin(134,0,11,-1,17,-1,0,133);
    journal.boundary(29999,1,12,13,18,19,1,134);
    const auto& first=journal.rows()[0];
    NC(first.seen && first.round==134 && first.checkpoint==1 && first.fuel==29999);
    NC(first.kind==1 && first.first==12 && first.second==13 && first.altar==18 && first.equipment==19);
    NC(first.stage==1 && first.epoch==134 && !first.execute_reached && !first.target_scanned);
    NC(first.lifecycle==-2 && first.move==-2 && first.target==~0u);
    journal.flush(100000);NC(journal.emitted()==0);
    journal.begin(141,0,21,-1,27,-1,0,140);
    journal.boundary(90000,0,21,-1,27,-1,0,141);
    journal.execute();journal.observation(141,141,0);journal.target(false,123);journal.move(3);
    const auto& second=journal.rows()[1];
    NC(second.seen && second.execute_reached && second.target_scanned && second.target==~0u);
    NC(second.epoch==141 && second.observed_round==141 && second.lifecycle==0 && second.move==3);
    journal.begin(143,0,31,-1,37,-1,0,142);
    journal.boundary(80000,0,31,-1,37,-1,0,143);
    journal.execute();journal.observation(143,143,2);journal.target(true,8);journal.move(7);
    const auto& third=journal.rows()[2];
    NC(third.seen && third.round==143 && third.target==8 && third.lifecycle==2 && third.move==7);
    journal.begin(144,5,99,99,99,99,99,144);
    journal.boundary(1,5,99,99,99,99,99,144);journal.execute();journal.target(true,99);journal.move(99);
    NC(third.first==31 && third.target==8 && third.move==7);
    journal.flush(74999);NC(journal.emitted()==0);
    for(unsigned i=0;i<3;++i){journal.flush(75000);NC(journal.emitted()==i+1);}
    journal.flush(100000);NC(journal.emitted()==3);
    NC(query_trace.size()==before_queries && fuel_reads==before_fuel_reads && identity_reads==before_identity_reads);
#endif
    std::printf("CONTINUATION_DIRECT {\"status\":\"pass\",\"enabled\":%u,\"emit\":%u,\"checks\":%u,\"nativeGames\":0}\n",
        unsigned(PLAN3_CONTINUATION_JOURNAL),unsigned(PLAN3_CONTINUATION_EMIT),nchecks);
}
}

int main() {
    continuation_caller(true,141,true);
    continuation_caller(false,134,true);
    continuation_caller(false,143,false);
    continuation_caller(false,142,true);
    continuation_direct();
    std::printf("CONTINUATION_RESULT {\"status\":\"pass\",\"side\":%u,\"checks\":%u,\"nativeGames\":0}\n",bp_side(),nchecks);
}
