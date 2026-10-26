#pragma once
#ifndef PLAN4_FIXTURE_BUILD
#define PLAN4_FIXTURE_BUILD "fixture-development-v1"
#endif
#include "../../../../counterpart/src/counterpart.h"
#include "../../../../commons/src/commons.h"
#include "../../../../common/movement.h"
#include <span>

namespace plan4_fixture {
using plan4::Point;
inline int designated_producer(int own,std::span<const int> public_roster){
    if(own<0)return-1;
    int producer=own;
    for(int actor:public_roster){if(actor<0)return-1;producer=std::min(producer,actor);}return producer;
}
inline bool canonical_anchor(std::span<const Point> public_spawn,Point& anchor){
    if(public_spawn.empty())return false;
    Point selected=public_spawn.front();
    for(Point p:public_spawn){if(p.x<0||p.y<0)return false;if(plan4::point_less(p,selected))selected=p;}
    anchor=selected;return true;
}
enum class RouteKnowledge {exact,pending,unsupported};
inline RouteKnowledge route_knowledge(const plan4::MovementStep& route){
    if(route.remaining_distance>=0)return RouteKnowledge::exact;
    return route.status==plan4::MovementStatus::unreachable||route.status==plan4::MovementStatus::invalid_origin
        ?RouteKnowledge::unsupported:RouteKnowledge::pending;
}
struct EnemySample {int id=-1;Point position{};unsigned bag=0;plan4::Provenance provenance{};bool visible=false;};
inline bool targets(int round,std::span<const EnemySample> samples,
                    std::array<plan4::counterpart::Target,8>& output,int& count){
    count=0;if(samples.size()>output.size())return false;
    for(const auto& sample:samples){
        if(!sample.visible||sample.provenance.observed_round!=round||sample.provenance.source!=plan4::Source::personal_vision
            ||sample.provenance.status!=plan4::Status::exact_now||sample.id<0)return false;
        for(int n=0;n<count;++n)if(output[n].id==sample.id)return false;
        output[count++]={sample.id,sample.position,sample.bag,true};
    }return true;
}
inline plan4::commons::Stock stock(const plan4::Group& group,int round){
    if(round<0||!group.visible||!group.occupancy_known||group.contested||group.provenance.observed_round!=round
        ||group.provenance.status!=plan4::Status::exact_now||group.provenance.source!=plan4::Source::personal_vision)return{};
    if(group.lifecycle==plan4::Lifecycle::living||(group.lifecycle==plan4::Lifecycle::cleared&&group.reward_available))return{round,0,true,false};
    if((group.lifecycle==plan4::Lifecycle::captured||group.lifecycle==plan4::Lifecycle::respawning)
        &&!group.occupied_spawn&&group.respawn_remaining>=0&&group.respawn_remaining<=std::numeric_limits<int>::max()-round)
        return{round+group.respawn_remaining,0,true,true};
    return{};
}
// A pure read-only distance table. Missing queries remain unknown. Each table
// is populated by a resumable public-terrain BFS before invoking either rule.
struct Distances {
    Point source{};std::array<Point,plan4::max_groups+plan4::max_altars+8> points{};
    std::array<int,plan4::max_groups+plan4::max_altars+8> values{};int count=0;
    void root(Point point){if(source!=point){source=point;count=0;}}
    int get(Point point)const{if(point==source)return 0;for(int n=0;n<count;++n)if(points[n]==point)return values[n];return-1;}
    bool put(Point point,int value){if(value<0)return false;if(get(point)>=0)return true;
        if(count==int(points.size()))return false;
        points[count]=point;values[count++]=value;return true;}
    static int lookup(Point a,Point b,const void* context){const auto& self=*static_cast<const Distances*>(context);
        if(a==b)return 0;
        if(a==self.source)return self.get(b);
        if(b==self.source)return self.get(a);
        return-1;}
};
}

#ifndef PLAN4_FIXTURE_NO_SDK
#include "cambc.h"
#include "../../../../common/telemetry.h"
#include <cstdio>
#include <memory>
#include <variant>
#include <vector>

