#pragma once
#include "../../common/src/events.h"
#include "information.h"

namespace p11_surveyor {
struct Policy {
    using InformationClock=p11_surveyor::InformationClock;
    static constexpr const char* name="p11-surveyor";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;

    static double prize(const p11::State&s,const p11::Candidate&c) {
        return 2.5*std::max(0,c.gems)+(s.mature?.12:1.5)*std::max(0,c.gold);
    }
    static int finish(const p11::State&s,const p11::Candidate&c,int extra=0) {
        const int payout=std::max(0,c.travel)+(c.ready?0:std::max(0,c.clear))+std::max(0,c.capture)+extra;
        const int gems=c.gems>0?payout+std::max(0,c.altar)+p11::tribute_ticks(s.bag+c.gems,
            s.tribute_multiplier,s.tribute_base,s.tribute_minimum):0;
        // Gold pays at capture. Before the build is mature, reserve a personal
        // shop visit when field purchases are unavailable; grant no future gear.
        const int gold=c.gold>0?payout+(!s.mature&&!s.field_shop?std::max(0,s.recall)+1:0):0;
        return std::max(gems,gold);
    }
    static double continuity(const p11::Candidate&c) {
        return c.committed?(c.ready?8.:4.):0.;
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {
        // Public clear work is an integer lower-work approximation for a full
        // group. It earns shortlist consideration, never an execution promise.
        if(prize(s,c)<=0 || (!c.ready&&c.clear>=10000)) return -std::numeric_limits<double>::infinity();
        return prize(s,c)-1.5*c.payout()+continuity(c);
    }
    static bool admissible(const p11::State&s,const p11::Candidate&c) {
        if(p11::feasible(s,c))return true;
        const float incoming=c.incoming*(s.jungle_shards>=5?.7f:1.f);
        // A healthy bounded reveal/progress visit can resolve pessimistic
        // public health estimates. Known unsafe fresh fights remain excluded.
        return (!c.known||c.committed)&&(s.hp>=s.maximum*.8f||c.committed)
            && incoming>0&&s.hp>6*incoming+80;
    }
    static double score(const p11::State&s,const p11::Candidate&c) {
        if(prize(s,c)<=0 || (!c.ready&&c.clear>=10000)) return -std::numeric_limits<double>::infinity();
        const float incoming=c.incoming*(s.jungle_shards>=5?.7f:1.f);
        if(!p11::feasible(s,c)&&s.hp<=6*incoming+80) return -std::numeric_limits<double>::infinity();
        // Fog adds whole engagement/reposition phases. No probability, allied
        // damage, reset, shield or already-dead hidden monster is invented.
        const int uncertainty=c.known||c.ready?0:2+std::min(4,p11::attacks(c.hp,std::max(1.f,s.strength)*8.f));
        const int completion=finish(s,c,uncertainty);
        if(completion>s.limit-s.round) return -std::numeric_limits<double>::infinity();
        const double maximum=std::max(1.f,s.maximum);
        const double exposure=c.ready?0.:std::max(0.f,c.loss)+(c.known?0.:2.*std::max(0.f,c.incoming));
        const double reserve_shortfall=std::max(0.,maximum*.35-(s.hp-exposure));
        // Conservative completion surplus, not a continuous DPS/rate or a
        // recurring-source forecast. Actual detailed exposure may reverse the
        // public screen. Costs remain finite for the executor's partial fallback.
        return prize(s,c)-1.5*completion-70.*exposure/maximum-90.*reserve_shortfall/maximum
            -12.*std::max(0,c.allies)+continuity(c);
    }
    static bool bank(const p11::State&s) {
        const int ticks=p11::tribute_ticks(s.bag,s.tribute_multiplier,s.tribute_base,s.tribute_minimum);
        return s.bag>0&&(s.bag>=24||s.altar<=2||s.limit-s.round<=s.altar+ticks+4);
    }
    static bool retreat(float hp,float maximum,float incoming) {
        return hp<=maximum*.33f||hp<=std::max(0.f,incoming)*3+40;
    }
};
}
