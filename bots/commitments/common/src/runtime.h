#pragma once
#include "navigation.h"
#include "receipts.h"
#include <cstdio>
#include <iostream>
#include <variant>

namespace p9::live {
// Each champion owns one Runtime and Policy. There is no team command channel,
// remote wallet, shared capture timer, or replay/event oracle in this adapter.
template<class Policy> class Runtime {
    const Profile& profile_;
    Arm arm_;
    Policy policy_{};
    Navigation nav_{};
    PersonalBank bank_{};
    int target_=-1,productive_=-1,hold_=-1,hold_since_=-1,hold_last_=-1;
    int hold_retries_=0,last_round_=-1,route_blocked_epoch_=-1;
    Gems hold_bag_=0;
    int hold_gold_=0;
    int bank_round_=-1,payout_round_=-1,payout_group_=-1,purchase_round_=-1;
    Gems bank_lower_=0,bank_upper_=-1;
    int kill_round_=-1,kill_target_=-1,heal_round_=-1,heal_actor_=-1;
    double heal_amount_=0;
    std::array<std::pair<int,int>,6> credited_deaths_{};
    int death_count_=0;
    bool recovering_=false;
    competition::Exploration exploration_{};
    competition::DamageMemory damage_{};
    int expected_damage_round_=-1;
    double expected_camp_damage_=0;
    std::array<std::pair<int,Gems>,65> enemy_scores_{};
    int score_count_=0,score_cursor_=0;
    ResponseHistory responses_{};
    int budget_reports_=0;
    const char* stage_="startup";
    Task executing_=Task::independent;
    int executing_group_=-1,decision_epoch_=-1;
    std::array<unsigned,13> ordinary_decisions_{},strategic_decisions_{};
    std::vector<cambc::champion> roster_;
    std::vector<unsigned char> visible_cells_;
    int map_columns_=0;
    unsigned fuel_skips_=0;
#if P9_NAV_DIAGNOSTICS
    unsigned diagnostic_reports_=0;
#endif
    bool partial_clear_=false;
    int partial_until_=-1;
    int bank_block_reports_=0;
    std::array<std::uint64_t,6> observation_fuel_{};
    unsigned group_deferrals_=0,route_deferrals_=0;
    int last_work_round_=-1,last_threat_round_=-1;
    Point loaded_position_{};
    int loaded_round_=-1,loaded_still_=0,loaded_trace_round_=-1,loaded_trace_reports_=0;

    void loaded_frame(const World& w) {
        if(w.self.bag<=0||w.self.channel!=Channel::none) {loaded_round_=-1;loaded_still_=0;return;}
        loaded_still_=loaded_round_==w.round-1&&loaded_position_==w.self.position?loaded_still_+1:0;
        loaded_position_=w.self.position;loaded_round_=w.round;
    }
    void loaded_trace(const World& w,const char* branch,std::uint64_t before,int walked=-1,int away=-1) {
        if(w.self.bag<=0||w.self.channel!=Channel::none||loaded_still_<2||loaded_trace_reports_>=8||
           (loaded_trace_reports_&&w.round-loaded_trace_round_<64))return;
        const auto after=cambc::fuel_remaining();
        if(after<20000||!same_round(w.round))return;
        // Position and flags belong to the coherent observation, not a new
        // live query after movement. The next owner frame proves progress.
        char line[512];
        const int size=std::snprintf(line,sizeof(line),
            "p9 loaded actor=%d round=%d branch=%s x=%d y=%d hp=%d bag=%lld safe=%d recover=%d partial=%d can=%d%d%d fuel=%llu:%llu walked=%d away=%d home=%d banks=%d,%d\n",
            w.self.id,w.round,branch,w.self.position.x,w.self.position.y,static_cast<int>(w.self.hp),
            static_cast<long long>(w.self.bag),w.safe_now,recovering_,partial_clear_,
            w.self.can_move,w.self.can_attack,w.self.can_cast,static_cast<unsigned long long>(before),
            static_cast<unsigned long long>(after),walked,away,Navigation::steps(nav_.own_spawn.get(),point(w.self.position)),
            w.altar_count>0?w.altars[0].travel:-1,w.altar_count>1?w.altars[1].travel:-1);
        if(size<=0)return;
        loaded_trace_round_=w.round;++loaded_trace_reports_;
        std::cout.write(line,std::min(size,static_cast<int>(sizeof(line)-1)));
    }

    void budget_trace(const char* phase,int round,std::uint64_t before) {
        const auto after=cambc::fuel_remaining();
        if(after<20000||budget_reports_>=12)return;
        if(!same_round(round)||(before>after&&before-after>80000)) {
            std::string line="p9 budget ["+std::string(profile_.slug)+"] round="+std::to_string(round)+
                " phase="+phase+" before="+std::to_string(before)+" after="+std::to_string(after)+
                " now="+std::to_string(cambc::round_number())+'\n';
            std::cout<<line;
            ++budget_reports_;
        }
    }

    BankSample bank_sample() const {
        auto& me=cambc::self();BankSample s;
        s.round=static_cast<int>(cambc::round_number());s.alive=me.is_alive();
        s.respawn=cambc::constants::respawn_time;s.score=cambc::global_submitted_gems(cambc::team::ally);
        s.bag=me.gem_count();s.channel=me.is_submitting_gems();
        if(s.channel)s.remaining=me.remaining_channel_time();
        if(!same_round(s.round)){s.alive=false;s.round=-1;}return s;
    }
    std::optional<cambc::champion> visible_actor(int id) const {
        for(const auto& c:roster_)if(handle(c)==id&&c.is_visible()&&c.is_alive())return c;
        return std::nullopt;
    }
    int nearest_bank(const World& w,int requested=-1) const {
        return competition::nearest_bank(w,requested);
    }
    int bank_travel(const World&,int bank,Point position) {
        return nav_.live_steps(nav_.banks[bank].field.get(),point(position));
    }
    bool shopping(cambc::item_type item,int limit,int round) {
        auto& me=cambc::self();
        if(!same_round(round)||channel_active()||cambc::fuel_remaining()<25000)return false;
        const auto held=owned_shards(me,item);const auto info=item.info();
        if(held>=static_cast<unsigned>(limit)||info.cost==0)return false;
        const auto gold=me.gold_count();
        const auto count=std::min({static_cast<unsigned>(limit)-held,gold/info.cost,8u});
        if(!count||!me.can_buy(item,count)||!same_round(round))return false;
        me.buy(item,count);
        if(same_round(round)&&me.gold_count()<gold)purchase_round_=round;
        return true;
    }
    void ordinary_shop(int round) {
        auto& me=cambc::self();
        const auto strength=cambc::item_type::monster_shard_of_strength;
        const auto health=cambc::item_type::jungle_shard_of_health;
        if(!same_round(round)||cambc::fuel_remaining()<=35000||!me.can_buy(strength))return;
        constexpr bool has_health_order=requires(Champion type){Policy::durability_after_first_jungle(type);};
        [[maybe_unused]] const bool durability_first=[&] {
            if constexpr(has_health_order)
                return Policy::durability_after_first_jungle(champion_type(me.champion_name()));
            return false;
        }();
        // Observation can leave a useful shopping budget below 65k. Keep the
        // bounded purchase's own 25k guard, rather than starving reinvestment.
        for(int pass=0;pass<3&&same_round(round)&&cambc::fuel_remaining()>35000;++pass) {
            if(me.gold_count()<strength.cost())return;
            const auto m=owned_shards(me,strength),j=owned_shards(me,health);
            const auto m_goal=std::max(profile_.monster_shards,strength.combine_count());
            auto j_goal=std::max(profile_.jungle_shards,health.combine_count());
            if constexpr(has_health_order)if(durability_first)j_goal=health.combine_count();
            if(m<m_goal) {if(!shopping(strength,m_goal,round))return;}
            else if(j<j_goal) {if(!shopping(health,j_goal,round))return;}
            else {
                if constexpr(has_health_order)if(durability_first) {
                    const auto defense=cambc::item_type::durability_shard_of_health;
                    const auto d_goal=defense.combine_count();
                    if(owned_shards(me,defense)<d_goal) {
                        if(!shopping(defense,d_goal,round))return;
                        continue;
                    }
                }
                const auto power=cambc::item_type::power_shard_of_strength;
                if(!shopping(power,owned_shards(me,power)+8,round))return;
            }
        }
    }
    void note_kill(cambc::champion enemy,int round,Gems bag,unsigned gold) {
        auto& me=cambc::self();
        if(!same_round(round)||me.gold_count()<=gold||me.gem_count()<bag)return;
        cambc::detail::champion_info live{};
        const auto code=cambc::detail::sdk_champion(static_cast<std::uint32_t>(handle(enemy)),&live);
        if(code==static_cast<std::int32_t>(cambc::error_code::api_not_in_vision)||
           code==static_cast<std::int32_t>(cambc::error_code::api_not_found))return;
        cambc::detail::check(code);
        if(cambc::champion_state::from_bits_retain(static_cast<std::uint32_t>(live.state)).contains(cambc::champion_state::alive))return;
        // This bracket contains only our targeted action(s), no intervening
        // owner/ally turn. Assist gold or a later disappearance is not a kill.
        kill_round_=round;kill_target_=handle(enemy);
        if(death_count_<6)credited_deaths_[death_count_++]={kill_target_,round};
        else {for(int i=1;i<6;++i)credited_deaths_[i-1]=credited_deaths_[i];credited_deaths_[5]={kill_target_,round};}
    }
    bool attack_enemy(cambc::champion enemy,int round,bool interrupt=false,bool walked=false) {
        auto& me=cambc::self();if(!same_round(round)||!enemy.is_visible()||!enemy.is_alive())return false;
        const auto gold=me.gold_count();const Gems bag=me.gem_count();
        bool acted=interrupt?cast_target(enemy,false,true,round,walked):fight(enemy,true,round,walked);
        note_kill(enemy,round,bag,gold);return acted;
    }
    void derive_certificates(World& w) const {
        // This intentionally narrow response certificate requires every other
        // public rival identity to be a personally credited, still-respawning
        // death. Unseen living substitutes cannot be priced as waiting.
        if(profile_.design==9&&w.enemy_count==1&&!w.extra_opposition) {
            const auto& e=w.enemies[0];bool covered=true;
            const int duration=std::min(e.cooldown[0],e.cooldown[1]);
            for(const auto& rival:roster_)if(rival.is_enemy()&&handle(rival)!=e.id) {
                bool dead=false;
                for(int i=0;i<death_count_;++i)if(credited_deaths_[i].first==handle(rival)&&
                    w.round>=credited_deaths_[i].second&&w.round+duration<credited_deaths_[i].second+cambc::constants::respawn_time)dead=true;
                covered&=dead;
            }
            // Require ordinary attacks unable to reach either column member
            // during this window too. No inferred Vendetta cooldown stability.
            const auto* ren=w.pick(0);const auto* collector=w.pick(1);
            const int arrival=collector?distance(e.position,collector->position)-e.range:0;
            bool no_crystal=false;
            if(auto rival=visible_actor(e.id))no_crystal=rival->consumables()[cambc::consumable_type::tp_crystal].count()==0;
            if(covered&&no_crystal&&ready(ren)&&ready(collector)&&duration>0&&e.type!=Champion::vendetta&&arrival>2*duration&&
               e.cooldown[0]>=0&&e.cooldown[1]>=0) {
                w.response_window_verified=true;w.verified_response_enemy=e.id;w.verified_response_until=w.round+duration;
            }
        }
        if(profile_.design!=10)return;
        const auto* clearer=w.pick(1);const auto* collector=w.pick(2);
        if(!ready(clearer)||!ready(collector)||w.enemy_count||w.extra_opposition)return;
        const World::Travel* clearer_routes=nullptr;const World::Travel* collector_routes=nullptr;
        for(int i=0;i<w.route_count;++i) {
            if(w.routes[i].actor==clearer->id)clearer_routes=&w.routes[i];
            if(w.routes[i].actor==collector->id)collector_routes=&w.routes[i];
        }
        for(int i=0;i<w.group_count;++i) {
            const auto& g=w.groups[i];
            const int clearer_travel=clearer->id==w.self.id?g.travel:clearer_routes?clearer_routes->groups[i]:-1;
            const int collector_travel=collector->id==w.self.id?g.travel:collector_routes?collector_routes->groups[i]:-1;
            if(!g.observed||g.respawning||g.contested||g.to_bank<0||!safe_for(g,*clearer)||!safe_for(g,*collector)||
               clearer_travel<0||collector_travel<0)continue;
            const int service=std::max(clearer_travel,collector_travel)+
                g.clear_turns+w.capture_turns+2*g.to_bank+w.ticks(collector->bag+g.gems)+4;
            // Conservative full-window exposure; no future heal or assumed
            // tank assignment appears in this unsupported-column calculation.
            if(service>120||collector->hp-g.incoming*(g.clear_turns+2)<.3*collector->max_hp||
               clearer->hp-g.incoming*(g.clear_turns+2)<.3*clearer->max_hp)continue;
            w.column_without_healer_verified=true;w.verified_column_group=g.id;break;
        }
    }
    World observe(int round) {
#if P9_NAV_DIAGNOSTICS
        NavBudgetTrace measured(diagnostic_reports_,round,"observe",-1,&observation_fuel_);
#endif
        observation_fuel_[0]=cambc::fuel_remaining();
        World w;w.round=round;w.picks=static_cast<int>(cambc::draft::current_draft(cambc::team::ally).size());
        w.max_rounds=static_cast<int>(cambc::constants::max_rounds);w.win_gems=static_cast<int>(cambc::constants::winning_gems);
        w.capture_turns=cambc::constants::capture_time+1;
        w.tribute_multiplier=cambc::constants::submit_gem_duration_multiplier;
        w.tribute_base=cambc::constants::submit_gem_base_duration;w.tribute_minimum=cambc::constants::submit_gem_minimum_duration;
        w.score=cambc::global_submitted_gems(cambc::team::ally);w.enemy_score=cambc::global_submitted_gems(cambc::team::enemy);
        const int remaining=std::max(0,w.max_rounds-round);
        int old_round=round;Gems old_score=w.enemy_score;
        for(int i=0;i<score_count_;++i)if(enemy_scores_[i].first>=round-64&&enemy_scores_[i].first<old_round) {
            old_round=enemy_scores_[i].first;old_score=enemy_scores_[i].second;
        }
        const double rate=double(std::max<Gems>(0,w.enemy_score-old_score))/std::max(1,round-old_round);
        const std::array<double,3> rates{0.,rate,std::max(1.,2*rate)};
        for(int i=0;i<3;++i)w.windows[i]=std::min(120,rates[i]>0?
            std::min(remaining,static_cast<int>(std::ceil(std::max<Gems>(0,w.win_gems-w.enemy_score)/rates[i]))):remaining);
        w.self=actor_view(cambc::self(),profile_,true);w.own_gold=static_cast<int>(cambc::self().gold_count());
        w.defense_cost=static_cast<int>(cambc::item_type::durability_shard_of_health.cost());
        w.lock_cost=static_cast<int>(cambc::consumable_type::altar_lock.cost());
        w.has_lock=cambc::self().consumables()[cambc::consumable_type::altar_lock].count()>0;
        if(w.self.channel==Channel::tribute)w.tribute_ticks=cambc::self().remaining_channel_time();
        const auto personal=bank_sample();
        if(personal.round!=round){bank_.invalidate();return w;}
        if(auto receipt=bank_.observe(personal);receipt.completed) {
            bank_round_=round;bank_lower_=receipt.lower;bank_upper_=receipt.upper;
        }
        w.own_bank_completed=bank_round_==round;w.last_bank_round=bank_round_;w.bank_lower=bank_lower_;w.bank_upper=bank_upper_;
        w.last_kill_round=kill_round_;w.last_kill_target=kill_target_;w.own_kill_receipt=kill_round_>=round-1&&kill_round_>=0;
        w.last_heal_round=heal_round_;w.last_heal_actor=heal_actor_;w.last_heal_amount=heal_amount_;
        w.own_heal_receipt=heal_round_>=round-1&&heal_round_>=0;
        w.last_purchase_round=purchase_round_;w.own_purchase_receipt=purchase_round_>=round-1&&purchase_round_>=0;
        observation_fuel_[1]=cambc::fuel_remaining();
        nav_.threats.clear();
        double enemy_danger=0;
        for(const auto& c:roster_) {
            if(handle(c)==w.self.id)continue;
            auto a=actor_view(c,profile_);if(!a.visible||!a.alive)continue;
            if(a.ally) {if(w.ally_count<5)w.allies[w.ally_count++]=a;}
            else {
                // Policy comparisons retain two rivals, but movement must
                // respect every personally visible opponent.
                nav_.threats.push_back({a.position,a.threat_range+1,a.id,a.burst,true});
                if(distance(a.position,w.self.position)<=a.threat_range+1)enemy_danger+=a.burst;
                if(w.enemy_count<2)w.enemies[w.enemy_count++]=a;
                else {
                    w.extra_opposition=true;
                    // Policies have a two-rival comparison budget. Retain the
                    // closest two; movement and immediate danger still include
                    // every personally observed rival above.
                    const int farther=distance(w.enemies[0].position,w.self.position)>
                        distance(w.enemies[1].position,w.self.position)?0:1;
                    if(distance(a.position,w.self.position)<distance(w.enemies[farther].position,w.self.position))
                        w.enemies[farther]=a;
                }
            }
        }
        observation_fuel_[2]=cambc::fuel_remaining();
        for(int i=0;i<static_cast<int>(nav_.banks.size());++i) {
            auto& source=nav_.banks[i];auto& a=w.altars[w.altar_count++];
            a.id=i;a.position=point(source.position);a.visible=cambc::vision::in_vision(source.position);
            a.travel=Navigation::steps(source.field.get(),point(w.self.position));a.route_known=a.travel>=0;
            if(source.field)a.approach=point(source.approach);
            if(a.visible) {a.enemy_lock=cambc::vision::altar_lock_tiles(cambc::team::enemy).is_set(source.position);
                a.friendly_lock=cambc::vision::altar_lock_tiles(cambc::team::ally).is_set(source.position);}
        }
        const auto visible=cambc::vision::visible_tiles();
        if(visible_cells_.empty()) {
            map_columns_=cambc::map::width();visible_cells_.resize(map_columns_*cambc::map::height());
        }
        std::fill(visible_cells_.begin(),visible_cells_.end(),0);
        for(auto p:visible.positions())visible_cells_[p.y*map_columns_+p.x]=1;
        auto in_view=[&](Point p){const int i=p.y*map_columns_+p.x;
            return p.x>=0&&p.x<map_columns_&&p.y>=0&&i>=0&&i<static_cast<int>(visible_cells_.size())&&visible_cells_[i];};
        struct SeenCamp {int id;bool known=false,dead=false;cambc::detail::camp_state live{};};
        std::vector<SeenCamp> camps(nav_.members.size());
        double danger=0;
        if(w.self.alive)for(const auto& member:nav_.members)if(in_view(member.spawn)) {
            auto& seen=camps[member.index];
            seen.id=static_cast<int>(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(member.unit)));
            seen.known=read_camp(member.unit,seen.live);seen.dead=seen.known&&seen.live.health<=0;
            const bool nearby=distance(member.spawn,w.self.position)<=member.range+1;
            if(!seen.known||(nearby&&seen.live.health>0&&seen.live.strength>0)) {
                std::uint32_t state{};const auto code=cambc::detail::sdk_camp_state(static_cast<std::uint32_t>(seen.id),&state);
                if(code!=static_cast<std::int32_t>(cambc::error_code::api_not_in_vision)) {
                    cambc::detail::check(code);seen.dead=state==0;
                    if(state==1&&seen.known&&nearby) {
                        nav_.threats.push_back({{seen.live.x,seen.live.y},member.range,member.group,seen.live.strength,false});
                        danger+=seen.live.strength*w.self.camp_defense_multiplier;
                    }
                }
            }
        }
        observation_fuel_[3]=cambc::fuel_remaining();
        danger+=enemy_danger;
        w.safe_now=w.self.alive&&w.self.hp>=std::max(.2*w.self.max_hp,2*danger)&&
            (enemy_danger<=0||w.self.hp>=.65*w.self.max_hp);
        // Loaded/recovering actors need their own exit before optional source
        // and ally comparisons. Reuse its release proof below in group prices.
        for(int i=0;i<w.altar_count;++i) {
            auto& altar=w.altars[i];altar.travel=bank_travel(w,i,w.self.position);altar.route_known=altar.travel>=0;
        }
        if(w.self.alive)for(auto& source:nav_.sources) {
            auto& g=w.groups[w.group_count++];g.id=handle(source.group);
            const auto& spawns=source.spawn_positions;if(spawns.empty())continue;
            g.position=point(spawns.front());g.gems=source.group.gem_value();g.gold=source.group.gold_value();
            g.observed=std::any_of(source.members.begin(),source.members.end(),[&](const auto& m){return in_view(m.spawn);});
            const bool essential=g.id==target_||g.id==hold_||g.id==exploration_.target||
                source.spawns.is_set(point(w.self.position));
            const bool expand=!recovering_&&w.safe_now&&cambc::fuel_remaining()>=60000;
            if(g.observed&&!essential&&!expand) {
                g.observed=false;++group_deferrals_;
            }
            g.travel=Navigation::steps(source.field.get(),point(w.self.position));
            if(g.travel<0&&(essential||expand))g.travel=nav_.live_steps(source.field.get(),point(w.self.position));
            g.route_known=g.travel>=0;
            g.own_distance=source.own_distance;g.enemy_distance=source.enemy_distance;
            g.approach=point(source.approach);g.to_bank=source.to_bank;
            if(!g.observed){g.hp=source.base_health;g.incoming=source.base_incoming;}
            if(g.observed) {
                // The current original-spawn visibility gate above is the
                // same one required by SDK camp_group::state(). No unused
                // respawn timer or second vision mask is needed here.
                std::uint32_t state{};
                cambc::detail::check(cambc::detail::sdk_camp_group_state(static_cast<std::uint32_t>(g.id),&state));
                g.living=state==1;g.cleared=state==2;g.respawning=state==3;
                if(state==0)throw cambc::error{cambc::error_code::api_not_in_vision,"camp group state is unknown"};
                if(state>3)throw std::runtime_error("unknown engine camp group state");
            }
            if(g.living)for(const auto& member:source.members) {
                const auto& seen=camps[member.index];
                if(seen.dead)continue;
                if(seen.known) {g.hp+=std::max(0.f,seen.live.health);g.incoming+=std::max(0.f,seen.live.strength);}
                else {g.hp+=member.health;g.incoming+=member.strength;}
            }
            g.camp_incoming=g.incoming;
            for(const auto& threat:nav_.threats)if(threat.enemy&&
                distance(threat.position,g.position)<=threat.range) {
                g.contested=true;g.incoming+=threat.damage;
            }
            g.clear_turns=g.cleared?0:static_cast<int>(std::ceil(g.hp/std::max(1.,w.self.strength*w.self.camp_attack_multiplier)))+2;
            g.safe_solo=safe_for(g,w.self);
            if(hold_==g.id) {
                const bool here=source.spawns.is_set(point(w.self.position));
                const bool received=w.self.bag>hold_bag_ || (g.gems==0&&g.gold>0&&
                    w.own_gold>hold_gold_+static_cast<int>(cambc::constants::passive_gold));
                if(hold_last_==round-1&&here&&g.respawning&&received&&!w.own_bank_completed&&
                    cambc::constants::respawn_time>=2&&same_round(round)) {
                    payout_round_=round;payout_group_=g.id;productive_=g.id;hold_=-1;
                } else if(!here||!g.cleared||hold_last_<round-1)hold_=-1;
                else w.capture_active=w.safe_now;
            }
        }
        observation_fuel_[4]=cambc::fuel_remaining();
        w.own_payout=payout_round_==round;w.last_payout_round=payout_round_;w.last_payout_group=payout_group_;
        w.productive_group=hold_>=0?hold_:productive_;
        // Own routes already live in Group/Altar. Optional ally matrices must
        // not consume the reserve needed to act on those observations.
        for(int actor_index=0;actor_index<w.ally_count;++actor_index) {
            if(recovering_||!w.safe_now||cambc::fuel_remaining()<60000){++route_deferrals_;break;}
            const auto& actor=w.allies[actor_index];if(!actor.alive)continue;
            auto& r=w.routes[w.route_count++];r.actor=actor.id;
            for(int i=0;i<w.altar_count;++i)r.altars[i]=bank_travel(w,i,actor.position);
            for(int i=0;i<w.group_count;++i) {
                if(cambc::fuel_remaining()<55000){++route_deferrals_;break;}
                r.groups[i]=nav_.live_steps(nav_.sources[i].field.get(),point(actor.position));
            }
        }
        if((profile_.design==9||profile_.design==10)&&cambc::fuel_remaining()>=60000)derive_certificates(w);
        observation_fuel_[5]=cambc::fuel_remaining();
        if(same_round(round)&&w.enemy_count==1&&!w.extra_opposition) {
            const auto& rival=w.enemies[0];int route=-1;bool occupied=false,other_site=false;
            if(w.self.bag>0) {
                const int bank_id=nearest_bank(w);
                if(bank_id>=0)route=Navigation::steps(nav_.banks[bank_id].field.get(),point(rival.position));
                for(int i=0;i<w.altar_count;++i)if(i!=bank_id&&distance(rival.position,w.altars[i].position)<=cambc::constants::submit_gems_range)other_site=true;
            } else if(auto* primary=nav_.source(target_)) {
                route=Navigation::steps(primary->field.get(),point(rival.position));
                if(const auto* group=w.group(target_);group&&group->cleared)occupied=primary->spawns.is_set(point(rival.position));
                for(auto& other:nav_.sources)if(handle(other.group)!=target_&&other.spawns.is_set(point(rival.position)))other_site=true;
            }
            const bool reach=(rival.can_attack&&distance(rival.position,w.self.position)<=rival.range)||
                (rival.can_cast&&std::min(rival.cooldown[0],rival.cooldown[1])==0&&distance(rival.position,w.self.position)<=rival.threat_range);
            w.response=responses_.classify({rival.id,round,route,rival.position},occupied||reach,other_site);
        } else if(same_round(round))w.response=responses_.classify({},false,false,true);
        if(same_round(round)) {
            damage_.observe(round,w.self.hp,w.self.alive,
                expected_damage_round_==round-1?expected_camp_damage_:0.);
            if(danger<=0&&damage_.recently_hurt(round))w.safe_now=false;
            enemy_scores_[score_cursor_]={round,w.enemy_score};score_cursor_=(score_cursor_+1)%65;score_count_=std::min(65,score_count_+1);
        }
        return w;
    }
    void recover(const World& w) {
#if P9_NAV_DIAGNOSTICS
        NavBudgetTrace measured(diagnostic_reports_,w.round,"recover",-1);
#endif
        auto& me=cambc::self();recovering_=true;
        if(!same_round(w.round)||channel_active())return;
        const auto before=cambc::fuel_remaining();
        if(w.self.at_spawn){nav_.home_dynamic=false;ordinary_shop(w.round);loaded_trace(w,"recover-spawn",before);return;}
        // Prefer one persistent home goal during threatened recovery, then
        // navigation's strictly improving local retreat fallback. Recall
        // still requires quiet damage and no current exposure.
        if(damage_.recently_hurt(w.round)||nav_.exposure(me.position())>0) {
            const bool walked=nav_.walk_home(w.round);
            const bool away=!walked&&nav_.step_away(w.round);
            loaded_trace(w,"recover-home",before,walked,away);
            return;
        }
        if(!me.buffs().has_stun()&&same_round(w.round))me.recall();
        loaded_trace(w,"recover-quiet",before);
    }

