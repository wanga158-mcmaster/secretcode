#pragma once
#include "contract.h"

namespace p9::live {
struct BankSample {
    int round=-1,remaining=-1,respawn=0;
    bool alive=false,channel=false;
    Gems bag=0,score=0;
};
struct BankReceipt { bool completed=false;Gems lower=0,upper=-1; };
class PersonalBank {
    BankSample last_{};
    Gems largest_=0;
    bool active_=false;
public:
    void invalidate(){active_=false;largest_=0;}
    bool begin(BankSample before,BankSample after,int ticks) {
        if(active_||before.round<0||after.round<0||before.score<0||after.score<0||!before.alive||!after.alive||before.channel||!after.channel||before.bag<=0||
           before.round!=after.round||before.respawn<2||before.respawn!=after.respawn||
           after.remaining!=ticks||ticks<1||after.bag<before.bag||after.score<before.score)return false;
        last_=after;largest_=after.bag;active_=true;return true;
    }
    BankReceipt observe(BankSample now) {
        if(!active_)return {};
        const int elapsed=now.round-last_.round;
        if(!now.alive||now.round<0||now.bag<0||now.score<0||now.respawn!=last_.respawn||elapsed<0||elapsed>1||now.score<last_.score) {invalidate();return {};}
        const int expected=last_.remaining-elapsed;
        if(now.channel) {
            if(expected<1||now.remaining!=expected||now.bag<last_.bag) {invalidate();return {};}
            largest_=std::max(largest_,now.bag);last_=now;return {};
        }
        BankReceipt out;
        if(elapsed==1&&expected==0&&now.bag==0&&now.score-last_.score>=largest_)
            out={true,largest_,now.score-last_.score};
        invalidate();return out;
    }
};
} // namespace p9::live
