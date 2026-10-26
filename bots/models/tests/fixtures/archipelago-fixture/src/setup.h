#pragma once
#include "../../../../common/movement.h"
#include <array>
#include <span>

namespace plan4_archipelago_fixture {
using plan4::Point;
enum class Condition { positive, connected_negative, unknown };
enum class Role { subject, blocker, idle };
enum class Kind { wait, warmup, move, ward, recall, home, model };
struct Configuration { Point actor{}, blocker{}, waiting{}; };
inline Configuration configuration(bool north, Condition condition) {
    return north ? Configuration{{15,20},condition==Condition::positive?Point{16,20}:Point{19,19},{20,19}}
                 : Configuration{{29,6},condition==Condition::positive?Point{29,7}:Point{25,8},{24,10}};
}
inline int distance(Point a,Point b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
inline bool roles(std::span<const int> roster,int own,Role& role,int& partner) {
    if (roster.size()<2 || roster.size()>5 || own<0) return false;
    std::array<int,5> ids{}; std::copy(roster.begin(),roster.end(),ids.begin());
    std::sort(ids.begin(),ids.begin()+roster.size());
    if (ids[0]<0 || std::adjacent_find(ids.begin(),ids.begin()+roster.size())!=ids.begin()+roster.size() ||
        std::find(ids.begin(),ids.begin()+roster.size(),own)==ids.begin()+roster.size()) return false;
    role=own==ids[0]?Role::subject:own==ids[1]?Role::blocker:Role::idle;
    partner=role==Role::subject?ids[1]:ids[0]; return true;
}
struct Frame {
    int round=-1,actor=-1,partner=-1,partner_round=-1;
    Point position{},partner_position{};
    unsigned bag=0;
    bool fresh=false,alive=false,channel=false,partner_personally_visible=false,adjacent_enemy=false;
    bool on_spawn=false,full_health=false,recall_safe=false;
    bool own_ward_present=false,partner_tile_visible=false,partner_tile_occupied=false;
    bool enemy_within_eight=false,awake_camp_in_range=false;
};
struct Command { Kind kind=Kind::wait; Point goal{}; int round=-1; };
// No clocks predict capture or visibility. Bag comes only from fresh own state;
// the disabled common farmer owns the entire acquisition/capture warmup.
class Staging {
    Role role_=Role::idle; int own_=-1,partner_=-1,last_round_=-1,restored_round_=-1;
    Configuration config_{}; Command last_{};
    bool acquired_=false,restored_=false,warded_=false,released_=false,closing_=false,blocking_=false,release_pending_=false;
public:
    Staging()=default;
    Staging(Role role,int own,int partner,Configuration config):role_(role),own_(own),partner_(partner),config_(config){}
    bool released()const{return released_;}
    bool acquired()const{return acquired_;}
    int pending_release_round()const{return release_pending_?last_.round:-1;}
    Command next(const Frame& f) {
        if(released_)return {Kind::model,{},f.round};
        release_pending_=false;
        last_={Kind::wait,{},f.round};
        if(!f.fresh || f.actor!=own_ || f.round<0 || f.round<last_round_)return last_;
        last_round_=f.round;
        if(role_==Role::idle)return last_;
        if(role_==Role::subject && !acquired_ && f.bag==0) {last_.kind=Kind::warmup;return last_;}
        if(!f.alive || f.channel)return last_;
        // An exact self ward observation also establishes this checkpoint.
        // A team ward mask alone is never an ownership/vision receipt.
        if(f.own_ward_present)warded_=true;
        if(role_==Role::subject) {
            if(f.bag==0)return last_; // Death/bag loss cannot release a fabricated bank episode.
            acquired_=true;
            if(!f.full_health)restored_=false;
            if(!restored_) {
                if(f.on_spawn&&f.full_health){restored_=true;restored_round_=f.round;return last_;}
                if(f.on_spawn)return last_;
                last_.kind=f.recall_safe?Kind::recall:Kind::home;return last_;
            }
            if(f.round<=restored_round_)return last_;
            if(!warded_)return last_={f.position==config_.blocker?Kind::ward:Kind::move,config_.blocker,f.round};
            if(f.position!=config_.actor)return last_={Kind::move,config_.actor,f.round};
            if(f.partner_personally_visible && f.partner_round==f.round && f.partner==partner_ &&
                f.partner_position==config_.blocker && f.own_ward_present && f.partner_tile_visible &&
                f.partner_tile_occupied && !f.adjacent_enemy){last_.kind=Kind::model;release_pending_=true;}
            return last_;
        }
        if(!warded_)return last_={f.position==config_.actor?Kind::ward:Kind::move,config_.actor,f.round};
        if(blocking_)return last_;
        if(closing_ && f.position==config_.blocker){blocking_=true;return last_;}
        const bool witnessed=f.partner_personally_visible && f.partner_round==f.round && f.partner==partner_ && f.partner_position==config_.actor &&
            f.own_ward_present && f.partner_tile_visible && f.partner_tile_occupied;
        if(witnessed) {
            closing_=true;
            if(f.position==config_.blocker) {blocking_=true;return last_;}
            return last_={Kind::move,config_.blocker,f.round};
        }
        if(f.position!=config_.waiting)return last_={Kind::move,config_.waiting,f.round};
        return last_;
    }
    bool commit_release(const Frame& f,bool ready) {
        const bool pending=release_pending_;release_pending_=false;
        if(!pending||!ready||released_||role_!=Role::subject||last_.kind!=Kind::model||
            !f.fresh||f.round!=last_.round||f.round!=last_round_||f.actor!=own_||!f.alive||f.channel||
            !f.full_health||!f.bag||f.position!=config_.actor||f.partner!=partner_||
            !f.partner_personally_visible||f.partner_round!=f.round||f.partner_position!=config_.blocker||
            !f.own_ward_present||!f.partner_tile_visible||!f.partner_tile_occupied||f.adjacent_enemy||
            f.enemy_within_eight||f.awake_camp_in_range)return false;
        released_=true;return true;
    }
    bool acknowledge(const Command& c,bool success) {
        // Model candidates require the fresh synchronous commit path. Generic
        // action acknowledgements cannot release or retain a failed ticket.
        if(c.kind==Kind::model){release_pending_=false;return false;}
        if(!success || c.kind!=last_.kind || c.round!=last_round_ || c.goal!=last_.goal)return false;
        if(c.kind==Kind::ward)warded_=true;
        return true;
    }
};
struct ParkingChoice { Point point{}; int distance=0; bool valid=false; };
inline bool parking_less(const ParkingChoice& a,const ParkingChoice& b) {
    return !b.valid || a.distance<b.distance || (a.distance==b.distance && plan4::point_less(a.point,b.point));
}
inline void consider_parking(std::array<ParkingChoice,5>& choices,Point p,Point origin,Point subject,bool walkable,bool camp,bool safe) {
    if(!walkable || camp || !safe || distance(p,subject)<=8)return;
    const ParkingChoice candidate{p,distance(p,origin),true};
    int slot=0;while(slot<5 && !parking_less(candidate,choices[slot]))++slot;
    if(slot==5)return;
    for(int n=4;n>slot;--n)choices[n]=choices[n-1];choices[slot]=candidate;
}
} // namespace plan4_archipelago_fixture

#ifndef PLAN4_ARCHIPELAGO_FIXTURE_NO_SDK
#include <cambc.h>
#include "../../../../common/telemetry.h"
#include <cstdio>
#include <memory>
#include <vector>
#ifndef PLAN4_REFERENCE_VERSION
#define PLAN4_REFERENCE_VERSION "archipelago-setup-development-v1"
#endif
#ifndef PLAN4_ARCHIPELAGO_CONDITION
#define PLAN4_ARCHIPELAGO_CONDITION 0
#endif
namespace plan4_archipelago_fixture {
inline Point point(cambc::vec2 p){return {p.x,p.y};}
inline cambc::vec2 tile(Point p){return {p.x,p.y};}
inline int id(const cambc::champion& c){return int(static_cast<cambc::handle_champion_t>(c));}
inline bool north_spawn(cambc::team side,bool& north) {
    if(cambc::map::width()!=45 || cambc::map::height()!=30)return false;
    const auto& spawn=cambc::map::spawn_region(side);
    const bool n=spawn.is_set({26,6}),s=spawn.is_set({16,21});
    if(n==s)return false;north=n;return true;
}
inline Condition public_condition() {
    const auto enemy=cambc::champions(cambc::team::enemy);
    if(enemy.size()!=4)return Condition::unknown;
    bool varg=false,vesper=false,vendetta=false,tavi=false,marcella=false;
    for(const auto& c:enemy) {
        switch(c.champion_name()) {
        case cambc::champion_type::varg:if(varg)return Condition::unknown;varg=true;break;
        case cambc::champion_type::vesper_vale:if(vesper)return Condition::unknown;vesper=true;break;
        case cambc::champion_type::vendetta:if(vendetta)return Condition::unknown;vendetta=true;break;
        case cambc::champion_type::tavi:if(tavi)return Condition::unknown;tavi=true;break;
        case cambc::champion_type::marcella:if(marcella)return Condition::unknown;marcella=true;break;
        default:return Condition::unknown;
        }
    }
    if(!vendetta||!tavi||!marcella||varg==vesper)return Condition::unknown;
    return varg?Condition::positive:Condition::connected_negative;
}
class World {
    struct Threat {Point p;int range;};
    cambc::bitboard walls_;
    std::vector<cambc::map::camp_group> groups_;
    std::vector<unsigned char> topology_,camp_;
    std::vector<Threat> threats_;
    std::array<cambc::camp_stats,32> stats_{};std::array<bool,32> loaded_stats_{};
    plan4::PersistentMovement movement_;
    std::size_t group_=0,member_=0,copy_=0;
    int width_=0,height_=0,round_=-1;unsigned used_=0;
    bool loaded_=false,ready_=false,failed_=false;Point initial_{};
    static bool allowed(const void* raw){return static_cast<const World*>(raw)->budget();}
    static bool blocked(Point p,const void* raw){return !static_cast<const cambc::bitboard*>(raw)->is_set(tile(p));}
public:
    void begin(int round){if(round!=round_){round_=round;used_=0;}}
    bool budget(unsigned floor=80000)const{return int(cambc::round_number())==round_ && cambc::fuel_remaining()>=floor;}
    unsigned vertices()const{return used_;}
    bool failed()const{return failed_;}
    bool prepared()const{return ready_;}
    bool open(Point p)const{return ready_ && p.x>=0&&p.y>=0&&p.x<width_&&p.y<height_&&!topology_[p.y*width_+p.x];}
    bool camp(Point p)const{return p.x<0||p.y<0||p.x>=width_||p.y>=height_||camp_[p.y*width_+p.x];}
    bool safe_parking(Point p)const{for(const auto& t:threats_)if(distance(p,t.p)<=t.range+1)return false;return true;}
    int size()const{return width_*height_;}
    Point at(int key)const{return {key%width_,key/width_};}
    bool charge(){if(used_>=128 || !budget())return false;++used_;return true;}
    bool prepare(Point reserve={-1,-1}) {
        if(failed_)return false;if(ready_)return true;
        if(!loaded_) {
            if(!budget(90000))return false;
            width_=cambc::map::width();height_=cambc::map::height();
            if(width_!=45||height_!=30){failed_=true;return false;}
            initial_=point(cambc::self().position());
            walls_=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(cambc::team::enemy);
            groups_=cambc::map::camp_groups();
            if(groups_.size()>64){failed_=true;return false;}
            topology_.resize(std::size_t(width_)*height_);camp_.resize(topology_.size());threats_.reserve(256);loaded_=true;
        }
        while(group_<groups_.size()) {
            const auto& members=groups_[group_].camps();
            if(members.empty()||members.size()>32){failed_=true;return false;}
            while(member_<members.size()) {
                if(!charge())return false;
                const auto& c=members[member_];const auto p=point(c.spawn_position());const int kind=int(c.kind());
                if(kind<0||kind>=32||p.x<0||p.y<0||p.x>=width_||p.y>=height_){failed_=true;return false;}
                if(!loaded_stats_[kind]){stats_[kind]=c.base_stats();loaded_stats_[kind]=true;}
                camp_[p.y*width_+p.x]=1;
                if(stats_[kind].base_strength>0){if(threats_.size()==256){failed_=true;return false;}
                    threats_.push_back({p,stats_[kind].base_attack_range});}
                ++member_;
            }
            ++group_;member_=0;
        }
        while(copy_<topology_.size()) {
            if(!charge())return false;const auto p=at(int(copy_));
            // Reservation is an explicit setup route constraint only. Neither
            // this nor camp avoidance is exported to the common model graph.
            topology_[copy_]=static_cast<unsigned char>(walls_.is_set(tile(p)) ||
                (p!=initial_ && (camp_[copy_] || p==reserve)));++copy_;
        }
        if(!budget(85000))return false;
        movement_.initialize(width_,height_,topology_);ready_=true;return true;
    }
    void move_goals(std::span<const Point> goals) {
        auto& me=cambc::self();if(!budget()||!me.can_move()||goals.empty())return;
        const auto origin=point(me.position());
        auto obstacles=walls_|cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
        obstacles.set(tile(origin),false);
        const auto legal=obstacles.reachable_tiles(tile(origin),1);
        if(!budget())return;
        movement_.begin_turn(0,128-used_);
        const auto step=movement_.next_step(origin,goals,blocked,&legal,128-used_,{allowed,this});used_+=step.visits;
        if(step.status==plan4::MovementStatus::unreachable||step.status==plan4::MovementStatus::invalid_origin){failed_=true;return;}
        if(step.status!=plan4::MovementStatus::ready||!budget(70000)||!me.can_move()||!legal.is_set(tile(step.next)))return;
        const bool moved=me.move_absolute(tile(step.next));movement_.notify_move_result(origin,step.next,moved);
    }
    void move(Point goal){if(open(goal)){const std::array goals{goal};move_goals(goals);}}
    void home() {
        auto available=cambc::map::spawn_region(cambc::team::ally)&~cambc::vision::champion_tiles();
        const auto positions=available.positions();std::array<Point,16> goals{};std::size_t count=0;
        if(positions.size()>goals.size()){failed_=true;return;}
        for(auto p:positions)if(open(point(p)))goals[count++]=point(p);
        move_goals(std::span(goals.data(),count));
    }
};
class Setup {
public: enum class Action { handled,warmup,model,prepare };
private:
    World world_;Staging staging_;Configuration config_{};
    Condition condition_=Condition::unknown;bool explicit_condition_=false,configured_=false,unsupported_=false;
    Role role_=Role::idle;int partner_=-1,round_=-1,summary_=-1,last_kind_=-1;
    std::optional<cambc::champion> partner_view_;
    int partner_view_round_=-1;
    void unsupported(const char* reason){if(!unsupported_)std::printf("plan4 fixture unsupported reason=%s actor=%d round=%d\n",reason,id(cambc::self()),round_);unsupported_=true;}
    Action finish(Action action) {
        if((action==Action::handled||action==Action::warmup) && world_.budget(12000) && (summary_<0||round_%32==0) && summary_!=round_) {
            auto& me=cambc::self();summary_=round_;
            plan4::LogRow("summary",PLAN4_REFERENCE_VERSION).field("actor",id(me)).field("round",round_)
                .field("fuel",cambc::fuel_remaining()).field("bag",me.gem_count())
                .field("score",cambc::global_submitted_gems(cambc::team::ally)).field("active",false).emit();
        }
        return action;
    }
    bool release_frame(Frame& frame) {
        const auto live=[&]{return world_.budget(30000);};
        if(!live()||role_!=Role::subject||!partner_view_||partner_view_round_!=round_)return false;
        auto& me=cambc::self();frame.round=round_;frame.actor=id(me);
        frame.alive=me.is_alive();if(!live()||!frame.alive)return false;
        frame.position=point(me.position());if(!live())return false;
        frame.bag=me.gem_count();if(!live()||!frame.bag)return false;
        const auto state=me.state();if(!live())return false;
        frame.channel=state.contains(cambc::champion_state::recalling)||state.contains(cambc::champion_state::submitting_gems)||
            state.contains(cambc::champion_state::teleporting);
        if(frame.channel||frame.position!=config_.actor)return false;
        const float health=me.health();if(!live())return false;
        const float maximum=me.max_health();if(!live())return false;
        frame.full_health=std::isfinite(health)&&std::isfinite(maximum)&&maximum>0&&health>=maximum;
        if(!frame.full_health)return false;
        const auto own_wards=me.ward_positions();if(!live()||own_wards.size()>16)return false;
        for(auto p:own_wards)frame.own_ward_present|=point(p)==config_.blocker;
        if(!frame.own_ward_present||id(*partner_view_)!=partner_)return false;
        const bool visible_partner=partner_view_->is_visible();if(!live()||!visible_partner)return false;
        const bool living_partner=partner_view_->is_alive();if(!live()||!living_partner)return false;
        frame.partner=partner_;frame.partner_position=point(partner_view_->position());if(!live())return false;
        frame.partner_round=round_;frame.partner_personally_visible=true;
        if(frame.partner_position!=config_.blocker)return false;
        const auto visible=cambc::vision::visible_tiles();if(!live())return false;
        const auto occupied=cambc::vision::champion_tiles();if(!live())return false;
        frame.partner_tile_visible=visible.is_set(tile(frame.partner_position));if(!live())return false;
        frame.partner_tile_occupied=occupied.is_set(tile(frame.partner_position));if(!live())return false;
        if(!frame.partner_tile_visible||!frame.partner_tile_occupied)return false;
        const auto enemies=cambc::vision::champions(cambc::team::enemy);if(!live()||enemies.size()>5)return false;
        for(const auto& enemy:enemies) {
            const bool alive=enemy.is_alive();if(!live())return false;if(!alive)continue;
            const auto p=point(enemy.position());if(!live())return false;
            if(distance(frame.position,p)<=8){frame.enemy_within_eight=true;return false;}
        }
        const auto camps=cambc::vision::camps();if(!live()||camps.size()>64)return false;
        for(const auto& camp:camps) {
            const bool alive=camp.is_alive();if(!live())return false;if(!alive)continue;
            const bool awake=camp.is_awake();if(!live())return false;if(!awake)continue;
            const auto p=point(camp.position());if(!live())return false;
            const int range=camp.attack_range();if(!live())return false;
            if(distance(frame.position,p)<=range){frame.awake_camp_in_range=true;return false;}
        }
        frame.fresh=live();return frame.fresh;
    }
public:
    Setup()=default;
    explicit Setup(Condition condition):condition_(condition),explicit_condition_(true){}
    unsigned spatial_vertices()const{return world_.vertices();}
    bool commit_release() {
        const auto fail=[&]{return staging_.commit_release(Frame{},false);};
        if(staging_.pending_release_round()!=round_||!world_.budget(40000))return fail();
        Frame frame;if(!release_frame(frame)||!world_.budget(35000))return fail();
        // This is preparation evidence, not an irreversible trigger receipt.
        // Output may suspend: the final current-clock/fuel check precedes the
        // pure acknowledgement, with no SDK reads or logging after it.
        std::printf("plan4 fixture prepared actor=%d round=%d condition=%d x=%d y=%d bx=%d by=%d bag=%u\n",
            frame.actor,round_,int(condition_),frame.position.x,frame.position.y,config_.blocker.x,config_.blocker.y,frame.bag);
        if(!world_.budget(31000))return fail(); // Leave headroom for the pure acknowledgement/caller handoff.
        return staging_.commit_release(frame,true);
    }
    Action turn() {
        round_=int(cambc::round_number());world_.begin(round_);
        if(staging_.released())return Action::model;
        auto& me=cambc::self();
        if(!configured_) {
            if(!world_.budget(90000))return finish(Action::handled);
            const auto roster=cambc::champions(cambc::team::ally);std::array<int,5> ids{};
            if(roster.size()>ids.size()){unsupported("roster");return finish(Action::handled);}
            for(std::size_t n=0;n<roster.size();++n)ids[n]=id(roster[n]);
            bool north=false;
            if(!roles(std::span(ids.data(),roster.size()),id(me),role_,partner_)||!north_spawn(cambc::team::ally,north)) {
                unsupported("map-or-roster");return finish(Action::handled);}
            if(!explicit_condition_)condition_=public_condition();
            if(condition_==Condition::unknown){unsupported("public-draft-profile");return finish(Action::handled);}
            config_=configuration(north,condition_);staging_=Staging(role_,id(me),partner_,config_);configured_=true;
        }
        if(unsupported_)return finish(Action::handled);
        Frame frame;frame.round=round_;frame.actor=id(me);frame.bag=me.gem_count();frame.alive=me.is_alive();
        if(frame.alive) {
            frame.position=point(me.position());frame.channel=me.is_recalling()||me.is_submitting_gems()||me.is_teleporting();
            frame.on_spawn=cambc::map::spawn_region(cambc::team::ally).is_set(tile(frame.position));
            frame.full_health=me.health()>=me.max_health();
            if(role_!=Role::idle && (staging_.acquired()||role_==Role::blocker||frame.bag>0)) {
                const auto allies=cambc::vision::champions(cambc::team::ally), enemies=cambc::vision::champions(cambc::team::enemy);
                if(allies.size()>5||enemies.size()>5){unsupported("vision-prefix");return finish(Action::handled);}
                const auto own_wards=me.ward_positions();
                if(own_wards.size()>16){unsupported("ward-prefix");return finish(Action::handled);}
                const auto ward_goal=role_==Role::subject?config_.blocker:config_.actor;
                for(auto p:own_wards)frame.own_ward_present|=point(p)==ward_goal;
                const auto visible=cambc::vision::visible_tiles(),occupied=cambc::vision::champion_tiles();
                for(const auto& ally:allies)if(id(ally)==partner_){frame.partner=partner_;frame.partner_position=point(ally.position());
                    partner_view_=ally;partner_view_round_=round_;
                    frame.partner_round=round_;frame.partner_personally_visible=true;
                    frame.partner_tile_visible=visible.is_set(tile(frame.partner_position));frame.partner_tile_occupied=occupied.is_set(tile(frame.partner_position));}
                bool enemy_near=false;
                for(const auto& enemy:enemies){const int d=distance(frame.position,point(enemy.position()));frame.adjacent_enemy|=d<=1;enemy_near|=d<=8;}
                frame.recall_safe=world_.prepared()&&world_.safe_parking(frame.position)&&!enemy_near;
            }
        }
        frame.fresh=world_.budget();const auto command=staging_.next(frame);
        if(int(command.kind)!=last_kind_){last_kind_=int(command.kind);std::printf("plan4 fixture stage actor=%d round=%d role=%d command=%d bag=%u\n",frame.actor,round_,int(role_),last_kind_,frame.bag);}
        if(command.kind==Kind::warmup)return finish(Action::warmup);
        if(command.kind==Kind::model) {
            if(world_.budget(85000))return Action::prepare;
            staging_.commit_release(Frame{},false);
        } else if(command.kind==Kind::move || command.kind==Kind::ward || command.kind==Kind::home || command.kind==Kind::recall) {
            if(world_.prepare(role_==Role::subject?config_.waiting:Point{-1,-1})) {
                if(command.kind==Kind::move)world_.move(command.goal);
                else if(command.kind==Kind::home)world_.home();
                else if(command.kind==Kind::recall) {
                    const auto state=me.state();
                    if(world_.budget(85000)&&state.contains(cambc::champion_state::can_cast)&&!state.contains(cambc::champion_state::recall_started))me.recall();
                }
                else if(world_.budget(85000)&&me.can_cast()&&me.ward_cooldown()==0&&me.ward_placement_tiles().is_set(tile(command.goal))&&
                        !cambc::vision::has_ward(tile(command.goal),cambc::team::ally)) {
                    const bool placed=me.place_ward(tile(command.goal));if(world_.budget(70000))staging_.acknowledge(command,placed);
                }
            }
            if(world_.failed())unsupported("setup-route-unreachable");
        }
        return finish(Action::handled);
    }
};
class Parking {
    World world_;Point subject_{},origin_{},goal_{};std::array<ParkingChoice,5> choices_{};
    int rank_=-1,cursor_=0,round_=-1,summary_=-1;bool selected_=false,failed_=false;
public:
    void turn() {
        round_=int(cambc::round_number());world_.begin(round_);auto& me=cambc::self();
        if(world_.budget(12000)&&(summary_<0||round_%32==0)&&summary_!=round_) {summary_=round_;
            plan4::LogRow("summary",PLAN4_REFERENCE_VERSION).field("actor",id(me)).field("round",round_).field("fuel",cambc::fuel_remaining())
                .field("bag",me.gem_count()).field("score",cambc::global_submitted_gems(cambc::team::ally)).field("active",false).emit();}
        if(!me.is_alive())return;
        if(rank_<0) {
            if(!world_.budget(90000))return;bool north=false;
            if(!north_spawn(cambc::team::enemy,north)){failed_=true;return;}
            subject_=configuration(north,Condition::positive).actor;
            const auto spawn=cambc::map::spawn_region(cambc::team::ally).positions();
            if(spawn.empty()){failed_=true;return;}origin_=point(spawn.front());
            for(auto p:spawn)if(plan4::point_less(point(p),origin_))origin_=point(p);
            const auto roster=cambc::champions(cambc::team::ally);std::array<int,5> ids{};
            if(roster.empty()||roster.size()>ids.size()){failed_=true;return;}
            for(std::size_t n=0;n<roster.size();++n)ids[n]=id(roster[n]);std::sort(ids.begin(),ids.begin()+roster.size());
            const auto own=std::find(ids.begin(),ids.begin()+roster.size(),id(me));
            if(own==ids.begin()+roster.size()){failed_=true;return;}rank_=int(own-ids.begin());
        }
        if(!failed_ && !me.is_recalling()&&!me.is_submitting_gems()&&!me.is_teleporting()&&world_.prepare()) {
            while(cursor_<world_.size() && world_.charge()) {const auto p=world_.at(cursor_++);
                consider_parking(choices_,p,origin_,subject_,world_.open(p),world_.camp(p),world_.safe_parking(p));}
            if(cursor_==world_.size()&&!selected_){if(!choices_[rank_].valid)failed_=true;else{goal_=choices_[rank_].point;selected_=true;
                std::printf("plan4 fixture parking actor=%d round=%d x=%d y=%d subjectx=%d subjecty=%d\n",id(me),round_,goal_.x,goal_.y,subject_.x,subject_.y);}}
            if(selected_)world_.move(goal_);
            failed_|=world_.failed();
        }
        if(failed_&&round_%32==0)std::printf("plan4 fixture unsupported reason=parking actor=%d round=%d\n",id(me),round_);
    }
};
inline void draft() {
    constexpr std::array positive{cambc::champion_draft::varg,cambc::champion_draft::vendetta,cambc::champion_draft::tavi,cambc::champion_draft::marcella};
    constexpr std::array negative{cambc::champion_draft::vesper_vale,cambc::champion_draft::vendetta,cambc::champion_draft::tavi,cambc::champion_draft::marcella};
    const auto& order=PLAN4_ARCHIPELAGO_CONDITION==0?positive:negative;
    const auto current=cambc::draft::current_draft(cambc::team::ally);
    if(current.size()>=order.size())throw std::runtime_error("archipelago fixture draft capacity");
    cambc::draft::pick(order[current.size()]);
}
} // namespace plan4_archipelago_fixture
#endif
