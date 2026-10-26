#pragma once
#include "capital-process.h"
#include <memory>

namespace plan4::runtime {
class Capital : public M0 {
    struct Storage {
        capital::InvestmentFrame frame;
        CombatKit kit;
        capital::Path path;
        std::array<capital::ProcessEvidence,max_jobs> paths;
        Menu menu;
        const Information* source=nullptr;
        int round=-1,actor=-1,chosen=-1;
        Point position{};float health=0,maximum_health=0;
        unsigned bag=0,cash=0,our_score=0,enemy_score=0;
    };
    std::unique_ptr<Storage> s_=std::make_unique<Storage>();
    bool enabled_=true,used_=false;
    capital::Unsupported reason_=capital::Unsupported::frame;
    int completed_=0,work_=0,unknown_=0;
    std::uint64_t revision_=0;
    void invalidate(){used_=false;s_->source=nullptr;s_->chosen=-1;completed_=work_=unknown_=0;reason_=capital::Unsupported::frame;}
    bool bind(const Information&info,const Menu&menu){
        const auto&f=s_->frame;const auto&k=s_->kit;
        reason_=capital::Unsupported::frame;
        if(info.round<0||info.round>=std::numeric_limits<int>::max()-max_horizon||info.settings.max_rounds<=info.round||!info.settings.winning_gems||
            info.settings.capture_rounds<0||info.settings.recall_rounds<0||info.own.channel!=Channel::none||
            !capital::exact_investment(f)||f.clock.round!=info.round||f.clock.own.id!=info.own.id||f.position!=info.own.position||
            f.health!=info.own.health||f.stats.maximum_health!=info.own.maximum_health||f.cash!=info.own.gold||f.bag!=info.own.bag||
            !f.alive||f.on_spawn!=info.own.on_spawn||!k.exact||k.clock.round!=info.round||k.clock.own.id!=info.own.id||
            k.champion!=f.champion||k.version!=f.kit_version||k.stat_epoch!=f.stat_epoch||k.strength!=f.stats.strength||k.focus!=f.stats.focus||
            k.monster_shards!=f.equipment.total_shards[0]||k.jungle_shards!=f.equipment.total_shards[1]||
            f.stats.camp_incoming<=0||info.group_count<0||info.group_count>max_groups||info.altar_count<0||info.altar_count>max_altars)return false;
        if(!f.definitions.actual_purchase_receipt_parity||f.parity_receipts<3){reason_=capital::Unsupported::parity;return false;}
        const float outgoing=f.equipment.total_shards[0]>=5?1+f.definitions.item[0].passive_magnitude:1;
        const float incoming=f.equipment.total_shards[1]>=5?1-f.definitions.item[1].passive_magnitude:1;
        if(f.stats.camp_outgoing!=outgoing||f.stats.camp_incoming!=incoming){reason_=capital::Unsupported::passive;return false;}
        if(menu.count<1||menu.count>max_jobs||menu.single_count<0||menu.single_count>8||menu.single_count>menu.count){reason_=capital::Unsupported::menu;return false;}
        for(int n=0;n<menu.count;++n){const auto&j=menu.jobs[n];
            if(unsigned(j.kind)>unsigned(JobKind::hold)||(n<menu.single_count&&(j.kind!=JobKind::farm||j.second_group>=0))||
               (j.kind==JobKind::farm&&(j.group<0||j.group>=info.group_count||j.second_group>=info.group_count||j.second_group==j.group))||
               ((j.kind==JobKind::farm||j.kind==JobKind::bank)&&(j.altar<0||j.altar>=info.altar_count))){reason_=capital::Unsupported::menu;return false;}}
        for(int n=0;n<menu.count;++n)if(menu.jobs[n].kind==JobKind::farm){const auto&j=menu.jobs[n];bool first=false,second=j.second_group<0;
            for(int s=0;s<menu.single_count;++s){first|=menu.jobs[s].group==j.group;second|=menu.jobs[s].group==j.second_group;}
            if(!first||!second){reason_=capital::Unsupported::menu;return false;}}
        if(info.in_progress_group_id>=0){bool included=false;
            for(int n=0;n<menu.single_count;++n){included|=info.groups[menu.jobs[n].group].id==info.in_progress_group_id;}
            if(!included){reason_=capital::Unsupported::menu;return false;}}
        reason_=capital::Unsupported::none;return true;
    }
public:
    explicit Capital(bool enabled=true):enabled_(enabled){}
    const char*name()const{return "capital";}
    void observe_combat(const CombatKit&kit){invalidate();s_->kit=kit;}
    void observe_investment(const capital::InvestmentFrame&frame){invalidate();s_->frame=frame;}
    void evaluate_into(const Information&info,const Menu&menu,Evaluation&out){
        invalidate();++revision_;completed_=work_=0;out.menu=menu;out.chosen=-1;out.evaluated_count=0;out.budget_exhausted=false;
        if(!bind(info,menu)){plan4::evaluate_into(info,menu,out);return;}
        for(int n=0;n<menu.count;++n){
            if(info.work_budget.can_refine&&!info.work_budget.can_refine(info.work_budget.context)){reason_=capital::Unsupported::budget;out.budget_exhausted=true;out.evaluated_count=0;return;}
            capital::Process process(info,menu,s_->frame,s_->path,out.forecasts[n],s_->paths[n]);
            const bool supported=process.run(menu.jobs[n],enabled_);work_+=s_->paths[n].work;
            if(!supported){reason_=s_->paths[n].unsupported;
                if(reason_==capital::Unsupported::budget){out.budget_exhausted=true;out.evaluated_count=0;return;}
                plan4::evaluate_into(info,menu,out);return;}
            const auto&f=out.forecasts[n];unknown_+=f.stock_unknown||f.lock_unknown||f.terminal_unknown||f.abort_reason==AbortReason::unknown_travel;
            ++out.evaluated_count;++completed_;
        }
        out.chosen=choose(info,menu,out.forecasts);used_=true;s_->source=&info;s_->round=info.round;s_->actor=info.own.id;
        s_->bag=info.own.bag;s_->cash=info.own.gold;s_->menu=menu;s_->chosen=out.chosen;
        s_->position=info.own.position;s_->health=info.own.health;s_->maximum_health=info.own.maximum_health;
        s_->our_score=info.our_score;s_->enemy_score=info.enemy_score;
    }
    ModelEvidence model_evidence()const{return{true,used_,enabled_,int(reason_),s_->frame.parity_receipts,
        unknown_+(reason_==capital::Unsupported::none?0:1),completed_,work_,revision_};}
    void prediction_receipts(const Information&info,const Job&job,PredictionEventSink sink,void*context)const{
        if(!sink||!used_||s_->source!=&info||s_->round!=info.round||s_->actor!=info.own.id||s_->bag!=info.own.bag||s_->cash!=info.own.gold||s_->chosen<0||
            s_->position!=info.own.position||s_->health!=info.own.health||s_->maximum_health!=info.own.maximum_health||
            s_->our_score!=info.our_score||s_->enemy_score!=info.enemy_score)return;
        const auto&key=s_->menu.jobs[s_->chosen];
        if(key.kind!=job.kind||key.group!=job.group||key.second_group!=job.second_group||key.altar!=job.altar)return;
        const auto&prefix=s_->paths[s_->chosen];
        for(int n=0;n<prefix.count;++n){if(info.work_budget.can_refine&&!info.work_budget.can_refine(info.work_budget.context))return;
            if(!sink(prefix.receipts[n],context))return;}
    }
    bool used_capital()const{return used_;}
    capital::Unsupported unsupported()const{return reason_;}
};
} // namespace plan4::runtime
