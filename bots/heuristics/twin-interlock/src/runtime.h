#pragma once
#include <cambc.h>
#include <cstdio>
#include <limits>
#include <optional>
#include "policy.h"
#include "interlock.h"
#include "../../common/src/capture.h"
#include "../../common/src/draft.h"

namespace h12_twin_interlock {
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
    bool monolith=false,harmless=true;
    std::vector<vec2> pads;
    std::vector<unsigned char> dead;
    int dead_epoch=-1,observed=-1,unavailable=0;
    float hp=0,incoming=0,design_hp=0,design_incoming=0;
    bool known=false,reward=false;
    int access_epoch=-1;
    bool access=true;
};
class Bot {
    Policy policy_;
    int incumbent_=-1,no_work_age_=0;
    bool twin_=false,walked_=false,packet_ok_=false,peer_accepted_=false;
    int mode_code_=4,last_mode_=-1,mode_logs_=0;
    std::optional<cambc::champion> partner_;
    Link link_;
    Report peer_{};
    Mode mode_=Mode::Unknown;
    std::uint64_t packet_max_=0;

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
            for(const auto&c:g.camps()) {
                s.pads.push_back(c.spawn_position());const auto stats=c.base_stats();
                s.harmless&=stats.base_strength<=0;s.design_hp+=stats.base_health;s.design_incoming+=stats.base_strength;
            }

