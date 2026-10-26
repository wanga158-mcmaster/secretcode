#pragma once
// PAUSED / INCOMPLETE / UNVERIFIED. service() and turn() remain undefined.
// The unapplied integration buffer is saved under
// .cache/contenders-mosaic/r21/saved-draft/runtime-service-turn.SAVED-WIP.cpp.
#include "budget.h"
#include "observations.h"
#include "escape.h"
#include "execution.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace mosaic::adaptive {
inline void draft(){
    constexpr std::array order{champion_draft::varg,champion_draft::ryth,champion_draft::marcella,
        champion_draft::vendetta,champion_draft::vesper_vale,champion_draft::tavi};
    const auto chosen=cambc::draft::current_draft(team::ally);
    for(const auto pick:order)if(std::find(chosen.begin(),chosen.end(),pick)==chosen.end()){
        cambc::draft::pick(pick);return;
    }
}

// One actual group service and bag, rather than independent command owners.
struct Runtime:Observations {
    enum class Phase:std::uint8_t{work,capture,egress,recover,bank};
    struct GroupMemory {float hp{};int to_bank{10000},unavailable_until{};};
    struct CampMemory {game::CampFact fact{};int absent_until{};};
    struct Frame {
        game::SelfFact self{};
        std::array<MicroCamp,micro_capacity> camps{};unsigned camp_count{};
        std::array<MicroChampion,micro_capacity> champions{};unsigned champion_count{};
        std::array<MicroMove,micro_capacity> moves{};unsigned move_count{};
        std::array<MicroGround,micro_capacity> ground{};unsigned ground_count{};
        std::array<game::AbilityFact,2> abilities{};
    };
    std::optional<bitboard> walls;
    std::vector<Point> homes,altars;
    std::vector<std::uint8_t> board,wall_bits;
    std::vector<std::uint64_t> support,selected_support;
    std::vector<GroupMemory> group_memory;
    std::vector<CampMemory> camp_memory;
    std::vector<camp> setup_camps;
    std::size_t setup_group{},setup_member{},board_cursor{};
    bool loaded{},group_loaded{},ready{};
    Route<> goal_route,exit_route;
    std::array<Escape,2> escapes{};unsigned current_escape{};
    std::uint64_t source_revision{1},event_serial{},command_serial{};
    MicroLedger micro_ledger;
    std::optional<ExecutionResult> pending;
    std::uint64_t pending_serial{};
    int job{-1},member{-1},altar{-1},source_group{-1},source_absent_until{},capture_deadline{-1};
    Phase phase{Phase::work};bool recovering{},walk_locked{};
    float camp_taken{1};
    unsigned successes{},attempts{};
    static std::uint64_t clock(void*){return fuel_remaining();}
    bool control(TurnBudget& budget,std::uint64_t margin=4000){
        if(int(round_number())!=epoch){budget.poisoned=true;return false;}
        return budget.checkpoint()&&budget.pure_used()+margin<=budget.pure_bound;
    }
    static Point point(vec2 value){return {value.x,value.y};}
    std::size_t index(Point p)const{return std::size_t(p.y)*walls->width()+std::size_t(p.x);}
    bool floor(Point p)const{
        return walls&&p.x>=0&&p.y>=0&&p.x<int(walls->width())&&p.y<int(walls->height())&&board[index(p)]!=0;
    }
    bool home(Point p)const{return std::find(homes.begin(),homes.end(),p)!=homes.end();}
    EscapePotential source(Point p)const{
        if(!ready||!floor(p))return {};
        auto amount=support[index(p)];
        if(source_group==job&&source_absent_until>epoch&&selected_support.size()==support.size())
            amount-=std::min(amount,selected_support[index(p)]);
        return {amount,true};
    }
    EscapeKey escape_key()const{return {exit_route.revision(),exit_route.semantic(),source_revision};}
    bool prepare(TurnBudget&);
    void select_group(const game::SelfFact&);
    bool bind_goal(Point,int,std::uint64_t,bool center=true);
    Escape& plan(TurnBudget&,Point,bool candidate=false);
    bool absence_covers(unsigned)const;
    void observe_camp(const game::CampFact&);
    void observe_group(const game::GroupFact&);
    bool read_self(TurnBudget&,game::SelfFact&,bool gold=false);
    void collect(TurnBudget&,Frame&);
    bool perform(TurnBudget&,Frame&,const MicroAction&);
    bool channel(TurnBudget&,game::SelfFact&,bool recall,Point altar_point={});
    void service(TurnBudget&,game::SelfFact&);
    void turn(std::uint64_t whole_entry);
    // Retained scalar SDK purchase transaction only, not the retired service
    // controller. It spends actual gold on home/dead permission and yields.
    #include "../../../miscellaneous/mosaic-ordered-words-r20-20261025/src/native_service_shop.h"
};

