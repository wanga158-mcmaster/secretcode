#pragma once
#include "config.h"
#include "../../common/src/decision.h"
#include "../../common/src/equipment.h"

namespace h12_splash_harvester {
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED,trace=HEURISTICS_TRACE;
    static constexpr std::array roster{1,8,9,0,4};
    struct Return {double personal,time,loss;};
    int current=-1,best_travel=10000,progress_round=0;
    float best_hp=std::numeric_limits<float>::infinity();
    static Return estimate(const heuristics::State&s,const heuristics::Job&j) {
        const double work=std::ceil(std::max(0.f,j.work));
        const double sharing=1+std::max(0,j.allies);
        const double finish=j.travel+work+s.capture+(j.known?0:s.capture);
        const double cash=finish+j.altar+std::ceil((s.bag+j.gems/sharing)*s.tribute)+1;
        const double score=cash<=s.limit-s.round?j.gems/sharing:0;
        // Personal useful equipment credit shrinks with shared gold and stops
        // once both affiliation emblems are complete.
        const double equipment=s.mature?0:std::min(30.,j.gold/sharing)/15.;
        return {score+equipment,1+cash+(j.enemy_eta<=finish?s.capture:0),
            j.loss/std::max(1.f,s.hp)+(!j.known?.25:0)};
    }
    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial) {
        std::array<const heuristics::Job*,12> list{};std::array<Return,12> values{};unsigned n=0;
        for(const auto&j:jobs)if(heuristics::allowed(j,partial)&&n<12){list[n]=&j;values[n]=estimate(s,j);++n;}
        int best=-1;double payback=1e100;
        const heuristics::Job*incumbent=nullptr;
        for(unsigned i=0;i<n;++i) {
            if(list[i]->id==current)incumbent=list[i];
            bool dominated=false;
            for(unsigned k=0;k<n;++k)if(k!=i) {
                const auto&a=values[k];const auto&b=values[i];
                if(a.personal>=b.personal&&a.time<=b.time&&a.loss<=b.loss
                    &&(a.personal>b.personal||a.time<b.time||a.loss<b.loss)){dominated=true;break;}
            }
            if(dominated)continue;
            const auto&r=values[i];
            const double cost=r.personal>0?r.time*(1+r.loss)/r.personal:r.time*(1+r.loss)+10000;
            if(cost<payback){payback=cost;best=list[i]->id;}
        }
        if(incumbent) {
            if(incumbent->travel<best_travel||incumbent->remaining_hp<best_hp)progress_round=s.round;
            best_travel=std::min(best_travel,incumbent->travel);best_hp=std::min(best_hp,incumbent->remaining_hp);
            if(s.round-progress_round<s.capture)return current;
        }
        if(best!=current){current=best;best_travel=10000;best_hp=std::numeric_limits<float>::infinity();progress_round=s.round;
            for(unsigned i=0;i<n;++i)if(list[i]->id==best){best_travel=list[i]->travel;best_hp=list[i]->remaining_hp;}}
        return best;
    }
    bool bank(const heuristics::State&s)const {
        const int lot=s.identity==5?6:s.identity==0?12:8;
        return s.bag>0&&(heuristics::deadline(s)||s.bag>=lot||s.altar<=1
            ||(s.bag>=3&&s.hp<s.maximum*.55f));
    }
    bool retreat(float hp,float maximum,float incoming,int bag)const {
        return hp<=maximum*(bag>0?.34f:.22f)+3*incoming;
    }
    static heuristics::Purchase purchase(int identity,unsigned monster,unsigned jungle) {
        using P=heuristics::Purchase;
        if(identity==5){if(monster<5)return P::monster_focus;if(jungle<5)return P::jungle_focus;return P::power_focus;}
        if(identity==0){if(monster<5)return P::monster_health;if(jungle<5)return P::jungle_health;return P::power_strength;}
        if(monster<5)return P::monster_strength;
        if(jungle<5)return P::jungle_health;
        return P::power_strength;
    }
    fundamentals::BuildStage build(unsigned monster,unsigned jungle)const {
        if(monster<5)return fundamentals::BuildStage::monster_strength;
        if(jungle<5)return fundamentals::BuildStage::jungle_health;
        return fundamentals::BuildStage::power_strength;
    }
};
} // namespace h12_splash_harvester
