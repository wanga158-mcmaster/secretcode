#pragma once
#include "candidates.h"
#include "coalition.h"
#include "context_geometry.h"
#include "defaults.h"
#include "escape_choice.h"
#include "offer_storage.h"
#include "admission_slack.h"
#include "../../canary-and-cashflow/src/policy.h"
#include "../../earned-center/src/policy.h"
#include "../../earned-center/src/admission_wait.h"
#include "../../earned-center/src/draft.h"
#include "../../banded-cells/src/policy.h"
#include "../../banded-cells/src/draft.h"
#include <cstdio>
#include <iostream>
#include <memory>
#include <type_traits>

namespace p8::native {
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
#ifndef P8_CANARY_WARD_INSPECTION
#define P8_CANARY_WARD_INSPECTION 0
#endif
inline constexpr common::Arm runtime_arm=P8_CONTROL?common::Arm::control:common::Arm::active;
// One persistent guest owns its ledger, work generation and actual receipts.
// The adapter never imports a replay, public-score attribution or ally proof.
template<bool Earned,bool Cells=false> class Runtime {
    using Inputs=std::conditional_t<Cells,banded_cells::Inputs,
        std::conditional_t<Earned,earned_center::Inputs,common::Inputs>>;
    Routes routes_;
    sdk::Adapter adapter_;
    std::unique_ptr<Inputs> input_=make_default<Inputs>();
    std::unique_ptr<SoloStorage> own_model_;
    struct TurnBudget {
        std::array<unsigned long long,10> fuel{};
        std::array<unsigned long long,3> solo{};
        std::array<std::uint64_t,3> queue{};
        std::array<unsigned long long,5> decision_fuel{};
        std::array<unsigned long long,4> admission_fuel{};
        int entered=0,returned=0,sites=0,banks=0,observation=0,model=-1,issue=-1,progress=0;
        int site=-1,bank=-1,admission_started=0;
        int selection_attempts=0,decision_rejection=-1,offer_rejection=-1,actor=-1;unsigned long long solo_entry=0,exit_fuel=0;
        bool ready=false;
    } pending_budget_;
    unsigned model_budget_logged_=0;
    int model_budget_progress_seen_=0;
    static int model_budget_rank(int progress) {
        switch(progress) {
        case 21:return 3;case 22:return 4;case 3:return 5;case 4:return 6;case 5:return 7;
        case 51:return 8;case 6:return 9;case 7:return 10;case 8:return 11;case 9:return 12;case 10:return 13;
        default:return progress;
        }
    }
    std::unique_ptr<EvidenceLedger> ledger_;
    p6::ScoreHistory scores_;
    common::ProbeBudget probes_;
    Work work_;
    bool admission_wait_pending_=false;
    NumericState admission_entry_{};
    LocalFrame admission_frame_=exact_default<LocalFrame>();
    earned_center::CycleTrace trace_;
    std::array<p6::CycleCertificate,p6::max_actors> initial_safety_{};
    int initial_safety_count_=0;
    int selected_site_=-1,selected_bank_=-1,last_round_=-1;
    int context_site_=-1,context_bank_=-1,renewal_site_=-1;
    int inspection_turns_=0,next_scan_=0,inspection_site_=-1,inspection_cursor_=0;
    int forecast_cursor_=-1;
    unsigned diagnostic_dispatch_count_=0;
    bool diagnostic_idle_logged_=false,diagnostic_fallback_logged_=false;
    bool diagnostic_routes_started_=false;
    bool inspection_due_=false;
    bool inspection_presented_=false;
#if P8_CANARY_WARD_INSPECTION
    struct WardInspection {
        int site=-1,turns=0,last_round=-1;
        bool active=false,done=false,revealed=false;
        Field staging{};
        p6::Point ward{};
        int model_round=-1,model_issue=-1,model_status=-1,model_members=0,model_legs=0;
        unsigned long long model_fuel=0;
        bool model_pending=false,model_logged=false;
#if P8_SOLO_INPUT_STATUS
        SoloInputStatus input_status{};
#endif
    } ward_inspection_;
#endif
    Route inspection_{};
    std::array<int,3> components_{-1,-1,-1},supported_{-1,-1,-1};
    std::array<std::array<int,2>,3> cell_supported_{{{-1,-1},{-1,-1},{-1,-1}}};
    std::uint64_t threat_=0;
    bool contexts_=false,unavailable_logged_=false;
    ContextGeometry context_geometry_;
    bool context_prepare_pending_=false,context_prepare_failed_=false;
    banded_cells::Assignments assignments_;
    std::vector<banded_cells::CircuitCandidate> cell_candidates_;
    int cell_model_=0;
    bool cell_models_done_=false;

    // One immutable public first-income model per owner turn. Live work below
    // keeps producing while this finite setup completes; no model is evidence.
    void prepare_cells(const sdk::Snapshot& snapshot) {
        if constexpr(!Cells) return;
        if (cell_models_done_||routes_.sites.empty()||routes_.banks.empty()||!routes_.spawn.ready) return;
        for(const auto& b:routes_.banks)if(!b.approach.ready)return;
        for(const auto& s:routes_.sites)if(!s.approach.ready||!s.capture.ready)return;
        const int count=int(routes_.sites.size())*int(routes_.banks.size());
        if(cell_model_>=count*2) {
            banded_cells::Assignments::select_into(assignments_,snapshot.draft,cell_candidates_);
            cell_models_done_=true;return;
        }
        if(cambc::fuel_remaining()<65000)return;
        const int ordinal=cell_model_/count,index=cell_model_%count;
        const int site=index/int(routes_.banks.size()),bank=index%int(routes_.banks.size());
        ++cell_model_;
        p6::Frame frame;frame.clock={0,snapshot.draft.actors[ordinal],0};auto& actor=frame.own.actor;
        actor.id=frame.clock.self;actor.pick_ordinal=ordinal;actor.type=snapshot.draft.champions[ordinal];
        actor.team=p6::Team::ally;actor.position=routes_.spawn.destination;
        bool found=false;
        for(const auto& design:cambc::champion_designs)if(sdk::type(design.champion_name())==actor.type) {
            const auto stats=design.base_stats();actor.health=actor.max_health=stats.base_health;
            actor.strength=stats.base_strength;actor.attack_range=stats.base_attack_range;
            actor.ranged=actor.attack_range>1;actor.alive=actor.can_move=actor.can_attack=true;found=true;break;
        }
        if(!found)return;
        frame.own.at_spawn=frame.own.spawn_recovery_observed=true;
        const auto model=std::make_unique<Solo>(solo(routes_,site,bank,frame,false,true));
        if(!model->feasible)return;
        banded_cells::CircuitCandidate candidate{model->plan,
            {model->plan.ingress.count-1,p6::Source::public_map},{true,p6::Source::public_map}};
        // Keep four nearest distinct districts for each actual draft identity.
        auto rank=[](const auto& c){return std::array<int,3>{c.spawn_distance.value,c.plan.group,c.plan.altar};};
        for(auto& old:cell_candidates_)if(old.plan.collector==actor.id&&old.plan.group==candidate.plan.group) {
            if(rank(candidate)<rank(old))old=std::move(candidate);return;
        }
        int owned=0,worst=-1;
        for(int i=0;i<int(cell_candidates_.size());++i)if(cell_candidates_[i].plan.collector==actor.id) {
            ++owned;if(worst<0||rank(cell_candidates_[worst])<rank(cell_candidates_[i]))worst=i;
        }
        if(owned<4)cell_candidates_.push_back(std::move(candidate));
        else if(worst>=0&&rank(candidate)<rank(cell_candidates_[worst]))cell_candidates_[worst]=std::move(candidate);
    }
    common::DraftRoster proposal_roster(const sdk::Snapshot& snapshot,int collector,int chosen_helper=-1) const {
        auto roster=snapshot.draft;
        if constexpr(Cells) {
            int helper=chosen_helper;
            const auto* leader=snapshot.frame.visible(snapshot.draft.actors[collector]);
            for(int i=3;chosen_helper<0&&i<snapshot.draft.picks;++i)if(const auto* a=snapshot.frame.visible(snapshot.draft.actors[i]);
                a&&a->alive&&leader&&(helper<0||distance(a->position,leader->position)<
                distance(snapshot.frame.visible(snapshot.draft.actors[helper])->position,leader->position)))helper=i;
            if(helper<0) {roster.picks=0;return roster;}
            std::swap(roster.actors[0],roster.actors[collector]);
            std::swap(roster.champions[0],roster.champions[collector]);
            std::swap(roster.actors[2],roster.actors[helper]);
            std::swap(roster.champions[2],roster.champions[helper]);
        }
        return roster;
    }

    static std::uint64_t threats(int except) {
        std::uint64_t hash=1469598103934665603ULL;
        for (const auto& camp:cambc::vision::camps())
            if (camp.is_alive()&&camp.is_awake()&&group_id(camp.camp_group())!=except) {
                hash=hash_word(hash,std::uint64_t(sdk::identity(camp))+1);
                hash=hash_word(hash,std::bit_cast<std::uint64_t>(double(camp.strength())));
            }
        for (const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
            hash=hash_word(hash,std::uint64_t(sdk::identity(enemy))+1);
            const auto p=enemy.position();hash=hash_word(hash,std::uint32_t(p.x));hash=hash_word(hash,std::uint32_t(p.y));
            hash=hash_word(hash,std::bit_cast<std::uint64_t>(double(enemy.strength())));
            hash=hash_word(hash,std::bit_cast<std::uint64_t>(double(enemy.focus())));
        }
        return hash;
    }
    void populate(const sdk::Snapshot& snapshot) {
        auto& in=*input_;
        in.frame=snapshot.frame;
        // Engine settings and champion designs are immutable for this guest.
        // Publish the queried source only after the complete identity query.
        if(in.settings_identity.source!=p6::Source::queried_setting) {
            in.settings=sdk::settings();
            in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
        }
        in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        if(work_.active) {
            const int regime=int(threats(selected_site_<0?-1:group_id(routes_.sites[selected_site_].group))&0x7fffffff);
            in.threat_regime={regime,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        } else in.threat_regime={}; // Filled for the actual winner before any decision or declaration.
        const int enemy_score=int(cambc::global_submitted_gems(cambc::team::enemy));
        scores_.sample({snapshot.frame.clock.round,snapshot.frame.clock.self},enemy_score);
        in.windows=scores_.windows(in.settings,{snapshot.frame.clock.round,snapshot.frame.clock.self},enemy_score);
        if constexpr(Earned) in.spawn_shop_legal=snapshot.spawn_shop_legal;
    }
    std::array<common::DraftRoster,2> context_rosters(const sdk::Snapshot& snapshot,int& count) const {
        std::array<common::DraftRoster,2> rosters{};
        count=Cells?std::max(0,snapshot.draft.picks-3):1;
        if(count>int(rosters.size())){count=0;return rosters;}
        for(int i=0;i<count;++i)rosters[i]=proposal_roster(snapshot,snapshot.frame.own.actor.pick_ordinal,Cells?i+3:-1);
        return rosters;
    }
    bool prepare_contexts(sdk::Snapshot& snapshot) {
        if(!context_prepare_pending_||context_prepare_failed_||contexts_)return false;
        const auto current=[&](std::uint64_t reserve) {
            return int(cambc::round_number())==snapshot.frame.clock.round&&cambc::fuel_remaining()>=reserve;
        };
        // The retained candidate is used only as public route payload. Its old
        // model, clock and certificate are never offered or admitted here.
        if(!snapshot.frame.own.at_spawn||snapshot.own.health!=snapshot.own.max_health||
           !current(60000)||!calm(snapshot)||!current(50000)) {
            if(current(25000))fallback(snapshot);
            return true;
        }
        if(!context_geometry_.frozen()) {
            int count=0;const auto rosters=context_rosters(snapshot,count);
            const auto& candidate=own_model_->result;
            if(!context_geometry_.freeze(routes_,candidate.site,candidate.bank,snapshot.frame.clock.self,
                    candidate.plan,{rosters.data(),std::size_t(count)},Earned)) {
                context_prepare_failed_=true;context_prepare_pending_=false;
            }
        } else {
            context_geometry_.advance(routes_,snapshot.frame.clock.round);
            if(context_geometry_.complete())context_prepare_pending_=false;
        }
        // A quantum grants no live premise or gameplay receipt. Always yield;
        // declarations later require a fresh current own model and snapshot.
        if(!current(20000))return true;
        return true;
    }
    void declare_contexts(const sdk::Snapshot& snapshot,const Plan& selected) {
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        if (contexts_||ledger_->active()||(Cells?ordinal>1:ordinal!=0)) return;
        if constexpr(Cells) if(!assignments_.valid(snapshot.draft))return;
        int shape_count=0;const auto rosters=context_rosters(snapshot,shape_count);
        if(!context_geometry_.ready()||!context_geometry_.matches(routes_,selected_site_,selected_bank_,
                snapshot.frame.clock.self,selected,{rosters.data(),std::size_t(shape_count)},Earned))return;
        const auto& own=snapshot.own;
        const auto fixed_hash=context_geometry_.fixed_hash,return_hash=context_geometry_.return_hash;
        const double bag_bound=std::max(double(cambc::constants::winning_gems),
            double(routes_.sites[selected_site_].group.gem_value()));
        const double max_focus=own.focus+(snapshot.frame.own.actor.type==p6::Champion::prism?
            double(cambc::constants::passive_abilities::prism_focus_per_gem)*bag_bound:0.);
        const auto current=[&]{return int(cambc::round_number())==snapshot.frame.clock.round&&cambc::fuel_remaining()>=20000;};
        if(!current())return;
        // One current declaration attempt. If a guard stops this batch, retain
        // issued slots and leave the rest unavailable; never overwrite a slot
        // with a duplicate rejection or mix a later live build/threat baseline.
        contexts_=true;context_site_=selected_site_;context_bank_=selected_bank_;
        for (int band=0;band<3;++band) {
            // One actual entry band plus both optional helper memberships fits
            // three slots. Other bands remain productive U without borrowed
            // evidence; there is no hidden ninth context or dynamic eviction.
            if constexpr(Cells) if(band!=int(health_band(own.health,own.max_health)))continue;
            ContextDeclaration d;auto& key=d.key;
            key.settings_id=input_->settings_identity.value;key.route_hash=fixed_hash;
            key.return_route_hash=return_hash;
            key.actor_id=snapshot.frame.clock.self;key.champion=int(snapshot.frame.own.actor.type);key.role=0;
            key.source_group=context_geometry_.source_group;key.route_template=0;key.endpoint=context_geometry_.endpoint;
            key.threat_regime=input_->threat_regime.value;key.participants[0]=key.actor_id;key.participant_count=1;
            key.build={own.equipment_hash,own.max_health,own.strength,own.focus,own.growth};key.hp_band=HealthBand(band);
            d.envelope={{std::nextafter(0.,1.),own.max_health},{own.max_health,own.max_health},
                {own.strength,own.strength},{own.focus,max_focus},{own.growth,own.growth},
                {0,bag_bound},0};
            d.require_spawn_shop=Earned;
            if(!current())return;
            components_[band]=ledger_->predeclare(d);
            // Membership is fixed at declaration; absent actual members never
            // become an offered supported plan merely because these IDs exist.
            for(int i=0;i<shape_count;++i) {
                const auto& shape=context_geometry_.supported[i];
                if (shape.valid) {
                    auto full=d;
                    full.kind=EpisodeKind::full_organization;full.key.role=1;full.key.route_hash=shape.hash;
                    full.key.participants=shape.participants;full.key.participant_count=shape.count;
                    full.require_supported_work=true;
                    if(!current())return;
                    const int slot=ledger_->predeclare(full);
                    if constexpr(Cells)cell_supported_[band][i]=slot;
                    else supported_[band]=slot;
                }
            }
        }
        if(!current())return;
    }
    void bind_context(const sdk::Snapshot& snapshot,common::Offer& offer) {
        auto& in=*input_;const int band=int(health_band(snapshot.own.health,snapshot.own.max_health));
        in.component_slot=components_[band];in.supported_slot=supported_[band];
        if (const auto* s=ledger_->state(in.component_slot)) in.component_entry=s->declaration.key;
        if (const auto* s=ledger_->state(in.supported_slot)) in.supported_entry=s->declaration.key;
        offer.context_slot=in.component_slot;offer.entry=in.component_entry;
    }
    std::vector<Solo> independent_models(const sdk::Snapshot& snapshot) {
        std::vector<Solo> out;out.reserve(3);
        for (int ordinal=0;ordinal<(Cells?5:3)&&ordinal<snapshot.draft.picks;++ordinal) {
            const auto* actor=snapshot.frame.visible(snapshot.draft.actors[ordinal]);
            if (!actor||!actor->alive)continue;
            const auto frame=subject(snapshot.frame,*actor);std::unique_ptr<Solo> best;
            for (int s=0;s<int(routes_.sites.size())&&cambc::fuel_remaining()>45000;++s) {
                if constexpr(Cells) if(runtime_arm==common::Arm::active&&ordinal<2&&assignments_.valid(snapshot.draft)&&
                    group_id(routes_.sites[s].group)!=assignments_.for_ordinal(ordinal)->group)continue;
                bool used=false;for (const auto& previous:out)used|=previous.plan.group==group_id(routes_.sites[s].group);
                if (used)continue;
                for (int b=0;b<int(routes_.banks.size())&&cambc::fuel_remaining()>45000;++b) {
                    if constexpr(Cells) if(runtime_arm==common::Arm::active&&ordinal<2&&assignments_.valid(snapshot.draft)&&
                        routes_.banks[b].altar.y*cambc::map::width()+routes_.banks[b].altar.x!=assignments_.for_ordinal(ordinal)->altar)continue;
                    auto candidate=std::make_unique<Solo>(solo(routes_,s,b,frame,Earned));
                    if (candidate->feasible&&(!best||candidate->plan.forecast_rounds<best->plan.forecast_rounds||
                        (candidate->plan.forecast_rounds==best->plan.forecast_rounds&&candidate->plan.group<best->plan.group)))
                        best=std::move(candidate);
                }
            }
            if (best) {normalize(*best,snapshot.frame);out.push_back(std::move(*best));}
        }
        return out;
    }
    common::Decision decide() {
        if constexpr(Cells) {
            if(!assignments_.valid(input_->draft))
                return canary::decide(*input_,*ledger_,probes_,common::Arm::control);
            return banded_cells::decide(*input_,*ledger_,probes_,runtime_arm);
        } else if constexpr(Earned) return earned_center::decide(*input_,*ledger_,probes_,runtime_arm);
        else return canary::decide(*input_,*ledger_,probes_,runtime_arm);
    }
    common::Admission commit(const LocalFrame& local,const common::Decision& decision) {
        if constexpr(Cells) {
            if(!assignments_.valid(input_->draft))
                return canary::commit(*input_,local,decision,*ledger_,probes_,common::Arm::control,work_);
            return banded_cells::commit(*input_,local,decision,*ledger_,probes_,runtime_arm,work_);
        } else if constexpr(Earned) return earned_center::commit(*input_,local,decision,*ledger_,probes_,runtime_arm,work_);
        else return canary::commit(*input_,local,decision,*ledger_,probes_,runtime_arm,work_);
    }
    Command advance(const LocalFrame& local) {
        if constexpr(Cells) return banded_cells::advance(*input_,local,*ledger_,runtime_arm,work_);
        else if constexpr(Earned) return earned_center::advance(*input_,local,*ledger_,runtime_arm,work_);
        else return canary::advance(*input_,local,*ledger_,runtime_arm,work_);
    }
    std::array<p6::CycleCertificate,p6::max_actors> current_safety(const sdk::Snapshot& snapshot) const {
        auto result=initial_safety_;
        for (int i=0;i<initial_safety_count_;++i) {
            result[i].clock=snapshot.frame.clock;
            if (const auto* actor=snapshot.frame.visible(result[i].actor))
                for (auto* exposure:{&result[i].clear,&result[i].hold,&result[i].bank,&result[i].recovery}) {
                    exposure->health=actor->health;exposure->max_health=actor->max_health;
                }
        }
        return result;
    }
    bool calm(const sdk::Snapshot& snapshot) const {
        const auto pos=snapshot.frame.own.actor.position;
        for (const auto& enemy:cambc::vision::champions(cambc::team::enemy))
            if (distance(pos,point(enemy.position()))<=enemy.attack_range()+2) return false;
        for (const auto& camp:cambc::vision::camps())
            if (camp.is_alive()&&camp.is_awake()&&distance(pos,point(camp.position()))<=camp.attack_range()+1) return false;
        return true;
    }
    bool productive(const sdk::Snapshot& snapshot) const {
        if (renewal_site_<0||renewal_site_>=int(routes_.sites.size()))return false;
        const auto live=group(routes_.sites[renewal_site_],snapshot.frame.clock);
        return live.original_spawn_visible&&(live.lifecycle==p6::Lifecycle::living||
            live.lifecycle==p6::Lifecycle::reward_available);
    }
    std::optional<p6::Point> escape(const sdk::Snapshot& snapshot) const {
        const auto current=[&] {
            return int(cambc::round_number())==snapshot.frame.clock.round&&cambc::fuel_remaining()>=20000;
        };
        if(!current())return {};
        const auto origin=snapshot.frame.own.actor.position;
        const auto camps=cambc::vision::camps();
        if(!current())return {};
        const auto enemies=cambc::vision::champions(cambc::team::enemy);
        if(!current())return {};
        struct Threat {p6::Point position;int range;double damage;};
        std::vector<Threat> observed;
        for(const auto& camp:camps) {
            if(!current())return {};
            if(camp.is_alive()&&camp.is_awake())
                observed.push_back({point(camp.position()),camp.attack_range(),double(camp.strength())});
            if(!current())return {};
        }
        for(const auto& enemy:enemies) {
            if(!current())return {};
            double cast=0;for(const auto& ability:enemy.abilities())
                if(ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))
                    cast=std::max(cast,double(ability.damage_amount()));
            observed.push_back({point(enemy.position()),enemy.attack_range()+1,double(enemy.strength())+cast});
            if(!current())return {};
        }
        auto incoming=[&](p6::Point p) {
            double damage=0;
            for(const auto& threat:observed)
                if(distance(p,threat.position)<=threat.range)damage+=threat.damage;
            return damage;
        };
        auto clearance=[&](p6::Point p) {
            int value=std::numeric_limits<int>::max();
            for(const auto& threat:observed)if(threat.damage>0)
                value=std::min(value,distance(p,threat.position)-threat.range);
            return value;
        };
        EscapeInputs choices;std::array<p6::Point,max_escape_neighbors> tiles{};
        choices.origin_damage=incoming(origin);choices.origin_clearance=clearance(origin);
        choices.health=snapshot.frame.own.actor.health;
        const auto occupied=cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
        for (int y=-1;y<=1;++y)for (int x=-1;x<=1;++x) {
            p6::Point p{origin.x+x,origin.y+y};
            if ((!x&&!y)||p.x<0||p.y<0||p.x>=cambc::map::width()||p.y>=cambc::map::height()||
                routes_.terrain().is_set(tile(p))||occupied.is_set(tile(p)))continue;
            tiles[choices.count]=p;
            choices.neighbors[choices.count++]={incoming(p),clearance(p)};
        }
        // Immediate damage remains first priority. Equal-damage progress is
        // only a local emergency step, never a route or survival certificate.
        const int selected=choose_escape(choices);
        if(!current()||selected<0)return {};
        return tiles[selected];
    }
    void diagnostic_receipt(const Command& command,const LocalFrame& local,bool attempted,bool success,int reason) {
        // Test the cap and reserve before queries or numeric formatting. These
        // first receipts diagnose the executor without changing its gates.
        if(diagnostic_dispatch_count_>=12||cambc::fuel_remaining()<32000)return;
        ++diagnostic_dispatch_count_;
        const int leg=int(command.leg);
        const StepAccess access=leg>=0&&leg<int(local.legal.value.routes.size())?local.legal.value.routes[leg]:StepAccess{};
        const auto& self=cambc::self();
        const auto position=self.is_alive()?point(self.position()):p6::Point{-1,-1};
        std::printf("p8 DIAG dispatch n=%u actor=%d r=%d cr=%d action=%d attempted=%d success=%d reason=%d x=%d y=%d tx=%d ty=%d lx=%d ly=%d fresh=%d safe=%d leg=%d assumptions=%d nextlegal=%d nx=%d ny=%d fuel=%llu\n",
            diagnostic_dispatch_count_,local.frame.clock.self,int(cambc::round_number()),command.clock.round,int(command.action),
            int(attempted),int(success),reason,position.x,position.y,command.target.x,command.target.y,
            local.frame.own.actor.position.x,local.frame.own.actor.position.y,int(local.fresh()),int(local.legal.value.safe_now),
            leg,int(access.assumptions_hold),int(access.next_move_legal),access.next_tile.x,access.next_tile.y,
            static_cast<unsigned long long>(cambc::fuel_remaining()));
    }
    void dispatch(const Command& command,const LocalFrame& local) {
        if (local.frame.clock.round!=int(cambc::round_number())||command.clock!=local.frame.clock) {
            diagnostic_receipt(command,local,false,false,1);return;
        }
        const auto receipt=adapter_.dispatch(command,local,work_);
        if (receipt.current_clock!=command.clock) {
            work_.request_stop();work_.route_witness_active=false;
            if (ledger_&&ledger_->active())ledger_->invalidate_active(receipt.current_clock.round);
            diagnostic_receipt(command,local,receipt.attempted,receipt.success,2);
            return;
        }
        work_.confirm(command,receipt);
        diagnostic_receipt(command,local,receipt.attempted,receipt.success,0);
    }
    void manual(const Plan& plan,const LocalFrame& local,PhysicalAction action,p6::Point target={},Leg leg=Leg::ingress) {
        if(!diagnostic_fallback_logged_&&cambc::fuel_remaining()>=32000) {
            diagnostic_fallback_logged_=true;
            std::printf("p8 DIAG fallback actor=%d r=%d site=%d path=%d bankpath=%d cursor=%d turns=%d action=%d fuel=%llu\n",
                local.frame.clock.self,int(cambc::round_number()),inspection_site_,inspection_.count,plan.bank.count,
                inspection_cursor_,inspection_turns_,int(action),static_cast<unsigned long long>(cambc::fuel_remaining()));
        }
        work_.plan=plan;work_.route_witness_active=false;
        dispatch(work_.emit(local,action,target,leg),local);
    }
#if P8_CANARY_WARD_INSPECTION
    // A single paid personal-observation attempt. Public harmlessness chooses
    // where to look; only fresh own vision can supply a live model premise.
    bool ward_inspection(sdk::Snapshot& snapshot) {
        if constexpr(Earned||Cells)return false;
        auto& trial=ward_inspection_;const auto& actor=snapshot.frame.own.actor;
        const int now=snapshot.frame.clock.round;
        const auto current=[&](std::uint64_t reserve=45000) {
            return int(cambc::round_number())==now&&cambc::fuel_remaining()>=reserve;
        };
        if(actor.pick_ordinal!=2||actor.type!=p6::Champion::varg)return false;
        if(trial.model_pending&&!trial.model_logged&&current(65000)) {
#if P8_SOLO_INPUT_STATUS
            const auto& s=trial.input_status;
            std::printf("p8 WARD_INPUT actor=%d r=%d model_r=%d group=%d phase=%d reason=%d observed_r=%d budget_fuel=%llu source_bits=%d field_bits=%d route_bits=%d ingress=%d capture=%d bank=%d renewal=%d groups=%d members=%d legs=%d issue=%d status=%d exit_fuel=%llu\n",
                actor.id,now,trial.model_round,group_id(routes_.sites[trial.site].group),s.phase,s.reason,s.round,s.fuel,
                s.source_bits,s.field_bits,s.route_bits,s.route_counts[0],s.route_counts[1],s.route_counts[2],s.route_counts[3],
                s.groups,s.members,s.legs,trial.model_issue,trial.model_status,trial.model_fuel);
#else
            std::printf("p8 WARD_MODEL actor=%d r=%d model_r=%d group=%d issue=%d status=%d members=%d legs=%d fuel=%llu\n",
                actor.id,now,trial.model_round,group_id(routes_.sites[trial.site].group),trial.model_issue,
                trial.model_status,trial.model_members,trial.model_legs,trial.model_fuel);
#endif
            trial.model_pending=false;trial.model_logged=true;
        }
        if(trial.done)return false;
        const auto finish=[&](int reason) {
            trial.done=true;trial.active=false;
            inspection_site_=-1;inspection_turns_=0;inspection_presented_=false;inspection_due_=false;next_scan_=now+8;
            if(current(22000))std::printf("p8 WARD_END actor=%d r=%d group=%d turns=%d reason=%d revealed=%d used=%d\n",
                actor.id,now,trial.site<0?-1:group_id(routes_.sites[trial.site].group),trial.turns,reason,trial.revealed,probes_.used);
        };
        if(!trial.active) {
            if(!snapshot.frame.own.at_spawn||actor.bag!=0||!current(65000)||
                !probes_.available(now/common::project_rounds)||!calm(snapshot)||!current(60000))return false;
            int nearest=-1,best=max_route_tiles;
            for(int i=0;i<int(routes_.sites.size());++i) {
                const auto& source=routes_.sites[i];
                if(!source.approach.ready||!source.capture.ready)continue;
                bool harmless=true;
                for(const auto& member:source.group)harmless&=member.base_stats().base_strength==0;
                if(!harmless)continue;
                const int cost=distance(routes_.spawn.destination,source.approach.destination);
                if(cost<best){nearest=i;best=cost;}
            }
            if(nearest<0||!current(55000))return false;
            if(!trial.staging.ready) {
                // The attack pad can lie beyond a long wall detour. Stage on
                // our side of one proposed ward view, not at the attack pad.
                auto ward=routes_.sites[nearest].approach.destination;
                const auto spawn=routes_.spawn.destination;
                const int dx=spawn.x-ward.x,dy=spawn.y-ward.y;
                const auto sign=[](int n){return (n>0)-(n<0);};
                if(std::abs(dx)>std::abs(dy))ward.x+=sign(dx);else ward.y+=sign(dy);
                const int reach=int(cambc::constants::ward_placement_radius);
                const p6::Point staging{ward.x+sign(spawn.x-ward.x)*reach,ward.y+sign(spawn.y-ward.y)*reach};
                if(reach<1||ward.x<0||ward.y<0||staging.x<0||staging.y<0||
                    ward.x>=cambc::map::width()||ward.y>=cambc::map::height()||
                    staging.x>=cambc::map::width()||staging.y>=cambc::map::height()||
                    routes_.terrain().is_set(tile(ward))||routes_.terrain().is_set(tile(staging))) {
                    trial.site=nearest;finish(7);return true;
                }
                trial.ward=ward;trial.staging.destination=staging;
                cambc::bitboard visited;
                trial.staging.paths.compute(routes_.terrain(),singleton(staging),visited,256);
                trial.staging.ready=true;
            }
            const auto path=routes_.route(actor.position,trial.staging);
            if(!route_valid(path)||!current(45000))return false;
            if(!probes_.spend(now/common::project_rounds))return false;
            trial.site=nearest;trial.active=true;
            inspection_site_=nearest;inspection_=path;inspection_cursor_=0;inspection_turns_=0;
            inspection_presented_=false;inspection_due_=false;next_scan_=now;
            std::printf("p8 WARD_BEGIN actor=%d r=%d group=%d x=%d y=%d path=%d used=%d\n",
                actor.id,now,group_id(routes_.sites[nearest].group),actor.position.x,actor.position.y,path.count,probes_.used);
        }
        if(trial.last_round>=0&&now!=trial.last_round+1){finish(1);return true;}
        if(trial.turns>=common::max_probe_turns){finish(2);return true;}
        trial.last_round=now;++trial.turns;probes_.observed_diagnostic_turns(trial.turns);
        if(actor.bag!=0||!actor.alive||actor.channel!=p6::Channel::none||!current(45000)||!calm(snapshot)||!current(40000)) {
            finish(3);return true;
        }
        const auto& source=routes_.sites[trial.site];
        const auto spawns=source.group.spawn_tiles();const auto visible=cambc::vision::visible_tiles();
        if((spawns&~visible).is_empty()) {
            trial.revealed=true;forecast_cursor_=trial.site*int(routes_.banks.size());finish(4);return true;
        }
        // The placement mask is distance only. Static terrain, visible wards,
        // restrictions and the actual private inventory are separate checks.
        const auto self=cambc::self();cambc::detail::ward_resources resource{};
        cambc::detail::check(cambc::detail::sdk_champion_wards(
            static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(self)),&resource));
        const bool restricted=self.restrictions().contains(cambc::buff_restrictions::ward);
        if(resource.charges>0&&!restricted&&current(35000)) {
            auto placements=self.ward_placement_tiles();placements&=~(routes_.terrain()|spawns|cambc::vision::ward_tiles(cambc::team::ally)|cambc::vision::camp_tiles());
            const auto candidate=tile(trial.ward);
            const std::optional<cambc::vec2> destination=placements.is_set(candidate)?std::optional{candidate}:std::nullopt;
            if(destination&&current(30000)) {
                const auto hypothetical=cambc::ward::visible_tiles_from(*destination);
                const bool covers=(spawns&~hypothetical).is_empty();
                if(covers&&current(26000)) {
                    const bool success=self.place_ward(*destination);
                    const auto after=cambc::vision::visible_tiles();trial.revealed=success&&(spawns&~after).is_empty();
                    cambc::detail::ward_resources remaining{};
                    cambc::detail::check(cambc::detail::sdk_champion_wards(
                        static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(self)),&remaining));
                    std::printf("p8 WARD_RECEIPT actor=%d r=%d group=%d turns=%d x=%d y=%d wx=%d wy=%d charges=%u cooldown=%d restricted=%d in_range=%d hypothetical=%d success=%d own_visible=%d after_charges=%u after_cooldown=%d\n",
                        actor.id,now,group_id(source.group),trial.turns,actor.position.x,actor.position.y,destination->x,destination->y,
                        resource.charges,resource.cooldown,restricted,self.ward_placement_tiles().is_set(*destination),covers,
                        success,trial.revealed,remaining.charges,remaining.cooldown);
                    if(trial.revealed)forecast_cursor_=trial.site*int(routes_.banks.size());
                    finish(success?5:6);return true;
                }
            }
        }
        if(current(50000)) {
            const auto pos=actor.position;
            if(inspection_cursor_+1<inspection_.count&&pos==inspection_.tiles[inspection_cursor_+1])++inspection_cursor_;
            if(inspection_cursor_+1<inspection_.count) {
                auto blocked=routes_.terrain()|cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
                if(blocked.is_set(tile(inspection_.tiles[inspection_cursor_+1]))) {
                    int best=routes_.static_distance(pos,trial.staging);std::optional<p6::Point> next;
                    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
                        const p6::Point candidate{pos.x+x,pos.y+y};
                        if((x==0&&y==0)||candidate.x<0||candidate.y<0||candidate.x>=cambc::map::width()||candidate.y>=cambc::map::height()||blocked.is_set(tile(candidate)))continue;
                        const int remaining=routes_.static_distance(candidate,trial.staging);
                        if(remaining<best){best=remaining;next=candidate;}
                    }
                    if(next&&current(45000)&&routes_.route_into(*next,trial.staging,inspection_)&&inspection_.count<max_route_tiles) {
                        for(int i=inspection_.count;i>0;--i)inspection_.tiles[i]=inspection_.tiles[i-1];
                        inspection_.tiles[0]=pos;++inspection_.count;inspection_cursor_=0;
                    }
                }
            }
        }
        if(current(45000))fallback(snapshot);
        if(trial.turns==common::max_probe_turns)finish(2);
        return true;
    }
