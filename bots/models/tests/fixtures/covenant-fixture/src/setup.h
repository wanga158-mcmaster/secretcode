#pragma once
#include <cambc.h>
#include "../../../../common/movement.h"
#include "../../../../common/draft-sdk.h"
#include "../../../../common/telemetry.h"
#include "policy.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <span>
#include <vector>

// This source stages a candidate experiment. It does not establish that the
// farmer reaches this checkpoint, survives the encounter, or chooses the pair.
// Opponent instructions stay here and never enter model inputs or forecasts.
namespace plan4_covenant_fixture {
using plan4::Point;
inline constexpr Point first{27,26}, second{17,26}, checkpoint{28,26};
inline constexpr Point encounter{25,28}, withdrawal{24,29};
inline Point point(cambc::vec2 p) { return {p.x,p.y}; }
inline cambc::vec2 tile(Point p) { return {p.x,p.y}; }
inline int id(const cambc::champion& c) { return int(static_cast<cambc::handle_champion_t>(c)); }
inline int distance(Point a,Point b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
inline bool channel(cambc::champion_state s) {
    return s.contains(cambc::champion_state::recalling)||s.contains(cambc::champion_state::submitting_gems)||
        s.contains(cambc::champion_state::teleporting);
}
inline int primary(cambc::team side) {
    // Global roster reads are immutable identities only. Bodies below are read
    // only from the current champion's personal vision list.
    const auto roster=cambc::champions(side);
    if(roster.empty()||roster.size()>5)return -1;
    int lowest=id(roster.front());
    for(const auto& c:roster)lowest=std::min(lowest,id(c));
    return lowest;
}
inline void draft() {
    plan4::drafting::sdk::run(plan4::drafting::Profile::covenant,
        [](auto,auto pick,int selected,int capacity) {
            std::printf("plan4 fixture draft fixture=covenant pick=%u selected=%d capacity=%d\n",
                unsigned(pick),selected,capacity);
        });
}

// Public immutable topology is copied incrementally. Existing ordinary BFS
// supplies at most 128 newly expanded vertices per setup turn. Current SDK
// movement masks remain the authority for each attempted adjacent move.
class World {
    cambc::bitboard walls_;
    std::vector<cambc::map::camp_group> groups_;
    std::vector<unsigned char> topology_,camps_;
    plan4::PersistentMovement movement_;
    std::size_t group_=0,member_=0,copy_=0;
    Point initial_{};
    int round_=-1,first_id_=-1,second_id_=-1,first_index_=-1;
    unsigned used_=0;
    bool loaded_=false,ready_=false,failed_=false,first_found_=false,second_found_=false;
    static bool allowed(const void* raw) { return static_cast<const World*>(raw)->budget(); }
    static bool blocked(Point p,const void* raw) { return !static_cast<const cambc::bitboard*>(raw)->is_set(tile(p)); }
    bool match(const cambc::map::camp_group& group,Point p) {
        const auto members=group.camps();
        return members.size()==1 && point(members.front().spawn_position())==p &&
            members.front().kind()==cambc::camp_type::gem_cluster && group.gem_value()>0;
    }
public:
    void begin(int round) { if(round_!=round){round_=round;used_=0;} }
    bool budget(unsigned floor=80000) const { return int(cambc::round_number())==round_ && cambc::fuel_remaining()>=floor; }
    bool failed() const { return failed_; }
    unsigned vertices() const { return used_; }
    int first_id() const { return first_id_; }
    int second_id() const { return second_id_; }
    int first_state() const {
        // Lifecycle reads are permitted only after this actor personally sees
        // the audited first tile in this very owner frame. Unknown stays -1.
        if(!ready_||first_index_<0||std::size_t(first_index_)>=groups_.size()||!budget(70000))return -1;
        const auto visible=cambc::vision::visible_tiles();
        if(!budget(70000)||!visible.is_set(tile(first)))return -1;
        const auto state=groups_[first_index_].state();
        if(!budget(70000))return -1;
        if(std::holds_alternative<cambc::map::camp_group_alive>(state))return 0;
        if(std::holds_alternative<cambc::map::camp_group_reward_available>(state))return 1;
        if(std::holds_alternative<cambc::map::camp_group_respawning>(state))return 2;
        return -1;
    }
    bool open(Point p) const { return ready_&&p.x>=0&&p.y>=0&&p.x<45&&p.y<35&&!topology_[p.y*45+p.x]; }
    bool charge() { if(used_>=128||!budget())return false;++used_;return true; }
    bool prepare() {
        if(failed_)return false;
        if(ready_)return true;
        if(!loaded_) {
            if(!budget(90000))return false;
            if(cambc::map::width()!=45||cambc::map::height()!=35){failed_=true;return false;}
            initial_=point(cambc::self().position());
            walls_=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(cambc::team::enemy);
            groups_=cambc::map::camp_groups();
            if(groups_.empty()||groups_.size()>64){failed_=true;return false;}
            topology_.resize(45*35);camps_.resize(45*35);loaded_=true;
        }
        while(group_<groups_.size()) {
            const auto& g=groups_[group_];const auto members=g.camps();
            if(members.empty()||members.size()>32){failed_=true;return false;}
            while(member_<members.size()) {
                if(!charge())return false;
                const auto p=point(members[member_].spawn_position());
                if(p.x<0||p.y<0||p.x>=45||p.y>=35){failed_=true;return false;}
                camps_[p.y*45+p.x]=1;++member_;
            }
            const int actual=int(static_cast<cambc::handle_camp_group_t>(g));
            if(match(g,first)) {
                if(first_found_){failed_=true;return false;}first_found_=true;first_id_=actual;first_index_=int(group_);
                std::printf("plan4 fixture geometry fixture=covenant role=first group=%d x=%d y=%d gems=%u\n",actual,first.x,first.y,g.gem_value());
            }
            if(match(g,second)) {
                if(second_found_){failed_=true;return false;}second_found_=true;second_id_=actual;
                std::printf("plan4 fixture geometry fixture=covenant role=second group=%d x=%d y=%d gems=%u\n",actual,second.x,second.y,g.gem_value());
            }
            ++group_;member_=0;
        }
        if(!first_found_||!second_found_||first_id_==second_id_){failed_=true;return false;}
        while(copy_<topology_.size()) {
            if(!charge())return false;
            const Point p{int(copy_%45),int(copy_/45)};
            topology_[copy_]=static_cast<unsigned char>(walls_.is_set(tile(p))||(p!=initial_&&camps_[copy_]));++copy_;
        }
        for(const Point p:{checkpoint,encounter,withdrawal})
            if(topology_[p.y*45+p.x]||camps_[p.y*45+p.x]){failed_=true;return false;}
        if(!budget(85000))return false;
        movement_.initialize(45,35,topology_);ready_=true;
        return true;
    }
    bool free(Point p) const {
        if(!open(p)||!budget())return false;
        if(point(cambc::self().position())==p)return true;
        return !cambc::vision::champion_tiles().is_set(tile(p))&&!cambc::vision::camp_tiles().is_set(tile(p))&&
            !cambc::vision::teleport_tiles().is_set(tile(p));
    }
    bool move_goals(std::span<const Point> goals) {
        auto& me=cambc::self();
        if(goals.empty()||!budget()||!me.can_move())return false;
        const auto origin=point(me.position());
        auto obstacles=walls_|cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
        obstacles.set(tile(origin),false);
        const auto legal=obstacles.reachable_tiles(tile(origin),1);
        if(!budget())return false;
        movement_.begin_turn(0,128-used_);
        const auto step=movement_.next_step(origin,goals,blocked,&legal,128-used_,{allowed,this});used_+=step.visits;
        if(step.status==plan4::MovementStatus::unreachable||step.status==plan4::MovementStatus::invalid_origin){failed_=true;return false;}
        if(step.status!=plan4::MovementStatus::ready||!budget(70000)||!me.can_move()||!legal.is_set(tile(step.next)))return false;
        const bool success=me.move_absolute(tile(step.next));movement_.notify_move_result(origin,step.next,success);
        return success;
    }
    bool move(Point p) { if(!open(p))return false;const std::array goals{p};return move_goals(goals); }
    void home() {
        const auto spawn=cambc::map::spawn_region(cambc::team::ally);
        const auto available=(spawn&~cambc::vision::champion_tiles()).positions();
        std::array<Point,16> goals{};std::size_t count=0;
        if(available.size()>goals.size()){failed_=true;return;}
        for(auto p:available)if(open(point(p)))goals[count++]=point(p);
        move_goals(std::span(goals.data(),count));
    }
};

class Setup {
public:
    enum class Action { handled,warmup,prepare,model };
private:
    World world_;
    int round_=-1,primary_=-1,pending_=-1,note_=-1,note_round_=-32,summary_=-1;
    bool configured_=false,staging_=false,released_=false,failed_=false;
    void note(int code,const char* reason) {
        if(world_.budget(14000)&&(code!=note_||round_-note_round_>=32)) {
            auto& me=cambc::self();
            const auto p=me.is_alive()?point(me.position()):Point{-1,-1};
            std::printf("plan4 fixture stage fixture=covenant actor=%d round=%d reason=%s x=%d y=%d bag=%u health=%.3f maximum=%.3f\n",
                id(me),round_,reason,p.x,p.y,me.gem_count(),double(me.health()),double(me.max_health()));
            note_=code;note_round_=round_;
        }
    }
    Action finish(Action action) {
        if(world_.budget(12000)&&(summary_<0||round_-summary_>=32)) {
            summary_=round_;auto& me=cambc::self();
            plan4::LogRow("summary",PLAN4_REFERENCE_VERSION).field("actor",id(me)).field("round",round_)
                .field("fuel",cambc::fuel_remaining()).field("bag",me.gem_count())
                .field("score",cambc::global_submitted_gems(cambc::team::ally)).field("active",false).emit();
        }
        return action;
    }
    bool fresh_body(unsigned floor) {
        if(!world_.budget(floor))return false;auto& me=cambc::self();
        if(id(me)!=primary_||!me.is_alive()||me.gem_count()!=0||channel(me.state())||point(me.position())!=checkpoint)return false;
        const float health=me.health(),maximum=me.max_health();
        return std::isfinite(health)&&std::isfinite(maximum)&&maximum>0&&health>=maximum&&world_.budget(floor);
    }
public:
    unsigned spatial_vertices() const { return world_.vertices(); }
    bool commit_release() {
        const bool ticket=pending_==round_;pending_=-1;
        if(!ticket||released_||failed_||!fresh_body(35000))return false;
        // The common caller already admitted the complete natural menu and
        // actual chosen pair. This callback only rechecks its physical setup.
        std::printf("plan4 fixture prepared fixture=covenant actor=%d round=%d x=%d y=%d bag=0 first=%d second=%d nativeReady=0\n",
            primary_,round_,checkpoint.x,checkpoint.y,world_.first_id(),world_.second_id());
        if(!fresh_body(31000))return false;
        released_=true;return true; // No SDK calls after this irreversible bit.
    }
    Action turn() {
        round_=int(cambc::round_number());world_.begin(round_);pending_=-1;
        if(released_)return Action::model;
        auto& me=cambc::self();
        if(!configured_) {
            if(!world_.budget(92000))return finish(Action::handled);
            primary_=primary(cambc::team::ally);configured_=true;
            if(primary_<0||cambc::map::width()!=45||cambc::map::height()!=35) {
                failed_=true;note(0,"unsupported-map-or-roster");
            }
        }
        if(failed_)return finish(Action::handled);
        if(id(me)!=primary_)return finish(Action::handled); // Other actors stay on their actual spawn.
        if(round_>=int(cambc::constants::max_rounds)-1)note(14,"unreleased-at-round-limit-censored");
        if(!me.is_alive()) {note(1,"subject-dead-censored");if(staging_)failed_=true;return finish(Action::handled);}
        if(channel(me.state())) {note(2,"preserve-channel");return finish(Action::handled);}
        const int release_window=std::max(0,int(cambc::constants::max_rounds)-200);
        if(round_<8||round_<release_window) {note(3,"ordinary-warmup");return finish(Action::warmup);}
        const float health=me.health(),maximum=me.max_health();
        const bool full=std::isfinite(health)&&std::isfinite(maximum)&&maximum>0&&health>=maximum;
        if(me.gem_count()!=0||!full) {
            note(me.gem_count()?4:5,me.gem_count()?"waiting-empty-bag":"waiting-full-health");
            if(staging_){failed_=true;note(6,"checkpoint-body-lost-censored");return finish(Action::handled);}
            return finish(Action::warmup);
        }
        staging_=true;
        if(!world_.prepare()) {
            if(world_.failed()){failed_=true;note(7,"unsupported-public-geometry");}
            else note(8,"bounded-topology-preparation");
            return finish(Action::handled);
        }
        if(point(me.position())!=checkpoint) {
            if(!world_.free(checkpoint)){note(9,"checkpoint-currently-occupied");return finish(Action::handled);}
            note(10,"moving-to-checkpoint");world_.move(checkpoint);
            if(world_.failed()){failed_=true;note(11,"setup-route-unreachable-censored");}
            return finish(Action::handled);
        }
        if(!fresh_body(85000)){note(12,"checkpoint-freshness-or-fuel");return finish(Action::handled);}
        note(13,"natural-menu-admission-pending");
        if(!fresh_body(85000))return finish(Action::handled);
        pending_=round_;return Action::prepare;
    }
};

enum class Condition { encounter, parked_negative };
class Opponent {
    World world_;Condition condition_;
    EncounterSequence sequence_;
    int round_=-1,primary_=-1,subject_=-1,note_=-1,note_round_=-32;
    bool configured_=false,arrived_=false,withdrawing_=false,withdrawn_=false,failed_=false;
    void note(int code,const char* reason) {
        if(world_.budget(12000)&&(code!=note_||round_-note_round_>=32)) {
            std::printf("plan4 fixture opponent fixture=covenant actor=%d round=%d condition=%d reason=%s checkpointSeen=%d holdSeen=%d captureSeen=%d intervals=%d nativeReady=0\n",
                id(cambc::self()),round_,int(condition_),reason,sequence_.checkpoint_seen,sequence_.hold_seen,
                sequence_.capture_seen,sequence_.intervals);note_=code;note_round_=round_;
        }
    }
public:
    explicit Opponent(Condition condition):condition_(condition){}
    void turn() {
        round_=int(cambc::round_number());world_.begin(round_);auto& me=cambc::self();
        if(!configured_) {
            if(!world_.budget(90000))return;
            primary_=primary(cambc::team::ally);subject_=primary(cambc::team::enemy);configured_=true;
            if(primary_<0||subject_<0||cambc::map::width()!=45||cambc::map::height()!=35)failed_=true;
            if(id(me)==primary_&&me.champion_name()!=cambc::champion_type::marcella)failed_=true;
        }
        if(failed_){note(0,"unsupported-map-roster-or-route");return;}
        if(id(me)!=primary_)return; // No remote-body reads or actions for parked teammates.
        if(!me.is_alive()){failed_=true;note(2,"opponent-dead-censored");return;}
        if(channel(me.state())){note(3,"preserve-channel");return;}
        const int departure=std::max(0,int(cambc::constants::max_rounds)-260);
        if(round_<departure) {
            const auto p=me.position();
            if(cambc::map::spawn_region(cambc::team::ally).is_set(p)&&world_.budget(85000)&&
                me.can_buy(cambc::item_type::power_shard_of_health))me.buy(cambc::item_type::power_shard_of_health);
            note(4,"affordable-health-at-own-spawn");return;
        }
        if(condition_==Condition::parked_negative){note(1,"negative-own-spawn");return;}
        if(!world_.prepare()) {if(world_.failed()){failed_=true;note(0,"unsupported-map-roster-or-route");}else note(5,"bounded-topology-preparation");return;}
        const auto p=point(me.position());
        if(withdrawn_) {
            note(6,"retreat-home");
            if(!cambc::map::spawn_region(cambc::team::ally).is_set(tile(p)))world_.home();
        } else if(withdrawing_) {
            note(7,"withdrawal-step-pending");
            // The audited retreat starts with this exact legal adjacent step.
            // Never replace a blocked step with an assumed withdrawal receipt.
            if(p==withdrawal){withdrawn_=true;note(8,"withdrawal-observed");}
            else if(p==encounter&&world_.free(withdrawal)&&world_.move(withdrawal)) {
                if(world_.budget(60000)&&point(me.position())==withdrawal){withdrawn_=true;note(8,"withdrawal-observed");}
            }
        } else if(p!=encounter) {
            if(arrived_){failed_=true;note(9,"encounter-position-lost-censored");return;}
            note(10,"moving-to-encounter");world_.move(encounter);
        } else {
            arrived_=true;
            const auto visible=cambc::vision::champions(cambc::team::enemy);
            if(visible.size()>5){failed_=true;note(11,"visible-prefix-unsupported");return;}
            bool subject_visible=false;Point subject_position{-1,-1};
            for(const auto& enemy:visible)if(id(enemy)==subject_&&enemy.is_alive()&&enemy.is_visible()) {
                subject_visible=true;subject_position=point(enemy.position());
            }
            const int lifecycle=world_.first_state();
            const int release_window=std::max(0,int(cambc::constants::max_rounds)-200);
            if(world_.budget(70000))sequence_.observe(round_,
                round_>=release_window&&subject_visible&&subject_position==checkpoint&&lifecycle==0,
                subject_visible&&subject_position==first&&lifecycle==1,
                lifecycle==2,subject_visible&&distance(p,subject_position)<=1);
            note(12,"waiting-observed-encounter-sequence");
            if(sequence_.withdrawal_due()) {
                withdrawing_=true;note(7,"withdrawal-step-pending");
                if(world_.free(withdrawal)&&world_.move(withdrawal)&&world_.budget(60000)&&point(me.position())==withdrawal) {
                    withdrawn_=true;note(8,"withdrawal-observed");
                }
            }
        }
        if(world_.failed()){failed_=true;note(0,"unsupported-map-roster-or-route");}
    }
};
} // namespace plan4_covenant_fixture
