#pragma once
#include "investment.h"

namespace plan4::capital {
enum class Unsupported { none, frame, parity, passive, quote, occupancy, capacity, budget, menu };
struct ProcessEvidence {
    std::array<runtime::ModelPredictionEvent,8> receipts;
    int count=0,work=0;
    Unsupported unsupported=Unsupported::none;
};
// One actor, the complete unchanged menu and its cyclic single-job cursor.
// Work and travel use the same frozen nominal quotes as M0. Investment changes
// only the future effective body/service law, never the inherited job list.
class Process {
    const Information& info;const Menu& menu;const InvestmentFrame& initial;
    Path& path;Forecast& out;ProcessEvidence& evidence;
    std::array<int,8> sources{};
    int last_shop=-1,cursor=0;bool initial_spawn=true;
    bool budget(){++evidence.work;if(!info.work_budget.can_refine||info.work_budget.can_refine(info.work_budget.context))return true;
        evidence.unsupported=Unsupported::budget;out.budget_exhausted=true;path.stopped=true;return false;}
    bool unsupported(Unsupported why){evidence.unsupported=why;path.stopped=true;return false;}
    bool abort(AbortReason why){out.abort_reason=why;path.stopped=true;return false;}
    void receipt(int kind,int group,std::int64_t amount,Phase phase=Phase::owner_action){
        if(evidence.count==8)return;
        evidence.receipts[evidence.count++]={path.now,int(phase),kind,info.own.id,group<0?-1:info.groups[group].id,amount*1000,path.bag};
    }
    bool event(EventKind kind,int start,int group=-1,unsigned bag_before=0,unsigned secured=0,
               Phase end_phase=Phase::owner_action){
        if(out.event_count==max_events){out.truncated=true;path.stopped=true;return false;}
        Event e;e.kind=kind;e.start={start,Phase::owner_action};e.end={path.now,end_phase};
        e.group_id=group<0?-1:info.groups[group].id;e.bag_before=bag_before;e.bag_after=path.bag;e.secured=secured;
        e.health_after=path.health;e.gold_after=path.cash;e.status=Status::predicted;out.events[out.event_count++]=e;return true;
    }
    bool terminal(){if(std::uint64_t(info.our_score)+path.secured>=info.settings.winning_gems){out.terminal_z=1;path.stopped=true;return true;}return false;}
    bool on_spawn()const{return path.position==info.own_spawn||(initial_spawn&&path.position==info.own.position&&info.own.on_spawn);}
    bool shop_now(){
        if(last_shop==path.now)return true;
        last_shop=path.now;
        if(!initial.remote_shop&&!on_spawn())return true;
        if(!budget())return false;
        const int before=path.purchase_count;
        if(!shop(path,{on_spawn(),false,initial.remote_shop,true},initial.definitions))return unsupported(Unsupported::capacity);
        for(int n=before;n<path.purchase_count;++n){const auto&p=path.purchases[n];receipt(100+int(p.family),-1,p.cost);}
        // The inherited policy has three attempts per owner turn, each with a
        // Power100 cap. Huge custom-price/cash states need intra-clear changing
        // service quotes; refuse them rather than delay a purchase silently.
        if(path.equipment.total_shards[0]>=5&&path.equipment.total_shards[1]>=5&&
           path.cash>=initial.definitions.item[2].cost)return unsupported(Unsupported::capacity);
        return true;
    }
    bool advance_turns(int rounds,bool eligible=true){
        if(rounds<0)return unsupported(Unsupported::quote);
        for(int n=0;n<rounds;++n){
            if(!budget())return false;
            if(path.now>=path.deadline){out.truncated=true;return abort(AbortReason::horizon);}
            if(eligible&&!shop_now())return false;
            if(!advance(path,1))return abort(AbortReason::horizon);
        }return true;
    }
    bool travel(Point target){
        if(!budget())return false;
        const int rounds=distance(info,path.position,target);if(rounds<0)return abort(AbortReason::unknown_travel);
        if(target==path.position)return true;
        const int start=path.now;
        if(!shop_now())return false;
        for(int g=0;g<path.stock_count;++g)if(path.stock[g].owned_spawn){path.stock[g].owned_spawn=false;
            path.stock[g].ready_round=std::max(path.stock[g].ready_round,path.now+1);}
        initial_spawn=false;path.position={-1,-1}; // No intermediate tile is asserted from a scalar distance.
        if(!advance_turns(rounds))return false;
        path.position=target;advance(path,0);return event(EventKind::travel,start);
    }
    int local(int index)const{for(int n=0;n<path.stock_count;++n)if(sources[n]==index)return n;return-1;}
    bool available(const Job&job)const{
        const int g=local(job.group);if(g<0)return false;const auto&s=path.stock[g];
        return s.assured&&!s.owned_spawn&&!info.groups[job.group].contested&&
            ((s.lifecycle!=Lifecycle::captured&&s.lifecycle!=Lifecycle::respawning)||s.ready_round>=0);
    }
    bool harvest(int index){
        if(!budget())return false;
        const int g=local(index);if(g<0)return abort(AbortReason::unknown_stock);
        auto&s=path.stock[g];const auto&source=info.groups[index];
        if(!s.assured){out.stock_unknown=true;return abort(AbortReason::unknown_stock);}
        if(source.contested)return abort(AbortReason::contested);
        if(s.lifecycle==Lifecycle::captured||s.lifecycle==Lifecycle::respawning){
            if(s.owned_spawn||source.occupied_spawn||path.position==s.spawn||s.ready_round<0){out.stock_unknown=true;return abort(AbortReason::unknown_stock);}
            const int wait=std::max(0,s.ready_round-path.now),start=path.now;
            if(!advance_turns(wait))return false;
            s.lifecycle=Lifecycle::living;s.current_health=s.full_health;
            if(wait&&!event(EventKind::wait,start,index))return false;
        }
        if(!travel(s.spawn)||!shop_now())return false;
        if(s.lifecycle!=Lifecycle::cleared){
            const float ratio=path.effective.camp_incoming/initial.stats.camp_incoming;
            const float incoming=s.unmodified_incoming*ratio;
            auto stats=path.effective;stats.camp_incoming=1;
            const auto quote=conservative_combat(stats,path.health,s.current_health,incoming);
            if(!quote.supported)return unsupported(Unsupported::quote);
            if(!quote.affordable)return abort(AbortReason::unsafe_clear);
            const int start=path.now;
            if(!advance_turns(quote.rounds))return false;
            path.health-=quote.loss;s.current_health=0;s.lifecycle=Lifecycle::cleared;
            if(!event(EventKind::clear,start,index))return false;
            receipt(int(EventKind::clear),index,quote.rounds);
        }
        if(!s.occupancy_known)return unsupported(Unsupported::occupancy);
        const unsigned gems=allocated_reward(s.gems,s.eligible_allied_occupants,s.own_is_lowest_occupant);
        const unsigned gold=allocated_reward(s.gold,s.eligible_allied_occupants,s.own_is_lowest_occupant);
        if(gems>std::numeric_limits<unsigned>::max()-path.bag||gold>std::numeric_limits<unsigned>::max()-path.cash)return unsupported(Unsupported::quote);
        if(source.occupied_spawn&&(source.eligible_allied_occupants!=1||info.own.position!=source.position))return unsupported(Unsupported::occupancy);
        const int start=path.now;const bool known_current=path.now==info.round&&source.capture_started_round>=0&&info.own.position==source.position;
        const std::int64_t finish=known_current?std::max(std::int64_t(path.now)+1,std::int64_t(source.capture_started_round)+info.settings.capture_rounds):
            std::int64_t(path.now)+1+info.settings.capture_rounds;
        if(finish>path.deadline)return advance_turns(path.deadline-path.now+1);
        if(!advance_turns(int(finish-path.now)))return false;
        if(out.event_count==max_events){out.truncated=true;return abort(AbortReason::event_capacity);}
        const unsigned before=path.bag;path.bag+=gems;path.cash+=gold;s.lifecycle=Lifecycle::captured;s.owned_spawn=true;
        s.ready_round=int(std::min<std::int64_t>(path.deadline+1,std::int64_t(path.now)+s.respawn_rounds));s.unmodified_incoming=source.full_incoming_damage;
        if(!event(EventKind::capture,start,index,before,0,Phase::capture))return false;
        receipt(int(EventKind::capture),index,gems,Phase::capture);return shop_now();
    }
    bool bank(int altar,bool existing=false){
        if(!existing&&(altar<0||altar>=info.altar_count||!info.altars[altar].accessible))return abort(AbortReason::inaccessible);
        if(!existing){if(!travel(info.altars[altar].position))return false;const auto&a=info.altars[altar];out.lock_unknown|=!a.lock_known;
            if(a.hostile_lock){if(a.remove_lock_rounds<1)return abort(AbortReason::inaccessible);const int start=path.now;
                if(!advance_turns(a.remove_lock_rounds)||!event(EventKind::remove_lock,start))return false;}}
        if(!shop_now())return false;
        if(!path.bag){out.no_conversion=true;return true;}
        const int ticks=existing?std::max(0,info.own.channel_ticks):tribute_ticks(path.bag,info.settings),start=path.now;
        const unsigned amount=path.bag;
        if(!advance_turns(ticks,false))return false;
        if(out.event_count==max_events){out.truncated=true;return abort(AbortReason::event_capacity);}
        if(amount>std::numeric_limits<unsigned>::max()-path.secured)return unsupported(Unsupported::quote);
        path.bag=0;path.secured+=amount;if(!out.completed_conversion)out.first_conversion_round=path.now;out.completed_conversion=true;
        if(!event(EventKind::bank,start,-1,amount,amount,Phase::owner_start))return false;
        receipt(int(EventKind::bank),-1,amount,Phase::owner_start);terminal();return true;
    }
    bool renew(bool existing=false){
        if(!shop_now())return false;
        if(path.health>=path.effective.maximum_health&&!existing){const int start=path.now;return advance_turns(1)&&event(EventKind::wait,start);}
        if(!on_spawn()){
            if(!existing&&!info.own.recall_safe)return abort(AbortReason::unsafe_recall);
            const int start=path.now;
            if(!existing){const float loss=info.own.escape_incoming*float(info.own.escape_rounds+1);
                if(path.health-loss<=0)return abort(AbortReason::unsafe_recall);
                if(!advance_turns(info.own.escape_rounds))return false;
                path.health-=loss;
                if(!event(EventKind::escape,start))return false;}
            if(!advance_turns(existing?std::max(0,info.own.channel_ticks):info.settings.recall_rounds,false))return false;
            initial_spawn=false;path.position=info.own_spawn;
            for(int g=0;g<path.stock_count;++g)if(path.stock[g].owned_spawn){path.stock[g].owned_spawn=false;path.stock[g].ready_round=std::max(path.stock[g].ready_round,path.now+1);}
            if(!event(EventKind::recall_arrival,start,-1,0,0,Phase::owner_start)||!shop_now())return false;
        }
        const float healing=path.effective.maximum_health*info.settings.spawn_heal_fraction;
        if(!std::isfinite(healing)||healing<=0)return abort(AbortReason::unsafe_recall);
        while(path.health<path.effective.maximum_health){const int start=path.now;
            if(!advance_turns(1))return false;
            path.health=std::min(path.effective.maximum_health,path.health+healing);
            if(!event(EventKind::heal,start,-1,0,0,Phase::owner_end))return false;}
        return true;
    }
    bool run_job(const Job&job){
        if(!budget()||path.stopped)return false;
        ++out.macros_started;
        if(job.kind==JobKind::farm){if(!harvest(job.group))return false;if(job.second_group>=0&&!harvest(job.second_group))return false;return bank(job.altar);}
        if(job.kind==JobKind::bank)return bank(job.altar);
        if(job.kind==JobKind::renew)return renew();
        if(out.macros_started==1){
            if(info.own.channel==Channel::tribute)return bank(-1,true);
            if(info.own.channel==Channel::recall)return renew(true);
            if(info.own.channel==Channel::other)return abort(AbortReason::unsupported_channel);
            for(int n=0;n<path.stock_count;++n)if(info.groups[sources[n]].id==info.in_progress_group_id&&path.stock[n].lifecycle==Lifecycle::cleared&&
                path.position==path.stock[n].spawn)return harvest(sources[n]);
        }
        const int start=path.now;return advance_turns(1)&&event(EventKind::wait,start);
    }
public:
    Process(const Information&i,const Menu&m,const InvestmentFrame&frame,Path&p,Forecast&f,ProcessEvidence&e)
        :info(i),menu(m),initial(frame),path(p),out(f),evidence(e){}
    bool run(const Job& first,bool enabled){
        out.reset();evidence.count=evidence.work=0;evidence.unsupported=Unsupported::none;
        if(!begin(path,info.round,horizon(info),info.own.position,info.own.health,info.own.gold,info.own.bag,initial.stats,initial.equipment,initial.definitions,enabled))return unsupported(Unsupported::frame);
        last_shop=info.round;out.first_conversion_round=path.deadline+1;
        for(int n=0;n<menu.single_count;++n){if(!budget())return false;const int index=menu.jobs[n].group;
            if(index<0||index>=info.group_count)return unsupported(Unsupported::menu);
            if(local(index)>=0)continue;
            const auto&g=info.groups[index];Stock s;s.id=g.id;s.spawn=g.position;s.full_health=g.full_health;
            const bool fresh=g.visible&&g.provenance.observed_round==info.round&&g.provenance.status==Status::exact_now&&g.provenance.source==Source::personal_vision;
            const bool hp=fresh&&g.health_provenance.observed_round==info.round&&g.health_provenance.status==Status::exact_now&&g.health_provenance.source==Source::personal_vision;
            s.current_health=hp?g.health:g.full_health;s.unmodified_incoming=hp&&g.lifecycle==Lifecycle::living?g.incoming_damage:g.full_incoming_damage;
            s.lifecycle=fresh?g.lifecycle:Lifecycle::unknown;s.assured=fresh&&g.lifecycle!=Lifecycle::unknown;
            if(g.lifecycle==Lifecycle::cleared&&!g.reward_available)s.assured=false;
            s.gems=g.gems;s.gold=g.gold;s.respawn_rounds=g.respawn_rounds;
            s.ready_round=g.respawn_remaining<0?-1:int(std::min<std::int64_t>(path.deadline+1,std::int64_t(info.round)+g.respawn_remaining));
            s.occupancy_known=g.occupancy_known;s.eligible_allied_occupants=g.eligible_allied_occupants;s.own_is_lowest_occupant=g.own_is_lowest_occupant;
            sources[path.stock_count]=index;if(!add_stock(path,s))return unsupported(Unsupported::quote);
        }
        if(info.our_score>=info.settings.winning_gems||info.enemy_score>=info.settings.winning_gems){
            if(info.our_score>=info.settings.winning_gems&&info.enemy_score>=info.settings.winning_gems)out.terminal_unknown=true;
            else out.terminal_z=info.our_score>=info.settings.winning_gems?1:-1;
            path.stopped=true;}
        if(!path.stopped&&path.now<path.deadline)run_job(first);
        while(!path.stopped&&path.now<path.deadline&&out.macros_started<max_macros&&menu.single_count){
            const auto eligible=[](const Job&job,const void*p){return static_cast<const Process*>(p)->available(job);};
            const int selected=next_fixed_job(menu,path.now,path.deadline,out.macros_started,cursor,eligible,this);
            if(selected<0)break;
            if(!run_job(menu.jobs[selected]))break;
        }
        out.final_round=path.now;out.final_health=path.health;out.final_bag=path.bag;out.final_gold=path.cash;out.our_secured=path.secured;
        out.truncated|=info.observation_truncated||info.public_layout_truncated||menu.truncated||(!path.stopped&&out.macros_started==max_macros&&path.now<path.deadline);
        if(!out.terminal_z&&path.deadline==info.settings.max_rounds)out.terminal_unknown=true;
        return evidence.unsupported==Unsupported::none&&!out.budget_exhausted;
    }
};
} // namespace plan4::capital