#endif
    void fallback(sdk::Snapshot& snapshot) {
        const bool safe=calm(snapshot);
        Plan route=exact_default<Plan>();route.collector=snapshot.frame.clock.self;
        route.group=inspection_site_>=0?group_id(routes_.sites[inspection_site_].group):-1;
        if (safe&&snapshot.frame.own.actor.channel!=p6::Channel::none&&snapshot.frame.own.actor.channel_valid) {
            auto local=adapter_.local(snapshot,route,{},safe,safe,false);
            manual(route,local,PhysicalAction::preserve_channel);return;
        }
        // Existing inventory is secured before attempting another gem source.
        if (snapshot.frame.own.actor.bag>0&&safe) {
            int best=-1;Route bank_path;
            for (int i=0;i<int(routes_.banks.size());++i) {
                const auto path=routes_.route(snapshot.frame.own.actor.position,routes_.banks[i].approach);
                if (route_valid(path)&&(best<0||path.count<bank_path.count)) {best=i;bank_path=path;}
            }
            if (best>=0) {
                route.altar=routes_.banks[best].altar.y*cambc::map::width()+routes_.banks[best].altar.x;route.bank=bank_path;
                auto local=adapter_.local(snapshot,route,{},safe,safe,false);
                const bool arrived=bank_path.count==1;
                manual(route,local,arrived?PhysicalAction::tribute:PhysicalAction::move,
                    arrived?p6::Point{}:bank_path.tiles[1],Leg::bank);return;
            }
        }
        const int now=snapshot.frame.clock.round;
        if (safe&&now>=next_scan_&&inspection_turns_<8) {
            if (inspection_site_<0) {
                int nearest=-1,steps=max_route_tiles;bool prepared=false;
                for (int i=0;i<int(routes_.sites.size());++i) {
                    const auto& field=routes_.sites[i].approach;if(!field.ready)continue;
                    prepared=true;const int cost=routes_.static_distance(snapshot.frame.own.actor.position,field);
                    if(cost<steps){nearest=i;steps=cost;}
                }
                if(nearest>=0) {
                    const auto path=routes_.route(snapshot.frame.own.actor.position,routes_.sites[nearest].approach);
                    if(route_valid(path)){inspection_site_=nearest;inspection_=path;inspection_cursor_=0;inspection_presented_=false;}
                    else inspection_turns_=8;
                } else if(prepared)inspection_turns_=8;
                else {
                    // Unprepared immutable fields are not a failed inspection.
                    next_scan_=now+1;
                }
            }
            if (inspection_site_>=0&&route_valid(inspection_)) {
                route.ingress=inspection_;route.group=group_id(routes_.sites[inspection_site_].group);
                const auto pos=snapshot.frame.own.actor.position;
                if (inspection_cursor_+1<inspection_.count&&pos==inspection_.tiles[inspection_cursor_+1])++inspection_cursor_;
                if (pos==inspection_.tiles[inspection_cursor_]) {
                    ++inspection_turns_;auto local=adapter_.local(snapshot,route,{},safe,safe,false);
                    const bool arrived=inspection_cursor_+1==inspection_.count;
                    manual(route,local,arrived?PhysicalAction::inspect:PhysicalAction::move,
                        arrived?p6::Point{}:inspection_.tiles[inspection_cursor_+1]);return;
                }
            }
            if(inspection_site_>=0)inspection_turns_=8;
        }
        if (inspection_turns_>=8) {
            if (!unavailable_logged_) {std::cout<<"p8 OPPORTUNITY_UNAVAILABLE\n";unavailable_logged_=true;}
            next_scan_=now+8;inspection_turns_=0;inspection_site_=-1;
        }
        const auto exit=safe?std::optional<p6::Point>{}:escape(snapshot);
        if(int(cambc::round_number())!=snapshot.frame.clock.round||cambc::fuel_remaining()<20000)return;
        auto local=adapter_.local(snapshot,route,{},safe,safe,false,exit);
        manual(route,local,snapshot.frame.own.at_spawn?PhysicalAction::recover:
            !safe&&exit?PhysicalAction::escape:safe?PhysicalAction::recall:PhysicalAction::yield,exit.value_or(p6::Point{}));
    }
