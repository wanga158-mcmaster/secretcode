#pragma once
// Four chosen-only economic experts. The loss-based exponential update follows
// Lattimore/Szepesvari, Bandit Algorithms, section11.3. Sleeping experts, bounded
// exploration, deterministic caller tickets, delayed/censored rewards and costs
// are explicit engineering extensions; no adversarial-regret bound is claimed.
// https://tor-lattimore.com/downloads/book/book.pdf
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace plan3_adapter {
inline constexpr unsigned expert_count=4;
using JobKey=std::array<std::int32_t,5>;
enum class Status {known,pending,unavailable,invalid,cutoff,capacity};
enum class Endpoint {completed,aborted,censored};
struct Config {
    double exploration=.125,learningRate=.03125,switchCost=.05,abortCost=.10;
    std::uint32_t rewardPerTurn=64,maximumTurns=4096,maximumGems=1000000,maximumEpisodes=4096;
};
struct Decision {
    Status status=Status::unavailable;
    int expert=-1;
    std::array<double,expert_count> probabilities{};
    std::uint32_t revision=0,ticket=0;
    unsigned available=0;
    bool fixedControl=false;
};
struct Feedback {
    std::uint64_t token=0;
    JobKey job{};
    int started=0,ended=0;
    Endpoint endpoint=Endpoint::censored;
    std::uint32_t convertedLowerBound=0;
    std::optional<std::uint32_t> exactConverted;
};
struct Pending {
    std::uint64_t token=0;
    JobKey job{};
    int started=0;
    unsigned expert=0;
    double probability=1;
    bool switched=false,fixedControl=false;
};
struct ExpertStats {
    std::uint32_t committed=0,completed=0,aborted=0,censored=0,rewardSamples=0,costSamples=0;
    std::uint64_t converted=0,elapsed=0;
    double knownCost=0,normalizedReward=0;
};
struct Update {
    Status status=Status::invalid;
    bool rewardObserved=false,costObserved=false;
    double reward=0,cost=0,loss=0,importanceLoss=0;
};
inline bool valid(const Config& config) {
    return std::isfinite(config.exploration) && config.exploration>=.03125 && config.exploration<=1 &&
        std::isfinite(config.learningRate) && config.learningRate>0 && config.learningRate<=config.exploration/expert_count &&
        std::isfinite(config.switchCost) && config.switchCost>=0 && config.switchCost<=1 &&
        std::isfinite(config.abortCost) && config.abortCost>=0 && config.abortCost<=1 &&
        config.rewardPerTurn>0 && config.rewardPerTurn<=1000000 &&
        config.maximumTurns>0 && config.maximumTurns<=4096 &&
        config.maximumGems>0 && config.maximumGems<=1000000 &&
        config.maximumEpisodes>0 && config.maximumEpisodes<=4096;
}
inline bool valid_key(const JobKey& job) {
    if(job[0]<0 || job[0]>5)return false;
    for(unsigned i=1;i<job.size();++i)if(job[i]<-1)return false;
    return true;
}
class Bandit {
    Config config_;
    std::array<double,expert_count> logWeights_{};
    std::array<ExpertStats,expert_count> statistics_{};
    std::optional<Pending> pending_;
    std::uint32_t revision_=0,episodes_=0;
    std::uint64_t lastToken_=0;
    int lastExpert_=-1;
public:
    explicit Bandit(Config config={}):config_(config) {}
    const Config& config() const {return config_;}
    const auto& log_weights() const {return logWeights_;}
    const auto& statistics() const {return statistics_;}
    const auto& pending() const {return pending_;}
    std::uint32_t revision() const {return revision_;}
    std::uint32_t episodes() const {return episodes_;}
    template<class Ready>
    Decision decide(unsigned available,std::uint32_t ticket,bool fixedControl,Ready&& ready) const {
        Decision result;result.revision=revision_;result.ticket=ticket;
        result.available=available;result.fixedControl=fixedControl;
        if(!ready())return Decision{Status::cutoff};
        if(!valid(config_) || available>15) {result.status=Status::invalid;return result;}
        if(pending_) {result.status=Status::pending;return result;}
        if(episodes_>=config_.maximumEpisodes) {result.status=Status::capacity;return result;}
        if(!available || (fixedControl && !(available&1)))return result;
        if(fixedControl) {
            result.probabilities[0]=1;result.expert=0;result.status=Status::known;return result;
        }
        unsigned count=0;double largest=-std::numeric_limits<double>::infinity();
        for(unsigned i=0;i<expert_count;++i) {
            if(!ready())return Decision{Status::cutoff};
            if(available&(1u<<i)) {++count;largest=std::max(largest,logWeights_[i]);}
        }
        double sum=0;
        for(unsigned i=0;i<expert_count;++i) {
            if(!ready())return Decision{Status::cutoff};
            if(available&(1u<<i)) {result.probabilities[i]=std::exp(logWeights_[i]-largest);sum+=result.probabilities[i];}
        }
        double cumulative=0;const double point=double(ticket)/4294967296.0;
        for(unsigned i=0;i<expert_count;++i) {
            if(!ready()) {Decision empty;empty.status=Status::cutoff;return empty;}
            if(!(available&(1u<<i)))continue;
            result.probabilities[i]=(1-config_.exploration)*result.probabilities[i]/sum+config_.exploration/count;
            cumulative+=result.probabilities[i];
            if(result.expert<0 && point<cumulative)result.expert=static_cast<int>(i);
        }
        // Roundoff at the final CDF boundary can only choose the last live arm.
        if(result.expert<0)for(unsigned i=0;i<expert_count;++i)if(available&(1u<<i))result.expert=static_cast<int>(i);
        result.status=Status::known;return result;
    }
    Decision decide(unsigned available,std::uint32_t ticket,bool fixedControl=false) const {
        return decide(available,ticket,fixedControl,[] {return true;});
    }
    Status commit(const Decision& decision,std::uint64_t token,const JobKey& job,int started) {
        if(decision.status!=Status::known || decision.revision!=revision_ || !token || token<=lastToken_ ||
           !valid_key(job) || started<0)return Status::invalid;
        const auto actual=decide(decision.available,decision.ticket,decision.fixedControl);
        if(actual.status!=Status::known)return actual.status;
        if(actual.expert!=decision.expert || actual.probabilities!=decision.probabilities)return Status::invalid;
        const auto expert=static_cast<unsigned>(actual.expert);
        pending_=Pending{token,job,started,expert,actual.probabilities[expert],
            lastExpert_>=0 && lastExpert_!=actual.expert,actual.fixedControl};
        ++statistics_[expert].committed;++episodes_;++revision_;
        lastToken_=token;lastExpert_=actual.expert;return Status::known;
    }
    Update finish(const Feedback& feedback) {
        Update update;
        if(!pending_ || feedback.token!=pending_->token || feedback.job!=pending_->job ||
           feedback.started!=pending_->started || feedback.ended<feedback.started ||
           std::int64_t(feedback.ended)-feedback.started>config_.maximumTurns ||
           static_cast<unsigned>(feedback.endpoint)>static_cast<unsigned>(Endpoint::censored) ||
           feedback.convertedLowerBound>config_.maximumGems ||
           (feedback.exactConverted && (*feedback.exactConverted>config_.maximumGems ||
               *feedback.exactConverted<feedback.convertedLowerBound)) ||
           (feedback.endpoint==Endpoint::censored && feedback.exactConverted) ||
           (feedback.ended==feedback.started && feedback.exactConverted && *feedback.exactConverted>0))return update;
        const auto current=*pending_;
        const auto elapsed=static_cast<std::uint32_t>(feedback.ended-feedback.started);
        auto stats=statistics_[current.expert];
        if(feedback.endpoint==Endpoint::completed)++stats.completed;
        if(feedback.endpoint==Endpoint::aborted)++stats.aborted;
        if(!feedback.exactConverted)++stats.censored;
        stats.elapsed+=elapsed;
        update.rewardObserved=feedback.exactConverted.has_value();
        if(update.rewardObserved) {
            // Clipping is declared normalization, not a measured rate bound.
            update.reward=std::min(1.0,double(*feedback.exactConverted)/(double(std::max(1u,elapsed))*config_.rewardPerTurn));
            ++stats.rewardSamples;stats.converted+=*feedback.exactConverted;stats.normalizedReward+=update.reward;
        }
        update.cost=(current.switched?config_.switchCost:0)+
            (feedback.endpoint==Endpoint::aborted?config_.abortCost:0);
        update.costObserved=update.cost>0;
        if(update.costObserved) {++stats.costSamples;stats.knownCost+=update.cost;}
        // Unknown reward contributes NO missing-as-zero loss. Its known cost
        // can still be assigned to this committed expert, separately counted.
        update.loss=((update.rewardObserved?1-update.reward:0)+update.cost)/(1+config_.switchCost+config_.abortCost);
        update.importanceLoss=update.loss/current.probability;
        if(!std::isfinite(update.importanceLoss))return Update{};
        auto logs=logWeights_;
        if(!current.fixedControl && (update.rewardObserved || update.costObserved)) {
            logs[current.expert]-=config_.learningRate*update.importanceLoss;
            const double maximum=*std::max_element(logs.begin(),logs.end());
            for(auto& value:logs)value-=maximum; // Common shift preserves every relative weight.
        }
        statistics_[current.expert]=stats;logWeights_=logs;pending_.reset();++revision_;
        update.status=Status::known;return update;
    }
};
} // namespace plan3_adapter
