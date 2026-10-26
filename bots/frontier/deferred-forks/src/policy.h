#pragma once
#include "model.h"
#include "../../common/src/contract.h"

namespace p10_forks {
inline const p10::Profile profile{"p10-deferred-forks",{p10::Champion::tavi,p10::Champion::vendetta,p10::Champion::marcella,p10::Champion::vesper,p10::Champion::willow}};
struct LegacyPolicy {
    Fork fork{};std::array<int,2> sources{-1,-1};
    int lastRound=-1,inspectionStart=-1,lastCommit=-1,purchasedAfter=-1,controlChosen=-1;
    bool firstIncome=false,pendingCapital=false;
    static Action advance(p10::Point from,p10::Point to){
        const int dx=(to.x>from.x)-(to.x<from.x),dy=(to.y>from.y)-(to.y<from.y);
        if(dx==0&&dy==0)return {};
        return {Atomic::move,from.x+dx,from.y+dy,-1,-1,false};
    }
    static p10::Directive convert_loaded(const p10::World&w){
        // The common-prefix policy concerns prospective work. Once that work
        // pays this actor, conversion is the next committed stage, rather than
        // another source fork that can perpetually postpone the first bank.
        const p10::Altar*best=nullptr;int cost=-1;
        for(const auto&a:w.altars)if(a.route_known&&a.travel>=0){
            const int locks=a.enemy_lock?std::max(1,a.lock_hits>0?a.lock_hits:w.lock_hits):0;
            const int candidate=a.travel+locks+w.ticks(w.self.bag);
            if(!best||candidate<cost||(candidate==cost&&a.id<best->id)){best=&a;cost=candidate;}
        }
        auto d=p10::bank(cost>=0&&cost+1<=w.remaining()
            ?"actual fork receipt commits personal travel/channel conversion before renewed work"
            :"convert actual bag now; remaining time cannot certify another productive commitment");
        if(best){d.altar=best->id;d.expected_min=w.round+cost;}
        // Future hidden locks, occupants and interruptions remain unresolved;
        // no exact upper completion bound is supplied by a route lower bound.
        d.valid_round=w.round;return d;
    }
    p10::Directive step(const p10::World&w,p10::Arm arm){
        using namespace p10;
        if(w.own_payout){firstIncome=true;pendingCapital=w.self.type==Champion::tavi;}
        if(w.own_purchase){pendingCapital=false;purchasedAfter=w.round;}
        if(w.self.channel!=Channel::none||w.capture_active)return wait("retain current receipt-producing commitment");
        if(!w.safe_now)return {Task::recover};
        if(w.own_bank_completed){sources={-1,-1};fork=Fork{};inspectionStart=-1;controlChosen=-1;}
        if(w.self.bag>0)return convert_loaded(w);
        if(arm==Arm::control&&controlChosen>=0){
            const auto*g=w.group(controlChosen);
            if(g&&usable(*g)&&g->safe_solo){
                if(pendingCapital&&w.own_gold>=w.shard_cost&&w.self.type==Champion::tavi)return buy(Item::focus,1,"checkpoint control commits its declared successor capital immediately");
                return farm(g->id,"checkpoint control retains its observed-safe early branch");
            }
            controlChosen=-1;
        }
        const Group*a=nullptr,*b=nullptr;
        for(const auto&g:w.groups)if(g.route_known&&g.program_available&&!g.respawning&&g.travel>=0){
            if(!a||g.travel<a->travel){b=a;a=&g;}else if(!b||g.travel<b->travel)b=&g;
        }
        if(!a||!b)return ordinary(w);
        if(a->id!=sources[0]||b->id!=sources[1]||w.round!=lastRound+1){
            fork=Fork{};fork.createdRound=w.round;sources={a->id,b->id};inspectionStart=-1;
        }
        lastRound=w.round;
        fork.coverage=w.geometry_complete;fork.extraDistinction=false;fork.splitInterval=false;
        if(a->monsters.size()>8||b->monsters.size()>8)fork.extraDistinction=true;
        // All eight rows remain unless a current own fact eliminates one. An
        // old HP record or an absent unseen rival does not eliminate a row.
        fork.predicates[0]={a->observed?Provenance::observed:Provenance::unknown,a->observed?w.round:-1,usable(*a)&&a->safe_solo};
        bool approachVisible=a->observed&&distance(w.self.position,a->approach)<=8;
        bool blocked=false;for(const auto&e:w.enemies)if(e.visible&&e.position==a->approach)blocked=true;
        for(const auto&e:w.allies)if(e.visible&&e.position==a->approach)blocked=true;
        fork.predicates[1]={approachVisible?Provenance::observed:Provenance::unknown,approachVisible?w.round:-1,!blocked};
        bool rival=false;int relevant=0;
        for(const auto&e:w.enemies)if(e.visible&&e.alive&&distance(e.position,a->approach)<=8){rival=true;++relevant;}
        if(relevant>1)fork.extraDistinction=true; // another necessary timing distinction exceeds this fork
        fork.predicates[2]={rival||w.opposition_complete?Provenance::observed:Provenance::unknown,(rival||w.opposition_complete)?w.round:-1,rival};
        enumerate(fork,w.round);
        const Group* groups[2]={a,b};
        for(int p=0;p<2;++p){
            auto&program=fork.skeletons[p];program=Program{};program.id=p;program.source=groups[p]->id;
            program.next[0]=advance(w.self.position,groups[p]->approach);
            const Point after{program.next[0].x,program.next[0].y};
            program.next[1]=advance(after,groups[p]->approach);
            for(int step=0;step<2;++step){
                program.useful[step]=program.next[step].kind!=Atomic::none;
                program.legal[step]=w.self.can_move&&groups[p]->route_known;
                program.safe[step]=w.safe_now&&w.enemies.empty();
            }
            // Value bounds concern secured output, with unknown lifecycle/
            // interruption explicitly retaining zero as the lower endpoint.
            program.bank={groups[p]->observed&&groups[p]->safe_solo&&w.opposition_complete?int(groups[p]->gems):0,int(groups[p]->gems),groups[p]->to_bank>=0};
            program.capital=p==0?int(Item::strength):int(Item::focus);
            for(int row=0;row<8;++row){const auto&r=fork.rows[row];fork.preference[p][row]=p==0?(r.usable&&r.direct&&!r.rival?2:0):1;}
        }
        // A loaded worker keeps both return alternatives only while useful
        // travel remains common; at divergence the real bag wins the fallback.
        const auto decision=decide(fork,arm==Arm::active);
        if(arm==Arm::control&&decision==Decision::inspect&&usable(*b)&&b->safe_solo){
            controlChosen=b->id;
            if(pendingCapital&&w.own_gold>=w.shard_cost&&w.self.type==Champion::tavi)return buy(Item::focus,1,"checkpoint control resolves safe fallback and capital before leaving");
            return farm(b->id,"strong checkpoint control commits the observed-safe alternative");
        }
        if(decision==Decision::prefix){
            const auto prefix=common_prefix(fork);const auto&action=prefix.actions[0];
            Directive d;d.task=Task::move;d.destination={action.x,action.y};d.reserve_reset=w.self.type==Champion::vendetta;d.reason="all eight retained worlds share useful next work; defer capital and sensing";d.template_id=1;d.valid_round=w.round;return d;
        }
        const Group*chosen=decision==Decision::commit_b?b:a;
        const bool observedCommit=usable(*chosen)&&chosen->safe_solo;
        if(observedCommit&&(decision==Decision::commit_a||decision==Decision::commit_b||
            (a->observed&&b->observed))){
            lastCommit=w.round;inspectionStart=-1;
            if(pendingCapital&&w.own_gold>=w.shard_cost&&w.self.type==Champion::tavi)
                return buy(chosen==a?Item::strength:Item::focus,1,"distinguishing observed successor now justifies irreversible field capital");
            return farm(chosen->id,"resolved physical fork: observed productive commitment");
        }
        if(decision==Decision::inspect){
            if(inspectionStart<0)inspectionStart=w.round;
            fork.inspectionTurns=w.round-inspectionStart;
            if(fork.inspectionTurns<4){
                Directive d;d.task=Task::inspect;d.group=a->id;d.destination=a->approach;d.reason="bounded useful inspection at actual branch disagreement";d.template_id=2;d.valid_round=w.round;return d;
            }
        }
        return ordinary(w);
    }
};
}
#include "wave.h"
