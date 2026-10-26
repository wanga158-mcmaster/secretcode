#pragma once
#include "executor.h"
#include "forecasts.h"
#include "allocation.h"
#include "capital.h"
#include "horizon-observations.h"
#include "horizon-trace.h"
#include "risk-prefix.h"
#include "policy.h"
#include "conversion-witness.h"
#include "opening-bootstrap.h"
#include "two-trip-menu.h"
#include "sortie-forecast.h"
#include "continuation-journal.h"
#include <array>
#include <stdexcept>
#include <string_view>

#ifndef PLAN3_TEMPO_NOMINAL
#define PLAN3_TEMPO_NOMINAL 0
#endif

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
    bool supplied_market_plan_ = false;
    plan3::JobSelector selector_ = nullptr;
    void* selector_state_ = nullptr;
    bool selector_belief_ = false;
    plan3::PolicyObserver policy_observer_ = nullptr;
    std::uint64_t active_policy_token_ = 0;
    Telemetry telemetry_;
#if PLAN3_CONTINUATION_JOURNAL
    mutable ContinuationJournal continuation_journal_;
#endif
    plan3::Controller controller_;
    bool control_ = false, initialized_ = false, recovering_ = false;
    bool observation_pending_ = false;
    bool bootstrap_pending_shop_ = false;
    two_trip::Menu two_trip_menu_;
    std::unique_ptr<two_trip::Plan> two_trip_episode_;
    sortie::Builder sortie_builder_;
    std::optional<std::size_t> sortie_target_;
    unsigned sortie_idle_opportunities_=0;
    std::vector<unsigned> sortie_retry_after_;
    std::unique_ptr<sortie::Candidate> sortie_candidate_;
    std::optional<plan3::JobForecast> sortie_forecast_;
    std::unique_ptr<sortie::Plan> sortie_episode_;
    int sortie_captured_group_=-1;
    unsigned sortie_capture_round_=0;
    struct SortiePreparationTrace {
        const char* reason="unknown";
        unsigned round=0;
        std::uint64_t before=0,after=0;
        int group=-1,status=0,tau=0,spawnDistance=-1;
        bool candidate=false,forecast=false;
        std::optional<sortie::ModelTrace> model=std::nullopt;
        int ordinaryOffered=-1; // Actual competing Cycle/Double rows, not coverage.
    };
    std::optional<SortiePreparationTrace> sortie_preparation_trace_;
    std::array<const char*,12> sortie_preparation_seen_{};
    unsigned sortie_preparation_seen_count_=0;
    unsigned sortie_exit_logs_=0;
    const char* profile_;
    std::optional<plan3::JobForecast> active_;
    unsigned start_round_ = 0, last_progress_ = 0, last_bag_ = 0;
    double start_health_ = 0;
    cambc::vec2 last_position_{};
    int stage_ = 0;
    std::size_t field_cursor_ = 0;
    std::vector<std::size_t> field_order_;
    std::array<int,3> initial_groups_{{-1,-1,-1}};
    unsigned initial_group_cursor_ = 0;
    std::vector<std::size_t> altar_order_;
    unsigned initialization_stage_ = 0;
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
    std::optional<std::pair<unsigned,CapitalUnknown>> capital_denial_;
    std::uint32_t capital_denials_seen_=0;
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
    bool initialize_campaign() {
        if(initialization_stage_==0) {
            set_telemetry(&telemetry_);
            telemetry_.emit("start",std::string(",\"profile\":\"")+profile_+"\",\"model\":\""+plan3::model_version+"\",\"assumptions\":true");
            Executor::initialize(); initialization_stage_=1; return false;
        }
        if(initialization_stage_==1) {
            if(!personal_.initialize_step())return false;
            paths_.initialize(personal_.terrain);
            if (controller_==plan3::Controller::market && !supplied_market_plan_)
                market_plan_=read_market_plan();
            const auto origin=cambc::self().position();
            for(std::size_t i=0;i<personal_.groups.size();++i)field_order_.push_back(i);
            std::stable_sort(field_order_.begin(),field_order_.end(),[&](auto a,auto b) {
                const auto& x=personal_.groups[a]; const auto& y=personal_.groups[b];
                if((x.incoming==0)!=(y.incoming==0))return x.incoming==0;
                return std::pair{chebyshev(origin,x.anchor),x.key}<std::pair{chebyshev(origin,y.anchor),y.key};
            });
            for(const auto i:field_order_) {
                const auto& group=personal_.groups[i];
                if(group.gem_value>0) {
                    for(unsigned slot=1;slot<initial_groups_.size();++slot)
                        if(initial_groups_[slot]<0){initial_groups_[slot]=int(i);break;}
                } else if(initial_groups_[0]<0 && group.gold_value>0)initial_groups_[0]=int(i);
            }
            personal_.altar_fields.assign(personal_.altars.size(),-1);
            for(std::size_t i=0;i<personal_.altars.size();++i)altar_order_.push_back(i);
            std::stable_sort(altar_order_.begin(),altar_order_.end(),[&](auto a,auto b) {
                const auto x=personal_.altars[a],y=personal_.altars[b];
                return std::pair{chebyshev(origin,x),x.y*personal_.width+x.x}<
                       std::pair{chebyshev(origin,y),y.y*personal_.width+y.x};
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
        // Queue funding and two conversion alternatives before the altar tail.
        // The order is only a public preparation heuristic; routes stay unknown
        // until their complete fields are published by StaticPaths.
        while(initial_group_cursor_<initial_groups_.size()) {
            const int g=initial_groups_[initial_group_cursor_];
            if(g<0){++initial_group_cursor_;continue;}
            auto& group=personal_.groups[std::size_t(g)];
            group.field=paths_.add(group.spawns);
            ++initial_group_cursor_;
            return false;
        }
        if(altar_initialization_cursor_<personal_.altars.size()) {
            const auto a=altar_order_[altar_initialization_cursor_];
            const auto altar=personal_.altars[a];
            personal_.altar_fields[a]=paths_.add(radius(altar,cambc::constants::submit_gems_range));
            ++altar_initialization_cursor_;
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
        while(field_cursor_<field_order_.size() && personal_.groups[field_order_[field_cursor_]].field>=0)++field_cursor_;
        if(field_cursor_<field_order_.size() && cambc::fuel_remaining()>=75000) {
            auto& d=personal_.groups[field_order_[field_cursor_]]; d.field=paths_.add(d.spawns);
            ++field_cursor_;
        }
    }
    bool startup_routes_ready() const {
        if(!paths_.complete(personal_.spawn_field))return false;
        for(const int g:initial_groups_)
            if(g>=0 && !paths_.complete(personal_.groups[std::size_t(g)].field))return false;
        return altar_order_.empty() || paths_.complete(personal_.altar_fields[altar_order_.front()]);
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
#if PLAN3_CONTINUATION_JOURNAL
        continuation_journal_.identity(s.actorOrdinal==static_cast<int>(cambc::champion_type::marcella));
#endif
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
        c.shared_loop_cache=&forecast_loops_;
        // Direct Ledger ranks complete nominal jobs. Supplied policies retain
        // the full response table required by their own decision rules.
#if PLAN3_TEMPO_NOMINAL
        // Tempo consumes the same nominal excursion as Ledger plus the
        // personally supported release/deadline/denial fields. Those fields
        // are prepared before response materialization. Other controllers
        // still require their complete response and terminal-potential data.
        c.nominal_acquisitions_only=!selector_ &&
            (controller_==plan3::Controller::ledger || controller_==plan3::Controller::tempo);
        if(controller_==plan3::Controller::tempo && !selector_)
            c.terminal_renewal_loops=false;
#else
        c.nominal_acquisitions_only=controller_==plan3::Controller::ledger && !selector_;
#endif
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
        const plan3::PolicyEvent event{plan3::PolicyEvent::Kind::aborted,active_policy_token_,
            active_->id,static_cast<int>(start_round_),static_cast<int>(cambc::round_number()),std::nullopt,false};
        ++telemetry_.aborted;
        if(two_trip_episode_)++telemetry_.two_trip_aborted;
        if(sortie_episode_)sortie_builder_.note_episode_aborted();
        telemetry_.emit("abort",job_fields(active_->id)+",\"reason\":\""+reason+"\",\"elapsed\":"+std::to_string(cambc::round_number()-start_round_));
        two_trip_episode_.reset();sortie_episode_.reset();
        active_.reset(); investment_.reset(); capital_review_due_=false; observation_pending_=false; stage_=0; held_group_=-1;
        active_policy_token_=0;
        stage_lifecycle_.reset(); last_respawn_timer_=-1;
        if (unowned_episode_) { unowned_episode_=false; context_.turnsWithoutOwnedJob=0; }
        if(policy_observer_ && event.token)policy_observer_(selector_state_,event);
    }
    void complete(bool conversion, unsigned observed_minimum=0,
                  std::optional<unsigned> exact_conversion=std::nullopt) {
        if (!active_) return;
        const plan3::PolicyEvent event{plan3::PolicyEvent::Kind::completed,active_policy_token_,
            active_->id,static_cast<int>(start_round_),static_cast<int>(cambc::round_number()),exact_conversion,conversion};
        ++telemetry_.completed;
        if(two_trip_episode_) {
            if(conversion)++telemetry_.two_trip_completed;else ++telemetry_.two_trip_aborted;
        }
        if(sortie_episode_) {
            if(conversion)sortie_builder_.note_episode_converted();else sortie_builder_.note_episode_aborted();
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
        if(telemetry_.detail_available() && cambc::fuel_remaining()>=45000)
            telemetry_.emit("endpoint",job_fields(active_->id)+",\"elapsed\":"+std::to_string(elapsed)
            +",\"conversion\":"+(conversion?"true":"false")+",\"actor_score_lower_bound\":"+std::to_string(observed_minimum)
            +",\"exact_conversion\":"+(exact_conversion?std::to_string(*exact_conversion):"null")+
            ",\"start_health\":"+telemetry_number(start_health_)+
            ",\"end_health\":"+telemetry_number(cambc::self().health())+
            ",\"predicted_end_health\":"+telemetry_number(predicted?predicted->terminal.health:0));
        else ++telemetry_.details_omitted;
        two_trip_episode_.reset();sortie_episode_.reset();
        active_.reset(); investment_.reset(); capital_review_due_=false; observation_pending_=false; stage_=0; held_group_=-1;
        active_policy_token_=0;
        stage_lifecycle_.reset(); last_respawn_timer_=-1;
        if (unowned_episode_) { unowned_episode_=false; context_.turnsWithoutOwnedJob=0; }
        if(policy_observer_ && event.token)policy_observer_(selector_state_,event);
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
        if (controller_!=plan3::Controller::belief && controller_!=plan3::Controller::scout && !selector_belief_) return;
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
                            controller_==plan3::Controller::belief && control_ && !selector_belief_);
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
    bool leave_captured_gold(bool& captured_tile) {
        captured_tile=false;
        auto& me=cambc::self();const auto position=me.position();
        const unsigned round=cambc::round_number();
        if(!me.can_move() || me.gem_count() || cambc::fuel_remaining()<45000)return false;
        for(const auto& group:personal_.groups) {
            if(group.gem_value || group.incoming>0 ||
               std::find(group.spawn_positions.begin(),group.spawn_positions.end(),position)==group.spawn_positions.end())continue;
            if(!std::holds_alternative<cambc::map::camp_group_respawning>(group.group.state()))return false;
            captured_tile=true;
            std::array<std::pair<int,int>,8> exits{};unsigned count=0;
            const int width=personal_.width,height=cambc::map::height();
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                const cambc::vec2 next{position.x+dx,position.y+dy};
                if((!dx&&!dy) || next.x<0 || next.y<0 || next.x>=width || next.y>=height || personal_.terrain.is_set(next))continue;
                bool camp=false;
                for(const auto& other:personal_.groups)
                    camp|=std::find(other.spawn_positions.begin(),other.spawn_positions.end(),next)!=other.spawn_positions.end();
                if(camp)continue;
                int d=paths_.get(personal_.spawn_field,next);
                if(d<0) {d=width+height;for(auto spawn:personal_.spawn_positions)d=std::min(d,chebyshev(next,spawn));}
                exits[count++]={d,next.y*width+next.x};
            }
            std::sort(exits.begin(),exits.begin()+count);
            for(unsigned i=0;i<count && cambc::fuel_remaining()>=35000;++i) {
                if(cambc::round_number()!=round)return true;
                const cambc::vec2 next{exits[i].second%width,exits[i].second/width};
                if(!move_one(next)) {
                    if(last_move_status()==MoveStatus::hidden_collision)return true;
                    continue;
                }
                if(sortie_exit_logs_<3 && telemetry_.detail_available() && cambc::fuel_remaining()>=45000) {
                    telemetry_.emit("captured-gold-exit",",\"group\":"+std::to_string(group.key)+
                        ",\"from\":["+std::to_string(position.x)+','+std::to_string(position.y)+"],\"to\":["+
                        std::to_string(next.x)+','+std::to_string(next.y)+']');
                    ++sortie_exit_logs_;
                }
                return true;
            }
            return false; // Preserve ordinary choices if no safe exit is available.
        }
        return false;
    }
    void prepare_sortie(const ForecastContext& context,const EquipmentStats& stats,bool allowAdvance) {
        // Most idle callbacks retain the ordinary planning budget. Completed
        // private math may be freshly published without advancing it again.
        if(!allowAdvance && !sortie_builder_.has_complete_work())return;
        const auto round=cambc::round_number();
        if(sortie_retry_after_.size()!=personal_.groups.size())sortie_retry_after_.assign(personal_.groups.size(),0);
        if(!sortie_target_) {
            int distance=0;const auto position=cambc::self().position();
            for(std::size_t i=0;i<personal_.groups.size();++i) {
                const auto& group=personal_.groups[i];
                if(!group.gem_value || round<sortie_retry_after_[i] || personal_.remembered_respawn_minimum(i)>0 ||
                   (group.key==sortie_captured_group_ &&
                    round-sortie_capture_round_<unsigned(group.max_respawn_timer)))continue;
                const int d=chebyshev(position,group.anchor);
                if(!sortie_target_ || std::pair{d,group.key}<std::pair{distance,personal_.groups[*sortie_target_].key}) {
                    sortie_target_=i;distance=d;
                }
            }
        }
        if(!sortie_target_)return;
        // Keep the established faster producer for already admitted single
        // clears. Unsupported public geometry rotates through other targets;
        // a short retry window permits later health/gear improvements.
        if(context.clear(*sortie_target_,context.info.health).admitted) {
            sortie_retry_after_[*sortie_target_]=round+32;sortie_target_.reset();return;
        }
        const auto ready=[] {return cambc::fuel_remaining()>=45000;};
        const auto before=cambc::fuel_remaining();
        auto result=sortie_builder_.prepare(personal_,*sortie_target_,stats,paths_,ready,allowAdvance);
        SortiePreparationTrace trace{result.reason,round,before,0,personal_.groups[*sortie_target_].key,
            static_cast<int>(result.status),result.candidate?result.candidate->accounting.tau:0,
            paths_.get(personal_.spawn_field,cambc::self().position()),bool(result.candidate),false};
        trace.model=result.model;
        if(result.status==sortie::Status::ready && result.candidate) {
            auto job=sortie::forecast(*result.candidate,context,ready);
            trace.forecast=bool(job);
            if(job) {
                sortie_builder_.note_forecast();
                sortie_forecast_=std::move(job);sortie_candidate_=std::move(result.candidate);
            }
        } else if(result.status==sortie::Status::unsupported) {
            sortie_retry_after_[*sortie_target_]=round+32;sortie_target_.reset();
        }
        trace.after=cambc::fuel_remaining();
        const std::string_view phase=trace.reason;
        const bool routine=phase=="public walls" || phase=="public members" || phase=="fuel" || phase=="inventory";
        if(!routine && !sortie_preparation_trace_ && sortie_preparation_seen_count_<sortie_preparation_seen_.size()) {
            bool seen=false;
            for(unsigned i=0;i<sortie_preparation_seen_count_;++i)
                seen|=std::string_view(sortie_preparation_seen_[i])==trace.reason;
            if(!seen) {
                sortie_preparation_seen_[sortie_preparation_seen_count_++]=trace.reason;
                sortie_preparation_trace_=trace;
            }
        }
    }
    std::vector<plan3::JobForecast> menu(const ForecastContext& c, bool emergency,
                                      const std::vector<cambc::camp>& camps,const two_trip::Menu::Prefix& prefix,
                                      bool audit=false,bool winning_only=false,
                                      bool defer_capital=false,MenuAudit* audit_data=nullptr,plan3::EvaluationCache* scout_evaluation=nullptr,ScoutCheckpoint* scout_checkpoint=nullptr) {
        std::vector<plan3::JobForecast> jobs;
        jobs.reserve(plan3::single_job_cap+plan3::double_job_cap+plan3::nonharvest_job_cap);
        if(sortie_forecast_)jobs.push_back(*sortie_forecast_);
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
            if(sortie_forecast_ && personal_.groups[pair.second].key==sortie_forecast_->id.firstGroup)continue;
            if(static_cast<unsigned>(std::count_if(jobs.begin(),jobs.end(),[](const auto& job){return job.id.kind==plan3::JobKind::cycle;}))>=plan3::single_job_cap)break;
            if (cambc::fuel_remaining()<60000u) { ++telemetry_.cutoffs; break; }
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
            if (doubles>=8 || cambc::fuel_remaining()<60000u) break;
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
    void choose(const plan3::InformationState& state, bool emergency,const std::vector<cambc::camp>& camps,bool winning_only=false) {
        sortie_candidate_.reset();sortie_forecast_.reset();
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
        const bool sortie_scope=!selector_ && (controller_==plan3::Controller::ledger || controller_==plan3::Controller::atlas) &&
            !active_ && !winning_only && !emergency &&
            state.carriedGems==0 && state.actorOrdinal==static_cast<int>(cambc::champion_type::tavi);
        // A captured camp tile stays blocked in future route models. Leave its
        // observed dead gold group before preparing a fresh decision next turn.
        bool captured_tile=false;
        if(sortie_scope && leave_captured_gold(captured_tile))return;
        const bool extend=false; // The complete bounded sortie schedule replaces Ledger's two-trip sidecar.
        std::optional<EquipmentStats> exact;
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(12);
#endif
        auto c=forecast_context(state,(extend || sortie_scope)?&exact:nullptr);
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(13);
#endif
        // Count opportunities after public preparation and idle eligibility.
        // Long ordinary jobs must not repeatedly skip a global callback phase.
        if(sortie_scope && !captured_tile && exact)
            prepare_sortie(c,*exact,(++sortie_idle_opportunities_%4u)==2u);
        const auto prefix=winning_only ? two_trip::Menu::Prefix{}:
            two_trip::Menu::single_prefix(personal_,paths_,cambc::self().position());
        // This interval performs queries only. A round change means execution
        // suspended; Menu then rejects the early live candidate before use.
        if(extend)two_trip_menu_.prepare_early(personal_,exact,paths_,state,prefix,
            [] {return cambc::fuel_remaining()>=65000;});
        if(cambc::round_number()!=static_cast<unsigned>(state.round)) {++telemetry_.cutoffs;return;}
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(14);
#endif
        auto ordinary=menu(c,emergency,camps,prefix,audit_compute,winning_only,capital_mode,&menu_audit,&evaluation,&scout_checkpoint);
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(15);
#endif
        if(cambc::round_number()!=static_cast<unsigned>(state.round)) {++telemetry_.cutoffs;return;}
        // Council's baseline contains only the available ordinary proposers.
        // Missing shopping is an unknown opportunity, never a complete four-proposer claim.
        auto jobs=controller_==plan3::Controller::council ? plan3::council_proposals(state,ordinary):std::move(ordinary);
        if(sortie_forecast_ && sortie_preparation_trace_ &&
           sortie_preparation_trace_->round==unsigned(state.round) && sortie_preparation_trace_->forecast) {
            auto& count=sortie_preparation_trace_->ordinaryOffered;count=0;
            for(const auto& job:jobs)
                if(job.id!=sortie_forecast_->id &&
                   (job.id.kind==plan3::JobKind::cycle || job.id.kind==plan3::JobKind::double_cycle))++count;
        }
        if(extend) {
            const auto prepared=two_trip_menu_.merge(state,jobs);
            if(two_trip::Menu::defer_selection(prepared,state,jobs)) {
                if(prepared==two_trip::Status::pending)++telemetry_.preparations;else ++telemetry_.cutoffs;
                return; // Unfinished geometry never supplies an executable job.
            }
        }
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
        const unsigned selection_reserve=sortie_forecast_ && !selector_ && sortie_scope?40000:45000;
        if(!scout_checkpoint.ready && ((controller_==plan3::Controller::scout && scout_checkpoint.unknown) ||
            cambc::fuel_remaining()<selection_reserve)) {
#if PLAN3_QUIET_BOUNDARY_PACKET
            quiet_boundary_.stamp(17);quiet_boundary_.reason(10);
#endif
            ++telemetry_.cutoffs;
            if(audit_compute)telemetry_.emit("compute",",\"phase\":\"selection_deferred\",\"fuel\":"+std::to_string(cambc::fuel_remaining()));
            return;
        }
        plan3::JobIntent intent,companion;
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(16);
#endif
        bool companion_available=true;
        std::optional<plan3::RiskPrefixSelection> risk;
        TelemetryFields risk_fields;
        if(selector_ && !capital_mode) {
            intent=plan3::select_policy(selector_,selector_state_,control_,state,jobs,context_,controller_);
            if(cambc::fuel_remaining()<45000 || cambc::round_number()!=static_cast<unsigned>(state.round)) {
                ++telemetry_.cutoffs;return;
            }
            companion=plan3::select_policy(selector_,selector_state_,!control_,state,jobs,context_,controller_);
            if(cambc::fuel_remaining()<45000 || cambc::round_number()!=static_cast<unsigned>(state.round)) {
                ++telemetry_.cutoffs;return;
            }
        } else if(scout_checkpoint.ready) {
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
            const auto first=plan3::select(controller_,capital_mode?false:control_,state,jobs,context_,&evaluation);
            if(capital_mode && cambc::fuel_remaining()<45000) {++telemetry_.cutoffs;return;}
            const auto second=plan3::select(controller_,capital_mode?true:!control_,state,jobs,context_,&evaluation);
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
                auto proposal=TreasuryPlanner::select_proposal(built,selector_?false:control_,controller_==plan3::Controller::council);
                if(selector_) {
                    // Both external arms receive the same complete capital
                    // offer. Their ordinary choices remain executable if the
                    // optional capital comparison cannot finish this turn.
                    auto full=std::move(jobs);
                    if(proposal)full.push_back(proposal->investmentJob);
                    const auto active=plan3::select_policy(selector_,selector_state_,false,state,full,context_,controller_);
                    if(cambc::fuel_remaining()<45000 || cambc::round_number()!=static_cast<unsigned>(state.round))capital_unknown=true;
                    if(!capital_unknown) {
                        const auto control=plan3::select_policy(selector_,selector_state_,true,state,full,context_,controller_);
                        if(cambc::fuel_remaining()<45000 || cambc::round_number()!=static_cast<unsigned>(state.round))capital_unknown=true;
                        else {
                            intent=control_?control:active;companion=control_?active:control;
                            capital_candidate_=std::move(proposal);jobs=std::move(full);evaluation={};
                        }
                    }
                    if(capital_unknown) {
                        if(proposal)full.pop_back();
                        jobs=std::move(full);evaluation={};
                    }
                } else
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
            if(capital_unknown && capital_trace.code!=CapitalUnknown::none && !capital_denial_) {
                const auto bit=std::uint32_t{1}<<static_cast<unsigned>(capital_trace.code);
                if(!(capital_denials_seen_&bit)) {
                    capital_denial_=std::pair{cambc::round_number(),capital_trace.code};
                    capital_denials_seen_|=bit;
                }
            }
            if(capital_unknown && selector_)companion_available=false;
            // Missing investment opportunities are unknown, not a reason to
            // suppress complete ordinary jobs or replace a design's ranker.
            if(capital_unknown && selector_ && !built.complete && cambc::fuel_remaining()>=45000) {
                const auto ordinary_intent=plan3::select_policy(selector_,selector_state_,control_,state,jobs,context_,controller_);
                if(cambc::round_number()!=static_cast<unsigned>(state.round)) {++telemetry_.cutoffs;return;}
                if(ordinary_intent.status!=plan3::IntentStatus::yield || cambc::fuel_remaining()>=45000) {
                    intent=ordinary_intent;companion=ordinary_intent;
                }
                companion_available=false;
            }
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
        const bool bootstrap_y_based=controller_!=plan3::Controller::ledger && controller_!=plan3::Controller::tempo &&
            controller_!=plan3::Controller::market && controller_!=plan3::Controller::horizon;
        const bool bootstrap_selection=opening_bootstrap::unfinished(*this) &&
            opening_bootstrap::prefer_single(state,jobs,intent,companion,bootstrap_y_based);
        horizon_context(state,intent,companion,jobs);
        if(!candidate_audit_ && !c.cutoff && !capital_unknown && (!risk || (!risk->cutoff && !risk->priorityFast)) && cambc::fuel_remaining()>=65000 &&
            std::any_of(jobs.begin(),jobs.end(),[&](const auto& j){return plan3::admitted(state,j,false);})) {
            std::vector<const plan3::JobForecast*> canonical;
            for(const auto& j:jobs)if(j.complete && j.nominal() && (!risk ||
                std::find(risk->prefix.begin(),risk->prefix.end(),&j)!=risk->prefix.end()))canonical.push_back(&j);
            std::sort(canonical.begin(),canonical.end(),[](auto a,auto b){return a->id<b->id;});
            const auto ids_logged=std::min<std::size_t>(16,canonical.size());
            for(std::size_t begin=0;begin<ids_logged;begin+=8) {
                TelemetryFields ids; ids<<",\"ids\":[";
                for(std::size_t at=begin;at<std::min(begin+8,ids_logged);++at) {
                    if(at!=begin)ids<<',';
                    const auto& id=canonical[at]->id;
                    ids<<'['<<static_cast<int>(id.kind)<<','<<id.firstGroup<<','<<id.secondGroup<<','<<id.altarTile<<','<<id.equipmentBlock<<']';
                }
                ids<<"],\"menu_size\":"<<canonical.size()<<",\"ids_omitted\":"<<canonical.size()-ids_logged
                    <<",\"forecast_rows_cap\":4,\"forecast_rows_omitted\":null"
                    <<",\"candidate_prefix\":true";
                telemetry_.emit("candidate-ids",ids.str());
            }
            for(std::size_t at=0;at<std::min<std::size_t>(4,canonical.size());++at) {
                if(cambc::fuel_remaining()<45000)break;
                const auto& j=*canonical[at];
                if(!j.complete)continue;
                const auto* n=j.nominal(); if(!n)continue;
                TelemetryFields fields;
                fields<<job_fields(j.id)<<",\"tau\":"<<n->tau<<",\"deltaS\":"<<n->deltaS
                    <<",\"loss\":"<<n->healthLoss<<",\"gold_in\":"<<n->goldEarned<<",\"gold_out\":"<<n->goldSpent
                    <<",\"terminal\":"<<n->terminal.position.tile<<",\"world_Y\":[";
                unsigned values_omitted=0;
                for(std::size_t w=0;w<j.outcomes.size();++w) {
                    if(w)fields<<',';
                    const bool compute=cambc::fuel_remaining()>=45000;
                    const double value=compute ? plan3::cached_outcome_value(state,j,w,&evaluation):
                        std::numeric_limits<double>::quiet_NaN();
                    if(!compute)++values_omitted;
                    if(std::isfinite(value))fields<<value; else fields<<"null";
                }
                fields<<"],\"world_values_omitted\":"<<values_omitted<<",\"assumed_forecast\":true,\"candidate_prefix\":true";
                telemetry_.emit("candidate",fields.str());
            }
            candidate_audit_=true;
        }
        unsigned alternatives=0;
        for(const auto& j:jobs)if(j.id.kind!=plan3::JobKind::renew && plan3::admitted(state,j,false) &&
            (!risk || risk->priorityFast || std::find(risk->prefix.begin(),risk->prefix.end(),&j)!=risk->prefix.end()))++alternatives;
        if(alternatives>=2)++telemetry_.opportunities;
        const bool disagreement=companion_available && intent.job!=companion.job;
        if(disagreement)++telemetry_.disagreements;
        ++telemetry_.decisions;
        const bool robust_degeneracy=controller_==plan3::Controller::robust && !control_ &&
            intent.reason==plan3::DecisionReason::robust_degeneracy;
        if(robust_degeneracy)++telemetry_.robust_degeneracies;
        if (intent.status!=plan3::IntentStatus::selected || !intent.job) return;
        for (auto& j:jobs) if (j.id==*intent.job) {
            if(cambc::round_number()!=static_cast<unsigned>(state.round)) {++telemetry_.cutoffs;return;}
            if (active_ && active_->id==j.id) return;
            const bool owns_sortie=sortie_forecast_ && j.id==sortie_forecast_->id;
            if(owns_sortie && (!sortie_candidate_ || sortie_candidate_->plan.preparedRound!=unsigned(state.round))) {
                ++telemetry_.cutoffs;return;
            }
            if(owns_sortie && cambc::fuel_remaining()<40000) {++telemetry_.cutoffs;return;}
            auto sortie_episode=owns_sortie?std::make_unique<sortie::Plan>(std::move(sortie_candidate_->plan)):nullptr;
            const bool owns_two_trip=extend && two_trip_menu_.owns(j.id);
            auto episode=owns_two_trip?two_trip_menu_.take(j.id):nullptr;
            if(owns_two_trip && !episode) {++telemetry_.cutoffs;return;}
            if (active_) abort("reselection");
            ++telemetry_.activations;
            bootstrap_pending_shop_=bootstrap_selection;
            two_trip_episode_=std::move(episode);
            sortie_episode_=std::move(sortie_episode);
            if(sortie_episode_) {sortie_builder_.note_episode_started();sortie_target_.reset();}
            if(two_trip_episode_)++telemetry_.two_trip_started;
            active_=std::move(j); stage_=0; start_round_=state.round; last_progress_=state.round;
#if PLAN3_QUIET_BOUNDARY_PACKET
            quiet_boundary_.stamp(18);quiet_boundary_.state(active_->id.firstGroup,stage_);
#endif
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
            active_policy_token_=intent.policyToken;
            if(policy_observer_ && active_policy_token_)
                policy_observer_(selector_state_,{plan3::PolicyEvent::Kind::committed,
                    active_policy_token_,active_->id,state.round,state.round,std::nullopt,false});
            if(sortie_episode_) {
                if(telemetry_.detail_available() && cambc::fuel_remaining()>=65000)
                    telemetry_.emit("sortie-start",job_fields(chosen.id)+",\"sorties\":"+std::to_string(sortie_episode_->accounting.sortieCount)+
                        ",\"tau\":"+std::to_string(sortie_episode_->accounting.tau)+",\"minimum_health\":"+
                        telemetry_number(sortie_episode_->accounting.minHealth)+",\"tavi_abilities\":"+
                        (sortie_episode_->input.tavi.enabled?"true":"false")+
                        ",\"combat_turns\":"+std::to_string(sortie_episode_->accounting.turnCount)+
                        ",\"bursts\":"+std::to_string(sortie_episode_->accounting.burstCount)+
                        ",\"resets\":"+std::to_string(sortie_episode_->accounting.resetCount)+
                        ",\"stun_damage_avoidance_credited\":false,\"public_full_health_bound\":true,\"quiet_assumption\":true");
                return; // Preserve the reserve for the same-round live-plan start.
            }
            // The job is committed. Optional formatting must leave fuel for
            // forecast cleanup and the turn's observation/yield path.
            if(!telemetry_.detail_available() || cambc::fuel_remaining()<45000) { ++telemetry_.details_omitted; return; }
            const auto* n=active_->nominal();
            TelemetryFields comparison;
            comparison<<",\"companion_job\":";
            if(companion_available && companion.job) {
                const auto& id=*companion.job;
                comparison<<'['<<static_cast<int>(id.kind)<<','<<id.firstGroup<<','<<id.secondGroup<<','<<id.altarTile<<','<<id.equipmentBlock<<']';
            } else comparison<<"null";
            comparison<<",\"changed\":"<<(companion_available?(disagreement?"true":"false"):"null")<<",\"companion_objective\":";
            if(companion_available && std::isfinite(companion.objective))comparison<<companion.objective; else comparison<<"null";
            comparison<<",\"counterfactual_only\":true,\"companion_available\":"<<(companion_available?"true":"false");
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
                    capital_fragment->fragment():",\"provenance\":\"analytical-assumed\"")+comparison.str());
            return;
        }
    }
    void prioritize_active_observation(const std::vector<cambc::camp>& camps) {
        if(!active_ || stage_>=2 || cambc::fuel_remaining()<30000)return;
        const int g=group_index(stage_==0?active_->id.firstGroup:active_->id.secondGroup);
        if(g<0 || personal_.current(std::size_t(g)))return;
        personal_.observe_group(std::size_t(g),camps,cambc::vision::visible_tiles(),30000);
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
        if(id.kind==plan3::JobKind::double_cycle && stage_==1 && !selector_ && controller_!=plan3::Controller::options)
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
#if PLAN3_CONTINUATION_JOURNAL
        continuation_journal_.execute();
#endif
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(20);
#endif
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
#if PLAN3_CONTINUATION_JOURNAL
        continuation_journal_.observation(personal_.observation_round,o?o->round:0,o && o->lifecycle?static_cast<int>(*o->lifecycle):-1);
#endif
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
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(21);
#endif
        const auto attack=me.attack_tiles();
        for (const auto& camp:camps) if (d.spawns.is_set(camp.spawn_position()) && camp.is_alive()) {
            const bool in_range=attack.is_set(camp.position());
            const double value=camp.health()+100*chebyshev(me.position(),camp.position())-(in_range?1e9:0);
            if (!target || value<best) { target=camp; best=value; }
        }
#if PLAN3_CONTINUATION_JOURNAL
        continuation_journal_.target(bool(target),target?static_cast<unsigned>(static_cast<cambc::handle_camp_t>(*target)):~0u);
#endif
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(22);quiet_boundary_.stamp(23);
#endif
        if (target && fight(*target,false,d.incoming>0)) { last_progress_=cambc::round_number(); return; }
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(24);
#endif
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
    void report_policy(std::string_view fields) {telemetry_.emit("policy",fields);}
    void supply_market_plan(MarketPlan plan) {
        if(initialization_stage_!=0)throw std::logic_error("allocation supplied after initialization");
        market_plan_=std::move(plan);supplied_market_plan_=true;
    }
    void supply_selector(plan3::JobSelector selector,void* state=nullptr,bool observe_belief=false,
                         plan3::PolicyObserver observer=nullptr) {
        if(initialization_stage_!=0)throw std::logic_error("selector supplied after initialization");
        selector_=selector;selector_state_=state;selector_belief_=observe_belief;
        policy_observer_=observer;
    }
    Campaign(plan3::Controller c,bool control,const char* profile):controller_(c),control_(control),profile_(profile) {
        if(c==plan3::Controller::treasury || c==plan3::Controller::council)capital_.enable_design_reuse();
        telemetry_.robust_logging=c==plan3::Controller::robust;
        telemetry_.capital_logging=c==plan3::Controller::treasury || c==plan3::Controller::council;
        if(c==plan3::Controller::horizon)telemetry_.reserve_context_line();
    }
    bool drive_sortie() {
        if(!sortie_episode_ || !active_)return false;
        const auto phase=sortie_episode_->phase;
        const auto camps=cambc::vision::camps();
        const auto enemies=cambc::vision::champions(cambc::team::enemy);
        const bool first_start=!sortie_episode_->started && sortie_episode_->preparedRound==cambc::round_number();
        const auto result=sortie::step(*sortie_episode_,*this,personal_,paths_,camps,enemies,
            [first_start] {return cambc::fuel_remaining()>=(first_start?30000u:45000u);});
        if(result.status==sortie::Status::abort)abort(result.reason);
        else {
            if(result.progress)last_progress_=cambc::round_number();
            if(result.captured && stage_<2) {
                stage_=2;++telemetry_.captures;
                sortie_captured_group_=active_->id.firstGroup;sortie_capture_round_=cambc::round_number();
                if(telemetry_.detail_available() && cambc::fuel_remaining()>=45000)
                    telemetry_.emit("capture",job_fields(active_->id)+",\"group\":"+std::to_string(active_->id.firstGroup)+
                        ",\"multi_sortie\":true,\"bag_gain_lower_bound\":"+std::to_string(cambc::self().gem_count()));
            }
            if(phase!=sortie_episode_->phase && telemetry_.detail_available() && cambc::fuel_remaining()>=45000)
                telemetry_.emit("sortie-phase",job_fields(active_->id)+",\"from\":"+std::to_string(static_cast<int>(phase))+
                    ",\"to\":"+std::to_string(static_cast<int>(sortie_episode_->phase))+
                    ",\"elapsed\":"+std::to_string(cambc::round_number()-start_round_));
            if(result.status==sortie::Status::cutoff)++telemetry_.cutoffs;
        }
        paths_.advance(45000);finish_observation();return true;
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
private:
    void turn_body() {
        auto& me=cambc::self();
        if (!initialized_ && !initialize_campaign())return;
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.begin(cambc::round_number(),me.champion_name());
        quiet_boundary_.state(active_?active_->id.firstGroup:-1,stage_);
#endif
        ++telemetry_.turns; paths_.begin_turn(); begin_execution_turn();
        score_observations();
        capital_.advance_catalogue(4); register_fields();
        if(sortie_target_)sortie_builder_.advance_preparation(paths_,[] {return cambc::fuel_remaining()>=65000;});
        else two_trip_menu_.advance_preparation(paths_,[] {return cambc::fuel_remaining()>=65000;});
        if (!me.is_alive()) {
            abort("death"); if(controller_!=plan3::Controller::treasury)shop();
            recovering_=false; previous_submission_=false; return;
        }
        if(drive_sortie() || drive_two_trip())return;
        if(observation_pending_ && active_ && active_->id.kind==plan3::JobKind::observe &&
            cambc::round_number()>start_round_)complete(false);
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) {
            finish_observation(); return;
        }
        previous_submission_=false;
        const bool can_act=me.can_move() || me.can_attack() || me.can_cast();
        if(can_act && controller_!=plan3::Controller::treasury &&
            (!active_ || active_->id.kind!=plan3::JobKind::invest))shop();
        const bool spawn=personal_.own_spawn.is_set(me.position());
        if(spawn && can_act && active_ && active_->id.kind==plan3::JobKind::invest &&
            !capital_prefix_)purchase_investment();
        if (spawn && me.health()<me.max_health()) { paths_.advance(); finish_observation(); return; }
        if (recovering_ && spawn) { recovering_=false; if (active_ && active_->id.kind==plan3::JobKind::renew) complete(false); }
        // Idle preparation gets its own bounded turn before vision and menu
        // work. A candidate-entry reserve can no longer starve every unfinished
        // path field indefinitely. No partially built field is published.
        const bool idle_start=!active_ && !recovering_ && spawn;
        const bool urgent_start=idle_start && !startup_routes_ready() && me.gem_count()==0;
        if (paths_.prepare_idle(idle_start,telemetry_.turns,field_cursor_<field_order_.size(),urgent_start)) {
            ++telemetry_.preparations;
            finish_observation(); return;
        }
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(1);
#endif
        const auto camps=cambc::vision::camps(); const auto enemies=cambc::vision::champions(cambc::team::enemy);
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(2);
#endif
        if (cambc::fuel_remaining()<30000) {
            // Fuel deferral cannot extend an unobserved commitment forever.
            // Its endpoint remains unknown if this turn's capture was not read.
            if(active_ && cambc::round_number()-last_progress_>=8)abort("no-progress");
            ++telemetry_.cutoffs; finish_observation(); return;
        }
        std::vector<int> previously_observed_groups;
        const auto now=cambc::round_number();
#if PLAN3_CONTINUATION_JOURNAL
        continuation_journal_.begin(now,active_?static_cast<int>(active_->id.kind):-1,
            active_?active_->id.firstGroup:-1,active_?active_->id.secondGroup:-1,
            active_?active_->id.altarTile:-1,active_?active_->id.equipmentBlock:-1,stage_,personal_.observation_round);
#endif
        if(controller_==plan3::Controller::belief || controller_==plan3::Controller::scout || selector_belief_)
            for(std::size_t i=0;i<personal_.observations.size();++i) {
                const auto& o=personal_.observations[i];
                if(o.lifecycle && o.round<now && now-o.round<=12)previously_observed_groups.push_back(personal_.groups[i].key);
            }
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(3);
#endif
        personal_.observe(camps,enemies);
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(4);quiet_boundary_.stamp(5);
#endif
        prioritize_active_observation(camps);
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(6);
#endif
        // A later query can resume in a new round after an earlier group was
        // published. The callback may consume only one observation epoch.
        const auto observation_current=[&] {
            return personal_.observation_round==now && cambc::round_number()==now;
        };
        if(!observation_current()) {++telemetry_.cutoffs;finish_observation();return;}
        if(cambc::fuel_remaining()>=45000)belief_observations(previously_observed_groups);
        if(!observation_current()) {++telemetry_.cutoffs;finish_observation();return;}
        capture_transition();
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(7);
#endif
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
        if(!group_ready && me.gem_count()==0) { finish_observation(); return; }
        // Most committed turns need only the executor. Check the cheap review
        // clock before constructing the full economic observation, and keep
        // the execution reserve separate from the planning reserve.
        if(active_ && (now-last_review_>=8 || capital_review_due_) &&
           cambc::fuel_remaining()>=45000) {
#if PLAN3_QUIET_BOUNDARY_PACKET
            quiet_boundary_.stamp(8);
#endif
            review_active(information());
#if PLAN3_QUIET_BOUNDARY_PACKET
            quiet_boundary_.stamp(9);
#endif
        }
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
            if(cambc::fuel_remaining()<45000) { ++telemetry_.cutoffs; finish_observation(); return; }
#if PLAN3_QUIET_BOUNDARY_PACKET
            quiet_boundary_.stamp(10);
#endif
            choose(information(),false,camps,winning_bank);
#if PLAN3_QUIET_BOUNDARY_PACKET
            quiet_boundary_.stamp(11);
#endif
        }
        // Planning includes owned-vector destruction before control returns.
        // Recheck the remaining budget before fresh native execution work.
        // The committed job remains private and is executed on a later turn.
#if PLAN3_CONTINUATION_JOURNAL
        const auto continuation_fuel=cambc::fuel_remaining();
        continuation_journal_.boundary(static_cast<unsigned>(continuation_fuel),active_?static_cast<int>(active_->id.kind):-1,
            active_?active_->id.firstGroup:-1,active_?active_->id.secondGroup:-1,
            active_?active_->id.altarTile:-1,active_?active_->id.equipmentBlock:-1,stage_,personal_.observation_round);
        if(continuation_fuel<30000) {
#else
        if(cambc::fuel_remaining()<30000) {
#endif
#if PLAN3_QUIET_BOUNDARY_PACKET
            quiet_boundary_.reason(9);
#endif
            ++telemetry_.cutoffs; finish_observation(); return;
        }
        if(drive_sortie() || drive_two_trip())return;
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(19);
#endif
        if (active_) execute(camps,enemies);
#if PLAN3_CONTINUATION_JOURNAL
        continuation_journal_.move(static_cast<int>(last_move_status()));
#endif
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(27);quiet_boundary_.move(static_cast<int>(last_move_status()));
#endif
        paths_.advance(45000);
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(28);
#endif
        finish_observation();
    }
public:
    void turn() {
        const auto before=telemetry_.turns;
        turn_body();
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(29);
#endif
        if(sortie_preparation_trace_ && telemetry_.detail_available() && cambc::fuel_remaining()>=30000) {
            const auto trace=*sortie_preparation_trace_;
            TelemetryFields fields;
            fields<<",\"observed_round\":"<<trace.round<<",\"group\":"<<trace.group
                <<",\"status\":"<<trace.status<<",\"phase\":\""<<trace.reason<<"\",\"before\":"<<trace.before
                <<",\"after\":"<<trace.after<<",\"candidate\":"<<trace.candidate<<",\"forecast\":"<<trace.forecast
                <<",\"tau\":"<<trace.tau<<",\"spawn_distance\":"<<trace.spawnDistance
                <<",\"ordinary_offered\":"<<trace.ordinaryOffered;
            // Model work counters accumulate across private chunks. Keep one
            // compact object so the full frame fits the logger's 512-byte cap.
            if(trace.model)fields<<",\"model\":{\"state\":"<<static_cast<int>(trace.model->state)
                <<",\"reason\":"<<static_cast<int>(trace.model->reason)
                <<",\"fuel\":["<<trace.model->before<<','<<trace.model->after<<']'
                <<",\"checks\":"<<trace.model->work.checkpoints
                <<",\"prefix\":"<<trace.model->work.prefixChecks
                <<",\"fast\":"<<trace.model->work.fastAttacks
                <<",\"basic\":"<<trace.model->work.scalarAttacks
                <<",\"phase\":"<<static_cast<int>(trace.model->work.phase)
                <<",\"resumed\":"<<trace.model->resumed<<",\"complete\":"<<trace.model->complete
                <<",\"chunk\":"<<trace.model->chunkOwnerTurns
                <<",\"turns\":"<<trace.model->completedOwnerTurns<<'}';
            telemetry_.emit("sortie-preparation",fields.str());sortie_preparation_trace_.reset();
        }
        if(capital_denial_ && telemetry_.detail_available() && cambc::fuel_remaining()>=45000) {
            const auto [round,code]=*capital_denial_;
            TelemetryFields fields;
            fields<<",\"observed_round\":"<<round<<",\"code\":"<<static_cast<unsigned>(code)
                <<capital_trace_descriptors[static_cast<unsigned>(code)].fragment;
            telemetry_.emit("capital-denial",fields.str());capital_denial_.reset();
        }
        // Optional diagnostics consume only leftover fuel after gameplay and
        // its cleanup. A summary must never borrow the next action's reserve.
#if PLAN3_CONTINUATION_JOURNAL
        if(telemetry_.turns==before)return;
        const auto continuation_tail_fuel=cambc::fuel_remaining();
        if(continuation_tail_fuel<30000)return;
#else
        if(telemetry_.turns==before || cambc::fuel_remaining()<30000)return;
#endif
        const bool final=cambc::round_number()+1>=cambc::constants::max_rounds;
        if(final || telemetry_.turns%32==0) {
            if(!selector_ && (controller_==plan3::Controller::ledger || controller_==plan3::Controller::atlas))
                telemetry_.summary_sortie(sortie_builder_.metrics().snapshot(),final);
            else telemetry_.summary(final);
        }
#if PLAN3_CONTINUATION_JOURNAL
        continuation_journal_.flush(static_cast<unsigned>(continuation_tail_fuel));
#endif
    }
#if PLAN3_QUIET_BOUNDARY_PACKET
    void quiet_packet_flush() { quiet_boundary_.flush(); }
#endif
};
inline int run(plan3::Controller controller,bool control,const char* profile) {
    Campaign bot(controller,control,profile);
    DraftMarketAllocation allocation;
    while (true) {
        try {
            if (cambc::is_draft()) {
                if(controller==plan3::Controller::market) {
                    const auto status=allocation.step();
                    std::fprintf(stdout,"P3D {\"allocation_status\":%d,\"considered\":%u,\"truncated\":%u}\n",
                        static_cast<int>(status),allocation.considered_groups(),allocation.truncated_groups());
                }
                fixed_draft();
            } else {
                bot.turn();
#if PLAN3_QUIET_BOUNDARY_PACKET
                bot.quiet_packet_flush();
#endif
            }
        }
        catch (const std::exception& e) { std::fprintf(stdout,"p3 error: %.240s\n",e.what()); std::fflush(stdout); }
        catch (...) { std::fputs("p3 error: unknown exception\n",stdout); std::fflush(stdout); }
        cambc::end_turn();
    }
}
} // namespace plan3_runtime
