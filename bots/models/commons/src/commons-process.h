#pragma once
// Private SDK-free chronological process. Include through commons.h.
namespace plan4::commons {
struct Actor {int id=-1;Point position{};float health=0,maximum_health=0;unsigned bag=0,gold=0;};
struct Enemy {
    Actor actor{};Provenance provenance{};
    bool personally_visible=false,entry_phase_supported=false;
    int next_action_round=-1,cursor=0;
};
enum class Leg {acquire,bank,renew,hold};
struct LegRequest {
    Actor actor{};Stock stock{};TravelLookup travel{};Leg kind=Leg::hold;
    int group=-1,altar=-1,start=-1;
    bool enemy=false,current_capture=false,existing_channel=false;
    int channel_ticks=0;
};
// Every endpoint is physical. Acquire's endpoint is the common public group
// anchor; a missed entry uses failure_position. No quote can issue reward.
// Departure is an explicitly supported owner-action bound. Until one exists,
// a captured spawn blocks replenishment; finishing off-spawn supplies only a
// conservative departure-by-completion bound, never an earlier release.
struct LegPlan {
    int entry_round=-1,completion_round=-1,departure_round=-1;
    Phase completion_phase=Phase::owner_action;
    Point end_position{},failure_position{};
    float entry_health=0,end_health=0,failure_health=0;
    bool supported=false,departure_supported=false,recall_safe=false,failure_supported=false;
    int escape_rounds=0;float escape_damage=0;
};
struct Input {
    const Information* info=nullptr;const Menu* menu=nullptr;Job first{};
    Enemy enemy{};const Priority* priority=nullptr;
    bool sole_producers_verified=false,order_known=false;
    // Rival walkability differs on team spawn regions. Never silently borrow
    // the owner's terrain field for the fixture's choices or physical quotes.
    TravelLookup enemy_travel{};
    // A fresh, explicit scheduling assumption, including any rank/tie premise.
    // Pair order need not alternate when the two actors have different ranks.
    bool (*order_at)(int round,bool& own_before_enemy,const void*)=nullptr;
    const void* order_context=nullptr;
    bool (*quote_leg)(const LegRequest&,const Information&,LegPlan&,const void*)=nullptr;
    const void* quote_context=nullptr;
};
enum class Kind {acquire,convert,missed_acquisition,wake,finish};
struct Event {Kind kind=Kind::wake;int round=-1,group_id=-1;bool enemy=false;unsigned amount=0,bag_after=0;Status status=Status::predicted;Phase phase=Phase::owner_action;};
enum class Unsupported {none,policy,enemy,stock,job,quote,contested,order,budget};
struct Result {
    std::array<Event,event_limit> events{};
    int event_count=0,first_conversion_round=0,terminal_z=0,final_round=0,enemy_displaced_attempts=0,own_macros_started=0;
    unsigned our_secured=0,enemy_secured=0,final_bag=0,enemy_final_bag=0,final_gold=0;
    float final_health=0;Point final_position{},enemy_final_position{};
    bool supported=false,truncated=false,stock_unknown=false,terminal_unknown=false,future_stock_predicted=false;
    Unsupported unsupported=Unsupported::none;
};
namespace detail {
enum class PendingKind {none,wake,entry,capture,bank,renew,hold};
struct Pending {PendingKind kind=PendingKind::none;Moment at{-1,Phase::owner_action};LegRequest request{};LegPlan plan{};int generation=-1;};
struct Holding {int group=-1,generation=-1,cooldown_end=-1,departure=-1;};
class Simulation {
    const Input& input;const Information& info;const Menu& menu;Result& out;bool shared;
    std::array<Stock,max_groups> own_stock,enemy_stock;
    std::array<Actor,2> actors{};std::array<Pending,2> pending{};std::array<Holding,2> holding{};
    Job own_job{};int deadline=0,enemy_cursor=0,own_cursor=0,pair_stage=0;bool stopped=false,hold_capture=false,enemy_started=false;
    Stock* stock(int side){return side&&!shared?enemy_stock.data():own_stock.data();}
    bool fail(Unsupported reason){if(out.unsupported==Unsupported::none)out.unsupported=reason;out.supported=false;stopped=true;return false;}
    bool budget(){return can_refine(info)||fail(Unsupported::budget);}
    bool space(){if(out.event_count<event_limit)return true;out.truncated=true;stopped=true;return false;}
    bool emit(Kind kind,Moment at,int side,int group,unsigned amount=0){
        if(!space())return false;
        out.events[out.event_count++]={kind,at.round,group<0?-1:info.groups[group].id,bool(side),amount,actors[side].bag,Status::predicted,at.phase};
        out.final_round=at.round;return true;
    }
    bool available(int side,int g,bool future,int now)const{
        const auto& token=(side&&!shared?enemy_stock:own_stock)[g];
        if(!token.supported||token.ready_round<0||(!future&&token.ready_round>now))return false;
        const auto& held=holding[side];return held.group!=g||held.departure>=0;
    }
    void release_by(int side,int round){
        auto& held=holding[side];if(held.group<0)return;
        auto& token=stock(side)[held.group];
        held.departure=round;
        if(held.cooldown_end>=0&&token.generation==held.generation){token.ready_round=int(std::min<std::int64_t>(deadline+1,
            std::max<std::int64_t>(held.cooldown_end,std::int64_t(round)+1)));token.supported=true;}
    }
    void position(int side,Point p,int now){
        auto& held=holding[side];
        if(held.group>=0&&p!=info.groups[held.group].position){if(held.departure<0||held.departure>now)release_by(side,now);held={};}
        actors[side].position=p;
    }
    bool valid_health(float value,float maximum)const{return std::isfinite(value)&&value>0&&value<=maximum;}
    int travel(int side,Point a,Point b){if(!budget())return-1;const auto lookup=side?input.enemy_travel:info.travel;
        const int d=lookup.distance?lookup.distance(a,b,lookup.context):-1;return budget()?d:-1;}
    bool quote(LegRequest request){
        if(!budget())return false;
        if(request.start>deadline){out.truncated=true;pending[request.enemy?1:0]={};return true;}
        const int side=request.enemy?1:0;request.actor=actors[side];request.travel=side?input.enemy_travel:info.travel;LegPlan plan;
        const bool quoted=input.quote_leg(request,info,plan,input.quote_context);
        if(!budget())return false;
        if(!quoted||!plan.supported)return fail(Unsupported::quote);
        if(plan.entry_round<request.start||plan.completion_round<plan.entry_round
            ||!valid_health(plan.entry_health,request.actor.health)||!valid_health(plan.end_health,request.actor.maximum_health)
            ||(plan.departure_supported&&(plan.departure_round<request.start||plan.departure_round>plan.completion_round)))return fail(Unsupported::quote);
        Pending next;next.request=request;next.plan=plan;next.generation=request.stock.generation;
        if(request.kind==Leg::acquire){
            const auto& group=info.groups[request.group];
            const auto minimum=request.current_capture?std::max<std::int64_t>(std::int64_t(request.start)+1,
                std::int64_t(group.capture_started_round)+info.settings.capture_rounds):std::int64_t(plan.entry_round)+1+info.settings.capture_rounds;
            const int d=travel(side,request.actor.position,group.position);
            if(d<0||plan.entry_round-request.start<d||plan.completion_round<minimum||plan.completion_phase!=Phase::capture
                ||plan.end_position!=group.position||plan.end_health>plan.entry_health)return fail(Unsupported::quote);
            if(plan.failure_supported){const int failed_travel=travel(side,request.actor.position,plan.failure_position);
                if(!valid_health(plan.failure_health,request.actor.health)||plan.failure_position==group.position||failed_travel<0
                    ||failed_travel>plan.entry_round-request.start)return fail(Unsupported::quote);}
            next.kind=PendingKind::entry;next.at={plan.entry_round,Phase::owner_action};
        }else if(request.kind==Leg::bank){
            const Point target=request.existing_channel?request.actor.position:info.altars[request.altar].position;
            const int d=travel(side,request.actor.position,target);
            const int ticks=request.existing_channel?request.channel_ticks:tribute_ticks(request.actor.bag,info.settings);
            if(d<0||plan.entry_round-request.start<d||plan.end_position!=target||plan.end_health>plan.entry_health
                ||plan.completion_round-plan.entry_round<ticks||plan.completion_phase!=(request.actor.bag?Phase::owner_start:Phase::owner_action))return fail(Unsupported::quote);
            next.kind=PendingKind::bank;next.at={plan.completion_round,plan.completion_phase};
        }else if(request.kind==Leg::hold){
            if(plan.completion_round<=request.start||plan.end_position!=request.actor.position||plan.end_health!=request.actor.health||plan.entry_health!=request.actor.health
                ||plan.completion_phase!=Phase::owner_action)return fail(Unsupported::quote);
            next.kind=PendingKind::hold;next.at={plan.completion_round,plan.completion_phase};
        }else{
            if(side)return fail(Unsupported::quote);
            const bool full=request.actor.health>=request.actor.maximum_health;
            const bool on_spawn=request.actor.position==info.own_spawn||(request.start==info.round&&request.actor.position==info.own.position&&info.own.on_spawn);
            if(full){if(plan.completion_round<=request.start||plan.end_health!=request.actor.health||plan.entry_health!=request.actor.health||plan.end_position!=request.actor.position)return fail(Unsupported::quote);}
            else{
                const float recovery=request.actor.maximum_health*info.settings.spawn_heal_fraction;
                if(!std::isfinite(recovery)||recovery<=0||plan.end_health!=request.actor.maximum_health)return fail(Unsupported::quote);
                if(on_spawn){if(plan.entry_round!=request.start||plan.end_position!=request.actor.position||plan.entry_health!=request.actor.health)return fail(Unsupported::quote);}
                else{
                    const int recall=request.existing_channel?request.channel_ticks:info.settings.recall_rounds;
                    if(!plan.recall_safe||plan.escape_rounds<0||plan.escape_rounds>max_horizon||!std::isfinite(plan.escape_damage)||plan.escape_damage<0
                        ||plan.escape_damage>=request.actor.health||plan.entry_health!=request.actor.health-plan.escape_damage
                        ||std::int64_t(plan.entry_round)-request.start<std::int64_t(plan.escape_rounds)+recall||plan.end_position!=info.own_spawn)return fail(Unsupported::quote);
                }
                const double healing=std::ceil((double(request.actor.maximum_health)-plan.entry_health)/recovery);
                if(!std::isfinite(healing)||healing>std::numeric_limits<int>::max()||plan.completion_round-plan.entry_round<healing)return fail(Unsupported::quote);
            }
            if(plan.completion_phase!=Phase::owner_action)return fail(Unsupported::quote);
            next.kind=PendingKind::renew;next.at={plan.completion_round,plan.completion_phase};
        }
        auto& held=holding[side];
        if(held.group>=0&&held.cooldown_end>=0){
            if(plan.departure_supported&&plan.end_position!=info.groups[held.group].position)release_by(side,plan.departure_round);
            else{stock(side)[held.group].ready_round=-1;out.stock_unknown=true;}
        }
        pending[side]=next;return true;
    }
    bool acquire(int side,int group,int now,bool current=false){
        if(group<0||group>=info.group_count||!available(side,group,true,now)){out.stock_unknown=true;return fail(Unsupported::stock);}
        const auto token=stock(side)[group];const int start=std::max(now,token.ready_round);
        if(start>now&&actors[side].position==info.groups[group].position){out.stock_unknown=true;return fail(Unsupported::stock);}
        out.future_stock_predicted|=token.regenerated;
        LegRequest request;request.kind=Leg::acquire;request.group=group;request.start=start;request.enemy=side;request.stock=token;
        request.current_capture=current||(!side&&start==info.round&&actors[side].position==info.groups[group].position
            &&info.groups[group].lifecycle==Lifecycle::cleared&&info.groups[group].capture_started_round>=0);
        return quote(request);
    }
    bool bank(int side,int altar,int now,bool existing=false){
        if(!budget())return false;
        if(!existing&&(altar<0||altar>=info.altar_count||!info.altars[altar].accessible))return fail(Unsupported::quote);
        LegRequest request;request.kind=Leg::bank;request.altar=altar;request.start=now;request.enemy=side;request.existing_channel=existing;
        request.channel_ticks=existing?info.own.channel_ticks:0;return quote(request);
    }
    bool start_own(const Job& job,int now){
        if(now>deadline)return true;
        own_job=job;pair_stage=0;hold_capture=false;++out.own_macros_started;
        if(job.kind==JobKind::farm)return acquire(0,job.group,now);
        if(job.kind==JobKind::bank)return bank(0,job.altar,now);
        LegRequest request;request.start=now;request.kind=job.kind==JobKind::renew?Leg::renew:Leg::hold;
        if(job.kind==JobKind::hold&&out.own_macros_started==1){
            if(info.own.channel==Channel::tribute)return bank(0,-1,now,true);
            if(info.own.channel==Channel::recall){request.kind=Leg::renew;request.existing_channel=true;request.channel_ticks=info.own.channel_ticks;}
            else if(info.own.channel==Channel::other)return fail(Unsupported::job);
            else for(int g=0;g<info.group_count;++g)if(info.groups[g].id==info.in_progress_group_id&&info.groups[g].lifecycle==Lifecycle::cleared
                &&info.groups[g].capture_started_round>=0&&actors[0].position==info.groups[g].position){hold_capture=true;return acquire(0,g,now,true);}
        }
        return quote(request);
    }
    bool next_own(int now){
        const auto eligible=[](const Job& job,const void* context){const auto& self=*static_cast<const Simulation*>(context);
            return job.group>=0&&job.group<self.info.group_count&&self.available(0,job.group,true,0);};
        const int selected=next_fixed_job(menu,now,deadline,out.own_macros_started,own_cursor,eligible,this);
        return selected<0||start_own(menu.jobs[selected],now);
    }
    bool next_enemy(int now){
        if(now>deadline)return true;
        if(!enemy_started){enemy_started=true;if(actors[1].bag)return bank(1,fixture_altar(info,actors[1].position,input.enemy_travel),now);}
        const auto choice=fixture_next(*input.priority,enemy_cursor,stock(1),info.group_count,now);
        if(choice.group>=0){enemy_cursor=choice.next_cursor;return acquire(1,choice.group,now);}
        int wake=std::numeric_limits<int>::max();
        for(int g=0;g<info.group_count;++g)if(available(1,g,true,now)&&stock(1)[g].ready_round>now)wake=std::min(wake,stock(1)[g].ready_round);
        if(wake<=deadline){pending[1].kind=PendingKind::wake;pending[1].at={wake,Phase::round_respawn};}return true;
    }
    bool next(int side,int now){return side?next_enemy(now):next_own(now);}
    int select(){
        int chosen=-1;
        for(int side=0;side<2;++side)if(pending[side].kind!=PendingKind::none){
            if(chosen<0||pending[side].at.round<pending[chosen].at.round){chosen=side;continue;}
            if(pending[side].at.round!=pending[chosen].at.round)continue;
            const bool global=int(pending[side].at.phase)<int(Phase::owner_start),other_global=int(pending[chosen].at.phase)<int(Phase::owner_start);
            if(global!=other_global){if(global)chosen=side;continue;}
            if(global){if(before(pending[side].at,pending[chosen].at))chosen=side;continue;}
            bool own_first=false;if(!input.order_at(pending[side].at.round,own_first,input.order_context)){fail(Unsupported::order);return-1;}
            if(!budget())return-1;
            if((side==0)==own_first)chosen=side;
        }
        return chosen;
    }
    bool process(int side,Pending event){
        const int group=event.request.group,now=event.at.round;
        if(event.kind==PendingKind::wake){if(!emit(Kind::wake,event.at,side,-1))return false;
            if(out.event_count==event_limit){out.truncated=stopped=true;return true;}return next_enemy(now);}
        if(event.kind==PendingKind::entry||event.kind==PendingKind::capture){
            auto& token=stock(side)[group];
            if(!token.supported||token.ready_round<0||token.ready_round>now||token.generation!=event.generation){
                if(event.kind==PendingKind::capture)return fail(Unsupported::stock);
                if(!event.plan.failure_supported)return fail(Unsupported::quote);
                if(!space())return false;
                position(side,event.plan.failure_position,now);actors[side].health=event.plan.failure_health;
                if(side)++out.enemy_displaced_attempts;
                if(!emit(Kind::missed_acquisition,event.at,side,group))return false;
                if(out.event_count==event_limit){out.truncated=stopped=true;return true;}
                return next(side,now+1);
            }
            const auto& other=holding[1-side];
            if(shared&&other.group==group&&(other.departure<0||other.departure>=now))return fail(Unsupported::contested);
            if(event.kind==PendingKind::entry){
                position(side,info.groups[group].position,now);actors[side].health=event.plan.entry_health;
                holding[side]={group,event.generation,-1,-1};event.kind=PendingKind::capture;event.at={event.plan.completion_round,Phase::capture};pending[side]=event;return true;
            }
            const auto& source=info.groups[group];
            if(source.gems>std::numeric_limits<unsigned>::max()-actors[side].bag||source.gold>std::numeric_limits<unsigned>::max()-actors[side].gold)return fail(Unsupported::quote);
            if(!space())return false;
            actors[side].health=event.plan.end_health;actors[side].bag+=source.gems;actors[side].gold+=source.gold;
            ++token.generation;token.regenerated=true;token.ready_round=-1;
            const int cooldown=int(std::min<std::int64_t>(deadline+1,std::int64_t(now)+source.respawn_rounds));holding[side]={group,token.generation,cooldown,-1};
            if(!emit(Kind::acquire,event.at,side,group,source.gems))return false;
            if(out.event_count==event_limit){out.truncated=stopped=true;return true;}
            if(!side&&hold_capture)return next_own(now);
            if(!side&&own_job.kind==JobKind::farm&&!pair_stage&&own_job.second_group>=0){pair_stage=1;return acquire(0,own_job.second_group,now);}
            return bank(side,side?fixture_altar(info,actors[side].position,input.enemy_travel):own_job.altar,now);
        }
        if(!space())return false;
        position(side,event.plan.end_position,now);actors[side].health=event.plan.end_health;
        if(event.kind==PendingKind::bank&&actors[side].bag){
            const unsigned amount=actors[side].bag;auto& secured=side?out.enemy_secured:out.our_secured;
            if(amount>std::numeric_limits<unsigned>::max()-secured)return fail(Unsupported::quote);
            actors[side].bag=0;secured+=amount;
            if(!side&&out.first_conversion_round==deadline+1)out.first_conversion_round=now;
            if(!emit(Kind::convert,event.at,side,-1,amount))return false;
            if(std::uint64_t(side?info.enemy_score:info.our_score)+secured>=info.settings.winning_gems){out.terminal_z=side?-1:1;stopped=true;return true;}
        }else if(!emit(Kind::finish,event.at,side,-1))return false;
        if(out.event_count==event_limit){out.truncated=stopped=true;return true;}
        return next(side,now);
    }
public:
    Simulation(const Input& in,bool coupling,Result& result):input(in),info(*in.info),menu(*in.menu),out(result),shared(coupling){}
    void run(){
        deadline=info.round+horizon(info);out.first_conversion_round=deadline+1;out.final_round=info.round;
        actors[0]={info.own.id,info.own.position,info.own.health,info.own.maximum_health,info.own.bag,info.own.gold};actors[1]=input.enemy.actor;enemy_cursor=input.enemy.cursor;
        for(int g=0;g<info.group_count;++g){
            if(!budget())return;
            const auto& source=info.groups[g];Stock token;
            const bool fresh=source.visible&&source.provenance.observed_round==info.round&&source.provenance.status==Status::exact_now
                &&source.provenance.source==Source::personal_vision&&source.occupancy_known&&!source.contested;
            if(fresh&&(source.lifecycle==Lifecycle::living||(source.lifecycle==Lifecycle::cleared&&source.reward_available)))token={info.round,0,true,false};
            else if(fresh&&(source.lifecycle==Lifecycle::respawning||source.lifecycle==Lifecycle::captured)&&source.respawn_remaining>=0&&!source.occupied_spawn)
                token={int(std::min<std::int64_t>(deadline+1,std::int64_t(info.round)+source.respawn_remaining)),0,true,true};
            else out.stock_unknown=true;
            own_stock[g]=enemy_stock[g]=token;
        }
        out.supported=true;
        out.truncated=info.observation_truncated||menu.truncated;
        if(info.our_score>=info.settings.winning_gems||info.enemy_score>=info.settings.winning_gems){
            if(info.our_score>=info.settings.winning_gems&&info.enemy_score>=info.settings.winning_gems)out.terminal_unknown=true;
            else out.terminal_z=info.our_score>=info.settings.winning_gems?1:-1;
            finish();return;}
        if(info.round>=deadline){finish();return;}
        if(!start_own(input.first,info.round)||!next_enemy(input.enemy.next_action_round)){finish();return;}
        while(!stopped&&out.event_count<event_limit){
            if(!budget())break;
            const int side=select();if(side<0)break;const auto event=pending[side];
            if(event.at.round>deadline){out.truncated=true;break;}
            pending[side]={};if(!process(side,event))break;
        }
        if(out.event_count==event_limit&&(pending[0].kind!=PendingKind::none||pending[1].kind!=PendingKind::none))out.truncated=true;
        finish();
    }
    void finish(){out.final_bag=actors[0].bag;out.enemy_final_bag=actors[1].bag;out.final_health=actors[0].health;
        out.final_gold=actors[0].gold;out.final_position=actors[0].position;out.enemy_final_position=actors[1].position;
        if(!out.terminal_z&&deadline==info.settings.max_rounds)out.terminal_unknown=true;}
};
}
inline void predict_into(const Input& input,bool coupling_enabled,Result& out){
    out=Result{};auto fail=[&](Unsupported reason){out.unsupported=reason;};
    if(!input.info||!input.menu||!input.priority||!input.priority->frozen||!known_policy(input.priority->policy)){fail(Unsupported::policy);return;}
    const auto& info=*input.info;const auto& enemy=input.enemy;const auto& menu=*input.menu;
    if(info.public_layout_truncated||info.round<0||info.round>=std::numeric_limits<int>::max()-max_horizon*3||info.settings.capture_rounds<0||info.settings.recall_rounds<0
        ||!info.settings.winning_gems||!std::isfinite(info.settings.tribute_multiplier)||info.settings.tribute_multiplier<0
        ||info.group_count<0||info.group_count>max_groups||info.altar_count<0||info.altar_count>max_altars||input.priority->count!=info.group_count
        ||menu.count<1||menu.count>max_jobs||menu.single_count<0||menu.single_count>8||menu.single_count>menu.count){fail(Unsupported::stock);return;}
    std::array<bool,max_groups> seen{};
    for(int n=0;n<input.priority->count;++n){const int g=input.priority->groups[n];
        if(g<0||g>=info.group_count||seen[g]||input.priority->ids[n]!=info.groups[g].id){fail(Unsupported::policy);return;}seen[g]=true;
        if(info.groups[g].respawn_rounds<0||info.groups[g].respawn_remaining< -1){fail(Unsupported::stock);return;}}
    if(!enemy.personally_visible||!enemy.entry_phase_supported||enemy.actor.id<0||enemy.actor.id==info.own.id
        ||enemy.provenance.source!=Source::personal_vision||enemy.provenance.status!=Status::exact_now||enemy.provenance.observed_round!=info.round
        ||enemy.next_action_round<info.round||enemy.next_action_round>info.round+1||enemy.cursor<0||enemy.actor.position.x<0||enemy.actor.position.y<0||!std::isfinite(enemy.actor.health)
        ||!std::isfinite(enemy.actor.maximum_health)||enemy.actor.health<=0||enemy.actor.health>enemy.actor.maximum_health){fail(Unsupported::enemy);return;}
    if(!input.order_known||!input.order_at){fail(Unsupported::order);return;}
    if(!input.sole_producers_verified){fail(Unsupported::stock);return;}
    bool admitted=false;
    for(int n=0;n<menu.count;++n){const auto& job=menu.jobs[n];
        admitted|=job.kind==input.first.kind&&job.group==input.first.group&&job.second_group==input.first.second_group&&job.altar==input.first.altar;
        if(unsigned(job.kind)>unsigned(JobKind::hold)||(n<menu.single_count&&(job.kind!=JobKind::farm||job.second_group>=0))
            ||(job.kind==JobKind::farm&&(job.group<0||job.group>=info.group_count||job.second_group>=info.group_count||job.second_group==job.group))
            ||((job.kind==JobKind::farm||job.kind==JobKind::bank)&&(job.altar<0||job.altar>=info.altar_count))){fail(Unsupported::job);return;}}
    if(!input.enemy_travel.distance){fail(Unsupported::quote);return;}
    if(!admitted||!input.quote_leg||!info.own.alive||info.own.position.x<0||info.own.position.y<0||info.own.position==enemy.actor.position||!std::isfinite(info.own.health)||!std::isfinite(info.own.maximum_health)
        ||info.own.health<=0||info.own.health>info.own.maximum_health||info.own.channel_ticks<0){fail(Unsupported::job);return;}
    detail::Simulation(input,coupling_enabled,out).run();
}
} // namespace plan4::commons
