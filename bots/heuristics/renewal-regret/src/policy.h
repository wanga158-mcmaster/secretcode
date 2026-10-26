#pragma once
#include "config.h"
#include "../../common/src/decision.h"

namespace h12_renewal_regret {
struct Policy {
    static constexpr bool enabled=HEURISTICS_ENABLED;
    static constexpr bool trace=HEURISTICS_TRACE;
    static constexpr std::array roster{1,0,4,2,3};
    struct Scenario {std::array<double,3> time;double value;};
    int current=-1,progress_round=0,best_travel=10000;
    float best_hp=std::numeric_limits<float>::infinity();

    // Three coarse stresses, not calibrated hidden-state probabilities or
    // exact forecasts. Renewal includes health spent and return from banking.
    static Scenario model(const heuristics::State&s,const heuristics::Job&j) {
        const double capacity=std::max(1.,double(s.hp)-.25*s.maximum);
        const double work=std::ceil(std::max(0.f,j.work));
        const double service=j.gems>0?2*j.altar+std::ceil((s.bag+j.gems)*s.tribute):0;
        const double renew=std::ceil((s.recall+j.home+1)*std::max(0.f,j.loss)/capacity);
        const double ordinary=1+j.travel+work+s.capture+service+renew;
        const double unseen=j.known?0:s.capture+std::min(std::max(0,j.age),s.recall);
        const double contest=j.enemy_eta<=j.travel+work+s.capture?s.capture:0;
        const double exposure=std::ceil((s.recall+j.home+1)*std::max(0.f,j.incoming)/capacity);
        const int needed=std::max(0,30*(std::max(0,5-s.monster)+std::max(0,5-s.jungle))-s.gold);
        // Only one holder is needed; nearby co-workers may split the payout.
        const double currency=needed>0?std::min(j.gold,needed):j.gems;
        return {{{std::max(1.,ordinary),std::max(1.,ordinary+unseen),
            std::max(1.,ordinary+unseen+contest+exposure)}},currency/(1+std::max(0,j.allies))};
    }

    int select(const heuristics::State&s,std::span<const heuristics::Job>jobs,bool partial) {
        // All work is O(3*n) over the executor's small detailed shortlist.
        std::array<double,3> outside{};
        std::array<Scenario,12> models;
        std::array<const heuristics::Job*,12> admitted;
        std::size_t count=0;
        for(const auto&j:jobs)if(heuristics::allowed(j,partial)) {
            const auto m=model(s,j);
            models[count]=m;admitted[count++]=&j;
            for(int k=0;k<3;++k)outside[k]=std::max(outside[k],m.value/m.time[k]);
        }
        if(!count)return -1; // A failed full-clear preview must preserve partial memory.
        auto regret=[&](const Scenario&m) {
            double result=0;
            for(int k=0;k<3;++k)result=std::max(result,outside[k]*m.time[k]-m.value);
            return result;
        };
        const double rate=*std::max_element(outside.begin(),outside.end());
        int best=-1;
        std::array<double,3> best_key{1e100,1e100,1e100};
        const heuristics::Job* incumbent=nullptr;
        double incumbent_regret=1e100;
        for(std::size_t i=0;i<count;++i) {
            const auto&j=*admitted[i];
            const auto&m=models[i];
            const double worst=regret(m);
            // When the bottleneck currency pays nothing, minimise the stressed
            // cycle rather than refusing every productive gem-only opening.
            const std::array key{rate>0?worst:m.time[2],m.time[2],double(j.id)};
            if(key<best_key){best_key=key;best=j.id;}
            if(j.id==current){incumbent=&j;incumbent_regret=rate>0?worst:m.time[2];}
        }
        if(incumbent) {
            if(incumbent->travel<best_travel||incumbent->remaining_hp<best_hp)progress_round=s.round;
            best_travel=std::min(best_travel,incumbent->travel);
            best_hp=std::min(best_hp,incumbent->remaining_hp);
            const double friction=rate>0?s.capture*rate:s.capture;
            if(s.round-progress_round<8&&incumbent_regret<=best_key[0]+friction)return current;
        }
        if(best!=current) {
            current=best;progress_round=s.round;best_travel=10000;
            best_hp=std::numeric_limits<float>::infinity();
            for(const auto&j:jobs)if(j.id==current){best_travel=j.travel;best_hp=j.remaining_hp;break;}
        }
        return best;
    }
    bool bank(const heuristics::State&s) const {
        const int batch=10*std::max(2,int(std::ceil(std::sqrt(double(std::max(0,s.altar)+1)))));
        return s.bag>0&&(heuristics::deadline(s)||s.altar<=1||s.bag>=batch
            ||(s.bag>=10&&s.hp<s.maximum*.55f));
    }
    bool retreat(float hp,float maximum,float incoming,int bag) const {
        return hp<=(bag>0?.40f:.25f)*maximum+3*incoming;
    }
    bool finance(const heuristics::State&s,unsigned health_price,unsigned combination)const {
        // Complete the controller's first health stage as one restoration trip.
        // Later finance keeps the incumbent wallet rule; field buying is Tavi's.
        if(s.identity==11)return false;
        if(s.gold>=300)return true;
        // An unsuccessful bank walk must not create a new early-finance trip.
        if(bank(s))return false;
        if(health_price==0||combination<=unsigned(s.jungle))return false;
        return static_cast<unsigned long long>(std::max(0,s.gold))>=
            static_cast<unsigned long long>(combination-s.jungle)*health_price;
    }
    fundamentals::BuildStage build(unsigned monster,unsigned jungle) const {
        if(jungle<5)return fundamentals::BuildStage::jungle_health;
        if(monster<5)return fundamentals::BuildStage::monster_strength;
        return fundamentals::BuildStage::power_strength;
    }
};
} // namespace h12_renewal_regret
