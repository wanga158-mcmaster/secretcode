#pragma once
#include "kernel.h"
#include "exact-kit.h"
#include "../../common/model-runtime.h"
#include <memory>

namespace plan4::runtime {
namespace kernel_binding {
struct Profile {
    const kernel::FrozenTable* table=nullptr; // Immutable; must outlive Runtime.
    std::uint64_t identity=0;
    bool production_frozen=false;
};
enum class Reason { none,table,source,composition,context,quote,endpoint,cohort,mapping,budget,no_applicable_job };
} // namespace kernel_binding

class Kernel : public M0 {
    using Reason=kernel_binding::Reason;
    struct Storage {
        kernel::Kernel process;
        SimulationState baseline,state;
        Forecast nominal,scratch;
        std::array<Forecast,kernel::sample_count*2> whole;
        std::array<PublicGroup,8> groups{};
        std::array<PublicMember,256> members{};
        std::array<VisibleMember,256> visible{};
        Storage() noexcept {} // Do not zero unused nested Forecast event capacity.
    };
    std::unique_ptr<Storage> s_=std::make_unique<Storage>();
    kernel_binding::Profile profile_{};
    kernel_binding::ExactKitFrame exact_{};
    CombatKit combat_{};
    ActorFrame actors_{};
    Actor own_{};
    ObservationClock snapshot_{},layout_{};
    int group_count_=0,member_count_=0,visible_count_=0;
    bool layout_valid_=false,visible_valid_=false,enabled_=true,batch_seen_=false,budget_failed_=false;
    kernel::Use use_=kernel::Use::production_only;
    Reason reason_=Reason::table;
    int supported_=0,unknown_=0,paths_=0,work_=0,samples_=0;
    std::uint64_t revision_=0;
    struct Continuation {
        Kernel& self;
        const Menu& menu;
        const kernel::FrozenTable::Bin& bin;
    };
    static bool budget(const Information& i){return kernel::budget(i);}
    void invalidate(){batch_seen_=false;supported_=unknown_=paths_=work_=samples_=0;budget_failed_=false;reason_=Reason::table;}
    static bool exact_now(Provenance p,int r,Source source){return p.observed_round==r&&p.source==source&&p.status==Status::exact_now;}
    static bool same_actor(const Actor& a,const Actor& b){return std::tie(a.id,a.position.x,a.position.y,a.health,a.maximum_health,a.bag,a.gold,a.channel,a.channel_ticks,
        a.alive,a.on_spawn,a.recall_safe,a.escape_rounds,a.escape_incoming)==std::tie(b.id,b.position.x,b.position.y,b.health,b.maximum_health,b.bag,b.gold,b.channel,b.channel_ticks,
        b.alive,b.on_spawn,b.recall_safe,b.escape_rounds,b.escape_incoming);}
    bool current(const Information& i)const {
        const auto& k=exact_.kit;const auto& a=actors_.own;
        return snapshot_.round==i.round&&snapshot_.own.id==i.own.id&&same_actor(own_,i.own)&&i.own.alive&&i.own.health>0&&
            i.own.channel==Channel::none&&!i.public_layout_truncated&&i.group_count>=0&&i.group_count<=max_groups&&i.altar_count>=0&&i.altar_count<=max_altars&&
            exact_.clock.round==i.round&&exact_.clock.own.id==i.own.id&&exact_now(exact_.provenance,i.round,Source::own_state)&&
            exact_.complete_inventory&&exact_.abilities_known&&k.known&&k.first_ability>=0&&k.second_ability>=0&&
            combat_.exact&&combat_.clock.round==i.round&&combat_.clock.own.id==i.own.id&&combat_.version==exact_.kit_version&&combat_.stat_epoch==exact_.stat_epoch&&
            combat_.champion==k.champion&&combat_.attack_range==k.attack_range&&combat_.strength==k.strength&&combat_.focus==k.focus&&
            actors_.clock.round==i.round&&actors_.clock.own.id==i.own.id&&actors_.roster_complete&&!actors_.truncated&&actors_.own_observed&&
            actors_.count>=0&&actors_.count<=9&&a.id==i.own.id&&a.ally&&a.alive&&a.position==i.own.position&&
            a.health==i.own.health&&a.maximum_health==i.own.maximum_health&&a.bag==i.own.bag&&a.champion==k.champion&&
            a.strength==k.strength&&a.attack_range==k.attack_range&&exact_now(a.provenance,i.round,Source::own_state);
    }
    bool context(const Information& i,kernel::Context& value)const {
        if(actors_.allied_count<1||actors_.allied_count>5||actors_.enemy_count<1||actors_.enemy_count>5)return false;
        int own_count=0;
        for(int side=0;side<2;++side){const auto& ids=side?actors_.enemy_roster:actors_.allied_roster;const int count=side?actors_.enemy_count:actors_.allied_count;
            for(int n=0;n<count;++n){if(ids[n]<0)return false;own_count+=!side&&ids[n]==i.own.id;
                for(int p=0;p<n;++p)if(ids[p]==ids[n])return false;
                if(side)for(int p=0;p<actors_.allied_count;++p)if(ids[n]==actors_.allied_roster[p])return false;}}
        if(own_count!=1)return false;
        for(int n=0;n<actors_.count;++n){const auto& a=actors_.samples[n];bool roster=false;
            const auto& ids=a.ally?actors_.allied_roster:actors_.enemy_roster;const int count=a.ally?actors_.allied_count:actors_.enemy_count;
            for(int p=0;p<count;++p)roster|=ids[p]==a.id;
            if(!roster||a.id==i.own.id||!a.alive||!exact_now(a.provenance,i.round,Source::personal_vision))return false;
            for(int p=0;p<n;++p)if(actors_.samples[p].id==a.id)return false;}
        value=actors_.count?kernel::Context::other_actor_visible:kernel::Context::solo_visible_context;return true;
    }
    bool composition(const Information& i,int g,kernel::Composition& result) {
        if(!layout_valid_||!visible_valid_||layout_.round!=i.round||layout_.own.id!=i.own.id)return false;
        const auto& source=i.groups[g];const PublicGroup* group=nullptr;
        for(int n=0;n<group_count_;++n)if(s_->groups[n].group_id==source.id){if(group)return false;group=&s_->groups[n];}
        if(!group||!group->complete||group->count<1||group->count>kernel::composition_limit||group->first<0||group->first>member_count_-group->count)return false;
        float health=0,maximum=0;bool anchor=false;result.count=group->count;
        for(int m=0;m<group->count;++m){if(!budget(i))return false;const auto& expected=s_->members[group->first+m];
            if(expected.group_id!=source.id||expected.member_id<0||expected.kind<0||expected.ordinal<0||expected.ordinal>=group->count||
                expected.declared_members!=group->count||!std::isfinite(expected.maximum_health)||expected.maximum_health<=0)return false;
            for(int p=0;p<m;++p){const auto& old=s_->members[group->first+p];
                if(old.member_id==expected.member_id||old.ordinal==expected.ordinal||old.spawn==expected.spawn)return false;}
            const VisibleMember* observed=nullptr;
            for(int v=0;v<visible_count_;++v){if((v%8)==0&&!budget(i))return false;const auto& x=s_->visible[v];
                if(x.group_id==source.id&&x.member_id==expected.member_id){if(observed)return false;observed=&x;}}
            if(!observed||observed->spawn!=expected.spawn||observed->ordinal!=expected.ordinal||observed->declared_members!=group->count||
                !exact_now(observed->provenance,i.round,Source::personal_vision)||!std::isfinite(observed->health)||observed->health<0||observed->health>expected.maximum_health)return false;
            anchor|=expected.spawn==source.position;health+=observed->health;maximum+=expected.maximum_health;result.kinds[m]=expected.kind;
        }
        result.known=anchor&&health==source.health&&maximum==source.full_health;return result.known;
    }
    static const kernel::PhaseTemplate* phase(const kernel::StartedRow& row,kernel::PhaseKind kind){
        for(int n=0;n<row.phase_count;++n)if(row.phases[n].kind==kind)return &row.phases[n];
        return nullptr;
    }
    static bool follows(const kernel::StartedRow& row,kernel::PhaseKind before,kernel::PhaseKind after){
        const auto* a=phase(row,before);const auto* z=phase(row,after);return !a||!z||!kernel::fraction_before(z->begin,a->end);
    }
    bool physical_rows(const kernel::Candidate& c,const kernel::FrozenTable::Bin& bin)const {
        for(int n=0;n<kernel::sample_count;++n){const auto& row=*bin.rows[n];int captures=0,rewards=0,conversions=0;
            for(int e=0;e<row.event_count;++e){captures+=row.tape[e].kind==kernel::EconomicKind::group_capture;
                rewards+=row.tape[e].kind==kernel::EconomicKind::solo_reward;conversions+=row.tape[e].kind==kernel::EconomicKind::whole_bag_conversion;}
            if(phase(row,kernel::PhaseKind::restore)||row.endpoint==kernel::Symbol::spawn||row.endpoint==kernel::Symbol::failed_approach)return false;
            // A retained conversion phase without its completion receipt can
            // alias an ongoing channel, which SimulationState cannot restore.
            if(phase(row,kernel::PhaseKind::conversion)&&conversions!=1)return false;
            if(c.minimum_phase[unsigned(kernel::PhaseKind::approach)]>0&&!phase(row,kernel::PhaseKind::approach))return false;
            if(c.bin.macro==kernel::Macro::one_camp){
                // The tape contains economic events only. Without an actual
                // capture it supplies no remaining HP/clear-progress state.
                if(captures!=1||rewards!=1||!phase(row,kernel::PhaseKind::capture)||
                    (c.minimum_phase[unsigned(kernel::PhaseKind::clear)]>0&&!phase(row,kernel::PhaseKind::clear))||
                    !follows(row,kernel::PhaseKind::approach,kernel::PhaseKind::clear)||!follows(row,kernel::PhaseKind::approach,kernel::PhaseKind::capture)||
                    !follows(row,kernel::PhaseKind::clear,kernel::PhaseKind::capture))return false;
                if(row.endpoint==kernel::Symbol::target){if(conversions||phase(row,kernel::PhaseKind::conversion_approach)||phase(row,kernel::PhaseKind::conversion))return false;}
                else if(row.endpoint!=kernel::Symbol::altar||!phase(row,kernel::PhaseKind::conversion_approach)||
                    !follows(row,kernel::PhaseKind::capture,kernel::PhaseKind::conversion_approach)||
                    !follows(row,kernel::PhaseKind::conversion_approach,kernel::PhaseKind::conversion))return false;
            }else if(row.endpoint!=kernel::Symbol::altar||captures||rewards||phase(row,kernel::PhaseKind::clear)||
                phase(row,kernel::PhaseKind::capture)||phase(row,kernel::PhaseKind::conversion_approach)||
                !follows(row,kernel::PhaseKind::approach,kernel::PhaseKind::conversion))return false;
            if(conversions&&!phase(row,kernel::PhaseKind::conversion))return false;
        }return true;
    }
    static int rounded(kernel::Fraction f,int scale){const auto value=std::uint64_t(f.numerator)*unsigned(scale);
        return int(value/f.denominator+(value%f.denominator!=0));}
    static bool continue_path(const kernel::Candidate& c,const kernel::Mapping& mapped,Forecast& whole,const void* pointer){
        auto& ctx=*const_cast<Continuation*>(static_cast<const Continuation*>(pointer));auto& self=ctx.self;const auto& i=*c.information;
        if(!budget(i))return false;
        int slot=-1;for(int a=0;a<2;++a)for(int k=0;k<kernel::sample_count;++k)if(&self.s_->process.path(a,k)==&mapped)slot=a*kernel::sample_count+k;
        if(slot<0)return false;
        auto& state=self.s_->state;const auto& baseline=self.s_->baseline;
        state.group_count=baseline.group_count;state.source_actor=baseline.source_actor;state.source_round=baseline.source_round;
        state.layout_fingerprint=baseline.layout_fingerprint;state.position=baseline.position;state.health=baseline.health;
        state.bag=baseline.bag;state.gold=baseline.gold;state.now=baseline.now;state.deadline=baseline.deadline;state.stopped=baseline.stopped;
        for(int g=0;g<baseline.group_count;++g){if((g%16)==0&&!budget(i))return false;state.groups[g]=baseline.groups[g];}
        state.position=mapped.endpoint;state.health=mapped.endpoint_health;state.bag=mapped.endpoint_bag;state.gold=mapped.endpoint_gold;
        state.now=mapped.endpoint_round;state.stopped=mapped.stopped||!mapped.alive;
        if(c.bin.macro==kernel::Macro::one_camp){
            if(!mapped.captured||mapped.captured_round<i.round)return false;
            const auto& source=i.groups[c.job.group];const auto& row=*ctx.bin.rows[slot%kernel::sample_count];
            auto& stock=state.groups[c.job.group];stock.lifecycle=Lifecycle::captured;stock.reward_known=true;
            stock.occupied_by_us=mapped.endpoint==source.position;
            std::int64_t ready=std::int64_t(mapped.captured_round)+std::max(0,source.respawn_rounds);
            if(!stock.occupied_by_us){const auto* movement=phase(row,kernel::PhaseKind::conversion_approach);if(!movement)return false;
                const int departure=i.round+rounded(movement->begin,mapped.duration);
                if(departure<mapped.captured_round||departure>mapped.endpoint_round)return false;
                ready=std::max(ready,std::int64_t(departure)+1);}
            stock.ready_round=int(std::min(ready,std::int64_t(state.deadline)+1));
        }
        const bool valid=plan4::continue_from_into(i,ctx.menu,mapped.first,state,whole);++self.work_;
        if(!budget(i)||!valid||whole.budget_exhausted)return false;
        self.s_->whole[slot]=whole;return true;
    }
    bool candidate(const Information& i,const Job& j,kernel::Candidate& c){
        if(j.kind!=JobKind::farm&&j.kind!=JobKind::bank)return false;
        if(j.second_group>=0||j.altar<0||j.altar>=i.altar_count||!i.altars[j.altar].accessible||
            !i.altars[j.altar].lock_known||i.altars[j.altar].hostile_lock)return false;
        kernel::Composition comp;comp.known=true;kernel::Context visible_context;
        if(!context(i,visible_context)){reason_=Reason::context;return false;}
        int approach=0,clear=0,to_bank=0;unsigned bag=i.own.bag;
        if(j.kind==JobKind::farm){if(j.group<0||j.group>=i.group_count)return false;const auto& g=i.groups[j.group];
            if(!g.visible||!g.all_spawns_visible||!g.occupancy_known||g.contested||g.occupied_spawn||g.eligible_allied_occupants!=1||
                !g.own_is_lowest_occupant||!exact_now(g.provenance,i.round,Source::personal_vision)||
                !exact_now(g.health_provenance,i.round,Source::personal_vision)||
                (g.lifecycle!=Lifecycle::living&&(g.lifecycle!=Lifecycle::cleared||!g.reward_available)))return false;
            if(!composition(i,j.group,comp)){reason_=Reason::composition;return false;}
            approach=distance(i,i.own.position,g.position);to_bank=distance(i,g.position,i.altars[j.altar].position);clear=nominal_clear(g,i.round);
            if(g.gems>std::numeric_limits<unsigned>::max()-bag)return false;
            bag+=g.gems;
        }else{if(!bag)return false;approach=distance(i,i.own.position,i.altars[j.altar].position);}
        if(!budget(i))return false;
        if(approach<0||to_bank<0||clear<0||std::int64_t(approach)+to_bank>std::numeric_limits<int>::max()||
            i.settings.capture_rounds<0||i.settings.capture_rounds==std::numeric_limits<int>::max())return false;
        c.information=&i;c.job=j;c.bin=kernel::preaction_bin(j.kind==JobKind::farm?kernel::Macro::one_camp:kernel::Macro::bank,
            exact_.kit,comp,visible_context,i.own.bag,i.own.health,i.own.maximum_health,approach+to_bank);
        if(!c.bin.valid)return false;
        c.minimum_phase={{approach,clear>0?1:0,j.kind==JobKind::farm?i.settings.capture_rounds+1:0,to_bank,tribute_ticks(bag,i.settings),-1}};
        const std::int64_t nominal=std::int64_t(approach)+clear+c.minimum_phase[2]+to_bank+c.minimum_phase[4];
        const std::int64_t minimum=std::int64_t(approach)+c.minimum_phase[1]+c.minimum_phase[2];
        if(nominal<=0||nominal>std::numeric_limits<int>::max()||minimum>std::numeric_limits<int>::max())return false;
        c.nominal_duration=int(nominal);c.minimum_total_duration=int(minimum);c.continuation_required=true;
        if(j.kind==JobKind::farm)c.endpoints[unsigned(kernel::Symbol::target)]={i.groups[j.group].position,true};
        c.endpoints[unsigned(kernel::Symbol::altar)]={i.altars[j.altar].position,true};return true;
    }
    void predict_current(const Information& i,const Job& j,const Menu& m,Forecast& out){
        auto fallback=[&](Reason reason){reason_=reason;++unknown_;plan4::predict_into(i,j,m,out);budget_failed_|=out.budget_exhausted;};
        if(!profile_.table||!profile_.identity||(use_==kernel::Use::production_only&&!profile_.production_frozen)){fallback(Reason::table);return;}
        if(j.kind!=JobKind::farm&&j.kind!=JobKind::bank){fallback(Reason::no_applicable_job);return;}
        if(!current(i)){fallback(Reason::source);return;}
        kernel::Candidate c;reason_=Reason::quote;
        if(!candidate(i,j,c)){fallback(budget(i)?reason_:Reason::budget);return;}
        const auto* bin=profile_.table->find(c.bin);
        if(!bin||bin->support!=kernel::Support::supported||(use_==kernel::Use::production_only&&!bin->production_qualified)){fallback(Reason::cohort);return;}
        if(!physical_rows(c,*bin)){fallback(Reason::endpoint);return;}
        if(!budget(i)||!plan4::initialize_state_into(i,m,s_->scratch,s_->baseline)){fallback(budget(i)?Reason::source:Reason::budget);return;}
        Continuation ctx{*this,m,*bin};const auto prediction=s_->process.predict_pair(*profile_.table,c,continue_path,&ctx,use_);
        if(prediction.use_m0_for_both){fallback(prediction.failure==kernel::Failure::budget||!budget(i)?Reason::budget:Reason::mapping);return;}
        const int arm=enabled_?0:1;const auto& path=s_->process.path(arm,0);
        out=path.reached_endpoint&&!path.stopped&&path.alive?s_->whole[arm*kernel::sample_count]:path.first;
        const auto& value=prediction.expectation[arm];
        if(!set_objective(out,{int(value.paths),int(value.z_sum),value.our_sum,value.enemy_sum,value.first_conversion_sum})){fallback(Reason::mapping);return;}
        out.terminal_unknown|=value.terminal_unknown_paths>0;++supported_;paths_+=kernel::sample_count*2;samples_=kernel::sample_count;reason_=Reason::none;
    }
public:
    explicit Kernel(bool enabled=true,kernel::Use use=kernel::Use::production_only):enabled_(enabled),use_(use){}
    const char* name()const{return "kernel";}
    bool configure(kernel_binding::Profile profile){profile_=profile;invalidate();++revision_;return profile.table&&profile.identity&&
        (use_!=kernel::Use::production_only||profile.production_frozen);}
    void observe_exact_kit(const kernel_binding::ExactKitFrame& frame){exact_=frame;invalidate();}
    void observe_combat(const CombatKit& kit){combat_=kit;invalidate();}
    void observe_actors(const ActorFrame& actors){actors_=actors;invalidate();}
    void observe_layout(const PublicLayoutFrame& frame){invalidate();layout_=frame.clock;layout_valid_=false;group_count_=member_count_=0;
        if(frame.group_count<0||frame.group_count>8||frame.member_count<0||frame.member_count>256||
            (frame.group_count&&!frame.groups)||(frame.member_count&&!frame.members))return;
        group_count_=frame.group_count;member_count_=frame.member_count;
        if(group_count_)std::copy_n(frame.groups,group_count_,s_->groups.begin());
        if(member_count_)std::copy_n(frame.members,member_count_,s_->members.begin());
        layout_valid_=true;
    }
    void observe_snapshot(const Information& i,MemberFrame frame={}){invalidate();snapshot_={i.round,{i.own.id}};own_=i.own;
        visible_valid_=frame.count>=0&&frame.count<=256&&(!frame.count||frame.samples);visible_count_=visible_valid_?frame.count:0;
        if(visible_count_)std::copy_n(frame.samples,visible_count_,s_->visible.begin());
    }
    void predict_into(const Information& i,const Job& j,const Menu& m,Forecast& out){invalidate();batch_seen_=true;++revision_;predict_current(i,j,m,out);}
    void evaluate_into(const Information& i,const Menu& m,Evaluation& out){invalidate();batch_seen_=true;++revision_;out.menu=m;out.evaluated_count=0;out.chosen=-1;out.budget_exhausted=false;
        for(int n=0;n<m.count;++n){if(!budget(i)){out.budget_exhausted=budget_failed_=true;out.evaluated_count=0;return;}
            predict_current(i,m.jobs[n],m,out.forecasts[n]);if(out.forecasts[n].budget_exhausted){out.budget_exhausted=budget_failed_=true;out.evaluated_count=0;return;}++out.evaluated_count;}
        out.chosen=choose(i,m,out.forecasts);
    }
    ModelEvidence model_evidence()const{return {true,batch_seen_&&!budget_failed_&&supported_>0,batch_seen_&&!budget_failed_&&supported_>0&&enabled_,
        int(budget_failed_?Reason::budget:supported_?Reason::none:reason_),samples_,unknown_,paths_,work_,revision_};}
};
} // namespace plan4::runtime
