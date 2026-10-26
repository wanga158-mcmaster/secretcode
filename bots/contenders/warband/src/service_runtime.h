#pragma once

#include <cambc.h>
#include "config.h"
#include "service.h"
#include "service_world.h"
#include "service_native.h"
#include "service_ready_field.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <iostream>
#include <vector>

namespace wb {

// First complete operator family: single-target basic production at harmless
// groups, with strict static quiet geometry and a prepared full walking suffix.
// This is deliberately not a universal camp/PvP controller.
class ServiceRuntime {
    enum class Mode { Cold, World, Nominate, Prepare, Execute, Unsupported };
    Mode mode_ = Mode::Cold;
    service_native::Adapter native_;
    service_native::Move selected_move_;
    ServiceWorld world_;
    route::Grid planning_grid_;
    route::Planner planner_;
    service::Interpreter service_;
    ready_field::Endpoint ready_endpoint_;
    service::Contract sealed_contract_;
    service::Authority last_attempted_;
    std::uint64_t service_episode_ = 0, sealed_bank_baseline_ = 0;
    std::uint64_t endpoint_episode_ = 0, endpoint_capture_ = 0, endpoint_bank_seen_ = 0;
    int endpoint_bank_round_ = -1;
    std::vector<route::Segment> itinerary_;
    std::vector<std::size_t> order_;
    std::vector<unsigned> retry_after_;
    std::vector<unsigned> failed_words_;
    std::vector<unsigned> observed_lifecycle_;
    std::vector<bool> rejected_;
    std::size_t group_index_ = 0, walk_segment_ = 0, walk_cell_ = 0;
    unsigned ordinal_ = 0, ally_count_ = 1, rotation_ = 0;
    std::uint64_t route_progress_ = 0, receipt_serial_ = 0, bank_serial_ = 0;
    service::Receipt receipt_;
    service::OwnBankReceipt bank_receipt_;
    service::Frame previous_;
    bool previous_valid_ = false, shop_done_ = false;
    bool residual_ = false;
    unsigned residual_replans_ = 0;
    int shop_gold_ = 0;
    float shop_strength_ = 0;
    service::Status trace_status_ = service::Status::NoCommand;
    service::Reason trace_reason_ = service::Reason::None;
    service::Authority trace_authority_;
    std::uint64_t trace_fuel_ = 0;
    std::array<std::int64_t,12> trace_packet_{},trace_last_{};
    bool trace_pending_=false,trace_seen_=false;
    struct BankTrace {
        bool valid = false;
        int round = -1, remaining = -1, bag = 0;
        unsigned team_score = 0;
        service::Position position{};
    } bank_trace_;

