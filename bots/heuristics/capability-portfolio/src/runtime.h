#pragma once
#include <cambc.h>
#include <cstdio>
#include <limits>
#include <optional>
#include "policy.h"
#include "program.h"
#include "drafting.h"
#include "../../common/src/capture.h"
#include "../../common/src/draft.h"

namespace h12_capability_portfolio {
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
    friend class Program;
    Program program_;
    Plan plan_{};
    bool walked_=false,plan_valid_=false,threshold_set_=false;
    int special_source_=-1,special_gold_=0,special_bag_=0,threshold_price_=0;
    int special_deadline_=0,program_logs_=0,last_phase_=-1;
    float before_capacity_=0;
    bool access_checked_=false,access_ok_=false,field_bought_=false;
    int special_hold_round_=-100,special_hold_gold_=0,special_hold_bag_=0;
    int skirmish_turns_=0,skirmish_until_=0;
    RouteWatch route_watch_;
    Renewal renewal_;
#if PORTFOLIO_CARGO_RENEWAL
    CargoRenewal cargo_renewal_;
#endif
    bool first_work_logged_=false;
    int logged_renewals_=0;
    bitboard remote_component_;
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
    int route_failures_=0;
    vec2 failed_step_{-1,-1};int failed_until_=0;
    int blocked_source_=-1,blocked_until_=0;
    unsigned actions_=0,logs_=0,event_logs_=0,work_=0;
    int action_kind_=0,last_adopted_=0,last_published_=0;
    std::uint64_t prefix_cost_=0;
#if HEURISTICS_TRACE
    int move_reason_=0;
    vec2 move_goal_{};
#endif

