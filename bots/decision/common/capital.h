#pragma once
#include "forecasts.h"
#include "model-capital.h"
#include "capital-trace.h"
#include "capital-meter.h"

namespace plan3_runtime {
struct EquipmentStats {
    double health=0,strength=0,focus=0,campDamage=1,campMitigation=1;
    double captureGoldMultiplier=1;
    int recall=0;
    plan3::EquipmentRecord equipment;
};
struct CapitalProposal {
    plan3::JobForecast investmentJob;
    int fundingGroup=-1,secondFundingGroup=-1;
    cambc::item_type targetShard=cambc::item_type::monster_shard_of_strength;
    unsigned shardsRemaining=0;
    // Absolute count of this shard plus its own combined result ingredients.
    // Unrelated items never advance a committed target.
    unsigned targetProgressGoal=0;
    int cashoutAltar=-1,readyAt=0,purchaseRecoveryAt=0,productiveTile=-1;
    bool requiresSpawnRecovery=false;
    plan3::CapitalOutput accounting;
    plan3::InvestmentBlock block=plan3::InvestmentBlock::defer;
};
struct CapitalWork {
    unsigned guards=0,targets=0,fundingPasses=0,fundingRows=0,forks=0,loopRequests=0,terminalRequests=0;
    bool cutoff=false;
    CapitalTrace unknown{};
    CapitalDeferMeter deferMeter{};
    unsigned schedulesAppended=0;
};
struct CapitalSchedules {
    bool complete=false;
    std::vector<plan3::InvestmentSchedule> schedules;
    std::vector<CapitalProposal> proposals;
    CapitalWork work{};
};
struct CapitalPhase {
    bool prefixPending=false, fundingCycleStarted=false;
    int stage=0;
    bool recovering=false;
};
enum class CapitalReviewStatus { unknown, keep, defer, target_complete };
struct CapitalReview {
    CapitalReviewStatus status=CapitalReviewStatus::unknown;
    unsigned shardsRemaining=0;
    plan3::InvestmentForecast forecast;
    plan3::CapitalOutput accounting;
    std::optional<CapitalProposal> proposal;
};
class TreasuryPlanner {
    static constexpr unsigned itemCount=static_cast<unsigned>(cambc::item_type::count);
    std::array<cambc::item_info,itemCount> catalogue_{};
    unsigned catalogueCursor_=0;
    std::vector<plan3::InvestmentSchedule> schedules_;
    // Only full-health, public renewal preparation persists. Each fixed
    // hypothetical gear target has its own signature-checked bounded cache;
    // current funding, cash and proposal accounting are rebuilt below.
    std::array<ForecastLoopCache,4> post_purchase_loops_;
    std::array<std::optional<EquipmentStats>,4> post_purchase_stats_;
    std::vector<std::pair<int,unsigned>> post_purchase_inventory_;
    struct ChampionDesignMetadata {
        cambc::champion_stats base;
        bool vesper=false;
    };
    // One planner belongs to the module's immutable authenticated self.
    // Dynamic inventory/equipment results are always reconstructed below.
    bool design_reuse_=false;
    mutable std::optional<ChampionDesignMetadata> champion_design_;
    static cambc::item_type type(unsigned i) {
        return cambc::item_type{static_cast<cambc::item_type::enum_type>(i)};
    }
    int group_index(const ForecastContext& c,int key) const {
        for (std::size_t i=0;i<c.personal.groups.size();++i)
            if (c.personal.groups[i].key==key) return static_cast<int>(i);
        return -1;
    }
    int altar_index(const ForecastContext& c,int tile) const {
        for (std::size_t i=0;i<c.personal.altars.size();++i)
            if (c.personal.altars[i].y*cambc::map::width()+c.personal.altars[i].x==tile) return static_cast<int>(i);
        return -1;
    }
    EquipmentStats stats(const cambc::champion& me,const cambc::enum_map<cambc::item_type>& counts) const {
        const bool warm=design_reuse_ && champion_design_.has_value();
        const auto base=warm ? champion_design_->base:me.base_stats();
        EquipmentStats s;
        s.health=base.base_health; s.strength=base.base_strength; s.focus=base.base_focus;
        s.campDamage=base.camp_damage_multiplier; s.recall=cambc::constants::recall_time;
        s.equipment.provenance=plan3::Provenance::mechanically_calculated;
        std::array<bool,6> passive{};
        const bool vesper=warm ? champion_design_->vesper:
            me.champion_name()==cambc::champion_type::vesper_vale;
        // Preserve cold identity/design/identity order and publish atomically.
        if(design_reuse_ && !warm)champion_design_=ChampionDesignMetadata{base,vesper};
        if (vesper)
            s.captureGoldMultiplier+=cambc::constants::passive_abilities::vesper_vale_bonus_gold_percent/100.0;
        for (unsigned i=0;i<itemCount;++i) {
            const unsigned count=counts[type(i)]; if (!count) continue;
            const auto& m=catalogue_[i];
            s.health+=count*m.health_points*base.health_growth;
            s.strength+=count*m.strength_points*base.strength_growth;
            s.focus+=count*m.focus_points*base.focus_growth;
            s.equipment.items.emplace_back(i,count);
            if (!m.passive) continue;
            const unsigned p=static_cast<unsigned>(*m.passive);
            if (p>=passive.size() || passive[p]) continue;
            passive[p]=true;
            switch (*m.passive) {
            case cambc::item_passive::monster: s.campDamage*=1+m.passive_magnitude; break;
            case cambc::item_passive::jungle:
            case cambc::item_passive::durability: s.campMitigation*=1-m.passive_magnitude; break;
            case cambc::item_passive::treasure: s.captureGoldMultiplier+=m.passive_magnitude; break;
            case cambc::item_passive::home:
                s.recall=static_cast<int>(std::ceil(cambc::constants::recall_time*(1-m.passive_magnitude))); break;
            }
        }
        // The Plan 3 frozen roster has no stat-changing champion passive.
        // Supporting a different draft requires a separately versioned model.
        return s;
    }
    static int healing_turns(double maximum,double health) {
        const double perTurn=cambc::constants::spawn_healing*maximum;
        if (health>=maximum) return 0;
        return perTurn>0 ? std::max(1,static_cast<int>(std::ceil((maximum-health)/perTurn))):
                          static_cast<int>(cambc::constants::max_rounds);
    }
    struct FixedFunding {
        bool known=true,valid=false;
        int turns=0,gems=0;
        double gold=0,health=0;
        cambc::vec2 end{};
    };
    // One declared remainder, using the committed altar rather than a newly
    // optimized route. Fresh repeats always clear full camps; current rewards
    // and timers apply only to the already-started remainder.
    FixedFunding fixed_funding(const ForecastContext& c,int firstKey,int secondKey,int altarTile,
                               cambc::vec2 start,double health,int bag,bool fresh) const {
        FixedFunding r; r.health=health;r.end=start;
        const int first=group_index(c,firstKey),second=group_index(c,secondKey);
        if(first<0 || (secondKey>=0 && second<0))return r;
        const auto& one=c.personal.groups[first];
        const auto& last=c.personal.groups[second>=0?second:first];
        const int travel=c.paths.get(one.field,start);
        if(travel==StaticPaths::unknown) {r.known=false;return r;}
        if(travel<0 || (!fresh && (c.current_reward_contested(first) ||
            (second>=0 && c.current_reward_contested(second)))))return r;
        const auto a=c.clear(first,health,fresh?0:-1);
        if(!a.admitted)return r;
        const auto wait=[&](int g) {
            if(fresh)return 0;
            const auto* o=c.personal.current(g);
            return o && o->lifecycle==Lifecycle::respawning ? std::max(0,o->respawn_remaining):
                c.personal.remembered_respawn_minimum(g);
        };
        const int hold=static_cast<int>(cambc::constants::capture_time)+1;
        int elapsed=std::max(travel,wait(first))+a.turns+hold;
        r.health-=a.healthLoss;
        int reward=one.gem_value,gold=one.gold_value;
        if(second>=0) {
            const int between=c.paths.minimum_between(last.field,one.field);
            if(between==StaticPaths::unknown) {r.known=false;return r;}
            if(between<0)return r;
            const auto b=c.clear(second,r.health,fresh?0:-1);
            if(!b.admitted)return r;
            elapsed=std::max(elapsed+between,wait(second))+b.turns+hold;
            r.health-=b.healthLoss;reward+=last.gem_value;gold+=last.gold_value;
        }
        r.gems=bag+reward;r.end=last.anchor;
        if(r.gems>0) {
            const int altar=altar_index(c,altarTile);
            if(altar<0)return r;
            const int back=c.paths.minimum_between(c.personal.altar_fields[altar],last.field);
            if(back==StaticPaths::unknown) {r.known=false;return r;}
            if(back<0)return r;
            elapsed+=back+c.tribute(r.gems)+c.lock_delay(altar);
            r.end=c.altar_endpoint(altar,last.field);
            if(c.cutoff) {r.known=false;return r;}
        }
        r.turns=elapsed;r.gold=std::floor(gold*c.capture_gold+1e-8);r.valid=true;return r;
    }
public:
    void enable_design_reuse() { design_reuse_=true; }
    // Public immutable metadata initialization is bounded and can span turns.
    bool advance_catalogue(unsigned work=4) {
        while (catalogueCursor_<itemCount && work-- && cambc::fuel_remaining()>=45000) {
            catalogue_[catalogueCursor_]=type(catalogueCursor_).info(); ++catalogueCursor_;
        }
        return catalogueCursor_==itemCount;
    }
    bool ready() const { return catalogueCursor_==itemCount; }
    std::optional<EquipmentStats> exact_stats() const {
        if (!ready()) return std::nullopt;
        const auto& me=cambc::self(); return stats(me,me.item_count());
    }
    const std::vector<plan3::InvestmentSchedule>& schedules() const { return schedules_; }
    CapitalSchedules build_schedules(const ForecastContext& c,
                                    const std::vector<plan3::JobForecast>& menu) {
        CapitalSchedules result;
        schedules_.clear();
        const auto guarded=[&](CapitalSite site,CapitalFuelReading* reading=nullptr) {
            ++result.work.guards;
            const bool contextCutoff=c.cutoff;
            const bool readyNow=!contextCutoff && [&] {
                const auto fuel=cambc::fuel_remaining();
                if(reading)*reading={true,fuel};
                return fuel>=45000;
            }();
            if(!readyNow)result.work.unknown.latch(capital_guard_unknown(site,contextCutoff));
            result.work.cutoff|=!readyNow;return readyNow;
        };
        if (!ready()) { result.work.unknown.latch(CapitalUnknown::catalogueMetadata);return result; }
        if (!guarded(CapitalSite::entry)) return result;
        const auto& me=cambc::self(); const auto counts=me.item_count();
        const auto owned=stats(me,counts);
        if(owned.equipment.items!=post_purchase_inventory_) {
            post_purchase_inventory_=owned.equipment.items;
            for(auto& cached:post_purchase_stats_)cached.reset();
        }
        const auto original=me.position();
        cambc::vec2 productivePosition=original;
        int prefix=0,cashoutAltar=-1;
        if (c.info.carriedGems>0) {
            const plan3::JobForecast* bank=nullptr;
            for (const auto& j:menu) if (j.id.kind==plan3::JobKind::bank &&
                plan3::admitted(c.info,j,true) && (!bank || j.nominal()->tau<bank->nominal()->tau ||
                (j.nominal()->tau==bank->nominal()->tau && j.id<bank->id))) bank=&j;
            if (!bank) { result.complete=!c.cutoff;if(!result.complete)result.work.unknown.latch(CapitalUnknown::bankPrefixCutoff);return result; }
            prefix=bank->nominal()->tau;
            const int tile=bank->nominal()->terminal.position.tile;
            productivePosition={tile%cambc::map::width(),tile/cambc::map::width()};
            cashoutAltar=bank->id.altarTile;
        }
        const int returnDistance=c.paths.get(c.personal.spawn_field,productivePosition);
        if (returnDistance<0) { result.complete=returnDistance!=StaticPaths::unknown;if(!result.complete)result.work.unknown.latch(CapitalUnknown::returnRouteUnknown);return result; }
        CapitalFuelReading deferBefore,deferAfter;
        if(!guarded(CapitalSite::deferBefore,&deferBefore))return result;
        result.work.deferMeter.begin(deferBefore.fuel);
        ++result.work.terminalRequests;
        const auto defer=c.terminal(original,c.info.health,c.info.carriedGems);
        const bool deferReady=guarded(CapitalSite::deferAfter,&deferAfter);
        result.work.deferMeter.finish(deferAfter);
        if (!deferReady) return result;
        std::array<double,3> deferOutputs{};
        const std::array<int,3> checkpoints{c.info.horizon()/4,c.info.horizon()/2,c.info.horizon()};
        const int capacity=std::max(0,c.info.winningGems-c.info.ownPermanentScore);
        for (unsigned k=0;k<3;++k) deferOutputs[k]=plan3::potential(defer,checkpoints[k],capacity);
        plan3::InvestmentForecast deferForecast;
        deferForecast.available=deferForecast.exactEquipmentReconstruction=true;
        deferForecast.output=deferForecast.deferOutput=deferOutputs;
        schedules_.push_back({plan3::InvestmentBlock::defer,deferForecast,false});++result.work.schedulesAppended;
        const std::array<cambc::item_type,4> targets{cambc::item_type::monster_shard_of_strength,
            cambc::item_type::jungle_shard_of_health,cambc::item_type::power_shard_of_strength,
            cambc::item_type::power_shard_of_focus};
        const auto progress=[&](cambc::item_type shard) {
            const auto& m=catalogue_[static_cast<unsigned>(shard)];
            return m.result && counts[*m.result] ? m.combine_count:counts[shard];
        };
        const auto inherited=progress(targets[0])<catalogue_[static_cast<unsigned>(targets[0])].combine_count ? targets[0]:
            progress(targets[1])<catalogue_[static_cast<unsigned>(targets[1])].combine_count ? targets[1]:targets[2];
        // All four targets share the same observed funding menu and owned gear.
        // Lazily scan it once; no target-specific hypothetical value is cached.
        const plan3::JobForecast* funding=nullptr;
        int fundingFirst=-1,fundingSecond=-1,firstDuration=0,repeatPeriod=0;
        double fundingGold=0,endingHealth=c.info.health,repeatedEndingHealth=c.info.health;
        bool fundingScanned=false;
        const auto findFunding=[&] {
            if(fundingScanned)return true;
            fundingScanned=true;++result.work.fundingPasses;
            double bestRate=-1;
            for (const auto& j:menu) if (plan3::productive(j.id.kind) && plan3::admitted(c.info,j,true)) {
                if(!guarded(CapitalSite::funding))return false;
                ++result.work.fundingRows;
                const int first=group_index(c,j.id.firstGroup),second=group_index(c,j.id.secondGroup);
                const int altar=altar_index(c,j.id.altarTile);
                if (first<0 || altar<0) continue;
                const auto& g=c.personal.groups[first];
                const int oldTravel=c.paths.get(g.field,original),newTravel=c.paths.get(g.field,productivePosition);
                if (oldTravel<0 || newTravel<0) continue;
                const auto one=c.clear(first,c.info.maximumHealth,0);
                if(!guarded(CapitalSite::funding))return false;
                if (!one.admitted) continue;
                const auto two=second>=0 ? c.clear(second,c.info.maximumHealth-one.healthLoss,0):plan3::ClearEstimate{};
                if(!guarded(CapitalSite::funding))return false;
                if (second>=0 && !two.admitted) continue;
                int freshClear=one.turns+(second>=0 ? two.turns:0);
                int originalClear=c.clear(first,c.info.health).turns;
                if(!guarded(CapitalSite::funding))return false;
                if (second>=0) originalClear+=c.clear(second,c.info.health-j.nominal()->healthLoss).turns;
                if(!guarded(CapitalSite::funding))return false;
                const int adjusted=j.nominal()->tau-oldTravel+newTravel-
                    c.tribute(c.info.carriedGems+j.campGems)+c.tribute(j.campGems);
                const int repeated=adjusted-originalClear+freshClear;
                const int recovery=owned.recall+healing_turns(owned.health,owned.health-one.healthLoss-(second>=0?two.healthLoss:0))+returnDistance;
                int regen=g.max_respawn_timer;
                if (second>=0) regen=std::max(regen,c.personal.groups[second].max_respawn_timer);
                const int period=std::max(repeated+recovery,repeated+regen);
                const double gold=std::floor(j.campGold*owned.captureGoldMultiplier+1e-8);
                const double rate=(gold+double(cambc::constants::passive_gold)*period)/std::max(1,period);
                if (adjusted<=0 || period<=0) continue;
                if (!funding || plan3::strictly_better(rate,bestRate) ||
                    (plan3::finite_equal(rate,bestRate) && j.id<funding->id)) {
                    funding=&j; bestRate=rate; fundingFirst=first; fundingSecond=second;
                    firstDuration=adjusted; repeatPeriod=period; fundingGold=gold;
                    endingHealth=std::max(0.0,c.info.health-j.nominal()->healthLoss);
                    repeatedEndingHealth=c.info.maximumHealth-one.healthLoss-(second>=0?two.healthLoss:0);
                }
            }
            return true;
        };
        // Query this immutable public region once, rather than once per gear.
        const auto& spawn=cambc::map::spawn_region(cambc::team::ally);
        const auto spawnPosition=c.personal.spawn_positions.front();
        const bool fieldShop=me.champion_name()==cambc::champion_type::tavi;
        std::vector<CapitalProposal> proposals;
        for (unsigned target=0;target<targets.size();++target) {
            if (!guarded(CapitalSite::target)) return result;
            ++result.work.targets;
            const auto shard=targets[target]; const auto& meta=catalogue_[static_cast<unsigned>(shard)];
            const auto block=static_cast<plan3::InvestmentBlock>(target+1);
            const bool completed=target<2 && meta.result && counts[*meta.result]>0;
            if (completed) { schedules_.push_back({block,{},true});++result.work.schedulesAppended; continue; }
            const unsigned needed=target<2 ? meta.combine_count-counts[shard]:1;
            if (!needed || !meta.purchasable || !meta.cost || !meta.combine_count || !meta.result) continue;
            auto hypothetical=counts;
            hypothetical[shard]+=needed;
            while (hypothetical[shard]>=meta.combine_count) {
                hypothetical[shard]-=meta.combine_count; ++hypothetical[*meta.result];
            }
            // This fork depends only on immutable design/catalogue metadata
            // and the exact owned inventory, never cash, health or position.
            // Publish it before the fuel fence so later callbacks do not
            // repeatedly spend the same budget rebuilding target zero.
            if(!post_purchase_stats_[target])post_purchase_stats_[target]=stats(me,hypothetical);
            const auto& post=*post_purchase_stats_[target];
            if(!guarded(CapitalSite::gear))return result;
            auto future=c.fork_without_loop_cache();++result.work.forks;
            future.shared_loop_cache=&post_purchase_loops_[target];
            future.info.maximumHealth=post.health; future.info.health=post.health; future.info.carriedGems=0;
            future.info.equipment=post.equipment;
            future.strength=post.strength; future.focus=post.focus;
            future.camp_damage=post.campDamage; future.camp_mitigation=post.campMitigation;
            future.recall=post.recall; future.heal=healing_turns(post.health,post.health*.25);
            plan3::CapitalTiming timing;
            timing.horizon=c.info.horizon(); timing.scoreCapacity=capacity;
            timing.oldBag=c.info.carriedGems; timing.cashoutAt=prefix;
            timing.initialGold=c.info.gold; timing.blockCost=double(needed)*meta.cost;
            timing.passiveGoldPerTurn=cambc::constants::passive_gold;
            const plan3::JobForecast* targetFunding=nullptr;
            if (timing.initialGold+timing.passiveGoldPerTurn*prefix<timing.blockCost) {
                if(!findFunding())return result;
                if (!funding) { schedules_.push_back({block,{},false});++result.work.schedulesAppended; continue; }
                targetFunding=funding;
            }
            timing.firstFundingCompletion=targetFunding?firstDuration:0; timing.fundingPeriod=targetFunding?repeatPeriod:0;
            timing.fundingGold=targetFunding?fundingGold:0; timing.fundingGems=targetFunding?targetFunding->campGems:0;
            const auto fundingOnly=plan3::capital_output(timing);
            const double targetEndingHealth=!targetFunding?c.info.health:
                fundingOnly.fundingCycles>1?repeatedEndingHealth:endingHealth;
            const bool requiresRecovery=targetEndingHealth<post.health;
            cambc::vec2 endingPosition=productivePosition;
            if (targetFunding) {
                const int tile=targetFunding->nominal()->terminal.position.tile;
                endingPosition={tile%cambc::map::width(),tile/cambc::map::width()};
            }
            const bool requiresSpawn=!fieldShop || requiresRecovery;
            const auto shoppingPosition=requiresSpawn ? spawnPosition:endingPosition;
            const int purchaseDelay=requiresSpawn ?
                (spawn.is_set(endingPosition) ? 0:post.recall)+healing_turns(post.health,targetEndingHealth):0;
            const int productiveReturn=requiresSpawn ? returnDistance:0;
            // Field purchases create no free return to the original territory.
            const auto readyPosition=requiresSpawn ? productivePosition:endingPosition;
            timing.purchaseRecoveryReturn=purchaseDelay+productiveReturn;
            if(!guarded(CapitalSite::loopsBefore))return result;
            ++result.work.loopRequests;timing.postPurchaseLoops=future.loops(readyPosition);
            if (future.cutoff) {result.work.unknown.latch(CapitalUnknown::loopsAfterCutoff);result.work.cutoff=true;return result;}
            if (!guarded(CapitalSite::loopsAfter)) {result.work.cutoff=true;return result;}
            if (timing.postPurchaseLoops.empty()) { schedules_.push_back({block,{},false});++result.work.schedulesAppended; continue; }
            const auto accounting=plan3::capital_output(timing);
            plan3::InvestmentForecast evaluation;
            evaluation.available=accounting.feasible;
            evaluation.exactEquipmentReconstruction=true;
            evaluation.output=accounting.checkpoints; evaluation.deferOutput=deferOutputs;
            evaluation.inheritedNextBlock=shard==inherited;
            schedules_.push_back({block,evaluation,false});++result.work.schedulesAppended;
            if (!evaluation.positive_payback()) continue;
            CapitalProposal proposal;
            proposal.block=block; proposal.targetShard=shard; proposal.shardsRemaining=needed;
            proposal.targetProgressGoal=counts[shard]+meta.combine_count*counts[*meta.result]+needed;
            proposal.fundingGroup=!targetFunding || fundingFirst<0 ? -1:c.personal.groups[fundingFirst].key;
            proposal.secondFundingGroup=!targetFunding || fundingSecond<0 ? -1:c.personal.groups[fundingSecond].key;
            proposal.cashoutAltar=cashoutAltar; proposal.readyAt=accounting.readyAt; proposal.accounting=accounting;
            proposal.purchaseRecoveryAt=accounting.readyAt-productiveReturn;
            proposal.requiresSpawnRecovery=requiresSpawn;
            proposal.productiveTile=readyPosition.y*cambc::map::width()+readyPosition.x;
            auto& j=proposal.investmentJob;
            j.id={plan3::JobKind::invest,proposal.fundingGroup,proposal.secondFundingGroup,
                  targetFunding ? targetFunding->id.altarTile:cashoutAltar,static_cast<int>(shard)};
            j.investment=evaluation; j.proposerMask=evaluation.inheritedNextBlock ? 2:0;
            plan3::Outcome outcome;
            outcome.tau=proposal.purchaseRecoveryAt; outcome.goldSpent=timing.blockCost;
            outcome.goldEarned=accounting.fundingCycles*timing.fundingGold+
                              timing.passiveGoldPerTurn*outcome.tau;
            outcome.conversions=accounting.fundingConversions;
            for (const auto& conversion:outcome.conversions) outcome.deltaS+=conversion.gems;
            if(!guarded(CapitalSite::proposalBefore))return result;
            ++result.work.terminalRequests;outcome.terminal=future.terminal(shoppingPosition,post.health);
            if (future.cutoff) {result.work.unknown.latch(CapitalUnknown::proposalAfterCutoff);result.work.cutoff=true;return result;}
            if (!guarded(CapitalSite::proposalAfter)) {result.work.cutoff=true;return result;}
            j.outcomes.push_back(std::move(outcome)); proposals.push_back(std::move(proposal));
        }
        result.complete=true;
        result.schedules=schedules_; result.proposals=std::move(proposals);
        return result;
    }
    // Both modes consume the same completed construction. Council's shopping
    // proposer tests inherited payback, leaving acceptance to its own critic.
    static std::optional<CapitalProposal> select_proposal(const CapitalSchedules& built,
                                                        bool control,bool council=false) {
        if(!built.complete)return std::nullopt;
        if(council) {
            const CapitalProposal* best=nullptr;
            for(const auto& p:built.proposals) {
                const auto& f=p.investmentJob.investment;
                if(!p.investmentJob.complete || !p.investmentJob.routeKnown || !p.investmentJob.nominal())continue;
                if(!f.inheritedNextBlock || !f.positive_payback() ||
                    !std::isfinite(f.checkpoint_utility()))continue;
                if(!best || plan3::strictly_better(f.checkpoint_utility(),best->investmentJob.investment.checkpoint_utility()) ||
                    (plan3::finite_equal(f.checkpoint_utility(),best->investmentJob.investment.checkpoint_utility()) &&
                     p.investmentJob.id<best->investmentJob.id))best=&p;
            }
            return best?std::optional<CapitalProposal>{*best}:std::nullopt;
        }
        const auto best=plan3::best_investment_schedule(built.schedules,control);
        if (!best || best->block==plan3::InvestmentBlock::defer) return std::nullopt;
        for(const auto& p:built.proposals)if(p.block==best->block)return p;
        return std::nullopt;
    }
    std::optional<CapitalProposal> proposals(const ForecastContext& c,
                                            const std::vector<plan3::JobForecast>& menu,bool control) {
        return select_proposal(build_schedules(c,menu),control);
    }
    std::optional<CapitalProposal> council_proposal(const ForecastContext& c,
                                                  const std::vector<plan3::JobForecast>& menu) {
        return select_proposal(build_schedules(c,menu),true,true);
    }
    CapitalReview recheck(const ForecastContext& c,const CapitalProposal& committed,
                         CapitalPhase phase,bool council=false) const {
        CapitalReview result;
        if(!ready())return result;
        const auto& me=cambc::self();const auto counts=me.item_count();
        const auto shard=committed.targetShard;
        const unsigned index=static_cast<unsigned>(shard);
        if(index>=itemCount || !committed.targetProgressGoal)return result;
        const auto& meta=catalogue_[index];
        if(!meta.result || !meta.combine_count || !meta.cost || !meta.purchasable)return result;
        const std::uint64_t have=counts[shard]+std::uint64_t(meta.combine_count)*counts[*meta.result];
        if(have>=committed.targetProgressGoal) {
            result.status=CapitalReviewStatus::target_complete;return result;
        }
        result.shardsRemaining=committed.targetProgressGoal-static_cast<unsigned>(have);
        if(c.cutoff || cambc::fuel_remaining()<45000)return result;
        const auto owned=stats(me,counts);
        auto current=c.fork_without_loop_cache();
        current.info.equipment=owned.equipment;current.info.maximumHealth=owned.health;
        current.strength=owned.strength;current.focus=owned.focus;
        current.camp_damage=owned.campDamage;current.camp_mitigation=owned.campMitigation;
        current.capture_gold=owned.captureGoldMultiplier;current.recall=owned.recall;
        auto reject=[&]() {result.status=CapitalReviewStatus::defer;return result;};
        auto unknown=[&]() {result.status=CapitalReviewStatus::unknown;return result;};
        if(phase.stage<0 || phase.stage>2)return unknown();
        FixedFunding pending;pending.valid=true;pending.end=me.position();pending.health=c.info.health;
        const bool needsFunding=c.info.gold<double(result.shardsRemaining)*meta.cost;
        const bool pendingAcquisition=needsFunding && !phase.prefixPending && phase.fundingCycleStarted && phase.stage<2;
        if(pendingAcquisition) {
            const int first=phase.stage==0?committed.fundingGroup:committed.secondFundingGroup;
            const int second=phase.stage==0?committed.secondFundingGroup:-1;
            pending=fixed_funding(current,first,second,committed.investmentJob.id.altarTile,
                                  me.position(),c.info.health,c.info.carriedGems,false);
        } else if(c.info.carriedGems>0) {
            const int altar=altar_index(c,phase.prefixPending?committed.cashoutAltar:committed.investmentJob.id.altarTile);
            if(altar<0)return reject();
            const auto turns=current.bank_completion(altar);
            if(!turns) {
                if(current.paths.get(current.personal.altar_fields[altar],me.position())==StaticPaths::unknown)return unknown();
                return reject();
            }
            pending.turns=*turns;pending.gems=c.info.carriedGems;
            pending.end=current.reached_approach(altar,me.position());
            if(current.cutoff)return unknown();
        }
        if(!pending.known)return unknown();
        if(!pending.valid)return reject();
        const int H=c.info.horizon(),capacity=std::max(0,c.info.winningGems-c.info.ownPermanentScore);
        const auto defer=current.terminal(pending.end,pending.health);
        if(current.cutoff || cambc::fuel_remaining()<45000)return unknown();
        const std::array<int,3> checkpoints{H/4,H/2,H};
        std::array<double,3> deferOutputs{};
        for(unsigned k=0;k<3;++k) {
            const int converted=pending.turns<=checkpoints[k]?pending.gems:0;
            deferOutputs[k]=std::min<double>(capacity,converted+(pending.turns<=checkpoints[k] ?
                plan3::potential(defer,checkpoints[k]-pending.turns,std::max(0,capacity-converted)):0));
        }
        auto hypothetical=counts;hypothetical[shard]+=result.shardsRemaining;
        while(hypothetical[shard]>=meta.combine_count) {
            hypothetical[shard]-=meta.combine_count;++hypothetical[*meta.result];
        }
        const auto post=stats(me,hypothetical);
        plan3::CapitalTiming timing;
        timing.horizon=H;timing.scoreCapacity=capacity;
        timing.initialGold=c.info.gold;timing.blockCost=double(result.shardsRemaining)*meta.cost;
        timing.passiveGoldPerTurn=cambc::constants::passive_gold;
        timing.settlementTurns=pending.turns;timing.settlementGems=pending.gems;timing.settlementGold=pending.gold;
        double endingHealth=pending.health;
        cambc::vec2 endingPosition=pending.end;
        const double afterPending=timing.initialGold+pending.gold+timing.passiveGoldPerTurn*pending.turns;
        if(afterPending<timing.blockCost) {
            if(committed.fundingGroup<0)return reject();
            // Root retains this exact fixed cycle; do not choose a new funding
            // group or count its already-captured first leg a second time.
            const int back=c.paths.get(c.personal.spawn_field,pending.end);
            if(back==StaticPaths::unknown)return unknown();
            if(back<0)return reject();
            const bool recoverFirst=phase.recovering || (!phase.prefixPending && phase.fundingCycleStarted &&
                pending.turns>0 && pending.health<owned.health);
            const bool alreadySpawned=c.personal.own_spawn.is_set(pending.end);
            const int recovery=recoverFirst ? (alreadySpawned?0:owned.recall)+
                healing_turns(owned.health,pending.health)+(alreadySpawned?0:back):0;
            const auto first=fixed_funding(current,committed.fundingGroup,committed.secondFundingGroup,
                committed.investmentJob.id.altarTile,pending.end,recoverFirst?owned.health:pending.health,0,true);
            if(!first.known)return unknown();
            if(!first.valid)return reject();
            const auto repeat=fixed_funding(current,committed.fundingGroup,committed.secondFundingGroup,
                committed.investmentJob.id.altarTile,pending.end,owned.health,0,true);
            if(!repeat.known)return unknown();
            if(!repeat.valid)return reject();
            const int g=group_index(c,committed.fundingGroup),g2=group_index(c,committed.secondFundingGroup);
            int regen=c.personal.groups[g].max_respawn_timer;
            if(g2>=0)regen=std::max(regen,c.personal.groups[g2].max_respawn_timer);
            int firstWait=phase.fundingCycleStarted?regen:0;
            if(!phase.fundingCycleStarted)for(const int group:{g,g2})if(group>=0) {
                const auto* observed=c.personal.current(group);
                firstWait=std::max(firstWait,observed && observed->lifecycle==Lifecycle::respawning ?
                    std::max(0,observed->respawn_remaining):c.personal.remembered_respawn_minimum(group));
            }
            timing.firstFundingCompletion=first.turns+recovery+firstWait;
            timing.fundingPeriod=repeat.turns+std::max(regen,owned.recall+
                healing_turns(owned.health,repeat.health)+back);
            timing.fundingGold=first.gold;timing.fundingGems=first.gems;
            const auto fundingOnly=plan3::capital_output(timing);
            endingHealth=fundingOnly.fundingCycles>1?repeat.health:first.health;endingPosition=first.end;
        }
        const bool fieldShop=me.champion_name()==cambc::champion_type::tavi;
        const bool requiresSpawn=!fieldShop || endingHealth<post.health;
        if(c.personal.spawn_positions.empty())return unknown();
        const bool atSpawn=c.personal.own_spawn.is_set(endingPosition);
        // A Health purchase at spawn heals its added maximum; field Health
        // does not. Both still require a completed full recovery endpoint.
        const double shopHealth=atSpawn?endingHealth+std::max(0.0,post.health-owned.health):endingHealth;
        const int delay=requiresSpawn ? (atSpawn?0:post.recall)+healing_turns(post.health,shopHealth):0;
        const int productiveReturn=requiresSpawn?c.paths.get(c.personal.spawn_field,pending.end):0;
        if(productiveReturn==StaticPaths::unknown)return unknown();
        if(productiveReturn<0)return reject();
        timing.purchaseRecoveryReturn=delay+productiveReturn;
        const auto productivePosition=requiresSpawn?pending.end:endingPosition;
        const auto shopPosition=requiresSpawn?c.personal.spawn_positions.front():endingPosition;
        auto future=current.fork_without_loop_cache();
        future.info.health=future.info.maximumHealth=post.health;future.info.carriedGems=0;
        future.info.equipment=post.equipment;future.strength=post.strength;future.focus=post.focus;
        future.camp_damage=post.campDamage;future.camp_mitigation=post.campMitigation;future.recall=post.recall;
        future.capture_gold=post.captureGoldMultiplier;
        timing.postPurchaseLoops=future.loops(productivePosition);
        if(future.cutoff || cambc::fuel_remaining()<45000)return unknown();
        const auto accounting=plan3::capital_output(timing);
        result.accounting=accounting;
        result.forecast.available=accounting.feasible && !timing.postPurchaseLoops.empty();
        result.forecast.exactEquipmentReconstruction=true;
        result.forecast.output=accounting.checkpoints;result.forecast.deferOutput=deferOutputs;
        result.forecast.inheritedNextBlock=committed.investmentJob.investment.inheritedNextBlock;
        const double deferUtility=.25*deferOutputs[0]+.25*deferOutputs[1]+.5*deferOutputs[2];
        if(!result.forecast.positive_payback() || (council && !result.forecast.inheritedNextBlock) ||
            (!council && !plan3::strictly_better(result.forecast.checkpoint_utility(),deferUtility)))return reject();
        auto refreshed=committed;refreshed.shardsRemaining=result.shardsRemaining;refreshed.accounting=accounting;
        refreshed.readyAt=accounting.readyAt;refreshed.purchaseRecoveryAt=accounting.readyAt-productiveReturn;
        refreshed.requiresSpawnRecovery=requiresSpawn;
        refreshed.productiveTile=productivePosition.y*c.personal.width+productivePosition.x;
        refreshed.investmentJob.investment=result.forecast;
        plan3::Outcome outcome;outcome.tau=refreshed.purchaseRecoveryAt;outcome.goldSpent=timing.blockCost;
        outcome.goldEarned=pending.gold+accounting.fundingCycles*timing.fundingGold+
            timing.passiveGoldPerTurn*outcome.tau;
        outcome.conversions=accounting.fundingConversions;
        for(const auto& conversion:outcome.conversions)outcome.deltaS+=conversion.gems;
        outcome.terminal=future.terminal(shopPosition,post.health);
        if(future.cutoff)return unknown();
        refreshed.investmentJob.outcomes={std::move(outcome)};refreshed.investmentJob.nominalIndex=0;
        result.proposal=std::move(refreshed);result.status=CapitalReviewStatus::keep;return result;
    }
};
} // namespace plan3_runtime