    void bank_block(const World& w,int index,std::uint64_t before) {
        if(w.round%64||bank_block_reports_>=16||cambc::fuel_remaining()<8000)return;
        const auto after=cambc::fuel_remaining();
        std::string line="p9 bank-block actor="+std::to_string(w.self.id)+" round="+std::to_string(w.round)+
            " x="+std::to_string(w.self.position.x)+" y="+std::to_string(w.self.position.y)+
            " bag="+std::to_string(w.self.bag)+" index="+std::to_string(index)+
            " fuel="+std::to_string(before)+":"+std::to_string(after)+" routes=";
        for(int i=0;i<w.altar_count;++i)line+=std::to_string(w.altars[i].travel)+":"+
            std::to_string(Navigation::steps(nav_.banks[i].field.get(),point(w.self.position)))+",";
        line+='\n';std::cout<<line;++bank_block_reports_;
    }
    void bank(const World& w,const Directive& d) {
        auto& me=cambc::self();if(w.self.bag<=0)return;
        const auto before_walk=cambc::fuel_remaining();
        const int index=nearest_bank(w,d.altar);if(index<0){bank_block(w,index,before_walk);loaded_trace(w,"bank-no-route",before_walk);return;}
        const auto& a=w.altars[index];
        if(distance(w.self.position,a.position)>cambc::constants::submit_gems_range) {
            const bool walked=nav_.walk_bank(index,w.round,d.strategic);
            if(!walked)bank_block(w,index,before_walk);
            loaded_trace(w,"bank-walk",before_walk,walked);
            return;
        }
        if(d.until>w.round||!a.visible){loaded_trace(w,"bank-wait",before_walk);return;}
        for(const auto& lock:cambc::vision::altar_locks(cambc::team::enemy))if(point(lock.position())==a.position) {
            if(same_round(w.round)&&me.can_attack()&&me.attack_tiles().is_set(lock.position()))me.attack(lock);return;
        }
        if(channel_active()||me.buffs().has_stun()||!same_round(w.round))return;
        const auto before=bank_sample();
        if(before.round!=w.round||!same_round(w.round)){bank_.invalidate();return;}
        me.submit_gems(point(a.position));
        if(same_round(w.round))bank_.begin(before,bank_sample(),w.ticks(before.bag));
    }
    void capture(const World& w,const Directive& d,Navigation::Source& source) {
        auto& me=cambc::self();
        if(d.until>w.round) {
            // Approaching is allowed, but staging must not start capture early.
            if(source.spawns.is_set(me.position()))nav_.walk_dynamic(source.goals,w.round,d.strategic);
            else nav_.walk_source(d.group,w.round,d.strategic);
            return;
        }
        if(!source.spawns.is_set(me.position())) {
            if(!nav_.walk_local(source.spawns,w.round))nav_.walk_dynamic(source.spawns,w.round,d.strategic);
            return;
        }
        if(hold_!=d.group) {hold_=d.group;hold_since_=w.round;hold_retries_=0;}
        if(w.round-hold_since_>w.capture_turns+1) {
            if(hold_retries_++==0)hold_since_=w.round;
            else {hold_=-1;source.inspection_until=w.round+8;nav_.walk_dynamic(source.goals,w.round,d.strategic);return;}
        }
        hold_last_=w.round;hold_bag_=me.gem_count();hold_gold_=static_cast<int>(me.gold_count());
        if constexpr(requires(const World& world){Policy::capture_defense_turns(world);}) {
            // A later blocker cannot renew the initial capture hold window.
            const int budget=Policy::capture_defense_turns(w);
            if(budget<=0||hold_retries_!=0||w.round-hold_since_>=budget||!w.safe_now||
               w.self.bag!=0||w.self.channel!=Channel::none||!w.self.can_move||(!w.self.can_attack&&!w.self.can_cast)||
               !same_round(w.round)||cambc::fuel_remaining()<40000)return;
            const auto origin=me.position();
            if(!same_round(w.round)||point(origin)!=w.self.position)return;
            const Navigation::Threat* blocker=nullptr;
            for(const auto& threat:nav_.threats)
                if(threat.enemy&&distance(threat.position,w.self.position)<=w.self.range&&
                   std::find(source.spawn_positions.begin(),source.spawn_positions.end(),point(threat.position))!=source.spawn_positions.end()) {
                    blocker=&threat;break;
                }
            if(!blocker)return;
            const auto enemy=std::find_if(roster_.begin(),roster_.end(),
                [&](const auto& actor){return handle(actor)==blocker->id;});
            CombatTarget live;
            if(enemy==roster_.end()||!combat_target(*enemy,live)||live.ally||!same_round(w.round))return;
            const auto target_position=live.position;
            if(std::find(source.spawn_positions.begin(),source.spawn_positions.end(),target_position)==source.spawn_positions.end()||
               origin.chebyshev_distance(target_position)>w.self.range||!me.attack_tiles().is_set(target_position))return;
            if(!same_round(w.round)||cambc::fuel_remaining()<35000||!me.can_move())return;
            const double harm=nav_.exposure(origin);bool exit=false;
            for(const auto next:nav_.blocked().reachable_tiles(origin,1).positions())
                if(next!=origin&&competition::safe_progress(harm,nav_.exposure(next))) {exit=true;break;}
            if(!exit||!same_round(w.round)||cambc::fuel_remaining()<35000)return;
            const int target=blocker->id;
            const bool acted=attack_enemy(*enemy,w.round,false,false);
            if constexpr(requires(Policy& policy){policy.report_capture_defense();})
                if(acted&&same_round(w.round)&&cambc::fuel_remaining()>=20000&&policy_.report_capture_defense()) {
                    char text[160];
                    const int count=std::snprintf(text,sizeof text,
                        "p9 capture-defense actor=%d round=%d group=%d target=%d acted=1\n",
                        w.self.id,w.round,d.group,target);
                    if(count>0)std::cout.write(text,std::min(count,static_cast<int>(sizeof text)-1));
                }
        }
    }
    void farm(const World& w,const Directive& d,bool helper=false) {
        auto* source=nav_.source(d.group);const auto* g=w.group(d.group);
        if(!source||!g||g->respawning||!same_round(w.round))return;
        if(g->observed)last_work_round_=w.round;
        const bool committed=partial_clear_&&target_==d.group&&w.round<partial_until_;
        target_=d.group;
        if(!g->observed){nav_.walk_source(d.group,w.round,d.strategic);return;}
        const auto* holder=w.actor(d.actor);
        if(g->cleared) {
            if(helper) {
                // Helpers never manufacture a share or holder. Clear the spawn
                // only for a currently present, individually safe collector.
                const bool here=source->spawns.is_set(cambc::self().position());
                if(ready(holder)&&safe_for(*g,*holder)&&source->spawns.is_set(point(holder->position))) {
                    if(here)nav_.walk_dynamic(source->goals,w.round,d.strategic);
                } else if(here) {
                    // No completed handoff exists. Preserve/finish this safe
                    // actual occupancy as our own fallback, never call it the
                    // absent actor's collection or wait forever for contact.
                    auto own=d;own.actor=-1;own.until=-1;capture(w,own,*source);
                } else if(ready(holder)&&distance(w.self.position,holder->position)>2)
                    nav_.walk_dynamic(around(point(holder->position),2)&~source->spawns,w.round,d.strategic);
                return;
            }
            capture(w,d,*source);return;
        }
        if(d.until>w.round){nav_.walk_source(d.group,w.round,d.strategic);return;}
        if(!g->safe_solo||!safe_for(*g,w.self)) {
            if(!profile_.partial_clears||!competition::partial_clear_step(w,*g,committed,nav_.max_camp_range+1))return;
            if(!committed)partial_until_=w.round+128;
            partial_clear_=true;
        } else partial_clear_=false;
        // A verified affordable fight can damage us before its final kill.
        // Do not mistake that known camp damage for an unseen attacker on
        // the next turn and abandon the freshly cleared capture.
        expected_damage_round_=w.round;
        expected_camp_damage_=std::max(0.,g->camp_incoming)*w.self.camp_defense_multiplier;
        auto& me=cambc::self();std::optional<cambc::camp> target;Point target_position;double best_target=-1e30;
        for(auto c:source->group) {
            if(!same_round(w.round))return;
            if(cambc::fuel_remaining()<35000)break;
            cambc::detail::camp_state live;
            if(read_camp(c,live)&&live.health>0) {
                const int d=distance(Point{live.x,live.y},w.self.position);
                const bool in_range=d<=w.self.range;
                const bool lethal=in_range&&w.self.can_attack&&live.health<=w.self.strength;
                const double value=(lethal?100000.:0)+(in_range?50.:0)+1000*live.strength/(live.health+1)-d;
                if(value>best_target){target=c;if(profile_.pre_move_cast)target_position={live.x,live.y};best_target=value;}
            }
        }
        if(!same_round(w.round))return;
        if(!target){nav_.walk_source(d.group,w.round,d.strategic);return;}
        // Stationary ranged attacks retain their allowance. Query it again after
        // every cast/move/reset instead of assuming melee sequencing.
        const bool in_range=profile_.pre_move_cast?me.attack_tiles().is_set(point(target_position)):
            me.attack_tiles().is_set(target->position());
        if(in_range)fight(*target,false,w.round);
        else {
            if(profile_.pre_move_cast&&cast_camp_before_move(*target,w.self,target_position,w.round))return;
            if(g->travel==0)nav_.walk_dynamic(around(profile_.pre_move_cast?point(target_position):target->position(),std::max(1,w.self.range)),w.round,d.strategic,d.group);
            else nav_.walk_source(d.group,w.round,d.strategic,true);
            if(same_round(w.round)&&target->is_visible()&&target->is_alive())fight(*target,false,w.round,point(me.position())!=w.self.position);
        }
    }
    Directive independent(const World& w) {
        constexpr bool reject_contested_captures=[] {
            if constexpr(requires{Policy::avoid_contested_captures();})return Policy::avoid_contested_captures();
            else return false;
        }();
        // Policies decide how to use earmarked capital before ordinary gear.
        // A purchase gets a fresh coherent actor observation next owner turn.
        if(hold_<0)ordinary_shop(w.round);
        if(purchase_round_==w.round)return {Task::wait,-1,-1,-1,-1,false,"refresh after equipment purchase"};
        const int bank_id=nearest_bank(w);
        const int bank_distance=bank_id>=0?w.altar(bank_id)->travel:-1;
        if(competition::bank_now<reject_contested_captures>(w,bank_distance,profile_.bank_batch))return live::bank(bank_id,"convert carried gems");
        const auto gear=cambc::item_type::monster_shard_of_strength.info();
        if(w.self.type!=Champion::tavi&&!w.self.at_spawn&&w.own_gold>=
           static_cast<int>(gear.cost*std::max(1u,profile_.capital_recall_shards))) {
            if(w.self.bag>0)return live::bank(bank_id,"bank before reinvestment");
            return {Task::recover,-1,-1,-1,-1,false,"reinvest earned gold"};
        }
        if(profile_.partial_clears&&partial_clear_&&w.round<partial_until_) {
            const auto* g=w.group(target_);
            if(g&&!g->respawning&&g->route_known&&(!g->observed||
               (g->living&&competition::partial_clear_step(w,*g,true,nav_.max_camp_range+1))))
                return live::farm(g->id,"resume bounded persistent camp work");
            if(!g||g->cleared||g->respawning)partial_clear_=false;
        }
        int gold=-1;
        if((profile_.bootstrap_pick==-1||w.self.pick==profile_.bootstrap_pick)&&w.self.bag==0&&
           (w.self.camp_attack_multiplier<=1||w.self.camp_defense_multiplier>=1)) {
            gold=competition::gold_source<reject_contested_captures>(w);
            // Establish the first camp emblems before starting another costly
            // gem attempt. Unknown gold is an inspection, never a payout or a
            // claim that its camps are still alive/available outside vision.
            if(const auto* g=w.group(gold);g&&!g->contested) {
                if(const auto* source=nav_.source(gold);source&&source->base_incoming==0) {
                    exploration_.target=-1;
                    return live::farm(gold,"fund first camp emblems from observed harmless gold");
                }
            }
            if(const auto* g=w.group(exploration_.target);g&&g->gems==0&&g->gold>0) {
                if(!g->observed&&exploration_.continue_route(g->id,w.round,g->travel))
                    return {Task::inspect,g->id,-1,-1,-1,false,"continue bounded gold inspection"};
                if(auto* source=nav_.source(g->id))source->inspection_until=w.round+(g->observed?12:24);
                exploration_.target=-1;
            }
            const Group* bootstrap=nullptr;double best=0;
            for(int i=0;i<w.group_count;++i) {
                const auto& g=w.groups[i];
                if(g.observed||g.gems!=0||g.gold<=0||g.contested||!g.route_known||g.travel<0)continue;
                const auto* source=nav_.source(g.id);
                if(!source||source->base_incoming!=0||source->inspection_until>w.round)continue;
                const double work=std::ceil(source->base_health/std::max(1.,w.self.strength*w.self.camp_attack_multiplier));
                const double rate=double(g.gold)/((g.travel+work+w.capture_turns+8.)*
                    (1+competition::nearby_allies(w,g)));
                if(rate>best){best=rate;bootstrap=&g;}
            }
            if(bootstrap) {
                exploration_.start(bootstrap->id,w.round,bootstrap->travel);
                return {Task::inspect,bootstrap->id,-1,-1,-1,false,"inspect harmless gold for first camp emblems"};
            }
        }
        const int gem=competition::gem_source<reject_contested_captures>(w);
        if(gem>=0)return live::farm(gem,"best safe gem throughput");
        if(w.self.bag>0)return live::bank(bank_id);
        if(gold<0)gold=competition::gold_source<reject_contested_captures>(w);
        if(profile_.partial_clears) {
            const Group* partial=nullptr;double best=0;
            for(int i=0;i<w.group_count;++i) {
                const auto& g=w.groups[i];
                if(!competition::partial_clear_step(w,g,false,nav_.max_camp_range+1))continue;
                const double work=g.hp/std::max(1.,w.self.strength*w.self.camp_attack_multiplier);
                // Extra occupants split this payout without accelerating its
                // capture. Spread new ordinary attempts; an existing partial
                // commitment and policy-directed cooperation take precedence.
                const double rate=(2.*g.gems+.4*g.gold)/
                    ((g.travel+work+w.capture_turns+12.)*(1+competition::nearby_allies(w,g)));
                if(rate>best){best=rate;partial=&g;}
            }
            if(partial)return live::farm(partial->id,"bounded camp progress with independent escape reserve");
        }
        if(const auto* g=w.group(exploration_.target);g) {
            if(!g->observed&&exploration_.continue_route(g->id,w.round,g->travel))
                return {Task::inspect,g->id,-1,-1,-1,false,"gem inspection with route progress"};
            if(auto* source=nav_.source(g->id))source->inspection_until=w.round+(g->observed?12:24);
        }
        exploration_.target=-1;
        const Group* prospect=nullptr;
        for(int i=0;i<w.group_count;++i) {
            const auto& g=w.groups[i];auto* source=nav_.source(g.id);
            if(!source||g.observed||g.gems<=0||!g.route_known||g.travel<0||source->inspection_until>w.round)continue;
            auto forecast=g;forecast.observed=true;forecast.living=true;
            if(!safe_for(forecast,w.self)&&!(profile_.partial_clears&&
               competition::partial_clear_step(w,forecast,false,nav_.max_camp_range+1)))continue;
            if(!prospect||competition::prospect_priority(source->priority,g.travel)>
                competition::prospect_priority(nav_.source(prospect->id)->priority,prospect->travel))prospect=&g;
        }
        if(prospect) {
            exploration_.start(prospect->id,w.round,prospect->travel);
            return {Task::inspect,prospect->id,-1,-1,-1,false,"seek reachable gem income"};
        }
        if(gold>=0)return live::farm(gold,"bootstrap equipment from gold");
        for(auto& source:nav_.sources) {
            const auto* g=w.group(handle(source.group));
            if(g&&!g->observed&&g->route_known&&source.inspection_until<=w.round) {
                exploration_.start(g->id,w.round,g->travel);
                return {Task::inspect,g->id,-1,-1,-1,false,"bounded fallback inspection"};
            }
        }
        return {Task::recover,-1,-1,-1,-1,false,"no currently verified safe production"};
    }
    void execute(const World& w,Directive d) {
#if P9_NAV_DIAGNOSTICS
        NavBudgetTrace measured(diagnostic_reports_,w.round,"execute",static_cast<int>(d.task));
#endif
        auto& me=cambc::self();if(!same_round(w.round)||channel_active())return;
        if(cambc::fuel_remaining()<25000){++fuel_skips_;loaded_trace(w,"execute-skip",cambc::fuel_remaining());return;}
        if(d.strategic&&route_blocked_epoch_==w.round/64)d={};
        if(d.task==Task::independent)d=independent(w);
        stage_="execute";executing_=d.task;executing_group_=d.group;
        const auto task=static_cast<unsigned>(d.task);
        if(task<ordinary_decisions_.size())(d.strategic?strategic_decisions_:ordinary_decisions_)[task]++;
        if(decision_epoch_!=w.round/256&&cambc::fuel_remaining()>60000) {
            decision_epoch_=w.round/256;
            std::string line="p9 decisions ["+std::string(profile_.slug)+"] actor="+std::to_string(w.self.id)+
                " round="+std::to_string(w.round)+" fuel="+std::to_string(cambc::fuel_remaining())+
                " fuelSkips="+std::to_string(fuel_skips_)+" ordinary=";
            for(auto count:ordinary_decisions_)line+=std::to_string(count)+',';
            line+=" strategic=";for(auto count:strategic_decisions_)line+=std::to_string(count)+',';
            line+=" observe=";for(auto fuel:observation_fuel_)line+=std::to_string(fuel)+',';
            line+=" deferred="+std::to_string(group_deferrals_)+":"+std::to_string(route_deferrals_);
            line+=" catalog="+std::to_string(nav_.sources.size())+":"+
                std::to_string(std::count_if(nav_.sources.begin(),nav_.sources.end(),[](const auto& source){return source.metadata_ready;}));
            line+='\n';std::cout<<line;
        }
        switch(d.task) {
        case Task::bank:bank(w,d);break;
        case Task::farm:case Task::capture:farm(w,d);break;
        case Task::support:
            if(auto actor=visible_actor(d.actor)) {
                if(d.group>=0)farm(w,d,true);
                else if(distance(w.self.position,point(actor->position()))>2)
                    nav_.walk_dynamic(around(actor->position(),2),w.round,d.strategic);
                // Farming/support travel can remove the ally from personal
                // vision. Reacquire before using any further live property.
                if(!same_round(w.round))break;
                actor=visible_actor(d.actor);if(!actor)break;
                const auto supported_position=point(actor->position());
                for(int i=0;i<w.enemy_count&&same_round(w.round);++i) {
                    const auto& e=w.enemies[i];
                    if(distance(e.position,supported_position)<=e.range+1&&e.hp<=3*w.self.strength&&w.self.hp>3*e.strength)
                        if(auto enemy=visible_actor(e.id)){attack_enemy(*enemy,w.round,false,point(me.position())!=w.self.position);break;}
                }
            } else if(d.group>=0) {
                if(auto* source=nav_.source(d.group);source&&source->spawns.is_set(me.position())) {
                    if(const auto* g=w.group(d.group);g&&g->cleared&&g->safe_solo) {
                        auto own=d;own.task=Task::capture;own.actor=-1;own.until=-1;farm(w,own);
                    }
                } else execute(w,independent(w));
            }break;
        case Task::intercept:
            if(auto enemy=visible_actor(d.actor)) {
                if(distance(w.self.position,point(enemy->position()))>w.self.range)
                    nav_.walk_dynamic(around(enemy->position(),std::max(1,w.self.range)),w.round,d.strategic,-1,d.actor);
                if(!same_round(w.round))break;
                enemy=visible_actor(d.actor);
                if(enemy)attack_enemy(*enemy,w.round,enemy->is_submitting_gems(),point(me.position())!=w.self.position);
            }break;
        case Task::heal:
            if(auto ally=visible_actor(d.actor);ally&&ally->is_ally()&&ally->health()<ally->max_health()) {
                const double hp=ally->health();
                if(cast_target(*ally,true,false,w.round)&&same_round(w.round)&&ally->is_visible()&&ally->health()>hp&&same_round(w.round)) {
                    heal_round_=w.round;heal_actor_=d.actor;heal_amount_=ally->health()-hp;
                }
            }break;
        case Task::buy_defense:shopping(cambc::item_type::durability_shard_of_health,
            d.purchase_target>=0?d.purchase_target:static_cast<int>(owned_shards(me,cambc::item_type::durability_shard_of_health))+1,w.round);break;
        case Task::buy_lock:
            if(!w.has_lock&&me.can_buy(cambc::consumable_type::altar_lock)&&same_round(w.round)) {
                me.buy(cambc::consumable_type::altar_lock);if(same_round(w.round)&&me.consumables()[cambc::consumable_type::altar_lock].count()>0)purchase_round_=w.round;
            }break;
        case Task::place_lock:
            if(const auto* altar=w.altar(d.altar);altar&&w.has_lock&&!altar->enemy_lock&&!altar->friendly_lock) {
                if(distance(w.self.position,altar->position)>2)nav_.walk_bank(altar->id,w.round,d.strategic);
                else if(altar->visible&&!me.restrictions().contains(cambc::buff_restrictions::consumable)&&same_round(w.round))
                    me.consumables()[cambc::consumable_type::altar_lock].use(point(altar->position));
            }break;
        case Task::recover:recover(w);break;
        case Task::inspect:nav_.walk_source(d.group,w.round,d.strategic);break;
        case Task::wait:case Task::independent:break;
        }
        if(nav_.route_failed){route_blocked_epoch_=w.round/64;nav_.route_failed=false;}
    }
public:
    Runtime(const Profile& profile,Arm arm):profile_(profile),arm_(arm){}
    void error_context(std::ostream& out) const {
        out<<" round="<<last_round_<<" stage="<<stage_<<" task="<<static_cast<int>(executing_)<<" group="<<executing_group_;
    }
    void turn() {
        const int round=static_cast<int>(cambc::round_number());auto& me=cambc::self();
        if(!me.is_alive()){nav_.home_dynamic=false;damage_.observe(round,0,false);bank_.invalidate();hold_=-1;recovering_=true;ordinary_shop(round);last_round_=round;return;}
        if(last_round_>=0&&round-last_round_>1){bank_.invalidate();hold_=-1;route_blocked_epoch_=round/64;}
        last_round_=round;
        stage_="prepare";auto fuel=cambc::fuel_remaining();
        // Late catalog preparation may consume half a turn. Current work,
        // loaded travel and recovery take precedence over adding a new field.
        const bool urgent=last_work_round_==round-1||last_threat_round_==round-1||
            hold_>=0||recovering_||channel_active()||me.gem_count()>0;
        const bool at_spawn=nav_.own_spawn&&Navigation::steps(nav_.own_spawn.get(),me.position())==0;
        if(!nav_.initialized||!nav_.own_spawn||!nav_.enemy_spawn||at_spawn||!urgent)nav_.prepare(round,!urgent);
        budget_trace("prepare",round,fuel);
        if(!same_round(round)||!nav_.initialized||cambc::fuel_remaining()<70000)return;
        if(roster_.empty()) {
            if(cambc::fuel_remaining()<80000)return;
            fuel=cambc::fuel_remaining();roster_=cambc::champions();budget_trace("roster",round,fuel);
            if(!same_round(round)||cambc::fuel_remaining()<70000)return;
        }
        stage_="observe";fuel=cambc::fuel_remaining();const auto world=observe(round);budget_trace("observe",round,fuel);
        if(!same_round(round))return;
        loaded_frame(world);
        if(!world.safe_now||world.enemy_count)last_threat_round_=round;
        if(cambc::fuel_remaining()<24000){++fuel_skips_;loaded_trace(world,"observe-skip",fuel);return;}
        if(world.self.channel!=Channel::none)return;
        if(world.capture_active&&world.safe_now) {execute(world,{Task::capture,world.productive_group});return;}
        if(!world.safe_now){recover(world);return;}
        if(partial_clear_&&world.round<partial_until_) {
            if(const auto* group=world.group(target_);group&&group->observed&&group->living&&
               !competition::partial_clear_step(world,*group,true,nav_.max_camp_range+1)) {recover(world);return;}
        }
        if(recovering_) {
            if(!world.self.at_spawn||world.self.hp<.9*world.self.max_hp){recover(world);return;}
            recovering_=false;
            nav_.home_dynamic=false;
        }
#if P9_NAV_DIAGNOSTICS
        stage_="policy";auto directive=[&]{NavBudgetTrace measured(diagnostic_reports_,round,"policy",-1);return policy_.step(world,arm_);}();
#else
        stage_="policy";auto directive=policy_.step(world,arm_);
#endif
        if(directive.task==Task::independent&&!world.immediate_bank()&&world.self.bag<12)
            if(const auto* enemy=favorable_rival(world);enemy&&enemy->bag>=world.self.bag&&
               distance(enemy->position,world.self.position)<=world.self.range)
                directive={Task::intercept,-1,enemy->id,-1,-1,false,"take a nearby favorable gem bounty"};
        if(same_round(round)) {fuel=cambc::fuel_remaining();execute(world,directive);budget_trace("execute",round,fuel);}
    }
};

inline void draft(const Profile& profile) {
    const auto selected=cambc::draft::current_draft(cambc::team::ally);
    for(auto type:profile.draft) {
        auto choice=draft_type(type);
        if(std::find(selected.begin(),selected.end(),choice)==selected.end()){cambc::draft::pick(choice);return;}
    }
}
template<class Policy> int run(const Profile& profile,Arm arm) {
    Runtime<Policy> bot(profile,arm);
    while(true) {
        try {if(cambc::is_draft())draft(profile);else bot.turn();}
        catch(const std::exception& error){std::cout<<"p9 error ["<<profile.slug<<"]: "<<error.what();bot.error_context(std::cout);std::cout<<'\n';}
        catch(...){std::cout<<"p9 error ["<<profile.slug<<"]: unknown exception\n";}
        cambc::end_turn();
    }
}
} // namespace p9::live