inline bool Runtime::prepare(TurnBudget& budget){
    if(ready)return true;
    if(!loaded){
        if(fuel_remaining()<85000)return false;
        // Immutable SDK metadata is acquired once. Observation epochs never
        // restart these retained catalogue/footprint cursors.
        groups=map::camp_groups();memory.resize(groups.size());group_memory.resize(groups.size());
        walls.emplace(map::walls()|map::holes()|map::altars()|map::spawn_region(team::enemy));
        for(const auto at:map::spawn_region(team::ally).positions())homes.push_back(point(at));
        for(const auto at:map::altars().positions())altars.push_back(point(at));
        wall_bits=walls->export_linear_buffer();
        const auto cells=std::size_t(walls->width())*walls->height();
        board.resize(cells);support.resize(cells);selected_support.resize(cells);
        const auto design=self().base_stats();camp_multiplier=design.camp_damage_multiplier;passive=self().passive();
        members.reserve(256);camp_memory.reserve(256);loaded=true;
        if(!control(budget))return false;
    }
    while(setup_group<groups.size()&&control(budget,10000)){
        if(!group_loaded){setup_camps=groups[setup_group].camps();setup_member=0;
            memory[setup_group].begin=members.size();group_loaded=true;}
        if(setup_member==setup_camps.size()){
            memory[setup_group].end=members.size();++setup_group;group_loaded=false;continue;
        }
        const auto camp=setup_camps[setup_member++];const auto stats=camp.base_stats();
        const auto at=camp.spawn_position();
        members.push_back({camp,int(setup_group),at,stats.base_health,stats.base_strength,stats.base_attack_range});
        camp_memory.push_back({});group_memory[setup_group].hp+=stats.base_health;
        for(const auto altar:altars)group_memory[setup_group].to_bank=std::min(group_memory[setup_group].to_bank,
            std::max(0,game::distance(point(at),altar)-int(constants::submit_gems_range)));
        const auto hit=std::uint64_t(std::ceil(std::max(0.0f,stats.base_strength)));
        for(int y=std::max(0,at.y-stats.base_attack_range);y<=std::min(int(walls->height())-1,at.y+stats.base_attack_range);++y)
            for(int x=std::max(0,at.x-stats.base_attack_range);x<=std::min(int(walls->width())-1,at.x+stats.base_attack_range);++x)
                support[std::size_t(y)*walls->width()+std::size_t(x)]+=hit;
        if(!control(budget))return false;
    }
    if(setup_group!=groups.size())return false;
    unsigned copied=0;
    while(board_cursor<board.size()&&copied<256&&control(budget)){
        board[board_cursor]=(wall_bits[board_cursor/8]&(1u<<(board_cursor%8)))==0;
        ++board_cursor;++copied;
    }
    if(board_cursor!=board.size())return false;
    if(homes.size()>256||!set_terrain(walls->width(),walls->height(),board))return false;
    if(!exit_route.begin(walls->width(),walls->height(),board,homes,1))return false;
    ready=true;return true;
}

