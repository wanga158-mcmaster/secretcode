#pragma once
#include <array>

namespace h12_active_surveyor {
// Owner-start positions, independent of the selected source. A changing job
// cannot make repeated physical A/B movement count as fresh route progress.
class RouteCycle {
    struct Position {
        int x=0,y=0;
        friend bool operator==(const Position&,const Position&)=default;
    };
    std::array<Position,5> positions_{};
    int count_=0,last_round_=-1;
    bool claimed_=false;

    bool start(int round,Position p) {
        reset();positions_[0]=p;count_=1;last_round_=round;return false;
    }
public:
    void reset() {count_=0;last_round_=-1;claimed_=false;}
    void work() {reset();}
    void payout() {reset();}

    bool observe(int round,int x,int y) {
        const Position p{x,y};
        if(count_==0)return start(round,p);
        // Repeated queries in one owner turn cannot advance the watchdog.
        if(round==last_round_)return stalled();
        if(round!=last_round_+1)return start(round,p);

        const auto first=positions_[0];
        Position second=first;
        bool has_second=false,seen=false;
        for(int n=0;n<count_;++n) {
            seen|=positions_[n]==p;
            if(positions_[n]!=first){second=positions_[n];has_second=true;}
        }
        // A third tile ends an unproductive two-tile pattern. After an escape
        // claim, any genuinely new tile starts a new physical attempt (also
        // allowing departure from a stationary, one-tile cycle).
        if((has_second&&p!=first&&p!=second)||(claimed_&&!seen))return start(round,p);

        if(count_<5)positions_[count_++]=p;
        else {
            for(int n=0;n<4;++n)positions_[n]=positions_[n+1];
            positions_[4]=p;
        }
        last_round_=round;
        return stalled();
    }

    bool stalled()const {
        return count_==5&&positions_[0]==positions_[2]
            &&positions_[1]==positions_[3]&&positions_[2]==positions_[4];
    }
    // Only one source can claim an escape in this physical cycle. An
    // alternating selector cannot quarantine both sources from one witness.
    bool claim_escape() {
        if(!stalled()||claimed_)return false;
        claimed_=true;return true;
    }
};
}