            sources_.push_back(std::move(s));
        }
        twin_=identity_==3||identity_==4;
        if(twin_)for(const auto&c:cambc::champions(team::ally))
            if(int(c.champion_name())==(identity_==3?4:3)){partner_=c;break;}
        packet_ok_=twin_&&cambc::shared_memory.size()>=8&&cambc::map::width()<=100&&cambc::map::height()<=100
            &&cambc::constants::max_rounds<=2047;
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
        for(const auto q:sources_[id].pads)if(q==p)return true;
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
        if(fresh_.bag!=previous_bag_||fresh_.gold!=previous_gold_) {
            ++finance_;
            if(fresh_.bag>previous_bag_||fresh_.gold>previous_gold_||(fresh_.bag==0&&previous_bag_>0))no_work_age_=0;
        }
        previous_bag_=fresh_.bag;previous_gold_=fresh_.gold;
    }
    // Only fresh immediate threats are detailed. No all-source HP/strength
    // aggregate, ally enumeration, feature shortlist or ranker precedes work.
    void safety() {
        nearby_incoming_=0;threats_.clear();enemies_=cambc::vision::champions(team::enemy);
        const auto awake=cambc::vision::awake_camp_tiles();
        if(!(awake&around(origin_,6)).is_empty()) {
            for(const auto&c:cambc::vision::camps()) {
                const auto p=c.spawn_position();
                if(distance(origin_,p)>6||!c.is_alive()||!c.is_awake())continue;
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
    bool landing_allowed(vec2 p)const {
        if(!twin_||mode_==Mode::Work)return true;
        // The dash mask is geometry only. Conservative protected movement
        // additionally refuses visible awake-camp and enemy exposure.
        bool exposed=false;
        for(const auto&e:enemies_)if(distance(p,e.position())<=e.attack_range()+1){exposed=true;break;}
        for(const auto&c:threats_)if(distance(p,c.spawn_position())<=c.attack_range()){exposed=true;break;}
        return safe_landing(mode_,exposed);
    }
    bool walking_component() {
        if(!component_needs_refresh(component_ready_,component_ready_&&component_.is_set(origin_)))return true;
        // A complete immutable-terrain flood is a routing admission certificate,
        // not a dynamic candidate prefix. Recall/respawn can change components.
        // Admission precedes selected-source perception. Reserve one field,
        // bounded physical continuation and the separate minimal yield tail.
        const auto before=fuel();const auto round=current_round();
        if(!component_admitted(before)){minimal_=true;return false;}
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
        for(const auto p:s.pads)if(component_.is_set(p)){s.access=true;break;}
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
                if(!cambc::map::is_in_bounds(p)||walls.is_set(p)||(p==failed_step_&&int(round)<failed_until_)||!landing_allowed(p))continue;
                const int d=distance(p,goal);if(d<best_d){best=p;best_d=d;}
            }
            if(best==pos)return false;
            if(cambc::round_number()!=round||!me.is_alive()||channel())return false;
            const bool ok=me.move_absolute(best);walked_|=ok;
            if(!ok){failed_step_=best;failed_until_=int(round)+3;}
            return ok;
        }
        if(cambc::fuel_remaining()<60000){minimal_=true;return false;}
        auto goals=around(goal,radius)&~walls;
        if(goals.is_empty())return false;
        cambc::distance_field field;bitboard visited;
        field.compute_to(walls,goals,around(pos,0),visited);
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
            if((p==failed_step_&&int(round)<failed_until_)||!landing_allowed(p))continue;
            const auto d=field.get(p);if(d<best_d){best=p;best_d=d;}
        }
        if(best==pos)return false;
        if(cambc::round_number()!=round||!me.is_alive()||channel()||!me.can_move())return false;
        const bool ok=me.move_absolute(best);walked_|=ok;
        if(!ok){failed_step_=best;failed_until_=int(round)+3;}
        return ok;
    }
    template<class Target> bool fight(const Target&target,bool dangerous) {
        auto&me=cambc::self();
        if(cambc::fuel_remaining()<25000||!target.is_visible()||target.health()<=0)return false;
        bool acted=false;
        // Cast one actual opportunity. Payload, target categories and fresh
        // readiness are separate from geometric range. No reset range query.
        for(const auto&a:me.abilities()) {
            if(!a.info()||!a.is_ready()||!(a.is_damage()||a.is_cc()))continue;
            const bool exclusive=std::visit([](const auto&p){return p.exclusive_with_movement;},*a.info());
            if(!cast_compatible(true,exclusive,walked_))continue;
            if constexpr(std::is_same_v<Target,cambc::camp>){if(!a.can_target_camps())continue;}
            else {if(!a.can_target_enemies())continue;}
            if(!a.valid_tiles().is_set(target.position()))continue;
            if(a.use(target)){acted=true;break;}
        }
        if(!target.is_visible()||target.health()<=0)return acted;
        if(!me.attack_tiles().is_set(target.position()))return acted;
        if(dangerous)for(const auto&a:me.abilities())if(a.is_shield()&&a.is_ready()){acted|=a.use();break;}
        if(me.can_attack()){me.attack(target);acted=true;}
        if(cambc::fuel_remaining()>20000&&target.is_visible()&&target.health()>0&&!me.can_attack())
            for(const auto&a:me.abilities())if(a.is_attack_reset()&&a.can_target_self()&&a.is_ready()) {
                if(a.use()){acted=true;if(me.can_attack()&&target.is_visible()&&target.health()>0)me.attack(target);}
                break;
            }
        return acted;
    }
    bool dash(vec2 goal) {
        if(identity_!=4||cambc::fuel_remaining()<22000||distance(cambc::self().position(),goal)<=1)return false;
        auto&me=cambc::self();const auto pos=me.position();const auto walls=obstacles();
        for(const auto&a:me.abilities())if(a.is_dash()&&a.is_ready()&&a.can_target_ground()) {
            const auto payload=a.get<cambc::ability_dash>();
            if(payload.modifier!=0||payload.duration!=0)return false;
            const int dx=(goal.x>pos.x)-(goal.x<pos.x),dy=(goal.y>pos.y)-(goal.y<pos.y);
            vec2 landing=pos;
            for(int step=1;step<=int(payload.range);++step){const vec2 p{pos.x+step*dx,pos.y+step*dy};
                if(!cambc::map::is_in_bounds(p)||walls.is_set(p)||!cambc::vision::in_vision(p))break;landing=p;}
            if(landing==pos||!a.valid_tiles().is_set(landing)||!landing_allowed(landing))return false;
            return a.use(landing);
        }
        return false;
    }
    bool recover() {
        auto&me=cambc::self();
        if(nearby_incoming_==0){me.recall();return true;}
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
        for(const auto p:altars_)if(!(around(p,1)&component_).is_empty()&&distance(me.position(),p)<best){best=distance(me.position(),p);nearest=p;}
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
        sources_[id].unavailable=fresh_.round+12;incumbent_=-1;
        const auto pos=cambc::self().position();auto walls=obstacles();
        for(const auto p:walls.reachable_tiles(pos,1).positions())if(p!=pos&&!on_source(id,p)&&cambc::self().can_move()) {
            if(cambc::self().move_absolute(p)){capture_.reset();action_kind_=6;return true;}
        }
        // Useful exit fallback; elapsed hold survives failed physical departure.
        if(fresh_.bag>0&&bank())return true;
        if(threats_.empty()&&enemies_.empty()){cambc::self().recall();action_kind_=6;return true;}
        return true;
    }

    bool available(int id)const {
        return id>=0&&id<int(sources_.size())&&!sources_[id].monolith&&sources_[id].unavailable<=fresh_.round
            &&!(sources_[id].access_epoch==component_epoch_&&!sources_[id].access)
            &&!(id==blocked_source_&&fresh_.round<blocked_until_);
    }
    heuristics::Job job(int id)const {
        const auto&s=sources_[id];heuristics::Job j;
        j.id=id;j.gems=s.gems;j.gold=s.gold;j.travel=distance(origin_,s.anchor);j.altar=fresh_.altar;
        j.committed=id==incumbent_;j.known=s.known&&s.observed==fresh_.round;j.ready=s.reward&&s.observed==fresh_.round;
        j.work=j.ready?0:(s.known?s.hp:s.design_hp)/std::max(1.f,cambc::self().strength());
        j.incoming=s.harmless?0:s.known?s.incoming:s.design_incoming;j.loss=j.incoming*(j.work+2);
        j.affordable=j.ready||fresh_.hp-j.loss>=fresh_.maximum*.4f;
        return j;
    }
    bool allowed_source(int id)const {
        if(!twin_)return true;
        const auto&s=sources_[id];
        if(s.harmless||(s.reward&&s.observed==fresh_.round))return mode_!=Mode::SelfRetreat&&mode_!=Mode::PartnerRetreat;
        if(mode_==Mode::Unknown&&link_.unknown_age(fresh_.round)>=6)
            return fresh_.hp>=fresh_.maximum*.82f&&enemies_.empty();
        const auto j=job(id);
        return may_provoke(mode_,link_.unknown_age(fresh_.round),fresh_.hp,fresh_.maximum,
            j.known?j.loss:fresh_.maximum,!enemies_.empty(),false);
    }
    int select() {
        std::vector<heuristics::Job> jobs;
        for(int id=0;id<int(sources_.size());++id) {
            if(!available(id)||!capture_access(id)||!allowed_source(id))continue;
            jobs.push_back(job(id));
        }
        return heuristics::choose(policy_,fresh_,jobs);
    }
    void read_link(bool alive) {
        if(!twin_)return;
        const auto before=cambc::fuel_remaining();
        link_.observe_life(alive,int(cambc::round_number()));
        if(packet_ok_&&before>12000) {
            peer_=read_reports(cambc::shared_memory)[identity_==3?1:0];
        }
        peer_accepted_=packet_ok_&&partner_&&link_.accept(peer_,int(cambc::round_number()),
            cambc::map::width(),cambc::map::height(),int(cambc::constants::max_rounds),partner_->is_alive());
        packet_max_=std::max(packet_max_,before-cambc::fuel_remaining());
    }
    void publish() {
        // This late report is still a dated own observation. It grants no
        // hidden query permission and a positive tribute bag remains positive.
        if(!packet_ok_||!packet_admitted(cambc::fuel_remaining()))return;
        const auto before=cambc::fuel_remaining();auto&me=cambc::self();
        Report r;r.round=int(cambc::round_number());r.alive=me.is_alive();r.epoch=link_.epoch();r.valid=true;
        if(r.alive) {
            const auto p=me.position();r.x=p.x;r.y=p.y;r.cargo=me.gem_count()>0;
            origin_=p;safety();
            r.safe=!policy_.retreat(me.health(),me.max_health(),nearby_incoming_,int(me.gem_count()));
            const auto st=me.state();r.channel=st.contains(cambc::champion_state::submitting_gems)?2:
                st.contains(cambc::champion_state::recalling)?1:st.contains(cambc::champion_state::teleporting)?3:0;
        }
        write_report(cambc::shared_memory,identity_,r);
        packet_max_=std::max(packet_max_,before-cambc::fuel_remaining());
    }
    void work() {
        if(incumbent_<0||!available(incumbent_)||!allowed_source(incumbent_))incumbent_=select();
        if(incumbent_<0) {
            // At most four owner turns following a recent reported position;
            // then ordinary conservative production replaces rendezvous.
            if(twin_&&mode_==Mode::Unknown&&peer_.valid&&fresh_.round-peer_.round>=0&&fresh_.round-peer_.round<=4
                &&link_.unknown_age(fresh_.round)<4&&distance(origin_,{peer_.x,peer_.y})>3)move({peer_.x,peer_.y},3);
            else if(!spawn()&&no_work_age_>16){recovering_=true;recover();}
            return;
        }
        const int id=incumbent_;auto view=observe(id);
        if(view.respawn){incumbent_=-1;return;}
        if(view.reward&&on_source(id,origin_)){expire_hold(id);action_kind_=3;return;}
        if(capture_.source()==id&&!view.reward)capture_.reset();
        if(policy_.retreat(fresh_.hp,fresh_.maximum,std::max(nearby_incoming_,view.incoming),fresh_.bag)&&!spawn()) {
            sources_[id].unavailable=quarantine_after_refusal(fresh_.round);incumbent_=-1;
            recovering_=true;recover();return;
        }
        const auto loss=view.incoming*(sources_[id].hp/std::max(1.f,cambc::self().strength())+2);
        const bool damage_ok=!twin_||may_provoke(mode_,link_.unknown_age(fresh_.round),fresh_.hp,fresh_.maximum,
            mode_==Mode::Unknown&&!sources_[id].known?fresh_.maximum:loss,!enemies_.empty(),sources_[id].harmless&&view.incoming==0);
        if(route_source_!=id){route_source_=id;route_best_=distance(origin_,view.goal);route_progress_=fresh_.round;}
        else if(distance(origin_,view.goal)<route_best_){route_best_=distance(origin_,view.goal);route_progress_=fresh_.round;}
        bool acted=false;
        if(view.target&&damage_ok&&fight(*view.target,view.incoming>0)) {
            action_kind_=2;++work_;no_work_age_=0;route_failures_=0;acted=true;
        } else if(view.target&&!damage_ok){blocked_source_=id;blocked_until_=fresh_.round+3;incumbent_=-1;}
        else {
            action_kind_=1;const auto goal=view.target?view.target->spawn_position():view.goal;
            acted=dash(goal);if(!acted)acted=move(goal,view.reward?0:1);
            if(acted&&view.target&&damage_ok&&cambc::fuel_remaining()>30000&&fight(*view.target,view.incoming>0)) {
                ++work_;no_work_age_=0;action_kind_=2;
            }
            if(acted)route_failures_=0;else ++route_failures_;
        }
        if((no_work_age_>=60&&fresh_.round-route_progress_>=8)||route_failures_>=6) {
            blocked_source_=id;blocked_until_=fresh_.round+18;incumbent_=-1;route_failures_=0;
        }
    }
    void gameplay() {
        auto&me=cambc::self();const bool alive=me.is_alive();read_link(alive);
        if(!alive) {
            if(was_alive_){++life_;no_work_age_=0;incumbent_=-1;capture_.reset();}
            was_alive_=false;recovering_=false;shop();return;
        }
        if(!was_alive_){was_alive_=true;++life_;no_work_age_=0;}
        ++no_work_age_;snapshot();
        if(capture_.source()>=0&&!on_source(capture_.source(),origin_))capture_.reset();
        const auto allowance=me.state();
        if(!has_action(allowance.contains(cambc::champion_state::can_move),allowance.contains(cambc::champion_state::can_attack),
            allowance.contains(cambc::champion_state::can_cast)))return;
        shop();snapshot();
        if(spawn()){recovering_=false;if(fresh_.hp<fresh_.maximum)return;}
        safety();
        const bool unsafe=policy_.retreat(fresh_.hp,fresh_.maximum,nearby_incoming_,fresh_.bag)&&!spawn();
        mode_=twin_?mode_for_report(unsafe,peer_accepted_,peer_,fresh_.round):unsafe?Mode::SelfRetreat:Mode::Work;
        mode_code_=int(mode_);
        if(channel()) {
            if(cancel_for_exposure(unsafe,nearby_incoming_)&&me.is_submitting_gems())me.cancel_submit_gems();
            else if(cancel_for_exposure(unsafe,nearby_incoming_)&&me.is_recalling())me.cancel_recall();
            else return;
        }
        if(unsafe) {
            if(incumbent_>=0)sources_[incumbent_].unavailable=quarantine_after_refusal(fresh_.round);
            incumbent_=-1;recovering_=true;
        }
        if(recovering_||mode_==Mode::PartnerRetreat){recover();return;}
        if(mode_==Mode::Protect&&nearby_incoming_>0){recover();return;}
        int held=capture_.source();if(held<0&&incumbent_>=0&&on_source(incumbent_,origin_))held=incumbent_;
        if(held>=0&&on_source(held,origin_)) {
            const auto status=sources_[held].group.state();
            if(std::holds_alternative<cambc::map::camp_group_reward_available>(status)){expire_hold(held);return;}
            capture_.reset();
        }
        if(fresh_.bag>0&&!walking_component())return;
        if(bank())return;
        if(!twin_||mode_==Mode::Work)for(const auto&e:enemies_)
            if(distance(origin_,e.position())<=1&&fight(e,true))return;
        if(!spawn()&&identity_!=int(cambc::champion_type::tavi)&&fresh_.gold>=policy_.next_price*5) {
            recovering_=true;recover();return;
        }
        if(!walking_component())return;
        work();
    }
