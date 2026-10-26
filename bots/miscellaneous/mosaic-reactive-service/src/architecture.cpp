// Independent physical task interpreter. The starter remains the default control.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <span>
#include <vector>
#include "architecture.h"
#include "config.h"
#include "frame.h"
#include "navigation.h"
#include "transition.h"
#include "task_actions.h"
#include "production_model.h"

namespace mosaic {
namespace {
using namespace cambc;
using transition::Kind;
using transition::TargetKind;
struct Actor {
    frame::SourceWorld world;
    frame::Frame observed;
    std::optional<transition::PlanningContext<bitboard>> planning;
    nav::RouteCache route;
    int owner{-1},epoch{},writer{-1},task{-1},resume{-1},last_bag{},failures{},last_failed_task{-1};
    std::uint64_t revision{};
    bool moved{},attacked{},cast_spent{},recovering{},converting{},initialized{};
    std::array<std::uint16_t,4> intents{};
    std::array<int,4> writers{{-1,-1,-1,-1}};
    std::vector<int> banned_until;
    std::vector<std::vector<vec2>> reveal_landings;
    std::vector<std::vector<int>> capture_blockers;
    std::vector<std::vector<vec2>> capture_landings;
    std::vector<bool> capture_unresolved;
    model::CaptureExpectation capture_expectation;
    std::vector<std::pair<vec2,int>> altar_bans;
    std::vector<transition::Command> refused;
    bitboard spawn,altars,failed_steps;
    std::vector<vec2> spawn_positions,altar_positions;
    int cursor{};
    unsigned shard_cost{};