namespace plan4_fixture {
inline Point point(cambc::vec2 p){return{p.x,p.y};}
inline cambc::vec2 tile(Point p){return{p.x,p.y};}
inline int id(const cambc::champion& actor){return int(static_cast<cambc::handle_champion_t>(actor));}
inline int range(Point a,Point b){return plan4::counterpart::chebyshev(a,b);}
inline void draft(){
    constexpr std::array order{cambc::champion_draft::varg,cambc::champion_draft::vendetta,cambc::champion_draft::tavi,
        cambc::champion_draft::marcella,cambc::champion_draft::vesper_vale};
    const auto picked=cambc::draft::current_draft(cambc::team::ally);
    for(auto choice:order)if(std::find(picked.begin(),picked.end(),choice)==picked.end()){cambc::draft::pick(choice);return;}
}
template<bool Commons> class Bot {
    std::unique_ptr<plan4::Information> info_;
    std::vector<cambc::map::camp_group> groups_;
    std::vector<cambc::vec2> blocked_positions_,altar_positions_,spawn_positions_;
    std::vector<unsigned char> topology_,visible_;
    std::vector<int> group_at_;
    plan4::PersistentMovement source_,movement_;
    Distances distances_;
    plan4::commons::Priority priority_;
    std::array<plan4::counterpart::Camp,32> camps_{};
    std::array<plan4::commons::Stock,plan4::max_groups> stock_{};
    std::array<float,plan4::max_groups> full_health_{},incoming_{};
    std::array<EnemySample,8> enemies_{};
    int enemy_count_=0,width_=0,height_=0,camp_count_=0,cursor_=0,target_=-1,bank_=-1;
    std::size_t copied_blocked_=0,metadata_group_=0,metadata_member_=0,priority_query_=0;
    Point initial_spawn_{};bool map_loaded_=false,topology_ready_=false,metadata_ready_=false,priority_ready_=false,retreat_=false;
    bool role_known_=!Commons,active_=true,support_failed_=false;
    Point last_unreachable_{-1,-1};
    std::uint64_t floor_=50000;
    static bool budget(const void* self){return cambc::fuel_remaining()>=static_cast<const Bot*>(self)->floor_+5000;}
    bool fuel(unsigned margin=5000)const{return cambc::fuel_remaining()>=floor_+margin;}
    int index(Point p)const{return p.y*width_+p.x;}
    bool walkable(Point p)const{return p.x>=0&&p.y>=0&&p.x<width_&&p.y<height_&&!topology_[index(p)];}
    static bool occupied(Point p,const void*){return cambc::vision::has_champion(tile(p))||cambc::vision::has_camp(tile(p));}
    bool initialize(){
        if(!map_loaded_){
            if(!fuel(25000))return false;
            width_=cambc::map::width();height_=cambc::map::height();
            const auto blocked=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(cambc::team::enemy);
            blocked_positions_=blocked.positions();altar_positions_=cambc::map::altars().positions();
            spawn_positions_=cambc::map::spawn_region(cambc::team::ally).positions();groups_=cambc::map::camp_groups();
            std::vector<Point> public_spawn;for(auto p:spawn_positions_)public_spawn.push_back(point(p));
            if(!canonical_anchor(public_spawn,initial_spawn_))throw std::runtime_error("fixture missing public spawn anchor");
            plan4::LogRow("fixture_init").field("model",Commons?"commons":"counterpart").field("build",PLAN4_FIXTURE_BUILD).field("r",cambc::round_number())
                .field("id",id(cambc::self())).field("anchor_rule","public_spawn_min_xy").field("anchorx",initial_spawn_.x).field("anchory",initial_spawn_.y).emit();
            if(groups_.size()>plan4::max_groups||altar_positions_.size()>plan4::max_altars)throw std::runtime_error("fixture public layout capacity");
            topology_.assign(std::size_t(width_)*height_,0);visible_.assign(topology_.size(),0);group_at_.assign(topology_.size(),-1);
            info_=std::make_unique<plan4::Information>();info_->group_count=int(groups_.size());info_->public_layout_truncated=true;
            map_loaded_=true;
        }
        while(copied_blocked_<blocked_positions_.size()){
            if(!fuel())return false;const auto end=std::min(copied_blocked_+16,blocked_positions_.size());
            for(;copied_blocked_<end;++copied_blocked_)topology_[index(point(blocked_positions_[copied_blocked_]))]=1;
        }
        if(!topology_ready_){if(!fuel(18000))return false;source_.initialize(width_,height_,topology_);movement_.initialize(width_,height_,topology_);topology_ready_=true;}
        while(metadata_group_<groups_.size()){
            if(!fuel(12000))return false;
            auto& group=info_->groups[metadata_group_];const auto& sdk=groups_[metadata_group_];
            if(!metadata_member_){group.id=int(static_cast<cambc::handle_camp_group_t>(sdk));group.gems=sdk.gem_value();group.respawn_rounds=sdk.max_respawn_timer();
                if(sdk.camps().empty())throw std::runtime_error("fixture empty public group");group.position=point(sdk.camps().front().spawn_position());}
            while(metadata_member_<sdk.camps().size()){
                if(!fuel(10000))return false;const auto& member=sdk.camps()[metadata_member_++];const auto stats=member.base_stats();
                const auto p=point(member.spawn_position());group_at_[index(p)]=int(metadata_group_);
                full_health_[metadata_group_]+=stats.base_health;incoming_[metadata_group_]+=stats.base_strength;
            }
            ++metadata_group_;metadata_member_=0;
        }
        if(!metadata_ready_){
            for(std::size_t n=0;n<altar_positions_.size();++n){auto& altar=info_->altars[n];altar.id=int(n);altar.altar_position=point(altar_positions_[n]);
                const int r=cambc::constants::submit_gems_range;
                for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){Point p{altar.altar_position.x+dx,altar.altar_position.y+dy};
                    if(walkable(p)&&(!altar.accessible||plan4::point_less(p,altar.position))){altar.position=p;altar.accessible=true;}}}
            info_->altar_count=int(altar_positions_.size());info_->public_layout_truncated=false;metadata_ready_=true;
        }
        return true;
    }
    bool distances(Point root,std::span<const Point> requested,bool required_layout=false){
        distances_.root(root);const std::array goals{root};bool complete=true;
        for(auto destination:requested){
            if(distances_.get(destination)>=0)continue;if(!fuel())return false;
            const auto answer=source_.next_step(destination,goals,nullptr,nullptr,192,{budget,this});
            if(route_knowledge(answer)==RouteKnowledge::unsupported){
                support_failed_|=required_layout;
                if(required_layout||last_unreachable_!=destination){last_unreachable_=destination;
                    plan4::LogRow("fixture_unsupported").field("model",Commons?"commons":"counterpart").field("r",cambc::round_number())
                        .field("id",id(cambc::self())).field("reason",required_layout?"public_layout_unreachable":"decision_route_unreachable")
                        .field("x",destination.x).field("y",destination.y).emit();}
                return false;
            }
            if(answer.remaining_distance>=0)distances_.put(destination,answer.remaining_distance);else complete=false;
        }return complete;
    }
    bool freeze_priority(){
        if(priority_ready_)return true;
        if constexpr(Commons){
            distances_.root(initial_spawn_);
            while(priority_query_<groups_.size()){
                const std::array goal{info_->groups[priority_query_].position};if(!distances(initial_spawn_,goal,true))return false;++priority_query_;
            }
            info_->travel={Distances::lookup,&distances_};info_->work_budget={budget,this};
            if(!plan4::commons::freeze_priority(*info_,initial_spawn_,true,{0x434f4d4d4f4e5331ULL,true,true,true},priority_))return false;
        }else{
            // Fixed public order: harmless clusters, then other groups, each by ID.
            std::vector<int> order(groups_.size());for(int g=0;g<int(groups_.size());++g)order[g]=g;
            std::sort(order.begin(),order.end(),[&](int a,int b){if(bool(incoming_[a])!=bool(incoming_[b]))return !incoming_[a];return info_->groups[a].id<info_->groups[b].id;});
            camp_count_=std::min(int(order.size()),32);for(int n=0;n<camp_count_;++n)camps_[n]={info_->groups[order[n]].id,info_->groups[order[n]].position};
        }
        priority_ready_=true;return true;
    }
    bool observe(){
        if(!fuel(15000))return false;
        const auto mask=cambc::vision::visible_tiles().positions();std::fill(visible_.begin(),visible_.end(),0);
        for(auto p:mask)visible_[index(point(p))]=1;
        const auto enemies=cambc::vision::champions(cambc::team::enemy);if(enemies.size()>enemies_.size())return false;enemy_count_=0;
        const int round=int(cambc::round_number());
        for(const auto& enemy:enemies){if(!fuel())return false;enemies_[enemy_count_++]={id(enemy),point(enemy.position()),enemy.gem_count(),{round,plan4::Source::personal_vision,plan4::Status::exact_now},true};}
        if constexpr(Commons){
            std::array<bool,plan4::max_groups> touched{};
            for(auto p:mask){const int g=group_at_[index(point(p))];if(g>=0)touched[g]=true;}
            const auto allies=cambc::vision::champions(cambc::team::ally);std::vector<Point> ally_positions;for(const auto& ally:allies)ally_positions.push_back(point(ally.position()));
            for(int g=0;g<int(groups_.size());++g){stock_[g]={};if(!touched[g])continue;if(!fuel(10000))return false;
                auto& group=info_->groups[g];group.visible=true;group.provenance={round,plan4::Source::personal_vision,plan4::Status::exact_now};group.occupancy_known=true;group.occupied_spawn=false;
                bool our_holder=false,their_holder=false;
                for(const auto& member:groups_[g].camps()){const auto p=point(member.spawn_position());group.occupancy_known&=bool(visible_[index(p)]);
                    our_holder|=std::find(ally_positions.begin(),ally_positions.end(),p)!=ally_positions.end();for(int n=0;n<enemy_count_;++n)their_holder|=enemies_[n].position==p;}
                group.occupied_spawn=our_holder||their_holder;group.contested=our_holder&&their_holder;group.reward_available=false;group.respawn_remaining=-1;
                const auto state=groups_[g].state();
                if(std::holds_alternative<cambc::map::camp_group_alive>(state))group.lifecycle=plan4::Lifecycle::living;
                else if(std::holds_alternative<cambc::map::camp_group_reward_available>(state)){group.lifecycle=plan4::Lifecycle::cleared;group.reward_available=true;}
                else{group.lifecycle=plan4::Lifecycle::respawning;group.respawn_remaining=std::get<cambc::map::camp_group_respawning>(state).timer;}
                stock_[g]=stock(group,round);
            }
        }return true;
    }
    void move(std::span<const Point> goals){
        auto& me=cambc::self();if(!fuel()||!me.can_move())return;
        movement_.begin_turn(source_.vertex_visits());floor_=35000;
        const Point origin=point(me.position());const auto answer=movement_.next_step(origin,goals,occupied,nullptr,256,{budget,this});
        if(answer.status==plan4::MovementStatus::ready&&fuel())movement_.notify_move_result(origin,answer.next,me.move_absolute(tile(answer.next)));
    }
    bool safety(){
        auto& me=cambc::self();const auto p=point(me.position());const float hp=me.health(),maximum=me.max_health();
        const bool on_spawn=std::find(spawn_positions_.begin(),spawn_positions_.end(),tile(p))!=spawn_positions_.end();
        if(on_spawn){if(hp<maximum*.95f)return true;retreat_=false;}
        if(hp>=maximum*.4f&&!retreat_)return false;
        bool danger=false;for(int g=0;g<int(groups_.size());++g)if(incoming_[g]>0)for(const auto& camp:groups_[g].camps())danger|=range(p,point(camp.spawn_position()))<=3;
        for(int n=0;n<enemy_count_;++n)danger|=range(p,enemies_[n].position)<=8;
        const auto state=me.state();
        if(!danger&&!on_spawn&&(me.can_move()||me.can_attack()||me.can_cast())&&!state.contains(cambc::champion_state::recall_started)
            &&!state.contains(cambc::champion_state::recall_cancelled)){me.recall();return true;}
        std::vector<Point> goals;for(auto s:spawn_positions_)goals.push_back(point(s));move(goals);return true;
    }
    bool bank(){
        auto& me=cambc::self();if(!me.gem_count())return false;
        if(bank_<0){std::array<Point,plan4::max_altars> goals{};int count=0;for(int a=0;a<info_->altar_count;++a)if(info_->altars[a].accessible)goals[count++]=info_->altars[a].position;
            const auto p=point(me.position());if(!distances(p,{goals.data(),std::size_t(count)}))return true;
            info_->travel={Distances::lookup,&distances_};bank_=plan4::commons::fixture_altar(*info_,p);if(bank_<0)return true;}
        const auto altar=info_->altars[bank_].altar_position;
        if(range(point(me.position()),altar)<=cambc::constants::submit_gems_range){
            for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))if(point(lock.position())==altar){if(me.can_attack()&&range(point(me.position()),altar)<=me.attack_range())me.attack(lock);return true;}
            if(me.can_move()||me.can_attack()||me.can_cast())me.submit_gems(tile(altar));return true;
        }
        const std::array goal{info_->altars[bank_].position};move(goal);return true;
    }
    bool holding_capture(){
        if(target_<0)return false;
        bool on=false,seen=false;const auto own=point(cambc::self().position());
        for(const auto& camp:groups_[target_].camps()){const auto p=point(camp.spawn_position());on|=own==p;seen|=bool(visible_[index(p)]);}
        return on&&seen&&fuel()&&std::holds_alternative<cambc::map::camp_group_reward_available>(groups_[target_].state());
    }
    void farm(){
        auto& me=cambc::self();if(target_<0)return;
        const auto& sdk=groups_[target_];bool seen=false;for(const auto& camp:sdk.camps())seen|=bool(visible_[index(point(camp.spawn_position()))]);
        bool reward=false;
        if(seen){if(!fuel())return;const auto state=sdk.state();reward=std::holds_alternative<cambc::map::camp_group_reward_available>(state);
            if(std::holds_alternative<cambc::map::camp_group_respawning>(state)){
                target_=-1;bank_=-1;if constexpr(!Commons)cursor_=(cursor_+1)%std::max(1,camp_count_);return;}}
        const auto origin=point(me.position());
        if(reward){std::vector<Point> spawns;for(const auto& camp:sdk.camps())spawns.push_back(point(camp.spawn_position()));
            if(std::find(spawns.begin(),spawns.end(),origin)==spawns.end())move(spawns);return;}
        const float dps=std::max(1.0f,me.strength()*.75f);const int rounds=int(std::ceil(full_health_[target_]/dps));
        if(!plan4::affordable(me.health(),me.max_health(),incoming_[target_],rounds)){target_=-1;retreat_=true;safety();return;}
        std::vector<Point> goals;
        for(const auto& camp:sdk.camps()){
            if(!fuel())return;const auto p=point(camp.spawn_position());
            if(camp.is_visible()&&camp.is_alive()&&range(origin,point(camp.position()))<=me.attack_range()&&me.can_attack()){me.attack(camp);return;}
            const int r=me.attack_range();for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){Point q{p.x+dx,p.y+dy};if(walkable(q)&&q!=p)goals.push_back(q);}
        }
        if(!goals.empty())move(goals);
    }
