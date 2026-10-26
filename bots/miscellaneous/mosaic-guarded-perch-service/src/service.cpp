// Explicit selected-service program. Planning data and finite nomination order
// persist; only a new selected packet/certificate grants each physical command.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <vector>
#include "config.h"
#include "service.h"
#include "candidate_observation.h"
#include "conversion_observation.h"
#include "proposal_lifecycle.h"
#include "route_observation.h"
#include "service_cursor.h"
#include "service_route.h"
#include "service_program.h"

namespace mosaic {
namespace {
using namespace cambc;
using transition::Kind;
using transition::TargetKind;
using program::Phase;
using program::Purpose;
struct Actor {
    frame::SourceWorld world;
    candidate::Service observer;
    selected::Packet packet;
    route_observation::Cache planning_cache;
    route_observation::Packet planning;
    route_observation::SelfPacket guide;
    conversion_observation::Cache conversion_cache;
    conversion::History channel_history;
    proposal::Lifecycle life;
    selected::Stamp stamp;
    candidate::History history;
    Phase phase{Phase::registration};Purpose purpose{Purpose::work};
    frame::Self nominal;
    bool nominal_known{},life_ready{},layout_started{},layout_ready{},goals_started{},goals_exported{};
    int actor{-1},job{-1},bag{},gold{},discovery_cursor{},scanned_jobs{},width{},height{};
    std::uint64_t serial{},goal_identity{},body_generation{},stat_generation{},occupancy_generation{};
    std::size_t layout_cursor{},respawn_cursor{},goal_cursor{},goal_flat_cursor{};
    std::vector<int> respawn_times;
    std::vector<vec2> altars,spawns;
    std::optional<bitboard> quiet_board,goal_board;
    std::vector<std::uint8_t> quiet_packed;
    std::vector<std::uint8_t> goal_packed;
    std::vector<service_route::Point> goal_points;
    std::optional<service_route::Compiler> route;
    // Trivial workspace has no eager per-element constructors/clear. The route
    // compiler initializes each used cell under its retained work quota.
    std::array<service_route::Node,service_route::max_cells> nodes;
    std::array<service_route::Tile,service_route::max_cells> queue;
    std::array<service_route::Candidate,service_route::family_capacity> endpoints;
    std::array<service_cursor::Raw,service_cursor::Cursor::capacity> order;
    std::size_t order_count{};
    service_cursor::Cursor cursor;
    std::optional<vec2> tribute_altar;
    int submitted_before{},tribute_bag{},tribute_epoch{-1};
    vec2 tribute_origin{};
    unsigned model_failures{},unattributed_loss{},bank_candidates{};
    bool physical_rebase{};
    bool awaiting_graph_change{};
    std::array<std::uint8_t,service_route::family_capacity> exhausted_local{};
    vec2 exhausted_origin{};int exhausted_speed{};
    conversion::Kind active_channel{conversion::Kind::recall_start};
    std::size_t altar_cursor{};
    std::uint64_t conversion_rejection{};
    // Only the siege arm uses these conditional absence intervals and perch
    // exclusions. They nominate a graph; fresh primitive admission remains.
    std::vector<int> absent_until;
    struct Perch {int group;vec2 point;std::uint64_t cycle,stats;};
    std::vector<Perch> rejected_perches;
    bool mask_started{};std::size_t mask_cursor{};
    int siege_reach{-1};
    bool absent(int group)const {
        return group>=0&&std::size_t(group)<absent_until.size()&&stamp.epoch<absent_until[std::size_t(group)];
    }
    void remember_absence(const frame::Group& fact) {
        if constexpr(cfg::siege) {
            if(fact.id<0||std::size_t(fact.id)>=absent_until.size()||fact.observed_round!=stamp.epoch)return;
            int count=0;
            if(fact.state==frame::GroupState::reward)count=respawn_times[std::size_t(fact.id)];
            if(fact.state==frame::GroupState::respawning)count=fact.cooldown;
            if(count>=3)absent_until[std::size_t(fact.id)]=std::max(absent_until[std::size_t(fact.id)],stamp.epoch+count-1);
        }
    }
    int work_reach() {
        if constexpr(!cfg::siege)return nominal.range;
        if(siege_reach>=0)return siege_reach;
        siege_reach=nominal.range;
        for(const auto& ability:self().abilities())if(ability.info())std::visit([&](const auto& info) {
            using T=std::decay_t<decltype(info)>;
            if constexpr(std::is_same_v<T,ability_damage>||std::is_same_v<T,ability_cc>) {
                if(info.valid_targets.contains(ability_target_mask::camps))siege_reach=std::max(siege_reach,int(info.range));
            } else if constexpr(std::is_same_v<T,ability_aoe_damage>) {
                if(info.valid_targets.contains(ability_target_mask::ground))siege_reach=std::max(siege_reach,int(info.range+info.radius));
            }
        },*ability.info());
        return siege_reach;
    }
    bool refresh_route_mask() {
        if constexpr(!cfg::siege)return true;
        if(!mask_started){quiet_board=*world.static_blocked;mask_cursor=0;mask_started=true;}
        while(mask_cursor<world.camps.size()&&current(45000)) {
            const auto& camp=world.camps[mask_cursor++].design;
            if(camp.design_strength>0&&!absent(camp.group))quiet_board->draw_chebyshev(camp.spawn,camp.range);
        }
        if(mask_cursor<world.camps.size()||!current(45000))return false;
        quiet_board->export_linear_buffer(quiet_packed);return true;
    }