    bool current(std::uint64_t reserve=17000) const {
        return int(round_number())==epoch && fuel_remaining()>reserve;
    }
    void setup() {
        if(initialized)return;
        // Explicit opt-in to the pinned public camp-position/range model. Live
        // observed disagreement revokes the borrowed index and forces full scan.
        world.set_spatial_model(true);
        world.set_local_support(true);
        owner=int(std::uint32_t(handle_champion_t(self())));
        auto roster=champions(team::ally);
        std::vector<int> ids;
        for(const auto& c:roster)ids.push_back(int(std::uint32_t(handle_champion_t(c))));
        std::sort(ids.begin(),ids.end());
        for(int i=0;i<std::min(4,int(ids.size()));++i){writers[i]=ids[i];if(ids[i]==owner)writer=i;}
        spawn=map::spawn_region(team::ally);altars=map::altars();
        shard_cost=item_type::jungle_shard_of_strength.cost();
        spawn_positions=spawn.positions();altar_positions=altars.positions();
        initialized=true;
    }
    bool snapshot() {
        return frame::capture(world,observed,epoch,owner,revision,moved,attacked,cast_spent);
    }
    void startup_trace(const char* phase) const {
        if constexpr(cfg::trace>=3)if(epoch<=12&&owner==writers[0]&&current(5000))
            std::printf("mosaic startup %s r%d init%d groups%u camps%u index%d scratch%d/%u layout%d/%u/%u fuel%llu\n",
                phase,epoch,int(world.initialized),unsigned(world.groups.size()),unsigned(world.camps.size()),
                int(world.spatial_built),int(world.memory_scratch_ready),unsigned(world.memory_scratch_cursor),
                int(observed.layout_initialized),unsigned(observed.layout_group_cursor),unsigned(observed.layout_camp_cursor),
                static_cast<unsigned long long>(fuel_remaining()));
    }
    transition::PlanningContext<bitboard>& context() {
        if(!planning)planning.emplace(observed,transition::ContextMode::spatial);
        else if(!planning->current())planning->reset(observed);
        return *planning;
    }
    transition::Evaluation certify(const transition::Command& cmd,const transition::Constraints& limits) {
        return transition::evaluate(context(),cmd,limits);
    }
    void publish(bool working) {
        if(writer<0||shared_memory.size()<8||constants::max_rounds>=2048||!current(4000))return;
        // Fixed 31-entry task dictionary and full 11-bit round; no timestamp wrap.
        const unsigned code=working && task>=0 && task<31?unsigned(task+1):0;
        const std::uint16_t value=std::uint16_t((unsigned(epoch)<<5)|code);
        const std::array<std::uint8_t,2> bytes{std::uint8_t(value),std::uint8_t(value>>8)};
        if(current(3500))shared_memory.write(std::size_t(writer*2),std::span{bytes});
    }
    void read_intents() {
        intents.fill(0);
        if(shared_memory.size()<8||constants::max_rounds>=2048||!current())return;
        std::array<std::uint8_t,8> bytes{};shared_memory.read(0,std::span{bytes});
        if(!current())return;
        for(int i=0;i<4;++i)intents[i]=std::uint16_t(bytes[2*i]|(unsigned(bytes[2*i+1])<<8));
    }
    bool claimed(int group) const {
        if(group>=31)return false;
        for(int i=0;i<4;++i)if(i!=writer && writers[i]>=0) {
            const int sent=int(intents[i]>>5),code=int(intents[i]&31);
            if(code==group+1&&epoch>=sent&&epoch-sent<=1&&(sent<epoch||i<writer))return true;
        }
        return false;
    }
    int nearest_spawn(vec2 p) const {
        int d=1<<20;for(auto q:spawn_positions)d=std::min(d,transition::distance(p,q));return d;
    }
    int nearest_altar(vec2 p,vec2* chosen=nullptr) const {
        int d=1<<20;
        for(auto q:altar_positions) {
            bool banned=false;for(const auto& b:altar_bans)if(b.first==q&&b.second>epoch)banned=true;
            if(banned)continue;
            const int next=std::max(0,transition::distance(p,q)-constants::submit_gems_range);
            if(next<d){d=next;if(chosen)*chosen=q;}
        }
        return d;
    }
    bitboard group_tiles(int group,int radius=0,bool living_only=false) const {
        bitboard result;result.clear();
        const auto* g=observed.group(group);if(!g)return result;
        for(auto index:g->members) {
            const auto& c=observed.camps[index];
            if(living_only && c.known&&!c.alive)continue;
            result.draw_chebyshev(c.spawn,float(radius));
        }
        return result;
    }
    float quote(const frame::Group& g) const {
        if(g.id<0||g.id>=int(banned_until.size())||banned_until[g.id]>epoch||claimed(g.id)||
            !capture_blockers[g.id].empty()||capture_unresolved[g.id])return -1;
        const auto& s=observed.self;
        if(g.state==frame::GroupState::respawning && g.observed_round==epoch)return -1;
        int travel=1<<20;float health=0,strength=0;vec2 finish=s.pos;
        const int splash=task_actions::splash_count(observed,g,s.splash_radius);
        for(auto i:g.members) {
            const auto& c=observed.camps[i];const int approach=transition::distance(s.pos,c.spawn);
            if(approach<travel){travel=approach;finish=c.spawn;}
            if(c.known&&!c.alive)continue;
            health+=c.hp>0?c.hp:c.design_hp;strength+=std::max(0.0f,c.strength);
        }
        // Ranking estimate only: no claim that this static rate certifies a legal clear.
        const float rate=std::max(1.0f,s.basic_damage*float(splash));
        const float fight=g.state==frame::GroupState::reward&&g.observed_round==epoch?0.0f:health/rate;
        const float injury=strength*std::max(0.0f,fight-1)*s.camp_taken_multiplier;
        const float recovery=injury/std::max(1.0f,s.max_hp)*float(constants::recall_time+1+nearest_spawn(finish));
        const int channel=std::max(constants::submit_gem_minimum_duration,constants::submit_gem_base_duration+
            int(std::ceil(double(g.gems)*constants::submit_gem_duration_multiplier)));
        const float conversion=g.gems>0?float(nearest_altar(finish)+channel):0;
        float value=float(g.gems);
        if(value==0 && s.gold<int(shard_cost*5))value=std::min(2.0f,float(g.gold)/std::max(1.0f,float(shard_cost*5)));
        return value/std::max(1.0f,float(std::max(0,travel-s.range)+constants::capture_time)+fight+recovery+conversion);
    }
    int select() {
        float best=-1;int chosen=-1;
        // The menu is bounded; every evaluated candidate still contains all of its members.
        const int count=std::min(16,int(observed.groups.size()));
        for(int i=0;i<count;++i) {
            if(!current(35000))break;
            const auto& g=observed.groups[(cursor+i)%observed.groups.size()];
            const float score=quote(g);if(score>best){best=score;chosen=g.id;}
        }
        if(!observed.groups.empty())cursor=(cursor+count)%int(observed.groups.size());
        return chosen;
    }
    transition::Constraints constraints(bool cover=true) const {
        transition::Constraints result;result.group=task;result.require_cover=cover;
        // Visible foe basics are represented, future enemy spells remain uncertain risk.
        result.allow_unmodelled_foes=true;result.max_candidates=96;
        return result;
    }
    nav::MovePlan plan(const bitboard& goals,const bitboard* forbidden=nullptr,bool use_cache=true) {
        auto dynamic=observed.dynamic_blocked|failed_steps;
        const nav::Input input{observed.static_blocked,dynamic,observed.self.pos,observed.self.move_speed,
            epoch,owner,revision,observed.complete,observed.self.alive,observed.self.can_move};
        if(forbidden||!use_cache)return nav::plan(input,goals,nullptr,forbidden);
        const auto prepared=route.prepare(input,goals);
        if(prepared!=nav::PrepareResult::ready&&prepared!=nav::PrepareResult::reused) {
            nav::MovePlan result;result.expected_epoch=epoch;result.actor=owner;result.revision=revision;
            result.expected_origin=observed.self.pos;
            if(prepared==nav::PrepareResult::no_static_goal)result.result=nav::Result::local_blocked;
            return result;
        }
        const nav::Destinations landings{observed.move_destinations,epoch,owner,revision,observed.self.pos};
        auto proposed=route.propose(input,landings);
        // Static distance is a direction potential, not a current occupancy
        // claim. A fresh local obstruction permits one bounded dynamic replan.
        if(proposed.result==nav::Result::local_blocked&&current(40000))
            return nav::plan(input,goals);
        return proposed;
    }
    std::optional<transition::Command> move_command(const nav::MovePlan& proposal) const {
        if(proposal.result!=nav::Result::step_ready||!proposal.destination)return std::nullopt;
        if(proposal.expected_epoch!=epoch||proposal.actor!=owner||proposal.revision!=revision||
            proposal.expected_origin!=observed.self.pos||!observed.matches(epoch,owner,revision))return std::nullopt;
        auto cmd=transition::command(observed,Kind::move);cmd.destination=*proposal.destination;return cmd;
    }
    bool was_refused(const transition::Command& cmd) const {
        for(const auto& c:refused)if(c.kind==cmd.kind&&c.target_kind==cmd.target_kind&&c.target_id==cmd.target_id&&
            c.slot==cmd.slot&&c.destination==cmd.destination)return true;
        return false;
    }
    bool act(const transition::Evaluation& evaluation) {
        const auto cmd=evaluation.command;
        if(!evaluation.eligible||!observed.matches(epoch,owner,revision)||!current(18000)||
            cmd.expected_epoch!=epoch||cmd.expected_actor!=owner||cmd.expected_revision!=revision)return false;
        for(int g:evaluation.wake_groups)world.remember_wake(g);
        auto& me=self();bool applied=false;
        // Stamp/origin/availability are checked immediately before the one native primitive.
        if(int(round_number())!=epoch||me.position()!=observed.self.pos||!current())return false;
        ++revision;observed.invalidate();
        switch(cmd.kind) {
        case Kind::move: applied=me.move_absolute(cmd.destination);moved=moved||applied;
            if(!applied)failed_steps.set(cmd.destination);break;
        case Kind::basic: {
            if(cmd.target_kind!=TargetKind::camp)return false;
            const auto it=world.camp_indices.find(cmd.target_id);if(it==world.camp_indices.end())return false;
            me.attack(world.camps[it->second].handle);attacked=true;applied=true;break;
        }
        case Kind::cast: case Kind::reset: {
            if(cmd.slot<0||cmd.slot>=2)return false;
            const auto a=me.abilities()[cmd.slot];
            if(cmd.kind==Kind::reset||cmd.target_kind==TargetKind::self)applied=a.use(me);
            else if(cmd.target_kind==TargetKind::ground)applied=a.use(cmd.destination);
            else if(cmd.target_kind==TargetKind::camp) {
                const auto it=world.camp_indices.find(cmd.target_id);if(it==world.camp_indices.end())return false;
                applied=a.use(world.camps[it->second].handle);
            }
            cast_spent=cast_spent||applied;break;
        }
        case Kind::recall: {
            const auto state=me.state();if(state.contains(champion_state::recall_started)||state.contains(champion_state::recall_cancelled))return false;
            me.recall();applied=true;break;
        }
        case Kind::tribute: {
            const auto state=me.state();if(state.contains(champion_state::submit_gems_started)||state.contains(champion_state::submit_gems_cancelled))return false;
            me.submit_gems(cmd.destination);applied=true;break;
        }
        case Kind::wait:return false;
        }
        if(!applied)refused.push_back(cmd);
        return applied;
    }
    bool buy_once() {
        if(!current(26000))return false;
        auto& me=self();const auto items=me.item_count();bool jungle=false,monster=false;
        for(int i=0;i<int(item_type::count);++i) {
            const item_type item{static_cast<item_type::enum_type>(i)};
            if(items[item]==0)continue;
            if(item.passive()==item_passive::jungle)jungle=true;
            if(item.passive()==item_passive::monster)monster=true;
        }
        const item_type item=!jungle?item_type::jungle_shard_of_strength:
            (!monster?item_type::monster_shard_of_strength:item_type::power_shard_of_strength);
        unsigned count=std::min(5u,me.gold_count()/item.cost());
        if(!jungle||!monster)count=std::min(count,5u-items[item]);
        if(!count||!me.can_buy(item,count)||!current())return false;
        ++revision;observed.invalidate();me.buy(item,count);return true;
    }
    transition::Evaluation service(Kind kind,vec2 destination={}) {
        auto cmd=transition::command(observed,kind);cmd.destination=destination;
        auto limits=constraints(false);limits.service_authorized=true;limits.group=-1;
        return certify(cmd,limits);
    }
    void failed_plan(int group,const nav::MovePlan& proposal) {
        if(proposal.result==nav::Result::arrived||proposal.result==nav::Result::step_ready){failures=0;return;}
        if(last_failed_task!=group){last_failed_task=group;failures=0;}
        ++failures;
        // This is a temporary planning deferral, never proof that work is impossible.
        if(failures>=4 && group>=0&&group<int(banned_until.size())) {
            banned_until[group]=epoch+8;task=-1;failures=0;
        }
    }
    std::optional<transition::Evaluation> recover_command() {
        auto quiet=certify(transition::command(observed,Kind::wait),constraints(false));
        if(spawn.is_set(observed.self.pos)) {
            if(observed.self.hp+0.01f<observed.self.max_hp)return std::nullopt;
            recovering=false;task=resume;resume=-1;return std::nullopt;
        }
        if(quiet.next_volley==0 && observed.enemies.empty())return service(Kind::recall);
        bitboard unsafe;unsafe.clear();
        for(const auto& c:observed.camps) {
            const auto* g=observed.group(c.group);
            if((c.known?c.alive:c.possible_alive)&&(c.awake||!c.known||(g&&g->possibly_woken)))
                unsafe.draw_chebyshev(c.pos,c.range);
        }
        for(const auto& e:observed.enemies)if(e.visible&&e.alive)unsafe.draw_chebyshev(e.pos,e.range+e.move_speed);
        bitboard local;local.clear();local.draw_chebyshev(observed.self.pos,10);local&=~unsafe;
        auto proposal=plan(local,nullptr,false);
        if(proposal.result!=nav::Result::step_ready)proposal=plan(spawn,nullptr,false);
        const auto cmd=move_command(proposal);if(!cmd)return std::nullopt;
        auto limits=constraints(false);limits.group=-1;
        return certify(*cmd,limits);
    }
    std::optional<transition::Evaluation> bank_command() {
        if(observed.self.gems==0){converting=false;return std::nullopt;}
        vec2 altar;const int distance=nearest_altar(observed.self.pos,&altar);
        if(distance==(1<<20))return std::nullopt; // Preserve the unconverted bag obligation.
        if(distance==0) {
            auto cmd=transition::command(observed,Kind::tribute);cmd.destination=altar;
            auto limits=constraints();limits.service_authorized=true;limits.group=-1;
            auto result=certify(cmd,limits);
            if(result.eligible)return result;
            recovering=true;resume=task;task=-1;return recover_command();
        }
        bitboard goals;goals.clear();goals.draw_chebyshev(altar,constants::submit_gems_range);
        const auto proposal=plan(goals);auto cmd=move_command(proposal);
        if(!cmd) {
            if(proposal.result==nav::Result::blocked&&++failures>=4){altar_bans.push_back({altar,epoch+16});failures=0;}
            return std::nullopt;
        }
        failures=0;return certify(*cmd,constraints());
    }
    std::optional<transition::Evaluation> work_command() {
        if(task<0)task=select();
        const auto* group=observed.group(task);if(!group)return std::nullopt;
        if(group->state==frame::GroupState::respawning && group->observed_round==epoch) {
            reveal_landings[task].clear();
            banned_until[task]=epoch+std::max(1,group->cooldown);task=-1;return std::nullopt;
        }
        if(group->state==frame::GroupState::reward && group->observed_round==epoch) {
            const auto goals=group_tiles(task);
            if(goals.is_set(observed.self.pos)) {
                capture_expectation.begin(task,epoch);
                bool contested=false;
                for(const auto& enemy:observed.enemies)if(enemy.visible&&enemy.alive)
                    for(auto i:group->members)if(observed.camps[i].spawn==enemy.pos) {
                        contested=true;auto& blockers=capture_blockers[task];
                        if(std::find(blockers.begin(),blockers.end(),enemy.handle)==blockers.end())blockers.push_back(enemy.handle);
                    }
                // Capture waiting is a real operator. An observed contest resets
                // capture progress, and an uncovered prefix cannot justify holding.
                if(contested) {
                    capture_expectation.clear();recovering=true;resume=-1;task=-1;return recover_command();
                }
                auto hold=certify(transition::command(observed,Kind::wait),constraints());
                if(!hold.eligible) {
                    capture_expectation.clear();recovering=true;resume=task;task=-1;return recover_command();
                }
                if(capture_expectation.needs_observation(epoch,constants::capture_time)) {
                    auto& inspected=capture_landings[task];
                    if(std::find(inspected.begin(),inspected.end(),observed.self.pos)==inspected.end())inspected.push_back(observed.self.pos);
                    auto inspect=goals;for(auto p:inspected)inspect.set(p,false);
                    inspect&=~observed.dynamic_blocked;
                    if(inspect.is_empty()) {
                        // Every represented landing is inspected or unavailable.
                        // Retire this unresolved episode until its lifecycle changes.
                        capture_unresolved[task]=true;capture_expectation.clear();task=-1;
                        return std::nullopt;
                    }
                    const auto proposal=plan(inspect);
                    if(const auto cmd=move_command(proposal)) {
                        auto reposition=certify(*cmd,constraints());
                        if(reposition.eligible){capture_expectation.clear();return reposition;}
                    }
                }
                return hold;
            }
            capture_expectation.clear();
            const auto proposal=plan(goals);failed_plan(task,proposal);
            if(const auto cmd=move_command(proposal))return certify(*cmd,constraints());
            return std::nullopt;
        }
        std::optional<transition::Command> movement;
        // Do not pay for a route when a visible current-task basic is already in range.
        bool in_range=false;
        for(auto i:group->members) {
            const auto& c=observed.camps[i];
            if(c.visible&&c.known&&c.alive&&transition::contains(observed.attack_targets,c.pos))in_range=true;
        }
        if(!in_range&&observed.self.can_move) {
            auto goals=group_tiles(task,observed.self.range,true);
            if(goals.is_set(observed.self.pos)) {
                // Geometric range without a personally observed target is not
                // arrival at executable work. Try a distinct observation landing.
                auto& visited=reveal_landings[task];
                if(std::find(visited.begin(),visited.end(),observed.self.pos)==visited.end())visited.push_back(observed.self.pos);
                goals=group_tiles(task,1,true);
                for(auto p:visited)goals.set(p,false);
                goals&=~observed.dynamic_blocked;
            }
            const auto proposal=plan(goals);failed_plan(task,proposal);movement=move_command(proposal);
        }
        auto nominated=task_actions::nominate(context(),constraints(),movement);
        // Only the selected useful operator is certified. A route step is a
        // task-progress witness in its own right; WAIT never wins a damage tie.
        for(const auto* cmd:{&nominated.progress,&nominated.movement,&nominated.protection})if(*cmd&&!was_refused(**cmd)) {
            auto result=certify(**cmd,constraints());if(result.eligible)return result;
        }
        // A rejected next transition pauses work; prior physical damage remains useful.
        const auto quiet=certify(transition::command(observed,Kind::wait),constraints(false));
        if(quiet.next_volley>0) {recovering=true;resume=task;task=-1;return recover_command();}
        return std::nullopt;
    }
    bool cancel_lethal_channel() {
        if(!observed.self.recalling&&!observed.self.submitting)return false;
        const auto waiting=certify(transition::command(observed,Kind::wait),constraints(false));
        if(waiting.survives_prefix||!current(28000))return false;
        if(!observed.matches(epoch,owner,revision)||int(round_number())!=epoch)return false;
        // One explicit channel cancellation, then actual observation before escape.
        if(observed.self.submitting)self().cancel_submit_gems();
        else self().cancel_recall();
        ++revision;observed.invalidate();recovering=true;resume=task;task=-1;
        return snapshot();
    }
    void turn() {
        epoch=int(round_number());setup();revision=0;moved=attacked=cast_spent=false;refused.clear();failed_steps.clear();
        startup_trace("before");
        const bool captured=snapshot();
        startup_trace("after");
        if constexpr(cfg::trace>=2)if((epoch==20||epoch==100)&&current(4000))
            std::printf("mosaic observe r%d actor%d complete%d fuel%llu\n",epoch,owner,int(captured),
                static_cast<unsigned long long>(fuel_remaining()));
        if(!captured)return;
        if(banned_until.size()!=observed.groups.size())banned_until.assign(observed.groups.size(),0);
        if(reveal_landings.size()!=observed.groups.size())reveal_landings.resize(observed.groups.size());
        if(capture_blockers.size()!=observed.groups.size())capture_blockers.resize(observed.groups.size());
        if(capture_landings.size()!=observed.groups.size())capture_landings.resize(observed.groups.size());
        if(capture_unresolved.size()!=observed.groups.size())capture_unresolved.resize(observed.groups.size(),false);
        for(const auto& group:observed.groups) {
            if(group.observed_round==epoch&&group.state!=frame::GroupState::reward) {
                capture_landings[group.id].clear();capture_unresolved[group.id]=false;
                if(capture_expectation.group==group.id)capture_expectation.clear();
            }
            auto& blockers=capture_blockers[group.id];if(blockers.empty())continue;
            if(group.observed_round==epoch&&group.state!=frame::GroupState::reward){blockers.clear();continue;}
            // An unseen contender is not presumed gone. Only a lifecycle change
            // or a fresh positive observation away from the region releases it.
            blockers.erase(std::remove_if(blockers.begin(),blockers.end(),[&](int id) {
                for(const auto& enemy:observed.enemies)if(enemy.handle==id&&enemy.visible) {
                    if(!enemy.alive)return true;
                    for(auto i:group.members)if(observed.camps[i].spawn==enemy.pos)return false;
                    return true;
                }
                return false;
            }),blockers.end());
        }
        read_intents();
        if(!observed.self.alive){task=resume=-1;capture_expectation.clear();recovering=false;converting=false;publish(false);buy_once();return;}
        if(observed.self.recalling||observed.self.submitting) {
            if(!cancel_lethal_channel()){publish(false);return;}
        }
        if(observed.self.teleporting){publish(false);return;}
        if(observed.self.stunned){publish(false);return;}
        if(observed.self.gems>last_bag){converting=true;capture_expectation.clear();}
        last_bag=observed.self.gems;
        if(spawn.is_set(observed.self.pos)) {
            if(buy_once()&&!frame::refresh_self_revision(world,observed,observed,epoch,owner,revision,moved,attacked,cast_spent))return;
            if(observed.self.hp+0.01f<observed.self.max_hp){publish(false);return;}
        }
        for(int primitive=0;primitive<6 && observed.complete && current(25000);++primitive) {
            std::optional<transition::Evaluation> next;
            if(recovering)next=recover_command();
            if(!next&&!recovering&&converting)next=bank_command();
            if(!next&&!recovering&&!converting)next=work_command();
            if(!next||next->command.kind==Kind::wait)break;
            const auto kind=next->command.kind;
            const bool self_only=kind==Kind::reset||(kind==Kind::cast&&next->command.target_kind==TargetKind::self&&
                next->command.slot>=0&&next->command.slot<2&&observed.abilities[next->command.slot].shield);
            // Bind the observed effect support before act() revokes this revision.
            // Only stationary camp effects in the implemented kit subset qualify.
            const int hit_target=next->command.target_id;
            int hit_radius=-1;
            if(kind==Kind::basic&&next->command.target_kind==TargetKind::camp)
                hit_radius=observed.self.splash_radius;
            else if(kind==Kind::cast&&next->command.slot>=0&&next->command.slot<2&&
                    (next->command.target_kind==TargetKind::camp||next->command.target_kind==TargetKind::ground)) {
                const auto& ability=observed.abilities[next->command.slot];
                if(ability.camps&&!ability.dash&&(ability.damage||ability.aoe||ability.cc))
                    hit_radius=ability.aoe?int(ability.radius):0;
            }
            const auto before_revision=revision;
            const bool applied=act(*next);
            // Admission failure applies no primitive and creates no new facts.
            // Retrying its same expensive certificate cannot create progress.
            if(!applied&&revision==before_revision)break;
            if(!observed.complete) {
                if(kind==Kind::recall||kind==Kind::tribute)break;
                if(self_only&&applied) {
                    if(!frame::refresh_self_revision(world,observed,observed,epoch,owner,revision,moved,attacked,cast_spent))break;
                } else if(applied&&hit_target>=0&&hit_radius>=0&&hit_radius<=1) {
                    if(!frame::refresh_stationary_camp_damage(world,observed,observed,epoch,owner,revision,
                        hit_target,hit_radius,moved,attacked,cast_spent))break;
                } else if(!snapshot())break;
            }
            if(!applied && refused.size()>=2)break;
        }
        publish(!recovering&&!converting&&task>=0);
        if constexpr(cfg::trace)if(epoch%50==0&&current(6000))
            std::printf("mosaic frame r%d task%d revision%llu fuel%llu\n",epoch,task,
                static_cast<unsigned long long>(revision),static_cast<unsigned long long>(fuel_remaining()));
    }
};
}
void play_draft() {
    const std::array order{champion_draft::varg,champion_draft::marcella,champion_draft::ryth,
        champion_draft::vendetta,champion_draft::vesper_vale};
    const auto chosen=draft::current_draft(team::ally);
    for(auto pick:order)if(std::find(chosen.begin(),chosen.end(),pick)==chosen.end()){draft::pick(pick);return;}
}
void play_turn(){static Actor actor;actor.turn();}
}
