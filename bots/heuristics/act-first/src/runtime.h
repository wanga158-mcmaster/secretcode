#pragma once
#include <cambc.h>
#include <cstdio>
#include <limits>
#include <optional>
#include "act.h"
#include "../../common/src/capture.h"
#include "../../common/src/draft.h"

namespace h12_act_first {
using cambc::vec2;
using cambc::bitboard;
using cambc::team;
int distance(vec2 a,vec2 b){return int(a.chebyshev_distance(b));}
bitboard around(vec2 p,int r){bitboard b;b.draw_chebyshev(p,r);return b;}

struct Source {
    cambc::map::camp_group group;
    vec2 anchor;
    // Public immutable metadata and timestamped observations are separate.
    int gems=0,gold=0,members=0;
    bool monolith=false;
    std::vector<unsigned char> dead;
    int dead_epoch=-1,observed=-1,unavailable=0;
    float hp=0,incoming=0;
    bool known=false,reward=false;
    int access_epoch=-1;
    bool access=true;
};
class Bot {
    Policy policy_;
    ActFirst controller_;
    std::vector<Source> sources_;
    std::vector<vec2> altars_;
    bitboard terrain_;
    bitboard component_;
    bool component_ready_=false,force_greedy_=false;
    int component_epoch_=0;
    std::uint64_t component_max_=0;
    bool initialized_=false,recovering_=false,was_alive_=false,minimal_=false;
    int identity_=0,life_=0,finance_=0,previous_bag_=0,previous_gold_=0;
    unsigned monster_=0,jungle_=0;
    heuristics::State fresh_;
    vec2 origin_{};
    float nearby_incoming_=0;
    std::vector<cambc::camp> threats_;
    std::vector<cambc::champion> enemies_;
    heuristics::CaptureWatch capture_;
    int route_source_=-1,route_best_=100000,route_progress_=0,route_failures_=0;
    vec2 failed_step_{-1,-1};int failed_until_=0;
    int blocked_source_=-1,blocked_until_=0;
    unsigned actions_=0,logs_=0,event_logs_=0,work_=0;
    int action_kind_=0,last_adopted_=0,last_published_=0;
    std::uint64_t prefix_cost_=0;
#if HEURISTICS_TRACE
    int move_reason_=0;
    vec2 move_goal_{};
#endif

