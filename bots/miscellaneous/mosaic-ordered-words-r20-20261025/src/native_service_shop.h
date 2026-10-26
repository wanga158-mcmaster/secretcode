// Included inside Runtime. The caller gives pending execution priority and
// enters only on an actual cached home tile or while dead. Buying ends the
// caller's input; no old shop or unpriced post-purchase body read is used.
enum class NativeShopStatus {not_applicable,already_attempted,unfunded,blocked,unaffordable,invalid,prepared,applied,overrun};
struct NativeShopResult {
    NativeShopStatus status{NativeShopStatus::not_applicable};
    bool prepared{},attempted{},applied{};std::uint32_t item{},count{};
    bool owned()const{return prepared||attempted;}
};
struct NativeShopMetadata {
    std::uint32_t cost{},combine_count{},result{},passive{};float magnitude{};bool purchasable{};
};
struct NativeShopInventory {std::uint32_t jungle_shards{},jungle_emblems{},power_shards{};};
struct NativeShopState {
    std::array<NativeShopMetadata,3> metadata{};unsigned cursor{};int last_attempt_round{-1};
};
struct NativeShopLogic {
    template<class Budget> struct Reservation {
        Budget& budget;std::uint64_t outer;
        ~Reservation(){budget.reserve(outer);}
    };
    template<class Host,class Budget,class Import> static bool query(Host& host,Budget& budget,Import import) {
        if(!budget.before_query()){
            host.denied(NativeFactsLogic::hard_poison(budget));return false;
        }
        const int code=import();const bool paid=budget.after_query();
        if(!paid)host.denied(true);host.check(code);return paid;
    }
    template<class Host,class Budget> static NativeShopResult step(Host& host,Budget& budget,NativeShopState& state,
                                                                   const native_service::SelfFact& self_fact) {
        NativeShopResult out;
        if(!self_fact.actual(host.epoch()))return out;
        if(state.last_attempt_round==self_fact.epoch.round){out.status=NativeShopStatus::already_attempted;return out;}
        if(state.cursor>3){out.status=NativeShopStatus::invalid;return out;}
        const auto owner=self_fact.id;const int round=self_fact.epoch.round;
        const bool need_gold=!self_fact.gold_actual;std::uint32_t gold=self_fact.gold;
        // Source-exact upper count: three immutable item_info imports cold;
        // every transaction reads can_shop + inventory + gold unless fresh
        // same-attempt gold was supplied. Exactly one buy_item may follow.
        unsigned left=(3-state.cursor)+2+unsigned(need_gold);
        const auto outer=budget.held_tail;
        Reservation<Budget> restore{budget,outer};
        auto tail=[&]{return Budget::add(outer,Budget::add(Budget::mul(left,budget.query_bound),budget.command_bound));};
        if(!budget.admit(tail())){out.status=NativeShopStatus::unfunded;host.denied(NativeFactsLogic::hard_poison(budget));return out;}
        auto next=[&](auto import){
            --left;budget.reserve(tail());return query(host,budget,import);
        };
        std::int32_t permission{};
        if(!next([&]{return host.permission(owner,permission);})){out.status=NativeShopStatus::unfunded;return out;}
        if(!permission){out.status=NativeShopStatus::blocked;return out;}
        while(state.cursor<3){
            NativeShopMetadata metadata{};const auto index=state.cursor;
            if(!next([&]{return host.metadata(index,metadata);})){out.status=out.prepared?NativeShopStatus::prepared:NativeShopStatus::unfunded;return out;}
            state.metadata[index]=metadata;++state.cursor;out.prepared=true;
        }
        NativeShopInventory inventory{};
        if(!next([&]{return host.inventory(owner,inventory);})){out.status=out.prepared?NativeShopStatus::prepared:NativeShopStatus::unfunded;return out;}
        if(need_gold&&!next([&]{return host.gold(owner,gold);})){out.status=out.prepared?NativeShopStatus::prepared:NativeShopStatus::unfunded;return out;}
        host.investment(inventory,state.metadata[2]);
        const bool jungle=inventory.jungle_emblems!=0;const auto index=jungle?1u:0u;
        const auto& metadata=state.metadata[index];const auto shards=jungle?inventory.power_shards:inventory.jungle_shards;
        if(!metadata.purchasable||!metadata.cost||!metadata.combine_count||shards>=metadata.combine_count||
           (!jungle&&metadata.result!=host.item(2))){out.status=NativeShopStatus::invalid;return out;}
        out.item=host.item(index);
        out.count=std::min(gold/metadata.cost,metadata.combine_count-shards);
        if(!out.count){out.status=NativeShopStatus::unaffordable;return out;}
        budget.reserve(outer);
        if(!budget.before_command()){
            out.status=NativeShopStatus::unfunded;host.denied(NativeFactsLogic::hard_poison(budget));return out;
        }
        state.last_attempt_round=round;out.attempted=true;
        const int code=host.buy(out.item,out.count);host.invalidate();out.applied=code==0;
        const bool paid=budget.after_command();if(!paid)host.denied(true);host.check(code);
        out.status=paid?NativeShopStatus::applied:NativeShopStatus::overrun;return out;
    }
};
#ifndef MOSAIC_NATIVE_SERVICE_SHOP_PURE_ONLY
static NativeShopState& native_shop_state(){static constinit NativeShopState value{};return value;}
struct NativeShopHost {
    Runtime& runtime;
    native_service::Epoch epoch()const{return runtime.native_getstate().epoch;}
    std::uint32_t item(unsigned index)const {
        if(index==0)return std::uint32_t(item_type::jungle_shard_of_strength);
        if(index==1)return std::uint32_t(item_type::power_shard_of_strength);
        return std::uint32_t(item_type::jungle_emblem_of_strength);
    }
    int permission(std::uint32_t owner,std::int32_t& out){return detail::sdk_champion_can_shop(owner,0,&out);}
    int metadata(unsigned index,NativeShopMetadata& out){
        detail::item_info raw{};const int code=detail::sdk_item_info(item(index),&raw);
        if(!code)out={raw.cost,raw.combine_count,raw.result,raw.passive,raw.passive_magnitude,raw.purchasable!=0};return code;
    }
    int inventory(std::uint32_t owner,NativeShopInventory& out){
        std::array<std::uint32_t,std::size_t(item_type::count)> raw{};
        const int code=detail::sdk_champion_items(owner,raw.data());
        if(!code)out={raw[item(0)],raw[item(2)],raw[item(1)]};return code;
    }
    int gold(std::uint32_t owner,std::uint32_t& out){return detail::sdk_champion_gold(owner,&out);}
    int buy(std::uint32_t item,std::uint32_t count){return detail::sdk_buy_item(item,count);}
    void investment(const NativeShopInventory& inventory,const NativeShopMetadata& emblem){
        runtime.camp_taken=inventory.jungle_emblems&&emblem.passive==std::uint32_t(item_passive::jungle)?
            std::clamp(1.0f-emblem.magnitude,0.0f,1.0f):1.0f;
    }
    void invalidate(){runtime.native_after_attempt(native_service::EffectScope::self_only);}
    void denied(bool hard){if(hard)runtime.native_getstate().reject();}
    static void check(int code){detail::check(code);}
};
template<class Budget> NativeShopResult native_shop_step(Budget& budget,const native_service::SelfFact& self_fact){
    NativeShopHost host{*this};return NativeShopLogic::step(host,budget,native_shop_state(),self_fact);
}
#endif
