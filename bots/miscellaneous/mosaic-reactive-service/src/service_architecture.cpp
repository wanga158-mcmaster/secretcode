// A retained physical job proposes one command; only a current sparse packet
// and its consumed certificate authorize the primitive. The reference interpreter
// remains available behind selected_service=false.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>
#include "config.h"
#include "candidate_observation.h"
#include "selected_navigation.h"
#include "conversion_observation.h"
#include "production_model.h"
#include "service_architecture.h"
#include "proposal_lifecycle.h"
#include "service_task.h"
#include "service_continuation.h"

namespace mosaic {
namespace {
using namespace cambc;
using transition::Kind;
using transition::TargetKind;
struct ServiceActor {
    frame::SourceWorld world;
    candidate::Service observation;
    selected::Packet packet;
    selected_nav::Service navigation;
    selected_nav::Service delivery_navigation;
    conversion_observation::Cache conversion_observation_cache;
    conversion::History conversion_history;
    conversion::Result last_conversion_result;
    bool conversion_facts_ready{};
    std::optional<proposal::Lifecycle> proposal_lifecycle;
    std::vector<int> configured_respawn;
    candidate::History history;
    selected::Stamp stamp;
    std::optional<candidate::Proposal> proposal;
    continuation::Pending pending;
    frame::Self nominal_body;
    bool nominal_body_known{},entry_dispatch{},work_goals_reward{};
    std::uint64_t goal_generation{},receipt_generation{},body_generation{},channel_generation{};
    std::vector<vec2> refused;
    std::vector<vec2> altar_positions;
    std::vector<vec2> spawn_positions;
    std::vector<int> postponed;
    std::vector<std::vector<vec2>> inspected;
    std::vector<frame::GroupState> lifecycle;
    model::CaptureExpectation capture_expectation;
    bitboard goals,delivery_goals,delivery_blocked;
    std::uint64_t serial{};
    int owner{-1},task{-1},cursor{},last_bag{},route_depth{256};
    bool initialized{},capture_wait{},unsupported_conversion{};
    std::size_t delivery_layout_cursor{};
    bool delivery_layout_started{},delivery_layout_ready{},delivering{},returning{},channel_was_tribute{};
    int tribute_bag{},tribute_epoch{-1},last_submitted{},last_gold{},observed_banked{},observed_lost{};
    vec2 tribute_origin{};

