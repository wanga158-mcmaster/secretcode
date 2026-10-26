#pragma once
#include "runtime.h"
#include "../../common/telemetry.h"
#include "cambc.h"

namespace plan4::runtime {
// Only immutable public definitions and this actor's own inventory/body/cash
// are queried. No teammate equipment, hidden camp state or replay data enters.
class CapitalLive : public Capital {
    capital::Definitions definitions_{};
    capital::Parity parity_{};
    capital::PurchaseReceipt pending_{};
    CombatKit kit_{};
    int champion_=-1,round_=-1;
    unsigned floor_=60000,pending_item_=0,pending_count_=0;
    bool definitions_ready_=false,pending_valid_=false;
    static std::array<cambc::item_type,3> shards(){return{cambc::item_type::monster_shard_of_strength,cambc::item_type::jungle_shard_of_health,cambc::item_type::power_shard_of_strength};}
    static std::array<cambc::item_type,3> emblems(){return{cambc::item_type::monster_emblem_of_strength,cambc::item_type::jungle_emblem_of_health,cambc::item_type::power_emblem_of_strength};}
    bool budget()const{return int(cambc::round_number())==round_&&cambc::fuel_remaining()>=floor_+500;}
    void start_budget(){round_=int(cambc::round_number());const unsigned before=cambc::fuel_remaining();floor_=std::max(60000u,before>12000?before-12000:60000u);}
    static void mix(std::uint64_t&hash,unsigned value){hash=(hash^value)*1099511628211ull;}
    static void floating(std::uint64_t&hash,float value){unsigned bits;__builtin_memcpy(&bits,&value,sizeof(bits));mix(hash,bits);}
    static std::int64_t milli(float value){if(value>=9e15f)return 9000000000000000000LL;
        if(value<=-9e15f)return-9000000000000000000LL;return std::int64_t(double(value)*1000);}
    bool definitions(const cambc::champion&me){
        if(!budget())return false;const int champion=int(me.champion_name());
        if(definitions_ready_&&champion==champion_)return true;
        definitions_ready_=false;definitions_={};parity_={};champion_=champion;
        if(!budget())return false;const auto growth=me.base_stats();
        definitions_.health_growth=growth.health_growth;definitions_.strength_growth=growth.strength_growth;definitions_.focus_growth=growth.focus_growth;
        std::uint64_t hash=1469598103934665603ull;mix(hash,unsigned(champion));floating(hash,growth.health_growth);floating(hash,growth.strength_growth);floating(hash,growth.focus_growth);
        for(unsigned f=0;f<3;++f){
            if(!budget())return false;const auto shard=shards()[f].info();
            if(!budget())return false;const auto emblem=emblems()[f].info();
            if(!shard.result||*shard.result!=emblems()[f]||!shard.combine_count||!shard.purchasable||emblem.purchasable||
                (f==0&&emblem.passive!=cambc::item_passive::monster)||(f==1&&emblem.passive!=cambc::item_passive::jungle)||
                (f==2&&emblem.passive))return false;
            definitions_.item[f]={shard.cost,shard.combine_count,shard.health_points,shard.strength_points,shard.focus_points,
                emblem.health_points,emblem.strength_points,emblem.focus_points,emblem.passive?emblem.passive_magnitude:0,shard.purchasable};
            mix(hash,shard.cost);mix(hash,shard.combine_count);mix(hash,unsigned(shard.health_points));mix(hash,unsigned(shard.strength_points));mix(hash,unsigned(shard.focus_points));
            mix(hash,unsigned(emblem.health_points));mix(hash,unsigned(emblem.strength_points));mix(hash,unsigned(emblem.focus_points));floating(hash,definitions_.item[f].passive_magnitude);
        }
        definitions_.public_source=hash;definitions_.quote_law=hash^0x4361706974616c32ull;definitions_.queried_public=true;
        definitions_ready_=capital::valid_definitions(definitions_);return definitions_ready_;
    }
    bool collect(capital::InvestmentFrame&out){
        out.exact=out.inventory_complete=false;
        if(!budget())return false;const auto&me=cambc::self();
        if(!definitions(me)||!budget())return false;
        const auto passive=me.passive();
        const bool supported_passive=passive==cambc::passive_type::none||passive==cambc::passive_type::remote_shop||
            passive==cambc::passive_type::splash_attack||passive==cambc::passive_type::reset_cooldowns_on_kill||
            passive==cambc::passive_type::retaliation||passive==cambc::passive_type::stun_immunity;
        if(!supported_passive)return false;
        if(!budget())return false;const auto items=me.item_count();
        capital::Equipment equipment;std::uint64_t inventory=1469598103934665603ull;
        for(unsigned item=0;item<unsigned(cambc::item_type::count);++item){const auto count=items[cambc::item_type(item)];mix(inventory,item);mix(inventory,count);
            bool represented=false;for(unsigned f=0;f<3;++f)represented|=cambc::item_type(item)==shards()[f]||cambc::item_type(item)==emblems()[f];
            if(count&&!represented)return false;}
        for(unsigned f=0;f<3;++f){const auto total=std::uint64_t(items[shards()[f]])+std::uint64_t(items[emblems()[f]])*definitions_.item[f].combine_count;
            if(total>std::numeric_limits<unsigned>::max())return false;equipment.total_shards[f]=unsigned(total);}
        if(!budget())return false;out.alive=me.is_alive();
        if(!budget())return false;out.health=me.health();
        if(!budget())return false;out.stats.maximum_health=me.max_health();
        if(!budget())return false;out.stats.strength=me.strength();
        if(!budget())return false;out.stats.focus=me.focus();
        if(!budget())return false;out.cash=me.gold_count();
        if(!budget())return false;out.bag=me.gem_count();
        out.position={};out.on_spawn=false;
        if(out.alive){if(!budget())return false;const auto p=me.position();out.position={p.x,p.y};
            if(!budget())return false;out.on_spawn=cambc::map::spawn_region(cambc::team::ally).is_set(p);}
        if(!budget())return false;
        out.clock={round_,{int(static_cast<cambc::handle_champion_t>(me))}};out.champion=champion_;out.queried_passive=int(passive);
        out.remote_shop=passive==cambc::passive_type::remote_shop;out.passive_supported=true;out.equipment=equipment;
        out.stats.camp_outgoing=equipment.total_shards[0]>=5?1+definitions_.item[0].passive_magnitude:1;
        out.stats.camp_incoming=equipment.total_shards[1]>=5?1-definitions_.item[1].passive_magnitude:1;
        out.definitions=definitions_;out.definitions.actual_purchase_receipt_parity=parity_.supported(definitions_);
        out.parity_receipts=parity_.receipts;out.inventory_fingerprint=inventory;
        out.kit_version=kit_.version;out.stat_epoch=kit_.stat_epoch;
        out.provenance={round_,Source::own_state,Status::exact_now};out.exact=out.inventory_complete=true;
        return capital::exact_investment(out);
    }
public:
    explicit CapitalLive(bool enabled=true):Capital(enabled){}
    void observe_combat(const CombatKit&kit){Capital::observe_combat(kit);kit_=kit;start_budget();capital::InvestmentFrame frame;collect(frame);observe_investment(frame);}
    void before_purchase(unsigned item,unsigned count){
        observe_investment(capital::InvestmentFrame{});pending_valid_=false;start_budget();int family=-1;
        for(unsigned f=0;f<3;++f)if(item==unsigned(shards()[f]))family=int(f);
        if(family<0||!count)return;
        pending_item_=item;pending_count_=count;pending_.family=capital::Family(family);pending_.count=count;
        pending_valid_=collect(pending_.before);
    }
    void after_purchase(unsigned item,unsigned count){
        if(!pending_valid_||item!=pending_item_||count!=pending_count_){pending_valid_=false;return;}
        start_budget();pending_valid_=false;if(!collect(pending_.after))return;
        pending_.isolated_successful_legal_purchase=true;const bool matches=parity_.observe(pending_);
        if(budget())LogRow("capital-purchase").field("actor",pending_.before.clock.own.id).field("round",round_)
            .field("item",item).field("count",count).field("beforeCash",pending_.before.cash).field("afterCash",pending_.after.cash)
            .field("beforeHPmilli",milli(pending_.before.health)).field("afterHPmilli",milli(pending_.after.health))
            .field("beforeMaxHPmilli",milli(pending_.before.stats.maximum_health)).field("beforeStrengthMilli",milli(pending_.before.stats.strength))
            .field("beforeFocusMilli",milli(pending_.before.stats.focus))
            .field("maxHPmilli",milli(pending_.after.stats.maximum_health)).field("strengthMilli",milli(pending_.after.stats.strength))
            .field("focusMilli",milli(pending_.after.stats.focus)).field("parity",matches).field("publicSource",definitions_.public_source)
            .field("beforeMonster",pending_.before.equipment.total_shards[0]).field("afterMonster",pending_.after.equipment.total_shards[0])
            .field("beforeJungle",pending_.before.equipment.total_shards[1]).field("afterJungle",pending_.after.equipment.total_shards[1])
            .field("beforePower",pending_.before.equipment.total_shards[2]).field("afterPower",pending_.after.equipment.total_shards[2])
            .field("quoteLaw",definitions_.quote_law).field("receipts",parity_.receipts).field("families",parity_.families)
            .field("combinations",parity_.combinations).field("supported",parity_.supported(definitions_)).emit();
    }
};
} // namespace plan4::runtime