inline void Runtime::select_group(const game::SelfFact& me){
    double best=std::numeric_limits<double>::infinity();int chosen=-1;
    for(std::size_t g=0;g<groups.size();++g){
        if(group_memory[g].unavailable_until>epoch||memory[g].begin==memory[g].end)continue;
        int travel=10000;
        for(auto m=memory[g].begin;m<memory[g].end;++m)travel=std::min(travel,
            std::max(0,game::distance(me.pose,point(members[m].pos))-me.attack_range));
        const auto duration=travel+group_memory[g].hp/std::max(1.0f,me.strength*camp_multiplier)+
            constants::capture_time+group_memory[g].to_bank+constants::submit_gem_base_duration;
        const auto score=double(duration)/std::max(1u,groups[g].gem_value());
        if(score<best){best=score;chosen=int(g);}
    }
    if(chosen==job)return;
    job=chosen;member=-1;source_group=-1;source_absent_until=0;capture_deadline=-1;
    ++source_revision;goal_route.retire();
    std::fill(selected_support.begin(),selected_support.end(),0);
    if(job<0)return;
    for(auto m=memory[std::size_t(job)].begin;m<memory[std::size_t(job)].end;++m){
        const auto& body=members[m];const auto hit=std::uint64_t(std::ceil(std::max(0.0f,body.base_strength)));
        for(int y=std::max(0,body.pos.y-body.range);y<=std::min(int(walls->height())-1,body.pos.y+body.range);++y)
            for(int x=std::max(0,body.pos.x-body.range);x<=std::min(int(walls->width())-1,body.pos.x+body.range);++x)
                selected_support[std::size_t(y)*walls->width()+std::size_t(x)]+=hit;
    }
}

inline bool Runtime::bind_goal(Point at,int range,std::uint64_t semantic,bool center){
    if(goal_route.semantic()==semantic)return true;
    std::array<Point,256> roots{};unsigned count=0;
    for(int y=std::max(0,at.y-range);y<=std::min(int(walls->height())-1,at.y+range);++y)
        for(int x=std::max(0,at.x-range);x<=std::min(int(walls->width())-1,at.x+range);++x){
            const Point p{x,y};if(!floor(p)||(!center&&p==at))continue;
            if(count==roots.size())return false;roots[count++]=p;
        }
    return count&&goal_route.begin(walls->width(),walls->height(),board,{roots.data(),count},semantic);
}

inline Escape& Runtime::plan(TurnBudget& budget,Point at,bool candidate){
    const auto key=escape_key();unsigned slot=current_escape;
    if(escapes[slot].origin()!=at||escapes[slot].key()!=key){
        const auto other=slot^1;
        if(escapes[other].origin()==at&&escapes[other].key()==key)slot=other;
        else if(candidate)slot=other;
    }
    auto& path=escapes[slot];path.begin(native_facts().epoch,at,key);
    path.advance(exit_route,[&](Point p){return source(p);},[&]{return control(budget);},128);
    if(!candidate)current_escape=slot;
    return path;
}
inline bool Runtime::absence_covers(unsigned phases)const{
    return source_group!=job||source_absent_until<=epoch||std::uint64_t(phases)<std::uint64_t(source_absent_until-epoch);
}
inline void Runtime::observe_camp(const game::CampFact& fact){
    if(!fact.actual(native_facts().epoch)||fact.member_ordinal<0||std::size_t(fact.member_ordinal)>=camp_memory.size())return;
    auto& record=camp_memory[std::size_t(fact.member_ordinal)];record.fact=fact;
    if(fact.life==game::Life::dead)record.absent_until=epoch+std::max(1,groups[std::size_t(fact.group_ordinal)].max_respawn_timer());
    else record.absent_until=0;
}
inline void Runtime::observe_group(const game::GroupFact& group){
    if(!group.current(native_facts().epoch)||group.ordinal!=job)return;
    native_facts().group=group;
    if(group.life==game::GroupLife::reward||group.life==game::GroupLife::respawning){
        const auto until=epoch+std::max(1,group.respawn_actual?group.respawn_time:groups[std::size_t(job)].max_respawn_timer());
        // An already valid earlier absence bound stays valid. Extending it on
        // every fresh Reward read would needlessly discard a usable path.
        if(source_group!=job||source_absent_until<=epoch){source_group=job;source_absent_until=until;++source_revision;}
        if(group.life==game::GroupLife::respawning)group_memory[std::size_t(job)].unavailable_until=until;
    }else if(source_group==job){source_group=-1;source_absent_until=0;++source_revision;}
}
inline bool Runtime::read_self(TurnBudget& budget,game::SelfFact& out,bool gold){
    if(!control(budget,0))return false;
    game::SelfFact fresh{};if(!native_read_self(budget,fresh,true,gold)||!control(budget,0))return false;
    native_facts().self=fresh;out=fresh;return true;
}