    bool current(std::uint64_t reserve=0)const{return candidate::current(stamp,reserve);}
    bool quiet_position(vec2 point)const {
        if(!layout_ready||point.x<0||point.y<0||point.x>=width||point.y>=height)return false;
        if constexpr(cfg::siege) {
            if(world.static_blocked->is_set(point))return false;
            const auto support=world.spatial_support.borrow().index->threats(point.x,point.y);
            if(!support.complete)return false;
            for(auto i:support)if(world.camps[i].design.design_strength>0&&!absent(world.camps[i].design.group))return false;
            return true;
        } else {
            const auto bit=std::size_t(point.y)*std::size_t(width)+std::size_t(point.x);
            return !(quiet_packed[bit/8]&(std::uint8_t(1u)<<(bit%8)));
        }
    }
    void note(const char* text)const {
        if constexpr(cfg::trace==1)if(current(5000))
            std::printf("mosaic phase %s r%d actor%d phase%d purpose%d job%d rank%u fuel%llu\n",text,
                stamp.epoch,actor,int(phase),int(purpose),job,cursor.peek().selection?cursor.peek().selection->index:0,
                static_cast<unsigned long long>(fuel_remaining()));
    }
    transition::Command command(Kind kind)const {
        transition::Command result;result.kind=kind;result.expected_actor=actor;
        result.expected_epoch=stamp.epoch;result.expected_revision=stamp.revision;return result;
    }
    selected::Request request(transition::Command value) {
        value.expected_actor=actor;value.expected_epoch=stamp.epoch;value.expected_revision=stamp.revision;
        return {value,++serial};
    }
    transition::Constraints constraints()const {
        transition::Constraints result;result.group=purpose==Purpose::work?job:-1;
        result.allow_unmodelled_foes=true;return result;
    }
    service_cursor::Key key(const frame::Self& body)const {
        return {actor,{body.pos.x,body.pos.y},{0,body.move_speed,body.range,body.splash_radius},int(purpose),job,
            job>=0&&life_ready?life.group_generation(std::size_t(job)):0,goal_identity,body_generation,stat_generation};
    }
    bool proposed(std::size_t index)const{return life_ready&&life.may_propose(index,stamp.epoch);}
    void begin_route(Purpose next) {
        purpose=next;phase=Phase::route_capture;route.reset();goals_started=false;
        goals_exported=false;goal_flat_cursor=0;awaiting_graph_change=false;
        goal_cursor=0;goal_points.clear();order_count=0;++goal_identity;
        if constexpr(cfg::siege)mask_started=false;
    }
    void actual_body(const frame::Self& body) {
        if(nominal_known&&(nominal.max_hp!=body.max_hp||nominal.strength!=body.strength||
            nominal.focus!=body.focus||nominal.range!=body.range||nominal.move_speed!=body.move_speed||
            nominal.splash_radius!=body.splash_radius))++stat_generation;
        if(body.gems<bag) {
            // This label alone never credits a bank. Saved native gemSubmission
            // is the final attribution; death/theft/drop also retire a residual.
            if(phase==Phase::channel&&active_channel==conversion::Kind::tribute_start&&body.alive&&!body.submitting&&body.pos==tribute_origin&&
               bag-body.gems==tribute_bag&&stamp.epoch>tribute_epoch&&current(20000)&&
               int(global_submitted_gems(team::ally))-submitted_before>=tribute_bag)++bank_candidates;
            else unattributed_loss+=unsigned(bag-body.gems);
            phase=Phase::recall;purpose=Purpose::home;route.reset();++goal_identity;
        }
        if(body.gems>bag){altar_cursor=0;begin_route(Purpose::bank);}
        if(body.gold>gold&&body.gems==0&&phase==Phase::capture){phase=Phase::recall;purpose=Purpose::home;}
        bag=body.gems;gold=body.gold;nominal=body;nominal_known=true;
    }
    bool self_guide() {
        if(route_observation::capture_self(world,planning_cache,stamp,guide)!=route_observation::Status::ready||
           !current(0)||!guide.stamp.matches(stamp))return false;
        actual_body(guide.body);
        // All compute waits are physical waits. This initial operator excludes
        // all potential camp ranges; a newly visible foe is unsupported here.
        if(!guide.body.alive||!quiet_position(guide.body.pos))return false;
        const auto foes=vision::champion_tiles(team::enemy);
        return current(20000)&&foes.is_empty()&&current(20000);
    }
    bool planning_capture() {
        if(route_observation::capture(world,planning_cache,stamp,planning)!=route_observation::Status::ready||
           !current(0)||!planning.stamp.matches(stamp))return false;
        actual_body(planning.body);
        return planning.complete&&planning.body.alive&&!planning.has_visible_enemy&&quiet_position(planning.body.pos);
    }
    bool local_graph(std::array<std::uint8_t,service_route::family_capacity>& out,vec2 origin,int speed)const {
        if(speed<0||speed>4||!planning.complete)return false;
        std::size_t n=0;
        for(int y=-speed;y<=speed;++y)for(int x=-speed;x<=speed;++x) {
            const auto tile=planning.tile(origin.x+x,origin.y+y);
            out[n++]=std::uint8_t(tile);
        }
        for(;n<out.size();++n)out[n]=0;
        return true;
    }
    void await_graph_change() {
        exhausted_origin=nominal.pos;exhausted_speed=nominal.move_speed;
        if(!local_graph(exhausted_local,exhausted_origin,exhausted_speed)){phase=Phase::unsupported;return;}
        // Preserve a finite-family exhaustion across inputs. A new observation
        // nonce alone cannot restart the first rejected nominee. Only a real
        // local graph change, actual phase/body change, or target lifecycle
        // change permits a different family.
        awaiting_graph_change=true;route.reset();phase=Phase::route_capture;
        if constexpr(cfg::siege)mask_started=false;
    }
    void capture_route() {
        if(!planning_capture()||phase!=Phase::route_capture)return;
        if(awaiting_graph_change) {
            std::array<std::uint8_t,service_route::family_capacity> actual;
            if(!local_graph(actual,exhausted_origin,exhausted_speed)){phase=Phase::unsupported;return;}
            if(nominal.pos==exhausted_origin&&nominal.move_speed==exhausted_speed&&actual==exhausted_local) {
                if constexpr(cfg::siege) {
                    // Exhausting one model family releases the actor to other
                    // work; an unresolved model question is never a wait proof.
                    job=-1;phase=Phase::discovery;awaiting_graph_change=false;
                }
                return;
            }
            awaiting_graph_change=false;++goal_identity;
        }
        if(!refresh_route_mask())return;
        phase=Phase::goals;goals_started=false;goals_exported=false;goal_flat_cursor=0;goal_points.clear();
    }
    void ingest() {
        if(!life_ready||!packet.matches(stamp,packet.request))return;
        bool changed=false;
        for(const auto& row:packet.rows)if(row.state_fresh&&row.camp.known)
            changed=changed||(row.camp.alive&&life.marked(row.index))||(!row.camp.alive&&!life.marked(row.index));
        life.observe(packet,stamp,respawn_times);
        if constexpr(cfg::siege)for(const auto& fact:packet.groups)remember_absence(fact);
        if(changed&&purpose==Purpose::work&&phase==Phase::dispatch)begin_route(Purpose::work);
    }
    std::optional<selected::Assessment> observe(const selected::Request& value) {
        if(candidate::capture(world,observer,packet,stamp,value,history)!=candidate::Status::ready||!current(0))return std::nullopt;
        actual_body(packet.self);ingest();return selected::assess(packet,value,constraints());
    }
    bool quiet_packet(const selected::Assessment& value)const {
        return value.result.origin_wait.complete&&value.result.origin_wait.safe&&value.result.origin_wait.next_volley==0&&
            std::none_of(packet.enemies.begin(),packet.enemies.end(),[](const auto& foe){return foe.visible&&foe.alive;});
    }
    void setup_layout() {
        if(!layout_started) {
            if(!current(60000))return;
            width=world.static_blocked->width();height=world.static_blocked->height();
            quiet_board=*world.static_blocked;goal_board.emplace();
            altars=map::altars().positions();spawns=map::spawn_region(team::ally).positions();
            respawn_times.reserve(world.groups.size());layout_started=true;return;
        }
        while(layout_cursor<world.camps.size()&&current(45000)) {
            const auto& design=world.camps[layout_cursor++].design;
            if(design.design_strength>0)quiet_board->draw_chebyshev(design.spawn,design.range);
        }
        if(layout_cursor<world.camps.size())return;
        while(respawn_cursor<world.groups.size()&&current(45000)) {
            const auto duration=world.groups[respawn_cursor].handle.max_respawn_timer();
            respawn_times.push_back(duration);++respawn_cursor;
        }
        if(respawn_cursor<world.groups.size()||!current(45000))return;
        if(!life_ready){life_ready=life.bind(world.camps,world.spatial_support.borrow(),world.groups.size());
            if(!life_ready)phase=Phase::unsupported;return;}
        quiet_packed.resize(route_observation::packed_size(width,height));quiet_board->export_linear_buffer(quiet_packed);
        if constexpr(cfg::siege)absent_until.assign(world.groups.size(),-1);
        layout_ready=true;phase=Phase::discovery;
    }
    void discover() {
        if(!planning_capture()||phase!=Phase::discovery)return;
        candidate::lifecycle_batch(world,observer,stamp,2);
        if(!current(25000))return;
        // Finite semantic job enumeration; a model-blocked job advances this
        // cursor rather than being recreated first by the next observation.
        for(unsigned examined=0;examined<world.groups.size();++examined) {
            const int next=discovery_cursor++%int(world.groups.size());
            const auto& design=world.groups[std::size_t(next)].design;
            if(design.gems<=0)continue;
            const auto& memory=world.memory[std::size_t(next)];
            if(memory.state==frame::GroupState::respawning&&stamp.epoch<memory.observed_round+memory.cooldown)continue;
            bool harmless=true;
            for(auto index:design.members)harmless=harmless&&world.camps[index].design.design_strength==0;
            if(!harmless&&memory.state!=frame::GroupState::reward&&work_reach()<=nominal.range)continue;
            if constexpr(cfg::siege)if(memory.observed_round==stamp.epoch) {
                frame::Group fact;fact.id=next;fact.state=memory.state;fact.cooldown=memory.cooldown;fact.observed_round=stamp.epoch;
                remember_absence(fact);
            }
            job=next;begin_route(memory.state==frame::GroupState::reward?Purpose::capture:Purpose::work);return;
        }
        note("no-supported-job");
    }
    void build_goals() {
        if(!goals_started){goal_board->clear();goals_started=true;goal_cursor=0;goals_exported=false;goal_flat_cursor=0;return;}
        if(purpose==Purpose::work||purpose==Purpose::capture) {
            if(job<0){phase=Phase::discovery;return;}
            const auto& members=world.groups[std::size_t(job)].design.members;
            while(goal_cursor<members.size()&&current(45000)) {
                const auto index=members[goal_cursor++];
                if(purpose==Purpose::work&&!proposed(index))continue;
                goal_board->draw_chebyshev(world.camps[index].design.spawn,purpose==Purpose::capture?0:work_reach());
            }
            if(goal_cursor<members.size())return;
        } else {
            const auto& positions=purpose==Purpose::bank?altars:spawns;
            while(goal_cursor<positions.size()&&current(45000)) {
                goal_board->draw_chebyshev(positions[goal_cursor++],purpose==Purpose::bank?constants::submit_gems_range:0);
            }
            if(goal_cursor<positions.size())return;
        }
        if(!current(45000))return;
        if(!goals_exported) {
            goal_packed.resize(route_observation::packed_size(width,height));
            goal_board->export_linear_buffer(goal_packed);goals_exported=true;return;
        }
        while(goal_flat_cursor<std::size_t(width*height)&&current(45000)) {
            const auto end=std::min(goal_flat_cursor+(cfg::siege?8u:64u),std::size_t(width*height));
            for(;goal_flat_cursor<end;++goal_flat_cursor)
                if(goal_packed[goal_flat_cursor/8]&(std::uint8_t(1u)<<(goal_flat_cursor%8))) {
                    const vec2 point{int(goal_flat_cursor%std::size_t(width)),int(goal_flat_cursor/std::size_t(width))};
                    bool rejected=false;
                    if constexpr(cfg::siege)if(purpose==Purpose::work)
                        rejected=std::any_of(rejected_perches.begin(),rejected_perches.end(),[&](const Perch& p){return p.group==job&&p.point==point&&p.cycle==life.group_generation(std::size_t(job))&&p.stats==stat_generation;});
                    if(!rejected)goal_points.push_back({point.x,point.y});
                }
        }
        if(goal_flat_cursor<std::size_t(width*height)||!current(45000))return;
        route.emplace();
        const service_route::Input input{width,height,{planning.body.pos.x,planning.body.pos.y},unsigned(planning.body.move_speed),
            quiet_packed,planning.observed_blocked,goal_points,goal_identity,std::uint64_t(planning.stamp.epoch)};
        if(!route->begin(input,{nodes,queue,endpoints})){phase=Phase::discovery;job=-1;return;}
        phase=Phase::compile;
    }
    void compile_route() {
        if(!route){phase=Phase::route_capture;return;}
        if(stamp.revision==0) {
            if(!self_guide()||phase!=Phase::compile)return;
            // While global compilation is unfinished, the frozen planning body
            // remains the conditional origin. A real displacement needs rebase.
            const auto expected=route->snapshot().field_complete?route->family().origin:
                service_route::Point{planning.body.pos.x,planning.body.pos.y};
            if(nominal.pos.x!=expected.x||nominal.pos.y!=expected.y) {
                begin_route(purpose);return;
            }
        }
        for(unsigned batches=0;batches<32&&current(45000);++batches) {
            const auto status=route->advance(256);
            if(!current(0))return;
            if(status==service_route::Status::Working)continue;
            if(status==service_route::Status::Ready){phase=Phase::dispatch;order_count=0;return;}
            ++model_failures;
            if(purpose==Purpose::work||purpose==Purpose::capture){job=-1;phase=Phase::discovery;}
            else phase=Phase::unsupported;
            note("conditional-route-unavailable");return;
        }
    }
    bool make_order() {
        if(!route||!route->family().complete)return false;
        const auto family=route->family();
        if(family.origin.x!=nominal.pos.x||family.origin.y!=nominal.pos.y) {
            if(!route->reposition({nominal.pos.x,nominal.pos.y},unsigned(nominal.move_speed),goal_identity)) {
                begin_route(purpose);return false;
            }
            phase=Phase::compile;return false;
        }
        order_count=0;
        const auto append=[&](service_cursor::Raw value) {
            if(order_count>=order.size())return false;
            order[order_count++]=value;return true;
        };
        if(purpose==Purpose::work&&job>=0) {
            for(auto index:world.groups[std::size_t(job)].design.members) {
                if(proposed(index)&&transition::distance(nominal.pos,world.camps[index].design.spawn)<=nominal.range)
                    if(!append(service_cursor::Raw::basic(int(index)))){phase=Phase::unsupported;return false;}
            }
        }
        for(const auto& endpoint:family.entries)if(endpoint.decreases)
            if(!append(service_cursor::Raw::move({endpoint.endpoint.x,endpoint.endpoint.y}))) {
                phase=Phase::unsupported;return false;
            }
        if(order_count==0) {
            if(family.origin.x==nominal.pos.x&&family.origin.y==nominal.pos.y&&route->snapshot().at_goal) {
                if constexpr(cfg::siege) {
                    if(purpose==Purpose::work&&work_reach()>nominal.range){phase=Phase::inspect;return false;}
                }
                phase=purpose==Purpose::work||purpose==Purpose::capture?Phase::inspect:
                    (purpose==Purpose::bank?Phase::tribute:Phase::home);
            } else await_graph_change();
            return false;
        }
        const service_cursor::Catalogue catalogue{{order.data(),order_count},true};
        if(physical_rebase) {
            cursor.rebase(key(nominal),catalogue,service_cursor::RebaseReason::physical_phase_committed);
            physical_rebase=false;
        } else cursor.bind(key(nominal),catalogue);
        return true;
    }
    // 0 no useful spell, 1 physical cast, 2 cooldown wait, 3 replan/query.
    int siege_spell() {
        if constexpr(!cfg::siege)return 0;
        if(purpose!=Purpose::work||job<0||!route||!route->snapshot().at_goal)return 0;
        auto basis=wait_packet();
        if(!basis||phase!=Phase::dispatch||!quiet_packet(*basis)||!current(45000))return 3;
        if(!packet.self.can_cast||packet.self.stunned)return 2;
        const auto visible=vision::visible_tiles();
        int target=-1;float target_hp=-1;bool saw_dead=false;
        for(auto index:world.groups[std::size_t(job)].design.members) {
            if(!current(35000))return 3;
            const auto& source=world.camps[index];
            if(!proposed(index)||!visible.is_set(source.design.spawn))continue;
            const auto state=source.handle.state();
            if(!state.is_alive()) {
                auto cmd=command(Kind::basic);cmd.target_kind=TargetKind::camp;cmd.target_index=int(index);cmd.target_id=source.design.handle;
                observe(request(cmd));saw_dead=true;break;
            }
            const float hp=source.handle.health();
            if(hp>target_hp){target=int(index);target_hp=hp;}
        }
        if(saw_dead||target<0){phase=Phase::inspect;return 3;}
        bool cooling=false,usable=false;
        for(const auto& ability:self().abilities()) {
            if(!ability.info())continue;
            float range=0,radius=0;bool direct=false,area=false,exclusive=false;
            std::visit([&](const auto& info) {
                using T=std::decay_t<decltype(info)>;
                exclusive=info.exclusive_with_movement;
                if constexpr(std::is_same_v<T,ability_damage>||std::is_same_v<T,ability_cc>) {
                    if(info.valid_targets.contains(ability_target_mask::camps)){range=info.range;direct=true;}
                } else if constexpr(std::is_same_v<T,ability_aoe_damage>) {
                    if(info.valid_targets.contains(ability_target_mask::ground)){range=info.range;radius=info.radius;area=true;}
                }
            },*ability.info());
            if(!direct&&!area)continue;
            usable=true;
            if((exclusive&&history.moved)||!ability.is_ready()){cooling=true;continue;}
            if(target<0)continue;
            // Nominate a currently visible living member this spell can reach.
            int best=-1;vec2 center{};float value=-1;
            for(auto index:world.groups[std::size_t(job)].design.members) {
                if(!current(30000))return 3;
                const auto& source=world.camps[index];
                if(!proposed(index)||!visible.is_set(source.design.spawn)||!source.handle.state().is_alive())continue;
                const auto position=source.design.spawn;
                if(direct&&transition::distance(packet.self.pos,position)<=range) {
                    const float hp=source.handle.health();
                    if(hp>value){value=hp;best=int(index);center=position;}
                } else if(area) {
                    for(int y=-int(radius);y<=int(radius);++y)for(int x=-int(radius);x<=int(radius);++x) {
                        const vec2 point{position.x+x,position.y+y};
                        if(point.x<0||point.y<0||point.x>=width||point.y>=height||transition::distance(packet.self.pos,point)>range||
                           !visible.is_set(point)||world.static_blocked->is_set(point))continue;
                        // This is a useful-damage nomination, never a corpse
                        // prediction. Actual life is observed again next input.
                        float score=0;
                        for(auto other:world.groups[std::size_t(job)].design.members)
                            if(proposed(other)&&transition::distance(world.camps[other].design.spawn,point)<=radius)score+=1;
                        if(score>value){value=score;best=int(index);center=point;}
                    }
                }
            }
            if(best<0||!current(20000))continue;
            bool applied=direct?ability.use(world.camps[std::size_t(best)].handle):ability.use(center);
            if(applied){++stamp.revision;packet.complete=packet.origin_complete=false;history.cast_spent=true;world.remember_wake(job);return 1;}
            return 3;
        }
        if(cooling)return 2;
        if(usable) {
            if(rejected_perches.size()>=64)rejected_perches.erase(rejected_perches.begin());
            rejected_perches.push_back({job,packet.self.pos,life.group_generation(std::size_t(job)),stat_generation});
            begin_route(Purpose::work);return 3;
        }
        phase=Phase::inspect;return 3;
    }
    bool dispatch() {
        if constexpr(cfg::siege) {
            const auto spell=siege_spell();
            if(spell==1)return current(52000);
            if(spell>=2)return false;
        }
        if(!nominal_known||!make_order()||phase!=Phase::dispatch)return false;
        const auto view=cursor.peek();
        if(!view.selection) {
            // Exhaustion means a finite family was examined. A fresh observed
            // graph, not a round/nonce, must justify its next explicit rebase.
            if(view.status==service_cursor::Status::exhausted)await_graph_change();
            else phase=Phase::unsupported;
            return false;
        }
        const auto chosen=*view.selection;
        const auto expected_key=key(nominal);
        auto cmd=command(chosen.raw.kind==service_cursor::Kind::move?Kind::move:Kind::basic);
        if(cmd.kind==Kind::move)cmd.destination={chosen.raw.destination.x,chosen.raw.destination.y};
        else {
            const auto index=std::size_t(chosen.raw.camp_index);
            if(index>=world.camps.size()){phase=Phase::unsupported;return false;}
            cmd.target_kind=TargetKind::camp;cmd.target_index=int(index);cmd.target_id=world.camps[index].design.handle;
        }
        const auto value=request(cmd);
        auto decision=observe(value);
        if(!decision) {
            cursor.report(chosen,service_cursor::Outcome::partial_capture);return false;
        }
        if(phase!=Phase::dispatch||key(packet.self)!=expected_key) {
            if(phase==Phase::dispatch)begin_route(purpose);
            return false;
        }
        cursor.acknowledge_fresh_packet(key(packet.self));
        if(packet.self.stunned||(cmd.kind==Kind::move&&!packet.self.can_move)||
           (cmd.kind==Kind::basic&&!packet.self.can_attack)) {
            cursor.report(chosen,service_cursor::Outcome::temporal_deferred);return false;
        }
        if(!quiet_packet(*decision)||decision->result.action.next_volley>0) {
            cursor.report(chosen,service_cursor::Outcome::complete_unsupported);return current(52000);
        }
        if(!decision->certificate) {
            cursor.report(chosen,decision->result.status==selected::Status::incomplete?
                service_cursor::Outcome::assessed_incomplete:service_cursor::Outcome::complete_rejected);
            return current(52000);
        }
        if(!current(19000)||!packet.matches(stamp,value)) {
            cursor.report(chosen,service_cursor::Outcome::admission_deferred);return false;
        }
        const auto granted=decision->certificate->consume(stamp,value,world.spatial_support.borrow(),packet.self.pos,packet.nonce);
        if(!granted||!current(18000)) {
            cursor.report(chosen,service_cursor::Outcome::admission_deferred);return false;
        }
        ++stamp.revision;packet.complete=packet.origin_complete=false;
        bool applied=false;
        try {
            if(cmd.kind==Kind::move) {
                applied=self().move_absolute(cmd.destination);
                if(applied){nominal.pos=cmd.destination;history.moved=true;}
            } else {
                const auto index=std::size_t(cmd.target_index);world.remember_wake(world.camps[index].design.group);
                self().attack(world.camps[index].handle);applied=true;history.attacked=true;
            }
        } catch(...) {
            cursor.report(chosen,service_cursor::Outcome::uncertain_effect);
            begin_route(purpose);throw;
        }
        cursor.report(chosen,applied?service_cursor::Outcome::applied:service_cursor::Outcome::sdk_refused);
        if(!applied)return current(52000);
        physical_rebase=true;
        if(cmd.kind==Kind::basic)return false;
        if(!route->reposition({nominal.pos.x,nominal.pos.y},unsigned(nominal.move_speed),goal_identity)) {
            begin_route(purpose);return false;
        }
        phase=Phase::compile;
        // A bounded move -> fresh-basic continuation may fit this same input.
        // No certificate or predicted target HP is carried through the move.
        return current(52000);
    }
    std::optional<selected::Assessment> wait_packet() {
        return observe(request(command(Kind::wait)));
    }
    void inspect() {
        auto basis=wait_packet();if(!basis||phase!=Phase::inspect||!quiet_packet(*basis)||job<0)return;
        const auto before=observer.work.group_reads;
        if(candidate::lifecycle_batch(world,observer,stamp,1,job)!=candidate::Status::ready||
           observer.work.group_reads==before||!current(20000))return;
        const auto& memory=world.memory[std::size_t(job)];frame::Group fact;
        fact.id=job;fact.state=memory.state;fact.cooldown=memory.cooldown;fact.observed_round=memory.observed_round;
        life.observe_group(fact,stamp,stamp);remember_absence(fact);
        if(memory.state==frame::GroupState::reward) {
            const auto& members=world.groups[std::size_t(job)].design.members;
            const bool occupied=std::any_of(members.begin(),members.end(),[&](auto i){return world.camps[i].design.spawn==packet.self.pos;});
            if(occupied){purpose=Purpose::capture;phase=Phase::capture;return;}
            begin_route(Purpose::capture);return;
        }
        if(memory.state==frame::GroupState::respawning){job=-1;phase=Phase::discovery;return;}
        begin_route(Purpose::work);
    }
    bool start_conversion(conversion::Kind kind,vec2 altar={}) {
        conversion_rejection=0;
        if(!current(32000)||!packet.complete||packet.request.command.kind!=Kind::wait)return false;
        const conversion::Request value{kind,stamp,++serial,altar};conversion::Facts facts;
        if(conversion_observation::capture(world,conversion_cache,packet,value,channel_history,facts)!=
            conversion_observation::Status::ready||!current(20000))return false;
        auto decision=conversion::assess(packet,facts,value);
        conversion_rejection=decision.result.flags;
        if(!decision.certificate||decision.result.requires_egress||!current(19000))return false;
        int before=0;
        if(kind==conversion::Kind::tribute_start)before=int(global_submitted_gems(team::ally));
        if(!current(19000)||!packet.matches(stamp,packet.request))return false;
        const auto grant=decision.certificate->consume(stamp,value,packet.request,world.spatial_support.borrow(),packet.self.pos,packet.nonce);
        if(!grant||!current(18000))return false;
        if(kind==conversion::Kind::tribute_start) {
            submitted_before=before;tribute_bag=packet.self.gems;
            tribute_origin=packet.self.pos;tribute_epoch=stamp.epoch;
        }
        ++stamp.revision;packet.complete=packet.origin_complete=false;
        active_channel=kind;
        if(kind==conversion::Kind::recall_start)self().recall();
        else if(kind==conversion::Kind::tribute_start)self().submit_gems(altar);
        else return false;
        if(const auto next=conversion::advanced(channel_history,value))channel_history=*next;
        phase=Phase::channel;return true;
    }
    void service_channel() {
        auto basis=wait_packet();if(!basis||!quiet_packet(*basis))return;
        if(phase==Phase::recall) {start_conversion(conversion::Kind::recall_start);return;}
        if(phase==Phase::channel) {
            if(packet.self.recalling||packet.self.submitting)return;
            if(active_channel==conversion::Kind::recall_start){phase=Phase::home;return;}
            if(packet.self.gems>0)begin_route(Purpose::bank);
            else {phase=Phase::recall;purpose=Purpose::home;}
            return;
        }
        if(phase==Phase::home) {
            const bool at_home=std::find(spawns.begin(),spawns.end(),packet.self.pos)!=spawns.end();
            if(!at_home){phase=Phase::recall;return;}
            if(packet.self.hp<packet.self.max_hp)return;
            if constexpr(cfg::siege) {
                // Invest the actual banked/earned gold in the operators this
                // body executes. Perch casters buy Focus; basic workers Strength.
                const auto item=work_reach()>nominal.range?item_type::power_shard_of_focus:item_type::power_shard_of_strength;
                const auto count=self().gold_count()/item.cost();
                if(count&&current(22000)&&self().can_buy(item,count)){self().buy(item,count);++stamp.revision;packet.complete=false;}
            }
            job=-1;purpose=Purpose::work;phase=Phase::discovery;scanned_jobs=0;return;
        }
        if(phase==Phase::tribute) {
            while(altar_cursor<altars.size()) {
                const auto altar=altars[altar_cursor];
                if(transition::distance(altar,packet.self.pos)>constants::submit_gems_range){++altar_cursor;continue;}
                if(start_conversion(conversion::Kind::tribute_start,altar))return;
                if(!(conversion_rejection&(conversion::altar_locked|conversion::bad_altar)))return;
                ++altar_cursor;
            }
            // Every in-range alternative was actually rejected, or none was
            // represented. This v1 class does not invent an unlock/egress word.
            // It reports capability failure instead of retrying a first lock.
            phase=Phase::unsupported;
        }
    }
    struct Operations {
        Actor& actor;
        bool current(){return actor.current(18000);}
        void discovery(){actor.discover();}
        void route_capture(){actor.capture_route();}
        void goals(){actor.build_goals();}
        bool compile(){actor.compile_route();return actor.phase==Phase::dispatch&&actor.stamp.revision>0&&actor.current(52000);}
        bool dispatch(){return actor.dispatch();}
        void inspect(){actor.inspect();}
        void capture(){auto basis=actor.wait_packet();if(basis&&actor.phase==Phase::capture&&actor.quiet_packet(*basis))actor.note("capture-hold");}
        void channel(){actor.service_channel();}
        void unsupported(){
            actor.note("unsupported");
            if constexpr(cfg::siege) {
                if(actor.bag>0){actor.altar_cursor=0;actor.begin_route(Purpose::bank);}
                else {actor.job=-1;actor.route.reset();actor.phase=Phase::discovery;}
            }
        }
    };
    // Bounded empirical fallback when a routing absence expires while exposed.
    // It chooses one actual reachable endpoint by catalog heat/depth, then
    // recompiles from the actual effect. This is a declared risk response, not
    // a universal hidden-actor safety guarantee.
    bool expired_absence_egress() {
        if constexpr(!cfg::siege)return false;
        if(!layout_ready||!current(50000))return false;
        const auto body=native_observation::read_champion(self(),{stamp.epoch,actor,stamp.revision});
        const auto origin=body.position();
        if(quiet_position(origin))return false;
        const auto state=body.state();
        if(state.contains(champion_state::recalling)){self().cancel_recall();++stamp.revision;}
        if(state.contains(champion_state::submitting_gems)){self().cancel_submit_gems();++stamp.revision;}
        if(!self().can_move())return true;
        auto blocked=*world.static_blocked|vision::champion_tiles()|vision::camp_tiles()|vision::teleport_tiles();
        blocked.set(origin,false);
        const auto candidates=blocked.reachable_tiles(origin,body.move_speed()).positions();
        const auto score=[&](vec2 point) {
            float heat=0,depth=0;
            const auto support=world.spatial_support.borrow().index->threats(point.x,point.y);
            for(auto i:support) {
                const auto& camp=world.camps[i].design;
                if(camp.design_strength>0&&!absent(camp.group)) {
                    heat+=camp.design_strength;
                    depth+=camp.design_strength*float(camp.range+1-transition::distance(point,camp.spawn));
                }
            }
            int home=10000;for(const auto spawn:spawns)home=std::min(home,transition::distance(point,spawn));
            return heat*10000+depth*10+float(home);
        };
        vec2 chosen=origin;float best=score(origin);
        for(const auto point:candidates) {
            if(!current(25000))break;
            const auto value=score(point);
            if(value<best){best=value;chosen=point;}
        }
        if(chosen!=origin&&current(18000)) {
            if(self().move_absolute(chosen)) {
                ++stamp.revision;nominal.pos=chosen;history.moved=true;
                begin_route(purpose);
            }
        }
        return true;
    }
    void turn() {
        if(actor<0){actor=int(std::uint32_t(handle_champion_t(self())));world.set_spatial_model(true);}
        stamp={int(round_number()),actor,0};history={};channel_history={stamp.epoch,actor};
        if(!self().is_alive()) {
            if(nominal_known){++body_generation;unattributed_loss+=unsigned(bag);bag=0;nominal_known=false;}
            if(phase!=Phase::registration&&phase!=Phase::layout){phase=Phase::discovery;job=-1;route.reset();}
            return;
        }
        if(phase==Phase::registration) {
            if(candidate::advance_registration(world,observer,packet,stamp)==candidate::Status::ready)phase=Phase::layout;
            return;
        }
        if(phase==Phase::layout){setup_layout();return;}
        if constexpr(cfg::trace>=100)if(stamp.epoch==cfg::trace&&current(20000)) {
            const auto c=cursor.peek();
            std::printf("MSNAP r%d a%d phase%d purpose%d job%d pos%d,%d bag%d await%d cursor%d route%d goal%d prop%d fuel%llu\n",
                stamp.epoch,actor,int(phase),int(purpose),job,nominal.pos.x,nominal.pos.y,bag,int(awaiting_graph_change),int(c.status),
                int(bool(route)),route?int(route->snapshot().at_goal):-1,job>=0?int(world.groups[std::size_t(job)].design.members.size()):-1,
                static_cast<unsigned long long>(fuel_remaining()));
        }
        if(expired_absence_egress())return;
        Operations operations{*this};program::run(phase,operations);
    }
};
} // namespace
void service_draft() {
    const std::array order{champion_draft::varg,champion_draft::marcella,champion_draft::ryth,
        champion_draft::vendetta,champion_draft::vesper_vale};
    const auto chosen=draft::current_draft(team::ally);
    for(auto pick:order)if(std::find(chosen.begin(),chosen.end(),pick)==chosen.end()){draft::pick(pick);return;}
}
void service_turn(){static Actor value;value.turn();}
} // namespace mosaic
