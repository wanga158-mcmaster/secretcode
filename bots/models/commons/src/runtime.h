#pragma once
#include "commons.h"
#include "../../common/model-runtime.h"
#include <memory>

namespace plan4::commons_binding {
// These are conditional fixture contracts, not facts supplied by a module SHA.
// A live producer must establish them from the declared inspectable role policy,
// public rosters and uninterrupted legal observation history. No SDK collector
// currently produces EntryFrame/IsolationFrame, so ordinary mains remain M0.
struct IsolationFrame {
    runtime::ObservationClock clock{};
    std::uint64_t profile=0;
    bool native_initial_state=false, own_idle_uninterrupted=false,
         enemy_idle_uninterrupted=false, no_other_producers=false;
};
struct StockEpoch {
    int group_id=-1, epoch=-1;
    Lifecycle lifecycle=Lifecycle::unknown;
    Provenance provenance{};
};
struct EntryFrame {
    runtime::ObservationClock clock{};
    std::uint64_t profile=0;
    int enemy_id=-1, cursor=-1, next_action_round=-1;
    Provenance provenance{};
    // Cursor is a derived observation-history value, never a private fixture
    // query. Boundary excludes an ongoing walk/attack/channel/capture macro.
    bool history_complete=false, idle_policy_boundary=false;
    std::array<StockEpoch,max_groups> stocks{};
    int stock_count=0;
};
struct LegalLegQuote {
    runtime::ObservationClock clock{};
    std::uint64_t profile=0, geometry=0;
    int actor_id=-1;
    bool enemy=false, physical_route=false, phase_schedule=false,
         safety_and_occupancy=false;
    commons::LegPlan plan{};
};
struct PublicProfile {
    std::uint64_t identity=0, geometry=0;
    commons::PolicySeal rival_policy{}, own_idle_policy{};
    std::array<int,5> allied_roster{}, enemy_roster{};
    int allied_count=0,enemy_count=0,own_producer=-1,enemy_producer=-1;
    Point own_spawn{},enemy_spawn{};
    bool configured=false,public_spawns_supported=false,rival_orientation_supported=false;
    // Separate enemy-oriented map: team spawn regions have different legality.
    // Callbacks must be immutable data-only bounded lookups for this frame.
    TravelLookup enemy_travel{};
    bool (*order_at)(int,bool&,const void*)=nullptr;
    const void* order_context=nullptr;
    // Publication supports every forecast round, not a guessed draft ordinal.
    int order_first_round=-1,order_last_round=-1;
    bool order_published=false;
    // Published ranks include every roster body and any draft-order tie. A
    // roster enumeration is not a rank certificate. Fixed priorities must be
    // established by the fixture policy/history and cross-checked when seen.
    int own_rank=-1,enemy_rank=-1,own_priority=0,enemy_priority=0;
    bool fixed_rank_certificate=false;
    bool (*quote_leg)(const commons::LegRequest&,const Information&,LegalLegQuote&,const void*)=nullptr;
    const void* quote_context=nullptr;
};
inline bool same_clock(runtime::ObservationClock a,runtime::ObservationClock b){return a.round==b.round&&a.own.id==b.own.id;}
inline bool fresh(Provenance p,int round){return p.observed_round==round&&p.status==Status::exact_now&&p.source==Source::personal_vision;}
inline std::uint64_t mix(std::uint64_t h,std::uint64_t x){return(h^x)*1099511628211ull;}
// Public source binding, independent of dynamic HP/bags. Reordering public
// rows changes this binding and requires a new profile/priority certificate.
inline std::uint64_t geometry(const Information& info,const runtime::PublicLayoutFrame& layout){
    if(info.group_count<0||info.group_count>max_groups||info.altar_count<0||info.altar_count>max_altars
        ||layout.group_count<0||layout.group_count>max_groups||layout.member_count<0||layout.member_count>max_groups*16
        ||(layout.group_count&&!layout.groups)||(layout.member_count&&!layout.members))return 0;
    std::uint64_t h=1469598103934665603ull;
    h=mix(h,unsigned(info.group_count));h=mix(h,unsigned(info.altar_count));
    for(int n=0;n<info.group_count;++n){if(!can_refine(info))return 0;const auto&g=info.groups[n];
        h=mix(h,unsigned(g.id));h=mix(h,unsigned(g.position.x));h=mix(h,unsigned(g.position.y));
        h=mix(h,g.gems);h=mix(h,g.gold);h=mix(h,unsigned(g.respawn_rounds));}
    for(int n=0;n<info.altar_count;++n){if(!can_refine(info))return 0;const auto&a=info.altars[n];
        h=mix(h,unsigned(a.id));h=mix(h,unsigned(a.position.x));h=mix(h,unsigned(a.position.y));
        h=mix(h,unsigned(a.altar_position.x));h=mix(h,unsigned(a.altar_position.y));h=mix(h,a.accessible);}
    for(int n=0;n<layout.group_count;++n){if(!can_refine(info))return 0;const auto&g=layout.groups[n];
        if(g.first<0||g.count<1||g.count>16||g.first>layout.member_count-g.count)return 0;
        h=mix(h,unsigned(g.group_id));h=mix(h,unsigned(g.count));
        for(int p=0;p<g.count;++p){if(!can_refine(info))return 0;const auto&m=layout.members[g.first+p];
            h=mix(h,unsigned(m.member_id));h=mix(h,unsigned(m.kind));h=mix(h,unsigned(m.ordinal));
            h=mix(h,unsigned(m.spawn.x));h=mix(h,unsigned(m.spawn.y));}}
    return h?h:1;
}
enum class Unsupported {none,profile,clock,roster,isolation,entry,layout,stock,priority,order,quote,forecast,budget};
} // namespace plan4::commons_binding