inline void Runtime::collect(TurnBudget& budget,Frame& frame){
    const auto now=native_facts().epoch;
    if(job<0)select_group(frame.self);
    if(job<0)return;
    const auto& group=memory[std::size_t(job)];
    // Lifecycle is mandatory at the dead-member boundary, not a prerequisite
    // for distant movement or a live visible Basic.
    if(member>=0&&std::size_t(member)<camp_memory.size()&&camp_memory[std::size_t(member)].absent_until>epoch){
        game::GroupFact life{};
        if(native_read_group(budget,job,life,member)&&control(budget,0)){
            observe_group(life);
            if(life.capturable(now))phase=Phase::capture;
        }
    }
    if(member<0||std::size_t(member)<group.begin||std::size_t(member)>=group.end||camp_memory[std::size_t(member)].absent_until>epoch){
        int best=10000;member=-1;
        for(auto m=group.begin;m<group.end;++m)if(camp_memory[m].absent_until<=epoch){
            const auto distance=game::distance(frame.self.pose,point(members[m].pos));
            if(distance<best){best=distance;member=int(m);}
        }
    }
    if(member<0){
        // The group may be fully dead or the earlier dated bounds may expire.
        // Approach/observe an actual footprint; do not invent Reward custody.
        int best=10000;
        for(auto m=group.begin;m<group.end;++m){const auto distance=game::distance(frame.self.pose,point(members[m].pos));
            if(distance<best){best=distance;member=int(m);}}
    }
    const auto outer=budget.held_tail;budget.reserve(TurnBudget::add(outer,24000));
    for(unsigned slot=0;slot<2;++slot){
        frame.abilities[slot]=native_facts().abilities[slot];
        if(!frame.self.can_cast(now))continue;
        game::AbilityFact ability{};
        if(native_read_ability(budget,slot,ability))frame.abilities[slot]=ability;
        if(budget.poisoned){budget.reserve(outer);return;}
    }
    int reach=frame.self.attack_range;
    for(const auto& ability:frame.abilities)if(ability.ready(frame.self,now)&&
        (ability.descriptor.kind==game::AbilityKind::damage||ability.descriptor.kind==game::AbilityKind::aoe_damage||ability.descriptor.kind==game::AbilityKind::cc))
        reach=std::max(reach,int(std::floor(ability.descriptor.range+ability.descriptor.radius)));
    std::array<int,3> probes{{member,-1,-1}};
    for(unsigned p=1;p<probes.size();++p){int best=10000;
        for(auto m=group.begin;m<group.end;++m){
            if(camp_memory[m].absent_until>epoch||std::find(probes.begin(),probes.begin()+p,int(m))!=probes.begin()+p)continue;
            const auto distance=game::distance(frame.self.pose,point(members[m].pos));
            if(distance<=reach&&distance<best){best=distance;probes[p]=int(m);}
        }
    }
    for(const auto ordinal:probes){
        if(ordinal<0||frame.camp_count==frame.camps.size()||!control(budget,8000))continue;
        auto camp=camp_memory[std::size_t(ordinal)].fact;
        if(!camp.actual(now)){
            game::CampFact fresh{};
            if(!native_read_camp(budget,ordinal,fresh)||!control(budget,0))continue;
            camp=fresh;observe_camp(camp);
        }
        auto& target=frame.camps[frame.camp_count++];target.fact=camp;
        target.visible=target.visibility_actual=camp.actual(now)&&camp.body_actual;
        for(unsigned slot=0;slot<2;++slot){const auto& ability=frame.abilities[slot];
            if(ability.descriptor.actual&&std::isfinite(ability.descriptor.modifier)&&ability.descriptor.modifier>0)
                target.damage[slot]={ability.descriptor.modifier*frame.self.focus*camp_multiplier,true};
        }
        if(camp.actual(now)&&camp.life==game::Life::dead){
            goal_route.observe_clear(frame.self.pose,camp.pose,now);exit_route.observe_clear(frame.self.pose,camp.pose,now);
            game::GroupFact life{};
            if(native_read_group(budget,job,life,ordinal)&&control(budget,0)){
                observe_group(life);if(life.capturable(now))phase=Phase::capture;
            }
        }
        if(budget.poisoned)break;
    }
    // These contact masks are charged conservatively as whole control work.
    // Only one personally visible contact body per side is imported, never a
    // complete private roster/resource refresh.
    if(control(budget,18000))for(const auto side:std::array{team::enemy,team::ally}){
        bool need=side==team::enemy;
        for(const auto& ability:frame.abilities)need|=ability.ready(frame.self,now)&&ability.descriptor.kind==game::AbilityKind::heal;
        if(!need)continue;
        const auto positions=vision::champion_tiles(side).positions();int best=10000;std::optional<vec2> chosen;
        for(const auto p:positions){if(point(p)==frame.self.pose)continue;const auto distance=game::distance(frame.self.pose,point(p));
            if(distance<=reach&&distance<best){best=distance;chosen=p;}}
        if(chosen&&control(budget,14000)){
            const auto who=vision::champion_at(*chosen);MicroChampion contact{};
            if(native_read_champion(budget,std::uint32_t(handle_champion_t(who)),contact)&&control(budget,0)){
                for(unsigned slot=0;slot<2;++slot)if(frame.abilities[slot].descriptor.actual){
                    const auto amount=frame.abilities[slot].descriptor.modifier*frame.self.focus;
                    contact.damage[slot]={amount,std::isfinite(amount)&&amount>0};contact.healing[slot]=contact.damage[slot];}
                frame.champions[frame.champion_count++]=contact;
            }
        }
        if(budget.poisoned)break;
    }
    budget.reserve(outer);
}

