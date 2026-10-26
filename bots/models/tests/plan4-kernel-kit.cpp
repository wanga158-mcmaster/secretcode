#include "../kernel/src/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
namespace kb=plan4::runtime::kernel_binding;
namespace rt=plan4::runtime;
namespace {
int checks=0;
void check(bool okay,const char* message){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
rt::CombatKit combat(){return {{10,{7}},3,1,50,20,0,0,55,2,true};}
struct Reader {
    kb::OwnKitFacts body{{10,{7}},3,1,50,20};
    std::array<std::uint32_t,256> counts{};
    std::array<int,2> kinds{2,7};
    int count=64,denied_at=-1,checks=0,own_reads=0,ability_reads=0,inventory_reads=0,reads_after_denial=0;
    bool own_known=true,first_known=true,second_known=true,inventory_known=true,complete=true,denied=false,null_counts=false;
    bool available(){++checks;denied=denied_at>0&&checks>=denied_at;return !denied;}
    bool own(kb::OwnKitFacts& out){own_reads++;reads_after_denial+=denied;out=body;return own_known;}
    bool ability(int slot,int& out){ability_reads++;reads_after_denial+=denied;out=kinds[slot];return slot?second_known:first_known;}
    bool inventory(kb::InventoryFacts& out){inventory_reads++;reads_after_denial+=denied;out={null_counts?nullptr:counts.data(),count,complete};return inventory_known;}
};
void unknown(const kb::KitCollection& c){check(!c.frame.kit.known&&!c.frame.complete_inventory&&!c.frame.abilities_known&&
    c.frame.kit.item_count==0&&c.frame.provenance.status!=Status::exact_now,"failed collection publishes no partial exact kit");}
}
int main(){
    {kb::KitPhaseBudget b;b.begin(200000);
        check(b.floor==196000&&b.permits(196500)&&!b.permits(196499),"first-turn extra fuel does not enlarge4000 collection allowance or500 atomic margin");
        b.begin(56000);check(b.floor==55000&&b.permits(55500)&&!b.permits(55499),"near-reserve collection preserves common M0 headroom");
        b.begin(54000);check(!b.permits(54000),"insufficient entry fuel cannot admit collection");}
    {Reader r;r.counts[17]=3;r.counts[2]=7;kb::KitCollection c;kb::collect_exact_kit(combat(),r,c);
        check(c.failure==kb::KitFailure::none&&c.frame.kit.known&&c.frame.complete_inventory&&c.frame.abilities_known,"both immutable slots and complete own inventory form an exact current kit");
        check(c.frame.clock.round==10&&c.frame.clock.own.id==7&&c.frame.kit_version==55&&c.frame.stat_epoch==2&&
            c.frame.provenance.source==Source::own_state&&c.frame.provenance.status==Status::exact_now,"kit provenance retains current actor/round and actual common kit version");
        check(c.frame.kit.items[0]==kernel::Item{2,7}&&c.frame.kit.items[1]==kernel::Item{17,3}&&c.frame.kit.item_count==2,"full catalog is sparse-copied in canonical enum order with exact quantities");
        check(c.frame.kit.first_ability==2&&c.frame.kit.second_ability==7,"ordered slot identity is preserved rather than sorted");
        check(r.own_reads==1&&r.ability_reads==2&&r.inventory_reads==1&&c.reads==4&&c.scanned_items==64,"collection uses one body, two cached slot reads and one complete inventory read");
        r.counts[2]=1;check(c.frame.kit.items[0].count==7,"snapshot owns its item values after the borrowed SDK catalog changes");
    }
    {Reader r;r.kinds={0,7};kb::KitCollection c;kb::collect_exact_kit(combat(),r,c);
        check(c.failure==kb::KitFailure::none&&c.frame.kit.first_ability==0,"documented unused slot is known zero rather than unknown metadata");
        check(c.frame.kit.item_count==0&&c.frame.complete_inventory,"zero owned items still requires a complete nonempty catalog");}
    for(int defect=0;defect<7;++defect){Reader r;
        if(defect==0)r.first_known=false;
        if(defect==1)r.second_known=false;
        if(defect==2)r.kinds[0]=-1;
        if(defect==3)r.kinds[1]=8;
        if(defect==4)r.kinds[1]=std::numeric_limits<int>::max();
        if(defect==5)r.kinds[0]=-5;
        if(defect==6)r.kinds[0]=8;
        kb::KitCollection c;kb::collect_exact_kit(combat(),r,c);unknown(c);
        check(c.failure==kb::KitFailure::ability&&r.inventory_reads==0,"unknown/malformed slot cannot be relabelled empty or read an unnecessary inventory");}
    for(int defect=0;defect<7;++defect){Reader r;
        if(defect==0)r.own_known=false;
        if(defect==1)r.body.clock.round=9;
        if(defect==2)r.body.clock.own.id=8;
        if(defect==3)r.body.champion=4;
        if(defect==4)r.body.strength=51;
        if(defect==5)r.body.attack_range=2;
        if(defect==6)r.body.focus=std::numeric_limits<float>::quiet_NaN();
        kb::KitCollection c;kb::collect_exact_kit(combat(),r,c);unknown(c);
        check(c.failure==kb::KitFailure::body&&r.ability_reads==0&&r.inventory_reads==0,"changed own source is rejected before slot/inventory reads");}
    for(int defect=0;defect<6;++defect){Reader r;auto b=combat();
        if(defect==0)b.exact=false;
        if(defect==1)b.version=0;
        if(defect==2)b.clock.round=-1;
        if(defect==3)b.clock.own.id=-1;
        if(defect==4)b.strength=-1;
        if(defect==5)b.focus=std::numeric_limits<float>::infinity();
        kb::KitCollection c;kb::collect_exact_kit(b,r,c);unknown(c);
        check(c.failure==kb::KitFailure::source&&r.checks==0&&r.own_reads==0,"unusable common kit does no additional own-source reads");}
    for(int defect=0;defect<6;++defect){Reader r;
        if(defect==0)r.inventory_known=false;
        if(defect==1)r.complete=false;
        if(defect==2)r.count=0;
        if(defect==3)r.count=-1;
        if(defect==4)r.count=kb::inventory_catalog_limit+1;
        if(defect==5)r.null_counts=true;
        kb::KitCollection c;kb::collect_exact_kit(combat(),r,c);unknown(c);
        check(c.failure==kb::KitFailure::inventory&&c.scanned_items==0,"missing/truncated/oversized catalog does not manufacture inventory absence");}
    {Reader r;for(int n=0;n<32;++n)r.counts[n]=n+1;kb::KitCollection c;kb::collect_exact_kit(combat(),r,c);
        check(c.failure==kb::KitFailure::none&&c.frame.kit.item_count==32,"exact kit admits its full distinct-item capacity");
        r.counts[33]=1;kb::collect_exact_kit(combat(),r,c);unknown(c);
        check(c.failure==kb::KitFailure::capacity,"one extra distinct item rejects the whole kit instead of silently truncating");}
    {Reader r;r.count=256;kb::KitCollection c;kb::collect_exact_kit(combat(),r,c);
        check(c.failure==kb::KitFailure::none&&c.scanned_items==256&&r.checks==25,"maximum catalog remains bounded by256 entries and25 outer budget checkpoints");}
    for(int denied=1;denied<=13;++denied){Reader r;r.denied_at=denied;kb::KitCollection c;kb::collect_exact_kit(combat(),r,c);unknown(c);
        check(c.failure==kb::KitFailure::budget&&r.reads_after_denial==0&&r.own_reads<=1&&r.ability_reads<=2&&r.inventory_reads<=1,
            "budget denial at every source/prefix boundary stops without extra queries or partial publication");}
    {Reader r;kb::KitCollection c;kb::collect_exact_kit(combat(),r,c);check(c.frame.kit.known,"first complete kit is usable");
        r.second_known=false;kb::collect_exact_kit(combat(),r,c);unknown(c);
        check(c.frame.clock.round==10&&c.failure==kb::KitFailure::ability,"later failed refresh cannot retain earlier exact kit evidence");}
    {Reader a,b;a.counts[8]=b.counts[8]=5;kb::KitCollection active,control;
        kb::collect_exact_kit(combat(),a,active);kb::collect_exact_kit(combat(),b,control);
        check(active.frame.kit==control.frame.kit&&a.own_reads==b.own_reads&&a.ability_reads==b.ability_reads&&a.inventory_reads==b.inventory_reads,
            "collection has no coupling-enabled branch or extra active-arm observations");
        for(bool enabled:{false,true}){auto model=std::make_unique<rt::Kernel>(enabled);model->observe_exact_kit(active.frame);
            auto info=std::make_unique<Information>();info->round=10;info->own.id=7;info->own.health=info->own.maximum_health=1000;
            Menu menu;menu.count=1;menu.jobs[0].kind=JobKind::hold;auto result=std::make_unique<Evaluation>();
            model->evaluate_into(*info,menu,*result);
            check(result->evaluated_count==1&&!model->model_evidence().supported&&!model->model_evidence().coupling_enabled,
                "exact own kit alone cannot fabricate empirical table or recorder support in either arm");}
    }
    std::printf("plan4-kernel-kit: %d checks passed\n",checks);
}