public:
    void turn() {
        const int entered=int(cambc::round_number());
        struct BudgetTrace:TurnBudget {
            bool enabled;TurnBudget* saved;int* progress_seen;
            BudgetTrace(bool enabled,int entered,TurnBudget& saved,int& seen):enabled(enabled),saved(&saved),progress_seen(&seen) {this->entered=entered;}
            void mark(int stage) {if(enabled)this->fuel[stage]=cambc::fuel_remaining();}
            ~BudgetTrace() {
                if(!enabled||this->model!=1)return;
                const int rank=Runtime::model_budget_rank(this->progress);
                if(rank<=*progress_seen)return;
                *progress_seen=rank;
                this->returned=int(cambc::round_number());this->exit_fuel=cambc::fuel_remaining();this->ready=true;*saved=*this;
            }
        } trace{model_budget_logged_<4,entered,pending_budget_,model_budget_progress_seen_};
        trace.mark(0);
        auto room=[&](std::uint64_t reserve=20000) {
            return int(cambc::round_number())==entered&&cambc::fuel_remaining()>=reserve;
        };
        // First-play initialization has a separate allowance. Do not spend its
        // remainder forecasting and then resume a stale proposal next round.
        if(!routes_.initialized()) {
            if(!room(70000))return;
            if(!diagnostic_routes_started_&&cambc::fuel_remaining()>=32000) {
                std::printf("p8 DIAG startup phase=1 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                    int(cambc::round_number()),static_cast<unsigned long long>(cambc::fuel_remaining()));
                diagnostic_routes_started_=true;
            }
            if(!room(70000))return;
            const bool complete=routes_.initialize_step(1);
            if(complete&&cambc::fuel_remaining()>=32000)
                std::printf("p8 DIAG startup phase=2 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                    int(cambc::round_number()),static_cast<unsigned long long>(cambc::fuel_remaining()));
            if(complete&&room(70000))routes_.prepare();
            return;
        }
        if(!own_model_) {
            if(room(70000))own_model_=make_default<SoloStorage>();
            return;
        }
        // Current owned work precedes optional immutable-field preparation.
        if(!work_.active&&room(70000))routes_.prepare();
        trace.sites=int(routes_.sites.size());trace.banks=int(routes_.banks.size());trace.mark(1);
        if(!room(45000))return;
        // Idle Solo requests observe their own target. Retain the full group
        // observation for owned work, then reobserve the actual winner before
        // commit; an abandoned proposal must not tax every later idle frame.
        const auto* selected=!work_.active||selected_site_<0?nullptr:&routes_.sites[selected_site_].group;
        auto snapshot=adapter_.observe(selected,&work_);trace.mark(2);trace.actor=snapshot.frame.clock.self;
        if(trace.enabled)trace.observation=(snapshot.frame.valid()?1:0)|(snapshot.draft.valid()?2:0)|
            (valid_numeric_state(snapshot.own)?4:0)|(snapshot.valid?8:0);
        if(!snapshot.valid||!room(30000))return;
        // Keep optional formatting off the measured model's turn. Use only an
        // idle, calm, fully healed spawn turn, so owned work and escape retain
        // priority. An unprinted record is unavailable telemetry.
        if(pending_budget_.ready&&model_budget_logged_<4&&!work_.active&&snapshot.frame.own.at_spawn&&
            snapshot.frame.own.actor.bag==0&&snapshot.frame.own.actor.channel==p6::Channel::none&&
            snapshot.own.health==snapshot.own.max_health&&room(60000)&&calm(snapshot)&&room(50000)) {
            const auto& b=pending_budget_;
            std::printf("p8 DIAG model_budget entered=%d returned=%d p=%d model=%d issue=%d site=%d bank=%d observation=%d f0=%llu f1=%llu f2=%llu f3=%llu f4=%llu f5=%llu f6=%llu sg=%llu sr=%llu sb=%llu attempts=%d entry=%llu exit=%llu rejection=%d actor=%d offer_rejection=%d foffer=%llu fqueue_in=%llu fqueue_out=%llu qroute=%llu qmodel=%llu qcert=%llu de0=%llu de1=%llu dc0=%llu dc1=%llu dc2=%llu started=%d alocal=%llu acommit=%llu adebit=%llu asetup=%llu\n",
                b.entered,b.returned,b.progress,b.model,b.issue,b.site,b.bank,b.observation,
                b.fuel[0],b.fuel[1],b.fuel[2],b.fuel[3],b.fuel[4],b.fuel[5],b.fuel[6],
                b.solo[0],b.solo[1],b.solo[2],b.selection_attempts,b.solo_entry,b.exit_fuel,b.decision_rejection,
                b.actor,b.offer_rejection,b.fuel[7],b.fuel[8],b.fuel[9],
                static_cast<unsigned long long>(b.queue[0]),static_cast<unsigned long long>(b.queue[1]),
                static_cast<unsigned long long>(b.queue[2]),b.decision_fuel[0],b.decision_fuel[1],
                b.decision_fuel[2],b.decision_fuel[3],b.decision_fuel[4],b.admission_started,
                b.admission_fuel[0],b.admission_fuel[1],b.admission_fuel[2],b.admission_fuel[3]);
            pending_budget_.ready=false;++model_budget_logged_;return;
        }
        if(!ledger_)ledger_=std::make_unique<EvidenceLedger>(snapshot.frame.clock.self);
        populate(snapshot);
        trace.mark(3);
        const bool skipped=last_round_>=0&&snapshot.frame.clock.round!=last_round_+1;
        if(snapshot.frame.clock.round==last_round_)return;
        last_round_=snapshot.frame.clock.round;
        if(work_.active) {
            const auto certificates=current_safety(snapshot);bool safe=false;
            for(int i=0;i<initial_safety_count_;++i)
                if(certificates[i].actor==snapshot.frame.clock.self)
                    safe=certificates[i].valid(snapshot.frame,snapshot.frame.clock.self,work_.plan.group);
            safe&=threats(work_.plan.group)==threat_;
            if(admission_wait_pending_) {
                const auto& old=admission_entry_;const auto& now=snapshot.own;
                const bool same_build=old.equipment_hash==now.equipment_hash&&old.max_health==now.max_health&&
                    old.strength==now.strength&&old.focus==now.focus&&old.growth==now.growth;
                safe&=same_build;
                if(!same_build){work_.request_stop();if(ledger_->active())ledger_->invalidate_active(snapshot.frame.clock.round);}
                admission_wait_pending_=false;
            }
            auto local=adapter_.local(snapshot,work_.plan,{certificates.data(),std::size_t(initial_safety_count_)},
                safe,calm(snapshot),productive(snapshot),escape(snapshot),&work_);
            local.fuel_resumed|=skipped;
            if(!room())return;
            if(skipped){work_.request_stop();if(ledger_->active())ledger_->invalidate_active(snapshot.frame.clock.round);}
            local.fuel_remaining=std::int64_t(cambc::fuel_remaining());
            const auto command=advance(local);
            if(!room(17000))return;
            dispatch(command,local);
            if(!room(16000))return;
            trace_.observe(local,work_,snapshot.spawn_shop_legal);
            if(!work_.active||!ledger_->active()) {
                ContextKey key=trace_.plan.organization==Organization::H?input_->supported_entry:input_->component_entry;
                PremiseMask observed=all_premises;
                for(int i=0;i<key.participant_count;++i)
                    if(!snapshot.frame.visible(key.participants[i]))observed&=~membership_known;
                if(!work_.route_witness_active)observed&=~route_known;
                if(threats(work_.plan.group)!=threat_)observed&=~threat_known;
                if(!room(16000))return;
                const auto result=trace_.finish(*ledger_,snapshot.frame.clock.round,{key,snapshot.own,observed});
                if(result.accepted)std::cout<<"p8 EPISODE group="<<work_.plan.group<<" positive="<<result.full_positive
                    <<" contradiction="<<result.contradiction<<'\n';
            }
            return;
        }
        if(!snapshot.frame.own.actor.alive||snapshot.frame.own.actor.bag>0||snapshot.frame.own.actor.channel!=p6::Channel::none) {
            if(room(25000))fallback(snapshot);return;
        }
#if P8_CANARY_WARD_INSPECTION
        if(ward_inspection(snapshot))return;
#endif
        if(prepare_contexts(snapshot))return;
        // A failed forecast must leave a later full-budget turn for the
        // plan's bounded source inspection, rather than starving it forever.
        bool inspecting=false;
        if(inspection_site_>=0) {
            const auto spawns=routes_.sites[inspection_site_].group.spawn_tiles();
            const bool visible=(spawns&~cambc::vision::visible_tiles()).is_empty();
            inspecting=!visible&&inspection_turns_<8&&snapshot.frame.clock.round>=next_scan_;
            if(visible&&!inspection_presented_) {
                inspection_presented_=true;inspection_due_=false;
                forecast_cursor_=inspection_site_*int(routes_.banks.size());
            }
        }
        if(inspection_due_||inspecting) {
            if(room(45000)){inspection_due_=false;fallback(snapshot);}
            return;
        }
        if constexpr(Cells) {
            if(room(85000))prepare_cells(snapshot);
        }
        trace.mark(4);
        if(!room(55000)) {
            if(!diagnostic_idle_logged_&&int(cambc::round_number())==entered&&cambc::fuel_remaining()>=32000&&cambc::fuel_remaining()<55000) {
                diagnostic_idle_logged_=true;
                std::printf("p8 DIAG idle_budget actor=%d r=%d fuel=%llu\n",snapshot.frame.clock.self,
                    int(cambc::round_number()),static_cast<unsigned long long>(cambc::fuel_remaining()));
            }
            return;
        }
        auto& in=*input_;
        if constexpr(Cells){in.assignments=assignments_;in.proposals={};in.control_route={};}
        const auto visible=cambc::vision::visible_tiles();
        const int pairs=int(routes_.sites.size())*int(routes_.banks.size());
        if(pairs==0){if(room(25000))fallback(snapshot);return;}
        if(forecast_cursor_<0)forecast_cursor_=(snapshot.frame.own.actor.pick_ordinal*int(routes_.banks.size()))%pairs;
        auto eligible=[&](int site,int bank,int actor,std::span<const Solo> used) {
            const auto& source=routes_.sites[site];
            if(!source.approach.ready||!source.capture.ready||!routes_.banks[bank].approach.ready||
                (Earned&&!routes_.spawn.ready))return false;
            // spawn_tiles() builds a fresh map-sized bitmap. This test needs
            // only whether one static member spawn is in the current mask.
            bool seen=false;
            for(const auto& member:source.group)if(visible.is_set(member.spawn_position())) {seen=true;break;}
            if(!seen)return false;
            for(const auto& prior:used)if(prior.plan.group==group_id(source.group))return false;
            if constexpr(Cells)if(runtime_arm==common::Arm::active&&assignments_.valid(snapshot.draft)) {
                for(int ordinal=0;ordinal<2;++ordinal)if(snapshot.draft.actors[ordinal]==actor) {
                    const auto* fixed=assignments_.for_ordinal(ordinal);
                    if(group_id(source.group)!=fixed->group||sdk::altar_id(routes_.banks[bank].altar)!=fixed->altar)return false;
                }
            }
            return true;
        };
        const Solo* best=nullptr;int first=forecast_cursor_;
        if(contexts_&&context_site_>=0&&context_bank_>=0&&
            snapshot.frame.own.actor.position==routes_.sites[context_site_].approach.destination)
            first=context_site_*int(routes_.banks.size())+context_bank_;
        // One complete current-frame own model per idle turn. Infeasible
        // candidates rotate; unobserved/unprepared pairs cost no forecast.
        for(int offset=0;offset<pairs;++offset) {
            const int pair=(first+offset)%pairs,site=pair/int(routes_.banks.size()),bank=pair%int(routes_.banks.size());
            if(trace.enabled)++trace.selection_attempts;
            if(!eligible(site,bank,snapshot.frame.clock.self,{}))continue;
            forecast_cursor_=(pair+1)%pairs;
            if(!room(55000))return;
            trace.site=site;trace.bank=bank;
            if(trace.enabled)trace.solo_entry=cambc::fuel_remaining();
            solo_into(routes_,site,bank,snapshot.frame,Earned,false,own_model_->result,&own_model_->request,
                trace.enabled?&trace.solo:nullptr,{},Earned
#if P8_SOLO_INPUT_STATUS && P8_CANARY_WARD_INSPECTION
                ,ward_inspection_.revealed&&site==ward_inspection_.site&&!ward_inspection_.model_logged?&ward_inspection_.input_status:nullptr
#endif
                );
            const auto* candidate=&own_model_->result;
#if P8_CANARY_WARD_INSPECTION
            if constexpr(!Earned&&!Cells)if(ward_inspection_.revealed&&site==ward_inspection_.site&&!ward_inspection_.model_logged) {
                auto& record=ward_inspection_;record.model_round=entered;record.model_issue=int(candidate->model.issue);
                record.model_status=int(candidate->model.status);record.model_members=own_model_->request.groups[0].member_count;
                record.model_legs=own_model_->request.leg_count;record.model_fuel=cambc::fuel_remaining();record.model_pending=true;
            }
#endif
            inspection_due_=!candidate->feasible;
            trace.model=candidate->feasible?1:0;trace.issue=int(candidate->model.issue);trace.mark(5);
            trace.progress=1;
            if(!room(30000))return;
            if(candidate->feasible)best=candidate;
            break;
        }
        trace.mark(6);
        if(!best){inspection_due_=true;if(room(45000)){inspection_due_=false;fallback(snapshot);}return;}
        trace.progress=2;
        selected_site_=best->site;selected_bank_=best->bank;
        const int ordinal=snapshot.frame.own.actor.pick_ordinal;
        if(!contexts_&&!context_geometry_.frozen()&&!context_prepare_failed_&&
           !ledger_->active()&&(Cells?ordinal<2:ordinal==0)) {
            if constexpr(Cells) {if(assignments_.valid(snapshot.draft))context_prepare_pending_=true;}
            else context_prepare_pending_=true;
            if(context_prepare_pending_)return;
        }
        if(!room(30000))return;
        trace.progress=21;
        // Bind public contexts without re-running the already complete winner.
        const auto target_threat=threats(best->plan.group);
        in.threat_regime={int(target_threat&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        trace.progress=22;
        if(!room(30000))return;
        declare_contexts(snapshot,best->plan);
        trace.progress=3;
        int project=contexts_?context_site_:best->site,project_bank=contexts_?context_bank_:best->bank;
        if constexpr(Cells)if(runtime_arm==common::Arm::control&&assignments_.valid(snapshot.draft)) {
            const auto* fixed=assignments_.for_ordinal(0);
            for(int site=0;site<int(routes_.sites.size());++site)if(group_id(routes_.sites[site].group)==fixed->group)project=site;
            for(int bank=0;bank<int(routes_.banks.size());++bank)if(sdk::altar_id(routes_.banks[bank].altar)==fixed->altar)project_bank=bank;
        }
        int collector_ordinal=Cells&&snapshot.frame.own.actor.pick_ordinal==1?1:0;
        if((!Cells&&snapshot.frame.own.actor.pick_ordinal==2)||(Cells&&snapshot.frame.own.actor.pick_ordinal>=3)) {
            int nearest=std::numeric_limits<int>::max();
            for(int leader=0;leader<(Cells?2:1);++leader)if(const auto* actor=snapshot.frame.visible(snapshot.draft.actors[leader]))
                for(int site=0;site<int(routes_.sites.size());++site)if(routes_.sites[site].rally_count>0&&actor->position==routes_.sites[site].rally[0].destination) {
                    const auto ingress=via_pad(routes_,snapshot.frame.own.actor.position,routes_.sites[site],routes_.sites[site].rally[1].destination);
                    if(!route_valid(ingress))continue;const int cost=ingress.count-1;
                    if(cost<nearest){nearest=cost;collector_ordinal=leader;project=site;}
                }
        }
        in.target_group=group_id(routes_.sites[project].group);in.offer_count=1;in.independent=0;
        auto& offer=in.offers[0];reset_offer(offer);
        if(!copy_plan_into(offer.plan,best->plan)||!copy_schedule_into(offer.schedule,best->model.schedule))return;
        offer.safety[0]=best->model.certificate;offer.safety_count=1;
        bind_context(snapshot,offer);
        trace.progress=4;trace.mark(7);
        in.visible_proposer={};
        const auto* proposer=snapshot.frame.visible(snapshot.draft.actors[collector_ordinal]);
        const bool visible_proposal=proposer&&routes_.sites[project].rally_count>0&&proposer->position==routes_.sites[project].rally[0].destination;
        if(visible_proposal)in.visible_proposer={in.target_group,snapshot.frame.clock,proposer->id,p6::Knowledge::exact};
        const bool owner=Cells?snapshot.frame.own.actor.pick_ordinal<2:snapshot.frame.own.actor.pick_ordinal==0;
        const bool component=common::slot_current(in,*ledger_,in.component_slot,in.component_entry)&&
            ledger_->component_qualified(in.component_slot,in.component_entry,snapshot.frame.clock.round);
        const auto organization=Earned?Organization::H:runtime_arm==common::Arm::control?Organization::J:Organization::H;
        bool can_couple=runtime_arm==common::Arm::control?(owner||visible_proposal):(owner?component:visible_proposal);
        if constexpr(Cells)can_couple&=assignments_.valid(snapshot.draft);
        can_couple&=snapshot.frame.own.actor.pick_ordinal<(Cells?5:3);
        bool coupled=false;
        if(can_couple&&room(80000)) {
            std::vector<Solo> alternatives;alternatives.reserve(Cells?5:3);alternatives.push_back(*best);normalize(alternatives.back(),snapshot.frame);
            const int participants=std::min(Cells?5:3,snapshot.draft.picks);bool complete=true;
            for(int ordinal=0;ordinal<participants;++ordinal) {
                const int id=snapshot.draft.actors[ordinal];if(id==snapshot.frame.clock.self)continue;
                const auto* actor=snapshot.frame.visible(id);if(!actor||!actor->alive||actor->channel!=p6::Channel::none||!room(65000)){complete=false;break;}
                int site=-1,bank=-1;
                for(int offset=0;offset<pairs;++offset) {
                    const int pair=(ordinal*int(routes_.banks.size())+offset)%pairs;
                    const int candidate=pair/int(routes_.banks.size()),conversion=pair%int(routes_.banks.size());
                    if(eligible(candidate,conversion,id,alternatives)){site=candidate;bank=conversion;break;}
                }
                if(site<0){complete=false;break;}
                auto peer=std::make_unique<Solo>(solo(routes_,site,bank,subject(snapshot.frame,*actor),Earned));
                if(!room(30000))return;
                if(!peer->feasible){complete=false;break;}normalize(*peer,snapshot.frame);alternatives.push_back(std::move(*peer));
            }
            if(complete&&alternatives.size()==std::size_t(participants)&&room(65000)) {
                auto baseline=std::make_unique<common::Offer>();baseline->plan=best->plan;baseline->schedule.target_id=best->plan.group;
                baseline->context_slot=offer.context_slot;baseline->entry=offer.entry;bool appended=true;
                for(const auto& alternative:alternatives) {
                    appended&=append(baseline->schedule,alternative.model.schedule,0,baseline->safety_count*64);
                    baseline->safety[baseline->safety_count++]=alternative.model.certificate;
                }
                if(appended&&queue_alternatives(*baseline,alternatives,snapshot.frame)&&room(65000)) {
                    const auto roster=organization==Organization::H?proposal_roster(snapshot,collector_ordinal):snapshot.draft;
                    auto supported=std::make_unique<Coalition>(coalition(routes_,project,project_bank,snapshot.frame,roster,alternatives,organization,Earned));
                    if(!room(30000))return;
                    if(supported->feasible) {
                        offer=*baseline;auto& proposed=in.offers[in.offer_count++];proposed=std::move(supported->offer);coupled=true;
                        if constexpr(Cells)if(organization==Organization::H&&snapshot.frame.own.actor.pick_ordinal<2) {
                            const int band=int(health_band(snapshot.own.health,snapshot.own.max_health));in.supported_slot=-1;in.supported_entry={};
                            for(int helper=3;helper<snapshot.draft.picks;++helper)if(roster.actors[2]==snapshot.draft.actors[helper])in.supported_slot=cell_supported_[band][helper-3];
                            if(const auto* state=ledger_->state(in.supported_slot))in.supported_entry=state->declaration.key;
                        }
                        proposed.context_slot=in.supported_slot;proposed.entry=in.supported_entry;
                        if constexpr(Cells) {
                            if(organization==Organization::J)in.control_route={banded_cells::joint_route_hash(proposed.plan),p6::Source::public_map};
                            else in.proposals[collector_ordinal]={{in.target_group,snapshot.frame.clock,snapshot.draft.actors[collector_ordinal],p6::Knowledge::exact},
                                {proposed.plan.ingress.count-1,p6::Source::public_map}};
                        }
                    }
                }
            }
        }
        trace.mark(8);
        if(!room(25000))return;
        if(!coupled) {
            const bool queued=queue_alternatives(offer,{best,1},snapshot.frame,trace.enabled?&trace.queue:nullptr);trace.mark(9);
            if(!queued){if(room(25000))fallback(snapshot);return;}
        } else trace.mark(9);
        trace.progress=5;
        if(!room(25000))return;
        trace.progress=51;
        common::Decision decision;int action_group=-1;bool attached=false;
        if constexpr(!Cells) {
            const bool ordinary=!coupled&&in.offer_count==1&&in.independent==0&&
                offer.plan.organization==Organization::U&&offer.plan.group==best->plan.group&&
                !offer.diagnostic_segment&&offer.marked_extra_turns==0;
            const bool earned_scope=!Earned||(!ledger_->active()&&offer.plan.crew_count==1&&
                offer.plan.collector==snapshot.frame.clock.self&&offer.plan.crew[0]==snapshot.frame.clock.self&&
                offer.schedule.actor_count==1&&offer.schedule.actors[0].actor_id==snapshot.frame.clock.self&&
                offer.safety_count==1&&offer.plan.ingress.count>=2);
            if(ordinary&&earned_scope) {
                // This identifies a read-only target, never an admitted action.
                if(trace.enabled)trace.decision_fuel[0]=cambc::fuel_remaining();
                common::refresh_evidence(in,*ledger_);
                if(trace.enabled)trace.decision_fuel[1]=cambc::fuel_remaining();
                if(!room(25000))return;
                const auto& request=own_model_->request;
                if(!best->feasible||best->model.certificate.clock!=snapshot.frame.clock||
                    request.group_count!=1||request.project_group!=best->plan.group||
                    !request.groups[0].observed.current(snapshot.frame.clock,best->plan.group)||
                    request.groups[0].observed.value.id!=best->plan.group)return;
                action_group=best->plan.group;selected_site_=best->site;
                if(!adapter_.attach_ordinary_group(snapshot,routes_.sites[selected_site_].group,
                        request.groups[0].observed,work_))return;
                attached=true;trace.progress=7;
            }
        }
        if(!attached) {
            decision=decide();trace.decision_rejection=int(decision.reason);trace.offer_rejection=int(decision.rejected[0]);
            if(decision.option<0){if(room(25000))fallback(snapshot);return;}
            trace.progress=6;
            // Preserve the full batch and its existing reserve for other menus.
            if(!room(25000))return;
            action_group=in.offers[decision.option].plan.group;
            for(int site=0;site<int(routes_.sites.size());++site)if(group_id(routes_.sites[site].group)==action_group)selected_site_=site;
            const auto before=std::make_unique<sdk::Snapshot>(snapshot);
            snapshot=adapter_.observe(&routes_.sites[selected_site_].group,&work_);
            trace.progress=7;
            if(!snapshot.valid||!room(25000)||snapshot.frame.clock.round!=before->frame.clock.round||
                snapshot.own.equipment_hash!=before->own.equipment_hash||snapshot.own.health!=before->own.health||snapshot.own.max_health!=before->own.max_health||
                snapshot.own.strength!=before->own.strength||snapshot.own.focus!=before->own.focus||snapshot.own.growth!=before->own.growth||
                snapshot.own.bag!=before->own.bag||snapshot.frame.own.gold!=before->frame.own.gold||snapshot.frame.own.actor.position!=before->frame.own.actor.position)return;
            for(int option=0;option<in.offer_count;++option)for(int actor=0;actor<in.offers[option].schedule.actor_count;++actor) {
                const int id=in.offers[option].schedule.actors[actor].actor_id;const auto* old=before->frame.visible(id);const auto* now=snapshot.frame.visible(id);
                if(!old||!now||old->alive!=now->alive||old->health!=now->health||old->max_health!=now->max_health||old->strength!=now->strength||
                    old->bag!=now->bag||old->position!=now->position||old->channel!=now->channel)return;
            }
        }
        // Settings/windows are immutable within this revalidated owner turn;
        // avoid repeating their full queried identity construction.
        in.frame=snapshot.frame;in.draft=snapshot.draft;in.picks=snapshot.draft.picks;
        in.own_state={snapshot.own,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        // Attachment preserves the exact same owner turn and group whose
        // personally queried threat regime was bound before the offer.
        if(!attached)in.threat_regime={int(threats(action_group)&0x7fffffff),snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
        if constexpr(Earned)in.spawn_shop_legal=snapshot.spawn_shop_legal;
        if(!attached) {
            for(int option=0;option<in.offer_count;++option)for(int certificate=0;certificate<in.offers[option].safety_count;++certificate)
                in.offers[option].safety[certificate].clock=snapshot.frame.clock;
            if(in.visible_proposer.knowledge==p6::Knowledge::exact)in.visible_proposer.clock=snapshot.frame.clock;
            if constexpr(Cells)for(auto& proposal:in.proposals)if(proposal.named_group.knowledge==p6::Knowledge::exact)proposal.named_group.clock=snapshot.frame.clock;
        }
        if(!room(25000))return;
        if constexpr(!Cells) {
            const bool wait_covered=attached&&[&] {
                if constexpr(Earned)return earned_center::spawn_return_wait_covered(snapshot.frame,offer.plan,
                    own_model_->request,best->model,routes_.spawn.destination);
                else return admission_wait_covered(snapshot.frame,offer.plan,own_model_->request,best->model);
            }();
            if(wait_covered) {
                auto& admission_frame=admission_frame_;
                admission_frame.current_round=-1;admission_frame.fuel_resumed=true;
                admission_frame.admission_only=true;admission_frame.safety_count=0;
                admission_frame.legal.knowledge=p6::Knowledge::unknown;
                if(trace.enabled)trace.decision_fuel[2]=cambc::fuel_remaining();
                const auto observe_admission=[&](const common::Decision& selected)->const LocalFrame& {
                        if(trace.enabled)trace.decision_fuel[4]=cambc::fuel_remaining();
                        trace.decision_rejection=int(selected.reason);trace.offer_rejection=int(selected.rejected[0]);
                        if(selected.option!=0)return admission_frame;
                        adapter_.admission_local(snapshot,offer.plan,
                            {offer.safety.data(),std::size_t(offer.safety_count)},work_,admission_frame);
                        if(trace.enabled)trace.admission_fuel[0]=cambc::fuel_remaining();
                        return admission_frame;
                    };
                const auto admission=[&] {
                    if constexpr(Earned)return earned_center::admit_current(in,*ledger_,probes_,runtime_arm,work_,observe_admission);
                    else return canary::admit_current(in,*ledger_,probes_,runtime_arm,work_,observe_admission);
                }();
                if(trace.enabled)trace.admission_fuel[1]=cambc::fuel_remaining();
                trace.progress=10;
                trace.admission_started=admission.started;
                const auto abandon=[&](PhysicalFailure reason) {
                    admission_wait_pending_=false;
                    work_.invalidate(reason);
                    if(ledger_->active())ledger_->invalidate_active(int(cambc::round_number()));
                    trace_.finish(*ledger_,int(cambc::round_number()),{});
                };
                if(!admission.started){if(room(25000))fallback(snapshot);return;}
                if(!room(2000)){abandon(PhysicalFailure::fuel);return;}
                // SDK admission_local accepts exactly one sole-U certificate.
                // Retain only that active payload; old tails stay behind count.
                if(offer.safety_count!=1){abandon(PhysicalFailure::stale);return;}
                initial_safety_[0]=offer.safety[0];initial_safety_count_=1;threat_=target_threat;
                renewal_site_=selected_site_;unavailable_logged_=false;inspection_turns_=0;inspection_site_=-1;
                if(admission.episode&&!trace_.begin(*ledger_,work_)){abandon(PhysicalFailure::stale);return;}
                if(trace.enabled)trace.admission_fuel[3]=cambc::fuel_remaining();
                // This reserve covers only the clock debit, entry snapshot and
                // turn epilogue. No SDK action is allowed from this frame.
                if(!room(1000)){abandon(PhysicalFailure::fuel);return;}
                if(!work_.consume_admission_turn(admission_frame)){abandon(PhysicalFailure::stale);return;}
                if(trace.enabled)trace.admission_fuel[2]=cambc::fuel_remaining();
                admission_entry_=snapshot.own;admission_wait_pending_=true;
                // No command or receipt belongs to this charged admission turn.
                // The next consecutive turn obtains full fresh SDK legality.
                return;
            }
        }
        if constexpr(!Earned&&!Cells) {
            if(trace.enabled)trace.decision_fuel[2]=cambc::fuel_remaining();
            common::refresh_evidence(in,*ledger_);
            if(trace.enabled)trace.decision_fuel[3]=cambc::fuel_remaining();
            decision=canary::choose(in,*ledger_,probes_,runtime_arm);
            if(trace.enabled)trace.decision_fuel[4]=cambc::fuel_remaining();
        } else decision=decide();
        trace.decision_rejection=int(decision.reason);trace.offer_rejection=int(decision.rejected[0]);
        if(decision.option<0){if(attached&&room(25000))fallback(snapshot);return;}
        if(in.offers[decision.option].plan.group!=action_group)return;
        trace.progress=8;
        // Local action preparation spends fuel before the existing commit
        // reserve gate. A lower remainder cannot pass that gate this turn.
        if(!room())return;
        const auto& chosen=in.offers[decision.option];
        auto local=adapter_.local(snapshot,chosen.plan,{chosen.safety.data(),std::size_t(chosen.safety_count)},true,calm(snapshot),true);
        trace.progress=9;
        if(!room())return;
        local.fuel_remaining=std::int64_t(cambc::fuel_remaining());
        const auto admission=commit(local,decision);
        trace.progress=10;
        if(!admission.started){if(room(25000))fallback(snapshot);return;}
        initial_safety_=chosen.safety;initial_safety_count_=chosen.safety_count;threat_=threats(work_.plan.group);
        renewal_site_=work_.plan.organization==Organization::H&&work_.plan.collector!=snapshot.frame.clock.self?best->site:selected_site_;
        unavailable_logged_=false;inspection_turns_=0;inspection_site_=-1;
        if(admission.episode&&!trace_.begin(*ledger_,work_)){ledger_->invalidate_active(snapshot.frame.clock.round);work_.request_stop();}
        if(!room(18000))return;
        std::cout<<"p8 ADMIT group="<<work_.plan.group<<" organization="<<int(work_.plan.organization)<<" episode="<<admission.episode<<'\n';
        if(!room(17000))return;
        local.fuel_remaining=std::int64_t(cambc::fuel_remaining());
        const auto command=work_.step(local);if(!room(16000))return;
        dispatch(command,local);if(room(16000))trace_.observe(local,work_,snapshot.spawn_shop_legal);
    }
};

template<bool Earned,bool Cells=false> void draft() {
    const auto selected=cambc::draft::current_draft(cambc::team::ally);
    const int ordinal=int(selected.size()),picks=int(cambc::draft::team_size());
    if (ordinal>=picks) return;
    common::DraftInputs in;in.ordinal=ordinal;in.picks=picks;in.prospective_actor=0;
    for (int i=0;i<ordinal&&i<5;++i)in.own_picks[i]=sdk::type(selected[i]);
    in.settings=sdk::settings();in.settings_identity={sdk::settings_identity(),p6::Source::queried_setting};
    const int window=std::min(common::forecast_window,in.settings.max_rounds);
    in.windows.deadlines=in.windows.windows={window,window,window};
    in.windows.sampled_actor_id=0;in.windows.sampled_round=0;in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto candidates=draft_candidates(0,ordinal,Earned);
    if (!cambc::is_draft() || int(cambc::draft::current_draft(cambc::team::ally).size())!=ordinal) return;
    common::DraftDecision decision;
    if (cambc::fuel_remaining()>30000) {
        if constexpr(Cells)decision=banded_cells::select_draft(in,candidates);
        else if constexpr(Earned)decision=earned_center::select_draft(in,candidates);
        else decision=common::select_draft(in,candidates);
    }
    if (decision.candidate>=0) {cambc::draft::pick(sdk::draft_type(decision.champion));return;}
    // A draft pick is mandatory even if the bounded static model cannot certify
    // an opening. This fallback is explicitly unavailable, never scored as safe.
    const auto& priority=Earned?earned_center::draft_priority(ordinal):common::champion_pool;
    for (const auto type:priority) {
        const auto pick=sdk::draft_type(type);
        if (std::find(selected.begin(),selected.end(),pick)==selected.end()) {cambc::draft::pick(pick);return;}
    }
}
template<bool Earned,bool Cells=false> int run() {
    std::unique_ptr<Runtime<Earned,Cells>> bot;
    while (true) {
        try {
            if (cambc::is_draft())draft<Earned,Cells>();else {
                if (!bot) {
                    // Members own their initialization. Value-initializing this
                    // aggregate adds a redundant whole-object leading zero-fill.
                    bot.reset(new Runtime<Earned,Cells>);
                    if(cambc::fuel_remaining()>=32000)
                        std::printf("p8 DIAG startup phase=0 actor=%d r=%d fuel=%llu\n",sdk::identity(cambc::self()),
                            int(cambc::round_number()),static_cast<unsigned long long>(cambc::fuel_remaining()));
                }
                bot->turn();
            }
        } catch (const cambc::error& error) {
            std::cout<<"p8 ERROR engine "<<error.what()<<'\n';
        } catch (const std::exception& error) {
            std::cout<<"p8 ERROR exception "<<error.what()<<'\n';
        } catch (...) {std::cout<<"p8 ERROR unknown\n";}
        cambc::end_turn();
    }
}
} // namespace p8::native
