// Mutable Backpressure bridge; original v1 derived from immutable17 e72ac009b8fdb7a5302206c9d5251914b19fb7cc597a0b3dff3f3b483c8f4dae; declared hooks only.
#pragma once
#include "../common/executor.h"
#include "../common/forecasts.h"
#include "../common/allocation.h"
#include "../common/capital.h"
#include "../common/horizon-observations.h"
#include "../common/horizon-trace.h"
#include "../common/risk-prefix.h"
#include "../common/conversion-witness.h"
#include "../common/opening-bootstrap.h"
#include "../common/two-trip-menu.h"
#include "../common/planning-gate.h"
#include <array>
#include "adapter.h"
#include "telemetry.h"


namespace plan3_runtime {
inline void fixed_draft() {
    constexpr std::array order{cambc::champion_draft::tavi,cambc::champion_draft::varg,
        cambc::champion_draft::vendetta,cambc::champion_draft::vesper_vale,cambc::champion_draft::marcella};
    const auto picked = cambc::draft::current_draft(cambc::team::ally);
    for (auto p : order) if (std::find(picked.begin(),picked.end(),p)==picked.end()) {
        cambc::draft::pick(p); return;
    }
}
struct ScoutCheckpoint {
    bool ready=false,unknown=false;
    plan3::JobIntent intent,companion;
    int target=-1;
};
class Campaign : public Executor {
    PersonalInformation personal_;
    StaticPaths paths_;
    TreasuryPlanner capital_;
    mutable ForecastLoopCache forecast_loops_;
    MarketPlan market_plan_;
    BackpressureTelemetry telemetry_;
    plan3_backpressure::ForecastDomain backpressure_domain_;
    plan3_backpressure::Input backpressure_input_;
    bool backpressure_rate_=false,backpressure_context_emitted_=false;
    bool backpressure_menu_evidence_=false;
    plan3::Controller controller_;
    bool control_ = false, initialized_ = false, recovering_ = false;
    bool observation_pending_ = false;
    bool bootstrap_pending_shop_ = false;
    two_trip::Menu two_trip_menu_;
    PlanningGateTrace planning_trace_;
    std::unique_ptr<two_trip::Plan> two_trip_episode_;
    const char* profile_;
    std::optional<plan3::JobForecast> active_;
    unsigned start_round_ = 0, last_progress_ = 0, last_bag_ = 0;
    double start_health_ = 0;
    cambc::vec2 last_position_{};
    int stage_ = 0;
    std::size_t field_cursor_ = 0;
    std::vector<std::size_t> field_order_;
    unsigned initialization_stage_ = 0;
    std::optional<unsigned> callback_round_;
    std::size_t altar_initialization_cursor_ = 0;
    unsigned last_gold_ = 0;
    float last_health_ = 0;
    std::optional<CapitalProposal> capital_candidate_, investment_;
    bool capital_prefix_ = false;
    bool funding_cycle_started_ = false;
    bool capital_review_due_ = false;
    int held_group_ = -1;
    unsigned held_round_ = 0;
    cambc::vec2 held_position_{};
    std::optional<Lifecycle> stage_lifecycle_;
    int last_respawn_timer_ = -1;
    unsigned last_review_ = 0;
    unsigned last_owned_review_ = 0;
    bool unowned_episode_ = false, submission_interrupted_ = false;
    bool candidate_audit_ = false;
    unsigned planning_audits_ = 0;
    std::vector<int> previously_visible_locks_;
    struct Pursuit {
        int identity;
        cambc::vec2 fixed, last;
        unsigned round, approaches, first_approach_round;
    };
    std::vector<Pursuit> pursuit_;
    bool previous_submission_ = false;
    unsigned last_team_score_ = 0, last_score_round_ = 0;
    ConversionSnapshot conversion_snapshot_;
    HorizonObservations horizon_observations_;
    bool horizon_context_attempted_=false,horizon_witness_logged_=false;
    plan3::ControllerContext context_;
    int group_index(int key) const {
        for (std::size_t i=0;i<personal_.groups.size();++i) if (personal_.groups[i].key==key) return static_cast<int>(i);
        return -1;
    }
    int altar_index(int tile) const {
        for (std::size_t i=0;i<personal_.altars.size();++i)
            if (personal_.altars[i].y*personal_.width+personal_.altars[i].x==tile) return static_cast<int>(i);
        return -1;
    }
    std::string job_fields(const plan3::JobId& id) const {
        TelemetryFields f; f << ",\"job\":[" << static_cast<int>(id.kind) << ',' << id.firstGroup
            << ',' << id.secondGroup << ',' << id.altarTile << ',' << id.equipmentBlock << ']'; return f.str();
    }
    bool enter_callback() {
        const auto round=cambc::round_number();
        if(callback_round_ && *callback_round_==round)return false;
        callback_round_=round;
        return true;
    }
    bool initialize_campaign() {
        if(initialization_stage_==0) {
            set_telemetry(&telemetry_);
            telemetry_.emit("start",std::string(",\"profile\":\"")+profile_+"\",\"model\":\""+plan3::model_version+"\",\"assumptions\":true");
            Executor::initialize(); initialization_stage_=1; return false;
        }
        if(initialization_stage_==1) {
            // Only the first immutable layout callback may precede raw reward
            // validation. All narrowing/member aggregation follows validation.
            if(!personal_.initialization_layout) { personal_.initialize_step();return false; }
            if(!backpressure_domain_.advance_static(personal_))return false;
            if(!personal_.initialize_step())return false;
            paths_.initialize(personal_.terrain);
            if (controller_==plan3::Controller::market) market_plan_=read_market_plan();
            for(std::size_t i=0;i<personal_.groups.size();++i)field_order_.push_back(i);
            std::stable_sort(field_order_.begin(),field_order_.end(),[&](auto a,auto b) {
                const auto& x=personal_.groups[a]; const auto& y=personal_.groups[b];
                if((x.incoming==0)!=(y.incoming==0))return x.incoming==0;
                return x.key<y.key;
            });
            initialization_stage_=2; return false;
        }
        if(cambc::fuel_remaining()<65000)return false;
        // Useful complete fields are published incrementally. A not-yet-built
        // group never receives a zero-distance estimate or a fictitious route.
        if(personal_.spawn_field<0) {
            personal_.spawn_field=paths_.add(personal_.own_spawn);
            return false;
        }
        if(altar_initialization_cursor_<personal_.altars.size()) {
            const auto altar=personal_.altars[altar_initialization_cursor_++];
            personal_.altar_fields.push_back(paths_.add(radius(altar,cambc::constants::submit_gems_range)));
            return false;
        }
        set_recovery_path(paths_,personal_.spawn_field);
        last_team_score_=cambc::global_submitted_gems(cambc::team::ally);
        last_score_round_=cambc::round_number();
        horizon_observations_.initialize(static_cast<int>(last_score_round_),static_cast<int>(last_team_score_),
            static_cast<int>(cambc::global_submitted_gems(cambc::team::enemy)));
        initialized_=true;
        if (controller_==plan3::Controller::market)
            telemetry_.emit("allocation",",\"complete\":"+std::string(market_plan_.complete?"true":"false")+
                ",\"groups\":"+std::to_string(market_plan_.groupIds.size())+",\"unavailable_fallback\":"+
                (market_plan_.complete?"false":"true"));
        return true;
    }
    void register_fields() {
        if(field_cursor_<field_order_.size() && cambc::fuel_remaining()>=75000) {
            auto& d=personal_.groups[field_order_[field_cursor_++]]; d.field=paths_.add(d.spawns);
        }
    }
    bool conversion_bank_stage() const {
        return active_ && (plan3::executing_bank(active_->id.kind,unsigned(stage_)) ||
            (active_->id.kind==plan3::JobKind::invest && (capital_prefix_ || stage_>=2)));
    }
    void finish_observation() {
        const unsigned snapshot_round=cambc::round_number();
        const auto& me=cambc::self();
        if(active_ && (me.health()>last_health_ || (recovering_ && me.position()!=last_position_)))last_progress_=cambc::round_number();
        last_position_=me.position(); last_bag_=me.gem_count(); last_gold_=me.gold_count();
        last_health_=me.health();
        const auto state=me.state();
        previous_submission_=state.contains(cambc::champion_state::submitting_gems);
        conversion_snapshot_={};
        conversion_snapshot_.round=snapshot_round;
        conversion_snapshot_.scoreRound=last_score_round_;
        conversion_snapshot_.ownScore=last_team_score_;
        conversion_snapshot_.bag=last_bag_;
        conversion_snapshot_.alive=state.contains(cambc::champion_state::alive);
        conversion_snapshot_.submitting=previous_submission_;
        if(conversion_bank_stage()) {
            conversion_snapshot_.bankingJob=active_->id;
            conversion_snapshot_.jobStarted=start_round_;
        }
        conversion_snapshot_.valid=snapshot_round==cambc::round_number();
    }
    void execute_bank(int altar) {
        auto& me=cambc::self();
        const bool attack=me.can_attack();
        const bool adjacent=chebyshev(me.position(),personal_.altars[std::size_t(altar)])<=cambc::constants::submit_gems_range;
        const bool handled=bank_at(personal_.altars[std::size_t(altar)],paths_,personal_.altar_fields[std::size_t(altar)]);
        if(last_move_status()==MoveStatus::moved || me.is_submitting_gems() || (handled && adjacent && attack))
            last_progress_=cambc::round_number();
    }
    plan3::InformationState information() const {
        const auto& me=cambc::self(); plan3::InformationState s;
        s.round=cambc::round_number(); s.roundLimit=cambc::constants::max_rounds;
        s.winningGems=cambc::constants::winning_gems;
        s.ownPermanentScore=cambc::global_submitted_gems(cambc::team::ally);
        s.opponentPermanentScore=cambc::global_submitted_gems(cambc::team::enemy);
        s.carriedGems=me.gem_count(); s.gold=me.gold_count(); s.actorOrdinal=static_cast<int>(me.champion_name());
        // Team handle counts are public; draft::team_size() is draft-only.
        s.teamSize=personal_.team_size;
        s.health=me.health(); s.maximumHealth=me.max_health();
        const auto p=me.position(); s.position.tile=p.y*personal_.width+p.x;
        s.equipment.provenance=plan3::Provenance::personally_observed;
        const auto items=me.item_count();
        for (unsigned i=0;i<static_cast<unsigned>(cambc::item_type::count);++i) {
            auto type=cambc::item_type{static_cast<cambc::item_type::enum_type>(i)};
            if (items[type]) s.equipment.items.emplace_back(static_cast<int>(i),items[type]);
        }
        for (std::size_t i=0;i<personal_.groups.size();++i) {
            plan3::GroupObservation g; g.minimumSpawnTile=personal_.groups[i].key;
            const auto& remembered=personal_.observations[i];
            g.memberSpawnCurrentlyVisible=personal_.current(i)!=nullptr;
            if (remembered.lifecycle) {
                g.lifecycle.value=static_cast<int>(*remembered.lifecycle);
                g.lifecycle.provenance=plan3::Provenance::personally_observed;
                g.lifecycle.observedRound=static_cast<int>(remembered.round);
            }
            s.groups.push_back(std::move(g));
        }
        for (const auto& observed:personal_.enemies) {
            plan3::ChampionObservation e; e.championOrdinal=observed.identity;
            e.position.value=plan3::Position{observed.position.y*personal_.width+observed.position.x};
            e.position.provenance=plan3::Provenance::personally_observed;
            e.position.observedRound=static_cast<int>(observed.round);
            s.champions.push_back(std::move(e));
        }
        return s;
    }
    ForecastContext forecast_context(const plan3::InformationState& s,
                                     std::optional<EquipmentStats>* exact=nullptr) const {
        ForecastContext c{personal_,paths_,s}; const auto& me=cambc::self();
        // Backpressure uses endpoint fields and its own renewal/return time;
        // generic terminal potential loops do not affect its paired model.
        c.terminal_renewal_loops=false;
        c.shared_loop_cache=&forecast_loops_;
        c.strength=me.strength(); c.focus=me.focus();
        c.monster_shards=monster_shards(); c.jungle_shards=jungle_shards();
        c.recall=cambc::constants::recall_time;
        // Until the immutable item catalogue is complete, the forecast is
        // conservative about owned passives. Exact reconstruction takes over
        // before any capital schedule is admitted.
        c.camp_damage=me.base_stats().camp_damage_multiplier;
        c.camp_mitigation=1;
        const auto equipment=capital_.exact_stats();
        if(exact)*exact=equipment;
        if (equipment) {
            c.camp_damage=equipment->campDamage; c.camp_mitigation=equipment->campMitigation;
            c.capture_gold=equipment->captureGoldMultiplier;
            c.recall=equipment->recall; c.info.equipment=equipment->equipment;
        }
        const double rate=cambc::constants::spawn_healing*s.maximumHealth;
        c.heal=rate>0 ? static_cast<int>(std::ceil(std::max(0.0,s.maximumHealth-s.health)/rate))+1 : s.roundLimit;
        for (std::size_t i=0;i<personal_.groups.size();++i)
            if (!personal_.current(i) && !personal_.remembered_respawn_minimum(i) &&
                paths_.get(personal_.groups[i].field,me.position())>=0 && c.clear(i,s.health).admitted) {
                c.uncertain_group=static_cast<int>(i); break;
            }
        return c;
    }
    void abort(const char* reason) {
        if (!active_) return;
        ++telemetry_.aborted;
        if(two_trip_episode_)++telemetry_.two_trip_aborted;
        telemetry_.emit("abort",job_fields(active_->id)+",\"reason\":\""+reason+"\",\"elapsed\":"+std::to_string(cambc::round_number()-start_round_));
        two_trip_episode_.reset();
        active_.reset(); investment_.reset(); capital_review_due_=false; observation_pending_=false; stage_=0; held_group_=-1;
        stage_lifecycle_.reset(); last_respawn_timer_=-1;
        if (unowned_episode_) { unowned_episode_=false; context_.turnsWithoutOwnedJob=0; }
    }
    void complete(bool conversion, unsigned observed_minimum=0,
                  std::optional<unsigned> exact_conversion=std::nullopt) {
        if (!active_) return;
        ++telemetry_.completed;
        if(two_trip_episode_) {
            if(conversion)++telemetry_.two_trip_completed;else ++telemetry_.two_trip_aborted;
        }
        const auto* predicted=active_->nominal();
        const auto elapsed=cambc::round_number()-start_round_;
        if (predicted) {
            telemetry_.duration_error+=std::abs(double(elapsed)-predicted->tau);
            telemetry_.health_error+=std::abs(cambc::self().health()-predicted->terminal.health);
            if(conversion && exact_conversion)
                telemetry_.conversion_error+=std::abs(double(*exact_conversion)-predicted->deltaS);
            // Attributable score is a lower bound when another capture can
            // resolve with tribute. Replay auditing resolves exact deltas.
        }
        telemetry_.emit("endpoint",job_fields(active_->id)+",\"elapsed\":"+std::to_string(elapsed)
            +",\"conversion\":"+(conversion?"true":"false")+",\"actor_score_lower_bound\":"+std::to_string(observed_minimum)
            +",\"exact_conversion\":"+(exact_conversion?std::to_string(*exact_conversion):"null")+
            ",\"start_health\":"+telemetry_number(start_health_)+
            ",\"end_health\":"+telemetry_number(cambc::self().health())+
            ",\"predicted_end_health\":"+telemetry_number(predicted?predicted->terminal.health:0));
        two_trip_episode_.reset();
        active_.reset(); investment_.reset(); capital_review_due_=false; observation_pending_=false; stage_=0; held_group_=-1;
        stage_lifecycle_.reset(); last_respawn_timer_=-1;
        if (unowned_episode_) { unowned_episode_=false; context_.turnsWithoutOwnedJob=0; }
    }
    void score_observations() {
        // Consume once even if a later SDK query throws or this callback resumes
        // in a different round; stale channel evidence cannot be credited twice.
        const auto previous=conversion_snapshot_;
        conversion_snapshot_.valid=false;
        const unsigned round=cambc::round_number(), own=cambc::global_submitted_gems(cambc::team::ally);
        const unsigned enemy=cambc::global_submitted_gems(cambc::team::enemy);
        const auto& me=cambc::self();
        const auto state=me.state();
        ConversionObservation now{round,own,me.gem_count(),false,
            state.contains(cambc::champion_state::alive),
            state.contains(cambc::champion_state::submitting_gems),
            state.contains(cambc::champion_state::submit_gems_cancelled)};
        now.valid=round==cambc::round_number();
        submission_interrupted_=false;
        if(!now.valid)return;
        const auto witness=observe_conversion(previous,now,cambc::constants::respawn_time);
        const bool personal_amount_known=witness.exactAmount.has_value();
        submission_interrupted_=witness.intervalKnown && previous.submitting && !now.submitting && now.bag>0;
        if(witness.confirmed())telemetry_.converted+=witness.lowerBound;
        if(active_ && conversion_completes_job(previous,witness,active_->id,start_round_,conversion_bank_stage())) {
            last_progress_=round;
            if (active_->id.kind==plan3::JobKind::invest) {
                telemetry_.emit("funding-conversion",job_fields(active_->id)+",\"actor_score_lower_bound\":"+std::to_string(witness.lowerBound));
                const bool funding=!capital_prefix_;
                capital_prefix_=false; stage_=0; held_group_=-1;
                funding_cycle_started_=false;
                capital_review_due_=true;
                stage_lifecycle_.reset(); last_respawn_timer_=-1;
                if(funding && me.health()<me.max_health())recovering_=true;
            } else complete(true,witness.lowerBound,witness.exactAmount);
        }
        horizon_observations_.observe(static_cast<int>(round),static_cast<int>(own),static_cast<int>(enemy),
            static_cast<int>(witness.lowerBound),personal_amount_known,context_.scoreHistory);
        last_team_score_=own; last_score_round_=round;
    }
    void belief_observations(const std::vector<int>& previously_observed_groups) {
        if (controller_!=plan3::Controller::belief && controller_!=plan3::Controller::scout) return;
        const unsigned now=cambc::round_number();
        const auto own_position=cambc::self().position();
        plan3::BeliefEvidence evidence;
        std::vector<int> locks;
        for (const auto& lock:cambc::vision::altar_locks(cambc::team::enemy)) {
            const auto p=lock.position(); const int tile=p.y*personal_.width+p.x;
            locks.push_back(tile);
            if (std::find(previously_visible_locks_.begin(),previously_visible_locks_.end(),tile)==previously_visible_locks_.end())
                evidence.newlyVisibleHostileLock=true;
        }
        previously_visible_locks_=std::move(locks);
        evidence.observedTributeTransition=submission_interrupted_;
        for (const auto& enemy:personal_.enemies) {
            // A remembered record or a second callback is not a new sighting.
            if (enemy.round!=now) continue;
            auto found=std::find_if(pursuit_.begin(),pursuit_.end(),[&](const auto& t){return t.identity==enemy.identity;});
            if (found==pursuit_.end()) {
                pursuit_.push_back({enemy.identity,own_position,enemy.position,now,0,0}); continue;
            }
            if (found->round>=now) continue;
            if (now-found->round>12 ||
                (found->approaches && now-found->first_approach_round>=12)) {
                *found={enemy.identity,own_position,enemy.position,now,0,0}; continue;
            }
            if (chebyshev(enemy.position,found->fixed)<chebyshev(found->last,found->fixed)) {
                if (!found->approaches) found->first_approach_round=now;
                if (++found->approaches==2) {
                    evidence.enemyMovedTowardPreviouslyFixedActorLocationTwice=true;
                    // Consume before event priority/block admission: this pair
                    // cannot become evidence again without two new movements.
                    found->fixed=own_position; found->approaches=0; found->first_approach_round=0;
                }
            } else if (enemy.position!=found->last) {
                found->fixed=own_position; found->approaches=0; found->first_approach_round=0;
            }
            found->last=enemy.position; found->round=now;
            for (const auto& old:personal_.previous_enemies) if (old.identity==enemy.identity && old.round<now && now-old.round<=12)
                for (const int key:previously_observed_groups) {
                    const int g=group_index(key);
                    if (g>=0 && chebyshev(enemy.position,personal_.groups[g].anchor)<chebyshev(old.position,personal_.groups[g].anchor))
                        evidence.enemyProgressTowardPreviouslyVisibleCamp=true;
                }
        }
        const auto previous=context_.belief.weights;
        const auto event=plan3::classify_event(evidence);
        plan3::belief_update(context_.belief,static_cast<int>(now),event,
                            controller_==plan3::Controller::belief && control_);
        if (context_.belief.weights!=previous) {
            TelemetryFields f; f<<",\"event\":"<<static_cast<int>(event)<<",\"weights\":[";
            for(unsigned n=0;n<4;++n) { if(n)f<<','; f<<context_.belief.weights[n]; }
            f<<"],\"last_evidence_round\":"<<context_.belief.lastEvidenceRound;
            telemetry_.emit("belief",f.str());
        }
    }
    std::optional<plan3::JobForecast> scout_proposal(const ForecastContext& c,
            std::vector<plan3::JobForecast>& jobs,const std::vector<PreparedAcquisition>& prepared,
            const std::vector<cambc::camp>& camps,plan3::EvaluationCache* chosen_evaluation=nullptr,
            ScoutCheckpoint* chosen_checkpoint=nullptr) {
        if(controller_!=plan3::Controller::scout)return std::nullopt;
        plan3::EvaluationCache local_retained;
        auto* retained_cache=chosen_evaluation?chosen_evaluation:&local_retained;
        retained_cache->clear();ScoutCheckpoint retained;
        if(chosen_checkpoint)*chosen_checkpoint={};
        const auto mark_unknown=[&] {
            c.cutoff=true;retained.unknown=true;
            if(chosen_checkpoint)*chosen_checkpoint=retained;
        };
        // Fixed active-then-control order in both actual arms. A completed
        // second selector may finish below the entry floor; never start one there.
        const auto checkpoint=[&](const auto& family,plan3::EvaluationCache& evaluation,
                                  ScoutCheckpoint& point,int target) {
            if(cambc::fuel_remaining()<45000)return false;
            const auto active=plan3::select(plan3::Controller::scout,false,c.info,family,context_,&evaluation);
            if(cambc::fuel_remaining()<45000)return false;
            const auto control=plan3::select(plan3::Controller::scout,true,c.info,family,context_,&evaluation);
            point.ready=true;point.target=target<0?-1:personal_.groups[target].key;
            point.intent=control_?control:active;point.companion=control_?active:control;
            return true;
        };
        // Per-world guards apply before the expensive potential, not after a
        // whole12-world row. This cache is fresh for every immutable target.
        const auto evaluate=[&](const auto& family,plan3::EvaluationCache& evaluation,
                                plan3::ObservationTable* table,std::vector<plan3::JobId>* ids,int* prior_best) {
            double prior=-std::numeric_limits<double>::infinity();
            for(const auto& job:family)if(job.id.kind!=plan3::JobKind::invest && job.id.kind!=plan3::JobKind::observe && plan3::admitted(c.info,job,true)) {
                std::vector<double> values;values.reserve(job.outcomes.size());
                plan3::EvaluationCache::Row* row=nullptr;
                for(std::size_t world=0;world<job.outcomes.size();++world) {
                    if(cambc::fuel_remaining()<45000)return false;
                    if(world==0)row=plan3::outcome_cache_row(c.info,job,&evaluation);
                    values.push_back(plan3::cached_outcome_value_row(c.info,job,world,&evaluation,row));
                }
                if(table) {
                    const auto value=plan3::weighted_mean(values,table->weights);
                    if(plan3::strictly_better(value,prior) || (plan3::finite_equal(value,prior) &&
                       (*prior_best<0 || job.id<(*ids)[static_cast<std::size_t>(*prior_best)]))) {
                        prior=value;*prior_best=static_cast<int>(ids->size());
                    }
                    ids->push_back(job.id);table->jobOutcomeValues.push_back(std::move(values));
                }
            }
            return true;
        };
        if(cambc::fuel_remaining()<45000) { mark_unknown();return std::nullopt; }
        // The declared pre-search fallback has four responses, no varying
        // lifecycle. Banks discard only identical lifecycle aliases; Renewal
        // repeats its same physical outcome under each response label.
        std::vector<std::pair<std::size_t,plan3::JobForecast>> aliases;aliases.reserve(4);
        const auto copy_ready=[] {return cambc::fuel_remaining()>=45000;};
        for(std::size_t index=0;index<jobs.size();++index)if(!plan3::productive(jobs[index].id.kind) && jobs[index].nominal()) {
            auto row=c.scout_response_aliases(jobs[index],{0},copy_ready);
            if(!row.complete) { mark_unknown();return std::nullopt; }
            aliases.emplace_back(index,std::move(row));
        }
        // No original row changes until every baseline alias has completed.
        for(auto& alias:aliases)jobs[alias.first]=std::move(alias.second);
        plan3::EvaluationCache baseline;
        if(!evaluate(jobs,baseline,nullptr,nullptr,nullptr) || !checkpoint(jobs,baseline,retained,-1)) {
            mark_unknown();return std::nullopt;
        }
        *retained_cache=std::move(baseline);
        if(chosen_checkpoint)*chosen_checkpoint=retained;
        if(c.cutoff) { mark_unknown();return std::nullopt; }
        if(cambc::fuel_remaining()<45000) { mark_unknown();return std::nullopt; }
        const auto availability=cambc::self().state();
        const bool observation_allowed=!active_ && held_group_<0 && availability.contains(cambc::champion_state::can_move) &&
            !availability.contains(cambc::champion_state::recalling) && !availability.contains(cambc::champion_state::submitting_gems) &&
            !availability.contains(cambc::champion_state::teleporting) &&
            static_cast<int>(telemetry_.turns)-context_.lastObservationTurn>=16;
        std::vector<int> targets;
        for(const auto& job:jobs)if(plan3::productive(job.id.kind))for(const int key:{job.id.firstGroup,job.id.secondGroup}) {
            const int index=group_index(key);
            if(index>=0 && !personal_.current(index) && !personal_.remembered_respawn_minimum(index))targets.push_back(index);
        }
        std::sort(targets.begin(),targets.end(),[&](int x,int y){return personal_.groups[x].key<personal_.groups[y].key;});
        targets.erase(std::unique(targets.begin(),targets.end()),targets.end());
        // Double partners use the same retained <=16-single pool.
        if(targets.size()>16) { mark_unknown();return std::nullopt; }
        plan3::ObservationTable table;std::vector<plan3::JobId> economic_ids;
        int prior_best=-1,selected_target=-1;bool sensitive=false;
        std::vector<plan3::JobForecast> fallback;plan3::EvaluationCache fallback_evaluation;ScoutCheckpoint fallback_checkpoint;
        bool search_unknown=false;
        for(const int target:targets) {
            if(cambc::fuel_remaining()<45000) { search_unknown=true;break; }
            const auto labels=c.lifecycle_values_for(target);if(labels.size()<2)continue;
            std::vector<plan3::JobForecast> family;
            family.reserve(plan3::single_job_cap+plan3::double_job_cap+plan3::nonharvest_job_cap);
            bool complete=true;
            for(const auto& frozen:jobs) {
                if(cambc::fuel_remaining()<45000) { complete=false;break; }
                plan3::JobForecast row;
                if(plan3::productive(frozen.id.kind)) {
                    const auto feature=std::find_if(prepared.begin(),prepared.end(),[&](const auto& f){return f.job.id==frozen.id;});
                    if(feature==prepared.end()) { complete=false;break; }
                    row=c.materialize_acquisition(*feature,target,labels,copy_ready);
                    if(row.id!=frozen.id || row.fullClearAdmitted!=frozen.fullClearAdmitted ||
                       row.routeKnown!=frozen.routeKnown || !row.complete) { complete=false;break; }
                    auto metadata=ForecastContext::scout_metadata(frozen);
                    metadata.nominalIndex=row.nominalIndex;metadata.outcomes=std::move(row.outcomes);
                    row=std::move(metadata);
                } else {
                    row=c.scout_response_aliases(frozen,labels,copy_ready);
                    if(!row.complete) { complete=false;break; }
                }
                if(!complete || row.outcomes.size()!=labels.size()*4) { complete=false;break; }
                family.push_back(std::move(row));
            }
            if(!complete || family.empty()) { search_unknown=true;break; }
            plan3::ObservationTable candidate;candidate.horizon=c.info.horizon();candidate.origin="frozen-single-target-menu";
            candidate.weights=plan3::normalized_weights(family.front(),&context_.belief.weights);
            for(const auto& outcome:family.front().outcomes)candidate.labels.push_back(outcome.lifecycle);
            std::vector<plan3::JobId> ids;int candidate_best=-1;plan3::EvaluationCache evaluation;
            if(!evaluate(family,evaluation,&candidate,&ids,&candidate_best)) { search_unknown=true;break; }
            int first_best=-1;bool disagreement=false;
            for(const int label:labels) {
                double best=-std::numeric_limits<double>::infinity();int winner=-1;
                for(std::size_t job=0;job<candidate.jobOutcomeValues.size();++job) {
                    double value=0,mass=0;
                    for(std::size_t world=0;world<candidate.labels.size();++world)if(candidate.labels[world]==label) {
                        value+=candidate.weights[world]*candidate.jobOutcomeValues[job][world];mass+=candidate.weights[world];
                    }
                    if(mass>0 && (plan3::strictly_better(value/mass,best) || (plan3::finite_equal(value/mass,best) &&
                       (winner<0 || ids[job]<ids[static_cast<std::size_t>(winner)])))) { best=value/mass;winner=static_cast<int>(job); }
                }
                if(winner>=0) { if(first_best<0)first_best=winner;else disagreement|=first_best!=winner; }
            }
            ScoutCheckpoint point;
            if(!checkpoint(family,evaluation,point,target)) { search_unknown=true;break; }
            if(disagreement) {
                jobs=std::move(family);*retained_cache=std::move(evaluation);retained=point;
                if(chosen_checkpoint)*chosen_checkpoint=retained;
                table=std::move(candidate);economic_ids=std::move(ids);prior_best=candidate_best;
                selected_target=target;sensitive=true;break;
            }
            if(fallback.empty()) {
                fallback=std::move(family);fallback_evaluation=std::move(evaluation);fallback_checkpoint=point;
            }
        }
        if(!sensitive) {
            if(!fallback.empty()) {
                jobs=std::move(fallback);*retained_cache=std::move(fallback_evaluation);retained=fallback_checkpoint;
                if(chosen_checkpoint)*chosen_checkpoint=retained;
            }
            if(search_unknown)mark_unknown();
            return std::nullopt;
        }
        if(cambc::fuel_remaining()>=45000) {
            TelemetryFields provenance;provenance<<",\"target\":"<<personal_.groups[selected_target].key
                <<",\"worlds\":"<<table.labels.size()<<",\"jobs\":"<<jobs.size()<<",\"sensitive\":true";
            telemetry_.emit("scout-table",provenance.str());
        }
        if(!observation_allowed)return std::nullopt;
        if(cambc::fuel_remaining()<45000) { mark_unknown();return std::nullopt; }
        double rho=0;
        for(const auto& loop:c.loops(cambc::self().position())) if(loop.supported && loop.period>0)
            rho=std::max(rho,double(loop.gems)/loop.period);
        if(c.cutoff || cambc::fuel_remaining()<45000) { mark_unknown();return std::nullopt; }
        int prior_field=-1;
        if(prior_best>=0) {
            const auto& id=economic_ids[std::size_t(prior_best)];
            const int g=group_index(id.firstGroup),a=altar_index(id.altarTile);
            if(plan3::productive(id.kind) && g>=0)prior_field=personal_.groups[g].field;
            else if(id.kind==plan3::JobKind::bank && a>=0)prior_field=personal_.altar_fields[a];
        }
        const auto p=cambc::self().position();
        const auto& target=personal_.groups[std::size_t(selected_target)];
        std::optional<cambc::vec2> selected; double best_net=0;
        unsigned considered=0;
        for (const auto step:blocked().reachable_tiles(p,1).positions()) {
            if(step==p)continue;
            if(considered++>=8)break;
            if(cambc::fuel_remaining()<45000) { mark_unknown();return std::nullopt; }
            bool safe=true;
            for(const auto& camp:camps) if(camp.is_alive() && camp.is_awake() && camp.strength()>0 &&
                chebyshev(step,camp.position())<=camp.attack_range())safe=false;
            for(const auto& enemy:personal_.enemies) if(chebyshev(step,enemy.position)<=enemy.range+1)safe=false;
            if(!safe || (cambc::self().visible_tiles_from(step)&target.spawns).is_empty())continue;
            double displacement=0;
            if(prior_field>=0) {
                const int before=paths_.get(prior_field,p),after=paths_.get(prior_field,step);
                if(before<0 || after<0)continue;
                displacement=rho*std::max(0,after-before);
            }
            const auto net=plan3::observation_net_value(table,rho,displacement);
            if(net && plan3::strictly_better(*net,best_net)) { selected=step; best_net=*net; }
        }
        if(cambc::fuel_remaining()<45000) { mark_unknown();return std::nullopt; }
        if(!selected || c.info.round+1>c.info.roundLimit)return std::nullopt;
        plan3::JobForecast j;
        j.id={plan3::JobKind::observe,target.key,-1,-1,selected->y*personal_.width+selected->x};
        j.observationNetValue=best_net; j.observationRevealsTarget=true; j.observationSafe=true;
        plan3::Outcome o; o.tau=1; o.terminal=c.terminal(*selected,c.info.health,c.info.carriedGems);
        j.outcomes.push_back(std::move(o));
        if(c.cutoff || cambc::fuel_remaining()<45000) { mark_unknown();return std::nullopt; }
        // Capacity was reserved before table evaluation, so this append cannot
        // invalidate economic cache keys. Keep the economic checkpoint unless
        // both active/control comparisons including OBSERVE finish.
        if(jobs.size()>=plan3::single_job_cap+plan3::double_job_cap+plan3::nonharvest_job_cap) {
            mark_unknown();return std::nullopt;
        }
        jobs.push_back(j);ScoutCheckpoint observed;
        const bool ready=checkpoint(jobs,*retained_cache,observed,selected_target);
        jobs.pop_back();
        if(!ready) { mark_unknown();return std::nullopt; }
        retained=observed;if(chosen_checkpoint)*chosen_checkpoint=retained;
        return j;
    }
    struct MenuAudit {
        bool captured=false,complete=false,admitted=false;
        std::uint64_t before=0,after=0;
        std::size_t worlds=0;
    };
    std::vector<plan3::JobForecast> menu(const ForecastContext& c, bool emergency,
                                      const std::vector<cambc::camp>& camps,const two_trip::Menu::Prefix& prefix,
                                      bool audit=false,bool winning_only=false,
                                      bool defer_capital=false,MenuAudit* audit_data=nullptr,plan3::EvaluationCache* scout_evaluation=nullptr,ScoutCheckpoint* scout_checkpoint=nullptr) {
        std::vector<plan3::JobForecast> jobs;
        jobs.reserve(plan3::single_job_cap+plan3::double_job_cap+plan3::nonharvest_job_cap);
        std::vector<PreparedAcquisition> scout_features;
        if(controller_==plan3::Controller::scout)scout_features.reserve(24);
        const auto p=cambc::self().position();
        if (c.info.carriedGems>0) {
            std::vector<std::pair<int,int>> altars;
            for (std::size_t a=0;a<personal_.altars.size();++a) {
                const int d=paths_.get(personal_.altar_fields[a],p); if (d>=0) altars.emplace_back(d,static_cast<int>(a));
            }
            std::sort(altars.begin(),altars.end());
            for (unsigned a=0;a<std::min<std::size_t>(2,altars.size());++a) jobs.push_back(c.bank(altars[a].second));
        }
        // The shared selector gives feasible winning Banks absolute priority.
        // Preserve their ordinary complete forecasts, but do not build jobs
        // that cannot affect this decision. Missing routes stay missing.
        if(winning_only)return jobs;
        const auto& nearby=prefix.groups;
        if(prefix.truncated)++telemetry_.cutoffs;
        bool candidate_logged=false;
        for (const auto& pair:nearby) {
            if (cambc::fuel_remaining()<60000) { ++telemetry_.cutoffs; break; }
            const auto before=cambc::fuel_remaining();
            PreparedAcquisition feature;
            auto j=controller_==plan3::Controller::scout?
                (feature=c.prepare_acquisition(pair.second),c.materialize_acquisition(feature,-1,{0},[] {return cambc::fuel_remaining()>=45000;})):c.acquisition(pair.second);
            if(controller_==plan3::Controller::scout && j.nominal())j.admissionTau=j.nominal()->tau;
            if(audit && !candidate_logged) {
                candidate_logged=true;
                if(audit_data)*audit_data={true,j.complete,j.fullClearAdmitted,before,cambc::fuel_remaining(),j.outcomes.size()};
                else {TelemetryFields f; f<<",\"phase\":\"first_candidate\",\"before\":"<<before
                    <<",\"after\":"<<cambc::fuel_remaining()<<",\"complete\":"<<j.complete
                    <<",\"admitted\":"<<j.fullClearAdmitted<<",\"worlds\":"<<j.outcomes.size()
                    <<",\"path_generation\":"<<paths_.generation()<<",\"fields_registered\":"<<field_cursor_;
                telemetry_.emit("compute",f.str());}
            }
            // Only admitted complete singles may supply nearest-eligible
            // partners. A known-invalid reward has no nominal outcome.
            if (plan3::admitted(c.info,j,false)) {
                if(controller_==plan3::Controller::scout)scout_features.push_back(std::move(feature));
                jobs.push_back(std::move(j));
            }
        }
        std::vector<const plan3::JobForecast*> rates;
        for(const auto& j:jobs)if(j.id.kind==plan3::JobKind::cycle && plan3::admitted(c.info,j,false))rates.push_back(&j);
        std::stable_sort(rates.begin(),rates.end(),[](const auto& a,const auto& b) {
            const double av=plan3::nominal_rate(*a),bv=plan3::nominal_rate(*b);
            return plan3::finite_equal(av,bv) ? a->id<b->id:av>bv;
        });
        unsigned doubles=0;
        for (const auto& first:rates) {
            if (doubles>=8 || cambc::fuel_remaining()<60000) break;
            const int i=group_index(first->id.firstGroup); int nearest=-1,bd=0;
            for (const auto& other:rates) if (other->id.firstGroup!=first->id.firstGroup) {
                const int k=group_index(other->id.firstGroup);
                const int d=paths_.minimum_between(personal_.groups[k].field,personal_.groups[i].field);
                if (d>=0 && (nearest<0 || d<bd ||
                    (d==bd && personal_.groups[k].key<personal_.groups[nearest].key))) { nearest=k; bd=d; }
            }
            if(nearest>=0) {
                PreparedAcquisition feature;
                auto j=controller_==plan3::Controller::scout?
                    (feature=c.prepare_acquisition(i,nearest),c.materialize_acquisition(feature,-1,{0},[] {return cambc::fuel_remaining()>=45000;})):c.acquisition(i,nearest);
                if(controller_==plan3::Controller::scout && j.nominal())j.admissionTau=j.nominal()->tau;
                if(plan3::admitted(c.info,j,false)) {
                    if(controller_==plan3::Controller::scout)scout_features.push_back(std::move(feature));
                    jobs.push_back(std::move(j));
                }
                ++doubles;
            }
        }
        if(cambc::fuel_remaining()>=50000)jobs.push_back(c.renewal(emergency));
        for(auto& j:jobs) if(plan3::productive(j.id.kind)) {
            const int actor=static_cast<int>(cambc::self().champion_name());
            j.owned=market_plan_.complete && market_plan_.owner(j.id.firstGroup)==actor &&
                (j.id.secondGroup<0 || market_plan_.owner(j.id.secondGroup)==actor);
        }
        capital_candidate_.reset();
        if(!defer_capital && (controller_==plan3::Controller::treasury || controller_==plan3::Controller::council)) {
            capital_candidate_=controller_==plan3::Controller::council ?
                capital_.council_proposal(c,jobs):capital_.proposals(c,jobs,control_);
            if(capital_candidate_)jobs.push_back(capital_candidate_->investmentJob);
        }
        if(controller_==plan3::Controller::scout) {
            for(auto& job:jobs)if(job.nominal() && !job.admissionTau)job.admissionTau=job.nominal()->tau;
            jobs.erase(std::remove_if(jobs.begin(),jobs.end(),[&](const auto& job){return !plan3::admitted(c.info,job,false);}),jobs.end());
            if(auto observation=scout_proposal(c,jobs,scout_features,camps,scout_evaluation,scout_checkpoint))jobs.push_back(std::move(*observation));
        }
        if(controller_==plan3::Controller::council && !defer_capital) {
            return plan3::council_proposals(c.info,jobs);
        }
        return jobs;
    }
    void horizon_context(const plan3::InformationState& state,const plan3::JobIntent& intent,const plan3::JobIntent& companion,
                         const std::vector<plan3::JobForecast>& jobs) {
        if(controller_!=plan3::Controller::horizon || !plan3::horizon_ready(context_.scoreHistory,state.round) ||
            intent.reason!=plan3::DecisionReason::objective || companion.reason!=plan3::DecisionReason::objective ||
            !intent.job || !companion.job || cambc::fuel_remaining()<45000)return;
        const bool fork=intent.job!=companion.job;
        if((fork && horizon_witness_logged_) || (!fork && horizon_context_attempted_))return;
        std::array<const plan3::Outcome*,2> forecasts{};
        for(const auto& job:jobs) {
            if(job.id==*intent.job)forecasts[0]=job.nominal();
            if(job.id==*companion.job)forecasts[1]=job.nominal();
        }
        for(const auto* forecast:forecasts)if(!forecast || forecast->conversions.size()>2)return;
        const auto fields=horizon_trace_fields(state,context_.scoreHistory,{*intent.job,*companion.job},forecasts,intent.assumedShare,fork);
        if(fields.empty())return;
        if(fork) {
            horizon_witness_logged_=telemetry_.emit_context(fields.str());
            horizon_context_attempted_|=horizon_witness_logged_;
        }
        else { telemetry_.emit("horizon-context",fields.str()); horizon_context_attempted_=true; }
    }
    void backpressure_menu(std::size_t original_jobs) {
        const auto& input=backpressure_input_;
        if(cambc::fuel_remaining()<45000 || cambc::round_number()!=unsigned(input.snapshotRound)) {
            ++telemetry_.details_omitted;return;
        }
        constexpr unsigned job_cap=6,group_cap=2;
        TelemetryFields f;f<<",\"bp\":1,\"arm\":"<<unsigned(backpressure_rate_)<<",\"raw\":"<<original_jobs
            <<",\"m\":"<<input.jobCount<<",\"g\":"<<input.groupCount
            <<",\"mt\":"<<(input.jobCount>job_cap)<<",\"gt\":"<<(input.groupCount>group_cap)<<",\"in\":["
            <<input.snapshotRound<<','<<input.observationRound<<','<<input.roundLimit<<','<<input.width<<','<<input.height
            <<','<<input.carriedGems<<','<<input.ownPermanentScore<<','<<input.winningGems
            <<','<<unsigned(input.submitting)<<','<<unsigned(input.candidatePrefix)<<','<<int(input.state)<<"],\"jobs\":[";
        for(unsigned j=0;j<std::min(input.jobCount,job_cap);++j) {
            if(j)f<<',';const auto& job=input.jobs[j];f<<'[';
            for(unsigned k=0;k<5;++k) {if(k)f<<',';f<<job.key[k];}
            const unsigned flags=unsigned(job.complete)|(unsigned(job.nominalKnown)<<1)|(unsigned(job.routeKnown)<<2)
                |(unsigned(job.fullClearAdmitted)<<3)|(unsigned(job.recoveryAdmitted)<<4)|(unsigned(job.sourceDomainValidated)<<5)
                |(unsigned(job.censored)<<6)|(unsigned(job.renewalRequired)<<7);
            f<<','<<int(job.state)<<','<<int(job.unavailableReason)<<','<<flags<<','<<job.nominalTau
                <<','<<job.renewalReturn<<','<<job.converted<<']';
        }
        f<<"],\"groups\":[";
        for(unsigned g=0;g<std::min(input.groupCount,group_cap);++g) {
            if(g)f<<',';const auto& group=input.groups[g];
            f<<'['<<group.key<<','<<group.gems<<','<<int(group.lifecycle)<<','<<group.observedRound<<','<<int(group.state)<<']';
        }
        f<<']';telemetry_.emit_menu(f.str());
    }
    void backpressure_context(const plan3_backpressure::Selection& selected,std::size_t original_jobs) {
        const auto& p=selected.pair;
        if(backpressure_context_emitted_ || selected.priority || p.state!=plan3_backpressure::State::known ||
           !p.active.present || !p.control.present || (!backpressure_menu_evidence_ && p.active.key==p.control.key))return;
        if(backpressure_menu_evidence_ && (cambc::fuel_remaining()<45000 ||
           cambc::round_number()!=unsigned(backpressure_input_.snapshotRound))) {++telemetry_.details_omitted;return;}
        const auto& row=p.rows[backpressure_rate_?p.control.row:p.active.row];
        TelemetryFields f;f<<",\"bp\":1,\"arm\":"<<unsigned(backpressure_rate_)<<",\"Q\":[";
        for(unsigned i=0;i<4;++i) {if(i)f<<',';f<<p.queues[i];}
        f<<"],\"s\":[";for(unsigned i=0;i<4;++i) {if(i)f<<',';f<<row.service[i];}
        f<<"],\"D\":"<<row.duration<<",\"a\":[\""<<plan3_backpressure::decimal(p.active.score.numerator)<<"\","<<p.active.score.denominator
            <<"],\"r\":[\""<<plan3_backpressure::decimal(p.control.score.numerator)<<"\","<<p.control.score.denominator<<"],\"ids\":[";
        for(unsigned arm=0;arm<2;++arm) {
            if(arm)f<<',';f<<'[';const auto& key=arm?p.control.key:p.active.key;
            for(unsigned i=0;i<5;++i) {if(i)f<<',';f<<key[i];}f<<']';
        }
        f<<"],\"omitted\":"<<p.omittedDependencyCount<<",\"prefix\":"<<p.candidatePrefix<<",\"assumed\":true"
            <<",\"diagnostic\":"<<backpressure_menu_evidence_<<",\"fork\":"<<(p.active.key!=p.control.key);
        backpressure_context_emitted_=telemetry_.emit_context(f.str());
        if(backpressure_context_emitted_ && backpressure_menu_evidence_)backpressure_menu(original_jobs);
    }
    void choose(const plan3::InformationState& state, bool emergency,const std::vector<cambc::camp>& camps,bool winning_only=false) {
        const bool audit_compute=planning_audits_++<2;
        const auto menu_start_fuel=cambc::fuel_remaining();
        const bool capital_mode=controller_==plan3::Controller::treasury || controller_==plan3::Controller::council;
        MenuAudit menu_audit;
        plan3::EvaluationCache evaluation;
        ScoutCheckpoint scout_checkpoint;
        context_.ownerTurn=telemetry_.turns;
        context_.activeJob=active_?std::optional<plan3::JobId>{active_->id}:std::nullopt;
        context_.newlyFeasibleWinningBank=state.carriedGems>0 &&
            state.carriedGems>=state.winningGems-state.ownPermanentScore;
        const auto set_bank_proxy=[&] {
            context_.b0BankRecommended=fundamentals::should_bank(state.carriedGems,state.round,
                [&] { int d=10000; for(auto altar:personal_.altars)d=std::min(d,chebyshev(cambc::self().position(),altar)); return d; }());
        };
        // Scout's paired selector checkpoint is made inside menu(). Every
        // governing context field must already describe this owner callback.
        if(controller_==plan3::Controller::scout)set_bank_proxy();
        const bool extend=controller_==plan3::Controller::ledger && !active_ && !winning_only && !emergency &&
            state.carriedGems==0 && state.actorOrdinal==static_cast<int>(cambc::champion_type::tavi);
        std::optional<EquipmentStats> exact;
        auto c=forecast_context(state,extend?&exact:nullptr);
        const auto prefix=winning_only ? two_trip::Menu::Prefix{}:
            two_trip::Menu::single_prefix(personal_,paths_,cambc::self().position());
        // This interval performs queries only. A round change means execution
        // suspended; Menu then rejects the early live candidate before use.
        if(extend)two_trip_menu_.prepare_early(personal_,exact,paths_,state,prefix,
            [] {return cambc::fuel_remaining()>=65000;});
        if(cambc::round_number()!=static_cast<unsigned>(state.round)) {++telemetry_.cutoffs;return;}
        auto ordinary=menu(c,emergency,camps,prefix,audit_compute,winning_only,capital_mode,&menu_audit,&evaluation,&scout_checkpoint);
        if(cambc::round_number()!=static_cast<unsigned>(state.round)) {++telemetry_.cutoffs;return;}
        // Council's baseline contains only the available ordinary proposers.
        // Missing shopping is an unknown opportunity, never a complete four-proposer claim.
        auto jobs=controller_==plan3::Controller::council ? plan3::council_proposals(state,ordinary):std::move(ordinary);
        planning_trace_.mark(PlanningGateTrace::Point::menu);
        if(extend) {
            const auto prepared=two_trip_menu_.merge(state,jobs);
            if(two_trip::Menu::defer_selection(prepared,state,jobs)) {
                if(prepared==two_trip::Status::pending)++telemetry_.preparations;else ++telemetry_.cutoffs;
                planning_trace_.mark(PlanningGateTrace::Point::extended);
                planning_trace_.defer(PlanningGateTrace::Gate::preparation,
                    prepared==two_trip::Status::pending?"pending":"cutoff");
                return; // Unfinished geometry never supplies an executable job.
            }
        }
        planning_trace_.mark(PlanningGateTrace::Point::extended);
        if(controller_!=plan3::Controller::scout)set_bank_proxy();
        if(controller_==plan3::Controller::market) {
            bool eligible=false;
            for(const auto& j:jobs)if(j.owned && plan3::productive(j.id.kind) && plan3::admitted(state,j,false))eligible=true;
            if(eligible)context_.turnsWithoutOwnedJob=0;
            else if(last_owned_review_+1==telemetry_.turns)++context_.turnsWithoutOwnedJob;
            else context_.turnsWithoutOwnedJob=1;
            last_owned_review_=telemetry_.turns;
            context_.allowOneUnownedJob=!unowned_episode_;
        }
        if(!scout_checkpoint.ready && ((controller_==plan3::Controller::scout && scout_checkpoint.unknown) ||
            cambc::fuel_remaining()<45000)) {
            ++telemetry_.cutoffs;
            planning_trace_.defer(PlanningGateTrace::Gate::menu_fuel,"below_45000_or_scout");
            if(audit_compute)telemetry_.emit("compute",",\"phase\":\"selection_deferred\",\"fuel\":"+std::to_string(cambc::fuel_remaining()));
            return;
        }
        plan3::JobIntent intent,companion;
        unsigned backpressure_eligible_count=0;
        std::optional<plan3::RiskPrefixSelection> risk;
        TelemetryFields risk_fields;
        if(scout_checkpoint.ready) {
            intent=scout_checkpoint.intent;companion=scout_checkpoint.companion;
            risk_fields<<",\"scout_table\":["<<scout_checkpoint.target<<','<<scout_checkpoint.unknown<<']';
        } else if(controller_==plan3::Controller::insurance || controller_==plan3::Controller::robust ||
            controller_==plan3::Controller::belief) {
            risk.emplace(plan3::risk_prefix_select(controller_,control_,state,jobs,context_,evaluation,
                [](plan3::RiskGuard) {return cambc::fuel_remaining()>=45000;}));
            if(risk->cutoff)++telemetry_.cutoffs;
            if(!risk->selected)return; // No whole paired checkpoint: preserve/yield.
            intent=risk->intent;companion=risk->companion;
            risk_fields<<",\"risk_prefix\":["<<risk->prefix.size()<<','<<risk->eligible<<','<<risk->scored
                <<','<<risk->cutoff<<','<<risk->priorityFast<<']';
        } else {
            const bool unknown_gem=backpressure_domain_.unknown_gem_opportunity(personal_,unsigned(state.round));
        const auto selection=plan3_backpressure::select_menu(personal_,state,jobs,backpressure_rate_,
            cambc::self().is_submitting_gems(),true,context_,unknown_gem,
            [] {return cambc::fuel_remaining()>=45000;},&backpressure_input_);
        planning_trace_.mark(PlanningGateTrace::Point::paired);
        backpressure_eligible_count=selection.pair.state==plan3_backpressure::State::known?selection.pair.eligibleCount:0;
        if(!selection.priority && selection.pair.state!=plan3_backpressure::State::known) {
            planning_trace_.defer(PlanningGateTrace::Gate::pair,"pair_unavailable",static_cast<int>(selection.pair.state));
            ++telemetry_.cutoffs;return;
        }
        planning_trace_.defer(PlanningGateTrace::Gate::pair_ready,selection.priority?"priority":"known");
        intent=selection.intent;companion=selection.companion;
        backpressure_context(selection,jobs.size());
        /* Both objectives were atomically completed above, active then rate.
           The old generic Ledger selector is deliberately not a fallback. */
        const auto first=intent;
            if(capital_mode && cambc::fuel_remaining()<45000) {++telemetry_.cutoffs;return;}
            const auto second=companion;
            intent=capital_mode && control_?second:first;
            companion=capital_mode && control_?first:second;
        }
        bool capital_unknown=false;
        CapitalTrace capital_trace;
        CapitalWork capital_work;
        if(capital_mode && !winning_only) {
            const auto& funding_menu=controller_==plan3::Controller::council ? ordinary:jobs;
            const auto built=capital_.build_schedules(c,funding_menu);
            capital_trace=built.work.unknown;
            capital_work=built.work;
            capital_unknown=!built.complete;
            if(built.complete) {
                auto proposal=TreasuryPlanner::select_proposal(built,control_,controller_==plan3::Controller::council);
                if(proposal) {
                    if(cambc::fuel_remaining()<45000) {capital_unknown=true;capital_trace.final_failure(CapitalUnknown::finalBeforeFuel);}
                    else {
                        auto full=controller_==plan3::Controller::council ? std::move(ordinary):std::move(jobs);
                        full.push_back(proposal->investmentJob);
                        if(controller_==plan3::Controller::council)full=plan3::council_proposals(state,full);
                        // New container addresses must never reuse baseline evaluation rows.
                        plan3::EvaluationCache completed_evaluation;
                        if(cambc::fuel_remaining()<45000) {capital_unknown=true;capital_trace.final_failure(CapitalUnknown::finalContainerFuel);}
                        else {
                            const auto completed_active=plan3::select(controller_,false,state,full,context_,&completed_evaluation);
                            if(cambc::fuel_remaining()<45000) {capital_unknown=true;capital_trace.final_failure(CapitalUnknown::finalActiveFuel);}
                            else {
                                const auto completed_control=plan3::select(controller_,true,state,full,context_,&completed_evaluation);
                                if(cambc::fuel_remaining()<45000) {capital_unknown=true;capital_trace.final_failure(CapitalUnknown::finalControlFuel);}
                                else {
                                    intent=control_?completed_control:completed_active;
                                    companion=control_?completed_active:completed_control;
                                    capital_candidate_=std::move(proposal);evaluation=std::move(completed_evaluation);
                                }
                            }
                        }
                        // If the guarded final comparison could not finish, restore
                        // the ordinary menu containing the already selected baseline.
                        if(capital_unknown) {
                            if(controller_!=plan3::Controller::council) {
                                jobs=std::move(full);jobs.pop_back();
                            }
                            evaluation={};
                        } else jobs=std::move(full);
                    }
                }
            }
            if(capital_unknown)++telemetry_.capital_unknown;
        }
        // Diagnostics may inspect only the remaining reserve, after a usable
        // intent exists. They cannot block ordinary-prefix selection.
        if(audit_compute && !capital_unknown && cambc::fuel_remaining()>=65000) {
            telemetry_.emit("compute",",\"phase\":\"menu_start\",\"fuel\":"+std::to_string(menu_start_fuel));
            if(menu_audit.captured && cambc::fuel_remaining()>=65000) {
                TelemetryFields f;f<<",\"phase\":\"first_candidate\",\"before\":"<<menu_audit.before
                    <<",\"after\":"<<menu_audit.after<<",\"complete\":"<<menu_audit.complete
                    <<",\"admitted\":"<<menu_audit.admitted<<",\"worlds\":"<<menu_audit.worlds
                    <<",\"path_generation\":"<<paths_.generation()<<",\"fields_registered\":"<<field_cursor_;
                telemetry_.emit("compute",f.str());
            }
            unsigned reachable=0,safe=0;double minimum_loss=std::numeric_limits<double>::infinity();
            for(std::size_t i=0;!winning_only && !capital_unknown && i<std::min<std::size_t>(16,personal_.groups.size()) && cambc::fuel_remaining()>=65000;++i)
                if(paths_.get(personal_.groups[i].field,cambc::self().position())>=0) {
                    ++reachable;const auto clear=c.clear(i,state.health);
                    safe+=clear.admitted;minimum_loss=std::min(minimum_loss,clear.healthLoss);
                }
            if(cambc::fuel_remaining()>=65000) {
                TelemetryFields f;f<<",\"phase\":\"menu_complete\",\"fuel\":"<<cambc::fuel_remaining()
                    <<",\"jobs\":"<<jobs.size()<<",\"reachable_groups\":"<<reachable<<",\"safe_groups\":"<<safe<<",\"minimum_loss\":";
                if(std::isfinite(minimum_loss))f<<minimum_loss;else f<<"null";
                f<<",\"cutoff\":"<<(c.cutoff?"true":"false")<<",\"sample_prefix\":true";telemetry_.emit("compute",f.str());
            }
        }
        if(audit_compute && !capital_unknown && cambc::fuel_remaining()>=65000)telemetry_.emit("compute",",\"phase\":\"selection_complete\",\"fuel\":"+std::to_string(cambc::fuel_remaining())+
            ",\"y_evaluations\":"+std::to_string(evaluation.computations)+",\"y_cache_hits\":"+std::to_string(evaluation.hits)+
            ",\"horizon_evaluations\":"+std::to_string(evaluation.horizonComputations)+",\"horizon_cache_hits\":"+std::to_string(evaluation.horizonHits)+risk_fields.str());
        const bool bootstrap_selection=opening_bootstrap::unfinished(*this) &&
            opening_bootstrap::prefer_single(state,jobs,intent,companion);
        horizon_context(state,intent,companion,jobs);
        // Backpressure publishes exact context, never unrelated Y diagnostics.
        unsigned alternatives=0;
        alternatives=backpressure_eligible_count;
        if(alternatives>=2)++telemetry_.opportunities;
        const bool disagreement=intent.job!=companion.job;
        if(disagreement)++telemetry_.disagreements;
        ++telemetry_.decisions;
        const bool robust_degeneracy=controller_==plan3::Controller::robust && !control_ &&
            intent.reason==plan3::DecisionReason::robust_degeneracy;
        if(robust_degeneracy)++telemetry_.robust_degeneracies;
        if (intent.status!=plan3::IntentStatus::selected || !intent.job) return;
        for (auto& j:jobs) if (j.id==*intent.job) {
            if(cambc::round_number()!=static_cast<unsigned>(state.round)) {++telemetry_.cutoffs;return;}
            if (active_ && active_->id==j.id) return;
            const bool owns_two_trip=extend && two_trip_menu_.owns(j.id);
            auto episode=owns_two_trip?two_trip_menu_.take(j.id):nullptr;
            if(owns_two_trip && !episode) {++telemetry_.cutoffs;return;}
            if (active_) abort("reselection");
            ++telemetry_.activations;
            bootstrap_pending_shop_=bootstrap_selection;
            two_trip_episode_=std::move(episode);
            if(two_trip_episode_)++telemetry_.two_trip_started;
            active_=std::move(j); stage_=0; start_round_=state.round; last_progress_=state.round;
            const auto& chosen=*active_;
            last_review_=state.round; held_group_=-1; stage_lifecycle_.reset(); last_respawn_timer_=-1;
            if(chosen.id.kind==plan3::JobKind::invest) {
                if(!capital_candidate_ || capital_candidate_->investmentJob.id!=chosen.id) { abort("missing-capital-plan"); return; }
                investment_=*capital_candidate_;
                capital_prefix_=investment_->cashoutAltar>=0 && state.carriedGems>0;
                funding_cycle_started_=false;
                capital_review_due_=false;
            }
            if(controller_==plan3::Controller::market && !control_ && plan3::productive(chosen.id.kind) && !chosen.owned) {
                unowned_episode_=true; context_.turnsWithoutOwnedJob=0;
            }
            start_health_=state.health;
            if(cambc::fuel_remaining()<45000) {
                ++telemetry_.details_omitted;return;
            }
            const auto* n=active_->nominal();
            TelemetryFields comparison;
            comparison<<",\"companion_job\":";
            if(companion.job) {
                const auto& id=*companion.job;
                comparison<<'['<<static_cast<int>(id.kind)<<','<<id.firstGroup<<','<<id.secondGroup<<','<<id.altarTile<<','<<id.equipmentBlock<<']';
            } else comparison<<"null";
            comparison<<",\"changed\":"<<(disagreement?"true":"false")<<",\"companion_objective\":";
            if(std::isfinite(companion.objective))comparison<<companion.objective; else comparison<<"null";
            comparison<<",\"counterfactual_only\":true";
            comparison<<risk_fields.str();
            if(controller_==plan3::Controller::horizon) {
                const bool warmup=!plan3::horizon_ready(context_.scoreHistory,state.round);
                comparison<<",\"warmup\":"<<warmup<<",\"share_assumed\":";
                if(warmup || intent.reason!=plan3::DecisionReason::objective)comparison<<"null";
                else comparison<<intent.assumedShare;
            }
            std::optional<CapitalMeterFragment> capital_fragment;
            if(capital_mode && !winning_only)capital_fragment.emplace(capital_trace,capital_work.deferMeter,capital_work.targets,
                capital_work.forks,capital_work.loopRequests,capital_work.terminalRequests,capital_work.schedulesAppended);
            telemetry_.emit("prediction",job_fields(chosen.id)+",\"objective\":"+telemetry_number(intent.objective)
                +",\"tau\":"+std::to_string(n?n->tau:0)+",\"health_loss\":"+telemetry_number(n?n->healthLoss:0)
                +",\"conversion\":"+std::to_string(n?n->deltaS:0)+",\"worlds\":"+std::to_string(active_->outcomes.size())
                +",\"fuel_before_cleanup\":"+std::to_string(cambc::fuel_remaining())
                +(robust_degeneracy?",\"provenance\":\"assumed-degenerate\"":capital_mode && !winning_only?
                    capital_fragment->fragment():",\"provenance\":\"backpressure-assumed\"")+comparison.str());
            return;
        }
    }
    void prioritize_active_observation() {
        if(!active_ || stage_>=2 || cambc::fuel_remaining()<30000)return;
        const int g=group_index(stage_==0?active_->id.firstGroup:active_->id.secondGroup);
        if(g<0 || personal_.current(std::size_t(g)))return;
        const auto& d=personal_.groups[std::size_t(g)];
        if((d.spawns&cambc::vision::visible_tiles()).is_empty())return;
        auto& o=personal_.observations[std::size_t(g)]; o={}; o.round=personal_.observation_round;
        const auto state=d.group.state();
        if(const auto* r=std::get_if<cambc::map::camp_group_respawning>(&state)) {
            o.lifecycle=Lifecycle::respawning; o.respawn_remaining=r->timer;
        } else if(std::holds_alternative<cambc::map::camp_group_reward_available>(state))o.lifecycle=Lifecycle::reward;
        else { o.lifecycle=Lifecycle::alive; o.remaining_health=d.full_health; o.incoming=d.incoming; }
    }
    void remember_hold(int group) {
        const int key=personal_.groups[std::size_t(group)].key;
        const auto now=cambc::round_number();
        const auto position=cambc::self().position();
        const bool new_occupation=held_group_!=key || held_position_!=position;
        held_group_=key; held_round_=now; held_position_=position;
        stage_lifecycle_=Lifecycle::reward;
        // A fresh occupation advances the job. Repeated waiting is evidence
        // for a later capture, not an observable countdown or fresh progress.
        // Reacquiring the same observation after a gap also proves no progress.
        if(new_occupation)last_progress_=now;
    }
    bool capture_contested(int group,const std::vector<cambc::champion>& enemies) const {
        const auto& region=personal_.groups[std::size_t(group)].spawns;
        return std::any_of(enemies.begin(),enemies.end(),[&](const auto& enemy) {
            return enemy.is_alive() && region.is_set(enemy.position());
        });
    }
    bool protect_reward_hold(const std::vector<cambc::champion>& enemies) {
        if(!active_ || stage_>=2)return false;
        const int g=group_index(stage_==0?active_->id.firstGroup:active_->id.secondGroup);
        if(g<0)return false;
        const auto* o=personal_.current(g);
        if(!o || o->lifecycle!=Lifecycle::reward || !personal_.groups[g].spawns.is_set(cambc::self().position()))return false;
        if(capture_contested(g,enemies)) { abort("capture-contested"); return false; }
        remember_hold(g); return true;
    }
    bool capture_transition() {
        if(!active_ || stage_>=2)return false;
        const auto id=active_->id;
        const int key=stage_==0?id.firstGroup:id.secondGroup,g=group_index(key);
        if(g<0)return false;
        const auto* o=personal_.current(std::size_t(g));
        if(!o || o->lifecycle!=Lifecycle::respawning || held_group_!=key)return false;
        auto& me=cambc::self();
        const bool witnessed=held_round_+1==cambc::round_number() && me.position()==held_position_ &&
            personal_.groups[std::size_t(g)].spawns.is_set(me.position());
        held_group_=-1;
        if(!witnessed) { abort("capture-observation-gap"); return true; }
        ++telemetry_.captures;
        stage_=(id.secondGroup>=0 && stage_==0)?1:2;
        stage_lifecycle_.reset(); last_respawn_timer_=-1; last_progress_=cambc::round_number();
        telemetry_.emit("capture",job_fields(id)+",\"group\":"+std::to_string(key)+
            ",\"bag_gain_lower_bound\":"+std::to_string(me.gem_count()>last_bag_?me.gem_count()-last_bag_:0));
        if(id.kind==plan3::JobKind::invest)capital_review_due_=true;
        if(active_->rewardOnlyEndpoint && stage_>=2) { complete(false); return true; }
        if(id.kind==plan3::JobKind::invest && stage_>=2 && !me.gem_count()) {
            // A funding capture can legally pay gold with no personal gems.
            // It supplies no conversion event and needs no zero-bag tribute.
            stage_=0; funding_cycle_started_=false;
            if(me.health()<me.max_health())recovering_=true;
            telemetry_.emit("funding-capture",job_fields(id)+",\"conversion\":false");
            return true;
        }
        if(!me.gem_count() && stage_>=2 && id.kind!=plan3::JobKind::invest) {
            abort("empty-conversion-endpoint"); return true;
        }
        if(id.kind==plan3::JobKind::double_cycle && stage_==1 && controller_!=plan3::Controller::options)
            abort("acquisition-milestone");
        return true;
    }
    void review_remaining(const plan3::InformationState& state,const plan3::JobForecast& remaining,bool acquisition) {
        // A fuel-truncated calculation is unknown. A completed calculation
        // that rejects the remaining work invalidates the commitment.
        if(!remaining.complete)return;
        if(acquisition && !remaining.fullClearAdmitted) { abort("full-clear-invalid"); return; }
        if(!remaining.routeKnown) { abort("observed-route-invalid"); return; }
        const auto* n=remaining.nominal();
        if(!n) { abort("observed-forecast-invalid"); return; }
        if(state.round+n->tau>state.roundLimit)abort("deadline-infeasible");
    }
    bool recheck_investment() {
        if(!active_ || active_->id.kind!=plan3::JobKind::invest || !investment_)return false;
        capital_review_due_=true;
        if(cambc::fuel_remaining()<45000)return false;
        auto c=forecast_context(information());
        const auto review=capital_.recheck(c,*investment_,
            {capital_prefix_,funding_cycle_started_,stage_,recovering_},controller_==plan3::Controller::council);
        if(review.status==CapitalReviewStatus::unknown)return false;
        if(review.status==CapitalReviewStatus::defer) { abort("investment-payback-invalid"); return false; }
        if(review.status==CapitalReviewStatus::target_complete)investment_->shardsRemaining=0;
        else if(review.proposal)investment_=*review.proposal;
        else return false;
        capital_review_due_=false;
        return true;
    }
    void review_active(const plan3::InformationState& state) {
        if(!active_ || (state.round-static_cast<int>(last_review_)<8 && !capital_review_due_) || cambc::fuel_remaining()<45000)return;
        last_review_=state.round;
        if(active_->id.kind==plan3::JobKind::observe)return;
        const bool investing=active_->id.kind==plan3::JobKind::invest;
        if(investing && !investment_) { abort("missing-capital-plan"); return; }
        if(investing && !recheck_investment())return;
        const bool investment_banking=investing && (capital_prefix_ || (funding_cycle_started_ && stage_>=2));
        if(plan3::executing_bank(active_->id.kind,stage_) || investment_banking) {
            const int a=altar_index(investment_banking && capital_prefix_ ? investment_->cashoutAltar:active_->id.altarTile);
            if(a<0) { abort("missing-altar"); return; }
            // A bank review only checks observed route and nominal deadline.
            // Avoid farm feasibility, future renewal features and 12-world work.
            ForecastContext bank_context{personal_,paths_,state};
            const auto remaining=bank_context.bank_completion(a);
            if(state.carriedGems>0 && !remaining)abort("observed-route-invalid");
            else if(remaining && state.round+*remaining>state.roundLimit)abort("deadline-infeasible");
            return;
        }
        // Capital recheck includes its fixed pending acquisition, settlement,
        // purchase and recovery. It never substitutes another funding camp.
        if(investing)return;
        auto c=forecast_context(state); plan3::JobForecast remaining;
        if(active_->id.kind==plan3::JobKind::renew)remaining=c.renewal(false);
        else {
            const int g=group_index(stage_==0?active_->id.firstGroup:active_->id.secondGroup);
            const int k=stage_==0?group_index(active_->id.secondGroup):-1;
            if(g<0) { abort("missing-group"); return; }
            remaining=c.acquisition_review(g,k);
        }
        review_remaining(state,remaining,active_->id.kind==plan3::JobKind::cycle || active_->id.kind==plan3::JobKind::double_cycle);
    }
    unsigned purchase_investment() {
        if(!active_ || !investment_ || !investment_->shardsRemaining)return 0;
        unsigned bought=0;
        const unsigned bound=investment_->shardsRemaining;
        for(unsigned i=0;i<bound;++i) {
            // Each discrete buy changes cash, equipment and the remaining
            // schedule; no previously spent gold becomes available again.
            if(!recheck_investment() || !investment_ || !investment_->shardsRemaining)break;
            if(!buy_target(investment_->targetShard,1))break;
            ++bought; --investment_->shardsRemaining; last_progress_=cambc::round_number();
            capital_review_due_=true;
            telemetry_.emit("purchase",job_fields(active_->id)+",\"count\":1"+
                ",\"remaining\":"+std::to_string(investment_->shardsRemaining));
        }
        // Successful discrete purchases establish completion directly. Do not
        // add an unmodeled idle callback before settling the actual bag.
        if(investment_ && !investment_->shardsRemaining)capital_review_due_=false;
        return bought;
    }
    bool investment_step(const std::vector<cambc::camp>& camps,const std::vector<cambc::champion>& enemies) {
        if(!active_ || active_->id.kind!=plan3::JobKind::invest)return false;
        if(!investment_) { abort("missing-capital-plan"); return true; }
        auto& me=cambc::self();
        if(capital_prefix_) {
            const int a=altar_index(investment_->cashoutAltar);
            if(a<0) { abort("missing-prefix-altar"); return true; }
            execute_bank(a);
            return true;
        }
        if(capital_review_due_ && !recheck_investment())return true;
        if(investment_->shardsRemaining) {
            const unsigned cost=investment_->targetShard.cost();
            const unsigned count=cost?std::min(investment_->shardsRemaining,me.gold_count()/cost):0;
            const unsigned bought=count?purchase_investment():0;
            if(!active_ || !investment_)return true;
            if(capital_review_due_)return true;
            if(!bought && count && count==investment_->shardsRemaining) {
                // Funding is finished even when this actor must return to a
                // shop. Do not acquire a second camp that the refreshed
                // current-cash schedule no longer includes.
                if(me.gem_count()>0) {
                    const int a=altar_index(active_->id.altarTile);
                    if(a<0) { abort("missing-altar"); return true; }
                    stage_=2; execute_bank(a); return true;
                }
                recovering_=true; recover(camps,enemies); return true;
            }
        }
        if(!investment_->shardsRemaining) {
            // A field purchase can finish the target between funding captures
            // and tribute. Settle the actual carried bag before recovering or
            // declaring the investment endpoint; no extra capture is needed.
            if(me.gem_count()>0) {
                const int a=altar_index(active_->id.altarTile);
                if(a<0) { abort("missing-altar"); return true; }
                stage_=2; execute_bank(a); return true;
            }
            const bool spawn=personal_.own_spawn.is_set(me.position());
            if((!investment_->requiresSpawnRecovery || spawn) && me.health()>=me.max_health()) { complete(false); return true; }
            recovering_=true; recover(camps,enemies); return true;
        }
        if(investment_->fundingGroup<0) {
            if(me.gold_count()>last_gold_)last_progress_=cambc::round_number();
            return true;
        }
        funding_cycle_started_=true;
        return false;
    }
    void execute(const std::vector<cambc::camp>& camps,const std::vector<cambc::champion>& enemies) {
        if (!active_) return;
        auto& me=cambc::self(); const auto id=active_->id;
        if (id.kind==plan3::JobKind::renew) {
            if(personal_.own_spawn.is_set(me.position()) && me.health()>=me.max_health())complete(false);
            else { recovering_=true; recover(camps,enemies); }
            return;
        }
        if(id.kind==plan3::JobKind::observe) {
            const cambc::vec2 destination{id.equipmentBlock%personal_.width,id.equipmentBlock/personal_.width};
            const bool moved=move_one(destination);
            context_.lastObservationTurn=static_cast<int>(telemetry_.turns);
            const int g=group_index(id.firstGroup);
            const bool sampled=g>=0 && !(personal_.groups[std::size_t(g)].spawns&cambc::vision::visible_tiles()).is_empty();
            telemetry_.emit("sample",job_fields(id)+",\"moved\":"+(moved?"true":"false")+
                ",\"lifecycle_visible\":"+(sampled?"true":"false"));
            if(sampled) {
                // The legal group query, not hypothetical vision, is the sample.
                const auto& design=personal_.groups[std::size_t(g)];
                const auto state=design.group.state();
                auto& observed=personal_.observations[std::size_t(g)]; observed={};
                observed.round=cambc::round_number();
                if(const auto* r=std::get_if<cambc::map::camp_group_respawning>(&state)) {
                    observed.lifecycle=Lifecycle::respawning; observed.respawn_remaining=r->timer;
                } else if(std::holds_alternative<cambc::map::camp_group_reward_available>(state))observed.lifecycle=Lifecycle::reward;
                else { observed.lifecycle=Lifecycle::alive; observed.remaining_health=design.full_health; observed.incoming=design.incoming; }
                telemetry_.emit("sample-state",job_fields(id)+",\"observed_lifecycle\":"+
                    std::to_string(static_cast<int>(*observed.lifecycle)));
                // Observe the endpoint at the next owner-turn start, matching
                // the single elapsed turn charged by the frozen EVSI table.
                observation_pending_=true;
            } else abort(moved?"observation-missed-lifecycle":"observation-hidden-obstruction");
            return;
        }
        if(investment_step(camps,enemies))return;
        const int a=altar_index(id.altarTile);
        if (id.kind==plan3::JobKind::bank || stage_>=2) {
            if(!me.gem_count()) { abort("empty-conversion-endpoint"); return; }
            if (a>=0) {
                execute_bank(a);
            } else abort("missing-altar");
            return;
        }
        const int key=stage_==0?id.firstGroup:id.secondGroup, g=group_index(key);
        if (g<0) { abort("missing-group"); return; }
        const auto& d=personal_.groups[g]; const auto* o=personal_.current(g);
        if (o && o->lifecycle==Lifecycle::reward) {
            if(capture_contested(g,enemies)) { abort("capture-contested"); return; }
            if (!d.spawns.is_set(me.position())) {
                if(move_field(paths_,d.field))last_progress_=cambc::round_number();
                if(d.spawns.is_set(me.position())) {
                    const auto current_enemies=cambc::vision::champions(cambc::team::enemy);
                    if(capture_contested(g,current_enemies)) { abort("capture-contested"); return; }
                    remember_hold(g);
                }
            }
            else remember_hold(g);
            stage_lifecycle_=Lifecycle::reward;
            return;
        }
        if(o && o->lifecycle==Lifecycle::respawning) {
            if(stage_lifecycle_ && *stage_lifecycle_!=Lifecycle::respawning) { abort("reward-unavailable"); return; }
            if(last_respawn_timer_>=0 && o->respawn_remaining<last_respawn_timer_)last_progress_=cambc::round_number();
            last_respawn_timer_=o->respawn_remaining; stage_lifecycle_=Lifecycle::respawning;
            if(d.spawns.is_set(me.position())) {
                for(auto step:blocked().reachable_tiles(me.position(),1).positions())
                    if(step!=me.position() && !d.spawns.is_set(step)) { if(move_one(step))last_progress_=cambc::round_number(); break; }
            } else if(move_field(paths_,d.field,&d.spawns))last_progress_=cambc::round_number();
            return;
        }
        if(o)stage_lifecycle_=o->lifecycle;
        if(held_group_==key && d.spawns.is_set(me.position())) {
            // Missing current group observation never proves a finished hold.
            // Keep presence while the priority observation catches up.
            return;
        }
        std::optional<cambc::camp> target; double best=0;
        const auto attack=me.attack_tiles();
        for (const auto& camp:camps) if (d.spawns.is_set(camp.spawn_position()) && camp.is_alive()) {
            const bool in_range=attack.is_set(camp.position());
            const double value=camp.health()+100*chebyshev(me.position(),camp.position())-(in_range?1e9:0);
            if (!target || value<best) { target=camp; best=value; }
        }
        if (target && fight(*target,false,d.incoming>0)) { last_progress_=cambc::round_number(); return; }
        const bool moved=target ? approach_visible(target->position(),me.attack_range()):move_field(paths_,d.field);
        if (moved) {
            last_progress_=cambc::round_number();
            for (const auto& camp:cambc::vision::camps())
                if (d.spawns.is_set(camp.spawn_position()) && camp.is_alive() && me.attack_tiles().is_set(camp.position())) {
                    fight(camp,true,d.incoming>0); break;
                }
        }
    }
public:
    void enable_backpressure_menu_evidence() {
        if(initialization_stage_)throw std::logic_error("Backpressure evidence enabled after initialization");
        backpressure_menu_evidence_=true;telemetry_.reserve_menu_line();
    }
    void set_backpressure_arm(bool rate) {
        if(initialization_stage_)throw std::logic_error("Backpressure arm changed after initialization");
        backpressure_rate_=rate;
    }
    Campaign(plan3::Controller c,bool control,const char* profile):controller_(c),control_(control),profile_(profile) {
        if(c==plan3::Controller::treasury || c==plan3::Controller::council)capital_.enable_design_reuse();
        telemetry_.robust_logging=c==plan3::Controller::robust;
        telemetry_.capital_logging=c==plan3::Controller::treasury || c==plan3::Controller::council;
        telemetry_.reserve_context_line();
    }
    // Query-only setup, paid in real owner callbacks. A legal-action prelude
    // may compile static routes without selecting jobs or publishing live
    // observations. Preparation and turn share one callback/work budget.
    void prepare_static() {
        if(!enter_callback() || cambc::fuel_remaining()<75000)return;
        if(!backpressure_domain_.raw_supported())return;
        if(!initialized_) { initialize_campaign();return; }
        paths_.begin_turn();
        register_fields();
        paths_.advance(65000);
    }
    bool drive_two_trip() {
        if(!two_trip_episode_ || !active_)return false;
        const auto phase=two_trip_episode_->phase;
        const auto camps=cambc::vision::camps();
        const auto enemies=cambc::vision::champions(cambc::team::enemy);
        const auto result=two_trip::step(*two_trip_episode_,*this,personal_,paths_,camps,enemies,
            [] {return cambc::fuel_remaining()>=45000;});
        if(result.status==two_trip::Status::abort)abort(result.reason);
        else {
            if(result.progress)last_progress_=cambc::round_number();
            if(result.captured && stage_<2) {
                stage_=2;++telemetry_.captures;
                telemetry_.emit("capture",job_fields(active_->id)+",\"group\":"+std::to_string(active_->id.firstGroup)+
                    ",\"two_trip\":true,\"bag_gain_lower_bound\":"+std::to_string(cambc::self().gem_count()));
            }
            if(phase!=two_trip_episode_->phase && cambc::fuel_remaining()>=45000)
                telemetry_.emit("two-trip-phase",job_fields(active_->id)+",\"from\":"+std::to_string(static_cast<int>(phase))+
                    ",\"to\":"+std::to_string(static_cast<int>(two_trip_episode_->phase))+
                    ",\"elapsed\":"+std::to_string(cambc::round_number()-start_round_));
            if(result.status==two_trip::Status::cutoff)++telemetry_.cutoffs;
        }
        paths_.advance(45000);finish_observation();return true;
    }
    void turn() {
        if(!enter_callback())return;
        auto& me=cambc::self();
        if(!backpressure_domain_.raw_supported()) {
            if(active_ && !me.is_recalling() && !me.is_submitting_gems() && !me.is_teleporting() &&
               cambc::round_number()-last_progress_>=8)abort("no-progress");
            return;
        }
        if (!initialized_ && !initialize_campaign())return;
        planning_trace_.flush(telemetry_);planning_trace_.begin(personal_.groups.size());
        ++telemetry_.turns; paths_.begin_turn(); begin_execution_turn();
        score_observations();
        if (telemetry_.turns%32==0) telemetry_.summary();
        if (cambc::round_number()+1>=cambc::constants::max_rounds) telemetry_.summary(true);
        capital_.advance_catalogue(4); register_fields();
        two_trip_menu_.advance_preparation(paths_,[] {return cambc::fuel_remaining()>=65000;});
        planning_trace_.mark(PlanningGateTrace::Point::prepared);
        if (!me.is_alive()) {
            abort("death"); if(controller_!=plan3::Controller::treasury)shop();
            recovering_=false; previous_submission_=false; return;
        }
        if(drive_two_trip())return;
        if(observation_pending_ && active_ && active_->id.kind==plan3::JobKind::observe &&
            cambc::round_number()>start_round_)complete(false);
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) {
            finish_observation(); return;
        }
        previous_submission_=false;
        const bool can_act=me.can_move() || me.can_attack() || me.can_cast();
        if(can_act && controller_!=plan3::Controller::treasury &&
            (!active_ || active_->id.kind!=plan3::JobKind::invest))shop();
        planning_trace_.mark(PlanningGateTrace::Point::shop);
        const bool spawn=personal_.own_spawn.is_set(me.position());
        if(spawn && can_act && active_ && active_->id.kind==plan3::JobKind::invest &&
            !capital_prefix_)purchase_investment();
        if (spawn && me.health()<me.max_health()) { paths_.advance(); finish_observation(); return; }
        if (recovering_ && spawn) { recovering_=false; if (active_ && active_->id.kind==plan3::JobKind::renew) complete(false); }
        // Idle preparation gets its own bounded turn before vision and menu
        // work. A candidate-entry reserve can no longer starve every unfinished
        // path field indefinitely. No partially built field is published.
        if (paths_.prepare_idle(!active_ && !recovering_ && spawn,telemetry_.turns,field_cursor_<field_order_.size())) {
            ++telemetry_.preparations;
            finish_observation(); return;
        }
        const auto camps=cambc::vision::camps(); const auto enemies=cambc::vision::champions(cambc::team::enemy);
        planning_trace_.mark(PlanningGateTrace::Point::vision);
        if (cambc::fuel_remaining()<30000) {
            planning_trace_.defer(PlanningGateTrace::Gate::vision_fuel,"below_30000");
            // Fuel deferral cannot extend an unobserved commitment forever.
            // Its endpoint remains unknown if this turn's capture was not read.
            if(active_ && cambc::round_number()-last_progress_>=8)abort("no-progress");
            ++telemetry_.cutoffs; finish_observation(); return;
        }
        // Validate visible range/coordinates before inherited capture, enemy
        // range+1 arithmetic, observation, recovery or bitboard membership.
        plan3_backpressure::ForecastDomain::Report views_report;
        if(!plan3_backpressure::ForecastDomain::views_supported(camps,enemies,&views_report)) {
            planning_trace_.defer(PlanningGateTrace::Gate::views,
                plan3_backpressure::ForecastDomain::reason_name(views_report.reason),views_report.index);
            if(active_ && cambc::round_number()-last_progress_>=8)abort("no-progress");
            ++telemetry_.cutoffs;finish_observation();return;
        }
        std::vector<int> previously_observed_groups;
        const auto now=cambc::round_number();
        if(controller_==plan3::Controller::belief || controller_==plan3::Controller::scout)
            for(std::size_t i=0;i<personal_.observations.size();++i) {
                const auto& o=personal_.observations[i];
                if(o.lifecycle && o.round<now && now-o.round<=12)previously_observed_groups.push_back(personal_.groups[i].key);
            }
        personal_.observe(camps,enemies);
        prioritize_active_observation();
        planning_trace_.mark(PlanningGateTrace::Point::observation);
        if(cambc::fuel_remaining()>=45000)belief_observations(previously_observed_groups);
        capture_transition();
        // Channels returned above. Waiting without an exposed capture countdown
        // cannot refresh its own eight-owner-turn stall clock, even when stunned.
        if(active_ && cambc::round_number()-last_progress_>=8)abort("no-progress");
        if(!can_act) {
            // Presence is observable while stunned; do not invent a witness
            // gap just because this turn has no movement/attack/cast action.
            protect_reward_hold(enemies);
            finish_observation(); return;
        }
        double incoming=0;
        for (const auto& c:camps) if (c.is_alive() && c.is_awake() && chebyshev(me.position(),c.position())<=c.attack_range()) incoming+=c.strength();
        for (const auto& e:enemies) if (chebyshev(me.position(),e.position())<=e.attack_range()+1) incoming+=e.strength();
        const bool emergency=!spawn && fundamentals::should_retreat(me.health(),me.max_health(),incoming);
        if (emergency) { abort("emergency"); recovering_=true; }
        if (recovering_) {
            // A completed funding conversion or gold-only capture is a
            // financial milestone even when recovery is the next safe phase.
            if(active_ && active_->id.kind==plan3::JobKind::invest && capital_review_due_)
                recheck_investment();
            if(active_ && cambc::round_number()-last_progress_>=8)abort("recovery-no-progress");
            recover(camps,enemies);
            if(me.is_recalling())last_progress_=cambc::round_number();
            finish_observation(); return;
        }
        // Only an uncontested capture is protected from ranking reviews.
        if(protect_reward_hold(enemies)) {
            finish_observation(); return;
        }
        const bool group_ready=std::any_of(personal_.groups.begin(),personal_.groups.end(),
            [&](const auto& group){return paths_.complete(group.field);});
        if(!group_ready)paths_.advance();
        if(!group_ready && me.gem_count()==0) {
            planning_trace_.defer(PlanningGateTrace::Gate::paths,"no_complete_group_field");
            finish_observation(); return;
        }
        if (cambc::fuel_remaining()<45000) {
            planning_trace_.defer(PlanningGateTrace::Gate::domain_fuel,"below_45000");
            ++telemetry_.cutoffs; finish_observation(); return;
        }
        // Revalidate all dynamic inputs in THIS callback. A denied scan is
        // unavailable, never resumed next turn with stale actor/gear evidence.
        plan3_backpressure::ForecastDomain::Report domain_report;
        const bool domain_supported=backpressure_domain_.forecast_supported(personal_,camps,enemies,true,&domain_report);
        planning_trace_.mark(PlanningGateTrace::Point::domain);
        if(!domain_supported) {
            planning_trace_.defer(PlanningGateTrace::Gate::domain,
                plan3_backpressure::ForecastDomain::reason_name(domain_report.reason),domain_report.index);
            ++telemetry_.cutoffs;finish_observation();return;
        }
        if(active_)review_active(information());
        if(opening_bootstrap::return_needed(bootstrap_pending_shop_,*this,personal_,bool(active_))) {
            recovering_=true;recover(camps,enemies);finish_observation();return;
        }
        const bool winning_bank=me.gem_count()>0 && me.gem_count()>=
            cambc::constants::winning_gems-cambc::global_submitted_gems(cambc::team::ally);
        // A Cycle/Double already in its conversion stage is heading to bank.
        // This obligation also suppresses redundant Options-control replanning.
        const bool banking=active_ && plan3::executing_bank(active_->id.kind,stage_);
        if (!active_ || (!winning_bank && controller_==plan3::Controller::options && control_) ||
            (winning_bank && !banking)) {
            if(cambc::fuel_remaining()<45000) {
                planning_trace_.defer(PlanningGateTrace::Gate::choose_fuel,"below_45000");
                ++telemetry_.cutoffs; finish_observation(); return;
            }
            choose(information(),false,camps,winning_bank);
        }
        // Planning includes owned-vector destruction before control returns.
        // Recheck the remaining budget before fresh native execution work.
        // The committed job remains private and is executed on a later turn.
        if(cambc::fuel_remaining()<30000) {
            ++telemetry_.cutoffs; finish_observation(); return;
        }
        if(drive_two_trip())return;
        if (active_) execute(camps,enemies);
        paths_.advance(45000);
        finish_observation();
    }
};
inline int run_backpressure(bool rate,const char* profile) {
    Campaign bot(plan3::Controller::ledger,false,profile);
    bot.set_backpressure_arm(rate);
    while(true) {
        try {
            if(cambc::is_draft())fixed_draft();else bot.turn();
        }
        catch(const std::exception& e) { std::fprintf(stdout,"p3 error: %.240s\n",e.what());std::fflush(stdout); }
        catch(...) {std::fputs("p3 error: unknown exception\n",stdout);std::fflush(stdout);}
        cambc::end_turn();
    }
}
} // namespace plan3_runtime
