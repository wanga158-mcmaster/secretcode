#pragma once
#include "model.h"
#include "../../common/src/contract.h"

namespace p10_envelope {
inline const p10::Profile profile{"p10-envelope-draft",{p10::Champion::willow,p10::Champion::varg,p10::Champion::marcella,p10::Champion::vesper,p10::Champion::tavi}};
inline p10::Champion champion(Kit k){using C=p10::Champion;switch(k){case Kit::willow:return C::willow;case Kit::varg:return C::varg;case Kit::marcella:return C::marcella;case Kit::vesper:return C::vesper;case Kit::tavi:return C::tavi;case Kit::vendetta:return C::vendetta;case Kit::heart:return C::heart;case Kit::ren:return C::ren;case Kit::prism:return C::prism;case Kit::cassi:return C::cassi;case Kit::issac:return C::issac;}return C::other;}
inline p10::Profile roster(int id){
    using C=p10::Champion;
    if(id==1)return {"p10-envelope-draft",{C::vendetta,C::heart,C::marcella,C::ren,C::tavi}};
    if(id==2)return {"p10-envelope-draft",{C::tavi,C::prism,C::vesper,C::willow,C::marcella}};
    if(id==3)return {"p10-envelope-draft",{C::cassi,C::willow,C::marcella,C::vesper,C::ren}};
    return profile;
}
struct LegacyPolicy {
    int selected=0;Envelope assigned{};OperatingPackage organization=package(0,3);
    bool firstPayout=false,bought=false;int assignedGroup=-1;
    using FuelReader=int(*)();FuelReader fuel_reader=nullptr;int fresh_trajectories=0;
    void set_fuel_reader(FuelReader reader){fuel_reader=reader;}
    // Gameplay construction must not replay draft geometry or all four package
    // evaluations in every physical actor. Recognize the actual draft, including
    // either representation of the linked selection, using only these small IDs.
    p10::Profile fastrestore(const std::vector<p10::Champion>&actual){
        using C=p10::Champion;
        std::array<C,5> picks{C::other,C::other,C::other,C::other,C::other};int count=0;
        for(auto c:actual){if(c==C::issac)c=C::cassi;
            if(std::find(picks.begin(),picks.begin()+count,c)==picks.begin()+count&&count<5)picks[count++]=c;
        }
        selected=-1;assigned={};assignedGroup=-1;
        for(int id=0;id<4;++id){const auto expected=roster(id);bool matches=count>=3&&count<=5;
            for(int i=0;i<count&&matches;++i)matches=std::find(picks.begin(),picks.begin()+count,expected.draft[i])!=picks.begin()+count;
            if(matches){selected=id;organization=package(id,count);break;}
        }
        return {profile.slug,picks};
    }
    static Design design(const p10::Actor&a,bool current){
        Design v;v.hp=current?a.hp:a.max_hp;v.strength=a.strength;v.focus=a.focus;v.range=a.range;
        v.strengthGrowth=a.strength_growth;v.focusGrowth=a.focus_growth;
        v.splash=a.type==p10::Champion::varg;v.retaliate=a.type==p10::Champion::ren;
        for(const auto&ability:a.abilities)if(!current||ability.ready){
            v.reset|=ability.reset;v.heal|=ability.heal;
            if(ability.shield){v.shieldModifier=ability.modifier;v.shieldDuration=ability.duration;}
            if(ability.damage){v.damageModifier=ability.modifier;v.areaRadius=ability.radius;v.damageExclusive=ability.exclusive;v.damageRange=ability.range;}
        }
        return v;
    }
    static Source source(const p10::Group&g,bool current){
        Source s;s.id=g.id;s.travel=g.travel;s.altarTravel=g.to_bank;s.spawnTravel=std::max(0,g.to_spawn);
        s.respawn=g.respawn;s.gems=int(g.gems);s.gold=int(g.gold);s.count=int(g.monsters.size());
        s.route=g.route_known&&g.travel>=0&&g.to_bank>=0;s.freeHold=!g.contested;s.exit=g.to_spawn>=0;
        s.positioned=true;s.leadX=g.approach.x;s.leadY=g.approach.y;
        p10::Point helper=g.approach;double least=std::numeric_limits<double>::max();
        for(auto pad:g.approach_pads)if(!(pad==g.approach)&&p10::distance(pad,g.approach)<=3){
            double damage=0;for(const auto&m:g.monsters)if(p10::distance(pad,m.position)<=m.range)damage+=m.strength;
            if(damage<least){least=damage;helper=pad;}
        }
        s.helperPad=!(helper==g.approach);s.helperX=helper.x;s.helperY=helper.y;
        if(s.count<=8)for(int i=0;i<s.count;++i){const auto&m=g.monsters[i];s.monsters[i]={current?(m.alive?m.hp:0):(m.max_hp>0?m.max_hp:m.hp),m.strength,m.range,m.position.x,m.position.y};}
        return s;
    }
    p10::Profile draft(const p10::DraftContext&d,p10::Arm arm){
        std::array<Source,6> sources{};int count=0;
        std::vector<const p10::Group*> screened;
        for(const auto&g:d.groups)if(g.route_known&&g.travel>=0)screened.push_back(&g);
        std::sort(screened.begin(),screened.end(),[](const auto*a,const auto*b){return a->travel!=b->travel?a->travel<b->travel:a->id<b->id;});
        for(const auto*ptr:screened){if(count>=6)break;const auto&g=*ptr;
            sources[count++]=source(g,false);
        }
        auto get=[&](Kit k){Design v;
            for(const auto&a:d.champions)if(a.type==champion(k)){
                v=design(a,false);
                break;
            }
            return v;
        };
        std::array<Envelope,4> envelopes{};
        for(int id=0;id<4;++id)envelopes[id]=evaluate(package(id,d.picks),sources,count,get);
        selected=select(envelopes,arm==p10::Arm::active,0);assigned=envelopes[selected];organization=package(selected,d.picks);
        return roster(selected);
    }
    void instantiate(const p10::World&w,int duty){
        using namespace p10;
        if(selected<0||duty<0||assigned.circuitsUsed>0)return;
        int lead=duty,helper=-1;
        if(organization.duties[duty].partner>=0)lead=organization.duties[duty].partner;
        if(organization.paired&&duty<=1){lead=1;helper=0;}
        else for(int i=0;i<organization.actors;++i)if(organization.duties[i].partner==lead)helper=i;
        const Actor*la=w.self.type==champion(organization.duties[lead].kit)?&w.self:nullptr;
        const Actor*ha=helper>=0&&w.self.type==champion(organization.duties[helper].kit)?&w.self:nullptr;
        for(const auto&a:w.allies)if(a.visible&&a.alive){if(a.type==champion(organization.duties[lead].kit))la=&a;if(helper>=0&&a.type==champion(organization.duties[helper].kit))ha=&a;}
        if(!la)return;
        // Stable small appointment: each independently productive duty takes a
        // different source from the six nearest static candidates. A helper
        // uses its lead's appointment and never credits an unseen actor.
        std::array<const Group*,6> groups{};int n=0;
        for(const auto&g:w.groups)if(g.route_known&&g.program_available&&!g.respawning&&!g.contested&&g.travel>=0&&g.to_bank>=0&&g.monsters.size()<=8){
            int at=0;while(at<n&&(groups[at]->travel<g.travel||(groups[at]->travel==g.travel&&groups[at]->id<g.id)))++at;
            if(at>=6)continue;
            for(int j=std::min(n,5);j>at;--j)groups[j]=groups[j-1];
            groups[at]=&g;n=std::min(6,n+1);
        }
        if(n==0)return;
        int ordinal=0;for(int i=0;i<lead;++i)if(organization.duties[i].partner<0&&!(organization.paired&&i==0))++ordinal;
        for(int attempt=0;attempt<std::min(2,n);++attempt){const auto&g=*groups[(ordinal+attempt)%n];
            if(!g.observed||!g.complete){Circuit c;c.source=g.id;c.lead=lead;c.helper=ha?helper:-1;assigned.circuits[0]=c;assigned.circuitsUsed=1;return;}
            const auto s=source(g,true);const auto ld=design(*la,true);Design hd;if(ha)hd=design(*ha,true);
            Circuit best;
            for(int mode=0;mode<2;++mode){const auto c=certify(s,ld,ha?&hd:nullptr,lead,ha?helper:-1,mode,w.capture_turns,w.tribute_multiplier,w.tribute_base,w.tribute_minimum,w.shard_cost);
                if(c.feasible&&(!best.feasible||c.firstBank<best.firstBank))best=c;
            }
            if(best.feasible){assigned.circuits[0]=best;assigned.circuitsUsed=1;return;}
        }
    }
    p10::Directive step(const p10::World&w,p10::Arm){
        using namespace p10;
        fresh_trajectories=0;
        if(w.own_payout)firstPayout=true;
        if(w.own_purchase)bought=true;
        if(w.self.channel!=Channel::none||w.capture_active)return wait("retain actual holder or personal service");
        if(!w.safe_now)return {Task::recover};
        if(firstPayout&&!bought&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==Champion::tavi)){
            const bool caster=w.self.type==Champion::prism||w.self.type==Champion::willow||w.self.type==Champion::cassi;
            return buy(caster?Item::focus:Item::strength,1,"package member spends its own first income");
        }
        if(w.self.bag>0){
            if(selected==3&&(w.self.type==Champion::cassi||w.self.type==Champion::issac)){
                const Champion twin=w.self.type==Champion::cassi?Champion::issac:Champion::cassi;
                const Actor*partner=nullptr;for(const auto&a:w.allies)if(a.type==twin&&a.visible&&a.alive)partner=&a;
                if(!partner){Directive d;d.task=Task::recover;d.reason="linked carried-bag contact lost: safe recovery";return d;}
            }
            return bank("selected package converts its actual personal bag");
        }
        if(firstPayout&&!bought&&w.own_gold>=w.shard_cost&&!w.self.at_spawn&&w.self.type!=Champion::tavi){
            Directive d;d.task=Task::recover;d.reason="return after the bank to invest this member's own first income";return d;
        }
        int duty=-1;for(int i=0;i<organization.actors;++i)if(champion(organization.duties[i].kit)==w.self.type)duty=i;
        instantiate(w,duty);
        const Circuit*program=nullptr;
        for(int i=0;i<assigned.circuitsUsed;++i){const auto&c=assigned.circuits[i];if(c.lead==duty||c.helper==duty){program=&c;break;}}
        if(program){
            assignedGroup=program->source;const auto*g=w.group(program->source);
            if(!g||!g->route_known||!g->program_available){assigned.circuitsUsed=0;return ordinary(w);}
            if(!g->observed){Directive d;d.task=Task::inspect;d.group=g->id;d.destination=g->approach;d.reason="verify selected first-income/renewal circuit privately";return d;}
            if(g->contested||g->respawning){assigned.circuitsUsed=0;return ordinary(w);}
            const auto leadKit=champion(organization.duties[program->lead].kit);
            const p10::Actor*presentLead=w.self.type==leadKit?&w.self:nullptr;
            const p10::Actor*presentHelper=nullptr;
            const auto helperKit=program->helper>=0?champion(organization.duties[program->helper].kit):Champion::other;
            if(w.self.type==helperKit)presentHelper=&w.self;
            for(const auto&a:w.allies)if(a.visible&&a.alive){if(a.type==leadKit)presentLead=&a;if(a.type==helperKit)presentHelper=&a;}
            if(program->helper>=0&&(!presentLead||!presentHelper))return ordinary(w);
            if(g->cleared)return farm(g->id,"selected package retains its actual payout appointment");
            if(program->helper>=0){const auto appointment=source(*g,true);
                const Point pad=program->helper==duty?Point{appointment.helperX,appointment.helperY}:Point{appointment.leadX,appointment.leadY};
                if(!(w.self.position==pad)){double hazard=0;for(const auto&m:g->monsters)if(m.alive&&distance(pad,m.position)<=m.range)hazard+=m.strength;
                    if(hazard*3+std::max(30.,w.self.max_hp*.1)>=w.self.hp)return ordinary(w);
                    Directive d;d.task=Task::move;d.destination=pad;d.reason="approach the selected visible crew appointment";return d;
                }
            }
            if(program->helper==duty){
                const Actor*lead=presentLead;
                if(!lead)return ordinary(w); // No unseen crew or hypothetical heal.
                for(int slot=0;slot<2;++slot)if(w.self.abilities[slot].heal&&w.self.abilities[slot].ready&&
                    lead->hp<lead->max_hp&&distance(w.self.position,lead->position)<=w.self.abilities[slot].range){Directive d;d.task=Task::heal;d.actor=lead->id;d.cast_slot=slot;d.reason="selected package's actually present healing service";return d;}
                for(const auto&a:w.self.abilities)if(a.heal)return wait("retain the selected healer's safe service pad");
            }
            if(program->helper>=0&&!g->safe_solo){
                // Reinstantiate only the chosen program, against actual crew
                // positions and personal health. Lost contact never borrows
                // a draft helper or the other actor's private observation.
                Source live;live.id=g->id;live.travel=g->travel;live.altarTravel=g->to_bank;live.spawnTravel=g->to_spawn;
                live.respawn=g->respawn;live.gems=int(g->gems);live.gold=int(g->gold);live.count=int(g->monsters.size());
                live.route=g->route_known;live.freeHold=!g->contested;live.exit=g->to_spawn>=0;live.positioned=true;
                live.leadX=presentLead->position.x;live.leadY=presentLead->position.y;live.helperX=presentHelper->position.x;live.helperY=presentHelper->position.y;
                if(live.count>8)return ordinary(w);
                for(int i=0;i<live.count;++i){const auto&m=g->monsters[i];live.monsters[i]={m.alive?m.hp:0,m.strength,m.range,m.position.x,m.position.y};}
                const int fresh=fuel_reader?fuel_reader():w.fuel;
                // No unfinished forecast may consume the action reserve. This
                // conservative guard is a source bound, not native fuel proof.
                const int phases=std::min(96,8+int(std::ceil(g->hp/std::max(1.,presentLead->strength))));
                if(fresh_trajectories>=2||fresh<std::max(w.action_reserve,w.entry_fuel*40/100)+3000+90*phases*live.count)return ordinary(w);
                ++fresh_trajectories;
                const auto actualLead=design(*presentLead,true),actualHelper=design(*presentHelper,true);
                const auto checked=run(live,actualLead,&actualHelper,program->lead,program->helper,program->templateId,w.capture_turns,w.tribute_multiplier,w.tribute_base,w.tribute_minimum,w.shard_cost);
                if(!checked.feasible)return ordinary(w);
            }
            auto d=farm(g->id,"execute selected cold-start/renewal program");d.template_id=selected*2+program->templateId;
            for(int slot=0;slot<2;++slot)if(program->templateId==1&&w.self.abilities[slot].ready&&
                (w.self.abilities[slot].shield||w.self.abilities[slot].damage)){d.cast_slot=slot;break;}
            if(program->templateId==0){
                const p10::Monster*target=nullptr;for(const auto&m:g->monsters)if(m.alive&&(!target||m.strength>target->strength))target=&m;
                if(target){d.task=Task::attack;d.monster=target->id;}
            }
            // The explicit pad approach above has already completed. Preserve
            // that certified crew stance while attacking or serving the lead.
            d.strict_tank=program->helper>=0;return d;
        }
        // Excluded or changed nominal premise leaves this member with a real
        // observed-safe production fallback, never a substitute champion.
        return ordinary(w);
    }
};
}
#include "wave.h"
