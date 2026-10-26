#pragma once
#include <array>
#include <algorithm>

namespace p10_forks {
enum class Provenance { observed,remembered,unknown };
enum class Atomic { none,move,attack,inspect,purchase,dash,bank };
struct Action {
    Atomic kind=Atomic::none;int x=-1,y=-1,target=-1,item=-1;
    bool irreversible=false;
    constexpr bool same(const Action&b)const{return kind==b.kind&&x==b.x&&y==b.y&&target==b.target&&item==b.item&&irreversible==b.irreversible;}
};
struct Fact {Provenance source=Provenance::unknown;int round=-1;bool value=false;};
struct WorldRow {
    bool usable=false,direct=false,rival=false,plausible=true;
};
struct Interval {int lower=0,upper=0;bool supported=false;};
struct Program {
    int id=-1,source=-1,altar=-1;
    std::array<Action,2> next{};
    std::array<bool,2> useful{false,false},legal{false,false},safe{false,false};
    Interval bank{};int capital=0,returnTurns=0;
};
struct Fork {
    std::array<Fact,3> predicates{};
    std::array<WorldRow,8> rows{};int rowsUsed=8;
    std::array<Program,4> skeletons{};
    std::array<std::array<int,8>,4> preference{};
    bool coverage=true,extraDistinction=false,splitInterval=false;
    int createdRound=-1,inspectionTurns=0,chosen=-1,cursor=0;
};
inline void enumerate(Fork&f,int now){
    f.rowsUsed=8;
    for(int i=0;i<8;++i){
        auto&r=f.rows[i];r={bool(i&1),bool(i&2),bool(i&4),true};
        const bool values[3]={r.usable,r.direct,r.rival};
        for(int p=0;p<3;++p)if(f.predicates[p].source==Provenance::observed&&
           f.predicates[p].round==now&&f.predicates[p].value!=values[p])r.plausible=false;
    }
}
struct Prefix {std::array<Action,2> actions{};int count=0;bool certified=false;};
inline Prefix common_prefix(const Fork&f){
    Prefix result;
    if(!f.coverage||f.extraDistinction||f.splitInterval||f.rowsUsed>8||f.rowsUsed<=0)return result;
    for(int step=0;step<2;++step){
        bool first=true,any=false;Action agreed;
        for(int row=0;row<f.rowsUsed;++row)if(f.rows[row].plausible){
            // preference is a finite ordered set. Every still-preferred useful
            // continuation must agree, including equal preferred alternatives.
            int best=-100000;
            for(int p=0;p<4;++p)if(f.skeletons[p].id>=0)best=std::max(best,f.preference[p][row]);
            for(int p=0;p<4;++p){
                const auto&program=f.skeletons[p];
                if(program.id<0||f.preference[p][row]!=best)continue;
                const auto&a=program.next[step];
                if(!program.useful[step]||!program.legal[step]||!program.safe[step]||
                   a.irreversible||a.kind==Atomic::none)return result;
                if(first){agreed=a;first=false;}else if(!a.same(agreed))return result;
                any=true;
            }
        }
        if(!any)return result;
        result.actions[result.count++]=agreed;result.certified=true;
    }
    return result;
}
inline int dominate(const Program&a,const Program&b){
    if(!a.bank.supported||!b.bank.supported)return -1;
    if(a.bank.lower>b.bank.upper)return a.id;
    if(b.bank.lower>a.bank.upper)return b.id;
    return -1;
}
enum class Decision { prefix,inspect,commit_a,commit_b,safe_fallback };
inline Decision decide(const Fork&f,bool active){
    const int dominant=dominate(f.skeletons[0],f.skeletons[1]);
    if(dominant==f.skeletons[0].id&&dominant>=0)return Decision::commit_a;
    if(dominant==f.skeletons[1].id&&dominant>=0)return Decision::commit_b;
    if(active&&common_prefix(f).count>0)return Decision::prefix;
    if(f.inspectionTurns<4&&f.coverage&&!f.extraDistinction&&!f.splitInterval)return Decision::inspect;
    return Decision::safe_fallback;
}
// Only witnessed conversion retires a fork; aging never turns memory into fact.
struct Trace {
    int commonActions=0,deferredGold=0,ownGold=0,bag=0,score=0;bool captured=false,bought=false,renewed=false;
    bool common(bool allRows){if(!allRows)return false;++commonActions;return true;}
    bool capture(int gems,int gold){if(gems<0||gold<0)return false;bag+=gems;ownGold+=gold;captured=true;return true;}
    bool purchase(int cost,bool revealed){if(!captured||!revealed||cost<=0||ownGold<cost)return false;ownGold-=cost;deferredGold=cost;bought=true;return true;}
    bool bank(int receipt){if(!captured||receipt!=bag||receipt<=0)return false;score+=receipt;bag=0;return true;}
    bool renewal(bool observed){renewed=observed&&bought&&score>0;return renewed;}
};
}
