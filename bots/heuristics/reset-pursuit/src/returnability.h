#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>

namespace h12_reset_pursuit {
struct BankPoint {int x=0,y=0;};
enum class BankCastKind {other,reset,damage,area,cc,dash};
struct BankCast {
    BankCastKind kind=BankCastKind::other;
    bool ready=false,exclusive=false,damages=false,stuns=false;
    int range=0,radius=0;
};
struct BankOpponent {
    BankPoint position;
    int walk=1,basic=1;
    bool melee=true;
    std::array<BankCast,2> casts{};
};

// Union of directly targeted damage/CC words before our next response. Each
// word uses at most one ordinary move and one cast. This deliberately does not
// model collateral basic splash, future purchases or unseen opponents.
inline int bank_opponent_reach(const BankOpponent& opponent) {
    int reach=opponent.basic+(opponent.melee?opponent.walk:0);
    for(const auto& cast:opponent.casts)if(cast.ready) {
        const int walk=cast.exclusive?0:opponent.walk;
        switch(cast.kind) {
        case BankCastKind::reset:
            // Ranged walking spends its basic; reset restores that basic,
            // never a second ordinary movement allowance.
            reach=std::max(reach,opponent.basic+walk);
            break;
        case BankCastKind::damage:
        case BankCastKind::area:
        case BankCastKind::cc:
            if(cast.damages||cast.stuns)
                reach=std::max(reach,cast.range+cast.radius+walk);
            break;
        case BankCastKind::dash:
            if(cast.damages||cast.stuns)
                reach=std::max(reach,cast.range+cast.radius+walk);
            // A ranged ordinary move cannot be followed by dash and basic:
            // the only cast was spent on dash, so no reset remains.
            reach=std::max(reach,cast.range+opponent.basic+(opponent.melee?walk:0));
            break;
        default:break;
        }
    }
    return reach;
}

template<class Opponents,class Draw>
bool bank_returnability_mask(BankPoint origin,const Opponents& opponents,Draw draw) {
    // An actor already inside a footprint keeps its incumbent escape/fight
    // program. Blocking every adjacent tile would prevent that escape.
    for(const auto& opponent:opponents)
        if(std::max(std::abs(origin.x-opponent.position.x),std::abs(origin.y-opponent.position.y))
                <=bank_opponent_reach(opponent))return false;
    for(const auto& opponent:opponents)draw(opponent.position,bank_opponent_reach(opponent));
    return !opponents.empty();
}
} // namespace h12_reset_pursuit