public:
    int current_round()const{return int(cambc::round_number());}
    std::uint64_t fuel()const{return cambc::fuel_remaining();}
    void turn() {
        minimal_=false;force_greedy_=false;walked_=false;action_kind_=0;mode_code_=-1;
        if(!initialized_)initialize();
        gameplay();
        if(!minimal_)publish();
        if(twin_&&mode_code_>=0&&fuel()>40000&&(last_mode_!=mode_code_||fresh_.round%128==identity_)&&mode_logs_<40) {
            ++mode_logs_;
            std::printf("twins actor=%d round=%d mode=%d accepted=%d peerround=%d cargo=%d peersafe=%d ownbag=%u kind=%d work=%u packetmax=%llu left=%llu\n",
                identity_,int(cambc::round_number()),mode_code_,int(peer_accepted_),peer_.round,int(peer_.cargo),int(peer_.safe),
                cambc::self().gem_count(),action_kind_,work_,static_cast<unsigned long long>(packet_max_),static_cast<unsigned long long>(fuel()));
        }
        last_mode_=mode_code_;
    }
};
inline void draft() {
    const auto selected=cambc::draft::current_draft(team::ally);
    for(const int n:Policy::roster){const auto choice=heuristics_draft::choices[n];
        if(std::find(selected.begin(),selected.end(),choice)==selected.end()){cambc::draft::pick(choice);return;}}
}
inline int run() {
    Bot bot;while(true) {
        try{if(cambc::is_draft())draft();else bot.turn();}
        catch(const std::exception&e){std::printf("twin-interlock error: %s\n",e.what());}
        catch(...){std::printf("twin-interlock error: unknown exception\n");}
        cambc::end_turn();
    }
}
}