    static int owner_id() { return static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self())); }
    static service::Position pos(cambc::vec2 p) { return {p.x,p.y}; }
    static cambc::vec2 pos(service::Position p) { return {p.x,p.y}; }
    int tile(service::Position p) const { return world_.tile(pos(p)); }
    static bool alive(const service_native::Self& self) { return self.state.contains(cambc::champion_state::alive); }
    static bool channelled(const service_native::Self& self) {
        return self.state.contains(cambc::champion_state::recalling) ||
            self.state.contains(cambc::champion_state::submitting_gems) ||
            self.state.contains(cambc::champion_state::teleporting);
    }
    bool field_quiet(const service_native::Self& self) const {
        return self.raw.x >= 0 && self.raw.y >= 0 && world_.quiet(world_.tile({self.raw.x,self.raw.y}));
    }

    // The same current metadata filter used by Nominate. This does not prove a
    // future route/service: it prevents releasing into a known empty entry.
    bool has_nomination_candidate(const service::Authority& auth) const {
        for(std::size_t i=0;i<world_.groups.size();++i)
            if(!rejected_[i] && failed_words_[i]<3 && retry_after_[i]<=static_cast<unsigned>(auth.round)
                && world_.harmless(world_.groups[i])) return true;
        return false;
    }

    void invalidate_endpoint(const service::Authority& auth,
                             ready_field::Reason reason=ready_field::Reason::EpisodeInvalidated) {
        if constexpr(config::observed_ready_terminal) (void)ready_endpoint_.invalidate(auth,reason);
        endpoint_episode_=endpoint_capture_=0;endpoint_bank_round_=-1;
    }

    ready_field::Facts readiness_facts(const service_native::Self& self, bool resources) const {
        ready_field::Facts facts;
        facts.authority=self.authority;
        facts.self_known=facts.life_known=facts.health_known=facts.position_known=self.observed;
        facts.bag_known=facts.gold_known=facts.channel_known=facts.stun_known=self.observed;
        facts.stunned=self.stunned;facts.permissions_known={self.observed,self.observed,false};
        facts.restrictions_known_mask=63;
        const std::array restrictions{cambc::buff_restrictions::move,cambc::buff_restrictions::attack,
            cambc::buff_restrictions::cast,cambc::buff_restrictions::shop,
            cambc::buff_restrictions::consumable,cambc::buff_restrictions::ward};
        for(unsigned i=0;i<restrictions.size();++i)
            if(self.restrictions.contains(restrictions[i])) facts.restrictions|=std::uint8_t(1u<<i);
        facts.input_unused_known=true;
        facts.input_unused=!service_micro::same_input(last_attempted_,self.authority);
        facts.episode_continuity_known=true;
        facts.same_episode=endpoint_episode_==service_episode_ && endpoint_capture_==receipt_.serial;
        facts.field={self.authority,true,field_quiet(self),{self.raw.x,self.raw.y}};
        if(resources) {
            // Complete own inventory, not an estimate or a private peer query.
            constexpr auto item_count=static_cast<unsigned>(cambc::item_type::count);
            constexpr auto consumable_count=static_cast<unsigned>(cambc::consumable_type::count);
            static_assert(item_count+consumable_count<=64);
            std::array<std::uint32_t,item_count> items{};
            std::array<std::uint32_t,4> consumables{};
            cambc::detail::check(cambc::detail::sdk_champion_items(self.authority.owner,items.data()));
            cambc::detail::check(cambc::detail::sdk_champion_consumables(self.authority.owner,consumables.data()));
            auto& inventory=facts.inventory;
            inventory.authority=self.authority;inventory.known=inventory.complete=true;
            for(unsigned i=0;i<items.size();++i) if(items[i])
                inventory.slots[inventory.count++]={ready_field::InventoryKind::Item,static_cast<int>(i),items[i]};
            for(unsigned i=0;i<consumable_count;++i) if(consumables[i])
                inventory.slots[inventory.count++]={ready_field::InventoryKind::Consumable,static_cast<int>(i),consumables[i]};
        }
        return facts;
    }

    bool observed_terminal(const service_native::Self& self,const service::Frame& frame) {
        if constexpr(!config::observed_ready_terminal) return false;
        const auto auth=frame.authority;
        ready_field::OwnBankEvidence proof;
        const ready_field::OwnBankEvidence* supplied=nullptr;
        if(bank_receipt_.present && bank_receipt_.verified && bank_receipt_.serial>sealed_bank_baseline_
            && bank_receipt_.serial>endpoint_bank_seen_ && receipt_.present && receipt_.verified
            && receipt_.gems==bank_receipt_.gems && self.bag==0) {
            // Bind once to the current actual capture and our actor-joined bank
            // trace. Failure/deadline never rearms this bank under a new epoch.
            endpoint_bank_seen_=bank_receipt_.serial;
            ready_field::Episode episode;
            episode.id=service_episode_;episode.sealed=sealed_contract_;episode.capture=receipt_;
            episode.bank_serial_baseline=sealed_bank_baseline_;episode.opened_at=auth;
            episode.terminal=ready_field::Terminal::ReadyFieldHoldCash;
            episode.deadline_round=auth.round+32;episode.max_observation_calls=32;
            if(ready_endpoint_.seal(auth,episode)) {
                endpoint_episode_=service_episode_;endpoint_capture_=receipt_.serial;endpoint_bank_round_=auth.round;
                proof={auth,auth.owner,sealed_contract_.group,service_episode_,receipt_.serial,
                    ready_field::ProofSource::ActorJoinedOwnCompletion,bank_receipt_};
                supplied=&proof;
            }
        }
        if(endpoint_episode_!=service_episode_ || endpoint_bank_round_<0) return false;
        // Receipt input latches proof only; its existing return primitive is
        // retained. Inventory is needed only on a later candidate-ready input.
        const bool later=auth.round>endpoint_bank_round_;
        const bool ready_body=frame.alive && frame.hp==frame.max_hp && frame.can_move && frame.can_attack
            && !frame.restricted && frame.channel==service::Channel::None && frame.bag==0;
        const auto facts=readiness_facts(self,later && ready_body);
        if(!native_.current(auth)) return false;
        const auto decision=ready_endpoint_.observe_readiness(auth,frame,facts,ready_field::bare_move_basic(),supplied);
        if(decision.status!=ready_field::Status::ObservedReadyForBasic) return false;
        ready_field::NominationEntry entry;
        entry.authority=auth;entry.position=frame.position;
        entry.world_known=true;entry.world_ready=world_.state()==ServiceWorld::State::Ready;
        entry.position_known=self.observed;entry.static_quiet_known=true;entry.static_npc_quiet=field_quiet(self);
        entry.candidate_known=true;entry.candidate_present=entry.world_ready && has_nomination_candidate(auth);
        entry.bag_known=true;entry.bag=frame.bag;entry.input_unused_known=true;entry.input_unused=facts.input_unused;
        if(!ready_field::nomination_entry_allowed(decision,auth,entry)) {
            (void)ready_endpoint_.continue_existing(auth,decision.ticket);return false;
        }
        if(ready_endpoint_.release(auth,decision.ticket).status!=ready_field::Status::Released) return false;
        // This is a distinct observed HoldCash endpoint, not legacy Complete,
        // required purchase, spawn return, or a future-pressure certificate.
        // Only the old interpreter is retired. The native one-input gateway,
        // semantic failure budgets and all receipt/episode serials survive.
        service_=service::Interpreter{};
        previous_valid_=false;bank_trace_={};receipt_={};
        itinerary_.clear();order_.clear();walk_segment_=walk_cell_=0;route_progress_=0;
        shop_done_=false;shop_gold_=0;shop_strength_=0;residual_=false;residual_replans_=0;
        ++rotation_;mode_=Mode::Nominate;
        nominate(self,auth); // current quiet metadata entry, no primitive
        return true;
    }

    void nominate(const service_native::Self& self, const service::Authority& auth) {
        if (!field_quiet(self) || self.bag != 0) {mode_ = Mode::Unsupported;return;}
        struct Candidate { std::size_t index; int distance; };
        std::vector<Candidate> candidates;
        const cambc::vec2 current{self.raw.x,self.raw.y};
        for (std::size_t i = 0; i < world_.groups.size(); ++i) {
            const auto& group = world_.groups[i];
            if (rejected_[i] || failed_words_[i]>=3 || retry_after_[i] > static_cast<unsigned>(auth.round) || !world_.harmless(group)) continue;
            int distance = std::numeric_limits<int>::max();
            for (const auto& member : group.camps())
                distance = std::min(distance, static_cast<int>(current.chebyshev_distance(member.spawn_position())));
            candidates.push_back({i,distance});
        }
        if (candidates.empty()) {
            bool timed_resolution=false;
            for(std::size_t i=0;i<retry_after_.size();++i)
                if(!rejected_[i] && retry_after_[i]>static_cast<unsigned>(auth.round)) timed_resolution=true;
            if(!timed_resolution) mode_=Mode::Unsupported;
            return; // finite known respawn wait or explicit unsupported class
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a,const auto& b){return a.distance<b.distance;});
        group_index_ = candidates[(ordinal_ + rotation_) % candidates.size()].index;
        const auto& group = world_.groups[group_index_];
        planning_grid_ = world_.grid;
        std::array<route::Member,30> members{};
        for (std::size_t i = 0; i < group.camps().size(); ++i) {
            const auto& member = group.camps()[i];
            const int at = world_.tile(member.spawn_position());
            if (world_.permanent[at]) {rejected_[group_index_]=true;return;}
            planning_grid_.blocked[at] = 0;
            members[i] = {static_cast<int>(static_cast<cambc::handle_camp_t>(member)),at,true};
        }
        // All-alive is conservative PLANNING occupancy, not a fabricated live
        // observation. Every basic and clear uses a new native selected frame.
        if (!planner_.begin(planning_grid_,world_.tile(current),
            std::span<const route::Member>{members.data(),group.camps().size()})) {
            rejected_[group_index_] = true;return;
        }
        residual_=false;residual_replans_=0;
        mode_ = Mode::Prepare;
    }

    bool residual_suffix(const service_native::Self& self) {
        if (!field_quiet(self) || !receipt_.present || !receipt_.verified || self.bag < receipt_.gems ||
            ++residual_replans_ > 3) {mode_=Mode::Unsupported;return false;}
        planning_grid_=world_.grid;
        // This quiet preparation packet excludes actual visible blockers.
        // It is planning occupancy only; every eventual endpoint is re-observed.
        for(const auto at:cambc::vision::champion_tiles().positions())
            planning_grid_.blocked[world_.tile(at)]=1;
        if(!planner_.begin_suffix(planning_grid_,world_.tile({self.raw.x,self.raw.y}))) {
            mode_=Mode::Unsupported;return false;
        }
        residual_=true;mode_=Mode::Prepare;return true;
    }

    void seal(const service_native::Self& self, const service::Authority& auth) {
        itinerary_ = planner_.result();
        order_.clear();
        service::Contract contract;
        contract.owner = auth.owner;
        contract.group = static_cast<int>(static_cast<cambc::handle_camp_group_t>(world_.groups[group_index_]));
        contract.start_from_receipt=residual_;
        if(residual_) contract.carried_receipt=receipt_;
        for (auto& segment : itinerary_) if (segment.role == route::Role::WorkMember) {
            const auto original = static_cast<std::size_t>(segment.member_index);
            order_.push_back(original);
            segment.member_index = static_cast<int>(contract.member_count);
            contract.member_ids[contract.member_count++] = static_cast<int>(
                static_cast<cambc::handle_camp_t>(world_.groups[group_index_].camps()[original]));
        }
        contract.capture_wait_bound = static_cast<unsigned>(cambc::constants::capture_time) + 3;
        contract.channel_wait_bound = 32;
        contract.max_deferrals = 4;
        contract.max_refusals = 2;
        // Ordinary cofarm is not confiscated: actual recipients share natively.
        // Require one funded shard only when even the maximum actual ally split
        // plus our real wallet funds it. No peer wallet is queried or invented.
        const auto shard = cambc::item_type::power_shard_of_strength;
        contract.require_shop = !config::observed_ready_terminal &&
            self.gold + world_.groups[group_index_].gold_value() / ally_count_ >= shard.cost();
        contract.return_by_recall = false;
        if (!service_.begin(contract)) {rejected_[group_index_]=true;mode_=Mode::Nominate;return;}
        invalidate_endpoint(auth);
        sealed_contract_=contract;sealed_bank_baseline_=bank_serial_;++service_episode_;
        walk_segment_ = walk_cell_ = 0; route_progress_ = 0;
        previous_valid_ = false; shop_done_ = false; shop_gold_=0;shop_strength_=0;bank_trace_ = {};
        mode_ = Mode::Execute;
    }

    std::size_t desired_segment(service::RouteRole role, std::size_t member) const {
        for (std::size_t i = 0; i < itinerary_.size(); ++i) {
            const auto& segment = itinerary_[i];
            if ((role == service::RouteRole::WorkMember && segment.role == route::Role::WorkMember &&
                    segment.member_index == static_cast<int>(member)) ||
                (role == service::RouteRole::Capture && segment.role == route::Role::Capture) ||
                (role == service::RouteRole::Bank && segment.role == route::Role::Bank) ||
                (role == service::RouteRole::Spawn && segment.role == route::Role::Spawn)) return i;
        }
        return itinerary_.size();
    }

    service::Route route_frame(const service_native::Self& self, service::RouteRole role, std::size_t member) {
        service::Route result;
        result.role = role; result.member_index = member; result.quiet = field_quiet(self);
        const auto goal = desired_segment(role,member);
        if (goal == itinerary_.size()) {result.invalidated=true;return result;}
        const int actual = world_.tile({self.raw.x,self.raw.y});
        // Keep finite unvisited route prefixes when another actor kills a local
        // member early. A target death does not discard the rest of the service.
        while (walk_segment_ <= goal) {
            const auto& segment = itinerary_[walk_segment_];
            if (walk_cell_ == segment.path.size()) {++walk_segment_;walk_cell_=0;++route_progress_;continue;}
            if (actual == segment.path[walk_cell_]) {++walk_cell_;++route_progress_;continue;}
            const auto next = world_.position(segment.path[walk_cell_]);
            const cambc::vec2 here{self.raw.x,self.raw.y};
            if (here.chebyshev_distance(next) != 1) {result.invalidated=true;break;}
            result.next = pos(next); result.next_step_available = true;
            // This is permission to ATTEMPT an adjacent reachable landing.
            // A hidden landing remains MayRefuse, never KnownFree. The strict
            // quiet origin prices one physical refused input in this first
            // NPC/no-foe class; no complete PvP response envelope is implied.
            const bool refusal_prefix=field_quiet(self) && world_.quiet(segment.path[walk_cell_]);
            selected_move_=native_.observe_move(self.authority,result.next,refusal_prefix);
            result.next_step_legal=selected_move_.observed && selected_move_.legal;
            break;
        }
        result.goal_reached = actual == itinerary_[goal].goal;
        result.progress_counter = route_progress_;
        return result;
    }

    void update_bank_trace(const service_native::Self& self, unsigned team_score) {
        const service::Position actual{self.raw.x,self.raw.y};
        if (bank_trace_.valid && self.authority.round == bank_trace_.round + 1 &&
            bank_trace_.remaining == 1 && bank_trace_.bag > 0 && alive(self) &&
            !self.state.contains(cambc::champion_state::submitting_gems) && self.bag == 0 &&
            actual == bank_trace_.position && team_score >= bank_trace_.team_score + static_cast<unsigned>(bank_trace_.bag)) {
            // Consecutive owner inputs exclude a 60-tick death/respawn. Under
            // the public channel rules, cancellation retains the bag; actual
            // disappearance here plus score rise is an own completion receipt.
            bank_receipt_ = {true,true,bank_trace_.bag,++bank_serial_};
        }
        if (self.state.contains(cambc::champion_state::submitting_gems))
            bank_trace_ = {true,self.authority.round,self.channel_remaining,self.bag,team_score,actual};
        else bank_trace_.valid = false;
    }

    service::Frame capture(service_native::Self self) {
        service::Frame frame;
        frame.authority = self.authority; frame.fresh = self.observed; frame.alive = alive(self);
        frame.position = {self.raw.x,self.raw.y}; frame.hp=self.raw.health;frame.max_hp=self.raw.max_health;
        frame.attack_range=self.raw.attack_range;
        frame.can_move=self.state.contains(cambc::champion_state::can_move) && !self.restrictions.contains(cambc::buff_restrictions::move);
        frame.can_attack=self.state.contains(cambc::champion_state::can_attack) && !self.restrictions.contains(cambc::buff_restrictions::attack);
        frame.can_cast=self.state.contains(cambc::champion_state::can_cast) && !self.restrictions.contains(cambc::buff_restrictions::cast);
        frame.restricted=self.stunned;
        frame.channel=self.state.contains(cambc::champion_state::submitting_gems) ? service::Channel::Tribute :
            self.state.contains(cambc::champion_state::recalling) ? service::Channel::Recall :
            self.state.contains(cambc::champion_state::teleporting) ? service::Channel::Other : service::Channel::None;
        frame.channel_remaining=self.channel_remaining;
        frame.bag=self.bag;frame.gold=self.gold;frame.at_spawn=self.at_spawn;
        const auto& group=world_.groups[group_index_];
        frame.domain_harmless=world_.harmless(group);frame.static_hazard_excluded=field_quiet(self);
        bool group_visible=false;
        for(std::size_t i=0;i<order_.size();++i) {
            const auto& member=group.camps()[order_[i]];
            auto& out=frame.members[i];
            out.id=static_cast<int>(static_cast<cambc::handle_camp_t>(member));
            out.position=pos(member.spawn_position());out.harmless=true;
            const auto actual=native_.observe_camp(self.authority,member);
            if(actual.observed && actual.visible) {
                out.known=out.visible=true;out.alive=actual.alive;
                if(actual.live_info) {out.hp=actual.raw.health;out.position={actual.raw.x,actual.raw.y};}
            }
            if(cambc::vision::in_vision(member.spawn_position())) group_visible=true;
            if(frame.position==pos(member.spawn_position())) frame.capture_occupied=true;
            if(cambc::vision::has_champion(member.spawn_position())) {
                const auto body=cambc::vision::champion_at(member.spawn_position());
                if(body.is_enemy()) frame.capture_contested=true;
            }
        }
        if(group_visible) {
            unsigned state=0;
            cambc::detail::check(cambc::detail::sdk_camp_group_state(
                static_cast<unsigned>(static_cast<cambc::handle_camp_group_t>(group)),&state));
            frame.group_fresh=state!=0;
            frame.group_state=state==1 ? service::GroupState::Alive : state==2 ? service::GroupState::RewardAvailable :
                state==3 ? service::GroupState::Captured : service::GroupState::Unknown;
            if(observed_lifecycle_[group_index_]==3 && state==1) {
                failed_words_[group_index_]=0;rejected_[group_index_]=false;
            }
            if(state) observed_lifecycle_[group_index_]=state;
            if(state==3) {
                std::int32_t remaining=0;
                cambc::detail::check(cambc::detail::sdk_camp_group_cooldown(
                    static_cast<unsigned>(static_cast<cambc::handle_camp_group_t>(group)),&remaining));
                if(remaining>0) retry_after_[group_index_]=static_cast<unsigned>(frame.authority.round+remaining);
            }
        }
        const unsigned score=cambc::global_submitted_gems(cambc::team::ally);
        update_bank_trace(self,score);
        if(previous_valid_ && previous_.authority.round+1==frame.authority.round &&
            previous_.group_fresh && previous_.group_state==service::GroupState::RewardAvailable &&
            previous_.capture_occupied && frame.capture_occupied &&
            frame.group_fresh && frame.group_state==service::GroupState::Captured &&
            previous_.bag==0 && frame.bag>0 && frame.gold>=previous_.gold) {
            // This basic-only service never attacks champions or casts: it has
            // no own kill/other-group source in the consecutive receipt interval.
            receipt_={true,true,static_cast<int>(static_cast<cambc::handle_camp_group_t>(group)),frame.bag,++receipt_serial_};
            failed_words_[group_index_]=0;
            retry_after_[group_index_]=static_cast<unsigned>(frame.authority.round+group.max_respawn_timer());
        }
        frame.receipt=receipt_;frame.own_bank_receipt=bank_receipt_;
        frame.route=route_frame(self,service_.route_role(),service_.member_index());
        for(const int at:world_.altars) {
            const auto altar=world_.position(at);
            if(cambc::vec2{self.raw.x,self.raw.y}.chebyshev_distance(altar)<=cambc::constants::submit_gems_range &&
                !cambc::vision::has_altar_lock(altar)) frame.submit_ready=true;
        }
        if(shop_gold_>0 && self.gold<shop_gold_ && self.raw.strength>shop_strength_) shop_done_=true;
        frame.shop_complete=shop_done_;
        if(service_.phase()==service::Phase::Shop && self.at_spawn)
            frame.shop_ready=cambc::self().can_buy(cambc::item_type::power_shard_of_strength,1);
        frame.renewal_ready=frame.can_move && frame.can_attack && frame.can_cast && !channelled(self);
        return frame;
    }

    void execute(service_native::Self self) {
        // At most three bounded phase/route rebindings, then one primitive.
        // No global nomination/observation/geometry work precedes this service.
        for(unsigned binding=0;binding<3;++binding) {
            auto frame=capture(self);
            if(!native_.current(frame.authority)) return;
            if(observed_terminal(self,frame)) return;
            if(!native_.current(frame.authority)) return;
            const auto step=service_.step(frame);
            if constexpr(config::trace) {trace_status_=step.status;trace_reason_=step.reason;}
            previous_=frame;previous_valid_=true;
            if(step.status==service::Status::Complete) {
                invalidate_endpoint(frame.authority);
                ++rotation_;residual_=false;residual_replans_=0;
                mode_=Mode::Nominate;return;
            }
            if(step.status==service::Status::Aborted || step.status==service::Status::Unsupported) {
                invalidate_endpoint(frame.authority);
                // Re-admit only the actual residual suffix. A failed sufficient
                // route certificate is not physical impossibility of the job.
                if(self.bag) {residual_suffix(self);return;}
                // A fresh epoch/route does not erase the unchanged semantic
                // service's failure budget. Only actual damage/death, receipt
                // or a freshly observed new native lifecycle rearms it.
                if(++failed_words_[group_index_]>=3) rejected_[group_index_]=true;
                retry_after_[group_index_]=std::max(retry_after_[group_index_],static_cast<unsigned>(frame.authority.round+1));
                ++rotation_;mode_=Mode::Nominate;return;
            }
            if(step.status==service::Status::NeedObservation &&
                (frame.route.role!=service_.route_role() || frame.route.member_index!=service_.member_index())) {
                auto newer=native_.start_input();self=native_.observe_self(newer);continue;
            }
            if(step.status!=service::Status::Issued) return;
            if(step.op==service::Op::Move || step.op==service::Op::Basic) {
                service_micro::Frame micro;
                service_micro::Request request;
                const cambc::camp* target=nullptr;
                if(step.op==service::Op::Basic) {
                    const auto& m=frame.members[step.member_index];
                    target=&world_.groups[group_index_].camps()[order_[step.member_index]];
                    const auto selected=native_.observe_camp(frame.authority,*target);
                    micro=service_native::make_frame(self,&selected,nullptr,true,
                        frame.domain_harmless && frame.static_hazard_excluded);
                    request={service_micro::Kind::Basic,m.id,{}};
                } else {
                    if(selected_move_.authority!=frame.authority || selected_move_.endpoint!=step.position) {
                        service_.complete(frame.authority,{});return;
                    }
                    micro=service_native::make_frame(self,nullptr,&selected_move_,true,
                        frame.domain_harmless && frame.static_hazard_excluded);
                    request={service_micro::Kind::Move,-1,step.position};
                }
                if constexpr(config::observed_ready_terminal) last_attempted_=frame.authority;
                const auto result=native_.dispatch(frame.authority,micro,request,target);
                if(result.effect==service_micro::Effect::Damaged || result.effect==service_micro::Effect::TargetDied)
                    failed_words_[group_index_]=0;
                service_.complete(frame.authority,{result.command_accepted ? service::Acceptance::Accepted :
                    result.effect==service_micro::Effect::Refused ? service::Acceptance::Refused : service::Acceptance::Invalidated});
                return;
            }
            if(!native_.current(frame.authority)) {service_.complete(frame.authority,{});return;}
            if(step.op==service::Op::Submit) {
                bool submitted=false;
                for(const int at:world_.altars) {
                    const auto altar=world_.position(at);
                    if(pos(frame.position).chebyshev_distance(altar)>cambc::constants::submit_gems_range ||
                        cambc::vision::has_altar_lock(altar)) continue;
                    if(!native_.current(frame.authority)) break;
                    if constexpr(config::observed_ready_terminal) last_attempted_=frame.authority;
                    cambc::self().submit_gems(altar);submitted=true;break;
                }
                service_.complete(frame.authority,{submitted?service::Acceptance::Accepted:service::Acceptance::Invalidated});
                if(submitted) {
                    const auto after=native_.observe_self(native_.start_input());
                    update_bank_trace(after,cambc::global_submitted_gems(cambc::team::ally));
                }
            } else if(step.op==service::Op::Shop) {
                const auto item=cambc::item_type::power_shard_of_strength;
                const bool ready=cambc::self().can_buy(item,1);
                if(!ready || !native_.current(frame.authority)) {service_.complete(frame.authority,{});return;}
                shop_gold_=self.gold;shop_strength_=self.raw.strength;
                if constexpr(config::observed_ready_terminal) last_attempted_=frame.authority;
                cambc::self().buy(item,1);
                service_.complete(frame.authority,{service::Acceptance::Accepted});
            } else service_.complete(frame.authority,{});
            return;
        }
    }

    void play_impl() {
        const auto auth=native_.start_input();
        const auto self=native_.observe_self(auth);
        if constexpr(config::trace) {trace_authority_=auth;trace_fuel_=cambc::fuel_remaining();}
        if(!self.observed) return;
        if(!alive(self)) {
            invalidate_endpoint(auth,ready_field::Reason::ActorDead);
            if(mode_==Mode::Execute) {
                service::Frame dead;
                dead.authority=auth;dead.fresh=true;dead.alive=false;
                (void)service_.step(dead); // invalidate the active word on actual death
            }
            previous_valid_=false;bank_trace_={};receipt_={};bank_receipt_={};
            itinerary_.clear();order_.clear();walk_segment_=walk_cell_=0;route_progress_=0;
            shop_done_=false;shop_gold_=0;shop_strength_=0;residual_=false;residual_replans_=0;
            if(world_.state()==ServiceWorld::State::Ready) mode_=Mode::Nominate;
            return; // actual respawn will bind a new recipe; no old live authority survives
        }
        if(mode_==Mode::Cold) {
            const auto allies=cambc::champions(cambc::team::ally);
            ally_count_=std::max(1u,static_cast<unsigned>(allies.size()));
            for(std::size_t i=0;i<allies.size();++i)
                if(static_cast<int>(static_cast<cambc::handle_champion_t>(allies[i]))==owner_id()) ordinal_=static_cast<unsigned>(i);
            if(!world_.begin({self.raw.x,self.raw.y})) {mode_=Mode::Unsupported;return;}
            mode_=Mode::World;return;
        }
        if(mode_==Mode::World) {
            const auto state=world_.advance();
            if(state==ServiceWorld::State::Unsupported) mode_=Mode::Unsupported;
            else if(state==ServiceWorld::State::Ready) {
                retry_after_.assign(world_.groups.size(),0);rejected_.assign(world_.groups.size(),false);
                failed_words_.assign(world_.groups.size(),0);observed_lifecycle_.assign(world_.groups.size(),0);
                mode_=Mode::Nominate;
            }
            return;
        }
        if(mode_==Mode::Nominate) {nominate(self,auth);return;}
        if(mode_==Mode::Prepare) {
            if(!field_quiet(self)) {mode_=Mode::Unsupported;return;}
            const auto state=planner_.advance(32);
            if(state==route::State::Unsupported) {rejected_[group_index_]=true;mode_=Mode::Nominate;}
            else if(state==route::State::Complete) seal(self,auth);
            return;
        }
        if(mode_==Mode::Execute) execute(self);
    }

