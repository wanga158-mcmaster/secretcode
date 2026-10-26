#pragma once
#include <cambc.h>
#include <cstdio>
#include <limits>
#include <optional>
#include "policy.h"
#include "counter.h"
#include "../../common/src/capture.h"
#include "../../common/src/draft.h"

namespace h12_counterpunch {
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
    bool renewal_pending=false;
    int access_epoch=-1;
    bool access=true;
};
class Bot {
    friend class Counter;
    Policy policy_;
    int incumbent_=-1,no_work_age_=0;
    Counter counter_;
    std::optional<cambc::camp> counter_target_;
    int counter_source_=-1,paid_count_=0,paid_logged_=0;
    PaidCapture paid_;
    bool counter_life_=true,prompt_bank_=false;
    Opportunity seed_{};
    bool walked_=false;
    SpawnRest spawn_rest_;
    int arm_count_=0,cleanup_count_=0,withdraw_count_=0,core_logs_=0;
    vec2 escape_walk_{-1,-1},escape_dash_{-1,-1};
    int skirmish_turns_=0,skirmish_until_=0;
    RouteWatch route_watch_;
    Renewal renewal_;
    bool first_work_logged_=false;
    int logged_renewals_=0;
    std::vector<Source> sources_;
    std::array<int,6> camp_ranges_{};
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
                if(!loaded[kind]){designs[kind]=c.base_stats();camp_ranges_[kind]=designs[kind].base_attack_range;loaded[kind]=true;}
                const auto stats=designs[kind];
                s.harmless&=stats.base_strength<=0;s.design_hp+=stats.base_health;s.design_incoming+=stats.base_strength;
            }

            sources_.push_back(std::move(s));
        }
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
        if(fresh_.bag==0)prompt_bank_=false;
        // Capture resolves before this entry. Recognize the actual personal
        // payout before bank/recall/source replacement can bypass observe().
        if(paid_.possible(fresh_.round,fresh_.gold,fresh_.bag)) {
            const int id=paid_.source();
            if(id>=0&&on_source(id,origin_)) {
                const auto status=sources_[id].group.state();
                if(paid_.receipt(fresh_.round,std::holds_alternative<cambc::map::camp_group_respawning>(status),fresh_.gold,fresh_.bag)){
                    prompt_bank_=fresh_.bag>0;++paid_count_;
                }
            }
        }
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
                if(distance(origin_,p)<=camp_ranges_[unsigned(c.kind())]+1)nearby_incoming_+=strength;
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
        if constexpr(std::is_same_v<Target,cambc::camp>) {
            if(identity_==9&&COUNTERPUNCH_ORDER&&cambc::fuel_remaining()>50000) {
                counter_target_=target;seed_={};seed_.escape=true;seed_.incoming=target.strength();seed_.enemy=!enemies_.empty();
                // Cheap real-state threshold first; expensive escape/competitor
                // observations run only for an otherwise executable window.
                if(admit(opportunity())) {
                    prepare_opportunity();
                    const auto result=counter_.arm(*this,camp_id(target));
                    if(result==CounterResult::Armed){++arm_count_;counter_source_=incumbent_;return true;}
                    if(counter_acted(result))return true;
                }
            }
        }
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
    static int camp_id(const cambc::camp&c){return int(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(c)));}
    // A concrete straight move-one/dash-two suffix, checked at admission and
    // again when executed. Every endpoint is visible and currently free.
    bool escape_prefix(bool execute=false) {
        auto&me=cambc::self();if(!me.can_move()||!counter_target_)return false;
        const auto abilities=me.abilities();const auto&dash=abilities[1];
        if(!dash.is_dash()||dash.cooldown()>(execute?0:1)||!dash.info())return false;
        const auto payload=dash.get<cambc::ability_dash>();if(payload.range<2)return false;
        auto occupied=cambc::vision::camp_tiles()|cambc::vision::champion_tiles()|cambc::vision::teleport_tiles();
        auto hard=cambc::map::walls()|cambc::map::altars()|cambc::map::spawn_region(team::enemy);
        const auto holes=cambc::map::holes();const auto p=me.position(),target=counter_target_->spawn_position();
        const auto visible_camps=cambc::vision::camps();
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            if(!dx&&!dy)continue;
            const vec2 a{p.x+dx,p.y+dy},b{p.x+2*dx,p.y+2*dy},c{p.x+3*dx,p.y+3*dy};
            if(distance(a,target)<=distance(p,target)||!cambc::map::is_in_bounds(c))continue;
            if(!cambc::vision::in_vision(a)||!cambc::vision::in_vision(b)||!cambc::vision::in_vision(c))continue;
            if(hard.is_set(a)||hard.is_set(b)||hard.is_set(c)||holes.is_set(a)||holes.is_set(c)
                ||occupied.is_set(a)||occupied.is_set(b)||occupied.is_set(c))continue;
            bool safe=true;
            for(const auto&camp:visible_camps)if(camp.is_alive()&&camp.strength()>0
                &&distance(c,camp.spawn_position())<=camp_ranges_[unsigned(camp.kind())]){safe=false;break;}
            const EscapeFacts facts{me.can_move(),dash.is_dash(),true,true,true,true,safe,dash.cooldown(),int(payload.range)};
            if(!escape_admitted(facts,execute))continue;
            escape_walk_=a;escape_dash_=c;
            if(!execute)return true;
            if(!permitted()||!me.can_move()||!me.move_absolute(a))return false;
            walked_=true;origin_=me.position();
            if(dash.is_ready()&&dash.valid_tiles().is_set(c)&&dash.use(c)){origin_=me.position();return true;}
            return true;
        }
        return false;
    }
    void prepare_opportunity() {
        seed_={};if(!counter_target_||!counter_target_->is_visible())return;
        auto&me=cambc::self();const auto&target=*counter_target_;const auto p=target.spawn_position();
        seed_.escape=escape_prefix();seed_.enemy=!enemies_.empty();
        for(const auto&c:cambc::vision::champions(team::ally))if(static_cast<cambc::handle_champion_t>(c)!=static_cast<cambc::handle_champion_t>(me)
            &&c.is_alive()&&distance(c.position(),p)<=distance(me.position(),p)){seed_.competitor=true;break;}
        // Raw incoming is a conservative upper bound even after Jungle. Count
        // sleepers in the selected group because this basic wakes that group.
        for(const auto&c:cambc::vision::camps())if(c.is_alive()&&c.strength()>0
            &&distance(me.position(),c.spawn_position())<=camp_ranges_[unsigned(c.kind())])
            seed_.incoming+=c.strength();
    }
    Opportunity opportunity()const {
        auto o=seed_;if(!counter_target_||!counter_target_->is_visible())return o;
        const auto&c=*counter_target_;auto&me=cambc::self();const auto abilities=me.abilities();const auto&stun=abilities[0];
        o.alive=c.is_alive();if(!o.alive)return o;
        o.health=c.health();o.hp=me.health();o.maximum=me.max_health();o.bag=int(me.gem_count());
        o.awake=c.is_awake();o.damaging=c.strength()>0;o.stunned=c.buffs().has_stun();o.shielded=c.buffs().has_shield()||me.buffs().has_shield();
        o.adjacent=distance(me.position(),c.spawn_position())<=1;o.basic_ready=me.can_attack();
        float multiplier=me.base_stats().camp_damage_multiplier;
        if(monster_>=5)multiplier*=1+cambc::item_type(cambc::item_type::monster_emblem_of_health).passive_magnitude();
        o.basic=me.strength()*multiplier;o.stun=stun.damage_amount()*multiplier;
        // Conservative: do not credit Monster's possible retaliation bonus.
        o.retaliation=cambc::constants::passive_abilities::ren_retaliation_focus_ratio*me.focus();
        o.stun_next=stun.cooldown()<=1;
        o.stun_now=cast_compatible(stun.is_ready(),true,walked_)&&stun.valid_tiles().is_set(c.spawn_position());
        return o;
    }
    bool visible()const{return counter_target_&&counter_target_->is_visible();}
    bool alive()const{return visible()&&counter_target_->is_alive();}
    bool same_life()const{return counter_life_&&(!alive()||counter_target_->health()<=counter_.after());}
    bool basic(){
        if(!permitted()||!alive()||!cambc::self().can_attack()||!cambc::self().attack_tiles().is_set(counter_target_->spawn_position()))return false;
        cambc::self().attack(*counter_target_);return true;
    }
    bool stun(){
        if(!permitted()||!alive())return false;
        const auto a=cambc::self().abilities()[0];
        return cast_compatible(a.is_ready(),true,walked_)&&a.valid_tiles().is_set(counter_target_->spawn_position())&&a.use(*counter_target_);
    }
    bool cleanup_available()const {
        if(!permitted()||!alive())return false;
        const auto o=opportunity();
        return cleanup_admitted(o);
    }
    void withdraw(){
        if(counter_source_>=0)sources_[counter_source_].unavailable=fresh_.round+48;
        incumbent_=-1;recovering_=true;
        if(!escape_prefix(true))recover();
    }
    bool bank() {
        if(fresh_.bag<=0||(!prompt_bank_&&!policy_.bank(fresh_)))return false;
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
                    if(s.dead[index]){for(const auto&member:s.group.camps())counter_.renewed(camp_id(member));std::fill(s.dead.begin(),s.dead.end(),0);}
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
                s.renewal_pending=true;
                paid_.completed(id);
                std::fill(s.dead.begin(),s.dead.end(),0);s.dead_epoch=-1;
            }
            else if(s.renewal_pending){for(const auto&member:s.group.camps())counter_.renewed(camp_id(member));s.renewal_pending=false;}
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
        remember_hold(id);
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
        j.original_hp=s.design_hp;j.remaining_hp=s.hp;
        j.committed=id==incumbent_;j.known=s.known&&s.observed==fresh_.round;j.ready=s.reward&&s.observed==fresh_.round;
        j.work=j.ready?0:(s.known?s.hp:s.design_hp)/std::max(1.f,cambc::self().strength());
        j.incoming=s.harmless?0:s.known?s.incoming:s.design_incoming;j.loss=j.incoming*(j.work+2);
        j.affordable=j.ready||fresh_.hp-j.loss>=fresh_.maximum*.4f;
        return j;
    }
    int select() {
        if(fuel()>75000) {
            int observed=0;
            for(int id=0;id<int(sources_.size())&&observed<2&&fuel()>65000;++id)if(available(id)&&distance(origin_,sources_[id].anchor)<=5) {
                if(cambc::vision::in_vision(sources_[id].anchor)){observe(id);++observed;}
            }
        }
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
    bool permitted()const {
        const auto st=cambc::self().state();
        return cambc::self().is_alive()&&!channel()&&has_action(st.contains(cambc::champion_state::can_move),
            st.contains(cambc::champion_state::can_attack),st.contains(cambc::champion_state::can_cast));
    }
    void remember_hold(int id){
        if(id==paid_.source()&&on_source(id,cambc::self().position()))paid_.hold(id,fresh_.round,int(cambc::self().gold_count()),int(cambc::self().gem_count()));
    }
    void gameplay() {
        auto&me=cambc::self();const bool alive=me.is_alive();
        if(!alive) {
            if(was_alive_){++life_;no_work_age_=0;incumbent_=-1;capture_.reset();counter_.reset();}
            was_alive_=false;spawn_rest_.death();paid_.reset();prompt_bank_=false;recovering_=false;shop();return;
        }
        if(!was_alive_){was_alive_=true;++life_;no_work_age_=0;}
        ++no_work_age_;snapshot();
        const bool arrival_rest=spawn_rest_.entry(fresh_.round,spawn(),fresh_.hp<fresh_.maximum);
        if(capture_.source()>=0&&!on_source(capture_.source(),origin_))capture_.reset();
        const auto allowance=me.state();
        if(!has_action(allowance.contains(cambc::champion_state::can_move),allowance.contains(cambc::champion_state::can_attack),
            allowance.contains(cambc::champion_state::can_cast)))return;
        shop();snapshot();
        if(spawn()){
            recovering_=false;
            // Arrival needs its end-turn heal. Later camp-start chip cannot
            // imprison a fully healed-at-end actor on spawn forever.
            if(arrival_rest)return;
        }
        safety();
        if(counter_.pending()) {
            prepare_opportunity();
            const auto result=counter_.cleanup(*this);
            if(result==CounterResult::Cleanup||result==CounterResult::Dead){++cleanup_count_;paid_.expect(counter_source_);}
            if(result==CounterResult::Withdraw){++withdraw_count_;return;}
            if(result==CounterResult::Cleanup){++work_;no_work_age_=0;renewal_.useful_work(fresh_.round);return;}
        }
        const bool danger=policy_.retreat(fresh_.hp,fresh_.maximum,nearby_incoming_,fresh_.bag)&&!spawn();
        if(channel()) {
            if(cancel_for_exposure(danger,nearby_incoming_)&&me.is_submitting_gems())me.cancel_submit_gems();
            else if(cancel_for_exposure(danger,nearby_incoming_)&&me.is_recalling())me.cancel_recall();
            else return;
        }
        if(danger) {
            counter_.reset();
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
        if(renewal_.due(fresh_.round,no_work_age_,spawn(),nearby_incoming_>0)&&fresh_.bag==0) {
            renewal_.begin(fresh_.round);
            if(incumbent_>=0)sources_[incumbent_].unavailable=fresh_.round+96;
            incumbent_=-1;route_watch_.reset();capture_.reset();counter_.reset();
            recovering_=true;me.recall();action_kind_=8;return;
        }
        if(bank())return;
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
        // Record actual residence after ALL own actions, even if an ordinary
        // early return bypassed the selected-source observation helper.
        if(cambc::self().is_alive()&&paid_.source()>=0&&on_source(paid_.source(),cambc::self().position())) {
            const auto status=sources_[paid_.source()].group.state();
            if(std::holds_alternative<cambc::map::camp_group_reward_available>(status))remember_hold(paid_.source());
            else if(std::holds_alternative<cambc::map::camp_group_respawning>(status))paid_.completed(paid_.source());
        }
        if(fuel()>30000&&paid_count_!=paid_logged_){paid_logged_=paid_count_;std::printf("counterpunch payout actor=%d round=%d count=%d bag=%u prompt=%d\n",identity_,int(cambc::round_number()),paid_count_,cambc::self().gem_count(),int(prompt_bank_));}
        if(identity_==9&&fuel()>30000&&core_logs_<24&&(arm_count_+cleanup_count_+withdraw_count_>core_logs_)) {
            ++core_logs_;
            std::printf("counterpunch actor=%d round=%d arm=%d cleanup=%d withdraw=%d target=%d source=%d after=%.3f bag=%u gold=%u left=%llu\n",
                identity_,int(cambc::round_number()),arm_count_,cleanup_count_,withdraw_count_,counter_.target(),counter_source_,counter_.after(),
                cambc::self().gem_count(),cambc::self().gold_count(),static_cast<unsigned long long>(fuel()));
        }
    }
};
inline void draft() {
    const auto selected=cambc::draft::current_draft(team::ally);
    for(const int n:Policy::roster) {
        const auto choice=heuristics_draft::choices[n];
        if(std::find(selected.begin(),selected.end(),choice)==selected.end()){cambc::draft::pick(choice);break;}
    }
}
inline int run() {
    Bot bot;while(true) {
        try{if(cambc::is_draft())draft();else bot.turn();}
        catch(const std::exception&e){std::printf("counterpunch error: %s\n",e.what());}
        catch(...){std::printf("counterpunch error: unknown exception\n");}
        cambc::end_turn();
    }
}
}
