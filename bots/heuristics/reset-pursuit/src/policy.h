#pragma once
#include "config.h"
#include "../../common/src/decision.h"
#if RESET_MENU_DIAGNOSTIC
#include "menu-diagnostic.h"
#endif

namespace h12_reset_pursuit {
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED;
    static constexpr bool trace=HEURISTICS_TRACE;
    static constexpr std::array roster{2,0,4,1,3};
    struct Model {double finish,cash,value;bool cashable,quick;};
    int current=-1,progress_round=0,best_travel=10000;
    float best_hp=std::numeric_limits<float>::infinity();
#if RESET_MENU_DIAGNOSTIC
    MenuDiagnostic menu_diagnostic{};
#endif
    static Model model(const heuristics::State&s,const heuristics::Job&j) {
        // Completion thresholds are coarse: no hidden kill schedule is assumed.
        const double work=std::ceil(std::max(0.f,j.work));
        const double uncertainty=j.known?0:std::min(s.capture,std::max(1,j.age));
        const double finish=1+j.travel+work+s.capture+uncertainty;
        const double cash=finish+j.altar+std::ceil((s.bag+j.gems)*s.tribute)+1;
        const bool cashable=cash<=s.limit-s.round;
        // Gold buys the NEXT useful strength purchase; it is never a compulsory
        // personal funding phase. Gems retain value before the first emblem.
        const double equipment=s.monster<5?std::min(j.gold,30):0;
        const double value=(cashable?j.gems:0)+equipment/30.;
        return {finish,cash,value/(1+std::max(0,j.allies)),cashable,
            j.ready||(j.known&&j.travel<=2&&work<=2)};
    }
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial) {
#if RESET_MENU_DIAGNOSTIC
        menu_diagnostic.open_selection(partial,int(jobs.size()),{current,progress_round,best_travel,best_hp});
#endif
        std::array<Model,12> models;
        std::array<const heuristics::Job*,12> admitted;
        std::size_t count=0;bool has_cash=false,has_quick=false;
        for(const auto&j:jobs)if(heuristics::allowed(j,partial)&&count<models.size()) {
            models[count]=model(s,j);admitted[count]=&j;
            has_cash|=models[count].cashable&&j.gems>0;
            has_quick|=models[count].quick&&models[count].value>0;
            ++count;
        }
#if RESET_MENU_DIAGNOSTIC
        menu_diagnostic.admission(int(count),has_cash,has_quick);
#endif
        int best=-1;std::array<double,4> best_key{1e100,1e100,1e100,1e100};
        const heuristics::Job* incumbent=nullptr;
        for(std::size_t i=0;i<count;++i) {
            const auto&j=*admitted[i];const auto&m=models[i];
            // Prefer cashable work near the deadline, but keep a productive
            // fallback even when no reward can be banked before the last turn.
            const double late=has_cash&&!m.cashable?1:0;
            const double quick=has_quick&&!m.quick?1:0;
            const double threat=j.enemy_eta<=m.finish?s.capture+std::max(0.f,j.incoming):0;
            const double cost=m.value>0?(m.finish+threat+std::max(0.f,j.loss)/std::max(1.f,s.hp))/m.value:m.finish+threat;
            const std::array key{late,quick,cost,double(j.id)};
#if RESET_MENU_DIAGNOSTIC
            menu_diagnostic.rank(j.id,m.finish,m.cash,m.value,m.cashable,m.quick,key);
#endif
            if(key<best_key){best_key=key;best=j.id;}
            if(j.id==current)incumbent=&j;
        }
        if(incumbent) {
            const bool progress=incumbent->travel<best_travel||incumbent->remaining_hp<best_hp;
            if(progress)progress_round=s.round;
            best_travel=std::min(best_travel,incumbent->travel);
            best_hp=std::min(best_hp,incumbent->remaining_hp);
            // Allow travel to finish too: reacting to nearby ally counts on
            // every step otherwise reverses competing routes indefinitely.
            // A quick completion can still interrupt travel. Lack of NEW best
            // progress expires the commitment after one capture-time window.
            const auto m=model(s,*incumbent);
            if(s.round-progress_round<s.capture&&(m.quick||!has_quick)
                &&m.value>0&&(!has_cash||m.cashable))
#if RESET_MENU_DIAGNOSTIC
            {menu_diagnostic.returned(current,best,true,{current,progress_round,best_travel,best_hp});return current;}
#else
                return current;
#endif
        }
        if(best!=current){
            current=best;progress_round=s.round;best_travel=10000;best_hp=std::numeric_limits<float>::infinity();
            for(std::size_t i=0;i<count;++i)if(admitted[i]->id==current){best_travel=admitted[i]->travel;best_hp=admitted[i]->remaining_hp;break;}
        }
#if RESET_MENU_DIAGNOSTIC
        menu_diagnostic.returned(best,best,false,{current,progress_round,best_travel,best_hp});
#endif
        return best;
    }
    bool bank(const heuristics::State&s) const {
        return s.bag>0&&(heuristics::deadline(s)||s.altar<=2||s.bag>=10
            ||(s.bag>=4&&s.hp<s.maximum*.60f));
    }
    bool retreat(float hp,float maximum,float incoming,int bag) const {
        return hp<=(bag>=4?.38f:.25f)*maximum+2*incoming;
    }
    fundamentals::BuildStage build(unsigned monster,unsigned jungle) const {
        if(monster<5)return fundamentals::BuildStage::monster_strength;
        if(jungle<5)return fundamentals::BuildStage::jungle_health;
        return fundamentals::BuildStage::power_strength;
    }
};
} // namespace h12_reset_pursuit