    void initialize() {
        // SDK returns immutable handles and public source records once. No
        // per-source SDK design query or topology setup delays the first job.
        const auto groups=cambc::map::camp_groups();
        terrain_=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(team::enemy);
        altars_=cambc::map::altars().positions();
        identity_=int(cambc::self().champion_name());
        for(const auto&g:groups) {
            Source s{g,g.camps().front().spawn_position()};
            s.gems=int(g.gem_value());s.gold=int(g.gold_value());s.members=int(g.camps().size());
            s.monolith=std::any_of(g.begin(),g.end(),[](const auto&c){return c.is_monolith();});
            s.dead.assign(s.members,0);
            sources_.push_back(std::move(s));
        }
        initialized_=true;
    }
    void shop() {
        auto&me=cambc::self();
        for(int stage=0;stage<3&&cambc::fuel_remaining()>16000;++stage) {
            const auto plan=Policy::purchase(identity_,monster_,jungle_);
            using P=heuristics::Purchase;using I=cambc::item_type;
            auto item=I::power_shard_of_strength;unsigned affiliation=2;
            switch(plan) {
                case P::monster_strength:item=I::monster_shard_of_strength;affiliation=0;break;
                case P::monster_focus:item=I::monster_shard_of_focus;affiliation=0;break;
                case P::monster_health:item=I::monster_shard_of_health;affiliation=0;break;
                case P::jungle_health:item=I::jungle_shard_of_health;affiliation=1;break;
                case P::jungle_focus:item=I::jungle_shard_of_focus;affiliation=1;break;
                case P::jungle_strength:item=I::jungle_shard_of_strength;affiliation=1;break;
                case P::power_focus:item=I::power_shard_of_focus;break;
                case P::power_health:item=I::power_shard_of_health;break;
                case P::power_strength:break;
            }
            const auto price=item.cost();policy_.next_price=int(price);
            const auto limit=affiliation==0?5-monster_:affiliation==1?5-jungle_:100u;
            const auto count=std::min(limit,me.gold_count()/price);
            if(!count||!me.can_buy(item,count))return;
            me.buy(item,count);++finance_;
            if(affiliation==0)monster_+=count;if(affiliation==1)jungle_+=count;
        }
    }
    bool channel()const {
        const auto st=cambc::self().state();
        return st.contains(cambc::champion_state::recalling)||st.contains(cambc::champion_state::submitting_gems)
            ||st.contains(cambc::champion_state::teleporting)||st.contains(cambc::champion_state::completed_turn);
    }
    bool spawn()const{return cambc::map::spawn_region(team::ally).is_set(origin_);}
    bool on_source(int id,vec2 p)const {
        for(const auto&c:sources_[id].group.camps())if(c.spawn_position()==p)return true;
        return false;
    }
    int nearest_altar()const {
        int best=10000;for(const auto p:altars_)best=std::min(best,std::max(0,distance(origin_,p)-1));return best;
    }
    void snapshot() {
        auto&me=cambc::self();
        fresh_.round=int(cambc::round_number());fresh_.limit=int(cambc::constants::max_rounds);
        fresh_.identity=identity_;fresh_.bag=int(me.gem_count());fresh_.gold=int(me.gold_count());
        fresh_.hp=me.health();fresh_.maximum=me.max_health();fresh_.capture=cambc::constants::capture_time;
        fresh_.recall=cambc::constants::recall_time;fresh_.tribute=cambc::constants::submit_gem_duration_multiplier;
        fresh_.monster=int(monster_);fresh_.jungle=int(jungle_);fresh_.mature=monster_>=5&&jungle_>=5;
        origin_=me.position();fresh_.altar=nearest_altar();
        if(finance_changed(fresh_.bag,fresh_.gold,previous_bag_,previous_gold_)) {
            ++finance_;
            if(fresh_.bag>previous_bag_||fresh_.gold>previous_gold_||(fresh_.bag==0&&previous_bag_>0))controller_.no_work_age=0;
        }
        previous_bag_=fresh_.bag;previous_gold_=fresh_.gold;
    }
    // Only fresh immediate threats are detailed. No all-source HP/strength
    // aggregate, ally enumeration, feature shortlist or ranker precedes work.
    void safety() {
        nearby_incoming_=0;threats_.clear();enemies_=cambc::vision::champions(team::enemy);
        const auto awake=cambc::vision::awake_camp_tiles();
        if(!(awake&around(origin_,4)).is_empty()) {
            for(const auto&c:cambc::vision::camps()) {
                const auto p=c.spawn_position();
                if(distance(origin_,p)>4||!c.is_alive()||!c.is_awake())continue;
                const auto strength=c.strength();if(strength<=0)continue;
                threats_.push_back(c);
                if(distance(origin_,p)<=c.attack_range()+1)nearby_incoming_+=strength;
            }
        }
        for(const auto&e:enemies_)if(distance(origin_,e.position())<=e.attack_range()+1)nearby_incoming_+=e.strength();
    }
    bitboard obstacles()const {
        auto b=terrain_|cambc::vision::camp_tiles()|cambc::vision::champion_tiles()|cambc::vision::teleport_tiles();
        b.set(cambc::self().position(),false);return b;
    }
    bool walking_component() {
        if(component_ready_&&component_.is_set(origin_))return true;
        // A complete immutable-terrain flood is a routing admission certificate,
        // not a dynamic candidate prefix. Recall/respawn can change components.
        // Admission precedes selected-source perception. Reserve one field,
        // bounded physical continuation and the separate minimal yield tail.
        const auto before=fuel();const auto round=current_round();
        if(before<80000){minimal_=true;return false;}
        cambc::distance_field field;bitboard reached;
        field.compute(terrain_,around(origin_,0),reached);
        const auto after=fuel();
        if(current_round()!=round||after>before){minimal_=true;component_ready_=false;return false;}
        component_max_=std::max(component_max_,before-after);
        component_=std::move(reached);component_ready_=true;++component_epoch_;
        force_greedy_=true;
        return true;
    }
    bool capture_access(int id) {
        auto&s=sources_[id];
        if(s.access_epoch==component_epoch_)return s.access;
        s.access=false;
        for(const auto&c:s.group.camps())if(component_.is_set(c.spawn_position())){s.access=true;break;}
        s.access_epoch=component_epoch_;
        return s.access;
    }
    // One full native field is admitted with its whole tail, or at most eight
    // fresh neighbors. A short failed-neighbor memory cannot certify a detour.
    bool move(vec2 goal,int radius=0,bool force_field=false) {
#if HEURISTICS_TRACE
        move_goal_=goal;move_reason_=0;
#endif
        auto&me=cambc::self();if(!me.can_move())return false;
        const auto round=cambc::round_number();const auto pos=me.position();
        if(distance(pos,goal)<=radius)return false;
        if(cambc::fuel_remaining()<12000){minimal_=true;return false;}
        auto walls=obstacles();
        if((cambc::fuel_remaining()<60000||force_greedy_)&&!force_field) {
            minimal_=true;
            auto best=pos;int best_d=distance(pos,goal);
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                if((dx==0&&dy==0)||cambc::fuel_remaining()<6500)continue;
                const vec2 p{pos.x+dx,pos.y+dy};
                if(!cambc::map::is_in_bounds(p)||walls.is_set(p)||(p==failed_step_&&int(round)<failed_until_))continue;
                const int d=distance(p,goal);if(d<best_d){best=p;best_d=d;}
            }
            if(best==pos)return false;
            if(cambc::round_number()!=round||!me.is_alive()||channel())return false;
            const bool ok=me.move_absolute(best);
            if(!ok){failed_step_=best;failed_until_=int(round)+3;}
            return ok;
        }
        if(cambc::fuel_remaining()<60000){minimal_=true;return false;}
        auto goals=around(goal,radius)&~walls;
        if(goals.is_empty())return false;
        cambc::distance_field field;bitboard visited;
        field.compute_to(walls,goals,around(pos,0),visited,256);
        if(cambc::fuel_remaining()<18000){minimal_=true;return false;}
        const auto current=field.get(pos);
        if(current==cambc::distance_field::unreachable) {
#if HEURISTICS_TRACE
            move_reason_=1;
#endif
            return false;
        }
        auto best=pos;auto best_d=current;
        for(const auto p:walls.reachable_tiles(pos,1).positions()) {
            if(p==failed_step_&&int(round)<failed_until_)continue;
            const auto d=field.get(p);if(d<best_d){best=p;best_d=d;}
        }
        if(best==pos)return false;
        if(cambc::round_number()!=round||!me.is_alive()||channel()||!me.can_move())return false;
        const bool ok=me.move_absolute(best);
        if(!ok){failed_step_=best;failed_until_=int(round)+3;}
        return ok;
    }
    template<class Target> bool fight(const Target&target,bool dangerous) {
        auto&me=cambc::self();
        if(cambc::fuel_remaining()<22000||!target.is_visible()||target.health()<=0)return false;
        bool acted=false;
        // Fresh SDK masks decide ability and basic legality, never an inferred
        // ranged allowance after movement.
        for(const auto&a:me.abilities())if((a.is_damage()||a.is_cc())&&a.is_ready()&&a.valid_tiles().is_set(target.position())) {
            acted=a.use(target);break;
        }
        if(!target.is_visible()||target.health()<=0)return acted;
        if(!me.attack_tiles().is_set(target.position()))return acted;
        if(dangerous)for(const auto&a:me.abilities())if(a.is_shield()&&a.is_ready()){acted|=a.use();break;}
        if(me.can_attack()){me.attack(target);acted=true;}
        if(cambc::fuel_remaining()>18000&&target.is_visible()&&target.health()>0&&!me.can_attack())
            for(const auto&a:me.abilities())if(a.is_attack_reset()&&a.is_ready()) {
                if(a.use()&&me.can_attack()&&target.is_visible()&&target.health()>0)me.attack(target);
                acted=true;break;
            }
        return acted;
    }
    bool recover() {
        auto&me=cambc::self();
        if(threats_.empty()&&enemies_.empty()){me.recall();return true;}
        auto walls=obstacles();const auto pos=me.position();auto best=pos;double value=-1e30;
        for(const auto p:walls.reachable_tiles(pos,1).positions()) {
            double score=0;
            for(const auto&c:threats_)score+=std::min(distance(p,c.spawn_position()),6)*c.strength();
            for(const auto&e:enemies_)score+=std::min(distance(p,e.position()),9)*150;
            if(score>value){best=p;value=score;}
        }
        if(best!=pos&&me.can_move()&&me.move_absolute(best))return true;
        if(nearby_incoming_==0){me.recall();return true;}
        return false;
    }
    bool bank() {
        if(!policy_.bank(fresh_))return false;
        auto&me=cambc::self();vec2 nearest{};int best=10000;
        for(const auto p:altars_)if(distance(me.position(),p)<best){best=distance(me.position(),p);nearest=p;}
        if(best<=1) {
            for(const auto&lock:cambc::vision::altar_locks(team::enemy))if(lock.position()==nearest) {
                if(me.can_attack()){me.attack(lock);return true;}return false;
            }
            me.submit_gems(nearest);action_kind_=5;return true;
        }
        action_kind_=4;return best<10000&&move(nearest,1);
    }
    struct View {bool seen=false,reward=false,respawn=false;std::optional<cambc::camp> target;vec2 goal{};float incoming=0;};
    View observe(int id) {
        auto&s=sources_[id];View v;v.goal=s.anchor;
        const auto round=int(cambc::round_number());
        if(s.dead_epoch>=0&&round-s.dead_epoch>=s.group.max_respawn_timer()){std::fill(s.dead.begin(),s.dead.end(),0);s.dead_epoch=-1;}
        int index=0,best_goal=100000;float best_target=std::numeric_limits<float>::max(),hp=0,incoming=0;
        bool all=true;
        // Selected-source members only. All visible empties are retained even
        // when another member is currently alive; movement does not reset them.
        for(const auto&c:s.group.camps()) {
            const auto p=c.spawn_position();const bool shown=cambc::vision::in_vision(p);all&=shown;
            if(shown) {
                v.seen=true;
                const bool alive=c.is_visible()&&c.is_alive();
                if(alive) {
                    if(s.dead[index])std::fill(s.dead.begin(),s.dead.end(),0);
                    s.dead[index]=0;
                    const auto health=c.health(),strength=c.strength();hp+=health;incoming+=strength;
                    const auto score=health+distance(origin_,p)*100.f;
                    if(score<best_target){best_target=score;v.target=c;}
                } else {s.dead[index]=1;s.dead_epoch=round;}
            }
            if(!s.dead[index]) {
                const int d=distance(origin_,p);
                if(d<best_goal){best_goal=d;v.goal=p;}
            }
            ++index;
        }
        if(v.seen) {
            const auto status=s.group.state();
            if(const auto*r=std::get_if<cambc::map::camp_group_respawning>(&status)) {
                s.unavailable=round+std::max(1,r->timer);v.respawn=true;s.reward=false;
                std::fill(s.dead.begin(),s.dead.end(),0);s.dead_epoch=-1;
            }
            v.reward=std::holds_alternative<cambc::map::camp_group_reward_available>(status);
            s.reward=v.reward;s.observed=round;
        }
        s.known=all;
        if(all){s.hp=hp;s.incoming=incoming;}
        v.incoming=incoming;
        if(v.reward) {
            int best=100000;
            for(const auto&c:s.group.camps())if(distance(origin_,c.spawn_position())<best){best=distance(origin_,c.spawn_position());v.goal=c.spawn_position();}
        }
        return v;
    }
    bool expire_hold(int id) {
        if(!capture_.expired(id,fresh_.round,cambc::constants::capture_time))return false;
        sources_[id].unavailable=fresh_.round+12;controller_.invalidate();controller_.incumbent=-1;
        const auto pos=cambc::self().position();auto walls=obstacles();
        for(const auto p:walls.reachable_tiles(pos,1).positions())if(p!=pos&&!on_source(id,p)&&cambc::self().can_move()) {
            if(cambc::self().move_absolute(p)){capture_.reset();action_kind_=6;return true;}
        }
        // Useful exit fallback; elapsed hold survives failed physical departure.
        if(fresh_.bag>0&&bank())return true;
        if(threats_.empty()&&enemies_.empty()){cambc::self().recall();action_kind_=6;return true;}
        return true;
    }
public:
    Stamp stamp()const{return {fresh_.round,life_,finance_};}
    int current_round()const{return int(cambc::round_number());}
    std::uint64_t fuel()const{return cambc::fuel_remaining();}
    int count()const{return int(sources_.size());}
    heuristics::State cached_state()const{return fresh_;}
    bool cached_available(int id)const{return available(id);}
    bool available(int id)const {
        return id>=0&&id<int(sources_.size())&&!sources_[id].monolith&&sources_[id].unavailable<=fresh_.round
            &&!(sources_[id].access_epoch==component_epoch_&&!sources_[id].access)
            &&!(id==blocked_source_&&fresh_.round<blocked_until_);
    }
    heuristics::Job cached_job(int id)const {
        const auto&s=sources_[id];heuristics::Job j;
        j.id=id;j.gems=s.gems;j.gold=s.gold;j.travel=distance(origin_,s.anchor);
        j.altar=fresh_.altar;j.committed=id==controller_.incumbent;
        j.known=s.known&&s.observed==fresh_.round;j.age=s.observed<0?fresh_.round+1:fresh_.round-s.observed;
        j.ready=s.reward&&s.observed==fresh_.round;
        // Public member count is a workload proxy, not copied monster HP.
        j.work=j.ready?0:s.known?s.hp/std::max(1.f,fresh_.maximum*.08f):float(s.members*4);
        j.incoming=s.incoming;j.loss=j.incoming*(j.work+2);
        j.affordable=j.ready||(!j.known?fresh_.hp>=fresh_.maximum*.75f:fresh_.hp-j.loss>=fresh_.maximum*.3f);
        return j;
    }
    int bootstrap()const {
        int best=-1;double merit=-1e30;
        // Only bounded public cached candidates; no full-source ranking prefix.
        const int n=count();if(!n)return -1;
        const int start=(identity_*7+fresh_.round/13)%n;
        for(int k=0;k<std::min(n,8);++k) {
            const int id=(start+k)%n;if(!available(id))continue;
            const auto j=cached_job(id);const double m=policy_.merit(fresh_,j);
            if(m>merit){merit=m;best=id;}
        }
        return best;
    }
    bool veto(int id) {
        if(!available(id)||recovering_||channel()||policy_.bank(fresh_)||nearby_incoming_>fresh_.hp*.15f
            ||capture_.source()>=0||controller_.no_work_age<4)return false;
        // Keep a physically improving approach. A fresh challenger may replace
        // a stagnant route, but cannot repeatedly restart a long useful walk.
        if(route_source_==controller_.incumbent&&fresh_.round-route_progress_<4)return false;
        if(!capture_access(id))return false;
        // One fresh proposed-source lifecycle check is allowed before adopting.
        auto&s=sources_[id];bool shown=false;
        for(const auto&c:s.group.camps())if(cambc::vision::in_vision(c.spawn_position())){shown=true;break;}
        if(shown&&std::holds_alternative<cambc::map::camp_group_respawning>(s.group.state()))return false;
        return fresh_.hp>fresh_.maximum*.55f;
    }
    Receipt prefix(int id) {
        const auto before=fuel();Receipt result{false,false,false,id,stamp()};
        if(id<0)return result;
        auto&me=cambc::self();
        if(cambc::fuel_remaining()<16000){minimal_=true;return result;}
        if(!capture_access(id)){controller_.incumbent=-1;controller_.invalidate();return result;}
        auto view=observe(id);
        if(view.respawn) {controller_.incumbent=-1;controller_.invalidate();return result;}
        if(capture_.source()>=0&&!on_source(capture_.source(),origin_))capture_.reset();
        if(view.reward&&on_source(id,origin_)) {
            if(expire_hold(id)){result.actual=action_kind_!=0;return result;}
            // Capture holding is useful but deliberately does not count as a
            // successful SDK action authorizing an audit.
            action_kind_=3;return result;
        }
        if(capture_.source()==id&&!view.reward)capture_.reset();
        if(policy_.retreat(fresh_.hp,fresh_.maximum,std::max(nearby_incoming_,view.incoming),fresh_.bag)&&!spawn()) {
            recovering_=true;result.actual=recover();return result;
        }
        if(route_source_!=id){route_source_=id;route_best_=distance(origin_,view.goal);route_progress_=fresh_.round;}
        else if(distance(origin_,view.goal)<route_best_){route_best_=distance(origin_,view.goal);route_progress_=fresh_.round;}
        if(view.target&&fight(*view.target,view.incoming>0)) {
            result.actual=true;result.work=true;action_kind_=2;++work_;route_failures_=0;
        } else {
            action_kind_=1;
            result.actual=move(view.target?view.target->spawn_position():view.goal,view.reward?0:1);
            if(result.actual)route_failures_=0;else ++route_failures_;
        }
        // Age survives source changes and ordinary movement. Only work/payout/
        // new life resets it. Stagnant jobs receive a bounded reconsideration.
        if(!result.work&&reconsider(controller_.no_work_age,fresh_.round-route_progress_,route_failures_)) {
            blocked_source_=id;blocked_until_=fresh_.round+18;controller_.incumbent=-1;
            controller_.invalidate();route_failures_=0;
        }
        result.admissible=result.actual&&!minimal_&&controller_.incumbent==id&&!view.reward;
        prefix_cost_=before-fuel();++actions_;
        return result;
    }
    void turn() {
        minimal_=false;force_greedy_=false;action_kind_=0;
        auto&me=cambc::self();
        if(!initialized_)initialize();
        const bool alive=me.is_alive();
        if(!alive) {
            if(was_alive_){++life_;controller_.no_work_age=0;controller_.incumbent=-1;controller_.invalidate();capture_.reset();}
            was_alive_=false;recovering_=false;shop();return;
        }
        if(!was_alive_){was_alive_=true;++life_;controller_.no_work_age=0;controller_.invalidate();}
        if(channel()) {controller_.invalidate();return;}
        const auto allowances=me.state();
        if(!allowances.contains(cambc::champion_state::can_move)&&!allowances.contains(cambc::champion_state::can_attack)
            &&!allowances.contains(cambc::champion_state::can_cast)){controller_.invalidate();return;}
        shop();snapshot();
        if(spawn()) {recovering_=false;if(fresh_.hp<fresh_.maximum)return;}
        safety();
        if(policy_.retreat(fresh_.hp,fresh_.maximum,nearby_incoming_,fresh_.bag)&&!spawn())recovering_=true;
        if(recovering_) {controller_.invalidate();recover();return;}
        // A real ongoing capture precedes urgent banking, and its elapsed hold
        // is independent of incidental target cancellation and empty vision.
        // An ally may have delivered the final hit since our previous owner
        // turn. Establish that fresh incumbent hold before proposal adoption.
        int held=capture_.source();
        if(held<0&&controller_.incumbent>=0&&on_source(controller_.incumbent,origin_))held=controller_.incumbent;
        if(held>=0&&on_source(held,origin_)) {
            const auto status=sources_[held].group.state();
            if(std::holds_alternative<cambc::map::camp_group_reward_available>(status)) {
                if(expire_hold(held))return;
                controller_.invalidate();return;
            }
            capture_.reset();
        }
        if(bank()){controller_.invalidate();return;}
        for(const auto&e:enemies_)if(distance(origin_,e.position())<=1&&fight(e,true)){controller_.invalidate();return;}
        if(!spawn()&&identity_!=int(cambc::champion_type::tavi)&&fresh_.gold>=policy_.next_price*6) {
            recovering_=true;controller_.invalidate();recover();return;
        }
        if(!walking_component())return;
        const auto receipt=controller_.tick(*this,policy_);
        if(minimal_)return; // No receipt queries, audit or formatted low tail.
#if HEURISTICS_TRACE
        if(!receipt.actual&&fresh_.round>=860&&fresh_.round<=865&&fuel()>40000) {
            std::printf("actidle actor=%d round=%d source=%d x=%d y=%d gx=%d gy=%d reason=%d fails=%d recover=%d age=%d left=%llu\n",
                identity_,fresh_.round,receipt.source,origin_.x,origin_.y,move_goal_.x,move_goal_.y,move_reason_,route_failures_,
                int(recovering_),controller_.no_work_age,static_cast<unsigned long long>(fuel()));
        }
#endif
        // Sparse receipts are outside the scalar audit and have their own
        // admission. Fuel before formatting plus native whole-turn fuel bounds
        // their entire end_turn tail, including output flush.
        const bool changed=last_adopted_!=int(controller_.adopted)||last_published_!=int(controller_.published);
        const bool event=changed&&event_logs_<16;
        if(fuel()>40000&&((receipt.actual&&(logs_<12||fresh_.round%128==identity_%128))||event)) {
            ++logs_;
            if(event)++event_logs_;
            std::printf("act actor=%d round=%d source=%d kind=%d actual=%d work=%u audits=%u publish=%u adopt=%u veto=%u proposal=%d owner=%d prefix=%llu auditmax=%llu staticmax=%llu left=%llu\n",
                identity_,fresh_.round,receipt.source,action_kind_,int(receipt.actual),work_,controller_.audits,controller_.published,controller_.adopted,
                controller_.vetoed,controller_.proposal().source,controller_.proposal().owner,static_cast<unsigned long long>(prefix_cost_),
                static_cast<unsigned long long>(controller_.maximum_audit),static_cast<unsigned long long>(component_max_),static_cast<unsigned long long>(fuel()));
        }
        last_adopted_=int(controller_.adopted);last_published_=int(controller_.published);
    }
};
inline void draft() {
    const auto selected=cambc::draft::current_draft(team::ally);
    for(const int n:Policy::roster) {
        const auto choice=heuristics_draft::choices[n];
        if(std::find(selected.begin(),selected.end(),choice)==selected.end()){cambc::draft::pick(choice);return;}
    }
}
inline int run() {
    Bot bot;
    while(true) {
        try {if(cambc::is_draft())draft();else bot.turn();}
        catch(const std::exception&e){std::printf("act-first error: %s\n",e.what());}
        catch(...){std::printf("act-first error: unknown exception\n");}
        cambc::end_turn();
    }
}
}