public:
    void turn(){
        floor_=50000;source_.begin_turn(0,192);movement_.begin_turn(0,0);if(!fuel())return;
        if(support_failed_)return;
        auto& me=cambc::self();
        if constexpr(Commons){
            if(!role_known_){if(!fuel(10000))return;std::vector<int> public_roster;
                // Immutable public roster identities only: never query hidden
                // allies' positions, health, channels, bags or inventories.
                for(const auto& ally:cambc::champions(cambc::team::ally))public_roster.push_back(id(ally));
                const int producer=designated_producer(id(me),public_roster);if(producer<0)return;
                active_=id(me)==producer;role_known_=true;
                plan4::LogRow("fixture_role").field("model","commons").field("r",cambc::round_number()).field("id",id(me))
                    .field("producer",producer).field("active",active_).emit();}
            if(!active_)return;
        }
        if(!me.is_alive()||me.is_recalling()||me.is_submitting_gems()||me.is_teleporting())return;
        if(!initialize())return;enemy_count_=0;
        if(safety())return;
        if(!freeze_priority()||!observe())return;
        if(safety())return;
        if constexpr(Commons)if(holding_capture())return;
        if constexpr(!Commons){
            std::array<plan4::counterpart::Target,8> choices{};int count=0;if(!targets(int(cambc::round_number()),{enemies_.data(),std::size_t(enemy_count_)},choices,count))return;
            std::array<Point,9> requested{};int total=0;for(int n=0;n<count;++n)if(choices[n].bag)requested[total++]=choices[n].position;
            if(!total&&camp_count_)requested[total++]=camps_[cursor_%camp_count_].position;
            const auto own=point(me.position());if(!distances(own,{requested.data(),std::size_t(total)}))return;
            const auto decision=plan4::counterpart::fixture_decision(own,choices.data(),count,camps_.data(),camp_count_,cursor_,{Distances::lookup,nullptr,&distances_});
            if(fuel())plan4::LogRow("fixture_choice").field("model","counterpart").field("r",cambc::round_number()).field("id",id(me))
                .field("branch",int(decision.branch)).field("target",decision.target_id).field("tx",decision.target.x).field("ty",decision.target.y)
                .field("x",own.x).field("y",own.y).field("visible",count).field("cursor",cursor_).emit();
            if(decision.branch==plan4::counterpart::Branch::unsupported)return;
            if(decision.branch==plan4::counterpart::Branch::chase){
                for(const auto& enemy:cambc::vision::champions(cambc::team::enemy))if(id(enemy)==decision.target_id){
                    const float reserve=(enemy.strength()*1.5f+enemy.focus()*.2f)*3;
                    if(me.health()-reserve<me.max_health()*.25f){retreat_=true;safety();return;}
                    const auto p=point(enemy.position());if(me.can_attack()&&range(own,p)<=me.attack_range())me.attack(enemy);
                    else{std::vector<Point> goals;const int r=me.attack_range();for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx){Point q{p.x+dx,p.y+dy};if(q!=p&&walkable(q))goals.push_back(q);}move(goals);}return;}
                return;
            }
            // Reactive chase has priority over a freely cancellable capture
            // hold. With no chased enemy, ordinary capture precedes banking.
            if(holding_capture())return;
            if(bank())return;
            if(target_<0&&decision.branch==plan4::counterpart::Branch::camp)for(int g=0;g<int(groups_.size());++g)if(info_->groups[g].id==decision.target_id){target_=g;break;}
        }else{
            if(bank())return;
            if(target_<0){const auto choice=plan4::commons::fixture_next(priority_,cursor_,stock_.data(),int(groups_.size()),int(cambc::round_number()));
                if(fuel())plan4::LogRow("fixture_choice").field("model","commons").field("r",cambc::round_number()).field("id",id(me))
                    .field("target",choice.group<0?-1:info_->groups[choice.group].id).field("cursor",cursor_).field("next",choice.next_cursor).emit();
                if(choice.group<0)return;target_=choice.group;cursor_=choice.next_cursor;}
        }
        farm();if(!me.gem_count())bank_=-1;
    }
    void after_turn(){
        if(cambc::fuel_remaining()<20000)return;auto& me=cambc::self();if(!me.is_alive())return;
        const auto p=point(me.position());plan4::LogRow("fixture_state").field("model",Commons?"commons":"counterpart")
            .field("r",cambc::round_number()).field("id",id(me)).field("x",p.x).field("y",p.y).field("bag",me.gem_count())
            .field("active",active_)
            .field("unsupported",support_failed_)
            .field("priority",me.prev_priority()).field("target",target_<0?-1:info_->groups[target_].id)
            .field("visits",source_.vertex_visits()+movement_.vertex_visits()).field("fuel",cambc::fuel_remaining()).emit();
    }
};
template<bool Commons> int run(){
    std::unique_ptr<Bot<Commons>> bot;
    while(true){try{if(cambc::is_draft())draft();else{if(!bot)bot=std::make_unique<Bot<Commons>>();bot->turn();bot->after_turn();}}
        catch(const std::exception& error){std::printf("plan4 error: fixture %s\n",error.what());}
        catch(...){std::printf("plan4 error: fixture unknown\n");}
        cambc::end_turn();}
}
}
#endif
