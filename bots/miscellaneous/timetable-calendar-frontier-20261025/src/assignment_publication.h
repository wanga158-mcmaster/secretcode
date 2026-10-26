#pragma once
// Mechanical pure extraction of the existing publication checks. These are
// predicates, never collection, Life observation, lookup, mutation or SDK facts.
// Callbacks MUST be constructed without observations. Their bodies execute only
// at the original short-circuit positions. Caller retains existing stage order,
// validated inventory bounds, matching Snapshot shapes and nonnull preconditions.
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace tt::assignment_publication {

template<class SelfInputsLike> std::uint64_t semantic_hash(const SelfInputsLike& self) {
    std::uint64_t value=1469598103934665603ull;
    const auto add=[&](std::uint64_t v){value^=v;value*=1099511628211ull;};
    const auto number=[&](float f){add(std::bit_cast<std::uint32_t>(f));};
    const auto& s=self.snapshot;
    add(s.actor);add(static_cast<std::uint32_t>(s.body.pos.x));add(static_cast<std::uint32_t>(s.body.pos.y));
    number(s.body.hp);number(s.body.max_hp);number(s.raw_strength);number(s.raw_focus);
    number(s.missing_health_bonus);number(s.incoming_scale);add(s.gold);add(s.carried_gems);
    add(static_cast<unsigned>(s.body.attack_range));add(s.body.ranged);add(s.body.splash);
    for(std::size_t i=0;i<self.inventory_size;++i)add(self.inventory[i]);
    for(std::size_t i=0;i<s.spells.size();++i) {
        const auto& spell=s.spells[i];add(static_cast<unsigned>(spell.kind));number(spell.damage);
        add(static_cast<unsigned>(spell.range));add(static_cast<unsigned>(spell.radius));
        add(static_cast<unsigned>(spell.duration));add(spell.exclusive);add(s.period[i]);
    }
    return value?value:1;
}

template<class BeforeSnapshot,class FreshSnapshot>
bool cooldowns_match(const BeforeSnapshot& before,const FreshSnapshot& actual) {
    for(std::size_t i=0;i<before.cooldown.size();++i) {
        const auto ready=static_cast<std::uint64_t>(before.epoch)+before.cooldown[i];
        if(actual.cooldown[i]) {
            if(static_cast<std::uint64_t>(actual.epoch)+actual.cooldown[i]!=ready)return false;
        } else if(actual.epoch<ready)return false;
    }
    return true;
}

template<class Key> std::uint64_t geometry_revision(const Key& key,std::uint64_t graph_revision) {
    auto value=key.map;
    for(const auto part:{key.layout,key.catalogue,key.roots,key.graph_policy,key.potential,graph_revision,
        static_cast<std::uint64_t>(key.require_quiet_bank),static_cast<std::uint64_t>(key.require_quiet_home)}) {
        value^=part;value*=1099511628211ull;
    }
    return value?value:1;
}

template<class Data,class LazyCashMatches> bool prepared(const Data& data,LazyCashMatches cash_matches) {
    return data.anchor&&data.actual&&data.companion&&data.model&&data.model->valuation_ready()&&
        data.cash_routes&&cash_matches()&&data.cash_routes==data.cash_cache.result();
}

template<class Data,class Owner,class LazySeedHash>
bool frozen_body(const Data& data,const Owner& owner,std::uint64_t expected,LazySeedHash seed_hash) {
    return expected&&owner.body==expected&&data.actual->snapshot.body_revision==expected&&seed_hash()==expected;
}
template<class Data,class Owner> bool frozen_body(const Data& data,const Owner& owner,std::uint64_t expected) {
    return frozen_body(data,owner,expected,[&]{return semantic_hash(*data.actual);});
}

template<class Data> bool cash_complete(const Data& data) {
    // Preserve both getter accesses from the old expression. prepared is the
    // earlier model-nonnull guard; do not add a new eligibility condition here.
    return data.model->cash_plan()&&data.model->cash_plan()->first_cash_complete;
}

template<class Data,class Owner,class Actual,class LazyEpochFresh,class LazyFieldMatches,
         class LazyGeometryRevision,class LazyLifeMatches>
bool fresh_matches(const Data& data,const Owner& /*owner*/,const Actual& actual,std::uint64_t expected,bool actual_ready,
                   LazyEpochFresh epoch_fresh,LazyFieldMatches field_matches,
                   LazyGeometryRevision current_geometry_revision,LazyLifeMatches life_matches) {
    // field_matches() contains the caller's owner.field/current-field equality.
    // Geometry/life callback construction must not dereference or observe them.
    return actual_ready&&epoch_fresh()&&semantic_hash(actual)==expected&&
        cooldowns_match(data.actual->snapshot,actual.snapshot)&&field_matches()&&data.geometry&&
        data.geometry->key()==data.companion->support&&
        current_geometry_revision()==data.companion->key.geometry_revision&&life_matches();
}

template<class Model> bool first_cash_fits(const Model& model,std::uint32_t start,std::uint32_t horizon) {
    const auto* cash=model.cash_plan();const auto& original=model.input();
    const auto first_end=original.job.repeat?cash->redeploy_complete:cash->heal_complete;
    return !(start>horizon||first_end<original.start||first_end-original.start>horizon-start);
}

template<class OptionalChoice,class LazyRefuted> bool choice_usable(const OptionalChoice& choice,LazyRefuted refuted) {
    return choice&&choice->banked_gems_by_H&&!refuted();
}

} // namespace tt::assignment_publication