    static vec2 position(Point p){return {p.x,p.y};}
    void initialize_plan() {
        Envelope packet{};
        if(cambc::draft_memory.size()==packet.size())for(std::size_t i=0;i<packet.size();++i)packet[i]=std::uint8_t(cambc::draft_memory[i]);
        plan_valid_=decode(packet,plan_)&&plan_.pick_count==plan_.expected_picks;
        if(plan_valid_) {
            const auto allies=cambc::champions(team::ally);
            if(int(allies.size())!=plan_.pick_count)plan_valid_=false;
            for(int i=0;plan_valid_&&i<plan_.pick_count;++i)
                if(std::none_of(allies.begin(),allies.end(),[&](const auto&c){return int(c.champion_name())==plan_.identities[i];}))plan_valid_=false;
        }
        if(!plan_valid_)plan_=ordinary_plan();
        for(int i=0;i<int(sources_.size());++i)
            if(int(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(sources_[i].group)))==plan_.witness.group_id)special_source_=i;
        Role role=plan_valid_?role_for(plan_,identity_):Role::Ordinary;
        if(special_source_<0||!PORTFOLIO_PROGRAMS)role=Role::Ordinary;
        program_.start(role,int(cambc::round_number()));
        special_gold_=int(cambc::self().gold_count());special_bag_=int(cambc::self().gem_count());
    }
    void initialize() {
        // SDK returns immutable handles and public source records once. No
        // per-source SDK design query or topology setup delays the first job.
        const auto groups=cambc::map::camp_groups();
        terrain_=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(team::enemy);
        altars_=cambc::map::altars().positions();
        identity_=int(cambc::self().champion_name());
        std::array<cambc::camp_stats,6> designs{};
        std::array<bool,6> loaded{};
        for(const auto&g:groups) {
            Source s{g,g.camps().front().spawn_position()};
            s.gems=int(g.gem_value());s.gold=int(g.gold_value());s.members=int(g.camps().size());
            s.monolith=std::any_of(g.begin(),g.end(),[](const auto&c){return c.is_monolith();});
            s.dead.assign(s.members,0);
            for(const auto&c:g.camps()) {
                s.pads.push_back(c.spawn_position());const auto kind=unsigned(c.kind());
                if(!loaded[kind]){designs[kind]=c.base_stats();loaded[kind]=true;}
                const auto stats=designs[kind];
                s.harmless&=stats.base_strength<=0;s.design_hp+=stats.base_health;s.design_incoming+=stats.base_strength;
            }

            sources_.push_back(std::move(s));
        }
        initialize_plan();
        initialized_=true;
    }
    bool shop(bool field_one=false) {
        bool purchased=false;
        auto&me=cambc::self();
        if(me.is_alive()&&!spawn()&&!field_one)return false;
        for(int stage=0;stage<(field_one?1:3)&&cambc::fuel_remaining()>16000;++stage) {
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
            const auto count=field_one?std::min(1u,me.gold_count()/price):std::min(limit,me.gold_count()/price);
            if(!count||!me.can_buy(item,count))return purchased;
            me.buy(item,count);++finance_;purchased=true;
            if(affiliation==0)monster_+=count;if(affiliation==1)jungle_+=count;
        }
        return purchased;
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
            if(fresh_.bag>previous_bag_||fresh_.gold>previous_gold_||(fresh_.bag==0&&previous_bag_>0)){
                no_work_age_=0;renewal_.useful_work(fresh_.round);
            }
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
        s.access=true;
        for(const auto p:s.pads)if(!component_.is_set(p)){s.access=false;break;}
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
        route_watch_.measured({goal.x,goal.y},int(current),int(round));
        auto best=pos;auto best_d=current;
        for(const auto p:walls.reachable_tiles(pos,1).positions()) {
            if((p==failed_step_&&int(round)<failed_until_))continue;
            const auto d=field.get(p);if(d<best_d){best=p;best_d=d;}
        }
        if(best==pos)return false;
        if(cambc::round_number()!=round||!me.is_alive()||channel()||!me.can_move())return false;
        const bool ok=me.move_absolute(best);walked_|=ok;
        if(ok)route_watch_.measured({goal.x,goal.y},int(best_d),int(round));
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
        if(fresh_.bag<=0||(!(program_.phase()==Phase::Bank)&&!policy_.bank(fresh_)
#if PORTFOLIO_CARGO_RENEWAL
            &&!cargo_renewal_.active(fresh_.round)
#endif
            ))return false;
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
        remember_special_hold(id);
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
    int select() {
        std::vector<heuristics::Job> jobs;
        for(int id=0;id<int(sources_.size());++id) {
            if(!available(id)||!capture_access(id))continue;
            jobs.push_back(job(id));
        }
        return heuristics::choose(policy_,fresh_,jobs);
    }
    void work() {
        if(incumbent_<0||!available(incumbent_))incumbent_=select();
        if(incumbent_<0) {
            if(!spawn()&&no_work_age_>16){recovering_=true;recover();}
            return;
        }
        const int id=incumbent_;auto view=observe(id);
        if(view.respawn){incumbent_=-1;return;}
        if(view.reward&&on_source(id,origin_)){expire_hold(id);action_kind_=3;return;}
        if(capture_.source()==id&&!view.reward)capture_.reset();
        if(policy_.retreat(fresh_.hp,fresh_.maximum,nearby_incoming_,fresh_.bag)&&!spawn()) {
            sources_[id].unavailable=quarantine_after_refusal(fresh_.round);incumbent_=-1;
            recovering_=true;recover();return;
        }
        const bool damage_ok=true;
        route_watch_.adopt(id,fresh_.round);
        bool acted=false;
        if(view.target&&damage_ok&&fight(*view.target,view.incoming>0)) {
            action_kind_=2;++work_;no_work_age_=0;renewal_.useful_work(fresh_.round);route_failures_=0;acted=true;
        } else if(view.target&&!damage_ok){blocked_source_=id;blocked_until_=fresh_.round+3;incumbent_=-1;}
        else {
            action_kind_=1;const auto goal=view.target?view.target->spawn_position():view.goal;
            acted=move(goal,view.reward?0:1);
            if(acted&&view.target&&damage_ok&&cambc::fuel_remaining()>30000&&fight(*view.target,view.incoming>0)) {
                ++work_;no_work_age_=0;renewal_.useful_work(fresh_.round);action_kind_=2;
            }
            if(acted)route_failures_=0;else ++route_failures_;
        }
        if(route_watch_.stale(fresh_.round,no_work_age_)||route_failures_>=6) {
            blocked_source_=id;blocked_until_=fresh_.round+18;incumbent_=-1;route_failures_=0;
        }
    }
    int round()const{return int(cambc::round_number());}
    int bag()const{return int(cambc::self().gem_count());}
    bool permitted()const {
        const auto st=cambc::self().state();
        return cambc::self().is_alive()&&!channel()&&has_action(st.contains(cambc::champion_state::can_move),
            st.contains(cambc::champion_state::can_attack),st.contains(cambc::champion_state::can_cast));
    }
    bool unsafe()const{return nearby_incoming_>0||!enemies_.empty();}
    bool at_entry()const{return cambc::self().position()==position(plan_.witness.entry);}
    bool at_landing()const{return cambc::self().position()==position(plan_.witness.landing);}
    bool exact_crossing(bool reverse,bool cast) {
        auto&me=cambc::self();const auto from=position(reverse?plan_.witness.landing:plan_.witness.entry);
        const auto to=position(reverse?plan_.witness.entry:plan_.witness.landing);
        if(me.position()!=from||identity_!=1||distance(from,to)!=2||!permitted()||unsafe()
            ||std::abs(to.x-from.x)==1||std::abs(to.y-from.y)==1)return false;
        const vec2 middle{(from.x+to.x)/2,(from.y+to.y)/2};
        const auto forbidden=cambc::map::walls()|cambc::map::altars()|cambc::map::spawn_region(team::enemy);
        if(!cambc::map::holes().is_set(middle)||!cambc::map::is_in_bounds(to)||forbidden.is_set(to)||forbidden.is_set(middle))return false;
        if(!cambc::vision::in_vision(to)||cambc::vision::camp_tiles().is_set(to)
            ||cambc::vision::champion_tiles().is_set(to)||cambc::vision::teleport_tiles().is_set(to))return false;
        for(const auto&a:me.abilities())if(a.is_dash()&&a.can_target_ground()&&a.info()) {
            const auto p=a.get<cambc::ability_dash>();
            if(p.range<2||!cast_compatible(a.is_ready(),p.exclusive_with_movement,walked_)||!a.valid_tiles().is_set(to))return false;
            if(!cast)return true;
            const bool ok=a.use(to);
            if(ok){component_ready_=false;origin_=me.position();action_kind_=7;}
            return ok;
        }
        return false;
    }
    bool admit() {
        if(special_source_<0||unsafe()||!plan_valid_)return false;
        auto&s=sources_[special_source_];
        if(s.monolith||(s.gems<4&&s.gold<policy_.next_price)||s.unavailable>fresh_.round)return false;
        if(!access_checked_) {
            if(fuel()<75000)return false;
            const auto landing=position(plan_.witness.landing),entry=position(plan_.witness.entry);
            if(terrain_.is_set(landing)||terrain_.is_set(entry)||!component_.is_set(entry)||component_.is_set(landing))return false;
            cambc::distance_field field;bitboard reached;
            field.compute(terrain_,around(landing,0),reached);
            remote_component_=std::move(reached);access_ok_=true;
            for(const auto p:s.pads)if(!remote_component_.is_set(p)){access_ok_=false;break;}
            bool bank_route=false;
            for(const auto p:altars_)if(!(around(p,1)&component_).is_empty()){bank_route=true;break;}
            access_ok_&=bank_route;
            access_checked_=true;
        }
        if(!access_ok_)return false;
        const auto v=observe(special_source_);
        if(v.respawn)return false;
        const float work=(s.known?s.hp:s.design_hp)/std::max(1.f,cambc::self().strength());
        const float exposure=std::min(4.f,work)*(s.known?s.incoming:s.design_incoming);
        return work<=25&&fresh_.hp-exposure>=fresh_.maximum*.5f&&nearby_incoming_==0;
    }
    void approach(){move(position(plan_.witness.entry));}
    bool cross_ready()const {
        for(const auto&a:cambc::self().abilities())if(a.is_dash())return a.is_ready();
        return false;
    }
    bool cross(bool reverse){return exact_crossing(reverse,true);}
    void return_approach(){move(position(plan_.witness.landing));}
    void exit() {
        incumbent_=-1;
        if(special_source_>=0)sources_[special_source_].unavailable=fresh_.round+48;
        // Recall is the explicit exit fallback when the reserved landing is
        // occupied or cooling down. No waiting for a hidden occupant to move.
        if(!spawn()&&nearby_incoming_==0){cambc::self().recall();action_kind_=8;recovering_=true;}
    }
    void remember_special_hold(int id) {
        if(id!=special_source_||!on_source(id,origin_))return;
        special_hold_round_=fresh_.round;special_hold_gold_=fresh_.gold;special_hold_bag_=fresh_.bag;
    }
    WorkResult clear() {
        if(special_source_<0)return WorkResult::Refused;
        auto v=observe(special_source_);
        if(v.respawn) {
            return capture_receipt(special_hold_round_==fresh_.round-1,true,special_hold_gold_,special_hold_bag_,
                int(cambc::self().gold_count()),bag())?WorkResult::Captured:WorkResult::Refused;
        }
        if(!capture_access(special_source_))return WorkResult::Refused;
        incumbent_=special_source_;work();
        if(incumbent_!=special_source_||recovering_)return WorkResult::Refused;
        return WorkResult::Working;
    }
    bool threshold() {
        if(special_source_<0||identity_!=11||unsafe())return false;
        if(threshold_set_)return fresh_.round<=special_deadline_;
        const auto&s=sources_[special_source_];
        // One capture finances one Focus shard whose queried growth reduces
        // the estimated hit count on at least one reachable subsequent job.
        using I=cambc::item_type;
        threshold_price_=int(I::monster_shard_of_focus.cost());
        const auto me=cambc::self();const auto stats=me.base_stats();
        const float focus=me.focus(),gain=stats.focus_growth;
        float modifier=0;
        for(const auto&a:me.abilities())if((a.is_damage()||a.is_cc())&&a.can_target_camps()&&a.info())
            modifier=std::visit([](const auto&p)->float{if constexpr(requires{p.modifier;})return p.modifier;else return 0;},*a.info());
        if(modifier<=0)return false;
        bool useful=false;
        for(int id=0;id<int(sources_.size());++id)if(id!=special_source_&&capture_access(id)&&!sources_[id].monolith) {
            const float hp=sources_[id].design_hp;
            if(std::ceil(hp/std::max(1.f,modifier*(focus+gain)))<std::ceil(hp/std::max(1.f,modifier*focus))){useful=true;break;}
        }
        if(!useful||threshold_price_<=0||s.gold<threshold_price_||fresh_.hp<fresh_.maximum*.75f||!capture_access(special_source_))return false;
        before_capacity_=me.focus();threshold_set_=true;special_deadline_=fresh_.round+96;
        return true;
    }
    BuyResult field_buy() {
        if(!threshold_set_||field_bought_||identity_!=11||spawn()||unsafe()||fresh_.round>special_deadline_
            ||fresh_.hp<fresh_.maximum*.65f)return BuyResult::Refused;
        if(int(cambc::self().gold_count())<threshold_price_)return BuyResult::Refused;
        if(Policy::purchase(identity_,monster_,jungle_)!=heuristics::Purchase::monster_focus)return BuyResult::Refused;
        field_bought_=shop(true);action_kind_=9;
        return field_bought_?BuyResult::Bought:BuyResult::Refused;
    }
    bool refresh_capacity(){snapshot();incumbent_=-1;return cambc::self().focus()>before_capacity_;}
    bool useful_work(){const auto before=work_;work();return work_>before;}
    void gameplay() {
        auto&me=cambc::self();const bool alive=me.is_alive();
        if(!alive) {
            if(was_alive_){++life_;no_work_age_=0;incumbent_=-1;capture_.reset();program_.refuse();}
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
        const bool danger=policy_.retreat(fresh_.hp,fresh_.maximum,nearby_incoming_,fresh_.bag)&&!spawn();
        if(channel()) {
            if(cancel_for_exposure(danger,nearby_incoming_)&&me.is_submitting_gems())me.cancel_submit_gems();
            else if(cancel_for_exposure(danger,nearby_incoming_)&&me.is_recalling())me.cancel_recall();
            else return;
        }
        if(danger) {
            program_.refuse();
            if(incumbent_>=0)sources_[incumbent_].unavailable=quarantine_after_refusal(fresh_.round);
            incumbent_=-1;recovering_=true;
        }
        if(recovering_){recover();return;}
        int held=capture_.source();if(held<0&&incumbent_>=0&&on_source(incumbent_,origin_))held=incumbent_;
        if(held>=0&&on_source(held,origin_)) {
            const auto status=sources_[held].group.state();
            if(std::holds_alternative<cambc::map::camp_group_reward_available>(status)){expire_hold(held);return;}
            capture_.reset();
        }
        if(!walking_component())return;
#if PORTFOLIO_CARGO_RENEWAL
        const bool cargo_conversion=cargo_renewal_.tick(fresh_.round,no_work_age_,fresh_.bag,program_.active());
        if(cargo_conversion&&bank())return;
#endif
        if(renewal_.due(fresh_.round,no_work_age_,spawn(),nearby_incoming_>0)&&fresh_.bag==0) {
            renewal_.begin(fresh_.round);
            if(incumbent_>=0)sources_[incumbent_].unavailable=fresh_.round+96;
            incumbent_=-1;route_watch_.reset();capture_.reset();program_.refuse();
            recovering_=true;me.recall();action_kind_=8;return;
        }
        if(program_.tick(*this))return;
#if PORTFOLIO_CARGO_RENEWAL
        if(!cargo_conversion&&bank())return;
#else
        if(bank())return;
#endif
        for(const auto&e:enemies_)if(distance(origin_,e.position())<=1
            &&defend_allowed(cambc::map::spawn_region(team::enemy).is_set(e.position()),fresh_.round,skirmish_until_)&&fight(e,true)) {
            if(++skirmish_turns_>=3){skirmish_turns_=0;skirmish_until_=fresh_.round+12;}
            return;
        }
        if(!spawn()&&fresh_.gold>=policy_.next_price*5) {recovering_=true;recover();return;}
        work();
    }
public:
    int current_round()const{return int(cambc::round_number());}
    std::uint64_t fuel()const{return cambc::fuel_remaining();}
    void turn() {
        minimal_=false;force_greedy_=false;walked_=false;action_kind_=0;
        if(!initialized_)initialize();
        gameplay();
        const int phase=int(program_.phase());
        if(fuel()>30000&&program_logs_<24&&(phase!=last_phase_||(!first_work_logged_&&work_>0)
            ||renewal_.attempts()!=logged_renewals_||(program_.active()&&fresh_.round%64==identity_))) {
            ++program_logs_;
            std::printf("portfolio actor=%d round=%d package=%d role=%d phase=%d source=%d job=%d age=%d renewals=%d bag=%u gold=%u kind=%d work=%u componentmax=%llu left=%llu\n",
                identity_,int(cambc::round_number()),int(plan_.package),int(program_.role()),phase,special_source_,
                incumbent_,no_work_age_,renewal_.attempts(),cambc::self().gem_count(),cambc::self().gold_count(),action_kind_,work_,
                static_cast<unsigned long long>(component_max_),static_cast<unsigned long long>(fuel()));
            first_work_logged_|=work_>0;logged_renewals_=renewal_.attempts();
        }
        last_phase_=phase;
    }
};
inline int run() {
    Bot bot;while(true) {
        try{if(cambc::is_draft())draft();else bot.turn();}
        catch(const std::exception&e){std::printf("capability-portfolio error: %s\n",e.what());}
        catch(...){std::printf("capability-portfolio error: unknown exception\n");}
        cambc::end_turn();
    }
}
}
