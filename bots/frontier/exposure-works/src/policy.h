#pragma once
#include "model.h"
#include "../../common/src/contract.h"

namespace p10_exposure {
inline const p10::Profile profile{"p10-exposure-works",{p10::Champion::ren,p10::Champion::willow,p10::Champion::stellar,p10::Champion::marcella,p10::Champion::tavi}};
struct LegacyPolicy {
    int source=-1,lastIncome=-1,lastBuy=-1,observedTriggers=0;
    bool witnessedIncome=false;
    static Crew crew(const p10::Actor*a){if(!a)return {};return {a->id,a->hp,a->max_hp,a->strength,a->focus,a->visible&&a->alive,a->can_cast};}
    p10::Directive step(const p10::World&w,p10::Arm arm){
        using namespace p10;
        if(w.own_payout){witnessedIncome=true;lastIncome=w.round;}
        if(w.own_purchase)lastBuy=w.round;
        if(w.self.channel!=Channel::none||w.capture_active)return wait("preserve actual capture or service");
        if(!w.safe_now)return {Task::recover};
        if(witnessedIncome&&lastBuy<lastIncome&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==Champion::tavi))
            return buy(w.self.type==Champion::ren||w.self.type==Champion::willow||w.self.type==Champion::stellar?Item::focus:Item::strength,1,"individually earned retaliation/healing/cast threshold capital");
        if(w.self.bag>0)return bank("convert actual reaction-mill payout before renewed exposure");
        if(w.self.type==Champion::marcella||w.self.type==Champion::tavi)return ordinary(w);
        const Actor*ren=w.self.type==Champion::ren?&w.self:nullptr,*willow=w.self.type==Champion::willow?&w.self:nullptr,*stellar=w.self.type==Champion::stellar?&w.self:nullptr;
        for(const auto&a:w.allies)if(a.visible&&a.alive){if(a.type==Champion::ren)ren=&a;if(a.type==Champion::willow)willow=&a;if(a.type==Champion::stellar)stellar=&a;}
        if(!ren)return ordinary(w);
        const Group*g=nullptr;
        for(const auto&candidate:w.groups)if(usable(candidate)&&candidate.travel>=0&&candidate.to_bank>=0&&
            candidate.monsters.size()<=8&&(!g||distance(candidate.position,ren->position)<distance(g->position,ren->position)))g=&candidate;
        if(!g)return ordinary(w);
        source=g->id;
        if(g->cleared){
            bool renHolds=false;for(const auto&tile:g->spawns)if(tile==ren->position)renHolds=true;
            if(w.self.id==ren->id)return farm(g->id,"Ren's actually observed future payout custody");
            if(renHolds&&ren->hp>80){Directive d;d.task=Task::release;d.group=g->id;d.destination=w.spawn;d.reason="release helper only after personally seeing actual safe holder";return d;}
            return ordinary(w);
        }
        Mill mill;mill.count=int(g->monsters.size());mill.ren=crew(ren);mill.willow=crew(willow);mill.stellar=crew(stellar);
        mill.exitLegal=g->route_known&&g->to_spawn>=0;mill.holderSafe=!g->contested&&w.enemies.empty();
        mill.travel=g->travel;mill.altar=g->to_bank;mill.capture=w.capture_turns;mill.channel=w.ticks(g->gems);
        mill.returnTravel=std::max(0,g->to_spawn);mill.respawn=g->respawn;mill.gems=int(g->gems);mill.gold=int(g->gold);mill.round=w.round;
        mill.exclusiveAggro=true;mill.ren.ready=ren->abilities[0].ready;
        mill.healArrival=willow&&willow->channel==Channel::none&&willow->abilities[0].ready&&distance(willow->position,ren->position)<=4;
        mill.willow.ready=mill.healArrival;
        if(willow&&distance(willow->position,ren->position)>4)mill.willow.visible=false;
        if(stellar&&distance(stellar->position,ren->position)>4)mill.stellar.visible=false;
        int areaSlot=-1;
        if(stellar)for(int slot=0;slot<2;++slot)if(stellar->abilities[slot].aoe){areaSlot=slot;mill.stellar.ready=stellar->abilities[slot].ready;}
        bool allInArea=stellar!=nullptr;Point center=g->monsters.empty()?g->position:g->monsters.front().position;
        for(int i=0;i<mill.count;++i){const auto&m=g->monsters[i];auto&a=mill.attackers[i];
            a.id=m.id;a.hp=m.alive?m.hp:0;a.damage=m.strength;a.range=m.range;a.observed=m.observed;
            a.inRange=distance(m.position,ren->position)<=m.range;a.stunned=m.stun>0;a.qualifying=m.strength>0;
            int nearest=distance(m.position,ren->position);a.target=ren->id;
            for(const auto&other:w.allies)if(other.visible&&other.alive&&other.id!=ren->id&&distance(m.position,other.position)<=nearest){a.target=-1;mill.exclusiveAggro=false;}
            if(w.self.id!=ren->id&&distance(m.position,w.self.position)<=nearest){a.target=-1;mill.exclusiveAggro=false;}
            for(const auto&enemy:w.enemies)if(enemy.visible&&enemy.alive&&distance(m.position,enemy.position)<=nearest){a.target=-1;mill.exclusiveAggro=false;}
            a.renReachable=distance(m.position,ren->position)<=ren->range;
            a.stellarReachable=stellar&&distance(m.position,stellar->position)<=stellar->range;
            a.willowReachable=willow&&distance(m.position,willow->position)<=willow->range;
            allInArea&=distance(m.position,center)<=1;
        }
        mill.areaHitSet=allInArea&&stellar&&distance(stellar->position,center)<=4;
        const auto program=choose(mill,arm==Arm::active);
        if(!program.legal||program.kind==Template::depart)return ordinary(w);
        const auto&action=program.steps[0];
        if(w.self.type==Champion::willow&&willow&&mill.healArrival&&ren->hp<ren->max_hp){
            Directive d;d.task=Task::heal;d.actor=ren->id;d.cast_slot=0;d.group=g->id;d.monster=action.directTarget;d.reserve_reset=true;d.reason="joint useful heal service with observed arrival and missing HP";d.template_id=int(program.kind);return d;
        }
        if(w.self.type==Champion::ren&&program.kind==Template::control_now&&w.self.abilities[0].ready){
            Directive d;d.task=Task::cast;d.group=g->id;d.monster=action.directTarget;d.cast_slot=0;d.reserve_reset=true;d.reason="camp CC wins complete safe source-to-bank program";d.template_id=int(program.kind);return d;
        }
        if(w.self.type==Champion::stellar&&program.kind==Template::area_first&&mill.areaHitSet&&areaSlot>=0&&w.self.abilities[areaSlot].ready){
            Directive d;d.task=Task::cast;d.group=g->id;d.destination=center;d.monster=action.directTarget;d.cast_slot=areaSlot;d.reserve_reset=true;d.reason="verified area hit set shapes actual source completion";d.template_id=int(program.kind);return d;
        }
        if(action.directTarget>=0){
            const Monster*target=nullptr;for(const auto&m:g->monsters)if(m.id==action.directTarget)target=&m;
            if(!target||distance(w.self.position,target->position)>w.self.range)return farm(g->id,"approach actual certified source before stationary hit scheduling");
            Directive d;d.task=Task::attack;d.group=g->id;d.monster=action.directTarget;d.reserve_reset=program.kind==Template::direct_elsewhere;d.strict_tank=true;
            d.reason=program.kind==Template::direct_elsewhere?"direct productive work elsewhere while a real safe hit finishes weak attacker":"independent strong finish with actual passive and service";
            d.template_id=int(program.kind);d.expected_min=w.round+program.bank;d.expected_max=d.expected_min+1;return d;
        }
        return farm(g->id,"ordinary joint first income when retaliation opportunity absent");
    }
};
}
#include "wave.h"