inline bool Runtime::perform(TurnBudget& budget,Frame& frame,const MicroAction& action){
    ExecutionContext context{};context.actual_self=frame.self;context.walk_locked=walk_locked;
    for(unsigned i=0;(action.kind==MicroKind::basic_camp||action.kind==MicroKind::cast_camp)&&i<frame.camp_count;++i)if(frame.camps[i].fact.id==action.target_id){
        context.camp=frame.camps[i].fact;context.member_ordinal=context.camp->member_ordinal;break;}
    for(unsigned i=0;(action.kind==MicroKind::basic_champion||action.kind==MicroKind::cast_champion)&&i<frame.champion_count;++i)if(frame.champions[i].id==action.target_id){context.champion=frame.champions[i];break;}
    for(unsigned i=0;action.kind==MicroKind::cast_ground&&i<frame.ground_count;++i)if(frame.ground[i].point==action.point){context.ground=frame.ground[i];break;}
    std::uint64_t serial{};
    const auto result=execute_action(*this,budget,action,context,[&](const ExecutionResult& recorded){
        if(recorded.attempted){
            serial=++command_serial;++attempts;pending=recorded;pending_serial=serial;
            MicroReceipt event{};event.event_id=++event_serial;event.serial=serial;event.action=action;
            event.outcome=recorded.outcome;event.before=recorded.before;event.after=recorded.after;
            micro_ledger.observe(event);
        }else if(recorded.prevalidation){
            MicroReceipt event{};event.event_id=++event_serial;event.action=action;
            event.before=recorded.before;event.after=recorded.after;micro_ledger.observe(event);
            if(action.kind==MicroKind::move){
                goal_route.reject(recorded.before.pose,action.point,recorded.before.epoch,recorded.blocker);
                exit_route.reject(recorded.before.pose,action.point,recorded.before.epoch,recorded.blocker);
            }
        }
    });
    walk_locked=result.walk_locked;
    if(!result.attempted)return false;
    if(result.outcome==MicroOutcome::applied)++successes;
    if(result.camp_after)observe_camp(*result.camp_after);
    std::array<MicroRankChange,1> ranks{};unsigned rank_count=0;
    if(context.camp&&result.camp_after&&context.camp->actual(result.before.epoch)&&result.camp_after->actual(result.after_epoch)&&
       context.camp->body_actual){
        const auto& before=*context.camp;const auto& after=*result.camp_after;
        ranks[0]={{result.before.epoch,MicroTargetKind::camp,before.id,before.group_id,before.hp,before.shield,true,before.life==game::Life::dead},
            {result.after_epoch,MicroTargetKind::camp,after.id,after.group_id,after.hp,after.shield,true,after.life==game::Life::dead}};
        rank_count=1;
        if(after.life==game::Life::dead){goal_route.observe_clear(result.after.pose,after.pose,result.after_epoch);
            exit_route.observe_clear(result.after.pose,after.pose,result.after_epoch);}
    }
    MicroReceipt event{};event.event_id=++event_serial;event.serial=serial;event.action=action;event.outcome=result.outcome;
    event.before=result.before;event.after=result.after;event.ranks={ranks.data(),rank_count};event.acknowledged=result.after_actual&&!result.pending;
    micro_ledger.observe(event);
    if(action.kind==MicroKind::move){
        const auto outcome=result.outcome==MicroOutcome::applied?MoveOutcome::applied:
            result.outcome==MicroOutcome::refused?MoveOutcome::refused:MoveOutcome::unknown;
        const MoveRecord receipt{serial,result.before.epoch,result.before.pose,action.point,outcome};
        goal_route.record(receipt);exit_route.record(receipt);
        if(result.after_actual){
            for(auto& escape:escapes)if(escape.origin()==result.before.pose)
                escape.observe_move(receipt,result.after_epoch,result.after.pose);
            for(unsigned i=0;i<escapes.size();++i)if(escapes[i].origin()==result.after.pose&&escapes[i].key()==escape_key()){
                escapes[i].begin(result.after_epoch,result.after.pose,escape_key());current_escape=i;break;}
        }
        if(result.outcome==MicroOutcome::refused){
            goal_route.reject(result.before.pose,action.point,result.after_epoch,BlockKind::unknown);
            exit_route.reject(result.before.pose,action.point,result.after_epoch,BlockKind::unknown);
        }
    }
    if(result.after_actual){frame.self=result.after;native_facts().self=result.after;
        goal_route.begin_input(result.after_epoch,result.after.pose);exit_route.begin_input(result.after_epoch,result.after.pose);}
    if(!result.pending)pending.reset();
    return true;
}