public:
    void play() {
        if constexpr(config::trace) {
            if(trace_pending_) {
                // Saved raw values only: no SDK getter, planning or command on
                // this fresh diagnostic input. This changes physical timing.
                std::cout << "wb_service";
                for(const auto value:trace_packet_) std::cout << ' ' << value;
                std::cout << '\n';trace_pending_=false;return;
            }
        }
        play_impl();
        if constexpr(config::trace) {
            const std::array<std::int64_t,12> packet{
                trace_authority_.owner,trace_authority_.round,static_cast<int>(mode_),
                static_cast<int>(world_.state()),static_cast<int>(world_.failure()),world_.terrain_stage(),
                static_cast<int>(planner_.state()),static_cast<int>(service_.phase()),
                static_cast<int>(trace_status_),static_cast<int>(trace_reason_),
                static_cast<std::int64_t>(planner_.search_nodes()),static_cast<std::int64_t>(trace_fuel_)};
            // Emit only structural changes, not every search slice/owner input.
            bool changed=!trace_seen_;
            for(std::size_t i=2;i<10;++i) if(packet[i]!=trace_last_[i]) changed=true;
            if(changed) {trace_packet_=packet;trace_last_=packet;trace_pending_=trace_seen_=true;}
        }
    }
};

inline void play_service_turn() {
    static ServiceRuntime runtime;
    runtime.play();
}

inline void play_service_draft() {
    const auto selected=cambc::draft::current_draft(cambc::team::ally);
    const std::array choices{cambc::champion_draft::marcella,cambc::champion_draft::ryth,
        cambc::champion_draft::vesper_vale,cambc::champion_draft::vendetta,cambc::champion_draft::tavi};
    for(const auto choice:choices)
        if(std::find(selected.begin(),selected.end(),choice)==selected.end()) {cambc::draft::pick(choice);return;}
}
} // namespace wb