    bool current(std::uint64_t reserve=18000)const {
        return candidate::current(stamp,reserve);
    }
    continuation::Context continuation_context()const {
        const auto mode=delivering?2u:(returning?3u:(task>=0?1u:0u));
        return {mode,mode==1u?task:-1,
            proposal_lifecycle&&task>=0?proposal_lifecycle->group_generation(std::size_t(task)):0,
            goal_generation,receipt_generation,body_generation,channel_generation,world.spatial_support.borrow()};
    }
    continuation::Binding continuation_binding(const frame::Self& body)const {
        return continuation::make_binding(owner,body,continuation_context());
    }
    bool valid_continuation_target(const transition::Command& cmd,const continuation::Binding& binding)const {
        if(binding.context.mode==2u||binding.context.mode==3u)
            return cmd.kind==Kind::move&&delivery_layout_ready&&!delivery_goals.is_empty();
        if(binding.context.mode!=1u||task<0||binding.context.group!=task)return false;
        if(cmd.kind==Kind::move)return !goals.is_empty(); // Reward goals require no living target.
        if(cmd.kind!=Kind::basic||work_goals_reward||cmd.target_index<0||
           std::size_t(cmd.target_index)>=world.camps.size())return false;
        const auto index=std::size_t(cmd.target_index);
        return world.camps[index].design.group==task&&world.camps[index].design.handle==cmd.target_id&&
            (!cfg::lifecycle_proposals||work_member(index).proposed);
    }
    void retain_nominee(const frame::Self& body) {
        if constexpr(cfg::service_continuations)pending.retain_nominee(continuation_binding(body),stamp.epoch);
    }
    void retain_command(transition::Command cmd,const frame::Self& body) {
        if constexpr(cfg::service_continuations)pending.retain(cmd,continuation_binding(body),stamp.epoch);
    }
    std::optional<transition::Command> cached_continuation_nominee(const continuation::Binding& binding) {
        const bool work=binding.context.mode==1u;
        if(work&&!work_goals_reward) {
            auto expected=nominal_body;expected.can_attack=true;
            if(const auto basic=basic_proposal(expected))return basic;
        }
        if(!work&&binding.context.mode!=2u&&binding.context.mode!=3u)return std::nullopt;
        const auto& blocked=work?*world.static_blocked:delivery_blocked;
        const auto& targets=work?goals:delivery_goals;
        const selected_nav::Hypothesis route{blocked,targets,binding.origin,binding.body.move_speed,
            stamp.epoch,owner,stamp.revision,true};
        const auto next=(work?navigation:delivery_navigation).nominate_hypothesis(route).proposal;
        if(!next.destination||!next.strictly_lower_potential||next.epoch!=stamp.epoch||next.actor!=owner||
           next.revision!=stamp.revision||next.origin!=binding.origin)return std::nullopt;
        auto cmd=command(Kind::move);cmd.destination=*next.destination;return cmd;
    }
    void retain_after_applied(const transition::Command& cmd,const frame::Self& before) {
        if constexpr(cfg::service_continuations) {
            nominal_body=before;nominal_body_known=true;
            if(cmd.kind==Kind::move){nominal_body.pos=cmd.destination;retain_nominee(nominal_body);}
            else retain_command(cmd,nominal_body);
        }
    }
    void setup() {
        if(initialized)return;
        owner=int(std::uint32_t(handle_champion_t(self())));
        world.set_spatial_model(true);
        altar_positions=map::altars().positions();
        if constexpr(cfg::conversion_service)spawn_positions=map::spawn_region(team::ally).positions();
        initialized=true;
    }
    transition::Command command(Kind kind)const {
        transition::Command result;result.kind=kind;
        result.expected_epoch=stamp.epoch;result.expected_actor=stamp.actor;
        result.expected_revision=stamp.revision;return result;
    }
    selected::Request request(transition::Command value) {
        value.expected_epoch=stamp.epoch;value.expected_actor=stamp.actor;value.expected_revision=stamp.revision;
        return {value,++serial};
    }
    selected::Request refine_proposal(transition::Command value) {
        if(!proposal||proposal->command.kind!=value.kind||proposal->command.target_id!=value.target_id||
           proposal->command.target_kind!=value.target_kind||proposal->command.destination!=value.destination)
            proposal=candidate::Proposal{value,++serial,stamp.epoch};
        if constexpr(cfg::service_continuations)if(nominal_body_known&&!entry_dispatch)retain_command(value,nominal_body);
        return proposal->request(stamp);
    }
    transition::Constraints limits()const {
        transition::Constraints value;value.group=task;
        // This is the reference diagnostic foe envelope, not a spell upper bound.
        value.allow_unmodelled_foes=true;return value;
    }
    bool fresh(const selected::Request& value,std::uint64_t reserve=18000)const {
        return current(reserve)&&packet.matches(stamp,value);
    }
    void decision_trace(candidate::Status status,const selected::Request& value,const selected::Assessment& decision)const {
        if constexpr(cfg::trace>=2) {
            static int logged_epoch=-1;
            if(stamp.epoch!=50||owner!=0||logged_epoch==stamp.epoch||!current(6000))return;
            logged_epoch=stamp.epoch;
            const auto* target=value.command.target_index>=0?packet.row(std::size_t(value.command.target_index)):nullptr;
            std::printf("mosaic selected r%d actor%d kind%d capture%d stamp%d layout%d packet%d self%d seal%d status%d eligible%d flags%llu origin%d/%d/%.1f canA%d target%d/%d/%d/%.1f rows%u state%u record%u buffs%u fuel%llu\n",
                stamp.epoch,owner,int(value.command.kind),int(status),int(candidate::request_stamp(stamp,value)),
                int(packet.layout.valid(world.camps.size())),int(packet.complete),int(packet.self_complete),int(packet.origin_complete),
                int(decision.result.status),int(decision.result.action.eligible),static_cast<unsigned long long>(decision.result.action.flags),
                int(decision.result.origin_wait.complete),int(decision.result.origin_wait.safe),double(decision.result.origin_wait.next_volley),
                int(packet.self.can_attack),int(target!=nullptr),int(target&&target->state_fresh),int(target&&target->buffs_fresh),
                double(target?target->camp.hp:-1),unsigned(observation.work.rows),unsigned(observation.work.state_reads),
                unsigned(observation.work.record_reads),unsigned(observation.work.buff_reads),static_cast<unsigned long long>(fuel_remaining()));
        }
    }
    selected::Assessment observe(const selected::Request& value) {
        const auto status=candidate::capture(world,observation,packet,stamp,value,history);
        if constexpr(cfg::service_continuations)if(packet.self_complete&&packet.stamp.matches(stamp)&&current(0)) {
            nominal_body=packet.self;nominal_body_known=true;
        }
        if(status!=candidate::Status::ready||!current()) {
            selected::Assessment result;
            if(current(12000))result.result.origin_wait=selected::assess_origin_wait(packet,value,limits());
            decision_trace(status,value,result);
            return result;
        }
        if constexpr(cfg::lifecycle_proposals)record_proposals();
        auto result=selected::assess(packet,value,limits());decision_trace(status,value,result);return result;
    }
    bool initialize_proposals() {
        if(proposal_lifecycle)return true;
        if(!current(45000))return false;
        proposal_lifecycle.emplace();
        if(!proposal_lifecycle->bind({world.camps.data(),world.camps.size()},world.spatial_support.borrow(),world.groups.size())) {
            proposal_lifecycle.reset();return false;
        }
        configured_respawn.reserve(world.groups.size());
        for(const auto& group:world.groups)configured_respawn.push_back(group.handle.max_respawn_timer());
        record_proposals();return false; // registration work owns one quiet input
    }
    void record_proposals() {
        if(!proposal_lifecycle||!packet.matches(stamp,packet.request))return;
        std::array<bool,160> before{};
        if constexpr(cfg::service_continuations)if(task>=0)
            for(auto index:world.groups[std::size_t(task)].design.members)
                if(index<before.size())before[index]=proposal_lifecycle->may_propose(index,stamp.epoch);
        std::vector<int> revived;
        for(const auto& row:packet.rows)if(row.state_fresh&&row.camp.known&&row.camp.alive&&
            proposal_lifecycle->marked(row.index))revived.push_back(row.camp.group);
        if(!proposal_lifecycle->observe(packet,stamp,configured_respawn))return;
        if constexpr(cfg::service_continuations)if(task>=0)
            for(auto index:world.groups[std::size_t(task)].design.members)
                if(index<before.size()&&before[index]!=proposal_lifecycle->may_propose(index,stamp.epoch))++goal_generation;
        for(const auto group:revived)if(group>=0&&std::size_t(group)<inspected.size())inspected[std::size_t(group)].clear();
        if(proposal&&proposal->command.kind==Kind::basic&&proposal->command.target_index>=0) {
            const auto* target=packet.row(std::size_t(proposal->command.target_index));
            if(target&&target->state_fresh&&target->camp.known&&!target->camp.alive)proposal.reset();
        }
    }
    void refresh_selected_lifecycle() {
        const auto before=observation.work.group_reads;
        const auto status=candidate::lifecycle_batch(world,observation,stamp,1,task);
        if constexpr(cfg::lifecycle_proposals)if(proposal_lifecycle&&status==candidate::Status::ready&&current(20000)&&
            observation.work.group_reads>before&&task>=0) {
            // Exactly one selected group was actually queried, then fenced by
            // lifecycle_batch. A hidden READY with no query grants no event.
            const auto& memory=world.memory[std::size_t(task)];frame::Group fact;
            fact.id=task;fact.state=memory.state;fact.cooldown=memory.cooldown;fact.observed_round=memory.observed_round;
            const auto generation=proposal_lifecycle->group_generation(std::size_t(task));
            proposal_lifecycle->observe_group(fact,stamp,stamp);
            if(generation!=proposal_lifecycle->group_generation(std::size_t(task))) {
                if constexpr(cfg::service_continuations)++goal_generation;
                inspected[std::size_t(task)].clear();
                // The immediate cycle event precedes ingestion of this input's
                // fresh rows. Reapply the still-current packet so a newly dead
                // member of the new cycle cannot be erased with old marks.
                record_proposals();
            }
        }
    }
    service_task::Member work_member(std::size_t index)const {
        const auto& design=world.camps[index].design;
        const auto& memory=world.memory[std::size_t(design.group)].members[std::size_t(design.member)];
        const bool proposed=proposal_lifecycle&&proposal_lifecycle->may_propose(index,stamp.epoch)&&
            !(memory.observed_round==stamp.epoch&&!memory.alive);
        const float hp=memory.observed_round>=0&&memory.alive?memory.hp:design.design_hp;
        return {{design.spawn.x,design.spawn.y},hp,proposed};
    }
    bool apply(selected::Assessment& decision,const selected::Request& value) {
        if constexpr(cfg::service_continuations)if(!entry_dispatch) {
            if(decision.certificate&&packet.matches(stamp,value)&&decision.result.action.next_volley==0&&
               decision.result.origin_wait.next_volley==0)retain_command(value.command,packet.self);
            else if(packet.matches(stamp,value))pending.clear();
        }
        if(!decision.certificate||!fresh(value,19000))return false;
        // V1 implements quiet service prefixes. A harmful finite commitment and
        // its withdrawal word require their own supported operator, not repeated
        // one-phase survival certificates that postpone an eventual death.
        if(decision.result.action.next_volley>0||decision.result.origin_wait.next_volley>0)return false;
        auto granted=decision.certificate->consume(stamp,value,world.spatial_support.borrow(),packet.self.pos,packet.nonce);
        if(!granted||!current(18000))return false;
        const auto cmd=*granted;
        const auto before=packet.self;
        // Revoke before the SDK wrapper, including all refused/error outcomes.
        ++stamp.revision;packet.complete=false;proposal.reset();
        if constexpr(cfg::service_continuations)pending.clear();
        bool applied=false;
        if(cmd.kind==Kind::move) {
            applied=self().move_absolute(cmd.destination);
            if(applied){history.moved=true;refused.clear();}
            else refused.push_back(cmd.destination);
        } else if(cmd.kind==Kind::basic&&cmd.target_kind==TargetKind::camp) {
            const auto found=world.camp_indices.find(cmd.target_id);
            if(found!=world.camp_indices.end()) {
                world.remember_wake(world.camps[found->second].design.group);
                self().attack(world.camps[found->second].handle);
                applied=true;history.attacked=true;
                if constexpr(cfg::direct_proposals)
                    proposal=candidate::Proposal{cmd,++serial,stamp.epoch};
            }
        }
        if(applied)retain_after_applied(cmd,before);
        return applied;
    }
    bool quiet(const selected::Assessment& basis)const {
        return basis.result.origin_wait.complete&&basis.result.origin_wait.safe&&basis.result.origin_wait.next_volley==0;
    }
    bool no_visible_foe()const {
        return std::none_of(packet.enemies.begin(),packet.enemies.end(),[](const auto& enemy){return enemy.visible&&enemy.alive;});
    }
    bool advance_delivery_layout() {
        if(delivery_layout_ready)return true;
        if(!delivery_layout_started) {
            if(!current(50000))return false;
            delivery_blocked=*world.static_blocked;delivery_layout_started=true;
        }
        // A conditional quiet-route class: exclude every positive-design camp
        // range regardless of present sleep, death, helper actions or respawn.
        // Fixed position/range is the same explicitly accepted spatial contract.
        while(delivery_layout_cursor<world.camps.size()) {
            if(!current(45000))return false;
            const auto& design=world.camps[delivery_layout_cursor++].design;
            if(design.design_strength>0)delivery_blocked.draw_chebyshev(design.spawn,design.range);
        }
        delivery_layout_ready=true;return false; // immutable construction owns this input
    }
    bool apply_conversion(conversion::Kind kind,vec2 altar={}) {
        conversion_facts_ready=false;last_conversion_result={};
        if(!current(32000)||packet.request.command.kind!=Kind::wait||!fresh(packet.request,32000))return false;
        const conversion::Request value{kind,stamp,++serial,altar};
        conversion::Facts facts;
        if(conversion_observation::capture(world,conversion_observation_cache,packet,value,conversion_history,facts)
            !=conversion_observation::Status::ready||!current(20000))return false;
        auto decision=conversion::assess(packet,facts,value);
        conversion_facts_ready=true;last_conversion_result=decision.result;
        if(!decision.certificate||decision.result.requires_egress||!fresh(packet.request,19000))return false;
        const auto basis=packet.request;
        const auto granted=decision.certificate->consume(stamp,value,basis,world.spatial_support.borrow(),packet.self.pos,packet.nonce);
        if(!granted||!current(18000))return false;
        const int bag=packet.self.gems;const auto origin=packet.self.pos;
        ++stamp.revision;packet.complete=packet.origin_complete=false;proposal.reset();
        if constexpr(cfg::service_continuations){pending.clear();++channel_generation;}
        // All four SDK wrappers return void. Only normal return advances history.
        switch(kind) {
        case conversion::Kind::recall_start:self().recall();break;
        case conversion::Kind::recall_cancel:self().cancel_recall();break;
        case conversion::Kind::tribute_start:self().submit_gems(altar);break;
        case conversion::Kind::tribute_cancel:self().cancel_submit_gems();break;
        case conversion::Kind::buy:return false;
        }
        if(const auto after=conversion::advanced(conversion_history,value))conversion_history=*after;
        if(kind==conversion::Kind::tribute_start) {
            channel_was_tribute=true;tribute_bag=bag;tribute_epoch=value.stamp.epoch;tribute_origin=origin;
        }
        return true;
    }
    bool delivery_step(const frame::Self& body) {
        if(!current(45000)||!delivery_layout_ready)return false;
        selected_nav::Request route{delivery_blocked,delivery_goals,body.pos,body.move_speed,
            stamp.epoch,owner,stamp.revision,true,body.alive,body.can_move};
        const auto progress=delivery_navigation.prepare(route,256);
        if(!progress.covered){trace("delivery-compute");return false;}
        retain_nominee(body);
        // Every landing still receives a fresh actual path/occupancy/refusal
        // certificate. The cached field is only a static route potential.
        for(unsigned tries=0;tries<81&&current(52000);++tries) {
            const selected_nav::FailedEndpoints failures{refused,stamp.epoch,owner,body.pos};
            const auto next=delivery_navigation.nominate(route,failures);
            if(next.status==selected_nav::Status::arrived){if constexpr(cfg::service_continuations)pending.clear();return false;}
            if(!next.destination||next.epoch!=stamp.epoch||next.actor!=owner||next.revision!=stamp.revision||
               next.origin!=body.pos||!next.strictly_lower_potential)return false;
            auto cmd=command(Kind::move);cmd.destination=*next.destination;
            const auto value=refine_proposal(cmd);auto decision=observe(value);
            if(apply(decision,value)){trace("delivery-move");return true;}
            if(stamp.revision!=0)return false;
            if(packet.move_test_complete&&!packet.move_legal){refused.push_back(cmd.destination);continue;}
            return false;
        }
        return false;
    }
    bool convert(const selected::Assessment& basis,const frame::Self& body) {
        // Actual bag loss retires a residual. Only the separately observed live
        // tribute/position/bag/team-score conjunction records a bank candidate;
        // native receipt analysis remains the final attribution authority.
        if(body.gems<last_bag) {
            if constexpr(cfg::service_continuations){pending.clear();++receipt_generation;}
            const int now=int(global_submitted_gems(team::ally));
            if(channel_was_tribute&&!body.submitting&&body.alive&&body.pos==tribute_origin&&
               last_bag-body.gems==tribute_bag&&now-last_submitted>=tribute_bag&&stamp.epoch>tribute_epoch) {
                observed_banked+=tribute_bag;returning=true;
            }
            else observed_lost+=last_bag-body.gems;
            channel_was_tribute=false;
        }
        if(body.gems>last_bag){
            if constexpr(cfg::service_continuations){pending.clear();++receipt_generation;}
            delivering=true;task=-1;capture_wait=false;capture_expectation.clear();proposal.reset();
        }
        // Even a zero-gem payout must release its occupied region. Cash is an
        // actual residual here, not a claim that a purchase or reinvestment paid.
        if(body.gold>last_gold&&body.gems==0&&capture_wait) {
            if constexpr(cfg::service_continuations){pending.clear();++receipt_generation;}
            returning=true;task=-1;capture_wait=false;capture_expectation.clear();proposal.reset();
        }
        last_gold=body.gold;
        last_bag=body.gems;
        if(body.gems==0)delivering=false;
        if(body.submitting||body.recalling) {
            // The one-phase quiet/visible-foe condition is reobserved each input.
            // V1 cannot promise an egress under harmful commitment; no unsafe
            // cancellation is issued merely to turn it into an unpriced wait.
            if(quiet(basis)&&no_visible_foe()){trace("channel-hold");return true;}
            trace("unsupported-channel-egress");return true;
        }
        if(!quiet(basis)){trace("delivery-needs-egress");return delivering||returning;}
        if(delivering) {
            // Direct-bank is one complete suffix. A recall/heal alternative may
            // replace it only after its actual static return potential is known.
            delivery_goals.clear();
            if constexpr(cfg::service_continuations)++goal_generation;
            for(auto altar:altar_positions) {
                if(transition::distance(body.pos,altar)<=constants::submit_gems_range) {
                    last_submitted=int(global_submitted_gems(team::ally));
                    if(apply_conversion(conversion::Kind::tribute_start,altar)){trace("tribute-start");return true;}
                    const auto rejected=conversion::altar_locked|conversion::bad_altar;
                    if(conversion_facts_ready&&last_conversion_result.status==selected::Status::rejected&&
                       (last_conversion_result.flags&rejected)!=0)continue;
                    trace("tribute-deferred");return true;
                }
                delivery_goals.draw_chebyshev(altar,constants::submit_gems_range);
            }
            if(delivery_goals.is_empty()){trace("all-altars-unavailable");return true;}
            delivery_step(body);return true;
        }
        if(returning) {
            const bool at_spawn=std::find(spawn_positions.begin(),spawn_positions.end(),body.pos)!=spawn_positions.end();
            if(at_spawn) {
                if(body.hp<body.max_hp){trace("spawn-heal");return true;}
                returning=false;task=-1;proposal.reset();
                if constexpr(cfg::service_continuations)pending.clear();
                trace("return-ready");return false;
            }
            if(no_visible_foe()&&apply_conversion(conversion::Kind::recall_start)){trace("return-recall");return true;}
            delivery_goals.clear();for(auto spawn:spawn_positions)delivery_goals.set(spawn);
            if constexpr(cfg::service_continuations)++goal_generation;
            delivery_step(body);return true;
        }
        return false;
    }
    bool group_ready(int id)const {
        return id>=0&&id<int(world.groups.size())&&
            !(world.memory[std::size_t(id)].state==frame::GroupState::respawning&&
              stamp.epoch<world.memory[std::size_t(id)].observed_round+world.memory[std::size_t(id)].cooldown);
    }
    // Dated estimates nominate useful complete jobs; they never certify a clear,
    // capture, landing or current target. Unpriced harmful commitments are not
    // implemented by this first selected-service slice.
    int nominate_job(const frame::Self& body) {
        int best=-1;float best_quote=-1;
        const int count=std::min(12,int(world.groups.size()));
        for(int n=0;n<count;++n) {
            const int id=(cursor+n)%int(world.groups.size());
            if(!group_ready(id)||postponed[std::size_t(id)]>stamp.epoch)continue;
            const auto& group=world.groups[std::size_t(id)].design;
            if(group.gems<=0)continue;
            float hp=0,harm=0;int approach=100000,delivery=100000;
            for(auto index:group.members) {
                const auto& design=world.camps[index].design;
                const auto& memory=world.memory[std::size_t(id)].members[std::size_t(design.member)];
                hp+=memory.observed_round>=0&&memory.alive?memory.hp:design.design_hp;
                harm+=design.design_strength;
                approach=std::min(approach,transition::distance(body.pos,design.spawn));
                for(auto altar:altar_positions)delivery=std::min(delivery,transition::distance(design.spawn,altar));
            }
            const auto& dated=world.memory[std::size_t(id)];
            const bool reward=dated.state==frame::GroupState::reward;
            if(harm>0&&!reward)continue; // unsupported fight, while capture-only remains proposed
            const float work=reward?0.0f:hp/std::max(1.0f,body.basic_damage);
            const float quote=float(group.gems)/float(1+approach+delivery+constants::capture_time+work);
            if(quote>best_quote){best_quote=quote;best=id;}
        }
        if(!world.groups.empty())cursor=(cursor+count)%int(world.groups.size());
        return best;
    }
    void set_goals(bool reward,const frame::Self& body) {
        if constexpr(cfg::service_continuations){++goal_generation;work_goals_reward=reward;}
        goals.clear();
        if(task<0)return;
        for(auto index:world.groups[std::size_t(task)].design.members) {
            if constexpr(cfg::lifecycle_proposals)if(!service_task::goal_member(index,reward,
                [&](auto member){return work_member(member);}))continue;
            goals.draw_chebyshev(world.camps[index].design.spawn,reward?0:(inspected[std::size_t(task)].empty()?body.range:1));
        }
        for(auto position:inspected[std::size_t(task)])goals.set(position,false);
    }
    std::optional<transition::Command> basic_proposal(const frame::Self& body)const {
        if(task<0||!body.can_attack)return std::nullopt;
        if constexpr(cfg::lifecycle_proposals) {
            const auto best=service_task::nominate_basic(world.groups[std::size_t(task)].design.members,
                service_task::Point{body.pos.x,body.pos.y},body.range,body.basic_damage,
                [&](auto index){return work_member(index);});
            if(!best)return std::nullopt;
            auto result=command(Kind::basic);result.target_kind=TargetKind::camp;
            result.target_index=int(*best);result.target_id=world.camps[*best].design.handle;return result;
        }
        int best=-1;float value=-1;
        for(auto index:world.groups[std::size_t(task)].design.members) {
            const auto& design=world.camps[index].design;
            if(transition::distance(body.pos,design.spawn)>body.range)continue;
            const auto& memory=world.memory[std::size_t(task)].members[std::size_t(design.member)];
            if(memory.observed_round==stamp.epoch&&!memory.alive)continue;
            const float hp=memory.observed_round>=0&&memory.alive?memory.hp:design.design_hp;
            const float useful=std::min(hp,std::max(0.0f,body.basic_damage));
            if(useful>value){value=useful;best=int(index);}
        }
        if(best<0)return std::nullopt;
        auto result=command(Kind::basic);result.target_kind=TargetKind::camp;
        result.target_index=best;result.target_id=world.camps[std::size_t(best)].design.handle;
        return result;
    }
    void trace(const char* phase)const {
        if constexpr(cfg::trace)if((stamp.epoch<=20||stamp.epoch%50==0)&&current(5000))
            std::printf("mosaic service %s r%d actor%d task%d rev%llu rows%u fuel%llu\n",phase,stamp.epoch,owner,task,
                static_cast<unsigned long long>(stamp.revision),unsigned(observation.work.rows),
                static_cast<unsigned long long>(fuel_remaining()));
    }
    // The same dispatch_first function is used by the scheduler fixture. Its
    // callbacks expose no acquisition path: an unfinished primitive gets its
    // one fresh packet before the ordinary Wait/route prefix below.
    struct ContinuationOps {
        ServiceActor& actor;
        std::optional<selected::Assessment> captured;
        selected::Request value;
        bool current(selected::Stamp expected)const {
            return expected.matches(actor.stamp)&&candidate::current(expected,0);
        }
        std::optional<transition::Command> nominate(const continuation::Binding& binding) {
            return actor.cached_continuation_nominee(binding);
        }
        selected::Request request(transition::Command cmd){return actor.request(cmd);}
        selected::Assessment& capture(const selected::Request& request) {
            value=request;captured.emplace(actor.observe(request));return *captured;
        }
        const selected::Packet& packet()const{return actor.packet;}
        continuation::Context context(const selected::Packet&)const{return actor.continuation_context();}
        bool valid_target(const transition::Command& cmd,const continuation::Binding& binding)const {
            // This interpreter supports quiet commitments only. A complete
            // current positive-volley result is an unsupported nominee, not a
            // budget deferral to be retained before every later input.
            if(captured&&(captured->result.action.next_volley>0||captured->result.origin_wait.next_volley>0))return false;
            return actor.valid_continuation_target(cmd,binding);
        }
        continuation::Attempt attempt(selected::Assessment& decision,const selected::Request& request) {
            const auto before=actor.stamp.revision;
            const bool applied=actor.apply(decision,request);
            if(actor.stamp.revision==before)return continuation::Attempt::not_attempted;
            return applied?continuation::Attempt::applied:continuation::Attempt::refused;
        }
    };
    void turn() {
        setup();stamp={int(round_number()),owner,0};history={};refused.clear();
        if constexpr(cfg::conversion_service) {
            conversion_history={stamp.epoch,owner};
            if(!self().is_alive()) {
                if constexpr(cfg::service_continuations){pending.clear();nominal_body_known=false;++body_generation;}
                observed_lost+=last_bag;last_bag=0;delivering=returning=channel_was_tribute=false;
                task=-1;proposal.reset();capture_wait=false;capture_expectation.clear();return;
            }
        }
        if(candidate::advance_registration(world,observation,packet,stamp)!=candidate::Status::ready) {
            trace("registration");return;
        }
        if(postponed.size()!=world.groups.size())postponed.assign(world.groups.size(),0);
        if(inspected.size()!=world.groups.size())inspected.resize(world.groups.size());
        if(lifecycle.size()!=world.groups.size())lifecycle.resize(world.groups.size(),frame::GroupState::unknown);
        selected::Request first;bool direct=false,continued=false;
        std::optional<selected::Assessment> entry_basis;
        if constexpr(cfg::service_continuations)if(pending.active()&&nominal_body_known) {
            ContinuationOps ops{*this,{}, {}};
            continuation::Report progress;
            {
                struct DispatchScope {bool& flag;~DispatchScope(){flag=false;}} scope{entry_dispatch};
                entry_dispatch=true;
                progress=continuation::dispatch_first(pending,stamp,continuation_binding(nominal_body),ops);
            }
            if(progress.attempted){trace("continued-primitive");return;}
            if(progress.captured) {
                // Reuse this packet and its explicit origin subset on fallback;
                // a second Wait capture would repeat the failed prefix.
                first=ops.value;entry_basis.emplace(std::move(*ops.captured));continued=true;
                if(progress.outcome==continuation::Outcome::deferred) {
                    trace(progress.origin_wait.complete&&progress.origin_wait.safe&&progress.origin_wait.next_volley==0?
                        "continuation-deferred":"continuation-needs-egress");
                    return;
                }
            }
        }
        if(!continued)first=request(command(Kind::wait));
        if constexpr(cfg::lifecycle_proposals)if(proposal&&proposal->command.kind==Kind::basic&&
            (proposal->command.target_index<0||!proposal_lifecycle||
             !proposal_lifecycle->may_propose(std::size_t(proposal->command.target_index),stamp.epoch)))proposal.reset();
        if constexpr(cfg::direct_proposals&&!cfg::service_continuations)if(proposal&&!unsupported_conversion&&
            (!cfg::conversion_service||(!delivering&&!returning))&&proposal->command.kind==Kind::basic) {
            first=proposal->request(stamp);direct=true;
        }
        auto basis=entry_basis?std::move(*entry_basis):observe(first);
        if constexpr(cfg::direct_proposals)if(direct&&fresh(first)&&packet.self.gems<=last_bag&&apply(basis,first)) {
            trace("direct-basic");return;
        }
        bool basis_valid=fresh(first);
        if constexpr(cfg::direct_proposals||cfg::service_continuations)if(!basis_valid&&(direct||continued)&&current(18000))
            basis_valid=packet.self_complete&&packet.stamp.matches(stamp)&&selected::same_request(packet.request,first)&&
                packet.layout.valid(world.camps.size())&&basis.result.origin_wait.complete;
        if(!basis_valid||!packet.self.alive){trace("observation");return;}
        const auto body=packet.self;
        if constexpr(cfg::lifecycle_proposals)if(!proposal_lifecycle) {
            if(quiet(basis))initialize_proposals();trace("proposal-registration");return;
        }
        if constexpr(cfg::conversion_service) {
            if(body.stunned||body.teleporting)return;
            if(!delivery_layout_ready) {
                if(quiet(basis))advance_delivery_layout();trace("delivery-layout");return;
            }
            if(convert(basis,body))return;
        } else {
            if(body.stunned||body.recalling||body.submitting||body.teleporting)return;
            if(body.gems>last_bag){unsupported_conversion=true;task=-1;capture_wait=false;}
            last_bag=body.gems;
            if(unsupported_conversion){trace("unsupported-conversion");return;}
        }
        // Only a fresh zero-potential-volley origin permits optional discovery or
        // field construction. A sleeping harmful source is saturated by the kernel.
        if(!basis.result.origin_wait.complete||!basis.result.origin_wait.safe||basis.result.origin_wait.next_volley>0) {
            trace("needs-egress");return;
        }
        // Discovery owns its own bounded quiet work. A retained job does not
        // repeat the discovery cycle before every selected action.
        if(task<0&&current(60000))candidate::lifecycle_batch(world,observation,stamp,2);
        if(task<0&&current(55000)){task=nominate_job(body);route_depth=256;capture_wait=false;}
        if(task<0){trace("no-priced-job");return;}
        int frontier=100000;
        for(auto index:world.groups[std::size_t(task)].design.members)
            frontier=std::min(frontier,transition::distance(body.pos,world.camps[index].design.spawn));
        if(frontier<=body.range+1&&current(60000))refresh_selected_lifecycle();
        const auto& memory=world.memory[std::size_t(task)];
        if(memory.observed_round==stamp.epoch&&lifecycle[std::size_t(task)]!=memory.state) {
            lifecycle[std::size_t(task)]=memory.state;inspected[std::size_t(task)].clear();
            if(capture_expectation.group==task)capture_expectation.clear();
        }
        if(memory.observed_round==stamp.epoch&&memory.state==frame::GroupState::respawning) {
            postponed[std::size_t(task)]=stamp.epoch+std::max(1,memory.cooldown);task=-1;return;
        }
        const bool reward=memory.observed_round==stamp.epoch&&memory.state==frame::GroupState::reward;
        if(reward)for(auto index:world.groups[std::size_t(task)].design.members)
            if(world.camps[index].design.spawn==body.pos) {
                bool contested=false;
                for(const auto& enemy:packet.enemies)if(enemy.visible&&enemy.alive)
                    for(auto member:world.groups[std::size_t(task)].design.members)
                        if(enemy.pos==world.camps[member].design.spawn)contested=true;
                capture_expectation.begin(task,stamp.epoch);
                if(!contested&&!capture_expectation.needs_observation(stamp.epoch,constants::capture_time)) {
                    if constexpr(cfg::service_continuations)pending.clear();
                    capture_wait=true;trace("capture-hold");return;
                }
                auto& visited=inspected[std::size_t(task)];
                if(std::find(visited.begin(),visited.end(),body.pos)==visited.end())visited.push_back(body.pos);
                capture_expectation.clear();
            }
        set_goals(reward,body);
        if constexpr(cfg::lifecycle_proposals)if(goals.is_empty()) {
            // Expired negative intervals permit an actual probe; none of these
            // dated marks authorizes a clear, reward or permanent job rejection.
            trace("dated-targets-await-probe");return;
        }
        selected_nav::Request route{*world.static_blocked,goals,body.pos,body.move_speed,
            stamp.epoch,owner,stamp.revision,true,body.alive,body.can_move};
        // A basic already in nominal range is refreshed directly; all observed
        // legality/effect/retaliation is checked by the selected packet.
        bool observed_nonactionable=false;
        if(!reward)if(const auto basic=basic_proposal(body)) {
            retain_command(*basic,body);
            if(!current(52000)){trace("basic-deferred");return;}
            const auto value=refine_proposal(*basic);
            auto decision=[&]() {
                if constexpr(cfg::direct_proposals)
                    if(selected::same_request(packet.request,value)&&fresh(value))return std::move(basis);
                return observe(value);
            }();
            if(fresh(value))if(const auto* target=packet.row(std::size_t(basic->target_index));target&&target->effect)
                observed_nonactionable=!target->state_fresh||!target->camp.alive||
                    !target->camp.visible||!packet.effect_champions_clear;
            if(apply(decision,value)){trace("basic");return;}
            if(stamp.revision!=0)return; // an actual refusal requires a new observation
            if(!packet.complete||!current(52000)){trace("basic-deferred");return;}
        }
        if(!current(45000)){trace("route-deferred");return;}
        const auto progress=navigation.prepare(route,unsigned(route_depth));
        if(!progress.covered) {
            if(progress.status==selected_nav::Status::no_static_goal) {
                postponed[std::size_t(task)]=int(constants::max_rounds)+1;
                capture_expectation.clear();task=-1;trace("unsupported-frontier");return;
            }
            if(progress.next_depth>256) {
                // This bounded planner did not cover the origin. That is a
                // capability limit, not a claim that the terrain is unreachable.
                postponed[std::size_t(task)]=int(constants::max_rounds)+1;task=-1;
                trace("unsupported-route-depth");return;
            }
            route_depth=std::max(route_depth,int(progress.next_depth));trace("route-compute");return;
        }
        retain_nominee(body);
        // Each rejected current landing is removed from this input's remaining
        // finite proposal set. A rejection is not a terrain-unreachability fact.
        for(unsigned attempts=0;attempts<81&&current(52000);++attempts) {
            const selected_nav::FailedEndpoints failures{refused,stamp.epoch,owner,body.pos};
            const auto landing=navigation.nominate(route,failures);
            if(landing.status==selected_nav::Status::arrived) {
                if constexpr(cfg::service_continuations)pending.clear();
                // Nominal range without an observed legal target is not completion.
                if(!observed_nonactionable){trace("observation-deferred");return;}
                auto& visited=inspected[std::size_t(task)];
                if(std::find(visited.begin(),visited.end(),body.pos)==visited.end()) {
                    visited.push_back(body.pos);if constexpr(cfg::service_continuations)++goal_generation;
                }
                trace("needs-reveal");return;
            }
            if(!landing.destination||landing.epoch!=stamp.epoch||landing.actor!=owner||landing.revision!=stamp.revision||
               landing.origin!=body.pos||!landing.strictly_lower_potential){trace("no-landing");return;}
            auto move=command(Kind::move);move.destination=*landing.destination;
            const auto value=refine_proposal(move);auto decision=observe(value);
            if(apply(decision,value)) {
                trace("move");
                // Keep legal same-input melee packing. The destination here is an
                // applied move receipt used only to nominate; the new basic packet
                // rereads actual self, allowances and every selected dependency.
                if(!reward&&body.range==1&&current(52000)) {
                    auto after=body;after.pos=move.destination;
                    if(const auto basic=basic_proposal(after)) {
                        const auto attack=refine_proposal(*basic);auto following=observe(attack);
                        if(apply(following,attack))trace("move-basic");
                    }
                }
                return;
            }
            if(stamp.revision!=0)return;
            if(packet.move_test_complete&&!packet.move_legal) {
                refused.push_back(move.destination);continue;
            }
            trace("move-deferred");return;
        }
    }
};
}
void service_turn(){static ServiceActor actor;actor.turn();}
}