inline bool Runtime::channel(TurnBudget& budget,game::SelfFact& me,bool recall,Point at){
    if(!me.actual(native_facts().epoch)||!me.alive||me.stunned||me.completed||me.recalling||me.submitting||me.teleporting)return false;
    if(recall&&me.recall_started)return false;
    if(!recall){
        if(!me.gems_actual||me.gems<=0||me.submit_started||game::distance(me.pose,at)>int(constants::submit_gems_range))return false;
        if(!budget.before_query())return false;
        detail::altar_lock_info lock{};const auto code=detail::sdk_altar_lock(at.x,at.y,1-me.team,&lock);
        const bool paid=budget.after_query();
        if(!paid)return false;
        if(code==int(error_code::api_not_in_vision))return false;
        if(code!=int(error_code::api_not_found)){detail::check(code);return false;}
    }
    const auto outer=budget.held_tail;
    if(!budget.admit(TurnBudget::add(outer,TurnBudget::add(budget.command_bound,4*budget.query_bound))))return false;
    budget.reserve(TurnBudget::add(outer,4*budget.query_bound));
    if(!control(budget,0)||!budget.before_command()){budget.reserve(outer);return false;}
    const auto code=recall?detail::sdk_recall():detail::sdk_submit_gems(at.x,at.y);
    ++command_serial;++attempts;if(!code)++successes;
    if(!code&&recall)recovering=true;
    native_after_attempt(game::EffectScope::self_only);
    const bool paid=budget.after_command();budget.reserve(outer);detail::check(code);
    if(!paid)return true;
    game::SelfFact after{};if(read_self(budget,after))me=after;
    return true;
}
}