namespace plan4::runtime {
class Commons : public M0 {
    struct Prefix {Job job{};std::array<ModelPredictionEvent,commons::event_limit>events{};int count=0;};
    struct Storage {
        commons_binding::PublicProfile profile{};
        commons_binding::IsolationFrame isolation{};
        commons_binding::EntryFrame entry{};
        ActorFrame actors{};PublicLayoutFrame layout{};SpatialFrame spatial{};MemberFrame members{};
        commons::Priority priority{};commons::Result result{};commons::Input input{};
        std::array<bool,max_groups> composition{};
        std::array<Prefix,max_jobs> prefixes{};
        const Information* snapshot=nullptr;const Information* receipt_source=nullptr;
        Menu receipt_menu{};int receipt_chosen=-1,receipt_round=-1,receipt_actor=-1;
        Point receipt_position{};float receipt_health=0,receipt_maximum=0;
        unsigned receipt_bag=0,receipt_gold=0,receipt_ours=0,receipt_enemy=0;
        std::uint64_t priority_geometry=0;
    };
    std::unique_ptr<Storage>s_=std::make_unique<Storage>();
    bool enabled_=true,used_=false;
    commons_binding::Unsupported reason_=commons_binding::Unsupported::profile;
    int paths_=0,work_=0,unknown_=0;
    bool current_own_first_=false;
    std::uint64_t revision_=0;
    using U=commons_binding::Unsupported;
    void invalidate(){used_=false;s_->receipt_source=nullptr;s_->receipt_chosen=-1;}
    bool fail(U why){reason_=why;return false;}
    bool budget(const Information& info){return can_refine(info)||fail(U::budget);}
    static bool roster(const std::array<int,5>&actual,int count,const std::array<int,5>&expected,int expected_count,int producer){
        if(count<1||count>5||count!=expected_count||producer<0)return false;
        bool found=false;int minimum=std::numeric_limits<int>::max();
        for(int n=0;n<count;++n){if(actual[n]<0)return false;found|=actual[n]==producer;bool match=false;
            minimum=std::min(minimum,actual[n]);
            for(int p=0;p<count;++p){if(p<n&&actual[p]==actual[n])return false;match|=actual[n]==expected[p];}
            if(!match)return false;}
        return found&&producer==minimum;
    }
    bool roster_bound()const{
        const auto&p=s_->profile;const auto&a=s_->actors;
        if(!a.roster_complete||a.truncated||!roster(a.allied_roster,a.allied_count,p.allied_roster,p.allied_count,p.own_producer)
            ||!roster(a.enemy_roster,a.enemy_count,p.enemy_roster,p.enemy_count,p.enemy_producer))return false;
        for(int n=0;n<a.allied_count;++n)for(int e=0;e<a.enemy_count;++e)if(a.allied_roster[n]==a.enemy_roster[e])return false;
        return true;
    }
    const VisibleActor* rival(const Information&info){
        const auto&a=s_->actors;const VisibleActor*found=nullptr;
        for(int n=0;n<a.count;++n){if(!budget(info))return nullptr;const auto&v=a.samples[n];
            if(v.id==s_->profile.enemy_producer){if(found||v.ally||!v.alive||!commons_binding::fresh(v.provenance,info.round))return nullptr;found=&v;}}
        return found;
    }
    static bool order(int round,bool& own_first,const void*context){
        auto&self=*const_cast<Commons*>(static_cast<const Commons*>(context));const auto&info=*self.s_->input.info;const auto&p=self.s_->profile;
        if(!self.budget(info)||round<p.order_first_round||round>p.order_last_round)return false;
        const bool okay=p.order_at&&p.order_at(round,own_first,p.order_context);
        if(!self.budget(info)||!okay)return false;
        const bool expected=p.own_rank!=p.enemy_rank?p.own_rank<p.enemy_rank:
            self.current_own_first_^bool((round-info.round)&1);
        return own_first==expected;
    }
    static bool quote(const commons::LegRequest&r,const Information&info,commons::LegPlan&out,const void*context){
        auto&self=*const_cast<Commons*>(static_cast<const Commons*>(context));const auto&p=self.s_->profile;
        if(!self.budget(info))return false;
        if(r.kind==commons::Leg::acquire&&(r.group<0||r.group>=info.group_count||!self.s_->composition[r.group]))return false;
        if(r.travel.distance!=(r.enemy?p.enemy_travel.distance:info.travel.distance)
            ||r.travel.context!=(r.enemy?p.enemy_travel.context:info.travel.context))return false;
        commons_binding::LegalLegQuote q;
        const bool okay=p.quote_leg&&p.quote_leg(r,info,q,p.quote_context);++self.work_;
        if(!self.budget(info))return false;
        if(!okay||!commons_binding::same_clock(q.clock,{info.round,{info.own.id}})||q.profile!=p.identity||q.geometry!=p.geometry
            ||q.actor_id!=r.actor.id||q.enemy!=r.enemy||!q.physical_route||!q.phase_schedule||!q.safety_and_occupancy)return false;
        out=q.plan;return true;
    }
    bool composition(const Information&info){
        const auto&f=s_->layout;const auto&m=s_->members;
        if(f.truncated||f.group_count<0||f.group_count>max_groups||f.member_count<0||f.member_count>max_groups*16
            ||(f.group_count&&!f.groups)||(f.member_count&&!f.members)||m.truncated||m.count<0||m.count>max_groups*16||(m.count&&!m.samples))return fail(U::layout);
        for(int g=0;g<info.group_count;++g)s_->composition[g]=false;
        for(int n=0;n<f.group_count;++n){if(!budget(info))return false;const auto&pg=f.groups[n];int g=-1;
            for(int p=0;p<info.group_count;++p){if(!budget(info))return false;if(info.groups[p].id==pg.group_id){if(g>=0)return fail(U::layout);g=p;}}
            if(g<0||s_->composition[g]||!pg.complete||pg.count<=0||pg.count>16||pg.first<0||pg.first>f.member_count-pg.count)return fail(U::layout);
            float maximum=0,health=0;bool health_complete=true;
            for(int p=0;p<pg.count;++p){if(!budget(info))return false;const auto&pm=f.members[pg.first+p];
                if(pm.group_id!=pg.group_id||pm.ordinal!=p||pm.declared_members!=pg.count||pm.member_id<0||pm.spawn.x<0||pm.spawn.y<0
                    ||!std::isfinite(pm.maximum_health)||pm.maximum_health<=0||pm.attack_range<0)return fail(U::layout);
                for(int k=0;k<p;++k)if(f.members[pg.first+k].member_id==pm.member_id)return fail(U::layout);
                maximum+=pm.maximum_health;const VisibleMember*observed=nullptr;
                for(int k=0;k<m.count;++k){if(!budget(info))return false;const auto&vm=m.samples[k];if(vm.group_id==pm.group_id&&vm.member_id==pm.member_id){if(observed)return fail(U::layout);observed=&vm;}}
                if(!observed||observed->ordinal!=p||observed->declared_members!=pg.count||observed->spawn!=pm.spawn
                    ||!commons_binding::fresh(observed->provenance,info.round)||!std::isfinite(observed->health)||observed->health<0||observed->health>pm.maximum_health)health_complete=false;
                else health+=observed->health;
            }
            const auto&group=info.groups[g];
            if(group.position!=f.members[pg.first].spawn||group.full_health!=maximum)return fail(U::layout);
            if(group.lifecycle==Lifecycle::living&&group.visible&&commons_binding::fresh(group.provenance,info.round)
                &&(!health_complete||!commons_binding::fresh(group.health_provenance,info.round)||health!=group.health))return fail(U::layout);
            s_->composition[g]=true;
        }
        return true;
    }
    bool priority(const Information&info){
        const auto&p=s_->profile;auto&out=s_->priority;
        if(out.frozen)return s_->priority_geometry==p.geometry||fail(U::priority);
        out.count=0;
        std::array<int,max_groups>distances;
        for(int g=0;g<info.group_count;++g){if(!budget(info))return false;
            const int d=p.enemy_travel.distance(p.enemy_spawn,info.groups[g].position,p.enemy_travel.context);
            if(!budget(info))return false;
            if(d<0)return fail(U::priority);
            distances[g]=d;
            for(int previous=0;previous<g;++previous)if(info.groups[previous].id==info.groups[g].id)return fail(U::priority);
            int place=out.count;
            while(place>0&&(distances[out.groups[place-1]]>d||(distances[out.groups[place-1]]==d&&out.ids[place-1]>info.groups[g].id))){
                if(!budget(info))return false;
                out.groups[place]=out.groups[place-1];out.ids[place]=out.ids[place-1];--place;}
            out.groups[place]=g;out.ids[place]=info.groups[g].id;++out.count;
        }
        out.policy=p.rival_policy;out.public_spawn=p.enemy_spawn;out.frozen=true;s_->priority_geometry=p.geometry;return true;
    }
    bool bind(const Information&info,const Menu&menu){
        const auto&p=s_->profile;const auto&a=s_->actors;const auto&f=s_->spatial;const auto&e=s_->entry;const auto&i=s_->isolation;
        const ObservationClock clock{info.round,{info.own.id}};
        if(!p.configured||!p.identity||!p.geometry||!commons::known_policy(p.rival_policy)||!commons::known_policy(p.own_idle_policy)
            ||!p.public_spawns_supported||!p.rival_orientation_supported||!p.enemy_travel.distance||!p.quote_leg)return fail(U::profile);
        if(!budget(info))return false;
        if(s_->snapshot!=&info||!commons_binding::same_clock(a.clock,clock)||!commons_binding::same_clock(f.clock,clock)
            ||!commons_binding::same_clock(s_->layout.clock,clock)||!commons_binding::same_clock(e.clock,clock)||!commons_binding::same_clock(i.clock,clock)
            ||f.information!=&info||f.menu!=&menu)return fail(U::clock);
        if(info.public_layout_truncated||info.group_count<0||info.group_count>max_groups||info.altar_count<0||info.altar_count>max_altars
            ||!f.current_complete||!f.topology.complete||!f.topology.walkable||!f.occupancy||!info.travel.distance
            ||f.travel.distance!=info.travel.distance||f.travel.context!=info.travel.context||p.own_spawn!=info.own_spawn
            ||p.enemy_spawn.x<0||p.enemy_spawn.y<0||p.enemy_spawn==p.own_spawn)return fail(U::layout);
        if(p.enemy_travel.distance==info.travel.distance&&p.enemy_travel.context==info.travel.context)return fail(U::profile);
        if(!roster_bound()||info.own.id!=p.own_producer||!a.own_observed||a.count<0||a.count>9)return fail(U::roster);
        const auto&own=a.own;
        if(own.id!=info.own.id||!own.ally||!own.alive||own.position!=info.own.position||own.health!=info.own.health
            ||own.maximum_health!=info.own.maximum_health||own.bag!=info.own.bag||own.provenance.observed_round!=info.round
            ||own.provenance.status!=Status::exact_now||own.provenance.source!=Source::own_state)return fail(U::roster);
        if(i.profile!=p.identity||!i.native_initial_state||!i.own_idle_uninterrupted||!i.enemy_idle_uninterrupted||!i.no_other_producers)return fail(U::isolation);
        const auto*v=rival(info);if(!v)return reason_==U::budget?false:fail(U::entry);
        if(e.profile!=p.identity||e.enemy_id!=v->id||!e.history_complete||!e.idle_policy_boundary||e.cursor<0||e.cursor>=std::max(1,info.group_count)
            ||e.next_action_round!=(v->completed_turn?info.round+1:info.round)||e.provenance.observed_round!=info.round
            ||e.provenance.status!=Status::derived||e.provenance.source!=Source::personal_vision||e.stock_count<0||e.stock_count>max_groups)return fail(U::entry);
        if(!p.order_published||!p.order_at||p.order_first_round>info.round||p.order_last_round<info.round+horizon(info)
            ||!p.fixed_rank_certificate||p.own_rank<0||p.own_rank>=a.allied_count||p.enemy_rank<0||p.enemy_rank>=a.enemy_count
            ||p.own_priority!=own.current_priority||p.enemy_priority!=v->current_priority)return fail(U::order);
        // Current relative order is observable even without draft ordinals.
        bool own_first=false;if(!p.order_at(info.round,own_first,p.order_context))return fail(U::order);
        if(!budget(info))return false;
        if(own_first==v->completed_turn)return fail(U::order);
        if(p.own_rank!=p.enemy_rank&&own_first!=(p.own_rank<p.enemy_rank))return fail(U::order);
        current_own_first_=own_first;
        if(horizon(info)>0){bool next_first=false;
            const bool quoted=p.order_at(info.round+1,next_first,p.order_context);
            if(!budget(info))return false;
            const bool expected=p.own_rank!=p.enemy_rank?p.own_rank<p.enemy_rank:!own_first;
            if(!quoted||next_first!=expected)return fail(U::order);
        }
        if(own.current_priority!=own.next_priority||v->current_priority!=v->next_priority)return fail(U::order);
        if(!composition(info))return false;
        for(int g=0;g<info.group_count;++g){if(!budget(info))return false;const auto&source=info.groups[g];
            if(!source.visible||!commons_binding::fresh(source.provenance,info.round))continue;
            if(!source.occupancy_known||!source.all_spawns_visible||source.contested)return fail(U::stock);
            const commons_binding::StockEpoch*witness=nullptr;
            for(int n=0;n<e.stock_count;++n){if(!budget(info))return false;if(e.stocks[n].group_id==source.id){if(witness)return fail(U::stock);witness=&e.stocks[n];}}
            if(!witness||witness->epoch<0||witness->lifecycle!=source.lifecycle||!commons_binding::fresh(witness->provenance,info.round))return fail(U::stock);
        }
        const auto signature=commons_binding::geometry(info,s_->layout);if(!budget(info))return false;
        if(signature!=p.geometry)return fail(U::layout);
        if(!priority(info))return false;
        auto&in=s_->input;in={};in.info=&info;in.menu=&menu;in.priority=&s_->priority;in.sole_producers_verified=in.order_known=true;
        in.enemy_travel=p.enemy_travel;in.order_at=order;in.order_context=this;in.quote_leg=quote;in.quote_context=this;
        // Rival gold is deliberately not observed or used by the fixture rule.
        // This zero is a local earned-gold counter, not an inventory assertion;
        // the known policy never buys or makes cash-dependent decisions.
        in.enemy.actor={v->id,v->position,v->health,v->maximum_health,v->bag,0};in.enemy.provenance=v->provenance;
        in.enemy.personally_visible=in.enemy.entry_phase_supported=true;in.enemy.cursor=e.cursor;in.enemy.next_action_round=e.next_action_round;
        reason_=U::none;return true;
    }
    void forecast(const commons::Result&r,Forecast&f){
        f.reset();f.macros_started=r.own_macros_started;f.final_round=r.final_round;f.first_conversion_round=r.first_conversion_round;
        f.our_secured=r.our_secured;f.enemy_secured=r.enemy_secured;f.final_bag=r.final_bag;f.final_gold=r.final_gold;f.final_health=r.final_health;
        f.terminal_z=r.terminal_z;f.terminal_unknown=r.terminal_unknown;f.stock_unknown=r.stock_unknown;f.truncated=r.truncated;
        f.completed_conversion=r.our_secured>0;f.no_conversion=!f.completed_conversion;
        // The economic tape has no per-event HP/gold fields. Do not invent
        // intermediate physical states from the terminal quote. Its exact
        // colored economic prefix is emitted separately through receipts.
    }
public:
    explicit Commons(bool enabled=true):enabled_(enabled){}
    const char*name()const{return "commons";}
    void configure(const commons_binding::PublicProfile&profile){invalidate();s_->profile=profile;s_->priority.frozen=false;s_->priority.count=0;}
    void observe_isolation(const commons_binding::IsolationFrame&frame){invalidate();s_->isolation=frame;}
    void observe_entry(const commons_binding::EntryFrame&frame){invalidate();s_->entry=frame;}
    void observe_actors(const ActorFrame&frame){invalidate();s_->actors=frame;}
    void observe_layout(const PublicLayoutFrame&frame){invalidate();s_->layout=frame;}
    void observe_spatial(const SpatialFrame&frame){invalidate();s_->spatial=frame;}
    void observe_snapshot(const Information&info,MemberFrame members={}){invalidate();s_->snapshot=&info;s_->members=members;}
    bool isolation_idle(const ObservationClock&clock)const{
        if(!s_->profile.configured)return false;
        if(!commons_binding::same_clock(clock,s_->actors.clock)||!roster_bound())return true;
        return clock.own.id!=s_->profile.own_producer;
    }
    void evaluate_into(const Information&info,const Menu&menu,Evaluation&out){
        invalidate();++revision_;paths_=work_=unknown_=0;reason_=U::profile;
        out.menu=menu;out.evaluated_count=0;out.chosen=-1;out.budget_exhausted=false;
        if(!bind(info,menu)){if(reason_==U::budget)out.budget_exhausted=true;else plan4::evaluate_into(info,menu,out);return;}
        for(int n=0;n<menu.count;++n){if(!budget(info)){out.evaluated_count=0;out.budget_exhausted=true;return;}
            s_->input.first=menu.jobs[n];commons::predict_into(s_->input,enabled_,s_->result);const auto&r=s_->result;
            if(!budget(info)||r.unsupported==commons::Unsupported::budget){reason_=U::budget;out.evaluated_count=0;out.budget_exhausted=true;return;}
            if(!r.supported){reason_=U::forecast;plan4::evaluate_into(info,menu,out);return;}
            forecast(r,out.forecasts[n]);auto&prefix=s_->prefixes[n];prefix.job=menu.jobs[n];prefix.count=0;
            for(int e=0;e<r.event_count;++e){if(!budget(info)){out.evaluated_count=0;out.budget_exhausted=true;return;}const auto&event=r.events[e];
                prefix.events[prefix.count++]={event.round,int(event.phase),int(event.kind),event.enemy?s_->profile.enemy_producer:info.own.id,event.group_id,
                    std::int64_t(event.amount)*1000,event.bag_after};}
            ++paths_;++out.evaluated_count;unknown_+=r.stock_unknown||r.terminal_unknown;
        }
        out.chosen=choose(info,menu,out.forecasts);used_=true;s_->receipt_source=&info;s_->receipt_round=info.round;s_->receipt_actor=info.own.id;
        s_->receipt_position=info.own.position;s_->receipt_health=info.own.health;s_->receipt_maximum=info.own.maximum_health;
        s_->receipt_bag=info.own.bag;s_->receipt_gold=info.own.gold;s_->receipt_ours=info.our_score;s_->receipt_enemy=info.enemy_score;
        s_->receipt_menu=menu;s_->receipt_chosen=out.chosen;
    }
    ModelEvidence model_evidence()const{return{true,used_,enabled_,int(reason_),used_?1:0,unknown_+(reason_!=U::none),paths_,work_,revision_};}
    void prediction_receipts(const Information&info,const Job&job,PredictionEventSink sink,void*context)const{
        const auto&s=*s_;if(!sink||!used_||s.receipt_source!=&info||s.receipt_round!=info.round||s.receipt_actor!=info.own.id||s.receipt_chosen<0
            ||s.receipt_position!=info.own.position||s.receipt_health!=info.own.health||s.receipt_maximum!=info.own.maximum_health||s.receipt_bag!=info.own.bag
            ||s.receipt_gold!=info.own.gold||s.receipt_ours!=info.our_score||s.receipt_enemy!=info.enemy_score)return;
        const auto&p=s.prefixes[s.receipt_chosen];if(p.job.kind!=job.kind||p.job.group!=job.group||p.job.second_group!=job.second_group||p.job.altar!=job.altar)return;
        for(int n=0;n<p.count;++n){if(!can_refine(info)||!sink(p.events[n],context))return;}
    }
    bool used_commons()const{return used_;}
    commons_binding::Unsupported unsupported()const{return reason_;}
    static constexpr bool live_profile_producer_available=false;
};
} // namespace plan4::runtime
