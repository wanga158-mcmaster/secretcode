#pragma once
#include "../../distributed-common/src/observe.h"
#include <span>

// Pure conservative bank occupancy bounds. No SDK, actions, ally intent or gold.
namespace p8::native::economy {
struct TributeTiming {
    p6::Fact<double> multiplier{};
    p6::Fact<int> base{},minimum{};
};
struct BankVisit {
    int actor=-1;
    p6::Point approach{};
    // The solo template may charge a full camp even when J receives a share.
    // Keep that longer channel; never shorten an already conservative event.
    p6::Gems template_bag=0;
    double ambient=0;
    p6::Point exit{-1,-1};
    std::span<const p6::Point> bank_path{};
};
struct QueueResult {
    bool valid=false;
    std::array<int,p6::max_actors> waiting{},bank_delay{},renewal_delay{};
};
inline bool queue_exits_clear(std::span<const BankVisit> visits) {
    for (std::size_t i=0;i<visits.size();++i)for (std::size_t j=0;j<visits.size();++j)
        if (i!=j&&visits[i].approach==visits[j].approach) {
            if (visits[i].exit.x<0||visits[i].exit.y<0||visits[i].exit==visits[i].approach||
                visits[j].bank_path.empty())return false;
            for (const auto tile:visits[j].bank_path)
                if (tile!=visits[j].approach&&tile==visits[i].exit)return false;
        }
    return true;
}
inline int tribute_ticks(p6::Gems bag,const TributeTiming& timing) {
    if (bag<=0 || bag>9007199254740991LL ||
        timing.multiplier.source!=p6::Source::queried_setting ||
        timing.base.source!=p6::Source::queried_setting ||
        timing.minimum.source!=p6::Source::queried_setting ||
        !std::isfinite(timing.multiplier.value)||timing.multiplier.value<0||timing.minimum.value<1) return -1;
    const double scaled=std::ceil(double(bag)*timing.multiplier.value);
    if (!std::isfinite(scaled)||scaled<0||scaled>1000000)return -1;
    const auto ticks=std::max<std::int64_t>(timing.minimum.value,std::int64_t(timing.base.value)+std::int64_t(scaled));
    return ticks>0&&ticks<=1000000?int(ticks):-1;
}
inline QueueResult queue_schedule(p6::Schedule& schedule,std::span<const BankVisit> visits,
                                 std::span<p6::CycleCertificate> safety,const TributeTiming& timing,
                                 int bound=120
#if P8_ORDINARY_COMPLETE_CYCLE
                                 ,bool ordinary_complete=false
#endif
                                 ) {
    QueueResult out;
    if (schedule.actor_count<1||schedule.actor_count>p6::max_actors||schedule.production_count<0||
        schedule.production_count>p6::max_productions||
        visits.size()!=std::size_t(schedule.actor_count)||safety.size()!=visits.size()||bound<1||
#if P8_ORDINARY_COMPLETE_CYCLE
        (bound>120&&(!ordinary_complete||schedule.actor_count!=1||visits.size()!=1||!safety[0].solo)))return out;
#else
        bound>120)return out;
#endif
    std::array<int,p6::max_actors> actors{},tributes{},services{},own_extra{};
    for (std::size_t i=0;i<visits.size();++i) {
        const auto& visit=visits[i];const int a=p6::actor_index(schedule,visit.actor);
        if (a<0||safety[i].actor!=visit.actor||visit.approach.x<0||visit.approach.y<0||
            visit.template_bag<0||!std::isfinite(visit.ambient)||visit.ambient<0)return out;
        for (std::size_t j=0;j<i;++j)if (visits[j].actor==visit.actor)return out;
        actors[i]=a;const auto& actor=schedule.actors[a];
        if (!actor.initial_bag.known()||actor.initial_bag.value<0||!actor.occupied_turns.known()||
            actor.event_count<1||actor.event_count>p6::max_actor_events)return out;
        int tribute=-1;
        for (int e=0;e<actor.event_count;++e)if (actor.events[e].kind==p6::EventKind::tribute) {
            if (tribute>=0)return out;
            tribute=e;
        }
        if (tribute<0)return out;
        tributes[i]=tribute;
        p6::Gems bag=actor.initial_bag.value;
        for (int p=0;p<schedule.production_count;++p) {
            const auto& production=schedule.productions[p];
            if (!production.gems.known()||production.gems.value<0||production.holder_count<1||
                production.holder_count>schedule.actor_count)return out;
            int lowest=production.holders[0],references=0;
            bool holder=false;
            for (int h=0;h<production.holder_count;++h) {
                holder|=production.holders[h]==visit.actor;lowest=std::min(lowest,production.holders[h]);
                for (int k=0;k<h;++k)if (production.holders[k]==production.holders[h])return out;
            }
            if (!holder)continue;
            if (production.tick>actor.events[tribute].tick)return out;
            for (int e=0;e<actor.event_count;++e)if (actor.events[e].kind==p6::EventKind::production&&
                actor.events[e].production_index==p&&actor.events[e].event_id==production.event_id)++references;
            if (references!=1)return out;
            const auto share=production.gems.value/production.holder_count+
                (lowest==visit.actor?production.gems.value%production.holder_count:0);
            if (!p6::add(bag,share,bag))return out;
        }
        const int actual=tribute_ticks(bag,timing),original=tribute_ticks(visit.template_bag,timing);
        if (actual<0||original<0)return out;
        const int channel=std::max(actual,original);services[i]=channel+2;own_extra[i]=channel-original;
    }
    for (std::size_t i=0;i<visits.size();++i) {
        int others=0;
        for (std::size_t j=0;j<visits.size();++j)if (i!=j&&visits[i].approach==visits[j].approach) {
            ++others;out.waiting[i]+=services[j];
        }
        // Each actor conservatively waits for EVERY other channel, independent
        // of arrival/team order. Entry and physical vacating cost one turn each.
        out.bank_delay[i]=own_extra[i]+out.waiting[i]+(others?1:0);
        out.renewal_delay[i]=own_extra[i]+out.waiting[i]+(others?2:0);
        const auto& actor=schedule.actors[actors[i]];const int bank=actor.events[tributes[i]].tick;
        if (bank<services[i]-2-own_extra[i]||bank+std::int64_t(out.bank_delay[i])>bound||
            actor.occupied_turns.value<0||actor.occupied_turns.value+out.renewal_delay[i]>bound)return out;
        for (int e=0;e<actor.event_count;++e) {
            const auto& event=actor.events[e];
            const int delay=e==tributes[i]?out.bank_delay[i]:event.tick>=bank?out.renewal_delay[i]:0;
            if (event.tick<0||event.tick+std::int64_t(delay)>bound)return out;
        }
        auto bank_risk=safety[i].bank,recovery=safety[i].recovery;
        bank_risk.incoming_total+=visits[i].ambient*out.bank_delay[i];
        recovery.incoming_total+=visits[i].ambient*out.renewal_delay[i];
        if (!p6::survives(bank_risk)||!p6::survives(recovery))return out;
    }
    // Commit only after all actors and cumulative exposure bounds pass.
    for (std::size_t i=0;i<visits.size();++i) {
        auto& actor=schedule.actors[actors[i]];const int bank=actor.events[tributes[i]].tick;
        for (int e=0;e<actor.event_count;++e) {
            auto& event=actor.events[e];event.tick+=e==tributes[i]?out.bank_delay[i]:
                event.tick>=bank?out.renewal_delay[i]:0;
        }
        actor.occupied_turns.value+=out.renewal_delay[i];
        safety[i].bank.incoming_total+=visits[i].ambient*out.bank_delay[i];
        safety[i].recovery.incoming_total+=visits[i].ambient*out.renewal_delay[i];
    }
    out.valid=true;return out;
}
} // namespace p8::native::economy
